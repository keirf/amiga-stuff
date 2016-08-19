/*  
 * joymouse.c
 * 
 * Test the two standard 9-pin general-purpose controller ports.
 * Supports three-button mouse, two-button joystick, and seven-button gamepad.
 * 
 * Written & released by Keir Fraser <keir.xen@gmail.com>
 * 
 * This is free and unencumbered software released into the public domain.
 * See the file COPYING for more details, or visit <http://unlicense.org>.
 */

#include "systest.h"

/* Mirror copy of CIAA registers, for bypassing CD32 fWSI floppy expander, 
 * which steals CIA cycles and breaks compatibility. */
static volatile struct amiga_cia * const ciaa_mirror =
    (struct amiga_cia *)0x0bfe003;

struct button {
    uint16_t x, y, w, h; /* box is (x,y) to (x+w,y+h) inclusive */
    const char name[4];  /* name to print on button */
};

const static struct button mouse[] = {
    {  90,  0, 31, 15, "L" },
    { 152,  0, 31, 15, "R" },
    { 121,  0, 31, 15, "M" },
    {  90, 15, 93, 50, "" }
};

const static struct button joystick[] = {
    { 138, 10, 27, 15, "U" },
    { 138, 50, 27, 15, "D" },
    {  96, 30, 27, 15, "L" },
    { 180, 30, 27, 15, "R" },
    {  67,  2, 37, 15, "B1" },
    { 200,  2, 37, 15, "B2" },
};

const static struct button gamepad[] = {
    /* Directional switches */
    {  76, 16, 23, 13, "U" },
    {  76, 44, 23, 13, "D" },
    {  49, 30, 23, 13, "L" },
    { 103, 30, 23, 13, "R" },
    /* Buttons */
    { 233, 36, 23, 13, "B" },
    { 200, 38, 23, 13, "R" },
    { 227, 19, 23, 13, "Y" },
    { 194, 21, 23, 13, "G" },
    { 202,  0, 50, 11, ">>" },
    {  55,  0, 50, 11, "<<" },
    { 130, 55, 50, 11, "||>" },
    /* Dummy buttons (ID bits) */
    {  265, 67,  4,  3, "" }, /* should be FALSE */
    {  269, 67,  4,  3, "" }, /* should be TRUE */
    {  273, 67,  4,  3, "" }  /* should be TRUE */
};

/* Reverse-video effect for highlighting keypresses in the keymap. */
const static uint16_t copper_joymouse[] = {
    0x4407, 0xfffe,
    0x0180, 0x0ddd,
    0x4507, 0xfffe,
    0x0180, 0x0402,
    0x8507, 0xfffe,
    /* reverse video */
    0x0182, 0x0ddd, /* col01 = foreground */
    0x0186, 0x0222, /* col03 = shadow */
    0x0188, 0x0484, /* col04 = previously-pressed highlight */
    0x018a, 0x0ddd, /* col05 = foreground */
    0x018e, 0x0222, /* col07 = shadow */
    0xd007, 0xfffe,
    /* normal video */
    0x0182, 0x0222, /* col01 = shadow */
    0x0186, 0x0ddd, /* col03 = foreground */
    0x0188, 0x04c4, /* col04 = menu-option highlight */
    0x018a, 0x0222, /* col05 = shadow */
    0x018e, 0x0ddd, /* col07 = foreground */
    0xf007, 0xfffe,
    0x0180, 0x0ddd,
    0xf107, 0xfffe,
    0x0180, 0x0103,
    0xffff, 0xfffe,
};

/* Read gamepad button state of specified port using specified ciaa address. */
static uint32_t read_gamepad(uint8_t port, volatile struct amiga_cia *_ciaa)
{
    unsigned int i, j;
    uint32_t state = 0;

    /* Pin 6 clocks the shift register (74LS165). Set as output, LOW, before 
     * we enable the pad's shift-register mode. */
    _ciaa->ddra |= CIAAPRA_FIR0 << port;
    _ciaa->pra &= ~(CIAAPRA_FIR0 << port);

    /* Port pin 5 enables the shift register. Set as output, LOW. Port pin 9 
     * is the shift-register output ('165 pin 9). Set it as input. */
    cust->potgo = port ? 0x2f00 : 0xf200;

    /* Probe 7 buttons (B0-B6), plus 3 ID bits (B7-B9).
     * B7 = '165 pin 11 (parallel input A) = FALSE (pulled high)
     * B8+ = '165 pin 10 (serial input) = TRUE (pulled low) */
    for (i = 0; i < 10; i++) {
        /* Delay for 8 CIA clocks (~10us). */
        for (j = 0; j < 8; j++)
            (void)_ciaa->pra;
        /* Read the shift-register output (port pin 9). */
        if (!(cust->potinp & (port ? 0x4000 : 0x0400)))
            state |= 1u << i;
        /* Clock the shift register: port pin 6 pulsed HIGH. */
        _ciaa->pra |= CIAAPRA_FIR0 << port;
        _ciaa->pra &= ~(CIAAPRA_FIR0 << port);
    }

    /* Return the port to joystick/mouse mode. */
    cust->potgo = 0xff00;
    _ciaa->ddra &= ~(CIAAPRA_FIR0 << port);

    return state;
}

void joymousecheck(void)
{
    char s[80];
    struct char_row r = { .x = 8, .y = 1, .s = s };
    uint16_t joydat[2], newjoydat[2];
    uint8_t key, nr_box = 0;
    unsigned int port, i, j;
    const struct button *box = NULL;
    const static char *names[] = { "Mouse", "Joystick", "Gamepad" };
    enum { T_MOUSE, T_JOYSTICK, T_GAMEPAD };
    struct {
        uint8_t changed, type;
        uint16_t start_x, start_y;
        uint32_t state;
        int mouse_x, mouse_y;
        volatile struct amiga_cia *ciaa;
    } ports[2], *p;

    /* Poke the new copper list at a safe point. */
    wait_bos();
    cust->cop2lc.p = (uint16_t *)copper_joymouse;

    joydat[0] = cust->joy0dat;
    joydat[1] = cust->joy1dat;

    /* Pull-ups on button 2 & 3 inputs (pins 5 & 9 on both ports). */
    cust->potgo = 0xff00;

    print_menu_nav_line();

    sprintf(s, "-- Mouse/Joystick/Gamepad Test --");
    print_line(&r);
    r.x = 0;
    r.y += 2;

    sprintf(s, "%35s%s", "$1 Port 1$ -", "$2 Port 2$ -");
    print_line(&r);
    r.y++;

    ports[0].changed = 1;
    ports[0].type = T_MOUSE;
    ports[0].start_x = 40;
    ports[0].start_y = 65;
    ports[1] = ports[0];
    ports[1].start_x = 320;

    while (!do_exit) {

        /* Key handler. */
        if ((key = keycode_buffer) != 0) {
            keycode_buffer = 0;
            do_exit |= (key == K_ESC);
            key -= K_F1;
            if (key < 2) {
                /* Controller type change for one of the two ports. */
                p = &ports[key];
                p->changed = 1;
                /* Cycle through the types. */
                if (++p->type > T_GAMEPAD)
                    p->type = 0;
            }
        }

        /* Check for changes to controller types and redraw the controller
         * graphics as necessary. */
        for (port = 0; port < 2; port++) {

            p = &ports[port];
            if (!p->changed)
                continue;

            p->changed = 0;
            p->state = 0;

            /* Update the label text. */
            sprintf(s, "%10s", names[p->type]);
            print_text_box(13 + (port ? 35 : 0), 3, s);

            /* Clear the old graphics. */
            clear_rect(p->start_x, p->start_y, 310, 93, 7);

            /* Get button locations for the new controller type. */
            switch (p->type) {
            case T_MOUSE:
                box = mouse;
                nr_box = ARRAY_SIZE(mouse);
                break;
            case T_JOYSTICK:
                box = joystick;
                nr_box = ARRAY_SIZE(joystick);
                break;
            case T_GAMEPAD:
                /* Check for gamepad ID in bits 7+ of the shift register. 
                 * We expect to see ..110 and if this is missing then either:
                 * 1. There is no gamepad connected; or 
                 * 2. The POT line responds too slowly due to capacitive 
                 *    load (we don't currently detect and handle this); or 
                 * 3. CIA emulation is preventing us from clocking the shift 
                 *    register. This occurs with the CD32 fWSI floppy expansion
                 *    which steals CIA access cycles and does not emulate
                 *    FIRE0/FIRE1 in output mode. We can detect and work around
                 *    this because fWSI fully decodes CIA addresses whereas
                 *    Akiko responds at CIA mirror locations. */
                p->ciaa = ciaa;
                if ((read_gamepad(port, ciaa) >> 7) == 6) {
                    sprintf(s, "Pad Detected");
                    print_text_box(4 + (port ? 35 : 0), 12, s);
                } else if ((read_gamepad(port, ciaa_mirror) >> 7) == 6) {
                    p->ciaa = ciaa_mirror;
                    sprintf(s, "Pad Detected @ CIA Mirror");
                    print_text_box(port ? 35 : 0, 12, s);
                    sprintf(s, "(CD32 + fWSI Floppy Exp?)");
                    print_text_box(port ? 35 : 0, 13, s);
                } else {
                    sprintf(s, "Pad Not Detected");
                    print_text_box(2 + (port ? 35 : 0), 12, s);
                }
                box = gamepad;
                nr_box = ARRAY_SIZE(gamepad);
                hollow_rect(p->start_x + 30, p->start_y + 11, 248, 60, 1<<1);
                break;
            }

            /* Draw the buttons one at a time. */
            for (i = 0; i < nr_box; i++, box++) {
                /* Button outline box. */
                unsigned int x = p->start_x + box->x;
                unsigned int y = p->start_y + box->y;
                hollow_rect(x, y, box->w + 1, box->h + 1, 1<<1);
                /* Centre the button label text in the button box. */
                for (j = 0; box->name[j]; j++)
                    continue;
                x += (box->w+1) / 2;
                x -= j * 4;
                y += (box->h+1) / 2;
                y -= 4;
                for (j = 0; box->name[j]; j++) {
                    draw_box_label(x, y, box->name[j]);
                    x += 8;
                }
            }
        }

        newjoydat[0] = cust->joy0dat;
        newjoydat[1] = cust->joy1dat;

        /* Draw mouse cursors. */
        wait_bos(); /* avoids flicker */
        for (port = 0; port < 2; port++) {
            p = &ports[port];
            if (p->type != T_MOUSE)
                continue;
            box = &mouse[3];
            /* Clear the old cursor location. */
            clear_rect(p->mouse_x + p->start_x + box->x + 1,
                       p->mouse_y/2 + p->start_y + box->y + 1,
                       4, 2, 1<<1);
            /* Update mouse coordinates. */
            p->mouse_x += (int8_t)(newjoydat[port] - joydat[port]);
            p->mouse_y += (int8_t)((newjoydat[port]>>8) - (joydat[port]>>8));
            p->mouse_x = min_t(int, max_t(int, p->mouse_x, 0), mouse[3].w-5);
            p->mouse_y = min_t(int, max_t(int, p->mouse_y, 0), mouse[3].h*2-5);
            /* Draw the new cursor location. */
            fill_rect(p->mouse_x + p->start_x + box->x + 1,
                      p->mouse_y/2 + p->start_y + box->y + 1,
                      4, 2, 1<<1);
        }

        /* Draw button states. */
        for (port = 0; port < 2; port++) {

            uint32_t xorstate;

            p = &ports[port];

            xorstate = p->state;
            p->state = 0;
            joydat[port] = newjoydat[port];

            if (p->type == T_GAMEPAD) {

                /* Gamepad Buttons. */
                p->state = read_gamepad(port, p->ciaa);

            } else {

                /* Joystick/Mouse Buttons. */
                uint8_t buttons = cust->potinp >> (port ? 12 : 8);
                /* Button 3 (MMB): Port pin 5 */
                p->state |= !(buttons & 1);
                p->state <<= 1;
                /* Button 2 (Fire 2, RMB): Port pin 9 */
                p->state |= !(buttons & 4);
                p->state <<= 1;
                /* Button 1 (Fire 1, LMB): Port pin 6 */
                p->state |= !(ciaa->pra & (CIAAPRA_FIR0 << port));

            }

            if (p->type != T_MOUSE) {
                /* Joystick/Gamepad Directional Switches. */
                uint16_t joy = joydat[port];
                p->state <<= 1;
                p->state |= !!(joy & 2); /* Right */
                p->state <<= 1;
                p->state |= !!(joy & 0x200); /* Left */
                p->state <<= 1;
                p->state |= !!((joy & 1) ^ ((joy & 2) >> 1)); /* Down */
                p->state <<= 1;
                p->state |= !!((joy & 0x100) ^ ((joy & 0x200) >> 1)); /* Up */
            }

            xorstate ^= p->state;

            switch (p->type) {
            case T_MOUSE:
                box = mouse;
                nr_box = 3;
                break;
            case T_JOYSTICK:
                box = joystick;
                nr_box = ARRAY_SIZE(joystick);
                break;
            case T_GAMEPAD:
                box = gamepad;
                nr_box = ARRAY_SIZE(gamepad);
                break;
            }

            /* Update the button boxes. */
            for (i = 0; i < nr_box; i++, box++) {
                uint8_t bpls, setclr;
                /* Skip buttons that have not changed state. */
                if (!(xorstate & (1u<<i)))
                    continue;
                /* Fill or clear depending on new button state. */
                setclr = !!(p->state & (1u<<i));
                bpls = setclr ? (1<<2)|(1<<0) : (1<<0); /* sticky bpl[2] */
                draw_rect(p->start_x + box->x + 1,
                          p->start_y + box->y + 1,
                          box->w-1, box->h-1, bpls, setclr);
            }
        }
    }

    /* Clean up. */
    cust->potgo = 0x0000;
}

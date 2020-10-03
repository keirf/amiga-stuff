/*  
 * serpar.c
 * 
 * Test the standard Amiga DB25 parallel and RS232/serial ports.
 * Requires a loopback dongle on each port for automated testing.
 * 
 * Parallel Port Dongle:
 *  Connect 1-10, 2-3, 4-5, 6-7, 9-11, 8-12-13.
 *  14[+5V] -> LED+270ohm -> 18[GND]
 * 
 * Serial Port Dongle:
 *  Connect 2-3, 4-5-6, 8-20-22.
 *  9[+12V] -> LED+1Kohm ->  7[GND]
 *  7[GND]  -> LED+1Kohm -> 10[-12V]
 * 
 * Written & released by Keir Fraser <keir.xen@gmail.com>
 * 
 * This is free and unencumbered software released into the public domain.
 * See the file COPYING for more details, or visit <http://unlicense.org>.
 */

#include "testkit.h"

const static char progress_chars[] = "|/-\\";

struct pin {
    uint16_t x, y, w, h; /* box is (x,y) to (x+w,y+h) inclusive */
    char name[4];        /* name to print on button */
};

static struct pin *db25;

#define RED   1<<0
#define WHITE 1<<1
#define GREEN 1<<2

#define RED_COL   0xf00
#define GREEN_COL 0x2c2
#define BLUE_COL  0x00f

static uint16_t copper_serpar[] = {
    0x4407, 0xfffe,
    0x0180, 0x0ddd,
    0x4507, 0xfffe,
    0x0180, 0x0402,
    /* pin map with highlights */
    0x0184, 0x0ddd, /* col02 = foreground */
    0x0186, 0x0ddd, /* col03 = foreground */
    0x018c, 0x0ddd, /* col06 = foreground */
    0x0182, RED_COL, /* col01 */
    0x0188, BLUE_COL, /* col04 */
    0x8807, 0xfffe,
    /* normal video */
    0x0182, 0x0222, /* col01 = shadow */
    0x0184, 0x0ddd, /* col02 = foreground */
    0x0186, 0x0ddd, /* col03 = foreground */
    0x0188, 0x04c4, /* col04 = menu-option highlight */
    0x018a, 0x0222, /* col05 = shadow */
    0x018c, 0x0ddd, /* col06 = foreground */
    0x018e, 0x0ddd, /* col07 = foreground */
    0xf007, 0xfffe,
    0x0180, 0x0ddd,
    0xf107, 0xfffe,
    0x0180, 0x0103,
    0xffff, 0xfffe,
};

static volatile unsigned int flag_count;
void ciaa_flag_IRQ(void)
{
    flag_count++;
}

static void set_pin_defaults(void)
{
    ciaa->ddrb = 0x00;
    ciab->ddra = 0xc0;
    ciaa->prb = 0xff;
    ciab->pra = 0xff;
}

static void draw_db25(const uint8_t *groups)
{
    unsigned int i, j, x, y;
    struct pin *pin;
    const uint8_t *p;
    uint16_t *cop;

    /* Initial 'OK' highlight is blue, indicates 'not confident yet'. */
    for (cop = copper_serpar; *cop != 0x188; cop += 2)
        continue;
    cop[1] = BLUE_COL;

    /* Draw the pins one at a time. */
    for (i = 0; i < 25; i++) {
        /* Pin outline box. */
        pin = &db25[i];
        x = pin->x;
        y = pin->y;
        hollow_rect(x, y, pin->w + 1, pin->h + 1, WHITE);
        /* Centre the label text in the pin box. */
        for (j = 0; pin->name[j]; j++)
            continue;
        x += (pin->w+1) / 2;
        x -= j * 4;
        y += (pin->h+1) / 2;
        y -= 4;
        print_label(x, y, 1, pin->name);
    }

    /* Draw connections within groups of pins. */
    p = groups;
    while (p && *p) {
        unsigned int x_min = ~0, x_max = 0, y_min = ~0, y_max = 0;
        unsigned int y_bar, nr = p[0]&15;
        /* Find horizontal bounds of connection bar [x_min,x_max]. */
        for (i = 0; i < nr; i++) {
            pin = &db25[p[i+1]-1];
            x = pin->x + (pin->w+1)/2;
            y = pin->y + (pin->h+1)/2;
            x_min = min(x_min, x);
            y_min = min(y_min, y);
            x_max = max(x_max, x);
            y_max = max(y_max, y);
        }
        /* Find vertical position of connection bar [y_bar]. */
        y_bar = ((p[0]>>4) < 3)
            ? y_min - 12 - (2-(p[0]>>4))*3
            : y_min + 12 + ((p[0]>>4)-3)*3;
        /* Draw the connecting bar. */
        fill_rect(x_min, y_bar, x_max-x_min+1, 1, WHITE);
        /* Draw each pin's connection to the bar. */
        for (i = 0; i < nr; i++) {
            pin = &db25[p[i+1]-1];
            x = pin->x + (pin->w+1)/2;
            if (pin->y < y_bar) {
                y = pin->y + pin->h;
                fill_rect(x, y, 1, y_bar-y+1, WHITE);
            } else {
                y = pin->y;
                fill_rect(x, y_bar, 1, y-y_bar+1, WHITE);
            }
        }
        /* Highlight each connected pin. */
        for (i = 0; i < nr; i++) {
            pin = &db25[p[i+1]-1];
            fill_rect(pin->x, pin->y, pin->w + 1, pin->h + 1, GREEN);
        }
        /* Skip to the next set of connected pins. */
        p += nr + 1;
    }
}

static unsigned int prev_vblank, bad_vbl[25];
static uint8_t turned_green, progress;

static void test_loop_prep(struct char_row *r)
{
    prev_vblank = vblank_count = 0;
    turned_green = progress = 0;
    memset(bad_vbl, 0, sizeof(bad_vbl));

    r->x = 4;
    r->y = 6;
    sprintf((char *)r->s, "BLUE=probing, RED=bad, GREEN=good ... ");
    print_line(r);
}

static void test_loop_handle_vblank(void)
{
    struct pin *pin;
    unsigned int i;

    if (vblank_count == prev_vblank)
        return;
    prev_vblank = vblank_count;

    /* Update the "I'm doing something" indicator. */
    if (!(progress++ & 7)) {
        char s[3] = "  ";
        s[0] = progress_chars[(progress>>3)&3];
        print_text_box(42, 6, s);
    }

    /* After an initial settling period, any pins which have seen no errors 
     * turn from BLUE to GREEN. */
    if (!turned_green && (prev_vblank > 150)) {
        uint16_t *cop;
        turned_green = 1;
        for (cop = copper_serpar; *cop != 0x188; cop += 2)
            continue;
        cop[1] = GREEN_COL;
    }

    /* Bad pins which haven't seen an error in a while turn good. */
    for (i = 0; i < ARRAY_SIZE(bad_vbl); i++) {
        if (!bad_vbl[i])
            continue;
        if ((prev_vblank - bad_vbl[i]) > 150) {
            bad_vbl[i] = 0;
            pin = &db25[i];
            clear_rect(pin->x, pin->y, pin->w + 1, pin->h + 1, RED);
            fill_rect(pin->x, pin->y, pin->w + 1, pin->h + 1, GREEN);
        }
    }
}

static void test_loop_handle_bad_pins(uint32_t bad)
{
    struct pin *pin;
    unsigned int i;

    /* Highlight each bad pin in RED. */
    for (i = 1; i <= ARRAY_SIZE(bad_vbl); i++) {
        if (!(bad & (1u<<i)))
            continue;
        /* Only highlight if not already highlighted. */
        if (!bad_vbl[i-1]) {
            pin = &db25[i-1];
            clear_rect(pin->x, pin->y, pin->w + 1, pin->h + 1, GREEN);
            fill_rect(pin->x, pin->y, pin->w + 1, pin->h + 1, RED);
        }
        /* Remember last time we saw this pin as bad. Used for timing out the
         * badness (return to green).  */
        bad_vbl[i-1] = vblank_count;
    }
}

static void parallel_auto(void)
{
    char s[80];
    struct char_row r = { .s = s };
    uint16_t cur = 0, group = 0, seen, bad;

    const static uint16_t bit_groups[] = {
        0x0003, 0x000c, 0x0030, 0x0640, 0x0180
    };

    const static uint8_t groups[] = {
        2+(3<<4), 2, 3, 
        2+(3<<4), 4, 5,
        2+(3<<4), 6, 7,
        3+(5<<4), 8, 12, 13,
        2+(3<<4), 9, 11,
        2+(2<<4), 1, 10,
        0 };

    draw_db25(groups);
    ciaa->icr = CIAICR_SETCLR | CIAICR_FLAG;

    r.x = 0;
    r.y = 8;
    sprintf(s, "  1 ( O): STROBE [A.PC]     11 (IO): BUSY [B.PA0]");
    print_line(&r);
    r.y++;
    sprintf(s, "2-9 (IO): D0-D7  [A.PBx]    12 (IO): POUT [B.PA1]");
    print_line(&r);
    r.y++;
    sprintf(s, " 10 (I ): ACK    [A.FLG]    13 (IO): SEL  [B.PA2]");
    print_line(&r);
    r.y++;

    test_loop_prep(&r);

    while (!do_exit && (keycode_buffer != K_ESC)) {

        test_loop_handle_vblank();

        /* Pick a pin in some group to set as output. */
        do {
            cur <<= 1;
            if (cur == 0) {
                if (++group == ARRAY_SIZE(bit_groups))
                    group = 0;
                cur = 1;
            }
        } while (!(bit_groups[group] & cur));

        /* Test #1: Set the chosen pin HIGH. */
        set_pin_defaults();
        ciaa->ddrb = (uint8_t)cur;
        ciab->ddra = 0xc0 | (uint8_t)(cur >> 8);
        delay_ms(1);
        /* Any LOW pins are bad (all inputs are pulled or driven HIGH). */
        seen = ((uint16_t)(ciab->pra & 7) << 8) | ciaa->prb;
        bad = seen ^ 0x7ff;

        /* Test #2: Set the chosen pin LOW. */
        ciaa->prb = (uint8_t)~cur;
        ciab->pra = (uint8_t)~(cur >> 8);
        delay_ms(1);
        /* Pin should be LOW iff it is in the output pin's group. */
        seen = ((uint16_t)(ciab->pra & 7) << 8) | ciaa->prb;
        bad |= seen ^ 0x7ff ^ bit_groups[group];

        /* Convert bit positions in PRx to pin numbers. */
        bad = ((bad&0xff)<<2) | ((bad&0x700)<<3);

        /* Check if STROBE/ACK is working. */
        if (!flag_count)
            bad |= (1u<<1) | (1u<<10);
        flag_count = 0;

        test_loop_handle_bad_pins(bad);
    }

    /* Clean up. */
    ciaa->icr = CIAICR_FLAG;
}

static void serial_auto(void)
{
    char s[80];
    struct char_row r = { .s = s };
    uint32_t bad;
    uint8_t seen;

    const static uint8_t groups[] = {
        2+(4<<4), 2, 3, 
        3+(4<<4), 4, 5, 6,
        3+(4<<4), 8, 20, 22,
        0 };

    draw_db25(groups);

    r.x = 0;
    r.y = 8;
    sprintf(s, "2:TxD [O,Paula]  5:CTS [I,B.PA4]  20:DTR [O,B.PA7]");
    print_line(&r);
    r.y++;
    sprintf(s, "3:RxD [I,Paula]  6:DSR [I,B.PA3]  22:RI  [I,B.PA2]");
    print_line(&r);
    r.y++;
    sprintf(s, "4:RTS [O,B.PA6]  8:CD  [I,B.PA5]");
    print_line(&r);
    r.y++;

    test_loop_prep(&r);

    while (!do_exit && (keycode_buffer != K_ESC)) {

        test_loop_handle_vblank();

        bad = 0;

        /* Test #1: Pin 20 LOW. */
        set_pin_defaults();
        ciab->pra = 0x7f; /* pin 20 LOW, pin 4 HIGH */
        cust->adkcon = 0x0800; /* pin 2 HIGH */
        delay_ms(1);
        seen = ciab->pra;
        /* Group 20,8,22 should be LOW */
        if ((seen & (1u<<7)))
            bad |= 1u<<20;
        if ((seen & (1u<<5)))
            bad |= 1u<<8;
        if ((seen & (1u<<2)))
            bad |= 1u<<22;
        /* Group 2,3 should be HIGH */
        if (!(cust->serdatr & (1u<<11)))
            bad |= (1u<<2)|(1u<<3);
        /* Group 4,5,6 should be HIGH */
        if (!(seen & (1u<<6)))
            bad |= 1u<<4;
        if (!(seen & (1u<<4)))
            bad |= 1u<<5;
        if (!(seen & (1u<<3)))
            bad |= 1u<<6;

        /* Test #2: Pins 2 and 4 LOW. */
        ciab->pra = 0xbf; /* pin 20 HIGH, pin 4 LOW */
        cust->adkcon = 0x8800; /* pin 2 LOW */
        delay_ms(1);
        seen = ciab->pra;
        /* Group 20,8,22 should be LOW */
        if (!(seen & (1u<<7)))
            bad |= 1u<<20;
        if (!(seen & (1u<<5)))
            bad |= 1u<<8;
        if (!(seen & (1u<<2)))
            bad |= 1u<<22;
        /* Group 2,3 should be HIGH */
        if ((cust->serdatr & (1u<<11)))
            bad |= (1u<<2)|(1u<<3);
        /* Group 4,5,6 should be HIGH */
        if ((seen & (1u<<6)))
            bad |= 1u<<4;
        if ((seen & (1u<<4)))
            bad |= 1u<<5;
        if ((seen & (1u<<3)))
            bad |= 1u<<6;

        test_loop_handle_bad_pins(bad);
    }

    /* Clean up. */
    cust->adkcon = 0x0800;
}

static void dongle_instructions(void)
{
    struct char_row r = { 0 };

    copperlist_default();

    r.x = 5;
    r.y = 1;
    r.s = "-- Dongle Build Instructions --";
    print_line(&r);

    r.x = 0;
    r.y = 3;
    r.s = "-- Serial-Port Dongle --";
    print_line(&r);
    r.y++;
    r.s = "Connect pins 2-3, 4-5-6, 8-20-22";
    print_line(&r);
    r.y++;
    r.s = "Pin 9[+12V] -> LED + 1Kohm -> Pin 7[GND]";
    print_line(&r);
    r.y++;
    r.s = "Pin 7[GND]  -> LED + 1Kohm -> Pin 10[-12V]";
    print_line(&r);

    r.y += 2;
    r.s = "-- Parallel-Port Dongle --";
    print_line(&r);
    r.y++;
    r.s = "Connect pins 1-10, 2-3, 4-5, 6-7, 9-11, 8-12-13";
    print_line(&r);
    r.y++;
    r.s = "Pin 14[+5V] -> LED + 270ohm -> Pin 18[GND]";
    print_line(&r);

    r.y += 2;
    r.s = "(LEDs indicate that port power pins are operational)";
    print_line(&r);

    while (!do_exit && (keycode_buffer != K_ESC))
        continue;
}

void serparcheck(void)
{
    char s[80];
    struct char_row r = { .s = s };
    uint8_t key = 0;
    unsigned int i;
    struct pin *pin;

    /* Construct the DB25 pin map. */
    db25 = allocmem(25 * sizeof(*db25));
    for (i = 0; i < 25; i++) {
        pin = &db25[i];
        if (i < 13) {
            pin->x = 40 * i;
            pin->y = 0;
        } else {
            pin->x = 20 + 40 * (i - 13);
            pin->y = 30;
        }
        pin->x += 60;
        pin->y += 20;
        pin->w = 23;
        pin->h = 13;
        sprintf(pin->name, "%u", i+1);
    }

    print_menu_nav_line();

    while (!do_exit) {

        r.x = 7;
        r.y = 1;
        sprintf(s, "-- Serial & Parallel Ports --");
        print_line(&r);

        r.x = 0;
        r.y += 2;
        sprintf(s, "$1 Loopback Serial Test (requires dongle)$");
        print_line(&r);
        r.y++;
        sprintf(s, "$2 Loopback Parallel Test (requires dongle)$");
        print_line(&r);
        r.y++;
        sprintf(s, "$3 Dongle Build Guide$");
        print_line(&r);
        r.y++;

        do {
            while (!do_exit && !(key = keycode_buffer))
                continue;
            keycode_buffer = 0;
            if (key == K_ESC)
                do_exit = 1;
            key -= K_F1;
        } while (!do_exit && (key >= 3));

        if (do_exit)
            break;

        clear_text_rows(0, 12);
        copperlist_set(copper_serpar);
        set_pin_defaults();

        switch (key) {
        case 0:
            serial_auto();
            break;
        case 1:
            parallel_auto();
            break;
        case 2:
            dongle_instructions();
            break;
        }

        set_pin_defaults();
        clear_rect(0, 10, xres, 10, 7);
        clear_text_rows(0, 12);
        copperlist_default();
        keycode_buffer = 0;
    }
}

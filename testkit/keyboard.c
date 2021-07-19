/*
 * keyboard.c
 * 
 * Test the standard Amiga keyboard interface.
 * 
 * Written & released by Keir Fraser <keir.xen@gmail.com>
 * 
 * This is free and unencumbered software released into the public domain.
 * See the file COPYING for more details, or visit <http://unlicense.org>.
 */

#include "testkit.h"

/* Keyboard IRQ: Keyboard variables. */
volatile uint8_t keycode_buffer, do_exit;
volatile uint8_t key_pressed[128];
/* A buffer ring for holding up to 1024 consecutive keycodes without loss.
 * Avoids losing key events during the keyboard test. Note no use of volatile:
 * make sure to use barrier() as needed to synchronise with the irq handler. */
static uint8_t keycode_ring[1024];
static uint16_t keycode_prod, keycode_cons;

uint8_t keyboard_IRQ(void)
{
    uint16_t t_s;
    uint8_t keycode, key;
    static uint8_t prev_key;

    /* Grab the keycode and begin handshake. */
    keycode = ~ciaa->sdr;
    ciaa->cra |= CIACRA_SPMODE; /* start the handshake */
    t_s = get_ciaatb();

    /* Decode the keycode, detect Ctrl + L.Alt. */
    key = (keycode >> 1) | (keycode << 7); /* ROR 1 */
    if ((prev_key == K_CTRL) && (key == K_LALT))
        do_exit = 1; /* Ctrl + L.Alt */
    prev_key = key;

    /* Place key-down events in the basic keycode buffer. */
    if (!(key & 0x80))
        keycode_buffer = key;

    /* Maintain an array of which keys are currently pressed. */
    key_pressed[key & 0x7f] = !(key & 0x80);

    /* Place all keycodes in the buffer ring if there is space. */
    if ((keycode_prod - keycode_cons) != ARRAY_SIZE(keycode_ring))
        keycode_ring[keycode_prod++ & (ARRAY_SIZE(keycode_ring)-1)] = key;

    /* Wait to finish handshake over the serial line. We wait for 65 CIA ticks,
     * which is approx 90us: Longer than the 85us minimum dictated by the
     * HRM. */
    while ((uint16_t)(t_s - get_ciaatb()) < 65)
        continue;
    ciaa->cra &= ~CIACRA_SPMODE; /* finish the handshake */

    return key;
}

/* List of keycaps and their locations, for drawing the keymap. 
 * Array is indexed by raw keycode. */
const static struct keycap {
    uint16_t x, y, w, h; /* box is (x,y) to (x+w,y+h) inclusive */
    const char name[4];  /* name to print on keycap */
} keymap[128] = {
    [0x45] = {   0,   0, 25, 15, "Esc" },
    [0x50] = {  35,   0, 31, 15, "F1" },
    [0x51] = {  66,   0, 31, 15, "F2" },
    [0x52] = {  97,   0, 31, 15, "F3" },
    [0x53] = { 128,   0, 31, 15, "F4" },
    [0x54] = { 159,   0, 31, 15, "F5" },
    [0x55] = { 200,   0, 31, 15, "F6" },
    [0x56] = { 231,   0, 31, 15, "F7" },
    [0x57] = { 262,   0, 31, 15, "F8" },
    [0x58] = { 293,   0, 31, 15, "F9" },
    [0x59] = { 324,   0, 31, 15, "F10" },

    [0x00] = {   0,  20, 35, 15, "`" },
    [0x01] = {  35,  20, 25, 15, "1" },
    [0x02] = {  60,  20, 25, 15, "2" },
    [0x03] = {  85,  20, 25, 15, "3" },
    [0x04] = { 110,  20, 25, 15, "4" },
    [0x05] = { 135,  20, 25, 15, "5" },
    [0x06] = { 160,  20, 25, 15, "6" },
    [0x07] = { 185,  20, 25, 15, "7" },
    [0x08] = { 210,  20, 25, 15, "8" },
    [0x09] = { 235,  20, 25, 15, "9" },
    [0x0a] = { 260,  20, 25, 15, "0" },
    [0x0b] = { 285,  20, 25, 15, "-" },
    [0x0c] = { 310,  20, 25, 15, "=" },
    [0x0d] = { 335,  20, 25, 15, "\\" },
    [0x41] = { 360,  20, 25, 15, "BS" },

    [0x42] = {   0,  35, 47, 15, "Tab" },
    [0x10] = {  47,  35, 25, 15, "Q" },
    [0x11] = {  72,  35, 25, 15, "W" },
    [0x12] = {  97,  35, 25, 15, "E" },
    [0x13] = { 122,  35, 25, 15, "R" },
    [0x14] = { 147,  35, 25, 15, "T" },
    [0x15] = { 172,  35, 25, 15, "Y" },
    [0x16] = { 197,  35, 25, 15, "U" },
    [0x17] = { 222,  35, 25, 15, "I" },
    [0x18] = { 247,  35, 25, 15, "O" },
    [0x19] = { 272,  35, 25, 15, "P" },
    [0x1a] = { 297,  35, 25, 15, "[" },
    [0x1b] = { 322,  35, 25, 15, "]" },
    [0x44] = { 355,  35, 30, 30, "Ret" },

    [0x63] = {   0,  50, 30, 15, "Ctl" },
    [0x62] = {  30,  50, 25, 15, "CL" },
    [0x20] = {  55,  50, 25, 15, "A" },
    [0x21] = {  80,  50, 25, 15, "S" },
    [0x22] = { 105,  50, 25, 15, "D" },
    [0x23] = { 130,  50, 25, 15, "F" },
    [0x24] = { 155,  50, 25, 15, "G" },
    [0x25] = { 180,  50, 25, 15, "H" },
    [0x26] = { 205,  50, 25, 15, "J" },
    [0x27] = { 230,  50, 25, 15, "K" },
    [0x28] = { 255,  50, 25, 15, "L" },
    [0x29] = { 280,  50, 25, 15, ";" },
    [0x2a] = { 305,  50, 25, 15, "'" },
    [0x2b] = { 330,  50, 25, 15, " " },

    [0x60] = {   0,  65, 40, 15, "Sh." },
    [0x30] = {  40,  65, 25, 15, " " },
    [0x31] = {  65,  65, 25, 15, "Z" },
    [0x32] = {  90,  65, 25, 15, "X" },
    [0x33] = { 115,  65, 25, 15, "C" },
    [0x34] = { 140,  65, 25, 15, "V" },
    [0x35] = { 165,  65, 25, 15, "B" },
    [0x36] = { 190,  65, 25, 15, "N" },
    [0x37] = { 215,  65, 25, 15, "M" },
    [0x38] = { 240,  65, 25, 15, "," },
    [0x39] = { 265,  65, 25, 15, "." },
    [0x3a] = { 290,  65, 25, 15, "/" },
    [0x61] = { 315,  65, 70, 15, "Sh." },

    [0x64] = {  20,  80, 30, 15, "Alt" },
    [0x66] = {  50,  80, 30, 15, "Am" },
    [0x40] = {  80,  80, 220, 15, "Spc" },
    [0x67] = { 300,  80, 30, 15, "Am" },
    [0x65] = { 330,  80, 30, 15, "Alt" },

    [0x46] = { 395,  20, 37, 15, "Del" },
    [0x5f] = { 432,  20, 38, 15, "Hlp" },
    [0x4c] = { 420,  50, 25, 15, "^" },
    [0x4f] = { 395,  65, 25, 15, "<" },
    [0x4d] = { 420,  65, 25, 15, "\\/" },
    [0x4e] = { 445,  65, 25, 15, ">" },

    [0x5a] = { 480,  20, 25, 15, "(" },
    [0x5b] = { 505,  20, 25, 15, ")" },
    [0x5c] = { 530,  20, 25, 15, "/" },
    [0x5d] = { 555,  20, 25, 15, "*" },
    [0x3d] = { 480,  35, 25, 15, "7" },
    [0x3e] = { 505,  35, 25, 15, "8" },
    [0x3f] = { 530,  35, 25, 15, "9" },
    [0x4a] = { 555,  35, 25, 15, "-" },
    [0x2d] = { 480,  50, 25, 15, "4" },
    [0x2e] = { 505,  50, 25, 15, "5" },
    [0x2f] = { 530,  50, 25, 15, "6" },
    [0x5e] = { 555,  50, 25, 15, "+" },
    [0x1d] = { 480,  65, 25, 15, "1" },
    [0x1e] = { 505,  65, 25, 15, "2" },
    [0x1f] = { 530,  65, 25, 15, "3" },
    [0x43] = { 555,  65, 25, 30, "En" },
    [0x0f] = { 480,  80, 50, 15, "0" },
    [0x3c] = { 530,  80, 25, 15, "." },
};

/* Reverse-video effect for highlighting keypresses in the keymap. */
static uint16_t copper_kbd[] = {
    /* reverse video */
    0x0182, 0x0ddd, /* col01 = foreground */
    0x0186, 0x0222, /* col03 = shadow */
    0x0188, 0x0484, /* col04 = previously-pressed highlight */
    0x018a, 0x0ddd, /* col05 = foreground */
    0x018e, 0x0222, /* col07 = shadow */
    0x4407, 0xfffe,
    0x0180, 0x0ddd,
    0x4507, 0xfffe,
    0x0180, 0x0402,
    0xbb07, 0xfffe,
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

void kbdcheck(void)
{
    char s[80], num[5];
    struct char_row r = { .x = 8, .y = 10, .s = s };
    const struct keycap *cap;
    unsigned int i, l, x, y;

    copperlist_set(copper_kbd);

    /* Draw the keymap. This resides in bitplane 1. We then draw filled boxes
     * in bitplane 0 to indicate keys which are currently pressed. The reverse
     * video effect causes the key to be highlighted with the key name still
     * visible. */
    hollow_rect(20, 8, 601, 106, 1<<1);
    for (i = 0; i < ARRAY_SIZE(keymap); i++) {
        cap = &keymap[i];
        if (!cap->h)
            continue;
        /* Draw the outline rectangle. */
        x = 30 + cap->x;
        y = 13 + cap->y;
        hollow_rect(x, y, cap->w+1, cap->h+1, 1<<1);
        if (i == 0x44) /* Return key is not a rectangle. Bodge it.*/
            clear_rect(x, y+1, 1, 14, 1<<1);
        /* Drawn the name in the centre of the outline rectangle. */
        for (l = 0; cap->name[l]; l++)
            continue;
        x += (cap->w+1) / 2;
        x -= l * 4;
        y += (cap->h+1) / 2;
        y -= 4;
        print_label(x, y, 1, cap->name);
    }

    /* Raw keycodes are displayed in a list at the bottom of the screen. */
    sprintf(s, "Raw Keycodes:");
    print_line(&r);
    r.y = 14;
    sprintf(s, "$C main menu$%21s[ATK %s]", "", version);
    print_line(&r);
    r.y = 11;

    i = 0;
    s[0] = '\0';
    keycode_cons = keycode_prod; /* clear the keycode ring */
    while (!do_exit) {
        /* Wait for a key in the keycode ring and then consume it. */
        uint8_t key, setclr, bpls;
        barrier(); /* see updates from keyboard irq */
        if (keycode_cons == keycode_prod)
            continue;
        key = keycode_ring[keycode_cons++ & (ARRAY_SIZE(keycode_ring)-1)];

        /* Out of list space. Clear the keycode-list area. */
        if (r.y == 14) {
            while (r.y > 11) {
                r.y--;
                sprintf(s, "");
                print_line(&r);
            }
        }

        /* Append the new keycode to the current line. */
        sprintf(num, "%02x ", key);
        strcat(s, num);
        print_line(&r);
        if (i++ == 8) {
            i = 0;
            s[0] = '\0';
            r.y++;
        }

        /* Find the keycap (if any) and highlight as necessary. */
        cap = &keymap[key & 0x7f];
        if (!cap->h)
            continue;
        x = 30 + cap->x;
        y = 13 + cap->y;
        setclr = !(key & 0x80);
        bpls = setclr ? (1<<2)|(1<<0) : (1<<0); /* sticky highlight bpl[2] */
        draw_rect(x+1, y+1, cap->w-1, cap->h-1, bpls, setclr);
        if ((key & 0x7f) == 0x44) /* Return needs a bodge.*/
            draw_rect(x-7, y+1, 8, 14, bpls, setclr);
    }
}

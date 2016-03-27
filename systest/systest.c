/*
 * systest.c
 * 
 * System Tests:
 *  - Slow RAM Detection and Check
 *  - Keyboard
 *  - Floppy Drive
 *  - Joystick / Mouse
 *  - Audio
 *  - Video
 * 
 * Written & released by Keir Fraser <keir.xen@gmail.com>
 * 
 * This is free and unencumbered software released into the public domain.
 * See the file COPYING for more details, or visit <http://unlicense.org>.
 */

static volatile struct m68k_vector_table * m68k_vec =
    (struct m68k_vector_table *)0x0;
static volatile struct amiga_custom * const cust =
    (struct amiga_custom *)0xdff000;
static volatile struct amiga_cia * const ciaa =
    (struct amiga_cia *)0x0bfe001;
static volatile struct amiga_cia * const ciab =
    (struct amiga_cia *)0x0bfd000;

int read_tracks(void *mfmbuf, void *dest, unsigned int start_track,
                unsigned int nr_tracks);
void write_track(void *mfmbuf, unsigned int track);
void mfm_encode_track(void *mfmbuf, unsigned int track);

/* Space for bitplanes and unpacked font. */
extern char GRAPHICS[];

#define IRQ(name)                               \
static void c_##name(void) attribute_used;      \
void name(void);                                \
asm (                                           \
#name":                             \n"         \
"    movem.l %d0-%d1/%a0-%a1,-(%sp) \n"         \
"    bsr c_"#name"                  \n"         \
"    movem.l (%sp)+,%d0-%d1/%a0-%a1 \n"         \
"    rte                            \n"         \
)

#define xres    640
#define yres    169
#define bplsz   (yres*xres/8)
#define planes  2

#define xstart  110
#define ystart  20
#define yperline 10

/* PAL/NTSC */
static int is_pal;
static unsigned int cpu_hz;
#define PAL_HZ 7093790
#define NTSC_HZ 7159090

/* VBL IRQ: 16- and 32-bit timestamps, and VBL counter. */
static volatile uint32_t stamp32;
static volatile uint16_t stamp16;
static volatile unsigned int vblank_count;

/* CIAA IRQ: Keyboard variables. */
static volatile uint8_t keycode_buffer, exit;

/* CIAB IRQ: FLAG (disk index) pulse counter. */
static volatile unsigned int disk_index_count;
static volatile uint32_t disk_index_time, disk_index_period;

static uint8_t *bpl[2];
static uint16_t *font;
static void *alloc_start, *alloc_p;

static uint16_t copper[] = {
    0x008e, 0x4681, /* diwstrt.v = 0x46 */
    0x0090, 0xefc1, /* diwstop.v = 0xef (169 lines) */
    0x0092, 0x003c, /* ddfstrt */
    0x0094, 0x00d4, /* ddfstop */
    0x0100, 0xa200, /* bplcon0 */
    0x0102, 0x0000, /* bplcon1 */
    0x0104, 0x0000, /* bplcon2 */
    0x0108, 0x0000, /* bpl1mod */
    0x010a, 0x0000, /* bpl2mod */
    0x00e0, 0x0000, /* bpl1pth */
    0x00e2, 0x0000, /* bpl1ptl */
    0x00e4, 0x0000, /* bpl2pth */
    0x00e6, 0x0000, /* bpl2ptl */
    0x0182, 0x0222, /* col01 */
    0x0184, 0x0ddd, /* col02 */
    0x0186, 0x0ddd, /* col03 */
    0x0180, 0x0103, /* col00 */
    0x008a, 0x0000, /* copjmp2 */
};

static uint16_t copper_2[] = {
    0x4401, 0xfffe,
    0x0180, 0x0ddd,
    0x4501, 0xfffe,
    0x0180, 0x0402,
    0xf001, 0xfffe,
    0x0180, 0x0ddd,
    0xf101, 0xfffe,
    0x0180, 0x0103,
    0xffff, 0xfffe,
};

struct char_row {
    uint8_t x, y;
    const char *s;
};

/* Test suite. */
static void memcheck(void);
static void kbdcheck(void);
static void floppycheck(void);
static void joymousecheck(void);
static void audiocheck(void);
static void videocheck(void);

const static struct menu_option {
    void (*fn)(void);
    const char *name;
} menu_option[] = {
    { memcheck,      "Slow RAM (512kB Trapdoor)" },
    { kbdcheck,      "Keyboard" },
    { floppycheck,   "Floppy Drive" },
    { joymousecheck, "Mouse / Joystick Ports" },
    { audiocheck,    "Audio" },
    { videocheck,    "Video" }
};

/* Allocate chip memory. Automatically freed when sub-test exits. */
static void *allocmem(unsigned int sz)
{
    void *p = alloc_p;
    alloc_p = (uint8_t *)alloc_p + sz;
    return p;
}

static uint32_t div32(uint32_t dividend, uint16_t divisor)
{
    do_div(dividend, divisor);
    return dividend;
}

static uint16_t get_ciaatb(void)
{
    uint8_t hi, lo;

    /* Loop to get consistent current CIA timer value. */
    do {
        hi = ciaa->tbhi;
        lo = ciaa->tblo;
    } while (hi != ciaa->tbhi);

    return ((uint16_t)hi << 8) | lo;
}

static uint32_t get_time(void)
{
    uint32_t _stamp32;
    uint16_t _stamp16;

    /* Loop to get consistent timestamps from the VBL IRQ handler. */
    do {
        _stamp32 = stamp32;
        _stamp16 = stamp16;
    } while (_stamp32 != stamp32);

    return -(_stamp32 - (uint16_t)(_stamp16 - get_ciaatb()));
}

static void delay_ms(unsigned int ms)
{
    uint16_t ticks_per_ms = div32(cpu_hz+9999, 10000); /* round up */
    uint32_t s, t;

    s = get_time();
    do {
        t = div32(get_time() - s, ticks_per_ms);
    } while (t < ms);
}

/* Convert CIA ticks (tick/10cy) into a printable string. */
static void ticktostr(uint32_t ticks, char *s)
{
    uint16_t ticks_per_ms = div32(cpu_hz+5000, 10000); /* round nearest */
    uint32_t sec, ms, us;

    if (ticks == (uint16_t)ticks) {
        us = div32(ticks<<16, ticks_per_ms);
        if (us != (uint16_t)us) {
            ms = us >> 16;
            us = ((uint16_t)us * 1000u) >> 16;
            sprintf(s, "%u.%03ums", ms, us);
        } else {
            us = (us * 1000u) >> 16;
            sprintf(s, "%uus", us);
        }
    } else {
        ms = ticks;
        us = do_div(ms, ticks_per_ms);
        if (ms > 1000) {
            sec = ms;
            ms = do_div(sec, 1000);
            sprintf(s, "%u.%us", sec, (uint16_t)ms/100);
        } else {
            us = ((uint16_t)div32(us<<16, ticks_per_ms) * 1000u) >> 16;
            sprintf(s, "%u.%ums", ms, (uint16_t)us/100);
        }
    }
}

static int pal_detect(void)
{
    uint32_t ticks;

    /* Synchronise to Vblank. */
    vblank_count = 0;
    while (!vblank_count)
        continue;
    ticks = get_time();

    /* Wait 10 blanks. */
    while (vblank_count < 11)
        continue;
    ticks = get_time() - ticks;

    /* Expected tick values: 
     *  NTSC: 10 * (715909 / 60) = 119318
     *  PAL:  10 * (709379 / 50) = 141875 
     * Use 130,000 as mid-point to discriminate.. */
    return ticks > 130000;
}

/* Wait for blitter idle. */
static void waitblit(void)
{
    while (*(volatile uint8_t *)&cust->dmaconr & (1u << 6))
        continue;
}

/* Wait for end of bitplane DMA. */
static void wait_bos(void)
{
    while (*(volatile uint8_t *)&cust->vhposr != 0xf0)
        continue;
}

/* Wait for beam to move to next scanline. */
static void wait_line(void)
{
    uint8_t v = *(volatile uint8_t *)&cust->vhposr;
    while (*(volatile uint8_t *)&cust->vhposr == v)
        continue;
}

static void drawpixel(uint8_t *plane, unsigned int x, unsigned int y, int set)
{
    uint16_t bpl_off = y * (xres/8) + (x/8);
    if (!set)
        plane[bpl_off] &= ~(0x80 >> (x & 7));
    else
        plane[bpl_off] |= 0x80 >> (x & 7);
}

static void draw_hollow_rect(
    uint8_t *plane,
    unsigned int x, unsigned int y,
    unsigned int w, unsigned int h,
    int set)
{
    unsigned int i, j;

    for (i = 0; i < w; i++) {
        drawpixel(plane, x+i, y, set);
        drawpixel(plane, x+i, y+h-1, set);
    }

    for (j = 0; j < h; j++) {
        drawpixel(plane, x, y+j, set);
        drawpixel(plane, x+w-1, y+j, set);
    }
}

static void draw_filled_rect(
    uint8_t *plane,
    unsigned int x, unsigned int y,
    unsigned int w, unsigned int h,
    int set)
{
    unsigned int i, j;
    uint8_t b, *p;
    plane += y * (xres/8) + (x/8);
    for (j = 0; j < h; j++) {
        p = plane;
        plane += xres/8;
        for (i = x; i < x+w; i = (i+8)&~7) {
            b = 0xff;
            y = i&7;
            if (y) /* first byte in row is partial fill? */
                b >>= y;
            y += x+w-i-1;
            if (y < 7) /* last byte in row is partial fill? */
                b &= 0xff << (7-y);
            if (!set)
                *p++ &= ~b;
            else
                *p++ |= b;
        }
    }
}

static void clear_screen_rows(uint16_t y_start, uint16_t y_nr)
{
    waitblit();
    cust->bltcon0 = 0x0100;
    cust->bltcon1 = 0x0000;
    cust->bltdpt.p = bpl[0] + y_start * (xres/8);
    cust->bltdmod = 0;
    cust->bltsize = (xres/16)|(y_nr<<6);
    waitblit();
    cust->bltdpt.p = bpl[1] + y_start * (xres/8);
    cust->bltsize = (xres/16)|(y_nr<<6);
    waitblit();
}

static void clear_colors(void)
{
    uint16_t i;
    for (i = 0; i < 16; i++)
        cust->color[i] = 0;
}

/* Unpack 8*8 font into destination.
 * Each char 00..7f is copied in sequence.
 * Destination is 10 longwords (= 10 rows) per character.
 * First word of each long is foreground, second word is background.
 * Background is computed from foreground. */
extern uint8_t packfont[];
static void *unpack_font(void *start)
{
    uint8_t *p = packfont;
    uint16_t i, j, x, *q;

    font = q = start;

    /* First 0x20 chars are blank. */
    memset(q, 0, 0x20 * yperline * 4);
    q += 0x20 * yperline * 2;

    for (i = 0x20; i < 0x80; i++) {
        /* Foreground char is shifted right one pixel. */
        q[0] = 0;
        for (j = 1; j < yperline-1; j++)
            q[2*j] = (uint16_t)(*p++) << 7; 
        q[2*j] = 0;

        /* OR the neighbouring rows into each background pixel. */
        q[1] = q[0] | q[2];
        for (j = 1; j < yperline-1; j++)
            q[2*j+1] = q[2*(j-1)] | q[2*j] | q[2*(j+1)];
        q[2*j+1] = q[2*(j-1)] | q[2*j];

        /* OR the neighbouring columns into each background pixel. */
        for (j = 0; j < yperline; j++) {
            x = q[2*j+1];
            q[2*j+1] = (x << 1) | x | (x >> 1);
        }

        q += yperline * 2;
    }

    return q;
}

static void print_char(uint16_t x, uint16_t y, char c)
{
    uint16_t *font_char = font + c * yperline * 2;
    uint16_t i, bpl_off = y * (xres/8) + (x/16) * 2;

    waitblit();

    cust->bltcon0 = 0x0dfc | ((x&15)<<12); /* ABD DMA, D = A|B, pre-shift A */
    cust->bltcon1 = 0;
    cust->bltamod = 0;
    cust->bltbmod = xres/8 - 4;
    cust->bltdmod = xres/8 - 4;
    cust->bltafwm = 0xffff;
    cust->bltalwm = 0x0000;
    
    for (i = 0; i < 2; i++) {
        waitblit();
        cust->bltapt.p = font_char + (i==0);
        cust->bltbpt.p = bpl[i] + bpl_off;
        cust->bltdpt.p = bpl[i] + bpl_off;
        cust->bltsize = 2 | (yperline<<6); /* 2 words * yperline rows */
    }
}

static void print_line(const struct char_row *r)
{
    uint16_t x = xstart + r->x * 8;
    uint16_t y = ystart + r->y * yperline;
    const char *p = r->s;
    char c;
    clear_screen_rows(y, yperline);
    while ((c = *p++) != '\0') {
        print_char(x, y, c);
        x += 8;
    }
}

static void menu(void)
{
    uint8_t i;
    char s[80];
    struct char_row r = { .x = 4, .y = 0, .s = s };

    clear_screen_rows(0, yres);
    keycode_buffer = 0;

    sprintf(s, "SysTest - by KAF <keir.xen@gmail.com>");
    print_line(&r);
    r.y++;
    sprintf(s, "------------------------------------");
    print_line(&r);
    r.y++;
    for (i = 0; i < ARRAY_SIZE(menu_option); i++) {
        sprintf(s, "F%u - %s", i+1, menu_option[i].name);
        print_line(&r);
        r.y++;
    }
    sprintf(s, "(Ctrl + L.Alt to quit back to menu)");
    print_line(&r);
    r.y++;
    sprintf(s, "----------- System: %s-----------",
            is_pal ? "PAL -" : "NTSC ");
    print_line(&r);
    r.y++;
    sprintf(s, "https://github.com/keirf/Amiga-Stuff");
    print_line(&r);
    r.y++;
    sprintf(s, "build: %s %s", __DATE__, __TIME__);
    print_line(&r);
    r.y++;

    while ((i = keycode_buffer - 0x50) >= ARRAY_SIZE(menu_option))
        continue;
    clear_screen_rows(0, yres);
    keycode_buffer = 0;
    exit = 0;
    alloc_p = alloc_start;
    (*menu_option[i].fn)();
}

static void memcheck(void)
{
    volatile uint16_t *p;
    volatile uint16_t *start = (volatile uint16_t *)0xc00000;
    volatile uint16_t *end = (volatile uint16_t *)0xc80000;
    char s[80];
    struct char_row r = { .x = 8, .y = 5, .s = s };
    uint16_t a, b, i, j;

    i = cust->intenar;

    /* If slow memory is absent then custom registers alias at C00000. We 
     * detect this by writing to what would be INTENA and checking for changes 
     * to what would be INTENAR. If we see no change then we are not writing 
     * to the custom registers and _EXRAM must be asserted at Gary. */
    p = start;
    p[0x9a/2] = 0x7fff; /* clear all bits in INTENA */
    a = p[0x1c/2];
    p[0x9a/2] = 0xbfff; /* set all bits in INTENA except master enable */
    b = p[0x1c/2];

    sprintf(s, "%04x / %04x", a, b);
    print_line(&r);
    r.y++;

    sprintf(s, "Slow RAM%s detected", (a != b) ? " *NOT*" : "");
    print_line(&r);
    r.y++;

    if (a != b) {
        cust->intena = 0x7fff;
        cust->intena = 0x8000 | i;
        while (!exit)
            continue;
        return;
    }

    /* We believe we have slow memory present. Now check the RAM for errors. 
     * This uses an inversions algorithm where we try to set an alternating 
     * 0/1 pattern in each memory cell (assuming 1-bit DRAM chips). */
    a = b = 0;
    while (!exit) {
        /* Start with all 0s. Write 1s to even words. */
        for (p = start; p != end;) {
            *p++ = 0x0000;
        }
        if (exit) break;
        for (p = start; p != end;) {
            *p++ = 0xffff;
            p++;
        }
        if (exit) break;
        for (p = start; p != end;) {
            a |= ~*p++;
            a |= *p++;
        }
        if (exit) break;

        /* Start with all 0s. Write 1s to odd words. */
        for (p = start; p != end;) {
            *p++ = 0x0000;
        }
        if (exit) break;
        for (p = start; p != end;) {
            p++;
            *p++ = 0xffff;
        }
        if (exit) break;
        for (p = start; p != end;) {
            a |= *p++;
            a |= ~*p++;
        }
        if (exit) break;

        /* Start with all 1s. Write 0s to even words. */
        for (p = start; p != end;) {
            *p++ = 0xffff;
        }
        if (exit) break;
        for (p = start; p != end;) {
            *p++ = 0x0000;
            p++;
        }
        if (exit) break;
        for (p = start; p != end;) {
            a |= *p++;
            a |= ~*p++;
        }
        if (exit) break;

        /* Start with all 1s. Write 0s to odd words. */
        for (p = start; p != end;) {
            *p++ = 0xffff;
        }
        if (exit) break;
        for (p = start; p != end;) {
            p++;
            *p++ = 0x0000;
        }
        if (exit) break;
        for (p = start; p != end;) {
            a |= ~*p++;
            a |= *p++;
        }
        if (exit) break;

        b++;
        if ((ciaa->pra & 0xc0) != 0xc0) {
            while (r.y >= 5) {
                sprintf(s, "");
                print_line(&r);
                r.y--;
            }
            r.y = 5;
            for (i = j = 0; i < 16; i++)
                if ((a >> i) & 1)
                    j++;
            sprintf(s, "After %u rounds: errors in %u bit positions", b, j);
            print_line(&r);
            r.y++;
            if (j != 0) {
                char num[8];
                sprintf(s, " -> Bits ");
                for (i = 0; i < 16; i++) {
                    if (!((a >> i) & 1))
                        continue;
                    sprintf(num, "%u", i);
                    if (--j)
                        strcat(num, ",");
                    strcat(s, num);
                }
                print_line(&r);
                r.y++;
            }
            a = b = 0;
            continue;
        }

        sprintf(s, "After %u rounds: errors %04x", b, a);
        print_line(&r);
    }
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

/* Draws a plain 8x8 character straight into bitplane 1. */
static void drawkbch(unsigned int x, unsigned int y, uint8_t c)
{
    uint16_t bpl_off = y * (xres/8) + (x/8);
    uint8_t d, *p = &packfont[8*(c-0x20)];
    unsigned int i;
    for (i = 0; i < 8; i++) {
        d = *p++;
        bpl[1][bpl_off] |= d >> (x&7);
        if (x & 7)
            bpl[1][bpl_off+1] |= d << (8-(x&7));
        bpl_off += xres/8;
    }
}

/* Reverse-video effect for highlighting keypresses in the keymap. */
static uint16_t copper_kbd[] = {
    /* reverse video */
    0x0182, 0x0ddd, /* col01 = foreground */
    0x0186, 0x0222, /* col03 = shadow */
    0x4401, 0xfffe,
    0x0180, 0x0ddd,
    0x4501, 0xfffe,
    0x0180, 0x0402,
    0xbd01, 0xfffe,
    /* normal video */
    0x0182, 0x0222, /* col01 = shadow */
    0x0186, 0x0ddd, /* col03 = foreground */
    0xf001, 0xfffe,
    0x0180, 0x0ddd,
    0xf101, 0xfffe,
    0x0180, 0x0103,
    0xffff, 0xfffe,
};

static void kbdcheck(void)
{
    char s[80], num[5];
    struct char_row r = { .x = 8, .y = 10, .s = s };
    const struct keycap *cap;
    unsigned int i, l, x, y;

    /* Poke the new copper list at a safe point. */
    wait_bos();
    cust->cop2lc.p = copper_kbd;

    /* Draw the keymap. This resides in bitplane 1. We then draw filled boxes
     * in bitplane 0 to indicate keys which are currently pressed. The reverse
     * video effect causes the key to be highlighted with the key name still
     * visible. */
    draw_hollow_rect(bpl[1], 20, 10, 601, 106, 1);
    for (i = 0; i < ARRAY_SIZE(keymap); i++) {
        cap = &keymap[i];
        if (!cap->h)
            continue;
        /* Draw the outline rectangle. */
        x = 30 + cap->x;
        y = 15 + cap->y;
        draw_hollow_rect(bpl[1], x, y, cap->w+1, cap->h+1, 1);
        if (i == 0x44) /* Return key is not a rectangle. Bodge it.*/
            draw_hollow_rect(bpl[1], x, y+1, 1, 14, 0);
        /* Drawn the name in the centre of the outline rectangle. */
        for (l = 0; cap->name[l]; l++)
            continue;
        x += (cap->w+1) / 2;
        x -= l * 4;
        y += (cap->h+1) / 2;
        y -= 4;
        for (l = 0; cap->name[l]; l++) {
            drawkbch(x, y, cap->name[l]);
            x += 8;
        }
    }

    /* Raw keycodes are displayed in a list at the bottom of the screen. */
    sprintf(s, "Raw Keycodes:");
    print_line(&r);
    r.y++;

    i = 0;
    s[0] = '\0';
    keycode_buffer = 0x7f;
    while (!exit) {
        /* Wait for a key. Latch it. */
        uint8_t key = keycode_buffer;
        if (key == 0x7f)
            continue;
        keycode_buffer = 0x7f;

        /* Out of list space. Clear the keycode-list area. */
        if (r.y == 14) {
            while (r.y >= 11) {
                sprintf(s, "");
                print_line(&r);
                r.y--;
            }
            r.y = 11;
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
        y = 15 + cap->y;
        draw_filled_rect(bpl[0], x+1, y+1, cap->w-1, cap->h-1, !(key & 0x80));
        if ((key & 0x7f) == 0x44) /* Return needs a bodge.*/
            draw_filled_rect(bpl[0], x-7, y+1, 8, 14, !(key & 0x80));
    }

    /* Clean up. */
    wait_bos();
    cust->cop2lc.p = copper_2;
}

/* Select @drv and set motor on or off. */
static void drive_select_motor(unsigned int drv, int on)
{
    ciab->prb |= 0xf9; /* motor-off, deselect all */
    if (on)
        ciab->prb &= 0x7f; /* motor-on */
    ciab->prb &= ~(0x08 << drv); /* select drv */
}

/* Wait for DSKRDY, or 500ms to pass, whichever is sooner. */
static void drive_wait_ready(void)
{
    unsigned int i;
    /* 8000 * 63us ~= 500ms */
    for (i = 0; i < 8000; i++) {
        if (!(ciaa->pra & (1u<<5)))
            break; /* READY */
        wait_line(); /* 63us */
    }
}

/* Returns number of head steps to find cylinder 0. */
static int cur_cyl;
static unsigned int seek_cyl0(void)
{
    unsigned int steps = 0;

    ciab->prb |= 6; /* outward, side 0 */
    delay_ms(18);

    cur_cyl = 0;

    while (ciaa->pra & (1u<<4)) {
        ciab->prb &= ~1; /* step */
        ciab->prb |= 1;
        delay_ms(3);
        if (steps++ == 100) {
            cur_cyl = -1;
            break;
        }
    }

    delay_ms(15);

    return steps;
}

static void seek_track(unsigned int track)
{
    unsigned int cyl = track >> 1;
    int diff;

    if (cur_cyl == -1)
        return;

    if (cyl == 0)
        seek_cyl0();

    diff = cyl - cur_cyl;
    if (diff < 0) {
        diff = -diff;
        ciab->prb |= 2; /* outward */
    } else {
        ciab->prb &= ~2; /* inward */
    }
    delay_ms(18);

    while (diff--) {
        ciab->prb &= ~1; /* step */
        ciab->prb |= 1;
        delay_ms(3);
    }

    delay_ms(15);

    cur_cyl = cyl;

    if (!(track & 1)) {
        ciab->prb |= 4; /* side 0 */
    } else {
        ciab->prb &= ~4; /* side 1 */
    }
}

static uint32_t drive_id(unsigned int drv)
{
    uint32_t id = 0;
    uint8_t mask = 0x08 << drv;
    unsigned int i;

    ciab->prb |= 0xf8;  /* motor-off, deselect all */
    ciab->prb &= 0x7f;  /* 1. MTRXD low */
    ciab->prb &= ~mask; /* 2. SELxB low */
    ciab->prb |= mask;  /* 3. SELxB high */
    ciab->prb |= 0x80;  /* 4. MTRXD high */
    ciab->prb &= ~mask; /* 5. SELxB low */
    ciab->prb |= mask;  /* 6. SELxB high */
    for (i = 0; i < 32; i++) {
        ciab->prb &= ~mask; /* 7. SELxB low */
        id = (id<<1) | ((ciaa->pra>>5)&1); /* 8. Read and save state of RDY */
        ciab->prb |= mask;  /* 9. SELxB high */
    }
    return ~id;
}

static void floppycheck(void)
{
    char s[80];
    struct char_row r = { .x = 8, .y = 1, .s = s };
    uint8_t pra, old_pra, key;
    unsigned int i, on = 0, drv = 0, old_disk_index_count;
    uint32_t rdy_delay, mtr_time;
    int rdy_changed;

    sprintf(s, "-- Floppy IDs --");
    print_line(&r);
    r.y++;

    for (i = 0; i < 4; i++) {
        uint32_t id = drive_id(i);
        sprintf(s, "DF%u: %08x (%s)", i, id,
                (id == -!!i) ? "Present" :
                (id ==  -!i) ? "Not Present" :
                "???");
        print_line(&r);
        r.y++;
    }
    r.y++;

    while (!exit) {

        drive_select_motor(drv, on);
        seek_cyl0();
        if (cur_cyl == 0) {
            unsigned int nr_cyls;
            seek_track(159);
            nr_cyls = seek_cyl0() + 1;
            if (cur_cyl == 0)
                sprintf(s, "-- DF%u: %u cylinders --", drv, nr_cyls);
        }
        if (cur_cyl < 0)
            sprintf(s, "-- DF%u: No Track 0 (Drive not present?) --", drv);
        print_line(&r);
        r.y += 3;

        sprintf(s, "F1-F4: DF0-DF3; F5: Motor On/Off; F6: Step");
        print_line(&r);
        r.y -= 2;

        old_pra = ciaa->pra;
        mtr_time = get_time();
        rdy_delay = rdy_changed = 0;
        old_disk_index_count = disk_index_count = 0;
        key = 1; /* force print */

        while (!exit) {
            if (((pra = ciaa->pra) != old_pra) || key) {
                rdy_changed = !!((old_pra ^ pra) & 32);
                if (rdy_changed)
                    rdy_delay = get_time() - mtr_time;
                sprintf(s, "MTR=%s CIAAPRA=0x%02x (%s %s %s %s)",
                        on ? "On" : "Off",
                        pra,
                        !(pra&4)?"CHG":"",
                        !(pra&8)?"WPR":"",
                        !(pra&16)?"TK0":"",
                        !(pra&32)?"RDY":"");
                print_line(&r);
                old_pra = pra;
            }
            if ((old_disk_index_count != disk_index_count)
                || rdy_changed || key) {
                char rdystr[10], idxstr[10];
                ticktostr(disk_index_period, idxstr);
                ticktostr(rdy_delay, rdystr);
                sprintf(s, "%u Index Pulses (period %s); MTR%s->RDY%u %s",
                        disk_index_count, idxstr, on?"On":"Off",
                        !!(old_pra & 32), rdystr);
                r.y++;
                print_line(&r);
                r.y--;
                old_disk_index_count = disk_index_count;
                rdy_changed = 0;
            }
            if ((key = keycode_buffer) == 0)
                continue;
            keycode_buffer = 0;
            if ((key >= 0x50) && (key <= 0x53)) { /* F1-F4 */
                drive_select_motor(drv, 0);
                drv = key - 0x50;
                r.y--;
                break;
            } else if (key == 0x54) { /* F5 */
                on = !on;
                drive_select_motor(drv, on);
                old_pra = ciaa->pra;
                mtr_time = get_time();
                rdy_delay = 0;
            } else if (key == 0x55) { /* F6 */
                seek_track((cur_cyl == 0) ? 2 : 0);
                key = 0; /* don't force print */
            } else {
                key = 0;
            }
        }
    }

    /* Clean up. */
    drive_select_motor(drv, 0);

    if (0) { /* XXX read/write test stuff*/
        while (read_tracks((void *)(unsigned long)0x12345678,
                           (void *)(unsigned long)0x87654321,
                           0xabcd, 0xdcba))
            continue;
        drive_wait_ready();
    }

}

static void printport(char *s, unsigned int port)
{
    uint16_t joy = port ? cust->joy1dat : cust->joy0dat;
    uint8_t buttons = cust->potinp >> (port ? 12 : 8);
    sprintf(s, "Button=(%c%c%c) Dir=(%c%c%c%c)",
            !(ciaa->pra & (1u << (port ? 7 : 6))) ? '1' : ' ',
            !(buttons & 1) ? '2' : ' ',
            !(buttons & 4) ? '3' : ' ',
            (joy & 0x200) ? 'L' : ' ',
            (joy & 2) ? 'R' : ' ',
            ((joy & 0x100) ^ ((joy & 0x200) >> 1)) ? 'U' : ' ',
            ((joy & 1) ^ ((joy & 2) >> 1)) ? 'D' : ' ');
}

static void updatecoords(uint16_t oldjoydat, uint16_t newjoydat, int *coords)
{
    coords[0] += (int8_t)(newjoydat - oldjoydat);
    coords[1] += (int8_t)((newjoydat >> 8) - (oldjoydat >> 8));
    coords[0] = min_t(int, max_t(int, coords[0], 0), 139);
    coords[1] = min_t(int, max_t(int, coords[1], 0), 139);
}

static void joymousecheck(void)
{
    char sub[2][30], s[80];
    struct char_row r = { .x = 8, .y = 1, .s = s };
    int i, coords[2][2] = { { 0 } };
    uint16_t joydat[2], newjoydat[2];

    joydat[0] = cust->joy0dat;
    joydat[1] = cust->joy1dat;

    /* Pull-ups on button 2 & 3 inputs. */
    cust->potgo = 0xff00;

    sprintf(s, "-- Joy / Mouse Test --");
    print_line(&r);
    r.x = 0;
    r.y += 3;

    for (i = 0; !exit; i++) {

        if (i & 1) {
            /* Odd frames: print button/direction info */
            printport(sub[0], 0);
            printport(sub[1], 1);
            r.y++;
        } else {
            /* Even frames: print coordinate info */
            sprintf(sub[0], "Port 1: (%3u,%3u)", coords[0][0], coords[0][1]);
            sprintf(sub[1], "Port 2: (%3u,%3u)", coords[1][0], coords[1][1]);
            r.y--;
        }
        sprintf(s, "%37s%s", sub[0], sub[1]);

        newjoydat[0] = cust->joy0dat;
        newjoydat[1] = cust->joy1dat;

        /* Wait for end of display, then start screen updates. */
        wait_bos();
        print_line(&r); /* sloooow, allows only one per vbl */
        draw_filled_rect(bpl[1], coords[0][0] + 100, coords[0][1]/2 + 80,
                         4, 2, 0);
        draw_filled_rect(bpl[1], coords[1][0] + 400, coords[1][1]/2 + 80,
                         4, 2, 0);
        updatecoords(joydat[0], newjoydat[0], coords[0]);
        updatecoords(joydat[1], newjoydat[1], coords[1]);
        draw_filled_rect(bpl[1], coords[0][0] + 100, coords[0][1]/2 + 80,
                         4, 2, 1);
        draw_filled_rect(bpl[1], coords[1][0] + 400, coords[1][1]/2 + 80,
                         4, 2, 1);

        joydat[0] = newjoydat[0];
        joydat[1] = newjoydat[1];
    }

    /* Clean up. */
    cust->potgo = 0x0000;
}

static void audiocheck(void)
{
    char s[80];
    struct char_row r = { .x = 8, .y = 3, .s = s };
    static const uint8_t sine[] = { 0,19,39,57,74,89,102,113,120,125,127 };
    const unsigned int nr_samples = 40;
    int8_t *aud = allocmem(nr_samples);
    uint8_t key, channels = 0;
    uint32_t period;
    unsigned int i;

    for (i = 0; i < 10; i++) {
        aud[i] = sine[i];
        aud[10+i] = sine[10-i];
        aud[20+i] = -sine[i];
        aud[30+i] = -sine[10-i];
    }

    sprintf(s, "-- 500Hz Sine Wave Audio Test --");
    print_line(&r);
    r.x += 7;
    r.y++;
    sprintf(s, "F1 - Channel 0 (L)");
    print_line(&r);
    r.y++;
    sprintf(s, "F2 - Channel 1 (R)");
    print_line(&r);
    r.y++;
    sprintf(s, "F3 - Channel 2 (R)");
    print_line(&r);
    r.y++;
    sprintf(s, "F4 - Channel 3 (L)");
    print_line(&r);

    /* period = cpu_hz / (2 * nr_samples * frequency) */
    period = div32(div32(div32(cpu_hz, 2), nr_samples), 500/*Hz*/);

    for (i = 0; i < 4; i++) {
        cust->aud[i].lc.p = aud;
        cust->aud[i].len = nr_samples / 2;
        cust->aud[i].per = (uint16_t)period;
        cust->aud[i].vol = 0;
    }
    cust->dmacon = 0x800f; /* all audio channels */

    r.x -= 2;
    r.y += 2;

    while (!exit) {
        sprintf(s, "0=%s 1=%s 2=%s 3=%s",
                (channels & 1) ? "ON " : "OFF",
                (channels & 2) ? "ON " : "OFF",
                (channels & 4) ? "ON " : "OFF",
                (channels & 8) ? "ON " : "OFF");
        wait_bos();
        print_line(&r);

        if ((key = keycode_buffer - 0x50) >= 4)
            continue;
        keycode_buffer = 0;
        channels ^= 1u << key;
        cust->aud[key].vol = (channels & (1u << key)) ? 64 : 0;
    }

    /* Clean up. */
    for (i = 0; i < 4; i++)
        cust->aud[i].vol = 0;
    cust->dmacon = 0x000f;
}

static void videocheck(void)
{
    uint16_t *p, *cop, wait;
    unsigned int i, j, k;

    cop = p = allocmem(16384 /* plenty */);

    /* First line of gradient. */
    wait = 0x4401;

    /* Top border black. */
    *p++ = 0x0180;
    *p++ = 0x0000;

    /* Create a vertical gradient of red, green, blue (across screen). 
     * Alternate white/black markers to indicate gradient changes. */
    for (i = 0; i < 16; i++) {
        for (j = 0; j < 10; j++) {
            /* WAIT line */
            *p++ = wait;
            *p++ = 0xfffe;
            /* color = black or white */
            *p++ = 0x0180;
            *p++ = (i & 1) ? 0x0000 : 0x0fff;

            for (k = 0; k < 4; k++) {
                /* WAIT horizontal */
                *p++ = wait | (0x50 + k*0x28);
                *p++ = 0xfffe;
                /* color = black or white */
                *p++ = 0x0180;
                *p++ = (i & 1) ? 0x0000 : 0x0fff;
                if (k == 3) break;
                /* color = red, green or blue gradients */
                *p++ = 0x0180;
                *p++ = i << (8 - k*4);
            }

            wait += 1<<8;
        }
    }

    /* End of copper list. */
    *p++ = 0xffff;
    *p++ = 0xfffe;

    /* Poke the new copper list at a safe point. */
    wait_bos();
    cust->cop2lc.p = cop;

    /* All work is done by the copper. Just wait for exit. */
    while (!exit)
        continue;

    /* Clean up. */
    wait_bos();
    cust->cop2lc.p = copper_2;
}

IRQ(CIAA_IRQ);
static void c_CIAA_IRQ(void)
{
    uint16_t i;
    uint8_t icr = ciaa->icr;
    static uint8_t prev_key;

    if (icr & (1u<<3)) { /* SDR finished a byte? */
        /* Grab and decode the keycode. */
        uint8_t keycode = ~ciaa->sdr;
        keycode_buffer = (keycode >> 1) | (keycode << 7); /* ROR 1 */
        if ((prev_key == 0x63) && (keycode_buffer == 0x64))
            exit = 1; /* Ctrl + L.Alt */
        prev_key = keycode_buffer;
        /* Handshake over the serial line. */
        ciaa->cra |= 1u<<6; /* start the handshake */
        for (i = 0; i < 3; i++) /* wait ~100us */
            wait_line();
        ciaa->cra &= ~(1u<<6); /* finish the handshake */
    }

    /* NB. Clear intreq.ciaa *after* reading/clearing ciaa.icr else we get a 
     * spurious extra interrupt, since intreq.ciaa latches the level of CIAA 
     * INT and hence would simply become set again immediately after we clear 
     * it. For this same reason (latches level not edge) it is *not* racey to 
     * clear intreq.ciaa second. Indeed AmigaOS does the same (checked 
     * Kickstart 3.1). */
    cust->intreq = 1u<<3;
}

IRQ(CIAB_IRQ);
static void c_CIAB_IRQ(void)
{
    uint8_t icr = ciab->icr;

    if (icr & (1u<<4)) { /* FLAG (disk index)? */
        uint32_t time = get_time();
        disk_index_count++;
        disk_index_period = time - disk_index_time;
        disk_index_time = time;
    }

    /* NB. Clear intreq.ciab *after* reading/clearing ciab.icr else we get a 
     * spurious extra interrupt, since intreq.ciab latches the level of CIAB 
     * INT and hence would simply become set again immediately after we clear 
     * it. For this same reason (latches level not edge) it is *not* racey to 
     * clear intreq.ciab second. Indeed AmigaOS does the same (checked 
     * Kickstart 3.1). */
    cust->intreq = 1u<<13;
}

IRQ(VBLANK_IRQ);
static void c_VBLANK_IRQ(void)
{
    uint16_t cur16 = get_ciaatb();

    vblank_count++;

    stamp32 -= (uint16_t)(stamp16 - cur16);
    stamp16 = cur16;

    cust->intreq = 1u<<5;
}

void cstart(void)
{
    uint16_t i;
    char *p;

    /* Set up CIAA ICR. We only care about keyboard. */
    ciaa->icr = 0x7f;
    ciaa->icr = 0x88;

    /* Set up CIAB ICR. We only care about FLAG line (disk index). */
    ciab->icr = 0x7f;
    ciab->icr = 0x90;

    /* Enable blitter DMA. */
    cust->dmacon = 0x8040;

    /* Clear BSS. */
    memset(_sbss, 0, _ebss-_sbss);

    /* Bitplanes and unpacked font allocated as directed by linker. */
    p = GRAPHICS;
    bpl[0] = (uint8_t *)p; p += bplsz;
    bpl[1] = (uint8_t *)p; p += bplsz;
    p = unpack_font(p);
    alloc_start = p;

    /* Poke bitplane addresses into the copper. */
    for (i = 0; copper[i] != 0x00e0/*bpl1pth*/; i++)
        continue;
    copper[i+1] = (uint32_t)bpl[0] >> 16;
    copper[i+3] = (uint32_t)bpl[0];
    copper[i+5] = (uint32_t)bpl[1] >> 16;
    copper[i+7] = (uint32_t)bpl[1];
    
    /* Clear bitplanes. */
    clear_screen_rows(0, yres);

    clear_colors();

    m68k_vec->level2_autovector.p = CIAA_IRQ;
    m68k_vec->level6_autovector.p = CIAB_IRQ;
    m68k_vec->level3_autovector.p = VBLANK_IRQ;
    cust->cop1lc.p = copper;
    cust->cop2lc.p = copper_2;

    wait_bos();
    cust->dmacon = 0x81c0; /* enable copper/bitplane/blitter DMA */
    cust->intena = 0xa028; /* enable CIA-A/CIA-B/VBLANK interrupts */

    /* Start CIAA Timer B in continuous mode. */
    ciaa->tblo = 0xff;
    ciaa->tbhi = 0xff;
    ciaa->crb = 0x11;

    is_pal = pal_detect();
    cpu_hz = is_pal ? PAL_HZ : NTSC_HZ;

    for (;;)
        menu();
}

asm (
"    .data                          \n"
"packfont: .incbin \"../base/font.raw\"\n"
"    .text                          \n"
);

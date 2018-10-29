/*
 * trainer.c
 * 
 * Example simple trainer intro in C.
 * 
 * Written & released by Keir Fraser <keir.xen@gmail.com>
 * 
 * This is free and unencumbered software released into the public domain.
 * See the file COPYING for more details, or visit <http://unlicense.org>.
 */

static volatile struct amiga_custom * const cust =
    (struct amiga_custom *)0xdff000;
static volatile struct amiga_cia * const ciaa =
    (struct amiga_cia *)0x0bfe001;
/*static volatile struct amiga_cia * const ciab =
    (struct amiga_cia *)0x0bfdd00;*/

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

static volatile uint8_t keycode_buffer;
static uint8_t *bpl[2];
static uint16_t *font;

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
    0x4407, 0xfffe,
    0x0180, 0x0ddd,
    0x4507, 0xfffe,
    0x0180, 0x0402,
    0xf007, 0xfffe,
    0x0180, 0x0ddd,
    0xf107, 0xfffe,
    0x0180, 0x0103,
    0xffff, 0xfffe,
};

struct char_row {
    uint8_t x, y;
    const char *s;
};

static const struct char_row banner[] = {
    { 13, 0,            "=====================" },
    { 11, 1,          "+ THE NEW ZEALAND STORY +" },
    { 13, 2,            "=====================" },
    {  6, 3,     "Cracked & Trained by KAF in June '11" },
    {  0,10, "Space, Mouse button, or Joystick Fire to Continue!" },
    { 0xff, 0xff, NULL }
};

#define OPT_RANGE 1
#define OPT_BOOL  2
static struct option {
    uint8_t type:4;
    uint8_t dfl:4;
    uint8_t min:4;
    uint8_t max:4;
    const char *s;
} opts[] = {
    { OPT_BOOL, .dfl = 0, .s = "Infinite Lives" },
    { OPT_RANGE, .dfl = 3, .min = 1, .max = 10, .s = "Initial Lives" },
    { OPT_BOOL, .dfl = 0, .s = "Load/Save Highscores" },
    { OPT_BOOL, .dfl = 0, .s = "Reset Saved Highscores" },
    { 0 }
};

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
static void unpack_font(void)
{
    uint8_t *p = packfont;
    uint16_t i, j, x, *q = font;

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

static void print_screen(void)
{
    const struct char_row *r;
    for (r = banner; r->x != 0xff; r++)
        print_line(r);
}

static void print_option(const struct option *opt, uint16_t nr)
{
    char s[80], t[4] = "ON";
    struct char_row r = { .x = 8, .y = 5 + nr, .s = s };
    switch (opt->type) {
    case OPT_BOOL:
        sprintf(t, opt->dfl ? "ON" : "OFF");
        break;
    case OPT_RANGE:
        sprintf(t, "%2u", opt->dfl);
        break;
    }
    sprintf(s, "F%u - %24s%s", (uint32_t)(nr+1), opt->s, t);
    print_line(&r);
}

static void setup_options(void)
{
    const struct option *opt;
    uint16_t i;
    for (opt = opts, i = 0; opt->type; opt++, i++)
        print_option(opt, i);
}

static void update_option(uint8_t nr)
{
    struct option *opt;
    if (nr > 3)
        return;
    opt = &opts[nr];
    switch (opt->type) {
    case OPT_BOOL:
        opt->dfl = !opt->dfl;
        break;
    case OPT_RANGE:
        if (opt->dfl++ == opt->max)
            opt->dfl = opt->min;
        break;
    }
    print_option(opt, nr);
}

IRQ(CIA_IRQ);
static void c_CIA_IRQ(void)
{
    uint16_t i;
    uint8_t icr = ciaa->icr;

    if (icr & (1u<<3)) { /* SDR finished a byte? */
        /* Grab and decode the keycode. */
        uint8_t keycode = ~ciaa->sdr;
        keycode_buffer = (keycode >> 1) | (keycode << 7); /* ROR 1 */
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

uint32_t trainer(void)
{
    uint32_t optres = 0;
    uint16_t i;
    uint8_t keycode;
    char *p;

    /* Set up CIAA ICR. We only care about keyboard. */
    ciaa->icr = 0x7f;
    ciaa->icr = 0x88;

    /* Enable blitter DMA. */
    cust->dmacon = 0x8040;

    /* Clear BSS. */
    memset(_sbss, 0, _ebss-_sbss);

    /* Bitplanes and unpacked font allocated as directed by linker. */
    p = GRAPHICS;
    bpl[0] = (uint8_t *)p; p += bplsz;
    bpl[1] = (uint8_t *)p; p += bplsz;
    font = (uint16_t *)p;

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

    unpack_font();

    *(volatile void **)0x68 = CIA_IRQ;
    cust->cop1lc.p = copper;

    print_screen();
    setup_options();

    wait_bos();
    cust->dmacon = 0x81c0; /* enable copper/bitplane/blitter DMA */
    cust->intena = 0x8008; /* enable CIA-A interrupts */

    /* Loop while no LMB or joystick fire. */
    while ((ciaa->pra & 0xc0) == 0xc0) {
        wait_bos();
        if ((keycode = keycode_buffer)) {
            keycode_buffer = 0;
            /* Space? Break. */
            if (keycode == 0x40)
                break;
            update_option(keycode - 0x50);
        }
    }

    /* Wait for idle then knock it on the head. */
    waitblit();
    wait_bos();
    cust->dmacon = 0x01c0;
    cust->intena = 0x0008;
    clear_colors();

    /* Marshal selected options into a packed longword. */
    for (i = 0; i < 4; i++) {
        optres <<= 4;
        optres |= opts[i].dfl;
    }

    return optres;
}

asm (
"    .data                          \n"
"packfont: .incbin \"../FONT2_8X8.BIN\"\n"
"    .text                          \n"
);

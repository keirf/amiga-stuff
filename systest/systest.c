/*
 * systest.c
 * 
 * System Tests:
 *  - Memory
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

unsigned int mfm_decode_track(void *mfmbuf, void *headers, void *data,
                              uint16_t mfm_bytes);
void mfm_encode_track(void *mfmbuf, uint16_t tracknr, uint16_t mfm_bytes);

/* Write to INTREQ twice at end of ISR to prevent spurious re-entry on 
 * A4000 with faster processors (040/060). */
#define IRQ_RESET(x) do {                       \
    uint16_t __x = (x);                         \
    cust->intreq = __x;                         \
    cust->intreq = __x;                         \
} while (0)

#define IRQ(name)                                                          \
static void c_##name(void) attribute_used;                                 \
void name(void);                                                           \
asm (                                                                      \
#name":                             \n"                                    \
"    movem.l %d0-%d1/%a0-%a1,-(%sp) \n" /* Save a c_exception_frame */     \
"    move.b  16(%sp),%d0            \n" /* D0 = SR[15:8] */                \
"    and.b   #7,%d0                 \n" /* D0 = SR.IRQ_MASK */             \
"    jne     1f                     \n" /* SR.IRQ_MASK == 0? */            \
"    move.l  %sp,user_frame         \n" /* If so ours is the user_frame */ \
"1:  jbsr    c_"#name"              \n"                                    \
"    movem.l (%sp)+,%d0-%d1/%a0-%a1 \n"                                    \
"    rte                            \n"                                    \
)

/* Long-running test may be asynchronously cancelled by exit event. */
static struct cancellation test_cancellation;

/* Initialised by init.S */
struct mem_region {
    uint16_t attr;
    uint32_t lower, upper;
} mem_region[16] __attribute__((__section__(".bss.early_init")));
uint16_t nr_mem_regions = 16;
static int mem_region_cmp(const void *p, const void *q)
{
    const struct mem_region *_p = (const struct mem_region *)p;
    const struct mem_region *_q = (const struct mem_region *)q;
    return (_p->lower < _q->lower) ? -1 : (_p->lower > _q->lower) ? 1 : 0;
}

static uint16_t pointer_sprite[] = {
    0x0000, 0x0000,
    0x8000, 0xc000,
    0xc000, 0x7000,
    0x7000, 0x3c00,
    0x7c00, 0x3f00,
    0x3f00, 0x1fc0,
    0x3fc0, 0x1fc0,
    0x1e00, 0x0f00,
    0x1f00, 0x0d80,
    0x0d80, 0x04c0,
    0x0cc0, 0x0460,
    0x0060, 0x0020,
    0x0000, 0x0000
};

const static uint16_t dummy_sprite[] = {
    0x0000, 0x0000
};

/* Display size and depth. */
#define xres    640
#define yres    169
#define bplsz   (yres*xres/8)
#define planes  3

/* Top-left coordinates of the display. */
#define diwstrt_h 0x81
#define diwstrt_v 0x46

/* Text area within the display. */
#define xstart  110
#define ystart  18
#define yperline 10

/* Chipset and CPU. */
#define CHIPSET_ocs 0
#define CHIPSET_ecs 1
#define CHIPSET_aga 2
#define CHIPSET_unknown 3
static const char *chipset_name[] = { "OCS", "ECS", "AGA", "???" };
static uint8_t chipset_type;
static uint8_t cpu_model; /* 680[x]0 */

/* PAL/NTSC and implied CPU frequency. */
static uint8_t is_pal;
static unsigned int cpu_hz;
#define PAL_HZ 7093790
#define NTSC_HZ 7159090

/* Regardless of intrinsic PAL/NTSC-ness, display may be 50 or 60Hz. */
static uint8_t vbl_hz;

/* VBL IRQ: 16- and 32-bit timestamps, and VBL counter. */
static volatile uint32_t stamp32;
static volatile uint16_t stamp16;
static volatile unsigned int vblank_count;

/* CIAA IRQ: Keyboard variables. */
static volatile uint8_t keycode_buffer, exit;
/* A buffer ring for holding up to 1024 consecutive keycodes without loss.
 * Avoids losing key events during the keyboard test. Note no use of volatile:
 * make sure to use barrier() as needed to synchronise with the irq handler. */
static uint8_t keycode_ring[1024];
static uint16_t keycode_prod, keycode_cons;

/* CIAB IRQ: FLAG (disk index) pulse counter. */
static volatile unsigned int disk_index_count;
static volatile uint32_t disk_index_time, disk_index_period;

static uint8_t *bpl[planes];
static uint16_t *font;
static void *alloc_start, *alloc_p;

static uint16_t copper[] = {
    0x008e, (diwstrt_v << 8) | diwstrt_h,
    0x0090, (((diwstrt_v+yres) & 0xFF) << 8) | ((diwstrt_h+xres/2) & 0xFF),
    0x0092, 0x003c, /* ddfstrt */
    0x0094, 0x00d4, /* ddfstop */
    0x0100, 0xb200, /* bplcon0 */
    0x0102, 0x0000, /* bplcon1 */
    0x0104, 0x0024, /* bplcon2 */
    0x0108, 0x0000, /* bpl1mod */
    0x010a, 0x0000, /* bpl2mod */
    0x00e0, 0x0000, /* bpl1pth */
    0x00e2, 0x0000, /* bpl1ptl */
    0x00e4, 0x0000, /* bpl2pth */
    0x00e6, 0x0000, /* bpl2ptl */
    0x00e8, 0x0000, /* bpl3pth */
    0x00ea, 0x0000, /* bpl3ptl */
    0x0120, 0x0000, /* spr0pth */
    0x0122, 0x0000, /* spr0ptl */
    0x0124, 0x0000, /* spr1pth */
    0x0126, 0x0000, /* spr1ptl */
    0x0128, 0x0000, /* spr2pth */
    0x012a, 0x0000, /* spr2ptl */
    0x012c, 0x0000, /* spr3pth */
    0x012e, 0x0000, /* spr3ptl */
    0x0130, 0x0000, /* spr4pth */
    0x0132, 0x0000, /* spr4ptl */
    0x0134, 0x0000, /* spr5pth */
    0x0136, 0x0000, /* spr5ptl */
    0x0138, 0x0000, /* spr6pth */
    0x013a, 0x0000, /* spr6ptl */
    0x013c, 0x0000, /* spr7pth */
    0x013e, 0x0000, /* spr7ptl */
    0x01a2, 0x0000, /* col17 */
    0x01a4, 0x0eec, /* col18 */
    0x01a6, 0x0e44, /* col19 */
    0x0180, 0x0103, /* col00 */
    0x0182, 0x0222, /* col01 */
    0x0184, 0x0ddd, /* col02 */
    0x0186, 0x0ddd, /* col03 */
    0x0188, 0x04c4, /* col04 */
    0x018a, 0x0222, /* col05 */
    0x018c, 0x0ddd, /* col06 */
    0x018e, 0x0ddd, /* col07 */
    0x008a, 0x0000, /* copjmp2 */
};

static uint16_t copper_2[] = {
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

#define assert(_p) do { if (!(_p)) __assert_fail(); } while (0)

#if 1
static void __assert_fail(void)
{
    cust->dmacon = cust->intena = 0x7fff;
    cust->color[0] = 0xf00;
    for (;;) ;
}
#else
/* Set a memory watchpoint in your debugger to catch this. */
#define __assert_fail() (*(volatile int *)0x80000 = 0)
#endif

/* Test suite. */
static void memcheck(void);
static void kbdcheck(void);
static void floppycheck(void);
static void joymousecheck(void);
static void audiocheck(void);
static void videocheck(void);

/* Keycodes used for menu navigation. */
enum {
    K_ESC = 0x45, K_CTRL = 0x63, K_LALT = 0x64,
    K_F1 = 0x50, K_F2, K_F3, K_F4, K_F5, K_F6, K_F7, K_F8, K_F9, K_F10
};

/* Menu options with associated key (c) and bounding box (x1,y),(x2,y). */
static uint8_t nr_menu_options, _menu_option_lock;
static struct menu_option {
    uint8_t x1, x2, y, c;
} *vbl_menu_option, menu_option[12]; /* sorted by y, then x1. */
static int menu_option_cmp(struct menu_option *m, uint8_t x, uint8_t y)
{
    if (m->y < y)
        return -1;
    if (m->y == y)
        return (m->x1 < x) ? -1 : (m->x1 > x) ? 1 : 0;
    return 1;
}

/* Lock menu options to prevent VBL IRQ from seeing inconsistent state. */
static void menu_option_update_start(void)
{
    /* The critical region must run to completion thus it is incompatible with
     * asynchronous cancellations. */
    assert(!cancellation_is_running(&test_cancellation));

    _menu_option_lock = 1;
    barrier(); /* lock /then/ modify */
}

static void menu_option_update_end(void)
{
    barrier(); /* modify /then/ unlock */
    _menu_option_lock = 0;
}

/* Checked by VBL IRQ. */
static int menu_option_is_being_updated(void)
{
    return _menu_option_lock;
}

/* Allocate chip memory. Automatically freed when sub-test exits. */
extern char HEAP_END[];
static void *allocmem(unsigned int sz)
{
    void *p = alloc_p;
    alloc_p = (uint8_t *)alloc_p + sz;
    assert((char *)alloc_p < HEAP_END);
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
        if (ms >= 1000) {
            sec = ms;
            ms = do_div(sec, 1000);
            sprintf(s, "%u.%us", sec, div32(ms, 100));
        } else {
            us = ((uint16_t)div32(us<<16, ticks_per_ms) * 1000u) >> 16;
            sprintf(s, "%u.%ums", ms, div32(us, 100));
        }
    }
}

static uint8_t detect_chipset_type(void)
{
    /* Type = VPOSR[14:8]. Ignore bit 4 as this identifies PAL vs NTSC. */
    uint8_t type = (cust->vposr >> 8) & 0x6f;
    switch (type) {
    case 0x00: /* 8361/8367 (Agnus, DIP); 8370/8371 (Fat Agnus, PLCC) */
        type = CHIPSET_ocs;
        break;
    case 0x20: /* 8372 (Fatter Agnus) through rev 4 */
    case 0x21: /* 8372 (Fatter Agnus) rev 5 */
        type = CHIPSET_ecs;
        break;
    case 0x22: /* 8374 (Alice) thru rev 2 */
    case 0x23: /* 8374 (Alice) rev 3 through rev 4 */
        type = CHIPSET_aga;
        break;
    default:
        type = CHIPSET_unknown;
        break;
    }
    return type;
}

static uint8_t detect_pal_chipset(void)
{
    return !(cust->vposr & (1u<<12));
}

static uint8_t detect_vbl_hz(void)
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
    return (ticks > 130000) ? 50 : 60;
}

static uint8_t detect_cpu_model(void)
{
    uint32_t model;

    /* Attempt to access control registers which are supported only in 
     * increasingly small subsets of the model range. */
    asm volatile (
        "lea    (0x10).w,%%a1  ; " /* %a0 = 0x10 (illegal_insn) */
        "move.l (%%a1),%%d1    ; " /* %d1 = old illegal_insn_vec */
        "lea    (1f).l,%%a0    ; " /* set illegal insn vector to... */
        "move.l %%a0,(%%a1)    ; " /* ...skip to end and restore %ssp */
        "move.l %%a7,%%a0      ; " /* save %ssp */
        "moveq  #0,%0          ; " /* 680[0]0 */
        "dc.l   0x4e7a0801     ; " /* movec %vbr,%d0  */ /* 68010+ only */
        "moveq  #1,%0          ; " /* 680[1]0 */
        "dc.l   0x4e7a0002     ; " /* movec %cacr,%d0 */ /* 68020+ only */
        "moveq  #2,%0          ; " /* 680[2]0 */
        "dc.l   0x4e7a0004     ; " /* movec %itt0,%d0 */ /* 68040+ only */
        "moveq  #4,%0          ; " /* 680[4]0 */
        "dc.l   0x4e7a0808     ; " /* movec %pcr,%d0  */ /* 68060 only */
        "moveq  #6,%0          ; " /* 680[6]0 */
        "1: move.l %%a0,%%a7   ; " /* restore %ssp */
        "move.l %%d1,(%%a1)    ; " /* restore illegal_insn_vec */
        : "=d" (model) : "0" (0) : "d0", "d1", "a0", "a1" );

    /* 68020 and 68030 implement the same control registers and so
     * require further discrimination. */
    if (model == 2) {
        uint32_t cacr;
        /* 68030 implements CACR.FD (Freeze Data Cache) as a r/w flag;
         * 68020 ignores writes, and reads as zero. */
        asm volatile (
            "move.l %0,%%d0    ; "
            "dc.l   0x4e7b0002 ; " /* movec %d0,%cacr */
            "dc.l   0x4e7a0002 ; " /* movec %cacr,%d0 */
            "move.l %%d0,%0    ; "
            : "=d" (cacr) : "0" (1u<<9) /* FD - Freeze Data Cache */ : "d0" );
        model = cacr ? 3 : 2;
    }

    return (uint8_t)model;
}

/* Wait for blitter idle. */
static void waitblit(void)
{
    while (*(volatile uint8_t *)&cust->dmaconr & (DMA_BBUSY >> 8))
        continue;
}

/* Wait for end of bitplane DMA. */
static void wait_bos(void)
{
    while (*(volatile uint8_t *)&cust->vhposr != 0xf0)
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
    uint16_t i;
    waitblit();
    cust->bltcon0 = 0x0100;
    cust->bltcon1 = 0x0000;
    cust->bltdmod = 0;
    for (i = 0; i < planes; i++) {
        cust->bltdpt.p = bpl[i] + y_start * (xres/8);
        cust->bltsize = (xres/16)|(y_nr<<6);
        waitblit();
    }
}

static void clear_whole_screen(void)
{
    /* This also clears the entire menu-option array. */
    menu_option_update_start();
    vbl_menu_option = NULL;
    nr_menu_options = 0;
    menu_option_update_end();

    /* Physically clear the screen. */
    clear_screen_rows(0, yres);
}

static void clear_text_rows(uint16_t y_start, uint16_t y_nr)
{
    struct menu_option *m, *n;
    uint16_t i, j;

    /* Search for any menu options which are being cleared. */
    for (i = 0, m = menu_option; i < nr_menu_options; i++, m++)
        if (m->y >= y_start)
            break;
    for (j = i, n = m; j < nr_menu_options; j++, n++)
        if (n->y >= (y_start + y_nr))
            break;

    /* Remove those menu options, if any, from the menu-option array. */
    if (i != j) {
        menu_option_update_start();
        if (vbl_menu_option >= n) {
            vbl_menu_option -= j-i;
        } else if (vbl_menu_option >= m) {
            vbl_menu_option = NULL;
        }
        memmove(m, n, (nr_menu_options-j)*sizeof(*m));
        nr_menu_options -= j-i;
        menu_option_update_end();
    }

    /* Physically clear the display rows. */
    clear_screen_rows(ystart + y_start * yperline, y_nr * yperline);
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
    struct menu_option _m, *m = NULL;
    uint16_t mi = 0;
    uint16_t _x = r->x, x = xstart + _x * 8;
    uint16_t _y = r->y, y = ystart + _y * yperline;
    const char *p = r->s;
    char c;

    clear_text_rows(_y, 1);

    while ((c = *p++) != '\0') {

        if (c != '$') {

            /* Normal character. */
            print_char(x, y, c);
            x += 8;
            _x++;

        } else if (m == NULL) {

            /* '$' starting a menu option */
            uint16_t i;
            char s[20];

            _m.c = *p++;
            _m.x1 = _x;
            _m.y = _y;

            /* Find location within sorted menu-option array. */
            assert(nr_menu_options < ARRAY_SIZE(menu_option));
            for (mi = 0, m = menu_option; mi < nr_menu_options; mi++, m++)
                if (menu_option_cmp(m, _x, _y) > 0)
                    break;

            /* Construct key-combo text. */
            if ((_m.c >= '1') && (_m.c <= '9')) {
                sprintf(s, "F%c:", _m.c);
                _m.c = _m.c - '1' + K_F1;
            } else if (_m.c == 'E') {
                sprintf(s, "ESC:");
                _m.c = K_ESC;
            } else if (_m.c == 'C') {
                sprintf(s, "Ctrl + L.Alt:");
                _m.c = K_CTRL;
            }

            /* Print key-combo text. */
            for (i = 0; (c = s[i]) != '\0'; i++) {
                print_char(x, y, c);
                x += 8;
                _x++;
            }

        } else {

            /* '$' ending a menu option */
            _m.x2 = _x;

            /* Shuffle and insert into the array. */
            menu_option_update_start();
            if (vbl_menu_option >= m)
                vbl_menu_option++;
            memmove(m+1, m, (nr_menu_options - mi) * sizeof(*m));
            *m = _m;
            nr_menu_options++;
            menu_option_update_end();
            m = NULL;

        }
    }

    assert(m == NULL);
}

static void print_menu_nav_line(void)
{
    char s[80];
    struct char_row r = { .x = 4, .y = 14, .s = s };
    sprintf(s, "$C main menu$  $E up one menu$");
    print_line(&r);
}

static void mainmenu(void)
{
    const static struct {
        void (*fn)(void);
        const char *name;
    } mainmenu_option[] = {
        { memcheck,      "Memory" },
        { kbdcheck,      "Keyboard" },
        { floppycheck,   "Floppy Drive" },
        { joymousecheck, "Mouse / Joystick Ports" },
        { audiocheck,    "Audio" },
        { videocheck,    "Video" }
    };

    uint8_t i;
    char s[80];
    struct char_row r = { .x = 4, .y = 0, .s = s };

    clear_whole_screen();
    keycode_buffer = 0;

    sprintf(s, "SysTest - by KAF <keir.xen@gmail.com>");
    print_line(&r);
    r.y++;
    sprintf(s, "------------------------------------");
    print_line(&r);
    r.y++;
    for (i = 0; i < ARRAY_SIZE(mainmenu_option); i++) {
        sprintf(s, "$%u %s$", i+1, mainmenu_option[i].name);
        print_line(&r);
        r.y++;
    }
    sprintf(s, "------ 680%u0 - %s/%s - %uHz -----%c",
            cpu_model, chipset_name[chipset_type],
            is_pal ? "PAL" : "NTSC",
            vbl_hz, is_pal ? '-' : ' ');
    print_line(&r);
    r.y++;
    sprintf(s, "https://github.com/keirf/Amiga-Stuff");
    print_line(&r);
    r.y++;
    sprintf(s, "build: %s %s", __DATE__, __TIME__);
    print_line(&r);
    r.y++;

    print_menu_nav_line();

    while ((i = keycode_buffer - K_F1) >= ARRAY_SIZE(mainmenu_option))
        continue;
    clear_whole_screen();
    keycode_buffer = 0;
    exit = 0;
    alloc_p = alloc_start;
    (*mainmenu_option[i].fn)();
}

static inline __attribute__((always_inline)) uint16_t lfsr(uint16_t x)
{
    asm volatile (
        "lsr.w #1,%0; bcc.s 1f; eor.w #0xb400,%0; 1:"
        : "=d" (x) : "0" (x));
    return x;
}

/* Fill every 32-bit word from @start to @end. */
static void fill_32(
    uint32_t fill, volatile uint16_t *start, volatile uint16_t *end)
{
    uint32_t x, y;
    asm volatile (
        "1: move.l %2,(%3)+; move.l %2,(%3)+; "
        "move.l %2,(%3)+; move.l %2,(%3)+; dbf %4,1b"
        : "=a" (x), "=d" (y)
        : "d" (fill), "0" (start), "1" ((end-start)/(2*4)-1));
}

/* Fill every other 16-bit word fromt @start to @end. */
static void fill_alt_16(
    uint16_t fill, volatile uint16_t *start, volatile uint16_t *end)
{
    uint32_t x, y;
    asm volatile (
        "1: move.w %2,(%3); move.w %2,4(%3); move.w %2,8(%3); "
        "move.w %2,12(%3); lea 16(%3),%3; dbf %4,1b"
        : "=a" (x), "=d" (y)
        : "d" (fill), "0" (start), "1" ((end-start+1)/(2*4)-1));
}

static uint16_t check_pattern(
    uint32_t check, volatile uint16_t *start, volatile uint16_t *end)
{
    uint32_t x, y, z, val;
    asm volatile (
        "1: move.l (%5)+,%2; eor.l %4,%2; or.l %2,%3; "
        "move.l (%5)+,%2; eor.l %4,%2; or.l %2,%3; "
        "move.l (%5)+,%2; eor.l %4,%2; or.l %2,%3; "
        "move.l (%5)+,%2; eor.l %4,%2; or.l %2,%3; "
        "dbf %6,1b; move.w %3,%2; swap %3; or.w %2,%3"
        : "=a" (x), "=d" (y), "=&d" (z), "=d" (val)
        : "d" (check), "0" (start), "1" ((end-start+1)/(2*4)-1), "3" (0));
    return (uint16_t)val;
}


struct test_memory_args {
    unsigned int round;
    struct char_row r;
    uint32_t start, end;
};
static int test_memory_range(void *_args)
{
    struct test_memory_args *args = _args;
    struct char_row *r = &args->r;
    volatile uint16_t *p;
    volatile uint16_t *start = (volatile uint16_t *)args->start;
    volatile uint16_t *end = (volatile uint16_t *)args->end;
    char *s = (char *)r->s;
    uint16_t a = 0, i, j, x;
    static uint16_t seed = 0x1234;

    /* We are run with asynchronous exit but async setup may have raced an exit
     * command so check for that before we start. */
    if (exit || (keycode_buffer == K_ESC))
        return 0;

    sprintf(s, "Testing 0x%p-0x%p", (char *)start, (char *)end-1);
    print_line(r);
    r->y++;

    /* 1. Random numbers. */
    sprintf(s, "Round %u.%u: Random Fill",
            args->round+1, 1);
    print_line(r);
    x = seed;
    for (p = start; p != end;) {
        *p++ = x = lfsr(x);
        *p++ = x = lfsr(x);
        *p++ = x = lfsr(x);
        *p++ = x = lfsr(x);
    }
    x = seed;
    for (p = start; p != end;) {
        a |= *p++ ^ (x = lfsr(x));
        a |= *p++ ^ (x = lfsr(x));
        a |= *p++ ^ (x = lfsr(x));
        a |= *p++ ^ (x = lfsr(x));
    }
    seed = x;

    /* Start with all 0s. Write 1s to even words. */
    sprintf(s, "Round %u.%u: Checkboard #1",
            args->round+1, 2);
    print_line(r);
    fill_32(0, start, end);
    fill_alt_16(~0, start, end);
    a |= check_pattern(0xffff0000, start, end);

    /* Start with all 0s. Write 1s to odd words. */
    sprintf(s, "Round %u.%u: Checkboard #2",
            args->round+1, 3);
    print_line(r);
    fill_32(0, start, end);
    fill_alt_16(~0, start+1, end);
    a |= check_pattern(0x0000ffff, start, end);

    /* Start with all 1s. Write 0s to even words. */
    sprintf(s, "Round %u.%u: Checkboard #3",
            args->round+1, 4);
    print_line(r);
    fill_32(~0, start, end);
    fill_alt_16(0, start, end);
    a |= check_pattern(0x0000ffff, start, end);

    /* Start with all 1s. Write 0s to odd words. */
    sprintf(s, "Round %u.%u: Checkboard #4",
            args->round+1, 5);
    print_line(r);
    fill_32(~0, start, end);
    fill_alt_16(0, start+1, end);
    a |= check_pattern(0xffff0000, start, end);

    /* Errors found: then print diagnostic and wait to exit. */
    if (a != 0) {
        for (i = j = 0; i < 16; i++)
            if ((a >> i) & 1)
                j++;
        sprintf(s, "Round %u: Errors found in %u bit position%c",
                args->round+1, j, (j > 1) ? 's' : ' ');
        print_line(r);
        r->y++;
        sprintf(s, "16-bit word: FEDCBA9876543210");
        print_line(r);
        r->y++;
        sprintf(s, " (X=error)   %c%c%c%c%c%c%c%c%c%c%c%c%c%c%c%c",
                (a & (1u<<15)) ? 'X' : '-', (a & (1u<<14)) ? 'X' : '-',
                (a & (1u<<13)) ? 'X' : '-', (a & (1u<<12)) ? 'X' : '-',
                (a & (1u<<11)) ? 'X' : '-', (a & (1u<<10)) ? 'X' : '-',
                (a & (1u<< 9)) ? 'X' : '-', (a & (1u<< 8)) ? 'X' : '-',
                (a & (1u<< 7)) ? 'X' : '-', (a & (1u<< 6)) ? 'X' : '-',
                (a & (1u<< 5)) ? 'X' : '-', (a & (1u<< 4)) ? 'X' : '-',
                (a & (1u<< 3)) ? 'X' : '-', (a & (1u<< 2)) ? 'X' : '-',
                (a & (1u<< 1)) ? 'X' : '-', (a & (1u<< 0)) ? 'X' : '-');
        print_line(r);
        /* Wait for async exit. */
        for (;;)
            continue;
    }

    return 0;
}

static void test_memory_slots(uint32_t slots, struct char_row *r)
{
    struct test_memory_args tm_args;
    char *s = (char *)r->s;
    uint16_t nr;

    /* Find first 0.5MB slot to test */
    for (nr = 0; nr < 32; nr++)
        if (slots & (1u << nr))
            break;
    if (nr == 32) {
        sprintf(s, "ERROR: No memory (above 256kB) to test!");
        print_line(r);
        while (!exit && (keycode_buffer != K_ESC))
            continue;
        goto out;
    }

    tm_args.round = 0;
    while (!exit && (keycode_buffer != K_ESC)) {

        tm_args.start = (nr == 0) ? 1 << 18 : nr << 19;
        tm_args.end = (nr + 1) << 19;

        tm_args.r = *r;
        call_cancellable_fn(&test_cancellation, test_memory_range, &tm_args);

        /* Next memory range, or next round if all ranges done. */
        do {
            if (++nr == 32) {
                nr = 0;
                tm_args.round++;
            }
        } while (!(slots & (1u << nr)));
    }

out:
    keycode_buffer = 0;
}

static void memcheck_direct_scan(void)
{
    volatile uint16_t *p;
    volatile uint16_t *q;
    char s[80];
    struct char_row r = { .s = s }, _r;
    uint32_t ram_slots = 0, aliased_slots = 0;
    uint16_t a, b, i, j;
    uint8_t key = 0xff;
    unsigned int fast_chunks, chip_chunks, slow_chunks, tot_chunks, holes;
    int dodgy_slow_ram = 0;

    clear_whole_screen();
    print_menu_nav_line();

    r.x = 4;
    sprintf(s, "-- Direct Memory Scan --");
    print_line(&r);
    r.x = 0;
    r.y += 2;

    /* 0xA00000-0xBFFFFF: CIA registers alias throughout this range */
    for (i = 20; i < 24; i++)
        aliased_slots |= (1u << i);

    /* 0xC00000-0xD7FFFF: If slow memory is absent then custom registers alias
     * here. We detect this by writing to what would be INTENA and checking 
     * for changes to what would be INTENAR. If we see no change then we are 
     * not writing to the custom registers and _EXRAM must be asserted at 
     * Gary. */
    for (i = 24; i < 27; i++) {
        uint16_t intenar = cust->intenar;
        p = (volatile uint16_t *)0 + (i << 18);
        p[0x9a/2] = 0x7fff; /* clear all bits in INTENA */
        j = cust->intenar;
        a = p[0x1c/2];
        p[0x9a/2] = 0xbfff; /* set all bits in INTENA except master enable */
        b = p[0x1c/2];
        if (a != b) {
            aliased_slots |= (1u << i);
            cust->intena = 0x7fff;
            cust->intena = 0x8000 | intenar;
        }
    }

    /* Detect CHIP, FAST and SLOW RAM. 
     * ram_slots: mask of 512kB chunks detected to contain working ram. */
    for (i = 0; i < 27; i++) {
        if (aliased_slots & (1u << i))
            continue;
        p = (volatile uint16_t *)s + (i << 18);
        p[0] = 0x5555;
        p[1<<17] = 0xaaaa;
        if ((p[0] != 0x5555) || (p[1<<17] != 0xaaaa)) {
            p[0] = p[1<<17] = 0;
            continue;
        }
        for (j = 0; j < i; j++) {
            q = (volatile uint16_t *)s + (j << 18);
            if ((ram_slots & (1u << j)) && (*q == 0x5555))
                break;
        }
        if (j == i)
            ram_slots |= 1u << i;
        else
            aliased_slots |= 1u << i;
        p[0] = p[1<<17] = 0;
    }

    /* Count up 512kB chunks of CHIP, FAST, and SLOW RAM. 
     * {chip,fast,slow}_chunks: # chunks of respective type 
     * tot_chunks: sum of above */
    holes = chip_chunks = fast_chunks = slow_chunks = 0;
    for (i = 0; i < 4; i++) {
        if (ram_slots & (1u << i)) {
            if (chip_chunks < i)
                holes++;
            chip_chunks++;
        }
    }
    for (i = 4; i < 20; i++) {
        if (ram_slots & (1u << i)) {
            if (fast_chunks < (i-4))
                holes++;
            fast_chunks++;
        }
    }
    for (i = 24; i < 27; i++) {
        if (ram_slots & (1u << i)) {
            if (slow_chunks < (i-24))
                holes++;
            slow_chunks++;
        }
    }
    tot_chunks = chip_chunks + fast_chunks + slow_chunks;

    sprintf(s, "** %u.%u MB Total Memory Detected **",
            tot_chunks >> 1, (tot_chunks & 1) ? 5 : 0);
    print_line(&r);
    r.y++;
    sprintf(s, "(Chip %u.%u MB -- Fast %u.%u MB -- Slow %u.%u MB)",
            chip_chunks >> 1, (chip_chunks & 1) ? 5 : 0,
            fast_chunks >> 1, (fast_chunks & 1) ? 5 : 0,
            slow_chunks >> 1, (slow_chunks & 1) ? 5 : 0);
    print_line(&r);
    r.y++;
    if (holes) {
        sprintf(s, "WARNING: %u holes in memory map?? (ram %08x; alias %08x)",
                holes, ram_slots, aliased_slots);
        print_line(&r);
        r.y++;
    }
    if (!(aliased_slots & (1u<<24)) && !(ram_slots & (1u<<24))) {
        sprintf(s, "WARNING: Possible faulty SLOW RAM detected");
        print_line(&r);
        r.y++;
        dodgy_slow_ram = 1;
    }

    r.y++;

    while (!exit) {
        sprintf(s, "$1 Test All Memory (excludes first 256kB Chip)$");
        print_line(&r);
        r.y++;
        if (dodgy_slow_ram) {
            sprintf(s, "$2 Force Test 0.5MB Slow (Trapdoor) RAM$");
            print_line(&r);
        }
        r.y--;

        for (;;) {
            /* Grab a key */
            while (!exit && !(key = keycode_buffer))
                continue;
            keycode_buffer = 0;
            /* Handle exit conditions */
            if (exit || (key == K_ESC))
                goto out;
            /* Check for keys F1-F2 only */
            if ((key == K_F1) || (dodgy_slow_ram && (key == K_F2)))
                break;
        }

        clear_text_rows(r.y, 4);
        _r = r;
        test_memory_slots((key == K_F1) ? ram_slots : 1u << 24, &_r);
        clear_text_rows(r.y, 4);
    }

out:
    clear_whole_screen();
}

static void kickstart_memory_list(void)
{
    char s[80];
    struct char_row r = { .s = s };
    unsigned int i, base = 0;
    uint32_t a, b;
    uint8_t key;

    clear_whole_screen();
    print_menu_nav_line();

    r.x = 4;
    sprintf(s, "-- Kickstart Memory List --");
    print_line(&r);

    r.x = 0;
    r.y = 2;
    sprintf(s, " #: LOWER    - UPPER     TYPE   SIZE");
    print_line(&r);

    for (;;) {
    print_page:
        clear_text_rows(3, 10);
        r.x = 0;
        r.y = 3;
        for (i = base; (i < nr_mem_regions) && (i < base+8); i++) {
            a = mem_region[i].lower & ~0xffff;
            b = (mem_region[i].upper + 0xffff) & ~ 0xffff;
            sprintf(s, "%2u: %08x - %08x  %s  %3u.%u MB",
                    i, a, b,
                    mem_region[i].attr & 2 ? "Chip" :
                    (a >= 0x00c00000) && (a < 0x00d00000) ? "Slow" : "Fast",
                    (b-a) >> 20, ((b-a)>>19)&1 ? 5 : 0);
            print_line(&r);
            r.y++;
        }

        r.x = 4;
        r.y = 12;
        sprintf(s, "Page %u/%u  $1 Prev$  $2 Next$",
                (base+8)>>3, (nr_mem_regions+7)>>3);
        print_line(&r);

        for (;;) {
            while (!(key = keycode_buffer) && !exit)
                continue;
            keycode_buffer = 0;
            
            if (exit || (key == K_ESC))
                goto out;

            switch (key) {
            case K_F1:
                if (base != 0) {
                    base -= 8;
                    goto print_page;
                }
                break;
            case K_F2:
                if (base+8 < nr_mem_regions) {
                    base += 8;
                    goto print_page;
                }
                break;
            }
        }
    }

out:
    clear_whole_screen();
}

static void kickstart_memory_test(struct char_row *r)
{
    struct test_memory_args tm_args;
    unsigned int i, nr_done;
    uint32_t a, b;

    tm_args.round = 0;
    while (!exit && (keycode_buffer != K_ESC)) {
        nr_done = 0;
        for (i = 0; i < nr_mem_regions; i++) {
            /* Calculate inclusive range [a,b] with limits expanded to 64kB
             * alignment (Kickstart sometimes steals RAM from the limits). */
            a = max_t(uint32_t, 0x40000, mem_region[i].lower & ~0xffff);
            b = ((mem_region[i].upper + 0xffff) & ~0xffff) - 1;
            tm_args.start = a;
            while (!exit && (keycode_buffer != K_ESC)
                   && tm_args.start && (tm_args.start < b)) {
                /* Calculate inclusive end limit for this chunk. Chunk size
                 * is 512kB or remainder of region, whichever is smaller. */
                tm_args.end = ((tm_args.start + 0x80000) & ~0x7ffff) - 1;
                tm_args.end = min_t(uint32_t, tm_args.end, b);
                /* test_memory_range() expects the end bound to be +1. */
                tm_args.end++;
                tm_args.r = *r;
                call_cancellable_fn(&test_cancellation, test_memory_range,
                                    &tm_args);
                tm_args.start = tm_args.end;
                nr_done++;
            }
        }
        /* If we did't do any work report this as an error and wait to exit. */
        if (!nr_done) {
            sprintf((char *)r->s, "ERROR: No memory (above 256kB) to test!");
            print_line(r);
            while (!exit && (keycode_buffer != K_ESC))
                continue;
            goto out;
        }
        /* Otherwise onto the next round... */
        tm_args.round++;
    }

out:
    keycode_buffer = 0;
}

static void memcheck(void)
{
    char s[80];
    struct char_row r = { .s = s };
    uint32_t a, b, chip, fast, slow, tot;
    uint8_t key;
    unsigned int i;

    while (!exit) {
        print_menu_nav_line();

        r.x = 4;
        r.y = 0;
        sprintf(s, "-- Kickstart Memory Scan --");
        print_line(&r);
        r.x = 0;
        r.y += 2;

        chip = fast = slow = 0;
        for (i = 0; i < nr_mem_regions; i++) {
            a = mem_region[i].lower & ~0xffff;
            b = (mem_region[i].upper + 0xffff) & ~ 0xffff;
            if (mem_region[i].attr & 2)
                chip += b-a;
            else if ((a >= 0x00c00000) && (a < 0x00d00000))
                slow += b-a;
            else
                fast += b-a;
        }
        tot = chip + fast + slow;

        sprintf(s, "** %u.%u MB Total Memory Detected **",
                tot >> 20, (tot>>19)&1 ? 5 : 0);
        print_line(&r);
        r.y++;
        sprintf(s, "(Chip %u.%u MB -- Fast %u.%u MB -- Slow %u.%u MB)",
                chip >> 20, (chip>>19)&1 ? 5 : 0,
                fast >> 20, (fast>>19)&1 ? 5 : 0,
                slow >> 20, (slow>>19)&1 ? 5 : 0);
        print_line(&r);
        r.y += 2;

    menu_items:
        sprintf(s, "$1 Test All Memory (excludes first 256kB Chip)$");
        print_line(&r);
        r.y++;
        sprintf(s, "$2 List Memory Regions$");
        print_line(&r);
        r.y++;
        sprintf(s, "$3 Direct Memory Scan (Ignores Kickstart)$");
        print_line(&r);
        r.y++;

        do {
            while (!(key = keycode_buffer) && !exit)
                continue;
            keycode_buffer = 0;

            if (exit || (key == K_ESC))
                goto out;
        } while ((key < K_F1) || (key > K_F3));

        switch (key) {
        case K_F1:
            r.y = 5;
            clear_text_rows(r.y, 4);
            kickstart_memory_test(&r);
            r.y = 5;
            clear_text_rows(r.y, 4);
            if (!exit)
                goto menu_items;
            break;
        case K_F2:
            kickstart_memory_list();
            break;
        case K_F3:
            memcheck_direct_scan();
            break;
        }
    }

out:
    clear_whole_screen();
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
    draw_hollow_rect(bpl[1], 20, 8, 601, 106, 1);
    for (i = 0; i < ARRAY_SIZE(keymap); i++) {
        cap = &keymap[i];
        if (!cap->h)
            continue;
        /* Draw the outline rectangle. */
        x = 30 + cap->x;
        y = 13 + cap->y;
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
    r.y = 14;
    sprintf(s, "$C main menu$");
    print_line(&r);
    r.y = 11;

    i = 0;
    s[0] = '\0';
    keycode_cons = keycode_prod; /* clear the keycode ring */
    while (!exit) {
        /* Wait for a key in the keycode ring and then consume it. */
        uint8_t key;
        while (keycode_cons == keycode_prod)
            barrier(); /* see updates from keyboard irq */
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
        draw_filled_rect(bpl[0], x+1, y+1, cap->w-1, cap->h-1, !(key & 0x80));
        if (!(key & 0x80)) /* permanent highlight in bpl[2] */
            draw_filled_rect(bpl[2], x+1, y+1, cap->w-1, cap->h-1, 1);
        if ((key & 0x7f) == 0x44) { /* Return needs a bodge.*/
            draw_filled_rect(bpl[0], x-7, y+1, 8, 14, !(key & 0x80));
            if (!(key & 0x80)) /* permanent highlight in bpl[2] */
                draw_filled_rect(bpl[2], x-7, y+1, 8, 14, 1);
        }
    }

    /* Clean up. */
    wait_bos();
    cust->cop2lc.p = copper_2;
    keycode_buffer = 0;
}

/* Select @drv and set motor on or off. */
static void drive_select_motor(unsigned int drv, int on)
{
    ciab->prb |= 0xf9; /* motor-off, deselect all */
    if (on)
        ciab->prb &= ~CIABPRB_MTR; /* motor-on */
    ciab->prb &= ~(CIABPRB_SEL0 << drv); /* select drv */
}

static void drive_check_ready(struct char_row *r)
{
    uint32_t s = get_time(), e, one_sec = div32(cpu_hz, 10);
    int ready;

    do {
        ready = !!(~ciaa->pra & CIAAPRA_RDY);
        e = get_time();
    } while (!ready && ((e - s) < one_sec));

    if (ready) {
        char delaystr[10];
        e = get_time();
        ticktostr(e - s, delaystr);
        sprintf((char *)r->s,
                (e - s) < div32(one_sec, 1000)
                ? "READY too fast (%s): Gotek or hacked PC drive?"
                : (e - s) <= (one_sec>>1)
                ? "READY in good time (%s)"
                : (e - s) < one_sec
                ? "READY late (%s): slow motor spin-up?"
                : "READY *very* late (%s): slow motor spin-up?",
                delaystr);
    } else {
        sprintf((char *)r->s,
                "No READY signal: PC or Escom drive?");
    }

    print_line(r);
    r->y++;

    if (ready) {
        do {
            ready = !!(~ciaa->pra & CIAAPRA_RDY);
            e = get_time();
        } while (ready && ((e - s) < one_sec));
        if (!ready) {
            sprintf((char *)r->s,
                    "READY signal is oscillating: hacked PC drive?");
            print_line(r);
            r->y++;
        }
    }
}

/* Returns number of head steps to find cylinder 0. */
static int cur_cyl;
static unsigned int seek_cyl0(void)
{
    unsigned int steps = 0;

    ciab->prb |= CIABPRB_DIR | CIABPRB_SIDE; /* outward, side 0 */
    delay_ms(18);

    cur_cyl = 0;

    while (!(~ciaa->pra & CIAAPRA_TK0)) {
        ciab->prb &= ~CIABPRB_STEP;
        ciab->prb |= CIABPRB_STEP;
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
        ciab->prb |= CIABPRB_DIR; /* outward */
    } else {
        ciab->prb &= ~CIABPRB_DIR; /* inward */
    }
    delay_ms(18);

    while (diff--) {
        ciab->prb &= ~CIABPRB_STEP;
        ciab->prb |= CIABPRB_STEP;
        delay_ms(3);
    }

    delay_ms(15);

    cur_cyl = cyl;

    if (!(track & 1)) {
        ciab->prb |= CIABPRB_SIDE; /* side 0 */
    } else {
        ciab->prb &= ~CIABPRB_SIDE; /* side 1 */
    }
}

static void disk_read_track(void *mfm, uint16_t mfm_bytes)
{
    cust->dskpt.p = mfm;
    cust->adkcon = 0x7f00; /* clear disk flags */
    cust->intreq = INT_DSKBLK; /* clear dsk-blk-done */
    cust->adkcon = 0x9500; /* MFM, wordsync */
    cust->dsksync = 0x4489; /* sync 4489 */
    cust->dsklen = 0x8000 + mfm_bytes / 2;
    cust->dsklen = 0x8000 + mfm_bytes / 2;
}

static void disk_write_track(void *mfm, uint16_t mfm_bytes)
{
    cust->dskpt.p = mfm;
    cust->adkcon = 0x7f00; /* clear disk flags */
    cust->intreq = INT_DSKBLK; /* clear dsk-blk-done */
    /* 140ns precomp for cylinders 40-79 (exactly the same as
     * trackdisk.device, tested on Kickstart 3.1). */
    if (cur_cyl >= 40)
        cust->adkcon = 0xa000;
    cust->adkcon = 0x9100; /* MFM, no wordsync */
    cust->dsklen = 0xc000 + mfm_bytes / 2;
    cust->dsklen = 0xc000 + mfm_bytes / 2;
}

static void disk_wait_dma(void)
{
    unsigned int i;
    for (i = 0; i < 1000; i++) {
        if (cust->intreqr & INT_DSKBLK) /* dsk-blk-done? */
            break;
        delay_ms(1);
    }
    cust->dsklen = 0x4000; /* no more dma */
}

static uint32_t drive_id(unsigned int drv)
{
    uint32_t id = 0;
    uint8_t mask = CIABPRB_SEL0 << drv;
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

static unsigned int drive_signal_test(unsigned int drv, struct char_row *r)
{
    char *s = (char *)r->s;
    uint8_t pra, old_pra, key = 0;
    unsigned int on, old_disk_index_count;
    uint32_t rdy_delay, mtr_time, key_time, mtr_timeout;
    int rdy_changed;

    /* Motor on for 30 seconds at a time when there is no user input. */
    mtr_timeout = 30 * div32(cpu_hz, 10);

    while (!exit && (key != K_ESC)) {

        on = 0;
        drive_select_motor(drv, 0);
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
        print_line(r);
        r->y += 3;

        sprintf(s, "$1 DF0$  $2 DF1$  $3 DF2$  $4 DF3$");
        print_line(r);
        r->y++;
        sprintf(s, "$5 Motor On/Off$  $6 Step$");
        print_line(r);
        r->y -= 3;

        old_pra = ciaa->pra;
        mtr_time = get_time();
        rdy_delay = rdy_changed = 0;
        old_disk_index_count = disk_index_count = 0;
        key_time = get_time();
        key = 1; /* force print */

        while (!exit) {
            if (((pra = ciaa->pra) != old_pra) || key) {
                rdy_changed = !!((old_pra ^ pra) & CIAAPRA_RDY);
                if (rdy_changed)
                    rdy_delay = get_time() - mtr_time;
                sprintf(s, "MTR=%s CIAAPRA=0x%02x (%s %s %s %s)",
                        on ? "On" : "Off",
                        pra,
                        ~pra & CIAAPRA_CHNG ? "CHG" : "",
                        ~pra & CIAAPRA_WPRO ? "WPR" : "",
                        ~pra & CIAAPRA_TK0  ? "TK0" : "",
                        ~pra & CIAAPRA_RDY  ? "RDY" : "");
                print_line(r);
                old_pra = pra;
            }
            if ((old_disk_index_count != disk_index_count)
                || rdy_changed || key) {
                char rdystr[10], idxstr[10];
                ticktostr(disk_index_period, idxstr);
                ticktostr(rdy_delay, rdystr);
                sprintf(s, "%u Index Pulses (period %s); MTR%s->RDY%u %s",
                        disk_index_count, idxstr, on?"On":"Off",
                        !!(old_pra & CIAAPRA_RDY), rdystr);
                r->y++;
                print_line(r);
                r->y--;
                old_disk_index_count = disk_index_count;
                rdy_changed = 0;
            }
            key = (on && ((get_time() - key_time) >= mtr_timeout))
                ? 0x54 /* force motor off */ : keycode_buffer;
            if (!key)
                continue;
            keycode_buffer = 0;
            key_time = get_time();
            if ((key >= K_F1) && (key <= 0x53)) { /* F1-F4 */
                drive_select_motor(drv, 0);
                drv = key - K_F1;
                r->y--;
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
            } else if (key == K_ESC) { /* ESC */
                break;
            } else {
                key = 0;
            }
        }
    }

    /* Clean up. */
    drive_select_motor(drv, 0);
    return drv;
}

struct sec_header {
    uint8_t format, trk, sec, togo;
    uint32_t data_csum;
};

static unsigned int drive_read_test(unsigned int drv, struct char_row *r)
{
    char *s = (char *)r->s, retrystr[20];
    void *alloc_s = alloc_p;
    void *mfmbuf, *data;
    struct sec_header *headers;
    unsigned int i, mfm_bytes = 13100, nr_secs;
    uint16_t valid_map;
    int done = 0, retries;

    sprintf(s, "-- DF%u: Read Test --", drv);
    print_line(r);
    r->y++;

    mfmbuf = allocmem(mfm_bytes);
    headers = allocmem(12 * sizeof(*headers));
    data = allocmem(12 * 512);

    drive_select_motor(drv, 0);
    seek_cyl0();

    if (cur_cyl < 0) {
        sprintf(s, "No Track 0: Drive Not Present?");
        print_line(r);
        goto out;
    }

    if (~ciaa->pra & CIAAPRA_CHNG) {
        seek_track(2);
        if (~ciaa->pra & CIAAPRA_CHNG) {
            sprintf(s, "DSKCHG: No Disk In Drive?");
            print_line(r);
            goto out;
        }
    }

    drive_select_motor(drv, 1);
    drive_check_ready(r);

    for (i = 0; i < 160; i++) {
        retries = 0;
        do {
            retrystr[0] = '\0';
            if (retries)
                sprintf(retrystr, " attempt %u", retries+1);
            sprintf(s, "Reading Track %u...%s", i, retrystr);
            print_line(r);
            done = (exit || (keycode_buffer == K_ESC));
            if (done)
                goto out;
            if (retries++)
                seek_cyl0();
            if (retries == 5) {
                sprintf(s, "Cannot Read Track %u", i);
                print_line(r);
                goto out;
            }
            seek_track(i);
            memset(mfmbuf, 0, mfm_bytes);
            disk_read_track(mfmbuf, mfm_bytes);
            disk_wait_dma();
            nr_secs = mfm_decode_track(mfmbuf, headers, data, mfm_bytes);
            valid_map = 0;
            while (nr_secs--) {
                struct sec_header *h = &headers[nr_secs];
                if ((h->format = 0xff) && (h->trk == i) && !h->data_csum)
                    valid_map |= 1u<<h->sec;
            }
        } while (valid_map != 0x7ff);
    }

    sprintf(s, "All tracks read okay");
    print_line(r);

out:
    drive_select_motor(drv, 0);
    alloc_p = alloc_s;

    while (!done)
        done = (exit || keycode_buffer == K_ESC);
    keycode_buffer = 0;

    return drv;
}

static unsigned int drive_write_test(unsigned int drv, struct char_row *r)
{
    char *s = (char *)r->s, retrystr[20];
    void *alloc_s = alloc_p;
    void *mfmbuf;
    struct sec_header *headers;
    unsigned int i, j, mfm_bytes = 13100, nr_secs;
    uint16_t valid_map;
    int done = 0, retries;
    uint8_t rnd, *data;

    sprintf(s, "-- DF%u: Write Test --", drv);
    print_line(r);
    r->y++;

    mfmbuf = allocmem(mfm_bytes);
    headers = allocmem(12 * sizeof(*headers));
    data = allocmem(12 * 512);

    drive_select_motor(drv, 0);
    seek_cyl0();

    if (cur_cyl < 0) {
        sprintf(s, "No Track 0: Drive Not Present?");
        print_line(r);
        goto out;
    }

    if (~ciaa->pra & CIAAPRA_CHNG) {
        seek_track(2);
        if (~ciaa->pra & CIAAPRA_CHNG) {
            sprintf(s, "DSKCHG: No Disk In Drive?");
            print_line(r);
            goto out;
        }
    }

    if (~ciaa->pra & CIAAPRA_WPRO) {
        sprintf(s, "WRPRO: Disk is Write Protected?");
        print_line(r);
        goto out;
    }

    drive_select_motor(drv, 1);
    drive_check_ready(r);

    for (i = 158; i < 160; i++) {
        retries = 0;
        do {
            retrystr[0] = '\0';
            if (retries)
                sprintf(retrystr, " attempt %u", retries+1);
            sprintf(s, "Writing Track %u...%s", i, retrystr);
            print_line(r);
            done = (exit || (keycode_buffer == K_ESC));
            if (done)
                goto out;
            if (retries++)
                seek_cyl0();
            if (retries == 5) {
                sprintf(s, "Cannot Write Track %u", i);
                print_line(r);
                goto out;
            }
            seek_track(i);
            rnd = stamp32;
            memset(mfmbuf, rnd, mfm_bytes);
            /* erase */
            disk_write_track(mfmbuf, mfm_bytes);
            disk_wait_dma();
            /* write */
            mfm_encode_track(mfmbuf, i, mfm_bytes);
            disk_write_track(mfmbuf, mfm_bytes);
            disk_wait_dma();
            /* read */
            memset(mfmbuf, 0, mfm_bytes);
            disk_read_track(mfmbuf, mfm_bytes);
            disk_wait_dma();
            /* verify */
            nr_secs = mfm_decode_track(mfmbuf, headers, data, mfm_bytes);
            valid_map = 0;
            /* Check sector headers */
            while (nr_secs--) {
                struct sec_header *h = &headers[nr_secs];
                if ((h->format = 0xff) && (h->trk == i) && !h->data_csum)
                    valid_map |= 1u<<h->sec;
            }
            /* Check our verification token */
            for (j = 0; j < 11*512; j++)
                if (data[j] != rnd)
                    valid_map = 0;
        } while (valid_map != 0x7ff);
    }

    sprintf(s, "Tracks 158 & 159 written okay");
    print_line(r);

out:
    drive_select_motor(drv, 0);
    alloc_p = alloc_s;

    while (!done)
        done = (exit || keycode_buffer == K_ESC);
    keycode_buffer = 0;

    return drv;
}

static void floppycheck(void)
{
    char s[80];
    struct char_row r = { .x = 8, .y = 1, .s = s }, _r;
    uint8_t key = 0xff;
    unsigned int i, drv = 0;

    print_menu_nav_line();

    sprintf(s, "-- Floppy IDs --");
    print_line(&r);
    r.y++;

    for (i = 0; i < 4; i++) {
        uint32_t id = drive_id(i);
        sprintf(s, "DF%u: %08x (%s)", i, id,
                (id == -!!i) ? "Present" :
                (id !=  -!i) ? "???" :
                (i == 0) ? "Gotek?" : "Not Present");
        print_line(&r);
        r.y++;
    }
    r.y++;

    while (!exit) {

        sprintf(s, "-- DF%u: Selected --", drv);
        print_line(&r);
        r.y++;
        sprintf(s, "$1 DF0$  $2 DF1$  $3 DF2$  $4 DF3$");
        print_line(&r);
        r.y++;
        sprintf(s, "$5 Signal Test$");
        print_line(&r);
        r.y++;
        sprintf(s, "$6 Read Test$");
        print_line(&r);
        r.y++;
        sprintf(s, "$7 Write Test$");
        print_line(&r);
        r.y -= 4;

        for (;;) {
            /* Grab a key */
            while (!exit && !(key = keycode_buffer))
                continue;
            keycode_buffer = 0;
            /* Handle exit conditions */
            exit |= (key == K_ESC); /* ESC = exit */
            if (exit)
                break;
            /* Check for keys F1-F7 only */
            key -= K_F1; /* Offsets from F1 */
            if (key >= 7)
                continue;
            /* F5-F7: handled outside this loop */
            if (key > 3)
                break;
            /* F1-F4: DF0-DF3 */
            drv = key;
            sprintf(s, "-- DF%u: Selected --", drv);
            print_line(&r);
        }

        if (exit)
            break;

        clear_text_rows(r.y, 6);
        _r = r;

        switch (key) {
        case 4: /* F5 */
            drv = drive_signal_test(drv, &_r);
            break;
        case 5: /* F6 */
            drv = drive_read_test(drv, &_r);
            break;
        case 6: /* F7 */
            drv = drive_write_test(drv, &_r);
            break;
        }

        clear_text_rows(r.y, 6);
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

    print_menu_nav_line();

    sprintf(s, "-- Joy / Mouse Test --");
    print_line(&r);
    r.x = 0;
    r.y += 3;

    for (i = 0; !exit; i++) {

        /* ESC also means exit */
        exit |= (keycode_buffer == K_ESC);

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
    struct char_row r = { .x = 8, .y = 0, .s = s };
    static const uint8_t sine[] = { 0,19,39,57,74,89,102,113,120,125,127 };
    const unsigned int nr_500hz_samples = 40;
    const unsigned int nr_10khz_samples = 2;
    int8_t *aud_500hz = allocmem(nr_500hz_samples);
    int8_t *aud_10khz = allocmem(nr_10khz_samples);
    uint8_t key, channels = 0, lowfreq = 1;
    uint32_t period;
    unsigned int i;

    /* Generate the 500Hz waveform. */
    for (i = 0; i < 10; i++) {
        aud_500hz[i] = sine[i];
        aud_500hz[10+i] = sine[10-i];
        aud_500hz[20+i] = -sine[i];
        aud_500hz[30+i] = -sine[10-i];
    }

    /* Generate the 10kHz waveform. */
    aud_10khz[0] = 127;
    aud_10khz[1] = -127;

    print_menu_nav_line();

    sprintf(s, "-- Audio Test --");
    print_line(&r);
    r.y += 2;

    sprintf(s, "$1 Channel 0 (L)$");
    print_line(&r);
    r.y++;
    sprintf(s, "$2 Channel 1 (R)$");
    print_line(&r);
    r.y++;
    sprintf(s, "$3 Channel 2 (R)$");
    print_line(&r);
    r.y++;
    sprintf(s, "$4 Channel 3 (L)$");
    print_line(&r);
    r.y++;
    sprintf(s, "$5 Frequency 500Hz / 10kHz$");
    print_line(&r);
    r.y++;
    sprintf(s, "$6 Low Pass Filter On / Off$");
    print_line(&r);
    r.y += 2;

    /* period = cpu_hz / (2 * nr_samples * frequency) */
    period = div32(div32(div32(cpu_hz, 2), nr_500hz_samples), 500/*Hz*/);

    for (i = 0; i < 4; i++) {
        cust->aud[i].lc.p = aud_500hz;
        cust->aud[i].len = nr_500hz_samples / 2;
        cust->aud[i].per = (uint16_t)period;
        cust->aud[i].vol = 0;
    }
    cust->dmacon = DMA_SETCLR | DMA_AUDxEN; /* dma on */

    for (;;) {
        sprintf(s, "Waveform: %s",
                lowfreq ? "500Hz Sine" : "10kHz Square");
        wait_bos();
        print_line(&r);
        r.y++;

        sprintf(s, "Filter:   %s", (ciaa->pra & CIAAPRA_LED) ? "OFF" : "ON");
        wait_bos();
        print_line(&r);
        r.y++;

        sprintf(s, "Channels: 0=%s 1=%s 2=%s 3=%s",
                (channels & 1) ? "ON " : "OFF",
                (channels & 2) ? "ON " : "OFF",
                (channels & 4) ? "ON " : "OFF",
                (channels & 8) ? "ON " : "OFF");
        wait_bos();
        print_line(&r);
        r.y -= 2;

        while (!(key = keycode_buffer) && !exit)
            continue;
        keycode_buffer = 0;

        /* ESC also means exit */
        exit |= (key == K_ESC);
        if (exit)
            break;

        key -= K_F1;
        if (key < 4) {
            /* F1-F4: Switch channel 0-3 */
            channels ^= 1u << key;
            cust->aud[key].vol = (channels & (1u << key)) ? 64 : 0;
        } else if (key == 4) {
            /* F5: Frequency */
            lowfreq ^= 1;
            cust->dmacon = DMA_AUDxEN; /* dma off */
            for (i = 0; i < 4; i++) {
                /* NB. programmed period does not change: sample lengths 
                 * determine the frequency. */
                if (lowfreq) {
                    cust->aud[i].lc.p = aud_500hz;
                    cust->aud[i].len = nr_500hz_samples / 2;
                } else {
                    cust->aud[i].lc.p = aud_10khz;
                    cust->aud[i].len = nr_10khz_samples / 2;
                }
            }
            cust->dmacon = DMA_SETCLR | DMA_AUDxEN; /* dma on */
        } else if (key == 5) {
            /* F6: Low Pass Filter */
            ciaa->pra ^= CIAAPRA_LED;
        }
    }

    /* Clean up. */
    for (i = 0; i < 4; i++)
        cust->aud[i].vol = 0;
    cust->dmacon = DMA_AUDxEN; /* dma off */
}

static void videocheck(void)
{
    uint16_t *p, *cop, wait;
    unsigned int i, j, k;
    uint32_t bpl3;

    cop = p = allocmem(16384 /* plenty */);

    /* First line of gradient. */
    wait = 0x4401;

    /* Top border black. */
    *p++ = 0x0180;
    *p++ = 0x0000;

    /* Reduce to 2-plane depth as otherwise copper timings are slowed.  */
    *p++ = 0x0100; /* bplcon0 */
    *p++ = 0xa200; /* hires, 2 planes */

    /* Adjust bitplane 3 start for when we re-enable third bitplane at 
     * bottom of screen. */
    bpl3 = (uint32_t)bpl[2];
    bpl3 += 158 * xres/8;
    *p++ = 0x00e8; /* bpl3pth */
    *p++ = bpl3 >> 16;
    *p++ = 0x00ea; /* bpl3ptl */
    *p++ = bpl3;

    /* Create a vertical gradient of red, green, blue (across screen). 
     * Alternate white/black markers to indicate gradient changes. */
    for (i = 0; i < 16; i++) {
        for (j = 0; j < 10; j++) {
            /* WAIT line */
            *p++ = wait | 0x06;
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

    /* WAIT line, then re-enable third bitplane. */
    *p++ = wait | 0x06;
    *p++ = 0xfffe;
    *p++ = 0x0100; /* bplcon0 */
    *p++ = 0xb200; /* hires, 3 planes */

    /* End of copper list. */
    *p++ = 0xffff;
    *p++ = 0xfffe;

    /* Poke the new copper list at a safe point. */
    wait_bos();
    cust->cop2lc.p = cop;

    print_menu_nav_line();

    /* All work is done by the copper. Just wait for exit. */
    while (!exit)
        exit |= (keycode_buffer == K_ESC);

    /* Clean up. */
    wait_bos();
    cust->cop2lc.p = copper_2;
}

IRQ(CIAA_IRQ);
static void c_CIAA_IRQ(void)
{
    uint16_t t_s;
    uint8_t keycode, icr = ciaa->icr;
    static uint8_t prev_key, key;

    if (icr & CIAICR_SERIAL) {
        /* Received a byte from the keyboard MPU. */

        /* Grab the keycode and begin handshake. */
        keycode = ~ciaa->sdr;
        ciaa->cra |= CIACRA_SPMODE; /* start the handshake */
        t_s = get_ciaatb();

        /* Decode the keycode, detect Ctrl + L.Alt. */
        key = (keycode >> 1) | (keycode << 7); /* ROR 1 */
        if ((prev_key == K_CTRL) && (key == K_LALT))
            exit = 1; /* Ctrl + L.Alt */
        prev_key = key;

        /* Place key-down events in the basic keycode buffer. */
        if (!(key & 0x80))
            keycode_buffer = key;

        /* Place all keycodes in the buffer ring if there is space. */
        if ((keycode_prod - keycode_cons) != ARRAY_SIZE(keycode_ring))
            keycode_ring[keycode_prod++ & (ARRAY_SIZE(keycode_ring)-1)] = key;

        /* Cancel any long-running check if instructed to exit. */
        if (exit || (key == K_ESC))
            cancel_call(&test_cancellation);

        /* Wait to finish handshake over the serial line. We wait for 65 CIA
         * ticks, which is approx 90us: Longer than the 85us minimum dictated
         * by the HRM.  */
        while ((uint16_t)(t_s - get_ciaatb()) < 65)
            continue;
        ciaa->cra &= ~CIACRA_SPMODE; /* finish the handshake */
    }

    /* NB. Clear intreq.ciaa *after* reading/clearing ciaa.icr else we get a 
     * spurious extra interrupt, since intreq.ciaa latches the level of CIAA 
     * INT and hence would simply become set again immediately after we clear 
     * it. For this same reason (latches level not edge) it is *not* racey to 
     * clear intreq.ciaa second. Indeed AmigaOS does the same (checked 
     * Kickstart 3.1). */
    IRQ_RESET(INT_CIAA);
}

IRQ(CIAB_IRQ);
static void c_CIAB_IRQ(void)
{
    uint8_t icr = ciab->icr;

    if (icr & CIAICR_FLAG) {
        /* Disk index. */
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
    IRQ_RESET(INT_CIAB);
}

IRQ(VBLANK_IRQ);
static uint16_t vblank_joydat;
static void c_VBLANK_IRQ(void)
{
    static uint16_t x, y, prev_lmb;
    uint16_t lmb, joydat, hstart, vstart, vstop, i;
    uint16_t cur16 = get_ciaatb();
    struct menu_option *vm, *m;

    vblank_count++;

    stamp32 -= (uint16_t)(stamp16 - cur16);
    stamp16 = cur16;

    /* Update mouse pointer coordinates based on mouse input. */
    joydat = cust->joy0dat;
    x += (int8_t)(joydat - vblank_joydat);
    y += (int8_t)((joydat >> 8) - (vblank_joydat >> 8));
    x = min_t(int16_t, max_t(int16_t, x, 0), xres-1);
    y = min_t(int16_t, max_t(int16_t, y, 0), yres-1);
    vblank_joydat = joydat;

    /* Move the mouse pointer sprite. */
    hstart = (x>>1) + diwstrt_h-1;
    vstart = y + diwstrt_v;
    vstop = vstart + 11;
    pointer_sprite[0] = (vstart<<8)|(hstart>>1);
    pointer_sprite[1] = (vstop<<8)|((vstart>>8)<<2)|((vstop>>8)<<1)|(hstart&1);

    /* Skip menu-option handling if the array is being updated. This avoids us
     * seeing inconsistent state if we interrupted an update halfway. */
    if (menu_option_is_being_updated())
        goto out;

    m = NULL;
    vm = vbl_menu_option;

    /* Is mouse pointer currently within a menu-option bounding box? */
    if ((x >= xstart) && (y >= ystart)) {
        uint8_t cx = (x - xstart) >> 3;
        uint8_t cy = div32(y - ystart, yperline);
        for (i = 0, m = menu_option; i < nr_menu_options; i++, m++) {
            if ((m->x1 <= cx) && (m->x2 > cx) && (m->y == cy))
                break;
        }
        if (i == nr_menu_options)
            m = NULL;
    }

    /* Has current bounding box (or lack thereof) changed since last check? 
     * Update the display if so. */
    if (m != vm) {
        uint16_t fx, fw, fy;
        if (vm != NULL) {
            fx = xstart + vm->x1 * 8 - 1;
            fw = (vm->x2 - vm->x1) * 8 + 3;
            fy = ystart + vm->y * yperline;
            draw_filled_rect(bpl[2], fx, fy, fw, yperline, 0);
        }
        if (m != NULL) {
            fx = xstart + m->x1 * 8 - 1;
            fw = (m->x2 - m->x1) * 8 + 3;
            fy = ystart + m->y * yperline;
            draw_filled_rect(bpl[2], fx, fy, fw, yperline, 1);
        }
        vbl_menu_option = m;
    }

    /* When LMB is first pressed emit a keycode if we are within a menu box. */
    lmb = !(ciaa->pra & CIAAPRA_FIR0);
    if (lmb && !prev_lmb && (m != NULL)) {
        keycode_buffer = m->c;
        if (m->c == K_CTRL)
            exit = 1; /* Ctrl (+ L.Alt) sets the exit flag */
        /* Cancel any long-running check if instructed to exit. */
        if (exit || (m->c == K_ESC))
            cancel_call(&test_cancellation);
    }
    prev_lmb = lmb;

out:
    IRQ_RESET(INT_VBLANK);
}

void cstart(void)
{
    uint16_t i, j;
    char *p;

    /* Set keyboard serial line to input mode. */
    ciaa->cra &= ~CIACRA_SPMODE;

    /* Set up CIAA ICR. We only care about keyboard. */
    ciaa->icr = (uint8_t)~CIAICR_SETCLR;
    ciaa->icr = CIAICR_SETCLR | CIAICR_SERIAL;

    /* Set up CIAB ICR. We only care about FLAG line (disk index). */
    ciab->icr = (uint8_t)~CIAICR_SETCLR;
    ciab->icr = CIAICR_SETCLR | CIAICR_FLAG;

    /* Enable blitter DMA. */
    cust->dmacon = DMA_SETCLR | DMA_BLTEN;

    /* Clear BSS. */
    memset(_sbss, 0, _ebss-_sbss);

    /* Bitplanes and unpacked font allocated as directed by linker. */
    p = _end;
    for (i = 0; i < planes; i++) {
        bpl[i] = (uint8_t *)p;
        p += bplsz;
    }
    p = unpack_font(p);
    alloc_start = p;

    /* Poke bitplane addresses into the copper. */
    for (i = 0; copper[i] != 0x00e0/*bpl1pth*/; i++)
        continue;
    for (j = 0; j < planes; j++) {
        copper[i+j*4+1] = (uint32_t)bpl[j] >> 16;
        copper[i+j*4+3] = (uint32_t)bpl[j];
    }
    
    /* Poke sprite addresses into the copper. */
    for (i = 0; copper[i] != 0x0120/*spr0pth*/; i++)
        continue;
    copper[i+1] = (uint32_t)pointer_sprite >> 16;
    copper[i+3] = (uint32_t)pointer_sprite;
    for (j = 1; j < 8; j++) {
        copper[i+j*4+1] = (uint32_t)dummy_sprite >> 16;
        copper[i+j*4+3] = (uint32_t)dummy_sprite;
    }

    /* Clear bitplanes. */
    clear_whole_screen();

    clear_colors();

    m68k_vec->level2_autovector.p = CIAA_IRQ;
    m68k_vec->level6_autovector.p = CIAB_IRQ;
    m68k_vec->level3_autovector.p = VBLANK_IRQ;
    cust->cop1lc.p = copper;
    cust->cop2lc.p = copper_2;

    vblank_joydat = cust->joy0dat;

    /* Start CIAA Timer B in continuous mode. */
    ciaa->tblo = 0xff;
    ciaa->tbhi = 0xff;
    ciaa->crb = CIACRB_LOAD | CIACRB_START;

    wait_bos();
    cust->dmacon = DMA_SETCLR | DMA_COPEN | DMA_DSKEN;
    cust->intena = (INT_SETCLR | INT_CIAA | INT_CIAB | INT_VBLANK);

    /* Detect our hardware environment. */
    cpu_model = detect_cpu_model();
    chipset_type = detect_chipset_type();
    vbl_hz = detect_vbl_hz();
    is_pal = detect_pal_chipset();
    cpu_hz = is_pal ? PAL_HZ : NTSC_HZ;

    sort(mem_region, nr_mem_regions, sizeof(mem_region[0]), mem_region_cmp);

    /* Make sure the copper has run once through, then enable bitplane 
     * and sprite DMA starting from the next frame. */
    delay_ms(1);
    wait_bos();
    cust->dmacon = DMA_SETCLR | DMA_BPLEN | DMA_SPREN;

    for (;;)
        mainmenu();
}

asm (
"    .data                          \n"
"packfont: .incbin \"../base/font.raw\"\n"
"    .text                          \n"
);

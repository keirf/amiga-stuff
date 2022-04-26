/*
 * testkit.c
 * 
 * Amiga Tests:
 *  - Memory
 *  - Keyboard
 *  - Floppy Drive
 *  - Controller Ports
 *  - Audio
 *  - Video
 *  - CIA Timers
 *  - Serial / Parallel
 * 
 * Written & released by Keir Fraser <keir.xen@gmail.com>
 * 
 * This is free and unencumbered software released into the public domain.
 * See the file COPYING for more details, or visit <http://unlicense.org>.
 */

#include "testkit.h"

#define _IRQ(name, _lvl, _mask)                                         \
static void c_##name(struct c_exception_frame *frame) attribute_used;   \
static void c_spurious_IRQ##_lvl(void) attribute_used;                  \
void name(void);                                                        \
asm (                                                                   \
#name":                             \n"                                 \
"    movem.l %d0-%d1/%a0-%a1,-(%sp) \n" /* Save a c_exception_frame */  \
"    move.l  %sp,%a0                \n"                                 \
"    move.l  %a0,-(%sp)             \n"                                 \
"    move.w  (0xdff01c).l,%d0       \n" /* intenar */                   \
"    btst    #14,%d0                \n" /* master enable? */            \
"    jeq     1f                     \n"                                 \
"    and.w   (0xdff01e).l,%d0       \n" /* intreqr */                   \
"    and.w   #"#_mask",%d0          \n" /* specific interrupts? */      \
"    jeq     1f                     \n"                                 \
"    jbsr    c_"#name"              \n"                                 \
"2:  addq.l  #4,%sp                 \n"                                 \
"    movem.l (%sp)+,%d0-%d1/%a0-%a1 \n"                                 \
"    rte                            \n"                                 \
"1:  jbsr    c_spurious_IRQ"#_lvl"  \n"                                 \
"    jra     2b                     \n"                                 \
);                                                                      \
static void c_spurious_IRQ##_lvl(void)                                  \
{                                                                       \
    uint8_t cnt;                                                        \
    asm volatile (                                                      \
        "move.b (%1),%0; addq.b #1,(%1)"                                \
        : "=d" (cnt) : "a" (&spurious_autovector_count[_lvl]));         \
    if (cnt >= 16)                                                      \
        m68k_vec->level##_lvl##_autovector.p = crash_autovector[_lvl];  \
}

#define IRQ(name, _lvl, _mask) _IRQ(name, _lvl, _mask)

#define UNUSED_IRQ(_lvl)                                                \
IRQ(LEVEL##_lvl##_IRQ, _lvl, 0);                                        \
static void c_LEVEL##_lvl##_IRQ(struct c_exception_frame *frame)        \
{ /* unused */ }

/* Autovector handling. */
static void *crash_autovector[8]; /* fallback (crash) handlers */
static uint8_t spurious_autovector_count[8]; /* per-vbl counts */
volatile uint32_t spurious_autovector_total;

IRQ(SOFT_IRQ,   1, INT_SOFT);
IRQ(CIAA_IRQ,   2, INT_CIAA);
IRQ(VBLANK_IRQ, 3, INT_VBLANK);
UNUSED_IRQ(     4);
UNUSED_IRQ(     5);
IRQ(CIAB_IRQ,   6, INT_CIAB);
UNUSED_IRQ(     7);

/* Initialised by init.S */
struct mem_region mem_region[16] __attribute__((__section__(".data")));
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

/* Text area within the display. */
#define xstart  110
#define ystart  18
#define yperline 10

/* Chipset and CPU. */
static const char *chipset_name[] = { "OCS", "ECS", "AGA", "???" };
uint8_t chipset_type;
struct cpu cpu;

/* PAL/NTSC and implied CPU frequency. */
uint8_t is_pal;
unsigned int cpu_hz;
#define PAL_HZ 7093790
#define NTSC_HZ 7159090

/* Regardless of intrinsic PAL/NTSC-ness, display may be 50 or 60Hz. */
uint8_t vbl_hz;

/* VBL IRQ: 16- and 32-bit timestamps, and VBL counter. */
static volatile uint32_t stamp32;
static volatile uint16_t stamp16;
volatile unsigned int vblank_count;

uint8_t *bpl[planes];
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
    0x0108, 0x0000, 0x010a, 0x0000, /* bplxmod */
    0x00e0, 0x0000, 0x00e2, 0x0000, /* bpl1ptx */
    0x00e4, 0x0000, 0x00e6, 0x0000, /* bpl2ptx */
    0x00e8, 0x0000, 0x00ea, 0x0000, /* bpl3ptx */
    0x0120, 0x0000, 0x0122, 0x0000, /* spr0ptx */
    0x0124, 0x0000, 0x0126, 0x0000, /* spr1ptx */
    0x0128, 0x0000, 0x012a, 0x0000, /* spr2ptx */
    0x012c, 0x0000, 0x012e, 0x0000, /* spr3ptx */
    0x0130, 0x0000, 0x0132, 0x0000, /* spr4ptx */
    0x0134, 0x0000, 0x0136, 0x0000, /* spr5ptx */
    0x0138, 0x0000, 0x013a, 0x0000, /* spr6ptx */
    0x013c, 0x0000, 0x013e, 0x0000, /* spr7ptx */
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

/* Device-specific IRQ handlers. */
void ciaa_flag_IRQ(void);
void disk_index_IRQ(void);
uint8_t keyboard_IRQ(void);
void ciaata_IRQ(void);
void ciaatb_IRQ(void);
void ciabta_IRQ(void);
void joymouse_ciabta_IRQ(void);
void ciabtb_IRQ(void);
void ciaa_TOD_IRQ(void);
void ciab_TOD_IRQ(void);

/* Test suite. */
void memcheck(void);
void kbdcheck(void);
void floppycheck(void);
void joymousecheck(void);
void audiocheck(void);
void videocheck(void);
void ciacheck(void);
void battclock_test(void);
void serparcheck(void);

/* Menu options with associated key (c) and bounding box (x1,y),(x2,y). */
static uint8_t nr_menu_options;
static struct menu_option {
    uint8_t x1, x2, y, c;
} *active_menu_option, menu_option[12]; /* sorted by y, then x1. */
static int menu_option_cmp(struct menu_option *m, uint8_t x, uint8_t y)
{
    if (m->y < y)
        return -1;
    if (m->y == y)
        return (m->x1 < x) ? -1 : (m->x1 > x) ? 1 : 0;
    return 1;
}

/* Lock menu options for modification. */
static void menu_option_update_start(void)
{
    /* Simply prevent INT_SOFT from running, as otherwise it could observe
     * inconsistent state during our critical section. This also prevents
     * asynchronous cancellations while we are in the critical section, since
     * they are only triggered from INT_SOFT. */
    IRQ_DISABLE(INT_SOFT);
}

static void menu_option_update_end(void)
{
    IRQ_ENABLE(INT_SOFT);
}

/* Long-running test may be asynchronously cancelled by exit event. */
static struct cancellation test_cancellation;
static uint8_t do_cancel;
void call_cancellable_test(int (*fn)(void *), void *arg)
{
    /* Clear the cancellation flag unless there is already an exit command
     * pending. */
    do_cancel = do_exit || (keycode_buffer == K_ESC);
    barrier();
    call_cancellable_fn(&test_cancellation, fn, arg);
    /* We can't be in any blitter or menu-option-update critical section. */
    assert(cust->intenar & INT_SOFT);
}

/* Allocate chip memory. Automatically freed when sub-test exits. */
void *allocmem(unsigned int sz)
{
    void *p = alloc_p;
    alloc_p = (uint8_t *)alloc_p + sz;
    assert((char *)alloc_p < HEAP_END);
    return p;
}

/* Start/end a heap arena. */
void *start_allocheap_arena(void)
{
    return alloc_p;
}
void end_allocheap_arena(void *p)
{
    alloc_p = p;
}

uint16_t get_ciaatb(void)
{
    return get_ciatime(ciaa, tb);
}

uint32_t get_time(void)
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

void delay_ms(unsigned int ms)
{
    uint16_t ticks_per_ms = div32(cpu_hz+9999, 10000); /* round up */
    uint32_t s, t;

    s = get_time();
    do {
        t = div32(get_time() - s, ticks_per_ms);
    } while (t < ms);
}

/* Convert CIA ticks (tick/10cy) into a printable string. */
void ticktostr(uint32_t ticks, char *s)
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

unsigned int ms_to_ticks(uint16_t ms)
{
    uint16_t ticks_per_ms = div32(cpu_hz+9999, 10000); /* round up */
    return mul32(ms, ticks_per_ms);
}

static uint8_t get_deniseid(void)
{
    uint8_t id = cust->deniseid;
    int i;
    for (i = 0; i < 32; i++) {
        uint8_t id2 = cust->deniseid;
        if (id != id2)
            return 0xff;
    }
    return id;
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
        /* Full ECS requires Super Denise (8373) */
        type = (get_deniseid() & 2) ? CHIPSET_ocs : CHIPSET_ecs;
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

static uint32_t detect_ntsc_8361(void)
{
    uint32_t t, thresh;

    /* Count CIA ticks for 1/10 second. */
    unsigned int vbl_count = vbl_hz/10+1;
    vblank_count = 0;
    while (!vblank_count)
        continue;
    t = get_time();
    while (vblank_count < vbl_count)
        continue;
    t = get_time() - t;

    /* Threshold is halfway between the ideal PAL and NTSC tick counts. 
     * Divide by 10 because E Clock is CPU_HZ/10. 
     * Divide by 10 again because we measured for 1/10 second. */
    thresh = (PAL_HZ + NTSC_HZ) / 200;

    /* Return TRUE if NTSC. */
    return t > thresh;
}

static uint8_t detect_pal_chipset(void)
{
    uint8_t agnusid = (cust->vposr >> 8) & 0x7f;

    if (agnusid != 0)
        return !(agnusid & (1u<<4));

    /* NTSC 8361 "Thin Agnus" can misreport ID as 0x00 instead of 0x10. 
     * This causes it to be misdetected as a PAL-frequency system. 
     * Perform a secondary check based on CIA tick frequency. */
    return !detect_ntsc_8361();
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

int priv_call(int (*fn)(void *), void *arg);
asm (
    "priv_call:\n"
    "    lea    (0x20).w,%a1   \n" /* a0 = 0x20 (privilege_violation) */
    "    move.l (%a1),%d0      \n" /* d0 = old privilege_violation vector */
    "    lea    (1f).l,%a0     \n"
    "    move.l %a0,(%a1)      \n"
    "1:  ori.w  #0x2000,%sr    \n"
    "    move.l %d0,(%a1)      \n" /* restore privilege_violation vector */
    "    move.l %usp,%a0       \n"
    "    move.l 4(%a0),%a1     \n"
    "    move.l 8(%a0),-(%sp)  \n"
    "    jsr    (%a1)          \n"
    "    addq.l #4,%sp         \n"
    "    lea    (1f).l,%a0     \n"
    "    move.l %a0,2(%sp)     \n" /* fix up return-to-user %pc */
    "    rte                   \n"
    "1:  rts                   \n"
    );

static int _detect_cpu_model(void *_c)
{
    uint32_t model;
    struct cpu *c = _c;

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

    /* 68060: Retrieve PCR. */
    if (model == 6) {
        asm volatile (
            "dc.l   0x4e7a0808 ; " /* movec %pcr,%d0 */
            "move.l %%d0,%0    ; "
            : "=d" (c->pcr) : : "d0" );
    }

    c->model = (uint8_t)model;
    return 0;
}

static void detect_cpu_model(struct cpu *c)
{
    priv_call(_detect_cpu_model, c);
    if (c->model == 6) {
        /* "68060 Rev 6", "68EC060 Rev 3", etc. */
        uint8_t rev = (uint8_t)(c->pcr >> 8);
        bool_t no_fpu = (c->pcr >> 16) & 1;
        sprintf(c->name, "68060%s Rev %x",
                no_fpu ? " LC/EC" : "",
                rev);
    } else {
        /* "68000", "68020", etc. */
        sprintf(c->name, "680%u0", c->model);
    }
}

static void system_reset(void)
{
    int _system_reset(void *unused)
    {
        /* Amiga Hardware Reference Manual, 3ed, "System Control Hardware".
         * Replaces the code from EAB, thread 78548 "Amiga hardware reset".
         * By reading the RESET entry point directly from the ROM we avoid
         * jumping into the reset-time ROM overlay in low memory. That method
         * assumes there is a JMP instruction in overlay memory at address 0x2,
         * which is not the case for some non-Kickstart overlays, including the
         * ACA500plus FlashROM (see GitHub issue #55).
         */
        asm volatile (
            "lea.l  0x01000000,%a0\n" /* MAGIC_ROMEND */
            "sub.l  -0x14(%a0),%a0\n" /* MAGIC_SIZEOFFSET */
            "move.l 4(%a0),%a0\n"     /* Reset Initial PC */
            "subq.l #2,%a0\n"         /* 2nd RESET instruction (in ROM) */
            ".balignw 4,0x4e71\n"     /* Longword alignment (paranoia) */
            "reset; jmp (%a0)" );     /* JMP is executed from prefetch */
        return 0;
    }

    priv_call(_system_reset, NULL);
}

/* Wait for blitter idle. */
static void waitblit(void)
{
    /* Dummy first read works around delayed blitter-busy flag assertion 
     * in early Agnus versions. */
    (void)*(volatile uint8_t *)&cust->dmaconr;
    while (*(volatile uint8_t *)&cust->dmaconr & (DMA_BBUSY >> 8))
        continue;
}

/* Must be called before modifying the blitter registers. On return the
 * blitter is available and inactive. */
static void blitter_acquire(void)
{
    /* Blitter is used by INT_SOFT. Don't let it trample our blitter state. */
    IRQ_DISABLE(INT_SOFT);
    /* Make sure previous operation is complete before we modify state. */
    waitblit();
}

/* Called after modifying blitter registers. Others may trample your blitter 
 * state after you call this function (though they must waitblit first). */
static void blitter_release(void)
{
    IRQ_ENABLE(INT_SOFT);
}

/* Wait for end of bitplane DMA. */
void wait_bos(void)
{
    while (*(volatile uint8_t *)&cust->vhposr == 0xf0)
        continue;
    while (*(volatile uint8_t *)&cust->vhposr != 0xf0)
        continue;
}

void copperlist_set(const void *list)
{
    wait_bos();
    cust->cop2lc.p = (void *)list;
}

void copperlist_default(void)
{
    copperlist_set(copper_2);
}

/* Draw rectangle (x,y),(x+w,y+h) into bitplanes specified by plane_mask. 
 * The rectangle sets pixels if set is specified, else it clears pixels. */
void draw_rect(
    unsigned int x, unsigned int y,
    unsigned int w, unsigned int h,
    uint8_t plane_mask, int set)
{
    uint16_t i, bpl_off = y * (xres/8) + (x/16) * 2;
    uint16_t _w = (w+x+15)/16 - x/16, mask;

    blitter_acquire();

    /* DMA=BD, D=~AB (clear) D=A|B (set) */
    cust->bltcon0 = set ? 0x05fc : 0x050c;
    cust->bltcon1 = 0;
    cust->bltbmod = xres/8 - _w*2;
    cust->bltdmod = xres/8 - _w*2;
    /* Channel A is a pixel-granularity row mask. */
    mask = 0xffff;
    if (x&15) mask >>= x&15;
    cust->bltafwm = mask;
    mask = 0xffff;
    if ((x+w)&15) mask <<= 16-((x+w)&15);
    cust->bltalwm = mask;
    cust->bltadat = 0xffff;

    for (i = 0; i < planes; i++, plane_mask >>= 1) {
        if (!(plane_mask & 1))
            continue;
        cust->bltbpt.p = bpl[i] + bpl_off;
        cust->bltdpt.p = bpl[i] + bpl_off;
        cust->bltsize = _w | (h<<6);
        waitblit();
    }

    blitter_release();
}

void hollow_rect(
    unsigned int x, unsigned int y,
    unsigned int w, unsigned int h,
    uint8_t plane_mask)
{
    fill_rect(x, y, w, h, plane_mask);
    if ((w > 2) && (h > 2))
        clear_rect(x+1, y+1, w-2, h-2, plane_mask);
}

void text_highlight(uint16_t x, uint16_t y, uint16_t nr, int fill)
{
    uint16_t fx, fy;

    fx = xstart + x * 8;
    fy = ystart + y * yperline - 1;

    draw_rect(fx, fy, nr*8+1, yperline+1, 1<<2, fill);
}

static void clear_screen_rows(uint16_t y_start, uint16_t y_nr)
{
    uint16_t i;
    blitter_acquire();
    cust->bltcon0 = 0x0100;
    cust->bltcon1 = 0x0000;
    cust->bltdmod = 0;
    for (i = 0; i < planes; i++) {
        cust->bltdpt.p = bpl[i] + y_start * (xres/8);
        cust->bltsize = (xres/16)|(y_nr<<6);
        waitblit();
    }
    blitter_release();
}

void clear_whole_screen(void)
{
    /* This also clears the entire menu-option array. */
    menu_option_update_start();
    active_menu_option = NULL;
    nr_menu_options = 0;
    menu_option_update_end();

    /* Physically clear the screen. */
    clear_screen_rows(0, yres);
}

void clear_text_rows(uint16_t y_start, uint16_t y_nr)
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
        if (active_menu_option >= n) {
            active_menu_option -= j-i;
        } else if (active_menu_option >= m) {
            active_menu_option = NULL;
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

/* Prints a string of plain 8x8 characters straight to bitplane @b. */
void print_label(unsigned int x, unsigned int y, uint8_t b, const char *s)
{
    uint16_t bpl_off;
    uint8_t d, *p;
    unsigned int i;
    uint8_t c;

    while ((c = *s++) != '\0') {
        bpl_off = y * (xres/8) + (x/8);
        p = &packfont[8*(c-0x20)];
        for (i = 0; i < 8; i++) {
            d = *p++;
            bpl[b][bpl_off] |= d >> (x&7);
            if (x & 7)
                bpl[b][bpl_off+1] |= d << (8-(x&7));
            bpl_off += xres/8;
        }
        x += 8;
    }
}

static void print_char(uint16_t x, uint16_t y, char c)
{
    uint16_t *font_char = font + c * yperline * 2;
    uint16_t i, bpl_off = y * (xres/8) + (x/16) * 2;

    blitter_acquire();

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

    blitter_release();
}

void print_line(const struct char_row *r)
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
            } else if (_m.c == '0') {
                sprintf(s, "F10:");
                _m.c = K_F10;
            } else if (_m.c == 'E') {
                sprintf(s, "ESC:");
                _m.c = K_ESC;
            } else if (_m.c == 'C') {
                sprintf(s, "Ctrl + L.Alt:");
                _m.c = K_CTRL;
            } else if (_m.c == 'H') {
                sprintf(s, "Help:");
                _m.c = K_HELP;
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
            if (active_menu_option >= m)
                active_menu_option++;
            memmove(m+1, m, (nr_menu_options - mi) * sizeof(*m));
            *m = _m;
            nr_menu_options++;
            menu_option_update_end();
            m = NULL;

        }
    }

    assert(m == NULL);
}

void print_menu_nav_line(void)
{
    char s[80];
    struct char_row r = { .x = 4, .y = 14, .s = s };
    sprintf(s, "$C main menu$  $E up one menu$%7s[ATK %s]", "", version);
    print_line(&r);
}

/* Print a simple string into a sub-section of a text row. */
void print_text_box(unsigned int x, unsigned int y, const char *s)
{
    uint16_t _x = xstart + x * 8;
    uint16_t _y = ystart + y * yperline;
    char c;

    /* Clear the area we are going to print into. */
    clear_rect(_x, _y, strlen(s)*8+1, yperline, 3);

    while ((c = *s++) != '\0') {
        print_char(_x, _y, c);
        _x += 8;
    }
}

/* Pad a string to @width chars, using @c repeated on either side. */
static void centre_string(char *s, int width, char c)
{
    int l = strlen(s);
    int fill = (width - l) >> 1;
    if (l >= width)
        return;
    memmove(s+fill, s, l);
    memset(s, c, fill);
    l += fill;
    memset(s+l, c, width-l);
    s[width] = '\0';
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
        { joymousecheck, "Controller Ports" },
        { audiocheck,    "Audio" },
        { videocheck,    "Video" },
        { ciacheck,      "CIA, Chipset" },
        { battclock_test,"RTC (Batt.Clock)" },
        { serparcheck,   "Serial, Parallel" }
    };
    const int split = 6;

    uint8_t i;
    char s[80];
    struct char_row r = { .x = 0, .y = 0, .s = s };

    clear_whole_screen();
    keycode_buffer = 0;

    sprintf(s, "Amiga Test Kit %s - by Keir Fraser", version);
    centre_string(s, 44, ' ');
    print_line(&r);
    r.y++;
    s[0] = '\0';
    centre_string(s, 44, '-');
    print_line(&r);
    r.y++;
    for (i = 0; i < split; i++) {
        if ((i+split) >= ARRAY_SIZE(mainmenu_option))
            sprintf(s, "$%u %17s$", i+1, mainmenu_option[i].name);
        else
            sprintf(s, "$%u %17s$  $%u %17s$",
                    i+1, mainmenu_option[i].name,
                    i+split+1, mainmenu_option[i+split].name);
        print_line(&r);
        r.y++;
    }

    r.y += 2;
    sprintf(s, " ROM: %s ", get_kick_string());
    centre_string(s, 44, '-');
    print_line(&r);
    r.y++;
    sprintf(s, "https://github.com/keirf/Amiga-Stuff");
    print_line(&r);
    r.y++;
    sprintf(s, "build: %s %s", build_date, build_time);
    print_line(&r);

redo_hz:
    r.y = 9;
    sprintf(s, " %s - %s/%s - %uHz ",
            cpu.name, chipset_name[chipset_type],
            is_pal ? "PAL" : "NTSC", vbl_hz);
    centre_string(s, 44, '-');
    print_line(&r);
    r.y = 14;
    sprintf(s, "$H System Reset$     $E Switch to %dHz$%10s[ATK %s]",
            (vbl_hz == 50) ? 60 : 50, "", version);
    print_line(&r);
    r.y--;

    while ((i = keycode_buffer - K_F1) >= ARRAY_SIZE(mainmenu_option)) {
        if (keycode_buffer == K_HELP)
            system_reset();
        if (keycode_buffer == K_ESC) {
            *(volatile uint16_t *)0xdff1dc = (vbl_hz == 50) ? 0x00 : 0x20;
            vbl_hz = detect_vbl_hz();
            keycode_buffer = 0;
            goto redo_hz;
        }
    }

    clear_whole_screen();
    keycode_buffer = 0;
    do_exit = 0;
    alloc_p = alloc_start;

    (*mainmenu_option[i].fn)();

    /* Clean up. */
    copperlist_default();
    keycode_buffer = 0;
}

static void c_CIAA_IRQ(struct c_exception_frame *frame)
{
    uint8_t key, icr = ciaa->icr;

    if (icr & CIAICR_FLAG)
        ciaa_flag_IRQ();

    if (icr & CIAICR_SERIAL) {
        /* Received a byte from the keyboard MPU. */
        key = keyboard_IRQ();
        /* Cancel any long-running check if instructed to exit. The actual
         * cancellation occurs in level-1 interrupt INT_SOFT. */
        if (do_exit || (key == K_ESC))
            do_cancel = 1;
    }

    if (icr & CIAICR_TOD)
        ciaa_TOD_IRQ();

    if (icr & CIAICR_TIMER_A)
        ciaata_IRQ();

    if (icr & CIAICR_TIMER_B)
        ciaatb_IRQ();

    /* NB. Clear intreq.ciaa *after* reading/clearing ciaa.icr else we get a 
     * spurious extra interrupt, since intreq.ciaa latches the level of CIAA 
     * INT and hence would simply become set again immediately after we clear 
     * it. For this same reason (latches level not edge) it is *not* racey to 
     * clear intreq.ciaa second. Indeed AmigaOS does the same (checked 
     * Kickstart 3.1). */
    IRQ_RESET(INT_CIAA);
}

static void c_CIAB_IRQ(struct c_exception_frame *frame)
{
    uint8_t icr = ciab->icr;

    if (icr & CIAICR_FLAG)
        disk_index_IRQ();

    if (icr & CIAICR_TOD)
        ciab_TOD_IRQ();

    if (icr & CIAICR_TIMER_A) {
        ciabta_IRQ();
        joymouse_ciabta_IRQ();
    }

    if (icr & CIAICR_TIMER_B)
        ciabtb_IRQ();

    /* NB. Clear intreq.ciab *after* reading/clearing ciab.icr else we get a 
     * spurious extra interrupt, since intreq.ciab latches the level of CIAB 
     * INT and hence would simply become set again immediately after we clear 
     * it. For this same reason (latches level not edge) it is *not* racey to 
     * clear intreq.ciab second. Indeed AmigaOS does the same (checked 
     * Kickstart 3.1). */
    IRQ_RESET(INT_CIAB);
}

static uint16_t vblank_joydat, mouse_x, mouse_y;
static void c_VBLANK_IRQ(struct c_exception_frame *frame)
{
    uint16_t joydat, hstart, vstart, vstop;
    uint16_t cur16 = get_ciaatb();

    vblank_count++;

    stamp32 -= (uint16_t)(stamp16 - cur16);
    stamp16 = cur16;

    /* Update mouse pointer coordinates based on mouse input. */
    joydat = cust->joy0dat;
    mouse_x += (int8_t)(joydat - vblank_joydat);
    mouse_y += (int8_t)((joydat >> 8) - (vblank_joydat >> 8));
    mouse_x = min_t(int16_t, max_t(int16_t, mouse_x, 0), xres-1);
    mouse_y = min_t(int16_t, max_t(int16_t, mouse_y, 0), 2*yres-1);
    vblank_joydat = joydat;

    /* Move the mouse pointer sprite. */
    hstart = (mouse_x>>1) + diwstrt_h-1;
    vstart = (mouse_y>>1) + diwstrt_v;
    vstop = vstart + 11;
    pointer_sprite[0] = (vstart<<8)|(hstart>>1);
    pointer_sprite[1] = (vstop<<8)|((vstart>>8)<<2)|((vstop>>8)<<1)|(hstart&1);

    /* Defer menu-option handling to lowest-priority interrupt group. */
    cust->intreq = INT_SETCLR | INT_SOFT;

    IRQ_RESET(INT_VBLANK);
}

static void c_SOFT_IRQ(struct c_exception_frame *frame)
{
    static uint16_t prev_lmb;
    uint16_t lmb, i, x, y;
    struct menu_option *am, *m;

    /* Clear the spurious autovector counts. This allows a certain maximum
     * number of autovector interrupts per vertical blanking period. 
     * We maintain an aggregate total to report to the user. */
    for (i = x = 0; i < sizeof(spurious_autovector_count); i++) {
        x += spurious_autovector_count[i];
        spurious_autovector_count[i] = 0;
    }
    spurious_autovector_total += x;

    m = NULL;
    am = active_menu_option;

    /* Is mouse pointer currently within a menu-option bounding box? */
    x = mouse_x;
    y = mouse_y >> 1;
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
    if (m != am) {
        uint16_t fx, fw, fy;
        if (am != NULL) {
            fx = xstart + am->x1 * 8 - 1;
            fw = (am->x2 - am->x1) * 8 + 3;
            fy = ystart + am->y * yperline;
            clear_rect(fx, fy, fw, yperline, 1<<2); /* bpl[2] */
        }
        if (m != NULL) {
            fx = xstart + m->x1 * 8 - 1;
            fw = (m->x2 - m->x1) * 8 + 3;
            fy = ystart + m->y * yperline;
            fill_rect(fx, fy, fw, yperline, 1<<2); /* bpl[2] */
        }
        active_menu_option = m;
    }

    /* When LMB is first pressed emit a keycode if we are within a menu box. */
    lmb = !(ciaa->pra & CIAAPRA_FIR0);
    if (lmb && !prev_lmb && (m != NULL)) {
        keycode_buffer = m->c;
        if (m->c == K_CTRL)
            do_exit = 1; /* Ctrl (+ L.Alt) sets the exit flag */
        /* Cancel any long-running check if instructed to exit. */
        if (do_exit || (m->c == K_ESC))
            do_cancel = 1;
    }
    prev_lmb = lmb;

    /* Perform an asynchronous function cancellation if so instructed. */
    if (do_cancel)
        cancel_call(&test_cancellation, frame);

    IRQ_RESET(INT_SOFT);
}

static void cia_init(volatile struct amiga_cia * const cia, uint8_t icrf)
{
    /* Enable only the requested interrupts. */
    cia->icr = (uint8_t)~CIAICR_SETCLR;
    cia->icr = CIAICR_SETCLR | icrf;
 
    /* Start all CIA timers in continuous mode. */
    cia->talo = cia->tahi = cia->tblo = cia->tbhi = 0xff;
    cia->cra = cia->crb = CIACRA_LOAD | CIACRA_START;
}

void ciaa_init(void)
{
    /* CIAA ICR: We only care about keyboard. */
    cia_init(ciaa, CIAICR_SERIAL);
}

void ciab_init(void)
{
    /* CIAB ICR: We only care about FLAG line (disk index). */    
    cia_init(ciab, CIAICR_FLAG);
}

void cstart(void)
{
    uint16_t i, j;
    char *p;

    /* Clear BSS. */
    memset(_sbss, 0, _ebss-_sbss);

    /* Set keyboard serial line to input mode. */
    ciaa->cra &= ~CIACRA_SPMODE;

    ciaa_init();
    ciab_init();

    /* Enable blitter DMA. */
    cust->dmacon = DMA_SETCLR | DMA_BLTEN;

    /* Bitplanes and unpacked font allocated as directed by linker. */
    p = _end;
    for (i = 0; i < planes; i++) {
        bpl[i] = (uint8_t *)p;
        p += bplsz;
    }
    p = unpack_font(p);
    alloc_start = p;

    /* Poke bitplane addresses into the copper. */
    for (i = 0; copper[i] != 0x00e0/*bpl1pth*/; i += 2)
        continue;
    for (j = 0; j < planes; j++) {
        copper[i+j*4+1] = (uint32_t)bpl[j] >> 16;
        copper[i+j*4+3] = (uint32_t)bpl[j];
    }
    
    /* Poke sprite addresses into the copper. */
    for (i = 0; copper[i] != 0x0120/*spr0pth*/; i += 2)
        continue;
    copper[i+1] = (uint32_t)pointer_sprite >> 16;
    copper[i+3] = (uint32_t)pointer_sprite;
    for (j = 1; j < 8; j++) {
        copper[i+j*4+1] = (uint32_t)dummy_sprite >> 16;
        copper[i+j*4+3] = (uint32_t)dummy_sprite;
    }

    init_crash_handler();

    clear_whole_screen();

    clear_colors();

    memcpy(&crash_autovector[1], (void *)&m68k_vec->level1_autovector, 7*4);

    m68k_vec->level1_autovector.p = SOFT_IRQ;
    m68k_vec->level2_autovector.p = CIAA_IRQ;
    m68k_vec->level3_autovector.p = VBLANK_IRQ;
    m68k_vec->level4_autovector.p = LEVEL4_IRQ;
    m68k_vec->level5_autovector.p = LEVEL5_IRQ;
    m68k_vec->level6_autovector.p = CIAB_IRQ;
    m68k_vec->level7_autovector.p = LEVEL7_IRQ;
    cust->cop1lc.p = copper;
    cust->cop2lc.p = copper_2;

    vblank_joydat = cust->joy0dat;

    wait_bos();
    cust->dmacon = DMA_SETCLR | DMA_COPEN | DMA_DSKEN;
    cust->intena = (INT_SETCLR | INT_CIAA | INT_CIAB | INT_VBLANK | INT_SOFT);

    /* Detect our hardware environment. */
    detect_cpu_model(&cpu);
    if (cpu.model == 0)
        fixup_68000_unrecoverable_faults();
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

    /* Fat Gary "Bus Timeout Mode" (TIMEOUT): Bit 7 of DE0000. 
     *  =0: silent bus timeout (power on default)
     *  =1: #BERR on timeout (set by later Kickstarts) */
    *(volatile uint8_t *)0xde0000 = 0;
    /* Fat Gary "Bus Timeout Enable" (TOENB): Bit 7 of DE0001. 
     *  =0: timeout enabled (power on default) */
    *(volatile uint8_t *)0xde0001 = 0;

    for (;;)
        mainmenu();
}

asm (
"    .data                          \n"
"packfont: .incbin \"../base/font.raw\"\n"
"    .text                          \n"
);

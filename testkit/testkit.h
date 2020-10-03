/*
 * testkit.h
 * 
 * Amiga Tests:
 *  - Memory
 *  - Keyboard
 *  - Floppy Drive
 *  - Joystick / Mouse
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


/*******************
 * AMIGA MEMORY MAP
 */

static volatile struct m68k_vector_table * const m68k_vec =
    (struct m68k_vector_table *)0x0;
static volatile struct amiga_custom * const cust =
    (struct amiga_custom *)0xdff000;
static volatile struct amiga_cia * const ciaa =
    (struct amiga_cia *)0x0bfe001;
static volatile struct amiga_cia * const ciab =
    (struct amiga_cia *)0x0bfd000;

/*******************
 * SYSTEM CONFIGURATION
 */

#define CHIPSET_ocs 0
#define CHIPSET_ecs 1
#define CHIPSET_aga 2
#define CHIPSET_unknown 3
extern uint8_t chipset_type;
struct cpu {
    char name[31];
    uint8_t model; /* 680[x]0 */
    uint32_t pcr;  /* 68060 only */
};
extern struct cpu cpu;


/*******************
 * INTERRUPTS
 */

/* Write to INTREQ twice at end of ISR to prevent spurious re-entry on 
 * A4000 with faster processors (040/060). */
#define IRQ_RESET(x) do {                       \
    uint16_t __x = (x);                         \
    cust->intreq = __x;                         \
    cust->intreq = __x;                         \
} while (0)
/* Similarly for disabling an IRQ, write INTENA twice to be sure that an 
 * interrupt won't creep in after the IRQ_DISABLE(). */
#define IRQ_DISABLE(x) do {                     \
    uint16_t __x = (x);                         \
    cust->intena = __x;                         \
    cust->intena = __x;                         \
} while (0)
#define IRQ_ENABLE(x) do {                      \
    uint16_t __x = INT_SETCLR | (x);            \
    cust->intena = __x;                         \
} while (0)

extern volatile uint32_t spurious_autovector_total;


/*******************
 * KICKSTART ROM
 */

const char *get_kick_string(void);


/*******************
 * KEYBOARD
 */

/* Keyboard IRQ: Keyboard variables. */
extern volatile uint8_t keycode_buffer, do_exit;
extern volatile uint8_t key_pressed[128];

/* Keycodes used for menu navigation. */
enum {
    K_ESC = 0x45, K_CTRL = 0x63, K_LALT = 0x64,
    K_F1 = 0x50, K_F2, K_F3, K_F4, K_F5, K_F6, K_F7, K_F8, K_F9, K_F10,
    K_HELP = 0x5f
};


/*******************
 * DISPLAY PRINTING, DRAWING, CLEARING
 */

/* Regardless of intrinsic PAL/NTSC-ness, display may be 50 or 60Hz. */
extern uint8_t vbl_hz;

/* Counts number of VBL IRQs. Tests may modify/reset this counter. */
extern volatile unsigned int vblank_count;

/* Display size and depth. */
#define xres    640
#define yres    169
#define bplsz   (yres*xres/8)
#define planes  3

/* Top-left coordinates of the display. */
#define diwstrt_h 0x81
#define diwstrt_v 0x46

/* Bitplane data. */
extern uint8_t *bpl[planes];

/* Print lines of text. */
struct char_row {
    uint8_t x, y;
    const char *s;
};
void print_text_box(unsigned int x, unsigned int y, const char *s);
void print_line(const struct char_row *r);
void print_menu_nav_line(void);
void text_highlight(uint16_t x, uint16_t y, uint16_t nr, int fill);

/* Print a string of plain 8x8 characters straight to bitplane @b. */
void print_label(unsigned int x, unsigned int y, uint8_t b, const char *s);

/* Fill and clear rectangles. Draw hollow/outline rectangles. */
void draw_rect(
    unsigned int x, unsigned int y,
    unsigned int w, unsigned int h,
    uint8_t plane_mask, int set);
#define fill_rect(x,y,w,h,p) draw_rect(x,y,w,h,p,1)
#define clear_rect(x,y,w,h,p) draw_rect(x,y,w,h,p,0)
void hollow_rect(
    unsigned int x, unsigned int y,
    unsigned int w, unsigned int h,
    uint8_t plane_mask);

/* Clear all or part of the screen. */
void clear_whole_screen(void);
void clear_text_rows(uint16_t y_start, uint16_t y_nr);

/* Wait for end of bitplane DMA. */
void wait_bos(void);

/* Copperlist set/reset */
void copperlist_set(const void *list);
void copperlist_default(void);


/*******************
 * TIME
 */

/* Detected CPU frequency. CIA timers run at cpu_hz/10. */
extern unsigned int cpu_hz;

/* PAL/NTSC CPU and CIA timings. */
extern uint8_t is_pal;

/* Reinitialise CIAA/CIAB if they are messed with. */
void ciaa_init(void);
void ciab_init(void);

/* Loop to get consistent current CIA timer value. */
#define get_ciatime(_cia, _tim) ({              \
    uint8_t __hi, __lo;                         \
    do {                                        \
        __hi = (_cia)->_tim##hi;                \
        __lo = (_cia)->_tim##lo;                \
    } while (__hi != (_cia)->_tim##hi);         \
    ((uint16_t)__hi << 8) | __lo; })

/* Get current value of CIAATB. */
uint16_t get_ciaatb(void);

/* Get number of CIA ticks since boot. */
uint32_t get_time(void);

/* Tick conversion functions. */
void ticktostr(uint32_t ticks, char *s);
unsigned int ms_to_ticks(uint16_t ms);

/* Wait for a given number of milliseconds. */
void delay_ms(unsigned int ms);


/*******************
 * MEMORY ALLOCATION
 */

/* Allocate a region of chip memory. */
void *allocmem(unsigned int sz);

/* Start/end a heap arena. */
void *start_allocheap_arena(void);
void end_allocheap_arena(void *p);

/* List of memory regions detected by Exec. */
struct mem_region {
    uint16_t attr;
    uint32_t lower, upper;
};
extern struct mem_region mem_region[];
extern uint16_t nr_mem_regions;


/*******************
 * CANCELLATIONS
 */

void call_cancellable_test(int (*fn)(void *), void *arg);


/*******************
 * DEBUG
 */

void init_crash_handler(void);
void fixup_68000_unrecoverable_faults(void);

extern char build_date[];
extern char build_time[];
extern char version[];

#define assert(_p) do { if (!(_p)) __assert_fail(); } while (0)
#define __assert_fail() asm volatile (          \
    "illegal        \n"                         \
    "move.w %0,%%d0 \n"                         \
    "move.w %1,%%d0 \n"                         \
    : : "i" (__FILE__), "i" (__LINE__) )

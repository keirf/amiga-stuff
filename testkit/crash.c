/*
 * crash.c
 * 
 * Crash handler for unexpected exceptions.
 * 
 * Written & released by Keir Fraser <keir.xen@gmail.com>
 * 
 * This is free and unencumbered software released into the public domain.
 * See the file COPYING for more details, or visit <http://unlicense.org>.
 */

#include "testkit.h"

const static char * const _exc_str[] = {
    [ 2] = "Bus Error",
    [ 3] = "Address Error",
    [ 4] = "Illegal Instruction",
    [ 5] = "Zero Divide",
    [ 6] = "CHK/CHK2",
    [ 7] = "TRAPcc",
    [ 8] = "Privilege Violation",
    [ 9] = "Trace",
    [10] = "Line A",
    [11] = "Line F",
    [13] = "Coprocessor Protocol Violation",
    [14] = "Format Error",
    [15] = "Uninitialised Interrupt",
    [24] = "Spurious Interrupt",
    [25] = "Level 1 IRQ",
    [26] = "Level 2 IRQ",
    [27] = "Level 3 IRQ",
    [28] = "Level 4 IRQ",
    [29] = "Level 5 IRQ",
    [30] = "Level 6 IRQ",
    [31] = "NMI",
};

static const char * const exc_str(unsigned int exc_nr)
{
    if ((exc_nr >= ARRAY_SIZE(_exc_str)) || (_exc_str[exc_nr] == NULL))
        return "Unknown";
    return _exc_str[exc_nr];
}

static uint16_t copper[] = {
    0x0100, 0x9200, /* bplcon0: 1 bitplane, hires */
    0x0180, 0x0000, /* col00, black */
    0x0182, 0x0ccc, /* col01, white */
    0xffff, 0xfffe,
};

void double_crash(void);
asm (
"double_crash:                      \n"
"    move.w #0x700,0xdff180         \n"
"    jra    double_crash            \n"
);

/* Common entry point for unexpected exceptions. */
void common(void);
asm (
"common:                            \n"
"    ori.w   #0x700,%sr             \n"
"    movem.l %d0-%d7/%a0-%a6,-(%sp) \n"
"    move.l  %usp,%a0               \n"
"    move.l  %a0,-(%sp)             \n"
"    move.l  %sp,%a0                \n"
"    move.l  %a0,-(%sp)             \n"
"    jbsr    crash                  \n"
);

/* Special unrecoverable-fault shim handlers for 68000 only. See below. */
void address_fault_68000(void);
asm (
"address_fault_68000:               \n"
"    addq.l  #8,%sp                 \n" /* discard top of stack */
"    dc.w    0x4ef9,0,0             \n" /* jump to main handler */
);
void bus_fault_68000(void);
asm (
"bus_fault_68000:                   \n"
"    addq.l  #8,%sp                 \n" /* discard top of stack */
"    dc.w    0x4ef9,0,0             \n" /* jump to main handler */
);

/* Per-vector entry points, 2 words per vector: BSR.w <target>.
 * The first 3 words are a common JMP target: JMP <common>.l */
static uint16_t vectors[2*256];

/* Crash exception frame. */
struct frame {
    uint32_t usp;
    uint32_t d[8], a[7];
    uint32_t vector_pc;
    uint16_t sr;
    uint32_t pc;
};

/* Fix up one 68000 unrecoverable fault handler. */
static void fixup_68000(void *new_fn, volatile uint32_t *vec)
{
    uint32_t old_fn = *vec;
    /* Search for the JMP instruction in the shim handler, and patch it. */
    uint16_t *p = new_fn;
    while (*p != 0x4ef9) p++;
    *(uint32_t *)(p+1) = old_fn;
    /* Install our shim handler. */
    *vec = (uint32_t)new_fn;
}

/* 68000 (only) has weird stack formats for unrecoverable faults. These cause 
 * us to print garbage in our crash handler. Fix this by inserting shim 
 * handlers which discard the extra stuff at the top of the stack. */
void fixup_68000_unrecoverable_faults(void)
{
    fixup_68000(address_fault_68000, &m68k_vec->address_error.x);
    fixup_68000(bus_fault_68000, &m68k_vec->bus_error.x);
}

void init_crash_handler(void)
{
    unsigned int i;
    uint16_t *v, **pv;

    /* Build common absolute JMP instruction shared by all vectors. */
    v = vectors;
    *v++ = 0x4ef9; /* JMP (common).L */
    *(uint32_t *)v = (uint32_t)common;
    v += 3;

    /* Poke all exception vectors, skipping reset PC/SP. The single unique 
     * per-vector instruction is a BSR.W which is an efficient way to stack
     * a value allowing us to identify the vector number. */
    pv = (uint16_t **)NULL + 2;
    for (i = 2; i < 256; i++) {
        *pv++ = v; /* Set the exception vector */
        *v++ = 0x6100; /* BSR.W <jmp> */
        *v = (uint16_t)((uint32_t)vectors - (uint32_t)v);
        v++;
    }
}

/* Print a string at text location (x,y) in the bitplane. */
static void print(uint16_t x, uint16_t y, const char *s)
{
    x *= 8;
    y *= 10;
    print_label(x, y, 0, s);
}

/* C entry point for an unexpected exception. */
static void crash(struct frame *f) attribute_used;
static void crash(struct frame *f)
{
    uint32_t sp, ssp;
    uint16_t *_sp, exc_nr;
    unsigned int i, x, y;
    char s[80], src[40];
    void (**pv)(void);

    /* Rewrite all exception vectors to avoid reentering this function should
     * it crash. */
    pv = (void (**)(void))NULL + 2;
    for (i = 2; i < 256; i++)
        *pv++ = double_crash;

    /* Stop all old activity and install a simple copper with a single clear 
     * bitplane. Wait for vblank before disabling sprites to avoid garbage. */
    cust->intena = 0x7fff;
    cust->intreq = INT_VBLANK;
    while (!(cust->intreqr & INT_VBLANK))
        continue;
    cust->dmacon = 0x7fff;
    cust->cop2lc.p = copper;
    memset(bpl[0], 0, bplsz);
    cust->dmacon = DMA_SETCLR | DMA_DMAEN | DMA_COPEN | DMA_BPLEN;

    /* Print a banner. */
    x = 4;
    y = 0;
    sprintf(s, "Amiga Test Kit %s (build: %s %s)",
            version, build_date, build_time);
    print(x, y, s);

    /* Calculate stack pointers. */
    ssp = (uint32_t)(f+1);
    sp = (f->sr & 0x2000) ? ssp : f->usp;

    /* Calculate exception vector number from vector PC. */
    exc_nr = ((f->vector_pc - (uint32_t)vectors) >> 2) - 1;

    /* An assertion failure tells us which line of source code failed. */
    if (/* illegal opcode vector */
        (exc_nr == 4)
        /* sensible program counter */
        && (f->pc > (uint32_t)_start)
        && (f->pc < (uint32_t)_end)
        && !(f->pc & 1)
        /* illegal ; move.w #<file>,%d0 ; move.w #<line>,%d0 */
        && (*(uint32_t *)f->pc == 0x4afc303c)) {
        sprintf(src, " (%s:%u)",
                (char *)(uint32_t)((uint16_t *)f->pc)[2],
                ((uint16_t *)f->pc)[4]);
    } else {
        /* No source-code information to print. */
        src[0] = '\0';
    }

    /* Print the exception stack frame. */
    y += 2;
    sprintf(s, "Exception #%02x (%u: %s)",
            exc_nr, exc_nr, exc_str(exc_nr));
    print(x, y, s);
    x += 2;
    y++;
    sprintf(s, "PC: %08x%s", f->pc, src);
    print(x, y, s);
    y++;
    sprintf(s, "d0: %08x  d1: %08x  d2: %08x  d3: %08x",
            f->d[0], f->d[1], f->d[2], f->d[3]);
    print(x, y, s);
    y++;
    sprintf(s, "d4: %08x  d5: %08x  d6: %08x  d7: %08x",
            f->d[4], f->d[5], f->d[6], f->d[7]);
    print(x, y, s);
    y++;
    sprintf(s, "a0: %08x  a1: %08x  a2: %08x  a3: %08x",
            f->a[0], f->a[1], f->a[2], f->a[3]);
    print(x, y, s);
    y++;
    sprintf(s, "a4: %08x  a5: %08x  a6: %08x  a7: %08x",
            f->a[4], f->a[5], f->a[6], sp);
    print(x, y, s);
    y++;
    sprintf(s, "sr: %04x  ssp: %08x  usp: %08x", f->sr, ssp, f->usp);
    print(x, y, s);

    /* Print the crashed stack. */
    x -= 2;
    y += 2;
    sprintf(s, "Stack Trace:");
    print(x, y, s);
    x += 2;
    _sp = (uint16_t *)(sp&~1);
    for (i = 0; i < 6; i++) {
        y++;
        sprintf(s, "%08p:  %04x %04x %04x %04x  %04x %04x %04x %04x", _sp,
                _sp[0], _sp[1], _sp[2], _sp[3],
                _sp[4], _sp[5], _sp[6], _sp[7]);
        print(x, y, s);
        _sp += 8;
    }

    /* We're done, forever. */
    for (;;) ;
}

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

#include "systest.h"

static uint16_t copper[] = {
    0x0100, 0x9200, /* bplcon0: 1 bitplane, hires */
    0x0180, 0x0000, /* col00, black */
    0x0182, 0x0ccc, /* col01, white */
    0xffff, 0xfffe,
};

void exceptions(void);

/* Crash exception frame. */
struct frame {
    uint32_t usp;
    uint32_t d[8], a[7];
    uint16_t exc_nr;
    uint16_t sr;
    uint32_t pc;
};

void init_crash_handler(void)
{
    unsigned int i;
    char *e, **pe;

    /* Poke all exception vectors, skipping reset PC/SP. */
    pe = (char **)NULL + 2;
    e = (char *)exceptions + 2*8;
    for (i = 2; i < 256; i++) {
        *pe++ = e;
        e += 8;
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
    uint16_t *_sp;
    unsigned int i, x, y;
    char s[80];

    /* Stop all old activity and install a simple copper with a single clear 
     * bitplane. */
    cust->intena = 0x7fff;
    cust->dmacon = 0x7fff;
    cust->cop2lc.p = copper;
    memset(bpl[0], 0, bplsz);
    cust->dmacon = DMA_SETCLR | DMA_DMAEN | DMA_COPEN | DMA_BPLEN;

    /* Calculate stack pointers. */
    ssp = (uint32_t)(f+1);
    sp = (f->sr & 0x2000) ? ssp : f->usp;

    /* Print the exception stack frame. */
    x = 4;
    y = 2;
    sprintf(s, "Exception #%02x at PC %08x:", f->exc_nr, f->pc);
    print(x, y, s);
    x += 2;
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

/* A single unique exception entry point. Pushes its vector onto the stack. */
#define EXC(nr)                                 \
"    move.w  #0x"#nr",-(%sp)        \n"         \
"    bra.w   common                 \n"         \

/* 16 unique exception entry points. */
#define E(nr)                                   \
EXC(nr##0) EXC(nr##1) EXC(nr##2) EXC(nr##3)     \
EXC(nr##4) EXC(nr##5) EXC(nr##6) EXC(nr##7)     \
EXC(nr##8) EXC(nr##9) EXC(nr##A) EXC(nr##B)     \
EXC(nr##C) EXC(nr##D) EXC(nr##E) EXC(nr##F)

/* Build the 256 unique entry points and their common branch target. */
asm (
"common:                            \n"
"    movem.l %d0-%d7/%a0-%a6,-(%sp) \n"
"    ori.w   #0x700,%sr             \n"
"    move.l  %usp,%a0               \n"
"    move.l  %a0,-(%sp)             \n"
"    move.l  %sp,%a0                \n"
"    move.l  %a0,-(%sp)             \n"
"    jbsr    crash                  \n"
"exceptions:                        \n"
E(0) E(1) E(2) E(3) E(4) E(5) E(6) E(7)
E(8) E(9) E(A) E(B) E(C) E(D) E(E) E(F)
);

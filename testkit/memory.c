/*
 * memory.c
 * 
 * Detect and test available memory.
 * 
 * Written & released by Keir Fraser <keir.xen@gmail.com>
 * 
 * This is free and unencumbered software released into the public domain.
 * See the file COPYING for more details, or visit <http://unlicense.org>.
 */

#include "testkit.h"

/* sprintf helpers */
#define mb_whole(x) ((x) >> 20)
#define mb_frac(x) (mul32((x)>>4, 100) >> 16)

static int delay_sec;

/* Strict memory regions should not be rounded because they are close to
 * critical memory-mapped resources (CIA, RTC, custom registers). Issue #39. */
const static struct strict_region {
    uint32_t val, mask;
} strict_region[] = {
    { 0x00bf0000, 0xffff0000 }, /* 0x00BF0000 - 0x00BFFFFF */
    { 0x00dc0000, 0xfffc0000 }  /* 0x00DC0000 - 0x00DFFFFF */
};

static bool_t in_strict_region(uint32_t x)
{
    int i;
    for (i = 0; i < ARRAY_SIZE(strict_region); i++) {
        const struct strict_region *sr = &strict_region[i];
        if ((x & sr->mask) == sr->val)
            return TRUE;
    }
    return FALSE;
}

static uint32_t lb(uint32_t _lb)
{
    uint32_t lb;
    if (in_strict_region(_lb))
        return _lb;
    /* Round down to 64kB boundary. */
    lb = _lb & ~0xffff;
    if (_lb > 0x10000000) {
        /* ZorroIII: Round to nearest 1MB (issue #6). */
        lb = (_lb + 0x7ffff) & ~0xfffff;
    }
    return lb;
}

static uint32_t ub(uint32_t _ub)
{
    uint32_t ub;
    if (in_strict_region(_ub))
        return _ub;
    /* Round up to 64kB boundary. */
    ub = (_ub + 0xffff) & ~0xffff;
    if (_ub > 0x10000000) {
        /* ZorroIII: Round to nearest 1MB (issue #6). */
        ub = (_ub + 0x7ffff) & ~0xfffff;
    }
    return ub;
}

/* 32-bit sequence of length 2^32-1 (all 32-bit values except zero). */
static inline __attribute__((always_inline)) uint32_t lfsr(uint32_t x)
{
    asm volatile (
        "lsr.l #1,%0; bcc.s 1f; eor.l #0x80000062,%0; 1:"
        : "=d" (x) : "0" (x));
    return x;
}

/* Fill every 32-bit word from @start to @end. */
static void fill_32(
    uint32_t fill, volatile uint32_t *start, volatile uint32_t *end)
{
    uint32_t x, y;
    asm volatile (
        "1: move.l %2,(%3)+; move.l %2,(%3)+; "
        "move.l %2,(%3)+; move.l %2,(%3)+; dbf %4,1b"
        : "=a" (x), "=d" (y)
        : "d" (fill), "0" (start), "1" ((end-start)/4-1));
}

/* Fill every other 16-bit word from @start to @end. */
static void fill_alt_16(
    uint16_t fill, uint16_t shift,
    volatile uint32_t *start, volatile uint32_t *end)
{
    uint32_t x = (uint32_t)start + shift, y;
    asm volatile (
        "1: move.w %2,(%3); move.w %2,4(%3); move.w %2,8(%3); "
        "move.w %2,12(%3); lea 16(%3),%3; dbf %4,1b"
        : "=a" (x), "=d" (y)
        : "d" (fill), "0" (x), "1" ((end-start)/4-1));
}

static uint16_t check_pattern(
    uint32_t check, volatile uint32_t *start, volatile uint32_t *end)
{
    uint32_t x, y, z, val;
    asm volatile (
        "1: move.l (%5)+,%2; eor.l %4,%2; or.l %2,%3; "
        "move.l (%5)+,%2; eor.l %4,%2; or.l %2,%3; "
        "move.l (%5)+,%2; eor.l %4,%2; or.l %2,%3; "
        "move.l (%5)+,%2; eor.l %4,%2; or.l %2,%3; "
        "dbf %6,1b; move.w %3,%2; swap %3; or.w %2,%3"
        : "=a" (x), "=d" (y), "=&d" (z), "=d" (val)
        : "d" (check), "0" (start), "1" ((end-start)/4-1), "3" (0));
    return (uint16_t)val;
}

struct test_memory_args {
    /* Testing is split into rounds and subrounds. Each subround covers all 
     * memory before proceeding to the next. Even subrounds fill memory; 
     * odd subrounds check the preceding fill. */
    unsigned int round, subround;
    /* Memory range being tested within the current subround. */
    uint32_t start, end;
    /* LFSR seed for pseudo-random fill. This gets saved at the start of fill 
     * so that the same sequence can be reproduced and checked. */
    uint32_t seed, _seed;
    /* For printing status updates. */
    struct char_row r;
};

static int test_memory_range(void *_args)
{
    struct test_memory_args *args = _args;
    struct char_row *r = &args->r;
    volatile uint32_t *p;
    volatile uint32_t *start = (volatile uint32_t *)((args->start+15) & ~15);
    volatile uint32_t *end = (volatile uint32_t *)(args->end & ~15);
    char *s = (char *)r->s;
    uint32_t a = 0, x;

    if (start >= end)
        return 0;

    r->y++;
    sprintf(s, "%sing 0x%p-0x%p", !(args->subround & 1) ? "Fill" : "Check",
            (char *)start, (char *)end-1);
    wait_bos();
    print_line(r);

    switch (args->subround) {

    case 0:
        /* Random numbers. */
        x = args->seed;
        for (p = start; p != end;) {
            *p++ = x = lfsr(x);
            *p++ = x = lfsr(x);
            *p++ = x = lfsr(x);
            *p++ = x = lfsr(x);
        }
        args->seed = x;
        break;
    case 1:
        x = args->seed;
        for (p = start; p != end;) {
            a |= *p++ ^ (x = lfsr(x));
            a |= *p++ ^ (x = lfsr(x));
            a |= *p++ ^ (x = lfsr(x));
            a |= *p++ ^ (x = lfsr(x));
        }
        args->seed = x;
        break;

    case 2:
        /* Start with all 0s. Write 1s to even words. */
        fill_32(0, start, end);
        fill_alt_16(~0, 0, start, end);
        break;
    case 3:
        a |= check_pattern(0xffff0000, start, end);
        break;

    case 4:
        /* Start with all 0s. Write 1s to odd words. */
        fill_32(0, start, end);
        fill_alt_16(~0, 2, start, end);
        break;
    case 5:
        a |= check_pattern(0x0000ffff, start, end);
        break;

    case 6:
        /* Start with all 1s. Write 0s to even words. */
        fill_32(~0, start, end);
        fill_alt_16(0, 0, start, end);
        break;
    case 7:
        a |= check_pattern(0x0000ffff, start, end);
        break;

    case 8:
        /* Start with all 1s. Write 0s to odd words. */
        fill_32(~0, start, end);
        fill_alt_16(0, 2, start, end);
        break;
    case 9:
        a |= check_pattern(0xffff0000, start, end);
        break;
    }

    /* Errors found: then print diagnostic and wait to exit. */
    if (a != 0) {
        r->y += 2;
        sprintf(s, "ERRORS:     D31..D24  D23..D16  D15...D8  D7....D0");
        print_line(r);
        r->y++;
        sprintf(s, "(X=error)   76543210  76543210  76543210  76543210");
        print_line(r);
        r->y++;
        sprintf(s, "32-bit bus  %c%c%c%c%c%c%c%c  %c%c%c%c%c%c%c%c  "
                "%c%c%c%c%c%c%c%c  %c%c%c%c%c%c%c%c",
                (a & (1u<<31)) ? 'X' : '-', (a & (1u<<30)) ? 'X' : '-',
                (a & (1u<<29)) ? 'X' : '-', (a & (1u<<28)) ? 'X' : '-',
                (a & (1u<<27)) ? 'X' : '-', (a & (1u<<26)) ? 'X' : '-',
                (a & (1u<<25)) ? 'X' : '-', (a & (1u<<24)) ? 'X' : '-',
                (a & (1u<<23)) ? 'X' : '-', (a & (1u<<22)) ? 'X' : '-',
                (a & (1u<<21)) ? 'X' : '-', (a & (1u<<20)) ? 'X' : '-',
                (a & (1u<<19)) ? 'X' : '-', (a & (1u<<18)) ? 'X' : '-',
                (a & (1u<<17)) ? 'X' : '-', (a & (1u<<16)) ? 'X' : '-',
                (a & (1u<<15)) ? 'X' : '-', (a & (1u<<14)) ? 'X' : '-',
                (a & (1u<<13)) ? 'X' : '-', (a & (1u<<12)) ? 'X' : '-',
                (a & (1u<<11)) ? 'X' : '-', (a & (1u<<10)) ? 'X' : '-',
                (a & (1u<< 9)) ? 'X' : '-', (a & (1u<< 8)) ? 'X' : '-',
                (a & (1u<< 7)) ? 'X' : '-', (a & (1u<< 6)) ? 'X' : '-',
                (a & (1u<< 5)) ? 'X' : '-', (a & (1u<< 4)) ? 'X' : '-',
                (a & (1u<< 3)) ? 'X' : '-', (a & (1u<< 2)) ? 'X' : '-',
                (a & (1u<< 1)) ? 'X' : '-', (a & (1u<< 0)) ? 'X' : '-');
        print_line(r);
        r->y++;
        a |= a >> 16; /* fold 16-bit halves together */
        sprintf(s, "16-bit bus                      "
                "%c%c%c%c%c%c%c%c  %c%c%c%c%c%c%c%c",
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

static void print_memory_test_type(struct test_memory_args *args)
{
    struct char_row *r = &args->r;
    char *s = (char *)r->s;

    switch (args->subround) {
    case 0:
        sprintf(s, "Round %u.%u: Random Fill",
                args->round+1, 1);
        /* Save the seed we use for this fill. */
        args->_seed = args->seed;
        break;
    case 1:
        /* Restore seed we used for fill. */
        args->seed = args->_seed;
        break;
    case 2: case 4: case 6: case 8:
        sprintf(s, "Round %u.%u: Checkboard #%u",
                args->round+1, args->subround/2+1, args->subround/2);
        break;
    }

    if (!(args->subround & 1)) {
        wait_bos();
        print_line(r);
    } else if (delay_sec) {
        int i, j;
        r->y++;
        for (i = 0; i < delay_sec; i++) {
            sprintf(s, "Delay %u...", delay_sec-i);
            wait_bos();
            print_line(r);
            for (j = 0; j < 10; j++) {
                delay_ms(100);
                if (do_exit || (keycode_buffer == K_ESC))
                    return;
            }
        }
    }
}

static void init_memory_test(struct test_memory_args *args)
{
    args->round = args->subround = 0;
    args->seed = 0x12341234;
    print_memory_test_type(args);
}

static void memory_test_next_subround(struct test_memory_args *args)
{
    if (args->subround++ == 9) {
        args->round++;
        args->subround = 0;
    }
    print_memory_test_type(args);
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
        while (!do_exit && (keycode_buffer != K_ESC))
            continue;
        goto out;
    }

    tm_args.r = *r;
    init_memory_test(&tm_args);
    while (!do_exit && (keycode_buffer != K_ESC)) {

        if (nr == 0) {
            /* 1st 256kB: Test unused heap. 
             * 2nd 256kB: Test all. */
            tm_args.start = (uint32_t)allocmem(0);
            tm_args.end = (uint32_t)HEAP_END;
            tm_args.r = *r;
            call_cancellable_test(test_memory_range, &tm_args);
            if (do_exit)
                break;
        }

        tm_args.start = (nr == 0) ? 1 << 18 : nr << 19;
        tm_args.end = (nr + 1) << 19;

        tm_args.r = *r;
        call_cancellable_test(test_memory_range, &tm_args);

        /* Next memory range, or next round if all ranges done. */
        do {
            if (++nr == 32) {
                nr = 0;
                tm_args.r = *r;
                memory_test_next_subround(&tm_args);
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
        if (aliased_slots & (1u << i))
            continue;
        p = (volatile uint16_t *)0 + (i << 18);
        p[0x9a/2] = 0x7fff; /* clear all bits in INTENA */
        j = cust->intenar;
        a = p[0x1c/2];
        p[0x9a/2] = 0xbfff; /* set all bits in INTENA except master enable */
        b = p[0x1c/2];
        if (a != b) {
            /* Slow memory starts at C00000 always. If we see custom-register 
             * aliasing then we're done. */
            for (; i < 27; i++)
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
        p = (volatile uint16_t *)&s[0] + (i << 18);
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

    sprintf(s, "** %u.%02u MB Total Memory Detected **",
            mb_whole(tot_chunks<<19), mb_frac(tot_chunks<<19));
    print_line(&r);
    r.y++;
    sprintf(s, "(Chip %u.%02u MB -- Fast %u.%02u MB -- Slow %u.%02u MB)",
            mb_whole(chip_chunks<<19), mb_frac(chip_chunks<<19),
            mb_whole(fast_chunks<<19), mb_frac(fast_chunks<<19),
            mb_whole(slow_chunks<<19), mb_frac(slow_chunks<<19));
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

    while (!do_exit) {
        sprintf(s, "$1 Test All Memory$");
        print_line(&r);
        r.y++;
        if (dodgy_slow_ram) {
            sprintf(s, "$2 Force Test 0.5MB Slow (Trapdoor) RAM$");
            print_line(&r);
        }
        r.y--;

        for (;;) {
            /* Grab a key */
            while (!do_exit && !(key = keycode_buffer))
                continue;
            keycode_buffer = 0;
            /* Handle exit conditions */
            if (do_exit || (key == K_ESC))
                goto out;
            /* Check for keys F1-F2 only */
            if ((key == K_F1) || (dodgy_slow_ram && (key == K_F2)))
                break;
        }

        clear_text_rows(r.y, 8);
        _r = r;
        test_memory_slots((key == K_F1) ? ram_slots : 1u << 24, &_r);
        clear_text_rows(r.y, 8);
    }

out:
    clear_whole_screen();
}

static uint32_t edit_address(
    uint32_t a, uint16_t x, uint16_t y, struct char_row *r)
{
    uint32_t _a = a;
    uint16_t pos, i;
    uint8_t key;
    char *s = (char *)r->s;
    const static uint8_t keys[] = { /* main keyboard */
        /* '0'-'9' */
        0x0a, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08, 0x09,
        /* 'A'-'F' */
        0x20, 0x35, 0x33, 0x22, 0x12, 0x23
    };
    const static uint8_t pad_keys[] = { /* numeric keypad */
        0x0f, 0x1d, 0x1e, 0x1f, 0x2d, 0x2e, 0x2f, 0x3d, 0x3e, 0x3f
    };

    sprintf(s, "0-9,A-F: Edit hex numeral");
    print_line(r);
    r->y++;
    sprintf(s, "L,R Arrow: Move cursor");
    print_line(r);
    r->y++;
    sprintf(s, "ESC: Cancel  RET: Confirm");
    print_line(r);

    pos = 0;

    for (;;) {

        /* Print current value. */
        sprintf(s, "%08x", _a);
        wait_bos();
        print_text_box(x, y, s);

        /* Update highlight. */
        text_highlight(x, y, 8, 0);
        text_highlight(x+pos, y, 1, 1);

        while (!(key = keycode_buffer) && !do_exit)
            continue;
        keycode_buffer = 0;

        if (do_exit)
            break;

        if (key == 0x4f) { /* left arrow */
            /* Cursor left. */
            if (pos > 0)
                pos--;
        } else if (key == 0x4e) { /* right arrow */
            /* Cursor right. */
            if (pos < 7)
                pos++;
        } else if (key == 0x44) { /* return */
            /* Exit with new value. */
            break;
        } else if (key == K_ESC) {
            /* Exit with original value. */
            _a = a;
            break;
        } else {
            /* Modify a hex numeral. */
            for (i = 0; i < ARRAY_SIZE(keys); i++) {
                if ((key == keys[i]) || ((i < ARRAY_SIZE(pad_keys))
                                         && (key == pad_keys[i]))) {
                    _a &= ~(0xf << ((7-pos)*4));
                    _a |= i << ((7-pos)*4);
                    if (pos < 7)
                        pos++;
                    break;
                }
            }
        }
    }

    text_highlight(x, y, 8, 0);
    return _a;
}

static void test_one_region(struct char_row *r, const struct mem_region *mr)
{
    struct test_memory_args tm_args;
    char *s = (char *)r->s;
    uint32_t a, b;
    uint8_t key;

    /* Calculate inclusive range [a,b] with limits expanded to 64kB alignment 
     * (Kickstart sometimes steals RAM from the limits). */
    a = lb(mr->lower);
    b = ub(mr->upper) - 1;

restart:
    keycode_buffer = 0;
    clear_whole_screen();
    print_menu_nav_line();

    r->x = 4;
    r->y = 0;
    sprintf(s, "-- Single-Region Memory Test --");
    print_line(r);

    do {
        r->x = 2;
        r->y = 2;
        sprintf(s, "%08x - %08x  %3u.%02u MB",
                a, b, mb_whole(b-a+1), mb_frac(b-a+1));
        print_line(r);

        r->x = 4;
        r->y = 4;
        sprintf(s, "$1 Test Memory Range$");
        print_line(r);
        r->y++;
        sprintf(s, "$2 Edit Start Address$");
        print_line(r);
        r->y++;
        sprintf(s, "$3 Edit End Address$");
        print_line(r);
        r->y = 4;

        do {
            while (!(key = keycode_buffer) && !do_exit)
                continue;
            keycode_buffer = 0;
            if (do_exit || (key == K_ESC))
                goto out;
        } while ((key < K_F1) || (key > K_F3));

        if (key == K_F2) {
            a = edit_address(a, 2, 2, r);
        } else if (key == K_F3) {
            b = edit_address(b, 13, 2, r);
        }

    } while (key != K_F1);

    r->y = 4;
    clear_text_rows(4, 3);

    tm_args.r = *r;
    init_memory_test(&tm_args);

    while (!do_exit && (keycode_buffer != K_ESC)) {

        /* Bottom 256kB: Test unused heap. */
        tm_args.start = max_t(uint32_t, (uint32_t)allocmem(0), a);
        tm_args.end = min_t(uint32_t, (uint32_t)HEAP_END-1, b) + 1;
        tm_args.r = *r;
        if (tm_args.start < tm_args.end)
            call_cancellable_test(test_memory_range, &tm_args);

        tm_args.start = max_t(uint32_t, 0x40000, a);
        while (!do_exit && (keycode_buffer != K_ESC)
               && tm_args.start && (tm_args.start < b)) {
            /* Calculate inclusive end limit for this chunk. Chunk size is
             * 512kB or remainder of region, whichever is smaller. */
            tm_args.end = ((tm_args.start + 0x80000) & ~0x7ffff) - 1;
            tm_args.end = min_t(uint32_t, tm_args.end, b);
            /* test_memory_range() expects the end bound to be +1. */
            tm_args.end++;
            tm_args.r = *r;
            call_cancellable_test(test_memory_range, &tm_args);
            tm_args.start = tm_args.end;
        }

        tm_args.r = *r;
        memory_test_next_subround(&tm_args);
    }

    if (!do_exit)
        goto restart;

out:
    keycode_buffer = 0;
}

static void kickstart_memory_list(void)
{
    char s[80];
    struct char_row r = { .s = s };
    unsigned int i, base = 0;
    uint32_t a, b;
    uint8_t key;

restart:
    clear_whole_screen();
    print_menu_nav_line();

    r.x = 4;
    r.y = 0;
    sprintf(s, "-- Kickstart Memory List --");
    print_line(&r);

    r.x = 0;
    r.y = 2;
    sprintf(s, "    NR  LOWER    - UPPER     TYPE   SIZE");
    print_line(&r);

    for (;;) {
    print_page:
        clear_text_rows(3, 10);
        r.x = 0;
        r.y = 3;
        for (i = base; (i < nr_mem_regions) && (i < base+8); i++) {
            a = lb(mem_region[i].lower);
            b = ub(mem_region[i].upper);
            sprintf(s, "$%u %2u  %08x - %08x  %s  %3u.%02u MB$",
                    i-base+1, i, a, b-1,
                    mem_region[i].attr & 2 ? "Chip" :
                    (a >= 0x00c00000) && (a < 0x00d00000) ? "Slow" : "Fast",
                    mb_whole(b-a), mb_frac(b-a));
            print_line(&r);
            r.y++;
        }

        r.x = 4;
        r.y = 12;
        sprintf(s, "Page %u/%u  $9 Prev$  $0 Next$",
                (base+8)>>3, (nr_mem_regions+7)>>3);
        print_line(&r);

        for (;;) {
            while (!(key = keycode_buffer) && !do_exit)
                continue;
            keycode_buffer = 0;
            
            if (do_exit || (key == K_ESC))
                goto out;

            switch (key) {
            case K_F1 ... K_F8:
                a = base + key - K_F1;
                if (a < nr_mem_regions) {
                    test_one_region(&r, &mem_region[a]);
                    goto restart;
                }
                break;
            case K_F9:
                if (base != 0) {
                    base -= 8;
                    goto print_page;
                }
                break;
            case K_F10:
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
    unsigned int i;
    uint32_t a, b;

    tm_args.r = *r;
    init_memory_test(&tm_args);

    while (!do_exit && (keycode_buffer != K_ESC)) {

        /* Bottom 256kB: Test unused heap. */
        tm_args.start = (uint32_t)allocmem(0);
        tm_args.end = (uint32_t)HEAP_END;
        tm_args.r = *r;
        call_cancellable_test(test_memory_range, &tm_args);

        for (i = 0; !do_exit && (i < nr_mem_regions); i++) {
            /* Calculate inclusive range [a,b] with limits expanded to 64kB
             * alignment (Kickstart sometimes steals RAM from the limits). */
            a = max_t(uint32_t, 0x40000, lb(mem_region[i].lower));
            b = ub(mem_region[i].upper) - 1;
            tm_args.start = a;
            while (!do_exit && (keycode_buffer != K_ESC)
                   && tm_args.start && (tm_args.start < b)) {
                /* Calculate inclusive end limit for this chunk. Chunk size
                 * is 512kB or remainder of region, whichever is smaller. */
                tm_args.end = ((tm_args.start + 0x80000) & ~0x7ffff) - 1;
                tm_args.end = min_t(uint32_t, tm_args.end, b);
                /* test_memory_range() expects the end bound to be +1. */
                tm_args.end++;
                tm_args.r = *r;
                call_cancellable_test(test_memory_range, &tm_args);
                tm_args.start = tm_args.end;
            }
        }

        tm_args.r = *r;
        memory_test_next_subround(&tm_args);
    }

    keycode_buffer = 0;
}

void memcheck(void)
{
    char s[80];
    struct char_row r = { .s = s };
    uint32_t a, b, chip, fast, slow, tot;
    uint8_t key;
    unsigned int i;

    while (!do_exit) {
        print_menu_nav_line();

        r.x = 4;
        r.y = 0;
        sprintf(s, "-- Kickstart Memory Scan --");
        print_line(&r);
        r.x = 0;
        r.y += 2;

        chip = fast = slow = 0;
        for (i = 0; i < nr_mem_regions; i++) {
            a = lb(mem_region[i].lower);
            b = ub(mem_region[i].upper);
            if (mem_region[i].attr & 2)
                chip += b-a;
            else if ((a >= 0x00c00000) && (a < 0x00d00000))
                slow += b-a;
            else
                fast += b-a;
        }
        tot = chip + fast + slow;

        sprintf(s, "** %u.%02u MB Total Memory Detected **",
                mb_whole(tot), mb_frac(tot));
        print_line(&r);
        r.y++;
        sprintf(s, "(Chip %u.%02u MB -- Fast %u.%02u MB -- Slow %u.%02u MB)",
                mb_whole(chip), mb_frac(chip),
                mb_whole(fast), mb_frac(fast),
                mb_whole(slow), mb_frac(slow));
        print_line(&r);
        r.y += 2;

    menu_items:
        sprintf(s, "$1 Test All Memory$");
        print_line(&r);
        r.y++;
        sprintf(s, "$2 List & Test Memory Regions$");
        print_line(&r);
        r.y++;
        sprintf(s, "$3 Direct Memory Scan (Ignores Kickstart)$");
        print_line(&r);
        r.y++;
        sprintf(s, "$4 Test Custom Range$");
        print_line(&r);
        r.y += 2;
        sprintf(s, "Pause between Fill & Check: %us", delay_sec);
        print_line(&r);
        r.y++;        
        sprintf(s, "$5 Decrease$  $6 Increase$");
        print_line(&r);
        r.y++;

    next_key:
        do {
            while (!(key = keycode_buffer) && !do_exit)
                continue;
            keycode_buffer = 0;

            if (do_exit || (key == K_ESC))
                goto out;
        } while ((key < K_F1) || (key > K_F6));

        switch (key) {
        case K_F1:
            r.y = 5;
            clear_text_rows(r.y, 8);
            kickstart_memory_test(&r);
            r.y = 5;
            clear_text_rows(r.y, 8);
            if (!do_exit)
                goto menu_items;
            break;
        case K_F2:
            kickstart_memory_list();
            break;
        case K_F3:
            memcheck_direct_scan();
            break;
        case K_F4: {
            struct mem_region mr = { .upper = 1 };
            test_one_region(&r, &mr);
            clear_whole_screen();
            break;
        }
        case K_F5:
            if (delay_sec != 0)
                delay_sec--;
        redraw_pause:
            sprintf(s, "Pause between Fill & Check: %us", delay_sec);
            r.y = 10;
            wait_bos();
            print_line(&r);
            goto next_key;
        case K_F6:
            if (delay_sec < 99)
                delay_sec++;
            goto redraw_pause;
        }
    }

out:
    clear_whole_screen();
}

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

#include "systest.h"

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
        while (!do_exit && (keycode_buffer != K_ESC))
            continue;
        goto out;
    }

    tm_args.round = 0;
    while (!do_exit && (keycode_buffer != K_ESC)) {

        tm_args.start = (nr == 0) ? 1 << 18 : nr << 19;
        tm_args.end = (nr + 1) << 19;

        tm_args.r = *r;
        call_cancellable_test(test_memory_range, &tm_args);

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

    while (!do_exit) {
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
            while (!(key = keycode_buffer) && !do_exit)
                continue;
            keycode_buffer = 0;
            
            if (do_exit || (key == K_ESC))
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
    while (!do_exit && (keycode_buffer != K_ESC)) {
        nr_done = 0;
        for (i = 0; i < nr_mem_regions; i++) {
            /* Calculate inclusive range [a,b] with limits expanded to 64kB
             * alignment (Kickstart sometimes steals RAM from the limits). */
            a = max_t(uint32_t, 0x40000, mem_region[i].lower & ~0xffff);
            b = ((mem_region[i].upper + 0xffff) & ~0xffff) - 1;
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
                nr_done++;
            }
        }
        /* If we did't do any work report this as an error and wait to exit. */
        if (!nr_done) {
            sprintf((char *)r->s, "ERROR: No memory (above 256kB) to test!");
            print_line(r);
            while (!do_exit && (keycode_buffer != K_ESC))
                continue;
            goto out;
        }
        /* Otherwise onto the next round... */
        tm_args.round++;
    }

out:
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
            while (!(key = keycode_buffer) && !do_exit)
                continue;
            keycode_buffer = 0;

            if (do_exit || (key == K_ESC))
                goto out;
        } while ((key < K_F1) || (key > K_F3));

        switch (key) {
        case K_F1:
            r.y = 5;
            clear_text_rows(r.y, 4);
            kickstart_memory_test(&r);
            r.y = 5;
            clear_text_rows(r.y, 4);
            if (!do_exit)
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

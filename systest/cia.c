/*
 * cia.c
 * 
 * Test the 8520 CIA chips.
 * 
 * Written & released by Keir Fraser <keir.xen@gmail.com>
 * 
 * This is free and unencumbered software released into the public domain.
 * See the file COPYING for more details, or visit <http://unlicense.org>.
 */

#include "systest.h"

static void get_cia_times(uint16_t *times)
{
    times[0] = get_ciatime(ciaa, ta);
    times[1] = get_ciatime(ciaa, tb);
    times[2] = get_ciatime(ciab, ta);
    times[3] = get_ciatime(ciab, tb);
}

void ciacheck(void)
{
    char s[80];
    struct char_row r = { .x = 8, .y = 1, .s = s };
    uint16_t i, times[2][4];
    uint32_t exp, tot[4] = { 0 };

    print_menu_nav_line();

    sprintf(s, "-- CIA Test --");
    print_line(&r);
    r.x = 0;
    r.y += 3;

    /* Get CIA timestamps at time of a VBL IRQ. */
    vblank_count = 0;
    do {
        get_cia_times(times[0]);
    } while (!vblank_count);

    /* Wait for 10 more VBL periods and accumulate CIA ticks into 
     * an array of 32-bit counters (tot[]). */
    do {
        get_cia_times(times[1]);
        for (i = 0; i < 4; i++) {
            tot[i] += (uint16_t)(times[0][i] - times[1][i]);
            times[0][i] = times[1][i];
        }
    } while (vblank_count < 11);

    exp = div32(cpu_hz, vbl_hz);
    sprintf(s, "Timer ticks during 10 VBLs (Expect approx. %u):", exp);
    print_line(&r);
    r.y++;

    /* Print the actual tick values and whether they are within 
     * one-percent tolerance (which is pretty generous). */
    for (i = 0; i < 4; i++) {
        static const char *name[] = { "ATA", "ATB", "BTA", "BTB" };
        uint32_t diff = tot[i] > exp ? tot[i] - exp : exp - tot[i];
        int in_tol = diff < (100*exp);
        sprintf(s, "CIA%s %u -> %s", name[i], tot[i],
                in_tol ? "OK (<1%, within tolerance)"
                : "FAILED (>1%, out of tolerance)");
        print_line(&r);
        r.y++;
    }

    while (!do_exit && (keycode_buffer != K_ESC))
        continue;
}

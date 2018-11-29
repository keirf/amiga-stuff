/*
 * battclock.c
 * 
 * Test the Oki MSM6242B / Ricoh RP5C01 battery-backed clock.
 * 
 * Written & released by Keir Fraser <keir.xen@gmail.com>
 * 
 * This is free and unencumbered software released into the public domain.
 * See the file COPYING for more details, or visit <http://unlicense.org>.
 */

#include "systest.h"

struct msm6242 {
    uint8_t _0[3], sec1;
    uint8_t _1[3], sec10;
    uint8_t _2[3], min1;
    uint8_t _3[3], min10;
    uint8_t _4[3], hr1;
    uint8_t _5[3], hr10;
    uint8_t _6[3], day1;
    uint8_t _7[3], day10;
    uint8_t _8[3], mon1;
    uint8_t _9[3], mon10;
    uint8_t _a[3], yr1;
    uint8_t _b[3], yr10;
    uint8_t _c[3], day_of_week;
    uint8_t _d[3], ctl_d;
    uint8_t _e[3], ctl_e;
    uint8_t _f[3], ctl_f;
};

struct rp5c01 {
    uint8_t _0[3], sec1;
    uint8_t _1[3], sec10;
    uint8_t _2[3], min1;
    uint8_t _3[3], min10;
    uint8_t _4[3], hr1;
    uint8_t _5[3], hr10;
    uint8_t _6[3], day_of_week;
    uint8_t _7[3], day1;
    uint8_t _8[3], day10;
    uint8_t _9[3], mon1;
    uint8_t _a[3], mon10;
    uint8_t _b[3], yr1;
    uint8_t _c[3], yr10;
    uint8_t _d[3], ctl_d;
    uint8_t _e[3], ctl_e;
    uint8_t _f[3], ctl_f;
};

enum bc_type {
    BC_RP5C01 = 0,
    BC_MSM6242,
    BC_NONE
};

const static uint8_t days_in_month[] = {
	31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31
};
const static char *mon_str[] = {
	"Jan", "Feb", "Mar", "Apr", "May", "Jun",
	"Jul", "Aug", "Sep", "Oct", "Nov", "Dec"
};
const static char *day_week_str[] = {
	"Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat"
};

/* This function has to be careful: 
 * 1. We do not know whether we have a RP5C01 or MSM6242 chip, and they are 
 *    not register compatible. 
 * 2. We don't want to mess up the existing date/time setting. */
static enum bc_type detect_clock_at(uint32_t base)
{
    volatile struct rp5c01 *rp5c01 = (struct rp5c01 *)base;
    uint8_t t;

    /* Clear all test/stop/reset flags, preserve MSM6242 24h/12h flag. */
    rp5c01->ctl_e = 0;
    rp5c01->ctl_f = rp5c01->ctl_f & 4;
    /* Check that flags really are cleared. If not, there's no clock chip. */
    if (((rp5c01->ctl_e & 0xf) != 0) || ((rp5c01->ctl_f & 0xb) != 0))
        return BC_NONE;

    /* Put RP5C01 into MODE 01 (MSM6242: set HOLD flag). */
    rp5c01->ctl_d = 1;
    if ((rp5c01->ctl_d & 0x9) != 1)
        return BC_NONE;

    /* MSM6242: Check BUSY flag and wait up to 10ms for it to clear. */
    t = 10;
    while ((rp5c01->ctl_d & 2) && --t) {
        rp5c01->ctl_d = 0;
        delay_ms(1);
        rp5c01->ctl_d = 1;
    }
    /* BUSY can't still be set. Should only be set a few usec per second. */
    if ((rp5c01->ctl_d & 0xb) != 1) /* checks ADJ,BUSY,HOLD flags */
        return BC_NONE;

    /* Write a non-existent register (in RP5C01 MODE 01) */
    t = rp5c01->yr10;
    rp5c01->yr10 = 5;
    if (((rp5c01->ctl_d & 0xf) == 1) && ((rp5c01->yr10 & 0xf) == 0)) {
        /* RP5C01? */
        rp5c01->ctl_d = 8; /* MODE 00, TIMER_EN */
        if ((rp5c01->ctl_d & 0xf) == 8)
            return BC_RP5C01;
    } else {
        /* MSM6242? */
        rp5c01->yr10 = t; /* restore day-of-week */
        rp5c01->ctl_d = 0; /* clear HOLD */
        if ((rp5c01->ctl_d & 0x9) == 0) /* check ADJ & HOLD are clear */
            return BC_MSM6242;
    }

    return BC_NONE;
}

static enum bc_type detect_clock(uint32_t *base)
{
    uint32_t bases[] = { 0xdc0000, 0xd80000 };
    enum bc_type bc_type;
    unsigned int i, nr = ARRAY_SIZE(bases);

    /* AGA systems never have RTC at 0xD80000 (only early A2000). Indeed A4000 
     * asserts a Bus Error on accesses in the region 0xD00000-0xDFFFFF. */
    if (chipset_type == CHIPSET_aga)
        nr--;

    for (i = 0; i < nr; i++) {
        bc_type = detect_clock_at(*base = bases[i]);
        if (bc_type != BC_NONE)
            break;
    }

    return bc_type;
}

/* Look for out-of-range register values indicating that the configured 
 * time/date is invalid and requires reset. This will often be the case 
 * when running a clock with no backup battery. */
static int bc_time_is_bogus(enum bc_type bc_type, uint32_t base)
{
    int good = 0;
    uint8_t t;

    switch (bc_type) {
    case BC_RP5C01: {
        volatile struct rp5c01 *rp5c01 = (struct rp5c01 *)base;
        rp5c01->ctl_d = 0; /* stop timer */
        good = ((rp5c01->sec1 & 0xf) <= 9)
            && ((rp5c01->sec10 & 0xf) <= 5)
            && ((rp5c01->min1 & 0xf) <= 9)
            && ((rp5c01->min10 & 0xf) <= 5)
            && ((rp5c01->hr1 & 0xf) <= 9)
            && ((rp5c01->hr10 & 0xf) <= 3)
            && ((rp5c01->day1 & 0xf) <= 9)
            && ((rp5c01->day10 & 0xf) <= 3)
            && ((rp5c01->yr1 & 0xf) <= 9);
        rp5c01->ctl_d = 8; /* start timer */
        break;
    }
    case BC_MSM6242: {
        volatile struct msm6242 *msm6242 = (struct msm6242 *)base;
        msm6242->ctl_d = 1; /* set HOLD */
        t = 10;
        while ((msm6242->ctl_d & 2) && --t) { /* wait 10ms for !BUSY */
            msm6242->ctl_d = 0;
            delay_ms(1);
            msm6242->ctl_d = 1;
        }
        good = ((msm6242->sec1 & 0xf) <= 9)
            && ((msm6242->sec10 & 0xf) <= 5)
            && ((msm6242->min1 & 0xf) <= 9)
            && ((msm6242->min10 & 0xf) <= 5)
            && ((msm6242->hr1 & 0xf) <= 9)
            && ((msm6242->hr10 & 0xf) <= 3)
            && ((msm6242->day1 & 0xf) <= 9)
            && ((msm6242->day10 & 0xf) <= 3)
            && ((msm6242->yr1 & 0xf) <= 9);
        msm6242->ctl_d = 0; /* clear HOLD */
        break;
    }
    default:
        good = 1;
        break;
    }
    return !good;
}

static void bc_get_time(enum bc_type bc_type, uint32_t base, char *s)
{
    uint32_t days_since_1900;
    uint8_t sec, min, hr, day, day_week, mon, hr24, t;
    uint16_t yr;
    switch (bc_type) {
    case BC_RP5C01: {
        volatile struct rp5c01 *rp5c01 = (struct rp5c01 *)base;
        rp5c01->ctl_d = 9;
        hr24 = rp5c01->mon10 & 1;
        rp5c01->ctl_d = 0; /* stop timer */
        sec = ((rp5c01->sec10 & 0xf) * 10) + (rp5c01->sec1 & 0xf);
        min = ((rp5c01->min10 & 0xf) * 10) + (rp5c01->min1 & 0xf);
        if (hr24) {
            hr = ((rp5c01->hr10 & 0x3) * 10) + (rp5c01->hr1 & 0xf);
        } else {
            hr = ((rp5c01->hr10 & 0x1) * 10) + (rp5c01->hr1 & 0xf);
            if (rp5c01->hr10 & 0x2)
                hr += 12;
        }
        day = ((rp5c01->day10 & 0x3) * 10) + (rp5c01->day1 & 0xf);
        mon = ((rp5c01->mon10 & 0x1) * 10) + (rp5c01->mon1 & 0xf);
        yr = ((rp5c01->yr10 & 0xf) * 10) + (rp5c01->yr1 & 0xf);
        day_week = rp5c01->day_of_week & 0xf;
        rp5c01->ctl_d = 8; /* start timer */
        break;
    }
    case BC_MSM6242: {
        volatile struct msm6242 *msm6242 = (struct msm6242 *)base;
        hr24 = !!(msm6242->ctl_f & 4);
        msm6242->ctl_d = 1; /* set HOLD */
        t = 10;
        while ((msm6242->ctl_d & 2) && --t) { /* wait 10ms for !BUSY */
            msm6242->ctl_d = 0;
            delay_ms(1);
            msm6242->ctl_d = 1;
        }
        sec = ((msm6242->sec10 & 0xf) * 10) + (msm6242->sec1 & 0xf);
        min = ((msm6242->min10 & 0xf) * 10) + (msm6242->min1 & 0xf);
        if (hr24) {
            hr = ((msm6242->hr10 & 0x3) * 10) + (msm6242->hr1 & 0xf);
        } else {
            hr = ((msm6242->hr10 & 0x1) * 10) + (msm6242->hr1 & 0xf);
            if (msm6242->hr10 & 0x4)
                hr += 12;
        }
        day = ((msm6242->day10 & 0x3) * 10) + (msm6242->day1 & 0xf);
        mon = ((msm6242->mon10 & 0x1) * 10) + (msm6242->mon1 & 0xf);
        yr = ((msm6242->yr10 & 0xf) * 10) + (msm6242->yr1 & 0xf);
        day_week = msm6242->day_of_week & 0xf;
        msm6242->ctl_d = 0; /* clear HOLD */
        break;
    }
    default:
        s[0] = '\0';
        return;
    }

    sec %= 60;
    min %= 60;
    hr %= 24;
    day &= 31;
    day_week %= 7;
    mon = (uint8_t)(mon-1) % 12;
    yr %= 100;
    yr += (yr < 78) ? 2000 : 1900;

    /* Workbench ignores the day-of-week register and does not set it. 
     * So calculate the day-of-week ourselves. */
    days_since_1900 = (uint32_t)(yr - 1900) * 365; /* years */
    if (yr >= 1904)
        days_since_1900 += (yr - 1901 + (mon >= 2)) >> 2; /* leap years */
    for (t = 0; t < mon; t++)
        days_since_1900 += days_in_month[t]; /* months */
    days_since_1900 += day - 1; /* day of month */
    days_since_1900 += 1; /* 1 Jan 1900 was a Monday */
    day_week = do_div(days_since_1900, 7);

    sprintf(s, "%s %s %u %02u:%02u:%02u %u",
            day_week_str[day_week], mon_str[mon],
            day, hr, min, sec, yr);
}

static void bc_reset(enum bc_type bc_type, uint32_t base)
{
    switch (bc_type) {
    case BC_RP5C01: {
        volatile struct rp5c01 *rp5c01 = (struct rp5c01 *)base;
        rp5c01->ctl_d = 9; /* stop timer, mode 01 */
        rp5c01->mon10 = 1; /* set 24h mode */
        rp5c01->ctl_d = 0; /* stop timer, mode 00 */
        /* Jan 1 00:00:00 2016 */
        rp5c01->sec10 = rp5c01->sec1 = 0;
        rp5c01->min10 = rp5c01->min1 = 0;
        rp5c01->hr10 = rp5c01->hr1 = 0;
        rp5c01->yr10 = 1; rp5c01->yr1 = 6;
        rp5c01->mon10 = 0; rp5c01->mon1 = 1;
        rp5c01->day10 = 0; rp5c01->day1 = 1;
        rp5c01->day_of_week = 6; /* Friday */
        rp5c01->ctl_d = 8; /* start timer */
        break;
    }
    case BC_MSM6242: {
        volatile struct msm6242 *msm6242 = (struct msm6242 *)base;
        uint8_t t;
        msm6242->ctl_d = 1; /* set HOLD */
        t = 10;
        while ((msm6242->ctl_d & 2) && --t) { /* wait 10ms for !BUSY */
            msm6242->ctl_d = 0;
            delay_ms(1);
            msm6242->ctl_d = 1;
        }
        msm6242->ctl_f = 4; /* set 24h mode */
        msm6242->sec10 = msm6242->sec1 = 0;
        msm6242->min10 = msm6242->min1 = 0;
        msm6242->hr10 = msm6242->hr1 = 0;
        /* NB. WB1.3 does not clamp yr10 in range 0-9 and borks if we do so. */
        msm6242->yr10 = 11; msm6242->yr1 = 6; /* 2016-1900 = 116 */
        msm6242->mon10 = 0; msm6242->mon1 = 1;
        msm6242->day10 = 0; msm6242->day1 = 1;
        msm6242->day_of_week = 6; /* Friday */
        msm6242->ctl_d = 0; /* clear HOLD */
        break;
    }
    default:
        break;
    }
}

/* Call this with the full actual 4-digit year */
static uint8_t get_day_of_week(uint8_t day, uint8_t mon, uint16_t yr)
{
    uint32_t days_since_1900;
    uint8_t day_week;
    uint8_t t;

     /* Workbench ignores the day-of-week register and does not set it.
     * So calculate the day-of-week ourselves. */
    days_since_1900 = (uint32_t)(yr - 1900) * 365; /* years */
    if (yr >= 1904)
        days_since_1900 += (yr - 1901 + (mon >= 2)) >> 2; /* leap years */
    for (t = 0; t < mon; t++)
        days_since_1900 += days_in_month[t]; /* months */
    days_since_1900 += day - 1; /* day of month */
    days_since_1900 += 1; /* 1 Jan 1900 was a Monday */
    day_week = do_div(days_since_1900, 7);

    return day_week;
}

static void inc_hours(enum bc_type bc_type, uint32_t base)
{
    uint8_t hr, hr24;

    switch (bc_type) {
    case BC_RP5C01: {
        volatile struct rp5c01 *rp5c01 = (struct rp5c01 *)base;
        // Get current hour
        rp5c01->ctl_d = 1; /* stop timer, mode 01 */
        hr24 = rp5c01->mon10 & 1;
        if (hr24) {
            hr = ((rp5c01->hr10 & 0x3) * 10) + (rp5c01->hr1 & 0xf);
        } else {
            hr = ((rp5c01->hr10 & 0x1) * 10) + (rp5c01->hr1 & 0xf);
            if (rp5c01->hr10 & 0x2)
                hr += 12;
        }

        // Increase
        if (++hr > 23)
            hr = 0;

        // Set
        rp5c01->ctl_d = 0; /* stop timer, mode 00 */
        if (hr24 || hr <= 12) {
            rp5c01->hr1 = do_div(hr, 10);    /* AM flag set implicitly */
            rp5c01->hr10 = hr;
        } else {
            // Set PM flag
            hr -= 12;
            rp5c01->hr1 = do_div(hr, 10);
            rp5c01->hr10 = hr | 0x2;
        }


        rp5c01->ctl_d = 8; /* restart timer */
        break;
    }
    case BC_MSM6242: {
        volatile struct msm6242 *msm6242 = (struct msm6242 *)base;
        uint8_t t;
        hr24 = !!(msm6242->ctl_f & 4);
        msm6242->ctl_d = 1; /* set HOLD */
        t = 10;
        while ((msm6242->ctl_d & 2) && --t) { /* wait 10ms for !BUSY */
            msm6242->ctl_d = 0;
            delay_ms(1);
            msm6242->ctl_d = 1;
        }
        if (hr24) {
            hr = ((msm6242->hr10 & 0x3) * 10) + (msm6242->hr1 & 0xf);
        } else {
            hr = ((msm6242->hr10 & 0x1) * 10) + (msm6242->hr1 & 0xf);
            if (msm6242->hr10 & 0x4)
                hr += 12;
        }
        if (++hr > 23)
            hr = 0;

        if (hr24 || hr <= 12) {
            msm6242->hr1 = do_div(hr, 10);
            msm6242->hr10 = hr;
        } else {
            hr -= 12;
            msm6242->hr1 = do_div(hr, 10);
            msm6242->hr10 = hr | 0x4;
        }

        msm6242->ctl_d = 0; /* clear HOLD */
        break;
    }
    default:
        break;
    }
}

static void inc_minutes(enum bc_type bc_type, uint32_t base)
{
    uint8_t min;

    switch (bc_type) {
    case BC_RP5C01: {
        volatile struct rp5c01 *rp5c01 = (struct rp5c01 *)base;
        // Get current value
        rp5c01->ctl_d = 0; /* stop timer, mode 00 */
        min = ((rp5c01->min10 & 0xf) * 10) + (rp5c01->min1 & 0xf);

        // Increase
        if (++min > 59)
            min = 0;

        // Set
        rp5c01->min1 = do_div(min, 10);
        rp5c01->min10 = min;

        // Also reset seconds
        rp5c01->sec10 = rp5c01->sec1 = 0;

        rp5c01->ctl_d = 8; /* restart timer */
        break;
    }
    case BC_MSM6242: {
        volatile struct msm6242 *msm6242 = (struct msm6242 *)base;
        uint8_t t;
        msm6242->ctl_d = 1; /* set HOLD */
        t = 10;
        while ((msm6242->ctl_d & 2) && --t) { /* wait 10ms for !BUSY */
            msm6242->ctl_d = 0;
            delay_ms(1);
            msm6242->ctl_d = 1;
        }
        min = ((msm6242->min10 & 0xf) * 10) + (msm6242->min1 & 0xf);
        if (++min > 59)
            min = 0;
        msm6242->min1 = do_div(min, 10);
        msm6242->min10 = min;
        msm6242->sec10 = msm6242->sec1 = 0;
        msm6242->ctl_d = 0; /* clear HOLD */
        break;
    }
    default:
        break;
    }
}

static void inc_day(enum bc_type bc_type, uint32_t base)
{
    uint8_t day, mon;
    uint16_t yr;

    switch (bc_type) {
    case BC_RP5C01: {
        volatile struct rp5c01 *rp5c01 = (struct rp5c01 *)base;
        rp5c01->ctl_d = 0; /* stop timer, mode 00 */
        day = ((rp5c01->day10 & 0x3) * 10) + (rp5c01->day1 & 0xf);
        mon = ((rp5c01->mon10 & 0x1) * 10) + (rp5c01->mon1 & 0xf);
        if (mon <= 0)        /* Make sure month is in the 1-12 range*/
            mon = 1;
        mon %= 12;
        yr = ((rp5c01->yr10 & 0xf) * 10) + (rp5c01->yr1 & 0xf);
        yr += (yr < 78) ? 2000 : 1900;        /* 0 <= yr <= 99 --> 1978-2077 */

        if (++day > days_in_month[(mon-1)])
            day = 1;

        rp5c01->day_of_week = get_day_of_week(day, mon, yr);
        rp5c01->day1 = do_div(day, 10);
        rp5c01->day10 = day;
        rp5c01->ctl_d = 8; /* restart timer */
        break;
    }
    case BC_MSM6242: {
        volatile struct msm6242 *msm6242 = (struct msm6242 *)base;
        uint8_t t;
        msm6242->ctl_d = 1; /* set HOLD */
        t = 10;
        while ((msm6242->ctl_d & 2) && --t) { /* wait 10ms for !BUSY */
            msm6242->ctl_d = 0;
            delay_ms(1);
            msm6242->ctl_d = 1;
        }

        day = ((msm6242->day10 & 0x3) * 10) + (msm6242->day1 & 0xf);
        mon = ((msm6242->mon10 & 0x1) * 10) + (msm6242->mon1 & 0xf);
        if (mon <= 0)        /* Make sure month is in the 1-12 range*/
            mon = 1;
        mon %= 12;
        yr = ((msm6242->yr10 & 0xf) * 10) + (msm6242->yr1 & 0xf);
        yr += (yr < 78) ? 2000 : 1900;

        if (++day > days_in_month[(mon-1)])
            day = 1;

        msm6242->day_of_week = get_day_of_week(day, mon, yr);
        msm6242->day1 = do_div(day, 10);
        msm6242->day10 = day;
        msm6242->ctl_d = 0; /* clear HOLD */
        break;
    }
    default:
        break;
    }
}

static void inc_month(enum bc_type bc_type, uint32_t base)
{
    uint8_t day, mon;
    uint16_t yr;

    switch (bc_type) {
    case BC_RP5C01: {
        volatile struct rp5c01 *rp5c01 = (struct rp5c01 *)base;
        rp5c01->ctl_d = 0; /* stop timer, mode 00 */
        day = ((rp5c01->day10 & 0x3) * 10) + (rp5c01->day1 & 0xf);
        mon = ((rp5c01->mon10 & 0x1) * 10) + (rp5c01->mon1 & 0xf);
        if (++mon > 12)
            mon = 1;
		if (day > days_in_month[(mon-1)])		/* Day might not exist in new month */
            day = 1;
        yr = ((rp5c01->yr10 & 0xf) * 10) + (rp5c01->yr1 & 0xf);
        yr += (yr < 78) ? 2000 : 1900;
        rp5c01->day_of_week = get_day_of_week(day, mon, yr);
        rp5c01->day1 = do_div(day, 10);
        rp5c01->day10 = day;
        rp5c01->mon1 = do_div(mon, 10);
        rp5c01->mon10 = mon;
        rp5c01->ctl_d = 8; /* restart timer */
        break;
    }
    case BC_MSM6242: {
        volatile struct msm6242 *msm6242 = (struct msm6242 *)base;
        uint8_t t;
        msm6242->ctl_d = 1; /* set HOLD */
        t = 10;
        while ((msm6242->ctl_d & 2) && --t) { /* wait 10ms for !BUSY */
            msm6242->ctl_d = 0;
            delay_ms(1);
            msm6242->ctl_d = 1;
        }
        day = ((msm6242->day10 & 0x3) * 10) + (msm6242->day1 & 0xf);
        mon = ((msm6242->mon10 & 0x1) * 10) + (msm6242->mon1 & 0xf);
        if (++mon > 12)
            mon = 1;
		if (day > days_in_month[(mon-1)])
            day = 1;
        yr = ((msm6242->yr10 & 0xf) * 10) + (msm6242->yr1 & 0xf);
        yr += (yr < 78) ? 2000 : 1900;

        msm6242->day_of_week = get_day_of_week(day, mon, yr);
        msm6242->day1 = do_div(day, 10);
        msm6242->day10 = day;
        msm6242->mon1 = do_div(mon, 10);
        msm6242->mon10 = mon;
        msm6242->ctl_d = 0; /* clear HOLD */
        break;
    }
    default:
        break;
    }
}

static void inc_dec_year(enum bc_type bc_type, uint32_t base, int8_t amount)
{
    uint8_t day, mon;
    uint16_t yr;

    switch (bc_type) {
    case BC_RP5C01: {
        volatile struct rp5c01 *rp5c01 = (struct rp5c01 *)base;
        rp5c01->ctl_d = 0; /* stop timer, mode 00 */
        day = ((rp5c01->day10 & 0x3) * 10) + (rp5c01->day1 & 0xf);
        mon = ((rp5c01->mon10 & 0x1) * 10) + (rp5c01->mon1 & 0xf);
        yr = ((rp5c01->yr10 & 0xf) * 10) + (rp5c01->yr1 & 0xf);
        yr += (yr < 78) ? 2000 : 1900;        /* 0 <= yr <= 99 --> 1978-2077 */

		yr += amount;
        if (yr > 2077)
            yr = 1978;

        rp5c01->day_of_week = get_day_of_week(day, mon, yr);

        yr -= (yr >= 2000) ? 2000 : 1900;
        rp5c01->yr1 = do_div(yr, 10);
        rp5c01->yr10 = yr;
        rp5c01->ctl_d = 8; /* restart timer */
        break;
    }
    case BC_MSM6242: {
        volatile struct msm6242 *msm6242 = (struct msm6242 *)base;
        uint8_t t;
        msm6242->ctl_d = 1; /* set HOLD */
        t = 10;
        while ((msm6242->ctl_d & 2) && --t) { /* wait 10ms for !BUSY */
            msm6242->ctl_d = 0;
            delay_ms(1);
            msm6242->ctl_d = 1;
        }
        day = ((msm6242->day10 & 0x3) * 10) + (msm6242->day1 & 0xf);
        mon = ((msm6242->mon10 & 0x1) * 10) + (msm6242->mon1 & 0xf);
        yr = ((msm6242->yr10 & 0xf) * 10) + (msm6242->yr1 & 0xf);
        yr += (yr < 78) ? 2000 : 1900;

		yr += amount;
        if (yr > 2077)
            yr = 1978;

		yr -= (yr >= 2000) ? 2000 : 1900;
        msm6242->day_of_week = get_day_of_week(day, mon, yr);
        msm6242->yr1 = do_div(yr, 10);
        msm6242->yr10 = yr;
        msm6242->ctl_d = 0; /* clear HOLD */
        break;
    }
    default:
        break;
    }
}



void battclock_test(void)
{
    char s[80];
    struct char_row r = { .s = s };
    uint8_t key = 0, is_bogus = 0;
    enum bc_type bc_type;
    uint32_t base;

    r.x = 5;
    r.y = 0;
    sprintf(s, "-- Battery-Backed Clock Test --");
    print_line(&r);

    bc_type = detect_clock(&base);
    if (bc_type == BC_NONE) {
        sprintf(s, "** No Clock Detected **");
    } else {
        sprintf(s, "%s detected at %08x",
                (bc_type == BC_RP5C01) ? "RP5C01" : "MSM6242",
                base);
    }
    r.x = 7;
    r.y = 3;
    print_line(&r);

    if (bc_type != BC_NONE) {
        r.x = 9;
        r.y = 8;
        sprintf(s, "$1 +Hours$   $2 +Minutes$");
        print_line(&r);
        r.y++;
        sprintf(s, "$3 +Day$     $4 +Month$");
        print_line(&r);
        r.y++;
        sprintf(s, "$5 +Year$    $6 -Year$");
        print_line(&r);
        r.y += 2;
        sprintf(s, "$9 Reset Date & Time$");
        print_line(&r);
    }

    while (!do_exit) {
        do {
            r.x = 7;
            r.y = 5;
            bc_get_time(bc_type, base, s);
            wait_bos();
            print_line(&r);
            if (bc_time_is_bogus(bc_type, base) && !is_bogus) {
                is_bogus = 1;
                r.y++;
                sprintf(s, "WARNING: Invalid date/time -- needs reset?");
                print_line(&r);
            }
            delay_ms(1);
        } while (!do_exit && !(key = keycode_buffer));
        keycode_buffer = 0;
        if (key == K_ESC)
            break;
        if (key == K_F1) {
            inc_hours(bc_type, base);
        }
        if (key == K_F2) {
            inc_minutes(bc_type, base);
        }
        if (key == K_F3) {
            inc_day(bc_type, base);
        }
        if (key == K_F4) {
            inc_month(bc_type, base);
        }
        if (key == K_F5) {
            inc_dec_year(bc_type, base, +1);
        }
        if (key == K_F6) {
            inc_dec_year(bc_type, base, -1);
        }
		if (key == K_F9) {
            bc_reset(bc_type, base);
            is_bogus = 0;
            clear_text_rows(6, 1);
        }
    }
}

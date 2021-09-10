/*
 * battclock.c
 * 
 * Test the battery-backed real-time clock.
 * Supported chips:
 *  * Oki MSM6242 (also Epson RTC-62421/62423/72421/72423)
 *  * Ricoh RP5C01
 * 
 * Written & released by Keir Fraser <keir.xen@gmail.com>
 * 
 * This is free and unencumbered software released into the public domain.
 * See the file COPYING for more details, or visit <http://unlicense.org>.
 */

#include "testkit.h"

struct time {
    uint8_t sec;   /* 0-59 */
    uint8_t min;   /* 0-59 */
    uint8_t hour;  /* 0-23 */
    uint8_t mday;  /* 1-31 */
    uint8_t mon;   /* 0-11 */
    uint16_t year; /* 1978-2077 */
};

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

struct bc {
    enum bc_type type;
    uint32_t base;
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

/* A short delay is required for register writes to take effect (issue #11). */
static void DELAY(void)
{
    uint16_t t_s = get_ciaatb();
    while ((uint16_t)(t_s - get_ciaatb()) < 2)
        continue;
}

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
    DELAY();
    /* Check that flags really are cleared. If not, there's no clock chip. */
    if (((rp5c01->ctl_e & 0xf) != 0) || ((rp5c01->ctl_f & 0xb) != 0))
        return BC_NONE;

    /* Put RP5C01 into MODE 01 (MSM6242: set HOLD flag). */
    rp5c01->ctl_d = 1;
    DELAY();
    if ((rp5c01->ctl_d & 0x9) != 1)
        return BC_NONE;

    /* MSM6242: Check BUSY flag and wait up to 10ms for it to clear. */
    t = 10;
    while ((rp5c01->ctl_d & 2) && --t) {
        rp5c01->ctl_d = 0;
        delay_ms(1);
        rp5c01->ctl_d = 1;
        DELAY();
    }
    /* BUSY can't still be set. Should only be set a few usec per second. */
    if ((rp5c01->ctl_d & 0xb) != 1) /* checks ADJ,BUSY,HOLD flags */
        return BC_NONE;

    /* Write a non-existent register (in RP5C01 MODE 01) */
    t = rp5c01->yr10;
    rp5c01->yr10 = 5;
    DELAY();
    if (((rp5c01->ctl_d & 0xf) == 1) && ((rp5c01->yr10 & 0xf) == 0)) {
        /* RP5C01? */
        rp5c01->ctl_d = 8; /* MODE 00, TIMER_EN */
        DELAY();
        if ((rp5c01->ctl_d & 0xf) == 8)
            return BC_RP5C01;
    } else {
        /* MSM6242? */
        rp5c01->yr10 = t; /* restore day-of-week */
        rp5c01->ctl_d = 0; /* clear HOLD */
        DELAY();
        if ((rp5c01->ctl_d & 0x9) == 0) /* check ADJ & HOLD are clear */
            return BC_MSM6242;
    }

    return BC_NONE;
}

static void detect_clock(struct bc *bc)
{
    uint32_t bases[] = { 0xdc0000, 0xd80000 };
    unsigned int i, nr = ARRAY_SIZE(bases);

    for (i = 0; i < nr; i++) {
        bc->type = detect_clock_at(bc->base = bases[i]);
        if (bc->type != BC_NONE)
            break;
    }
}

static void msm6242_hold(volatile struct msm6242 *msm6242)
{
    unsigned int i = 10;
    msm6242->ctl_d = 1; /* set HOLD */
    DELAY();
    while ((msm6242->ctl_d & 2) && --i) { /* wait 10ms for !BUSY */
        msm6242->ctl_d = 0;
        delay_ms(1);
        msm6242->ctl_d = 1;
        DELAY();
    }
}

static void msm6242_release(volatile struct msm6242 *msm6242)
{
    DELAY();
    msm6242->ctl_d = 0; /* clear HOLD */
}

static void rp5c01_hold(volatile struct rp5c01 *rp5c01)
{
    rp5c01->ctl_d = 0; /* stop timer */
    DELAY();
}

static void rp5c01_release(volatile struct rp5c01 *rp5c01)
{
    DELAY();
    rp5c01->ctl_d = 8; /* start timer */
}

/* Look for out-of-range register values indicating that the configured 
 * time/date is invalid and requires reset. This will often be the case 
 * when running a clock with no backup battery. */
static int bc_time_is_bogus(struct bc *bc)
{
    int good = 0;

    switch (bc->type) {
    case BC_RP5C01: {
        volatile struct rp5c01 *rp5c01 = (struct rp5c01 *)bc->base;
        rp5c01_hold(rp5c01);
        good = ((rp5c01->sec1 & 0xf) <= 9)
            && ((rp5c01->sec10 & 0xf) <= 5)
            && ((rp5c01->min1 & 0xf) <= 9)
            && ((rp5c01->min10 & 0xf) <= 5)
            && ((rp5c01->hr1 & 0xf) <= 9)
            && ((rp5c01->hr10 & 0xf) <= 3)
            && ((rp5c01->day1 & 0xf) <= 9)
            && ((rp5c01->day10 & 0xf) <= 3)
            && ((rp5c01->yr1 & 0xf) <= 9);
        rp5c01_release(rp5c01);
        break;
    }
    case BC_MSM6242: {
        volatile struct msm6242 *msm6242 = (struct msm6242 *)bc->base;
        msm6242_hold(msm6242);
        good = ((msm6242->sec1 & 0xf) <= 9)
            && ((msm6242->sec10 & 0xf) <= 5)
            && ((msm6242->min1 & 0xf) <= 9)
            && ((msm6242->min10 & 0xf) <= 5)
            && ((msm6242->hr1 & 0xf) <= 9)
            && ((msm6242->hr10 & 0xf) <= 3)
            && ((msm6242->day1 & 0xf) <= 9)
            && ((msm6242->day10 & 0xf) <= 3)
            && ((msm6242->yr1 & 0xf) <= 9)
            && ((msm6242->ctl_f & 0xb) == 0); /* REST, STOP, TEST = 0 */
        msm6242_release(msm6242);
        break;
    }
    default:
        good = 1;
        break;
    }
    return !good;
}

static void bc_get_time(struct bc *bc, struct time *t)
{
    uint8_t hr24;

    switch (bc->type) {
    case BC_RP5C01: {
        volatile struct rp5c01 *rp5c01 = (struct rp5c01 *)bc->base;
        rp5c01->ctl_d = 9;
        DELAY();
        hr24 = rp5c01->mon10 & 1;
        rp5c01_hold(rp5c01);
        t->sec = ((rp5c01->sec10 & 0xf) * 10) + (rp5c01->sec1 & 0xf);
        t->min = ((rp5c01->min10 & 0xf) * 10) + (rp5c01->min1 & 0xf);
        if (hr24) {
            t->hour = ((rp5c01->hr10 & 0x3) * 10) + (rp5c01->hr1 & 0xf);
        } else {
            t->hour = ((rp5c01->hr10 & 0x1) * 10) + (rp5c01->hr1 & 0xf);
            if (rp5c01->hr10 & 0x2)
                t->hour += 12;
        }
        t->mday = ((rp5c01->day10 & 0x3) * 10) + (rp5c01->day1 & 0xf);
        t->mon = ((rp5c01->mon10 & 0x1) * 10) + (rp5c01->mon1 & 0xf);
        t->year = ((rp5c01->yr10 & 0xf) * 10) + (rp5c01->yr1 & 0xf);
        /*day_week = rp5c01->day_of_week & 0xf;*/
        rp5c01_release(rp5c01);
        break;
    }
    case BC_MSM6242: {
        volatile struct msm6242 *msm6242 = (struct msm6242 *)bc->base;
        hr24 = !!(msm6242->ctl_f & 4);
        msm6242_hold(msm6242);
        t->sec = ((msm6242->sec10 & 0xf) * 10) + (msm6242->sec1 & 0xf);
        t->min = ((msm6242->min10 & 0xf) * 10) + (msm6242->min1 & 0xf);
        if (hr24) {
            t->hour = ((msm6242->hr10 & 0x3) * 10) + (msm6242->hr1 & 0xf);
        } else {
            t->hour = ((msm6242->hr10 & 0x1) * 10) + (msm6242->hr1 & 0xf);
            if (msm6242->hr10 & 0x4)
                t->hour += 12;
        }
        t->mday = ((msm6242->day10 & 0x3) * 10) + (msm6242->day1 & 0xf);
        t->mon = ((msm6242->mon10 & 0x1) * 10) + (msm6242->mon1 & 0xf);
        t->year = ((msm6242->yr10 & 0xf) * 10) + (msm6242->yr1 & 0xf);
        /*day_week = msm6242->day_of_week & 0xf;*/
        msm6242_release(msm6242);
        break;
    }
    default:
        return;
    }

    t->sec %= 60;
    t->min %= 60;
    t->hour %= 24;
    t->mday &= 31;
    t->mon = (uint8_t)(t->mon-1) % 12;
    t->year %= 100;
    t->year += (t->year < 78) ? 2000 : 1900;
}

static uint8_t get_day_week(struct time *t)
{
    uint32_t days_since_1900;
    unsigned int i;

    /* Workbench ignores the day-of-week register and does not set it. 
     * So calculate the day-of-week ourselves. */
    days_since_1900 = (t->year - 1900) * 365; /* years */
    if (t->year >= 1904) {
        /* leap years */
        days_since_1900 += (t->year - 1901 + (t->mon >= 2)) >> 2;
    }
    for (i = 0; i < t->mon; i++)
        days_since_1900 += days_in_month[i]; /* months */
    days_since_1900 += t->mday - 1; /* day of month */
    days_since_1900 += 1; /* 1 Jan 1900 was a Monday */
    return do_div(days_since_1900, 7);
}

static void strtime(struct time *t, char *s)
{
    uint8_t day_week = get_day_week(t);

    sprintf(s, "%s %s %u %02u:%02u:%02u %u",
            day_week_str[day_week], mon_str[t->mon],
            t->mday, t->hour, t->min, t->sec, t->year);
}

static void bc_set_time(struct bc *bc, struct time *t)
{
    uint8_t day_week = get_day_week(t);
    uint8_t yr = t->year - 1900;
    uint8_t mon = t->mon + 1;

    switch (bc->type) {
    case BC_RP5C01: {
        volatile struct rp5c01 *rp5c01 = (struct rp5c01 *)bc->base;
        rp5c01->ctl_d = 9; /* stop timer, mode 01 */
        DELAY();
        rp5c01->mon10 = 1; /* set 24h mode */
        DELAY();
        rp5c01_hold(rp5c01);
        /* Jan 1 00:00:00 2016 */
        rp5c01->sec10 = t->sec / 10;
        rp5c01->sec1 = t->sec % 10;
        rp5c01->min10 = t->min / 10;
        rp5c01->min1 = t->min % 10;
        rp5c01->hr10 = t->hour / 10;
        rp5c01->hr1 = t->hour % 10;
        rp5c01->yr10 = (yr / 10) % 10;
        rp5c01->yr1 = yr % 10;
        rp5c01->mon10 = mon / 10;
        rp5c01->mon1 = mon % 10;
        rp5c01->day10 = t->mday / 10;
        rp5c01->day1 = t->mday % 10;
        rp5c01->day_of_week = day_week;
        rp5c01_release(rp5c01);
        break;
    }
    case BC_MSM6242: {
        volatile struct msm6242 *msm6242 = (struct msm6242 *)bc->base;
        msm6242->ctl_f = 1; /* REST */
        DELAY();
        msm6242_hold(msm6242);
        msm6242->ctl_f = 5; /* set 24h mode */
        DELAY();
        msm6242->sec10 = t->sec / 10;
        msm6242->sec1 = t->sec % 10;
        msm6242->min10 = t->min / 10;
        msm6242->min1 = t->min % 10;
        msm6242->hr10 = t->hour / 10;
        msm6242->hr1 = t->hour % 10;
        /* NB. WB1.3 does not clamp yr10 in range 0-9 and borks if we do so. */
        msm6242->yr10 = yr / 10;
        msm6242->yr1 = yr % 10;
        msm6242->mon10 = mon / 10;
        msm6242->mon1 = mon % 10;
        msm6242->day10 = t->mday / 10;
        msm6242->day1 = t->mday % 10;
        msm6242->day_of_week = day_week;
        msm6242_release(msm6242);
        msm6242->ctl_f = 4; /* !REST */
        DELAY();
        break;
    }
    default:
        break;
    }
}

static void battclock_set_time(
    struct bc *bc, struct time *t, struct char_row *r)
{
    char *s = (char *)r->s;
    uint8_t key;
    unsigned int i, j;

    r->x = 10;
    r->y = 7;
    sprintf(s, "-- Set Date & Time --");
    print_line(r);
    r->x -= 7;
    r->y++;
    sprintf(s, "$1 Month$  $2 Day$      $3 Year$");
    print_line(r);
    r->y++;
    sprintf(s, "$4 Hour$   $5 Minutes$  $6 Seconds$");
    print_line(r);
    r->x += 9;
    r->y++;
    sprintf(s, "$E Save & Exit$");
    print_line(r);

    r->x = 7;
    r->y = 5;
    strtime(t, s);
    wait_bos();
    print_line(r);

    for (;;) {

        do {
            while (!(key = keycode_buffer) && !do_exit)
                continue;
            keycode_buffer = 0;

            if (do_exit || (key == K_ESC))
                goto out;
        } while ((key < K_F1) || (key > K_F6));

        i = 0;
        do {
            switch (key) {
            case K_F1: /* Month */
                t->mon++;
                t->mon %= 12;
                break;
            case K_F2: /* Day */
                t->mday++;
                if (t->mday > 31)
                    t->mday = 1;
                break;
            case K_F3: /* Year */
                t->year++;
                if (t->year > 2077)
                    t->year = 1978;
                break;
            case K_F4: /* Hour */
                t->hour++;
                t->hour %= 24;
                break;
            case K_F5: /* Minutes */
                t->min++;
                t->min %= 60;
                break;
            case K_F6: /* Seconds */
                t->sec++;
                t->sec %= 60;
                break;
            }

            strtime(t, s);
            wait_bos();
            print_line(r);

            j = (i == 0) ? 500 : (i < 5) ? 100 : 50;
            while (--j && !do_exit && key_pressed[key])
                delay_ms(1);
            i++;

        } while (key_pressed[key] && !do_exit);
    }

out:
    clear_text_rows(7, 4);
}

void battclock_test(void)
{
    char s[80];
    struct char_row r = { .s = s };
    uint8_t key, is_bogus;
    struct time time;
    struct bc bc;

    print_menu_nav_line();

    r.x = 5;
    r.y = 0;
    sprintf(s, "-- RTC / Battery-Backed Clock --");
    print_line(&r);

    detect_clock(&bc);
redisplay:
    if (bc.type == BC_NONE) {
        sprintf(s, "** No Clock Detected **");
    } else {
        sprintf(s, "%s detected at %08x",
                (bc.type == BC_RP5C01) ? "RP5C01" : "6242/7242",
                bc.base);
    }
    r.x = 7;
    r.y = 3;
    print_line(&r);

    if (bc.type == BC_NONE) {
        while (!do_exit && (keycode_buffer != K_ESC))
            continue;
        keycode_buffer = 0;
        return;
    }

    r.x = 9;
    r.y = 8;
    sprintf(s, "$1 Reset Date & Time$");
    print_line(&r);
    r.y++;
    sprintf(s, "$2 Set Date & Time$");
    print_line(&r);
    r.y++;

    is_bogus = 0;

    while (!do_exit) {
        do {
            r.x = 7;
            r.y = 5;
            bc_get_time(&bc, &time);
            strtime(&time, s);
            wait_bos();
            print_line(&r);
            if (bc_time_is_bogus(&bc) && !is_bogus) {
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
        switch (key) {
        case K_F1:
            /* Fri Jan 1 00:00:00 2016 */
            memset(&time, 0, sizeof(time));
            time.mday = 1;
            time.year = 2016;
            bc_set_time(&bc, &time);
            is_bogus = 0;
            clear_text_rows(6, 1);
            break;
        case K_F2:
            clear_text_rows(6, 1);
            battclock_set_time(&bc, &time, &r);
            if (do_exit)
                break;
            bc_set_time(&bc, &time);
            goto redisplay;
        }
    }
}

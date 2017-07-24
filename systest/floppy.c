/*
 * floppy.c
 * 
 * Test the standard Amiga floppy-drive interface: DF0-DF3.
 * 
 * Written & released by Keir Fraser <keir.xen@gmail.com>
 * 
 * This is free and unencumbered software released into the public domain.
 * See the file COPYING for more details, or visit <http://unlicense.org>.
 */

#include "systest.h"

unsigned int mfm_decode_track(void *mfmbuf, void *headers, void *data,
                              uint16_t mfm_bytes);
void mfm_encode_track(void *mfmbuf, uint16_t tracknr, uint16_t mfm_bytes);

/* CIAB IRQ: FLAG (disk index) pulse counter. */
static volatile unsigned int disk_index_count;
static volatile uint32_t disk_index_time, disk_index_period;

void disk_index_IRQ(void)
{
    uint32_t time = get_time();
    disk_index_count++;
    disk_index_period = time - disk_index_time;
    disk_index_time = time;
}

static void drive_deselect(void)
{
    ciab->prb |= 0xf9; /* motor-off, deselect all */
}

/* Select @drv and set motor on or off. */
static void drive_select(unsigned int drv, int on)
{
    drive_deselect(); /* motor-off, deselect all */
    if (on)
        ciab->prb &= ~CIABPRB_MTR; /* motor-on */
    ciab->prb &= ~(CIABPRB_SEL0 << drv); /* select drv */
}

/* Basic wait-for-RDY. */
static void drive_wait_ready(void)
{
    uint32_t s = get_time(), half_sec = ms_to_ticks(500);
    int ready;

    do {
        ready = !!(~ciaa->pra & CIAAPRA_RDY);
    } while (!ready && ((get_time() - s) < half_sec));
}

/* Sophisticated wait-for-RDY with diagnostic report. */
static void drive_check_ready(struct char_row *r)
{
    uint32_t s = get_time(), e, one_sec = ms_to_ticks(1000);
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
    uint8_t motors = 0, pra, old_pra, key = 0;
    unsigned int i, old_disk_index_count;
    uint32_t rdy_delay, mtr_time, key_time, mtr_timeout;
    int rdy_changed;

    /* Motor on for 30 seconds at a time when there is no user input. */
    mtr_timeout = ms_to_ticks(30*1000);

    while (!do_exit && (key != K_ESC)) {

        /* Oddities of external drives when motor is off:
         *  1. TRK0 signal may be switched off;
         *  2. Drive may not physically step heads, in one or both directions.
         * However:
         *  1. CHNG signal correctly asserts on disk removal and deasserts on
         *     disk insertion + step signal (even if the drive does not
         *     physically step);
         *  2. WPRO signal appears to be correctly produced at all times 
         *     when a disk is in the drive. 
         * In summary: 
         *  Do not step heads or synchronise to track 0 except when the motor 
         *  is switched on, and preferably after waiting for RDY or 500ms. 
         *  CHNG and WPRO handling can occur with motor switched off. */
        drive_select(drv, 1);

        /* We shouldn't strictly need to wait for RDY but it's sensible to
         * allow the turn-on current surge to subside before energising the
         * stepper motor. */
        drive_wait_ready();

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

        /* Switch off the drive motor if it was only turned on for
         * external-drive seek test. */
        if (!(motors & (1u << drv)))
            drive_select(drv, 0);

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

        while (!do_exit) {
            if (((pra = ciaa->pra) != old_pra) || key) {
                rdy_changed = !!((old_pra ^ pra) & CIAAPRA_RDY);
                if (rdy_changed)
                    rdy_delay = get_time() - mtr_time;
                sprintf(s, "Motors=(%c%c%c%c) CIAAPRA=0x%02x (%s %s %s %s)",
                        motors&(1u<<0) ? '0' : ' ',
                        motors&(1u<<1) ? '1' : ' ',
                        motors&(1u<<2) ? '2' : ' ',
                        motors&(1u<<3) ? '3' : ' ',
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
                        disk_index_count, idxstr,
                        !!(motors&(1u<<drv)) ? "On" : "Off",
                        !!(old_pra & CIAAPRA_RDY), rdystr);
                r->y++;
                print_line(r);
                r->y--;
                old_disk_index_count = disk_index_count;
                rdy_changed = 0;
            }
            if (((get_time() - key_time) >= mtr_timeout) && motors) {
                int was_on = !!(motors & (1u<<drv));
                motors = 0;
                for (i = 0; i < 4; i++)
                    drive_select(i, 0);
                drive_select(drv, 0);
                if (was_on)
                    mtr_time = get_time();
                key = 1; /* force print */
                continue;
            }
            if (!(key = keycode_buffer))
                continue;
            keycode_buffer = 0;
            key_time = get_time();
            if ((key >= K_F1) && (key <= 0x53)) { /* F1-F4 */
                drv = key - K_F1;
                r->y--;
                break;
            } else if (key == 0x54) { /* F5 */
                motors ^= 1u << drv;
                drive_select(drv, !!(motors & (1u << drv)));
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
    for (i = 0; i < 4; i++)
        drive_select(i, 0);
    drive_deselect();

    return drv;
}

struct sec_header {
    uint8_t format, trk, sec, togo;
    uint32_t data_csum;
};

static void drive_read_test(unsigned int drv, struct char_row *r)
{
    char *s = (char *)r->s, retrystr[20];
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

    drive_select(drv, 1);
    drive_check_ready(r);
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

    for (i = 0; i < 160; i++) {
        retries = 0;
        do {
            retrystr[0] = '\0';
            if (retries)
                sprintf(retrystr, " attempt %u", retries+1);
            sprintf(s, "Reading Track %u...%s", i, retrystr);
            print_line(r);
            done = (do_exit || (keycode_buffer == K_ESC));
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
    drive_select(drv, 0);
    drive_deselect();

    while (!done)
        done = (do_exit || keycode_buffer == K_ESC);
    keycode_buffer = 0;
}

static uint32_t wait_for_index(void)
{
    uint16_t ticks_per_ms = ms_to_ticks(1);
    uint32_t s = disk_index_time, e;
    while ((e = disk_index_time) == s) {
        if (div32(get_time()-s, ticks_per_ms) > 1000)
            return 1000;
    }
    return div32(e-s, ticks_per_ms);
}

static char *index_wait_to_str(uint32_t ms)
{
    return ((ms < 180) ? "Early" : (ms > 220) ? "Late" : "OK");
}

static void drive_write_test(unsigned int drv, struct char_row *r)
{
    char *s = (char *)r->s, retrystr[20];
    void *mfmbuf;
    struct sec_header *headers;
    unsigned int i, j, mfm_bytes = 13100, nr_secs;
    uint32_t erase_wait, write_wait;
    uint16_t valid_map;
    int done = 0, retries, late_indexes = 0;
    uint8_t rnd, *data;

    r->y = 0;
    sprintf(s, "-- DF%u: Write Test --", drv);
    print_line(r);
    r->y += 2;

    mfmbuf = allocmem(mfm_bytes);
    headers = allocmem(12 * sizeof(*headers));
    data = allocmem(12 * 512);

    drive_select(drv, 1);
    drive_check_ready(r);
    seek_cyl0();
    r->y++;

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

    for (i = 158; i < 160; i++) {

        retries = 0;
        do {
            retrystr[0] = '\0';
            if (retries)
                sprintf(retrystr, " attempt %u", retries+1);
            sprintf(s, "Writing Track %u...%s", i, retrystr);
            print_line(r);
            done = (do_exit || (keycode_buffer == K_ESC));
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
            rnd = get_time();

            /* erase */
            memset(mfmbuf, rnd, mfm_bytes);
            wait_for_index();
            disk_write_track(mfmbuf, mfm_bytes);
            disk_wait_dma();

            /* write */
            mfm_encode_track(mfmbuf, i, mfm_bytes);
            erase_wait = wait_for_index();
            disk_write_track(mfmbuf, mfm_bytes);
            disk_wait_dma();

            /* read */
            memset(mfmbuf, 0, mfm_bytes);
            write_wait = wait_for_index();
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

        sprintf(s, "Track %u written:", i);
        print_line(r);
        r->y++;
        sprintf(s, " - Erase To Index Pulse: %u ms (%s)", erase_wait,
                index_wait_to_str(erase_wait));
        print_line(r);
        r->y++;
        sprintf(s, " - Write To Index Pulse: %u ms (%s)", write_wait,
                index_wait_to_str(write_wait));
        print_line(r);
        r->y++;
        if ((erase_wait > 220) || (write_wait > 220))
            late_indexes = 1;
    }

    r->y++;
    sprintf(s, "Tracks 158 & 159 written okay");
    print_line(r);
    if (late_indexes) {
        r->x = 0;
        r->y++;
        sprintf(s, "(Late Index Pulses may be due to drive emulation)");
        print_line(r);
    }

out:
    drive_select(drv, 0);
    drive_deselect();

    while (!done)
        done = (do_exit || keycode_buffer == K_ESC);
    keycode_buffer = 0;
}

static void drive_cal_test(unsigned int drv, struct char_row *r)
{
    char *s = (char *)r->s;
    char map[12];
    void *mfmbuf, *data;
    struct sec_header *headers;
    unsigned int i, mfm_bytes = 13100, nr_secs;
    int done = 0;
    uint8_t key, good, progress = 0, head, cyl = 0;
    char progress_chars[] = "|/-\\";

    r->x = r->y = 0;
    sprintf(s, "-- DF%u: Continuous Head Calibration Test --", drv);
    print_line(r);
    r->y += 2;

    mfmbuf = allocmem(mfm_bytes);
    headers = allocmem(12 * sizeof(*headers));
    data = allocmem(12 * 512);

    drive_select(drv, 1);
    drive_check_ready(r);
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

    /* Start the test proper. Print option keys and instructions. */
    r->y--;
    sprintf(s, "$1 Re-Seek Current Cylinder$");
    print_line(r);
    r->y++;
    sprintf(s, "$2 Cylinder: %u$", cyl);
    print_line(r);
    r->y += 2;
    sprintf(s, "-> Use an AmigaDOS disk written by a well-calibrated drive.");
    print_line(r);
    r->y++;
    sprintf(s, "-> Adjust drive until 11 valid sectors found on both sides.");
    print_line(r);
    r->y += 5;
    sprintf(s, "    (.:okay X:missing -:cyl-low +:cyl-high)");
    print_line(r);
    r->y -= 3;

    seek_track(0);
    head = 0;

    for (;;) {
        key = keycode_buffer;
        done = (do_exit || (key == K_ESC));
        if (done)
            goto out;
        if (key) {
            keycode_buffer = 0;
            if (key == K_F2) {
                cyl = (cyl == 0) ? 40 : (cyl == 40) ? 79 : 0;
                r->y -= 5;
                sprintf(s, "$2 Cylinder: %u$", cyl);
                wait_bos();
                print_line(r);
                r->y += 5;
                key = K_F1; /* re-seek */
            }
            if (key == K_F1) {
                /* Step away from and back to cylinder 0. Useful after 
                 * stepper and cyl-0 sensor adjustments. */
                sprintf(s, "Seeking...");
                wait_bos();
                clear_text_rows(r->y+1, 1); /* clear side-1 text */
                print_line(r); /* overwrites side-0 text */
                seek_track(80);
                seek_cyl0();
                seek_track(cyl*2);
                head = 0;
                wait_bos();
                clear_text_rows(r->y, 1); /* clear seek text */
            }
        }
        /* Read and decode a full track of data. */
        ciab->prb |= CIABPRB_SIDE;
        if (head)
            ciab->prb &= ~CIABPRB_SIDE;
        memset(mfmbuf, 0, mfm_bytes);
        disk_read_track(mfmbuf, mfm_bytes);
        disk_wait_dma();
        nr_secs = mfm_decode_track(mfmbuf, headers, data, mfm_bytes);
        /* Default sector map is all X's (all sectors missing). */
        for (i = 0; i < 11; i++)
            map[i] = 'X';
        map[i] = '\0';
        /* Parse the sector headers, extract cyl# of each good sector. */
        while (nr_secs--) {
            struct sec_header *h = &headers[nr_secs];
            if ((h->format == 0xff) && !h->data_csum && (h->sec < 11))
                map[h->sec] = (((h->trk>>1) > cyl) ? '+' :
                               ((h->trk>>1) < cyl) ? '-' : '.');
        }
        /* Count the number of valid (for this cylinder) sectors found. */
        good = 0;
        for (i = 0; i < 11; i++) {
            if (map[i] == '.')
                good++;
        }
        /* Update status message. */
        if (head)
            r->y++;
        sprintf(s, "%c Side %u (%ser) Cyl.Nrs: %s (%u/11 okay)",
                progress_chars[(progress+(head?2:0))&3],
                head, head ? "Upp" : "Low", map, good);
        wait_bos();
        print_line(r);
        if (head) {
            r->y--;
            progress++;
        }
        head ^= 1;
    }

out:
    drive_select(drv, 0);
    drive_deselect();

    while (!done)
        done = (do_exit || keycode_buffer == K_ESC);
    keycode_buffer = 0;
}

void floppycheck(void)
{
    char s[80];
    struct char_row r = { .x = 8, .s = s }, _r;
    void *alloc_s;
    uint8_t key = 0xff;
    unsigned int i, drv = 0;
    int draw_floppy_ids = 1;

    print_menu_nav_line();

    while (!do_exit) {

        if (draw_floppy_ids) {
            r.y = 1;
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
        }

        draw_floppy_ids = 0;

        r.y = 7;
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
        r.y++;
        sprintf(s, "$8 Head Calibration Test$");
        print_line(&r);
        r.y -= 5;

        for (;;) {
            /* Grab a key */
            while (!do_exit && !(key = keycode_buffer))
                continue;
            keycode_buffer = 0;
            /* Handle exit conditions */
            do_exit |= (key == K_ESC); /* ESC = exit */
            if (do_exit)
                break;
            /* Check for keys F1-F8 only */
            key -= K_F1; /* Offsets from F1 */
            if (key >= 8)
                continue;
            /* F5-F8: handled outside this loop */
            if (key > 3)
                break;
            /* F1-F4: DF0-DF3 */
            drv = key;
            sprintf(s, "-- DF%u: Selected --", drv);
            print_line(&r);
        }

        if (do_exit)
            break;

        alloc_s = start_allocheap_arena();
        clear_text_rows(r.y, 6);
        _r = r;

        switch (key) {
        case 4: /* F5 */
            drv = drive_signal_test(drv, &_r);
            break;
        case 5: /* F6 */
            drive_read_test(drv, &_r);
            break;
        case 6: /* F7 */
            clear_text_rows(0, r.y);
            drive_write_test(drv, &_r);
            clear_text_rows(0, r.y);
            draw_floppy_ids = 1;
            break;
        case 7: /* F8 */
            clear_text_rows(0, r.y);
            drive_cal_test(drv, &_r);
            clear_text_rows(0, r.y);
            draw_floppy_ids = 1;
            break;
        }

        end_allocheap_arena(alloc_s);
        clear_text_rows(r.y, 6);
    }
}

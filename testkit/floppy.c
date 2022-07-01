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

#include "testkit.h"

unsigned int mfm_decode_track(void *mfmbuf, void *headers, void *data,
                              unsigned int mfm_bytes);
void mfm_encode_track(void *mfmbuf, unsigned int tracknr,
                      unsigned int mfm_bytes,
                      unsigned int nr_secs);

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
static void drive_check_ready(struct char_row *r, bool_t is_hd)
{
    uint32_t s = get_time(), e;
    unsigned int timeout = ms_to_ticks(is_hd ? 2000 : 1250);
    int ready;

    do {
        ready = !!(~ciaa->pra & CIAAPRA_RDY);
        e = get_time();
    } while (!ready && ((e - s) < timeout));

    if (ready) {
        char delaystr[10];
        e = get_time();
        ticktostr(e - s, delaystr);
        sprintf((char *)r->s,
                (e - s) < ms_to_ticks(1)
                ? "Drive READY too fast (%s): Gotek or modified PC drive?"
                : "Drive READY asserted %s after Motor On",
                delaystr);
    } else {
        sprintf((char *)r->s,
                "No READY signal: PC or Escom drive?");
    }

    print_line(r);
    r->y++;

    if (ready) {
        s = get_time();
        timeout = ms_to_ticks(10);
        do {
            ready = !!(~ciaa->pra & CIAAPRA_RDY);
            e = get_time();
        } while (ready && ((e - s) < timeout));
        if (!ready) {
            sprintf((char *)r->s,
                    "READY signal is oscillating: modified PC drive?");
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

static void disk_wait_dma(bool_t is_hd)
{
    unsigned int i, timeout = is_hd ? 1000 : 500;
    for (i = 0; i < timeout; i++) {
        if (cust->intreqr & INT_DSKBLK) /* dsk-blk-done? */
            break;
        delay_ms(1);
    }
    cust->dsklen = 0x4000; /* no more dma */
}

/* Amiga Drive IDs, from resources/disk.i.
 * These disagree in part with the Hardware Reference Manual, however it is
 * the HRM which is wrong (see discussion in Github issue #58). */
#define DRT_AMIGA      0x00000000
#define DRT_37422D2S   0x55555555
#define DRT_EMPTY      0xFFFFFFFF
#define DRT_150RPM     0xAAAAAAAA

static uint32_t _drive_id(unsigned int drv)
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
    return id;
}

/* Get drive id but handle Gotek's unsynchronised HD ID bitstream. 
 * We can see either 5555.... or AAAA.... */
static uint32_t drive_id(unsigned int drv)
{
    uint32_t id[3];
    id[0] = _drive_id(drv);
    id[1] = _drive_id(drv);
    ciab->prb &= ~(CIABPRB_SEL0 << drv);
    ciab->prb |= CIABPRB_SEL0 << drv;
    id[2] = _drive_id(drv);
    if (((id[0] != id[1]) || (id[1] != id[2]))
        && ((id[0] == 0x55555555) || (id[0] == 0xaaaaaaaa))
        && ((id[1] == id[0]) || (id[1] == ~id[0]))
        && ((id[2] == id[0]) || (id[2] == ~id[0]))) {
        id[0] = DRT_150RPM;
    }
    if ((id[0] == DRT_EMPTY) && (drv == 0))
        id[0] = DRT_AMIGA;
    return id[0];
}

static const char *drive_id_type(unsigned int drv, uint32_t id)
{
    return (id == 0) || ((id == ~0) && (drv == 0)) ? "DS-DD 80"
        : (id == 0x55555555) ? "DS-DD 40"
        : (id == 0xaaaaaaaa) ? "DS-HD 80"
        : (id == 0xffffffff) ? "No Drive" : "Unknown";
}

static unsigned int drive_signal_test(unsigned int drv, struct char_row *r)
{
    char *s = (char *)r->s;
    uint8_t motors = 0, pra, old_pra, prb, old_prb, key = 0;
    unsigned int i, old_disk_index_count;
    uint32_t rdy_delay, mtr_time;
    int rdy_changed;

    r->x = 6;

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
        old_prb = ciab->prb;
        mtr_time = get_time();
        rdy_delay = rdy_changed = 0;
        old_disk_index_count = disk_index_count = 0;
        key = 1; /* force print */

        while (!do_exit) {
            pra = ciaa->pra;
            prb = ciab->prb;
            if ((pra != old_pra) || key) {
                rdy_changed = !!((old_pra ^ pra) & CIAAPRA_RDY);
                if (rdy_changed)
                    rdy_delay = get_time() - mtr_time;
                sprintf(s, "Motors=(%c%c%c%c) CIAAPRA=0x%02x (%s %s %s %s)",
                        motors&(1u<<0) ? '0' : ' ',
                        motors&(1u<<1) ? '1' : ' ',
                        motors&(1u<<2) ? '2' : ' ',
                        motors&(1u<<3) ? '3' : ' ',
                        pra,
                        ~pra & CIAAPRA_CHNG ? "/CHG" : "",
                        ~pra & CIAAPRA_WPRO ? "/WPR" : "",
                        ~pra & CIAAPRA_TK0  ? "/TK0" : "",
                        ~pra & CIAAPRA_RDY  ? "/RDY" : "");
                wait_bos();
                print_line(r);
                old_pra = pra;
            }
            if ((prb != old_prb) || key) {
                r->y += 4;
                sprintf(s, "$7 /STEP=%c$  $8 /DIR=%c (%3s)$  "
                        "$9 /SIDE=%c (%s)$",
                        prb & CIABPRB_STEP ? 'H' : 'L',
                        prb & CIABPRB_DIR ? 'H' : 'L',
                        prb & CIABPRB_DIR ? "Out" : "In",
                        prb & CIABPRB_SIDE ? 'H' : 'L',
                        prb & CIABPRB_SIDE ? "0/Lower" : "1/Upper");
                wait_bos();
                print_line(r);
                r->y -= 4;
                old_prb = prb;
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
                wait_bos();
                print_line(r);
                r->y--;
                old_disk_index_count = disk_index_count;
                rdy_changed = 0;
            }
            if (!(key = keycode_buffer))
                continue;
            keycode_buffer = 0;
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
            } else if (key == 0x56) { /* F7 */
                ciab->prb ^= CIABPRB_STEP;
                key = 0; /* don't force print */
            } else if (key == 0x57) { /* F8 */
                ciab->prb ^= CIABPRB_DIR;
                key = 0; /* don't force print */
            } else if (key == 0x58) { /* F9 */
                ciab->prb ^= CIABPRB_SIDE;
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

#define RED   1<<0
#define GREEN 1<<2

#define RED_COL   0xf00
#define GREEN_COL 0x2c2

static uint16_t copper_read[] = {
    0x4407, 0xfffe,
    0x0180, 0x0ddd,
    0x4507, 0xfffe,
    0x0180, 0x0402,
    /* pin map with highlights */
    0x8b07, 0xfffe,
    0x0184, 0x0ddd, /* col02 = foreground */
    0x0186, 0x0ddd, /* col03 = foreground */
    0x018c, 0x0ddd, /* col06 = foreground */
    0x0182, RED_COL, /* col01 */
    0x0188, GREEN_COL, /* col04 */
    0xdb07, 0xfffe,
    /* normal video */
    0x0182, 0x0222, /* col01 = shadow */
    0x0184, 0x0ddd, /* col02 = foreground */
    0x0186, 0x0ddd, /* col03 = foreground */
    0x0188, 0x04c4, /* col04 = menu-option highlight */
    0x018a, 0x0222, /* col05 = shadow */
    0x018c, 0x0ddd, /* col06 = foreground */
    0x018e, 0x0ddd, /* col07 = foreground */
    0xf007, 0xfffe,
    0x0180, 0x0ddd,
    0xf107, 0xfffe,
    0x0180, 0x0103,
    0xffff, 0xfffe,
};

static void drive_read_test(unsigned int drv, struct char_row *r)
{
    char *s = (char *)r->s;
    void *mfmbuf, *data;
    struct sec_header *headers;
    unsigned int mfm_bytes = 13100, trk_secs = 11, nr_secs, nr_valid;
    int done = 0, retries;
    uint8_t trk = 0;
    uint32_t valid_map;
    uint16_t xstart = 144, xpos = xstart;
    uint8_t ystart = 70,  ypos = ystart;
    uint32_t id = drive_id(drv);
    bool_t is_hd = (id == DRT_150RPM);
    unsigned int nr_cyls = (id == 0x55555555) ? 40 : 80;

    r->x = r->y = 0;
    sprintf(s, "-- DF%u: %s Density %u-Cyl Read Test --", drv,
            is_hd ? "High" : "Double", nr_cyls);
    print_line(r);
    r->y += 2;

    copperlist_set(copper_read);

    if (is_hd) {
        trk_secs *= 2;
        mfm_bytes *= 2;
    }

    mfmbuf = allocmem(mfm_bytes);
    headers = allocmem((trk_secs+1) * sizeof(*headers));
    data = allocmem((trk_secs+1) * 512);

    drive_select(drv, 1);
    drive_check_ready(r, is_hd);

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

    r->y = 4;
    r->x = 4;
    sprintf(s, "------ Lower ------ | ------ Upper ------");
    print_line(r);
    r->y++;

    /* Move the heads full range to check/reset calibration. */
    seek_track(nr_cyls*2);
    seek_track(0);

    while (trk < (nr_cyls*2)) {

        if (!(trk % 20)) {
            sprintf(s, "%2u :", trk>>1);
            print_label(xstart-40, ypos, 1, s);
        }

        retries = 0;
        do {
            done = (do_exit || (keycode_buffer == K_ESC));
            if (done)
                goto out;
            seek_track(trk);
            /* Read and decode a full track of data. */
            memset(mfmbuf, 0, mfm_bytes);
            disk_read_track(mfmbuf, mfm_bytes);
            disk_wait_dma(is_hd);
            nr_secs = mfm_decode_track(mfmbuf, headers, data, mfm_bytes);
            valid_map = nr_valid = s[0] = 0;
            while (nr_secs--) {
                struct sec_header *h = &headers[nr_secs];
                if ((h->format == 0xff) && !h->data_csum
                    && (h->sec < trk_secs)) {
                    if (h->trk > trk) {
                        s[0] = '+';
                    } else if (h->trk < trk) {
                        s[0] = '-';
                    } else if (!(valid_map & 1u<<h->sec)) {
                        valid_map |= 1u<<h->sec;
                        nr_valid++;
                    }
                }
            }
            s[0] = s[0] ?: "MLKJIHGFEDCBA9876543210"[nr_valid+(is_hd?0:11)];
            s[1] = '\0';
            clear_rect(xpos+(trk&1)*176-3, ypos-1, 14, 10, 7);
            fill_rect(xpos+(trk&1)*176-3, ypos-1, 14, 9,
                      s[0] == '0' ? GREEN : RED);
            print_label(xpos+(trk&1)*176, ypos, 1, s);
        } while ((s[0] != '0') && (retries++ < 3));

        if (!(++trk & 1))
        {
            xpos += 16;
            if ((trk%20) == 0) {
                xpos = xstart;
                ypos += 10;
            }
        }
    }

out:
    drive_select(drv, 0);
    drive_deselect();

    while (!done)
        done = (do_exit || keycode_buffer == K_ESC);
    keycode_buffer = 0;
    copperlist_default();
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

static char *index_wait_to_str(uint32_t ms, bool_t is_hd)
{
    unsigned int base = is_hd ? 400 : 200;
    return ((ms < base-20) ? "Early" : (ms > base+20) ? "Late" : "OK");
}

static void drive_write_test(unsigned int drv, struct char_row *r)
{
    char *s = (char *)r->s, retrystr[20];
    void *mfmbuf;
    struct sec_header *headers;
    unsigned int i, j, mfm_bytes = 13100, trk_secs = 11, nr_secs;
    uint32_t erase_wait, write_wait, index_late_thresh;
    uint32_t valid_map;
    int done = 0, retries, late_indexes = 0;
    uint8_t rnd, *data;
    uint32_t id = drive_id(drv);
    bool_t is_hd = (id == DRT_150RPM);
    unsigned int nr_cyls = (id == 0x55555555) ? 40 : 80;

    r->x = r->y = 0;
    sprintf(s, "-- DF%u: %s Density %u-Cyl Write Test --", drv,
            is_hd ? "High" : "Double", nr_cyls);
    print_line(r);
    r->y += 2;

    if (is_hd) {
        trk_secs *= 2;
        mfm_bytes *= 2;
    }

    mfmbuf = allocmem(mfm_bytes);
    headers = allocmem((trk_secs+1) * sizeof(*headers));
    data = allocmem((trk_secs+1) * 512);

    drive_select(drv, 1);
    drive_check_ready(r, is_hd);
    r->y++;

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

    if (~ciaa->pra & CIAAPRA_WPRO) {
        sprintf(s, "WRPRO: Disk is Write Protected?");
        print_line(r);
        goto out;
    }

    for (i = (nr_cyls-1)*2; i < nr_cyls*2; i++) {

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
            disk_wait_dma(is_hd);

            /* write */
            mfm_encode_track(mfmbuf, i, mfm_bytes, trk_secs);
            erase_wait = wait_for_index();
            disk_write_track(mfmbuf, mfm_bytes);
            disk_wait_dma(is_hd);

            /* read */
            memset(mfmbuf, 0, mfm_bytes);
            write_wait = wait_for_index();
            disk_read_track(mfmbuf, mfm_bytes);
            disk_wait_dma(is_hd);

            /* verify */
            nr_secs = mfm_decode_track(mfmbuf, headers, data, mfm_bytes);
            valid_map = 0;

            /* Check sector headers */
            while (nr_secs--) {
                struct sec_header *h = &headers[nr_secs];
                if ((h->format = 0xff) && (h->trk == i) && !h->data_csum
                    && (h->sec < trk_secs))
                    valid_map |= 1u<<h->sec;
            }

            /* Check our verification token */
            for (j = 0; j < trk_secs*512; j++)
                if (data[j] != rnd)
                    valid_map = 0;

        } while (valid_map != (1u<<trk_secs)-1);

        sprintf(s, "Track %u written:", i);
        print_line(r);
        r->y++;
        sprintf(s, " - Erase To Index Pulse: %u ms (%s)", erase_wait,
                index_wait_to_str(erase_wait, is_hd));
        print_line(r);
        r->y++;
        sprintf(s, " - Write To Index Pulse: %u ms (%s)", write_wait,
                index_wait_to_str(write_wait, is_hd));
        print_line(r);
        r->y++;
        index_late_thresh = is_hd ? 420 : 220;
        if ((erase_wait > index_late_thresh)
            || (write_wait > index_late_thresh))
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
    int done = 0, cyl = 0;
    uint8_t key, good, progress = 0, head;
    char progress_chars[] = "|/-\\";
    uint32_t id = drive_id(drv);
    bool_t is_hd = (id == DRT_150RPM);
    int8_t headsel = -1;
    struct reseek {
        uint32_t last_time;
        uint32_t interval;
        int8_t sel;
        struct char_row r;
    } reseek = { .sel = 0 };
    const uint8_t reseek_options[] = { 0, 1, 2, 3, 5, 10, 30 };
    char reseek_str[10] = "Off";

    r->x = r->y = 0;
    sprintf(s, "-- DF%u: Continuous Head Calibration Test --", drv);
    print_line(r);
    r->y += 2;

    mfmbuf = allocmem(mfm_bytes);
    headers = allocmem(12 * sizeof(*headers));
    data = allocmem(12 * 512);

    drive_select(drv, 1);
    drive_wait_ready();
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

    if (is_hd) {
        sprintf(s, "HD disks are unsupported for head calibration.");
        print_line(r);
        r->y++;
        sprintf(s, "Please retry with a DD 880kB AmigaDOS disk.");
        print_line(r);
        goto out;
    }

    /* Start the test proper. Print option keys and instructions. */
    sprintf(s, "$1 Re-Seek Current Cylinder$");
    print_line(r);
    r->y++;
    sprintf(s, "Change Cyl: $2+40$  $3+10$  $4-10$  $5+1$  $6-1$");
    print_line(r);
    r->y++;
    reseek.r = *r;
    sprintf(s, "$7 Head(s): Both   $   $8 Auto re-seek: Off   $");
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
        if (reseek.sel && !key
            && (get_time() - reseek.last_time) > reseek.interval) {
            key = K_F1;
        }
        if (key) {
            enum {SEEK_NONE=0, SEEK_FAST, SEEK_SLOW} seek = SEEK_NONE;
            keycode_buffer = 0;
            if ((key >= K_F2) && (key <= K_F6)) {
                switch (key) {
                case K_F2: cyl += 40; break;
                case K_F3: cyl += 10; break;
                case K_F4: cyl -= 10; break;
                case K_F5: cyl +=  1; break;
                case K_F6: cyl -=  1; break;
                }
                if (cyl < 0) cyl += 80;
                if (cyl >= 80) cyl -= 80;
                seek = SEEK_FAST;
            }
            if (key == K_F7) {
                if (++headsel > 1) {
                    headsel = -1;
                    head = 0;
                }
                if (headsel >= 0) {
                    clear_text_rows(r->y+!headsel, 1);
                    head = headsel;
                }
            }
            if (key == K_F8) {
                if (++reseek.sel >= ARRAY_SIZE(reseek_options))
                    reseek.sel = 0;
                reseek.interval = ms_to_ticks(1000*reseek_options[reseek.sel]);
                reseek.last_time = get_time();
                if (reseek.sel)
                    sprintf(reseek_str, "%u sec", reseek_options[reseek.sel]);
                else
                    sprintf(reseek_str, "Off");
            }
            if ((key == K_F7) || (key == K_F8)) {
                sprintf(s, "$7 Head(s): %7s$   $8 Auto re-seek: %6s$",
                        headsel == -1 ? "Both" :
                        headsel == 0 ? "0/Lower" : "1/Upper",
                        reseek_str);
                wait_bos();
                print_line(&reseek.r);
            }
            if (key == K_F1)
                seek = SEEK_SLOW;
            if (seek != SEEK_NONE) {
                /* Step away from and back to cylinder 0. Useful after 
                 * stepper and cyl-0 sensor adjustments. */
                sprintf(s, "Seeking...");
                wait_bos();
                clear_text_rows(r->y+1, 1); /* clear side-1 text */
                print_line(r); /* overwrites side-0 text */
                if (seek == SEEK_SLOW) {
                    seek_track(79*2);
                    seek_cyl0();
                }
                seek_track(cyl*2);
                if (headsel == -1)
                    head = 0;
                wait_bos();
                clear_text_rows(r->y, 1); /* clear seek text */
                reseek.last_time = get_time();
            }
        }
        /* Read and decode a full track of data. */
        ciab->prb |= CIABPRB_SIDE;
        if (head)
            ciab->prb &= ~CIABPRB_SIDE;
        memset(mfmbuf, 0, mfm_bytes);
        disk_read_track(mfmbuf, mfm_bytes);
        disk_wait_dma(is_hd);
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
        r->y += head;
        sprintf(s, "%c Cyl %u Head %u (%ser): %s (%u/11 okay)",
                progress_chars[progress&3],
                cyl, head, head ? "Upp" : "Low", map, good);
        wait_bos();
        print_line(r);
        r->y -= head;
        if ((headsel >= 0) || ((head = !head) == 0))
            progress++;
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
                uint32_t id[3];
                char suffix[32];
                bool_t id_unstable;
                suffix[0] = '\0';
                id[0] = _drive_id(i);
                id[1] = _drive_id(i);
                ciab->prb &= ~(CIABPRB_SEL0 << i);
                ciab->prb |= CIABPRB_SEL0 << i;
                id[2] = _drive_id(i);
                id_unstable = ((id[0] != id[1]) || (id[1] != id[2]));
                if (id_unstable) {
                    sprintf(suffix, "ID unstable");
                    if (((id[0] == 0x55555555) || (id[0] == 0xaaaaaaaa))
                        && ((id[1] == id[0]) || (id[1] == ~id[0]))
                        && ((id[2] == id[0]) || (id[2] == ~id[0]))) {
                        id[0] = 0xaaaaaaaa;
                        sprintf(suffix+strlen(suffix), " (Gotek?)");
                    }
                }
                sprintf(s, "DF%u: %08X (%8s) %s", i, id[0],
                        drive_id_type(i, id[0]), suffix);
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

        while (!do_exit) {
            /* Grab a key */
            while (!do_exit && !(key = keycode_buffer))
                continue;
            keycode_buffer = 0;
            /* Handle exit conditions */
            if (key == K_ESC)
                do_exit = 1;
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
        clear_text_rows(r.y, 7);
        _r = r;

        switch (key) {
        case 4: /* F5 */
            drv = drive_signal_test(drv, &_r);
            break;
        case 5: /* F6 */
            clear_text_rows(0, r.y);
            drive_read_test(drv, &_r);
            clear_text_rows(0, r.y);
            draw_floppy_ids = 1;
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
        case 8: /* F9 */
            break;
        }

        end_allocheap_arena(alloc_s);
        clear_text_rows(r.y, 7);
    }
}

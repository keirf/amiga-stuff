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

#include "testkit.h"

static void get_cia_times(uint16_t *times)
{
    times[0] = get_ciatime(ciaa, ta);
    times[1] = get_ciatime(ciaa, tb);
    times[2] = get_ciatime(ciab, ta);
    times[3] = get_ciatime(ciab, tb);
}

static bool_t within_tolerance(uint32_t exp, uint32_t seen)
{
    uint32_t diff = seen > exp ? seen - exp : exp - seen;
    return (exp <= 100) ? diff <= 1 : diff <= div32(exp, 100);
}

static volatile unsigned int TIMER[4];
void ciaata_IRQ(void) { TIMER[0]++; }
void ciaatb_IRQ(void) { TIMER[1]++; }
void ciabta_IRQ(void) { TIMER[2]++; }
void ciabtb_IRQ(void) { TIMER[3]++; }

static volatile unsigned int TOD;
void ciaa_TOD_IRQ(void) { TOD++; }
void ciab_TOD_IRQ(void) { TOD++; }

/* Inform user we are waiting on vblank IRQs. This is useful if a test 
 * hangs due to hardware problems. */
static void print_wait(struct char_row *r, unsigned int vbls)
{
    char *s = (char *)r->s;
    sprintf(s, "Waiting %u VBLs...", vbls);
    print_line(r);
}

/* Test the TOD timer ticks at the expected rate. */
static bool_t test_TOD(
    struct char_row *r,
    volatile struct amiga_cia * const cia,
    unsigned int tod_ticks_per_vbl,
    char cia_ch)
{
    char *s = (char *)r->s;
    unsigned int vbl_ticks = 16;
    unsigned int tod_ticks = vbl_ticks * tod_ticks_per_vbl;
    uint32_t start = 0x652341, end;
    bool_t in_tol;

    print_wait(r, vbl_ticks);

    /* Wait for a vblank and reset the vblank counter. */
    vblank_count = 0;
    while (!vblank_count)
        continue;
    vblank_count = 0;

    /* Set the TOD timer. */
    cia->todhi = start >> 16;
    cia->todmid = start >> 8;
    cia->todlow = start;

    /* Wait for required number of VBL periods. */
    while (vblank_count < vbl_ticks)
        continue;

    /* Read final timer value. */
    end = cia->todhi;
    end <<= 8;
    end |= cia->todmid;
    end <<= 8;
    end |= cia->todlow;

    /* Subtract our initial settings. */
    end -= start;
    end &= 0xffffff;

    /* Check that TOD ticked at expected rate. */
    in_tol = within_tolerance(tod_ticks, end);
    sprintf(s, "CIA%c TOD expect %u ticks: %u -> %s",
            cia_ch, tod_ticks, end,
            in_tol ? "OK" : "FAIL");
    print_line(r);
    r->y++;

    return in_tol;
}

/* Test the TOD alarm IRQ fires when it should. */
static bool_t test_TOD_IRQ(
    struct char_row *r,
    volatile struct amiga_cia * const cia,
    unsigned int tod_ticks_per_vbl,
    char cia_ch)
{
    char *s = (char *)r->s;
    unsigned int vbl_ticks = 16;
    unsigned int tod_ticks = vbl_ticks * tod_ticks_per_vbl;
    uint32_t start = 0x775132, alarm = start + tod_ticks;
    bool_t in_tol = FALSE;

    print_wait(r, vbl_ticks);

    /* Set TOD to zero, so it cannot prematurely trigger the alarm. */
    cia->todhi = 0;
    cia->todmid = 0;
    cia->todlow = 0;

    /* Set the TOD alarm. */
    cia->crb |= CIACRB_ALARM;
    cia->todhi = alarm >> 16;
    cia->todmid = alarm >> 8;
    cia->todlow = alarm;
    cia->crb &= ~CIACRB_ALARM;
    cia->icr = CIAICR_SETCLR | CIAICR_TOD;

    /* Wait for a vblank and reset IRQ counters. */
    vblank_count = 0;
    while (!vblank_count)
        continue;
    vblank_count = 0;
    TOD = 0;

    /* Set up TOD a little after the vertical blank. */
    delay_ms(1);

    /* Set the TOD counter for real. */
    cia->todhi = start >> 16;
    cia->todmid = start >> 8;
    cia->todlow = start;

    /* Wait for a TOD alarm interrupt.  */
    while ((vblank_count < (vbl_ticks + 5)) && !TOD)
        continue;
    cia->icr = CIAICR_TOD;

    /* Did it happen? And at the expected time? */
    if (TOD == 0) {
        sprintf(s, "CIA%c TOD IRQ Failure!", cia_ch);
    } else {
        in_tol = within_tolerance(vbl_ticks, vblank_count);
        sprintf(s, "CIA%c TOD IRQ expected in %u VBLs: %u -> %s",
                cia_ch, vbl_ticks, vblank_count,
                in_tol ? "OK" : "FAIL");
    }

    print_line(r);
    r->y++;

    return in_tol;
}

static void cia_timer_test(void)
{
    char s[80];
    struct char_row r = { .x = 12, .y = 0, .s = s };
    uint16_t i, times[2][4];
    uint32_t exp, exp_irq, tot[4] = { 0 };
    unsigned int hsync_per_vbl = (vbl_hz == 50) ? 313 : 263;
    bool_t all_in_tol = TRUE, true_vbl_hz_is_good;
    uint32_t true_vbl_hz, x, y;

    sprintf(s, "-- CIA Timer Test --");
    print_line(&r);
    r.y += 2;
    r.x = 0;

    print_wait(&r, 100);

    /* Get CIA timestamps and reset IRQ counters at time of a VBL IRQ. */
    ciaa->icr = CIAICR_SETCLR | CIAICR_TIMER_A | CIAICR_TIMER_B;
    ciab->icr = CIAICR_SETCLR | CIAICR_TIMER_A | CIAICR_TIMER_B;
    vblank_count = 0;
    do {
        get_cia_times(times[0]);
    } while (!vblank_count);
    for (i = 0; i < 4; i++)
        TIMER[i] = 0;

    /* Wait for 10 more VBL periods and accumulate CIA ticks into 
     * an array of 32-bit counters (tot[]). */
    do {
        get_cia_times(times[1]);
        for (i = 0; i < 4; i++) {
            tot[i] += (uint16_t)(times[0][i] - times[1][i]);
            times[0][i] = times[1][i];
        }
    } while (vblank_count < 101);
    ciaa->icr = CIAICR_TIMER_A | CIAICR_TIMER_B;
    ciab->icr = CIAICR_TIMER_A | CIAICR_TIMER_B;

    x = div32(tot[1]+20, 40); /* x = ticks_per_vbl * 2.5 */
    true_vbl_hz = div32(cpu_hz*25+(x>>1), x); /* 250*ticks_per_sec / x */
    x = true_vbl_hz; /* true_vbl_hz = 100 * ticks_per_sec / ticks_per_vbl */
    y = do_div(x, 100);
    true_vbl_hz_is_good = ((x >= (vbl_hz - 5)) && (x <= (vbl_hz + 5)));
    sprintf(s, "Detected VBlank frequency is %u.%02uHz -> %s", x, y,
            true_vbl_hz_is_good ? "OK" : "FAIL");
    print_line(&r);
    r.y++;
    if (!true_vbl_hz_is_good)
        true_vbl_hz = vbl_hz * 100;

    exp = div32(cpu_hz*10*50, true_vbl_hz>>1);
    exp_irq = exp >> 16;
    sprintf(s, "Expect %u Ticks and %u-%u IRQs during 100 VBLs:",
            exp, exp_irq, exp_irq+1);
    print_line(&r);
    r.y++;

    /* Print the actual tick values and whether they are within 
     * one-percent tolerance (which is pretty generous). */
    for (i = 0; i < 4; i++) {
        static const char *name[] = { "ATA", "ATB", "BTA", "BTB" };
        bool_t in_tol = within_tolerance(exp, tot[i]);
        sprintf(s, "CIA%s %u -> %s; %u IRQs -> %s", name[i], tot[i],
                in_tol ? "OK" : "FAIL", TIMER[i],
                within_tolerance(exp_irq, TIMER[i]) ? "OK" : "FAIL");
        print_line(&r);
        r.y++;
        if (!in_tol)
            all_in_tol = FALSE;
    }

    /* Test the TOD timers. */
    if (!test_TOD(&r, ciaa, 1, 'A'))
        all_in_tol = FALSE;
    if (!test_TOD(&r, ciab, hsync_per_vbl, 'B'))
        all_in_tol = FALSE;

    /* Test the TOD alarm IRQs. */
    if (!test_TOD_IRQ(&r, ciaa, 1, 'A'))
        all_in_tol = FALSE;
    if (!test_TOD_IRQ(&r, ciab, hsync_per_vbl, 'B'))
        all_in_tol = FALSE;

    sprintf(s, all_in_tol ? "** ALL TESTS PASSED **"
            : "** SOME FAILURES (values >1%% from expected) **");
    print_line(&r);
    r.y++;

    while (!do_exit && (keycode_buffer != K_ESC))
        continue;
}

static void cia_port_test(void)
{
    const static char *info[4][9] = {
        { "CIAA Port A (Gameport, Floppy, LED)",
          "$1 CIAA.PA7$ /FIR1",
          "$2 CIAA.PA6$ /FIR0",
          "$3 CIAA.PA5$ /RDY ",
          "$4 CIAA.PA4$ /TRK0",
          "$5 CIAA.PA3$ /WPRO",
          "$6 CIAA.PA2$ /CHNG",
          "$7 CIAA.PA1$ /LED ",
          "$8 CIAA.PA0$ /OVL " },
        { "CIAA Port B (Parallel Port Data)",
          "$1 CIAA.PB7$  D7",
          "$2 CIAA.PB6$  D6",
          "$3 CIAA.PB5$  D5",
          "$4 CIAA.PB4$  D4",
          "$5 CIAA.PB3$  D3",
          "$6 CIAA.PB2$  D2",
          "$7 CIAA.PB1$  D1",
          "$8 CIAA.PB0$  D0" },
        { "CIAB Port A (Serial/Parallel Control)",
          "$1 CIAB.PA7$ /DTR",
          "$2 CIAB.PA6$ /RTS",
          "$3 CIAB.PA5$ /CD ",
          "$4 CIAB.PA4$ /CTS",
          "$5 CIAB.PA3$ /DSR",
          "$6 CIAB.PA2$ SEL",
          "$7 CIAB.PA1$ POUT",
          "$8 CIAB.PA0$ BUSY" },
        { "CIAB Port B (Floppy Control)",
          "$1 CIAB.PB7$ /MTR",
          "$2 CIAB.PB6$ /SEL3",
          "$3 CIAB.PB5$ /SEL2",
          "$4 CIAB.PB4$ /SEL1",
          "$5 CIAB.PB3$ /SEL0",
          "$6 CIAB.PB2$ /SIDE",
          "$7 CIAB.PB1$ DIR",
          "$8 CIAB.PB0$ /STEP" }
    };

    const static char *mode[4] = { "INPUT", "OUT.0", "OUT.1", "OUT.~" };

    struct port {
        volatile uint8_t *ddrp, *prp;
        uint8_t orig_ddr, orig_pr, ddr, out, alt_mask;
        enum { P_IN, P_OUT0, P_OUT1, P_OUT_A } mode[8];
    } ports[4], *port;
    char s[80];
    struct char_row r = { .x = 12, .y = 0, .s = s };
    unsigned int prev_vblank_count, i, j, port_nr = 0;
    uint8_t alt = 0, key = 0, in, prev_in;

    ports[0].ddrp = &ciaa->ddra;
    ports[1].ddrp = &ciaa->ddrb;
    ports[2].ddrp = &ciab->ddra;
    ports[3].ddrp = &ciab->ddrb;
    ports[0].prp = &ciaa->pra;
    ports[1].prp = &ciaa->prb;
    ports[2].prp = &ciab->pra;
    ports[3].prp = &ciab->prb;

    for (i = 0; i < 4; i++) {
        port = &ports[i];
        port->ddr = port->orig_ddr = *port->ddrp;
        port->out = port->orig_pr = *port->prp;
        port->alt_mask = 0;
        for (j = 0; j < 8; j++)
            port->mode[j] = !(port->ddr & (1u<<j)) ? P_IN
                : (port->out & (1u<<j)) ? P_OUT1 : P_OUT0;
    }

    sprintf(s, "-- CIA Port Test --");
    print_line(&r);

    r.x = 0;
    r.y = 3;
    sprintf(s, "WARNING: This test allows arbitrary configuration");
    print_line(&r);
    r.x += 6;
    r.y++;
    sprintf(s, "of CIA port pins. This may damage hardware");
    print_line(&r);
    r.y++;
    sprintf(s, "or crash the system if pins are in use.");
    print_line(&r);
    r.x += 4;
    r.y += 2;
    sprintf(s, "$2 Continue, With Care$");
    print_line(&r);

    while (key != K_F2) {
        key = keycode_buffer;
        if (key)
            keycode_buffer = 0;
        if (do_exit || (key == K_ESC))
            return;
    }

    clear_text_rows(3, 7);

    r.x = 9;
    r.y = 2;
    sprintf(s, "-Pin-   -Name-  -Mode-  -Val-");
    print_line(&r);

    r.x = 4;
    r.y = 12;
    sprintf(s, "$9 Prev Port$    $0 Next Port$");
    print_line(&r);

    prev_vblank_count = vblank_count;

    /* Main loop. */
    while (!do_exit && (key != K_ESC)) {

        /* Port changed. */
        port = &ports[port_nr];

        /* Print the menu / table for the new port. */
        r.x = 4;
        r.y = 1;
        for (i = 0; i < 9; i++) {
            r.s = info[port_nr][i];
            print_line(&r);
            r.y = i ? r.y+1 : 3;
        }

        /* Refresh all pin modes and values for the new port. */
        in = prev_in = *port->prp;
        for (i = 0; i < 8; i++) {
            print_text_box(25, 10-i, mode[port->mode[i]]);
            print_text_box(35, 10-i, !(in & (1u<<i)) ? "0" : "1");
        }

        /* Processing loop: Continues until exit or port change. */
        while (!do_exit && (key != K_ESC)) {

            /* Wait for key, meanwhile regularly update pins and screen. */
            while (!do_exit && !(key = keycode_buffer)) {

                /* We update outputs and sample inputs only once per vbl. */
                if (vblank_count == prev_vblank_count)
                    continue;
                prev_vblank_count = vblank_count;

                /* Toggle alternating outputs. */
                alt ^= 1;
                for (i = 0; i < 4; i++) {
                    port = &ports[i];
                    port->out = alt
                        ? port->out | port->alt_mask
                        : port->out & ~port->alt_mask;
                    *port->prp = port->out;
                }

                /* Give *lots* of time for the pins to settle. */
                delay_ms(1);

                /* Sample the pin values and update the screen. */
                port = &ports[port_nr];
                in = *port->prp;
                prev_in ^= in;
                for (i = 0; i < 8; i++) {
                    if (!(prev_in & (1u<<i)))
                        continue;
                    print_text_box(35, 10-i, !(in & (1u<<i)) ? "0" : "1");
                }
                prev_in = in;
            }

            /* Key pressed: Process it. */
            keycode_buffer = 0;
            if ((key >= K_F1) && (key <= K_F8)) {
                /* F1-F8: Change mode of corresponding port pin. */
                i = K_F8 - key;
                port->mode[i]++;
                port->mode[i] &= 3;
                print_text_box(25, 10-i, mode[port->mode[i]]);
                switch (port->mode[i]) {
                case P_IN: /* Input mode */
                    port->alt_mask &= ~(1u<<i);
                    *port->ddrp = port->ddr &= ~(1u<<i);
                    break;
                case P_OUT0: /* Output 0 */
                    *port->prp = port->out &= ~(1u<<i);
                    *port->ddrp = port->ddr |= 1u<<i;
                    break;
                case P_OUT1: /* Output 1 */
                    *port->prp = port->out |= 1u<<i;
                    break;
                case P_OUT_A: /* Alternating/toggling output */
                    port->alt_mask |= 1u<<i;
                    break;
                }
            } else if (key == K_F9) {
                /* F9: Previous port */
                port_nr = (port_nr - 1) & 3;
                break;
            } else if (key == K_F10) {
                /* F10: Next port */
                port_nr = (port_nr + 1) & 3;
                break;
            }
        }
    }

    /* Clean up. Put ports back as they were. */
    for (i = 0; i < 4; i++) {
        port = &ports[i];
        *port->prp = port->orig_pr;
        *port->ddrp = port->orig_ddr;
    }
}

void ciacheck(void)
{
    char s[80];
    struct char_row r = { .s = s };
    uint8_t key = 0;
    uint16_t lisaid, aliceid;
    unsigned int _spurious_autovector_total = 0;

    print_menu_nav_line();

    while (!do_exit) {
        r.x = 12;
        r.y = 0;
        sprintf(s, "-- CIA, Chipset --");
        print_line(&r);

        r.x = 7;
        r.y = 3;
        sprintf(s, "$1 CIA Precision Timers$");
        print_line(&r);
        r.y++;
        sprintf(s, "$2 CIA Peripheral Ports$");
        print_line(&r);

        /* Chipset IDs. */
        r.y += 2;
        sprintf(s, "-- Chipset IDs --");
        print_line(&r);
        r.y++;
        lisaid = cust->deniseid;
        sprintf(s, "Denise/Lisa: %04x", lisaid);
        print_line(&r);
        r.y++;
        aliceid = (cust->vposr >> 8) & 0x7f;
        sprintf(s, "Agnus/Alice: %04x", aliceid);
        print_line(&r);
        if (/* Detect AGA Alice IDs: 0x22, 0x23, 0x32, 0x33. */
            ((aliceid & 0x6e) == 0x22)
            /* In which case LisaID bits 3,8,9 should be zero. */
            && !!(lisaid & 0x304)) {
            sprintf(s, "WARNING: AGA Alice with bad Lisa ID");
            r.y++;
            print_line(&r);
        }

        r.y += 2;

        do {
            while (!do_exit && !(key = keycode_buffer)) {
                if (_spurious_autovector_total == spurious_autovector_total)
                    continue;
                _spurious_autovector_total = spurious_autovector_total;
                sprintf(s, "Spurious IRQs/NMIs: %u",
                        _spurious_autovector_total);
                wait_bos();
                print_line(&r);
            }
            keycode_buffer = 0;
            if (key == K_ESC)
                do_exit = 1;
            key -= K_F1;
        } while (!do_exit && (key >= 2));

        if (do_exit)
            break;
 
        clear_text_rows(0, 13);
        switch (key) {
        case 0:
            cia_timer_test();
            break;
        case 1:
            cia_port_test();
            break;
        }
        clear_text_rows(0, 13);
        keycode_buffer = 0;
    }
}

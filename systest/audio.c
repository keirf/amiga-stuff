/*
 * audio.c
 * 
 * Test standard Amiga audio hardware.
 * 
 * Written & released by Keir Fraser <keir.xen@gmail.com>
 * 
 * This is free and unencumbered software released into the public domain.
 * See the file COPYING for more details, or visit <http://unlicense.org>.
 */

#include "systest.h"

void audiocheck(void)
{
    char s[80];
    struct char_row r = { .s = s };
    static const uint8_t sine[] = { 0,19,39,57,74,89,102,113,120,125,127 };
    const unsigned int nr_500hz_samples = 40;
    const unsigned int nr_10khz_samples = 2;
    int8_t *aud_500hz = allocmem(nr_500hz_samples);
    int8_t *aud_10khz = allocmem(nr_10khz_samples);
    uint8_t key, channels = 0, lowfreq = 1;
    uint32_t period;
    unsigned int i;

    /* Low-pass filter activated by default. */
    ciaa->pra &= ~CIAAPRA_LED;

    /* Generate the 500Hz waveform. */
    for (i = 0; i < 10; i++) {
        aud_500hz[i] = sine[i];
        aud_500hz[10+i] = sine[10-i];
        aud_500hz[20+i] = -sine[i];
        aud_500hz[30+i] = -sine[10-i];
    }

    /* Generate the 10kHz waveform. */
    aud_10khz[0] = 127;
    aud_10khz[1] = -127;

    print_menu_nav_line();

    r.x = 12;
    sprintf(s, "-- Audio Test --");
    print_line(&r);
    r.y += 2;
    r.x = 8;

    sprintf(s, "$1 Channel 0/L$  -  OFF");
    print_line(&r);
    r.y++;
    sprintf(s, "$2 Channel 1/R$  -  OFF");
    print_line(&r);
    r.y++;
    sprintf(s, "$3 Channel 2/R$  -  OFF");
    print_line(&r);
    r.y++;
    sprintf(s, "$4 Channel 3/L$  -  OFF");
    print_line(&r);
    r.y++;
    sprintf(s, "$5 Frequency  $  -  500Hz Sine");
    print_line(&r);
    r.y++;
    sprintf(s, "$6 L.P. Filter$  -  ON");
    print_line(&r);
    r.y += 2;

    /* period = cpu_hz / (2 * nr_samples * frequency) */
    period = div32(div32(div32(cpu_hz, 2), nr_500hz_samples), 500/*Hz*/);

    for (i = 0; i < 4; i++) {
        cust->aud[i].lc.p = aud_500hz;
        cust->aud[i].len = nr_500hz_samples / 2;
        cust->aud[i].per = (uint16_t)period;
        cust->aud[i].vol = 0;
    }
    cust->dmacon = DMA_SETCLR | DMA_AUDxEN; /* dma on */

    for (;;) {
        while (!(key = keycode_buffer) && !do_exit)
            continue;
        keycode_buffer = 0;

        /* ESC also means exit */
        if (do_exit || (key == K_ESC))
            break;

        key -= K_F1;
        if (key < 4) {
            /* F1-F4: Switch channel 0-3 */
            channels ^= 1u << key;
            cust->aud[key].vol = (channels & (1u << key)) ? 64 : 0;
            print_text_box(29, 2+key, channels & (1u<<key) ? "N " : "FF");
        } else if (key == 4) {
            /* F5: Frequency */
            lowfreq ^= 1;
            cust->dmacon = DMA_AUDxEN; /* dma off */
            for (i = 0; i < 4; i++) {
                /* NB. programmed period does not change: sample lengths 
                 * determine the frequency. */
                if (lowfreq) {
                    cust->aud[i].lc.p = aud_500hz;
                    cust->aud[i].len = nr_500hz_samples / 2;
                } else {
                    cust->aud[i].lc.p = aud_10khz;
                    cust->aud[i].len = nr_10khz_samples / 2;
                }
            }
            cust->dmacon = DMA_SETCLR | DMA_AUDxEN; /* dma on */
            print_text_box(28, 6, lowfreq ? "500Hz Sine  " : "10kHz Square");
        } else if (key == 5) {
            /* F6: Low Pass Filter */
            ciaa->pra ^= CIAAPRA_LED;
            print_text_box(29, 7, (ciaa->pra & CIAAPRA_LED) ? "FF" : "N ");
        }
    }

    /* Clean up. */
    for (i = 0; i < 4; i++)
        cust->aud[i].vol = 0;
    cust->dmacon = DMA_AUDxEN; /* dma off */
    ciaa->pra &= ~CIAAPRA_LED;
}

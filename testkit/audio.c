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

#include "testkit.h"
#include "ptplayer/wrapper.c"

extern uint8_t mod[];
enum { SOUND_mod, SOUND_low, SOUND_high, SOUND_max };

static void mod_start(uint8_t channels)
{
    mt_install_cia(is_pal);
    mt_init(mod, 0);
    mt_disablemask(~channels);
    _mt_Enable = 1;
}

static void mod_stop(void)
{
    mt_end();
    mt_remove_cia();
    ciab_init();
}

static void tone_stop(void)
{
    unsigned int i;
    for (i = 0; i < 4; i++)
        cust->aud[i].vol = 0;
    cust->dmacon = DMA_AUDxEN; /* dma off */
}

static void tone_toggle(uint8_t sound, uint8_t channels, unsigned int i)
{
    if (!(channels & (1u << i)))
        cust->aud[i].vol = 0;
    else if (sound != SOUND_mod)
        cust->aud[i].vol = 64;
    print_text_box(29, 2+i, channels & (1u<<i) ? "N " : "FF");
}

void audiocheck(void)
{
    char s[80];
    struct char_row r = { .s = s };
    static const uint8_t sine[] = { 0,19,39,57,74,89,102,113,120,125,127 };
    const unsigned int nr_500hz_samples = 40;
    const unsigned int nr_10khz_samples = 2;
    int8_t *aud_500hz = allocmem(nr_500hz_samples);
    int8_t *aud_10khz = allocmem(nr_10khz_samples);
    uint8_t key, channels = 0xf, sound = SOUND_mod;
    uint32_t period;
    unsigned int i;

    /* Low-pass filter deactivated by default. */
    ciaa->pra |= CIAAPRA_LED;

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

    r.x = 14;
    sprintf(s, "-- Audio --");
    print_line(&r);
    r.y += 2;
    r.x = 8;

    sprintf(s, "$1 Channel 0/L$  -  ON");
    print_line(&r);
    r.y++;
    sprintf(s, "$2 Channel 1/R$  -  ON");
    print_line(&r);
    r.y++;
    sprintf(s, "$3 Channel 2/R$  -  ON");
    print_line(&r);
    r.y++;
    sprintf(s, "$4 Channel 3/L$  -  ON");
    print_line(&r);
    r.y++;
    sprintf(s, "$5 Sound      $  -  Music");
    print_line(&r);
    r.y++;
    sprintf(s, "$6 L.P. Filter$  -  OFF");
    print_line(&r);
    r.y++;
    sprintf(s, "$7 All Channels On/Off$");
    print_line(&r);
    r.y += 2;
    r.x -= 3;
    sprintf(s, "Music: \"Spice It Up\" by Jester/Sanity");
    print_line(&r);
    r.y++;
    sprintf(s, "Protracker Play Routine by Frank Wille");
    print_line(&r);

    /* period = cpu_hz / (2 * nr_samples * frequency) */
    period = div32(div32(div32(cpu_hz, 2), nr_500hz_samples), 500/*Hz*/);

    mod_start(channels);

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
            mt_disablemask(~channels);
            tone_toggle(sound, channels, key);
        } else if (key == 4) {
            /* F5: Sound */
            if (sound == SOUND_mod)
                mod_stop();
            tone_stop();
            if (++sound == SOUND_max)
                sound = 0;
            switch (sound) {
            case SOUND_mod:
                mod_start(channels);
                print_text_box(28, 6, "Music       ");
                break;
            case SOUND_low:
                for (i = 0; i < 4; i++) {
                    cust->aud[i].lc.p = aud_500hz;
                    cust->aud[i].len = nr_500hz_samples / 2;
                    cust->aud[i].per = (uint16_t)period;
                    cust->aud[i].vol = (channels & (1u << i)) ? 64 : 0;
                }
                cust->dmacon = DMA_SETCLR | DMA_AUDxEN; /* dma on */
                print_text_box(28, 6, "500Hz Sine  ");
                break;
            case SOUND_high:
                for (i = 0; i < 4; i++) {
                    cust->aud[i].lc.p = aud_10khz;
                    cust->aud[i].len = nr_10khz_samples / 2;
                    cust->aud[i].per = (uint16_t)period;
                    cust->aud[i].vol = (channels & (1u << i)) ? 64 : 0;
                }
                cust->dmacon = DMA_SETCLR | DMA_AUDxEN; /* dma on */
                print_text_box(28, 6, "10kHz Square");
                break;
            }
        } else if (key == 5) {
            /* F6: Low Pass Filter */
            ciaa->pra ^= CIAAPRA_LED;
            print_text_box(29, 7, (ciaa->pra & CIAAPRA_LED) ? "FF" : "N ");
        } else if (key == 6) {
            /* F7: All Channels On/Off */
            channels = channels ? 0 : 0xf;
            mt_disablemask(~channels);
            for (i = 0; i < 4; i++)
                tone_toggle(sound, channels, i);
        }
    }

    /* Clean up. */
    if (sound == SOUND_mod)
        mod_stop();
    tone_stop();
    ciaa->pra &= ~CIAAPRA_LED;
}

asm (
"    .data                          \n"
"mod: .incbin \"ptplayer/spice.mod\"\n"
"    .text                          \n"
);

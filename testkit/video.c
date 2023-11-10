/*
 * video.c
 * 
 * Video test pattern.
 * 
 * Written & released by Keir Fraser <keir.xen@gmail.com>
 * 
 * This is free and unencumbered software released into the public domain.
 * See the file COPYING for more details, or visit <http://unlicense.org>.
 */

#include "testkit.h"

static void gradients(void)
{
    uint16_t *p, *cop, wait;
    unsigned int i, j, k;
    uint32_t bpl3;

    /* This test has two uses: 
     * (1) It show a colour gradient for each of R, G, B components. 
     *     This will show up any errors that affect a particular component, 
     *     including any bit failures in any of {R,G,B}[3:0]. 
     * (2) Boundary lines are drawn for standard NTSC and PAL playfields. 
     *     These are useful for calibrating the TV or monitor. */

    cop = p = allocmem(16384 /* plenty */);

    /* First line of gradient. */
    wait = 0x4401;

    /* Top border black. */
    *p++ = 0x0180;
    *p++ = 0x0000;

    /* Reduce to 2-plane depth as otherwise copper timings are slowed.  */
    *p++ = 0x0100; /* bplcon0 */
    *p++ = 0xa200; /* hires, 2 planes */

    /* Adjust bitplane 3 start for when we re-enable third bitplane at 
     * bottom of screen. */
    bpl3 = (uint32_t)bpl[2];
    bpl3 += 158 * xres/8;
    *p++ = 0x00e8; /* bpl3pth */
    *p++ = bpl3 >> 16;
    *p++ = 0x00ea; /* bpl3ptl */
    *p++ = bpl3;

    /* Change diwstrt/diwstop for standard PAL display. Repeat the same
     * bitplane line repeatedly at top section of display to achieve the
     * left/right boundary lines. */
    *p++ = 0x008e; *p++ = 0x2c81; /* diwstrt */
    *p++ = 0x0090; *p++ = 0x2cc1; /* diwstop */
    *p++ = 0x0108; *p++ = -(xres/8); /* bpl1mod: same line repeatedly */
    *p++ = 0x010a; *p++ = -(xres/8); /* bpl2mod: same line repeatedly */

    /* Horizontal line at the top of the normal (NTSC or PAL) playfield. */
    *p++ = 0x2c41; *p++ = 0xfffe;
    *p++ = 0x0180; *p++ = 0x0ddd;
    *p++ = 0x2cdd; *p++ = 0xfffe;
    *p++ = 0x0180; *p++ = 0x0000;

    /* Create a vertical gradient of red, green, blue (across screen). 
     * Alternate white/black markers to indicate gradient changes. */
    for (i = 0; i < 16; i++) {
        for (j = 0; j < 10; j++) {
            /* WAIT line */
            *p++ = wait | 0x06;
            *p++ = 0xfffe;
            /* color = black or white */
            *p++ = 0x0180;
            *p++ = (i & 1) ? 0x0000 : 0x0fff;

            /* At our normal playfield start position un-kludge the 
             * bitplane modulos. */
            if (wait == ((diwstrt_v<<8)|1)) {
                *p++ = 0x0108; *p++ = 0; /* bpl1mod */
                *p++ = 0x010a; *p++ = 0; /* bpl2mod */
            }

            for (k = 0; k < 4; k++) {
                /* WAIT horizontal */
                *p++ = wait | (0x50 + k*0x28);
                *p++ = 0xfffe;
                /* color = black or white */
                *p++ = 0x0180;
                *p++ = (i & 1) ? 0x0000 : 0x0fff;
                if (k == 3) break;
                /* color = red, green or blue gradients */
                *p++ = 0x0180;
                *p++ = i << (8 - k*4);
            }

            wait += 1<<8;
        }
    }

    /* WAIT line, then re-enable third bitplane. */
    *p++ = wait | 0x06;
    *p++ = 0xfffe;
    *p++ = 0x0100; /* bplcon0 */
    *p++ = 0xb200; /* hires, 3 planes */

    /* End of our normal playfield: re-kludge the bitplane modulos to 
     * achieve the left/right playfield boundary lines. */
    *p++ = ((diwstrt_v+yres) << 8) | 7; *p++ = 0xfffe;
    *p++ = 0x0100; *p++ = 0x9200; /* bplcon0: 1 bitplane */
    *p++ = 0x0182; *p++ = 0x0ddd; /* color[1]: light grey */
    *p++ = 0x0108; *p++ = -(xres/8); /* bpl1mod: same line repeatedly */

    /* Horizontal line at bottom of normal NTSC playfield.  */
    *p++ = 0xf341; *p++ = 0xfffe;
    *p++ = 0x0180; *p++ = 0x0ddd;
    *p++ = 0xf3dd; *p++ = 0xfffe;
    *p++ = 0x0180; *p++ = 0x0000;

    /* Horizontal line at bottom of normal PAL playfield. */
    *p++ = 0xffdf; *p++ = 0xfffe;
    *p++ = 0x2b41; *p++ = 0xfffe;
    *p++ = 0x0180; *p++ = 0x0ddd;
    *p++ = 0x2bdd; *p++ = 0xfffe;
    *p++ = 0x0180; *p++ = 0x0000;

    /* End of copper list. */
    *p++ = 0xffff; *p++ = 0xfffe;

    copperlist_set(cop);

    print_menu_nav_line();

    /* Left/right boundary lines for a normal (NTSC or PAL) playfield. */
    fill_rect(0, 0, 10, yres, 3);
    fill_rect(xres-10, 0, 10, yres, 3);

    /* All work is done by the copper. Just wait for exit. */
    while (!do_exit && (keycode_buffer != K_ESC))
        continue;
}

static void checkerboard(bool_t alternating)
{
    uint32_t *b, pattern;
    uint16_t *p, *cop;
    unsigned int i, j;

    /* This test shows a per-pixel black-and-white checkerboard for
     * calibrating video scalers such as OSSC. */

    cop = p = allocmem(16384 /* plenty */);

    /* Black background, B&W checker pattern (colours 02 & 03). */
    *p++ = 0x0180; *p++ = 0x0000;
    *p++ = 0x0184; *p++ = 0x0000;
    *p++ = 0x0186; *p++ = 0x0fff;

    /* Colours back to normal for navigation text. */
    *p++ = 0xe407; *p++ = 0xfffe;
    *p++ = 0x0184; *p++ = 0x0ddd;
    *p++ = 0x0186; *p++ = 0x0ddd;

    /* Plane 2 is filled. Plane 1 alternates (checker pattern). */
    fill_rect(0, 0, xres, yres-11, 2);
    b = (uint32_t *)bpl[0];
    pattern = 0xaaaaaaaa;
    for (i = 0; i < yres-11; i++) {
        pattern = ~pattern;
        for (j = 0; j < xres/32; j++)
            *b++ = pattern;
    }

    /* End of copper list. */
    *p++ = 0xffff; *p++ = 0xfffe;

    copperlist_set(cop);

    /* Wait for exit; alternate checker pattern each frame (if requested). */
    pattern = 0;
    while (!do_exit && (keycode_buffer != K_ESC)) {
        if (!alternating)
            continue;
        wait_bos();
        cop[3] = pattern;
        cop[5] = pattern = ~pattern;
    }
}

static void uniform(int colour_index)
{
    uint16_t *p, *cop;

    const uint16_t colours[] = {
        0x0000, 0x0fff, 0x0f00, 0x00f0, 0x000f
    };

    /* This test shows full-screen solid colours, for adjusting purity on CRT
     * monitors and spotting dead pixels on LCDs. */

    cop = p = allocmem(16384 /* plenty */);

    /* Turn off all bitplanes, which also hides the pointer sprite. */
    *p++ = 0x0100; *p++ = 0x0200;

    /* Background colour. */
    *p++ = 0x0180;
    *p++ = colours[colour_index];

    /* End of copper list. */
    *p++ = 0xffff; *p++ = 0xfffe;

    copperlist_set(cop);

    /* Any key exits. */
    while (!do_exit && (keycode_buffer == 0))
        continue;
}

static void grid(bool_t crosshatch)
{
    const int width = 384;
    const int height = 284;
    const int line_size = width / 8;
    const int bitplane_size = line_size * height;
    const int vpitch = 20;
    const int hpitch = vpitch;

    uint16_t *p, *cop;
    uint8_t *bitplane;
    uint16_t y, x;

    /* This test shows full-screen dots or crosshatch, for adjusting
     * convergence and linearity on CRT monitors. */

    bitplane = allocmem(bitplane_size);

    /* Draw the pattern. */
    memset(bitplane, 0, bitplane_size);
    if (crosshatch) {
        for (y = 0; y < height; y++) {
            uint8_t *line = &bitplane[line_size * y];
            for (x = hpitch / 2; x < width; x += hpitch)
                line[x / 8] |= 0x80 >> (x % 8);
        }
        for (y = vpitch / 2; y < height; y += vpitch) {
            memset(&bitplane[line_size * y], 0xff, line_size);
        }
    } else {
        for (y = vpitch / 2; y < height; y += vpitch) {
            uint8_t *line = &bitplane[line_size * y];
            for (x = hpitch / 2; x < width; x += hpitch)
                line[x / 8] |= 0x80 >> (x % 8);
        }
    }

    cop = p = allocmem(16384 /* plenty */);

    /* Disable sprite DMA to hide the pointer sprite. */
    *p++ = 0x0096; *p++ = DMA_SPREN;

    /* Black background, white foreground. */
    *p++ = 0x0180; *p++ = 0x0000;
    *p++ = 0x0182; *p++ = 0x0fff;

    /* Low res, one bitplane. */
    *p++ = 0x0100; *p++ = 0x1200;

    /* Bitplane 1 data. */
    *p++ = 0x00e0; *p++ = ((uint32_t) bitplane) >> 16;
    *p++ = 0x00e2; *p++ = ((uint32_t) bitplane) & 0xffff;

    /* Display window with maximum overscan. */
    *p++ = 0x008e; *p++ = 0x1b51;
    *p++ = 0x0090; *p++ = 0x37d1;

    /* Data fetch start and stop. */
    *p++ = 0x0092; *p++ = 0x0020;
    *p++ = 0x0094; *p++ = 0x00d8;

    /* End of copper list. */
    *p++ = 0xffff; *p++ = 0xfffe;

    copperlist_set(cop);

    /* Any key exits. */
    while (!do_exit && (keycode_buffer == 0))
        continue;

    /* Reenable sprite DMA. */
    copperlist_default();
    wait_bos();
    cust->dmacon = DMA_SETCLR | DMA_SPREN;
}

/* This test shows full-screen EBU-style colour bars, for adjusting hue and
 * checking colour rendition generally. */
static void ebu_bars(int index)
{
    uint16_t *p, *cop;
    uint16_t y, i;

    const uint16_t ebu_100_0_100_0[] = {
        0x0fff, 0x0ff0, 0x00ff, 0x00f0, 0x0f0f, 0x0f00, 0x000f, 0x0000,
    };
    const uint16_t ebu_100_0_75_0[] = {
        0x0fff, 0x0bb0, 0x00bb, 0x00b0, 0x0b0b, 0x0b00, 0x000b, 0x0000,
    };
    const uint16_t ebu_75_0_75_0[] = {
        0x0bbb, 0x0bb0, 0x00bb, 0x00b0, 0x0b0b, 0x0b00, 0x000b, 0x0000,
    };
    const uint16_t greyscale[] = {
        0x0fff, 0x0ddd, 0x0bbb, 0x0999, 0x0777, 0x0555, 0x0333, 0x0111,
    };
    const uint16_t *colours = NULL;

    switch (index) {
    case 0: colours = ebu_100_0_100_0; break;
    case 1: colours = ebu_100_0_75_0; break;
    case 2: colours = ebu_75_0_75_0; break;
    case 3: colours = greyscale; break;
    }

    cop = p = allocmem(16384 /* plenty */);

    /* Turn off all bitplanes, which also hides the pointer sprite. */
    *p++ = 0x0100; *p++ = 0x0200;

    for (y = 1; y < 313; y++) {
        /* Bar 0 starts immediately after hsync. */
        uint16_t x = 3;

        for (i = 0; i < 8; i++) {
            /* Wait for the next bar and set the colour. */
            *p++ = ((y & 0xff) << 8) | (x << 1) | 1; *p++ = 0xfffe;
            *p++ = 0x0180; *p++ = colours[i];

            /* Space bars 1-7 evenly across a non-overscanned display. */
            if (i == 0)
                x = 26;
            x += 11;

            /* Before y wraps back to 0, wait for the end of the line. */
            if (y == 0xff && i == 7) {
                *p++ = 0xffdf; *p++ = 0xfffe;
            }
        }
    }

    /* End of copper list. */
    *p++ = 0xffff; *p++ = 0xfffe;

    copperlist_set(cop);

    /* Any key exits. */
    while (!do_exit && (keycode_buffer == 0))
        continue;
}

static void full_field_tests(void)
{
    char s[80];
    struct char_row r = { .s = s };
    void *alloc_s;
    uint8_t key = 0;

    while (!do_exit) {

        print_menu_nav_line();

        r.x = 5;
        r.y = 0;
        sprintf(s, "-- Full-Field: Uniform & Bars --");
        print_line(&r);

        r.x = 3;
        r.y = 2;
        sprintf(s, "$1 Black$");
        print_line(&r);
        r.y++;
        sprintf(s, "$2 White$");
        print_line(&r);
        r.y++;
        sprintf(s, "$3 Red$");
        print_line(&r);
        r.y++;
        sprintf(s, "$4 Green$");
        print_line(&r);
        r.y++;
        sprintf(s, "$5 Blue$");
        print_line(&r);
        r.y += 2;
        sprintf(s, "$6 EBU 100/0/100/0 Bars (100%% colour)$");
        print_line(&r);
        r.y++;
        sprintf(s, "$7 EBU 100/0/75/0 Bars (75%% colour)$");
        print_line(&r);
        r.y++;
        sprintf(s, "$8 EBU 75/0/75/0 Bars (75%% variant)$");
        print_line(&r);
        r.y++;
        sprintf(s, "$9 Greyscale Bars$");
        print_line(&r);

        do {
            while (!do_exit && !(key = keycode_buffer))
                continue;
            keycode_buffer = 0;
            if (key == K_ESC)
                break;
            key -= K_F1;
        } while (!do_exit && (key >= 9));

        if (do_exit || (key == K_ESC))
            break;

        alloc_s = start_allocheap_arena();
        clear_text_rows(0, 13);
        switch (key) {
        case 0 ... 4:
            uniform(key);
            break;
        case 5 ... 8:
            ebu_bars(key-5);
            break;
        }
        clear_whole_screen();
        keycode_buffer = 0;
        copperlist_default();
        end_allocheap_arena(alloc_s);

    }
}

void videocheck(void)
{
    char s[80];
    struct char_row r = { .s = s };
    void *alloc_s;
    uint8_t key = 0;

    while (!do_exit) {

        print_menu_nav_line();

        r.x = 16;
        r.y = 0;
        sprintf(s, "-- Video --");
        print_line(&r);

        r.x = 3;
        r.y = 3;
        sprintf(s, "$1 RGB gradients & PAL/NTSC extents$");
        print_line(&r);
        r.y++;
        sprintf(s, "$2 Pixel checkerboard (static)$");
        print_line(&r);
        r.y++;
        sprintf(s, "$3 Pixel checkerboard (alternating)$");
        print_line(&r);

        r.y += 2;

        sprintf(s, "Full-field tests (any key exits each test):");
        print_line(&r);
        r.y++;
        sprintf(s, "$4 Uniform & Bars$");
        print_line(&r);
        r.y++;
        sprintf(s, "$5 Dots (convergence)$");
        print_line(&r);
        r.y++;
        sprintf(s, "$6 Crosshatch (linearity)$");
        print_line(&r);
        r.y++;

        r.y += 2;

        do {
            while (!do_exit && !(key = keycode_buffer))
                continue;
            keycode_buffer = 0;
            if (key == K_ESC)
                do_exit = 1;
            key -= K_F1;
        } while (!do_exit && (key >= 6));

        if (do_exit)
            break;

        alloc_s = start_allocheap_arena();
        clear_text_rows(0, 13);
        switch (key) {
        case 0:
            gradients();
            break;
        case 1:
            checkerboard(FALSE);
            break;
        case 2:
            checkerboard(TRUE);
            break;
        case 3:
            full_field_tests();
            break;
        case 4:
            grid(FALSE);
            break;
        case 5:
            grid(TRUE);
            break;
        }
        clear_whole_screen();
        keycode_buffer = 0;
        copperlist_default();
        end_allocheap_arena(alloc_s);

    }
}

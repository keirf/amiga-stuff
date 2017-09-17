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

#include "systest.h"

void videocheck(void)
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
    *p++ = 0xffff;
    *p++ = 0xfffe;

    copperlist_set(cop);

    print_menu_nav_line();

    /* Left/right boundary lines for a normal (NTSC or PAL) playfield. */
    fill_rect(0, 0, 10, yres, 3);
    fill_rect(xres-10, 0, 10, yres, 3);

    /* All work is done by the copper. Just wait for exit. */
    while (!do_exit && (keycode_buffer != K_ESC))
        continue;
}

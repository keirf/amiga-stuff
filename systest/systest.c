/*
 * systest.c
 * 
 * System Tests:
 *  - Slow/Ranger RAM detection and check.
 *  - Keyboard test.
 *  - Disk test.
 * 
 * Written & released by Keir Fraser <keir.xen@gmail.com>
 * 
 * This is free and unencumbered software released into the public domain.
 * See the file COPYING for more details, or visit <http://unlicense.org>.
 */

static volatile struct amiga_custom * const cust =
    (struct amiga_custom *)0xdff000;
static volatile struct amiga_cia * const ciaa =
    (struct amiga_cia *)0x0bfe001;
static volatile struct amiga_cia * const ciab =
    (struct amiga_cia *)0x0bfd000;

/* Space for bitplanes and unpacked font. */
extern char GRAPHICS[];

#define IRQ(name)                               \
static void c_##name(void) attribute_used;      \
void name(void);                                \
asm (                                           \
#name":                             \n"         \
"    movem.l %d0-%d1/%a0-%a1,-(%sp) \n"         \
"    bsr c_"#name"                  \n"         \
"    movem.l (%sp)+,%d0-%d1/%a0-%a1 \n"         \
"    rte                            \n"         \
)

#define xres    640
#define yres    169
#define bplsz   (yres*xres/8)
#define planes  2

#define xstart  110
#define ystart  20
#define yperline 10

static volatile uint8_t keycode_buffer, exit;
static uint8_t *bpl[2];
static uint16_t *font;

static uint16_t copper[] = {
    0x008e, 0x4681, /* diwstrt.v = 0x46 */
    0x0090, 0xefc1, /* diwstop.v = 0xef (169 lines) */
    0x0092, 0x003c, /* ddfstrt */
    0x0094, 0x00d4, /* ddfstop */
    0x0100, 0xa200, /* bplcon0 */
    0x0102, 0x0000, /* bplcon1 */
    0x0104, 0x0000, /* bplcon2 */
    0x0108, 0x0000, /* bpl1mod */
    0x010a, 0x0000, /* bpl2mod */
    0x00e0, 0x0000, /* bpl1pth */
    0x00e2, 0x0000, /* bpl1ptl */
    0x00e4, 0x0000, /* bpl2pth */
    0x00e6, 0x0000, /* bpl2ptl */
    0x0182, 0x0222, /* col01 */
    0x0184, 0x0ddd, /* col02 */
    0x0186, 0x0ddd, /* col03 */
    0x0180, 0x0103, /* col00 */
    0x4401, 0xfffe,
    0x0180, 0x0ddd,
    0x4501, 0xfffe,
    0x0180, 0x0402,
    0xf001, 0xfffe,
    0x0180, 0x0ddd,
    0xf101, 0xfffe,
    0x0180, 0x0103,
    0xffff, 0xfffe,
};

struct char_row {
    uint8_t x, y;
    const char *s;
};

/* Wait for blitter idle. */
static void waitblit(void)
{
    while (*(volatile uint8_t *)&cust->dmaconr & (1u << 6))
        continue;
}

/* Wait for end of bitplane DMA. */
static void wait_bos(void)
{
    while (*(volatile uint8_t *)&cust->vhposr != 0xf0)
        continue;
}

/* Wait for beam to move to next scanline. */
static void wait_line(void)
{
    uint8_t v = *(volatile uint8_t *)&cust->vhposr;
    while (*(volatile uint8_t *)&cust->vhposr == v)
        continue;
}

static void clear_screen_rows(uint16_t y_start, uint16_t y_nr)
{
    waitblit();
    cust->bltcon0 = 0x0100;
    cust->bltcon1 = 0x0000;
    cust->bltdpt.p = bpl[0] + y_start * (xres/8);
    cust->bltdmod = 0;
    cust->bltsize = (xres/16)|(y_nr<<6);
    waitblit();
    cust->bltdpt.p = bpl[1] + y_start * (xres/8);
    cust->bltsize = (xres/16)|(y_nr<<6);
    waitblit();
}

static void clear_colors(void)
{
    uint16_t i;
    for (i = 0; i < 16; i++)
        cust->color[i] = 0;
}

/* Unpack 8*8 font into destination.
 * Each char 00..7f is copied in sequence.
 * Destination is 10 longwords (= 10 rows) per character.
 * First word of each long is foreground, second word is background.
 * Background is computed from foreground. */
extern uint8_t packfont[];
static void unpack_font(void)
{
    uint8_t *p = packfont;
    uint16_t i, j, x, *q = font;

    /* First 0x20 chars are blank. */
    memset(q, 0, 0x20 * yperline * 4);
    q += 0x20 * yperline * 2;

    for (i = 0x20; i < 0x80; i++) {
        /* Foreground char is shifted right one pixel. */
        q[0] = 0;
        for (j = 1; j < yperline-1; j++)
            q[2*j] = (uint16_t)(*p++) << 7; 
        q[2*j] = 0;

        /* OR the neighbouring rows into each background pixel. */
        q[1] = q[0] | q[2];
        for (j = 1; j < yperline-1; j++)
            q[2*j+1] = q[2*(j-1)] | q[2*j] | q[2*(j+1)];
        q[2*j+1] = q[2*(j-1)] | q[2*j];

        /* OR the neighbouring columns into each background pixel. */
        for (j = 0; j < yperline; j++) {
            x = q[2*j+1];
            q[2*j+1] = (x << 1) | x | (x >> 1);
        }

        q += yperline * 2;
    }
}

static void print_char(uint16_t x, uint16_t y, char c)
{
    uint16_t *font_char = font + c * yperline * 2;
    uint16_t i, bpl_off = y * (xres/8) + (x/16) * 2;

    waitblit();

    cust->bltcon0 = 0x0dfc | ((x&15)<<12); /* ABD DMA, D = A|B, pre-shift A */
    cust->bltcon1 = 0;
    cust->bltamod = 0;
    cust->bltbmod = xres/8 - 4;
    cust->bltdmod = xres/8 - 4;
    cust->bltafwm = 0xffff;
    cust->bltalwm = 0x0000;
    
    for (i = 0; i < 2; i++) {
        waitblit();
        cust->bltapt.p = font_char + (i==0);
        cust->bltbpt.p = bpl[i] + bpl_off;
        cust->bltdpt.p = bpl[i] + bpl_off;
        cust->bltsize = 2 | (yperline<<6); /* 2 words * yperline rows */
    }
}

static void print_line(const struct char_row *r)
{
    uint16_t x = xstart + r->x * 8;
    uint16_t y = ystart + r->y * yperline;
    const char *p = r->s;
    char c;
    clear_screen_rows(y, yperline);
    while ((c = *p++) != '\0') {
        print_char(x, y, c);
        x += 8;
    }
}

static unsigned int menu(void)
{
    uint8_t key;
    char s[80];
    struct char_row r = { .x = 4, .y = 0, .s = s };

    clear_screen_rows(0, yres);
    keycode_buffer = 0;

    sprintf(s, "SysTest - by KAF <keir.xen@gmail.com>");
    print_line(&r);
    r.y++;
    sprintf(s, "------------------------------------");
    print_line(&r);
    r.y++;
    sprintf(s, "F1 - Ranger RAM (0.5MB Slow RAM Expansion)");
    print_line(&r);
    r.y++;
    sprintf(s, "F2 - Keyboard");
    print_line(&r);
    r.y++;
    sprintf(s, "F3 - Floppy Drive");
    print_line(&r);
    r.y++;
    sprintf(s, "F4 - Mouse / Joystick Ports");
    print_line(&r);
    r.y++;
    sprintf(s, "F5 - Audio");
    print_line(&r);
    r.y++;
    sprintf(s, "(ESC + F1 to quit back to menu)");
    print_line(&r);
    r.y++;
    sprintf(s, "------------------------------------");
    print_line(&r);
    r.y++;
    sprintf(s, "https://github.com/keirf/Amiga-Stuff");
    print_line(&r);
    r.y++;
    sprintf(s, "build: %s %s", __DATE__, __TIME__);
    print_line(&r);
    r.y++;

    while ((key = keycode_buffer - 0x50) >= 5)
        continue;
    clear_screen_rows(0, yres);
    keycode_buffer = 0;
    exit = 0;
    return key;
}

static void memcheck(void)
{
    volatile uint16_t *p;
    volatile uint16_t *start = (volatile uint16_t *)0xc00000;
    volatile uint16_t *end = (volatile uint16_t *)0xc80000;
    char s[80];
    struct char_row r = { .x = 8, .y = 5, .s = s };
    uint16_t a, b, i, j;

    i = cust->intenar;

    /* If slow memory is absent then custom registers alias at C00000. We 
     * detect this by writing to what would be INTENA and checking for changes 
     * to what would be INTENAR. If we see no change then we are not writing 
     * to the custom registers and _EXRAM must be asserted at Gary. */
    p = start;
    p[0x9a/2] = 0x7fff; /* clear all bits in INTENA */
    a = p[0x1c/2];
    p[0x9a/2] = 0xbfff; /* set all bits in INTENA except master enable */
    b = p[0x1c/2];

    sprintf(s, "%04x / %04x", a, b);
    print_line(&r);
    r.y++;

    sprintf(s, "Ranger RAM%s detected", (a != b) ? " *NOT*" : "");
    print_line(&r);
    r.y++;

    if (a != b) {
        cust->intena = 0x7fff;
        cust->intena = 0x8000 | i;
        while (!exit)
            continue;
        return;
    }

    /* We believe we have slow memory present. Now check the RAM for errors. 
     * This uses an inversions algorithm where we try to set an alternating 
     * 0/1 pattern in each memory cell (assuming 1-bit DRAM chips). */
    a = b = 0;
    while (!exit) {
        /* Start with all 0s. Write 1s to even words. */
        for (p = start; p != end;) {
            *p++ = 0x0000;
        }
        if (exit) break;
        for (p = start; p != end;) {
            *p++ = 0xffff;
            p++;
        }
        if (exit) break;
        for (p = start; p != end;) {
            a |= ~*p++;
            a |= *p++;
        }
        if (exit) break;

        /* Start with all 0s. Write 1s to odd words. */
        for (p = start; p != end;) {
            *p++ = 0x0000;
        }
        if (exit) break;
        for (p = start; p != end;) {
            p++;
            *p++ = 0xffff;
        }
        if (exit) break;
        for (p = start; p != end;) {
            a |= *p++;
            a |= ~*p++;
        }
        if (exit) break;

        /* Start with all 1s. Write 0s to even words. */
        for (p = start; p != end;) {
            *p++ = 0xffff;
        }
        if (exit) break;
        for (p = start; p != end;) {
            *p++ = 0x0000;
            p++;
        }
        if (exit) break;
        for (p = start; p != end;) {
            a |= *p++;
            a |= ~*p++;
        }
        if (exit) break;

        /* Start with all 1s. Write 0s to odd words. */
        for (p = start; p != end;) {
            *p++ = 0xffff;
        }
        if (exit) break;
        for (p = start; p != end;) {
            p++;
            *p++ = 0x0000;
        }
        if (exit) break;
        for (p = start; p != end;) {
            a |= ~*p++;
            a |= *p++;
        }
        if (exit) break;

        b++;
        if ((ciaa->pra & 0xc0) != 0xc0) {
            while (r.y >= 5) {
                sprintf(s, "");
                print_line(&r);
                r.y--;
            }
            r.y = 5;
            for (i = j = 0; i < 16; i++)
                if ((a >> i) & 1)
                    j++;
            sprintf(s, "After %u rounds: errors in %u bit positions", b, j);
            print_line(&r);
            r.y++;
            if (j != 0) {
                char num[8];
                sprintf(s, " -> Bits ");
                for (i = 0; i < 16; i++) {
                    if (!((a >> i) & 1))
                        continue;
                    sprintf(num, "%u", i);
                    if (--j)
                        strcat(num, ",");
                    strcat(s, num);
                }
                print_line(&r);
                r.y++;
            }
            a = b = 0;
            continue;
        }

        sprintf(s, "After %u rounds: errors %04x", b, a);
        print_line(&r);
    }
}

static void kbdcheck(void)
{
    char s[80], num[5];
    struct char_row r = { .x = 8, .y = 1, .s = s };
    unsigned int i = 0;

    sprintf(s, "Keyboard Test:");
    print_line(&r);
    r.y++;

    s[0] = '\0';
    keycode_buffer = 0x7f;
    while (!exit) {
        uint8_t key = keycode_buffer;
        if (key == 0x7f)
            continue;
        keycode_buffer = 0x7f;
        if (r.y == 13) {
            while (r.y >= 2) {
                sprintf(s, "");
                print_line(&r);
                r.y--;
            }
            r.y = 2;
        }
        sprintf(num, "%02x ", key);
        strcat(s, num);
        print_line(&r);
        if (i++ == 8) {
            i = 0;
            s[0] = '\0';
            r.y++;
        }
    }
}

static void motor(int on)
{
    ciab->prb |= 0xf8;
    if (on)
        ciab->prb &= 0x7f;
    ciab->prb &= 0xf7;
    wait_line();
}

static void floppycheck(void)
{
    char s[80];
    struct char_row r = { .x = 8, .y = 1, .s = s };
    uint8_t pra, old_pra;
    int on = 0, frames = 0;

    motor(on);
    pra = ciaa->pra;

    sprintf(s, "-- DF0: Test --");
    print_line(&r);
    r.y++;

    while (!exit) {
        sprintf(s, " CIAAPRA=0x%02x: CHG=%u WPR=%u TK0=%u RDY=%u",
                pra, !!(pra&4), !!(pra&8), !!(pra&16), !!(pra&32));
        print_line(&r);
        old_pra = pra;
        while (((pra = ciaa->pra) == old_pra) && !exit) {
            wait_line();
            if (frames++ == 250*250) {
                frames = 0;
                on = !on;
                motor(on);
            }
        }
    }

    /* Clean up. */
    motor(0);
}

static void drawpixel(unsigned int x, unsigned int y, int set)
{
    uint16_t bpl_off = y * (xres/8) + (x/8);
    if (!set)
        bpl[1][bpl_off] &= ~(0x80 >> (x & 7));
    else
        bpl[1][bpl_off] |= 0x80 >> (x & 7);
}

static void drawbox(unsigned int x, unsigned int y, int set)
{
    unsigned int i, j;
    for (i = 0; i < 2; i++)
        for (j = 0; j < 2; j++)
            drawpixel(x+i, y+j, set);
}

static void printport(char *s, unsigned int port)
{
    uint16_t joy = port ? cust->joy1dat : cust->joy0dat;
    uint8_t buttons = cust->potinp >> (port ? 12 : 8);
    sprintf(s, "Button=(%c%c%c) Dir=(%c%c%c%c)",
            !(ciaa->pra & (1u << (port ? 7 : 6))) ? '1' : ' ',
            !(buttons & 1) ? '2' : ' ',
            !(buttons & 4) ? '3' : ' ',
            (joy & 0x200) ? 'L' : ' ',
            (joy & 2) ? 'R' : ' ',
            ((joy & 0x100) ^ ((joy & 0x200) >> 1)) ? 'U' : ' ',
            ((joy & 1) ^ ((joy & 2) >> 1)) ? 'D' : ' ');
}

static void updatecoords(uint16_t oldjoydat, uint16_t newjoydat, int *coords)
{
    coords[0] += (int8_t)(newjoydat - oldjoydat);
    coords[1] += (int8_t)((newjoydat >> 8) - (oldjoydat >> 8));
    coords[0] = min_t(int, max_t(int, coords[0], 0), 139);
    coords[1] = min_t(int, max_t(int, coords[1], 0), 139);
}

static void joymousecheck(void)
{
    char sub[2][30], s[80];
    struct char_row r = { .x = 8, .y = 1, .s = s };
    int i, coords[2][2] = { { 0 } };
    uint16_t joydat[2], newjoydat[2];

    joydat[0] = cust->joy0dat;
    joydat[1] = cust->joy1dat;

    /* Pull-ups on button 2 & 3 inputs. */
    cust->potgo = 0xff00;

    sprintf(s, "-- Joy / Mouse Test --");
    print_line(&r);
    r.x = 0;
    r.y += 3;

    for (i = 0; !exit; i++) {

        if (i & 1) {
            /* Odd frames: print button/direction info */
            printport(sub[0], 0);
            printport(sub[1], 1);
            r.y++;
        } else {
            /* Even frames: print coordinate info */
            sprintf(sub[0], "Port 1: (%3u,%3u)", coords[0][0], coords[0][1]);
            sprintf(sub[1], "Port 2: (%3u,%3u)", coords[1][0], coords[1][1]);
            r.y--;
        }
        sprintf(s, "%37s%s", sub[0], sub[1]);

        newjoydat[0] = cust->joy0dat;
        newjoydat[1] = cust->joy1dat;

        /* Wait for end of display, then start screen updates. */
        wait_bos();
        print_line(&r); /* sloooow, allows only one per vbl */
        drawbox(coords[0][0] + 100, coords[0][1]/2 + 80, 0);
        drawbox(coords[1][0] + 400, coords[1][1]/2 + 80, 0);
        updatecoords(joydat[0], newjoydat[0], coords[0]);
        updatecoords(joydat[1], newjoydat[1], coords[1]);
        drawbox(coords[0][0] + 100, coords[0][1]/2 + 80, 1);
        drawbox(coords[1][0] + 400, coords[1][1]/2 + 80, 1);

        joydat[0] = newjoydat[0];
        joydat[1] = newjoydat[1];
    }

    /* Clean up. */
    cust->potgo = 0x0000;
}

static void audiocheck(void)
{
    char s[80];
    struct char_row r = { .x = 8, .y = 3, .s = s };
    static const uint8_t sine[] = { 0,19,39,57,74,89,102,113,120,125,127 };
    static int8_t aud[40] __attribute__((__aligned__(2)));
    uint8_t key, channels = 0;
    unsigned int i;

    for (i = 0; i < 10; i++) {
        aud[i] = sine[i];
        aud[10+i] = sine[10-i];
        aud[20+i] = -sine[i];
        aud[30+i] = -sine[10-i];
    }

    sprintf(s, "-- 500Hz Sine Wave Audio Test --");
    print_line(&r);
    r.x += 7;
    r.y++;
    sprintf(s, "F1 - Channel 0 (L)");
    print_line(&r);
    r.y++;
    sprintf(s, "F2 - Channel 1 (R)");
    print_line(&r);
    r.y++;
    sprintf(s, "F3 - Channel 2 (R)");
    print_line(&r);
    r.y++;
    sprintf(s, "F4 - Channel 3 (L)");
    print_line(&r);

    for (i = 0; i < 4; i++) {
        cust->aud[i].lc.p = aud;
        cust->aud[i].len = sizeof(aud)/2;
        cust->aud[i].per = 3546895 / (sizeof(aud) * 500/*HZ*/); /* PAL */
        cust->aud[i].vol = 0;
    }
    cust->dmacon = 0x800f; /* all audio channels */

    r.x -= 2;
    r.y += 2;

    while (!exit) {
        sprintf(s, "0=%s 1=%s 2=%s 3=%s",
                (channels & 1) ? "ON " : "OFF",
                (channels & 2) ? "ON " : "OFF",
                (channels & 4) ? "ON " : "OFF",
                (channels & 8) ? "ON " : "OFF");
        wait_bos();
        print_line(&r);

        while ((key = keycode_buffer - 0x50) >= 4)
            continue;
        keycode_buffer = 0;
        channels ^= 1u << key;
        cust->aud[key].vol = (channels & (1u << key)) ? 64 : 0;
    }

    /* Clean up. */
    for (i = 0; i < 4; i++)
        cust->aud[i].vol = 0;
    cust->dmacon = 0x000f;
}

IRQ(CIA_IRQ);
static void c_CIA_IRQ(void)
{
    uint16_t i;
    uint8_t icr = ciaa->icr;
    static uint8_t prev_key;

    if (icr & (1u<<3)) { /* SDR finished a byte? */
        /* Grab and decode the keycode. */
        uint8_t keycode = ~ciaa->sdr;
        keycode_buffer = (keycode >> 1) | (keycode << 7); /* ROR 1 */
        if ((prev_key == 0x45) && (keycode_buffer == 0x50))
            exit = 1; /* ESC + F1 */
        prev_key = keycode_buffer;
        /* Handshake over the serial line. */
        ciaa->cra |= 1u<<6; /* start the handshake */
        for (i = 0; i < 3; i++) /* wait ~100us */
            wait_line();
        ciaa->cra &= ~(1u<<6); /* finish the handshake */
    }

    /* NB. Clear intreq.ciaa *after* reading/clearing ciaa.icr else we get a 
     * spurious extra interrupt, since intreq.ciaa latches the level of CIAA 
     * INT and hence would simply become set again immediately after we clear 
     * it. For this same reason (latches level not edge) it is *not* racey to 
     * clear intreq.ciaa second. Indeed AmigaOS does the same (checked 
     * Kickstart 3.1). */
    cust->intreq = 1u<<3;
}

void cstart(void)
{
    uint16_t i;
    char *p;

    /* Set up CIAA ICR. We only care about keyboard. */
    ciaa->icr = 0x7f;
    ciaa->icr = 0x88;

    /* Enable blitter DMA. */
    cust->dmacon = 0x8040;

    /* Clear BSS. */
    memset(_sbss, 0, _ebss-_sbss);

    /* Bitplanes and unpacked font allocated as directed by linker. */
    p = GRAPHICS;
    bpl[0] = (uint8_t *)p; p += bplsz;
    bpl[1] = (uint8_t *)p; p += bplsz;
    font = (uint16_t *)p;

    /* Poke bitplane addresses into the copper. */
    for (i = 0; copper[i] != 0x00e0/*bpl1pth*/; i++)
        continue;
    copper[i+1] = (uint32_t)bpl[0] >> 16;
    copper[i+3] = (uint32_t)bpl[0];
    copper[i+5] = (uint32_t)bpl[1] >> 16;
    copper[i+7] = (uint32_t)bpl[1];
    
    /* Clear bitplanes. */
    clear_screen_rows(0, yres);

    clear_colors();

    unpack_font();

    *(volatile void **)0x68 = CIA_IRQ;
    cust->cop1lc.p = copper;

    wait_bos();
    cust->dmacon = 0x81c0; /* enable copper/bitplane/blitter DMA */
    cust->intena = 0x8008; /* enable CIA-A interrupts */

    for (;;) {
        switch (menu()) {
        case 0:
            memcheck();
            break;
        case 1:
            kbdcheck();
            break;
        case 2:
            floppycheck();
            break;
        case 3:
            joymousecheck();
            break;
        case 4:
            audiocheck();
            break;
        }
    }
}

asm (
"    .data                          \n"
"packfont: .incbin \"../base/font.raw\"\n"
"    .text                          \n"
);

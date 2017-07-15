/*
 * overscan.c
 * 
 * Overscan display example for screen setup.
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

#define IRQ(name)                               \
static void c_##name(void) attribute_used;      \
void name(void);                                \
asm (                                           \
#name":                             \n"         \
"    movem.l %d0-%d1/%a0-%a1,-(%sp) \n"         \
"    jbsr c_"#name"                 \n"         \
"    movem.l (%sp)+,%d0-%d1/%a0-%a1 \n"         \
"    rte                            \n"         \
)

/* Bog standard PAL 320x256 lo-res display. */
static uint16_t copper_normal[] = {
    0x008e, 0x2c81, /* diwstrt */
    0x0090, 0x2cc1, /* diwstop */
    0x0092, 0x0038, /* ddfstrt */
    0x0094, 0x00d0, /* ddfstop */
    0x0100, 0x2200, /* bplcon0 */
    0x0102, 0x0000, /* bplcon1 */
    0x0104, 0x0000, /* bplcon2 */
    0x0108, 0x0000, /* bpl1mod */
    0x010a, 0x0000, /* bpl2mod */
    0x00e0, 0x0002, /* bpl1pth */
    0x00e2, 0x0000, /* bpl1ptl */
    0x00e4, 0x0002, /* bpl2pth */
    0x00e6, 0x4000, /* bpl2ptl */
    0x0182, 0x00f0, /* col01 */
    0x0184, 0x000f, /* col02 */
    0x0186, 0x0f0f, /* col03 */
    0x0180, 0x0f00, /* col00 */
    0xffff, 0xfffe,
};

/* Overscan values taken from Rink a Dink: Redux. 352x272 resolution.
 * Left border: 16px overscan 
 * Right border: 16px overscan 
 * Top border: 12px overscan 
 * Bottom border: 4px overscan (ungenerous? but okay on my lcd tv) */
static uint16_t copper_overscan[] = {
    0x008e, 0x2071, /* diwstrt */
    0x0090, 0x30d1, /* diwstop */
    0x0092, 0x0030, /* ddfstrt */
    0x0094, 0x00d8, /* ddfstop */
    0x0100, 0x2200, /* bplcon0 */
    0x0102, 0x0000, /* bplcon1 */
    0x0104, 0x0000, /* bplcon2 */
    0x0108, 0x0000, /* bpl1mod */
    0x010a, 0x0000, /* bpl2mod */
    0x00e0, 0x0002, /* bpl1pth */
    0x00e2, 0x8000, /* bpl1ptl */
    0x00e4, 0x0002, /* bpl2pth */
    0x00e6, 0xc000, /* bpl2ptl */
    0x0182, 0x00f0, /* col01 */
    0x0184, 0x000f, /* col02 */
    0x0186, 0x0f0f, /* col03 */
    0x0180, 0x0f00, /* col00 */
    /* Horizontal positions e2->02 inclusive are indistinguishable and all 
     * occur before the end of 16px overscan. */
    0x8001, 0xfffe,
    0x0182, 0x0fff, /* Oh no! White appears before end of overscan! */
    0x8101, 0xfffe,
    0x0182, 0x00f0,
    /* Horizontal positions 04+ occur during or immediately before horizontal 
     * blank. Position 06 certainly hides the colour change behind flyback. */
    0x9007, 0xfffe,
    0x0180, 0x0fff, /* All good! White does not appear mid-line. */
    0x9107, 0xfffe,
    0x0180, 0x0f00,
    0xffff, 0xfffe,
};

/* As above but more generous 12px overscan on bottom border (352x280 res.)
 * Avoids blank (i.e, RED) bottom border on my CRT. I also tried 8px overscan
 * but that wasn't quite enough with a relaxed CRT vertical-size setting. */
static uint16_t copper_overscan_xtra[] = {
    0x008e, 0x2071, /* diwstrt */
    0x0090, 0x38d1, /* diwstop */
    0x0092, 0x0030, /* ddfstrt */
    0x0094, 0x00d8, /* ddfstop */
    0x0100, 0x2200, /* bplcon0 */
    0x0102, 0x0000, /* bplcon1 */
    0x0104, 0x0000, /* bplcon2 */
    0x0108, 0x0000, /* bpl1mod */
    0x010a, 0x0000, /* bpl2mod */
    0x00e0, 0x0002, /* bpl1pth */
    0x00e2, 0x8000, /* bpl1ptl */
    0x00e4, 0x0002, /* bpl2pth */
    0x00e6, 0xc000, /* bpl2ptl */
    0x0182, 0x00f0, /* col01 */
    0x0184, 0x000f, /* col02 */
    0x0186, 0x0f0f, /* col03 */
    0x0180, 0x0f00, /* col00 */
    0xffff, 0xfffe,
};

/* c.w Pac-Mania: top +43px, bottom -1px(!), left +16px, right +4px 
 * Bottom border is bizarre. Limited right overscan maybe explained by fact 
 * default display is often visibly right-shifted on television sets. */

static void wait_button(void)
{
    while ((ciaa->pra & 0xc0) == 0xc0)
        continue;
    /* Debounce */
    cust->intreq = 0x20;
    while (!(cust->intreqr & 0x20))
        continue;
    while ((ciaa->pra & 0xc0) != 0xc0)
        continue;
    /* Debounce */
    cust->intreq = 0x20;
    while (!(cust->intreqr & 0x20))
        continue;
}

void cstart(void)
{
    uint16_t i;
    char *p;

    /* Clear BSS. */
    memset(_sbss, 0, _ebss-_sbss);

    /* Normal display: all BLUE; background color RED. */
    p = (char *)0x20000;
    memset(p, 0x00, 0x4000);
    p = (char *)0x24000;
    memset(p, 0xff, 0x4000);

    /* Overscan display: GREEN in overscan border. BLUE in middle. */
    p = (char *)0x28000;
    memset(p, 0x00, 0x4000);
    for (i = 0; i < 12; i++) {
        memset(p, 0xff, 352/8);
        p += 352/8;
    }
    for (i = 0; i < 256; i++) {
        memset(p, 0xff, 16/8);
        memset(p+336/8, 0xff, 16/8);
        p += 352/8;
    }
    for (i = 0; i < 12; i++) {
        memset(p, 0xff, 352/8);
        p += 352/8;
    }
    p = (char *)0x2c000;
    memset(p, 0x00, 0x4000);
    p += 12*(352/8);
    for (i = 0; i < 256; i++) {
        memset(p+16/8, 0xff, 320/8);
        p += 352/8;
    }

    cust->cop1lc.p = copper_normal;
    cust->dmacon = 0x81c8; /* enable copper/bitplane DMA */

    /* Switch display size on LMB/Fire. */
    for (;;) {
        wait_button();
        cust->cop1lc.p = copper_overscan;
        wait_button();
        cust->cop1lc.p = copper_overscan_xtra;
        wait_button();
        cust->cop1lc.p = copper_normal;
    }
}

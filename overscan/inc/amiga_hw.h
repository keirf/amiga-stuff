/*
 * amiga_hw.h
 * 
 * Register definitions.
 * 
 * Written & released by Keir Fraser <keir.xen@gmail.com>
 * 
 * This is free and unencumbered software released into the public domain.
 * See the file COPYING for more details, or visit <http://unlicense.org>.
 */

typedef union {
    void *p;
    uint32_t x;
    struct {
        uint16_t h, l;
    };
} cust_ptr_t;

struct amiga_custom {
    uint16_t bltddat;
    uint16_t dmaconr;
    uint16_t vposr;
    uint16_t vhposr;
    uint16_t dskdatr;
    uint16_t joy0dat;
    uint16_t joy1dat;
    uint16_t clxdat;
    uint16_t adkconr;
    uint16_t pot0dat;
    uint16_t pot1dat;
    uint16_t potinp;
    uint16_t serdatr;
    uint16_t dskbytr;
    uint16_t intenar;
    uint16_t intreqr;
    cust_ptr_t dskpt;
    uint16_t dsklen;
    uint16_t dskdat;
    uint16_t refptr;
    uint16_t vposw;
    uint16_t vhposw;
    uint16_t copcon;
    uint16_t serdat;
    uint16_t serper;
    uint16_t potgo;
    uint16_t joytest;
    uint16_t strequ;
    uint16_t strvbl;
    uint16_t strhor;
    uint16_t strlong;
    uint16_t bltcon0;
    uint16_t bltcon1;
    uint16_t bltafwm;
    uint16_t bltalwm;
    cust_ptr_t bltcpt;
    cust_ptr_t bltbpt;
    cust_ptr_t bltapt;
    cust_ptr_t bltdpt;
    uint16_t bltsize;
    uint16_t _0[3];
    uint16_t bltcmod;
    uint16_t bltbmod;
    uint16_t bltamod;
    uint16_t bltdmod;
    uint16_t _1[4];
    uint16_t bltcdat;
    uint16_t bltbdat;
    uint16_t bltadat;
    uint16_t _2[4];
    uint16_t dsksync;
    cust_ptr_t cop1lc;
    cust_ptr_t cop2lc;
    uint16_t copjmp1;
    uint16_t copjmp2;
    uint16_t copins;
    uint16_t diwstrt;
    uint16_t diwstop;
    uint16_t ddfstrt;
    uint16_t ddfstop;
    uint16_t dmacon;
    uint16_t clxcon;
    uint16_t intena;
    uint16_t intreq;
    uint16_t adkcon;
    struct {
        cust_ptr_t lc;
        uint16_t len;
        uint16_t per;
        uint16_t vol;
        uint16_t dat;
        uint16_t _0[2];
    } aud[4];
    cust_ptr_t bpl1pt;
    cust_ptr_t bpl2pt;
    cust_ptr_t bpl3pt;
    cust_ptr_t bpl4pt;
    cust_ptr_t bpl5pt;
    cust_ptr_t bpl6pt;
    cust_ptr_t bpl7pt;
    cust_ptr_t bpl8pt;
    uint16_t bplcon[4];
    uint16_t bpl1mod;
    uint16_t bpl2mod;
    uint16_t bplcon4;
    uint16_t clxcon2;
    uint16_t bpl1dat;
    uint16_t bpl2dat;
    uint16_t bpl3dat;
    uint16_t bpl4dat;
    uint16_t bpl5dat;
    uint16_t bpl6dat;
    uint16_t bpl7dat;
    uint16_t bpl8dat;
    cust_ptr_t sprpt[8];
    struct {
        uint16_t pos;
        uint16_t ctl;
        uint16_t data;
        uint16_t datb;
    } spr[8];
    uint16_t color[32];
    uint16_t htotal;
    uint16_t hsstop;
    uint16_t hbstrt;
    uint16_t hbstop;
    uint16_t vtotal;
    uint16_t vsstop;
    uint16_t vbstrt;
    uint16_t vbstop;
    uint16_t sprhstrt;
    uint16_t sprhstop;
    uint16_t bplhstrt;
    uint16_t bplhstop;
    uint16_t hhposw;
    uint16_t hhposr;
    uint16_t beamcon0;
    uint16_t hsstrt;
    uint16_t vsstrt;
    uint16_t hcenter;
    uint16_t diwhigh;
};

struct amiga_cia {
    uint8_t pra;
    uint8_t _0[0xff];
    uint8_t prb;
    uint8_t _1[0xff];
    uint8_t ddra;
    uint8_t _2[0xff];
    uint8_t ddrb;
    uint8_t _3[0xff];
    uint8_t talo;
    uint8_t _4[0xff];
    uint8_t tahi;
    uint8_t _5[0xff];
    uint8_t tblo;
    uint8_t _6[0xff];
    uint8_t tbhi;
    uint8_t _7[0xff];
    uint8_t todlow;
    uint8_t _8[0xff];
    uint8_t todmid;
    uint8_t _9[0xff];
    uint8_t todhi;
    uint8_t _a[0xff];
    uint8_t b00;
    uint8_t _b[0xff];
    uint8_t sdr;
    uint8_t _c[0xff];
    uint8_t icr;
    uint8_t _d[0xff];
    uint8_t cra;
    uint8_t _e[0xff];
    uint8_t crb;
    uint8_t _f[0xff];
};

/*
 * Local variables:
 * mode: C
 * c-file-style: "Linux"
 * c-basic-offset: 4
 * tab-width: 4
 * indent-tabs-mode: nil
 * End:
 */

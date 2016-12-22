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
    uint16_t _2[3];
    uint16_t deniseid;
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

#define CIAAPRA_OVL  0x01
#define CIAAPRA_LED  0x02
#define CIAAPRA_CHNG 0x04
#define CIAAPRA_WPRO 0x08
#define CIAAPRA_TK0  0x10
#define CIAAPRA_RDY  0x20
#define CIAAPRA_FIR0 0x40
#define CIAAPRA_FIR1 0x80

#define CIABPRB_STEP 0x01
#define CIABPRB_DIR  0x02
#define CIABPRB_SIDE 0x04
#define CIABPRB_SEL0 0x08
#define CIABPRB_SEL1 0x10
#define CIABPRB_SEL2 0x20
#define CIABPRB_SEL3 0x40
#define CIABPRB_MTR  0x80

#define CIAICR_TIMER_A 0x01
#define CIAICR_TIMER_B 0x02
#define CIAICR_TOD     0x04
#define CIAICR_SERIAL  0x08
#define CIAICR_FLAG    0x10
#define CIAICR_SETCLR  0x80

#define CIACRA_START   0x01
#define CIACRA_PBON    0x02
#define CIACRA_OUTMODE 0x04
#define CIACRA_RUNMODE 0x08
#define CIACRA_LOAD    0x10
#define CIACRA_INMODE  0x20
#define CIACRA_SPMODE  0x40

#define CIACRB_START   0x01
#define CIACRB_PBON    0x02
#define CIACRB_OUTMODE 0x04
#define CIACRB_RUNMODE 0x08
#define CIACRB_LOAD    0x10
#define CIACRB_INMODE  0x60
#define CIACRB_ALARM   0x80

#define DMA_AUD0EN 0x0001
#define DMA_AUD1EN 0x0002
#define DMA_AUD2EN 0x0004
#define DMA_AUD3EN 0x0008
#define DMA_AUDxEN 0x000f /* all channels */
#define DMA_DSKEN  0x0010
#define DMA_SPREN  0x0020
#define DMA_BLTEN  0x0040
#define DMA_COPEN  0x0080
#define DMA_BPLEN  0x0100
#define DMA_DMAEN  0x0200
#define DMA_BLTPRI 0x0400
#define DMA_BZERO  0x2000
#define DMA_BBUSY  0x4000
#define DMA_SETCLR 0x8000

#define INT_SER_TX 0x0001
#define INT_DSKBLK 0x0002
#define INT_SOFT   0x0004
#define INT_CIAA   0x0008
#define INT_COPPER 0x0010
#define INT_VBLANK 0x0020
#define INT_BLIT   0x0040
#define INT_AUD0   0x0080
#define INT_AUD1   0x0100
#define INT_AUD2   0x0200
#define INT_AUD3   0x0400
#define INT_SER_RX 0x0800
#define INT_DSKSYN 0x1000
#define INT_CIAB   0x2000
#define INT_INTEN  0x4000
#define INT_SETCLR 0x8000

/*
 * Local variables:
 * mode: C
 * c-file-style: "Linux"
 * c-basic-offset: 4
 * tab-width: 4
 * indent-tabs-mode: nil
 * End:
 */

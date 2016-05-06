/*
 * entry.S
 *
 * Common startup/entry code
 *
 * Written & released by Keir Fraser <keir.xen@gmail.com>
 * 
 * This is free and unencumbered software released into the public domain.
 * See the file COPYING for more details, or visit <http://unlicense.org>.
 */

#define EXEC_Supervisor -0x1e

        /* a4 = current VBR */
        move.l  4,a6
        sub.l   a4,a4
        btst.b  #0,297(a6) /* check for 68010+ in AttnFlags */
        jeq     .novbr     /* If a mere 68000 then there is no VBR */
        lea     getvbr(pc),a5
        jsr     EXEC_Supervisor(a6)
.novbr:

        /* Initialise custom chips */
        lea.l   (0xdff000).l,a6
        move.w  #0x7fff,d0
        move.w  d0,0x9a(a6)  /* intena = 0 */
        move.w  d0,0x9c(a6)  /* intreq = 0 */
        move.w  d0,0x96(a6)  /* dmacon = 0 */
        move.w  d0,0x9e(a6)  /* adkcon = 0 */
        move.w  #0x8200,0x96(a6)  /* enable master DMA */
        move.w  #0xc000,0x9a(a6)  /* enable master IRQ */
        moveq   #0,d0
        move.w  d0,0x180(a6)     /* color0 = black */

        /* Floppy motors off */
        lea     (0xbfd100).l,a5
        ori.b   #0xf8,(a5)
        andi.b  #0x87,(a5)
        ori.b   #0x78,(a5)

        /* Initialise CPU state */
        lea.l   .priv(pc),a0
        move.l  a0,0x20(a4)
.priv:  move.w  #0x2700,sr      /* SR = 0x2700 (supervisor mode, no irqs) */
        lea.l   .skip(pc),a0
        move.l  a0,(0x10).w
        dc.l    0x4e7b0801      /* movec.l d0,vbr  (VBR  = 0) */
        dc.l    0x4e7b0002      /* movec.l d0,cacr (CACR = 0) */
.skip:  lea.l   (SUPER_SP).l,sp /* SSP */
        lea.l   (USER_SP).l,a0
        move.l  a0,usp          /* USP */

        lea.l   start(pc),a0
        move.l  a0,d0           /* a0 = d0 = current location */
        move.l  #start,d1       /* d1 = destination */
        moveq   #4,d2
        swap    d2              /* d2 = 0x40000 (256kB) */
        eor.l   d1,d0           /* *Very* conservative test: could current */
        and.l   d2,d0           /* location and destination overlap? */
        jne     1f
        /* Temp. copy to other half of bottom 512kB */
        eor.l   d2,d1
        jsr     copy
        eor.l   d2,d1
        move.l  #1f,d0
        eor.l   d2,d0
        move.l  d0,a0
        jmp     (a0)
1:      /* Main copy to final relocation point */
        move.l  #main,-(sp)

copy:   lea.l   start(pc),a0    /* a0 = current location */
        move.l  d1,a1           /* a1 = destination */
        move.l  #_end-start,d0
        lsr.l   #2,d0
1:      move.l  (a0)+,(a1)+
        dbf     d0,1b
        rts

main:   move.w  #0x2000,sr      /* allow CPU interrupts now that we are */
                                /* definitely executing clear of the stack */

        jra     cstart

getvbr: dc.l    0x4e7ac801      /* movec.l vbr,a4 */
        rte

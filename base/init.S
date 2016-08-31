/*
 * init.S
 *
 * Common startup/entry code
 *
 * Written & released by Keir Fraser <keir.xen@gmail.com>
 * 
 * This is free and unencumbered software released into the public domain.
 * See the file COPYING for more details, or visit <http://unlicense.org>.
 */

#define EXEC_Supervisor -0x1e

#ifdef DETECT_MEMORY
        /* Walk Exec memory list to find all memory regions. */
        move.l  4,a6
        move.l  0x142(a6),a0 /* ExecBase->MemList.lh_Head */
        lea.l   mem_region(pc),a1
        lea.l   nr_mem_regions(pc),a2
        move.w  (a2),d0      /* d0.w = max # regions */
.mem:   /* First check mh_Attributes specifies some valid flags: at least one
         * of MEMF_PUBLIC, MEMF_CHIP, or MEMF_FAST. This fixes memory detection
         * with AROS which includes some dummy regions in the MemList. */
        move.b  15(a0),d1    /* mh_Attributes */
        and.b   #0x7,d1      /* MEMF_PUBLIC|MEMF_CHIP|MEMF_FAST */
        jeq     1f           /* Skip this chunk if none of the above */
        move.w  14(a0),(a1)+ /* mh_Attributes */
        move.l  20(a0),(a1)+ /* mh_Lower */
        move.l  24(a0),(a1)+ /* mh_Upper */
        subq.w  #1,d0        /* #regions-- */
        jeq     2f           /* done if we can fit no more regions */
1:      move.l  (a0),a0      /* ln_Succ */
        tst.l   (a0)         /* ... != NULL? */
        jne     .mem
2:      sub.w   d0,(a2)      /* update nr_mem_regions with actual # */
#endif

        /* a4 = current VBR */
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

        /* Clobber ExecBase. We are going to take over memory and Kickstart
         * cannot rely on the Exec memory lists when it restarts. Make ExecBase
         * an odd address as that is guaranteed to fail the restart checks. */
        not.l   (4).w

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

        /* Check whether current location and final destination overlap.
         * Since the final destination is as low as possible in memory, this
         * can only occur by the end of the destination running into the start
         * of the current location. */
        lea.l   start(pc),a0
        move.l  a0,d0           /* d0 = current location */
        move.l  #_end,d1        /* d1 = end of destination */
        cmp.l   d1,d0           /* Does the destination overlap with us? */
        jcc     1f
        /* It does overlap so make a temp. copy immediately beyond us.
         * This is safe because we are so low in memory there is guaranteed
         * to be enough memory beyond us to copy into. If we were high enough
         * in memory for this to be a problem we would not be overlapping
         * with the final destination in the first place. */
        move.l  a0,d1
        add.l   #_end-start,d1  /* d1 = temp. location (current end loc.) */
        jbsr    copy
        move.l  d1,a0           /* a0 = temp. location */
        jmp     1f-start(a0)
1:      /* Main copy to final relocation point */
        move.l  #main,-(sp)     /* jump to final destination after the copy */
        move.l  #start,d1       /* d1 = destination */

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

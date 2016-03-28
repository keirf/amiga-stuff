        .section .text.init
        .global start
start:
        /* Standard DOS bootblock header */
        dc.b    'D','O','S',0   /* signature */
        dc.l    0               /* checksum (filled later) */
        dc.l    880             /* dummy root block index */

        /* Compute load size, rounded up to sector alignment */
        move.l  #_edat-start,d0
        add.l   #511,d0
        and.w   #~511,d0
        move.l  d0,-(sp)

        /* Save trackdisk ioreq for later use */
        move.l  a1,-(sp)

        /* Allocate chip memory for the data we're loading */
        moveq   #3,d1           /* MEMF_PUBLIC | MEMF_CHIP */
        jsr     -0xc6(a6)       /* exec.AllocMem */
        move.l  d0,-(sp)
        jne     .load
        add.l   #8,sp
        moveq   #-1,d0
        rts

        /* Load data */
.load:  move.l  4(sp),a1
.retry: move.l  (sp),0x28(a1)   /* io_Data */
        move.l  8(sp),0x24(a1)  /* io_Length */
        clr.l   0x2c(a1)        /* io_Offset */
        move.w  #2,0x1c(a1)     /* io_Command = CMD_READ */
        jsr     -0x1c8(a6)      /* exec.DoIO */
        move.l  4(sp),a1
        move.b  0x1f(a1),d0     /* check io_Error */
        jne     .retry          /* ...retry if failed */

        /* We have loaded code from disk: Flush caches if possible */
        cmp.w   #37,0x14(a6)    /* exec.lib_version >= 37 */
        jmi     .done
        jsr     -0x27c(a6)      /* exec.CacheClearU */

        /* Clean up our stack and jump at what we just loaded */
.done:  movem.l (sp)+,a0-a2
        add.l   #.begin-start,a0
        jmp     (a0)

.begin:
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

        /* Initialise CPU state */
        lea.l   .priv(pc),a0
        move.l  a0,(0x20).w
.priv:  move.w  #0x2700,sr      /* SR = 0x2700 (supervisor mode, no irqs) */
        lea.l   .skip(pc),a0
        move.l  a0,(0x10).w
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

        /* motors off */
        lea     (0xBFD100).l,a5
        ori.b   #0xF8,(a5)
        andi.b  #0x87,(a5)
        ori.b   #0x78,(a5)

        jra     cstart

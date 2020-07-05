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
#define EXEC_CacheClearU -0x27c
#define EXEC_CacheControl -0x288

#ifdef DETECT_MEMORY
        /* Walk Exec memory list to find all memory regions. */
        move.l  4,a6
        move.l  0x142(a6),a0 /* ExecBase->MemList.lh_Head */
        lea.l   start(pc),a1
        move.l  a1,a2
        add.l   #mem_region-start,a1
        add.l   #nr_mem_regions-start,a2
        move.w  (a2),d0      /* d0.w = max # regions */
.mem:   /* First check mh_Attributes specifies some valid flags: at least one
         * of MEMF_PUBLIC, MEMF_CHIP, or MEMF_FAST. This fixes memory detection
         * with AROS which includes some dummy regions in the MemList. */
        move.b  15(a0),d1    /* mh_Attributes */
        and.b   #0x7,d1      /* MEMF_PUBLIC|MEMF_CHIP|MEMF_FAST */
        jeq     1f           /* Skip this chunk if none of the above */
        move.w  14(a0),(a1)+ /* mh_Attributes */
        /* A second sanity check: is mh_Lower < mh_Upper? */
        move.l  20(a0),d1
        cmp.l   24(a0),d1    /* mh_Lower < mh_Upper?... */
        jcc     1f           /* ...skip if not */
        /* Copy this region into our own region list. */
        move.l  d1,(a1)+     /* mh_Lower */
        move.l  24(a0),(a1)+ /* mh_Upper */
        subq.w  #1,d0        /* #regions-- */
        jeq     2f           /* done if we can fit no more regions */
1:      move.l  (a0),a0      /* ln_Succ */
        tst.l   (a0)         /* ... != NULL? */
        jne     .mem
2:      sub.w   d0,(a2)      /* update nr_mem_regions with actual # */
#endif

        /* Disable caches if possible, including flush. */
        cmp.w   #37,0x14(a6)    /* exec.lib_version >= 37 */
        jcs     .no_cache_disable
        moveq   #0,d0
        moveq   #-1,d1
        jsr     EXEC_CacheControl(a6)
.no_cache_disable:

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

        /* Force supervisor mode */
        lea.l   .priv(pc),a0
        move.l  a0,0x20(a4)
.priv:  move.w  #0x2700,sr      /* SR = 0x2700 (supervisor mode, no irqs) */

        /* Skip faulting instruction on Illegal and Line F exceptions */
        lea.l   trap_skip(pc),a0
        move.l  a0,(0x10).w     /* Illegal */
        move.l  a0,(0x2c).w     /* Line F */

        /* Disable caches (Note: d0.l = 0) */
        dc.l    0x4e7b0801      /* movec.l d0,vbr  */
        tst.b   d0              /* set CCR.ZF */
        dc.l    0x4e7a1004      /* movec.l itt0,d1 */ /* 68040+ only */
        jeq     .040            /* CCR.ZF iff 040/060 */

        /* Disable 020/030 caches */
        tst.b   d0              /* set CCR.ZF */
        dc.l    0x4e7b0002      /* movec.l d0,cacr */
        jne     .done           /* CCR.ZF iff 020/030: Bail on 000/010 */

        /* Detect 020 vs 030: Does CACR implement bit 9? */
        move.l  #1<<9,d1
        dc.l    0x4e7b1002      /* movec.l d1,cacr */
        dc.l    0x4e7a1002      /* movec.l cacr,d1 */
        tst.w   d1              /* d1=0 iff running on 68020 */
        jeq     .done           /* Bail if 68020 */

.030:   /* Disable 030 MMU */
        dc.l    0xf0174200      /* pmove tc,(sp) */
        clr.b   (sp)            /* clear TC.{E,SRE,FCL} */
        dc.l    0xf0174000      /* pmove (sp),tc */
        jra     .done

.040:   /* Disable 040/060 caches and superscalar dispatch */
        dc.l    0x4e7a1002      /* movec.l cacr,d1 */
        tst.l   d1              /* Bit 31 set? (68040+ Data Cache Enabled) */
        jpl     1f              /* Skip CPUSHA if not */
        dc.l    0xf4784e71      /* cpusha dc       */ /* 68040+ only */
1:      dc.l    0x4e7b0002      /* movec.l d0,cacr */
        dc.l    0x4e7b0808      /* movec.l d0,pcr  */ /* 68060 only */
        /* Disable 040/060 MMU */
        dc.l    0x4e7b0003      /* movec.l d0,tc */
        dc.l    0x4e7b0004      /* movec.l d0,itt0 */
        dc.l    0x4e7b0005      /* movec.l d0,itt1 */
        dc.l    0x4e7b0006      /* movec.l d0,dtt0 */
        dc.l    0x4e7b0007      /* movec.l d0,dtt1 */

        /* Initialise stack pointers.
         * *** DO NOT USE THE STACK UNTIL AFTER WE ARE RELOCATED!! *** */
.done:  lea.l   (SUPER_SP).l,sp /* SSP */
        lea.l   (USER_SP).l,a0
        move.l  a0,usp          /* USP */

        /* Check whether our init code overlaps with its final destination.
         * Since the final destination is as low as possible in memory, this
         * can only occur by the end of the destination running into the start
         * of the current location. */
        lea.l   start(pc),a0
        move.l  a0,d0           /* d0 = current location */
        move.l  #estart,d1      /* d1 = end of destination */
        cmp.l   d1,d0           /* Does the destination overlap with us? */
        jcc     2f

        /* It does overlap so shuffle the entire payload upwards.
         * This is safe because we are so low in memory there is guaranteed
         * to be enough memory beyond us to copy into. If we were high enough
         * in memory for this to be a problem we would not be overlapping
         * with the final destination in the first place. We copy backwards
         * because the shuffle destination overlaps with us. */
        lea.l   estart(pc),a1   /* a1 = destination */
        move.l  #_sbss-start,d0 /* d0 = #bytes to copy */
        add.l   d0,a0
        add.l   d0,a1
        lsr.l   #2,d0
        subq.l  #1,d0
1:      move.l  -(a0),-(a1)     /* backwards copy */
        dbf     d0,1b
        move.l  a1,a0           /* our dest is the next copy's source */
        jmp     2f-start(a1)

2:      /* Copy the init code only to final destination. We will copy the rest
         * of the payload using a further copy loop, executing from the final
         * destination. */
        move.l  #start,a1
        lea.l   2f-start(a1),a2  /* a2 = jump target */
        move.l  #estart-start,d0 /* d0 = #bytes = init code only */
        lsr.l   #2,d0
        subq.l  #1,d0
1:      move.l  (a0)+,(a1)+
        dbf     d0,1b
        jmp     (a2)
        
2:      /* NOW EXECUTING INIT CODE AT THE FINAL DESTINATION.
         * We now copy the rest of the payload. Copy pointers are already
         * initialised for us. Calculate new length and do the copy. */
        move.l  #_sbss-estart,d0
        lsr.l   #2,d0
        subq.l  #1,d0
1:      move.l  (a0)+,(a1)+
        dbf     d0,1b

        /* Allow CPU interrupts now that we are definitely executing clear
         * of the stack, and return to user mode. */
        move.w  #0x0000,sr

        /* Jump to C code. */
        jmp     (cstart).l

getvbr: dc.l    0x4e7ac801      /* movec.l vbr,a4 */
        rte

trap_skip:
        and.w   #~4,(sp)        /* clear CCR_ZF */
        addq.l  #4,2(sp)        /* skip faulting instruction */
        rte

        .balign 4
estart:

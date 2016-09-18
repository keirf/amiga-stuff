/*
 * bootblock.S
 *
 * Amiga floppy-disk bootblock loader.
 *
 * Written & released by Keir Fraser <keir.xen@gmail.com>
 * 
 * This is free and unencumbered software released into the public domain.
 * See the file COPYING for more details, or visit <http://unlicense.org>.
 */

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
        jmp     .begin-start(a0)

.begin:
#include "init.S"

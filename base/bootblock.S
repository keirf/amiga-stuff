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

/* LibBase offsets */
#define LibBase_Version 20

/* Exec library calls */
#define Exec_AllocMem    -0x0c6
#define Exec_FreeMem     -0x0d2
#define Exec_DoIO        -0x1c8
#define Exec_CacheClearU -0x27c

/* IORequest.io_Command values */
#define CMD_READ   2
#define TD_MOTOR   9

/* struct IORequest offsets */
#define io_Command 0x1c
#define io_Error   0x1f
#define io_Length  0x24
#define io_Data    0x28
#define io_Offset  0x2c

/* AllocMem flags */
#define MEMF_PUBLIC 1
#define MEMF_CHIP   2
#define MEMF_FAST   4

        .text
start:
        /* Bootblock header */
        .asciz  "DOS"
        dc.l    0, 880
        bra.b   1f
pl_load_off: dc.l 0
pl_load_len: dc.l 0
        
1:      /* Save trackdisk ioreq for later use */
        move.l  a1,-(sp)

        /* Allocate memory for the data we're loading from trackdisk. */
        move.l  pl_load_off(pc),d0
        add.l   pl_load_len(pc),d0
        moveq   #MEMF_PUBLIC|MEMF_CHIP,d1
        cmp.w   #37,LibBase_Version(a6)
        jcs     1f
        moveq   #MEMF_PUBLIC,d1  /* exec >= 37: don't need to load into chip */
1:      jsr     Exec_AllocMem(a6)
        move.l  (sp),a1
        move.l  d0,-(sp)
        jeq     error

        /* Load the INFLATE routine and the compressed payload data. */
        moveq   #3-1,d2 /* max three attempts */
1:      move.l  (sp),d0
        add.l   pl_load_off(pc),d0
        move.l  d0,io_Data(a1)
        move.l  pl_load_len(pc),io_Length(a1)
        clr.l   io_Offset(a1)
        move.w  #CMD_READ,io_Command(a1)
        jsr     Exec_DoIO(a6)
        move.l  4(sp),a1
        tst.b   io_Error(a1)
        dbeq    d2,1b
        jne     error

        jbsr    motor_off

        /* Allocate memory for INFLATE. We cannot run it from the payload
         * area as it will be overwritten during payload decompression. */
        move.l  #pl_data-start,d0
        moveq   #MEMF_PUBLIC,d1
        jsr     Exec_AllocMem(a6)
        move.l  4(sp),a1
        move.l  d0,-(sp)
        jeq     error

        /* Copy INFLATE routine from payload to its own allocated area. */
        move.l  (sp),a1
        move.l  4(sp),a0
        add.l   pl_load_off(pc),a0
        move.l  #pl_data-start,d0
        lsr.l   #2,d0
        subq.l  #1,d0
1:      move.l  (a0)+,(a1)+
        dbf     d0,1b

        /* Call INFLATE. */
        jbsr    cache_clear
        move.l  (sp)+,a0
        jsr     INFLATE-start(a0)

        /* Jump to the decompressed payload. */
        move.l  (sp)+,a0        /* a0 = payload data */
        move.l  (sp)+,a1        /* a1 = TDIOReq */
        jmp     (a0)

INFLATE:
        move.l  4(sp),a4        /* a4 = dest */
        lea     pl_data-start(a4),a5
        add.l   pl_load_off(pc),a5 /* a5 = src */
        jbsr    inflate

        jbsr    cache_clear

        /* Deallocate the INFLATE memory area and return to the bootblock. */
        lea     start(pc),a1
        move.l  #pl_data-start,d0
        move.l  (4).w,a6
        jmp     Exec_FreeMem(a6)

cache_clear:
        /* Code has been written to memory: Flush caches if possible. */
        cmp.w   #37,LibBase_Version(a6) /* exec.lib_version >= 37? */
        jcs     1f
        jsr     Exec_CacheClearU(a6)
1:      rts

motor_off:
        clr.l   io_Length(a1)
        move.w  #TD_MOTOR,io_Command(a1)
        jmp     Exec_DoIO(a6)
        
error:  jbsr    motor_off
1:      move.w  #0xf00,0xdff180
        jra     1b
        
#include "../inflate/inflate.S"

        .balign 4
pl_data:

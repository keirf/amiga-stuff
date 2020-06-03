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

#define pl_load_off  0
#define pl_load_len  4
#define pl_data      8

        .text
start:
        /* Bootblock header */
        .asciz  "DOS"
        dc.l    0, 880
        
        /* Save trackdisk ioreq for later use */
        move.l  a1,-(sp)

        /* Allocate chip memory for the data we're loading */
        move.l  pl+pl_load_off(pc),d0
        add.l   pl+pl_load_len(pc),d0
        moveq   #MEMF_PUBLIC|MEMF_CHIP,d1
        jsr     Exec_AllocMem(a6)
        move.l  d0,-(sp)
        jne     1f
2:      move.w  #0xf00,0xdff180
        jra     2b

        /* Load data, retry on failure */
1:      move.l  4(sp),a1
.retry: move.l  (sp),d0
        add.l   pl+pl_load_off(pc),d0
        move.l  d0,io_Data(a1)
        move.l  pl+pl_load_len(pc),io_Length(a1)
        clr.l   io_Offset(a1)
        move.w  #CMD_READ,io_Command(a1)
        jsr     Exec_DoIO(a6)
        move.l  4(sp),a1
        tst.b   io_Error(a1)
        jne     .retry

        /* Motor off. */
        clr.l   io_Length(a1)
        move.w  #TD_MOTOR,io_Command(a1)
        jsr     Exec_DoIO(a6)

        move.l  (sp),a1
        lea.l   pl+pl_data-start(a1),a0
        add.l   pl+pl_load_off(pc),a0
        sub.l   a2,a2
        move.l  #0,0x6feee
        jbsr    ShrinklerDecompress

        /* We have loaded code from disk: Flush caches if possible */
        cmp.w   #37,LibBase_Version(a6) /* exec.lib_version >= 37? */
        jcs     1f
        jsr     Exec_CacheClearU(a6)

1:      move.l  (sp)+,a0        /* a0 = payload data */
        move.l  (sp)+,a1        /* a1 = TDIOReq */
        jmp     (a0)

ShrinklerDecompress:
        .incbin "shrinkler.bin"

        .balign 4
pl:

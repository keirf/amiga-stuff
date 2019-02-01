/*
 * dos_entry.S
 *
 * Entry code for startup from Amiga Workbench or CLI.
 *
 * Written & released by Keir Fraser <keir.xen@gmail.com>
 * 
 * This is free and unencumbered software released into the public domain.
 * See the file COPYING for more details, or visit <http://unlicense.org>.
 */

/* Exec Library Vector Offsets */
#define EXEC_Forbid   -0x84
#define EXEC_FindTask -0x126
#define EXEC_GetMsg   -0x174
#define EXEC_ReplyMsg -0x17a
#define EXEC_WaitPort -0x180
#define EXEC_OpenLibrary -0x198
#define EXEC_CloseLibrary -0x19e

/* Graphics Linrary Vector Offsets */
#define GFX_LoadView  -0xde
#define GFX_WaitTOF   -0x10e

/* Offsets into DOS Process structure (extends Exec Task) */
#define pr_MsgPort   0x5c
#define pr_CLI       0xac

/* Offsets into GfxBase */
#define gfx_ActiView 0x22
#define gfx_cop1     0x26
#define gfx_cop2     0x32

        .section .text.init
        .global start
start:
        move.l  4,a6

        /* Check if we were trackloaded (try to open DOS library). */
        lea     .dos(pc),a1
        moveq   #0,d0
        move.l  d0,-(sp)          /* no wb startup msg yet */
        jsr     EXEC_OpenLibrary(a6)
        tst.l   d0
        jeq     .nowb             /* No DOSBase? We were trackloaded. */
        movel   d0,a1
        jsr     EXEC_CloseLibrary(a6)

        /* Find our Process structure and determine how we were started. */
        sub.l   a1,a1
        jsr     EXEC_FindTask(a6)  /* FindTask(NULL) */
        move.l  d0,a4
        tst.l   pr_CLI(a4)        /* Running from CLI? */
        bne     .nowb

        /* Started from Workbench: wait for start message */
        lea     pr_MsgPort(a4),a0
        jsr     EXEC_WaitPort(a6)
        lea     pr_MsgPort(a4),a0
        jsr     EXEC_GetMsg(a6)
        move.l  d0,(sp)           /* Save the startup msg */
.nowb:
        /* graphics.library set up a compatible view */
        lea     .gfx(pc),a1
        moveq   #0,d0
        jsr     EXEC_OpenLibrary(a6)
        move.l  d0,a6
        move.l  gfx_ActiView(a6),-(sp)
        sub.l   a1,a1
        jsr     GFX_LoadView(a6)  /* LoadView(NULL) */
        jsr     GFX_WaitTOF(a6)
        jsr     GFX_WaitTOF(a6)
        move.l  a6,-(sp)
        
        jbsr    .begin
        move.l  d0,d7

        /* d7 = return code */
        /* 0(sp) = gfxbase */
        /* 4(sp) = original view */
        /* 8(sp) = wb msg (or null) */
        
        /* Restore original view */
        move.l  (sp)+,a6
        jsr     GFX_WaitTOF(a6)
        jsr     GFX_WaitTOF(a6)
        lea.l   (0xdff000).l,a5
        move.l  gfx_cop1(a6),0x80(a5)   /* cop1lc */
        move.l  gfx_cop2(a6),0x84(a5)   /* cop2lc */
        move.l  (sp)+,a1
        jsr     GFX_LoadView(a6)        /* original view */

        /* Close graphics library */
        movel   a6,a1
        move.l  4,a6
        jsr     EXEC_CloseLibrary(a6)

        move.l  (sp)+,d3        /* d3 = workbench msg */
        beq     .quit
        /* Workbench: Return the startup message to our parent */
        jsr     EXEC_Forbid(a6)
        move.l  d3,a1
        jsr     EXEC_ReplyMsg(a6)
.quit:  move.l  d7,d0
        rts

.dos:   .asciz  "dos.library"
.gfx:   .asciz  "graphics.library"
        .balign 2

.begin:
#include "init.S"

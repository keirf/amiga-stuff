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

/* Hunk IDs in Amiga load file */
#define HUNK_HEADER   0x3F3
#define HUNK_CODE     0x3E9
#define HUNK_DATA     0x3EA
#define HUNK_BSS      0x3EB
#define HUNK_RELOC32  0x3EC
#define HUNK_END      0x3F2

/* Payload length (rounded up to sector boundary) is prepended along with
 * standard bootblock header by a build script. */
#define load_len start-4
        
        .text
start:
        /* Save trackdisk ioreq for later use */
        move.l  a1,-(sp)

        /* Allocate chip memory for the data we're loading */
        move.l  load_len(pc),d0
        moveq   #MEMF_PUBLIC|MEMF_CHIP,d1
        jsr     Exec_AllocMem(a6)
        move.l  d0,-(sp)
        jne     .load
        addq.l  #8,sp
        moveq   #-1,d0
        rts

        /* Load data, retry on failure */
.load:  move.l  4(sp),a1
.retry: move.l  (sp),io_Data(a1)
        move.l  load_len(pc),io_Length(a1)
        move.l  #512,io_Offset(a1)
        move.w  #CMD_READ,io_Command(a1)
        jsr     Exec_DoIO(a6)
        move.l  4(sp),a1
        tst.b   io_Error(a1)
        jne     .retry

        /* We have loaded code from disk: Flush caches if possible */
        cmp.w   #37,LibBase_Version(a6) /* exec.lib_version >= 37? */
        jmi     .nocache
        jsr     Exec_CacheClearU(a6)
.nocache:

        move.l  (sp),a3         /* a3 = payload data */
        move.l  (a3),d0
        cmp.l   #HUNK_HEADER,d0 /* AmigaDOS load file? */
        jeq     LoadSeg         /* LoadSeg it if so. */

        /* Raw payload: jump directly at it. */
        addq.l  #8,sp
        jmp     (a3)

LoadSeg: /* LoadSeg(a3) */
        add.w   #16,a3          /* Skip HUNK_HEADER, string, nr hunks, first */
        move.l  (a3)+,d2        /* d2 = d3 = last hunk id (== nr - 1) */
        move.l  d2,d3
        lea     hunks(pc),a2
1:      move.l  (a3)+,d0        /* d0 = hunk size (longwords) + flags */
        move.l  d0,d1
        lsl.l   #2,d0           /* d0 = hunk size (bytes) */
        rol.l   #3,d1
        and.l   #7,d1
        or.b    #1,d1           /* d1 = AllocMem flags */
        jsr     Exec_AllocMem(a6)
        move.l  d0,(a2)+        /* save hunk base to hunks[] */
        dbf     d2,1b
        lea     hunks(pc),a2

next_hunk:
        move.l  (a3)+,d0        /* d0 = hunk id */
        move.l  (a3)+,d2
        subq.w  #1,d2           /* d2 = hunk size (longwords) - 1 */
        move.l  (a2)+,a0
        move.l  a0,a1           /* a0 = a1 = hunk base address */
        cmp.w   #HUNK_BSS,d0
        jeq     hunk_bss
hunk_code_or_data:              /* HUNK_CODE, HUNK_DATA */
1:      move.l  (a3)+,(a1)+     /* Fill the hunk */
        dbf     d2,1b
        jra     next_subhunk
hunk_bss:                       /* HUNK_BSS */
        moveq   #0,d0
1:      move.l  d0,(a1)+        /* Zero the hunk */
        dbf     d2,1b
next_subhunk:
        move.l  (a3)+,d0        /* d0 = subhunk id */
        cmp.w   #HUNK_RELOC32,d0
        jne     hunk_end        /* must be HUNK_END */
next_reloc:                     /* HUNK_RELOC32 */
        move.l  (a3)+,d2        /* nr relocs */
        jeq     next_subhunk
        subq.w  #1,d2           /* d2 = nr relocs - 1 */
        move.l  (a3)+,d1        /* reloc hunk */
        lsl.w   #2,d1
        move.l  hunks(pc,d1.w),d1 /* reloc hunk base address */
1:      move.l  (a3)+,d0        /* reloc offset */
        add.l   d1,(a0,d0.l)
        dbf     d2,1b
        jra     next_reloc
hunk_end:                       /* HUNK_END */
        dbf     d3,next_hunk

        /* Free the payload */
        move.l  (sp)+,a1
        move.l  load_len(pc),d0
        jsr     Exec_FreeMem(a6)

        /* Jump at Hunk 0 (must be HUNK_CODE!) */
        move.l  hunks(pc),a0
        move.l  (sp)+,a1        /* a1 = TDIOReq */
        jmp     (a0)
        
        .align  4
hunks:

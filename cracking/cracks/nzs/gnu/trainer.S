
#define xres    640
#define yres    169
#define bplsz   (yres*xres/8)
#define planes  2

#define xstart  110
#define ystart  20
#define yperline 10

#define bss     0x20000
#define gfx     bss + size

#define key     /* 0 */ /* First offset in BSS */

trainer:
        /* a4=bss; a5=ciaa; a6=custom (set up on entry) */
        lea     (bss).l,a4
        lea.l   (0xbfe001).l,a5

        /* Set up CIAA ICR. We only care about keyboard/ */
        move.b  #0x7f,0xd00(a5)   /* enable only keyboard data rx'ed */
        move.b  #0x88,0xd00(a5)   /* interrupt from CIAA */

        move.w  #0x8040,0x96(a6)  /* enable blitter DMA */
        jbsr    init_bss_and_copper
        jbsr    clear_screen
        jbsr    clear_colors
        jbsr    unpack_font
        
	lea	CIA_IRQ(pc),a0
	move.l	a0,(0x68).w      /* level 2 interrupt (CIA) */
        lea.l   copper(pc),a0
        move.l  a0,0x80(a6)      /* cop1lc */
        
        jbsr    print_screen
        jbsr    setup_options

        /* At end of screen display, turn everything on */
        jbsr    wait_bos
        move.w  #0x81c0,0x96(a6)  /* enable copper/bitplane/blitter DMA */
        move.w  #0x8008,0x9a(a6)  /* enable CIA-A interrupts */

        /* Event loop: check for keys, buttons, etc. Act on them */
.wait:  jbsr    wait_bos
        moveq   #0,d0
        move.b  key(a4),d0
        jeq     .nokey
        jmi     .nokey
        clr.b   key(a4)
        cmp.b   #0x40,d0         /* SPACE? Then exit */
        jeq     .exit
        sub.b   #0x50,d0
        jbsr    update_option
.nokey: move.b  (a5),d0
        and.b   #0xc0,d0
        cmp.b   #0xc0,d0
        jeq     .wait

        /* Tear down */
.exit:  jbsr    waitblit         /* blitter idle */
        jbsr    wait_bos         /* screen drawn for the last time */
        move.w  #0x01c0,0x96(a6)
        move.w  #0x0008,0x9a(a6) /* knock it on the head */
        jbsr    clear_colors     /* black screen */

        /* Marshal selected trainer options into d0.l */
        lea.l   options(a4),a0
        moveq   #0,d0
.opts:  move.l  (a0)+,d1
        jeq     .done
        move.l  d1,a1
        move.b  (a1),d1
        lsr.b   #4,d1
        lsl.l   #4,d0
        or.b    d1,d0
        jra     .opts
.done:  rts

init_bss_and_copper:
        move.l  a4,a0
        move.w  #size/4,d0
1:      clr.l   (a0)+
        dbf     d0,1b
        lea.l   copper(pc),a1
2:      cmp.w   #0x00e0,(a1)+
        jne     2b
        move.l  #gfx,d0
        move.l  d0,bpl0(a4)
        move.w  d0,4+0(a1)
        swap    d0
        move.w  d0,0+0(a1)
        move.l  #gfx+bplsz,d0
        move.l  d0,bpl1(a4)
        move.w  d0,4+8(a1)
        swap    d0
        move.w  d0,0+8(a1)
        move.l  #gfx+2*bplsz,d0
        move.l  d0,font(a4)
        rts
        
clear_colors:
        lea     0x180(a6),a0
        moveq   #0,d0
        moveq   #15,d1
1:      move.l  d0,(a0)+
        dbf     d1,1b
        rts

        /* d0 = option # */
update_option:
        tst.b   d0
        jmi     .udone
        cmp.b   #10,d0
        jpl     .udone
        move.w  d0,d1
        lsl.w   #2,d1
        move.l  options(a4,d1.w),d1
        jeq     .udone
        move.l  d1,a0
        move.b  (a0),d1
        and.b   #3,d1
        cmp.b   #1,d1
        jeq     .urange
.ubool: eor.b   #0x10,(a0)
        jra     .print
.urange:move.b  (a0),d1
        lsr.b   #4,d1
        addq    #1,d1
        move.b  1(a0),d2
        lsr.b   #4,d2
        cmp.b   d2,d1
        jmi     .ok
        jeq     .ok
        move.b  1(a0),d1
.ok:    lsl.b   #4,d1
        or.b    #1,d1
        move.b  d1,(a0)
.print: jbsr    print_option
.udone: rts
        
        /* d0 = line # */
        /* all regs prserved */
clear_line:
        movem.l d0/a0,-(sp)
        mulu.w  #yperline,d0
        add.w   #ystart,d0
        mulu.w  #xres/8,d0
        move.l  bpl0(a4),a0
        adda.w  d0,a0
        jbsr    waitblit
        move.l  #0x01000000,0x40(a6) /* bltcon0/bltcon1 */
        move.l  a0,0x54(a6)         /* bltdpt */
        move.w  #0,0x66(a6)         /* bltdmod */
        move.w  #(xres/16)|(yperline<<6),0x58(a6)
        move.l  bpl1(a4),a0
        adda.w  d0,a0
        jbsr    waitblit
        move.l  a0,0x54(a6)         /* bltdpt */
        move.w  #(xres/16)|(yperline<<6),0x58(a6)
        movem.l (sp)+,d0/a0
        rts
        
clear_screen:
        jbsr    waitblit
        move.l  #0x01000000,0x40(a6) /* bltcon0/bltcon1 */
        move.l  bpl0(a4),a0
        move.l  a0,0x54(a6)         /* bltdpt */
        move.w  #0,0x66(a6)         /* bltdmod */
        move.w  #(xres/16)|((yres*planes)<<6),0x58(a6)
        jra     waitblit

        /* Unpack 8*8 font into destination */
        /* Each char 00..7f is copied in sequence */
        /* Destination is 10 longwords (= 10 rows) per character */
        /* First word of each long is foreground, second word is background */
        /* Background is computed from foreground */
unpack_font:
        lea.l   packfont(pc),a0
        move.l  font(a4),a1
        move.l  a1,a2
        move.w  #0x20*yperline-1,d0
1:      clr.l   (a1)+           /* first 0x20 chars are blank */
        dbf     d0,1b
        moveq   #0x60-1,d0
1:      clr.l   (a1)+           /* first row of foreground is blank */
        moveq   #yperline-3,d1
2:      moveq   #0,d2
        move.b  (a0)+,d2
        lsl.w   #7,d2           /* foreground character is shifted right */
        swap    d2              /* one place from leftmost */
        move.l  d2,(a1)+
        dbf     d1,2b
        clr.l   (a1)+           /* last row of foreground is blank */
        dbf     d0,1b
        move.l  a2,a1
        moveq   #0x80-1,d0
1:      move.w  (a1)+,d2
        or.w    2(a1),d2
#define shift_lr       \
        move.w  d2,d3; \
        add.w   d3,d3; \
        or.w    d2,d3; \
        lsr.w   #1,d2; \
        or.w    d3,d2; \
        move.w  d2,(a1)+
        shift_lr
        moveq   #yperline-3,d1
2:      move.w  -4(a1),d2
        or.w    (a1)+,d2
        or.w    2(a1),d2
        shift_lr
        dbf     d1,2b
        move.w  -4(a1),d2
        or.w    (a1)+,d2
        shift_lr
        dbf     d0,1b
        rts

print_screen:
        lea.l   str(pc),a3
1:      tst.b   (a3)
        jmi     9f
        jbsr    print_line
        jra     1b
9:      rts

setup_options:
        lea.l   opts(pc),a0
        lea.l   options(a4),a1
        moveq   #0,d0
1:      move.l  a0,(a1)+
        move.b  (a0),d1
        jmi     9f
        jbsr    print_option
        addq    #1,d0
        jra     1b
9:      clr.l   -4(a1)
        rts

        /* a0 = option (updated at end to point past end of option) */
        /* d0 = option # */
        /* all regs preserved */
#define opt_col1    8
#define opt_row1    5
#define opt_confcol 37
print_option:
        movem.l d0-d3/a3,-(sp)
        lea.l   string(a4),a3
        move.b  (a0)+,d2
        move.b  (a0)+,d3
        move.b  #opt_col1,(a3)+
        move.b  d0,d1
        add.b   #opt_row1,d0
        move.b  d0,(a3)+
        jbsr    clear_line
        move.b  #'F',(a3)+
        add.b   #'1',d1
        move.b  d1,(a3)+
        move.b  #' ',(a3)+
        move.b  #'-',(a3)+
        move.b  #' ',(a3)+
        moveq   #opt_confcol-13,d0
.l1:    subq.l  #1,d0
        move.b  (a0)+,(a3)+
        jne     .l1
        sub.w   #1,a3
.l2:    move.b  #'.',(a3)+
        dbf     d0,.l2
        move.b  d2,d3
        and.b   #3,d3
        cmp.b   #1,d3
        jeq     .prange
.pbool: move.b  #'O',(a3)+
        and.b   #0x10,d2
        jne     .bon
        move.b  #'F',(a3)+
        move.b  #'F',(a3)+
        jra     .pdone
.bon:   move.b  #'N',(a3)+
        move.b  #' ',(a3)+
        jra     .pdone
.prange:move.b  #' ',(a3)+
        lsr.b   #4,d2
        cmp.b   #10,d2
        jmi     .onedig
        move.b  #'1',-1(a3)
        sub.b   #10,d2
.onedig:add.b   #'0',d2
        move.b  d2,(a3)+
.pdone: move.b  #0,(a3)
        lea.l   string(a4),a3
        jbsr    print_line
        movem.l (sp)+,d0-d3/a3
        rts
        
        /* a3 = string (points at string end on exit) */
        /* all other registers preserved */
print_line:
        movem.l d0-d1/a0-a1,-(sp)
        moveq   #0,d0
        move.b  (a3)+,d0
        lsl.w   #3,d0
        add.w   #xstart,d0
        moveq   #0,d1
        move.b  (a3)+,d1
        mulu.w  #yperline,d1
        add.w   #ystart,d1
1:      move.b  (a3)+,d2
        jeq     9f
        jbsr    print_char
        add.w   #8,d0
        jra     1b
9:      movem.l (sp)+,d0-d1/a0-a1
        rts                
        
        /* d0.w = x; d1.w = y; d2.w = char */
        /* a0-a1 trashed */
print_char:
        movem.l d0-d2,-(sp)
        and.w   #0x7f,d2
        mulu.w  #yperline*4,d2
        addq    #2,d2
        move.l  font(a4),a0
        adda.w  d2,a0           /* a0 = points to correct font char */
        mulu.w  #xres/8,d1
        move.w  d0,d2
        lsr.w   #4,d2
        add.w   d2,d1
        add.w   d2,d1
        move.l  bpl0(a4),a1
        adda.w  d1,a1           /* a1 = points to correct first dest word */
        moveq   #0,d2
        move.b  d0,d2
        and.b   #15,d2
        ror.w   #4,d2
        or.w    #0x0dfc,d2       /* ABD DMA, D=A|B */
        swap    d2
        jbsr    waitblit
        move.l  d2,0x40(a6)      /* bltcon0 (D=A|B) / bltcon1 */
        move.l  #(xres/8)-4,0x60(a6)/* bltcmod/bltbmod */
        move.l  #(xres/8)-4,0x64(a6)/* bltamod/bltdmod */
        move.l  #0xffff0000,0x44(a6) /* bltafwm/bltalwm */
        moveq   #1,d0
1:      jbsr    waitblit
        move.l  a0,0x50(a6)         /* bltapt */
        move.l  a1,0x4c(a6)         /* bltbpt */
        move.l  a1,0x54(a6)         /* bltdpt */
        move.w  #2|(yperline<<6),0x58(a6) /* bltsz = 2 words * yperline rows */
        sub.w   #2,a0
        move.l  bpl1(a4),a1
        adda.w  d1,a1
        dbf     d0,1b
        movem.l (sp)+,d0-d2
        rts

wait_bos:
        cmp.b   #0xf0,6(a6)      /* wait for end of bitplane DMA */
        jne     wait_bos
        rts
        
waitblit:
        btst.b  #6,2(a6)
1:      btst.b  #6,2(a6)        /* wait for idle blitter */
        jne     1b
        rts
        
wait_line:
        move.b  0x6(a6),d0
1:      cmp.b   0x6(a6),d0
        jeq     1b
        rts
        
CIA_IRQ:
        movem.l d0-d1/a0,-(sp)
        move.b  0xd00(a5),d0
        btst    #3,d0           /* ciaa.icr - SDR finished a byte? */
        jeq     9f
        move.b  0xc00(a5),d1    /* grab the keyboard byte if so */
        bset.b  #6,0xe00(a5)    /* start the handshake */
        not.b   d1
        ror.b   #1,d1
        move.b  d1,key(a4)      /* fix up and save the key code */
        jbsr    wait_line
        jbsr    wait_line       /* wait ~100us */
        bclr.b  #6,0xe00(a5)    /* finish the handshake */
9:      movem.l (sp)+,d0-d1/a0
        /* NB. Clear intreq.ciaa *after* reading/clearing ciaa.icr else we
         * get a spurious extra interrupt, since intreq.ciaa latches the level
         * of CIAA INT and hence would simply become set again immediately
         * after we clear it. For this same reason (latches level not edge) it
         * is *not* racey to clear intreq.ciaa second. Indeed AmigaOS does the
         * same (checked Kickstart 3.1). */
        move.w  #0x0008,0x9c(a6)
        rte
        
copper:
        dc.l    0x008e4681       /* diwstrt.v = 0x46 */
        dc.l    0x0090efc1       /* diwstop.v = 0xef (169 lines) */
        dc.l    0x0092003c       /* ddfstrt */
        dc.l    0x009400d4       /* ddfstop */
        dc.l    0x0100a200       /* bplcon0 */
        dc.l    0x01020000       /* bplcon1 */
        dc.l    0x01040000       /* bplcon2 */
        dc.l    0x01080000       /* bpl1mod */
        dc.l    0x010a0000       /* bpl2mod */
        dc.l    0x00e00000       /* bpl1pth */
        dc.l    0x00e20000       /* bpl1ptl */
        dc.l    0x00e40000       /* bpl2pth */
        dc.l    0x00e60000       /* bpl2ptl */
        dc.l    0x01820222       /* col01 */
        dc.l    0x01840ddd       /* col02 */
        dc.l    0x01860ddd       /* col03 */
        dc.l    0x01800103       /* col00 */
        dc.l    0x4407fffe
        dc.l    0x01800ddd
        dc.l    0x4507fffe
        dc.l    0x01800402
        dc.l    0xf007fffe
        dc.l    0x01800ddd
        dc.l    0xf107fffe
        dc.l    0x01800103
        dc.l    0xfffffffe

packfont: .incbin  "../FONT2_8X8.BIN"
str:    dc.b 13, 0; .asciz            "====================="
        dc.b 11, 1; .asciz          "+ THE NEW ZEALAND STORY +"
        dc.b 13, 2; .asciz            "====================="
        dc.b  6, 3; .asciz     "Cracked & Trained by KAF in June '11"
        dc.b  0,10; .asciz "Space, Mouse button, or Joystick Fire to Continue!"
        dc.b 0xff

#define RANGE_OPT(def,min,max) \
        dc.b 1|((def)<<4),(min)|((max)<<4)
#define BOOL_OPT(def) \
        dc.b 2|((def)<<4),0
opts:   BOOL_OPT(0)
        .asciz "Infinite Lives"
        RANGE_OPT(3,1,10)
        .asciz "Initial Lives"
        BOOL_OPT(0)
        .asciz "Load/Save Highscores"
        BOOL_OPT(0)
        .asciz "Reset Saved Highscores"
        dc.b 0xff
        
        .struct 0
RSB(_key,    1)
RSB(pad,     1)
RSL(bpl0,    1)
RSL(bpl1,    1)
RSL(font,    1)
RSL(options, 10)
RSB(string,  80)
RSB(size,    0)
        .text


xres    equ     640
yres    equ     169
bplsz   equ     (yres*xres/8)
planes  equ     2

xstart  equ     110
ystart  equ     20
yperline equ    10

bss     equ     $20000
gfx     equ     bss + size
        
trainer:
        ; a4=bss; a5=ciaa; a6=custom (set up on entry)
        lea     (bss).l,a4
        lea.l   ($bfe001).l,a5

        ; Set up CIAA ICR. We only care about keyboard/
        move.b  #$7f,$d00(a5)   ; enable only keyboard data rx'ed
        move.b  #$88,$d00(a5)   ; interrupt from CIAA

        move.w  #$8040,$96(a6)  ; enable blitter DMA
        bsr     init_bss_and_copper
        bsr     clear_screen
        bsr     clear_colors
        bsr     unpack_font
        
	lea	CIA_IRQ(pc),a0
	move.l	a0,($68).w      ; level 2 interrupt (CIA)
        lea.l   copper(pc),a0
        move.l  a0,$80(a6)      ; cop1lc
        
        bsr     print_screen
        bsr     setup_options

        ; At end of screen display, turn everything on
        bsr     wait_bos
        move.w  #$81c0,$96(a6)  ; enable copper/bitplane/blitter DMA
        move.w  #$8008,$9a(a6)  ; enable CIA-A interrupts

        ; Event loop: check for keys, buttons, etc. Act on them
.wait:  bsr     wait_bos
        moveq   #0,d0
        move.b  key(a4),d0
        beq     .nokey
        bmi     .nokey
        clr.b   key(a4)
        cmp.b   #$40,d0         ; SPACE? Then exit
        beq     .exit
        sub.b   #$50,d0
        bsr     update_option
.nokey: move.b  (a5),d0
        and.b   #$c0,d0
        cmp.b   #$c0,d0
        beq     .wait

        ; Tear down
.exit:  bsr     waitblit        ; blitter idle
        bsr     wait_bos        ; screen drawn for the last time
        move.w  #$01c0,$96(a6)
        move.w  #$0008,$9a(a6)  ; knock it on the head
        bsr     clear_colors    ; black screen

        ; Marshal selected trainer options into d0.l
        lea.l   options(a4),a0
        moveq   #0,d0
.opts:  move.l  (a0)+,d1
        beq     .done
        move.l  d1,a1
        move.b  (a1),d1
        lsr.b   #4,d1
        lsl.l   #4,d0
        or.b    d1,d0
        bra     .opts
.done:  rts

init_bss_and_copper:
        move.l  a4,a0
        move.w  #size/4,d0
.clr:   clr.l   (a0)+
        dbf     d0,.clr
        lea.l   copper(pc),a1
.srch:  cmp.w   #$00e0,(a1)+
        bne     .srch
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
        lea     $180(a6),a0
        moveq   #0,d0
        moveq   #15,d1
.clr:   move.l  d0,(a0)+
        dbf     d1,.clr
        rts

        ; d0 = option #
update_option:
        tst.b   d0
        bmi     .done
        cmp.b   #10,d0
        bpl     .done
        move.w  d0,d1
        lsl.w   #2,d1
        move.l  options(a4,d1.w),d1
        beq     .done
        move.l  d1,a0
        move.b  (a0),d1
        and.b   #3,d1
        cmp.b   #1,d1
        beq     .range
.bool:  eor.b   #$10,(a0)
        bra     .print
.range: move.b  (a0),d1
        lsr.b   #4,d1
        addq    #1,d1
        move.b  1(a0),d2
        lsr.b   #4,d2
        cmp.b   d2,d1
        bmi     .ok
        beq     .ok
        move.b  1(a0),d1
.ok:    lsl.b   #4,d1
        or.b    #1,d1
        move.b  d1,(a0)
.print: bsr     print_option
.done:  rts
        
        ; d0 = line #
        ; all regs prserved
clear_line:
        movem.l d0/a0,-(sp)
        mulu.w  #yperline,d0
        add.w   #ystart,d0
        mulu.w  #xres/8,d0
        move.l  bpl0(a4),a0
        adda.w  d0,a0
        bsr     waitblit
        move.l  #$01000000,$40(a6) ; bltcon0/bltcon1
        move.l  a0,$54(a6)         ; bltdpt
        move.w  #0,$66(a6)         ; bltdmod
        move.w  #(xres/16)|(yperline<<6),$58(a6)
        move.l  bpl1(a4),a0
        adda.w  d0,a0
        bsr     waitblit
        move.l  a0,$54(a6)         ; bltdpt
        move.w  #(xres/16)|(yperline<<6),$58(a6)
        movem.l (sp)+,d0/a0
        rts
        
clear_screen:
        bsr     waitblit
        move.l  #$01000000,$40(a6) ; bltcon0/bltcon1
        move.l  bpl0(a4),a0
        move.l  a0,$54(a6)         ; bltdpt
        move.w  #0,$66(a6)         ; bltdmod
        move.w  #(xres/16)|((yres*planes)<<6),$58(a6)
        bra     waitblit

        ; Unpack 8*8 font into destination
        ; Each char 00..7f is copied in sequence
        ; Destination is 10 longwords (= 10 rows) per character
        ; First word of each long is foreground, second word is background
        ; Background is computed from foreground
unpack_font:
        lea.l   packfont(pc),a0
        move.l  font(a4),a1
        move.l  a1,a2
        move.w  #$20*yperline-1,d0
.clr:   clr.l   (a1)+           ; first $20 chars are blank
        dbf     d0,.clr
        moveq   #$60-1,d0
.lop1:  clr.l   (a1)+           ; first row of foreground is blank
        moveq   #yperline-3,d1
.lop2:  moveq   #0,d2
        move.b  (a0)+,d2
        lsl.w   #7,d2           ; foreground character is shifted right
        swap    d2              ; one place from leftmost
        move.l  d2,(a1)+
        dbf     d1,.lop2
        clr.l   (a1)+           ; last row of foreground is blank
        dbf     d0,.lop1
        move.l  a2,a1
        moveq   #$80-1,d0
.lop3:  move.w  (a1)+,d2
        or.w    2(a1),d2
shift_lr MACRO
        move.w  d2,d3
        add.w   d3,d3
        or.w    d2,d3
        lsr.w   #1,d2
        or.w    d3,d2
        move.w  d2,(a1)+
        ENDM
        shift_lr
        moveq   #yperline-3,d1
.lop4:  move.w  -4(a1),d2
        or.w    (a1)+,d2
        or.w    2(a1),d2
        shift_lr
        dbf     d1,.lop4
        move.w  -4(a1),d2
        or.w    (a1)+,d2
        shift_lr
        dbf     d0,.lop3
        rts

print_screen:
        lea.l   str(pc),a3
.lop:   tst.b   (a3)
        bmi     .done
        bsr     print_line
        bra     .lop
.done:  rts

setup_options:
        lea.l   opts(pc),a0
        lea.l   options(a4),a1
        moveq   #0,d0
.lop:   move.l  a0,(a1)+
        move.b  (a0),d1
        bmi     .done
        bsr     print_option
        addq    #1,d0
        bra     .lop
.done:  clr.l   -4(a1)
        rts

        ; a0 = option (updated at end to point past end of option)
        ; d0 = option #
        ; all regs preserved
opt_col1        equ     8
opt_row1        equ     5
opt_confcol     equ     37
print_option:
        movem.l d0-d3/a3,-(sp)
        lea.l   string(a4),a3
        move.b  (a0)+,d2
        move.b  (a0)+,d3
        move.b  #opt_col1,(a3)+
        move.b  d0,d1
        add.b   #opt_row1,d0
        move.b  d0,(a3)+
        bsr     clear_line
        move.b  #'F',(a3)+
        add.b   #'1',d1
        move.b  d1,(a3)+
        move.b  #' ',(a3)+
        move.b  #'-',(a3)+
        move.b  #' ',(a3)+
        moveq   #opt_confcol-13,d0
.l1:    subq.l  #1,d0
        move.b  (a0)+,(a3)+
        bne     .l1
        sub.w   #1,a3
.l2:    move.b  #'.',(a3)+
        dbf     d0,.l2
        move.b  d2,d3
        and.b   #3,d3
        cmp.b   #1,d3
        beq     .range
.bool:  move.b  #'O',(a3)+
        and.b   #$10,d2
        bne     .bon
        move.b  #'F',(a3)+
        move.b  #'F',(a3)+
        bra     .done
.bon:   move.b  #'N',(a3)+
        move.b  #' ',(a3)+
        bra     .done
.range: move.b  #' ',(a3)+
        lsr.b   #4,d2
        cmp.b   #10,d2
        bmi     .onedig
        move.b  #'1',-1(a3)
        sub.b   #10,d2
.onedig:add.b   #'0',d2
        move.b  d2,(a3)+
.done:  move.b  #0,(a3)
        lea.l   string(a4),a3
        bsr     print_line
        movem.l (sp)+,d0-d3/a3
        rts
        
        ; a3 = string (points at string end on exit)
        ; all other registers preserved
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
.lop:   move.b  (a3)+,d2
        beq     .done
        bsr     print_char
        add.w   #8,d0
        bra     .lop
.done:  movem.l (sp)+,d0-d1/a0-a1
        rts                
        
        ; d0.w = x; d1.w = y; d2.w = char
        ; a0-a1 trashed
print_char:
        movem.l d0-d2,-(sp)
        and.w   #$7f,d2
        mulu.w  #yperline*4,d2
        addq    #2,d2
        move.l  font(a4),a0
        adda.w  d2,a0           ; a0 = points to correct font char
        mulu.w  #xres/8,d1
        move.w  d0,d2
        lsr.w   #4,d2
        add.w   d2,d1
        add.w   d2,d1
        move.l  bpl0(a4),a1
        adda.w  d1,a1           ; a1 = points to correct first dest word
        moveq   #0,d2
        move.b  d0,d2
        and.b   #15,d2
        ror.w   #4,d2
        or.w    #$0dfc,d2       ; ABD DMA, D=A|B
        swap    d2
        bsr     waitblit
        move.l  d2,$40(a6)      ; bltcon0 (D=A|B) / bltcon1
        move.l  #(xres/8)-4,$60(a6); bltcmod/bltbmod
        move.l  #(xres/8)-4,$64(a6); bltamod/bltdmod
        move.l  #$ffff0000,$44(a6) ; bltafwm/bltalwm
        moveq   #1,d0
.lop:   bsr     waitblit
        move.l  a0,$50(a6)         ; bltapt
        move.l  a1,$4c(a6)         ; bltbpt
        move.l  a1,$54(a6)         ; bltdpt
        move.w  #2|(yperline<<6),$58(a6) ; bltsize = 2 words * yperline rows
        suba.w  #2,a0
        move.l  bpl1(a4),a1
        adda.w  d1,a1
        dbf     d0,.lop
        movem.l (sp)+,d0-d2
        rts

wait_bos:
        cmp.b   #$f0,6(a6)      ; wait for end of bitplane DMA
        bne     wait_bos
        rts
        
waitblit:
        btst.b  #6,2(a6)
.wait:  btst.b  #6,2(a6)        ; wait for idle blitter
        bne     .wait
        rts
        
wait_line:
        move.b  $6(a6),d0
.wait:  cmp.b   $6(a6),d0
        beq     .wait
        rts
        
CIA_IRQ:
        movem.l d0-d1/a0,-(sp)
        move.b  $d00(a5),d0
        btst    #3,d0           ; ciaa.icr - SDR finished a byte?
        beq     .done
        move.b  $c00(a5),d1     ; grab the keyboard byte if so
        bset.b  #6,$e00(a5)     ; start the handshake
        not.b   d1
        ror.b   #1,d1
        move.b  d1,key(a4)      ; fix up and save the key code
        bsr     wait_line
        bsr     wait_line       ; wait ~100us
        bclr.b  #6,$e00(a5)     ; finish the handshake
.done:  movem.l (sp)+,d0-d1/a0
        ; NB. Clear intreq.ciaa *after* reading/clearing ciaa.icr else we
        ; get a spurious extra interrupt, since intreq.ciaa latches the level
        ; of CIAA INT and hence would simply become set again immediately
        ; after we clear it. For this same reason (latches level not edge) it
        ; is *not* racey to clear intreq.ciaa second. Indeed AmigaOS does the
        ; same (checked Kickstart 3.1).
        move.w  #$0008,$9c(a6)
        rte
        
copper:
        dc.l    $008e4681       ; diwstrt.v = $46
        dc.l    $0090efc1       ; diwstop.v = $ef (169 lines)
        dc.l    $0092003c       ; ddfstrt
        dc.l    $009400d4       ; ddfstop
        dc.l    $0100a200       ; bplcon0
        dc.l    $01020000       ; bplcon1
        dc.l    $01040000       ; bplcon2
        dc.l    $01080000       ; bpl1mod
        dc.l    $010a0000       ; bpl2mod
        dc.l    $00e00000       ; bpl1pth
        dc.l    $00e20000       ; bpl1ptl
        dc.l    $00e40000       ; bpl2pth
        dc.l    $00e60000       ; bpl2ptl
        dc.l    $01820222       ; col01
        dc.l    $01840ddd       ; col02
        dc.l    $01860ddd       ; col03
        dc.l    $01800103       ; col00
        dc.l    $4407fffe
        dc.l    $01800ddd
        dc.l    $4507fffe
        dc.l    $01800402
        dc.l    $f007fffe
        dc.l    $01800ddd
        dc.l    $f107fffe
        dc.l    $01800103
        dc.l    $fffffffe

packfont:   incbin  "FONT2_8X8.BIN"
str:    dc.b 13, 0,             "=====================\0"
        dc.b 11, 1,           "+ THE NEW ZEALAND STORY +\0"
        dc.b 13, 2,             "=====================\0"
        dc.b  6, 3,      "Cracked & Trained by KAF in June '11\0"
        dc.b  0,10,"Space, Mouse button, or Joystick Fire to Continue!\0"
        dc.b $ff

RANGE_OPT MACRO
        dc.b 1|(\1<<4),\2|(\3<<4)
        ENDM
BOOL_OPT MACRO
        dc.b 2|(\1<<4),0
        ENDM
opts:   BOOL_OPT 0
        dc.b "Infinite Lives\0"
        RANGE_OPT 3,1,10
        dc.b "Initial Lives\0"
        BOOL_OPT 0
        dc.b "Load/Save Highscores\0"
        BOOL_OPT 0
        dc.b "Reset Saved Highscores\0"
        dc.b $ff
        
        rsreset
key     rs.b    1
pad     rs.b    1
bpl0    rs.l    1
bpl1    rs.l    1
font    rs.l    1
options rs.l    10
string  rs.b    80
size    rs.b    0

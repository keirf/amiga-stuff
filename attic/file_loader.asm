mfm_bytes       equ $3200

loc_cylinder    equ 0
loc_track       equ 1

        movem.l d0-a6,-(sp)
        lea     ($BFD100).l,a5
        bsr     motors_off
        lea     unk_7EA93(pc),a6
        andi.b  #$7F,(a5)
        move.b  (a6)+,d0
        move.l  a0,2(a6)
        st      6(a6)
        and.b   d0,(a5)
        move.w  #$370,d0
        bsr.w   load_sector
        movea.l a1,a2

loc_7E8AE:
        tst.b   (a2)+
        bne.s   loc_7E8AE
        suba.l  a1,a2
        subq.w  #1,a2
        move.l  a2,d2
        moveq   #0,d3
        move.l  d2,d4
        move.l  d4,d6
        movea.l a1,a3

loc_7E8C0:
        mulu.w  #$D,d2
        move.b  (a1)+,d3
        cmpi.b  #$61,d3 ; 'a'
        bcs.s   loc_7E8D6
        cmpi.b  #$7A,d3 ; 'z'
        bhi.s   loc_7E8D6
        subi.b  #$20,d3

loc_7E8D6:
        add.w   d3,d2
        andi.w  #$7FF,d2
        subq.w  #1,d4
        bne.s   loc_7E8C0
        divu.w  #$48,d2
        swap    d2
        addq.w  #6,d2
        lsl.w   #2,d2
        move.l  (a0,d2.w),d0

loc_7E8EE:
        bsr.w   load_sector
        lea     $1B0(a0),a1
        cmp.b   (a1)+,d6
        bne.s   loc_7E93A
        movea.l a3,a2
        move.w  d6,d0

loc_7E8FE:
        cmpm.b  (a1)+,(a2)+
        bne.s   loc_7E93A
        subq.w  #1,d0
        bne.s   loc_7E8FE
        move.l  $144(a0),d0
        add.l   dword_7EA96(pc),d0
        move.l  d0,6(a6)

loc_7E912:
        move.l  $10(a0),d0
        beq.s   loc_7E940
        bsr.w   load_sector
        lea     $18(a0),a1
        movea.l dword_7EA96(pc),a2
        move.l  dword_7EA9A(pc),d0
        move.w  #$1E7,d7

loc_7E92C:
        move.b  (a1)+,(a2)+
        cmpa.l  d0,a2
        dbeq    d7,loc_7E92C
        move.l  a2,2(a6)
        bra.s   loc_7E912

loc_7E93A:
        move.l  $1F0(a0),d0
        bne.s   loc_7E8EE

loc_7E940:
        bsr.s   motors_off
        movem.l (sp)+,d0-a6
        rts

motors_off:
        ori.b   #$F8,(a5)
        andi.b  #$87,(a5)
        ori.b   #$78,(a5)
        rts

_step_one_out:
        bset    d7,(a5)         ; seek outwards (bset #1,bfd100)
        bsr     step_one
_seek_cyl0:
        btst    #4,$F01(a5)
        bne     _step_one_out
        sf      (a6)            ; loc_cylinder(a6) = 0

seek_cylinder:
        tst.b   (a6)            ; loc_cylinder(a6) < 0?
        bmi     _seek_cyl0      ; resync heads if so
.check_cyl:
        cmp.b   (a6),d2
        beq     wait_dskrdy     ; current cyl is correct: bail
        bcc     .seek_inward    ; current cyl too low: seek inward
        bset    d7,(a5)
        subq.b  #1,(a6)
        bra     .seek_outward
.seek_inward:
        bclr    d7,(a5)
        addq.b  #1,(a6)
.seek_outward:
        bsr     step_one
        bra     .check_cyl

step_one:
        bclr    d3,(a5)
        mulu.w  d3,d3
        bset    d3,(a5)         ; step pulse
        move.b  d3,$500(a5)
        move.b  #$10,$600(a5)
        move.b  #$19,$E00(a5)   ; CIA delay
.wait_cia:
        btst    d3,$E00(a5)
        bne     .wait_cia

wait_dskrdy:
        btst    #5,$F01(a5)
        bne     wait_dskrdy
        rts

        ;  d0 = sector #, a1 = dest
load_sector:
        moveq   #0,d3
        moveq   #1,d7
        moveq   #2,d5
        movea.l #mfmbuff,a4
        move.l  d0,d2
        ext.l   d2
        divu.w  #$B,d2
        move.l  d2,d4
        swap    d4
        cmp.b   loc_track(a6),d2
        beq     _decode_mfm
        move.b  d2,loc_track(a6)
        bset    d5,(a5)
        btst    d3,d2
        beq     .side0
        bclr    d5,(a5)
.side0: lsr.w   #1,d2
        bsr     seek_cylinder
        move.b  #5,-2(a6)
_fail_retry:
        subq.b  #1,-2(a6)
        bne.s   _read_mfm
        moveq   #-1,d2
        move.w  d2,(a6)
        bra.s   load_sector
_read_mfm:
        movea.l #mfmbuff,a4
        lea     ($DFF020).l,a0
        move.l  a4,(a0)+        ; dskpt
        move.w  #$8210,$72(a0)  ; dmacon -- enable disk dma
        move.l  #$27F00,$78(a0) ; clear intreq & adkcon
        move.w  #$9500,$7A(a0)  ; adkcon -- MFM, wordsync
        move.w  #$4489,$5A(a0)  ; sync 4489
        move.w  #$8000+mfm_bytes/2,(a0)
        move.w  #$8000+mfm_bytes/2,(a0)     ; dsklen -- 0x1900 words
        move.l  #$A00000,d7
.wait_dma:
        subq.l  #1,d7
        beq     _fail_retry
        btst    #1,-5(a0)       ; intreqr -- disk dma done?
        beq     .wait_dma
        move.w  #$4000,(a0)     ; dsklen -- no more dma

        ; d4.w = sector #
_decode_mfm:
        lea     mfm_bytes-$438(a4),a2
        move.l  #$55555555,d5
.next_sector:
        cmpa.l  a4,a2
        bmi     _fail_retry
        cmpi.w  #$4489,(a4)+
        bne     .next_sector
        cmpi.w  #$4489,(a4)
        beq     .next_sector
        movem.l (a4),d2-d3
        bsr     decode_mfm_long
        lsr.w   #8,d2           ; d2.w = sector #
        cmp.w   d4,d2
        beq     .sector_found
        lea.l   $438(a4),a4     ; skip this sector
        bra     .next_sector
.sector_found:
        swap    d2              ; d2.b = track #
        cmp.b   loc_track(a6),d2
        bne     _fail_retry     ; wrong track?!
        lea     $30(a4),a4
        move.l  (a4)+,d4
        move.l  (a4)+,d3
        eor.l   d3,d4           ; d4.l = data checksum
        moveq   #$7F,d7
.next_data_long:
        move.l  $200(a4),d3
        move.l  (a4)+,d2
        eor.l   d2,d4
        eor.l   d3,d4
        bsr     decode_mfm_long
        move.l  d2,(a1)+
        dbf     d7,.next_data_long
        and.l   d5,d4
        bne     _fail_retry
        rts

        ; d2 = decode_mfm_long(d2 = odd, d3 = even, d5 = 0x55555555)
decode_mfm_long:
        and.l   d5,d2
        and.l   d5,d3
        add.l   d2,d2
        or.l    d3,d2
        rts

        dc.b   5
unk_7EA93:      dc.b $F7
        dc.b $FF
        dc.b $FF
dword_7EA96:    dc.l $ffffffff
dword_7EA9A:    dc.l $ffffffff
mfmaddr:        dc.l $0
filename:       dc.b "TEST",0
        even
mfmbuff:
loadbuff:


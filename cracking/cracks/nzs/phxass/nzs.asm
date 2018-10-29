SSP     equ     $80000
USP     equ     $7fc00
CODE    equ     $7e000

start:
        ; Initialise custom chips
        lea.l   ($dff000).l,a6
        move.w  #$7fff,d0
        move.w  d0,$9a(a6)  ; intena = 0
        move.w  d0,$9c(a6)  ; intreq = 0
        move.w  d0,$96(a6)  ; dmacon = 0
        move.w  #$8200,$96(a6)  ; enable master DMA
        move.w  #$c000,$9a(a6)  ; enable master IRQ
        moveq   #0,d0
        move.w  d0,$180(a6)     ; color0 = black

        ; Initialise CPU state
        lea.l   .priv(pc),a0
        move.l  a0,($20).w
.priv:  move.w  #$2700,sr       ; SR = $2700 (supervisor mode, no interrupts)
        lea.l   .skip(pc),a0
        move.l  a0,($10).w
        dc.l    $4e7b0002       ; movec.l d0,cacr (CACR = 0)
.skip:  lea.l   (SSP).l,sp      ; SSP
        lea.l   (USP).l,a0
        move.l  a0,usp          ; USP

        lea.l   main_copy(pc),a0
        move.l  a0,d0           ; a0 = d0 = current location
        move.l  #CODE,d1        ; d1 = destination
        moveq   #4,d2
        swap    d2              ; d2 = $40000 (256kB)
        eor.l   d1,d0           ; *Very* conservative test: could current
        and.l   d2,d0           ; location and destination overlap?
        bne     main_copy
        eor.l   d2,d1           ; Temp. copy to other half of bottom 512kB
        move.l  d1,a1
        eor.l   d2,d1
        move.w  #(end-main_copy)>>2,d0
        bra     _copy

main_copy:
        lea.l   main(pc),a0    ; a0 = current location
        move.l  d1,a1           ; a1 = destination
        move.w  #(end-main)>>2,d0
_copy:  move.l  a1,a2
.loop:  move.l  (a0)+,(a1)+
        dbf     d0,.loop
        jmp     (a2)

main:   move.w  #$2000,sr       ; allow CPU interrupts now that we are
                                ; definitely executing clear of the stack

        lea     ($BFD100).l,a5
        bsr     motors_off

        bsr     trainer
        ; d0 = Inf Lives | # Lives | Load/Save Hi | Reset Hi
        lea.l   config_options(pc),a4
        btst    #0,d0
        sne     config_reset_hi(a4)
        btst    #4,d0
        sne     config_save_hi(a4)
        btst    #12,d0
        sne     config_inf_lives(a4)
        lsr.w   #8,d0
        and.b   #15,d0
        move.b  d0,config_nr_lives(a4)

        ; Zap high score load area
        lea.l   ($60000).l,a1
        move.w  #$57f,d1
.lop    clr.l   (a1)+
        dbf     d1,.lop

        ; If resetting saved high scores, zero out disk track 1
        tst.b   config_reset_hi(a4)
        beq     .skipzap
        lea.l   ($60000).l,a0
        moveq   #1,d0
        bsr     mfm_encode_track
        bsr     write_track     
.skipzap:
        
        ; If loading high scores, stash disk track 1 at $60000
        tst.b   config_save_hi(a4)
        beq     .skiphiscore
        moveq   #1,d0           ; Load track 1
        moveq   #1,d1           ; (just the one track)
        lea.l   ($60000).l,a0   ; Load it to $60000
        bsr     nzs_load_tracks
.skiphiscore:
        
        ; Load track 90 to 0x75ffc
        moveq   #90,d0          ; Load track 90
        moveq   #1,d1           ; (just the one track)
        lea.l   ($75ffc).l,a0   ; Load it to $75ffc
        bsr     nzs_load_tracks
        tst.w   d0
        bpl     .okay
.crash: move.w  #$f00,$180(a6)
        bra     .crash

.okay:  move.l  #$76000,a5      ; Our jump address

        ; Copy our payload over the defunct Copylock
        lea.l   PATCH1_START(pc),a0
        lea.l   PATCH1_END(pc),a2
        lea.l   $64(a5),a1
.copy:  move.l  (a0)+,(a1)+
        cmp.l   a0,a2
        bpl     .copy

        ; Patch the old track loader to jump at us
        ; 76698: bsr.w nzs_load_tracks
        move.w  #$6100,$698(a5)                 ; bsr.w xxx
        move.w  #nzs_load_tracks-PATCH1_START,d0; do this in 3 steps...
        add.w   #$64-$69a,d0                    ; ...because PhxAss...
        move.w  d0,$69a(a5)                     ; ...is buggy :-(

        ; Patch out the existing drive motor/head routines (using rts)
        move.w  #$4e75,$764(a5) ; motor_on
        move.w  #$4e75,$798(a5) ; motor_off
        move.w  #$4e75,$9d2(a5) ; seek_track0
        
        ; Patch the final jump calculation to jump at us for final patching
        ; 7668e: bra.w patch2
        move.w  #$6000,$68e(a5)                 ; bra.w xxx
        move.w  #patch2-PATCH1_START,d0
        add.w   #$64-$690,d0
        move.w  d0,$690(a5)

        jmp     (a5)

PATCH1_START:    
        ; (We are in place of the Copylock)
        ; Fake the Copylock by hardwiring the key and jumping straight past
        move.l  #$f974db7d,d0
        move.l  d0,($24).w
        jmp     ($76668).l

patch2: ; (We jump here instead of the final jump to $400)
        ; Copy our track loader over the game's loader
        lea.l   PATCH2_START(pc),a0
        lea.l   PATCH2_END(pc),a2
        lea.l   ($c28).w,a1
.copy:  move.l  (a0)+,(a1)+
        cmp.l   a0,a2
        bpl     .copy

        ; TRAINER 1. Infinite lives
        lea.l   config_options(pc),a4
        tst.b   config_inf_lives(a4)
        beq     .skip_inf_lives
        moveq   #4,d0
        lea.l   .lives_dec(pc),a0
.nop:   move.l  (a0)+,a1
        move.l  #$4e714e71,(a1) ; NOP out lives decrement
        dbf     d0,.nop
.skip_inf_lives:

        ; TRAINER 2. Start with N lives
        moveq   #0,d0
        move.b  config_nr_lives(a4),d0
        move.w  d0,($4fba).w

        ; HIGHSCORE SAVE
        tst.b   config_save_hi(a4)
        beq     .skip_hi
        move.w  #$4ef8,($4736).w ; jmp (xxx).w
        move.w  #$0c24,d0        ; copy destination of nzs_load_tracks,
                                 ; adjusted for subsequent 4-byte relocation
        add.w   #nzs_save_hiscore-nzs_load_tracks,d0
        move.w  d0,($4738).w

        ; HIGHSCORE LOAD
        lea.l   ($60000).l,a0   ; We stashed track 1 contents here
        lea.l   ($4820).w,a1    ; This is where the data belongs in the game
        cmp.l   #$68697363,(a0)+
        bne     .skip_hi        ; Skip if track 1 doesn't have our signature
        moveq   #$1f,d1
.lop    move.l  (a0)+,d0        ; Grab some data...
        eor.l   #$f381a092,d0   ; ...decrypt it...
        move.l  d0,(a1)+        ; ...poke it
        dbf     d1,.lop
        
        ; Finish with the final jump to the game
.skip_hi: jmp   ($400).w
       
.lives_dec: ; Addresses of lives decrement insns
        dc.l    $79c8,$8a72,$8b2e,$8bee,$990a
        
        rsreset
config_reset_hi         rs.b    1
config_save_hi          rs.b    1
config_nr_lives         rs.b    1
config_inf_lives        rs.b    1
config_size             rs.b    0
config_options:         dcb.b   config_size

PATCH2_START:
        ; ********* TRACK LOADER **********
        ; IN: a0 = load_address; d0 = start track; d1 = nr tracks
        ; OUT: d0 = 0/-1 (0=success); all other registers preserved
nzs_load_tracks:
        ; Constants from the original NZS loader
mfm_bytes       equ $332c
mfm_addr        equ $68000
        move.l  #mfm_addr,a1
        exg.l   a0,a1
        bsr     load_tracks
        exg.l   a0,a1
        rts
        
loc_cylinder    equ 0
loc_track       equ 1

        ; IN: a0 = mfmbuf, a1 = dest, d0 = start track, d1 = nr tracks
        ; OUT: d0 = 0/-1 (0=success); all other registers preserved
load_tracks:
        movem.l d0-d6/a0-a1/a4-a6,-(sp)

        lea     ($DFF000).l,a4
        lea     ($BFD100).l,a5
        lea     locals(pc),a6
        move.l  #$55555555,d5
        st      loc_track(a6)   ; mfmbuf has unknown contents, so scratch
                                ; the buffered track #
        bsr     motor_on

        move.w  2(sp),d0
        move.w  6(sp),d1
        mulu.w  #11,d0
        mulu.w  #11,d1
        moveq   #-1,d2
        move.l  d2,(sp)         ; initialise return code (failure)
.next:  bsr     load_sector
        beq     .bail           ; CC_Z=1 on failure
        lea.l   $200(a1),a1
        addq.w  #1,d0
        subq.w  #1,d1
        bne     .next
        clr.l   (sp)            ; success, return d0=0
.bail:  bsr     motors_off
        movem.l (sp)+,d0-d6/a0-a1/a4-a6
        rts

        ; Turn on DF0 motor only, wait for DSKRDY, or 500ms to pass
        ; scratches d0-d1
motor_on:
        ori.b   #$F8,(a5)
        andi.b  #$7F,(a5)
        andi.b  #$F7,(a5)
        move.w  #8000,d1        ; 8000 * 63us ~= 500ms
.wait:  btst    #5,$F01(a5)
        beq     .done
        bsr     wait_vline
        dbf     d1,.wait
.done:  rts
        
motors_off:
        ori.b   #$F8,(a5)
        andi.b  #$87,(a5)
        ori.b   #$78,(a5)
        rts

_step_one_out:
        bsr     step_one
_seek_cyl0:
        btst    #4,$F01(a5)
        bne     _step_one_out
        sf      (a6)            ; loc_cylinder(a6) = 0

        ; d2 = track, d0-d2 scratch
seek_track:
        bset    #1,(a5)         ; seek outwards by default
        tst.b   (a6)            ; loc_cylinder(a6) < 0?
        bmi     _seek_cyl0      ; resync heads if so
        moveq   #2,d0
        bset    d0,(a5)         ; side 0
        lsr.w   #1,d2           ; d2.w = cyl#
        bcc     .check_cyl
        bclr    d0,(a5)         ; side 1
.check_cyl:
        cmp.b   (a6),d2
        bne     .seek           ; current cyl is correct:
        move.w  #250,d1         ; ...then wait 250 * 63us ~= 15ms
.wait:  bsr     wait_vline      ; ...for drive settle
        dbf     d1,.wait
        rts
.seek:  bcs     .seek_outward   ; current cyl too high: seek outward
        bclr    #1,(a5)
        addq.b  #2,(a6)         ; +2 as we -1 straight after ;-)
.seek_outward:
        subq.b  #1,(a6)
        bsr     step_one
        bra     .check_cyl

step_one:
        moveq   #0,d0
        bclr    d0,(a5)
        mulu.w  d0,d0
        bset    d0,(a5)         ; step pulse
        move.w  #50,d1          ; 50 * 63us ~= 3ms
.wait:  bsr     wait_vline
        dbf     d1,.wait
        rts

wait_vline:
        move.b  $6(a4),d0
.wait:  cmp.b   $6(a4),d0
        beq     .wait
        rts

        ;  d0 = sector #, a0 = mfm, a1 = dest
        ;  CC_Z is set on failure
load_sector:
        movem.l d0-d1/a0-a1,-(sp)
        moveq   #16,d6           ; d6 = retry counter (16)
_load_sector:
        move.l  (sp),d2
        ext.l   d2
        divu.w  #$B,d2
        move.l  d2,d4           ; d2.w = track#
        swap    d4              ; d4.w = sector#
        cmp.b   loc_track(a6),d2
        beq     .decode_mfm     ; immediately start decode if track is buffered
        move.b  d2,loc_track(a6)
        bsr     seek_track
        move.l  8(sp),$20(a4)   ; dskpt
        move.w  #$8210,$96(a4)  ; dmacon -- enable disk dma
        move.l  #$27F00,$9c(a4) ; clear intreq & adkcon
        move.w  #$9500,$9e(a4)  ; adkcon -- MFM, wordsync
        move.w  #$4489,$7e(a4)  ; sync 4489
        move.w  #$8000+mfm_bytes/2,$24(a4)
        move.w  #$8000+mfm_bytes/2,$24(a4)     ; dsklen -- 0x1900 words
        move.w  #16000,d1       ; 16000 * 63us ~= 1 second
.wait:  subq.w  #1,d1
        beq     .fail_retry
        bsr     wait_vline
        btst    #1,$1f(a4)      ; intreqr -- disk dma done?
        beq     .wait
        move.w  #$4000,$24(a4)  ; dsklen -- no more dma
.decode_mfm:
        move.l  8(sp),a0                ; a0 = mfm start
        lea     mfm_bytes-$438(a0),a1   ; a1 = mfm end - 1 sector
.next_sector:
        cmpi.w  #$4489,(a0)     ; skip 4489 sync
        beq     .find_sector
        movem.l (a0),d0-d1
        bsr     decode_mfm_long
        lsr.w   #8,d0           ; d0.w = sector #
        cmp.w   d4,d0
        beq     .sector_found
        lea.l   $438(a0),a0     ; skip this sector
.find_sector:
        cmpa.l  a0,a1           ; bail if we scan to end of mfm buffer
        bmi     .fail_retry
        cmpi.w  #$4489,(a0)+
        bne     .find_sector
        bra     .next_sector
.sector_found:
        swap    d0              ; d0.b = track #
        cmp.b   loc_track(a6),d0
        bne     .fail_retry     ; wrong track?!
        lea     $30(a0),a0
        move.l  (a0)+,d4
        move.l  (a0)+,d0
        eor.l   d0,d4           ; d4.l = data checksum
        moveq   #$7F,d2
        move.l  12(sp),a1       ; a1 = destination
.next_data_long:
        move.l  $200(a0),d1
        move.l  (a0)+,d0
        eor.l   d0,d4
        eor.l   d1,d4
        bsr     decode_mfm_long
        move.l  d0,(a1)+
        dbf     d2,.next_data_long
        and.l   d5,d4
        bne     .fail_retry
.fail:  movem.l (sp)+,d0-d1/a0-a1
        tst.b   d6
        rts

.fail_retry:
        move.w  #$4000,$24(a4)  ; dsklen -- no more dma
        st      loc_track(a6)   ; scratch the buffered track
        subq.b  #1,d6
        beq     .fail
        moveq   #3,d0
        and.b   d6,d0           ; every four retries...
        bne     .nosync
        st      (a6)            ; ...we resync the drive heads
.nosync:bra     _load_sector    ; ..so we resync via track 0

        ; d0 = decode_mfm_long(d0 = odd, d1 = even, d5 = 0x55555555)
decode_mfm_long:
        and.l   d5,d0
        and.l   d5,d1
        add.l   d0,d0
        or.l    d1,d0
        rts

locals: dc.b $FF                ; loc_cylinder: current cylinder
        dc.b $FF                ; loc_track: current track

nzs_save_hiscore:
        movem.l d0-d1/a0-a1,-(sp)
        lea.l   ($4820).w,a0
        lea.l   ($68000).l,a1
        move.l  #$68697363,(a1)+       
        moveq   #$1f,d1
.lop    move.l  (a0)+,d0
        eor.l   #$f381a092,d0
        move.l  d0,(a1)+
        dbf     d1,.lop
        move.w  #$55f,d1
.lop2   clr.l   (a1)+
        dbf     d1,.lop2
        lea.l   ($68000).l,a0
        moveq   #1,d0
        bsr     mfm_encode_track
        bsr     write_track     
        movem.l (sp)+,d0-d1/a0-a1
        tst.w   $2c(a6)         ; from the code we patched over
        rts
        
        ; a0 = mfm buffer; d0 = track#; all regs preserved
write_track:
        movem.l d0-d2/a4-a6,-(sp)
        lea     ($DFF000).l,a4
        lea     ($BFD100).l,a5
        lea     locals(pc),a6
        move.l  d0,d2           ; stash track# in d2 for seek_track
        ori.b   #$F8,(a5)
        andi.b  #$F7,(a5)       ; select drive 0 (motor off for now)
        btst    #3,$F01(a5)     ; bail if disk is write protected
        beq     .done
        bsr     motor_on
        bsr     seek_track
        move.l  a0,$20(a4)      ; dskpt
        move.w  #$8210,$96(a4)  ; dmacon -- enable disk dma
        move.l  #$27F00,$9c(a4) ; clear intreq & adkcon
        cmp.b   #40,(a6)
        bcs     .noprec
        move.w  #$a000,$9e(a4)  ; adkcon -- 140ns precomp for cylinders 40-79
                                ; (exactly the same as trackdisk.device, tested
                                ;  on Kickstart 3.1)
.noprec:move.w  #$9100,$9e(a4)  ; adkcon -- MFM, no wordsync
        move.w  #$c000+mfm_bytes/2,$24(a4)
        move.w  #$c000+mfm_bytes/2,$24(a4)     ; dsklen
        move.w  #16000,d1       ; 16000 * 63us ~= 1 second
.wait:  subq.w  #1,d1
        beq     .done
        bsr     wait_vline
        btst    #1,$1f(a4)      ; intreqr -- disk dma done?
        beq     .wait
.done:  move.w  #$4000,$24(a4)  ; dsklen -- no more dma
        bsr     motors_off
        movem.l (sp)+,d0-d2/a4-a6
        rts

        ; a0.l = buffer to encode ; d0.b = track #
        ; All registers are preserved
mfm_encode_track:
        movem.l d0-d7/a0-a1,-(sp)
        move.l  #$55555555,d5
        move.l  #$aaaaaaaa,d6
        lea.l   mfm_bytes(a0),a1
        lea.l   $1600(a0),a0
        move.w  #10,d7
.sect:  moveq   #$7f,d2
        moveq   #0,d3
        move.l  d3,-(a1)        ; sector gap
        lea.l   -$200(a1),a1
.lop:   move.l  -(a0),d0
        bsr     encode_mfm_long
        eor.l   d0,d3
        eor.l   d1,d3
        move.l  d0,-(a1)        ; even data bits
        move.l  d1,$200(a1)     ; odd data bits
        dbf     d2,.lop
        and.l   d5,d3
        move.l  d3,d0
        bsr     encode_mfm_long
        movem.l d0-d1,-(a1)     ; data checksum
        moveq   #0,d0
        moveq   #9,d1
.lop2:  move.l  d0,-(a1)        ; header checksum + sector label
        dbf     d1,.lop2
        move.w  #$ff00,d0       ; info.format = 0xff
        move.b  3(sp),d0        ; info.track
        swap    d0
        move.b  d7,d0           ; info.sector
        lsl.w   #8,d0
        move.b  #11,d0
        sub.b   d7,d0           ; info.sectors_to_gap
        bsr     encode_mfm_long
        movem.l d0-d1,-(a1)     ; sector info long
        eor.l   d1,d0
        bsr     encode_mfm_long
        movem.l d0-d1,40(a1)    ; header checksum
        move.l  #$44014401,-(a1)
        move.w  #271,d2
.clk:   move.l  (a1),d0         ; get a longword of data bits
        move.l  d0,d1
        roxr.l  #1,d0           ; d0 = (X . data_bits) >> 1 -> X
        rol.l   #1,d1           ; d1 = data_bits << 1
        or.l    d0,d1
        not.l   d1              ; clock[n] = data[n-1] NOR data[n]
        and.l   d6,d1
        or.l    d1,(a1)+        ; OR the clock bits into the longword
        dbf     d2,.clk
        lea.l   -1088(a1),a1
        move.l  #$44894489,(a1) ; sync marker
        dbf     d7,.sect
        move.l  #$aaaaaaaa,d0
.lop3:  move.l  d0,-(a1)
        cmpa.l  a1,a0
        bmi     .lop3
        movem.l (sp)+,d0-d7/a0-a1
        rts

encode_mfm_long:
        move.l  d0,d1
        lsr.l   #1,d0
        and.l   d5,d0
        and.l   d5,d1
        rts
PATCH2_END:
PATCH1_END:
        include "trainer.asm"
end:

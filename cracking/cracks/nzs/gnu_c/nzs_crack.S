        .struct 0
#define RSB(label, x) label : .struct label + x
#define RSL(label, x) label : .struct label + 4*x
RSB(_config_reset_hi, 1)
RSB(config_save_hi,   1)
RSB(config_nr_lives,  1)
RSB(config_inf_lives, 1)
RSB(config_size,      0)

        .section .text.init

#define config_reset_hi /* 0 */ /* First offset in BSS */

        .global start
start:
        /* Initialise custom chips */
        lea.l   (0xdff000).l,a6
        move.w  #0x7fff,d0
        move.w  d0,0x9a(a6)  /* intena = 0 */
        move.w  d0,0x9c(a6)  /* intreq = 0 */
        move.w  d0,0x96(a6)  /* dmacon = 0 */
        move.w  #0x8200,0x96(a6)  /* enable master DMA */
        move.w  #0xc000,0x9a(a6)  /* enable master IRQ */
        moveq   #0,d0
        move.w  d0,0x180(a6)     /* color0 = black */

        /* Initialise CPU state */
        lea.l   .priv(pc),a0
        move.l  a0,(0x20).w
.priv:  move.w  #0x2700,sr      /* SR = 0x2700 (supervisor mode, no irqs) */
        lea.l   .skip(pc),a0
        move.l  a0,(0x10).w
        dc.l    0x4e7b0002      /* movec.l d0,cacr (CACR = 0) */
.skip:  lea.l   (SUPER_SP).l,sp /* SSP */
        lea.l   (USER_SP).l,a0
        move.l  a0,usp          /* USP */

        lea.l   start(pc),a0
        move.l  a0,d0           /* a0 = d0 = current location */
        move.l  #start,d1       /* d1 = destination */
        moveq   #4,d2
        swap    d2              /* d2 = 0x40000 (256kB) */
        eor.l   d1,d0           /* *Very* conservative test: could current */
        and.l   d2,d0           /* location and destination overlap? */
        jne     1f
        /* Temp. copy to other half of bottom 512kB */
        eor.l   d2,d1
        jsr     copy
        eor.l   d2,d1
        move.l  #1f,d0
        eor.l   d2,d0
        move.l  d0,a0
        jmp     (a0)
1:      /* Main copy to final relocation point */
        move.l  #main,-(sp)

copy:   lea.l   start(pc),a0    /* a0 = current location */
        move.l  d1,a1           /* a1 = destination */
        move.l  #_end-start,d0
        lsr.l   #2,d0
1:      move.l  (a0)+,(a1)+
        dbf     d0,1b
        rts

main:   move.w  #0x2000,sr      /* allow CPU interrupts now that we are */
                                /* definitely executing clear of the stack */

        lea     (0xBFD100).l,a5
        jbsr    motors_off

        jbsr    trainer
        /* d0 = Inf Lives | # Lives | Load/Save Hi | Reset Hi */
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

        /* Zap high score load area */
        lea.l   (0x60000).l,a1
        move.w  #0x57f,d1
1:      clr.l   (a1)+
        dbf     d1,1b

        /* If resetting saved high scores, zero out disk track 1 */
        tst.b   config_reset_hi(a4)
        jeq     .skipzap
        lea.l   (0x60000).l,a0
        moveq   #1,d0
        jbsr    mfm_encode_track
        jbsr    write_track     
.skipzap:
        
        /* If loading high scores, stash disk track 1 at 0x60000 */
        tst.b   config_save_hi(a4)
        jeq     .skiphiscore
        moveq   #1,d0           /* Load track 1 */
        moveq   #1,d1           /* (just the one track) */
        lea.l   (0x60000).l,a0  /* Load it to 0x60000 */
        jbsr    nzs_load_tracks
.skiphiscore:
        
        /* Load track 90 to 0x75ffc */
        moveq   #90,d0          /* Load track 90 */
        moveq   #1,d1           /* (just the one track) */
        lea.l   (0x75ffc).l,a0  /* Load it to 0x75ffc */
        jbsr    nzs_load_tracks
        tst.w   d0
        jpl     .okay
.crash: move.w  #0xf00,0x180(a6)
        jra     .crash

.okay:  move.l  #0x76000,a5     /* Our jump address */

        /* Copy our payload over the defunct Copylock */
        lea.l   PATCH1_START(pc),a0
        lea.l   PATCH1_END(pc),a2
        lea.l   0x64(a5),a1
1:      move.l  (a0)+,(a1)+
        cmp.l   a0,a2
        jcc     1b

        /* Patch the old track loader to jump at us */
        /* 76698: bsr.w nzs_load_tracks */
        move.w  #0x6100,0x698(a5)                 /* bsr.w xxx */
        move.w  #nzs_load_tracks-PATCH1_START,d0  /* do this in 3 steps... */
        add.w   #0x64-0x69a,d0                    /* ...because PhxAss... */
        move.w  d0,0x69a(a5)                      /* ...is buggy :-( */

        /* Patch out the existing drive motor/head routines (using rts) */
        move.w  #0x4e75,0x764(a5) /* motor_on */
        move.w  #0x4e75,0x798(a5) /* motor_off */
        move.w  #0x4e75,0x9d2(a5) /* seek_track0 */
        
        /* Patch the final jump calculation to jump at us for final patching */
        /* 7668e: bra.w patch2 */
        move.w  #0x6000,0x68e(a5)                 /* bra.w xxx */
        move.w  #patch2-PATCH1_START,d0
        add.w   #0x64-0x690,d0
        move.w  d0,0x690(a5)

        jmp     (a5)

PATCH1_START:    
        /* (We are in place of the Copylock) */
        /* Fake the Copylock by hardwiring the key and jumping straight past */
        move.l  #0xf974db7d,d0
        move.l  d0,(0x24).w
        jmp     (0x76668).l

patch2: /* (We jump here instead of the final jump to 0x400) */
        /* Copy our track loader over the game's loader */
        lea.l   PATCH2_START(pc),a0
        lea.l   PATCH2_END(pc),a2
        lea.l   (0xc28).w,a1
1:      move.l  (a0)+,(a1)+
        cmp.l   a0,a2
        jcc     1b

        /* TRAINER 1. Infinite lives */
        lea.l   config_options(pc),a4
        tst.b   config_inf_lives(a4)
        jeq     .skip_inf_lives
        moveq   #4,d0
        lea.l   .lives_dec(pc),a0
.nop:   move.l  (a0)+,a1
        move.l  #0x4e714e71,(a1) /* NOP out lives decrement */
        dbf     d0,.nop
.skip_inf_lives:

        /* TRAINER 2. Start with N lives */
        moveq   #0,d0
        move.b  config_nr_lives(a4),d0
        move.w  d0,(0x4fba).w

        /* HIGHSCORE SAVE */
        tst.b   config_save_hi(a4)
        jeq     .skip_hi
        move.w  #0x4ef8,(0x4736).w /* jmp (xxx).w */
        move.w  #0x0c24,d0      /* copy destination of nzs_load_tracks, */
                                /* adjusted for subsequent 4-byte relocation */
        add.w   #nzs_save_hiscore-nzs_load_tracks,d0
        move.w  d0,(0x4738).w

        /* HIGHSCORE LOAD */
        lea.l   (0x60000).l,a0  /* We stashed track 1 contents here */
        lea.l   (0x4820).w,a1   /* This is where the data belongs in game */
        cmp.l   #0x68697363,(a0)+
        jne     .skip_hi        /* Skip if track 1 doesn't have our signature */
        moveq   #0x1f,d1
1:      move.l  (a0)+,d0        /* Grab some data... */
        eor.l   #0xf381a092,d0  /* ...decrypt it... */
        move.l  d0,(a1)+        /* ...poke it */
        dbf     d1,1b
        
        /* Finish with the final jump to the game */
.skip_hi: jmp   (0x400).w
       
.lives_dec: /* Addresses of lives decrement insns */
        dc.l    0x79c8,0x8a72,0x8b2e,0x8bee,0x990a

config_options: .space config_size

PATCH2_START:
        /* ********* TRACK LOADER ********** */
        /* IN: a0 = load_address; d0 = start track; d1 = nr tracks */
        /* OUT: d0 = 0/-1 (0=success); all other registers preserved */
nzs_load_tracks:
        /* Constants from the original NZS loader */
#define mfm_bytes 0x332c
#define mfm_addr  0x68000
        move.l  #mfm_addr,a1
        exg     a0,a1
        jbsr    load_tracks
        exg     a0,a1
        rts
        
#define loc_cylinder 0
#define loc_track    1

        /* IN: a0 = mfmbuf, a1 = dest, d0 = start track, d1 = nr tracks */
        /* OUT: d0 = 0/-1 (0=success); all other registers preserved */
load_tracks:
        movem.l d0-d6/a0-a1/a4-a6,-(sp)

        lea     (0xDFF000).l,a4
        lea     (0xBFD100).l,a5
        lea     locals(pc),a6
        move.l  #0x55555555,d5
        st      loc_track(a6)   /* mfmbuf has unknown contents, so scratch */
                                /* the buffered track # */
        jbsr    motor_on

        move.w  2(sp),d0
        move.w  6(sp),d1
        mulu.w  #11,d0
        mulu.w  #11,d1
        moveq   #-1,d2
        move.l  d2,(sp)         /* initialise return code (failure) */
.next:  jbsr    load_sector
        jeq     .bail           /* CC_Z=1 on failure */
        lea.l   0x200(a1),a1
        addq.w  #1,d0
        subq.w  #1,d1
        jne     .next
        clr.l   (sp)            /* success, return d0=0 */
.bail:  jbsr    motors_off
        movem.l (sp)+,d0-d6/a0-a1/a4-a6
        rts

        /* Turn on DF0 motor only, wait for DSKRDY, or 500ms to pass */
        /* scratches d0-d1 */
motor_on:
        ori.b   #0xF8,(a5)
        andi.b  #0x7F,(a5)
        andi.b  #0xF7,(a5)
        move.w  #8000,d1        /* 8000 * 63us ~= 500ms */
1:      btst    #5,0xF01(a5)
        jeq     2f
        jbsr    wait_vline
        dbf     d1,1b
2:      rts
        
motors_off:
        ori.b   #0xF8,(a5)
        andi.b  #0x87,(a5)
        ori.b   #0x78,(a5)
        rts

_step_one_out:
        jbsr    step_one
_seek_cyl0:
        btst    #4,0xF01(a5)
        jne     _step_one_out
        sf      (a6)            /* loc_cylinder(a6) = 0 */

        /* d2 = track, d0-d2 scratch */
seek_track:
        bset    #1,(a5)         /* seek outwards by default */
        tst.b   (a6)            /* loc_cylinder(a6) < 0? */
        jmi     _seek_cyl0      /* resync heads if so */
        moveq   #2,d0
        bset    d0,(a5)         /* side 0 */
        lsr.w   #1,d2           /* d2.w = cyl# */
        jcc     .check_cyl
        bclr    d0,(a5)         /* side 1 */
.check_cyl:
        cmp.b   (a6),d2
        jne     .seek           /* current cyl is correct: */
        move.w  #250,d1         /* ...then wait 250 * 63us ~= 15ms */
1:      jbsr    wait_vline      /* ...for drive settle */
        dbf     d1,1b
        rts
.seek:  jcs     .seek_outward   /* current cyl too high: seek outward */
        bclr    #1,(a5)
        addq.b  #2,(a6)         /* +2 as we -1 straight after ;-) */
.seek_outward:
        subq.b  #1,(a6)
        jbsr    step_one
        jra     .check_cyl

step_one:
        moveq   #0,d0
        bclr    d0,(a5)
        mulu.w  d0,d0
        bset    d0,(a5)         /* step pulse */
        move.w  #50,d1          /* 50 * 63us ~= 3ms */
1:      jbsr    wait_vline
        dbf     d1,1b
        rts

wait_vline:
        move.b  0x6(a4),d0
1:      cmp.b   0x6(a4),d0
        jeq     1b
        rts

        /*  d0 = sector #, a0 = mfm, a1 = dest */
        /*  CC_Z is set on failure */
load_sector:
        movem.l d0-d1/a0-a1,-(sp)
        moveq   #16,d6           /* d6 = retry counter (16) */
_load_sector:
        move.l  (sp),d2
        ext.l   d2
        divu.w  #0xB,d2
        move.l  d2,d4           /* d2.w = track# */
        swap    d4              /* d4.w = sector# */
        cmp.b   loc_track(a6),d2
        jeq     .decode_mfm     /* start decode if track is already buffered */
        move.b  d2,loc_track(a6)
        jbsr    seek_track
        move.l  8(sp),0x20(a4)    /* dskpt */
        move.w  #0x8210,0x96(a4)  /* dmacon -- enable disk dma */
        move.l  #0x27F00,0x9c(a4) /* clear intreq & adkcon */
        move.w  #0x9500,0x9e(a4)  /* adkcon -- MFM, wordsync */
        move.w  #0x4489,0x7e(a4)  /* sync 4489 */
        move.w  #0x8000+mfm_bytes/2,0x24(a4)
        move.w  #0x8000+mfm_bytes/2,0x24(a4)     /* dsklen -- 0x1900 words */
        move.w  #16000,d1         /* 16000 * 63us ~= 1 second */
1:      subq.w  #1,d1
        jeq     .fail_retry
        jbsr    wait_vline
        btst    #1,0x1f(a4)       /* intreqr -- disk dma done? */
        jeq     1b
        move.w  #0x4000,0x24(a4)  /* dsklen -- no more dma */
.decode_mfm:
        move.l  8(sp),a0                /* a0 = mfm start */
        lea     mfm_bytes-1080(a0),a1   /* a1 = mfm end - 1 sector */
.next_sector:
        cmpi.w  #0x4489,(a0)      /* skip 4489 sync */
        jeq     .find_sector
        movem.l (a0),d0-d1
        jbsr    decode_mfm_long
        lsr.w   #8,d0             /* d0.w = sector # */
        cmp.w   d4,d0
        jeq     .sector_found
        lea.l   0x438(a0),a0      /* skip this sector */
.find_sector:
        cmpa.l  a0,a1             /* bail if we scan to end of mfm buffer */
        jls     .fail_retry
        cmpi.w  #0x4489,(a0)+
        jne     .find_sector
        jra     .next_sector
.sector_found:
        swap    d0                /* d0.b = track # */
        cmp.b   loc_track(a6),d0
        jne     .fail_retry       /* wrong track?! */
        lea     0x30(a0),a0
        move.l  (a0)+,d4
        move.l  (a0)+,d0
        eor.l   d0,d4             /* d4.l = data checksum */
        moveq   #0x7F,d2
        move.l  12(sp),a1         /* a1 = destination */
.next_data_long:
        move.l  0x200(a0),d1
        move.l  (a0)+,d0
        eor.l   d0,d4
        eor.l   d1,d4
        jbsr    decode_mfm_long
        move.l  d0,(a1)+
        dbf     d2,.next_data_long
        and.l   d5,d4
        jne     .fail_retry
.fail:  movem.l (sp)+,d0-d1/a0-a1
        tst.b   d6
        rts

.fail_retry:
        move.w  #0x4000,0x24(a4)  /* dsklen -- no more dma */
        st      loc_track(a6)     /* scratch the buffered track */
        subq.b  #1,d6
        jeq     .fail
        moveq   #3,d0
        and.b   d6,d0             /* every four retries... */
        jne     .nosync
        st      (a6)              /* ...we resync the drive heads */
.nosync:bra     _load_sector      /* ..so we resync via track 0 */

        /* d0 = decode_mfm_long(d0 = odd, d1 = even, d5 = 0x55555555) */
decode_mfm_long:
        and.l   d5,d0
        and.l   d5,d1
        add.l   d0,d0
        or.l    d1,d0
        rts

locals: dc.b 0xFF                /* loc_cylinder: current cylinder */
        dc.b 0xFF                /* loc_track: current track */

nzs_save_hiscore:
        movem.l d0-d1/a0-a1,-(sp)
        lea.l   (0x4820).w,a0
        lea.l   (0x68000).l,a1
        move.l  #0x68697363,(a1)+       
        moveq   #0x1f,d1
1:      move.l  (a0)+,d0
        eor.l   #0xf381a092,d0
        move.l  d0,(a1)+
        dbf     d1,1b
        move.w  #0x55f,d1
2:      clr.l   (a1)+
        dbf     d1,2b
        lea.l   (0x68000).l,a0
        moveq   #1,d0
        jbsr    mfm_encode_track
        jbsr    write_track     
        movem.l (sp)+,d0-d1/a0-a1
        tst.w   0x2c(a6)         /* from the code we patched over */
        rts
        
        /* a0 = mfm buffer; d0 = track#; all regs preserved */
write_track:
        movem.l d0-d2/a4-a6,-(sp)
        lea     (0xDFF000).l,a4
        lea     (0xBFD100).l,a5
        lea     locals(pc),a6
        move.l  d0,d2             /* stash track# in d2 for seek_track */
        ori.b   #0xF8,(a5)
        andi.b  #0xF7,(a5)        /* select drive 0 (motor off for now) */
        btst    #3,0xF01(a5)      /* bail if disk is write protected */
        jeq     .donew
        jbsr    motor_on
        jbsr    seek_track
        move.l  a0,0x20(a4)       /* dskpt */
        move.w  #0x8210,0x96(a4)  /* dmacon -- enable disk dma */
        move.l  #0x27F00,0x9c(a4) /* clear intreq & adkcon */
        cmp.b   #40,(a6)
        jcs     .noprec
        /* adkcon -- 140ns precomp for cylinders 40-79 (exactly the same as
         * trackdisk.device, tested on Kickstart 3.1) */
        move.w  #0xa000,0x9e(a4)
.noprec:move.w  #0x9100,0x9e(a4)  /* adkcon -- MFM, no wordsync */
        move.w  #0xc000+mfm_bytes/2,0x24(a4)
        move.w  #0xc000+mfm_bytes/2,0x24(a4)     /* dsklen */
        move.w  #16000,d1         /* 16000 * 63us ~= 1 second */
1:      subq.w  #1,d1
        jeq     .donew
        jbsr    wait_vline
        btst    #1,0x1f(a4)       /* intreqr -- disk dma done? */
        jeq     1b
.donew: move.w  #0x4000,0x24(a4)  /* dsklen -- no more dma */
        jbsr    motors_off
        movem.l (sp)+,d0-d2/a4-a6
        rts

        /* a0.l = buffer to encode ; d0.b = track # */
        /* All registers are preserved */
mfm_encode_track:
        movem.l d0-d7/a0-a1,-(sp)
        move.l  #0x55555555,d5
        move.l  #0xaaaaaaaa,d6
        lea.l   mfm_bytes(a0),a1
        lea.l   0x1600(a0),a0
        move.w  #10,d7
.sect:  moveq   #0x7f,d2
        moveq   #0,d3
        move.l  d3,-(a1)        /* sector gap */
        lea.l   -0x200(a1),a1
.lop:   move.l  -(a0),d0
        jbsr    encode_mfm_long
        eor.l   d0,d3
        eor.l   d1,d3
        move.l  d0,-(a1)        /* even data bits */
        move.l  d1,0x200(a1)    /* odd data bits */
        dbf     d2,.lop
        and.l   d5,d3
        move.l  d3,d0
        jbsr    encode_mfm_long
        movem.l d0-d1,-(a1)     /* data checksum */
        moveq   #0,d0
        moveq   #9,d1
.lop2:  move.l  d0,-(a1)        /* header checksum + sector label */
        dbf     d1,.lop2
        move.w  #0xff00,d0      /* info.format = 0xff */
        move.b  3(sp),d0        /* info.track */
        swap    d0
        move.b  d7,d0           /* info.sector */
        lsl.w   #8,d0
        move.b  #11,d0
        sub.b   d7,d0           /* info.sectors_to_gap */
        jbsr    encode_mfm_long
        movem.l d0-d1,-(a1)     /* sector info long */
        eor.l   d1,d0
        jbsr    encode_mfm_long
        movem.l d0-d1,40(a1)    /* header checksum */
        move.l  #0x44014401,-(a1)
        move.w  #271,d2
.clk:   move.l  (a1),d0         /* get a longword of data bits */
        move.l  d0,d1
        roxr.l  #1,d0           /* d0 = (X . data_bits) >> 1 -> X */
        rol.l   #1,d1           /* d1 = data_bits << 1 */
        or.l    d0,d1
        not.l   d1              /* clock[n] = data[n-1] NOR data[n] */
        and.l   d6,d1
        or.l    d1,(a1)+        /* OR the clock bits into the longword */
        dbf     d2,.clk
        lea.l   -1088(a1),a1
        move.l  #0x44894489,(a1) /* sync marker */
        dbf     d7,.sect
        move.l  #0xaaaaaaaa,d0
.lop3:  move.l  d0,-(a1)
        cmpa.l  a0,a1
        jhi     .lop3
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

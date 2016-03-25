
        .section .text

        .global read_tracks, write_track, mfm_encode_track
        
#define mfm_bytes 0x332c

#define loc_cylinder 0
#define loc_track    1

        /* IN: a0 = mfmbuf, a1 = dest, d0 = start track, d1 = nr tracks */
        /* OUT: d0 = 0/-1 (0=success); all other registers preserved */
read_tracks:
        movem.l d0-d6/a0-a1/a4-a6,-(sp)

        lea     (0xDFF000).l,a4
        lea     (0xBFD100).l,a5
        lea     locals(pc),a6
        move.l  #0x55555555,d5
        st      loc_track(a6)   /* mfmbuf has unknown contents, so scratch */
                                /* the buffered track # */

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
.bail:  movem.l (sp)+,d0-d6/a0-a1/a4-a6
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
        lea     mfm_bytes-0x438(a0),a1  /* a1 = mfm end - 1 sector */
.next_sector:
        cmpa.l  a0,a1             /* bail if we scan to end of mfm buffer */
        jmi     .fail_retry
        cmpi.w  #0x4489,(a0)+
        jne     .next_sector
        cmpi.w  #0x4489,(a0)      /* skip 4489 sync */
        jeq     .next_sector
        movem.l (a0),d0-d1
        jbsr    decode_mfm_long
        lsr.w   #8,d0             /* d0.w = sector # */
        cmp.w   d4,d0
        jeq     .sector_found
        lea.l   0x438(a0),a0      /* skip this sector */
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

        /* a0 = mfm buffer; d0 = track#; all regs preserved */
write_track:
        movem.l d0-d2/a4-a6,-(sp)
        lea     (0xDFF000).l,a4
        lea     (0xBFD100).l,a5
        lea     locals(pc),a6
        move.l  d0,d2             /* stash track# in d2 for seek_track */
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
        cmpa.l  a1,a0
        jmi     .lop3
        movem.l (sp)+,d0-d7/a0-a1
        rts

encode_mfm_long:
        move.l  d0,d1
        lsr.l   #1,d0
        and.l   d5,d0
        and.l   d5,d1
        rts

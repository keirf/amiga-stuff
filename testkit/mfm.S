
        .section .text

        .global mfm_decode_track, mfm_encode_track
        
        /* nr_secs = mfm_decode_track(mfmbuf, headers, data, mfm_bytes) */
mfm_decode_track:
        movem.l d0-d7/a0-a3,-(sp)
        lea     13*4(sp),a3       /* args: */
        move.l  (a3)+,a0          /* mfmbuf */
        move.l  (a3)+,a1          /* headers */
        move.l  (a3)+,a2          /* data */
        move.l  (a3)+,d0          /* mfm_bytes */
        moveq   #0,d3             /* nr_sectors */
        move.l  #0x55555555,d5
        sub.w   #1080,d0
        lea     (a0,d0.w),a3      /* a3 = mfm end - 1 sector */
.next_sector:
        cmpi.w  #0x4489,(a0)      /* skip 4489 sync */
        jeq     .find_sector
        movem.l (a0),d0-d1
        jbsr    decode_mfm_long
        move.l  d0,(a1)+          /* save the header longword */
        lea     0x30(a0),a0
        move.l  (a0)+,d4
        move.l  (a0)+,d0
        eor.l   d0,d4             /* d4.l = data checksum */
        addq    #1,d3
        moveq   #0x7F,d2
.next_data_long:
        move.l  0x200(a0),d1
        move.l  (a0)+,d0
        eor.l   d0,d4
        eor.l   d1,d4
        jbsr    decode_mfm_long
        move.l  d0,(a2)+
        dbf     d2,.next_data_long
        and.l   d5,d4
        move.l  d4,(a1)+          /* save the EOR checksum */
.find_sector:
        cmpa.l  a0,a3             /* bail if we scan to end of mfm buffer */
        jls     .done
        cmpi.w  #0x4489,(a0)+
        jne     .find_sector
        jra     .next_sector
.done:  move.l  d3,(sp)
        movem.l (sp)+,d0-d7/a0-a3
        rts

        /* d0 = decode_mfm_long(d0 = odd, d1 = even, d5 = 0x55555555) */
decode_mfm_long:
        and.l   d5,d0
        and.l   d5,d1
        add.l   d0,d0
        or.l    d1,d0
        rts

        /* mfm_encode_track(mfmbuf, tracknr, mfm_bytes, nr_secs) */
mfm_encode_track:
        movem.l d0-d7/a0-a1,-(sp)
        lea     11*4(sp),a1       /* args: */
        move.l  (a1)+,a0          /* mfmbuf */
        move.l  (a1)+,d4          /* tracknr */
        move.l  (a1)+,d1          /* mfm_bytes */
        move.l  (a1)+,d7          /* nr_secs */
        move.l  #0x55555555,d5
        move.l  #0xaaaaaaaa,d6
        lea.l   (a0,d1.w),a1    /* a1 = end of mfm buffer */
        move.w  d7,d1
        lsl.w   #8,d1
        add.w   d1,d1
        lea.l   (a0,d1.w),a0    /* a0 = end of input data */
        subq.w  #1,d7
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
        move.b  d4,d0           /* info.track */
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

/*
 * inflate.S
 * 
 * Decompression of DEFLATE streams, as produced by zip/gzip/pkzip and
 * specified in RFC 1951 "DEFLATE Compressed Data Format Specification".
 *
 * Usage: Optionally configure the OPT_xxx options below at build time;
 * at run time 'bsr inflate' with arguments:
 *    a4 = output buffer, a5 = input stream
 *    a6 = *end* of temporary storage area (only if OPT_STORAGE_OFFSTACK)
 * All register values (including arguments) are preserved.
 *
 * Space requirements: 646-928 bytes code; 2044-2940 bytes stack.
 * (NB1. Above ranges are [No Optimisations]-[All Optimisations])
 * (NB2. Stack space can be relocated to a separately-specified storage
 *       area, see OPT_STORAGE_OFFSTACK below)
 *
 * Timings: With all Optimisation Options enabled (see below) this routine
 * will decompress on a basic 7MHz 68000 at ~23kB/s. An AmigaDOS track of
 * data (5.5kB) is processed in ~240ms. This is only fractionally slower than
 * the track can be fetched from disk, hence there is scope for a
 * decompressing loader to keep CPU and disk both at near 100% utilisation.
 * 
 * Written & released by Keir Fraser <keir.xen@gmail.com>
 * 
 * This is free and unencumbered software released into the public domain.
 * See the file COPYING for more details, or visit <http://unlicense.org>.
 */

/* Optimisation Option #1:
 * Avoid long Huffman-tree walks by indexing the first 8 bits of each codeword
 * in a 256-entry lookup table. This shortens all walks by 8 steps and since
 * the most common codes are less than 8 bits, most tree walks are avoided.
 * SPEEDUP: 41% (c.w. no Options); COST: 110 bytes code, 896 bytes stack */
#ifndef OPT_TABLE_LOOKUP
#define OPT_TABLE_LOOKUP 1
#endif

/* Optimisation Option #2:
 * Inline functions in the main decode loop to avoid all BSR/RTS pairs.
 * SPEEDUP: 15% (on top of Option #1); COST: 166 bytes code */
#ifndef OPT_INLINE_FUNCTIONS
#define OPT_INLINE_FUNCTIONS 1
#endif

/* Optimisation Option #3:
 * Unroll the copy loop for <distance,length> tuples by one iteration
 * (so two bytes are copied per iteration).
 * SPEEDUP: ~1% (on top of Options #1 and #2); COST: 6 bytes code */
#ifndef OPT_UNROLL_COPY_LOOP
#define OPT_UNROLL_COPY_LOOP 1
#endif

/* Storage Option:
 * All but 12 bytes of this routine's space requirement can be allocated
 * off stack, in a data area specified in register a6.
 * If this option is set then inflate must be called with a6 pointing at
 * the *end* of the reserved storage area (+2032 or +2928 bytes, depending
 * on whether OPT_TABLE_LOOKUP is enabled).
 * SPEEDUP: none; COST: -2 bytes code (makes code slightly smaller) */
#ifndef OPT_STORAGE_OFFSTACK
#define OPT_STORAGE_OFFSTACK 1
#endif

#if OPT_STORAGE_OFFSTACK
#define aS a6
#else
#define aS sp
#endif

/* Longest possible code. */
#define MAX_CODE_LEN   16

/* (Maximum) alphabet sizes. */
#define nr_codelen_symbols  19
#define nr_litlen_symbols  288
#define nr_distance_symbols 32

/* Alphabet-description stream for a static Huffman block (BTYPE=01b). */
static_huffman_prefix:
        dc.b 0xff, 0x5b, 0x00, 0x6c, 0x03, 0x36, 0xdb
        dc.b 0xb6, 0x6d, 0xdb, 0xb6, 0x6d, 0xdb, 0xb6
        dc.b 0xcd, 0xdb, 0xb6, 0x6d, 0xdb, 0xb6, 0x6d
        dc.b 0xdb, 0xa8, 0x6d, 0xce, 0x8b, 0x6d, 0x3b

#if OPT_TABLE_LOOKUP

/* Number of bytes required for code-lookup table/tree:
 *  - 256 2-byte entries for the 8-bit lookup table
 *  - Worst-case only 8 symbols decode directly in the table and all the rest
 *    are in a tree hanging off one table entry. This tree requires
 *    (nr_symbols-8)-1 internal 4-byte nodes. */
#define LOOKUP_BYTES(nr_syms) (256*2+((nr_syms)-9)*4)

        /* a0 = len[], a1 = nodes[], d0 = nr_symbols */
        /* a2-a3 are scratched */
build_code:
        movem.l d0-d7,-(aS)

        /* Allocate space for bl_count[]/next_code[] array on stack. */
        moveq   #(MAX_CODE_LEN+1)/2,d1
        moveq   #0,d2
1:      move.l  d2,-(aS)
        dbf     d1,1b

        /* Count occurrences of each code length into bl_count[] array. */
        subq.w  #1,d0
        move.w  d0,d1
        move.l  a0,a2           /* a2 = &len[0] */
1:      move.b  (a2)+,d2        /* d2 = len[i] */
        add.b   d2,d2
        addq.w  #1,(aS,d2.w)    /* bl_count[len[i]]++ */
        dbf     d1,1b

        /* Calculate next_code[] start values for each code length. */
        move.l  aS,a2           /* a2 = bl_count[] / next_code[] */
        moveq   #MAX_CODE_LEN-1,d1
        moveq   #0,d2           /* d2 = code */
        move.w  d2,(aS)         /* bl_count[0] = 0, ignore zero-length codes */
1:      add.w   (a2),d2
        add.w   d2,d2           /* code = (code + bl_count[i-1]) << 1 */
        move.w  d2,(a2)+        /* next_code[i] = code */
        dbf     d1,1b

        /* Create the Huffman-code lookup tree */
        move.w  d0,d1
        moveq   #127,d4         /* d4 = next_node = 127 */
        move.l  a0,a2           /* a2 = &len[0] */
1:      moveq   #0,d5
        move.b  (a2)+,d5        /* d5 = len[i] / *len++ */
        jeq     4f
        subq.w  #1,d5
        move.w  d5,d6
        add.w   d6,d6
        move.w  (aS,d6.w),d3    /* d3 = code = next_code[len[i]]++ */
        addq.w  #1,(aS,d6.w)

        move.w  d5,d6
        moveq   #0,d2
9:      lsr.w   #1,d3
        roxl.w  #1,d2
        dbf     d6,9b           /* d5 = codelen-1; d2 = reversed code */
        move.b  d2,d3
        add.w   d3,d3           /* d3 = table offset */
        move.w  d0,d6
        sub.w   d1,d6           /* d6 = symbol */
        cmp.b   #9-1,d5
        jpl     codelen_gt_8

codelen_le_8: /* codelen <= 8: leaf in table entry(s) */
        lsl.w   #3,d6
        or.b    d5,d6           /* d6 = (symbol<<3) | (codelen-1) */
        moveq   #7,d2
        sub.b   d5,d2
        moveq   #0,d7
        bset    d2,d7
        subq.b  #1,d7           /* d7 = 1<<(8-codelen)-1 */
        moveq   #0,d2
        addq.b  #2,d5
        bset    d5,d2           /* d2 = 1<<(codelen+1) */
9:      move.w  d6,(a1,d3.w)
        add.w   d2,d3
        dbf     d7,9b
        jra     4f

codelen_gt_8: /* codelen > 8: requires a tree walk */
        lsr.w   #8,d2
        subq.b  #8,d5           /* Skip the first 8 bits of code */
        lea     (a1,d3.w),a3    /* pnode = table entry */

2:      /* Walk through *pnode. */
        move.w  (a3),d7         /* d3 = *pnode */
        jne     3f
        /* Link missing: Create a new internal node */
        addq.w  #1,d4
        move.w  d4,d7
        bset    #15,d7
        move.w  d7,(a3)         /* *pnode = ++next_node | INTERNAL */
3:      /* Take left or right branch depending on next code bit */
        lsr.b   #1,d2
        addx.w  d7,d7
        add.w   d7,d7
        lea     (a1,d7.w),a3    /* pnode = next_bit ? &node->r : &node->l */
3:      dbf     d5,2b

        /* Insert the current symbol as a new leaf node */
        move.w  d6,(a3)         /* *pnode = sym */
4:      dbf     d1,1b

        lea     (((MAX_CODE_LEN+1)/2)+1)*4(aS),aS
        movem.l (aS)+,d0-d7
        rts

        /* d5-d6/a5 = stream, a0 = tree */
        /* d0.w = result, d1.l = scratch */
.macro STREAM_NEXT_SYMBOL
        moveq   #0,d0   /* 4 */
        moveq   #7,d1   /* 4 */
        cmp.b   d1,d6   /* 4 */
        jhi     99f     /* 10 */
        /* Less than 8 bits cached; grab another byte from the stream */
        move.b  (a5)+,d0 /* [8] */
        lsl.w   d6,d0   /* [~14] */
        or.w    d0,d5   /* [4] */ /* s->cur |= *p++ << s->nr */
        addq.b  #8,d6   /* [4] */ /* s->nr += 8 */
        moveq   #0,d0   /* [4] */
99:     /* Use next input byte as index into code lookup table */
        move.b  d5,d0   /* 4 */
        add.w   d0,d0   /* 4 */
        move.w  (a0,d0.w),d0 /* 14 */
        jpl     99f     /* 10 (taken) */
        /* Code is longer than 8 bits: do the remainder via a tree walk */
        lsr.w   #8,d5
        subq.b  #8,d6           /* consume 8 bits from the stream */
98:     /* stream_next_bits(1), inlined & optimised */
        subq.b  #1,d6           /* 4 cy */
        jpl     97f             /* 10 cy (taken) */
        move.b  (a5)+,d5        /* [8 cy] */
        moveq   #7,d6           /* [4 cy] */
97:     lsr.w   #1,d5           /* 8 cy */
        addx.w  d0,d0           /* 4 cy */
        add.w   d0,d0           /* 4 cy */
        move.w  (a0,d0.w),d0    /* 14 cy */
        jmi     98b             /* 10 cy (taken); loop on INTERNAL flag */
        jra     98f             /* TOTAL LOOP CYCLES ~= 54 */
99:     /* Symbol found directly: consume bits and return symbol */
        and.b   d0,d1   /* 4 */
        addq.b  #1,d1   /* 4 */
        lsr.w   d1,d5   /* ~16 */ /* consume bits from the stream */
        sub.b   d1,d6   /* 4 */
        lsr.w   #3,d0   /* 12 */  /* d0 = symbol */
98:                     /* ~94 CYCLES TOTAL [+ 34] */
.endm

#else /* !OPT_TABLE_LOOKUP */

/* Number of bytes required for code-lookup tree:
 *  - Every binary tree with N leaves has N-1 internal nodes.
 *  - Internal nodes require 4 bytes each. Leaves are free. */
#define LOOKUP_BYTES(nr_syms) (((nr_syms)-1)*4)

        /* a0 = len[], a1 = nodes[], d0 = nr_symbols */
        /* a2-a3 are scratched */
build_code:
        movem.l d0-d5,-(aS)

        /* Allocate space for bl_count[]/next_code[] array on stack. */
        moveq   #(MAX_CODE_LEN+1)/2,d1
        moveq   #0,d2
1:      move.l  d2,-(aS)
        dbf     d1,1b

        /* Count occurrences of each code length into bl_count[] array. */
        subq.w  #1,d0
        move.w  d0,d1
        move.l  a0,a2           /* a2 = &len[0] */
1:      move.b  (a2)+,d2        /* d2 = len[i] */
        add.b   d2,d2
        addq.w  #1,(aS,d2.w)    /* bl_count[len[i]]++ */
        dbf     d1,1b

        /* Calculate next_code[] start values for each code length. */
        move.l  aS,a2           /* a2 = bl_count[] / next_code[] */
        moveq   #MAX_CODE_LEN-1,d1
        moveq   #0,d2           /* d2 = code */
        move.w  d2,(aS)         /* bl_count[0] = 0, ignore zero-length codes */
1:      add.w   (a2),d2
        add.w   d2,d2           /* code = (code + bl_count[i-1]) << 1 */
        move.w  d2,(a2)+        /* next_code[i] = code */
        dbf     d1,1b

        /* Create the Huffman-code lookup tree */
        move.w  d0,d1
        moveq   #0,d4           /* d4 = next_node */
        move.l  a0,a2           /* a2 = &len[0] */
1:      moveq   #0,d5
        move.b  (a2)+,d5        /* d5 = len[i] / *len++ */
        jeq     4f
        subq.w  #1,d5
        add.w   d5,d5
        move.w  (aS,d5.w),d3    /* d3 = code = next_code[len[i]]++ */
        addq.w  #1,(aS,d5.w)
        lsr.w   #1,d5
        /* Walk down the tree, creating nodes as necessary */
        moveq   #0,d2           /* d2 = 0 (root node) */
        jra     3f

2:      /* Walk through *pnode. */
        move.w  (a3),d2         /* d2 = *pnode */
        jne     3f
        /* Link missing: Create a new internal node */
        addq.w  #1,d4
        move.w  d4,d2
        bset    #15,d2
        move.w  d2,(a3)         /* *pnode = ++next_node | INTERNAL */
3:      /* Take left or right branch depending on next code bit */
        lsl.w   #2,d2
        btst    d5,d3
        jeq     3f
        addq.w  #2,d2
3:      lea     (a1,d2.w),a3    /* pnode = next_bit ? &node->r : &node->l */
        dbf     d5,2b

        /* Insert the current symbol as a new leaf node */
        move.w  d0,d2
        sub.w   d1,d2
        move.w  d2,(a3)         /* *pnode = sym */
4:      dbf     d1,1b

        lea     (((MAX_CODE_LEN+1)/2)+1)*4(aS),aS
        movem.l (aS)+,d0-d5
        rts

        /* d5-d6/a5 = stream, a0 = tree */
        /* d0.w = result */
.macro STREAM_NEXT_SYMBOL
        moveq   #0,d0
99:     /* stream_next_bits(1), inlined & optimised */
        subq.b  #1,d6           /* 4 cy */
        jpl     98f             /* 10 cy (taken) */
        move.b  (a5)+,d5        /* [8 cy] */
        moveq   #7,d6           /* [4 cy] */
98:     lsr.w   #1,d5           /* 8 cy */
        addx.w  d0,d0           /* 4 cy */
        add.w   d0,d0           /* 4 cy */
        move.w  (a0,d0.w),d0    /* 14 cy */
        jmi     99b             /* 10 cy (taken); loop on INTERNAL flag set */
                                /* TOTAL LOOP CYCLES ~= 54 */
.endm

#endif

        /* d1.b = nr, d5-d6/a5 = stream [fetched_bits/nr_fetched_bits/inp] */
        /* d0.w = result */
.macro  STREAM_NEXT_BITS
99:     moveq   #0,d0
        cmp.b   d1,d6
        jpl     99f             /* while (s->nr < nr) */
        move.b  (a5)+,d0
        lsl.l   d6,d0
        or.l    d0,d5           /* s->cur |= *p++ << s->nr */
        addq.b  #8,d6           /* s->nr += 8 */
        jra     99b
99:     bset    d1,d0
        subq.w  #1,d0           /* d0 = (1<<nr)-1 */
        and.w   d5,d0           /* d0 = s->cur & ((1<<nr)-1) */
        lsr.l   d1,d5           /* s->cur >>= nr */
        sub.b   d1,d6           /* s->nr -= nr */
.endm

#if OPT_INLINE_FUNCTIONS
#define INLINE_stream_next_bits   STREAM_NEXT_BITS
#define INLINE_stream_next_symbol STREAM_NEXT_SYMBOL
#else
#define INLINE_stream_next_bits   jbsr stream_next_bits
#define INLINE_stream_next_symbol jbsr stream_next_symbol
#endif

stream_next_bits:
        STREAM_NEXT_BITS
        rts

        /* d5-d6/a5 = stream, a4 = output */
        /* d0-d1 are scratched */
uncompressed_block:
#if OPT_TABLE_LOOKUP
        /* Push whole bytes back into input stream. */
        lsr.w   #3,d6
        sub.w   d6,a5
#else
        /* No need to push bytes back into input stream because stream_next_
         * {bits,symbol} will never leave more than 7 bits cached. */
#endif
        /* Snap input stream up to byte boundary. */
        moveq   #0,d5
        moveq   #0,d6
        /* Read block header and copy LEN bytes. */
        moveq   #16,d1
        jbsr    stream_next_bits /* LEN */
        addq.w  #2,a5           /* skip NLEN */
        subq.w  #1,d0           /* d0.w = len-1 (for dbf) */
1:      move.b  (a5)+,(a4)+
        dbf     d0,1b
        rts

#define o_hdist /*0*/
#define o_hlit  2
#define o_lens  (o_hlit+2)
#define o_codelen_tree (o_lens+nr_litlen_symbols+nr_distance_symbols)
#if OPT_TABLE_LOOKUP
/* Lit/len and codelen lookup structures share space. */
#define o_litlen_tree o_codelen_tree
#else
#define o_litlen_tree (o_codelen_tree+LOOKUP_BYTES(nr_codelen_symbols))
#endif
#define o_dist_tree (o_litlen_tree+LOOKUP_BYTES(nr_litlen_symbols))
#define o_stream (o_dist_tree+LOOKUP_BYTES(nr_distance_symbols))
#define o_frame (o_stream+3*4)
#if OPT_STORAGE_OFFSTACK
#define o_mode (o_frame)
#else
/* Allow for BSR return address from decoder */
#define o_mode (o_frame+4)
#endif
#define o_dist_extra (o_mode+4)
#define o_length_extra (o_dist_extra+30*4)

        /* d5-d6/a5 = stream, a4 = output */
        /* d0-d3,a0-a3 are scratched */
static_huffman:
        movem.l d5-d6/a5,-(aS)
        moveq   #0,d5
        moveq   #0,d6
        lea     static_huffman_prefix(pc),a5
        move.w  #o_stream/4-2,d0
        jra     1f

        /* d5-d6/a5 = stream, a4 = output */
        /* d0-d3,a0-a3 are scratched */
dynamic_huffman:
        /* Allocate stack space for len[] and node[] arrays */
        move.w  #o_frame/4-2,d0
1:      moveq   #0,d1
1:      move.l  d1,-(aS)
        dbf     d0,1b
        /* HLIT = stream_next_bits(5) + 257 */
        moveq   #5,d1
        jbsr    stream_next_bits
        add.w   #257,d0
        move.w  d0,-(aS)
        /* HDIST = stream_next_bits(5) + 1 */
        moveq   #5,d1
        jbsr    stream_next_bits
        addq.w  #1,d0
        move.w  d0,-(aS)
        /* HCLEN = stream_next_bits(4) + 4 */
        moveq   #4,d1
        jbsr    stream_next_bits
        addq.w  #4-1,d0         /* -1 for dbf */
        /* Initialise len[] array with code-length symbol code lengths */
        lea     codelen_order(pc),a1
        lea     o_lens(aS),a0   /* a0 = len[] */
        moveq   #0,d2
        move.w  d0,d3
1:      moveq   #3,d1
        jbsr    stream_next_bits
        move.b  (a1)+,d2
        move.b  d0,(a0,d2.w)    /* len[codelen_order[i++]] = next_bits(3) */
        dbf     d3,1b
        /* Build the codelen_tree */
        lea     o_codelen_tree(aS),a1
        moveq   #nr_codelen_symbols,d0
        jbsr    build_code      /* build_code(codelen_tree) */
        /* Read the literal/length & distance code lengths */
        move.w  o_hlit(aS),d2
        add.w   o_hdist(aS),d2
        subq.w  #1,d2           /* d2 = hlit+hdist-1 */
        move.l  a0,a2           /* a2 = len[] */
        move.l  a1,a0           /* a0 = a1 = codelen_tree */
1:      INLINE_stream_next_symbol
        cmp.b   #16,d0
        jmi     c_lit
        jeq     c_16
        cmp.b   #17,d0
        jeq     c_17
c_18:   /* 18: Repeat zero N times */
        moveq   #7,d1
        jbsr    stream_next_bits
        addq.w  #11-3,d0
        jra     2f
c_17:   /* 17: repeat zero N times */
        moveq   #3,d1
        jbsr    stream_next_bits
2:      moveq   #0,d1
        jra     3f
c_16:   /* 16: repeat previous N times */
        moveq   #2,d1
        jbsr    stream_next_bits
        move.b  -1(a2),d1
3:      addq.w  #3-1,d0
        sub.w   d0,d2
4:      move.b  d1,(a2)+
        dbf     d0,4b
        jra     5f
c_lit:  /* 0-16: Literal symbol */
        move.b  d0,(a2)+
5:      dbf     d2,1b
        /* Build the lit/len and distance trees */
#if OPT_TABLE_LOOKUP
        /* Clear the codelen tree (shared space with lit/len tree).
         * NB. a0 = a1 = codelen_tree = litlen_tree */
        moveq   #0,d0
        move.w  #LOOKUP_BYTES(nr_codelen_symbols)/4-1,d1
1:      move.l  d0,(a0)+
        dbf     d1,1b
        /* litlen_tree (= codelen_tree) is already in a1, and now zeroed. */
#else
        lea     o_litlen_tree(aS),a1
#endif
        lea     o_lens(aS),a0
        move.w  o_hlit(aS),d0
        jbsr    build_code      /* build_code(litlen_tree) */
        lea     (a0,d0.w),a0
        lea     o_dist_tree(aS),a1
        move.w  o_hdist(aS),d0
        jbsr    build_code      /* build_code(dist_tree) */
        /* Reinstate the main stream if we used the static prefix */
        tst.l   o_stream+8(aS)
        jeq     1f
        movem.l o_stream(aS),d5-d6/a5
        /* Now decode the compressed data stream up to EOB */
1:      lea     o_litlen_tree(aS),a0
        /* START OF HOT LOOP */
2:      INLINE_stream_next_symbol /* litlen_sym */
        cmp.w   #256,d0  /*  8 cy */
        jpl     2f       /*  8 cy */
        /* 0-255: Byte literal */
        move.b  d0,(a4)+ /*  8 cy */
        jra     2b       /* 10 cy */
        /* END OF HOT LOOP -- 34 + ~108 + [34] = ~160 CYCLES */
2:      jeq     9f
        /* 257+: <length,distance> pair */
        lsl.w   #2,d0
        lea     o_length_extra-257*4(aS),a2
        lea     (a2,d0.w),a2
        move.w  (a2)+,d1
        INLINE_stream_next_bits
        add.w   (a2),d0
        move.w  d0,d3           /* d3 = cplen */
        lea     o_dist_tree(aS),a0
        INLINE_stream_next_symbol /* dist_sym */
        lsl.w   #2,d0
        lea     o_dist_extra(aS),a2
        lea     (a2,d0.w),a2
        move.w  (a2)+,d1
        INLINE_stream_next_bits
        add.w   (a2),d0         /* d0 = cpdst */
        neg.w   d0
        lea     (a4,d0.w),a0    /* a0 = outp - cpdst */
#if OPT_UNROLL_COPY_LOOP
        lsr.w   #1,d3
        jcs     4f
        subq.w  #1,d3
3:      move.b  (a0)+,(a4)+
4:      move.b  (a0)+,(a4)+
#else
        subq.w  #1,d3
3:      move.b  (a0)+,(a4)+
#endif
        dbf     d3,3b
        jra     1b
9:      /* 256: End-of-block: we're done */
        lea     o_frame(aS),aS
        rts
#undef o_hdist
#undef o_hlit
#undef o_lens
#undef o_codelen_tree
#undef o_litlen_tree
#undef o_dist_tree
#undef o_frame

#if !OPT_INLINE_FUNCTIONS
stream_next_symbol:
        STREAM_NEXT_SYMBOL
        rts
#endif

        /* Build a base/extra-bits table on the stack */
build_base_extrabits:
#if !OPT_STORAGE_OFFSTACK
        move.l  (sp)+,a0
#endif
1:      move.w  d0,d3
        lsr.w   d4,d3
        subq.w  #1,d3
        jpl     2f
        moveq   #0,d3
2:      moveq   #0,d1
        bset    d3,d1
        sub.w   d1,d2
        move.w  d2,-(aS)
        move.w  d3,-(aS)
        dbf     d0,1b
#if !OPT_STORAGE_OFFSTACK
        jmp     (a0)
#else
        rts
#endif

dispatch: /* Decoder dispatch table. */
        dc.b uncompressed_block - uncompressed_block
        dc.b static_huffman - uncompressed_block
        dc.b dynamic_huffman - uncompressed_block

codelen_order: /* Order of code lengths for the code length alphabet. */
        dc.b 16, 17, 18, 0, 8, 7, 9, 6, 10, 5, 11, 4, 12, 3, 13, 2, 14, 1, 15

        /* a4 = output, a5 = input, all regs preserved
         * a6 = *end* of storage area (only if OPT_STORAGE_OFFSTACK) */
inflate:
        movem.l d0-d6/a0-a5,-(aS)

        /* Build the <length> base/extra-bits table */
        move.l  #258,d2
        move.l  d2,-(aS)
        addq.w  #1,d2
        moveq   #27,d0
        moveq   #2,d4
        jbsr    build_base_extrabits

        /* Build the <distance> base/extra-bits table */
        move.w  #32769,d2
        moveq   #29,d0
        moveq   #1,d4
        jbsr    build_base_extrabits

        /* Initialise the stream */
        moveq   #0,d5           /* d5 = stream: fetched data */
        moveq   #0,d6           /* d6 = stream: nr fetched bits */

1:      /* Process a block: Grab the BTYPE|BFINAL 3-bit code */
        moveq   #3,d1
        jbsr    stream_next_bits
        move.l  d0,-(aS)
        /* Dispatch to the correct decoder for this block */
        lsr.b   #1,d0
        move.b  dispatch(pc,d0.w),d0
        lea     uncompressed_block(pc),a0
        jsr     (a0,d0.w)
        /* Keep going until we see BFINAL=1 */
        move.l  (aS)+,d0
        lsr.b   #1,d0
        jcc     1b

        /* Pop the base/extra-bits lookup tables */
        lea     (30+29)*4(aS),aS

        movem.l (aS)+,d0-d6/a0-a5
        rts

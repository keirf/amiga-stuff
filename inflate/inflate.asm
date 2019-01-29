*
* inflate.asm
* 
* Fixed for Amiga native asm syntax by phx / English Amiga Board.
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
* Space requirements: 638-930 bytes code; 2044-2940 bytes stack.
* (NB1. Above ranges are [No Optimisations]-[All Optimisations])
* (NB2. Stack space can be relocated to a separately-specified storage
*       area, see OPT_STORAGE_OFFSTACK below)
*
* Timings: With all Optimisation Options enabled (see below) this routine
* will decompress on a basic 7MHz 68000 at ~25kB/s. An AmigaDOS track of
* data (5.5kB) is processed in ~220ms. This is only fractionally slower than
* the track can be fetched from disk, hence there is scope for a
* decompressing loader to keep CPU and disk both at near 100% utilisation.
* 
* Written & released by Keir Fraser <keir.xen@gmail.com>
* 
* This is free and unencumbered software released into the public domain.
* See the file COPYING for more details, or visit <http://unlicense.org>.
*

* Optimisation Option #1:
* Avoid long Huffman-tree walks by indexing the first 8 bits of each codeword
* in a 256-entry lookup table. This shortens all walks by 8 steps and since
* the most common codes are less than 8 bits, most tree walks are avoided.
* Also pre-shifts selected symbols in the code->symbol table, ready to be used
* as indexes into further lookup tables.
* SPEEDUP: 41% (c.w. no Options); COST: 122 bytes code, 896 bytes stack */
        ifnd	OPT_TABLE_LOOKUP
OPT_TABLE_LOOKUP	= 1
        endc

* Optimisation Option #2:
* Inline functions in the main decode loop to avoid all BSR/RTS pairs.
* SPEEDUP: 15% (on top of Option #1); COST: 164 bytes code
        ifnd	OPT_INLINE_FUNCTIONS
OPT_INLINE_FUNCTIONS	= 1
        endc

* Optimisation Option #3:
* Unroll the copy loop for <distance,length> tuples by one iteration
* (so two bytes are copied per iteration).
* SPEEDUP: ~1% (on top of Options #1 and #2); COST: 6 bytes code
        ifnd	OPT_UNROLL_COPY_LOOP
OPT_UNROLL_COPY_LOOP	= 1
        endc

* Storage Option:
* All but 12 bytes of this routine's space requirement can be allocated
* off stack, in a data area specified in register a6.
* If this option is set then inflate must be called with a6 pointing at
* the *end* of the reserved storage area (+2032 or +2928 bytes, depending
* on whether OPT_TABLE_LOOKUP is enabled).
* SPEEDUP: none; COST: -2 bytes code (makes code slightly smaller)
        ifnd	OPT_STORAGE_OFFSTACK
OPT_STORAGE_OFFSTACK	= 0
        endc

* By default all lookup/conversion tables are generated on-the-fly on every
* call to inflate. In some cases this can be very inefficient.
* If this option is enabled then two new routines are generated: At start-of-
* day call 'inflate_gentables' with a6 pointing to the *end* of a 6000-byte
* block of memory. Then call 'inflate_fromtables' instead of 'inflate', with
* a6 still pointing to the end of the pre-generated memory block.
* SPEEDUP: variable; COST: 116 bytes code
        ifnd	OPT_PREGENERATE_TABLES
OPT_PREGENERATE_TABLES	= 0
        endc

        ifne	OPT_STORAGE_OFFSTACK
aS	equr	a6
        else
aS	equr	sp
        endc

; Longest possible code.
MAX_CODE_LEN		= 16

; (Maximum) alphabet sizes.
nr_codelen_symbols	= 19
nr_litlen_symbols	= 288
nr_distance_symbols	= 32

; Alphabet-description stream for a static Huffman block (BTYPE=01b).
static_huffman_prefix:
        dc.b $ff,$5b,$00,$6c,$03,$36,$db
        dc.b $b6,$6d,$db,$b6,$6d,$db,$b6
        dc.b $cd,$db,$b6,$6d,$db,$b6,$6d
        dc.b $db,$a8,$6d,$ce,$8b,$6d,$3b

        ifne	OPT_TABLE_LOOKUP

* Number of bytes required for code-lookup table/tree:
*  - 256 2-byte entries for the 8-bit lookup table
*  - Worst-case only 8 symbols decode directly in the table and all the rest
*    are in a tree hanging off one table entry. This tree requires
*    (nr_symbols-8)-1 internal 4-byte nodes.
LOOKUP_BYTES_CODELEN	= 256*2+(nr_codelen_symbols-9)*4
LOOKUP_BYTES_LITLEN	= 256*2+(nr_litlen_symbols-9)*4
LOOKUP_BYTES_DISTANCE	= 256*2+(nr_distance_symbols-9)*4

        ; a0 = len[], a1 = nodes[], d0 = nr_symbols
        ; d1 = symbol beyond which all symbols get <<2
        ; a2-a3 are scratched
build_code:
        movem.l d0-d7,-(aS)

        ; Allocate space for bl_count[]/next_code[] array on stack.
        moveq   #(MAX_CODE_LEN+1)/2,d1
        moveq   #0,d2
.1:     move.l  d2,-(aS)
        dbf     d1,.1

        ; Count occurrences of each code length into bl_count[] array.
        subq.w  #1,d0
        move.w  d0,d1
        move.l  a0,a2           ; a2 = &len[0]
.2:     move.b  (a2)+,d2        ; d2 = len[i]
        ifd MC68020
        addq.w  #1,(aS,d2.w*2)
        else
        add.b   d2,d2
        addq.w  #1,(aS,d2.w)    ; bl_count[len[i]]++
        endif
        dbf     d1,.2

        ; Calculate next_code[] start values for each code length.
        move.l  aS,a2           ; a2 = bl_count[] / next_code[]
        moveq   #MAX_CODE_LEN-1,d1
        moveq   #0,d2           ; d2 = code
        move.w  d2,(aS)         ; bl_count[0] = 0, ignore zero-length codes
.3:     add.w   (a2),d2
        add.w   d2,d2           ; code = (code + bl_count[i-1]) << 1
        move.w  d2,(a2)+        ; next_code[i] = code
        dbf     d1,.3

        ; Create the Huffman-code lookup tree
        move.w  d0,d1
        moveq   #127,d4         ; d4 = next_node = 127
        move.l  a0,a2           ; a2 = &len[0]
build_code_loop:
        moveq   #0,d5
        move.b  (a2)+,d5        ; d5 = len[i] / *len++
        beq.b   build_code_next
        subq.w  #1,d5
        move.w  d5,d6
        ifd MC68020
        move.w  (aS,d6.w*2),d3
        addq.w  #1,(aS,d6.w*2)
        else
        add.w   d6,d6
        move.w  (aS,d6.w),d3    ; d3 = code = next_code[len[i]]++
        addq.w  #1,(aS,d6.w)
        move.w  d5,d6
        endif

        moveq   #0,d2
.1:     lsr.w   #1,d3
        roxl.w  #1,d2
        dbf     d6,.1           ; d5 = codelen-1; d2 = reversed code
        move.b  d2,d3
        add.w   d3,d3           ; d3 = table offset
        move.w  d0,d6
        sub.w   d1,d6           ; d6 = symbol
        cmp.w   (((MAX_CODE_LEN+1)/2)+1)*4+6(aS),d6 ; symbol > saved d1.w?
        bls     .2
        lsl.w   #2,d6           ; symbol <<= 2 if so
.2:     cmp.b   #9-1,d5
        bcc     codelen_gt_8

codelen_le_8: ; codelen <= 8: leaf in table entry(s)
        lsl.w   #3,d6
        or.b    d5,d6           ; d6 = (symbol<<3) | (codelen-1)
        moveq   #0,d2
        addq.b  #2,d5
        bset    d5,d2           ; d2 = 1<<(codelen+1) [table step]
        move.w  d2,d7
        neg.w   d7
        and.w   #511,d7
        or.w    d7,d3           ; d3 = last table offset
.1:     move.w  d6,(a1,d3.w)
        sub.w   d2,d3
        bcc     .1
        bra     build_code_next

codelen_gt_8: ; codelen > 8: requires a tree walk
        lsr.w   #8,d2
        subq.b  #8,d5           ; Skip the first 8 bits of code
        lea     (a1,d3.w),a3    ; pnode = table entry

.1:     ; Walk through *pnode.
        move.w  (a3),d7         ; d3 = *pnode
        bne     .2
        ; Link missing: Create a new internal node
        addq.w  #1,d4
        move.w  d4,d7
        bset    #15,d7
        move.w  d7,(a3)         ; *pnode = ++next_node | INTERNAL
.2:     ; Take left or right branch depending on next code bit
        lsr.b   #1,d2
        addx.w  d7,d7
        ifd MC68020
        lea     (a1,d7.w*2),a3
        else
        add.w   d7,d7
        lea     (a1,d7.w),a3    ; pnode = next_bit ? &node->r : &node->l
        endif
        dbf     d5,.1

        ; Insert the current symbol as a new leaf node
        move.w  d6,(a3)         ; *pnode = sym
build_code_next:
        dbf     d1,build_code_loop

        lea     (((MAX_CODE_LEN+1)/2)+1)*4(aS),aS
        movem.l (aS)+,d0-d7
        rts

        ; d5-d6/a5 = stream, a0 = tree
        ; d0.w = result, d1.l = scratch
STREAM_NEXTSYMBOL macro
        moveq   #0,d0   ; 4
        moveq   #7,d1   ; 4
        cmp.b   d1,d6   ; 4
        bhi     .1\@    ; 10
        ; Less than 8 bits cached; grab another byte from the stream
        move.b  (a5)+,d0 ; [8]
        lsl.w   d6,d0   ; [~14]
        or.w    d0,d5   ; [4] s->cur |= *p++ << s->nr
        addq.b  #8,d6   ; [4] s->nr += 8
        moveq   #0,d0   ; [4]
.1\@:   ; Use next input byte as index into code lookup table
        move.b  d5,d0   ; 4
        ifd MC68020
        move.w  (a0,d0.w*2),d0
        else
        add.w   d0,d0   ; 4
        move.w  (a0,d0.w),d0 ; 14
        endif
        bpl     .4\@    ; 10 (taken)
        ; Code is longer than 8 bits: do the remainder via a tree walk
        lsr.w   #8,d5
        subq.b  #8,d6           ; consume 8 bits from the stream
.2\@:   ; stream_next_bits(1), inlined & optimised
        subq.b  #1,d6           ; 4 cy
        bcc     .3\@            ; 10 cy (taken)
        move.b  (a5)+,d5        ; [8 cy]
        moveq   #7,d6           ; [4 cy]
.3\@:   lsr.w   #1,d5           ; 8 cy
        addx.w  d0,d0           ; 4 cy
        ifd MC68020
        move.w  (a0,d0.w*2),d0
        else
        add.w   d0,d0           ; 4 cy
        move.w  (a0,d0.w),d0    ; 14 cy
        endif
        bmi     .2\@            ; 10 cy (taken); loop on INTERNAL flag
        bra     .5\@            ; TOTAL LOOP CYCLES ~= 54
.4\@:   ; Symbol found directly: consume bits and return symbol
        and.b   d0,d1   ; 4
        addq.b  #1,d1   ; 4
        lsr.w   d1,d5   ; ~16 consume bits from the stream
        sub.b   d1,d6   ; 4
        lsr.w   #3,d0   ; 12  d0 = symbol
.5\@:                   ; ~94 CYCLES TOTAL [+ 34]
        endm

        else ; !OPT_TABLE_LOOKUP

* Number of bytes required for code-lookup tree:
*   - Every binary tree with N leaves has N-1 internal nodes.
*   - Internal nodes require 4 bytes each. Leaves are free.
LOOKUP_BYTES_CODELEN	= ((nr_codelen_symbols)-1)*4
LOOKUP_BYTES_LITLEN	= ((nr_litlen_symbols)-1)*4
LOOKUP_BYTES_DISTANCE	= ((nr_distance_symbols)-1)*4

        ; a0 = len[], a1 = nodes[], d0 = nr_symbols
        ; a2-a3 are scratched
build_code:
        movem.l d0-d5,-(aS)

        ; Allocate space for bl_count[]/next_code[] array on stack.
        moveq   #(MAX_CODE_LEN+1)/2,d1
        moveq   #0,d2
.1:     move.l  d2,-(aS)
        dbf     d1,.1

        ; Count occurrences of each code length into bl_count[] array.
        subq.w  #1,d0
        move.w  d0,d1
        move.l  a0,a2           ; a2 = &len[0]
.2:     move.b  (a2)+,d2        ; d2 = len[i]
        add.b   d2,d2
        addq.w  #1,(aS,d2.w)    ; bl_count[len[i]]++
        dbf     d1,.2

        ; Calculate next_code[] start values for each code length.
        move.l  aS,a2           ; a2 = bl_count[] / next_code[]
        moveq   #MAX_CODE_LEN-1,d1
        moveq   #0,d2           ; d2 = code
        move.w  d2,(aS)         ; bl_count[0] = 0, ignore zero-length codes
.3:     add.w   (a2),d2
        add.w   d2,d2           ; code = (code + bl_count[i-1]) << 1
        move.w  d2,(a2)+        ; next_code[i] = code
        dbf     d1,.3

        ; Create the Huffman-code lookup tree
        move.w  d0,d1
        moveq   #0,d4           ; d4 = next_node
        move.l  a0,a2           ; a2 = &len[0]
build_code_loop:
        moveq   #0,d5
        move.b  (a2)+,d5        ; d5 = len[i] / *len++
        beq     build_code_next
        subq.w  #1,d5
        add.w   d5,d5
        move.w  (aS,d5.w),d3    ; d3 = code = next_code[len[i]]++
        addq.w  #1,(aS,d5.w)
        lsr.w   #1,d5
        ; Walk down the tree, creating nodes as necessary
        moveq   #0,d2           ; d2 = 0 (root node)
        bra     .2

.1:     ; Walk through *pnode.
        move.w  (a3),d2         ; d2 = *pnode
        bne     .2
        ; Link missing: Create a new internal node
        addq.w  #1,d4
        move.w  d4,d2
        bset    #15,d2
        move.w  d2,(a3)         ; *pnode = ++next_node | INTERNAL
.2:     ; Take left or right branch depending on next code bit
        lsl.w   #2,d2
        btst    d5,d3
        beq     .3
        addq.w  #2,d2
.3:     lea     (a1,d2.w),a3    ; pnode = next_bit ? &node->r : &node->l
        dbf     d5,.1

        ; Insert the current symbol as a new leaf node
        move.w  d0,d2
        sub.w   d1,d2
        move.w  d2,(a3)         ; *pnode = sym
build_code_next:
        dbf     d1,build_code_loop

        lea     (((MAX_CODE_LEN+1)/2)+1)*4(aS),aS
        movem.l (aS)+,d0-d5
        rts

        ; d5-d6/a5 = stream, a0 = tree
        ; d0.w = result
STREAM_NEXTSYMBOL macro
        moveq   #0,d0
.1\@:   ; stream_next_bits(1), inlined & optimised
        subq.b  #1,d6           ; 4 cy
        bcc     .2\@            ; 10 cy (taken)
        move.b  (a5)+,d5        ; [8 cy]
        moveq   #7,d6           ; [4 cy]
.2\@:   lsr.w   #1,d5           ; 8 cy
        addx.w  d0,d0           ; 4 cy
        add.w   d0,d0           ; 4 cy
        move.w  (a0,d0.w),d0    ; 14 cy
        bmi     .1\@            ; 10 cy (taken) loop on INTERNAL flag set
                                ; TOTAL LOOP CYCLES ~= 54
        endm

        endc

        ; d1.b = nr, d5-d6/a5 = stream [fetched_bits/nr_fetched_bits/inp]
        ; d0.w = result
STREAM_NEXTBITS macro
.1\@:   moveq   #0,d0
        cmp.b   d1,d6
        bcc     .2\@            ; while (s->nr < nr)
        move.b  (a5)+,d0
        lsl.l   d6,d0
        or.l    d0,d5           ; s->cur |= *p++ << s->nr
        addq.b  #8,d6           ; s->nr += 8
        bra     .1\@
.2\@:   bset    d1,d0
        subq.w  #1,d0           ; d0 = (1<<nr)-1
        and.w   d5,d0           ; d0 = s->cur & ((1<<nr)-1)
        lsr.l   d1,d5           ; s->cur >>= nr
        sub.b   d1,d6           ; s->nr -= nr
        endm

        ifne	OPT_INLINE_FUNCTIONS
INLINE_stream_next_bits macro
        STREAM_NEXTBITS
        endm
INLINE_stream_next_symbol macro
        STREAM_NEXTSYMBOL
        endm
        else
INLINE_stream_next_bits macro
        bsr	stream_next_bits
        endm
INLINE_stream_next_symbol macro
        bsr	stream_next_symbol
        endm
        endc

stream_next_bits:
        STREAM_NEXTBITS
        rts

        ; d5-d6/a5 = stream, a4 = output
        ; d0-d1 are scratched
uncompressed_block:
        ifne	OPT_TABLE_LOOKUP
        ; Push whole bytes back into input stream.
        lsr.w   #3,d6
        sub.w   d6,a5
        else
        ; No need to push bytes back into input stream because stream_next_
        ; {bits,symbol} will never leave more than 7 bits cached.
        endc
        ; Snap input stream up to byte boundary.
        moveq   #0,d5
        moveq   #0,d6
        ; Read block header and copy LEN bytes.
        moveq   #16,d1
        bsr     stream_next_bits ; LEN
        addq.w  #2,a5           ; skip NLEN
        subq.w  #1,d0           ; d0.w = len-1 (for dbf)
.1:     move.b  (a5)+,(a4)+
        dbf     d0,.1
        rts

o_hdist = 0
o_hlit = 2
o_lens = o_hlit+2
o_codelen_tree = o_lens+nr_litlen_symbols+nr_distance_symbols
        ifne	OPT_TABLE_LOOKUP
; Lit/len and codelen lookup structures share space.
o_litlen_tree = o_codelen_tree
        else
o_litlen_tree = o_codelen_tree+LOOKUP_BYTES_CODELEN
        endc
o_dist_tree = o_litlen_tree+LOOKUP_BYTES_LITLEN
o_stream = o_dist_tree+LOOKUP_BYTES_DISTANCE
o_frame = o_stream+3*4
        ifne	OPT_STORAGE_OFFSTACK
o_mode = o_frame
        else
; Allow for BSR return address from decoder
o_mode = o_frame+4
        endc
o_dist_extra = o_mode+4
o_length_extra = o_dist_extra+30*4

        ; d5-d6/a5 = stream, a4 = output
        ; d0-d4,a0-a3 are scratched
static_huffman:
        movem.l d5-d6/a5,-(aS)
        moveq   #0,d5
        moveq   #0,d6
        lea     static_huffman_prefix(pc),a5
        move.w  #o_stream/4-2,d0
        bra     huffman

        ; d5-d6/a5 = stream, a4 = output
        ; d0-d4,a0-a3 are scratched
dynamic_huffman:
        ; Allocate stack space for len[] and node[] arrays
        move.w  #o_frame/4-2,d0
huffman:
        moveq   #0,d1
.1:     move.l  d1,-(aS)
        dbf     d0,.1
        ; HLIT = stream_next_bits(5) + 257
        moveq   #5,d1
        bsr     stream_next_bits
        add.w   #257,d0
        move.w  d0,-(aS)
        ; HDIST = stream_next_bits(5) + 1
        moveq   #5,d1
        bsr     stream_next_bits
        addq.w  #1,d0
        move.w  d0,-(aS)
        ; HCLEN = stream_next_bits(4) + 4
        moveq   #4,d1
        bsr     stream_next_bits
        addq.w  #4-1,d0         ; -1 for dbf
        ; Initialise len[] array with code-length symbol code lengths
        lea     codelen_order(pc),a1
        lea     o_lens(aS),a0   ; a0 = len[]
        moveq   #0,d2
        move.w  d0,d3
.2:     moveq   #3,d1
        bsr     stream_next_bits
        move.b  (a1)+,d2
        move.b  d0,(a0,d2.w)    ; len[codelen_order[i++]] = next_bits(3)
        dbf     d3,.2
        ; Build the codelen_tree
        lea     o_codelen_tree(aS),a1
        moveq   #nr_codelen_symbols,d0
        ifne	OPT_TABLE_LOOKUP
        moveq   #127,d1         ; don't left-shift any symbols
        endc
        bsr     build_code      ; build_code(codelen_tree)
        ; Read the literal/length & distance code lengths
        move.w  o_hlit(aS),d2
        add.w   o_hdist(aS),d2
        subq.w  #1,d2           ; d2 = hlit+hdist-1
        move.l  a0,a2           ; a2 = len[]
        move.l  a1,a0           ; a0 = a1 = codelen_tree
c_loop:
        INLINE_stream_next_symbol
        cmp.b   #16,d0
        bcs     .c_lit
        beq     .c_16
        cmp.b   #17,d0
        beq     .c_17
.c_18:  ; 18: Repeat zero N times
        moveq   #7,d1
        bsr     stream_next_bits
        addq.w  #11-3,d0
        bra     .1
.c_17:  ; 17: repeat zero N times
        moveq   #3,d1
        bsr     stream_next_bits
.1:     moveq   #0,d1
        bra     .2
.c_16:  ; 16: repeat previous N times
        moveq   #2,d1
        bsr     stream_next_bits
        move.b  -1(a2),d1
.2:     addq.w  #3-1,d0
        sub.w   d0,d2
.3:     move.b  d1,(a2)+
        dbf     d0,.3
        bra     .4
.c_lit: ; 0-16: Literal symbol
        move.b  d0,(a2)+
.4:     dbf     d2,c_loop
        ; Build the lit/len and distance trees
        ifne	OPT_TABLE_LOOKUP
        ; Clear the codelen tree (shared space with lit/len tree).
        ; NB. a0 = a1 = codelen_tree = litlen_tree
        moveq   #0,d0
        move.w  #LOOKUP_BYTES_CODELEN/4-1,d1
.5:     move.l  d0,(a0)+
        dbf     d1,.5
        ; litlen_tree (= codelen_tree) is already in a1, and now zeroed.
        else
        lea     o_litlen_tree(aS),a1
        endc
        lea     o_lens(aS),a0
        move.w  o_hlit(aS),d0
        ifne	OPT_TABLE_LOOKUP
        move.w  #256,d1
        move.w  d1,d4           ; left-shift symbols >127 (i.e., lengths)
        endc
        bsr     build_code      ; build_code(litlen_tree)
        add.w   d0,a0
        lea     o_dist_tree(aS),a1
        move.w  o_hdist(aS),d0
        ifne	OPT_TABLE_LOOKUP
        moveq   #0,d1           ; left-shift all symbols (i.e., distances)
        endc
        bsr     build_code      ; build_code(dist_tree)
        ; Reinstate the main stream if we used the static prefix
        tst.l   o_stream+8(aS)
        beq     decode_loop
        movem.l o_stream(aS),d5-d6/a5
        ; Now decode the compressed data stream up to EOB
decode_loop:
        lea     o_litlen_tree(aS),a0
        ; START OF HOT LOOP
.1:     INLINE_stream_next_symbol ; litlen_sym
        ifne	OPT_TABLE_LOOKUP
        cmp.w   d4,d0    ;  4 cy (d4.w = 256)
        else
        cmp.w   #256,d0  ;  8 cy
        endc
        bcc     .2       ;  8 cy
        ; 0-255: Byte literal
        move.b  d0,(a4)+ ;  8 cy
        bra     .1       ; 10 cy
        ; END OF HOT LOOP -- 30 + ~108 + [34] = ~160 CYCLES
.done:  ; 256: End-of-block: we're done
        lea     o_frame(aS),aS
        rts
.2:     beq     .done
        ; 257+: <length,distance> pair
        ifeq	OPT_TABLE_LOOKUP
        lsl.w   #2,d0
        endc
        lea     o_length_extra-257*4(aS),a2
        add.w   d0,a2
        move.w  (a2)+,d1
        INLINE_stream_next_bits
        add.w   (a2),d0
        move.w  d0,d3           ; d3 = cplen
        lea     o_dist_tree(aS),a0
        INLINE_stream_next_symbol ; dist_sym
        ifeq	OPT_TABLE_LOOKUP
        lsl.w   #2,d0
        endc
        lea     o_dist_extra(aS),a2
        add.w   d0,a2
        move.w  (a2)+,d1
        INLINE_stream_next_bits
        add.w   (a2),d0         ; d0 = cpdst
        move.l  a4,a0
        sub.w   d0,a0           ; a0 = outp - cpdst
        ifne	OPT_UNROLL_COPY_LOOP
        lsr.w   #1,d3
        bcs     .4
        subq.w  #1,d3
.3:     move.b  (a0)+,(a4)+
.4:     move.b  (a0)+,(a4)+
        else
        subq.w  #1,d3
.3:     move.b  (a0)+,(a4)+
        endc
        dbf     d3,.3
        bra     decode_loop

        ifeq	OPT_INLINE_FUNCTIONS
stream_next_symbol:
        STREAM_NEXTSYMBOL
        rts
        endc

        ; Build a base/extra-bits table on the stack
        ; d0 = #pairs-1, d2 = max_value, d4 = log_2(extrabits_repeat)
build_base_extrabits:
        ifeq	OPT_STORAGE_OFFSTACK
        move.l  (sp)+,a0
        endc
.1:     move.w  d0,d3
        lsr.w   d4,d3
        subq.w  #1,d3
        bcc     .2
        moveq   #0,d3
.2:     moveq   #0,d1
        bset    d3,d1           ; d1 = 1 << extrabits
        sub.w   d1,d2           ; d2 = base
        move.w  d2,-(aS)
        move.w  d3,-(aS)
        dbf     d0,.1
        ifeq	OPT_STORAGE_OFFSTACK
        jmp     (a0)
        else
        rts
        endc

dispatch: ; Decoder dispatch table.
        dc.b uncompressed_block-uncompressed_block
        dc.b static_huffman-uncompressed_block
        dc.b dynamic_huffman-uncompressed_block

codelen_order: ; Order of code lengths for the code length alphabet.
        dc.b 16,17,18,0,8,7,9,6,10,5,11,4,12,3,13,2,14,1,15

        ; a4 = output, a5 = input, all regs preserved
        ; a6 = *end* of storage area (only if OPT_STORAGE_OFFSTACK)
inflate:
        movem.l d0-d6/a0-a5,-(aS)

        ; Build the <length> base/extra-bits table
        move.l  #258,d2
        move.l  d2,-(aS)
        addq.w  #1,d2
        moveq   #27,d0
        moveq   #2,d4
        bsr     build_base_extrabits

        ; Build the <distance> base/extra-bits table
        move.w  #32769,d2
        moveq   #29,d0
        moveq   #1,d4
        bsr     build_base_extrabits

        ; Initialise the stream
        moveq   #0,d5           ; d5 = stream: fetched data
        moveq   #0,d6           ; d6 = stream: nr fetched bits

.1:     ; Process a block: Grab the BTYPE|BFINAL 3-bit code
        moveq   #3,d1
        bsr     stream_next_bits
        move.l  d0,-(aS)
        ; Dispatch to the correct decoder for this block
        lsr.b   #1,d0
        move.b  dispatch(pc,d0.w),d0
        lea     uncompressed_block(pc),a0
        jsr     (a0,d0.w)
        ; Keep going until we see BFINAL=1
        move.l  (aS)+,d0
        lsr.b   #1,d0
        bcc     .1

        ; Pop the base/extra-bits lookup tables
        lea     (30+29)*4(aS),aS

        movem.l (aS)+,d0-d6/a0-a5
        rts

        ifne    OPT_PREGENERATE_TABLES
pregen_static_huffman:
        lea     -o_frame(aS),aS         ; frame pre-generated; skip over it
        move.w  #256,d4
        bra     decode_loop
pregen_dynamic_huffman:
        move.l  (aS),d0
        lea     -3000(aS),aS            ; move to dynamic-huffman frame
        move.l  d0,(aS)                 ; copy o_mode into it
        bsr     dynamic_huffman
        lea     3000(aS),aS
        rts

        ; Pre-generate conversion tables for Inflate.
        ; a6 = Pointer to end of 6000-byte block of memory to contain
        ; pre-generated tables. All registers preserved.
inflate_gentables:
        movem.l a5-a6,-(sp)
        lea     pregen_dummy_block(pc),a5
        bsr     inflate                 ; static block
        lea     -3000(aS),aS
        lea     pregen_dummy_block(pc),a5
        bsr     inflate                 ; dynamic block
        movem.l (sp)+,a5-a6
        rts

        ; Inflate, using pre-generated tables.
        ; a4 = output, a5 = input, all regs preserved
        ; a6 = *end* of 6000-byte pre-generated storage area
inflate_fromtables:
        movem.l d0-d6/a0-a5,-(aS)

        ; Skip the pre-generated base/extra-bits lookup tables
        lea     -(30+29)*4(aS),aS

        ; Initialise the stream
        moveq   #0,d5           ; d5 = stream: fetched data
        moveq   #0,d6           ; d6 = stream: nr fetched bits

.1:     ; Process a block: Grab the BTYPE|BFINAL 3-bit code
        moveq   #3,d1
        bsr     stream_next_bits
        move.l  d0,-(aS)
        ; Dispatch to the correct decoder for this block
        and.b   #$fe,d0
        move.w  pregen_dispatch(pc,d0.w),d0
        lea     uncompressed_block(pc),a0
        jsr     (a0,d0.w)
        ; Keep going until we see BFINAL=1
        move.l  (aS)+,d0
        lsr.b   #1,d0
        bcc     .1

        ; Pop the base/extra-bits lookup tables
        lea     (30+29)*4(aS),aS

        movem.l (aS)+,d0-d6/a0-a5
        rts

pregen_dispatch:
        dc.w uncompressed_block - uncompressed_block
        dc.w pregen_static_huffman - uncompressed_block
        dc.w pregen_dynamic_huffman - uncompressed_block
pregen_dummy_block: ; A single static block containing EOB symbol only
        dc.b $03,$00
        endc ; OPT_PREGENERATE_TABLES

        end

/*
 * test_inflate.S
 *
 * Test harness for inflate.S
 * 
 * Written & released by Keir Fraser <keir.xen@gmail.com>
 * 
 * This is free and unencumbered software released into the public domain.
 * See the file COPYING for more details, or visit <http://unlicense.org>.
 */

        .text
start:
        // Calculate input parameters:
        //  a0 = stream_start (start of DEFLATE stream);
        //  a1 = buffer_start (start of decompression buffer);
        //  d0.l = stream_len (DEFLATE stream length in bytes);
        //  d1.l = original_len (unpacked file length);
        //  d2.l = leeway / security #bytes
        // On stack: original_len, header_crc
        lea     input(pc),a0
        move.l  a0,a1           // a1 = buffer_start
        movem.l (a0)+,d0-d2     // a0 = stream_start, d0=in_len, d1=out_len
        swap    d2
        movem.l d1-d2,-(sp)     // save original_len and header_crc.w
        clr.w   d2
        swap    d2              // d2.l = leeway
        sub.l   #12+2,d0        // d0.l = stream_len = DEFLATE stream length
        // Calculate a1 = End of output buffer (longword aligned)
        add.l   d1,d2           // d2 = original_len + leeway
        moveq   #0,d1
        move.b  d0,d1
        neg.b   d1
        and.b   #3,d1           // d1.l = -stream_len & 3 (= input padding)
        add.l   d1,d2           // d2 += input padding (extra leeway)
        addq.l  #3,d2
        and.b   #0xfc,d2        // d2 = buffer_size, rounded up to a longword
        add.l   d2,a1           // a1 = buffer_end
        // Calculate d0 = DEFLATE stream length, rounded to longwoprd multiple
        addq.l  #3,d0
        and.b   #0xfc,d0
        // Calculate a0 = End of DEFLATE stream (longword aligned)
        add.l   d0,a0           // a0 = stream_end
        // Output buffer must include the full DEFLATE stream
        cmp.l   a0,a1           // stream_end > buffer_end?
        jcc     1f
        move.l  a0,a1           // buffer_end = stream_end
1:      // Compute CRC table
        move.l  a1,-(sp)        // Save crctable
        jbsr    crc16_ccitt_gentable
        // Copy DEFLATE stream to end of output buffer
        lsr.l   #2,d0           // d0 = DEFLATE stream length in longwords
1:      move.l  -(a0),-(a1)
        subq.l  #1,d0
        jne     1b
        // INFLATE
        move.l  a1,a5           // a5 = input stream
        lea     input(pc),a4    // a4 = output buffer
        jbsr    inflate
        // Compute CRC over unpacked data
        move.l  (sp)+,a1        // a1 = crctable
        move.l  a4,a0           // a0 = data
        movem.l (sp)+,d1-d2     // d1.l = original_len; d2.w = header_crc
        moveq   #-1,d0          // d0.w = in_crc
        jbsr    crc16_ccitt
        // CRC correct?
        cmp.w   d0,d2
        rts                     // d0.w and d2.w should match!
        
#include "../attic/crc16_ccitt.S"        
#include "inflate.S"

        .balign 4
input:

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
        lea     input(pc),a0
        move.l  a0,a1           // a1 = out_start
        movem.l (a0)+,d0-d2     // a0 = in_start
        swap    d2
        move.l  d2,d3           // d2.w = out_crc
        clr.w   d3
        swap    d3              // d3.l = leeway
        sub.l   #12+2,d0        // subtract header and footer from input
        move.b  d0,d4
        subq.b  #4,d4
        and.l   #3,d4
        add.l   d4,d3           // extend leeway for input padding
        add.l   d1,d3           // d3 = output buffer size (inc leeway)
        addq.l  #3,d0
        and.b   #0xfc,d0        // d0 = input size rounded up to a long
        add.l   d0,a0           // a0 = in_end
        move.l  d0,d4
        move.l  d1,d0           // d0 = output size (in bytes)
        addq.l  #3,d3
        and.b   #0xfc,d3        // d3 = output buffer rounded up to a long
        add.l   d3,a1
        move.l  a1,a2           // a2 = crctable
        jbsr    crc16_ccitt_gentable
        lsr.l   #2,d4
1:      move.l  -(a0),-(a1)
        subq.l  #1,d4
        jne     1b
        move.l  a1,a5           // a5 = input stream
        lea     input(pc),a4    // a4 = output buffer
        jbsr    inflate
        // d0.l = output size
        // d2.w = output crc
        move.l  a2,a1           // a1 = crctable
        move.l  a4,a0           // a0 = data
        move.l  d0,d1           // d1 = #bytes
        moveq   #-1,d0          // d0 = in_crc
        jbsr    crc16_ccitt
        cmp.w   d0,d2
        rts                     // d0.w and d2.w should match!
        
#include "../attic/crc16_ccitt.S"        
#include "inflate.S"

.align 4
input:

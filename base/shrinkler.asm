; Copyright 1999-2015 Aske Simon Christensen.
;
; The code herein is free to use, in whole or in part,
; modified or as is, for any legal purpose.
;
; No warranties of any kind are given as to its behavior
; or suitability.


INIT_ONE_PROB		=	$8000
ADJUST_SHIFT		=	4
SINGLE_BIT_CONTEXTS	=	1
NUM_CONTEXTS		=	1536


; Decompress Shrinkler-compressed data produced with the --data option.
;
; A0 = Compressed data
; A1 = Decompressed data destination
; A2 = Progress callback, can be zero if no callback is desired.
;      Callback will be called continuously with
;      D0 = Number of bytes decompressed so far
;      A0 = Callback argument
; A3 = Callback argument
;
; Uses 3 kilobytes of space on the stack.
; Preserves D2-D7/A2-A6 and assumes callback does the same.
;
; Decompression code may read one longword beyond compressed data.
; The contents of this longword does not matter.

ShrinklerDecompress:
	movem.l	d2-d7/a4-a6,-(a7)

	move.l	a0,a4
	move.l	a1,a5
	move.l	a1,a6

	; Init range decoder state
	moveq.l	#0,d2
	moveq.l	#1,d3
	moveq.l	#1,d4
	ror.l	#1,d4

	; Init probabilities
	move.l	#NUM_CONTEXTS,d6
.init:	move.w	#INIT_ONE_PROB,-(a7)
	subq.w	#1,d6
	bne.b	.init

	; D6 = 0
.lit:
	; Literal
	addq.b	#1,d6
.getlit:
	bsr.b	GetBit
	addx.b	d6,d6
	bcc.b	.getlit
	move.b	d6,(a5)+
	bsr.b	ReportProgress
.switch:
	; After literal
	bsr.b	GetKind
	bcc.b	.lit
	; Reference
	moveq.l	#-1,d6
	bsr.b	GetBit
	bcc.b	.readoffset
.readlength:
	moveq.l	#4,d6
	bsr.b	GetNumber
.copyloop:
	move.b	(a5,d5.l),(a5)+
	subq.l	#1,d7
	bne.b	.copyloop
	bsr.b	ReportProgress
	; After reference
	bsr.b	GetKind
	bcc.b	.lit
.readoffset:
	moveq.l	#3,d6
	bsr.b	GetNumber
	moveq.l	#2,d5
	sub.l	d7,d5
	bne.b	.readlength

	lea.l	NUM_CONTEXTS*2(a7),a7
	movem.l	(a7)+,d2-d7/a4-a6
	rts

ReportProgress:
	move.l	a2,d0
	beq.b	.nocallback
	move.l	a5,d0
	sub.l	a6,d0
	move.l	a3,a0
	jsr	(a2)
.nocallback:
	rts

GetKind:
	; Use parity as context
	move.l	a5,d1
	moveq.l	#1,d6
	and.l	d1,d6
	lsl.w	#8,d6
	bra.b	GetBit

GetNumber:
	; D6 = Number context

	; Out: Number in D7
	lsl.w	#8,d6
.numberloop:
	addq.b	#2,d6
	bsr.b	GetBit
	bcs.b	.numberloop
	moveq.l	#1,d7
	subq.b	#1,d6
.bitsloop:
	bsr.b	GetBit
	addx.l	d7,d7
	subq.b	#2,d6
	bcc.b	.bitsloop
	rts

	; D6 = Bit context

	; D2 = Range value
	; D3 = Interval size
	; D4 = Input bit buffer

	; Out: Bit in C and X

readbit:
	add.l	d4,d4
	bne.b	nonewword
	move.l	(a4)+,d4
	addx.l	d4,d4
nonewword:
	addx.w	d2,d2
	add.w	d3,d3
GetBit:
	tst.w	d3
	bpl.b	readbit

	lea.l	4+SINGLE_BIT_CONTEXTS*2(a7,d6.l),a1
	add.l	d6,a1
	move.w	(a1),d1
	; D1 = One prob

	lsr.w	#ADJUST_SHIFT,d1
	sub.w	d1,(a1)
	add.w	(a1),d1

	mulu.w	d3,d1
	swap.w	d1

	sub.w	d1,d2
	blo.b	.one
.zero:
	; oneprob = oneprob * (1 - adjust) = oneprob - oneprob * adjust
	sub.w	d1,d3
	; 0 in C and X
	rts
.one:
	; onebrob = 1 - (1 - oneprob) * (1 - adjust) = oneprob - oneprob * adjust + adjust
	add.w	#$ffff>>ADJUST_SHIFT,(a1)
	move.w	d1,d3
	add.w	d1,d2
	; 1 in C and X
	rts

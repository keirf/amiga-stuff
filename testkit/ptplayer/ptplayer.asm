;**************************************************
;*    ----- Protracker V2.3B Playroutine -----	  *
;**************************************************
;
; XXXXXXXX WARNING XXXXXXXXX
; This has been hacked for Amiga Test Kit.
;  - Functionality has been removed (master volume, sound fx)
;  - A cheesy method to nobble channels has been added (mt_disablemask)
; YOU PROBABLY DON'T WANT TO USE THIS OUTSIDE AMIGA TEST KIT!
; Go download Frank Wille's original instead :D
; (NOTE: This version is, however, in the Public Domain, just as the original.)
; XXXXXXXX WARNING XXXXXXXXX
;
; Version 5.3
; Written by Frank Wille in 2013, 2016, 2017, 2018, 2019.
;
; I, the copyright holder of this work, hereby release it into the
; public domain. This applies worldwide.
;
; The default version (single section, local base register) should
; work with most assemblers. Tested are: Devpac, vasm, PhxAss,
; Barfly-Asm, SNMA, AsmOne, AsmPro.
;
; The small data model can be enabled by defining the symbol SDATA. In
; this case all functions expect a4 to be initialised with the small
; data base register. Interrupt functions restore a4 from _LinkerDB.
; Small data may work with vasm and PhxAss only.
;
; Exported functions and variables:
; (CUSTOM is the custom-chip register set base address $dff000.)
;
; _mt_install_cia(a6=CUSTOM, a0=VectorBase, d0=PALflag.b)
;   Install a CIA-B interrupt for calling _mt_music or mt_sfxonly.
;   The music module is replayed via _mt_music when _mt_Enable is non-zero.
;   Otherwise the interrupt handler calls mt_sfxonly to play sound
;   effects only. VectorBase is 0 for 68000, otherwise set it to the CPU's
;   VBR register. A non-zero PALflag selects PAL-clock for the CIA timers
;   (NTSC otherwise).
;
; _mt_remove_cia(a6=CUSTOM)
;   Remove the CIA-B music interrupt, restore the previous handler and
;   reset the CIA timer registers to their original values.
;
; _mt_init(a6=CUSTOM, a0=TrackerModule, a1=Samples|NULL, d0=InitialSongPos.b)
;   Initialize a new module.
;   Reset speed to 6, tempo to 125 and start at the given song position.
;   Master volume is at 64 (maximum).
;   When a1 is NULL the samples are assumed to be stored after the patterns.
;
; _mt_end(a6=CUSTOM)
;   Stop playing current module.
;
; _mt_disablemask(a6=CUSTOM, d0=ChannelMask.b)
;   Bits set in the mask define which specific channels are disabled.
;   Set bit 0 for channel 0, ..., bit 3 for channel 3.
;   The mask defaults to 0.
;
; _mt_music(a6=CUSTOM)
;   The replayer routine. Is called automatically after _mt_install_cia.
;
; Byte Variables:
;
; _mt_Enable
;   Set this byte to non-zero to play music, zero to pause playing.
;   Note that you can still play sound effects, while music is stopped.
;   It is set to 0 by _mt_install_cia.
;
; _mt_SongEnd
;   Set to -1 ($ff) if you want the song to stop automatically when
;   the last position has been played (clears _mt_Enable). Otherwise, the
;   song is restarted and _mt_SongEnd is incremented to count the restarts.
;   It is reset to 0 after _mt_init.
;
; _mt_E8Trigger
;   This byte reflects the value of the last E8 command.
;   It is reset to 0 after _mt_init.
;

		include	"custom.i"
		include	"cia.i"


; Audio channel registers
AUDLC		equ	0
AUDLEN		equ	4
AUDPER		equ	6
AUDVOL		equ	8


; Channel Status
		rsreset
n_note		rs.w	1
n_cmd		rs.b	1
n_cmdlo 	rs.b	1
n_start 	rs.l	1
n_loopstart	rs.l	1
n_length	rs.w	1
n_replen	rs.w	1
n_period	rs.w	1
n_volume	rs.w	1
n_pertab	rs.l	1
n_dmabit	rs.w	1
n_noteoff	rs.w	1
n_toneportspeed rs.w	1
n_wantedperiod	rs.w	1
n_pattpos	rs.w	1
n_funk		rs.w	1
n_wavestart	rs.l	1
n_reallength	rs.w	1
n_intbit	rs.w	1
n_audreg	rs.w	1
n_looped	rs.b	1
n_minusft	rs.b	1
n_vibratoamp	rs.b	1
n_vibratospd	rs.b	1
n_vibratopos	rs.b	1
n_vibratoctrl	rs.b	1
n_tremoloamp	rs.b	1
n_tremolospd	rs.b	1
n_tremolopos	rs.b	1
n_tremoloctrl	rs.b	1
n_gliss		rs.b	1
n_sampleoffset	rs.b	1
n_loopcount	rs.b	1
n_funkoffset	rs.b	1
n_retrigcount	rs.b	1
n_freecnt	rs.b	1
n_disabled	rs.b	1
n_pad           rs.b    1
n_sizeof	rs.b	0


	ifd	SDATA
	xref	_LinkerDB		; small data base from linker
	near	a4
	code
	endc



;---------------------------------------------------------------------------
	xdef	_mt_install_cia
_mt_install_cia:
; Install a CIA-B interrupt for calling _mt_music.
; a6 = CUSTOM
; a0 = VectorBase
; d0 = PALflag.b (0 is NTSC)

	ifnd	SDATA
	move.l	a4,-(sp)
	lea	mt_data(pc),a4
	endc

	clr.b	mt_Enable(a4)

	lea	mt_Lev6Int(pc),a1
	lea	$78(a0),a0		; Level 6 interrupt vector
	move.l	a0,(a1)

	move.w	#$2000,d1
	and.w	INTENAR(a6),d1
	or.w	#$8000,d1
	move.w	d1,mt_Lev6Ena(a4)	; remember level 6 interrupt enable

	; disable level 6 EXTER interrupts, set player interrupt vector
	move.w	#$2000,INTENA(a6)
	move.l	(a0),mt_oldLev6(a4)
	lea	mt_TimerAInt(pc),a1
	move.l	a1,(a0)

	; disable CIA-B interrupts, stop and save all timers
	lea	CIAB,a0
	move.b	#$7f,CIAICR(a0)
	move.b	#$10,CIACRA(a0)
	move.b	#$10,CIACRB(a0)

	; determine if 02 clock for timers is based on PAL or NTSC
	tst.b	d0
	bne	.1
	move.l	#1789773,d0		; NTSC
	bra	.2
.1:	move.l	#1773447,d0		; PAL
.2:	move.l	d0,mt_timerval(a4)

	; load TimerA in continuous mode for the default tempo of 125
	divu	#125,d0
	move.b	d0,CIATALO(a0)
	lsr.w	#8,d0
	move.b	d0,CIATAHI(a0)
	move.b	#$11,CIACRA(a0)		; load timer, start continuous

	; load TimerB with 496 ticks for setting DMA and repeat
	move.b	#496&255,CIATBLO(a0)
	move.b	#496>>8,CIATBHI(a0)

	; TimerA and TimerB interrupt enable
	move.b	#$83,CIAICR(a0)

	; enable level 6 interrupts
	move.w	#$a000,INTENA(a6)

	bra	mt_reset

mt_Lev6Int:
	dc.l	0


;---------------------------------------------------------------------------
	xdef	_mt_remove_cia
_mt_remove_cia:
; Remove CIA-B music interrupt and restore the old vector.
; a6 = CUSTOM

	ifnd	SDATA
	move.l	a4,-(sp)
	lea	mt_data(pc),a4
	endc

	; disable level 6 and CIA-B interrupts
	lea	CIAB,a0
	move.b	#$7f,CIAICR(a0)
	move.w	#$2000,INTENA(a6)

	; restore original level 6 interrupt vector
	move.l	mt_Lev6Int(pc),a1
	move.l	mt_oldLev6(a4),(a1)

	; reenable previous level 6 interrupt
	move.w	mt_Lev6Ena(a4),INTENA(a6)

	ifnd	SDATA
	move.l	(sp)+,a4
	endc
	rts


;---------------------------------------------------------------------------
mt_TimerAInt:
; TimerA interrupt calls _mt_music at a selectable tempo (Fxx command),
; which defaults to 50 times per second.

	movem.l	d0-d7/a0-a6,-(sp)
	lea	CUSTOM,a6
	ifd	SDATA
	lea	_LinkerDB,a4
	else
	lea	mt_data(pc),a4
	endc

	; check and clear CIAB interrupt flags
	move.b	CIAB+CIAICR,d0
	btst	#1,d0
	beq	.2
	tst.b	mt_TimBRpt(a4)
	bne	.3
	bsr	mt_TimerBdmaon
	bra	.2
.3:     bsr	mt_TimerBsetrep
.2:	btst	#0,d0
	beq	.1

	; it was a TA interrupt, do music when enabled
	tst.b	mt_Enable(a4)
	beq	.1

	bsr	_mt_music

.1:	; clear EXTER interrupt flag
	move.w	#$2000,INTREQ(a6)
	move.w	#$2000,INTREQ(a6)
	movem.l	(sp)+,d0-d7/a0-a6
	rte


;---------------------------------------------------------------------------
mt_TimerBdmaon:
; One-shot TimerB interrupt to enable audio DMA after 496 ticks.

	; it was a TB interrupt, restart timer to set repeat, enable DMA
	move.b	#$19,CIAB+CIACRB
	move.w	mt_dmaon(pc),DMACON(a6)

	; set level 6 interrupt to mt_TimerBsetrep
	st	mt_TimBRpt(a4)
	rts

mt_dmaon:
	dc.w	$8000


;---------------------------------------------------------------------------
mt_TimerBsetrep:
; One-shot TimerB interrupt to set repeat samples after another 496 ticks.

	; it was a TB interrupt, set repeat sample pointers and lengths
	move.l	mt_chan1+n_loopstart(a4),AUD0LC(a6)
	move.w	mt_chan1+n_replen(a4),AUD0LEN(a6)
	move.l	mt_chan2+n_loopstart(a4),AUD1LC(a6)
	move.w	mt_chan2+n_replen(a4),AUD1LEN(a6)
	move.l	mt_chan3+n_loopstart(a4),AUD2LC(a6)
	move.w	mt_chan3+n_replen(a4),AUD2LEN(a6)
	move.l	mt_chan4+n_loopstart(a4),AUD3LC(a6)
	move.w	mt_chan4+n_replen(a4),AUD3LEN(a6)

	clr.b   mt_TimBRpt(a4)
	rts


;---------------------------------------------------------------------------
	xdef	_mt_init
_mt_init:
; Initialize new module.
; Reset speed to 6, tempo to 125 and start at given song position.
; Master volume is at 64 (maximum).
; a6 = CUSTOM
; a0 = module pointer
; a1 = sample pointer (NULL means samples are stored within the module)
; d0 = initial song position

	ifnd	SDATA
	move.l	a4,-(sp)
	lea	mt_data(pc),a4
	endc

	move.l	a0,mt_mod(a4)
	movem.l	d2/a2,-(sp)

	; set initial song position
	cmp.b	950(a0),d0
	blo	.1
	moveq	#0,d0
.1:	move.b	d0,mt_SongPos(a4)

	move.l	a1,d0		; sample data location is given?
	bne	.4

	; get number of highest pattern
	lea	952(a0),a1	; song arrangement list
	moveq	#127,d0
	moveq	#0,d2
.2:	move.b	(a1)+,d1
	cmp.b	d2,d1
	bls	.3
	move.b	d1,d2
.3:	dbf	d0,.2
	addq.b	#1,d2		; number of patterns

	; now we can calculate the base address of the sample data
	moveq	#10,d0
	asl.l	d0,d2
	lea	(a0,d2.l),a1
	add.w	#1084,a1

	; save start address of each sample and do some fixes for broken mods
.4:	lea	mt_SampleStarts(a4),a2
	moveq	#1,d2
	moveq	#31-1,d0
.5:	move.l	a1,(a2)+
	moveq	#0,d1
	move.w	42(a0),d1
	cmp.w	d2,d1		; length 0 and 1 define unused samples
	bls	.6
	add.l	d1,d1
	add.l	d1,a1
	bra	.7
.6:	clr.w	42(a0)		; length 1 means zero -> no sample
.7:	lea	30(a0),a0
	dbf	d0,.5

	movem.l	(sp)+,d2/a2

	; reset CIA timer A to default (125)
	move.l	mt_timerval(a4),d0
	divu	#125,d0
	move.b	d0,CIAB+CIATALO
	lsr.w	#8,d0
	move.b	d0,CIAB+CIATAHI

mt_reset:
; a4 must be initialised with base register

	; reset speed and counters
	move.b	#6,mt_Speed(a4)
	clr.b	mt_Counter(a4)
	clr.w	mt_PatternPos(a4)

	; initialise channel DMA, interrupt bits and audio register base
	move.w	#$0001,mt_chan1+n_dmabit(a4)
	move.w	#$0002,mt_chan2+n_dmabit(a4)
	move.w	#$0004,mt_chan3+n_dmabit(a4)
	move.w	#$0008,mt_chan4+n_dmabit(a4)
	move.w	#$0080,mt_chan1+n_intbit(a4)
	move.w	#$0100,mt_chan2+n_intbit(a4)
	move.w	#$0200,mt_chan3+n_intbit(a4)
	move.w	#$0400,mt_chan4+n_intbit(a4)
	move.w	#AUD0LC,mt_chan1+n_audreg(a4)
	move.w	#AUD1LC,mt_chan2+n_audreg(a4)
	move.w	#AUD2LC,mt_chan3+n_audreg(a4)
	move.w	#AUD3LC,mt_chan4+n_audreg(a4)

	; make sure n_period doesn't start as 0
	move.w	#320,d0
	move.w	d0,mt_chan1+n_period(a4)
	move.w	d0,mt_chan2+n_period(a4)
	move.w	d0,mt_chan3+n_period(a4)
	move.w	d0,mt_chan4+n_period(a4)

	clr.b	mt_SilCntValid(a4)
	clr.b	mt_E8Trigger(a4)
	clr.b	mt_SongEnd(a4)
	clr.b	mt_TimBRpt(a4)

	ifnd	SDATA
	move.l	(sp)+,a4
	endc


;---------------------------------------------------------------------------
	xdef	_mt_end
_mt_end:
; Stop playing current module.
; a6 = CUSTOM

	ifd	SDATA
	clr.b	mt_Enable(a4)
	else
	lea	mt_data+mt_Enable(pc),a0
	clr.b	(a0)
	endc

	moveq	#0,d0
	move.w	d0,AUD0VOL(a6)
	move.w	d0,AUD1VOL(a6)
	move.w	d0,AUD2VOL(a6)
	move.w	d0,AUD3VOL(a6)
	move.w	#$000f,DMACON(a6)
	rts


;---------------------------------------------------------------------------
	xdef	_mt_disablemask
_mt_disablemask:
; Set bits in the mask define which specific channels are disabled.
; a6 = CUSTOM
; d0.b = channel-mask (bit 0 for channel 0, ..., bit 3 for channel 3)

	ifnd	SDATA
	move.l	a4,-(sp)
	lea	mt_data(pc),a4
	endc

	lsl.b	#5,d0
	scs	mt_chan4+n_disabled(a4)
        add.b	d0,d0
	scs	mt_chan3+n_disabled(a4)
        add.b	d0,d0
	scs	mt_chan2+n_disabled(a4)
        add.b	d0,d0
	scs	mt_chan1+n_disabled(a4)

	ifnd	SDATA
	move.l	(sp)+,a4
	endc
	rts


;---------------------------------------------------------------------------
	xdef	_mt_music
_mt_music:
; Called from interrupt.
; Play next position when Counter equals Speed.
; Effects are always handled.
; a6 = CUSTOM

	moveq	#0,d7			; d7 is always zero

	lea	mt_dmaon+1(pc),a0
	move.b	d7,(a0)

	addq.b	#1,mt_Counter(a4)

	move.b	mt_Counter(a4),d0
	cmp.b	mt_Speed(a4),d0
	blo	no_new_note

	; handle a new note
	move.b	d7,mt_Counter(a4)
	tst.b	mt_PattDelTime2(a4)
	beq	get_new_note

	; we have a pattern delay, check effects then step
	lea	AUD0LC(a6),a5
	lea	mt_chan1(a4),a2
	bsr	mt_checkfx
	lea	AUD1LC(a6),a5
	lea	mt_chan2(a4),a2
	bsr	mt_checkfx
	lea	AUD2LC(a6),a5
	lea	mt_chan3(a4),a2
	bsr	mt_checkfx
	lea	AUD3LC(a6),a5
	lea	mt_chan4(a4),a2
	bsr	mt_checkfx
	bra	settb_step

no_new_note:
	; no new note, just check effects, don't step to next position
	lea	AUD0LC(a6),a5
	lea	mt_chan1(a4),a2
	bsr	mt_checkfx
	lea	AUD1LC(a6),a5
	lea	mt_chan2(a4),a2
	bsr	mt_checkfx
	lea	AUD2LC(a6),a5
	lea	mt_chan3(a4),a2
	bsr	mt_checkfx
	lea	AUD3LC(a6),a5
	lea	mt_chan4(a4),a2
	bsr	mt_checkfx

	; set one-shot TimerB interrupt for enabling DMA, when needed
	move.b	mt_dmaon+1(pc),d0
	beq	same_pattern

	clr.b	mt_TimBRpt(a4)
	move.b	#$19,CIAB+CIACRB	; load/start timer B, one-shot
	bra	same_pattern

get_new_note:
	; determine pointer to current pattern line
	move.l	mt_mod(a4),a0
	lea	12(a0),a3		; sample info table
	lea	1084(a0),a1		; pattern data
	lea	952(a0),a0
	moveq	#0,d0
	move.b	mt_SongPos(a4),d0
	move.b	(a0,d0.w),d0		; current pattern number
	swap	d0
	lsr.l	#6,d0
	add.l	d0,a1			; pattern base
	add.w	mt_PatternPos(a4),a1	; a1 pattern line

	; play new note for each channel, apply some effects
	lea	AUD0LC(a6),a5
	lea	mt_chan1(a4),a2
	bsr	mt_playvoice
	lea	AUD1LC(a6),a5
	lea	mt_chan2(a4),a2
	bsr	mt_playvoice
	lea	AUD2LC(a6),a5
	lea	mt_chan3(a4),a2
	bsr	mt_playvoice
	lea	AUD3LC(a6),a5
	lea	mt_chan4(a4),a2
	bsr	mt_playvoice

settb_step:
	; set one-shot TimerB interrupt for enabling DMA, when needed
	move.b	mt_dmaon+1(pc),d0
	beq	pattern_step

	clr.b	mt_TimBRpt(a4)
	move.b	#$19,CIAB+CIACRB	; load/start timer B, one-shot

pattern_step:
	; next pattern line, handle delay and break
	clr.b	mt_SilCntValid(a4)	; recalculate silence counters
	moveq	#16,d2			; offset to next pattern line

	move.b	mt_PattDelTime2(a4),d1
	move.b	mt_PattDelTime(a4),d0
	beq	.1
	move.b	d0,d1
	move.b	d7,mt_PattDelTime(a4)
.1:	tst.b	d1
	beq	.3
	subq.b	#1,d1
	beq	.2
	moveq	#0,d2			; do not advance to next line
.2:	move.b	d1,mt_PattDelTime2(a4)

.3:	add.w	mt_PatternPos(a4),d2	; d2 PatternPos

	; check for break
	bclr	#0,mt_PBreakFlag(a4)
	beq	.4
	move.w	mt_PBreakPos(a4),d2
	move.w	d7,mt_PBreakPos(a4)

	; check whether end of pattern is reached
.4:	move.w	d2,mt_PatternPos(a4)
	cmp.w	#1024,d2
	blo	same_pattern

song_step:
	move.w	mt_PBreakPos(a4),mt_PatternPos(a4)
	move.w	d7,mt_PBreakPos(a4)
	move.b	d7,mt_PosJumpFlag(a4)

	; next position in song
	moveq	#1,d0
	add.b	mt_SongPos(a4),d0
	and.w	#$007f,d0
	move.l	mt_mod(a4),a0
	cmp.b	950(a0),d0		; end of song reached?
	blo	.1
	moveq	#0,d0			; restart the song from the beginning
	addq.b	#1,mt_SongEnd(a4)
	bne	.2
	clr.b	mt_Enable(a4)		; stop the song when mt_SongEnd was -1
.2:	and.b	#$7f,mt_SongEnd(a4)
.1:	move.b	d0,mt_SongPos(a4)

same_pattern:
	tst.b	mt_PosJumpFlag(a4)
	bne	song_step

	rts


;---------------------------------------------------------------------------
mt_checkfx:
; a2 = channel data
; a5 = audio registers

	; do channel effects between notes
.3:	move.w	n_funk(a2),d0
	beq	.4
	bsr	mt_updatefunk

.4:	move.w	#$0fff,d4
	and.w	n_cmd(a2),d4
	beq	mt_pernop
	and.w	#$00ff,d4

	moveq	#$0f,d0
	and.b	n_cmd(a2),d0
	add.w	d0,d0
	move.w	fx_tab(pc,d0.w),d0
	jmp	fx_tab(pc,d0.w)

fx_tab:
	dc.w	mt_arpeggio-fx_tab	; $0
	dc.w	mt_portaup-fx_tab
	dc.w	mt_portadown-fx_tab
	dc.w	mt_toneporta-fx_tab
	dc.w	mt_vibrato-fx_tab	; $4
	dc.w	mt_tonevolslide-fx_tab
	dc.w	mt_vibrvolslide-fx_tab
	dc.w	mt_tremolo-fx_tab
	dc.w	mt_nop-fx_tab		; $8
	dc.w	mt_nop-fx_tab
	dc.w	mt_volumeslide-fx_tab
	dc.w	mt_nop-fx_tab
	dc.w	mt_nop-fx_tab		; $C
	dc.w	mt_nop-fx_tab
	dc.w	mt_e_cmds-fx_tab
	dc.w	mt_nop-fx_tab


mt_pernop:
; just set the current period

	move.w	n_period(a2),AUDPER(a5)
mt_nop:
	rts


;---------------------------------------------------------------------------
mt_playvoice:
; a1 = pattern ptr
; a2 = channel data
; a3 = sample info table
; a5 = audio registers

	move.l	(a1)+,d6		; d6 current note/cmd words

.2:	tst.l	(a2)			; n_note/cmd: any note or cmd set?
	bne	.3
	move.w	n_period(a2),AUDPER(a5)
.3:	move.l	d6,(a2)

	moveq	#15,d5
	and.b	n_cmd(a2),d5
	add.w	d5,d5			; d5 cmd*2

	moveq	#0,d4
	move.b	d6,d4			; d4 cmd argument (in MSW)
	swap	d4
	move.w	#$0ff0,d4
	and.w	d6,d4			; d4 for checking E-cmd (in LSW)

	swap	d6
	move.l	d6,d0			; S...S...
	clr.b	d0
	rol.w	#4,d0
	rol.l	#4,d0			; ....00SS

	and.w	#$0fff,d6		; d6 note

	; get sample start address
	add.w	d0,d0			; sample number * 2
	beq	set_regs
	move.w	mult30tab(pc,d0.w),d1	; d1 sample info table offset
	lea	mt_SampleStarts(a4),a0
	add.w	d0,d0
	move.l	-4(a0,d0.w),d2

	; read length, volume and repeat from sample info table
	lea	(a3,d1.w),a0
	move.w	(a0)+,d0		; length
	bne	.4

	; use the first two bytes from the first sample for empty samples
	move.l	mt_SampleStarts(a4),d2
	addq.w	#1,d0

.4:	move.l	d2,n_start(a2)
	move.w	d0,n_reallength(a2)

	; determine period table from fine-tune parameter
	moveq	#0,d3
	move.b	(a0)+,d3
	add.w	d3,d3
	move.l	a0,d1
	lea	mt_PerFineTune(pc),a0
	add.w	(a0,d3.w),a0
	move.l	a0,n_pertab(a2)
	move.l	d1,a0
	cmp.w	#2*8,d3
	shs	n_minusft(a2)

	moveq	#0,d1
	move.b	(a0)+,d1		; volume
	move.w	d1,n_volume(a2)
	move.w	(a0)+,d3		; repeat offset
	beq	no_offset

	; set repeat
	add.l	d3,d2
	add.l	d3,d2
	move.w	(a0),d0
;	beq	idle_looping		; @@@ shouldn't happen, d0=n_length!?
	move.w	d0,n_replen(a2)
	exg	d0,d3			; n_replen to d3
	add.w	d3,d0
	bra	set_len_start

mult30tab:
	dc.w	0*30,1*30,2*30,3*30,4*30,5*30,6*30,7*30
	dc.w	8*30,9*30,10*30,11*30,12*30,13*30,14*30,15*30
	dc.w	16*30,17*30,18*30,19*30,20*30,21*30,22*30,23*30
	dc.w	24*30,25*30,26*30,27*30,28*30,29*30,30*30,31*30

no_offset:
	move.w	(a0),d3
	bne	set_replen
idle_looping:
	; repeat length zero means idle-looping
	moveq	#0,d2			; FIXME: expect two zero bytes at $0
	addq.w	#1,d3
set_replen:
	move.w	d3,n_replen(a2)
set_len_start:
	move.w	d0,n_length(a2)
	move.l	d2,n_loopstart(a2)
	move.l	d2,n_wavestart(a2)

        tst.b   n_disabled(a2)
        bne     .1
	move.w	d1,AUDVOL(a5)
.1:
	; remember if sample is looped
	; @@@ FIXME: also need to check if n_loopstart equals n_start
	subq.w	#1,d3
	sne	n_looped(a2)

set_regs:
; d4 = cmd argument | masked E-cmd
; d5 = cmd*2
; d6 = cmd.w | note.w

	move.w	d4,d3			; d3 masked E-cmd
	swap	d4			; d4 cmd argument into LSW

	tst.w	d6
	beq	checkmorefx		; no new note

	cmp.w	#$0e50,d3
	beq	set_finetune

	move.w	prefx_tab(pc,d5.w),d0
	jmp	prefx_tab(pc,d0.w)

prefx_tab:
	dc.w	set_period-prefx_tab,set_period-prefx_tab,set_period-prefx_tab
	dc.w	set_toneporta-prefx_tab			; $3
	dc.w	set_period-prefx_tab
	dc.w	set_toneporta-prefx_tab			; $5
	dc.w	set_period-prefx_tab,set_period-prefx_tab,set_period-prefx_tab
	dc.w	set_sampleoffset-prefx_tab		; $9
	dc.w	set_period-prefx_tab,set_period-prefx_tab,set_period-prefx_tab
	dc.w	set_period-prefx_tab,set_period-prefx_tab,set_period-prefx_tab

set_toneporta:
	move.l	n_pertab(a2),a0		; tuned period table

	; find first period which is less or equal the note in d6
	moveq	#36-1,d0
	moveq	#-2,d1
.1:	addq.w	#2,d1
	cmp.w	(a0)+,d6
	dbhs	d0,.1

	tst.b	n_minusft(a2)		; negative fine tune?
	beq	.2
	tst.w	d1
	beq	.2
	subq.l	#2,a0			; then take previous period
	subq.w	#2,d1

.2:	move.w	d1,n_noteoff(a2)	; note offset in period table
	move.w	n_period(a2),d2
	move.w	-(a0),d1
	cmp.w	d1,d2
	bne	.3
	moveq	#0,d1
.3:	move.w	d1,n_wantedperiod(a2)

	move.w	n_funk(a2),d0
	beq	.4
	bsr	mt_updatefunk

.4:	move.w	d2,AUDPER(a5)
	rts

set_sampleoffset:
; cmd 9 x y (xy = offset in 256 bytes)
; d4 = xy
	moveq	#0,d0
	move.b	d4,d0
	bne	.1
	move.b	n_sampleoffset(a2),d0
	bra	.2
.1:	move.b	d0,n_sampleoffset(a2)

.2:	lsl.w	#7,d0
	cmp.w	n_length(a2),d0
	bhs	.3
	sub.w	d0,n_length(a2)
	add.w	d0,d0
	add.l	d0,n_start(a2)
	bra	set_period

.3:	move.w	#1,n_length(a2)
	bra	set_period

set_finetune:
	lea	mt_PerFineTune(pc),a0
	moveq	#$0f,d0
	and.b	n_cmdlo(a2),d0
	add.w	d0,d0
	add.w	(a0,d0.w),a0
	move.l	a0,n_pertab(a2)
	cmp.w	#2*8,d0
	shs	n_minusft(a2)

set_period:
; find nearest period for a note value, then apply finetuning
; d3 = masked E-cmd
; d4 = cmd argument
; d5 = cmd*2
; d6 = note.w

	lea	mt_PeriodTable(pc),a0
	moveq	#36-1,d0
	moveq	#-2,d1
.1:	addq.w	#2,d1			; table offset
	cmp.w	(a0)+,d6
	dbhs	d0,.1

	; apply finetuning, set period and note-offset
	move.l	n_pertab(a2),a0
	move.w	(a0,d1.w),d2
	move.w	d2,n_period(a2)
	move.w	d1,n_noteoff(a2)

	; check for notedelay
	cmp.w	#$0ed0,d3		; notedelay
	beq	checkmorefx

	; disable DMA
	move.w	n_dmabit(a2),d0
	move.w	d0,DMACON(a6)

	btst	#2,n_vibratoctrl(a2)
	bne	.2
	move.b	d7,n_vibratopos(a2)

.2:	btst	#2,n_tremoloctrl(a2)
	bne	.3
	move.b	d7,n_tremolopos(a2)

.3:	move.l	n_start(a2),AUDLC(a5)
	move.w	n_length(a2),AUDLEN(a5)
	move.w	d2,AUDPER(a5)
	lea	mt_dmaon(pc),a0
	or.w	d0,(a0)

checkmorefx:
; d4 = cmd argument
; d5 = cmd*2
; d6 = note.w

	move.w	n_funk(a2),d0
	beq	.1
	bsr	mt_updatefunk

.1:	move.w	morefx_tab(pc,d5.w),d0
	jmp	morefx_tab(pc,d0.w)

morefx_tab:
	dc.w	mt_pernop-morefx_tab,mt_pernop-morefx_tab,mt_pernop-morefx_tab
	dc.w	mt_pernop-morefx_tab,mt_pernop-morefx_tab,mt_pernop-morefx_tab
	dc.w	mt_pernop-morefx_tab,mt_pernop-morefx_tab,mt_pernop-morefx_tab
	dc.w	mt_pernop-morefx_tab			; $9
	dc.w	mt_pernop-morefx_tab
	dc.w	mt_posjump-morefx_tab			; $B
	dc.w	mt_volchange-morefx_tab
	dc.w	mt_patternbrk-morefx_tab		; $D
	dc.w	mt_e_cmds-morefx_tab
	dc.w	mt_setspeed-morefx_tab


moreblockedfx:
; d6 = note.w | cmd.w

	moveq	#0,d4
	move.b	d6,d4			; cmd argument
	and.w	#$0f00,d6
	lsr.w	#7,d6
	move.w	blmorefx_tab(pc,d6.w),d0
	jmp	blmorefx_tab(pc,d0.w)

blmorefx_tab:
	dc.w	mt_nop-blmorefx_tab,mt_nop-blmorefx_tab
	dc.w	mt_nop-blmorefx_tab,mt_nop-blmorefx_tab
	dc.w	mt_nop-blmorefx_tab,mt_nop-blmorefx_tab
	dc.w	mt_nop-blmorefx_tab,mt_nop-blmorefx_tab
	dc.w	mt_nop-blmorefx_tab,mt_nop-blmorefx_tab
	dc.w	mt_nop-blmorefx_tab
	dc.w	mt_posjump-blmorefx_tab			; $B
	dc.w	mt_nop-blmorefx_tab
	dc.w	mt_patternbrk-blmorefx_tab		; $D
	dc.w	blocked_e_cmds-blmorefx_tab
	dc.w	mt_setspeed-blmorefx_tab		; $F


mt_arpeggio:
; cmd 0 x y (x = first arpeggio offset, y = second arpeggio offset)
; d4 = xy

	moveq	#0,d0
	move.b	mt_Counter(a4),d0
	move.b	arptab(pc,d0.w),d0
	beq	mt_pernop		; step 0, just use normal period
	bmi	.1

	; step 1, arpeggio by left nibble
	lsr.b	#4,d4
	bra	.2

	; step 2, arpeggio by right nibble
.1:	and.w	#$000f,d4

	; offset current note
.2:	add.w	d4,d4
	add.w	n_noteoff(a2),d4
	cmp.w	#2*36,d4
	bhs	.4

	; set period with arpeggio offset from note table
	move.l	n_pertab(a2),a0
	move.w	(a0,d4.w),AUDPER(a5)
.4:	rts

arptab:
	dc.b	0,1,-1,0,1,-1,0,1,-1,0,1,-1,0,1,-1,0
	dc.b	1,-1,0,1,-1,0,1,-1,0,1,-1,0,1,-1,0,1


mt_fineportaup:
; cmd E 1 x (subtract x from period)
; d0 = x

	tst.b	mt_Counter(a4)
	beq	do_porta_up
	rts


mt_portaup:
; cmd 1 x x (subtract xx from period)
; d4 = xx

	move.w	d4,d0

do_porta_up:
	move.w	n_period(a2),d1
	sub.w	d0,d1
	cmp.w	#113,d1
	bhs	.1
	moveq	#113,d1
.1:	move.w	d1,n_period(a2)
	move.w	d1,AUDPER(a5)
	rts


mt_fineportadn:
; cmd E 2 x (add x to period)
; d0 = x

	tst.b	mt_Counter(a4)
	beq	do_porta_down
	rts


mt_portadown:
; cmd 2 x x (add xx to period)
; d4 = xx

	move.w	d4,d0

do_porta_down:
	move.w	n_period(a2),d1
	add.w	d0,d1
	cmp.w	#856,d1
	bls	.1
	move.w	#856,d1
.1:	move.w	d1,n_period(a2)
	move.w	d1,AUDPER(a5)
	rts


mt_toneporta:
; cmd 3 x y (xy = tone portamento speed)
; d4 = xy

	tst.b	d4
	beq	mt_toneporta_nc
	move.w	d4,n_toneportspeed(a2)
	move.b	d7,n_cmdlo(a2)

mt_toneporta_nc:
	move.w	n_wantedperiod(a2),d1
	beq	.6

	move.w	n_toneportspeed(a2),d0
	move.w	n_period(a2),d2
	cmp.w	d1,d2
	blo	.2

	; tone porta up
	sub.w	d0,d2
	cmp.w	d1,d2
	bgt	.3
	move.w	d1,d2
	move.w	d7,n_wantedperiod(a2)
	bra	.3

	; tone porta down
.2:	add.w	d0,d2
	cmp.w	d1,d2
	blt	.3
	move.w	d1,d2
	move.w	d7,n_wantedperiod(a2)

.3:	move.w	d2,n_period(a2)

	tst.b	n_gliss(a2)
	beq	.5

	; glissando: find nearest note for new period
	move.l	n_pertab(a2),a0
	moveq	#36-1,d0
	moveq	#-2,d1
.4:	addq.w	#2,d1
	cmp.w	(a0)+,d2
	dbhs	d0,.4

	move.w	d1,n_noteoff(a2)	; @@@ needed?
	move.w	-(a0),d2

.5:	move.w	d2,AUDPER(a5)
.6	rts


mt_vibrato:
; cmd 4 x y (x = speed, y = amplitude)
; d4 = xy

	moveq	#$0f,d2
	and.b	d4,d2
	beq	.1
	move.b	d2,n_vibratoamp(a2)
	bra	.2
.1:	move.b	n_vibratoamp(a2),d2

.2:	lsr.b	#4,d4
	beq	.3
	move.b	d4,n_vibratospd(a2)
	bra	mt_vibrato_nc
.3:	move.b	n_vibratospd(a2),d4

mt_vibrato_nc:
	; calculate vibrato table offset: 64 * amplitude + (pos & 63)
	lsl.w	#6,d2
	moveq	#63,d0
	and.b	n_vibratopos(a2),d0
	add.w	d0,d2

	; select vibrato waveform
	moveq	#3,d1
	and.b	n_vibratoctrl(a2),d1
	beq	.6
	subq.b	#1,d1
	beq	.5

	; ctrl 2 & 3 select a rectangle vibrato
	lea	mt_VibratoRectTable(pc),a0
	bra	.9

	; ctrl 1 selects a sawtooth vibrato
.5:	lea	mt_VibratoSawTable(pc),a0
	bra	.9

	; ctrl 0 selects a sine vibrato
.6:	lea	mt_VibratoSineTable(pc),a0

	; add vibrato-offset to period
.9:	move.b	(a0,d2.w),d0
	ext.w	d0
	add.w	n_period(a2),d0
	move.w	d0,AUDPER(a5)

	; increase vibratopos by speed
	add.b	d4,n_vibratopos(a2)
	rts


mt_tonevolslide:
; cmd 5 x y (x = volume-up, y = volume-down)
; d4 = xy

	pea	mt_volumeslide(pc)
	bra	mt_toneporta_nc


mt_vibrvolslide:
; cmd 6 x y (x = volume-up, y = volume-down)
; d4 = xy

	move.w	d4,d3
	move.b	n_vibratoamp(a2),d2
	move.b	n_vibratospd(a2),d4
	bsr	mt_vibrato_nc

	move.w	d3,d4
	bra	mt_volumeslide


mt_tremolo:
; cmd 7 x y (x = speed, y = amplitude)
; d4 = xy

	moveq	#$0f,d2
	and.b	d4,d2
	beq	.1
	move.b	d2,n_tremoloamp(a2)
	bra	.2
.1:	move.b	n_tremoloamp(a2),d2

.2:	lsr.b	#4,d4
	beq	.3
	move.b	d4,n_tremolospd(a2)
	bra	.4
.3:	move.b	n_tremolospd(a2),d4

	; calculate tremolo table offset: 64 * amplitude + (pos & 63)
.4:	lsl.w	#6,d2
	moveq	#63,d0
	and.b	n_tremolopos(a2),d0
	add.w	d0,d2

	; select tremolo waveform
	moveq	#3,d1
	and.b	n_tremoloctrl(a2),d1
	beq	.6
	subq.b	#1,d1
	beq	.5

	; ctrl 2 & 3 select a rectangle tremolo
	lea	mt_VibratoRectTable(pc),a0
	bra	.9

	; ctrl 1 selects a sawtooth tremolo
.5:	lea	mt_VibratoSawTable(pc),a0
	bra	.9

	; ctrl 0 selects a sine tremolo
.6:	lea	mt_VibratoSineTable(pc),a0

	; add tremolo-offset to volume
.9:	move.b	(a0,d2.w),d0
	add.w	n_volume(a2),d0
	bpl	.10
	moveq	#0,d0
.10:	cmp.w	#64,d0
	bls	.11
	moveq	#64,d0
.11:	move.w	n_period(a2),AUDPER(a5)
        tst.b   n_disabled(a2)
        bne     .12
	move.w	d0,AUDVOL(a5)
.12:
	; increase tremolopos by speed
	add.b	d4,n_tremolopos(a2)
	rts


mt_volumeslide:
; cmd A x y (x = volume-up, y = volume-down)
; d4 = xy

	move.w	n_volume(a2),d0
	moveq	#$0f,d1
	and.b	d4,d1
	lsr.b	#4,d4
	beq	vol_slide_down

	; slide up, until 64
	add.b	d4,d0
vol_slide_up:
	cmp.b	#64,d0
	bls	set_vol
	moveq	#64,d0
	bra	set_vol

	; slide down, until 0
vol_slide_down:
	sub.b	d1,d0
	bpl	set_vol
	moveq	#0,d0

set_vol:
	move.w	d0,n_volume(a2)
	move.w	n_period(a2),AUDPER(a5)
        tst.b   n_disabled(a2)
        bne     .1
	move.w	d0,AUDVOL(a5)
.1:	rts


mt_posjump:
; cmd B x y (xy = new song position)
; d4 = xy

	move.b	d4,d0
	subq.b	#1,d0
	move.b	d0,mt_SongPos(a4)

jump_pos0:
	move.w	d7,mt_PBreakPos(a4)
	st	mt_PosJumpFlag(a4)
	rts


mt_volchange:
; cmd C x y (xy = new volume)
; d4 = xy

	cmp.w	#64,d4
	bls	.1
	moveq	#64,d4
.1:	move.w	d4,n_volume(a2)
        tst.b   n_disabled(a2)
        bne     .2
	move.w	d4,AUDVOL(a5)
.2:	rts


mt_patternbrk:
; cmd D x y (xy = break pos in decimal)
; d4 = xy

	moveq	#$0f,d0
	and.w	d4,d0
	move.w	d4,d1
	lsr.w	#4,d1
	add.b	mult10tab(pc,d1.w),d0
	cmp.b	#63,d0
	bhi	jump_pos0

	lsl.w	#4,d0
	move.w	d0,mt_PBreakPos(a4)
	st	mt_PosJumpFlag(a4)
	rts

mult10tab:
	dc.b	0,10,20,30,40,50,60,70,80,90,0,0,0,0,0,0


mt_setspeed:
; cmd F x y (xy<$20 new speed, xy>=$20 new tempo)
; d4 = xy

	cmp.b	#$20,d4
	bhs	.1
	move.b	d4,mt_Speed(a4)
	beq	_mt_end
	rts

	; set tempo (CIA only)
.1:	and.w	#$00ff,d4
	move.l	mt_timerval(a4),d0
	divu	d4,d0
	move.b	d0,CIAB+CIATALO
	lsr.w	#8,d0
	move.b	d0,CIAB+CIATAHI
	rts


mt_e_cmds:
; cmd E x y (x=command, y=argument)
; d4 = xy

	moveq	#$0f,d0
	and.w	d4,d0			; pass E-cmd argument in d0

	move.w	d4,d1
	lsr.w	#4,d1
	add.w	d1,d1
	move.w	ecmd_tab(pc,d1.w),d1
	jmp	ecmd_tab(pc,d1.w)

ecmd_tab:
	dc.w	mt_filter-ecmd_tab
	dc.w	mt_fineportaup-ecmd_tab
	dc.w	mt_fineportadn-ecmd_tab
	dc.w	mt_glissctrl-ecmd_tab
	dc.w	mt_vibratoctrl-ecmd_tab
	dc.w	mt_finetune-ecmd_tab
	dc.w	mt_jumploop-ecmd_tab
	dc.w	mt_tremoctrl-ecmd_tab
	dc.w	mt_e8-ecmd_tab
	dc.w	mt_retrignote-ecmd_tab
	dc.w	mt_volfineup-ecmd_tab
	dc.w	mt_volfinedn-ecmd_tab
	dc.w	mt_notecut-ecmd_tab
	dc.w	mt_notedelay-ecmd_tab
	dc.w	mt_patterndelay-ecmd_tab
	dc.w	mt_funk-ecmd_tab


blocked_e_cmds:
; cmd E x y (x=command, y=argument)
; d4 = xy

	moveq	#$0f,d0
	and.w	d4,d0			; pass E-cmd argument in d0

	move.w	d4,d1
	lsr.w	#4,d1
	add.w	d1,d1
	move.w	blecmd_tab(pc,d1.w),d1
	jmp	blecmd_tab(pc,d1.w)

blecmd_tab:
	dc.w	mt_filter-blecmd_tab
	dc.w	mt_rts-blecmd_tab
	dc.w	mt_rts-blecmd_tab
	dc.w	mt_glissctrl-blecmd_tab
	dc.w	mt_vibratoctrl-blecmd_tab
	dc.w	mt_finetune-blecmd_tab
	dc.w	mt_jumploop-blecmd_tab
	dc.w	mt_tremoctrl-blecmd_tab
	dc.w	mt_e8-blecmd_tab
	dc.w	mt_rts-blecmd_tab
	dc.w	mt_rts-blecmd_tab
	dc.w	mt_rts-blecmd_tab
	dc.w	mt_rts-blecmd_tab
	dc.w	mt_rts-blecmd_tab
	dc.w	mt_patterndelay-blecmd_tab
	dc.w	mt_rts-blecmd_tab


mt_filter:
; cmd E 0 x (x=1 disable, x=0 enable)
; d0 = x

mt_rts:
	rts


mt_glissctrl:
; cmd E 3 x (x gliss)
; d0 = x

	and.b	#$04,n_gliss(a2)
	or.b	d0,n_gliss(a2)
	rts


mt_vibratoctrl:
; cmd E 4 x (x = vibrato)
; d0 = x

	move.b	d0,n_vibratoctrl(a2)
	rts


mt_finetune:
; cmd E 5 x (x = finetune)
; d0 = x

	lea	mt_PerFineTune(pc),a0
	add.w	d0,d0
	add.w	(a0,d0.w),a0
	move.l	a0,n_pertab(a2)
	cmp.w	#2*8,d0
	shs	n_minusft(a2)
	rts


mt_jumploop:
; cmd E 6 x (x=0 loop start, else loop count)
; d0 = x

	tst.b	mt_Counter(a4)
	bne	.4

.1:	tst.b	d0
	beq	.3			; set start

	; otherwise we are at the end of the loop
	subq.b	#1,n_loopcount(a2)
	beq	.4			; loop finished
	bpl	.2

	; initialize loop counter
	move.b	d0,n_loopcount(a2)

	; jump back to start of loop
.2:	move.w	n_pattpos(a2),mt_PBreakPos(a4)
	st	mt_PBreakFlag(a4)
	rts

	; remember start of loop position
.3:	move.w	mt_PatternPos(a4),d0
	move.w	d0,n_pattpos(a2)
.4:	rts


mt_tremoctrl:
; cmd E 7 x (x = tremolo)
; d0 = x

	move.b	d0,n_tremoloctrl(a2)
	rts


mt_e8:
; cmd E 8 x (x = trigger value)
; d0 = x

	move.b	d0,mt_E8Trigger(a4)
	rts


mt_retrignote:
; cmd E 9 x (x = retrigger count)
; d0 = x

	tst.b	d0
	beq	.2

	; set new retrigger count when Counter=0
.1:	tst.b	mt_Counter(a4)
	bne	.3
	move.b	d0,n_retrigcount(a2)

	; avoid double retrigger, when Counter=0 and a note was set
	move.w	#$0fff,d2
	and.w	(a2),d2
	beq	do_retrigger
.2:	rts

	; check if retrigger count is reached
.3:	subq.b	#1,n_retrigcount(a2)
	bne	.2
	move.b	d0,n_retrigcount(a2)	; reset

do_retrigger:
	; DMA off, set sample pointer and length
	move.w	n_dmabit(a2),d0
	move.w	d0,DMACON(a6)
	move.l	n_start(a2),AUDLC(a5)
	move.w	n_length(a2),AUDLEN(a5)
	lea	mt_dmaon(pc),a0
	or.w	d0,(a0)
	rts


mt_volfineup:
; cmd E A x (x = volume add)
; d0 = x

	tst.b	mt_Counter(a4)
	beq	.1
	rts

.1:	add.w	n_volume(a2),d0
	bra	vol_slide_up


mt_volfinedn:
; cmd E B x (x = volume sub)
; d0 = x

	tst.b	mt_Counter(a4)
	beq	.1
	rts

.1:	move.b	d0,d1
	move.w	n_volume(a2),d0
	bra	vol_slide_down


mt_notecut:
; cmd E C x (x = counter to cut at)
; d0 = x

	cmp.b	mt_Counter(a4),d0
	bne	.1
	move.w	d7,n_volume(a2)
	move.w	d7,AUDVOL(a5)
.1:	rts


mt_notedelay:
; cmd E D x (x = counter to retrigger at)
; d0 = x

	cmp.b	mt_Counter(a4),d0
	bne	.1
	tst.w	(a2)			; trigger note when given
	bne	do_retrigger
.1:	rts


mt_patterndelay:
; cmd E E x (x = delay count)
; d0 = x

	tst.b	mt_Counter(a4)
	bne	.1
	tst.b	mt_PattDelTime2(a4)
	bne	.1
	addq.b	#1,d0
	move.b	d0,mt_PattDelTime(a4)
.1:	rts


mt_funk:
; cmd E F x (x = funk speed)
; d0 = x

	tst.b	mt_Counter(a4)
	bne	.1
	move.w	d0,n_funk(a2)
	bne	mt_updatefunk
.1:	rts

mt_updatefunk:
; d0 = funk speed

	move.b	mt_FunkTable(pc,d0.w),d0
	add.b	d0,n_funkoffset(a2)
	bpl	.2
	move.b	d7,n_funkoffset(a2)

	move.l	n_loopstart(a2),d0
	moveq	#0,d1
	move.w	n_replen(a2),d1
	add.l	d1,d1
	add.l	d0,d1
	move.l	n_wavestart(a2),a0
	addq.l	#1,a0
	cmp.l	d1,a0
	blo	.1
	move.l	d0,a0
.1:	move.l	a0,n_wavestart(a2)
	not.b	(a0)

.2:	rts


mt_FunkTable:
	dc.b	0,5,6,7,8,10,11,13,16,19,22,26,32,43,64,128

mt_VibratoSineTable:
	dc.b	0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0
	dc.b	0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0
	dc.b	0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0
	dc.b	0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0
	dc.b	0,0,0,0,0,0,1,1,1,1,1,1,1,1,1,1
	dc.b	1,1,1,1,1,1,1,1,1,1,1,0,0,0,0,0
	dc.b	0,0,0,0,0,0,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1
	dc.b	-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,0,0,0,0,0
	dc.b	0,0,0,1,1,1,2,2,2,3,3,3,3,3,3,3
	dc.b	3,3,3,3,3,3,3,3,2,2,2,1,1,1,0,0
	dc.b	0,0,0,-1,-1,-1,-2,-2,-2,-3,-3,-3,-3,-3,-3,-3
	dc.b	-3,-3,-3,-3,-3,-3,-3,-3,-2,-2,-2,-1,-1,-1,0,0
	dc.b	0,0,1,1,2,2,3,3,4,4,4,5,5,5,5,5
	dc.b	5,5,5,5,5,5,4,4,4,3,3,2,2,1,1,0
	dc.b	0,0,-1,-1,-2,-2,-3,-3,-4,-4,-4,-5,-5,-5,-5,-5
	dc.b	-5,-5,-5,-5,-5,-5,-4,-4,-4,-3,-3,-2,-2,-1,-1,0
	dc.b	0,0,1,2,3,3,4,5,5,6,6,7,7,7,7,7
	dc.b	7,7,7,7,7,7,6,6,5,5,4,3,3,2,1,0
	dc.b	0,0,-1,-2,-3,-3,-4,-5,-5,-6,-6,-7,-7,-7,-7,-7
	dc.b	-7,-7,-7,-7,-7,-7,-6,-6,-5,-5,-4,-3,-3,-2,-1,0
	dc.b	0,0,1,2,3,4,5,6,7,7,8,8,9,9,9,9
	dc.b	9,9,9,9,9,8,8,7,7,6,5,4,3,2,1,0
	dc.b	0,0,-1,-2,-3,-4,-5,-6,-7,-7,-8,-8,-9,-9,-9,-9
	dc.b	-9,-9,-9,-9,-9,-8,-8,-7,-7,-6,-5,-4,-3,-2,-1,0
	dc.b	0,1,2,3,4,5,6,7,8,9,9,10,11,11,11,11
	dc.b	11,11,11,11,11,10,9,9,8,7,6,5,4,3,2,1
	dc.b	0,-1,-2,-3,-4,-5,-6,-7,-8,-9,-9,-10,-11,-11,-11,-11
	dc.b	-11,-11,-11,-11,-11,-10,-9,-9,-8,-7,-6,-5,-4,-3,-2,-1
	dc.b	0,1,2,4,5,6,7,8,9,10,11,12,12,13,13,13
	dc.b	13,13,13,13,12,12,11,10,9,8,7,6,5,4,2,1
	dc.b	0,-1,-2,-4,-5,-6,-7,-8,-9,-10,-11,-12,-12,-13,-13,-13
	dc.b	-13,-13,-13,-13,-12,-12,-11,-10,-9,-8,-7,-6,-5,-4,-2,-1
	dc.b	0,1,3,4,6,7,8,10,11,12,13,14,14,15,15,15
	dc.b	15,15,15,15,14,14,13,12,11,10,8,7,6,4,3,1
	dc.b	0,-1,-3,-4,-6,-7,-8,-10,-11,-12,-13,-14,-14,-15,-15,-15
	dc.b	-15,-15,-15,-15,-14,-14,-13,-12,-11,-10,-8,-7,-6,-4,-3,-1
	dc.b	0,1,3,5,6,8,9,11,12,13,14,15,16,17,17,17
	dc.b	17,17,17,17,16,15,14,13,12,11,9,8,6,5,3,1
	dc.b	0,-1,-3,-5,-6,-8,-9,-11,-12,-13,-14,-15,-16,-17,-17,-17
	dc.b	-17,-17,-17,-17,-16,-15,-14,-13,-12,-11,-9,-8,-6,-5,-3,-1
	dc.b	0,1,3,5,7,9,11,12,14,15,16,17,18,19,19,19
	dc.b	19,19,19,19,18,17,16,15,14,12,11,9,7,5,3,1
	dc.b	0,-1,-3,-5,-7,-9,-11,-12,-14,-15,-16,-17,-18,-19,-19,-19
	dc.b	-19,-19,-19,-19,-18,-17,-16,-15,-14,-12,-11,-9,-7,-5,-3,-1
	dc.b	0,2,4,6,8,10,12,13,15,16,18,19,20,20,21,21
	dc.b	21,21,21,20,20,19,18,16,15,13,12,10,8,6,4,2
	dc.b	0,-2,-4,-6,-8,-10,-12,-13,-15,-16,-18,-19,-20,-20,-21,-21
	dc.b	-21,-21,-21,-20,-20,-19,-18,-16,-15,-13,-12,-10,-8,-6,-4,-2
	dc.b	0,2,4,6,9,11,13,15,16,18,19,21,22,22,23,23
	dc.b	23,23,23,22,22,21,19,18,16,15,13,11,9,6,4,2
	dc.b	0,-2,-4,-6,-9,-11,-13,-15,-16,-18,-19,-21,-22,-22,-23,-23
	dc.b	-23,-23,-23,-22,-22,-21,-19,-18,-16,-15,-13,-11,-9,-6,-4,-2
	dc.b	0,2,4,7,9,12,14,16,18,20,21,22,23,24,25,25
	dc.b	25,25,25,24,23,22,21,20,18,16,14,12,9,7,4,2
	dc.b	0,-2,-4,-7,-9,-12,-14,-16,-18,-20,-21,-22,-23,-24,-25,-25
	dc.b	-25,-25,-25,-24,-23,-22,-21,-20,-18,-16,-14,-12,-9,-7,-4,-2
	dc.b	0,2,5,8,10,13,15,17,19,21,23,24,25,26,27,27
	dc.b	27,27,27,26,25,24,23,21,19,17,15,13,10,8,5,2
	dc.b	0,-2,-5,-8,-10,-13,-15,-17,-19,-21,-23,-24,-25,-26,-27,-27
	dc.b	-27,-27,-27,-26,-25,-24,-23,-21,-19,-17,-15,-13,-10,-8,-5,-2
	dc.b	0,2,5,8,11,14,16,18,21,23,24,26,27,28,29,29
	dc.b	29,29,29,28,27,26,24,23,21,18,16,14,11,8,5,2
	dc.b	0,-2,-5,-8,-11,-14,-16,-18,-21,-23,-24,-26,-27,-28,-29,-29
	dc.b	-29,-29,-29,-28,-27,-26,-24,-23,-21,-18,-16,-14,-11,-8,-5,-2

mt_VibratoSawTable:
	dc.b	0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0
	dc.b	0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0
	dc.b	0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0
	dc.b	0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0
	dc.b	0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0
	dc.b	1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1
	dc.b	-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1
	dc.b	0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0
	dc.b	0,0,0,0,0,0,0,0,1,1,1,1,1,1,1,1
	dc.b	2,2,2,2,2,2,2,2,3,3,3,3,3,3,3,3
	dc.b	-3,-3,-3,-3,-3,-3,-3,-3,-2,-2,-2,-2,-2,-2,-2,-2
	dc.b	-1,-1,-1,-1,-1,-1,-1,-1,0,0,0,0,0,0,0,0
	dc.b	0,0,0,0,0,0,1,1,1,1,1,2,2,2,2,2
	dc.b	3,3,3,3,3,3,4,4,4,4,4,5,5,5,5,5
	dc.b	-5,-5,-5,-5,-5,-5,-4,-4,-4,-4,-4,-3,-3,-3,-3,-3
	dc.b	-2,-2,-2,-2,-2,-2,-1,-1,-1,-1,-1,0,0,0,0,0
	dc.b	0,0,0,0,1,1,1,1,2,2,2,2,3,3,3,3
	dc.b	4,4,4,4,5,5,5,5,6,6,6,6,7,7,7,7
	dc.b	-7,-7,-7,-7,-6,-6,-6,-6,-5,-5,-5,-5,-4,-4,-4,-4
	dc.b	-3,-3,-3,-3,-2,-2,-2,-2,-1,-1,-1,-1,0,0,0,0
	dc.b	0,0,0,0,1,1,1,2,2,2,3,3,3,4,4,4
	dc.b	5,5,5,5,6,6,6,7,7,7,8,8,8,9,9,9
	dc.b	-9,-9,-9,-9,-8,-8,-8,-7,-7,-7,-6,-6,-6,-5,-5,-5
	dc.b	-4,-4,-4,-4,-3,-3,-3,-2,-2,-2,-1,-1,-1,0,0,0
	dc.b	0,0,0,1,1,1,2,2,3,3,3,4,4,4,5,5
	dc.b	6,6,6,7,7,7,8,8,9,9,9,10,10,10,11,11
	dc.b	-11,-11,-11,-10,-10,-10,-9,-9,-8,-8,-8,-7,-7,-7,-6,-6
	dc.b	-5,-5,-5,-4,-4,-4,-3,-3,-2,-2,-2,-1,-1,-1,0,0
	dc.b	0,0,0,1,1,2,2,3,3,3,4,4,5,5,6,6
	dc.b	7,7,7,8,8,9,9,10,10,10,11,11,12,12,13,13
	dc.b	-13,-13,-13,-12,-12,-11,-11,-10,-10,-10,-9,-9,-8,-8,-7,-7
	dc.b	-6,-6,-6,-5,-5,-4,-4,-3,-3,-3,-2,-2,-1,-1,0,0
	dc.b	0,0,1,1,2,2,3,3,4,4,5,5,6,6,7,7
	dc.b	8,8,9,9,10,10,11,11,12,12,13,13,14,14,15,15
	dc.b	-15,-15,-14,-14,-13,-13,-12,-12,-11,-11,-10,-10,-9,-9,-8,-8
	dc.b	-7,-7,-6,-6,-5,-5,-4,-4,-3,-3,-2,-2,-1,-1,0,0
	dc.b	0,0,1,1,2,2,3,3,4,5,5,6,6,7,7,8
	dc.b	9,9,10,10,11,11,12,12,13,14,14,15,15,16,16,17
	dc.b	-17,-17,-16,-16,-15,-15,-14,-13,-13,-12,-12,-11,-11,-10,-10,-9
	dc.b	-8,-8,-7,-7,-6,-6,-5,-4,-4,-3,-3,-2,-2,-1,-1,0
	dc.b	0,0,1,1,2,3,3,4,5,5,6,6,7,8,8,9
	dc.b	10,10,11,11,12,13,13,14,15,15,16,16,17,18,18,19
	dc.b	-19,-19,-18,-18,-17,-16,-16,-15,-14,-14,-13,-13,-12,-11,-11,-10
	dc.b	-9,-9,-8,-8,-7,-6,-6,-5,-4,-4,-3,-3,-2,-1,-1,0
	dc.b	0,0,1,2,2,3,4,4,5,6,6,7,8,8,9,10
	dc.b	11,11,12,13,13,14,15,15,16,17,17,18,19,19,20,21
	dc.b	-21,-21,-20,-19,-19,-18,-17,-17,-16,-15,-15,-14,-13,-12,-12,-11
	dc.b	-10,-10,-9,-8,-8,-7,-6,-6,-5,-4,-4,-3,-2,-1,-1,0
	dc.b	0,0,1,2,3,3,4,5,6,6,7,8,9,9,10,11
	dc.b	12,12,13,14,15,15,16,17,18,18,19,20,21,21,22,23
	dc.b	-23,-23,-22,-21,-20,-20,-19,-18,-17,-17,-16,-15,-14,-14,-13,-12
	dc.b	-11,-11,-10,-9,-8,-8,-7,-6,-5,-5,-4,-3,-2,-2,-1,0
	dc.b	0,0,1,2,3,4,4,5,6,7,8,8,9,10,11,12
	dc.b	13,13,14,15,16,17,17,18,19,20,21,21,22,23,24,25
	dc.b	-25,-25,-24,-23,-22,-21,-21,-20,-19,-18,-17,-16,-16,-15,-14,-13
	dc.b	-12,-12,-11,-10,-9,-8,-8,-7,-6,-5,-4,-3,-3,-2,-1,0
	dc.b	0,0,1,2,3,4,5,6,7,7,8,9,10,11,12,13
	dc.b	14,14,15,16,17,18,19,20,21,21,22,23,24,25,26,27
	dc.b	-27,-27,-26,-25,-24,-23,-22,-21,-20,-20,-19,-18,-17,-16,-15,-14
	dc.b	-13,-13,-12,-11,-10,-9,-8,-7,-6,-6,-5,-4,-3,-2,-1,0
	dc.b	0,0,1,2,3,4,5,6,7,8,9,10,11,12,13,14
	dc.b	15,15,16,17,18,19,20,21,22,23,24,25,26,27,28,29
	dc.b	-29,-28,-28,-27,-26,-25,-24,-23,-22,-21,-20,-19,-18,-17,-16,-15
	dc.b	-14,-13,-13,-12,-11,-10,-9,-8,-7,-6,-5,-4,-3,-2,-1,0

mt_VibratoRectTable:
	dc.b	0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0
	dc.b	0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0
	dc.b	0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0
	dc.b	0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0
	dc.b	1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1
	dc.b	1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1
	dc.b	-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1
	dc.b	-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1
	dc.b	3,3,3,3,3,3,3,3,3,3,3,3,3,3,3,3
	dc.b	3,3,3,3,3,3,3,3,3,3,3,3,3,3,3,3
	dc.b	-3,-3,-3,-3,-3,-3,-3,-3,-3,-3,-3,-3,-3,-3,-3,-3
	dc.b	-3,-3,-3,-3,-3,-3,-3,-3,-3,-3,-3,-3,-3,-3,-3,-3
	dc.b	5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5
	dc.b	5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5
	dc.b	-5,-5,-5,-5,-5,-5,-5,-5,-5,-5,-5,-5,-5,-5,-5,-5
	dc.b	-5,-5,-5,-5,-5,-5,-5,-5,-5,-5,-5,-5,-5,-5,-5,-5
	dc.b	7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7
	dc.b	7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7
	dc.b	-7,-7,-7,-7,-7,-7,-7,-7,-7,-7,-7,-7,-7,-7,-7,-7
	dc.b	-7,-7,-7,-7,-7,-7,-7,-7,-7,-7,-7,-7,-7,-7,-7,-7
	dc.b	9,9,9,9,9,9,9,9,9,9,9,9,9,9,9,9
	dc.b	9,9,9,9,9,9,9,9,9,9,9,9,9,9,9,9
	dc.b	-9,-9,-9,-9,-9,-9,-9,-9,-9,-9,-9,-9,-9,-9,-9,-9
	dc.b	-9,-9,-9,-9,-9,-9,-9,-9,-9,-9,-9,-9,-9,-9,-9,-9
	dc.b	11,11,11,11,11,11,11,11,11,11,11,11,11,11,11,11
	dc.b	11,11,11,11,11,11,11,11,11,11,11,11,11,11,11,11
	dc.b	-11,-11,-11,-11,-11,-11,-11,-11,-11,-11,-11,-11,-11,-11,-11,-11
	dc.b	-11,-11,-11,-11,-11,-11,-11,-11,-11,-11,-11,-11,-11,-11,-11,-11
	dc.b	13,13,13,13,13,13,13,13,13,13,13,13,13,13,13,13
	dc.b	13,13,13,13,13,13,13,13,13,13,13,13,13,13,13,13
	dc.b	-13,-13,-13,-13,-13,-13,-13,-13,-13,-13,-13,-13,-13,-13,-13,-13
	dc.b	-13,-13,-13,-13,-13,-13,-13,-13,-13,-13,-13,-13,-13,-13,-13,-13
	dc.b	15,15,15,15,15,15,15,15,15,15,15,15,15,15,15,15
	dc.b	15,15,15,15,15,15,15,15,15,15,15,15,15,15,15,15
	dc.b	-15,-15,-15,-15,-15,-15,-15,-15,-15,-15,-15,-15,-15,-15,-15,-15
	dc.b	-15,-15,-15,-15,-15,-15,-15,-15,-15,-15,-15,-15,-15,-15,-15,-15
	dc.b	17,17,17,17,17,17,17,17,17,17,17,17,17,17,17,17
	dc.b	17,17,17,17,17,17,17,17,17,17,17,17,17,17,17,17
	dc.b	-17,-17,-17,-17,-17,-17,-17,-17,-17,-17,-17,-17,-17,-17,-17,-17
	dc.b	-17,-17,-17,-17,-17,-17,-17,-17,-17,-17,-17,-17,-17,-17,-17,-17
	dc.b	19,19,19,19,19,19,19,19,19,19,19,19,19,19,19,19
	dc.b	19,19,19,19,19,19,19,19,19,19,19,19,19,19,19,19
	dc.b	-19,-19,-19,-19,-19,-19,-19,-19,-19,-19,-19,-19,-19,-19,-19,-19
	dc.b	-19,-19,-19,-19,-19,-19,-19,-19,-19,-19,-19,-19,-19,-19,-19,-19
	dc.b	21,21,21,21,21,21,21,21,21,21,21,21,21,21,21,21
	dc.b	21,21,21,21,21,21,21,21,21,21,21,21,21,21,21,21
	dc.b	-21,-21,-21,-21,-21,-21,-21,-21,-21,-21,-21,-21,-21,-21,-21,-21
	dc.b	-21,-21,-21,-21,-21,-21,-21,-21,-21,-21,-21,-21,-21,-21,-21,-21
	dc.b	23,23,23,23,23,23,23,23,23,23,23,23,23,23,23,23
	dc.b	23,23,23,23,23,23,23,23,23,23,23,23,23,23,23,23
	dc.b	-23,-23,-23,-23,-23,-23,-23,-23,-23,-23,-23,-23,-23,-23,-23,-23
	dc.b	-23,-23,-23,-23,-23,-23,-23,-23,-23,-23,-23,-23,-23,-23,-23,-23
	dc.b	25,25,25,25,25,25,25,25,25,25,25,25,25,25,25,25
	dc.b	25,25,25,25,25,25,25,25,25,25,25,25,25,25,25,25
	dc.b	-25,-25,-25,-25,-25,-25,-25,-25,-25,-25,-25,-25,-25,-25,-25,-25
	dc.b	-25,-25,-25,-25,-25,-25,-25,-25,-25,-25,-25,-25,-25,-25,-25,-25
	dc.b	27,27,27,27,27,27,27,27,27,27,27,27,27,27,27,27
	dc.b	27,27,27,27,27,27,27,27,27,27,27,27,27,27,27,27
	dc.b	-27,-27,-27,-27,-27,-27,-27,-27,-27,-27,-27,-27,-27,-27,-27,-27
	dc.b	-27,-27,-27,-27,-27,-27,-27,-27,-27,-27,-27,-27,-27,-27,-27,-27
	dc.b	29,29,29,29,29,29,29,29,29,29,29,29,29,29,29,29
	dc.b	29,29,29,29,29,29,29,29,29,29,29,29,29,29,29,29
	dc.b	-29,-29,-29,-29,-29,-29,-29,-29,-29,-29,-29,-29,-29,-29,-29,-29
	dc.b	-29,-29,-29,-29,-29,-29,-29,-29,-29,-29,-29,-29,-29,-29,-29,-29


mt_PerFineTune:
	dc.w	mt_Tuning0-mt_PerFineTune,mt_Tuning1-mt_PerFineTune
	dc.w	mt_Tuning2-mt_PerFineTune,mt_Tuning3-mt_PerFineTune
	dc.w	mt_Tuning4-mt_PerFineTune,mt_Tuning5-mt_PerFineTune
	dc.w	mt_Tuning6-mt_PerFineTune,mt_Tuning7-mt_PerFineTune
	dc.w	mt_TuningM8-mt_PerFineTune,mt_TuningM7-mt_PerFineTune
	dc.w	mt_TuningM6-mt_PerFineTune,mt_TuningM5-mt_PerFineTune
	dc.w	mt_TuningM4-mt_PerFineTune,mt_TuningM3-mt_PerFineTune
	dc.w	mt_TuningM2-mt_PerFineTune,mt_TuningM1-mt_PerFineTune

mt_PeriodTable:
mt_Tuning0:	; Tuning 0, Normal c-1 - b3
	dc.w	856,808,762,720,678,640,604,570,538,508,480,453
	dc.w	428,404,381,360,339,320,302,285,269,254,240,226
	dc.w	214,202,190,180,170,160,151,143,135,127,120,113
mt_Tuning1:
	dc.w	850,802,757,715,674,637,601,567,535,505,477,450
	dc.w	425,401,379,357,337,318,300,284,268,253,239,225
	dc.w	213,201,189,179,169,159,150,142,134,126,119,113
mt_Tuning2:
	dc.w	844,796,752,709,670,632,597,563,532,502,474,447
	dc.w	422,398,376,355,335,316,298,282,266,251,237,224
	dc.w	211,199,188,177,167,158,149,141,133,125,118,112
mt_Tuning3:
	dc.w	838,791,746,704,665,628,592,559,528,498,470,444
	dc.w	419,395,373,352,332,314,296,280,264,249,235,222
	dc.w	209,198,187,176,166,157,148,140,132,125,118,111
mt_Tuning4:
	dc.w	832,785,741,699,660,623,588,555,524,495,467,441
	dc.w	416,392,370,350,330,312,294,278,262,247,233,220
	dc.w	208,196,185,175,165,156,147,139,131,124,117,110
mt_Tuning5:
	dc.w	826,779,736,694,655,619,584,551,520,491,463,437
	dc.w	413,390,368,347,328,309,292,276,260,245,232,219
	dc.w	206,195,184,174,164,155,146,138,130,123,116,109
mt_Tuning6:
	dc.w	820,774,730,689,651,614,580,547,516,487,460,434
	dc.w	410,387,365,345,325,307,290,274,258,244,230,217
	dc.w	205,193,183,172,163,154,145,137,129,122,115,109
mt_Tuning7:
	dc.w	814,768,725,684,646,610,575,543,513,484,457,431
	dc.w	407,384,363,342,323,305,288,272,256,242,228,216
	dc.w	204,192,181,171,161,152,144,136,128,121,114,108
mt_TuningM8:
	dc.w	907,856,808,762,720,678,640,604,570,538,508,480
	dc.w	453,428,404,381,360,339,320,302,285,269,254,240
	dc.w	226,214,202,190,180,170,160,151,143,135,127,120
mt_TuningM7:
	dc.w	900,850,802,757,715,675,636,601,567,535,505,477
	dc.w	450,425,401,379,357,337,318,300,284,268,253,238
	dc.w	225,212,200,189,179,169,159,150,142,134,126,119
mt_TuningM6:
	dc.w	894,844,796,752,709,670,632,597,563,532,502,474
	dc.w	447,422,398,376,355,335,316,298,282,266,251,237
	dc.w	223,211,199,188,177,167,158,149,141,133,125,118
mt_TuningM5:
	dc.w	887,838,791,746,704,665,628,592,559,528,498,470
	dc.w	444,419,395,373,352,332,314,296,280,264,249,235
	dc.w	222,209,198,187,176,166,157,148,140,132,125,118
mt_TuningM4:
	dc.w	881,832,785,741,699,660,623,588,555,524,494,467
	dc.w	441,416,392,370,350,330,312,294,278,262,247,233
	dc.w	220,208,196,185,175,165,156,147,139,131,123,117
mt_TuningM3:
	dc.w	875,826,779,736,694,655,619,584,551,520,491,463
	dc.w	437,413,390,368,347,328,309,292,276,260,245,232
	dc.w	219,206,195,184,174,164,155,146,138,130,123,116
mt_TuningM2:
	dc.w	868,820,774,730,689,651,614,580,547,516,487,460
	dc.w	434,410,387,365,345,325,307,290,274,258,244,230
	dc.w	217,205,193,183,172,163,154,145,137,129,122,115
mt_TuningM1:
	dc.w	862,814,768,725,684,646,610,575,543,513,484,457
	dc.w	431,407,384,363,342,323,305,288,272,256,242,228
	dc.w	216,203,192,181,171,161,152,144,136,128,121,114

	ifd	SDATA

	section	__MERGED,bss

mt_chan1:
	ds.b	n_sizeof
mt_chan2:
	ds.b	n_sizeof
mt_chan3:
	ds.b	n_sizeof
mt_chan4:
	ds.b	n_sizeof

mt_SampleStarts:
	ds.l	31

mt_mod:
	ds.l	1
mt_oldLev6:
	ds.l	1
mt_timerval:
	ds.l	1
mt_Lev6Ena:
	ds.w	1
mt_PatternPos:
	ds.w	1
mt_PBreakPos:
	ds.w	1
mt_PosJumpFlag:
	ds.b	1
mt_PBreakFlag:
	ds.b	1
mt_Speed:
	ds.b	1
mt_Counter:
	ds.b	1
mt_SongPos:
	ds.b	1
mt_PattDelTime:
	ds.b	1
mt_PattDelTime2:
	ds.b	1
mt_SilCntValid:
	ds.b	1
mt_TimBRpt:
	ds.b	1

	xdef	_mt_Enable
_mt_Enable:
mt_Enable:
	ds.b	1

	xdef	_mt_E8Trigger
_mt_E8Trigger:
mt_E8Trigger:
	ds.b	1

	xdef	_mt_SongEnd
_mt_SongEnd:
mt_SongEnd:
	ds.b	1


	else	; !SDATA : single section with local base register

	rsreset
mt_chan1	rs.b	n_sizeof
mt_chan2	rs.b	n_sizeof
mt_chan3	rs.b	n_sizeof
mt_chan4	rs.b	n_sizeof
mt_SampleStarts	rs.l	31
mt_mod		rs.l	1
mt_oldLev6	rs.l	1
mt_timerval	rs.l	1
mt_Lev6Ena	rs.w	1
mt_PatternPos	rs.w	1
mt_PBreakPos	rs.w	1
mt_PosJumpFlag	rs.b	1
mt_PBreakFlag	rs.b	1
mt_Speed	rs.b	1
mt_Counter	rs.b	1
mt_SongPos	rs.b	1
mt_PattDelTime	rs.b	1
mt_PattDelTime2	rs.b	1
mt_SilCntValid	rs.b	1
mt_TimBRpt	rs.b	1
mt_Enable	rs.b	1		; exported as _mt_Enable
mt_E8Trigger	rs.b	1		; exported as _mt_E8Trigger
mt_SongEnd	rs.b	1		; exported as _mt_SongEnd

mt_data:
	ds.b	mt_Enable
	xdef	_mt_Enable
_mt_Enable:
	ds.b	1
	xdef	_mt_E8Trigger
_mt_E8Trigger:
	ds.b	1
	xdef	_mt_SongEnd
_mt_SongEnd:
	ds.b	1

	endc	; SDATA/!SDATA

	end

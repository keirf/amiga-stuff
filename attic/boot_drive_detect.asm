_LVOOpenDevice	= -444
_LVOCloseDevice = -450

	INCDIR	SOURCES:INCLUDE/
	INCLUDE	exec/io.i
	INCLUDE	devices/trackdisk.i


BOOT	dc.l	"DOS"<<8
	dc.l	0
	dc.l	"STR!"


; find drive which was used to boot the disk and determine
; which drives are available
	bsr.b	CheckDrives

; d3.w: bit mask for available drives
; d4.w: boot drive
; d5.w: number of available drives

.wait	bra.b	.wait


CheckDrives
	move.l	a1,a5

	moveq	#0,d3		; bit mask for available drives
	moveq	#-1,d4		; boot drive: default: unknown
	moveq	#0,d5		; number of available drives
	moveq	#4-1,d6		; device number and loop counter
.loop	bsr.b	OpenTrackdisk
	tst.l	d0
	bne.b	.noDevice

	addq.w	#1,d5		; drive available
	bset	d6,d3

	lea	IOStdReq(pc),a1
	move.l	IO_UNIT(a1),d0
	cmp.l	IO_UNIT(a5),d0
	bne.b	.noBootDrive
	move.w	d6,d4

.noBootDrive
	jsr	_LVOCloseDevice(a6)


.noDevice
	dbf	d6,.loop
	rts





OpenTrackdisk
	lea	TrackDiskName(pc),a0
	lea	IOStdReq(pc),a1

	pea	port(pc)
	move.l	(a7)+,ReplyPort-IOStdReq(a1)

	pea	ReplyPort(pc)
	move.l	(a7)+,MN_REPLYPORT(a1)

	move.l	d6,d0
	moveq	#0,d1			; flags
	;move.l	$4.W,a6
	jmp	_LVOOpenDevice(a6)



IOStdReq	ds.b	IOTD_SIZE
ReplyPort	dc.l	0

port		ds.b	100

TrackDiskName	dc.b	"trackdisk.device",0

	ds.b	1024

OBJS += ../base/bootblock.o
OBJS += ../base/util.o
OBJS += disk.o
OBJS += systest.o

include ../base/Rules.mk

.PHONY: all
all: systest.adf

systest.adf: systest.bin
	dd if=/dev/zero of=$@ bs=512 count=1760
	adfwrite $@ $< 0 43 -r -c

clean::
	$(RM) *.adf

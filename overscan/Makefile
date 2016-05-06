OBJS += ../base/bootblock.o
OBJS += ../base/util.o
OBJS += overscan.o

include ../base/Rules.mk

.PHONY: all
all: overscan.adf

overscan.adf: overscan.bin
	dd if=/dev/zero of=$@ bs=512 count=1760
	adfwrite $@ $< 0 21 -r -c

clean::
	$(RM) *.adf ../base/*.o

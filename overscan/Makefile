OBJS += bootblock.o
OBJS += overscan.o

include Rules.mk

.PHONY: all
all: overscan.adf

overscan.adf: overscan.bin
	dd if=/dev/zero of=$@ bs=512 count=1760
	adfbb $@ -r$<

clean::
	$(RM) *.adf

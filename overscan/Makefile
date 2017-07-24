OBJS += entry.o
OBJS += util.o
OBJS += overscan.o

include ../base/Rules.mk

.PHONY: all
all: overscan.adf

overscan.adf: bootblock.bin overscan.bin
	python ../base/mk_adf.py $^ $@

clean::
	$(RM) *.adf

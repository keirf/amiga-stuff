OBJS += ../base/entry.o
OBJS += ../base/util.o
OBJS += overscan.o

include ../base/Rules.mk

.PHONY: all
all: overscan.adf

overscan.adf: ../base/bootblock.bin overscan.bin
	python ../base/mk_adf.py $^ $@

clean::
	$(RM) *.adf ../base/*.o ../base/*.bin

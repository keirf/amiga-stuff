OBJS += entry.o
OBJS += util.o
OBJS += overscan.o

include ../base/Rules.mk

all: overscan.adf

overscan.adf: bootblock.bin overscan.bin
	$(GZIP) overscan.bin
	../inflate/degzip -H overscan.bin.gz overscan.tmp
	xdftool $@ format "Overscan"
	$(PYTHON) ../base/mk_adf.py bootblock.bin overscan.tmp $@
	$(RM) overscan.tmp

clean::
	$(RM) *.adf *.exe *.zip $(NAME) *.tmp *.gz

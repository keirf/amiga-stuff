.PHONY: all clean

SUBDIR := gnu_c

all:
	$(MAKE) -C $(SUBDIR)
	$(MAKE) nzs_crack.adf

nzs_crack.adf: nzs.adf $(SUBDIR)/nzs.bin $(SUBDIR)/nzs_bootblock.bin
	cp nzs.adf $@
	adfbb $@ -r$(SUBDIR)/nzs_bootblock.bin
	adfwrite $@ $(SUBDIR)/nzs.bin 2 10 -r -f -e46414d45

clean:
	$(MAKE) -C $(SUBDIR) clean
	$(RM) nzs_crack.adf

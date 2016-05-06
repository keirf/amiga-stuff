OBJS += ../base/util.o
OBJS += disk.o
OBJS += systest.o

include ../base/Rules.mk

.PHONY: all
all: systest.zip

systest.zip: systest.adf systest.exe SysTest.info
	cp systest.exe SysTest
	chmod 0755 SysTest
	$(RM) $@
	zip -q $@ $(subst systest.exe,SysTest,$^)
	$(RM) SysTest

systest_exe.elf: OBJS := ../base/dos_entry.o $(OBJS)
systest_exe.elf: ../base/dos_entry.o

systest_adf.elf: OBJS := ../base/bootblock.o $(OBJS)
systest_adf.elf: ../base/bootblock.o

systest.exe: systest_exe.bin
	python ../base/mk_amiga_exe.py $< $@

systest.adf: systest_adf.bin
	dd if=/dev/zero of=$@ bs=512 count=1760 status=none
	adfwrite $@ $< 0 43 -r -c

clean::
	$(RM) *.adf *.exe *.zip ../base/*.o SysTest

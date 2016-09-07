OBJS += ../base/cancellation.o
OBJS += ../base/util.o
OBJS += build.o
OBJS += crash.o
OBJS += audio.o
OBJS += cia.o
OBJS += floppy.o
OBJS += joymouse.o
OBJS += keyboard.o
OBJS += memory.o
OBJS += mfm.o
OBJS += serpar.o
OBJS += systest.o
OBJS += video.o

include ../base/Rules.mk

# build.o must be refreshed on every build
.PHONY: build.c

.PHONY: all
all: systest.zip

systest.zip: systest.adf systest.exe SysTest.info
	cp systest.exe SysTest
	chmod 0755 SysTest
	$(RM) $@
	zip -q $@ $(subst systest.exe,SysTest,$^)
	$(RM) SysTest

systest_exe.elf: OBJS := dos_entry.o $(OBJS)
systest_exe.elf: dos_entry.o

systest_adf.elf: OBJS := bootblock.o $(OBJS)
systest_adf.elf: bootblock.o

systest.exe: systest_exe.bin
	python ../base/mk_amiga_exe.py $< $@

systest.adf: systest_adf.bin
	dd if=/dev/zero of=$@ bs=512 count=1760 status=none
	adfwrite $@ $< 0 1759 -r -c

clean::
	$(RM) *.adf *.exe *.zip ../base/*.o SysTest

OBJS += entry.o
OBJS += cancellation.o
OBJS += util.o
OBJS += build.o
OBJS += crash.o
OBJS += audio.o
OBJS += battclock.o
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

entry.o: AFLAGS += -DDETECT_MEMORY

systest.zip: systest.adf systest.exe SysTest.info README.md
	cp systest.exe SysTest
	chmod 0755 SysTest
	mkdir -p debug ; cp systest.elf debug
	$(RM) $@
	zip -q $@ $(subst systest.exe,SysTest,$^) debug/*
	$(RM) -r SysTest debug

systest.exe: systest.bin
	python ../base/mk_amiga_exe.py $< $@

systest.adf: bootblock.bin systest.bin
	python ../base/mk_adf.py $^ $@

clean::
	$(RM) *.adf *.exe *.zip SysTest

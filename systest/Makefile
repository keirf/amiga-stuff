VER := 1.1

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
all: systest-v$(VER).zip

build.o: CFLAGS += -DVER="\"$(VER)\""

entry.o: AFLAGS += -DDETECT_MEMORY

systest-v$(VER).zip: systest.adf SysTest SysTest.info README.md systest.elf
	$(RM) -r systest-v$(VER)
	mkdir -p systest-v$(VER)/debug
	cp $^ systest-v$(VER)
	mv systest-v$(VER)/systest.elf systest-v$(VER)/debug
	$(RM) $@
	zip -r $@ systest-v$(VER)

SysTest: systest.exe
	cp systest.exe SysTest
	chmod 0755 SysTest

systest.exe: systest.bin
	python ../base/mk_amiga_exe.py $< $@

systest.adf: bootblock.bin systest.bin
	python ../base/mk_adf.py $^ $@

clean::
	$(RM) *.adf *.exe *.zip SysTest
	$(RM) -r systest-v*

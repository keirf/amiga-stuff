NAME := AmigaTestKit
VER := $(shell git rev-parse --short HEAD)

# Year to set when RTC is reset (F1 in battery clock test)
RTC_RESET_YEAR ?= $(shell date +%Y)

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
OBJS += kickmem.o
OBJS += memory.o
OBJS += mfm.o
OBJS += serpar.o
OBJS += testkit.o
OBJS += video.o
OBJS += ptplayer/ptplayer.o

include ../base/Rules.mk

# build.o must be refreshed on every build
.PHONY: build.c

all: $(NAME)-$(VER).zip

build.o: CFLAGS += -DVER="\"$(VER)\""
battclock.o: CFLAGS += -DRTC_RESET_YEAR=$(RTC_RESET_YEAR)
entry.o: AFLAGS += -DDETECT_MEMORY

$(NAME)-$(VER).zip: $(NAME).adf $(NAME) $(NAME).info README.md $(NAME).elf
	$(RM) -r $(NAME)-$(VER)
	mkdir -p $(NAME)-$(VER)/debug
	cp $^ RELEASE_NOTES $(NAME)-$(VER)
	mv $(NAME)-$(VER)/$(NAME).elf $(NAME)-$(VER)/debug
	$(RM) $@
	zip -r $@ $(NAME)-$(VER)

$(NAME): $(NAME).exe
	cp $< $@
	chmod 0755 $@

$(NAME).exe: $(NAME).bin
	$(PYTHON) ../base/mk_amiga_exe.py $< $@
	$(PYTHON) ../inflate/pack_exe.py $@ $@.tmp
	mv $@.tmp $@

$(NAME).adf: bootblock.bin $(NAME).bin $(NAME) $(NAME).info
	$(GZIP) $(NAME).bin
	../inflate/degzip -H $(NAME).bin.gz $(NAME).bin.tmp
	xdftool $@ format "AmigaTestKit $(VER)"
	$(PYTHON) ../base/mk_adf.py bootblock.bin $(NAME).bin.tmp $@
	$(RM) $(NAME).bin.tmp
	xdftool $@ write $(NAME) + write $(NAME).info

clean::
	$(RM) *.adf *.exe *.zip $(NAME) *.tmp *.gz
	$(RM) -r $(NAME)-* ptplayer/*.o

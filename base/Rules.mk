# GNU Make cross-dev build rules
# My own cross-dev setup is binutils-2.24 + gcc-4.9.2
# Also tested with binutils-2.26 + gcc-5.3.0
# Target is m68k-unknown-elf

TOOL_PREFIX = m68k-unknown-elf-
CC = $(TOOL_PREFIX)gcc
OBJCOPY = $(TOOL_PREFIX)objcopy

ifneq ($(VERBOSE),1)
TOOL_PREFIX := @$(TOOL_PREFIX)
endif

FLAGS  = -Os -nostdlib -std=gnu99 -iquote ../base/inc
FLAGS += -Wall -Werror -Wno-format -Wdeclaration-after-statement
FLAGS += -Wstrict-prototypes -Wredundant-decls -Wnested-externs
FLAGS += -fno-common -fno-exceptions -fno-strict-aliasing -fomit-frame-pointer
FLAGS += -fno-delete-null-pointer-checks -m68000 -msoft-float

FLAGS += -MMD -MF .$(@F).d
DEPS = .*.d

CFLAGS += $(FLAGS) -include decls.h
AFLAGS += $(FLAGS) -D__ASSEMBLY__ -Wa,--register-prefix-optional
AFLAGS += -Wa,-l -Wa,--bitwise-or -Wa,--base-size-default-16
AFLAGS += -Wa,--disp-size-default-16 -Wa,--pcrel
LDFLAGS += $(FLAGS) -Wl,--gc-sections -T../base/amiga.ld

.DEFAULT_GOAL := all

.PHONY: clean

.SECONDARY:

%.o: %.c Makefile
	@echo CC $@
	$(CC) $(CFLAGS) -c $< -o $@

%.o: %.S Makefile
	@echo AS $@
	$(CC) $(AFLAGS) -c $< -o $@

%.elf: $(OBJS) ../base/amiga.ld Makefile
	@echo LD $@
	$(CC) $(LDFLAGS) $(OBJS) -o $@
	@chmod a-x $@

%.bin: %.elf
	@echo OBJCOPY $@
	$(OBJCOPY) -O binary $< $@
	@chmod a-x $@

clean::
	$(RM) *~ *.o *.elf *.bin $(DEPS)

-include $(DEPS)

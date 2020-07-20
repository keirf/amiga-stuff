# GNU Make cross-dev build rules
# Tested cross-dev setups (gcc/binutils) (*=recommended):
#  4.9.2/2.24, 5.3.0/2.26, 7.1.0/2.28, 9.3.0/2.34*
# Target is m68k-unknown-elf

TOOL_PREFIX = m68k-unknown-elf-
CC = $(TOOL_PREFIX)gcc
OBJCOPY = $(TOOL_PREFIX)objcopy
PYTHON = python3
GZIP = zopfli
#GZIP = gzip -fk9

ifneq ($(VERBOSE),1)
TOOL_PREFIX := @$(TOOL_PREFIX)
endif

# -Ofast produces code approx 50% larger than -Os.
# The relative speed of -Ofast vs -Os has not been benchmarked.
OPT_FLAGS = -Os
#OPT_FLAGS = -Ofast

FLAGS  = $(OPT_FLAGS) -nostdlib -std=gnu99 -iquote ../base/inc -fno-builtin
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

HOSTCC = gcc
HOSTCFLAGS = -O2 -Wall -Werror
HOSTCFLAGS += -MMD -MF .$(@F).d

.DEFAULT_GOAL := all

.PHONY: all clean

.SECONDARY:

%.o: %.c Makefile
	@echo CC $@
	$(CC) $(CFLAGS) -c $< -o $@

%.o: ../base/%.c Makefile
	@echo CC $@
	$(CC) $(CFLAGS) -c $< -o $@

%.o: %.S Makefile
	@echo AS $@
	$(CC) $(AFLAGS) -c $< -o $@

%.o: ../base/%.S Makefile
	@echo AS $@
	$(CC) $(AFLAGS) -c $< -o $@

%.o: %.asm
	@echo VASM $@
	vasmm68k_mot -Felf $< -o $@

%.elf: $(OBJS) ../base/amiga.ld Makefile
	@echo LD $@
	$(CC) $(LDFLAGS) $(OBJS) -o $@
	@chmod a-x $@

%.bin: %.elf
	@echo OBJCOPY $@
	$(OBJCOPY) -O binary $< $@
	@chmod a-x $@

bootblock.bin: bootblock.o
	$(OBJCOPY) -O binary $< $@
	chmod a-x $@

clean::
	$(RM) *~ *.o *.elf *.bin $(DEPS)

-include $(DEPS)

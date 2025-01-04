# Default to m68k-elf- as TOOL_PREFIX
TOOL_PREFIX ?= m68k-elf-

# Autodetect TOOL_PREFIX
ifeq ($(shell which $(TOOL_PREFIX)gcc 2>/dev/null),)
    # Try m68k-unknown-elf-
    TOOL_PREFIX := m68k-unknown-elf-
    ifeq ($(shell which $(TOOL_PREFIX)gcc 2>/dev/null),)
        $(error Neither m68k-elf-gcc nor m68k-unknown-elf-gcc found in PATH)
    endif
endif

CC = $(TOOL_PREFIX)gcc
OBJCOPY = $(TOOL_PREFIX)objcopy

ifneq ($(VERBOSE),1)
TOOL_PREFIX := @$(TOOL_PREFIX)
endif

FLAGS  = -Os -nostdlib -std=gnu99
FLAGS += -Wall -Werror -Wno-format -Wdeclaration-after-statement
FLAGS += -Wstrict-prototypes -Wredundant-decls -Wnested-externs
FLAGS += -fno-common -fno-exceptions -fno-strict-aliasing -fomit-frame-pointer
FLAGS += -m68000 -msoft-float

FLAGS += -MMD -MF .$(@F).d
DEPS = .*.d

CFLAGS += $(FLAGS)
AFLAGS += $(FLAGS) -D__ASSEMBLY__ -Wa,--register-prefix-optional
AFLAGS += -Wa,-l -Wa,--bitwise-or -Wa,--base-size-default-16
AFLAGS += -Wa,--disp-size-default-16 -Wa,--pcrel
LDFLAGS += $(FLAGS) -Wl,--gc-sections -Tamiga.ld

.DEFAULT_GOAL := all

.PHONY: clean

.SECONDARY:

%.o: %.c Makefile
	@echo CC $@
	$(CC) $(CFLAGS) -c $< -o $@

%.o: %.S Makefile
	@echo AS $@
	$(CC) $(AFLAGS) -c $< -o $@

%.elf: $(OBJS) amiga.ld Makefile
	@echo LD $@
	$(CC) $(LDFLAGS) $(OBJS) -o $@
	@chmod a-x $@

%.elf: %.o amiga.ld Makefile
	@echo LD $@
	$(CC) $(LDFLAGS) $< -o $@
	@chmod a-x $@

%.bin: %.o
	@echo OBJCOPY $@
	$(OBJCOPY) -O binary $< $@
	@chmod a-x $@

%.bin: %.elf
	@echo OBJCOPY $@
	$(OBJCOPY) -O binary $< $@
	@chmod a-x $@

clean:
	$(RM) *~ *.o *.elf *.bin $(DEPS)

-include $(DEPS)

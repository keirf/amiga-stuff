PREFIX = m68k-amigaos-
CC = $(PREFIX)gcc
AS = $(PREFIX)as
PYTHON = python3

CFLAGS += -MMD -MF .$(@F).d
DEPS = .*.d

CFLAGS += -nostdlib -fno-builtin -I..
CFLAGS += -Os -m68000 -Wall -Werror -std=gnu99 -msoft-float
#CFLAGS += -msmall-code
#CFLAGS += -DDEBUG
LDFLAGS += $(CFLAGS) -s
AFLAGS += $(CFLAGS) -D__ASSEMBLY__ -Wa,--register-prefix-optional
AFLAGS += -Wa,-l -Wa,--bitwise-or -Wa,--base-size-default-16
AFLAGS += -Wa,--disp-size-default-16 -Wa,--pcrel

all: depacker degzip

# Test harness for inflate.S
test_inflate: degzip test_inflate.o
	$(CC) -o $@ $@.o $(LDFLAGS)
	$(PYTHON) test_inflate.py *.[cSo] *.asm ../cracking/protections/* ../cracking/cracks/nzs/gnu_c/* ../testkit/*

%.o: %.S
	$(CC) -o $@ -c $< $(AFLAGS)

%.o: %.c
	$(CC) -o $@ -c $< $(CFLAGS)

depacker: depacker_entry depacker_main depacker_packed

depacker_entry: depacker_entry.o
	$(CC) -o $@ $@.o $(LDFLAGS)

depacker_main: depacker_main.o
	$(CC) -o $@ $@.o $(LDFLAGS)

depacker_packed: depacker_packed.o
	$(CC) -o $@ $@.o $(LDFLAGS)

degzip: degzip_gnu.c
	gcc -O2 -Wall -o $@ $^

.PHONY: test_inflate depacker

clean:
	$(RM) degzip *.o *~ test_inflate _test_* _pack*
	$(RM) depacker_main depacker_entry depacker_packed

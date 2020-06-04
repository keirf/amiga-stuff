include ../base/Rules.mk

TARGETS := hunk_loader kickconv

all: $(TARGETS)

%: %.c
	$(HOSTCC) $(HOSTCFLAGS) $< -o $@

clean::
	$(RM) $(TARGETS)

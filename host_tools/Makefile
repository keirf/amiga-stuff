include ../base/Rules.mk

TARGETS := hunk_loader kickconv

.PHONY: all
all: $(TARGETS)

%: %.c
	$(HOSTCC) $(HOSTCFLAGS) $< -o $@

clean::
	$(RM) $(TARGETS)

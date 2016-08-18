include base/Rules.mk

SUBDIRS := base overscan systest

.PHONY: all
all:

kickconv: kickconv.c
	$(HOSTCC) $(HOSTCFLAGS) $< -o $@

clean:: $(addsuffix clean,$(SUBDIRS))
%clean: %
	$(MAKE) -C $< clean

clean::
	$(RM) degzip kickconv

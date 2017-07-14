include base/Rules.mk

SUBDIRS := base overscan systest

.PHONY: all
all:

hunk_loader: hunk_loader.c
	$(HOSTCC) $(HOSTCFLAGS) $< -o $@

kickconv: kickconv.c
	$(HOSTCC) $(HOSTCFLAGS) $< -o $@

clean:: $(addsuffix clean,$(SUBDIRS))
%clean: %
	$(MAKE) -C $< clean

clean::
	$(RM) degzip kickconv hunk_loader

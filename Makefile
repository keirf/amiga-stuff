include base/Rules.mk

SUBDIRS := base overscan systest

.PHONY: all
all:

clean:: $(addsuffix clean,$(SUBDIRS))
%clean: %
	$(MAKE) -C $< clean

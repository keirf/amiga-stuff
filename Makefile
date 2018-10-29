include base/Rules.mk

SUBDIRS := base overscan systest inflate

.PHONY: all $(SUBDIRS)
all:

host_tools overscan systest:
	$(MAKE) -C $@ all

clean:: $(addsuffix clean,$(SUBDIRS))
%clean: %
	$(MAKE) -C $< clean

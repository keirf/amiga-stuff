include base/Rules.mk

SUBDIRS := base host_tools overscan testkit inflate

.PHONY: all $(SUBDIRS)
all: $(SUBDIRS)

host_tools overscan testkit:
	$(MAKE) -C $@ all

clean::
	@set -e; for subdir in $(SUBDIRS); do \
		$(MAKE) -C $$subdir clean; \
	done

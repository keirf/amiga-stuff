include base/Rules.mk


SUBDIRS := base host_tools overscan systest inflate

.PHONY: all $(SUBDIRS)
all: $(SUBDIRS)

host_tools overscan systest:
	$(MAKE) -C $@ all

clean::
	@set -e; for subdir in $(SUBDIRS); do \
		$(MAKE) -C $$subdir clean; \
	done

include base/Rules.mk

SUBDIRS := base inflate host_tools overscan testkit inflate

.PHONY: $(SUBDIRS)

all: $(SUBDIRS)

overscan testkit: inflate

$(SUBDIRS):
	$(MAKE) -C $@ all

clean::
	@set -e; for subdir in $(SUBDIRS); do \
		$(MAKE) -C $$subdir clean; \
	done

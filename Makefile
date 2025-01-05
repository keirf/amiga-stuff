include base/Rules.mk

SUBDIRS := base inflate host_tools overscan testkit inflate
SUBDIRS += cracking/cracks/nzs/gnu cracking/cracks/nzs/gnu_c

.PHONY: $(SUBDIRS)

all: $(SUBDIRS)

overscan testkit: inflate

$(SUBDIRS):
	$(MAKE) -C $@ all

clean::
	@set -e; for subdir in $(SUBDIRS); do \
		$(MAKE) -C $$subdir clean; \
	done

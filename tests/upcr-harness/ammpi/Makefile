PERL ?= perl
AMX_FLAVOR=mpi
CONDUIT_DIR=$(TOP_BUILDDIR)/gasnet/$(AMX_FLAVOR)-conduit
AMX_DIR=$(TOP_BUILDDIR)/gasnet/other/am$(AMX_FLAVOR)
MANUAL_CFLAGS=

-include $(CONDUIT_DIR)/$(AMX_FLAVOR)-seq.mak

amxdist: force
PASSTHRU_PHONY=check check-exports tests amxdist

$(PASSTHRU_PHONY): force
	$(MAKE) -C $(AMX_DIR) $@
	@echo '#!/bin/sh' > $@ ; chmod +x $@

distcheck: force
	rm -Rf am$(AMX_FLAVOR)[0-9].[0-9]*
	$(MAKE) -C $(AMX_DIR) amxdist
	@set -x ; \
	 TESTDIR=`pwd` ; \
	 cd $(AMX_DIR) && \
	 VERSION=`/bin/ls -t am$(AMX_FLAVOR)*.tar.gz | $(PERL) -ne 'if (m@am$(AMX_FLAVOR)(.+)\.tar.gz@) { print "$$1"; exit; }'` ; \
	 DIR="am$(AMX_FLAVOR)$$VERSION" ; \
	 gunzip -c $$DIR.tar.gz | tar xvf - && \
	 cd $$DIR && \
	 $(MAKE) all tests CC="$(GASNET_LD)" && \
	 cp ./testam$(EXESUFFIX) $$TESTDIR/$@$(EXESUFFIX)

test%: force
	$(MAKE) -C $(AMX_DIR) $@
	@cp $(AMX_DIR)/$@$(EXESUFFIX) $@$(EXESUFFIX)

force:

.PHONY: force


LEGION_CLONE_ARGS ?= --depth=1 --branch=stable
LEGION_CLONE_URL  ?= https://github.com/StanfordLegion/legion.git
LEGION_CLONE_DIR  ?= legion-clone
LEGION_GASNET     ?= #empty
LEGION_TEST_ENV   ?= GASNET_QUIET=1  # for the default udp-conduit
LEGION_LAUNCHER   ?= $$GASNET/bin/amudprun -np $(UPCTHREADS)  # assumes static threads

# Re: $(LEGION_CLONE_DIR) the default is to clone into $(TOP_BUILDDIR), which
# is often a local f/s even when the harness workdir is not.

legion: force
	env
	rm -rf legion-built gasnet $(LEGION_CLONE_DIR)
	git clone -q $(LEGION_CLONE_ARGS) $(LEGION_CLONE_URL) $(LEGION_CLONE_DIR)
	git --git-dir=$(LEGION_CLONE_DIR)/.git describe --always
	$(PERL) -pi -e s,mpi_interop,realm_saxpy,g -- $(LEGION_CLONE_DIR)/test.py # replace bogus test
	if test -f "$(LEGION_GASNET)/include/gasnet.h"; then \
	  ln -s `cd "$(LEGION_GASNET)" && pwd` gasnet; \
	else \
	  $(MAKE) install -C $(TOP_BUILDDIR)/gasnet prefix="`pwd`/gasnet"; \
	fi
	@echo '#!/bin/sh' > $@ ; chmod +x $@
	@touch legion-built

TESTS_PY = legion_cxx # regent fuzzer realm external private perf
$(TESTS_PY): legion-built
	GASNET="`pwd`/gasnet" && \
	cd $(LEGION_CLONE_DIR) && \
	env USE_GASNET=1 GASNET="$$GASNET" CONDUIT=$(NETWORK) LAUNCHER="$(LEGION_LAUNCHER)" \
	    LG_RT_DIR="`pwd`/runtime" $(LEGION_TEST_ENV) \
	    ./test.py --use gasnet --test $@ 2>&1

# Build and run w/ given thread count
legion_cxx_st%:
	$(MAKE) legion_cxx
	@echo '#!/bin/sh' > $@ ; chmod +x $@

force:

.PHONY: force $(TESTS_PY)

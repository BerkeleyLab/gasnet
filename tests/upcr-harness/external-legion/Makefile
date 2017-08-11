LEGION_GIT_REPO   ?= StanfordLegion/legion
LEGION_GIT_COMMIT ?= stable
LEGION_BLDDIR     ?= $(TOP_BUILDDIR)/legion
LEGION_TMPDIR     ?= $(TOP_BUILDDIR)/legion-tmp
LEGION_GASNET     ?= # Path to an existing installation, if any
WGET              ?= wget -nv

# Symlink or simple script that corresponds to
#    RunCmd = ./launcher -np %N %P %A
launcher:
	case $(NETWORK) in \
	smp) echo '#!/bin/sh' > $@ ; \
	     echo 'N=$$2;shift 2;eval env GASNET_PSHM_NODES=$$N "$$@"' >> $@ ; \
	     chmod +x $@;; \
	udp) ln -s "$(LEGION_BLDDIR)/gasnet/bin/amudprun" $@;; \
	  *) ln -s "$(LEGION_BLDDIR)/gasnet/bin/gasnetrun_$(NETWORK)" $@;; \
	esac
	
legion: force
	env
	rm -Rf legion-built launcher $(LEGION_TMPDIR) $(LEGION_BLDDIR)
	mkdir $(LEGION_TMPDIR) && cd $(LEGION_TMPDIR) && \
	  $(WGET) https://github.com/$(LEGION_GIT_REPO)/archive/$(LEGION_GIT_COMMIT).zip 2>&1 && \
	  unzip -z $(LEGION_GIT_COMMIT).zip && \
	  unzip -q $(LEGION_GIT_COMMIT).zip && \
	  rm $(LEGION_GIT_COMMIT).zip && \
	  mv * $(LEGION_BLDDIR) # archive's root dir will vary
	rm -Rf $(LEGION_TMPDIR)
	$(PERL) -pi -e s,mpi_interop,realm_saxpy,g -- $(LEGION_BLDDIR)/test.py # replace bogus test
	if test -f "$(LEGION_GASNET)/include/gasnet.h"; then \
	  ln -s `cd "$(LEGION_GASNET)" && pwd` $(LEGION_BLDDIR)/gasnet; \
	else \
	  $(MAKE) install -C $(TOP_BUILDDIR)/gasnet prefix="$(LEGION_BLDDIR)/gasnet"; \
	fi
	ln -s "$(LEGION_BLDDIR)/gasnet/bin" .
	@echo '#!/bin/sh' > $@ ; chmod +x $@
	$(MAKE) launcher
	@touch legion-built

# Runs of test.py:
TESTS_PY = legion_cxx regent fuzzer realm external private perf
$(TESTS_PY): legion-built
	cd $(LEGION_BLDDIR) && \
	env USE_GASNET=1 GASNET="$(LEGION_BLDDIR)/gasnet" CONDUIT=$(NETWORK) \
	    LAUNCHER="echo Would run: " \
	    LG_RT_DIR="$(LEGION_BLDDIR)/runtime" $(LEGION_TEST_ENV) \
	    ./test.py --use gasnet --test $@ 2>&1
	@echo '#!/bin/sh' > $@ ; chmod +x $@

# Examples:
EXAMPLES = circuit dynamic_registration ghost ghost_pull realm_saxpy spmd_cgsolver virtual_map 
$(EXAMPLES): legion-built
	env USE_GASNET=1 GASNET="$(LEGION_BLDDIR)/gasnet" CONDUIT=$(NETWORK) \
	    LG_RT_DIR="$(LEGION_BLDDIR)/runtime" $(LEGION_TEST_ENV) \
	    $(MAKE) -C $(LEGION_BLDDIR)/examples/$@
	mv $(LEGION_BLDDIR)/examples/$@/$@ .

force:

.PHONY: force $(TESTS_PY)

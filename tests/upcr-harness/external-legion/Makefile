LEGION_GIT_REPO   ?= StanfordLegion/legion
LEGION_GIT_COMMIT ?= stable
LEGION_BLDDIR     ?= $(TOP_BUILDDIR)/legion
LEGION_TMPDIR     ?= $(TOP_BUILDDIR)/legion-tmp
LEGION_GASNET     ?= # Path to an existing installation, if any

HARNESS_LOGDIR    ?= .
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
	fi > $(HARNESS_LOGDIR)/legion.log 2>&1
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


# Build a single test
one_test: legion-built
	env USE_GASNET=1 GASNET="$(LEGION_BLDDIR)/gasnet" CONDUIT=$(NETWORK) \
	    LG_RT_DIR="$(LEGION_BLDDIR)/runtime" $(LEGION_TEST_ENV) \
	    $(MAKE) -C $(LEGION_BLDDIR)/$(TEST_DIR)  >> $(HARNESS_LOGDIR)/legion.log 2>&1
	mv $(LEGION_BLDDIR)/$(TEST_DIR)/$(TEST_EXE) .

# examples/:
EXAMPLES = circuit dynamic_registration ghost ghost_pull realm_saxpy spmd_cgsolver virtual_map 
$(EXAMPLES): ; $(MAKE) one_test TEST_DIR=examples/$@ TEST_EXE=$@

# tutorial/:
TUTORIALS = hello_world tasks_and_futures index_tasks global_vars logical_regions \
            physical_regions privileges partitioning multiple_partitions custom_mapper 
$(TUTORIALS): ; $(MAKE) one_test TEST_DIR=tutorial/??_$@ TEST_EXE=$@

# test/:
TESTS = attach_file_mini
$(TESTS): ; $(MAKE) one_test TEST_DIR=test/$@ TEST_EXE=$@

force:

.PHONY: force one_test $(TESTS_PY)

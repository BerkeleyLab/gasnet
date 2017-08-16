LEGION_GIT_REPO   ?= StanfordLegion/legion
LEGION_GIT_COMMIT ?= stable
LEGION_BLDDIR     ?= $(TOP_BUILDDIR)/legion
LEGION_TMPDIR     ?= $(TOP_BUILDDIR)/legion-tmp
LEGION_GASNET     ?= # Path to an existing installation, if any

HARNESS_LOGDIR    ?= .
WGET              ?= wget -nv
ZIP_COMMENT       ?= unzip -z
ZIP_EXTRACT       ?= unzip -q

LOGFILE = $(HARNESS_LOGDIR)/legion.log
TO_LOG = >> $(LOGFILE) 2>&1

RUNTIME_TARGETS = librealm.a liblegion.a
SEED_DIR = $(LEGION_BLDDIR)/examples/circuit # NOTE: must not be an OMP or CUDA test
COMMON_ENV = \
	LG_RT_DIR="$(LEGION_BLDDIR)/runtime" USE_GASNET=1 \
	GASNET="$(LEGION_BLDDIR)/gasnet" CONDUIT=$(NETWORK)
DO_MAKE = env $(COMMON_ENV) $(LEGION_TEST_ENV) $(MAKE)

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
	rm -Rf legion-built launcher $(LEGION_TMPDIR) $(LEGION_BLDDIR) $(LOGFILE)
	mkdir $(LEGION_TMPDIR) && cd $(LEGION_TMPDIR) && \
	  $(WGET) https://github.com/$(LEGION_GIT_REPO)/archive/$(LEGION_GIT_COMMIT).zip 2>&1 && \
	  $(ZIP_COMMENT) $(LEGION_GIT_COMMIT).zip && \
	  $(ZIP_EXTRACT) $(LEGION_GIT_COMMIT).zip && \
	  rm $(LEGION_GIT_COMMIT).zip && \
	  mv * $(LEGION_BLDDIR) # archive's root dir will vary
	rm -Rf $(LEGION_TMPDIR)
	$(PERL) -pi -e s,mpi_interop,realm_saxpy,g -- $(LEGION_BLDDIR)/test.py # replace bogus test
	if test -f "$(LEGION_GASNET)/include/gasnet.h"; then \
	  echo ======== GASNet symlinked ========; \
	  ln -s `cd "$(LEGION_GASNET)" && pwd` $(LEGION_BLDDIR)/gasnet; \
	else \
	  echo ======== GASNet install ========; \
	  $(MAKE) install -C $(TOP_BUILDDIR)/gasnet prefix="$(LEGION_BLDDIR)/gasnet"; \
	fi $(TO_LOG)
	( echo ======== Prebuild Legion RT libs in $(SEED_DIR) ========; \
	  $(DO_MAKE) -C $(SEED_DIR) $(RUNTIME_TARGETS) \
	) $(TO_LOG)
	@echo '#!/bin/sh' > $@ ; chmod +x $@
	$(MAKE) launcher
	@touch legion-built

# Runs of test.py:
TESTS_PY = legion_cxx regent fuzzer realm external private perf
$(TESTS_PY): legion-built
	cd $(LEGION_BLDDIR) && \
	env $(COMMON_ENV) LAUNCHER="echo Would run: " $(LEGION_TEST_ENV) \
	    ./test.py --use gasnet --test $@ 2>&1
	@echo '#!/bin/sh' > $@ ; chmod +x $@


# Build a single test
one_test: legion-built
	( echo ======== Rebuild Legion RT libs in $(TEST_DIR) ========; \
	  $(DO_MAKE) -C $(LEGION_BLDDIR)/$(TEST_DIR) $(RUNTIME_TARGETS) \
	) $(TO_LOG)
	$(DO_MAKE) -C $(LEGION_BLDDIR)/$(TEST_DIR)
	mv $(LEGION_BLDDIR)/$(TEST_DIR)/$(TEST_EXE) .

# examples/:
EXAMPLES = circuit dynamic_registration ghost ghost_pull realm_saxpy spmd_cgsolver virtual_map 
$(EXAMPLES): force; $(MAKE) one_test TEST_DIR=examples/$@ TEST_EXE=$@

# tutorial/:
TUTORIALS = hello_world tasks_and_futures index_tasks global_vars logical_regions \
            physical_regions privileges partitioning multiple_partitions custom_mapper 
$(TUTORIALS): force; $(MAKE) one_test TEST_DIR=tutorial/??_$@ TEST_EXE=$@

# test/:
TESTS = attach_file_mini
$(TESTS): force; $(MAKE) one_test TEST_DIR=test/$@ TEST_EXE=$@

force:

.PHONY: force one_test $(TESTS_PY)

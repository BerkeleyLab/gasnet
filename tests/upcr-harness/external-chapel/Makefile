CHAPEL_GIT_REPO   ?= chapel-lang/chapel
CHAPEL_GIT_COMMIT ?= master
CHAPEL_MAKE_ARGS  ?= -j4
CHAPEL_BLDDIR     ?= $(TOP_BUILDDIR)/chapel
CHAPEL_TMPDIR     ?= $(TOP_BUILDDIR)/chapel-tmp
CHAPEL_CONDUIT    ?= $(NETWORK) # Default to same conduit as the enclosing harness run

HARNESS_LOGDIR    ?= .

# Paths and options for standard tools
WGET              ?= wget -nv
GZCAT             ?= gzip -cd
TAR               ?= tar
UNZIP             ?= unzip
P7ZIP             ?= 7za

# Must not inherit these from harness environment:
unexport LD LDFLAGS LIBS
unexport CC CFLAGS
unexport CXX CXXFLAGS
unexport AR RANLIB

# Parameterized support for download/unpack
CHAPEL_URL      ?= https://github.com/$(CHAPEL_GIT_REPO)/archive/$(CHAPEL_GIT_COMMIT)$(chapel_dl_suffix)
CHAPEL_ARCHIVE  ?= $(notdir $(CHAPEL_URL))
CHAPEL_DOWNLOAD ?= unzip # one of "unzip", "p7zip" or "tgz"
chapel-download: force
	rm -Rf $(CHAPEL_BLDDIR) $(CHAPEL_TMPDIR) && mkdir $(CHAPEL_TMPDIR)
	cd $(CHAPEL_TMPDIR) && $(WGET) $(CHAPEL_URL) 2>&1
	$(MAKE) -C $(CHAPEL_TMPDIR) -f `pwd`/Makefile chapel-unpack-$(CHAPEL_DOWNLOAD)
	rm $(CHAPEL_TMPDIR)/$(CHAPEL_ARCHIVE)
	mv $(CHAPEL_TMPDIR)/* $(CHAPEL_BLDDIR) # archive's root dir will vary
	rmdir $(CHAPEL_TMPDIR)
# Three ways to unpack the archive:
#  Option 1: "unzip" - .zip w/ unzip
#   This is the favored approach because it gives us the hash and uses a widely available utility.
chapel-unpack-unzip: force; $(UNZIP) -z $(CHAPEL_ARCHIVE) && $(UNZIP) -q $(CHAPEL_ARCHIVE)
chapel_dl_suffix_unzip = .zip
#  Option 2: "p7zip" - .zip w/ 7za (from p7zip package)
#   This also gives us the hash, but uses a less widely available utility.
#   However, it is sometimes necessary because many unzip installations contain a bug w.r.t. symlinks
chapel-unpack-p7zip: force; $(P7ZIP) x -bd $(CHAPEL_ARCHIVE)
chapel_dl_suffix_p7zip = .zip
#  Option 3: "tgz" - tar + gzip
#   This is the most portable, but it sacrifices having the git hash in the log file.
chapel-unpack-tgz:   force; $(GZCAT) $(CHAPEL_ARCHIVE) | $(TAR) xf -
chapel_dl_suffix_tgz   = .tar.gz
####
chapel_dl_suffix = $(chapel_dl_suffix_$(strip $(CHAPEL_DOWNLOAD)))
.PHONY: chapel-download chapel-unpack-unzip chapel-unpack-p7zip chapel-unpack-tgz

# Save time by building for a single conduit
CHPL_GASNET_CFG_OPTIONS += --disable-auto-conduit-detect --enable-$(CHAPEL_CONDUIT)

COMMON_ENV = CHPL_COMM=gasnet CHPL_COMM_SUBSTRATE=$(CHAPEL_CONDUIT)

# Build Chapel runtime
chapel: force
	rm -f chapel-built
	$(MAKE) chapel-download
	cd $(CHAPEL_BLDDIR)/third-party/gasnet && mv gasnet-src gasnet-src.dist
	cp -Rp $(TOP_SRCDIR)/gasnet $(CHAPEL_BLDDIR)/third-party/gasnet/gasnet-src
	cd $(CHAPEL_BLDDIR) && \
	source util/setchplenv.bash && \
	  env $(COMMON_ENV) $(CHAPEL_BUILD_ENV) \
	    $(MAKE) $(CHAPEL_MAKE_ARGS) all > $(HARNESS_LOGDIR)/chapel.log 2>&1
	@echo '#!/bin/sh' > $@ ; chmod +x $@
	@touch chapel-built

# Not reliable other than on UDP, and wants to *run* the test
check: chapel-built
	source $(CHAPEL_BLDDIR)/util/setchplenv.bash && \
	  env $(COMMON_ENV) $(CHAPEL_CHECK_ENV) \
	    $(MAKE) -C $(CHAPEL_BLDDIR) check
	@echo '#!/bin/sh' > $@ ; chmod +x $@

#
# Build one test
#
test_common: chapel-built
	rm -f $(TEST_EXE){,_real}
	source $(CHAPEL_BLDDIR)/util/setchplenv.bash && \
	  env $(COMMON_ENV) $(CHAPEL_TEST_ENV) \
	    $(MAKE) -C $(CHAPEL_BLDDIR)/$(TEST_DIR) $(REAL_EXE) 2>&1
	$(CP_MV) $(CHAPEL_BLDDIR)/$(TEST_DIR)/$(REAL_EXE) $(TEST_EXE)
	if test -e $(CHAPEL_BLDDIR)/$(TEST_DIR)/$(REAL_EXE)_real; then \
	  $(CP_MV) $(CHAPEL_BLDDIR)/$(TEST_DIR)/$(REAL_EXE)_real $(TEST_EXE)_real; \
	fi
one_test: force  # Using the tests natural name
	$(MAKE) test_common REAL_EXE=$(TEST_EXE) CP_MV=mv
alt_test: force  # Using an alternate test name
	$(MAKE) test_common CP_MV=cp
.PHONY: test_common one_test alt_test


# top-level examples
EXAMPLES = hello hello2-module hello3-datapar hello4-datapar-dist hello5-taskpar hello6-taskpar-dist
$(EXAMPLES): force; $(MAKE) one_test TEST_EXE=$@ TEST_DIR=examples

# directory examples/benchmarks
BENCHMARKS = isx lulesh miniMD ssca2
$(BENCHMARKS): force; $(MAKE) one_test TEST_EXE=$@ TEST_DIR=examples/benchmarks/$@

# directory examples/benchmarks/hpcc
HPCC = stream stream-ep ra ra-atomics fft ptrans hpl
$(HPCC): force; $(MAKE) one_test TEST_EXE=$@ TEST_DIR=examples/benchmarks/hpcc

# directory examples/benchmarks/isx
ISX = isx-strong isx-weak isx-weakISO
$(ISX): force; $(MAKE) alt_test TEST_EXE=$@ REAL_EXE=isx TEST_DIR=examples/benchmarks/isx

# directory examples/primers
PRIMERS = \
        arrays arrayVectorOps associative atomics chpldoc.doc classes         \
        distributions domains fileIO genericClasses iterators locales         \
        modules opaque parIters procedures randomNumbers ranges reductions    \
        slices sparse syncsingle taskParallel timers varargs variables        \
        voidVariables
$(PRIMERS): force; $(MAKE) one_test TEST_EXE=$@ TEST_DIR=examples/primers/$@

# directory examples/programs
PROGRAMS = beer genericStack jacobi linkedList norm prodCons quicksort tree
$(PROGRAMS): force; $(MAKE) one_test TEST_EXE=$@ TEST_DIR=examples/programs/$@

force:

.PHONY: force

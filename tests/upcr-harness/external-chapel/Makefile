CHAPEL_GIT_REPO   ?= chapel-lang/chapel
CHAPEL_GIT_COMMIT ?= master
CHAPEL_MAKE_ARGS  ?= -j4
CHAPEL_BLDDIR     ?= $(TOP_BUILDDIR)/chapel
CHAPEL_TMPDIR     ?= $(TOP_BUILDDIR)/chapel-tmp
CHAPEL_FLAGS      ?= #empty
CHAPEL_CONDUIT    ?= $(NETWORK) # Default to same conduit as the enclosing harness run

HARNESS_LOGDIR    ?= .
WGET              ?= wget -nv
ZIP_COMMENT       ?= unzip -z
ZIP_EXTRACT       ?= unzip -q

# Must not inherit these from harness environment:
unexport LD LDFLAGS LIBS
unexport CC CFLAGS
unexport CXX CXXFLAGS
unexport AR RANLIB

# Save time by building for a single conduit
CHPL_GASNET_CFG_OPTIONS += --disable-auto-conduit-detect --enable-$(CHAPEL_CONDUIT)

COMMON_ENV = CHPL_COMM=gasnet CHPL_COMM_SUBSTRATE=$(CHAPEL_CONDUIT)

# Build Chapel runtime
chapel: force
	rm -Rf chapel-built $(CHAPEL_TMPDIR) $(CHAPEL_BLDDIR)
	mkdir $(CHAPEL_TMPDIR) && cd $(CHAPEL_TMPDIR) && \
	  $(WGET) https://github.com/$(CHAPEL_GIT_REPO)/archive/$(CHAPEL_GIT_COMMIT).zip 2>&1 && \
	  $(ZIP_COMMENT) $(CHAPEL_GIT_COMMIT).zip && \
	  $(ZIP_EXTRACT) $(CHAPEL_GIT_COMMIT).zip && \
	  rm $(CHAPEL_GIT_COMMIT).zip && \
	  mv * $(CHAPEL_BLDDIR) # archive's root dir will vary
	rm -Rf $(CHAPEL_TMPDIR)
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

# Build one test
one_test: chapel-built
	source $(CHAPEL_BLDDIR)/util/setchplenv.bash && \
	  env $(COMMON_ENV) $(CHAPEL_TEST_ENV) \
	    chpl $(CHAPEL_FLAGS) -o $(TEST_EXE) $(addprefix $$CHPL_HOME/,$(TEST_SRC)) 2>&1

example%: force; $(MAKE) one_test TEST_EXE=$@ TEST_SRC=examples/$(@:example-%=%).chpl

force:

.PHONY: force one_test

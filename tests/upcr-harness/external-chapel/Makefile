CHAPEL_GIT_REPO   ?= chapel-lang/chapel
CHAPEL_GIT_COMMIT ?= master
CHAPEL_MAKE_ARGS  ?= -j4
CHAPEL_BLDDIR     ?= $(TOP_BUILDDIR)/chapel
CHAPEL_TMPDIR     ?= $(TOP_BUILDDIR)/chapel-tmp
CHAPEL_FLAGS      ?= #empty

HARNESS_LOGDIR    ?= .
WGET              ?= wget -nv

# Must not inherit these from harness environment:
unexport LD LDFLAGS LIBS
unexport CC CFLAGS
unexport CXX CXXFLAGS
unexport AR RANLIB

COMMON_ENV = CHPL_COMM=gasnet CHPL_COMM_SUBSTRATE=$(NETWORK)

chapel: force
	rm -Rf chapel-built $(CHAPEL_TMPDIR) $(CHAPEL_BLDDIR)
	mkdir $(CHAPEL_TMPDIR) && cd $(CHAPEL_TMPDIR) && \
	  $(WGET) https://github.com/$(CHAPEL_GIT_REPO)/archive/$(CHAPEL_GIT_COMMIT).zip 2>&1 && \
	  unzip -z $(CHAPEL_GIT_COMMIT).zip && \
	  unzip -q $(CHAPEL_GIT_COMMIT).zip && \
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

example%: chapel-built
	source $(CHAPEL_BLDDIR)/util/setchplenv.bash && \
	  env $(COMMON_ENV) $(CHAPEL_TEST_ENV) \
	    chpl $(CHAPEL_FLAGS) -o $@ $$CHPL_HOME/examples/$(@:example-%=%).chpl 2>&1

force:

.PHONY: force

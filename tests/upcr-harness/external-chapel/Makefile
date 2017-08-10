CHAPEL_CLONE_ARGS ?= --depth=1 --branch=master
CHAPEL_CLONE_URL  ?= https://github.com/chapel-lang/chapel.git
CHAPEL_CLONE_DIR  ?= $(TOP_BUILDDIR)/chapel
CHAPEL_BUILD_ENV  ?= #empty
CHAPEL_CHECK_ENV  ?= #empty
CHAPEL_FLAGS      ?= #empty

# Re: $(CHAPEL_CLONE_DIR) the default is to clone into $(TOP_BUILDDIR), which
# is often a local f/s even when the harness workdir is not.

chapel: force
	env
	rm -rf chapel-built $(CHAPEL_CLONE_DIR)
	git clone -q $(CHAPEL_CLONE_ARGS) $(CHAPEL_CLONE_URL) $(CHAPEL_CLONE_DIR)
	git --git-dir=$(CHAPEL_CLONE_DIR)/.git describe --always
	cd $(CHAPEL_CLONE_DIR)/third-party/gasnet && mv gasnet-src gasnet-src.dist
	cp -Rp $(TOP_SRCDIR)/gasnet $(CHAPEL_CLONE_DIR)/third-party/gasnet/gasnet-src
	source $(CHAPEL_CLONE_DIR)/util/setchplenv.bash && \
	  env CHPL_COMM=gasnet CHPL_COMM_SUBSTRATE=$(NETWORK) $(CHAPEL_BUILD_ENV) \
	    $(MAKE) -C $(CHAPEL_CLONE_DIR) all 2>&1
	@echo '#!/bin/sh' > $@ ; chmod +x $@
	@touch chapel-built

example-%: chapel-built
	source $(CHAPEL_CLONE_DIR)/util/setchplenv.bash && \
	  env CHPL_COMM=gasnet CHPL_COMM_SUBSTRATE=$(NETWORK) $(CHAPEL_CHECK_ENV) \
	    chpl $(CHAPEL_FLAGS) -o $@ $$CHPL_HOME/examples/$(@:example-%=%).chpl 2>&1

force:

.PHONY: force

UPCXX_GIT_REPO   ?= berkeleylab/upcxx
UPCXX_GIT_COMMIT ?= develop
UPCXX_BLDDIR     ?= $(TOP_BUILDDIR)/upcxx
UPCXX_TMPDIR     ?= $(TOP_BUILDDIR)/upcxx-tmp
UPCXX_GASNET     ?= # Path to an existing installation, if any
UPCXX_CONDUIT    ?= $(NETWORK) # Default to same conduit as the enclosing harness run

HARNESS_LOGDIR    ?= .
LOGFILE = $(HARNESS_LOGDIR)/upcxx.log
TO_LOG = >> $(LOGFILE) 2>&1

# nobs requires bash
SHELL := /bin/bash

# Paths and options for standard tools
WGET              ?= wget -nv
GZCAT             ?= gzip -cd
TAR               ?= tar
UNZIP             ?= unzip
P7ZIP             ?= 7za

RUNTIME_TARGETS = test/hello_gasnet.cpp test/hello_upcxx.cpp
COMMON_ENV = \
        CC=$(CC) CXX=$(CXX) \
        DBGSYM=$(DBGSYM) OPTLEV=$(OPTLEV) \
	GASNET="$(UPCXX_BLDDIR)/gasnet/include" GASNET_CONDUIT=$(UPCXX_CONDUIT)
DO_NOBS = cd $(UPCXX_BLDDIR) && source ./sourceme && export $(COMMON_ENV) $(UPCXX_TEST_ENV) && nobs

# XXX: Hard-coded download of fixed tarball for now
UPCXX_URL      ?= https://mantis.lbl.gov/nightly/unlisted/berkeleylab-upcxx-$(UPCXX_GIT_COMMIT)$(upcxx_dl_suffix)

# Parametrized support for download/unpack
UPCXX_URL      ?= https://bitbucket.org/$(UPCXX_GIT_REPO)/get/$(UPCXX_GIT_COMMIT)$(upcxx_dl_suffix)
UPCXX_ARCHIVE  ?= berkeleylab-upcxx-*$(upcxx_dl_suffix)
UPCXX_DOWNLOAD ?= unzip # one of "unzip", "p7zip" or "tgz"
upcxx-download: force
	rm -Rf $(UPCXX_BLDDIR) $(UPCXX_TMPDIR) && mkdir $(UPCXX_TMPDIR)
	cd $(UPCXX_TMPDIR) && $(WGET) $(UPCXX_URL) 2>&1
	@cd $(UPCXX_TMPDIR) && echo Fetched $(UPCXX_ARCHIVE)
	$(MAKE) -C $(UPCXX_TMPDIR) -f `pwd`/Makefile upcxx-unpack-$(UPCXX_DOWNLOAD)
	rm $(UPCXX_TMPDIR)/$(UPCXX_ARCHIVE)
	mv $(UPCXX_TMPDIR)/* $(UPCXX_BLDDIR) # archive's root dir will vary
	rmdir $(UPCXX_TMPDIR)
# Three ways to unpack the archive:
#  Option 1: "unzip" - .zip w/ unzip
#   This is the favored approach because it gives us the hash and uses a widely available utility.
upcxx-unpack-unzip: force; $(UNZIP) -z $(UPCXX_ARCHIVE) && $(UNZIP) -q $(UPCXX_ARCHIVE)
upcxx_dl_suffix_unzip = .zip
#  Option 2: "p7zip" - .zip w/ 7za (from p7zip package)
#   This also gives us the hash, but uses a less widely available utility.
#   However, it is sometimes necessary because many unzip installations contain a bug w.r.t. symlinks
upcxx-unpack-p7zip: force; $(P7ZIP) x -bd $(UPCXX_ARCHIVE)
upcxx_dl_suffix_p7zip = .zip
#  Option 3: "tgz" - tar + gzip
#   This is the most portable, but it sacrifices having the git hash in the log file.
upcxx-unpack-tgz:   force; $(GZCAT) $(UPCXX_ARCHIVE) | $(TAR) xf -
upcxx_dl_suffix_tgz   = .tar.gz
####
upcxx_dl_suffix = $(upcxx_dl_suffix_$(strip $(UPCXX_DOWNLOAD)))
.PHONY: upcxx-download upcxx-unpack-unzip upcxx-unpack-p7zip upcxx-unpack-tgz

# Symlink or simple script that corresponds to
#    RunCmd = ./launcher -np %N %P %A
launcher:
	@set -x; case $(UPCXX_CONDUIT) in \
	smp) echo '#!/bin/sh' > $@ ; \
	     echo 'N=$$2;shift 2;eval env GASNET_PSHM_NODES=$$N "$$@"' >> $@ ; \
	     chmod +x $@;; \
	udp) ln -s "$(UPCXX_BLDDIR)"/gasnet/bin/amudprun $@;; \
	  *) ln -s "$(UPCXX_BLDDIR)"/gasnet/bin/gasnetrun_$(UPCXX_CONDUIT) $@;; \
	esac

upcxx: force
	rm -Rf upcxx-built launcher $(LOGFILE)
	$(MAKE) upcxx-download
	@if test -f "$(UPCXX_GASNET)/include/gasnet.h"; then \
	  echo ======== GASNet symlinked ========; set -x; \
	  ln -s `cd "$(UPCXX_GASNET)" && pwd` $(UPCXX_BLDDIR)/gasnet; \
	else \
	  echo ======== GASNet install ========; set -x; \
	  $(MAKE) install -C $(TOP_BUILDDIR)/gasnet prefix="$(UPCXX_BLDDIR)/gasnet"; \
	fi $(TO_LOG)
	$(MAKE) launcher $(TO_LOG)
	@( echo ======== Prebuild UPCXX RT libs ========; \
	  for f in $(RUNTIME_TARGETS); do \
             echo '$(DO_NOBS) exe '$$f ; \
                   $(DO_NOBS) exe $$f || exit 1; \
          done; \
	) $(TO_LOG)
	@echo '#!/bin/sh' > $@ ; chmod +x $@
	@touch upcxx-built

# Build a single test
one_test: upcxx-built
	$(DO_NOBS) exe $(TEST_PATH)
	OUTPUT=`$(DO_NOBS) exe $(TEST_PATH)` && cp $$OUTPUT $(TEST_EXE)

# Pattern rule builds any named test with a corresponding cpp in test directory
% :: $(UPCXX_BLDDIR)/test/%.cpp force
	$(MAKE) one_test TEST_PATH=$< TEST_EXE=$@

force:

.PHONY: force one_test 

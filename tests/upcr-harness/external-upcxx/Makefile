UPCXX_GIT_REPO   ?= berkeleylab/upcxx
UPCXX_GIT_COMMIT ?= develop
UPCXX_BLDDIR     ?= $(TOP_BUILDDIR)/upcxx
UPCXX_INSTDIR    ?= $(TOP_BUILDDIR)/upcxx-inst
UPCXX_TMPDIR     ?= $(TOP_BUILDDIR)/upcxx-tmp
UPCXX_CONDUIT    ?= $(NETWORK) # Default to same conduit as the enclosing harness run
UPCXX_EXTRA_FLAGS ?=# extra flags to add to both compilers
UPCXX_BACKEND    ?= gasnet1_seq

# Options for providing GASNET to nobs:
# 1. set UPCXX_GASNET to an existing build or install path to use
# The remaining options assume $(TOP_BUILDDIR)/gasnet is a valid build dir
# 2. set UPCXX_INSTALL_GASNET=1 to perform a GASNet install and use it
# 3. set UPCXX_TARBALL_GASNET=1 to create a GASNet tarball and use it
#    this one notably implies nobs will configure and build GASNet
# 4. Default is to point at the existing $(TOP_BUILDDIR)/gasnet build
ifneq ($(strip $(UPCXX_GASNET)),)
  UPCXX_EXTERNAL_GASNET:=1
  NOBS_GASNET:=$(UPCXX_GASNET)
else ifneq ($(strip $(UPCXX_INSTALL_GASNET)),)
  NOBS_GASNET:=$(UPCXX_BLDDIR)/gasnet
  UPCXX_LAUNCHERS:=$(UPCXX_BLDDIR)/gasnet
  UPCXX_FRAGMENTS:=$(TOP_BUILDDIR)/gasnet
else ifneq ($(strip $(UPCXX_TARBALL_GASNET)),)
  NOBS_GASNET:=$(UPCXX_BLDDIR)/gasnet.tar.gz
  UPCXX_FRAGMENTS:=$(TOP_BUILDDIR)/gasnet
  # Don't pollute nobs configure with harness flags
  UNSET_VARS = CFLAGS CXXFLAGS CPPFLAGS LDFLAGS LIBS
  # use meta to prevent a spurious second configure
  UPCXX_USE_META = 1
else # default to using builddir
  NOBS_GASNET:=$(TOP_BUILDDIR)/gasnet
endif
UPCXX_FRAGMENTS ?= $(NOBS_GASNET)
UPCXX_LAUNCHERS ?= $(UPCXX_FRAGMENTS)

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
GIT               ?= git


# Logic to extract GASNet's compiler variables.
# nobs does not yet automatically pickup GASNET_CC/CXX for external GASNET
# intentionally ignore the environment here because it is set by harness
# use UPCXX_TEST_ENV to override if necessary.
# We use a sub-shell to avoid pulling in the entire makefile fragment.
# TODO: may want to handle LDFLAGS here as well?
GASNET_VARS = $(shell $(MAKE) --no-print-directory echovars FRAGMENT=$(strip $(UPCXX_CONDUIT)))
FRAGMENT=#empty by default
ifneq ($(FRAGMENT),) # handle build or install dir, use any fragment we can find
-include $(UPCXX_FRAGMENTS)/$(FRAGMENT)-conduit/$(FRAGMENT)-seq.mak
-include $(UPCXX_FRAGMENTS)/$(FRAGMENT)-conduit/$(FRAGMENT)-par.mak
-include $(UPCXX_FRAGMENTS)/include/$(FRAGMENT)-conduit/$(FRAGMENT)-seq.mak
-include $(UPCXX_FRAGMENTS)/include/$(FRAGMENT)-conduit/$(FRAGMENT)-par.mak
echovars: force; @echo "CC='$(GASNET_CC)' CXX='$(GASNET_CXX)'"
.PHONY: echovars
endif

RUNTIME_TARGETS = test/hello_gasnet.cpp test/hello_upcxx.cpp
COMMON_ENV = \
        $(GASNET_VARS) \
        DBGSYM=$(DBGSYM) OPTLEV=$(OPTLEV) \
	GASNET="$(NOBS_GASNET)" GASNET_CONDUIT=$(UPCXX_CONDUIT) \
	TERM=dumb PAGER=cat 
DO_ENV = unset $(UNSET_VARS) ; export $(COMMON_ENV) $(UPCXX_TEST_ENV) && export CC="$$CC $(UPCXX_EXTRA_FLAGS)" CXX="$$CXX $(UPCXX_EXTRA_FLAGS)"
DO_NOBS = cd $(UPCXX_BLDDIR) && source ./sourceme && $(DO_ENV) && nobs

# XXX: Hard-coded download of fixed tarball for now
UPCXX_URL      ?= https://upc-bugs.lbl.gov/nightly/unlisted/berkeleylab-upcxx-$(UPCXX_GIT_COMMIT)$(upcxx_dl_suffix)

# Parametrized support for download/unpack
UPCXX_URL      ?= https://bitbucket.org/$(UPCXX_GIT_REPO)/get/$(UPCXX_GIT_COMMIT)$(upcxx_dl_suffix)
UPCXX_ARCHIVE  ?= berkeleylab-upcxx-*$(upcxx_dl_suffix)
UPCXX_DOWNLOAD ?= unzip # one of "unzip", "p7zip" or "tgz"
upcxx-download: force
	rm -Rf $(UPCXX_BLDDIR) $(UPCXX_TMPDIR) && mkdir $(UPCXX_TMPDIR)
	cd $(UPCXX_TMPDIR) && $(WGET) $(UPCXX_URL) 2>&1
	@cd $(UPCXX_TMPDIR) && echo Fetched $(UPCXX_ARCHIVE)
	$(MAKE) -C $(UPCXX_TMPDIR) -f `pwd`/Makefile upcxx-unpack-$(UPCXX_DOWNLOAD)
	rm -f $(UPCXX_TMPDIR)/$(UPCXX_ARCHIVE) $(UPCXX_TMPDIR)/wget-log
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
#   This is the most portable, but cannot extract the hash unless git is available
upcxx-unpack-tgz:   force
	-$(GZCAT) $(UPCXX_ARCHIVE) | $(GIT) get-tar-commit-id ; exit 0
	$(GZCAT) $(UPCXX_ARCHIVE) | $(TAR) xf -
upcxx_dl_suffix_tgz   = .tar.gz
####
upcxx_dl_suffix = $(upcxx_dl_suffix_$(strip $(UPCXX_DOWNLOAD)))
.PHONY: upcxx-download upcxx-unpack-unzip upcxx-unpack-p7zip upcxx-unpack-tgz

# Symlink or simple script that corresponds to
#    RunCmd = ./launcher -np %N %P %A
launcher:
	@set -x; \
	if test $(UPCXX_CONDUIT) = smp; then \
	  echo '#!/bin/sh' > $@ ; \
	  echo 'N=$$2;shift 2;eval env GASNET_PSHM_NODES=$$N "$$@"' >> $@ ; \
	  chmod +x $@; \
	elif test -d "$(UPCXX_LAUNCHERS)"/bin; then \
	  case $(UPCXX_CONDUIT) in \
	  udp) ln -s "$(UPCXX_LAUNCHERS)"/bin/amudprun $@;; \
	    *) ln -s "$(UPCXX_LAUNCHERS)"/bin/gasnetrun_$(UPCXX_CONDUIT) $@;; \
	  esac; \
	else \
	  case $(UPCXX_CONDUIT) in \
	  udp) ln -s "$(UPCXX_LAUNCHERS)"/other/amudp/amudprun $@;; \
	    *) ln -s "$(UPCXX_LAUNCHERS)"/$(strip $(UPCXX_CONDUIT))-conduit/contrib/gasnetrun_$(UPCXX_CONDUIT) $@;; \
	  esac; \
	fi

_upcxx: force
	rm -Rf upcxx-built launcher $(LOGFILE)
	$(MAKE) upcxx-download 2>&1 | tee $(LOGFILE)
	@echo UPCXX_GASNET=$(UPCXX_GASNET) NOBS_GASNET=$(NOBS_GASNET) UPCXX_BLDDIR=$(UPCXX_BLDDIR) UPCXX_FRAGMENTS=$(UPCXX_FRAGMENTS) $(TO_LOG)
	@if test -n "$(UPCXX_EXTERNAL_GASNET)" ; then \
	  echo ======== UPCXX_GASNET:external ========; set -x; \
	elif test -n "$(UPCXX_INSTALL_GASNET)" ; then \
	  echo ======== UPCXX_GASNET:install ========; set -x; \
	  $(MAKE) install -C $(TOP_BUILDDIR)/gasnet prefix="$(UPCXX_BLDDIR)/gasnet"; \
	elif test -n "$(UPCXX_TARBALL_GASNET)" ; then \
	  echo ======== UPCXX_GASNET:tarball ========; set -x; \
	  $(MAKE) dist -C $(TOP_BUILDDIR)/gasnet && \
          cp -f $(TOP_BUILDDIR)/gasnet/GASNet-*.tar.gz $(UPCXX_BLDDIR)/gasnet.tar.gz ; \
	else \
	  echo ======== UPCXX_GASNET:builddir ========; set -x; \
	fi $(TO_LOG)
	$(MAKE) launcher $(TO_LOG)
	-echo PATH="$$PATH" $(TO_LOG)
	@-( set -x; $(DO_ENV) ; $$CC  --version ) 2>&1 | grep -i -v -e error -e warning $(TO_LOG)
	@-( set -x; $(DO_ENV) ; $$CXX --version ) 2>&1 | grep -i -v -e error -e warning $(TO_LOG)
	@( echo ======== Prebuild UPCXX RT libs ========; \
	  for f in $(RUNTIME_TARGETS); do \
             echo '$(DO_NOBS) exe '$$f ; \
                   $(DO_NOBS) exe $$f || exit 1; \
             echo ; \
          done; \
	) $(TO_LOG)
	@touch upcxx-built
	$(MAKE) upcxx-install 2>&1 | tee -a $(LOGFILE)
	@echo '#!/bin/sh' > $@ ; chmod +x $@

upcxx-install: upcxx-built # NO force
	@echo UPCXX_GASNET=$(UPCXX_GASNET) NOBS_GASNET=$(NOBS_GASNET) UPCXX_BLDDIR=$(UPCXX_BLDDIR) UPCXX_FRAGMENTS=$(UPCXX_FRAGMENTS)
	rm -Rf $(UPCXX_INSTDIR) upcxx-install 
	@echo ======== UPCXX install ========; set -x; \
	 cd $(UPCXX_BLDDIR) && $(DO_ENV) && ./install $(UPCXX_INSTDIR) && \
	 /bin/ls -alR $(UPCXX_INSTDIR)
	@touch upcxx-install

# Build a single test from the nobs build directory
one_test: upcxx-built
	$(DO_NOBS) exe $(TEST_PATH)
	OUTPUT=`$(DO_NOBS) exe $(TEST_PATH)` && cp $$OUTPUT $(TEST_EXE)

# Build a single test from the installed UPCXX directory
inst_test: upcxx-install
	@set -x ; $(DO_ENV) && \
	PPFLAGS=`$(UPCXX_INSTDIR)/bin/upcxx-meta PPFLAGS` && \
	LDFLAGS=`$(UPCXX_INSTDIR)/bin/upcxx-meta LDFLAGS` && \
	LIBFLAGS=`$(UPCXX_INSTDIR)/bin/upcxx-meta LIBFLAGS` && \
	$$CXX -std=c++11 $$PPFLAGS $$LDFLAGS -o $(TEST_EXE) $(TEST_PATH) $$LIBFLAGS

VPATH=$(UPCXX_BLDDIR)/test:$(UPCXX_BLDDIR)/test/uts

# Pattern rule builds any named test with a corresponding cpp in test directory
% :: %.cpp force
	$(MAKE) one_test TEST_PATH=$< TEST_EXE=$@

# Pattern rule builds example/prog-guide tests
% :: $(UPCXX_BLDDIR)/example/prog-guide/%.cpp upcxx-install force
	@if test -n "$(UPCXX_USE_META)"; then set -x ; \
	  $(MAKE) inst_test TEST_PATH=$< TEST_EXE=$@ ; \
	else set -x ; \
	  $(MAKE) one_test TEST_PATH=$< TEST_EXE=$@ UPCXX_EXTRA_FLAGS='$(UPCXX_EXTRA_FLAGS) -DUPCXX_BACKEND=$(UPCXX_BACKEND)' ; \
	fi

force:

.PHONY: force one_test 

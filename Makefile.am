## Process this file with automake to produce Makefile.in	-*- makefile -*-
#  $Archive:: /Ti/AMMPI/Makefile                                         $
#     $Date: 2003/12/11 20:19:48 $
# $Revision: 1.20 $
# Description: Makefile for GASNet 
# Copyright 2002, Dan Bonachea <bonachea@cs.berkeley.edu>
# Terms of use are as specified in license.txt

AUTOMAKE_OPTIONS = foreign 1.4

SUBDIRS =            \
	config-aux   \
        other        \
        elan-conduit \
        gm-conduit   \
        lapi-conduit \
	mpi-conduit  \
	smp-conduit  \
	udp-conduit  \
	vapi-conduit

configdir = $(includedir)
config_DATA = $(CONFIG_HEADER)

headers =                  \
        gasnet.h           \
	gasnet_atomicops.h \
        gasnet_basic.h     \
        gasnet_handler.h   \
        gasnet_help.h      \
        gasnet_internal.h  \
        gasnet_timer.h     \
	gasnet_tools.h     \
	gasnet_trace.h     

EXTRA_DIST =              \
        README            \
	ChangeLog	  \
	license.txt       \
	docs              \
        extended-ref      \
	template-conduit  \
        Bootstrap         \
        unBootstrap       \
        configure.in      \
        acconfig.h        \
        acinclude.m4      \
        $(headers)        \
        gasnet_internal.c \
        gasnet_mmap.c 
#       tests handled below

include_HEADERS = $(headers)

force:
	
TEST_TYPE=tests-seq

tests: force
	@$(MAKE) -C $(top_builddir)/other all tests
if USE_MPI_CONDUIT
	@$(MAKE) -C $(top_builddir)/mpi-conduit all $(TEST_TYPE)
endif
if USE_UDP_CONDUIT
	@$(MAKE) -C $(top_builddir)/udp-conduit all $(TEST_TYPE)
endif
if USE_GM_CONDUIT
	@$(MAKE) -C $(top_builddir)/gm-conduit all $(TEST_TYPE)
endif
if USE_ELAN_CONDUIT
	@$(MAKE) -C $(top_builddir)/elan-conduit all $(TEST_TYPE)
endif
if USE_LAPI_CONDUIT
	@$(MAKE) -C $(top_builddir)/lapi-conduit all $(TEST_TYPE)
endif
if USE_SMP_CONDUIT
	@$(MAKE) -C $(top_builddir)/smp-conduit all $(TEST_TYPE)
endif
if USE_VAPI_CONDUIT
	@$(MAKE) -C $(top_builddir)/vapi-conduit all $(TEST_TYPE)
endif

test: tests

tests-seq: 
	$(MAKE) -C $(top_builddir) TEST_TYPE=tests-seq tests

tests-par: 
	$(MAKE) -C $(top_builddir) TEST_TYPE=tests-par tests

tests-parsync: 
	$(MAKE) -C $(top_builddir) TEST_TYPE=tests-parsync tests

.PHONY: force clean tests test tests-seq tests-par tests-parsync

dist-hook:
	if test -f $(distdir)/docs/Makefile ; then \
          $(MAKE) -C $(distdir)/docs dist-hook ; \
        fi
	mkdir -p $(distdir)/tests  # need this special hack to handle tests or distcheck will fail
	cp -Rp $(srcdir)/tests $(distdir)
	rm -Rf `find $(distdir) -name CVS -type d -print`
	rm -f `find $(distdir) -name .#\* -print`



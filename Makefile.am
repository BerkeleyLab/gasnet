## Process this file with automake to produce Makefile.in	-*- makefile -*-
#  $Archive:: /Ti/AMMPI/Makefile                                         $
#     $Date: 2003/05/04 01:33:44 $
# $Revision: 1.15 $
# Description: Makefile for GASNet 
# Copyright 2002, Dan Bonachea <bonachea@cs.berkeley.edu>
# Terms of use are as specified in license.txt

AUTOMAKE_OPTIONS = foreign 1.4

SUBDIRS =            \
        other        \
        elan-conduit \
        gm-conduit   \
        lapi-conduit \
	mpi-conduit  \
	smp-conduit

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
	config-aux        \
        README            \
	ChangeLog	  \
	license.txt       \
	docs              \
        extended-ref      \
	template-conduit  \
        tests             \
        Bootstrap         \
        unBootstrap       \
        configure.in      \
        acconfig.h        \
        acinclude.m4      \
        $(headers)        \
        gasnet_internal.c \
        gasnet_mmap.c 

include_HEADERS = $(headers)

force:
	

tests: force
	@$(MAKE) -C $(top_builddir)/other all tests
if USE_MPI_CONDUIT
	@$(MAKE) -C $(top_builddir)/mpi-conduit all tests-seq
endif
if USE_GM_CONDUIT
	@$(MAKE) -C $(top_builddir)/gm-conduit all tests-seq
endif
if USE_ELAN_CONDUIT
	@$(MAKE) -C $(top_builddir)/elan-conduit all tests-seq
endif
if USE_LAPI_CONDUIT
	@$(MAKE) -C $(top_builddir)/lapi-conduit all tests-seq
endif
if USE_SMP_CONDUIT
	@$(MAKE) -C $(top_builddir)/smp-conduit all tests-seq
endif

.PHONY: force clean tests

dist-hook:
	if test -f $(distdir)/docs/Makefile ; then \
          gmake -C $(distdir)/docs dist-hook ; \
        fi
	rm -Rf `find $(distdir) -name CVS -type d -print`
	rm -f `find $(distdir) -name .#\* -print`


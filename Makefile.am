## Process this file with automake to produce Makefile.in	-*- makefile -*-
#  $Archive:: /Ti/AMMPI/Makefile                                         $
#     $Date: 2004/07/29 04:15:20 $
# $Revision: 1.26.2.1 $
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
	sci-conduit  \
	smp-conduit  \
	udp-conduit  \
	vapi-conduit

configdir = $(includedir)
config_DATA = $(CONFIG_HEADER)

headers =                  \
        gasnet.h           \
	gasnet_ammacros.h  \
	gasnet_atomicops.h \
        gasnet_basic.h     \
        gasnet_handler.h   \
        gasnet_help.h      \
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
        gasnet_internal.h \
        gasnet_internal.c \
        gasnet_mmap.c 
#       tests handled below

include_HEADERS = $(headers)

force:

# echo an arbitrary make/environment variable
echovar: force
	@echo $(VARNAME)=\"$($(VARNAME))\"
	
test: tests-seq
tests: tests-seq

other-tests:
	@$(MAKE) -C $(top_builddir)/other all tests

other-run-tests:
	@$(MAKE) -C $(top_builddir)/other run-tests

tests-seq: other-tests
	@for conduit in $(CONDUITS) ; do                                      \
	  $(MAKE) -C "$(top_builddir)/$$conduit-conduit" tests-seq || exit 1; \
	done

tests-par: other-tests
	@for conduit in $(CONDUITS) ; do                                      \
	  $(MAKE) -C "$(top_builddir)/$$conduit-conduit" tests-par || exit 1; \
	done

tests-parsync: other-tests
	@for conduit in $(CONDUITS) ; do                                          \
	  $(MAKE) -C "$(top_builddir)/$$conduit-conduit" tests-parsync || exit 1; \
	done

run-tests: other-run-tests
	@for conduit in $(CONDUITS) ; do                                      \
	  $(MAKE) -C "$(top_builddir)/$$conduit-conduit" run-tests || exit 1; \
	done

run-tests-seq: tests-seq run-tests
run-tests-par: tests-par run-tests
run-tests-parsync: tests-parsync run-tests

.PHONY: force clean tests test                                        \
        other-tests other-run-tests tests-seq tests-par tests-parsync \
        run-tests-seq run-tests-par run-tests-parsync

dist-hook:
	if test -f $(distdir)/docs/Makefile ; then \
          $(MAKE) -C $(distdir)/docs dist-hook ; \
        fi
	mkdir -p $(distdir)/tests  # need this special hack to handle tests or distcheck will fail
	fulldistdir=`cd $(distdir) ; pwd` export fulldistdir ; \
        cd $(srcdir)/tests ; for file in `find . -print` ; do \
 	  if test -d $$file -a ! -d $$fulldistdir/tests/$$file ; then \
	     mkdir -p $$fulldistdir/tests/$$file ; \
          fi; \
	  if test -f $$file -a ! -f $$fulldistdir/tests/$$file ; then \
	     cp -p $$file $$fulldistdir/tests/$$file ; \
          fi ; \
        done
	rm -Rf `find $(distdir) -name CVS -type d -print`
	rm -f `find $(distdir) -name .#\* -print`



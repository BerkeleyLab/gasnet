## Process this file with automake to produce Makefile.in	-*- makefile -*-
#   $Source: /Users/kamil/work/gasnet-cvs2/gasnet/other/Makefile.am,v $
#     $Date: 2006/06/06 22:35:19 $
# $Revision: 1.25.8.1 $
# Description: Makefile for GASNet other 
# Copyright 2002, Dan Bonachea <bonachea@cs.berkeley.edu>
# Terms of use are as specified in license.txt

AUTOMAKE_OPTIONS = foreign 1.4

SUBDIRS =          \
        ammpi      \
	amudp	   \
	contrib    \
	testspawn


EXTRA_DIST =        \
	amxtests    \
	firehose    \
	ssh-spawner \
	mpi-spawner \
	detect-cachesz.c 
# 	exclude these from distribution for now
# 	cross-configure-help.c

include_HEADERS = \
	portable_inttypes.h \
	portable_platform.h

if USE_UDP_CONDUIT                           
  UDP_dir=$(top_builddir)/other/amudp
endif
if USE_MPI_CONDUIT                           
  MPI_dir=$(top_builddir)/other/ammpi
endif
testdirs= $(UDP_dir) $(MPI_dir)

tests tests-clean run-tests check-exports: 
	@list='$(testdirs)'; for dir in $$list ; do \
	  ( cd "$$dir" && $(MAKE) $@ ) || exit $$?; \
	done

all-local: Makefile-conduit.mak perlstart

.PHONY: force clean tests tests-clean run-tests

force:
	

## Process this file with automake to produce Makefile.in	-*- makefile -*-
#  $Archive:: /Ti/AMMPI/Makefile                                         $
#     $Date: 2004/03/12 12:15:26 $
# $Revision: 1.7.2.1 $
# Description: Makefile for GASNet other 
# Copyright 2002, Dan Bonachea <bonachea@cs.berkeley.edu>
# Terms of use are as specified in license.txt

AUTOMAKE_OPTIONS = foreign 1.4

SUBDIRS =          \
        ammpi      \
	amudp

EXTRA_DIST =       \
	amxtests   \
	firehose   \
	perlstart

if USE_UDP_CONDUIT                           
  UDP_dir=$(top_builddir)/other/amudp
endif
if USE_MPI_CONDUIT                           
  MPI_dir=$(top_builddir)/other/ammpi
endif
testdirs= $(UDP_dir) $(MPI_dir)

tests: 
	@list='$(testdirs)'; for dir in $$list ; do \
	  $(MAKE) -C "$$dir" tests ;                \
	done

run-tests: 
	@list='$(testdirs)'; for dir in $$list ; do \
	  $(MAKE) -C "$$dir" run-tests ;            \
	done

all-local: Makefile-conduit.mak

.PHONY: force clean tests run-tests

force:
	

## Process this file with automake to produce Makefile.in	-*- makefile -*-
#  $Archive:: /Ti/AMMPI/Makefile                                         $
#     $Date: 2004/01/23 23:22:21 $
# $Revision: 1.4.4.1 $
# Description: Makefile for GASNet other 
# Copyright 2002, Dan Bonachea <bonachea@cs.berkeley.edu>
# Terms of use are as specified in license.txt

AUTOMAKE_OPTIONS = foreign 1.4

SUBDIRS =          \
        ammpi      \
	amudp

EXTRA_DIST =       \
	firehose   \
	perlstart


tests: force
if USE_UDP_CONDUIT                           
	@$(MAKE) -C $(top_builddir)/other/amudp tests
endif
if USE_MPI_CONDUIT                           
	@$(MAKE) -C $(top_builddir)/other/ammpi tests
endif

.PHONY: force clean tests

force:
	

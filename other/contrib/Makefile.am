## Process this file with automake to produce Makefile.in	-*- makefile -*-
#   $Source: /Users/kamil/work/gasnet-cvs2/gasnet/other/contrib/Makefile.am,v $
#     $Date: 2004/08/26 04:53:55 $
# $Revision: 1.3 $
# Description: Makefile for GASNet other/contrib
# Copyright 2002, Dan Bonachea <bonachea@cs.berkeley.edu>
# Terms of use are as specified in license.txt

AUTOMAKE_OPTIONS = foreign 1.4

SUBDIRS =         

EXTRA_DIST = gasnet_trace.pl

# Need a bin_DATA rule, but automake disallows that.  So, fake it.
mybindir = $(bindir)

bin_SCRIPTS = gasnet_trace gasnet_trace.pl
mybin_DATA = gasnet_trace.pl

builddir = $(top_builddir)/other/contrib

all-local: gasnet_trace $(builddir)/gasnet_trace.pl

$(top_builddir)/other/perlstart: $(top_srcdir)/other/perlstart.in
	@$(MAKE) -C $(top_builddir)/other perlstart

gasnet_trace: $(top_builddir)/other/perlstart $(builddir)/gasnet_trace.pl

$(builddir)/gasnet_trace.pl: force
	@if test "`cd $(top_builddir);@PWD_PROG@`" != "`cd $(top_srcdir);@PWD_PROG@`"; then \
            echo cp -f $(srcdir)/gasnet_trace.pl $@; \
            cp -f $(srcdir)/gasnet_trace.pl $@; \
         fi;

distclean-local:
	@if test "`cd $(top_builddir);@PWD_PROG@`" != "`cd $(top_srcdir);@PWD_PROG@`"; then \
            echo rm -f $(builddir)/gasnet_trace.pl; \
            rm -f $(builddir)/gasnet_trace.pl; \
         fi;

install-data-local:
	chmod -x $(DESTDIR)$(bindir)/gasnet_trace.pl


.PHONY: force

force:
	

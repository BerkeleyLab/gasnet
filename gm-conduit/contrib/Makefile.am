## Process this file with automake to produce Makefile.in       -*- makefile -*-
# $Header: /Users/kamil/work/gasnet-cvs2/gasnet/gm-conduit/contrib/Attic/Makefile.am,v 1.8 2003/11/09 04:05:53 phargrov Exp $
# Description: Makefile for GASNet gm conduit
# Copyright 2002, Christian Bell <csbell@cs.berkeley.edu>
# Copyright 2002, Dan Bonachea <bonachea@cs.berkeley.edu>
# Terms of use are as specified in license.txt

AUTOMAKE_OPTIONS = foreign 1.4

builddir = $(top_builddir)/gm-conduit/contrib
target = gasnetrun_gm

# Need a bin_DATA rule, but automake disallows that.  So, fake it.
mybindir = $(bindir)

# Make sure the perl script and shell wrapper are always current locally.
# This ensures we can run from the build directory if needed.
all-local: $(builddir)/$(target).pl $(target)
$(builddir)/$(target).pl: force
	@if test "`cd $(builddir);pwd`" != "`cd $(srcdir);pwd`"; then \
	    echo cp -f $(srcdir)/$(target).pl $(builddir); \
	    cp -f $(srcdir)/$(target).pl $(builddir); \
	 fi
distclean-local:
	@if test "`cd $(builddir);pwd`" != "`cd $(srcdir);pwd`"; then \
	    echo rm -f $(builddir)/$(target).pl; \
	    rm -f $(builddir)/$(target).pl; \
	 fi

# Ensure the .pl is distributed, even if not installed
EXTRA_DIST = $(target).pl

# Conditionally install the .pl and its wrapper
if USE_GM_CONDUIT
scripts = $(target) $(target).pl
data = $(target).pl
else
scripts =
data =
endif
bin_SCRIPTS = $(scripts)
mybin_DATA = $(data)

.PHONY: force
force:


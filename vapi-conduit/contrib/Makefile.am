## Process this file with automake to produce Makefile.in       -*- makefile -*-
# $Header: /Users/kamil/work/gasnet-cvs2/gasnet/vapi-conduit/contrib/Attic/Makefile.am,v 1.3 2003/11/09 05:01:48 phargrov Exp $
# Description: Makefile for GASNet VAPI spawner
# Terms of use are as specified in vapi-conduit/license.txt

AUTOMAKE_OPTIONS = foreign 1.4

builddir = $(top_builddir)/vapi-conduit/contrib
target = gasnetrun_vapi

# Need a bin_DATA rule, but automake disallows that.  So, fake it.
mybindir = $(bindir)

# Make sure the perl script and shell wrapper are always current locally.
# This ensures we can run from the build directory if needed.
all-local: $(target).pl $(target)
$(target).pl: $(top_srcdir)/mpi-conduit/contrib/gasnetrun_mpi.pl
	cp -f $< $@
distclean-local:
	rm -f $(target).pl

# Ensure the .pl is distributed, even if not installed
#EXTRA_DIST = $(target).pl
#  DON'T NEED THIS SINCE WE ARE COPYING FROM THE MPI-CONDUIT

# Conditionally install the .pl and its wrapper
if USE_VAPI_CONDUIT
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


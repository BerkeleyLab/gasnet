## Process this file with automake to produce Makefile.in       -*- makefile -*-
#   $Source: /Users/kamil/work/gasnet-cvs2/gasnet/mpi-conduit/contrib/Makefile.am,v $
#     $Date: 2004/08/30 06:57:58 $
# $Revision: 1.2.6.3 $
# Description: Makefile for GASNet MPI spawner
# Terms of use are as specified in license.txt

AUTOMAKE_OPTIONS = foreign 1.4

builddir = $(top_builddir)/mpi-conduit/contrib
target = gasnetrun_mpi

# Need a bin_DATA rule, but automake disallows that.  So, fake it.
mybindir = $(bindir)

# Make sure the perl script and shell wrapper are always current locally.
# This ensures we can run from the build directory if needed.
$(top_builddir)/other/perlstart: $(top_srcdir)/other/perlstart.in
	@$(MAKE) -C $(top_builddir)/other perlstart
$(target): $(top_builddir)/other/perlstart
all-local: $(builddir)/$(target).pl $(target)
$(builddir)/$(target).pl: force
	@if test "`cd $(builddir);@PWD_PROG@`" != "`cd $(srcdir);@PWD_PROG@`"; then \
	    echo cp -f $(srcdir)/$(target).pl $(builddir); \
	    cp -f $(srcdir)/$(target).pl $(builddir); \
	 fi
distclean-local:
	@if test "`cd $(builddir);@PWD_PROG@`" != "`cd $(srcdir);@PWD_PROG@`"; then \
	    echo rm -f $(builddir)/$(target).pl; \
	    rm -f $(builddir)/$(target).pl; \
	fi

# Ensure the .pl is distributed, even if not installed
EXTRA_DIST = $(target).pl

# Conditionally install the .pl and its wrapper
if USE_MPI_CONDUIT
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


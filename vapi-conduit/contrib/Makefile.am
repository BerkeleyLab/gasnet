## Process this file with automake to produce Makefile.in       -*- makefile -*-
#   $Source: /Users/kamil/work/gasnet-cvs2/gasnet/vapi-conduit/contrib/Attic/Makefile.am,v $
#     $Date: 2005/04/17 15:44:42 $
# $Revision: 1.7.8.1 $
# Description: Makefile for GASNet VAPI spawner
# Terms of use are as specified in vapi-conduit/license.txt

AUTOMAKE_OPTIONS = foreign 1.4

builddir = $(top_builddir)/vapi-conduit/contrib
target = gasnetrun_vapi

# Need a bin_DATA rule, but automake disallows that.  So, fake it.
mybindir = $(bindir)

# Use of the MPI spawning support is optional
if VAPI_BOOTSTRAP_MPI
mpi_target_pl = $(target)-mpi.pl
else
mpi_target_pl =
endif

# Make sure the perl script and shell wrapper are always current locally.
# This ensures we can run from the build directory if needed.
$(top_builddir)/other/perlstart: $(top_srcdir)/other/perlstart.in
	@$(MAKE) -C $(top_builddir)/other perlstart
$(target): $(top_builddir)/other/perlstart
all-local: $(builddir)/$(target).pl $(target) $(mpi_target_pl)
$(builddir)/$(target).pl: force
	@if test "`cd $(builddir);@PWD_PROG@`" != "`cd $(srcdir);@PWD_PROG@`"; then \
	    echo cp -f $(srcdir)/$(target).pl $(builddir); \
	    cp -f $(srcdir)/$(target).pl $(builddir); \
	fi
$(target)-mpi.pl: $(top_srcdir)/mpi-conduit/contrib/gasnetrun_mpi.pl
	cp -f $< $@
distclean-local:
	rm -f $(target)-mpi.pl
	@if test "`cd $(builddir);@PWD_PROG@`" != "`cd $(srcdir);@PWD_PROG@`"; then \
	    echo rm -f $(builddir)/$(target).pl; \
	    rm -f $(builddir)/$(target).pl; \
	fi

# Ensure the .pl is distributed, even if not installed
EXTRA_DIST = $(target).pl

# Conditionally install the .pl and its wrapper
if USE_VAPI_CONDUIT
scripts = $(target)
data = $(target).pl $(mpi_target_pl)
else
scripts =
data =
endif
bin_SCRIPTS = $(scripts)
mybin_DATA = $(data)

.PHONY: force
force:


## Process this file with automake to produce Makefile.in       -*- makefile -*-
# $Header: /Users/kamil/work/gasnet-cvs2/gasnet/sci-conduit/contrib/Attic/Makefile.am,v 1.2 2004/07/05 22:42:25 bonachea Exp $
# Description: Makefile for GASNet sci conduit
# Copyright 2002, Dan Bonachea <bonachea@cs.berkeley.edu>
# Terms of use are as specified in license.txt

AUTOMAKE_OPTIONS = foreign 1.4

EXTRA_DIST = \
	gasnetrun_sci.ksh \
	machines.sci

# Need a bin_DATA rule, but automake disallows that.  So, fake it.
mybindir = $(bindir)

# Conditionally install the run script
if USE_SCI_CONDUIT
scripts = gasnetrun_sci
data = machines.sci
else
scripts =
data =
endif
bin_SCRIPTS = $(scripts)
mybin_DATA = $(data)

.PHONY: force
force:


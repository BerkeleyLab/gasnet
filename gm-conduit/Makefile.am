## Process this file with automake to produce Makefile.in	-*- makefile -*-
#   $Source: /Users/kamil/work/gasnet-cvs2/gasnet/gm-conduit/Attic/Makefile.am,v $
#     $Date: 2004/10/09 14:54:02 $
# $Revision: 1.46 $
# Description: Makefile for GASNet gm conduit
# Copyright 2002, Dan Bonachea <bonachea@cs.berkeley.edu>
# Terms of use are as specified in license.txt

AUTOMAKE_OPTIONS = foreign 1.4

CONDUIT_NAME=gm

# any conduit-specific subdirectories containing Makefile.am's
SUBDIRS = contrib

# complete list of files in the conduit directory
# include all headers, documentation, etc. 
# and any subdirectories not containing Makefile.am's
CONDUIT_FILELIST =               \
      gasnet_core.c              \
      gasnet_core.h              \
      gasnet_core_conf.c         \
      gasnet_core_fwd.h          \
      gasnet_core_help.h         \
      gasnet_core_internal.h     \
      gasnet_core_receive.c      \
      gasnet_extended.c          \
      gasnet_extended_firehose.c \
      gasnet_extended_fwd.h      \
      gasnet_extended_internal.h \
      gasnet_extended_op.c       \
      gasnet_extended_ref.c      \
      firehose_fwd.h		 \
      license.txt                

# list of conduit core and extended .c source files 
# to be compiled into libgasnet on the compiler command line
fh_srcdir = $(top_srcdir)/other/firehose
CONDUIT_SOURCELIST =                       \
      $(srcdir)/gasnet_core.c              \
      $(srcdir)/gasnet_core_receive.c      \
      $(srcdir)/gasnet_core_conf.c         \
      $(srcdir)/gasnet_extended.c          \
      $(srcdir)/gasnet_extended_op.c       \
      $(srcdir)/gasnet_extended_ref.c      \
      $(srcdir)/gasnet_extended_firehose.c \
      $(fh_srcdir)/firehose.c              \
      $(fh_srcdir)/firehose_hash.c         \
      $(fh_srcdir)/firehose_page.c

# additional -I or -D directives needed by this specific conduit
# other than the standard GASNet includes and flags
CONDUIT_EXTRALIBCFLAGS = @GM_NO_ALIAS@ -I$(fh_srcdir) -I@GM_INCLUDE@

# additional conduit header files to install from external, non-standard directories
CONDUIT_EXTRAHEADERS = 

# additional file dependencies not mentioned elsewhere 
# that should force libgasnet rebuild on update
CONDUIT_EXTRADEPS = $(fh_srcdir)/*.h

# additional object files to be included in libgasnet that need to be compiled 
# using a special, conduit-specific command. These should also be included as
# forced targets in this file, and should probably use LIBINCLUDES/LIBDEFINES
CONDUIT_SPECIAL_OBJS = 

# the default job spawn command to be used for "make run-tests"
# The following substitutions are performed:
#   %P = program executable name
#   %N = requested node count
#   %A = program arguments
CONDUIT_RUNCMD = @TOP_BUILDDIR@/gm-conduit/contrib/gasnetrun_gm -np %N %P %A

# conduit-specific tests in ../tests directory
CONDUIT_TESTS =

# -------- Do not modify anything below this line --------

if HAVE_PTHREAD
libgasnet_gm_par_a_SOURCES =
libgasnet_gm_parsync_a_SOURCES =
threaded_libraries = libgasnet-gm-par.a libgasnet-gm-parsync.a
endif
libgasnet_gm_seq_a_SOURCES =
libraries = libgasnet-gm-seq.a $(threaded_libraries)

include $(top_builddir)/other/Makefile-conduit.mak

libgasnet-gm-seq.a: $(libgasnet_dependencies)
	@$(MAKE) do-libgasnet-seq

libgasnet-gm-par.a: $(libgasnet_dependencies)
	@$(MAKE) do-libgasnet-par

libgasnet-gm-parsync.a: $(libgasnet_dependencies)
	@$(MAKE) do-libgasnet-parsync

if USE_GM_CONDUIT  
lib_LIBRARIES = $(libraries) 
all-local: $(lib_LIBRARIES) $(makefile_fragments)
clean-local: do-clean-local
install-data-local: do-install-data-local
uninstall-local: do-uninstall-local
else
$(top_srcdir)/gasnet.h: do-error
endif



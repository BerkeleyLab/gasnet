## Process this file with automake to produce Makefile.in	-*- makefile -*-
# $Header: /Users/kamil/work/gasnet-cvs2/gasnet/vapi-conduit/Attic/Makefile.am,v 1.19.2.1 2004/03/12 12:15:34 csbell Exp $
# Description: Makefile for GASNet vapi conduit
# Copyright 2002, Dan Bonachea <bonachea@cs.berkeley.edu>
# Terms of use are as specified in license.txt

AUTOMAKE_OPTIONS = foreign 1.4

CONDUIT_NAME=vapi

if GASNET_SEGMENT_FAST
else
# only use firehose for segment large/everything
fh_srcdir = $(top_srcdir)/other/firehose
fh_include = -I$(fh_srcdir)
fh_deps = $(fh_srcdir)/*.h
fh_srcs =                          \
      $(srcdir)/gasnet_firehose.c  \
      $(fh_srcdir)/firehose.c      \
      $(fh_srcdir)/firehose_hash.c \
      $(fh_srcdir)/firehose_region.c
endif

# any conduit-specific subdirectories containing Makefile.am's
SUBDIRS = contrib

# complete list of files in the conduit directory
# include all headers, documentation, etc. 
# and any subdirectories not containing Makefile.am's
CONDUIT_FILELIST =               \
      gasnet_core.c              \
      gasnet_core_sndrcv.c       \
      gasnet_core.h              \
      gasnet_core_fwd.h          \
      gasnet_core_help.h         \
      gasnet_core_internal.h     \
      gasnet_extended.c          \
      gasnet_extended_fwd.h      \
      gasnet_extended_internal.h \
      gasnet_bootstrap_mpi.c     \
      gasnet_firehose.c          \
      firehose_fwd.h             \
      license.txt                \
      README

# list of conduit core and extended .c source files 
# to be compiled into libgasnet on the compiler command line
CONDUIT_SOURCELIST =                 \
      $(srcdir)/gasnet_core.c        \
      $(srcdir)/gasnet_core_sndrcv.c \
      $(srcdir)/gasnet_extended.c    \
      $(fh_srcs)

if VAPI_BOOTSTRAP_MPI
# special VAPI-conduit-specific backdoor hack
CONDUIT_MPISOURCELIST = $(srcdir)/gasnet_bootstrap_mpi.c
endif

# additional -I or -D directives needed by this specific conduit
# other than the standard GASNet includes and flags
CONDUIT_EXTRALIBCFLAGS = -I@VAPI_INCLUDE@ $(fh_include)

# additional conduit header files to install from external, non-standard directories
CONDUIT_EXTRAHEADERS = 

# additional file dependencies not mentioned elsewhere 
# that should force libgasnet rebuild on update
CONDUIT_EXTRADEPS = $(fh_deps)

# additional object files to be included in libgasnet that need to be compiled 
# using a special, conduit-specific command. These should also be included as
# forced targets in this file, and should probably use LIBINCLUDES/LIBDEFINES
CONDUIT_SPECIAL_OBJS=gasnet_bootstrap_mpi.o

gasnet_bootstrap_mpi.o: gasnet_bootstrap_mpi.c force
	@MPI_CC@ @MPI_CFLAGS@ $(LIBDEFINES) $(CONDUIT_EXTRALIBCFLAGS) $(LIBINCLUDES) -c $<

# the default job spawn command to be used for "make run-tests"
# The following substitutions are performed:
#   %P = program executable name
#   %N = requested node count
#   %A = program arguments
CONDUIT_RUNCMD = @TOP_BUILDDIR@/vapi-conduit/contrib/gasnetrun_vapi -np %N %P %A

# -------- Do not modify anything below this line --------

if HAVE_PTHREAD
libgasnet_vapi_par_a_SOURCES =
libgasnet_vapi_parsync_a_SOURCES =
threaded_libraries = libgasnet-vapi-par.a libgasnet-vapi-parsync.a
endif
libgasnet_vapi_seq_a_SOURCES =
libraries = libgasnet-vapi-seq.a $(threaded_libraries)

include $(top_builddir)/other/Makefile-conduit.mak

libgasnet-vapi-seq.a: $(libgasnet_dependencies)
	@$(MAKE) do-libgasnet-seq

libgasnet-vapi-par.a: $(libgasnet_dependencies)
	@$(MAKE) do-libgasnet-par

libgasnet-vapi-parsync.a: $(libgasnet_dependencies)
	@$(MAKE) do-libgasnet-parsync

if USE_VAPI_CONDUIT  
lib_LIBRARIES = $(libraries) 
all: do-all
clean-local: do-clean-local
install-data-local: do-install-data-local
uninstall-local: do-uninstall-local
else
$(top_srcdir)/gasnet.h: do-error
endif



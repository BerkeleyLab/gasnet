## Process this file with automake to produce Makefile.in	-*- makefile -*-
# $Header: /Users/kamil/work/gasnet-cvs2/gasnet/shmem-conduit/Makefile.am,v 1.1.2.7 2004/03/11 11:03:53 bonachea Exp $
# Description: Makefile for GASNet shmem conduit
# Copyright 2002, Dan Bonachea <bonachea@cs.berkeley.edu>
# Terms of use are as specified in license.txt

AUTOMAKE_OPTIONS = foreign 1.4

CONDUIT_NAME=shmem

# any conduit-specific subdirectories containing Makefile.am's
SUBDIRS = 

# complete list of files in the conduit directory
# include all headers, documentation, etc. 
# and any subdirectories not containing Makefile.am's
CONDUIT_FILELIST =           \
      gasnet_core.c          \
      gasnet_core.h          \
      gasnet_core_fwd.h      \
      gasnet_core_help.h     \
      gasnet_core_internal.h \
      gasnet_extended.c	     \
      README

# list of conduit core and extended .c source files 
# to be compiled into libgasnet on the compiler command line
CONDUIT_SOURCELIST =          \
      $(srcdir)/gasnet_core.c \
      $(srcdir)/gasnet_extended.c 

# additional -I or -D directives needed by this specific conduit
# other than the standard GASNet includes and flags
CONDUIT_EXTRALIBCFLAGS = @SHMEM_FLAGS@

# additional conduit header files to install from external, non-standard directories
CONDUIT_EXTRAHEADERS = 

# additional file dependencies not mentioned elsewhere 
# that should force libgasnet rebuild on update
CONDUIT_EXTRADEPS = 

# -------- Do not modify anything below this line --------

# TODO: shmem-conduit currently does not support pthreads
do-pthreads-error:
	@echo "ERROR: shmem-conduit currently does not support pthreads"
	@exit 1

CONDUIT_PAR_HOOK = do-pthreads-error
CONDUIT_PARSYNC_HOOK = do-pthreads-error

threaded_libraries=
#if HAVE_PTHREAD
#libgasnet_shmem_par_a_SOURCES =
#libgasnet_shmem_parsync_a_SOURCES =
#threaded_libraries = libgasnet-shmem-par.a libgasnet-shmem-parsync.a
#endif
libraries = libgasnet-shmem-seq.a $(threaded_libraries)
libgasnet_shmem_seq_a_SOURCES =

include $(top_builddir)/other/Makefile-conduit.mak

libgasnet-shmem-seq.a: $(libgasnet_dependencies)
	@$(MAKE) do-libgasnet-seq

libgasnet-shmem-par.a: $(libgasnet_dependencies)
	@$(MAKE) do-libgasnet-par

libgasnet-shmem-parsync.a: $(libgasnet_dependencies)
	@$(MAKE) do-libgasnet-parsync

if USE_SHMEM_CONDUIT  
lib_LIBRARIES = $(libraries) 
all: do-all
clean-local: do-clean-local
install-data-local: do-install-data-local
uninstall-local: do-uninstall-local
else
$(top_srcdir)/gasnet.h: do-error
endif



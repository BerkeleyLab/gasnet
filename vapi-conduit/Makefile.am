## Process this file with automake to produce Makefile.in	-*- makefile -*-
# $Header: /Users/kamil/work/gasnet-cvs2/gasnet/vapi-conduit/Attic/Makefile.am,v 1.5 2003/08/25 09:52:39 bonachea Exp $
# Description: Makefile for GASNet vapi conduit
# Copyright 2002, Dan Bonachea <bonachea@cs.berkeley.edu>
# Terms of use are as specified in license.txt

AUTOMAKE_OPTIONS = foreign 1.4

thread_defines = @GASNET_THREAD_DEFINES@

# XXX: As long as we use MPI for bootstrapping
CC = @MPI_CC@

INCLUDES = -I$(srcdir) -I$(top_srcdir) -I$(top_builddir) -I$(top_srcdir)/extended-ref -I@VAPI_INCLUDE@
	   

filelist =                   \
      gasnet_core.c          \
      gasnet_core_sndrcv.c   \
      gasnet_bootstrap_mpi.c \
      gasnet_core.h          \
      gasnet_core_fwd.h      \
      gasnet_core_help.h     \
      gasnet_core_internal.h \
      gasnet_extended.c      \
      gasnet_extended_fwd.h  \
      gasnet_extended_internal.h

othersourcefiles =                             \
      $(top_srcdir)/gasnet_internal.c

EXTRA_DIST = $(filelist) README license.txt

libgasnet_sources =                    \
      $(srcdir)/gasnet_core.c          \
      $(srcdir)/gasnet_core_sndrcv.c   \
      $(srcdir)/gasnet_bootstrap_mpi.c \
      $(srcdir)/gasnet_extended.c \
      $(othersourcefiles)
libgasnet_dependencies = $(filelist) $(libgasnet_sources) \
                         $(top_srcdir)/*.[ch]             \
                         $(top_srcdir)/extended-ref/*.h       
libgasnet_objects = gasnet_core.o gasnet_core_sndrcv.o gasnet_bootstrap_mpi.o gasnet_internal.o gasnet_extended.o

if HAVE_PTHREAD
threaded_libraries = libgasnet-vapi-par.a libgasnet-vapi-parsync.a
libgasnet_vapi_par_a_SOURCES =
libgasnet_vapi_parsync_a_SOURCES =
endif
libraries = libgasnet-vapi-seq.a $(threaded_libraries)
libgasnet_vapi_seq_a_SOURCES =

VPATH=$(srcdir)

LIBCFLAGS = @CFLAGS@ @MISC_CFLAGS@ @BUILDCONFIG_DEFINES@ @GASNET_MACHINE_DEFINES@ $(INCLUDES)

libgasnet-vapi-seq.a: $(libgasnet_dependencies)
	$(CC) $(LIBCFLAGS) -DGASNET_SEQ $(thread_defines) -c $(libgasnet_sources)
	$(AR) cru $@ $(libgasnet_objects)
	$(RANLIB) $@
	rm -f $(libgasnet_objects)

libgasnet-vapi-par.a: $(libgasnet_dependencies)
	$(CC) $(LIBCFLAGS) -DGASNET_PAR $(thread_defines) -c $(libgasnet_sources)
	$(AR) cru $@ $(libgasnet_objects)
	$(RANLIB) $@
	rm -f $(libgasnet_objects)

libgasnet-vapi-parsync.a: $(libgasnet_dependencies)
	$(CC) $(LIBCFLAGS) -DGASNET_PARSYNC $(thread_defines) -c $(libgasnet_sources)
	$(AR) cru $@ $(libgasnet_objects)
	$(RANLIB) $@
	rm -f $(libgasnet_objects)

.PHONY: all libs clean-local install-data-local uninstall-local tests force tests-seq tests-par tests-parsync


libs: $(libraries)
	
if USE_VAPI_CONDUIT                           
lib_LIBRARIES = $(libraries)

all: libs $(makefile_fragments)
	
clean-local: 
	rm -f $(libgasnet_objects) $(libraries) *.o core
	@$(MAKE) -f $(top_builddir)/tests/Makefile clean     \
	  srcdir="$(top_srcdir)/tests"			     \
	  configfile="/dev/null"

headers = $(srcdir)/*.h $(top_srcdir)/extended-ref/*.h 
makefile_fragments = vapi-seq.mak vapi-parsync.mak vapi-par.mak

install-data-local: $(headers)
	$(mkinstalldirs) $(includedir)/vapi-conduit
	@list='$(headers)'; for p in $$list; do \
	  if test -f $$p; then \
	    filename=`basename $$p`; \
	    echo " $(INSTALL_DATA) $$p $(includedir)/vapi-conduit/$$filename"; \
	    $(INSTALL_DATA) $$p $(includedir)/vapi-conduit/$$filename; \
	  else :; fi; \
	done
	@list='$(makefile_fragments)'; for p in $$list; do \
	  if test -f $$p; then \
	    filename=`basename $$p`; \
	    echo " grep -v "###NOINSTALL###" $$p > $(includedir)/vapi-conduit/$$filename"; \
	    grep -v "###NOINSTALL###" $$p > $(includedir)/vapi-conduit/$$filename; \
	  else :; fi; \
	done

uninstall-local:
	rm -f $(includedir)/vapi-conduit/*.h $(includedir)/vapi-conduit/*.mak

endif

tests:
	@echo You must specify one of tests-seq, tests-par or tests-parsync
	@exit 1

tests-seq: force libgasnet-vapi-seq.a vapi-seq.mak
	@$(MAKE) -f $(top_builddir)/tests/Makefile tests-seq \
	  srcdir="$(top_srcdir)/tests"			     \
	  configfile="vapi-seq.mak"

tests-parsync: force libgasnet-vapi-parsync.a vapi-parsync.mak
	@$(MAKE) -f $(top_builddir)/tests/Makefile tests-parsync \
	  srcdir="$(top_srcdir)/tests"			     \
	  configfile="vapi-parsync.mak"

tests-par: force libgasnet-vapi-par.a vapi-par.mak
	@$(MAKE) -f $(top_builddir)/tests/Makefile tests-par \
	  srcdir="$(top_srcdir)/tests"			     \
	  configfile="vapi-par.mak"

force:
	



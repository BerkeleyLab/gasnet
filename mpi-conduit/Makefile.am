## Process this file with automake to produce Makefile.in	-*- makefile -*-
# $Header: /Users/kamil/work/gasnet-cvs2/gasnet/mpi-conduit/Makefile.am,v 1.19 2004/01/05 05:01:17 bonachea Exp $
# Description: Makefile for GASNet MPI conduit
# Copyright 2002, Dan Bonachea <bonachea@cs.berkeley.edu>
# Terms of use are as specified in license.txt

AUTOMAKE_OPTIONS = foreign 1.4

thread_defines = @GASNET_THREAD_DEFINES@

INCLUDES = -I$(srcdir) -I$(top_srcdir) -I$(top_builddir)        \
           -I$(top_srcdir)/extended-ref -I$(top_srcdir)/other/ammpi 
	   

filelist =                   \
      gasnet_core.c          \
      gasnet_core.h          \
      gasnet_core_fwd.h      \
      gasnet_core_help.h     \
      gasnet_core_internal.h 

othersourcefiles =                             \
      $(top_srcdir)/gasnet_internal.c          \
      $(top_srcdir)/extended-ref/gasnet_extended.c 

EXTRA_DIST = $(filelist) \
	README

SUBDIRS = contrib

libgasnet_sources = $(srcdir)/gasnet_core.c     \
		    $(othersourcefiles)

libgasnet_dependencies = $(filelist) $(libgasnet_sources) \
                         $(top_srcdir)/*.[ch]             \
                         $(top_srcdir)/extended-ref/gasnet_extended_refbarrier.c \
                         $(top_srcdir)/extended-ref/*.h   \
			 $(top_srcdir)/other/ammpi/*.h

libgasnet_objects = gasnet_core.o gasnet_internal.o gasnet_extended.o

if HAVE_PTHREAD
threaded_libraries = libgasnet-mpi-par.a libgasnet-mpi-parsync.a
libgasnet_mpi_par_a_SOURCES =
libgasnet_mpi_parsync_a_SOURCES =
endif
libraries = libgasnet-mpi-seq.a $(threaded_libraries)
libgasnet_mpi_seq_a_SOURCES =

VPATH=$(srcdir)

LIBCFLAGS = @CFLAGS@ @MISC_CFLAGS@ @BUILDCONFIG_DEFINES@ @GASNET_MACHINE_DEFINES@ $(INCLUDES)

libgasnet-mpi-seq.a: $(libgasnet_dependencies)
	$(CC) $(LIBCFLAGS) -DGASNET_SEQ -c $(libgasnet_sources)
	$(AR) cru $@ $(libgasnet_objects)
	$(RANLIB) $@
	rm -f $(libgasnet_objects)

libgasnet-mpi-par.a: $(libgasnet_dependencies)
	$(CC) $(LIBCFLAGS) -DGASNET_PAR $(thread_defines) -c $(libgasnet_sources)
	$(AR) cru $@ $(libgasnet_objects)
	$(RANLIB) $@
	rm -f $(libgasnet_objects)

libgasnet-mpi-parsync.a: $(libgasnet_dependencies)
	$(CC) $(LIBCFLAGS) -DGASNET_PARSYNC $(thread_defines) -c $(libgasnet_sources)
	$(AR) cru $@ $(libgasnet_objects)
	$(RANLIB) $@
	rm -f $(libgasnet_objects)

.PHONY: all libs clean-local install-data-local uninstall-local tests force tests-seq tests-par tests-parsync


libs: $(libraries)
	
if USE_MPI_CONDUIT                           
lib_LIBRARIES = $(libraries)

all: libs $(makefile_fragments)
	
clean-local: 
	rm -f $(libgasnet_objects) $(libraries) *.o core
	@$(MAKE) -f $(top_builddir)/tests/Makefile clean     \
	  srcdir="$(top_srcdir)/tests"			     \
	  configfile="/dev/null"

headers = $(srcdir)/*.h $(top_srcdir)/extended-ref/*.h $(top_srcdir)/other/ammpi/ammpi*.h 
makefile_fragments = mpi-seq.mak mpi-parsync.mak mpi-par.mak

install-data-local: $(headers)
	$(mkinstalldirs) $(DESTDIR)$(includedir)/mpi-conduit
	@list='$(headers)'; for p in $$list; do \
	  if test -f $$p; then \
	    filename=`basename $$p`; \
	    echo " $(INSTALL_DATA) $$p $(DESTDIR)$(includedir)/mpi-conduit/$$filename"; \
	    $(INSTALL_DATA) $$p $(DESTDIR)$(includedir)/mpi-conduit/$$filename; \
	  else :; fi; \
	done
	@list='$(makefile_fragments)'; for p in $$list; do \
	  if test -f $$p; then \
	    filename=`basename $$p`; \
	    echo "@GASNET_INSTALL_CMD@ < $$p > $(DESTDIR)$(includedir)/mpi-conduit/$$filename"; \
	    @GASNET_INSTALL_CMD@ < $$p > $(DESTDIR)$(includedir)/mpi-conduit/$$filename; \
	  else :; fi; \
	done


uninstall-local:
	rm -f $(DESTDIR)$(includedir)/mpi-conduit/*.h $(DESTDIR)$(includedir)/mpi-conduit/*.mak

else
$(top_srcdir)/gasnet.h: force
	@echo "ERROR: mpi-conduit support was not detected at configure time"
	@echo "       try re-running configure with --enable-mpi"
	@exit 1
endif

tests:
	@echo You must specify one of tests-seq, tests-par or tests-parsync
	@exit 1

tests-seq: force libgasnet-mpi-seq.a mpi-seq.mak
	@$(MAKE) -f $(top_builddir)/tests/Makefile tests-seq \
	  srcdir="$(top_srcdir)/tests"			     \
	  configfile="mpi-seq.mak"

tests-parsync: force libgasnet-mpi-parsync.a mpi-parsync.mak
	@$(MAKE) -f $(top_builddir)/tests/Makefile tests-parsync \
	  srcdir="$(top_srcdir)/tests"			     \
	  configfile="mpi-parsync.mak"

tests-par: force libgasnet-mpi-par.a mpi-par.mak
	@$(MAKE) -f $(top_builddir)/tests/Makefile tests-par \
	  srcdir="$(top_srcdir)/tests"			     \
	  configfile="mpi-par.mak"

force:
	



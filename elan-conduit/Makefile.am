## Process this file with automake to produce Makefile.in	-*- makefile -*-
# $Header: /Users/kamil/work/gasnet-cvs2/gasnet/elan-conduit/Attic/Makefile.am,v 1.18 2004/01/05 05:01:11 bonachea Exp $
# Description: Makefile for GASNet elan conduit
# Copyright 2002, Dan Bonachea <bonachea@cs.berkeley.edu>
# Terms of use are as specified in license.txt 

AUTOMAKE_OPTIONS = foreign 1.4

thread_defines = @GASNET_THREAD_DEFINES@

INCLUDES = -I$(srcdir) -I$(top_srcdir) -I$(top_builddir) \
           -I$(top_srcdir)/extended-ref                  \
	   -I@ELAN_INCLUDE@
	   

filelist =                       \
      gasnet_core.c              \
      gasnet_core.h              \
      gasnet_core_fwd.h          \
      gasnet_core_help.h         \
      gasnet_core_internal.h     \
      gasnet_core_dump.c         \
      gasnet_core_reqrep.c       \
      gasnet_extended.c          \
      gasnet_extended_fwd.h      \
      gasnet_extended_internal.h

othersourcefiles =                             \
      $(top_srcdir)/gasnet_internal.c          

EXTRA_DIST = $(filelist) \
	README

libgasnet_sources =                  \
      $(srcdir)/gasnet_core.c        \
      $(srcdir)/gasnet_core_dump.c   \
      $(srcdir)/gasnet_core_reqrep.c \
      $(srcdir)/gasnet_extended.c    \
      $(othersourcefiles)

libgasnet_dependencies = $(filelist) $(libgasnet_sources) \
                         $(top_srcdir)/*.[ch]             \
                         $(top_srcdir)/extended-ref/gasnet_extended_refbarrier.c \
                         $(top_srcdir)/extended-ref/*.h      
			  
libgasnet_objects =      \
      gasnet_core.o      \
      gasnet_internal.o  \
      gasnet_extended.o  \
      gasnet_core_dump.o \
      gasnet_core_reqrep.o

if HAVE_PTHREAD
threaded_libraries = libgasnet-elan-par.a libgasnet-elan-parsync.a
libgasnet_elan_par_a_SOURCES =
libgasnet_elan_parsync_a_SOURCES =
endif
libraries = libgasnet-elan-seq.a $(threaded_libraries)
libgasnet_elan_seq_a_SOURCES =

VPATH=$(srcdir)

LIBCFLAGS = @CFLAGS@ @MISC_CFLAGS@ @BUILDCONFIG_DEFINES@ @GASNET_MACHINE_DEFINES@ $(INCLUDES) @ELAN_FLAGS@

libgasnet-elan-seq.a: $(libgasnet_dependencies)
	$(CC) $(LIBCFLAGS) -DGASNET_SEQ -c $(libgasnet_sources)
	$(AR) cru $@ $(libgasnet_objects)
	$(RANLIB) $@
	rm -f $(libgasnet_objects)

libgasnet-elan-par.a: $(libgasnet_dependencies)
	$(CC) $(LIBCFLAGS) -DGASNET_PAR $(thread_defines) -c $(libgasnet_sources)
	$(AR) cru $@ $(libgasnet_objects)
	$(RANLIB) $@
	rm -f $(libgasnet_objects)

libgasnet-elan-parsync.a: $(libgasnet_dependencies)	
	$(CC) $(LIBCFLAGS) -DGASNET_PARSYNC $(thread_defines) -c $(libgasnet_sources)
	$(AR) cru $@ $(libgasnet_objects)
	$(RANLIB) $@
	rm -f $(libgasnet_objects)

.PHONY: all libs clean-local install-data-local uninstall-local tests force tests-seq tests-par tests-parsync


libs: $(libraries)
	
if USE_ELAN_CONDUIT                           
lib_LIBRARIES = $(libraries)

all: libs $(makefile_fragments)
	
clean-local: 
	rm -f $(libgasnet_objects) $(libraries) *.o core
	@$(MAKE) -f $(top_builddir)/tests/Makefile clean     \
	  srcdir="$(top_srcdir)/tests"			     \
	  configfile="/dev/null"

headers = $(srcdir)/*.h                             \
  $(top_srcdir)/extended-ref/gasnet_extended.h      \
  $(top_srcdir)/extended-ref/gasnet_extended_help.h

makefile_fragments = elan-seq.mak elan-parsync.mak elan-par.mak

install-data-local: $(headers)
	$(mkinstalldirs) $(DESTDIR)$(includedir)/elan-conduit
	@list='$(headers)'; for p in $$list; do \
	  if test -f $$p; then \
	    filename=`basename $$p`; \
	    echo " $(INSTALL_DATA) $$p $(DESTDIR)$(includedir)/elan-conduit/$$filename"; \
	    $(INSTALL_DATA) $$p $(DESTDIR)$(includedir)/elan-conduit/$$filename; \
	  else :; fi; \
	done
	@list='$(makefile_fragments)'; for p in $$list; do \
	  if test -f $$p; then \
	    filename=`basename $$p`; \
	    echo "@GASNET_INSTALL_CMD@ < $$p > $(DESTDIR)$(includedir)/elan-conduit/$$filename"; \
	    @GASNET_INSTALL_CMD@ < $$p > $(DESTDIR)$(includedir)/elan-conduit/$$filename; \
	  else :; fi; \
	done

uninstall-local:
	rm -f $(DESTDIR)$(includedir)/elan-conduit/*.h $(DESTDIR)$(includedir)/elan-conduit/*.mak

else
$(top_srcdir)/gasnet.h: force
	@echo "ERROR: elan-conduit support was not detected at configure time"
	@echo "       try re-running configure with --enable-elan"
	@exit 1
endif

tests:
	@echo You must specify one of tests-seq, tests-par or tests-parsync
	@exit 1

tests-seq: force libgasnet-elan-seq.a elan-seq.mak
	@$(MAKE) -f $(top_builddir)/tests/Makefile tests-seq \
	  srcdir="$(top_srcdir)/tests"			     \
	  configfile="elan-seq.mak"

tests-parsync: force libgasnet-elan-parsync.a elan-parsync.mak
	@$(MAKE) -f $(top_builddir)/tests/Makefile tests-parsync \
	  srcdir="$(top_srcdir)/tests"			     \
	  configfile="elan-parsync.mak"

tests-par: force libgasnet-elan-par.a elan-par.mak
	@$(MAKE) -f $(top_builddir)/tests/Makefile tests-par \
	  srcdir="$(top_srcdir)/tests"			     \
	  configfile="elan-par.mak"

force:
	



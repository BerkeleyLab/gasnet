## Process this file with automake to produce Makefile.in	-*- makefile -*-
# $Header: /Users/kamil/work/gasnet-cvs2/gasnet/smp-conduit/Makefile.am,v 1.8 2004/01/05 05:01:21 bonachea Exp $
# Description: Makefile for GASNet smp conduit
# Copyright 2002, Dan Bonachea <bonachea@cs.berkeley.edu>
# Terms of use are as specified in license.txt

AUTOMAKE_OPTIONS = foreign 1.4

thread_defines = @GASNET_THREAD_DEFINES@

INCLUDES = -I$(srcdir) -I$(top_srcdir) -I$(top_builddir)        \
           -I$(top_srcdir)/extended-ref  
	   

filelist =                   \
      gasnet_core.c          \
      gasnet_core.h          \
      gasnet_core_fwd.h      \
      gasnet_core_help.h     \
      gasnet_core_internal.h 

othersourcefiles =                             \
      $(top_srcdir)/gasnet_internal.c          \
      $(top_srcdir)/extended-ref/gasnet_extended.c 

### template users: Delete the README.template line below and the README.template file 
EXTRA_DIST = $(filelist) \
	README

libgasnet_sources = $(srcdir)/gasnet_core.c $(othersourcefiles)
libgasnet_dependencies = $(filelist) $(libgasnet_sources) \
                         $(top_srcdir)/*.[ch]             \
                         $(top_srcdir)/extended-ref/gasnet_extended_refbarrier.c \
                         $(top_srcdir)/extended-ref/*.h       
libgasnet_objects = gasnet_core.o gasnet_internal.o gasnet_extended.o

if HAVE_PTHREAD
threaded_libraries = libgasnet-smp-par.a libgasnet-smp-parsync.a
libgasnet_smp_par_a_SOURCES =
libgasnet_smp_parsync_a_SOURCES =
endif
libraries = libgasnet-smp-seq.a $(threaded_libraries)
libgasnet_smp_seq_a_SOURCES =

VPATH=$(srcdir)

LIBCFLAGS = @CFLAGS@ @MISC_CFLAGS@ @BUILDCONFIG_DEFINES@ @GASNET_MACHINE_DEFINES@ $(INCLUDES)

libgasnet-smp-seq.a: $(libgasnet_dependencies)
	$(CC) $(LIBCFLAGS) -DGASNET_SEQ -c $(libgasnet_sources)
	$(AR) cru $@ $(libgasnet_objects)
	$(RANLIB) $@
	rm -f $(libgasnet_objects)

libgasnet-smp-par.a: $(libgasnet_dependencies)
	$(CC) $(LIBCFLAGS) -DGASNET_PAR $(thread_defines) -c $(libgasnet_sources)
	$(AR) cru $@ $(libgasnet_objects)
	$(RANLIB) $@
	rm -f $(libgasnet_objects)

libgasnet-smp-parsync.a: $(libgasnet_dependencies)
	$(CC) $(LIBCFLAGS) -DGASNET_PARSYNC $(thread_defines) -c $(libgasnet_sources)
	$(AR) cru $@ $(libgasnet_objects)
	$(RANLIB) $@
	rm -f $(libgasnet_objects)

.PHONY: all libs clean-local install-data-local uninstall-local tests force tests-seq tests-par tests-parsync


libs: $(libraries)
	
if USE_SMP_CONDUIT                           
lib_LIBRARIES = $(libraries)

all: libs $(makefile_fragments)
	
clean-local: 
	rm -f $(libgasnet_objects) $(libraries) *.o core
	@$(MAKE) -f $(top_builddir)/tests/Makefile clean     \
	  srcdir="$(top_srcdir)/tests"			     \
	  configfile="/dev/null"

headers = $(srcdir)/*.h $(top_srcdir)/extended-ref/*.h 
makefile_fragments = smp-seq.mak smp-parsync.mak smp-par.mak

install-data-local: $(headers)
	$(mkinstalldirs) $(DESTDIR)$(includedir)/smp-conduit
	@list='$(headers)'; for p in $$list; do \
	  if test -f $$p; then \
	    filename=`basename $$p`; \
	    echo " $(INSTALL_DATA) $$p $(DESTDIR)$(includedir)/smp-conduit/$$filename"; \
	    $(INSTALL_DATA) $$p $(DESTDIR)$(includedir)/smp-conduit/$$filename; \
	  else :; fi; \
	done
	@list='$(makefile_fragments)'; for p in $$list; do \
	  if test -f $$p; then \
	    filename=`basename $$p`; \
	    echo "@GASNET_INSTALL_CMD@ < $$p > $(DESTDIR)$(includedir)/smp-conduit/$$filename"; \
	    @GASNET_INSTALL_CMD@ < $$p > $(DESTDIR)$(includedir)/smp-conduit/$$filename; \
	  else :; fi; \
	done

uninstall-local:
	rm -f $(DESTDIR)$(includedir)/smp-conduit/*.h $(DESTDIR)$(includedir)/smp-conduit/*.mak

else
$(top_srcdir)/gasnet.h: force
	@echo "ERROR: smp-conduit support was not detected at configure time"
	@echo "       try re-running configure with --enable-smp"
	@exit 1
endif

tests:
	@echo You must specify one of tests-seq, tests-par or tests-parsync
	@exit 1

tests-seq: force libgasnet-smp-seq.a smp-seq.mak
	@$(MAKE) -f $(top_builddir)/tests/Makefile tests-seq \
	  srcdir="$(top_srcdir)/tests"			     \
	  configfile="smp-seq.mak"

tests-parsync: force libgasnet-smp-parsync.a smp-parsync.mak
	@$(MAKE) -f $(top_builddir)/tests/Makefile tests-parsync \
	  srcdir="$(top_srcdir)/tests"			     \
	  configfile="smp-parsync.mak"

tests-par: force libgasnet-smp-par.a smp-par.mak
	@$(MAKE) -f $(top_builddir)/tests/Makefile tests-par \
	  srcdir="$(top_srcdir)/tests"			     \
	  configfile="smp-par.mak"

force:
	



## Process this file with automake to produce Makefile.in	-*- makefile -*-
# $Header: /Users/kamil/work/gasnet-cvs2/gasnet/template-conduit/Makefile.am,v 1.12 2003/03/26 23:16:04 phargrov Exp $
# Description: Makefile for GASNet <conduitname> conduit
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
	README.template \
	README

libgasnet_sources = $(srcdir)/gasnet_core.c $(othersourcefiles)
libgasnet_dependencies = $(filelist) $(libgasnet_sources) \
                         $(top_srcdir)/*.[ch]             \
                         $(top_srcdir)/extended-ref/*.h       
libgasnet_objects = gasnet_core.o gasnet_internal.o gasnet_extended.o

if HAVE_PTHREAD
threaded_libraries = libgasnet-<conduitname>-par.a libgasnet-<conduitname>-parsync.a
libgasnet_<conduitname>_par_a_SOURCES =
libgasnet_<conduitname>_parsync_a_SOURCES =
endif
libraries = libgasnet-<conduitname>-seq.a $(threaded_libraries)
libgasnet_<conduitname>_seq_a_SOURCES =

VPATH=$(srcdir)

LIBCFLAGS = @CFLAGS@ @MISC_CFLAGS@ @BUILDCONFIG_DEFINES@ @GASNET_MACHINE_DEFINES@ $(INCLUDES)

libgasnet-<conduitname>-seq.a: $(libgasnet_dependencies)
	$(CC) $(LIBCFLAGS) -DGASNET_SEQ -c $(libgasnet_sources)
	$(AR) cru $@ $(libgasnet_objects)
	$(RANLIB) $@
	rm -f $(libgasnet_objects)

libgasnet-<conduitname>-par.a: $(libgasnet_dependencies)
	$(CC) $(LIBCFLAGS) -DGASNET_PAR $(thread_defines) -c $(libgasnet_sources)
	$(AR) cru $@ $(libgasnet_objects)
	$(RANLIB) $@
	rm -f $(libgasnet_objects)

libgasnet-<conduitname>-parsync.a: $(libgasnet_dependencies)
	$(CC) $(LIBCFLAGS) -DGASNET_PARSYNC $(thread_defines) -c $(libgasnet_sources)
	$(AR) cru $@ $(libgasnet_objects)
	$(RANLIB) $@
	rm -f $(libgasnet_objects)

.PHONY: all libs clean-local install-data-local uninstall-local tests force tests-seq tests-par tests-parsync


libs: $(libraries)
	
if USE_<CONDUITNAME>_CONDUIT                           
lib_LIBRARIES = $(libraries)

all: libs $(makefile_fragments)
	
clean-local: 
	rm -f $(libgasnet_objects) $(libraries) *.o core
	@$(MAKE) -f $(top_builddir)/tests/Makefile clean     \
	  srcdir="$(top_srcdir)/tests"			     \
	  configfile="/dev/null"

headers = $(srcdir)/*.h $(top_srcdir)/extended-ref/*.h 
makefile_fragments = <conduitname>-seq.mak <conduitname>-parsync.mak <conduitname>-par.mak

install-data-local: $(headers)
	$(mkinstalldirs) $(includedir)/<conduitname>-conduit
	@list='$(headers)'; for p in $$list; do \
	  if test -f $$p; then \
	    filename=`basename $$p`; \
	    echo " $(INSTALL_DATA) $$p $(includedir)/<conduitname>-conduit/$$filename"; \
	    $(INSTALL_DATA) $$p $(includedir)/<conduitname>-conduit/$$filename; \
	  else :; fi; \
	done
	@list='$(makefile_fragments)'; for p in $$list; do \
	  if test -f $$p; then \
	    filename=`basename $$p`; \
	    echo " grep -v "###NOINSTALL###" $$p > $(includedir)/<conduitname>-conduit/$$filename"; \
	    grep -v "###NOINSTALL###" $$p > $(includedir)/<conduitname>-conduit/$$filename; \
	  else :; fi; \
	done

uninstall-local:
	rm -f $(includedir)/<conduitname>-conduit/*.h $(includedir)/<conduitname>-conduit/*.mak

endif

tests:
	@echo You must specify one of tests-seq, tests-par or tests-parsync
	@exit 1

tests-seq: force libgasnet-<conduitname>-seq.a <conduitname>-seq.mak
	@$(MAKE) -f $(top_builddir)/tests/Makefile tests     \
	  srcdir="$(top_srcdir)/tests"			     \
	  configfile="<conduitname>-seq.mak"

tests-parsync: force libgasnet-<conduitname>-parsync.a <conduitname>-parsync.mak
	@$(MAKE) -f $(top_builddir)/tests/Makefile tests     \
	  srcdir="$(top_srcdir)/tests"			     \
	  configfile="<conduitname>-parsync.mak"

tests-par: force libgasnet-<conduitname>-par.a <conduitname>-par.mak
	@$(MAKE) -f $(top_builddir)/tests/Makefile tests     \
	  srcdir="$(top_srcdir)/tests"			     \
	  configfile="<conduitname>-par.mak"

force:
	



## Process this file with automake to produce Makefile.in	-*- makefile -*-
# $Header: /Users/kamil/work/gasnet-cvs2/gasnet/sci-conduit/Attic/Makefile.am,v 1.1.2.6 2004/01/05 05:04:10 bonachea Exp $
# Description: Makefile for GASNet sci conduit
# Copyright 2002, Dan Bonachea <bonachea@cs.berkeley.edu>
# Terms of use are as specified in license.txt

AUTOMAKE_OPTIONS = foreign 1.4

thread_defines = @GASNET_THREAD_DEFINES@

INCLUDES = -I$(srcdir) -I$(top_srcdir) -I$(top_builddir)        \
           -I$(top_srcdir)/extended-ref				\
	   @SCI_INCLUDES@

filelist =                   \
      gasnet_core.c          \
      gasnet_core_internal.c \
      gasnet_core.h          \
      gasnet_core_fwd.h      \
      gasnet_core_help.h     \
      gasnet_core_internal.h 

othersourcefiles =                             \
      $(top_srcdir)/gasnet_internal.c          \
      $(top_srcdir)/extended-ref/gasnet_extended.c

EXTRA_DIST = $(filelist) \
      README

libgasnet_sources = $(srcdir)/gasnet_core_internal.c $(srcdir)/gasnet_core.c $(othersourcefiles)
libgasnet_dependencies = $(filelist) $(libgasnet_sources) \
                         $(top_srcdir)/*.[ch]             \
                         $(top_srcdir)/extended-ref/*.h       
libgasnet_objects = gasnet_core_internal.o gasnet_core.o gasnet_internal.o gasnet_extended.o 

if HAVE_PTHREAD
threaded_libraries = libgasnet-sci-par.a libgasnet-sci-parsync.a
libgasnet_sci_par_a_SOURCES =
libgasnet_sci_parsync_a_SOURCES =
endif
libraries = libgasnet-sci-seq.a $(threaded_libraries)
libgasnet_sci_seq_a_SOURCES =

VPATH=$(srcdir)

# Always need -D_REENTRANT for SCI callbacks
LIBCFLAGS = @CFLAGS@ -D_REENTRANT @MISC_CFLAGS@ @BUILDCONFIG_DEFINES@ @GASNET_MACHINE_DEFINES@ $(INCLUDES)

libgasnet-sci-seq.a: $(libgasnet_dependencies)
	$(CC) $(LIBCFLAGS) -DGASNET_SEQ -c $(libgasnet_sources)
	$(AR) cru $@ $(libgasnet_objects)
	$(RANLIB) $@
	rm -f $(libgasnet_objects)

libgasnet-sci-par.a: $(libgasnet_dependencies)
	$(CC) $(LIBCFLAGS) -DGASNET_PAR $(thread_defines) -c $(libgasnet_sources)
	$(AR) cru $@ $(libgasnet_objects)
	$(RANLIB) $@
	rm -f $(libgasnet_objects)

libgasnet-sci-parsync.a: $(libgasnet_dependencies)
	$(CC) $(LIBCFLAGS) -DGASNET_PARSYNC $(thread_defines) -c $(libgasnet_sources)
	$(AR) cru $@ $(libgasnet_objects)
	$(RANLIB) $@
	rm -f $(libgasnet_objects)

.PHONY: all libs clean-local install-data-local uninstall-local tests force tests-seq tests-par tests-parsync


libs: $(libraries)
	
if USE_SCI_CONDUIT                           
lib_LIBRARIES = $(libraries)

all: libs $(makefile_fragments)
	
clean-local: 
	rm -f $(libgasnet_objects) $(libraries) *.o core
	@$(MAKE) -f $(top_builddir)/tests/Makefile clean     \
	  srcdir="$(top_srcdir)/tests"			     \
	  configfile="/dev/null"

headers = $(srcdir)/*.h $(top_srcdir)/extended-ref/*.h 
makefile_fragments = sci-seq.mak sci-parsync.mak sci-par.mak

install-data-local: $(headers)
	$(mkinstalldirs) $(DESTDIR)$(includedir)/sci-conduit
	@list='$(headers)'; for p in $$list; do \
	  if test -f $$p; then \
	    filename=`basename $$p`; \
	    echo " $(INSTALL_DATA) $$p $(DESTDIR)$(includedir)/sci-conduit/$$filename"; \
	    $(INSTALL_DATA) $$p $(DESTDIR)$(includedir)/sci-conduit/$$filename; \
	  else :; fi; \
	done
	@list='$(makefile_fragments)'; for p in $$list; do \
	  if test -f $$p; then \
	    filename=`basename $$p`; \
	    echo " grep -v "###NOINSTALL###" $$p | sed "s@###INSTALL_PREFIX###@${prefix}@g" > $(DESTDIR)$(includedir)/sci-conduit/$$filename"; \
	    grep -v "###NOINSTALL###" $$p | sed "s@###INSTALL_PREFIX###@${prefix}@g" > $(DESTDIR)$(includedir)/sci-conduit/$$filename; \
	  else :; fi; \
	done

uninstall-local:
	rm -f $(DESTDIR)$(includedir)/sci-conduit/*.h $(DESTDIR)$(includedir)/sci-conduit/*.mak

else
$(top_srcdir)/gasnet.h: force
	@echo "ERROR: sci-conduit support was not detected at configure time"
	@echo "       try re-running configure with --enable-sci"
	@exit 1
endif

tests:
	@echo You must specify one of tests-seq, tests-par or tests-parsync
	@exit 1

tests-seq: force libgasnet-sci-seq.a sci-seq.mak
	@$(MAKE) -f $(top_builddir)/tests/Makefile tests-seq \
	  srcdir="$(top_srcdir)/tests"			     \
	  configfile="sci-seq.mak"

tests-parsync: force libgasnet-sci-parsync.a sci-parsync.mak
	@$(MAKE) -f $(top_builddir)/tests/Makefile tests-parsync \
	  srcdir="$(top_srcdir)/tests"			     \
	  configfile="sci-parsync.mak"

tests-par: force libgasnet-sci-par.a sci-par.mak
	@$(MAKE) -f $(top_builddir)/tests/Makefile tests-par \
	  srcdir="$(top_srcdir)/tests"			     \
	  configfile="sci-par.mak"

force:
	



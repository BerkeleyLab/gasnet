## Process this file with automake to produce Makefile.in	-*- makefile -*-
# $Header: /Users/kamil/work/gasnet-cvs2/gasnet/udp-conduit/Makefile.am,v 1.2 2003/12/16 17:33:01 phargrov Exp $
# Description: Makefile for GASNet UDP conduit
# Copyright 2002, Dan Bonachea <bonachea@cs.berkeley.edu>
# Terms of use are as specified in license.txt

AUTOMAKE_OPTIONS = foreign 1.4

thread_defines = @GASNET_THREAD_DEFINES@

INCLUDES = -I$(srcdir) -I$(top_srcdir) -I$(top_builddir)        \
           -I$(top_srcdir)/extended-ref -I$(top_srcdir)/other/amudp 
	   

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

libgasnet_sources = $(srcdir)/gasnet_core.c     \
		    $(othersourcefiles)

libgasnet_dependencies = $(filelist) $(libgasnet_sources) \
                         $(top_srcdir)/*.[ch]             \
                         $(top_srcdir)/extended-ref/gasnet_extended_refbarrier.c \
                         $(top_srcdir)/extended-ref/*.h   \
			 $(top_srcdir)/other/amudp/*.h

libgasnet_objects = gasnet_core.o gasnet_internal.o gasnet_extended.o

if HAVE_PTHREAD
threaded_libraries = libgasnet-udp-par.a libgasnet-udp-parsync.a
libgasnet_udp_par_a_SOURCES =
libgasnet_udp_parsync_a_SOURCES =
endif
libraries = libgasnet-udp-seq.a $(threaded_libraries)
libgasnet_udp_seq_a_SOURCES =

VPATH=$(srcdir)

LIBCFLAGS = @CFLAGS@ @MISC_CFLAGS@ @BUILDCONFIG_DEFINES@ @GASNET_MACHINE_DEFINES@ $(INCLUDES)

libgasnet-udp-seq.a: $(libgasnet_dependencies)
	$(CC) $(LIBCFLAGS) -DGASNET_SEQ -c $(libgasnet_sources)
	$(AR) cru $@ $(libgasnet_objects)
	$(RANLIB) $@
	rm -f $(libgasnet_objects)

libgasnet-udp-par.a: $(libgasnet_dependencies)
	$(CC) $(LIBCFLAGS) -DGASNET_PAR $(thread_defines) -c $(libgasnet_sources)
	$(AR) cru $@ $(libgasnet_objects)
	$(RANLIB) $@
	rm -f $(libgasnet_objects)

libgasnet-udp-parsync.a: $(libgasnet_dependencies)
	$(CC) $(LIBCFLAGS) -DGASNET_PARSYNC $(thread_defines) -c $(libgasnet_sources)
	$(AR) cru $@ $(libgasnet_objects)
	$(RANLIB) $@
	rm -f $(libgasnet_objects)

.PHONY: all libs clean-local install-data-local uninstall-local tests force tests-seq tests-par tests-parsync


libs: $(libraries)
	
if USE_UDP_CONDUIT                           
lib_LIBRARIES = $(libraries)

all: libs $(makefile_fragments)
	
clean-local: 
	rm -f $(libgasnet_objects) $(libraries) *.o core
	@$(MAKE) -f $(top_builddir)/tests/Makefile clean     \
	  srcdir="$(top_srcdir)/tests"			     \
	  configfile="/dev/null"

headers = $(srcdir)/*.h $(top_srcdir)/extended-ref/*.h $(top_srcdir)/other/amudp/amudp*.h 
makefile_fragments = udp-seq.mak udp-parsync.mak udp-par.mak

install-data-local: $(headers)
	$(mkinstalldirs) $(DESTDIR)$(includedir)/udp-conduit
	@list='$(headers)'; for p in $$list; do \
	  if test -f $$p; then \
	    filename=`basename $$p`; \
	    echo " $(INSTALL_DATA) $$p $(DESTDIR)$(includedir)/udp-conduit/$$filename"; \
	    $(INSTALL_DATA) $$p $(DESTDIR)$(includedir)/udp-conduit/$$filename; \
	  else :; fi; \
	done
	@list='$(makefile_fragments)'; for p in $$list; do \
	  if test -f $$p; then \
	    filename=`basename $$p`; \
	    echo "@GASNET_INSTALL_CMD@ < $$p > $(DESTDIR)$(includedir)/udp-conduit/$$filename"; \
	    @GASNET_INSTALL_CMD@ < $$p > $(DESTDIR)$(includedir)/udp-conduit/$$filename; \
	  else :; fi; \
	done


uninstall-local:
	rm -f $(DESTDIR)$(includedir)/udp-conduit/*.h $(DESTDIR)$(includedir)/udp-conduit/*.mak

endif

tests:
	@echo You must specify one of tests-seq, tests-par or tests-parsync
	@exit 1

tests-seq: force libgasnet-udp-seq.a udp-seq.mak
	@$(MAKE) -f $(top_builddir)/tests/Makefile tests-seq \
	  srcdir="$(top_srcdir)/tests"			     \
	  configfile="udp-seq.mak"

tests-parsync: force libgasnet-udp-parsync.a udp-parsync.mak
	@$(MAKE) -f $(top_builddir)/tests/Makefile tests-parsync \
	  srcdir="$(top_srcdir)/tests"			     \
	  configfile="udp-parsync.mak"

tests-par: force libgasnet-udp-par.a udp-par.mak
	@$(MAKE) -f $(top_builddir)/tests/Makefile tests-par \
	  srcdir="$(top_srcdir)/tests"			     \
	  configfile="udp-par.mak"

force:
	



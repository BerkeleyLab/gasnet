## Process this file with automake to produce Makefile.in	-*- makefile -*-
# $Header: /Users/kamil/work/gasnet-cvs2/gasnet/gm-conduit/Attic/Makefile.am,v 1.33 2004/01/05 05:01:14 bonachea Exp $
# Description: Makefile for GASNet gm conduit
# Copyright 2002, Christian Bell <csbell@cs.berkeley.edu>
# Copyright 2002, Dan Bonachea <bonachea@cs.berkeley.edu>
# Terms of use are as specified in license.txt

AUTOMAKE_OPTIONS = foreign 1.4

thread_defines = @GASNET_THREAD_DEFINES@
gm_include = @GM_INCLUDE@
platform_libraries = @LIBS@ -L@GM_LIB@
linkend = -lgm

fh_srcdir = $(top_srcdir)/other/firehose

INCLUDES = -I$(srcdir) -I$(top_srcdir) -I$(top_srcdir)/extended-ref/ \
	   -I$(fh_srcdir) -I$(top_builddir) -I$(gm_include)
	   
filelist = gasnet_core.c              \
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
	   firehose_fwd.h


othersourcefiles = $(top_srcdir)/gasnet_internal.c $(fh_srcdir)/firehose.c \
		   $(fh_srcdir)/firehose_hash.c $(fh_srcdir)/firehose_page.c

EXTRA_DIST = $(filelist) README license.txt

SUBDIRS = contrib

libgasnet_sources = $(srcdir)/gasnet_core.c $(srcdir)/gasnet_core_receive.c \
		    $(srcdir)/gasnet_core_conf.c $(srcdir)/gasnet_extended.c \
		    $(srcdir)/gasnet_extended_op.c $(srcdir)/gasnet_extended_ref.c \
		    $(srcdir)/gasnet_extended_firehose.c $(othersourcefiles)
libgasnet_dependencies = $(filelist) $(libgasnet_sources) \
                         $(top_srcdir)/*.[ch] \
                         $(top_srcdir)/extended-ref/gasnet_extended_refbarrier.c \
                         $(top_srcdir)/extended-ref/*.h \
                         $(fh_srcdir)/firehose.h $(fh_srcdir)/firehose_internal.h
libgasnet_objects = gasnet_core.o gasnet_internal.o gasnet_extended.o \
		    gasnet_core_receive.o gasnet_core_conf.o gasnet_extended_op.o \
		    gasnet_extended_ref.o gasnet_extended_firehose.o \
		    firehose.o firehose_hash.o firehose_page.o

if HAVE_PTHREAD
threaded_libraries = libgasnet-gm-par.a libgasnet-gm-parsync.a
libgasnet_gm_par_a_SOURCES =
libgasnet_gm_parsync_a_SOURCES =
endif
libraries = libgasnet-gm-seq.a $(threaded_libraries)
libgasnet_gm_seq_a_SOURCES =

VPATH=$(srcdir)

LIBCFLAGS = @CFLAGS@ @MISC_CFLAGS@ @BUILDCONFIG_DEFINES@ @GASNET_MACHINE_DEFINES@ $(INCLUDES)

libgasnet-gm-seq.a: $(libgasnet_dependencies)
	$(CC) $(LIBCFLAGS) -DGASNET_SEQ -c $(libgasnet_sources)
	$(AR) cru $@ $(libgasnet_objects)
	$(RANLIB) $@
	rm -f $(libgasnet_objects)

libgasnet-gm-par.a: $(libgasnet_dependencies)
	$(CC) $(LIBCFLAGS) -DGASNET_PAR $(thread_defines) -c $(libgasnet_sources)
	$(AR) cru $@ $(libgasnet_objects)
	$(RANLIB) $@
	rm -f $(libgasnet_objects)

libgasnet-gm-parsync.a: $(libgasnet_dependencies)
	$(CC) $(LIBCFLAGS) -DGASNET_PARSYNC $(thread_defines) -c $(libgasnet_sources)
	$(AR) cru $@ $(libgasnet_objects)
	$(RANLIB) $@
	rm -f $(libgasnet_objects)

.PHONY: all libs clean-local install-data-local uninstall-local tests force tests-seq tests-par tests-parsync


libs: $(libraries)
	
if USE_GM_CONDUIT                           
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

makefile_fragments = gm-seq.mak gm-parsync.mak gm-par.mak

install-data-local: $(headers)
	$(mkinstalldirs) $(DESTDIR)$(includedir)/gm-conduit
	@list='$(headers)'; for p in $$list; do \
	  if test -f $$p; then \
	    filename=`basename $$p`; \
	    echo " $(INSTALL_DATA) $$p $(DESTDIR)$(includedir)/gm-conduit/$$filename"; \
	    $(INSTALL_DATA) $$p $(DESTDIR)$(includedir)/gm-conduit/$$filename; \
	  else :; fi; \
	done
	@list='$(makefile_fragments)'; for p in $$list; do \
	  if test -f $$p; then \
	    filename=`basename $$p`; \
	    echo "@GASNET_INSTALL_CMD@ < $$p > $(DESTDIR)$(includedir)/gm-conduit/$$filename"; \
	    @GASNET_INSTALL_CMD@ < $$p > $(DESTDIR)$(includedir)/gm-conduit/$$filename; \
	  else :; fi; \
	done

uninstall-local:
	rm -f $(includedir)/gm-conduit/*.h $(includedir)/gm-conduit/*.mak

else
$(top_srcdir)/gasnet.h: force
	@echo "ERROR: gm-conduit support was not detected at configure time"
	@echo "       try re-running configure with --enable-gm"
	@exit 1
endif

tests:
	@echo You must specify one of tests-seq, tests-par or tests-parsync
	@exit 1

tests-seq: force libgasnet-gm-seq.a gm-seq.mak
	@$(MAKE) -f $(top_builddir)/tests/Makefile tests-seq \
	  srcdir="$(top_srcdir)/tests"			     \
	  configfile="gm-seq.mak"

tests-parsync: force libgasnet-gm-parsync.a gm-parsync.mak
	@$(MAKE) -f $(top_builddir)/tests/Makefile tests-parsync \
	  srcdir="$(top_srcdir)/tests"			     \
	  configfile="gm-parsync.mak"

tests-par: force libgasnet-gm-par.a gm-par.mak
	@$(MAKE) -f $(top_builddir)/tests/Makefile tests-par \
	  srcdir="$(top_srcdir)/tests"			     \
	  configfile="gm-par.mak"

force:
	



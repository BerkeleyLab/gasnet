## Process this file with automake to produce Makefile.in	-*- makefile -*-
#   $Source: /Users/kamil/work/gasnet-cvs2/gasnet/Makefile.am,v $
#     $Date: 2007/02/24 00:00:34 $
# $Revision: 1.54.10.1 $
# Description: Makefile for GASNet 
# Copyright 2002, Dan Bonachea <bonachea@cs.berkeley.edu>
# Terms of use are as specified in license.txt

AUTOMAKE_OPTIONS = foreign 1.4

# Directories for automake to recurse into:
SUBDIRS = .          \
	config-aux   \
        other

# Directories we manually recurse into:
CONDUIT_DIRS =       \
        elan-conduit \
        gm-conduit   \
        lapi-conduit \
	mpi-conduit  \
	sci-conduit  \
	shmem-conduit  \
	smp-conduit  \
	udp-conduit  \
	vapi-conduit

DIST_SUBDIRS = $(SUBDIRS) $(CONDUIT_DIRS)

configdir = $(includedir)
config_DATA = $(CONFIG_HEADER)
docdir = $(datadir)/doc/gasnet

headers =                  \
        gasnet.h           \
	gasnet_ammacros.h  \
	gasnet_asm.h       \
	gasnet_atomicops.h \
	gasnet_atomic_bits.h \
        gasnet_basic.h     \
        gasnet_coll.h      \
        gasnet_handler.h   \
        gasnet_help.h      \
	gasnet_membar.h    \
	gasnet_syncops.h   \
        gasnet_timer.h     \
	gasnet_tools.h     \
	gasnet_toolhelp.h  \
	gasnet_trace.h     \
        gasnet_vis.h      

EXTRA_DIST =              \
        README            \
	ChangeLog	  \
	license.txt       \
	docs              \
        extended-ref      \
	template-conduit  \
	.autom4te.cfg     \
        Bootstrap         \
        unBootstrap       \
        configure.in      \
        acconfig.h        \
        acinclude.m4      \
        $(headers)        \
        gasnet_internal.h \
        gasnet_internal.c \
        gasnet_mmap.c  	  \
        gasnet_diagnostic.c \
        gasnet_tools.c    \
        gasnet_trace.c
#       tests handled below

include_HEADERS = $(headers)

force:

#-------------------------------------------------------
# libgasnet_tools goop

if HAVE_PTHREAD
libgasnet_tools_par_a_SOURCES =
threaded_libraries = libgasnet_tools-par.a 
endif
libgasnet_tools_seq_a_SOURCES =
libraries = libgasnet_tools-seq.a $(threaded_libraries)

# leading - prevents distclean failures
-include $(top_builddir)/other/Makefile-libgasnet.mak

libgasnet_tools-seq.a: $(libgasnet_tools_dependencies)
	@$(MAKE) do-libgasnet_tools-seq

libgasnet_tools-par.a: $(libgasnet_tools_dependencies)
	@$(MAKE) do-libgasnet_tools-par

lib_LIBRARIES = $(libraries)
#-------------------------------------------------------

Makefile: $(srcdir)/Makefile.in $(top_builddir)/other/Makefile-libgasnet.mak
	cd $(top_builddir) && CONFIG_FILES=$@ CONFIG_HEADERS= ./config.status

# echo an arbitrary make/environment variable
echovar: force
	@echo $(VARNAME)=\"$($(VARNAME))\"
	
test: tests-seq
tests: tests-seq

other-tests:
	@cd $(top_builddir)/other && $(MAKE) all tests

tests-seq tests-parsync tests-par: other-tests
	@for conduit in $(CONDUITS) ; do                                       \
	  ( cd "$(top_builddir)/$$conduit-conduit" && $(MAKE) $@ ) || exit $$?; \
	done

TESTINSTANCE=$${PPID:-xxxx}
TESTLOG=`pwd`/.test-results


run-tests run-tests-seq run-tests-parsync run-tests-par: do-begin-tests
	@testinst=$(TESTINSTANCE) ; 							\
	testlog=$(TESTLOG) ; 								\
        err=0 ; 									\
	for conduit in $(CONDUITS) ; do                                        		\
	  ( cd "$(top_builddir)/$$conduit-conduit" &&                          		\
            $(MAKE) TESTINSTANCE=$$testinst TESTLOG=$$testlog 				\
		    TESTLOG_INHERIT=1 $@ ) || err=$$? ;					\
          if test "$$err" != "0" ; then                             			\
		echo " *-* COMPILE FAILURE: $$conduit-conduit $@ *-*" >> $(TESTLOG) ;   \
		break ; 								\
	  fi ;                        							\
	done ; 										\
        if test "$$err" = "0" ; then  							\
	  ( cd $(top_builddir)/other &&  						\
            $(MAKE) TESTINSTANCE=$$testinst TESTLOG=$$testlog 				\
		    TESTLOG_INHERIT=1 run-tests ) || err=$$? ; 				\
        fi ; 										\
	$(MAKE) TESTINSTANCE=$(TESTINSTANCE) TESTLOG=$(TESTLOG) do-end-tests ; 		\
	exit $$err

do-begin-tests:
	@testinst=$(TESTINSTANCE) ; \
	testlog=$(TESTLOG) ; \
	if test "`head -1 $$testlog 2> /dev/null`" != "$$testinst" ; then \
	  echo "$$testinst" > $$testlog ; \
	  echo Starting a new test instance, log=$$testlog ; \
	fi 

do-end-tests:
	@testinst=$(TESTINSTANCE) ; \
	testlog=$(TESTLOG) ; \
	if test "`head -1 $$testlog 2> /dev/null`" = "$$testinst" ; then \
	  line='+++++++++++++++++++++++++++++++++++++++' ; line="$$line$$line" ; \
	  echo "$$line" ; \
	  echo Test results: `tail +2 $$testlog | grep "\*\-\*" | wc -l` failures ; \
	  tail +2 $$testlog ; \
	  echo "$$line" ; \
	fi

CLEANFILES = $(TESTLOG) 

# Build, clean, install, uninstall, etc. the conduits - in parallel if possible.
# Instead of automake's loop over subdir, we expand to one target per subdir.
# Care is taken w/ the 'clean-local' rule in case distclean has wiped the 
# local or conduit Makefiles (order depends on the version of automake).
# Note that if ($VAR:%=%FOO) doesn't work for a given make, then we are
# reduced to using actual conduit names as PHONY targets, but the rule for
# $(recurse_local) is tailored to work correctly regardless.
recurse_local = $(CONDUITS:%=%-recurse)
$(recurse_local):
	@cd `echo $@ | sed s/-recurse//`-conduit && $(MAKE) $(DO_WHAT)
# conduit subdirectories that should be made, even when the conduit is disabled
SPECIAL_SUBDIRS = mpi-conduit/contrib
recurse_special:
	@for dir in $(SPECIAL_SUBDIRS) ; do \
	  if test -f $$dir/Makefile ; then \
	    cd $$dir && $(MAKE) $(DO_WHAT) || exit $$? ; \
	  fi ; \
	 done
all-local: $(lib_LIBRARIES)
	@$(MAKE) $(recurse_local) DO_WHAT=$${DO_WHAT=all}
	@$(MAKE) recurse_special  DO_WHAT=$${DO_WHAT=all}
clean-local:
	@if test -f Makefile -a -f smp-conduit/Makefile; then \
	  $(MAKE) $(recurse_local) DO_WHAT=clean || exit $$? ; \
	 fi
	@$(MAKE) recurse_special DO_WHAT=clean
check-local: check-exports
seq par parsync:
	@$(MAKE) $(recurse_local) DO_WHAT=$@
check tests-clean:
	@$(MAKE) $(recurse_local) DO_WHAT=$@
	@cd "$(top_builddir)/other" && $(MAKE) $@
install-data-local: 
	@$(MAKE) $(recurse_local) DO_WHAT=install-data
	@$(MAKE) recurse_special DO_WHAT=install-data
	$(mkinstalldirs) $(DESTDIR)$(docdir)
	cp -f $(top_srcdir)/README $(DESTDIR)$(docdir)/README
	cp -f $(top_srcdir)/extended-ref/README $(DESTDIR)$(docdir)/README-extref
	cp -f $(top_srcdir)/license.txt $(DESTDIR)$(docdir)/license.txt
install-exec-local:
	@$(MAKE) $(recurse_local) DO_WHAT=install-exec
	@$(MAKE) recurse_special DO_WHAT=install-exec
uninstall-local: 
	@$(MAKE) $(recurse_local) DO_WHAT=uninstall
	@$(MAKE) recurse_special DO_WHAT=uninstall
	rm -Rf $(DESTDIR)$(docdir)

# automake 1.4 doesn't automatically make distclean in DIST_SUBDIRS
# So, we run it here if it doesn't appear to have already been run.
# We also take care of tests, which contains a generated Makefile
# that older autotools fail to automatically remove as well.
distclean-local:
	@for dir in $(CONDUIT_DIRS) tests; do               \
	   if test -f "$$dir/Makefile"; then                \
	     ( cd "$$dir" && $(MAKE) distclean ) || exit 1; \
	   fi;                                              \
	 done
	@$(MAKE) recurse_special DO_WHAT=distclean

.PHONY: force tests test do-begin-tests do-end-tests                  \
        other-tests other-run-tests tests-seq tests-par tests-parsync \
        run-tests-seq run-tests-par run-tests-parsync                 \
	$(recurse_local)


dist-hook:
	if test -f $(distdir)/docs/Makefile ; then \
          cd $(distdir)/docs && $(MAKE) dist-hook || exit $$? ; \
        fi
	mkdir -p $(distdir)/tests  # need this special hack to handle tests or distcheck will fail
	fulldistdir=`cd $(distdir) ; @PWD_PROG@` export fulldistdir ; \
        cd $(srcdir)/tests ; for file in `find . -print` ; do \
 	  if test -d $$file -a ! -d $$fulldistdir/tests/$$file ; then \
	     mkdir -p $$fulldistdir/tests/$$file || exit $$? ; \
          fi; \
	  if test -f $$file -a ! -f $$fulldistdir/tests/$$file ; then \
	     cp -p $$file $$fulldistdir/tests/$$file || exit $$? ; \
          fi ; \
        done
	rm -Rf `find $(distdir) -name CVS -type d -print`
	rm -f `find $(distdir) -name .#\* -print`


## Process this file with automake to produce Makefile.in	-*- makefile -*-
# NOTE: This Makefile is used to tie in with the GASNet build infrastructure
#       the stand-alone AMUDP product doesn't currently provide an automake framework

AUTOMAKE_OPTIONS = foreign 1.4 no-dependencies

if PLATFORM_ILP32
  apputils_flags =
else
  apputils_flags = -DAPPUTILS_OMIT_READWRITE
endif

CC  = @CC@
CFLAGS = @CFLAGS@ @MISC_CFLAGS@ $(apputils_flags)
CXX = @CXX@
CXXFLAGS = @CXXFLAGS@ @MISC_CXXFLAGS@
LDFLAGS = @LDFLAGS@

platform_defines = @GASNET_MACHINE_DEFINES@ @BUILDCONFIG_DEFINES@ -DAMUDP_ENV_PREFIX=GASNET
platform_libraries = @LIBS@ @GASNET_UDP_LIBS@

VPATH = $(srcdir)
testdir = $(srcdir)/../amxtests
altincdir = $(srcdir)/..
INCLUDES = -I$(srcdir) -I$(altincdir) $(platform_defines)
compile = $(CC) $(CFLAGS) $(INCLUDES)
cxxcompile = $(CXX) $(CXXFLAGS) $(INCLUDES)
link = $(CXX) $(CXXFLAGS) $(LDFLAGS) $(INCLUDES) 
linkend = $(platform_libraries)

if USE_UDP_CONDUIT
lib_LIBRARIES = libamudp.a
endif
headers = \
	amudp.h			\
	amudp_internal.h	\
	amudp_spmd.h		\
	exc.h			\
	sig.h			\
	sockaddr.h		\
	socket.h		\
	socklist.h		\
	sockutil.h		\
	ueth.h			\
	ufxp.h

libamudp_a_SOURCES = \
	$(headers)		\
	amudp_ep.cpp		\
	amudp_reqrep.cpp	\
	amudp_spawn.cpp		\
	amudp_spmd.cpp		\
	exc.cpp			\
	sig.cpp			\
	socklist.cpp		\
	sockutil.cpp		

libamudp_a_DEPENDENCIES = $(libamudp_a_SOURCES:%.cpp=%.o)

EXTRA_DIST = Makefile.common Makefile.standalone

# ---------- test programs -------------
# all the test executables
if PLATFORM_ILP32
  testprograms_ILP32only = 	\
    testgetput    		\
    testreadwrite 
else
  testprograms_ILP32only = 
endif

testprograms =    		\
    testam        		\
    testbounce    		\
    testbulk      		\
    testlatency   		\
    testlatencyM   		\
    testping      		\
    testreduce			\
    $(testprograms_ILP32only)

tests: apputils.o $(testprograms)

test%: test%.o apputils.o libamudp.a 
	$(link) -o $@ $< apputils.o -L. -lamudp $(linkend)


TEST_NODES=2
TEST_ITERS=100
AMUDP_SPAWNFN=L
TEST_SPAWNFN=$(AMUDP_SPAWNFN)
TEST_MODE=P
TEST_DEPTH=32
TEST_DUPLEX=H
AMUDP_RUNCMD=
TEST_RUNCMD=$(AMUDP_RUNCMD)
AMUDP_NETWORKDEPTH=$(TEST_DEPTH)
run-tests: tests
	@echo +++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
	@echo  Running AMUDP tests...
	@echo  If this fails to spawn a job, you may need to re-run with a
	@echo   command like: gmake run-tests AMUDP_SPAWNFN=S
	@echo +++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
	@echo TESTPING
	$(TEST_RUNCMD) ./testping $(TEST_NODES) $(TEST_SPAWNFN)  $(TEST_ITERS) $(TEST_MODE)
	@echo +++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
	@echo TESTLATENCY
	$(TEST_RUNCMD) ./testlatency $(TEST_NODES) $(TEST_SPAWNFN)  $(TEST_ITERS) $(TEST_MODE)
	@echo +++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
	@echo TESTLATENCYM
	$(TEST_RUNCMD) ./testlatencyM $(TEST_NODES) $(TEST_SPAWNFN)  $(TEST_ITERS) $(TEST_MODE) 64
	@echo +++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
	@echo TESTBULK
	$(TEST_RUNCMD) ./testbulk $(TEST_NODES) $(TEST_SPAWNFN)  $(TEST_ITERS) 1048576 $(TEST_MODE) $(TEST_DUPLEX)
	@echo +++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
	@echo TESTAM
	$(TEST_RUNCMD) ./testam $(TEST_NODES) $(TEST_SPAWNFN)  $(TEST_ITERS) $(TEST_MODE)
	@echo +++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
	@echo TESTBOUNCE
	$(TEST_RUNCMD) ./testbounce $(TEST_NODES) $(TEST_SPAWNFN)  $(TEST_ITERS) $(TEST_MODE) 
	@echo +++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
	@echo TESTREDUCE
	$(TEST_RUNCMD) ./testreduce $(TEST_NODES) $(TEST_SPAWNFN)  
if PLATFORM_ILP32
	@echo +++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++ 
	@echo TESTGETPUT 
	$(TEST_RUNCMD) ./testgetput $(TEST_NODES) $(TEST_SPAWNFN)  $(TEST_ITERS) 
	@echo +++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
	@echo TESTREADWRITE
	$(TEST_RUNCMD) ./testreadwrite $(TEST_NODES) $(TEST_SPAWNFN)  $(TEST_ITERS)
endif
	@echo +++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
	@echo TESTS COMPLETE

test%.o : $(testdir)/test%.c $(testdir)/*.h $(headers)
	$(compile) -DAMUDP -I$(testdir) -c $< -o $@

apputils.o : $(testdir)/apputils.c $(testdir)/apputils.h $(headers)
	$(compile) -DAMUDP -I$(testdir) -c $(testdir)/apputils.c -o apputils.o

%.o: %.c $(headers)
	$(compile) -o $@ -c $<

%.o: %.cpp $(headers)
	$(cxxcompile) -o $@ -c $<

if USE_UDP_CONDUIT
clean-local:
	rm -f libamudp.a $(testprograms:%=%$(EXESUFFIX)) *.o core
endif

force:

.PHONY: force clean-local tests run-tests


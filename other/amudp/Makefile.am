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
CCFLAGS = @CFLAGS@ @MISC_CFLAGS@ $(apputils_flags)
CXX = @CXX@
CXXFLAGS = @CXXFLAGS@ @MISC_CXXFLAGS@

platform_defines = @GASNET_MACHINE_DEFINES@ @BUILDCONFIG_DEFINES@ -DAMUDP_ENV_PREFIX=GASNET
platform_libraries = @LIBS@ @GASNET_UDP_LIBS@

VPATH = $(srcdir)
testdir = $(srcdir)/../amxtests
INCLUDES = -I$(srcdir) ${platform_defines}
compile = ${CC} ${CCFLAGS} ${INCLUDES}
cxxcompile = ${CXX} ${CXXFLAGS} ${INCLUDES}
link = $(CXX) ${CXXFLAGS} ${INCLUDES} 
linkend = ${platform_libraries}

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
	sockutil.h

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

EXTRA_DIST =        \
    apputils.c      \
    apputils.h      \
    testam.c        \
    testbounce.c    \
    testbulk.c      \
    testgetput.c    \
    testlatency.c   \
    testlatencyM.c  \
    testping.c      \
    testreadwrite.c \
    testreduce.c    

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

tests: apputils.o ${testprograms}

test%: test%.o apputils.o libamudp.a 
	${link} -o $@ $< apputils.o -L. -lamudp ${linkend}


TEST_NODES=2
TEST_ITERS=100
TEST_SPAWNFN=L
TEST_MODE=P
TEST_DEPTH=32
TEST_DUPLEX=H
AMUDP_NETWORKDEPTH=$(TEST_DEPTH)
run-tests: tests
	./testping $(TEST_NODES) $(TEST_SPAWNFN) $(TEST_ITERS) $(TEST_MODE)
	./testlatency $(TEST_NODES) $(TEST_SPAWNFN) $(TEST_ITERS) $(TEST_MODE)
	./testlatencyM $(TEST_NODES) $(TEST_SPAWNFN) $(TEST_ITERS) $(TEST_MODE) 64
	./testbulk $(TEST_NODES) $(TEST_SPAWNFN) $(TEST_ITERS) 1048576 $(TEST_MODE) $(TEST_DUPLEX)
	./testam $(TEST_NODES) $(TEST_SPAWNFN) $(TEST_ITERS) $(TEST_MODE)
	./testbounce $(TEST_NODES) $(TEST_SPAWNFN) $(TEST_ITERS) $(TEST_MODE) 
	./testreduce $(TEST_NODES) $(TEST_SPAWNFN) 
if PLATFORM_ILP32
	./testgetput $(TEST_NODES) $(TEST_SPAWNFN) $(TEST_ITERS) 
	./testreadwrite $(TEST_NODES) $(TEST_SPAWNFN) $(TEST_ITERS) 
endif

test%.o : $(testdir)/test%.c $(testdir)/apputils.h ${headers}
	${compile} -DAMUDP -I$(testdir) -c $< -o $@

apputils.o : $(testdir)/apputils.c $(testdir)/apputils.h ${headers}
	${compile} -DAMUDP -I$(testdir) -c $(testdir)/apputils.c -o apputils.o

%.o: %.c $(headers)
	${compile} -o $@ -c $<

%.o: %.cpp $(headers)
	${cxxcompile} -o $@ -c $<

if USE_UDP_CONDUIT
clean-local:
	rm -f libamudp.a $(testprograms:%=%$(EXESUFFIX)) *.o core
endif

force:

.PHONY: force clean-local tests run-tests


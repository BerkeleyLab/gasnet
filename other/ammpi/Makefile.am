## Process this file with automake to produce Makefile.in	-*- makefile -*-
# NOTE: This Makefile is used to tie in with the GASNet build infrastructure
#       the stand-alone AMMPI product doesn't currently provide an automake framework

AUTOMAKE_OPTIONS = foreign 1.4 no-dependencies

CC = @MPI_CC@
CFLAGS = @MPI_CFLAGS@ 

endianness = @ENDIANNESS@
platform_defines = @GASNET_MACHINE_DEFINES@ -DAMMPI_DISABLE_AMTAGS
platform_libraries = @LIBS@ @MPI_LIBS@

INCLUDES = -I$(srcdir) ${platform_defines} $(endianness)
link = $(CC) ${CFLAGS} ${platform_defines} ${INCLUDES} 
linkend = ${platform_libraries}

if USE_MPI_CONDUIT
lib_LIBRARIES = libammpi.a
endif
libammpi_a_SOURCES = \
    ammpi_ep.c       \
    ammpi_reqrep.c   \
    ammpi_spmd.c     \
    ammpi.h          \
    ammpi_internal.h \
    ammpi_spmd.h

#EXTRA_libammpi_SOURCES = 


EXTRA_DIST =        \
    apputils.c      \
    apputils.h      \
    testammpi.c     \
    testbounce.c    \
    testbulk.c      \
    testgetput.c    \
    testlatency.c   \
    testlatency2.c  \
    testping.c      \
    testreadwrite.c \
    testreduce.c    

# ---------- test programs -------------
# all the test executables
testprograms =    \
    testammpi     \
    testbounce    \
    testbulk      \
    testgetput    \
    testlatency   \
    testping      \
    testreadwrite \
    testreduce

tests: ${testprograms}

test% : test%.o apputils.o libammpi.a 
	${link} -o $@ $< apputils.o -L. -lammpi ${linkend}

if USE_MPI_CONDUIT
clean-local:
	rm -f libammpi.a $(testprograms) *.o core
endif

force:

.PHONY: force clean-local


#=============================================================================
# GasNetPlatform.cmake - Platform and OS detection
#=============================================================================

#---------------------------------------------------------------------
# Canonical system identification
#---------------------------------------------------------------------
# These are used to set GASNETI_PLATFORM_COMPILER_* defines that
# gasnet_portable_platform.h uses for compiler identity verification.

include(CheckTypeSize)

check_type_size("void *" SIZEOF_VOID_P BUILTIN_TYPES_ONLY)
check_type_size("size_t" SIZEOF_SIZE_T BUILTIN_TYPES_ONLY)
if(NOT DEFINED SIZEOF_VOID_P)
  set(SIZEOF_VOID_P 8)
endif()
if(NOT DEFINED SIZEOF_SIZE_T)
  set(SIZEOF_SIZE_T 8)
endif()

#---------------------------------------------------------------------
# Detect endianness
#---------------------------------------------------------------------
include(TestBigEndian)
test_big_endian(WORDS_BIGENDIAN)

#---------------------------------------------------------------------
# Detect OS for platform identification
#---------------------------------------------------------------------
if(APPLE)
  set(GASNET_PLATFORM_OS "DARWIN")
  set(GASNETI_ARCH_DARWIN 1)
elseif(UNIX)
  if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    set(GASNET_PLATFORM_OS "LINUX")
    set(GASNETI_ARCH_LINUX 1)
  elseif(CMAKE_SYSTEM_NAME STREQUAL "FreeBSD")
    set(GASNET_PLATFORM_OS "FREEBSD")
  elseif(CMAKE_SYSTEM_NAME STREQUAL "SunOS")
    set(GASNET_PLATFORM_OS "SOLARIS")
  elseif(CMAKE_SYSTEM_NAME STREQUAL "CYGWIN_NT")
    set(GASNET_PLATFORM_OS "CYGWIN")
    set(GASNETI_ARCH_CYGWIN 1)
  endif()
elseif(MSVC)
  set(GASNET_PLATFORM_OS "MSWINDOWS")
endif()

#---------------------------------------------------------------------
# Detect CPU architecture
#---------------------------------------------------------------------
set(GASNET_PLATFORM_ARCH "")
if(CMAKE_SYSTEM_PROCESSOR MATCHES "x86_64|AMD64|amd64")
  set(GASNET_PLATFORM_ARCH "X86_64")
  set(GASNETI_ARCH_X86_64 1)
elseif(CMAKE_SYSTEM_PROCESSOR MATCHES "i[3-6]86")
  set(GASNET_PLATFORM_ARCH "X86")
  set(GASNETI_ARCH_X86 1)
elseif(CMAKE_SYSTEM_PROCESSOR MATCHES "aarch64|ARM64|arm64")
  set(GASNET_PLATFORM_ARCH "AARCH64")
  set(GASNETI_ARCH_AARCH64 1)
elseif(CMAKE_SYSTEM_PROCESSOR MATCHES "^arm")
  set(GASNET_PLATFORM_ARCH "ARM")
  set(GASNETI_ARCH_ARM 1)
elseif(CMAKE_SYSTEM_PROCESSOR MATCHES "ppc64|powerpc64")
  set(GASNET_PLATFORM_ARCH "PPC64")
  set(GASNETI_ARCH_PPC64 1)
elseif(CMAKE_SYSTEM_PROCESSOR MATCHES "ppc|powerpc")
  set(GASNET_PLATFORM_ARCH "POWERPC")
elseif(CMAKE_SYSTEM_PROCESSOR MATCHES "mips")
  set(GASNET_PLATFORM_ARCH "MIPS")
elseif(CMAKE_SYSTEM_PROCESSOR MATCHES "sparc")
  set(GASNET_PLATFORM_ARCH "SPARC")
elseif(CMAKE_SYSTEM_PROCESSOR MATCHES "riscv")
  set(GASNET_PLATFORM_ARCH "RISCV")
endif()

#---------------------------------------------------------------------
# Detect common platform features
#---------------------------------------------------------------------
include(CheckIncludeFile)
include(CheckFunctionExists)
include(CheckSymbolExists)

check_include_file("features.h" HAVE_FEATURES_H)
check_include_file("inttypes.h" HAVE_INTTYPES_H)
check_include_file("stdint.h" HAVE_STDINT_H)
check_include_file("sys/types.h" HAVE_SYS_TYPES_H)
check_include_file("execinfo.h" HAVE_EXECINFO_H)

#---------------------------------------------------------------------
# Detect mmap support
#---------------------------------------------------------------------
check_function_exists("mmap" HAVE_MMAP)
check_include_file("sys/mman.h" HAVE_SYS_MMAN_H)
if(HAVE_SYS_MMAN_H)
  check_symbol_exists("MAP_ANON" "sys/mman.h" HAVE_MAP_ANON)
  check_symbol_exists("MAP_ANONYMOUS" "sys/mman.h" HAVE_MAP_ANONYMOUS)
  check_symbol_exists("MAP_NORESERVE" "sys/mman.h" HAVE_MAP_NORESERVE)
endif()

#---------------------------------------------------------------------
# POSIX functions
#---------------------------------------------------------------------
check_function_exists("posix_memalign" HAVE_POSIX_MEMALIGN)
check_function_exists("clock_gettime" HAVE_CLOCK_GETTIME)
check_function_exists("sched_yield" HAVE_SCHED_YIELD)
check_function_exists("usleep" HAVE_USLEEP)
check_function_exists("nanosleep" HAVE_NANOSLEEP)

check_symbol_exists("snprintf" "stdio.h" HAVE_SNPRINTF_DECL)
check_symbol_exists("vsnprintf" "stdio.h" HAVE_VSNPRINTF_DECL)
check_symbol_exists("isblank" "ctype.h" HAVE_ISBLANK_DECL)

#---------------------------------------------------------------------
# Signal handling
#---------------------------------------------------------------------
check_function_exists("sigaction" HAVE_SIGACTION)
if(HAVE_SIGACTION)
  check_symbol_exists("SA_RESTART" "signal.h" GASNETI_HAVE_SA_RESTART)
endif()

#---------------------------------------------------------------------
# Backtrace support
#---------------------------------------------------------------------
check_function_exists("backtrace" HAVE_BACKTRACE)
check_function_exists("backtrace_symbols" HAVE_BACKTRACE_SYMBOLS)

#---------------------------------------------------------------------
# prctl support (Linux)
#---------------------------------------------------------------------
check_symbol_exists("PR_SET_PDEATHSIG" "sys/prctl.h" HAVE_PR_SET_PDEATHSIG)
check_symbol_exists("PR_SET_PTRACER" "sys/prctl.h" HAVE_PR_SET_PTRACER)

#---------------------------------------------------------------------
# Page size detection
#---------------------------------------------------------------------
include(CheckCSourceRuns)
check_c_source_runs("
#include <unistd.h>
int main() { long sz = sysconf(_SC_PAGESIZE); return (sz > 0) ? 0 : 1; }
" GASNET_PAGESIZE_RUNNABLE)

if(GASNET_PAGESIZE_RUNNABLE)
  # Will be determined at runtime via sysconf
  set(GASNETI_PAGESIZE 0)
  set(GASNETI_PAGESHIFT 0)
else()
  set(GASNETI_PAGESIZE 4096)
  set(GASNETI_PAGESHIFT 12)
endif()

#---------------------------------------------------------------------
# Cache line size detection
#---------------------------------------------------------------------
set(GASNETI_CACHE_LINE_BYTES 64 CACHE STRING "Cache line size in bytes")
if(GASNETI_CACHE_LINE_BYTES EQUAL 128)
  set(GASNETI_CACHE_LINE_SHIFT 7)
elseif(GASNETI_CACHE_LINE_BYTES EQUAL 64)
  set(GASNETI_CACHE_LINE_SHIFT 6)
elseif(GASNETI_CACHE_LINE_BYTES EQUAL 32)
  set(GASNETI_CACHE_LINE_SHIFT 5)
else()
  set(GASNETI_CACHE_LINE_SHIFT 0)
endif()

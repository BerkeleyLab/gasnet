#=============================================================================
# GasNetDebugMalloc.cmake - Debug malloc configuration
#=============================================================================

option(GASNET_ENABLE_SYSTEM_DEBUG_MALLOC "Use system-specific debugging malloc" OFF)

if(GASNET_ENABLE_DEBUG_MALLOC)
  set(GASNETI_MALLOC_CONFIG "debugmalloc")
else()
  set(GASNETI_MALLOC_CONFIG "nodebugmalloc")
endif()

if(GASNET_ENABLE_SYSTEM_DEBUG_MALLOC)
  if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    # glibc MALLOC_CHECK_ support
    set(GASNET_DEBUGMALLOC_VAR "MALLOC_CHECK_")
    set(GASNET_DEBUGMALLOC_VAL "3")
  elseif(CMAKE_SYSTEM_NAME STREQUAL "FreeBSD" OR CMAKE_SYSTEM_NAME STREQUAL "NetBSD")
    set(GASNET_DEBUGMALLOC_VAR "MALLOC_OPTIONS")
    set(GASNET_DEBUGMALLOC_VAL "AJ")
  elseif(CMAKE_SYSTEM_NAME STREQUAL "OpenBSD")
    set(GASNET_DEBUGMALLOC_VAR "MALLOC_OPTIONS")
    set(GASNET_DEBUGMALLOC_VAL "AFGJ")
  endif()
endif()

# ptmalloc detection
include(CheckCSourceCompiles)
check_c_source_compiles("
  #include <malloc.h>
  int main(void) { return mallopt(M_MMAP_THRESHOLD, 128*1024); }
" HAVE_PTMALLOC)

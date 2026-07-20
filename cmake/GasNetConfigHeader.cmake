#=============================================================================
# GasNetConfigHeader.cmake - Compute variables and generate gasnet_config.h
#=============================================================================

#---------------------------------------------------------------------
# Compute all template variables
#---------------------------------------------------------------------

# Debug/optimization
if(GASNET_ENABLE_DEBUG)
  set(GASNET_DEBUG_DEFINE "#define GASNET_DEBUG 1")
  set(GASNET_NDEBUG_DEFINE "/* #undef GASNET_NDEBUG */")
else()
  set(GASNET_DEBUG_DEFINE "/* #undef GASNET_DEBUG */")
  set(GASNET_NDEBUG_DEFINE "#define GASNET_NDEBUG 1")
endif()

# Trace/stats/srclines/debug-malloc/verbose/valgrind
foreach(_opt TRACE STATS SRCLINES DEBUGMALLOC DEBUG_VERBOSE VALGRIND)
  string(TOUPPER "GASNET_ENABLE_${_opt}" _var)
  if(${_var})
    set(_def "GASNET_${_opt}_DEFINE" "#define GASNET_${_opt} 1" CACHE INTERNAL "")
    if(_opt STREQUAL "VALGRIND")
      set(_def "GASNETI_${_opt}_DEFINE" "#define GASNETI_${_opt} 1" CACHE INTERNAL "")
    endif()
  else()
    set(_def "GASNET_${_opt}_DEFINE" "/* #undef GASNET_${_opt} */" CACHE INTERNAL "")
    if(_opt STREQUAL "VALGRIND")
      set(_def "GASNETI_${_opt}_DEFINE" "/* #undef GASNETI_${_opt} */" CACHE INTERNAL "")
    endif()
  endif()
endforeach()

# Handle trace/stats/srclines/debug-malloc/debug-verbose/valgrind properly
if(GASNET_ENABLE_TRACE)
  set(GASNET_TRACE_DEFINE "#define GASNET_TRACE 1")
else()
  set(GASNET_TRACE_DEFINE "/* #undef GASNET_TRACE */")
endif()
if(GASNET_ENABLE_STATS)
  set(GASNET_STATS_DEFINE "#define GASNET_STATS 1")
else()
  set(GASNET_STATS_DEFINE "/* #undef GASNET_STATS */")
endif()
if(GASNET_ENABLE_SRCLINES)
  set(GASNET_SRCLINES_DEFINE "#define GASNET_SRCLINES 1")
else()
  set(GASNET_SRCLINES_DEFINE "/* #undef GASNET_SRCLINES */")
endif()
if(GASNET_ENABLE_DEBUG_MALLOC)
  set(GASNET_DEBUGMALLOC_DEFINE "#define GASNET_DEBUGMALLOC 1")
else()
  set(GASNET_DEBUGMALLOC_DEFINE "/* #undef GASNET_DEBUGMALLOC */")
endif()
if(GASNET_ENABLE_DEBUG_VERBOSE)
  set(GASNET_DEBUG_VERBOSE_DEFINE "#define GASNET_DEBUG_VERBOSE 1")
else()
  set(GASNET_DEBUG_VERBOSE_DEFINE "/* #undef GASNET_DEBUG_VERBOSE */")
endif()
if(GASNET_ENABLE_VALGRIND)
  set(GASNETI_VALGRIND_DEFINE "#define GASNETI_VALGRIND 1")
else()
  set(GASNETI_VALGRIND_DEFINE "/* #undef GASNETI_VALGRIND */")
endif()

# Segment
if(GASNET_SEGMENT_CONFIG STREQUAL "FAST")
  set(GASNET_SEGMENT_DEFINE "#define GASNET_SEGMENT_FAST 1")
elseif(GASNET_SEGMENT_CONFIG STREQUAL "LARGE")
  set(GASNET_SEGMENT_DEFINE "#define GASNET_SEGMENT_LARGE 1")
else()
  set(GASNET_SEGMENT_DEFINE "#define GASNET_SEGMENT_EVERYTHING 1")
endif()
if(NOT GASNET_ENABLE_ALIGNED_SEGMENTS)
  set(GASNETI_DISABLE_ALIGNED_SEGMENTS_DEFINE "#define GASNETI_DISABLE_ALIGNED_SEGMENTS 1")
else()
  set(GASNETI_DISABLE_ALIGNED_SEGMENTS_DEFINE "/* #undef GASNETI_DISABLE_ALIGNED_SEGMENTS */")
endif()

# Compiler identity is set by GasNetCompiler.cmake
# If not set, provide defaults
if(NOT DEFINED GASNET_CC_FAMILYID)
  set(GASNET_CC_FAMILYID 1)
endif()
if(NOT DEFINED GASNET_CC_PLATFORM_ID)
  set(GASNET_CC_PLATFORM_ID ${GASNET_CC_FAMILYID})
endif()
if(NOT DEFINED GASNET_CC_VERSION)
  set(GASNET_CC_VERSION 0)
endif()
if(NOT DEFINED GASNET_CXX_FAMILYID)
  set(GASNET_CXX_FAMILYID 1)
endif()
if(NOT DEFINED GASNET_CXX_PLATFORM_ID)
  set(GASNET_CXX_PLATFORM_ID ${GASNET_CXX_FAMILYID})
endif()
if(NOT DEFINED GASNET_CXX_VERSION)
  set(GASNET_CXX_VERSION 0)
endif()

# Compiler versions (match gasnet_portable_platform.h encoding)
if(DEFINED GASNET_CC_VERSION)
  set(GASNET_CC_VERSION "${GASNET_CC_VERSION}")
else()
  set(GASNET_CC_VERSION 0)
endif()
if(DEFINED GASNET_CXX_VERSION)
  set(GASNET_CXX_VERSION "${GASNET_CXX_VERSION}")
else()
  set(GASNET_CXX_VERSION 0)
endif()
set(GASNET_CC_LANGLVL 201112)
set(GASNET_CXX_LANGLVL 201402)

# Endianness (must use #undef for 0, NOT #define to 0 - see gasnet_portable_platform.h:1070)
if(WORDS_BIGENDIAN)
  set(WORDS_BIGENDIAN_VAL "1")
else()
  set(WORDS_BIGENDIAN_VAL "/* #undef WORDS_BIGENDIAN */")
endif()

# Atomics (numeric values 0-5 from gasnet_atomic_fwd.h)
if(NOT DEFINED GASNETI_ATOMIC_IMPL_CONFIGURE)
  set(GASNETI_ATOMIC_IMPL_CONFIGURE 1)
endif()
if(NOT DEFINED GASNETI_ATOMIC32_IMPL_CONFIGURE)
  set(GASNETI_ATOMIC32_IMPL_CONFIGURE 1)
endif()
if(NOT DEFINED GASNETI_ATOMIC64_IMPL_CONFIGURE)
  set(GASNETI_ATOMIC64_IMPL_CONFIGURE 1)
endif()

# Thread info opt
if(NOT DEFINED GASNETI_THREADINFO_OPT_CONFIGURE)
  set(GASNETI_THREADINFO_OPT_CONFIGURE 0)
endif()

# PSHM (must use #define/#undef pattern since code uses #if VAR check)
if(GASNET_PSHM_ENABLED)
  set(GASNETI_PSHM_ENABLED_VAL "#define GASNETI_PSHM_ENABLED 1")
  if(GASNET_PSHM_POSIX_ENABLED)
    set(GASNETI_PSHM_POSIX_VAL "#define GASNETI_PSHM_POSIX 1")
    set(GASNETI_PSHM_SYSV_VAL "/* #undef GASNETI_PSHM_SYSV */")
    set(GASNETI_PSHM_XPMEM_VAL "/* #undef GASNETI_PSHM_XPMEM */")
    set(GASNETI_PSHM_HUGETLBFS_VAL "/* #undef GASNETI_PSHM_HUGETLBFS */")
    set(GASNETI_PSHM_FILE_VAL "/* #undef GASNETI_PSHM_FILE */")
  else()
    set(GASNETI_PSHM_POSIX_VAL "/* #undef GASNETI_PSHM_POSIX */")
  endif()
else()
  set(GASNETI_PSHM_ENABLED_VAL "/* #undef GASNETI_PSHM_ENABLED */")
endif()

# Default PSHM values if not set
if(NOT DEFINED GASNETI_PSHM_POSIX_VAL)
  set(GASNETI_PSHM_POSIX_VAL "/* #undef GASNETI_PSHM_POSIX */")
endif()
if(NOT DEFINED GASNETI_PSHM_SYSV_VAL)
  set(GASNETI_PSHM_SYSV_VAL "/* #undef GASNETI_PSHM_SYSV */")
endif()
if(NOT DEFINED GASNETI_PSHM_XPMEM_VAL)
  set(GASNETI_PSHM_XPMEM_VAL "/* #undef GASNETI_PSHM_XPMEM */")
endif()
if(NOT DEFINED GASNETI_PSHM_HUGETLBFS_VAL)
  set(GASNETI_PSHM_HUGETLBFS_VAL "/* #undef GASNETI_PSHM_HUGETLBFS */")
endif()
if(NOT DEFINED GASNETI_PSHM_FILE_VAL)
  set(GASNETI_PSHM_FILE_VAL "/* #undef GASNETI_PSHM_FILE */")
endif()

# GASNETI_COMMON - tentative definitions attribute
# Must always be defined (even if empty) since it's used as a type attribute
set(GASNETI_COMMON_VAL "#define GASNETI_COMMON")

# GASNETI_MAX_SEGSIZE_CONFIGURE - default max segment size
set(GASNETI_MAX_SEGSIZE_CONFIGURE "\"0.8\"")

# For features checked with #ifdef (not #if), must produce #define or /*#undef*/
# NOT #define to 0 which would still pass #ifdef
foreach(_f PR_SET_PDEATHSIG PR_SET_PTRACER)
  if(${HAVE_${_f}})
    set(HAVE_${_f}_VAL "#define HAVE_${_f} 1")
  else()
    set(HAVE_${_f}_VAL "/* #undef HAVE_${_f} */")
  endif()
endforeach()

# GASNETI_SYSTEM_TUPLE
set(GASNETI_SYSTEM_TUPLE "\"${CMAKE_SYSTEM_PROCESSOR}-${CMAKE_SYSTEM_NAME}\"")

# Build identification strings
set(GASNETI_SYSTEM_NAME "\"${CMAKE_SYSTEM_NAME}\"")
set(GASNETI_BUILD_ID "\"cmake-build\"")
set(GASNETI_CONFIGURE_ARGS "\"cmake\"")

# CONFIGURE_ARGS (legacy)
set(CONFIGURE_ARGS "\"cmake\"")

# Page/cache sizes
if(NOT DEFINED GASNETI_PAGESIZE OR GASNETI_PAGESIZE EQUAL 0)
  set(GASNETI_PAGESIZE 4096)
  set(GASNETI_PAGESHIFT 12)
endif()

# C99 format specifiers
if(HAVE_C99_FORMAT_SPECIFIERS)
  set(HAVE_C99_FORMAT_SPECIFIERS 1)
else()
  set(HAVE_C99_FORMAT_SPECIFIERS 0)
endif()

# Pause instruction
if(GASNETI_HAVE_PAUSE)
  # Already set by GasNetMembar.cmake
else()
  set(GASNETI_PAUSE_INSTRUCTION "\"\"")
endif()

# Force options
foreach(_f GENERIC_ATOMICOPS OS_ATOMICOPS COMPILER_ATOMICOPS YIELD_MEMBARS SLOW_MEMBARS GETTIMEOFDAY POSIX_REALTIME)
  if(GASNET_FORCE_${_f})
    set(GASNETI_FORCE_${_f}_DEFINE "#define GASNETI_FORCE_${_f} 1")
  else()
    set(GASNETI_FORCE_${_f}_DEFINE "/* #undef GASNETI_FORCE_${_f} */")
  endif()
endforeach()

# BUG1389 workaround
if(GASNET_ENABLE_CONSERVATIVE_LOCAL_COPY)
  set(GASNETI_BUG1389_WORKAROUND_DEFINE "#define GASNETI_BUG1389_WORKAROUND 1")
else()
  set(GASNETI_BUG1389_WORKAROUND_DEFINE "/* #undef GASNETI_BUG1389_WORKAROUND */")
endif()

# Cross-compiling
if(CMAKE_CROSSCOMPILING)
  set(GASNETI_CROSS_COMPILING 1)
endif()

# Apple GCC
if(CC_FAMILY STREQUAL "Clang" AND CC_SUBFAMILY STREQUAL "APPLE")
  set(GASNETI_GCC_APPLE 1)
endif()

# Inline modifier
set(GASNETI_CC_INLINE_MODIFIER "inline")
set(GASNETI_CXX_INLINE_MODIFIER "inline")

# SIZEOF_SIZE_T
if(NOT DEFINED SIZEOF_SIZE_T OR SIZEOF_SIZE_T STREQUAL "")
  set(SIZEOF_SIZE_T 8)
endif()

# GASNETI_TM0_ALIGN - thread model alignment
if(NOT DEFINED GASNETI_TM0_ALIGN)
  set(GASNETI_TM0_ALIGN 16)
endif()

# GASNETC_SMP_SPAWNER_CONF - spawner for smp conduit
set(GASNETC_SMP_SPAWNER_CONF "\"fork\"")
set(GASNETC_IBV_SPAWNER_CONF "\"ssh\"")
set(GASNETC_OFI_SPAWNER_CONF "\"ssh\"")
set(GASNETC_UCX_SPAWNER_CONF "\"ssh\"")
set(GASNETC_MPI_SPAWNER_CONF "\"mpi\"")

# Enabled conduits list
set(_conduits "")
if(GASNET_ENABLE_SMP)
  list(APPEND _conduits "smp")
endif()
if(GASNET_ENABLE_UDP)
  list(APPEND _conduits "udp")
endif()
if(GASNET_ENABLE_MPI)
  list(APPEND _conduits "mpi")
endif()
if(GASNET_ENABLE_IBV)
  list(APPEND _conduits "ibv")
endif()
if(GASNET_ENABLE_OFI)
  list(APPEND _conduits "ofi")
endif()
if(GASNET_ENABLE_UCX)
  list(APPEND _conduits "ucx")
endif()
string(JOIN " " _g_conduits ${_conduits})
set(GASNETI_CONDUITS "\"${_g_conduits}\"")

# Restrict keyword (GCC/Clang support __restrict__)
set(GASNETI_CC_RESTRICT "__restrict__")
set(GASNETI_CXX_RESTRICT "__restrict__")

#---------------------------------------------------------------------
# Generate the config header
#---------------------------------------------------------------------
set(GASNET_CONFIG_H "${CMAKE_CURRENT_BINARY_DIR}/gasnet_config.h")
set(GASNET_CONFIG_H_IN "${CMAKE_CURRENT_SOURCE_DIR}/cmake/gasnet_config.h.in")

configure_file(
  "${GASNET_CONFIG_H_IN}"
  "${GASNET_CONFIG_H}"
  @ONLY
)

include_directories(BEFORE "${CMAKE_CURRENT_BINARY_DIR}")
message(STATUS "Generated gasnet_config.h in ${CMAKE_CURRENT_BINARY_DIR}")

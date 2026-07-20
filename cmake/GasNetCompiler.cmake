#=============================================================================
# GasNetCompiler.cmake - Compiler detection and feature probing for GASNet
#
# GASNet probes THREE compilers independently (CC, CXX, MPI_CC) and
# cross-references their detected features at compile time via gasnet_basic.h.
#=============================================================================

include(CheckCSourceCompiles)
include(CheckCXXSourceCompiles)
include(CheckIncludeFile)

#---------------------------------------------------------------------
# Compiler identification - map CMake compiler ID to GASNet family
#---------------------------------------------------------------------
function(gasnet_detect_compiler_family lang_var family_var subfamily_var)
  if(CMAKE_${lang_var}_COMPILER_ID STREQUAL "GNU")
    set(${family_var} "GNU" PARENT_SCOPE)
    set(${subfamily_var} "" PARENT_SCOPE)
  elseif(CMAKE_${lang_var}_COMPILER_ID STREQUAL "Clang" OR CMAKE_${lang_var}_COMPILER_ID STREQUAL "AppleClang")
    set(${family_var} "Clang" PARENT_SCOPE)
    if(CMAKE_${lang_var}_COMPILER_ID STREQUAL "AppleClang")
      set(${subfamily_var} "APPLE" PARENT_SCOPE)
    else()
      set(${subfamily_var} "" PARENT_SCOPE)
    endif()
  elseif(CMAKE_${lang_var}_COMPILER_ID STREQUAL "Intel" OR CMAKE_${lang_var}_COMPILER_ID STREQUAL "IntelLLVM")
    set(${family_var} "Intel" PARENT_SCOPE)
    set(${subfamily_var} "" PARENT_SCOPE)
  elseif(CMAKE_${lang_var}_COMPILER_ID STREQUAL "PGI" OR CMAKE_${lang_var}_COMPILER_ID STREQUAL "NVHPC")
    set(${family_var} "NVHPC" PARENT_SCOPE)
    set(${subfamily_var} "" PARENT_SCOPE)
  else()
    set(${family_var} "Unknown" PARENT_SCOPE)
    set(${subfamily_var} "" PARENT_SCOPE)
  endif()
endfunction()

gasnet_detect_compiler_family("C" CC_FAMILY CC_SUBFAMILY)
gasnet_detect_compiler_family("CXX" CXX_FAMILY CXX_SUBFAMILY)

#---------------------------------------------------------------------
# Platform compiler identity (must match gasnet_portable_platform.h)
# In the current version, PLATFORM_COMPILER_ID == PLATFORM_COMPILER_FAMILYID
#---------------------------------------------------------------------
if(CC_FAMILY STREQUAL "GNU")
  set(GASNET_CC_FAMILYID 1)
elseif(CC_FAMILY STREQUAL "Intel")
  set(GASNET_CC_FAMILYID 2)
elseif(CC_FAMILY STREQUAL "NVHPC")
  set(GASNET_CC_FAMILYID 4)
elseif(CC_FAMILY STREQUAL "XLC")
  set(GASNET_CC_FAMILYID 5)
elseif(CC_FAMILY STREQUAL "Sun")
  set(GASNET_CC_FAMILYID 7)
elseif(CC_FAMILY STREQUAL "Cray")
  set(GASNET_CC_FAMILYID 10)
elseif(CC_FAMILY STREQUAL "Clang")
  set(GASNET_CC_FAMILYID 19)
else()
  set(GASNET_CC_FAMILYID 1)
endif()
set(GASNET_CC_PLATFORM_ID ${GASNET_CC_FAMILYID})

if(CXX_FAMILY STREQUAL "GNU")
  set(GASNET_CXX_FAMILYID 1)
elseif(CXX_FAMILY STREQUAL "Clang")
  set(GASNET_CXX_FAMILYID 19)
else()
  set(GASNET_CXX_FAMILYID 1)
endif()
set(GASNET_CXX_PLATFORM_ID ${GASNET_CXX_FAMILYID})

#---------------------------------------------------------------------
# Extract actual compiler version from gasnet_portable_platform.h
# We use try_run because the version encoding differs per compiler family
#---------------------------------------------------------------------
file(WRITE "${CMAKE_CURRENT_BINARY_DIR}/cmake_try/plat_ver.c"
"#include <stdio.h>
#include \"${PROJECT_SOURCE_DIR}/other/gasnet_portable_platform.h\"
int main(void) {
  FILE *f = fopen(\"${CMAKE_CURRENT_BINARY_DIR}/cmake_try/plat_ver.txt\", \"w\");
  fprintf(f, \"%d\\n%d\\n\", (int)PLATFORM_COMPILER_VERSION, (int)PLATFORM_COMPILER_FAMILYID);
  fclose(f);
  return 0;
}
")

try_run(GASNET_PLAT_VER_RUN GASNET_PLAT_VER_COMPILE
  "${CMAKE_CURRENT_BINARY_DIR}/cmake_try"
  "${CMAKE_CURRENT_BINARY_DIR}/cmake_try/plat_ver.c"
  COMPILE_OUTPUT_VARIABLE _plat_out
)

if(GASNET_PLAT_VER_COMPILE AND GASNET_PLAT_VER_RUN EQUAL 0)
  file(READ "${CMAKE_CURRENT_BINARY_DIR}/cmake_try/plat_ver.txt" _plat_ver)
  string(REGEX MATCH "^([0-9]+)" _vm "${_plat_ver}")
  set(GASNET_CC_VERSION "${CMAKE_MATCH_1}")
  if(NOT GASNET_CC_VERSION)
    set(GASNET_CC_VERSION 0)
  endif()
else()
  set(GASNET_CC_VERSION 0)
  message(WARNING "Cannot extract PLATFORM_COMPILER_VERSION; compilation may fail")
endif()
set(GASNET_CXX_VERSION ${GASNET_CC_VERSION})

#---------------------------------------------------------------------
# C99 feature support
#---------------------------------------------------------------------
check_c_source_compiles("
  int main(void) {
    // C++/C99 comment
    for (int i = 0; i < 10; i++) { int x = i; }
    long long ll = 0x1234567812345678LL + 0x8765432187654321ULL;
    return (int)ll;
  }
" GASNET_C99_SUBSET_OK)

if(NOT GASNET_C99_SUBSET_OK)
  message(FATAL_ERROR "C compiler does not support GASNet-required ISO C99 subset")
endif()

# __VA_ARGS__ support (required for gasnet_ammacros.h)
check_c_source_compiles("
  #define GASNETI_AMNUMARGS(...) GASNETI_AMNUMARGS_(__VA_ARGS__,16,15,14,13,12,11,10,9,8,7,6,5,4,3,2,1,0,0)
  #define GASNETI_AMNUMARGS_(_0,_1,_2,_3,_4,_5,_6,_7,_8,_9,_10,_11,_12,_13,_14,_15,_16,N,...) N
  int b[GASNETI_AMNUMARGS(1)+1];
  int c[GASNETI_AMNUMARGS(1,2)+1];
  int d[GASNETI_AMNUMARGS(1,2,3)+1];
  int main(void) { return b[0] + c[0] + d[0]; }
" GASNET_VA_ARGS_OK)

if(NOT GASNET_VA_ARGS_OK)
  message(FATAL_ERROR "C compiler does not support __VA_ARGS__")
endif()

#---------------------------------------------------------------------
# Inline assembly support
#---------------------------------------------------------------------
check_c_source_compiles("
  int main(void) {
    __asm__ __volatile__ (\"nop\" : : : \"memory\");
    return 0;
  }
" GASNETI_HAVE_CC_GCC_ASM)

check_cxx_source_compiles("
  int main() {
    __asm__ __volatile__ (\"nop\" : : : \"memory\");
    return 0;
  }
" GASNETI_HAVE_CXX_GCC_ASM)

#---------------------------------------------------------------------
# Compiler builtins
#---------------------------------------------------------------------
check_c_source_compiles("
  int main(void) { int x = 0; if (__builtin_expect(x, 0)) return 1; return 0; }
" GASNETI_HAVE_CC_BUILTIN_EXPECT)

check_c_source_compiles("
  int main(void) { int x = 1; if (__builtin_constant_p(x)) return 1; return 0; }
" GASNETI_HAVE_CC_BUILTIN_CONSTANT_P)

check_c_source_compiles("
  int main(void) { char buf[64]; __builtin_prefetch(buf); return 0; }
" GASNETI_HAVE_CC_BUILTIN_PREFETCH)

check_c_source_compiles("
  int main(void) { __builtin_unreachable(); return 0; }
" GASNETI_HAVE_CC_BUILTIN_UNREACHABLE)

check_c_source_compiles("
  unsigned int bswap32(unsigned int x) { return __builtin_bswap32(x); }
  int main(void) { return bswap32(0x12345678); }
" GASNETI_HAVE_CC_BUILTIN_BSWAP32)

check_c_source_compiles("
  unsigned long long bswap64(unsigned long long x) { return __builtin_bswap64(x); }
  int main(void) { return (int)bswap64(0x1234567812345678ULL); }
" GASNETI_HAVE_CC_BUILTIN_BSWAP64)

check_c_source_compiles("
  int main(void) { return __builtin_clz(1) + __builtin_clzl(1L) + __builtin_clzll(1LL); }
" GASNETI_HAVE_CC_BUILTIN_CLZ)

check_c_source_compiles("
  int main(void) { return __builtin_ctz(2) + __builtin_ctzl(2L) + __builtin_ctzll(2LL); }
" GASNETI_HAVE_CC_BUILTIN_CTZ)

# C99 format specifiers
check_c_source_compiles("
  #include <stdio.h>
  #include <inttypes.h>
  int main(void) { uint64_t x = 0; printf(\"%\" PRIu64, x); return 0; }
" HAVE_C99_FORMAT_SPECIFIERS)

#---------------------------------------------------------------------
# __sync atomics support
#---------------------------------------------------------------------
check_c_source_compiles("
  int main(void) {
    int x = 0;
    return __sync_bool_compare_and_swap(&x, 0, 1) ? __sync_fetch_and_add(&x, 1) : 0;
  }
" GASNETI_HAVE_CC_SYNC_ATOMICS_32)

check_c_source_compiles("
  int main(void) {
    long long x = 0;
    return __sync_bool_compare_and_swap(&x, 0LL, 1LL) ? (int)__sync_fetch_and_add(&x, 1LL) : 0;
  }
" GASNETI_HAVE_CC_SYNC_ATOMICS_64)

check_c_source_compiles("
  int main(void) { __sync_synchronize(); return 0; }
" GASNETI_HAVE_CC_SYNC_SYNCHRONIZE)

#---------------------------------------------------------------------
# CPU-specific features
#---------------------------------------------------------------------
if(GASNET_PLATFORM_ARCH STREQUAL "X86_64")
  check_c_source_compiles("
    int main(void) {
      unsigned long long old_lo = 0, old_hi = 0, new_lo = 1, new_hi = 0;
      __uint128_t old_val = 0, new_val = 1;
      __asm__ __volatile__(\"lock cmpxchg16b %1\" : \"+A\"(old_val) : \"m\"(*((__uint128_t*)&old_lo)), \"b\"(new_lo), \"c\"(new_hi));
      return 0;
    }
  " GASNETI_HAVE_X86_CMPXCHG16B)
endif()

if(GASNET_PLATFORM_ARCH STREQUAL "AARCH64")
  check_c_source_compiles("
    int main(void) {
      unsigned long val;
      __asm__ __volatile__(\"mrs %0, CNTVCT_EL0\" : \"=r\"(val));
      return 0;
    }
  " GASNETI_HAVE_AARCH64_CNTVCT_EL0)
endif()

# PIC detection
check_c_source_compiles("
  #ifndef __PIC__
  #error not PIC
  #endif
  int main(void) { return 0; }
" GASNETI_CONFIGURED_PIC)

# TLS support
check_c_source_compiles("
  __thread int tls_var = 0;
  int main(void) { return tls_var; }
" GASNETI_HAVE_TLS_SUPPORT)

#---------------------------------------------------------------------
# Optimization flags
#---------------------------------------------------------------------
if(GASNET_ENABLE_DEBUG)
  set(GASNET_OPT_CFLAGS "-g -O0" CACHE STRING "Optimization C flags for debug build")
else()
  set(GASNET_OPT_CFLAGS "-O2" CACHE STRING "Optimization C flags for release build")
endif()

#---------------------------------------------------------------------
# Developer warnings
#---------------------------------------------------------------------
if(GASNET_ENABLE_DEV_WARNINGS)
  if(CMAKE_C_COMPILER_ID STREQUAL "GNU" OR CMAKE_C_COMPILER_ID STREQUAL "Clang" OR CMAKE_C_COMPILER_ID STREQUAL "AppleClang")
    set(DEVWARN_CFLAGS "-Wall;-Wextra;-Wno-unused-parameter" CACHE STRING "Developer warning flags")
  endif()
else()
  set(DEVWARN_CFLAGS "" CACHE STRING "Developer warning flags")
endif()

#---------------------------------------------------------------------
# RPATH
#---------------------------------------------------------------------
if(GASNET_ENABLE_RPATH)
  set(CMAKE_INSTALL_RPATH_USE_LINK_PATH TRUE)
endif()

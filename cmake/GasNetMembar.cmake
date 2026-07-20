#=============================================================================
# GasNetMembar.cmake - Memory barrier configuration
#=============================================================================

include(CheckCSourceCompiles)

#---------------------------------------------------------------------
# Detect pause instruction (x86 rep;nop / aarch64 yield / arm yield)
#---------------------------------------------------------------------
if(GASNET_PLATFORM_ARCH STREQUAL "X86_64" OR GASNET_PLATFORM_ARCH STREQUAL "X86")
  check_c_source_compiles("
    int main(void) { __asm__ __volatile__(\"rep; nop\" : : : \"memory\"); return 0; }
  " GASNETI_HAVE_PAUSE)
  if(GASNETI_HAVE_PAUSE)
    set(GASNETI_PAUSE_INSTRUCTION "\"rep; nop\"")
  endif()
elseif(GASNET_PLATFORM_ARCH STREQUAL "AARCH64")
  check_c_source_compiles("
    int main(void) { __asm__ __volatile__(\"yield\" : : : \"memory\"); return 0; }
  " GASNETI_HAVE_PAUSE)
  if(GASNETI_HAVE_PAUSE)
    set(GASNETI_PAUSE_INSTRUCTION "\"yield\"")
  endif()
endif()

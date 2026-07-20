#=============================================================================
# GasNetTimer.cmake - High-performance timer detection
#
# GASNet uses platform-native timers with fallback to POSIX.
# Priority: RDTSC > mach_absolute_time > clock_gettime > gettimeofday
#=============================================================================

# Force options override
if(GASNET_FORCE_GETTIMEOFDAY)
  set(GASNETI_TIMER_IMPL "GETTIMEOFDAY")
  return()
endif()

if(GASNET_FORCE_POSIX_REALTIME AND HAVE_CLOCK_GETTIME)
  set(GASNETI_TIMER_IMPL "POSIX_REALTIME")
  return()
endif()

# Detect sleep functions
include(CheckFunctionExists)
check_function_exists("clock_nanosleep" HAVE_CLOCK_NANOSLEEP)
check_function_exists("nsleep" HAVE_NSLEEP)

# On Linux with x86/x86_64, use RDTSC
if(GASNET_PLATFORM_OS STREQUAL "LINUX" AND GASNETI_HAVE_CC_GCC_ASM)
  if(GASNET_PLATFORM_ARCH STREQUAL "X86_64" OR GASNET_PLATFORM_ARCH STREQUAL "X86")
    set(GASNETI_TIMER_IMPL "RDTSC")
  endif()
endif()

# On macOS, use mach_absolute_time
if(NOT GASNETI_TIMER_IMPL AND APPLE)
  set(GASNETI_TIMER_IMPL "MACH")
endif()

# On AARCH64 with GCC asm, use CNTVCT_EL0
if(NOT GASNETI_TIMER_IMPL AND GASNETI_HAVE_CC_GCC_ASM AND GASNETI_HAVE_AARCH64_CNTVCT_EL0)
  set(GASNETI_TIMER_IMPL "AARCH64")
endif()

# On Cygwin, use QueryPerformanceCounter
if(NOT GASNETI_TIMER_IMPL AND CYGWIN)
  set(GASNETI_TIMER_IMPL "QPC")
endif()

# Fall back to clock_gettime if available
if(NOT GASNETI_TIMER_IMPL AND HAVE_CLOCK_GETTIME)
  set(GASNETI_TIMER_IMPL "POSIX_REALTIME")
endif()

# Ultimate fallback
if(NOT GASNETI_TIMER_IMPL)
  set(GASNETI_TIMER_IMPL "GETTIMEOFDAY")
endif()

message(STATUS "GASNet timer implementation: ${GASNETI_TIMER_IMPL}")

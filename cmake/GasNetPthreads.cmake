#=============================================================================
# GasNetPthreads.cmake - Pthreads detection
#=============================================================================

if(GASNET_PTHREADS_MODE STREQUAL "OFF")
  set(GASNET_HAVE_PTHREADS OFF)
  return()
endif()

find_package(Threads QUIET)

if(NOT Threads_FOUND AND GASNET_PTHREADS_MODE STREQUAL "ON")
  message(FATAL_ERROR "Pthreads requested but not found. Try setting GASNET_PTHREADS_MODE=AUTO or install pthreads.")
endif()

if(Threads_FOUND)
  set(GASNET_HAVE_PTHREADS ON)
  set(GASNET_THREAD_DEFINES "${CMAKE_THREAD_LIBS_INIT}")
  set(GASNET_THREAD_LIBS "${CMAKE_THREAD_LIBS_INIT}")

  # Set thread defines based on platform
  if(APPLE)
    set(GASNET_THREAD_DEFINES "")
  elseif(CMAKE_SYSTEM_NAME STREQUAL "FreeBSD")
    set(GASNET_THREAD_DEFINES "-D_THREAD_SAFE")
  elseif(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    set(GASNET_THREAD_DEFINES "-D_REENTRANT")
  endif()

  # Detect pthread features
  include(CheckFunctionExists)
  include(CheckCSourceCompiles)

  check_function_exists("pthread_setconcurrency" HAVE_PTHREAD_SETCONCURRENCY)
  check_function_exists("pthread_kill" HAVE_PTHREAD_KILL)
  check_function_exists("pthread_sigmask" HAVE_PTHREAD_SIGMASK)

  # Check for pthread_rwlock_t
  check_c_source_compiles("
    #include <pthread.h>
    int main(void) { pthread_rwlock_t rwlock; return 0; }
  " GASNETI_HAVE_PTHREAD_RWLOCK)

  # Thread info optimization
  if(NOT DEFINED GASNETI_THREADINFO_OPT_CONFIGURE)
    if(CMAKE_SYSTEM_PROCESSOR MATCHES "x86_64|AMD64")
      set(GASNETI_THREADINFO_OPT_CONFIGURE 1)
    else()
      set(GASNETI_THREADINFO_OPT_CONFIGURE 0)
    endif()
  endif()

else()
  set(GASNET_HAVE_PTHREADS OFF)
  set(GASNET_THREAD_DEFINES "")
  set(GASNET_THREAD_LIBS "")
endif()

# Threading library build flags
if(GASNET_BUILD_PAR AND NOT GASNET_HAVE_PTHREADS)
  message(WARNING "PAR libraries require pthreads; disabling PAR and PARSYNC builds")
  set(GASNET_BUILD_PAR OFF)
  set(GASNET_BUILD_PARSYNC OFF)
endif()

if(GASNET_BUILD_PARSYNC AND NOT GASNET_HAVE_PTHREADS)
  set(GASNET_BUILD_PARSYNC OFF)
endif()

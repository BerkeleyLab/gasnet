#=============================================================================
# GasNetPSHM.cmake - Inter-Process Shared Memory configuration
#=============================================================================

include(CheckCSourceCompiles)
include(CheckFunctionExists)
include(CheckIncludeFile)

if(GASNET_ENABLE_PSHM)
  # POSIX shared memory (shm_open)
  if(GASNET_ENABLE_PSHM_POSIX)
    check_function_exists("shm_open" HAVE_SHM_OPEN)
    check_function_exists("shm_unlink" HAVE_SHM_UNLINK)
    if(HAVE_SHM_OPEN AND HAVE_SHM_UNLINK)
      set(GASNET_PSHM_POSIX_ENABLED TRUE)
    endif()
  endif()

  # SysV shared memory (shmat/shmget)
  if(CMAKE_SYSTEM_NAME STREQUAL "SunOS" AND NOT GASNET_PSHM_POSIX_ENABLED)
    check_function_exists("shmat" HAVE_SHMAT)
    if(HAVE_SHMAT)
      set(GASNET_PSHM_SYSV_ENABLED TRUE)
    endif()
  endif()

  # hugetlbfs
  if(GASNET_ENABLE_PSHM_HUGETLBFS OR CMAKE_SYSTEM_NAME MATCHES "Cray")
    find_library(HUGETLBFS_LIB hugetlbfs)
    if(HUGETLBFS_LIB)
      set(GASNET_PSHM_HUGETLBFS_ENABLED TRUE)
    endif()
  endif()

  # XPMEM
  if(GASNET_ENABLE_PSHM_XPMEM)
    check_include_file("xpmem.h" HAVE_XPMEM_H)
    check_function_exists("xpmem_make" HAVE_XPMEM)
    if(HAVE_XPMEM_H AND HAVE_XPMEM)
      set(GASNET_PSHM_XPMEM_ENABLED TRUE)
    endif()
  endif()

  if(GASNET_PSHM_POSIX_ENABLED OR GASNET_PSHM_SYSV_ENABLED OR
     GASNET_PSHM_HUGETLBFS_ENABLED OR GASNET_PSHM_XPMEM_ENABLED)
    set(GASNET_PSHM_ENABLED TRUE)
  else()
    set(GASNET_PSHM_ENABLED FALSE)
    message(STATUS "No PSHM mechanism available; disabling PSHM")
  endif()

  # Detect socketpair for PSHM bootstrap
  check_function_exists("socketpair" HAVE_SOCKETPAIR)
endif()

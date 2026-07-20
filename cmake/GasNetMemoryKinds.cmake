#=============================================================================
# GasNetMemoryKinds.cmake - GPU memory kinds detection
#=============================================================================

if(NOT GASNET_ENABLE_MEMORY_KINDS)
  set(GASNET_MEMORY_KINDS_ENABLED FALSE)
  return()
endif()

set(GASNET_MEMORY_KINDS_ENABLED TRUE)

# CUDA UVA
if(GASNET_ENABLE_KIND_CUDA_UVA)
  find_package(CUDA QUIET)
  if(CUDA_FOUND)
    set(GASNET_KIND_CUDA_UVA_ENABLED TRUE)
  else()
    message(STATUS "CUDA not found; disabling CUDA UVA memory kind")
  endif()
endif()

# HIP
if(GASNET_ENABLE_KIND_HIP)
  find_package(hip QUIET)
  if(hip_FOUND OR DEFINED ENV{ROCM_PATH})
    set(GASNET_KIND_HIP_ENABLED TRUE)
  else()
    message(STATUS "HIP not found; disabling HIP memory kind")
  endif()
endif()

# Level Zero
if(GASNET_ENABLE_KIND_ZE)
  find_package(LevelZero QUIET)
  if(LevelZero_FOUND)
    set(GASNET_KIND_ZE_ENABLED TRUE)
  else()
    message(STATUS "Level Zero not found; disabling ZE memory kind")
  endif()
endif()

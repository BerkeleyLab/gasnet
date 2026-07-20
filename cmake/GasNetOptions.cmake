#=============================================================================
# GasNetOptions.cmake - Top-level build options for GASNet
#=============================================================================
include(CMakeDependentOption)

#---------------------------------------------------------------------
# Threading model selection
#---------------------------------------------------------------------
set(GASNET_THREAD_MODEL "SEQ" CACHE STRING "GASNet threading model: SEQ, PAR, or PARSYNC")
set_property(CACHE GASNET_THREAD_MODEL PROPERTY STRINGS "SEQ" "PAR" "PARSYNC")

option(GASNET_BUILD_SEQ "Build SEQ (single-threaded) libraries" ON)
option(GASNET_BUILD_PAR "Build PAR (multi-threaded) libraries" ON)
option(GASNET_BUILD_PARSYNC "Build PARSYNC (multi-threaded, serialized) libraries" ON)

#---------------------------------------------------------------------
# Debug and diagnostics
#---------------------------------------------------------------------
option(GASNET_ENABLE_DEBUG "Build in debugging mode with extensive error/sanity checks" OFF)
cmake_dependent_option(GASNET_ENABLE_TRACE "Build with tracing enabled" ON "GASNET_ENABLE_DEBUG" OFF)
cmake_dependent_option(GASNET_ENABLE_STATS "Build with statistical collection enabled" ON "GASNET_ENABLE_DEBUG" OFF)
cmake_dependent_option(GASNET_ENABLE_DEBUG_MALLOC "Build with debugging malloc" ON "GASNET_ENABLE_DEBUG" OFF)
cmake_dependent_option(GASNET_ENABLE_SRCLINES "Build with source line tracing support" ON "GASNET_ENABLE_TRACE" OFF)
option(GASNET_ENABLE_DEBUG_VERBOSE "Enable verbose debug status messages from GASNet lib" OFF)

#---------------------------------------------------------------------
# Compiler warnings
#---------------------------------------------------------------------
option(GASNET_ENABLE_DEV_WARNINGS "Enable developer compiler warnings for library and tests" ON)

#---------------------------------------------------------------------
# Segment configuration
#---------------------------------------------------------------------
set(GASNET_SEGMENT_CONFIG "FAST" CACHE STRING "Segment configuration: FAST, LARGE, or EVERYTHING")
set_property(CACHE GASNET_SEGMENT_CONFIG PROPERTY STRINGS "FAST" "LARGE" "EVERYTHING")

option(GASNET_ENABLE_ALIGNED_SEGMENTS "Enable aligned segment support" ON)

#---------------------------------------------------------------------
# PSHM (inter-Process Shared Memory)
#---------------------------------------------------------------------
option(GASNET_ENABLE_PSHM "Enable inter-Process Shared Memory support" ON)
option(GASNET_ENABLE_PSHM_POSIX "Enable PSHM via POSIX shared memory" ON)
option(GASNET_ENABLE_PSHM_XPMEM "Enable PSHM via XPMEM" OFF)
option(GASNET_ENABLE_PSHM_HUGETLBFS "Enable PSHM via hugetlbfs" OFF)

#---------------------------------------------------------------------
# Memory kinds support
#---------------------------------------------------------------------
option(GASNET_ENABLE_MEMORY_KINDS "Enable memory kinds support" ON)
option(GASNET_ENABLE_KIND_CUDA_UVA "Enable CUDA UVA memory kind" OFF)
option(GASNET_ENABLE_KIND_HIP "Enable HIP memory kind" OFF)
option(GASNET_ENABLE_KIND_ZE "Enable Level Zero memory kind" OFF)

#---------------------------------------------------------------------
# Valgrind support
#---------------------------------------------------------------------
option(GASNET_ENABLE_VALGRIND "Build valgrind-friendly library (disables some optimizations)" OFF)

#---------------------------------------------------------------------
# RPATH
#---------------------------------------------------------------------
option(GASNET_ENABLE_RPATH "Build libraries using RPATH for dependent libraries" OFF)

#---------------------------------------------------------------------
# Pthreads
#---------------------------------------------------------------------
set(GASNET_PTHREADS_MODE "AUTO" CACHE STRING "Pthreads support: AUTO, ON, or OFF")
set_property(CACHE GASNET_PTHREADS_MODE PROPERTY STRINGS "AUTO" "ON" "OFF")

#---------------------------------------------------------------------
# Misc options
#---------------------------------------------------------------------
option(GASNET_ENABLE_CONSERVATIVE_LOCAL_COPY "Enable slower conservative local data movement" OFF)
option(GASNET_ENABLE_GASNET_VERBOSE "Build with verbose debug status messages in library" OFF)
option(GASNET_ENABLE_AUTO_CONDUIT_DETECT "Automatically detect supported network conduits" ON)

#---------------------------------------------------------------------
# Conduit selection
#---------------------------------------------------------------------
option(GASNET_ENABLE_SMP "Enable SMP (shared memory) conduit" ON)
option(GASNET_ENABLE_UDP "Enable UDP conduit" OFF)
option(GASNET_ENABLE_MPI "Enable MPI conduit" OFF)
option(GASNET_ENABLE_IBV "Enable InfiniBand Verbs conduit" OFF)
option(GASNET_ENABLE_OFI "Enable Open Fabrics Interfaces conduit" OFF)
option(GASNET_ENABLE_UCX "Enable UCX conduit" OFF)

#---------------------------------------------------------------------
# MPI compatibility (required by some conduits)
#---------------------------------------------------------------------
option(GASNET_ENABLE_MPI_COMPAT "Enable MPI compatibility support" ON)

#---------------------------------------------------------------------
# Conduit-specific options
#---------------------------------------------------------------------
# IBV
option(GASNET_IBV_ODP "Enable On-Demand Paging for ibv-conduit" OFF)
option(GASNET_IBV_XRC "Enable XRC support for ibv-conduit" OFF)
option(GASNET_IBV_FENCED_PUTS "Enable fenced puts for ibv-conduit" ON)
set(GASNET_IBV_MAX_HCAS "1" CACHE STRING "Max HCAs for ibv-conduit")
set(GASNET_IBV_MAX_MEDIUM "4096" CACHE STRING "Max medium size for ibv-conduit")

# OFI
set(GASNET_OFI_PROVIDER "auto" CACHE STRING "libfabric provider for ofi-conduit (auto=sockets;verbs;ofi_rxm)")
option(GASNET_OFI_LEGACY_EXTENDED "Enable legacy extended endpoint support" OFF)
set(GASNET_OFI_MAX_MEDIUM "4096" CACHE STRING "Max medium size for ofi-conduit")

# UCX
set(GASNET_UCX_MAX_MEDIUM "4096" CACHE STRING "Max medium size for ucx-conduit")

#---------------------------------------------------------------------
# Advanced options
#---------------------------------------------------------------------
set(GASNET_MAX_SEGSIZE "auto" CACHE STRING "Default max segment size (auto = 0.8 * physical memory)")
set(GASNET_WITH_SSH_CMD "" CACHE STRING "SSH command for ssh-spawner")
set(GASNET_WITH_SSH_OPTIONS "" CACHE STRING "SSH options for ssh-spawner")
set(GASNET_WITH_SSH_OUT_DEGREE "" CACHE STRING "SSH out-degree for ssh-spawner")

#---------------------------------------------------------------------
# Tests
#---------------------------------------------------------------------
option(GASNET_ENABLE_TESTS "Build and enable tests" ON)

#---------------------------------------------------------------------
# Force options for atomic operations
#---------------------------------------------------------------------
option(GASNET_FORCE_GENERIC_ATOMICOPS "Force generic (mutex-based) atomic operations" OFF)
option(GASNET_FORCE_OS_ATOMICOPS "Force OS-based atomic operations" OFF)
option(GASNET_FORCE_COMPILER_ATOMICOPS "Force compiler-based atomic operations" OFF)

#---------------------------------------------------------------------
# Memory barrier force options
#---------------------------------------------------------------------
option(GASNET_FORCE_YIELD_MEMBARS "Force yield-based memory barriers" OFF)
option(GASNET_FORCE_SLOW_MEMBARS "Force slow memory barriers" OFF)

#---------------------------------------------------------------------
# Timer force options
#---------------------------------------------------------------------
option(GASNET_FORCE_GETTIMEOFDAY "Force use of gettimeofday for timers" OFF)
option(GASNET_FORCE_POSIX_REALTIME "Force use of POSIX realtime clock for timers" OFF)

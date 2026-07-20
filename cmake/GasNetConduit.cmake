#=============================================================================
# GasNetConduit.cmake - Shared conduit build logic
#
# Each conduit includes this module and calls gasnet_conduit_setup().
# This handles building the three threading-mode library variants,
# generating pkg-config files, and setting up tests.
#=============================================================================

#---------------------------------------------------------------------
# gasnet_conduit_setup(<name> [SOURCES <srcs...>] [EXTRA_FLAGS <flags...>]
#                      [LINK_LIBS <libs...>] [KINDS <kind...>]
#                      [SPAWNER <ssh|mpi|pmi>] [RUN_CMD <cmd>])
#
# Creates the three library targets:
#   gasnet-<name>-seq
#   gasnet-<name>-par
#   gasnet-<name>-parsync
#---------------------------------------------------------------------
function(gasnet_conduit_setup CONDUIT_NAME)
  set(options "")
  set(oneValueArgs RUN_CMD SPAWNER SRC_DIR)
  set(multiValueArgs SOURCES EXTRA_FLAGS LINK_LIBS KINDS BOOTSTRAP_SOURCES)
  cmake_parse_arguments(COND "${options}" "${oneValueArgs}" "${multiValueArgs}" ${ARGN})

  #-------------------------------------------------------------------
  # Common include directories for all conduit libraries
  #-------------------------------------------------------------------
  set(CONDUIT_INCLUDES
    "${CMAKE_CURRENT_SOURCE_DIR}"
    "${CMAKE_CURRENT_BINARY_DIR}"
    "${PROJECT_SOURCE_DIR}"
    "${PROJECT_BINARY_DIR}"
    "${PROJECT_SOURCE_DIR}/other"
    "${PROJECT_SOURCE_DIR}/other/kinds"
    "${PROJECT_SOURCE_DIR}/extended-ref"
    "${PROJECT_SOURCE_DIR}/extended-ref/coll"
    "${PROJECT_SOURCE_DIR}/extended-ref/vis"
    "${PROJECT_SOURCE_DIR}/extended-ref/ratomic"
  )

  # Add conduit-specific source directory
  if(COND_SRC_DIR)
    list(APPEND CONDUIT_INCLUDES "${COND_SRC_DIR}")
  endif()

  #-------------------------------------------------------------------
  # Common source files shared across ALL conduits
  #-------------------------------------------------------------------
  set(GASNET_COMMON_SOURCES
    "${PROJECT_SOURCE_DIR}/gasnet_tools.c"
    "${PROJECT_SOURCE_DIR}/gasnet_trace.c"
    "${PROJECT_SOURCE_DIR}/gasnet_event.c"
    "${PROJECT_SOURCE_DIR}/gasnet_legacy.c"
    "${PROJECT_SOURCE_DIR}/gasnet_internal.c"
    "${PROJECT_SOURCE_DIR}/gasnet_am.c"
    "${PROJECT_SOURCE_DIR}/gasnet_mmap.c"
    "${PROJECT_SOURCE_DIR}/gasnet_tm.c"
    "${PROJECT_SOURCE_DIR}/gasnet_diagnostic.c"
    # extended-ref sources
    "${PROJECT_SOURCE_DIR}/extended-ref/coll/gasnet_refcoll.c"
    "${PROJECT_SOURCE_DIR}/extended-ref/coll/gasnet_putget.c"
    "${PROJECT_SOURCE_DIR}/extended-ref/coll/gasnet_eager.c"
    "${PROJECT_SOURCE_DIR}/extended-ref/coll/gasnet_rvous.c"
    "${PROJECT_SOURCE_DIR}/extended-ref/coll/gasnet_team.c"
    "${PROJECT_SOURCE_DIR}/extended-ref/coll/gasnet_hashtable.c"
    "${PROJECT_SOURCE_DIR}/extended-ref/coll/gasnet_reduce.c"
    "${PROJECT_SOURCE_DIR}/extended-ref/coll/gasnet_bootstrap.c"
    "${PROJECT_SOURCE_DIR}/extended-ref/vis/gasnet_refvis.c"
    "${PROJECT_SOURCE_DIR}/extended-ref/ratomic/gasnet_refratomic.c"
    "${PROJECT_SOURCE_DIR}/other/kinds/gasnet_refkinds.c"
  )

  # PSHM source (if enabled)
  if(GASNET_PSHM_ENABLED)
    list(APPEND GASNET_COMMON_SOURCES "${PROJECT_SOURCE_DIR}/gasnet_pshm.c")
  endif()

  # Combine all sources
  set(ALL_SOURCES ${COND_SOURCES} ${GASNET_COMMON_SOURCES} ${COND_BOOTSTRAP_SOURCES})

  #-------------------------------------------------------------------
  # Build three library variants: SEQ, PAR, PARSYNC
  #-------------------------------------------------------------------
  foreach(THREAD_MODE SEQ PAR PARSYNC)
    string(TOLOWER "${THREAD_MODE}" mode_lc)
    set(lib_name "gasnet-${CONDUIT_NAME}-${mode_lc}")

    if(THREAD_MODE STREQUAL "PAR" OR THREAD_MODE STREQUAL "PARSYNC")
      if(NOT GASNET_HAVE_PTHREADS)
        continue()
      endif()
      if(THREAD_MODE STREQUAL "PAR" AND NOT GASNET_BUILD_PAR)
        continue()
      endif()
      if(THREAD_MODE STREQUAL "PARSYNC" AND NOT GASNET_BUILD_PARSYNC)
        continue()
      endif()
    else()
      if(NOT GASNET_BUILD_SEQ)
        continue()
      endif()
    endif()

    # Create static library
    add_library(${lib_name} STATIC ${ALL_SOURCES})

    # Threading define
    target_compile_definitions(${lib_name} PRIVATE
      -DGASNET_${THREAD_MODE}
      -DGASNETI_BUILDING_CONDUIT
      -D_GNU_SOURCE=1
    )

    # Debug/NDEBUG
    if(GASNET_ENABLE_DEBUG)
      target_compile_definitions(${lib_name} PRIVATE -DGASNET_DEBUG)
    else()
      target_compile_definitions(${lib_name} PRIVATE -DGASNET_NDEBUG)
    endif()

    # Trace/stats/debug-malloc
    if(GASNET_ENABLE_TRACE)
      target_compile_definitions(${lib_name} PRIVATE -DGASNET_TRACE)
    endif()
    if(GASNET_ENABLE_STATS)
      target_compile_definitions(${lib_name} PRIVATE -DGASNET_STATS)
    endif()
    if(GASNET_ENABLE_DEBUG_MALLOC)
      target_compile_definitions(${lib_name} PRIVATE -DGASNET_DEBUGMALLOC)
    endif()
    if(GASNET_ENABLE_SRCLINES)
      target_compile_definitions(${lib_name} PRIVATE -DGASNET_SRCLINES)
    endif()

    # Segment config
    if(GASNET_SEGMENT_CONFIG STREQUAL "FAST")
      target_compile_definitions(${lib_name} PRIVATE -DGASNET_SEGMENT_FAST)
    elseif(GASNET_SEGMENT_CONFIG STREQUAL "LARGE")
      target_compile_definitions(${lib_name} PRIVATE -DGASNET_SEGMENT_LARGE)
    elseif(GASNET_SEGMENT_CONFIG STREQUAL "EVERYTHING")
      target_compile_definitions(${lib_name} PRIVATE -DGASNET_SEGMENT_EVERYTHING)
    endif()

    # PSHM
    if(GASNET_PSHM_ENABLED)
      target_compile_definitions(${lib_name} PRIVATE -DGASNETI_PSHM_ENABLED)
      if(GASNET_PSHM_POSIX_ENABLED)
        target_compile_definitions(${lib_name} PRIVATE -DGASNETI_PSHM_POSIX)
      endif()
    endif()

    # Thread-specific defines
    if(THREAD_MODE STREQUAL "PAR" OR THREAD_MODE STREQUAL "PARSYNC")
      if(GASNET_THREAD_DEFINES)
        target_compile_definitions(${lib_name} PRIVATE ${GASNET_THREAD_DEFINES})
      endif()
      if(GASNET_THREAD_LIBS)
        target_link_libraries(${lib_name} PRIVATE ${GASNET_THREAD_LIBS})
      endif()
    endif()

    # Include directories
    target_include_directories(${lib_name} PRIVATE ${CONDUIT_INCLUDES})

    # Extra flags from conduit
    if(COND_EXTRA_FLAGS)
      target_compile_options(${lib_name} PRIVATE ${COND_EXTRA_FLAGS})
    endif()

    # Link libraries
    if(COND_LINK_LIBS)
      target_link_libraries(${lib_name} PRIVATE ${COND_LINK_LIBS})
    endif()

    # Developer warnings
    if(GASNET_ENABLE_DEV_WARNINGS AND DEVWARN_CFLAGS)
      target_compile_options(${lib_name} PRIVATE ${DEVWARN_CFLAGS})
    endif()

    # Set C standard
    set_target_properties(${lib_name} PROPERTIES
      C_STANDARD 11
      C_STANDARD_REQUIRED ON
      POSITION_INDEPENDENT_CODE ON
    )

    # Install
    install(TARGETS ${lib_name}
      ARCHIVE DESTINATION "${CMAKE_INSTALL_LIBDIR}"
    )

    # Memory kinds (GPU support) - special objects
    foreach(kind ${COND_KINDS})
      if(kind STREQUAL "cuda_uva" AND GASNET_KIND_CUDA_UVA_ENABLED)
        target_compile_definitions(${lib_name} PRIVATE -DGASNETI_MK_CLASS_CUDA_UVA_ENABLED)
      elseif(kind STREQUAL "hip" AND GASNET_KIND_HIP_ENABLED)
        target_compile_definitions(${lib_name} PRIVATE -DGASNETI_MK_CLASS_HIP_ENABLED)
      elseif(kind STREQUAL "ze" AND GASNET_KIND_ZE_ENABLED)
        target_compile_definitions(${lib_name} PRIVATE -DGASNETI_MK_CLASS_ZE_ENABLED)
      endif()
    endforeach()

    # Generate pkg-config file
    gasnet_gen_pkgconfig("${CONDUIT_NAME}" "${mode_lc}" "${lib_name}")

    message(STATUS "  Created library: ${lib_name}")
  endforeach()

  #-------------------------------------------------------------------
  # Install conduit headers
  #-------------------------------------------------------------------
  install(DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/"
    DESTINATION "${CMAKE_INSTALL_INCLUDEDIR}/${CONDUIT_NAME}-conduit"
    FILES_MATCHING
    PATTERN "*.h"
    PATTERN "*_internal.h" EXCLUDE
    PATTERN "CMakeLists.txt" EXCLUDE
  )
endfunction()

#---------------------------------------------------------------------
# gasnet_gen_pkgconfig - Generate pkg-config .pc file for a conduit
#---------------------------------------------------------------------
function(gasnet_gen_pkgconfig CONDUIT_NAME THREAD_MODEL LIB_NAME)
  set(pc_file "${CMAKE_CURRENT_BINARY_DIR}/gasnet-${CONDUIT_NAME}-${THREAD_MODEL}.pc")
  set(pc_content "prefix=${CMAKE_INSTALL_PREFIX}
exec_prefix=\${prefix}
libdir=\${prefix}/${CMAKE_INSTALL_LIBDIR}
includedir=\${prefix}/${CMAKE_INSTALL_INCLUDEDIR}

Name: GASNet ${CONDUIT_NAME}-conduit (${THREAD_MODEL})
Description: GASNet-EX ${CONDUIT_NAME} conduit (${THREAD_MODEL} mode)
Version: ${GASNET_RELEASE_VERSION}
URL: https://gasnet.lbl.gov

Requires:
Libs: -L\${libdir} -lgasnet-${CONDUIT_NAME}-${THREAD_MODEL}
Cflags: -I\${includedir} -I\${includedir}/${CONDUIT_NAME}-conduit
")
  file(WRITE "${pc_file}" "${pc_content}")
  install(FILES "${pc_file}" DESTINATION "${CMAKE_INSTALL_LIBDIR}/pkgconfig")
endfunction()

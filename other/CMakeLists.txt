#=============================================================================
# other/CMakeLists.txt - Build gasnet_tools library and supporting components
#=============================================================================

#---------------------------------------------------------------------
# gasnet_tools library - conduit-independent portability library
#---------------------------------------------------------------------
set(TOOLS_SOURCES
  "${PROJECT_SOURCE_DIR}/gasnet_tools.c"
)

set(TOOLS_INCLUDES
  "${PROJECT_SOURCE_DIR}"
  "${PROJECT_BINARY_DIR}"
  "${PROJECT_SOURCE_DIR}/other"
)

# SEQ variant
if(GASNET_BUILD_SEQ)
  add_library(gasnet_tools-seq STATIC ${TOOLS_SOURCES})
  target_include_directories(gasnet_tools-seq PRIVATE ${TOOLS_INCLUDES})
  target_compile_definitions(gasnet_tools-seq PRIVATE
    -DGASNET_SEQ
    -DGASNETT_THREAD_SINGLE
    -DGASNETI_BUILDING_TOOLS
    -D_GNU_SOURCE=1
  )
  set_target_properties(gasnet_tools-seq PROPERTIES
    C_STANDARD 11
    POSITION_INDEPENDENT_CODE ON
  )
  install(TARGETS gasnet_tools-seq ARCHIVE DESTINATION "${CMAKE_INSTALL_LIBDIR}")
  message(STATUS "  Created library: gasnet_tools-seq")
endif()

# PAR variant (thread-safe)
if(GASNET_BUILD_PAR AND GASNET_HAVE_PTHREADS)
  add_library(gasnet_tools-par STATIC ${TOOLS_SOURCES})
  target_include_directories(gasnet_tools-par PRIVATE ${TOOLS_INCLUDES})
  target_compile_definitions(gasnet_tools-par PRIVATE
    -DGASNET_PAR
    -DGASNETT_THREAD_SAFE
    -DGASNETI_BUILDING_TOOLS
    -D_GNU_SOURCE=1
    ${GASNET_THREAD_DEFINES}
  )
  if(GASNET_THREAD_LIBS)
    target_link_libraries(gasnet_tools-par PRIVATE ${GASNET_THREAD_LIBS})
  endif()
  set_target_properties(gasnet_tools-par PROPERTIES
    C_STANDARD 11
    POSITION_INDEPENDENT_CODE ON
  )
  install(TARGETS gasnet_tools-par ARCHIVE DESTINATION "${CMAKE_INSTALL_LIBDIR}")
  message(STATUS "  Created library: gasnet_tools-par")
endif()

#---------------------------------------------------------------------
# Install other/ headers
#---------------------------------------------------------------------
install(FILES
  "${CMAKE_CURRENT_SOURCE_DIR}/gasnet_arch_arm.h"
  "${CMAKE_CURRENT_SOURCE_DIR}/portable_inttypes.h"
  "${CMAKE_CURRENT_SOURCE_DIR}/portable_platform.h"
  "${CMAKE_CURRENT_SOURCE_DIR}/gasnet_portable_platform.h"
  DESTINATION "${CMAKE_INSTALL_INCLUDEDIR}"
)

#---------------------------------------------------------------------
# Generate pkg-config files for tools library
#---------------------------------------------------------------------
if(GASNET_BUILD_SEQ)
  set(pc_file "${CMAKE_CURRENT_BINARY_DIR}/gasnet_tools-seq.pc")
  file(WRITE "${pc_file}" "prefix=${CMAKE_INSTALL_PREFIX}
exec_prefix=\${prefix}
libdir=\${exec_prefix}/${CMAKE_INSTALL_LIBDIR}
includedir=\${prefix}/${CMAKE_INSTALL_INCLUDEDIR}

Name: GASNet Tools (SEQ)
Description: GASNet-EX conduit-independent tools library (single-threaded)
Version: ${GASNET_RELEASE_VERSION}
URL: https://gasnet.lbl.gov

Libs: -L\${libdir} -lgasnet_tools-seq
Cflags: -I\${includedir}
")
  install(FILES "${pc_file}" DESTINATION "${CMAKE_INSTALL_LIBDIR}/pkgconfig")
endif()

if(GASNET_BUILD_PAR AND GASNET_HAVE_PTHREADS)
  set(pc_file "${CMAKE_CURRENT_BINARY_DIR}/gasnet_tools-par.pc")
  file(WRITE "${pc_file}" "prefix=${CMAKE_INSTALL_PREFIX}
exec_prefix=\${prefix}
libdir=\${exec_prefix}/${CMAKE_INSTALL_LIBDIR}
includedir=\${prefix}/${CMAKE_INSTALL_INCLUDEDIR}

Name: GASNet Tools (PAR)
Description: GASNet-EX conduit-independent tools library (thread-safe)
Version: ${GASNET_RELEASE_VERSION}
URL: https://gasnet.lbl.gov

Libs: -L\${libdir} -lgasnet_tools-par ${GASNET_THREAD_LIBS}
Cflags: -I\${includedir}
")
  install(FILES "${pc_file}" DESTINATION "${CMAKE_INSTALL_LIBDIR}/pkgconfig")
endif()

#---------------------------------------------------------------------
# Install documentation from other/spawner directories
#---------------------------------------------------------------------
if(GASNET_ENABLE_SMP OR GASNET_ENABLE_IBV OR GASNET_ENABLE_OFI OR GASNET_ENABLE_UCX)
  install(FILES
    "${CMAKE_CURRENT_SOURCE_DIR}/ssh-spawner/README"
    DESTINATION "${CMAKE_INSTALL_DATADIR}/doc/GASNet"
    RENAME "README-ssh-spawner"
  )
  install(FILES
    "${CMAKE_CURRENT_SOURCE_DIR}/mpi-spawner/README"
    DESTINATION "${CMAKE_INSTALL_DATADIR}/doc/GASNet"
    RENAME "README-mpi-spawner"
  )
  install(FILES
    "${CMAKE_CURRENT_SOURCE_DIR}/pmi-spawner/README"
    DESTINATION "${CMAKE_INSTALL_DATADIR}/doc/GASNet"
    RENAME "README-pmi-spawner"
  )
endif()

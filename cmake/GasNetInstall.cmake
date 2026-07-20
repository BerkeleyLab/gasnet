#=============================================================================
# GasNetInstall.cmake - Installation rules for GASNet
#=============================================================================

#---------------------------------------------------------------------
# Install top-level public headers
#---------------------------------------------------------------------
install(FILES
  "${PROJECT_SOURCE_DIR}/gasnetex.h"
  "${PROJECT_SOURCE_DIR}/gasnet.h"
  "${PROJECT_SOURCE_DIR}/gasnet_ammacros.h"
  "${PROJECT_SOURCE_DIR}/gasnet_trace.h"
  "${PROJECT_SOURCE_DIR}/gasnet_fwd.h"
  "${PROJECT_SOURCE_DIR}/gasnet_asm.h"
  "${PROJECT_SOURCE_DIR}/gasnet_atomicops.h"
  "${PROJECT_SOURCE_DIR}/gasnet_atomic_bits.h"
  "${PROJECT_SOURCE_DIR}/gasnet_atomic_fwd.h"
  "${PROJECT_SOURCE_DIR}/gasnet_basic.h"
  "${PROJECT_SOURCE_DIR}/gasnet_help.h"
  "${PROJECT_SOURCE_DIR}/gasnet_membar.h"
  "${PROJECT_SOURCE_DIR}/gasnet_timer.h"
  "${PROJECT_SOURCE_DIR}/gasnet_tools.h"
  "${PROJECT_SOURCE_DIR}/gasnet_toolhelp.h"
  DESTINATION "${CMAKE_INSTALL_INCLUDEDIR}"
)

# Install kinds header
install(FILES
  "${PROJECT_SOURCE_DIR}/other/kinds/gasnet_mk.h"
  DESTINATION "${CMAKE_INSTALL_INCLUDEDIR}"
)

#---------------------------------------------------------------------
# Install documentation
#---------------------------------------------------------------------
install(FILES
  "${PROJECT_SOURCE_DIR}/README"
  "${PROJECT_SOURCE_DIR}/README-tools"
  "${PROJECT_SOURCE_DIR}/ChangeLog"
  "${PROJECT_SOURCE_DIR}/license.txt"
  DESTINATION "${CMAKE_INSTALL_DATADIR}/doc/GASNet"
)

install(FILES
  "${PROJECT_SOURCE_DIR}/docs/GASNet-EX.txt"
  "${PROJECT_SOURCE_DIR}/docs/gasnet1_differences.md"
  "${PROJECT_SOURCE_DIR}/docs/implementation_defined.md"
  "${PROJECT_SOURCE_DIR}/docs/memory_kinds_implementation.md"
  "${PROJECT_SOURCE_DIR}/docs/memory_kinds.pdf"
  DESTINATION "${CMAKE_INSTALL_DATADIR}/doc/GASNet"
)

#---------------------------------------------------------------------
# Install gasnet_config.h
#---------------------------------------------------------------------
install(FILES
  "${PROJECT_BINARY_DIR}/gasnet_config.h"
  DESTINATION "${CMAKE_INSTALL_INCLUDEDIR}"
)

#---------------------------------------------------------------------
# Export CMake package (for find_package)
#---------------------------------------------------------------------
include(CMakePackageConfigHelpers)

configure_package_config_file(
  "${PROJECT_SOURCE_DIR}/cmake/GasNetConfig.cmake.in"
  "${PROJECT_BINARY_DIR}/GasNetConfig.cmake"
  INSTALL_DESTINATION "${CMAKE_INSTALL_LIBDIR}/cmake/GasNet"
)

write_basic_package_version_file(
  "${PROJECT_BINARY_DIR}/GasNetConfigVersion.cmake"
  VERSION ${GASNET_RELEASE_VERSION}
  COMPATIBILITY SameMajorVersion
)

install(FILES
  "${PROJECT_BINARY_DIR}/GasNetConfig.cmake"
  "${PROJECT_BINARY_DIR}/GasNetConfigVersion.cmake"
  DESTINATION "${CMAKE_INSTALL_LIBDIR}/cmake/GasNet"
)

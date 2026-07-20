#=============================================================================
# extended-ref/CMakeLists.txt - Reference extended API implementation
#
# This directory contains sources for the reference implementation of the
# GASNet extended API. These sources are compiled directly into each conduit
# library (not as a separate library), so this file is a placeholder.
# The actual sources are listed in cmake/GasNetConduit.cmake.
#=============================================================================

# Install extended-ref public headers alongside each conduit's headers
# These are included by conduit-specific install rules
set(EXTENDED_REF_HEADERS
  "${CMAKE_CURRENT_SOURCE_DIR}/gasnet_extended.h"
  "${CMAKE_CURRENT_SOURCE_DIR}/gasnet_extended_fwd.h"
  "${CMAKE_CURRENT_SOURCE_DIR}/gasnet_extended_help.h"
  "${CMAKE_CURRENT_SOURCE_DIR}/vis/gasnet_vis_fwd.h"
  "${CMAKE_CURRENT_SOURCE_DIR}/ratomic/gasnet_ratomic_fwd.h"
  "${CMAKE_CURRENT_SOURCE_DIR}/coll/gasnet_coll_fwd.h"
)

# Install public API headers globally
install(FILES
  "${CMAKE_CURRENT_SOURCE_DIR}/coll/gasnet_coll.h"
  "${CMAKE_CURRENT_SOURCE_DIR}/ratomic/gasnet_ratomic.h"
  "${CMAKE_CURRENT_SOURCE_DIR}/vis/gasnet_vis.h"
  DESTINATION "${CMAKE_INSTALL_INCLUDEDIR}"
)

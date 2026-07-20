# ofi-conduit: Open Fabrics Interfaces conduit
find_package(Libfabric REQUIRED)

gasnet_conduit_setup(ofi
  SRC_DIR "${PROJECT_SOURCE_DIR}/ofi-conduit"
  SOURCES
    "${PROJECT_SOURCE_DIR}/ofi-conduit/gasnet_core.c"
    "${PROJECT_SOURCE_DIR}/ofi-conduit/gasnet_extended.c"
    "${PROJECT_SOURCE_DIR}/ofi-conduit/gasnet_ofi.c"
    "${PROJECT_SOURCE_DIR}/ofi-conduit/gasnet_hwloc.c"
  EXTRA_FLAGS
    ${LIBFABRIC_CFLAGS}
  LINK_LIBS
    ${LIBFABRIC_LIBRARIES}
  KINDS
    cuda_uva hip ze
  RUN_CMD "gasnetrun_ofi -np %N %P %A"
)

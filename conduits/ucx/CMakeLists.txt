# ucx-conduit: Unified Communication X conduit (experimental)
find_package(UCX REQUIRED)

gasnet_conduit_setup(ucx
  SRC_DIR "${PROJECT_SOURCE_DIR}/ucx-conduit"
  SOURCES
    "${PROJECT_SOURCE_DIR}/ucx-conduit/gasnet_core.c"
    "${PROJECT_SOURCE_DIR}/ucx-conduit/gasnet_core_sndrcv.c"
    "${PROJECT_SOURCE_DIR}/ucx-conduit/gasnet_extended.c"
    "${PROJECT_SOURCE_DIR}/ucx-conduit/gasnet_ratomic.c"
  EXTRA_FLAGS
    ${UCX_CFLAGS}
  LINK_LIBS
    ${UCX_LIBRARIES}
  KINDS
    cuda_uva hip
  RUN_CMD "gasnetrun_ucx -np %N %P %A"
)

# udp-conduit: Portable UDP/IP conduit
gasnet_conduit_setup(udp
  SRC_DIR "${PROJECT_SOURCE_DIR}/udp-conduit"
  SOURCES
    "${PROJECT_SOURCE_DIR}/udp-conduit/gasnet_core.c"
    "${PROJECT_SOURCE_DIR}/extended-ref/gasnet_extended.c"
  EXTRA_FLAGS
    -I${PROJECT_SOURCE_DIR}/other/amudp
    -I${PROJECT_BINARY_DIR}/other/amudp
  RUN_CMD "%P %N %A"
)

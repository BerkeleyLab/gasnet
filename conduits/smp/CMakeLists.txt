# smp-conduit: SMP shared-memory loopback
gasnet_conduit_setup(smp
  SRC_DIR "${PROJECT_SOURCE_DIR}/smp-conduit"
  SOURCES
    "${PROJECT_SOURCE_DIR}/smp-conduit/gasnet_core.c"
    "${PROJECT_SOURCE_DIR}/smp-conduit/gasnet_extended.c"
  RUN_CMD "gasnetrun_smp -np %N %P %A"
)

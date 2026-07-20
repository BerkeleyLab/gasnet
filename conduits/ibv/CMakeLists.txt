# ibv-conduit: Native InfiniBand Verbs conduit
find_package(IBVerbs REQUIRED)

gasnet_conduit_setup(ibv
  SRC_DIR "${PROJECT_SOURCE_DIR}/ibv-conduit"
  SOURCES
    "${PROJECT_SOURCE_DIR}/ibv-conduit/gasnet_core.c"
    "${PROJECT_SOURCE_DIR}/ibv-conduit/gasnet_core_connect.c"
    "${PROJECT_SOURCE_DIR}/ibv-conduit/gasnet_core_sndrcv.c"
    "${PROJECT_SOURCE_DIR}/ibv-conduit/gasnet_core_thread.c"
    "${PROJECT_SOURCE_DIR}/ibv-conduit/gasnet_extended.c"
    "${PROJECT_SOURCE_DIR}/ibv-conduit/gasnet_firehose.c"
    "${PROJECT_SOURCE_DIR}/ibv-conduit/gasnet_ratomic.c"
  EXTRA_FLAGS
    ${IBVERBS_CFLAGS}
  LINK_LIBS
    ${IBVERBS_LIBRARIES}
  KINDS
    cuda_uva hip
  RUN_CMD "gasnetrun_ibv -np %N %P %A"
)

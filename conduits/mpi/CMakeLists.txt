# mpi-conduit: Portable MPI conduit
find_package(MPI REQUIRED COMPONENTS C)

gasnet_conduit_setup(mpi
  SRC_DIR "${PROJECT_SOURCE_DIR}/mpi-conduit"
  SOURCES
    "${PROJECT_SOURCE_DIR}/mpi-conduit/gasnet_core.c"
    "${PROJECT_SOURCE_DIR}/extended-ref/gasnet_extended.c"
  EXTRA_FLAGS
    -I${PROJECT_SOURCE_DIR}/other/ammpi
    -I${PROJECT_BINARY_DIR}/other/ammpi
    ${MPI_C_COMPILE_FLAGS}
  LINK_LIBS
    ${MPI_C_LIBRARIES}
  RUN_CMD "gasnetrun_mpi -np %N %P %A"
)

/*   $Source: /Users/kamil/work/gasnet-cvs2/gasnet/vapi-conduit/Attic/gasnet_bootstrap_mpi.c,v $
 *     $Date: 2004/09/22 23:14:12 $
 * $Revision: 1.7 $
 * Description: GASNet vapi conduit implementation, mpi bootstrap code
 * Copyright 2003, LBNL
 * Terms of use are as specified in license.txt
 */

#include <signal.h>

#include <mpi.h>
#include <gasnet.h>
#include <gasnet_internal.h>

static MPI_Comm gasnetc_mpi_comm;

void gasnetc_bootstrapInit(int *argc, char ***argv, gasnet_node_t *nodes, gasnet_node_t *mynode) {
  MPI_Group world;
  int initialized = 0;
  int err;
  int tmp;

  /* Call MPI_Init exactly once */
  err = MPI_Initialized(&initialized);
  gasneti_assert(err == MPI_SUCCESS);
  if (!initialized) {
    err = MPI_Init(argc, argv);
    gasneti_assert(err == MPI_SUCCESS);
  }

  /* Create private communicator */
  err = MPI_Comm_group(MPI_COMM_WORLD, &world);
  gasneti_assert(err == MPI_SUCCESS);
  err = MPI_Comm_create(MPI_COMM_WORLD, world, &gasnetc_mpi_comm);
  gasneti_assert(err == MPI_SUCCESS);
  err = MPI_Group_free(&world);
  gasneti_assert(err == MPI_SUCCESS);

  /* Get size and rank */
  err = MPI_Comm_size(gasnetc_mpi_comm, &tmp);
  gasneti_assert(err == MPI_SUCCESS);
  *nodes = tmp;

  err = MPI_Comm_rank(gasnetc_mpi_comm, &tmp);
  gasneti_assert(err == MPI_SUCCESS);
  *mynode = tmp;
}

void gasnetc_bootstrapFini(void) {
  int err;

  err = MPI_Comm_free(&gasnetc_mpi_comm);
  gasneti_assert(err == MPI_SUCCESS);

#if 0	/* Finalize will prevent exit code from reaching the caller!! */
  (void) MPI_Finalize();
#endif
}

void gasnetc_bootstrapAbort(int exitcode) {
  (void) MPI_Abort(gasnetc_mpi_comm, exitcode);

  gasneti_reghandler(SIGABRT, SIG_DFL);
  abort();
  /* NOT REACHED */
}

void gasnetc_bootstrapBarrier(void) {
  int err;

  err = MPI_Barrier(gasnetc_mpi_comm);
  gasneti_assert(err == MPI_SUCCESS);
}

void gasnetc_bootstrapAllgather(void *src, size_t len, void *dest) {
  int err;

  err = MPI_Allgather(src, len, MPI_CHAR, dest, len, MPI_CHAR, gasnetc_mpi_comm);
  gasneti_assert(err == MPI_SUCCESS);
}

void gasnetc_bootstrapAlltoall(void *src, size_t len, void *dest) {
  int err;

  err = MPI_Alltoall(src, len, MPI_CHAR, dest, len, MPI_CHAR, gasnetc_mpi_comm);
  gasneti_assert(err == MPI_SUCCESS);
}

void gasnetc_bootstrapBroadcast(void *src, size_t len, void *dest, int rootnode) {
  int err;
  
  if (gasnetc_mynode == rootnode) {
    memcpy(dest, src, len);
  }
  err = MPI_Bcast(dest, len, MPI_CHAR, rootnode, gasnetc_mpi_comm);
}



/*   $Source: /Users/kamil/work/gasnet-cvs2/gasnet/vapi-conduit/Attic/gasnet_bootstrap_mpi.c,v $
 *     $Date: 2005/01/03 15:15:39 $
 * $Revision: 1.7.2.1 $
 * Description: GASNet vapi conduit implementation, mpi bootstrap code
 * Copyright 2003, LBNL
 * Terms of use are as specified in license.txt
 */

#include <signal.h>

#include <mpi.h>
#include <gasnet.h>
#include <gasnet_internal.h>

static MPI_Comm gasnetc_mpi_comm;
static int gasnetc_mpi_preinitialized = 0;

void gasnetc_bootstrapInit(int *argc, char ***argv, gasnet_node_t *nodes, gasnet_node_t *mynode) {
  MPI_Group world;
  int err;
  int tmp;

  /* Call MPI_Init exactly once */
  err = MPI_Initialized(&gasnetc_mpi_preinitialized);
  gasneti_assert(err == MPI_SUCCESS);
  if (!gasnetc_mpi_preinitialized) {
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

  /* In most cases it appears that calling MPI_Finalize() will
   * prevent us from propagating the exit code to the spawner.
   * However, as seen w/ mpich-1.2.5, the alternative is to
   * hang on exit, which is no alternative at all.
   */
  if (!gasnetc_mpi_preinitialized) {
    (void) MPI_Finalize();
  }
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



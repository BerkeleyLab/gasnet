/*  $Archive:: gasnet/vapi-conduit/gasnet_bootstrap_mpi.c                  $
 *     $Date: 2003/06/20 00:15:04 $
 * $Revision: 1.1.2.3 $
 * Description: GASNet vapi conduit implementation, mpi bootstrap code
 * Copyright 2003, LBNL
 * Terms of use are as specified in license.txt
 */

#include <assert.h>
#include <gasnet.h>
#include <mpi.h>

void gasnetc_bootstrapInit(int *argc, char ***argv) {
  int err;

  err = MPI_Init(argc, argv);
  assert(err == MPI_SUCCESS);
}

void gasnetc_bootstrapFini(void) {
  (void) MPI_Finalize();
}

void gasnetc_bootstrapAbort(int exitcode) {
  (void) MPI_Abort(MPI_COMM_WORLD, exitcode);
}

void gasnetc_bootstrapConf(void) {
  int err, tmp;

  err = MPI_Comm_rank(MPI_COMM_WORLD, &tmp);
  assert(err == MPI_SUCCESS);
  gasnetc_mynode = tmp;
    
  err = MPI_Comm_size(MPI_COMM_WORLD, &tmp);
  assert(err == MPI_SUCCESS);
  gasnetc_nodes = tmp;
}

void gasnetc_bootstrapBarrier(void) {
  int err;

  err = MPI_Barrier(MPI_COMM_WORLD);
  assert(err == MPI_SUCCESS);
}

void gasnetc_bootstrapAllgather(void *src, size_t len, void *dest) {
  int err;

  err = MPI_Allgather(src, len, MPI_CHAR, dest, len, MPI_CHAR, MPI_COMM_WORLD);
  assert(err == MPI_SUCCESS);
}

void gasnetc_bootstrapAlltoall(void *src, size_t len, void *dest) {
  int err;

  err = MPI_Alltoall(src, len, MPI_CHAR, dest, len, MPI_CHAR, MPI_COMM_WORLD);
  assert(err == MPI_SUCCESS);
}

void gasnetc_bootstrapBroadcast(void *src, size_t len, void *dest, int rootnode) {
  int err;
  
  if (gasnetc_mynode == rootnode) {
    memcpy(dest, src, len);
  }
  err = MPI_Bcast(dest, len, MPI_CHAR, rootnode, MPI_COMM_WORLD);
}



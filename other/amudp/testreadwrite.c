#include <stdio.h>
#include <stdlib.h>

#include <amudp.h>
#include <amudp_spmd.h>

#include "apputils.h"

#define MAX_PROCS 255
static uint32_t vals[MAX_PROCS];
static uint32_t readarray[MAX_PROCS];

int main(int argc, char **argv) {
  eb_t eb;
  ep_t ep;
  uint64_t networkpid;
  int myproc;
  int numprocs;
  int k;
  int iters = 0;

  if (argc < 3) {
    printf("Usage: %s numprocs spawnfn (iters)\n", argv[0]);
    exit(1);
    }

  AMUDP_VerboseErrors = 1;

  /* call startup */
  AM_Safe(AMUDP_SPMDStartup(&argc, &argv, 
                        0, 0, NULL, 
                        &networkpid, &eb, &ep));

  /* setup handlers */
  setupUtilHandlers(ep, eb);

  /* get SPMD info */
  myproc = AMUDP_SPMDMyProc();
  numprocs = AMUDP_SPMDNumProcs();

  if (argc > 1) iters = atoi(argv[1]);
  if (!iters) iters = 1;
  if (myproc == 0) {
    printf("Running %i iterations of read/write test...\n", iters);
    fflush(stdout);
    }

  for (k=0;k < iters; k++) {

    /* set left neighbor's array */
    {int i;
     int leftP = myproc-1;
     if (leftP == -1) leftP = numprocs-1;
     for (i=0;i<MAX_PROCS;i++) writeWord(leftP, &vals[i], k);
     writeSync();
     }

    AM_Safe(AMUDP_SPMDBarrier()); /* barrier */

    { /* read right neighbor's array  */
      int i;
      int rightP = myproc+1;
      if (rightP == numprocs) rightP = 0;

      for (i=0;i<MAX_PROCS;i++) readWord(&readarray[i], rightP, &vals[i]);
      readSync();

      /* verify */
      for (i=0;i<MAX_PROCS;i++) {
        if (((int)readarray[i]) != k) {
          printf("Proc %i READ/WRITE TEST FAILED : readarray[%i] = %i   k = %i\n", myproc, i, readarray[i], k);
          fflush(stdout);
          break;
          }
        }
      #if AMUDP_DEBUG
        if (i != MAX_PROCS) {
          printf("Proc %i verified.\n", myproc);
          fflush(stdout);
          }
      #endif
      }

    AM_Safe(AMUDP_SPMDBarrier()); /* barrier */

    }

  /* dump stats */
  AM_Safe(AMUDP_SPMDBarrier());
  printGlobalStats();
  AM_Safe(AMUDP_SPMDBarrier());

  /* exit */
  AM_Safe(AMUDP_SPMDExit(0));

  return 0;
  }
/* ------------------------------------------------------------------------------------ */

#include <stdio.h>
#include <stdlib.h>

#include <amudp.h>
#include <amudp_spmd.h>

#include "apputils.h"

#define MAX_PROCS 255
static uint32_t vals[MAX_PROCS];

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
    printf("Running %i iterations of get/put test...\n", iters);
    fflush(stdout);
    }

  for (k=0;k < iters; k++) {

    /* set just my val */
    {int i;
     for (i=0;i<MAX_PROCS;i++) vals[i] = -1;
     vals[myproc] = myproc;
     }

    AM_Safe(AMUDP_SPMDBarrier()); /* barrier */

    { /* try some gets */
      int i;
      int sum = 0;
      int verify = 0;
      for (i = 0; i < numprocs; i++) {
        sum += getWord(i, &vals[i]); /*  get each peer's value and add them up */
        verify += i;
        }
      if (verify != sum) {
        printf("Proc %i GET TEST FAILED : sum = %i   verify = %i\n", myproc, sum, verify);
        fflush(stdout);
        }
      #if AMUDP_DEBUG
        else {
          printf("Proc %i verified.\n", myproc);
          fflush(stdout);
          }
      #endif
      }

    AM_Safe(AMUDP_SPMDBarrier()); /* barrier */

    { /* try some puts */
      int i;
      for (i = 0; i < numprocs; i++) {
        putWord(i, &vals[myproc], myproc); /*  push our value to correct position on each peer */
        }
      AM_Safe(AMUDP_SPMDBarrier()); /* barrier */
      for (i = 0; i < numprocs; i++) {
        if (((int)vals[i]) != i) {
          printf("Proc %i PUT TEST FAILED : i = %i   vals[i] = %i\n", myproc, i, vals[i]);
          break;
          }
        }
      #if AMUDP_DEBUG
        if (i == numprocs) {
          printf("Proc %i verified.\n", myproc);
          fflush(stdout);
          }
      #endif

      }
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

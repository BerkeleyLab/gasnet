#include <stdio.h>
#include <stdlib.h>

#include <ammpi.h>
#include <ammpi_spmd.h>

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

  if (argc < 2) {
    printf("Usage: %s iters\n", argv[0]);
    exit(1);
    }

  AMMPI_VerboseErrors = 1;

  /* call startup */
  AM_Safe(AMMPI_SPMDStartup(&argc, &argv, 
                            0, &networkpid, &eb, &ep));

  /* setup handlers */
  setupUtilHandlers(ep, eb);

  /* get SPMD info */
  myproc = AMMPI_SPMDMyProc();
  numprocs = AMMPI_SPMDNumProcs();

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

    AM_Safe(AMMPI_SPMDBarrier()); /* barrier */

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
      #ifdef AMMPI_DEBUG
        else {
          printf("Proc %i verified.\n", myproc);
          fflush(stdout);
          }
      #endif
      }

    AM_Safe(AMMPI_SPMDBarrier()); /* barrier */

    { /* try some puts */
      int i;
      for (i = 0; i < numprocs; i++) {
        putWord(i, &vals[myproc], myproc); /*  push our value to correct position on each peer */
        }
      AM_Safe(AMMPI_SPMDBarrier()); /* barrier */
      for (i = 0; i < numprocs; i++) {
        if (((int)vals[i]) != i) {
          printf("Proc %i PUT TEST FAILED : i = %i   vals[i] = %i\n", myproc, i, vals[i]);
          break;
          }
        }
      #ifdef AMMPI_DEBUG
        if (i == numprocs) {
          printf("Proc %i verified.\n", myproc);
          fflush(stdout);
          }
      #endif

      }
    }

  /* dump stats */
  AM_Safe(AMMPI_SPMDBarrier());
  printGlobalStats();
  AM_Safe(AMMPI_SPMDBarrier());

  /* exit */
  AM_Safe(AMMPI_SPMDExit(0));

  return 0;
  }
/* ------------------------------------------------------------------------------------ */

#include <stdio.h>
#include <stdlib.h>

#include <ammpi.h>
#include <ammpi_spmd.h>

#include "apputils.h"

#ifdef AMMPI_DEBUG
#define VERBOSE 0
#else
#define VERBOSE 0
#endif

/* non-pipelined version of ping tester */

#define PING_REQ_HANDLER 1
#define PING_REP_HANDLER 2

static volatile int numleft;

int myproc;
int numprocs;
eb_t eb;
ep_t ep;

static void ping_request_handler(void *token, void *msg, int nbytes) {
  numleft--;

  #if VERBOSE
    printf("%i: ping_request_handler(). sending reply...\n", myproc); fflush(stdout);
  #endif

  AM_Safe(AM_Reply0(token, PING_REP_HANDLER));
  }

static void ping_reply_handler(void *token) {

  #if VERBOSE
    printf("%i: ping_reply_handler()\n", myproc); fflush(stdout);
  #endif

  numleft--;
  }

void spinwait(int polling) {
  if (polling) { /* poll until everyone done */
    while (numleft) {
      AM_Safe(AM_Poll(eb));
      }
    }
  else {
    while (numleft) {
      AM_Safe(AM_SetEventMask(eb, AM_NOTEMPTY)); 
      AM_Safe(AM_WaitSema(eb));
      AM_Safe(AM_Poll(eb));
      }
    }
}

/* usage: testlatency  numprocs  spawnfn  iters  P/B  depth msgsz
 */
int main(int argc, char **argv) {
  uint64_t networkpid;
  int64_t begin, end, total;
  int polling = 1;
  int k;
  int iters = 0;
  int depth = 0;
  int msgsz = 0;
  char *msg=NULL;

  if (argc < 2) {
    printf("Usage: %s (iters) (Poll/Block) (netdepth) (msgsize)\n", argv[0]);
    exit(1);
    }

  AMMPI_VerboseErrors = 1;

  if (argc > 3) depth = atoi(argv[3]);
  if (!depth) depth = 4;

  if (argc > 4) msgsz = atoi(argv[4]);
  if (!msgsz) msgsz = 1;

  /* call startup */
  AM_Safe(AMMPI_SPMDStartup(&argc, &argv, 
                            depth, &networkpid, &eb, &ep));
  /* setup handlers */
  AM_Safe(AM_SetHandler(ep, PING_REQ_HANDLER, ping_request_handler));
  AM_Safe(AM_SetHandler(ep, PING_REP_HANDLER, ping_reply_handler));

  setupUtilHandlers(ep, eb);

  /* get SPMD info */
  myproc = AMMPI_SPMDMyProc();
  numprocs = AMMPI_SPMDNumProcs();

  if (argc > 1) iters = atoi(argv[1]);
  if (!iters) iters = 1;
  if (argc > 2) {
    switch(argv[2][0]) {
      case 'p': case 'P': polling = 1; break;
      case 'b': case 'B': polling = 0; break;
      default: printf("polling must be 'P' or 'B'..\n"); AMMPI_SPMDExit(1);
      }
    }

  if (myproc == 0) numleft = (numprocs-1)*iters;
  else numleft = iters;

  outputTimerStats();

  AM_Safe(AMMPI_SPMDBarrier());

  if (myproc == 0) printf("Running %i iterations of latency test (MSGSZ=%i)...\n", iters, msgsz);
  else msg = (char *)malloc(msgsz);

  begin = getCurrentTimeMicrosec();

  if (myproc == 0) spinwait(polling);
  else { /* everybody sends packets to 0 */
    for (k=0;k < iters; k++) {
      numleft = 1;
      #if VERBOSE
        printf("%i: sending request...", myproc); fflush(stdout);
      #endif
      AM_Safe(AM_RequestI0(ep, 0, PING_REQ_HANDLER, msg, msgsz));
      spinwait(polling);
      }
    }
  
  end = getCurrentTimeMicrosec();

  total = end - begin;
  if (myproc != 0) printf("Slave %i: %i microseconds total, throughput: %i requests/sec (%i us / request)\n", 
    myproc, (int)total, (int)(((float)1000000)*iters/((int)total)), ((int)total)/iters);
  else printf("Slave 0 done.\n");
  fflush(stdout);

  /* dump stats */
  AM_Safe(AMMPI_SPMDBarrier());
  printGlobalStats();
  AM_Safe(AMMPI_SPMDBarrier());

  /* exit */
  AM_Safe(AMMPI_SPMDExit(0));

  return 0;
  }
/* ------------------------------------------------------------------------------------ */

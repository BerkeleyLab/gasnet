#include <stdio.h>
#include <stdlib.h>

#include <amudp.h>
#include <amudp_spmd.h>

#include "apputils.h"

#define ABASE 0x69690000

#define A1  (ABASE + 1)
#define A2  (ABASE + 2)
#define A3  (ABASE + 3)
#define A4  (ABASE + 4)
#define A5  (ABASE + 5)
#define A6  (ABASE + 6)
#define A7  (ABASE + 7)
#define A8  (ABASE + 8)


#define SHORT_0REQ_HANDLER  10
#define SHORT_1REQ_HANDLER  11
#define SHORT_2REQ_HANDLER  12
#define SHORT_3REQ_HANDLER  13
#define SHORT_4REQ_HANDLER  14
#define SHORT_5REQ_HANDLER  15
#define SHORT_6REQ_HANDLER  16
#define SHORT_7REQ_HANDLER  17
#define SHORT_8REQ_HANDLER  18

#define SHORT_0REP_HANDLER  20
#define SHORT_1REP_HANDLER  21
#define SHORT_2REP_HANDLER  22
#define SHORT_3REP_HANDLER  23
#define SHORT_4REP_HANDLER  24
#define SHORT_5REP_HANDLER  25
#define SHORT_6REP_HANDLER  26
#define SHORT_7REP_HANDLER  27
#define SHORT_8REP_HANDLER  28

int numreq = 0;
int numrep = 0;
int myproc;
int numprocs;
/* ------------------------------------------------------------------------------------ */
void short_0req_handler(void *token) {
  numreq++;
   AM_Reply0(token, SHORT_0REP_HANDLER);
  }
void short_1req_handler(void *token, int a1) {
  if (a1!=A1) {
    fprintf(stderr, "Arg mismatch on P%i\n", myproc);
    abort();
  }
  numreq++;
   AM_Reply1(token, SHORT_1REP_HANDLER, a1);
  }
void short_2req_handler(void *token, int a1, int a2) {
  if (a1!=A1||a2!=A2) {
    fprintf(stderr, "Arg mismatch on P%i\n", myproc);
    abort();
  }
  numreq++;
   AM_Reply2(token, SHORT_2REP_HANDLER, a1, a2);
  }
void short_3req_handler(void *token, int a1, int a2, int a3) {
  if (a1!=A1||a2!=A2||a3!=A3) {
    fprintf(stderr, "Arg mismatch on P%i\n", myproc);
    abort();
  }
  numreq++;
   AM_Reply3(token, SHORT_3REP_HANDLER, a1, a2, a3);
  }
void short_4req_handler(void *token, int a1, int a2, int a3, int a4) {
  if (a1!=A1||a2!=A2||a3!=A3||a4!=A4) {
    fprintf(stderr, "Arg mismatch on P%i\n", myproc);
    abort();
  }
  numreq++;
   AM_Reply4(token, SHORT_4REP_HANDLER, a1, a2, a3, a4);
  }
void short_5req_handler(void *token, int a1, int a2, int a3, int a4, int a5) {
  if (a1!=A1||a2!=A2||a3!=A3||a4!=A4||a5!=A5) {
    fprintf(stderr, "Arg mismatch on P%i\n", myproc);
    abort();
  }
  numreq++;
   AM_Reply5(token, SHORT_5REP_HANDLER, a1, a2, a3, a4, a5);
  }
void short_6req_handler(void *token, int a1, int a2, int a3, int a4, int a5, int a6) {
  if (a1!=A1||a2!=A2||a3!=A3||a4!=A4||a5!=A5||a6!=A6) {
    fprintf(stderr, "Arg mismatch on P%i\n", myproc);
    abort();
  }
  numreq++;
   AM_Reply6(token, SHORT_6REP_HANDLER, a1, a2, a3, a4, a5, a6);
  }
void short_7req_handler(void *token, int a1, int a2, int a3, int a4, int a5, int a6, int a7) {
  if (a1!=A1||a2!=A2||a3!=A3||a4!=A4||a5!=A5||a6!=A6||a7!=A7) {
    fprintf(stderr, "Arg mismatch on P%i\n", myproc);
    abort();
  }
  numreq++;
   AM_Reply7(token, SHORT_7REP_HANDLER, a1, a2, a3, a4, a5, a6, a7);
  }
void short_8req_handler(void *token, int a1, int a2, int a3, int a4, int a5, int a6, int a7, int a8) {
  if (a1!=A1||a2!=A2||a3!=A3||a4!=A4||a5!=A5||a6!=A6||a7!=A7||a8!=A8) {
    fprintf(stderr, "Arg mismatch on P%i\n", myproc);
    abort();
  }
  numreq++;
   AM_Reply8(token, SHORT_8REP_HANDLER, a1, a2, a3, a4, a5, a6, a7, a8);
  }
/* ------------------------------------------------------------------------------------ */
void short_0rep_handler(void *token) {
  numrep++;

  }
void short_1rep_handler(void *token, int a1) {
  if (a1!=A1) {
    fprintf(stderr, "Arg mismatch on P%i\n", myproc);
    abort();
  }
  numrep++;

  }
void short_2rep_handler(void *token, int a1, int a2) {
  if (a1!=A1||a2!=A2) {
    fprintf(stderr, "Arg mismatch on P%i\n", myproc);
    abort();
  }
  numrep++;

  }
void short_3rep_handler(void *token, int a1, int a2, int a3) {
  if (a1!=A1||a2!=A2||a3!=A3) {
    fprintf(stderr, "Arg mismatch on P%i\n", myproc);
    abort();
  }
  numrep++;

  }
void short_4rep_handler(void *token, int a1, int a2, int a3, int a4) {
  if (a1!=A1||a2!=A2||a3!=A3||a4!=A4) {
    fprintf(stderr, "Arg mismatch on P%i\n", myproc);
    abort();
  }
  numrep++;

  }
void short_5rep_handler(void *token, int a1, int a2, int a3, int a4, int a5) {
  if (a1!=A1||a2!=A2||a3!=A3||a4!=A4||a5!=A5) {
    fprintf(stderr, "Arg mismatch on P%i\n", myproc);
    abort();
  }
  numrep++;

  }
void short_6rep_handler(void *token, int a1, int a2, int a3, int a4, int a5, int a6) {
  if (a1!=A1||a2!=A2||a3!=A3||a4!=A4||a5!=A5||a6!=A6) {
    fprintf(stderr, "Arg mismatch on P%i\n", myproc);
    abort();
  }
  numrep++;

  }
void short_7rep_handler(void *token, int a1, int a2, int a3, int a4, int a5, int a6, int a7) {
  if (a1!=A1||a2!=A2||a3!=A3||a4!=A4||a5!=A5||a6!=A6||a7!=A7) {
    fprintf(stderr, "Arg mismatch on P%i\n", myproc);
    abort();
  }
  numrep++;

  }
void short_8rep_handler(void *token, int a1, int a2, int a3, int a4, int a5, int a6, int a7, int a8) {
  if (a1!=A1||a2!=A2||a3!=A3||a4!=A4||a5!=A5||a6!=A6||a7!=A7||a8!=A8) {
    fprintf(stderr, "Arg mismatch on P%i\n", myproc);
    abort();
  }
  numrep++;

  }
/* ------------------------------------------------------------------------------------ */
int main(int argc, char **argv) {
  eb_t eb;
  ep_t ep;
  uint64_t networkpid;
  int partner;
  int iters=0, depth=0, polling = 1, i;

  AMUDP_VerboseErrors = 1;

  if (argc < 3) {
    printf("Usage: %s numprocs spawnfn (iters) (Poll/Block) (netdepth)\n", argv[0]);
    exit(1);
    }

  if (argc > 5) depth = atoi(argv[5]);
  if (!depth) depth = 4;

  putenv((char*)"A=A");
  putenv((char*)"B=B");
  putenv((char*)"C=C");
  putenv((char*)"ABC=ABC");
  putenv((char*)"AReallyLongEnvironmentName=A Really Long Environment Value");

  /* call startup */
  AM_Safe(AMUDP_SPMDStartup(&argc, &argv, 
                            0, depth, NULL, 
                            &networkpid, &eb, &ep));

  if (argc > 1) iters = atoi(argv[1]);
  if (!iters) iters = 1;

  if (argc > 2) {
    switch(argv[2][0]) {
      case 'p': case 'P': polling = 1; break;
      case 'b': case 'B': polling = 0; break;
      default: printf("polling must be 'P' or 'B'..\n"); AMUDP_SPMDExit(1);
      }
    }

  /* setup handlers */
  AM_Safe(AM_SetHandler(ep, SHORT_0REQ_HANDLER, short_0req_handler));
  AM_Safe(AM_SetHandler(ep, SHORT_1REQ_HANDLER, short_1req_handler));
  AM_Safe(AM_SetHandler(ep, SHORT_2REQ_HANDLER, short_2req_handler));
  AM_Safe(AM_SetHandler(ep, SHORT_3REQ_HANDLER, short_3req_handler));
  AM_Safe(AM_SetHandler(ep, SHORT_4REQ_HANDLER, short_4req_handler));
  AM_Safe(AM_SetHandler(ep, SHORT_5REQ_HANDLER, short_5req_handler));
  AM_Safe(AM_SetHandler(ep, SHORT_6REQ_HANDLER, short_6req_handler));
  AM_Safe(AM_SetHandler(ep, SHORT_7REQ_HANDLER, short_7req_handler));
  AM_Safe(AM_SetHandler(ep, SHORT_8REQ_HANDLER, short_8req_handler));

  AM_Safe(AM_SetHandler(ep, SHORT_0REP_HANDLER, short_0rep_handler));
  AM_Safe(AM_SetHandler(ep, SHORT_1REP_HANDLER, short_1rep_handler));
  AM_Safe(AM_SetHandler(ep, SHORT_2REP_HANDLER, short_2rep_handler));
  AM_Safe(AM_SetHandler(ep, SHORT_3REP_HANDLER, short_3rep_handler));
  AM_Safe(AM_SetHandler(ep, SHORT_4REP_HANDLER, short_4rep_handler));
  AM_Safe(AM_SetHandler(ep, SHORT_5REP_HANDLER, short_5rep_handler));
  AM_Safe(AM_SetHandler(ep, SHORT_6REP_HANDLER, short_6rep_handler));
  AM_Safe(AM_SetHandler(ep, SHORT_7REP_HANDLER, short_7rep_handler));
  AM_Safe(AM_SetHandler(ep, SHORT_8REP_HANDLER, short_8rep_handler));
  setupUtilHandlers(ep, eb);
  
  numreq = 0;
  numrep = 0;

  /* barrier */
  AM_Safe(AMUDP_SPMDBarrier());

  /* get SPMD info */
  myproc = AMUDP_SPMDMyProc();
  numprocs = AMUDP_SPMDNumProcs();

  partner = (myproc + 1)%numprocs;

  /* compute */

  for (i=0; i < iters; i++) {
    AM_Safe(AM_Request0(ep, partner, SHORT_0REQ_HANDLER));
    AM_Safe(AM_Request1(ep, partner, SHORT_1REQ_HANDLER, A1));
    AM_Safe(AM_Request2(ep, partner, SHORT_2REQ_HANDLER, A1, A2));
    AM_Safe(AM_Request3(ep, partner, SHORT_3REQ_HANDLER, A1, A2, A3));
    AM_Safe(AM_Request4(ep, partner, SHORT_4REQ_HANDLER, A1, A2, A3, A4));
    AM_Safe(AM_Request5(ep, partner, SHORT_5REQ_HANDLER, A1, A2, A3, A4, A5));
    AM_Safe(AM_Request6(ep, partner, SHORT_6REQ_HANDLER, A1, A2, A3, A4, A5, A6));
    AM_Safe(AM_Request7(ep, partner, SHORT_7REQ_HANDLER, A1, A2, A3, A4, A5, A6, A7));
    AM_Safe(AM_Request8(ep, partner, SHORT_8REQ_HANDLER, A1, A2, A3, A4, A5, A6, A7, A8));

    while (numrep < 9*(i+1)) {
      if (polling) {
        AM_Safe(AM_Poll(eb));
        } 
      else {
        AM_Safe(AM_SetEventMask(eb, AM_NOTEMPTY));
        AM_Safe(AM_WaitSema(eb));
        AM_Safe(AM_Poll(eb));
        }
      }
    }

  if (strcmp(AMUDP_SPMDgetenvMaster("A"),"A")) {
    fprintf(stderr, "Environment value mismatch on P%i\n", myproc);
    abort();
    }
  if (strcmp(AMUDP_SPMDgetenvMaster("B"),"B")) {
    fprintf(stderr, "Environment value mismatch on P%i\n", myproc);
    abort();
    }
  if (strcmp(AMUDP_SPMDgetenvMaster("C"),"C")) {
    fprintf(stderr, "Environment value mismatch on P%i\n", myproc);
    abort();
    }
  if (strcmp(AMUDP_SPMDgetenvMaster("ABC"),"ABC")) {
    fprintf(stderr, "Environment value mismatch on P%i\n", myproc);
    abort();
    }
  if (strcmp(AMUDP_SPMDgetenvMaster("AReallyLongEnvironmentName"),"A Really Long Environment Value")) {
    fprintf(stderr, "Environment value mismatch on P%i\n", myproc);
    abort();
    }

  /* barrier */
  AM_Safe(AMUDP_SPMDBarrier());

  printGlobalStats();

  AM_Safe(AMUDP_SPMDBarrier());

  /* exit */
  AM_Safe(AMUDP_SPMDExit(0));

  return 0;
  }
/* ------------------------------------------------------------------------------------ */

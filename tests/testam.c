/*  $Archive:: /Ti/GASNet/tests/testmisc.c                             $
 *     $Date: 2003/11/13 12:05:05 $
 * $Revision: 1.8 $
 * Description: GASNet Active Messages performance test
 * Copyright 2002, Dan Bonachea <bonachea@cs.berkeley.edu>
 * Terms of use are as specified in license.txt
 */

#include <gasnet.h>

#include <test.h>

int mynode = 0;
void *myseg = NULL;
int sender;
int peer;
void *peerseg = NULL;

void report(const char *desc, int64_t totaltime, int iters) {
  if (mynode == 0) {
      printf("%-50s: %8.3f sec  %8.3f us\n",
        desc, ((float)totaltime)/1000000, ((float)totaltime)/iters);
      fflush(stdout);
  }
}

gasnet_hsl_t inchsl = GASNET_HSL_INITIALIZER;
#define INC(var) do {           \
    gasnet_hsl_lock(&inchsl);   \
    var++;                      \
    gasnet_hsl_unlock(&inchsl); \
  } while (0)

/* ------------------------------------------------------------------------------------ */
#define hidx_ping_shorthandler   201
#define hidx_pong_shorthandler   202

#define hidx_ping_medhandler     203
#define hidx_pong_medhandler     204

#define hidx_ping_longhandler    205
#define hidx_pong_longhandler    206

#define hidx_fping_shorthandler   207
#define hidx_fpong_shorthandler   208

#define hidx_fping_medhandler     209
#define hidx_fpong_medhandler     210

#define hidx_fping_longhandler    211
#define hidx_fpong_longhandler    212

volatile int flag = 0;

void ping_shorthandler(gasnet_token_t token) {
  GASNET_Safe(gasnet_AMReplyShort0(token, hidx_pong_shorthandler));
}
void pong_shorthandler(gasnet_token_t token) {
  flag++;
}


void ping_medhandler(gasnet_token_t token, void *buf, size_t nbytes) {
  GASNET_Safe(gasnet_AMReplyMedium0(token, hidx_pong_medhandler, buf, nbytes));
}
void pong_medhandler(gasnet_token_t token, void *buf, size_t nbytes) {
  flag++;
}


void ping_longhandler(gasnet_token_t token, void *buf, size_t nbytes) {
  GASNET_Safe(gasnet_AMReplyLong0(token, hidx_pong_longhandler, buf, nbytes, peerseg));
}

void pong_longhandler(gasnet_token_t token, void *buf, size_t nbytes) {
  flag++;
}
/* ------------------------------------------------------------------------------------ */
void fping_shorthandler(gasnet_token_t token) {
  GASNET_Safe(gasnet_AMReplyShort0(token, hidx_fpong_shorthandler));
}
void fpong_shorthandler(gasnet_token_t token) {
  INC(flag);
}


void fping_medhandler(gasnet_token_t token, void *buf, size_t nbytes) {
  GASNET_Safe(gasnet_AMReplyMedium0(token, hidx_fpong_medhandler, buf, nbytes));
}
void fpong_medhandler(gasnet_token_t token, void *buf, size_t nbytes) {
  INC(flag);
}


void fping_longhandler(gasnet_token_t token, void *buf, size_t nbytes) {
  GASNET_Safe(gasnet_AMReplyLong0(token, hidx_fpong_longhandler, buf, nbytes, peerseg));
}

void fpong_longhandler(gasnet_token_t token, void *buf, size_t nbytes) {
  INC(flag);
}

/* ------------------------------------------------------------------------------------ */
/* This tester measures the performance of a number of miscellaneous GASNet functions 
   that don't involve actual communication, to assist in evaluating the overhead of 
   the GASNet layer itself
 */
int main(int argc, char **argv) {
  int iters=0;
  int i = 0;
  int maxsz=64*1024;
  gasnet_handlerentry_t htable[] = { 
    { hidx_ping_shorthandler,  ping_shorthandler  },
    { hidx_pong_shorthandler,  pong_shorthandler  },
    { hidx_ping_medhandler,    ping_medhandler    },
    { hidx_pong_medhandler,    pong_medhandler    },
    { hidx_ping_longhandler,   ping_longhandler   },
    { hidx_pong_longhandler,   pong_longhandler   },

    { hidx_fping_shorthandler,  fping_shorthandler  },
    { hidx_fpong_shorthandler,  fpong_shorthandler  },
    { hidx_fping_medhandler,    fping_medhandler    },
    { hidx_fpong_medhandler,    fpong_medhandler    },
    { hidx_fping_longhandler,   fping_longhandler   },
    { hidx_fpong_longhandler,   fpong_longhandler   }
  };

  GASNET_Safe(gasnet_init(&argc, &argv));
  GASNET_Safe(gasnet_attach(htable, sizeof(htable)/sizeof(gasnet_handlerentry_t),
                            TEST_SEGSZ, TEST_MINHEAPOFFSET));
  TEST_SEG(gasnet_mynode()); /* ensure we got the segment requested */

  MSG("running...");

  mynode = gasnet_mynode();
  myseg = TEST_MYSEG();

  if (argc > 1) iters = atoi(argv[1]);
  if (!iters) iters = 10000;

  peer = (mynode + 1) % gasnet_nodes();
  sender = mynode % 2 == 0;

  peerseg = TEST_SEG(peer);

  if (mynode == 0) {
      printf("Running AM performance test with %i iterations...\n",iters);
      printf("%-50s    Total time    Avg. time\n"
             "%-50s    ----------    ---------\n", "", "");
      fflush(stdout);
  }

  /* ------------------------------------------------------------------------------------ */
  { GASNET_BEGIN_FUNCTION();

    BARRIER();

    if (sender) {
      int64_t start = TIME();
      flag = -1;
      for (i=0; i < iters; i++) {
        GASNET_Safe(gasnet_AMRequestShort0(peer, hidx_ping_shorthandler));
        GASNET_BLOCKUNTIL(flag == i);
      }
      report("AMShort ping-pong, roundtrip latency",TIME() - start, iters);
    }

    if (mynode == 0) { printf("\n"); fflush(stdout); }
    BARRIER();

    /* ------------------------------------------------------------------------------------ */
    if (sender) {
      int64_t start = TIME();
      flag = 0;
      BARRIER();
      for (i=0; i < iters; i++) {
        GASNET_Safe(gasnet_AMRequestShort0(peer, hidx_fpong_shorthandler));
      }
      if (peer == mynode) GASNET_BLOCKUNTIL(flag == iters);
      BARRIER();
      report("AMShort one-way flood, inv. throughput",TIME() - start, iters);
    } else {
      flag = 0;
      BARRIER();
      GASNET_BLOCKUNTIL(flag == iters);
      BARRIER();
    }

    if (mynode == 0) { printf("\n"); fflush(stdout); }
    BARRIER();

    /* ------------------------------------------------------------------------------------ */
    if (sender) {
      int64_t start = TIME();
      flag = 0;
      for (i=0; i < iters; i++) {
        GASNET_Safe(gasnet_AMRequestShort0(peer, hidx_fping_shorthandler));
      }
      GASNET_BLOCKUNTIL(flag == iters);
      report("AMShort roundtrip flood, inv. throughput",TIME() - start, iters);
    }

    if (mynode == 0) { printf("\n"); fflush(stdout); }
    BARRIER();

    /* ------------------------------------------------------------------------------------ */
    if (sender) {
      int64_t start = TIME();
      flag = -1;
      for (i=0; i < iters; i++) {
        GASNET_Safe(gasnet_AMRequestMedium0(peer, hidx_ping_medhandler, myseg, 0));
        GASNET_BLOCKUNTIL(flag == i);
      }
      report("AMMedium(sz=    0) ping-pong, roundtrip latency",TIME() - start, iters);
    }

    BARRIER();

    { int sz = 1;
      char msg[255];
      for (sz = 1; sz <= MIN(maxsz, gasnet_AMMaxMedium()); sz *= 2) {
        sprintf(msg, "AMMedium(sz=%5i) ping-pong, roundtrip latency", sz);
        BARRIER();
        if (sender) {
          int64_t start = TIME();
          flag = -1;
          for (i=0; i < iters; i++) {
            GASNET_Safe(gasnet_AMRequestMedium0(peer, hidx_ping_medhandler, myseg, sz));
            GASNET_BLOCKUNTIL(flag == i);
          }
          report(msg,TIME() - start, iters);
        }
        BARRIER();
      }
    }

    if (mynode == 0) { printf("\n"); fflush(stdout); }
    BARRIER();

    /* ------------------------------------------------------------------------------------ */
    flag = 0;
    BARRIER();
    if (sender) {
      int64_t start = TIME();
      for (i=0; i < iters; i++) {
        GASNET_Safe(gasnet_AMRequestMedium0(peer, hidx_fpong_medhandler, myseg, 0));
      }
      if (peer == mynode) GASNET_BLOCKUNTIL(flag == iters);
      BARRIER();
      report("AMMedium(sz=    0) one-way flood, inv. throughput",TIME() - start, iters);
    } else {
      GASNET_BLOCKUNTIL(flag == iters);
      BARRIER();
    }

    BARRIER();

    { int sz = 1;
      char msg[255];
      for (sz = 1; sz <= MIN(maxsz, gasnet_AMMaxMedium()); sz *= 2) {
        flag = 0;
        sprintf(msg, "AMMedium(sz=%5i) one-way flood, inv. throughput", sz);
        BARRIER();
        if (sender) {
          int64_t start = TIME();
          for (i=0; i < iters; i++) {
            GASNET_Safe(gasnet_AMRequestMedium0(peer, hidx_fpong_medhandler, myseg, sz));
          }
          if (peer == mynode) GASNET_BLOCKUNTIL(flag == iters);
          BARRIER();
          report(msg,TIME() - start, iters);
        } else {
          GASNET_BLOCKUNTIL(flag == iters);
          BARRIER();
        }
        BARRIER();
      }
    }

    if (mynode == 0) { printf("\n"); fflush(stdout); }
    BARRIER();

    /* ------------------------------------------------------------------------------------ */
    if (sender) {
      int64_t start = TIME();
      flag = 0;
      for (i=0; i < iters; i++) {
        GASNET_Safe(gasnet_AMRequestMedium0(peer, hidx_fping_medhandler, myseg, 0));
      }
      GASNET_BLOCKUNTIL(flag == iters);
      report("AMMedium(sz=    0) roundtrip flood, inv. throughput",TIME() - start, iters);
    }

    BARRIER();

    { int sz = 1;
      char msg[255];
      for (sz = 1; sz <= MIN(maxsz, gasnet_AMMaxMedium()); sz *= 2) {
        sprintf(msg, "AMMedium(sz=%5i) roundtrip flood, inv. throughput", sz);
        BARRIER();
        if (sender) {
          int64_t start = TIME();
          flag = 0;
          for (i=0; i < iters; i++) {
            GASNET_Safe(gasnet_AMRequestMedium0(peer, hidx_fping_medhandler, myseg, sz));
          }
          GASNET_BLOCKUNTIL(flag == iters);
          report(msg,TIME() - start, iters);
        }
        BARRIER();
      }
    }

    if (mynode == 0) { printf("\n"); fflush(stdout); }
    BARRIER();

    /* ------------------------------------------------------------------------------------ */
    if (sender) {
      int64_t start = TIME();
      flag = -1;
      for (i=0; i < iters; i++) {
        GASNET_Safe(gasnet_AMRequestLong0(peer, hidx_ping_longhandler, myseg, 0, peerseg));
        GASNET_BLOCKUNTIL(flag == i);
      }
      report("AMLong(sz=    0) ping-pong, roundtrip latency",TIME() - start, iters);
    }

    BARRIER();

    { int sz = 1;
      char msg[255];
      for (sz = 1; sz <= MIN(maxsz, MIN(gasnet_AMMaxLongRequest(),gasnet_AMMaxLongReply())); sz *= 2) {
        sprintf(msg, "AMLong(sz=%5i) ping-pong, roundtrip latency", sz);
        BARRIER();
        if (sender) {
          int64_t start = TIME();
          flag = -1;
          for (i=0; i < iters; i++) {
            GASNET_Safe(gasnet_AMRequestLong0(peer, hidx_ping_longhandler, myseg, sz, peerseg));
            GASNET_BLOCKUNTIL(flag == i);
          }
          report(msg,TIME() - start, iters);
        }
        BARRIER();
      }
    }

    if (mynode == 0) { printf("\n"); fflush(stdout); }
    BARRIER();
    /* ------------------------------------------------------------------------------------ */
    flag = 0;
    BARRIER();
    if (sender) {
      int64_t start = TIME();
      for (i=0; i < iters; i++) {
        GASNET_Safe(gasnet_AMRequestLong0(peer, hidx_fpong_longhandler, myseg, 0, peerseg));
      }
      if (peer == mynode) GASNET_BLOCKUNTIL(flag == iters);
      BARRIER();
      report("AMLong(sz=    0) one-way flood, inv. throughput",TIME() - start, iters);
    } else {
      GASNET_BLOCKUNTIL(flag == iters);
      BARRIER();
    }

    BARRIER();

    { int sz = 1;
      char msg[255];
      for (sz = 1; sz <= MIN(maxsz, MIN(gasnet_AMMaxLongRequest(),gasnet_AMMaxLongReply())); sz *= 2) {
        flag = 0;
        sprintf(msg, "AMLong(sz=%5i) one-way flood, inv. throughput", sz);
        BARRIER();
        if (sender) {
          int64_t start = TIME();
          for (i=0; i < iters; i++) {
            GASNET_Safe(gasnet_AMRequestLong0(peer, hidx_fpong_longhandler, myseg, sz, peerseg));
          }
          if (peer == mynode) GASNET_BLOCKUNTIL(flag == iters);
          BARRIER();
          report(msg,TIME() - start, iters);
        } else {
          GASNET_BLOCKUNTIL(flag == iters);
          BARRIER();
        }
        BARRIER();
      }
    }

    if (mynode == 0) { printf("\n"); fflush(stdout); }
    BARRIER();
    /* ------------------------------------------------------------------------------------ */
    if (sender) {
      int64_t start = TIME();
      flag = 0;
      for (i=0; i < iters; i++) {
        GASNET_Safe(gasnet_AMRequestLong0(peer, hidx_fping_longhandler, myseg, 0, peerseg));
      }
      GASNET_BLOCKUNTIL(flag == iters);
      report("AMLong(sz=    0) roundtrip flood, inv. throughput",TIME() - start, iters);
    }

    BARRIER();

    { int sz = 1;
      char msg[255];
      for (sz = 1; sz <= MIN(maxsz, MIN(gasnet_AMMaxLongRequest(),gasnet_AMMaxLongReply())); sz *= 2) {
        sprintf(msg, "AMLong(sz=%5i) roundtrip flood, inv. throughput", sz);
        BARRIER();
        if (sender) {
          int64_t start = TIME();
          flag = 0;
          for (i=0; i < iters; i++) {
            GASNET_Safe(gasnet_AMRequestLong0(peer, hidx_fping_longhandler, myseg, sz, peerseg));
          }
          GASNET_BLOCKUNTIL(flag == iters);
          report(msg,TIME() - start, iters);
        }
        BARRIER();
      }
    }

    if (mynode == 0) { printf("\n"); fflush(stdout); }
    BARRIER();
    /* ------------------------------------------------------------------------------------ */
  }

  MSG("done.");

  gasnet_exit(0);
  return 0;
}

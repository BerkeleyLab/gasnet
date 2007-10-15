/* $Source: /Users/kamil/work/gasnet-cvs2/gasnet/tests/testcore2.c,v $
 * $Date: 2007/10/11 16:10:21 $
 * $Revision: 1.2 $
 * Copyright 2007, Dan Bonachea <bonachea@cs.berkeley.edu>
 * Terms of use are as specified in license.txt
 *
 * Description: GASNet Core checksum test
 * This stress tests the ability of the core to successfully send
 * AM Requests/Replies with correct data delivery
 * testing is run 'iters' times with Medium/Long payload sizes ranging from 1..'max_payload',
 *  with up to 'depth' AMs in-flight from a given node at any moment
 *
 */

int max_payload = 0;
int depth = 0;
#ifndef TEST_SEGSZ
  #define TEST_SEGSZ_EXPR ((uintptr_t)max_payload*depth*2)
#endif

#include "test.h"

int myproc;
int numproc;
int peerproc;
int numprocs;
int iters = 0;
int maxlong;
volatile int done = 0;
uint8_t *peerreqseg; /* long request landing zone */
uint8_t *peerrepseg; /* long reply landing zone */
uint8_t *localseg;

GASNETT_THREADKEY_DECLARE(mythread);
GASNETT_THREADKEY_DEFINE(mythread);

#define ELEM_VALUE(iter,chunkidx,elemidx) \
        ((((uint8_t)(iter)&0x3) << 6) | (((uint8_t)(chunkidx)&0x3) << 4) | (((uint8_t)(elemidx))&0xF))

void init_chunk(uint8_t *buf, size_t sz, int iter, int chunkidx) {
  size_t elemidx;
  for (elemidx = 0; elemidx < sz; elemidx++) {
    buf[chunkidx*sz+elemidx] = ELEM_VALUE(iter,chunkidx,elemidx);
  }
}

void validate_chunk(const char *context, uint8_t *buf, size_t sz, int iter, int chunkidx) {
  size_t elemidx;
  for (elemidx = 0; elemidx < sz; elemidx++) {
    uint8_t actual = buf[elemidx];
    uint8_t expected = ELEM_VALUE(iter,chunkidx,elemidx);
    if (actual != expected) {
      int id = (uintptr_t)gasnett_threadkey_get(mythread); 
      ERR("TH%i data mismatch at sz=%i iter=%i chunk=%i elem=%i : actual=%02x expected=%02x in %s",
           id, (int)sz,iter,chunkidx,(int)elemidx,
           (unsigned int)actual,(unsigned int)expected,
           context);
    }
  }
}

/* Test handlers */
#define hidx_ping_medhandler     203
#define hidx_pong_medhandler     204

#define hidx_ping_longhandler    205
#define hidx_pong_longhandler    206

gasnett_atomic_t pong_recvd;

#define CHECK_SRC(token) do {               \
    gasnet_node_t srcnode;                  \
    gasnet_AMGetMsgSource(token, &srcnode); \
    assert_always(srcnode == peerproc);     \
  } while (0)

void ping_medhandler(gasnet_token_t token, void *buf, size_t nbytes, 
                     gasnet_handlerarg_t iter, gasnet_handlerarg_t chunkidx) {
  validate_chunk("Medium Request", buf, nbytes, iter, chunkidx);
  CHECK_SRC(token);
  GASNET_Safe(gasnet_AMReplyMedium2(token, hidx_pong_medhandler, buf, nbytes, iter, chunkidx));
}
void pong_medhandler(gasnet_token_t token, void *buf, size_t nbytes,
                     gasnet_handlerarg_t iter, gasnet_handlerarg_t chunkidx) {
  validate_chunk("Medium Reply", buf, nbytes, iter, chunkidx);
  CHECK_SRC(token);
  gasnett_atomic_increment(&pong_recvd,0);
}

void ping_longhandler(gasnet_token_t token, void *buf, size_t nbytes,
                     gasnet_handlerarg_t iter, gasnet_handlerarg_t chunkidx) {
  validate_chunk("Long Request", buf, nbytes, iter, chunkidx);
  CHECK_SRC(token);
  GASNET_Safe(gasnet_AMReplyLong2(token, hidx_pong_longhandler, buf, nbytes, peerrepseg+chunkidx*nbytes, iter, chunkidx));
}

void pong_longhandler(gasnet_token_t token, void *buf, size_t nbytes,
                     gasnet_handlerarg_t iter, gasnet_handlerarg_t chunkidx) {
  validate_chunk("Long Reply", buf, nbytes, iter, chunkidx);
  CHECK_SRC(token);
  gasnett_atomic_increment(&pong_recvd,0);
}

void *doit(void *id);

int main(int argc, char **argv) {
  gasnet_handlerentry_t htable[] = {
    { hidx_ping_medhandler,    ping_medhandler    },
    { hidx_pong_medhandler,    pong_medhandler    },
    { hidx_ping_longhandler,   ping_longhandler   },
    { hidx_pong_longhandler,   pong_longhandler   },
  };

  /* call startup */
  GASNET_Safe(gasnet_init(&argc, &argv));

  if (argc > 1) iters = atoi(argv[1]);
  if (!iters) iters = 10;
  if (argc > 2) max_payload = atoi(argv[2]);
  if (!max_payload) max_payload = 1024*1024;
  if (argc > 3) depth = atoi(argv[3]);
  if (!depth) depth = 16;

  /* round down to largest payload AM allows */
  maxlong = MIN(gasnet_AMMaxLongRequest(),gasnet_AMMaxLongReply());
  max_payload = MIN(max_payload,MAX(gasnet_AMMaxMedium(),maxlong));

  GASNET_Safe(gasnet_attach(htable, sizeof(htable)/sizeof(gasnet_handlerentry_t), TEST_SEGSZ_REQUEST, TEST_MINHEAPOFFSET));
  test_init("testcore2",0,"(iters) (max_payload) (depth)");

  if (argc > 4) test_usage();

  TEST_PRINT_CONDUITINFO();

  /* get SPMD info */
  myproc = gasnet_mynode();
  numprocs = gasnet_nodes();

  peerproc = myproc ^ 1;
  if (peerproc == gasnet_nodes()) {
    /* w/ odd # of nodes, last one talks to self */
    peerproc = myproc;
  }
  peerreqseg = TEST_SEG(peerproc);
  peerrepseg = peerreqseg+max_payload*depth;
  assert_always(TEST_SEGSZ >= max_payload*depth*2);
  localseg = test_malloc(max_payload*depth);

  #ifdef GASNET_PAR
    test_createandjoin_pthreads(2,doit,NULL,0);
  #else
    doit(0);
  #endif

  BARRIER();
  test_free(localseg);
  MSG("done.");
  gasnet_exit(0);
  return(0);
}

void *doit(void *id) {
  gasnett_threadkey_set(mythread,id); 
  if ((uintptr_t)id != 0) { /* additional threads polling, to encourage handler concurrency */
    while (!done) {
      gasnet_AMPoll();
      gasnett_sched_yield();
    }
    return 0;
  } 

  MSG0("Running AM correctness test with %i iterations, max_payload=%i, depth=%i...",iters,max_payload,depth);

  BARRIER();

  { int sz,iter,savesz = 1;
    int max1 = gasnet_AMMaxMedium(), max2 = maxlong;
    if (maxlong < gasnet_AMMaxMedium()) { max1 = maxlong; max2 = gasnet_AMMaxMedium(); }
    assert_always(max1 <= max2);

    for (sz = 1; sz <= max_payload; ) {
      #if 1
        BARRIER(); /* optional barrier, to separate tests at each payload size */
        MSG0("payload = %i",sz);
      #endif
      for (iter = 0; iter < iters; iter++) {
        int chunkidx;
        /* initialize local seg to known values */
        for (chunkidx = 0; chunkidx < depth; chunkidx++) {
          init_chunk(localseg,sz,iter,chunkidx);
        }
        if (sz <= gasnet_AMMaxMedium()) { /* test Medium AMs */
          gasnett_atomic_set(&pong_recvd,0,0);
          for (chunkidx = 0; chunkidx < depth; chunkidx++) {
            GASNET_Safe(gasnet_AMRequestMedium2(peerproc, hidx_ping_medhandler, localseg+chunkidx*sz, sz,
                                    iter, chunkidx));
          }
          /* wait for completion */
          GASNET_BLOCKUNTIL(gasnett_atomic_read(&pong_recvd,0) == depth);
        }

        if (sz <= maxlong) { /* test Long AMs */
          gasnett_atomic_set(&pong_recvd,0,0);
          for (chunkidx = 0; chunkidx < depth; chunkidx++) {
            GASNET_Safe(gasnet_AMRequestLong2(peerproc, hidx_ping_longhandler, localseg+chunkidx*sz, sz,
                                  peerreqseg+chunkidx*sz, iter, chunkidx));
          }
          /* wait for completion */
          GASNET_BLOCKUNTIL(gasnett_atomic_read(&pong_recvd,0) == depth);
        }
      }

      /* double sz each time, but make sure to also exactly hit MaxMedium, MaxLong and max payload */
      if (sz < max1 && savesz * 2 > max1) sz = max1;
      else if (sz < max2 && savesz * 2 > max2) sz = max2;
      else if (sz < max_payload && savesz * 2 > max_payload) sz = max_payload;
      else { sz = savesz * 2; savesz = sz; }
    }
  }

  BARRIER();
  done = 1;

  return(0);
}

/* $Source: bitbucket.org:berkeleylab/gasnet.git/tests/testcore2.c $
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
  #define TEST_SEGSZ_EXPR ((uintptr_t)max_payload*depth*5)
#endif

#include "test.h"

static gex_Client_t      myclient;
static gasnetex_endpoint_t    myep;
static gasnetex_team_member_t myteam;
static gex_Segment_t     mysegment;

int myproc;
int numproc;
int peerproc;
int numprocs;
int iters = 0;
int maxmed;
int maxlong;
volatile int done = 0;
int allowretry = 1;
uint8_t *myseg;      /* my segment */
int doinseg = 1;
int dooutseg = 1;
#define INSEG(iter) ((doinseg&&dooutseg)?(iter&0x1):doinseg)
#define ITERSEG(iter) (INSEG(iter)?localseg:privateseg)
uint8_t *peerreqseg; /* long request landing zone */
uint8_t *peerrepseg; /* long reply landing zone */
uint8_t *localseg;
uint8_t *privateseg;
uint8_t *longreplysrc;

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
  int errcnt = 0;
  int doretry = 0;
retry:
  for (elemidx = 0; elemidx < sz; elemidx++) {
    uint8_t actual = buf[elemidx];
    uint8_t expected = ELEM_VALUE(iter,chunkidx,elemidx);
    if (actual != expected) {
      int id = (uintptr_t)gasnett_threadkey_get(mythread); 
      ERR("TH%i data mismatch at sz=%i iter=%i chunk=%i elem=%i : actual=%02x expected=%02x in %s",
           id, (int)sz,iter,chunkidx,(int)elemidx,
           (unsigned int)actual,(unsigned int)expected,
           context);
      errcnt++;
    }
  }
  if (errcnt && allowretry && !doretry) {
    doretry = 1; errcnt = 0;
    sleep(1);
    goto retry;
  } else if (doretry) {
    if (errcnt == 0) MSG("retry DID clear errors");
    else MSG("retry DID NOT clear errors");
  }
}

/* Test handlers */
#define hidx_ping_medhandler     203
#define hidx_pong_medhandler     204

#define hidx_ping_longhandler    205
#define hidx_pong_longhandler    206

gasnett_atomic_t pong_recvd;

#define INIT_CHECKS() do {                               \
    gasnetex_rank_t srcnode;                                        \
    gasnet_AMGetMsgSource(token, &srcnode);                       \
    assert_always(srcnode == peerproc);                           \
    assert_always(iter < iters);                                  \
    assert_always(nbytes <= max_payload);                         \
  } while (0)


void ping_medhandler(gasnetex_token_t token, void *buf, size_t nbytes, 
                     gasnetex_handlerarg_t iter, gasnetex_handlerarg_t chunkidx) {
  INIT_CHECKS();
  validate_chunk("Medium Request (pre-reply)", buf, nbytes, iter, chunkidx);
  gex_AM_ReplyMedium2(token, hidx_pong_medhandler, buf, nbytes, GASNETEX_EVENT_NOW, 0, iter, chunkidx);
  validate_chunk("Medium Request (post-reply)", buf, nbytes, iter, chunkidx);
}
void pong_medhandler(gasnetex_token_t token, void *buf, size_t nbytes,
                     gasnetex_handlerarg_t iter, gasnetex_handlerarg_t chunkidx) {
  INIT_CHECKS();
  validate_chunk("Medium Reply", buf, nbytes, iter, chunkidx);
  gasnett_atomic_increment(&pong_recvd,0);
}

void ping_longhandler(gasnetex_token_t token, void *buf, size_t nbytes,
                     gasnetex_handlerarg_t iter, gasnetex_handlerarg_t chunkidx) {
  uint8_t *srcbuf;
  INIT_CHECKS();
  validate_chunk("Long Request", buf, nbytes, iter, chunkidx);
  if (INSEG(iter)) srcbuf = buf;
  else {
    srcbuf = longreplysrc+chunkidx*nbytes;
    memcpy(srcbuf, buf, nbytes);
  }
  gex_AM_ReplyLong2(token, hidx_pong_longhandler, srcbuf, nbytes, peerrepseg+chunkidx*nbytes, GASNETEX_EVENT_NOW, 0, iter, chunkidx);
}

void pong_longhandler(gasnetex_token_t token, void *buf, size_t nbytes,
                     gasnetex_handlerarg_t iter, gasnetex_handlerarg_t chunkidx) {
  INIT_CHECKS();
  validate_chunk("Long Reply", buf, nbytes, iter, chunkidx);
  gasnett_atomic_increment(&pong_recvd,0);
}


void *doit(void *id);

int doprime = 0;
int dosizesync = 1;
int domultith = 1;
int domed = 1;
int dolong = 1;
int amopt = 0;

int main(int argc, char **argv) {
  int arg = 1, help = 0;
  gasnetex_handlerentry_t htable[] = {
    { hidx_ping_medhandler,  ping_medhandler,  0, 2 },
    { hidx_pong_medhandler,  pong_medhandler,  0, 2 },
    { hidx_ping_longhandler, ping_longhandler, 0, 2 },
    { hidx_pong_longhandler, pong_longhandler, 0, 2 },
  };

  /* call startup */
  GASNET_Safe(gex_Client_Init(&myclient, &myep, &myteam, "testcore2", &argc, &argv, 0));

  #define AMOPT() if (!amopt) { amopt = 1; domed = 0; dolong = 0; }
  while (argc > arg) {
    if (!strcmp(argv[arg], "-p")) {
      doprime = 1;
      ++arg;
    } else if (!strcmp(argv[arg], "-u")) {
      dosizesync = 0;
      ++arg;
    } else if (!strcmp(argv[arg], "-s")) {
      domultith = 0;
      ++arg;
    } else if (!strcmp(argv[arg], "-n")) {
      allowretry = 0;
      ++arg;
    } else if (!strcmp(argv[arg], "-in")) {
      doinseg = 1; dooutseg = 0;
      ++arg;
    } else if (!strcmp(argv[arg], "-out")) {
      doinseg = 0; dooutseg = 1;
      ++arg;
    } else if (!strcmp(argv[arg], "-m")) {
      AMOPT();
      domed = 1; 
      ++arg;
    } else if (!strcmp(argv[arg], "-l")) {
      AMOPT();
      dolong = 1;
      ++arg;
    } else if (argv[arg][0] == '-') {
      help = 1;
      ++arg;
    } else break;
  }

  if (argc > arg) { iters = atoi(argv[arg]); arg++; }
  if (!iters) iters = 10;
  if (argc > arg) { max_payload = atoi(argv[arg]); arg++; }
  if (!max_payload) max_payload = 1024*1024;
  if (argc > arg) { depth = atoi(argv[arg]); arg++; }
  if (!depth) depth = 16;

  /* round down to largest payload AM allows with 2 arguments */
  maxmed  = MIN(gasnetex_max_AMRequestMedium(myteam,GASNETEX_ALL_RANKS,GASNETEX_EVENT_NOW,0,2),
                gasnetex_max_AMReplyMedium  (myteam,GASNETEX_ALL_RANKS,GASNETEX_EVENT_NOW,0,2));
  maxlong = MIN(gasnetex_max_AMRequestLong  (myteam,GASNETEX_ALL_RANKS,GASNETEX_EVENT_NOW,0,2),
                gasnetex_max_AMReplyLong    (myteam,GASNETEX_ALL_RANKS,GASNETEX_EVENT_NOW,0,2));
  max_payload = MIN(max_payload,MAX(maxmed,maxlong));

  GASNET_Safe(gex_Segment_Attach(&mysegment, myteam, TEST_SEGSZ_REQUEST));
  GASNET_Safe(gasnetex_EPRegisterHandlers(myep, htable, sizeof(htable)/sizeof(gasnetex_handlerentry_t)));
  test_init("testcore2",0,"[options] (iters) (max_payload) (depth)\n"
                 "  -m   test AMMedium    (defaults to all types)\n"
                 "  -l   test AMLong      (defaults to all types)\n"
                 "  -p   prime the AMLong transfer areas with puts, to encourage pinning\n"
                 "  -u   loosen sychronization to allow diff payload sizes to be in flight at once\n"
                 "  -s   single-threaded PAR mode (default is to start a polling thread in PAR mode)\n"
                 "  -n   no retry on failure\n"
                 "  -in/-out use only in- or out-of-segment sources for AMLong (default is both)\n"
                 );
  if (help || argc > arg) test_usage();

  TEST_PRINT_CONDUITINFO();

  /* get SPMD info */
  myproc = gasnet_mynode();
  numprocs = gasnet_nodes();

  peerproc = myproc ^ 1;
  if (peerproc == gasnet_nodes()) {
    /* w/ odd # of nodes, last one talks to self */
    peerproc = myproc;
  }
  myseg = TEST_MYSEG();
  peerreqseg = TEST_SEG(peerproc);
  peerrepseg = peerreqseg+max_payload*depth*2;
  localseg = myseg + max_payload*depth*4;
  assert_always(TEST_SEGSZ >= max_payload*depth*5);
  privateseg = test_malloc(max_payload*depth*2); /* out-of-seg request src, long reply src */
  longreplysrc = privateseg+max_payload*depth;

  #ifdef GASNET_PAR
    if (domultith) test_createandjoin_pthreads(2,doit,NULL,0);
    else
  #endif
      doit(0);

  BARRIER();
  test_free(privateseg);
  MSG("done. (detected %i errs)", test_errs);
  gasnet_exit(test_errs > 0 ? 1 : 0);
  return 0;
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

  MSG0("Running %sAM%s%s%s correctness test %s%swith %i iterations, max_payload=%i, depth=%i...",
#if GASNET_PAR
    (domultith?"multi-threaded ":"single-threaded "),
#else
    "",
#endif
    (amopt?(domed?" Medium":""):""),(amopt?(dolong?" Long":""):""),
    ((doinseg^dooutseg)?(doinseg?" in-segment":" out-of-segment"):""),
    (dosizesync?"":"loosely-synced "),
    (doprime?"with priming ":""),
    iters,max_payload,depth);

  BARRIER();
  if (doprime) { /* issue some initial puts that cover the Long regions, to try and trigger dynamic pinning */
    int chunkidx;
    for (chunkidx = 0; chunkidx < depth; chunkidx++) {
      /* AMRequestLong primer */
      gasnetex_put(myteam, peerproc, peerreqseg+chunkidx*max_payload, privateseg+chunkidx*max_payload, max_payload, 0);
      gasnetex_put(myteam, peerproc, peerreqseg+chunkidx*max_payload, localseg+chunkidx*max_payload, max_payload, 0);
      /* AMReplyLong primer */
      gasnetex_put(myteam, peerproc, peerrepseg+chunkidx*max_payload, myseg+chunkidx*max_payload, max_payload, 0);
      gasnetex_put(myteam, peerproc, peerrepseg+chunkidx*max_payload, longreplysrc+chunkidx*max_payload, max_payload, 0);
    }
    BARRIER();
  }

  { int sz,iter,savesz = 1;
    int max1 = maxmed, max2 = maxlong;
    if (maxlong < maxmed) { max1 = maxlong; max2 = maxmed; }
    assert_always(max1 <= max2);

    for (sz = 1; sz <= max_payload; ) {
      if (dosizesync) BARRIER(); /* optional barrier, to synchronize tests at each payload size across nodes */
      
      MSG0("payload = %i",sz);

      for (iter = 0; iter < iters; iter++) {
        int chunkidx;
        uint8_t *srcseg = ITERSEG(iter);

        /* initialize local seg to known values */
        for (chunkidx = 0; chunkidx < depth; chunkidx++) {
          init_chunk(srcseg,sz,iter,chunkidx);
        }
        if (domed && sz <= maxmed) { /* test Medium AMs */
          gasnett_atomic_set(&pong_recvd,0,0);
          for (chunkidx = 0; chunkidx < depth; chunkidx++) {
            gex_AM_RequestMedium2(myteam, peerproc, hidx_ping_medhandler, srcseg+chunkidx*sz, sz,
                                      GASNETEX_EVENT_NOW, 0, iter, chunkidx);
          }
          /* wait for completion */
          GASNET_BLOCKUNTIL(gasnett_atomic_read(&pong_recvd,0) == depth);
        }

        if (sz <= maxlong) { 
         if (dolong) { /* test Long AMs */
          gasnett_atomic_set(&pong_recvd,0,0);
          for (chunkidx = 0; chunkidx < depth; chunkidx++) {
            gex_AM_RequestLong2(myteam, peerproc, hidx_ping_longhandler, srcseg+chunkidx*sz, sz,
                                    peerreqseg+chunkidx*sz,  GASNETEX_EVENT_NOW, 0, iter, chunkidx);
          }
          /* wait for completion */
          GASNET_BLOCKUNTIL(gasnett_atomic_read(&pong_recvd,0) == depth);
         }
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

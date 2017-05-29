/*   $Source: bitbucket.org:berkeleylab/gasnet.git/tests/testhsl.c $
 * Description: GASNet HSL correctness test
 * Copyright 2002, Dan Bonachea <bonachea@cs.berkeley.edu>
 * Terms of use are as specified in license.txt
 */

#include <gasnetex.h>

#include <test.h>

static gasnetex_client_t      myclient;
static gasnetex_endpoint_t    myep;
static gasnetex_team_member_t myteam;
static gasnetex_segment_t     mysegment;

int peer = -1;
int flag = 0;
uint64_t iters = 100;
gasnetex_hsl_t globallock = GASNETEX_HSL_INITIALIZER;

void okhandler3(gasnetex_token_t token) {
  gasnetex_hsl_lock(&globallock);
  flag++;
  gasnetex_hsl_unlock(&globallock);
}


void badhandler1(gasnetex_token_t token) {
  gasnetex_hsl_lock(&globallock);
}
void badhandler2(gasnetex_token_t token) {
  gasnetex_hsl_lock(&globallock);
  gasnetex_AMReplyShort0(token, 250, 0);
}

uint64_t counter = 0;
void increq(gasnetex_token_t token) {
  gasnetex_hsl_lock(&globallock);
  counter++;
  gasnetex_hsl_unlock(&globallock);
  gasnetex_AMReplyShort0(token, 222, 0);
}
gasnetex_hsl_t replock = GASNETEX_HSL_INITIALIZER;
uint64_t repcounter = 0;
void increp(gasnetex_token_t token) {
  gasnetex_hsl_lock(&replock);
  repcounter++;
  gasnetex_hsl_unlock(&replock);
}


void donothing(gasnetex_token_t token) {
}

#if GASNET_PAR
  int NUM_THREADS = 4;
  void * thread_fn(void *arg);
#endif

int main(int argc, char **argv) {
  int mynode, nodes;
  gasnetex_handlerentry_t htable[] = { 
    { 203, okhandler3,  0, 0 },

    { 221, increq,      0, 0 },
    { 222, increp,      0, 0 },

    { 231, badhandler1, 0, 0 },
    { 232, badhandler2, 0, 0 },

    { 250, donothing,   0, 0 }
  };

  GASNET_Safe(gasnetex_ClientInit(&myclient, &myep, &myteam, &argc, &argv, "testhsl", 0));
  GASNET_Safe(gasnetex_TeamSegmentCreate(&mysegment, myteam, NULL, TEST_SEGSZ_REQUEST, GASNETEX_MEMKIND_DEFAULT, 0));
  GASNET_Safe(gasnetex_EPRegisterHandlers(myep, htable, sizeof(htable)/sizeof(gasnetex_handlerentry_t)));
  test_init("testhsl",0,"(0|errtestnum:1..16)");

  mynode = gasnet_mynode();
  nodes = gasnet_nodes();
  peer = (gasnet_mynode() ^ 1);
  if (peer == gasnet_nodes()) peer = gasnet_mynode();

  if (argc < 2) test_usage();
  {
    int errtest = atoi(argv[1]);
    gasnetex_hsl_t lock1 = GASNETEX_HSL_INITIALIZER;
    gasnetex_hsl_t lock2;
    gasnetex_hsl_init(&lock2);

    MSG0("testing legal local cases...");
    gasnetex_hsl_lock(&lock1);
    gasnetex_hsl_lock(&lock2);
    assert(mynode == gasnet_mynode()); 
    assert(nodes == gasnet_nodes());
    gasnetex_hsl_unlock(&lock2);
    gasnetex_hsl_unlock(&lock1);

    assert_always(gasnetex_hsl_trylock(&lock1) == GASNET_OK);
    gasnetex_hsl_unlock(&lock1);

    BARRIER();
    MSG0("testing legal AM cases...");

    gasnetex_AMRequestShort0(myteam, peer, 203, 0);
    GASNET_BLOCKUNTIL(flag == 1);

    BARRIER();

   if (errtest) {
    int dummy = 0;
    MSG0("testing illegal case %i...", errtest);
    switch(errtest) {
      case 1:
        gasnetex_hsl_init(&lock1);
      break;
      case 2:
        gasnetex_hsl_destroy(&lock1);
        gasnetex_hsl_destroy(&lock1);
      break;
      case 3:
        gasnetex_hsl_unlock(&lock1);
      break;
      case 4:
        gasnetex_hsl_lock(&lock1);
        gasnetex_hsl_lock(&lock2);
        gasnetex_hsl_unlock(&lock1);
      break;
      case 5:
        gasnetex_hsl_lock(&lock1);
        gasnetex_hsl_lock(&lock1);
      break;
      case 6:
        dummy += gasnetex_hsl_trylock(&lock1);
        dummy += gasnetex_hsl_trylock(&lock1);
      break;
      case 7:
        gasnetex_hsl_lock(&lock1);
        gasnet_AMPoll();
      break;
      case 8:
        gasnetex_AMRequestShort0(myteam, gasnet_mynode(), 231, 0);
        GASNET_BLOCKUNTIL(0);
      break;
      case 9:
        gasnetex_AMRequestShort0(myteam, gasnet_mynode(), 232, 0);
        GASNET_BLOCKUNTIL(0);
      break;
      case 10:
        gasnetex_hsl_lock(&lock1);
        gasnetex_AMRequestShort0(myteam, gasnet_mynode(), 250, 0);
        gasnetex_hsl_unlock(&lock1);
      break;
      case 11:
        gasnetex_hsl_lock(&lock1);
        sleep(2);
        gasnetex_hsl_unlock(&lock1);
        goto done;
      break;
      case 12:
        dummy += gasnetex_hsl_trylock(&lock1);
        sleep(2);
        gasnetex_hsl_unlock(&lock1);
        goto done;
      break;
      default:
        ERR("bad err test num.");
        test_usage();
    }
    FATALERR("FAILED: err test failed.");
   } else {
  #if GASNET_PAR
    MSG0("Spawning pthreads...");
    NUM_THREADS = test_thread_limit(NUM_THREADS);
    test_createandjoin_pthreads(NUM_THREADS, &thread_fn, NULL, 0);
  #endif
   }
  }

done:
  BARRIER();

  MSG0("done.");

  BARRIER();
  gasnet_exit(0);
  return 0;
}

#if GASNET_PAR

#undef MSG0
#undef ERR
#define MSG0 THREAD_MSG0(id)
#define ERR  THREAD_ERR(id)

void * thread_fn(void *arg) {
  int id = (int)(uintptr_t)arg;
  uint64_t iters2 = iters*100;
  uint64_t i;

  counter = 0; repcounter = 0;
  PTHREAD_BARRIER(NUM_THREADS);

  MSG0("hsl exclusion test, local-only...");
    for (i=0;i<iters2;i++) {
      if (i&1) {
        gasnetex_hsl_lock(&globallock);
      } else {
        int retval;
        while ((retval=gasnetex_hsl_trylock(&globallock)) != GASNET_OK) {
          assert_always(retval == GASNET_ERR_NOT_READY);
        }
      }
      counter++;
      gasnetex_hsl_unlock(&globallock);
    }

    PTHREAD_LOCALBARRIER(NUM_THREADS);

    if (counter != (NUM_THREADS * iters2)) 
      ERR("failed hsl test: counter=%"PRIu64" expecting=%"PRIu64, 
          counter, (NUM_THREADS * iters2));

  PTHREAD_BARRIER(NUM_THREADS);
  counter = 0; repcounter = 0;
  PTHREAD_BARRIER(NUM_THREADS);

  MSG0("hsl exclusion test, AM-only...");
    for (i=0;i<iters;i++) {
      gasnetex_AMRequestShort0(myteam, peer, 221, 0);
    }
    GASNET_BLOCKUNTIL(repcounter == NUM_THREADS * iters);
    PTHREAD_BARRIER(NUM_THREADS);

    if (counter != (NUM_THREADS * iters)) 
      ERR("failed hsl test: counter=%"PRIu64" expecting=%"PRIu64, 
          counter, (NUM_THREADS * iters));

  PTHREAD_BARRIER(NUM_THREADS);
  counter = 0; repcounter = 0;
  PTHREAD_BARRIER(NUM_THREADS);

  MSG0("hsl exclusion test, AM & local...");
    for (i=0;i<iters;i++) {
      gasnetex_AMRequestShort0(myteam, peer, 221, 0);
      if (i&1) {
        gasnetex_hsl_lock(&globallock);
      } else {
        int retval;
        while ((retval=gasnetex_hsl_trylock(&globallock)) != GASNET_OK) {
          assert_always(retval == GASNET_ERR_NOT_READY);
        }
      }
      counter++;
      gasnetex_hsl_unlock(&globallock);
    }
    GASNET_BLOCKUNTIL(repcounter == NUM_THREADS * iters);
    PTHREAD_BARRIER(NUM_THREADS);

    if (counter != (2 * NUM_THREADS * iters)) 
      ERR("failed hsl test: counter=%"PRIu64" expecting=%"PRIu64, 
          counter, (2 * NUM_THREADS * iters));

  PTHREAD_BARRIER(NUM_THREADS);
  counter = 0; repcounter = 0;
  PTHREAD_BARRIER(NUM_THREADS);

  return NULL;
}

#endif

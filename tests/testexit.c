/*   $Source: /Users/kamil/work/gasnet-cvs2/gasnet/tests/testexit.c,v $
 *     $Date: 2006/02/19 11:08:36 $
 * $Revision: 1.16.4.1 $
 * Description: GASNet gasnet_exit correctness test
 * Copyright 2002, Dan Bonachea <bonachea@cs.berkeley.edu>
 * Terms of use are as specified in license.txt
 */

#include <gasnet.h>
#include <gasnet_tools.h>

#include <test.h>
#include <signal.h>

int mynode, nodes;
int peer = -1;
int testid = 0;
int numpthreads = 4;
#define thread_barrier() PTHREAD_BARRIER(numpthreads)

/* test various modes of exiting a GASNet program.
   Basically, none of these should hang or leave orphaned processes.
   Those with non-collective exit should cause the SIGQUIT handler to 
    fire on the non-exiting nodes.
*/
const char *testdesc[] = {
  "simultaneous collective gasnet_exit(1)",
  "simultaneous return from main()... exit_code 2",
  "non-collective gasnet_exit(3), others in barrier",
  "non-collective SIGINT, others in barrier ... exit_code 4",
  "non-collective gasnet_exit(5), others in spin-loop",
  "collective gasnet_exit(6) between init()/attach()",
  "non-collective gasnet_exit(7) between init()/attach()",
  "non-collective return(8) from main(), others in barrier",
  "non-collective return(9) from main(), others in spin-loop",
  "collective gasnet_exit(10) from AM handlers on all nodes",
  "non-collective gasnet_exit(11) from AM handler on one node",
  "non-collective gasnet_exit(12) from AM handler on one node (loopback)",
  "non-collective gasnet_exit(13) from AM handler on one node (N requests)",
#ifdef GASNET_PAR
  "collective gasnet_exit(14) from all pthreads on all nodes",
  "non-collective gasnet_exit(15) from one pthread, other local in barrier",
  "non-collective gasnet_exit(16) from one pthread, others in spin-loop",
  "non-collective gasnet_exit(17) from one pthread, others in poll-loop",
  "non-collective gasnet_exit(18) from one pthread, others sending messages",
#endif
};
#define MAXTEST (sizeof(testdesc)/sizeof(char*))

#define hidx_exit_handler		201
#define hidx_noop_handler               202
#define hidx_ping_handler               203

void test_exit_handler(gasnet_token_t token, gasnet_handlerarg_t exitcode) {
  gasnet_exit((int)exitcode);
}

void ping_handler(gasnet_token_t token, void *buf, size_t nbytes) {
  static int x = 1; 
  gasnet_node_t src;
  gasnet_AMGetMsgSource(token, &src);
  x = !x;/* harmless race */
  if (x) 
    GASNET_Safe(gasnet_AMReplyMedium0(token, hidx_noop_handler, buf, nbytes));
  else
    GASNET_Safe(gasnet_AMReplyLong0(token, hidx_noop_handler, buf, nbytes, TEST_SEG(src)));
}

void noop_handler(gasnet_token_t token, void *buf, size_t nbytes) {
}

#ifdef GASNET_PAR
void *workerthread(void *args) {
  int mythread = (int)(intptr_t)args;
  thread_barrier();
  switch (testid) {
    case 14:
      gasnet_exit(14);
      break;
    case 15:
      if (mynode == 0) {
        if (mythread == 0) { 
          sleep(1); 
          gasnet_exit(15); 
          MSG("TEST FAILED!!");
        } else if (mythread == 1) BARRIER();
      } else {
        if (mythread == 0) while(1) GASNET_Safe(gasnet_AMPoll());
      }
      while(1) ;
      break;
    case 16:
      if (mynode == 0 && mythread == 0) { 
          sleep(1); 
          gasnet_exit(16); 
      } else while(1);
      break;
    case 17:
      if (mynode == 0 && mythread == 0) { 
          sleep(1); 
          gasnet_exit(17); 
      } else while(1) GASNET_Safe(gasnet_AMPoll());
      break;
    case 18:
      if (mynode == 0 && mythread == 0) { 
          sleep(1); 
          gasnet_exit(18); 
      } else {
        int junk;
        int lim = MIN(MIN(MIN(gasnet_AMMaxMedium(), gasnet_AMMaxLongRequest()), gasnet_AMMaxLongReply()), TEST_SEGSZ);
        char *p = malloc(lim);
        char *peerseg = TEST_SEG(peer);
        while (1) {
          switch (rand() % 18) {
            case 0:  GASNET_Safe(gasnet_AMPoll()); break;
            case 1:  GASNET_Safe(gasnet_AMRequestMedium0(peer, hidx_noop_handler, p, 4)); break;
            case 2:  GASNET_Safe(gasnet_AMRequestMedium0(peer, hidx_ping_handler, p, 4)); break;
            case 3:  GASNET_Safe(gasnet_AMRequestMedium0(peer, hidx_noop_handler, p, lim)); break;
            case 4:  GASNET_Safe(gasnet_AMRequestMedium0(peer, hidx_ping_handler, p, lim)); break;
            case 5:  GASNET_Safe(gasnet_AMRequestLong0(peer, hidx_noop_handler, p, 4, peerseg)); break;
            case 6:  GASNET_Safe(gasnet_AMRequestLong0(peer, hidx_ping_handler, p, 4, peerseg)); break;
            case 7:  GASNET_Safe(gasnet_AMRequestLong0(peer, hidx_noop_handler, p, lim, peerseg)); break;
            case 8:  GASNET_Safe(gasnet_AMRequestLong0(peer, hidx_ping_handler, p, lim, peerseg)); break;
            case 9:  gasnet_put(peer, peerseg, &junk, sizeof(int)); break;
            case 10: gasnet_get(&junk, peer, peerseg, sizeof(int)); break;
            case 11: gasnet_put(peer, peerseg, p, lim); break;
            case 12: gasnet_get(p, peer, peerseg, lim); break;
            case 13: gasnet_put_nbi(peer, peerseg, &junk, sizeof(int)); break;
            case 14: gasnet_get_nbi(&junk, peer, peerseg, sizeof(int)); break;
            case 15: gasnet_put_nbi(peer, peerseg, p, lim); break;
            case 16: gasnet_get_nbi(p, peer, peerseg, lim); break;
            case 17: gasnet_wait_syncnbi_all(); break;
          }
        }
      }
      break;
    default:
      abort();
  }

  /* if we ever reach here, something really bad happenned */
  MSG("TEST FAILED!!");
  abort();
  return NULL;
}
#endif

typedef void (*test_sighandlerfn_t)(int);
void testSignalHandler(int sig) {
  if (sig != SIGQUIT) {
    MSG("ERROR! got an unexpected signal!");
    abort();
  } else {
    MSG("in SIGQUIT handler, calling gasnet_exit(4)...");
    gasnet_exit(4);
  }
}

int main(int argc, char **argv) {
  char *argvzero;
  const char *pth_args = "";
  gasnet_handlerentry_t htable[] = { 
    { hidx_exit_handler, test_exit_handler },
    { hidx_ping_handler, ping_handler },
    { hidx_noop_handler, noop_handler },
  };

  GASNET_Safe(gasnet_init(&argc, &argv));
  test_init_early("testexit",0);

  mynode = gasnet_mynode();
  nodes = gasnet_nodes();

  peer = mynode ^ 1;
  if (peer == nodes) {
    /* w/ odd # of nodes, last one talks to self */
    peer = mynode;
  }

  argvzero = argv[0];
  argv++; argc--;
  if (argc > 0) { testid = atoi(*argv); argv++; argc--; }
  #ifdef GASNET_PAR
    if (argc > 0) { numpthreads = atoi(*argv); argv++; argc--; }
    pth_args = " (num_pthreads)";
  #endif
  if (argc > 0 || testid <= 0 || testid > MAXTEST || numpthreads <= 1) {
    printf("Usage: %s (errtestnum:1..%i)%s\n", argvzero, (int)MAXTEST, pth_args);fflush(stdout);
    gasnet_exit(-1);
  }

  if (testid == 6 || testid == 7) {
    if (mynode == 0) {
        printf("Running exit test %i...\n",testid);
        printf("%s\n",testdesc[testid-1]);
        fflush(stdout);
    }
    gasnett_sched_yield();
    sleep(1);
    if (testid == 6) {
      gasnet_exit(6);
      abort();
    } else if (testid == 7 && mynode == nodes - 1) {
      gasnet_exit(7);
      abort();
    }
  }

  GASNET_Safe(gasnet_attach(htable,  sizeof(htable)/sizeof(gasnet_handlerentry_t),
	                    TEST_SEGSZ_REQUEST, TEST_MINHEAPOFFSET));

  /* register a SIGQUIT handler, as permitted by GASNet spec */
  { test_sighandlerfn_t fpret = (test_sighandlerfn_t)signal(SIGQUIT, testSignalHandler); 
    if (fpret == (test_sighandlerfn_t)SIG_ERR) {
      MSG("Got a SIG_ERR while registering SIGQUIT handler");
      perror("signal");
      abort();
    }
  }

  TEST_SEG(mynode);

  BARRIER();
  if (mynode == 0) {
      printf("Running exit test %i...\n",testid);
      printf("%s\n",testdesc[testid-1]);
      fflush(stdout);
  }
  BARRIER();

  switch (testid) {
    case 1: 
      gasnet_exit(testid);
      break;
    case 2: 
      return testid;
      break;
    case 3: 
      if (mynode == nodes-1) { sleep(1); gasnet_exit(testid); }
      else BARRIER();
      break;
    case 4: 
      if (mynode == nodes-1) { 
        sleep(1); 
        /*raise(SIGINT); */
        kill(getpid(), SIGINT); /* more reliable */
        while (1) gasnett_sched_yield(); /* await delivery */
      } else BARRIER();
      break;
    case 5: 
      if (mynode == nodes-1) { sleep(1); gasnet_exit(testid); }
      else while(1);
      break;
    case 8: 
      if (mynode == nodes-1) { sleep(1); return testid; }
      else BARRIER();
      break;
    case 9: 
      if (mynode == nodes-1) { sleep(1); return testid; }
      else while(1);
      break;
    case 10:
      GASNET_Safe(gasnet_AMRequestShort1(peer, hidx_exit_handler, testid));
      while(1) GASNET_Safe(gasnet_AMPoll());
      break;
    case 11:
      if (mynode == 0) { 
        GASNET_Safe(gasnet_AMRequestShort1(nodes-1, hidx_exit_handler, testid));
      }
      while(1) GASNET_Safe(gasnet_AMPoll());
      break;
    case 12:
      if (mynode == nodes-1) { 
        GASNET_Safe(gasnet_AMRequestShort1(mynode, hidx_exit_handler, testid));
      }
      while(1) GASNET_Safe(gasnet_AMPoll());
      break;
    case 13:
      GASNET_Safe(gasnet_AMRequestShort1(nodes-1, hidx_exit_handler, testid));
      while(1) GASNET_Safe(gasnet_AMPoll());
      break;
  #ifdef GASNET_PAR
    case 14: case 15: case 16: case 17: case 18: {
      pthread_t *tt_tids = test_malloc(numpthreads*sizeof(pthread_t));
      int i;
      for (i = 1; i < numpthreads; i++) {
        pthread_attr_t attr;
        pthread_attr_init(&attr);
        pthread_attr_setscope(&attr, PTHREAD_SCOPE_SYSTEM);
        if (pthread_create(&tt_tids[i], &attr, workerthread, (void *)(intptr_t)i) != 0) { MSG("ERROR forking threads\n"); gasnet_exit(-1); }
      }
      workerthread(0);
      break;
    }
  #endif
    default:
      abort();
  }

  /* if we ever reach here, something really bad happenned */
  MSG("TEST FAILED!!");
  abort();
}

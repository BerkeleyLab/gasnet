/*  $Archive:: /Ti/GASNet/tests/testbarrier.c                             $
 *     $Date: 2004/01/05 05:01:24 $
 * $Revision: 1.6 $
 * Description: GASNet gasnet_exit correctness test
 * Copyright 2002, Dan Bonachea <bonachea@cs.berkeley.edu>
 * Terms of use are as specified in license.txt
 */

#include <gasnet.h>
#include <gasnet_tools.h>

#include <test.h>
#include <signal.h>
#include <sched.h>

int mynode, nodes;

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
};
#define MAXTEST (sizeof(testdesc)/sizeof(char*))

                                                                                                              
#define hidx_exit_handler		201

void test_exit_handler(gasnet_token_t token, gasnet_handlerarg_t exitcode) {
  gasnet_exit((int)exitcode);
}

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
  int testid = 0;
  int peer = -1;
  gasnet_handlerentry_t htable[] =
  	{ { hidx_exit_handler, test_exit_handler } };

  GASNET_Safe(gasnet_init(&argc, &argv));

  mynode = gasnet_mynode();
  nodes = gasnet_nodes();

  peer = mynode ^ 1;
  if (peer == nodes) {
    /* w/ odd # of nodes, last one talks to self */
    peer = mynode;
  }
	  
  MSG("running...");

  if (argc > 1) testid = atoi(argv[1]);
  if (argc < 2 || testid <= 0 || testid > MAXTEST) {
    printf("Usage: %s (errtestnum:1..%i)\n", argv[0], (int)MAXTEST);fflush(stdout);
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
	      TEST_SEGSZ, TEST_MINHEAPOFFSET));

  /* register a SIGQUIT handler, as permitted by GASNet spec */
  { test_sighandlerfn_t fpret = (test_sighandlerfn_t)signal(SIGQUIT, testSignalHandler); 
    if (fpret == (test_sighandlerfn_t)SIG_ERR) {
      MSG("Got a SIG_ERR while registering SIGQUIT handler");
      perror("signal");
      abort();
    }
  }

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
      if (mynode == nodes-1) { sleep(1); raise(SIGINT); }
      else BARRIER();
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

    default:
      abort();
  }

  /* if we ever reach here, something really bad happenned */
  MSG("TEST FAILED!!");
  abort();
}

/*  $Archive:: /Ti/GASNet/tests/testbarrier.c                             $
 *     $Date: 2003/06/24 18:55:07 $
 * $Revision: 1.4 $
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
char *testdesc[] = {
  "simultaneous collective gasnet_exit(1)",
  "simultaneous return from main()... exit_code 2",
  "non-collective gasnet_exit(3), others in barrier",
  "non-collective SIGINT, others in barrier ... exit_code 4",
  "non-collective gasnet_exit(5), others in spin-loop",
  "collective gasnet_exit(6) between init()/attach()",
  "non-collective gasnet_exit(7) between init()/attach()",
  "non-collective return(8) from main(), others in barrier",
  "non-collective return(9) from main(), others in spin-loop",
};
#define MAXTEST (sizeof(testdesc)/sizeof(char*))

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

  GASNET_Safe(gasnet_init(&argc, &argv));

  mynode = gasnet_mynode();
  nodes = gasnet_nodes();

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

  GASNET_Safe(gasnet_attach(NULL, 0, TEST_SEGSZ, TEST_MINHEAPOFFSET));

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
      if (mynode == nodes-1) { sleep(1); return 8; }
      else BARRIER();
      break;
    case 9: 
      if (mynode == nodes-1) { sleep(1); return 9; }
      else while(1);
      break;

    default:
      abort();
  }

  /* if we ever reach here, something really bad happenned */
  MSG("TEST FAILED!!");
  abort();
}

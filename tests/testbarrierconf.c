/*  $Archive:: /Ti/GASNet/tests/testbarrier.c                             $
 *     $Date: 2004/03/05 18:46:39 $
 * $Revision: 1.1 $
 * Description: GASNet barrier performance test
 * Copyright 2002, Dan Bonachea <bonachea@cs.berkeley.edu>
 * Terms of use are as specified in license.txt
 */

#include <gasnet.h>

#include <test.h>

int main(int argc, char **argv) {
  int mynode;
  int result;
  int i = 0;

  GASNET_Safe(gasnet_init(&argc, &argv));
  GASNET_Safe(gasnet_attach(NULL, 0, TEST_SEGSZ, TEST_MINHEAPOFFSET));

  MSG("running...");

  mynode = gasnet_mynode();

  if (mynode == 0) {
      printf("Running barrier conformance test...\n");
      fflush(stdout);
  }
  BARRIER();

  /* Test for required failures: */

  /* node 0 indicates mismatch on entry: */
  gasnet_barrier_notify(0, !mynode ? GASNET_BARRIERFLAG_MISMATCH : 0);
  result = gasnet_barrier_wait(0, !mynode ? GASNET_BARRIERFLAG_MISMATCH : 0);
  if (result != GASNET_ERR_BARRIER_MISMATCH) {
    MSG("Failed to detect barrier mismatch indicated on notify.");
    gasnet_exit(1);
  }

  /* ids differ between notify and wait */
  gasnet_barrier_notify(0, 0);
  result = gasnet_barrier_wait(1, 0);
  if (result != GASNET_ERR_BARRIER_MISMATCH) {
    MSG("Failed to detect mismatch between id at notify and wait.");
    gasnet_exit(1);
  }

  /* Flags differ between notify and wait: */
  gasnet_barrier_notify(0, GASNET_BARRIERFLAG_ANONYMOUS);
  result = gasnet_barrier_wait(0, 0);
  if (result != GASNET_ERR_BARRIER_MISMATCH) {
    MSG("Failed to detect anonymous notify with named wait.");
    gasnet_exit(1);
  }
  gasnet_barrier_notify(0, 0);
  result = gasnet_barrier_wait(0, GASNET_BARRIERFLAG_ANONYMOUS);
  if (result != GASNET_ERR_BARRIER_MISMATCH) {
    MSG("Failed to detect named notify with anonymous wait.");
    gasnet_exit(1);
  }

  /* Mismatched id: */
  if (gasnet_nodes() > 1) {
    gasnet_barrier_notify(!mynode, 0);
    result = gasnet_barrier_wait(0, 0);
    if (result != GASNET_ERR_BARRIER_MISMATCH) {
      MSG("Failed to detect different id on node 0.");
      gasnet_exit(1);
    }
  }

  BARRIER();

  MSG("done.");

  gasnet_exit(0);
  return 0;
}

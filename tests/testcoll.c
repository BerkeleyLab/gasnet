/*  $Archive:: /Ti/GASNet/tests/testcoll.c                                 $
 *     $Date: 2004/05/10 18:20:02 $
 * $Revision: 1.1.2.1 $
 * Description: GASNet collectives test
 * Copyright 2002-2004, Jaein Jeong and Dan Bonachea <bonachea@cs.berkeley.edu>
 * Terms of use are as specified in license.txt
 */

#include "gasnet.h"
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <sys/time.h>
#include <unistd.h>
#include <fcntl.h>
#include "test.h"

#define DEFAULT_SZ	(32*1024)

#define PRINT_LATENCY 0
#define PRINT_THROUGHPUT 1

#ifndef GASNET_ALIGNED_SEGMENTS
 #error "This test requires aligned segments"
#endif

int myproc;
int numprocs;
int peerproc;

int *segment;

int main(int argc, char **argv)
{
    int arg;
    int iters = 0;
    int size = 0;
    int i, j;
   
    /* call startup */
    GASNET_Safe(gasnet_init(&argc, &argv));
    GASNET_Safe(gasnet_attach(NULL, 0, TEST_SEGSZ, TEST_MINHEAPOFFSET));

    if (argc > 1) {
      iters = atoi(argv[1]);
    }
    if (iters < 1) {
      iters = 1000;
    }
    /* get SPMD info */
    myproc = gasnet_mynode();
    numprocs = gasnet_nodes();
    
    if (myproc == 0) {
	printf("Running coll test(s) with %d iterations.\n", iters);
    }

    segment = (int *) TEST_MYSEG();

    MSG("running.");
    BARRIER();

    for (j = 0; j < iters; ++j) {

      for (i = 0; i < numprocs; ++i) {
//fprintf(stderr, "%d> i=%d j=%d\n", myproc, i, j);
        *segment = -1;
        (void)gasnet_coll_broadcast_nb(GASNET_TEAM_ALL, segment, i, &myproc, sizeof(int),
				      GASNET_COLL_SINGLE |
				      GASNET_COLL_IN_MYSYNC |
				      GASNET_COLL_OUT_NOSYNC |
				      GASNET_COLL_SRC_IN_SEGMENT |
				      GASNET_COLL_DST_IN_SEGMENT |
				      GASNET_COLL_AGGREGATE);
        gasnet_coll_broadcast(GASNET_TEAM_ALL, segment+1, i, &myproc, sizeof(int),
				      GASNET_COLL_SINGLE |
				      GASNET_COLL_IN_NOSYNC |
				      GASNET_COLL_OUT_MYSYNC |
				      GASNET_COLL_SRC_IN_SEGMENT |
				      GASNET_COLL_DST_IN_SEGMENT);

	if (*segment != i) {
          MSG("Expected %d got %d", i, *segment);
	}
      }
    }

    BARRIER();
    MSG("done.");

    gasnet_exit(0);

    return 0;

}
/* ------------------------------------------------------------------------------------ */

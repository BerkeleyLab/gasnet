/*  $Archive:: /Ti/GASNet/tests/testcoll.c                                 $
 *     $Date: 2004/05/10 23:19:15 $
 * $Revision: 1.1.2.2 $
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
    gasnet_coll_init(NULL, 0);

    segment = (int *) TEST_MYSEG();

    MSG("running.");
    BARRIER();

    for (j = 0; j < iters; ++j) {
      *segment = -1;
      for (i = 0; i < numprocs; ++i) {
	int src = j ^ myproc;
	int want = j ^ i;
	int want2 = (i < (numprocs - 1)) ? (j ^ (i+1)) : (j+1);	/* exactly 1 step ahead */
	int tmp;
	gasnet_coll_handle_t h;

        h = gasnet_coll_broadcast_nb(GASNET_TEAM_ALL, segment+2, i, &src, sizeof(int),
				     GASNET_COLL_SINGLE |
				     GASNET_COLL_IN_ALLSYNC |
				     GASNET_COLL_OUT_ALLSYNC |
				     GASNET_COLL_SRC_IN_SEGMENT |
				     GASNET_COLL_DST_IN_SEGMENT);

        (void)gasnet_coll_broadcast_nb(GASNET_TEAM_ALL, segment, i, &src, sizeof(int),
				      GASNET_COLL_SINGLE |
				      GASNET_COLL_IN_MYSYNC |
				      GASNET_COLL_OUT_NOSYNC |
				      GASNET_COLL_SRC_IN_SEGMENT |
				      GASNET_COLL_DST_IN_SEGMENT |
				      GASNET_COLL_AGGREGATE);
        gasnet_coll_broadcast(GASNET_TEAM_ALL, segment+1, i, &src, sizeof(int),
				      GASNET_COLL_SINGLE |
				      GASNET_COLL_IN_NOSYNC |
				      GASNET_COLL_OUT_MYSYNC |
				      GASNET_COLL_SRC_IN_SEGMENT |
				      GASNET_COLL_DST_IN_SEGMENT);
	tmp = segment[0];
	if (tmp != want) {
          MSG("Expected segment[0]=%d got %d", want, tmp);
	}
	/* Carefully verify segment[1], it could legally be 1 iteration ahead (but no more) */
	tmp = segment[1];
	if ((tmp != want) && (tmp != want2)) {
          MSG("Expected segment[1]=%d or %d got %d", want, want2, tmp);
	}
	gasnet_coll_wait_sync(h);
	tmp = segment[2];
	if (tmp != want) {
          MSG("Expected segment[2]=%d got %d", want, tmp);
	}
      }
    }

    BARRIER();
    MSG("done.");

    gasnet_exit(0);

    return 0;

}
/* ------------------------------------------------------------------------------------ */

/*  $Archive:: /Ti/GASNet/tests/testrand.c                                 $
 *     $Date: 2004/02/09 19:40:47 $
 * $Revision: 1.1 $
 * Description: GASNet get/put performance test
 *   measures measures the total time to write to each page of the
 *   remote test segment, using blocking puts in a random order.
 *   This is meant to measure the cost of dynamic pinning.
 * Copyright 2002-4, Jaein Jeong and Dan Bonachea <bonachea@cs.berkeley.edu>
 * Terms of use are as specified in license.txt
 */

/************************************************************
	testrand.c:
		measures the cost of randomly ordered writes
		to the remote segment to stress test dynamic
		pinning.
		
*************************************************************/

#include "gasnet.h"
#include <stdio.h>
#include <stdlib.h>

#include "test.h"

int myproc;
int numprocs;
int peerproc;
unsigned int seed = 0;
int nbytes = 8;

void *remmem;
void *locmem;

void do_test(void) {GASNET_BEGIN_FUNCTION();
    int i;
    int64_t begin, end;
    int iamsender = (myproc % 2 == 0);
    int pages = TEST_SEGSZ / PAGESZ;
    void **loc_addr = test_malloc(pages * sizeof(void *));
    void **rem_addr = test_malloc(pages * sizeof(void *));
    
	if (iamsender) {
		/* create in-order arrays of page addresses */
		for (i = 0; i < pages; ++i) {
		    loc_addr[i] = (void *)((uintptr_t)locmem + (i * PAGESZ));
		    rem_addr[i] = (void *)((uintptr_t)remmem + (i * PAGESZ));
		}
		/* permute the arrays separately */
		for (i = 0; i < pages - 1; ++i) {
		    int j;
		    void *tmp;
		   
		    j = rand() % (pages - i);
		    tmp = loc_addr[i+j];
		    loc_addr[i+j] = loc_addr[i];
		    loc_addr[i] = tmp;
		   
		    j = rand() % (pages - i);
		    tmp = rem_addr[i+j];
		    rem_addr[i+j] = rem_addr[i];
		    rem_addr[i] = tmp;
		}
	}

	BARRIER();

	if (iamsender) {
		GASNETT_TRACE_SETSOURCELINE(__FILE__,__LINE__);
		begin = TIME();
		for (i = 0; i < pages; ++i) {
		    gasnet_put(peerproc, rem_addr[i], loc_addr[i], nbytes);
		}
		end = TIME();
		printf("Proc %3i - %5i bytes, seed %10u, %7i pages: %12i us total, %9.3f us ave. per page\n",
			myproc, nbytes, seed, pages, (int)(end-begin), ((double)(end-begin))/pages);
	}

	BARRIER();
}

int main(int argc, char **argv)
{
    /* call startup */
    GASNET_Safe(gasnet_init(&argc, &argv));
    GASNET_Safe(gasnet_attach(NULL, 0, TEST_SEGSZ, TEST_MINHEAPOFFSET));
    TEST_SEG(gasnet_mynode()); /* ensure we got the segment requested */

    /* parse arguments (we could do better) */
    if ((argc < 2) || (argc > 3)) {
	printf("Usage: %s nbytes (seed)\n", argv[0]);
	gasnet_exit(1);
    }
    nbytes = atoi(argv[1]);
    if (argc > 2) seed = atoi(argv[2]);
    if (!seed) seed = getpid();
    srand(seed);

    /* get SPMD info */
    myproc = gasnet_mynode();
    numprocs = gasnet_nodes();
    
    /* Only allow even number for numprocs */
    if (numprocs % 2 == 1) {
    	printf("Number of threads should be even number.\n");
    	gasnet_exit(1);
    }
    
    /* Setting peer thread rank */
    peerproc = (myproc % 2) ? (myproc - 1) : (myproc + 1);
    
    remmem = (void *) TEST_SEG(peerproc);
    locmem = (void *) TEST_MYSEG();

    do_test();

    gasnet_exit(0);

    return 0;

}


/* ------------------------------------------------------------------------------------ */

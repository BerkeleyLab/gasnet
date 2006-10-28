/*
 *  testcollperf.c
 *  gasnet_tree_coll
 *
 *  Created by Rajesh Nishtala on 10/25/06.
 *  Copyright 2006 Berkeley UPC. All rights reserved.
 *
 */

/* test collective performance */
/* for now assume both source and destination are in the segment*/
/* will relax this assumption later*/

#include "gasnet.h"
#include "gasnet_coll.h"

#if GASNET_PAR
#define DEFAULT_THREADS 2
#else
#define DEFAULT_THREADS 1
#endif
#define TEST_SEGSZ_EXPR (sizeof(int)*(datasize*iters*2))
#define WARM_ITERS MIN(4,iters)

#define COLL_BARRIER 1
#define NO_COLL_BARRIER 0
#define VERIFY_RESULT 1
int datasize;
int numprocs;
int iters = 0;
int images;	 /* numproc * threads */
int threads = DEFAULT_THREADS; /* per node */

#include "test.h"

#if GASNET_PAR
#define local_barrier()	PTHREAD_LOCALBARRIER(threads)
#define global_barrier()	PTHREAD_BARRIER(threads)
#else
#define local_barrier()	do {} while(0)
#define global_barrier()	BARRIER()
#endif

void run_bcast_test(int flags, int use_barrier, char *tree_type, int fanout) {
	int *A; /*source*/
	int *B; /*destination*/
	int *C; /*other*/
	int i,j;
	char flagstr[20];
	gasnett_tick_t begin, end;
	gasnet_node_t root, mynode;
	mynode = gasnet_mynode();
	assert(gasnet_getMaxLocalSegmentSize() > 2*datasize*iters*sizeof(int));
	/*allocate array to be datasize*iters ints out of the aligned segment*/
	A = (int*) TEST_MYSEG();
	B = (int*) A + datasize*iters;
	C = (int*) B + datasize*iters;
	gasnet_coll_set_tree_kind(tree_type);
	gasnet_coll_set_fanout(fanout);
	BARRIER();
	if(flags & (GASNET_COLL_IN_NOSYNC | GASNET_COLL_OUT_NOSYNC)) {
		sprintf(flagstr, "no/no");
	} else 	if(flags & (GASNET_COLL_IN_MYSYNC | GASNET_COLL_OUT_MYSYNC)) {
		sprintf(flagstr, "my/my");
	} else 	if(flags & (GASNET_COLL_IN_ALLSYNC | GASNET_COLL_OUT_ALLSYNC)) {
		sprintf(flagstr, "all/all");
	} else {
		MSG0("wtf\n");
	}
	
	/*	for(i=0; i< 3; i++) { */
	for(i=0; i< 1; i++) {
		if (i == 0) {
			root = 0;
		} else if (i == 1) {
			if (gasnet_nodes() < 3) continue;
			root = images / 2;
		} else {
			if (gasnet_nodes() < 2) continue;
			root = images - 1;
		}
		
		if(mynode == root) {
			/* fill in the source data with live values*/
			for(j=0; j<iters; j++) {
				for(i=0; i<datasize; i++) {
					A[j*datasize+i] = j*datasize+i;
					B[j*datasize+i] = -1;
				}
			}
		} else { 
			/* fill in source data with -1 */
			for(j=0; j<iters; j++) {
				for(i=0; i<datasize; i++) {
					B[j*datasize+i] = -1;
				}
			}
			
		}
		
		BARRIER();
		for(j=0; j<WARM_ITERS; j++) {
			gasnet_coll_broadcast(GASNET_TEAM_ALL, B+datasize*j, root, A+datasize*j, datasize*sizeof(int), flags | GASNET_COLL_SINGLE);			
		}
		
		BARRIER();		
		begin = gasnett_ticks_now();
		for(j=0; j<iters; j++) {
			gasnet_coll_broadcast(GASNET_TEAM_ALL, B+datasize*j, root, A+datasize*j, datasize*sizeof(int), flags | GASNET_COLL_SINGLE);			
		}
		
		BARRIER();
		end =  gasnett_ticks_now() - begin;
		
		/*verify that the data got there */
		#if VERIFY_RESULT
		for(j=0; j<iters; j++) {
			for(i=0; i<datasize; i++) {
				if(B[j*datasize+i] != j*datasize+i) {
					MSG("ERROR: broadcast validation failed (j=%d,i=%d) expected %d got %d", j,i,j*datasize+i, B[j*datasize+i]);              
					if(flags & (GASNET_COLL_IN_NOSYNC | GASNET_COLL_OUT_NOSYNC)) {
						MSG("running no/no\n");
					} else 	if(flags & (GASNET_COLL_IN_MYSYNC | GASNET_COLL_OUT_MYSYNC)) {
						MSG("running my/my\n");
					} else 	if(flags & (GASNET_COLL_IN_ALLSYNC | GASNET_COLL_OUT_ALLSYNC)) {
						MSG("running all/all\n");
					} else {
						MSG("wtf\n");
					}
					gasnet_exit(1);             
				} 
			}	
		}
		#endif	
	} /*end changing root*/
	MSG("bcast syncflags: %s tree_geom: (%s,%d) datasize: %d bytes time: %g microseconds", flagstr, tree_type, fanout, datasize*sizeof(int), (double)gasnett_ticks_to_us(end)/iters);
	BARRIER();
	
} /*end function*/

int main(int argc, char **argv) 
{
	
	gasnet_node_t myproc, i;
	int j;
	

	
	/*startup*/
	GASNET_Safe(gasnet_init(&argc, &argv));
	
#if GASNET_PAR
	MSG0("Test does not support par build yet\n");
	gasnet_exit(0);
	return 1; 
#endif
	
	switch(argc) {
		case 1: MSG0("usage: %s datasize (iters=1000)", argv[0]); gasnet_exit(1);
		case 2: /*just the size*/
			datasize = atoi(argv[1]); iters = 1000; break;
		case 3: /*size and iteration count*/
			datasize = atoi(argv[1]); iters = atoi(argv[2]); break; 
#if GASNET_PAR
		case 4: /*size, iteration count, and threads */
			datasize = atoi(argv[1]); iters = atoi(argv[2]); threads = atoi(argv[3]); break;
#endif
		default:  test_usage();
	}

	GASNET_Safe(gasnet_attach(NULL, 0, TEST_SEGSZ_REQUEST, TEST_MINHEAPOFFSET));
	gasnet_coll_init(NULL, 0, NULL, 0, 0);
	
	MSG0("Running coll test(s) with %d iterations and %d ints (%d bytes).", (int)iters, (int)datasize, (int)(datasize*sizeof(int)));
	
	test_init("testcollperf", 1, "size (iters) (threadcnt)");
	
	BARRIER();
	
	/*	run_bcast_test(GASNET_COLL_IN_NOSYNC | GASNET_COLL_OUT_NOSYNC, COLL_BARRIER);
		run_bcast_test(GASNET_COLL_IN_MYSYNC | GASNET_COLL_OUT_MYSYNC, COLL_BARRIER);
		run_bcast_test(GASNET_COLL_IN_ALLSYNC | GASNET_COLL_OUT_ALLSYNC, COLL_BARRIER); */
	run_bcast_test(GASNET_COLL_IN_NOSYNC | GASNET_COLL_OUT_NOSYNC, NO_COLL_BARRIER, "GASNET_BINOMIAL_TREE", 0);
	for(i=1; i<=gasnet_nodes(); i++) {
		run_bcast_test(GASNET_COLL_IN_NOSYNC | GASNET_COLL_OUT_NOSYNC, NO_COLL_BARRIER, "GASNET_NARY_TREE", i);
	}
	run_bcast_test(GASNET_COLL_IN_MYSYNC | GASNET_COLL_OUT_MYSYNC, NO_COLL_BARRIER, "GASNET_BINOMIAL_TREE", 0);
	for(i=1; i<=gasnet_nodes(); i++) {
		run_bcast_test(GASNET_COLL_IN_MYSYNC | GASNET_COLL_OUT_MYSYNC, NO_COLL_BARRIER, "GASNET_NARY_TREE", i);
	}
	run_bcast_test(GASNET_COLL_IN_ALLSYNC | GASNET_COLL_OUT_ALLSYNC, NO_COLL_BARRIER, "GASNET_BINOMIAL_TREE", 0);
	for(i=1; i<=gasnet_nodes(); i++) {
		run_bcast_test(GASNET_COLL_IN_ALLSYNC | GASNET_COLL_OUT_ALLSYNC, NO_COLL_BARRIER, "GASNET_NARY_TREE", i);
	}

	
	
	
	gasnet_exit(0);
	return 0;
}

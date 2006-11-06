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
/*max size in ints*/
#define MAX_SIZE 2048
#define DEFAULT_ITERS 1000
#define MAX_TREE_FANOUT 10
#ifndef MIN
#define MIN(a,b) ((a) < (b) ? (a) : (b))
#endif
#define TEST_SEGSZ_EXPR (sizeof(int)*(2048*iters*gasnet_nodes()*2))
#define WARM_ITERS MIN(4,iters)

#if GASNET_ALIGNED_SEGMENTS
#else
  #error "THIS TEST ASSUMES ALIGNED SEGMENTS!"
#endif

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

void run_exchange_test(int flags, int use_barrier, int dissem_radix) {
  int *A; /*source*/
  int *B; /*destination*/
  int *C; /*other*/
  int i,j,k,nodes;
  char flagstr[20];
  gasnett_tick_t begin, end;
  gasnet_node_t root, mynode;
  mynode = gasnet_mynode();
  nodes = gasnet_nodes();
  assert(gasnet_getMaxLocalSegmentSize() > 2*datasize*nodes*iters*sizeof(int));
	
  /*allocate array to be datasize*iters ints out of the aligned segment*/
  BARRIER();
  A = (int*) TEST_MYSEG();
  B = (int*) A + datasize*gasnet_nodes()*iters;
  C = (int*) B + datasize*gasnet_nodes()*iters;
  assert(dissem_radix == 2);
  if(flags & (GASNET_COLL_IN_NOSYNC | GASNET_COLL_OUT_NOSYNC)) {
    sprintf(flagstr, "no/no");
  } else 	if(flags & (GASNET_COLL_IN_MYSYNC | GASNET_COLL_OUT_MYSYNC)) {
    sprintf(flagstr, "my/my");
  } else 	if(flags & (GASNET_COLL_IN_ALLSYNC | GASNET_COLL_OUT_ALLSYNC)) {
    sprintf(flagstr, "all/all");
  } else {
    MSG0("wtf\n");
  }
  /* fill in the source data with live values*/
  for(j=0; j<iters; j++) {
    for(k=0; k<gasnet_nodes(); k++) {
      for(i=0; i<datasize; i++) {
	A[j*datasize*nodes+k*datasize+i] = j*datasize*nodes+mynode*datasize+(i+1)*10;
	B[j*datasize*nodes+k*datasize+i] = -1;
      }
    }
  }
	
  BARRIER();		
  begin = gasnett_ticks_now();
  for(j=0; j<iters; j++) {
    gasnet_coll_exchange(GASNET_TEAM_ALL, B+datasize*nodes*j, A+datasize*nodes*j, datasize*sizeof(int), flags | GASNET_COLL_SINGLE);
  }
	
  BARRIER();
  end =  gasnett_ticks_now() - begin;
	
  /*verify that the data got there */
#if VERIFY_RESULT
  for(j=0; j<iters; j++) {
    for(k=0; k<nodes; k++) {
      for(i=0; i<datasize; i++) {
	//	MSG("j: %d k: %d i: %d val: %d\n", j, k, i, B[j*datasize*nodes+k*datasize+i]); 

	if(B[j*datasize*nodes+k*datasize+i] != j*datasize*nodes+k*datasize+(i+1)*10) {
	  MSG("ERROR: exchange validation failed (j=%d,i=%d) expected %d got %d", j,i,j*datasize*nodes+k*datasize+i, B[j*datasize*nodes+k*datasize+i]);              
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
  }
#endif	
	
  MSG0("exchange syncflags: %s radix: (%d) datasize: %ld bytes time: %g microseconds", flagstr, dissem_radix, (datasize*sizeof(int)), (double)gasnett_ticks_to_us(end)/iters);
  BARRIER();
	
	
}

void run_bcast_test(int flags, int use_barrier, char *tree_type, int fanout) {
  int *A; /*source*/
  int *B; /*destination*/
  int *C; /*other*/
  int i,j;
  char flagstr[20];
  gasnett_tick_t begin, end, barrier_begin, barrier_end=0;
  gasnet_node_t root, mynode;
  mynode = gasnet_mynode();
  assert(gasnet_getMaxLocalSegmentSize() > 2*MAX_SIZE*iters*sizeof(int));
  /*allocate array to be datasize*iters ints out of the aligned segment*/
  A = (int*) TEST_MYSEG();
  B = (int*) A + datasize*iters;
  C = (int*) B + datasize*iters;
 
  gasnet_coll_set_tree_kind(tree_type);
  gasnet_coll_set_fanout(fanout);
  BARRIER();
  if((flags & (GASNET_COLL_IN_NOSYNC))  && (flags & (GASNET_COLL_OUT_NOSYNC))) {
    sprintf(flagstr, "no/no");
  } else if ((flags & GASNET_COLL_IN_NOSYNC)  && (flags & GASNET_COLL_OUT_MYSYNC)) {
    sprintf(flagstr, "no/my");
  } else if ((flags & GASNET_COLL_IN_NOSYNC)  && (flags & GASNET_COLL_OUT_ALLSYNC)) { 
    sprintf(flagstr, "no/all");
  } else if((flags & GASNET_COLL_IN_MYSYNC)  && (flags & GASNET_COLL_OUT_NOSYNC)) {
    sprintf(flagstr, "my/no");
  } else if ((flags & GASNET_COLL_IN_MYSYNC)  && (flags & GASNET_COLL_OUT_MYSYNC)) {
    sprintf(flagstr, "my/my");
  } else if ((flags & GASNET_COLL_IN_MYSYNC)  && (flags & GASNET_COLL_OUT_ALLSYNC)) { 
    sprintf(flagstr, "my/all");
  } else if((flags & GASNET_COLL_IN_ALLSYNC)  && (flags & GASNET_COLL_OUT_NOSYNC)) {
    sprintf(flagstr, "all/no");
  } else if ((flags & GASNET_COLL_IN_ALLSYNC)  && (flags & GASNET_COLL_OUT_MYSYNC)) {
    sprintf(flagstr, "all/my");
  } else if ((flags & GASNET_COLL_IN_ALLSYNC)  && (flags & GASNET_COLL_OUT_ALLSYNC)) { 
    sprintf(flagstr, "all/all");
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
      BARRIER();
    }
		
    BARRIER();		
    begin = gasnett_ticks_now();
    for(j=0; j<iters; j++) {
      gasnet_coll_broadcast(GASNET_TEAM_ALL, B+datasize*j, root, A+datasize*j, datasize*sizeof(int), flags | GASNET_COLL_SINGLE);			
      #if 0
      if(use_barrier){
	barrier_begin = gasnett_ticks_now();
	BARRIER();
	barrier_end += gasnett_ticks_now() - barrier_begin;
      }
      #endif
    }
    
    if(!use_barrier) {
      barrier_begin = gasnett_ticks_now();
      BARRIER();
      barrier_end += gasnett_ticks_now() - barrier_begin;
    }
    end =  gasnett_ticks_now() - begin;
    BARRIER();
    /*verify that the data got there */
#if VERIFY_RESULT
    for(j=0; j<iters; j++) {
      for(i=0; i<datasize; i++) {
	if(B[j*datasize+i] != j*datasize+i) {
	  MSG("ERROR %s: broadcast validation failed (%s,j=%d,i=%d) expected %d got %d", flagstr, tree_type, j,i,j*datasize+i, B[j*datasize+i]);              
	  gasnet_exit(1);             
	} 
      }	
    }
#endif	
  } /*end changing root*/

  if(use_barrier) {
    MSG("bcast-latency syncflags: %s tree_geom: (%s,%d) datasize: %ld bytes coll_time: %g us barrier_time: %g us", flagstr, tree_type, fanout, datasize*sizeof(int), (double)gasnett_ticks_to_us(end)/iters, (double)gasnett_ticks_to_us(barrier_end)/iters);
  } else {
    MSG("bcast-throughput syncflags: %s tree_geom: (%s,%d) datasize: %ld bytes coll_time: %g us barrier_time: %g us", flagstr, tree_type, fanout, datasize*sizeof(int), (double)gasnett_ticks_to_us(end)/iters, (double)gasnett_ticks_to_us(barrier_end));
  }
  BARRIER();
	
} /*end function*/

int main(int argc, char **argv) 
{
	
  gasnet_node_t myproc, i;
  int j;
  int tree_fanout=0;
  int run_all = 1;
	
  /*startup*/
  GASNET_Safe(gasnet_init(&argc, &argv));
	
#if GASNET_PAR
  MSG0("Test does not support par build yet\n");
  gasnet_exit(0);
  return 1; 
#endif
  
  
  switch(argc) {
  case 1: run_all=1; iters=DEFAULT_ITERS; break;
  case 2: /*just tree fanout*/
    tree_fanout = atoi(argv[1]); run_all=0; iters=DEFAULT_ITERS; break;
  case 3: /*fanout and iters*/
    iters = atoi(argv[2]); tree_fanout = atoi(argv[1]); run_all=0; break;
  default:  test_usage();
  }
	
  GASNET_Safe(gasnet_attach(NULL, 0, TEST_SEGSZ_REQUEST, TEST_MINHEAPOFFSET));
  gasnet_coll_init(NULL, 0, NULL, 0, 0);
	
  MSG0("Running coll test(s) with %d iterations and %d ints (%d bytes).", (int)iters, (int)datasize, (int)(datasize*sizeof(int)));
  
  test_init("testcollperf", 0, "(fanout) (iters) (threadcnt)");
  
  BARRIER();
  
  for(datasize=1; datasize<=MAX_SIZE; datasize = datasize*2) {
/*     run_exchange_test(GASNET_COLL_IN_NOSYNC | GASNET_COLL_OUT_NOSYNC, NO_COLL_BARRIER, 2); */
    
    if(!run_all) {
      if(tree_fanout == 0) {
	run_bcast_test(GASNET_COLL_IN_NOSYNC | GASNET_COLL_OUT_NOSYNC, NO_COLL_BARRIER,
		       (char*)"GASNET_BINOMIAL_TREE", 0);
	run_bcast_test(GASNET_COLL_IN_MYSYNC | GASNET_COLL_OUT_MYSYNC, NO_COLL_BARRIER, 
		       (char*)"GASNET_BINOMIAL_TREE", 0); 
	run_bcast_test(GASNET_COLL_IN_ALLSYNC | GASNET_COLL_OUT_MYSYNC, COLL_BARRIER, 
		       (char*)"GASNET_BINOMIAL_TREE", 0); 
      } else {
	run_bcast_test(GASNET_COLL_IN_NOSYNC | GASNET_COLL_OUT_NOSYNC, NO_COLL_BARRIER, 
		       (char*)"GASNET_NARY_TREE", tree_fanout); 
	run_bcast_test(GASNET_COLL_IN_MYSYNC | GASNET_COLL_OUT_MYSYNC, NO_COLL_BARRIER, 
		       (char*)"GASNET_NARY_TREE", tree_fanout); 
	run_bcast_test(GASNET_COLL_IN_ALLSYNC | GASNET_COLL_OUT_MYSYNC, COLL_BARRIER, 
		       (char*)"GASNET_NARY_TREE", tree_fanout); 
      }
    } else {
      run_bcast_test(GASNET_COLL_IN_NOSYNC | GASNET_COLL_OUT_NOSYNC, NO_COLL_BARRIER, 
		     (char*)"GASNET_BINOMIAL_TREE", 0); 
      run_bcast_test(GASNET_COLL_IN_MYSYNC | GASNET_COLL_OUT_MYSYNC, NO_COLL_BARRIER, 
		     (char*)"GASNET_BINOMIAL_TREE", 0); 
      run_bcast_test(GASNET_COLL_IN_ALLSYNC | GASNET_COLL_OUT_MYSYNC, COLL_BARRIER, 
		     (char*)"GASNET_BINOMIAL_TREE", 0); 
      for(tree_fanout=1; tree_fanout < MIN(gasnet_nodes(), MAX_TREE_FANOUT); tree_fanout++) {
	run_bcast_test(GASNET_COLL_IN_NOSYNC | GASNET_COLL_OUT_NOSYNC, NO_COLL_BARRIER, 
		       (char*)"GASNET_NARY_TREE", tree_fanout); 
	run_bcast_test(GASNET_COLL_IN_MYSYNC | GASNET_COLL_OUT_MYSYNC, NO_COLL_BARRIER, 
		       (char*)"GASNET_NARY_TREE", tree_fanout); 
	run_bcast_test(GASNET_COLL_IN_ALLSYNC | GASNET_COLL_OUT_MYSYNC, COLL_BARRIER, 
		       (char*)"GASNET_NARY_TREE", tree_fanout); 
      }
    }
    
  }
  
  
  MSG("tests finished successfully");
  gasnet_exit(0);
  
  
  return 0;
}

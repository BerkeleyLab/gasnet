/*
 *  testcollperf2.c
 *  gasnet_tree_coll
 *
 *  Created by Rajesh Nishtala on 10/1/07.
 *  Copyright 2007 Berkeley UPC Group. All rights reserved.
 *
 */

#include "gasnet.h"
#include "gasnet_coll.h"

#define VERIFICATION_MODE 1

#if VERIFICATION_MODE
#define VERIFY_RESULT 1
#define MAX_SIZE 256
#define DEFAULT_ITERS 4
#define ROOT_ITER_MAX 1
#else
#define VERIFY_RESULT 0
#define MAX_SIZE 2048
#define DEFAULT_ITERS 10000
#define ROOT_ITER_MAX 1
#endif

/*max_dsize is a variable set in main*/
#define TOTAL_THREADS threads_per_node*gasnet_nodes()

#if VERIFICATION_MODE
#define TEST_SEGSZ_EXPR (sizeof(int)*(MAX_SIZE*iters*TOTAL_THREADS*threads_per_node*2))
#define SEG_PER_THREAD (sizeof(int)*MAX_SIZE*iters*TOTAL_THREADS)
#else
#define TEST_SEGSZ_EXPR (sizeof(int)*(MAX_SIZE*TOTAL_THREADS*threads_per_node*2))
#define SEG_PER_THREAD (sizeof(int)*MAX_SIZE*TOTAL_THREADS)
#endif

gasnet_node_t mynode;
gasnet_node_t nodes;
int threads_per_node;
int THREADS;
int iters;
size_t max_data_size;

#include "test.h"

#define COLL_BARRIER() PTHREAD_BARRIER(threads_per_node)

typedef struct {
  int my_local_thread;
  int mythread;
  
  gasnet_coll_handle_t *hndl;
  char _pad[GASNETT_CACHE_LINE_BYTES];
  uint8_t *mysrc, *mydest;
  uint8_t *node_src, *node_dst;
} thread_data_t;

uint8_t **my_srcs;
uint8_t **my_dsts;
uint8_t **all_srcs;
uint8_t **all_dsts;

void run_broadcastM_test(thread_data_t *td, uint8_t **dst_arr, uint8_t **src_arr, size_t nelem, int root_thread, int flags) {
  int i;
  int *src, *dst;
  
  if(flags & GASNET_COLL_SINGLE) dst = (int*) dst_arr[td->mythread];
  else if(flags & GASNET_COLL_LOCAL) dst = (int*) dst_arr[td->my_local_thread];
  else { MSG("ERROR in INPUT FLAGS"); gasnet_exit(1);}
  
  src = (int*) src_arr[0];
  
  
  for(i=0; i<nelem; i++) {
    if(td->mythread == root_thread) src[i] = 42+i;
    td->mydest[i] = -1;
  }
//  COLL_BARRIER();
  gasnet_coll_broadcastM(GASNET_TEAM_ALL, (void* const*) dst_arr, root_thread, src, nelem*sizeof(int), flags); 
 // COLL_BARRIER();
 
  
  for(i=0; i<nelem; i++) {
    if(dst[i]!=42+i) {
      MSG("broadcast verification failure on thread %d .. expected %d got %d", td->mythread, 42+i, dst[i]);
    }  else {
      MSG("%d> %d %d %d", td->mythread, i, src[i], dst[i]);
    }
  }
}

void run_scatterM_test(thread_data_t *td, uint8_t **dst_arr, uint8_t **src_arr, size_t nelem, int root_thread, int flags) {
  int i;
  int *src, *dst;
  
#if 0
  if(flags & GASNET_COLL_SINGLE) dst = (int*) dst_arr[td->mythread];
  else if(flags & GASNET_COLL_LOCAL) dst = (int*) dst_arr[td->my_local_thread];
  else { MSG("ERROR in INPUT FLAGS"); gasnet_exit(1);}
#endif
  src = (int*) src_arr[0];
  dst = (int*) td->mydest;
  if(td->mythread == root_thread) {
    for(i=0; i<nelem*THREADS; i++) {
      src[i] = 42+i;
    }
  }
  
  for(i=0; i<nelem; i++) {
    dst[i] = -42;
  }
  
  COLL_BARRIER();
  gasnet_coll_scatterM(GASNET_TEAM_ALL, (void* const*) dst_arr, root_thread, src, nelem*sizeof(int), flags); 
  COLL_BARRIER();
  
  for(i=0; i<nelem; i++) {
    if(dst[i]!=42+i+td->mythread*nelem) {
      MSG("scatter verification failure on thread %d .. expected %d got %d", (int)td->mythread, (int)(42+i+td->mythread*nelem), (int)dst[i]);
    } else {
      MSG("%d> scatter: %d %d %d", td->mythread, i, src[i], dst[i]);
    } 
  }
}

void run_gatherM_test(thread_data_t *td, uint8_t **dst_arr, uint8_t **src_arr, size_t nelem, int root_thread, int flags) {
  int i;
  int *src, *dst;
  
  dst = (int*) dst_arr[0];
  src = (int*) td->mysrc;
  
  for(i=0; i<nelem; i++) {
    src[i] = 42+i+td->mythread*nelem;
  }
  
  if(td->mythread == root_thread) {
    for(i=0; i<nelem*THREADS; i++) {
      ((int*)td->mydest)[i] = -1;
    }
  }
  
  COLL_BARRIER();
  gasnet_coll_gatherM(GASNET_TEAM_ALL, root_thread, dst, (void* const*) src_arr, nelem*sizeof(int), flags); 
  COLL_BARRIER();
  if(td->mythread == root_thread) {
    for(i=0; i<THREADS*nelem; i++) {
      if(dst[i]!=42+i) {
        MSG("gather verification failure on elem %d .. expected %d got %d", i, 42+i, dst[i]);
      } else {
        MSG("%d> gather: %d %d %d", td->mythread, i, src[i], dst[i]);
      } 
    }
  }
  

}


void run_exchange_test(thread_data_t *td, int *dst, int *src, size_t nelem, int flags) {
  int i;
  for(i=0; i<nelem*THREADS*iters; i++) {
    src[i] = td->mythread*1000+42+i;
    MSG("src[%d] = %d", i, src[i]);
    dst[i] = -1;
  }
  if(flags & GASNET_COLL_IN_NOSYNC) COLL_BARRIER();
  for(i=0; i<iters; i++) {
    gasnet_coll_exchange(GASNET_TEAM_ALL, dst+i*nelem*THREADS, src+i*nelem*THREADS, nelem*sizeof(int), flags);
  }
  if(flags & GASNET_COLL_OUT_NOSYNC) COLL_BARRIER();  
  for(i=0; i<nelem*THREADS*iters; i++) {
    MSG("dst[%d] = %d", i, dst[i]);
  }
}

void run_exchangeM_test(thread_data_t *td, uint8_t **dst_lst, uint8_t **src_lst, size_t nelem, int flags) {
  int i;
//  MSG0("running exchangeM test");
  for(i=0; i<nelem*THREADS; i++) {
    ((int*) td->mysrc)[i] = td->mythread*1000+42+i;
    MSG("%d> src[%d] = %d", td->mythread, i, ((int*) td->mysrc)[i]);
    ((int*) td->mydest)[i] = -1;
  }
  
  gasnet_coll_exchangeM(GASNET_TEAM_ALL, (void* const*) dst_lst, (void* const*)src_lst, nelem*sizeof(int), flags);
  
  for(i=0; i<nelem*THREADS; i++) {
    MSG("%d> dst[%d] = %d", td->mythread, i, ((int*) td->mydest)[i]);
  }
}

void run_gather_all_test(thread_data_t *td, int *dst, int *src, size_t nelem, int flags) {
  int i;
  for(i=0; i<nelem; i++) {
    src[i] = td->mythread*1000+42+i;
    MSG("src[%d] = %d", i, src[i]);
    dst[i] = -1;
  }

  for(i=0; i<iters; i++) {
    gasnet_coll_gather_all(GASNET_TEAM_ALL, dst+i*THREADS, src+i, nelem*sizeof(int), flags);
  }
  for(i=0; i<nelem*THREADS*iters; i++) {
    MSG("dst[%d] = %d", i, dst[i]);
  }
}

void run_gather_allM_test(thread_data_t *td, uint8_t **dst_lst, uint8_t **src_lst, size_t nelem, int flags) {
  int i;
  for(i=0; i<nelem*THREADS; i++) {
    ((int*) td->mysrc)[i] = td->mythread*1000+42+i;
    MSG("%d> oringal src[%d] = %d", td->mythread, i, ((int*) td->mysrc)[i]);
  }
  
  gasnet_coll_scatterM(GASNET_TEAM_ALL, (void* const*) dst_lst, 0, src_lst[1], sizeof(int)*nelem, flags);
  
  for(i=0; i<nelem; i++) {
    MSG("%d> mydest[%d] = %d", td->mythread, i, ((int*) td->mydest)[i]);
    ((int*) td->mydest)[i] *= 100;
  }
  
  gasnet_coll_gather_allM(GASNET_TEAM_ALL, (void* const*) src_lst, (void* const*) dst_lst, nelem*sizeof(int), flags);
  
  for(i=0; i<nelem*THREADS; i++) {
    MSG("%d> final src[%d] = %d", td->mythread, i, ((int*) td->mysrc)[i]);
  }
  
}
void *thread_main(void *arg) {
  thread_data_t *td = (thread_data_t*) arg;
  int i;
#if GASNET_PAR
  gasnet_image_t *imagearray = test_malloc(nodes * sizeof(gasnet_image_t));
  for (i=0; i<nodes; ++i) { imagearray[i] = threads_per_node; }
  gasnet_coll_init(imagearray, td->mythread, NULL, 0, 0);
  test_free(imagearray);
#else
  gasnet_coll_init(NULL, 0, NULL, 0, 0);
#endif
  
  //COLL_BARRIER();
  //run_broadcastM_test(td, my_dsts, &all_srcs[0], max_data_size, 0, GASNET_COLL_LOCAL | GASNET_COLL_IN_MYSYNC | GASNET_COLL_OUT_MYSYNC);
  //COLL_BARRIER();
  //run_scatterM_test(td, all_dsts, &all_srcs[0], max_data_size, 0, GASNET_COLL_SINGLE | GASNET_COLL_IN_MYSYNC | GASNET_COLL_OUT_MYSYNC);
  COLL_BARRIER();
  run_gatherM_test(td, &all_dsts[0], all_srcs, max_data_size, 0, GASNET_COLL_LOCAL | GASNET_COLL_IN_MYSYNC | GASNET_COLL_OUT_MYSYNC);
  COLL_BARRIER();
  //run_exchangeM_test(td, all_dsts, all_srcs, max_data_size, GASNET_COLL_SINGLE | GASNET_COLL_IN_MYSYNC | GASNET_COLL_OUT_MYSYNC);
  //COLL_BARRIER();
  //run_exchange_test(td, td->mydest, td->mysrc, max_data_size, GASNET_COLL_LOCAL | GASNET_COLL_IN_NOSYNC | GASNET_COLL_OUT_NOSYNC);
  //COLL_BARRIER();
  //run_gather_all_test(td, td->mydest, td->mysrc, max_data_size, GASNET_COLL_LOCAL | GASNET_COLL_IN_NOSYNC | GASNET_COLL_OUT_NOSYNC);
  //COLL_BARRIER();
  //run_gather_allM_test(td, all_dsts, all_srcs, max_data_size, GASNET_COLL_SINGLE | GASNET_COLL_IN_MYSYNC | GASNET_COLL_OUT_MYSYNC);
  //COLL_BARRIER();
  
  return NULL;
}

int main(int argc, char **argv)
{
  static uint8_t *A, *B;
  int i,j;
  thread_data_t *td_arr;

  GASNET_Safe(gasnet_init(&argc, &argv));
  
  if (argc > 1) {
    iters = atoi(argv[1]);
  }
  if (iters < 1) {
    iters = 1000;
  }
  
  if(argc > 2) {
    max_data_size = atoi(argv[2]); 
  } else {
    max_data_size = 4;
  }
  
#if GASNET_PAR
  if (argc > 3) {
    threads_per_node = atoi(argv[3]);
  } else {
    threads_per_node = 1; 
  }
  if (threads_per_node > TEST_MAXTHREADS || threads_per_node < 1) {
    printf("ERROR: Threads must be between 1 and %d\n", TEST_MAXTHREADS);
    exit(EXIT_FAILURE);
  }
  if (threads_per_node > gasnett_cpu_count()) {
    MSG0("WARNING: thread count (%i) exceeds physical cpu count (%i) - enabling  \"polite\", low-performance synchronization algorithms",
         threads_per_node, gasnett_cpu_count());
    gasnet_set_waitmode(GASNET_WAIT_BLOCK);
  }
#else
  threads_per_node = 1;
#endif  
  
  /* get SPMD info */
  mynode = gasnet_mynode();
  nodes = gasnet_nodes();
  THREADS = nodes * threads_per_node;
  GASNET_Safe(gasnet_attach(NULL, 0, TEST_SEGSZ_REQUEST, TEST_MINHEAPOFFSET));
  test_init("testcollperf2",0,"(iters) (max data size) (thread count)");
  A = TEST_MYSEG();
  B = A+(SEG_PER_THREAD*threads_per_node);
  my_srcs =  (uint8_t**) test_malloc(sizeof(uint8_t*)*threads_per_node);
  my_dsts =  (uint8_t**) test_malloc(sizeof(uint8_t*)*threads_per_node);
  all_srcs = (uint8_t**) test_malloc(sizeof(uint8_t*)*THREADS);
  all_dsts = (uint8_t**) test_malloc(sizeof(uint8_t*)*THREADS);
  td_arr = (thread_data_t*) test_malloc(sizeof(thread_data_t)*threads_per_node);
  
  for(i=0; i<threads_per_node; i++) {
    my_srcs[i] = A + i*SEG_PER_THREAD;
    my_dsts[i] = B + i*SEG_PER_THREAD;
    td_arr[i].my_local_thread = i;
    td_arr[i].mythread = mynode*threads_per_node+i;
    td_arr[i].mysrc = my_srcs[i];
    td_arr[i].mydest = my_dsts[i];
  }
  for(i=0; i<nodes; i++) {
/*    assert_always(TEST_SEG(i).size >= SEG_PER_THREAD*threads_per_node); */
    for(j=0; j<threads_per_node; j++) {
      all_srcs[i*threads_per_node+j] = (uint8_t*) TEST_SEG(i) + j*SEG_PER_THREAD;
      all_dsts[i*threads_per_node+j] = (uint8_t*) TEST_SEG(i) + SEG_PER_THREAD*threads_per_node + j*SEG_PER_THREAD;
    }
  }
  MSG("threads_per_node=%d max_data_size=%d iters=%d", threads_per_node, (int)max_data_size, iters);

#if GASNET_PAR
  test_createandjoin_pthreads(threads_per_node, &thread_main, td_arr, sizeof(thread_data_t));
#else
  thread_main(&td_arr[0]);
#endif
  
  test_free(td_arr);
  BARRIER();
  MSG("done.");
  gasnet_exit(0);
  return 0;
}
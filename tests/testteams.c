/*
 *  testteams.c
 *  autotune_xcode
 *
 *  Created by Rajesh Nishtala on 6/23/09.
 *  Copyright 2009 __MyCompanyName__. All rights reserved.
 *
 */


#include <stdio.h>
#include <stdlib.h>

#include <gasnet.h>
#include <gasnet_tools.h>
#include <gasnet_coll.h>
typedef struct {
  int my_local_thread;
  int mythread;
  gasnet_coll_handle_t *hndl;
  uint8_t *mysrc, *mydest;
  uint8_t *node_src, *node_dst;
  char _pad[GASNETT_CACHE_LINE_BYTES];
  
} thread_data_t;

/* max data size for the test in bytes*/
#define DEFAULT_MAX_DATA_SIZE 32768 

#define PRINT_TIMERS 1
#define VERBOSE_VERIFICATION_OUTPUT 0

/*max_dsize is a variable set in main*/
#define TOTAL_THREADS threads_per_node*gasnet_nodes()

#if 1
#define ERROR_EXIT() gasnet_exit(1)
#else
#define ERROR_EXIT() do {} while(0)
#endif

gasnet_node_t mynode;
gasnet_node_t nodes;
gasnet_image_t threads_per_node;
gasnet_image_t THREADS;
#define TOTAL_THREADS threads_per_node*gasnet_nodes()
size_t max_data_size;
#define SEG_PER_THREAD (sizeof(int)*max_data_size)
#define TEST_SEGSZ_EXPR (2*SEG_PER_THREAD*threads_per_node)

#include "test.h"

#define COLL_BARRIER() PTHREAD_BARRIER(threads_per_node)
uint8_t **my_srcs;
uint8_t **my_dsts;
uint8_t **all_srcs;
uint8_t **all_dsts;

#if GASNET_PAR
#error test teams doesnt support GASNET_PAR 
#endif
#define CURRENT_ROOT 0
#define DATA_LEN 2048
void *thread_main(void *arg) {
  thread_data_t *td = (thread_data_t*) arg;

  int *src;
  int *dst;
  int i;
#if GASNET_ALIGNED_SEGMENTS
#warning compiling w/ alinged segments
  int single_local_flag = GASNET_COLL_SINGLE;
  src = (int*) all_srcs[CURRENT_ROOT+(mynode%2)];
  dst = (int*) all_dsts[td->mythread];
#else
  int single_local_flag = GASNET_COLL_LOCAL;
  src = (td->mythread == CURRENT_ROOT ? (int*) td->mysrc : NULL);
  dst = (int*) td->mydest;
#endif

  gasnet_coll_init(NULL, td->mythread, NULL, 0, 0);
  COLL_BARRIER();
  /*Basic tests for TEAM ALL*/
  for(i=0; i<DATA_LEN; i++) {
    src[i] = td->mythread*10000 + i;
  }
  
  COLL_BARRIER();
  i=0;
  if(mynode%2 == 0) {
    gasnet_coll_broadcast(GASNET_TEAM_EVEN, dst, 0, src, (DATA_LEN/2)*sizeof(int), GASNET_COLL_IN_NOSYNC | GASNET_COLL_OUT_NOSYNC | single_local_flag);
    gasnet_coll_broadcast(GASNET_TEAM_EVEN, dst+DATA_LEN/2, 0, src+DATA_LEN/2, (DATA_LEN/2)*sizeof(int), GASNET_COLL_IN_NOSYNC | GASNET_COLL_OUT_NOSYNC | single_local_flag);
    for(i=0; i<100000; i++) {
      gasnet_coll_barrier_notify(GASNET_TEAM_EVEN, i, 0);
      gasnet_coll_barrier_wait(GASNET_TEAM_EVEN, i, 0);
    }
  } else {
    gasnet_coll_broadcast(GASNET_TEAM_ODD, dst, 0, src, DATA_LEN*sizeof(int), GASNET_COLL_IN_NOSYNC | GASNET_COLL_OUT_ALLSYNC | single_local_flag);
  }
  {
    gasnett_tick_t start,total;
    start = gasnett_ticks_now();
    COLL_BARRIER();
    total = gasnett_ticks_now();
    printf("%d> time in last barrier %g\n", gasneti_mynode, (double) gasnett_ticks_to_us(total-start));
  }
  printf("%d> i: %d\n", td->mythread, i); 
  for(i=0; i<DATA_LEN; i++) {
    if(dst[i] != (CURRENT_ROOT+(mynode%2))*10000 +i) {
      fprintf(stderr, "%d ERROR expected: %d got %d\n", td->mythread,  (CURRENT_ROOT+(mynode%2))*10000 +i, dst[i]);
    }
  }   
  fprintf(stderr, "%d> all verification done!\n", td->mythread);
  return NULL;
}
  
int main(int argc, char **argv) {
  int i,j;
  static uint8_t *A, *B;
  thread_data_t *td_arr;

  GASNET_Safe(gasnet_init(&argc, &argv));

#if GASNET_PAR
  threads_per_node = gasnett_cpu_count();
#else
  threads_per_node = 1;
#endif
  max_data_size = DEFAULT_MAX_DATA_SIZE/sizeof(int);

  mynode = gasnet_mynode();
  nodes = gasnet_nodes();
  THREADS = nodes * threads_per_node;
  
  if (threads_per_node > gasnett_cpu_count()) {
    MSG0("WARNING: thread count (%i) exceeds physical cpu count (%i) - enabling  \"polite\", low-performance synchronization algorithms",
         (int) threads_per_node, gasnett_cpu_count());
    gasnet_set_waitmode(GASNET_WAIT_BLOCK);
  }
  
  GASNET_Safe(gasnet_attach(NULL, 0, TEST_SEGSZ_REQUEST, TEST_MINHEAPOFFSET));
  test_init("testteams",0,"");
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
  
  thread_main(&td_arr[0]);
  COLL_BARRIER();
  gasnet_exit(0);
  return 0;
}

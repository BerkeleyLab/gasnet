/* $Source: /Users/kamil/work/gasnet-cvs2/gasnet/tests/Attic/testteambcast2.c,v $
 * $Date: 2011/04/18 23:37:44 $
 * $Revision: 1.1.2.1 $
 *
 * Description: GASNet team collectives test. Each thread participates
 * in two teams: row team and column team.
 * 
 * Copyright 2010, E. O. Lawrence Berekely National Laboratory                                                   
 * Terms of use are as specified in license.txt           
 */

#include <gasnet.h>
#include <gasnet_tools.h>
#include <gasnet_coll.h>
#include <gasnet_coll_team.h>

#define SCRATCH_SIZE (4*1024*1024)
#define COLL_BUFF_SIZE (8*1024*1024)
#define SEG_PER_THREAD (SCRATCH_SIZE*3+COLL_BUFF_SIZE*2)
#define TEST_SEGSZ_EXPR (SEG_PER_THREAD*threads)

#define MAX_SIZE (1*1024*1024)

#if GASNET_PAR
#define DEFAULT_THREADS 2
#else
#define DEFAULT_THREADS 1
#endif

#if GASNET_PAR
  #define local_barrier()	PTHREAD_LOCALBARRIER(threads)
  #define global_barrier()	PTHREAD_BARRIER(threads)
#else
  #define local_barrier()	do {} while(0)
  #define global_barrier()	BARRIER()
#endif

#include <math.h> 

typedef struct {
  int local_id;
  int mythread;
  int mynode;
  int *mysrc;
  int *mydst;
  gasnet_seginfo_t teamA_scratch;
  gasnet_seginfo_t teamB_scratch;
  gasnet_seginfo_t teamC_scratch;
} thread_data_t;


/* global data */
int iters = 0;
int images;	 /* nodes * threads */

gasnet_node_t mynode, nodes;
gasnet_image_t nrows, ncols;
gasnet_image_t threads, total_images;

uint8_t *A, *B;

uint8_t **my_srcs;
uint8_t **my_dsts;
uint8_t **all_srcs;
uint8_t **all_dsts;

/* end of global data */

#include <test.h>

void *thread_main(void *arg) 
{
  thread_data_t *td = arg;
  int i;
  gasnet_team_handle_t my_row_team, my_col_team, my_shuffle_team;
  gasnet_image_t myimage = (gasnet_image_t)td->mythread;
  gasnet_image_t my_row, my_col, my_shuffle_rank;
  int64_t start, total;
  size_t sz;

#if GASNET_PAR
  gasnet_image_t *imagearray = test_malloc(nodes * sizeof(gasnet_image_t));
  for (i=0; i<nodes; ++i) { imagearray[i] = threads; }
  gasnet_coll_init(imagearray, td->mythread, NULL, 0, 0);
  test_free(imagearray);
#else
  gasnet_coll_init(NULL, 0, NULL, 0, 0);
#endif

  my_row = myimage / ncols;
  my_col = myimage % ncols;

  my_shuffle_rank = (myimage+1)%total_images;
  MSG("Mythread %u, my row %u, my col %u, my shuffled rank %u, total images %u",
      myimage, my_row, my_col, my_shuffle_rank, total_images);


  global_barrier();

  MSG("Creating a shuffled team.");
  my_shuffle_team = gasnet_coll_team_split(GASNET_TEAM_ALL,
                                           0, // same color for all threads
                                           my_shuffle_rank,
                                           &td->teamC_scratch);

  global_barrier();


  MSG("Creating row teams from the shuffled team.");
  my_row_team = gasnet_coll_team_split(my_shuffle_team,
                                       my_row,
                                       my_col,
                                       &td->teamA_scratch);

  global_barrier();

  MSG("Creating column teams from the shuffled team.");
  my_col_team = gasnet_coll_team_split(my_shuffle_team,
                                       my_col,
                                       my_row,
                                       &td->teamB_scratch);

  global_barrier();

  /* if (my_col == 0) { */
  /*   printf("row team %u: Running team barrier test with row teams...\n", */
  /*          (int)my_row); */
  /*   fflush(stdout); */
  /* } */

  /* start = TIME(); */
  /* for (i=0; i < iters; i++) { */
  /*   gasnete_coll_teambarrier(my_row_team); */
  /* } */
  /* total = TIME() - start; */

  /* if (my_col == 0) { */
  /*   printf("row team %u: total time: %8.3f sec, avg row team Barrier latency: %8.3f us\n", */
  /*          (int)my_row, ((float)total)/1000000, ((float)total)/iters); */
  /*   fflush(stdout); */
  /* } */

  /* global_barrier(); */

  /* if (my_row == 0) { */
  /*   printf("col team %u: Running team barrier test with column teams...\n", */
  /*          (int)my_col); */
  /*   fflush(stdout); */
  /* } */

  /* start = TIME(); */
  /* for (i=0; i < iters; i++) { */
  /*   gasnete_coll_teambarrier(my_col_team); */
  /* } */
  /* total = TIME() - start; */
  
  /* if (my_row == 0) { */
  /*   printf("col team %u: total time: %8.3f sec  Avg column team Barrier latency: %8.3f us\n", */
  /*          (int)my_col, ((float)total)/1000000, ((float)total)/iters); */
  /*   fflush(stdout); */
  /* } */

  

  MSG("%d> start shuffled team broadcast tests...\n", gasnete_coll_team_my_image(GASNET_TEAM_ALL));
  for (sz = 1; sz<=MAX_SIZE; sz=sz*2) {
    int root = 0;
    if (sz >= 1024*1024 && iters >= 1000) {
      iters = iters/10;
    }
    for(i=0; i<sz; i++) {
      td->mysrc[i] =  gasnete_coll_team_my_image(my_shuffle_team)*sz+42+i;
      td->mydst[i] = -1;
    }
    global_barrier();
    gasnet_coll_broadcast(my_shuffle_team, td->mydst, root, td->mysrc, sz*sizeof(int),
                          GASNET_COLL_IN_MYSYNC|GASNET_COLL_OUT_MYSYNC|GASNET_COLL_LOCAL);

    global_barrier();
    for(i=0; i<sz; i++) {
      int expected = root*sz+42+i;
      if(expected != td->mydst[i]) {
        fprintf(stderr, "[%d] %d %d (expecting %d)\n", 
                gasnete_coll_team_my_image(my_shuffle_team), i, td->mydst[i], expected);
        gasnet_exit(1);
      }
    }
    global_barrier();
    /*time this*/
    start = TIME();
    for(i=0; i<iters; i++) {
      gasnet_coll_broadcast(my_shuffle_team, td->mydst, root, td->mysrc, sz*sizeof(int),
                            GASNET_COLL_IN_MYSYNC|GASNET_COLL_OUT_MYSYNC|GASNET_COLL_LOCAL);
    }
    total = TIME() - start;
    
    if(mynode == 0){
      printf("%u> %lu byte shuffled team broadcast time: %8.3f usec\n",
             gasnete_coll_team_my_image(my_shuffle_team), sz*sizeof(int), ((double)total)/(iters));
      fflush(stdout);
    }
  }
  global_barrier();
  MSG("%d> start row team broadcast tests...\n", gasnete_coll_team_my_image(GASNET_TEAM_ALL));

  /*next do row broadcasts*/
  for (sz = 1; sz<=MAX_SIZE; sz=sz*2) {
    //MSG("Here1\n");
    if (sz >= 1024*1024 && iters >= 1000) {
      iters = iters/10;
    }

    //MSG("Here2\n");
 
    for(i=0; i<sz; i++) {
      // MSG("i=%d td->mysrc %p, td->mydst %p\n", i, td->mysrc, td->mydst);
      td->mysrc[i] = my_row*sz+42+i;
      td->mydst[i] = -1;
    }
    global_barrier();
    //MSG("Here3\n");
    gasnet_coll_broadcast(my_row_team, td->mydst, 0, td->mysrc, sz*sizeof(int), 
                          GASNET_COLL_IN_MYSYNC|GASNET_COLL_OUT_MYSYNC|GASNET_COLL_LOCAL);
    //MSG("Here 4\n");
    global_barrier();
    //MSG("Here 5\n");
    for(i=0; i<sz; i++) {
      int expected = my_row*sz+42+i;
      if(expected != td->mydst[i]) {
        fprintf(stderr, "%u> %d %d (expecting %d)\n", 
                myimage, i, td->mydst[i], expected);
        gasnet_exit(1);
      }
    }
    global_barrier();
    /*time this*/
    start = TIME();
    for(i=0; i<iters; i++) {
      gasnet_coll_broadcast(my_row_team, td->mydst, 0, td->mysrc, sz*sizeof(int), 
                            GASNET_COLL_IN_MYSYNC|GASNET_COLL_OUT_MYSYNC|GASNET_COLL_LOCAL);
    }
    total = TIME() - start;
    
    if(my_col == 0){
      printf("%d> %lu byte broadcast row team %u time: %8.3f usec\n",
             gasnete_coll_team_my_image(GASNET_TEAM_ALL), sz*sizeof(int), my_row,  ((double)total)/(iters));
      fflush(stdout);
    }
  }
  global_barrier();

  MSG("%d> start column team broadcast tests...\n", gasnete_coll_team_my_image(GASNET_TEAM_ALL));

  /*next do col broadcasts*/
  for (sz = 1; sz<=MAX_SIZE; sz=sz*2) {
    if (sz >= 1024*1024 && iters >= 1000) {
      iters = iters/10;
    }

    for(i=0; i<sz; i++) {
      td->mysrc[i] = my_col*sz+42+i;
      td->mydst[i] = -1;
    }
    global_barrier();
    gasnet_coll_broadcast(my_col_team, td->mydst, 0, td->mysrc, sz*sizeof(int), 
                          GASNET_COLL_IN_MYSYNC|GASNET_COLL_OUT_MYSYNC|GASNET_COLL_LOCAL);
    global_barrier();
    for(i=0; i<sz; i++) {
      int expected = my_col*sz+42+i;
      if(expected != td->mydst[i]) {
        fprintf(stderr, "%d> %d %d (expecting %d)\n", myimage, i, td->mydst[i], expected);
        fflush(stderr);
        gasnet_exit(1);
      }
    }
    global_barrier();

    /*time this*/
    start = TIME();
    for(i=0; i<iters; i++) {
      gasnet_coll_broadcast(my_col_team, td->mydst, 0, td->mysrc, sz*sizeof(int), 
                            GASNET_COLL_IN_MYSYNC|GASNET_COLL_OUT_MYSYNC|GASNET_COLL_LOCAL);
    }
    total = TIME() - start;
    
    if(my_row == 0){
      printf("%d> %lu byte broadcast col team %u time: %8.3f usec\n",
               gasnete_coll_team_my_image(GASNET_TEAM_ALL), sz*sizeof(int), my_col, ((double)total)/(iters));
      fflush(stdout);
    }
  }
  
  /* MSG("Thread %u completes.\n", myimage); */
  
  global_barrier();

  return NULL;
}


int main(int argc, char **argv) 
{
  int i, j;
  thread_data_t* td_arr;

  GASNET_Safe(gasnet_init(&argc, &argv));

  mynode = gasnet_mynode();
  nodes = gasnet_nodes();
  
  if (argc > 5)
    test_usage();

  threads = 0;
  if (argc > 4) {
    threads = atoi(argv[4]);
  }
  if (!threads) {
    threads = DEFAULT_THREADS;
  }

  total_images = nodes * threads;

  iters = 0;
  if (argc > 1) {
    iters = atoi(argv[1]);
  }
  if (!iters) {
    iters = 1000;
  }

  if (argc > 2) {
    nrows = atoi(argv[2]);
  }
  if (!nrows) {
    /* search for as near to square as possible */
    nrows = sqrt(total_images);
    while (total_images % nrows) --nrows;
  }
  if (argc > 3) {
    ncols = atoi(argv[3]);
  } else {
    ncols = total_images / nrows;
  }
  assert_always(nrows*ncols == total_images);
  
  GASNET_Safe(gasnet_attach(NULL, 0, TEST_SEGSZ_REQUEST, TEST_MINHEAPOFFSET));
  A = TEST_MYSEG();
  test_init("testteambcast", 1, "(iters) (nrows) (ncols) (threads)");


  TEST_SET_WAITMODE(threads);
                    
  MSG0("Running team collectives test with a %u-by-%u grid (%u nodes %u threads per node) and %i iterations...\n",
       (int)nrows, (int)ncols, (int)nodes, (int)threads, iters);
 
  my_srcs =  (uint8_t**)test_malloc(sizeof(uint8_t*)*threads);
  my_dsts =  (uint8_t**)test_malloc(sizeof(uint8_t*)*threads);
  all_srcs = (uint8_t**)test_malloc(sizeof(uint8_t*)*total_images);
  all_dsts = (uint8_t**)test_malloc(sizeof(uint8_t*)*total_images);
  td_arr = (thread_data_t*) test_malloc(sizeof(thread_data_t)*threads);
  
  for(i=0; i<threads; i++) {
    my_srcs[i] = A + i*SEG_PER_THREAD + 3*SCRATCH_SIZE;
    my_dsts[i] = my_srcs[i] + COLL_BUFF_SIZE;
    td_arr[i].local_id = i;
    td_arr[i].mythread = mynode*threads+i;
    td_arr[i].mysrc = (int *)my_srcs[i];
    td_arr[i].mydst = (int *)my_dsts[i];
    td_arr[i].teamA_scratch.addr = A + (i*SEG_PER_THREAD); 
    td_arr[i].teamA_scratch.size = SCRATCH_SIZE;
    td_arr[i].teamB_scratch.addr = (uint8_t *)td_arr[i].teamA_scratch.addr + SCRATCH_SIZE;
    td_arr[i].teamB_scratch.size = SCRATCH_SIZE;
    td_arr[i].teamC_scratch.addr = (uint8_t *)td_arr[i].teamB_scratch.addr + SCRATCH_SIZE;
    td_arr[i].teamC_scratch.size = SCRATCH_SIZE;

  }
  for(i=0; i<nodes; i++) {
    gasneti_assert(TEST_SEGINFO()[i].size >= SEG_PER_THREAD*threads); 
    for(j=0; j<threads; j++) {
      all_srcs[i*threads+j] = (uint8_t*) TEST_SEG(i) + j*SEG_PER_THREAD + 3*SCRATCH_SIZE;
      all_dsts[i*threads+j] = all_srcs[i*threads+j] + COLL_BUFF_SIZE;
    }
  }
  
#if GASNET_PAR
  test_createandjoin_pthreads(threads, &thread_main, td_arr, sizeof(thread_data_t));
#else
  thread_main(&td_arr[0]);
#endif
  
  test_free(td_arr);
  test_free(all_dsts);
  test_free(all_srcs);
  test_free(my_dsts);
  test_free(my_srcs);

  BARRIER();
  MSG("done.");
  
  gasnet_exit(0);
  return 0;                
}

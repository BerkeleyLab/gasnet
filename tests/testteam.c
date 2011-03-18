/* $Source: /Users/kamil/work/gasnet-cvs2/gasnet/tests/testteam.c,v $
 * $Date: 2011/03/18 23:05:03 $
 * $Revision: 1.2.8.4 $
 *
 * Description: GASNet team split and barrier test. Each thread
 * participates in two teams: row team and column team.
 *
 * Copyright 2010, E. O. Lawrence Berekely National Laboratory                                                     * Terms of use are as specified in license.txt           
 */

#include <gasnet.h>
#include <gasnet_coll.h>
#include <gasnet_coll_team.h>

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


#define SEG_PER_THREAD (2*1024*1024)
#define TEST_SEGSZ_EXPR (SEG_PER_THREAD)

#include <math.h> /* for sqrt() */
#include <test.h>

typedef struct {
  int local_id;
  int mythread;
  int mynode;
  char _pad[GASNETT_CACHE_LINE_BYTES];
} thread_data_t;

/* global data */
int iters = 0;
int images;	 /* nodes * threads */

gasnet_node_t mynode, nodes;
gasnet_image_t nrows, ncols;
gasnet_image_t threads, total_images;

gasnet_seginfo_t teamA_scratch;
gasnet_seginfo_t teamB_scratch;

uint8_t *A, *B;
/* end of global data */

void *thread_main(void *arg) 
{
  thread_data_t *td = arg;
  int i;
  gasnet_team_handle_t my_row_team, my_col_team;
  gasnet_image_t myimage = (gasnet_image_t)td->mythread;
  gasnet_image_t my_row, my_col;
  int64_t start, total;

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
                 
  MSG("Mythread %u, my row %u, my col %u, total images %u",
      myimage, my_row, my_col, total_images);

  global_barrier();

  MSG("Creating row teams.");
  my_row_team = gasnet_coll_team_split(GASNET_TEAM_ALL,
                                       my_row,
                                       my_col,
                                       &teamA_scratch);

  global_barrier();

  MSG("Creating column teams.");
  my_col_team = gasnet_coll_team_split(GASNET_TEAM_ALL,
                                       my_col,
                                       my_row,
                                       &teamB_scratch);

  global_barrier();

  if (my_col == 0) {
    printf("row team %u: Running team barrier test with row teams...\n",
           (int)my_row);
    fflush(stdout);
  }

  start = TIME();
  for (i=0; i < iters; i++) {
    gasnete_coll_teambarrier(my_row_team);
  }
  total = TIME() - start;

  if (my_col == 0) {
    printf("row team %u: total time: %8.3f sec, avg row team Barrier latency: %8.3f us\n",
           (int)my_row, ((float)total)/1000000, ((float)total)/iters);
    fflush(stdout);
  }

  global_barrier();

  if (my_row == 0) {
    printf("col team %u: Running team barrier test with column teams...\n",
           (int)my_col);
    fflush(stdout);
  }

  start = TIME();
  for (i=0; i < iters; i++) {
    gasnete_coll_teambarrier(my_col_team);
  }
  total = TIME() - start;
  
  if (my_row == 0) {
    printf("col team %u: total time: %8.3f sec  Avg column team Barrier latency: %8.3f us\n",
           (int)my_col, ((float)total)/1000000, ((float)total)/iters);
    fflush(stdout);
  }

  /* MSG("Thread %u completes.\n", myimage); */

  global_barrier();

  return NULL;
}


int main(int argc, char **argv) 
{
  int i;

  gasnet_seginfo_t const * test_segs;
  GASNET_Safe(gasnet_init(&argc, &argv));

  GASNET_Safe(gasnet_attach(NULL, 0, TEST_SEGSZ_REQUEST, TEST_MINHEAPOFFSET));
  
  A = TEST_MYSEG();
  
  test_init("test_team", 1, "(iters) (nrows) (ncols) (threads)");

  mynode = gasnet_mynode();
  nodes = gasnet_nodes();
  test_segs = TEST_SEGINFO();
  
  teamA_scratch.addr = test_segs[mynode].addr;
  teamA_scratch.size = test_segs[mynode].size/2;
  
  teamB_scratch.addr = (uint8_t*)teamA_scratch.addr + teamA_scratch.size;
  teamB_scratch.size = teamA_scratch.size;

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
    iters = 10000;
  }

  if (argc > 2) {
    nrows = atoi(argv[2]);
  } else {
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

  MSG0("Running team test with a %u-by-%u grid (%u nodes %u threads per node) and %i iterations...\n",
       (int)nrows, (int)ncols, (int)nodes, (int)threads, iters);

#if GASNET_PAR
  MSG("Forking %d gasnet threads", threads);
  {
    int i;
    thread_data_t* tt_thread_data = test_malloc(threads*sizeof(thread_data_t));
    for (i = 0; i < threads; i++) {
	    tt_thread_data[i].mynode = mynode;
	    tt_thread_data[i].local_id = i;
	    tt_thread_data[i].mythread = i + threads * mynode;
    }
    test_createandjoin_pthreads(threads, &thread_main, tt_thread_data, sizeof(tt_thread_data[0]));
    test_free(tt_thread_data);
  }
#else
  { 
    thread_data_t td;
    td.mynode = mynode;
    td.local_id = 0;
    td.mythread = mynode;

    thread_main(&td);
  }
#endif

  MSG("done.");

  gasnet_exit(0); /* for faster exit */
  return 0;
}

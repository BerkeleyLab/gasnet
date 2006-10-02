/*   $Source: /Users/kamil/work/gasnet-cvs2/gasnet/tests/testlockcontend.c,v $
 *     $Date: 2006/10/02 23:29:47 $
 * $Revision: 1.1 $
 * Description: GASNet lock performance test
 *   Measures the overhead associated with contended locks
 * Copyright 2002, Dan Bonachea <bonachea@cs.berkeley.edu>
 * Terms of use are as specified in license.txt
 */

#include <test.h>

#ifndef GASNET_PAR
#error This test can only be built for GASNet PAR configuration
#endif

int mynode = 0;
int iters=0;
void *myseg = NULL;
int accuracy = 0;
int maxthreads = 2;

void report(int threads, int64_t totaltime, int iters) {
      char format[80];
      sprintf(format, "%c:  %%6i     %%%i.%if s  %%%i.%if us\n", 
              TEST_SECTION_NAME(), (4+accuracy), accuracy, (4+accuracy), accuracy);
      printf(format, threads, totaltime/1.0E9, (totaltime/1000.0)/iters);
      fflush(stdout);
}

/* placed in a function to avoid excessive inlining */
gasnett_tick_t ticktime() { return gasnett_ticks_now(); }
uint64_t tickcvt(gasnett_tick_t ticks) { return gasnett_ticks_to_ns(ticks); }

void* thread_fn(void*);

/* ------------------------------------------------------------------------------------ */
/* This tester measures the performance of contended HSLs and pthread mutexes.
 */
int main(int argc, char **argv) {
  

  GASNET_Safe(gasnet_init(&argc, &argv));
  GASNET_Safe(gasnet_attach(NULL, 0, TEST_SEGSZ_REQUEST, TEST_MINHEAPOFFSET));
  test_init("testlockcontend",1,"(maxthreads) (iters) (accuracy) (test sections)");

  if (argc > 1) maxthreads = atoi(argv[1]);
  if (maxthreads > TEST_MAXTHREADS || maxthreads < 1) {
    printf("Threads must be between 1 and %i\n", TEST_MAXTHREADS);
    gasnet_exit(-1);
  }

  if (argc > 2) iters = atoi(argv[2]);
  if (!iters) iters = 1000000;

  if (argc > 3) accuracy = atoi(argv[3]);
  if (!accuracy) accuracy = 3;

  if (argc > 4) TEST_SECTION_PARSE(argv[4]);

  if (argc > 5) test_usage();

  mynode = gasnet_mynode();
  myseg = TEST_MYSEG();

  if (mynode == 0) {
    printf("Running locks performance test with 1..%i threads and %i iterations...\n",maxthreads,iters);
    fflush(stdout);
    MSG0("Spawning pthreads...");
    test_createandjoin_pthreads(maxthreads, &thread_fn, NULL, 0);
  }

  BARRIER();
  MSG("done.");

  gasnet_exit(0);
  return 0;
}

#define TIME_OPERATION(id, desc, op, altop)                     \
do {                                                            \
  if (!id) TEST_SECTION_BEGIN();                                \
  PTHREAD_LOCALBARRIER(maxthreads);                             \
  if (TEST_SECTION_ENABLED())                                   \
  { int i, _thr, _iters = iters, _warmupiters = MAX(1,iters/10);\
    gasnett_tick_t start,end;  /* use ticks interface */        \
    if (!id) {                                                  \
      printf("\n---- %s ----\n"                                 \
             "   Threads    Total time    Avg. time\n"          \
             "   -------    ----------    ---------\n", desc);  \
    }                                                           \
    for (i=0; i < _warmupiters; i++) { op; } /* warm-up */      \
    for (_thr = 1; _thr <= maxthreads; ++_thr) {                \
      PTHREAD_LOCALBARRIER(maxthreads);                         \
      start = ticktime();                                       \
      if (id < _thr) for (i=0; i < _iters; i++) { op; }         \
      else { altop; }                                           \
      PTHREAD_LOCALBARRIER(maxthreads);                         \
      end = ticktime();                                         \
      if (!id)                                                  \
        report(_thr, tickcvt(end - start), iters);              \
    }                                                           \
  }                                                             \
} while (0)

#undef MSG0
#undef ERR
#define MSG0 THREAD_MSG0(id)
#define ERR  THREAD_ERR(id)

pthread_mutex_t mutex = PTHREAD_MUTEX_INITIALIZER;
gasnet_hsl_t hsl = GASNET_HSL_INITIALIZER;

void * thread_fn(void *arg) { GASNET_BEGIN_FUNCTION();
  int id = (int)(uintptr_t)arg;
 
  TIME_OPERATION(id, "lock/unlock contended pthread mutex (others in thread barrier)",
		  { pthread_mutex_lock(&mutex); pthread_mutex_unlock(&mutex); }, {});

  TIME_OPERATION(id, "lock/unlock contended HSL (others in thread barrier)",
		  { gasnet_hsl_lock(&hsl); gasnet_hsl_unlock(&hsl); }, {});

  return NULL;
}
/* ------------------------------------------------------------------------------------ */


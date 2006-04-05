/*   $Source: /Users/kamil/work/gasnet-cvs2/gasnet/tests/testtools.c,v $
 *     $Date: 2006/04/05 23:08:14 $
 * $Revision: 1.44.4.2 $
 * Description: helpers for GASNet tests
 * Copyright 2002, Dan Bonachea <bonachea@cs.berkeley.edu>
 * Terms of use are as specified in license.txt
 */

#define TEST_GASNET_TOOLS_ONLY
#define GASNETT_MAIN
#include "test.h"

#include <ctype.h>

/* specifically omit gasnet.h/test.h to test independence */
#if defined(_GASNET_H) || defined(TEST_GASNET_H)
#error testtools should *not* include gasnet.h
#endif

#ifdef HAVE_PTHREAD_H
  #ifndef MAX_NUM_THREADS
    #define MAX_NUM_THREADS 255
  #endif
  int NUM_THREADS = 0;
  gasnett_atomic_t thread_flag[MAX_NUM_THREADS];
  int valX[MAX_NUM_THREADS];
  int valY[MAX_NUM_THREADS];
  gasnett_atomic_t atomicX[MAX_NUM_THREADS];
#endif

#define DEFAULT_THREADS 10
#define DEFAULT_ITERS 100
int iters = 0;
#define TEST_HEADER_PREFIX() ((void)0)
#define TEST_HEADER(desc)             \
  TEST_HEADER_PREFIX();               \
  if (TEST_SECTION_BEGIN_ENABLED() && \
      (MSG0("%c: %s",TEST_SECTION_NAME(),desc),1))

void * thread_fn(void *arg);

/* test gasnet tools modifier convenience macros */
GASNETT_INLINE(test_dummy)
void test_dummy(void * GASNETT_RESTRICT p) {}

void test_dummy2(void) GASNETT_NORETURN;
GASNETT_NORETURNP(test_dummy2)
void test_dummy2(void) { abort(); }

GASNETT_BEGIN_EXTERNC
void *test_dummy3(void) GASNETT_MALLOC;
void *test_dummy3(void) { return malloc(1); }
GASNETT_INLINE(test_dummy4) GASNETT_MALLOC
void *test_dummy4(void) { return malloc(1); }
GASNETT_END_EXTERNC

GASNETT_EXTERNC
void test_dummy5(void) { }

int main(int argc, char **argv) {

  test_init("testtools", 0,"(iters) (num_threads) (tests_to_run)");

  if (argc > 1) iters = atoi(argv[1]);
  if (iters < 1) iters = DEFAULT_ITERS;
  #ifdef HAVE_PTHREAD_H
    if (argc > 2) NUM_THREADS = atoi(argv[2]);
    if (NUM_THREADS < 1) NUM_THREADS = DEFAULT_THREADS;
    if (NUM_THREADS > MAX_NUM_THREADS) NUM_THREADS = MAX_NUM_THREADS;
  #else
    if (argc > 2 && atoi(argv[2]) != 1) { ERR("no pthreads - only one thread available."); test_usage(); }
  #endif
  if (argc > 3) TEST_SECTION_PARSE(argv[3]);
  if (argc > 4) test_usage();

  TEST_GENERICS_WARNING();
  #ifdef HAVE_PTHREAD_H
    MSG("Running testtools with %i iterations and %i threads", iters, NUM_THREADS);
  #else
    MSG("Running testtools with %i iterations", iters);
  #endif

  #if defined(GASNETT_PAGESIZE) && defined(GASNETT_PAGESHIFT)
    if (0x1 << GASNETT_PAGESHIFT != GASNETT_PAGESIZE)
      ERR("bad pagesizes: GASNETT_PAGESHIFT=%i GASNETT_PAGESIZE=%i",
              GASNETT_PAGESHIFT, GASNETT_PAGESIZE);
    else 
      MSG("System page size is 2^%i == %i", GASNETT_PAGESHIFT, GASNETT_PAGESIZE);
  #endif
   
  MSG("Cache line size estimated to be: %i", GASNETT_CACHE_LINE_BYTES);
  if ((GASNETT_CACHE_LINE_BYTES & (GASNETT_CACHE_LINE_BYTES-1)) != 0)
        ERR("GASNETT_CACHE_LINE_BYTES not a power of two!");

  gasnett_sched_yield();
  TEST_TRACING_MACROS();

  TEST_HEADER("Testing high-performance timers...")
  { /* high performance timers */
    int i;
    gasnett_tick_t start, end;
    int64_t startref, endref;
    gasnett_tick_t ticktimemin = GASNETT_TICK_MIN;
    gasnett_tick_t ticktimemax = GASNETT_TICK_MAX;

    if (!(ticktimemin < ticktimemax)) ERR("!(min < max)");
    if (!(gasnett_ticks_now() > ticktimemin)) ERR("!(now > min)");
    if (!(gasnett_ticks_now() < ticktimemax)) ERR("!(now < max)");

    for (i=0; i < 3; i++) {
      int time, timeref;
      start = gasnett_ticks_now();
      startref = TIME();
        sleep(1);
      end = gasnett_ticks_now();
      endref = TIME();

      time = gasnett_ticks_to_us(end) - gasnett_ticks_to_us(start);
      timeref = endref - startref;

      if (abs(timeref - time) > 100000)
        ERR("timer and reference differ by more than 0.1sec:\n"
               "\ttime=%i  timeref=%i\n",time,timeref);

      if (abs( (gasnett_ticks_to_us(end) - gasnett_ticks_to_us(start)) - 
               gasnett_ticks_to_us(end - start) ) > 1)
        ERR("ticks_to_us(A) - ticks_to_us(B) != ticks_to_us(A-B)");

      if (abs( gasnett_ticks_to_ns(end - start)/1000 - 
               gasnett_ticks_to_us(end - start) ) > 1)
        ERR("ticks_to_ns(A)/1000 != ticks_to_us(A)");

    }
    {
      double overhead = gasnett_timer_overheadus();
      double granularity = gasnett_timer_granularityus();
      if (granularity <= 0.0 || overhead <= 0.0 ||
          (granularity+0.1) < 0.5*overhead) 
          /* allow some leeway for noise at granularities approaching cycle speed */
          /*granularity < 0.5*overhead)*/
          ERR("nonsensical timer overhead/granularity measurements:\n"
               "  overhead: %.3fus  granularity: %.3fus\n",overhead, granularity);
    }
  }

  TEST_HEADER("Testing local membar...")
  { /* local membar */
    int i;
    for (i=0;i<iters;i++) {
      gasnett_local_mb();
    }
  }

  TEST_HEADER("Testing local write membar...")
  { /* local membar */
    int i;
    for (i=0;i<iters;i++) {
      gasnett_local_wmb();
    }
  }

  TEST_HEADER("Testing local read membar...")
  { /* local membar */
    int i;
    for (i=0;i<iters;i++) {
      gasnett_local_rmb();
    }
  }

  TEST_HEADER("Testing atomic ops (sequential)...")
  { /* we can't really test atomicity without spinning threads, 
       but we can at least test simple operations  */
    int i = 0;
    gasnett_atomic_t var = gasnett_atomic_init(10);

    if (gasnett_atomic_read(&var,0) != 10)
      ERR("gasnett_atomic_init/gasnett_atomic_read got wrong value");

    gasnett_atomic_set(&var, 2*iters, 0);
    if (gasnett_atomic_read(&var,0) != 2*iters)
      ERR("gasnett_atomic_set/gasnett_atomic_read got wrong value");

    for (i=1;i<=iters;i++) {
      gasnett_atomic_increment(&var,0);
      if (gasnett_atomic_read(&var,0) != 2*iters + i)
        ERR("gasnett_atomic_increment got wrong value");
    }

    for (i=iters-1;i>=0;i--) {
      gasnett_atomic_decrement(&var,0);
      if (gasnett_atomic_read(&var,0) != 2*iters + i)
        ERR("gasnett_atomic_decrement got wrong value");
    }

    for (i=1;i<=iters;i++) {
      gasnett_atomic_set(&var, i, 0);
      gasnett_atomic_increment(&var,0);
      if (gasnett_atomic_read(&var,0) != i+1)
        ERR("gasnett_atomic_set/gasnett_atomic_increment got wrong value");
    }

    for (i=1;i<=iters;i++) {
      gasnett_atomic_set(&var, i, 0);
      gasnett_atomic_decrement(&var,0);
      if (gasnett_atomic_read(&var,0) != i-1)
        ERR("gasnett_atomic_set/gasnett_atomic_decrement got wrong value");
    }

    gasnett_atomic_set(&var, iters, 0);
    for (i=iters-1;i>=1;i--) {
      if (gasnett_atomic_decrement_and_test(&var,0))
        ERR("gasnett_atomic_decrement_and_test got wrong value");
      if (gasnett_atomic_read(&var,0) != i)
        ERR("gasnett_atomic_decrement_and_test set wrong value");
    }
    if (!gasnett_atomic_decrement_and_test(&var,0))
      ERR("gasnett_atomic_decrement_and_test got wrong value at zero");
    if (gasnett_atomic_read(&var,0) != 0)
      ERR("gasnett_atomic_decrement_and_test set wrong value at zero");

    #if defined(GASNETT_HAVE_ATOMIC_CAS)
      gasnett_atomic_set(&var, 0, 0);
      for (i=0;i<=iters;i++) {
	if (gasnett_atomic_compare_and_swap(&var, i-1, i-2, 0))
          ERR("gasnett_atomic_compare_and_swap succeeded at i=%i when it should have failed", i);
	if (gasnett_atomic_compare_and_swap(&var, i+1, i-2, 0))
          ERR("gasnett_atomic_compare_and_swap succeeded at i=%i when it should have failed", i);
        if (gasnett_atomic_read(&var,0) != i)
          ERR("gasnett_atomic_compare_and_swap altered value when it should not have at i=%i", i);
	if (!gasnett_atomic_compare_and_swap(&var, i, i+1, 0))
          ERR("gasnett_atomic_compare_and_swap failed at i=%i when it should have succeeded", i);
        if (gasnett_atomic_read(&var,0) != i+1)
          ERR("gasnett_atomic_compare_and_swap set wrong updated value at i=%i", i);
      }
    #endif

    #if defined(GASNETT_HAVE_ATOMIC_ADD_SUB)
      gasnett_atomic_set(&var, 1, 0);
      for (i=1;i<=iters;i++) {
        if ((gasnett_atomic_add(&var, i, 0) != 2*i) ||
            (gasnett_atomic_read(&var,0) != 2*i))
          ERR("gasnett_atomic_add got wrong value");
        if ((gasnett_atomic_subtract(&var, i-1, 0) != i+1) ||
            (gasnett_atomic_read(&var,0) != i+1))
          ERR("gasnett_atomic_subtract got wrong value");
      }
    #endif
  }

#ifdef HAVE_PTHREAD_H
  MSG("Spawning pthreads...");
  { 
    int i;
    for(i=0;i<NUM_THREADS;i++) gasnett_atomic_set(thread_flag+i,1,0);
    gasnett_local_mb();
    test_createandjoin_pthreads(NUM_THREADS, &thread_fn, NULL, 0);
  }
#endif

  MSG("Done.");
  return (test_errs > 0 ? 1 : 0);
}

#ifdef HAVE_PTHREAD_H

#undef MSG0
#undef ERR
#define MSG0 THREAD_MSG0(id)
#define ERR  THREAD_ERR(id)

gasnett_atomic_t up = gasnett_atomic_init(0);
gasnett_atomic_t down = gasnett_atomic_init(0);
gasnett_atomic_t x1 = gasnett_atomic_init(10000);
gasnett_atomic_t x2 = gasnett_atomic_init(10000);
gasnett_atomic_t x3 = gasnett_atomic_init(10000);
gasnett_atomic_t x4 = gasnett_atomic_init(10000);
gasnett_atomic_t x5 = gasnett_atomic_init(10000);

gasnett_atomic_t _thread_barrier = gasnett_atomic_init(0);

#define THREAD_BARRIER() do {                                               \
   barcnt++;                                                                \
   gasnett_atomic_increment(&_thread_barrier, GASNETT_ATOMIC_REL);          \
   while (gasnett_atomic_read(&_thread_barrier,0) < (barcnt*NUM_THREADS)) { \
      gasnett_sched_yield();                                                \
   }                                                                        \
   gasnett_local_rmb(); /* Acquire */                                       \
  } while(0)                                                                \

#undef TEST_HEADER_PREFIX
#define TEST_HEADER_PREFIX() THREAD_BARRIER()

void * thread_fn(void *arg) {
  int id = (int)(uintptr_t)arg;
  int i;
  int iters2=100*iters;
  int barcnt = 0;
  char th_test_section = test_section;
  #define test_section th_test_section
 
  /* sanity check - ensure unique threadids */
  if (!gasnett_atomic_decrement_and_test(thread_flag+id,0)) {
      ERR("thread %i failed sanity check", id);
  }

  /* sanity check - ensure thread barriers are working */
  TEST_HEADER("parallel atomic-op barrier test...") {  
    for (i=0;i<iters;i++) {
      int tmp;
      /* simple count-up barrier */
      gasnett_atomic_increment(&up,0);
      while (gasnett_atomic_read(&up,0) < NUM_THREADS) gasnett_sched_yield(); 

      gasnett_atomic_set(&down, 2*NUM_THREADS, 0);

      gasnett_atomic_increment(&up,0);
      while (gasnett_atomic_read(&up,0) < 2*NUM_THREADS) gasnett_sched_yield(); 

      tmp = gasnett_atomic_read(&up,0);
      if (tmp != 2*NUM_THREADS)
        ERR("count-up post-barrier read: %i != %i", tmp, 2*NUM_THREADS);

      /* simple count-down barrier */
      gasnett_atomic_decrement(&down,0);
      while (gasnett_atomic_read(&down,0) > NUM_THREADS) gasnett_sched_yield(); 

      gasnett_atomic_set(&up, 0, 0);

      gasnett_atomic_decrement(&down,0);
      while (gasnett_atomic_read(&down,0) > 0) gasnett_sched_yield(); 

      tmp = gasnett_atomic_read(&down,0);
      if (tmp != 0)
        ERR("count-down post-barrier read: %i != 0", tmp);
    }
  }

  TEST_HEADER("parallel atomic-op pounding test...") {
    int val;
    gasnett_atomic_set(&x1, 5, 0);
    gasnett_atomic_set(&x2, 5+iters2*NUM_THREADS, 0);
    gasnett_atomic_set(&x3, 5, 0);
    gasnett_atomic_set(&x4, 5+iters2*NUM_THREADS, 0);

    THREAD_BARRIER();

    for (i=0;i<iters2;i++) {
      gasnett_atomic_increment(&x1,0);
      gasnett_atomic_decrement(&x2,0);
    }
    #if defined(GASNETT_HAVE_ATOMIC_ADD_SUB)
      for (i=0;i<iters2;i++) {
	val = (i & 1) << 1; /* Alternate 0 and 2. (iters2=100*iters is always even) */
        gasnett_atomic_add(&x3,val,0);
        gasnett_atomic_subtract(&x4,val,0);
      }
    #endif

    THREAD_BARRIER();

    val = gasnett_atomic_read(&x1,0);
    if (val != 5+iters2*NUM_THREADS)
      ERR("pounding inc test mismatch: %i != %i",val,5+iters2*NUM_THREADS);

    val = gasnett_atomic_read(&x2,0);
    if (val != 5)
      ERR("pounding dec test mismatch: %i != 5",val);

  #if defined(GASNETT_HAVE_ATOMIC_ADD_SUB)
      val = gasnett_atomic_read(&x3,0);
      if (val != 5+iters2*NUM_THREADS)
        ERR("pounding add test mismatch: %i != %i",val,5+iters2*NUM_THREADS);
  
      val = gasnett_atomic_read(&x4,0);
      if (val != 5)
        ERR("pounding subtract test mismatch: %i != 5",val);
  #endif

  }

  TEST_HEADER("parallel dec-test pounding test...") {

    gasnett_atomic_set(&x3, NUM_THREADS, 0);
    gasnett_atomic_set(&x4, 0, 0);
    gasnett_atomic_set(&x5, 0, 0); /* count of "wins" */

    THREAD_BARRIER();

    for (i=0;i<iters;i++) {
      if (gasnett_atomic_decrement_and_test(&x3,0)) { /* I won */
        gasnett_atomic_increment(&x5,0); /* tally win */
        if (gasnett_atomic_read(&x3,0) != 0) ERR("pounding dec-test mismatch x3");
        if (gasnett_atomic_read(&x4,0) != 0) ERR("pounding dec-test mismatch x4");
        gasnett_atomic_set(&x4, NUM_THREADS, GASNETT_ATOMIC_REL); /* go */
      } else {
        while (gasnett_atomic_read(&x4,0) == 0) gasnett_sched_yield(); /* I lost - wait */
      }

      if (gasnett_atomic_decrement_and_test(&x4,0)) { /* I won */
        gasnett_atomic_increment(&x5,0); /* tally win */
        if (gasnett_atomic_read(&x3,0) != 0) ERR(" pounding dec-test mismatch x3");
        if (gasnett_atomic_read(&x4,0) != 0) ERR("pounding dec-test mismatch x4");
        gasnett_atomic_set(&x3, NUM_THREADS,  GASNETT_ATOMIC_REL); /* go */
      } else {
        while (gasnett_atomic_read(&x3,0) == 0) gasnett_sched_yield(); /* I lost - wait */
      }
    }

    if (gasnett_atomic_read(&x5, GASNETT_ATOMIC_RMB_PRE) != 2*iters)
      ERR("pounding dec-test mismatch");
  }

  TEST_HEADER("parallel word-tearing test...") {

    gasnett_atomic_set(&x3, 0, 0);
    gasnett_atomic_set(&x4, 0, 0);
    gasnett_atomic_set(&x5, 0, 0); 

    THREAD_BARRIER();

    if (NUM_THREADS <= 100) {  /* need 2*NUM_THREADS + 1 < 255 to prevent byte overflow */
      uint32_t x = id + 1;
      uint32_t myval = (x << 24) | (x << 16) | (x << 8) | x;
      for (i=0;i<iters2;i++) {
        uint32_t v;
        gasnett_atomic_set(&x3, myval, 0);
        gasnett_atomic_set(&x4, myval, 0);
        gasnett_atomic_set(&x5, myval, 0);
        gasnett_atomic_increment(&x4,0);
        gasnett_atomic_decrement(&x5,0);
        v = gasnett_atomic_read(&x3,0);
        if (((v >> 24) & 0xFF) != (v & 0xFF) ||
            ((v >> 16) & 0xFF) != (v & 0xFF) ||
            ((v >>  8) & 0xFF) != (v & 0xFF)) 
            ERR("observed word tearing on gasnett_atomic_set");
        v = gasnett_atomic_read(&x4,0); 
        /* bottom byte may have increased by up to NUM_THREADS, but high bytes must be same */
        if (((v >> 24) & 0xFF) != ((v >>  8) & 0xFF) ||
            ((v >> 16) & 0xFF) != ((v >>  8) & 0xFF)) 
            ERR("observed word tearing on gasnett_atomic_set/gasnett_atomic_increment");
        v = gasnett_atomic_read(&x5,0); 
        v += NUM_THREADS;
        /* bottom byte may have decreased by by  to NUM_THREADS, but high bytes must be same */
        if (((v >> 24) & 0xFF) != ((v >>  8) & 0xFF) ||
            ((v >> 16) & 0xFF) != ((v >>  8) & 0xFF)) 
            ERR("observed word tearing on gasnett_atomic_set/gasnett_atomic_decrement");
      }
    }
  }

  TEST_HEADER("parallel membar test...") {
    valX[id] = 0;
    valY[id] = 0;

    THREAD_BARRIER();

    { int partner = (id + 1) % NUM_THREADS;
      int lx, ly;
      for (i=0;i<iters2;i++) {
        valX[id] = i;
        gasnett_local_wmb();
        valY[id] = i;

        ly = valY[partner];
        gasnett_local_rmb();
        lx = valX[partner];
        if (lx < ly) ERR("mismatch in gasnett_local_wmb/gasnett_local_rmb test: lx=%i ly=%i", lx, ly);
      }
    }
  }

  #if defined(GASNETT_HAVE_ATOMIC_CAS)
    TEST_HEADER("parallel compare-and-swap test...") {
      static gasnett_atomic_t counter2 = gasnett_atomic_init(0);
      uint32_t goal = (NUM_THREADS * iters);
      uint32_t woncnt = 0;
      uint32_t oldval;
      while (woncnt < iters &&
             (oldval = gasnett_atomic_read(&counter2,0)) != goal) {
        if (gasnett_atomic_compare_and_swap(&counter2, oldval, (oldval + 1), 0)) {
           woncnt++;
        }
      }
      THREAD_BARRIER();
      oldval = gasnett_atomic_read(&counter2,0);
      if (oldval != goal) 
        ERR("failed compare-and-swap test: counter=%i expecting=%i", (int)oldval, (int)goal);
      if (woncnt != iters) 
        ERR("failed compare-and-swap test: woncnt=%i iters=%i", (int)woncnt, (int)iters);
    }
  #endif

  TEST_HEADER("parallel atomic-op fence test...") {
    int partner = (id + 1) % NUM_THREADS;
    int lx, ly;

    gasnett_atomic_set(&atomicX[id], 0, 0);
    valY[id] = 0;

    THREAD_BARRIER();

    for (i=0;i<iters;i++) {
      gasnett_atomic_set(&atomicX[id], 6*i, GASNETT_ATOMIC_WMB_POST);
      valY[id] = 5*i;
      ly = valY[partner];
      lx = gasnett_atomic_read(&atomicX[partner], GASNETT_ATOMIC_RMB_PRE);
      if (lx < ly) ERR("pounding fenced set/read mismatch: lx=%i ly=%i", lx, ly);

      gasnett_atomic_increment(&atomicX[id], GASNETT_ATOMIC_WMB_POST);
      ++valY[id];
      ly = valY[partner];
      lx = gasnett_atomic_read(&atomicX[partner], GASNETT_ATOMIC_RMB_PRE);
      if (lx < ly) ERR("pounding fenced dec/read mismatch: lx=%i ly=%i", lx, ly);

      #if defined(GASNETT_HAVE_ATOMIC_COMPARE_AND_SWAP)
      {
	uint32_t oldval;
	do {
	  oldval = gasnett_atomic_read(&atomicX[id], 0);
	} while (!gasnett_atomic_compare_and_swap(&atomicX[id], oldval, oldval + 1, GASNETT_ATOMIC_WMB_POST));
        valY[id]++;
        ly = valY[partner];
        lx = gasnett_atomic_read(&atomicX[partner], GASNETT_ATOMIC_RMB_PRE);
        if (lx < ly) ERR("pounding fenced c-a-s/read mismatch: lx=%i ly=%i", lx, ly);
      }
      #endif

      #if defined(GASNETT_HAVE_ATOMIC_ADD_SUB)
      {
        int step = i & 4;
        gasnett_atomic_add(&atomicX[id], step, GASNETT_ATOMIC_WMB_POST);
        valY[id] += step;
        ly = valY[partner];
        lx = gasnett_atomic_read(&atomicX[partner], GASNETT_ATOMIC_RMB_PRE);
        if (lx < ly) ERR("pounding fenced add/read mismatch: lx=%i ly=%i", lx, ly);
      }
      #endif
    }

    THREAD_BARRIER();

    for (i=iters-1;i>=0;i--) {
      #if defined(GASNETT_HAVE_ATOMIC_ADD_SUB)
      {
        int step = i & 4;
        valY[id] -= step;
        gasnett_atomic_subtract(&atomicX[id], step, GASNETT_ATOMIC_REL);
        lx = gasnett_atomic_read(&atomicX[partner], GASNETT_ATOMIC_ACQ);
        ly = valY[partner];
        if (lx < ly) ERR("pounding fenced sub/read mismatch: lx=%i ly=%i", lx, ly);
      }
      #endif

      #if defined(GASNETT_HAVE_ATOMIC_COMPARE_AND_SWAP)
      {
	uint32_t oldval;
        valY[id]--;
	do {
	  oldval = gasnett_atomic_read(&atomicX[id], 0);
	} while (!gasnett_atomic_compare_and_swap(&atomicX[id], oldval, oldval - 1, GASNETT_ATOMIC_REL));
        lx = gasnett_atomic_read(&atomicX[partner], GASNETT_ATOMIC_ACQ);
        ly = valY[partner];
        if (lx < ly) ERR("pounding fenced c-a-s/read mismatch: lx=%i ly=%i", lx, ly);
      }
      #endif

      --valY[id];
      gasnett_atomic_decrement(&atomicX[id], GASNETT_ATOMIC_REL);
      lx = gasnett_atomic_read(&atomicX[partner], GASNETT_ATOMIC_ACQ);
      ly = valY[partner];
      if (lx < ly) ERR("pounding fenced dec/read mismatch: lx=%i ly=%i", lx, ly);

      valY[id] = 6*i;
      gasnett_atomic_set(&atomicX[id], 6*i, GASNETT_ATOMIC_REL);
      lx = gasnett_atomic_read(&atomicX[partner], GASNETT_ATOMIC_ACQ);
      ly = valY[partner];
      if (lx < ly) ERR("pounding fenced set/read mismatch: lx=%i ly=%i", lx, ly);
    }
  }

  THREAD_BARRIER();

  return NULL;
}

#endif

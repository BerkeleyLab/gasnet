/*   $Source: /Users/kamil/work/gasnet-cvs2/gasnet/tests/testtools.c,v $
 *     $Date: 2005/08/15 06:30:57 $
 * $Revision: 1.29.4.1 $
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
#endif

#define DEFAULT_THREADS 10
#define DEFAULT_ITERS 100
int iters = 0;
char tests[255];
char curtest = 'A';
#define TEST_HEADER(desc)                        \
  curtest++;                                     \
  if ((!tests[0] || strchr(tests, curtest-1)) && \
      (MSG0("%c: %s",curtest-1,desc),1))

void * thread_fn(void *arg);

int main(int argc, char **argv) {

  if (argc > 1) iters = atoi(argv[1]);
  if (iters < 1) iters = DEFAULT_ITERS;
  #ifdef HAVE_PTHREAD_H
    if (argc > 2) NUM_THREADS = atoi(argv[2]);
    if (NUM_THREADS < 1) NUM_THREADS = DEFAULT_THREADS;
  #endif
  if (argc > 3) {
    const char *p = argv[3];
    char *q = tests;
    while (*p) *(q++) = toupper(*(p++));
  }

  test_init("testtools", 0);
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

    if (gasnett_atomic_read(&var) != 10)
      ERR("gasnett_atomic_init/gasnett_atomic_read got wrong value");

    gasnett_atomic_set(&var, 2*iters);
    if (gasnett_atomic_read(&var) != 2*iters)
      ERR("gasnett_atomic_set/gasnett_atomic_read got wrong value");

    for (i=1;i<=iters;i++) {
      gasnett_atomic_increment(&var);
      if (gasnett_atomic_read(&var) != 2*iters + i)
        ERR("gasnett_atomic_increment got wrong value");
    }

    for (i=iters-1;i>=0;i--) {
      gasnett_atomic_decrement(&var);
      if (gasnett_atomic_read(&var) != 2*iters + i)
        ERR("gasnett_atomic_decrement got wrong value");
    }

    for (i=1;i<=iters;i++) {
      gasnett_atomic_set(&var, i);
      gasnett_atomic_increment(&var);
      if (gasnett_atomic_read(&var) != i+1)
        ERR("gasnett_atomic_set/gasnett_atomic_increment got wrong value");
    }

    for (i=1;i<=iters;i++) {
      gasnett_atomic_set(&var, i);
      gasnett_atomic_decrement(&var);
      if (gasnett_atomic_read(&var) != i-1)
        ERR("gasnett_atomic_set/gasnett_atomic_decrement got wrong value");
    }

    gasnett_atomic_set(&var,iters);
    for (i=iters-1;i>=1;i--) {
      if (gasnett_atomic_decrement_and_test(&var))
        ERR("gasnett_atomic_decrement_and_test got wrong value");
      if (gasnett_atomic_read(&var) != i)
        ERR("gasnett_atomic_decrement_and_test set wrong value");
    }
    if (!gasnett_atomic_decrement_and_test(&var))
      ERR("gasnett_atomic_decrement_and_test got wrong value at zero");
    if (gasnett_atomic_read(&var) != 0)
      ERR("gasnett_atomic_decrement_and_test set wrong value at zero");

    #if defined(GASNETT_HAVE_ATOMIC_CAS)
      gasnett_atomic_set(&var,0);
      for (i=0;i<=iters;i++) {
	if (gasnett_atomic_compare_and_swap(&var, i-1, i-2))
          ERR("gasnett_atomic_compare_and_swap succeeded at i=%i when it should have failed", i);
	if (gasnett_atomic_compare_and_swap(&var, i+1, i-2))
          ERR("gasnett_atomic_compare_and_swap succeeded at i=%i when it should have failed", i);
        if (gasnett_atomic_read(&var) != i)
          ERR("gasnett_atomic_compare_and_swap altered value when it should not have at i=%i", i);
	if (!gasnett_atomic_compare_and_swap(&var, i, i+1))
          ERR("gasnett_atomic_compare_and_swap failed at i=%i when it should have succeeded", i);
        if (gasnett_atomic_read(&var) != i+1)
          ERR("gasnett_atomic_compare_and_swap set wrong updated value at i=%i", i);
      }
    #endif
  }

#ifdef HAVE_PTHREAD_H
  MSG("Spawning pthreads...");
  { 
    int i;
    pthread_t threadid[MAX_NUM_THREADS];

    for(i=0;i<NUM_THREADS;i++) gasnett_atomic_set(thread_flag+i,1);
    gasnett_local_mb();
    #ifdef HAVE_PTHREAD_SETCONCURRENCY
        pthread_setconcurrency(NUM_THREADS);
    #endif

    for(i=0;i<NUM_THREADS;i++) {
      pthread_attr_t attr;   
      pthread_attr_init(&attr);   
      pthread_attr_setscope(&attr, PTHREAD_SCOPE_SYSTEM); 
      if (pthread_create(&threadid[i], &attr, &thread_fn, (void *)(uintptr_t)i)) 
        perror("pthread_create");
    }

    for(i=0;i<NUM_THREADS;i++) {
      if (pthread_join(threadid[i], NULL))
        perror("pthread_join");
    }
  }
#endif

  MSG("Done.");
  return (test_errs > 0 ? 1 : 0);
}

#ifdef HAVE_PTHREAD_H

#undef MSG0
#undef ERR
#define MSG0  test_makeMsg(("%s\n","%s"), (id == 0), 0)
#define ERR   test_makeMsg(("ERROR: thread %i: %s (at %s:%i)\n", \
                            id, "%s", __FILE__, __LINE__), 1, test_errs++)

gasnett_atomic_t up = gasnett_atomic_init(0);
gasnett_atomic_t down = gasnett_atomic_init(0);
gasnett_atomic_t x1 = gasnett_atomic_init(10000);
gasnett_atomic_t x2 = gasnett_atomic_init(10000);
gasnett_atomic_t x3 = gasnett_atomic_init(10000);
gasnett_atomic_t x4 = gasnett_atomic_init(10000);
gasnett_atomic_t x5 = gasnett_atomic_init(10000);

gasnett_atomic_t _thread_barrier = gasnett_atomic_init(0);

#define THREAD_BARRIER() do {                                             \
   barcnt++;                                                              \
   gasnett_local_mb();                                                    \
   gasnett_atomic_increment(&_thread_barrier);                            \
   while (gasnett_atomic_read(&_thread_barrier) < (barcnt*NUM_THREADS)) { \
      gasnett_sched_yield();                                              \
    }                                                                     \
  } while(0)                                                              \

#undef TEST_HEADER
#define TEST_HEADER(desc)                           \
  th_curtest++;                                     \
  THREAD_BARRIER();                                 \
  if ((!tests[0] || strchr(tests, th_curtest-1)) && \
      (MSG0("%c: %s",th_curtest-1,desc),1))

void * thread_fn(void *arg) {
  int id = (int)(uintptr_t)arg;
  int i;
  int iters2=100*iters;
  int barcnt = 0;
  char th_curtest = curtest;
 
  /* sanity check - ensure unique threadids */
  if (!gasnett_atomic_decrement_and_test(thread_flag+id)) {
      ERR("thread %i failed sanity check", id);
  }

  /* sanity check - ensure thread barriers are working */
  TEST_HEADER("parallel atomic-op barrier test...") {  
    for (i=0;i<iters;i++) {
      int tmp;
      /* simple count-up barrier */
      gasnett_atomic_increment(&up);
      while (gasnett_atomic_read(&up) < NUM_THREADS) gasnett_sched_yield(); 

      gasnett_atomic_set(&down, 2*NUM_THREADS);

      gasnett_atomic_increment(&up);
      while (gasnett_atomic_read(&up) < 2*NUM_THREADS) gasnett_sched_yield(); 

      tmp = gasnett_atomic_read(&up);
      if (tmp != 2*NUM_THREADS)
        ERR("count-up post-barrier read: %i != %i", tmp, 2*NUM_THREADS);

      /* simple count-down barrier */
      gasnett_atomic_decrement(&down);
      while (gasnett_atomic_read(&down) > NUM_THREADS) gasnett_sched_yield(); 

      gasnett_atomic_set(&up, 0);

      gasnett_atomic_decrement(&down);
      while (gasnett_atomic_read(&down) > 0) gasnett_sched_yield(); 

      tmp = gasnett_atomic_read(&down);
      if (tmp != 0)
        ERR("count-down post-barrier read: %i != 0", tmp);
    }
  }

  TEST_HEADER("parallel atomic-op pounding test...") {
    int val;
    gasnett_atomic_set(&x1, 5);
    gasnett_atomic_set(&x2, 5+iters2*NUM_THREADS);

    THREAD_BARRIER();

    for (i=0;i<iters2;i++) {
      gasnett_atomic_increment(&x1);
      gasnett_atomic_decrement(&x2);
    }

    THREAD_BARRIER();

    val = gasnett_atomic_read(&x1);
    if (val != 5+iters2*NUM_THREADS)
      ERR("pounding inc test mismatch: %i != %i",val,5+iters2*NUM_THREADS);

    val = gasnett_atomic_read(&x2);
    if (val != 5)
      ERR("pounding dec test mismatch: %i != 5",val);

  }

  TEST_HEADER("parallel dec-test pounding test...") {

    gasnett_atomic_set(&x3, NUM_THREADS);
    gasnett_atomic_set(&x4, 0);
    gasnett_atomic_set(&x5, 0); /* count of "wins" */

    THREAD_BARRIER();

    for (i=0;i<iters;i++) {
      if (gasnett_atomic_decrement_and_test(&x3)) { /* I won */
        gasnett_atomic_increment(&x5); /* tally win */
        if (gasnett_atomic_read(&x3) != 0) ERR("pounding dec-test mismatch x3");
        if (gasnett_atomic_read(&x4) != 0) ERR("pounding dec-test mismatch x4");
        gasnett_atomic_set(&x4, NUM_THREADS); /* go */
      } else {
        while (gasnett_atomic_read(&x4) == 0) gasnett_sched_yield(); /* I lost - wait */
      }

      if (gasnett_atomic_decrement_and_test(&x4)) { /* I won */
        gasnett_atomic_increment(&x5); /* tally win */
        if (gasnett_atomic_read(&x3) != 0) ERR(" pounding dec-test mismatch x3");
        if (gasnett_atomic_read(&x4) != 0) ERR("pounding dec-test mismatch x4");
        gasnett_atomic_set(&x3, NUM_THREADS); /* go */
      } else {
        while (gasnett_atomic_read(&x3) == 0) gasnett_sched_yield(); /* I lost - wait */
      }
    }

    if (gasnett_atomic_read(&x5) != 2*iters)
      ERR("pounding dec-test mismatch");
  }

  TEST_HEADER("parallel word-tearing test...") {

    gasnett_atomic_set(&x3, 0);
    gasnett_atomic_set(&x4, 0);
    gasnett_atomic_set(&x5, 0); 

    THREAD_BARRIER();

    if (NUM_THREADS <= 100) {  /* need 2*NUM_THREADS + 1 < 255 to prevent byte overflow */
      uint32_t x = id + 1;
      uint32_t myval = (x << 24) | (x << 16) | (x << 8) | x;
      for (i=0;i<iters2;i++) {
        uint32_t v;
        gasnett_atomic_set(&x3, myval);
        gasnett_atomic_set(&x4, myval);
        gasnett_atomic_set(&x5, myval);
        gasnett_atomic_increment(&x4);
        gasnett_atomic_decrement(&x5);
        v = gasnett_atomic_read(&x3);
        if (((v >> 24) & 0xFF) != (v & 0xFF) ||
            ((v >> 16) & 0xFF) != (v & 0xFF) ||
            ((v >>  8) & 0xFF) != (v & 0xFF)) 
            ERR("observed word tearing on gasnett_atomic_set");
        v = gasnett_atomic_read(&x4); 
        /* bottom byte may have increased by up to NUM_THREADS, but high bytes must be same */
        if (((v >> 24) & 0xFF) != ((v >>  8) & 0xFF) ||
            ((v >> 16) & 0xFF) != ((v >>  8) & 0xFF)) 
            ERR("observed word tearing on gasnett_atomic_set/gasnett_atomic_increment");
        v = gasnett_atomic_read(&x5); 
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
      static gasneti_atomic_t counter2 = gasneti_atomic_init(0);
      uint32_t goal = (NUM_THREADS * iters);
      uint32_t woncnt = 0;
      uint32_t oldval;
      while (woncnt < iters &&
             (oldval = gasneti_atomic_read(&counter2)) != goal) {
        if (gasneti_atomic_compare_and_swap(&counter2, oldval, (oldval + 1))) {
           woncnt++;
        }
      }
      THREAD_BARRIER();
      oldval = gasneti_atomic_read(&counter2);
      if (oldval != (NUM_THREADS * iters)) 
        ERR("failed compare-and-swap test: counter=%i expecting=%i", (int)oldval, (int)(NUM_THREADS * iters));
      if (woncnt != iters) 
        ERR("failed compare-and-swap test: woncnt=%i iters=%i", (int)woncnt, (int)iters);
    }
  #endif

  #if defined(GASNETI_HAVE_SPINLOCK)
    /* spinlocks are GASNet-internal and not available via gasnet_tools,
       so this test is currently disabled */
    TEST_HEADER("parallel spinlock test...") {
      static gasneti_atomic_t my_lock = GASNETI_SPINLOCK_INITIALIZER;
      static uint32_t counter1 = 0;

      for (i=0;i<=iters;i++) {
	gasneti_spinlock_lock(&my_lock);
	++counter1;
	gasneti_spinlock_unlock(&my_lock);
      }
      THREAD_BARRIER();
      if (counter1 != (NUM_THREADS * iters)) 
        ERR("failed spinlock test: counter=%i expecting=%i", counter, (NUM_THREADS * iters));
    }
  #endif

  THREAD_BARRIER();

  return NULL;
}

#endif

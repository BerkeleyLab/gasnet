/*   $Source: /Users/kamil/work/gasnet-cvs2/gasnet/tests/testtools.c,v $
 *     $Date: 2005/01/03 15:15:37 $
 * $Revision: 1.15.2.1 $
 * Description: helpers for GASNet tests
 * Copyright 2002, Dan Bonachea <bonachea@cs.berkeley.edu>
 * Terms of use are as specified in license.txt
 */

#include <gasnet_tools.h>

/* specifically omit gasnet.h/test.h to test independence */
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <math.h>
#include <sys/time.h>

#ifdef HAVE_PTHREAD_H
  #include <pthread.h>
  #define NUM_THREADS 10
#endif


static int64_t mygetMicrosecondTimeStamp(void)
{
    int64_t retval;
    struct timeval tv;
    if (gettimeofday(&tv, NULL)) {
	perror("gettimeofday");
	abort();
    }
    retval = ((int64_t)tv.tv_sec) * 1000000 + tv.tv_usec;
    return retval;
}
#define TIME() mygetMicrosecondTimeStamp()

void * thread_fn(void *arg);

int main() {

  #if defined(GASNETT_PAGESIZE) && defined(GASNETT_PAGESHIFT)
    if (0x1 << GASNETT_PAGESHIFT != GASNETT_PAGESIZE)
      printf("ERROR: bad pagesizes: GASNETT_PAGESHIFT=%i GASNETT_PAGESIZE=%i\n",
              GASNETT_PAGESHIFT, GASNETT_PAGESIZE);
    else 
      printf("System page size is 2^%i == %i\n", GASNETT_PAGESHIFT, GASNETT_PAGESIZE);
  #endif
   
  printf("Testing high-performance timers...\n");
  { /* high performance timers */
    int i;
    gasnett_tick_t start, end;
    int64_t startref, endref;
    gasnett_tick_t ticktimemin = GASNETT_TICK_MIN;
    gasnett_tick_t ticktimemax = GASNETT_TICK_MAX;

    if (!(ticktimemin < ticktimemax)) printf("ERROR: !(min < max)\n");
    if (!(gasnett_ticks_now() > ticktimemin)) printf("ERROR: !(now > min)\n");
    if (!(gasnett_ticks_now() < ticktimemax)) printf("ERROR: !(now < max)\n");

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
        printf("ERROR: timer and reference differ by more than 0.1sec:\n"
               "\ttime=%i  timeref=%i\n",time,timeref);

      if (abs( (gasnett_ticks_to_us(end) - gasnett_ticks_to_us(start)) - 
               gasnett_ticks_to_us(end - start) ) > 1)
        printf("ERROR: ticks_to_us(A) - ticks_to_us(B) != ticks_to_us(A-B)\n");

    }
    {
      double overhead = gasnett_timer_overheadus();
      double granularity = gasnett_timer_granularityus();
      if (granularity <= 0.0 || overhead <= 0.0 ||
          (granularity+0.1) < 0.5*overhead) 
          /* allow some leeway for noise at granularities approaching cycle speed */
          /*granularity < 0.5*overhead)*/
        printf("ERROR: nonsensical timer overhead/granularity measurements:\n"
               "  overhead: %.3fus  granularity: %.3fus\n",overhead, granularity);
    }
  }

  printf("Testing local membar...\n");
  { /* local membar */
    int i;
    for (i=0;i<100;i++) {
      gasnett_local_mb();
    }
  }

  printf("Testing local write membar...\n");
  { /* local membar */
    int i;
    for (i=0;i<100;i++) {
      gasnett_local_wmb();
    }
  }

  printf("Testing local read membar...\n");
  { /* local membar */
    int i;
    for (i=0;i<100;i++) {
      gasnett_local_rmb();
    }
  }

  printf("Testing atomic ops (sequential)...\n");
  { /* we can't really test atomicity without spinning threads, 
       but we can at least test simple operations  */
    int i = 0;
    gasnett_atomic_t var = gasnett_atomic_init(100);

    if (gasnett_atomic_read(&var) != 100)
      printf("ERROR: gasnett_atomic_init/gasnett_atomic_read got wrong value\n");

    gasnett_atomic_set(&var, 200);
    if (gasnett_atomic_read(&var) != 200)
      printf("ERROR: gasnett_atomic_set/gasnett_atomic_read got wrong value\n");

    for (i=1;i<=100;i++) {
      gasnett_atomic_increment(&var);
      if (gasnett_atomic_read(&var) != 200 + i)
        printf("ERROR: gasnett_atomic_increment got wrong value\n");
    }

    for (i=99;i>=0;i--) {
      gasnett_atomic_decrement(&var);
      if (gasnett_atomic_read(&var) != 200 + i)
        printf("ERROR: gasnett_atomic_decrement got wrong value\n");
    }

    gasnett_atomic_set(&var,100);
    for (i=99;i>=1;i--) {
      if (gasnett_atomic_decrement_and_test(&var))
        printf("ERROR: gasnett_atomic_decrement_and_test got wrong value\n");
      if (gasnett_atomic_read(&var) != i)
        printf("ERROR: gasnett_atomic_decrement_and_test set wrong value\n");
    }
    if (!gasnett_atomic_decrement_and_test(&var))
      printf("ERROR: gasnett_atomic_decrement_and_test got wrong value at zero\n");
    if (gasnett_atomic_read(&var) != 0)
      printf("ERROR: gasnett_atomic_decrement_and_test set wrong value at zero\n");
  }

#ifdef HAVE_PTHREAD_H
  printf("Testing atomic ops (parallel)...\n");
  { 
    int i;
    pthread_t threadid[NUM_THREADS];

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

  printf("Done.\n");
  return 0;
}

#ifdef HAVE_PTHREAD_H

gasnett_atomic_t up = gasnett_atomic_init(0);
gasnett_atomic_t down = gasnett_atomic_init(2*NUM_THREADS);
gasnett_atomic_t x1 = gasnett_atomic_init(10000);
gasnett_atomic_t x2 = gasnett_atomic_init(10000);
gasnett_atomic_t x3 = gasnett_atomic_init(10000);
gasnett_atomic_t x4 = gasnett_atomic_init(10000);
gasnett_atomic_t x5 = gasnett_atomic_init(10000);

void * thread_fn(void *arg) {
  int id = (int)(uintptr_t)arg;
  int i;
  int iters=10;
  int iters2=1000;

  if (id == 0) printf("parallel atomic-op barrier test...\n");

  for (i=0;i<iters;i++) {
    /* simple count-up barrier */
    gasnett_atomic_increment(&up);
    while (gasnett_atomic_read(&up) < NUM_THREADS) gasnett_sched_yield(); 

    gasnett_atomic_set(&down, 2*NUM_THREADS);

    gasnett_atomic_increment(&up);
    while (gasnett_atomic_read(&up) < 2*NUM_THREADS) gasnett_sched_yield(); 

    if (gasnett_atomic_read(&up) != 2*NUM_THREADS)
      printf("ERROR: count-up post-barrier read\n");

    /* simple count-down barrier */
    gasnett_atomic_decrement(&down);
    while (gasnett_atomic_read(&down) > NUM_THREADS) gasnett_sched_yield(); 

    gasnett_atomic_set(&up, 0);

    gasnett_atomic_decrement(&down);
    while (gasnett_atomic_read(&down) > 0) gasnett_sched_yield(); 

    if (gasnett_atomic_read(&down) != 0)
      printf("ERROR: count-down post-barrier read\n");
  }
  
  if (id == 0) printf("parallel atomic-op pounding test...\n");

  gasnett_atomic_increment(&up);
  while (gasnett_atomic_read(&up) < NUM_THREADS) gasnett_sched_yield(); 

  for (i=0;i<iters2;i++) {
    gasnett_atomic_increment(&x1);
    gasnett_atomic_decrement(&x2);
  }

  gasnett_atomic_increment(&up);
  while (gasnett_atomic_read(&up) < 2*NUM_THREADS) gasnett_sched_yield(); 

  if (gasnett_atomic_read(&x1) != 10000+iters2*NUM_THREADS)
    printf("ERROR: pounding inc test mismatch\n");

  if (gasnett_atomic_read(&x2) != 10000-iters2*NUM_THREADS)
    printf("ERROR: pounding dec test mismatch\n");

  if (id == 0) printf("parallel dec-test pounding test...\n");


  gasnett_atomic_set(&x3, NUM_THREADS);
  gasnett_atomic_set(&x4, 0);
  gasnett_atomic_set(&x5, 0); /* count of "wins" */

  gasnett_atomic_increment(&up);
  while (gasnett_atomic_read(&up) < 3*NUM_THREADS) gasnett_sched_yield(); 

  for (i=0;i<iters;i++) {
    if (gasnett_atomic_decrement_and_test(&x3)) { /* I won */
      gasnett_atomic_increment(&x5); /* tally win */
      if (gasnett_atomic_read(&x3) != 0) printf("ERROR: pounding dec-test mismatch x3\n");
      if (gasnett_atomic_read(&x4) != 0) printf("ERROR: pounding dec-test mismatch x4\n");
      gasnett_atomic_set(&x4, NUM_THREADS); /* go */
    } else {
      while (gasnett_atomic_read(&x4) == 0) gasnett_sched_yield(); /* I lost - wait */
    }

    if (gasnett_atomic_decrement_and_test(&x4)) { /* I won */
      gasnett_atomic_increment(&x5); /* tally win */
      if (gasnett_atomic_read(&x3) != 0) printf("ERROR: pounding dec-test mismatch x3\n");
      if (gasnett_atomic_read(&x4) != 0) printf("ERROR: pounding dec-test mismatch x4\n");
      gasnett_atomic_set(&x3, NUM_THREADS); /* go */
    } else {
      while (gasnett_atomic_read(&x3) == 0) gasnett_sched_yield(); /* I lost - wait */
    }
  }

  if (gasnett_atomic_read(&x5) != 2*iters)
    printf("ERROR: pounding dec-test mismatch\n");

  gasnett_atomic_increment(&up);
  while (gasnett_atomic_read(&up) < 4*NUM_THREADS) gasnett_sched_yield(); 


  return NULL;
}

/* Mimic Berkeley UPC build config strings, to allow running GASNet tests using upcrun */
#define GASNET_CONFIG_STRING \
  "RELEASE=x,SPEC=x,CONDUIT=SMP-x/REFERENCE-x,THREADMODEL=PAR,SEGMENT=FAST,PTR=x,align,nodebug,notrace,nostats"
GASNETT_IDENT(GASNetT_IdentString_link_GASNetConfig, 
 "$GASNetConfig: (<link>) " GASNET_CONFIG_STRING " $");
GASNETT_IDENT(GASNetT_IdentString_link_UPCRConfig,
 "$UPCRConfig: (<link>) " GASNET_CONFIG_STRING ",SHMEM=pthreads,dynamicthreads $");
GASNETT_IDENT(GASNetT_IdentString_link_upcver, 
 "$UPCVersion: (<link>) *** GASNet test *** $");
GASNETT_IDENT(GASNetT_IdentString_link_compileline, 
 "$UPCCompileLine: (<link>) *** GASNet test *** $");
GASNETT_IDENT(GASNetT_IdentString_link_compiletime, 
 "$UPCCompileTime: (<link>) " __DATE__ " " __TIME__ " $");
GASNETT_IDENT(GASNetT_IdentString_HeapSz, 
 "$UPCRDefaultHeapSizes: UPC_SHARED_HEAP_OFFSET=0 UPC_SHARED_HEAP_SIZE=0 $");
GASNETT_IDENT(GASNetT_IdentString_PthCnt, "$UPCRDefaultPthreadCount: 1 $");
#ifdef GASNETI_PTR32
  GASNETT_IDENT(GASNetT_IdentString_PtrSz, "$UPCRSizeof: void_ptr=( $");
#else
  GASNETT_IDENT(GASNetT_IdentString_PtrSz, "$UPCRSizeof: void_ptr=, $");
#endif
/* Ditto for Titanium tcrun */
GASNETT_IDENT(GASNetT_TiBackend_IdentString,
 "$TitaniumBackend: sequential $");
GASNETT_IDENT(GASNetT_TiCompiler_IdentString,          
 "$TitaniumCompilerFlags: *** GASNet test *** -g $");

#endif

/*  $Archive:: /Ti/GASNet/tests/test.h                                    $
 *     $Date: 2004/04/20 00:24:36 $
 * $Revision: 1.18.2.3 $
 * Description: helpers for GASNet tests
 * Copyright 2002, Dan Bonachea <bonachea@cs.berkeley.edu>
 * Terms of use are as specified in license.txt
 */


#ifndef _TEST_H
#define _TEST_H

#include <stdio.h>
#include <stdarg.h>
#include <stdlib.h>
#include <time.h>
#include <sys/time.h>
#include <unistd.h>
#include <fcntl.h>

#if !defined(DEBUG) && !defined(NDEBUG)
  #ifdef GASNET_DEBUG
    #define DEBUG 1
  #else
    #define NDEBUG 1
  #endif
#endif
#include <assert.h>

#include <gasnet.h>
#include <gasnet_tools.h>

#define GASNET_Safe(fncall) do {                            \
    int retval;                                             \
    if ((retval = fncall) != GASNET_OK) {                   \
            fprintf(stderr, "Error calling: %s\n"           \
                   " at: %s:%i\n"                           \
                   " error: %s (%s)\n",                     \
                   #fncall, __FILE__, __LINE__,             \
                   gasnet_ErrorName(retval),                \
                   gasnet_ErrorDesc(retval));               \
            fflush(stderr);                                 \
            gasnet_exit(retval);                            \
    }                                                       \
  } while(0)

/* return a microsecond time-stamp */
#ifdef FORCE_GETTIMEOFDAY
  static int64_t mygetMicrosecondTimeStamp(void) {
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
#else
  #define TIME() gasnett_ticks_to_us(gasnett_ticks_now()) 
#endif

uint64_t test_checksum(void *p, int numbytes) {
 uint8_t *buf = (uint8_t *)p;
 uint64_t result = 0;
 int i;
 for (i=0;i<numbytes;i++) {
   result = ((result << 8) | ((result >> 56) & 0xFF) ) ^ *buf;
   buf++;
 }
 return result;
}

static void _MSG(const char *format, ...) __attribute__((__format__ (__printf__, 1, 2)));
static void _MSG(const char *format, ...) {
  #define TEST_BUFSZ 1024
  char output[TEST_BUFSZ];
  va_list argptr;
  va_start(argptr, format); /*  pass in last argument */
    { int sz = vsnprintf(output, TEST_BUFSZ, format, argptr);
      if (sz >= (TEST_BUFSZ-5) || sz < 0) strcpy(output+(TEST_BUFSZ-5),"...");
    }
  va_end(argptr);
  printf("node %i/%i %s\n", (int)gasnet_mynode(), (int)gasnet_nodes(), output); 
  fflush(stdout);
}

#define MSG GASNETT_TRACE_SETSOURCELINE(__FILE__,__LINE__), _MSG

#define BARRIER() do {                                                \
  gasnete_barrier_notify(0,GASNET_BARRIERFLAG_ANONYMOUS);            \
  GASNET_Safe(gasnete_barrier_wait(0,GASNET_BARRIERFLAG_ANONYMOUS)); \
} while (0)

static void *_test_malloc(size_t sz, const char *curloc) {
  void *ptr;
  gasnet_hold_interrupts();
  ptr = malloc(sz);
  gasnet_resume_interrupts();
  if (ptr == NULL) {
    fprintf(stderr,"*** ERROR: Failed to malloc(%i) bytes at %s\n",(int)sz,curloc);
    abort();
  }
  return ptr;
}
static void *_test_calloc(size_t sz, const char *curloc) {
  void *retval = _test_malloc(sz, curloc);
  if (retval) memset(retval, 0, sz);
  return retval;
}
#define test_malloc(sz) _test_malloc((sz), __FILE__ ":" _STRINGIFY(__LINE__))
#define test_calloc(N,S) _test_calloc((N*S), __FILE__ ":" _STRINGIFY(__LINE__))

static void test_free(void *ptr) {
  gasnet_hold_interrupts();
  free(ptr);
  gasnet_resume_interrupts();
}

#define PAGESZ GASNETT_PAGESIZE
#define alignup(a,b) ((((a)+(b)-1)/(b))*(b))

#if defined(GASNET_PAR) || defined(GASNET_PARSYNC)
  #ifndef TEST_MAXTHREADS
    #define TEST_MAXTHREADS      256
  #endif
  #ifndef TEST_SEGZ_PER_THREAD
    #define TEST_SEGZ_PER_THREAD (64*1024)
  #endif
  #ifndef TEST_SEGSZ
    #define TEST_SEGSZ	      alignup(TEST_MAXTHREADS*TEST_SEGZ_PER_THREAD,PAGESZ)
  #endif
  #if TEST_SEGSZ < (TEST_MAXTHREADS*TEST_SEGZ_PER_THREAD)
    #error "TEST_SEGSZ < (TEST_MAXTHREADS*TEST_SEGZ_PER_THREAD)"
  #endif
#else
  #ifndef TEST_SEGSZ
    #define TEST_SEGSZ          alignup(64*1024,PAGESZ)
  #endif
#endif
#if (TEST_SEGSZ % PAGESZ) != 0 || TEST_SEGSZ <= 0
  #error Bad TEST_SEGSZ
#endif

#define TEST_MINHEAPOFFSET  alignup(128*4096,PAGESZ)
#if (TEST_MINHEAPOFFSET % PAGESZ) != 0 
  #error Bad TEST_MINHEAPOFFSET
#endif

#ifdef GASNET_SEGMENT_EVERYTHING
  uint8_t _hidden_seg[TEST_SEGSZ+PAGESZ];
  #define TEST_SEG(node) ((void *)(((uint8_t*)_hidden_seg) + \
    (((((uintptr_t)_hidden_seg)%PAGESZ) == 0)? 0 :           \
     (PAGESZ-(((uintptr_t)_hidden_seg)%PAGESZ)))))          
#else
  static void *_test_getseg(gasnet_node_t node) {
    static gasnet_seginfo_t *si = NULL;
    if (si == NULL) {
      int i;
      gasnet_seginfo_t *s = test_malloc(gasnet_nodes()*sizeof(gasnet_seginfo_t));
      GASNET_Safe(gasnet_getSegmentInfo(s, gasnet_nodes()));
      for (i=0; i < gasnet_nodes(); i++) {
        assert(s[i].size >= TEST_SEGSZ);
        #if GASNET_ALIGNED_SEGMENTS == 1
          assert(s[i].addr == s[0].addr);
        #endif
      }
      si = s;
    }
    return si[node].addr;
  }
  #define TEST_SEG(node) (_test_getseg(node))
#endif

#define TEST_MYSEG()          (TEST_SEG(gasnet_mynode()))

int _test_rand(int low, int high) {
  int result;
  assert(low <= high);
  result = low+(int)(((double)(high-low+1))*rand()/(RAND_MAX+1.0));
  assert(result >= low && result <= high);
  return result;
}
#define TEST_RAND(low,high) _test_rand((low), (high))
#define TEST_RAND_PICK(a,b) (TEST_RAND(0,1)==1?(a):(b))
#define TEST_SRAND(seed)    srand(seed)
#define TEST_RAND_ONEIN(p)  (TEST_RAND(1,p) == 1)

#define TEST_HIWORD(arg)     ((uint32_t)(((uint64_t)(arg)) >> 32))
#define TEST_LOWORD(arg)     ((uint32_t)((uint64_t)(arg)))

/* Functions for obtaining calibrated delays */
#ifdef TEST_DELAY
extern void test_delay(int64_t n);	 /* in delay.o */

/* smallest number of delay loops to try in calibration */
#ifndef TEST_DELAY_LOOP_MIN
  #define TEST_DELAY_LOOP_MIN        100
#endif
/* max number of calibration iterations to wait for convergance */
#ifndef TEST_DELAY_CALIBRATION_LIMIT
  #define TEST_DELAY_CALIBRATION_LIMIT 100
#endif

/* Compute the number of loops needed to get no less that the specified delay
 * when executing "test_delay(loops)" excatly 'iters' times.
 *
 * Returns the number of loops needed and overwrites the argument with the
 * actual achieved delay for 'iters' calls to "delay(*time_p)".
 * The 'time_p' is given in microseconds.
 */
int64_t test_calibrate_delay(int iters, int64_t *time_p) 
{
	int64_t begin, end, time;
	float target = *time_p;
	float ratio = 0.0;
	int i;
        int64_t loops = 0;
        int caliters = 0;

	do {
		if (loops == 0) {
			loops = TEST_DELAY_LOOP_MIN;	/* first pass */
		} else {
			int64_t tmp = loops * ratio;

			if (tmp > loops) {
				loops = tmp;
			} else {
				loops += 1;	/* ensure progress in the face of round-off */
			}
                        assert(loops < 1ll<<62);
		}

		begin = TIME();
		for (i = 0; i < iters; i++) { test_delay(loops); }
		end = TIME();
		time = end - begin;
                assert(time > 0);
		ratio = target / (float)time;
                caliters++;
                if (caliters > TEST_DELAY_CALIBRATION_LIMIT) {
                  fprintf(stderr,"ERROR: test_calibrate_delay(%i,%i) failed to converge after %i iterations.\n",
                          iters, (int)*time_p, iters);
                  abort();
                }
              #if 0
                printf("loops=%llu\n",(unsigned long long)loops); fflush(stdout);
                printf("ratio=%f target=%f time=%llu\n",ratio,target,(unsigned long long)time); fflush(stdout);
              #endif
	} while (ratio > 1.0);

	*time_p = time;
	return loops;
}
#endif

#endif

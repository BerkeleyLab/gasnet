/*  $Archive:: /Ti/GASNet/tests/test.h                                    $
 *     $Date: 2004/01/23 10:35:05 $
 * $Revision: 1.21 $
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
  #include <gasnet_tools.h>
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
  #define BUFSZ 1024
  char output[BUFSZ];
  va_list argptr;
  va_start(argptr, format); /*  pass in last argument */
    { int sz = vsnprintf(output, BUFSZ, format, argptr);
      assert(sz <= BUFSZ);
      if (sz >= (BUFSZ-5)) strcpy(output+(BUFSZ-5),"...");
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
#define test_malloc(sz) _test_malloc((sz), __FILE__ ":" _STRINGIFY(__LINE__))
static void test_free(void *ptr) {
  gasnet_hold_interrupts();
  free(ptr);
  gasnet_resume_interrupts();
}


#ifdef IRIX
  #define PAGESZ 16384
#elif defined(OSF) || defined(__alpha__)
  #define PAGESZ 8192
#else
  #define PAGESZ 4096
#endif


#if defined(GASNET_PAR) || defined(GASNET_PARSYNC)
  #define TEST_MAXTHREADS      256
  #define TEST_SEGZ_PER_THREAD 64*1024
  #define TEST_SEGSZ	      (TEST_MAXTHREADS*TEST_SEGZ_PER_THREAD)
#else
  #define TEST_SEGSZ          (64*1024)
#endif

#define TEST_MINHEAPOFFSET  (128*PAGESZ)

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

#endif

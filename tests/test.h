/*  $Archive:: /Ti/GASNet/tests/test.h                                    $
 *     $Date: 2003/06/30 21:04:05 $
 * $Revision: 1.12 $
 * Description: helpers for GASNet tests
 * Copyright 2002, Dan Bonachea <bonachea@cs.berkeley.edu>
 * Terms of use are as specified in license.txt
 */


#ifndef _TEST_H
#define _TEST_H

#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <sys/time.h>
#include <unistd.h>
#include <fcntl.h>

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


#define MSG(s) do {                                                              \
  printf("node %i/%i %s\n", gasnet_mynode(), gasnet_nodes(), s); fflush(stdout); \
  } while(0)

#define BARRIER() do {                                                \
  gasnete_barrier_notify(0,GASNET_BARRIERFLAG_ANONYMOUS);            \
  GASNET_Safe(gasnete_barrier_wait(0,GASNET_BARRIERFLAG_ANONYMOUS)); \
} while (0)

#ifdef IRIX
  #define PAGESZ 16384
#elif defined(OSF) || defined(__alpha__)
  #define PAGESZ 8192
#else
  #define PAGESZ 4096
#endif

#define TEST_SEGSZ          (64*1024)
#define TEST_MINHEAPOFFSET  (128*PAGESZ)

#ifdef GASNET_SEGMENT_EVERYTHING
  uint8_t _hidden_seg[TEST_SEGSZ+PAGESZ];
  #define TEST_SEG(node) ((void *)(((uint8_t*)_hidden_seg) + \
    (((((uintptr_t)_hidden_seg)%PAGESZ) == 0)? 0 :           \
     (PAGESZ-(((uintptr_t)_hidden_seg)%PAGESZ)))))          
#else
  static void *_test_getseg(gasnet_node_t node) {
    gasnet_seginfo_t *si = malloc(gasnet_nodes()*sizeof(gasnet_seginfo_t));
    void *ptr;
    GASNET_Safe(gasnet_getSegmentInfo(si, gasnet_nodes()));
    assert(si[gasnet_mynode()].size >= TEST_SEGSZ && si[node].size >= TEST_SEGSZ);
    ptr = si[node].addr;
    free(si);
    return ptr;
  }
  #define TEST_SEG(node) (_test_getseg(node))
#endif
#define TEST_MYSEG()          (TEST_SEG(gasnet_mynode()))

#endif

/*  $Archive:: /Ti/GASNet/extended/gasnet_extended_fwd.h                  $
 *     $Date: 2004/07/15 01:29:30 $
 * $Revision: 1.2 $
 * Description: GASNet Extended API Header (forward decls)
 * Copyright 2002, Dan Bonachea <bonachea@cs.berkeley.edu>
 * Terms of use are as specified in license.txt
 */

#ifndef _IN_GASNET_H
  #error This file is not meant to be included directly- clients should include gasnet.h
#endif

#ifndef _GASNET_EXTENDED_FWD_H
#define _GASNET_EXTENDED_FWD_H

#define GASNET_EXTENDED_VERSION      1.5
#define GASNET_EXTENDED_VERSION_STR  _STRINGIFY(GASNET_EXTENDED_VERSION)
#define GASNET_EXTENDED_NAME         FIREHOSETEST
#define GASNET_EXTENDED_NAME_STR     _STRINGIFY(GASNET_EXTENDED_NAME)

#define GASNET_INVALID_HANDLE ((gasnet_handle_t)0)

#if GASNETI_CLIENT_THREADS
  #define GASNETI_THREADINFO_OPT
  #define GASNETI_LAZY_BEGINFUNCTION
#endif

#ifdef GASNETI_THREADINFO_OPT
  /* Here we use a clever trick - GASNET_GET_THREADINFO() uses the sizeof(gasneti_threadinfo_available)
      to determine whether gasneti_threadinfo_cache was bound a value posted by GASNET_POST_THREADINFO()
      of if it bound to the globally declared dummy variables. 
     Even a very stupid C optimizer should constant-fold away the unused calls to gasneti_get_threadinfo() 
      and discard the unused variables
     We need 2 separate variables to ensure correct name-binding semantics for GASNET_POST_THREADINFO(GASNET_GET_THREADINFO())
   */
  static uint8_t gasnete_threadinfo_cache = 0;
  static uint8_t gasnete_threadinfo_available = 
    sizeof(gasnete_threadinfo_cache) + sizeof(gasnete_threadinfo_available);
    /* silly little trick to prevent unused variable warning on gcc -Wall */

  #define GASNET_POST_THREADINFO(info)                     \
    gasnet_threadinfo_t gasnete_threadinfo_cache = (info); \
    uint32_t gasnete_threadinfo_available = 0
    /* if you get an unused variable warning on gasnete_threadinfo_available, 
       it means you POST'ed in a function which made no GASNet calls that needed it */

  #ifdef GASNETI_LAZY_BEGINFUNCTION
    #define GASNET_GET_THREADINFO()                              \
      ( (sizeof(gasnete_threadinfo_available) == 1) ?            \
        (gasnet_threadinfo_t)gasnete_mythread() :                \
        ( (uintptr_t)gasnete_threadinfo_cache == 0 ?             \
          ((*(gasnet_threadinfo_t *)&gasnete_threadinfo_cache) = \
            (gasnet_threadinfo_t)gasnete_mythread()) :           \
          (gasnet_threadinfo_t)(uintptr_t)gasnete_threadinfo_cache) )
  #else
    #define GASNET_GET_THREADINFO()                   \
      ( (sizeof(gasnete_threadinfo_available) == 1) ? \
        (gasnet_threadinfo_t)gasnete_mythread() :     \
        (gasnet_threadinfo_t)(uintptr_t)gasnete_threadinfo_cache )
  #endif

  /* the gasnet_threadinfo_t pointer points to a thread data-structure owned by
     the extended API, whose first element is a pointer reserved
     for use by the core API (initialized to NULL)
   */

  #ifdef GASNETI_LAZY_BEGINFUNCTION
    /* postpone thread discovery to first use */
    #define GASNET_BEGIN_FUNCTION() GASNET_POST_THREADINFO(0)
  #else
    #define GASNET_BEGIN_FUNCTION() GASNET_POST_THREADINFO(GASNET_GET_THREADINFO())
  #endif

#else
  #define GASNET_POST_THREADINFO(info)   \
    static uint8_t gasnete_dummy = sizeof(gasnete_dummy) /* prevent a parse error */
  #define GASNET_GET_THREADINFO() (NULL)
  #define GASNET_BEGIN_FUNCTION() GASNET_POST_THREADINFO(GASNET_GET_THREADINFO())
#endif

  /* this can be used to add statistical collection values 
     specific to the extended API implementation (see gasnet_help.h) */
#define CONDUIT_EXTENDED_STATS(CNT,VAL,TIME) 		\
        GASNETI_REFVIS_STATS(CNT,VAL,TIME)              \
        CNT(C, DYNAMIC_THREADLOOKUP, cnt)		\
	CNT(C, FIREHOSE_REMOTE_HITS, remote firehose hits) \
	CNT(C, FIREHOSE_REMOTE_MISSES, remote firehose misses) \
	CNT(C, FIREHOSE_LOCAL_HITS, local firehose hits) \
	CNT(C, FIREHOSE_LOCAL_MISSES, local firehose misses) \
	\
	VAL(C, FIREHOSE_MOVE_OLD_BUCKETS,		\
		number of replacement firhoses)		\
	CNT(C, FIREHOSE_VICTIM_POLLS,			\
		number of firehoses recovered by poll)	\
	VAL(C, FIREHOSE_TOUCHED, 			\
		firehoses touched for puts)		\
	VAL(C, BUCKET_LOCAL_PINS,			\
		local buckets pinned for puts/gets)	\
	VAL(C, BUCKET_LOCAL_TOUCHED, 			\
		local buckets touched for puts/gets)	\
	VAL(C, BUCKET_VICTIM_UNPINS, 			\
		number of bucket unpins in victim FIFO) \
	VAL(C, BUCKET_VICTIM_COUNT, 			\
		number of buckets in victim FIFO)	\
	TIME(C, FIREHOSE_MOVE_TIME, unpin+pin time in   \
		firehose handler)			\
	TIME(C, FIREHOSE_BUILD_LIST_TIME, time to build \
		firehose list)				\
	TIME(C, FIREHOSE_MOVE_LOCAL, local bookkeeping	\
		in firehose reply handler)		\
	TIME(C, FIREHOSE_UNPIN_TIME, unpin time in	\
		firehose handler)			\
	TIME(C, FIREHOSE_PIN_TIME, pin time in firehose \
		handler)				\
	TIME(C, FIREHOSE_PUT_ONE, puts one fh move)	\
	TIME(C, FIREHOSE_PUT_MANY, puts many fh moves)	\
	TIME(C, FIREHOSE_PUT_ONESIDED, puts one-sided)  \
	TIME(C, FIREHOSE_GET_ONE, gets one fh move)	\
	TIME(C, FIREHOSE_GET_MANY, gets many fh moves)	\
	TIME(C, FIREHOSE_GET_ONESIDED, gets one-sided)


#endif


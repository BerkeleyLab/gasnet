/*  $Archive:: /Ti/GASNet/gasnet_help.h                                   $
 *     $Date: 2004/05/02 08:05:11 $
 * $Revision: 1.25 $
 * Description: GASNet Header Helpers (Internal code, not for client use)
 * Copyright 2002, Dan Bonachea <bonachea@cs.berkeley.edu>
 * Terms of use are as specified in license.txt
 */

#ifndef _IN_GASNET_H
  #error This file is not meant to be included directly- clients should include gasnet.h
#endif

#ifndef _GASNET_HELP_H
#define _GASNET_HELP_H

#include <errno.h>
#include <string.h>
#include <stdlib.h>
#if GASNETI_THREADS
  #ifdef LINUX
   struct timespec; /* avoid an annoying warning on Linux */
  #endif
  #include <pthread.h>
#endif

BEGIN_EXTERNC

extern void gasneti_fatalerror(const char *msg, ...) GASNET_NORETURN __attribute__((__format__ (__printf__, 1, 2)));
extern char *gasneti_getenv(const char *keyname);

/* set/unset an environment variable, for the local process ONLY */
extern void gasneti_setenv(const char *key, const char *value);
extern void gasneti_unsetenv(const char *key);

/* extern versions of gasneti_malloc/gasnet_free for use in public headers */
extern void *gasneti_extern_malloc(size_t sz);
extern void gasneti_extern_free(void *p);

#if defined(__GNUC__) || defined(__FUNCTION__)
  #define GASNETI_CURRENT_FUNCTION __FUNCTION__
#elif defined(HAVE_FUNC)
  /* __func__ should also work for ISO C99 compilers */
  #define GASNETI_CURRENT_FUNCTION __func__
#else
  #define GASNETI_CURRENT_FUNCTION ""
#endif
extern char *gasneti_build_loc_str(const char *funcname, const char *filename, int linenum);
#define gasneti_current_loc gasneti_build_loc_str(GASNETI_CURRENT_FUNCTION,__FILE__,__LINE__)

#if GASNET_NDEBUG
  #define gasneti_boundscheck(node,ptr,nbytes,T) 
#else
  #define gasneti_boundscheck(node,ptr,nbytes,T) do {                                                             \
      gasnet_node_t _node = node;                                                                                 \
      uintptr_t _ptr = (uintptr_t)ptr;                                                                            \
      size_t _nbytes = nbytes;                                                                                    \
      if_pf (_node > gasnet##T##_nodes)                                                                           \
        gasneti_fatalerror("Node index out of range (%lu >= %lu) at %s",                                          \
                           (unsigned long)_node, (unsigned long)gasnet##T##_nodes, gasneti_current_loc);          \
      if_pf (_ptr < (uintptr_t)gasnet##T##_seginfo[_node].addr ||                                                 \
             (_ptr + _nbytes) > (((uintptr_t)gasnet##T##_seginfo[_node].addr) + gasnet##T##_seginfo[_node].size)) \
        gasneti_fatalerror("Remote address out of range (node=%lu ptr="GASNETI_LADDRFMT" nbytes=%lu "             \
                           "segment=("GASNETI_LADDRFMT"..."GASNETI_LADDRFMT")) at %s",                            \
                           (unsigned long)_node, GASNETI_LADDRSTR(_ptr), (unsigned long)_nbytes,                  \
                           GASNETI_LADDRSTR(gasnet##T##_seginfo[_node].addr),                                     \
                           GASNETI_LADDRSTR(((uint8_t*)gasnet##T##_seginfo[_node].addr) +                         \
                                            gasnet##T##_seginfo[_node].size),                                     \
                           gasneti_current_loc);                                                                  \
    } while(0)
#endif

/* gasneti_assert_always():
 * an assertion that never compiles away - for sanity checks in non-critical paths 
 */
#define gasneti_assert_always(expr) \
    (PREDICT_TRUE(expr) ? (void)0 : gasneti_fatalerror("Assertion failure at %s: %s", gasneti_current_loc, #expr))

/* gasneti_assert():
 * an assertion that compiles away in non-debug mode - for sanity checks in critical paths 
 */
#if GASNET_NDEBUG
  #define gasneti_assert(expr) ((void)0)
#else
  #define gasneti_assert(expr) gasneti_assert_always(expr)
#endif

#if GASNET_DEBUG
  extern void gasneti_checkinit();
  extern void gasneti_checkattach();
  #define GASNETI_CHECKINIT()    gasneti_checkinit()
  #define GASNETI_CHECKATTACH()  gasneti_checkattach()
#else
  #define GASNETI_CHECKINIT()
  #define GASNETI_CHECKATTACH()
#endif

/* Blocking functions */
extern int gasneti_wait_mode; /* current waitmode hint */
#define GASNETI_WAITHOOK() do {                                       \
    if (gasneti_wait_mode != GASNET_WAIT_SPIN) gasneti_sched_yield(); \
    gasneti_spinloop_hint();                                          \
  } while (0)

/* busy-waits, with no implicit polling (cnd should include an embedded poll)
   differs from GASNET_BLOCKUNTIL because it may be waiting for an event
     caused by the receipt of a non-AM message
 */
#ifndef gasneti_waitwhile
#define gasneti_waitwhile(cnd) while (cnd) GASNETI_WAITHOOK()
#endif
#define gasneti_waituntil(cnd) gasneti_waitwhile(!(cnd)) 

/* busy-wait, with implicit polling */
#ifndef gasneti_pollwhile
#define gasneti_pollwhile(cnd) do { \
    if (!(cnd)) break;              \
    gasnet_AMPoll();                \
    while (cnd) {                   \
      GASNETI_WAITHOOK();           \
      gasnet_AMPoll();              \
    }                               \
  } while (0)
#endif
#define gasneti_polluntil(cnd) gasneti_pollwhile(!(cnd)) 

/* conduits may replace the following types, 
   but they should at least include all the following fields */
#ifndef GASNETI_MEMVECLIST_STATS_T
  typedef struct {
    size_t minsz;
    size_t maxsz;
    uintptr_t totalsz;
    void *minaddr;
    void *maxaddr;
  } gasneti_memveclist_stats_t;
#endif

#ifndef GASNETI_ADDRLIST_STATS_T
  typedef struct {
    void *minaddr;
    void *maxaddr;
  } gasneti_addrlist_stats_t;
#endif

/* stats needed by the VIS reference implementation */
#ifndef GASNETI_REFVIS_STATS
  #define GASNETI_REFVIS_STATS(CNT,VAL,TIME) \
        CNT(C, PUTV_REF_INDIV, cnt)          \
        CNT(C, GETV_REF_INDIV, cnt)          \
        CNT(C, PUTI_REF_INDIV, cnt)          \
        CNT(C, GETI_REF_INDIV, cnt)          \
        CNT(C, PUTI_REF_VECTOR, cnt)         \
        CNT(C, GETI_REF_VECTOR, cnt)         \
        CNT(C, PUTS_REF_INDIV, cnt)          \
        CNT(C, GETS_REF_INDIV, cnt)          \
        CNT(C, PUTS_REF_VECTOR, cnt)         \
        CNT(C, GETS_REF_VECTOR, cnt)         \
        CNT(C, PUTS_REF_INDEXED, cnt)        \
        CNT(C, GETS_REF_INDEXED, cnt)
#endif

/* ------------------------------------------------------------------------------------ */
/* Error checking system mutexes -
     wrapper around pthread mutexes that provides extra support for 
     error checking when GASNET_DEBUG is defined
   gasneti_mutex_lock(&lock)/gasneti_mutex_unlock(&lock) - 
     lock and unlock (checks for recursive locking errors)
   gasneti_mutex_trylock(&lock) - 
     non-blocking trylock - returns EBUSY on failure, 0 on success
   gasneti_mutex_assertlocked(&lock)/gasneti_mutex_assertunlocked(&lock) - 
     allow functions to assert a given lock is held / not held by the current thread
  
   -DGASNETI_USE_TRUE_MUTEXES=1 will force gasneti_mutex_t to always
    use true locking (even under GASNET_SEQ or GASNET_PARSYNC config)
*/
#if GASNET_PAR || GASNETI_CONDUIT_THREADS
  /* need to use true locking if we have concurrent calls from multiple client threads 
     or if conduit has private threads that can run handlers */
  #define GASNETI_USE_TRUE_MUTEXES 1 
#elif !defined(GASNETI_USE_TRUE_MUTEXES)
  #define GASNETI_USE_TRUE_MUTEXES 0
#endif

#if GASNET_DEBUG
  #define GASNETI_MUTEX_NOOWNER       -1
  #ifndef GASNETI_THREADIDQUERY
    /* allow conduit override of thread-id query */
    #if GASNETI_USE_TRUE_MUTEXES
      #define GASNETI_THREADIDQUERY()   ((uintptr_t)pthread_self())
    #else
      #define GASNETI_THREADIDQUERY()   (0)
    #endif
  #endif
  #if GASNETI_USE_TRUE_MUTEXES
    #include <pthread.h>
    typedef struct {
      pthread_mutex_t lock;
      uintptr_t owner;
    } gasneti_mutex_t;
    #if defined(PTHREAD_ADAPTIVE_MUTEX_INITIALIZER_NP)
      /* These are faster, though less "featureful" than the default
       * mutexes on linuxthreads implementations which offer them.
       */
      #define GASNETI_MUTEX_INITIALIZER { PTHREAD_ADAPTIVE_MUTEX_INITIALIZER_NP, (uintptr_t)GASNETI_MUTEX_NOOWNER }
    #else
      #define GASNETI_MUTEX_INITIALIZER { PTHREAD_MUTEX_INITIALIZER, (uintptr_t)GASNETI_MUTEX_NOOWNER }
    #endif
    #define gasneti_mutex_lock(pl) do {                                                   \
              int retval;                                                                 \
              gasneti_assert((pl)->owner != GASNETI_THREADIDQUERY());                     \
              retval = pthread_mutex_lock(&((pl)->lock));                                 \
              if (retval) gasneti_fatalerror("pthread_mutex_lock()=%s",strerror(retval)); \
              gasneti_assert((pl)->owner == (uintptr_t)GASNETI_MUTEX_NOOWNER);            \
              (pl)->owner = GASNETI_THREADIDQUERY();                                      \
            } while (0)
    GASNET_INLINE_MODIFIER(gasneti_mutex_trylock)
    int gasneti_mutex_trylock(gasneti_mutex_t *pl) {
              int retval;
              gasneti_assert((pl)->owner != GASNETI_THREADIDQUERY());
              retval = pthread_mutex_trylock(&((pl)->lock));
              if (retval == EBUSY) return EBUSY;
              if (retval) gasneti_fatalerror("pthread_mutex_trylock()=%s",strerror(retval));
              gasneti_assert((pl)->owner == (uintptr_t)GASNETI_MUTEX_NOOWNER);
              (pl)->owner = GASNETI_THREADIDQUERY();
              return 0;
    }
    #define gasneti_mutex_unlock(pl) do {                                                   \
              int retval;                                                                   \
              gasneti_assert((pl)->owner == GASNETI_THREADIDQUERY());                       \
              (pl)->owner = (uintptr_t)GASNETI_MUTEX_NOOWNER;                               \
              retval = pthread_mutex_unlock(&((pl)->lock));                                 \
              if (retval) gasneti_fatalerror("pthread_mutex_unlock()=%s",strerror(retval)); \
            } while (0)
    #define gasneti_mutex_init(pl) do {                                                   \
              int retval = pthread_mutex_init(&((pl)->lock),NULL);                        \
              if (retval) gasneti_fatalerror("pthread_mutex_init()=%s",strerror(retval)); \
              (pl)->owner = (uintptr_t)GASNETI_MUTEX_NOOWNER;                             \
            } while (0)
    #define gasneti_mutex_destroy(pl) do {                                                   \
              int retval = pthread_mutex_destroy(&((pl)->lock));                             \
              if (retval) gasneti_fatalerror("pthread_mutex_destroy()=%s",strerror(retval)); \
            } while (0)
  #else
    typedef struct {
      volatile int owner;
    } gasneti_mutex_t;
    #define GASNETI_MUTEX_INITIALIZER   { GASNETI_MUTEX_NOOWNER }
    #define gasneti_mutex_lock(pl) do {                             \
              gasneti_assert((pl)->owner == GASNETI_MUTEX_NOOWNER); \
              (pl)->owner = GASNETI_THREADIDQUERY();                \
            } while (0)
    GASNET_INLINE_MODIFIER(gasneti_mutex_trylock)
    int gasneti_mutex_trylock(gasneti_mutex_t *pl) {
              gasneti_assert((pl)->owner == GASNETI_MUTEX_NOOWNER);
              (pl)->owner = GASNETI_THREADIDQUERY();
              return 0;
    }
    #define gasneti_mutex_unlock(pl) do {                             \
              gasneti_assert((pl)->owner == GASNETI_THREADIDQUERY()); \
              (pl)->owner = GASNETI_MUTEX_NOOWNER;                    \
            } while (0)
    #define gasneti_mutex_init(pl) do {                       \
              (pl)->owner = GASNETI_MUTEX_NOOWNER;            \
            } while (0)
    #define gasneti_mutex_destroy(pl)
  #endif
  #define gasneti_mutex_assertlocked(pl)    gasneti_assert((pl)->owner == GASNETI_THREADIDQUERY())
  #define gasneti_mutex_assertunlocked(pl)  gasneti_assert((pl)->owner != GASNETI_THREADIDQUERY())
#else
  #if GASNETI_USE_TRUE_MUTEXES
    #include <pthread.h>
    typedef pthread_mutex_t           gasneti_mutex_t;
    #if defined(PTHREAD_ADAPTIVE_MUTEX_INITIALIZER_NP)
      /* These are faster, though less "featureful" than the default
       * mutexes on linuxthreads implementations which offer them.
       */
      #define GASNETI_MUTEX_INITIALIZER PTHREAD_ADAPTIVE_MUTEX_INITIALIZER_NP
    #else
      #define GASNETI_MUTEX_INITIALIZER PTHREAD_MUTEX_INITIALIZER
    #endif
    #define gasneti_mutex_lock(pl)      pthread_mutex_lock(pl)
    #define gasneti_mutex_trylock(pl)   pthread_mutex_trylock(pl)
    #define gasneti_mutex_unlock(pl)    pthread_mutex_unlock(pl)
    #define gasneti_mutex_init(pl)      pthread_mutex_init((pl),NULL)
    #define gasneti_mutex_destroy(pl)   pthread_mutex_destroy(pl)
  #else
    typedef char           gasneti_mutex_t;
    #define GASNETI_MUTEX_INITIALIZER '\0'
    #define gasneti_mutex_lock(pl)    
    #define gasneti_mutex_trylock(pl) 0
    #define gasneti_mutex_unlock(pl)  
    #define gasneti_mutex_init(pl)
    #define gasneti_mutex_destroy(pl)
  #endif
  #define gasneti_mutex_assertlocked(pl)
  #define gasneti_mutex_assertunlocked(pl)
#endif
/* ------------------------------------------------------------------------------------ */


/* high-performance timer library */
#include <gasnet_timer.h>

/* tracing utilities */
#include <gasnet_trace.h>

/* ------------------------------------------------------------------------------------ */

END_EXTERNC

#endif

/*  $Archive:: /Ti/GASNet/gasnet_help.h                                   $
 *     $Date: 2004/07/15 02:01:09 $
 * $Revision: 1.22.4.4 $
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

/* extern versions of gasneti_{malloc,free,strdup} for use in public headers */
extern void *gasneti_extern_malloc(size_t sz);
extern void gasneti_extern_free(void *p);
extern char *gasneti_extern_strdup(const char *s);

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

/* gasneti_assert_zeroret(), gasneti_assert_nzeroret():
 * evaluate an expression (always), and in debug mode additionally 
 * assert that it returns zero or non-zero
 * useful for making system calls and checking the result
 */
#if GASNET_DEBUG
  #define gasneti_assert_zeroret(op) do {                                     \
    int retval = op;                                                          \
    if_pf(retval) gasneti_fatalerror(#op": %s(%i)",strerror(retval), retval); \
  } while (0)
  #define gasneti_assert_nzeroret(op) do {                                     \
    int retval = op;                                                           \
    if_pf(!retval) gasneti_fatalerror(#op": %s(%i)",strerror(retval), retval); \
  } while (0)
#else
  #define gasneti_assert_zeroret(op)  op
  #define gasneti_assert_nzeroret(op) op
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

/* stats needed by the COLL reference implementation */
#ifndef GASNETI_REFCOLL_STATS
  #define GASNETI_REFCOLL_STATS(CNT,VAL,TIME) \
        VAL(X, COLL_TRY_SYNC, success)        \
        VAL(X, COLL_TRY_SYNC_ALL, success)    \
        VAL(X, COLL_TRY_SYNC_SOME, success)   \
        TIME(X, COLL_WAIT_SYNC, waittime)     \
        TIME(X, COLL_WAIT_SYNC_ALL, waittime) \
        TIME(X, COLL_WAIT_SYNC_SOME, waittime)\
        VAL(W, COLL_BROADCAST, sz)            \
        VAL(W, COLL_BROADCAST_NB, sz)         \
        VAL(W, COLL_BROADCAST_M, sz)          \
        VAL(W, COLL_BROADCAST_M_NB, sz)       \
        VAL(W, COLL_SCATTER, sz)              \
        VAL(W, COLL_SCATTER_NB, sz)           \
        VAL(W, COLL_SCATTER_M, sz)            \
        VAL(W, COLL_SCATTER_M_NB, sz)         \
        VAL(W, COLL_GATHER, sz)               \
        VAL(W, COLL_GATHER_NB, sz)            \
        VAL(W, COLL_GATHER_M, sz)             \
        VAL(W, COLL_GATHER_M_NB, sz)          \
        VAL(W, COLL_GATHER_ALL, sz)           \
        VAL(W, COLL_GATHER_ALL_NB, sz)        \
        VAL(W, COLL_GATHER_ALL_M, sz)         \
        VAL(W, COLL_GATHER_ALL_M_NB, sz)      \
        VAL(W, COLL_EXCHANGE, sz)             \
        VAL(W, COLL_EXCHANGE_NB, sz)          \
        VAL(W, COLL_EXCHANGE_M, sz)           \
        VAL(W, COLL_EXCHANGE_M_NB, sz)
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
    #define gasneti_mutex_lock(pl) do {                                        \
              int retval;                                                      \
              gasneti_assert((pl)->owner != GASNETI_THREADIDQUERY());          \
              gasneti_assert_zeroret(pthread_mutex_lock(&((pl)->lock)));       \
              gasneti_assert((pl)->owner == (uintptr_t)GASNETI_MUTEX_NOOWNER); \
              (pl)->owner = GASNETI_THREADIDQUERY();                           \
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
    #define gasneti_mutex_unlock(pl) do {                                  \
              int retval;                                                  \
              gasneti_assert((pl)->owner == GASNETI_THREADIDQUERY());      \
              (pl)->owner = (uintptr_t)GASNETI_MUTEX_NOOWNER;              \
              gasneti_assert_zeroret(pthread_mutex_unlock(&((pl)->lock))); \
            } while (0)
    #define gasneti_mutex_init(pl) do {                                       \
              gasneti_assert_zeroret(pthread_mutex_init(&((pl)->lock),NULL)); \
              (pl)->owner = (uintptr_t)GASNETI_MUTEX_NOOWNER;                 \
            } while (0)
    #define gasneti_mutex_destroy(pl) \
              gasneti_assert_zeroret(pthread_mutex_destroy(&((pl)->lock)))
  #else /* GASNET_DEBUG non-pthread (error-check-only) mutexes */
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
#else /* non-debug mutexes */
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
    #define gasneti_mutex_lock(pl)    ((void)0)
    #define gasneti_mutex_trylock(pl) 0
    #define gasneti_mutex_unlock(pl)  ((void)0)
    #define gasneti_mutex_init(pl)    ((void)0)
    #define gasneti_mutex_destroy(pl) ((void)0)
  #endif
  #define gasneti_mutex_assertlocked(pl)    ((void)0)
  #define gasneti_mutex_assertunlocked(pl)  ((void)0)
#endif

/* gasneti_cond_t Condition variables - 
   Provides pthread_cond-like functionality, with error checking
  GASNETI_COND_INITIALIZER - value to statically initialize a gasneti_cond_t
  gasneti_cond_init(gasneti_cond_t *pc) - dynamically initialize a gasneti_cond_t   
  gasneti_cond_destroy(gasneti_cond_t *pc) - reclaim a gasneti_cond_t
  gasneti_cond_signal(gasneti_cond_t *pc) - 
    signal at least one waiter on a gasneti_cond_t, while holding the associated mutex
  gasneti_cond_broadcast(gasneti_cond_t *pc) - 
    signal all current waiters on a gasneti_cond_t, while holding the associated mutex
  gasneti_cond_wait(gasneti_cond_t *pc, gasneti_mutex_t *pl) - 
    release gasneti_mutex_t pl (which must be held) and block WITHOUT POLLING 
    until gasneti_cond_t pc is signalled by another thread, or until the system
    decides to wake this thread for no good reason (which it may or may not do).
    Upon wakeup for any reason, the mutex will be reacquired before returning.

    It's an error to wait if there is only one thread, and can easily lead to 
    deadlock if the last thread goes to sleep. No thread may call wait unless it
    can guarantee that (A) some other thread is still polling and (B) some other
    thread will eventually signal it to wake up. The system may or may not also 
    randomly signal threads to wake up for no good reason, so upon awaking the thread
    MUST verify using its own means that the condition it was waiting for 
    has actually been signalled (ie that the client-level "outer" condition has been set).

    In order to prevent races leading to missed signals and deadlock, signaling
    threads must always hold the associated mutex while signaling, and ensure the
    outer condition is set *before* releasing the mutex. Additionally, all waiters
    must check the outer condition *after* acquiring the same mutex and *before*
    calling wait (which atomically releases the lock and puts the thread to sleep).
*/

#if GASNETI_USE_TRUE_MUTEXES
  typedef pthread_cond_t            gasneti_cond_t;

  #define GASNETI_COND_INITIALIZER    PTHREAD_COND_INITIALIZER
  #define gasneti_cond_init(pc)       gasneti_assert_zeroret(pthread_cond_init(pc))
  #define gasneti_cond_destroy(pc)    gasneti_assert_zeroret(pthread_cond_destroy(pc))
  #define gasneti_cond_signal(pc)     gasneti_assert_zeroret(pthread_cond_signal(pc))
  #define gasneti_cond_broadcast(pc)  gasneti_assert_zeroret(pthread_cond_broadcast(pc))
  #if GASNET_DEBUG
    #define gasneti_cond_wait(pc,pl)  do {                    \
      gasneti_assert((pl)->owner == GASNETI_THREADIDQUERY()); \
      (pl)->owner = GASNETI_MUTEX_NOOWNER;                    \
      gasneti_assert_zeroret(pthread_cond_wait(pc, pl));      \
      gasneti_assert((pl)->owner == GASNETI_MUTEX_NOOWNER);   \
      (pl)->owner = GASNETI_THREADIDQUERY();                  \
    } while (0)
  #else
    #define gasneti_cond_wait(pc,pl)  gasneti_assert_zeroret(pthread_cond_wait(pc, pl))
  #endif
#else
  typedef char           gasneti_cond_t;
  #define GASNETI_COND_INITIALIZER  '\0'
  #define gasneti_cond_init(pc)       ((void)0)
  #define gasneti_cond_destroy(pc)    ((void)0)
  #define gasneti_cond_signal(pc)     ((void)0)
  #define gasneti_cond_broadcast(pc)  ((void)0)
  #define gasneti_cond_wait(pc,pl) \
      gasneti_fatalerror("There's only one thread: waiting on condition variable => deadlock")
#endif
/* ------------------------------------------------------------------------------------ */

/* high-performance timer library */
#include <gasnet_timer.h>

/* tracing utilities */
#include <gasnet_trace.h>

/* ------------------------------------------------------------------------------------ */

END_EXTERNC

#endif

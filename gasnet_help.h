/*   $Source: /Users/kamil/work/gasnet-cvs2/gasnet/gasnet_help.h,v $
 *     $Date: 2004/10/19 04:41:49 $
 * $Revision: 1.40 $
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
#include <gasnet_atomicops.h>

BEGIN_EXTERNC

extern void gasneti_fatalerror(const char *msg, ...) GASNET_NORETURN __attribute__((__format__ (__printf__, 1, 2)));
/* internal GASNet environment query function
 * uses the gasneti_globalEnv if available or regular getenv otherwise
 * legal to call before gasnet_init, but may malfunction if
 * the conduit has not yet established the contents of the environment
 */
extern char *gasneti_getenv(const char *keyname);

/* internal conduit query for a system string parameter
   if user has set value the return value indicates their selection
   if value is not set, the provided default value is returned
   call is reported to the console in verbose-environment mode,
   so this function should never be called more than once per key
   legal to call before gasnet_init, but may malfunction if
   the conduit has not yet established the contents of the environment
 */
extern char *gasneti_getenv_withdefault(const char *keyname, const char *defaultval);

/* internal conduit query for a system yes/no parameter
   if user has set value to 'Y|YES|y|yes|1' or 'N|n|NO|no|0', 
   the return value indicates their selection
   if value is not set, the provided default value is returned
   same restrictions on gasneti_getenv_withdefault also apply
 */
extern int gasneti_getenv_yesno_withdefault(const char *keyname, int defaultval);

/* set/unset an environment variable, for the local process ONLY */
extern void gasneti_setenv(const char *key, const char *value);
extern void gasneti_unsetenv(const char *key);

/* extern versions of gasneti_{malloc,free,strdup} for use in public headers */
extern void *gasneti_extern_malloc(size_t sz);
extern void gasneti_extern_free(void *p);
extern char *gasneti_extern_strdup(const char *s);

#if defined(__GNUC__) || defined(__FUNCTION__)
  #define GASNETI_CURRENT_FUNCTION __FUNCTION__
#elif defined(HAVE_FUNC) && !defined(__cplusplus)
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
/* Conditionally compiled memory barriers -

   gasneti_sync_{reads,writes,mem} are like gasneti_local_{rmb,wmb,mb} except that when
   not using threads we want them to compile away to nothing, and when compiling for
   threads on a uniprocessor we want only a compiler optimization barrier
*/

#ifndef gasneti_sync_writes
  #if !GASNETI_THREADS
    #define gasneti_sync_writes() /* NO-OP */
  #elif GASNETI_UNI_BUILD
    #define gasneti_sync_writes() gasneti_compiler_fence()
  #else
    #define gasneti_sync_writes() gasneti_local_wmb()
  #endif
#endif

#ifndef gasneti_sync_reads
  #if !GASNETI_THREADS
    #define gasneti_sync_reads() /* NO-OP */
  #elif GASNETI_UNI_BUILD
    #define gasneti_sync_reads() gasneti_compiler_fence()
  #else
    #define gasneti_sync_reads() gasneti_local_rmb()
  #endif
#endif

#ifndef gasneti_sync_mem
  #if !GASNETI_THREADS
    #define gasneti_sync_mem() /* NO-OP */
  #elif GASNETI_UNI_BUILD
    #define gasneti_sync_mem() gasneti_compiler_fence()
  #else
    #define gasneti_sync_mem() gasneti_local_mb()
  #endif
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
    #define gasneti_cond_wait(pc,pl)  do {                          \
      gasneti_assert((pl)->owner == GASNETI_THREADIDQUERY());       \
      (pl)->owner = GASNETI_MUTEX_NOOWNER;                          \
      gasneti_assert_zeroret(pthread_cond_wait(pc, &((pl)->lock))); \
      gasneti_assert((pl)->owner == GASNETI_MUTEX_NOOWNER);         \
      (pl)->owner = GASNETI_THREADIDQUERY();                        \
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
/* Wrappers for pthread getspecific data 

  Must be declared as:
    gasneti_threadkey_t mykey = GASNETI_THREADKEY_INITIALIZER;
  and then can be used as:
    void *val = gasneti_threadkey_get(mykey);
    gasneti_threadkey_set(mykey,val);
  no initialization is required (happens automatically on first access).

  Initialization can optionally be performed using:
    gasneti_threadkey_init(&mykey);
  which then allows subsequent calls to:
    void *val = gasneti_threadkey_get_noinit(mykey);
    gasneti_threadkey_set_noinit(mykey,val);
  these save a branch by avoiding the initialization check.
  gasneti_threadkey_init is permitted to be called multiple times and
  from multiple threads - calls after the first one will be ignored.
*/
#define GASNETI_THREADKEY_MAGIC 0xFF00ABCDEF573921ULL

typedef struct { 
  uint64_t magic;
  gasneti_mutex_t initmutex;
  int isinit;
  #if GASNETI_THREADS
    pthread_key_t value;
  #else
    void *value;
  #endif
} gasneti_threadkey_t;

#define GASNETI_THREADKEY_INITIALIZER \
  { GASNETI_THREADKEY_MAGIC, GASNETI_MUTEX_INITIALIZER, 0 /* value field left NULL */ }

#define _gasneti_threadkey_check(key, requireinit)         \
 ( gasneti_assert((key).magic == GASNETI_THREADKEY_MAGIC), \
   (requireinit ? gasneti_assert((key).isinit) : ((void)0)))

#if GASNETI_THREADS
  #define _gasneti_threadkey_init(pvalue) \
    gasneti_assert_zeroret(pthread_key_create((pvalue),NULL));
  #define gasneti_threadkey_get_noinit(key) \
    ( _gasneti_threadkey_check((key), 1),   \
      pthread_getspecific((key).value) )
  #define gasneti_threadkey_set_noinit(key, newvalue) do {                \
    _gasneti_threadkey_check((key), 1);                                   \
    gasneti_assert_zeroret(pthread_setspecific((key).value, (newvalue))); \
  } while (0)
#else
  #define _gasneti_threadkey_init(pvalue) ((void)0)
  #define gasneti_threadkey_get_noinit(key) \
    (_gasneti_threadkey_check((key), 1), (key).value)
  #define gasneti_threadkey_set_noinit(key, newvalue) do { \
    _gasneti_threadkey_check((key), 1);                    \
    (key).value = (newvalue);                              \
  } while (0)
#endif
  
/* not inlined, to avoid inserting overhead for an uncommon path */
static void gasneti_threadkey_init(gasneti_threadkey_t *pkey) {
  _gasneti_threadkey_check(*pkey, 0);
  gasneti_mutex_lock(&(pkey->initmutex));
    if (pkey->isinit == 0) {
      _gasneti_threadkey_init(&(pkey->value));
      gasneti_local_wmb();
      pkey->isinit = 1;
    }
  gasneti_mutex_unlock(&(pkey->initmutex));
}
  
#define gasneti_threadkey_get(key)       \
  ( _gasneti_threadkey_check(key, 0),    \
    ( PREDICT_FALSE((key).isinit == 0) ? \
      gasneti_threadkey_init(&(key)) :   \
      ((void)0) ),                       \
    gasneti_threadkey_get_noinit(key) )

#define gasneti_threadkey_set(key,newvalue) do { \
    _gasneti_threadkey_check(key, 0);            \
    if_pf((key).isinit == 0)                     \
      gasneti_threadkey_init(&(key));            \
    gasneti_threadkey_set_noinit(key, newvalue); \
  } while (0)

/* ------------------------------------------------------------------------------------ */
#ifndef GASNETI_GASNETI_AMPOLL
  /*
   gasnet_AMPoll() - public poll function called by the client, throttled and traced 
                     should not be called from within GASNet (so we only trace directly user-initiated calls)
   gasneti_AMPoll() - called internally by GASNet, provides throttling (if enabled), but no tracing
   gasnetc_AMPoll() - conduit AM dispatcher, should only be called from gasneti_AMPoll()
   */
  #ifndef GASNETI_GASNETC_AMPOLL
    extern int gasnetc_AMPoll();
  #endif

  #if GASNETI_THROTTLE_FEATURE_ENABLED && (GASNET_PAR || GASNETI_CONDUIT_THREADS)
    #define GASNETI_THROTTLE_POLLERS 1
  #else
    #define GASNETI_THROTTLE_POLLERS 0
  #endif

  /* threads who need a network lock in order to send a message make 
     matched calls to gasneti_suspend/resume_spinpollers to help them 
     get the lock. They should not AMPoll while this suspend is in effect.
     The following debugging assertions detect violations of these rules.
  */ 
  #if GASNET_DEBUG && GASNETI_THREADS
    extern gasneti_threadkey_t gasneti_throttledebug_key;

    #define gasneti_AMPoll_spinpollers_check()          \
      /* assert this thread hasn't already suspended */ \
      gasneti_assert((int)(intptr_t)gasneti_threadkey_get(gasneti_throttledebug_key) == 0)
    #define gasneti_suspend_spinpollers_check() do {                                        \
      /* assert this thread hasn't already suspended */                                     \
      gasneti_assert((int)(intptr_t)gasneti_threadkey_get(gasneti_throttledebug_key) == 0); \
      gasneti_threadkey_set(gasneti_throttledebug_key, (void *)(intptr_t)1);                \
    } while(0)
    #define gasneti_resume_spinpollers_check() do {                                         \
      /* assert this thread previously suspended */                                         \
      gasneti_assert((int)(intptr_t)gasneti_threadkey_get(gasneti_throttledebug_key) == 1); \
      gasneti_threadkey_set(gasneti_throttledebug_key, (void *)(intptr_t)0);                \
    } while(0)
  #elif GASNET_DEBUG
    extern int gasneti_throttledebug_cnt;

    #define gasneti_AMPoll_spinpollers_check()          \
      /* assert this thread hasn't already suspended */ \
      gasneti_assert(gasneti_throttledebug_cnt == 0)
    #define gasneti_suspend_spinpollers_check() do {    \
      /* assert this thread hasn't already suspended */ \
      gasneti_assert(gasneti_throttledebug_cnt == 0);   \
      gasneti_throttledebug_cnt = 1;                    \
    } while(0)
    #define gasneti_resume_spinpollers_check() do {   \
      /* assert this thread previously suspended */   \
      gasneti_assert(gasneti_throttledebug_cnt == 1); \
      gasneti_throttledebug_cnt = 0;                  \
    } while(0)
  #else
    #define gasneti_AMPoll_spinpollers_check()  ((void)0)
    #define gasneti_suspend_spinpollers_check() ((void)0)
    #define gasneti_resume_spinpollers_check()  ((void)0)
  #endif

  #if !GASNETI_THROTTLE_POLLERS 
    #define gasneti_AMPoll() (gasneti_AMPoll_spinpollers_check(), gasnetc_AMPoll())
    #define gasneti_suspend_spinpollers() gasneti_suspend_spinpollers_check()
    #define gasneti_resume_spinpollers()  gasneti_resume_spinpollers_check()
  #else
    /* AMPoll with throttling, to reduce lock contention in the network:
       poll if and only if no other thread appears to already be spin-polling,
       and no thread is attempting to use the network for sending 
       Design goals (in rough order of importance):
        - when one or more threads need to send, all spin-pollers should get 
          out of the way (but continue checking their completion condition)
        - only one thread should be spin-polling at a time, to prevent 
          lock contention and cache thrashing between spin-pollers
        - manage AMPoll calls internal to GASNet (including those from polluntil) 
          and explicit client AMPoll calls
        - allow concurrent handler execution - if the spin poller recvs an AM, 
          it should release another spin poller before invoking the handler
        - the single spin-poller should not need to pay locking overheads in the loop
    */
    extern gasneti_atomic_t gasneti_throttle_haveusefulwork;
    extern gasneti_mutex_t gasneti_throttle_spinpoller;

    #define gasneti_suspend_spinpollers() do {                      \
        gasneti_suspend_spinpollers_check();                        \
        gasneti_atomic_increment(&gasneti_throttle_haveusefulwork); \
    } while (0)
    #define gasneti_resume_spinpollers() do {                       \
        gasneti_resume_spinpollers_check();                         \
        gasneti_atomic_decrement(&gasneti_throttle_haveusefulwork); \
    } while (0)

    /* and finally, the throttled poll implementation */
    GASNET_INLINE_MODIFIER(gasneti_AMPoll)
    int gasneti_AMPoll() {
       int retval;
       gasneti_AMPoll_spinpollers_check();
       if (gasneti_atomic_read(&gasneti_throttle_haveusefulwork) > 0) 
         return GASNET_OK; /* another thread sending - skip the poll */
       if (gasneti_mutex_trylock(&gasneti_throttle_spinpoller) != 0)
         return GASNET_OK; /* another thread spin-polling - skip the poll */
       retval = gasnetc_AMPoll();
       gasneti_mutex_unlock(&gasneti_throttle_spinpoller);
       return retval;
    }
  #endif
#endif
  
/* Blocking functions
 * Note the _rmb at the end loop of each is required to ensure that subsequent
 * reads will not observe values that were prefeteched or are otherwise out
 * of date.
 */
extern int gasneti_wait_mode; /* current waitmode hint */
#define GASNETI_WAITHOOK() do {                                       \
    if (gasneti_wait_mode != GASNET_WAIT_SPIN) gasneti_sched_yield(); \
    /* prevent optimizer from hoisting the condition check out of */  \
    /* the enclosing spin loop - this is our way of telling the */    \
    /* optimizer "the wholse world could change here" */              \
    gasneti_compiler_fence();                                         \
    gasneti_spinloop_hint();                                          \
  } while (0)

/* busy-waits, with no implicit polling (cnd should include an embedded poll)
   differs from GASNET_BLOCKUNTIL because it may be waiting for an event
     caused by the receipt of a non-AM message
 */
#ifndef gasneti_waitwhile
  #define gasneti_waitwhile(cnd) do { \
    while (cnd) GASNETI_WAITHOOK();   \
    gasneti_local_rmb();              \
  } while (0)
#endif
#define gasneti_waituntil(cnd) gasneti_waitwhile(!(cnd)) 

/* busy-wait, with implicit polling */
/* Note no poll if the condition is already satisfied */
#ifndef gasneti_pollwhile
  #define gasneti_pollwhile(cnd) do { \
    if (cnd) {                        \
      gasneti_AMPoll();               \
      while (cnd) {                   \
        GASNETI_WAITHOOK();           \
        gasneti_AMPoll();             \
      }                               \
    }                                 \
    gasneti_local_rmb();              \
  } while (0)
#endif
#define gasneti_polluntil(cnd) gasneti_pollwhile(!(cnd)) 

/* ------------------------------------------------------------------------------------ */

/* high-performance timer library */
#include <gasnet_timer.h>

/* tracing utilities */
#include <gasnet_trace.h>

/* ------------------------------------------------------------------------------------ */
  /* default implementation of public gasnet_AMPoll */
#ifndef GASNETI_GASNET_AMPOLL
  /* GASNet client calls gasnet_AMPoll(), which throttles and traces */
  GASNET_INLINE_MODIFIER(gasnet_AMPoll)
  int gasnet_AMPoll() {
    GASNETI_TRACE_EVENT(I, AMPOLL);
    return gasneti_AMPoll();
  }
#endif

#ifndef _GASNET_GETENV
#define _GASNET_GETENV
  GASNET_INLINE_MODIFIER(gasnet_getenv)
  char *gasnet_getenv(const char *s) {
    GASNETI_CHECKINIT();
    return gasneti_getenv(s);
  }
#endif

/* ------------------------------------------------------------------------------------ */

END_EXTERNC

#endif

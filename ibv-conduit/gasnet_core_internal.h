/*   $Source: /Users/kamil/work/gasnet-cvs2/gasnet/ibv-conduit/gasnet_core_internal.h,v $
 *     $Date: 2006/03/09 00:33:07 $
 * $Revision: 1.123 $
 * Description: GASNet vapi conduit header for internal definitions in Core API
 * Copyright 2002, Dan Bonachea <bonachea@cs.berkeley.edu>
 * Terms of use are as specified in license.txt
 */

#ifndef _GASNET_CORE_INTERNAL_H
#define _GASNET_CORE_INTERNAL_H

#include <stddef.h>	/* for offsetof() */

#include <gasnet_internal.h>
#include <firehose.h>

#include <ssh-spawner/gasnet_bootstrap_internal.h>
#if HAVE_MPI_SPAWNER
  #include <mpi-spawner/gasnet_bootstrap_internal.h>
#endif

#include <vapi.h>
#include <evapi.h>
#include <vapi_common.h>

#if HAVE_MMAP
  #include <sys/mman.h> /* For MAP_FAILED */
#endif

/*  whether or not to use spin-locking for HSL's */
#define GASNETC_HSL_SPINLOCK 1

#define GASNETC_CACHE_PAD(SZ) (GASNETC_ALIGNUP(SZ,GASNETI_CACHE_LINE_BYTES)-(SZ))

/* check (even in optimized build) for VAPI errors */
#define GASNETC_VAPI_CHECK(vstat,msg) \
  if_pf ((vstat) != VAPI_OK) \
    { gasneti_fatalerror("Unexpected error %s %s",VAPI_strerror_sym(vstat),(msg)); }

/* check for exit in progress */
extern gasneti_atomic_t gasnetc_exit_running;
#define GASNETC_IS_EXITING() gasneti_atomic_read(&gasnetc_exit_running)

/* ------------------------------------------------------------------------------------ */
#define GASNETC_HANDLER_BASE  1 /* reserve 1-63 for the core API */
#define _hidx_gasnetc_auxseg_reqh             (GASNETC_HANDLER_BASE+0)
/* add new core API handlers here and to the bottom of gasnet_core.c */

/* System-category handlers.
 * These form a separate AM handler space and are available even before _attach()
 */
#define _hidx_gasnetc_SYS_ack             0
#define _hidx_gasnetc_SYS_exit_role_req   1
#define _hidx_gasnetc_SYS_exit_role_rep   2
#define _hidx_gasnetc_SYS_exit_req        3
#define _hidx_gasnetc_SYS_exit_rep        4
#define _hidx_gasnetc_SYS_init_ping       5

/* ------------------------------------------------------------------------------------ */

typedef struct {
#if GASNETI_STATS_OR_TRACE
  gasneti_stattime_t	stamp;
#endif
  gasnet_handlerarg_t	args[GASNETC_MAX_ARGS];	
} gasnetc_shortmsg_t;
#define GASNETC_MSG_SHORT_ARGSEND(nargs) offsetof(gasnetc_shortmsg_t,args[(unsigned int)nargs])

typedef struct {
#if GASNETI_STATS_OR_TRACE
  gasneti_stattime_t	stamp;
#endif
  uint32_t		nBytes;	/* 16 bits would be sufficient if we ever need the space */
  gasnet_handlerarg_t	args[GASNETC_MAX_ARGS];	
} gasnetc_medmsg_t;
#define GASNETC_MSG_MED_ARGSEND(nargs) /* Note 8-byte alignment for payload */ \
		GASNETI_ALIGNUP(offsetof(gasnetc_medmsg_t,args[(unsigned int)nargs]), 8)
#define GASNETC_MSG_MED_DATA(msg,nargs) \
		((void *)((uintptr_t)(msg) + GASNETC_MSG_MED_ARGSEND(nargs)))

typedef struct {
#if GASNETI_STATS_OR_TRACE
  gasneti_stattime_t	stamp;
#endif
  uintptr_t		destLoc;
  int32_t		nBytes;
  gasnet_handlerarg_t	args[GASNETC_MAX_ARGS];	
} gasnetc_longmsg_t;
#define GASNETC_MSG_LONG_ARGSEND(nargs)  offsetof(gasnetc_longmsg_t,args[(unsigned int)nargs])
#define GASNETC_MSG_LONG_DATA(msg,nargs) (void *)(&msg->longmsg.args[(unsigned int)nargs])

typedef union {
  uint8_t		raw[GASNETC_BUFSZ];
#if GASNETI_STATS_OR_TRACE
  gasneti_stattime_t	stamp;
#endif
  gasnetc_shortmsg_t	shortmsg;
  gasnetc_medmsg_t	medmsg;
  gasnetc_longmsg_t	longmsg;
} gasnetc_buffer_t;

typedef enum {
  gasnetc_Short=0,
  gasnetc_Medium=1,
  gasnetc_Long=2,
  gasnetc_System=3
} gasnetc_category_t;

/* ------------------------------------------------------------------------------------ */

#define GASNETC_MAX_NUMHANDLERS   256
typedef void (*gasnetc_handler_fn_t)();  /* prototype for handler function */
extern gasnetc_handler_fn_t gasnetc_handler[GASNETC_MAX_NUMHANDLERS];

/* ------------------------------------------------------------------------------------ */
typedef void (*gasnetc_sys_handler_fn_t)(gasnet_token_t token, gasnet_handlerarg_t *args, int numargs);
extern const gasnetc_sys_handler_fn_t gasnetc_sys_handler[GASNETC_MAX_NUMHANDLERS];

#define RUN_HANDLER_SYSTEM(phandlerfn, token, args, numargs) \
    if (phandlerfn != NULL) (*phandlerfn)(token, args, numargs)

#if GASNET_TRACE
  #define _GASNETC_TRACE_SYSTEM(name,dest,handler,numargs) do {                        \
    _GASNETI_TRACE_GATHERARGS(numargs);                                                \
    _GASNETI_STAT_EVENT(C,name);                                                       \
    GASNETI_TRACE_PRINTF(C,(#name": dest=%i handler=%i args:%s",dest,handler,argstr)); \
  } while(0)
  #define GASNETC_TRACE_SYSTEM_REQUEST(dest,handler,numargs) \
          _GASNETC_TRACE_SYSTEM(AMREQUEST_SYS,dest,handler,numargs)
  #define GASNETC_TRACE_SYSTEM_REPLY(dest,handler,numargs) \
          _GASNETC_TRACE_SYSTEM(AMREPLY_SYS,dest,handler,numargs)

  #define _GASNETC_TRACE_SYSTEM_HANDLER(name, handlerid, token, numargs, arghandle) do { \
    gasnet_node_t src;                                                                    \
    _GASNETI_TRACE_GATHERHANDLERARGS(numargs, arghandle);                                 \
    _GASNETI_STAT_EVENT(C,name);                                                          \
    if (gasnet_AMGetMsgSource(token,&src) != GASNET_OK)                                   \
	gasneti_fatalerror("gasnet_AMGetMsgSource() failed");                               \
    GASNETI_TRACE_PRINTF(C,(#name": src=%i handler=%i args:%s",                           \
      (int)src,(int)(handlerid),argstr));                                                 \
    GASNETI_TRACE_PRINTF(C,(#name": token: %s",                                           \
                      gasneti_formatdata(&token, sizeof(token))));                        \
    } while(0)
  #define GASNETC_TRACE_SYSTEM_REQHANDLER(handlerid, token, numargs, arghandle) \
         _GASNETC_TRACE_SYSTEM_HANDLER(AMREQUEST_SYS_HANDLER, handlerid, token, numargs, arghandle)
  #define GASNETC_TRACE_SYSTEM_REPHANDLER(handlerid, token, numargs, arghandle) \
         _GASNETC_TRACE_SYSTEM_HANDLER(AMREPLY_SYS_HANDLER, handlerid, token, numargs, arghandle)
#else
  #define GASNETC_TRACE_SYSTEM_REQUEST(dest,handler,numargs)
  #define GASNETC_TRACE_SYSTEM_REPLY(dest,handler,numargs)
  #define GASNETC_TRACE_SYSTEM_REQHANDLER(handlerid, token, numargs, arghandle) 
  #define GASNETC_TRACE_SYSTEM_REPHANDLER(handlerid, token, numargs, arghandle) 
#endif

#if GASNETI_STATS_OR_TRACE
  #define GASNETC_TRACE_WAIT_BEGIN() \
    gasneti_stattime_t _waitstart = GASNETI_STATTIME_NOW_IFENABLED(C)
#else 
  #define GASNETC_TRACE_WAIT_BEGIN() \
    static char _dummy = (char)sizeof(_dummy)
#endif

#define GASNETC_TRACE_WAIT_END(name) \
  GASNETI_TRACE_EVENT_TIME(C,name,GASNETI_STATTIME_NOW() - _waitstart)

#define GASNETC_STAT_EVENT(name) \
  _GASNETI_STAT_EVENT(C,name)
#define GASNETC_STAT_EVENT_VAL(name,val) \
  _GASNETI_STAT_EVENT_VAL(C,name,val)

/* ------------------------------------------------------------------------------------ */
/* Configuration */

/* Maximum number of segments to gather on send */
#define GASNETC_SND_SG	4

/* Defined non-zero in gasnet_config.h to enable a progress thread for receiving AMs . */
#ifndef GASNETC_VAPI_RCV_THREAD
  #define GASNETC_VAPI_RCV_THREAD	0
#endif

/* maximum number of ops reaped from the send CQ per poll */
#ifndef GASNETC_SND_REAP_LIMIT
  #define GASNETC_SND_REAP_LIMIT	32
#endif

/* maximum number of ops reaped from the recv CQ per poll */
#ifndef GASNETC_RCV_REAP_LIMIT
  #define GASNETC_RCV_REAP_LIMIT	16
#endif

/* Define non-zero if we want to allow the mlock rlimit to bound the
 * amount of memory we will pin. */
#ifndef GASNETC_HONOR_RLIMIT_MEMLOCK
  #define GASNETC_HONOR_RLIMIT_MEMLOCK 0
#endif

/* ------------------------------------------------------------------------------------ */

/* Measures of concurency
 *
 * GASNETC_ANY_PAR	Non-zero if multiple threads can be executing in GASNet.
 * 			This is inclusive of the AM receive thread.
 * GASNETC_CLI_PAR	Non-zero if multiple _client_ threads can be executing in GASNet.
 * 			This excludes the AM receive thread.
 * These differ from GASNETI_THREADS and GASNETI_CLIENT_THREADS in that they don't count
 * GASNET_PARSYNC, since it has threads which do not enter GASNet concurrently.
 */

#if GASNET_PAR
  #define GASNETC_CLI_PAR	1
#else
  #define GASNETC_CLI_PAR	0
#endif

#define GASNETC_ANY_PAR		(GASNETC_CLI_PAR || GASNETC_VAPI_RCV_THREAD)

/* ------------------------------------------------------------------------------------ */

#if defined(__i386__) || defined(__x86_64__)
  /* Since the LOCK prefix is already a full mb() */
  #define gasneti_wmb_before_atomic_op()	gasneti_compiler_fence()
  #define gasneti_rmb_after_atomic_op()		gasneti_compiler_fence()
#else
  #define gasneti_wmb_before_atomic_op()	gasneti_sync_writes()
  #define gasneti_rmb_after_atomic_op()		gasneti_sync_reads()
#endif

/* ------------------------------------------------------------------------------------ */

/*
 * gasnetc_sema_t
 *
 * This is a simple busy-waiting semaphore used, for instance, to control access to
 * some resource of known multiplicity.
 */

#if !GASNETC_ANY_PAR
  /* No threads, so we use the mutex code that compiles away. */
#elif (defined(__i386__) || defined(__x86_64__)) && \
	(defined(__GNUC__) || defined(__INTEL_COMPILER))
  /* optimized implementation for x86 and x86-64 systems */

  typedef gasneti_weakatomic_t _gasnetc_sema_t;

  #define _GASNETC_SEMA_INITIALIZER(N) {gasneti_weakatomic_init(N)}

  GASNET_INLINE_MODIFIER(_gasnetc_sema_init)
  void _gasnetc_sema_init(_gasnetc_sema_t *s, int n) {
    gasneti_weakatomic_set(s, n);
  }

  GASNET_INLINE_MODIFIER(_gasnetc_sema_destroy)
  void _gasnetc_sema_destroy(_gasnetc_sema_t *s) {
    /* Nothing */
  }

  GASNET_INLINE_MODIFIER(_gasnetc_sema_read)
  uint32_t _gasnetc_sema_read(_gasnetc_sema_t *s) {
    return gasneti_weakatomic_read(s);
  }

  GASNET_INLINE_MODIFIER(_gasnetc_sema_up)
  void _gasnetc_sema_up(_gasnetc_sema_t *s) {
    __asm__ __volatile__ (GASNETI_LOCK "incl %0" : "=m" (*s) : : "cc", "memory");
  }

  GASNET_INLINE_MODIFIER(_gasnetc_sema_trydown)
  int _gasnetc_sema_trydown(_gasnetc_sema_t *s) {
    register int tmp, retval;

    #if defined(__x86_64__) /* 1-character difference for 32-bit arithmetic via leal (bug 1411) */
      #define _GASNETC_SEMA_LEAL "leal	-1(%q1), %2"
    #else
      #define _GASNETC_SEMA_LEAL "leal	-1(%1), %2"
    #endif
    __asm__ __volatile__ ("movl		%0, %1		\n\t"	/* retval = *s */
		          "1: testl	%1, %1		\n\t"	/* test retval */
		          _GASNETC_SEMA_LEAL	       "\n\t"   /* tmp = retval-1 w/o changing cc */
		          "je		2f		\n\t"	/* fail if retval 0 */
             GASNETI_LOCK "cmpxchgl	%2, %0		\n\t"	/* swap */
                          "jne		1b		\n\t"	/* retry on conflict */
			  "2: "
                                : "=m" (*s), "=&a" (retval), "=&r" (tmp)
				: /* no inputs */
                                : "cc", "memory");
    #undef _GASNETC_SEMA_LEAL

    return retval;
  }

  GASNET_INLINE_MODIFIER(_gasnetc_sema_up_n)
  void _gasnetc_sema_up_n(_gasnetc_sema_t *s, uint32_t n) {
    __asm__ __volatile__ (GASNETI_LOCK "addl %1, %0" : "=m" (*s) : "ri" (n) : "cc", "memory");
  }

  GASNET_INLINE_MODIFIER(_gasnetc_sema_trydown_n)
  uint32_t _gasnetc_sema_trydown_n(_gasnetc_sema_t *s, uint32_t n) {
    register int oldval, newval;

    __asm__ __volatile__ ("movl		%0, %1		\n\t"	/* oldval = *s */
		          "1: movl	%1, %2		\n\t"	/* newval = ... */
		          "subl		%3, %2		\n\t"	/*          oldval - n */
		          "js		2f		\n\t"	/* fail if newval < 0 */
             GASNETI_LOCK "cmpxchgl	%2, %0		\n\t"	/* swap */
                          "jne		1b		\n\t"	/* retry on conflict */
			  "2: "
                                : "=m" (*s), "=&a" (oldval), "=&r" (newval)
				: "ri" (n)
                                : "cc", "memory");

    return (newval >= 0);
  }

  #define GASNETC_HAVE_ARCH_SEMA 1
#elif defined(GASNETI_HAVE_ATOMIC_CAS)
  /* Semi-generic implementation for CAS-capable systems */

  typedef gasneti_weakatomic_t _gasnetc_sema_t;

  #define _GASNETC_SEMA_INITIALIZER(N) {gasneti_weakatomic_init(N)}

  GASNET_INLINE_MODIFIER(_gasnetc_sema_init)
  void _gasnetc_sema_init(_gasnetc_sema_t *s, int n) {
    gasneti_weakatomic_set(s, n);
  }

  GASNET_INLINE_MODIFIER(_gasnetc_sema_destroy)
  void _gasnetc_sema_destroy(_gasnetc_sema_t *s) {
    /* Nothing */
  }

  GASNET_INLINE_MODIFIER(_gasnetc_sema_read)
  uint32_t _gasnetc_sema_read(_gasnetc_sema_t *s) {
    return gasneti_weakatomic_read(s);
  }

  GASNET_INLINE_MODIFIER(_gasnetc_sema_up)
  void _gasnetc_sema_up(_gasnetc_sema_t *s) {
    gasneti_wmb_before_atomic_op();
    gasneti_weakatomic_increment(s);
  }

  GASNET_INLINE_MODIFIER(_gasnetc_sema_trydown)
  int _gasnetc_sema_trydown(_gasnetc_sema_t *s) {
    uint32_t old;
    int retval = 0;

  again:
    old = gasneti_weakatomic_read(s);
    if_pt (old) {
      retval = gasneti_weakatomic_compare_and_swap(s, old, old - 1);
      if_pf (!retval) {
	/* contention in CAS */
	goto again;
      }
      gasneti_rmb_after_atomic_op();
    }

    return retval;
  }

  GASNET_INLINE_MODIFIER(_gasnetc_sema_up_n)
  void _gasnetc_sema_up_n(_gasnetc_sema_t *s, uint32_t n) {
    uint32_t old;
    gasneti_wmb_before_atomic_op();
    do {
      old = gasneti_weakatomic_read(s);
    } while (!gasneti_weakatomic_compare_and_swap(s, old, n + old));
  }

  GASNET_INLINE_MODIFIER(_gasnetc_sema_trydown_n)
  uint32_t _gasnetc_sema_trydown_n(_gasnetc_sema_t *s, uint32_t n) {
    uint32_t retval, old;

    do {
      old = gasneti_weakatomic_read(s);
      if_pf (old == 0)
        return 0;
      retval = MIN(old, n);
    } while(!gasneti_weakatomic_compare_and_swap(s, old, old - retval));
    gasneti_rmb_after_atomic_op();

    return retval;
  }

  #define GASNETC_HAVE_ARCH_SEMA 1
#endif

#ifndef GASNETC_HAVE_ARCH_SEMA
  /* Generic mutex-based implementation */
  typedef struct {
    gasneti_mutex_t		lock;
    gasneti_weakatomic_t	count;
  } _gasnetc_sema_t;

  #define _GASNETC_SEMA_INITIALIZER(N) {GASNETI_MUTEX_INITIALIZER, gasneti_weakatomic_init(N)}

  GASNET_INLINE_MODIFIER(_gasnetc_sema_init)
  void _gasnetc_sema_init(_gasnetc_sema_t *s, int n) {
    gasneti_mutex_init(&(s->lock));
    gasneti_weakatomic_set(&(s->count), n);
  }

  GASNET_INLINE_MODIFIER(_gasnetc_sema_destroy)
  void _gasnetc_sema_destroy(_gasnetc_sema_t *s) {
    gasneti_mutex_destroy(&(s->lock));
  }

  GASNET_INLINE_MODIFIER(_gasnetc_sema_read)
  uint32_t _gasnetc_sema_read(_gasnetc_sema_t *s) {
    return gasneti_weakatomic_read(&(s->count));
  }

  GASNET_INLINE_MODIFIER(_gasnetc_sema_up)
  void _gasnetc_sema_up(_gasnetc_sema_t *s) {
    gasneti_wmb_before_atomic_op();
    gasneti_weakatomic_increment(&(s->count));
  }

  GASNET_INLINE_MODIFIER(_gasnetc_sema_trydown)
  int _gasnetc_sema_trydown(_gasnetc_sema_t *s) {
    int retval;

    gasneti_mutex_lock(&(s->lock));
    retval = gasneti_weakatomic_read(&(s->count));
    if_pt(retval != 0)
      gasneti_weakatomic_decrement(&(s->count));
    gasneti_mutex_unlock(&(s->lock));

    return retval;
  }

  GASNET_INLINE_MODIFIER(_gasnetc_sema_up_n)
  void _gasnetc_sema_up_n(_gasnetc_sema_t *s, uint32_t n) {
    gasneti_mutex_lock(&(s->lock));
    gasneti_weakatomic_set(&(s->count), n + gasneti_weakatomic_read(&(s->count)));
    gasneti_mutex_unlock(&(s->lock));
  }

  GASNET_INLINE_MODIFIER(_gasnetc_sema_trydown_n)
  uint32_t _gasnetc_sema_trydown_n(_gasnetc_sema_t *s, uint32_t n) {
    uint32_t retval, old;

    gasneti_mutex_lock(&(s->lock));
    old = gasneti_weakatomic_read(&(s->count));
    retval = MIN(old, n);
    gasneti_weakatomic_set(&(s->count), old - retval);
    gasneti_mutex_unlock(&(s->lock));

    return retval;
  }

  #define GASNETC_HAVE_ARCH_SEMA 0
#endif

typedef struct {
  #if GASNET_DEBUG
    _gasnetc_sema_t	S;
    uint32_t		limit;
    char		_pad[GASNETC_CACHE_PAD(sizeof(uint32_t)+sizeof(_gasnetc_sema_t))];
  #else
    _gasnetc_sema_t	S;
    char		_pad[GASNETC_CACHE_PAD(sizeof(_gasnetc_sema_t))];
  #endif
} gasnetc_sema_t;

#if GASNET_DEBUG
  #define GASNETC_SEMA_INITIALIZER(N) {_GASNETC_SEMA_INITIALIZER(N), (N),}
  #define GASNETC_SEMA_CHECK(_s)	do {                                  \
      int32_t _tmp = _gasnetc_sema_read(&(_s)->S);                            \
      gasneti_assert((_tmp >= 0) && ((_tmp <= (_s)->limit) || !(_s)->limit)); \
    } while (0)
#else
  #define GASNETC_SEMA_INITIALIZER(N) {_GASNETC_SEMA_INITIALIZER(N),}
  #define GASNETC_SEMA_CHECK(_s)	do {} while(0)
#endif

/* gasnetc_sema_init */
GASNET_INLINE_MODIFIER(gasnetc_sema_init)
void gasnetc_sema_init(gasnetc_sema_t *s, int n, uint32_t limit) {
  _gasnetc_sema_init(&(s->S), n);
  #if GASNET_DEBUG
    s->limit = limit;
  #endif
  GASNETC_SEMA_CHECK(s);
}

/* gasnetc_sema_destroy */
GASNET_INLINE_MODIFIER(gasnetc_sema_destroy)
void gasnetc_sema_destroy(gasnetc_sema_t *s) {
  GASNETC_SEMA_CHECK(s);
  _gasnetc_sema_destroy(&(s->S));
}

/* gasnetc_sema_read
 *
 * Returns current value of the semaphore
 */
GASNET_INLINE_MODIFIER(gasnetc_sema_read)
uint32_t gasnetc_sema_read(gasnetc_sema_t *s) {
  GASNETC_SEMA_CHECK(s);
  return _gasnetc_sema_read(&(s->S));
}

/* gasnetc_sema_up
 *
 * Atomically increments the value of the semaphore.
 * Since this just a busy-waiting semaphore, no waking operations are required.
 */
GASNET_INLINE_MODIFIER(gasnetc_sema_up)
void gasnetc_sema_up(gasnetc_sema_t *s) {
  GASNETC_SEMA_CHECK(s);
  _gasnetc_sema_up(&(s->S));
  GASNETC_SEMA_CHECK(s);
}

/* gasnetc_sema_trydown
 *
 * If the value of the semaphore is non-zero, decrements it and returns non-zero.
 * If the value is zero, returns zero.
 */
GASNET_INLINE_MODIFIER(gasnetc_sema_trydown) GASNETI_WARN_UNUSED_RESULT
int gasnetc_sema_trydown(gasnetc_sema_t *s) {
  int retval;

  GASNETC_SEMA_CHECK(s);
  retval = _gasnetc_sema_trydown(&(s->S));
  GASNETC_SEMA_CHECK(s);

  return retval;
}

/* gasnetc_sema_up_n
 *
 * Increases the value of the semaphore by the indicated count.
 * Since this just a busy-waiting semaphore, no waking operations are required.
 */
GASNET_INLINE_MODIFIER(gasnetc_sema_up_n)
void gasnetc_sema_up_n(gasnetc_sema_t *s, uint32_t n) {
  GASNETC_SEMA_CHECK(s);
  _gasnetc_sema_up_n(&(s->S), n);
  GASNETC_SEMA_CHECK(s);
}

/* gasnetc_sema_trydown_n
 *
 * Decrements the semaphore by as much as 'n' and returns the number of "counts" thus
 * obtained.  The decrement is the smaller of 'n' and the "old" value of the semaphore,
 * and this value is returned.
 * If the "old" value is zero, returns zero.
 */
GASNET_INLINE_MODIFIER(gasnetc_sema_trydown_n) GASNETI_WARN_UNUSED_RESULT
uint32_t gasnetc_sema_trydown_n(gasnetc_sema_t *s, uint32_t n) {
  uint32_t retval;

  GASNETC_SEMA_CHECK(s);
  retval = _gasnetc_sema_trydown_n(&(s->S), n);
  GASNETC_SEMA_CHECK(s);

  return retval;
}

/* ------------------------------------------------------------------------------------ */

/* Global freelist type
 *
 * Freelists in vapi-conduit have multiple consumers and multiple producers.
 * Thread-local lists don't use this data structure.
 *
 * Current implementation is a LIFO (stack) with a mutex (and optionally lock-free).
 * Other possibilities include FIFO (queue) with mutex, or lock-free LIFO or FIFO
 */

/*
 * Data type for the linkage of a freelist
 * Must be the first field in the data structure to be held in the list or the
 * data structure should be considered to be a union of this type and the real
 * type.  In the union case one can expect some number of bytes to get clobbered
 * while an element is on the freelist, while making this type the first element
 * (as shown below) preserves the contents of the free element (useful if there
 * are invariants to preserve).
 *
 * struct foobar {
 *   gasneti_freelist_ptr_t	linkage;
 *
 *   int			blah;
 *   double			boo;
 * };
 */
typedef struct _gasneti_freelist_ptr_s {
  struct _gasneti_freelist_ptr_s *next;
} gasneti_freelist_ptr_t;


/* Optional arch-specific freelist code */
#if !GASNETC_ANY_PAR
  /* No threads, so we use the mutex code that compiles away. */
#elif defined(__i386__) /* x86 but NOT x86_64 */
  #if defined(__GNUC__) || defined(__INTEL_COMPILER)
    typedef struct {
      volatile uintptr_t 	head;
      volatile uintptr_t 	ABA_tag;
      char			_pad[GASNETC_CACHE_PAD(2*sizeof(uintptr_t))];
    } gasneti_freelist_t;

    GASNET_INLINE_MODIFIER(gasneti_fl_push)
    void gasneti_fl_push(gasneti_freelist_t *p, gasneti_freelist_ptr_t *head, gasneti_freelist_ptr_t *tail) {
      /* RELEASE semantics: LOCK prefix is a full mb() */
      __asm__ __volatile__ ("1: movl	%0, %%eax	\n\t"	/* eax = p->head */
                            "movl	%%eax, %2	\n\t"	/* tail->next = eax */
               GASNETI_LOCK "cmpxchgl	%1, %0		\n\t"	/* p->head = head */
                            "jne	1b"		/* retry on conflict */
                                : "=m" (p->head)
                                : "r" (head), "m" (tail->next)
                                : "cc", "memory", "eax");
    }
    GASNET_INLINE_MODIFIER(gasneti_fl_pop)
    void *gasneti_fl_pop(gasneti_freelist_t *p) {
      /* ACQUIRE semantics: LOCK prefix is a full mb() */
      register uintptr_t retval = p->head;
      __asm__ __volatile__ ("1: test	%0,%0		\n\t"	/* terminate loop ... */
                            "jz		2f		\n\t"	/*        ... on NULL */
                            "mov	(%0), %%ebx	\n\t"	/* ebx = p->head->next */
                            "lea	1(%3), %%ecx	\n\t"	/* ecx = ABA_tag + 1 */ 
               GASNETI_LOCK "cmpxchg8b	%1		\n\t"	/* p->(head,ABA_tag) = (ebx,ecx) */
                            "jne	1b		\n\t"	/* retry w/ updated (eax,edx) */
                            "2:"
                                : "=a" (retval)
                                : "m" (p->head), "a" (retval), "d" (p->ABA_tag)
                                : "cc", "memory", "ebx", "ecx");
      return (void *)retval;
    }
    GASNET_INLINE_MODIFIER(gasneti_fl_init)
    void gasneti_fl_init(gasneti_freelist_t *p) {
      p->head = 0;
    }
    #define GASNETI_FREELIST_INITIALIZER	{0,}
    #define GASNETI_HAVE_ARCH_FL	1
  #endif
#elif defined(__x86_64__)
  /* No support yet because there is no CAS2 or DCSS (double-compare single-swap) support for 8-byte
   * pointers.  While the architecture includes an optional cmpxchg16b (CAS2), no current CPU implements
   * it.  The CS literature offers many ways to simulate CAS2 or DCSS using just CAS (cmpxchg8b), but
   * they all are either very complex and/or require thread-specific data to help resolve the ABA
   * problem.  I'll continue to look into this.  -PHH 2006.01.19
   */
#elif defined(__ia64__) || defined(__ia64)
  /* Issues are similar to x86_64, lacking CAS2 or DCSS instructions (even optional ones) */
#elif (defined(__APPLE__) && defined(__MACH__) && defined(__ppc__)) || (defined(__linux__) && defined(__PPC__))
  /* PowerPC
   * (__APPLE__) && __MACH__ && __ppc__) == OS/X, Darwin
   * (__linux__ && __PPC__) == Linux
   */
  #if defined(__GNUC__)
    typedef struct {
      volatile gasneti_freelist_ptr_t *head;
      char			_pad[GASNETC_CACHE_PAD(sizeof(gasneti_freelist_ptr_t *))];
    } gasneti_freelist_t;

    GASNET_INLINE_MODIFIER(gasneti_fl_push)
    void gasneti_fl_push(gasneti_freelist_t *p, gasneti_freelist_ptr_t *head, gasneti_freelist_ptr_t *tail) {
      /* Roughly based on Appendix D of IBM's "Programming Environments Manual for 64-bit Microprocessors."
       * The key is moving the store to tail->next outside the loop and rechecking tmp1==tmp2 inside.
       * This is needed because a store in the l[wd]arx/st[wd]cx interval can lead to livelock.
       */
      /* RELEASE semantics: 'sync' is wmb after the write to tail->next */
      register uintptr_t tmp1, tmp2;
      #if (SIZEOF_VOID_P == 4)
        __asm__ __volatile__ ("lwz	%3,0(%0)   \n\t" /* tmp1 = p->head */
			      "1: mr	%4,%3      \n\t" /* tmp2 = tmp1 */
			      "stw	%3,0(%2)   \n\t" /* tail->next = tmp1 */
			      "sync	           \n\t" /* wmb */
			      "2: lwarx	%3,0,%0    \n\t" /* reload tmp1 = p->head */
			      "cmpw	%3,%4      \n\t" /* check tmp1 still == tmp2 */
			      "bne-	1b         \n\t" /* retry if p->head changed since starting */
			      "stwcx.	%1,0,%0    \n\t" /* p->head = head */
			      "bne-	2b         \n\t" /* retry on conflict */
			      "isync"
				: "=b" (p), "=r" (head), "=b" (tail), "=r" (tmp1), "=r" (tmp2)
				: "0" (p), "1" (head), "2" (tail) 
				: "memory", "cc");
      #elif (SIZEOF_VOID_P == 8)
        __asm__ __volatile__ ("ld	%3,0(%0)   \n\t" /* tmp1 = p->head */
			      "1: mr	%4,%3      \n\t" /* tmp2 = tmp1 */
			      "std	%3,0(%2)   \n\t" /* tail->next = tmp1 */
			      "sync	           \n\t" /* wmb */
			      "2: ldarx	%3,0,%0    \n\t" /* reload tmp1 = p->head */
			      "cmpd	%3,%4      \n\t" /* check tmp1 still == tmp2 */
			      "bne-	1b         \n\t" /* retry if p->head changed since starting */
			      "stdcx.	%1,0,%0    \n\t" /* p->head = head */
			      "bne-	2b"		 /* retry on conflict */
				: "=b" (p), "=r" (head), "=b" (tail), "=r" (tmp1), "=r" (tmp2)
				: "0" (p), "1" (head), "2" (tail) 
				: "memory", "cc");
      #else
        #error "PPC w/ unknown word size"
      #endif
    }
    GASNET_INLINE_MODIFIER(gasneti_fl_pop)
    void *gasneti_fl_pop(gasneti_freelist_t *p) {
      /* ACQUIRE semantics: 'isync' between read of head and head->next */
      register uintptr_t head, next;
      if_pf (p->head == NULL) {
	/* One expects the empty list case to be the most prone to contention because
	 * many threads may be continuously polling for it become non-empty.  The l[wd]arx
	 * involves obtaining the cache line in an Exclusive state, while this normal
	 * load does not.  Thus this redundant check is IBM's recommended practice.
	 */
	return NULL;
      }
      #if (SIZEOF_VOID_P == 4)
        __asm__ __volatile__ ("1: lwarx	%1,0,%0    \n\t" /* head = p->head */
			      "cmpwi	0,%1,0     \n\t" /* head == NULL? */
			      "beq-	2f         \n\t" /* end on NULL */
			      "isync               \n\t" /* rmb */
			      "lwz	%2,0(%1)   \n\t" /* next = head->next */
			      "stwcx.	%2,0,%0    \n\t" /* p->head = next */
			      "bne-	1b         \n\t" /* retry on conflict */
			      "2: "
				: "=b" (p), "=b" (head), "=r" (next)
				: "0" (p)
				: "memory", "cc");
      #elif (SIZEOF_VOID_P == 8)
        __asm__ __volatile__ ("1: ldarx	%1,0,%0    \n\t" /* head = p->head */
			      "cmpdi	0,%1,0     \n\t" /* head == NULL? */
			      "beq-	2f         \n\t" /* end on NULL */
			      "isync               \n\t" /* rmb */
			      "ld	%2,0(%1)   \n\t" /* next = head->next */
			      "stdcx.	%2,0,%0    \n\t" /* p->head = next */
			      "bne-	1b         \n\t" /* retry on conflict */
			      "2: "
				: "=b" (p), "=b" (head), "=r" (next)
				: "0" (p)
				: "memory", "cc");
      #else
        #error "PPC w/ unknown word size"
      #endif
      return (void *)head;
    }
    GASNET_INLINE_MODIFIER(gasneti_fl_init)
    void gasneti_fl_init(gasneti_freelist_t *p) {
      p->head = NULL;
    }
    #define GASNETI_FREELIST_INITIALIZER	{NULL,}
    #define GASNETI_HAVE_ARCH_FL
  #elif defined(__xlC__)
    /* XLC assembly is too painful to consider this yet */
  #endif
#else
  /* Not x86, x86_64, ia64 or ppc?  Where else is VAPI running? */
#endif

/* Generic mutex-based default implementation */
#ifndef GASNETI_HAVE_ARCH_FL
    typedef struct {
      gasneti_mutex_t		lock;
      gasneti_freelist_ptr_t	*head;
      char			_pad[GASNETC_CACHE_PAD(sizeof(gasneti_mutex_t)+sizeof(gasneti_freelist_ptr_t *))];
    } gasneti_freelist_t;

    GASNET_INLINE_MODIFIER(gasneti_fl_push)
    void gasneti_fl_push(gasneti_freelist_t *p, gasneti_freelist_ptr_t *head, gasneti_freelist_ptr_t *tail) {
      gasneti_mutex_lock(&(p->lock));
      tail->next = p->head;
      p->head = head;
      gasneti_mutex_unlock(&(p->lock));
    }
    GASNET_INLINE_MODIFIER(gasneti_fl_pop)
    void *gasneti_fl_pop(gasneti_freelist_t *p) {
      gasneti_freelist_ptr_t *head;
      gasneti_mutex_lock(&(p->lock));
      head = p->head;
      if_pt (head != NULL) {
        p->head = head->next;
      }
      gasneti_mutex_unlock(&(p->lock));
      return (void *)head;
    }
    GASNET_INLINE_MODIFIER(gasneti_fl_init)
    void gasneti_fl_init(gasneti_freelist_t *p) {
      gasneti_mutex_init(&(p->lock));
      p->head = NULL;
    }
    #define GASNETI_FREELIST_INITIALIZER	{ GASNETI_MUTEX_INITIALIZER, NULL }
    #define GASNETI_HAVE_ARCH_FL	0
#endif
    


/* Initializer for dynamically allocated freelists */
GASNET_INLINE_MODIFIER(gasneti_freelist_init)
void gasneti_freelist_init(gasneti_freelist_t *fl) {
  gasneti_fl_init(fl);
}

/* Get one element from the freelist or NULL if it is empty */
#ifdef __GNUC__
  GASNET_INLINE_MODIFIER(gasneti_freelist_get)
  void *gasneti_freelist_get(gasneti_freelist_t *fl) GASNETI_MALLOC;
#endif
GASNET_INLINE_MODIFIER(gasneti_freelist_get)
void *gasneti_freelist_get(gasneti_freelist_t *fl) {
  return gasneti_fl_pop(fl);
}

/* Put an unused element into the freelist */
GASNET_INLINE_MODIFIER(gasneti_freelist_put)
void gasneti_freelist_put(gasneti_freelist_t *fl, void *elem) {
  gasneti_assert(elem != NULL);
  gasneti_fl_push(fl, (gasneti_freelist_ptr_t *)elem, (gasneti_freelist_ptr_t *)elem);
}

/* Put a chain of unused elements into the freelist */
GASNET_INLINE_MODIFIER(gasneti_freelist_put_many)
void gasneti_freelist_put_many(gasneti_freelist_t *fl, void *head, void *tail) {
  gasneti_assert(head != NULL);
  gasneti_assert(tail != NULL);
  gasneti_fl_push(fl, (gasneti_freelist_ptr_t *)head, (gasneti_freelist_ptr_t *)tail);
}

/* Build a chain (q follows p) for use with _put_many() */
GASNET_INLINE_MODIFIER(gasneti_freelist_link)
void gasneti_freelist_link(void *p, void *q) {
  gasneti_assert(p != NULL);

  ((gasneti_freelist_ptr_t *)p)->next = q;
}

/* Get next element in a chain */
GASNET_INLINE_MODIFIER(gasneti_freelist_next)
void *gasneti_freelist_next(void *elem) {
  gasneti_assert(elem != NULL);

  return (void *)(((gasneti_freelist_ptr_t *)elem)->next);
}

/* ------------------------------------------------------------------------------------ */

#if GASNETC_VAPI_MAX_HCAS > 1
  #define GASNETC_FOR_ALL_HCA_INDEX(h)	for (h = 0; h < gasnetc_num_hcas; ++h)
  #define GASNETC_FOR_ALL_HCA(p)	for (p = &gasnetc_hca[0]; p < &gasnetc_hca[gasnetc_num_hcas]; ++p)
#else
  #define GASNETC_FOR_ALL_HCA_INDEX(h)	for (h = 0; h < 1; ++h)
  #define GASNETC_FOR_ALL_HCA(p)	for (p = &gasnetc_hca[0]; p < &gasnetc_hca[1]; ++p)
#endif

/* ------------------------------------------------------------------------------------ */


/* Description of a pre-pinned memory region */
typedef struct {
  VAPI_mr_hndl_t	handle;	/* used to release or modify the region */
  VAPI_lkey_t		lkey;	/* used for local access by HCA */
  VAPI_rkey_t		rkey;	/* used for remote access by HCA */
  VAPI_hca_hndl_t	hca_hndl;
  uintptr_t		addr;
  size_t		len;
  uintptr_t		end;	/* inclusive */

  /* requested values, before rounding by HCA */
  void *		req_addr;
  size_t		req_size;
} gasnetc_memreg_t;

/* Structure for an HCA */
typedef struct {
  VAPI_hca_hndl_t	handle;
  gasnetc_memreg_t	rcv_reg;
  gasnetc_memreg_t	snd_reg;
#if GASNETC_PIN_SEGMENT
  gasnetc_memreg_t	*seg_reg;
  VAPI_rkey_t		*rkeys;	/* RKey(s) registered at attach time */
#endif
  VAPI_cq_hndl_t	rcv_cq;
  VAPI_cq_hndl_t	snd_cq;
  VAPI_pd_hndl_t	pd;
#if FIREHOSE_VAPI_USE_FMR
  EVAPI_fmr_t		fmr_props;
#endif
  int			hca_index;
  char			*hca_id;
  VAPI_hca_cap_t	hca_cap;
  VAPI_hca_vendor_t	hca_vendor;
  int			qps; /* qps per peer */
  int			total_qps; /* total over all peers */

  void			*rbuf_alloc;
  gasneti_freelist_t	rbuf_freelist;

  /* Rcv thread */
  EVAPI_compl_handler_hndl_t rcv_handler;
  void			*rcv_thread_priv;
} gasnetc_hca_t;

/* Keys in a cep, all replicated from other data */
struct gasnetc_cep_keys_ {
#if GASNETC_PIN_SEGMENT
  gasnetc_memreg_t	*seg_reg;
  VAPI_rkey_t		*rkeys;	/* RKey(s) registered at attach time (== uint32_t) */
#endif
  VAPI_lkey_t		rcv_lkey;
  VAPI_lkey_t		snd_lkey;
};

/* Structure for a cep (connection end-point) */
typedef struct {
  /* Read/write fields */
  gasnetc_sema_t	sq_sema;	/* control in-flight ops (send queue slots) */
  gasnetc_sema_t	am_sema;	/* control in-flight AM Requests (recv queue slots )*/
  gasnetc_sema_t	am_unrcvd;	/* ACK coalescing - unmatched rcv buffers */
  gasnetc_sema_t	*snd_cq_sema_p;	/* control in-flight ops (send completion queue slots) */
  gasneti_weakatomic_t	am_unsent;	/* ACK coalescing - unsent credits */
  char			_pad0[GASNETC_CACHE_PAD(3*sizeof(gasnetc_sema_t)+
						 sizeof(gasnetc_sema_t*)+
						 sizeof(gasneti_weakatomic_t))];

  /* Read-only fields */
  struct gasnetc_cep_keys_ keys;
  gasneti_freelist_t	*rbuf_freelist;	/* Source of rcv buffers for AMs */
  gasnetc_hca_t		*hca;
  VAPI_qp_hndl_t	qp_handle;	/* == unsigned long */
  VAPI_hca_hndl_t	hca_handle;	/* == uint32_t */
  int			hca_index;
  gasnetc_epid_t	epid;		/* == uint32_t */
  char			_pad1[GASNETC_CACHE_PAD(sizeof(struct gasnetc_cep_keys_) +
						sizeof(gasneti_freelist_t*)+
						sizeof(gasnetc_hca_t*)+
						sizeof(VAPI_qp_hndl_t)+
						sizeof(VAPI_hca_hndl_t)+
						sizeof(int)+
						sizeof(gasnetc_epid_t))];
} gasnetc_cep_t;

/* Routines in gasnet_core_sndrcv.c */
extern int gasnetc_sndrcv_init(void);
extern void gasnetc_sndrcv_fini(void);
extern void gasnetc_sndrcv_init_peer(gasnet_node_t node);
extern void gasnetc_sndrcv_attach_peer(gasnet_node_t node);
extern void gasnetc_sndrcv_fini_peer(gasnet_node_t node);
extern void gasnetc_sndrcv_poll(void);
extern int gasnetc_RequestGeneric(gasnetc_category_t category,
				  int dest, gasnet_handler_t handler,
				  void *src_addr, int nbytes, void *dst_addr,
				  int numargs, gasnetc_counter_t *mem_oust, va_list argptr);
extern int gasnetc_ReplyGeneric(gasnetc_category_t category,
				gasnet_token_t token, gasnet_handler_t handler,
				void *src_addr, int nbytes, void *dst_addr,
				int numargs, gasnetc_counter_t *mem_oust, va_list argptr);

/* General routines in gasnet_core.c */
extern VAPI_ret_t gasnetc_pin(gasnetc_hca_t *hca, void *addr, size_t size, VAPI_mrw_acl_t acl, gasnetc_memreg_t *reg);
extern void gasnetc_unpin(gasnetc_memreg_t *reg);
#define gasnetc_unmap(reg)	gasneti_munmap((reg)->req_addr, (reg)->req_size)

/* Global configuration variables */
extern int		gasnetc_op_oust_limit;
extern int		gasnetc_op_oust_pp;
extern int		gasnetc_am_oust_limit;
extern int		gasnetc_am_oust_pp;
extern int		gasnetc_bbuf_limit;
extern int		gasnetc_use_rcv_thread;
extern int		gasnetc_am_credits_slack;
extern int		gasnetc_num_qps;
extern size_t		gasnetc_packedlong_limit;
extern size_t		gasnetc_inline_limit;
extern size_t		gasnetc_bounce_limit;
#if !GASNETC_PIN_SEGMENT
  extern size_t		gasnetc_putinmove_limit;
#endif
#if GASNET_DEBUG
  #define GASNETC_USE_FIREHOSE	gasnetc_use_firehose
  extern int		gasnetc_use_firehose;
#else
  #define GASNETC_USE_FIREHOSE	1
#endif

/* Global variables */
extern int		gasnetc_num_hcas;
extern gasnetc_hca_t	gasnetc_hca[GASNETC_VAPI_MAX_HCAS];
extern gasnetc_cep_t	*gasnetc_cep;
extern uintptr_t	gasnetc_max_msg_sz;
#if GASNETC_PIN_SEGMENT
  extern int			gasnetc_seg_reg_count;
  extern int			gasnetc_max_regs; /* max of gasnetc_seg_reg_count over all nodes */
  extern uintptr_t		gasnetc_seg_start;
  extern uintptr_t		gasnetc_seg_end;
  extern unsigned long		gasnetc_pin_maxsz;
  extern int			gasnetc_pin_maxsz_shift;
#endif
extern size_t			gasnetc_fh_align;
extern size_t			gasnetc_fh_align_mask;
extern firehose_info_t		gasnetc_firehose_info;

#endif

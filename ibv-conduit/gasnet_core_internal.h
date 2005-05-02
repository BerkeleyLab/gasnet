/*   $Source: /Users/kamil/work/gasnet-cvs2/gasnet/ibv-conduit/gasnet_core_internal.h,v $
 *     $Date: 2005/05/02 20:43:11 $
 * $Revision: 1.79 $
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

/* ------------------------------------------------------------------------------------ */

typedef struct {
#if GASNETI_STATS_OR_TRACE
  gasneti_stattime_t	stamp;
#endif
  gasnet_handlerarg_t	args[GASNETC_MAX_ARGS];	
} gasnetc_shortmsg_t;

typedef struct {
#if GASNETI_STATS_OR_TRACE
  gasneti_stattime_t	stamp;
#endif
  uint16_t		nBytes;
  uint16_t		_pad0;
  gasnet_handlerarg_t	args[GASNETC_MAX_ARGS];	
} gasnetc_medmsg_t;

typedef struct {
#if GASNETI_STATS_OR_TRACE
  gasneti_stattime_t	stamp;
#endif
  uintptr_t		destLoc;
  uint32_t		nBytes;
  gasnet_handlerarg_t	args[GASNETC_MAX_ARGS];	
} gasnetc_longmsg_t;

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

/* The a Medium AM's payload starts as soon after the args as possible while still
   providing the required 8-byte alignment.
*/
#define GASNETC_MSG_MED_OFFSET(nargs)	\
	(offsetof(gasnetc_medmsg_t,args) + 4 * (nargs + ((nargs & 0x1) ^ ((GASNETC_MEDIUM_HDRSZ>>2) & 0x1))))
#define GASNETC_MSG_MED_DATA(msg, nargs) \
	((void *)((uintptr_t)(msg) + GASNETC_MSG_MED_OFFSET(nargs)))

/* Only needed for non-RDMA ReplyLong, used when remote segment not pinned.
   This is a much simpler expression than the medium, since we don't 8-byte align.
*/
#define GASNETC_MSG_LONG_OFFSET(nargs)	\
	(offsetof(gasnetc_longmsg_t,args) + 4 * nargs)
#define GASNETC_MSG_LONG_DATA(msg, nargs) \
	((void *)((uintptr_t)(msg) + GASNETC_MSG_LONG_OFFSET(nargs)))

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

/* Lock ops that depend on the level of concurrency */
#define gasnetc_mutex_t                      gasneti_mutex_t
#define GASNETC_MUTEX_INITIALIZER            GASNETI_MUTEX_INITIALIZER
#define gasnetc_mutex_init                   gasneti_mutex_init
#define gasnetc_mutex_destroy                gasneti_mutex_destroy
#define gasnetc_mutex_lock(X,C)              if (C) { gasneti_mutex_lock(X); }
#define gasnetc_mutex_unlock(X,C)            if (C) { gasneti_mutex_unlock(X); }
#define gasnetc_mutex_assertlocked(X,C)      if (C) { gasneti_mutex_assertlocked(X); }
#define gasnetc_mutex_assertunlocked(X,C)    if (C) { gasneti_mutex_assertlocked(X); }

/* ------------------------------------------------------------------------------------ */

/*
 * gasnetc_sema_t
 *
 * This is a simple busy-waiting semaphore used, for instance, to control access to
 * some resource of known multiplicity.
 */
typedef struct {
  #ifdef GASNETI_HAVE_ATOMIC_CAS
    gasneti_atomic_t	count;
    char		_pad[GASNETC_CACHE_PAD(sizeof(gasneti_atomic_t))];
  #else
    gasnetc_mutex_t	lock;
    gasneti_atomic_t	count;
    char		_pad[GASNETC_CACHE_PAD(sizeof(gasnetc_mutex_t)+sizeof(gasneti_atomic_t))];
  #endif
} gasnetc_sema_t;

#ifdef GASNETI_HAVE_ATOMIC_CAS
  #define GASNETC_SEMA_INITIALIZER(N) {gasneti_atomic_init(N)}
#else
  #define GASNETC_SEMA_INITIALIZER(N) {GASNETC_MUTEX_INITIALIZER, gasneti_atomic_init(N)}
#endif

/* gasnetc_sema_init */
GASNET_INLINE_MODIFIER(gasnetc_sema_init)
void gasnetc_sema_init(gasnetc_sema_t *s, int n) {
  #ifndef GASNETI_HAVE_ATOMIC_CAS
    gasnetc_mutex_init(&(s->lock));
  #endif
  gasneti_atomic_set(&(s->count), n);
}

/* gasnetc_sema_destroy */
GASNET_INLINE_MODIFIER(gasnetc_sema_destroy)
void gasnetc_sema_destroy(gasnetc_sema_t *s) {
  #ifndef GASNETI_HAVE_ATOMIC_CAS
    gasnetc_mutex_destroy(&(s->lock));
  #endif
}

/* gasnetc_sema_read
 *
 * Returns current value of the semaphore
 */
GASNET_INLINE_MODIFIER(gasnetc_sema_read)
uint32_t gasnetc_sema_read(gasnetc_sema_t *s) {
  /* no locking needed here */
  return gasneti_atomic_read(&(s->count));
}

/* gasnetc_sema_up
 *
 * Atomically increments the value of the semaphore.
 * Since this just a busy-waiting semaphore, no waking operations are required.
 */
GASNET_INLINE_MODIFIER(gasnetc_sema_up)
void gasnetc_sema_up(gasnetc_sema_t *s) {
  /* no locking needed here */
  gasneti_atomic_increment(&(s->count));
}

/* gasnetc_sema_trydown
 *
 * If the value of the semaphore is non-zero, decrements it and returns the old value.
 * If the value is zero, returns zero.
 *
 * If non-zero, the "concurrent" argument indicates that there are multiple threads
 * calling gasnetc_sema_trydown, and thus locking is required.
 */
GASNET_INLINE_MODIFIER(gasnetc_sema_trydown)
int gasnetc_sema_trydown(gasnetc_sema_t *s, int concurrent) {
  int retval;

  #ifdef GASNETI_HAVE_ATOMIC_CAS
    uint32_t old = gasneti_atomic_read(&(s->count));
    retval = (old > 0) && gasneti_atomic_compare_and_swap(&(s->count), old, old - 1);
    if (retval) gasneti_sync_reads();
  #else
    gasnetc_mutex_lock(&(s->lock), concurrent);

    retval = gasneti_atomic_read(&(s->count));
    if_pt(retval != 0)
      gasneti_atomic_decrement(&(s->count));

    gasnetc_mutex_unlock(&(s->lock), concurrent);
  #endif

  return retval;
}

/* gasnetc_sema_up_n
 *
 * Increases the value of the semaphore by the indicated count.
 * Since this just a busy-waiting semaphore, no waking operations are required.
 */
GASNET_INLINE_MODIFIER(gasnetc_sema_up_n)
void gasnetc_sema_up_n(gasnetc_sema_t *s, uint32_t n) {
  #ifdef GASNETI_HAVE_ATOMIC_CAS
    uint32_t old;
    do {
      old = gasneti_atomic_read(&(s->count));
    } while (!gasneti_atomic_compare_and_swap(&(s->count), old, n + old));
  #else
    gasnetc_mutex_lock(&(s->lock), concurrent);
    gasneti_atomic_write(&(s->count), n + gasneti_atomic_read(&(s->count)));
    gasnetc_mutex_unlock(&(s->lock), concurrent);
  #endif
}

/* gasnetc_sema_trydown_n
 *
 * Decrements the semaphore by as much as 'n' and returns the number of "counts" thus
 * obtained.  The decrement is the smaller of 'n' and the "old" value of the semaphore,
 * and this value is returned.
 * If the "old" value is zero, returns zero.
 *
 * If non-zero, the "concurrent" argument indicates that there are multiple threads
 * calling gasnetc_sema_trydown, and thus locking is required.
 */
GASNET_INLINE_MODIFIER(gasnetc_sema_trydown_n)
uint32_t gasnetc_sema_trydown_n(gasnetc_sema_t *s, uint32_t n, int concurrent) {
  uint32_t retval, old;

  #ifdef GASNETI_HAVE_ATOMIC_CAS
    do {
      old = gasneti_atomic_read(&(s->count));
      if_pf (old == 0)
        return 0;
      retval = MIN(old, n);
    } while(!gasneti_atomic_compare_and_swap(&(s->count), old, old - retval));
    gasneti_sync_reads();
  #else
    gasnetc_mutex_lock(&(s->lock), concurrent);

    old = gasneti_atomic_read(&(s->count));
    retval = MIN(old, n);
    gasneti_atomic_write(&(s->count), old - retval);

    gasnetc_mutex_unlock(&(s->lock), concurrent);
  #endif

  return retval;
}

#if GASNET_DEBUG
  GASNET_INLINE_MODIFIER(gasnetc_sema_check)
  void gasnetc_sema_check(gasnetc_sema_t *s, int limit) {
    uint32_t old = gasneti_atomic_read(&(s->count));

    gasneti_assert((old >= 0) && (old <= limit));
  }
  
  #define GASNETC_SEMA_CHECK(S, L)	gasnetc_sema_check((S),(L))
#else
  #define GASNETC_SEMA_CHECK(S, L)
#endif

/* ------------------------------------------------------------------------------------ */

/* Global freelist type
 *
 * Freelists in vapi-conduit have multiple consumers and multiple producers.
 * Thread-local lists don't use this data structure.
 *
 * Current implementation is a LIFO (stack) with a mutex.
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

/*
 * Data type for the "head" of a freelist.
 */
typedef struct {
  gasneti_mutex_t		lock;
  gasneti_freelist_ptr_t	*head;
  char				_pad[GASNETC_CACHE_PAD(sizeof(gasneti_mutex_t)+sizeof(gasneti_freelist_ptr_t *))];
} gasneti_freelist_t;

/* Initializer for staticly allocated freelists */
#define GASNETI_FREELIST_INITIALIZER	{ GASNETI_MUTEX_INITIALIZER, NULL }

/* Initializer for dynamically allocated freelists */
GASNET_INLINE_MODIFIER(gasneti_freelist_init)
void gasneti_freelist_init(gasneti_freelist_t *fl) {
  gasneti_mutex_init(&(fl->lock));
  fl->head = NULL;
}

/* Get one element from the freelist or NULL if it is empty */
#ifdef __GNUC__
  GASNET_INLINE_MODIFIER(gasneti_freelist_get)
  void *gasneti_freelist_get(gasneti_freelist_t *fl) __attribute__((__malloc__));
#endif
GASNET_INLINE_MODIFIER(gasneti_freelist_get)
void *gasneti_freelist_get(gasneti_freelist_t *fl) {
  gasneti_freelist_ptr_t *head;

  gasneti_mutex_lock(&((fl)->lock));
  head = fl->head;
  if_pt (head != NULL) {
    fl->head = head->next;
  }
  gasneti_mutex_unlock(&((fl)->lock));

  return (void *)head;
}

/* Put an unused element into the freelist */
GASNET_INLINE_MODIFIER(gasneti_freelist_put)
void gasneti_freelist_put(gasneti_freelist_t *fl, void *elem) {
  gasneti_assert(elem != NULL);

  gasneti_mutex_lock(&((fl)->lock));
  ((gasneti_freelist_ptr_t *)elem)->next = fl->head;
  fl->head = elem;
  gasneti_mutex_unlock(&((fl)->lock));
}

/* Put a chain of unused elements into the freelist */
GASNET_INLINE_MODIFIER(gasneti_freelist_put_many)
void gasneti_freelist_put_many(gasneti_freelist_t *fl, void *head, void *tail) {
  gasneti_assert(head != NULL);
  gasneti_assert(tail != NULL);

  gasneti_mutex_lock(&((fl)->lock));
  ((gasneti_freelist_ptr_t *)tail)->next = fl->head;
  fl->head = head;
  gasneti_mutex_unlock(&((fl)->lock));
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

/* Structure for a cep (connection end-point)
 * Include whatever per-node data we need.
 */
typedef struct {
  gasnetc_sema_t	sq_sema;	/* control in-flight RDMA ops (send queue slots) */
  gasnetc_sema_t	am_sema;	/* control in-flight AM Requests */
  VAPI_qp_hndl_t	qp_handle;	/* == unsigned long */
  #if GASNETC_PIN_SEGMENT
    /* Bounds and RKey(s) for the segment, registered at attach time */
    uintptr_t		end;	/* Cached inclusive ending address of the remote segment */
    VAPI_rkey_t		*rkeys;	/* RKey(s) registered at attach time (== uint32_t) */
    char		_pad[GASNETC_CACHE_PAD(2*sizeof(gasnetc_sema_t)+sizeof(VAPI_qp_hndl_t)+sizeof(uintptr_t)+sizeof(void*))];
  #else
    char		_pad[GASNETC_CACHE_PAD(2*sizeof(gasnetc_sema_t)+sizeof(VAPI_qp_hndl_t))];
  #endif
} gasnetc_cep_t;

/* Description of a pre-pinned memory region */
typedef struct {
  VAPI_mr_hndl_t	handle;	/* used to release or modify the region */
  VAPI_lkey_t		lkey;	/* used for local access by HCA */
  VAPI_rkey_t		rkey;	/* used for remote access by HCA */
  uintptr_t		addr;
  size_t		len;
  uintptr_t		end;	/* inclusive */

  /* requested values, before rounding by HCA */
  void *		req_addr;
  size_t		req_size;
} gasnetc_memreg_t;

/* Routines in gasnet_core_sndrcv.c */
extern int gasnetc_sndrcv_init(void);
extern void gasnetc_sndrcv_fini(void);
extern void gasnetc_sndrcv_init_cep(gasnetc_cep_t *cep);
extern void gasnetc_sndrcv_fini_cep(gasnetc_cep_t *cep);
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
extern void *gasnetc_alloc_pinned(size_t size, VAPI_mrw_acl_t acl, gasnetc_memreg_t *reg);
extern void gasnetc_free_pinned(gasnetc_memreg_t *reg);

/* Global configuration variables */
extern char		*gasnetc_hca_id;
extern IB_port_t	gasnetc_port_num;
extern int		gasnetc_op_oust_limit;
extern int		gasnetc_op_oust_pp;
extern int		gasnetc_am_oust_limit;
extern int		gasnetc_am_oust_pp;
extern int		gasnetc_bbuf_limit;
extern int		gasnetc_use_poll_lock;
extern int		gasnetc_use_rcv_thread;
extern int		gasnetc_use_firehose;

/* Global variables */
extern gasnetc_cep_t	*gasnetc_cep;
extern VAPI_hca_hndl_t	gasnetc_hca;
extern VAPI_hca_cap_t	gasnetc_hca_cap;
extern VAPI_hca_port_t	gasnetc_hca_port;
extern VAPI_pd_hndl_t	gasnetc_pd;
extern gasnetc_memreg_t		gasnetc_snd_reg;
extern gasnetc_memreg_t		gasnetc_rcv_reg;
#if GASNETC_PIN_SEGMENT
  extern int			gasnetc_seg_reg_count;
  extern gasnetc_memreg_t	*gasnetc_seg_reg;
  extern uintptr_t		gasnetc_seg_start;
  extern uintptr_t		gasnetc_seg_end;
  extern unsigned long		gasnetc_pin_maxsz;
  extern int			gasnetc_pin_maxsz_shift;
#endif
extern size_t			gasnetc_fh_maxsz;
extern firehose_info_t		gasnetc_firehose_info;
#if FIREHOSE_VAPI_USE_FMR
  extern EVAPI_fmr_t		gasnetc_fmr_props;
#endif
extern size_t			gasnetc_inline_limit;
extern size_t			gasnetc_bounce_limit;

extern VAPI_cq_hndl_t	gasnetc_snd_cq;
extern VAPI_cq_hndl_t	gasnetc_rcv_cq;


#endif

/*   $Source: /Users/kamil/work/gasnet-cvs2/gasnet/vapi-conduit/Attic/gasnet_core_internal.h,v $
 *     $Date: 2006/07/08 03:29:48 $
 * $Revision: 1.134.12.3 $
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
#define GASNETC_IS_EXITING() gasneti_atomic_read(&gasnetc_exit_running, GASNETI_ATOMIC_RMB_PRE)

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
  gasneti_tick_t	stamp;
#endif
  gasnet_handlerarg_t	args[GASNETC_MAX_ARGS];	
} gasnetc_shortmsg_t;
#define GASNETC_MSG_SHORT_ARGSEND(nargs) offsetof(gasnetc_shortmsg_t,args[(unsigned int)nargs])

typedef struct {
#if GASNETI_STATS_OR_TRACE
  gasneti_tick_t	stamp;
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
  gasneti_tick_t	stamp;
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
  gasneti_tick_t	stamp;
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
    gasneti_tick_t _waitstart = GASNETI_TICKS_NOW_IFENABLED(C)
#else 
  #define GASNETC_TRACE_WAIT_BEGIN() \
    static char _dummy = (char)sizeof(_dummy)
#endif

#define GASNETC_TRACE_WAIT_END(name) \
  GASNETI_TRACE_EVENT_TIME(C,name,gasneti_ticks_now() - _waitstart)

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

/* XXX: need env var overrides for DEPTH and MAX */
#define GASNETC_AMRDMA_DEPTH	2	/* Power-of-2 */
#define GASNETC_AMRDMA_HDRSZ    sizeof(uint32_t) /* space for immediate data */
#define GASNETC_AMRDMA_SZ	4096 /* Keep to a power-of-2 */
#define GASNETC_AMRDMA_MAX	(GASNETC_AMRDMA_SZ/2 - GASNETC_AMRDMA_HDRSZ)
typedef char gasnetc_amrdma_buf_t[GASNETC_AMRDMA_SZ];

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
  gasneti_lifo_head_t	rbuf_freelist;

  /* Rcv thread */
  EVAPI_compl_handler_hndl_t rcv_handler;
  void			*rcv_thread_priv;

  /* AM-over-RMDA */
  gasnetc_memreg_t	amrdma_reg;
  gasnetc_amrdma_buf_t	*amrdma_next;
} gasnetc_hca_t;

/* Keys in a cep, all replicated from other data */
struct gasnetc_cep_keys_ {
#if GASNETC_PIN_SEGMENT
  gasnetc_memreg_t	*seg_reg;
  VAPI_rkey_t		*rkeys;	/* RKey(s) registered at attach time (== uint32_t) */
#endif
  VAPI_lkey_t		rcv_lkey;
  VAPI_lkey_t		snd_lkey;
  VAPI_rkey_t		amrdma_rkey;
};

/* Structure for a cep (connection end-point) */
typedef struct {
  char			_pad0[GASNETI_CACHE_LINE_BYTES];

  /* Read/write fields */
  gasneti_semaphore_t	sq_sema;	/* control in-flight ops (send queue slots) */
  gasneti_semaphore_t	am_rem;		/* control in-flight AM Requests (remote rcv queue slots)*/
  gasneti_semaphore_t	am_loc;		/* control unmatched rcv buffers (local rcv queue slots) */
  gasneti_semaphore_t	*snd_cq_sema_p;	/* control in-flight ops (send completion queue slots) */
  /* XXX: The atomics in the next 2 structs really should get padded to full cache lines */
  struct {	/* AM flow control coallescing */
  	gasneti_weakatomic_t	sndrcv;
  	gasneti_weakatomic_t	rdma;
	gasneti_weakatomic32_t	ack_mask;
  } am_flow;
  struct {	/* AM-over-RDMA local state */
        gasneti_weakatomic_t	send_in_use[GASNETC_AMRDMA_DEPTH];
        gasneti_weakatomic_t	recv_in_use[GASNETC_AMRDMA_DEPTH];
	#if (GASNETC_AMRDMA_DEPTH > 1)
	  gasneti_weakatomic_t	send_count;
	  gasneti_weakatomic_t	recv_count;
	#endif
  } amrdma;

  char			_pad1[GASNETI_CACHE_LINE_BYTES];

  /* Read-only fields */
  struct gasnetc_cep_keys_ keys;
  gasneti_lifo_head_t	*rbuf_freelist;	/* Source of rcv buffers for AMs */
  gasnetc_hca_t		*hca;
  VAPI_qp_hndl_t	qp_handle;	/* == unsigned long */
  VAPI_hca_hndl_t	hca_handle;	/* == uint32_t */
  int			hca_index;
  gasnetc_epid_t	epid;		/* == uint32_t */
  gasnetc_amrdma_buf_t	*amrdma_loc;	
  uintptr_t		amrdma_rem;

  char			_pad2[GASNETI_CACHE_LINE_BYTES];
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

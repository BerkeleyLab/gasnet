/*   $Source: /Users/kamil/work/gasnet-cvs2/gasnet/vapi-conduit/Attic/gasnet_core_internal.h,v $
 *     $Date: 2006/09/07 01:11:23 $
 * $Revision: 1.134.18.3 $
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

#ifdef XXX_BUILD_VAPI
  #include <vapi.h>
  #include <evapi.h>
  #include <vapi_common.h>
#else
  #include <infiniband/verbs.h>
#endif

#if HAVE_MMAP
  #include <sys/mman.h> /* For MAP_FAILED */
#endif

/*  whether or not to use spin-locking for HSL's */
#define GASNETC_HSL_SPINLOCK 1

#define GASNETC_CACHE_PAD(SZ) (GASNETC_ALIGNUP(SZ,GASNETI_CACHE_LINE_BYTES)-(SZ))

/* check (even in optimized build) for VAPI errors */
#ifdef XXX_BUILD_VAPI
  #define GASNETC_VAPI_CHECK(rc,msg) \
    if_pf ((rc) != 0) \
      { gasneti_fatalerror("Unexpected error %s %s",VAPI_strerror_sym(rc),(msg)); }
#else
  #define GASNETC_VAPI_CHECK(rc,msg) \
    if_pf ((rc) != 0) \
      { gasneti_fatalerror("Unexpected error %s (errno=%d) %s",strerror(errno),errno,(msg)); }
  #define GASNETC_VAPI_CHECK_PTR(ptr,msg) GASNETC_VAPI_CHECK((ptr)==NULL,(msg))
#endif

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
/* Map VAPI and IBV type systems to a common gasnetc_ prefix */

#ifdef XXX_BUILD_VAPI
  typedef VAPI_lkey_t		gasnetc_lkey_t;
  typedef VAPI_rkey_t		gasnetc_rkey_t;
  typedef IB_port_t		gasnetc_port_t;
  typedef VAPI_mrw_acl_t	gasnetc_acl_t;
  typedef VAPI_wc_status_t	gasnetc_wc_status_t;
  typedef VAPI_wr_opcode_t	gasnetc_wr_opcode_t;

  typedef VAPI_hca_hndl_t	gasnetc_hca_hndl_t;
  typedef VAPI_pd_hndl_t	gasnetc_pd_hndl_t;
  typedef VAPI_mr_hndl_t	gasnetc_mr_hndl_t;
  typedef VAPI_cq_hndl_t	gasnetc_cq_hndl_t;
  typedef VAPI_qp_hndl_t	gasnetc_qp_hndl_t;
  
  typedef VAPI_hca_cap_t	gasnetc_hca_cap_t;
  typedef VAPI_hca_port_t	gasnetc_hca_port_t;

  typedef VAPI_rr_desc_t	gasnetc_rcv_wr_t;
  typedef VAPI_sr_desc_t	gasnetc_snd_wr_t;
  typedef VAPI_wc_desc_t	gasnetc_wc_t;
  typedef VAPI_sg_lst_entry_t	gasnetc_sge_t;
#else
  typedef uint32_t		gasnetc_lkey_t;
  typedef uint32_t		gasnetc_rkey_t;
  typedef uint8_t		gasnetc_port_t;
  typedef enum ibv_access_flags	gasnetc_acl_t;
  typedef enum ibv_wc_status	gasnetc_wc_status_t;
  typedef enum ibv_wr_opcode	gasnetc_wr_opcode_t;

  typedef struct ibv_context	*gasnetc_hca_hndl_t;
  typedef struct ibv_pd		*gasnetc_pd_hndl_t;
  typedef struct ibv_mr		*gasnetc_mr_hndl_t;
  typedef struct ibv_cq		*gasnetc_cq_hndl_t;
  typedef struct ibv_qp		*gasnetc_qp_hndl_t;
  
  typedef struct ibv_device_attr gasnetc_hca_cap_t;
  typedef struct ibv_port_attr	gasnetc_hca_port_t;

  typedef struct ibv_recv_wr	gasnetc_rcv_wr_t;
  typedef struct ibv_send_wr	gasnetc_snd_wr_t;
  typedef struct ibv_wc		gasnetc_wc_t;
  typedef struct ibv_sge	gasnetc_sge_t;
#endif

/* ------------------------------------------------------------------------------------ */
/* XXX: Need to map these and other constants to a GASNETC_ namespace */

#ifndef XXX_BUILD_VAPI
  #define PORT_DOWN		IBV_PORT_DOWN
  #define PORT_INITIALIZE	IBV_PORT_INIT
  #define PORT_ACTIVE		IBV_PORT_ACTIVE
  #define PORT_ARMED		IBV_PORT_ARMED
  
  #define VAPI_EN_LOCAL_WRITE	IBV_ACCESS_LOCAL_WRITE
  #define VAPI_EN_REMOTE_WRITE	IBV_ACCESS_REMOTE_WRITE
  #define VAPI_EN_REMOTE_READ	IBV_ACCESS_REMOTE_READ
#endif

/* ------------------------------------------------------------------------------------ */

/* Description of a pre-pinned memory region */
typedef struct {
  gasnetc_mr_hndl_t	handle;	/* used to release or modify the region */
  gasnetc_lkey_t	lkey;	/* used for local access by HCA */	/* XXX: redundant w/ ibv */
  gasnetc_rkey_t	rkey;	/* used for remote access by HCA */	/* XXX: redundant w/ ibv */
  gasnetc_hca_hndl_t	hca_hndl;					/* XXX: redundant w/ ibv */
  uintptr_t		addr;
  size_t		len;
  uintptr_t		end;	/* inclusive */
} gasnetc_memreg_t;

/* Structure for an HCA */
typedef struct {
  gasnetc_hca_hndl_t	handle;
  gasnetc_memreg_t	rcv_reg;
  gasnetc_memreg_t	snd_reg;
#if GASNETC_PIN_SEGMENT
  gasnetc_memreg_t	*seg_reg;
  gasnetc_rkey_t	*rkeys;	/* RKey(s) registered at attach time */
#endif
  gasnetc_cq_hndl_t	rcv_cq;
  gasnetc_cq_hndl_t	snd_cq;
  gasnetc_pd_hndl_t	pd;
#if FIREHOSE_VAPI_USE_FMR
  EVAPI_fmr_t		fmr_props;
#endif
  int			hca_index;
  const char		*hca_id;
  gasnetc_hca_cap_t	hca_cap;
#ifdef XXX_BUILD_VAPI
  VAPI_hca_vendor_t	hca_vendor;
#endif
  int			qps; /* qps per peer */
  int			total_qps; /* total over all peers */

  void			*rbuf_alloc;
  gasneti_lifo_head_t	rbuf_freelist;

#ifdef XXX_BUILD_VAPI
  /* Rcv thread */
  EVAPI_compl_handler_hndl_t rcv_handler;
  void			*rcv_thread_priv;
#endif
} gasnetc_hca_t;

/* Keys in a cep, all replicated from other data */
struct gasnetc_cep_keys_ {
#if GASNETC_PIN_SEGMENT
  gasnetc_memreg_t	*seg_reg;
  gasnetc_rkey_t	*rkeys;	/* RKey(s) registered at attach time (== uint32_t) */
#endif
  gasnetc_lkey_t	rcv_lkey;
  gasnetc_lkey_t	snd_lkey;
};

/* Structure for a cep (connection end-point) */
typedef struct {
  /* Read/write fields */
  gasneti_semaphore_t	sq_sema;	/* control in-flight ops (send queue slots) */
  gasneti_semaphore_t	am_sema;	/* control in-flight AM Requests (recv queue slots )*/
  gasneti_semaphore_t	am_unrcvd;	/* ACK coalescing - unmatched rcv buffers */
  gasneti_semaphore_t	*snd_cq_sema_p;	/* control in-flight ops (send completion queue slots) */
  gasneti_weakatomic_t	am_unsent;	/* ACK coalescing - unsent credits */
  char			_pad0[GASNETC_CACHE_PAD(3*sizeof(gasneti_semaphore_t)+
						 sizeof(gasneti_semaphore_t*)+
						 sizeof(gasneti_weakatomic_t))];

  /* Read-only fields */
  struct gasnetc_cep_keys_ keys;
  gasneti_lifo_head_t	*rbuf_freelist;	/* Source of rcv buffers for AMs */
  gasnetc_hca_t		*hca;
  gasnetc_qp_hndl_t	qp_handle;
  gasnetc_hca_hndl_t	hca_handle;
  int			hca_index;
  gasnetc_epid_t	epid;		/* == uint32_t */
  char			_pad1[GASNETC_CACHE_PAD(sizeof(struct gasnetc_cep_keys_) +
						sizeof(gasneti_lifo_head_t*)+
						sizeof(gasnetc_hca_t*)+
						sizeof(gasnetc_qp_hndl_t)+
						sizeof(gasnetc_hca_hndl_t)+
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
extern int gasnetc_pin(gasnetc_hca_t *hca, void *addr, size_t size, gasnetc_acl_t acl, gasnetc_memreg_t *reg);
extern void gasnetc_unpin(gasnetc_memreg_t *reg);
#define gasnetc_unmap(reg)	gasneti_munmap((void *)((reg)->addr), (reg)->len)

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

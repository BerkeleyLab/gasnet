/* $Id: gasnet_core_internal.h,v 1.45 2003/06/29 02:33:04 bonachea Exp $
 * $Date: 2003/06/29 02:33:04 $
 * $Revision: 1.45 $
 * Description: GASNet gm conduit header for internal definitions in Core API
 * Copyright 2002, Christian Bell <csbell@cs.berkeley.edu>
 * Copyright 2002, Dan Bonachea <bonachea@cs.berkeley.edu>
 * Terms of use are as specified in license.txt
 */

#ifndef _GASNET_CORE_INTERNAL_H
#define _GASNET_CORE_INTERNAL_H

#include <gasnet.h>
#include <gasnet_internal.h>
#include <gasnet_handler.h>
#include <gasnet_extended_internal.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <sys/mman.h>	/* mmap */
#include <errno.h>
#if defined(__i386__) && !defined(i386)	/* fix gm. cpu detection */
#define i386
#endif
#ifdef __GNUC__
  #define inline __inline__
  #include <gm.h>
  #undef inline
#else
  #include <gm.h>
#endif
#ifdef LINUX
#include <asm/param.h> /* MAXHOSTNAMELEN */
#else
  #ifdef FREEBSD	 /* sys/param.h defines its own min/max */
  #include <sys/types.h> /* mmap on FreeBSD */
  #undef MIN
  #undef MAX 
  #endif
#include <sys/param.h>
#endif

#ifdef GASNETC_FIREHOSE
#define GASNETC_PINNED_STACK_PAGES	40
#ifndef GASNETC_BUCKET_SIZE
#define GASNETC_BUCKET_SIZE		GASNET_PAGESIZE
#endif
#ifndef GASNETC_BUCKET_SHIFT
#define GASNETC_BUCKET_SHIFT		12
#endif

/* define to the maximum fraction of physical memory to assume to be pinnable.
 * This is only a heuristic used to put a limit on the firehose_M and
 * firehose_Maxvictim parameters (usually set in the environment  */
#define GASNETC_FIREHOSE_PHYSMEM_RATIO		0.7

/* define to the fraction in physical memory to use for the victim fifo if the
 * parameter is undefined in the environment.  As a result, the victim fifo
 * queue will have GASNETC_FIREHOSE_MAXVICTIM_RATIO of physmem while the
 * firehose M parameter will be set to
 * (1-GASNETC_FIREHOSE_MAXVICTIM_RATIO)*_gmc.pinnable_global */
#define GASNETC_FIREHOSE_MAXVICTIM_RATIO	0.15

/* define to be the fraction in physical memory to leave lazily pinned */
#define GASNETC_BUCKET_VICTIM_MAX_SIZE	0.5
#define GASNETC_NUM_BUCKETS(addr,len)	(assert(addr%GASNETC_BUCKET_SIZE==0),\
					(GASNETI_ALIGNUP((len)-1,            \
					GASNETC_BUCKET_SIZE)-(addr))>>       \
					GASNETC_BUCKET_SHIFT)

#define GASNETC_SEGMENT_ALIGN	GASNETC_BUCKET_SIZE
#else
#define GASNETC_SEGMENT_ALIGN	GASNET_PAGESIZE
#endif /* GASNETC_FIREHOSE */

extern gasnet_seginfo_t *gasnetc_seginfo;

#define gasnetc_boundscheck(node,ptr,nbytes)     \
	    gasneti_boundscheck(node,ptr,nbytes,c)

/*  whether or not to use spin-locking for HSL's */
#define GASNETC_HSL_SPINLOCK 1

/* -------------------------------------------------------------------------- */
/* make a GASNet call - if it fails, print error message and return */
#define GASNETC_SAFE(fncall) do {                            \
   int retcode = (fncall);                                   \
   if_pf (gasneti_VerboseErrors && retcode != GASNET_OK) {   \
     char msg[1024];                                         \
     sprintf(msg, "\nGASNet encountered an error: %s(%i)\n", \
        gasnet_ErrorName(retcode), retcode);                 \
     GASNETI_RETURN_ERRFR(RESOURCE, fncall, msg);            \
   }                                                         \
 } while (0)
/* -------------------------------------------------------------------------- */
/* Core locks */
extern gasneti_mutex_t	gasnetc_lock_gm;
extern gasneti_mutex_t	gasnetc_lock_reqpool;
extern gasneti_mutex_t	gasnetc_lock_amreq;
/* -------------------------------------------------------------------------- */
/* Core-specific AMs */
#define GASNETC_HANDLER_BASE  1 /* reserve 1-63 for the core API */
#define _hidx_gasnetc_am_medcopy		(GASNETC_HANDLER_BASE+0) 
#ifdef GASNETC_FIREHOSE
#define _hidx_gasnetc_firehose_move_reqh	(GASNETC_HANDLER_BASE+1) 
#define _hidx_gasnetc_firehose_move_reph	(GASNETC_HANDLER_BASE+2) 
#endif
#define _hidx_					(GASNETC_HANDLER_BASE+)

/* -------------------------------------------------------------------------- */
/* System message types: Remember to add in core_receive.c for lengths */
typedef
enum gasnetc_sysmsg {
	_NO_MSG = 0,
	BARRIER_GATHER = 1,
	BARRIER_NOTIFY = 2,
	EXCHANGE_GATHER = 3,
	EXCHANGE_BROADCAST = 4,
	_LAST_ONE = 5
}
gasnetc_sysmsg_t;

typedef struct gasnetc_bufdesc gasnetc_bufdesc_t;
typedef void (*gasnetc_handler_fn_t)();

gasnetc_bufdesc_t * 	gasnetc_AMRequestPool_block();
gasnetc_sysmsg_t	gasnetc_SysPoll(void *context);

void	gasnetc_tokensend_AMRequest(void *, uint32_t, uint32_t, uint32_t, 
		gm_send_completion_callback_t, void *, uintptr_t);
int	gasnetc_gm_nodes_compare(const void *, const void *);
void	gasnetc_sendbuf_init();
void	gasnetc_sendbuf_finalize();
int	gasnetc_alloc_nodemap(int);
int	gasnetc_gmport_allocate(int *board, int *port);
void	gasnetc_provide_receive_buffers();

/* 3 bootstrapping methods */
int	gasnetc_getconf_conffile();
int	gasnetc_getconf_BNR();
int	gasnetc_getconf_sockets();
int	gasnetc_getconf();

uintptr_t 	gasnetc_getPhysMem();
unsigned long	gasnetc_getenv_numeric(const char *var);
void		gasnetc_am_medcopy(gasnet_token_t token, void *addr, 
				   size_t nbytes, void *dest);
int		gasnetc_AMReplyLongAsyncM(gasnet_token_t token, 
					  gasnet_handler_t handler, 
					  void *source_addr, size_t nbytes,
					  void *dest_addr, int numargs, ...);

void	gasnetc_gm_send_AMSystem_broadcast(void *, size_t, 
		gm_send_completion_callback_t, void *, int);
void	gasnetc_dump_tokens();

void	gasnetc_bootstrapBarrier();
void	gasnetc_bootstrapExchange(void *src, size_t len, void *dest);

/* Provided by RDMA plugins */
extern int	gasnetc_is_pinned  (gasnet_node_t, uintptr_t, size_t);
extern void	gasnetc_done_pinned(gasnet_node_t, uintptr_t, size_t);

/* GM Callback functions */
void	gasnetc_callback_error(gm_status_t status, gasnetc_bufdesc_t *bufd);
void	gasnetc_callback_lo          (struct gm_port *, void *, gm_status_t);
void	gasnetc_callback_lo_bufd     (struct gm_port *, void *, gm_status_t);
void	gasnetc_callback_lo_rdma     (struct gm_port *, void *, gm_status_t);
void	gasnetc_callback_lo_bufd_rdma(struct gm_port *, void *, gm_status_t);

void	gasnetc_callback_hi          (struct gm_port *, void *, gm_status_t);
void	gasnetc_callback_hi_bufd     (struct gm_port *, void *, gm_status_t);
void	gasnetc_callback_hi_rdma     (struct gm_port *, void *, gm_status_t);


/* -------------------------------------------------------------------------- */
/*
 * These are GM tokens, represented by the type
 * gasnetc_token_t (not to be mistaken with gasnet_token_t
 * which is the user interface to the AMReply opaque type)
 *
 */
typedef
struct gasnetc_token {
	int	max;
	int	hi;
	int	lo;
	int	total;
}
gasnetc_token_t;

/* Buffer descriptor.  Each DMA-pinned AM buffer has one
 * of these attached to it. */
#define GASNETC_FLAG_REPLY		0x01
#define GASNETC_FLAG_AMREQUEST_MEDIUM	0x02
#define GASNETC_FLAG_LONG_SEND		0x04
#define GASNETC_FLAG_DMA_SEND		0x08
#define GASNETC_FLAG_EXTENDED_DMA_SEND	0x10
#define GASNETC_BUFDESC_OPT_ISSET(b,o)	((b)->flag & (o))
#define GASNETC_BUFDESC_OPT_SET(b,f)	((b)->flag = ((b)->flag | (f))) 
#define GASNETC_BUFDESC_OPT_UNSET(b,f)	((b)->flag = ((b)->flag & ~(f))) 
#define GASNETC_BUFDESC_OPT_RESET(b)	((b)->flag = 0x00)

struct gasnetc_bufdesc {
	void	*sendbuf;	/* map to buffer */
	short	id;		/* reverse map in bufdesc list */
	uint8_t	flag;		/* bufdesc flags as defined above */

	/* AMReply/AMRequest fields */
	uintptr_t	dest_addr;	/* directed_send address */
	uintptr_t	source_addr;	/* used only in Async AMs */
	gasnet_node_t	node;		/* used only in Async AMs */
	off_t		rdma_off;	/* rdma_off for AMLong */
	uint32_t	rdma_len;	/* rdma_len for AMLong */

	/* AMReply only fields */
	uint16_t		gm_id;
	uint16_t		gm_port;
	uint32_t		len;		/* length for queued sends */
	struct	gasnetc_bufdesc	*next;		/* send FIFO queue */
};

/* Gasnet GM node->id mapping */
typedef
struct gasnetc_gm_nodes {
	uint16_t	id;
	uint16_t	port;
} gasnetc_gm_nodes_t;

/* Gasnet GM id->node mapping */
typedef
struct gasnetc_gm_nodes_rev {
	uint16_t	id;	/* sort key #1 */
	uint16_t	port;	/* sort key #2 */
	gasnet_node_t	node;
} gasnetc_gm_nodes_rev_t;

/* Global GM Core type */
typedef
struct _gasnetc_state {
	gasnetc_token_t		stoks;
	gasnetc_token_t		rtoks;
	int			ReplyCount;
#if GASNETC_RROBIN_BUFFERS > 1
	int			RRobinCount;
#endif
	gasnetc_handler_fn_t	handlers[GASNETC_AM_MAX_HANDLERS];
	gasnetc_gm_nodes_t	*gm_nodes;
	gasnetc_gm_nodes_rev_t	*gm_nodes_rev;

	gasnetc_bufdesc_t	*AMReplyBuf;
	gasnetc_bufdesc_t	*bd_ptr;
	int			bd_list_num;
	void			*dma_bufs;	/* All DMA bufs */

	void			*scratchBuf;	/* for system messages */

	/* AMRequest send Pool */
	int		*reqs_pool;
	int		reqs_pool_max;
	volatile int	reqs_pool_cur;

	/* FIFO overflow send queue */
	gasnetc_bufdesc_t	*fifo_bd_head;
	gasnetc_bufdesc_t	*fifo_bd_tail;

	/* Bootstrap parameters */
	unsigned int	master_port1;	/* GM port for master */
	unsigned int	master_port2;	/* GM port for master */
	unsigned int	my_id;
	unsigned int	my_port;
	unsigned int	my_board;
	unsigned long	job_magic;	/* job magic */
	uintptr_t	pinnable_local;
	uintptr_t	pinnable_global;

	struct sockaddr_in	master_addr;


	void		*reqsbuf;	/* DMAd portion of send buffers */
	struct gm_port	*port;		/* GM port structure */

#ifdef GASNETC_FIREHOSE
	uintptr_t	fh_M;		/* size of M parameter */
	unsigned long	firehoses;	/* number of per-node firehoses */
	uintptr_t	fh_maxvictim;	/* size of MaxVictim parameter */
#endif

} gasnetc_state_t;	

extern gasnetc_state_t	_gmc;

/* -------------------------------------------------------------------------- */
GASNET_INLINE_MODIFIER(gasnetc_portid)
uint16_t
gasnetc_portid(gasnet_node_t node)
{
	assert(node < gasnetc_nodes);
	return _gmc.gm_nodes[node].port;
}

GASNET_INLINE_MODIFIER(gasnetc_nodeid)
uint16_t
gasnetc_nodeid(gasnet_node_t node)
{
	assert(node < gasnetc_nodes);
	return _gmc.gm_nodes[node].id;
}

/* -------------------------------------------------------------------------- */
/* The following function and macro definitions are related to token
 * operations on GM.  It is implicit that the caller *should* always
 * own the GM lock before calling them.
 */
#define GASNETC_TOKEN_HI_NUM()	(_gmc.stoks.max-1 - _gmc.stoks.hi)
#define GASNETC_TOKEN_HI_AVAILABLE() \
				((_gmc.stoks.hi < _gmc.stoks.max-1) && \
				 (_gmc.stoks.total < _gmc.stoks.max))
#define GASNETC_TOKEN_LO_NUM()	(_gmc.stoks.max-1 - _gmc.stoks.lo)
#define GASNETC_TOKEN_LO_AVAILABLE() \
				((_gmc.stoks.lo < _gmc.stoks.max-1) && \
				 (_gmc.stoks.total < _gmc.stoks.max))

/* HI TOKENS
 * acquire() version simply try to get a token, call must be locked around GM
 *           mutex
 * poll()    wraps acquire around a gasnetc_AMPoll loop and returns only when a
 *           hi token could be obtained
 */
GASNET_INLINE_MODIFIER(gasnetc_token_hi_acquire)
int
gasnetc_token_hi_acquire()
{
	gasneti_mutex_assertlocked(&gasnetc_lock_gm);
	if (GASNETC_TOKEN_HI_AVAILABLE()) {
		_gmc.stoks.hi += 1;
		_gmc.stoks.total += 1;
		return 1;
	}
	else {
		return 0;
	}
}

GASNET_INLINE_MODIFIER(gasnetc_token_hi_poll)
void
gasnetc_token_hi_poll()
{
	gasneti_mutex_assertunlocked(&gasnetc_lock_gm);
	while (1) {
		if (GASNETC_TOKEN_HI_AVAILABLE()) {
			gasneti_mutex_lock(&gasnetc_lock_gm);
			if (gasnetc_token_hi_acquire())
				return;
			gasneti_mutex_unlock(&gasnetc_lock_gm);
		}
		gasnetc_AMPoll();
	}
}

GASNET_INLINE_MODIFIER(gasnetc_token_hi_release)
void
gasnetc_token_hi_release()
{
	gasneti_mutex_assertlocked(&gasnetc_lock_gm);
	assert((_gmc.stoks.hi-1 >= 0) && (_gmc.stoks.total-1 >= 0));
	_gmc.stoks.hi -= 1;
	_gmc.stoks.total -= 1;
}

GASNET_INLINE_MODIFIER(gasnetc_token_lo_acquire)
int
gasnetc_token_lo_acquire()
{
	gasneti_mutex_assertlocked(&gasnetc_lock_gm);
	if (GASNETC_TOKEN_LO_AVAILABLE()) {
		_gmc.stoks.lo += 1;
		_gmc.stoks.total += 1;
		return 1;
	}
	else {
		return 0;
	}
}

GASNET_INLINE_MODIFIER(gasnetc_token_lo_poll)
void
gasnetc_token_lo_poll()
{
	gasneti_mutex_assertlocked(&gasnetc_lock_gm);
	while (1) {
		if (gasnetc_token_lo_acquire())
			return;

		gasneti_mutex_unlock(&gasnetc_lock_gm);
		gasnetc_AMPoll();
		gasneti_mutex_lock(&gasnetc_lock_gm);
	}
}

GASNET_INLINE_MODIFIER(gasnetc_token_lo_release)
void
gasnetc_token_lo_release()
{
	gasneti_mutex_assertlocked(&gasnetc_lock_gm);
	assert((_gmc.stoks.lo-1 >= 0) && (_gmc.stoks.total-1 >= 0));
	_gmc.stoks.lo -= 1;
	_gmc.stoks.total -= 1;
}

/* -------------------------------------------------------------------------- */
/*
 * Special case to handle AMReplies from AMRequestMedium, in which
 * case the original bufd must be substituted to the AMReplyBuf.
 * This causes Replies originating from AMRequestMedium to be serialized
 * until they can be sent out.
 */
GASNET_INLINE_MODIFIER(gasnetc_bufdesc_from_token)
gasnetc_bufdesc_t *
gasnetc_bufdesc_from_token(gasnet_token_t token)
{
	gasnetc_bufdesc_t *bufd;

	bufd = (gasnetc_bufdesc_t *) token;
	assert(bufd != NULL);
	assert(bufd->gm_id > 0);
    	GASNETC_BUFDESC_OPT_SET(bufd, GASNETC_FLAG_REPLY);
	if (GASNETC_BUFDESC_OPT_ISSET(bufd, GASNETC_FLAG_AMREQUEST_MEDIUM)) {
		GASNETI_TRACE_PRINTF(C, 
		    ("AMMedium LOCK: (bufd %p -> AMReplyBuf %p)", 
		    (void *) bufd, (void *) _gmc.AMReplyBuf));
		gasneti_mutex_lock(&gasnetc_lock_amreq);
		_gmc.AMReplyBuf->gm_id = bufd->gm_id;
		_gmc.AMReplyBuf->gm_port = bufd->gm_port;
		return _gmc.AMReplyBuf;
	}
	return bufd;
}
/* -------------------------------------------------------------------------- */
/* GM provide receive buffer wrapper */
GASNET_INLINE_MODIFIER(gasnetc_relinquish_AMReply_buffer)
void
gasnetc_relinquish_AMReply_buffer()
{
	gasneti_mutex_assertlocked(&gasnetc_lock_gm);
	_gmc.rtoks.hi--;
	assert(_gmc.rtoks.hi >= 0);
}
GASNET_INLINE_MODIFIER(gasnetc_relinquish_AMRequest_buffer)
void
gasnetc_relinquish_AMRequest_buffer()
{
	gasneti_mutex_assertlocked(&gasnetc_lock_gm);
	_gmc.rtoks.lo--;
	assert(_gmc.rtoks.lo >= 0);
}
GASNET_INLINE_MODIFIER(gasnetc_provide_AMReply_buffer)
void
gasnetc_provide_AMReply_buffer(void *buf)
{
	gasneti_mutex_assertlocked(&gasnetc_lock_gm);
	GASNETC_ASSERT_BUFDESC_PTR(GASNETC_BUFDESC_PTR(buf),buf);
	gm_provide_receive_buffer(_gmc.port, buf, GASNETC_AM_SIZE,
			GM_HIGH_PRIORITY);
	_gmc.rtoks.hi++;
	assert(_gmc.rtoks.hi < _gmc.rtoks.max);
}

GASNET_INLINE_MODIFIER(gasnetc_provide_AMRequest_buffer)
void
gasnetc_provide_AMRequest_buffer(void *buf)
{
	gasneti_mutex_assertlocked(&gasnetc_lock_gm);
	GASNETC_ASSERT_BUFDESC_PTR(GASNETC_BUFDESC_PTR(buf),buf);
	gm_provide_receive_buffer(_gmc.port, buf, GASNETC_AM_SIZE,
			GM_LOW_PRIORITY);
	_gmc.rtoks.lo++;
	assert(_gmc.rtoks.lo < _gmc.rtoks.max);
}
	

/* -------------------------------------------------------------------------- */
/* GM gm_send/gm_directed_send wrapper for AMReply */
GASNET_INLINE_MODIFIER(gasnetc_gm_send_bufd)
void
gasnetc_gm_send_bufd(gasnetc_bufdesc_t *bufd)
{
	uintptr_t			send_ptr;
	uint32_t			len;
	gm_send_completion_callback_t	callback;

	gasneti_mutex_assertlocked(&gasnetc_lock_gm);
	assert(bufd != NULL);
	assert(bufd->sendbuf != NULL);
	assert(bufd->gm_id > 0);

	if (GASNETC_BUFDESC_OPT_ISSET(bufd, GASNETC_FLAG_DMA_SEND)) {
		assert(bufd->dest_addr > 0);
		assert(bufd->node < gasnetc_nodes);
		assert(bufd->rdma_len > 0);
		if (bufd->source_addr > 0)
			send_ptr = bufd->source_addr;
		else
			send_ptr = (uintptr_t) bufd->sendbuf + bufd->rdma_off;

		callback = gasnetc_callback_hi_rdma;
		/*
		if (GASNETC_BUFDESC_OPT_ISSET(bufd, 
		    GASNETC_FLAG_EXTENDED_DMA_SEND))
			callback = gasnetc_callback_hi;
		else
			callback = gasnetc_callback_hi_rdma;
		*/
		GASNETI_TRACE_PRINTF(C, ("gm_directed (%d,%p <- %p,%d bytes)",
		    bufd->node, (void *) bufd->dest_addr, (void *) send_ptr,
		    bufd->rdma_len));
		gm_directed_send_with_callback(_gmc.port, 
		    (void *) send_ptr,
		    (gm_remote_ptr_t) bufd->dest_addr,
		    bufd->rdma_len,
		    GM_HIGH_PRIORITY,
		    (uint32_t) bufd->gm_id,
		    (uint32_t) bufd->gm_port,
		    callback,
		    (void *) bufd);
		return;
	}
	else {
		if (GASNETC_BUFDESC_OPT_ISSET(bufd, GASNETC_FLAG_LONG_SEND)) {
			callback = gasnetc_callback_hi;
			len = bufd->rdma_len;
			send_ptr = (uintptr_t)bufd->sendbuf + 
			    (uintptr_t)bufd->rdma_off;
		}
		else {
			callback = gasnetc_callback_hi_bufd;
			len = bufd->len;
			send_ptr = (uintptr_t) bufd->sendbuf;
		}
	}
	assert(GASNETC_AM_IS_REPLY(*((uint8_t *) bufd->sendbuf)));
	assert(len <= GASNETC_AM_PACKET);
	GASNETI_TRACE_PRINTF(C, ("gm_send (gm id %d <- %p,%d bytes)",
	    (unsigned) bufd->gm_id, (void *) send_ptr, len));
	gm_send_with_callback(_gmc.port, 
		(void *) send_ptr,
		GASNETC_AM_SIZE,
		len,
		GM_HIGH_PRIORITY,
		(uint32_t) bufd->gm_id,
		(uint32_t) bufd->gm_port,
		callback,
		(void *) bufd);
	return;
}

/* GM gm_send/gm_directed_send wrapper for AMRequest */
GASNET_INLINE_MODIFIER(gasnetc_gm_send_AMRequest)
void
gasnetc_gm_send_AMRequest(void *buf, size_t len,
		uint32_t id, uint32_t port, 
		gm_send_completion_callback_t callback,
		void *callback_ptr,
		uintptr_t dest_addr)
{
	gasneti_mutex_assertlocked(&gasnetc_lock_gm);
	assert(buf != NULL);
	assert(id > 0);
	assert(callback != NULL);
	if (callback == gasnetc_callback_lo_bufd_rdma) {
		gasnetc_bufdesc_t	*bufd = (gasnetc_bufdesc_t *)callback_ptr;
		GASNETI_TRACE_PRINTF(C, ("!!! bufd_rdma dest_addr=%p", (void *) bufd->dest_addr));
		GASNETI_TRACE_PRINTF(C, ("!!! bufd = %p", (void *) bufd));
	}

	if (dest_addr > 0) {
		gm_directed_send_with_callback(_gmc.port, 
			buf,
			dest_addr,
			(unsigned int) len,
			GM_LOW_PRIORITY,
			id,
			port,
			callback,
			callback_ptr);
	}
	else {
		assert(GASNETC_AM_IS_REQUEST(*((uint8_t *) buf)));
		assert(len <= GASNETC_AM_PACKET);
		gm_send_with_callback(_gmc.port, 
			buf,
			GASNETC_AM_SIZE,
			(unsigned int) len,
			GM_LOW_PRIORITY,
			id,
			port,
			callback,
			callback_ptr);
	}
}

GASNET_INLINE_MODIFIER(gasnetc_gm_send_AMSystem)
void
gasnetc_gm_send_AMSystem(void *buf, size_t len,
		uint16_t id, uint16_t port, 
		gm_send_completion_callback_t callback,
		void *callback_ptr)
{
	gasneti_mutex_assertlocked(&gasnetc_lock_gm);
	assert(buf != NULL);
	assert(len >= 1); 
	assert(id > 0);
	assert(port > 0 && port < 8);
	assert(callback != NULL);
	assert(len <= gm_max_length_for_size(GASNETC_SYS_SIZE));

	GASNETI_TRACE_PRINTF(C, ("SendAMSystem (%d:%d) index %d", id, port,
		GASNETC_SYS_INDEX(*((uint8_t *)buf))));
	gm_send_with_callback(_gmc.port, buf, GASNETC_SYS_SIZE, len, 
			GM_HIGH_PRIORITY, id, port, callback, callback_ptr);
}

/* -------------------------------------------------------------------------- */
/* FIFO related operations for sending AMReplies */
#define gasnetc_fifo_head()	_gmc.fifo_bd_head

GASNET_INLINE_MODIFIER(gasnetc_fifo_remove)
void
gasnetc_fifo_remove()
{
	gasneti_mutex_assertlocked(&gasnetc_lock_gm);
	assert(_gmc.fifo_bd_head != NULL);
	assert(_gmc.fifo_bd_tail != NULL);

	if (_gmc.fifo_bd_head == _gmc.fifo_bd_tail)
		_gmc.fifo_bd_head = _gmc.fifo_bd_tail = NULL;
	else 
		_gmc.fifo_bd_head = _gmc.fifo_bd_head->next;
}

GASNET_INLINE_MODIFIER(gasnetc_fifo_insert)
void
gasnetc_fifo_insert(gasnetc_bufdesc_t *bufd)
{
	gasneti_mutex_assertlocked(&gasnetc_lock_gm);
	assert(bufd != NULL);
	assert(bufd->gm_id > 0);
	bufd->next = NULL;
	if ((_gmc.fifo_bd_head == NULL) || (_gmc.fifo_bd_tail == NULL))
		_gmc.fifo_bd_head = _gmc.fifo_bd_tail = bufd;
	else {
		_gmc.fifo_bd_tail->next = bufd;
		_gmc.fifo_bd_tail = bufd;
	}
	GASNETI_TRACE_PRINTF(C, ("QUEUEd bufd has flags %d", bufd->flag));
	return;
}

/*
 * Here we relax the MUTEX requirement to sample the number of
 * tokens available.  If there are tokens available without entering
 * the critical section, hopes are that we'll be able to get one.
 */

GASNET_INLINE_MODIFIER(gasnetc_fifo_progress)
void
gasnetc_fifo_progress()
{
	while (gasnetc_fifo_head() && GASNETC_TOKEN_HI_AVAILABLE()) {
		gasneti_mutex_lock(&gasnetc_lock_gm);
		if_pt (gasnetc_token_hi_acquire()) { 
			gasnetc_bufdesc_t *bufd = gasnetc_fifo_head();
			assert(bufd->gm_id > 0);
			GASNETI_TRACE_PRINTF(C, 
			    ("queued to token=%p, buf=%p, flags=%d %hd:%hd", 
			    bufd, bufd->sendbuf, bufd->flag, bufd->gm_id, 
			    bufd->gm_port));
			if (GASNETC_BUFDESC_OPT_ISSET(bufd, 
			    GASNETC_FLAG_EXTENDED_DMA_SEND | 
			    GASNETC_FLAG_DMA_SEND |
			    GASNETC_FLAG_LONG_SEND)) {

				gasnetc_gm_send_bufd(bufd);
				if (GASNETC_BUFDESC_OPT_ISSET(bufd, 
				    GASNETC_FLAG_EXTENDED_DMA_SEND))
					bufd->dest_addr = 0;
				GASNETI_TRACE_PRINTF(C, ("??? sent Reply Payload"));
				GASNETC_BUFDESC_OPT_UNSET(bufd, 
				    GASNETC_FLAG_EXTENDED_DMA_SEND | 
				    GASNETC_FLAG_DMA_SEND |
				    GASNETC_FLAG_LONG_SEND);
			}
			else {
				gasnetc_gm_send_bufd(bufd);
				GASNETI_TRACE_PRINTF(C, ("??? sent Reply Header"));
				gasnetc_fifo_remove();
			}
		}
		else 
			GASNETI_TRACE_PRINTF(C, 
			    ("gasnetc_fifo_progress() lock interrupted.\n"));
		gasneti_mutex_unlock(&gasnetc_lock_gm);
	}
}

/* -------------------------------------------------------------------------- */
/* AM buffer preparation functions */
/* 
 * This writes a Short sized buffer and returns the number of
 * bytes written in total to the buffer
 *
 * |header(1)|handler(1)|pad(2)|args(0..64)
 */
GASNET_INLINE_MODIFIER(gasnetc_write_AMBufferShort)
uint32_t
gasnetc_write_AMBufferShort(	void *buf,
				gasnet_handler_t handler, int numargs, 
				va_list argptr, int req)
{
	uint8_t *pbuf = (uint8_t *)buf;

	GASNETC_ASSERT_AMSHORT(buf, GASNETC_AM_SHORT, handler, numargs, req);
	GASNETC_AMHEADER_WRITE(pbuf, GASNETC_AM_SHORT, numargs, req);
	GASNETC_AMHANDLER_WRITE(&pbuf[1], handler);
	GASNETC_ARGS_WRITE(&pbuf[GASNETC_AM_SHORT_ARGS_OFF], argptr, numargs);
	assert(GASNETC_AM_SHORT_HEADER_LEN(numargs) <= GASNETC_AM_PACKET);
	return GASNETC_AM_SHORT_HEADER_LEN(numargs);
}

/* 
 * This writes a Medium sized buffer and returns the number of
 * bytes written in total to the buffer
 *
 * |header(1)|handler(1)|len(2)|args(0..64)|pad(0/4)|payload(0..?)
 *
 * pad depends on the number of arguments.  If even, the pad will be
 * 4, or else 0.
 */
GASNET_INLINE_MODIFIER(gasnetc_write_AMBufferMedium)
uint32_t
gasnetc_write_AMBufferMedium(	void *buf,
				gasnet_handler_t handler,
				int numargs, va_list argptr, 
				size_t nbytes,
				void *source_addr,
				int req)
{
	uint8_t *pbuf = (uint8_t *)buf;

	GASNETC_ASSERT_AMMEDIUM(buf, GASNETC_AM_MEDIUM, handler, numargs,
				req, nbytes, source_addr);

	GASNETC_AMHEADER_WRITE(pbuf, GASNETC_AM_MEDIUM, numargs, req);
	GASNETC_AMHANDLER_WRITE(&pbuf[1], handler);
	GASNETC_AMLENGTH_WRITE(&pbuf[2], (uint16_t) nbytes);
	GASNETC_ARGS_WRITE(&pbuf[GASNETC_AM_MEDIUM_ARGS_OFF], argptr, numargs);
	GASNETC_AMPAYLOAD_WRITE(&pbuf[GASNETC_AM_MEDIUM_HEADER_LEN(numargs)], 
	    source_addr, nbytes);

	assert(GASNETC_AM_MEDIUM_HEADER_LEN(numargs)+nbytes <= 
			GASNETC_AM_PACKET);
	return GASNETC_AM_MEDIUM_HEADER_LEN(numargs)+nbytes;
}

/* 
 * This writes a Long sized buffer header and returns the number of
 * bytes written 
 *
 * |header(1)|handler(1)|pad(2)|len(4)|dest_addr(4/8)|args(0..64)|payload(0..?)
 *
 * pad depends on the number of arguments.  If even, the pad will be
 * 4, or else 0.
 */
GASNET_INLINE_MODIFIER(gasnetc_write_AMBufferLong)
uint32_t
gasnetc_write_AMBufferLong(	void *buf,
				gasnet_handler_t handler,
				int numargs, va_list argptr, 
				size_t nbytes,
				void *source_addr,
				uintptr_t dest_addr,
				int req)
{
	uint8_t *pbuf = (uint8_t *)buf;

	GASNETC_ASSERT_AMLONG(buf, GASNETC_AM_LONG, handler, numargs,
			req, nbytes, source_addr, dest_addr);
	GASNETC_AMHEADER_WRITE(pbuf, GASNETC_AM_LONG, numargs, req);
	GASNETC_AMHANDLER_WRITE(&pbuf[1], handler);
	GASNETC_AMLENGTH_WRITE4(&pbuf[4], nbytes);
	GASNETC_AMDESTADDR_WRITE(&pbuf[8], dest_addr); 
	GASNETC_ARGS_WRITE(&pbuf[GASNETC_AM_LONG_ARGS_OFF], argptr, numargs);

	assert(GASNETC_AM_LONG_HEADER_LEN(numargs) <= GASNETC_AM_PACKET);
	return GASNETC_AM_LONG_HEADER_LEN(numargs);
}

GASNET_INLINE_MODIFIER(gasnetc_write_AMBufferBulk)
void
gasnetc_write_AMBufferBulk(void *dest, void *src, size_t nbytes)
{
	assert(nbytes >= 0);
	GASNETC_AMPAYLOAD_WRITE(dest, src, nbytes);
	return;
}
/* -------------------------------------------------------------------------- */
#define gasnetc_alloca(nbytes) alloca(nbytes)

GASNET_INLINE_MODIFIER(gasnetc_gm_nodes_search)
gasnet_node_t
gasnetc_gm_nodes_search(uint16_t sender_node_id, uint16_t sender_port_id)
{
	gasnetc_gm_nodes_rev_t	gm_node_sender, *gm_node;

	if_pf (!sender_node_id) GASNETI_RETURN_ERRR(BAD_ARG, 
						"Wrong GM sender_node_id");
	if_pf (sender_port_id < 1 || sender_port_id > 8)
			GASNETI_RETURN_ERRR(BAD_ARG,
						"Wrong GM sender_port_id");
	gm_node_sender.id = sender_node_id;
	gm_node_sender.port = sender_port_id;
	gm_node = (gasnetc_gm_nodes_rev_t *)
		bsearch((void *) &gm_node_sender,
		    (const void *) _gmc.gm_nodes_rev, (size_t) gasnetc_nodes,
		    sizeof(gasnetc_gm_nodes_rev_t), gasnetc_gm_nodes_compare);
	if_pf(gm_node == NULL)
		gasneti_fatalerror("gasnetc_gm_nodes_search() GM id unknown");
	return gm_node->node;
}
/* ------------------------------------------------------------------------------------ */
/* Private access to ReplyLongAsync */
#define gasnetc_AMReplyLongAsync0(token, handler, source_addr, nbytes, token_addr) \
       gasnetc_AMReplyLongAsyncM(token, handler, source_addr, nbytes, token_addr, 0)
#define gasnetc_AMReplyLongAsync1(token, handler, source_addr, nbytes, token_addr, a0) \
       gasnetc_AMReplyLongAsyncM(token, handler, source_addr, nbytes, token_addr, 1, (gasnet_handlerarg_t)a0)
#define gasnetc_AMReplyLongAsync2(token, handler, source_addr, nbytes, token_addr, a0, a1) \
       gasnetc_AMReplyLongAsyncM(token, handler, source_addr, nbytes, token_addr, 2, (gasnet_handlerarg_t)a0, (gasnet_handlerarg_t)a1)
#define gasnetc_AMReplyLongAsync3(token, handler, source_addr, nbytes, token_addr, a0, a1, a2) \
       gasnetc_AMReplyLongAsyncM(token, handler, source_addr, nbytes, token_addr, 3, (gasnet_handlerarg_t)a0, (gasnet_handlerarg_t)a1, (gasnet_handlerarg_t)a2)
#define gasnetc_AMReplyLongAsync4(token, handler, source_addr, nbytes, token_addr, a0, a1, a2, a3) \
       gasnetc_AMReplyLongAsyncM(token, handler, source_addr, nbytes, token_addr, 4, (gasnet_handlerarg_t)a0, (gasnet_handlerarg_t)a1, (gasnet_handlerarg_t)a2, (gasnet_handlerarg_t)a3)

#define gasnetc_AMReplyLongAsync5(token, handler, source_addr, nbytes, token_addr, a0, a1, a2, a3, a4) \
       gasnetc_AMReplyLongAsyncM(token, handler, source_addr, nbytes, token_addr, 5, (gasnet_handlerarg_t)a0, (gasnet_handlerarg_t)a1, (gasnet_handlerarg_t)a2, (gasnet_handlerarg_t)a3, (gasnet_handlerarg_t)a4)
#define gasnetc_AMReplyLongAsync6(token, handler, source_addr, nbytes, token_addr, a0, a1, a2, a3, a4, a5) \
       gasnetc_AMReplyLongAsyncM(token, handler, source_addr, nbytes, token_addr, 6, (gasnet_handlerarg_t)a0, (gasnet_handlerarg_t)a1, (gasnet_handlerarg_t)a2, (gasnet_handlerarg_t)a3, (gasnet_handlerarg_t)a4, (gasnet_handlerarg_t)a5)
#define gasnetc_AMReplyLongAsync7(token, handler, source_addr, nbytes, token_addr, a0, a1, a2, a3, a4, a5, a6) \
       gasnetc_AMReplyLongAsyncM(token, handler, source_addr, nbytes, token_addr, 7, (gasnet_handlerarg_t)a0, (gasnet_handlerarg_t)a1, (gasnet_handlerarg_t)a2, (gasnet_handlerarg_t)a3, (gasnet_handlerarg_t)a4, (gasnet_handlerarg_t)a5, (gasnet_handlerarg_t)a6)
#define gasnetc_AMReplyLongAsync8(token, handler, source_addr, nbytes, token_addr, a0, a1, a2, a3, a4, a5, a6, a7) \
       gasnetc_AMReplyLongAsyncM(token, handler, source_addr, nbytes, token_addr, 8, (gasnet_handlerarg_t)a0, (gasnet_handlerarg_t)a1, (gasnet_handlerarg_t)a2, (gasnet_handlerarg_t)a3, (gasnet_handlerarg_t)a4, (gasnet_handlerarg_t)a5, (gasnet_handlerarg_t)a6, (gasnet_handlerarg_t)a7)

#define gasnetc_AMReplyLongAsync9 (token, handler, source_addr, nbytes, token_addr, a0, a1, a2, a3, a4, a5, a6, a7, a8 ) \
        gasnetc_AMReplyLongAsyncM(token, handler, source_addr, nbytes, token_addr,  9, (gasnet_handlerarg_t)a0, (gasnet_handlerarg_t)a1, (gasnet_handlerarg_t)a2, (gasnet_handlerarg_t)a3, (gasnet_handlerarg_t)a4, (gasnet_handlerarg_t)a5, (gasnet_handlerarg_t)a6, (gasnet_handlerarg_t)a7, (gasnet_handlerarg_t)a8)
#define gasnetc_AMReplyLongAsync10(token, handler, source_addr, nbytes, token_addr, a0, a1, a2, a3, a4, a5, a6, a7, a8, a9) \
        gasnetc_AMReplyLongAsyncM(token, handler, source_addr, nbytes, token_addr, 10, (gasnet_handlerarg_t)a0, (gasnet_handlerarg_t)a1, (gasnet_handlerarg_t)a2, (gasnet_handlerarg_t)a3, (gasnet_handlerarg_t)a4, (gasnet_handlerarg_t)a5, (gasnet_handlerarg_t)a6, (gasnet_handlerarg_t)a7, (gasnet_handlerarg_t)a8, (gasnet_handlerarg_t)a9)
#define gasnetc_AMReplyLongAsync11(token, handler, source_addr, nbytes, token_addr, a0, a1, a2, a3, a4, a5, a6, a7, a8, a9, a10) \
        gasnetc_AMReplyLongAsyncM(token, handler, source_addr, nbytes, token_addr, 11, (gasnet_handlerarg_t)a0, (gasnet_handlerarg_t)a1, (gasnet_handlerarg_t)a2, (gasnet_handlerarg_t)a3, (gasnet_handlerarg_t)a4, (gasnet_handlerarg_t)a5, (gasnet_handlerarg_t)a6, (gasnet_handlerarg_t)a7, (gasnet_handlerarg_t)a8, (gasnet_handlerarg_t)a9, (gasnet_handlerarg_t)a10)
#define gasnetc_AMReplyLongAsync12(token, handler, source_addr, nbytes, token_addr, a0, a1, a2, a3, a4, a5, a6, a7, a8, a9, a10, a11) \
        gasnetc_AMReplyLongAsyncM(token, handler, source_addr, nbytes, token_addr, 12, (gasnet_handlerarg_t)a0, (gasnet_handlerarg_t)a1, (gasnet_handlerarg_t)a2, (gasnet_handlerarg_t)a3, (gasnet_handlerarg_t)a4, (gasnet_handlerarg_t)a5, (gasnet_handlerarg_t)a6, (gasnet_handlerarg_t)a7, (gasnet_handlerarg_t)a8, (gasnet_handlerarg_t)a9, (gasnet_handlerarg_t)a10, (gasnet_handlerarg_t)a11)

#define gasnetc_AMReplyLongAsync13(token, handler, source_addr, nbytes, token_addr, a0, a1, a2, a3, a4, a5, a6, a7, a8, a9, a10, a11, a12) \
        gasnetc_AMReplyLongAsyncM(token, handler, source_addr, nbytes, token_addr, 13, (gasnet_handlerarg_t)a0, (gasnet_handlerarg_t)a1, (gasnet_handlerarg_t)a2, (gasnet_handlerarg_t)a3, (gasnet_handlerarg_t)a4, (gasnet_handlerarg_t)a5, (gasnet_handlerarg_t)a6, (gasnet_handlerarg_t)a7, (gasnet_handlerarg_t)a8, (gasnet_handlerarg_t)a9, (gasnet_handlerarg_t)a10, (gasnet_handlerarg_t)a11, (gasnet_handlerarg_t)a12)
#define gasnetc_AMReplyLongAsync14(token, handler, source_addr, nbytes, token_addr, a0, a1, a2, a3, a4, a5, a6, a7, a8, a9, a10, a11, a12, a13) \
        gasnetc_AMReplyLongAsyncM(token, handler, source_addr, nbytes, token_addr, 14, (gasnet_handlerarg_t)a0, (gasnet_handlerarg_t)a1, (gasnet_handlerarg_t)a2, (gasnet_handlerarg_t)a3, (gasnet_handlerarg_t)a4, (gasnet_handlerarg_t)a5, (gasnet_handlerarg_t)a6, (gasnet_handlerarg_t)a7, (gasnet_handlerarg_t)a8, (gasnet_handlerarg_t)a9, (gasnet_handlerarg_t)a10, (gasnet_handlerarg_t)a11, (gasnet_handlerarg_t)a12, (gasnet_handlerarg_t)a13)
#define gasnetc_AMReplyLongAsync15(token, handler, source_addr, nbytes, token_addr, a0, a1, a2, a3, a4, a5, a6, a7, a8, a9, a10, a11, a12, a13, a14) \
        gasnetc_AMReplyLongAsyncM(token, handler, source_addr, nbytes, token_addr, 15, (gasnet_handlerarg_t)a0, (gasnet_handlerarg_t)a1, (gasnet_handlerarg_t)a2, (gasnet_handlerarg_t)a3, (gasnet_handlerarg_t)a4, (gasnet_handlerarg_t)a5, (gasnet_handlerarg_t)a6, (gasnet_handlerarg_t)a7, (gasnet_handlerarg_t)a8, (gasnet_handlerarg_t)a9, (gasnet_handlerarg_t)a10, (gasnet_handlerarg_t)a11, (gasnet_handlerarg_t)a12, (gasnet_handlerarg_t)a13, (gasnet_handlerarg_t)a14)
#define gasnetc_AMReplyLongAsync16(token, handler, source_addr, nbytes, token_addr, a0, a1, a2, a3, a4, a5, a6, a7, a8, a9, a10, a11, a12, a13, a14, a15) \
        gasnetc_AMReplyLongAsyncM(token, handler, source_addr, nbytes, token_addr, 16, (gasnet_handlerarg_t)a0, (gasnet_handlerarg_t)a1, (gasnet_handlerarg_t)a2, (gasnet_handlerarg_t)a3, (gasnet_handlerarg_t)a4, (gasnet_handlerarg_t)a5, (gasnet_handlerarg_t)a6, (gasnet_handlerarg_t)a7, (gasnet_handlerarg_t)a8, (gasnet_handlerarg_t)a9, (gasnet_handlerarg_t)a10, (gasnet_handlerarg_t)a11, (gasnet_handlerarg_t)a12, (gasnet_handlerarg_t)a13, (gasnet_handlerarg_t)a14, (gasnet_handlerarg_t)a15)

#endif

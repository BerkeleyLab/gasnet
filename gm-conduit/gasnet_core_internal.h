/* $Id: gasnet_core_internal.h,v 1.20.2.1 2002/08/14 06:56:51 csbell Exp $
 * $Date: 2002/08/14 06:56:51 $
 * $Revision: 1.20.2.1 $
 * Description: GASNet gm conduit header for internal definitions in Core API
 * Copyright 2002, Christian Bell <csbell@cs.berkeley.edu>
 * Copyright 2002, Dan Bonachea <bonachea@cs.berkeley.edu>
 */

#ifndef _GASNET_CORE_INTERNAL_H
#define _GASNET_CORE_INTERNAL_H

#include <gasnet.h>
#include <gasnet_internal.h>
#include <gasnet_handler.h>
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
/* FreeBSD links against its own libc_r */
#if defined(GASNETI_THREADS) && !defined(FREEBSD)
#include <pthread.h>
#endif

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
        gasneti_ErrorName(retcode), retcode);                \
     GASNETI_RETURN_ERRFR(RESOURCE, fncall, msg);            \
   }                                                         \
 } while (0)

/* -------------------------------------------------------------------------- */
/* Core-specific AMs */
#define GASNETC_HANDLER_BASE  1 /* reserve 1-99 for the core API */
#define _hidx_gasnetc_am_medcopy		(GASNETC_HANDLER_BASE+0) 
#define _hidx_					(GASNETC_HANDLER_BASE+)

/* -------------------------------------------------------------------------- */
/* System message types */
typedef
enum gasnetc_sysmsg {
	_NO_MSG = 0,
	SBRK_TOP = 1,
	SBRK_BASE = 2,
	SEGMENT_LOCAL = 3,
	SEGMENT_GLOBAL = 4,
	SEGINFO_GATHER = 5,
	SEGINFO_BROADCAST = 6,
	BARRIER_GATHER = 7,
	BARRIER_NOTIFY = 8,
	KILL_NOTIFY = 9,
	KILL_DONE = 10,
	_LAST_ONE = 11 
}
gasnetc_sysmsg_t;

typedef struct gasnetc_bufdesc gasnetc_bufdesc_t;
typedef void (*gasnetc_handler_fn_t)();

/* Forward declaration for miscellaneous functions used by GM core */
void	gasnetc_AM_InitHandler();
int	gasnetc_AM_SetHandler(gasnet_handler_t, gasnetc_handler_fn_t);
int	gasnetc_AM_SetHandlerAny(gasnet_handler_t *, gasnetc_handler_fn_t);

gasnetc_bufdesc_t * 	gasnetc_AMRequestPool_block();
gasnetc_sysmsg_t	gasnetc_SysPoll(void *context);

void	gasnetc_tokensend_AMRequest(void *, uint32_t, uint32_t, uint32_t, 
		gm_send_completion_callback_t, void *, uintptr_t);
int	gasnetc_gm_nodes_compare(const void *, const void *);
int	gasnetc_mmap_segment_search(gasnet_seginfo_t *segment, size_t segsize, 
		size_t offset);
int	gasnetc_mmap_segment(gasnet_seginfo_t *segment);
int	gasnetc_munmap_segment(gasnet_seginfo_t *segment);
void	gasnetc_sendbuf_init();
void	gasnetc_sendbuf_finalize();
void	gasnetc_provide_receive_buffers();
int	gasnetc_gmpiconf_init();

uintptr_t	gasnetc_gather_MaxSegment(void *segbase, uintptr_t segsize);
int		gasnetc_gather_seginfo(gasnet_seginfo_t *segment);
void		gasnetc_am_medcopy(gasnet_token_t token, void *addr, 
				   size_t nbytes, void *dest);

void 	*gasnetc_segment_gather(uintptr_t);
void	gasnetc_gm_send_AMSystem_broadcast(void *, size_t, 
		gm_send_completion_callback_t, void *, int);
void	gasnetc_SysBarrier();

/* GM Callback functions */
void	gasnetc_callback_AMRequest    (struct gm_port *, void *, gm_status_t);
void	gasnetc_callback_AMRequest_NOP(struct gm_port *, void *, gm_status_t);
void	gasnetc_callback_AMReply      (struct gm_port *, void *, gm_status_t);
void	gasnetc_callback_AMReply_NOP  (struct gm_port *, void *, gm_status_t);

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
#define GASNETC_FLAG_REPLY	0x01
#define GASNETC_FLAG_AMREQUEST_MEDIUM	0x02
#define GASNETC_BUFDESC_FLAG_SET(b,f)	((b) = ((b) | (f))) 
#define GASNETC_BUFDESC_FLAG_RESET(b)	((b) = 0x00)

struct gasnetc_bufdesc {
	void	*sendbuf;	/* map to buffer */
	short	id;		/* reverse map in bufdesc list */
	uint8_t	flag;		/* bufdesc flags as defined above */

	/* AMReply/AMRequest fields */
	uintptr_t	dest_addr;	/* directed_send address */
	off_t		rdma_off;	/* rdma_off for AMLong */
	uint32_t	rdma_len;	/* rdma_len for AMLong */

	/* AMReply only fields */
	gm_recv_event_t		*e;		/* GM receive event */
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
	gasnet_seginfo_t	segment_mmap;
	void *			segment_base;
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

	void		*reqsbuf;	/* DMAd portion of send buffers */
	struct gm_port	*port;		/* GM port structure */
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
#ifdef GASNETI_THREADS
extern pthread_mutex_t	_gasnetc_lock_gm;
extern pthread_mutex_t	_gasnetc_lock_reqfifo;
extern pthread_mutex_t	_gasnetc_lock_amreq;
#ifdef DEBUG
#define _GASNETC_MUTEX_LOCK(m) do { \
		int ret = pthread_mutex_lock(&m); assert(!ret); } while (0)
#define _GASNETC_MUTEX_UNLOCK(m) do { \
		int ret = pthread_mutex_unlock(&m); assert(!ret); } while (0)
#else
#define _GASNETC_MUTEX_LOCK(m) pthread_mutex_lock(&m)
#define _GASNETC_MUTEX_UNLOCK(m) pthread_mutex_unlock(&m)
#endif
#define GASNETC_GM_MUTEX_LOCK	_GASNETC_MUTEX_LOCK(_gasnetc_lock_gm)
#define GASNETC_GM_MUTEX_UNLOCK _GASNETC_MUTEX_UNLOCK(_gasnetc_lock_gm)
#define GASNETC_REQUEST_POOL_MUTEX_LOCK \
		_GASNETC_MUTEX_LOCK(_gasnetc_lock_reqfifo)
#define GASNETC_REQUEST_POOL_MUTEX_UNLOCK \
		_GASNETC_MUTEX_UNLOCK(_gasnetc_lock_reqfifo)
#define GASNETC_AMMEDIUM_REQUEST_MUTEX_LOCK \
		_GASNETC_MUTEX_LOCK(_gasnetc_lock_amreq)
#define GASNETC_AMMEDIUM_REQUEST_MUTEX_UNLOCK \
		_GASNETC_MUTEX_UNLOCK(_gasnetc_lock_amreq)
#else
#define GASNETC_GM_MUTEX_LOCK
#define GASNETC_GM_MUTEX_UNLOCK
#define GASNETC_REQUEST_POOL_MUTEX_LOCK
#define GASNETC_REQUEST_POOL_MUTEX_UNLOCK
#define GASNETC_AMMEDIUM_REQUEST_MUTEX_LOCK
#define GASNETC_AMMEDIUM_REQUEST_MUTEX_UNLOCK
#endif 

/* -------------------------------------------------------------------------- */
/* The following function and macro definitions are related to token
 * operations on GM.  It is implicit that the caller *should* always
 * own the GM lock before calling them.
 *
 * XXX More debug/lock metadata to be added
 */
#define GASNETC_TOKEN_HI_NUM()	(_gmc.stoks.max-1 - _gmc.stoks.hi)
#define GASNETC_TOKEN_HI_AVAILABLE() \
				((_gmc.stoks.hi < _gmc.stoks.max-1) && \
				 (_gmc.stoks.total < _gmc.stoks.max))
#define GASNETC_TOKEN_LO_NUM()	(_gmc.stoks.max-1 - _gmc.stoks.lo)
#define GASNETC_TOKEN_LO_AVAILABLE() \
				((_gmc.stoks.lo < _gmc.stoks.max-1) && \
				 (_gmc.stoks.total < _gmc.stoks.max))

GASNET_INLINE_MODIFIER(gasnetc_token_hi_acquire)
int
gasnetc_token_hi_acquire()
{
	if (GASNETC_TOKEN_HI_AVAILABLE()) {
		_gmc.stoks.hi += 1;
		_gmc.stoks.total += 1;
		return 1;
	}
	else {
		return 0;
	}
}

GASNET_INLINE_MODIFIER(gasnetc_token_hi_release)
void
gasnetc_token_hi_release()
{
	assert((_gmc.stoks.hi-1 >= 0) && (_gmc.stoks.total-1 >= 0));
	_gmc.stoks.hi -= 1;
	_gmc.stoks.total -= 1;
}

/* gasnetc_tokenc_lo_acquire() is solely represented in
 * gasnetc_AMRequestPool_block();
 */

GASNET_INLINE_MODIFIER(gasnetc_token_lo_release)
void
gasnetc_token_lo_release()
{
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
	gasnetc_bufdesc_t *bufd, *bufd_temp;

	bufd = (gasnetc_bufdesc_t *) token;
	assert(bufd != NULL);
	assert(bufd->e != NULL);
    	GASNETC_BUFDESC_FLAG_SET(bufd->flag, GASNETC_FLAG_REPLY);
	if (bufd->flag & GASNETC_FLAG_AMREQUEST_MEDIUM) {
    		GASNETC_AMMEDIUM_REQUEST_MUTEX_LOCK; 
		_gmc.AMReplyBuf->e = bufd->e;
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
	_gmc.rtoks.hi--;
	assert(_gmc.rtoks.hi >= 0);
}
GASNET_INLINE_MODIFIER(gasnetc_relinquish_AMRequest_buffer)
void
gasnetc_relinquish_AMRequest_buffer()
{
	_gmc.rtoks.lo--;
	assert(_gmc.rtoks.lo >= 0);
}
GASNET_INLINE_MODIFIER(gasnetc_provide_AMReply_buffer)
void
gasnetc_provide_AMReply_buffer(void *buf)
{
	GASNETC_ASSERT_BUFDESC_PTR(GASNETC_BUFDESC_PTR(buf),buf);
	GASNETI_TRACE_PRINTF(C, ("provide_receive_buffer_hi = %p", buf));
	gm_provide_receive_buffer(_gmc.port, buf, GASNETC_AM_SIZE,
			GM_HIGH_PRIORITY);
	_gmc.rtoks.hi++;
	/*
	GASNETI_TRACE_PRINTF(C, ("rtoks.hi = %d, buf=%p", _gmc.rtoks.hi, buf));
	*/
	assert(_gmc.rtoks.hi < _gmc.rtoks.max);
}

GASNET_INLINE_MODIFIER(gasnetc_provide_AMRequest_buffer)
void
gasnetc_provide_AMRequest_buffer(void *buf)
{
	GASNETC_ASSERT_BUFDESC_PTR(GASNETC_BUFDESC_PTR(buf),buf);
	GASNETI_TRACE_PRINTF(C, ("provide_receive_buffer_lo = %p", buf));
	gm_provide_receive_buffer(_gmc.port, buf, GASNETC_AM_SIZE,
			GM_LOW_PRIORITY);
	_gmc.rtoks.lo++;
	/*
	GASNETI_TRACE_PRINTF(C, ("rtoks.lo = %d, bufd = %p", _gmc.rtoks.lo,
	    buf));
	 */
	assert(_gmc.rtoks.lo < _gmc.rtoks.max);
}
	

/* -------------------------------------------------------------------------- */
/* GM gm_send/gm_directed_send wrapper for AMReply */
GASNET_INLINE_MODIFIER(gasnetc_gm_send_bufd)
void
gasnetc_gm_send_bufd(gasnetc_bufdesc_t *bufd)
{
	uintptr_t	send_ptr;
	uint32_t	len;
	gm_send_completion_callback_t	callback;

	assert(bufd != NULL);
	assert(bufd->sendbuf != NULL);
	assert(bufd->len > 0);
	assert(bufd->gm_id > 0);

	if (bufd->rdma_len > 0) {
		send_ptr = (uintptr_t)bufd->sendbuf + (uintptr_t)bufd->rdma_off;
		len = bufd->rdma_len;
		bufd->rdma_off = 0;
		callback = gasnetc_callback_AMReply_NOP;
	}
	else {
		send_ptr = (uintptr_t) bufd->sendbuf;
		len = bufd->len;
		callback = gasnetc_callback_AMReply;
	}

	if (bufd->dest_addr > 0) {
		gm_directed_send_with_callback(_gmc.port, 
			(void *) send_ptr,
			bufd->dest_addr,
			len,
			GM_HIGH_PRIORITY,
			(uint32_t) bufd->gm_id,
			(uint32_t) bufd->gm_port,
			callback,
			(void *) bufd);
	}
	else {
		assert(GASNETC_AM_IS_REPLY(*((uint8_t *) bufd->sendbuf)));
		assert(bufd->len <= GASNETC_AM_PACKET);
		gm_send_with_callback(_gmc.port, 
			(void *) send_ptr,
			GASNETC_AM_SIZE,
			len,
			GM_HIGH_PRIORITY,
			(uint32_t) bufd->gm_id,
			(uint32_t) bufd->gm_port,
			callback,
			(void *) bufd);
	}
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
	assert(buf != NULL);
	assert(id > 0);
	assert(callback != NULL);

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
	assert(buf != NULL);
	assert(len >= 1); 
	assert(id > 0);
	assert(port > 0 && port < 8);
	assert(callback != NULL);

	GASNETI_TRACE_PRINTF(C, ("SendAMSystem (%d:%d) index %d", id, port,
		GASNETC_SYS_INDEX(*((uint8_t *)buf))));
	gm_send_with_callback(_gmc.port, buf, GASNETC_SYS_SIZE, len, 
			GM_HIGH_PRIORITY, id, port, callback, callback_ptr);
}

/* -------------------------------------------------------------------------- */
/* Allocate segments */
GASNET_INLINE_MODIFIER(gasnetc_segment_alloc)
void *
gasnetc_segment_alloc(void *segbase, size_t segsize)
{
	void	*ptr;
	int	flags;

	GASNETI_TRACE_PRINTF(C, 
	    ("mmap(0x%x, %d)\n", (uintptr_t) segbase, segsize) );
	ptr = mmap(segbase, segsize, (PROT_READ|PROT_WRITE), 
		(MAP_ANON | MAP_PRIVATE | MAP_FIXED), -1, 0);
	if (ptr == MAP_FAILED) {
		perror("mmap failed: ");
		gasneti_fatalerror("mmap failed at 0x%x for size %d\n",
			(uintptr_t) segbase, segsize);
	}
	if (segbase != ptr) 
		gasneti_fatalerror("mmap moved from 0x%x to 0x%x for size %d\n",
			(uintptr_t) segbase, (uintptr_t) ptr, segsize);
	return ptr;
}

/* Allocate segments */
GASNET_INLINE_MODIFIER(gasnetc_segment_register)
void
gasnetc_segment_register(void *segbase, size_t segsize)
{
	void		*ptr;
	gm_status_t	status;

	GASNETI_TRACE_PRINTF(C, 
	    ("gm_register_memory(0x%x, %d)\n", (uintptr_t) segbase, segsize));
	if (gm_register_memory(_gmc.port, segbase, (gm_size_t) segsize) !=
			GM_SUCCESS)
		gasneti_fatalerror("gm_register_memory failed at 0x%x for size "
			"%d\n", (uintptr_t) segbase, segsize);
	return;
}

/* -------------------------------------------------------------------------- */
/* FIFO related operations for sending AMReplies */
#define gasnetc_fifo_head()	_gmc.fifo_bd_head

GASNET_INLINE_MODIFIER(gasnetc_fifo_remove)
void
gasnetc_fifo_remove()
{
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
	/* Insert at end of queue and update head/tail */
	assert(bufd != NULL);
	assert(bufd->gm_id > 0);
	bufd->next = NULL;
	if ((_gmc.fifo_bd_head == NULL) || (_gmc.fifo_bd_tail == NULL))
		_gmc.fifo_bd_head = _gmc.fifo_bd_tail = bufd;
	else {
		_gmc.fifo_bd_tail->next = bufd;
		_gmc.fifo_bd_tail = bufd;
	}
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
		GASNETC_GM_MUTEX_LOCK;
		if_pt (gasnetc_token_hi_acquire()) { 
			gasnetc_bufdesc_t *bufd = gasnetc_fifo_head();
			assert(bufd->gm_id > 0);
			GASNETI_TRACE_PRINTF(C, ("queued to token=%p, buf=%p %hd:%hd", 
			    bufd, bufd->sendbuf, bufd->gm_id, bufd->gm_port));
			gasnetc_gm_send_bufd(bufd);
			if (bufd->rdma_len > 0) {
				GASNETI_TRACE_PRINTF(C, ("??? sent Reply Payload"));
				bufd->rdma_len = 0;
				bufd->dest_addr = 0;
			}
			else {
				GASNETI_TRACE_PRINTF(C, ("??? sent Reply Header"));
				gasnetc_fifo_remove();
			}
		}
		else 
			GASNETI_TRACE_PRINTF(C, 
			    ("gasnetc_fifo_progress() lock interrupted.\n"));
		GASNETC_GM_MUTEX_UNLOCK;
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
/* Few utility functions which are nice inlined */
GASNET_INLINE_MODIFIER(gasnetc_alloca)
void *
gasnetc_alloca(size_t nbytes)
{
	void *ptr;
	if ((ptr = alloca(nbytes)) == NULL)
		gasneti_fatalerror("alloca(%d) failed\n", nbytes);
	return ptr;
}

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
			(const void *) _gmc.gm_nodes_rev,
			(size_t) gasnetc_nodes,
			sizeof(gasnetc_gm_nodes_rev_t),
			gasnetc_gm_nodes_compare);
	if_pf(gm_node == NULL)
		gasneti_fatalerror("gasnetc_gm_nodes_search() GM id unknown");
	return gm_node->node;
}
#endif

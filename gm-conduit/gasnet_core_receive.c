/* $Id: gasnet_core_receive.c,v 1.23.2.1 2003/01/18 22:07:47 csbell Exp $
 * $Date: 2003/01/18 22:07:47 $
 * $Revision: 1.23.2.1 $
 * Description: GASNet GM conduit Implementation
 * Copyright 2002, Christian Bell <csbell@cs.berkeley.edu>
 * Copyright 2002, Dan Bonachea <bonachea@cs.berkeley.edu>
 */
#include <gasnet_core_internal.h>

extern int gasnetc_init_done;
extern int gasnetc_attach_done;
#if defined(GASNETC_FIREHOSE) || defined(GASNETC_TURKEY)
extern void gasnete_fifo_progress();
#else
#define gasnete_fifo_progress()
#endif

/* Three processing functions called from gasnetc_poll() */
void		 gasnetc_process_AMRequest(uint8_t *, gm_recv_event_t *);
void		 gasnetc_process_AMReply(uint8_t *, gm_recv_event_t *);
gasnetc_sysmsg_t gasnetc_process_AMSystem(uint8_t *, gm_recv_event_t *, void *);

const
struct {
	const char	msg[32];
	size_t		len;
} gasnetc_sysmsg_types[] =
	{ { "", 0 }, 
	  { "SBRK_TOP", 2*sizeof(uintptr_t) },
	  { "SBRK_BASE", 2*sizeof(uintptr_t) },
	  { "SEGMENT_LOCAL", 3*sizeof(uintptr_t) },
	  { "SEGMENT_GLOBAL", 3*sizeof(uintptr_t) },
	  { "SEGINFO_GATHER", 2*sizeof(uintptr_t) },
	  { "SEGINFO_BROADCAST", 0 },
	  { "BARRIER_GATHER", 1 },
	  { "BARRIER_NOTIFY", 1 },
	  { "KILL_NOTIFY", 1 },
	  { "KILL_DONE", 1 }
	};

/* 
 * make progress in the receive queue
 */
int
gasnetc_AMPoll()
{
	gm_recv_event_t	*e;
	int		fast = 0;
	uint8_t		*ptr = NULL;

	gasnetc_fifo_progress();

	gasnete_fifo_progress();	/* Entry for extended API */
	gasneti_mutex_lock(&gasnetc_lock_gm);
	e = gm_receive(_gmc.port);

	switch (gm_ntohc(e->recv.type)) {
		case GM_NO_RECV_EVENT:
			gasneti_mutex_unlock(&gasnetc_lock_gm);
			return GASNET_OK;

		case GM_FAST_HIGH_RECV_EVENT:	/* handle AMReplies */
		case GM_FAST_HIGH_PEER_RECV_EVENT:
			fast = 1;
		case GM_HIGH_RECV_EVENT:
			gasnetc_relinquish_AMReply_buffer();
			assert(gm_ntoh_u32(e->recv.length) <= GASNETC_AM_PACKET);
			gasneti_mutex_unlock(&gasnetc_lock_gm);
			ptr = (uint8_t *) GASNETC_GM_RECV_PTR(e,fast);
			assert(GASNETC_AM_IS_REPLY(*ptr));
			gasnetc_process_AMReply(ptr, e);
			return GASNET_OK;

		case GM_FAST_RECV_EVENT:	/* handle AMRequests */
		case GM_FAST_PEER_RECV_EVENT:
			fast = 1;
		case GM_RECV_EVENT:
			gasnetc_relinquish_AMRequest_buffer();
			assert(gm_ntoh_u32(e->recv.length) <= GASNETC_AM_PACKET);
			gasneti_mutex_unlock(&gasnetc_lock_gm);
			ptr = (uint8_t *) GASNETC_GM_RECV_PTR(e, fast);
			if (GASNETC_AM_IS_SYSTEM(*ptr)) {
				gasnetc_process_AMSystem(ptr, e, NULL);
				gasnetc_provide_AMRequest_buffer
				    (gm_ntohp(e->recv.buffer));
			}
			else {
				assert(GASNETC_AM_IS_REQUEST(*ptr));
				gasnetc_process_AMRequest(ptr, e);
			}
			return GASNET_OK;

		default:
			gm_unknown(_gmc.port, e);
	}
	gasneti_mutex_unlock(&gasnetc_lock_gm);

	gasnetc_fifo_progress();
	return GASNET_OK;
}

/* 
 * SysPoll is a specific AMPoll only used during bootstrap.  It does
 * not require any locking since the client guarentees that only one
 * thread calls the function.  Returned is the type of system message
 * delivered with context filled in with appropriate data if applicable
 * to the type of system message.  It is up to the client to treat the
 * received system message as expected or not (although some intelligence
 * is put into the processing call - see gasnetc_process_AMSystem
 */
gasnetc_sysmsg_t
gasnetc_SysPoll(void *context)
{
	gm_recv_event_t	*e;
	int		fast = 0, error = 0;
	uint8_t		*ptr;

	gasneti_mutex_assertlocked(&gasnetc_lock_gm);
	/* should register some GM alarm to make sure we wait for
	 * a bounded amount of time
	 */
	while (1) {
		e = gm_receive(_gmc.port);

		switch (gm_ntohc(e->recv.type)) {
			case GM_NO_RECV_EVENT:
				break;

			case GM_FAST_HIGH_RECV_EVENT:
			case GM_FAST_HIGH_PEER_RECV_EVENT:
			case GM_FAST_RECV_EVENT:
			case GM_FAST_PEER_RECV_EVENT:
				fast = 1;
			case GM_HIGH_RECV_EVENT:
				ptr = (uint8_t *) GASNETC_GM_RECV_PTR(e,fast);
				if (GASNETC_AM_IS_SYSTEM(*ptr)) 
					return gasnetc_process_AMSystem
					    (ptr, e, context);
				else
					error = 1;
				break;
			case GM_RECV_EVENT:
				ptr = (uint8_t *) GASNETC_GM_RECV_PTR(e,fast);
				error = 1;
				break;

			default:
				gm_unknown(_gmc.port, e);
				if ((void *) context == (void *) -1)
					return _NO_MSG;
		}
		if (error == 0)
			continue;
		else
			gasneti_fatalerror("gasnetc_SysPoll: unexpected "
			    "Message 0x%x of type %hd", *ptr,
			    (uint16_t) gm_ntoh_u8(e->recv.type));
	}
}

/* 
 * Three processing functions called from gasnetc_receive 
 * From gasnetc_AMPoll()
 * <e> contains the event as returned by gm_receive()
 * <fast> tells if a message or buffer is available
 */

void
gasnetc_process_AMRequest(uint8_t *ptr, gm_recv_event_t *e)
{
	gasnetc_bufdesc_t	*bufd;
	uint8_t			handler_idx, numargs;
	uintptr_t		dest_addr;
	uint32_t		len;
	int32_t			*argptr;

	/* match the buffer provided by GM with our list of bufdesc_t */
	bufd = (gasnetc_bufdesc_t *) 
		GASNETC_BUFDESC_PTR(gm_ntohp(e->recv.buffer));
	GASNETC_ASSERT_BUFDESC_PTR(bufd, gm_ntohp(e->recv.buffer));
	assert((bufd)->sendbuf == gm_ntohp(e->recv.buffer));
    	bufd->dest_addr = bufd->rdma_len = bufd->rdma_off = bufd->len = 0;
	bufd->gm_id = gm_ntoh_u16(e->recv.sender_node_id);
	bufd->gm_port = (uint16_t) gm_ntoh_u8(e->recv.sender_port_id);
	handler_idx = ptr[1];
	numargs = GASNETC_AM_NUMARGS(*ptr);
	len = (uint32_t) gm_ntoh_u32(e->recv.length);
	assert(len >= 2); /* minimum AM message */
	assert(numargs <= GASNETC_AM_MAX_ARGS); /* maximum AM args */

	switch (GASNETC_AM_TYPE(*ptr)) {
		case GASNETC_AM_SHORT:
			GASNETC_TRACE_SHORT(AMRecv, RequestShort, 
			    gasnetc_gm_nodes_search(bufd->gm_id, bufd->gm_port),
			    bufd, handler_idx, GASNETC_AM_NUMARGS(*ptr));
			argptr = (int32_t *) &ptr[GASNETC_AM_SHORT_ARGS_OFF];
			GASNETC_RUN_HANDLER_SHORT(_gmc.handlers[handler_idx],
			    (void *) bufd, argptr, numargs);
			break;
		case GASNETC_AM_MEDIUM:
			GASNETC_TRACE_MEDIUM(AMRecv, RequestMedium, 
			    gasnetc_gm_nodes_search(bufd->gm_id, bufd->gm_port),
			    bufd, handler_idx, GASNETC_AM_NUMARGS(*ptr), 
			    ptr + GASNETC_AM_MEDIUM_HEADER_LEN(numargs), 
			    len - GASNETC_AM_MEDIUM_HEADER_LEN(numargs));
			argptr = (int32_t *) &ptr[GASNETC_AM_MEDIUM_ARGS_OFF];
			GASNETC_BUFDESC_OPT_SET(bufd, 
			    GASNETC_FLAG_AMREQUEST_MEDIUM);
			GASNETI_TRACE_PRINTF(C, ("running handler %p %p",
			    (void *) _gmc.handlers[handler_idx],
			    (void *) _gmc.handlers[handler_idx+1]));
			GASNETC_RUN_HANDLER_MEDIUM(_gmc.handlers[handler_idx],
			    (void *) bufd, argptr, numargs, 
			    (void *) (ptr + GASNETC_AM_MEDIUM_HEADER_LEN(numargs)), 
			    len - GASNETC_AM_MEDIUM_HEADER_LEN(numargs));
			break;
		case GASNETC_AM_LONG:
			dest_addr = (uintptr_t) ptr[8];
			GASNETC_TRACE_LONG(AMRecv, RequestLong, 
			    gasnetc_gm_nodes_search(bufd->gm_id, bufd->gm_port),
			    bufd, handler_idx, GASNETC_AM_NUMARGS(*ptr), 0, 
			    dest_addr, len-GASNETC_AM_LONG_HEADER_LEN(numargs));
			argptr = (int32_t *) &ptr[GASNETC_AM_LONG_ARGS_OFF];
			len = *((uint32_t *) &ptr[4]);
			GASNETC_RUN_HANDLER_LONG(_gmc.handlers[handler_idx],
			    (void *) bufd, argptr, numargs, dest_addr, len);
			break;
		default:
			gasneti_fatalerror("AMRequest type unknown 0x%x",
			    GASNETC_AM_TYPE(*ptr));
	}

	/* Unlock the AMMEDIUM_REQUEST lock if it was required */
	if (GASNETC_BUFDESC_OPT_ISSET(bufd, GASNETC_FLAG_REPLY)) {
		gasneti_mutex_lock(&gasnetc_lock_gm);
		_gmc.ReplyCount++;
		gasneti_mutex_unlock(&gasnetc_lock_gm);
		if (GASNETC_BUFDESC_OPT_ISSET(bufd, 
		    GASNETC_FLAG_AMREQUEST_MEDIUM)) {
			GASNETI_TRACE_PRINTF(C, 
			    ("AMMedium UNLOCK: (AMReplyBuf %p <- bufd %p)", 
			    (void *) _gmc.AMReplyBuf, (void *) bufd));
			/* The received buffer becomes the new AMReplyBuf */
			_gmc.AMReplyBuf = bufd;
			GASNETC_BUFDESC_OPT_UNSET(bufd, 
			    GASNETC_FLAG_AMREQUEST_MEDIUM);
			gasneti_mutex_unlock(&gasnetc_lock_amreq);
		}
		/*
		GASNETC_BUFDESC_OPT_UNSET(bufd, 
		    GASNETC_FLAG_REPLY | GASNETC_FLAG_AMREQUEST_MEDIUM);
		 */
	}
	/* Always give the buffer back if no AMReply was called */
	else {
		GASNETC_BUFDESC_OPT_RESET(bufd);
		gasneti_mutex_lock(&gasnetc_lock_gm);
		gasnetc_provide_AMRequest_buffer(gm_ntohp(e->recv.buffer));
		gasneti_mutex_unlock(&gasnetc_lock_gm);
	}
	return;
}

void
gasnetc_process_AMReply(uint8_t *ptr, gm_recv_event_t *e)
{
	gasnetc_bufdesc_t	*bufd;
	uint8_t			handler_idx, numargs;
	uintptr_t		dest_addr;
	uint32_t		len;
	int32_t			*argptr;

	/* match the buffer provided by GM with our list of bufdesc_t */
	bufd = (gasnetc_bufdesc_t *) 
		GASNETC_BUFDESC_PTR(gm_ntohp(e->recv.buffer));
	GASNETC_ASSERT_BUFDESC_PTR(bufd, gm_ntohp(e->recv.buffer));
	assert((bufd)->sendbuf == gm_ntohp(e->recv.buffer));
    	bufd->dest_addr = bufd->rdma_off = bufd->len = 0;
	bufd->gm_id = gm_ntoh_u16(e->recv.sender_node_id);
	bufd->gm_port = (uint16_t) gm_ntoh_u8(e->recv.sender_port_id);
	handler_idx = ptr[1];
	numargs = GASNETC_AM_NUMARGS(*ptr);
	len = (uint32_t) gm_ntoh_u32(e->recv.length);
	assert(len >= 2); /* minimum AM message */
	assert(numargs <= GASNETC_AM_MAX_ARGS); /* maximum AM args */

	switch (GASNETC_AM_TYPE(*ptr)) {
		case GASNETC_AM_SHORT:
			GASNETC_TRACE_SHORT(AMRecv, ReplyShort, 
			    gasnetc_gm_nodes_search(bufd->gm_id, bufd->gm_port),
			    bufd, handler_idx, GASNETC_AM_NUMARGS(*ptr));
			argptr = (int32_t *) &ptr[GASNETC_AM_SHORT_ARGS_OFF];
			GASNETC_RUN_HANDLER_SHORT(_gmc.handlers[handler_idx],
			    (void *) bufd, argptr, numargs);
			break;
		case GASNETC_AM_MEDIUM:
			GASNETC_TRACE_MEDIUM(AMRecv, ReplyMedium, 
			    gasnetc_gm_nodes_search(bufd->gm_id, bufd->gm_port),
			    bufd, handler_idx, GASNETC_AM_NUMARGS(*ptr),
			    ptr + GASNETC_AM_MEDIUM_HEADER_LEN(numargs), 
			    len - GASNETC_AM_MEDIUM_HEADER_LEN(numargs)); 
			argptr = (int32_t *) &ptr[GASNETC_AM_MEDIUM_ARGS_OFF];
			GASNETC_RUN_HANDLER_MEDIUM(_gmc.handlers[handler_idx],
			    (void *) bufd, argptr, numargs,
			    (void *)(ptr + GASNETC_AM_MEDIUM_HEADER_LEN(numargs)), 
			    len - GASNETC_AM_MEDIUM_HEADER_LEN(numargs)); 
			break;
		case GASNETC_AM_LONG:
			dest_addr = (uintptr_t) ptr[8];
			len = *((uint32_t *) &ptr[4]);
			GASNETC_TRACE_LONG(AMRecv, ReplyLong, 
			    gasnetc_gm_nodes_search(bufd->gm_id, bufd->gm_port),
			    bufd, handler_idx, GASNETC_AM_NUMARGS(*ptr), 0, 
			    dest_addr, len);
			argptr = (int32_t *) &ptr[GASNETC_AM_LONG_ARGS_OFF];
			GASNETC_RUN_HANDLER_LONG(_gmc.handlers[handler_idx],
			    (void *) bufd, argptr, numargs, dest_addr, len);
			break;
		default:
			gasneti_fatalerror("AMReply type unknown 0x%x",
			    GASNETC_AM_TYPE(*ptr));
	}

	/* Simply provide the buffer back to GM */
	gasneti_mutex_lock(&gasnetc_lock_gm);
	gasnetc_provide_AMReply_buffer(gm_ntohp(e->recv.buffer));
	gasneti_mutex_unlock(&gasnetc_lock_gm);
	return;
}

/* This process function may either be called from SysPoll or
 * from AMPoll.  The former handles messages during bootstrap
 * while the latter handles post-bootstrap messages that may
 * be received asynchronously, such as kill messages or possibly
 * ulterior optimized barrier messages
 */

gasnetc_sysmsg_t
gasnetc_process_AMSystem(uint8_t *ptr, gm_recv_event_t *e, void *context)
{
	gasnetc_sysmsg_t	sysmsg;

	GASNETC_SYSHEADER_READ(ptr, sysmsg); 
	if_pf (sysmsg == 0 || sysmsg >= _LAST_ONE)
		gasneti_fatalerror("AMSystem: unknown message 0x%x", *ptr);

	if_pf (gasnetc_sysmsg_types[sysmsg].len > 0 &&
	    gasnetc_sysmsg_types[sysmsg].len != gm_ntoh_u32(e->recv.length))
		gasneti_fatalerror("AMSystem: message %x (%s) has length %d "
		    "instead of %d", sysmsg, gasnetc_sysmsg_types[sysmsg].msg,
		    gm_ntoh_u32(e->recv.length), 
		    gasnetc_sysmsg_types[sysmsg].len);

	assert(context != (void *) -1);
	switch (sysmsg) {
		case SBRK_TOP:
			if_pf (gasnetc_init_done || gasnet_mynode != 0)
				gasneti_fatalerror("AMSystem SBRK_TOP: already"
					" initialized or mynode is not 0");
			assert(context != NULL);
			*((uintptr_t *)context) = *((uintptr_t *)ptr + 1);
			GASNETI_TRACE_PRINTF(C, ("SBRK_TOP %4hd = 0x%x",
			    gasnetc_gm_nodes_search(
			    gm_ntoh_u16(e->recv.sender_node_id),
			    gm_ntoh_u8(e->recv.sender_port_id)),
			    *((uintptr_t *)context) ));
			break;
		case SBRK_BASE:
			if_pf (gasnetc_init_done || gasnetc_mynode == 0)
				gasneti_fatalerror("AMSystem SBRK_BASE: already"
					" initialized");
			assert(context != NULL);
			*((uintptr_t *)context) = *((uintptr_t *)ptr + 1);
			GASNETI_TRACE_PRINTF(C, ("SBRK_BASE 0x%x",
				*((uintptr_t *) context) ));
			break;
		case SEGMENT_LOCAL:
			if_pf (gasnetc_init_done || gasnetc_mynode != 0)
				gasneti_fatalerror("AMSystem SEGMENT_LOCAL: "
				    "already initialized");
			assert(context != NULL);
			{
				uintptr_t *pptr = (uintptr_t *)ptr;
				gasnet_seginfo_t *seginfo =
					(gasnet_seginfo_t *)context;
				seginfo->addr = (void *) pptr[1];
				seginfo->size = (uintptr_t) pptr[2];
				GASNETI_TRACE_PRINTF(C, ("SEGMENT_LOCAL: "
				    "0x%x %d", (uintptr_t) seginfo->addr, 
				    seginfo->size) );
			}
			break;
		case SEGMENT_GLOBAL:
			if_pf (gasnetc_init_done || gasnetc_mynode == 0)
				gasneti_fatalerror("AMSystem SEGMENT_GLOBAL: "
				    "already initialized or mynode is 0");
			assert(context != NULL);
			{
				uintptr_t *pptr = (uintptr_t *)ptr;
				gasnet_seginfo_t *seginfo = 
					(gasnet_seginfo_t *)context;
				seginfo->addr = (void *) pptr[1];
				seginfo->size = (uintptr_t) pptr[2];
				GASNETI_TRACE_PRINTF(C, ("SEGMENT_GLOBAL: "
				    "0x%x %d", (uintptr_t) seginfo->addr, 
				    seginfo->size) );
			}
			break;
		case SEGINFO_GATHER:
			if_pf (gasnetc_attach_done || gasnetc_mynode != 0)
				gasneti_fatalerror("AMSystem SEGINFO_GATHER: "
				    "already attached or mynode is not 0");
			assert(context != NULL);
			{
				gasnet_seginfo_t *seginfo = 
				    (gasnet_seginfo_t *)context;
				gasnet_node_t node = 
				    gasnetc_gm_nodes_search(
				    gm_ntoh_u16(e->recv.sender_node_id),
				    gm_ntoh_u8(e->recv.sender_port_id));
				uintptr_t segsize = (uintptr_t) 
				    *((uintptr_t *)ptr+1);
				if_pf ((size_t) segsize < 0)
					gasneti_fatalerror("SEGINFO_GATHER: "
					    "segsize too large for mmap");
				seginfo[node].size = segsize;
				GASNETI_TRACE_PRINTF(C,("SEGINFO_GATHER: "
				    "%d> %d bytes", node, seginfo[node].size) );
			}
			break;
		case SEGINFO_BROADCAST:
			if_pf (gasnetc_attach_done || gasnetc_mynode == 0)
				gasneti_fatalerror(
				    "AMSystem SEGINFO_BROADCAST: already "
				    "attached or mynode is 0");
			assert(context != NULL);
			{
				int	i;
				gasnet_seginfo_t *seginfo = 
				    (gasnet_seginfo_t *)context;
				uintptr_t *segptr = (uintptr_t *)ptr+1;
				assert(
				    gm_ntoh_u32(
				        e->recv.length)-sizeof(uintptr_t) >=
				        gasnetc_nodes*sizeof(uintptr_t));
				for (i = 0; i < gasnetc_nodes; i++) {
					if (i == gasnetc_mynode &&
					    seginfo[i].size != segptr[i]) {
						gasneti_fatalerror(
						    "SEGINFO_BROADCAST: "
						    "segsize don't match "
						    "locally");
					}
					else {
						seginfo[i].size = segptr[i];
						GASNETI_TRACE_PRINTF(C,
						  ("SEGINFO_BROADCAST %d: %d\n",
						   i, segptr[i]) );
					}
				}
			}
			break;
		case BARRIER_GATHER:
			if_pf (gasnetc_mynode != 0)
				gasneti_fatalerror("AMSystem BARRIER_GATHER "
					"can only be done at node 0");
			assert(context != NULL);
			(*((int *) context))++;
			GASNETI_TRACE_PRINTF(C, ("BARRIER_GATHER %4hd = %d",
			    gasnetc_gm_nodes_search(
			    gm_ntoh_u16(e->recv.sender_node_id),
			    gm_ntoh_u8(e->recv.sender_port_id)),
			    *((int *) context) ));
			break;
		case BARRIER_NOTIFY:
			if_pf (gasnetc_mynode == 0)
				gasneti_fatalerror("AMSystem BARRIER_NOTIFY "
					"can only be done from node 0");
			GASNETI_TRACE_PRINTF(C, ("BARRIER_NOTIFY") );
			break;
		default:
			gasneti_fatalerror("AMSystem process fatal!");
	}
	return sysmsg;
}

/* -------------------------------------------------------------------------- */
/* The next few functions are related to GM callback processing and _all_
 * own the GM_MUTEX while running.
 */

/* callback_error processes information if we don't have GM_SUCCESS
 * For now, we wont support any resending for GM_SEND_TIMED_OUT,
 * so this function is limited to simply printing error messages
 * and aborting.  Interpretation of these messages comes from GM's
 * mpich code
 */
void
gasnetc_callback_error(gm_status_t status, gasnetc_bufdesc_t *bufd)
{
	char reason[128];
	char dest_msg[64];

	assert(status != GM_SUCCESS);	/* function is for errors only */

	if (bufd != NULL)
		snprintf(dest_msg, 63, "AMReply to %hd port %hd",
				bufd->gm_id, bufd->gm_port);
	else
		snprintf(dest_msg, 63, "AMRequest failed");

	switch (status) {
		case GM_SEND_TIMED_OUT:
			snprintf(reason, 127, "GM timed out. . %s", dest_msg);
		case GM_SEND_TARGET_PORT_CLOSED:
			snprintf(reason, 127, 
			    "Target node is down or exited. .%s", dest_msg);
		case GM_SEND_TARGET_NODE_UNREACHABLE:
			snprintf(reason, 127, 
			    "Target unknown. Check mapper/cables. . %s",
			    dest_msg);
		case GM_SEND_REJECTED:
		case GM_SEND_DROPPED:
			snprintf(reason, 127,
			    "Target node rejected the send. . %s", dest_msg);
		default:
			snprintf(reason, 127,
			    "Unknown GM error. . %s", dest_msg);
	}
	gasneti_fatalerror("gasnetc_callback: %s", reason);
	return;
}

/*
 * Callback functions for hi and lo token bounded functions.  Since these
 * functions are called from gm_unknown(), they already own a GM lock
 */

GASNET_INLINE_MODIFIER(gasnetc_callback_generic)
void
gasnetc_callback_generic_inner(struct gm_port *p, void *context, gm_status_t status)
{
	gasnetc_bufdesc_t	*bufd;
	
	/* zero out bufdesc for future receive/send */
	bufd = (gasnetc_bufdesc_t *) context;
	bufd->dest_addr = 0;
	bufd->source_addr = 0;
	bufd->rdma_off = 0;
	bufd->rdma_len = 0;
	bufd->len = 0;
	GASNETC_BUFDESC_OPT_RESET(bufd);
	assert(bufd->sendbuf != NULL);
	gasneti_mutex_assertlocked(&gasnetc_lock_gm);

	/* Either give the buffer back to the receive queue if some replies
	 * where in flight or give it back to the AMRequest pool 
	 */ 
	#if GASNETC_RROBIN_BUFFERS > 1
	if (_gmc.RRobinCount == 0) {
		if (_gmc.reqs_pool_cur < _gmc.reqs_pool_max)
			gasnetc_provide_request_pool(bufd);
		else {
			if_pf (_gmc.ReplyCount < 1)
				gasneti_fatalerror("provide buffer overflow");
			_gmc.ReplyCount--;
			GASNETI_TRACE_PRINTF(C, ("gasnetc_callback:\t"
			    "buffer (%p) to AMRequest queue (ReplyCount=%d)",
			    (void *) bufd->sendbuf, _gmc.ReplyCount) );
			gasnetc_provide_AMRequest_buffer(bufd->sendbuf);
		}
		_gmc.RRobinCount++;
	}
	else {
	#endif
		if (_gmc.ReplyCount > 0)  {
			_gmc.ReplyCount--;
			GASNETI_TRACE_PRINTF(C, ("gasnetc_callback:\t"
			    "buffer (%p) to AMRequest queue (ReplyCount=%d)",
			    (void *) bufd->sendbuf, _gmc.ReplyCount) );
			gasnetc_provide_AMRequest_buffer(bufd->sendbuf);
		}
		else {
			gasneti_mutex_lock(&gasnetc_lock_reqpool);
			if (_gmc.reqs_pool_cur < _gmc.reqs_pool_max) {
				_gmc.reqs_pool_cur++;
				_gmc.reqs_pool[_gmc.reqs_pool_cur] = bufd->id;
				GASNETI_TRACE_PRINTF(C, ("gasnetc_callback:\t"
	    			    "buffer to Pool (%d/%d) ReplyCount=%d", 
				    _gmc.reqs_pool_cur, _gmc.reqs_pool_max, 
				    _gmc.ReplyCount) );
			}
			gasneti_mutex_unlock(&gasnetc_lock_reqpool);
		}

	#if GASNETC_RROBIN_BUFFERS > 1
			if (_gmc.RRobinCount == GASNETC_RROBIN_BUFFERS-1)
				_gmc.RRobinCount = 0;
			else
				_gmc.RRobinCount++;
	}
	#endif
}

extern void
gasnetc_callback_ambuffer(struct gm_port *p, void *context, gm_status_t status) {
	gasneti_mutex_assertlocked(&gasnetc_lock_gm);
	gasnetc_callback_generic_inner(p, context, status);
	return;
}

/* Callbacks for AMRequest/AMReply functions
 * All of them relinquish a send token but the _NOP callbacks do *not* give a
 * buffer back to the system - they are used to issue two sends out of the same
 * buffer (ie: directed_send+send for an AMLongReply).
 */

void
gasnetc_callback_lo(struct gm_port *p, void *c, gm_status_t status)
{
	gasneti_mutex_assertlocked(&gasnetc_lock_gm);
	if_pf (status != GM_SUCCESS)
		gasnetc_callback_error(status, NULL);
	gasnetc_token_lo_release();
	GASNETI_TRACE_PRINTF(C, ("callback_lo stoks.lo = %d", _gmc.stoks.lo));
}

void
gasnetc_callback_lo_bufd(struct gm_port *p, void *ctx, gm_status_t status)
{
	gasneti_mutex_assertlocked(&gasnetc_lock_gm);
	if_pf (status != GM_SUCCESS)
		gasnetc_callback_error(status, NULL);
	/* Provide the buffer back */
	gasnetc_callback_generic_inner(p, ctx, status);
	gasnetc_token_lo_release();
	GASNETI_TRACE_PRINTF(C, ("callback_lo_bufd stoks.lo = %d", _gmc.stoks.lo));
}

void
gasnetc_callback_lo_rdma(struct gm_port *p, void *ctx, gm_status_t status)
{
	gasnetc_bufdesc_t	*bufd;
	gasneti_mutex_assertlocked(&gasnetc_lock_gm);
	if_pf (status != GM_SUCCESS)
		gasnetc_callback_error(status, NULL);
	bufd = (gasnetc_bufdesc_t *)ctx;
	assert(bufd->node < gasnetc_nodes);
	assert(bufd->dest_addr > 0);
	assert(bufd->rdma_len > 0);
	/* tell core plugins that the rdma is done */
	if (bufd->source_addr != 0)
		gasnetc_done_pinned(gasnetc_mynode, bufd->source_addr, 
		    bufd->rdma_len);
	gasnetc_done_pinned(bufd->node, bufd->dest_addr, bufd->rdma_len);
	gasnetc_token_lo_release();
	GASNETI_TRACE_PRINTF(C, 
	    ("callback_lo_rdma stoks.lo = %d", _gmc.stoks.lo));
}

void
gasnetc_callback_lo_bufd_rdma(struct gm_port *p, void *ctx, gm_status_t status)
{
	gasnetc_bufdesc_t	*bufd;
	gasneti_mutex_assertlocked(&gasnetc_lock_gm);
	if_pf (status != GM_SUCCESS)
		gasnetc_callback_error(status, NULL);
	bufd = (gasnetc_bufdesc_t *) ctx;
	assert(bufd->node < gasnetc_nodes);
	GASNETI_TRACE_PRINTF(C, ("!!! callback bufd = %p", (void *) bufd));
	assert(bufd->dest_addr > 0);
	assert(bufd->rdma_len > 0);
	if (bufd->source_addr != 0)
		gasnetc_done_pinned(gasnetc_mynode, bufd->source_addr, 
		    bufd->rdma_len);
	gasnetc_done_pinned(bufd->node, bufd->dest_addr, bufd->rdma_len);
	gasnetc_callback_generic_inner(p, ctx, status);
	gasnetc_token_lo_release();
	GASNETI_TRACE_PRINTF(C, 
	    ("callback_lo_bufd_rdma stoks.lo = %d", _gmc.stoks.lo));
}

void
gasnetc_callback_hi(struct gm_port *p, void *ctx, gm_status_t status)
{
	gasneti_mutex_assertlocked(&gasnetc_lock_gm);
	if_pf (status != GM_SUCCESS)
		gasnetc_callback_error(status, ctx);
	gasnetc_token_hi_release();
	GASNETI_TRACE_PRINTF(C, ("callback_hi stoks.hi = %d", _gmc.stoks.hi));
}

void
gasnetc_callback_hi_bufd(struct gm_port *p, void *ctx, gm_status_t status)
{
	gasneti_mutex_assertlocked(&gasnetc_lock_gm);
	if_pf (status != GM_SUCCESS)
		gasnetc_callback_error(status, ctx);
	gasnetc_callback_generic_inner(p, ctx, status);
	gasnetc_token_hi_release();
	GASNETI_TRACE_PRINTF(C, 
	    ("callback_hi_bufd stoks.hi = %d", _gmc.stoks.hi));
}

void
gasnetc_callback_hi_rdma(struct gm_port *p, void *ctx, 
				  gm_status_t status)
{
	gasnetc_bufdesc_t	*bufd;
	gasneti_mutex_assertlocked(&gasnetc_lock_gm);
	if_pf (status != GM_SUCCESS)
		gasnetc_callback_error(status, ctx);
	bufd = (gasnetc_bufdesc_t *) ctx;
	assert(bufd->node < gasnetc_nodes);
	assert(bufd->rdma_len > 0);
	if (bufd->source_addr != 0) {
		GASNETI_TRACE_PRINTF(C, 
		    ("callback_hi_rdma: local done_pinned(%d, %p, %d)",
		    gasnetc_mynode, (void *)bufd->source_addr,
		    bufd->rdma_len));

		gasnetc_done_pinned(gasnetc_mynode, bufd->source_addr, 
		    bufd->rdma_len);
	}
	if (bufd->dest_addr != 0) {
		GASNETI_TRACE_PRINTF(C, 
		    ("callback_hi_rdma: remote done_pinned(%d, %p, %d)",
		    bufd->node, (void *)bufd->source_addr, bufd->rdma_len));
		gasnetc_done_pinned(bufd->node, bufd->dest_addr, bufd->rdma_len);
	}
	gasnetc_token_hi_release();
	GASNETI_TRACE_PRINTF(C, 
	    ("callback_hi_rdma stoks.hi = %d", _gmc.stoks.hi));
}

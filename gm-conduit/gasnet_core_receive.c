/* $Id: gasnet_core_receive.c,v 1.35 2004/01/19 11:13:58 bonachea Exp $
 * $Date: 2004/01/19 11:13:58 $
 * $Revision: 1.35 $
 * Description: GASNet GM conduit Implementation
 * Copyright 2002, Christian Bell <csbell@cs.berkeley.edu>
 * Copyright 2002, Dan Bonachea <bonachea@cs.berkeley.edu>
 * Terms of use are as specified in license.txt
 *
 * This file contains the GASNet polling loop, gasnetc_AMPoll, as well as core
 * callback functions for communications calls issued to GM.
 */
#include <gasnet_core_internal.h>
#include <firehose.h>

/* Three processing functions called from gasnetc_poll() */
void		 gasnetc_process_AMRequest(gasnetc_bufdesc_t *bufd);
void		 gasnetc_process_AMReply(gasnetc_bufdesc_t *bufd);
void		 gasnetc_process_AMSystem(gasnetc_bufdesc_t *bufd);


GASNET_INLINE_MODIFIER(gasnetc_node_lookup)
gasnet_node_t
gasnetc_node_lookup(uint16_t sender_node_id, uint16_t sender_port_id)
{
	gasnetc_gm_nodes_rev_t	gm_node_sender, *gm_node;

	if_pf (!sender_node_id) GASNETI_RETURN_ERRR(BAD_ARG, 
						"Wrong GM sender_node_id");
	if_pf (sender_port_id < 1 || sender_port_id >= GASNETC_GM_MAXPORTS)
			GASNETI_RETURN_ERRR(BAD_ARG,
						"Wrong GM sender_port_id");
	gm_node_sender.id = sender_node_id;
	gm_node_sender.port = sender_port_id;
	gm_node = (gasnetc_gm_nodes_rev_t *)
		bsearch((void *) &gm_node_sender,
		    (const void *) _gmc.gm_nodes_rev, (size_t) gasnetc_nodes,
		    sizeof(gasnetc_gm_nodes_rev_t), gasnetc_gm_nodes_compare);
	if_pf(gm_node == NULL)
		gasneti_fatalerror("gasnetc_node_lookup() GM id unknown");
	return gm_node->node;
}

gasnetc_bufdesc_t *
gasnetc_bufdesc_from_event(gm_recv_event_t *e)
{
	gasnetc_bufdesc_t *bufd;

	gasneti_mutex_assertlocked(&gasnetc_lock_gm);

	bufd = GASNETC_BUFDESC_PTR(gm_ntohp(e->recv.buffer));
	GASNETC_ASSERT_BUFDESC_PTR(bufd, gm_ntohp(e->recv.buffer));

	bufd->dest_addr   = 0;
	bufd->payload_off = 0;
	bufd->len         = (uint32_t) gm_ntoh_u32(e->recv.length);
	bufd->gm_id       = gm_ntoh_u16(e->recv.sender_node_id);
	bufd->gm_port     = (uint16_t) gm_ntoh_u8(e->recv.sender_port_id);
	bufd->node	  = gasnetc_node_lookup(bufd->gm_id, bufd->gm_port);
	bufd->ran_reply   = NULL;

	gasneti_assert(bufd->node < gasnetc_nodes);
	gasneti_assert(bufd->len <= GASNETC_AM_PACKET);

	return bufd;
}

/* 
 * make progress in the receive queue
 *
 * Notes of interest:  We always memorize the message if the message if fast
 */

int
gasnetc_AMPoll()
{
	gm_recv_event_t	*e;
	int		fast = 0;
	int		did_reply = 0;
	int		locked_AMMedBuf = 0;
	uint8_t		*ptr = NULL;
	uint8_t		tag;

	gasnetc_bufdesc_t       *bufd;

	if (!gm_receive_pending(_gmc.port))
		return GASNET_OK;

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
			gasnetc_relinquish_AMReply_token();
			bufd = gasnetc_bufdesc_from_event(e);
			gasneti_assert(BUFD_ISSTATE(bufd) == BUFD_S_GMREPLY);
			BUFD_SETSTATE(bufd, BUFD_S_USED);
			tag = gm_ntoh_u8 (e->recv.tag);
			gasneti_assert(tag > 0 ? tag == 1 : 1);
			if (fast)
				gm_memorize_message(
				    gm_ntohp(e->recv.message),
				    bufd->buf,
				    gm_ntoh_u32(e->recv.length));

			gasneti_mutex_unlock(&gasnetc_lock_gm);
			gasneti_assert(GASNETC_AM_IS_REPLY(*((uint8_t *) bufd->buf)));
			gasnetc_process_AMReply(bufd);

			gasneti_mutex_lock(&gasnetc_lock_gm);
			gasnetc_provide_AMReply(bufd);
			break;

		case GM_FAST_RECV_EVENT:	/* handle AMRequests */
		case GM_FAST_PEER_RECV_EVENT:
			fast = 1;
		case GM_RECV_EVENT:
			gasnetc_relinquish_AMRequest_token();
			bufd = gasnetc_bufdesc_from_event(e);
			gasneti_assert(BUFD_ISSTATE(bufd) == BUFD_S_GMREQ);
			BUFD_SETSTATE(bufd, BUFD_S_USED);
			gasneti_assert(BUFD_ISSTATE(bufd) == BUFD_S_USED);
			if (fast)
				gm_memorize_message(
				    gm_ntohp(e->recv.message),
				    bufd->buf,
				    gm_ntoh_u32(e->recv.length));
			tag = gm_ntoh_u8 (e->recv.tag);
			gasneti_assert(tag > 0 ? tag == 2 : 1);

			/* Run handlers concurrently */
			gasneti_mutex_unlock(&gasnetc_lock_gm);
			
			ptr = (uint8_t *) bufd->buf;
			if_pf (GASNETC_AM_IS_SYSTEM(ptr[0])) {
				gasnetc_process_AMSystem(bufd);

				gasneti_mutex_lock(&gasnetc_lock_gm);
				gasnetc_provide_AMRequest(bufd);
			}
			else {
				gasneti_assert(GASNETC_AM_IS_REQUEST(ptr[0]));
				bufd->ran_reply = &did_reply;
				bufd->locked_AMMedBuf = &locked_AMMedBuf;
				gasnetc_process_AMRequest(bufd);

				gasneti_mutex_lock(&gasnetc_lock_gm);
				if (!did_reply)
					gasnetc_provide_AMRequest(bufd);
				else if (locked_AMMedBuf)
					gasnetc_provide_AMMedium(bufd);
			}
			break;
		default:
			gm_unknown(_gmc.port, e);
	}

	gasnetc_fifo_progress();

	gasneti_mutex_unlock(&gasnetc_lock_gm);

	firehose_poll();

	return GASNET_OK;
}

/* 
 * Process a received reply without holding the core lock.  This essentially
 * allows concurrent handler execution from a threaded client.
 *
 * This function returns non-zero if the buffer is to be provided back to GM.
 * If the buffer we used to receive the request in was reused to send a reply,
 * it cannot be given back to GM just yet.
 *
 */
void
gasnetc_process_AMRequest(gasnetc_bufdesc_t *bufd)
{
	uint8_t			handler_idx, numargs, *ptr;
	uintptr_t		dest_addr;
	int32_t			*argptr, len;

	ptr = (uint8_t *) bufd->buf;
	len = bufd->len;
	handler_idx = ptr[1];
	numargs = GASNETC_AM_NUMARGS(*ptr);

	gasneti_assert(len >= 2); /* minimum AM message */
	gasneti_assert(GASNETC_AM_IS_REQUEST(*ptr));
	gasneti_assert(numargs <= GASNETC_AM_MAX_ARGS); /* maximum AM args */

	switch (GASNETC_AM_TYPE(*ptr)) {
		case GASNETC_AM_SHORT:
			GASNETC_TRACE_SHORT(AMRecv, RequestShort, bufd->node,
			    bufd, handler_idx, GASNETC_AM_NUMARGS(*ptr));
			argptr = (int32_t *) &ptr[GASNETC_AM_SHORT_ARGS_OFF];
			GASNETC_RUN_HANDLER_SHORT(_gmc.handlers[handler_idx],
			    (void *) bufd, argptr, numargs);
			break;

		case GASNETC_AM_MEDIUM:
			GASNETC_TRACE_MEDIUM(AMRecv, RequestMedium, bufd->node,
			    bufd, handler_idx, GASNETC_AM_NUMARGS(*ptr), 
			    ptr + GASNETC_AM_MEDIUM_HEADER_LEN(numargs), 
			    len - GASNETC_AM_MEDIUM_HEADER_LEN(numargs));
			argptr = (int32_t *) &ptr[GASNETC_AM_MEDIUM_ARGS_OFF];

			BUFD_SET(bufd, BUFD_REQMEDIUM);
			GASNETC_RUN_HANDLER_MEDIUM(_gmc.handlers[handler_idx],
			    (void *) bufd, argptr, numargs, 
			    (void *) (ptr + GASNETC_AM_MEDIUM_HEADER_LEN(numargs)), 
			    len - GASNETC_AM_MEDIUM_HEADER_LEN(numargs));
			break;

		case GASNETC_AM_LONG:
			dest_addr = *((uintptr_t *) &ptr[8]);
			GASNETC_TRACE_LONG(AMRecv, RequestLong, bufd->node,
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

	gasneti_mutex_assertunlocked(&gasnetc_lock_gm);

	return;
}

void
gasnetc_process_AMReply(gasnetc_bufdesc_t *bufd)
{
	uint8_t			handler_idx, numargs, *ptr;
	uintptr_t		dest_addr;
	uint32_t		len;
	int32_t			*argptr;

	ptr = (uint8_t *) bufd->buf;
	len = bufd->len;
	handler_idx = ptr[1];
	numargs = GASNETC_AM_NUMARGS(*ptr);

	gasneti_assert(len >= 2); /* minimum AM message */
	gasneti_assert(GASNETC_AM_IS_REPLY(*ptr));
	gasneti_assert(numargs <= GASNETC_AM_MAX_ARGS); /* maximum AM args */

	switch (GASNETC_AM_TYPE(*ptr)) {
		case GASNETC_AM_SHORT:
			GASNETC_TRACE_SHORT(AMRecv, ReplyShort, bufd->node,
			    bufd, handler_idx, GASNETC_AM_NUMARGS(*ptr));
			argptr = (int32_t *) &ptr[GASNETC_AM_SHORT_ARGS_OFF];
			GASNETC_RUN_HANDLER_SHORT(_gmc.handlers[handler_idx],
			    (void *) bufd, argptr, numargs);
			break;

		case GASNETC_AM_MEDIUM:
			GASNETC_TRACE_MEDIUM(AMRecv, ReplyMedium, bufd->node, 
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
			dest_addr = *((uintptr_t *) &ptr[8]);
			len = *((uint32_t *) &ptr[4]);
			GASNETC_TRACE_LONG(AMRecv, ReplyLong, bufd->node,
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

	return;
}

extern uint8_t  gasnetc_bootstrapGather_buf[2][4096];
volatile int	gasnetc_bootstrapGather_recvd[2];
volatile int	gasnetc_bootstrapBroadcast_recvd[2];
volatile int	gasnetc_bootstrapGather_sent;
volatile int	gasnetc_bootstrapBroadcast_sent;

void
gasnetc_process_AMSystem(gasnetc_bufdesc_t *bufd)
{
	uint8_t		*hdr, msg;
	uint8_t		*payload;
	size_t		len, paylen = 0;
	uint16_t	phase;

	len = bufd->len;
	hdr = (uint8_t *) bufd->buf;
	payload = hdr + 4;

	msg = GASNETC_SYSHEADER_READ(hdr);

	switch (msg) {
		case GASNETC_SYS_GATHER:
			phase = *((uint16_t *) hdr + 1);
			gasneti_assert(phase == 0 || phase == 1);
			if (len > 4) {
				paylen = len - 4;
				gasneti_assert(bufd->node*paylen+paylen < 4096);

				memcpy(gasnetc_bootstrapGather_buf[phase]
				    + bufd->node*paylen, payload, paylen);
			}

			gasnetc_bootstrapGather_recvd[phase]++;
			#if 0
			printf("0> %s phase from node %d (counter=%d)\n",
					phase ? "odd" : "even", bufd->node,
					gasnetc_bootstrapGather_recvd[phase]);
			#endif
			
			GASNETI_TRACE_PRINTF(C, 
			    ("AMSystem Gather Received (node=%d,msg=0x%x,"
			     "paylen=%d)", bufd->node, msg, (int)paylen));
			break;

		case GASNETC_SYS_BROADCAST:
			phase = *((uint16_t *) hdr + 1);
			if (len > 4) {
				paylen = len - 4;
				memcpy(gasnetc_bootstrapGather_buf[phase],
				    payload, paylen);
			}
			gasnetc_bootstrapBroadcast_recvd[phase]++;
			gasneti_assert(
			    gasnetc_bootstrapBroadcast_recvd[phase] == 1);
			#if 0
			printf("%d> GOT phase %s BROADCAST!\n", gasnetc_mynode,
					phase ? "odd" : "even");
			#endif
			GASNETI_TRACE_PRINTF(C, 
			    ("AMSystem Broadcast Received (node=%d,msg=0x%x,"
			     "paylen=%d)", bufd->node, msg, (int)paylen));
			break;

		default:
			gasneti_fatalerror(
			    "Received unknown system message: 0x%x",
			    GASNETC_SYSHEADER_READ(hdr));
			break;
	}
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
gasnetc_callback_error(gm_status_t status, const char *dest_msg)
{
	char reason[128];

	gasneti_assert(status != GM_SUCCESS);	/* function is for errors only */

	switch (status) {
		case GM_SEND_TIMED_OUT:
			snprintf(reason, 127, "GM timed out. . %s", dest_msg);
			break;
		case GM_SEND_TARGET_PORT_CLOSED:
			snprintf(reason, 127, 
			    "Target node is down or exited. .%s", dest_msg);
			break;
		case GM_SEND_TARGET_NODE_UNREACHABLE:
			snprintf(reason, 127, 
			    "Target unknown. Check mapper/cables. . %s",
			    dest_msg);
			break;
		case GM_SEND_REJECTED:
		case GM_SEND_DROPPED:
			snprintf(reason, 127,
			    "Target node rejected the send. . %s", dest_msg);
			break;
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
extern void
gasnetc_callback_ambuffer(struct gm_port *p, void *context, 
		          gm_status_t status) 
{
	gasnetc_bufdesc_t	*bufd;

	gasneti_mutex_assertlocked(&gasnetc_lock_gm);
	if_pf (status != GM_SUCCESS)
		gasnetc_callback_error(status, "AM Put Buffer");

	bufd = (gasnetc_bufdesc_t *) context;
	gasnetc_provide_AMRequestPool(bufd);
	return;
}

/* Callbacks for AMRequest/AMReply functions */
void
gasnetc_callback_lo(struct gm_port *p, void *ctx, gm_status_t status)
{
	gasnetc_bufdesc_t	*bufd = (gasnetc_bufdesc_t *) ctx;

	gasneti_mutex_assertlocked(&gasnetc_lock_gm);

	if_pf (status != GM_SUCCESS)
		gasnetc_callback_error(status, "AMRequest");

	GASNETI_TRACE_PRINTF(C, 
	    ("GM Send Lo Callback: stoks.lo=%d, bufd %s", _gmc.stoks.lo,
	     bufd == NULL ? "absent" : "to AMRequestPool"));

	if_pt (bufd != NULL)
		gasnetc_provide_AMRequestPool(bufd);

	gasnetc_token_lo_release();
}

void
gasnetc_callback_hi(struct gm_port *p, void *ctx, gm_status_t status)
{
	gasnetc_bufdesc_t	*bufd = (gasnetc_bufdesc_t *) ctx;

	gasneti_mutex_assertlocked(&gasnetc_lock_gm);

	if_pf (status != GM_SUCCESS)
		gasnetc_callback_error(status, "AMReply");


	if_pt (bufd != NULL) {
		gasnetc_provide_AMRequest(bufd);
		GASNETI_TRACE_PRINTF(C, 
		    ("GM Send Hi Callback: stoks.hi=%d, AMRequest queue "
		     "gets buffer", _gmc.stoks.hi));
	}
	else {
		GASNETI_TRACE_PRINTF(C, 
		    ("GM Send Hi Callback: stoks.hi=%d, System AM ", 
		     _gmc.stoks.hi));
	}

	gasnetc_token_hi_release();
}

/* Utility function for releasing rdma from bufd.  At least the remote_req must
 * have a firehose request type whereas the local request type is optional (AM
 * buffers can be used to send payload from).
 */
GASNET_INLINE_MODIFIER(gasnetc_release_rdma)
void
gasnetc_release_rdma(gasnetc_bufdesc_t *bufd)
{
	const firehose_request_t	*reqs[2];
	int				numreqs = 1;

	gasneti_mutex_assertlocked(&gasnetc_lock_gm);
	gasneti_assert(bufd->node < gasnetc_nodes);
	gasneti_assert(bufd->remote_req != NULL);

	/* Release firehose on regions (remote and possibly local) */
	reqs[0] = bufd->remote_req;
	if (bufd->local_req != NULL) {
		reqs[1] = bufd->local_req;
		numreqs++;
	}
	GASNETE_GM_SET_IN_UNKNOWN();
	firehose_release(reqs, numreqs);
	GASNETE_GM_UNSET_IN_UNKNOWN();

	return;
}

void
gasnetc_callback_lo_rdma(struct gm_port *p, void *ctx, gm_status_t status)
{
	gasnetc_bufdesc_t	*bufd = (gasnetc_bufdesc_t *) ctx;

	gasneti_mutex_assertlocked(&gasnetc_lock_gm);

	if_pf (status != GM_SUCCESS)
		gasnetc_callback_error(status, "AMRequest/DMA");

	GASNETI_TRACE_PRINTF(C, 
	    ("GM RDMA Lo Callback: stoks.lo=%d, bufd %s", _gmc.stoks.lo,
	     bufd == NULL ? "absent" : "to AMRequestPool" ));

	if_pt (bufd != NULL) {
		gasnetc_release_rdma(bufd);
		gasnetc_provide_AMRequestPool(bufd);
	}

	gasnetc_token_lo_release();
}

/*
 * Hi callbacks are used for AMReplies, and AMReplies never request to locally
 * pin a region through firehose, so only remote requests can exist in the
 * bufdesc.
 */

/*
 * Callback for AMReply when destination is pinned
 */
void
gasnetc_callback_hi_rdma(struct gm_port *p, void *ctx, 
				  gm_status_t status)
{
	gasnetc_bufdesc_t	*bufd = (gasnetc_bufdesc_t *) ctx;

	gasneti_mutex_assertlocked(&gasnetc_lock_gm);

	gasneti_assert(bufd != NULL);
	gasneti_assert(bufd->node < gasnetc_nodes);
	gasneti_assert(bufd->payload_len > 0);
	gasneti_assert(bufd->local_req == NULL);

	if_pf (status != GM_SUCCESS)
		gasnetc_callback_error(status, "AMReply/DMA");

	if (bufd->remote_req == NULL) {
		GASNETI_TRACE_PRINTF(C, 
		    ("GM RDMA Hi Callback: stoks.hi=%d, LongReplyAsync",
		     _gmc.stoks.hi));
	}
	else {
		GASNETI_TRACE_PRINTF(C, 
		    ("GM RDMA Hi Callback: stoks.hi=%d, bufd absent",
		     _gmc.stoks.hi));
		GASNETE_GM_SET_IN_UNKNOWN();
		firehose_release(&(bufd->remote_req), 1);
		GASNETE_GM_UNSET_IN_UNKNOWN();
	}

	gasnetc_token_hi_release();
}

/*
 * System callbacks always wait on a counter to be incremented, 
 * simple as that.
 */
void
gasnetc_callback_system(struct gm_port *p, void *ctx, gm_status_t status)
{
	int	*ctr = (int *) ctx;

	if_pf (status != GM_SUCCESS)
		gasnetc_callback_error(status, "AMSystem");

	gasneti_mutex_assertlocked(&gasnetc_lock_gm);

	(*ctr)++;
	GASNETI_TRACE_PRINTF(C, 
	    ("GM System Lo Callback: counter now %d", *((int *) ctx)));
	gasnetc_token_lo_release();
}


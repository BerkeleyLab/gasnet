/* $Id: gasnet_extended_firehose.c,v 1.21.8.1 2003/08/04 11:06:51 csbell Exp $
 * $Date: 2003/08/04 11:06:51 $
 * Description: GASNet GM conduit Firehose DMA Registration Algorithm
 * Copyright 2002, Christian Bell <csbell@cs.berkeley.edu>
 * Terms of use are as specified in license.txt
 */
#include <gasnet.h>
#ifdef GASNETC_FIREHOSE
#include <gasnet_extended_internal.h>
#include <gasnet_core_internal.h>
#include <gasnet_internal.h>
#include <gasnet_handler.h>

#ifndef GASNETE_PUT_NON_DMA_CUTOFF
#define GASNETE_PUT_NON_DMA_CUTOFF	gasnet_AMMaxMedium()
#warning GASNETE_PUT_NON_DMA_CUTOFF was not defined
#endif
#ifndef GASNETE_GET_NON_DMA_CUTOFF
#define GASNETE_GET_NON_DMA_CUTOFF	gasnet_AMMaxMedium()
#warning GASNETE_GET_NON_DMA_CUTOFF was not defined
#endif

#define GASNETE_FH_HAVE_TOKEN		0
#define GASNETE_FH_POLL_TOKEN		1

extern void gasnetc_callback_ambuffer(struct gm_port *, void *, gm_status_t);

/* ------------------------------------------------------------------------ */
/* Tracing Firehose */
#ifdef GASNETC_FIREHOSE_TRACE
#define GASNETE_FIREHOSE_TRACE_PUTGET(eop, putget)		\
	do {							\
	    switch(eop->fh_stats) {				\
		case fh_onesided: GASNETI_TRACE_EVENT_TIME(C,	\
			    FIREHOSE_ ## putget ## _ONESIDED, 	\
			    GASNETI_STATTIME_NOW_IFENABLED(C)-	\
			    eop->starttime); break;		\
		case fh_one: GASNETI_TRACE_EVENT_TIME(C,	\
			    FIREHOSE_ ## putget ## _ONE, 	\
			    GASNETI_STATTIME_NOW_IFENABLED(C)-	\
			    eop->starttime); break;		\
		case fh_many: GASNETI_TRACE_EVENT_TIME(C,	\
			    FIREHOSE_ ## putget ## _MANY, 	\
			    GASNETI_STATTIME_NOW_IFENABLED(C)-	\
			    eop->starttime); break;		\
		default: break;					\
	    }							\
	    eop->fh_stats = fh_none;				\
	} while (0)
#else
#define GASNETE_FIREHOSE_TRACE_PUTGET(eop, putget)
#endif

#define gasnete_in_segment(node,ptr,len)					\
		(!((uintptr_t)(ptr) < (uintptr_t)gasnetc_seginfo[(node)].addr	\
		    || ((uintptr_t)(ptr) + (len)) > 				\
		    ((uintptr_t)gasnetc_seginfo[(node)].addr + 			\
		    gasnetc_seginfo[(node)].size)))

#if 0
/* ------------------------------------------------------------------------ */
/* FIFO operations */
/* All assume that the GM lock is held the whole time.  Since the gm lock is
 * global, there is no need to protect access to the fifo */

gasnete_eop_t	 *gasnete_fifo_head = NULL;

GASNET_INLINE_MODIFIER(gasnete_fifo_enqueue)
void
gasnete_fifo_enqueue(gasnete_eop_t *eop)
{
	gasneti_mutex_assertlocked(&gasnetc_lock_gm);

	eop->next = gasnete_fifo_head;
	gasnete_fifo_head = eop;

	GASNETI_TRACE_PRINTF(C, ("Firehose queue has %p", gasnete_fifo_head));
	return;
}

GASNET_INLINE_MODIFIER(gasnete_fifo_dequeue)
void
gasnete_fifo_dequeue()
{
	gasneti_mutex_assertlocked(&gasnetc_lock_gm);
	assert(gasnete_fifo_head != NULL);
	gasnete_fifo_head = gasnete_fifo_head->next;
}

extern void
gasnete_firehose_move_done(void *context)
{
	gasnete_eop_t *eop = (gasnete_eop_t *) context;
	GASNETI_TRACE_PRINTF(C, ("Firehose move done in extended"));
	gasneti_mutex_lock(&gasnetc_lock_gm);
	gasnete_fifo_enqueue(eop);
	gasneti_mutex_unlock(&gasnetc_lock_gm);
}
#endif

/* ##################################################################### */
/* PUTS                                                                  */
/* ##################################################################### */
void
gasnete_fh_callback_put(struct gm_port *p, void *context, 
			      gm_status_t status)
{
	gasnete_eop_t		*pop = (gasnete_eop_t *) context;
	gasneti_stattime_t      starttime = GASNETI_STATTIME_NOW_IFENABLED(C);
	const firehose_request_t	*fhreqs[2];
	int				numreqs = 1;

	gasneti_mutex_assertlocked(&gasnetc_lock_gm);
	assert(pop != NULL);
	assert(pop->req_remote.node < gasnete_nodes);

	if_pf (status != GM_SUCCESS)
	    gasnetc_callback_error(status, NULL);
	gasnetc_token_lo_release();

	GASNETI_TRACE_PRINTF(C, 
	    ("Firehose decrement remote refcount for (%p,%d) on node %d\n",
	     (void *) pop->dest, pop->len, (unsigned) pop->req_remote.node));

	fhreqs[0] = &(pop->req_remote);

	/* If this was associated to an iop, increment put completed count */
	gasnete_op_markdone((gasnete_op_t *)pop, 0);

	/* Puts use an ambuffer, while bulk puts send from a pinned location */
	if (OPMISC(pop) == OPMISC_AMBUF) {
		gasnetc_bufdesc_t	*bufd;
		bufd = (gasnetc_bufdesc_t *) GASNETC_BUFDESC_PTR(pop->src);
		GASNETC_ASSERT_BUFDESC_PTR(bufd, pop->src);
		gasnetc_callback_ambuffer(p, (void *) bufd, status);
	}
	else  {
		fhreqs[1] = pop->req_local;
		numreqs++;
	}

	firehose_release(fhreqs, numreqs);

	if (pop->iop != NULL) {
		gasneti_atomic_increment(&(pop->iop->completed_put_cnt));
		gasnete_op_free((gasnete_op_t *) pop);
	}

	GASNETI_TRACE_EVENT_TIME(C, FIREHOSE_MOVE_LOCAL,
		    GASNETI_STATTIME_NOW_IFENABLED(C)-starttime);

	GASNETE_FIREHOSE_TRACE_PUTGET(pop, PUT);
	return;
}

void
gasnete_fh_request_put(void *_pop, firehose_request_t *req)
{
	gasnete_eop_t	*pop = (gasnete_eop_t *) _pop;
	gasnet_node_t	node = req->node;

	assert(pop != NULL);
	assert(pop->src > 0 && pop->dest > 0);
	assert(node < gasnete_nodes);
	assert(pop->len > 0);
	assert(req == &(pop->req_remote));

	gasneti_mutex_lock(&gasnetc_lock_gm);
	gasnetc_token_lo_poll();

	GASNETI_TRACE_PRINTF(C, 
	    ("Firehose directed send(%p): (%d,%p) <- %p (%d bytes)", 
	     pop, (unsigned) pop->req_remote.node, (void *) pop->dest, 
	     (void *) pop->src, pop->len));

	GASNETC_GM_PUT(
	    _gmc.port, (void *) pop->src, (gm_remote_ptr_t) pop->dest,
	    (unsigned long) pop->len, GM_LOW_PRIORITY,
	    gasnetc_nodeid(node), gasnetc_portid(node),
	    gasnete_fh_callback_put, (void *) pop);
	gasneti_mutex_unlock(&gasnetc_lock_gm);
	return;
}


GASNET_INLINE_MODIFIER(gasnete_firehose_put_bulk)
gasnet_handle_t
gasnete_firehose_put_bulk(gasnet_node_t node, void *dest, void *src, 
			  size_t nbytes, gasnete_iop_t *iop GASNETE_THREAD_FARG)
{
	gasnete_eop_t	*pop;

	pop = gasnete_eop_new(GASNETE_MYTHREAD);
	pop->src = (uintptr_t) src;
	pop->dest = (uintptr_t) dest;
	pop->len = (uint32_t) nbytes;
	pop->iop = iop;
	SET_OPMISC(pop, OPMISC_NONAMBUF);
	#if defined(TRACE) || defined(STATS)
	pop->starttime = GASNETI_STATTIME_NOW_IFENABLED(C);
	#endif

	/* XXX should check according to fh_info */

	/* Pin locally, incrementing reference counts where necessary */
	pop->req_local = 
	    firehose_local_pin((uintptr_t) src, nbytes, NULL);
	if (pop->req_local == NULL)
		gasneti_fatalerror("Can't pin locally");

	/* If we were dealing with implicit put, increment the iop */
	if (pop->iop != NULL)
		pop->iop->initiated_put_cnt++;

	if (firehose_remote_pin(node, (uintptr_t) dest, nbytes,
	    gasnete_fh_request_put, pop, 0, 
	    (firehose_request_t *) &(pop->req_remote)) == NULL)
		gasneti_fatalerror("Can't pin remotely");

	return (gasnete_op_t *) pop;
}

extern gasnet_handle_t
gasnete_put_nb_bulk (gasnet_node_t node, void *dest, void *src, 
		     size_t nbytes GASNETE_THREAD_FARG)
{
	gasnet_handle_t	handle;
	if (nbytes > GASNETE_PUT_NON_DMA_CUTOFF) {
		GASNETI_TRACE_PRINTF(C, 
		    ("gasnete_put_nb_bulk Firehose (%d,%p <- %p,%d bytes)",
		    (unsigned) node, dest, src, nbytes));
		handle = gasnete_firehose_put_bulk(node, dest, src, nbytes, 
		    NULL GASNETE_THREAD_PASS);
		return handle;
	}
	else { 
		GASNETI_TRACE_PRINTF(C,
		    ("gasnete_put_nb_bulk Extref (%d,%p <- %p,%d bytes)",
		    (unsigned) node, dest, src, nbytes));
		return gasnete_extref_put_nb_bulk(node, dest, src, 
		    nbytes GASNETE_THREAD_PASS);
	}
}

extern void
gasnete_put_nbi_bulk (gasnet_node_t node, void *dest, void *src, 
		      size_t nbytes GASNETE_THREAD_FARG)
{
	gasnete_threaddata_t * const mythread = GASNETE_MYTHREAD;
	gasnete_iop_t *iop = mythread->current_iop;

	if (nbytes > GASNETE_PUT_NON_DMA_CUTOFF) {
		GASNETI_TRACE_PRINTF(C, 
		    ("gasnete_put_nbi_bulk Firehose (%d,%p <- %p,%d bytes)",
		    (unsigned) node, dest, src, nbytes));
		gasnete_firehose_put_bulk(node, dest, src, nbytes,
		    iop GASNETE_THREAD_PASS);
	}
	else
		gasnete_extref_put_nbi_bulk(node, dest, src, 
		    nbytes GASNETE_THREAD_PASS);

	return;
}

GASNET_INLINE_MODIFIER(gasnete_firehose_put)
gasnet_handle_t
gasnete_firehose_put(gasnet_node_t node, void *dest, void *src, size_t nbytes,
		     gasnete_iop_t *iop GASNETE_THREAD_FARG)
{
	gasnete_eop_t		*pop;
	gasnetc_bufdesc_t	*bufd;

	assert(nbytes <= GASNETC_AM_LEN);
	bufd = gasnetc_AMRequestPool_block();

	pop = gasnete_eop_new(GASNETE_MYTHREAD);
	pop->src = (uintptr_t) bufd->sendbuf;
	pop->dest = (uintptr_t) dest;
	pop->len = (uint32_t) nbytes;
	pop->iop = iop;
	SET_OPMISC(pop, OPMISC_AMBUF);
	#if defined(TRACE) || defined(STATS)
	pop->starttime = GASNETI_STATTIME_NOW_IFENABLED(C);
	#endif
	GASNETE_FAST_UNALIGNED_MEMCPY(bufd->sendbuf, src, nbytes);

	/* If we were dealing with implicit put, increment the iop */
	if (iop != NULL)
		iop->initiated_put_cnt++;

	if (firehose_remote_pin(node, (uintptr_t) dest, nbytes,
	    gasnete_fh_request_put, pop, 0, 
	    (firehose_request_t *) &(pop->req_remote)) == NULL)
		gasneti_fatalerror("Can't pin remotely");
	
	return (gasnete_op_t *) pop;
}

/*
 * In the non-bulk version of put, we always need a source copy of the local
 * data before sending it off, which doesn't require a local memory
 * registration.  
 *
 * By using AMRequestLong, the core API will attempt to query if the
 * destination is pinned and will leverage DMAs if possible.  The difference
 * between AMRequestLong and put_bulk is that the latter will _try_ to have the
 * remote memory pinned before issuing the DMA while the former is simply a
 * lookup/fallback approach - if the destination is not pinned, Mediums are
 * used.
 */
extern gasnet_handle_t 
gasnete_put_nb (gasnet_node_t node, void *dest, void *src, 
		size_t nbytes GASNETE_THREAD_FARG)
{
	gasnet_handle_t	handle;

	if (nbytes < GASNETE_PUT_NON_BULK_CUTOFF) {
		handle = gasnete_firehose_put(node, dest, src, nbytes, 
		    NULL GASNETE_THREAD_PASS);
		return handle;
	}
	else
		return gasnete_extref_put_nb(node, dest, src, 
		    nbytes GASNETE_THREAD_PASS);
}

extern void
gasnete_put_nbi(gasnet_node_t node, void *dest, void *src, 
		size_t nbytes GASNETE_THREAD_FARG)
{
	if (nbytes < GASNETE_PUT_NON_BULK_CUTOFF) {
		gasnete_threaddata_t * const mythread = GASNETE_MYTHREAD;
		gasnete_iop_t *iop = mythread->current_iop;
		gasnete_firehose_put(node, dest, src, nbytes, 
		    iop GASNETE_THREAD_PASS);
	}
	else
		gasnete_extref_put_nbi(node, dest, src, 
		    nbytes GASNETE_THREAD_PASS);
	return;
}

/* ##################################################################### */
/* GETS                                                                  */
/* ##################################################################### */
/*
 * Under firehose, a get from the requestors point of view does not include
 * looking up the remote node's firehose list: gets do not move the firehose.
 *
 * Upon a get call, if nbytes is within the GASNETE_GET_NON_DMA_CUTOFF, an
 * AMRequest for copy on the host side is sent.  For other cases, local buckets
 * are pinned and their reference count incremented.  At completion of the get
 * (during the callback), the reference count is decremented.
 *
 * The reason for using a GASNETE_GET_NON_DMA_CUTOFF is that GM still does not
 * support DMA gets, which means we must interrupt the host processor in order
 * for every get to succeed.  When using a DMA reversed put to complete the get
 * operation, an AMReply must still be sent in order to mark the get operation
 * complete (the host processor cannot know when a DMA operation is received).
 * It may be desirable to send payload with the get AMReply as an optimization
 * for smaller sizes.  This allows a get operation to be completed with two
 * sends as opposed to three (less GM tokens are used).
 */

#ifdef GASNETC_GM_2
/* In GM 2.0, we can use directed receives (gm_get) once the remote region is
 * known to be pinned */
void
gasnete_fh_request_get(void *_gop, firehose_request_t *req)
{
	gasnete_eop_t	*gop = (gasnete_eop_t *) _gop;
	gasnet_node_t	node = req->node;

	assert(gop != NULL);
	assert(gop->src > 0 && gop->dest > 0);
	assert(node < gasnete_nodes);
	assert(gop->len > 0);
	assert(req == &(gop->req_remote));

	/* Always poll for tokens as gets are initiated only from the client on
	 * the current host */
	gasneti_mutex_lock(&gasnetc_lock_gm);
	gasnetc_token_lo_poll();

	GASNETI_TRACE_PRINTF(C, 
	    ("Firehose RDMA GET(%p): %p <- (%d,%p) (%d bytes)", 
	     gop, (void *) gop->dest, (unsigned) gop->node, 
	     (void *) gop->src, gop->len));

	gm_get(_gmc.port, (gm_remote_ptr_t) (void *) gop->src,
	    (gm_size_t) gop->len, GM_LOW_PRIORITY,
	    gasnetc_nodeid(node), gasnetc_portid(node),
	    gasnete_fh_callback_get, (void *) gop);

	gasneti_mutex_unlock(&gasnetc_lock_gm);
}

#else	/* GM 1.x */
/*
 * AM Handler: Reply to get into a pinned memory location
 */
GASNET_INLINE_MODIFIER(gasnete_get_dma_reph_inner)
void
gasnete_get_dma_reph_inner(gasnet_token_t token, void *op)
{
	gasnete_eop_t	*eop = (gasnete_eop_t *) op;
	const firehose_request_t	*fhreqs[2];

	assert(eop->src > 0 && eop->len > 0);

	/* Gets with DMA are a result of a local pin and remote pin request */
	fhreqs[0] = eop->req_local;
	fhreqs[1] = &(eop->req_remote);
	firehose_release(fhreqs, 2);

	gasnete_op_markdone((gasnete_op_t *) op, 1);
	if (eop->iop != NULL) {
		gasneti_atomic_increment(&(eop->iop->completed_get_cnt));
		gasnete_op_free((gasnete_op_t *) eop);
	}

	GASNETE_FIREHOSE_TRACE_PUTGET(eop, GET);
}
LONG_HANDLER(gasnete_get_dma_reph,1,2, 
    (token, UNPACK(a0)    ),
    (token, UNPACK2(a0,a1)));

/* In GM 1.x, we can send a request for a ReplyLongAsync which essentially
 * translates to doing a put in the reverse direction */
GASNET_INLINE_MODIFIER(gasnete_firehose_get_dma_reqh_inner)
void
gasnete_get_dma_reqh_inner(gasnet_token_t token, 
				    gasnet_handlerarg_t nbytes, 
				    void *dest, void *src, void *op, void *op2)
{
	assert(op != NULL && op2 != NULL); /* XXX this _was_ a bug on alvarez */
	/* The memory should already be pinned per a previous pin request */
	GASNETE_SAFE(
	    LONGASYNC_REP(1,2, (token,
	    gasneti_handleridx(gasnete_get_dma_reph), src, nbytes,
	    dest, PACK(op))));
}
SHORT_HANDLER(gasnete_get_dma_reqh,5,9, 
    (token, a0, UNPACK(a1),     UNPACK(a2),     UNPACK(a3),	UNPACK(a4)    ),
    (token, a0, UNPACK2(a1,a2), UNPACK2(a3,a4), UNPACK2(a5, a6),UNPACK(a7,a8)));

void
gasnete_fh_request_get(void *_gop, firehose_request_t *req)
{
	gasnete_eop_t	*gop = (gasnete_eop_t *) _gop;

	assert(gop != NULL);
	assert(gop->src != NULL gop->dest != NULL);
	assert(req->node < gasnete_nodes);

	SHORT_REQ(5, 9,
	    (req->node, gasneti_handleridx(gasnete_get_dma_reqh), gop->len,
	     PACK(gop->dest), PACK(gop->src), PACK(gop), PACK(gop)));
}

#endif

GASNET_INLINE_MODIFIER(gasnete_firehose_get_bulk)
gasnet_handle_t
gasnete_firehose_get(void *dest, gasnet_node_t node, void *src, 
		     size_t nbytes, gasnete_iop_t *iop GASNETE_THREAD_FARG)
{
	/* Request a Get in terms of a DMA put */
	gasnete_eop_t	*gop;

	gop = gasnete_eop_new(GASNETE_MYTHREAD);
	gop->dest = (uintptr_t) dest;
	gop->src = (uintptr_t) src;
	gop->len = nbytes;
	gop->iop = iop;
	SET_OPMISC(gop, OPMISC_NONAMBUF);
	#if defined(TRACE) || defined(STATS)
	gop->starttime = GASNETI_STATTIME_NOW_IFENABLED(C);
	gop->fh_stats = fh_onesided;
	#endif

	/* Pin locally, incrementing reference counts where necessary */
	gop->req_local = 
	    firehose_local_pin((uintptr_t) dest, nbytes, NULL);
	if (gop->req_local == NULL)
		gasneti_fatalerror("Can't pin locally");

	if (iop != NULL)
		iop->initiated_get_cnt++;

	if (firehose_remote_pin(node, (uintptr_t) src, nbytes,
	    gasnete_fh_request_get, gop, 0, 
	    (firehose_request_t *) &(gop->req_remote)) == NULL)
		gasneti_fatalerror("Can't pin remotely");

	return (gasnete_op_t *) gop;
}

extern gasnet_handle_t
gasnete_get_nb_bulk (void *dest, gasnet_node_t node, void *src, 
		     size_t nbytes GASNETE_THREAD_FARG)
{
	gasnete_boundscheck(node, src, nbytes);

	if (nbytes > GASNETE_GET_NON_DMA_CUTOFF) {
		GASNETI_TRACE_PRINTF(C, 
		    ("gasnete_get_nb_bulk Firehose (%d,%p <- %p,%d bytes)",
		    (unsigned) node, dest, src, nbytes));
		return gasnete_firehose_get(dest, node, src, nbytes, 
		    NULL GASNETE_THREAD_PASS);
	}
	else {
		GASNETI_TRACE_PRINTF(C,
		    ("gasnete_get_nb_bulk Extref (%d,%p <- %p,%d bytes)",
		    (unsigned) node, dest, src, nbytes));
		return gasnete_extref_get_nb_bulk(dest, node, src, 
		    nbytes GASNETE_THREAD_PASS);
	}
}

extern void
gasnete_get_nbi_bulk (void *dest, gasnet_node_t node, void *src, 
		      size_t nbytes GASNETE_THREAD_FARG)
{
	gasnete_boundscheck(node, src, nbytes);

	if (nbytes > GASNETE_GET_NON_DMA_CUTOFF) {
		gasnete_threaddata_t * const mythread = GASNETE_MYTHREAD;
		gasnete_iop_t *iop = mythread->current_iop;
		GASNETI_TRACE_PRINTF(C, 
		    ("gasnete_get_nbi_bulk Firehose (%d,%p <- %p,%d bytes)",
		    (unsigned) node, dest, src, nbytes));
		gasnete_firehose_get(dest, node, src, nbytes, 
		    iop GASNETE_THREAD_PASS);
	}
	else 
		gasnete_extref_get_nbi_bulk(dest, node, src, 
		    nbytes GASNETE_THREAD_PASS);
	return;
}

#if 0
/* The non-bulk get is similar to the bulk version */
/* gasnete_get_nb ...
 * gasnete_get_nbi ...
 */

/* ------------------------------------------------------------------------ */
extern void
gasnete_fifo_progress()	
{
	gasneti_mutex_assertlocked(&gasnetc_lock_gm);

	while (gasnete_fifo_head != NULL) {

		GASNETI_TRACE_PRINTF(C, ("Firehose fifo progress drain 1"));

		if (!gasnetc_token_lo_acquire())
			return;

		gasnete_firehose_put_using_directed(gasnete_fifo_head, 
		    GASNETE_FH_HAVE_TOKEN);

		gasnete_fifo_dequeue();
	}
}
#endif

/* ##################################################################### */
/* Handlers                                                              */
/* ##################################################################### */
#ifdef GASNETC_GM_2
static gasnet_handlerentry_t const gasnete_handlers[] = {
	{ 0, NULL }
};
#else
static gasnet_handlerentry_t const gasnete_handlers[] = {
	gasneti_handler_tableentry_with_bits(gasnete_get_dma_reqh),
	gasneti_handler_tableentry_with_bits(gasnete_get_dma_reph),
	{ 0, NULL }
};
#endif

extern gasnet_handlerentry_t const *gasnete_get_handlertable() {
	return gasnete_handlers;
}

#endif

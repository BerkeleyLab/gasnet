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

extern uintptr_t	*gasnetc_firehose_buf;
extern size_t		 gasnetc_firehose_buf_num;
extern gasneti_mutex_t	 gasnetc_lock_fh_victim;
extern gasneti_mutex_t	 gasnetc_lock_fh_hash;

extern void	gasnetc_done_pinned(gasnet_node_t, uintptr_t, size_t);
extern void	gasnetc_callback_ambuffer(struct gm_port *, void *, gm_status_t);
extern void	gasnetc_bucket_pin_by_addr(uintptr_t, size_t);
extern void	gasnetc_bucket_unpin_by_addr(uintptr_t, size_t);
extern int	gasnetc_firehose_build_list(gasnet_node_t, uintptr_t, size_t, 
					    size_t *, size_t *);
extern void	gasnetc_firehose_decrement_refcount(gasnet_node_t, uintptr_t, 
						    size_t);
extern void	gasnete_firehose_move_done(void *);
extern void	gasnete_fifo_progress();

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
/* ------------------------------------------------------------------------ */
/* FIFO operations */
gasneti_mutex_t	 gasnete_fifo_lock = GASNETI_MUTEX_INITIALIZER;
gasnete_eop_t	 *gasnete_fifo_head = NULL;

GASNET_INLINE_MODIFIER(gasnete_fifo_enqueue)
void
gasnete_fifo_enqueue(gasnete_eop_t *eop)
{
	gasneti_mutex_lock(&gasnete_fifo_lock);
	eop->next = gasnete_fifo_head;
	gasnete_fifo_head = eop;
	gasneti_mutex_unlock(&gasnete_fifo_lock);
	GASNETI_TRACE_PRINTF(C, ("Firehose queue has %p", gasnete_fifo_head));
	return;
}

GASNET_INLINE_MODIFIER(gasnete_fifo_dequeue)
void
gasnete_fifo_dequeue()
{
	gasnete_eop_t *eop;

	assert(gasnete_fifo_head != NULL);
	gasneti_mutex_lock(&gasnete_fifo_lock);
	gasnete_fifo_head = gasnete_fifo_head->next;
	gasneti_mutex_unlock(&gasnete_fifo_lock);
}

extern void
gasnete_firehose_move_done(void *context)
{
	gasnete_eop_t *eop = (gasnete_eop_t *) context;
	GASNETI_TRACE_PRINTF(C, ("Firehose move done in extended"));
	gasnete_fifo_enqueue(eop);
}


/* ------------------------------------------------------------------------ */
/* 
 * Extended AM Handlers 
 */
/*
 * AM Handler: Request to get into a pinned memory location
 */
GASNET_INLINE_MODIFIER(gasnete_firehose_get_dma_reqh_inner)
void
gasnete_firehose_get_dma_reqh_inner(gasnet_token_t token, 
				    gasnet_handlerarg_t nbytes, 
				    void *dest, void *src, void *op)
{
	gasnetc_bucket_pin_by_addr((uintptr_t) src, (size_t) nbytes);
	GASNETE_SAFE(
	    LONGASYNC_REP(1,2, (token,
	    gasneti_handleridx(gasnete_firehose_get_dma_reph), src, nbytes,
	    dest, PACK(op))));
}
SHORT_HANDLER(gasnete_firehose_get_dma_reqh,4,7, 
    (token, a0, UNPACK(a1),     UNPACK(a2),     UNPACK(a3)    ),
    (token, a0, UNPACK2(a1,a2), UNPACK2(a3,a4), UNPACK2(a5, a6)));

/*
 * AM Handler: Reply to get into a pinned memory location
 */
GASNET_INLINE_MODIFIER(gasnete_firehose_get_dma_reph_inner)
void
gasnete_firehose_get_dma_reph_inner(gasnet_token_t token, void *op)
{
	gasnete_eop_t	*eop;

	eop = (gasnete_eop_t *) op;
	assert(eop->src > 0 && eop->len > 0);
	gasnetc_bucket_unpin_by_addr(eop->dest,eop->len);
	gasnete_op_markdone((gasnete_op_t *) op, 1);
	if (eop->iop != NULL) {
		gasneti_atomic_increment(&(eop->iop->completed_get_cnt));
		GASNETI_TRACE_PRINTF(C, ("iop increment at %p", (void *) op));
		gasnete_op_free((gasnete_op_t *) eop);
	}
	else {
		GASNETI_TRACE_PRINTF(C, ("eop markdone at %p", (void *) op));
	}
	GASNETE_FIREHOSE_TRACE_PUTGET(eop, GET);
}
LONG_HANDLER(gasnete_firehose_get_dma_reph,1,2, 
    (token, UNPACK(a0)    ),
    (token, UNPACK2(a0,a1)));

/* ------------------------------------------------------------------------ */
/*
 * Firehose callback function
 */

void
gasnete_firehose_callback_pop(struct gm_port *p, void *context, 
			      gm_status_t status)
{
	gasnete_eop_t	*eop = (gasnete_eop_t *) context;

	gasneti_mutex_assertlocked(&gasnetc_lock_gm);
	assert(eop != NULL);
	assert(eop->node < gasnete_nodes);
	if_pf (status != GM_SUCCESS)
	    gasnetc_callback_error(status, NULL);
	gasnetc_token_lo_release();
	GASNETI_TRACE_PRINTF(C, ("Firehose callback directed send(%p), stoks.lo=%d", 
	   eop, _gmc.stoks.lo));
	GASNETI_TRACE_PRINTF(C, 
	    ("Firehose decrement refcount for (%p,%d) on node %d\n",
	     (void *) eop->dest, eop->len, (unsigned) eop->node));
#if 0
	gasnetc_firehose_decrement_refcount(eop->node, eop->dest, eop->len);
#endif
	/* If this was associated to an iop, increment put completed count */
	gasnete_op_markdone((gasnete_op_t *)eop, 0);
	/* Puts use an ambuffer, while bulk puts send from a pinned location */
	if (OPMISC(eop) == OPMISC_AMBUF) {
		gasnetc_bufdesc_t	*bufd;
		GASNETI_TRACE_PRINTF(C, 
		    ("Firehose pop callback -> ambuffer %p",
		    (void *)eop->src));
		bufd = (gasnetc_bufdesc_t *) GASNETC_BUFDESC_PTR(eop->src);
		GASNETC_ASSERT_BUFDESC_PTR(bufd, eop->src);
		gasnetc_callback_ambuffer(p, (void *) bufd, status);
	}
	else  {
		gasnetc_done_pinned(gasnetc_mynode, eop->src, eop->len);
	}
	GASNETE_FIREHOSE_TRACE_PUTGET(eop, PUT);
	if (eop->iop != NULL) {
		gasneti_atomic_increment(&(eop->iop->completed_put_cnt));
		GASNETI_TRACE_PRINTF(C, ("iop increment at %p", (void *) eop));
		gasnete_op_free((gasnete_op_t *) eop);
	}
	else {
		GASNETI_TRACE_PRINTF(C, ("eop markdone at %p", (void *) eop));
	}
	return;
}

/* ------------------------------------------------------------------------ */
/*
 * Firehose put helper
 */
GASNET_INLINE_MODIFIER(gasnete_firehose_put_using_directed)
void
gasnete_firehose_put_using_directed(gasnete_eop_t *pop, int poll)
{
	gm_send_completion_callback_t	callback;
	assert(pop != NULL);
	assert(pop->src > 0 && pop->dest > 0);
	assert(pop->node < gasnete_nodes);
	assert(pop->len > 0);
	gasneti_mutex_assertlocked(&gasnetc_lock_gm);
	if (poll == GASNETE_FH_POLL_TOKEN)
		gasnetc_token_lo_poll();
	GASNETI_TRACE_PRINTF(C, 
	    ("Firehose directed send(%p): (%d,%p) <- %p (%d bytes)", 
	     pop, (unsigned) pop->node, (void *) pop->dest, (void *) pop->src, 
	     pop->len));
	gm_directed_send_with_callback(
	    _gmc.port, (void *) pop->src, (gm_remote_ptr_t) pop->dest,
	    (unsigned long) pop->len, GM_LOW_PRIORITY,
	    gasnetc_nodeid(pop->node), gasnetc_portid(pop->node),
	    gasnete_firehose_callback_pop, (void *) pop);
	return;
}

/* ------------------------------------------------------------------------ */
/* 
 * Puts
 */
GASNET_INLINE_MODIFIER(gasnete_firehose_move_for_put)
void
gasnete_firehose_move_for_put(gasnete_eop_t *pop)
{
	size_t		num_buckets, tot_buckets;
	size_t		new_buckets, old_buckets;

	assert(pop->len > 0);
	assert(pop->node < gasnete_nodes);
	assert(OPSTATE(pop) == OPSTATE_INFLIGHT);

	/* In rendez-vous, we need to flush extra sends to make sure previous
	 * unpin AMs do not interfere with future pin requests */
	gasnete_fifo_progress();

	num_buckets = GASNETC_NUM_BUCKETS(
	    GASNETI_PAGE_ALIGN(pop->dest, GASNETC_BUCKET_SIZE), 
	    pop->dest+pop->len);
	tot_buckets = num_buckets*2;
	/* XXX need better runtime support for cases where num firehoses > than
	 * we can support in a single medium
	 */
	assert(sizeof(uintptr_t)*tot_buckets < gasnet_AMMaxMedium());
	SET_OPMISC(pop, OPMISC(pop) & ~OPMISC_RVUNPIN);

	if (gasnetc_firehose_buf_num < tot_buckets) {
		void	*old_buf;
		gasneti_mutex_lock(&gasnetc_lock_fh_hash);
		if (gasnetc_firehose_buf_num < tot_buckets) {
		       	old_buf = gasnetc_firehose_buf;
			gasnetc_firehose_buf_num = tot_buckets;
			gasnetc_firehose_buf = (uintptr_t *)
			    gasneti_malloc(sizeof(uintptr_t) * tot_buckets);
			if (old_buf != NULL)
				gasneti_free(old_buf);
		}
		gasneti_mutex_unlock(&gasnetc_lock_fh_hash);
	}

	if (gasnetc_firehose_build_list(pop->node, pop->dest, num_buckets,
	    &old_buckets, &new_buckets)) {
		assert(gasneti_handleridx(gasnete_firehose_move_reph) > 0);
		#ifdef GASNETC_FIREHOSE_TRACE
		if (new_buckets+old_buckets == 1)
			pop->fh_stats = fh_one;
		else
			pop->fh_stats = fh_many;
		#endif
		#ifdef TRACE
		{
			int i;
			for (i = 0; i < new_buckets; i++)
				GASNETI_TRACE_PRINTF(C, 
				    ("Firehose move new %d=%p",
				    i, (void *) gasnetc_firehose_buf[i]));
			for (i = 0; i < old_buckets; i++)
				GASNETI_TRACE_PRINTF(C, 
				    ("Firehose move old %d=%p",
				    i, (void *) gasnetc_firehose_buf[i]));
		}
		#endif
		MEDIUM_REQ(4, 5, 
		   (pop->node, gasneti_handleridx(gasnetc_firehose_move_reqh),
		    (void *) gasnetc_firehose_buf, 
		    (num_buckets + old_buckets)*sizeof(uintptr_t),
		    new_buckets, old_buckets, num_buckets*sizeof(uintptr_t), 
		    PACK((void *) pop)));
	}
	else {
		fprintf(stderr, "should have nothing pinned in on demand!\n");
		gasnetc_exit(-1);
		/* all firehoses are remote pinned buckets */
		#ifdef GASNETC_FIREHOSE_TRACE
		pop->fh_stats = fh_onesided;
		#endif
		gasneti_mutex_lock(&gasnetc_lock_gm);
		gasnete_firehose_put_using_directed(pop, GASNETE_FH_POLL_TOKEN);
		gasneti_mutex_unlock(&gasnetc_lock_gm);
	}
	GASNETI_TRACE_EVENT_VAL(C, FIREHOSE_TOUCHED, num_buckets);
	GASNETI_TRACE_EVENT_VAL(C, FIREHOSE_MOVES, new_buckets+old_buckets);

	/* If we were dealing with implicit put, increment the iop */
	if (pop->iop != NULL)
		pop->iop->initiated_put_cnt++;
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
	pop->dest = pop->rv_addr = (uintptr_t) dest;
	pop->len = (uint32_t) nbytes;
	pop->node = node;
	pop->iop = iop;
	#if defined(TRACE) || defined(STATS)
	pop->starttime = GASNETI_STATTIME_NOW_IFENABLED(C);
	#endif

	/* Pin locally, incrementing reference counts where necessary */
	gasnetc_bucket_pin_by_addr((uintptr_t) src, nbytes);

	gasnete_firehose_move_for_put(pop);
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
		GASNETI_TRACE_PRINTF(C, ("put_nb_bulk returns handle=%p",
		    (void *) handle));
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
	pop->dest = pop->rv_addr = (uintptr_t) dest;
	pop->len = (uint32_t) nbytes;
	pop->node = node;
	pop->iop = iop;
	SET_OPMISC(pop, OPMISC_AMBUF);
	#if defined(TRACE) || defined(STATS)
	pop->starttime = GASNETI_STATTIME_NOW_IFENABLED(C);
	#endif
	GASNETE_FAST_UNALIGNED_MEMCPY(bufd->sendbuf, src, nbytes);

	gasnete_firehose_move_for_put(pop);
	return (gasnete_op_t *) pop;
}

/*
 * In the typed version of put, we always need a source copy of the local data
 * before sending it off, which doesn't require a local memory registration.  
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

/* ------------------------------------------------------------------------ */
/* Gets */
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
GASNET_INLINE_MODIFIER(gasnete_firehose_get_bulk)
gasnet_handle_t
gasnete_firehose_get_bulk(void *dest, gasnet_node_t node, void *src, 
			  size_t nbytes, gasnete_iop_t *iop GASNETE_THREAD_FARG)
{
	/* Request a Get in terms of a DMA put */
	gasnete_eop_t	*gop;

	gop = gasnete_eop_new(GASNETE_MYTHREAD);
	gop->dest = (uintptr_t) dest;
	gop->src = (uintptr_t) src;
	gop->len = nbytes;
	#if defined(TRACE) || defined(STATS)
	gop->starttime = GASNETI_STATTIME_NOW_IFENABLED(C);
	gop->fh_stats = fh_onesided;
	#endif
	gasnetc_bucket_pin_by_addr((uintptr_t) dest, nbytes);
	gop->iop = iop;
	if (iop != NULL)
		iop->initiated_get_cnt++;
	SHORT_REQ(4, 7,
	    (node, gasneti_handleridx(gasnete_firehose_get_dma_reqh), nbytes,
	     PACK(dest), PACK(src), PACK(gop)));
	return (gasnete_op_t *) gop;
}

extern gasnet_handle_t
gasnete_get_nb_bulk (void *dest, gasnet_node_t node, void *src, 
		     size_t nbytes GASNETE_THREAD_FARG)
{
	if (nbytes > GASNETE_GET_NON_DMA_CUTOFF) {
		GASNETI_TRACE_PRINTF(C, 
		    ("gasnete_get_nb_bulk Firehose (%d,%p <- %p,%d bytes)",
		    (unsigned) node, dest, src, nbytes));
		return gasnete_firehose_get_bulk(dest, node, src, nbytes, 
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
	if (nbytes > GASNETE_GET_NON_DMA_CUTOFF) {
		gasnete_threaddata_t * const mythread = GASNETE_MYTHREAD;
		gasnete_iop_t *iop = mythread->current_iop;
		GASNETI_TRACE_PRINTF(C, 
		    ("gasnete_get_nbi_bulk Firehose (%d,%p <- %p,%d bytes)",
		    (unsigned) node, dest, src, nbytes));
		gasnete_firehose_get_bulk(dest, node, src, nbytes, 
		    iop GASNETE_THREAD_PASS);
	}
	else 
		gasnete_extref_get_nbi_bulk(dest, node, src, 
		    nbytes GASNETE_THREAD_PASS);
	return;
}

/* The non-bulk get is similar to the bulk version */
/* gasnete_get_nb ...
 * gasnete_get_nbi ...
 */

/* ------------------------------------------------------------------------ */
extern void
gasnete_fifo_progress()	
{
	gasnete_eop_t	*eop;

	/* below is wrong in multithreaded environment */
	gasneti_mutex_lock(&gasnetc_lock_gm);
	while (gasnete_fifo_head != NULL) {
		if (!gasnetc_token_lo_acquire())
			return;
		eop = gasnete_fifo_head;
		if (OPMISC(eop) == OPMISC_RVUNPIN) {
			gasnetc_bufdesc_t 	*bufd;
			uint8_t			*ptr;
			int			*argptr;

			GASNETI_TRACE_PRINTF(C, 
			    ("Firehose fifo progress drain: eop=%p, unpin!", eop));
			if ((bufd = gasnetc_AMRequestPool_nb()) != NULL) {
				ptr = bufd->sendbuf;
				GASNETC_AMHEADER_WRITE(ptr, GASNETC_AM_SHORT, 2, 1);
				GASNETC_AMHANDLER_WRITE(&ptr[1], 
				    gasneti_handleridx(gasnetc_rendezvous_unpin_reqh));
				argptr = (int *) &ptr[GASNETC_AM_SHORT_ARGS_OFF];
				argptr[0] = (int) eop->len;
				argptr[1] = (int) eop->rv_addr;

				gasnetc_gm_send_AMRequest(bufd->sendbuf, 
				    GASNETC_AM_SHORT_HEADER_LEN(2), 
				    gasnetc_nodeid(eop->node), gasnetc_portid(eop->node), 
				    gasnetc_callback_lo_bufd, (void *)bufd, 0);
				gasnete_fifo_dequeue();
			}
			else {
				gasnetc_token_lo_release();
			}
		}
		else {
			GASNETI_TRACE_PRINTF(C, 
			    ("Firehose fifo progress drain: eop=%p, onesided", eop));
			gasnete_firehose_put_using_directed(eop, GASNETE_FH_HAVE_TOKEN);
			SET_OPMISC(eop, OPMISC_RVUNPIN);
		}
	}
	gasneti_mutex_unlock(&gasnetc_lock_gm);
}
/* ------------------------------------------------------------------------------------ */
/*
  Handlers:
  =========
*/
static gasnet_handlerentry_t const gasnete_handlers[] = {
  gasneti_handler_tableentry_with_bits(gasnete_firehose_get_dma_reqh),
  gasneti_handler_tableentry_with_bits(gasnete_firehose_get_dma_reph),

  { 0, NULL }
};

extern gasnet_handlerentry_t const *gasnete_get_handlertable() {
  return gasnete_handlers;
}

/* ------------------------------------------------------------------------------------ */
#endif

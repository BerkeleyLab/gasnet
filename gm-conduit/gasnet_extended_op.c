/* $Id: gasnet_extended_op.c,v 1.8 2004/01/05 16:20:01 bonachea Exp $
 * $Date: 2004/01/05 16:20:01 $
 * $Revision: 1.8 $
 * Description: GASNet Extended API OPs interface
 * Copyright 2002, Christian Bell <csbell@cs.berkeley.edu>
 * Copyright 2002, Dan Bonachea <bonachea@cs.berkeley.edu>
 * Terms of use are as specified in license.txt
 */

#include <gasnet.h>
#include <gasnet_tools.h>
#include <gasnet_extended_internal.h>
#include <gasnet_internal.h>

/*
  Op management
  =============
*/
extern void _gasnete_iop_check(gasnete_iop_t *iop) { gasnete_iop_check(iop); }
/*  get a new op and mark it in flight */
gasnete_eop_t *
gasnete_eop_new(gasnete_threaddata_t * const thread)
{
	gasnete_eopaddr_t head = thread->eop_free;
	if_pt (!gasnete_eopaddr_isnil(head)) {
		gasnete_eop_t *eop = GASNETE_EOPADDR_TO_PTR(thread, head);
		thread->eop_free = eop->addr;
		eop->addr = head;
		gasneti_assert(!gasnete_eopaddr_equal(thread->eop_free,head));
		gasneti_assert(eop->threadidx == thread->threadidx);
		gasneti_assert(OPTYPE(eop) == OPTYPE_EXPLICIT);
		gasneti_assert(OPTYPE(eop) == OPSTATE_FREE);
		SET_OPSTATE(eop, OPSTATE_INFLIGHT);
		return eop;
	} 
	else { /*  free list empty - need more eops */
		int bufidx = thread->eop_num_bufs;
		gasnete_eop_t *buf;
		int i;

		gasnete_threadidx_t threadidx = thread->threadidx;
		if (bufidx == 256) 
			gasneti_fatalerror("GASNet Extended API: Ran out "
			    "of explicit handles (limit=65535)");
		thread->eop_num_bufs++;
		buf = (gasnete_eop_t *)
		gasneti_calloc(256, sizeof(gasnete_eop_t));
		for (i=0; i < 256; i++) {
			gasnete_eopaddr_t addr;
			addr.bufferidx = bufidx;
			#if GASNETE_SCATTER_EOPS_ACROSS_CACHELINES
				#ifdef GASNETE_EOP_MOD
				addr.eopidx = (i+32) % 255;
				#else
					{ int k = i+32;
					addr.eopidx = k > 255 ? k - 255 : k;
					}
				#endif
			#else
				addr.eopidx = i+1;
			#endif
			buf[i].threadidx = threadidx;
			buf[i].addr = addr;
		}
		/*  add a list terminator */
		#if GASNETE_SCATTER_EOPS_ACROSS_CACHELINES
			#ifdef GASNETE_EOP_MOD
				buf[223].addr.eopidx = 255; 
				/* modular arithmetic messes up this one */
			#endif
			buf[255].addr = EOPADDR_NIL;
		#else
			buf[255].addr = EOPADDR_NIL;
		#endif
		thread->eop_bufs[bufidx] = buf;
		head.bufferidx = bufidx;
		head.eopidx = 0;
		thread->eop_free = head;

		#if GASNET_DEBUG
		{ /* verify new free list got built correctly */
			int i;
			int seen[256];
			gasnete_eopaddr_t addr = thread->eop_free;

                        gasneti_memcheck(thread->eop_bufs[bufidx]);
			memset(seen, 0, 256*sizeof(int));
			for (i=0;i<(bufidx==255?255:256);i++) {
				gasnete_eop_t *eop; 
				gasneti_assert(!gasnete_eopaddr_isnil(addr));
				eop = GASNETE_EOPADDR_TO_PTR(thread,addr);
				gasneti_assert(OPTYPE(eop) == OPTYPE_EXPLICIT);
				gasneti_assert(OPSTATE(eop) == OPSTATE_FREE);
				gasneti_assert(eop->threadidx == threadidx);
				gasneti_assert(addr.bufferidx == bufidx);
				/* see if we hit a cycle */
				gasneti_assert(!seen[addr.eopidx]);
				seen[addr.eopidx] = 1;
				addr = eop->addr;
			}
			gasneti_assert(gasnete_eopaddr_isnil(addr)); 
		}
		#endif
		/*  should succeed this time */
		return gasnete_eop_new(thread);
	}
}

gasnete_iop_t *
gasnete_iop_new(gasnete_threaddata_t * const thread)
{
	gasnete_iop_t *iop;

	if_pt (thread->iop_free) {
		iop = thread->iop_free;
		thread->iop_free = iop->next;
                gasneti_memcheck(iop);
		gasneti_assert(OPTYPE(iop) == OPTYPE_IMPLICIT);
		gasneti_assert(iop->threadidx == thread->threadidx);
	} else {
		iop = (gasnete_iop_t *)gasneti_malloc(sizeof(gasnete_iop_t));
		SET_OPTYPE((gasnete_op_t *)iop, OPTYPE_IMPLICIT);
		iop->threadidx = thread->threadidx;
	}
	iop->next = NULL;
	iop->initiated_get_cnt = 0;
	iop->initiated_put_cnt = 0;
	gasneti_atomic_set(&(iop->completed_get_cnt), 0);
	gasneti_atomic_set(&(iop->completed_put_cnt), 0);
	return iop;
}

/*  query an op for completeness - for iop this means both puts and gets */
int 
gasnete_op_isdone(gasnete_op_t *op) 
{
	gasneti_assert(op->threadidx == gasnete_mythread()->threadidx);
	if_pt (OPTYPE(op) == OPTYPE_EXPLICIT) {
		gasneti_assert(OPSTATE(op) != OPSTATE_FREE);
                gasnete_eop_check((gasnete_eop_t *)op);
		return OPSTATE(op) == OPSTATE_COMPLETE;
	} else {
		gasnete_iop_t *iop = (gasnete_iop_t*)op;
                gasnete_iop_check(iop);
		return 
		    (gasneti_atomic_read(&(iop->completed_get_cnt)) == 
		         iop->initiated_get_cnt) &&
		    (gasneti_atomic_read(&(iop->completed_put_cnt)) == 
		         iop->initiated_put_cnt);
	}
}

/*  mark an op done - isget ignored for explicit ops */
void gasnete_op_markdone(gasnete_op_t *op, int isget) {
	if (OPTYPE(op) == OPTYPE_EXPLICIT) {
		gasnete_eop_t *eop = (gasnete_eop_t *)op;
		gasneti_assert(OPSTATE(eop) == OPSTATE_INFLIGHT);
                gasnete_eop_check(eop);
		SET_OPSTATE(eop, OPSTATE_COMPLETE);
	} else {
		gasnete_iop_t *iop = (gasnete_iop_t *)op;
                gasnete_iop_check(iop);
		if (isget) 
			gasneti_atomic_increment(&(iop->completed_get_cnt));
		else 
			gasneti_atomic_increment(&(iop->completed_put_cnt));
	}
}

/*  free an op */
void gasnete_op_free(gasnete_op_t *op) {
	gasnete_threaddata_t * const thread = gasnete_threadtable[op->threadidx];
	gasneti_assert(thread == gasnete_mythread());
	if (OPTYPE(op) == OPTYPE_EXPLICIT) {
		gasnete_eop_t *eop = (gasnete_eop_t *)op;
		gasnete_eopaddr_t addr = eop->addr;
		gasneti_assert(OPSTATE(eop) == OPSTATE_COMPLETE);
                gasnete_eop_check(eop);
		SET_OPSTATE(eop, OPSTATE_FREE);
		eop->addr = thread->eop_free;
		thread->eop_free = addr;
	} else {
		gasnete_iop_t *iop = (gasnete_iop_t *)op;
                gasnete_iop_check(iop);
                gasneti_assert(iop->next == NULL);
		iop->next = thread->iop_free;
		thread->iop_free = iop;
	}
}

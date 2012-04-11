/*   $Source: /Users/kamil/work/gasnet-cvs2/gasnet/pami-conduit/gasnet_extended.c,v $
 *     $Date: 2012/04/11 20:09:46 $
 * $Revision: 1.1.2.12 $
 * Description: GASNet Extended API PAMI-conduit Implementation
 * Copyright 2002, Dan Bonachea <bonachea@cs.berkeley.edu>
 * Copyright 2012, Lawrence Berkeley National Laboratory
 * Terms of use are as specified in license.txt
 */

#include <gasnet_internal.h>
#include <gasnet_core_internal.h>
#include <gasnet_extended_internal.h>
#include <gasnet_handler.h>

static pami_send_hint_t gasnete_null_send_hint;
static const gasnete_eopaddr_t EOPADDR_NIL = { { 0xFF, 0xFF } };
extern void _gasnete_iop_check(gasnete_iop_t *iop) { gasnete_iop_check(iop); }

/* ------------------------------------------------------------------------------------ */
/*
  Extended API Common Code
  ========================
  Factored bits of extended API code common to most conduits, overridable when necessary
*/

#include "gasnet_extended_common.c"

/* ------------------------------------------------------------------------------------ */
/*
  Initialization
  ==============
*/
/* called at startup to check configuration sanity */
static void gasnete_check_config(void) {
  gasneti_check_config_postattach();

  gasneti_assert_always(gasnete_eopaddr_isnil(EOPADDR_NIL));
}

extern void gasnete_init(void) {
  static int firstcall = 1;
  GASNETI_TRACE_PRINTF(C,("gasnete_init()"));
  gasneti_assert(firstcall); /*  make sure we haven't been called before */
  firstcall = 0;

  gasnete_check_config(); /*  check for sanity */

  memset(&gasnete_null_send_hint, 0, sizeof(gasnete_null_send_hint));
#if GASNET_PSHM
  gasnete_null_send_hint.use_shmem = PAMI_HINT_DISABLE;
#endif

  gasneti_assert(gasneti_nodes >= 1 && gasneti_mynode < gasneti_nodes);

  { gasnete_threaddata_t *threaddata = NULL;
    gasnete_eop_t *eop = NULL;
    #if GASNETI_CLIENT_THREADS
      /* register first thread (optimization) */
      threaddata = gasnete_mythread(); 
    #else
      /* register only thread (required) */
      threaddata = gasnete_new_threaddata();
    #endif

    /* cause the first pool of eops to be allocated (optimization) */
    eop = gasnete_eop_new(threaddata);
    gasnete_op_markdone((gasnete_op_t *)eop, 0);
    gasnete_op_free((gasnete_op_t *)eop);
  }

  /* Initialize barrier resources */
  gasnete_barrier_init();

  /* Initialize VIS subsystem */
  gasnete_vis_init();
}

/* ------------------------------------------------------------------------------------ */
/*
  Op management
  =============
*/
/*  get a new op and mark it in flight */
gasnete_eop_t *gasnete_eop_new(gasnete_threaddata_t * const thread) {
  gasnete_eopaddr_t head = thread->eop_free;
  if_pt (!gasnete_eopaddr_isnil(head)) {
    gasnete_eop_t *eop = GASNETE_EOPADDR_TO_PTR(thread, head);
    thread->eop_free = eop->addr;
    eop->addr = head;
    gasneti_assert(!gasnete_eopaddr_equal(thread->eop_free,head));
    gasneti_assert(eop->threadidx == thread->threadidx);
    gasneti_assert(OPTYPE(eop) == OPTYPE_EXPLICIT);
    gasneti_assert(OPTYPE(eop) == OPSTATE_FREE);
#if 0
    gasnete_op_clr_lc((gasnete_op_t *)eop));
    SET_OPSTATE(eop, OPSTATE_INFLIGHT);
#else
    eop->flags = OPSTATE_INFLIGHT;
#endif
    return eop;
  } else { /*  free list empty - need more eops */
    int bufidx = thread->eop_num_bufs;
    gasnete_eop_t *buf;
    int i;
    gasnete_threadidx_t threadidx = thread->threadidx;
    if (bufidx == 256) gasneti_fatalerror("GASNet Extended API: Ran out of explicit handles (limit=65535)");
    thread->eop_num_bufs++;
    buf = (gasnete_eop_t *)gasneti_calloc(256,sizeof(gasnete_eop_t));
    gasneti_leak(buf);
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
      #if 0 /* these can safely be skipped when the values are zero */
        SET_OPSTATE(&(buf[i]),OPSTATE_FREE); 
        SET_OPTYPE(&(buf[i]),OPTYPE_EXPLICIT); 
      #endif
    }
     /*  add a list terminator */
    #if GASNETE_SCATTER_EOPS_ACROSS_CACHELINES
      #ifdef GASNETE_EOP_MOD
        buf[223].addr.eopidx = 255; /* modular arithmetic messes up this one */
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

      #if 0
      if (gasneti_mynode == 0)
        for (i=0;i<256;i++) {                                   
          fprintf(stderr,"%i:  %i: next=%i\n",gasneti_mynode,i,buf[i].addr.eopidx);
          fflush(stderr);
        }
        sleep(5);
      #endif

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
        gasneti_assert(!seen[addr.eopidx]);/* see if we hit a cycle */
        seen[addr.eopidx] = 1;
        addr = eop->addr;                                     
      }                                                       
      gasneti_assert(gasnete_eopaddr_isnil(addr)); 
    }
    #endif

    return gasnete_eop_new(thread); /*  should succeed this time */
  }
}

gasnete_iop_t *gasnete_iop_new(gasnete_threaddata_t * const thread) {
  gasnete_iop_t *iop;
  if_pt (thread->iop_free) {
    iop = thread->iop_free;
    thread->iop_free = iop->next;
    gasneti_memcheck(iop);
    gasneti_assert(OPTYPE(iop) == OPTYPE_IMPLICIT);
    gasneti_assert(! gasnete_op_read_lc((gasnete_op_t *)iop));
    gasneti_assert(iop->threadidx == thread->threadidx);
  } else {
    iop = (gasnete_iop_t *)gasneti_malloc(sizeof(gasnete_iop_t));
    gasneti_leak(iop);
    #if GASNET_DEBUG
      memset(iop, 0, sizeof(gasnete_iop_t)); /* set pad to known value */
    #endif
#if 0
    gasnete_op_clr_lc((gasnete_op_t *)iop));
    SET_OPTYPE((gasnete_op_t *)iop, OPTYPE_IMPLICIT);
#else
    iop->flags = OPTYPE_IMPLICIT;
#endif
    iop->threadidx = thread->threadidx;
  }
  iop->next = NULL;
  iop->initiated_get_cnt = 0;
  iop->initiated_put_cnt = 0;
  gasneti_weakatomic_set(&(iop->completed_get_cnt), 0, 0);
  gasneti_weakatomic_set(&(iop->completed_put_cnt), 0, 0);
  gasnete_iop_check(iop);
  return iop;
}

/*  query an op for completeness - for iop this means both puts and gets */
int gasnete_op_isdone(gasnete_op_t *op) {
  gasneti_assert(op->threadidx == gasnete_mythread()->threadidx);
  if_pt (OPTYPE(op) == OPTYPE_EXPLICIT) {
    gasneti_assert(OPSTATE(op) != OPSTATE_FREE);
    gasnete_eop_check((gasnete_eop_t *)op);
    return OPSTATE(op) == OPSTATE_COMPLETE;
  } else {
    gasnete_iop_t *iop = (gasnete_iop_t*)op;
    gasnete_iop_check(iop);
    return (gasneti_weakatomic_read(&(iop->completed_get_cnt), 0) == iop->initiated_get_cnt) &&
           (gasneti_weakatomic_read(&(iop->completed_put_cnt), 0) == iop->initiated_put_cnt);
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
    if (isget) gasneti_weakatomic_increment(&(iop->completed_get_cnt), 0);
    else gasneti_weakatomic_increment(&(iop->completed_put_cnt), 0);
  }
}

/* callbacks implementing subsets of gasnete_op_markdone */
static void gasnete_cb_eop_done(pami_context_t context, void *cookie, pami_result_t status) {
  gasnete_eop_t *eop = (gasnete_eop_t *)cookie;
  gasneti_assert(OPSTATE(eop) == OPSTATE_INFLIGHT);
  /* gasnete_eop_check(eop);  XXX: conflicts w/ on-stack EOP used for blocking ops */
  SET_OPSTATE(eop, OPSTATE_COMPLETE);
}
static void gasnete_cb_iput_done(pami_context_t context, void *cookie, pami_result_t status) {
  gasnete_iop_t *iop = (gasnete_iop_t *)cookie;
  gasnete_iop_check(iop);
  gasneti_weakatomic_increment(&(iop->completed_put_cnt), 0);
}
static void gasnete_cb_iget_done(pami_context_t context, void *cookie, pami_result_t status) {
  gasnete_iop_t *iop = (gasnete_iop_t *)cookie;
  gasnete_iop_check(iop);
  gasneti_weakatomic_increment(&(iop->completed_get_cnt), 0);
}

/* callback for local completion of a non-bulk put */
static void gasnete_cb_op_lc(pami_context_t context, void *cookie, pami_result_t status) {
  gasnete_op_t *op = (gasnete_op_t *)cookie;
  if (OPTYPE(op) == OPTYPE_EXPLICIT) {
    gasnete_eop_t *eop = (gasnete_eop_t *)op;
    /* While rare, the REMOTE completion event might be processed before the LOCAL one.
     * So, OPSTATE_COMPLETE is a valid state here. */
    gasneti_assert((OPSTATE(eop) == OPSTATE_INFLIGHT) ||
                   (OPSTATE(eop) == OPSTATE_COMPLETE));
    gasnete_eop_check(eop);
  } else {
    gasnete_iop_t *iop = (gasnete_iop_t *)op;
    gasnete_iop_check(iop);
  }
  gasneti_assert(! gasnete_op_read_lc(op));
  gasnete_op_set_lc(op);
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
/* ------------------------------------------------------------------------------------ */
/* GASNET-Internal OP Interface */
gasneti_eop_t *gasneti_eop_create(GASNETE_THREAD_FARG_ALONE) {
  gasnete_eop_t *op = gasnete_eop_new(GASNETE_MYTHREAD);
  return (gasneti_eop_t *)op;
}
gasneti_iop_t *gasneti_iop_register(unsigned int noperations, int isget GASNETE_THREAD_FARG) {
  gasnete_threaddata_t * const mythread = GASNETE_MYTHREAD;
  gasnete_iop_t * const op = mythread->current_iop;
  gasnete_iop_check(op);
  if (isget) op->initiated_get_cnt += noperations;
  else       op->initiated_put_cnt += noperations;
  gasnete_iop_check(op);
  return (gasneti_iop_t *)op;
}
void gasneti_eop_markdone(gasneti_eop_t *eop) {
  gasnete_op_markdone((gasnete_op_t *)eop, 0);
}
void gasneti_iop_markdone(gasneti_iop_t *iop, unsigned int noperations, int isget) {
  gasnete_iop_t *op = (gasnete_iop_t *)iop;
  gasneti_weakatomic_t * const pctr = (isget ? &(op->completed_get_cnt) : &(op->completed_put_cnt));
  gasnete_iop_check(op);
  if (noperations == 1) gasneti_weakatomic_increment(pctr, 0);
  else {
    #if defined(GASNETI_HAVE_WEAKATOMIC_ADD_SUB)
      gasneti_weakatomic_add(pctr, noperations, 0);
    #else /* yuk */
      while (noperations) {
        gasneti_weakatomic_increment(pctr, 0);
        noperations--;
      }
    #endif
  }
  gasnete_iop_check(op);
}
/* ------------------------------------------------------------------------------------ */
/*
 * Design/Approach for gets/puts in Extended API in terms of PAMI
 * ========================================================================
 *
 * gasnet_put(_bulk) is translated to PAMI_Put()
 *   and blocks on an on-stack eop
 *
 * gasnet_get(_bulk) is translated to PAMI_Get()
 *   and blocks on an on-stack eop
 *
 * gasnete_put_nb(_bulk) translates to PAMI_Put()
 *   non-bulk spin-polls for local-completion flag in the eop
 *
 * gasnete_get_nb(_bulk) translates to PAMI_Get()
 *
 * gasnete_put_nbi(_bulk) translates to PAMI_Put();
 *   non-bulk spin-polls for local-completion flag in the eop
 *
 * gasnete_get_nbi(_bulk) translates to PAMI_Get()
 *
 * For the local-completion, the design is slightly complicated by the fact
 * that while PAMI has distinct Local and Remote completion callbacks, they
 * are passed the same "cookie".  This means one can't just block for local
 * completion by spinning on a stack variable without some method to get
 * both the handle and the spin-flag from the same pointer.  Rather than
 * try to do that, an unused bit in the 'flags' field common to all ops has
 * been allocated to be an "LC" flag, on which we can spin.
 */

/* ------------------------------------------------------------------------------------ */
/*
  Non-blocking memory-to-memory transfers (helpers)
  ==========================================================
*/

// TODO: use Rput when both src and dest are in-segment
GASNETI_INLINE(gasnete_put_common)
void gasnete_put_common(gasnet_node_t node, void *dest, void *src, size_t nbytes,
                        gasnete_op_t *op, int need_lc, int is_eop) {
  pami_put_simple_t cmd;

  cmd.rma.dest = gasnetc_endpoint(node);
  cmd.rma.hints = gasnete_null_send_hint;
  cmd.rma.bytes = nbytes;
  cmd.rma.cookie = op;
  cmd.rma.done_fn = need_lc ? gasnete_cb_op_lc : NULL;
  cmd.addr.local = src;
  cmd.addr.remote = dest;
  cmd.put.rdone_fn = is_eop ? gasnete_cb_eop_done : gasnete_cb_iput_done;

  PAMI_Context_lock(gasnetc_context);
  {
    pami_result_t rc;

    rc = PAMI_Put(gasnetc_context, &cmd);
    GASNETC_PAMI_CHECK(rc, "calling PAMI_Put");

    /* Always advance at least once */
    do {
      rc = PAMI_Context_advance(gasnetc_context, 1);
      GASNETC_PAMI_CHECK_ADVANCE(rc, "advancing PAMI_Put");
    } while (need_lc && !gasnete_op_read_lc((gasnete_op_t *)op));
  }
  PAMI_Context_unlock(gasnetc_context);
}

// TODO: use Rget when both src and dest are in-segment
GASNETI_INLINE(gasnete_get_common)
void gasnete_get_common(void *dest, gasnet_node_t node, void *src, size_t nbytes,
                        gasnete_op_t *op, int is_eop) {
  pami_get_simple_t cmd;

  cmd.rma.dest = gasnetc_endpoint(node);
  cmd.rma.hints = gasnete_null_send_hint;
  cmd.rma.bytes = nbytes;
  cmd.rma.cookie = op;
  cmd.rma.done_fn = is_eop ? gasnete_cb_eop_done : gasnete_cb_iget_done;
  cmd.addr.local = dest;
  cmd.addr.remote = src;

  PAMI_Context_lock(gasnetc_context);
  { pami_result_t rc;

    rc = PAMI_Get(gasnetc_context, &cmd);
    GASNETC_PAMI_CHECK(rc, "calling PAMI_Get");

    rc = PAMI_Context_advance(gasnetc_context, 1);
    GASNETC_PAMI_CHECK_ADVANCE(rc, "advancing PAMI_Get");
  }
  PAMI_Context_unlock(gasnetc_context);
}

GASNETI_INLINE(gasnete_memset_common)
void gasnete_memset_common(gasnet_node_t node, void *dest, int val, size_t nbytes,
                           gasnete_op_t *op) {
  GASNETI_SAFE(
    SHORT_REQ(4,7,(node, gasneti_handleridx(gasnete_memset_reqh),
                   (gasnet_handlerarg_t)val, PACK(nbytes),
                   PACK(dest), PACK(op))));
}

/* ------------------------------------------------------------------------------------ */
/*
  Non-blocking memory-to-memory transfers (explicit handle)
  ==========================================================
*/
/* ------------------------------------------------------------------------------------ */
GASNETI_INLINE(gasnete_memset_reqh_inner)
void gasnete_memset_reqh_inner(gasnet_token_t token, 
  gasnet_handlerarg_t val, void *nbytes_arg, void *dest, void *op) {
  size_t nbytes = (uintptr_t)nbytes_arg;
  memset(dest, (int)(uint32_t)val, nbytes);
  gasneti_sync_writes();
  GASNETI_SAFE(
    SHORT_REP(1,2,(token, gasneti_handleridx(gasnete_markdone_reph),
                  PACK(op))));
}
SHORT_HANDLER(gasnete_memset_reqh,4,7,
              (token, a0, UNPACK(a1),      UNPACK(a2),      UNPACK(a3)     ),
              (token, a0, UNPACK2(a1, a2), UNPACK2(a3, a4), UNPACK2(a5, a6)));
/* ------------------------------------------------------------------------------------ */
GASNETI_INLINE(gasnete_markdone_reph_inner)
void gasnete_markdone_reph_inner(gasnet_token_t token, 
  void *op) {
  gasnete_op_markdone((gasnete_op_t *)op, 0); /*  assumes this is a put or explicit */
}
SHORT_HANDLER(gasnete_markdone_reph,1,2,
              (token, UNPACK(a0)    ),
              (token, UNPACK2(a0, a1)));
/* ------------------------------------------------------------------------------------ */

extern gasnet_handle_t gasnete_get_nb_bulk (void *dest, gasnet_node_t node, void *src, size_t nbytes GASNETE_THREAD_FARG) {
  GASNETI_CHECKPSHM_GET(UNALIGNED,H);
  {
    gasnete_eop_t * op = gasnete_eop_new(GASNETE_MYTHREAD);
    gasnete_get_common(dest, node, src, nbytes, (gasnete_op_t *)op, 1);
    return (gasnet_handle_t)op;
  }
}

extern gasnet_handle_t gasnete_put_nb      (gasnet_node_t node, void *dest, void *src, size_t nbytes GASNETE_THREAD_FARG) {
  GASNETI_CHECKPSHM_PUT(ALIGNED,H);
  {
    gasnete_eop_t * op = gasnete_eop_new(GASNETE_MYTHREAD);
    gasnete_put_common(node, dest, src, nbytes, (gasnete_op_t *)op, 1, 1);
    gasneti_assert(gasnete_op_read_lc((gasnete_op_t *)op));
    return (gasnet_handle_t)op;
  }
}

extern gasnet_handle_t gasnete_put_nb_bulk (gasnet_node_t node, void *dest, void *src, size_t nbytes GASNETE_THREAD_FARG) {
  GASNETI_CHECKPSHM_PUT(UNALIGNED,H);
  {
    gasnete_eop_t * op = gasnete_eop_new(GASNETE_MYTHREAD);
    gasnete_put_common(node, dest, src, nbytes, (gasnete_op_t *)op, 0, 1);
    return (gasnet_handle_t)op;
  }
}

extern gasnet_handle_t gasnete_memset_nb   (gasnet_node_t node, void *dest, int val, size_t nbytes GASNETE_THREAD_FARG) {
 GASNETI_CHECKPSHM_MEMSET(H);
 {
  gasnete_eop_t *op = gasnete_eop_new(GASNETE_MYTHREAD);
  gasnete_memset_common(node, dest, val, nbytes, (gasnete_op_t *)op);
  return (gasnet_handle_t)op;
 }
}

/* ------------------------------------------------------------------------------------ */
/*
  Synchronization for explicit-handle non-blocking operations:
  ===========================================================
*/

extern int  gasnete_try_syncnb(gasnet_handle_t handle) {
  GASNETI_SAFE(gasneti_AMPoll());

  if (gasnete_op_isdone(handle)) {
    gasneti_sync_reads();
    gasnete_op_free(handle);
    return GASNET_OK;
  }
  else return GASNET_ERR_NOT_READY;
}

extern int  gasnete_try_syncnb_some (gasnet_handle_t *phandle, size_t numhandles) {
  int success = 0;
  int empty = 1;
  GASNETI_SAFE(gasneti_AMPoll());

  gasneti_assert(phandle);

  { int i;
    for (i = 0; i < numhandles; i++) {
      gasnete_op_t *op = phandle[i];
      if (op != GASNET_INVALID_HANDLE) {
        empty = 0;
        if (gasnete_op_isdone(op)) {
	  gasneti_sync_reads();
          gasnete_op_free(op);
          phandle[i] = GASNET_INVALID_HANDLE;
          success = 1;
        }  
      }
    }
  }

  if (success || empty) return GASNET_OK;
  else return GASNET_ERR_NOT_READY;
}

extern int  gasnete_try_syncnb_all (gasnet_handle_t *phandle, size_t numhandles) {
  int success = 1;
  GASNETI_SAFE(gasneti_AMPoll());

  gasneti_assert(phandle);

  { int i;
    for (i = 0; i < numhandles; i++) {
      gasnete_op_t *op = phandle[i];
      if (op != GASNET_INVALID_HANDLE) {
        if (gasnete_op_isdone(op)) {
	  gasneti_sync_reads();
          gasnete_op_free(op);
          phandle[i] = GASNET_INVALID_HANDLE;
        } else success = 0;
      }
    }
  }

  if (success) return GASNET_OK;
  else return GASNET_ERR_NOT_READY;
}

/* ------------------------------------------------------------------------------------ */
/*
  Non-blocking memory-to-memory transfers (implicit handle)
  ==========================================================
  each completion increments a counter - we compare this to the  number of implicit ops launched
  for memset only, the completion is an explicit AM-level ack
*/

extern void gasnete_get_nbi_bulk (void *dest, gasnet_node_t node, void *src, size_t nbytes GASNETE_THREAD_FARG) {
  GASNETI_CHECKPSHM_GET(UNALIGNED,V);
  {
    gasnete_threaddata_t * const mythread = GASNETE_MYTHREAD;
    gasnete_iop_t * const op = mythread->current_iop;
    op->initiated_get_cnt++;
    gasnete_get_common(dest, node, src, nbytes, (gasnete_op_t *)op, 0);
  }
}

extern void gasnete_put_nbi      (gasnet_node_t node, void *dest, void *src, size_t nbytes GASNETE_THREAD_FARG) {
  GASNETI_CHECKPSHM_PUT(ALIGNED,V);
  {
    gasnete_threaddata_t * const mythread = GASNETE_MYTHREAD;
    gasnete_iop_t * const op = mythread->current_iop;
    op->initiated_put_cnt++;
    gasnete_put_common(node, dest, src, nbytes, (gasnete_op_t *)op, 1, 0);
    /* reset LC flag for next time: */
    gasneti_assert(gasnete_op_read_lc((gasnete_op_t *)op));
  #if 0
    gasnete_op_clr_lc((gasnete_op_t *)op));
  #else
    op->flags = OPTYPE_IMPLICIT; /* Should be cheaper than r-m-w */
  #endif
  }
}

extern void gasnete_put_nbi_bulk (gasnet_node_t node, void *dest, void *src, size_t nbytes GASNETE_THREAD_FARG) {
  GASNETI_CHECKPSHM_PUT(UNALIGNED,V);
  {
    gasnete_threaddata_t * const mythread = GASNETE_MYTHREAD;
    gasnete_iop_t * const op = mythread->current_iop;
    op->initiated_put_cnt++;
    gasnete_put_common(node, dest, src, nbytes, (gasnete_op_t *)op, 0, 0);
  }
}

extern void gasnete_memset_nbi   (gasnet_node_t node, void *dest, int val, size_t nbytes GASNETE_THREAD_FARG) {
  GASNETI_CHECKPSHM_MEMSET(V);
  {
    gasnete_threaddata_t * const mythread = GASNETE_MYTHREAD;
    gasnete_iop_t *op = mythread->current_iop;
    op->initiated_put_cnt++;
    gasnete_memset_common(node, dest, val, nbytes, (gasnete_op_t *)op);
  }
}

/* ------------------------------------------------------------------------------------ */
/*
  Synchronization for implicit-handle non-blocking operations:
  ===========================================================
*/

extern int  gasnete_try_syncnbi_gets(GASNETE_THREAD_FARG_ALONE) {
  #if 0
    /* polling for syncnbi now happens in header file to avoid duplication */
    GASNETI_SAFE(gasneti_AMPoll());
  #endif
  {
    gasnete_threaddata_t * const mythread = GASNETE_MYTHREAD;
    gasnete_iop_t *iop = mythread->current_iop;
    gasneti_assert(iop->threadidx == mythread->threadidx);
    gasneti_assert(OPTYPE(iop) == OPTYPE_IMPLICIT);
    gasneti_assert(! gasnete_op_read_lc((gasnete_op_t *)iop));
    #if GASNET_DEBUG
      if (iop->next != NULL)
        gasneti_fatalerror("VIOLATION: attempted to call gasnete_try_syncnbi_gets() inside an NBI access region");
    #endif

    if (gasneti_weakatomic_read(&(iop->completed_get_cnt), 0) == iop->initiated_get_cnt) {
      if_pf (iop->initiated_get_cnt > 65000) { /* make sure we don't overflow the counters */
        gasneti_weakatomic_set(&(iop->completed_get_cnt), 0, 0);
        iop->initiated_get_cnt = 0;
      }
      gasneti_sync_reads();
      return GASNET_OK;
    } else return GASNET_ERR_NOT_READY;
  }
}

extern int  gasnete_try_syncnbi_puts(GASNETE_THREAD_FARG_ALONE) {
  #if 0
    /* polling for syncnbi now happens in header file to avoid duplication */
    GASNETI_SAFE(gasneti_AMPoll());
  #endif
  {
    gasnete_threaddata_t * const mythread = GASNETE_MYTHREAD;
    gasnete_iop_t *iop = mythread->current_iop;
    gasneti_assert(iop->threadidx == mythread->threadidx);
    gasneti_assert(iop->next == NULL);
    gasneti_assert(OPTYPE(iop) == OPTYPE_IMPLICIT);
    gasneti_assert(! gasnete_op_read_lc((gasnete_op_t *)iop));
    #if GASNET_DEBUG
      if (iop->next != NULL)
        gasneti_fatalerror("VIOLATION: attempted to call gasnete_try_syncnbi_puts() inside an NBI access region");
    #endif


    if (gasneti_weakatomic_read(&(iop->completed_put_cnt), 0) == iop->initiated_put_cnt) {
      if_pf (iop->initiated_put_cnt > 65000) { /* make sure we don't overflow the counters */
        gasneti_weakatomic_set(&(iop->completed_put_cnt), 0, 0);
        iop->initiated_put_cnt = 0;
      }
      gasneti_sync_reads();
      return GASNET_OK;
    } else return GASNET_ERR_NOT_READY;
  }
}

/* ------------------------------------------------------------------------------------ */
/*
  Implicit access region synchronization
  ======================================
*/
/*  This implementation allows recursive access regions, although the spec does not require that */
/*  operations are associated with the most immediately enclosing access region */
extern void            gasnete_begin_nbi_accessregion(int allowrecursion GASNETE_THREAD_FARG) {
  gasnete_threaddata_t * const mythread = GASNETE_MYTHREAD;
  gasnete_iop_t *iop = gasnete_iop_new(mythread); /*  push an iop  */
  GASNETI_TRACE_PRINTF(S,("BEGIN_NBI_ACCESSREGION"));
  #if GASNET_DEBUG
    if (!allowrecursion && mythread->current_iop->next != NULL)
      gasneti_fatalerror("VIOLATION: tried to initiate a recursive NBI access region");
  #endif
  iop->next = mythread->current_iop;
  mythread->current_iop = iop;
}

extern gasnet_handle_t gasnete_end_nbi_accessregion(GASNETE_THREAD_FARG_ALONE) {
  gasnete_threaddata_t * const mythread = GASNETE_MYTHREAD;
  gasnete_iop_t *iop = mythread->current_iop; /*  pop an iop */
  GASNETI_TRACE_EVENT_VAL(S,END_NBI_ACCESSREGION,iop->initiated_get_cnt + iop->initiated_put_cnt);
  #if GASNET_DEBUG
    if (iop->next == NULL)
      gasneti_fatalerror("VIOLATION: call to gasnete_end_nbi_accessregion() outside access region");
  #endif
  mythread->current_iop = iop->next;
  iop->next = NULL;
  return (gasnet_handle_t)iop;
}

/* ------------------------------------------------------------------------------------ */
/*
  Blocking memory-to-memory transfers
  ===================================
*/

#if GASNETI_DIRECT_GET_BULK
extern void gasnete_get_bulk (void *dest, gasnet_node_t node, void *src,
			      size_t nbytes GASNETE_THREAD_FARG) {
  GASNETI_CHECKPSHM_GET(UNALIGNED,V);
  {
    volatile gasnete_eop_t op = { OPSTATE_INFLIGHT, };
    gasnete_get_common(dest, node, src, nbytes, (gasnete_op_t *)&op, 1);
    gasneti_polluntil(op.flags == OPSTATE_COMPLETE);
  }
}
#endif

#if GASNETI_DIRECT_PUT_BULK
extern void gasnete_put_bulk (gasnet_node_t node, void* dest, void *src,
			      size_t nbytes GASNETE_THREAD_FARG) {
  GASNETI_CHECKPSHM_PUT(UNALIGNED,V);
  {
    volatile gasnete_eop_t op = { OPSTATE_INFLIGHT, };
    gasnete_put_common(node, dest, src, nbytes, (gasnete_op_t *)&op, 0, 1);
    gasneti_polluntil(op.flags == OPSTATE_COMPLETE);
  }
}   
#endif

/* ------------------------------------------------------------------------------------ */
/*
  Barriers:
  =========
*/

/* use reference implementation of barrier */
#define GASNETI_GASNET_EXTENDED_REFBARRIER_C 1
#include "gasnet_extended_refbarrier.c"
#undef GASNETI_GASNET_EXTENDED_REFBARRIER_C

/* ------------------------------------------------------------------------------------ */
/*
  Vector, Indexed & Strided:
  =========================
*/

/* use reference implementation of scatter/gather and strided */
#include "gasnet_extended_refvis.h"

/* ------------------------------------------------------------------------------------ */
/*
  Collectives:
  ============
*/

/* use reference implementation of collectives */
#include "gasnet_extended_refcoll.h"

/* ------------------------------------------------------------------------------------ */
/*
  Handlers:
  =========
*/
static gasnet_handlerentry_t const gasnete_handlers[] = {
  #ifdef GASNETE_REFBARRIER_HANDLERS
    GASNETE_REFBARRIER_HANDLERS(),
  #endif
  #ifdef GASNETE_REFVIS_HANDLERS
    GASNETE_REFVIS_HANDLERS()
  #endif
  #ifdef GASNETE_REFCOLL_HANDLERS
    GASNETE_REFCOLL_HANDLERS()
  #endif

  /* ptr-width independent handlers */

  /* ptr-width dependent handlers */
  gasneti_handler_tableentry_with_bits(gasnete_memset_reqh),
  gasneti_handler_tableentry_with_bits(gasnete_markdone_reph),

  { 0, NULL }
};

extern gasnet_handlerentry_t const *gasnete_get_handlertable(void) {
  return gasnete_handlers;
}
/* ------------------------------------------------------------------------------------ */


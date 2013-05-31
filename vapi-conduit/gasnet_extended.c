/*   $Source: /Users/kamil/work/gasnet-cvs2/gasnet/vapi-conduit/Attic/gasnet_extended.c,v $
 *     $Date: 2013/05/31 00:02:44 $
 * $Revision: 1.64.2.1 $
 * Description: GASNet Extended API over VAPI/IB Implementation
 * Copyright 2002, Dan Bonachea <bonachea@cs.berkeley.edu>
 * Terms of use are as specified in license.txt
 */

#include <gasnet_internal.h>
#include <gasnet_extended_internal.h>
#include <gasnet_handler.h>

static const gasnete_eopaddr_t EOPADDR_NIL = { { 0xFF, 0xFF } };
extern void _gasnete_iop_check(gasnete_iop_t *iop) { gasnete_iop_check(iop); }

/* ------------------------------------------------------------------------------------ */
/*
  Op management
  =============
*/
/*  get a new explicit op */
gasnete_eop_t *gasnete_eop_new(gasnete_threaddata_t * const thread) {
  gasnete_eopaddr_t head = thread->eop_free;
  if_pt (!gasnete_eopaddr_isnil(head)) {
    gasnete_eop_t *eop = GASNETE_EOPADDR_TO_PTR(thread, head);
    thread->eop_free = eop->addr;
    eop->addr = head;
    gasneti_assert(!gasnete_eopaddr_equal(thread->eop_free,head));
    gasneti_assert(eop->threadidx == thread->threadidx);
    gasneti_assert(eop->type == gasnete_opExplicit);
    gasneti_assert(gasnetc_counter_done(&(eop->req_oust)));
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
      #if 0 /* this can safely be skipped when values are zero */
        buf[i].type = gasnete_opExplicit; 
        gasnetc_atomic_set(&(buf[i].req_oust), 0);
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
        gasneti_assert(eop->type == gasnete_opExplicit);               
        gasneti_assert(gasnetc_counter_done(&(eop->req_oust)));
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
    gasneti_assert(iop->type == gasnete_opImplicit);
    gasneti_assert(iop->threadidx == thread->threadidx);
    gasneti_assert(gasnetc_counter_done(&(iop->get_req_oust)));
    gasneti_assert(gasnetc_counter_done(&(iop->put_req_oust)));
  } else {
    iop = (gasnete_iop_t *)gasneti_malloc(sizeof(gasnete_iop_t));
    gasneti_leak(iop);
    iop->type = gasnete_opImplicit;
    iop->threadidx = thread->threadidx;
    gasnetc_counter_reset(&(iop->get_req_oust));
    gasnetc_counter_reset(&(iop->put_req_oust));
  }
  iop->next = NULL;
  gasnete_iop_check(iop);
  return iop;
}

GASNETI_INLINE(gasnete_eop_free)
void gasnete_eop_free(gasnete_eop_t *eop) {
  gasnete_threaddata_t * const thread = gasnete_threadtable[eop->threadidx];
  gasnete_eopaddr_t addr = eop->addr;
  gasneti_assert(thread == gasnete_mythread());
  gasnete_eop_check(eop);
  gasneti_assert(gasnetc_counter_done(&(eop->req_oust)));
  eop->addr = thread->eop_free;
  thread->eop_free = addr;
}

GASNETI_INLINE(gasnete_iop_free)
void gasnete_iop_free(gasnete_iop_t *iop) {
  gasnete_threaddata_t * const thread = gasnete_threadtable[iop->threadidx];
  gasneti_assert(thread == gasnete_mythread());
  gasnete_iop_check(iop);
  gasneti_assert(gasnetc_counter_done(&(iop->get_req_oust)));
  gasneti_assert(gasnetc_counter_done(&(iop->put_req_oust)));
  iop->next = thread->iop_free;
  thread->iop_free = iop;
}

/* query an eop for completeness */
GASNETI_INLINE(gasnete_eop_test)
int gasnete_eop_test(gasnete_eop_t *eop) {
  gasnete_eop_check(eop);
  return gasnetc_counter_done(&eop->req_oust);
}

/* query an iop for completeness - this means both puts and gets */
GASNETI_INLINE(gasnete_iop_test)
int gasnete_iop_test(gasnete_iop_t *iop) {
  gasnete_iop_check(iop);
  if (gasnetc_counter_done(&(iop->get_req_oust)) &&
      gasnetc_counter_done(&(iop->put_req_oust))) {
    gasnetc_counter_reset(&(iop->get_req_oust));
    gasnetc_counter_reset(&(iop->put_req_oust));
    return 1;
  }
  return 0;
}

/*  query an op for completeness 
 *  free it if complete
 *  returns 0 or 1 */
GASNETI_INLINE(gasnete_op_try_free)
int gasnete_op_try_free(gasnet_handle_t handle) {
  gasnete_op_t *op = (gasnete_op_t *)handle;

  gasneti_assert(op->threadidx == gasnete_mythread()->threadidx);
  if_pt (op->type == gasnete_opExplicit) {
    gasnete_eop_t *eop = (gasnete_eop_t*)op;

    if (gasnete_eop_test(eop)) {
      gasneti_sync_reads();
      gasnete_eop_free(eop);
      return 1;
    }
  } else {
    gasnete_iop_t *iop = (gasnete_iop_t*)op;

    if (gasnete_iop_test(iop)) {
      gasneti_sync_reads();
      gasnete_iop_free(iop);
      return 1;
    }
  }
  return 0;
}

/*  query an op for completeness 
 *  free it and clear the handle if complete
 *  returns 0 or 1 */
GASNETI_INLINE(gasnete_op_try_free_clear)
int gasnete_op_try_free_clear(gasnet_handle_t *handle_p) {
  gasnete_op_t *op = (gasnete_op_t *)(*handle_p);

  gasneti_assert(op->threadidx == gasnete_mythread()->threadidx);
  if_pt (op->type == gasnete_opExplicit) {
    gasnete_eop_t *eop = (gasnete_eop_t*)op;

    if (gasnete_eop_test(eop)) {
      gasneti_sync_reads();
      gasnete_eop_free(eop);
      *handle_p = GASNET_INVALID_HANDLE;
      return 1;
    }
  } else {
    gasnete_iop_t *iop = (gasnete_iop_t*)op;

    if (gasnete_iop_test(iop)) {
      gasneti_sync_reads();
      gasnete_iop_free(iop);
      *handle_p = GASNET_INVALID_HANDLE;
      return 1;
    }
  }
  return 0;
}

/* Reply handler to complete an op - might be replaced w/ IB atomics one day */
GASNETI_INLINE(gasnete_markdone_reph_inner)
void gasnete_markdone_reph_inner(gasnet_token_t token, void *counter) {
  gasnetc_counter_dec((gasnetc_counter_t *)counter);
}
SHORT_HANDLER(gasnete_markdone_reph,1,2,
              (token, UNPACK(a0)    ),
              (token, UNPACK2(a0, a1)));
#define GASNETE_DONE(token, counter)                                               \
  GASNETI_SAFE(                                                                    \
    SHORT_REP(1,2,((token), gasneti_handleridx(gasnete_markdone_reph), PACK(counter))) \
  )

/* ------------------------------------------------------------------------------------ */
/*
  Extended API Common Code
  ========================
  Factored bits of extended API code common to most conduits, overridable when necessary
*/

#define GASNETE_IOP_ISDONE(iop) gasnete_iop_test(iop)

#include "gasnet_extended_common.c"

/* ------------------------------------------------------------------------------------ */
/* GASNET-Internal OP Interface */
gasneti_eop_t *gasneti_eop_create(GASNETE_THREAD_FARG_ALONE) {
  gasnete_eop_t *op = gasnete_eop_new(GASNETE_MYTHREAD);
  gasnetc_counter_inc(&op->req_oust);
  gasnete_eop_check(op);
  return (gasneti_eop_t *)op;
}
gasneti_iop_t *gasneti_iop_register(unsigned int noperations, int isget GASNETE_THREAD_FARG) {
  gasnete_threaddata_t * const mythread = GASNETE_MYTHREAD;
  gasnete_iop_t * const op = mythread->current_iop;
  gasnete_iop_check(op);
  if (isget) gasnetc_counter_inc_by(&op->get_req_oust,noperations);
  else       gasnetc_counter_inc_by(&op->put_req_oust,noperations);
  gasnete_iop_check(op);
  return (gasneti_iop_t *)op;
}
void gasneti_eop_markdone(gasneti_eop_t *eop) {
  gasnete_eop_t *op = (gasnete_eop_t *)eop;
  gasnete_eop_check(op);
  gasnetc_counter_dec(&op->req_oust);
  gasnete_eop_check(op);
}
void gasneti_iop_markdone(gasneti_iop_t *iop, unsigned int noperations, int isget) {
  gasnete_iop_t *op = (gasnete_iop_t *)iop;
  gasnetc_counter_t * const pctr = (isget ? &(op->get_req_oust) : &(op->put_req_oust));
  gasnete_iop_check(op);
  gasnetc_counter_dec_by(pctr,noperations);
  gasnete_iop_check(op);
}

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

  gasneti_assert(gasneti_nodes >= 1 && gasneti_mynode < gasneti_nodes);

  { gasnete_threaddata_t *threaddata = NULL;
    #if GASNETI_MAX_THREADS > 1
      /* register first thread (optimization) */
      threaddata = gasnete_mythread(); 
    #else
      /* register only thread (required) */
      threaddata = gasnete_new_threaddata();
    #endif

    /* cause the first pool of eops to be allocated (optimization) */
    gasnete_eop_free(gasnete_eop_new(threaddata));
  }
   
  /* Initialize barrier resources */
  gasnete_barrier_init();

  /* Initialize VIS subsystem */
  gasnete_vis_init();
}

/* ------------------------------------------------------------------------------------ */
/*
  Non-blocking memory-to-memory transfers (explicit handle)
  ==========================================================
*/
/* ------------------------------------------------------------------------------------ */
GASNETI_INLINE(gasnete_memset_reqh_inner)
void gasnete_memset_reqh_inner(gasnet_token_t token, 
  gasnet_handlerarg_t val, void *nbytes_arg, void *dest, void *counter) {
  size_t nbytes = (uintptr_t)nbytes_arg;
  memset(dest, (int)(uint32_t)val, nbytes);
  gasneti_sync_writes();
  GASNETE_DONE(token, counter);
}
SHORT_HANDLER(gasnete_memset_reqh,4,7,
              (token, a0, UNPACK(a1),      UNPACK(a2),      UNPACK(a3)     ),
              (token, a0, UNPACK2(a1, a2), UNPACK2(a3, a4), UNPACK2(a5, a6)));
/* ------------------------------------------------------------------------------------ */

extern gasnet_handle_t gasnete_get_nb_bulk (void *dest, gasnet_node_t node, void *src, size_t nbytes GASNETE_THREAD_FARG) {
  GASNETI_CHECKPSHM_GET(UNALIGNED,H);
 {
  gasnete_eop_t *eop = gasnete_eop_new(GASNETE_MYTHREAD);

  /* XXX check error returns */
  gasnetc_rdma_get(node, src, dest, nbytes, &eop->req_oust);

  return (gasnet_handle_t)eop;
 }
}

extern gasnet_handle_t gasnete_put_nb      (gasnet_node_t node, void *dest, void *src, size_t nbytes GASNETE_THREAD_FARG) {
  GASNETI_CHECKPSHM_PUT(UNALIGNED,H);
 {
  gasnete_eop_t *eop = gasnete_eop_new(GASNETE_MYTHREAD);
  gasnetc_counter_t mem_oust = GASNETC_COUNTER_INITIALIZER;

  /* XXX check error returns */
  gasnetc_rdma_put(node, src, dest, nbytes, &mem_oust, &eop->req_oust);
  gasnetc_counter_wait(&mem_oust, 0);

  return (gasnet_handle_t)eop;
 }
}

extern gasnet_handle_t gasnete_put_nb_bulk (gasnet_node_t node, void *dest, void *src, size_t nbytes GASNETE_THREAD_FARG) {
  GASNETI_CHECKPSHM_PUT(UNALIGNED,H);
 {
  gasnete_eop_t *eop = gasnete_eop_new(GASNETE_MYTHREAD);

  /* XXX check error returns */
  gasnetc_rdma_put(node, src, dest, nbytes, NULL, &eop->req_oust);

  return (gasnet_handle_t)eop;
 }
}

extern gasnet_handle_t gasnete_memset_nb   (gasnet_node_t node, void *dest, int val, size_t nbytes GASNETE_THREAD_FARG) {
  GASNETI_CHECKPSHM_MEMSET(H);
 {
  gasnete_eop_t *eop = gasnete_eop_new(GASNETE_MYTHREAD);

  gasnetc_counter_inc(&eop->req_oust);
  GASNETI_SAFE(
    SHORT_REQ(4,7,(node, gasneti_handleridx(gasnete_memset_reqh),
                 (gasnet_handlerarg_t)val, PACK(nbytes),
                 PACK(dest), PACK(&eop->req_oust))));

  return (gasnet_handle_t)eop;
 }
}

/* ------------------------------------------------------------------------------------ */
/*
  Synchronization for explicit-handle non-blocking operations:
  ===========================================================
  
  Note that, other than gasnete_wait_syncnb, these routines do not check for INVALID_HANDLE
*/

/* Note that the handle might actually be an IMPLICIT one! */
extern void gasnete_wait_syncnb(gasnet_handle_t op) {
  if_pt (op != GASNET_INVALID_HANDLE) {
    gasneti_assert(op->threadidx == gasnete_mythread()->threadidx);
    if_pt (op->type == gasnete_opExplicit) {
      gasnete_eop_t *eop = (gasnete_eop_t*)op;
      gasnetc_counter_wait(&eop->req_oust, 0);
      gasnete_eop_free(eop);
    } else {
      gasnete_iop_t *iop = (gasnete_iop_t*)op;
      gasnetc_counter_wait(&iop->get_req_oust, 0);
      gasnetc_counter_reset(&iop->get_req_oust);
      gasnetc_counter_wait(&iop->put_req_oust, 0);
      gasnetc_counter_reset(&iop->put_req_oust);
      gasnete_iop_free(iop);
    }
  }
}

extern int  gasnete_try_syncnb(gasnet_handle_t handle) {
#if 0
  /* polling now takes place in callers which needed and NOT in those which don't */
  GASNETI_SAFE(gasneti_AMPoll());
#endif

  return gasnete_op_try_free(handle) ? GASNET_OK : GASNET_ERR_NOT_READY;
}

extern int  gasnete_try_syncnb_some (gasnet_handle_t *phandle, size_t numhandles) {
  int success = 0;
  int empty = 1;

#if 0
  /* polling for syncnb now happens in header file to avoid duplication */
  GASNETI_SAFE(gasneti_AMPoll());
#endif

  gasneti_assert(phandle);

  { int i;
    for (i = 0; i < numhandles; i++) {
      if (phandle[i] != GASNET_INVALID_HANDLE) {
        empty = 0;
	success |= gasnete_op_try_free_clear(&phandle[i]);
      }
    }
  }

  return (success || empty) ? GASNET_OK : GASNET_ERR_NOT_READY;
}

extern int  gasnete_try_syncnb_all (gasnet_handle_t *phandle, size_t numhandles) {
  int success = 1;

#if 0
  /* polling for syncnb now happens in header file to avoid duplication */
  GASNETI_SAFE(gasneti_AMPoll());
#endif

  gasneti_assert(phandle);

  { int i;
    for (i = 0; i < numhandles; i++) {
      if (phandle[i] != GASNET_INVALID_HANDLE) {
        success &= gasnete_op_try_free_clear(&phandle[i]);
      }
    }
  }

  return success ? GASNET_OK : GASNET_ERR_NOT_READY;
}

/* ------------------------------------------------------------------------------------ */
/*
  Non-blocking memory-to-memory transfers (implicit handle)
  ==========================================================
*/

extern void gasnete_get_nbi_bulk (void *dest, gasnet_node_t node, void *src, size_t nbytes GASNETE_THREAD_FARG) {
  GASNETI_CHECKPSHM_GET(UNALIGNED,V);
 {
  gasnete_threaddata_t * const mythread = GASNETE_MYTHREAD;
  gasnete_iop_t *iop = mythread->current_iop;

  /* XXX check error returns */ 
  gasnetc_rdma_get(node, src, dest, nbytes, &iop->get_req_oust);
 }
}

extern void gasnete_put_nbi (gasnet_node_t node, void *dest, void *src, size_t nbytes GASNETE_THREAD_FARG) {
  
  GASNETI_CHECKPSHM_PUT(ALIGNED,V);
 {
  gasnete_threaddata_t * const mythread = GASNETE_MYTHREAD;
  gasnete_iop_t *iop = mythread->current_iop;
  gasnetc_counter_t mem_oust = GASNETC_COUNTER_INITIALIZER;

  /* XXX check error returns */ 
  gasnetc_rdma_put(node, src, dest, nbytes, &mem_oust, &iop->put_req_oust);
  gasnetc_counter_wait(&mem_oust, 0);
 }
}

extern void gasnete_put_nbi_bulk (gasnet_node_t node, void *dest, void *src, size_t nbytes GASNETE_THREAD_FARG) {
  GASNETI_CHECKPSHM_PUT(UNALIGNED,V);
 {
  gasnete_threaddata_t * const mythread = GASNETE_MYTHREAD;
  gasnete_iop_t *iop = mythread->current_iop;

  /* XXX check error returns */ 
  gasnetc_rdma_put(node, src, dest, nbytes, NULL, &iop->put_req_oust);
 }
}

extern void gasnete_memset_nbi   (gasnet_node_t node, void *dest, int val, size_t nbytes GASNETE_THREAD_FARG) {
  GASNETI_CHECKPSHM_MEMSET(V);
 {
  gasnete_threaddata_t * const mythread = GASNETE_MYTHREAD;
  gasnete_iop_t *iop = mythread->current_iop;

  gasnetc_counter_inc(&iop->put_req_oust);
  GASNETI_SAFE(
    SHORT_REQ(4,7,(node, gasneti_handleridx(gasnete_memset_reqh),
                 (gasnet_handlerarg_t)val, PACK(nbytes),
                 PACK(dest), PACK(&iop->put_req_oust))));
 }
}

/* ------------------------------------------------------------------------------------ */
/*
  Synchronization for implicit-handle non-blocking operations:
  ===========================================================
*/

extern int  gasnete_try_syncnbi_gets(GASNETE_THREAD_FARG_ALONE) {
  gasnete_threaddata_t * const mythread = GASNETE_MYTHREAD;
  gasnete_iop_t *iop = mythread->current_iop;
  gasneti_assert(iop->threadidx == mythread->threadidx);
  gasneti_assert(iop->type == gasnete_opImplicit);
  #if GASNET_DEBUG
    if (iop->next != NULL)
      gasneti_fatalerror("VIOLATION: attempted to call gasnete_try_syncnbi_gets() inside an NBI access region");
  #endif

  if (gasnetc_counter_done(&iop->get_req_oust)) {
    gasnetc_counter_reset(&iop->get_req_oust); /* Avoid overflow */
    return GASNET_OK;
  }
  return GASNET_ERR_NOT_READY;
}

extern int  gasnete_try_syncnbi_puts(GASNETE_THREAD_FARG_ALONE) {
  gasnete_threaddata_t * const mythread = GASNETE_MYTHREAD;
  gasnete_iop_t *iop = mythread->current_iop;
  gasneti_assert(iop->threadidx == mythread->threadidx);
  gasneti_assert(iop->type == gasnete_opImplicit);
  #if GASNET_DEBUG
    if (iop->next != NULL)
      gasneti_fatalerror("VIOLATION: attempted to call gasnete_try_syncnbi_puts() inside an NBI access region");
  #endif

  if (gasnetc_counter_done(&iop->put_req_oust)) {
    gasnetc_counter_reset(&iop->put_req_oust); /* Avoid overflow */
    return GASNET_OK;
  }
  return GASNET_ERR_NOT_READY;
}

extern void gasnete_wait_syncnbi_gets(GASNETE_THREAD_FARG_ALONE) {
  gasnete_threaddata_t * const mythread = GASNETE_MYTHREAD;
  gasnete_iop_t *iop = mythread->current_iop;
  gasneti_assert(iop->threadidx == mythread->threadidx);
  gasneti_assert(iop->type == gasnete_opImplicit);
  #if GASNET_DEBUG
    if (iop->next != NULL)
      gasneti_fatalerror("VIOLATION: attempted to call gasnete_wait_syncnbi_gets() inside an NBI access region");
  #endif

  gasnetc_counter_wait(&iop->get_req_oust, 0);
  gasnetc_counter_reset(&iop->get_req_oust);
}

extern void gasnete_wait_syncnbi_puts(GASNETE_THREAD_FARG_ALONE) {
  gasnete_threaddata_t * const mythread = GASNETE_MYTHREAD;
  gasnete_iop_t *iop = mythread->current_iop;
  gasneti_assert(iop->threadidx == mythread->threadidx);
  gasneti_assert(iop->type == gasnete_opImplicit);
  #if GASNET_DEBUG
    if (iop->next != NULL)
      gasneti_fatalerror("VIOLATION: attempted to call gasnete_wait_syncnbi_puts() inside an NBI access region");
  #endif

  gasnetc_counter_wait(&iop->put_req_oust, 0);
  gasnetc_counter_reset(&iop->put_req_oust);
}

/* ------------------------------------------------------------------------------------ */
/*
  Implicit access region synchronization
  ======================================
*/
extern void            gasnete_begin_nbi_accessregion(int allowrecursion GASNETE_THREAD_FARG) {
  gasnete_threaddata_t * const mythread = GASNETE_MYTHREAD;
  gasnete_iop_t *iop = gasnete_iop_new(mythread);
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
  gasnete_iop_t *iop = mythread->current_iop;
  GASNETI_TRACE_EVENT_VAL(S,END_NBI_ACCESSREGION,gasnetc_counter_val(&iop->get_req_oust) + gasnetc_counter_val(&iop->put_req_oust));
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

extern void gasnete_get_bulk (void *dest, gasnet_node_t node, void *src,
			      size_t nbytes GASNETE_THREAD_FARG) {
  GASNETI_CHECKPSHM_GET(UNALIGNED,V);
 {
  gasnetc_counter_t req_oust = GASNETC_COUNTER_INITIALIZER;
  gasnetc_rdma_get(node, src, dest, nbytes, &req_oust);
  gasnetc_counter_wait(&req_oust, 0);
 }
}

extern void gasnete_put_bulk (gasnet_node_t node, void* dest, void *src,
			      size_t nbytes GASNETE_THREAD_FARG) {
  GASNETI_CHECKPSHM_PUT(UNALIGNED,V);
 {
  gasnetc_counter_t req_oust = GASNETC_COUNTER_INITIALIZER;
  gasnetc_rdma_put(node, src, dest, nbytes, NULL, &req_oust);
  gasnetc_counter_wait(&req_oust, 0);
 }
}   

extern void gasnete_memset (gasnet_node_t node, void *dest, int val,
		            size_t nbytes GASNETE_THREAD_FARG) {
  GASNETI_CHECKPSHM_MEMSET(V);
 {
  gasnetc_counter_t req_oust = GASNETC_COUNTER_INITIALIZER;

  gasnetc_counter_inc(&req_oust);
  GASNETI_SAFE(
    SHORT_REQ(4,7,(node, gasneti_handleridx(gasnete_memset_reqh),
                 (gasnet_handlerarg_t)val, PACK(nbytes),
                 PACK(dest), PACK(&req_oust))));

  gasnetc_counter_wait(&req_oust, 0);
 }
}

/* ------------------------------------------------------------------------------------ */
/*
  Barriers:
  =========
  "ibd" = IB Dissemination
*/

/* Safe only for 64-bit PCI bus, which we can assume only for 64-bit CPU */
#if PLATFORM_ARCH_64
  #define GASNETE_BARRIER_DEFAULT "IBDISSEM"
#endif

/* Forward decls for init function(s): */
static void gasnete_ibdbarrier_init(gasnete_coll_team_t team);

#define GASNETE_BARRIER_READENV() do {                                  \
  if (GASNETE_ISBARRIER("IBDISSEM"))                                    \
    gasnete_coll_default_barrier_type = GASNETE_COLL_BARRIER_IBDISSEM;  \
} while (0)

#define GASNETE_BARRIER_INIT(TEAM, BARRIER_TYPE) do {                                  \
    if ((BARRIER_TYPE) == GASNETE_COLL_BARRIER_IBDISSEM && (TEAM)==GASNET_TEAM_ALL) { \
      gasnete_ibdbarrier_init(TEAM);                              \
    }                                                            \
  } while (0)

/* Can use the auxseg allocation from the generic implementation: */
static int gasnete_conduit_rdmabarrier(const char *barrier, gasneti_auxseg_request_t *result);
#define GASNETE_CONDUIT_RDMABARRIER gasnete_conduit_rdmabarrier

/* use reference implementation of barrier */
#define GASNETI_GASNET_EXTENDED_REFBARRIER_C 1
#include "gasnet_extended_refbarrier.c"
#undef GASNETI_GASNET_EXTENDED_REFBARRIER_C

static int gasnete_conduit_rdmabarrier(const char *barrier, gasneti_auxseg_request_t *result) {
  if (0 == strcmp(barrier, "IBDISSEM")) {
    /* TODO: could keep the full space and allocate some to additional teams */
    size_t request;
#if GASNETI_PSHM_BARRIER_HIER
    const int size = gasneti_nodemap_global_count;
#else
    const int size = gasneti_nodes;
#endif
    int steps, i;

    for (steps=0, i=1; i<size; ++steps, i*=2) /* empty */ ;

    request = 2 * steps * GASNETE_RDMABARRIER_INBOX_SZ;
    gasneti_assert_always(GASNETE_RDMABARRIER_INBOX_SZ >= sizeof(uint64_t));
    gasneti_assert_always(request <= result->optimalsz);
    result->minsz = request;
    result->optimalsz = request;
    return (steps != 0);
  }

  return 0;
}

/* ------------------------------------------------------------------------------------ */
/* IB-specific RDMA-based Dissemination implementation of barrier
 * This is an adaptation of the "rmd" barrier in exteneded-ref.
 * Key differences:
 *  + no complications due to thread-specific handles
 *  + simple (normally inline) 64-bit put
 *  + no handle completion latency
 */

#if !GASNETI_THREADS
  #define GASNETE_IBDBARRIER_LOCK(_var)		/* empty */
  #define gasnete_ibdbarrier_lock_init(_var)	((void)0)
  #define gasnete_ibdbarrier_trylock(_var)	(0/*success*/)
  #define gasnete_ibdbarrier_unlock(_var)	((void)0)
#elif GASNETI_HAVE_SPINLOCK
  #define GASNETE_IBDBARRIER_LOCK(_var)		gasneti_atomic_t _var;
  #define gasnete_ibdbarrier_lock_init(_var)	gasneti_spinlock_init(_var)
  #define gasnete_ibdbarrier_trylock(_var)	gasneti_spinlock_trylock(_var)
  #define gasnete_ibdbarrier_unlock(_var)	gasneti_spinlock_unlock(_var)
#else
  #define GASNETE_IBDBARRIER_LOCK(_var)		gasneti_mutex_t _var;
  #define gasnete_ibdbarrier_lock_init(_var)	gasneti_mutex_init(_var)
  #define gasnete_ibdbarrier_trylock(_var)	gasneti_mutex_trylock(_var)
  #define gasnete_ibdbarrier_unlock(_var)	gasneti_mutex_unlock(_var)
#endif

typedef struct {
  GASNETE_IBDBARRIER_LOCK(barrier_lock) /* no semicolon */
  struct {
    gasnet_node_t node;
    uint64_t      *addr;
  } *barrier_peers;           /*  precomputed list of peers to communicate with */
#if GASNETI_PSHM_BARRIER_HIER
  gasnete_pshmbarrier_data_t *barrier_pshm; /* non-NULL if using hierarchical code */
  int barrier_passive;        /*  2 if some other node makes progress for me, 0 otherwise */
#endif
  int barrier_size;           /*  ceil(lg(nodes)) */
  int barrier_goal;           /*  (ceil(lg(nodes)) << 1) */
  int volatile barrier_slot;  /*  (step << 1) | phase */
  int volatile barrier_value; /*  barrier value (evolves from local value) */
  int volatile barrier_flags; /*  barrier flags (evolves from local value) */
  volatile uint64_t *barrier_inbox; /*  in-segment memory to recv notifications */
} gasnete_coll_ibdbarrier_t;

/* Unlike the extended-ref version we CAN assume that a 64-bit write
 * will be atomic, and so can use a "present" bit in flags.
 */
#define GASNETE_IBDBARRIER_PRESENT 0x80000000
#define GASNETE_IBDBARRIER_VALUE(_u64) GASNETI_HIWORD(_u64)
#define GASNETE_IBDBARRIER_FLAGS(_u64) GASNETI_LOWORD(_u64)
#define GASNETE_IBDBARRIER_BUILD(_value,_flags) \
                GASNETI_MAKEWORD((_value),GASNETE_IBDBARRIER_PRESENT|(_flags))
  
/* Pad struct to a specfic size and interleave */
#define GASNETE_IBDBARRIER_INBOX_WORDS (GASNETE_RDMABARRIER_INBOX_SZ/sizeof(uint64_t))
#define GASNETE_IBDBARRIER_INBOX(_bd,_slot)     \
            (((_bd)->barrier_inbox) \
                       + (unsigned)(_slot) * GASNETE_IBDBARRIER_INBOX_WORDS)
#define GASNETE_IBDBARRIER_INBOX_REMOTE(_bd,_step,_slot)  \
            (((_bd)->barrier_peers[(unsigned)(_step)].addr \
                       + (unsigned)(_slot) * GASNETE_IBDBARRIER_INBOX_WORDS))
#define GASNETE_IBDBARRIER_INBOX_NEXT(_addr)    \
            ((_addr) + 2U * GASNETE_IBDBARRIER_INBOX_WORDS)

GASNETI_INLINE(gasnete_ibdbarrier_send)
void gasnete_ibdbarrier_send(gasnete_coll_ibdbarrier_t *barrier_data,
                             int numsteps, unsigned int slot,
                             gasnet_handlerarg_t value, gasnet_handlerarg_t flags) {
  unsigned int step = slot >> 1;
  int i;

  /* Use the upper half (padding) an "other phase" inbox as an in-segment temporary.
   * This has sufficient lifetime for bulk and sufficient alignment for non-bulk.
   * Use of opposite phase prevents cacheline contention with arrivals.
   */
  volatile uint64_t *payload = GASNETE_IBDBARRIER_INBOX(barrier_data, (slot^1))
                                    + (GASNETE_IBDBARRIER_INBOX_WORDS/2);
  *payload = GASNETE_IBDBARRIER_BUILD(value, flags);

  /* TODO:
   *   Reduce latency by pre-computing sr_desc and rkey_index at init time
   *   AND/OR providing a specialized alternative to gasnetc_rdma_put()
   */

  for (i = 0; i < numsteps; ++i, slot += 2, step += 1) {
    const gasnet_node_t node = barrier_data->barrier_peers[step].node;
    uint64_t * const dst = GASNETE_IBDBARRIER_INBOX_REMOTE(barrier_data, step, slot);
    (void) gasnetc_rdma_put(node, (void*)payload, dst, sizeof(*payload), NULL, NULL);
  }
}

void gasnete_ibdbarrier_kick(gasnete_coll_team_t team) {
  gasnete_coll_ibdbarrier_t *barrier_data = team->barrier_data;
  volatile uint64_t *inbox;
  uint64_t result;
  int numsteps = 0;
  int slot, cursor;
  int flags, value;

  /* early unlocked read: */
  slot = barrier_data->barrier_slot;

  if (slot >= barrier_data->barrier_goal)
    return; /* nothing to do */

  gasneti_assert(team->total_ranks > 1); /* singleton should have matched (slot >= goal), above */

#if GASNETI_THREADS
  if (gasnete_ibdbarrier_trylock(&barrier_data->barrier_lock))
    return; /* another thread is currently in kick */

  /* reread w/ lock held: */
  slot = barrier_data->barrier_slot;

  if_pf (slot < 2) {/* need to pick up value/flags from notify */
    gasneti_sync_reads(); /* value/flags were written by the non-locked notify */
  }
#endif

  value = barrier_data->barrier_value;
  flags = barrier_data->barrier_flags;

  /* process all consecutive steps which have arrived since we last ran */
  inbox = GASNETE_IBDBARRIER_INBOX(barrier_data, slot);
  for (cursor = slot; cursor < barrier_data->barrier_goal && (0 != (result = *inbox)); cursor+=2) {
    const int step_value = GASNETE_IBDBARRIER_VALUE(result);
    const int step_flags = GASNETE_IBDBARRIER_FLAGS(result);
    *inbox = 0;

    if ((flags | step_flags) & GASNET_BARRIERFLAG_MISMATCH) {
      flags = GASNET_BARRIERFLAG_MISMATCH; 
    } else if (flags & GASNET_BARRIERFLAG_ANONYMOUS) {
      flags = step_flags; 
      value = step_value; 
    } else if (!(step_flags & GASNET_BARRIERFLAG_ANONYMOUS) && (step_value != value)) {
      flags = GASNET_BARRIERFLAG_MISMATCH; 
    }

    ++numsteps;
    inbox = GASNETE_IBDBARRIER_INBOX_NEXT(inbox);
  }

  if (numsteps) { /* completed one or more steps */
    barrier_data->barrier_flags = flags; 
    barrier_data->barrier_value = value; 

    if (cursor >= barrier_data->barrier_goal) { /* We got the last recv - barrier locally complete */
      gasnete_barrier_pf_disable(team);
      gasneti_sync_writes(); /* flush state before the write to barrier_slot below */
      numsteps -= 1; /* no send at last step */
    } 
    /* notify all threads of the step increase - 
       this may allow other local threads to proceed on the barrier and even indicate
       barrier completion while we overlap outgoing notifications to other nodes
    */
    barrier_data->barrier_slot = cursor;
  } 

  gasnete_ibdbarrier_unlock(&barrier_data->barrier_lock);

  if (numsteps) { /* need to issue one or more Puts */
    gasnete_ibdbarrier_send(barrier_data, numsteps, slot+2, value, flags);
  }
}

static void gasnete_ibdbarrier_notify(gasnete_coll_team_t team, int id, int flags) {
  gasnete_coll_ibdbarrier_t *barrier_data = team->barrier_data;
  int do_send = 1;
  int slot;

  if_pf(team->barrier_splitstate == INSIDE_BARRIER) 
    gasneti_fatalerror("gasnet_barrier_notify() called twice in a row");

#if GASNETI_PSHM_BARRIER_HIER
  if (barrier_data->barrier_pshm) {
    PSHM_BDATA_DECL(pshm_bdata, barrier_data->barrier_pshm);
    (void)gasnete_pshmbarrier_notify_inner(pshm_bdata, id, flags);
    do_send = !barrier_data->barrier_passive;
    id = pshm_bdata->shared->value;
    flags = pshm_bdata->shared->flags;
  }
#endif

  barrier_data->barrier_value = id;
  barrier_data->barrier_flags = flags;

  slot = ((barrier_data->barrier_slot & 1) ^ 1); /* enter new phase */
  gasneti_sync_writes();
  barrier_data->barrier_slot = slot;

  if (do_send) {
    gasnete_ibdbarrier_send(barrier_data, 1, slot, id, flags);
    gasnete_barrier_pf_enable(team);
  }

  /*  update state */
  team->barrier_splitstate = INSIDE_BARRIER;
  gasneti_sync_writes(); /* ensure all state changes committed before return */
}

/* Notify specialized to one (super)node case (reduced branches in BOTH variants) */
static void gasnete_ibdbarrier_notify_singleton(gasnete_coll_team_t team, int id, int flags) {
  gasnete_coll_ibdbarrier_t *barrier_data = team->barrier_data;

  if_pf(team->barrier_splitstate == INSIDE_BARRIER) 
    gasneti_fatalerror("gasnet_barrier_notify() called twice in a row");

#if GASNETI_PSHM_BARRIER_HIER
  if (barrier_data->barrier_pshm) {
    PSHM_BDATA_DECL(pshm_bdata, barrier_data->barrier_pshm);
    (void)gasnete_pshmbarrier_notify_inner(pshm_bdata, id, flags);
    id = pshm_bdata->shared->value;
    flags = pshm_bdata->shared->flags;
  }
#endif

  barrier_data->barrier_value = id;
  barrier_data->barrier_flags = flags;

  /*  update state */
  team->barrier_splitstate = INSIDE_BARRIER;
  gasneti_sync_writes(); /* ensure all state changes committed before return */
}

static int gasnete_ibdbarrier_wait(gasnete_coll_team_t team, int id, int flags) {
  gasnete_coll_ibdbarrier_t *barrier_data = team->barrier_data;
#if GASNETI_PSHM_BARRIER_HIER
  const PSHM_BDATA_DECL(pshm_bdata, barrier_data->barrier_pshm);
#endif
  int retval = GASNET_OK;

  gasneti_sync_reads(); /* ensure we read correct barrier_splitstate */
  if_pf(team->barrier_splitstate == OUTSIDE_BARRIER) 
    gasneti_fatalerror("gasnet_barrier_wait() called without a matching notify");

#if GASNETI_PSHM_BARRIER_HIER
  if (pshm_bdata) {
    const int passive_shift = barrier_data->barrier_passive;
    retval = gasnete_pshmbarrier_wait_inner(pshm_bdata, id, flags, passive_shift);
    if (passive_shift) {
      /* Once the active peer signals done, we can return */
      barrier_data->barrier_value = pshm_bdata->shared->value;
      barrier_data->barrier_flags = pshm_bdata->shared->flags;
      team->barrier_splitstate = OUTSIDE_BARRIER;
      gasneti_sync_writes(); /* ensure all state changes committed before return */
      return retval;
    }
  }
#endif

  if (barrier_data->barrier_slot >= barrier_data->barrier_goal) {
    /* completed asynchronously before wait (via progressfns or try) */
    GASNETI_TRACE_EVENT_TIME(B,BARRIER_ASYNC_COMPLETION,GASNETI_TICKS_NOW_IFENABLED(B)-gasnete_barrier_notifytime);
  } else {
    /* kick once, and if still necessary, wait for a response */
    gasnete_ibdbarrier_kick(team);
    /* cannot BLOCKUNTIL since progess may occur on non-AM events */
    while (barrier_data->barrier_slot < barrier_data->barrier_goal) {
      GASNETI_WAITHOOK();
      GASNETI_SAFE(gasneti_AMPoll());
      gasnete_ibdbarrier_kick(team);
    }
  }
  gasneti_sync_reads(); /* ensure correct barrier_flags will be read */

  /* determine return value */
  if_pf (barrier_data->barrier_flags & GASNET_BARRIERFLAG_MISMATCH) {
    retval = GASNET_ERR_BARRIER_MISMATCH;
  } else
  if_pf(/* try/wait value must match consensus value, if both are present */
        !((flags|barrier_data->barrier_flags) & GASNET_BARRIERFLAG_ANONYMOUS) &&
	 ((gasnet_handlerarg_t)id != barrier_data->barrier_value)) {
    retval = GASNET_ERR_BARRIER_MISMATCH;
  }

  /*  update state */
  team->barrier_splitstate = OUTSIDE_BARRIER;
#if GASNETI_PSHM_BARRIER_HIER
  if (pshm_bdata) {
    /* Signal any passive peers w/ the final result */
    pshm_bdata->shared->value = barrier_data->barrier_value;
    pshm_bdata->shared->flags = barrier_data->barrier_flags;
    PSHM_BSTATE_SIGNAL(pshm_bdata, retval, pshm_bdata->private.two_to_phase << 2); /* includes a WMB */
    gasneti_assert(!barrier_data->barrier_passive);
  } else
#endif
  gasneti_sync_writes(); /* ensure all state changes committed before return */

  return retval;
}

static int gasnete_ibdbarrier_try(gasnete_coll_team_t team, int id, int flags) {
  gasnete_coll_ibdbarrier_t *barrier_data = team->barrier_data;
  gasneti_sync_reads(); /* ensure we read correct barrier_splitstate */

  if_pf(team->barrier_splitstate == OUTSIDE_BARRIER) 
    gasneti_fatalerror("gasnet_barrier_try() called without a matching notify");

  GASNETI_SAFE(gasneti_AMPoll());

#if GASNETI_PSHM_BARRIER_HIER
  if (barrier_data->barrier_pshm) {
    const int passive_shift = barrier_data->barrier_passive;
    if (!gasnete_pshmbarrier_try_inner(barrier_data->barrier_pshm, passive_shift))
      return GASNET_ERR_NOT_READY;
    if (passive_shift)
      return gasnete_ibdbarrier_wait(team, id, flags);
  }
  if (!barrier_data->barrier_passive)
#endif
    gasnete_ibdbarrier_kick(team);

  if (barrier_data->barrier_slot >= barrier_data->barrier_goal) return gasnete_ibdbarrier_wait(team, id, flags);
  else return GASNET_ERR_NOT_READY;
}

static int gasnete_ibdbarrier_result(gasnete_coll_team_t team, int *id) {
  gasneti_sync_reads();
  if_pf (team->barrier_splitstate != OUTSIDE_BARRIER) {
    gasneti_fatalerror("gasnet_barrier_result() called between notify and wait/try");
  }

  { const gasnete_coll_ibdbarrier_t * const barrier_data = team->barrier_data;
    *id = barrier_data->barrier_value;
    return (GASNET_BARRIERFLAG_ANONYMOUS & barrier_data->barrier_flags);
  }
}

void gasnete_ibdbarrier_kick_team_all(void) {
  gasnete_ibdbarrier_kick(GASNET_TEAM_ALL);
}

static void gasnete_ibdbarrier_init(gasnete_coll_team_t team) {
  gasnete_coll_ibdbarrier_t *barrier_data;
  int steps;
  int total_ranks = team->total_ranks;
  int myrank = team->myrank;
#if GASNETI_PSHM_BARRIER_HIER
  gasnet_node_t *supernode_reps = NULL;
  PSHM_BDATA_DECL(pshm_bdata, gasnete_pshmbarrier_init_hier(team, &total_ranks, &myrank, &supernode_reps));
#endif
  int64_t j;

  barrier_data = gasneti_malloc_aligned(GASNETI_CACHE_LINE_BYTES, sizeof(gasnete_coll_ibdbarrier_t));
  gasneti_leak_aligned(barrier_data);
  memset(barrier_data, 0, sizeof(gasnete_coll_ibdbarrier_t));
  team->barrier_data = barrier_data;

#if GASNETI_PSHM_BARRIER_HIER
  if (pshm_bdata) {
    barrier_data->barrier_passive = (pshm_bdata->private.rank != 0) ? 2 : 0; /* precompute shift */
    barrier_data->barrier_pshm = pshm_bdata;
  }
#endif

  gasneti_assert(team == GASNET_TEAM_ALL); /* TODO: deal w/ in-segment allocation */

  gasnete_ibdbarrier_lock_init(&barrier_data->barrier_lock);
  team->barrier_splitstate = OUTSIDE_BARRIER;

  /* determine barrier size (number of steps) */
  for (steps=0, j=1; j < total_ranks; ++steps, j*=2) ;

  barrier_data->barrier_size = steps;
  barrier_data->barrier_goal = steps << 1;

  if (steps) {
#if GASNETI_PSHM_BARRIER_HIER
    gasnet_node_t *nodes = supernode_reps ? supernode_reps : gasneti_pshm_firsts;
#endif
    int step;

    gasneti_assert(gasnete_rdmabarrier_auxseg);
    barrier_data->barrier_inbox = gasnete_rdmabarrier_auxseg[gasneti_mynode].addr;
    gasneti_assert(GASNETE_IBDBARRIER_INBOX_WORDS > 1);

    barrier_data->barrier_peers = gasneti_malloc(steps * sizeof(* barrier_data->barrier_peers));
    gasneti_leak(barrier_data->barrier_peers);
  
    for (step = 0; step < steps; ++step) {
      gasnet_node_t distance, tmp, peer, node;

      distance = (1 << step);
      tmp = total_ranks - myrank;
      peer = (distance < tmp) ? (distance + myrank) : (distance - tmp); /* mod N w/o overflow */
      gasneti_assert(peer < total_ranks);

#if GASNETI_PSHM_BARRIER_HIER
      if (pshm_bdata) {
        node = nodes[peer];
      } else
#endif
      {
        node = GASNETE_COLL_REL2ACT(team, peer);
      }

      barrier_data->barrier_peers[step].node = node;
      barrier_data->barrier_peers[step].addr = gasnete_rdmabarrier_auxseg[node].addr;
    }
  } else {
    barrier_data->barrier_slot = barrier_data->barrier_goal;
  }

  gasneti_free(gasnete_rdmabarrier_auxseg);

#if GASNETI_PSHM_BARRIER_HIER
  gasneti_free(supernode_reps);

  if (pshm_bdata && (pshm_bdata->shared->size == 1)) {
    /* With singleton proc on local supernode we can short-cut the PSHM code.
     * This does not require alteration of the barrier_peers[] contructed above
     */
    gasnete_pshmbarrier_fini_inner(pshm_bdata);
    barrier_data->barrier_pshm = NULL;
  }
#endif

  team->barrier_notify = steps ? &gasnete_ibdbarrier_notify : &gasnete_ibdbarrier_notify_singleton;
  team->barrier_wait =   &gasnete_ibdbarrier_wait;
  team->barrier_try =    &gasnete_ibdbarrier_try;
  team->barrier_result = &gasnete_ibdbarrier_result;
  team->barrier_pf =     (team == GASNET_TEAM_ALL) ? &gasnete_ibdbarrier_kick_team_all : NULL;
}

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
  gasneti_handler_tableentry_with_bits(gasnete_markdone_reph),
  gasneti_handler_tableentry_with_bits(gasnete_memset_reqh),

  { 0, NULL }
};

extern gasnet_handlerentry_t const *gasnete_get_handlertable(void) {
  return gasnete_handlers;
}
/* ------------------------------------------------------------------------------------ */


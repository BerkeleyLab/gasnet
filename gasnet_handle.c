/*   $Source: bitbucket.org:berkeleylab/gasnet.git/gasnet_handle.c $
 * Description: GASNet handle/eop/iop common code
 * Copyright 2002, Dan Bonachea <bonachea@cs.berkeley.edu>
 * Terms of use are as specified in license.txt
 */


#include <gasnet_internal.h>
#include <gasnet_extended_internal.h>
// TODO-EX: should *not* require extended_internal (should be handle_internal.h?)

extern void _gasnete_iop_check(gasnete_iop_t *iop) { gasnete_iop_check(iop); }

/* ------------------------------------------------------------------------------------ */
/*
  Op management
  =============
*/

#if !GASNETI_DISABLE_REFERENCE_EOP

/*  allocate more eops */
GASNETI_NEVER_INLINE(gasnete_eop_alloc,
static void gasnete_eop_alloc(gasnete_threaddata_t * const thread)) {
    int bufidx = thread->eop_num_bufs;
    gasnete_eop_t *buf;
    int i;
    gasnete_threadidx_t threadidx = thread->threadidx;
    if (bufidx == 256) gasneti_fatalerror("GASNet Extended API: Ran out of explicit handles (limit=65535)");
    thread->eop_num_bufs++;
    buf = (gasnete_eop_t *)gasneti_calloc(256,sizeof(gasnete_eop_t));
    gasneti_leak(buf);
    for (i=0; i < 256; i++) {
      uint8_t eopidx;
      #if GASNETE_SCATTER_EOPS_ACROSS_CACHELINES
        int k = i+32; // TODO-EX: revisit the value '32' or remove this if eop fills cache line
        eopidx = k > 255 ? k - 255 : k;
      #else
        eopidx = i+1;
      #endif
      EOP_NEXT(buf + i) = buf + eopidx;
      #if 0 /* this can safely be skipped when the values are zero */
       #if GASNETE_EOP_COUNTED
        buf[i].initiated_cnt = 0;
        buf[i].initiated_alc = 0;
       #endif
      #endif
      #if GASNETE_EOP_COUNTED
        gasnete_op_atomic_set(&buf[i].completed_cnt, 0 , 0);
        gasnete_op_atomic_set(&buf[i].completed_alc, 0 , 0);
      #endif
      #ifdef GASNETE_EOP_ALLOC_EXTRA
        // Hook for conduit-specific initializations and assertions
        GASNETE_EOP_ALLOC_EXTRA(&buf[i]);
      #endif
    }
     /*  add a list terminator */
    EOP_NEXT(buf + 255) = NULL;
    thread->eop_bufs[bufidx] = buf;
    thread->eop_free = buf;

    #if GASNET_DEBUG
    { /* verify new free list got built correctly */
      int i;
      int seen[256];
      gasnete_eop_t *eop;

      gasneti_memcheck(thread->eop_bufs[bufidx]);
      memset(seen, 0, 256*sizeof(int));
      for (i=0, eop = buf; i<(bufidx==255?255:256); i++) {
        gasneti_assert(!seen[eop-buf]);/* see if we hit a cycle */
        seen[eop-buf] = 1;
        eop = EOP_NEXT(eop);
      }                                                       
      gasneti_assert(eop == NULL);
    }
    #endif
}

/*  allocate a new iop */
GASNETI_NEVER_INLINE(gasnete_iop_alloc,
static gasnete_iop_t *gasnete_iop_alloc(gasnete_threaddata_t * const thread)) {
    gasnete_iop_t *iop = (gasnete_iop_t *)gasneti_malloc(sizeof(gasnete_iop_t));
    gasneti_leak(iop);
    #if GASNET_DEBUG
      memset(iop, 0, sizeof(gasnete_iop_t)); /* set pad to known value */
    #endif
    iop->flags = OPTYPE_IMPLICIT;
    iop->threadidx = thread->threadidx;
    iop->initiated_alc_cnt = 0;
    iop->initiated_get_cnt = 0;
    iop->initiated_put_cnt = 0;
    gasnete_op_atomic_set(&(iop->completed_alc_cnt), 0, 0);
    gasnete_op_atomic_set(&(iop->completed_get_cnt), 0, 0);
    gasnete_op_atomic_set(&(iop->completed_put_cnt), 0, 0);
  #ifdef GASNETE_IOP_ALLOC_EXTRA
    // Hook for conduit-specific initializations and assertions
    GASNETE_IOP_ALLOC_EXTRA(iop);
  #endif
    return iop;
}

/*  get a new op */
static
gasnete_eop_t *_gasnete_eop_new(gasnete_threaddata_t * const thread) {
  gasnete_eop_t *eop = thread->eop_free;
  if_pf (!eop) {
    gasnete_eop_alloc(thread);
    eop = thread->eop_free;
  }
  {
    thread->eop_free = EOP_NEXT(eop);
    eop->flags = (OPTYPE_EXPLICIT | EOPSTATE_FREE);
    eop->flags2 = 0;
    if (offsetof(gasnete_eop_t, threadidx) <= sizeof(void*))
      eop->threadidx = thread->threadidx;
    gasneti_assert(OPTYPE(eop) == OPTYPE_EXPLICIT);
    gasneti_assert(EOPSTATE(eop) == EOPSTATE_FREE);
    gasneti_assert(LCSTATE(eop) ==  LCSTATE_NONE);
  #if GASNET_DEBUG
    // TODO-EX: this is used to assert "not-on-freelist" and should be encode differently
    SET_EOPSTATE(eop, EOPSTATE_INFLIGHT);
  #endif
  #if GASNETE_EOP_COUNTED
    gasneti_assert(GASNETE_EOP_DONE(eop));
    gasneti_assert(GASNETE_EOP_LC(eop));
  #endif
  #ifdef _GASNETE_EOP_NEW_EXTRA
    // Hook for conduit-specific initializations and assertions
    _GASNETE_EOP_NEW_EXTRA(eop);
  #endif
    return eop;
  }
}

/*  get a new op AND mark it in flight */
GASNETI_INLINE(gasnete_eop_new)
gasnete_eop_t *gasnete_eop_new(gasnete_threaddata_t * const thread) {
  gasnete_eop_t *eop = _gasnete_eop_new(thread);
#if GASNETE_EOP_BOOLEAN
  SET_EOPSTATE(eop, EOPSTATE_INFLIGHT);
#endif
#if GASNETE_EOP_COUNTED
  eop->initiated_cnt++;
#endif
#ifdef GASNETE_EOP_NEW_EXTRA
  // Hook for conduit-specific initializations and assertions
  GASNETE_EOP_NEW_EXTRA(eop);
#endif
  return eop;
}

/*  get a new iop */
static
gasnete_iop_t *gasnete_iop_new(gasnete_threaddata_t * const thread) {
  gasnete_iop_t *iop = thread->iop_free;
  if_pt (iop) {
    thread->iop_free = iop->next;
    gasneti_memcheck(iop);
    gasneti_assert(OPTYPE(iop) == OPTYPE_IMPLICIT);
    gasneti_assert(iop->threadidx == thread->threadidx);
    /* If using trace or stats, want meaningful counts when tracing NBI access regions */
    #if GASNETI_STATS_OR_TRACE
      iop->initiated_alc_cnt = 0;
      iop->initiated_get_cnt = 0;
      iop->initiated_put_cnt = 0;
      gasnete_op_atomic_set(&(iop->completed_alc_cnt), 0, 0);
      gasnete_op_atomic_set(&(iop->completed_get_cnt), 0, 0);
      gasnete_op_atomic_set(&(iop->completed_put_cnt), 0, 0);
    #endif
    SET_LCSTATE(iop, LCSTATE_NONE);
  } else {
    iop = gasnete_iop_alloc(thread);
  }
  iop->next = NULL;
#ifdef GASNETE_IOP_NEW_EXTRA
  // Hook for conduit-specific initializations and assertions
  GASNETE_IOP_NEW_EXTRA(iop);
#endif
  gasnete_iop_check(iop);
  return iop;
}

/*  query an eop for completeness */
static
int gasnete_eop_isdone(gasnete_eop_t *eop) {
  gasneti_assert(eop->threadidx == gasnete_mythread()->threadidx);
  gasnete_eop_check(eop);
  return GASNETE_EOP_DONE(eop);
}

/*  query an iop (returned from end_nbi_accessregion) for completeness -
 *  this always means both puts and gets, and may mean LC too */
static
int gasnete_iop_isdone(gasnete_iop_t *iop) {
  int result;
  gasneti_assert(iop->threadidx == gasnete_mythread()->threadidx);
  gasnete_iop_check(iop);
  #if GASNET_DEBUG
    if (LCSTATE(iop) == LCSTATE_LIVE) // TODO-EX: better wording?
      gasneti_fatalerror("VIOLATION: attempted to call syncnb on an NBI access region handle before locally-complete");
  #endif
  result = (GASNETE_IOP_CNTDONE(iop,get) && GASNETE_IOP_CNTDONE(iop,put) &&
          ((LCSTATE(iop) == LCSTATE_NONE) || GASNETE_IOP_CNTDONE(iop,alc)));
  #if GASNET_DEBUG
    if (result) SET_LCSTATE(iop, LCSTATE_NONE);
  #endif
  return result;
}

/*  mark an op done - isget ignored for explicit ops */
extern
void gasnete_op_markdone(gasnete_op_t *op, int isget) {
  if (OPTYPE(op) == OPTYPE_EXPLICIT) {
    gasnete_eop_t *eop = (gasnete_eop_t *)op;
    gasnete_eop_check(eop);
    GASNETE_EOP_MARKDONE(eop);
  } else {
    gasnete_iop_t *iop = (gasnete_iop_t *)op;
    gasnete_iop_check(iop);
    if (isget) gasnete_op_atomic_increment(&(iop->completed_get_cnt), 0);
    else gasnete_op_atomic_increment(&(iop->completed_put_cnt), 0);
  }
}

/*  free an eop */
static
void gasnete_eop_free(gasnete_eop_t *eop) {
  gasnete_threaddata_t * const thread = gasnete_threadtable[eop->threadidx];
  gasneti_assert(thread == gasnete_mythread());
  gasnete_eop_check(eop);
  gasneti_assert(GASNETE_EOP_DONE(eop));
  gasneti_assert(GASNETE_EOP_LC(eop));
#ifdef GASNETE_EOP_FREE_EXTRA
  // Hook for conduit-specific cleanups and assertions
  GASNETE_EOP_FREE_EXTRA(eop);
#endif
#if GASNET_DEBUG
  SET_EOPSTATE(eop, EOPSTATE_FREE);
#endif
  EOP_NEXT(eop) = thread->eop_free;
  thread->eop_free = eop;
}

/*  free an iop */
static
void gasnete_iop_free(gasnete_iop_t *iop) {
  gasnete_threaddata_t * const thread = gasnete_threadtable[iop->threadidx];
  gasneti_assert(thread == gasnete_mythread());
  gasnete_iop_check(iop);
  gasneti_assert(GASNETE_IOP_CNTDONE(iop,alc));
  gasneti_assert(GASNETE_IOP_CNTDONE(iop,get));
  gasneti_assert(GASNETE_IOP_CNTDONE(iop,put));
  gasneti_assert(LCSTATE(iop) == LCSTATE_NONE);
  gasneti_assert(iop->next == NULL);
#ifdef GASNETE_IOP_FREE_EXTRA
  // Hook for conduit-specific cleanups and assertions
  GASNETE_IOP_FREE_EXTRA(iop);
#endif
  iop->next = thread->iop_free;
  thread->iop_free = iop;
}

#endif // GASNETI_DISABLE_REFERENCE_EOP

/* ------------------------------------------------------------------------------------ */
/* GASNET-Internal OP Interface */

#if !GASNETI_DISABLE_EOP_INTERFACE

gasneti_eop_t *gasneti_eop_create(GASNETI_THREAD_FARG_ALONE) {
  gasnete_eop_t *op = gasnete_eop_new(GASNETI_MYTHREAD);
  return (gasneti_eop_t *)op;
}
gasneti_iop_t *gasneti_iop_register(unsigned int noperations, int isget GASNETI_THREAD_FARG) {
  gasnete_threaddata_t * const mythread = GASNETI_MYTHREAD;
  gasnete_iop_t * const op = mythread->current_iop;
  gasnete_iop_check(op);
  if (isget) op->initiated_get_cnt += noperations;
  else       op->initiated_put_cnt += noperations;
  gasnete_iop_check(op);
  return (gasneti_iop_t *)op;
}
void gasneti_eop_markdone(gasneti_eop_t *eop) {
  gasnete_eop_t *op = (gasnete_eop_t *)eop;
  gasnete_eop_check(op);
  GASNETE_EOP_MARKDONE(op);
}
void gasneti_iop_markdone(gasneti_iop_t *iop, unsigned int noperations, int isget) {
  gasnete_iop_t *op = (gasnete_iop_t *)iop;
  gasnete_op_atomic_t * const pctr = (isget ? &(op->completed_get_cnt) : &(op->completed_put_cnt));
  gasnete_iop_check(op);
  if (gasneti_constant_p(noperations) && (noperations == 1))
      gasnete_op_atomic_increment(pctr, 0);
  else {
    #if defined(GASNETI_HAVE_WEAKATOMIC_ADD_SUB)
      gasnete_op_atomic_add(pctr, noperations, 0);
    #else /* yuk */
      while (noperations) {
        gasnete_op_atomic_increment(pctr, 0);
        noperations--;
      }
    #endif
  }
  gasnete_iop_check(op);
}

#endif // GASNETI_DISABLE_EOP_INTERFACE

/* ------------------------------------------------------------------------------------ */
/*
  Synchronization for explicit-handle non-blocking operations:
  ===========================================================
*/

/*  query an op for completeness 
 *  free it if complete
 *  returns 0 or 1 */
#if !defined(gasnete_test_syncnb) || \
    !defined(gasnete_test_syncnb_all) || \
    !defined(gasnete_test_syncnb_some)
GASNETI_INLINE(gasnete_op_try_free)
int gasnete_op_try_free(gasnetex_handle_t handle) {
  gasnete_op_t *op = (gasnete_op_t *)handle;

  gasneti_assert(op->threadidx == gasnete_mythread()->threadidx);

#ifdef GASNETE_OP_TRY_FREE_EXTRA
  // Hook to operate on conduit-specific handles
  GASNETE_OP_TRY_FREE_EXTRA(handle);
#endif

  if_pt (OPTYPE(op) == OPTYPE_EXPLICIT) {
    gasnete_eop_t *eop = (gasnete_eop_t*)op;

    if (gasnete_eop_isdone(eop)) {
      gasneti_sync_reads();
      gasnete_eop_free(eop);
      return 1;
    }
  } else {
    gasnete_iop_t *iop = (gasnete_iop_t*)op;

    if (gasnete_iop_isdone(iop)) {
      gasneti_sync_reads();
      gasnete_iop_free(iop);
      return 1;
    }
  }
  return 0;
}
#endif

/*  query an op for completeness 
 *  free it and clear the handle if complete
 *  returns 0 or 1 */
#if !defined(gasnete_test_syncnb_all) || \
    !defined(gasnete_test_syncnb_some)
GASNETI_INLINE(gasnete_op_try_free_clear)
int gasnete_op_try_free_clear(gasnetex_handle_t *handle_p) {
  if (gasnete_op_try_free(*handle_p)) {
    *handle_p = GASNETEX_INVALID_HANDLE;
    return 1;
  }
  return 0;
}
#endif

#ifndef gasnete_test_syncnb
extern int  gasnete_test_syncnb(gasnetex_handle_t handle) {
  return gasnete_op_try_free(handle) ? GASNET_OK : GASNET_ERR_NOT_READY;
}
#endif

#ifndef gasnete_test_syncnb_some
extern int  gasnete_test_syncnb_some (gasnetex_handle_t *phandle, size_t numhandles) {
  int success = 0;
  int empty = 1;

  gasneti_assert(phandle);

  { int i;
    for (i = 0; i < numhandles; i++) {
      if (phandle[i] != GASNETEX_INVALID_HANDLE) {
        empty = 0;
	success |= gasnete_op_try_free_clear(&phandle[i]);
      }
    }
  }

  return (success || empty) ? GASNET_OK : GASNET_ERR_NOT_READY;
}
#endif

#ifndef gasnete_test_syncnb_all
extern int  gasnete_test_syncnb_all (gasnetex_handle_t *phandle, size_t numhandles) {
  int success = 1;

  gasneti_assert(phandle);

  { int i;
    for (i = 0; i < numhandles; i++) {
      if (phandle[i] != GASNETEX_INVALID_HANDLE) {
        success &= gasnete_op_try_free_clear(&phandle[i]);
      }
    }
  }

  return success ? GASNET_OK : GASNET_ERR_NOT_READY;
}
#endif

/* ------------------------------------------------------------------------------------ */
/*
  Operations on local-completion handles
  ======================================
*/

/* This implementation assumes that ganetex_lc_handle_t is a pointer to
   either an eop or an iop, exactly as with gasnetex_handle_t.
*/

/*  query an op for local-completeness
 *  free it if complete (and appropriate)
 *  returns 0 or 1 */
#if !defined(gasnete_test_lc) || \
    !defined(gasnete_test_lc_all) || \
    !defined(gasnete_test_lc_some)
GASNETI_INLINE(gasnete_lc_try_free)
int gasnete_lc_try_free(gasnetex_lc_handle_t lchandle) {
  gasnete_op_t *op = (gasnete_op_t *)lchandle;

  gasneti_assert(op->threadidx == gasnete_mythread()->threadidx);
  if_pt (OPTYPE(op) == OPTYPE_EXPLICIT) {
    gasnete_eop_t *eop = (gasnete_eop_t*)op;

    if (GASNETE_EOP_LC(eop)) {
      if (eop->flags & EOPFLAG_LC_ONLY) {
        gasneti_sync_reads();
        gasnete_eop_free(eop);
      } else {
        gasneti_compiler_fence(); // TODO-EX: revisit this
      }
      return 1;
    }
  } else {
    gasnete_iop_t *iop = (gasnete_iop_t*)op;
    gasneti_assert(LCSTATE(iop) == LCSTATE_LIVE);

    if (GASNETE_IOP_CNTDONE(iop,alc)) {
      #if GASNET_DEBUG
        SET_LCSTATE(iop, LCSTATE_NONE);
      #endif
      gasneti_compiler_fence(); // TODO-EX: revisit this
      return 1;
    }
  }
  return 0;
}
#endif

/*  query an op for local-completeness
 *  free it and clear the handle if complete
 *  returns 0 or 1 */
#if !defined(gasnete_test_lc_all) || \
    !defined(gasnete_test_lc_some)
GASNETI_INLINE(gasnete_lc_try_free_clear)
int gasnete_lc_try_free_clear(gasnetex_lc_handle_t *lchandle_p) {
  if (gasnete_lc_try_free(*lchandle_p)) {
    *lchandle_p = GASNETEX_INVALID_LC_HANDLE;
    return 1;
  }
  return 0;
}
#endif

#ifndef gasnete_test_lc
extern int  gasnete_test_lc(gasnetex_lc_handle_t lchandle) {
  return gasnete_lc_try_free(lchandle) ? GASNET_OK : GASNET_ERR_NOT_READY;
}
#endif

#ifndef gasnete_test_lc_some
extern int  gasnete_test_lc_some (gasnetex_lc_handle_t *plchandle, size_t numlchandles) {
  int success = 0;
  int empty = 1;

  gasneti_assert(plchandle);

  { int i;
    for (i = 0; i < numlchandles; i++) {
      if (plchandle[i] != GASNETEX_INVALID_LC_HANDLE) {
        empty = 0;
        success |= gasnete_lc_try_free_clear(&plchandle[i]);
      }
    }
  }

  return (success || empty) ? GASNET_OK : GASNET_ERR_NOT_READY;
}
#endif

#ifndef gasnete_test_lc_all
extern int  gasnete_test_lc_all (gasnetex_lc_handle_t *plchandle, size_t numlchandles) {
  int success = 1;

  gasneti_assert(plchandle);

  { int i;
    for (i = 0; i < numlchandles; i++) {
      if (plchandle[i] != GASNETEX_INVALID_LC_HANDLE) {
        success &= gasnete_lc_try_free_clear(&plchandle[i]);
      }
    }
  }

  return success ? GASNET_OK : GASNET_ERR_NOT_READY;
}
#endif

#ifndef gasnete_test_lc_group
extern int gasnete_test_lc_group (GASNETI_THREAD_FARG_ALONE) {
  gasnete_threaddata_t * const mythread = GASNETI_MYTHREAD;
  gasnete_iop_t *iop = mythread->current_iop;
  gasneti_assert(iop->threadidx == mythread->threadidx);
  gasneti_assert(iop->next == NULL);
  gasneti_assert(OPTYPE(iop) == OPTYPE_IMPLICIT);
  #if GASNET_DEBUG
    if (iop->next != NULL)
      gasneti_fatalerror("VIOLATION: attempted to call gasnete_test_lc_group() inside an NBI access region");
  #endif

    if (GASNETE_IOP_CNTDONE(iop,alc)) {
      gasneti_compiler_fence(); // TODO-EX: revisit this
      return GASNET_OK;
    } else return GASNET_ERR_NOT_READY;
}
#endif

/* ------------------------------------------------------------------------------------ */
/*
  Synchronization for implicit-handle non-blocking operations:
  ===========================================================
*/

#ifndef gasnete_test_syncnbi_gets
extern int  gasnete_test_syncnbi_gets(GASNETI_THREAD_FARG_ALONE) {
  gasnete_threaddata_t * const mythread = GASNETI_MYTHREAD;
  gasnete_iop_t *iop = mythread->current_iop;
  gasneti_assert(iop->threadidx == mythread->threadidx);
  gasneti_assert(OPTYPE(iop) == OPTYPE_IMPLICIT);
  #if GASNET_DEBUG
    if (iop->next != NULL)
      gasneti_fatalerror("VIOLATION: attempted to call gasnete_test_syncnbi_gets() inside an NBI access region");
  #endif

    if (GASNETE_IOP_CNTDONE(iop,get)) {
      gasneti_sync_reads();
      return GASNET_OK;
    } else return GASNET_ERR_NOT_READY;
}
#endif

#ifndef gasnete_test_syncnbi_puts
extern int  gasnete_test_syncnbi_puts(GASNETI_THREAD_FARG_ALONE) {
  gasnete_threaddata_t * const mythread = GASNETI_MYTHREAD;
  gasnete_iop_t *iop = mythread->current_iop;
  gasneti_assert(iop->threadidx == mythread->threadidx);
  gasneti_assert(iop->next == NULL);
  gasneti_assert(OPTYPE(iop) == OPTYPE_IMPLICIT);
  #if GASNET_DEBUG
    if (iop->next != NULL)
      gasneti_fatalerror("VIOLATION: attempted to call gasnete_test_syncnbi_puts() inside an NBI access region");
  #endif

    // If any put_nbi calls passed LC_SYNC then we need to complete their LC too.
    // Since we don't track LC_SYNC calls separately, and since one expects LC before
    // RC to be the common case, it is simple/cheap to sync LC here unconditionally.
    if (GASNETE_IOP_CNTDONE(iop,put) && GASNETE_IOP_CNTDONE(iop,alc)) {
      gasneti_sync_reads();
      return GASNET_OK;
    } else return GASNET_ERR_NOT_READY;
}
#endif

/* ------------------------------------------------------------------------------------ */
/*
  Implicit access region synchronization
  ======================================
*/
/*  This implementation allows recursive access regions, although the spec does not require that */
/*  operations are associated with the most immediately enclosing access region */
#ifndef gasnete_begin_nbi_accessregion
extern void gasnete_begin_nbi_accessregion(gasnetex_flags_t flags, int allowrecursion GASNETI_THREAD_FARG) {
  gasnete_threaddata_t * const mythread = GASNETI_MYTHREAD;
  gasnete_iop_t *iop = gasnete_iop_new(mythread); /*  push an iop */
  GASNETI_TRACE_PRINTF(S,("BEGIN_NBI_ACCESSREGION"));
  #if GASNET_DEBUG
    if (!allowrecursion && mythread->current_iop->next != NULL)
      gasneti_fatalerror("VIOLATION: tried to initiate a recursive NBI access region");
  #endif
  iop->next = mythread->current_iop;
  mythread->current_iop = iop;
}
#endif

#ifndef gasnete_end_nbi_accessregion
extern gasnetex_handle_t gasnete_end_nbi_accessregion(gasnetex_lc_handle_t *lc_opt, gasnetex_flags_t flags GASNETI_THREAD_FARG) {
  gasnete_threaddata_t * const mythread = GASNETI_MYTHREAD;
  gasnete_iop_t *iop = mythread->current_iop; /*  pop an iop */
  GASNETI_TRACE_EVENT_VAL(S,END_NBI_ACCESSREGION,iop->initiated_get_cnt + iop->initiated_put_cnt);

  gasneti_assert(lc_opt != GASNETEX_LC_GROUP); // TODO-EX: allow this if we nest access region?
  if (GASNETE_IOP_CNTDONE(iop,alc)) {
    if (lc_opt) gasneti_lc_opt_finish(lc_opt);
    #if GASNET_DEBUG
      SET_LCSTATE(iop, LCSTATE_NONE);
    #endif
  } else {
    gasneti_assert(LCSTATE(iop) == LCSTATE_LIVE);
    #if GASNET_DEBUG
      if (lc_opt == NULL) // TODO-EX: better wording?
        gasneti_fatalerror("VIOLATION: call to gasnete_end_nbi_accessregion(lc_opt==NULL,...) with local completion outstanding");
    #endif
    if (lc_opt == GASNETEX_LC_INIT) {
      gasneti_polluntil(GASNETE_IOP_CNTDONE(iop,alc));
      #if GASNET_DEBUG
        SET_LCSTATE(iop, LCSTATE_NONE);
      #endif
    } else if (lc_opt == GASNETEX_LC_SYNC) {
      SET_LCSTATE(iop, LCSTATE_SYNC);
    } else {
      gasneti_assert(gasneti_lc_is_pointer(lc_opt));
      *lc_opt = (gasnetex_lc_handle_t)iop;
    }
  }

  #if GASNET_DEBUG
    if (iop->next == NULL)
      gasneti_fatalerror("VIOLATION: call to gasnete_end_nbi_accessregion() outside access region");
  #endif
  mythread->current_iop = iop->next;
  iop->next = NULL;
  return (gasnetex_handle_t)iop;
}
#endif

/* ------------------------------------------------------------------------------------ */


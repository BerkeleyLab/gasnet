/*   $Source: bitbucket.org:berkeleylab/gasnet.git/gasnet_event.c $
 * Description: GASNet event/eop/iop common code
 * Copyright 2002, Dan Bonachea <bonachea@cs.berkeley.edu>
 * Terms of use are as specified in license.txt
 */

#include <gasnet_event_internal.h>

extern void _gasnete_iop_check(gasnete_iop_t *iop) { gasnete_iop_check(iop); }

/* ------------------------------------------------------------------------------------ */
/*
  Op management
  =============
*/

#if !GASNETI_DISABLE_REFERENCE_EOP

/*  allocate more eops */
GASNETI_NEVER_INLINE(gasnete_eop_alloc,
extern void gasnete_eop_alloc(gasnete_threaddata_t * const thread)) {
    const size_t allocsz = GASNETI_ALIGNUP(sizeof(gasnete_eop_t),GASNETI_CACHE_LINE_BYTES);
    int bufidx = thread->eop_num_bufs;
    gasnete_eop_t *buf;
    int i;
    const gasnete_threadidx_t threadidx = thread->threadidx;
    if (bufidx == 256) gasneti_fatalerror("GASNet Extended API: Ran out of explicit events (limit=65535)");
    thread->eop_num_bufs++;
    buf = (gasnete_eop_t *)gasneti_calloc(256,allocsz);
    gasneti_leak(buf);
    for (i=0; i < 256; i++) {
      gasnete_eop_t *eop = (gasnete_eop_t *)((uintptr_t)buf + i*allocsz);
      eop->threadidx = threadidx;
      eop->next = (i==255) ? NULL: (gasnete_eop_t *)((uintptr_t)eop + allocsz);
      #if GASNET_DEBUG
        // Returns to type==free_eop when on free list
        eop->event[0] = gasnete_event_type_free_eop;
      #else
        // Type==eop at all times
        eop->event[0] = gasnete_event_type_eop;
      #endif
      #ifdef GASNETE_EOP_ALLOC_EXTRA
        // Hook for conduit-specific initializations and assertions
        GASNETE_EOP_ALLOC_EXTRA(eop);
      #endif
    }
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
        size_t eopidx = (((uintptr_t)eop) - ((uintptr_t)buf)) / allocsz;
        gasneti_assert(eopidx < 256);
        gasneti_assert(!seen[eopidx]);/* see if we hit a cycle */
        seen[eopidx] = 1;
        eop = eop->next;
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
      memset(iop, 0, sizeof(gasnete_iop_t)); /* set event[] and pad to known value */
    #else
      memset(&iop->event, 0, sizeof(iop->event)); /* set event[] to known value */
    #endif
    iop->event[0] = OPTYPE_IMPLICIT;
    iop->threadidx = thread->threadidx;
    iop->initiated_get_cnt = 0;
    iop->initiated_put_cnt = 0;
    gasnete_op_atomic_set(&(iop->completed_get_cnt), 0, 0);
    gasnete_op_atomic_set(&(iop->completed_put_cnt), 0, 0);
  #if GASNETE_HAVE_LC
    iop->initiated_alc_cnt = 0;
    gasnete_op_atomic_set(&(iop->completed_alc_cnt), 0, 0);
  #endif
  #ifdef GASNETE_IOP_ALLOC_EXTRA
    // Hook for conduit-specific initializations and assertions
    GASNETE_IOP_ALLOC_EXTRA(iop);
  #endif
    return iop;
}

/*  get a new iop */
extern
gasnete_iop_t *gasnete_iop_new(gasnete_threaddata_t * const thread) {
  gasnete_iop_t *iop = thread->iop_free;
  if_pt (iop) {
    thread->iop_free = iop->next;
    gasneti_memcheck(iop);
    gasneti_assert(iop->threadidx == thread->threadidx);
    #if GASNET_DEBUG
      gasneti_assert(OPTYPE(iop) == gasnete_event_type_free_iop);
      iop->event[0] = gasnete_event_type_iop;
    #endif
    /* If using trace or stats, want meaningful counts when tracing NBI access regions */
    #if GASNETI_STATS_OR_TRACE
      iop->initiated_get_cnt = 0;
      iop->initiated_put_cnt = 0;
      gasnete_op_atomic_set(&(iop->completed_get_cnt), 0, 0);
      gasnete_op_atomic_set(&(iop->completed_put_cnt), 0, 0);
    #endif
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


/* prepare to free an iop, but do not destroy anything that would
 * be necessary to test/wait on the iop.
 */
GASNETI_INLINE(gasnete_iop_prep_free)
void gasnete_iop_prep_free(gasnete_iop_t *iop) {
  gasnete_iop_check(iop);
  gasneti_assert(EVENT_ALL_DONE(iop));
  gasneti_assert(GASNETE_IOP_CNTDONE(iop,get));
  gasneti_assert(GASNETE_IOP_CNTDONE(iop,put));
  gasneti_assert(GASNETE_IOP_LC_CNTDONE(iop));
  gasneti_assert(iop->next == iop);
#ifdef GASNETE_IOP_PREP_FREE_EXTRA
  // Hook for conduit-specific cleanups and assertions
  GASNETE_IOP_PREP_FREE_EXTRA(iop);
#endif
#if GASNET_DEBUG
  gasneti_assert(iop->event[0] == gasnete_event_type_iop);
  iop->event[0] = gasnete_event_type_pendingfree_iop;
#endif
}

/*  free an iop */
static
void gasnete_iop_free(gasnete_iop_t *iop) {
  gasnete_threaddata_t * const thread = gasnete_threadtable[iop->threadidx];
  gasneti_assert(thread == gasnete_mythread()); // TODO-EX: to be removed
  gasnete_iop_prep_free(iop);
#ifdef GASNETE_IOP_FREE_EXTRA
  // Hook for conduit-specific cleanups
  // NOTE: Defining this adds an extra pass in {test,wait}_{some,all} and
  // therefore should only be used if there are steps that cannot safely be
  // performed in GASNETE_IOP_PREP_FREE_EXTRA.
  GASNETE_IOP_FREE_EXTRA(iop);
#endif
#if GASNET_DEBUG
  gasneti_assert(iop->event[0] == gasnete_event_type_pendingfree_iop);
  iop->event[0] = gasnete_event_type_free_iop;
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
  if (isget) GASNETE_IOP_CNT_FINISH(op, get, noperations, 0);
  else       GASNETE_IOP_CNT_FINISH(op, put, noperations, 0);
  gasnete_iop_check(op);
}

#endif // GASNETI_DISABLE_EOP_INTERFACE

/* ------------------------------------------------------------------------------------ */
/*
  Synchronization for explicit-event non-blocking operations:
  ===========================================================
*/

#ifndef gasnete_test
/*  query an op for completeness 
 *  free it if complete
 *  returns 0 or 1 */
GASNETI_INLINE(gasnete_op_try_free)
int gasnete_op_try_free(gex_Event_t event GASNETI_THREAD_FARG) {
#ifdef GASNETE_OP_TRY_FREE_EXTRA
  // Hook to operate on conduit-specific events
  GASNETE_OP_TRY_FREE_EXTRA(event);
  #error "GASNETE_OP_TRY_FREE_EXTRA not implemented in array test/wait ops"
#endif

  gasnete_event_check(event);

  // "Fast-path" detects outstanding event w/o any branches
  if (EVENT_LIVE_MASK & *(volatile uint8_t *)event) return 0;

  // "Slow-path" must distinguish root from leaf
  const unsigned int idx = gasneti_event_idx(event);
  if_pt (! idx) { // It's a root event event
    if (EVENT_ANY_LIVE(event)) return 0;

    // TODO-EX:
    // Could potentially weaken "sync_reads" for some cases?
    // However, that might not be worth the branching it would require.
    gasneti_sync_reads();

    // TODO-EX: the mask operation in OPTYPE() unnecessary?
    if_pt (OPTYPE((gasnete_op_t*)event) == OPTYPE_EXPLICIT) {
      gasnete_eop_free((gasnete_eop_t*)event);
    } else {
      gasnete_iop_free((gasnete_iop_t*)event);
    }
  } else { // It's a leaf event
    gasneti_assert(EVENT_DONE(gasneti_event_op(event), idx)); // confirm the EVENT_LIVE_MASK result
    gasneti_compiler_fence(); // TODO-EX: revisit this
  }

  return 1;
}

extern int  gasnete_test(gex_Event_t event GASNETI_THREAD_FARG) {
  gasneti_assert(event != GEX_EVENT_INVALID); // invalid handled inline in header
  return gasnete_op_try_free(event GASNETI_THREAD_PASS) ? GASNET_OK : GASNET_ERR_NOT_READY;
}
#endif

#if !defined(gasnete_test_all) || \
    !defined(gasnete_test_some)
GASNETI_INLINE(gasnete_test_array)
int gasnete_test_array(const int is_all, gex_Event_t *pevent, size_t numevents GASNETI_THREAD_FARG) {
  gasnete_threaddata_t * const mythread = GASNETI_MYTHREAD;
  gasnete_eop_t *eop_head = NULL, **eop_tail_p = &eop_head;
  gasnete_iop_t *iop_head = NULL, **iop_tail_p = &iop_head;
  int all_synced = 1;
  int some_synced = 0;
  int empty = 1;
  size_t to_retest = 0;

  gasneti_assert(pevent);

  // NOTE: We must allow leaves and their corresponding roots to appear in the
  // array in any order while ensuring that we don't sync a root but not some
  // corresponding leaf.  The current approach is to make two passes, the first
  // testing all events.  If and only if the first pass synced at least one
  // root and failed to sync at least one leaf, a second pass is made to retest
  // all of the remaining leaves.
  //
  // Between the two passes, the roots synced in the first pass are kept on a
  // temporary linked-list to avoid them being recycled from the free list
  // while their leaves are still being tested.  Even in the absence of a
  // second pass, this will yield an efficient "bulk free".

  // Pass 1: test all events, evaluate 'empty', and count 'to_retest'
  for (size_t i = 0; i < numevents; i++) {
    const gex_Event_t event = pevent[i];
    if (GEX_EVENT_INVALID != event) {
      gasnete_event_check(event);
      empty = 0;
      if (gasneti_event_idx(event)) { // It's a leaf
        if (EVENT_LIVE_MASK & *(volatile uint8_t *)pevent[i]) {
          // Do NOT "all_synced = 0" since we'll retry this leaf in second pass
          to_retest += 1;
          continue;
        }
      } else { // It's a root
        if (EVENT_ANY_LIVE(event)) {
          all_synced = 0;
          continue;
        }

        // TODO-EX: we need to revist this sync_reads() calll.
        // Can we weaken it for some cases (if the branches don't eliminate the benfits)?
        // Can we ever safely move it out of this loop?
        // Should it move inside the ..._prep_free() calls and become DEBUG only?
        // Of course, if any Gets were syned we still need at least one RMB before return.
        gasneti_sync_reads();

        // TODO-EX: track if all are from same thread so bulk free can act accordingly
        gasnete_op_t *op = (gasnete_op_t*)event;
        gasneti_assert(op->threadidx == mythread->threadidx); // TODO-EX: until we event "foreign" ops
        if (OPTYPE(op) == OPTYPE_EXPLICIT) { // TODO-EX: the mask operation in OPTYPE() unnecessary?
          gasnete_eop_t *eop = (gasnete_eop_t*)op;
          gasnete_eop_prep_free(eop);
          *eop_tail_p = eop;
          eop_tail_p = &eop->next;
          eop->next = NULL;
        } else {
          gasnete_iop_t *iop = (gasnete_iop_t*)op;
          gasnete_iop_prep_free(iop);
          *iop_tail_p = iop;
          iop_tail_p = &iop->next;
          iop->next = NULL;
        }
      }

      pevent[i] = GEX_EVENT_INVALID;
      some_synced = 1;
    }
  }

  // Pass 2: retest all still-live leaf events (if any)
  gasneti_assert(! gasneti_event_idx(GEX_EVENT_INVALID));
  if (to_retest) {
    if (eop_head || iop_head) {
      size_t to_test = to_retest;
      for (size_t i = 0; to_test; i++) {
        gasneti_assert(i < numevents);
        const gex_Event_t event = pevent[i];
        if (gasneti_event_idx(event)) {
          to_test -= 1;
          if (EVENT_LIVE_MASK & *(volatile uint8_t *)event) {
            all_synced = 0;
          } else {
            pevent[i] = GEX_EVENT_INVALID;
	    some_synced = 1;
          }
        }
      }
    } else {
      all_synced = 0;
    }
  }

  // Bulk free
  // TODO-EX: deal with foreign (other theads) ops
  if (eop_head) {
  #if defined(GASNET_DEBUG) || defined(GASNETE_EOP_FREE_EXTRA)
    gasnete_eop_t *eop = eop_head;
    do {
    #ifdef GASNETE_EOP_FREE_EXTRA
      GASNETE_EOP_FREE_EXTRA(eop);
    #endif
    #if GASNET_DEBUG
      eop->event[0] = gasnete_event_type_free_eop;
    #endif
      eop = eop->next;
    } while(eop);
  #endif
    gasnete_threaddata_t * const thread = gasnete_threadtable[eop_head->threadidx];
    *eop_tail_p = thread->eop_free;
    thread->eop_free = eop_head;
  }
  if (iop_head) {
  #if defined(GASNET_DEBUG) || defined(GASNETE_IOP_FREE_EXTRA)
    gasnete_iop_t *iop = iop_head;
    do {
    #ifdef GASNETE_IOP_FREE_EXTRA
      GASNETE_EOP_FREE_EXTRA(iop);
    #endif
    #if GASNET_DEBUG
      iop->event[0] = gasnete_event_type_free_iop;
    #endif
      iop = iop->next;
    } while(iop);
  #endif
    gasnete_threaddata_t * const thread = gasnete_threadtable[iop_head->threadidx];
    *iop_tail_p = thread->iop_free;
    thread->iop_free = iop_head;
  }

  return is_all ? all_synced : (some_synced || empty);
}
#endif

#ifndef gasnete_test_some
extern int  gasnete_test_some (gex_Event_t *pevent, size_t numevents GASNETI_THREAD_FARG) {
  return gasnete_test_array(0, pevent, numevents GASNETI_THREAD_PASS) ? GASNET_OK : GASNET_ERR_NOT_READY;
}
#endif

#ifndef gasnete_test_all
extern int  gasnete_test_all (gex_Event_t *pevent, size_t numevents GASNETI_THREAD_FARG) {
  return gasnete_test_array(1, pevent, numevents GASNETI_THREAD_PASS) ? GASNET_OK : GASNET_ERR_NOT_READY;
}
#endif

/* ------------------------------------------------------------------------------------ */
/*
  Synchronization for implicit-event non-blocking operations:
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

    // If any put_nbi calls passed EVENT_DEFER then we need to complete their LC too.
    if (GASNETE_IOP_CNTDONE(iop,put) && GASNETE_IOP_LC_CNTDONE(iop)) {
      gasneti_sync_reads(); // TODO-EX: revisit this
      return GASNET_OK;
    } else return GASNET_ERR_NOT_READY;
}
#endif

#ifndef gasnete_test_syncnbi_mask
extern int gasnete_test_syncnbi_mask(unsigned int mask, gex_Flags_t flags GASNETI_THREAD_FARG) {
  gasnete_threaddata_t * const mythread = GASNETI_MYTHREAD;
  gasnete_iop_t *iop = mythread->current_iop;
  gasneti_assert(iop->threadidx == mythread->threadidx);
  gasneti_assert(iop->next == NULL);
  gasneti_assert(OPTYPE(iop) == OPTYPE_IMPLICIT);
  #if GASNET_DEBUG
    if (iop->next != NULL)
      gasneti_fatalerror("VIOLATION: attempted to call gasnete_test_syncnbi_mask() inside an NBI access region");
  #endif

  if (mask & GEX_NBI_LC) {
    if (! GASNETE_IOP_LC_CNTDONE(iop)) return GASNET_ERR_NOT_READY;
  }
  if (mask & GEX_NBI_PUTS) {
    if (! GASNETE_IOP_CNTDONE(iop,put)) return GASNET_ERR_NOT_READY;
  }
  if (mask & GEX_NBI_GETS) {
    if (! GASNETE_IOP_CNTDONE(iop,get)) return GASNET_ERR_NOT_READY;
    gasneti_sync_reads(); // TODO-EX: revisit this
  } else {
    gasneti_compiler_fence(); // TODO-EX: revisit this
  }

  return GASNET_OK;
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
extern void gasnete_begin_nbi_accessregion(gex_Flags_t flags, int allowrecursion GASNETI_THREAD_FARG) {
  gasnete_threaddata_t * const mythread = GASNETI_MYTHREAD;
  gasnete_iop_t *iop = gasnete_iop_new(mythread); /*  push an iop */
  GASNETI_TRACE_PRINTF(S,("BEGIN_NBI_ACCESSREGION"));
  #if GASNET_DEBUG
    if (!allowrecursion && mythread->current_iop->next != NULL)
      gasneti_fatalerror("VIOLATION: tried to initiate a recursive NBI access region");
  #endif

  iop->initiated_put_cnt++;
  iop->initiated_get_cnt++;
  SET_EVENT_TYPE(iop, gasnete_iop_event_put, gasnete_event_type_iop);
  SET_EVENT_TYPE(iop, gasnete_iop_event_get, gasnete_event_type_iop);
#if GASNETE_HAVE_LC
  iop->initiated_alc_cnt++;
  SET_EVENT_TYPE(iop, gasnete_iop_event_alc, gasnete_event_type_lc);
#endif

  iop->next = mythread->current_iop;
  mythread->current_iop = iop;
}
#endif

#ifndef gasnete_end_nbi_accessregion
extern gex_Event_t gasnete_end_nbi_accessregion(gex_Flags_t flags GASNETI_THREAD_FARG) {
  gasnete_threaddata_t * const mythread = GASNETI_MYTHREAD;
  gasnete_iop_t *iop = mythread->current_iop; /*  pop an iop */
  GASNETI_TRACE_EVENT_VAL(S,END_NBI_ACCESSREGION,iop->initiated_get_cnt + iop->initiated_put_cnt);

  GASNETE_IOP_CNT_FINISH_REG(iop, put, 1, 0);
  GASNETE_IOP_CNT_FINISH_REG(iop, get, 1, 0);
#if GASNETE_HAVE_LC
  GASNETE_IOP_CNT_FINISH_REG(iop, alc, 1, 0);
#endif

  #if GASNET_DEBUG
    if (iop->next == NULL)
      gasneti_fatalerror("VIOLATION: call to gasnete_end_nbi_accessregion() outside access region");
  #endif
  mythread->current_iop = iop->next;
  iop->next = iop; /* Identifies an iop returned from access region */
  return (gex_Event_t)iop;
}
#endif

#ifndef gasnete_Event_QueryLeaf
#if GASNET_DEBUG
static void _gasnete_get_leaf_check(gasnete_op_t *op, unsigned int event_id) {
  gasneti_assert(! gasneti_event_idx(op));
  switch (OPTYPE(op)) {
    case OPTYPE_IMPLICIT: {
      gasnete_iop_t *iop = (gasnete_iop_t*)op;
      gasnete_iop_check(iop);
      gasneti_assert(iop->next); // was returned from access region
      switch (event_id) {
        case GEX_NBI_PUTS:  // fall-through...
        case GEX_NBI_GETS:  // fall-through...
        case GEX_NBI_LC:    return;
      }
      break;
    }
    case OPTYPE_EXPLICIT: {
      gasnete_eop_t *eop = (gasnete_eop_t*)op;
      gasnete_eop_check(eop);
      switch (event_id) {
        case GEX_NBI_LC: return;
      }
      break;
    }
  }
  gasneti_fatalerror("Invalid arguments to gex_Event_QueryLeaf()");
}
#else
  #define _gasnete_get_leaf_check(op, event_id) ((void)0)
#endif

extern gex_Event_t gasnete_Event_QueryLeaf(gex_Event_t root, unsigned int event_id) {
  gasnete_op_t *op = (gasnete_op_t*)root;
  _gasnete_get_leaf_check(op, event_id);

  gasneti_assert(gasnete_eop_event_alc == gasnete_iop_event_alc); // TODO-EX: move elsewhere

  switch (event_id) {
    case GEX_NBI_PUTS: return gasneti_op_event(op, gasnete_iop_event_put);
    case GEX_NBI_GETS: return gasneti_op_event(op, gasnete_iop_event_get);
    case GEX_NBI_LC:   return gasneti_op_event(op, gasnete_iop_event_alc);
  }

  gasneti_fatalerror("Invalid arguments to gex_Event_QueryLeaf()");
  return GEX_EVENT_INVALID; // NOT REACHED
}
#endif

/* ------------------------------------------------------------------------------------ */


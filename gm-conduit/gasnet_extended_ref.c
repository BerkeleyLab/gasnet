/* $Id: gasnet_extended_ref.c,v 1.6.12.1 2003/10/27 13:04:12 bonachea Exp $
 * $Date: 2003/10/27 13:04:12 $
 * Description: GASNet GM conduit Extended API Implementation
 * Copyright 2002, Dan Bonachea <bonachea@cs.berkeley.edu>
 * Terms of use are as specified in license.txt
 */
#include <gasnet.h>
#include <gasnet_extended_internal.h>
#include <gasnet_internal.h>
#include <gasnet_handler.h>

/* ------------------------------------------------------------------------------------ */
/*
 * Design/Approach for gets/puts in Extended Reference API in terms of Core
 * ========================================================================
 *
 * The extended API implements gasnet_put and gasnet_put_nbi differently, 
 * all in terms of 'nbytes', the number of bytes to be transferred as 
 * payload.
 *
 * The core usually implements AMSmall and AMMedium as host-side copies and
 * AMLongs are implemented according to the implementation.  Some conduits 
 * may optimize AMLongRequest/AMLongRequestAsync/AMLongReply with DMA
 * operations.
 *
 * gasnet_put(_bulk) is translated to a gasnete_put_nb(_bulk) + sync
 * gasnet_get(_bulk) is translated to a gasnete_get_nb(_bulk) + sync
 *
 * gasnete_put_nb(_bulk) translates to
 *    AMMedium(payload) if nbytes < GASNETE_GETPUT_MEDIUM_LONG_THRESHOLD
 *    AMLongRequest(payload) if nbytes < AMMaxLongRequest
 *    gasnete_put_nbi(_bulk)(payload) otherwise
 * gasnete_get_nb(_bulk) translates to
 *    AMSmall request + AMMedium(payload) if nbytes < GASNETE_GETPUT_MEDIUM_LONG_THRESHOLD
 *    gasnete_get_nbi(_bulk)() otherwise
 *
 * gasnete_put_nbi(_bulk) translates to
 *    AMMedium(payload) if nbytes < GASNETE_GETPUT_MEDIUM_LONG_THRESHOLD
 *    AMLongRequest(payload) if nbytes < AMMaxLongRequest
 *    chunks of AMMaxLongRequest with AMLongRequest() otherwise
 *    AMLongRequestAsync is used instead of AMLongRequest for put_bulk
 * gasnete_get_nbi(_bulk) translates to
 *    AMSmall request + AMMedium(payload) if nbytes < GASNETE_GETPUT_MEDIUM_LONG_THRESHOLD
 *    chunks of AMMaxMedium with AMSmall request + AMMedium() otherwise
 *
 * The current implementation uses AMLongs for large puts because the 
 * destination is guaranteed to fall within the registered GASNet segment.
 * The spec allows gets to be received anywhere into the virtual memory space,
 * so we can only use AMLong when the destination happens to fall within the 
 * segment - GASNETE_USE_LONG_GETS indicates whether or not we should try to do this.
 * (conduits which can support AMLongs to areas outside the segment
 * could improve on this through the use of this conduit-specific information).
 * 
 */

/* ------------------------------------------------------------------------------------ */
/*
  Non-blocking memory-to-memory transfers (explicit handle)
  ==========================================================
*/
/* ------------------------------------------------------------------------------------ */
GASNET_INLINE_MODIFIER(gasnete_extref_get_reqh_inner)
void gasnete_extref_get_reqh_inner(gasnet_token_t token, 
  gasnet_handlerarg_t nbytes, void *dest, void *src, void *op) {
  gasneti_assert(nbytes <= gasnet_AMMaxMedium());
  GASNETE_SAFE(
    MEDIUM_REP(2,4,(token, gasneti_handleridx(gasnete_extref_get_reph),
                  src, nbytes, 
                  PACK(dest), PACK(op))));
}
SHORT_HANDLER(gasnete_extref_get_reqh,4,7, 
              (token, a0, UNPACK(a1),      UNPACK(a2),      UNPACK(a3)     ),
              (token, a0, UNPACK2(a1, a2), UNPACK2(a3, a4), UNPACK2(a5, a6)));
/* ------------------------------------------------------------------------------------ */
GASNET_INLINE_MODIFIER(gasnete_extref_get_reph_inner)
void gasnete_extref_get_reph_inner(gasnet_token_t token, 
  void *addr, size_t nbytes,
  void *dest, void *op) {
  GASNETE_FAST_UNALIGNED_MEMCPY(dest, addr, nbytes);
  gasneti_memsync();
  gasnete_op_markdone((gasnete_op_t *)op, 1);
}
MEDIUM_HANDLER(gasnete_extref_get_reph,2,4,
              (token,addr,nbytes, UNPACK(a0),      UNPACK(a1)    ),
              (token,addr,nbytes, UNPACK2(a0, a1), UNPACK2(a2, a3)));
/* ------------------------------------------------------------------------------------ */
GASNET_INLINE_MODIFIER(gasnete_extref_getlong_reqh_inner)
void gasnete_extref_getlong_reqh_inner(gasnet_token_t token, 
  gasnet_handlerarg_t nbytes, void *dest, void *src, void *op) {

  GASNETE_SAFE(
    LONG_REP(1,2,(token, gasneti_handleridx(gasnete_extref_getlong_reph),
                  src, nbytes, dest,
                  PACK(op))));
}
SHORT_HANDLER(gasnete_extref_getlong_reqh,4,7, 
              (token, a0, UNPACK(a1),      UNPACK(a2),      UNPACK(a3)     ),
              (token, a0, UNPACK2(a1, a2), UNPACK2(a3, a4), UNPACK2(a5, a6)));
/* ------------------------------------------------------------------------------------ */
GASNET_INLINE_MODIFIER(gasnete_extref_getlong_reph_inner)
void gasnete_extref_getlong_reph_inner(gasnet_token_t token, 
  void *addr, size_t nbytes, 
  void *op) {
  gasneti_memsync();
  gasnete_op_markdone((gasnete_op_t *)op, 1);
}
LONG_HANDLER(gasnete_extref_getlong_reph,1,2,
              (token,addr,nbytes, UNPACK(a0)     ),
              (token,addr,nbytes, UNPACK2(a0, a1)));
/* ------------------------------------------------------------------------------------ */
GASNET_INLINE_MODIFIER(gasnete_extref_put_reqh_inner)
void gasnete_extref_put_reqh_inner(gasnet_token_t token, 
  void *addr, size_t nbytes,
  void *dest, void *op) {
  GASNETE_FAST_UNALIGNED_MEMCPY(dest, addr, nbytes);
  gasneti_memsync();
  GASNETE_SAFE(
    SHORT_REP(1,2,(token, gasneti_handleridx(gasnete_extref_markdone_reph),
                  PACK(op))));
}
MEDIUM_HANDLER(gasnete_extref_put_reqh,2,4, 
              (token,addr,nbytes, UNPACK(a0),      UNPACK(a1)     ),
              (token,addr,nbytes, UNPACK2(a0, a1), UNPACK2(a2, a3)));
/* ------------------------------------------------------------------------------------ */
GASNET_INLINE_MODIFIER(gasnete_extref_putlong_reqh_inner)
void gasnete_extref_putlong_reqh_inner(gasnet_token_t token, 
  void *addr, size_t nbytes,
  void *op) {
  gasneti_memsync();
  GASNETE_SAFE(
    SHORT_REP(1,2,(token, gasneti_handleridx(gasnete_extref_markdone_reph),
                  PACK(op))));
}
LONG_HANDLER(gasnete_extref_putlong_reqh,1,2, 
              (token,addr,nbytes, UNPACK(a0)     ),
              (token,addr,nbytes, UNPACK2(a0, a1)));
/* ------------------------------------------------------------------------------------ */
GASNET_INLINE_MODIFIER(gasnete_extref_memset_reqh_inner)
void gasnete_extref_memset_reqh_inner(gasnet_token_t token, 
  gasnet_handlerarg_t val, gasnet_handlerarg_t nbytes, void *dest, void *op) {
  memset(dest, (int)(uint32_t)val, nbytes);
  gasneti_memsync();
  GASNETE_SAFE(
    SHORT_REP(1,2,(token, gasneti_handleridx(gasnete_extref_markdone_reph),
                  PACK(op))));
}
SHORT_HANDLER(gasnete_extref_memset_reqh,4,6,
              (token, a0, a1, UNPACK(a2),      UNPACK(a3)     ),
              (token, a0, a1, UNPACK2(a2, a3), UNPACK2(a4, a5)));
/* ------------------------------------------------------------------------------------ */
GASNET_INLINE_MODIFIER(gasnete_extref_markdone_reph_inner)
void gasnete_extref_markdone_reph_inner(gasnet_token_t token, 
  void *op) {
  gasnete_op_markdone((gasnete_op_t *)op, 0); /*  assumes this is a put or explicit */
}
SHORT_HANDLER(gasnete_extref_markdone_reph,1,2,
              (token, UNPACK(a0)    ),
              (token, UNPACK2(a0, a1)));
/* ------------------------------------------------------------------------------------ */

gasnet_handle_t gasnete_extref_get_nb_bulk (void *dest, gasnet_node_t node, void *src, size_t nbytes GASNETE_THREAD_FARG) {
  if (nbytes <= GASNETE_GETPUT_MEDIUM_LONG_THRESHOLD) {
    gasnete_eop_t *op = gasnete_eop_new(GASNETE_MYTHREAD);

    GASNETE_SAFE(
      SHORT_REQ(4,7,(node, gasneti_handleridx(gasnete_extref_get_reqh), 
                   (gasnet_handlerarg_t)nbytes, PACK(dest), PACK(src), PACK(op))));

    return (gasnet_handle_t)op;
  } else {
    /*  need many messages - use an access region to coalesce them into a single handle */
    /*  (note this relies on the fact that our implementation of access regions allows recursion) */
    gasnete_begin_nbi_accessregion(1 /* enable recursion */ GASNETE_THREAD_PASS);
    gasnete_extref_get_nbi_bulk(dest, node, src, nbytes GASNETE_THREAD_PASS);
    return gasnete_end_nbi_accessregion(GASNETE_THREAD_PASS_ALONE);
  }
}

GASNET_INLINE_MODIFIER(gasnete_extref_put_nb_inner)
gasnet_handle_t gasnete_extref_put_nb_inner(gasnet_node_t node, void *dest, void *src, size_t nbytes, int isbulk GASNETE_THREAD_FARG) {
  if (nbytes <= GASNETE_GETPUT_MEDIUM_LONG_THRESHOLD) {
    gasnete_eop_t *op = gasnete_eop_new(GASNETE_MYTHREAD);

    GASNETE_SAFE(
      MEDIUM_REQ(2,4,(node, gasneti_handleridx(gasnete_extref_put_reqh),
                    src, nbytes,
                    PACK(dest), PACK(op))));

    return (gasnet_handle_t)op;
  } else if (nbytes <= gasnet_AMMaxLongRequest()) {
    gasnete_eop_t *op = gasnete_eop_new(GASNETE_MYTHREAD);

    if (isbulk) {
      GASNETE_SAFE(
        LONGASYNC_REQ(1,2,(node, gasneti_handleridx(gasnete_extref_putlong_reqh),
                    src, nbytes, dest,
                    PACK(op))));
    } else {
      GASNETE_SAFE(
        LONG_REQ(1,2,(node, gasneti_handleridx(gasnete_extref_putlong_reqh),
                    src, nbytes, dest,
                    PACK(op))));
    }

    return (gasnet_handle_t)op;
  } else { 
    /*  need many messages - use an access region to coalesce them into a single handle */
    /*  (note this relies on the fact that our implementation of access regions allows recursion) */
    gasnete_begin_nbi_accessregion(1 /* enable recursion */ GASNETE_THREAD_PASS);
      if (isbulk) gasnete_extref_put_nbi_bulk(node, dest, src, nbytes GASNETE_THREAD_PASS);
      else        gasnete_extref_put_nbi    (node, dest, src, nbytes GASNETE_THREAD_PASS);
    return gasnete_end_nbi_accessregion(GASNETE_THREAD_PASS_ALONE);
  }
}

gasnet_handle_t gasnete_extref_put_nb      (gasnet_node_t node, void *dest, void *src, size_t nbytes GASNETE_THREAD_FARG) {
  return gasnete_extref_put_nb_inner(node, dest, src, nbytes, 0 GASNETE_THREAD_PASS);
}

gasnet_handle_t gasnete_extref_put_nb_bulk (gasnet_node_t node, void *dest, void *src, size_t nbytes GASNETE_THREAD_FARG) {
  return gasnete_extref_put_nb_inner(node, dest, src, nbytes, 1 GASNETE_THREAD_PASS);
}

gasnet_handle_t gasnete_extref_memset_nb   (gasnet_node_t node, void *dest, int val, size_t nbytes GASNETE_THREAD_FARG) {
  gasnete_eop_t *op = gasnete_eop_new(GASNETE_MYTHREAD);

  GASNETE_SAFE(
    SHORT_REQ(4,6,(node, gasneti_handleridx(gasnete_extref_memset_reqh),
                 (gasnet_handlerarg_t)val, (gasnet_handlerarg_t)nbytes,
                 PACK(dest), PACK(op))));

  return (gasnet_handle_t)op;
}

/* ------------------------------------------------------------------------------------ */
/*
  Non-blocking memory-to-memory transfers (implicit handle)
  ==========================================================
  each message sends an ack - we count the number of implicit ops launched and compare
    with the number acknowledged
  Another possible design would be to eliminate some of the acks (at least for puts) 
    by piggybacking them on other messages (like get replies) or simply aggregating them
    the target until the source tries to synchronize
*/

void gasnete_extref_get_nbi_bulk (void *dest, gasnet_node_t node, void *src, size_t nbytes GASNETE_THREAD_FARG) {
  gasnete_threaddata_t * const mythread = GASNETE_MYTHREAD;
  gasnete_iop_t *op = mythread->current_iop;
  if (nbytes <= GASNETE_GETPUT_MEDIUM_LONG_THRESHOLD) {
    op->initiated_get_cnt++;
  
    GASNETE_SAFE(
      SHORT_REQ(4,7,(node, gasneti_handleridx(gasnete_extref_get_reqh), 
                   (gasnet_handlerarg_t)nbytes, PACK(dest), PACK(src), PACK(op))));
    return;
  } else {
    int chunksz;
    int msgsent=0;
    gasnet_handler_t reqhandler;
    uint8_t *psrc = src;
    uint8_t *pdest = dest;
    #if GASNETE_USE_LONG_GETS
      /* TODO: optimize this check by caching segment upper-bound in gasnete_seginfo */
      if (dest >= gasnete_seginfo[gasnete_mynode].addr &&
         (((uintptr_t)dest) + nbytes) <= 
          (((uintptr_t)gasnete_seginfo[gasnete_mynode].addr) +
                       gasnete_seginfo[gasnete_mynode].size)) {
        chunksz = gasnet_AMMaxLongReply();
        reqhandler = gasneti_handleridx(gasnete_extref_getlong_reqh);
      }
      else 
    #endif
      { reqhandler = gasneti_handleridx(gasnete_extref_get_reqh);
        chunksz = gasnet_AMMaxMedium();
      }
    for (;;) {
      msgsent++;
      if (nbytes > chunksz) {
        GASNETE_SAFE(
          SHORT_REQ(4,7,(node, reqhandler, 
                       (gasnet_handlerarg_t)chunksz, PACK(pdest), PACK(psrc), PACK(op))));
        nbytes -= chunksz;
        psrc += chunksz;
        pdest += chunksz;
      } else {
        GASNETE_SAFE(
          SHORT_REQ(4,7,(node, reqhandler, 
                       (gasnet_handlerarg_t)nbytes, PACK(pdest), PACK(psrc), PACK(op))));
        break;
      }
    }
    op->initiated_get_cnt += msgsent;
    return;
  }
}

GASNET_INLINE_MODIFIER(gasnete_extref_put_nbi_inner)
void gasnete_extref_put_nbi_inner(gasnet_node_t node, void *dest, void *src, size_t nbytes, int isbulk GASNETE_THREAD_FARG) {
  gasnete_threaddata_t * const mythread = GASNETE_MYTHREAD;
  gasnete_iop_t *op = mythread->current_iop;

  if (nbytes <= GASNETE_GETPUT_MEDIUM_LONG_THRESHOLD) {
    op->initiated_put_cnt++;

    GASNETE_SAFE(
      MEDIUM_REQ(2,4,(node, gasneti_handleridx(gasnete_extref_put_reqh),
                    src, nbytes,
                    PACK(dest), PACK(op))));
    return;
  } else if (nbytes <= gasnet_AMMaxLongRequest()) {
    op->initiated_put_cnt++;

    if (isbulk) {
      GASNETE_SAFE(
        LONGASYNC_REQ(1,2,(node, gasneti_handleridx(gasnete_extref_putlong_reqh),
                      src, nbytes, dest,
                      PACK(op))));
    } else {
      GASNETE_SAFE(
        LONG_REQ(1,2,(node, gasneti_handleridx(gasnete_extref_putlong_reqh),
                      src, nbytes, dest,
                      PACK(op))));
    }

    return;
  } else {
    int chunksz = gasnet_AMMaxLongRequest();
    int msgsent=0;
    uint8_t *psrc = src;
    uint8_t *pdest = dest;
    for (;;) {
      msgsent++;
      if (nbytes > chunksz) {
        if (isbulk) {
          GASNETE_SAFE(
            LONGASYNC_REQ(1,2,(node, gasneti_handleridx(gasnete_extref_putlong_reqh),
                          psrc, chunksz, pdest,
                          PACK(op))));
        } else {
          GASNETE_SAFE(
            LONG_REQ(1,2,(node, gasneti_handleridx(gasnete_extref_putlong_reqh),
                          psrc, chunksz, pdest,
                          PACK(op))));
        }
        nbytes -= chunksz;
        psrc += chunksz;
        pdest += chunksz;
      } else {
        if (isbulk) {
          GASNETE_SAFE(
            LONGASYNC_REQ(1,2,(node, gasneti_handleridx(gasnete_extref_putlong_reqh),
                          psrc, nbytes, pdest,
                          PACK(op))));
        } else {
          GASNETE_SAFE(
            LONG_REQ(1,2,(node, gasneti_handleridx(gasnete_extref_putlong_reqh),
                          psrc, nbytes, pdest,
                          PACK(op))));
        }
        break;
      }
    }
    op->initiated_put_cnt += msgsent;
    return;
  }
}

void gasnete_extref_put_nbi      (gasnet_node_t node, void *dest, void *src, size_t nbytes GASNETE_THREAD_FARG) {
  gasnete_extref_put_nbi_inner(node, dest, src, nbytes, 0 GASNETE_THREAD_PASS);
}

void gasnete_extref_put_nbi_bulk (gasnet_node_t node, void *dest, void *src, size_t nbytes GASNETE_THREAD_FARG) {
  gasnete_extref_put_nbi_inner(node, dest, src, nbytes, 1 GASNETE_THREAD_PASS);
}

void gasnete_extref_memset_nbi   (gasnet_node_t node, void *dest, int val, size_t nbytes GASNETE_THREAD_FARG) {
  gasnete_threaddata_t * const mythread = GASNETE_MYTHREAD;
  gasnete_iop_t *op = mythread->current_iop;
  op->initiated_put_cnt++;

  GASNETE_SAFE(
    SHORT_REQ(4,6,(node, gasneti_handleridx(gasnete_extref_memset_reqh),
                 (gasnet_handlerarg_t)val, (gasnet_handlerarg_t)nbytes,
                 PACK(dest), PACK(op))));
}
/* ------------------------------------------------------------------------------------ */
/*
  Barriers:
  =========
*/
/*  TODO: optimize this */
/*  a silly, centralized barrier implementation:
     everybody sends notifies to a single node, where we count them up
     central node eventually notices the barrier is complete (probably
     when it calls wait) and then it broadcasts the completion to all the nodes
    The main problem is the need for the master to call wait before the barrier can
     make progress - we really need a way for the "last thread" to notify all 
     the threads when completion is detected, but AM semantics don't provide a 
     simple way to do this.
    The centralized nature also makes it non-scalable - we really want to use 
     a tree-based barrier or pairwise exchange algorithm for scalability
     (but these impose even greater potential delays due to the lack of attentiveness to
     barrier progress)
 */

static enum { OUTSIDE_BARRIER, INSIDE_BARRIER } barrier_splitstate = OUTSIDE_BARRIER;
static int volatile barrier_value; /*  local barrier value */
static int volatile barrier_flags; /*  local barrier flags */
static int volatile barrier_phase = 0;  /*  2-phase operation to improve pipelining */
static int volatile barrier_response_done[2] = { 0, 0 }; /*  non-zero when barrier is complete */
static int volatile barrier_response_mismatch[2] = { 0, 0 }; /*  non-zero if we detected a mismatch */
#if GASNETI_STATS_OR_TRACE
  static gasneti_stattime_t barrier_notifytime; /* for statistical purposes */ 
#endif

/*  global state on P0 */
#define GASNETE_BARRIER_MASTER (gasnete_nodes-1)
static gasnet_hsl_t barrier_lock = GASNET_HSL_INITIALIZER;
static int volatile barrier_consensus_value[2]; /*  consensus barrier value */
static int volatile barrier_consensus_value_present[2] = { 0, 0 }; /*  consensus barrier value found */
static int volatile barrier_consensus_mismatch[2] = { 0, 0 }; /*  non-zero if we detected a mismatch */
static int volatile barrier_count[2] = { 0, 0 }; /*  count of how many remotes have notified (on P0) */

static void gasnete_extref_barrier_notify_reqh(gasnet_token_t token, 
  gasnet_handlerarg_t phase, gasnet_handlerarg_t value, gasnet_handlerarg_t flags) {
  gasneti_assert(gasnete_mynode == GASNETE_BARRIER_MASTER);

  gasnet_hsl_lock(&barrier_lock);
  { int count = barrier_count[phase];
    if (flags == 0 && !barrier_consensus_value_present[phase]) {
      barrier_consensus_value[phase] = (int)value;
      barrier_consensus_value_present[phase] = 1;
    } else if (flags == GASNET_BARRIERFLAG_MISMATCH ||
               (flags == 0 && barrier_consensus_value[phase] != (int)value)) {
      barrier_consensus_mismatch[phase] = 1;
    }
    count++;
    if (count == gasnete_nodes) gasneti_memsync(); /* about to signal, ensure we flush state */
    barrier_count[phase] = count;
  }
  gasnet_hsl_unlock(&barrier_lock);
}

static void gasnete_extref_barrier_done_reqh(gasnet_token_t token, 
  gasnet_handlerarg_t phase,  gasnet_handlerarg_t mismatch) {
  gasneti_assert(phase == barrier_phase);

  barrier_response_mismatch[phase] = mismatch;
  gasneti_memsync();
  barrier_response_done[phase] = 1;
}

/*  make some progress on the barrier */
static void gasnete_extref_barrier_kick() {
  int phase = barrier_phase;
  GASNETE_SAFE(gasnet_AMPoll());

  if (gasnete_mynode != GASNETE_BARRIER_MASTER) return;

  /*  master does all the work */
  if (barrier_count[phase] == gasnete_nodes) {
    /*  barrier is complete */
    int i;
    int mismatch = barrier_consensus_mismatch[phase];

    /*  inform the nodes */
    for (i=0; i < gasnete_nodes; i++) {
      GASNETE_SAFE(
        gasnet_AMRequestShort2(i, gasneti_handleridx(gasnete_extref_barrier_done_reqh), 
                             phase, mismatch));
    }

    /*  reset state */
    barrier_count[phase] = 0;
    barrier_consensus_mismatch[phase] = 0;
    barrier_consensus_value_present[phase] = 0;
  }
}

void gasnete_extref_barrier_notify(int id, int flags) {
  int phase;
  if_pf(barrier_splitstate == INSIDE_BARRIER) 
    gasneti_fatalerror("gasnet_barrier_notify() called twice in a row");

  GASNETI_TRACE_PRINTF(B, ("BARRIER_NOTIFY(id=%i,flags=%i)", id, flags));
  #if GASNETI_STATS_OR_TRACE
    barrier_notifytime = GASNETI_STATTIME_NOW_IFENABLED(B);
  #endif

  barrier_value = id;
  barrier_flags = flags;
  phase = !barrier_phase; /*  enter new phase */
  barrier_phase = phase;

  if (gasnete_nodes > 1) {
    /*  send notify msg to 0 */
    GASNETE_SAFE(
      gasnet_AMRequestShort3(GASNETE_BARRIER_MASTER, gasneti_handleridx(gasnete_extref_barrier_notify_reqh), 
                           phase, barrier_value, flags));
  } else {
    barrier_response_mismatch[phase] = (flags & GASNET_BARRIERFLAG_MISMATCH);
    barrier_response_done[phase] = 1;
  }

  /*  update state */
  barrier_splitstate = INSIDE_BARRIER;
  gasneti_memsync(); /* ensure all state changes committed before return */
}


int gasnete_extref_barrier_wait(int id, int flags) {
  #if GASNETI_STATS_OR_TRACE
    gasneti_stattime_t wait_start = GASNETI_STATTIME_NOW_IFENABLED(B);
  #endif
  int phase = barrier_phase;
  if_pf(barrier_splitstate == OUTSIDE_BARRIER) 
    gasneti_fatalerror("gasnet_barrier_wait() called without a matching notify");

  GASNETI_TRACE_EVENT_TIME(B,BARRIER_NOTIFYWAIT,GASNETI_STATTIME_NOW()-barrier_notifytime);

  /*  wait for response */
  while (!barrier_response_done[phase]) {
    gasnete_extref_barrier_kick();
  }

  GASNETI_TRACE_EVENT_TIME(B,BARRIER_WAIT,GASNETI_STATTIME_NOW()-wait_start);

  /*  update state */
  barrier_splitstate = OUTSIDE_BARRIER;
  barrier_response_done[phase] = 0;
  gasneti_memsync(); /* ensure all state changes committed before return */
  if_pf(id != barrier_value || flags != barrier_flags || 
        barrier_response_mismatch[phase]) {
        barrier_response_mismatch[phase] = 0;
        return GASNET_ERR_BARRIER_MISMATCH;
  }
  else return GASNET_OK;
}

int gasnete_extref_barrier_try(int id, int flags) {
  if_pf(barrier_splitstate == OUTSIDE_BARRIER) 
    gasneti_fatalerror("gasnet_barrier_try() called without a matching notify");

  gasnete_extref_barrier_kick();

  if (barrier_response_done[barrier_phase]) {
    GASNETI_TRACE_EVENT_VAL(B,BARRIER_TRY,1);
    return gasnete_extref_barrier_wait(id, flags);
  }
  else {
    GASNETI_TRACE_EVENT_VAL(B,BARRIER_TRY,0);
    return GASNET_ERR_NOT_READY;
  }
}
/* ------------------------------------------------------------------------------------ */
/*
  Handlers:
  =========
*/
static gasnet_handlerentry_t const gasnete_ref_handlers[] = {
  /* ptr-width independent handlers */
  gasneti_handler_tableentry_no_bits(gasnete_extref_barrier_notify_reqh),
  gasneti_handler_tableentry_no_bits(gasnete_extref_barrier_done_reqh),

  /* ptr-width dependent handlers */
  gasneti_handler_tableentry_with_bits(gasnete_extref_get_reqh),
  gasneti_handler_tableentry_with_bits(gasnete_extref_get_reph),
  gasneti_handler_tableentry_with_bits(gasnete_extref_getlong_reqh),
  gasneti_handler_tableentry_with_bits(gasnete_extref_getlong_reph),
  gasneti_handler_tableentry_with_bits(gasnete_extref_put_reqh),
  gasneti_handler_tableentry_with_bits(gasnete_extref_putlong_reqh),
  gasneti_handler_tableentry_with_bits(gasnete_extref_memset_reqh),
  gasneti_handler_tableentry_with_bits(gasnete_extref_markdone_reph),

  { 0, NULL }
};

extern gasnet_handlerentry_t const *gasnete_get_extref_handlertable()
{
	return gasnete_ref_handlers;
}


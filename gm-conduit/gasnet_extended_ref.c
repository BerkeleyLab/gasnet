/* $Id: gasnet_extended_ref.c,v 1.11 2004/01/12 08:18:26 bonachea Exp $
 * $Date: 2004/01/12 08:18:26 $
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
  gasnete_iop_t * const iop = mythread->current_iop;
  if (nbytes <= GASNETE_GETPUT_MEDIUM_LONG_THRESHOLD) {
    iop->initiated_get_cnt++;
  
    GASNETE_SAFE(
      SHORT_REQ(4,7,(node, gasneti_handleridx(gasnete_extref_get_reqh), 
                   (gasnet_handlerarg_t)nbytes, PACK(dest), PACK(src), PACK(iop))));
    return;
  } else {
    int chunksz;
    gasnet_handler_t reqhandler;
    uint8_t *psrc = src;
    uint8_t *pdest = dest;
    #if GASNETE_USE_LONG_GETS
      /* TODO: optimize this check by caching segment upper-bound in gasnete_seginfo */
      gasneti_memcheck(gasnete_seginfo);
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
      iop->initiated_get_cnt++;
      if (nbytes > chunksz) {
        GASNETE_SAFE(
          SHORT_REQ(4,7,(node, reqhandler, 
                       (gasnet_handlerarg_t)chunksz, PACK(pdest), PACK(psrc), PACK(iop))));
        nbytes -= chunksz;
        psrc += chunksz;
        pdest += chunksz;
      } else {
        GASNETE_SAFE(
          SHORT_REQ(4,7,(node, reqhandler, 
                       (gasnet_handlerarg_t)nbytes, PACK(pdest), PACK(psrc), PACK(iop))));
        break;
      }
    }
    return;
  }
}

GASNET_INLINE_MODIFIER(gasnete_extref_put_nbi_inner)
void gasnete_extref_put_nbi_inner(gasnet_node_t node, void *dest, void *src, size_t nbytes, int isbulk GASNETE_THREAD_FARG) {
  gasnete_threaddata_t * const mythread = GASNETE_MYTHREAD;
  gasnete_iop_t * const iop = mythread->current_iop;

  if (nbytes <= GASNETE_GETPUT_MEDIUM_LONG_THRESHOLD) {
    iop->initiated_put_cnt++;

    GASNETE_SAFE(
      MEDIUM_REQ(2,4,(node, gasneti_handleridx(gasnete_extref_put_reqh),
                    src, nbytes,
                    PACK(dest), PACK(iop))));
    return;
  } else if (nbytes <= gasnet_AMMaxLongRequest()) {
    iop->initiated_put_cnt++;

    if (isbulk) {
      GASNETE_SAFE(
        LONGASYNC_REQ(1,2,(node, gasneti_handleridx(gasnete_extref_putlong_reqh),
                      src, nbytes, dest,
                      PACK(iop))));
    } else {
      GASNETE_SAFE(
        LONG_REQ(1,2,(node, gasneti_handleridx(gasnete_extref_putlong_reqh),
                      src, nbytes, dest,
                      PACK(iop))));
    }

    return;
  } else {
    int chunksz = gasnet_AMMaxLongRequest();
    uint8_t *psrc = src;
    uint8_t *pdest = dest;
    for (;;) {
      iop->initiated_put_cnt++;
      if (nbytes > chunksz) {
        if (isbulk) {
          GASNETE_SAFE(
            LONGASYNC_REQ(1,2,(node, gasneti_handleridx(gasnete_extref_putlong_reqh),
                          psrc, chunksz, pdest,
                          PACK(iop))));
        } else {
          GASNETE_SAFE(
            LONG_REQ(1,2,(node, gasneti_handleridx(gasnete_extref_putlong_reqh),
                          psrc, chunksz, pdest,
                          PACK(iop))));
        }
        nbytes -= chunksz;
        psrc += chunksz;
        pdest += chunksz;
      } else {
        if (isbulk) {
          GASNETE_SAFE(
            LONGASYNC_REQ(1,2,(node, gasneti_handleridx(gasnete_extref_putlong_reqh),
                          psrc, nbytes, pdest,
                          PACK(iop))));
        } else {
          GASNETE_SAFE(
            LONG_REQ(1,2,(node, gasneti_handleridx(gasnete_extref_putlong_reqh),
                          psrc, nbytes, pdest,
                          PACK(iop))));
        }
        break;
      }
    }
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
/* reference implementation of barrier */
#define GASNETI_GASNET_EXTENDED_REFBARRIER_C 1
#define gasnete_refbarrier_notify  gasnete_extref_barrier_notify
#define gasnete_refbarrier_wait    gasnete_extref_barrier_wait
#define gasnete_refbarrier_try     gasnete_extref_barrier_try
#include "gasnet_extended_refbarrier.c"
#undef GASNETI_GASNET_EXTENDED_REFBARRIER_C

/* ------------------------------------------------------------------------------------ */
/*
  Handlers:
  =========
*/
static gasnet_handlerentry_t const gasnete_ref_handlers[] = {
  GASNETE_REFBARRIER_HANDLERS(),

  /* ptr-width independent handlers */

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


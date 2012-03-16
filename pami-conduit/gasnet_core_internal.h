/*   $Source: /Users/kamil/work/gasnet-cvs2/gasnet/pami-conduit/gasnet_core_internal.h,v $
 *     $Date: 2012/03/16 21:28:43 $
 * $Revision: 1.1.2.6 $
 * Description: GASNet PAMI conduit header for internal definitions in Core API
 * Copyright 2012, Lawrence Berkeley National Laboratory
 * Terms of use are as specified in license.txt
 */

#ifndef _GASNET_CORE_INTERNAL_H
#define _GASNET_CORE_INTERNAL_H

#include <gasnet_internal.h>
#include <gasnet_handler.h>

#include <pami.h>

#define GASNETC_PAMI_CHECK(rc,msg) \
  if_pf ((rc) != PAMI_SUCCESS) \
    { gasneti_fatalerror("Unexpected error %d on node %i/%i %s",\
                         (rc), gasneti_mynode, gasneti_nodes, (msg)); }

/* ------------------------------------------------------------------------------------ */
/*  whether or not to use spin-locking for HSL's */
#define GASNETC_HSL_SPINLOCK 0

/* ------------------------------------------------------------------------------------ */
#define GASNETC_HANDLER_BASE  1 /* reserve 1-63 for the core API */
#define _hidx_gasnetc_auxseg_reqh             (GASNETC_HANDLER_BASE+0)
/* add new core API handlers here and to the bottom of gasnet_core.c */

/* ------------------------------------------------------------------------------------ */
/* handler table (recommended impl) */
#define GASNETC_MAX_NUMHANDLERS   256
extern gasneti_handler_fn_t gasnetc_handler[GASNETC_MAX_NUMHANDLERS];

/* ------------------------------------------------------------------------------------ */
/* AM category (recommended impl if supporting PSHM) */
typedef enum {
  gasnetc_Short=0,
  gasnetc_Medium=1,
  gasnetc_Long=2
} gasnetc_category_t;

/* ------------------------------------------------------------------------------------ */
/* Completion counters */

extern void gasnetc_cb_inc_uint(pami_context_t, void *, pami_result_t);
extern void gasnetc_cb_inc_atomic(pami_context_t, void *, pami_result_t);
extern void gasnetc_cb_inc_release(pami_context_t, void *, pami_result_t);

/* spin-poll a simple (non-atomic) counter */
GASNETI_INLINE(gasnetc_wait_uint)
pami_result_t gasnetc_wait_uint(pami_context_t context,
                                volatile unsigned int *counter_p,
                                unsigned int goal) {
  while (*counter_p != goal) {
    pami_result_t rc = PAMI_Context_advance(context, 1);
    if_pf (rc != PAMI_SUCCESS) return rc;
  }
  return PAMI_SUCCESS;
}

/* spin-poll an atomic counter */
GASNETI_INLINE(gasnetc_wait_atomic)
pami_result_t gasnetc_wait_atomic(pami_context_t context,
                                  gasneti_weakatomic_t *counter_p,
                                  gasneti_weakatomic_val_t goal) {
  while (gasneti_weakatomic_read(counter_p, 0) != goal) {
    pami_result_t rc = PAMI_Context_advance(context, 1);
    if_pf (rc != PAMI_SUCCESS) return rc;
  }
  return PAMI_SUCCESS;
}

#endif

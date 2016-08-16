/*   $Source: bitbucket.org:berkeleylab/gasnet.git/ibv-conduit/gasnet_core_internal.h $
 * Description: GASNet ibv conduit header for internal defns common to Core & Extended APIs
 * Copyright 2016, The Regents of the University of California
 * Terms of use are as specified in license.txt
 */

#ifndef _GASNET_IBV_H
#define _GASNET_IBV_H

#include <gasnet_handle_internal.h>

/* ------------------------------------------------------------------------------------ *
 *  Common op completion logic
 * ------------------------------------------------------------------------------------ */

typedef enum {
  gasnetc_comptype_eop_alc,
  gasnetc_comptype_eop_get,
  gasnetc_comptype_eop_put,
  gasnetc_comptype_iop_alc,
  gasnetc_comptype_iop_get,
  gasnetc_comptype_iop_put,
} gasnetc_comptype_t;

GASNETI_ALWAYS_INLINE(gasnetc_complete_inner)
void gasnetc_complete_inner(gasnete_op_t *op, gasnetc_comptype_t type)
{
  switch (type) {
    case gasnetc_comptype_eop_alc:
    case gasnetc_comptype_iop_alc:
      GASNETE_EOP_LC_FINISH(op);
      break;
    default:
      GASNETE_EOP_MARKDONE(op);
      break;
  }
}

GASNETI_ALWAYS_INLINE(gasnetc_complete_eop)
int gasnetc_complete_eop(gasnete_eop_t *eop, gasnetc_comptype_t type)
{ // Advance and test the proper counter
  gasnete_op_t *op = (gasnete_op_t*)eop;
  gasnetc_atomic_val_t completed;
  gasnetc_atomic_val_t initiated;

  switch (type) {
    case gasnetc_comptype_eop_alc:
      completed = gasnetc_atomic_add(&eop->completed_alc, 1, GASNETI_ATOMIC_ACQ);
      initiated = eop->initiated_alc;
      break;
    case gasnetc_comptype_eop_put:
      completed = gasnetc_atomic_add(&eop->completed_cnt, 1, GASNETI_ATOMIC_ACQ);
      initiated = eop->initiated_cnt;
      break;
    case gasnetc_comptype_eop_get:
      completed = gasnetc_atomic_add(&eop->completed_cnt, 1, GASNETI_ATOMIC_ACQ | GASNETI_ATOMIC_REL);
      initiated = eop->initiated_cnt;
      break;
  #if GASNET_DEBUG
    default:
      gasneti_fatalerror("Unreachable switch case");
  #endif
  }

  if (completed == (initiated & GASNETI_ATOMIC_MAX)) {
    gasnetc_complete_inner(op, type);
    return 1;
  }
  return 0;
}

GASNETI_ALWAYS_INLINE(gasnetc_complete_iop)
int gasnetc_complete_iop(gasnete_iop_t *iop, gasnetc_comptype_t type)
{ // Advance and test the proper counter
  gasnete_op_t *op = (gasnete_op_t*)iop;
#if 0 // TODO-EX: will iops use the event bits?
  gasnetc_atomic_val_t initiated;
  gasnetc_atomic_val_t completed;

  switch (type) {
    case gasnetc_comptype_iop_alc:
      completed = gasnetc_atomic_add(&iop->completed_alc_cnt, 1, GASNETI_ATOMIC_ACQ);
      initiated = iop->initiated_alc_cnt;
      break;
    case gasnetc_comptype_iop_put:
      completed = gasnetc_atomic_add(&iop->completed_put_cnt, 1, GASNETI_ATOMIC_ACQ);
      initiated = iop->initiated_put_cnt;
      break;
    case gasnetc_comptype_iop_get:
      completed = gasnetc_atomic_add(&iop->completed_get_cnt, 1, GASNETI_ATOMIC_ACQ | GASNETI_ATOMIC_REL);
      initiated = iop->initiated_get_cnt;
      break;
  #if GASNET_DEBUG
    default:
      gasneti_fatalerror("Unreachable switch case");
  #endif
  }

  if (completed == (initiated & GASNETI_ATOMIC_MAX)) {
    gasnetc_complete_inner(op, type);
    return 1;
  }
#else
  switch (type) {
    case gasnetc_comptype_iop_alc:
      gasnetc_atomic_increment(&iop->completed_alc_cnt, GASNETI_ATOMIC_NONE);
      break;
    case gasnetc_comptype_iop_put:
      gasnetc_atomic_increment(&iop->completed_put_cnt, GASNETI_ATOMIC_NONE);
      break;
    case gasnetc_comptype_iop_get:
      gasnetc_atomic_increment(&iop->completed_get_cnt, GASNETI_ATOMIC_REL);
      break;
  #if GASNET_DEBUG
    default:
      gasneti_fatalerror("Unreachable switch case");
  #endif
  }
#endif
  return 0;
}

#endif

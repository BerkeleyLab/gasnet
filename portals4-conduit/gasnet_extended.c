/*   $Source: bitbucket.org:berkeleylab/gasnet.git/portals4-conduit/gasnet_extended.c $
 * Description: GASNet Extended API Reference Implementation
 * Copyright 2002, Dan Bonachea <bonachea@cs.berkeley.edu>
 * Terms of use are as specified in license.txt
 */

#include <gasnet_internal.h>
#include <gasnet_extended_internal.h>
#include <gasnet_portals4.h>

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
  gasnete_check_config_amref();
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
    #if !GASNETI_DISABLE_REFERENCE_EOP
      /* cause the first pool of eops to be allocated (optimization) */
      gasnete_eop_t *eop = gasnete_eop_new(threaddata);
      GASNETE_EOP_MARKDONE(eop);
      gasnete_eop_free(eop);
    #endif
  }

  /* Initialize barrier resources */
  gasnete_barrier_init();

  /* Initialize VIS subsystem */
  gasnete_vis_init();
}

/* ------------------------------------------------------------------------------------ */
/*
  Get/Put:
  ========
*/

/* Use some or all of the reference implementation of get/put in terms of AMs
 * Configuration appears in gasnet_extended_fwd.h
 */
#include "gasnet_extended_amref.c"

/* ------------------------------------------------------------------------------------ */
/*
  Non-blocking memory-to-memory transfers (explicit handle)
  ==========================================================
*/
/* ------------------------------------------------------------------------------------ */

/* Conduits not using the gasnete_amref_ versions should implement at least the following:
     gasnete_get_nb
     gasnete_put_nb
*/

gasnet_handle_t
gasnete_get_nb_bulk(void *dest, gasnet_node_t node, void *src, size_t nbytes GASNETE_THREAD_FARG)
{
    GASNETI_CHECKPSHM_GET(UNALIGNED,H);
    {
        gasnete_eop_t *op = gasnete_eop_new(GASNETE_MYTHREAD);
        gasnetc_rdma_get(dest, node, src, nbytes, OP_TYPE_EOP, (gasnet_handle_t) op);
        return (gasnet_handle_t)op;
    }
}


gasnet_handle_t
gasnete_put_nb(gasnet_node_t node, void *dest, void *src, size_t nbytes GASNETE_THREAD_FARG)
{
    GASNETI_CHECKPSHM_PUT(UNALIGNED,H);
    {
        gasnete_eop_t *op = gasnete_eop_new(GASNETE_MYTHREAD);
        gasnetc_rdma_put(node, dest, src, nbytes, OP_TYPE_EOP, (gasnet_handle_t) op);
        gasnetc_rdma_put_wait((gasnet_handle_t) op);
        return (gasnet_handle_t)op;
    }
}


gasnet_handle_t
gasnete_put_nb_bulk(gasnet_node_t node, void *dest, void *src, size_t nbytes GASNETE_THREAD_FARG)
{
    GASNETI_CHECKPSHM_PUT(UNALIGNED,H);
    {
        gasnete_eop_t *op = gasnete_eop_new(GASNETE_MYTHREAD);
        gasnetc_rdma_put(node, dest, src, nbytes, OP_TYPE_EOP, (gasnet_handle_t) op);
        return (gasnet_handle_t)op;
    }
}

/* ------------------------------------------------------------------------------------ */
/*
  Non-blocking memory-to-memory transfers (implicit handle)
  ==========================================================
*/
/* ------------------------------------------------------------------------------------ */

/* Conduits not using the gasnete_amref_ versions should implement at least the following:
     gasnete_get_nbi
     gasnete_put_nbi
*/

void
gasnete_get_nbi_bulk(void *dest, gasnet_node_t node, void *src, size_t nbytes GASNETE_THREAD_FARG)
{
    GASNETI_CHECKPSHM_GET(UNALIGNED,V);
    {
        gasnete_threaddata_t * const mythread = GASNETE_MYTHREAD;
        gasnete_iop_t *op = mythread->current_iop;
        op->initiated_get_cnt++;
        gasnetc_rdma_get(dest, node, src, nbytes, OP_TYPE_IOP, (gasnet_handle_t) op);
    }
}


void
gasnete_put_nbi(gasnet_node_t node, void *dest, void *src, size_t nbytes GASNETE_THREAD_FARG)
{
    GASNETI_CHECKPSHM_PUT(ALIGNED,V);
    {
        gasnete_threaddata_t * const mythread = GASNETE_MYTHREAD;
        gasnete_iop_t *op = mythread->current_iop;
        op->initiated_put_cnt++;
        gasnetc_rdma_put(node, dest, src, nbytes, OP_TYPE_IOP, (gasnet_handle_t) op);
        gasnetc_rdma_put_wait((gasnet_handle_t) op);
    }
}


void
gasnete_put_nbi_bulk(gasnet_node_t node, void *dest, void *src, size_t nbytes GASNETE_THREAD_FARG)
{
    GASNETI_CHECKPSHM_PUT(ALIGNED,V);
    {
        gasnete_threaddata_t * const mythread = GASNETE_MYTHREAD;
        gasnete_iop_t *op = mythread->current_iop;
        op->initiated_put_cnt++;
        gasnetc_rdma_put(node, dest, src, nbytes, OP_TYPE_IOP, (gasnet_handle_t) op);
    }
}

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
#if GASNETE_BUILD_AMREF_GET_HANDLERS
  gasneti_handler_tableentry_with_bits(gasnete_amref_get_reqh),
  gasneti_handler_tableentry_with_bits(gasnete_amref_get_reph),
  gasneti_handler_tableentry_with_bits(gasnete_amref_getlong_reqh),
  gasneti_handler_tableentry_with_bits(gasnete_amref_getlong_reph),
#endif
#if GASNETE_BUILD_AMREF_PUT_HANDLERS
  gasneti_handler_tableentry_with_bits(gasnete_amref_put_reqh),
  gasneti_handler_tableentry_with_bits(gasnete_amref_putlong_reqh),
  gasneti_handler_tableentry_with_bits(gasnete_amref_markdone_reph),
#endif

  { 0, NULL }
};

extern gasnet_handlerentry_t const *gasnete_get_handlertable(void) {
  return gasnete_handlers;
}
/* ------------------------------------------------------------------------------------ */


/*   $Source: bitbucket.org:berkeleylab/gasnet.git/psm-conduit/gasnet_extended.c $
 * Description: GASNet Extended API Reference Implementation
 * Copyright (c) 2013-2015 Intel Corporation. All rights reserved.
 * Copyright 2002, Dan Bonachea <bonachea@cs.berkeley.edu>
 * Terms of use are as specified in license.txt
 */

#include <gasnet_internal.h>
#include <gasnet_core_internal.h>
#include <gasnet_core_list.h>
#include <gasnet_extended_internal.h>

#include <psm2.h>
#include <psm2_am.h>

void gasnete_put_long(gasnet_node_t node, void *dest, void *src,
        size_t nbytes, gasnet_handle_t op GASNETE_THREAD_FARG);
void gasnete_get_long (void *dest, gasnet_node_t node, void *src,
        size_t nbytes, gasnet_handle_t op GASNETE_THREAD_FARG);


/* ------------------------------------------------------------------------------------ */
/*
  Tuning Parameters
  =================
  Conduits may choose to override the default tuning parameters below by defining them
  in their gasnet_core_fwd.h
*/

#define GASNETE_GETREQS_INCR 256

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
GASNETI_COLD
static void gasnete_check_config(void) {
  gasneti_check_config_postattach();
}

GASNETI_COLD
extern void gasnete_init(void) {
  GASNETI_UNUSED_UNLESS_DEBUG
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

  /* Initialize psm2-specific stuff */
  gasnetc_list_init(&gasnetc_psm_state.getreqs, 0, 0);
  gasnetc_psm_state.getreq_slab = NULL;
  gasnetc_psm_state.getreq_alloc = 0;
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

/* -------------------------------------------------------------------------- */

/* Internal psm2/AM message handlers */

/* Completion handler used by PUT routines to mark an op as done. */
static void gasnete_complete_markdone_put(void* op)
{
    PSM_MARK_DONE(op, 0);
}

/* Service a put request: write provided data to memory */
int gasnete_handler_put(psm2_am_token_t token,
    psm2_amarg_t* args, int nargs, void* addr, uint32_t len)
{
    void* dest_addr;

    gasneti_assert(nargs == 1);

    dest_addr = (void*)args[0].u64w0;

    GASNETE_FAST_UNALIGNED_MEMCPY(dest_addr, addr, len);

    return 0;
}

/* Service a get request: send data back to the initiator */

/* Instead of passing a destination address and op handle to the target and
   back, pass an integer offset value into an array of get request objects
   (getreqs).  The GET reply handler can use the integer offset to look up
   the destination address and op handle stored by the original get request.

   Passing <= 2 AM arguments allows messages to fit entirely in the psm2 packet
   header, resulting in lower latency.  Thus this is an important optimization.

   A slab, or array of get requests is maintained.  The integer offset value
   passed to the target and back is computed by subtracting the slab's base
   address from the get request's address.

   Since offsets are sent across the wire, the slab can be dynamically grown
   without affecting get requests that are in flight.  The slab's base address
   may change, but using realloc ensures any get request's offset into the slab
   remains the same.  Another benefit is that fewer bits are needed for an
   offset (32 or less) compared to a full address (64).

   Free get requests are tracked in a LIFO linked list.
 */

typedef struct _gasnete_getreq {
    union {
        gasnetc_item_t item;
        void *dest_addr;
    };
    void *op;
} gasnete_getreq_t;

static void gasnete_get_getreq_inner(void)
{
    /* Realloc the slab and add the new elements to the free list. */
    gasnetc_list_t *list = &gasnetc_psm_state.getreqs;
    gasnete_getreq_t *slab = gasnetc_psm_state.getreq_slab;
    int alloc_len = gasnetc_psm_state.getreq_alloc + GASNETE_GETREQS_INCR;
    int i;

    slab = gasneti_realloc(slab, alloc_len * sizeof(gasnete_getreq_t));
    gasneti_leak(slab);

    gasnetc_psm_state.getreq_slab = slab;
    gasnetc_psm_state.getreq_alloc = alloc_len;

    for (i = alloc_len - GASNETE_GETREQS_INCR; i < alloc_len - 1; i++)
        slab[i].item.next = (gasnetc_item_t *)&slab[i + 1];

    slab[alloc_len - 1].item.next = NULL;
    list->head.next = (gasnetc_item_t *)
        &slab[alloc_len - GASNETE_GETREQS_INCR];
    /* List tail is unused. */
}

static gasnete_getreq_t *gasnete_get_getreq(void)
{
    gasnetc_list_t *list = &gasnetc_psm_state.getreqs;
    gasnetc_item_t *item;

    gasneti_spinlock_lock(&list->lock);
    if_pf (list->head.next == NULL)
        gasnete_get_getreq_inner();

    item = list->head.next;
    list->head.next = item->next;
    /* List tail is unused. */
    gasneti_spinlock_unlock(&list->lock);

    return (gasnete_getreq_t *)item;
}

static void gasnete_put_getreq(gasnete_getreq_t *req)
{
    gasnetc_list_add_head(&gasnetc_psm_state.getreqs,
            (gasnetc_item_t *)req);
}

GASNETI_INLINE(gasnete_getreq_to_offset)
uint32_t gasnete_getreq_to_offset(gasnete_getreq_t *req)
{
    return (uintptr_t)req - (uintptr_t)gasnetc_psm_state.getreq_slab;
}

GASNETI_INLINE(gasnete_offset_to_getreq)
gasnete_getreq_t *gasnete_offset_to_getreq(uint32_t offset)
{
    return (gasnete_getreq_t *)
        ((uintptr_t)gasnetc_psm_state.getreq_slab + offset);
}

/* Service a get request: send data back to the initiator */
int gasnete_handler_get_request(psm2_am_token_t token,
    psm2_amarg_t* args, int nargs, void* addr, uint32_t len)
{
    psm2_error_t ret;

    /* args[0] is the target's source address
       args[1].u32w0 is the initiator's packed getreq handle (return it back)
       args[1].u32w1 is the data length (one MTU max) */
    gasneti_assert(nargs == 2);
    gasneti_assert(args[1].u32w1 <= gasnetc_psm_max_reply_len);

    /* Return the second argument as-is back to the initiator. */
    ret = psm2_am_reply_short(token,
            gasnetc_psm_state.am_handlers[AM_HANDLER_GET_REPLY],
            &args[1], 1, (void *)args[0].u64w0, args[1].u32w1,
            PSM2_AM_FLAG_NONE, NULL, NULL);
    if(ret != PSM2_OK) {
        gasneti_fatalerror("psm2_am_reply_short failure: %s\n",
                psm2_error_get_string(ret));
    }

    return 0;
}

/* Complete a get request: receive data from source */
int gasnete_handler_get_reply(psm2_am_token_t token,
    psm2_amarg_t* args, int nargs, void* addr, uint32_t len)
{
    gasnete_getreq_t *req;


    /* The only argument is an offset to a getreq */
    gasneti_assert(nargs == 1);
    req = gasnete_offset_to_getreq(args[0].u32w0);

    GASNETE_FAST_UNALIGNED_MEMCPY(req->dest_addr, addr, len);

    if(req->op != NULL)
    PSM_MARK_DONE(req->op, 1);

    gasnete_put_getreq(req);
    return 0;
}


/* -------------------------------------------------------------------------- */
/*
  Non-blocking memory-to-memory transfers (implicit handle)
  ==========================================================
  each message sends an ack - we count the number of implicit ops launched and
  compare with the number acknowledged Another possible design would be to
  eliminate some of the acks (at least for puts) by piggybacking them on other
  messages (like get replies) or simply aggregating them the target until the
  source tries to synchronize
*/

static void gasnete_put_nbi_inner (gasnet_node_t node, void *dest, void *src,
                             size_t nbytes GASNETE_THREAD_FARG)
{
    gasnete_threaddata_t * const mythread = GASNETE_MYTHREAD;
    gasnete_iop_t * const op = mythread->current_iop;

    size_t mtu_size = gasnetc_psm_max_request_len;
    size_t bytes_remaining = nbytes;
    uintptr_t src_addr = (uintptr_t)src;
    uintptr_t dest_addr = (uintptr_t)dest;
    psm2_epaddr_t epaddr = gasnetc_psm_state.peer_epaddrs[node];
    psm2_handler_t handler = gasnetc_psm_state.am_handlers[AM_HANDLER_PUT];
    psm2_error_t ret;

    gasneti_assert(node < gasneti_nodes);

    if(nbytes >= gasnetc_psm_state.long_msg_threshold) {
            op->initiated_put_cnt++;
        gasnete_put_long(node, dest, src, nbytes,
                (gasnet_handle_t)op GASNETE_THREAD_PASS);
        return;
    }

    GASNETC_PSM_LOCK();
    while(bytes_remaining > mtu_size) {
        ret = psm2_am_request_short(epaddr, handler,
                (psm2_amarg_t*)&dest_addr, 1, (void*)src_addr, mtu_size,
                PSM2_AM_FLAG_NOREPLY, NULL, NULL);
        if_pf (ret != PSM2_OK) {
            gasneti_fatalerror("psm2_am_request_short failure: %s\n",
                psm2_error_get_string(ret));
        }

        src_addr += mtu_size;
        dest_addr += mtu_size;
        bytes_remaining -= mtu_size;
    }

    ret = psm2_am_request_short(epaddr, handler,
            (psm2_amarg_t*)&dest_addr, 1, (void*)src_addr, bytes_remaining,
            PSM2_AM_FLAG_NOREPLY,
            gasnete_complete_markdone_put, PSM_PACK_IOP_DONE(op,put));
    GASNETC_PSM_UNLOCK();
    if_pf (ret != PSM2_OK) {
        gasneti_fatalerror("psm2_am_request_short failure: %s\n",
                psm2_error_get_string(ret));
    }

    op->initiated_put_cnt++;
    gasnetc_psm_poll_periodic();
}

extern void gasnete_put_nbi (gasnet_node_t node, void *dest, void *src,
                             size_t nbytes GASNETE_THREAD_FARG)
{
    GASNETI_CHECKPSHM_PUT(ALIGNED,V);
    gasnete_put_nbi_inner(node, dest, src, nbytes GASNETE_THREAD_PASS);
}

extern void gasnete_put_nbi_bulk (gasnet_node_t node, void *dest, void *src,
                              size_t nbytes GASNETE_THREAD_FARG) {
    GASNETI_CHECKPSHM_PUT(UNALIGNED,V);
    gasnete_put_nbi_inner(node, dest, src, nbytes GASNETE_THREAD_PASS);
}

extern void gasnete_get_nbi_bulk (void *dest, gasnet_node_t node, void *src, size_t nbytes GASNETE_THREAD_FARG) {
    gasnete_threaddata_t * const mythread = GASNETE_MYTHREAD;
    gasnete_iop_t * const op = mythread->current_iop;
    uintptr_t src_addr = (uintptr_t)src;
    uintptr_t dest_addr = (uintptr_t)dest;
    size_t mtu_size = gasnetc_psm_max_reply_len;
    size_t bytes_remaining = nbytes;
    psm2_amarg_t args[2];
    gasnete_getreq_t* req;
    psm2_error_t ret;

    GASNETI_CHECKPSHM_GET(UNALIGNED,V);
    gasneti_assert(node < gasneti_nodes);

    if(nbytes >= gasnetc_psm_state.long_msg_threshold) {
        op->initiated_get_cnt++;
        gasnete_get_long(dest, node, src, nbytes,
                (gasnet_handle_t)op GASNETE_THREAD_PASS);
        return;
    }

    /* args[0] is the target's source address
       args[1].u32w0 is the initiator's packed getreq handle (return it back)
       args[1].u32w1 is the data length (one MTU max) */
    args[1].u32w1 = mtu_size;

    GASNETC_PSM_LOCK();
    while(bytes_remaining > mtu_size) {
        req = gasnete_get_getreq();
        req->dest_addr = (void *)dest_addr;
        req->op = NULL;

        args[0].u64w0 = src_addr;
        args[1].u32w0 = gasnete_getreq_to_offset(req);

        ret = psm2_am_request_short(gasnetc_psm_state.peer_epaddrs[node],
                gasnetc_psm_state.am_handlers[AM_HANDLER_GET_REQUEST],
                args, 2, NULL, 0, PSM2_AM_FLAG_NONE, NULL, NULL);
        if_pf (ret != PSM2_OK) {
            gasneti_fatalerror("psm2_am_request_short failure: %s\n",
                    psm2_error_get_string(ret));
        }

        src_addr += mtu_size;
        dest_addr += mtu_size;
        bytes_remaining -= mtu_size;
    }

    /* Request final MTU worth of payload transfer */
    req = gasnete_get_getreq();
    req->dest_addr = (void *)dest_addr;
    req->op = op;

    args[0].u64w0 = src_addr;
    args[1].u32w0 = gasnete_getreq_to_offset(req);
    args[1].u32w1 = bytes_remaining;

    ret = psm2_am_request_short(gasnetc_psm_state.peer_epaddrs[node],
            gasnetc_psm_state.am_handlers[AM_HANDLER_GET_REQUEST],
            args, 2, NULL, 0, PSM2_AM_FLAG_NONE, NULL, NULL);
    GASNETC_PSM_UNLOCK();
    if_pf (ret != PSM2_OK) {
        gasneti_fatalerror("psm2_am_request_short failure: %s\n",
                psm2_error_get_string(ret));
    }

    op->initiated_get_cnt++;
    gasnetc_psm_poll_periodic();
}


/*
  Non-blocking memory-to-memory transfers (explicit handle)
  ==========================================================
*/
/* -------------------------------------------------------------------------- */

/* Conduits not using the gasnete_amref_ versions should implement at least the following:
     gasnete_get_nb
     gasnete_put_nb
*/

extern gasnet_handle_t gasnete_put_nb_inner(gasnet_node_t node, void *dest,
                               void *src, size_t nbytes GASNETE_THREAD_FARG)
{
    gasnete_eop_t* op = gasnete_eop_new(GASNETE_MYTHREAD);
    size_t mtu_size = gasnetc_psm_max_request_len;
    size_t bytes_remaining = nbytes;
    uintptr_t src_addr = (uintptr_t)src;
    uintptr_t dest_addr = (uintptr_t)dest;
    psm2_error_t ret;

    gasneti_assert(node < gasneti_nodes);

    if(nbytes >= gasnetc_psm_state.long_msg_threshold) {
        gasnete_put_long(node, dest, src, nbytes,
                (gasnet_handle_t)op GASNETE_THREAD_PASS);
        return (gasnet_handle_t)op;
    }

    GASNETC_PSM_LOCK();
    while(bytes_remaining > mtu_size) {
        ret = psm2_am_request_short(gasnetc_psm_state.peer_epaddrs[node],
                gasnetc_psm_state.am_handlers[AM_HANDLER_PUT],
                (psm2_amarg_t*)&dest_addr, 1, (void*)src_addr, mtu_size,
                PSM2_AM_FLAG_NOREPLY, NULL, NULL);
        if_pf (ret != PSM2_OK) {
            gasneti_fatalerror("psm2_am_request_short failure: %s\n",
                    psm2_error_get_string(ret));
        }

        src_addr += mtu_size;
        dest_addr += mtu_size;
        bytes_remaining -= mtu_size;
    }

    ret = psm2_am_request_short(gasnetc_psm_state.peer_epaddrs[node],
            gasnetc_psm_state.am_handlers[AM_HANDLER_PUT],
            (psm2_amarg_t*)&dest_addr, 1, (void*)src_addr, bytes_remaining,
            PSM2_AM_FLAG_NONE,
            gasnete_complete_markdone_put, PSM_PACK_EOP_DONE(op));
    GASNETC_PSM_UNLOCK();
    if_pf (ret != PSM2_OK) {
        gasneti_fatalerror("psm2_am_request_short failure: %s\n",
                psm2_error_get_string(ret));
    }

    gasnetc_psm_poll_periodic();
    return (gasnet_handle_t)op;
}

extern gasnet_handle_t gasnete_put_nb(gasnet_node_t node, void *dest, void *src, size_t nbytes GASNETE_THREAD_FARG)
{
    GASNETI_CHECKPSHM_PUT(ALIGNED,H);
    return gasnete_put_nb_inner(node, dest, src, nbytes GASNETE_THREAD_PASS);
}

extern gasnet_handle_t gasnete_put_nb_bulk (gasnet_node_t node, void *dest, void *src, size_t nbytes GASNETE_THREAD_FARG) {
    GASNETI_CHECKPSHM_PUT(UNALIGNED,H);
    return gasnete_put_nb_inner(node, dest, src, nbytes GASNETE_THREAD_PASS);
}


extern gasnet_handle_t gasnete_get_nb_bulk (void *dest, gasnet_node_t node, void *src, size_t nbytes GASNETE_THREAD_FARG) {
    gasnete_eop_t* op;
    uintptr_t src_addr = (uintptr_t)src;
    uintptr_t dest_addr = (uintptr_t)dest;
    size_t mtu_size;
    size_t bytes_remaining = nbytes;
    psm2_amarg_t args[2];
    gasnete_getreq_t* req;
    psm2_error_t ret;

    GASNETI_CHECKPSHM_GET(UNALIGNED,H);
    gasneti_assert(node < gasneti_nodes);

    op = gasnete_eop_new(GASNETE_MYTHREAD);
    mtu_size = gasnetc_psm_max_reply_len;

    if(nbytes >= gasnetc_psm_state.long_msg_threshold) {
        gasnete_get_long(dest, node, src, nbytes,
                (gasnet_handle_t)op GASNETE_THREAD_PASS);
        return (gasnet_handle_t)op;
    }

    /* args[0] is the target's source address
       args[1].u32w0 is the initiator's packed getreq handle (return it back)
       args[1].u32w1 is the data length (one MTU max) */
    args[1].u32w1 = mtu_size;

    GASNETC_PSM_LOCK();
    while(bytes_remaining > mtu_size) {
        req = gasnete_get_getreq();
        req->dest_addr = (void *)dest_addr;
        req->op = NULL;

        args[0].u64w0 = src_addr;
        args[1].u32w0 = gasnete_getreq_to_offset(req);

        ret = psm2_am_request_short(gasnetc_psm_state.peer_epaddrs[node],
                gasnetc_psm_state.am_handlers[AM_HANDLER_GET_REQUEST],
                args, 2, NULL, 0, PSM2_AM_FLAG_NONE, NULL, NULL);
        if_pf (ret != PSM2_OK) {
            gasneti_fatalerror("psm2_am_request_short failure: %s\n",
                    psm2_error_get_string(ret));
        }

        src_addr += mtu_size;
        dest_addr += mtu_size;
        bytes_remaining -= mtu_size;
    }

    /* Request final MTU worth of payload transfer */
    req = gasnete_get_getreq();
    req->dest_addr = (void *)dest_addr;
    req->op = op;

    args[0].u64w0 = src_addr;
    args[1].u32w0 = gasnete_getreq_to_offset(req);
    args[1].u32w1 = bytes_remaining;

    ret = psm2_am_request_short(gasnetc_psm_state.peer_epaddrs[node],
            gasnetc_psm_state.am_handlers[AM_HANDLER_GET_REQUEST],
            args, 2, NULL, 0, PSM2_AM_FLAG_NONE, NULL, NULL);
    GASNETC_PSM_UNLOCK();
    if_pf (ret != PSM2_OK) {
        gasneti_fatalerror("psm2_am_request_short failure: %s\n",
                psm2_error_get_string(ret));
    }

    gasnetc_psm_poll_periodic();
    return (gasnet_handle_t)op;
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


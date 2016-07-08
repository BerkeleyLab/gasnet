/*
 * Description: GASNet Extended API Implementation
 * Copyright (c)  2002, Dan Bonachea <bonachea@cs.berkeley.edu>
 * Copyright (c)  2012, Mellanox Technologies LTD. All rights reserved.
 * Terms of use are as specified in license.txt
 */

#include <gasnet_internal.h>
#include <gasnet_mxm_req.h>
#include <gasnet_extended_internal.h>
#include <gasnet_handler.h>

/* -------------------------------------------------------------------------- */

#if MXM_API < MXM_VERSION(1,5)
extern uint32_t gasnetc_find_lkey(void *addr, int nbytes);
extern uint32_t gasnetc_find_rkey(void *addr, int nbytes, int rank);
#elif MXM_API == MXM_VERSION(1,5)
extern mxm_mem_h gasnetc_find_memh(void *addr, int nbytes);
extern mxm_mem_h gasnetc_find_remote_memh(void *addr, int nbytes, int rank);
#else
extern mxm_mem_key_t *gasnetc_find_remote_mkey(void *addr, int nbytes, int rank);
#endif

/* ------------------------------------------------------------------------------------ */
/*
  Tuning Parameters
  =================
*/

static int gasnete_mxm_max_outstanding_msgs;
#define GASNETE_MXM_MAX_OUTSTANDING_MSGS gasnete_mxm_max_outstanding_msgs
#define GASNETE_ACTIVE_MXM_GET_NUMBER (GASNETE_MYTHREAD->current_iop->initiated_get_cnt - gasneti_weakatomic_read(&(GASNETE_MYTHREAD->current_iop->completed_get_cnt),0))
#define GASNETE_ACTIVE_MXM_PUT_NUMBER (GASNETE_MYTHREAD->current_iop->initiated_put_cnt - gasneti_weakatomic_read(&(GASNETE_MYTHREAD->current_iop->completed_put_cnt),0))
#define GASNETE_ACTIVE_MXM_MSG_NUMBER (GASNETE_ACTIVE_MXM_GET_NUMBER + GASNETE_ACTIVE_MXM_PUT_NUMBER)


/* ------------------------------------------------------------------------------------ */
/*
  Common Code for gasnetex_handle_t
  =================================
  Factored bits of handle-management code common to most conduits, overridable when necessary
*/

#define GASNETE_OP_TRY_FREE_EXTRA(handle) do {                                       \
    if_pt (handle->flags == OPFLAG_MXM) {                                            \
            gasnet_mxm_send_req_t *send_req = (gasnet_mxm_send_req_t *)handle;       \
            if (mxm_req_test(&send_req->mxm_sreq.base)) {                            \
                gasneti_sync_reads();                                                \
                gasnetc_free_send_req(send_req);                                     \
                return 1;                                                            \
            }                                                                        \
    }                                                                                \
  } while (0)

#include "gasnet_handle.c"

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
    GASNETI_UNUSED_UNLESS_DEBUG
    static int firstcall = 1;
    GASNETI_TRACE_PRINTF(C,("gasnete_init()"));
    gasneti_assert(firstcall); /*  make sure we haven't been called before */
    firstcall = 0;

    gasnete_check_config(); /*  check for sanity */

    gasneti_assert(gasneti_nodes >= 1 && gasneti_mynode < gasneti_nodes);

    {   gasnete_threaddata_t *threaddata = NULL;
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

    gasnete_mxm_max_outstanding_msgs = gasneti_getenv_int_withdefault("GASNET_MXM_MAX_OUTSTANDING_MSGS", 500, 1);
    /* Initialize barrier resources */
    gasnete_barrier_init();

    /* Initialize VIS subsystem */
    gasnete_vis_init();
}

/* ------------------------------------------------------------------------------------ */
/* Make a gasnet_mxm_send_req_t act as a gasnete_op_t */

#define OPFLAG_MXM OPFLAG_CONDUIT0

GASNETI_INLINE(gasnete_sreq_to_handle) /* two call sites */
gasnet_handle_t gasnete_sreq_to_handle(gasnet_mxm_send_req_t *mop GASNETE_THREAD_FARG) {
    gasnete_threaddata_t * const mythread = GASNETE_MYTHREAD;
    mop->flags = OPFLAG_MXM;
    mop->threadidx = mythread->threadidx;
    return (gasnet_handle_t)mop;
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

typedef struct mxm_nbi_callback_struct {
    gasnet_mxm_send_req_t *gasnet_mxm_sreq_send;
    gasnete_iop_t *op;
} mxm_nbi_callback_struct_t;

GASNETI_INLINE(gasnete_rls_send_req)
void gasnete_rls_send_req(gasnet_mxm_send_req_t *sreq)
{
    if (gasneti_atomic_decrement_and_test(&sreq->ref_count, 0)) {
        gasnetc_free_send_req(sreq);
    }
}

static void mxm_nbi_put_callback(void *mxm_callback_data)
{
    mxm_nbi_callback_struct_t *cb = (mxm_nbi_callback_struct_t *)mxm_callback_data;

    gasnete_rls_send_req(cb->gasnet_mxm_sreq_send);
    gasnete_op_markdone((gasnete_op_t *)(cb->op), 0);
#if GASNET_DEBUG
    /* clear object's data before freeing it - might catch use after free */
    memset(cb, 0, sizeof(mxm_nbi_callback_struct_t));
#endif
    gasneti_free(cb);
}

static void mxm_nbi_get_callback(void *mxm_callback_data) {
    mxm_nbi_callback_struct_t *cb = (mxm_nbi_callback_struct_t *)mxm_callback_data;
    gasnetc_free_send_req(cb->gasnet_mxm_sreq_send);
    gasnete_op_markdone((gasnete_op_t *)(cb->op), 1);
#if GASNET_DEBUG
    /* clear object's data before freeing it - might catch use after free */
    memset(cb, 0, sizeof(mxm_nbi_callback_struct_t));
#endif
    gasneti_free(cb);
}

/* -------------------------------------------------------------------------- */

GASNETI_INLINE(gasnete_fill_get_request)
void gasnete_fill_get_request(mxm_send_req_t * mxm_sreq, void *dest,
                              gasnet_node_t node, void *src, size_t nbytes)
{
    mxm_sreq->base.state = MXM_REQ_NEW;
    mxm_sreq->base.conn = gasnet_mxm_module.connections[node];
    mxm_sreq->base.mq = gasnet_mxm_module.mxm_mq;
#if MXM_API < MXM_VERSION(2,0)
    mxm_sreq->base.flags = 0;
#else
    mxm_sreq->flags = 0;
#endif

    mxm_sreq->base.data_type = MXM_REQ_DATA_BUFFER;
    mxm_sreq->opcode = MXM_REQ_OP_GET;

    mxm_sreq->base.data.buffer.ptr = dest;
    mxm_sreq->base.data.buffer.length = nbytes;
    mxm_sreq->op.mem.remote_vaddr = (mxm_vaddr_t)src;

#if MXM_API < MXM_VERSION(1,5)
    mxm_sreq->base.data.buffer.mkey = gasnetc_find_lkey(dest, nbytes);
    mxm_sreq->op.mem.remote_mkey = gasnetc_find_rkey(src, nbytes, (int)node);
#elif MXM_API == MXM_VERSION(1,5)
    mxm_sreq->base.data.buffer.memh = gasnetc_find_memh(dest, nbytes);
    mxm_sreq->op.mem.remote_memh = gasnetc_find_remote_memh(src, nbytes, (int)node);
#else
    mxm_sreq->op.mem.remote_mkey = gasnetc_find_remote_mkey(src, nbytes, (int)node);
#endif

    mxm_sreq->base.completed_cb = NULL;
    mxm_sreq->base.context = NULL;
}

/* -------------------------------------------------------------------------- */
GASNETI_INLINE(gasnete_fill_put_request)
void gasnete_fill_put_request(mxm_send_req_t * mxm_sreq, void *dest,
                              gasnet_node_t node, void *src, size_t nbytes)
{
    mxm_sreq->base.state = MXM_REQ_NEW;
    mxm_sreq->base.conn = gasnet_mxm_module.connections[node];
    mxm_sreq->base.mq = gasnet_mxm_module.mxm_mq;

#if MXM_API < MXM_VERSION(2,0)
    mxm_sreq->opcode = MXM_REQ_OP_PUT;
    mxm_sreq->base.flags = MXM_REQ_FLAG_BLOCKING|MXM_REQ_FLAG_SEND_SYNC;
#else
    mxm_sreq->opcode = MXM_REQ_OP_PUT_SYNC;
    mxm_sreq->flags = MXM_REQ_SEND_FLAG_BLOCKING;
#endif
    mxm_sreq->base.data_type = MXM_REQ_DATA_BUFFER;

    mxm_sreq->base.data.buffer.ptr = src;
    mxm_sreq->base.data.buffer.length = nbytes;
    mxm_sreq->op.mem.remote_vaddr = (mxm_vaddr_t)dest;

#if MXM_API < MXM_VERSION(1,5)
    mxm_sreq->base.data.buffer.mkey = gasnetc_find_lkey(src, nbytes);
    mxm_sreq->op.mem.remote_mkey = gasnetc_find_rkey(dest, nbytes, (int)node);
#elif MXM_API == MXM_VERSION(1,5)
    mxm_sreq->base.data.buffer.memh = gasnetc_find_memh(src, nbytes);
    mxm_sreq->op.mem.remote_memh = gasnetc_find_remote_memh(dest, nbytes, (int)node);
#else
    mxm_sreq->op.mem.remote_mkey = gasnetc_find_remote_mkey(dest, nbytes, (int)node);
#endif

    mxm_sreq->base.completed_cb = NULL;
    mxm_sreq->base.context = NULL;
}

/* wait till source buffer is safe for reuse */
GASNETI_INLINE(gasneti_wait)
void gasneti_wait(gasnet_mxm_send_req_t *h)
{ 
    mxm_wait_t wait;

    wait.req = &h->mxm_sreq.base;
    wait.state = (mxm_req_state_t)(MXM_REQ_SENT | MXM_REQ_COMPLETED);
    wait.progress_cb = NULL;
    wait.progress_arg = NULL;
    mxm_wait(&wait);
}

/* -------------------------------------------------------------------------- */
/*
 * gasnet_get_nb
 * gasnet_get_nb_bulk
 *
 */

GASNETI_INLINE(gasnete_get_nb_inner)
gasnet_handle_t gasnete_get_nb_inner (void *dest, gasnet_node_t node,
                                      void *src, size_t nbytes GASNETE_THREAD_FARG)
{
    gasnet_mxm_send_req_t *gasnet_mxm_sreq = gasnetc_alloc_send_req();
    mxm_send_req_t *mxm_sreq = &gasnet_mxm_sreq->mxm_sreq;
    mxm_error_t mxm_res;

    gasnete_fill_get_request(mxm_sreq, dest, node, src, nbytes);

    mxm_res = mxm_req_send(mxm_sreq);
    if (mxm_res != MXM_OK)
        gasneti_fatalerror("Error posting send request - %s\n",
                           mxm_error_string(mxm_res));

    gasnetc_AMPoll();
    return gasnete_sreq_to_handle(gasnet_mxm_sreq GASNETE_THREAD_PASS);
}

/* -------------------------------------------------------------------------- */

#ifdef GASNETI_DIRECT_GET_NB
#if (GASNETI_DIRECT_GET_NB)

extern gasnet_handle_t gasnete_get_nb (void *dest, gasnet_node_t node,
                                       void *src, size_t nbytes GASNETE_THREAD_FARG)
{
    GASNETI_CHECKPSHM_GET(ALIGNED,H);
    return gasnete_get_nb_inner (dest, node, src, nbytes GASNETE_THREAD_PASS);
}

#endif /* if  (GASNETI_DIRECT_GET_NB) */
#endif /* ifdef GASNETI_DIRECT_GET_NB */

/* -------------------------------------------------------------------------- */

extern gasnet_handle_t gasnete_get_nb_bulk (void *dest, gasnet_node_t node,
        void *src, size_t nbytes GASNETE_THREAD_FARG)
{
    GASNETI_CHECKPSHM_GET(UNALIGNED,H);
    return gasnete_get_nb_inner (dest, node, src, nbytes GASNETE_THREAD_PASS);
}

/* -------------------------------------------------------------------------- */

GASNETI_INLINE(gasnete_put_nb_inner)
gasnet_handle_t gasnete_put_nb_inner(
    gasnet_node_t node, void *dest, void *src,
    size_t nbytes, const int isbulk GASNETE_THREAD_FARG)
{
    gasnet_mxm_send_req_t *gasnet_mxm_sreq = gasnetc_alloc_send_req();
    mxm_send_req_t *mxm_sreq = &gasnet_mxm_sreq->mxm_sreq;
    mxm_error_t mxm_res;

    gasnete_fill_put_request(mxm_sreq, dest, node, src, nbytes);

    mxm_res = mxm_req_send(mxm_sreq);
    if (mxm_res != MXM_OK)
        gasneti_fatalerror("Error posting send request - %s\n",
                           mxm_error_string(mxm_res));

    gasnetc_AMPoll();

    if (! isbulk) {
        gasneti_wait(gasnet_mxm_sreq);
    }

    return gasnete_sreq_to_handle(gasnet_mxm_sreq GASNETE_THREAD_PASS);
}

/* -------------------------------------------------------------------------- */
extern gasnet_handle_t gasnete_put_nb      (gasnet_node_t node, void *dest, void *src, size_t nbytes GASNETE_THREAD_FARG) {
    GASNETI_CHECKPSHM_PUT(ALIGNED,H);
    return gasnete_put_nb_inner(node, dest, src, nbytes, 0 GASNETE_THREAD_PASS);
}

/* -------------------------------------------------------------------------- */
extern gasnet_handle_t gasnete_put_nb_bulk (gasnet_node_t node, void *dest, void *src, size_t nbytes GASNETE_THREAD_FARG) {
    GASNETI_CHECKPSHM_PUT(UNALIGNED,H);
    return gasnete_put_nb_inner(node, dest, src, nbytes, 1 GASNETE_THREAD_PASS);
}
/* -------------------------------------------------------------------------- */

GASNETI_INLINE(gasnete_put_inner)
void gasnete_put_inner(gasnet_node_t node, void* dest, void *src,
                       size_t nbytes GASNETE_THREAD_FARG)
{
    mxm_send_req_t mxm_sreq;
    mxm_error_t mxm_res;

    gasnete_fill_put_request(&mxm_sreq, dest, node, src, nbytes);

    if (!gasnet_mxm_module.strict_api) {
#if MXM_API < MXM_VERSION(2,0)
        mxm_sreq.base.flags = MXM_REQ_FLAG_BLOCKING;
#else
        mxm_sreq.opcode = MXM_REQ_OP_PUT;
#endif
    }

    mxm_res = mxm_req_send(&mxm_sreq);
    if (mxm_res != MXM_OK)
        gasneti_fatalerror("Error posting send request - %s\n",
                           mxm_error_string(mxm_res));

    while (!mxm_req_test(&mxm_sreq.base))
        gasnetc_AMPoll();

    if (!gasnet_mxm_module.strict_api)
        gasnet_mxm_module.need_fence[node] = 1;
}

/* -------------------------------------------------------------------------- */

#ifdef GASNETI_DIRECT_PUT
#if (GASNETI_DIRECT_PUT)

extern void gasnete_put(gasnet_node_t node, void* dest, void *src,
                        size_t nbytes GASNETE_THREAD_FARG)
{
    GASNETI_CHECKPSHM_PUT(ALIGNED,V);
    gasnete_put_inner(node, dest, src, nbytes GASNETE_THREAD_PASS);
}

#endif /* if  (GASNETI_DIRECT_PUT) */
#endif /* ifdef GASNETI_DIRECT_PUT */

/* -------------------------------------------------------------------------- */

#ifdef GASNETI_DIRECT_PUT_BULK
#if (GASNETI_DIRECT_PUT_BULK)

extern void gasnete_put_bulk(gasnet_node_t node, void* dest, void *src,
                             size_t nbytes GASNETE_THREAD_FARG)
{
    GASNETI_CHECKPSHM_PUT(UNALIGNED,V);
    gasnete_put_inner(node, dest, src, nbytes GASNETE_THREAD_PASS);
}

#endif /* if  (GASNETI_DIRECT_PUT_BULK) */
#endif /* ifdef GASNETI_DIRECT_PUT_BULK */

/* -------------------------------------------------------------------------- */

GASNETI_INLINE(gasnete_get_inner)
void gasnete_get_inner(void *dest, gasnet_node_t node, void *src,
                       size_t nbytes GASNETE_THREAD_FARG)
{
    mxm_send_req_t mxm_sreq;
    mxm_error_t mxm_res;

    gasnete_fill_get_request(&mxm_sreq, dest, node, src, nbytes);
    mxm_res = mxm_req_send(&mxm_sreq);
    if (mxm_res != MXM_OK)
        gasneti_fatalerror("Error posting send request - %s\n",
                           mxm_error_string(mxm_res));

    while (!mxm_req_test(&mxm_sreq.base))
        gasnetc_AMPoll();
}

/* -------------------------------------------------------------------------- */

#ifdef GASNETI_DIRECT_GET
#if (GASNETI_DIRECT_GET)

extern void gasnete_get(void *dest, gasnet_node_t node, void *src,
                        size_t nbytes GASNETE_THREAD_FARG)
{
    GASNETI_CHECKPSHM_GET(ALIGNED,V);
    gasnete_get_inner(dest, node, src, nbytes GASNETE_THREAD_PASS);
}

#endif /* if  (GASNETI_DIRECT_GET) */
#endif /* ifdef GASNETI_DIRECT_GET */

/* -------------------------------------------------------------------------- */

#ifdef GASNETI_DIRECT_GET_BULK
#if (GASNETI_DIRECT_GET_BULK)

extern void gasnete_get_bulk(void *dest, gasnet_node_t node, void *src,
                             size_t nbytes GASNETE_THREAD_FARG)
{
    GASNETI_CHECKPSHM_GET(UNALIGNED,V);
    gasnete_get_inner(dest, node, src, nbytes GASNETE_THREAD_PASS);
}

#endif /* if  (GASNETI_DIRECT_GET_BULK) */
#endif /* ifdef GASNETI_DIRECT_GET_BULK */

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

/*
 * gasnet_get_nbi
 * gasnet_get_nbi_bulk
 *
 */

GASNETI_INLINE(gasnete_get_nbi_inner)
void gasnete_get_nbi_inner (void *dest, gasnet_node_t node, void *src,
                            size_t nbytes GASNETE_THREAD_FARG)
{
    gasnete_threaddata_t * const mythread = GASNETE_MYTHREAD;
    gasnete_iop_t * const op = mythread->current_iop;
    mxm_nbi_callback_struct_t *mxm_nbi_cb_data =
        (mxm_nbi_callback_struct_t *)gasneti_malloc(sizeof(mxm_nbi_callback_struct_t));

    gasnet_mxm_send_req_t *gasnet_mxm_sreq = gasnetc_alloc_send_req();
    mxm_send_req_t *mxm_sreq = &gasnet_mxm_sreq->mxm_sreq;
    mxm_error_t mxm_res;

    op->initiated_get_cnt++;
    mxm_nbi_cb_data->gasnet_mxm_sreq_send = gasnet_mxm_sreq;
    mxm_nbi_cb_data->op = op;

    gasnete_fill_get_request(mxm_sreq, dest, node, src, nbytes);
    mxm_sreq->base.completed_cb = mxm_nbi_get_callback;
    mxm_sreq->base.context = mxm_nbi_cb_data;

    if_pf (GASNETE_ACTIVE_MXM_MSG_NUMBER >= GASNETE_MXM_MAX_OUTSTANDING_MSGS) {
        do {
            gasnetc_AMPoll();
        } while (GASNETE_ACTIVE_MXM_MSG_NUMBER >= GASNETE_MXM_MAX_OUTSTANDING_MSGS);
    }
    mxm_res = mxm_req_send(mxm_sreq);
    if (mxm_res != MXM_OK)
        gasneti_fatalerror("Error posting send request - %s\n",
                           mxm_error_string(mxm_res));
    gasnetc_AMPoll();
}

/* -------------------------------------------------------------------------- */

#ifdef GASNETI_DIRECT_GET_NBI
#if (GASNETI_DIRECT_GET_NBI)

extern void gasnete_get_nbi (void *dest, gasnet_node_t node, void *src,
                             size_t nbytes GASNETE_THREAD_FARG)
{
    GASNETI_CHECKPSHM_GET(ALIGNED,V);
    gasnete_get_nbi_inner(dest, node, src, nbytes GASNETE_THREAD_PASS);
}

#endif /* if  (GASNETI_DIRECT_GET_NBI) */
#endif /* ifdef GASNETI_DIRECT_GET_NBI */

/* -------------------------------------------------------------------------- */

extern void gasnete_get_nbi_bulk (void *dest, gasnet_node_t node, void *src,
                                  size_t nbytes GASNETE_THREAD_FARG)
{
    GASNETI_CHECKPSHM_GET(UNALIGNED,V);
    gasnete_get_nbi_inner(dest, node, src, nbytes GASNETE_THREAD_PASS);
}

/* -------------------------------------------------------------------------- */
GASNETI_INLINE(gasnete_put_nbi_inner)
void gasnete_put_nbi_inner(gasnet_node_t node, void *dest, void *src,
                           size_t nbytes, const int isbulk GASNETE_THREAD_FARG)
{
    gasnete_threaddata_t * const mythread = GASNETE_MYTHREAD;
    gasnete_iop_t * const op = mythread->current_iop;
    mxm_nbi_callback_struct_t *mxm_nbi_cb_data =
        (mxm_nbi_callback_struct_t *)gasneti_malloc(sizeof(mxm_nbi_callback_struct_t));

    gasnet_mxm_send_req_t *gasnet_mxm_sreq = gasnetc_alloc_send_req();
    mxm_send_req_t *mxm_sreq = &gasnet_mxm_sreq->mxm_sreq;
    mxm_error_t mxm_res;

    op->initiated_put_cnt++;
    mxm_nbi_cb_data->gasnet_mxm_sreq_send = gasnet_mxm_sreq;
    mxm_nbi_cb_data->op = op;

    gasnete_fill_put_request(mxm_sreq, dest, node, src, nbytes);

    mxm_sreq->base.completed_cb = (void (*)(void *))mxm_nbi_put_callback;
    mxm_sreq->base.context = mxm_nbi_cb_data;

    gasneti_atomic_set(&gasnet_mxm_sreq->ref_count, (isbulk ? 1 : 2), 0);

    if_pf (GASNETE_ACTIVE_MXM_MSG_NUMBER >= GASNETE_MXM_MAX_OUTSTANDING_MSGS) {
        do {
            gasnetc_AMPoll();
        } while (GASNETE_ACTIVE_MXM_MSG_NUMBER >= GASNETE_MXM_MAX_OUTSTANDING_MSGS);
    }

    mxm_res = mxm_req_send(mxm_sreq);
    if (mxm_res != MXM_OK)
        gasneti_fatalerror("Error posting send request - %s\n",
                           mxm_error_string(mxm_res));

    gasnetc_AMPoll();

    if (! isbulk) {
       gasneti_wait(gasnet_mxm_sreq);
       gasnete_rls_send_req(gasnet_mxm_sreq);
    }
}

/* -------------------------------------------------------------------------- */

#ifdef GASNETI_DIRECT_PUT_NBI
#if (GASNETI_DIRECT_PUT_NBI)

extern void gasnete_put_nbi (gasnet_node_t node, void *dest, void *src,
                             size_t nbytes GASNETE_THREAD_FARG)
{
    GASNETI_CHECKPSHM_PUT(ALIGNED,V);
    gasnete_put_nbi_inner(node, dest, src, nbytes, 0 GASNETE_THREAD_PASS);
}

#endif /* if  (GASNETI_DIRECT_PUT_NBI) */
#endif /* ifdef GASNETI_DIRECT_PUT_NBI */

/* -------------------------------------------------------------------------- */

extern void gasnete_put_nbi_bulk (gasnet_node_t node, void *dest, void *src,
                                  size_t nbytes GASNETE_THREAD_FARG)
{
    GASNETI_CHECKPSHM_PUT(UNALIGNED,V);
    gasnete_put_nbi_inner(node, dest, src, nbytes, 1 GASNETE_THREAD_PASS);
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
#if GASNET_DEBUG
        if (iop->next != NULL)
            gasneti_fatalerror("VIOLATION: attempted to call gasnete_try_syncnbi_gets() inside an NBI access region");
#endif

        if (GASNETE_IOP_CNTDONE(iop,get)) {
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
#if GASNET_DEBUG
        if (iop->next != NULL)
            gasneti_fatalerror("VIOLATION: attempted to call gasnete_try_syncnbi_puts() inside an NBI access region");
#endif


        if (GASNETE_IOP_CNTDONE(iop,put)) {
            gasneti_sync_reads();
            return GASNET_OK;
        } else return GASNET_ERR_NOT_READY;
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


/*  $Archive:: gasnet/gasnet-conduit/gasnet_core_rcv.c                  $
 *     $Date: 2003/05/20 19:15:54 $
 * $Revision: 1.1.2.10 $
 * Description: GASNet vapi conduit implementation, receive side logic
 * Copyright 2003, LBNL
 * Terms of use are as specified in license.txt
 */

#include <gasnet.h>
#include <gasnet_internal.h>
#include <gasnet_handler.h>
#include <gasnet_core_internal.h>

#include <errno.h>
#include <unistd.h>
#include <signal.h>
#include <sched.h>

/* ------------------------------------------------------------------------------------ *
 *  Global variables                                                                    *
 * ------------------------------------------------------------------------------------ */
gasnetc_memreg_t                        gasnetc_rcv_reg;
VAPI_cq_hndl_t                          gasnetc_rcv_cq;

/* ------------------------------------------------------------------------------------ *
 *  File-scoped variables & types                                                       *
 * ------------------------------------------------------------------------------------ */
static gasnetc_rbuf_t			*gasnetc_rbuf_head;
static gasnetc_rbuf_t			*gasnetc_rbuf_tail;
static EVAPI_compl_handler_hndl_t	gasnetc_rcv_handler;

/* ------------------------------------------------------------------------------------ *
 *  File-scoped functions                                                               *
 * ------------------------------------------------------------------------------------ */


GASNET_INLINE_MODIFIER(gasnetc_rcv_post)
int gasnetc_rcv_post(gasnetc_rbuf_t *rbuf) {
  return (VAPI_OK != VAPI_post_rr(gasnetc_hca, rbuf->cep->qp_handle, &rbuf->rr_desc));
}

GASNET_INLINE_MODIFIER(gasnetc_processPacket)
void gasnetc_processPacket(gasnetc_rbuf_t *rbuf) {
  gasnetc_buffer_t *buf = (gasnetc_buffer_t *)(uintptr_t)(rbuf->rr_sg.addr);
  uint32_t flags = rbuf->flags;
  gasnet_handler_t handler_id = GASNETC_MSG_HANDLERID(flags);
  gasnetc_handler_fn_t handler_fn = gasnetc_handler[handler_id];
  gasnetc_category_t category = GASNETC_MSG_CATEGORY(flags);
  int numargs = GASNETC_MSG_NUMARGS(flags);
  gasnet_handlerarg_t *args;
  size_t nbytes;
  void *data;

  rbuf->replyIssued = 0;
  rbuf->handlerRunning = 1;
  switch (category) {
    case gasnetc_Short:
      { 
	args = buf->shortmsg.args;
        if (GASNETC_MSG_ISREQUEST(flags))
          GASNETI_TRACE_AMSHORT_REQHANDLER(handler_id, rbuf, numargs, args);
        else
          GASNETI_TRACE_AMSHORT_REPHANDLER(handler_id, rbuf, numargs, args);
        RUN_HANDLER_SHORT(handler_fn,rbuf,args,numargs);
      }
      break;

    case gasnetc_Medium:
      {
        nbytes = buf->medmsg.nBytes;
        data = GASNETC_MSG_MED_DATA(buf, numargs);
	args = buf->medmsg.args;

        if (GASNETC_MSG_ISREQUEST(flags))
          GASNETI_TRACE_AMMEDIUM_REQHANDLER(handler_id, rbuf, data, nbytes, numargs, args);
        else
          GASNETI_TRACE_AMMEDIUM_REPHANDLER(handler_id, rbuf, data, nbytes, numargs, args);
        RUN_HANDLER_MEDIUM(handler_fn,rbuf,args,numargs,data,nbytes);
      }
      break;

    case gasnetc_Long:
      { 
        nbytes = buf->longmsg.nBytes;
        data = (void *)(buf->longmsg.destLoc);
	args = buf->longmsg.args;
        if (GASNETC_MSG_ISREQUEST(flags))
          GASNETI_TRACE_AMLONG_REQHANDLER(handler_id, rbuf, data, nbytes, numargs, args);
        else
          GASNETI_TRACE_AMLONG_REPHANDLER(handler_id, rbuf, data, nbytes, numargs, args);
        RUN_HANDLER_LONG(handler_fn,rbuf,args,numargs,data,nbytes);
      }
      break;

    default:
      assert(0);
  }
  rbuf->handlerRunning = 0;
}

GASNET_INLINE_MODIFIER(gasnetc_rcv_reap)
void gasnetc_rcv_reap(int limit) {
  static pthread_mutex_t poll_lock = PTHREAD_MUTEX_INITIALIZER;
  VAPI_ret_t vstat;

  while (--limit) {
    VAPI_wc_desc_t comp;

    /* It seems that VAPI_poll_cq() is not thread-safe */
    pthread_mutex_lock(&poll_lock);
    vstat = VAPI_poll_cq(gasnetc_hca, gasnetc_rcv_cq, &comp);
    pthread_mutex_unlock(&poll_lock);

    if (vstat == VAPI_OK) {
      if (comp.status == VAPI_SUCCESS) {
        gasnetc_rbuf_t *rbuf = (gasnetc_rbuf_t *)(uintptr_t)comp.id;
        rbuf->flags = comp.imm_data; 
        gasnetc_processPacket(rbuf);
        gasnetc_rcv_post(rbuf);
      } else {
#if 1
        fprintf(stderr, "@ %d> rcv comp.status=%d\n", gasnetc_mynode, comp.status);
        while((vstat = VAPI_poll_cq(gasnetc_hca, gasnetc_snd_cq, &comp)) == VAPI_OK) {
          fprintf(stderr, "@ %d> - snd comp.status=%d\n", gasnetc_mynode, comp.status);
        }
#endif
        /* ### What needs to be done here? */
      }
    } else {
      assert(vstat == VAPI_CQ_EMPTY);
      break;
    }
  }
}

static void gasnetc_rcv_thread(VAPI_hca_hndl_t	hca_hndl,
			       VAPI_cq_hndl_t	cq_hndl,
			       void		*context) {
  VAPI_ret_t vstat;

  gasnetc_rcv_reap(0);

  vstat = VAPI_req_comp_notif(gasnetc_hca, gasnetc_rcv_cq, VAPI_NEXT_COMP);
  assert(vstat == VAPI_OK);

  gasnetc_rcv_reap(0);
}

/* ------------------------------------------------------------------------------------ *
 *  Externally visible functions                                                        *
 * ------------------------------------------------------------------------------------ */

extern void gasnetc_rcv_init(void) {
  VAPI_cqe_num_t	act_size;
  VAPI_ret_t		vstat;
  gasnetc_buffer_t	*buf;
  gasnetc_rbuf_t	*rbuf;
  int 			count, i;

  if (gasnetc_nodes == 1) {
    /* Don't even bother to allocate zero-byte regions */
    return;
  }

  count = GASNETC_RCV_WQE * (gasnetc_nodes - 1);

  buf = gasnetc_alloc_pinned(count * sizeof(gasnetc_buffer_t),
			     VAPI_EN_LOCAL_WRITE, &gasnetc_rcv_reg);
  assert(buf != NULL);

  rbuf = calloc(count, sizeof(gasnetc_rbuf_t));
  assert(rbuf != NULL);

  for (i = 0; i < count; ++i) {
    rbuf[i].rr_desc.id         = (uintptr_t)&rbuf[i];	/* CQE will point back to this request */
    rbuf[i].rr_desc.opcode     = VAPI_RECEIVE;
    rbuf[i].rr_desc.comp_type  = VAPI_SIGNALED;	/* XXX: is this right? */
    rbuf[i].rr_desc.sg_lst_len = 1;
    rbuf[i].rr_desc.sg_lst_p   = &rbuf[i].rr_sg;
    rbuf[i].rr_sg.len          = GASNETC_BUFSZ;
    rbuf[i].rr_sg.addr         = (uintptr_t)&buf[i];
    rbuf[i].rr_sg.lkey         = gasnetc_rcv_reg.lkey;
  }

  vstat = VAPI_create_cq(gasnetc_hca, count, &gasnetc_rcv_cq, &act_size);
  assert(vstat == VAPI_OK);
  assert(act_size >= count);

  vstat = EVAPI_set_comp_eventh(gasnetc_hca, gasnetc_rcv_cq, &gasnetc_rcv_thread,
				NULL, &gasnetc_rcv_handler);
  assert(vstat == VAPI_OK);
  vstat = VAPI_req_comp_notif(gasnetc_hca, gasnetc_rcv_cq, VAPI_NEXT_COMP);
  assert(vstat == VAPI_OK);

  gasnetc_rbuf_head = gasnetc_rbuf_tail = rbuf;
}

extern void gasnetc_rcv_init_cep(gasnetc_cep_t *cep) {
  int i, rc;
  
  for (i = 0; i < GASNETC_RCV_WQE; ++i) {
    gasnetc_rbuf_tail->cep = cep;
    rc = gasnetc_rcv_post(gasnetc_rbuf_tail);
    assert(rc == 0);

    gasnetc_rbuf_tail++;
    assert((gasnetc_rbuf_tail - gasnetc_rbuf_head) <= (GASNETC_RCV_WQE * (gasnetc_nodes - 1)));
  }
}

extern void gasnetc_rcv_fini(void) {
  VAPI_ret_t vstat;

  vstat = EVAPI_clear_comp_eventh(gasnetc_hca, gasnetc_rcv_handler);
  assert(vstat == VAPI_OK);

  vstat = VAPI_destroy_cq(gasnetc_hca, gasnetc_rcv_cq);
  assert(vstat == VAPI_OK);

  gasnetc_free_pinned(&gasnetc_rcv_reg);
  free(gasnetc_rbuf_head);
}

extern void gasnetc_rcv_poll(void) {
  gasnetc_rcv_reap(GASNETC_RCV_REAP_LIMIT);
}

extern void gasnetc_rcv_loopback(gasnetc_buffer_t *buffer, uint32_t flags) {
  gasnetc_rbuf_t	rbuf;

  rbuf.flags      = flags;
  rbuf.rr_sg.addr = (uintptr_t)buffer;

  gasnetc_processPacket(&rbuf);
}

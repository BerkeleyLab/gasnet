/*  $Archive:: gasnet/gasnet-conduit/gasnet_core_rcv.c                  $
 *     $Date: 2003/04/07 18:53:36 $
 * $Revision: 1.1.2.4 $
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
static gasnetc_rcv_desc_t		*gasnetc_rcv_desc_head;
static gasnetc_rcv_desc_t		*gasnetc_rcv_desc_tail;
static EVAPI_compl_handler_hndl_t	gasnetc_rcv_handler;

/* ------------------------------------------------------------------------------------ *
 *  File-scoped functions                                                               *
 * ------------------------------------------------------------------------------------ */


GASNET_INLINE_MODIFIER(gasnetc_rcv_post)
int gasnetc_rcv_post(gasnetc_rcv_desc_t *desc) {
  return (VAPI_OK != VAPI_post_rr(gasnetc_hca, desc->cep->qp_handle, &desc->rr_desc));
}

GASNET_INLINE_MODIFIER(gasnetc_processPacket)
void gasnetc_processPacket(gasnetc_rcv_desc_t *desc) {
  gasnetc_buffer_t *buf = (gasnetc_buffer_t *)(uintptr_t)(desc->rr_sg.addr);
  uint32_t flags = desc->flags;
  gasnet_handler_t handler_id = GASNETC_MSG_HANDLERID(flags);
  gasnetc_handler_fn_t handler_fn = gasnetc_handler[handler_id];
  gasnetc_category_t category = GASNETC_MSG_CATEGORY(flags);
  int numargs = GASNETC_MSG_NUMARGS(flags);
  gasnet_handlerarg_t *args;
  size_t nbytes;
  void *data;

  desc->replyIssued = 0;
  desc->handlerRunning = 1;
  switch (category) {
    case gasnetc_Short:
      { 
	args = buf->shortmsg.args;
        if (GASNETC_MSG_ISREQUEST(flags))
          GASNETI_TRACE_AMSHORT_REQHANDLER(handler_id, desc, numargs, args);
        else
          GASNETI_TRACE_AMSHORT_REPHANDLER(handler_id, desc, numargs, args);
        RUN_HANDLER_SHORT(handler_fn,desc,args,numargs);
      }
      break;

    case gasnetc_Medium:
      {
        nbytes = buf->medmsg.nBytes;
        data = GASNETC_MSG_MED_DATA(buf, numargs);
	args = buf->medmsg.args;

        if (GASNETC_MSG_ISREQUEST(flags))
          GASNETI_TRACE_AMMEDIUM_REQHANDLER(handler_id, desc, data, nbytes, numargs, args);
        else
          GASNETI_TRACE_AMMEDIUM_REPHANDLER(handler_id, desc, data, nbytes, numargs, args);
        RUN_HANDLER_MEDIUM(handler_fn,desc,args,numargs,data,nbytes);
      }
      break;

    case gasnetc_Long:
      { 
        nbytes = buf->longmsg.nBytes;
        data = (void *)(buf->longmsg.destLoc);
	args = buf->longmsg.args;
        if (GASNETC_MSG_ISREQUEST(flags))
          GASNETI_TRACE_AMLONG_REQHANDLER(handler_id, desc, data, nbytes, numargs, args);
        else
          GASNETI_TRACE_AMLONG_REPHANDLER(handler_id, desc, data, nbytes, numargs, args);
        RUN_HANDLER_LONG(handler_fn,desc,args,numargs,data,nbytes);
      }
      break;

    default:
      assert(0);
  }
  desc->handlerRunning = 0;
}

static void gasnetc_rcv_thread(VAPI_hca_hndl_t	hca_hndl,
			       VAPI_cq_hndl_t	cq_hndl,
			       void		*context) {
  VAPI_ret_t		vstat;
  VAPI_wc_desc_t	comp;
  gasnetc_rcv_desc_t	*desc;

  while (VAPI_OK == (vstat = VAPI_poll_cq(gasnetc_hca, gasnetc_rcv_cq, &comp))) {
    desc = (gasnetc_rcv_desc_t *)(uintptr_t)comp.id;
    desc->flags = comp.imm_data; 

    if (comp.status == VAPI_SUCCESS) {
      gasnetc_processPacket(desc);
      gasnetc_rcv_post(desc);
    } else {
#if 0
      fprintf(stderr, "@ %d> comp.status=%d\n", gasnetc_mynode, comp.status);
      while((vstat = VAPI_poll_cq(gasnetc_hca, gasnetc_snd_cq, &comp)) == VAPI_OK) {
        fprintf(stderr, "@ %d> snd comp.status=%d\n", gasnetc_mynode, comp.status);
      }
#endif
      /* ### What needs to be done here? */
    }
  }
  
  vstat = VAPI_req_comp_notif(gasnetc_hca, gasnetc_rcv_cq, VAPI_NEXT_COMP);
  assert(vstat == VAPI_OK);
}

/* ------------------------------------------------------------------------------------ *
 *  Externally visible functions                                                        *
 * ------------------------------------------------------------------------------------ */

extern void gasnetc_rcv_init(void) {
  VAPI_cqe_num_t	act_size;
  VAPI_ret_t		vstat;
  gasnetc_buffer_t	*buf;
  gasnetc_rcv_desc_t	*desc;
  int 			count, i;

  if (gasnetc_nodes == 1) {
    /* Don't even bother to allocate zero-byte regions */
    return;
  }

  count = GASNETC_RCV_WQE * (gasnetc_nodes - 1);

  buf = gasnetc_alloc_pinned(count * sizeof(gasnetc_buffer_t),
			     VAPI_EN_LOCAL_WRITE, &gasnetc_rcv_reg);
  assert(buf != NULL);

  desc = calloc(count, sizeof(gasnetc_rcv_desc_t));
  assert(desc != NULL);

  for (i = 0; i < count; ++i) {
    desc[i].rr_desc.id         = (uintptr_t)&desc[i];	/* CQE will point back to this request */
    desc[i].rr_desc.opcode     = VAPI_RECEIVE;
    desc[i].rr_desc.comp_type  = VAPI_SIGNALED;	/* XXX: is this right? */
    desc[i].rr_desc.sg_lst_len = 1;
    desc[i].rr_desc.sg_lst_p   = &desc[i].rr_sg;
    desc[i].rr_sg.len          = GASNETC_BUFSZ;
    desc[i].rr_sg.addr         = (uintptr_t)&buf[i];
    desc[i].rr_sg.lkey         = gasnetc_rcv_reg.lkey;
  }

  vstat = VAPI_create_cq(gasnetc_hca, count, &gasnetc_rcv_cq, &act_size);
  assert(vstat == VAPI_OK);
  assert(act_size >= count);

  vstat = EVAPI_set_comp_eventh(gasnetc_hca, gasnetc_rcv_cq, &gasnetc_rcv_thread,
				NULL, &gasnetc_rcv_handler);
  assert(vstat == VAPI_OK);
  vstat = VAPI_req_comp_notif(gasnetc_hca, gasnetc_rcv_cq, VAPI_NEXT_COMP);
  assert(vstat == VAPI_OK);

  gasnetc_rcv_desc_head = gasnetc_rcv_desc_tail = desc;
}

extern void gasnetc_rcv_init_cep(gasnetc_cep_t *cep) {
  int i, rc;
  
  for (i = 0; i < GASNETC_RCV_WQE; ++i) {
    gasnetc_rcv_desc_tail->cep = cep;
    rc = gasnetc_rcv_post(gasnetc_rcv_desc_tail);
    assert(rc == 0);

    gasnetc_rcv_desc_tail++;
    assert((gasnetc_rcv_desc_tail - gasnetc_rcv_desc_head) <= (GASNETC_RCV_WQE * (gasnetc_nodes - 1)));
  }
}

extern void gasnetc_rcv_loopback(gasnetc_snd_desc_t *snd_desc) {
  gasnetc_rcv_desc_t	rcv_desc;

  rcv_desc.flags      = snd_desc->sr_desc.imm_data;
  rcv_desc.rr_sg.addr = (uintptr_t)snd_desc->buffer;

  gasnetc_processPacket(&rcv_desc);
}

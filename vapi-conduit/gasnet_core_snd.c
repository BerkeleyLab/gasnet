/*  $Archive:: gasnet/gasnet-conduit/gasnet_core_snd.c                  $
 *     $Date: 2003/04/02 02:03:24 $
 * $Revision: 1.1.2.6 $
 * Description: GASNet vapi conduit implementation, send side logic
 * Copyright 2003, LBNL
 * Terms of use are as specified in license.txt
 */

#include <gasnet.h>
#include <gasnet_internal.h>
#include <gasnet_handler.h>
#include <gasnet_core_internal.h>

#include <errno.h>
#include <unistd.h>

/* ------------------------------------------------------------------------------------ *
 *  Global variables                                                                    *
 * ------------------------------------------------------------------------------------ */
gasnetc_memreg_t			gasnetc_snd_reg;
VAPI_cq_hndl_t				gasnetc_snd_cq;

/* ------------------------------------------------------------------------------------ *
 *  File-scoped variables & types                                                       *
 * ------------------------------------------------------------------------------------ */
static gasnetc_snd_desc_t		*gasnetc_snd_desc_pool;
static pthread_mutex_t			gasnetc_snd_desc_lock = PTHREAD_MUTEX_INITIALIZER;
static EVAPI_compl_handler_hndl_t	gasnetc_snd_handler;

/* ------------------------------------------------------------------------------------ *
 *  File-scoped functions                                                               *
 * ------------------------------------------------------------------------------------ */

/* free a list of send descriptor/buffer pairs */
GASNET_INLINE_MODIFIER(gasnetc_put_snd_desc)
void gasnetc_put_snd_desc(gasnetc_snd_desc_t *head, gasnetc_snd_desc_t *tail) {
  VAPI_ret_t vstat;

  /* Add the list segment to the free list */
  pthread_mutex_lock(&gasnetc_snd_desc_lock);
  tail->next = gasnetc_snd_desc_pool;
  gasnetc_snd_desc_pool = head;
  pthread_mutex_unlock(&gasnetc_snd_desc_lock);

  /* Wake anybody blocked waiting for free descriptors */
  vstat = EVAPI_poll_cq_unblock(gasnetc_hca, gasnetc_snd_cq);
  assert(vstat == VAPI_OK);
}

/* allocate a send descriptor/buffer pair */
GASNET_INLINE_MODIFIER(gasnetc_get_snd_desc)
gasnetc_snd_desc_t *gasnetc_get_snd_desc(void) {
  gasnetc_snd_desc_t *desc = NULL;
  VAPI_wc_desc_t comp;
  VAPI_ret_t vstat;

  while (1) {
    /* First: try to reap a single completed entry from the send CQ w/o blocking */
    vstat = VAPI_poll_cq(gasnetc_hca, gasnetc_snd_cq, &comp);
    if (vstat == VAPI_OK) {
      assert(comp.status == VAPI_SUCCESS);

      desc = (gasnetc_snd_desc_t *)(uintptr_t)comp.id;

      /* If the reaped entry is part of a chain then add the others to the free list */
      if (desc->next) {
	gasnetc_put_snd_desc(desc->next, desc->tail);
      }

      break;	/* Have a decsriptor - leave the loop */
    } else {
      assert(vstat == VAPI_CQ_EMPTY);
    }


    /* Second: try to get an unused descriptor from the free list */
    pthread_mutex_lock(&gasnetc_snd_desc_lock);
    desc = gasnetc_snd_desc_pool;
    if (desc != NULL) {
      gasnetc_snd_desc_pool = desc->next;
      pthread_mutex_unlock(&gasnetc_snd_desc_lock);
      break;	/* Have a decsriptor - leave the loop */
    }
    pthread_mutex_unlock(&gasnetc_snd_desc_lock);


    /* Third: block on the CQ until the next completion or until the blocking call
     * is interrupted.  That will happen if anyone adds new entries to the free list */
    vstat = EVAPI_poll_cq_block(gasnetc_hca, gasnetc_snd_cq, 0 /* == no timeout */, &comp);
    if (vstat == VAPI_OK) {
      assert(comp.status == VAPI_SUCCESS);

      desc = (gasnetc_snd_desc_t *)(uintptr_t)comp.id;

      /* If the reaped entry is part of a chain then add the others to the free list */
      if (desc->next) {
	gasnetc_put_snd_desc(desc->next, desc->tail);
      }

      break;	/* Have a decsriptor - leave the loop */
    } else {
      assert(vstat == VAPI_CQ_EMPTY);
    }
  }

  desc->next = NULL;
  return desc;
}

/* Post a work request to the send queue of the given endpoint */
GASNET_INLINE_MODIFIER(gasnetc_snd_post)
int gasnetc_snd_post(gasnetc_cep_t *cep, gasnetc_snd_desc_t *desc) {
  return (VAPI_OK != VAPI_post_sr(gasnetc_hca, cep->qp_handle, &desc->sr_desc));
}

GASNET_INLINE_MODIFIER(gasnetc_ReqRepGeneric)
int gasnetc_ReqRepGeneric(gasnetc_category_t category, int isReq,
			  int dest, gasnet_handler_t handler,
			  void *src_addr, int nbytes, void *dst_addr,
			  int numargs, gasnetc_snd_desc_t **rdma_desc, va_list argptr) {
  gasnetc_snd_desc_t *desc;
  gasnetc_buffer_t *buf;
  gasnet_handlerarg_t *args;
  size_t msg_len;
  int retval, i;

  desc = gasnetc_get_snd_desc();
  buf = desc->buffer;

  switch (category) {
  case gasnetc_Short:
    args = buf->shortmsg.args;
    msg_len = offsetof(gasnetc_buffer_t, shortmsg.args[numargs]);
    break;

  case gasnetc_Medium:
    args = buf->medmsg.args;
    buf->medmsg.nBytes = nbytes;
    memcpy(GASNETC_MSG_MED_DATA(buf, numargs), src_addr, nbytes);
    msg_len = GASNETC_MSG_MED_OFFSET(numargs) + nbytes;
    break;

  case gasnetc_Long:
    assert(rdma_desc != NULL);
    *rdma_desc = gasnetc_rdma_put(&gasnetc_cep[dest], (uintptr_t)src_addr, (uintptr_t)dst_addr, nbytes);
    assert(*rdma_desc != NULL);

    args = buf->longmsg.args;
    buf->longmsg.destLoc = (uintptr_t)dst_addr;
    buf->longmsg.nBytes  = nbytes;
    msg_len = offsetof(gasnetc_buffer_t, longmsg.args[numargs]);
    break;

  default:
    assert(0);
  }
 
  /* copy args */
  for (i=0; i <numargs; ++i) {
    args[i] = va_arg(argptr, gasnet_handlerarg_t);
  }

  /* build send descriptor */
  desc->sr_sg[0].addr      = (uintptr_t)buf;
  desc->sr_sg[0].len       = msg_len;
  desc->sr_sg[0].lkey      = gasnetc_snd_reg.lkey;
  desc->sr_desc.sg_lst_len = 1;
  desc->sr_desc.imm_data   = GASNETC_MSG_GENFLAGS(isReq, category, numargs, handler, gasnetc_mynode);
  desc->sr_desc.opcode     = VAPI_SEND_WITH_IMM;

  if (dest == gasnetc_mynode) {
    if (category == gasnetc_Long) {
      memcpy(dst_addr, src_addr, nbytes);
    }
    gasnetc_rcv_loopback(desc);
    gasnetc_put_snd_desc(desc, desc);
    retval = GASNET_OK;
  } else {
    retval = gasnetc_snd_post(&gasnetc_cep[dest], desc);
  }

  va_end(argptr);
  GASNETI_RETURN(retval);
}

/* ------------------------------------------------------------------------------------ *
 *  Externally visible functions                                                        *
 * ------------------------------------------------------------------------------------ */
extern void gasnetc_snd_init(void) {
  VAPI_cqe_num_t	act_size;
  VAPI_ret_t		vstat;
  gasnetc_buffer_t	*buf;
  gasnetc_snd_desc_t	*desc;
  int 			count, i;

  count = MIN(GASNETC_SQ_SIZE, gasnetc_hca_cap.max_qp_ous_wr * gasnetc_nodes);

  buf = gasnetc_alloc_pinned(count * sizeof(gasnetc_buffer_t), 0, &gasnetc_snd_reg);
  assert(buf != NULL);

  desc = calloc(count, sizeof(gasnetc_snd_desc_t));
  assert(desc != NULL);

  gasnetc_snd_desc_pool = desc;
  for (i = 0; i < count; ++i, ++desc, ++buf) {
    desc->buffer	    = buf;
    desc->sr_desc.id        = (uintptr_t)desc;		/* CQE will point back to this request */
    desc->sr_desc.comp_type = VAPI_SIGNALED;		/* XXX: is this correct? */
    desc->sr_desc.sg_lst_p  = desc->sr_sg;
    desc->sr_desc.set_se    = FALSE;			/* XXX: is this correct? */
    /* REST OF sr_desc SET AT SEND TIME */
    desc->next = desc + 1;
  }
  (desc - 1)->next = NULL;

  vstat = VAPI_create_cq(gasnetc_hca, count, &gasnetc_snd_cq, &act_size);
  assert(vstat == VAPI_OK);
  assert(act_size >= count);

  vstat = EVAPI_set_comp_eventh(gasnetc_hca, gasnetc_snd_cq, EVAPI_POLL_CQ_UNBLOCK_HANDLER,
                                NULL, &gasnetc_snd_handler);
  assert(vstat == VAPI_OK);
}

extern void gasnetc_snd_fini(void) {
  /* ### cleanup/release everything done in gasnetc_snd_init()
   *
   * Some things to free/destroy:
   *   gasnetc_snd_cq
   *   gasnetc_snd_buffer_pool (descriptors and buffers)
   *   gasnetc_snd_reg
   *   comp_eventh for snd_cq
   */
}

/*
 * Block until a given send descriptor is completed
 */
extern void gasnetc_snd_wait(gasnetc_snd_desc_t *desc) {
  if (desc != NULL) {
    /* ### implement this */
    assert(0);
  } else {
    /* NULL is not an error.  We return immediately. */
  }
}

/* Perform an RDMA put
 * Returns the send descriptor one which one should sync for completion.
 * May return NULL if the transfer is known to be complete.
 *
 * Current system uses bounce buffers only when source is not pinned
 * and uses zero-copy when source is pinned.
 * Note that if the source is partially pinned, both are used.  However,
 * the bounce buffers might include a small portion of the pinned memory
 * since no optimization is doen to get the exact start of the pinned memory.
 *
 * XXX
 * Later "optimization" would be to use bounce buffers for ALL small transfers,
 * regardless of pinning, because the copy would cost less than blocking for the
 * transfer to complete.  This is the return NULL case described above.
 */
extern gasnetc_snd_desc_t *gasnetc_rdma_put(gasnetc_cep_t *cep, uintptr_t src, uintptr_t dst, size_t nbytes) {
  gasnetc_snd_desc_t *desc;
  int rc;

  #if defined(GASNET_SEGMENT_FAST)
  { 
    VAPI_rkey_t rkey = cep->rkey;

    /* Outer loop is over RDMA put operations.
     * We perform as many operations as needed to move the entire payload.
     */
    while (nbytes) {
      uintptr_t msg_limit = MIN(nbytes, gasnetc_hca_port.max_msg_sz);
      gasnetc_snd_desc_t *tail = NULL;
      int i = 0;

      desc = gasnetc_get_snd_desc();

      /* This inner loop assembles gather entries into a single RDMA operation.
       * Each operation is subject to some limits:
       * 1) A single operation cannot move more than gasnetc_hca_port.max_msg_sz bytes
       * 2) Each operation can include no more than GASNETC_SND_SG gather entries.
       *    Each bounce buffer or zero-copy region uses a single gather entry.
       * Of course we stop earlier if the entire payload is accounted for.
       */
      do {
	gasnetc_memreg_t *reg = gasnetc_local_reg(src);
        uintptr_t count;

	tail = NULL;

	if (reg) {
	  /* Zero-copy case:
	   *
	   * Move as many bytes as possible before reaching one of:
	   * 1) msg_limit = MIN(remainder of payload size, remainder of max_msg_sz)
	   * 2) (reg->end - src) + 1 = remainder of the pinned region
	   */
	  count = MIN(msg_limit, (reg->end - src) + 1);
          desc->sr_sg[i].addr = src;
          desc->sr_sg[i].len  = count;
          desc->sr_sg[i].lkey = reg->lkey;
	} else {
	  /* Bounce buffer case:
	   *
	   * Move as many bytes as possible before reaching one of:
	   * 1) msg_limit = MIN(remainder of payload size, remainder of max_msg_sz)
	   * 2) GASNETC_BUFSZ = size of a bounce buffer
	   */
	  count = MIN(msg_limit, GASNETC_BUFSZ);

	  /* Bounce buffers and the descriptors are inseparable.
	   * Therefore we must allocate a descriptor for each bounce buffer, chaining the 'next' fields,
	   * even tough we only need the buffer space, not the fields of the descriptor.
	   */
	  if (tail == NULL) {
	    /* The first descriptor's buffer has not yet been used.  Use it now. */
	    tail = desc;
	  } else {
	    /* Allocate a new descriptor, adding to the chain. */
	    tail->next = gasnetc_get_snd_desc();
	    tail = tail->next;
 	  }
          memcpy(tail->buffer, (void *)src, count);
          desc->sr_sg[i].addr = (uintptr_t)tail->buffer;
          desc->sr_sg[i].len  = count;
          desc->sr_sg[i].lkey = gasnetc_snd_reg.lkey;
	}

	msg_limit -= count;
        nbytes -= count;
        src += count;
        dst += count;
        ++i;
      } while ((i < GASNETC_SND_SG) && msg_limit);

      desc->sr_desc.opcode      = VAPI_RDMA_WRITE;
      desc->sr_desc.remote_addr = dst;
      desc->sr_desc.r_key       = rkey;
      desc->sr_desc.sg_lst_len  = i;
      desc->tail = tail;

      /* ### translate into a sensible error code */
      rc = gasnetc_snd_post(cep, desc);
    }
  }
  #else
  #error "I can only do FAST right now"
  #endif

  /* Since IB requires that RDMA Writes don't pass on another, we know that
   * once the final one is completed, they are all completed.
   */
  return desc;
}

extern int gasnetc_RequestGeneric(gasnetc_category_t category,
				  int dest, gasnet_handler_t handler,
				  void *src_addr, int nbytes, void *dst_addr,
				  int numargs, gasnetc_snd_desc_t **rdma_desc, va_list argptr) {
  return gasnetc_ReqRepGeneric(category, 1, dest, handler,
                               src_addr, nbytes, dst_addr,
                               numargs, rdma_desc, argptr);
}

extern int gasnetc_ReplyGeneric(gasnetc_category_t category,
				gasnet_token_t token, gasnet_handler_t handler,
				  void *src_addr, int nbytes, void *dst_addr,
				  int numargs, gasnetc_snd_desc_t **rdma_desc, va_list argptr) {
  gasnetc_rcv_desc_t *desc = (gasnetc_rcv_desc_t *)token;
  int retval;

  assert(desc);
  assert(desc->handlerRunning);
  assert(!desc->replyIssued);
  assert(GASNETC_MSG_ISREQUEST(desc->flags));

  retval = gasnetc_ReqRepGeneric(category, 0, GASNETC_MSG_SRCIDX(desc->flags), handler,
				 src_addr, nbytes, dst_addr,
				 numargs, rdma_desc, argptr);

  desc->replyIssued = 1;
  return retval;
}

/*  $Archive:: gasnet/gasnet-conduit/gasnet_core_snd.c                  $
 *     $Date: 2003/04/09 23:17:24 $
 * $Revision: 1.1.2.11 $
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
static gasnetc_sbuf_t			*gasnetc_sbuf_pool;
static pthread_mutex_t			gasnetc_sbuf_lock = PTHREAD_MUTEX_INITIALIZER;

typedef struct {
  VAPI_sr_desc_t	sr_desc;		/* send request descriptor */
  VAPI_sg_lst_entry_t	sr_sg[GASNETC_SND_SG];	/* send request gather list */
} gasnetc_sreq_t;

/* ------------------------------------------------------------------------------------ *
 *  File-scoped functions                                                               *
 * ------------------------------------------------------------------------------------ */

GASNET_INLINE_MODIFIER(gasnetc_init_sreq)
void gasnetc_init_sreq(gasnetc_sreq_t *req, gasnetc_sbuf_t *sbuf) {
  req->sr_desc.id        = (uintptr_t)sbuf;
  req->sr_desc.comp_type = VAPI_SIGNALED;		/* XXX: is this correct? */
  req->sr_desc.sg_lst_p  = req->sr_sg;
  req->sr_desc.set_se    = FALSE;			/* XXX: is this correct? */
}

/* free a list of send buffers */
GASNET_INLINE_MODIFIER(gasnetc_put_sbuf)
void gasnetc_put_sbuf(gasnetc_sbuf_t *head, gasnetc_sbuf_t *tail) {
  /* Add the list segment to the free list */
  pthread_mutex_lock(&gasnetc_sbuf_lock);
  tail->next = gasnetc_sbuf_pool;
  gasnetc_sbuf_pool = head;
  pthread_mutex_unlock(&gasnetc_sbuf_lock);
}

/* Completion function for sbufs associated with a send handle */
static void gasnetc_comp_handle(gasnetc_sbuf_t *sbuf) {
  gasnetc_send_handle_t *hand = sbuf->comp_data;
  gasneti_atomic_decrement(&hand->count);
  gasnetc_put_sbuf(sbuf, sbuf->tail);
}

/* Completion function for sbufs which nobody will wait for */
static void gasnetc_comp_trivial(gasnetc_sbuf_t *sbuf) {
  gasnetc_put_sbuf(sbuf, sbuf->tail);
}

/*
 * Try to pull one completed entry from the send CQ (if any).
 */
GASNET_INLINE_MODIFIER(gasnetc_snd_reap)
void gasnetc_snd_reap(void) {
  gasnetc_sbuf_t *sbuf = NULL;
  VAPI_wc_desc_t comp;
  VAPI_ret_t vstat;

  vstat = VAPI_poll_cq(gasnetc_hca, gasnetc_snd_cq, &comp);
  if (vstat == VAPI_OK) {
    if (comp.status == VAPI_SUCCESS) {
      sbuf = (gasnetc_sbuf_t *)(uintptr_t)comp.id;
      assert(sbuf->comp_func != NULL);
      (*sbuf->comp_func)(sbuf);
    } else {
#if 1 
      fprintf(stderr, "@ %d> snd comp.status=%d\n", gasnetc_mynode, comp.status);
      while((vstat = VAPI_poll_cq(gasnetc_hca, gasnetc_rcv_cq, &comp)) == VAPI_OK) {
        fprintf(stderr, "@ %d> - rcv comp.status=%d\n", gasnetc_mynode, comp.status);
      }
#endif
      /* ### What needs to be done here? */
    }
  } else {
    assert(vstat == VAPI_CQ_EMPTY);
  }
}

/* allocate a send buffer pair */
GASNET_INLINE_MODIFIER(gasnetc_get_sbuf)
gasnetc_sbuf_t *gasnetc_get_sbuf(void) {
  gasnetc_sbuf_t *sbuf;

  while (1) {
    /* Try to reap an sbuf from the send CQ.  */
    gasnetc_snd_reap();

    /* Now try to get an unused sbuf from the free list */
    pthread_mutex_lock(&gasnetc_sbuf_lock);
    sbuf = gasnetc_sbuf_pool;
    if (sbuf != NULL) {
      gasnetc_sbuf_pool = sbuf->next;
      pthread_mutex_unlock(&gasnetc_sbuf_lock);
      break;	/* Have a decsriptor - leave the loop */
    }
    pthread_mutex_unlock(&gasnetc_sbuf_lock);

    /* be kind */
    sched_yield();
  }

  assert(sbuf != NULL);

  sbuf->next = NULL;
  sbuf->tail = sbuf;
  sbuf->comp_func = NULL;
  return sbuf;
}

/* Post a work request to the send queue of the given endpoint */
GASNET_INLINE_MODIFIER(gasnetc_snd_post)
int gasnetc_snd_post(gasnetc_cep_t *cep, gasnetc_sreq_t *req) {
  return (VAPI_OK != VAPI_post_sr(gasnetc_hca, cep->qp_handle, &req->sr_desc));
}

GASNET_INLINE_MODIFIER(gasnetc_ReqRepGeneric)
int gasnetc_ReqRepGeneric(gasnetc_category_t category, int isReq,
			  int dest, gasnet_handler_t handler,
			  void *src_addr, int nbytes, void *dst_addr,
			  int numargs, gasnetc_send_handle_t *rdma_hand, va_list argptr) {
  gasnetc_sbuf_t *sbuf;
  gasnetc_buffer_t *buf;
  gasnet_handlerarg_t *args;
  uint32_t flags;
  size_t msg_len;
  int retval, i;

  sbuf = gasnetc_get_sbuf();
  sbuf->comp_func = &gasnetc_comp_trivial;
  buf = sbuf->buffer;

  switch (category) {
  case gasnetc_Short:
    args = buf->shortmsg.args;
    msg_len = offsetof(gasnetc_buffer_t, shortmsg.args[numargs]);
    if (!msg_len) msg_len = 1; /* Mellanox bug (zero-length sends) work-around */
    break;

  case gasnetc_Medium:
    args = buf->medmsg.args;
    buf->medmsg.nBytes = nbytes;
    memcpy(GASNETC_MSG_MED_DATA(buf, numargs), src_addr, nbytes);
    msg_len = GASNETC_MSG_MED_OFFSET(numargs) + nbytes;
    break;

  case gasnetc_Long:
    /* XXX check for error returns */
    (void)gasnetc_rdma_put(dest, (uintptr_t)src_addr, (uintptr_t)dst_addr, nbytes, rdma_hand);
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

  /* generate flags */
  flags = GASNETC_MSG_GENFLAGS(isReq, category, numargs, handler, gasnetc_mynode);

  if (dest == gasnetc_mynode) {
    gasnetc_rcv_loopback(buf, flags);
    gasnetc_put_sbuf(sbuf, sbuf->tail);
    retval = GASNET_OK;
  } else {
    gasnetc_sreq_t req;

    gasnetc_init_sreq(&req, sbuf);
    req.sr_sg[0].addr      = (uintptr_t)buf;
    req.sr_sg[0].len       = msg_len;
    req.sr_sg[0].lkey      = gasnetc_snd_reg.lkey;
    req.sr_desc.sg_lst_len = 1;
    req.sr_desc.imm_data   = flags;
    req.sr_desc.opcode     = VAPI_SEND_WITH_IMM;

    retval = gasnetc_snd_post(&gasnetc_cep[dest], &req);
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
  gasnetc_sbuf_t	*sbuf;
  int 			count, i;

  count = MIN(GASNETC_SQ_SIZE, gasnetc_hca_cap.max_qp_ous_wr * gasnetc_nodes);

  buf = gasnetc_alloc_pinned(count * sizeof(gasnetc_buffer_t), 0, &gasnetc_snd_reg);
  assert(buf != NULL);

  sbuf = calloc(count, sizeof(gasnetc_sbuf_t));
  assert(sbuf != NULL);

  gasnetc_sbuf_pool = sbuf;
  for (i = 0; i < count; ++i, ++sbuf, ++buf) {
    sbuf->buffer	    = buf;
    sbuf->next = sbuf + 1;
  }
  (sbuf - 1)->next = NULL;

  vstat = VAPI_create_cq(gasnetc_hca, count, &gasnetc_snd_cq, &act_size);
  assert(vstat == VAPI_OK);
  assert(act_size >= count);
}

extern void gasnetc_snd_fini(void) {
  /* ### cleanup/release everything done in gasnetc_snd_init()
   *
   * Some things to free/destroy:
   *   gasnetc_snd_cq
   *   gasnetc_sbuf_pool (sbufs and actual buffers)
   *   gasnetc_snd_reg
   */
}

/*
 * Block until a given send handle is marked as done
 */
extern void gasnetc_snd_wait(gasnetc_send_handle_t *hand) {
  if (gasneti_atomic_read(&hand->count) != 0) {
    gasnetc_snd_reap();
    while (gasneti_atomic_read(&hand->count) != 0) {
      sched_yield();
      gasnetc_snd_reap();
    }
  }
}

/* Perform an RDMA put
 * Iff hand is non-NULL, set it up to allow syncing of the put.
 *
 * Current system uses bounce buffers only when source is not pinned
 * and uses zero-copy when source is pinned.
 * Note that if the source is partially pinned, both are used.  However,
 * the bounce buffers might include a small portion of the pinned memory
 * since no optimization is done to get the exact start of the pinned memory.
 *
 * Later "optimization" would be to use bounce buffers for ALL small synchronous transfers,
 * regardless of pinning, because the copy would cost less than blocking for the
 * transfer to complete.
 */
extern int gasnetc_rdma_put(int dest, uintptr_t src, uintptr_t dst, size_t nbytes, gasnetc_send_handle_t *hand) {
  gasnetc_cep_t *cep = &gasnetc_cep[dest];
  gasnetc_sbuf_t *sbuf;
  int rc;

  if (dest == gasnetc_mynode) {
    memcpy((void *)dst, (void *)src, nbytes);
    return 0;
  }

  #if defined(GASNET_SEGMENT_FAST)
  { 
    VAPI_rkey_t rkey = cep->rkey;

    /* Outer loop is over RDMA put operations.
     * We perform as many operations as needed to move the entire payload.
     */
    while (nbytes) {
      uintptr_t msg_limit = MIN(nbytes, gasnetc_hca_port.max_msg_sz);
      gasnetc_sreq_t req;
      gasnetc_sbuf_t *tail = NULL;
      int did_zero_copy = 0;
      int i = 0;

      /* Buffers are our means to account for available slots in the send queue.
       * Therefore we must allocate at least one sbuf even if we will only do zero-copy puts.
       */
      sbuf = gasnetc_get_sbuf();
      sbuf->comp_func = &gasnetc_comp_trivial;

      gasnetc_init_sreq(&req, sbuf);
      req.sr_desc.opcode      = VAPI_RDMA_WRITE;
      req.sr_desc.remote_addr = dst;
      req.sr_desc.r_key       = rkey;

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

	if (reg) {
	  /* Zero-copy case:
	   *
	   * Move as many bytes as possible before reaching one of:
	   * 1) msg_limit = MIN(remainder of payload size, remainder of max_msg_sz)
	   * 2) (reg->end - src) + 1 = remainder of the pinned region
	   */
	  count = MIN(msg_limit, (reg->end - src) + 1);
          req.sr_sg[i].addr = src;
          req.sr_sg[i].len  = count;
          req.sr_sg[i].lkey = reg->lkey;
	  did_zero_copy = 1;
	} else {
	  /* Bounce buffer case:
	   *
	   * Move as many bytes as possible before reaching one of:
	   * 1) msg_limit = MIN(remainder of payload size, remainder of max_msg_sz)
	   * 2) GASNETC_BUFSZ = size of a bounce buffer
	   */
	  count = MIN(msg_limit, GASNETC_BUFSZ);

	  if (tail == NULL) {
	    /* The first sbuf has not yet been used.  Use it now. */
	    tail = sbuf;
	  } else {
	    /* Allocate a new sbuf, adding to the chain. */
	    tail->next = gasnetc_get_sbuf();
	    tail = tail->next;
 	  }
          memcpy(tail->buffer, (void *)src, count);
          req.sr_sg[i].addr = (uintptr_t)tail->buffer;
          req.sr_sg[i].len  = count;
          req.sr_sg[i].lkey = gasnetc_snd_reg.lkey;
	}

	msg_limit -= count;
        nbytes -= count;
        src += count;
        dst += count;
        ++i;
      } while ((i < GASNETC_SND_SG) && msg_limit);

      req.sr_desc.sg_lst_len  = i;
      sbuf->tail = tail;

      if (hand && did_zero_copy) {
	/* Ensure the caller will wait for this descriptor */
	gasneti_atomic_increment(&hand->count);
	sbuf->comp_func = &gasnetc_comp_handle;
	sbuf->comp_data = hand;
      }

      /* ### translate into a sensible error code */
      rc = gasnetc_snd_post(cep, &req);
    }
  }
  #else
  #error "I can only do FAST right now"
  #endif

  return 0;
}

extern int gasnetc_RequestGeneric(gasnetc_category_t category,
				  int dest, gasnet_handler_t handler,
				  void *src_addr, int nbytes, void *dst_addr,
				  int numargs, gasnetc_send_handle_t *rdma_hand, va_list argptr) {
  return gasnetc_ReqRepGeneric(category, 1, dest, handler,
                               src_addr, nbytes, dst_addr,
                               numargs, rdma_hand, argptr);
}

extern int gasnetc_ReplyGeneric(gasnetc_category_t category,
				gasnet_token_t token, gasnet_handler_t handler,
				  void *src_addr, int nbytes, void *dst_addr,
				  int numargs, gasnetc_send_handle_t *rdma_hand, va_list argptr) {
  gasnetc_rbuf_t *rbuf = (gasnetc_rbuf_t *)token;
  int retval;

  assert(rbuf);
  assert(rbuf->handlerRunning);
  assert(!rbuf->replyIssued);
  assert(GASNETC_MSG_ISREQUEST(rbuf->flags));

  retval = gasnetc_ReqRepGeneric(category, 0, GASNETC_MSG_SRCIDX(rbuf->flags), handler,
				 src_addr, nbytes, dst_addr,
				 numargs, rdma_hand, argptr);

  rbuf->replyIssued = 1;
  return retval;
}

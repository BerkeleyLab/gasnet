/*  $Archive:: gasnet/gasnet-conduit/gasnet_core_snd.c                  $
 *     $Date: 2003/04/16 06:54:41 $
 * $Revision: 1.1.2.17 $
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
#if !defined(GASNET_SEQ)
  static pthread_mutex_t		gasnetc_sbuf_lock = PTHREAD_MUTEX_INITIALIZER;
#endif

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
  #if !defined(GASNET_SEQ)
    pthread_mutex_lock(&gasnetc_sbuf_lock);
  #endif
  tail->next = gasnetc_sbuf_pool;
  gasnetc_sbuf_pool = head;
  #if !defined(GASNET_SEQ)
    pthread_mutex_unlock(&gasnetc_sbuf_lock);
  #endif
}

/* Try to pull completed entries from the send CQ (if any). */
GASNET_INLINE_MODIFIER(gasnetc_snd_reap)
gasnetc_sbuf_t *gasnetc_snd_reap(void) {
  gasnetc_sbuf_t *head, *tail;
  int count;
  
  head = tail = NULL;
  for (count = 0; count < GASNETC_SND_REAP_LIMIT; ++count) {
    VAPI_ret_t vstat;
    VAPI_wc_desc_t comp;

    vstat = VAPI_poll_cq(gasnetc_hca, gasnetc_snd_cq, &comp);
    if (vstat == VAPI_OK) {
      if (comp.status == VAPI_SUCCESS) {
        gasnetc_sbuf_t *sbuf = (gasnetc_sbuf_t *)(uintptr_t)comp.id;
        if (sbuf) {
          if (sbuf->local_counter) gasneti_atomic_decrement(sbuf->local_counter);
          if (sbuf->remote_counter) gasneti_atomic_decrement(sbuf->remote_counter);
	  if (head) {
	    sbuf->tail->next = head;
	    sbuf->tail = head->tail;
	  }
	  head = sbuf;
        } else {
          fprintf(stderr, "@ %d> snd_reap reaped NULL sbuf\n", gasnetc_mynode);
        }
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
      break;
    }
  }

  return head;
}

/* allocate a send buffer pair */
GASNET_INLINE_MODIFIER(gasnetc_get_sbuf)
gasnetc_sbuf_t *gasnetc_get_sbuf(void) {
  gasnetc_sbuf_t *sbuf;

  while (1) {
    /* try to get an unused sbuf by reaping the send CQ */
    sbuf = gasnetc_snd_reap();
    if (sbuf) {
      if (sbuf->next) {
        gasnetc_put_sbuf(sbuf->next, sbuf->tail);
      }
      break;
    }

    /* try to get an unused sbuf from the free list */
    #if !defined(GASNET_SEQ)
      pthread_mutex_lock(&gasnetc_sbuf_lock);
    #endif
    sbuf = gasnetc_sbuf_pool;
    if (sbuf != NULL) {
      gasnetc_sbuf_pool = sbuf->next;
      #if !defined(GASNET_SEQ)
        pthread_mutex_unlock(&gasnetc_sbuf_lock);
      #endif
      break;	/* Have a decsriptor - leave the loop */
    }
    #if !defined(GASNET_SEQ)
      pthread_mutex_unlock(&gasnetc_sbuf_lock);
    #endif

    /* be kind */
    sched_yield();
  }

  assert(sbuf != NULL);

  sbuf->next = NULL;
  sbuf->tail = sbuf;
  sbuf->local_counter = NULL;
  sbuf->remote_counter = NULL;
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
			  int numargs, gasneti_atomic_t *local_counter, va_list argptr) {
  gasnetc_sbuf_t *sbuf;
  gasnetc_buffer_t *buf;
  gasnet_handlerarg_t *args;
  uint32_t flags;
  size_t msg_len;
  int retval, i;

  sbuf = gasnetc_get_sbuf();
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
    (void)gasnetc_rdma_put(dest, src_addr, dst_addr, nbytes, local_counter, NULL);
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


/* Clean send CQ */
void gasnetc_snd_poll(void) {
  gasnetc_sbuf_t *sbuf;

  sbuf = gasnetc_snd_reap();

  if (sbuf) {
    gasnetc_put_sbuf(sbuf, sbuf->tail);
  }
}

/*
 * Block until a given counter is marked as done
 */
extern void gasnetc_rdma_wait(gasneti_atomic_t *counter) {
  int value = gasneti_atomic_read(counter);
  GASNETI_TRACE_PRINTF(C, ("gasnetc_rdma_wait: counter %p has value %d", counter, value));

  if (value != 0) {
    gasnetc_snd_poll();
    value = gasneti_atomic_read(counter);

    while (value != 0) {
      sched_yield();
      gasnetc_snd_poll();
      value = gasneti_atomic_read(counter);
    }
  }

  GASNETI_TRACE_PRINTF(C, ("gasnetc_rdma_wait: counter %p is done", counter));
}

/*
 * Check if a given counter is marked as done
 */
extern int gasnetc_rdma_test(gasneti_atomic_t *counter) {
  int value = gasneti_atomic_read(counter);
  GASNETI_TRACE_PRINTF(C, ("gasnetc_rdma_test: counter %p has value %d", counter, value));
  return !value;
}

/* Perform an RDMA put
 *
 * Uses bounce buffers when the source is not pinned, or is "small enough" and the caller is
 * planning to wait for local completion.  Otherwise zero-copy is used when the source is pinned.
 */
extern int gasnetc_rdma_put(int dest, void *src_ptr, void *dst_ptr, size_t nbytes, gasneti_atomic_t *local_counter, gasneti_atomic_t *remote_counter) {
  gasnetc_cep_t *cep = &gasnetc_cep[dest];
  gasnetc_sbuf_t *sbuf;
  uintptr_t src, dst;
  int force_copy;
  int rc;

  if (dest == gasnetc_mynode) {
    memcpy(dst_ptr, src_ptr, nbytes);
    return 0;
  }

  /* If the caller will wait on local completion, then for small transfers it is best to just
   * perform the copy locally and allow the caller to proceed */
  force_copy = ((local_counter != NULL) && (nbytes <= GASNETC_PUT_COPY_LIMIT));

  src = (uintptr_t)src_ptr;
  dst = (uintptr_t)dst_ptr;

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
	gasnetc_memreg_t *reg;
        uintptr_t count;

	reg = force_copy ? NULL : gasnetc_local_reg(src);

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

      if (local_counter && did_zero_copy) {
	gasneti_atomic_increment(local_counter);
        sbuf->local_counter = local_counter;
      }
      if (remote_counter) {
	gasneti_atomic_increment(remote_counter);
        sbuf->remote_counter = remote_counter;
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
				  int numargs, gasneti_atomic_t *local_counter, va_list argptr) {
  return gasnetc_ReqRepGeneric(category, 1, dest, handler,
                               src_addr, nbytes, dst_addr,
                               numargs, local_counter, argptr);
}

extern int gasnetc_ReplyGeneric(gasnetc_category_t category,
				gasnet_token_t token, gasnet_handler_t handler,
				  void *src_addr, int nbytes, void *dst_addr,
				  int numargs, gasneti_atomic_t *local_counter, va_list argptr) {
  gasnetc_rbuf_t *rbuf = (gasnetc_rbuf_t *)token;
  int retval;

  assert(rbuf);
  assert(rbuf->handlerRunning);
  assert(!rbuf->replyIssued);
  assert(GASNETC_MSG_ISREQUEST(rbuf->flags));

  retval = gasnetc_ReqRepGeneric(category, 0, GASNETC_MSG_SRCIDX(rbuf->flags), handler,
				 src_addr, nbytes, dst_addr,
				 numargs, local_counter, argptr);

  rbuf->replyIssued = 1;
  return retval;
}

/*  $Archive:: gasnet/gasnet-conduit/gasnet_core_snd.c                  $
 *     $Date: 2003/05/28 18:59:25 $
 * $Revision: 1.1.2.34 $
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

/* Description of a send buffer */
typedef struct _gasnetc_sbuf_t {
  struct _gasnetc_sbuf_t	*next;
  gasnetc_buffer_t		*buffer;

  /* Completion counters */
  gasneti_atomic_t		*mem_oust;	/* source memory refs outstanding */
  gasneti_atomic_t		*req_oust;	/* requests outstanding */

  /* Destination address/len for bounced RDMA reads */
  void				*addr;
  size_t			len;
} gasnetc_sbuf_t;

/* VAPI structures for a send request */
typedef struct {
  VAPI_sr_desc_t	sr_desc;		/* send request descriptor */
  VAPI_sg_lst_entry_t	sr_sg;			/* single send request gather list entry */
} gasnetc_sreq_t;

static gasnetc_sbuf_t			*gasnetc_sbuf_head;
static gasnetc_sbuf_t			*gasnetc_sbuf_pool;
static pthread_mutex_t		gasnetc_sbuf_lock = PTHREAD_MUTEX_INITIALIZER;

/* ------------------------------------------------------------------------------------ *
 *  File-scoped functions                                                               *
 * ------------------------------------------------------------------------------------ */

GASNET_INLINE_MODIFIER(gasnetc_init_sreq)
void gasnetc_init_sreq(gasnetc_sreq_t *req, gasnetc_sbuf_t *sbuf) {
  req->sr_desc.id        = (uintptr_t)sbuf;
  req->sr_desc.comp_type = VAPI_SIGNALED;		/* XXX: is this correct? */
  req->sr_desc.sg_lst_p  = &req->sr_sg;
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

/* Try to pull completed entries from the send CQ (if any). */
GASNET_INLINE_MODIFIER(gasnetc_snd_reap)
gasnetc_sbuf_t *gasnetc_snd_reap(gasnetc_sbuf_t **tail_p) {
  static pthread_mutex_t poll_lock = PTHREAD_MUTEX_INITIALIZER;
  gasnetc_sbuf_t *head, *tail;
  int count;
  
  head = tail = NULL;
  for (count = 0; count < GASNETC_SND_REAP_LIMIT; ++count) {
    VAPI_ret_t vstat;
    VAPI_wc_desc_t comp;

    /* It seems that VAPI_poll_cq() is not thread-safe */
    pthread_mutex_lock(&poll_lock);
    vstat = VAPI_poll_cq(gasnetc_hca, gasnetc_snd_cq, &comp);
    pthread_mutex_unlock(&poll_lock);

    if (vstat == VAPI_OK) {
      if (comp.status == VAPI_SUCCESS) {
        gasnetc_sbuf_t *sbuf = (gasnetc_sbuf_t *)(uintptr_t)comp.id;
        if (sbuf) {
	  /* complete bounced RMDA read, if any */
	  if (sbuf->addr) {
	    assert(comp.opcode == VAPI_CQE_SQ_RDMA_READ);

	    memcpy(sbuf->addr, sbuf->buffer, sbuf->len);
            gasneti_memsync();
	  }
	  
	  /* decrement any outstanding counters */
          if (sbuf->mem_oust) {
	    assert((int)gasneti_atomic_read(sbuf->mem_oust) > 0);
	    gasneti_atomic_decrement(sbuf->mem_oust);
	  }
          if (sbuf->req_oust){
	    assert((int)gasneti_atomic_read(sbuf->req_oust) > 0);
            gasneti_atomic_decrement(sbuf->req_oust);
	  }
	  
	  /* keep a list of reaped sbufs */
	  if (!tail) {
	    tail = sbuf;
	  }
	  sbuf->next = head;
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

  *tail_p = tail;
  return head;
}

/* allocate a send buffer pair */
GASNET_INLINE_MODIFIER(gasnetc_get_sbuf)
gasnetc_sbuf_t *gasnetc_get_sbuf(void) {
  gasnetc_sbuf_t *sbuf, *tail;

  while (1) {
    /* try to get an unused sbuf by reaping the send CQ */
    sbuf = gasnetc_snd_reap(&tail);
    if (sbuf) {
      if (sbuf->next) {
        gasnetc_put_sbuf(sbuf->next, tail);
      }
      break;
    }

    /* try to get an unused sbuf from the free list */
    pthread_mutex_lock(&gasnetc_sbuf_lock);
    sbuf = gasnetc_sbuf_pool;
    if (sbuf != NULL) {
      gasnetc_sbuf_pool = sbuf->next;
      pthread_mutex_unlock(&gasnetc_sbuf_lock);
      break;	/* Have a decsriptor - leave the loop */
    }
    pthread_mutex_unlock(&gasnetc_sbuf_lock);

    /* be kind */
    gasneti_sched_yield();
  }

  assert(sbuf != NULL);

  sbuf->next = NULL;
  sbuf->mem_oust = NULL;
  sbuf->req_oust = NULL;
  sbuf->addr = NULL;

  return sbuf;
}

/* Post a work request to the send queue of the given endpoint */
GASNET_INLINE_MODIFIER(gasnetc_snd_post)
int gasnetc_snd_post(gasnetc_cep_t *cep, gasnetc_sreq_t *req) {
  /* check for attempted loopback traffic */
  assert(cep != &gasnetc_cep[gasnetc_mynode]);

  return (VAPI_OK != VAPI_post_sr(gasnetc_hca, cep->qp_handle, &req->sr_desc));
}

/* Post an INLINE work request to the send queue of the given endpoint */
GASNET_INLINE_MODIFIER(gasnetc_snd_inline_post)
int gasnetc_snd_inline_post(gasnetc_cep_t *cep, gasnetc_sreq_t *req) {
  /* check for attempted loopback traffic */
  assert(cep != &gasnetc_cep[gasnetc_mynode]);

  return (VAPI_OK != EVAPI_post_inline_sr(gasnetc_hca, cep->qp_handle, &req->sr_desc));
}

GASNET_INLINE_MODIFIER(gasnetc_ReqRepGeneric)
int gasnetc_ReqRepGeneric(gasnetc_category_t category, int isReq,
			  int dest, gasnet_handler_t handler,
			  void *src_addr, int nbytes, void *dst_addr,
			  int numargs, gasneti_atomic_t *mem_oust, va_list argptr) {
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
    if (nbytes) {
      if (dest == gasnetc_mynode) {
        memcpy(dst_addr, src_addr, nbytes);
      } else {
        /* XXX check for error returns */
        (void)gasnetc_rdma_put(dest, src_addr, dst_addr, nbytes, mem_oust, NULL);
      }
    }
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
    gasnetc_put_sbuf(sbuf, sbuf);
    retval = GASNET_OK;
  } else {
    gasnetc_sreq_t req;

    gasnetc_init_sreq(&req, sbuf);
    req.sr_desc.opcode     = VAPI_SEND_WITH_IMM;
    req.sr_desc.sg_lst_len = 1;
    req.sr_desc.imm_data   = flags;
    req.sr_desc.fence      = TRUE;
    req.sr_sg.addr         = (uintptr_t)buf;
    req.sr_sg.len          = msg_len;
    req.sr_sg.lkey         = gasnetc_snd_reg.lkey;

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

  buf = gasnetc_alloc_pinned(count * sizeof(gasnetc_buffer_t), VAPI_EN_LOCAL_WRITE, &gasnetc_snd_reg);
  assert(buf != NULL);

  sbuf = calloc(count, sizeof(gasnetc_sbuf_t));
  assert(sbuf != NULL);

  gasnetc_sbuf_head = sbuf;
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
  VAPI_ret_t vstat;

  vstat = VAPI_destroy_cq(gasnetc_hca, gasnetc_snd_cq);
  assert(vstat == VAPI_OK);

  gasnetc_free_pinned(&gasnetc_snd_reg);
  free(gasnetc_sbuf_head);
}


/* Clean send CQ */
void gasnetc_snd_poll(void) {
  gasnetc_sbuf_t *sbuf, *tail;

  sbuf = gasnetc_snd_reap(&tail);

  if (sbuf) {
    gasnetc_put_sbuf(sbuf, tail);
  }
}

/* Perform an RDMA put
 *
 * Uses bounce buffers when the source is not pinned, or is "small enough" and the caller is
 * planning to wait for local completion.  Otherwise zero-copy is used when the source is pinned.
 */
extern int gasnetc_rdma_put(int node, void *src_ptr, void *dst_ptr, size_t nbytes, gasneti_atomic_t *mem_oust, gasneti_atomic_t *req_oust) {
  gasnetc_cep_t *cep = &gasnetc_cep[node];
  uintptr_t src, dst;
  int rc;

  src = (uintptr_t)src_ptr;
  dst = (uintptr_t)dst_ptr;

  assert(nbytes != 0);
  
  do {
    gasnetc_sbuf_t *sbuf;
    gasnetc_sreq_t req;

    /* Buffers are our means to account for available slots in the send queue.
     * Therefore we must allocate an sbuf even for zero-copy puts.
     */
    sbuf = gasnetc_get_sbuf();

    gasnetc_init_sreq(&req, sbuf);
    req.sr_desc.opcode      = VAPI_RDMA_WRITE;
    req.sr_desc.sg_lst_len  = 1;
    req.sr_desc.fence       = TRUE;
    req.sr_desc.remote_addr = dst;
    req.sr_desc.r_key       = cep->rkey;	/* XXX: change for non-FAST */

    if (req_oust) {
      gasneti_atomic_increment(req_oust);
      sbuf->req_oust = req_oust;
    }

    if (nbytes <= GASNETC_PUT_INLINE_LIMIT) {
      /* Use a short-cut for sends that are short enough.
       *
       * Note that we do this based only on the size of the request, without bothering to check whether
       * the caller cares about local completion, or whether zero-copy is possible.
       * We do this is because the cost of this small copy appears cheaper then the alternative logic.
       */
	  
      req.sr_sg.addr          = src;
      req.sr_sg.len           = nbytes;

      /* ### translate into a sensible error code */
      rc = gasnetc_snd_inline_post(cep, &req);
      assert(rc == VAPI_OK);
      
      break;	/* done */
    } else if ((nbytes <= GASNETC_PUT_COPY_LIMIT) && (mem_oust != NULL)) {
      /* If the transfer is "not too large" and the caller will wait on local completion,
       * then perform the copy locally, thus allowing the caller to proceed.
       */
    
      /* Setup the gather bounce buffer */
      memcpy(sbuf->buffer, (void *)src, nbytes);
      req.sr_sg.addr = (uintptr_t)sbuf->buffer;
      req.sr_sg.len  = nbytes;
      req.sr_sg.lkey = gasnetc_snd_reg.lkey;

      /* ### translate into a sensible error code */
      rc = gasnetc_snd_post(cep, &req);
      assert(rc == VAPI_OK);
      
      break;	/* done */
    } else {
      uintptr_t count;
      gasnetc_memreg_t *reg;

      reg = gasnetc_local_reg(src, src + (nbytes - 1));

      if (reg != NULL) {
        /* ZERO COPY CASE */
        count  = MIN(nbytes, gasnetc_hca_port.max_msg_sz);

        req.sr_sg.addr = src;
        req.sr_sg.lkey = reg->lkey;

        if (mem_oust) {
  	  gasneti_atomic_increment(mem_oust);
          sbuf->mem_oust = mem_oust;
        }
      } else {
        /* BOUNCE BUFFER CASE */
        count  = MIN(nbytes, GASNETC_BUFSZ);

        memcpy(sbuf->buffer, (void *)src, count);
        req.sr_sg.addr = (uintptr_t)sbuf->buffer;
        req.sr_sg.lkey = gasnetc_snd_reg.lkey;
      }
      req.sr_sg.len  = count;

      /* ### translate into a sensible error code */
      rc = gasnetc_snd_post(cep, &req);
      assert(rc == VAPI_OK);

      src += count;
      dst += count;
      nbytes -= count;
    }
  } while (nbytes);

  return 0;
}

/* Perform an RDMA get
 *
 * Uses bounce buffers when the destination is not pinned, zero-copy otherwise.
 */
extern int gasnetc_rdma_get(int node, void *src_ptr, void *dst_ptr, size_t nbytes, gasneti_atomic_t *req_oust) {
  gasnetc_cep_t *cep = &gasnetc_cep[node];
  gasnetc_sbuf_t *sbuf;
  uintptr_t src, dst;
  int rc;

  src = (uintptr_t)src_ptr;
  dst = (uintptr_t)dst_ptr;

  assert(nbytes != 0);

  do {
    gasnetc_memreg_t *reg;
    gasnetc_sbuf_t *sbuf;
    gasnetc_sreq_t req;
    uintptr_t count;

    /* Buffers are our means to account for available slots in the send queue.
     * Therefore we must allocate an sbuf even for zero-copy puts.
     */
    sbuf = gasnetc_get_sbuf();

    gasnetc_init_sreq(&req, sbuf);
    req.sr_desc.opcode      = VAPI_RDMA_READ;
    req.sr_desc.sg_lst_len  = 1;
    req.sr_desc.fence       = FALSE;
    req.sr_desc.remote_addr = src;
    req.sr_desc.r_key       = cep->rkey;	/* XXX: change for non-FAST */

    if (req_oust) {
      gasneti_atomic_increment(req_oust);
      sbuf->req_oust = req_oust;
    }

    reg = gasnetc_local_reg(dst, dst + (nbytes - 1));

    if (reg != NULL) {
      /* ZERO-COPY CASE */
      count = MIN(nbytes, gasnetc_hca_port.max_msg_sz);

      req.sr_sg.addr = dst;
      req.sr_sg.lkey = reg->lkey;
    } else {
      /* BOUNCE BUFFER CASE */
      count = MIN(nbytes, GASNETC_BUFSZ);

      req.sr_sg.addr = (uintptr_t)sbuf->buffer;
      req.sr_sg.lkey = gasnetc_snd_reg.lkey;
      sbuf->addr = (void *)dst;
      sbuf->len = count;
    }

    req.sr_sg.len  = count;

    /* ### translate into a sensible error code */
    rc = gasnetc_snd_post(cep, &req);

    src += count;
    dst += count;
    nbytes -= count;
  } while (nbytes);

  return 0;
}

/* write a constant pattern to remote memory using a local memset and an RDMA put */
extern int gasnetc_rdma_memset(int node, void *dst_ptr, int val, size_t nbytes, gasneti_atomic_t *req_oust) {
  gasnetc_cep_t *cep = &gasnetc_cep[node];
  uintptr_t dst = (uintptr_t)dst_ptr;
  gasnetc_sbuf_t *sbuf;
  gasnetc_sreq_t req;
  int rc;

  assert(nbytes != 0);
	  
  do {
    uintptr_t count = MIN(nbytes, GASNETC_BUFSZ);

    sbuf = gasnetc_get_sbuf();
    memset(sbuf->buffer, val, count);

    gasnetc_init_sreq(&req, sbuf);
    req.sr_desc.opcode      = VAPI_RDMA_WRITE;
    req.sr_desc.sg_lst_len  = 1;
    req.sr_desc.fence       = TRUE;
    req.sr_desc.remote_addr = dst;
    req.sr_desc.r_key       = cep->rkey;	/* XXX: change for non-FAST */
    req.sr_sg.addr = (uintptr_t)sbuf->buffer;
    req.sr_sg.len  = count;
    req.sr_sg.lkey = gasnetc_snd_reg.lkey;

    if (req_oust) {
      gasneti_atomic_increment(req_oust);
      sbuf->req_oust = req_oust;
    }

    /* ### translate into a sensible error code */
    rc = gasnetc_snd_post(cep, &req);
     
    dst += count;
    nbytes -= count;
  } while (nbytes);

  return 0;
}

extern int gasnetc_RequestGeneric(gasnetc_category_t category,
				  int dest, gasnet_handler_t handler,
				  void *src_addr, int nbytes, void *dst_addr,
				  int numargs, gasneti_atomic_t *mem_oust, va_list argptr) {
  return gasnetc_ReqRepGeneric(category, 1, dest, handler,
                               src_addr, nbytes, dst_addr,
                               numargs, mem_oust, argptr);
}

extern int gasnetc_ReplyGeneric(gasnetc_category_t category,
				gasnet_token_t token, gasnet_handler_t handler,
				  void *src_addr, int nbytes, void *dst_addr,
				  int numargs, gasneti_atomic_t *mem_oust, va_list argptr) {
  gasnetc_rbuf_t *rbuf = (gasnetc_rbuf_t *)token;
  int retval;

  assert(rbuf);
  assert(rbuf->handlerRunning);
  assert(!rbuf->replyIssued);
  assert(GASNETC_MSG_ISREQUEST(rbuf->flags));

  retval = gasnetc_ReqRepGeneric(category, 0, GASNETC_MSG_SRCIDX(rbuf->flags), handler,
				 src_addr, nbytes, dst_addr,
				 numargs, mem_oust, argptr);

  rbuf->replyIssued = 1;
  return retval;
}

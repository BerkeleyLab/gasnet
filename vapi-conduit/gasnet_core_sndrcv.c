/*  $Archive:: gasnet/gasnet-conduit/gasnet_core_sndrcv.c                  $
 *     $Date: 2003/06/24 20:48:08 $
 * $Revision: 1.1.2.7 $
 * Description: GASNet vapi conduit implementation, transport send/receive logic
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
gasnetc_memreg_t			gasnetc_snd_reg;
VAPI_cq_hndl_t                          gasnetc_rcv_cq;
VAPI_cq_hndl_t				gasnetc_snd_cq;

/* ------------------------------------------------------------------------------------ *
 *  File-scoped types                                                                   *
 * ------------------------------------------------------------------------------------ */

/* Description of a receive buffer */
typedef struct {
  VAPI_rr_desc_t        rr_desc;        /* recv request descriptor */
  VAPI_sg_lst_entry_t   rr_sg;          /* single-entry scatter list */

  /* Intialized at recv time: */
  int                   needReply;
  int                   handlerRunning;
  uint32_t              flags;
} gasnetc_rbuf_t;

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

/* ------------------------------------------------------------------------------------ *
 *  File-scoped variables
 * ------------------------------------------------------------------------------------ */

static gasnetc_sbuf_t			*gasnetc_sbuf_alloc, *gasnetc_sbuf_free;
static pthread_mutex_t			gasnetc_sbuf_lock = PTHREAD_MUTEX_INITIALIZER;
static gasnetc_rbuf_t			*gasnetc_rbuf_alloc, *gasnetc_rbuf_free;
#if GASNETC_RCV_THREAD
  static EVAPI_compl_handler_hndl_t	gasnetc_rcv_handler;
#endif

/* ------------------------------------------------------------------------------------ *
 *  File-scoped functions and macros                                                    *
 * ------------------------------------------------------------------------------------ */

#define GASNETC_MSG_GENFLAGS(isreq, cat, nargs, hand, srcidx)   \
  (uint32_t)(  (((srcidx) & 0xffff) << 16)      \
	     | (((hand)   & 0xff)   << 8 )      \
	     | (((nargs)  & 0x1f)   << 3 )      \
	     | ((!(isreq) & 0x1)    << 2 )      \
	     | (((cat)    & 0x3)         ))

#define GASNETC_MSG_NUMARGS(flags)      (((flags) >> 3) & 0x1f)
#define GASNETC_MSG_ISREQUEST(flags)    (!((flags) & 0x4))
#define GASNETC_MSG_ISREPLY(flags)      (!!((flags) & 0x4))
#define GASNETC_MSG_CATEGORY(flags)     ((gasnetc_category_t)((flags) & 0x3))
#define GASNETC_MSG_HANDLERID(flags)    ((gasnet_handler_t)((flags) >> 8))
#define GASNETC_MSG_SRCIDX(flags)       ((gasnet_node_t)((flags) >> 16))


/* Post a work request to the send queue of the given endpoint */
GASNET_INLINE_MODIFIER(gasnetc_snd_post)
void gasnetc_snd_post(gasnetc_cep_t *cep, gasnetc_sreq_t *req) {
  VAPI_ret_t vstat;

  /* check for attempted loopback traffic */
  assert(cep != &gasnetc_cep[gasnetc_mynode]);

  vstat = VAPI_post_sr(gasnetc_hca, cep->qp_handle, &req->sr_desc);
  assert((vstat == VAPI_OK) || (vstat == VAPI_EINVAL_QP_HNDL /* disconnected (race) */));
}

/* Post an INLINE work request to the send queue of the given endpoint */
GASNET_INLINE_MODIFIER(gasnetc_snd_inline_post)
void gasnetc_snd_inline_post(gasnetc_cep_t *cep, gasnetc_sreq_t *req) {
  VAPI_ret_t vstat;

  /* check for attempted loopback traffic */
  assert(cep != &gasnetc_cep[gasnetc_mynode]);

  vstat = EVAPI_post_inline_sr(gasnetc_hca, cep->qp_handle, &req->sr_desc);
  assert((vstat == VAPI_OK) || (vstat == VAPI_EINVAL_QP_HNDL /* disconnected (race) */));
}

/* Post a work request to the receive queue of the given endpoint */
GASNET_INLINE_MODIFIER(gasnetc_rcv_post)
void gasnetc_rcv_post(gasnetc_cep_t *cep, gasnetc_rbuf_t *rbuf) {
  VAPI_ret_t vstat;

  /* check for attempted loopback traffic */
  assert(cep != &gasnetc_cep[gasnetc_mynode]);
  
  vstat = VAPI_post_rr(gasnetc_hca, cep->qp_handle, &rbuf->rr_desc);
  assert((vstat == VAPI_OK) || (vstat == VAPI_EINVAL_QP_HNDL /* disconnected (race) */));
}

GASNET_INLINE_MODIFIER(gasnetc_processPacket)
void gasnetc_processPacket(gasnetc_rbuf_t *rbuf, uint32_t flags) {
  gasnetc_buffer_t *buf = (gasnetc_buffer_t *)(uintptr_t)(rbuf->rr_sg.addr);
  gasnet_handler_t handler_id = GASNETC_MSG_HANDLERID(flags);
  gasnetc_handler_fn_t handler_fn = gasnetc_handler[handler_id];
  gasnetc_category_t category = GASNETC_MSG_CATEGORY(flags);
  int numargs = GASNETC_MSG_NUMARGS(flags);
  gasnet_handlerarg_t *args;
  size_t nbytes;
  void *data;

  rbuf->needReply = GASNETC_MSG_ISREQUEST(flags);
  rbuf->handlerRunning = 1;
  rbuf->flags = flags;

  switch (category) {
    case gasnetc_System:
      {
        gasnetc_sys_handler_fn_t sys_handler_fn = gasnetc_sys_handler[handler_id];
        #if TRACE
	  /* This is needed because w/o it the ony way for the tracing macros
	   * to get the src node would be to call _AMGetMsgSource(), which will
	   * fail with an assertion if invoked before _attach().
	   */
	  gasnet_node_t src = GASNETC_MSG_SRCIDX(flags);
	#endif
	args = buf->shortmsg.args;
        if (GASNETC_MSG_ISREQUEST(flags))
          GASNETI_TRACE_SYSTEM_REQHANDLER(handler_id, src, rbuf, numargs, args);
        else
          GASNETI_TRACE_SYSTEM_REPHANDLER(handler_id, src, rbuf, numargs, args);
        RUN_HANDLER_SYSTEM(sys_handler_fn,rbuf,args,numargs);
      }
      break;

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
  tail->next = gasnetc_sbuf_free;
  gasnetc_sbuf_free = head;
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
        fprintf(stderr, "@ %d> snd comp.status=%d comp.opcode=%d\n", gasnetc_mynode, comp.status, comp.opcode);
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

  GASNETI_TRACE_WAIT_BEGIN();
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
    sbuf = gasnetc_sbuf_free;
    if (sbuf != NULL) {
      gasnetc_sbuf_free = sbuf->next;
      pthread_mutex_unlock(&gasnetc_sbuf_lock);
      break;	/* Have a decsriptor - leave the loop */
    }
    pthread_mutex_unlock(&gasnetc_sbuf_lock);

    /* be kind */
    gasneti_sched_yield();
  }
  GASNETI_TRACE_WAIT_END(GET_SBUF);

  assert(sbuf != NULL);

  sbuf->next = NULL;
  sbuf->mem_oust = NULL;
  sbuf->req_oust = NULL;
  sbuf->addr = NULL;

  return sbuf;
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
  case gasnetc_System:
    /* currently all System AMs are shorts, they could be mediums later */
    /* fall through... */

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
    /* process loopback AM */
    gasnetc_rbuf_t	rbuf;

    rbuf.rr_sg.addr = (uintptr_t)buf;

    gasnetc_processPacket(&rbuf, flags);
    gasnetc_put_sbuf(sbuf, sbuf);
    retval = GASNET_OK;
  } else {
    /* send the AM */
    gasnetc_sreq_t req;
    gasnetc_cep_t *cep = &gasnetc_cep[dest];

#if GASNETC_AM_FLOWCTRL
    /* Requests require credit for flow control */
    if (isReq) {
      GASNETI_TRACE_WAIT_BEGIN();
      do {
        pthread_mutex_lock(&cep->lock);
        if_pt(gasneti_atomic_read(&cep->req_credits)) {
          gasneti_atomic_decrement(&cep->req_credits);
          pthread_mutex_unlock(&cep->lock);
          break;
        }
        pthread_mutex_unlock(&cep->lock);
        gasnetc_sndrcv_poll();
      } while (1);
      GASNETI_TRACE_WAIT_END(GET_AMREQ_CREDIT);
    }
#endif

    gasnetc_init_sreq(&req, sbuf);
    req.sr_desc.opcode     = VAPI_SEND_WITH_IMM;
    req.sr_desc.sg_lst_len = 1;
    req.sr_desc.imm_data   = flags;
    req.sr_desc.fence      = TRUE;
    req.sr_sg.addr         = (uintptr_t)buf;
    req.sr_sg.len          = msg_len;
    req.sr_sg.lkey         = gasnetc_snd_reg.lkey;

    gasnetc_snd_post(cep, &req);

    retval = GASNET_OK;
  }

  va_end(argptr);
  GASNETI_RETURN(retval);
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
	uint32_t flags = comp.imm_data;
	gasnetc_cep_t *cep = &gasnetc_cep[GASNETC_MSG_SRCIDX(flags)];

        #if GASNETC_AM_FLOWCTRL
          if (GASNETC_MSG_ISREPLY(flags)) {
            gasneti_atomic_increment(&cep->req_credits);
          }

          gasnetc_processPacket(rbuf, flags);

	  {
	    int needReply = rbuf->needReply;

            gasnetc_rcv_post(cep, rbuf);

            if (needReply) {
	      int retval;
              retval = gasnetc_ReplySystem((gasnet_token_t)rbuf, gasneti_handleridx(gasnetc_SYS_ack), 0 /* no args */);
	      assert(retval == GASNET_OK);
            }
	  }
	#else
          gasnetc_processPacket(rbuf, flags);
          gasnetc_rcv_post(cep, rbuf);
	#endif
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

#if GASNETC_RCV_THREAD
static void gasnetc_rcv_thread(VAPI_hca_hndl_t	hca_hndl,
			       VAPI_cq_hndl_t	cq_hndl,
			       void		*context) {
  VAPI_ret_t vstat;

  gasnetc_rcv_reap(0);

  vstat = VAPI_req_comp_notif(gasnetc_hca, gasnetc_rcv_cq, VAPI_NEXT_COMP);
  assert(vstat == VAPI_OK);

  gasnetc_rcv_reap(0);
}
#endif

/* ------------------------------------------------------------------------------------ *
 *  Externally visible functions                                                        *
 * ------------------------------------------------------------------------------------ */

extern void gasnetc_sndrcv_init(void) {
  VAPI_cqe_num_t	act_size;
  VAPI_ret_t		vstat;
  gasnetc_buffer_t	*buf;
  gasnetc_rbuf_t	*rbuf;
  gasnetc_sbuf_t	*sbuf;
  int 			count, i;

  if (gasnetc_nodes == 1) {
    /* Don't even bother to allocate zero-byte regions */
    return;
  }

  /* setup rcv resources */
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
  gasnetc_rbuf_alloc = gasnetc_rbuf_free = rbuf;

  vstat = VAPI_create_cq(gasnetc_hca, count, &gasnetc_rcv_cq, &act_size);
  assert(vstat == VAPI_OK);
  assert(act_size >= count);

  #if GASNETC_RCV_THREAD
    vstat = EVAPI_set_comp_eventh(gasnetc_hca, gasnetc_rcv_cq, &gasnetc_rcv_thread,
				  NULL, &gasnetc_rcv_handler);
    assert(vstat == VAPI_OK);
    vstat = VAPI_req_comp_notif(gasnetc_hca, gasnetc_rcv_cq, VAPI_NEXT_COMP);
    assert(vstat == VAPI_OK);
  #endif

  /* setup snd resources */
  count = MIN(GASNETC_SQ_SIZE, gasnetc_hca_cap.max_qp_ous_wr * (gasnetc_nodes - 1));
  buf = gasnetc_alloc_pinned(count * sizeof(gasnetc_buffer_t),
		             VAPI_EN_LOCAL_WRITE, &gasnetc_snd_reg);
  assert(buf != NULL);

  sbuf = calloc(count, sizeof(gasnetc_sbuf_t));
  assert(sbuf != NULL);

  for (i = 0; i < count; ++i) {
    sbuf[i].buffer = &buf[i];
    sbuf[i].next   = &sbuf[i + 1];
  }
  sbuf[count - 1].next = NULL;
  gasnetc_sbuf_alloc = gasnetc_sbuf_free = sbuf;

  vstat = VAPI_create_cq(gasnetc_hca, count, &gasnetc_snd_cq, &act_size);
  assert(vstat == VAPI_OK);
  assert(act_size >= count);
}

extern void gasnetc_sndrcv_init_cep(gasnetc_cep_t *cep) {
  int i;
  
  for (i = 0; i < GASNETC_RCV_WQE; ++i) {
    gasnetc_rbuf_t *rbuf = gasnetc_rbuf_free++;

    gasnetc_rcv_post(cep, rbuf);

    assert((gasnetc_rbuf_free - gasnetc_rbuf_alloc) <= (GASNETC_RCV_WQE * (gasnetc_nodes - 1)));
  }

  #if GASNETC_AM_FLOWCTRL
    pthread_mutex_init(&cep->lock, NULL);
    gasneti_atomic_set(&cep->req_credits, GASNETC_RCV_WQE / 2);
  #endif
}

extern void gasnetc_sndrcv_fini(void) {
  VAPI_ret_t vstat;

  if (gasnetc_nodes == 1) {
    /* Don't even bother with no peers */
    return;
  }

  #if GASNETC_RCV_THREAD
    vstat = EVAPI_clear_comp_eventh(gasnetc_hca, gasnetc_rcv_handler);
    assert(vstat == VAPI_OK);
  #endif

  vstat = VAPI_destroy_cq(gasnetc_hca, gasnetc_rcv_cq);
  assert(vstat == VAPI_OK);

  gasnetc_free_pinned(&gasnetc_rcv_reg);
  free(gasnetc_rbuf_alloc);

  vstat = VAPI_destroy_cq(gasnetc_hca, gasnetc_snd_cq);
  assert(vstat == VAPI_OK);

  gasnetc_free_pinned(&gasnetc_snd_reg);
  free(gasnetc_sbuf_alloc);
}

extern void gasnetc_sndrcv_poll(void) {
  gasnetc_sbuf_t *sbuf, *tail;

  #if GASNETC_RCV_POLL
    gasnetc_rcv_reap(GASNETC_RCV_REAP_LIMIT);
  #endif

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

      gasnetc_snd_inline_post(cep, &req);
      
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

      gasnetc_snd_post(cep, &req);
      
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

      gasnetc_snd_post(cep, &req);

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

    gasnetc_snd_post(cep, &req);

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

    gasnetc_snd_post(cep, &req);
     
    dst += count;
    nbytes -= count;
  } while (nbytes);

  return 0;
}

extern int gasnetc_RequestGeneric(gasnetc_category_t category,
				  int dest, gasnet_handler_t handler,
				  void *src_addr, int nbytes, void *dst_addr,
				  int numargs, gasneti_atomic_t *mem_oust, va_list argptr) {
  gasnetc_sndrcv_poll();	/* ensure progress */

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
  assert(GASNETC_MSG_ISREQUEST(rbuf->flags));
  assert(rbuf->needReply);

  retval = gasnetc_ReqRepGeneric(category, 0, GASNETC_MSG_SRCIDX(rbuf->flags), handler,
				 src_addr, nbytes, dst_addr,
				 numargs, mem_oust, argptr);

  rbuf->needReply = 0;
  return retval;
}

extern int gasnetc_RequestSystem(gasnet_node_t dest,
                                 gasnet_handler_t handler,
                                 int numargs, ...) {
  int retval;
  va_list argptr;

  GASNETI_TRACE_SYSTEM_REQUEST(dest,handler,numargs);

  va_start(argptr, numargs);
  retval = gasnetc_ReqRepGeneric(gasnetc_System, 1, dest, handler,
		  		 NULL, 0, NULL, numargs, NULL, argptr);
  va_end(argptr);
  return retval;
}

extern int gasnetc_ReplySystem(gasnet_token_t token,
                               gasnet_handler_t handler,
                               int numargs, ...) {
  gasnetc_rbuf_t *rbuf = (gasnetc_rbuf_t *)token;
  int retval;
  va_list argptr;
  gasnet_node_t dest;

  assert(rbuf);

  dest = GASNETC_MSG_SRCIDX(rbuf->flags);

  GASNETI_TRACE_SYSTEM_REPLY(dest,handler,numargs);

  va_start(argptr, numargs);
  retval = gasnetc_ReqRepGeneric(gasnetc_System, 0, dest, handler,
		  		 NULL, 0, NULL, numargs, NULL, argptr);
  va_end(argptr);
  return retval;
}

/* ------------------------------------------------------------------------------------ */
/*
  Misc. Active Message Functions
  ==============================
*/
extern int gasnetc_AMGetMsgSource(gasnet_token_t token, gasnet_node_t *srcindex) {
  gasnet_node_t sourceid;
  GASNETC_CHECKATTACH();
  if (!token) GASNETI_RETURN_ERRR(BAD_ARG,"bad token");
  if (!srcindex) GASNETI_RETURN_ERRR(BAD_ARG,"bad src ptr");

  sourceid = GASNETC_MSG_SRCIDX(((gasnetc_rbuf_t *)token)->flags);

  assert(sourceid < gasnetc_nodes);
  *srcindex = sourceid;
  return GASNET_OK;
}

extern int gasnetc_AMPoll() {
  GASNETC_CHECKATTACH();

  gasnetc_sndrcv_poll();

  return GASNET_OK;
}

/*  $Archive:: gasnet/gasnet-conduit/gasnet_core_sndrcv.c                  $
 *     $Date: 2003/07/03 22:21:04 $
 * $Revision: 1.2 $
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
#include <limits.h>

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
typedef struct _gasnetc_rbuf_t {
  VAPI_rr_desc_t        	rr_desc;        /* recv request descriptor */
  VAPI_sg_lst_entry_t   	rr_sg;          /* single-entry scatter list */

  /* Intialized at recv time: */
  int                   	needReply;
  int                   	handlerRunning;
  uint32_t              	flags;

  /* Linkage for the free list */
  struct _gasnetc_rbuf_t	*next;
} gasnetc_rbuf_t;

/* Description of a send buffer */
typedef struct _gasnetc_sbuf_t {
  struct _gasnetc_sbuf_t	*next;
  gasnetc_buffer_t		*buffer;

  /* Send semaphore for the corresponding CEP */
  gasnetc_sema_t		*send_sema;

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
static gasnetc_mutex_t			gasnetc_sbuf_lock = GASNETC_MUTEX_INITIALIZER;
static gasnetc_rbuf_t			*gasnetc_rbuf_alloc, *gasnetc_rbuf_free;
static gasnetc_mutex_t			gasnetc_rbuf_lock = GASNETC_MUTEX_INITIALIZER;
#if GASNETC_RCV_THREAD
  static EVAPI_compl_handler_hndl_t	gasnetc_rcv_handler;
#endif

/* ------------------------------------------------------------------------------------ *
 *  File-scoped functions and macros                                                    *
 * ------------------------------------------------------------------------------------ */


/* Use of IB's 32-bit immediate data:
 *   0-1: category
 *     2: request or reply
 *   3-7: numargs
 *  8-15: handerID
 * 16-29: source index (14 bit LID space in IB)
 *    30: credit bit
 *    31: UNUSED
 */

#define GASNETC_MSG_NUMARGS(flags)      (((flags) >> 3) & 0x1f)
#define GASNETC_MSG_ISREQUEST(flags)    (!((flags) & 0x4))
#define GASNETC_MSG_ISREPLY(flags)      (!!((flags) & 0x4))
#define GASNETC_MSG_CATEGORY(flags)     ((gasnetc_category_t)((flags) & 0x3))
#define GASNETC_MSG_HANDLERID(flags)    ((gasnet_handler_t)((flags) >> 8))
#define GASNETC_MSG_SRCIDX(flags)       ((gasnet_node_t)((flags) >> 16) & 0x3fff)
#define GASNETC_MSG_CREDIT(flags)       ((flags) & (1<<30))

#define GASNETC_MSG_GENFLAGS(isreq, cat, nargs, hand, srcidx, credit)   \
  (uint32_t)(  ((!!(credit)       ) << 30)      \
             | (((srcidx) & 0x3fff) << 16)      \
	     | (((hand)   & 0xff  ) << 8 )      \
	     | (((nargs)  & 0x1f  ) << 3 )      \
	     | ((!(isreq)         ) << 2 )      \
	     | (((cat)    & 0x3   )      ))


GASNET_INLINE_MODIFIER(gasnetc_get_rbuf)
gasnetc_rbuf_t *gasnetc_get_rbuf(void) {
  gasnetc_rbuf_t *rbuf;

  gasnetc_mutex_lock(&gasnetc_rbuf_lock, GASNETC_CLI_PAR);
  rbuf = gasnetc_rbuf_free;
  if (rbuf) {
    gasnetc_rbuf_free = rbuf->next;
  }
  gasnetc_mutex_unlock(&gasnetc_rbuf_lock, GASNETC_CLI_PAR);

  return rbuf;
}

GASNET_INLINE_MODIFIER(gasnetc_put_rbuf)
void gasnetc_put_rbuf(gasnetc_rbuf_t *rbuf) {
  if (rbuf) {
    gasnetc_mutex_lock(&gasnetc_rbuf_lock, GASNETC_CLI_PAR);
    rbuf->next = gasnetc_rbuf_free;
    gasnetc_rbuf_free = rbuf;
    gasnetc_mutex_unlock(&gasnetc_rbuf_lock, GASNETC_CLI_PAR);
  }
}

GASNET_INLINE_MODIFIER(gasnetc_pre_snd)
void gasnetc_pre_snd(gasnetc_cep_t *cep, gasnetc_sreq_t *req, gasnetc_sbuf_t *sbuf) {
  assert(cep);
  assert(req);
  assert(sbuf);

  /* check for attempted loopback traffic */
  assert(cep != &gasnetc_cep[gasnetc_mynode]);

  /* setup some invariant fields */
  req->sr_desc.id        = (uintptr_t)sbuf;
  req->sr_desc.comp_type = VAPI_SIGNALED;		/* XXX: is this correct? */
  req->sr_desc.sg_lst_p  = &req->sr_sg;
  req->sr_desc.set_se    = FALSE;			/* XXX: is this correct? */
  sbuf->send_sema = &(cep->send_sema);

  /* loop until space is available on the SQ */
  { 
    int first_try = 1;
    GASNETC_TRACE_WAIT_BEGIN();

    while (!gasnetc_sema_trydown(&(cep->send_sema), GASNETC_ANY_PAR)) {
      first_try = 0;
      gasnetc_snd_poll(); /* try to reap the resources we need */
    }
    if_pf (!first_try) {
      GASNETC_TRACE_WAIT_END(POST_SR_STALL);
    }
  }
}

/* Post a work request to the send queue of the given endpoint */
GASNET_INLINE_MODIFIER(gasnetc_snd_post)
void gasnetc_snd_post(gasnetc_cep_t *cep, gasnetc_sreq_t *req, gasnetc_sbuf_t *sbuf) {
  VAPI_ret_t vstat;

  gasnetc_pre_snd(cep, req, sbuf);

  vstat = VAPI_post_sr(gasnetc_hca, cep->qp_handle, &req->sr_desc);

  if_pt (vstat == VAPI_OK) {
    /* SUCCESS, the request is posted */
  } else if (vstat == VAPI_EINVAL_QP_HNDL) {
    /* race against disconnect in another thread (harmless) */
  } else {
    gasneti_fatalerror("Got unexpected VAPI error %s while posting a send work request.", VAPI_strerror_sym(vstat));
  }
}

/* Post an INLINE work request to the send queue of the given endpoint */
GASNET_INLINE_MODIFIER(gasnetc_snd_post_inline)
void gasnetc_snd_post_inline(gasnetc_cep_t *cep, gasnetc_sreq_t *req, gasnetc_sbuf_t *sbuf) {
  VAPI_ret_t vstat;

  gasnetc_pre_snd(cep, req, sbuf);

  vstat = EVAPI_post_inline_sr(gasnetc_hca, cep->qp_handle, &req->sr_desc);

  if_pt (vstat == VAPI_OK) {
    /* SUCCESS, the request is posted */
  } else if (vstat == VAPI_EINVAL_QP_HNDL) {
    /* race against disconnect in another thread (harmless) */
  } else {
    gasneti_fatalerror("Got unexpected VAPI error %s while posting a send work request.", VAPI_strerror_sym(vstat));
  }
}

/* Post a work request to the receive queue of the given endpoint */
GASNET_INLINE_MODIFIER(gasnetc_rcv_post)
void gasnetc_rcv_post(gasnetc_cep_t *cep, gasnetc_rbuf_t *rbuf, int credit) {
  VAPI_ret_t vstat;

  assert(cep);
  assert(rbuf);

  /* check for attempted loopback traffic */
  assert(cep != &gasnetc_cep[gasnetc_mynode]);
  
  vstat = VAPI_post_rr(gasnetc_hca, cep->qp_handle, &rbuf->rr_desc);
  if (credit) {
    gasnetc_sema_up(&cep->credit_sema);
    assert(gasneti_atomic_read(&(cep->credit_sema.count)) <= GASNETC_RCV_WQE / 2);
  }

  if_pt (vstat == VAPI_OK)
    return;
  if_pf (vstat == VAPI_EINVAL_QP_HNDL)
    return;	/* race against disconnect in another thread */
    
  gasneti_fatalerror("Got unexpected VAPI error %s while posting a receive work request.", VAPI_strerror_sym(vstat));
}

/* GASNET_INLINE_MODIFIER(gasnetc_processPacket) */
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
          GASNETC_TRACE_SYSTEM_REQHANDLER(handler_id, src, rbuf, numargs, args);
        else
          GASNETC_TRACE_SYSTEM_REPHANDLER(handler_id, src, rbuf, numargs, args);
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

/* free a list of send buffers */
GASNET_INLINE_MODIFIER(gasnetc_put_sbuf)
void gasnetc_put_sbuf(gasnetc_sbuf_t *head, gasnetc_sbuf_t *tail) {
  /* Add the list segment to the free list */
  gasnetc_mutex_lock(&gasnetc_sbuf_lock, GASNETC_ANY_PAR);
  tail->next = gasnetc_sbuf_free;
  gasnetc_sbuf_free = head;
  gasnetc_mutex_unlock(&gasnetc_sbuf_lock, GASNETC_ANY_PAR);
}

/* Try to pull completed entries from the send CQ (if any). */
GASNET_INLINE_MODIFIER(gasnetc_snd_reap)
gasnetc_sbuf_t *gasnetc_snd_reap(gasnetc_sbuf_t **tail_p) {
  gasnetc_sbuf_t *head, *tail;
  int count;
  
  head = tail = NULL;
  for (count = 0; count < GASNETC_SND_REAP_LIMIT; ++count) {
    VAPI_ret_t vstat;
    VAPI_wc_desc_t comp;

    #if 1
    {
      /* It seems that VAPI_poll_cq() is not thread-safe */
      static gasnetc_mutex_t poll_lock = GASNETC_MUTEX_INITIALIZER;
      gasnetc_mutex_lock(&poll_lock, GASNETC_ANY_PAR);
      vstat = VAPI_poll_cq(gasnetc_hca, gasnetc_snd_cq, &comp);
      gasnetc_mutex_unlock(&poll_lock, GASNETC_ANY_PAR);
    }
    #else
      vstat = VAPI_poll_cq(gasnetc_hca, gasnetc_snd_cq, &comp);
    #endif

    if (vstat == VAPI_OK) {
      if (comp.status == VAPI_SUCCESS) {
        gasnetc_sbuf_t *sbuf = (gasnetc_sbuf_t *)(uintptr_t)comp.id;
        if (sbuf) {
	  /* resource accounting */
	  gasnetc_sema_up(sbuf->send_sema);

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

  if (count)
    GASNETI_TRACE_EVENT_VAL(C,SND_REAP,count);

  *tail_p = tail;
  return head;
}

/* allocate a send buffer pair */
GASNET_INLINE_MODIFIER(gasnetc_get_sbuf)
gasnetc_sbuf_t *gasnetc_get_sbuf(void) {
  int first_try = 1;
  gasnetc_sbuf_t *sbuf, *tail;

  GASNETC_TRACE_WAIT_BEGIN();
  GASNETI_TRACE_EVENT(C,GET_SBUF);

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
    gasnetc_mutex_lock(&gasnetc_sbuf_lock, GASNETC_ANY_PAR);
    sbuf = gasnetc_sbuf_free;
    if (sbuf != NULL) {
      gasnetc_sbuf_free = sbuf->next;
      gasnetc_mutex_unlock(&gasnetc_sbuf_lock, GASNETC_ANY_PAR);
      break;	/* Have a decsriptor - leave the loop */
    }
    gasnetc_mutex_unlock(&gasnetc_sbuf_lock, GASNETC_ANY_PAR);

    /* be kind */
    gasneti_sched_yield();

    first_try = 0;
  }

  if (!first_try) {
    GASNETC_TRACE_WAIT_END(GET_SBUF_STALL);
  }

  assert(sbuf != NULL);

  sbuf->next = NULL;
  sbuf->mem_oust = NULL;
  sbuf->req_oust = NULL;
  sbuf->addr = NULL;

  return sbuf;
}

GASNET_INLINE_MODIFIER(gasnetc_ReqRepGeneric)
int gasnetc_ReqRepGeneric(gasnetc_category_t category, int isReq,
			  int credits_needed, int credits_granted,
			  int dest, gasnet_handler_t handler,
			  void *src_addr, int nbytes, void *dst_addr,
			  int numargs, gasneti_atomic_t *mem_oust, va_list argptr) {
  gasnetc_sbuf_t *sbuf;
  gasnetc_buffer_t *buf;
  gasnet_handlerarg_t *args;
  uint32_t flags;
  size_t msg_len;
  int retval, i;

  assert((credits_granted == 0) || (credits_granted == 1));

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
  flags = GASNETC_MSG_GENFLAGS(isReq, category, numargs, handler, gasnetc_mynode, credits_granted);

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

    while (credits_needed) {
      /* Requests require credit for flow control
       * Since the AM recv thread will never send a Request, it can't run here
       *
       * XXX: Note we should probably get the credit BEFORE we get the sbuf,
       * to avoid blocking the RDMA traffic (which also requires sbuf's).
       */
      int first_try = 1;
      GASNETC_TRACE_WAIT_BEGIN();
      GASNETI_TRACE_EVENT(C,GET_AMREQ_CREDIT);

      while (!gasnetc_sema_trydown(&cep->credit_sema, GASNETC_CLI_PAR)) {
        gasnetc_sndrcv_poll();
  	first_try = 0;
      }

      if (!first_try) {
        GASNETC_TRACE_WAIT_END(GET_AMREQ_CREDIT_STALL);
      }

      credits_needed--;
    }

    req.sr_desc.opcode     = VAPI_SEND_WITH_IMM;
    req.sr_desc.sg_lst_len = 1;
    req.sr_desc.imm_data   = flags;
    req.sr_desc.fence      = TRUE;
    req.sr_sg.addr         = (uintptr_t)buf;
    req.sr_sg.len          = msg_len;
    req.sr_sg.lkey         = gasnetc_snd_reg.lkey;

    gasnetc_snd_post(cep, &req, sbuf);

    retval = GASNET_OK;
  }

  va_end(argptr);
  GASNETI_RETURN(retval);
}

GASNET_INLINE_MODIFIER(gasnetc_rcv_am)
void gasnetc_rcv_am(const VAPI_wc_desc_t *comp, gasnetc_rbuf_t **spare_p) {
  gasnetc_rbuf_t *rbuf = (gasnetc_rbuf_t *)(uintptr_t)comp->id;
  uint32_t flags = comp->imm_data;
  gasnetc_cep_t *cep = &gasnetc_cep[GASNETC_MSG_SRCIDX(flags)];
  gasnetc_rbuf_t *spare;
  int needReply;

  /* If possible, post a replacement buffer right away. */
  spare = (*spare_p) ? (*spare_p) : gasnetc_get_rbuf();
  if_pt (spare) {
    /* This is the normal case, in which we have sufficient resources to post
     * a replacement buffer before processing the recv'd buffer.  That way
     * we are certain that a buffer is in-place before the potential reply
     * sends a credit to our peer, which then could use the buffer
     */
    gasnetc_rcv_post(cep, spare, GASNETC_MSG_CREDIT(flags));
    *spare_p = rbuf;	/* recv'd rbuf becomes the spare for next pass (if any) */
  } else {
    /* This is the reduced-performance case.  Because we don't have any "spare" rbuf
     * available to post, there is the possibility that a "bad" sequence of events could
     * take place:
     *   1) Assume that all the rbuf posted to the cep have been consumed by AMs
     *   2) Assume the current AM is a request
     *   3) Assume the request handler sends a reply
     *  then     
     *   4) The peer receives the reply and thus receives a credit
     *   5) The credit allows the peer to send us another AM request
     *   6) This additional request arrives before the current request handler completes
     *      (which would allow the current rbuf to be reposted to the cep).
     *   7) The HCA is unable, until the current rbuf is reposted, to complete the transfer
     *   8) IB's RNR (Reciever Not Ready) flow-control kicks in, stalling our peer's send queue
     *  Fortunetely this is not only an unlikely occurance, it is also non-fatal.
     *  Once the current AM request handler completes (which it must do in finite time without
     *  depending on external events) the rbuf will be reposted and the stall will end.
     */
  }

  /* Now process the packet */
  gasnetc_processPacket(rbuf, flags);

  /* Finalize flow control */
  needReply = rbuf->needReply;
  if_pf (!spare) {
    /* This is the fallback (reduced performance) case.
     * Because no replacement rbuf was posted earlier, we must repost the recv'd rbuf now.
     */
    gasnetc_rcv_post(cep, rbuf, GASNETC_MSG_CREDIT(flags));
  }
  if_pf (needReply) {
    int retval = gasnetc_ReplySystem((gasnet_token_t)rbuf, 1, gasneti_handleridx(gasnetc_SYS_ack), 0 /* no args */);
    assert(retval == GASNET_OK);
  }
}

GASNET_INLINE_MODIFIER(gasnetc_rcv_reap)
void gasnetc_rcv_reap(int limit, gasnetc_rbuf_t **spare_p) {
  VAPI_ret_t vstat;
  int count;

  for (count = 0; count < limit; ++count) {
    VAPI_wc_desc_t comp;

    #if 1
    {
      /* It seems that VAPI_poll_cq() is not thread-safe */
      static gasnetc_mutex_t poll_lock = GASNETC_MUTEX_INITIALIZER;
      gasnetc_mutex_lock(&poll_lock, GASNETC_ANY_PAR);
      vstat = VAPI_poll_cq(gasnetc_hca, gasnetc_rcv_cq, &comp);
      gasnetc_mutex_unlock(&poll_lock, GASNETC_ANY_PAR);
    }
    #else
      vstat = VAPI_poll_cq(gasnetc_hca, gasnetc_rcv_cq, &comp);
    #endif

    if (vstat == VAPI_OK) {
      if (comp.status == VAPI_SUCCESS) {
        gasnetc_rcv_am(&comp, spare_p);
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

  if (count)
    GASNETI_TRACE_EVENT_VAL(C,RCV_REAP,count);
}

#if GASNETC_RCV_THREAD
static gasnetc_rbuf_t *gasnetc_rcv_thread_rbuf = NULL;
static void gasnetc_rcv_thread(VAPI_hca_hndl_t	hca_hndl,
			       VAPI_cq_hndl_t	cq_hndl,
			       void		*context) {
  VAPI_ret_t vstat;

  gasnetc_rcv_reap(INT_MAX, &gasnetc_rcv_thread_rbuf);

  vstat = VAPI_req_comp_notif(gasnetc_hca, gasnetc_rcv_cq, VAPI_NEXT_COMP);
  assert(vstat == VAPI_OK);

  gasnetc_rcv_reap(INT_MAX, &gasnetc_rcv_thread_rbuf);
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

  /*
   * setup RCV resources
   */
  count = GASNETC_RCV_WQE * (gasnetc_nodes - 1) + GASNETC_RCV_SPARES;
  assert(count <= GASNETC_RCV_CQ_SIZE);

  /* create the RCV CQ */
  vstat = VAPI_create_cq(gasnetc_hca, count, &gasnetc_rcv_cq, &act_size);
  assert(vstat == VAPI_OK);
  assert(act_size >= count);

  if (gasnetc_nodes > 1) {
    #if GASNETC_RCV_THREAD
      /* create the RCV thread */
      vstat = EVAPI_set_comp_eventh(gasnetc_hca, gasnetc_rcv_cq, &gasnetc_rcv_thread,
				    NULL, &gasnetc_rcv_handler);
      assert(vstat == VAPI_OK);
      vstat = VAPI_req_comp_notif(gasnetc_hca, gasnetc_rcv_cq, VAPI_NEXT_COMP);
      assert(vstat == VAPI_OK);
    #endif

    /* Allocated pinned memory for receive buffers */
    buf = gasnetc_alloc_pinned(count * sizeof(gasnetc_buffer_t),
			       VAPI_EN_LOCAL_WRITE, &gasnetc_rcv_reg);
    assert(buf != NULL);

    /* Allocated normal memory for receive descriptors (rbuf's) */
    rbuf = calloc(count, sizeof(gasnetc_rbuf_t));
    assert(rbuf != NULL);

    /* Initialize the rbuf's */
    for (i = 0; i < count; ++i) {
      rbuf[i].rr_desc.id         = (uintptr_t)&rbuf[i];	/* CQE will point back to this request */
      rbuf[i].rr_desc.opcode     = VAPI_RECEIVE;
      rbuf[i].rr_desc.comp_type  = VAPI_SIGNALED;	/* XXX: is this right? */
      rbuf[i].rr_desc.sg_lst_len = 1;
      rbuf[i].rr_desc.sg_lst_p   = &rbuf[i].rr_sg;
      rbuf[i].rr_sg.len          = GASNETC_BUFSZ;
      rbuf[i].rr_sg.addr         = (uintptr_t)&buf[i];
      rbuf[i].rr_sg.lkey         = gasnetc_rcv_reg.lkey;
      rbuf[i].next               = &rbuf[i + 1];
    }
    rbuf[count - 1].next = NULL;
    gasnetc_rbuf_alloc = gasnetc_rbuf_free = rbuf;
    #if GASNETC_RCV_THREAD
      gasnetc_rcv_thread_rbuf = gasnetc_get_rbuf();
    #endif
  }

  /*
   * setup SND resources
   */
  count = MIN(GASNETC_SND_CQ_SIZE, GASNETC_SND_WQE * gasnetc_nodes);

  /* create the SND CQ */
  vstat = VAPI_create_cq(gasnetc_hca, count, &gasnetc_snd_cq, &act_size);
  assert(vstat == VAPI_OK);
  assert(act_size >= count);

  /* Allocated pinned memory for bounce buffers */
  buf = gasnetc_alloc_pinned(count * sizeof(gasnetc_buffer_t),
		             VAPI_EN_LOCAL_WRITE, &gasnetc_snd_reg);
  assert(buf != NULL);

  /* Allocated normal memory for send descriptors (sbuf's) */
  sbuf = calloc(count, sizeof(gasnetc_sbuf_t));
  assert(sbuf != NULL);

  /* Initialize the sbuf's */
  for (i = 0; i < count; ++i) {
    sbuf[i].buffer = &buf[i];
    sbuf[i].next   = &sbuf[i + 1];
  }
  sbuf[count - 1].next = NULL;
  gasnetc_sbuf_alloc = gasnetc_sbuf_free = sbuf;
}

extern void gasnetc_sndrcv_init_cep(gasnetc_cep_t *cep) {
  int i;
  
  for (i = 0; i < GASNETC_RCV_WQE; ++i) {
    gasnetc_rcv_post(cep, gasnetc_get_rbuf(), 0);
  }

  gasnetc_sema_init(&cep->credit_sema, GASNETC_RCV_WQE / 2);
  gasnetc_sema_init(&cep->send_sema, GASNETC_SND_WQE);
}

extern void gasnetc_sndrcv_fini(void) {
  VAPI_ret_t vstat;

  if (gasnetc_nodes > 1) {
    #if GASNETC_RCV_THREAD
      vstat = EVAPI_clear_comp_eventh(gasnetc_hca, gasnetc_rcv_handler);
      assert(vstat == VAPI_OK);
    #endif

    gasnetc_free_pinned(&gasnetc_rcv_reg);
    free(gasnetc_rbuf_alloc);

    gasnetc_free_pinned(&gasnetc_snd_reg);
    free(gasnetc_sbuf_alloc);
  }

  vstat = VAPI_destroy_cq(gasnetc_hca, gasnetc_rcv_cq);
  assert(vstat == VAPI_OK);

  vstat = VAPI_destroy_cq(gasnetc_hca, gasnetc_snd_cq);
  assert(vstat == VAPI_OK);
}

extern void gasnetc_sndrcv_poll(void) {
  #if GASNETC_RCV_POLL
  {
    gasnetc_rbuf_t *spare = NULL;
    gasnetc_rcv_reap(GASNETC_RCV_REAP_LIMIT, &spare);
    gasnetc_put_rbuf(spare);
  }
  #endif

  {
    gasnetc_sbuf_t *sbuf, *tail;

    sbuf = gasnetc_snd_reap(&tail);

    if (sbuf) {
      gasnetc_put_sbuf(sbuf, tail);
    }
  }
}

extern void gasnetc_snd_poll(void) {
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

      gasnetc_snd_post_inline(cep, &req, sbuf);
      
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

      gasnetc_snd_post(cep, &req, sbuf);
      
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

      gasnetc_snd_post(cep, &req, sbuf);

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

    gasnetc_snd_post(cep, &req, sbuf);

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

    gasnetc_snd_post(cep, &req, sbuf);
     
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

  return gasnetc_ReqRepGeneric(category, 1, /* need */ 1, /* grant */ 0,
			       dest, handler,
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

  retval = gasnetc_ReqRepGeneric(category, 0, /* need */ 0, /* grant */ 1,
		                 GASNETC_MSG_SRCIDX(rbuf->flags), handler,
				 src_addr, nbytes, dst_addr,
				 numargs, mem_oust, argptr);

  rbuf->needReply = 0;
  return retval;
}

extern int gasnetc_RequestSystem(gasnet_node_t dest,
				 int credits_needed,
                                 gasnet_handler_t handler,
                                 int numargs, ...) {
  int retval;
  va_list argptr;

  gasnetc_sndrcv_poll();	/* ensure progress (should this really be done _here_?) */

  GASNETC_TRACE_SYSTEM_REQUEST(dest,handler,numargs);

  va_start(argptr, numargs);
  retval = gasnetc_ReqRepGeneric(gasnetc_System, 1, credits_needed, /* grant */ 0,
		  		 dest, handler, NULL, 0, NULL, numargs, NULL, argptr);
  va_end(argptr);
  return retval;
}

extern int gasnetc_ReplySystem(gasnet_token_t token,
			       int credits_granted,
                               gasnet_handler_t handler,
                               int numargs, ...) {
  gasnetc_rbuf_t *rbuf = (gasnetc_rbuf_t *)token;
  int retval;
  va_list argptr;
  gasnet_node_t dest;

  assert(rbuf);

  dest = GASNETC_MSG_SRCIDX(rbuf->flags);

  GASNETC_TRACE_SYSTEM_REPLY(dest,handler,numargs);

  va_start(argptr, numargs);
  retval = gasnetc_ReqRepGeneric(gasnetc_System, 0, /* need */ 0, credits_granted,
		  		 dest, handler, NULL, 0, NULL, numargs, NULL, argptr);
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

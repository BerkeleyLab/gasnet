/*  $Archive:: gasnet/gasnet-conduit/gasnet_core_sndrcv.c                  $
 *     $Date: 2003/09/06 02:22:34 $
 * $Revision: 1.23 $
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
 *  Configuration                                                                       *
 * ------------------------------------------------------------------------------------ */

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
  gasneti_freelist_ptr_t	linkage;	/* Linkage for the free list */
  VAPI_rr_desc_t        	rr_desc;        /* recv request descriptor */
  VAPI_sg_lst_entry_t   	rr_sg;          /* single-entry scatter list */

  /* Intialized at recv time: */
  int                   	needReply;
  int                   	handlerRunning;
  uint32_t              	flags;
} gasnetc_rbuf_t;

/* Description of a send buffer */
typedef struct {
  gasneti_freelist_ptr_t	linkage;	/* Linkage for the free list */

  gasnetc_buffer_t		*buffer;

  /* RDMA Op semaphore for the corresponding CEP */
  gasnetc_sema_t		*op_sema;

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
  VAPI_sg_lst_entry_t	sr_sg[GASNETC_SND_SG];	/* send request gather list */
} gasnetc_sreq_t;

/* ------------------------------------------------------------------------------------ *
 *  File-scoped variables
 * ------------------------------------------------------------------------------------ */

static void				*gasnetc_sbuf_alloc;
static void				*gasnetc_rbuf_alloc;
static gasneti_freelist_t		gasnetc_sbuf_freelist = GASNETI_FREELIST_INITIALIZER;
static gasneti_freelist_t		gasnetc_rbuf_freelist = GASNETI_FREELIST_INITIALIZER;
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

/* Work around apparent thread-safety bug in VAPI_poll_cq (and peek as well?) */
#if 1
  static gasnetc_mutex_t gasnetc_cq_poll_lock = GASNETC_MUTEX_INITIALIZER;
  #define CQ_LOCK	gasnetc_mutex_lock(&gasnetc_cq_poll_lock, GASNETC_ANY_PAR);
  #define CQ_UNLOCK	gasnetc_mutex_unlock(&gasnetc_cq_poll_lock, GASNETC_ANY_PAR);
#else
  #define CQ_LOCK
  #define CQ_UNLOCK
#endif

#define gasnetc_poll_cq(CQ, COMP_P)	VAPI_poll_cq(gasnetc_hca, (CQ), (COMP_P))
#define gasnetc_peek_cq(CQ, N)		EVAPI_peek_cq(gasnetc_hca, (CQ), (N))

#define gasnetc_poll_rcv()		gasnetc_do_poll(1,0)
#define gasnetc_poll_snd()		gasnetc_do_poll(0,1)
#define gasnetc_poll_both()		gasnetc_do_poll(1,1)

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
    gasnetc_sema_up(&cep->am_sema);
    GASNETC_SEMA_CHECK(&(cep->am_sema), gasnetc_am_oust_pp);
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
	args = buf->shortmsg.args;
        if (GASNETC_MSG_ISREQUEST(flags))
          GASNETC_TRACE_SYSTEM_REQHANDLER(handler_id, rbuf, numargs, args);
        else
          GASNETC_TRACE_SYSTEM_REPHANDLER(handler_id, rbuf, numargs, args);
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

/* Try to pull completed entries from the send CQ (if any). */
static int gasnetc_snd_reap(int limit, gasnetc_sbuf_t **head_p, gasnetc_sbuf_t **tail_p) {
  VAPI_ret_t vstat;
  VAPI_wc_desc_t comp;
  gasnetc_sbuf_t dummy;
  gasnetc_sbuf_t *tail = &dummy;
  int count;
  
  for (count = 0; count < limit; ++count) {
    CQ_LOCK;
    vstat = gasnetc_poll_cq(gasnetc_snd_cq, &comp);
    CQ_UNLOCK;

    if_pt (vstat == VAPI_CQ_EMPTY) {
      /* CQ empty - we are done */
      break;
    } else if_pt (vstat == VAPI_OK) {
      if_pt (comp.status == VAPI_SUCCESS) {
        gasnetc_sbuf_t *sbuf = (gasnetc_sbuf_t *)(uintptr_t)comp.id;
        if_pt (sbuf) {
	  /* resource accounting */
	  gasnetc_sema_up(sbuf->op_sema);
          GASNETC_SEMA_CHECK(sbuf->op_sema, gasnetc_op_oust_pp);

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
	  gasneti_freelist_link(tail, sbuf);
	  tail = sbuf;
        } else {
          fprintf(stderr, "@ %d> snd_reap reaped NULL sbuf\n", gasnetc_mynode);
          break;
        }
      } else {
#if 1 
        fprintf(stderr, "@ %d> snd comp.status=%d comp.opcode=%d\n", gasnetc_mynode, comp.status, comp.opcode);
        while((vstat = VAPI_poll_cq(gasnetc_hca, gasnetc_rcv_cq, &comp)) == VAPI_OK) {
          fprintf(stderr, "@ %d> - rcv comp.status=%d\n", gasnetc_mynode, comp.status);
        }
#endif
        /* ### What needs to be done here? */
        break;
      }
    } else {
      assert(0);
    }
  }

  if (count)
    GASNETC_STAT_EVENT_VAL(SND_REAP,count);

  /* The following is unneccesary when count == 0, but we rather avoid the branch
   * knowing the caller won't look at these in that case. */
  gasneti_freelist_link(tail, NULL);
  *head_p = gasneti_freelist_next(&dummy);
  *tail_p = tail;

  return count;
}

/* allocate a send buffer pair */
GASNET_INLINE_MODIFIER(gasnetc_get_sbuf)
gasnetc_sbuf_t *gasnetc_get_sbuf(void) {
  int first_try = 1;
  gasnetc_sbuf_t *sbuf;

  GASNETC_TRACE_WAIT_BEGIN();
  GASNETC_STAT_EVENT(GET_SBUF);

  while (1) {
    /* try to get an unused sbuf by reaping the send CQ */
    gasnetc_sbuf_t *tail;
    int count = gasnetc_snd_reap(1, &sbuf, &tail);
    if_pt (count > 0) {
      assert(count == 1);
      break;	/* Have an sbuf - leave the loop */
    }

    /* try to get an unused sbuf from the free list */
    sbuf = gasneti_freelist_get(&gasnetc_sbuf_freelist);
    if_pt (sbuf != NULL) {
      break;	/* Have an sbuf - leave the loop */
    }

    /* be kind */
    gasneti_sched_yield();

    first_try = 0;
  }

  if (!first_try) {
    GASNETC_TRACE_WAIT_END(GET_SBUF_STALL);
  }

  assert(sbuf != NULL);

  sbuf->mem_oust = NULL;
  sbuf->req_oust = NULL;
  sbuf->addr = NULL;

  return sbuf;
}

GASNET_INLINE_MODIFIER(gasnetc_rcv_am)
void gasnetc_rcv_am(const VAPI_wc_desc_t *comp, gasnetc_rbuf_t **spare_p) {
  gasnetc_rbuf_t *rbuf = (gasnetc_rbuf_t *)(uintptr_t)comp->id;
  uint32_t flags = comp->imm_data;
  gasnetc_cep_t *cep = &gasnetc_cep[GASNETC_MSG_SRCIDX(flags)];
  gasnetc_rbuf_t *spare;

  /* If possible, post a replacement buffer right away. */
  spare = (*spare_p) ? (*spare_p) : gasneti_freelist_get(&gasnetc_rbuf_freelist);
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
     *   3) Either the request handler or this function sends a reply
     *  then     
     *   4) The peer receives the reply and thus receives a credit
     *   5) The credit allows the peer to send us another AM request
     *   6) This additional request arrives before the current rbuf is reposted to the cep
     *   7) The HCA is unable, until the current rbuf is reposted, to complete the transfer
     *   8) IB's RNR (Reciever Not Ready) flow-control kicks in, stalling our peer's send queue
     *  Fortunetely this is not only an unlikely occurance, it is also non-fatal.
     *  Eventually, the rbuf will be reposted and the stall will end.
     */
  }

  /* Now process the packet */
  gasnetc_processPacket(rbuf, flags);

  /* Finalize flow control */
  if_pf (rbuf->needReply) {
    int retval = gasnetc_ReplySystem((gasnet_token_t)rbuf, 1, gasneti_handleridx(gasnetc_SYS_ack), 0 /* no args */);
    assert(retval == GASNET_OK);
  }
  if_pf (!spare) {
    /* This is the fallback (reduced performance) case.
     * Because no replacement rbuf was posted earlier, we must repost the recv'd rbuf now.
     */
    gasnetc_rcv_post(cep, rbuf, GASNETC_MSG_CREDIT(flags));
  }
}

static int gasnetc_rcv_reap(int limit, gasnetc_rbuf_t **spare_p) {
  VAPI_ret_t vstat;
  VAPI_wc_desc_t comp;
  int count;

  for (count = 0; count < limit; ++count) {
    CQ_LOCK;
    vstat = gasnetc_poll_cq(gasnetc_rcv_cq, &comp);
    CQ_UNLOCK;

    if_pt (vstat == VAPI_CQ_EMPTY) {
      /* CQ empty - we are done */
      break;
    } else if_pt (vstat == VAPI_OK) {
      if_pt (comp.status == VAPI_SUCCESS) {
        gasnetc_rcv_am(&comp, spare_p);
      } else {
#if 1
        fprintf(stderr, "@ %d> rcv comp.status=%d\n", gasnetc_mynode, comp.status);
        while((vstat = VAPI_poll_cq(gasnetc_hca, gasnetc_snd_cq, &comp)) == VAPI_OK) {
          fprintf(stderr, "@ %d> - snd comp.status=%d\n", gasnetc_mynode, comp.status);
        }
#endif
        /* ### What needs to be done here? */
	break;
        assert(0);
      }
    } else {
      assert(0);
    }
  } 

  if (count)
    GASNETC_STAT_EVENT_VAL(RCV_REAP,count);

  return count;
}

GASNET_INLINE_MODIFIER(gasnetc_do_poll)
void gasnetc_do_poll(int poll_rcv, int poll_snd) {
  if (poll_rcv) {
    gasnetc_rbuf_t *spare = NULL;

    (void)gasnetc_rcv_reap(GASNETC_RCV_REAP_LIMIT, &spare);
    if (spare) {
      gasneti_freelist_put(&gasnetc_rbuf_freelist, spare);
    }
  }

  if (poll_snd) {
    gasnetc_sbuf_t *head, *tail;
    int count;

    count = gasnetc_snd_reap(GASNETC_SND_REAP_LIMIT, &head, &tail);
    if (count > 0) {
	gasneti_freelist_put_many(&gasnetc_sbuf_freelist, head, tail);
    }
  }
}

GASNET_INLINE_MODIFIER(gasnetc_pre_snd)
void gasnetc_pre_snd(gasnetc_cep_t *cep, gasnetc_sreq_t *req, gasnetc_sbuf_t *sbuf) {
  assert(cep);
  assert(req);
  assert(sbuf);

  /* check for attempted loopback traffic */
  assert(cep != &gasnetc_cep[gasnetc_mynode]);

  GASNETC_STAT_EVENT(POST_SR);

  /* check for reasonable message sizes
   * With SEND 0-bytes triggers a Mellanox bug
   * With RDMA ops, 0-bytes makes no sense.
   */
  #ifdef DEBUG
  {
    u_int32_t	sum = 0;
    int i;

    for (i = 0; i < GASNETC_SND_SG; ++i) {
      sum += req->sr_sg[i].len;
      assert(req->sr_sg[i].len != 0);
      assert(req->sr_sg[i].len <= gasnetc_hca_port.max_msg_sz);
      assert(req->sr_sg[i].len <= sum); /* check for overflow of 'sum' */
    }

    assert(sum <= gasnetc_hca_port.max_msg_sz);
  }
  #endif

  /* setup some invariant fields */
  req->sr_desc.id        = (uintptr_t)sbuf;
  req->sr_desc.comp_type = VAPI_SIGNALED;		/* XXX: is this correct? */
  req->sr_desc.sg_lst_p  = req->sr_sg;
  req->sr_desc.set_se    = FALSE;			/* XXX: is this correct? */
  sbuf->op_sema = &(cep->op_sema);

  /* loop until space is available on the SQ */
  if_pf (!gasnetc_sema_trydown(&cep->op_sema, GASNETC_ANY_PAR)) {
    GASNETC_TRACE_WAIT_BEGIN();
    do {
        gasnetc_poll_snd();
    } while (!gasnetc_sema_trydown(&cep->op_sema, GASNETC_ANY_PAR));
    GASNETC_TRACE_WAIT_END(POST_SR_STALL);
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

#if GASNETC_RCV_THREAD
static gasnetc_rbuf_t *gasnetc_rcv_thread_rbuf = NULL;
static void gasnetc_rcv_thread(VAPI_hca_hndl_t	hca_hndl,
			       VAPI_cq_hndl_t	cq_hndl,
			       void		*context) {
  VAPI_ret_t vstat;

  (void)gasnetc_rcv_reap(INT_MAX, &gasnetc_rcv_thread_rbuf);

  vstat = VAPI_req_comp_notif(gasnetc_hca, gasnetc_rcv_cq, VAPI_NEXT_COMP);
  assert(vstat == VAPI_OK);

  (void)gasnetc_rcv_reap(INT_MAX, &gasnetc_rcv_thread_rbuf);
}
#endif

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
  int use_inline = 0;

  assert(!isReq || (credits_granted == 0));
  assert((credits_granted == 0) || (credits_granted == 1));
  assert(isReq || (credits_needed == 0));
  assert((credits_needed == 0) || (credits_needed == 1));

  /* FIRST, get any flow-control credits needed for AM Requests.
   * This way we can be sure that we never hold the last sbuf while spinning on
   * the rcv queue waiting for credits.
   */
  if (credits_needed) {
    gasnetc_sema_t *sema = &gasnetc_cep[dest].am_sema;
    GASNETC_STAT_EVENT(GET_AMREQ_CREDIT);

    /* XXX: We don't want to deal with liveness and fairness issues for multi-credit
     * requests until we have an actual need for them.  Thus this assertion: */
    assert(credits_needed == 1);

    if_pf (!gasnetc_sema_trydown(sema, GASNETC_ANY_PAR)) {
      GASNETC_TRACE_WAIT_BEGIN();
      do {
        gasnetc_poll_rcv();
      } while (!gasnetc_sema_trydown(sema, GASNETC_ANY_PAR));
      GASNETC_TRACE_WAIT_END(GET_AMREQ_CREDIT_STALL);
    }
  }

  /* Now get an sbuf and start building the message */
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
    use_inline = (sizeof(gasnetc_shortmsg_t) <= GASNETC_AM_INLINE_LIMIT);
    break;

  case gasnetc_Medium:
    args = buf->medmsg.args;
    buf->medmsg.nBytes = nbytes;
    memcpy(GASNETC_MSG_MED_DATA(buf, numargs), src_addr, nbytes);
    msg_len = GASNETC_MSG_MED_OFFSET(numargs) + nbytes;
    use_inline = ((GASNETC_AM_INLINE_LIMIT != 0) && (msg_len <= GASNETC_AM_INLINE_LIMIT));
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
    use_inline = (sizeof(gasnetc_longmsg_t) <= GASNETC_AM_INLINE_LIMIT);
    break;

  default:
    assert(0);
    /* NOT REACHED */
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
    gasneti_freelist_put(&gasnetc_sbuf_freelist, sbuf);
    retval = GASNET_OK;
  } else {
    /* send the AM */
    gasnetc_sreq_t req;

    req.sr_desc.opcode     = VAPI_SEND_WITH_IMM;
    req.sr_desc.sg_lst_len = 1;
    req.sr_desc.imm_data   = flags;
    req.sr_desc.fence      = TRUE;
    req.sr_sg[0].addr      = (uintptr_t)buf;
    req.sr_sg[0].len       = msg_len;
    req.sr_sg[0].lkey      = gasnetc_snd_reg.lkey;

    if_pt (use_inline) {
      gasnetc_snd_post_inline(&gasnetc_cep[dest], &req, sbuf);
    } else {
      gasnetc_snd_post(&gasnetc_cep[dest], &req, sbuf);
    }

    retval = GASNET_OK;
  }

  va_end(argptr);
  GASNETI_RETURN(retval);
}

/* Helper for rdma puts: inline send case */
static void gasnetc_do_put_inline(gasnetc_cep_t *cep, VAPI_rkey_t rkey,
                                  uintptr_t src, uintptr_t dst, size_t nbytes,
                                  gasneti_atomic_t *req_oust) {
  gasnetc_sbuf_t *sbuf;
  gasnetc_sreq_t req;

  GASNETI_TRACE_EVENT_VAL(C, RDMA_PUT_INLINE, nbytes);

  assert(nbytes != 0);
  assert(nbytes <= GASNETC_PUT_INLINE_LIMIT);

  sbuf = gasnetc_get_sbuf();

  req.sr_desc.opcode      = VAPI_RDMA_WRITE;
  req.sr_desc.sg_lst_len  = 1;
  req.sr_desc.fence       = TRUE;
  req.sr_desc.remote_addr = dst;
  req.sr_desc.r_key       = rkey;
  req.sr_sg[0].addr       = src;
  req.sr_sg[0].len        = nbytes;

  if (req_oust) {
    gasneti_atomic_increment(req_oust);
    sbuf->req_oust = req_oust;
  }

  gasnetc_snd_post_inline(cep, &req, sbuf);
}
      
/* Helper for rdma puts: bounce buffer case */
static void gasnetc_do_put_bounce(gasnetc_cep_t *cep, VAPI_rkey_t rkey,
                                  uintptr_t src, uintptr_t dst, size_t nbytes,
                                  gasneti_atomic_t *req_oust) {
  gasnetc_sbuf_t *sbuf;
  gasnetc_sreq_t req;

  GASNETI_TRACE_EVENT_VAL(C, RDMA_PUT_BOUNCE, nbytes);

  assert(nbytes != 0);

  /* Use full bounce buffers until just one buffer worth of data remains */
  while (nbytes > GASNETC_BUFSZ) {
    sbuf = gasnetc_get_sbuf();

    req.sr_desc.opcode      = VAPI_RDMA_WRITE;
    req.sr_desc.sg_lst_len  = 1;
    req.sr_desc.fence       = TRUE;
    req.sr_desc.remote_addr = dst;
    req.sr_desc.r_key       = rkey;

    memcpy(sbuf->buffer, (void *)src, GASNETC_BUFSZ);
    req.sr_sg[0].addr = (uintptr_t)sbuf->buffer;
    req.sr_sg[0].lkey = gasnetc_snd_reg.lkey;
    req.sr_sg[0].len  = GASNETC_BUFSZ;

    gasnetc_snd_post(cep, &req, sbuf);

    src += GASNETC_BUFSZ;
    dst += GASNETC_BUFSZ;
    nbytes -= GASNETC_BUFSZ;
  }

  /* Send out the last buffer w/ the counter (if any) advanced */
  assert(nbytes <= GASNETC_BUFSZ);

  sbuf = gasnetc_get_sbuf();

  req.sr_desc.opcode      = VAPI_RDMA_WRITE;
  req.sr_desc.sg_lst_len  = 1;
  req.sr_desc.fence       = TRUE;
  req.sr_desc.remote_addr = dst;
  req.sr_desc.r_key       = rkey;

  memcpy(sbuf->buffer, (void *)src, nbytes);
  req.sr_sg[0].addr = (uintptr_t)sbuf->buffer;
  req.sr_sg[0].lkey = gasnetc_snd_reg.lkey;
  req.sr_sg[0].len  = nbytes;

  if (req_oust) {
    gasneti_atomic_increment(req_oust);
    sbuf->req_oust = req_oust;
  }

  gasnetc_snd_post(cep, &req, sbuf);
}

/* Helper for rdma puts: zero copy case */
static void gasnetc_do_put_zerocp(gasnetc_cep_t *cep, VAPI_lkey_t lkey, VAPI_rkey_t rkey,
                                  uintptr_t src, uintptr_t dst, size_t nbytes,
                                  gasneti_atomic_t *mem_oust, gasneti_atomic_t *req_oust) {
  gasnetc_sbuf_t *sbuf;
  gasnetc_sreq_t req;
  size_t max_sz = gasnetc_hca_port.max_msg_sz;

  GASNETI_TRACE_EVENT_VAL(C, RDMA_PUT_ZEROCP, nbytes);

  assert(nbytes != 0);

  /* Use max-sized messages until just one msg worth of data remains */
  if_pf (nbytes > max_sz) {
    do {
      sbuf = gasnetc_get_sbuf();

      req.sr_desc.opcode      = VAPI_RDMA_WRITE;
      req.sr_desc.sg_lst_len  = 1;
      req.sr_desc.fence       = TRUE;
      req.sr_desc.remote_addr = dst;
      req.sr_desc.r_key       = rkey;

      req.sr_sg[0].addr = src;
      req.sr_sg[0].lkey = lkey;
      req.sr_sg[0].len  = max_sz;

      gasnetc_snd_post(cep, &req, sbuf);

      src += max_sz;
      dst += max_sz;
      nbytes -= max_sz;
    } while (nbytes > max_sz);
  }

  /* Send out the last buffer w/ the counters (if any) advanced */
  assert(nbytes <= max_sz);

  sbuf = gasnetc_get_sbuf();

  req.sr_desc.opcode      = VAPI_RDMA_WRITE;
  req.sr_desc.sg_lst_len  = 1;
  req.sr_desc.fence       = TRUE;
  req.sr_desc.remote_addr = dst;
  req.sr_desc.r_key       = rkey;

  req.sr_sg[0].addr = src;
  req.sr_sg[0].lkey = lkey;
  req.sr_sg[0].len  = nbytes;

  if (mem_oust) {
    gasneti_atomic_increment(mem_oust);
    sbuf->mem_oust = mem_oust;
  }
  if (req_oust) {
    gasneti_atomic_increment(req_oust);
    sbuf->req_oust = req_oust;
  }

  gasnetc_snd_post(cep, &req, sbuf);
}

/* Helper for rdma gets: bounce buffer case */
static void gasnetc_do_get_bounce(gasnetc_cep_t *cep, VAPI_rkey_t rkey,
                                  uintptr_t src, uintptr_t dst, size_t nbytes,
                                  gasneti_atomic_t *req_oust) {
  gasnetc_sbuf_t *sbuf;
  gasnetc_sreq_t req;

  GASNETI_TRACE_EVENT_VAL(C, RDMA_GET_BOUNCE, nbytes);

  assert(nbytes != 0);

  /* Use full bounce buffers until just one buffer worth of data remains */
  while (nbytes > GASNETC_BUFSZ) {
    sbuf = gasnetc_get_sbuf();

    req.sr_desc.opcode      = VAPI_RDMA_READ;
    req.sr_desc.sg_lst_len  = 1;
    req.sr_desc.fence       = FALSE;
    req.sr_desc.remote_addr = src;
    req.sr_desc.r_key       = rkey;

    req.sr_sg[0].addr = (uintptr_t)sbuf->buffer;
    req.sr_sg[0].lkey = gasnetc_snd_reg.lkey;
    req.sr_sg[0].len  = GASNETC_BUFSZ;
    sbuf->addr = (void *)dst;
    sbuf->len = GASNETC_BUFSZ;

    gasnetc_snd_post(cep, &req, sbuf);

    src += GASNETC_BUFSZ;
    dst += GASNETC_BUFSZ;
    nbytes -= GASNETC_BUFSZ;
  }

  /* Send out the last buffer w/ the counter (if any) advanced */
  assert(nbytes <= GASNETC_BUFSZ);

  sbuf = gasnetc_get_sbuf();

  req.sr_desc.opcode      = VAPI_RDMA_READ;
  req.sr_desc.sg_lst_len  = 1;
  req.sr_desc.fence       = FALSE;
  req.sr_desc.remote_addr = src;
  req.sr_desc.r_key       = rkey;

  req.sr_sg[0].addr = (uintptr_t)sbuf->buffer;
  req.sr_sg[0].lkey = gasnetc_snd_reg.lkey;
  req.sr_sg[0].len  = nbytes;
  sbuf->addr = (void *)dst;
  sbuf->len = nbytes;

  if (req_oust) {
    gasneti_atomic_increment(req_oust);
    sbuf->req_oust = req_oust;
  }

  gasnetc_snd_post(cep, &req, sbuf);
}

/* Helper for rdma gets: zero copy case */
static void gasnetc_do_get_zerocp(gasnetc_cep_t *cep, VAPI_lkey_t lkey, VAPI_rkey_t rkey,
                                  uintptr_t src, uintptr_t dst, size_t nbytes,
                                  gasneti_atomic_t *req_oust) {
  gasnetc_sbuf_t *sbuf;
  gasnetc_sreq_t req;
  size_t max_sz = gasnetc_hca_port.max_msg_sz;

  GASNETI_TRACE_EVENT_VAL(C, RDMA_GET_ZEROCP, nbytes);

  assert(nbytes != 0);

  /* Use max-sized messages until just one msg worth of data remains */
  if_pf (nbytes > max_sz) {
    do {
      sbuf = gasnetc_get_sbuf();

      req.sr_desc.opcode      = VAPI_RDMA_READ;
      req.sr_desc.sg_lst_len  = 1;
      req.sr_desc.fence       = FALSE;
      req.sr_desc.remote_addr = src;
      req.sr_desc.r_key       = rkey;

      req.sr_sg[0].addr = dst;
      req.sr_sg[0].lkey = lkey;
      req.sr_sg[0].len  = max_sz;

      gasnetc_snd_post(cep, &req, sbuf);

      src += max_sz;
      dst += max_sz;
      nbytes -= max_sz;
    } while (nbytes > max_sz);
  }

  /* Send out the last buffer w/ the counters (if any) advanced */
  assert(nbytes <= max_sz);

  sbuf = gasnetc_get_sbuf();

  req.sr_desc.opcode      = VAPI_RDMA_READ;
  req.sr_desc.sg_lst_len  = 1;
  req.sr_desc.fence       = FALSE;
  req.sr_desc.remote_addr = src;
  req.sr_desc.r_key       = rkey;

  req.sr_sg[0].addr = dst;
  req.sr_sg[0].lkey = lkey;
  req.sr_sg[0].len  = nbytes;

  if (req_oust) {
    gasneti_atomic_increment(req_oust);
    sbuf->req_oust = req_oust;
  }

  gasnetc_snd_post(cep, &req, sbuf);
}

/* ------------------------------------------------------------------------------------ *
 *  Externally visible functions                                                        *
 * ------------------------------------------------------------------------------------ */

extern void gasnetc_sndrcv_init(void) {
  VAPI_cqe_num_t	act_size;
  VAPI_ret_t		vstat;
  gasnetc_buffer_t	*buf;
  gasnetc_rbuf_t	*rbuf;
  gasnetc_sbuf_t	*sbuf;
  int 			padded_size, count, i;

  /*
   * setup RCV resources
   */
  assert(gasnetc_am_oust_pp * (gasnetc_nodes - 1) <= gasnetc_am_oust_limit);
  count = (gasnetc_am_oust_pp * 2) * (gasnetc_nodes - 1) + gasnetc_am_spares;

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
    padded_size = GASNETC_ALIGNUP(sizeof(gasnetc_rbuf_t), GASNETC_CACHE_LINE_SIZE);
    gasnetc_rbuf_alloc = gasneti_malloc(count*padded_size + GASNETC_CACHE_LINE_SIZE-1);

    /* Initialize the rbuf's */
    rbuf = (gasnetc_rbuf_t *)GASNETC_ALIGNUP(gasnetc_rbuf_alloc, GASNETC_CACHE_LINE_SIZE);
    for (i = 0; i < count; ++i) {
      rbuf->rr_desc.id         = (uintptr_t)rbuf;	/* CQE will point back to this request */
      rbuf->rr_desc.opcode     = VAPI_RECEIVE;
      rbuf->rr_desc.comp_type  = VAPI_SIGNALED;	/* XXX: is this right? */
      rbuf->rr_desc.sg_lst_len = 1;
      rbuf->rr_desc.sg_lst_p   = &rbuf->rr_sg;
      rbuf->rr_sg.len          = GASNETC_BUFSZ;
      rbuf->rr_sg.addr         = (uintptr_t)&buf[i];
      rbuf->rr_sg.lkey         = gasnetc_rcv_reg.lkey;
      gasneti_freelist_put(&gasnetc_rbuf_freelist, rbuf);

      rbuf = (gasnetc_rbuf_t *)((uintptr_t)rbuf + padded_size);
    }
    #if GASNETC_RCV_THREAD
      gasnetc_rcv_thread_rbuf = gasneti_freelist_get(&gasnetc_rbuf_freelist);
      assert(gasnetc_rcv_thread_rbuf != NULL);
    #endif
  }

  /*
   * setup SND resources
   */
  count = MIN(gasnetc_op_oust_limit, gasnetc_op_oust_pp * gasnetc_nodes);

  /* create the SND CQ */
  vstat = VAPI_create_cq(gasnetc_hca, count, &gasnetc_snd_cq, &act_size);
  assert(vstat == VAPI_OK);
  assert(act_size >= count);

  /* Allocated pinned memory for bounce buffers */
  buf = gasnetc_alloc_pinned(count * sizeof(gasnetc_buffer_t),
		             VAPI_EN_LOCAL_WRITE, &gasnetc_snd_reg);
  assert(buf != NULL);

  /* Allocated normal memory for send descriptors (sbuf's) */
  padded_size = GASNETC_ALIGNUP(sizeof(gasnetc_sbuf_t), GASNETC_CACHE_LINE_SIZE);
  gasnetc_sbuf_alloc = gasneti_malloc(count*padded_size + GASNETC_CACHE_LINE_SIZE-1);

  /* Initialize the sbuf's */
  sbuf = (gasnetc_sbuf_t *)GASNETC_ALIGNUP(gasnetc_sbuf_alloc, GASNETC_CACHE_LINE_SIZE);
  for (i = 0; i < count; ++i) {
    sbuf->buffer = &buf[i];
    gasneti_freelist_put(&gasnetc_sbuf_freelist, sbuf);

    sbuf   = (gasnetc_sbuf_t *)((uintptr_t)sbuf + padded_size);
  }
}

extern void gasnetc_sndrcv_init_cep(gasnetc_cep_t *cep) {
  int i;
  
  if (cep != &gasnetc_cep[gasnetc_mynode]) {
    /* XXX:
     * Currently preposting one for each incomming request and one for
     * each possible reply.  Later hope to post the reply buffers on-demand.
     * That will allow us to run with
     *   gasnetc_am_oust_limit < (gasnetc_nodes - 1)*gasnetc_am_oust_pp
     */
    for (i = 0; i < 2 * gasnetc_am_oust_pp; ++i) {
      gasnetc_rcv_post(cep, gasneti_freelist_get(&gasnetc_rbuf_freelist), 0);
    }

    gasnetc_sema_init(&cep->am_sema, gasnetc_am_oust_pp);
    gasnetc_sema_init(&cep->op_sema, gasnetc_op_oust_pp);
  } else {
    /* Even the loopback AMs are restricted by credits, so we make this limit LARGE.
     * Since the handlers run synchronously, this just limits the number of threads
     * which are sending AM Requests to no more than 1 Million :-)
     */
    gasnetc_sema_init(&cep->am_sema, 1000000);
    gasnetc_sema_init(&cep->op_sema, 0);
  }
}

extern void gasnetc_sndrcv_fini(void) {
  VAPI_ret_t vstat;

  if (gasnetc_nodes > 1) {
    #if GASNETC_RCV_THREAD
      vstat = EVAPI_clear_comp_eventh(gasnetc_hca, gasnetc_rcv_handler);
      assert(vstat == VAPI_OK);
    #endif

    gasnetc_free_pinned(&gasnetc_rcv_reg);
    gasneti_free(gasnetc_rbuf_alloc);

    gasnetc_free_pinned(&gasnetc_snd_reg);
    gasneti_free(gasnetc_sbuf_alloc);
  }

  vstat = VAPI_destroy_cq(gasnetc_hca, gasnetc_rcv_cq);
  assert(vstat == VAPI_OK);

  vstat = VAPI_destroy_cq(gasnetc_hca, gasnetc_snd_cq);
  assert(vstat == VAPI_OK);
}

extern void gasnetc_sndrcv_poll(void) {
  gasnetc_poll_both();
}

extern void gasnetc_counter_wait_aux(gasneti_atomic_t *counter, int handler_context)
{
  if (handler_context) {
    do {
	/* must not poll rcv queue in hander context */
        gasnetc_poll_snd();
    } while (gasneti_atomic_read(counter) != 0);
  } else {
    do {
        gasnetc_poll_both();
    } while (gasneti_atomic_read(counter) != 0);
  }
}

/* Perform an RDMA put
 *
 * Uses bounce buffers when the source is not pinned, or is "small enough" and the caller is
 * planning to wait for local completion.  Otherwise zero-copy is used when the source is pinned.
 */
extern int gasnetc_rdma_put(int node, void *src_ptr, void *dst_ptr, size_t nbytes, gasneti_atomic_t *mem_oust, gasneti_atomic_t *req_oust) {
  gasnetc_cep_t *cep = &gasnetc_cep[node];
  VAPI_rkey_t rkey;
  uintptr_t src = (uintptr_t)src_ptr;
  uintptr_t dst = (uintptr_t)dst_ptr;

  assert(nbytes != 0);

#if defined(GASNET_SEGMENT_FAST)
  rkey = cep->rkey;
#else
  /* (###) implement firehose */
#endif
  
  do {
    /* Use a short-cut for sends that are short enough.
     *
     * Note that we do this based only on the size of the request, without bothering to check whether
     * the caller cares about local completion, or whether zero-copy is possible.
     * We do this is because the cost of this small copy appears cheaper then the alternative logic.
     */
    if ((GASNETC_PUT_INLINE_LIMIT != 0) && (nbytes <= GASNETC_PUT_INLINE_LIMIT)) {
      gasnetc_do_put_inline(cep, rkey, src, dst, nbytes, req_oust);
      break;	/* done */
    }

    /* Because VAPI lacks any indication of "local" completion, the only ways to
     * implement non-bulk puts (mem_oust != NULL) are as fully blocking puts, or
     * with bounce buffers.  So, if a non-bulk put is "not too large" use bounce
     * buffers.
     */
    if ((nbytes <= GASNETC_PUT_COPY_LIMIT) && (mem_oust != NULL)) {
      gasnetc_do_put_bounce(cep, rkey, src, dst, nbytes, req_oust);
      break;	/* done */
    }

    /* Here is the general case, where we must check if the local memory is pinned */
    {
      size_t count = nbytes;	/* later will be length of (un)pinned interval */
      gasnetc_memreg_t *reg = gasnetc_local_reg(src, src + (count - 1));

      if_pf (reg == NULL) {
        /* Source not pinned - use bounce buffers upto some size limit */
	gasnetc_do_put_bounce(cep, rkey, src, dst, count, req_oust);
      } else {
        /* Source pinned - use zero copy RDMA write */
	gasnetc_do_put_zerocp(cep, reg->lkey, rkey, src, dst, count, mem_oust, req_oust);
      }

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
  VAPI_rkey_t rkey;
  uintptr_t src = (uintptr_t)src_ptr;
  uintptr_t dst = (uintptr_t)dst_ptr;

  assert(nbytes != 0);

#if defined(GASNET_SEGMENT_FAST)
  rkey = cep->rkey;
#else
  /* (###) implement firehose */
#endif

  do {
    size_t count = nbytes;	/* later will be length of (un)pinned interval */
    gasnetc_memreg_t *reg = gasnetc_local_reg(dst, dst + (count - 1));

    if_pf (reg == NULL) {
      /* Destination not pinned - use bounce buffers upto some size limit */
      gasnetc_do_get_bounce(cep, rkey, src, dst, count, req_oust);
    } else {
      /* Destination pinned - use zero copy RDMA read */
      gasnetc_do_get_zerocp(cep, reg->lkey, rkey, src, dst, count, req_oust);
    }

    src += count;
    dst += count;
    nbytes -= count;
  } while (nbytes);

  return 0;
}

#if defined(GASNET_SEGMENT_FAST)
/* write a constant pattern to remote memory using a local memset and an RDMA put */
extern int gasnetc_rdma_memset(int node, void *dst_ptr, int val, size_t nbytes, gasneti_atomic_t *req_oust) {
  gasnetc_cep_t *cep = &gasnetc_cep[node];
  uintptr_t dst = (uintptr_t)dst_ptr;
  gasnetc_sbuf_t *sbuf;
  gasnetc_sreq_t req;

  assert(nbytes != 0);
	  
  do {
    size_t count = MIN(nbytes, GASNETC_BUFSZ);

    sbuf = gasnetc_get_sbuf();
    memset(sbuf->buffer, val, count);

    req.sr_desc.opcode      = VAPI_RDMA_WRITE;
    req.sr_desc.sg_lst_len  = 1;
    req.sr_desc.fence       = TRUE;
    req.sr_desc.remote_addr = dst;
    req.sr_desc.r_key       = cep->rkey;

    req.sr_sg[0].addr = (uintptr_t)sbuf->buffer;
    req.sr_sg[0].len  = count;
    req.sr_sg[0].lkey = gasnetc_snd_reg.lkey;

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
#endif

extern int gasnetc_RequestGeneric(gasnetc_category_t category,
				  int dest, gasnet_handler_t handler,
				  void *src_addr, int nbytes, void *dst_addr,
				  int numargs, gasneti_atomic_t *mem_oust, va_list argptr) {
  gasnetc_poll_both();	/* ensure progress */

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

  gasnetc_poll_both();	/* ensure progress (should this really be done _here_?) */

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

  rbuf->needReply = 0;
  return retval;
}

/* ------------------------------------------------------------------------------------ */
/*
  Misc. Active Message Functions
  ==============================
*/
extern int gasnetc_AMGetMsgSource(gasnet_token_t token, gasnet_node_t *srcindex) {
  uint32_t flags;
  gasnet_node_t sourceid;

  if (!token) GASNETI_RETURN_ERRR(BAD_ARG,"bad token");
  if (!srcindex) GASNETI_RETURN_ERRR(BAD_ARG,"bad src ptr");

  flags = ((gasnetc_rbuf_t *)token)->flags;

  if (GASNETC_MSG_CATEGORY(flags) != gasnetc_System) {
    GASNETI_CHECKATTACH();
  }

  sourceid = GASNETC_MSG_SRCIDX(flags);

  assert(sourceid < gasnetc_nodes);
  *srcindex = sourceid;
  return GASNET_OK;
}

extern int gasnetc_AMPoll() {
  int work;

  GASNETI_CHECKATTACH();

  CQ_LOCK;
  work = ((gasnetc_peek_cq(gasnetc_rcv_cq, 1) == VAPI_OK) ||
          (gasnetc_peek_cq(gasnetc_snd_cq, 1) == VAPI_OK));
  CQ_UNLOCK;

  if_pf (work) {
    gasnetc_poll_both();
  }

  return GASNET_OK;
}

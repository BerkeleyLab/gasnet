/*   $Source: /Users/kamil/work/gasnet-cvs2/gasnet/vapi-conduit/Attic/gasnet_core_sndrcv.c,v $
 *     $Date: 2004/10/22 21:02:17 $
 * $Revision: 1.57 $
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
#if GASNETC_USE_FIREHOSE
  size_t				gasnetc_fh_maxsz;
#endif

/* ------------------------------------------------------------------------------------ *
 *  File-scoped types                                                                   *
 * ------------------------------------------------------------------------------------ */

/* Description of a receive buffer.
 *
 * Note that use of the freelist will overwrite the first sizeof(gasneti_freelist_ptr_t)
 * bytes.  Therefore it is imporant to have the union to ensure the fixed fields are
 * not clobbered regardless of the freelist implementation.  Note the macros following
 * the typedef are used to hide the existence of the union.
 */
typedef struct {
  union {
    gasneti_freelist_ptr_t	linkage;
    struct {
      /* Fields intialized at recv time: */
      int                   	needReply;
      int                   	handlerRunning;
      uint32_t              	flags;
    }				am;
  } u;

  /* Fields fixed for life of the rbuf as it is reused */
  VAPI_rr_desc_t        	rr_desc;        /* recv request descriptor */
  VAPI_sg_lst_entry_t   	rr_sg;          /* single-entry scatter list */
} gasnetc_rbuf_t;
#define rbuf_needReply		u.am.needReply
#define rbuf_handlerRunning	u.am.handlerRunning
#define rbuf_flags		u.am.flags

/* Description of a send request.
 *
 * Note that use of the freelist will overwrite the first sizeof(gasneti_freelist_ptr_t) bytes.
 */
typedef struct {
  /* Communication end point */
  gasnetc_cep_t			*cep;

  /* Number of Work Request entries */
  uint32_t			count;

  /* Completion counters */
  gasnetc_counter_t		*mem_oust;	/* source memory refs outstanding */
  gasnetc_counter_t		*req_oust;	/* requests outstanding */

  #if GASNETC_PIN_SEGMENT
    /* Destination address/len for bounced GETs */
    void			*addr;
    size_t			len;
  #endif

  /* Buffer when needed for bounced GETs or PUTs, or for an AM header */
  gasnetc_buffer_t		*buffer;

  #if GASNETC_USE_FIREHOSE
    /* Firehose data */
    VAPI_wr_opcode_t		opcode;
    size_t			fh_len;
    uintptr_t			loc_addr, rem_addr;
    const firehose_request_t	*fh_ptr[2];
    firehose_request_t          fh_loc, fh_rem;
    gasneti_atomic_t		fh_oust;
  #endif
} gasnetc_sreq_t;

/* ------------------------------------------------------------------------------------ *
 *  File-scoped variables
 * ------------------------------------------------------------------------------------ */

static void				*gasnetc_sreq_alloc;
static void				*gasnetc_rbuf_alloc;
#if GASNETC_RCV_THREAD
  static EVAPI_compl_handler_hndl_t	gasnetc_rcv_handler;
#endif

static gasneti_freelist_t		gasnetc_bbuf_freelist = GASNETI_FREELIST_INITIALIZER;
static gasneti_freelist_t		gasnetc_sreq_freelist = GASNETI_FREELIST_INITIALIZER;
static gasneti_freelist_t		gasnetc_rbuf_freelist = GASNETI_FREELIST_INITIALIZER;
static gasnetc_sema_t			gasnetc_op_sema;

/* ------------------------------------------------------------------------------------ *
 *  File-scoped functions and macros                                                    *
 * ------------------------------------------------------------------------------------ */

#define GASNETC_SR_ALIGN GASNETC_CACHE_LINE_SIZE
#define GASNETC_DECL_SR_DESC(_name, _len) \
	char _CONCAT(_name,_align)[sizeof(VAPI_sr_desc_t)+_len*sizeof(VAPI_sg_lst_entry_t)+2*GASNETC_SR_ALIGN];\
	VAPI_sr_desc_t *_name = (VAPI_sr_desc_t *)GASNETC_ALIGNUP(_CONCAT(_name,_align), GASNETC_SR_ALIGN);\
	void *_CONCAT(_name,_dummy) = (void *)(_name->sg_lst_len=_len,_name->sg_lst_p=(VAPI_sg_lst_entry_t *)(sizeof(VAPI_sr_desc_t)+(uintptr_t)_name)) /* note intentional lack of final semicolon */

/* Use of IB's 32-bit immediate data:
 *   0-1: category
 *     2: request or reply
 *   3-7: numargs
 *  8-15: handerID
 * 16-29: source index (14 bit LID space in IB)
 * 30-31: UNUSED
 */

#define GASNETC_MSG_NUMARGS(flags)      (((flags) >> 3) & 0x1f)
#define GASNETC_MSG_ISREQUEST(flags)    (!((flags) & 0x4))
#define GASNETC_MSG_ISREPLY(flags)      (!!((flags) & 0x4))
#define GASNETC_MSG_CATEGORY(flags)     ((gasnetc_category_t)((flags) & 0x3))
#define GASNETC_MSG_HANDLERID(flags)    ((gasnet_handler_t)((flags) >> 8))
#define GASNETC_MSG_SRCIDX(flags)       ((gasnet_node_t)((flags) >> 16) & 0x3fff)
#define GASNETC_MSG_CREDIT(flags)       ((flags) & (1<<30))

#define GASNETC_MSG_GENFLAGS(isreq, cat, nargs, hand, srcidx)   \
  (uint32_t)(  (((srcidx) & 0x3fff) << 16)      \
	     | (((hand)   & 0xff  ) << 8 )      \
	     | (((nargs)  & 0x1f  ) << 3 )      \
	     | ((!(isreq)         ) << 2 )      \
	     | (((cat)    & 0x3   )      ))

/* Work around apparent thread-safety bug in VAPI_poll_cq (and peek as well?) */
int gasnetc_use_poll_lock;
static gasnetc_mutex_t gasnetc_cq_poll_lock = GASNETC_MUTEX_INITIALIZER;
#if GASNETC_VAPI_FORCE_POLL_LOCK
  /* ALWAYS on */
  #define CQ_LOCK	gasnetc_mutex_lock(&gasnetc_cq_poll_lock, GASNETC_ANY_PAR);
  #define CQ_UNLOCK	gasnetc_mutex_unlock(&gasnetc_cq_poll_lock, GASNETC_ANY_PAR);
#else
  /* Conditionally on */
  #define CQ_LOCK	if_pf (gasnetc_use_poll_lock) {\
  				gasnetc_mutex_lock(&gasnetc_cq_poll_lock, GASNETC_ANY_PAR);\
			}
  #define CQ_UNLOCK	if_pf (gasnetc_use_poll_lock) {\
				gasnetc_mutex_unlock(&gasnetc_cq_poll_lock, GASNETC_ANY_PAR);\
			}
#endif

#define gasnetc_poll_cq(CQ, COMP_P)	VAPI_poll_cq(gasnetc_hca, (CQ), (COMP_P))
#define gasnetc_peek_cq(CQ, N)		EVAPI_peek_cq(gasnetc_hca, (CQ), (N))

#define gasnetc_poll_rcv()		gasnetc_do_poll(1,0)
#define gasnetc_poll_snd()		gasnetc_do_poll(0,1)
#define gasnetc_poll_both()		gasnetc_do_poll(1,1)

#if GASNETC_PIN_SEGMENT
/* Test if a given (addr, len) is in the GASNet segment or not.
 * Returns non-zero if starting address is in the segment.
 * For interval that is only partially in the segment, the length will
 * be adjusted to describe a region either fully in or fully out.
 */
GASNET_INLINE_MODIFIER(gasnetc_in_segment)
int gasnetc_in_segment(uintptr_t start, size_t *len_p) {
  size_t len = *len_p;
  uintptr_t end = start + (len - 1);

  if_pt ((start >= gasnetc_seg_reg.addr) && (end <= gasnetc_seg_reg.end)) {
    /* FULLY IN */
    return 1;
  }

  if_pt ((start > gasnetc_seg_reg.end) || (end < gasnetc_seg_reg.addr)) {
    /* FULLY OUT */
    return 0;
  }

  /* Partials: */
  if (start < gasnetc_seg_reg.addr) {
    /* Starts OUT, ends IN */
    *len_p = gasnetc_seg_reg.addr - start;
    return 0;
  } else {
    gasneti_assert(end > gasnetc_seg_reg.end);
    /* Starts IN, ends OUT */
    *len_p = (gasnetc_seg_reg.end - start) + 1;
    return 1;
  }
}
#endif

/* Post a work request to the receive queue of the given endpoint */
GASNET_INLINE_MODIFIER(gasnetc_rcv_post)
void gasnetc_rcv_post(gasnetc_cep_t *cep, gasnetc_rbuf_t *rbuf, int credit) {
  VAPI_ret_t vstat;

  gasneti_assert(cep);
  gasneti_assert(rbuf);

  /* check for attempted loopback traffic */
  gasneti_assert(cep != &gasnetc_cep[gasnetc_mynode]);
  
  vstat = VAPI_post_rr(gasnetc_hca, cep->qp_handle, &rbuf->rr_desc);
  if (credit) {
    gasnetc_sema_up(&cep->am_sema);
    GASNETC_SEMA_CHECK(&(cep->am_sema), gasnetc_am_oust_pp);
  }

  if_pt (vstat == VAPI_OK) {
    /* normal return */
    return;
  } else if (GASNETC_IS_EXITING()) {
    /* disconnected by another thread */
    gasnetc_exit(0);
  } else {
    /* unexpected error */
    GASNETC_VAPI_CHECK(vstat, "while posting a receive work request");
  }
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

  rbuf->rbuf_needReply = GASNETC_MSG_ISREQUEST(flags);
  rbuf->rbuf_handlerRunning = 1;
  rbuf->rbuf_flags = flags;

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
          GASNETI_TRACE_AMMEDIUM_REQHANDLER(handler_id, rbuf, data, (int)nbytes, numargs, args);
        else
          GASNETI_TRACE_AMMEDIUM_REPHANDLER(handler_id, rbuf, data, (int)nbytes, numargs, args);
        RUN_HANDLER_MEDIUM(handler_fn,rbuf,args,numargs,data,nbytes);
      }
      break;

    case gasnetc_Long:
      { 
        nbytes = buf->longmsg.nBytes;
        data = (void *)(buf->longmsg.destLoc);
	args = buf->longmsg.args;
        if (GASNETC_MSG_ISREQUEST(flags)) {
          GASNETI_TRACE_AMLONG_REQHANDLER(handler_id, rbuf, data, (int)nbytes, numargs, args);
        } else {
	  #if !GASNETC_PIN_SEGMENT
	    if (GASNETC_MSG_SRCIDX(flags) != gasnetc_mynode) {
	      /* No RDMA for ReplyLong.  So, must relocate the payload. */
	      memcpy(data, GASNETC_MSG_LONG_DATA(buf, numargs), nbytes);
	    }
	  #endif
          GASNETI_TRACE_AMLONG_REPHANDLER(handler_id, rbuf, data, (int)nbytes, numargs, args);
	}
        RUN_HANDLER_LONG(handler_fn,rbuf,args,numargs,data,nbytes);
      }
      break;

    default:
    gasneti_fatalerror("invalid AM category on recv");
  }
  
  rbuf->rbuf_handlerRunning = 0;
}

/* Try to pull completed entries from the send CQ (if any). */
static int gasnetc_snd_reap(int limit, gasnetc_sreq_t **head_p, gasnetc_sreq_t **tail_p) {
  VAPI_ret_t vstat;
  VAPI_wc_desc_t comp;
  gasneti_freelist_ptr_t dummy;
  void *tail = &dummy;
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
        gasnetc_sreq_t *sreq = (gasnetc_sreq_t *)(uintptr_t)comp.id;
        if_pt (sreq) {
	  gasnetc_sema_up_n(&sreq->cep->op_sema, sreq->count);
          GASNETC_SEMA_CHECK(&sreq->cep->op_sema, gasnetc_op_oust_pp);
	  gasnetc_sema_up(&gasnetc_op_sema);
          GASNETC_SEMA_CHECK(&gasnetc_op_sema, gasnetc_op_oust_limit);

          #if GASNETC_PIN_SEGMENT
	  /* complete bounced RMDA read, if any */
	  if (sreq->addr) {
	    gasneti_assert(comp.opcode == VAPI_CQE_SQ_RDMA_READ);
	    gasneti_assert(sreq->len > 0);

	    memcpy(sreq->addr, sreq->buffer, sreq->len);
            gasneti_sync_writes();
	  }
	  #endif
	  
	  /* decrement any outstanding counters */
          if (sreq->mem_oust) {
	    gasneti_assert(!gasnetc_counter_done(sreq->mem_oust));
	    gasnetc_counter_dec(sreq->mem_oust);
	  }
          if (sreq->req_oust) {
	    gasneti_assert(!gasnetc_counter_done(sreq->req_oust));
            gasnetc_counter_dec(sreq->req_oust);
	  }
	  
	  #if GASNETC_USE_FIREHOSE
	  if (sreq->fh_ptr[1]) {
	    firehose_release(sreq->fh_ptr, 2);
	  } else if (sreq->fh_ptr[0]) {
	    firehose_release(sreq->fh_ptr, 1);
	  }
	  #endif

	  if_pf (sreq->buffer) {
	    gasneti_freelist_put(&gasnetc_bbuf_freelist, sreq->buffer);
	  }

	  /* keep a list of reaped sreqs */
	  gasneti_freelist_link(tail, sreq);
	  tail = sreq;
        } else {
          gasneti_fatalerror("snd_reap reaped NULL sreq");
          break;
        }
      } else if (GASNETC_IS_EXITING()) {
        /* disconnected */
	break;	/* can't exit since we can be called in exit path */
      } else {
#if 1 
        fprintf(stderr, "@ %d> snd comp.status=%d comp.opcode=%d\n", gasnetc_mynode, comp.status, comp.opcode);
        while((vstat = VAPI_poll_cq(gasnetc_hca, gasnetc_rcv_cq, &comp)) == VAPI_OK) {
          fprintf(stderr, "@ %d> - rcv comp.status=%d\n", gasnetc_mynode, comp.status);
        }
#endif
        gasneti_fatalerror("aborting on reap of failed send");
        break;
      }
    } else if (GASNETC_IS_EXITING()) {
      /* disconnected by another thread */
      gasnetc_exit(0);
    } else {
      GASNETC_VAPI_CHECK(vstat, "while reaping the send queue");
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

GASNET_INLINE_MODIFIER(gasnetc_rcv_am)
void gasnetc_rcv_am(const VAPI_wc_desc_t *comp, gasnetc_rbuf_t **spare_p) {
  gasnetc_rbuf_t emergency_spare;
  gasnetc_rbuf_t *rbuf = (gasnetc_rbuf_t *)(uintptr_t)comp->id;
  uint32_t flags = comp->imm_data;
  gasnetc_cep_t *cep = &gasnetc_cep[GASNETC_MSG_SRCIDX(flags)];
  int credit = GASNETC_MSG_ISREPLY(flags);
  gasnetc_rbuf_t *spare;

  /* If possible, post a replacement buffer right away. */
  spare = (*spare_p) ? (*spare_p) : gasneti_freelist_get(&gasnetc_rbuf_freelist);
  if_pt (spare) {
    /* This is the normal case, in which we have sufficient resources to post
     * a replacement buffer before processing the recv'd buffer.  That way
     * we are certain that a buffer is in-place before the potential reply
     * sends a credit to our peer, which then could use the buffer
     */
    gasnetc_rcv_post(cep, spare, credit);
    *spare_p = rbuf;	/* recv'd rbuf becomes the spare for next pass (if any) */
  } else {
    /* Because we don't have any "spare" rbuf available to post we copy the recvd
     * message to a temporary (non-pinned) buffer so we can repost rbuf.
     */
    gasnetc_buffer_t *buf = gasneti_malloc(sizeof(gasnetc_buffer_t));
    memcpy(buf, (void *)(uintptr_t)rbuf->rr_sg.addr, sizeof(gasnetc_buffer_t));
    emergency_spare.rr_sg.addr = (uintptr_t)buf;

    gasnetc_rcv_post(cep, rbuf, credit);

    rbuf = &emergency_spare;
    GASNETC_STAT_EVENT(ALLOC_AM_SPARE);
    GASNETI_TRACE_PRINTF(C,("ALLOC_AM_SPARE\n"));
  }

  /* Now process the packet */
  gasnetc_processPacket(rbuf, flags);

  /* Finalize flow control */
  if_pf (rbuf->rbuf_needReply) {
    int retval = gasnetc_ReplySystem((gasnet_token_t)rbuf, NULL,
		    		     gasneti_handleridx(gasnetc_SYS_ack), 0 /* no args */);
    gasneti_assert(retval == GASNET_OK);
  }
  if_pf (!spare) {
    /* Free the temporary buffer we created */
    gasneti_free((void *)(uintptr_t)emergency_spare.rr_sg.addr);
  }

  #if GASNETC_USE_FIREHOSE && !defined(FIREHOSE_COMPLETION_IN_HANDLER)
    /* Handler might have queued work */
    firehose_poll();
  #endif
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
      } else if (GASNETC_IS_EXITING()) {
        /* disconnected */
	break;	/* can't exit since we can be called in exit path */
      } else {
#if 1
        fprintf(stderr, "@ %d> rcv comp.status=%d\n", gasnetc_mynode, comp.status);
        while((vstat = VAPI_poll_cq(gasnetc_hca, gasnetc_snd_cq, &comp)) == VAPI_OK) {
          fprintf(stderr, "@ %d> - snd comp.status=%d\n", gasnetc_mynode, comp.status);
        }
#endif
        gasneti_fatalerror("aborting on reap of failed recv");
	break;
      }
    } else if (GASNETC_IS_EXITING()) {
      /* disconnected by another thread */
      gasnetc_exit(0);
    } else {
      GASNETC_VAPI_CHECK(vstat, "while reaping the recv queue");
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
    gasnetc_sreq_t *head, *tail;
    int count;

    count = gasnetc_snd_reap(GASNETC_SND_REAP_LIMIT, &head, &tail);
    if (count > 0) {
	gasneti_freelist_put_many(&gasnetc_sreq_freelist, head, tail);
    }
  }
}

/* allocate a send request/buffer pair */
#ifdef __GNUC__
  GASNET_INLINE_MODIFIER(gasnetc_get_sreq)
  gasnetc_sreq_t *gasnetc_get_sreq(int need_bbuf) __attribute__((__malloc__));
#endif
GASNET_INLINE_MODIFIER(gasnetc_get_sreq)
gasnetc_sreq_t *gasnetc_get_sreq(int need_bbuf) {
  gasnetc_sreq_t *sreq;
  gasnetc_buffer_t *bbuf = NULL;
  gasnetc_sreq_t *tail;
  int count;

  /* The bounce buffers are finite, so get one first if needed */
  if_pf (need_bbuf) {
    GASNETC_TRACE_WAIT_BEGIN();
    GASNETC_STAT_EVENT(GET_BBUF);

    bbuf = gasneti_freelist_get(&gasnetc_bbuf_freelist);

    if_pf (bbuf == NULL) {
      gasnetc_poll_snd();
      bbuf = gasneti_freelist_get(&gasnetc_bbuf_freelist);
      while (bbuf == NULL) {
        GASNETI_WAITHOOK();
        gasnetc_poll_snd();
        bbuf = gasneti_freelist_get(&gasnetc_bbuf_freelist);
      }
      GASNETC_TRACE_WAIT_END(GET_BBUF_STALL);
    }
  }

  GASNETC_STAT_EVENT(GET_SBUF);
  /* 1) try to get an unused sreq by reaping the send CQ */
  count = gasnetc_snd_reap(1, &sreq, &tail);
  if_pf (count == 0) {
    /* 2) try the free list */
    sreq = gasneti_freelist_get(&gasnetc_sreq_freelist);
    if_pf (!sreq) {
      /* 3) malloc a new one */
      sreq = gasneti_malloc(MAX(sizeof(gasnetc_sreq_t), sizeof(gasneti_freelist_ptr_t)));
      GASNETC_STAT_EVENT(ALLOC_SBUF);
      GASNETI_TRACE_PRINTF(C,("ALLOC_SBUF\n"));
    }
  } else {
    gasneti_assert(count == 1);
  }

  gasneti_assert(sreq != NULL);

  #if GASNET_DEBUG
    /* invalidate field(s) which should always be set by caller */
    sreq->cep = NULL;
  #endif
  sreq->buffer = bbuf;
  sreq->mem_oust = NULL;
  sreq->req_oust = NULL;
  #if GASNETC_PIN_SEGMENT
    sreq->addr = NULL;
  #endif
  #if GASNETC_USE_FIREHOSE
    sreq->fh_ptr[0] = NULL;
    sreq->fh_ptr[1] = NULL;
  #endif

  return sreq;
}

#if GASNET_TRACE || GASNET_DEBUG
GASNET_INLINE_MODIFIER(gasnetc_snd_validate)
void gasnetc_snd_validate(gasnetc_sreq_t *sreq, VAPI_sr_desc_t *sr_desc, int count, const char *type) {
  int i;

  gasneti_assert(sreq);
  gasneti_assert(sreq->cep);
  gasneti_assert(sreq->cep != &gasnetc_cep[gasnetc_mynode]); /* detects loopback */
  gasneti_assert(sr_desc);
  gasneti_assert(count > 0);
  gasneti_assert(type);

  for (i = 0; i < count; ++i, ++sr_desc) {
    uintptr_t l_addr = sr_desc->sg_lst_p[0].addr;
    uintptr_t r_addr = sr_desc->remote_addr;
    size_t    len    = sr_desc->sg_lst_p[0].len;

    switch (sr_desc->opcode) {
    case VAPI_SEND_WITH_IMM:
      GASNETI_TRACE_PRINTF(D,("%s op=SND len=%lu local=[%p-%p] remote=N/A\n",
				type, (unsigned long)len,
				(void *)l_addr, (void *)(l_addr + (len - 1))));
      break;

    case VAPI_RDMA_WRITE:
      GASNETI_TRACE_PRINTF(D,("%s op=PUT len=%lu local=[%p-%p] remote=[%p-%p]\n",
				type, (unsigned long)len,
				(void *)l_addr, (void *)(l_addr + (len - 1)),
				(void *)r_addr, (void *)(r_addr + (len - 1))));
      break;

    case VAPI_RDMA_READ:
      GASNETI_TRACE_PRINTF(D,("%s op=GET len=%lu local=[%p-%p] remote=[%p-%p]\n",
				type, (unsigned long)len,
				(void *)l_addr, (void *)(l_addr + (len - 1)),
				(void *)r_addr, (void *)(r_addr + (len - 1))));
      break;

    default:
      gasneti_fatalerror("Invalid operation %d for %s\n", sr_desc->opcode, type);
    }

    /* check for reasonable message sizes
     * With SEND 0-bytes triggers a Mellanox bug
     * With RDMA ops, 0-bytes makes no sense.
     */
    #if GASNET_DEBUG
    {
      u_int32_t	sum = 0;

      for (i = 0; i < sr_desc->sg_lst_len; ++i) {
        sum += sr_desc->sg_lst_p[i].len;
        gasneti_assert(sr_desc->sg_lst_p[i].len != 0);
        gasneti_assert(sr_desc->sg_lst_p[i].len <= gasnetc_hca_port.max_msg_sz);
        gasneti_assert(sr_desc->sg_lst_p[i].len <= sum); /* check for overflow of 'sum' */
      }

      gasneti_assert(sum <= gasnetc_hca_port.max_msg_sz);
    }
  #endif
  }
}
#endif /* DEBUG || TRACE */


GASNET_INLINE_MODIFIER(gasnetc_snd_post_common)
void gasnetc_snd_post_common(gasnetc_sreq_t *sreq, VAPI_sr_desc_t *sr_desc) {
  gasnetc_sema_t *op_sema;
  int i;

  /* loop until space is available on the CQ */
  if_pf (!gasnetc_sema_trydown(&gasnetc_op_sema, GASNETC_ANY_PAR)) {
    GASNETC_TRACE_WAIT_BEGIN();
    do {
      GASNETI_WAITHOOK();
      gasnetc_poll_snd();
    } while (!gasnetc_sema_trydown(&gasnetc_op_sema, GASNETC_ANY_PAR));
    GASNETC_TRACE_WAIT_END(POST_SR_STALL_CQ);
  }

  /* loop until space is available on the SQ for 1 new entry */
  op_sema = &sreq->cep->op_sema;
  if_pf (!gasnetc_sema_trydown(op_sema, GASNETC_ANY_PAR)) {
    GASNETC_TRACE_WAIT_BEGIN();
    do {
      GASNETI_WAITHOOK();
      gasnetc_poll_snd();
    } while (!gasnetc_sema_trydown(op_sema, GASNETC_ANY_PAR));
    GASNETC_TRACE_WAIT_END(POST_SR_STALL_SQ);
  }

  /* setup some invariant fields */
  sreq->count = 1;
  sr_desc[0].id        = (uintptr_t)sreq;
  sr_desc[0].comp_type = VAPI_SIGNALED;
  sr_desc[0].set_se    = FALSE;
  sr_desc[0].fence     = FALSE;
}

GASNET_INLINE_MODIFIER(gasnetc_snd_post_list_common)
void gasnetc_snd_post_list_common(gasnetc_sreq_t *sreq, VAPI_sr_desc_t *sr_desc, uint32_t count) {
  gasnetc_sema_t *op_sema;
  uint32_t tmp;
  int i;

  /* loop until space is available on the CQ */
  if_pf (!gasnetc_sema_trydown(&gasnetc_op_sema, GASNETC_ANY_PAR)) {
    GASNETC_TRACE_WAIT_BEGIN();
    do {
      GASNETI_WAITHOOK();
      gasnetc_poll_snd();
    } while (!gasnetc_sema_trydown(&gasnetc_op_sema, GASNETC_ANY_PAR));
    GASNETC_TRACE_WAIT_END(POST_SR_STALL_CQ);
  }

  /* loop until space is available on the SQ for at least 1 new entry */
  op_sema = &sreq->cep->op_sema;
  tmp = gasnetc_sema_trydown_n(op_sema, GASNETC_ANY_PAR, count);
  if_pf (!tmp) {
    GASNETC_TRACE_WAIT_BEGIN();
    do {
      GASNETI_WAITHOOK();
      gasnetc_poll_snd();
      tmp = gasnetc_sema_trydown_n(op_sema, GASNETC_ANY_PAR, count);
    } while (!tmp);
    GASNETC_TRACE_WAIT_END(POST_SR_STALL_SQ);
  }

  /* setup some invariant fields */
  sreq->count = tmp;
  tmp -= 1;
  for (i = 0; i < tmp; ++i) {
    #if GASNET_DEBUG	/* unused otherwise */
      sr_desc[i].id      = 0;
    #endif
    sr_desc[i].comp_type = VAPI_UNSIGNALED;
    sr_desc[i].set_se    = FALSE;
    sr_desc[i].fence     = FALSE;
  }
  sr_desc[tmp].id        = (uintptr_t)sreq;
  sr_desc[tmp].comp_type = VAPI_SIGNALED;
  sr_desc[tmp].set_se    = FALSE;
  sr_desc[tmp].fence     = FALSE;
}

/* Post a work request to the send queue of the given endpoint */
GASNET_INLINE_MODIFIER(gasnetc_snd_post)
void gasnetc_snd_post(gasnetc_sreq_t *sreq, VAPI_sr_desc_t *sr_desc) {
  VAPI_ret_t vstat;

  gasnetc_snd_post_common(sreq, sr_desc);
  gasneti_assert(sreq->count == 1);

  GASNETC_STAT_EVENT(POST_SR);
  #if GASNET_TRACE || GASNET_DEBUG
    gasnetc_snd_validate(sreq, sr_desc, 1, "POST_SR");
  #endif

  vstat = VAPI_post_sr(gasnetc_hca, sreq->cep->qp_handle, sr_desc);

  if_pt (vstat == VAPI_OK) {
    /* SUCCESS, the request is posted */
    return;
  } else if (GASNETC_IS_EXITING()) {
    /* disconnected by another thread */
    gasnetc_exit(0);
  } else {
    /* unexpected error */
    GASNETC_VAPI_CHECK(vstat, "while posting a send work request");
  }
}

/* Post an INLINE work request to the send queue of the given endpoint */
GASNET_INLINE_MODIFIER(gasnetc_snd_post_inline)
void gasnetc_snd_post_inline(gasnetc_sreq_t *sreq, VAPI_sr_desc_t *sr_desc) {
  VAPI_ret_t vstat;

  gasnetc_snd_post_common(sreq, sr_desc);
  gasneti_assert(sreq->count == 1);

  GASNETC_STAT_EVENT(POST_INLINE_SR);
  #if GASNET_TRACE || GASNET_DEBUG
    gasnetc_snd_validate(sreq, sr_desc, 1, "POST_INLINE_SR");
  #endif

  vstat = EVAPI_post_inline_sr(gasnetc_hca, sreq->cep->qp_handle, sr_desc);

  if_pt (vstat == VAPI_OK) {
    /* SUCCESS, the request is posted */
    return;
  } else if (GASNETC_IS_EXITING()) {
    /* disconnected by another thread */
    gasnetc_exit(0);
  } else {
    /* unexpected error */
    GASNETC_VAPI_CHECK(vstat, "while posting an inline send work request");
  }
}

#if GASNETC_PIN_SEGMENT
/* Post multiple work requests to the send queue of the given endpoint */
GASNET_INLINE_MODIFIER(gasnetc_snd_post_list)
void gasnetc_snd_post_list(gasnetc_sreq_t *sreq, int count, VAPI_sr_desc_t *sr_desc) {

  /* Can't handle bounce buffers or AMs (yet or ever?) */
  gasneti_assert(sreq->buffer = NULL);

  GASNETC_STAT_EVENT_VAL(SND_POST_LIST,count);

  do {
    gasnetc_sreq_t *next = NULL;
    VAPI_ret_t vstat;

    gasnetc_snd_post_list_common(sreq, sr_desc, count);
    gasneti_assert(sreq->count >= 1);

    if_pf (sreq->count < count) {
      /* If there is not enough SQ space, so we split the request list */
      next = gasnetc_get_sreq(0);
      next->cep = sreq->cep;
      next->mem_oust = sreq->mem_oust;  sreq->mem_oust = NULL;
      next->req_oust = sreq->req_oust;  sreq->req_oust = NULL;
    }

    GASNETC_STAT_EVENT_VAL(POST_SR_LIST,sreq->count);
    #if GASNET_TRACE || GASNET_DEBUG
      gasnetc_snd_validate(sreq, sr_desc, count, "POST_ST_LIST");
    #endif

    vstat = EVAPI_post_sr_list(gasnetc_hca, sreq->cep->qp_handle, sreq->count, sr_desc);

    if_pt (vstat == VAPI_OK) {
      /* SUCCESS, the requests are posted */
    } else if (GASNETC_IS_EXITING()) {
      /* disconnected by another thread */
      gasnetc_exit(0);
    } else {
      /* unexpected error */
      GASNETC_VAPI_CHECK(vstat, "while posting multiple send work requests");
    }

    count -= sreq->count;
    sr_desc += sreq->count;
    sreq = next;
  } while (sreq != NULL);
}
#endif

#if GASNETC_RCV_THREAD
static gasnetc_rbuf_t *gasnetc_rcv_thread_rbuf = NULL;
static void gasnetc_rcv_thread(VAPI_hca_hndl_t	hca_hndl,
			       VAPI_cq_hndl_t	cq_hndl,
			       void		*context) {
  VAPI_ret_t vstat;

  (void)gasnetc_rcv_reap(INT_MAX, &gasnetc_rcv_thread_rbuf);

  vstat = VAPI_req_comp_notif(gasnetc_hca, gasnetc_rcv_cq, VAPI_NEXT_COMP);

  if_pf (vstat != VAPI_OK) {
    if (GASNETC_IS_EXITING()) {
      /* disconnected by another thread */
      gasnetc_exit(0);
    } else {
      GASNETC_VAPI_CHECK(vstat, "from VAPI_req_comp_notif()");
    }
  }

  (void)gasnetc_rcv_reap(INT_MAX, &gasnetc_rcv_thread_rbuf);
}
#endif

GASNET_INLINE_MODIFIER(gasnetc_ReqRepGeneric)
int gasnetc_ReqRepGeneric(gasnetc_category_t category, int isReq,
			  int dest, gasnet_handler_t handler,
			  void *src_addr, int nbytes, void *dst_addr,
			  int numargs, gasnetc_counter_t *mem_oust,
			  gasnetc_counter_t *req_oust, va_list argptr) {
  gasnetc_sreq_t *sreq;
  gasnetc_buffer_t *buf;
  gasnet_handlerarg_t *args;
  uint32_t flags;
  size_t msg_len;
  int retval, i;
  int use_inline = 0;

  /* FIRST, if using firehose then Long requests may need AMs for moves.
   * Thus we do any RDMA before getting credits.
   */
  if ((category == gasnetc_Long) && nbytes) {
    if (dest == gasnetc_mynode) {
      memcpy(dst_addr, src_addr, nbytes);
    } else if (!GASNETC_PIN_SEGMENT && !isReq) {
      /* No RDMA for Long Reply's when using firehose, since we can't send the AM request(s).
       * We'll send it like a Medium below.
       */
    } else {
      /* XXX check for error returns */
      (void)gasnetc_rdma_put(dest, src_addr, dst_addr, nbytes, mem_oust, NULL);
    }
  }

  /* NEXT, get the flow-control credit needed for AM Requests.
   * This way we can be sure that we never hold the last pinned buffer
   * while spinning on the rcv queue waiting for credits.
   */
  if (isReq) {
    gasnetc_sema_t *sema = &gasnetc_cep[dest].am_sema;
    GASNETC_STAT_EVENT(GET_AMREQ_CREDIT);

    if_pf (!gasnetc_sema_trydown(sema, GASNETC_ANY_PAR)) {
      GASNETC_TRACE_WAIT_BEGIN();
      do {
	GASNETI_WAITHOOK();
        gasnetc_poll_rcv();
      } while (!gasnetc_sema_trydown(sema, GASNETC_ANY_PAR));
      GASNETC_TRACE_WAIT_END(GET_AMREQ_CREDIT_STALL);
    }
  }

  /* Now get an sreq and buffer to start building the message */
  sreq = gasnetc_get_sreq(1);
  buf = sreq->buffer;

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
    args = buf->longmsg.args;
    buf->longmsg.destLoc = (uintptr_t)dst_addr;
    buf->longmsg.nBytes  = nbytes;
    if (!GASNETC_PIN_SEGMENT && nbytes && (dest != gasnetc_mynode) && !isReq) {
      /* No RDMA for Long Reply's when using firehose, since we can't send the AM request(s) */
      memcpy(GASNETC_MSG_LONG_DATA(buf, numargs), src_addr, nbytes);
      msg_len = GASNETC_MSG_LONG_OFFSET(numargs) + nbytes;
      use_inline = (msg_len <= GASNETC_AM_INLINE_LIMIT);
    } else {
      msg_len = offsetof(gasnetc_buffer_t, longmsg.args[numargs]);
      use_inline = (sizeof(gasnetc_longmsg_t) <= GASNETC_AM_INLINE_LIMIT);
    }
    break;

  default:
    gasneti_fatalerror("invalid AM category on send");
    /* NOT REACHED */
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
    gasneti_freelist_put(&gasnetc_bbuf_freelist, buf);
    gasneti_freelist_put(&gasnetc_sreq_freelist, sreq);
    retval = GASNET_OK;
  } else {
    /* send the AM */
    GASNETC_DECL_SR_DESC(sr_desc, 1);
    sr_desc->opcode     = VAPI_SEND_WITH_IMM;
    sr_desc->imm_data   = flags;
    sr_desc->sg_lst_p[0].addr      = (uintptr_t)buf;
    sr_desc->sg_lst_p[0].len       = msg_len;
    sr_desc->sg_lst_p[0].lkey      = gasnetc_snd_reg.lkey;

    sreq->cep = &gasnetc_cep[dest];
    if_pf (req_oust) {
      gasnetc_counter_inc(req_oust);
      sreq->req_oust = req_oust;
    }

    if_pt (use_inline) {
      gasnetc_snd_post_inline(sreq, sr_desc);
    } else {
      gasnetc_snd_post(sreq, sr_desc);
    }

    retval = GASNET_OK;
  }

  GASNETI_RETURN(retval);
}

#if GASNETC_PIN_SEGMENT
/* Helper for rdma puts: inline send case */
static void gasnetc_do_put_inline(gasnetc_cep_t *cep, VAPI_rkey_t rkey,
                                  uintptr_t src, uintptr_t dst, size_t nbytes,
                                  gasnetc_counter_t *req_oust) {
  GASNETC_DECL_SR_DESC(sr_desc, 1);
  gasnetc_sreq_t *sreq;

  GASNETI_TRACE_EVENT_VAL(C, RDMA_PUT_INLINE, nbytes);

  gasneti_assert(nbytes != 0);
  gasneti_assert(nbytes <= GASNETC_PUT_INLINE_LIMIT);

  sreq = gasnetc_get_sreq(0);
  sreq->cep = cep;
  if (req_oust) {
    gasnetc_counter_inc(req_oust);
    sreq->req_oust = req_oust;
  }

  sr_desc->opcode      = VAPI_RDMA_WRITE;
  sr_desc->remote_addr = dst;
  sr_desc->r_key       = rkey;
  sr_desc->sg_lst_p[0].addr       = src;
  sr_desc->sg_lst_p[0].len        = nbytes;

  gasnetc_snd_post_inline(sreq, sr_desc);
}
      
/* Helper for rdma puts: bounce buffer case */
static void gasnetc_do_put_bounce(gasnetc_cep_t *cep, VAPI_rkey_t rkey,
                                  uintptr_t src, uintptr_t dst, size_t nbytes,
                                  gasnetc_counter_t *req_oust) {
  GASNETC_DECL_SR_DESC(sr_desc, 1);
  gasnetc_sreq_t *sreq;

  GASNETI_TRACE_EVENT_VAL(C, RDMA_PUT_BOUNCE, nbytes);

  gasneti_assert(nbytes != 0);

  /* Use full bounce buffers until just one buffer worth of data remains */
  while (nbytes > GASNETC_BUFSZ) {
    sreq = gasnetc_get_sreq(1);
    memcpy(sreq->buffer, (void *)src, GASNETC_BUFSZ);
    sreq->cep  = cep;

    sr_desc->opcode      = VAPI_RDMA_WRITE;
    sr_desc->remote_addr = dst;
    sr_desc->r_key       = rkey;

    sr_desc->sg_lst_p[0].addr = (uintptr_t)sreq->buffer;
    sr_desc->sg_lst_p[0].len  = GASNETC_BUFSZ;
    sr_desc->sg_lst_p[0].lkey = gasnetc_snd_reg.lkey;

    gasnetc_snd_post(sreq, sr_desc);

    src += GASNETC_BUFSZ;
    dst += GASNETC_BUFSZ;
    nbytes -= GASNETC_BUFSZ;
  }

  /* Send out the last buffer w/ the counter (if any) advanced */
  gasneti_assert(nbytes <= GASNETC_BUFSZ);

  sreq = gasnetc_get_sreq(1);
  memcpy(sreq->buffer, (void *)src, nbytes);
  sreq->cep = cep;
  if (req_oust) {
    gasnetc_counter_inc(req_oust);
    sreq->req_oust = req_oust;
  }

  sr_desc->opcode      = VAPI_RDMA_WRITE;
  sr_desc->remote_addr = dst;
  sr_desc->r_key       = rkey;

  sr_desc->sg_lst_p[0].addr = (uintptr_t)sreq->buffer;
  sr_desc->sg_lst_p[0].len  = nbytes;
  sr_desc->sg_lst_p[0].lkey = gasnetc_snd_reg.lkey;

  gasnetc_snd_post(sreq, sr_desc);
}

/* Helper for rdma puts: zero copy case */
static void gasnetc_do_put_zerocp(gasnetc_cep_t *cep, VAPI_lkey_t lkey, VAPI_rkey_t rkey,
                                  uintptr_t src, uintptr_t dst, size_t nbytes,
                                  gasnetc_counter_t *mem_oust, gasnetc_counter_t *req_oust) {
  GASNETC_DECL_SR_DESC(sr_desc, 1);
  gasnetc_sreq_t *sreq;
  size_t max_sz = gasnetc_hca_port.max_msg_sz;

  GASNETI_TRACE_EVENT_VAL(C, RDMA_PUT_ZEROCP, nbytes);

  gasneti_assert(nbytes != 0);

  /* Use max-sized messages until just one msg worth of data remains */
  if_pf (nbytes > max_sz) {
    do {
      sreq = gasnetc_get_sreq(0);
      sreq->cep = cep;

      sr_desc->opcode      = VAPI_RDMA_WRITE;
      sr_desc->remote_addr = dst;
      sr_desc->r_key       = rkey;

      sr_desc->sg_lst_p[0].addr = src;
      sr_desc->sg_lst_p[0].len  = max_sz;
      sr_desc->sg_lst_p[0].lkey = lkey;

      gasnetc_snd_post(sreq, sr_desc);

      src += max_sz;
      dst += max_sz;
      nbytes -= max_sz;
    } while (nbytes > max_sz);
  }

  /* Send out the last buffer w/ the counters (if any) advanced */
  gasneti_assert(nbytes <= max_sz);

  sreq = gasnetc_get_sreq(0);
  sreq->cep = cep;
  if (mem_oust) {
    gasnetc_counter_inc(mem_oust);
    sreq->mem_oust = mem_oust;
  }
  if (req_oust) {
    gasnetc_counter_inc(req_oust);
    sreq->req_oust = req_oust;
  }

  sr_desc->opcode      = VAPI_RDMA_WRITE;
  sr_desc->remote_addr = dst;
  sr_desc->r_key       = rkey;

  sr_desc->sg_lst_p[0].addr = src;
  sr_desc->sg_lst_p[0].len  = nbytes;
  sr_desc->sg_lst_p[0].lkey = lkey;

  gasnetc_snd_post(sreq, sr_desc);
}

/* Helper for rdma gets: bounce buffer case */
static void gasnetc_do_get_bounce(gasnetc_cep_t *cep, VAPI_rkey_t rkey,
                                  uintptr_t src, uintptr_t dst, size_t nbytes,
                                  gasnetc_counter_t *req_oust) {
  GASNETC_DECL_SR_DESC(sr_desc, 1);
  gasnetc_sreq_t *sreq;

  GASNETI_TRACE_EVENT_VAL(C, RDMA_GET_BOUNCE, nbytes);

  gasneti_assert(nbytes != 0);

  /* Use full bounce buffers until just one buffer worth of data remains */
  while (nbytes > GASNETC_BUFSZ) {
    sreq = gasnetc_get_sreq(1);
    sreq->cep  = cep;
    sreq->addr = (void *)dst;
    sreq->len  = GASNETC_BUFSZ;

    sr_desc->opcode      = VAPI_RDMA_READ;
    sr_desc->remote_addr = src;
    sr_desc->r_key       = rkey;

    sr_desc->sg_lst_p[0].addr = (uintptr_t)sreq->buffer;
    sr_desc->sg_lst_p[0].len  = GASNETC_BUFSZ;
    sr_desc->sg_lst_p[0].lkey = gasnetc_snd_reg.lkey;

    gasnetc_snd_post(sreq, sr_desc);

    src += GASNETC_BUFSZ;
    dst += GASNETC_BUFSZ;
    nbytes -= GASNETC_BUFSZ;
  }

  /* Send out the last buffer w/ the counter (if any) advanced */
  gasneti_assert(nbytes <= GASNETC_BUFSZ);

  sreq = gasnetc_get_sreq(1);
  sreq->cep  = cep;
  sreq->addr = (void *)dst;
  sreq->len  = nbytes;
  if (req_oust) {
    gasnetc_counter_inc(req_oust);
    sreq->req_oust = req_oust;
  }

  sr_desc->opcode      = VAPI_RDMA_READ;
  sr_desc->remote_addr = src;
  sr_desc->r_key       = rkey;

  sr_desc->sg_lst_p[0].addr = (uintptr_t)sreq->buffer;
  sr_desc->sg_lst_p[0].len  = nbytes;
  sr_desc->sg_lst_p[0].lkey = gasnetc_snd_reg.lkey;

  gasnetc_snd_post(sreq, sr_desc);
}

/* Helper for rdma gets: zero copy case */
static void gasnetc_do_get_zerocp(gasnetc_cep_t *cep, VAPI_lkey_t lkey, VAPI_rkey_t rkey,
                                  uintptr_t src, uintptr_t dst, size_t nbytes,
                                  gasnetc_counter_t *req_oust) {
  GASNETC_DECL_SR_DESC(sr_desc, 1);
  gasnetc_sreq_t *sreq;
  size_t max_sz = gasnetc_hca_port.max_msg_sz;

  GASNETI_TRACE_EVENT_VAL(C, RDMA_GET_ZEROCP, nbytes);

  gasneti_assert(nbytes != 0);

  /* Use max-sized messages until just one msg worth of data remains */
  if_pf (nbytes > max_sz) {
    do {
      sreq = gasnetc_get_sreq(0);
      sreq->cep = cep;

      sr_desc->opcode      = VAPI_RDMA_READ;
      sr_desc->remote_addr = src;
      sr_desc->r_key       = rkey;

      sr_desc->sg_lst_p[0].addr = dst;
      sr_desc->sg_lst_p[0].len  = max_sz;
      sr_desc->sg_lst_p[0].lkey = lkey;

      gasnetc_snd_post(sreq, sr_desc);

      src += max_sz;
      dst += max_sz;
      nbytes -= max_sz;
    } while (nbytes > max_sz);
  }

  /* Send out the last buffer w/ the counters (if any) advanced */
  gasneti_assert(nbytes <= max_sz);

  sreq = gasnetc_get_sreq(0);
  sreq->cep = cep;
  if (req_oust) {
    gasnetc_counter_inc(req_oust);
    sreq->req_oust = req_oust;
  }

  sr_desc->opcode      = VAPI_RDMA_READ;
  sr_desc->remote_addr = src;
  sr_desc->r_key       = rkey;

  sr_desc->sg_lst_p[0].addr = dst;
  sr_desc->sg_lst_p[0].len  = nbytes;
  sr_desc->sg_lst_p[0].lkey = lkey;

  gasnetc_snd_post(sreq, sr_desc);
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
  gasnetc_sreq_t	*sreq;
  int 			padded_size, count, i;

  /*
   * setup RCV resources
   */
  gasneti_assert(gasnetc_am_oust_pp * (gasnetc_nodes - 1) <= gasnetc_am_oust_limit);
  count = (gasnetc_am_oust_pp * 2) * (gasnetc_nodes - 1) + gasnetc_am_spares;

  /* create the RCV CQ */
  vstat = VAPI_create_cq(gasnetc_hca, count, &gasnetc_rcv_cq, &act_size);
  GASNETC_VAPI_CHECK(vstat, "from VAPI_create_cq(rcv_cq)");
  gasneti_assert(act_size >= count);

  if (gasnetc_nodes > 1) {
    #if GASNETC_RCV_THREAD
      /* create the RCV thread */
      vstat = EVAPI_set_comp_eventh(gasnetc_hca, gasnetc_rcv_cq, &gasnetc_rcv_thread,
				    NULL, &gasnetc_rcv_handler);
      GASNETC_VAPI_CHECK(vstat, "from EVAPI_set_comp_eventh()");
      vstat = VAPI_req_comp_notif(gasnetc_hca, gasnetc_rcv_cq, VAPI_NEXT_COMP);
      GASNETC_VAPI_CHECK(vstat, "from VAPI_req_comp_notif()");
    #endif

    /* Allocated pinned memory for receive buffers */
    buf = gasnetc_alloc_pinned(count * sizeof(gasnetc_buffer_t),
			       VAPI_EN_LOCAL_WRITE, &gasnetc_rcv_reg);
    gasneti_assert(buf != NULL);

    /* Allocated normal memory for receive descriptors (rbuf's) */
    padded_size = GASNETC_ALIGNUP(sizeof(gasnetc_rbuf_t), GASNETC_CACHE_LINE_SIZE);
    gasnetc_rbuf_alloc = gasneti_malloc(count*padded_size + GASNETC_CACHE_LINE_SIZE-1);

    /* Initialize the rbuf's */
    rbuf = (gasnetc_rbuf_t *)GASNETC_ALIGNUP(gasnetc_rbuf_alloc, GASNETC_CACHE_LINE_SIZE);
    for (i = 0; i < count; ++i) {
      rbuf->rr_desc.id         = (uintptr_t)rbuf;	/* CQE will point back to this request */
      rbuf->rr_desc.opcode     = VAPI_RECEIVE;
      rbuf->rr_desc.comp_type  = VAPI_SIGNALED;
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
      gasneti_assert(gasnetc_rcv_thread_rbuf != NULL);
    #endif
  }

  /*
   * setup SND resources
   */
  count = MIN(gasnetc_op_oust_limit, gasnetc_op_oust_pp * (gasnetc_nodes - 1));
  gasnetc_op_oust_limit = count;
  gasnetc_sema_init(&gasnetc_op_sema, count);

  /* create the SND CQ */
  vstat = VAPI_create_cq(gasnetc_hca, count, &gasnetc_snd_cq, &act_size);
  GASNETC_VAPI_CHECK(vstat, "from VAPI_create_cq(snd_cq)");
  gasneti_assert(act_size >= count);

  /* Allocated pinned memory for bounce buffers */
  count = MIN(gasnetc_bbuf_limit, gasnetc_op_oust_pp * gasnetc_nodes);
  gasnetc_bbuf_limit = count;
  buf = gasnetc_alloc_pinned(count * sizeof(gasnetc_buffer_t),
			     VAPI_EN_LOCAL_WRITE, &gasnetc_snd_reg);
  gasneti_assert(buf != NULL);
  for (i = 0; i < count; ++i) {
    gasneti_freelist_put(&gasnetc_bbuf_freelist, buf);
    ++buf;
  }

  /* Allocated normal memory for send requests (sreq's) */
  padded_size = GASNETC_ALIGNUP(MAX(sizeof(gasnetc_sreq_t),
				    sizeof(gasneti_freelist_ptr_t)),
			        GASNETC_CACHE_LINE_SIZE);
  gasnetc_sreq_alloc = gasneti_malloc(count*padded_size + GASNETC_CACHE_LINE_SIZE-1);
  sreq = (gasnetc_sreq_t *)GASNETC_ALIGNUP(gasnetc_sreq_alloc, GASNETC_CACHE_LINE_SIZE);
  for (i = 0; i < count; ++i) {
    gasneti_freelist_put(&gasnetc_sreq_freelist, sreq);
    sreq = (gasnetc_sreq_t *)((uintptr_t)sreq + padded_size);
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
      GASNETC_VAPI_CHECK(vstat, "from EVAPI_clear_comp_eventh()");
    #endif

    gasnetc_free_pinned(&gasnetc_rcv_reg);
    gasneti_free(gasnetc_rbuf_alloc);

    gasnetc_free_pinned(&gasnetc_snd_reg);
    
    /* XXX: can only free the "big" piece here.
     * So we  leak any singletons we may have allocated
     */
    gasneti_free(gasnetc_sreq_alloc);
  }

  vstat = VAPI_destroy_cq(gasnetc_hca, gasnetc_rcv_cq);
  GASNETC_VAPI_CHECK(vstat, "from VAPI_destroy_cq(rcv_cq)");

  vstat = VAPI_destroy_cq(gasnetc_hca, gasnetc_snd_cq);
  GASNETC_VAPI_CHECK(vstat, "from VAPI_destroy_cq(snd_cq)");
}

extern void gasnetc_sndrcv_fini_cep(gasnetc_cep_t *cep) {
  VAPI_ret_t vstat;

  if (cep != &gasnetc_cep[gasnetc_mynode]) {
    vstat = VAPI_destroy_qp(gasnetc_hca, cep->qp_handle);
    GASNETC_VAPI_CHECK(vstat, "from VAPI_destroy_qp()");
  }
}

extern void gasnetc_sndrcv_poll(void) {
  gasnetc_poll_both();
}

extern void gasnetc_counter_wait_aux(gasnetc_counter_t *counter, int handler_context)
{
  if (handler_context) {
    do {
      /* must not poll rcv queue in hander context */
      GASNETI_WAITHOOK();
      gasnetc_poll_snd();
    } while (!gasnetc_counter_done(counter));
  } else {
    do {
      GASNETI_WAITHOOK();
      gasnetc_poll_both();
    } while (!gasnetc_counter_done(counter));
  }
}

#if GASNETC_PIN_SEGMENT
/*
 * ############################################
 * RDMA ops used when the segment is pre-pinned
 * ############################################
 */

/* Perform an RDMA put
 *
 * Uses bounce buffers when the source is not pinned, or is "small enough" and the caller is
 * planning to wait for local completion.  Otherwise zero-copy is used when the source is pinned.
 */
extern int gasnetc_rdma_put(int node, void *src_ptr, void *dst_ptr, size_t nbytes, gasnetc_counter_t *mem_oust, gasnetc_counter_t *req_oust) {
  gasnetc_cep_t *cep = &gasnetc_cep[node];
  VAPI_rkey_t rkey = cep->rkey;
  uintptr_t src = (uintptr_t)src_ptr;
  uintptr_t dst = (uintptr_t)dst_ptr;

  gasneti_assert(nbytes != 0);
  
#if 0
  /* XXX: experimental
   * Try to perform "prefix cleanup" of unaligned transfers
   */
  #define GASNETC_PREFIX_ALIGN	2048
  #define GASNETC_PREFIX_MASK	(GASNETC_PREFIX_ALIGN - 1)
  #define GASNETC_PREFIX_COPY_LIMIT	8192
  if ((nbytes > GASNETC_PREFIX_ALIGN) && (src & GASNETC_PREFIX_MASK)) {
    if (nbytes <= GASNETC_PREFIX_COPY_LIMIT) {
      /* just copy it to realign */
      gasnetc_do_put_bounce(cep, rkey, src, dst, nbytes, req_oust);
      return 0;
    } else {
      /* send enough to leave the rest aligned */
      size_t size = GASNETC_PREFIX_ALIGN - (src & GASNETC_PREFIX_MASK);

      if ((GASNETC_PUT_INLINE_LIMIT != 0) && (size <= GASNETC_PUT_INLINE_LIMIT)) {
        gasnetc_do_put_inline(cep, rkey, src, dst, size, req_oust);
      } else {
        gasnetc_do_put_bounce(cep, rkey, src, dst, size, req_oust);
      }

      src += size;
      dst += size;
      nbytes -= size;
    }
  }
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
      size_t count = nbytes;	/* in/out parameter to gasnetc_in_segment() */

      if_pt (gasnetc_in_segment(src, &count)) {
        /* Source in segment - use zero copy RDMA write */
	gasnetc_do_put_zerocp(cep, gasnetc_seg_reg.lkey, rkey, src, dst, count, mem_oust, req_oust);
      } else {
        /* Source not in segment - use bounce buffers */
	gasnetc_do_put_bounce(cep, rkey, src, dst, count, req_oust);
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
extern int gasnetc_rdma_get(int node, void *src_ptr, void *dst_ptr, size_t nbytes, gasnetc_counter_t *req_oust) {
  gasnetc_cep_t *cep = &gasnetc_cep[node];
  VAPI_rkey_t rkey = cep->rkey;
  uintptr_t src = (uintptr_t)src_ptr;
  uintptr_t dst = (uintptr_t)dst_ptr;

  gasneti_assert(nbytes != 0);

  do {
    size_t count = nbytes;	/* in/out parameter to gasnetc_in_segment() */

    if_pt (gasnetc_in_segment(dst, &count)) {
      /* Destination in segment - use zero copy RDMA read */
      gasnetc_do_get_zerocp(cep, gasnetc_seg_reg.lkey, rkey, src, dst, count, req_oust);
    } else {
      /* Destination not in segment - use bounce buffers */
      gasnetc_do_get_bounce(cep, rkey, src, dst, count, req_oust);
    }

    src += count;
    dst += count;
    nbytes -= count;
  } while (nbytes);

  return 0;
}

/* write a constant pattern to remote memory using a local memset and an RDMA put */
extern int gasnetc_rdma_memset(int node, void *dst_ptr, int val, size_t nbytes, gasnetc_counter_t *req_oust) {
  GASNETC_DECL_SR_DESC(sr_desc, 1);
  gasnetc_cep_t *cep = &gasnetc_cep[node];
  uintptr_t dst = (uintptr_t)dst_ptr;
  gasnetc_sreq_t *sreq;

  gasneti_assert(nbytes != 0);
	  
  do {
    size_t count = MIN(nbytes, GASNETC_BUFSZ);

    sreq = gasnetc_get_sreq(1);
    memset(sreq->buffer, val, count);
    sreq->cep = cep;
    if (req_oust) {
      gasnetc_counter_inc(req_oust);
      sreq->req_oust = req_oust;
    }

    sr_desc->opcode      = VAPI_RDMA_WRITE;
    sr_desc->remote_addr = dst;
    sr_desc->r_key       = cep->rkey;

    sr_desc->sg_lst_p[0].addr = (uintptr_t)sreq->buffer;
    sr_desc->sg_lst_p[0].len  = count;
    sr_desc->sg_lst_p[0].lkey = gasnetc_snd_reg.lkey;

    gasnetc_snd_post(sreq, sr_desc);
     
    dst += count;
    nbytes -= count;
  } while (nbytes);

  return 0;
}
#elif GASNETC_USE_FIREHOSE
/*
 * #######################################
 * RDMA ops used with the firehose library
 * #######################################
 */

static void gasnetc_fh_put_inline(void *context, const firehose_request_t *req, int allLocalHit) {
  GASNETC_DECL_SR_DESC(sr_desc, 1);
  gasnetc_sreq_t *sreq = context;

  gasneti_assert(req == &(sreq->fh_rem));

  sreq->fh_ptr[0] = &(sreq->fh_rem);

  sr_desc->opcode = VAPI_RDMA_WRITE;
  sr_desc->remote_addr = sreq->rem_addr;
  sr_desc->r_key = sreq->fh_rem.client.rkey;
  sr_desc->sg_lst_p[0].addr = sreq->loc_addr;
  sr_desc->sg_lst_p[0].len = sreq->fh_len;

  gasnetc_snd_post_inline(sreq, sr_desc);
}

GASNET_INLINE_MODIFIER(gasnetc_fh_post)
void gasnetc_fh_post(gasnetc_sreq_t *sreq) {
  GASNETC_DECL_SR_DESC(sr_desc, 1);

  sreq->fh_ptr[0] = &(sreq->fh_rem);
  sreq->fh_ptr[1] = &(sreq->fh_loc);

  sr_desc->opcode = sreq->opcode;
  sr_desc->remote_addr = sreq->rem_addr;
  sr_desc->r_key = sreq->fh_rem.client.rkey;
  sr_desc->sg_lst_p[0].addr = sreq->loc_addr;
  sr_desc->sg_lst_p[0].len = sreq->fh_len;
  sr_desc->sg_lst_p[0].lkey = sreq->fh_loc.client.lkey;

  gasnetc_snd_post(sreq, sr_desc);
}

static void gasnetc_fh_getput(void *context, const firehose_request_t *req, int allLocalHit) {
  gasnetc_sreq_t *sreq = context;

  gasneti_assert(req == &(sreq->fh_rem));

  if (gasneti_atomic_decrement_and_test(&sreq->fh_oust)) {
    gasnetc_fh_post(sreq);
  }
}

/* We get here when we have a hit on the remote firehose table.
 * We initiate exactly one RDMA, returning the number of bytes it contains.
 */
GASNET_INLINE_MODIFIER(gasnetc_fh_hit)
size_t gasnetc_fh_hit(gasnet_node_t node, gasnetc_sreq_t *sreq,
		      uintptr_t loc_addr, size_t len) {
  const firehose_request_t *req;
  size_t limit;

  /* Local (mis)alignment could limit how much we can pin */
  limit = gasnetc_fh_maxsz - (loc_addr & (FH_BUCKET_SIZE - 1));
  len = MIN(len, limit);
  sreq->fh_len = len;

  req = firehose_local_pin(loc_addr, len, &sreq->fh_loc);
  gasneti_assert(req == &(sreq->fh_loc));

  gasnetc_fh_post(sreq);

  return len;
}

/* We get here when we have a miss on the remote firehose table.
 * We initiate exactly one RDMA, returning the number of bytes it contains.
 */
GASNET_INLINE_MODIFIER(gasnetc_fh_miss)
size_t gasnetc_fh_miss(gasnet_node_t node, gasnetc_sreq_t *sreq,
		       uintptr_t loc_addr, uintptr_t rem_addr, size_t len) {
  const firehose_request_t *req;
  size_t limit;

  /* Both local and remote (mis)alignment could limit how much we can pin */
  limit = gasnetc_fh_maxsz - MAX(loc_addr & (FH_BUCKET_SIZE - 1),
				 rem_addr & (FH_BUCKET_SIZE - 1));
  len = MIN(len, limit);
  sreq->fh_len = len;

  gasneti_atomic_set(&sreq->fh_oust, 2);
  req = firehose_remote_pin(node, rem_addr, len, 0, &sreq->fh_rem,
			    NULL, &gasnetc_fh_getput, sreq);
  gasneti_assert(req == NULL);

  req = firehose_local_pin(loc_addr, len, &sreq->fh_loc);
  gasneti_assert(req == &(sreq->fh_loc));
  
  if (gasneti_atomic_decrement_and_test(&sreq->fh_oust)) {
    gasnetc_fh_post(sreq);
  }

  return len;
}

GASNET_INLINE_MODIFIER(gasnetc_fh_helper)
int gasnetc_fh_helper(int is_put, gasnet_node_t node, gasnetc_sreq_t *sreq,
		      uintptr_t loc_addr, uintptr_t rem_addr, size_t len) {
  const firehose_request_t *req;

  sreq->rem_addr = rem_addr;
  sreq->loc_addr = loc_addr;

  if (is_put && (GASNETC_PUT_INLINE_LIMIT != 0) && (len <= GASNETC_PUT_INLINE_LIMIT)) {
    sreq->fh_len = len;
    req = firehose_remote_pin(node, rem_addr, len, 0, &sreq->fh_rem,
			      NULL, &gasnetc_fh_put_inline, sreq);
    gasneti_assert(req == NULL);
  } else {
    /* See how much (if any) is already pinned.
     * Note that it is safe to ask about 'len' without checking any limits first.  */
    req = firehose_partial_remote_pin(node, rem_addr, len, 0, &sreq->fh_rem);
    gasneti_assert((req == NULL) || (req == &sreq->fh_rem));

    if_pt (req && (req->addr <= rem_addr)) {
      /* HIT in remote firehose table - some initial part of the region is pinned */
      len = MIN(len, req->addr + req->len - rem_addr);	/* trim to pinned region */
      len = gasnetc_fh_hit(node, sreq, loc_addr, len);
    } else {
      /* Some initial part (or all) of the region is unpinned */
      if (req) {
        /* XXX: should we try to initiate RDMA here rather then releasing?
	 * If we can't easily split into two simple ranges, then the bookkeeping
	 * is probably too much to justify it.
	 */
        firehose_release(&req, 1);	/* avoid deadlock */
      }
      len = gasnetc_fh_miss(node, sreq, loc_addr, rem_addr, len);
    }
  }

  return len;
}

/* RDMA put */
extern int gasnetc_rdma_put(int node, void *src_ptr, void *dst_ptr, size_t nbytes, gasnetc_counter_t *mem_oust, gasnetc_counter_t *req_oust) {
  gasnetc_cep_t *cep = &gasnetc_cep[node];
  uintptr_t src = (uintptr_t)src_ptr;
  uintptr_t dst = (uintptr_t)dst_ptr;

  GASNETI_TRACE_EVENT_VAL(C, RDMA_PUT_FH, nbytes);

  gasneti_assert(nbytes != 0);

  do {
    gasnetc_sreq_t *sreq = gasnetc_get_sreq(0);
    size_t count;

    sreq->cep    = cep;
    sreq->opcode = VAPI_RDMA_WRITE;
 
    /* We must set counters on all chunks since order of completion is uncertain */
    if (mem_oust) {
      gasnetc_counter_inc(mem_oust);
      sreq->mem_oust = mem_oust;
    }
    if (req_oust) {
      gasnetc_counter_inc(req_oust);
      sreq->req_oust = req_oust;
    }

    count = gasnetc_fh_helper(1, node, sreq, src, dst, nbytes);

    src += count;
    dst += count;
    nbytes -= count;
  } while (nbytes);

  return 0;
}

/* Perform an RDMA get */
extern int gasnetc_rdma_get(int node, void *src_ptr, void *dst_ptr, size_t nbytes, gasnetc_counter_t *req_oust) {
  gasnetc_cep_t *cep = &gasnetc_cep[node];
  uintptr_t src = (uintptr_t)src_ptr;
  uintptr_t dst = (uintptr_t)dst_ptr;

  GASNETI_TRACE_EVENT_VAL(C, RDMA_GET_FH, nbytes);

  gasneti_assert(nbytes != 0);

  do {
    gasnetc_sreq_t *sreq = gasnetc_get_sreq(0);
    size_t count;

    sreq->cep    = cep;
    sreq->opcode = VAPI_RDMA_READ;
 
    /* We must set counters on all chunks since order of completion is uncertain */
    if (req_oust) {
      gasnetc_counter_inc(req_oust);
      sreq->req_oust = req_oust;
    }

    count = gasnetc_fh_helper(0, node, sreq, dst, src, nbytes);

    src += count;
    dst += count;
    nbytes -= count;
  } while (nbytes);

  return 0;
}
#else
  #error "Not using firehose, nor prepinning the segment."
#endif

extern int gasnetc_RequestGeneric(gasnetc_category_t category,
				  int dest, gasnet_handler_t handler,
				  void *src_addr, int nbytes, void *dst_addr,
				  int numargs, gasnetc_counter_t *mem_oust, va_list argptr) {
  gasnetc_poll_rcv();	/* ensure progress */

  return gasnetc_ReqRepGeneric(category, 1, dest, handler,
                               src_addr, nbytes, dst_addr,
                               numargs, mem_oust, NULL, argptr);
}

extern int gasnetc_ReplyGeneric(gasnetc_category_t category,
				gasnet_token_t token, gasnet_handler_t handler,
				  void *src_addr, int nbytes, void *dst_addr,
				  int numargs, gasnetc_counter_t *mem_oust, va_list argptr) {
  gasnetc_rbuf_t *rbuf = (gasnetc_rbuf_t *)token;
  int retval;

  gasneti_assert(rbuf);
  gasneti_assert(rbuf->rbuf_handlerRunning);
  gasneti_assert(GASNETC_MSG_ISREQUEST(rbuf->rbuf_flags));
  gasneti_assert(rbuf->rbuf_needReply);

  retval = gasnetc_ReqRepGeneric(category, 0, GASNETC_MSG_SRCIDX(rbuf->rbuf_flags), handler,
				 src_addr, nbytes, dst_addr,
				 numargs, mem_oust, NULL, argptr);

  rbuf->rbuf_needReply = 0;
  return retval;
}

extern int gasnetc_RequestSystem(gasnet_node_t dest,
			         gasnetc_counter_t *req_oust,
                                 gasnet_handler_t handler,
                                 int numargs, ...) {
  int retval;
  va_list argptr;

  gasnetc_poll_rcv();	/* ensure progress */

  GASNETC_TRACE_SYSTEM_REQUEST(dest,handler,numargs);

  va_start(argptr, numargs);
  retval = gasnetc_ReqRepGeneric(gasnetc_System, 1, dest, handler,
				 NULL, 0, NULL, numargs, NULL, req_oust, argptr);
  va_end(argptr);
  return retval;
}

extern int gasnetc_ReplySystem(gasnet_token_t token,
			       gasnetc_counter_t *req_oust,
                               gasnet_handler_t handler,
                               int numargs, ...) {
  gasnetc_rbuf_t *rbuf = (gasnetc_rbuf_t *)token;
  int retval;
  va_list argptr;
  gasnet_node_t dest;

  gasneti_assert(rbuf);

  dest = GASNETC_MSG_SRCIDX(rbuf->rbuf_flags);

  GASNETC_TRACE_SYSTEM_REPLY(dest,handler,numargs);

  va_start(argptr, numargs);
  retval = gasnetc_ReqRepGeneric(gasnetc_System, 0, dest, handler,
				 NULL, 0, NULL, numargs, NULL, req_oust, argptr);
  va_end(argptr);

  rbuf->rbuf_needReply = 0;
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

  flags = ((gasnetc_rbuf_t *)token)->rbuf_flags;

  if (GASNETC_MSG_CATEGORY(flags) != gasnetc_System) {
    GASNETI_CHECKATTACH();
  }

  sourceid = GASNETC_MSG_SRCIDX(flags);

  gasneti_assert(sourceid < gasnetc_nodes);
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

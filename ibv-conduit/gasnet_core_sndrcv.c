/*   $Source: /Users/kamil/work/gasnet-cvs2/gasnet/ibv-conduit/gasnet_core_sndrcv.c,v $
 *     $Date: 2005/03/22 06:15:28 $
 * $Revision: 1.79.4.1 $
 * Description: GASNet vapi conduit implementation, transport send/receive logic
 * Copyright 2003, LBNL
 * Terms of use are as specified in license.txt
 */

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
size_t					gasnetc_fh_maxsz;
int					gasnetc_use_rcv_thread = GASNETC_VAPI_RCV_THREAD;

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

  /* Buffer when needed for bounced GETs or PUTs, or for an AM header */
  gasnetc_buffer_t		*buffer;

  /* Firehose data */
  #if GASNETC_PIN_SEGMENT
    const firehose_request_t	*fh_ptr[1];
    firehose_request_t          fh_loc;
  #else
    VAPI_wr_opcode_t		opcode;
    size_t			fh_len;
    uintptr_t			loc_addr, rem_addr;
    const firehose_request_t	*fh_ptr[2];
    firehose_request_t          fh_loc, fh_rem;
    gasneti_atomic_t		fh_oust;
    gasnetc_counter_t		*am_oust;	/* fh transactions outstanding */
  #endif
} gasnetc_sreq_t;

/* ------------------------------------------------------------------------------------ *
 *  File-scoped variables
 * ------------------------------------------------------------------------------------ */

static void				*gasnetc_sreq_alloc;
static void				*gasnetc_rbuf_alloc;
static EVAPI_compl_handler_hndl_t	gasnetc_rcv_handler;

static gasneti_freelist_t		gasnetc_bbuf_freelist = GASNETI_FREELIST_INITIALIZER;
static gasneti_freelist_t		gasnetc_sreq_freelist = GASNETI_FREELIST_INITIALIZER;
static gasneti_freelist_t		gasnetc_rbuf_freelist = GASNETI_FREELIST_INITIALIZER;
static gasnetc_sema_t			gasnetc_cq_sema;

/* ------------------------------------------------------------------------------------ *
 *  File-scoped functions and macros                                                    *
 * ------------------------------------------------------------------------------------ */

#define GASNETC_SR_ALIGN GASNETI_CACHE_LINE_BYTES
#define GASNETC_DECL_SR_DESC(_name, _len) \
	char _CONCAT(_name,_align)[sizeof(VAPI_sr_desc_t)+_len*sizeof(VAPI_sg_lst_entry_t)+2*GASNETC_SR_ALIGN];\
	VAPI_sr_desc_t *_name = (VAPI_sr_desc_t *)GASNETC_ALIGNUP(_CONCAT(_name,_align), GASNETC_SR_ALIGN);\
	void *_CONCAT(_name,_dummy) = (void *)(_name->sg_lst_len=_len,_name->sg_lst_p=(VAPI_sg_lst_entry_t *)(sizeof(VAPI_sr_desc_t)+(uintptr_t)_name)) /* note intentional lack of final semicolon */

/* Use of IB's 32-bit immediate data:
 *   0-1: category
 *     2: request (0) or reply (1)
 *   3-7: numargs (5 bits, but only 0-16 are legal values)
 *  8-15: handlerID
 * 16-29: source node (14 bit LID space in IB)
 * 30-31: UNUSED
 */

#define GASNETC_MSG_NUMARGS(flags)      (((flags) >> 3) & 0x1f)
#define GASNETC_MSG_ISREPLY(flags)      ((flags) & 0x4)
#define GASNETC_MSG_ISREQUEST(flags)    (!GASNETC_MSG_ISREPLY(flags))
#define GASNETC_MSG_CATEGORY(flags)     ((gasnetc_category_t)((flags) & 0x3))
#define GASNETC_MSG_HANDLERID(flags)    ((gasnet_handler_t)((flags) >> 8))
#define GASNETC_MSG_SRCIDX(flags)       ((gasnet_node_t)((flags) >> 16) & 0x3fff)

#define GASNETC_MSG_GENFLAGS(isreq, cat, nargs, hand, srcidx)   \
 (gasneti_assert(!((isreq) & ~1)),              \
  gasneti_assert(!((cat) & ~3)),                \
  gasneti_assert((nargs) <= GASNETC_MAX_ARGS),  \
  gasneti_assert((srcidx) < gasneti_nodes),     \
  (uint32_t)(  (((srcidx) & 0x3fff) << 16)      \
	     | (((hand)   & 0xff  ) << 8 )      \
	     | (((nargs)  & 0x1f  ) << 3 )      \
	     | ((!(isreq)         ) << 2 )      \
	     | (((cat)    & 0x3   )      )))

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

/* Post a work request to the receive queue of the given endpoint */
GASNET_INLINE_MODIFIER(gasnetc_rcv_post)
void gasnetc_rcv_post(gasnetc_cep_t *cep, gasnetc_rbuf_t *rbuf) {
  VAPI_ret_t vstat;

  gasneti_assert(cep);
  gasneti_assert(rbuf);

  /* check for attempted loopback traffic */
  gasneti_assert(cep != &gasnetc_cep[gasneti_mynode]);
  
  vstat = VAPI_post_rr(gasnetc_hca, cep->qp_handle, &rbuf->rr_desc);

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
        GASNETI_RUN_HANDLER_SHORT(handler_fn,rbuf,args,numargs);
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
        GASNETI_RUN_HANDLER_MEDIUM(handler_fn,rbuf,args,numargs,data,nbytes);
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
	    if (GASNETC_MSG_SRCIDX(flags) != gasneti_mynode) {
	      /* No RDMA for ReplyLong.  So, must relocate the payload. */
	      memcpy(data, GASNETC_MSG_LONG_DATA(buf, numargs), nbytes);
	    }
	  #endif
          GASNETI_TRACE_AMLONG_REPHANDLER(handler_id, rbuf, data, (int)nbytes, numargs, args);
	}
        GASNETI_RUN_HANDLER_LONG(handler_fn,rbuf,args,numargs,data,nbytes);
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
	  gasnetc_sema_up_n(&sreq->cep->sq_sema, sreq->count);
          GASNETC_SEMA_CHECK(&sreq->cep->sq_sema, gasnetc_op_oust_pp);
	  gasnetc_sema_up(&gasnetc_cq_sema);
          GASNETC_SEMA_CHECK(&gasnetc_cq_sema, gasnetc_op_oust_limit);

	  switch (comp.opcode) {
	  case VAPI_CQE_SQ_RDMA_READ:	/* Get */
	    gasneti_assert(sreq->req_oust != NULL);
	    gasneti_assert(sreq->mem_oust == NULL);

            gasnetc_counter_dec(sreq->req_oust);

	    #if !GASNETC_PIN_SEGMENT
	    if (sreq->fh_ptr[1]) {
	      firehose_release(sreq->fh_ptr, 2);
	    } else
	    #endif
	    if (sreq->fh_ptr[0]) {
	      firehose_release(sreq->fh_ptr, 1);
	    }
	    break;

	  case VAPI_CQE_SQ_RDMA_WRITE:	/* Put */
            if (sreq->mem_oust) {
	      gasneti_assert(sreq->buffer == NULL);
	      gasnetc_counter_dec(sreq->mem_oust);
	    } else if (sreq->buffer) {
	      gasneti_freelist_put(&gasnetc_bbuf_freelist, sreq->buffer);
	    }
            if (sreq->req_oust) {
              gasnetc_counter_dec(sreq->req_oust);
	    }

	    #if !GASNETC_PIN_SEGMENT
	    if (sreq->fh_ptr[1]) {
	      firehose_release(sreq->fh_ptr, 2);
	    } else
	    #endif
	    if (sreq->fh_ptr[0]) {
	      firehose_release(sreq->fh_ptr, 1);
	    }
	    break;

	  case VAPI_CQE_SQ_SEND_DATA:	/* AM send */
	    gasneti_assert(sreq->mem_oust == NULL);
	    gasneti_assert(sreq->buffer != NULL);
	  
            if (sreq->req_oust) {
              gasnetc_counter_dec(sreq->req_oust);
	    }

	    gasneti_freelist_put(&gasnetc_bbuf_freelist, sreq->buffer);
	    break;

	  default:
	    gasneti_fatalerror("Reaped send with invalid/unknown opcode %d", (int)comp.opcode);
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
        fprintf(stderr, "@ %d> snd comp.status=%d comp.opcode=%d\n", gasneti_mynode, comp.status, comp.opcode);
        while((vstat = VAPI_poll_cq(gasnetc_hca, gasnetc_rcv_cq, &comp)) == VAPI_OK) {
	  if (comp.status != VAPI_WR_FLUSH_ERR) {
            fprintf(stderr, "@ %d> - rcv comp.status=%d\n", gasneti_mynode, comp.status);
	  }
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
  gasnetc_rbuf_t *spare;

  if (GASNETC_MSG_ISREPLY(flags)) {
#if GASNETI_STATS_OR_TRACE
    gasneti_stattime_t _starttime = ((gasnetc_buffer_t *)(uintptr_t)(rbuf->rr_sg.addr))->stamp;
    GASNETI_TRACE_EVENT_TIME(C,AM_ROUNDTRIP_TIME,GASNETI_STATTIME_NOW()-_starttime);
#endif

    /* Process the implicitly received flow control credit */
    gasnetc_sema_up(&cep->am_sema);
    GASNETC_SEMA_CHECK(&(cep->am_sema), gasnetc_am_oust_pp);

    /* Now process the packet */
    gasnetc_processPacket(rbuf, flags);

    /* Return the rcv buffer to the free list */
    gasneti_freelist_put(&gasnetc_rbuf_freelist, rbuf);
  } else {
    /* Post a replacement buffer before processing the request.
     * This ensures that the credit implicitly sent with every reply will
     * have a corresponding buffer available at this end */
    spare = (*spare_p) ? (*spare_p) : gasneti_freelist_get(&gasnetc_rbuf_freelist);
    if_pt (spare) {
      /* This is the normal case */
      gasnetc_rcv_post(cep, spare);
      *spare_p = rbuf;	/* recv'd rbuf becomes the spare for next pass (if any) */
    } else {
      /* Because we don't have any "spare" rbuf available to post we copy the recvd
       * message to a temporary (non-pinned) buffer so we can repost rbuf.
       */
      gasnetc_buffer_t *buf = gasneti_malloc(sizeof(gasnetc_buffer_t));
      memcpy(buf, (void *)(uintptr_t)rbuf->rr_sg.addr, sizeof(gasnetc_buffer_t));
      emergency_spare.rr_sg.addr = (uintptr_t)buf;
  
      gasnetc_rcv_post(cep, rbuf);

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
  }

  #if !defined(FIREHOSE_COMPLETION_IN_HANDLER)
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
        fprintf(stderr, "@ %d> rcv comp.status=%d\n", gasneti_mynode, comp.status);
        while((vstat = VAPI_poll_cq(gasnetc_hca, gasnetc_snd_cq, &comp)) == VAPI_OK) {
	  if (comp.status != VAPI_WR_FLUSH_ERR) {
            fprintf(stderr, "@ %d> - snd comp.status=%d\n", gasneti_mynode, comp.status);
	  }
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

/* allocate a send request structure, trying to reap existing ones first */
#ifdef __GNUC__
  GASNET_INLINE_MODIFIER(gasnetc_get_sreq)
  gasnetc_sreq_t *gasnetc_get_sreq(void) __attribute__((__malloc__));
#endif
GASNET_INLINE_MODIFIER(gasnetc_get_sreq)
gasnetc_sreq_t *gasnetc_get_sreq(void) {
  gasnetc_sreq_t *sreq;
  gasnetc_sreq_t *tail;
  int count;

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
  sreq->buffer = NULL;
  sreq->mem_oust = NULL;
  sreq->req_oust = NULL;
  sreq->fh_ptr[0] = NULL;
  #if !GASNETC_PIN_SEGMENT
    sreq->fh_ptr[1] = NULL;
    sreq->am_oust = NULL;
  #endif

  return sreq;
}

/* allocate a pre-pinned bounce buffer */
#ifdef __GNUC__
  GASNET_INLINE_MODIFIER(gasnetc_get_bbuf)
  gasnetc_buffer_t *gasnetc_get_bbuf(int block) __attribute__((__malloc__));
#endif
GASNET_INLINE_MODIFIER(gasnetc_get_bbuf)
gasnetc_buffer_t *gasnetc_get_bbuf(int block) {
  gasnetc_buffer_t *bbuf = NULL;

  GASNETC_TRACE_WAIT_BEGIN();
  GASNETC_STAT_EVENT(GET_BBUF);

  bbuf = gasneti_freelist_get(&gasnetc_bbuf_freelist);
  if_pf (!bbuf) {
    gasnetc_poll_snd();
    bbuf = gasneti_freelist_get(&gasnetc_bbuf_freelist);
    if (block) {
      while (!bbuf) {
        GASNETI_WAITHOOK();
        gasnetc_poll_snd();
        bbuf = gasneti_freelist_get(&gasnetc_bbuf_freelist);
      }
    }
    GASNETC_TRACE_WAIT_END(POST_SR_STALL_CQ);
  }
  gasneti_assert((bbuf != NULL) || !block);

  return bbuf;
}

#if GASNET_TRACE || GASNET_DEBUG
GASNET_INLINE_MODIFIER(gasnetc_snd_validate)
void gasnetc_snd_validate(gasnetc_sreq_t *sreq, VAPI_sr_desc_t *sr_desc, int count, const char *type) {
  int i;

  gasneti_assert(sreq);
  gasneti_assert(sreq->cep);
  gasneti_assert(sreq->cep != &gasnetc_cep[gasneti_mynode]); /* detects loopback */
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
  gasnetc_sema_t *sq_sema;
  int i;

  /* Loop until space is available on the SQ for 1 new entry.
   * If we hold the last one then threads sending to the same node will stall. */
  sq_sema = &sreq->cep->sq_sema;
  if_pf (!gasnetc_sema_trydown(sq_sema, GASNETC_ANY_PAR)) {
    GASNETC_TRACE_WAIT_BEGIN();
    do {
      GASNETI_WAITHOOK();
      gasnetc_poll_snd();
    } while (!gasnetc_sema_trydown(sq_sema, GASNETC_ANY_PAR));
    GASNETC_TRACE_WAIT_END(POST_SR_STALL_SQ);
  }

  /* Loop until space is available for 1 new entry on the CQ.
   * If we hold the last one then threads sending to ANY node will stall. */
  if_pf (!gasnetc_sema_trydown(&gasnetc_cq_sema, GASNETC_ANY_PAR)) {
    GASNETC_TRACE_WAIT_BEGIN();
    do {
      GASNETI_WAITHOOK();
      gasnetc_poll_snd();
    } while (!gasnetc_sema_trydown(&gasnetc_cq_sema, GASNETC_ANY_PAR));
    GASNETC_TRACE_WAIT_END(POST_SR_STALL_CQ);
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
  gasnetc_sema_t *sq_sema;
  uint32_t tmp;
  int i;

  /* Loop until space is available on the SQ for at least 1 new entry.
   * If we hold the last one then threads sending to the same node will stall. */
  sq_sema = &sreq->cep->sq_sema;
  tmp = gasnetc_sema_trydown_n(sq_sema, count, GASNETC_ANY_PAR);
  if_pf (!tmp) {
    GASNETC_TRACE_WAIT_BEGIN();
    do {
      GASNETI_WAITHOOK();
      gasnetc_poll_snd();
      tmp = gasnetc_sema_trydown_n(sq_sema, count, GASNETC_ANY_PAR);
    } while (!tmp);
    GASNETC_TRACE_WAIT_END(POST_SR_STALL_SQ);
  }

  /* Loop until space is available for 1 new entry on the CQ.
   * If we hold the last one then threads sending to ANY node will stall. */
  if_pf (!gasnetc_sema_trydown(&gasnetc_cq_sema, GASNETC_ANY_PAR)) {
    GASNETC_TRACE_WAIT_BEGIN();
    do {
      GASNETI_WAITHOOK();
      gasnetc_poll_snd();
    } while (!gasnetc_sema_trydown(&gasnetc_cq_sema, GASNETC_ANY_PAR));
    GASNETC_TRACE_WAIT_END(POST_SR_STALL_CQ);
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
      next = gasnetc_get_sreq();
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

static gasnetc_rbuf_t *gasnetc_rcv_thread_rbuf = NULL;
static void gasnetc_rcv_thread(VAPI_hca_hndl_t	hca_hndl,
			       VAPI_cq_hndl_t	cq_hndl,
			       void		*context) {
#if GASNETC_VAPI_RCV_THREAD
  GASNETC_TRACE_WAIT_BEGIN();
  VAPI_ret_t vstat;

  (void)gasnetc_rcv_reap((int)(unsigned int)-1, &gasnetc_rcv_thread_rbuf);

  vstat = VAPI_req_comp_notif(gasnetc_hca, gasnetc_rcv_cq, VAPI_NEXT_COMP);

  if_pf (vstat != VAPI_OK) {
    if (GASNETC_IS_EXITING()) {
      /* disconnected by another thread */
      gasnetc_exit(0);
    } else {
      GASNETC_VAPI_CHECK(vstat, "from VAPI_req_comp_notif()");
    }
  }

  (void)gasnetc_rcv_reap((int)(unsigned int)-1, &gasnetc_rcv_thread_rbuf);
  GASNETC_TRACE_WAIT_END(RCV_THREAD_WAKE);
#else
  gasneti_fatalerror("unexpected call to gasnetc_rcv_thread");
#endif
}

GASNET_INLINE_MODIFIER(gasnetc_ReqRepGeneric)
int gasnetc_ReqRepGeneric(gasnetc_category_t category, gasnetc_rbuf_t *token,
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
   * Thus we MUST do any RDMA before getting credits.  It can't hurt to queue
   * the Long RDMA as early as possible even when firehose is not in use.
   */
  if ((category == gasnetc_Long) && nbytes) {
    if (dest == gasneti_mynode) {
      memcpy(dst_addr, src_addr, nbytes);
    } else {
      /* XXX check for error returns */
      #if GASNETC_PIN_SEGMENT
	/* Queue the RDMA.  We can count on point-to-point ordering to deliver payload before header */
        (void)gasnetc_rdma_put(dest, src_addr, dst_addr, nbytes, mem_oust, NULL);
      #else
	if (!token) {
	  /* Point-to-point ordering still holds, but only once the RDMA is actually queued.
	   * In the case of a firehose hit, the RDMA is already queued before return from
	   * gasnetc_rdma_put_fh().  On a miss, however, we'll need to spin on am_oust to
	   * determine when all the RDMA is actually queued.
	   * It would have been nice to move the wait down further in this function, but
	   * that would lead to deadlock if we hold the resources needed to queue the RDMA.
	   */
	  gasnetc_counter_t am_oust = GASNETC_COUNTER_INITIALIZER;
	  (void)gasnetc_rdma_put_fh(dest, src_addr, dst_addr, nbytes, mem_oust, NULL, &am_oust);
	  gasnetc_counter_wait(&am_oust, 0);
	} else {
	  /* No RDMA for Long Reply's when using firehose, since we can't send the AM request(s).
	   * We'll send it like a Medium below.
	   */
	}
      #endif
    }
  }

  /* NEXT, get the flow-control credit needed for AM Requests.
   * This way we can be sure that we never hold the last pinned buffer
   * while spinning on the rcv queue waiting for credits.
   */
  if (!token && (dest != gasneti_mynode)) {
    gasnetc_cep_t *cep = &gasnetc_cep[dest];
    GASNETC_STAT_EVENT(GET_AMREQ_CREDIT);

    /* Get the p2p credit needed */
    {
        gasnetc_sema_t *sema = &(cep->am_sema);

        if_pf (!gasnetc_sema_trydown(sema, GASNETC_ANY_PAR)) {
          GASNETC_TRACE_WAIT_BEGIN();
          do {
	    GASNETI_WAITHOOK();
            gasnetc_poll_rcv();
          } while (!gasnetc_sema_trydown(sema, GASNETC_ANY_PAR));
          GASNETC_TRACE_WAIT_END(GET_AMREQ_CREDIT_STALL);
        }
    }

    /* Post the rbuf needed for the reply */
    {
	gasnetc_rbuf_t	*rbuf = gasneti_freelist_get(&gasnetc_rbuf_freelist);
        if_pf (rbuf == NULL) {
          GASNETC_TRACE_WAIT_BEGIN();
          do {
	    GASNETI_WAITHOOK();
            gasnetc_poll_rcv();
	    rbuf = gasneti_freelist_get(&gasnetc_rbuf_freelist);
          } while (rbuf == NULL);
          GASNETC_TRACE_WAIT_END(GET_AMREQ_BUFFER_STALL);
        }
        gasnetc_rcv_post(cep, rbuf);
    }
  }

  /* Now get an sreq and buffer to start building the message */
  sreq = gasnetc_get_sreq();
  buf = sreq->buffer = gasnetc_get_bbuf(1);

  switch (category) {
  case gasnetc_System:
    /* currently all System AMs are shorts, they could be mediums later */
    /* fall through... */

  case gasnetc_Short:
    args = buf->shortmsg.args;
    msg_len = offsetof(gasnetc_buffer_t, shortmsg.args[numargs]);
    if (!msg_len) msg_len = 1; /* Mellanox bug (zero-length sends) work-around */
    use_inline = (GASNETC_AM_INLINE_LIMIT != 0) &&
	    	  ((sizeof(gasnetc_shortmsg_t) <= GASNETC_AM_INLINE_LIMIT) ||
	    	   (msg_len <= GASNETC_AM_INLINE_LIMIT));
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
    if (!GASNETC_PIN_SEGMENT && nbytes && (dest != gasneti_mynode) && token) {
      /* No RDMA for Long Reply's when using firehose, since we can't send the AM request(s) */
      memcpy(GASNETC_MSG_LONG_DATA(buf, numargs), src_addr, nbytes);
      msg_len = GASNETC_MSG_LONG_OFFSET(numargs) + nbytes;
      use_inline = (GASNETC_AM_INLINE_LIMIT != 0) && (msg_len <= GASNETC_AM_INLINE_LIMIT);
    } else {
      msg_len = offsetof(gasnetc_buffer_t, longmsg.args[numargs]);
      use_inline = (GASNETC_AM_INLINE_LIMIT != 0) &&
	    	    ((sizeof(gasnetc_longmsg_t) <= GASNETC_AM_INLINE_LIMIT) ||
	    	     (msg_len <= GASNETC_AM_INLINE_LIMIT));
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
  flags = GASNETC_MSG_GENFLAGS(!token, category, numargs, handler, gasneti_mynode);

  #if GASNETI_STATS_OR_TRACE
    buf->stamp = token ? ((gasnetc_buffer_t *)(uintptr_t)(token->rr_sg.addr))->stamp : GASNETI_STATTIME_NOW_IFENABLED(C);
  #endif

  if (dest == gasneti_mynode) {
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

/* Static helper function for RDMA */
#if GASNETC_PIN_SEGMENT
GASNET_INLINE_MODIFIER(gasnetc_get_lkey)
size_t gasnetc_get_lkey(uintptr_t start, size_t len, gasnetc_sreq_t *sreq, VAPI_sg_lst_entry_t *sg) {
  uintptr_t end = start + (len - 1);
  VAPI_lkey_t lkey;

  if_pt ((start >= gasnetc_seg_start) && (start <= gasnetc_seg_end)) {
    /* Starts in-segment */
    int i = (start - gasnetc_seg_start) / GASNETC_PIN_MAXSZ;
    gasneti_assert(i >= 0);
    gasneti_assert(i < gasnetc_seg_reg_count);

    lkey = gasnetc_seg_reg[i].lkey;
    if (end > gasnetc_seg_reg[i].end) {
      len = (gasnetc_seg_reg[i].end - start) + 1;
    }
  } else {
    const firehose_request_t *fh_req;

    /* Local (mis)alignment could limit how much we can pin */
    size_t limit = gasnetc_fh_maxsz - (start & (FH_BUCKET_SIZE - 1));
    if_pf (limit < len) {
      len = limit;
      end = start + (len - 1);
    }

    fh_req = firehose_local_pin(start, len, &sreq->fh_loc);
    gasneti_assert(fh_req == &(sreq->fh_loc));
    sreq->fh_ptr[0] = &(sreq->fh_loc);
    lkey = sreq->fh_loc.client.lkey;
  }

  sg->addr = start;
  sg->len  = len;
  sg->lkey = lkey;

  return len;
}

/* Relies on GASNET_ALIGNED_SEGMENTS to use a single global segment base here */
GASNET_INLINE_MODIFIER(gasnetc_get_rkey)
void gasnetc_get_rkey(gasnetc_cep_t *cep, uintptr_t start, size_t *len_p, VAPI_rkey_t *rkey) {
  size_t len = *len_p;
  uintptr_t end = start + (len - 1);
  uintptr_t tmp;
  int i;

  gasneti_assert(start >= gasnetc_seg_start);
  gasneti_assert(end <= cep->end);

  i = (start - gasnetc_seg_start) / GASNETC_PIN_MAXSZ;
  gasneti_assert(i >= 0);
  gasneti_assert(i < gasnetc_seg_reg_count);

  *rkey = cep->rkeys[i];

  /* if in last region might still run past end, but that should be caught elsewhere */
  tmp = (gasnetc_seg_start - 1) + (GASNETC_PIN_MAXSZ * (i+1));
  if (end > tmp) {
    *len_p = (tmp - start) + 1;
  }
}

/* Helper for rdma puts: inline send case */
static void gasnetc_do_put_inline(gasnetc_cep_t *cep, VAPI_rkey_t rkey,
                                  uintptr_t src, uintptr_t dst, size_t nbytes,
                                  gasnetc_counter_t *req_oust) {
  GASNETC_DECL_SR_DESC(sr_desc, 1);
  gasnetc_sreq_t *sreq;

  GASNETI_TRACE_EVENT_VAL(C, RDMA_PUT_INLINE, nbytes);

  gasneti_assert(nbytes != 0);
  gasneti_assert(nbytes <= GASNETC_PUT_INLINE_LIMIT);

  sreq = gasnetc_get_sreq();
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
    sreq = gasnetc_get_sreq();
    sreq->buffer = gasnetc_get_bbuf(1);
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

  sreq = gasnetc_get_sreq();
  sreq->buffer = gasnetc_get_bbuf(1);
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
static void gasnetc_do_put_zerocp(gasnetc_cep_t *cep, VAPI_rkey_t rkey,
                                  uintptr_t src, uintptr_t dst, size_t nbytes,
                                  gasnetc_counter_t *mem_oust, gasnetc_counter_t *req_oust) {
  GASNETC_DECL_SR_DESC(sr_desc, 1);
  gasnetc_sreq_t *sreq;

  GASNETI_TRACE_EVENT_VAL(C, RDMA_PUT_ZEROCP, nbytes);

  gasneti_assert(nbytes != 0);

  /* loop over local pinned regions */
  do {
    gasnetc_sreq_t *sreq = gasnetc_get_sreq();
    size_t count = gasnetc_get_lkey(src, nbytes, sreq, sr_desc->sg_lst_p);

    sreq->cep = cep;

    sr_desc->opcode      = VAPI_RDMA_WRITE;
    sr_desc->remote_addr = dst;
    sr_desc->r_key       = rkey;


    if (count == nbytes) {
      /* Send out the last buffer w/ the counters (if any) advanced */
      if (mem_oust) {
        gasnetc_counter_inc(mem_oust);
        sreq->mem_oust = mem_oust;
      }
      if (req_oust) {
        gasnetc_counter_inc(req_oust);
        sreq->req_oust = req_oust;
      }
    }

    gasnetc_snd_post(sreq, sr_desc);

    src += count;
    dst += count;
    nbytes -= count;
  } while (nbytes);
}

/* Helper for rdma gets: zero copy case */
static void gasnetc_do_get_zerocp(gasnetc_cep_t *cep, VAPI_rkey_t rkey,
                                  uintptr_t src, uintptr_t dst, size_t nbytes,
                                  gasnetc_counter_t *req_oust) {
  GASNETC_DECL_SR_DESC(sr_desc, 1);

  GASNETI_TRACE_EVENT_VAL(C, RDMA_GET_ZEROCP, nbytes);

  gasneti_assert(nbytes != 0);
  gasneti_assert(req_oust != NULL);

  /* loop over local pinned regions */
  do {
    gasnetc_sreq_t *sreq = gasnetc_get_sreq();
    size_t count = gasnetc_get_lkey(dst, nbytes, sreq, sr_desc->sg_lst_p);

    sreq->cep = cep;

    sr_desc->opcode      = VAPI_RDMA_READ;
    sr_desc->remote_addr = src;
    sr_desc->r_key       = rkey;

    sreq->req_oust = req_oust;
    gasnetc_counter_inc(req_oust);

    gasnetc_snd_post(sreq, sr_desc);

    src += count;
    dst += count;
    nbytes -= count;
  } while (nbytes);
}
#else
static void gasnetc_fh_put_inline(void *context, const firehose_request_t *req, int allLocalHit) {
  GASNETC_DECL_SR_DESC(sr_desc, 1);
  gasnetc_sreq_t *sreq = context;

  GASNETI_TRACE_EVENT_VAL(C, RDMA_PUT_INLINE, sreq->fh_len);

  gasneti_assert(req == &(sreq->fh_rem));

  sreq->fh_ptr[0] = &(sreq->fh_rem);

  sr_desc->opcode = VAPI_RDMA_WRITE;
  sr_desc->remote_addr = sreq->rem_addr;
  sr_desc->r_key = sreq->fh_rem.client.rkey;
  sr_desc->sg_lst_p[0].addr = sreq->loc_addr;
  sr_desc->sg_lst_p[0].len = sreq->fh_len;

  gasnetc_snd_post_inline(sreq, sr_desc);

  if (sreq->am_oust) {
    gasnetc_counter_dec(sreq->am_oust);
  }
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

  if (sr_desc->opcode == VAPI_RDMA_WRITE) {
    GASNETI_TRACE_EVENT_VAL(C, RDMA_PUT_ZEROCP, sreq->fh_len);
  } else {
    GASNETI_TRACE_EVENT_VAL(C, RDMA_GET_ZEROCP, sreq->fh_len);
  }

  gasnetc_snd_post(sreq, sr_desc);

  if (sreq->am_oust) {
    gasnetc_counter_dec(sreq->am_oust);
  }
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
        /* XXX: should we try to initiate RDMA here rather than releasing?
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
  count = gasnetc_am_oust_pp * (gasneti_nodes - 1)	/* max inbound requests */
	  + gasnetc_am_oust_limit			/* max inbound replies */
	  + (gasnetc_use_rcv_thread ? 1 : 0);		/* dedicated spare */

  /* create the RCV CQ */
  vstat = VAPI_create_cq(gasnetc_hca, count , &gasnetc_rcv_cq, &act_size);
  GASNETC_VAPI_CHECK(vstat, "from VAPI_create_cq(rcv_cq)");
  gasneti_assert(act_size >= count);

  if (gasneti_nodes > 1) {
    if (gasnetc_use_rcv_thread) {
      /* create the RCV thread */
      vstat = EVAPI_set_comp_eventh(gasnetc_hca, gasnetc_rcv_cq, &gasnetc_rcv_thread,
				    NULL, &gasnetc_rcv_handler);
      GASNETC_VAPI_CHECK(vstat, "from EVAPI_set_comp_eventh()");
      vstat = VAPI_req_comp_notif(gasnetc_hca, gasnetc_rcv_cq, VAPI_NEXT_COMP);
      GASNETC_VAPI_CHECK(vstat, "from VAPI_req_comp_notif()");
    }

    /* Allocated pinned memory for receive buffers */
    buf = gasnetc_alloc_pinned(count * sizeof(gasnetc_buffer_t),
			       VAPI_EN_LOCAL_WRITE, &gasnetc_rcv_reg);
    gasneti_assert(buf != NULL);

    /* Allocated normal memory for receive descriptors (rbuf's) */
    padded_size = GASNETC_ALIGNUP(sizeof(gasnetc_rbuf_t), GASNETI_CACHE_LINE_BYTES);
    gasnetc_rbuf_alloc = gasneti_malloc(count*padded_size + GASNETI_CACHE_LINE_BYTES-1);

    /* Initialize the rbuf's */
    rbuf = (gasnetc_rbuf_t *)GASNETC_ALIGNUP(gasnetc_rbuf_alloc, GASNETI_CACHE_LINE_BYTES);
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
    if (gasnetc_use_rcv_thread) {
      gasnetc_rcv_thread_rbuf = gasneti_freelist_get(&gasnetc_rbuf_freelist);
      gasneti_assert(gasnetc_rcv_thread_rbuf != NULL);
    }
  }

  /*
   * setup SND resources
   */
  count = MIN(gasnetc_op_oust_limit, gasnetc_op_oust_pp * (gasneti_nodes - 1));
  gasnetc_op_oust_limit = count;
  gasnetc_sema_init(&gasnetc_cq_sema, count);

  /* create the SND CQ */
  vstat = VAPI_create_cq(gasnetc_hca, count, &gasnetc_snd_cq, &act_size);
  GASNETC_VAPI_CHECK(vstat, "from VAPI_create_cq(snd_cq)");
  gasneti_assert(act_size >= count);

  /* Allocated pinned memory for bounce buffers */
  count = MIN(gasnetc_bbuf_limit, gasnetc_op_oust_pp * gasneti_nodes);
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
			        GASNETI_CACHE_LINE_BYTES);
  gasnetc_sreq_alloc = gasneti_malloc(count*padded_size + GASNETI_CACHE_LINE_BYTES-1);
  sreq = (gasnetc_sreq_t *)GASNETC_ALIGNUP(gasnetc_sreq_alloc, GASNETI_CACHE_LINE_BYTES);
  for (i = 0; i < count; ++i) {
    gasneti_freelist_put(&gasnetc_sreq_freelist, sreq);
    sreq = (gasnetc_sreq_t *)((uintptr_t)sreq + padded_size);
  }
}

extern void gasnetc_sndrcv_init_cep(gasnetc_cep_t *cep) {
  int i;
  
  if (cep != &gasnetc_cep[gasneti_mynode]) {
    /* Prepost one for each possible incomming request */
    for (i = 0; i < gasnetc_am_oust_pp; ++i) {
      gasnetc_rcv_post(cep, gasneti_freelist_get(&gasnetc_rbuf_freelist));
    }

    gasnetc_sema_init(&cep->am_sema, gasnetc_am_oust_pp);
    gasnetc_sema_init(&cep->sq_sema, gasnetc_op_oust_pp);
  } else {
    /* Should never use these for loopback */
    gasnetc_sema_init(&cep->am_sema, 0);
    gasnetc_sema_init(&cep->sq_sema, 0);
  }
}

extern void gasnetc_sndrcv_fini(void) {
  VAPI_ret_t vstat;

  if (gasneti_nodes > 1) {
    if (gasnetc_use_rcv_thread) {
      vstat = EVAPI_clear_comp_eventh(gasnetc_hca, gasnetc_rcv_handler);
      GASNETC_VAPI_CHECK(vstat, "from EVAPI_clear_comp_eventh()");
    }

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

  if (cep != &gasnetc_cep[gasneti_mynode]) {
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
 * Uses inline if possible, bounce buffers if "small enough" and the caller is planning to wait
 * for local completion.  Otherwise zero-copy is used (with firehose if the source is not pre-pinned).
 */
extern int gasnetc_rdma_put(int node, void *src_ptr, void *dst_ptr, size_t nbytes, gasnetc_counter_t *mem_oust, gasnetc_counter_t *req_oust) {
  gasnetc_cep_t *cep = &gasnetc_cep[node];
  uintptr_t src = (uintptr_t)src_ptr;
  uintptr_t dst = (uintptr_t)dst_ptr;

  gasneti_assert(nbytes != 0);
  
  do {
    /* Loop over contiguous pinned regions on remote end */
    size_t count = nbytes;
    VAPI_rkey_t rkey;
    gasnetc_get_rkey(cep, dst, &count, &rkey);

    if ((GASNETC_PUT_INLINE_LIMIT != 0) && (count <= GASNETC_PUT_INLINE_LIMIT)) {
      /* Use a short-cut for sends that are short enough.
       *
       * Note that we do this based only on the size of the request, without bothering to check whether
       * the caller cares about local completion, or whether zero-copy is possible.
       * We do this is because the cost of this small copy appears cheaper then the alternative logic.
       */
      gasnetc_do_put_inline(cep, rkey, src, dst, count, req_oust);
    } else if ((count <= GASNETC_PUT_COPY_LIMIT) && (mem_oust != NULL)) {
      /* Because VAPI lacks any indication of "local" completion, the only ways to
       * implement non-bulk puts (mem_oust != NULL) are as fully blocking puts, or
       * with bounce buffers.  So, if a non-bulk put is "not too large" use bounce
       * buffers.
       */
      gasnetc_do_put_bounce(cep, rkey, src, dst, count, req_oust);
    } else {
      /* Here is the general case */
      gasnetc_do_put_zerocp(cep, rkey, src, dst, count, mem_oust, req_oust);
    }

    src += count;
    dst += count;
    nbytes -= count;
  } while (nbytes);

  return 0;
}

/* Perform an RDMA get
 *
 * Uses bounce buffers when the destination is not pinned, zero-copy otherwise.
 */
extern int gasnetc_rdma_get(int node, void *src_ptr, void *dst_ptr, size_t nbytes, gasnetc_counter_t *req_oust) {
  gasnetc_cep_t *cep = &gasnetc_cep[node];
  uintptr_t src = (uintptr_t)src_ptr;
  uintptr_t dst = (uintptr_t)dst_ptr;

  gasneti_assert(nbytes != 0);
  gasneti_assert(req_oust != NULL);

  do {
    /* Loop over contiguous pinned regions on remote end */
    size_t count = nbytes;
    VAPI_rkey_t rkey;
    VAPI_lkey_t lkey;

    gasnetc_get_rkey(cep, src, &count, &rkey);
    gasnetc_do_get_zerocp(cep, rkey, src, dst, count, req_oust);

    src += count;
    dst += count;
    nbytes -= count;
  } while (nbytes);

  return 0;
}
#else
/*
 * ###########################################
 * RDMA ops when the segment is NOT pre-pinned
 * ###########################################
 */
/* RDMA put */
extern int gasnetc_rdma_put_fh(int node, void *src_ptr, void *dst_ptr, size_t nbytes, gasnetc_counter_t *mem_oust, gasnetc_counter_t *req_oust, gasnetc_counter_t *am_oust) {
  gasnetc_cep_t *cep = &gasnetc_cep[node];
  uintptr_t src = (uintptr_t)src_ptr;
  uintptr_t dst = (uintptr_t)dst_ptr;

  gasneti_assert(nbytes != 0);

  do {
    gasnetc_sreq_t *sreq = gasnetc_get_sreq();
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
    if (am_oust) {
      gasnetc_counter_inc(am_oust);
      sreq->am_oust = am_oust;
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

  gasneti_assert(nbytes != 0);
  gasneti_assert(req_oust != NULL);

  do {
    gasnetc_sreq_t *sreq = gasnetc_get_sreq();
    size_t count;

    sreq->cep    = cep;
    sreq->opcode = VAPI_RDMA_READ;
 
    /* We must set counters on all chunks since order of completion is uncertain */
    sreq->req_oust = req_oust;
    gasnetc_counter_inc(req_oust);

    count = gasnetc_fh_helper(0, node, sreq, dst, src, nbytes);

    src += count;
    dst += count;
    nbytes -= count;
  } while (nbytes);

  return 0;
}
#endif

extern int gasnetc_RequestGeneric(gasnetc_category_t category,
				  int dest, gasnet_handler_t handler,
				  void *src_addr, int nbytes, void *dst_addr,
				  int numargs, gasnetc_counter_t *mem_oust, va_list argptr) {
  gasnetc_poll_rcv();	/* ensure progress */

  return gasnetc_ReqRepGeneric(category, NULL, dest, handler,
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

  retval = gasnetc_ReqRepGeneric(category, rbuf, GASNETC_MSG_SRCIDX(rbuf->rbuf_flags), handler,
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
  retval = gasnetc_ReqRepGeneric(gasnetc_System, NULL, dest, handler,
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
  retval = gasnetc_ReqRepGeneric(gasnetc_System, rbuf, dest, handler,
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

  gasneti_assert(sourceid < gasneti_nodes);
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

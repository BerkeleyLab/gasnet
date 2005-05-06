/*   $Source: /Users/kamil/work/gasnet-cvs2/gasnet/vapi-conduit/Attic/gasnet_core_sndrcv.c,v $
 *     $Date: 2005/05/06 01:22:28 $
 * $Revision: 1.100.4.1 $
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

#if GASNETC_PIN_SEGMENT
  /* Max firehose per op is one per local scatter/gather segment */
  #define GASNETC_MAX_FH	GASNETC_SND_SG
#else
  /* Max firehose per op is one per local scatter/gather segment + one remote */
  #define GASNETC_MAX_FH	(GASNETC_SND_SG + 1)
#endif

/* ------------------------------------------------------------------------------------ *
 *  Global variables                                                                    *
 * ------------------------------------------------------------------------------------ */
gasnetc_memreg_t                        gasnetc_rcv_reg;
gasnetc_memreg_t			gasnetc_snd_reg;
VAPI_cq_hndl_t                          gasnetc_rcv_cq;
VAPI_cq_hndl_t				gasnetc_snd_cq;
size_t					gasnetc_fh_maxsz;
size_t                   		gasnetc_inline_limit;
size_t                   		gasnetc_bounce_limit;
int					gasnetc_use_rcv_thread = GASNETC_VAPI_RCV_THREAD;
int					gasnetc_use_firehose = 1;

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
  /* Firehose, bounce buffers, and AMs are mutually exclusive.
   * + AMs are distingished by an opcode of SEND_WITH_IMM.
   * + fh_count == 0: in-segment gets/puts (use none of the union)
   * + fh_count > 0: out-of-segment gets/puts using firehose
   * + fh_count < 0: out-of-segment gets/puts using bounce buffers
   */
  int				fh_count;
  union {
    struct { /* Firehose data */
      const firehose_request_t	*fh_ptr[GASNETC_MAX_FH];
    } fh;
    struct { /* Bounce buffer data */
      gasnetc_buffer_t		*bb_buff;
      void			*bb_addr;	/* local address for bounced GETs */
      size_t			bb_len;		/* length for bounced GETs */
    } bb;
    struct { /* AM buffer */
      gasnetc_buffer_t		*am_buff;
    } am;
  } u;
  #define fh_ptr	u.fh.fh_ptr
  #define bb_buff	u.bb.bb_buff
  #define bb_addr	u.bb.bb_addr
  #define bb_len	u.bb.bb_len
  #define am_buff	u.am.am_buff
#else
  /* Firehose, and AMs are mutually exclusive.
   * + AMs are distingished by an opcode of SEND_WITH_IMM.
   */
  union {
    /* Firehose data */
    struct {
      int			fh_count;
      const firehose_request_t	*fh_ptr[GASNETC_MAX_FH];
      size_t			fh_len;
      uintptr_t			fh_loc_addr;
      uintptr_t			fh_rem_addr;
      gasnetc_buffer_t		*fh_bbuf;
      gasneti_atomic_t		fh_ready;	/* 0 when loc and rem both ready */
      gasnetc_counter_t		*fh_oust;	/* fh transactions outstanding */
    } fh;
    struct { /* AM buffer */
      gasnetc_buffer_t		*am_buff;
    } am;
  } u;
  #define fh_count	u.fh.fh_count
  #define fh_ptr	u.fh.fh_ptr
  #define fh_len	u.fh.fh_len
  #define fh_loc_addr	u.fh.fh_loc_addr
  #define fh_rem_addr	u.fh.fh_rem_addr
  #define fh_bbuf	u.fh.fh_bbuf
  #define fh_ready	u.fh.fh_ready
  #define fh_oust	u.fh.fh_oust
  #define am_buff	u.am.am_buff
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

GASNET_INLINE_MODIFIER(gasnetc_sr_desc_init)
VAPI_sr_desc_t *gasnetc_sr_desc_init(void *base, int sg_lst_len, int count)
{
  VAPI_sr_desc_t *result = (VAPI_sr_desc_t *)GASNETC_ALIGNUP(base, GASNETI_CACHE_LINE_BYTES);
  VAPI_sg_lst_entry_t *sg_lst_p = (VAPI_sg_lst_entry_t *)((uintptr_t)result + count*sizeof(VAPI_sr_desc_t));
  int i;

  for (i=0; i<count; ++i, sg_lst_p += sg_lst_len) {
        #if GASNET_DEBUG
	  result[i].sg_lst_len = 0; /* invalid to ensure caller sets it */
	#endif
	result[i].sg_lst_p = sg_lst_p;
  }
  
  return result;
}
#define GASNETC_DECL_SR_DESC(_name, _sg_lst_len, _count) \
	char _CONCAT(_name,_align)[_count*(sizeof(VAPI_sr_desc_t)+_sg_lst_len*sizeof(VAPI_sg_lst_entry_t))+GASNETI_CACHE_LINE_BYTES];\
	VAPI_sr_desc_t *_name = gasnetc_sr_desc_init(_CONCAT(_name,_align), _sg_lst_len, _count) /* note intentional lack of final semicolon */

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
  gasneti_assert((cep - gasnetc_cep)/GASNETC_CEPS != gasneti_mynode);
  
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
        GASNETI_RUN_HANDLER_SHORT(GASNETC_MSG_ISREQUEST(flags),handler_id,handler_fn,rbuf,args,numargs);
      }
      break;

    case gasnetc_Medium:
      {
        nbytes = buf->medmsg.nBytes;
        data = GASNETC_MSG_MED_DATA(buf, numargs);
	args = buf->medmsg.args;
        GASNETI_RUN_HANDLER_MEDIUM(GASNETC_MSG_ISREQUEST(flags),handler_id,handler_fn,rbuf,args,numargs,data,nbytes);
      }
      break;

    case gasnetc_Long:
      { 
        nbytes = buf->longmsg.nBytes;
        data = (void *)(buf->longmsg.destLoc);
	args = buf->longmsg.args;
        if (!GASNETC_MSG_ISREQUEST(flags)) {
	  #if !GASNETC_PIN_SEGMENT
	    if (GASNETC_MSG_SRCIDX(flags) != gasneti_mynode) {
	      /* No RDMA for ReplyLong.  So, must relocate the payload. */
	      memcpy(data, GASNETC_MSG_LONG_DATA(buf, numargs), nbytes);
	    }
	  #endif
	}
        GASNETI_RUN_HANDLER_LONG(GASNETC_MSG_ISREQUEST(flags),handler_id,handler_fn,rbuf,args,numargs,data,nbytes);
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
	    gasneti_assert((sreq->req_oust != NULL) || !GASNETC_ANY_PAR);
	    gasneti_assert(sreq->mem_oust == NULL);
            #if GASNETC_PIN_SEGMENT
	    if_pf (sreq->fh_count < 0) {
	      /* complete bounced RMDA read */
	      gasneti_assert(!gasnetc_use_firehose); /* Only possible when firehose disabled */
	      gasneti_assert(sreq->bb_buff != NULL);
	      gasneti_assert(sreq->bb_addr != NULL);
	      gasneti_assert(sreq->bb_len > 0);
	      memcpy(sreq->bb_addr, sreq->bb_buff, sreq->bb_len);
              gasneti_sync_writes();
	      if (GASNETC_ANY_PAR || sreq->req_oust) {
                gasnetc_counter_dec(sreq->req_oust);
	      }
	      gasneti_freelist_put(&gasnetc_bbuf_freelist, sreq->bb_buff);
	    } else
	    #endif
	    {
              gasnetc_counter_dec(sreq->req_oust);
	      if (sreq->fh_count) {
		gasneti_assert(sreq->fh_count > 0);
		gasneti_assert(sreq->fh_count <= GASNETC_MAX_FH);
	        firehose_release(sreq->fh_ptr, sreq->fh_count);
	      }
	    }
	    break;

	  case VAPI_CQE_SQ_RDMA_WRITE:	/* Put */
            if (sreq->mem_oust) {
	      gasneti_assert(sreq->fh_count >= 0);
	      gasnetc_counter_dec(sreq->mem_oust);
	    }
            if (sreq->req_oust) {
              gasnetc_counter_dec(sreq->req_oust);
	    }
            #if !GASNETC_PIN_SEGMENT
	    if_pf (sreq->fh_bbuf != NULL) {
	      /* Bounce buffer PUT */
	      gasneti_freelist_put(&gasnetc_bbuf_freelist, sreq->fh_bbuf);
	    }
	    #else
	    if_pf (sreq->fh_count < 0) {
	      /* Bounce buffer PUT */
	      gasneti_assert(sreq->bb_buff != NULL);
	      gasneti_freelist_put(&gasnetc_bbuf_freelist, sreq->bb_buff);
	    } else
	    #endif
	    if (sreq->fh_count > 0) {
	      gasneti_assert(sreq->fh_count <= GASNETC_MAX_FH);
	      firehose_release(sreq->fh_ptr, sreq->fh_count);
	    }
	    break;

	  case VAPI_CQE_SQ_SEND_DATA:	/* AM send */
	    gasneti_assert(sreq->mem_oust == NULL);
            if (sreq->req_oust) {
              gasnetc_counter_dec(sreq->req_oust);
	    }
	    if_pf (sreq->am_buff != NULL) {
	      gasneti_freelist_put(&gasnetc_bbuf_freelist, sreq->am_buff);
	    }
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
        gasnetc_sreq_t *sreq = (gasnetc_sreq_t *)(uintptr_t)comp.id;
        fprintf(stderr, "@ %d> snd comp.status=%d comp.opcode=%d dst_node=%d dst_qp=%d\n", gasneti_mynode, comp.status, comp.opcode, (int)(sreq->cep - gasnetc_cep)/GASNETC_CEPS, (int)(sreq->cep - gasnetc_cep)%GASNETC_CEPS);
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
  gasnetc_cep_t *cep = GASNETC_CEP(GASNETC_MSG_SRCIDX(flags), 0);
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
    sreq->fh_count = GASNETC_MAX_FH + 1;
    #if !GASNETC_PIN_SEGMENT
    sreq->fh_len = ~0;
    #endif
  #endif

  /* Assume no counters */
  sreq->mem_oust = NULL;
  sreq->req_oust = NULL;
  #if !GASNETC_PIN_SEGMENT
    sreq->fh_oust = NULL;
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
  int i, j;

  gasneti_assert(sreq);
  gasneti_assert(sreq->cep);
  gasneti_assert((sreq->cep - gasnetc_cep)/GASNETC_CEPS != gasneti_mynode); /* detects loopback */
  gasneti_assert(sr_desc);
  gasneti_assert(sr_desc->sg_lst_len >= 1);
  gasneti_assert(sr_desc->sg_lst_len <= GASNETC_SND_SG);
  gasneti_assert(count > 0);
  gasneti_assert(type);

  GASNETI_TRACE_PRINTF(D,("%s sreq=%p\n", type, sreq));
  for (i = 0; i < count; ++i, ++sr_desc) {
    uintptr_t r_addr = sr_desc->remote_addr;

    switch (sr_desc->opcode) {
    case VAPI_SEND_WITH_IMM:
      GASNETI_TRACE_PRINTF(D,("%s op=SND rkey=0x%08x\n", type, (unsigned int)sr_desc->r_key));
      for (j = 0; j < sr_desc->sg_lst_len; ++j) {
        uintptr_t l_addr = sr_desc->sg_lst_p[j].addr;
        size_t    len    = sr_desc->sg_lst_p[j].len;
	unsigned  lkey   = sr_desc->sg_lst_p[j].lkey;
        GASNETI_TRACE_PRINTF(D,("  %i: lkey=0x%08x len=%lu local=[%p-%p] remote=N/A\n",
			        j, lkey, (unsigned long)len,
			        (void *)l_addr, (void *)(l_addr + (len - 1))));
      }
      break;

    case VAPI_RDMA_WRITE:
      GASNETI_TRACE_PRINTF(D,("%s op=PUT rkey=0x%08x\n", type, (unsigned int)sr_desc->r_key));
      for (j = 0; j < sr_desc->sg_lst_len; ++j) {
        uintptr_t l_addr = sr_desc->sg_lst_p[j].addr;
        size_t    len    = sr_desc->sg_lst_p[j].len;
	unsigned  lkey   = sr_desc->sg_lst_p[j].lkey;
        GASNETI_TRACE_PRINTF(D,("  %i: lkey=0x%08x len=%lu local=[%p-%p] remote=[%p-%p]\n",
			        j, lkey, (unsigned long)len,
				(void *)l_addr, (void *)(l_addr + (len - 1)),
				(void *)r_addr, (void *)(r_addr + (len - 1))));
	r_addr += len;
      }
      break;

    case VAPI_RDMA_READ:
      GASNETI_TRACE_PRINTF(D,("%s op=GET rkey=0x%08x\n", type, (unsigned int)sr_desc->r_key));
      for (j = 0; j < sr_desc->sg_lst_len; ++j) {
        uintptr_t l_addr = sr_desc->sg_lst_p[j].addr;
        size_t    len    = sr_desc->sg_lst_p[j].len;
	unsigned  lkey   = sr_desc->sg_lst_p[j].lkey;
        GASNETI_TRACE_PRINTF(D,("  %i: lkey=0x%08x len=%lu local=[%p-%p] remote=[%p-%p]\n",
			        j, lkey, (unsigned long)len,
				(void *)l_addr, (void *)(l_addr + (len - 1)),
				(void *)r_addr, (void *)(r_addr + (len - 1))));
	r_addr += len;
      }
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

/* Post a work request to the send queue of the given endpoint */
GASNET_INLINE_MODIFIER(gasnetc_snd_post)
void gasnetc_snd_post(gasnetc_sreq_t *sreq, VAPI_sr_desc_t *sr_desc) {
  VAPI_ret_t vstat;

  gasnetc_snd_post_common(sreq, sr_desc);
  gasneti_assert(sreq->count == 1);

  GASNETC_STAT_EVENT_VAL(POST_SR, sr_desc->sg_lst_len);
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

#if 0 && GASNET_PIN_SEGMENT
/* XXX: Broken now that FAST uses firehose, too.
 * In particular we don't do anything with firehose resources if we needed to
 * split the request across multiple sreqs.  This is because there is no way to
 * correlate the firehose_request_t's with the sr_desc's at the current time.
 */
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

/* Post multiple work requests to the send queue of the given endpoint */
GASNET_INLINE_MODIFIER(gasnetc_snd_post_list)
void gasnetc_snd_post_list(gasnetc_sreq_t *sreq, int count, VAPI_sr_desc_t *sr_desc) {

  gasneti_assert(sr_desc->opcode != VAPI_SEND_WITH_IMM); /* Can't (yet?) handle SENDs (AMs) */
  gasneti_assert(gasnetc_use_firehose || (sreq->bb_buff == NULL)); /* Can't (yet?) handle BB GET/PUT */

  GASNETC_STAT_EVENT_VAL(SND_POST_LIST,count);

  do {
    gasnetc_sreq_t *next = NULL;
    VAPI_ret_t vstat;

    gasnetc_snd_post_list_common(sreq, sr_desc, count);
    gasneti_assert(sreq->count >= 1);
    gasneti_assert(sreq->count <= count);

    if_pf (sreq->count < count) {
      /* If there is not enough SQ space, so we split the request list */
      /* XXX: this is where we are most broken w.r.t. firehose resources */
      next = gasnetc_get_sreq();
      next->cep = sreq->cep;
      next->mem_oust = sreq->mem_oust;  sreq->mem_oust = NULL;
      next->req_oust = sreq->req_oust;  sreq->req_oust = NULL;
    }

    GASNETC_STAT_EVENT_VAL(POST_SR_LIST,sreq->count);
    #if GASNET_TRACE || GASNET_DEBUG
      gasnetc_snd_validate(sreq, sr_desc, sreq->count, "POST_SR_LIST");
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
  union {         
    gasnetc_shortmsg_t    shortmsg;
    gasnetc_medmsg_t      medmsg;
    gasnetc_longmsg_t     longmsg;
    uint8_t		  raw[72];	/* could be gasnetc_inline_limit if we had VLA */
  } tmp_buf;


  /* FIRST, figure out msg_len so we know if we can use inline or not.
   * Also, if using firehose then Long requests may need AMs for moves.
   * Thus we MUST do any RDMA before getting credits.  It can't hurt to queue
   * the Long RDMA as early as possible even when firehose is not in use.
   */
  switch (category) {
  case gasnetc_System: /* Currently System == Short.  Fall through... */
  case gasnetc_Short:
    msg_len = offsetof(gasnetc_buffer_t, shortmsg.args[numargs]);
    if (!msg_len) msg_len = 1; /* Mellanox bug (zero-length sends) work-around */
    break;

  case gasnetc_Medium:
    msg_len = GASNETC_MSG_MED_OFFSET(numargs) + nbytes;
    break;

  case gasnetc_Long:
    msg_len = offsetof(gasnetc_buffer_t, longmsg.args[numargs]);
    /* Start moving the Long payload if possible */
    if (nbytes) {
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
	    msg_len = GASNETC_MSG_LONG_OFFSET(numargs) + nbytes;
	  }
        #endif
      }
    }
    break;

  default:
    gasneti_fatalerror("invalid AM category on send");
    /* NOT REACHED */
  }

  /* NEXT, get the flow-control credit needed for AM Requests.
   * This way we can be sure that we never hold the last pinned buffer
   * while spinning on the rcv queue waiting for credits.
   */
  if (!token && (dest != gasneti_mynode)) {
    gasnetc_cep_t *cep = GASNETC_CEP(dest, 0);
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

  /* Now get an sreq and buffer and start building the message */
  sreq = gasnetc_get_sreq();
  if_pt ((msg_len <= gasnetc_inline_limit) && (msg_len <= sizeof(tmp_buf))) {
    buf = (gasnetc_buffer_t *)&tmp_buf;
    sreq->am_buff = NULL;
  } else {
    buf = gasnetc_get_bbuf(1);
    sreq->am_buff = buf;
  }

  switch (category) {
  case gasnetc_System: /* Currently System == Short.  Fall through... */
  case gasnetc_Short:
    args = buf->shortmsg.args;
    break;

  case gasnetc_Medium:
    args = buf->medmsg.args;
    buf->medmsg.nBytes = nbytes;
    memcpy(GASNETC_MSG_MED_DATA(buf, numargs), src_addr, nbytes);
    break;

  case gasnetc_Long:
    args = buf->longmsg.args;
    buf->longmsg.destLoc = (uintptr_t)dst_addr;
    buf->longmsg.nBytes  = nbytes;
    if (!GASNETC_PIN_SEGMENT && nbytes && (dest != gasneti_mynode) && token) {
      /* No RDMA for Long Reply's when using firehose, since we can't send the AM request(s) */
      memcpy(GASNETC_MSG_LONG_DATA(buf, numargs), src_addr, nbytes);
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
    if_pf (sreq->am_buff != NULL) {
      gasneti_freelist_put(&gasnetc_bbuf_freelist, buf);
    }
    gasneti_freelist_put(&gasnetc_sreq_freelist, sreq);
    retval = GASNET_OK;
  } else {
    /* send the AM */
    GASNETC_DECL_SR_DESC(sr_desc, 1, 1);
    sr_desc->opcode     = VAPI_SEND_WITH_IMM;
    sr_desc->imm_data   = flags;
    sr_desc->sg_lst_len = 1;
    sr_desc->sg_lst_p[0].addr      = (uintptr_t)buf;
    sr_desc->sg_lst_p[0].len       = msg_len;
    sr_desc->sg_lst_p[0].lkey      = gasnetc_snd_reg.lkey;

    sreq->cep = GASNETC_CEP(dest, 0);
    if_pf (req_oust) {
      gasnetc_counter_inc(req_oust);
      sreq->req_oust = req_oust;
    }

    if_pt (sreq->am_buff == NULL) {
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
/* Test if a given (addr, len) is in the GASNet segment or not.
 * Returns non-zero if starting address is outside the segment
 * and adjusts len to describe a region that is fully out of segment.
 * Len is unchanged if start is in the segment.
 */
GASNET_INLINE_MODIFIER(gasnetc_unpinned)
int gasnetc_unpinned(uintptr_t start, size_t *len_p) {
  size_t len = *len_p;
  uintptr_t end = start + (len - 1);

  if_pt ((start >= gasnetc_seg_start) && (end <= gasnetc_seg_end)) {
    /* FULLY IN */
    return 0;
  }

  if_pt ((start > gasnetc_seg_end) || (end < gasnetc_seg_start)) {
    /* FULLY OUT */
    return 1;
  }

  /* Partials: */
  if (start < gasnetc_seg_start) {
    /* Starts OUT, ends IN */
    *len_p = gasnetc_seg_start - start;
    return 1;
  } else {
    gasneti_assert(end > gasnetc_seg_end);
    return 0;
  }
}

GASNET_INLINE_MODIFIER(gasnetc_get_lkey)
size_t gasnetc_get_lkey(uintptr_t start, size_t len, gasnetc_sreq_t *sreq, VAPI_sg_lst_entry_t *sg) {
  uintptr_t end = start + (len - 1);
  VAPI_lkey_t lkey;

  if_pt ((start >= gasnetc_seg_start) && (start <= gasnetc_seg_end)) {
    /* Starts in-segment */
    int i = (start - gasnetc_seg_start) >> gasnetc_pin_maxsz_shift;
    gasneti_assert(i >= 0);
    gasneti_assert(i < gasnetc_seg_reg_count);

    lkey = gasnetc_seg_reg[i].lkey;
    if (end > gasnetc_seg_reg[i].end) {
      len = (gasnetc_seg_reg[i].end - start) + 1;
    }
  } else {
    const firehose_request_t *fh_loc;
    int count = sreq->fh_count;

    if (count == 0) {
      /* Local (mis)alignment could limit how much we can pin */
      len = MIN(len, (gasnetc_fh_maxsz - (start & (FH_BUCKET_SIZE - 1))));
      fh_loc = firehose_local_pin(start, len, NULL);
    } else {
      /* We hold a local firehose already, we can only 'try' or risk deadlock */
      fh_loc = firehose_try_local_pin(start, 1, NULL);
      if_pf (!fh_loc) {
	/* Firehose miss.  Caller queue what has been built up so far. */
	return 0;
      }
      len = MIN(len, (fh_loc->addr + fh_loc->len - start));
    }
    lkey = fh_loc->client.lkey;
    sreq->fh_ptr[count] = fh_loc;
    sreq->fh_count = count + 1;
  }

  sg->addr = start;
  sg->len  = len;
  sg->lkey = lkey;

  return len;
}

/* Relies on GASNET_ALIGNED_SEGMENTS to use a single global segment base here */
GASNET_INLINE_MODIFIER(gasnetc_get_rkey)
void gasnetc_get_rkey(gasnetc_peer_t *peer, uintptr_t start, size_t *len_p, VAPI_rkey_t *rkey) {
  size_t len = *len_p;
  uintptr_t end = start + (len - 1);
  uintptr_t tmp;
  int i;

  gasneti_assert(start >= gasnetc_seg_start);
  gasneti_assert(end <= peer->end);

  i = (start - gasnetc_seg_start) >> gasnetc_pin_maxsz_shift;
  gasneti_assert(i >= 0);
  gasneti_assert(i < gasnetc_seg_reg_count);

  *rkey = peer->rkeys[i];

  /* if in last region might still run past end, but that should be caught elsewhere */
  tmp = (gasnetc_seg_start - 1) + ((i+1) << gasnetc_pin_maxsz_shift);
  if (end > tmp) {
    *len_p = (tmp - start) + 1;
  }
  gasneti_assert(((start + (*len_p-1) - gasnetc_seg_start) >> gasnetc_pin_maxsz_shift) == i);
}

/* Helper for rdma puts: inline send case */
static void gasnetc_do_put_inline(gasnetc_cep_t *cep, VAPI_rkey_t rkey,
                                  uintptr_t src, uintptr_t dst, size_t nbytes,
                                  gasnetc_counter_t *req_oust) {
  GASNETC_DECL_SR_DESC(sr_desc, 1, 1);
  gasnetc_sreq_t *sreq;

  GASNETI_TRACE_EVENT_VAL(C, RDMA_PUT_INLINE, nbytes);

  gasneti_assert(nbytes != 0);
  gasneti_assert(nbytes <= gasnetc_inline_limit);

  sreq = gasnetc_get_sreq();
  sreq->cep = cep;
  sreq->fh_count = 0;
  if (req_oust) {
    gasnetc_counter_inc(req_oust);
    sreq->req_oust = req_oust;
  }

  sr_desc->opcode      = VAPI_RDMA_WRITE;
  sr_desc->remote_addr = dst;
  sr_desc->r_key       = rkey;
  sr_desc->sg_lst_len  = 1;
  sr_desc->sg_lst_p[0].addr       = src;
  sr_desc->sg_lst_p[0].len        = nbytes;

  gasnetc_snd_post_inline(sreq, sr_desc);
}
      
/* Helper for rdma puts: bounce buffer case */
static void gasnetc_do_put_bounce(gasnetc_cep_t *cep, VAPI_rkey_t rkey,
                                  uintptr_t src, uintptr_t dst, size_t nbytes,
                                  gasnetc_counter_t *req_oust) {
  GASNETC_DECL_SR_DESC(sr_desc, 1, 1);
  gasnetc_sreq_t *sreq;

  GASNETI_TRACE_EVENT_VAL(C, RDMA_PUT_BOUNCE, nbytes);

  gasneti_assert(nbytes != 0);

  /* Use full bounce buffers until just one buffer worth of data remains */
  while (nbytes > GASNETC_BUFSZ) {
    sreq = gasnetc_get_sreq();
    sreq->bb_buff = gasnetc_get_bbuf(1);
    memcpy(sreq->bb_buff, (void *)src, GASNETC_BUFSZ);
    sreq->cep  = cep;
    sreq->fh_count = -1;

    sr_desc->opcode      = VAPI_RDMA_WRITE;
    sr_desc->remote_addr = dst;
    sr_desc->r_key       = rkey;
    sr_desc->sg_lst_len  = 1;
    sr_desc->sg_lst_p[0].addr = (uintptr_t)sreq->bb_buff;
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
  sreq->bb_buff = gasnetc_get_bbuf(1);
  memcpy(sreq->bb_buff, (void *)src, nbytes);
  sreq->cep = cep;
  sreq->fh_count = -1;
  if (req_oust) {
    gasnetc_counter_inc(req_oust);
    sreq->req_oust = req_oust;
  }

  sr_desc->opcode      = VAPI_RDMA_WRITE;
  sr_desc->remote_addr = dst;
  sr_desc->r_key       = rkey;
  sr_desc->sg_lst_len  = 1;
  sr_desc->sg_lst_p[0].addr = (uintptr_t)sreq->bb_buff;
  sr_desc->sg_lst_p[0].len  = nbytes;
  sr_desc->sg_lst_p[0].lkey = gasnetc_snd_reg.lkey;

  gasnetc_snd_post(sreq, sr_desc);
}

/* Helper for rdma puts: zero copy case */
static void gasnetc_do_put_zerocp(gasnetc_cep_t *cep, VAPI_rkey_t rkey,
                                  uintptr_t src, uintptr_t dst, size_t nbytes,
                                  gasnetc_counter_t *mem_oust, gasnetc_counter_t *req_oust) {
  GASNETC_DECL_SR_DESC(sr_desc, GASNETC_SND_SG, 1);
  gasnetc_sreq_t *sreq = NULL;
  int seg_count = 0;

  GASNETI_TRACE_EVENT_VAL(C, RDMA_PUT_ZEROCP, nbytes);

  gasneti_assert(nbytes != 0);

  /* loop over local pinned regions */
  do {
    size_t count;

    if (!seg_count) {
      sreq = gasnetc_get_sreq();
      sreq->cep = cep;
      sreq->fh_count = 0;
      sr_desc->opcode      = VAPI_RDMA_WRITE;
      sr_desc->remote_addr = dst;
      sr_desc->r_key       = rkey;
    }

    count = gasnetc_get_lkey(src, nbytes, sreq, sr_desc->sg_lst_p + seg_count);
    gasneti_assert(count <= nbytes);
    if_pt (count) {
      ++seg_count;
    }

    if (count == nbytes) {
      /* Send out the last piece w/ the counters (if any) advanced */
      if (mem_oust) {
        gasnetc_counter_inc(mem_oust);
        sreq->mem_oust = mem_oust;
      }
      if (req_oust) {
        gasnetc_counter_inc(req_oust);
        sreq->req_oust = req_oust;
      }
    }

    if ((count == nbytes) || (seg_count == GASNETC_SND_SG) || !count) {
      sr_desc->sg_lst_len = seg_count;
      gasnetc_snd_post(sreq, sr_desc);
      seg_count = 0;
    }

    src += count;
    dst += count;
    nbytes -= count;
  } while (nbytes);

  gasneti_assert(seg_count == 0);
}

#if GASNETC_PIN_SEGMENT
/* Helper for rdma gets: bounce buffer case */
static void gasnetc_do_get_bounce(gasnetc_cep_t *cep, VAPI_rkey_t rkey,
                                  uintptr_t src, uintptr_t dst, size_t nbytes,
                                  gasnetc_counter_t *req_oust) {
  GASNETC_DECL_SR_DESC(sr_desc, 1, 1);
  gasnetc_sreq_t *sreq;

  GASNETI_TRACE_EVENT_VAL(C, RDMA_GET_BOUNCE, nbytes);

  gasneti_assert(nbytes != 0);
  gasneti_assert(req_oust != NULL);

  /* Use full bounce buffers until just one buffer worth of data remains */
  while (nbytes > GASNETC_BUFSZ) {
    sreq = gasnetc_get_sreq();
    sreq->cep     = cep;
    sreq->fh_count = -1;
    sreq->bb_addr = (void *)dst;
    sreq->bb_len  = GASNETC_BUFSZ;
    sreq->bb_buff = gasnetc_get_bbuf(1);

    if (GASNETC_ANY_PAR) {
      /* We must set counters on all chunks since order of completion is uncertain */
      sreq->req_oust = req_oust;
      gasnetc_counter_inc(req_oust);
    }

    sr_desc->opcode      = VAPI_RDMA_READ;
    sr_desc->remote_addr = src;
    sr_desc->r_key       = rkey;
    sr_desc->sg_lst_len  = 1;
    sr_desc->sg_lst_p[0].addr = (uintptr_t)sreq->bb_buff;
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
  sreq->cep      = cep;
  sreq->fh_count = -1;
  sreq->bb_addr  = (void *)dst;
  sreq->bb_len   = nbytes;
  sreq->bb_buff  = gasnetc_get_bbuf(1);
  sreq->req_oust = req_oust;
  gasnetc_counter_inc(req_oust);

  sr_desc->opcode      = VAPI_RDMA_READ;
  sr_desc->remote_addr = src;
  sr_desc->r_key       = rkey;
  sr_desc->sg_lst_len  = 1;
  sr_desc->sg_lst_p[0].addr = (uintptr_t)sreq->bb_buff;
  sr_desc->sg_lst_p[0].len  = nbytes;
  sr_desc->sg_lst_p[0].lkey = gasnetc_snd_reg.lkey;

  gasnetc_snd_post(sreq, sr_desc);
}
#endif

/* Helper for rdma gets: zero copy case */
static void gasnetc_do_get_zerocp(gasnetc_cep_t *cep, VAPI_rkey_t rkey,
                                  uintptr_t src, uintptr_t dst, size_t nbytes,
                                  gasnetc_counter_t *req_oust) {
  GASNETC_DECL_SR_DESC(sr_desc, GASNETC_SND_SG, 1);
  gasnetc_sreq_t *sreq = NULL;
  int seg_count = 0;

  GASNETI_TRACE_EVENT_VAL(C, RDMA_GET_ZEROCP, nbytes);

  gasneti_assert(nbytes != 0);
  gasneti_assert(req_oust != NULL);

  /* loop over local pinned regions */
  do {
    size_t count;

    if (!seg_count) {
      sreq = gasnetc_get_sreq();
      sreq->cep = cep;
      sreq->req_oust = req_oust;
      gasnetc_counter_inc(req_oust);
      sreq->fh_count = 0;
      sr_desc->opcode      = VAPI_RDMA_READ;
      sr_desc->remote_addr = src;
      sr_desc->r_key       = rkey;
    }

    count = gasnetc_get_lkey(dst, nbytes, sreq, sr_desc->sg_lst_p + seg_count);
    gasneti_assert(count <= nbytes);
    if_pt (count) {
      ++seg_count;
    }

    if ((count == nbytes) || (seg_count == GASNETC_SND_SG) || !count) {
      sr_desc->sg_lst_len = seg_count;
      gasnetc_snd_post(sreq, sr_desc);
      seg_count = 0;
    }

    src += count;
    dst += count;
    nbytes -= count;
  } while (nbytes);

  gasneti_assert(seg_count == 0);
}
#else
GASNET_INLINE_MODIFIER(gasnetc_fh_put_inline)
void gasnetc_fh_put_inline(gasnetc_sreq_t *sreq, const firehose_request_t *fh_rem, size_t len) {
  GASNETC_DECL_SR_DESC(sr_desc, 1, 1);
  gasnetc_counter_t *mem_oust;

  gasneti_assert(sreq->fh_rem_addr >= fh_rem->addr);
  gasneti_assert(sreq->fh_rem_addr + (len - 1) <= fh_rem->addr + (fh_rem->len - 1));

  sreq->fh_count = 1;

  sr_desc->opcode      = VAPI_RDMA_WRITE;
  sr_desc->remote_addr = sreq->fh_rem_addr;
  sr_desc->r_key       = fh_rem->client.rkey;
  sr_desc->sg_lst_len  = 1;
  sr_desc->sg_lst_p[0].addr = sreq->fh_loc_addr;
  sr_desc->sg_lst_p[0].len = len;

  mem_oust = sreq->mem_oust;
  sreq->mem_oust = NULL;

  gasnetc_snd_post_inline(sreq, sr_desc);

  if (mem_oust) {
    /* Because the inline put already copied it */
    gasnetc_counter_dec(mem_oust);
  }
}

GASNET_INLINE_MODIFIER(gasnetc_fh_put_bounce)
void gasnetc_fh_put_bounce(gasnetc_sreq_t *orig_sreq, const firehose_request_t *fh_rem, size_t nbytes) {
  GASNETC_DECL_SR_DESC(sr_desc, 1, 1);
  gasnetc_cep_t *cep = orig_sreq->cep;
  uintptr_t src = orig_sreq->fh_loc_addr;
  uintptr_t dst = orig_sreq->fh_rem_addr;
  VAPI_rkey_t rkey = fh_rem->client.rkey;
  gasnetc_counter_t *mem_oust;

  gasneti_assert(nbytes != 0);
  gasneti_assert(orig_sreq->mem_oust != NULL);
  gasneti_assert(orig_sreq->fh_rem_addr >= fh_rem->addr);
  gasneti_assert(orig_sreq->fh_rem_addr + (nbytes - 1) <= fh_rem->addr + (fh_rem->len - 1));

  /* Use full bounce buffers until just one buffer worth of data remains */
  while (nbytes > GASNETC_BUFSZ) {
    gasnetc_sreq_t *sreq = gasnetc_get_sreq();
    sreq->fh_bbuf = gasnetc_get_bbuf(1);
    memcpy(sreq->fh_bbuf, (void *)src, GASNETC_BUFSZ);
    sreq->cep  = cep;
    sreq->fh_count = 0;

    sr_desc->opcode      = VAPI_RDMA_WRITE;
    sr_desc->remote_addr = dst;
    sr_desc->r_key       = rkey;
    sr_desc->sg_lst_len  = 1;
    sr_desc->sg_lst_p[0].addr = (uintptr_t)sreq->fh_bbuf;
    sr_desc->sg_lst_p[0].len  = GASNETC_BUFSZ;
    sr_desc->sg_lst_p[0].lkey = gasnetc_snd_reg.lkey;

    gasnetc_snd_post(sreq, sr_desc);

    src += GASNETC_BUFSZ;
    dst += GASNETC_BUFSZ;
    nbytes -= GASNETC_BUFSZ;
  }

  /* Send out the last buffer w/ the original resource */
  gasneti_assert(nbytes <= GASNETC_BUFSZ);

  mem_oust = orig_sreq->mem_oust;
  orig_sreq->mem_oust = NULL;
  orig_sreq->fh_count = 1;

  orig_sreq->fh_bbuf = gasnetc_get_bbuf(1);
  memcpy(orig_sreq->fh_bbuf, (void *)src, nbytes);
  gasnetc_counter_dec(mem_oust);

  sr_desc->opcode      = VAPI_RDMA_WRITE;
  sr_desc->remote_addr = dst;
  sr_desc->sg_lst_len  = 1;
  sr_desc->r_key       = rkey;
  sr_desc->sg_lst_p[0].addr = (uintptr_t)orig_sreq->fh_bbuf;
  sr_desc->sg_lst_p[0].len  = nbytes;
  sr_desc->sg_lst_p[0].lkey = gasnetc_snd_reg.lkey;

  gasnetc_snd_post(orig_sreq, sr_desc);
}

GASNET_INLINE_MODIFIER(gasnetc_fh_post)
void gasnetc_fh_post(gasnetc_sreq_t *sreq, VAPI_wr_opcode_t op) {
  GASNETC_DECL_SR_DESC(sr_desc, GASNETC_SND_SG, 1);
  VAPI_sg_lst_entry_t *sg_entry;
  uintptr_t loc_addr;
  size_t remain;
  int i;

  gasneti_assert(sreq->fh_count >= 2);
  gasneti_assert(sreq->fh_count <= GASNETC_MAX_FH);
  gasneti_assert(sreq->fh_ptr[0] != NULL);
  gasneti_assert(sreq->fh_ptr[1] != NULL);

  sr_desc->opcode = op;
  sr_desc->remote_addr = sreq->fh_rem_addr;
  sr_desc->r_key = sreq->fh_ptr[0]->client.rkey;
  sr_desc->sg_lst_len = sreq->fh_count - 1;

  remain = sreq->fh_len;
  loc_addr = sreq->fh_loc_addr;
  sg_entry = sr_desc->sg_lst_p;
  for (i = 1; i < sreq->fh_count; ++i) {
    const firehose_request_t *fh_req = sreq->fh_ptr[i];
    size_t nbytes = MIN(remain, (fh_req->addr + fh_req->len - loc_addr));

    gasneti_assert(remain > 0);
    gasneti_assert(nbytes > 0);

    sg_entry->addr = loc_addr;
    sg_entry->len  = nbytes;
    sg_entry->lkey = fh_req->client.lkey;

    ++sg_entry;
    remain -= nbytes;
    loc_addr += nbytes;
  }
  gasneti_assert(remain == 0);

  gasnetc_snd_post(sreq, sr_desc);
}

static void gasnetc_fh_do_put(gasnetc_sreq_t *sreq, const firehose_request_t *fh_rem, size_t nbytes) {
  if (nbytes <= gasnetc_inline_limit) {
    /* Inline when small enough */
    GASNETI_TRACE_EVENT_VAL(C, RDMA_PUT_INLINE, nbytes);
    gasnetc_fh_put_inline(sreq, fh_rem, nbytes);
  } else if ((nbytes <= gasnetc_bounce_limit) && (sreq->mem_oust != NULL)) {
    /* Bounce buffer use for non-bulk puts (upto a limit) */
    GASNETI_TRACE_EVENT_VAL(C, RDMA_PUT_BOUNCE, nbytes);
    gasnetc_fh_put_bounce(sreq, fh_rem, nbytes);
  } else {
    /* Use the local firehose(s) obtained earlier */
    GASNETI_TRACE_EVENT_VAL(C, RDMA_PUT_ZEROCP, nbytes);
    gasnetc_fh_post(sreq, VAPI_RDMA_WRITE);
  }

  if (sreq->fh_oust) {
    gasnetc_counter_dec(sreq->fh_oust);
  }
}

static void gasnetc_fh_put_cb(void *context, const firehose_request_t *fh_rem, int allLocalHit) {
  gasnetc_sreq_t *sreq = context;

  #if 0
  /* XXX when implementing a piggybacked Put for bug #1057, we must check allLocalHit
     to determine is any AM was sent or not. */
  if (!allLocalHit) {
    /* We *did* send an AM */
    sreq->fh_skip = ...
    /* everything else happens in gasnetc_fh_post, when everything is ready */
  }
  #endif

  sreq->fh_ptr[0] = fh_rem;
  if (gasneti_atomic_decrement_and_test(&sreq->fh_ready)) {
    gasnetc_fh_do_put(sreq, fh_rem, sreq->fh_len);
  }
}

static void gasnetc_fh_do_get(gasnetc_sreq_t *sreq) {
  GASNETI_TRACE_EVENT_VAL(C, RDMA_GET_ZEROCP, sreq->fh_len);
  gasnetc_fh_post(sreq, VAPI_RDMA_READ);
}

static void gasnetc_fh_get_cb(void *context, const firehose_request_t *fh_rem, int allLocalHit) {
  gasnetc_sreq_t *sreq = context;

  sreq->fh_ptr[0] = fh_rem;
  if (gasneti_atomic_decrement_and_test(&sreq->fh_ready)) {
    gasnetc_fh_do_get(sreq);
  }

  if (sreq->fh_oust) {
    gasnetc_counter_dec(sreq->fh_oust);
  }
}

GASNET_INLINE_MODIFIER(gasnetc_get_local_fh)
size_t gasnetc_get_local_fh(gasnetc_sreq_t *sreq, uintptr_t loc_addr, size_t len) {
  size_t remain;
  int i;

  gasneti_assert(len != 0);

  for (i = 1, remain = len; (remain && (i < GASNETC_MAX_FH)); ++i) {
    const firehose_request_t *fh_loc = firehose_try_local_pin(loc_addr, 1, NULL);
    if (!fh_loc) {
      break;
    } else {
      size_t nbytes = MIN(remain, (fh_loc->addr + fh_loc->len - loc_addr));
      sreq->fh_ptr[i] = fh_loc;
      remain -= nbytes;
      loc_addr += nbytes;
    }
  }
  if (i > 1) {
    sreq->fh_count = i;
    len -= remain;
  } else {
    /* Local (mis)alignment could limit how much we can pin */
    len = MIN(len, (gasnetc_fh_maxsz - (loc_addr & (FH_BUCKET_SIZE - 1))));
    sreq->fh_ptr[1] = firehose_local_pin(loc_addr, len, NULL);
    sreq->fh_count = 2;
  }

  return len;
}

GASNET_INLINE_MODIFIER(gasnetc_fh_put_helper)
int gasnetc_fh_put_helper(gasnet_node_t node, gasnetc_sreq_t *sreq,
		          uintptr_t loc_addr, uintptr_t rem_addr, size_t len) {
  const firehose_request_t *fh_rem;

  sreq->fh_rem_addr = rem_addr;
  sreq->fh_loc_addr = loc_addr;

  /* See how much (if any) is already pinned.  A call to firehose_partial_remote_pin()
   * might acquire a firehose for a region starting above rem_addr.  By instead calling
   * firehose_try_remote_pin() with len==1, we get a *contiguous* firehose if available.
   * We count on the implementation of firehose region giving out the largest region
   * that covers our request.
   */
  fh_rem = firehose_try_remote_pin(node, rem_addr, 1, 0, NULL);

  if_pt (fh_rem != NULL) {
    /* HIT in remote firehose table - some initial part of the region is pinned */
    sreq->fh_ptr[0] = fh_rem;
    gasneti_assert(rem_addr >= fh_rem->addr);
    gasneti_assert(rem_addr <= (fh_rem->addr + fh_rem->len - 1));
    len = MIN(len, (fh_rem->addr + fh_rem->len - rem_addr));
  } else {
    /* MISS - Some initial part (or all) of the region is unpinned */
    gasneti_atomic_set(&sreq->fh_ready, 2);
    len = MIN(len, (gasnetc_fh_maxsz - (rem_addr & (FH_BUCKET_SIZE - 1))));
    (void)firehose_remote_pin(node, rem_addr, len, 0, NULL,
			      NULL, &gasnetc_fh_put_cb, sreq);
  }

  /* Get local firehose(s) IFF inline and bounce-buffers are not to be used.
   * We do this here to overlap with the in-flight AM if applicable.
   */
  if (!(len <= gasnetc_inline_limit) &&
      !((len <= gasnetc_bounce_limit) && (sreq->mem_oust != NULL))) {
    len = gasnetc_get_local_fh(sreq, loc_addr, len);
  }
  sreq->fh_len = len;

  if ((fh_rem != NULL) || gasneti_atomic_decrement_and_test(&sreq->fh_ready)) {
    gasnetc_fh_do_put(sreq, fh_rem, len);
  }

  return len;
}

GASNET_INLINE_MODIFIER(gasnetc_fh_get_helper)
int gasnetc_fh_get_helper(gasnet_node_t node, gasnetc_sreq_t *sreq,
		          uintptr_t loc_addr, uintptr_t rem_addr, size_t len) {
  const firehose_request_t *fh_rem;

  sreq->fh_rem_addr = rem_addr;
  sreq->fh_loc_addr = loc_addr;

  /* See how much (if any) is already pinned.  A call to firehose_partial_remote_pin()
   * might acquire a firehose for a region starting above rem_addr.  By instead calling
   * firehose_try_remote_pin() with len==1, we get a *contiguous* firehose if available.
   * We count on the implementation of firehose region giving out the largest region
   * that covers our request.
   */
  fh_rem = firehose_try_remote_pin(node, rem_addr, 1, 0, NULL);

  if_pt (fh_rem != NULL) {
    /* HIT in remote firehose table - some initial part of the region is pinned */
    sreq->fh_ptr[0] = fh_rem;
    gasneti_assert(rem_addr >= fh_rem->addr);
    gasneti_assert(rem_addr <= (fh_rem->addr + fh_rem->len - 1));
    len = MIN(len, (fh_rem->addr + fh_rem->len - rem_addr));
  } else {
    /* MISS: Some initial part (or all) of the region is unpinned */
    gasneti_atomic_set(&sreq->fh_ready, 2);
    len = MIN(len, (gasnetc_fh_maxsz - (rem_addr & (FH_BUCKET_SIZE - 1))));
    (void)firehose_remote_pin(node, rem_addr, len, 0, NULL,
			      NULL, &gasnetc_fh_get_cb, sreq);
  }

  len = gasnetc_get_local_fh(sreq, loc_addr, len);
  sreq->fh_len = len;

  if ((fh_rem != NULL) || gasneti_atomic_decrement_and_test(&sreq->fh_ready)) {
    gasnetc_fh_do_get(sreq);
  }

  return len;
}
#endif

/* ------------------------------------------------------------------------------------ *
 *  Externally visible functions                                                        *
 * ------------------------------------------------------------------------------------ */

extern int gasnetc_sndrcv_init(void) {
  VAPI_cqe_num_t	act_size;
  VAPI_ret_t		vstat;
  gasnetc_buffer_t	*buf;
  gasnetc_rbuf_t	*rbuf;
  gasnetc_sreq_t	*sreq;
  int 			padded_size, rcv_count, i;

  /*
   * Check/compute limits before allocating anything
   */


  if (gasnetc_op_oust_limit == 0) { /* 0 = automatic limit computation */
    gasnetc_op_oust_limit = gasnetc_hca_cap.max_num_ent_cq;
  }
  gasnetc_op_oust_limit = MIN(gasnetc_op_oust_limit, gasnetc_op_oust_pp * (gasneti_nodes - 1));
  GASNETI_TRACE_PRINTF(C, ("Final/effective GASNET_NETWORKDEPTH_TOTAL = %d", gasnetc_op_oust_limit));
  if (gasnetc_op_oust_limit > gasnetc_hca_cap.max_num_ent_cq) {
    GASNETI_RETURN_ERRR(RESOURCE, "GASNET_NETWORKDEPTH_{PP,TOTAL} exceed HCA capabilities");
  }

  if (gasnetc_am_oust_limit == 0) { /* 0 = automatic limit computation */
    int used = /* max inbound Req: */ gasnetc_am_oust_pp * (gasneti_nodes - 1) +
	       /* dedicated spare: */ (gasnetc_use_rcv_thread ? 1 : 0);
    gasnetc_am_oust_limit = gasnetc_hca_cap.max_num_ent_cq - used;
  }
  gasnetc_am_oust_limit = MIN(gasnetc_am_oust_limit, gasnetc_am_oust_pp * (gasneti_nodes - 1));
  gasnetc_am_oust_limit = MIN(gasnetc_am_oust_limit, gasnetc_op_oust_limit);
  GASNETI_TRACE_PRINTF(C, ("Final/effective GASNET_AM_CREDITS_TOTAL = %d", gasnetc_am_oust_limit));
  rcv_count = /* max inbound Req: */ gasnetc_am_oust_pp * (gasneti_nodes - 1) +
	      /* max inbound Rep: */ gasnetc_am_oust_limit +
	      /* dedicated spare: */ (gasnetc_use_rcv_thread ? 1 : 0);
  if (rcv_count > gasnetc_hca_cap.max_num_ent_cq) {
    GASNETI_RETURN_ERRR(RESOURCE, "GASNET_AM_CREDIT_{PP,TOTAL} exceed HCA capabilities");
  }

  if (gasnetc_bbuf_limit == 0) { /* 0 = automatic limit computation */
    /* We effectively count local AMs against gasnetc_op_oust_limit for simplicity,
     * but only expect one in-flight per thread anyway. */
    gasnetc_bbuf_limit = gasnetc_op_oust_limit;
  } else {
    gasnetc_bbuf_limit = MIN(gasnetc_bbuf_limit, gasnetc_op_oust_limit);
  }
  if (gasneti_nodes == 1) {
    /* no AM or RDMA on the wire, but still need bufs for constructing AMs */
    gasnetc_bbuf_limit = gasnetc_am_oust_pp;
  }
  GASNETI_TRACE_PRINTF(C, ("Final/effective GASNET_BBUF_LIMIT = %d", gasnetc_bbuf_limit));

  /*
   * setup RCV resources
   */

  /* create the RCV CQ */
  vstat = VAPI_create_cq(gasnetc_hca, rcv_count , &gasnetc_rcv_cq, &act_size);
  GASNETC_VAPI_CHECK(vstat, "from VAPI_create_cq(rcv_cq)");
  gasneti_assert(act_size >= rcv_count);

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
    buf = gasnetc_alloc_pinned(rcv_count * sizeof(gasnetc_buffer_t),
			       VAPI_EN_LOCAL_WRITE, &gasnetc_rcv_reg);
    if_pf (buf == NULL) {
      (void)VAPI_destroy_cq(gasnetc_hca, gasnetc_snd_cq);
      (void)VAPI_destroy_cq(gasnetc_hca, gasnetc_rcv_cq);
      GASNETI_RETURN_ERRR(RESOURCE, "Unable to allocate pinned memory for AM recv buffers");
    }

    /* Allocated normal memory for receive descriptors (rbuf's) */
    padded_size = GASNETC_ALIGNUP(sizeof(gasnetc_rbuf_t), GASNETI_CACHE_LINE_BYTES);
    gasnetc_rbuf_alloc = gasneti_malloc(rcv_count*padded_size + GASNETI_CACHE_LINE_BYTES-1);

    /* Initialize the rbuf's */
    rbuf = (gasnetc_rbuf_t *)GASNETC_ALIGNUP(gasnetc_rbuf_alloc, GASNETI_CACHE_LINE_BYTES);
    for (i = 0; i < rcv_count; ++i) {
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
  gasnetc_sema_init(&gasnetc_cq_sema, gasnetc_op_oust_limit);

  /* create the SND CQ */
  vstat = VAPI_create_cq(gasnetc_hca, gasnetc_op_oust_limit, &gasnetc_snd_cq, &act_size);
  GASNETC_VAPI_CHECK(vstat, "from VAPI_create_cq(snd_cq)");
  gasneti_assert(act_size >= gasnetc_op_oust_limit);

  /* Allocated pinned memory for AMs and bounce buffers */
  buf = gasnetc_alloc_pinned(gasnetc_bbuf_limit * sizeof(gasnetc_buffer_t),
			     VAPI_EN_LOCAL_WRITE, &gasnetc_snd_reg);
  if_pf (buf == NULL) {
    if (gasneti_nodes > 1) {
      if (gasnetc_use_rcv_thread) {
	vstat = EVAPI_clear_comp_eventh(gasnetc_hca, gasnetc_rcv_handler);
      }
      gasneti_free(gasnetc_rbuf_alloc);
      gasnetc_free_pinned(&gasnetc_rcv_reg);
    }
    (void)VAPI_destroy_cq(gasnetc_hca, gasnetc_snd_cq);
    (void)VAPI_destroy_cq(gasnetc_hca, gasnetc_rcv_cq);
    GASNETI_RETURN_ERRR(RESOURCE, "Unable to allocate pinned memory for AM/bounce buffers");
  }
  for (i = 0; i < gasnetc_bbuf_limit; ++i) {
    gasneti_freelist_put(&gasnetc_bbuf_freelist, buf);
    ++buf;
  }

  /* Allocated normal memory for send requests (sreq's) */
  /* This initial allocation is the same size as the BBUF limit,
   * but this pool can grow dynamically if more are needed. */
  padded_size = GASNETC_ALIGNUP(MAX(sizeof(gasnetc_sreq_t),
				    sizeof(gasneti_freelist_ptr_t)),
			        GASNETI_CACHE_LINE_BYTES);
  gasnetc_sreq_alloc = gasneti_malloc(gasnetc_bbuf_limit*padded_size + GASNETI_CACHE_LINE_BYTES-1);
  sreq = (gasnetc_sreq_t *)GASNETC_ALIGNUP(gasnetc_sreq_alloc, GASNETI_CACHE_LINE_BYTES);
  for (i = 0; i < gasnetc_bbuf_limit; ++i) {
    gasneti_freelist_put(&gasnetc_sreq_freelist, sreq);
    sreq = (gasnetc_sreq_t *)((uintptr_t)sreq + padded_size);
  }

  return GASNET_OK;
}

extern void gasnetc_sndrcv_init_peer(gasnet_node_t node) {
  int i;
  
  if (node != gasneti_mynode) {
    /* Prepost one for each possible incomming request */
    gasnetc_cep_t *cep = GASNETC_CEP(node, 0);

    for (i = 0; i < gasnetc_am_oust_pp; ++i) {
      gasnetc_rcv_post(cep, gasneti_freelist_get(&gasnetc_rbuf_freelist));
    }

    gasnetc_sema_init(&cep->am_sema, gasnetc_am_oust_pp);
    gasnetc_sema_init(&cep->sq_sema, gasnetc_op_oust_pp);
  } else {
    /* Should never use these for loopback */
    gasnetc_cep_t *cep = GASNETC_CEP(node, 0);
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
     * So we leak any singletons we may have allocated
     */
    gasneti_free(gasnetc_sreq_alloc);
  }

  vstat = VAPI_destroy_cq(gasnetc_hca, gasnetc_rcv_cq);
  GASNETC_VAPI_CHECK(vstat, "from VAPI_destroy_cq(rcv_cq)");

  vstat = VAPI_destroy_cq(gasnetc_hca, gasnetc_snd_cq);
  GASNETC_VAPI_CHECK(vstat, "from VAPI_destroy_cq(snd_cq)");
}

extern void gasnetc_sndrcv_fini_peer(gasnet_node_t node) {
  VAPI_ret_t vstat;
  int i;

  if (node != gasneti_mynode) {
    for (i = 0; i < GASNETC_CEPS; ++i) {
      gasnetc_cep_t *cep = GASNETC_CEP(node, i);
      vstat = VAPI_destroy_qp(gasnetc_hca, cep->qp_handle);
      GASNETC_VAPI_CHECK(vstat, "from VAPI_destroy_qp()");
    }
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
 * If firehose is disabled, then bounce buffers are used for unpinned sources.
 */
extern int gasnetc_rdma_put(int node, void *src_ptr, void *dst_ptr, size_t nbytes, gasnetc_counter_t *mem_oust, gasnetc_counter_t *req_oust) {
  gasnetc_peer_t *peer = &gasnetc_peer[node];
  gasnetc_cep_t *cep = GASNETC_CEP(node, 0);
  uintptr_t src = (uintptr_t)src_ptr;
  uintptr_t dst = (uintptr_t)dst_ptr;

  gasneti_assert(nbytes != 0);
  
  do {
    /* Loop over contiguous pinned regions on remote end */
    size_t count = nbytes;
    VAPI_rkey_t rkey;
    gasnetc_get_rkey(peer, dst, &count, &rkey);

    if (count <= gasnetc_inline_limit) {
      /* Use a short-cut for sends that are short enough.
       *
       * Note that we do this based only on the size of the request, without bothering to check whether
       * the caller cares about local completion, or whether zero-copy is possible.
       * We do this is because the cost of this small copy appears cheaper then the alternative logic.
       */
      gasnetc_do_put_inline(cep, rkey, src, dst, count, req_oust);
    } else if_pf (!gasnetc_use_firehose && gasnetc_unpinned(src, &count)) {
      /* Firehose disabled.  Use bounce buffers since src is out-of-segment */
      gasnetc_do_put_bounce(cep, rkey, src, dst, count, req_oust);
    } else if ((count <= gasnetc_bounce_limit) && (mem_oust != NULL)) {
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
 * Uses zero-copy (with firehose if the destination is not pre-pinned).
 * If firehose is disabled, then bounce buffers are used for unpinned destinations.
 */
extern int gasnetc_rdma_get(int node, void *src_ptr, void *dst_ptr, size_t nbytes, gasnetc_counter_t *req_oust) {
  gasnetc_peer_t *peer = &gasnetc_peer[node];
  gasnetc_cep_t *cep = GASNETC_CEP(node, 0);
  uintptr_t src = (uintptr_t)src_ptr;
  uintptr_t dst = (uintptr_t)dst_ptr;

  gasneti_assert(nbytes != 0);
  gasneti_assert(req_oust != NULL);

  do {
    /* Loop over contiguous pinned regions on remote end */
    size_t count = nbytes;
    VAPI_rkey_t rkey;

    gasnetc_get_rkey(peer, src, &count, &rkey);

    if_pf (!gasnetc_use_firehose && gasnetc_unpinned(dst, &count)) {
      /* Firehose disabled.  Use bounce buffers since dst is out-of-segment */
      gasnetc_do_get_bounce(cep, rkey, src, dst, count, req_oust);
    } else {
      gasnetc_do_get_zerocp(cep, rkey, src, dst, count, req_oust);
    }

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
  gasnetc_cep_t *cep = GASNETC_CEP(node, 0);
  uintptr_t src = (uintptr_t)src_ptr;
  uintptr_t dst = (uintptr_t)dst_ptr;

  gasneti_assert(nbytes != 0);

  do {
    gasnetc_sreq_t *sreq = gasnetc_get_sreq();
    size_t count;

    sreq->cep   = cep;
    sreq->fh_bbuf = NULL;
 
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
      sreq->fh_oust = am_oust;
    }

    count = gasnetc_fh_put_helper(node, sreq, src, dst, nbytes);

    src += count;
    dst += count;
    nbytes -= count;
  } while (nbytes);

  return 0;
}

/* Perform an RDMA get */
extern int gasnetc_rdma_get(int node, void *src_ptr, void *dst_ptr, size_t nbytes, gasnetc_counter_t *req_oust) {
  gasnetc_cep_t *cep = GASNETC_CEP(node, 0);
  uintptr_t src = (uintptr_t)src_ptr;
  uintptr_t dst = (uintptr_t)dst_ptr;

  gasneti_assert(nbytes != 0);
  gasneti_assert(req_oust != NULL);

  do {
    gasnetc_sreq_t *sreq = gasnetc_get_sreq();
    size_t count;

    sreq->cep   = cep;
 
    /* We must set counters on all chunks since order of completion is uncertain */
    sreq->req_oust = req_oust;
    gasnetc_counter_inc(req_oust);

    count = gasnetc_fh_get_helper(node, sreq, dst, src, nbytes);

    src += count;
    dst += count;
    nbytes -= count;
  } while (nbytes);

  return 0;
}
#endif

/* Putv - contiguous remote dst, vector local src
 *
 * Initial naive implementation
 */
extern int gasnetc_rdma_putv(int node, size_t srccount, gasnet_memvec_t const srclist[], void *dst_ptr, gasnetc_counter_t *mem_oust, gasnetc_counter_t *req_oust) {
  while (srccount) {
    /* XXX: check return value for errors */
    (void)gasnetc_rdma_put(node, srclist->addr, dst_ptr, srclist->len, mem_oust, req_oust);
    --srccount;
    ++srclist;
  }

  return 0;
}

/* Getv - contiguous remote src, vector local dst
 *
 * Initial naive implementation
 */
extern int gasnetc_rdma_getv(int node, void *src_ptr, size_t dstcount, gasnet_memvec_t const dstlist[], gasnetc_counter_t *req_oust) {
  while (dstcount) {
    /* XXX: check return value for errors */
    (void)gasnetc_rdma_get(node, src_ptr, dstlist->addr, dstlist->len, req_oust);
    --dstcount;
    ++dstlist;
  }

  return 0;
}

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

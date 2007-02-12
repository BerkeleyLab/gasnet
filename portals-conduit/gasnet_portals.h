#ifndef GASNET_PORTALS_H
#define GASNET_PORTALS_H

#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <portals/portals3.h>
#include <gasnet_internal.h>
#include <gasnet_core_internal.h>
#include <gasnet_extended_internal.h>
#include <gasnet_handler.h>

/* ------------------------------------------------------------------------------------ */
/* MLW:  Support for Portals 3.0 */

/* set to 1 to compile in Sandia specific Accelerated Portals code */
#define GASNETC_USE_SANDIA_ACCEL 0

/* defined to 1 the following to pack the RplSB offset in the upper 32
 * bits of the match_bits.
 * If defined to 0, this space will be used for AM Reply handler arguments
 */
#define GASNETC_PACK_RPLOFF_MBITS 1

/* Do we register an EQ handler with a queue or just poll ourselves */
#if GASNETC_USE_EQ_HANDLER
  #define GASNETC_EQ_HANDLER gasnetc_event_handler
  #define GASNETC_CALL_EQ_HANDLER(ev) gasnetc_event_handler(&ev)
#else
  #define GASNETC_EQ_HANDLER NULL
  #define GASNETC_CALL_EQ_HANDLER(ev) (*(gasnetc_ptl_event_handler)(ev.md.user_ptr))(&(ev)) 
#endif

/* Max transfer size SHOULD be defined by portals, but apparently is not */
#ifdef PTL_MAX_TRANS_SZ
#define GASNETC_PTL_MAX_TRANS_SZ PTL_MAX_TRANS_SZ
#else
#define GASNETC_PTL_MAX_TRANS_SZ 2147483648UL
#endif

/* types of polling */
typedef enum{GASNETC_NO_POLL=0, GASNETC_SAFE_POLL, GASNETC_FULL_POLL} gasnetc_pollflag_t;

/* check for signal and call gasnet_exit */
#if GASNETC_USE_SANDIA_ACCEL
#define GASNETC_CHECKSIG() do {						\
    int sig = gasneti_weakatomic_read(&gasnetc_got_signum,0);		\
    if (sig) {								\
      gasneti_weakatomic_set(&gasnetc_got_signum,0,0);			\
      fprintf(stderr,"*** Caught a signal number %i on node %i/%i\n",	\
	      sig,(int)gasneti_mynode,(int)gasneti_nodes);		\
      gasnet_exit(1);							\
    }									\
  } while(0);
#else
#define GASNETC_CHECKSIG()
#endif

/* Macro that checks error condition of Portals calls */
#define GASNETC_PTLSAFE(fncall) do {					\
    int _retcode;							\
    GASNETC_CHECKSIG();							\
    _retcode = (fncall);						\
    if_pf (_retcode != (int)PTL_OK) {					\
      gasneti_fatalerror("\nGASNet Portals encountered an error: %s (%i)\n" \
			 "  while calling: %s\n"			\
			 "  at %s",					\
			 ptl_err_str[_retcode], _retcode, #fncall, gasneti_current_loc); \
    }									\
 } while (0)

/* Portals Access table not implemented on XT3 */
#define GASNETC_PTL_AC_ID  0

/* We need Cray to reserve two table entries for UPC/GASNET
 * We believe these two are currently not used.
 */
#define GASNETC_PTL_RAR_PTE 38
#define GASNETC_PTL_AM_PTE 39

/* Values that are encoded in the MBITs of Portals Data Transfer ops */
#define GASNETC_PTL_IGNORE_BITS  0xFFFFFFFFFFFFFFF0ULL
#define GASNETC_PTL_RAR_BITS     0x00
#define GASNETC_PTL_RARAM_BITS   0x01
#define GASNETC_PTL_RARSRC_BITS  0x02
#define GASNETC_PTL_REQRB_BITS   0x03
#define GASNETC_PTL_CB_BITS      0x03
#define GASNETC_PTL_REQSB_BITS   0x04
#define GASNETC_PTL_SYS_BITS     0x05

/* Operation type */
#define GASNETC_PTL_MSG_PUT      0x10
#define GASNETC_PTL_MSG_GET      0x20
#define GASNETC_PTL_MSG_AM       0x40
#define GASNETC_PTL_MSG_AMDATA   0x80
#define GASNETC_PTL_MSG_DOLC     0x80
/* DOLC - Do Local Completion flag for Extended API Puts/Gets.
 * Can occupy same bit as AMDATA flag since is only used when PUT or GET
 * Indicates to event handler that it must decrement the local completion counter
 * for this gasnet operation
 */

/* Additional flags used for Active Messages */
#define GASNETC_PTL_AM_SHORT     0x01
#define GASNETC_PTL_AM_MEDIUM    0x02
#define GASNETC_PTL_AM_LONG      0x04
#define GASNETC_PTL_AM_REQUEST   0x08
#define GASNETC_PTL_AM_PACKED    0x10
#define GASNETC_PTL_AM_SYNC      0x20

/* Token flag values */
#define GASNETC_PTL_REPLY_SENT   0x01

/* Masks used in constructing 64-bit bit fields */
#define GASNETC_SELECT_UPPER32     0xFFFFFFFF00000000ULL
#define GASNETC_SELECT_LOWER32     0x00000000FFFFFFFFULL
#define GASNETC_SELECT_OPBITS      0x00000000FFFFFF00ULL
#define GASNETC_SELECT_BYTE0       0x00000000000000FFULL
#define GASNETC_SELECT_BYTE1       0x000000000000FF00ULL
#define GASNETC_SELECT_BYTE2       0x0000000000FF0000ULL
#define GASNETC_SELECT_BYTE3       0x00000000FF000000ULL
#define GASNETC_SELECT_BYTE4       0x000000FF00000000ULL
#define GASNETC_SELECT_BYTE5       0x0000FF0000000000ULL
#define GASNETC_SELECT_BYTE6       0x00FF000000000000ULL
#define GASNETC_SELECT_BYTE7       0xFF00000000000000ULL

/* x is a uint64_t and a and b are int32_t */
#define GASNETC_PACK_UPPER(lhs,rhs) lhs = ((lhs) & GASNETC_SELECT_LOWER32) | ((uint64_t)(rhs) << 32)
#define GASNETC_PACK_LOWER(lhs,rhs) lhs = ((lhs) & GASNETC_SELECT_UPPER32) | ((uint64_t)(rhs) & GASNETC_SELECT_LOWER32)
#define GASNETC_PACK_2INT(x,up,low) x = ((uint64_t)(up)<<32) | ((uint64_t)(low) & GASNETC_SELECT_LOWER32)

#define GASNETC_UNPACK_UPPER(x) (int32_t)((x)>>32)
#define GASNETC_UNPACK_LOWER(x) (int32_t)((x)&GASNETC_SELECT_LOWER32)
#define GASNETC_UNPACK_2INT(x,up,low) do { \
    (up) = GASNETC_UNPACK_UPPER(x);	   \
    (low) = GASNETC_UNPACK_LOWER(x);	   \
  } while (0)

#define gasnetc_alloc_ticket(semptr) gasneti_semaphore_trydown(semptr)
#define gasnetc_return_ticket(semptr) gasneti_semaphore_up(semptr)
#define gasnetc_num_tickets(semptr) gasneti_semaphore_read(semptr)


/* poll until thread has cached requested number of send tickets */
#define GASNETC_GET_SEND_TICKETS(th,nsend,pollcnt) do {			\
    if (gasnetc_msg_limit == 0) {					\
      th->snd_tickets = nsend; /* no limit */				\
    } else {								\
      while(th->snd_tickets < nsend) {					\
	if (gasnetc_alloc_ticket(&gasnetc_send_tickets)) {		\
	  th->snd_tickets++;						\
	} else {							\
	  pollcnt++;							\
	  GASNETI_TRACE_EVENT(C, MSG_THROTTLE);				\
	  gasneti_AMPoll();						\
	}								\
      }									\
    }									\
  } while(0);

/* Before starting an AM Request, poll until certain conditions are met */
#define GASNETC_COMMON_AMREQ_START(state,offset,th,nsend) do {		\
    int pollcnt = 0;							\
    while (gasneti_weakatomic_read(&((state)->in_recovery), 0)) {	\
      pollcnt++;							\
      gasneti_AMPoll();							\
    }									\
    /* Allocate a send buffer */					\
    while (!gasnetc_chunk_alloc(&gasnetc_ReqSB, GASNETC_CHUNKSIZE, &(offset)) ) { \
      pollcnt++;							\
      gasneti_AMPoll();							\
    }									\
    GASNETC_GET_SEND_TICKETS(th,nsend,pollcnt);				\
    /* Insure at least one full poll before AM */			\
    if (!pollcnt) gasneti_AMPoll();					\
    /* Polling may have spent our send tickets, get them again */	\
    GASNETC_GET_SEND_TICKETS(th,nsend,pollcnt);				\
  } while (0)

#define GASNETC_PACK_AM_MBITS(mbits, offset, numarg, hndlr, amflag, targ_mbits) \
    (mbits) = ((uint64_t)(offset) << 32) | ( ((uint64_t)(numarg) & GASNETC_SELECT_BYTE0) << 24) \
      | ((uint64_t)(hndlr) << 16) | ((uint64_t)(amflag) << 8) | (uint64_t)(targ_mbits)

#define GASNETC_UNPACK_AM_MBITS(mbits, offset, numarg, hndlr, amflag, targ_mbits) do { \
    offset     = (mbits)>>32;						\
    numarg     = ((mbits)&GASNETC_SELECT_BYTE3)>>24;			\
    hndlr      = ((mbits)&GASNETC_SELECT_BYTE2)>>16;			\
    amflag     = ((mbits)&GASNETC_SELECT_BYTE1)>>8;			\
    targ_mbits =  (mbits)&GASNETC_SELECT_BYTE0;				\
  } while(0)

#define GASNETC_GET_AM_LOWBITS(mbits,numarg,ghndlr,amflag) do { \
    numarg = ((mbits)&GASNETC_SELECT_BYTE3) >> 24;		\
    ghndlr = ((mbits)&GASNETC_SELECT_BYTE2) >> 16;		\
    amflag = ((mbits)&GASNETC_SELECT_BYTE1) >>  8;		\
  } while(0)

#define GASNETC_GET_MSG_TYPE(mbits) ((mbits) & 0xF0)
#define GASNETC_SET_MSG_TYPE(mbits,mtyp) (((mbits) & 0xFFFFFFFFFFFFFF0F) | ((mtyp) & 0xF0))

#define GASNETC_COMPUTE_DOUBLE_PAD(n,pad) do {	\
    int p = (n) % sizeof(double);		\
    pad = (p == 0 ? 0 : sizeof(double)-p);	\
  } while(0)

#define GASNETC_PTL_OFFSET(n,s) ((uint8_t*)(s) - (uint8_t*)gasneti_seginfo[n].addr)

/* Macro to decrement the AM_pending field of a connection state record
 * We just completed an AM operation
 * If node is in recovery, no other thread will be accessing AM_pending, all
 * at best would be polling on in_recovery.  So, if about to come out of
 * recovery state, decrement AM_pending first
 */
#define DECREMENT_AM_PENDING(state) do {	\
    if (gasneti_weakatomic_read(&state->in_recovery, 0)) {		\
      int pending = (int)gasneti_weakatomic_read(&state->AM_pending, 0); \
      gasneti_weakatomic_decrement(&state->AM_pending,0);		\
      if (pending == 1) {						\
	/* all AMs to this target complete, no longer in recovery */	\
	gasneti_weakatomic_set(&state->in_recovery, 0, 0);		\
      }									\
    } else {								\
      gasneti_weakatomic_decrement(&state->AM_pending,0);		\
    }									\
  } while(0)

/* AM tokens used by portals */
typedef struct token_rec {
  uint8_t           flags;
  uint32_t          initiator_offset;
  ptl_size_t        rplsb_offset;
  ptl_process_id_t  initiator;
  gasnet_node_t     srcnode;
} gasnetc_ptl_token_t;

#define GASNETC_INITLOCK_LIDCACHE(srcnode)			\
  gasneti_mutex_init(&gasnetc_conn_state[srcnode].lidlock)
#define GASNETC_LOCK_LIDCACHE(srcnode)				\
  gasneti_mutex_lock(&gasnetc_conn_state[srcnode].lidlock)
#define GASNETC_UNLOCK_LIDCACHE(srcnode)			\
  gasneti_mutex_unlock(&gasnetc_conn_state[srcnode].lidlock)

#define GASNETC_LID_DATA_HERE    0x1
#define GASNETC_LID_HEADER_HERE  0x2
/* data cached by Long Put or AM Long Header */
typedef struct gasnetc_amlongcache_rec {
  uint8_t             flags;
  gasnet_handler_t    ghandler;
  uint32_t            dest_lid;
  uint32_t            initiator_offset;
  uint32_t            narg;
  struct gasnetc_amlongcache_rec *next;
  void               *data;
  size_t              datalen;
  gasnet_handlerarg_t args[];
} gasnetc_amlongcache_t;

/* gasnet connection state used for AM send squelch */
typedef struct gconrec {
  gasneti_weakatomic_t AM_pending;
  gasneti_weakatomic_t in_recovery;
  int                  got_shutdown_msg;
  gasneti_weakatomic_t src_lid;  /* must be 32 bit unsigned so will roll after 2^32 */
  gasneti_mutex_t      lidlock;
  gasnetc_amlongcache_t *lids;   /* lids cache objects are stored on list indexed by src node */
} gasnetc_conn_t;
/* array of connection states */
extern gasnetc_conn_t *gasnetc_conn_state;

/* Flag to determine if we use Portals or MPI for AMs */
extern int gasnetc_use_AM_portals;

#if GASNETC_USE_SANDIA_ACCEL
extern int gasnetc_use_accel;
#endif

/* Types of GASNET Portals Memory Descriptors */
enum { GASNETC_RAR_MD,
       GASNETC_RARAM_MD,
       GASNETC_RARSRC_MD,
       GASNETC_REQSB_MD,
       GASNETC_REQRB_MD,
       GASNETC_RPLSB_MD,
       GASNETC_CB_MD,
       GASNETC_TMP_MD,
       GASNETC_SYS_SEND_MD,
       GASNETC_SYS_RECV_MD,
       GASNETC_NUM_MD
};

/* an array of MD names for diagnostics */
extern const char* gasnetc_md_name[];

/* pointer to event handler functions */
typedef void (*gasnetc_ptl_event_handler)(ptl_event_t *ev);

/* Number of temporary Portals MDs in use at any time */
#define GASNETC_MAX_TMP_MDS 1024
extern int gasnetc_max_tmpmd;
extern gasneti_semaphore_t gasnetc_tmpmd_tickets;
#if GASNETI_STATS_OR_TRACE
int gasnetc_tmpmd_hwm;
#endif

extern int gasnetc_io_buffer_size;
extern void* gasnetc_flush_buffer;

/* An array of Portals Proc IDs used to determine network address of nodes
 * Also used as elements in a hash table, for reverse lookup of ptl_process_id_t
 * structures to gasnet_node_t.
 */
typedef struct gasnetc_procrec {
  gasnet_node_t     node_id;
  ptl_process_id_t  ptl_id;
  struct gasnetc_procrec *next;  /* linked list for hash table reverse lookup */
} gasnetc_procid_t;

extern ptl_process_id_t    gasnetc_myid;
extern ptl_uid_t           gasnetc_uid;
extern gasnetc_procid_t   *gasnetc_procid_map;

/* An array of strings that name the Portals events
 * MLW: Not defined in API but exists in Portals implementation
 */
extern char* ptl_event_str[];


/* -----------------------------------------------------------------------------------
 * A simple chunk allocator used for put/get bounce buffer
 * WARNING: Not a thread-safe freelist implementation!!!
 * Will have to re-implement for multi-threaded (Linux) XT3
 */
#define GASNETC_CHUNKSIZE 1024
typedef union _gasnetc_chunk {
    uint8_t chunk[GASNETC_CHUNKSIZE];
    union _gasnetc_chunk *next;
} gasnetc_chunk_t;

#ifdef GASNET_PAR
#define GASNETC_REQRB_START(start_addr) do {			\
    gasnetc_PtlBuffer_t *p = ReqRB_getbuf(start_addr);		\
    gasneti_weakatomic_increment(&p->threads_active, 0);	\
  } while(0)
#define GASNETC_REQRB_FINISH(start_addr) do {			\
    gasnetc_PtlBuffer_t *p = ReqRB_getbuf(start_addr);		\
    gasneti_weakatomic_decrement(&p->threads_active, 0);	\
  } while(0)
#else
#define GASNETC_REQRB_START(bufptr)  do {} while(0)
#define GASNETC_REQRB_FINISH(bufptr)  do {} while(0)
#endif

/* The RAR, RARAM, and the AM request/reply send/receive buffers are described by */
typedef struct {
  size_t alignment;                    /* alignment (power of 2) */
  size_t nbytes;                       /* number of bytes in buffer after alignment */
  void*  actual_start;                 /* returned by allocator */
  void*  start;                        /* aligned start */
  ptl_handle_md_t  md_h;               /* The Portals memory descriptor handler */
  ptl_handle_me_t  me_h;               /* The Portals match-list entry handle (if used) */
  char *name;                          /* string used for diagnostics */
  int use_chunks;                      /* Is the buffer under control of a chunk allocator? */
  gasneti_weakatomic_t threads_active; /* Used only in ReqRB, counts number of threads
					* actively using buffer */

  /* The following fields are only used in the case of a chunk allocator */
  gasneti_mutex_t      lock;           /* locks access to chunk allocator freelist */
  int numchunks;                       /* number of chunks in buffer */
  int inuse;                           /* number of chunks currently in use */
  int hwm;                             /* High water mark of chunk use */
  gasnetc_chunk_t *freelist;           /* chunk freelist */
} gasnetc_PtlBuffer_t;

/* Thread local data
 * This is attached to the gasnetc_threaddata hook in gasnete_threaddata_t
 */
#define GASNETC_THREAD_HAVE_TMPMD   0x01U
#define GASNETC_THREAD_HAVE_RPLSB   0x02U
typedef struct _gasnetc_threaddata_t {
  /* (flags & GASNETC_THREAD_HAVE_TMPMD) => gasnetc_alloc_tmpmd counter
   *    has already been decremented so ok to alloc a tmpmd   */
  uint8_t flags;

  /* can cache up to two send tickets, may need two for AM Long Reply */
  uint8_t snd_tickets;

  /* this is set when sending a non-async amlong request.  Issuing thread will
   * poll on this variable until cleared.  Thread processing the SEND_END event
   * will decrement the count.  Issuing thread ID must be sent in match_bits.
   * Each thread allowed to issue one non-async amlong at a time.  */
  gasneti_weakatomic_t amlong_data_inflight;

  /* When (flags & GASNETC_THREAD_HAVE_RPLSB)
   * rplsb_off contains offset of cached request send buffer  */
  ptl_size_t rplsb_off;
} gasnetc_threaddata_t;

/* configurable sizes for Portals buffers */
extern int gasnetc_ReqRB_pool_size;
extern size_t gasnetc_ReqRB_numchunk;         /* Number of chunks in each ReqRB */
extern size_t gasnetc_ReqSB_numchunk;         /* Number of chunks to alloc for ReqSB */
extern size_t gasnetc_RplSB_numchunk;         /* Number of chunks to alloc for RplSB */

extern gasnetc_PtlBuffer_t gasnetc_ReqSB;
extern gasnetc_PtlBuffer_t gasnetc_RplSB;    /* MLW: Can elim this, and alloc a per-thread buffer and MD
					      * No need for an EQ since will only use it to send */
extern gasnetc_PtlBuffer_t *gasnetc_ReqRB;   /* an array of buffers */
extern gasnetc_PtlBuffer_t gasnetc_RAR;
extern gasnetc_PtlBuffer_t gasnetc_RARAM;
extern gasnetc_PtlBuffer_t gasnetc_RARSRC;
extern gasnetc_PtlBuffer_t gasnetc_CB;

/* handles to Portals network interface, memory descriptors and event queues */
extern ptl_handle_ni_t gasnetc_ni_h;              /* the network interface handle */
extern ptl_handle_eq_t gasnetc_AM_EQ_h;           /* Handle to the AM Event Queue */
extern ptl_handle_eq_t gasnetc_SAFE_EQ_h;         /* Handle to the SAFE Event Queue */

/* out of band MDs for sending system messages */
extern gasnetc_PtlBuffer_t gasnetc_SYS_Send;       /* out-of-band message send buffer */
extern gasnetc_PtlBuffer_t gasnetc_SYS_Recv;       /* out-of-band message recv buffer */
extern ptl_handle_eq_t gasnetc_SYS_EQ_h;           /* out-of-band system Event Queue */
extern int gasnetc_shutdown_seconds;               /* number of seconds to poll before forceful shutdown */
extern int gasnetc_shutdownInProgress;             /* set upon entry to gasnetc_exit */
typedef enum{GASNETC_SYS_SHUTDOWN_REQUEST=0,
	     GASNETC_SYS_BARRIER_ARRIVE,
	     GASNETC_SYS_BARRIER_GO,
	     GASNETC_SYS_NUM} gasnetc_sys_t;
static gasneti_weakatomic_t sys_barrier_cnt;
static gasneti_weakatomic_t sys_barrier_got;
static gasneti_weakatomic_t sys_barrier_checkin;

#if GASNETC_USE_SANDIA_ACCEL
/* did we get a signal, and if so, what signal number */
extern gasneti_weakatomic_t gasnetc_got_signum;
#endif

/* max packed am data field = 1024 - 15*4 - 8  (max of 15 args + 8 bytes for destaddr, no pad) */
#define GASNETC_MAX_AMLONG_PACKED 956


/* Vars that limit total number of Portals operations in flight at any time
 * originating from this node.
 * Performance decreases when too many messages are inflight so we try to
 * limit the number we initiate.
 * The send_tickets counter represents the number of tickets available.
 * It is initialized to be the msg_limit and decremented each time a send_ticket
 * is allocated.  It is incremented each time an operation completes.
 * NOTE: gasnetc_msg_limit == 0 means there is no limit
 */
extern gasneti_semaphore_t gasnetc_send_tickets;
extern int gasnetc_msg_limit;


/* prototype for gasnet handler functions */
typedef void (*gasnetc_handler_fn_t)();
extern gasnetc_handler_fn_t gasnetc_handler[]; /* the handler table */

/* Functions we export to the core and extended API */
extern int gasnetc_chunk_alloc(gasnetc_PtlBuffer_t *buf, size_t nbytes, ptl_size_t *offset);
extern int gasnetc_chunk_alloc_withpoll(gasnetc_PtlBuffer_t *buf, size_t nbytes, ptl_size_t *offset,
					int pollcnt, gasnetc_pollflag_t poll_type);
extern void gasnetc_chunk_free(gasnetc_PtlBuffer_t *buf, ptl_size_t offset);
extern ptl_handle_md_t gasnetc_alloc_tmpmd(void* dest, size_t nbytes, ptl_handle_eq_t eq_h);
extern void gasnetc_free_tmpmd(ptl_handle_md_t md_h);
extern void gasnetc_init_portals_network(void);
extern uintptr_t gasnetc_portalsMaxPinMem(void);
extern void gasnetc_bootstrapBarrier(void);
extern void gasnetc_bootstrapBroadcast(void *src, size_t len, void *dest, int rootnode);
extern void gasnetc_bootstrapExchange(void *src, size_t len, void *dest);
extern void gasnetc_init_portals_resources(void);
extern void gasnetc_portals_preexit(int do_trace);
extern void gasnetc_portals_exit();
extern void gasnetc_portals_poll(gasnetc_pollflag_t poll_type);
extern void gasnetc_event_handler(ptl_event_t *ev);
extern void gasnetc_ptl_trace_finish(void);
extern gasnet_node_t gasnetc_get_nodeid(ptl_process_id_t *proc);
extern void gasnetc_amlong_datasend(int sync, int isReq, uint32_t lid, gasnet_node_t dest,
				    void *src_addr, size_t nbytes, void* dest_addr);
extern void gasnetc_getmsg(void *dest, gasnet_node_t node, void *src, size_t nbytes,
			   ptl_match_bits_t match_bits, gasnetc_pollflag_t pollflag);
extern void gasnetc_putmsg(void *dest, gasnet_node_t node, void *src, size_t nbytes,
			   ptl_match_bits_t match_bits, int is_bulk, int *wait_lcc,
			   gasneti_weakatomic_t *lcc, gasnetc_pollflag_t pollflag);
extern void gasnetc_sys_SendMsg(gasnet_node_t node, gasnetc_sys_t msg_id,
				int32_t arg0, int32_t arg1, int32_t arg2);
extern void gasnetc_sys_barrier(void);
/* need a special signal handler for Portals */
extern void gasnetc_portalsSignalHandler(int sig);

/* Inline Function Definitions */
GASNETI_INLINE(gasnete_set_mbits_lowbits)
void gasnete_set_mbits_lowbits(ptl_match_bits_t *mbits, uint8_t msg_type, gasnete_op_t *op)
{
  /* The format is 0x00000000TTAAAAMM
   * Where TT   = threadid with eop/iop encoding in most significant bit
   *       AAAA = EOP/IOP opaddr bits
   *       MM   = message type and match bits
   */
    uint32_t th_b   = ( ((uint32_t)op->threadidx) << 24)    & 0xFF000000;
    uint32_t addr_b = ( ((uint32_t)op->addr.fulladdr) << 8) & 0x00FFFF00;
    uint32_t m_b    = ( (uint32_t)msg_type )                & 0x000000FF;
    *mbits = (GASNETC_SELECT_UPPER32 & *mbits) | (GASNETC_SELECT_LOWER32 & (ptl_match_bits_t)(th_b | addr_b | m_b));
    GASNETI_TRACE_PRINTF(C,("set lowbits th = 0x%x, addr = 0x%x, type = 0x%x, bits = 0x%llx",th_b,addr_b,m_b,*mbits));
}

GASNETI_INLINE(gasnete_get_op_lowbits)
void gasnete_get_op_lowbits(ptl_match_bits_t mbits, uint8_t *threadid, gasnete_opaddr_t *addr)
			    
{
    uint32_t lb    = (uint32_t)(GASNETC_SELECT_LOWER32 & mbits);
    *threadid      = (uint8_t)(lb >> 24);
    addr->fulladdr = (uint16_t)((lb & 0x00FFFF00) >> 8);
    GASNETI_TRACE_PRINTF(C,("get bits = 0x%lx, th = 0x%x, addr = 0x%x",(unsigned long)mbits,*threadid,addr->fulladdr));
}

GASNETI_INLINE(gasnetc_in_local_rar)
int gasnetc_in_local_rar(uint8_t* pstart, size_t n)
{
  uint8_t *pend  = pstart + n;
  uint8_t *start = (uint8_t*)gasneti_seginfo[gasneti_mynode].addr;
  uint8_t *end   = start + gasneti_seginfo[gasneti_mynode].size;

  return (pstart >= start) && (pend <= end);
}

GASNETI_INLINE(gasnetc_try_alloc_tmpmd)
int gasnetc_try_alloc_tmpmd(void* start, size_t nbytes, ptl_handle_eq_t eq_h, ptl_handle_md_t *md_h)
{
  if (gasnetc_alloc_ticket(&gasnetc_tmpmd_tickets)) {
    *md_h = gasnetc_alloc_tmpmd(start,nbytes,eq_h);
    return 1;
  }
  return 0;
}

GASNETI_INLINE(gasnetc_alloc_tmpmd_withpoll)
ptl_handle_md_t gasnetc_alloc_tmpmd_withpoll(void* start, size_t nbytes, ptl_handle_eq_t eq_h)
{
  while (! gasnetc_alloc_ticket(&gasnetc_tmpmd_tickets)) {
    gasnetc_portals_poll(GASNETC_SAFE_POLL);
  }
  return gasnetc_alloc_tmpmd(start, nbytes, eq_h);
}

GASNETI_INLINE(gasnetc_get_event)
int gasnetc_get_event(ptl_handle_eq_t eq_h, ptl_event_t *ev)
{
  int rc;
  int retcode = 0;

  rc = PtlEQGet( eq_h, ev);
  switch (rc) {
  case PTL_OK:
    retcode = 1;
    break;
  case PTL_EQ_EMPTY:
    break;
  default:
    gasneti_fatalerror("gasnetc_get_event Portals Error in PtlEQGet: %s (%i)\n at %s\n",
		       ptl_err_str[rc],rc,gasneti_current_loc);
    break;
  }

  return retcode;
}


GASNETI_INLINE(gasnetc_sys_poll)
void gasnetc_sys_poll()
{
  ptl_event_t ev;
  /* always check for receipt of signal before polling since we do not protect get_event
   * with _SAFE wrapper */
  GASNETC_CHECKSIG();
  while (gasnetc_get_event(gasnetc_SYS_EQ_h, &ev)) {
    GASNETI_TRACE_PRINTF(C,("Got event %s from SYS_EQ, md=%lu, mbits=0x%lx",ptl_event_str[ev.type],(ulong)ev.md_handle,(unsigned long)ev.match_bits));
    GASNETC_CALL_EQ_HANDLER(ev);
  }
}

GASNETI_INLINE(gasnetc_mythread)
gasnetc_threaddata_t *gasnetc_mythread(void)
{
  gasnete_threaddata_t *th = gasnete_mythread();
  return th->gasnetc_threaddata;
}

GASNETI_INLINE(gasnetc_new_threaddata)
gasnetc_threaddata_t* gasnetc_new_threaddata(void)
{
  gasnetc_threaddata_t *th = (gasnetc_threaddata_t*)gasneti_malloc(sizeof(gasnetc_threaddata_t));
  gasneti_assert_always(th);
  th->flags = 0;
  th->snd_tickets = 0;
  gasneti_weakatomic_set(&th->amlong_data_inflight, 0, 0);
  return th;
}

/* ---------------------------------------------------------------------------------
 * Allocate a new LID = "Long ID" for a new AMLong Request or Reply operation
 * --------------------------------------------------------------------------------- */
GASNETI_INLINE(gasnetc_new_lid)
uint32_t gasnetc_new_lid(gasnet_node_t dest)
{
  /* use _add rather than _incr since it returns the new value */
  return gasneti_weakatomic_add(&gasnetc_conn_state[dest].src_lid,1,0);
}

#endif

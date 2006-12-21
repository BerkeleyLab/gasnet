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

/* Needed for bootstrap */
#include <catamount/cnos_mpi_os.h>

/* ------------------------------------------------------------------------------------ */
/* MLW:  Support for Portals 3.0 */

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
#define GASNETC_PTL_REQRB_BITS   0x03
#define GASNETC_PTL_CB_BITS      0x03
#define GASNETC_PTL_REQSB_BITS   0x02

/* Operation type */
#define GASNETC_PTL_MSG_PUT      0x10
#define GASNETC_PTL_MSG_GET      0x20
#define GASNETC_PTL_MSG_AM       0x40
#define GASNETC_PTL_MSG_AMDATA   0x80
/* DOLC - Do Local Completion flag.  Indicates to event handler that it must
 * decrement the local completion counter for this gasnet operation
 */
#define GASNETC_PTL_MSG_DOLC     0x80

#define GASNETC_PTL_AM_SHORT     0x01
#define GASNETC_PTL_AM_MEDIUM    0x02
#define GASNETC_PTL_AM_LONG      0x04
#define GASNETC_PTL_AM_REQUEST   0x08
#define GASNETC_PTL_AM_PACKED    0x10

/* Token flag values */
#define GASNETC_PTL_REPLY_SENT   0x01

/* Masks used in constructing 64-bit bit fields */
#define GASNETC_MASK_UPPER32     0xFFFFFFFF00000000ULL
#define GASNETC_MASK_LOWER32     0x00000000FFFFFFFFULL
#define GASNETC_MASK_OPBITS      0x00000000FFFFFF00ULL
#define GASNETC_MASK_BYTE0       0x00000000000000FFULL
#define GASNETC_MASK_BYTE1       0x000000000000FF00ULL
#define GASNETC_MASK_BYTE2       0x0000000000FF0000ULL
#define GASNETC_MASK_BYTE3       0x00000000FF000000ULL
#define GASNETC_MASK_BYTE4       0x000000FF00000000ULL
#define GASNETC_MASK_BYTE5       0x0000FF0000000000ULL
#define GASNETC_MASK_BYTE6       0x00FF000000000000ULL
#define GASNETC_MASK_BYTE7       0xFF00000000000000ULL

/* x is a uint64_t and a and b are int32_t */
#define GASNETC_PACK_UPPER(lhs,rhs) lhs = ((lhs) & GASNETC_MASK_LOWER32) | ((uint64_t)(rhs) << 32)
#define GASNETC_PACK_LOWER(lhs,rhs) lhs = ((lhs) & GASNETC_MASK_UPPER32) | ((uint64_t)(rhs) & GASNETC_MASK_LOWER32)
#define GASNETC_PACK_2INT(x,up,low) x = ((uint64_t)(up)<<32) | ((uint64_t)(low) & GASNETC_MASK_LOWER32)

#define GASNETC_UNPACK_UPPER(x) (int32_t)((x)>>32)
#define GASNETC_UNPACK_LOWER(x) (int32_t)((x)&GASNETC_MASK_LOWER32)
#define GASNETC_UNPACK_2INT(x,up,low) do { \
    (up) = GASNETC_UNPACK_UPPER(x);	   \
    (low) = GASNETC_UNPACK_LOWER(x);	   \
  } while (0)

#define GASNETC_COMPUTE_DOUBLE_PAD(n,pad) do { \
    int p = (n) % sizeof(double);  \
    pad = (p == 0 ? 0 : sizeof(double)-p);  \
  } while(0)

/* AM tokens used by portals */
typedef struct token_rec {
  uint8_t           flags;
  uint32_t          rplsb_offset;
  uint32_t          initiator_offset;
  ptl_process_id_t  initiator;
  gasnet_node_t     srcnode;
} gasnetc_ptl_token_t;

#define GASNETC_LID_DATA_HERE    0x1;
#define GASNETC_LID_HEADER_HERE  0x2;
/* data cached by Long Put or AM Long Header */
typedef struct gasnetc_amlongcache_rec {
  uint8_t             flags;
  gasnet_handler_t    ghandler;
  uint32_t            dest_lid;
  uint32_t            initiator_offset;
  uint32_t            narg;
  struct gasnetc_amlongcache_rec *next;
  void               *data;
  ptl_size_t          datalen;
  gasnet_handlerarg_t args[];
} gasnetc_amlongcache_t;

/* gasnet state used for AM send squelch */
typedef struct gconrec {
  gasneti_weakatomic_t AM_pending;
  gasneti_weakatomic_t in_recovery;
  uint32_t   src_lid;  /* a counter that is incremented for each AM Long issued */
  gasnetc_amlongcache_t *lids;
} gasnetc_conn_t;
/* array of connection states */
extern gasnetc_conn_t *gasnetc_conn_state;

/* Flag to determine if we use Portals or MPI for AMs */
extern int gasnetc_use_AM_portals;

#define GASNETC_PTLSAFE(fncall) do {                                         \
   int _retcode = (fncall);                                                  \
   if_pf (_retcode != (int)PTL_OK) {                                         \
     gasneti_fatalerror("\nGASNet Portals encountered an error: %s (%i)\n"   \
        "  while calling: %s\n"                                              \
        "  at %s",                                                           \
        ptl_err_str[_retcode], _retcode, #fncall, gasneti_current_loc); \
   }                                                                         \
 } while (0)

/* Types of GASNET Portals Memory Descriptors */
enum { GASNETC_RAR_MD,
       GASNETC_RARAM_MD,
       GASNETC_REQSB_MD,
       GASNETC_REQRB_MD,
       GASNETC_RPLSB_MD,
       GASNETC_CB_MD,
       GASNETC_TMP_MD,
       GASNETC_NUM_MD
};

/* an array of MD names for diagnostics */
extern const char* gasnetc_md_name[];

/* pointer to event handler functions */
typedef void (*gasnetc_ptl_event_handler)(ptl_event_t *ev);

/* Number of temporary Portals MDs in use at any time */
#define GASNETC_MAX_TMP_MDS 1024
extern int gasnetc_max_tmpmd;
gasneti_weakatomic_t gasnetc_tmpmd_count;
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

extern ptl_process_id_t  gasnetc_myid;
extern ptl_uid_t         gasnetc_uid;
extern ptl_procid_t     *gasnetc_procid_map;

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

/* The RAR, RARAM, and the AM request/reply send/receive buffers are described by */
typedef struct {
  size_t alignment;                    /* alignment (power of 2) */
  size_t nbytes;                       /* number of bytes in buffer after alignment */
  void*  actual_start;                 /* returned by allocator */
  void*  start;                        /* aligned start */
  ptl_handle_md_t  md_h;               /* The Portals memory descriptor handler */
  ptl_handle_me_t  me_h;               /* The Portals match-list entry handle (if used) */
  const char *name;                    /* string used for diagnostics */
  int use_chunks;                      /* Is the buffer under control of a chunk allocator? */

  /* The following fields are only used in the case of a chunk allocator */
  int numchunks;                       /* number of chunks in buffer */
  int inuse;                           /* number of chunks currently in use */
  int hwm;                             /* High water mark of chunk use */
  gasnetc_chunk_t *freelist;           /* chunk freelist */
} gasnetc_PtlBuffer_t;

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
extern gasnetc_PtlBuffer_t gasnetc_CB;

/* handles to Portals network interface, memory descriptors and event queues */
extern ptl_handle_ni_t gasnetc_ni_h;              /* the network interface handle */
extern ptl_handle_eq_t gasnetc_EQ_h;              /* Handle to the combined Event Queue */

#define GASNETC_MAX_POLL_EVENTS 40
extern int gasnetc_max_poll_events;

/* Vars that insure polling progress when issuing large numbers of Put/Get
 * operations without intermediate sync calls.
 *    putget_poll_cnt = number of put/get calls since last poll
 *    putget_poll     = number of allowed put/get calls between poll calls
 */
extern int gasnete_putget_poll;
extern gasneti_weakatomic_t gasnete_putget_poll_cnt;

/* Var used for sync operation in AMLong Request and AMLong Reply.
 * The call must not return until the data payload can be modified
 * by the client.  The AMLong Request or Reply will set this var
 * and the event handler will unset it when the SEND_END event for
 * the data message is processed.
 * NOTE: need this to be thread specific?
 */
extern gasneti_weakatomic_t gasnetc_amlongReq_datacnt;

/* Vars that limit total number of put/get operations in flight at any time
 * originating from this node.
 * We allocate enough events to handle the limit, assuing 2 events per put/get.
 *    putget_inflight = number of put/get operations that are outstanding
 *    putget_limit    = number of allowed put/get operations allowed at any one time
 */
extern gasneti_weakatomic_t gasnete_putget_inflight;
extern int gasnete_putget_limit;

#define GASNETC_COMMON_AMSTART(state,offset) do {	  \
    /* poll until dest node is out of recovery */ \
    gasneti_pollwhile( gasneti_weakatomic_read(&((state)->in_recovery), 0) ); \
    /* poll until local node has enough resources to send an AM */ \
    /* MLW: INSERT PROPER CODE HERE */ \
    /* Allocate a send buffer (Note that chunk_alloc will poll internally) */ \
    while (gasnetc_chunk_alloc(&gasnetc_ReqSB, GASNETC_CHUNKSIZE, &(offset)) == 0) {}; \
  } while (0)

#define GASNETC_PACK_AM_MBITS(mbits, offset, amtype, narg, hndlr, targ_mbits) \
    (mbits) = ((uint64_t)(offset) << 32) | ((uint64_t)(amtype) << 24)	\
      | ( ((uint64_t)(narg) & GASNETC_MASK_BYTE0) << 16) | ((uint64_t)(hndlr) << 8) \
      | (targ_mbits)

#define PACK_SHORT_MED_HDR(hdr,srcnode,arg0) \
  (hdr) = ((uint64_t)(srcnode) << 32) | ((uint64_t)(arg0) & GASNETC_MASK_LOWER32)

#define UNPACK_SHORT_MED_HDR(hdr,srcnode,arg0) do { \
    srcnode = (uint32_t)((uint64_t)(hdr) >> 32); \
    arg0 = (int32_t)((uint64_t)(hdr) & GASNETC_MACK_LOWER32); \
    while(0)

  (hdr) = ((uint64_t)(srcnode) << 32) | ((uint64_t)(arg0) & GASNETC_MASK_LOWER32)

#define GASNETC_GET_MSG_TYPE(mbits) ((mbits) & 0xF0)
#define GASNETC_SET_MSG_TYPE(mbits,mtyp) (((mbits) & 0xFFFFFFFFFFFFFF0F) | ((mtyp) & 0xF0))

#define GASNETE_PTL_MBITS_ENCODE_HANDLE(mbits,op) \
    do { mbits |= ((0xFF & op->threadidx) << 24) | ((op->addr.fulladdr & 0xFFFF)<<8); } while (0)

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
    *mbits = (GASNETC_MASK_UPPER32 & *mbits) | (GASNETC_MASK_LOWER32 & (ptl_match_bits_t)(th_b | addr_b | m_b));
    GASNETI_TRACE_PRINTF(C,("set lowbits th = 0x%x, addr = 0x%x, type = 0x%x, bits = 0x%llx",th_b,addr_b,m_b,*mbits));
}

GASNETI_INLINE(gasnetc_get_am_lowbits)
void gasnetc_get_am_lowbits(ptl_match_bits_t mbits, uint8_t *amflag, uint8_t *numarg, uint8_t *ghndlr)
{
    uint32_t lb    = (uint32_t)(GASNETC_MASK_LOWER32 & mbits);
    *amflag        = (uint8_t)(lb >> 24);
    *numarg        = (uint8_t)((lb & 0x00FF0000) >> 16);
    *ghndlr        = (uint8_t)((lb & 0x0000FF00) >> 8);
    GASNETI_TRACE_PRINTF(C,("get am bits=0x%lx, flag=0x%x, numarg=0x%x, hndlr=0x%x",(unsigned long)mbits,*amflag,*numarg,*ghndlr));
}

GASNETI_INLINE(gasnete_get_op_lowbits)
void gasnete_get_op_lowbits(ptl_match_bits_t mbits, uint8_t *threadid, gasnete_opaddr_t *addr)
			    
{
    uint32_t lb    = (uint32_t)(GASNETC_MASK_LOWER32 & mbits);
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
#define GASNETC_PTL_OFFSET(n,s) ((uint8_t*)(s) - (uint8_t*)gasneti_seginfo[n].addr)

/* prototype for gasnet handler functions */
typedef void (*gasnetc_handler_fn_t)();
extern gasnetc_handler_fn_t gasnetc_handler[]; /* the handler table */

/* Functions we export to the core and extended API */
/* MLW: some of these may not have to be exported */
extern int gasnetc_chunk_alloc(gasnetc_PtlBuffer_t *buf, size_t nbytes, ptl_size_t *offset);
extern void gasnetc_chunk_free(gasnetc_PtlBuffer_t *buf, ptl_size_t offset);
extern ptl_handle_md_t gasnetc_alloc_tmpmd(void* dest, size_t nbytes, ptl_handle_eq_t eq_h);
extern void gasnetc_free_tmpmd(ptl_handle_md_t md_h);
extern void gasnetc_init_portals_network(void);
extern uintptr_t gasnetc_portalsMaxPinMem(void);
extern void gasnetc_bootstrapBarrier(void);
extern void gasnetc_bootstrapBroadcast(void *src, size_t len, void *dest, int rootnode);
extern void gasnetc_bootstrapExchange(void *src, size_t len, void *dest);
extern void gasnetc_init_portals_resources(void);
extern void gasnetc_portals_exit();
extern void gasnetc_portals_poll(void);
extern void gasnetc_event_handler(ptl_event_t *ev);
extern void gasnetc_ptl_trace_finish(void);
extern void gasnetc_testBootExch(void);
extern gasnet_node_t gasnetc_get_nodeid(ptl_process_id_t *proc);
extern int gasnetc_get_event(ptl_handle_eq_t eq_h, ptl_event_t *ev);
extern void gasnetc_amlong_datasend(int sync, uint32_t lid, gasnet_node_t dest, void *src_addr,
				    size_t nbytes, void* dest_addr);
extern uint32_t gasnetc_new_lid(gasnet_node_t dest);

SHORT_HANDLER_DECL(gasnetc_AMNoop,0,0);
#endif

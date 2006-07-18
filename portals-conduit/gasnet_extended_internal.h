/*   $Source: /Users/kamil/work/gasnet-cvs2/gasnet/portals-conduit/Attic/gasnet_extended_internal.h,v $
 *     $Date: 2006/07/18 02:04:32 $
 * $Revision: 1.1.2.4 $
 * Description: GASNet header for internal definitions in Extended API
 * Copyright 2002, Dan Bonachea <bonachea@cs.berkeley.edu>
 * Terms of use are as specified in license.txt
 */

#ifndef _GASNET_EXTENDED_INTERNAL_H
#define _GASNET_EXTENDED_INTERNAL_H

#include <gasnet_internal.h>
#include <gasnet_handler.h>
#include <portals/portals3.h>

/* ------------------------------------------------------------------------------------ */
typedef uint8_t gasnete_threadidx_t;


/* for compactness, eops and iops address each other in the free list using a gasnete_opaddr_t */ 
typedef union _gasnete_opaddr_t {
  struct {
    uint8_t _bufferidx;
    uint8_t _opidx;
  } compaddr;
  uint16_t fulladdr;
} gasnete_opaddr_t;
#define bufferidx compaddr._bufferidx
#define opidx compaddr._opidx

/* gasnet_handle_t is a void* pointer to a gasnete_op_t, 
 * which is either a gasnete_eop_t or an gasnete_iop_t
 */
typedef struct _gasnete_op_t {
  uint8_t flags;                  /*  flags - type tag */
  gasnete_threadidx_t threadidx;  /*  Leading bit indicate eop/iop, 7 lower bits are thread that owns me */
  gasnete_opaddr_t addr;          /*  next cell while in free list, my own opaddr_t while in use */
} gasnete_op_t;

#define gasnete_opaddr_equal(addr1,addr2) ((addr1).fulladdr == (addr2).fulladdr)
#define gasnete_opaddr_isnil(addr) ((addr).fulladdr == OPADDR_NIL.fulladdr)

typedef struct _gasnete_eop_t {
  uint8_t flags;                  /*  state flags */
  gasnete_threadidx_t threadidx;  /*  thread that owns me */
  gasnete_opaddr_t addr;          /*  next cell while in free list, my own opaddr_t while in use */
} gasnete_eop_t;

typedef struct _gasnete_iop_t {
  uint8_t flags;                  /*  state flags */
  gasnete_threadidx_t threadidx;  /*  thread that owns me */
  gasnete_opaddr_t addr;          /*  my own opaddr_t for use as compact pointer */
  struct _gasnete_iop_t *next;    /*  used for free list and iop stack */
  int initiated_get_cnt;     /*  count of get ops initiated */
  int initiated_put_cnt;     /*  count of put ops initiated */

  /*  make sure the counters live on different cache lines for SMP's */
#if 0
  /*  MLW: Need to adjust this pad field length */
  uint8_t pad[MAX(8,(ssize_t)(GASNETI_CACHE_LINE_BYTES - sizeof(void*) - sizeof(int)))]; 
#endif
  gasneti_weakatomic_t completed_get_cnt;     /*  count of get ops completed */
  gasneti_weakatomic_t completed_put_cnt;     /*  count of put ops completed */
} gasnete_iop_t;

/* ------------------------------------------------------------------------------------ */
typedef struct _gasnete_threaddata_t {
  void *gasnetc_threaddata;     /* pointer reserved for use by the core */
  void *gasnete_coll_threaddata;/* pointer reserved for use by the collectives */
  void *gasnete_vis_threaddata; /* pointer reserved for use by the VIS implementation */

  gasnete_threadidx_t threadidx;

  gasnete_eop_t *eop_bufs[256]; /*  buffers of eops for memory management */
  int eop_num_bufs;             /*  number of valid eop buffer entries */
  gasnete_opaddr_t eop_free;    /*  free list of eops */

  gasnete_iop_t *iop_bufs[256]; /*  buffers of iops for memory management */
  int iop_num_bufs;             /*  number of valid iop buffer entries (generally just one) */
  gasnete_iop_t *iop_free;      /*  free list of iops */

  /*  stack of iops - head is active iop servicing new implicit ops */
  gasnete_iop_t *current_iop;  

  /* counter used by non-blocking non-bulk put ops to delay returning to caller before local
   * completion.  Initialized by calling thread, incremented and read by gasnete_put_nb*
   * operations (in some cases) decremented by event handler functions.
   * Since these will not execute in a signal handler, they are weakatomic.
   */
  gasneti_weakatomic_t local_completion_count;

  struct _gasnet_valget_op_t *valget_free; /* free list of valget cells */
} gasnete_threaddata_t;
/* ------------------------------------------------------------------------------------ */

/* gasnete_op_t flags field */
#define OPTYPE_EXPLICIT               0x00  /*  gasnete_eop_new() relies on this value */
#define OPTYPE_IMPLICIT               0x80
#define OPTYPE(op) ((op)->threadidx & 0x80)
GASNETI_INLINE(SET_OPTYPE)
void SET_OPTYPE(gasnete_op_t *op, uint8_t type) {
  op->threadidx = (op->threadidx & 0x7F) | (type & 0x80);
}
GASNETI_INLINE(SET_THREADID)
void SET_THREADID(gasnete_op_t *op, uint8_t threadid) {
  op->threadidx = (threadid & 0x7F) | (op->threadidx & 0x80);
}
GASNETI_INLINE(SET_EOP_THREADID)
void SET_EOP_THREADID(gasnete_eop_t *eop, uint8_t threadid) {
  eop->threadidx = (threadid & 0x7F) | OPTYPE_EXPLICIT;
}
GASNETI_INLINE(SET_IOP_THREADID)
void SET_IOP_THREADID(gasnete_iop_t *iop, uint8_t threadid) {
  iop->threadidx = (threadid & 0x7F) | OPTYPE_IMPLICIT;
}

#define GASNETE_THREADID(th) ((th) & 0x7F)
#define GASNETE_OP_THREADID(op) GASNETE_THREADID((op)->threadidx)
#define GASNETE_IS_EOP(op) (((op)->threadidx & 0x80) == OPTYPE_EXPLICIT)
#define GASNETE_IS_IOP(op) (((op)->threadidx & 0x80) == OPTYPE_IMPLICIT)

/*  state - only valid for explicit ops */
/*  MLW: iops also use state, but only OPSTATE_FREE or OPSTATE_INFLIGHT */
#define OPSTATE_FREE      0   /*  gasnete_eop_new() relies on this value */
#define OPSTATE_INFLIGHT  1
#define OPSTATE_COMPLETE  2
#define OPSTATE(op) ((op)->flags & 0x03) 
GASNETI_INLINE(SET_OPSTATE)
void SET_OPSTATE(gasnete_op_t *op, uint8_t state) {
  op->flags = (op->flags & 0xFC) | (state & 0x03);
  /* RACE: If we are marking the op COMPLETE, don't assert for completion
   * state as another thread spinning on the op may already have changed
   * the state. */
  gasneti_assert(state == OPSTATE_COMPLETE ? 1 : OPSTATE(op) == state);
}

/*  get a new op and mark it in flight */
gasnete_eop_t *gasnete_eop_new(gasnete_threaddata_t *thread);
gasnete_iop_t *gasnete_iop_new(gasnete_threaddata_t *thread);
/*  query an eop for completeness */
int gasnete_op_isdone(gasnete_op_t *op);
/*  mark an op done - isget ignored for explicit ops */
void gasnete_op_markdone(gasnete_op_t *op, int isget);
/*  free an op */
void gasnete_op_free(gasnete_op_t *op);


#define GASNETE_EOPADDR_TO_PTR(threaddata, opaddr)                      \
      (gasneti_memcheck(threaddata),                                    \
       gasneti_assert(!gasnete_opaddr_isnil(opaddr)),                   \
       gasneti_assert((opaddr).bufferidx < (threaddata)->eop_num_bufs), \
       gasneti_memcheck((threaddata)->eop_bufs[(opaddr).bufferidx]),    \
       (threaddata)->eop_bufs[(opaddr).bufferidx] + (opaddr).opidx)

#define GASNETE_IOPADDR_TO_PTR(threaddata, opaddr)                      \
      (gasneti_memcheck(threaddata),                                    \
       gasneti_assert(!gasnete_opaddr_isnil(opaddr)),                   \
       gasneti_assert((opaddr).bufferidx < (threaddata)->iop_num_bufs), \
       gasneti_memcheck((threaddata)->iop_bufs[(opaddr).bufferidx]),    \
       (threaddata)->iop_bufs[(opaddr).bufferidx] + (opaddr).opidx)


#if GASNET_DEBUG
  /* check an in-flight/complete eop */
  #define gasnete_eop_check(eop) do {                                \
    gasnete_threaddata_t * _th;                                      \
    gasneti_assert(OPTYPE(eop) == OPTYPE_EXPLICIT);                  \
    gasneti_assert(OPSTATE(eop) == OPSTATE_INFLIGHT ||               \
                   OPSTATE(eop) == OPSTATE_COMPLETE);                \
    _th = gasnete_threadtable[GASNETE_OP_THREADID(eop)];                     \
    gasneti_assert(GASNETE_EOPADDR_TO_PTR(_th, (eop)->addr) == eop); \
  } while (0)
  #define gasnete_iop_check(iop) do {                         \
    int _temp;                                                \
    gasneti_assert(OPTYPE(iop) == OPTYPE_IMPLICIT);           \
    gasneti_assert(OPSTATE(iop) == OPSTATE_INFLIGHT);         \
    gasneti_assert(GASNETE_OP_THREADID(iop) < gasnete_numthreads);    \
    gasneti_memcheck(gasnete_threadtable[GASNETE_OP_THREADID(iop)]);  \
    _temp = gasneti_weakatomic_read(&((iop)->completed_put_cnt), 0); \
    if (_temp <= 65000) /* prevent race condition on reset */ \
      gasneti_assert((iop)->initiated_put_cnt >= _temp);      \
    _temp = gasneti_weakatomic_read(&((iop)->completed_get_cnt), 0); \
    if (_temp <= 65000) /* prevent race condition on reset */ \
      gasneti_assert((iop)->initiated_get_cnt >= _temp);      \
  } while (0)
  extern void _gasnete_iop_check(gasnete_iop_t *iop);
#else
  #define gasnete_eop_check(eop)   ((void)0)
  #define gasnete_iop_check(iop)   ((void)0)
#endif

/*  1 = scatter newly allocated eops across cache lines to reduce false sharing */
#define GASNETE_SCATTER_EOPS_ACROSS_CACHELINES    1 

/* ------------------------------------------------------------------------------------ */
/* MLW:  Support for Portals 3.0 */

/* Max transfer size SHOULD be defined by portals, but apparently is not */
#ifdef PTL_MAX_TRANS_SZ
#define GASNETE_PTL_MAX_TRANS_SZ PTL_MAX_TRANS_SZ
#else
#define GASNETE_PTL_MAX_TRANS_SZ 2147483648UL
#endif

/* Types of GASNET Portals Memory Descriptors */
#define GASNETE_RARAM_MD 0
#define GASNETE_BB_MD    1
#define GASNETE_TMP_MD   2

/* Portals Access table not implemented on XT3 */
#define GASNETE_PTL_AC_ID  0

/* We need Cray to reserve two table entries for UPC/GASNET
 * We believe these two are currently not used.
 */
#define GASNETE_PTL_RAR_PTE 38
#define GASNETE_PTL_AM_PTE 39

/* Values that are encoded in the MBITs of Portals Data Transfer ops */
#define GASNETE_PTL_IGNORE_BITS  0xFFFFFFFFFFFFFFF0
#define GASNETE_PTL_RAR_BITS     0x00
#define GASNETE_PTL_RARAM_BITS   0x01
#define GASNETE_PTL_REQRB_BITS   0x03
#define GASNETE_PTL_CB_BITS      0x03
#define GASNETE_PTL_REQSB_BITS   0x02
#define GASNETE_PTL_BB_BITS      0x02

/* Operation type */
#define GASNETE_PTL_MSG_PUT      0x10
#define GASNETE_PTL_MSG_GET      0x20
#define GASNETE_PTL_MSG_AM       0x40
/* DOLC - Do Local Completion flag.  Indicates to event handler that it must
 * decrement the local completion counter for this gasnet operation
 */
#define GASNETE_PTL_MSG_DOLC     0x80

#define GASNETE_MASK_UPPER32     0xFFFFFFFF00000000
#define GASNETE_MASK_LOWER32     0x00000000FFFFFFFF
#define GASNETE_MASK_OPBITS      0x00000000FFFFFF00
#define GASNETE_MASK_BYTE0       0x00000000000000FF
#define GASNETE_MASK_BYTE1       0x000000000000FF00
#define GASNETE_MASK_BYTE2       0x0000000000FF0000
#define GASNETE_MASK_BYTE3       0x00000000FF000000
#define GASNETE_MASK_BYTE4       0x000000FF00000000
#define GASNETE_MASK_BYTE5       0x0000FF0000000000
#define GASNETE_MASK_BYTE6       0x00FF000000000000
#define GASNETE_MASK_BYTE7       0xFF00000000000000

typedef union _gasnete_lowbits_t {
    struct {
	gasnete_threadidx_t threadidx;
	gasnete_opaddr_t    addr;
	uint8_t             msgtype;
    } lb_addr;
    uint32_t lowbits;
} gasnete_lowbits_t;

#define GASNETE_PTLSAFE(fncall) do {                                         \
   int _retcode = (fncall);                                                  \
   if_pf (_retcode != (int)PTL_OK) {                                         \
     gasneti_fatalerror("\nGASNet Portals encountered an error: %s (%i)\n"   \
        "  while calling: %s\n"                                              \
        "  at %s",                                                           \
        ptl_err_str[_retcode], _retcode, #fncall, gasneti_current_loc); \
   }                                                                         \
 } while (0)


/* */
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
#if 0
    gasnete_lowbits_t lb;
    lb.lb_addr.threadidx = op->threadidx;  /* contains encoding of eop/iop type */
    lb.lb_addr.addr.fulladdr = op->addr.fulladdr;
    lb.lb_addr.msgtype = msg_type;
    *mbits = (GASNETE_MASK_UPPER32 & *mbits) | (GASNETE_MASK_LOWER32 & lb.lowbits);
#else
    uint32_t th_b   = ( ((uint32_t)op->threadidx) << 24)    & 0xFF000000;
    uint32_t addr_b = ( ((uint32_t)op->addr.fulladdr) << 8) & 0x00FFFF00;
    uint32_t m_b    = ( (uint32_t)msg_type )                & 0x000000FF;
    *mbits = (GASNETE_MASK_UPPER32 & *mbits) | (GASNETE_MASK_LOWER32 & (ptl_match_bits_t)(th_b | addr_b | m_b));
    GASNETI_TRACE_PRINTF(C,("set lowbits th = 0x%x, addr = 0x%x, type = 0x%x, bits = 0x%llx",th_b,addr_b,m_b,*mbits));
#endif
}

GASNETI_INLINE(gasnete_get_mbits_lowbits)
void gasnete_get_mbits_lowbits(ptl_match_bits_t mbits, uint8_t *threadid,
			       uint8_t *msg_type, gasnete_opaddr_t *addr)
{
#if 0
    gasnete_lowbits_t lb;
    lb.lowbits = (uint32_t)(GASNETE_MASK_LOWER32 & mbits);
    *threadid = lb.lb_addr.threadidx;  /* contains encoding of eop/iop type */
    *msg_type = lb.lb_addr.msgtype;
    addr->fulladdr = lb.lb_addr.addr.fulladdr;
#else
    uint32_t lb = (uint32_t)(GASNETE_MASK_LOWER32 & mbits);
    *threadid = (uint8_t)(lb >> 24);
    addr->fulladdr = (uint16_t)((lb & 0x00FFFF00) >> 8);
    *msg_type = (uint8_t)(lb & 0x000000FF);
    GASNETI_TRACE_PRINTF(C,("get bits = 0x%lx, th = 0x%x, addr = 0x%x, type = 0x%x",(unsigned long)mbits,*threadid,addr->fulladdr,*msg_type));
#endif
}

extern ptl_process_id_t gasnete_ptl_nodeid(gasnet_node_t node);

/* -----------------------------------------------------------------------------------
 * A dumb chunk allocator used for put/get bounce buffer
 * WARNING: Not a thread-safe freelist implementation!!!
 * Will have to re-implement for multi-threaded (Linux) XT3
 */
#define GASNETE_BB_CHUNKSIZE 1024
#define GASNETE_BB_NUM_CHUNK 1024
extern ptl_handle_md_t gasnete_rar_md;
extern ptl_handle_md_t gasnete_bb_md;
typedef union _gasnete_bb_chunk {
    uint8_t chunk[GASNETE_BB_CHUNKSIZE];
    union _gasnete_bb_chunk *next;
} gasnete_bb_chunk_t;

extern gasnete_bb_chunk_t *gasnete_bb_freelist;
extern void* gasnete_bb_start;
extern int gasnete_bb_outstanding;
extern int gasnete_bb_hwm;
extern ptl_handle_md_t  gasnete_bb_md_h;
extern int gasnete_bb_chunk_alloc(size_t nbytes, ptl_size_t *offset);
extern void gasnete_bb_init(size_t nchunks);
extern void gasnete_bb_remove(void);

GASNETI_INLINE(gasnete_bb_chunk_free)
void gasnete_bb_chunk_free(ptl_size_t offset)
{
    gasnete_bb_chunk_t *p = (gasnete_bb_chunk_t*)((uint8_t*)gasnete_bb_start + offset);
    p->next = gasnete_bb_freelist;
    gasnete_bb_freelist = p;
    gasnete_bb_outstanding--;
    gasneti_assert(gasnete_bb_outstanding >= 0);
    GASNETI_TRACE_PRINTF(C,("BB_chunk_freed: outstanding = %d",gasnete_bb_outstanding));
}

extern void gasnete_portals_init(void);
extern ptl_handle_md_t gasnete_alloc_tmpmd(void* dest, size_t nbytes);
extern void gasnete_event_handler(ptl_event_t *ev);

#define GASNETE_PTL_OFFSET(n,s) ((uint8_t*)(s) - (uint8_t*)gasneti_seginfo[n].addr)

GASNETI_INLINE(gasnete_in_local_rar)
int gasnete_in_local_rar(uint8_t* pstart, size_t n)
{
  uint8_t *pend  = pstart + n;
  uint8_t *start = (uint8_t*)gasneti_seginfo[gasneti_mynode].addr;
  uint8_t *end   = start + gasneti_seginfo[gasneti_mynode].size;

  return (pstart >= start) && (pend <= end);
}

/* Number of temporary Portals MDs in use at any time */
#define GASNETE_MAX_TMP_MDS 1024


/* ------------------------------------------------------------------------------------ */

#define GASNETE_HANDLER_BASE  64 /* reserve 64-127 for the extended API */
#define _hidx_gasnete_amdbarrier_notify_reqh (GASNETE_HANDLER_BASE+0) 
#define _hidx_gasnete_amcbarrier_notify_reqh (GASNETE_HANDLER_BASE+1) 
#define _hidx_gasnete_amcbarrier_done_reqh   (GASNETE_HANDLER_BASE+2)
#define _hidx_gasnete_get_reqh               (GASNETE_HANDLER_BASE+3)
#define _hidx_gasnete_get_reph               (GASNETE_HANDLER_BASE+4)
#define _hidx_gasnete_getlong_reqh           (GASNETE_HANDLER_BASE+5)
#define _hidx_gasnete_getlong_reph           (GASNETE_HANDLER_BASE+6)
#define _hidx_gasnete_put_reqh               (GASNETE_HANDLER_BASE+7)
#define _hidx_gasnete_putlong_reqh           (GASNETE_HANDLER_BASE+8)
#define _hidx_gasnete_memset_reqh            (GASNETE_HANDLER_BASE+9)
#define _hidx_gasnete_markdone_reph          (GASNETE_HANDLER_BASE+10)
/* add new extended API handlers here and to the bottom of gasnet_extended.c */

#endif

/*  $Archive:: /Ti/GASNet/extended-ref/gasnet_extended_internal.h         $
 *     $Date: 2003/06/29 02:33:04 $
 * $Revision: 1.11 $
 * Description: GASNet header for internal definitions in Extended API
 * Copyright 2002, Christian Bell <csbell@cs.berkeley.edu>
 * Copyright 2002, Dan Bonachea <bonachea@cs.berkeley.edu>
 * Terms of use are as specified in license.txt
 */

#ifndef _GASNET_EXTENDED_INTERNAL_H
#define _GASNET_EXTENDED_INTERNAL_H

#include <gasnet.h>
#include <gasnet_handler.h>
#include <gasnet_internal.h>

/* ------------------------------------------------------------------------------------ */
/*  reasonable upper-bound on L2 cache line size (don't make this too big) */
#define GASNETE_CACHE_LINE_BYTES  (128)
#if defined(TRACE) || defined(STATS)
  #define GASNETC_FIREHOSE_TRACE
  typedef
  enum gasnetc_fh_stats { 
	fh_none, fh_onesided, fh_one, fh_many
  } gasnetc_fh_stats_t;
#endif

typedef uint8_t gasnete_threadidx_t;

/* gasnet_handle_t is a void* pointer to a gasnete_op_t, 
   which is either a gasnete_eop_t or an gasnete_iop_t
   */
typedef struct _gasnete_op_t {
	uint8_t			flags;		/*  flags - type tag */
	gasnete_threadidx_t	threadidx;	/*  thread that owns me */
} gasnete_op_t;

/* for compactness, eops address each other in the free list using a gasnete_eopaddr_t */ 
typedef struct _gasnete_eopaddr_t {
  uint8_t bufferidx;
  uint8_t eopidx;
} gasnete_eopaddr_t;

#define gasnete_eopaddr_equal(addr1,addr2) (*(uint16_t*)&(addr1) == *(uint16_t*)&(addr2))
#define gasnete_eopaddr_isnil(addr) (*(uint16_t*)&(addr) == *(uint16_t*)&(EOPADDR_NIL))

/* -------------------------------------------------------------------------- */
/* Explicit ops, used when operations are known to require only one send in
 * order to complete */
struct _gasnete_iop_t;
typedef struct _gasnete_eop_t {
	uint8_t		flags;	/*  state flags */

	gasnete_threadidx_t	threadidx;  /*  thread that owns me */
	gasnet_node_t		node;
	uintptr_t		dest;
	uintptr_t		src;
	uint32_t		len;
	struct _gasnete_iop_t	*iop;
	struct _gasnete_eop_t	*next;		/* when used in FIFO */
	#ifdef GASNETC_FIREHOSE_TRACE
	gasnetc_fh_stats_t	fh_stats;
	gasneti_stattime_t	starttime;
	#endif

	gasnete_eopaddr_t	addr;      /*  next cell while in free list, 
					       my own eopaddr_t while in use */
} gasnete_eop_t;

/* -------------------------------------------------------------------------- */
/* Implicit ops, used by the extended reference implementation */
typedef struct _gasnete_iop_t {
  	uint8_t		flags;	/*  state flags */

	gasnete_threadidx_t	threadidx;  /*  thread that owns me */
	gasnet_node_t		node;
  	uintptr_t		dest;		/* remote RDMA addr */
	uintptr_t		src;		/* source addr */
	uint32_t		len;		/* length */

	int	initiated_get_cnt;     /*  count of get ops initiated */
	int	initiated_put_cnt;     /*  count of put ops initiated */

	struct _gasnete_iop_t *next;    /*  next cell while in free list, deferred 
				            iop while being filled */

	/*  make sure the counters live on different cache lines for SMP's */
	uint8_t pad[GASNETE_CACHE_LINE_BYTES - sizeof(void*) - sizeof(int)]; 

	gasneti_atomic_t completed_get_cnt;     /*  count of get ops completed */
	gasneti_atomic_t completed_put_cnt;     /*  count of put ops completed */
} gasnete_iop_t;

/* -------------------------------------------------------------------------- */
typedef struct _gasnete_threaddata_t {
	/* pointer reserved for use by the core */
	void			*gasnetc_threaddata;
	gasnete_threadidx_t	threadidx;

#ifdef GASNETC_FIREHOSE
	uintptr_t		*fh_buf;
	size_t			fh_num;
#endif

	int eop_num_bufs;	/*  number of valid buffer entries */
	gasnete_eop_t		*eop_bufs[256]; /*  buffers of eops */
	gasnete_eopaddr_t	eop_free;   /*  free list of eops */

	/*  stack of iops - head is active iop servicing new implicit ops */
	gasnete_iop_t *current_iop;  
	gasnete_iop_t *iop_free;      /*  free list of iops */
        struct _gasnet_valget_op_t *valget_free; /* free list of valget cells */
} gasnete_threaddata_t;
/* -------------------------------------------------------------------------- */

/* gasnete_op_t flags field */
#define OPTYPE_EXPLICIT		0x00  /* gasnete_eop_new() relies on this value */
#define OPTYPE_IMPLICIT		0x80
#define OPTYPE(op)		((op)->flags & 0x80)
GASNET_INLINE_MODIFIER(SET_OPTYPE)
void SET_OPTYPE(gasnete_op_t *op, uint8_t type) {
	op->flags = (op->flags & 0x7F) | (type & 0x80);
	assert(OPTYPE(op) == type);
}

/*  state */
#define OPSTATE_FREE		0   /* gasnete_eop_new() relies on this value */
#define OPSTATE_INFLIGHT	1
#define OPSTATE_COMPLETE	3
#define OPSTATE(op)		((op)->flags & 0x03) 
#define OPMISC_NONAMBUF		4
#define OPMISC_AMBUF		8
#define OPMISC(op)		((op)->flags & 0x0C)
GASNET_INLINE_MODIFIER(SET_OPSTATE)
void SET_OPSTATE(gasnete_eop_t *op, uint8_t state) {
	op->flags = (op->flags & 0xFC) | (state & 0x03);
	assert(OPSTATE(op) == state);
}

GASNET_INLINE_MODIFIER(SET_OPMISC)
void SET_OPMISC(gasnete_eop_t *op, uint8_t misc) {
	op->flags = (op->flags & 0xF3) | (misc & 0x0C);
	assert(OPMISC(op) == misc);
}

/* New op creation, gets new op and marks it in flight */
gasnete_eop_t	*gasnete_eop_new(gasnete_threaddata_t * const thread);
gasnete_iop_t	*gasnete_iop_new(gasnete_threaddata_t * const thread);

/*  query an eop for completeness */
int		gasnete_op_isdone(gasnete_op_t *op);

/*  mark an op done - isget ignored for explicit ops */
void		gasnete_op_markdone(gasnete_op_t *op, int isget);
void		gasnete_op_free(gasnete_op_t *op);

#define GASNETE_EOPADDR_TO_PTR(threaddata, eopaddr)            \
      (assert(threaddata),                                     \
       assert((eopaddr).bufferidx<(threaddata)->eop_num_bufs), \
       assert(!gasnete_eopaddr_isnil(eopaddr)),                \
       (threaddata)->eop_bufs[(eopaddr).bufferidx] + (eopaddr).eopidx)

/* 1 = scatter newly allocated eops across cache lines to 
 *     reduce false sharing */
#define GASNETE_SCATTER_EOPS_ACROSS_CACHELINES    1 

/* -------------------------------------------------------------------------- */

/* make a GASNet call - if it fails, print error message and abort */
#define GASNETE_SAFE(fncall) do {                                           \
   int retcode = (fncall);                                                  \
   if_pf (retcode != GASNET_OK) {                                           \
     gasneti_fatalerror("\nGASNet encountered an error: %s(%i)\n"           \
        "  while calling: %s\n"                                             \
        "  at %s",                                                          \
        gasnet_ErrorName(retcode), retcode, #fncall, gasneti_current_loc);  \
   }                                                                        \
 } while (0)

/* Extended threads support */
extern const gasnete_eopaddr_t	EOPADDR_NIL;
extern gasnete_threaddata_t	*gasnete_threadtable[256];
#ifdef GASNETI_THREADS
extern gasnete_threaddata_t * gasnete_mythread();
#else
  #define gasnete_mythread() (gasnete_threadtable[0])
#endif

void            gasnete_begin_nbi_accessregion(
			int allowrecursion GASNETE_THREAD_FARG);
gasnet_handle_t gasnete_end_nbi_accessregion(GASNETE_THREAD_FARG_ALONE);

/* Extended reference functions */
gasnet_handle_t gasnete_extref_get_nb_bulk(void *dest, gasnet_node_t node, 
			void *src, size_t nbytes GASNETE_THREAD_FARG);
gasnet_handle_t gasnete_extref_put_nb(gasnet_node_t node, void *dest, 
			void *src, size_t nbytes GASNETE_THREAD_FARG);
gasnet_handle_t gasnete_extref_put_nb_bulk (gasnet_node_t node, void *dest, 
			void *src, size_t nbytes GASNETE_THREAD_FARG);
gasnet_handle_t gasnete_extref_memset_nb   (gasnet_node_t node, void *dest, 
			int val, size_t nbytes GASNETE_THREAD_FARG);
void gasnete_extref_get_nbi_bulk (void *dest, gasnet_node_t node, void *src, 
			size_t nbytes GASNETE_THREAD_FARG);
void gasnete_extref_put_nbi      (gasnet_node_t node, void *dest, void *src, 
			size_t nbytes GASNETE_THREAD_FARG);
void gasnete_extref_put_nbi_bulk (gasnet_node_t node, void *dest, void *src, 
			size_t nbytes GASNETE_THREAD_FARG);
void gasnete_extref_memset_nbi   (gasnet_node_t node, void *dest, int val, 
			size_t nbytes GASNETE_THREAD_FARG);
void gasnete_extref_barrier_notify(int id, int flags);
int gasnete_extref_barrier_wait(int id, int flags);
int gasnete_extref_barrier_try(int id, int flags);

/* Additional support for core AMReplyLongAsync */
#if defined(GASNETI_PTR32)
#define LONGASYNC_REP(cnt32, cnt64, args) \
	gasnetc_AMReplyLongAsync ## cnt32 args
#elif defined(GASNETI_PTR64)
#define LONGASYNC_REP(cnt32, cnt64, args) \
	gasnetc_AMReplyLongAsync ## cnt64 args
#endif

#define GASNETE_HANDLER_BASE  64 /* reserve 64-127 for the extended API */
#define _hidx_gasnete_extref_barrier_notify_reqh	(GASNETE_HANDLER_BASE+0) 
#define _hidx_gasnete_extref_barrier_done_reqh		(GASNETE_HANDLER_BASE+1)
#define _hidx_gasnete_extref_get_reqh			(GASNETE_HANDLER_BASE+2)
#define _hidx_gasnete_extref_get_reph			(GASNETE_HANDLER_BASE+3)
#define _hidx_gasnete_extref_getlong_reqh		(GASNETE_HANDLER_BASE+4)
#define _hidx_gasnete_extref_getlong_reph		(GASNETE_HANDLER_BASE+5)
#define _hidx_gasnete_extref_put_reqh			(GASNETE_HANDLER_BASE+6)
#define _hidx_gasnete_extref_putlong_reqh		(GASNETE_HANDLER_BASE+7)
#define _hidx_gasnete_extref_memset_reqh		(GASNETE_HANDLER_BASE+8)
#define _hidx_gasnete_extref_markdone_reph		(GASNETE_HANDLER_BASE+9)
#ifdef GASNETC_FIREHOSE
#define _hidx_gasnete_firehose_move_reph		(GASNETE_HANDLER_BASE+10)
#define _hidx_gasnete_firehose_get_dma_reqh		(GASNETE_HANDLER_BASE+11)
#define _hidx_gasnete_firehose_get_dma_reph		(GASNETE_HANDLER_BASE+12)
#elif defined(GASNETC_TURKEY)
#error not implemented yet
#endif
#endif

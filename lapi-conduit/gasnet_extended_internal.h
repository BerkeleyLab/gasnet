/*  $Archive:: /Ti/GASNet/extended-ref/gasnet_extended_internal.h         $
 *     $Date: 2003/06/29 02:33:05 $
 * $Revision: 1.4 $
 * Description: GASNet header for internal definitions in Extended API
 * Copyright 2002, Dan Bonachea <bonachea@cs.berkeley.edu>
 * Terms of use are as specified in license.txt
 */

#ifndef _GASNET_EXTENDED_INTERNAL_H
#define _GASNET_EXTENDED_INTERNAL_H

#include <gasnet.h>
#include <gasnet_handler.h>
#include <gasnet_internal.h>



/* =====================================================================
 * LAPI specific structures
 * =====================================================================
 */
#include <lapi.h>
/* no point in re-defining everything */
#include <gasnet_core_internal.h>

extern void** gasnete_remote_memset_hh;
extern void** gasnete_remote_barrier_hh;
extern void* gasnete_lapi_memset_hh(lapi_handle_t *context, void *uhdr, uint *uhdr_len,
				    ulong *msg_len, compl_hndlr_t **comp_h, void **uinfo);
extern void* gasnete_lapi_barrier_hh(lapi_handle_t *context, void *uhdr, uint *uhdr_len,
				     ulong *msg_len, compl_hndlr_t **comp_h, void **uinfo);

/* LAPI header handler argument for remote memset operation */
typedef struct {
    uintptr_t destLoc;
    int       value;
    size_t    nbytes;
} gasnete_memset_uhdr_t;

/* LAPI header handler argument for barrier operations */
typedef struct {
    int phase;
    int value;
    int flags;
    int mismatch;
    int is_notify;
    int src;
} gasnete_barrier_uhdr_t;

/* ------------------------------------------------------------------------------------ */
/*  reasonable upper-bound on L2 cache line size (don't make this too big) */
#define GASNETE_CACHE_LINE_BYTES  (128)

typedef uint8_t gasnete_threadidx_t;

/* gasnet_handle_t is a void* pointer to a gasnete_op_t, 
   which is either a gasnete_eop_t or an gasnete_iop_t
*/
typedef struct _gasnete_op_t {
    uint8_t flags;                  /*  flags - type tag */
    gasnete_threadidx_t threadidx;  /*  thread that owns me */
} gasnete_op_t;

/* for compactness, eops address each other in the free list using a gasnete_eopaddr_t */ 
typedef struct _gasnete_eopaddr_t {
    uint8_t bufferidx;
    uint8_t eopidx;
} gasnete_eopaddr_t;

#define gasnete_eopaddr_equal(addr1,addr2) (*(uint16_t*)&(addr1) == *(uint16_t*)&(addr2))
#define gasnete_eopaddr_isnil(addr) (*(uint16_t*)&(addr) == *(uint16_t*)&(EOPADDR_NIL))

typedef struct _gasnete_eop_t {
    uint8_t flags;                  /*  state flags */
    gasnete_threadidx_t threadidx;  /*  thread that owns me */
    gasnete_eopaddr_t addr;         /*  next cell while in free list, my own eopaddr_t while in use */
    int          initiated_cnt;
    lapi_cntr_t  cntr;
} gasnete_eop_t;

typedef struct _gasnete_iop_t {
    uint8_t flags;                  /*  state flags */
    gasnete_threadidx_t threadidx;  /*  thread that owns me */
    uint16_t _unused;
    int initiated_get_cnt;     /*  count of get ops initiated */
    int initiated_put_cnt;     /*  count of put ops initiated */

    struct _gasnete_iop_t *next;    /*  next cell while in free list, deferred iop while being filled */

    /*  make sure the counters live on different cache lines for SMP's */
    uint8_t pad[GASNETE_CACHE_LINE_BYTES - sizeof(void*) - sizeof(int)]; 

    lapi_cntr_t      get_cntr;
    lapi_cntr_t      put_cntr;
} gasnete_iop_t;

/* ------------------------------------------------------------------------------------ */
typedef struct _gasnete_threaddata_t {
    void *gasnetc_threaddata;     /* pointer reserved for use by the core */

    gasnete_threadidx_t threadidx;

    gasnete_eop_t *eop_bufs[256]; /*  buffers of eops for memory management */
    int eop_num_bufs;             /*  number of valid buffer entries */
    gasnete_eopaddr_t eop_free;   /*  free list of eops */

    /*  stack of iops - head is active iop servicing new implicit ops */
    gasnete_iop_t *current_iop;  

    gasnete_iop_t *iop_free;      /*  free list of iops */

    struct _gasnet_valget_op_t *valget_free; /* free list of valget cells */
} gasnete_threaddata_t;
/* ------------------------------------------------------------------------------------ */

/* gasnete_op_t flags field */
#define OPTYPE_EXPLICIT               0x00  /*  gasnete_eop_new() relies on this value */
#define OPTYPE_IMPLICIT               0x80
#define OPTYPE(op) ((op)->flags & 0x80)
GASNET_INLINE_MODIFIER(SET_OPTYPE)
    void SET_OPTYPE(gasnete_op_t *op, uint8_t type) {
    op->flags = (op->flags & 0x7F) | (type & 0x80);
}

/*  state - only valid for explicit ops */
#define OPSTATE_FREE      0   /*  gasnete_eop_new() relies on this value */
#define OPSTATE_INFLIGHT  1
#define OPSTATE_COMPLETE  2
#define OPSTATE(op) ((op)->flags & 0x03) 
GASNET_INLINE_MODIFIER(SET_OPSTATE)
    void SET_OPSTATE(gasnete_eop_t *op, uint8_t state) {
    op->flags = (op->flags & 0xFC) | (state & 0x03);
    assert(OPSTATE(op) == state);
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
#define GASNETE_EOPADDR_TO_PTR(threaddata, eopaddr)            \
      (assert(threaddata),                                     \
       assert((eopaddr).bufferidx<(threaddata)->eop_num_bufs), \
       assert(!gasnete_eopaddr_isnil(eopaddr)),                \
       (threaddata)->eop_bufs[(eopaddr).bufferidx] + (eopaddr).eopidx)

/*  1 = scatter newly allocated eops across cache lines to reduce false sharing */
#define GASNETE_SCATTER_EOPS_ACROSS_CACHELINES    1 

/* ------------------------------------------------------------------------------------ */

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

#define GASNETE_HANDLER_BASE  64 /* reserve 64-127 for the extended API */
/* add new extended API handlers here and to the bottom of gasnet_extended.c */

#endif

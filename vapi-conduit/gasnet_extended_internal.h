/*  $Archive:: /Ti/GASNet/extended-ref/gasnet_extended_internal.h         $
 *     $Date: 2003/09/06 02:22:34 $
 * $Revision: 1.8 $
 * Description: GASNet header for internal definitions in Extended API
 * Copyright 2002, Dan Bonachea <bonachea@cs.berkeley.edu>
 * Terms of use are as specified in license.txt
 */

#ifndef _GASNET_EXTENDED_INTERNAL_H
#define _GASNET_EXTENDED_INTERNAL_H

#include <gasnet.h>
#include <gasnet_handler.h>
#include <gasnet_internal.h>

/* Tune cut-off between PUTs and AMs for memset, 0 disables PUTs */
#if defined(GASNET_SEGMENT_FAST)
  #define GASNETE_MEMSET_PUT_LIMIT        GASNETC_BUFSZ
#else
  #define GASNETE_MEMSET_PUT_LIMIT        0
#endif

/* ------------------------------------------------------------------------------------ */
/*  reasonable upper-bound on L2 cache line size (don't make this too big) */
#define GASNETE_CACHE_LINE_BYTES  (128)

typedef uint8_t gasnete_threadidx_t;

enum {
  gasnete_opExplicit = 0,	/* gasnete_eop_new() relies on this value */
  gasnete_opImplicit
};

/* gasnet_handle_t is a void* pointer to a gasnete_op_t, 
   which is either a gasnete_eop_t or an gasnete_iop_t
   */
typedef struct _gasnete_op_t {
  uint8_t type;                   /*  type tag */
  gasnete_threadidx_t threadidx;  /*  thread that owns me */
} gasnete_op_t;

/* for compactness, eops address each other in the free list using a gasnete_eopaddr_t */ 
typedef union _gasnete_eopaddr_t {
  struct {
    uint8_t _bufferidx;
    uint8_t _eopidx;
  } compaddr;
  uint16_t fulladdr;
} gasnete_eopaddr_t;
#define bufferidx compaddr._bufferidx
#define eopidx compaddr._eopidx

#define gasnete_eopaddr_equal(addr1,addr2) ((addr1).fulladdr == (addr2).fulladdr)
#define gasnete_eopaddr_isnil(addr) ((addr).fulladdr == EOPADDR_NIL.fulladdr)

typedef struct _gasnete_eop_t {
  uint8_t type;                   /*  type tag */
  gasnete_threadidx_t threadidx;  /*  thread that owns me */
  gasnete_eopaddr_t addr;         /*  next cell while in free list, my own eopaddr_t while in use */
  gasneti_atomic_t req_oust;
} gasnete_eop_t;

typedef struct _gasnete_iop_t {
  uint8_t type;                   /*  type tag */
  gasnete_threadidx_t threadidx;  /*  thread that owns me */
  uint16_t _unused;

  struct _gasnete_iop_t *next;    /*  next cell while in free list */

  /*  make sure the counters live on a distinct cache line for SMPs */
  uint8_t _pad1[GASNETE_CACHE_LINE_BYTES - 2*sizeof(void *)];
  gasneti_atomic_t get_req_oust;     /*  count of get ops outstanding */
  gasneti_atomic_t put_req_oust;     /*  count of put ops outstanding */
  uint8_t _pad2[GASNETE_CACHE_LINE_BYTES - 2*sizeof(gasneti_atomic_t)];
} gasnete_iop_t;

/* ------------------------------------------------------------------------------------ */
typedef struct _gasnete_threaddata_t {
  void *gasnetc_threaddata;     /* pointer reserved for use by the core */

  gasnete_threadidx_t threadidx;

  gasnete_eop_t *eop_bufs[256]; /*  buffers of eops for memory management */
  int eop_num_bufs;             /*  number of valid buffer entries */
  gasnete_eopaddr_t eop_free;   /*  free list of eops */

  gasnete_iop_t *current_iop;   /* active iop servicing new implicit ops */
  gasnete_iop_t *default_iop;   /* iop used when no access region is active */

  gasnete_iop_t *iop_free;      /*  free list of iops */

  struct _gasnet_valget_op_t *valget_free; /* free list of valget cells */
} gasnete_threaddata_t;
/* ------------------------------------------------------------------------------------ */

/*  get a new op */
gasnete_eop_t *gasnete_eop_new(gasnete_threaddata_t *thread);
gasnete_iop_t *gasnete_iop_new(gasnete_threaddata_t *thread);
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
#define _hidx_gasnete_barrier_notify_reqh   (GASNETE_HANDLER_BASE+0) 
#define _hidx_gasnete_barrier_done_reqh     (GASNETE_HANDLER_BASE+1)
#define _hidx_gasnete_done_reph             (GASNETE_HANDLER_BASE+2)
#define _hidx_gasnete_getmed_reqh           (GASNETE_HANDLER_BASE+3)
#define _hidx_gasnete_getmed_reph           (GASNETE_HANDLER_BASE+4)
#define _hidx_gasnete_getlong_reqh          (GASNETE_HANDLER_BASE+5)
#define _hidx_gasnete_put_reqh              (GASNETE_HANDLER_BASE+6)
#define _hidx_gasnete_memset_reqh           (GASNETE_HANDLER_BASE+7)
/* add new extended API handlers here and to the bottom of gasnet_extended.c */

#endif

/*  $Archive:: /Ti/GASNet/elan-conduit/gasnet_extended_internal.h         $
 *     $Date: 2003/06/29 02:33:01 $
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
#include <gasnet_core_internal.h>

/* ------------------------------------------------------------------------------------ */
/*  reasonable upper-bound on L2 cache line size (don't make this too big) */
#define GASNETE_CACHE_LINE_BYTES  (128)

#ifdef ELAN_VER_1_2
  #define GASNETE_USE_PGCTRL_NBI  0 /* pgctrl not available on 1.2 */
#else
  #define GASNETE_USE_PGCTRL_NBI  0
#endif

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

/* header for bounce buffers */
typedef struct {
  struct _gasnete_eop_t *next;    /* next ptr for ELANPUTBB/GETBB in iop lists */
  ELAN_EVENT *evt;                /* elan event for this operation */
  void *get_dest;                 /* destination for get (ELANGETBB only) */
  uint32_t get_nbytes;            /* length of get */
  char _pad[64 - 3*sizeof(void *) - 4]; /* pad to 64-byte alignment for fast block copy */
  /* user data */
} gasnete_bouncebuf_t;

typedef struct _gasnete_eop_t {
  uint8_t flags;                  /*  state flags */
  gasnete_threadidx_t threadidx;  /*  thread that owns me */
  gasnete_eopaddr_t addr;         /*  next cell while in free list, my own eopaddr_t while in use */

  /* for in-flight ELANPUTBB/GETBB where a copy is required */
  gasnete_bouncebuf_t *bouncebuf; 
} gasnete_eop_t;

typedef struct {
  ELAN_EVENT  **evt_lst; 
  int          evt_cnt;
} gasnete_putgetctrl;

typedef struct _gasnete_iop_t {
  uint8_t flags;                  /*  state flags */
  gasnete_threadidx_t threadidx;  /*  thread that owns me */
  uint16_t _unused;
  int initiated_put_cnt;     /*  count of put ops initiated */
  int initiated_get_cnt;     /*  count of get ops initiated */

  struct _gasnete_iop_t *next;    /*  next cell while in free list, deferred iop while being filled */

  #if GASNETE_USE_PGCTRL_NBI
    ELAN_PGCTRL *elan_pgctrl;     /* put/get controller for direct elan put/gets, (NULL for not init) */
  #else
    gasnete_putgetctrl putctrl;  /* direct elan puts/gets */
    gasnete_putgetctrl getctrl;
  #endif
  gasnete_eop_t *elan_putbb_list; /* list of bounce-buffered elan put eops */
  gasnete_eop_t *elan_getbb_list; /* list of bounce-buffered elan get eops */

  /*  make sure the completion counters live on a cache line by themselves for SMP's */
  uint8_t _pad[GASNETE_CACHE_LINE_BYTES - 4*sizeof(void*) - sizeof(int)]; 
  gasneti_atomic_t completed_put_cnt;     /*  count of put ops completed */
  gasneti_atomic_t completed_get_cnt;     /*  count of get ops completed */
  uint8_t _pad2[GASNETE_CACHE_LINE_BYTES - 2*sizeof(gasneti_atomic_t)]; 
} gasnete_iop_t;

/* ------------------------------------------------------------------------------------ */
typedef struct _gasnete_threaddata_t {
  void *gasnetc_threaddata;     /* pointer reserved for use by the core - MUST be first */

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

/* op category - only valid for explicit ops */
#define OPCAT_RESERVED0 0 /* unused */
#define OPCAT_ELANGETBB 1 /* elan get through bounce buffer */
#define OPCAT_ELANPUTBB 2 /* elan put through bounce buffer */
#define OPCAT_AMGET     3
#define OPCAT_AMPUT     4
#define OPCAT_MEMSET    5
#define OPCAT_RESERVED6 6 /* unused */
#define OPCAT_RESERVED7 7 /* unused */
#define OPCAT(op) (((op)->flags >> 2) & 0x07)
GASNET_INLINE_MODIFIER(SET_OPCAT)
void SET_OPCAT(gasnete_eop_t *op, uint8_t cat) {
  op->flags = (op->flags & 0xE3) | ((cat & 0x07) << 2);
  assert(OPCAT(op) == cat);
}

/*  get a new op and mark it in flight */
gasnete_eop_t *gasnete_eop_new(gasnete_threaddata_t *thread, uint8_t cat);
gasnete_iop_t *gasnete_iop_new(gasnete_threaddata_t *thread);
/*  query an eop for completeness */
int gasnete_op_isdone(gasnete_op_t *op, int have_elanLock);
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
#define _hidx_gasnete_barrier_notify_reqh   (GASNETE_HANDLER_BASE+0) 
#define _hidx_gasnete_barrier_done_reqh     (GASNETE_HANDLER_BASE+1)
#define _hidx_gasnete_get_reqh              (GASNETE_HANDLER_BASE+2)
#define _hidx_gasnete_get_reph              (GASNETE_HANDLER_BASE+3)
#define _hidx_gasnete_getlong_reqh          (GASNETE_HANDLER_BASE+4)
#define _hidx_gasnete_getlong_reph          (GASNETE_HANDLER_BASE+5)
#define _hidx_gasnete_put_reqh              (GASNETE_HANDLER_BASE+6)
#define _hidx_gasnete_putlong_reqh          (GASNETE_HANDLER_BASE+7)
#define _hidx_gasnete_memset_reqh           (GASNETE_HANDLER_BASE+8)
#define _hidx_gasnete_markdone_reph         (GASNETE_HANDLER_BASE+9)
/*
#define _hidx_                              (GASNETC_HANDLER_BASE+)
*/

/* add new extended API handlers here and to the bottom of gasnet_extended.c */

#endif

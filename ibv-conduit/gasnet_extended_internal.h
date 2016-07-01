/*   $Source: bitbucket.org:berkeleylab/gasnet.git/ibv-conduit/gasnet_extended_internal.h $
 * Description: GASNet header for internal definitions in Extended API
 * Copyright 2002, Dan Bonachea <bonachea@cs.berkeley.edu>
 * Terms of use are as specified in license.txt
 */

#ifndef _GASNET_EXTENDED_INTERNAL_H
#define _GASNET_EXTENDED_INTERNAL_H

#include <gasnet_internal.h>
#ifdef GASNETE_EXTENDED_NEEDS_CORE
#include <gasnet_core_internal.h>
#endif

/* ------------------------------------------------------------------------------------ */

#if defined(GASNETE_EOP_COUNTED)
#  ifdef GASNETE_EOP_BOOLEAN
#    error "Only one of GASNETE_EOP_COUNTED or GASNETE_EOP_BOOLEAN may be defined"
#  endif
#  undef GASNETE_EOP_COUNTED
#  define GASNETE_EOP_COUNTED 1
#  define GASNETE_EOP_BOOLEAN 0
#else
#  undef GASNETE_EOP_BOOLEAN
#  define GASNETE_EOP_BOOLEAN 1
#  define GASNETE_EOP_COUNTED 0
#endif

/* ------------------------------------------------------------------------------------ */

/* gasnetex_handle_t is a void* pointer to a gasnete_op_t, 
   which is either a gasnete_eop_t or an gasnete_iop_t
   For "normal" ABIs we layout the op as follows:
     1-byte threadidx: FFT.                  KEY:
     2-byte threadidx: FFTT                  F = flags bytes
     4-byte threadidx: FF..TTTT              T = threadidx bytes
     8-byte threadidx: FF......TTTTTTTT      . = implicit (ABI) padding
   */
typedef struct _gasnete_op_t {
  uint8_t flags;                  /*  flags - type tag */
  uint8_t flags2;                 /*  flags2 - LC info */
  gasnete_threadidx_t threadidx;  /*  thread that owns me (16-bit by default) */
} gasnete_op_t;

#define EOP_NEXT(eop) (*(void**)(eop))

typedef struct _gasnete_eop_t {
  uint8_t flags;                  /*  state flags */
  uint8_t flags2;                 /*  LC state flags */
  gasnete_threadidx_t threadidx;  /*  thread that owns me */
  // Padding to ensure sizeof(eop) >= sizeof(void*), and avoid any later fields
  // having offset < sizeof(void) and thus conflict with the freelist linkage.
  #if PLATFORM_ARCH_64 && (SIZEOF_GASNETE_THREADIDX_T < 4)
    uint32_t pad;
  #endif
  #if GASNETE_EOP_COUNTED
  gasnetc_atomic_val_t initiated_cnt;
  gasnetc_atomic_t     completed_cnt;
  gasnetc_atomic_val_t initiated_alc;
  gasnetc_atomic_t     completed_alc;
  #endif
  #ifdef GASNETE_CONDUIT_EOP_FIELDS
  GASNETE_CONDUIT_EOP_FIELDS
  #endif
} gasnete_eop_t;

typedef struct _gasnete_iop_t {
  uint8_t flags;                  /*  state flags */
  uint8_t flags2;                 /*  currently unused */
  gasnete_threadidx_t threadidx;  /*  thread that owns me */
  gasnetc_atomic_val_t initiated_alc_cnt;     /*  count of ops initiated with async local completion */
  gasnetc_atomic_val_t initiated_get_cnt;     /*  count of get ops initiated */
  gasnetc_atomic_val_t initiated_put_cnt;     /*  count of put ops initiated */

  struct _gasnete_iop_t *next;    /*  next cell while in free list, deferred iop while being filled */

  /*  make sure the initiated/completed counters live on different cache lines for SMP's */
  uint8_t pad[GASNETI_CACHE_PAD(sizeof(void*) + 3*sizeof(gasnetc_atomic_val_t))];

  gasnetc_atomic_t completed_alc_cnt;     /*  count of async-lc ops completed */
  gasnetc_atomic_t completed_get_cnt;     /*  count of get ops completed */
  gasnetc_atomic_t completed_put_cnt;     /*  count of put ops completed */
  uint8_t _pad2[MAX(8,(ssize_t)(GASNETI_CACHE_LINE_BYTES - 2*sizeof(gasnetc_atomic_t)))];

  #ifdef GASNETE_CONDUIT_IOP_FIELDS
  GASNETE_CONDUIT_IOP_FIELDS
  #endif
} gasnete_iop_t;

/* ------------------------------------------------------------------------------------ */
typedef struct _gasnete_threaddata_t {
  GASNETE_COMMON_THREADDATA_FIELDS /* MUST come first, for reserved ptrs */

  gasnete_eop_t *eop_bufs[256]; /*  buffers of eops for memory management */
  int eop_num_bufs;             /*  number of valid buffer entries */
  gasnete_eop_t *eop_free;      /*  free list of eops */

  /*  stack of iops - head is active iop servicing new implicit ops */
  gasnete_iop_t *current_iop;  

  gasnete_iop_t *iop_free;      /*  free list of iops */

  #ifdef GASNETE_CONDUIT_THREADDATA_FIELDS
  GASNETE_CONDUIT_THREADDATA_FIELDS
  #endif
} gasnete_threaddata_t;

/* ------------------------------------------------------------------------------------ */

/* gasnete_op_t flags field */
#define OPTYPE_EXPLICIT               0x00  /*  gasnete_eop_new() relies on this value */
#define OPTYPE_IMPLICIT               0x80
#define OPTYPE(op) ((op)->flags & 0x80)
GASNETI_INLINE(SET_OPTYPE)
void SET_OPTYPE(gasnete_op_t *op, uint8_t type) {
  op->flags = (op->flags & 0x7F) | (type & 0x80);
}

/*  state - only valid for explicit ops */
#define EOPSTATE_FREE      0   /*  gasnete_eop_new() relies on this value */
#define EOPSTATE_INFLIGHT  1
#define EOPSTATE_COMPLETE  2
#define EOPSTATE(op) ((op)->flags & 0x03) 
GASNETI_INLINE(SET_EOPSTATE)
void SET_EOPSTATE(gasnete_eop_t *op, uint8_t state) {
  op->flags = (op->flags & 0xFC) | (state & 0x03);
  /* RACE: If we are marking the op COMPLETE, don't assert for completion
   * state as another thread spinning on the op may already have changed
   * the state. */
  gasneti_assert(state == EOPSTATE_COMPLETE ? 1 : EOPSTATE(op) == state);
}

/* eop is LC only (e.g. from an AM initiation w/ lc_opt=ptr) */
#define EOPFLAG_LC_ONLY 0x40

/* gasnete_op_t flag bits reserved for conduit-specific uses.
 * guaranteed not to conflict with use in extendef-ref and
 * are preserved by SET_OP{STATE,TYPE}() */
#define OPFLAG_CONDUIT0 0x04
#define OPFLAG_CONDUIT1 0x08
#define OPFLAG_CONDUIT2 0x10

/*  Local Completion (LC) state */
#define LCSTATE_NONE   0 /*  op has NO local completion state */
#define LCSTATE_LIVE   1 /*  op has competion state - only used in DEBUG builds */
#define LCSTATE_SYNC   2 /*  op is an iop returned from end_nbi_accessregion(LC_SYNC) w/ LC outstanding */
#if 0 // Fully general implementation
  #define LCSTATE(op) ((op)->flags2 & 0x03))
  GASNETI_INLINE(SET_LCSTATE_)
  void SET_LCSTATE_(gasnete_op_t *op, uint8_t state) {
    gasneti_assert((state & 0x03) == state);
    op->flags2 = (op->flags2 & 0xFC) | state;
  }
#else // Cheaper, but correct only because nothing else is using 'flags2'
  #define LCSTATE(op) ((op)->flags2)
  GASNETI_INLINE(SET_LCSTATE_)
  void SET_LCSTATE_(gasnete_op_t *op, uint8_t state) {
    gasneti_assert((state & 0x03) == state);
    op->flags2 = state;
  }
#endif
#define SET_LCSTATE(op,flags) SET_LCSTATE_((gasnete_op_t*)(op),flags)

#if GASNET_DEBUG
  /* check an in-flight/complete eop */
  #define gasnete_eop_check(eop) do {                                \
    gasnete_threaddata_t * _th;                                      \
    gasneti_assert(OPTYPE(eop) == OPTYPE_EXPLICIT);                  \
    gasneti_assert(EOPSTATE(eop) == EOPSTATE_INFLIGHT ||               \
                   EOPSTATE(eop) == EOPSTATE_COMPLETE);                \
    gasnete_assert_valid_threadid((eop)->threadidx);                 \
    _th = gasnete_threadtable[(eop)->threadidx];                     \
  } while (0)
  #define gasnete_iop_check(iop) do {                         \
    gasnete_iop_t *_tmp_next;                                 \
    gasnetc_atomic_val_t _temp;                               \
    gasneti_memcheck(iop);                                    \
    _tmp_next = (iop)->next;                                  \
    if (_tmp_next != NULL) _gasnete_iop_check(_tmp_next);     \
    gasneti_assert(OPTYPE(iop) == OPTYPE_IMPLICIT);           \
    gasnete_assert_valid_threadid((iop)->threadidx);          \
    _temp = gasnetc_atomic_read(&((iop)->completed_put_cnt), GASNETI_ATOMIC_RMB_POST); \
    gasneti_assert((((iop)->initiated_put_cnt - _temp) & GASNETI_ATOMIC_MAX) < (GASNETI_ATOMIC_MAX/2)); \
    _temp = gasnetc_atomic_read(&((iop)->completed_get_cnt), GASNETI_ATOMIC_RMB_POST); \
    gasneti_assert((((iop)->initiated_get_cnt - _temp) & GASNETI_ATOMIC_MAX) < (GASNETI_ATOMIC_MAX/2)); \
  } while (0)
  extern void _gasnete_iop_check(gasnete_iop_t *iop);
#else
  #define gasnete_eop_check(eop)   ((void)0)
  #define gasnete_iop_check(iop)   ((void)0)
#endif

#define GASNETE_IOP_CNTDONE(_iop, _name) \
  (gasnetc_atomic_read(&(_iop)->completed_##_name##_cnt, 0) \
          == ((_iop)->initiated_##_name##_cnt & GASNETI_ATOMIC_MAX))

#if GASNETE_EOP_COUNTED
  #define GASNETE_EOP_DONE(_eop) \
    (gasnetc_atomic_read(&(_eop)->completed_cnt, 0) \
          == ((_eop)->initiated_cnt & GASNETI_ATOMIC_MAX))
  #define GASNETE_EOP_MARKDONE(_eop) do {                        \
      gasneti_assert(!GASNETE_EOP_DONE(_eop));                   \
      gasnetc_atomic_increment(&((_eop)->completed_cnt), 0);     \
    } while (0)
  #define GASNETE_EOP_LC(_eop) \
    (gasnetc_atomic_read(&(_eop)->completed_alc, 0) \
          == ((_eop)->initiated_alc & GASNETI_ATOMIC_MAX))
  #define GASNETE_EOP_MARKLC(_eop) do {                        \
      gasneti_assert(!GASNETE_EOP_LC(_eop));                   \
      gasnetc_atomic_increment(&((_eop)->completed_alc), 0);   \
    } while (0)
#else
  #define GASNETE_EOP_DONE(_eop) (EOPSTATE(_eop) == EOPSTATE_COMPLETE)
  #define GASNETE_EOP_MARKDONE(_eop) do {      \
      gasneti_assert(!GASNETE_EOP_DONE(_eop)); \
      SET_EOPSTATE((_eop), EOPSTATE_COMPLETE);   \
    } while (0)
  #define GASNETE_EOP_LC(_eop) (LCSTATE(_eop) == LCSTATE_NONE)
  #define GASNETE_EOP_MARKLC(_eop) do {      \
      gasneti_assert(!GASNETE_EOP_LC(_eop)); \
      SET_LCSTATE((_eop), LCSTATE_NONE); \
    } while (0)
#endif

/*  1 = scatter newly allocated eops across cache lines to reduce false sharing */
#define GASNETE_SCATTER_EOPS_ACROSS_CACHELINES    1 

/* ------------------------------------------------------------------------------------ */
/* called at startup to check configuration sanity if using any portion of AMRef */
extern void gasnete_check_config_amref(void);

/* ------------------------------------------------------------------------------------ */

#define GASNETE_HANDLER_BASE  64 /* reserve 64-127 for the extended API */
#define _hidx_gasnete_amdbarrier_notify_reqh (GASNETE_HANDLER_BASE+0) 
#define _hidx_gasnete_amcbarrier_notify_reqh (GASNETE_HANDLER_BASE+1) 
#define _hidx_gasnete_amcbarrier_done_reqh   (GASNETE_HANDLER_BASE+2)
#define _hidx_gasnete_amref_get_reqh         (GASNETE_HANDLER_BASE+3)
#define _hidx_gasnete_amref_get_reph         (GASNETE_HANDLER_BASE+4)
#define _hidx_gasnete_amref_getlong_reqh     (GASNETE_HANDLER_BASE+5)
#define _hidx_gasnete_amref_getlong_reph     (GASNETE_HANDLER_BASE+6)
#define _hidx_gasnete_amref_put_reqh         (GASNETE_HANDLER_BASE+7)
#define _hidx_gasnete_amref_putlong_reqh     (GASNETE_HANDLER_BASE+8)
#define _hidx_gasnete_amref_markdone_reph    (GASNETE_HANDLER_BASE+9)
/* add new extended API handlers here and to the bottom of gasnet_extended.c */

#endif

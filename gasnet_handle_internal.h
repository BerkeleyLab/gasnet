/*   $Source: bitbucket.org:berkeleylab/gasnet.git/gasnet_handle_internal.h $
 * Description: GASNet header for internal definitions for handle management
 * Copyright 2002, Dan Bonachea <bonachea@cs.berkeley.edu>
 * Terms of use are as specified in license.txt
 */

#ifndef _GASNET_HANDLE_INTERNAL_H
#define _GASNET_HANDLE_INTERNAL_H

#include <gasnet_core_internal.h>

/* ------------------------------------------------------------------------------------ */

// TODO-EX: remove default?
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

/* Conduit may optionally choose the atomic type for counters */
#ifndef gasnete_op_atomic_
  #define gasnete_op_atomic_(_id)       gasneti_weakatomic_##_id
#endif
#define gasnete_op_atomic_t             gasnete_op_atomic_(t)
#define gasnete_op_atomic_val_t         gasnete_op_atomic_(val_t)
#define gasnete_op_atomic_read          gasnete_op_atomic_(read)
#define gasnete_op_atomic_set           gasnete_op_atomic_(set)
#define gasnete_op_atomic_increment     gasnete_op_atomic_(increment)
#define gasnete_op_atomic_add           gasnete_op_atomic_(add)

/* ------------------------------------------------------------------------------------ */

/* gasnetex_handle_t is a void* pointer to a gasnete_op_t, 
   which is either a gasnete_eop_t or an gasnete_iop_t
 */
#define GASNETE_OP_EVENTS 6
typedef struct _gasnete_op_t {
  uint8_t event[GASNETE_OP_EVENTS];
  gasnete_threadidx_t threadidx;  /*  thread that owns me (16-bit by default) */
} gasnete_op_t;

typedef struct _gasnete_eop_t {
  uint8_t event[GASNETE_OP_EVENTS];
  gasnete_threadidx_t threadidx;  /*  thread that owns me */
  //----------------------------------
  struct _gasnete_eop_t *next;
  #if GASNETE_EOP_COUNTED
  gasnete_op_atomic_val_t initiated_cnt;
  gasnete_op_atomic_t     completed_cnt;
  gasnete_op_atomic_val_t initiated_alc;
  gasnete_op_atomic_t     completed_alc;
  #endif
  #ifdef GASNETE_CONDUIT_EOP_FIELDS
  GASNETE_CONDUIT_EOP_FIELDS
  #endif
} gasnete_eop_t;

typedef struct _gasnete_iop_t {
  uint8_t event[GASNETE_OP_EVENTS];
  gasnete_threadidx_t threadidx;  /*  thread that owns me */
  //----------------------------------
  gasnete_op_atomic_val_t initiated_alc_cnt;     /*  count of ops initiated with async local completion */
  gasnete_op_atomic_val_t initiated_get_cnt;     /*  count of get ops initiated */
  gasnete_op_atomic_val_t initiated_put_cnt;     /*  count of put ops initiated */

  struct _gasnete_iop_t *next;    /*  next cell while in free list, deferred iop while being filled */

  /*  make sure the corresponding initiated/completed counters live on different cache lines for SMP's */
  uint8_t pad[GASNETI_CACHE_PAD(sizeof(void*) + 3*sizeof(gasnete_op_atomic_val_t))];

  gasnete_op_atomic_t completed_alc_cnt;     /*  count of async-lc ops completed */
  gasnete_op_atomic_t completed_get_cnt;     /*  count of get ops completed */
  gasnete_op_atomic_t completed_put_cnt;     /*  count of put ops completed */

  #ifdef GASNETE_CONDUIT_IOP_FIELDS
  GASNETE_CONDUIT_IOP_FIELDS
  #endif
} gasnete_iop_t;

/* ------------------------------------------------------------------------------------ */

/* gasnete_op_t event[0] field */
#define OPTYPE_EXPLICIT               0x00  /*  gasnete_eop_new() relies on this value */
#define OPTYPE_IMPLICIT               0x80
#define OPTYPE(op) ((op)->event[0] & 0x80)
GASNETI_INLINE(SET_OPTYPE)
void SET_OPTYPE(gasnete_op_t *op, uint8_t type) {
  op->event[0] = (op->event[0] & 0x7F) | (type & 0x80);
}

/*  EOP state - only valid for explicit ops */
#define EOPSTATE_FREE      0   /*  gasnete_eop_new() relies on this value */
#define EOPSTATE_INFLIGHT  1
#define EOPSTATE_COMPLETE  2
#define EOPSTATE(eop) (gasneti_assert(OPTYPE(eop)==OPTYPE_EXPLICIT), ((eop)->event[0] & 0x03))
GASNETI_INLINE(SET_EOPSTATE)
void SET_EOPSTATE(gasnete_eop_t *op, uint8_t state) {
  op->event[0] = (op->event[0] & 0xFC) | (state & 0x03);
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
#define LCSTATE_DEFER  2 /*  op is an iop returned from end_nbi_accessregion(EVENT_DEFER) w/ LC outstanding */
#if 0 // Fully general implementation
  #define LCSTATE(op) ((op)->event[1] & 0x03))
  GASNETI_INLINE(SET_LCSTATE_)
  void SET_LCSTATE_(gasnete_op_t *op, uint8_t state) {
    gasneti_assert((state & 0x03) == state);
    op->event[1] = (op->event[1] & 0xFC) | state;
  }
#else // Cheaper, but correct only because nothing else is using 'event[1]'
  #define LCSTATE(op) ((op)->event[1])
  GASNETI_INLINE(SET_LCSTATE_)
  void SET_LCSTATE_(gasnete_op_t *op, uint8_t state) {
    gasneti_assert((state & 0x03) == state);
    op->event[1] = state;
  }
#endif
#define SET_LCSTATE(op,flags) SET_LCSTATE_((gasnete_op_t*)(op),flags)

#if GASNET_DEBUG
  /* check an in-flight/complete eop */
  #define gasnete_eop_check(eop) do {                                \
    gasnete_threaddata_t * _th;                                      \
    gasneti_assert(OPTYPE(eop) == OPTYPE_EXPLICIT);                  \
    gasneti_assert(EOPSTATE(eop) == EOPSTATE_INFLIGHT ||             \
                   EOPSTATE(eop) == EOPSTATE_COMPLETE ||             \
                   GASNETE_EOP_COUNTED);                             \
    gasnete_assert_valid_threadid((eop)->threadidx);                 \
    _th = gasnete_threadtable[(eop)->threadidx];                     \
  } while (0)
  #define gasnete_iop_check(iop) do {                         \
    gasnete_iop_t *_tmp_next;                                 \
    gasnete_op_atomic_val_t _temp;                            \
    gasneti_memcheck(iop);                                    \
    _tmp_next = (iop)->next;                                  \
    if (_tmp_next != NULL) _gasnete_iop_check(_tmp_next);     \
    gasneti_assert(OPTYPE(iop) == OPTYPE_IMPLICIT);           \
    gasnete_assert_valid_threadid((iop)->threadidx);          \
    _temp = gasnete_op_atomic_read(&((iop)->completed_put_cnt), GASNETI_ATOMIC_RMB_POST); \
    gasneti_assert((((iop)->initiated_put_cnt - _temp) & GASNETI_ATOMIC_MAX) < (GASNETI_ATOMIC_MAX/2)); \
    _temp = gasnete_op_atomic_read(&((iop)->completed_get_cnt), GASNETI_ATOMIC_RMB_POST); \
    gasneti_assert((((iop)->initiated_get_cnt - _temp) & GASNETI_ATOMIC_MAX) < (GASNETI_ATOMIC_MAX/2)); \
  } while (0)
  extern void _gasnete_iop_check(gasnete_iop_t *iop);
#else
  #define gasnete_eop_check(eop)   ((void)0)
  #define gasnete_iop_check(iop)   ((void)0)
#endif

#ifndef GASNETE_IOP_CNTDONE
#define GASNETE_IOP_CNTDONE(_iop, _name) \
  (gasnete_op_atomic_read(&(_iop)->completed_##_name##_cnt, 0) \
          == ((_iop)->initiated_##_name##_cnt & GASNETI_ATOMIC_MAX))
#endif

#ifndef GASNETE_EOP_DONE
 #if GASNETE_EOP_COUNTED
  #define GASNETE_EOP_DONE(_eop) \
    (gasnete_op_atomic_read(&(_eop)->completed_cnt, 0) \
          == ((_eop)->initiated_cnt & GASNETI_ATOMIC_MAX))
 #else // GASNETE_EOP_BOOLEAN
  #define GASNETE_EOP_DONE(_eop) (EOPSTATE(_eop) == EOPSTATE_COMPLETE)
 #endif
#endif

#ifndef GASNETE_EOP_MARKDONE
 #if GASNETE_EOP_COUNTED
  #define GASNETE_EOP_MARKDONE(_eop) do {                        \
      gasneti_assert(!GASNETE_EOP_DONE(_eop));                   \
      gasnete_op_atomic_increment(&((_eop)->completed_cnt), 0);  \
    } while (0)
 #else // GASNETE_EOP_BOOLEAN
  #define GASNETE_EOP_MARKDONE(_eop) do {      \
      gasneti_assert(!GASNETE_EOP_DONE(_eop)); \
      SET_EOPSTATE((_eop), EOPSTATE_COMPLETE); \
    } while (0)
 #endif
#endif

#ifndef GASNETE_EOP_LC
 #if GASNETE_EOP_COUNTED
  #define GASNETE_EOP_LC(_eop) \
    (gasnete_op_atomic_read(&(_eop)->completed_alc, 0) \
          == ((_eop)->initiated_alc & GASNETI_ATOMIC_MAX))
 #else // GASNETE_EOP_BOOLEAN
  #define GASNETE_EOP_LC(_eop) (LCSTATE(_eop) == LCSTATE_NONE)
 #endif
#endif

#ifndef GASNETE_EOP_MARKLC
 #if GASNETE_EOP_COUNTED
  #define GASNETE_EOP_MARKLC(_eop) do {                        \
      gasneti_assert(!GASNETE_EOP_LC(_eop));                   \
      gasnete_op_atomic_increment(&((_eop)->completed_alc), 0);\
    } while (0)
 #else // GASNETE_EOP_BOOLEAN
  #define GASNETE_EOP_MARKLC(_eop) do {      \
      gasneti_assert(!GASNETE_EOP_LC(_eop)); \
      SET_LCSTATE((_eop), LCSTATE_NONE); \
    } while (0)
 #endif
#endif

/* ------------------------------------------------------------------------------------ */
// TODO-EX: This really should move.
// However, relocating to any existing header creates a circular dependency

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
/* Reference implementation of eop and iop */
#if !GASNETI_DISABLE_REFERENCE_EOP

extern void gasnete_eop_alloc(gasnete_threaddata_t * const thread);
extern gasnete_iop_t *gasnete_iop_new(gasnete_threaddata_t * const thread);

/*  get a new op */
GASNETI_INLINE(_gasnete_eop_new)
gasnete_eop_t *_gasnete_eop_new(gasnete_threaddata_t * const thread) {
  gasnete_eop_t *eop = thread->eop_free;
  if_pf (!eop) {
    gasnete_eop_alloc(thread);
    eop = thread->eop_free;
  }
  {
    thread->eop_free = eop->next;
    gasneti_assert(OPTYPE(eop) == OPTYPE_EXPLICIT);
    gasneti_assert(EOPSTATE(eop) == EOPSTATE_FREE);
    gasneti_assert(LCSTATE(eop) ==  LCSTATE_NONE);
    gasneti_assert(eop->threadidx == thread->threadidx);
  #if GASNET_DEBUG
    // TODO-EX: this is used to assert "not-on-freelist" and should be encode differently
    SET_EOPSTATE(eop, EOPSTATE_INFLIGHT);
  #endif
  #if GASNETE_EOP_COUNTED
    gasneti_assert(GASNETE_EOP_DONE(eop));
    gasneti_assert(GASNETE_EOP_LC(eop));
  #endif
  #ifdef _GASNETE_EOP_NEW_EXTRA
    // Hook for conduit-specific initializations and assertions
    _GASNETE_EOP_NEW_EXTRA(eop);
  #endif
    return eop;
  }
}

/*  get a new op AND mark it in flight */
GASNETI_INLINE(gasnete_eop_new)
gasnete_eop_t *gasnete_eop_new(gasnete_threaddata_t * const thread) {
  gasnete_eop_t *eop = _gasnete_eop_new(thread);
#if GASNETE_EOP_BOOLEAN
  SET_EOPSTATE(eop, EOPSTATE_INFLIGHT);
#endif
#if GASNETE_EOP_COUNTED
  eop->initiated_cnt++;
#endif
#ifdef GASNETE_EOP_NEW_EXTRA
  // Hook for conduit-specific initializations and assertions
  GASNETE_EOP_NEW_EXTRA(eop);
#endif
  return eop;
}

/*  query an eop for completeness */
static
int gasnete_eop_isdone(gasnete_eop_t *eop) {
  gasneti_assert(eop->threadidx == gasnete_mythread()->threadidx);
  gasnete_eop_check(eop);
  return GASNETE_EOP_DONE(eop);
}

/*  query an iop (returned from end_nbi_accessregion) for completeness -
 *  this always means both puts and gets, and may mean LC too */
static
int gasnete_iop_isdone(gasnete_iop_t *iop) {
  int result;
  gasneti_assert(iop->threadidx == gasnete_mythread()->threadidx);
  gasnete_iop_check(iop);
  #if GASNET_DEBUG
    if (LCSTATE(iop) == LCSTATE_LIVE) // TODO-EX: better wording?
      gasneti_fatalerror("VIOLATION: attempted to call syncnb on an NBI access region handle before locally-complete");
  #endif
  result = (GASNETE_IOP_CNTDONE(iop,get) && GASNETE_IOP_CNTDONE(iop,put) &&
          ((LCSTATE(iop) == LCSTATE_NONE) || GASNETE_IOP_CNTDONE(iop,alc)));
  #if GASNET_DEBUG
    if (result) SET_LCSTATE(iop, LCSTATE_NONE);
  #endif
  return result;
}

/*  mark an op done - isget ignored for explicit ops */
static
void gasnete_op_markdone(gasnete_op_t *op, int isget) {
  if (OPTYPE(op) == OPTYPE_EXPLICIT) {
    gasnete_eop_t *eop = (gasnete_eop_t *)op;
    gasnete_eop_check(eop);
    GASNETE_EOP_MARKDONE(eop);
  } else {
    gasnete_iop_t *iop = (gasnete_iop_t *)op;
    gasnete_iop_check(iop);
    if (isget) gasnete_op_atomic_increment(&(iop->completed_get_cnt), 0);
    else gasnete_op_atomic_increment(&(iop->completed_put_cnt), 0);
  }
}

/*  free an eop */
static
void gasnete_eop_free(gasnete_eop_t *eop) {
  gasnete_threaddata_t * const thread = gasnete_threadtable[eop->threadidx];
  gasneti_assert(thread == gasnete_mythread());
  gasnete_eop_check(eop);
  gasneti_assert(GASNETE_EOP_DONE(eop));
  gasneti_assert(GASNETE_EOP_LC(eop));
#ifdef GASNETE_EOP_FREE_EXTRA
  // Hook for conduit-specific cleanups and assertions
  GASNETE_EOP_FREE_EXTRA(eop);
#endif
#if GASNET_DEBUG
  SET_EOPSTATE(eop, EOPSTATE_FREE);
#endif
  eop->next = thread->eop_free;
  thread->eop_free = eop;
}

#endif // GASNETI_DISABLE_EOP_INTERFACE
/* ------------------------------------------------------------------------------------ */

#endif

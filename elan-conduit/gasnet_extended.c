/*   $Source: /Users/kamil/work/gasnet-cvs2/gasnet/elan-conduit/Attic/gasnet_extended.c,v $
 *     $Date: 2004/10/16 19:19:49 $
 * $Revision: 1.50 $
 * Description: GASNet Extended API ELAN Implementation
 * Copyright 2002, Dan Bonachea <bonachea@cs.berkeley.edu>
 * Terms of use are as specified in license.txt
 */

#include <gasnet.h>
#include <gasnet_core_internal.h>
#include <gasnet_extended_internal.h>
#include <gasnet_internal.h>
#include <gasnet_handler.h>
#include <elan3/elan3.h> /* for ELAN_POLL_EVENT */

gasnet_node_t gasnete_mynode = (gasnet_node_t)-1;
gasnet_node_t gasnete_nodes = 0;
gasnet_seginfo_t *gasnete_seginfo = NULL;
static gasnete_threaddata_t *gasnete_threadtable[256] = { 0 };
static int gasnete_numthreads = 0;
static gasnet_hsl_t threadtable_lock = GASNET_HSL_INITIALIZER;
#if GASNETI_CLIENT_THREADS
  /* pthread thread-specific ptr to our threaddata (or NULL for a thread never-seen before) */
  static gasneti_threadkey_t gasnete_threaddata = GASNETI_THREADKEY_INITIALIZER;
#endif
static const gasnete_eopaddr_t EOPADDR_NIL = { 0xFF, 0xFF };
extern void _gasnete_iop_check(gasnete_iop_t *iop) { gasnete_iop_check(iop); }

/* 
  Basic design of the extended implementation:
  ===========================================

  gasnet_handle_t - can be either an (gasnete_op_t *) or (ELAN_EVENT *),
    as flagged by LSB

  eops - marked with the operation type - 
    always AM-based op or put/get w/ bouncebuf
  iops - completion counters for AM-based ops
    linked list of put/get eops that need bounce-bufs
    pgctrl objects for holding put/get ELAN_EVENT's for nbi

  if !GASNETE_USE_ELAN_PUTGET,
    all put/gets are done using AM (extended-ref)
    GASNETE_USE_LONG_GETS works just like in extended-ref
  otherwise...

  get_nb:
    if elan-addressable dest
      use a simple elan_get
    else if < GASNETE_MAX_COPYBUFFER_SZ and mem available
      use a elan bouncebuf dest with an eop
    else 
      use AM ref-ext med or long (if larger than 1 long, use get_nbi)

  put_nb:
    if bulk and elan-addressable src or < GASNETC_ELAN_SMALLPUTSZ (128)
      use a simple elan_put
    else if < GASNETE_MAX_COPYBUFFER_SZ and mem available
      copy to elan bouncebuf with an eop
    else 
      use AM ref-ext med or long (if larger than 1 long, use put_nbi)

  get_nbi:
    if elan-addressable dest
      use a simple elan_get and add ELAN_EVENT to pgctrl 
        (spin-poll if more than GASNETE_DEFAULT_NBI_THROTTLE(256) outstanding) 
    else if < GASNETE_MAX_COPYBUFFER_SZ and mem available
      use a elan bouncebuf dest with an eop and add to nbi linked-list
    else 
      use AM ref-ext

  put_nbi:
    if bulk and elan-addressable src or < GASNETC_ELAN_SMALLPUTSZ (128)
      use a simple elan_put and add ELAN_EVENT to pgctrl 
        (spin-poll if more than GASNETE_DEFAULT_NBI_THROTTLE(256) outstanding) 
    else if < GASNETE_MAX_COPYBUFFER_SZ and mem available
      copy to a elan bouncebuf src with an eop and add to nbi linked-list
    else 
      use AM ref-ext

  barrier:
    if !GASNETE_USE_ELAN_BARRIER
      use AM (extended ref)
    else
      register a poll callback function at startup to ensure polling 
       during hardware barrier
      if GASNETE_FAST_ELAN_BARRIER and barrier anonymous
        mismatchers report to all nodes
        hardware elan barrier
      else
        hardware broadcast id from zero
        if node detects mismatch, report to all nodes
        hardware elan barrier
*/

/* ------------------------------------------------------------------------------------ */
/*
  Tuning Parameters
  =================
*/
#define GASNETE_MAX_COPYBUFFER_SZ  1048576    /* largest temp buffer we'll allocate for put/get */
#if GASNETE_USE_PGCTRL_NBI
  static int     gasnete_pgctrl_throttle = 64;  /* limit number of concurrent DMA's (must be <= 64) */
  static E3_Addr gasnete_pgctrl_devent = 0;     /* remote elan3 event to fire */
  static int     gasnete_pgctrl_rail = 0;       /* rail to use (local or remote?) */
#else
  #ifndef GASNETE_DEFAULT_NBI_THROTTLE
    #define GASNETE_DEFAULT_NBI_THROTTLE    256
  #endif
  static int gasnete_nbi_throttle = GASNETE_DEFAULT_NBI_THROTTLE;
#endif

/* the size threshold where gets/puts stop using medium messages and start using longs */
#ifndef GASNETE_GETPUT_MEDIUM_LONG_THRESHOLD
#define GASNETE_GETPUT_MEDIUM_LONG_THRESHOLD   gasnet_AMMaxMedium()
#endif

/* true if we should try to use Long replies in gets (only possible if dest falls in segment) */
#ifndef GASNETE_USE_LONG_GETS
#define GASNETE_USE_LONG_GETS 1
#endif

/* true if we should use elan put/get (setting to zero means all put/gets use AM only) */
#ifndef GASNETE_USE_ELAN_PUTGET
#define GASNETE_USE_ELAN_PUTGET 1
#endif

/* true to use elan hardware supported barrier */
#ifndef GASNETE_USE_ELAN_BARRIER
  #define GASNETE_USE_ELAN_BARRIER 1
#endif

/* true to "bend" the rules of barrier to improve performance
   (may deadlock if threads disagree on named/anon barrier flags) */
#ifndef GASNETE_FAST_ELAN_BARRIER
  #define GASNETE_FAST_ELAN_BARRIER 1
#endif
  
/* ------------------------------------------------------------------------------------ */
#if GASNETE_USE_ELAN_BARRIER
  extern void gasnete_barrier_init();
#endif

GASNETI_IDENT(gasnete_IdentString_Version, "$GASNetExtendedLibraryVersion: " GASNET_EXTENDED_VERSION_STR " $");
#if GASNETE_USE_ELAN_PUTGET
  GASNETI_IDENT(gasnete_IdentString_ExtendedName, "$GASNetExtendedLibraryName: " GASNET_EXTENDED_NAME_STR " $");
#else
  GASNETI_IDENT(gasnete_IdentString_ExtendedName, "$GASNetExtendedLibraryName: " GASNET_EXTENDED_NAME_STR " (extended-ref) $");
#endif

/* take advantage of the fact that (ELAN_EVENT *)'s and ops are always 4-byte aligned 
   LSB of handle tells us which is in use, 0=ELAN_EVENT, 1=op
*/
#define GASNETE_ASSERT_ALIGNED(p)           gasneti_assert((((uintptr_t)(p))&0x3)==0)
#define GASNETE_HANDLE_IS_ELANEVENT(handle) (!(((uintptr_t)(handle)) & 0x1))
#define GASNETE_HANDLE_IS_OP(handle)        (((uintptr_t)(handle)) & 0x1)
#define GASNETE_OP_TO_HANDLE(op)            (GASNETE_ASSERT_ALIGNED(op), \
                                             (gasnet_handle_t)(((uintptr_t)(op)) | ((uintptr_t)0x01)))
#define GASNETE_HANDLE_TO_OP(handle)        ((gasnete_op_t *)(((uintptr_t)(handle)) & ~((uintptr_t)0x01)))
#define GASNETE_ELANEVENT_TO_HANDLE(ee)     (GASNETE_ASSERT_ALIGNED(ee), \
                                             (gasnet_handle_t)(ee))
#define GASNETE_HANDLE_TO_ELANEVENT(handle) ((ELAN_EVENT *)(handle))

/* ------------------------------------------------------------------------------------ */
/*
  Thread Management
  =================
*/
extern void gasnetc_new_threaddata_callback(void **core_threadinfo);

static gasnete_threaddata_t * gasnete_new_threaddata() {
  gasnete_threaddata_t *threaddata = NULL;
  int idx;
  gasnet_hsl_lock(&threadtable_lock);
    idx = gasnete_numthreads;
    gasnete_numthreads++;
  gasnet_hsl_unlock(&threadtable_lock);
  #if GASNETI_CLIENT_THREADS
    if (idx >= 256) gasneti_fatalerror("GASNet Extended API: Too many local client threads (limit=256)");
  #else
    gasneti_assert(idx == 0);
  #endif
  gasneti_assert(gasnete_threadtable[idx] == NULL);

  threaddata = (gasnete_threaddata_t *)gasneti_calloc(1,sizeof(gasnete_threaddata_t));

  threaddata->threadidx = idx;
  threaddata->eop_free = EOPADDR_NIL;

  gasnete_threadtable[idx] = threaddata;
  threaddata->current_iop = gasnete_iop_new(threaddata);

  /* give the core a chance to set its thread context */
  gasnetc_new_threaddata_callback(&(threaddata->gasnetc_threaddata));

  return threaddata;
}
/* PURE function (returns same value for a given thread every time) 
*/
#if GASNETI_CLIENT_THREADS
  extern gasnete_threaddata_t *gasnete_mythread() {
    gasnete_threaddata_t *threaddata = gasneti_threadkey_get(gasnete_threaddata);
    GASNETI_TRACE_EVENT(C, DYNAMIC_THREADLOOKUP);
    if_pt (threaddata) {
      gasneti_memcheck(threaddata);
      return threaddata;
    }

    /* first time we've seen this thread - need to set it up */
    threaddata = gasnete_new_threaddata();
    gasneti_threadkey_set(gasnete_threaddata, threaddata);
    return threaddata;
  }
#else
  #define gasnete_mythread() (gasnete_threadtable[0])
#endif
/* ------------------------------------------------------------------------------------ */
/*
  Initialization
  ==============
*/
/* called at startup to check configuration sanity */
static void gasnete_check_config() {
  gasneti_check_config_postattach();

  gasneti_assert_always(GASNETE_GETPUT_MEDIUM_LONG_THRESHOLD <= gasnet_AMMaxMedium());
  gasneti_assert_always(gasnete_eopaddr_isnil(EOPADDR_NIL));
}

extern void gasnete_init() {
  GASNETI_TRACE_PRINTF(C,("gasnete_init()"));
  gasneti_assert(gasnete_nodes == 0); /*  make sure we haven't been called before */

  gasnete_check_config(); /*  check for sanity */

  gasnete_mynode = gasnet_mynode();
  gasnete_nodes = gasnet_nodes();
  gasneti_assert(gasnete_nodes >= 1 && gasnete_mynode < gasnete_nodes);
  gasnete_seginfo = (gasnet_seginfo_t*)gasneti_malloc(sizeof(gasnet_seginfo_t)*gasnete_nodes);
  gasnet_getSegmentInfo(gasnete_seginfo, gasnete_nodes);

  gasnete_nbi_throttle = atoi(
    gasneti_getenv_withdefault("GASNET_NBI_THROTTLE", _STRINGIFY(GASNETE_DEFAULT_NBI_THROTTLE)));
  if (gasnete_nbi_throttle < 1) gasnete_nbi_throttle = GASNETE_DEFAULT_NBI_THROTTLE;

  { gasnete_threaddata_t *threaddata = NULL;
    gasnete_eop_t *eop = NULL;
    #if GASNETI_CLIENT_THREADS
      /* register first thread (optimization) */
      threaddata = gasnete_mythread(); 
    #else
      /* register only thread (required) */
      threaddata = gasnete_new_threaddata();
    #endif

    /* cause the first pool of eops to be allocated (optimization) */
    eop = gasnete_eop_new(threaddata, OPCAT_MEMSET);
    gasnete_op_markdone((gasnete_op_t *)eop, 0);
    gasnete_op_free((gasnete_op_t *)eop);
  }
  gasnete_barrier_init();
}

/* ------------------------------------------------------------------------------------ */
/*
  Op management
  =============
*/
/*  get a new op and mark it in flight */
gasnete_eop_t *gasnete_eop_new(gasnete_threaddata_t * const thread, uint8_t const cat) {
  gasnete_eopaddr_t head = thread->eop_free;
  if_pt (!gasnete_eopaddr_isnil(head)) {
    gasnete_eop_t *eop = GASNETE_EOPADDR_TO_PTR(thread, head);
    thread->eop_free = eop->addr;
    eop->addr = head;
    gasneti_assert(!gasnete_eopaddr_equal(thread->eop_free,head));
    gasneti_assert(eop->threadidx == thread->threadidx);
    gasneti_assert(OPTYPE(eop) == OPTYPE_EXPLICIT);
    gasneti_assert(OPTYPE(eop) == OPSTATE_FREE);
    SET_OPSTATE(eop, OPSTATE_INFLIGHT);
    SET_OPCAT(eop, cat);
    #if GASNET_DEBUG
      eop->bouncebuf = NULL;
    #endif
    return eop;
  } else { /*  free list empty - need more eops */
    int bufidx = thread->eop_num_bufs;
    gasnete_eop_t *buf;
    int i;
    gasnete_threadidx_t threadidx = thread->threadidx;
    if (bufidx == 256) gasneti_fatalerror("GASNet Extended API: Ran out of explicit handles (limit=65535)");
    thread->eop_num_bufs++;
    buf = (gasnete_eop_t *)gasneti_calloc(256,sizeof(gasnete_eop_t));
    for (i=0; i < 256; i++) {
      gasnete_eopaddr_t addr;
      addr.bufferidx = bufidx;
      #if GASNETE_SCATTER_EOPS_ACROSS_CACHELINES
        #ifdef GASNETE_EOP_MOD
          addr.eopidx = (i+32) % 255;
        #else
          { int k = i+32;
            addr.eopidx = k > 255 ? k - 255 : k;
          }
        #endif
      #else
        addr.eopidx = i+1;
      #endif
      buf[i].threadidx = threadidx;
      buf[i].addr = addr;
      #if 0 /* these can safely be skipped when the values are zero */
        SET_OPSTATE(&(buf[i]),OPSTATE_FREE); 
        SET_OPTYPE(&(buf[i]),OPTYPE_EXPLICIT); 
      #endif
    }
     /*  add a list terminator */
    #if GASNETE_SCATTER_EOPS_ACROSS_CACHELINES
      #ifdef GASNETE_EOP_MOD
        buf[223].addr.eopidx = 255; /* modular arithmetic messes up this one */
      #endif
      buf[255].addr = EOPADDR_NIL;
    #else
      buf[255].addr = EOPADDR_NIL;
    #endif
    thread->eop_bufs[bufidx] = buf;
    head.bufferidx = bufidx;
    head.eopidx = 0;
    thread->eop_free = head;

    #if GASNET_DEBUG
    { /* verify new free list got built correctly */
      int i;
      int seen[256];
      gasnete_eopaddr_t addr = thread->eop_free;

      #if 0
      if (gasnete_mynode == 0)
        for (i=0;i<256;i++) {                                   
          fprintf(stderr,"%i:  %i: next=%i\n",gasnete_mynode,i,buf[i].addr.eopidx);
          fflush(stderr);
        }
        sleep(5);
      #endif

      gasneti_memcheck(thread->eop_bufs[bufidx]);
      memset(seen, 0, 256*sizeof(int));
      for (i=0;i<(bufidx==255?255:256);i++) {                                   
        gasnete_eop_t *eop;                                   
        gasneti_assert(!gasnete_eopaddr_isnil(addr));                 
        eop = GASNETE_EOPADDR_TO_PTR(thread,addr);            
        gasneti_assert(OPTYPE(eop) == OPTYPE_EXPLICIT);               
        gasneti_assert(OPSTATE(eop) == OPSTATE_FREE);                 
        gasneti_assert(eop->threadidx == threadidx);                  
        gasneti_assert(addr.bufferidx == bufidx);
        gasneti_assert(!seen[addr.eopidx]);/* see if we hit a cycle */
        seen[addr.eopidx] = 1;
        addr = eop->addr;                                     
      }                                                       
      gasneti_assert(gasnete_eopaddr_isnil(addr)); 
    }
    #endif

    return gasnete_eop_new(thread, cat); /*  should succeed this time */
  }
}

gasnete_iop_t *gasnete_iop_new(gasnete_threaddata_t * const thread) {
  gasnete_iop_t *iop;
  if_pt (thread->iop_free) {
    iop = thread->iop_free;
    thread->iop_free = iop->next;
    gasneti_memcheck(iop);
    gasneti_assert(OPTYPE(iop) == OPTYPE_IMPLICIT);
    gasneti_assert(iop->threadidx == thread->threadidx);
  } else {
    int sz = sizeof(gasnete_iop_t);
    #if !GASNETE_USE_PGCTRL_NBI
      gasneti_assert(sizeof(gasnete_iop_t) % sizeof(void*) == 0);
      sz += 2*gasnete_nbi_throttle*sizeof(ELAN_EVENT*);
    #endif
    iop = (gasnete_iop_t *)gasneti_malloc(sz);
    SET_OPTYPE((gasnete_op_t *)iop, OPTYPE_IMPLICIT);
    iop->threadidx = thread->threadidx;
  }
  iop->next = NULL;
  iop->initiated_get_cnt = 0;
  iop->initiated_put_cnt = 0;
  gasneti_atomic_set(&(iop->completed_get_cnt), 0);
  gasneti_atomic_set(&(iop->completed_put_cnt), 0);

  #if GASNETE_USE_PGCTRL_NBI
    iop->elan_pgctrl = NULL
  #else
    iop->putctrl.evt_cnt = 0;
    iop->putctrl.evt_lst = (ELAN_EVENT *)(iop+1);
    iop->getctrl.evt_cnt = 0;
    iop->getctrl.evt_lst = iop->putctrl.evt_lst + gasnete_nbi_throttle;
  #endif

  iop->elan_putbb_list = NULL;
  iop->elan_getbb_list = NULL;

  gasnete_iop_check(iop);
  return iop;
}

static int gasnete_iop_gets_done(gasnete_iop_t *iop);
static int gasnete_iop_puts_done(gasnete_iop_t *iop);

/*  query an op for completeness - for iop this means both puts and gets */
int gasnete_op_isdone(gasnete_op_t *op, int have_elanLock) {
  gasneti_assert(op->threadidx == gasnete_mythread()->threadidx);
  if (have_elanLock)   ASSERT_ELAN_LOCKED_WEAK();
  else                 ASSERT_ELAN_UNLOCKED();
  if_pt (OPTYPE(op) == OPTYPE_EXPLICIT) {
    uint8_t cat;
    gasneti_assert(OPSTATE(op) != OPSTATE_FREE);
    gasnete_eop_check((gasnete_eop_t *)op);
    if (OPSTATE(op) == OPSTATE_COMPLETE) {
      gasneti_sync_reads();
      return TRUE;
    }
    cat = OPCAT(op);
    switch (cat) {
      case OPCAT_ELANGETBB:
      case OPCAT_ELANPUTBB: {
        int result;
        gasnete_bouncebuf_t *bb = ((gasnete_eop_t *)op)->bouncebuf;
        if (!have_elanLock) LOCK_ELAN_WEAK();
          result = elan_poll(bb->evt, 1);
          if (result) {
            if (cat == OPCAT_ELANGETBB) {
              gasneti_assert(bb->get_dest);
              memcpy(bb->get_dest, bb+1, bb->get_nbytes);
            }
            elan_free(STATE(), bb);
            #if GASNET_DEBUG
              ((gasnete_eop_t *)op)->bouncebuf = NULL;
            #endif
            SET_OPSTATE((gasnete_eop_t *)op, OPSTATE_COMPLETE);
          } 
        if (!have_elanLock) UNLOCK_ELAN_WEAK();
        /* gasneti_sync_reads() is NOT required along this path-
          we've verified by source inspection that elan_poll executes
          a read memory barrier before returning success
         */
        return result;
      }
      case OPCAT_AMGET:
      case OPCAT_AMPUT:
      case OPCAT_MEMSET:
        return FALSE;
      default: abort();
    }
  } else {
    gasnete_iop_t *iop = (gasnete_iop_t*)op;
    gasnete_iop_check(iop);
    if (gasnete_iop_gets_done(iop) && gasnete_iop_puts_done(iop)) {
      gasneti_sync_reads();
      return TRUE;
    } else return FALSE;
  }
}

/*  mark an op done - isget ignored for explicit ops */
void gasnete_op_markdone(gasnete_op_t *op, int isget) {
  if (OPTYPE(op) == OPTYPE_EXPLICIT) {
    gasnete_eop_t *eop = (gasnete_eop_t *)op;
    gasneti_assert(OPSTATE(eop) == OPSTATE_INFLIGHT);
    gasnete_eop_check(eop);
    SET_OPSTATE(eop, OPSTATE_COMPLETE);
  } else {
    gasnete_iop_t *iop = (gasnete_iop_t *)op;
    gasnete_iop_check(iop);
    if (isget) gasneti_atomic_increment(&(iop->completed_get_cnt));
    else gasneti_atomic_increment(&(iop->completed_put_cnt));
  }
}

/*  free an op */
void gasnete_op_free(gasnete_op_t *op) {
  gasnete_threaddata_t * const thread = gasnete_threadtable[op->threadidx];
  gasneti_assert(thread == gasnete_mythread());
  if (OPTYPE(op) == OPTYPE_EXPLICIT) {
    gasnete_eop_t *eop = (gasnete_eop_t *)op;
    gasnete_eopaddr_t addr = eop->addr;
    gasneti_assert(OPSTATE(eop) == OPSTATE_COMPLETE);
    gasnete_eop_check(eop);
    SET_OPSTATE(eop, OPSTATE_FREE);
    eop->addr = thread->eop_free;
    thread->eop_free = addr;
  } else {
    gasnete_iop_t *iop = (gasnete_iop_t *)op;
    gasnete_iop_check(iop);
    gasneti_assert(iop->next == NULL);
    iop->next = thread->iop_free;
    thread->iop_free = iop;
  }
}

/* ------------------------------------------------------------------------------------ */
/*
  Non-blocking memory-to-memory transfers (explicit handle)
  ==========================================================
*/
/* ------------------------------------------------------------------------------------ */
GASNET_INLINE_MODIFIER(gasnete_get_reqh_inner)
void gasnete_get_reqh_inner(gasnet_token_t token, 
  gasnet_handlerarg_t nbytes, void *dest, void *src, void *op) {
  gasneti_assert(nbytes <= gasnet_AMMaxMedium());
  GASNETE_SAFE(
    MEDIUM_REP(2,4,(token, gasneti_handleridx(gasnete_get_reph),
                  src, nbytes, 
                  PACK(dest), PACK(op))));
}
SHORT_HANDLER(gasnete_get_reqh,4,7, 
              (token, a0, UNPACK(a1),      UNPACK(a2),      UNPACK(a3)     ),
              (token, a0, UNPACK2(a1, a2), UNPACK2(a3, a4), UNPACK2(a5, a6)));
/* ------------------------------------------------------------------------------------ */
GASNET_INLINE_MODIFIER(gasnete_get_reph_inner)
void gasnete_get_reph_inner(gasnet_token_t token, 
  void *addr, size_t nbytes,
  void *dest, void *op) {
  GASNETE_FAST_UNALIGNED_MEMCPY(dest, addr, nbytes);
  gasneti_sync_writes();
  gasnete_op_markdone((gasnete_op_t *)op, 1);
}
MEDIUM_HANDLER(gasnete_get_reph,2,4,
              (token,addr,nbytes, UNPACK(a0),      UNPACK(a1)    ),
              (token,addr,nbytes, UNPACK2(a0, a1), UNPACK2(a2, a3)));
/* ------------------------------------------------------------------------------------ */
GASNET_INLINE_MODIFIER(gasnete_getlong_reqh_inner)
void gasnete_getlong_reqh_inner(gasnet_token_t token, 
  gasnet_handlerarg_t nbytes, void *dest, void *src, void *op) {

  GASNETE_SAFE(
    LONG_REP(1,2,(token, gasneti_handleridx(gasnete_getlong_reph),
                  src, nbytes, dest,
                  PACK(op))));
}
SHORT_HANDLER(gasnete_getlong_reqh,4,7, 
              (token, a0, UNPACK(a1),      UNPACK(a2),      UNPACK(a3)     ),
              (token, a0, UNPACK2(a1, a2), UNPACK2(a3, a4), UNPACK2(a5, a6)));
/* ------------------------------------------------------------------------------------ */
GASNET_INLINE_MODIFIER(gasnete_getlong_reph_inner)
void gasnete_getlong_reph_inner(gasnet_token_t token, 
  void *addr, size_t nbytes, 
  void *op) {
  gasneti_sync_writes();
  gasnete_op_markdone((gasnete_op_t *)op, 1);
}
LONG_HANDLER(gasnete_getlong_reph,1,2,
              (token,addr,nbytes, UNPACK(a0)     ),
              (token,addr,nbytes, UNPACK2(a0, a1)));
/* ------------------------------------------------------------------------------------ */
GASNET_INLINE_MODIFIER(gasnete_put_reqh_inner)
void gasnete_put_reqh_inner(gasnet_token_t token, 
  void *addr, size_t nbytes,
  void *dest, void *op) {
  GASNETE_FAST_UNALIGNED_MEMCPY(dest, addr, nbytes);
  gasneti_sync_writes();
  GASNETE_SAFE(
    SHORT_REP(1,2,(token, gasneti_handleridx(gasnete_markdone_reph),
                  PACK(op))));
}
MEDIUM_HANDLER(gasnete_put_reqh,2,4, 
              (token,addr,nbytes, UNPACK(a0),      UNPACK(a1)     ),
              (token,addr,nbytes, UNPACK2(a0, a1), UNPACK2(a2, a3)));
/* ------------------------------------------------------------------------------------ */
GASNET_INLINE_MODIFIER(gasnete_putlong_reqh_inner)
void gasnete_putlong_reqh_inner(gasnet_token_t token, 
  void *addr, size_t nbytes,
  void *op) {
  gasneti_sync_writes();
  GASNETE_SAFE(
    SHORT_REP(1,2,(token, gasneti_handleridx(gasnete_markdone_reph),
                  PACK(op))));
}
LONG_HANDLER(gasnete_putlong_reqh,1,2, 
              (token,addr,nbytes, UNPACK(a0)     ),
              (token,addr,nbytes, UNPACK2(a0, a1)));
/* ------------------------------------------------------------------------------------ */
GASNET_INLINE_MODIFIER(gasnete_memset_reqh_inner)
void gasnete_memset_reqh_inner(gasnet_token_t token, 
  gasnet_handlerarg_t val, gasnet_handlerarg_t nbytes, void *dest, void *op) {
  memset(dest, (int)(uint32_t)val, nbytes);
  gasneti_sync_writes();
  GASNETE_SAFE(
    SHORT_REP(1,2,(token, gasneti_handleridx(gasnete_markdone_reph),
                  PACK(op))));
}
SHORT_HANDLER(gasnete_memset_reqh,4,6,
              (token, a0, a1, UNPACK(a2),      UNPACK(a3)     ),
              (token, a0, a1, UNPACK2(a2, a3), UNPACK2(a4, a5)));
/* ------------------------------------------------------------------------------------ */
GASNET_INLINE_MODIFIER(gasnete_markdone_reph_inner)
void gasnete_markdone_reph_inner(gasnet_token_t token, 
  void *op) {
  gasnete_op_markdone((gasnete_op_t *)op, 0); /*  assumes this is a put or explicit */
}
SHORT_HANDLER(gasnete_markdone_reph,1,2,
              (token, UNPACK(a0)    ),
              (token, UNPACK2(a0, a1)));
/* ------------------------------------------------------------------------------------ */
extern gasnet_handle_t gasnete_get_nb_bulk (void *dest, gasnet_node_t node, void *src, size_t nbytes GASNETE_THREAD_FARG) {
#if GASNETE_USE_ELAN_PUTGET
  LOCK_ELAN_WEAK();
  #if GASNET_SEGMENT_EVERYTHING
    if (!elan_addressable(STATE(),src,nbytes)) {
      UNLOCK_ELAN_WEAK();
      GASNETI_TRACE_PRINTF(I,("Warning: get source not elan-mapped, using AM instead"));
    } else 
  #endif
  if (elan_addressable(STATE(),dest,nbytes)) { 
    ELAN_EVENT *evt;
    evt = elan_get(STATE(), src, dest, nbytes, node);
    UNLOCK_ELAN_WEAK();
    GASNETI_TRACE_EVENT_VAL(C,GET_DIRECT,nbytes);
    gasneti_assert(evt);
    return GASNETE_ELANEVENT_TO_HANDLE(evt);
  } else if (nbytes <= GASNETE_MAX_COPYBUFFER_SZ) { /* use a bounce buffer */
    gasnete_bouncebuf_t *bouncebuf;
    bouncebuf = (gasnete_bouncebuf_t *)elan_allocMain(STATE(), 64, sizeof(gasnete_bouncebuf_t)+nbytes);
    if_pt (bouncebuf) {
      ELAN_EVENT *evt;
      gasnete_eop_t *eop;
      gasneti_assert(elan_addressable(STATE(),bouncebuf,sizeof(gasnete_bouncebuf_t)+nbytes));
      evt = elan_get(STATE(), src, bouncebuf+1, nbytes, node);
      UNLOCK_ELAN_WEAK();
      GASNETI_TRACE_EVENT_VAL(C,GET_BUFFERED,nbytes);
      eop = gasnete_eop_new(GASNETE_MYTHREAD, OPCAT_ELANGETBB);
      bouncebuf->evt = evt;
      #if GASNET_DEBUG
        bouncebuf->next = NULL;
      #endif
      bouncebuf->get_dest = dest;
      bouncebuf->get_nbytes = nbytes;
      eop->bouncebuf = bouncebuf;
      return GASNETE_OP_TO_HANDLE(eop);
    } else {
      UNLOCK_ELAN_WEAK();
      GASNETI_TRACE_EVENT(C, EXHAUSTED_ELAN_MEMORY);
      GASNETI_TRACE_PRINTF(I,("Warning: Elan conduit exhausted the main memory heap trying to get a bounce buffer, using AM instead"));
    }
  } else UNLOCK_ELAN_WEAK();
#endif

  /* use AM */
  if (nbytes <= GASNETE_GETPUT_MEDIUM_LONG_THRESHOLD) {
    gasnete_eop_t *op = gasnete_eop_new(GASNETE_MYTHREAD, OPCAT_AMGET);

    GASNETE_SAFE(
      SHORT_REQ(4,7,(node, gasneti_handleridx(gasnete_get_reqh), 
                   (gasnet_handlerarg_t)nbytes, PACK(dest), PACK(src), PACK(op))));
    GASNETI_TRACE_EVENT_VAL(C,GET_AMMEDIUM,nbytes);

    return GASNETE_OP_TO_HANDLE(op);
  } else {
    /*  need many messages - use an access region to coalesce them into a single handle */
    /*  (note this relies on the fact that our implementation of access regions allows recursion) */
    gasnete_begin_nbi_accessregion(1 /* enable recursion */ GASNETE_THREAD_PASS);
    gasnete_get_nbi_bulk(dest, node, src, nbytes GASNETE_THREAD_PASS);
    return gasnete_end_nbi_accessregion(GASNETE_THREAD_PASS_ALONE);
  }
}

GASNET_INLINE_MODIFIER(gasnete_put_nb_inner)
gasnet_handle_t gasnete_put_nb_inner(gasnet_node_t node, void *dest, void *src, size_t nbytes, int isbulk GASNETE_THREAD_FARG) {
#if GASNETE_USE_ELAN_PUTGET
  LOCK_ELAN_WEAK();
  #if GASNET_SEGMENT_EVERYTHING
    if (!elan_addressable(STATE(),dest,nbytes)) {
      UNLOCK_ELAN_WEAK();
      GASNETI_TRACE_PRINTF(I,("Warning: put destination not elan-mapped, using AM instead"));
    } else 
  #endif
  if (nbytes <= GASNETC_ELAN_SMALLPUTSZ || 
    (isbulk && elan_addressable(STATE(),src,nbytes))) { 
    /* legal to use ordinary elan_put */
    ELAN_EVENT *evt;
      evt = elan_put(STATE(), src, dest, nbytes, node);
    UNLOCK_ELAN_WEAK();
    if (isbulk) GASNETI_TRACE_EVENT_VAL(C,PUT_BULK_DIRECT,nbytes);
    else        GASNETI_TRACE_EVENT_VAL(C,PUT_DIRECT,nbytes);
    gasneti_assert(evt);
    return GASNETE_ELANEVENT_TO_HANDLE(evt);
  } else if (nbytes <= GASNETE_MAX_COPYBUFFER_SZ) { /* use a bounce buffer */
    gasnete_bouncebuf_t *bouncebuf;
    /* TODO: it would be nice if our bounce buffers could reside in SDRAM, 
        but not sure the elan_put interface can handle it
     */
    bouncebuf = (gasnete_bouncebuf_t *)elan_allocMain(STATE(), 64, sizeof(gasnete_bouncebuf_t)+nbytes);
    if_pt (bouncebuf) {
      ELAN_EVENT *evt;
      gasnete_eop_t *eop;
      memcpy(bouncebuf+1, src, nbytes);
      gasneti_assert(elan_addressable(STATE(),bouncebuf,sizeof(gasnete_bouncebuf_t)+nbytes));
      /* TODO: this gets a "not-addressable" elan exception on dual-rail runs - why? */
      evt = elan_put(STATE(), bouncebuf+1, dest, nbytes, node);
      UNLOCK_ELAN_WEAK();
      if (isbulk) GASNETI_TRACE_EVENT_VAL(C,PUT_BULK_BUFFERED,nbytes);
      else        GASNETI_TRACE_EVENT_VAL(C,PUT_BUFFERED,nbytes);
      eop = gasnete_eop_new(GASNETE_MYTHREAD, OPCAT_ELANPUTBB);
      bouncebuf->evt = evt;
      #if GASNET_DEBUG
        bouncebuf->next = NULL;
        bouncebuf->get_dest = NULL;
        bouncebuf->get_nbytes = 0;
      #endif
      eop->bouncebuf = bouncebuf;
      return GASNETE_OP_TO_HANDLE(eop);
    } else {
      UNLOCK_ELAN_WEAK();
      GASNETI_TRACE_EVENT(C, EXHAUSTED_ELAN_MEMORY);
      GASNETI_TRACE_PRINTF(I,("Warning: Elan conduit exhausted the main memory heap trying to get a bounce buffer, using AM instead"));
    }
  } else UNLOCK_ELAN_WEAK();
#endif

  /* use AM */
  if (nbytes <= GASNETE_GETPUT_MEDIUM_LONG_THRESHOLD) {
    gasnete_eop_t *op = gasnete_eop_new(GASNETE_MYTHREAD, OPCAT_AMPUT);

    GASNETE_SAFE(
      MEDIUM_REQ(2,4,(node, gasneti_handleridx(gasnete_put_reqh),
                    src, nbytes,
                    PACK(dest), PACK(op))));
    if (isbulk) GASNETI_TRACE_EVENT_VAL(C,PUT_BULK_AMMEDIUM,nbytes);
    else        GASNETI_TRACE_EVENT_VAL(C,PUT_AMMEDIUM,nbytes);

    return GASNETE_OP_TO_HANDLE(op);
  } else if (nbytes <= gasnet_AMMaxLongRequest()) {
    gasnete_eop_t *op = gasnete_eop_new(GASNETE_MYTHREAD, OPCAT_AMPUT);

    if (isbulk) {
      GASNETE_SAFE(
        LONGASYNC_REQ(1,2,(node, gasneti_handleridx(gasnete_putlong_reqh),
                    src, nbytes, dest,
                    PACK(op))));
      GASNETI_TRACE_EVENT_VAL(C,PUT_BULK_AMLONG,nbytes);
    } else {
      GASNETE_SAFE(
        LONG_REQ(1,2,(node, gasneti_handleridx(gasnete_putlong_reqh),
                    src, nbytes, dest,
                    PACK(op))));
      GASNETI_TRACE_EVENT_VAL(C,PUT_AMLONG,nbytes);
    }

    return GASNETE_OP_TO_HANDLE(op);
  } else { 
    /*  need many messages - use an access region to coalesce them into a single handle */
    /*  (note this relies on the fact that our implementation of access regions allows recursion) */
    gasnete_begin_nbi_accessregion(1 /* enable recursion */ GASNETE_THREAD_PASS);
      if (isbulk) gasnete_put_nbi_bulk(node, dest, src, nbytes GASNETE_THREAD_PASS);
      else        gasnete_put_nbi    (node, dest, src, nbytes GASNETE_THREAD_PASS);
    return gasnete_end_nbi_accessregion(GASNETE_THREAD_PASS_ALONE);
  }
}

extern gasnet_handle_t gasnete_put_nb      (gasnet_node_t node, void *dest, void *src, size_t nbytes GASNETE_THREAD_FARG) {
  return gasnete_put_nb_inner(node, dest, src, nbytes, 0 GASNETE_THREAD_PASS);
}

extern gasnet_handle_t gasnete_put_nb_bulk (gasnet_node_t node, void *dest, void *src, size_t nbytes GASNETE_THREAD_FARG) {
  return gasnete_put_nb_inner(node, dest, src, nbytes, 1 GASNETE_THREAD_PASS);
}

extern gasnet_handle_t gasnete_memset_nb   (gasnet_node_t node, void *dest, int val, size_t nbytes GASNETE_THREAD_FARG) {
  gasnete_eop_t *op = gasnete_eop_new(GASNETE_MYTHREAD, OPCAT_MEMSET);

  GASNETE_SAFE(
    SHORT_REQ(4,6,(node, gasneti_handleridx(gasnete_memset_reqh),
                 (gasnet_handlerarg_t)val, (gasnet_handlerarg_t)nbytes,
                 PACK(dest), PACK(op))));

  return GASNETE_OP_TO_HANDLE(op);
}

/* ------------------------------------------------------------------------------------ */
/*
  Synchronization for explicit-handle non-blocking operations:
  ===========================================================
*/
GASNET_INLINE_MODIFIER(gasnete_try_syncnb_inner)
int gasnete_try_syncnb_inner(gasnet_handle_t handle) {
  ASSERT_ELAN_UNLOCKED();
  if (GASNETE_HANDLE_IS_OP(handle)) {
    gasnete_op_t *op = GASNETE_HANDLE_TO_OP(handle);
    if (gasnete_op_isdone(op, FALSE)) {
      gasnete_op_free(op);
      return GASNET_OK;
    }
    else return GASNET_ERR_NOT_READY;
  } else {
    ELAN_EVENT *evt = GASNETE_HANDLE_TO_ELANEVENT(handle);
    int result;
    LOCK_ELAN_WEAK();
      result = elan_poll(evt, 1);
    UNLOCK_ELAN_WEAK();

    if (result) return GASNET_OK;
    else return GASNET_ERR_NOT_READY;
  }
}

extern int  gasnete_try_syncnb(gasnet_handle_t handle) {
  GASNETE_SAFE(gasneti_AMPoll());
  return gasnete_try_syncnb_inner(handle);
}

extern int  gasnete_try_syncnb_some (gasnet_handle_t *phandle, size_t numhandles) {
  int success = 0;
  int empty = 1;
  GASNETE_SAFE(gasneti_AMPoll());

  gasneti_assert(phandle);

  { int i;
    for (i = 0; i < numhandles; i++) {
      gasnet_handle_t handle = phandle[i];
      if (handle != GASNET_INVALID_HANDLE) {
        empty = 0;
        /* could rewrite this to reduce contention for weak elan lock,
           but the important thing is we only poll once
        */
        if (gasnete_try_syncnb_inner(handle) == GASNET_OK) { 
          phandle[i] = GASNET_INVALID_HANDLE;
          success = 1;
        }  
      }
    }
  }

  if (success || empty) return GASNET_OK;
  else return GASNET_ERR_NOT_READY;
}

extern int  gasnete_try_syncnb_all (gasnet_handle_t *phandle, size_t numhandles) {
  int success = 1;
  GASNETE_SAFE(gasneti_AMPoll());

  gasneti_assert(phandle);

  { int i;
    for (i = 0; i < numhandles; i++) {
      gasnet_handle_t handle = phandle[i];
      if (handle != GASNET_INVALID_HANDLE) {
        /* could rewrite this to reduce contention for weak elan lock,
           but the important thing is we only poll once
        */
        if (gasnete_try_syncnb_inner(handle) == GASNET_OK) {
          phandle[i] = GASNET_INVALID_HANDLE;
        } else success = 0;
      }
    }
  }

  if (success) return GASNET_OK;
  else return GASNET_ERR_NOT_READY;
}

/* ------------------------------------------------------------------------------------ */
/*
  Non-blocking memory-to-memory transfers (implicit handle)
  ==========================================================
  each message sends an ack - we count the number of implicit ops launched and compare
    with the number acknowledged
  Another possible design would be to eliminate some of the acks (at least for puts) 
    by piggybacking them on other messages (like get replies) or simply aggregating them
    the target until the source tries to synchronize
*/
/* return true iff a pgctrl has completed all ops - assumes elan lock held */
static int gasnete_putgetctrl_done(gasnete_putgetctrl *pgctrl) {
  int i;
  ASSERT_ELAN_LOCKED_WEAK();
  gasneti_assert(pgctrl && pgctrl->evt_cnt >= 0);
  for (i = 0; i < pgctrl->evt_cnt; i++) {
    if (elan_poll(pgctrl->evt_lst[i], 1)) {
      pgctrl->evt_cnt--;
      pgctrl->evt_lst[i] = pgctrl->evt_lst[pgctrl->evt_cnt];
      i--;
    }
  }
  return (pgctrl->evt_cnt == 0);
}

/* add a put or get to the control - assumes elan lock held */
static void gasnete_putgetctrl_save(gasnete_putgetctrl *pgctrl, ELAN_EVENT *evt) {
  ELAN_EVENT ** evt_lst;
  ASSERT_ELAN_LOCKED_WEAK();
  gasneti_assert(pgctrl && evt && pgctrl->evt_cnt <= gasnete_nbi_throttle);
  evt_lst = pgctrl->evt_lst;
  while (pgctrl->evt_cnt == gasnete_nbi_throttle) {
    int i;
    for (i=0; i < pgctrl->evt_cnt; i++) {
      if (elan_poll(evt_lst[i], 1)) {
        pgctrl->evt_cnt--;
        evt_lst[i] = evt_lst[pgctrl->evt_cnt];
        i--;
      }
    }
    if (pgctrl->evt_cnt == gasnete_nbi_throttle) {
      UNLOCKRELOCK_ELAN_WEAK(gasneti_AMPoll());
    }
  }
  evt_lst[pgctrl->evt_cnt] = evt;
  pgctrl->evt_cnt++;
}
/* return a list containing some pending putgetbb eops (NULL if done) - assumes elan lock held */
static gasnete_eop_t * gasnete_putgetbblist_pending(gasnete_eop_t *eoplist) {
  ASSERT_ELAN_LOCKED_WEAK();
  while (eoplist) {
    gasnete_op_t *op = (gasnete_op_t *)eoplist;
    gasnete_eop_t *next;
    gasneti_assert(OPCAT(op) == OPCAT_ELANGETBB || OPCAT(op) == OPCAT_ELANPUTBB);
    gasneti_assert(eoplist->bouncebuf);
    next = eoplist->bouncebuf->next;
    if (gasnete_op_isdone(op, TRUE)) {
      gasnete_op_free(op);
    }
    else 
      return eoplist; /* stop when we find the first pending one */
    eoplist = next;
  }
  return NULL;
}

extern void gasnete_get_nbi_bulk (void *dest, gasnet_node_t node, void *src, size_t nbytes GASNETE_THREAD_FARG) {
  gasnete_threaddata_t * const mythread = GASNETE_MYTHREAD;
  gasnete_iop_t * const iop = mythread->current_iop;
#if GASNETE_USE_ELAN_PUTGET
  LOCK_ELAN_WEAK();
  #if GASNET_SEGMENT_EVERYTHING
    if (!elan_addressable(STATE(),src,nbytes)) {
      UNLOCK_ELAN_WEAK();
      GASNETI_TRACE_PRINTF(I,("Warning: get source not elan-mapped, using AM instead"));
    } else 
  #endif
  if (elan_addressable(STATE(),dest,nbytes)) { 
    ELAN_EVENT *evt;
    #if GASNETE_USE_PGCTRL_NBI
      if (!iop->elan_pgctrl) 
        iop->elan_pgctrl = elan_putgetInit(STATE(), GASNETC_ELAN_SMALLPUTSZ, gasnete_pgctrl_throttle);
      evt = elan_doget(iop->elan_pgctrl, src, dest, nbytes, node, gasnete_pgctrl_rail);
      gasneti_assert(evt);
    #else
      evt = elan_get(STATE(), src, dest, nbytes, node);
      gasneti_assert(evt);
      gasnete_putgetctrl_save(&(iop->getctrl),evt);
    #endif
    UNLOCK_ELAN_WEAK();
    GASNETI_TRACE_EVENT_VAL(C,GET_DIRECT,nbytes);
    return;
  } else if (nbytes <= GASNETE_MAX_COPYBUFFER_SZ) { /* use a bounce buffer */
    gasnete_bouncebuf_t *bouncebuf;
    tryagain:
    bouncebuf = (gasnete_bouncebuf_t *)elan_allocMain(STATE(), 64, sizeof(gasnete_bouncebuf_t)+nbytes);
    if_pt (bouncebuf) {
      ELAN_EVENT *evt;
      gasnete_eop_t *eop;
      gasneti_assert(elan_addressable(STATE(),bouncebuf,sizeof(gasnete_bouncebuf_t)+nbytes));
      evt = elan_get(STATE(), src, bouncebuf+1, nbytes, node);
      UNLOCK_ELAN_WEAK();
      GASNETI_TRACE_EVENT_VAL(C,GET_BUFFERED,nbytes);
      eop = gasnete_eop_new(GASNETE_MYTHREAD, OPCAT_ELANGETBB);
      bouncebuf->evt = evt;
      bouncebuf->get_dest = dest;
      bouncebuf->get_nbytes = nbytes;
      eop->bouncebuf = bouncebuf;

      bouncebuf->next = iop->elan_getbb_list;
      iop->elan_getbb_list = eop;
      return;
    } else { /* alloc failed - out of elan memory */
      UNLOCKRELOCK_ELAN_WEAK_IFTRACE(GASNETI_TRACE_EVENT(C, EXHAUSTED_ELAN_MEMORY));
      #if !GASNETE_USE_PGCTRL_NBI
        /* try to reclaim some memory by reaping nbi eops before we punt to AM */
        if (iop->elan_getbb_list || iop->elan_putbb_list) {
          if (((iop->elan_getbb_list = gasnete_putgetbblist_pending(iop->elan_getbb_list)) != NULL) 
               | /* don't want short-circuit || evaluation here */
              ((iop->elan_putbb_list = gasnete_putgetbblist_pending(iop->elan_putbb_list)) != NULL)) {
            UNLOCKRELOCK_ELAN_WEAK(gasneti_AMPoll()); /* prevent deadlock */
          }
          goto tryagain;
        }
      #endif
      UNLOCK_ELAN_WEAK();
      GASNETI_TRACE_PRINTF(I,("Warning: Elan conduit exhausted the main memory heap trying to get a bounce buffer, using AM instead"));
    }
  } else UNLOCK_ELAN_WEAK();
#endif

  /* use AM */
  if (nbytes <= GASNETE_GETPUT_MEDIUM_LONG_THRESHOLD) {
    iop->initiated_get_cnt++;
  
    GASNETE_SAFE(
      SHORT_REQ(4,7,(node, gasneti_handleridx(gasnete_get_reqh), 
                   (gasnet_handlerarg_t)nbytes, PACK(dest), PACK(src), PACK(iop))));
    GASNETI_TRACE_EVENT_VAL(C,GET_AMMEDIUM,nbytes);
    return;
  } else {
    int chunksz;
    gasnet_handler_t reqhandler;
    uint8_t *psrc = src;
    uint8_t *pdest = dest;
    #if GASNETE_USE_LONG_GETS
      /* TODO: optimize this check by caching segment upper-bound in gasnete_seginfo */
      gasneti_memcheck(gasnete_seginfo);
      if (dest >= gasnete_seginfo[gasnete_mynode].addr &&
         (((uintptr_t)dest) + nbytes) < 
          (((uintptr_t)gasnete_seginfo[gasnete_mynode].addr) +
                       gasnete_seginfo[gasnete_mynode].size)) {
        chunksz = gasnet_AMMaxLongReply();
        reqhandler = gasneti_handleridx(gasnete_getlong_reqh);
        GASNETI_TRACE_EVENT_VAL(C,GET_AMLONG,nbytes);
      }
      else 
    #endif
      { reqhandler = gasneti_handleridx(gasnete_get_reqh);
        chunksz = gasnet_AMMaxMedium();
        GASNETI_TRACE_EVENT_VAL(C,GET_AMMEDIUM,nbytes);
      }
    for (;;) {
      iop->initiated_get_cnt++;
      if (nbytes > chunksz) {
        GASNETE_SAFE(
          SHORT_REQ(4,7,(node, reqhandler, 
                       (gasnet_handlerarg_t)chunksz, PACK(pdest), PACK(psrc), PACK(iop))));
        nbytes -= chunksz;
        psrc += chunksz;
        pdest += chunksz;
      } else {
        GASNETE_SAFE(
          SHORT_REQ(4,7,(node, reqhandler, 
                       (gasnet_handlerarg_t)nbytes, PACK(pdest), PACK(psrc), PACK(iop))));
        break;
      }
    }
    return;
  }
}

GASNET_INLINE_MODIFIER(gasnete_put_nbi_inner)
void gasnete_put_nbi_inner(gasnet_node_t node, void *dest, void *src, size_t nbytes, int isbulk GASNETE_THREAD_FARG) {
  gasnete_threaddata_t * const mythread = GASNETE_MYTHREAD;
  gasnete_iop_t * const iop = mythread->current_iop;

#if GASNETE_USE_ELAN_PUTGET
  LOCK_ELAN_WEAK();
  #if GASNET_SEGMENT_EVERYTHING
    if (!elan_addressable(STATE(),dest,nbytes)) {
      UNLOCK_ELAN_WEAK();
      GASNETI_TRACE_PRINTF(I,("Warning: put destination not elan-mapped, using AM instead"));
    } else 
  #endif
  if (nbytes <= GASNETC_ELAN_SMALLPUTSZ || 
    (isbulk && elan_addressable(STATE(),src,nbytes))) { 
    /* legal to use ordinary elan_put */
    ELAN_EVENT *evt;
    #if GASNETE_USE_PGCTRL_NBI
      if (!iop->elan_pgctrl) 
        iop->elan_pgctrl = elan_putgetInit(STATE(), GASNETC_ELAN_SMALLPUTSZ, gasnete_pgctrl_throttle);
      evt = elan_doput(iop->elan_pgctrl, src, dest, gasnete_pgctrl_devent, nbytes, node, gasnete_pgctrl_rail);
      gasneti_assert(evt);
    #else
      evt = elan_put(STATE(), src, dest, nbytes, node);
      gasneti_assert(evt);
      gasnete_putgetctrl_save(&(iop->putctrl),evt);
    #endif
    UNLOCK_ELAN_WEAK();
    if (isbulk) GASNETI_TRACE_EVENT_VAL(C,PUT_BULK_DIRECT,nbytes);
    else        GASNETI_TRACE_EVENT_VAL(C,PUT_DIRECT,nbytes);
    return;
  } else if (nbytes <= GASNETE_MAX_COPYBUFFER_SZ) { /* use a bounce buffer */
    gasnete_bouncebuf_t *bouncebuf;
    tryagain:
    bouncebuf = (gasnete_bouncebuf_t *)elan_allocMain(STATE(), 64, sizeof(gasnete_bouncebuf_t)+nbytes);
    if_pt (bouncebuf) {
      ELAN_EVENT *evt;
      gasnete_eop_t *eop;
      memcpy(bouncebuf+1, src, nbytes);
      gasneti_assert(elan_addressable(STATE(),bouncebuf,sizeof(gasnete_bouncebuf_t)+nbytes));
      evt = elan_put(STATE(), bouncebuf+1, dest, nbytes, node);
      UNLOCK_ELAN_WEAK();
      if (isbulk) GASNETI_TRACE_EVENT_VAL(C,PUT_BULK_BUFFERED,nbytes);
      else        GASNETI_TRACE_EVENT_VAL(C,PUT_BUFFERED,nbytes);
      eop = gasnete_eop_new(GASNETE_MYTHREAD, OPCAT_ELANPUTBB);
      bouncebuf->evt = evt;
      #if GASNET_DEBUG
        bouncebuf->get_dest = NULL;
        bouncebuf->get_nbytes = 0;
      #endif
      eop->bouncebuf = bouncebuf;

      bouncebuf->next = iop->elan_putbb_list;
      iop->elan_putbb_list = eop;
      return;
    } else { /* alloc failed - out of elan memory */
      UNLOCKRELOCK_ELAN_WEAK_IFTRACE(GASNETI_TRACE_EVENT(C, EXHAUSTED_ELAN_MEMORY));
      #if !GASNETE_USE_PGCTRL_NBI
        /* try to reclaim some memory by reaping nbi eops before we punt to AM */
        if (iop->elan_getbb_list || iop->elan_putbb_list) {
          if (((iop->elan_getbb_list = gasnete_putgetbblist_pending(iop->elan_getbb_list)) != NULL) 
               | /* don't want short-circuit || evaluation here */
              ((iop->elan_putbb_list = gasnete_putgetbblist_pending(iop->elan_putbb_list)) != NULL)) {
            UNLOCKRELOCK_ELAN_WEAK(gasneti_AMPoll()); /* prevent deadlock */
          }
          goto tryagain;
        }
      #endif
      UNLOCK_ELAN_WEAK();
      GASNETI_TRACE_PRINTF(I,("Warning: Elan conduit exhausted the main memory heap trying to get a bounce buffer, using AM instead"));
    }
  } else UNLOCK_ELAN_WEAK();
#endif

  /* use AM */
  if (nbytes <= GASNETE_GETPUT_MEDIUM_LONG_THRESHOLD) {
    iop->initiated_put_cnt++;

    GASNETE_SAFE(
      MEDIUM_REQ(2,4,(node, gasneti_handleridx(gasnete_put_reqh),
                    src, nbytes,
                    PACK(dest), PACK(iop))));
    if (isbulk) GASNETI_TRACE_EVENT_VAL(C,PUT_BULK_AMMEDIUM,nbytes);
    else        GASNETI_TRACE_EVENT_VAL(C,PUT_AMMEDIUM,nbytes);
    return;
  } else if (nbytes <= gasnet_AMMaxLongRequest()) {
    iop->initiated_put_cnt++;

    if (isbulk) {
      GASNETE_SAFE(
        LONGASYNC_REQ(1,2,(node, gasneti_handleridx(gasnete_putlong_reqh),
                      src, nbytes, dest,
                      PACK(iop))));
      GASNETI_TRACE_EVENT_VAL(C,PUT_BULK_AMLONG,nbytes);
    } else {
      GASNETE_SAFE(
        LONG_REQ(1,2,(node, gasneti_handleridx(gasnete_putlong_reqh),
                      src, nbytes, dest,
                      PACK(iop))));
      GASNETI_TRACE_EVENT_VAL(C,PUT_AMLONG,nbytes);
    }

    return;
  } else {
    int chunksz = gasnet_AMMaxLongRequest();
    uint8_t *psrc = src;
    uint8_t *pdest = dest;
    if (isbulk) GASNETI_TRACE_EVENT_VAL(C,PUT_BULK_AMLONG,nbytes);
    else        GASNETI_TRACE_EVENT_VAL(C,PUT_AMLONG,nbytes);
    for (;;) {
      iop->initiated_put_cnt++;
      if (nbytes > chunksz) {
        if (isbulk) {
          GASNETE_SAFE(
            LONGASYNC_REQ(1,2,(node, gasneti_handleridx(gasnete_putlong_reqh),
                          psrc, chunksz, pdest,
                          PACK(iop))));
        } else {
          GASNETE_SAFE(
            LONG_REQ(1,2,(node, gasneti_handleridx(gasnete_putlong_reqh),
                          psrc, chunksz, pdest,
                          PACK(iop))));
        }
        nbytes -= chunksz;
        psrc += chunksz;
        pdest += chunksz;
      } else {
        if (isbulk) {
          GASNETE_SAFE(
            LONGASYNC_REQ(1,2,(node, gasneti_handleridx(gasnete_putlong_reqh),
                          psrc, nbytes, pdest,
                          PACK(iop))));
        } else {
          GASNETE_SAFE(
            LONG_REQ(1,2,(node, gasneti_handleridx(gasnete_putlong_reqh),
                          psrc, nbytes, pdest,
                          PACK(iop))));
        }
        break;
      }
    }
    return;
  }
}

extern void gasnete_put_nbi      (gasnet_node_t node, void *dest, void *src, size_t nbytes GASNETE_THREAD_FARG) {
  gasnete_put_nbi_inner(node, dest, src, nbytes, 0 GASNETE_THREAD_PASS);
}

extern void gasnete_put_nbi_bulk (gasnet_node_t node, void *dest, void *src, size_t nbytes GASNETE_THREAD_FARG) {
  gasnete_put_nbi_inner(node, dest, src, nbytes, 1 GASNETE_THREAD_PASS);
}

extern void gasnete_memset_nbi   (gasnet_node_t node, void *dest, int val, size_t nbytes GASNETE_THREAD_FARG) {
  gasnete_threaddata_t * const mythread = GASNETE_MYTHREAD;
  gasnete_iop_t *op = mythread->current_iop;
  op->initiated_put_cnt++;

  GASNETE_SAFE(
    SHORT_REQ(4,6,(node, gasneti_handleridx(gasnete_memset_reqh),
                 (gasnet_handlerarg_t)val, (gasnet_handlerarg_t)nbytes,
                 PACK(dest), PACK(op))));
}

/* ------------------------------------------------------------------------------------ */
/*
  Synchronization for implicit-handle non-blocking operations:
  ===========================================================
*/
static int gasnete_iop_gets_done(gasnete_iop_t *iop) {
  ASSERT_ELAN_UNLOCKED();
  if (gasneti_atomic_read(&(iop->completed_get_cnt)) == iop->initiated_get_cnt) {
    int retval = TRUE;
    if_pf (iop->initiated_get_cnt > 65000) { /* make sure we don't overflow the counters */
      gasneti_atomic_set(&(iop->completed_get_cnt), 0);
      iop->initiated_get_cnt = 0;
    }
    #if !GASNETE_USE_PGCTRL_NBI
      if (iop->getctrl.evt_cnt || iop->elan_getbb_list)
    #endif
      {
        LOCK_ELAN_WEAK();
          #if GASNETE_USE_PGCTRL_NBI
            if (iop->elan_pgctrl) {
              /* shit, this is broken - need a non-blocking way to poll ELAN_PGCTRL */
              broken;
            }
          #else
            if (!gasnete_putgetctrl_done(&(iop->getctrl))) 
              retval = FALSE;
          #endif
          if ((iop->elan_getbb_list = gasnete_putgetbblist_pending(iop->elan_getbb_list)) != NULL) 
            retval = FALSE;
        UNLOCK_ELAN_WEAK();
      }
    return retval;
  }
  return FALSE;
}
static int gasnete_iop_puts_done(gasnete_iop_t *iop) {
  ASSERT_ELAN_UNLOCKED();
  if (gasneti_atomic_read(&(iop->completed_put_cnt)) == iop->initiated_put_cnt) {
    int retval = TRUE;
    if_pf (iop->initiated_put_cnt > 65000) { /* make sure we don't overflow the counters */
      gasneti_atomic_set(&(iop->completed_put_cnt), 0);
      iop->initiated_put_cnt = 0;
    }
    #if !GASNETE_USE_PGCTRL_NBI
      if (iop->putctrl.evt_cnt || iop->elan_putbb_list)
    #endif
      {
        LOCK_ELAN_WEAK();
          #if GASNETE_USE_PGCTRL_NBI
            if (iop->elan_pgctrl) {
              /* shit, this is broken - need a non-blocking way to poll ELAN_PGCTRL */
              broken;
            }
          #else
            if (!gasnete_putgetctrl_done(&(iop->putctrl))) 
              retval = FALSE;
          #endif
          if ((iop->elan_putbb_list = gasnete_putgetbblist_pending(iop->elan_putbb_list)) != NULL) 
            retval = FALSE;
        UNLOCK_ELAN_WEAK();
      }
    return retval;
  }
  return FALSE;
}

extern int  gasnete_try_syncnbi_gets(GASNETE_THREAD_FARG_ALONE) {
  #if 0
    /* polling for syncnbi now happens in header file to avoid duplication */
    GASNETE_SAFE(gasneti_AMPoll());
  #endif
  {
    gasnete_threaddata_t * const mythread = GASNETE_MYTHREAD;
    gasnete_iop_t *iop = mythread->current_iop;
    gasneti_assert(iop->threadidx == mythread->threadidx);
    gasneti_assert(OPTYPE(iop) == OPTYPE_IMPLICIT);
    #if GASNET_DEBUG
      if (iop->next != NULL)
        gasneti_fatalerror("VIOLATION: attempted to call gasnete_try_syncnbi_gets() inside an NBI access region");
    #endif

    if (gasnete_iop_gets_done(iop)) {
      gasneti_sync_reads();
      return GASNET_OK;
    }
    else return GASNET_ERR_NOT_READY;
  }
}

extern int  gasnete_try_syncnbi_puts(GASNETE_THREAD_FARG_ALONE) {
  #if 0
    /* polling for syncnbi now happens in header file to avoid duplication */
    GASNETE_SAFE(gasneti_AMPoll());
  #endif
  {
    gasnete_threaddata_t * const mythread = GASNETE_MYTHREAD;
    gasnete_iop_t *iop = mythread->current_iop;
    gasneti_assert(iop->threadidx == mythread->threadidx);
    gasneti_assert(iop->next == NULL);
    gasneti_assert(OPTYPE(iop) == OPTYPE_IMPLICIT);
    #if GASNET_DEBUG
      if (iop->next != NULL)
        gasneti_fatalerror("VIOLATION: attempted to call gasnete_try_syncnbi_puts() inside an NBI access region");
    #endif

    if (gasnete_iop_puts_done(iop)) {
      gasneti_sync_reads();
      return GASNET_OK;
    }
    else return GASNET_ERR_NOT_READY;
  }
}

/* ------------------------------------------------------------------------------------ */
/*
  Implicit access region synchronization
  ======================================
*/
/*  This implementation allows recursive access regions, although the spec does not require that */
/*  operations are associated with the most immediately enclosing access region */
extern void            gasnete_begin_nbi_accessregion(int allowrecursion GASNETE_THREAD_FARG) {
  gasnete_threaddata_t * const mythread = GASNETE_MYTHREAD;
  gasnete_iop_t *iop = gasnete_iop_new(mythread); /*  push an iop  */
  GASNETI_TRACE_PRINTF(S,("BEGIN_NBI_ACCESSREGION"));
  #if GASNET_DEBUG
    if (!allowrecursion && mythread->current_iop->next != NULL)
      gasneti_fatalerror("VIOLATION: tried to initiate a recursive NBI access region");
  #endif
  iop->next = mythread->current_iop;
  mythread->current_iop = iop;
}

extern gasnet_handle_t gasnete_end_nbi_accessregion(GASNETE_THREAD_FARG_ALONE) {
  gasnete_threaddata_t * const mythread = GASNETE_MYTHREAD;
  gasnete_iop_t *iop = mythread->current_iop; /*  pop an iop */
  GASNETI_TRACE_EVENT_VAL(S,END_NBI_ACCESSREGION,iop->initiated_get_cnt + iop->initiated_put_cnt);
  #if GASNET_DEBUG
    if (iop->next == NULL)
      gasneti_fatalerror("VIOLATION: call to gasnete_end_nbi_accessregion() outside access region");
  #endif
  mythread->current_iop = iop->next;
  iop->next = NULL;
  return GASNETE_OP_TO_HANDLE(iop);
}

/* ------------------------------------------------------------------------------------ */
/*
  Non-Blocking Value Get (explicit-handle)
  ========================================
*/
typedef struct _gasnet_valget_op_t {
  gasnet_handle_t handle;
  gasnet_register_value_t val;

  struct _gasnet_valget_op_t* next; /* for free-list only */
  gasnete_threadidx_t threadidx;  /*  thread that owns me */
} gasnet_valget_op_t;

extern gasnet_valget_handle_t gasnete_get_nb_val(gasnet_node_t node, void *src, size_t nbytes GASNETE_THREAD_FARG) {
  gasnete_threaddata_t * const mythread = GASNETE_MYTHREAD;
  gasnet_valget_handle_t retval;
  gasneti_assert(nbytes > 0 && nbytes <= sizeof(gasnet_register_value_t));
  gasnete_boundscheck(node, src, nbytes);
  if (mythread->valget_free) {
    retval = mythread->valget_free;
    mythread->valget_free = retval->next;
    gasneti_memcheck(retval);
  } else {
    retval = (gasnet_valget_op_t*)gasneti_malloc(sizeof(gasnet_valget_op_t));
    retval->threadidx = mythread->threadidx;
  }

  retval->val = 0;
  if (gasnete_islocal(node)) {
    GASNETE_FAST_ALIGNED_MEMCPY(GASNETE_STARTOFBITS(&(retval->val),nbytes), src, nbytes);
    retval->handle = GASNET_INVALID_HANDLE;
  } else {
    retval->handle = gasnete_get_nb_bulk(GASNETE_STARTOFBITS(&(retval->val),nbytes), node, src, nbytes GASNETE_THREAD_PASS);
  }
  return retval;
}

extern gasnet_register_value_t gasnete_wait_syncnb_valget(gasnet_valget_handle_t handle) {
  gasnet_register_value_t val;
  gasnete_threaddata_t * const thread = gasnete_threadtable[handle->threadidx];
  gasneti_assert(thread == gasnete_mythread());
  handle->next = thread->valget_free; /* free before the wait to save time after the wait, */
  thread->valget_free = handle;       /*  safe because this thread is under our control */

  gasnete_wait_syncnb(handle->handle);
  val = handle->val;
  return val;
}

/* ------------------------------------------------------------------------------------ */
/*
  Barriers:
  =========
*/
#if !GASNETE_USE_ELAN_BARRIER
  /* use reference implementation of barrier */
  #define GASNETI_GASNET_EXTENDED_REFBARRIER_C 1
  #define gasnete_refbarrier_init    gasnete_barrier_init
  #define gasnete_refbarrier_notify  gasnete_barrier_notify
  #define gasnete_refbarrier_wait    gasnete_barrier_wait
  #define gasnete_refbarrier_try     gasnete_barrier_try
  #include "gasnet_extended_refbarrier.c"
  #undef GASNETI_GASNET_EXTENDED_REFBARRIER_C
/* ------------------------------------------------------------------------------------ */
#else /* GASNETE_USE_ELAN_BARRIER */

#if GASNETI_STATS_OR_TRACE
  static gasneti_stattime_t barrier_notifytime; /* for statistical purposes */ 
#endif
static enum { OUTSIDE_BARRIER, INSIDE_BARRIER } barrier_splitstate = OUTSIDE_BARRIER;

#ifdef ELAN_VER_1_2
  typedef int (*ELAN_POLLFN)(void *handle, unsigned int *ready);
  extern void elan_addPollFn(ELAN_STATE *elan_state, ELAN_POLLFN, void *handle);
#endif
typedef struct {
  int volatile barrier_value;
  int volatile barrier_flags;
} gasnete_barrier_state_t;
static gasnete_barrier_state_t *barrier_state = NULL;
static int volatile barrier_blocking = 0;
static int barrier_phase = 0;
int gasnete_barrier_poll(void *handle, unsigned int *ready) {
  if_pf (barrier_blocking && !GASNETC_EXITINPROGRESS()) {
    UNLOCK_ELAN_WEAK();
      barrier_blocking = 0;
      #if 0
        GASNETI_TRACE_EVENT(C, POLL_CALLBACK_BARRIER);
      #else
        /* prevent high contention for trace lock while idling at barrier */
        _GASNETI_STAT_EVENT(C, POLL_CALLBACK_BARRIER); 
      #endif
      gasneti_AMPoll(); 
      barrier_blocking = 1;
    LOCK_ELAN_WEAK();
  } 
  else 
  #if 1 
    /* use _GASNETI_STAT_EVENT to avoid elan locking problems */
    _GASNETI_STAT_EVENT(C, POLL_CALLBACK_NOOP); 
  #else
    UNLOCKRELOCK_ELAN_WEAK_IFTRACE(GASNETI_TRACE_EVENT(C, POLL_CALLBACK_NOOP));
  #endif

  return 0; /* return 0 => don't delay the elan blocking */
}

extern void gasnete_barrier_init() {
  #ifdef ELAN_VER_1_2
    barrier_state = elan_gallocMain(BASE()->galloc, GROUP(), 64, 2*sizeof(gasnete_barrier_state_t));
  #else
    barrier_state = elan_gallocMain(BASE(), GROUP(), 64, 2*sizeof(gasnete_barrier_state_t));
  #endif
  if_pf(barrier_state == NULL) 
    gasneti_fatalerror("error allocating barrier_state buffer in gasnete_barrier_init()");
  barrier_state[0].barrier_value = 0;
  barrier_state[0].barrier_flags = 0;
  barrier_state[1].barrier_value = 0;
  barrier_state[1].barrier_flags = 0;

  #if ELAN_VERSION_GE(1,4,8)
    elan_addProgressFn(STATE(), (ELAN_PROGFN)gasnete_barrier_poll, NULL);
  #else
    elan_addPollFn(STATE(), (ELAN_POLLFN)gasnete_barrier_poll, NULL);
  #endif
}

extern void gasnete_barrier_notify(int id, int flags) {
  int phase;
  gasneti_sync_reads(); /* ensure we read correct barrier_splitstate */
  if_pf(barrier_splitstate == INSIDE_BARRIER) 
    gasneti_fatalerror("gasnet_barrier_notify() called twice in a row");
  phase = barrier_phase;

  GASNETI_TRACE_PRINTF(B, ("BARRIER_NOTIFY(id=%i,flags=%i)", id, flags));
  #if GASNETI_STATS_OR_TRACE
    barrier_notifytime = GASNETI_STATTIME_NOW_IFENABLED(B);
  #endif

  barrier_state[phase].barrier_value = id;
  barrier_state[phase].barrier_flags = flags;

  if (gasnete_nodes > 1) {
    LOCK_ELAN_WEAK();
    barrier_blocking = 1; /* allow polling while inside blocking barriers */
      /* TODO: this algorithm requires all threads agree on whether the flags 
               indicate a named or anonymous barrier 
               (otherwise it may deadlock when GASNETE_FAST_ELAN_BARRIER is enabled, 
                or fail to detect a mismatch otherwise)
      */
    #if GASNETE_FAST_ELAN_BARRIER
      if (flags & GASNET_BARRIERFLAG_ANONYMOUS) {
        if_pf(flags & GASNET_BARRIERFLAG_MISMATCH) {
          int i;
          barrier_state->barrier_flags = GASNET_BARRIERFLAG_MISMATCH;
          for (i=0; i < gasnete_nodes; i++) {
            elan_wait(elan_put(STATE(), (int *)&(barrier_state[phase].barrier_flags), 
                                        (int *)&(barrier_state[phase].barrier_flags),
                                        sizeof(int), i), ELAN_POLL_EVENT);
          }
        }
        elan_hgsync(GROUP()); 
      } else
    #endif
      {
        elan_hbcast(GROUP(), &(barrier_state[phase]), sizeof(gasnete_barrier_state_t), 0, 1);
        if_pf (flags != barrier_state[phase].barrier_flags || 
           (!(flags & GASNET_BARRIERFLAG_ANONYMOUS) && 
             barrier_state[phase].barrier_value != id) || 
            (flags & GASNET_BARRIERFLAG_MISMATCH)) { /* detected a mismatch - tell everybody */
          int i;
          barrier_state[phase].barrier_flags = GASNET_BARRIERFLAG_MISMATCH;
          for (i=0; i < gasnete_nodes; i++) {
            elan_wait(elan_put(STATE(), (int *)&(barrier_state[phase].barrier_flags), 
                                        (int *)&(barrier_state[phase].barrier_flags),
                                        sizeof(int), i), ELAN_POLL_EVENT);
          }
        }
        elan_hgsync(GROUP()); 
      }
    barrier_blocking = 0; 
    UNLOCK_ELAN_WEAK();
  } 

  /*  update state */
  barrier_splitstate = INSIDE_BARRIER;
  gasneti_sync_writes(); /* ensure all state changes committed before return */
}

extern int gasnete_barrier_wait(int id, int flags) {
  int phase;
  #if GASNETI_STATS_OR_TRACE
    gasneti_stattime_t wait_start = GASNETI_STATTIME_NOW_IFENABLED(B);
  #endif
  gasneti_sync_reads(); /* ensure we read correct barrier_splitstate */
  if_pf(barrier_splitstate == OUTSIDE_BARRIER) 
    gasneti_fatalerror("gasnet_barrier_wait() called without a matching notify");
  phase = barrier_phase;
  barrier_phase = !phase;

  GASNETI_TRACE_EVENT_TIME(B,BARRIER_NOTIFYWAIT,GASNETI_STATTIME_NOW()-barrier_notifytime);

  GASNETI_TRACE_EVENT_TIME(B,BARRIER_WAIT,0);

  /*  update state */
  barrier_splitstate = OUTSIDE_BARRIER;
  gasneti_sync_writes(); /* ensure all state changes committed before return */
  if_pf((barrier_state[phase].barrier_flags & GASNET_BARRIERFLAG_MISMATCH) ||
        flags != barrier_state[phase].barrier_flags ||
        (!(flags & GASNET_BARRIERFLAG_ANONYMOUS) && 
          id != barrier_state[phase].barrier_value)) 
    return GASNET_ERR_BARRIER_MISMATCH;
  else 
    return GASNET_OK;
}

extern int gasnete_barrier_try(int id, int flags) {
  gasneti_sync_reads(); /* ensure we read correct barrier_splitstate */
  if_pf(barrier_splitstate == OUTSIDE_BARRIER) 
    gasneti_fatalerror("gasnet_barrier_try() called without a matching notify");

  GASNETI_TRACE_EVENT_VAL(B,BARRIER_TRY,1);
  return gasnete_barrier_wait(id, flags);
}
#endif
/* ------------------------------------------------------------------------------------ */
/*
  Vector, Indexed & Strided:
  =========================
*/

/* use reference implementation of scatter/gather and strided */
#define GASNETI_GASNET_EXTENDED_VIS_C 1
#include "gasnet_extended_refvis.c"
#undef GASNETI_GASNET_EXTENDED_VIS_C

/* ------------------------------------------------------------------------------------ */
/*
  Collectives:
  ============
*/

/* use reference implementation of collectives */
#define GASNETI_GASNET_EXTENDED_COLL_C 1
#include "gasnet_extended_refcoll.c"
#undef GASNETI_GASNET_EXTENDED_COLL_C

/* ------------------------------------------------------------------------------------ */
/*
  Handlers:
  =========
*/
static gasnet_handlerentry_t const gasnete_handlers[] = {
  #ifdef GASNETE_REFBARRIER_HANDLERS
    GASNETE_REFBARRIER_HANDLERS(),
  #endif
  #ifdef GASNETE_REFVIS_HANDLERS
    GASNETE_REFVIS_HANDLERS(),
  #endif
  #ifdef GASNETE_REFCOLL_HANDLERS
    GASNETE_REFCOLL_HANDLERS(),
  #endif

  /* ptr-width independent handlers */

  /* ptr-width dependent handlers */
  gasneti_handler_tableentry_with_bits(gasnete_get_reqh),
  gasneti_handler_tableentry_with_bits(gasnete_get_reph),
  gasneti_handler_tableentry_with_bits(gasnete_getlong_reqh),
  gasneti_handler_tableentry_with_bits(gasnete_getlong_reph),
  gasneti_handler_tableentry_with_bits(gasnete_put_reqh),
  gasneti_handler_tableentry_with_bits(gasnete_putlong_reqh),
  gasneti_handler_tableentry_with_bits(gasnete_memset_reqh),
  gasneti_handler_tableentry_with_bits(gasnete_markdone_reph),

  { 0, NULL }
};

extern gasnet_handlerentry_t const *gasnete_get_handlertable() {
  return gasnete_handlers;
}

/* ------------------------------------------------------------------------------------ */

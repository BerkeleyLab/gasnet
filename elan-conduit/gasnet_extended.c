/*  $Archive:: /Ti/GASNet/elan-conduit/gasnet_extended.c                  $
 *     $Date: 2003/06/11 04:45:29 $
 * $Revision: 1.23 $
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
#ifdef GASNETI_THREADS
  static pthread_key_t gasnete_threaddata; /*  pthread thread-specific ptr to our threaddata (or NULL for a thread never-seen before) */
#endif
static const gasnete_eopaddr_t EOPADDR_NIL = { 0xFF, 0xFF };

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
  static void gasnete_barrier_init();
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
#define GASNETE_ASSERT_ALIGNED(p)           assert((((uintptr_t)(p))&0x3)==0)
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
  #ifdef GASNETI_THREADS
    if (idx >= 256) gasneti_fatalerror("GASNet Extended API: Too many local client threads (limit=256)");
  #else
    assert(idx == 0);
  #endif
  assert(gasnete_threadtable[idx] == NULL);

  threaddata = (gasnete_threaddata_t *)gasneti_malloc(sizeof(gasnete_threaddata_t));
  memset(threaddata, 0, sizeof(gasnete_threaddata_t));

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
#ifdef GASNETI_THREADS
  extern gasnete_threaddata_t *gasnete_mythread() {
    gasnete_threaddata_t *threaddata = pthread_getspecific(gasnete_threaddata);
    GASNETI_TRACE_EVENT(C, DYNAMIC_THREADLOOKUP);
    if_pt (threaddata) return threaddata;

    /*  first time we've seen this thread - need to set it up */
    { int retval;
      gasnete_threaddata_t *threaddata = gasnete_new_threaddata();

      retval = pthread_setspecific(gasnete_threaddata, threaddata);
      assert(!retval);
      return threaddata;
    }
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
  assert(sizeof(int8_t) == 1);
  assert(sizeof(uint8_t) == 1);
  #if !defined(CRAYT3E)
    assert(sizeof(int16_t) == 2);
    assert(sizeof(uint16_t) == 2);
  #endif
  assert(sizeof(int32_t) == 4);
  assert(sizeof(uint32_t) == 4);
  assert(sizeof(int64_t) == 8);
  assert(sizeof(uint64_t) == 8);

  assert(sizeof(uintptr_t) >= sizeof(void *));

  /* check GASNET_PAGESIZE is a power of 2 and > 0 */
  assert(GASNET_PAGESIZE > 0 && 
         (GASNET_PAGESIZE & (GASNET_PAGESIZE - 1)) == 0);

  assert(SIZEOF_GASNET_REGISTER_VALUE_T == sizeof(gasnet_register_value_t));
  assert(SIZEOF_GASNET_REGISTER_VALUE_T >= sizeof(int));
  assert(SIZEOF_GASNET_REGISTER_VALUE_T >= sizeof(void *));

  #if    defined(GASNETI_PTR32) && !defined(GASNETI_PTR64)
    assert(sizeof(void*) == 4);
  #elif !defined(GASNETI_PTR32) &&  defined(GASNETI_PTR64)
    assert(sizeof(void*) == 8);
  #else
    #error must #define exactly one of GASNETI_PTR32 or GASNETI_PTR64
  #endif

  assert(gasnete_eopaddr_isnil(EOPADDR_NIL));

  /*  verify sanity of the core interface */
  assert(gasnet_AMMaxArgs() >= 2*MAX(sizeof(int),sizeof(void*)));      
  assert(gasnet_AMMaxMedium() >= 512);
  assert(gasnet_AMMaxLongRequest() >= 512);
  assert(gasnet_AMMaxLongReply() >= 512);
}

extern void gasnete_init() {
  GASNETI_TRACE_PRINTF(C,("gasnete_init()"));
  assert(gasnete_nodes == 0); /*  make sure we haven't been called before */

  gasnete_check_config(); /*  check for sanity */

  #ifdef GASNETI_THREADS
  {/*  TODO: we could provide a non-NULL destructor and reap data structures from exiting threads */
    int retval = pthread_key_create(&gasnete_threaddata, NULL);
    if (retval) gasneti_fatalerror("In gasnete_init(), pthread_key_create()=%s",strerror(retval));
  }
  #endif

  gasnete_mynode = gasnet_mynode();
  gasnete_nodes = gasnet_nodes();
  assert(gasnete_nodes >= 1 && gasnete_mynode < gasnete_nodes);
  gasnete_seginfo = (gasnet_seginfo_t*)gasneti_malloc(sizeof(gasnet_seginfo_t)*gasnete_nodes);
  gasnet_getSegmentInfo(gasnete_seginfo, gasnete_nodes);

  if (gasnet_getenv("GASNET_NBI_THROTTLE")) {
    gasnete_nbi_throttle = atoi(gasnet_getenv("GASNET_NBI_THROTTLE"));
    if (gasnete_nbi_throttle < 1) gasnete_nbi_throttle = GASNETE_DEFAULT_NBI_THROTTLE;
    else GASNETI_TRACE_PRINTF(C,("Set gasnete_nbi_throttle = %i", gasnete_nbi_throttle));
  }

  { gasnete_threaddata_t *threaddata = NULL;
    gasnete_eop_t *eop = NULL;
    #ifdef GASNETI_THREADS
      /* register first thread (optimization) */
      threaddata = gasnete_mythread(); 
    #else
      /* register only thread (required) */
      threaddata = gasnete_new_threaddata();
      gasnete_threadtable[0] = threaddata;
    #endif

    /* cause the first pool of eops to be allocated (optimization) */
    eop = gasnete_eop_new(threaddata, OPCAT_MEMSET);
    gasnete_op_markdone((gasnete_op_t *)eop, 0);
    gasnete_op_free((gasnete_op_t *)eop);
  }
  #if GASNETE_USE_ELAN_BARRIER
    gasnete_barrier_init();
  #endif
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
    assert(!gasnete_eopaddr_equal(thread->eop_free,head));
    assert(eop->threadidx == thread->threadidx);
    assert(OPTYPE(eop) == OPTYPE_EXPLICIT);
    assert(OPTYPE(eop) == OPSTATE_FREE);
    SET_OPSTATE(eop, OPSTATE_INFLIGHT);
    SET_OPCAT(eop, cat);
    #ifdef DEBUG
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
    buf = (gasnete_eop_t *)gasneti_malloc(256*sizeof(gasnete_eop_t));
    memset(buf, 0, 256*sizeof(gasnete_eop_t));
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

    #ifdef DEBUG
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

      memset(seen, 0, 256*sizeof(int));
      for (i=0;i<(bufidx==255?255:256);i++) {                                   
        gasnete_eop_t *eop;                                   
        assert(!gasnete_eopaddr_isnil(addr));                 
        eop = GASNETE_EOPADDR_TO_PTR(thread,addr);            
        assert(OPTYPE(eop) == OPTYPE_EXPLICIT);               
        assert(OPSTATE(eop) == OPSTATE_FREE);                 
        assert(eop->threadidx == threadidx);                  
        assert(addr.bufferidx == bufidx);
        assert(!seen[addr.eopidx]);/* see if we hit a cycle */
        seen[addr.eopidx] = 1;
        addr = eop->addr;                                     
      }                                                       
      assert(gasnete_eopaddr_isnil(addr)); 
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
    assert(OPTYPE(iop) == OPTYPE_IMPLICIT);
    assert(iop->threadidx == thread->threadidx);
  } else {
    int sz = sizeof(gasnete_iop_t);
    #if !GASNETE_USE_PGCTRL_NBI
      assert(sizeof(gasnete_iop_t) % sizeof(void*) == 0);
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

  return iop;
}

static int gasnete_iop_gets_done(gasnete_iop_t *iop);
static int gasnete_iop_puts_done(gasnete_iop_t *iop);

/*  query an op for completeness - for iop this means both puts and gets */
int gasnete_op_isdone(gasnete_op_t *op, int have_elanLock) {
  assert(op->threadidx == gasnete_mythread()->threadidx);
  if (have_elanLock)   ASSERT_ELAN_LOCKED_WEAK();
  else                 ASSERT_ELAN_UNLOCKED();
  if_pt (OPTYPE(op) == OPTYPE_EXPLICIT) {
    uint8_t cat;
    assert(OPSTATE(op) != OPSTATE_FREE);
    if (OPSTATE(op) == OPSTATE_COMPLETE) return TRUE;
    cat = OPCAT(op);
    switch (cat) {
      case OPCAT_ELANGETBB:
      case OPCAT_ELANPUTBB: {
        int result;
        gasnete_bouncebuf_t *bb = ((gasnete_eop_t *)op)->bouncebuf;
        assert(bb);
        if (!have_elanLock) LOCK_ELAN_WEAK();
          result = elan_poll(bb->evt, 1);
          if (result) {
            if (cat == OPCAT_ELANGETBB) {
              assert(bb->get_dest);
              memcpy(bb->get_dest, bb+1, bb->get_nbytes);
            }
            elan_free(STATE(), bb);
            #ifdef DEBUG
              ((gasnete_eop_t *)op)->bouncebuf = NULL;
            #endif
            SET_OPSTATE((gasnete_eop_t *)op, OPSTATE_COMPLETE);
          } 
        if (!have_elanLock) UNLOCK_ELAN_WEAK();
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
    return gasnete_iop_gets_done(iop) && gasnete_iop_puts_done(iop);
  }
}

/*  mark an op done - isget ignored for explicit ops */
void gasnete_op_markdone(gasnete_op_t *op, int isget) {
  if (OPTYPE(op) == OPTYPE_EXPLICIT) {
    gasnete_eop_t *eop = (gasnete_eop_t *)op;
    assert(OPSTATE(eop) == OPSTATE_INFLIGHT);
    SET_OPSTATE(eop, OPSTATE_COMPLETE);
  } else {
    gasnete_iop_t *iop = (gasnete_iop_t *)op;
    if (isget) gasneti_atomic_increment(&(iop->completed_get_cnt));
    else gasneti_atomic_increment(&(iop->completed_put_cnt));
  }
}

/*  free an op */
void gasnete_op_free(gasnete_op_t *op) {
  gasnete_threaddata_t * const thread = gasnete_threadtable[op->threadidx];
  assert(thread == gasnete_mythread());
  if (OPTYPE(op) == OPTYPE_EXPLICIT) {
    gasnete_eop_t *eop = (gasnete_eop_t *)op;
    gasnete_eopaddr_t addr = eop->addr;
    SET_OPSTATE(eop, OPSTATE_FREE);
    eop->addr = thread->eop_free;
    thread->eop_free = addr;
  } else {
    gasnete_iop_t *iop = (gasnete_iop_t *)op;
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
  assert(nbytes <= gasnet_AMMaxMedium());
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
  gasneti_memsync();
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
  gasneti_memsync();
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
  gasneti_memsync();
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
  gasneti_memsync();
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
  gasneti_memsync();
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
  #ifdef GASNET_SEGMENT_EVERYTHING
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
    assert(evt);
    return GASNETE_ELANEVENT_TO_HANDLE(evt);
  } else if (nbytes <= GASNETE_MAX_COPYBUFFER_SZ) { /* use a bounce buffer */
    gasnete_bouncebuf_t *bouncebuf;
    bouncebuf = (gasnete_bouncebuf_t *)elan_allocMain(STATE(), 64, sizeof(gasnete_bouncebuf_t)+nbytes);
    if_pt (bouncebuf) {
      ELAN_EVENT *evt;
      gasnete_eop_t *eop;
      assert(elan_addressable(STATE(),bouncebuf,sizeof(gasnete_bouncebuf_t)+nbytes));
      evt = elan_get(STATE(), src, bouncebuf+1, nbytes, node);
      UNLOCK_ELAN_WEAK();
      GASNETI_TRACE_EVENT_VAL(C,GET_BUFFERED,nbytes);
      eop = gasnete_eop_new(GASNETE_MYTHREAD, OPCAT_ELANGETBB);
      bouncebuf->evt = evt;
      #ifdef DEBUG
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
  }
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
  #ifdef GASNET_SEGMENT_EVERYTHING
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
    assert(evt);
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
      assert(elan_addressable(STATE(),bouncebuf,sizeof(gasnete_bouncebuf_t)+nbytes));
      /* TODO: this gets a "not-addressable" elan exception on dual-rail runs - why? */
      evt = elan_put(STATE(), bouncebuf+1, dest, nbytes, node);
      UNLOCK_ELAN_WEAK();
      if (isbulk) GASNETI_TRACE_EVENT_VAL(C,PUT_BULK_BUFFERED,nbytes);
      else        GASNETI_TRACE_EVENT_VAL(C,PUT_BUFFERED,nbytes);
      eop = gasnete_eop_new(GASNETE_MYTHREAD, OPCAT_ELANPUTBB);
      bouncebuf->evt = evt;
      #ifdef DEBUG
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
  }
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
  GASNETE_SAFE(gasnet_AMPoll());
  return gasnete_try_syncnb_inner(handle);
}

extern int  gasnete_try_syncnb_some (gasnet_handle_t *phandle, size_t numhandles) {
  int success = 0;
  int empty = 1;
  GASNETE_SAFE(gasnet_AMPoll());

  assert(phandle);

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
  GASNETE_SAFE(gasnet_AMPoll());

  assert(phandle);

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
  assert(pgctrl && pgctrl->evt_cnt >= 0);
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
  assert(pgctrl && evt && pgctrl->evt_cnt <= gasnete_nbi_throttle);
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
      UNLOCKRELOCK_ELAN_WEAK(gasnetc_AMPoll());
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
    assert(OPCAT(op) == OPCAT_ELANGETBB || OPCAT(op) == OPCAT_ELANPUTBB);
    assert(eoplist->bouncebuf);
    next = eoplist->bouncebuf->next;
    if (gasnete_op_isdone(op, TRUE)) 
      gasnete_op_free(op);
    else 
      return eoplist; /* stop when we find the first pending one */
    eoplist = next;
  }
  return NULL;
}

extern void gasnete_get_nbi_bulk (void *dest, gasnet_node_t node, void *src, size_t nbytes GASNETE_THREAD_FARG) {
  gasnete_threaddata_t * const mythread = GASNETE_MYTHREAD;
  gasnete_iop_t *iop = mythread->current_iop;
#if GASNETE_USE_ELAN_PUTGET
  LOCK_ELAN_WEAK();
  #ifdef GASNET_SEGMENT_EVERYTHING
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
      assert(evt);
    #else
      evt = elan_get(STATE(), src, dest, nbytes, node);
      assert(evt);
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
      assert(elan_addressable(STATE(),bouncebuf,sizeof(gasnete_bouncebuf_t)+nbytes));
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
            UNLOCKRELOCK_ELAN_WEAK(gasnetc_AMPoll()); /* prevent deadlock */
          }
          goto tryagain;
        }
      #endif
      UNLOCK_ELAN_WEAK();
      GASNETI_TRACE_PRINTF(I,("Warning: Elan conduit exhausted the main memory heap trying to get a bounce buffer, using AM instead"));
    }
  }
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
    int msgsent=0;
    gasnet_handler_t reqhandler;
    uint8_t *psrc = src;
    uint8_t *pdest = dest;
    #if GASNETE_USE_LONG_GETS
      /* TODO: optimize this check by caching segment upper-bound in gasnete_seginfo */
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
      msgsent++;
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
    iop->initiated_get_cnt += msgsent;
    return;
  }
}

GASNET_INLINE_MODIFIER(gasnete_put_nbi_inner)
void gasnete_put_nbi_inner(gasnet_node_t node, void *dest, void *src, size_t nbytes, int isbulk GASNETE_THREAD_FARG) {
  gasnete_threaddata_t * const mythread = GASNETE_MYTHREAD;
  gasnete_iop_t *iop = mythread->current_iop;

#if GASNETE_USE_ELAN_PUTGET
  LOCK_ELAN_WEAK();
  #ifdef GASNET_SEGMENT_EVERYTHING
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
      assert(evt);
    #else
      evt = elan_put(STATE(), src, dest, nbytes, node);
      assert(evt);
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
      assert(elan_addressable(STATE(),bouncebuf,sizeof(gasnete_bouncebuf_t)+nbytes));
      evt = elan_put(STATE(), bouncebuf+1, dest, nbytes, node);
      UNLOCK_ELAN_WEAK();
      if (isbulk) GASNETI_TRACE_EVENT_VAL(C,PUT_BULK_BUFFERED,nbytes);
      else        GASNETI_TRACE_EVENT_VAL(C,PUT_BUFFERED,nbytes);
      eop = gasnete_eop_new(GASNETE_MYTHREAD, OPCAT_ELANPUTBB);
      bouncebuf->evt = evt;
      #ifdef DEBUG
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
            UNLOCKRELOCK_ELAN_WEAK(gasnetc_AMPoll()); /* prevent deadlock */
          }
          goto tryagain;
        }
      #endif
      UNLOCK_ELAN_WEAK();
      GASNETI_TRACE_PRINTF(I,("Warning: Elan conduit exhausted the main memory heap trying to get a bounce buffer, using AM instead"));
    }
  }
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
    int msgsent=0;
    uint8_t *psrc = src;
    uint8_t *pdest = dest;
    if (isbulk) GASNETI_TRACE_EVENT_VAL(C,PUT_BULK_AMLONG,nbytes);
    else        GASNETI_TRACE_EVENT_VAL(C,PUT_AMLONG,nbytes);
    for (;;) {
      msgsent++;
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
    iop->initiated_put_cnt += msgsent;
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
    GASNETE_SAFE(gasnet_AMPoll());
  #endif
  {
    gasnete_threaddata_t * const mythread = GASNETE_MYTHREAD;
    gasnete_iop_t *iop = mythread->current_iop;
    assert(iop->threadidx == mythread->threadidx);
    assert(OPTYPE(iop) == OPTYPE_IMPLICIT);
    #ifdef DEBUG
      if (iop->next != NULL)
        gasneti_fatalerror("VIOLATION: attempted to call gasnete_try_syncnbi_gets() inside an NBI access region");
    #endif

    if (gasnete_iop_gets_done(iop)) return GASNET_OK;
    else return GASNET_ERR_NOT_READY;
  }
}

extern int  gasnete_try_syncnbi_puts(GASNETE_THREAD_FARG_ALONE) {
  #if 0
    /* polling for syncnbi now happens in header file to avoid duplication */
    GASNETE_SAFE(gasnet_AMPoll());
  #endif
  {
    gasnete_threaddata_t * const mythread = GASNETE_MYTHREAD;
    gasnete_iop_t *iop = mythread->current_iop;
    assert(iop->threadidx == mythread->threadidx);
    assert(iop->next == NULL);
    assert(OPTYPE(iop) == OPTYPE_IMPLICIT);
    #ifdef DEBUG
      if (iop->next != NULL)
        gasneti_fatalerror("VIOLATION: attempted to call gasnete_try_syncnbi_puts() inside an NBI access region");
    #endif

    if (gasnete_iop_puts_done(iop)) return GASNET_OK;
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
  #ifdef DEBUG
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
  #ifdef DEBUG
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
  assert(nbytes > 0 && nbytes <= sizeof(gasnet_register_value_t));
  gasnete_boundscheck(node, src, nbytes);
  if (mythread->valget_free) {
    retval = mythread->valget_free;
    mythread->valget_free = retval->next;
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
  assert(thread == gasnete_mythread());
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
#if defined(STATS) || defined(TRACE)
  static gasneti_stattime_t barrier_notifytime; /* for statistical purposes */ 
#endif
static enum { OUTSIDE_BARRIER, INSIDE_BARRIER } barrier_splitstate = OUTSIDE_BARRIER;

#if GASNETE_USE_ELAN_BARRIER
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
      gasnet_AMPoll(); 
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

static void gasnete_barrier_init() {
  #ifdef ELAN_VER_1_2
    barrier_state = elan_gallocMain(BASE()->galloc, GROUP(), 64, sizeof(gasnete_barrier_state_t));
  #else
    barrier_state = elan_gallocMain(BASE(), GROUP(), 64, sizeof(gasnete_barrier_state_t));
  #endif
  if_pf(barrier_state == NULL) 
    gasneti_fatalerror("error allocating barrier_state buffer in gasnete_barrier_init()");
  barrier_state->barrier_value = 0;
  barrier_state->barrier_flags = 0;

  elan_addPollFn(STATE(), (ELAN_POLLFN)gasnete_barrier_poll, NULL);
}

extern void gasnete_barrier_notify(int id, int flags) {
  if_pf(barrier_splitstate == INSIDE_BARRIER) 
    gasneti_fatalerror("gasnet_barrier_notify() called twice in a row");

  GASNETI_TRACE_PRINTF(B, ("BARRIER_NOTIFY(id=%i,flags=%i)", id, flags));
  #if defined(STATS) || defined(TRACE)
    barrier_notifytime = GASNETI_STATTIME_NOW_IFENABLED(B);
  #endif

  barrier_state->barrier_value = id;
  barrier_state->barrier_flags = flags;

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
        if_pf(flags == GASNET_BARRIERFLAG_MISMATCH) {
          int i;
          barrier_state->barrier_flags = GASNET_BARRIERFLAG_MISMATCH;
          for (i=0; i < gasnete_nodes; i++) {
            elan_wait(elan_put(STATE(), (int *)&(barrier_state->barrier_flags), 
                                        (int *)&(barrier_state->barrier_flags),
                                        sizeof(int), i), ELAN_POLL_EVENT);
          }
        }
        elan_hgsync(GROUP()); 
      } else
    #endif
      {
        elan_hbcast(GROUP(), barrier_state, sizeof(gasnete_barrier_state_t), 0, 1);
        if_pf (flags != barrier_state->barrier_flags || 
           (!(flags & GASNET_BARRIERFLAG_ANONYMOUS) && barrier_state->barrier_value != id) || 
            flags == GASNET_BARRIERFLAG_MISMATCH) { /* detected a mismatch - tell everybody */
          int i;
          barrier_state->barrier_flags = GASNET_BARRIERFLAG_MISMATCH;
          for (i=0; i < gasnete_nodes; i++) {
            elan_wait(elan_put(STATE(), (int *)&(barrier_state->barrier_flags), 
                                        (int *)&(barrier_state->barrier_flags),
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
}

extern int gasnete_barrier_wait(int id, int flags) {
  #if defined(STATS) || defined(TRACE)
    gasneti_stattime_t wait_start = GASNETI_STATTIME_NOW_IFENABLED(B);
  #endif
  if_pf(barrier_splitstate == OUTSIDE_BARRIER) 
    gasneti_fatalerror("gasnet_barrier_wait() called without a matching notify");

  GASNETI_TRACE_EVENT_TIME(B,BARRIER_NOTIFYWAIT,GASNETI_STATTIME_NOW()-barrier_notifytime);

  GASNETI_TRACE_EVENT_TIME(B,BARRIER_WAIT,0);

  /*  update state */
  barrier_splitstate = OUTSIDE_BARRIER;
  if_pf(barrier_state->barrier_flags == GASNET_ERR_BARRIER_MISMATCH ||
        flags != barrier_state->barrier_flags ||
        (!(flags & GASNET_BARRIERFLAG_ANONYMOUS) && id != barrier_state->barrier_value)) 
    return GASNET_ERR_BARRIER_MISMATCH;
  else 
    return GASNET_OK;
}

extern int gasnete_barrier_try(int id, int flags) {
  if_pf(barrier_splitstate == OUTSIDE_BARRIER) 
    gasneti_fatalerror("gasnet_barrier_try() called without a matching notify");

  GASNETI_TRACE_EVENT_VAL(B,BARRIER_TRY,1);
  return gasnete_barrier_wait(id, flags);
}
/* ------------------------------------------------------------------------------------ */
#else /* AM barrier */
static int volatile barrier_value; /*  local barrier value */
static int volatile barrier_flags; /*  local barrier flags */
static int volatile barrier_phase = 0;  /*  2-phase operation to improve pipelining */
static int volatile barrier_response_done[2] = { 0, 0 }; /*  non-zero when barrier is complete */
static int volatile barrier_response_mismatch[2] = { 0, 0 }; /*  non-zero if we detected a mismatch */

/*  global state on P0 */
#define GASNETE_BARRIER_MASTER (gasnete_nodes-1)
static gasnet_hsl_t barrier_lock = GASNET_HSL_INITIALIZER;
static int volatile barrier_consensus_value[2]; /*  consensus barrier value */
static int volatile barrier_consensus_value_present[2] = { 0, 0 }; /*  consensus barrier value found */
static int volatile barrier_consensus_mismatch[2] = { 0, 0 }; /*  non-zero if we detected a mismatch */
static int volatile barrier_count[2] = { 0, 0 }; /*  count of how many remotes have notified (on P0) */

static void gasnete_barrier_notify_reqh(gasnet_token_t token, 
  gasnet_handlerarg_t phase, gasnet_handlerarg_t value, gasnet_handlerarg_t flags) {
  assert(gasnete_mynode == GASNETE_BARRIER_MASTER);

  gasnet_hsl_lock(&barrier_lock);
  { int count = barrier_count[phase];
    if (flags == 0 && !barrier_consensus_value_present[phase]) {
      barrier_consensus_value[phase] = (int)value;
      barrier_consensus_value_present[phase] = 1;
    } else if (flags == GASNET_BARRIERFLAG_MISMATCH ||
               (flags == 0 && barrier_consensus_value[phase] != (int)value)) {
      barrier_consensus_mismatch[phase] = 1;
    }
    barrier_count[phase] = count+1;
  }
  gasnet_hsl_unlock(&barrier_lock);
}

static void gasnete_barrier_done_reqh(gasnet_token_t token, 
  gasnet_handlerarg_t phase,  gasnet_handlerarg_t mismatch) {
  assert(phase == barrier_phase);

  barrier_response_mismatch[phase] = mismatch;
  barrier_response_done[phase] = 1;
}

/*  make some progress on the barrier */
static void gasnete_barrier_kick() {
  int phase = barrier_phase;
  GASNETE_SAFE(gasnet_AMPoll());

  if (gasnete_mynode != GASNETE_BARRIER_MASTER) return;

  /*  master does all the work */
  if (barrier_count[phase] == gasnete_nodes) {
    /*  barrier is complete */
    int i;
    int mismatch = barrier_consensus_mismatch[phase];

    /*  inform the nodes */
    for (i=0; i < gasnete_nodes; i++) {
      GASNETE_SAFE(
        gasnet_AMRequestShort2(i, gasneti_handleridx(gasnete_barrier_done_reqh), 
                             phase, mismatch));
    }

    /*  reset state */
    barrier_count[phase] = 0;
    barrier_consensus_mismatch[phase] = 0;
    barrier_consensus_value_present[phase] = 0;
  }
}

extern void gasnete_barrier_notify(int id, int flags) {
  int phase;
  if_pf(barrier_splitstate == INSIDE_BARRIER) 
    gasneti_fatalerror("gasnet_barrier_notify() called twice in a row");

  GASNETI_TRACE_PRINTF(B, ("BARRIER_NOTIFY(id=%i,flags=%i)", id, flags));
  #if defined(STATS) || defined(TRACE)
    barrier_notifytime = GASNETI_STATTIME_NOW_IFENABLED(B);
  #endif

  barrier_value = id;
  barrier_flags = flags;
  phase = !barrier_phase; /*  enter new phase */
  barrier_phase = phase;

  if (gasnete_nodes > 1) {
    /*  send notify msg to 0 */
    GASNETE_SAFE(
      gasnet_AMRequestShort3(GASNETE_BARRIER_MASTER, gasneti_handleridx(gasnete_barrier_notify_reqh), 
                           phase, barrier_value, flags));
  } else {
    barrier_response_mismatch[phase] = (flags & GASNET_BARRIERFLAG_MISMATCH);
    barrier_response_done[phase] = 1;
  }

  /*  update state */
  barrier_splitstate = INSIDE_BARRIER;
}


extern int gasnete_barrier_wait(int id, int flags) {
  #if defined(STATS) || defined(TRACE)
    gasneti_stattime_t wait_start = GASNETI_STATTIME_NOW_IFENABLED(B);
  #endif
  int phase = barrier_phase;
  if_pf(barrier_splitstate == OUTSIDE_BARRIER) 
    gasneti_fatalerror("gasnet_barrier_wait() called without a matching notify");

  GASNETI_TRACE_EVENT_TIME(B,BARRIER_NOTIFYWAIT,GASNETI_STATTIME_NOW()-barrier_notifytime);

  /*  wait for response */
  while (!barrier_response_done[phase]) {
    gasnete_barrier_kick();
  }

  GASNETI_TRACE_EVENT_TIME(B,BARRIER_WAIT,GASNETI_STATTIME_NOW()-wait_start);

  /*  update state */
  barrier_splitstate = OUTSIDE_BARRIER;
  barrier_response_done[phase] = 0;
  if_pf((!(flags & GASNET_BARRIERFLAG_ANONYMOUS) && id != barrier_value) || 
        flags != barrier_flags || 
        barrier_response_mismatch[phase]) {
        barrier_response_mismatch[phase] = 0;
        return GASNET_ERR_BARRIER_MISMATCH;
  }
  else return GASNET_OK;
}

extern int gasnete_barrier_try(int id, int flags) {
  if_pf(barrier_splitstate == OUTSIDE_BARRIER) 
    gasneti_fatalerror("gasnet_barrier_try() called without a matching notify");

  gasnete_barrier_kick();

  if (barrier_response_done[barrier_phase]) {
    GASNETI_TRACE_EVENT_VAL(B,BARRIER_TRY,1);
    return gasnete_barrier_wait(id, flags);
  }
  else {
    GASNETI_TRACE_EVENT_VAL(B,BARRIER_TRY,0);
    return GASNET_ERR_NOT_READY;
  }
}
#endif
/* ------------------------------------------------------------------------------------ */
/*
  Handlers:
  =========
*/
static gasnet_handlerentry_t const gasnete_handlers[] = {
  /* ptr-width independent handlers */
  #if !GASNETE_USE_ELAN_BARRIER
    gasneti_handler_tableentry_no_bits(gasnete_barrier_notify_reqh),
    gasneti_handler_tableentry_no_bits(gasnete_barrier_done_reqh),
  #endif

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

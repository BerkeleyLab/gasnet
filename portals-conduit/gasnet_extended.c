/*   $Source: /Users/kamil/work/gasnet-cvs2/gasnet/portals-conduit/Attic/gasnet_extended.c,v $
 *     $Date: 2006/07/19 17:54:55 $
 * $Revision: 1.1.2.5 $
 * Description: GASNet Extended API Reference Implementation
 * Copyright 2002, Dan Bonachea <bonachea@cs.berkeley.edu>
 * Terms of use are as specified in license.txt
 */

#include <gasnet_internal.h>
#include <gasnet_extended_internal.h>
#include <gasnet_handler.h>

/* Needed for bootstrap */
#include <catamount/cnos_mpi_os.h>

GASNETI_IDENT(gasnete_IdentString_Version, "$GASNetExtendedLibraryVersion: " GASNET_EXTENDED_VERSION_STR " $");
GASNETI_IDENT(gasnete_IdentString_ExtendedName, "$GASNetExtendedLibraryName: " GASNET_EXTENDED_NAME_STR " $");

gasnete_threaddata_t *gasnete_threadtable[GASNETI_MAX_THREADS] = { 0 };
static int gasnete_numthreads = 0;
static gasnet_hsl_t threadtable_lock = GASNET_HSL_INITIALIZER;
#if GASNETI_CLIENT_THREADS
  /* pthread thread-specific ptr to our threaddata (or NULL for a thread never-seen before) */
  static gasneti_threadkey_t gasnete_threaddata = GASNETI_THREADKEY_INITIALIZER;
#endif
static const gasnete_opaddr_t OPADDR_NIL = { { 0xFF, 0xFF } };
extern void _gasnete_iop_check(gasnete_iop_t *iop) { gasnete_iop_check(iop); }

/* MLW: Not defined in API but exists in Portals implementation */
extern char* ptl_event_str[];
static ptl_process_id_t *ptl_procid_map = NULL;
#if GASNETE_USE_EQ_HANDLER
  #define GASNETE_EQ_HANDLER gasnete_event_handler
#else
  #define GASNETE_EQ_HANDLER NULL
#endif

GASNETI_INLINE(gasnete_opaddr_to_ptr)
gasnete_op_t *gasnete_opaddr_to_ptr(gasnete_threadidx_t threadid, gasnete_opaddr_t opaddr)
{
  gasnete_threaddata_t *th = gasnete_threadtable[GASNETE_THREADID(threadid)];
  return ((threadid & OPTYPE_IMPLICIT)  == OPTYPE_IMPLICIT
	  ? (gasnete_op_t*)(GASNETE_IOPADDR_TO_PTR(th,opaddr))
	  : (gasnete_op_t*)(GASNETE_EOPADDR_TO_PTR(th,opaddr))
	  );
}

/* ------------------------------------------------------------------------------------ */
/*
  Tuning Parameters
  =================
  Conduits may choose to override the default tuning parameters below by defining them
  in their gasnet_core_fwd.h
*/

/* the size threshold where gets/puts stop using medium messages and start using longs */
#ifndef GASNETE_GETPUT_MEDIUM_LONG_THRESHOLD
#define GASNETE_GETPUT_MEDIUM_LONG_THRESHOLD   gasnet_AMMaxMedium()
#endif

/* true if we should try to use Long replies in gets (only possible if dest falls in segment) */
#ifndef GASNETE_USE_LONG_GETS
#define GASNETE_USE_LONG_GETS 1
#endif

/* ------------------------------------------------------------------------------------ */
/*
  Thread Management
  =================
*/
static gasnete_threaddata_t * gasnete_new_threaddata() {
  gasnete_threaddata_t *threaddata = NULL;
  int idx;
  gasnet_hsl_lock(&threadtable_lock);
  GASNETI_TRACE_PRINTF(C,("gasnete_new_threaddata, gasnete_numthreads = %i",gasnete_numthreads));
    idx = gasnete_numthreads;
    gasnete_numthreads++;
  gasnet_hsl_unlock(&threadtable_lock);
  gasneti_assert(GASNETI_MAX_THREADS <= 256);
  #if GASNETI_CLIENT_THREADS
    if (idx >= GASNETI_MAX_THREADS) 
      gasneti_fatalerror("GASNet Extended API: Too many local client threads (limit=%i)",GASNETI_MAX_THREADS);
  #else
    gasneti_assert(idx == 0);
  #endif
  gasneti_assert(gasnete_threadtable[idx] == NULL);

  threaddata = (gasnete_threaddata_t *)gasneti_calloc(1,sizeof(gasnete_threaddata_t));
  GASNETI_TRACE_PRINTF(C,("gasnete_new_threaddata, threaddata = 0x%lx",(uintptr_t)threaddata));

  threaddata->threadidx = idx;
  threaddata->eop_free = OPADDR_NIL;
#if GASNETI_STATS_OR_TRACE
  threaddata->eop_inuse = 0;
  threaddata->eop_hwm = 0;
#endif

  threaddata->iop_free = NULL;
  threaddata->current_iop = NULL;

  gasneti_weakatomic_set(&(threaddata->local_completion_count), 0, 0);
  GASNETI_TRACE_PRINTF(C,("gasnete_new_threaddata, local_compltion_count set"));

  GASNETI_TRACE_PRINTF(C,("gasnete_new_threaddata, idx = %i",idx));
  gasnete_threadtable[idx] = threaddata;

  GASNETI_TRACE_PRINTF(C,("gasnete_new_threaddata, (pre) current_iop = 0x%lx",(uintptr_t)(threaddata->current_iop)));
  threaddata->current_iop = gasnete_iop_new(threaddata);
  GASNETI_TRACE_PRINTF(C,("gasnete_new_threaddata, (post) current_iop = 0x%lx",(uintptr_t)(threaddata->current_iop)));

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
#endif
/* ------------------------------------------------------------------------------------ */
/*
  Initialization
  ==============
*/
/* called at startup to check configuration sanity */
static void gasnete_check_config() {
  gasneti_check_config_postattach();

  gasneti_assert_always(sizeof(gasnete_bb_chunk_t) == GASNETE_BB_CHUNKSIZE);
  gasneti_assert_always(GASNETE_GETPUT_MEDIUM_LONG_THRESHOLD <= gasnet_AMMaxMedium());
  gasneti_assert_always(gasnete_opaddr_isnil(OPADDR_NIL));
}


/* ------------------------------------------------------------------------------------ */
/*
  Op management
  =============
*/
/*  get a new op and mark it in flight */
gasnete_eop_t *gasnete_eop_new(gasnete_threaddata_t * const thread) {
  gasnete_opaddr_t head = thread->eop_free;
  if_pt (!gasnete_opaddr_isnil(head)) {
    gasnete_eop_t *eop = GASNETE_EOPADDR_TO_PTR(thread, head);
#if GASNETI_STATS_OR_TRACE
    thread->eop_inuse++;
    if (thread->eop_inuse > thread->eop_hwm) thread->eop_hwm = thread->eop_inuse;
    GASNETI_TRACE_PRINTF(C,("EOP_NEW: avail, inuse = %d, hwm = %d eop = 0x%lx",thread->eop_inuse,thread->eop_hwm,(uintptr_t)eop));
#endif
    GASNETI_TRACE_EVENT(C,EOP_ALLOC);
    thread->eop_free = eop->addr;   /* next eop in freelist */
    eop->addr = head;               /* my opaddr_t          */
    gasneti_assert(!gasnete_opaddr_equal(thread->eop_free,head));
    gasneti_assert(GASNETE_OP_THREADID(eop) == thread->threadidx);
    gasneti_assert(OPTYPE(eop) == OPTYPE_EXPLICIT);
    gasneti_assert(OPTYPE(eop) == OPSTATE_FREE);
    SET_OPSTATE((gasnete_op_t*)eop, OPSTATE_INFLIGHT);
    return eop;
  } else { /*  free list empty - need more eops */
    int bufidx = thread->eop_num_bufs;
    gasnete_eop_t *buf;
    int i;
    gasnete_threadidx_t threadidx = thread->threadidx;

    if (bufidx == 256) gasneti_fatalerror("GASNet Extended API: Ran out of explicit handles (limit=65535)");
    thread->eop_num_bufs++;
    buf = (gasnete_eop_t *)gasneti_calloc(256,sizeof(gasnete_eop_t));
    GASNETI_TRACE_EVENT(C, EOP_BUCKETS);
    for (i=0; i < 256; i++) {
      gasnete_opaddr_t addr;
      addr.bufferidx = bufidx;
      #if GASNETE_SCATTER_EOPS_ACROSS_CACHELINES
        #ifdef GASNETE_EOP_MOD
          addr.opidx = (i+32) % 255;
        #else
          { int k = i+32;
            addr.opidx = k > 255 ? k - 255 : k;
          }
        #endif
      #else
        /* Remember... addr points to next elem when on free list */
        addr.opidx = i+1;
      #endif
      buf[i].threadidx = threadidx;
      buf[i].addr = addr;
      #if 0 /* these can safely be skipped when the values are zero */
      SET_OPSTATE((gasnete_op_t*)&(buf[i]),OPSTATE_FREE); 
      SET_OPTYPE(gasnete_op_t*)&(buf[i]),OPTYPE_EXPLICIT); 
      #endif
    }
     /*  add a list terminator */
    #if GASNETE_SCATTER_EOPS_ACROSS_CACHELINES
      #ifdef GASNETE_EOP_MOD
        buf[223].addr.opidx = 255; /* modular arithmetic messes up this one */
      #endif
      buf[255].addr = OPADDR_NIL;
    #else
      buf[255].addr = OPADDR_NIL;
    #endif
    thread->eop_bufs[bufidx] = buf;
    head.bufferidx = bufidx;
    head.opidx = 0;
    thread->eop_free = head;

    #if GASNET_DEBUG
    { /* verify new free list got built correctly */
      int i;
      int seen[256];
      gasnete_opaddr_t addr = thread->eop_free;

      #if 0
      if (gasneti_mynode == 0)
        for (i=0;i<256;i++) {                                   
          fprintf(stderr,"%i:  %i: next=%i\n",gasneti_mynode,i,buf[i].addr.opidx);
          fflush(stderr);
        }
        sleep(5);
      #endif

      gasneti_memcheck(thread->eop_bufs[bufidx]);
      memset(seen, 0, 256*sizeof(int));
      for (i=0;i<(bufidx==255?255:256);i++) {                                   
        gasnete_eop_t *eop;                                   
        gasneti_assert(!gasnete_opaddr_isnil(addr));                 
        eop = GASNETE_EOPADDR_TO_PTR(thread,addr);            
        gasneti_assert(OPTYPE(eop) == OPTYPE_EXPLICIT);               
        gasneti_assert(OPSTATE(eop) == OPSTATE_FREE);                 
        gasneti_assert(GASNETE_OP_THREADID(eop) == threadidx);                  
        gasneti_assert(addr.bufferidx == bufidx);
        gasneti_assert(!seen[addr.opidx]);/* see if we hit a cycle */
        seen[addr.opidx] = 1;
        addr = eop->addr;                                     
      }                                                       
      gasneti_assert(gasnete_opaddr_isnil(addr)); 
    }
    #endif

    return gasnete_eop_new(thread); /*  should succeed this time */
  }
}

gasnete_iop_t *gasnete_iop_new(gasnete_threaddata_t * const thread) {
  if_pt (thread->iop_free) {
    gasnete_iop_t *iop = thread->iop_free;

    GASNETI_TRACE_PRINTF(C,("gasnete_iop_new: avail, iop = 0x%lx",(uintptr_t)iop));

    thread->iop_free = iop->next;    
    iop->next = NULL;                
    gasneti_assert(GASNETE_OP_THREADID(iop) == thread->threadidx);
    gasneti_assert(iop == GASNETE_IOPADDR_TO_PTR(thread,iop->addr));
    gasneti_assert(OPTYPE(iop) == OPTYPE_IMPLICIT);
    gasneti_assert(OPSTATE(iop) == OPSTATE_FREE);
    SET_OPSTATE((gasnete_op_t*)iop, OPSTATE_INFLIGHT);
    iop->initiated_get_cnt = 0;
    iop->initiated_put_cnt = 0;
    gasneti_weakatomic_set(&(iop->completed_get_cnt), 0, 0);
    gasneti_weakatomic_set(&(iop->completed_put_cnt), 0, 0);
    gasnete_iop_check(iop);
    return iop;
  } else { /*  free list empty - need more iops */
    int bufidx = thread->iop_num_bufs;
    gasnete_iop_t *buf;
    int i;
    gasnete_threadidx_t threadidx = thread->threadidx;

    GASNETI_TRACE_PRINTF(C,("gasnete_iop_new: Allocating 256 more IOPs"));

    if (bufidx == 256) gasneti_fatalerror("GASNet Extended API: Ran out of implicit handles (limit=65535)");
    thread->iop_num_bufs++;
    buf = (gasnete_iop_t *)gasneti_calloc(256,sizeof(gasnete_iop_t));
    thread->iop_bufs[bufidx] = buf;
    for (i=0; i < 256; i++) {
      gasnete_opaddr_t addr;
      addr.bufferidx = bufidx;
      /* Differing from eops, iops addr always point to themselves */
      addr.opidx = i;
      buf[i].threadidx = threadidx;
      buf[i].addr = addr;
      buf[i].next = thread->iop_free;
      thread->iop_free = &buf[i];
      SET_OPSTATE((gasnete_op_t*)&(buf[i]),OPSTATE_FREE); 
      SET_OPTYPE((gasnete_op_t*)&(buf[i]),OPTYPE_IMPLICIT); 
    }

    #if GASNET_DEBUG
    { /* verify new free list got built correctly */
      gasnete_iop_t *p = thread->iop_free;
      int count = 0;
      gasneti_memcheck(thread->iop_bufs[bufidx]);
      while (p) {
	gasnete_threadidx_t p_th = GASNETE_OP_THREADID(p);
	gasnete_iop_t *me;
	gasneti_assert(threadidx == GASNETE_THREADID(p_th));
	me = GASNETE_IOPADDR_TO_PTR(thread,p->addr);
	gasneti_assert(p == me);
        gasneti_assert(OPTYPE(p) == OPTYPE_IMPLICIT);               
        gasneti_assert(OPSTATE(p) == OPSTATE_FREE);                 
        gasneti_assert(p->addr.bufferidx == bufidx);
	p = p->next;
	count++;
      }
      gasneti_assert(count == 256);
    }
    #endif

    return gasnete_iop_new(thread); /*  should succeed this time */
  }
}

/*  query an op for completeness - for iop this means both puts and gets */
int gasnete_op_isdone(gasnete_op_t *op) {
    gasneti_assert(GASNETE_OP_THREADID(op) == gasnete_mythread()->threadidx);
  if_pt (OPTYPE(op) == OPTYPE_EXPLICIT) {
    gasneti_assert(OPSTATE(op) != OPSTATE_FREE);
    gasnete_eop_check((gasnete_eop_t *)op);
    return OPSTATE(op) == OPSTATE_COMPLETE;
  } else {
    gasnete_iop_t *iop = (gasnete_iop_t*)op;
    gasnete_iop_check(iop);
    return (gasneti_weakatomic_read(&(iop->completed_get_cnt), 0) == iop->initiated_get_cnt) &&
           (gasneti_weakatomic_read(&(iop->completed_put_cnt), 0) == iop->initiated_put_cnt);
  }
}

/*  mark an op done - isget ignored for explicit ops */
void gasnete_op_markdone(gasnete_op_t *op, int isget) {
  if (OPTYPE(op) == OPTYPE_EXPLICIT) {
    gasnete_eop_t *eop = (gasnete_eop_t *)op;
    gasneti_assert(OPSTATE(eop) == OPSTATE_INFLIGHT);
    gasnete_eop_check(eop);
    SET_OPSTATE((gasnete_op_t*)eop, OPSTATE_COMPLETE);
  } else {
    gasnete_iop_t *iop = (gasnete_iop_t *)op;
    gasnete_iop_check(iop);
    if (isget) gasneti_weakatomic_increment(&(iop->completed_get_cnt), 0);
    else gasneti_weakatomic_increment(&(iop->completed_put_cnt), 0);
  }
}

/*  free an op */
void gasnete_op_free(gasnete_op_t *op) {
  gasnete_threaddata_t * const thread = gasnete_threadtable[GASNETE_OP_THREADID(op)];
  gasneti_assert(thread == gasnete_mythread());
  if (OPTYPE(op) == OPTYPE_EXPLICIT) {
    gasnete_eop_t *eop = (gasnete_eop_t *)op;
    gasnete_opaddr_t addr = eop->addr;
    gasneti_assert(OPSTATE(eop) == OPSTATE_COMPLETE);
    gasnete_eop_check(eop);
    SET_OPSTATE((gasnete_op_t*)eop, OPSTATE_FREE);
    eop->addr = thread->eop_free;
    thread->eop_free = addr;
#if GASNETI_STATS_OR_TRACE
    thread->eop_inuse--;
    GASNETI_TRACE_PRINTF(C,("EOP_FREE: inuse = %d, eop = 0x%lx",thread->eop_inuse,(uintptr_t)eop));
    GASNETI_TRACE_EVENT(C,EOP_FREE);
#endif
  } else {
    gasnete_iop_t *iop = (gasnete_iop_t *)op;
    gasnete_iop_check(iop);
    iop->next = thread->iop_free;
    thread->iop_free = iop;
    SET_OPSTATE((gasnete_op_t*)iop, OPSTATE_FREE);
  }
}
/* ------------------------------------------------------------------------------------ */
/* GASNET-Internal OP Interface */
gasneti_eop_t *gasneti_eop_create(GASNETE_THREAD_FARG_ALONE) {
  gasnete_eop_t *op = gasnete_eop_new(GASNETE_MYTHREAD);
  return (gasneti_eop_t *)op;
}
gasneti_iop_t *gasneti_iop_register(unsigned int noperations, int isget GASNETE_THREAD_FARG) {
  gasnete_threaddata_t * const mythread = GASNETE_MYTHREAD;
  gasnete_iop_t * const op = mythread->current_iop;
  gasnete_iop_check(op);
  if (isget) op->initiated_get_cnt += noperations;
  else       op->initiated_put_cnt += noperations;
  gasnete_iop_check(op);
  return (gasneti_iop_t *)op;
}
void gasneti_eop_markdone(gasneti_eop_t *eop) {
  gasnete_op_markdone((gasnete_op_t *)eop, 0);
}
void gasneti_iop_markdone(gasneti_iop_t *iop, unsigned int noperations, int isget) {
  gasnete_iop_t *op = (gasnete_iop_t *)iop;
  gasneti_weakatomic_t * const pctr = (isget ? &(op->completed_get_cnt) : &(op->completed_put_cnt));
  gasnete_iop_check(op);
  if (noperations == 1) gasneti_weakatomic_increment(pctr, 0);
  else {
    #if defined(GASNETI_HAVE_WEAKATOMIC_ADD_SUB)
      gasneti_weakatomic_add(pctr, noperations, 0);
    #else /* yuk */
      while (noperations) {
        gasneti_weakatomic_increment(pctr, 0);
        noperations--;
      }
    #endif
  }
  gasnete_iop_check(op);
}
/* ------------------------------------------------------------------------------------ */
/* Portals data objects used in this extended API implementation */
ptl_handle_ni_t gasnete_ni_h;              /* the network interface handle */
ptl_handle_md_t gasnete_rar_md_h;          /* Handle to RAR Memory Descriptor */
ptl_handle_md_t gasnete_raram_md_h;        /* Handle to RARAM Memory Descriptor */
ptl_handle_md_t gasnete_bb_md_h;           /* Handle to Bounce Buffer Memory Descriptor */
ptl_handle_eq_t gasnete_eq_h;              /* Handle to the combined Event Queue */
static const char* gasnete_md_name[] = {"RARAM_MD","BB_MD","TMP_MD"};

/* WARNING: Not a thread-safe freelist implementation!!!
 * Can easily have one thread pulling something off the list while a portals
 * event handler, executing in another thread is putting a chunk back on the list.
 * Leave as is, for now, but must re-implement for Linux XT3.
 */
static int gasnete_bb_numchunk = GASNETE_BB_NUM_CHUNK;
gasnete_bb_chunk_t *gasnete_bb_freelist;
void* gasnete_bb_start;
#if GASNETI_STATS_OR_TRACE
static int gasnete_bb_inuse = 0;
static int gasnete_bb_hwm = 0;
#endif

/* We limit the number of temporary memory descriptors in use at any time.
 * If over the limit, allocator will poll until the number of outstanding tmp mds
 * drops below the limit.
 */
static int gasnete_max_tmpmd = GASNETE_MAX_TMP_MDS;
static gasneti_weakatomic_t gasnete_tmpmd_count;
#if GASNETI_STATS_OR_TRACE
static int gasnete_tmpmd_hwm = 0;
#endif

#define GASNETE_MAX_POLL_EVENTS 40
static int gasnete_max_poll_events = GASNETE_MAX_POLL_EVENTS;

/* ------------------------------------------------------------------------------------ */
/* Trivial chunk allocator for bounce buffer
 */
void gasnete_bb_init(size_t nchunks)
{
  gasnete_bb_chunk_t *start;
  int i;
  ptl_md_t bb_md;
  size_t nbytes = nchunks * sizeof(gasnete_bb_chunk_t);

  start = (gasnete_bb_chunk_t*)gasneti_malloc(nbytes);
  if (start == NULL) {
    gasneti_fatalerror("failed to alloc bounce buffer at %s",gasneti_current_loc);
  }
  gasnete_bb_start = (void*)start;
  gasnete_bb_freelist = NULL;
  for (i = 0; i < nchunks; i++) {
    start->next = gasnete_bb_freelist;
    gasnete_bb_freelist = start;
    start++;
  }

  /* Do we construct our own event queue or share with others? */

  /* construct a memory descriptor for the bounce buffer */
  bb_md.start = (void*)gasnete_bb_start;
  bb_md.length = nbytes;
  bb_md.threshold = PTL_MD_THRESH_INF;
  bb_md.max_size = 0;
  /* free-floating md that is src of put or dest of get.  Disable start ops */
  bb_md.options = PTL_MD_EVENT_START_DISABLE;
  bb_md.user_ptr = (void*)(uint64_t)GASNETE_BB_MD;
  bb_md.eq_handle = gasnete_eq_h;

  /* register the md and get handle */
  GASNETE_PTLSAFE(PtlMDBind(gasnete_ni_h, bb_md, PTL_RETAIN, &gasnete_bb_md_h));
}

void gasnete_bb_remove()
{
  int nchunk = 0;
  gasnete_bb_chunk_t *p = gasnete_bb_freelist;

  /* check for allocated chunks */
  while (p != NULL) {
    nchunk++;
    p = p->next;
  }
  GASNETI_TRACE_PRINTF(C,("BB_Remove: %d free chunks, expected %d",nchunk,gasnete_bb_numchunk));
  gasneti_assert(nchunk == gasnete_bb_numchunk);

  /* remove Portals MD */
  GASNETE_PTLSAFE(PtlMDUnlink(gasnete_bb_md_h));

  gasneti_free(gasnete_bb_start);

}

int gasnete_bb_chunk_alloc(size_t nbytes, ptl_size_t *offset)
{
    gasnete_bb_chunk_t *p;
    int poll_max = 2;
    int cnt = 0;
    
    if (nbytes > GASNETE_BB_CHUNKSIZE) return 0;
    while ((gasnete_bb_freelist == NULL) && cnt < poll_max) {
      /* poll npoll times, to see if slot frees up */
      GASNETI_SAFE(gasneti_AMPoll());
      cnt++;
    }
    if (gasnete_bb_freelist == NULL) return 0;
    p = gasnete_bb_freelist;
    gasnete_bb_freelist = p->next;
    *offset = ((uint8_t*)p - (uint8_t*)gasnete_bb_start);
#if GASNETI_STATS_OR_TRACE
    gasnete_bb_inuse++;
    if (gasnete_bb_inuse > gasnete_bb_hwm) gasnete_bb_hwm = gasnete_bb_inuse;
    GASNETI_TRACE_PRINTF(C,("BB_ALLOC: inuse = %d, hwm = %d, offset=%ul",gasnete_bb_inuse,gasnete_bb_hwm,(unsigned long)*offset));
    GASNETI_TRACE_EVENT(C, BB_ALLOC);
#endif

    return 1;
}

void gasnete_bb_chunk_free(ptl_size_t offset)
{
    gasnete_bb_chunk_t *p = (gasnete_bb_chunk_t*)((uint8_t*)gasnete_bb_start + offset);
    p->next = gasnete_bb_freelist;
    gasnete_bb_freelist = p;
#if GASNETI_STATS_OR_TRACE
    gasnete_bb_inuse--;
    GASNETI_TRACE_EVENT(C, BB_FREE);
#endif
}

void gasnete_portals_init(void)
{
  /* Set up my RAR MDs */
  void* rar_start   = gasneti_seginfo[gasneti_mynode].addr;
  size_t rar_len    = gasneti_seginfo[gasneti_mynode].size;
  ptl_md_t          md;
  ptl_process_id_t  match_id;
  ptl_handle_me_t   gasnete_rar_mle_h;
  ptl_handle_me_t   gasnete_raram_mle_h;
  ptl_interface_t   ptl_iface;
  int               use_bridge = PTL_BRIDGE_QK;
  int               use_nal = PTL_IFACE_SS;
  ptl_size_t        eq_len;
  int               rc;
  cnos_nidpid_map_t *cnos_map;
  int               my_rank, my_size;
  ptl_process_id_t  my_id;
  int               i;

  /* read Portals specific env vars */
  gasnete_bb_numchunk = (int)gasneti_getenv_int_withdefault("GASNET_PORTAL_NUM_BB",
							    (int64_t)GASNETE_BB_NUM_CHUNK,0);
  gasnete_max_tmpmd = (int)gasneti_getenv_int_withdefault("GASNET_PORTAL_NUM_TMPMD",
							  (int64_t)GASNETE_MAX_TMP_MDS,0);
  gasnete_max_poll_events = (int)gasneti_getenv_int_withdefault("GASNET_PORTAL_MAX_POLL",
							  (int64_t)GASNETE_MAX_POLL_EVENTS,0);
  
  GASNETI_TRACE_PRINTF(C,("Portals_Init: num_bb = %d, max tmp_md = %d, max poll = %d",(int)gasnete_bb_numchunk,(int)gasnete_max_tmpmd,gasnete_max_poll_events));

  if (gasneti_mynode == 0) {
	fprintf(stderr,"Portals_init: max_event = %d\n",gasnete_max_poll_events);
  }

  /* Init the temp md counter to zero */
  gasneti_weakatomic_set(&gasnete_tmpmd_count, 0, 0);

  /* construct the interface */
  /* Hmm, how was it constructed for MPI? Will I get different ni? */
  ptl_iface = IFACE_FROM_BRIDGE_AND_NALID(use_bridge,use_nal);

  /* Get the network handle */
  rc = PtlNIInit(ptl_iface, PTL_PID_ANY, NULL, NULL, &gasnete_ni_h);
  switch (rc) {
  case PTL_OK:
  case PTL_IFACE_DUP:
    break;
  default:
    gasneti_fatalerror("GASNet Portals failed on call to PtlNiInit:\n"
		       "  iface_type = %x error=%s (%i)\n"
		       "  at: %s\n",ptl_iface,ptl_err_str[rc],rc,gasneti_current_loc);
  }

  /* Get my process info, does it match? */
  GASNETE_PTLSAFE(PtlGetId(gasnete_ni_h,&my_id));
  my_rank = cnos_get_rank();
  my_size = cnos_get_size();
  gasneti_assert_always(my_rank == gasneti_mynode);
  gasneti_assert_always(my_size == gasneti_nodes);
  
  /* get process to portals address mapping */
  if (cnos_launcher() == CNOS_LAUNCHER_APRUN) {
    short port = my_id.pid;
    int   rc;
    if((rc=cnos_register_ptlpid(port))) {
      gasneti_fatalerror("cnos_register_ptlpid returned %d",rc);
    }
    cnos_get_nidpid_map(&cnos_map);
  } else if (cnos_launcher() == CNOS_LAUNCHER_YOD) {
    if (my_size != cnos_get_nidpid_map(&cnos_map)) {
      gasneti_fatalerror("cnos_get_nidpid_map size != %d",my_size);
    }
  } else {
    gasneti_fatalerror("Unknown Launcher = %d",cnos_launcher());
  }
  gasneti_assert_always(cnos_map[my_rank].nid == my_id.nid);
  gasneti_assert_always(cnos_map[my_rank].pid == my_id.pid);
  ptl_procid_map = (ptl_process_id_t*)gasneti_malloc(my_size * sizeof(ptl_process_id_t));
  for (i = 0; i < my_size; i++) {
    ptl_procid_map[i].nid = cnos_map[i].nid;
    ptl_procid_map[i].pid = cnos_map[i].pid;
  }

  match_id.nid = PTL_NID_ANY;
  match_id.pid = PTL_PID_ANY;

  /* Insert a MLE at the head of the list */
  GASNETE_PTLSAFE(PtlMEAttach(gasnete_ni_h, GASNETE_PTL_RAR_PTE, match_id, GASNETE_PTL_RAR_BITS,
			      GASNETE_PTL_IGNORE_BITS, PTL_UNLINK, PTL_INS_BEFORE, &gasnete_rar_mle_h));

  /* The RAR does not generate events, but will produce ACKs */
  md.start = rar_start;
  md.length = rar_len;
  md.threshold = PTL_MD_THRESH_INF;
  md.max_size = 0;
  md.options = PTL_MD_OP_PUT | PTL_MD_OP_GET | PTL_MD_MANAGE_REMOTE |
    PTL_MD_EVENT_START_DISABLE | PTL_MD_EVENT_END_DISABLE;
  md.user_ptr = 0;
  md.eq_handle = PTL_EQ_NONE;

  /* Attach this as first Match-list entry in portals table at index RAR_PTE */
  GASNETE_PTLSAFE(PtlMDAttach(gasnete_rar_mle_h, md, PTL_RETAIN, &gasnete_rar_md_h));

  /* On first cut, lets just create a single EQ for all of the MDs that will
   * generate events.  This includes:
   * BounceBuffer:  number = 2*num_chunks;
   * RARAM:         unknown ... scale with num procs? what scaling factor?
   * TMPMD:         2*max number of tmpmds
   */
  eq_len = 2*(gasnete_bb_numchunk + gasnete_max_tmpmd + 10*gasneti_nodes);

  GASNETE_PTLSAFE(PtlEQAlloc(gasnete_ni_h, eq_len, GASNETE_EQ_HANDLER, &gasnete_eq_h));

  /* We create another md to cover the same RAR region but this one will have
   * an EQ.  We will use it as the source MD of a Put or dest MD of a get
   * in the case when the src/dest happens to lie within the RAR.
   * We could make this free-floating but will need it attached to an MLE
   * in the future, when used as the data target of an AM Long
   */
  md.start = rar_start;
  md.length = rar_len;
  md.threshold = PTL_MD_THRESH_INF;
  md.max_size = 0;
  md.options = PTL_MD_OP_PUT | PTL_MD_OP_GET | PTL_MD_MANAGE_REMOTE |
    PTL_MD_EVENT_START_DISABLE;
  md.user_ptr = (uint64_t)GASNETE_RARAM_MD;
  md.eq_handle = gasnete_eq_h;

  GASNETE_PTLSAFE(PtlMEInsert(gasnete_rar_mle_h, match_id, GASNETE_PTL_RARAM_BITS,
			      GASNETE_PTL_IGNORE_BITS, PTL_UNLINK, PTL_INS_AFTER,
			      &gasnete_raram_mle_h));
  GASNETE_PTLSAFE(PtlMDAttach(gasnete_raram_mle_h, md, PTL_RETAIN, &gasnete_raram_md_h));

  /* Now, init the Bounce Buffer */
  gasnete_bb_init(gasnete_bb_numchunk);

#ifndef GASNETE_USE_EQ_HANDLER
  /* Enable the progress function */
  GASNETI_TRACE_PRINTF(C,("Enabling Portals polling function (No EQ Handler)"));
  GASNETI_PROGRESSFNS_ENABLE(gasnete_pf_portals_poll,BOOLEAN);
#endif
}

extern void gasnete_init() {
  static int firstcall = 1;
  GASNETI_TRACE_PRINTF(C,("gasnete_init()"));
  gasneti_assert(firstcall); /*  make sure we haven't been called before */
  firstcall = 0;

  gasnete_check_config(); /*  check for sanity */

  gasneti_assert(gasneti_nodes >= 1 && gasneti_mynode < gasneti_nodes);

#if 1
  GASNETI_TRACE_PRINTF(C,("Sizeof(int) = %i",(int)sizeof(int)));
  GASNETI_TRACE_PRINTF(C,("Sizeof(long) = %i",(int)sizeof(long)));
  GASNETI_TRACE_PRINTF(C,("Sizeof(long long) = %i",(int)sizeof(long long)));
  GASNETI_TRACE_PRINTF(C,("Sizeof(void*) = %i",(int)sizeof(void*)));
  GASNETI_TRACE_PRINTF(C,("Sizeof(ptl_match_bits_t) = %i",(int)sizeof(ptl_match_bits_t)));
  GASNETI_TRACE_PRINTF(C,("Sizeof(gasnete_opaddr_t) = %i",(int)sizeof(gasnete_opaddr_t)));
#endif

  { gasnete_threaddata_t *threaddata = NULL;
    gasnete_eop_t *eop = NULL;
    gasnete_iop_t *iop = NULL;
    #if GASNETI_CLIENT_THREADS
      /* register first thread (optimization) */
      threaddata = gasnete_mythread(); 
    #else
      /* register only thread (required) */
      GASNETI_TRACE_PRINTF(C,("gasnete_init: about to call gasnete_new_threadata()"));
      threaddata = gasnete_new_threaddata();
    #endif

    /* cause the first pool of eops and iops to be allocated (optimization) */
    eop = gasnete_eop_new(threaddata);
    gasnete_op_markdone((gasnete_op_t *)eop, 0);
    gasnete_op_free((gasnete_op_t *)eop);

    /* MLW: add for iops as well */
    iop = gasnete_iop_new(threaddata);
    gasnete_op_free((gasnete_op_t *)iop);
  }

  /* MLW: Allocate Portals resources */
  gasnete_portals_init();
  
  /* Initialize barrier resources */
  gasnete_barrier_init();

  /* Initialize VIS subsystem */
  gasnete_vis_init();
}

/* This is called by gasnetc_exit for the purposes of cleanup-up
 * resources used exclusively by the extended API implementation.
 */
void gasnete_exit(int exitcode)
{
  GASNETI_TRACE_PRINTF(C,("gasnete_exit: outstanding tmp_mds = %d",gasneti_weakatomic_read(&gasnete_tmpmd_count,0)));

  /* remove the RAR and RARAM.  Note that MLE's will be removed by this as well */
  GASNETE_PTLSAFE(PtlMDUnlink(gasnete_rar_md_h));
  GASNETE_PTLSAFE(PtlMDUnlink(gasnete_raram_md_h));

  /* release the bounce buffer */
  gasnete_bb_remove();

  /* free the event queue */
  GASNETE_PTLSAFE(PtlEQFree(gasnete_eq_h));

  GASNETI_TRACE_PRINTF(C,("leaving gasnete_exit"));
  
}

/* 
 * used to print final conduit-extended specific stats
 */
void gasnete_trace_finish(void)
{
  int i;
  for (i=0; i < gasnete_numthreads; i++) {
    GASNETI_STATS_PRINTF(C,("Thread %i, EOP HWM = %i\n",gasnete_threadtable[i]->eop_hwm));
  }
  GASNETI_STATS_PRINTF(C,("Bounce Buffer High Water Mark = %i\n",gasnete_bb_hwm));
  GASNETI_STATS_PRINTF(C,("TMPMD High Water Mark = %i\n",gasnete_tmpmd_hwm));
}

/* Allocate a temp md to be used as the source of a Put or destination
 * of a Get operation.  MD to be free floating, not target of remote op.
 */
ptl_handle_md_t gasnete_alloc_tmpmd(void* dest, size_t nbytes)
{
  ptl_md_t md;
  ptl_handle_md_t md_h;

  /* Want to limit the number of tmp MDs in operation at once.
   * Poll until number of outstanding TMP MDs is less than limit.
   */
  GASNETI_TRACE_PRINTF(C,("Alloc_Tmpmd: num TmpMD outstanding = %d",gasneti_weakatomic_read(&gasnete_tmpmd_count,0)));
  gasneti_pollwhile( (gasneti_weakatomic_read(&gasnete_tmpmd_count,0) >= gasnete_max_tmpmd) );

  gasneti_weakatomic_increment(&gasnete_tmpmd_count,0);
  md.start = dest;
  md.length = nbytes;
  md.threshold = PTL_MD_THRESH_INF;
  md.max_size = 0;
  md.options = PTL_MD_EVENT_START_DISABLE;
  md.user_ptr = (void*)(uint64_t)GASNETE_TMP_MD;
  md.eq_handle = gasnete_eq_h;

  GASNETE_PTLSAFE(PtlMDBind(gasnete_ni_h, md, PTL_RETAIN, &md_h));

#if GASNETI_STATS_OR_TRACE
      {
	int inuse = (int)gasneti_weakatomic_read(&gasnete_tmpmd_count,0);
	if (gasnete_tmpmd_hwm < inuse) gasnete_tmpmd_hwm = inuse;
	GASNETI_TRACE_PRINTF(C,("ALLOC TMPMD at 0x%p, len=%d, inuse=%d, hwm=%d",dest,nbytes,inuse,gasnete_tmpmd_hwm));
	GASNETI_TRACE_EVENT(C, TMPMD_ALLOC);
      }
#endif

  return md_h;
}

/* Extended API polling function.  Process the event queue.
 * This gets called by gasneti_AMPoll() after checking for incoming MPI messages 
 * In the current (extended API-only) implementation, we only have one event queue,
 * so just check it and run appropriate handler
 */

void gasnete_portals_poll(void)
{
  ptl_event_t ev;
  int rc;
  int processed = 0;
  int finished = 0;

  /* Note that PtlEQGet is just a user-space call and will see if anything is
   * on the list without diving into the kernel to see if any unprocessed events
   * are waiting.  Try it first.  If multiple exist, run them back to back since they
   * release resources.
   */
  while (! finished) {
    rc = PtlEQGet( gasnete_eq_h, &ev);
    switch (rc) {
    case PTL_OK:
      GASNETI_TRACE_PRINTF(C,("Q Handler: Got event %s from PtlEQGet",ptl_event_str[ev.type]));
      gasnete_event_handler(&ev);
      processed++;
      if (processed > gasnete_max_poll_events) finished = 1;
      break;
    case PTL_EQ_EMPTY:
      finished = 1;
      break;
    default:
      gasneti_fatalerror("GASNet Portals Error in PtlEQGet: %s (%i)\n at %s\n",
			 ptl_err_str[rc],rc,gasneti_current_loc);
      break;
    }
  }

  if (processed == 0) {
    /* No easy pickings... try polling, which may enter the kernel */
    int which = 0;
    int timeout = 0;       /* number of usec to wait */
    rc = PtlEQPoll(&gasnete_eq_h,1,timeout,&ev,&which);
    switch (rc) {
    case PTL_OK:
      GASNETI_TRACE_PRINTF(C,("Q Handler: Got event %s from PtlEQPoll",ptl_event_str[ev.type]));
      gasnete_event_handler(&ev);
      break;
    case PTL_EQ_EMPTY:
      break;
    default:
      gasneti_fatalerror("GASNet Portals Error in PtlEQPoll: %s (%i)\n at %s\n",
			 ptl_err_str[rc],rc,gasneti_current_loc);
      break;
    }
  }
}

/*
 * We use a single event handler for all the MDs, the "which_md" arg will
 * be set to:
 *    GASNETE_BB_MD:     if the event occured on the Bounce Buffer MD
 *    GASENTE_RARAM_MD:  if the event occured on the RARAM MD
 *    GASENTE_TMP_MD:    if the event occured on a temporary MD
 */
void gasnete_event_handler(ptl_event_t *ev)
{
  ptl_size_t offset = ev->offset;
  ptl_match_bits_t   mbits = ev->match_bits;
  gasnete_threadidx_t threadid;
  gasnete_opaddr_t addr;
  uint8_t msg_type;
  gasnete_op_t *op;
  unsigned int which_md = (unsigned int)(uintptr_t)ev->md.user_ptr;

  gasneti_assert(which_md < 3);

  /* we should never truncate a message */
  gasneti_assert(ev->rlength == ev->mlength);

  gasnete_get_mbits_lowbits(mbits, &threadid, &msg_type, &addr);
  GASNETI_TRACE_PRINTF(C,("EV_handler for event %s on MD %s",ptl_event_str[ev->type],gasnete_md_name[which_md]));
  GASNETI_TRACE_PRINTF(C,("EV_handler offset = %i, mbits = 0x%lx, msg_type = 0x%x",(int)offset,(uint64_t)mbits,msg_type));

  switch (ev->type) {
  case PTL_EVENT_SEND_END:
    /* Work around for Portals implementation bug.  Spec says PTL_EVENT_SEND_END should
     * only occur for Put operations, not Get, but we are getting them.
     * Disambiguate using our own MSG flag in the match bits
     */
    if (msg_type & GASNETE_PTL_MSG_PUT) {
      if (msg_type & GASNETE_PTL_MSG_DOLC) {
	gasnete_threaddata_t *th = gasnete_threadtable[GASNETE_THREADID(threadid)];
	gasneti_weakatomic_decrement(&(th->local_completion_count), 0);
      }
      if (which_md == GASNETE_BB_MD) {
	/* A Put operation through a bounce buffer has completed locally. return chunk. */
	gasnete_bb_chunk_free(offset);
	GASNETI_TRACE_PRINTF(C,("EV_handler freed BB chunk for Put at offset 0x%x",offset));
      }
    }
    /* Ignore this event for GET messages */
    break;
	
  case PTL_EVENT_ACK:
    /* A Put operation has completed remotely */
    gasneti_assert(msg_type & GASNETE_PTL_MSG_PUT);
    op = gasnete_opaddr_to_ptr(threadid, addr);
    if (which_md == GASNETE_TMP_MD) {
      GASNETE_PTLSAFE(PtlMDUnlink(ev->md_handle));
      gasneti_weakatomic_decrement(&gasnete_tmpmd_count,0);

#if GASNETI_STATS_OR_TRACE
      {
	int inuse = (int)gasneti_weakatomic_read(&gasnete_tmpmd_count,0);
	GASNETI_TRACE_PRINTF(C,("FREE TMPMD at 0x%p, len=%d, inuse=%d",ev->md.start,ev->rlength,inuse));
	GASNETI_TRACE_EVENT(C, TMPMD_FREE);
      }
#endif
    }
    /* mark the put (isget=0) operation complete */
    gasnete_op_markdone(op, 0 /* !isget */);
    break;
	
  case PTL_EVENT_REPLY_END:
    /* extract the operation pointer from the match_bits */
    gasneti_assert(msg_type & GASNETE_PTL_MSG_GET);
    gasneti_assert(ev->rlength == ev->mlength);
    op = gasnete_opaddr_to_ptr(threadid, addr);

    if (which_md == GASNETE_BB_MD) {
      /* A Get operation through a bounce buffer has completed.  Copy and reclaim */
      /* The dest addr is contained in first part of chunk, just in front of p */
      uint8_t *pdata = ((uint8_t*)ev->md.start + offset);
      uint8_t *q = pdata - sizeof(void*);
      void *dest;
      /* q points to location where real destination address is stored */
      dest = (void*)*(uintptr_t*)q;
      GASNETI_TRACE_PRINTF(C,("EV_handler copying %i bytes from bb 0x%lx to 0x%lx",ev->mlength,(uintptr_t)pdata,(uintptr_t)dest));
      memcpy(dest,pdata,ev->mlength);
      /* free the bounce buffer */
      offset -= sizeof(void*);
      gasnete_bb_chunk_free(offset);
    } else if (which_md == GASNETE_TMP_MD) {
      /* unlink the md */
      GASNETE_PTLSAFE(PtlMDUnlink(ev->md_handle));
      gasneti_weakatomic_decrement(&gasnete_tmpmd_count,0);

#if GASNETI_STATS_OR_TRACE
      {
	int inuse = (int)gasneti_weakatomic_read(&gasnete_tmpmd_count,0);
	GASNETI_TRACE_PRINTF(C,("FREE TMPMD at 0x%p, len=%d, inuse=%d",ev->md.start,ev->rlength,inuse));
	GASNETI_TRACE_EVENT(C, TMPMD_FREE);
      }
#endif
    }

    /* mark the get (isget=1) operation complete */
    gasnete_op_markdone(op, 1);
    break;
	
  default:
    /* unexpected (erronous) event occurred */
    gasneti_fatalerror("GASNet Portals Unexpected event type in gasnete_event_handler:\n"
		       "  %s [%i]\n  at %s\n",ptl_event_str[ev->type],ev->type,
		       gasneti_current_loc);
  }

}


/* ------------------------------------------------------------------------------------ */
/*
 * Design/Approach for gets/puts in Extended Reference API in terms of Core
 * ========================================================================
 *
 * The extended API implements gasnet_put and gasnet_put_nbi differently, 
 * all in terms of 'nbytes', the number of bytes to be transferred as 
 * payload.
 *
 * The core usually implements AMSmall and AMMedium as host-side copies and
 * AMLongs are implemented according to the implementation.  Some conduits 
 * may optimize AMLongRequest/AMLongRequestAsync/AMLongReply with DMA
 * operations.
 *
 * gasnet_put(_bulk) is translated to a gasnete_put_nb(_bulk) + sync
 * gasnet_get(_bulk) is translated to a gasnete_get_nb(_bulk) + sync
 *
 * gasnete_put_nb(_bulk) translates to
 *    if nbytes < GASNETE_GETPUT_MEDIUM_LONG_THRESHOLD
 *      AMMedium(payload)
 *    else if nbytes < AMMaxLongRequest
 *      AMLongRequest(payload)
 *    else
 *      gasnete_put_nbi(_bulk)(payload)
 *
 * gasnete_get_nb(_bulk) translates to
 *    if nbytes < GASNETE_GETPUT_MEDIUM_LONG_THRESHOLD
 *      AMSmall request + AMMedium(payload) reply
 *    else
 *      gasnete_get_nbi(_bulk)()
 *
 * gasnete_put_nbi(_bulk) translates to
 *    if nbytes < GASNETE_GETPUT_MEDIUM_LONG_THRESHOLD
 *      AMMedium(payload)
 *    else if nbytes < AMMaxLongRequest
 *      AMLongRequest(payload)
 *    else
 *      chunks of AMMaxLongRequest with AMLongRequest()
 *      AMLongRequestAsync is used instead of AMLongRequest for put_bulk
 *
 * gasnete_get_nbi(_bulk) translates to
 *    if nbytes < GASNETE_GETPUT_MEDIUM_LONG_THRESHOLD
 *      AMSmall request + AMMedium(payload) reply
 *    else
 *      chunks of AMMaxMedium with AMSmall request + AMMedium() reply
 *
 * The current implementation uses AMLongs for large puts because the 
 * destination is guaranteed to fall within the registered GASNet segment.
 * The spec allows gets to be received anywhere into the virtual memory space,
 * so we can only use AMLong when the destination happens to fall within the 
 * segment - GASNETE_USE_LONG_GETS indicates whether or not we should try to do this.
 * (conduits which can support AMLongs to areas outside the segment
 * could improve on this through the use of this conduit-specific information).
 * 
 */

/* ------------------------------------------------------------------------------------ */
/*
  Non-blocking memory-to-memory transfers (explicit handle)
  ==========================================================
*/
/* ------------------------------------------------------------------------------------ */
GASNETI_INLINE(gasnete_get_reqh_inner)
void gasnete_get_reqh_inner(gasnet_token_t token, 
  gasnet_handlerarg_t nbytes, void *dest, void *src, void *op) {
  gasneti_assert(nbytes <= gasnet_AMMaxMedium());
  GASNETI_SAFE(
    MEDIUM_REP(2,4,(token, gasneti_handleridx(gasnete_get_reph),
                  src, nbytes, 
                  PACK(dest), PACK(op))));
}
SHORT_HANDLER(gasnete_get_reqh,4,7, 
              (token, a0, UNPACK(a1),      UNPACK(a2),      UNPACK(a3)     ),
              (token, a0, UNPACK2(a1, a2), UNPACK2(a3, a4), UNPACK2(a5, a6)));
/* ------------------------------------------------------------------------------------ */
GASNETI_INLINE(gasnete_get_reph_inner)
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
GASNETI_INLINE(gasnete_getlong_reqh_inner)
void gasnete_getlong_reqh_inner(gasnet_token_t token, 
  gasnet_handlerarg_t nbytes, void *dest, void *src, void *op) {

  GASNETI_SAFE(
    LONG_REP(1,2,(token, gasneti_handleridx(gasnete_getlong_reph),
                  src, nbytes, dest,
                  PACK(op))));
}
SHORT_HANDLER(gasnete_getlong_reqh,4,7, 
              (token, a0, UNPACK(a1),      UNPACK(a2),      UNPACK(a3)     ),
              (token, a0, UNPACK2(a1, a2), UNPACK2(a3, a4), UNPACK2(a5, a6)));
/* ------------------------------------------------------------------------------------ */
GASNETI_INLINE(gasnete_getlong_reph_inner)
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
GASNETI_INLINE(gasnete_put_reqh_inner)
void gasnete_put_reqh_inner(gasnet_token_t token, 
  void *addr, size_t nbytes,
  void *dest, void *op) {
  GASNETE_FAST_UNALIGNED_MEMCPY(dest, addr, nbytes);
  gasneti_sync_writes();
  GASNETI_SAFE(
    SHORT_REP(1,2,(token, gasneti_handleridx(gasnete_markdone_reph),
                  PACK(op))));
}
MEDIUM_HANDLER(gasnete_put_reqh,2,4, 
              (token,addr,nbytes, UNPACK(a0),      UNPACK(a1)     ),
              (token,addr,nbytes, UNPACK2(a0, a1), UNPACK2(a2, a3)));
/* ------------------------------------------------------------------------------------ */
GASNETI_INLINE(gasnete_putlong_reqh_inner)
void gasnete_putlong_reqh_inner(gasnet_token_t token, 
  void *addr, size_t nbytes,
  void *op) {
  gasneti_sync_writes();
  GASNETI_SAFE(
    SHORT_REP(1,2,(token, gasneti_handleridx(gasnete_markdone_reph),
                  PACK(op))));
}
LONG_HANDLER(gasnete_putlong_reqh,1,2, 
              (token,addr,nbytes, UNPACK(a0)     ),
              (token,addr,nbytes, UNPACK2(a0, a1)));
/* ------------------------------------------------------------------------------------ */
GASNETI_INLINE(gasnete_memset_reqh_inner)
void gasnete_memset_reqh_inner(gasnet_token_t token, 
  gasnet_handlerarg_t val, gasnet_handlerarg_t nbytes, void *dest, void *op) {
  memset(dest, (int)(uint32_t)val, nbytes);
  gasneti_sync_writes();
  GASNETI_SAFE(
    SHORT_REP(1,2,(token, gasneti_handleridx(gasnete_markdone_reph),
                  PACK(op))));
}
SHORT_HANDLER(gasnete_memset_reqh,4,6,
              (token, a0, a1, UNPACK(a2),      UNPACK(a3)     ),
              (token, a0, a1, UNPACK2(a2, a3), UNPACK2(a4, a5)));
/* ------------------------------------------------------------------------------------ */
GASNETI_INLINE(gasnete_markdone_reph_inner)
void gasnete_markdone_reph_inner(gasnet_token_t token, 
  void *op) {
  gasnete_op_markdone((gasnete_op_t *)op, 0); /*  assumes this is a put or explicit */
}
SHORT_HANDLER(gasnete_markdone_reph,1,2,
              (token, UNPACK(a0)    ),
              (token, UNPACK2(a0, a1)));
/* ------------------------------------------------------------------------------------ */

extern gasnet_handle_t gasnete_get_nb_bulk (void *dest, gasnet_node_t node, void *src, size_t nbytes GASNETE_THREAD_FARG) {
  if (nbytes <= GASNETE_PTL_MAX_TRANS_SZ) {
    gasnete_eop_t *op = gasnete_eop_new(GASNETE_MYTHREAD);
    ptl_size_t local_offset = 0;
    ptl_size_t remote_offset = GASNETE_PTL_OFFSET(node,src);
    ptl_handle_md_t md_h;
    ptl_process_id_t target_id = ptl_procid_map[node];
    ptl_ac_index_t ac_index = GASNETE_PTL_AC_ID;
    ptl_match_bits_t match_bits = 0UL;
    uint8_t lbits = GASNETE_PTL_RAR_BITS | GASNETE_PTL_MSG_GET;
    /* encode gasnet handle into match bits, upper bits ignored */
    gasnete_set_mbits_lowbits(&match_bits, lbits, (gasnete_op_t*)op);
    /* Determine destination MD for Ptl Get */
    if (gasnete_in_local_rar(dest,nbytes)) {
      md_h = gasnete_raram_md_h;
      local_offset = GASNETE_PTL_OFFSET(gasneti_mynode,dest);
      GASNETI_TRACE_PRINTF(G,("get_nb: into local rar at %i",(int)local_offset));
      GASNETI_TRACE_EVENT(C, GET_NB_RAR);
    } else if ( (nbytes <= (GASNETE_BB_CHUNKSIZE - (sizeof(void*))))  &&
		gasnete_bb_chunk_alloc(nbytes, &local_offset) ) {
      /* Encode dest addr in BB chunk for later copy */
      void* bb;
      md_h = gasnete_bb_md_h;
      /* get the addr of the start of the chunk */
      bb = ((uint8_t*)gasnete_bb_start + local_offset);
      /* store the dest address at this location */
      *(uintptr_t*)bb = (uintptr_t)dest;
      /* Let portals use the rest of the chunk */
      local_offset += sizeof(void*);
      GASNETI_TRACE_PRINTF(G,("get_nb: into bb at 0x%lx offset %i",(uintptr_t)bb,(int)local_offset));
      GASNETI_TRACE_EVENT(C, GET_NB_BB);

    } else {
      /* alloc a temp md for the destination region */
      md_h = gasnete_alloc_tmpmd(dest, nbytes);
      local_offset = 0;
      GASNETI_TRACE_PRINTF(G,("get_nb: into tmpmd, dest= 0x%lx",(uintptr_t)dest));
      GASNETI_TRACE_EVENT(C, GET_NB_TMPMD);
    }

    GASNETI_TRACE_PRINTF(G,("get_nb: match_bits = 0x%lx, remote_off=%i",(uint64_t)match_bits,(int)remote_offset));
    /* Issue Ptl Get operation */
    GASNETE_PTLSAFE(PtlGetRegion(md_h, local_offset, nbytes, target_id, GASNETE_PTL_RAR_PTE, ac_index, match_bits, remote_offset));

    return (gasnet_handle_t)op;
  } else {
    /*  need many messages - use an access region to coalesce them into a single handle */
    /*  (note this relies on the fact that our implementation of access regions allows recursion) */
    gasnete_begin_nbi_accessregion(1 /* enable recursion */ GASNETE_THREAD_PASS);
    gasnete_get_nbi_bulk(dest, node, src, nbytes GASNETE_THREAD_PASS);
    return gasnete_end_nbi_accessregion(GASNETE_THREAD_PASS_ALONE);
  }
}

GASNETI_INLINE(gasnete_put_nb_inner)
gasnet_handle_t gasnete_put_nb_inner(gasnet_node_t node, void *dest, void *src, size_t nbytes, int isbulk GASNETE_THREAD_FARG) {
  if (nbytes <= GASNETE_PTL_MAX_TRANS_SZ) {
    gasnete_threaddata_t * const mythread = GASNETE_MYTHREAD;
    gasnete_eop_t *op = gasnete_eop_new(mythread);
    ptl_size_t local_offset = 0;
    ptl_size_t remote_offset = GASNETE_PTL_OFFSET(node,dest);
    ptl_handle_md_t md_h;
    ptl_process_id_t target_id = ptl_procid_map[node];
    ptl_ac_index_t ac_index = GASNETE_PTL_AC_ID;
    ptl_match_bits_t match_bits = 0ULL;
    uint8_t lbits = GASNETE_PTL_RAR_BITS | GASNETE_PTL_MSG_PUT;
    int wait_for_local_completion = 0;
    ptl_hdr_data_t hdr_data = 0;

    gasneti_assert(gasneti_weakatomic_read(&(mythread->local_completion_count), 0) == 0);

    /* Determine destination MD for Ptl Put */
    if (gasnete_in_local_rar(src,nbytes)) {
      md_h = gasnete_raram_md_h;
      local_offset = GASNETE_PTL_OFFSET(gasneti_mynode,src);
      if (! isbulk) wait_for_local_completion = 1;
      GASNETI_TRACE_PRINTF(P,("put_nb: from local rar at %i",(int)local_offset));
      GASNETI_TRACE_EVENT(C, PUT_NB_RAR);
    } else if ( (nbytes <= GASNETE_BB_CHUNKSIZE)  &&
		gasnete_bb_chunk_alloc(nbytes, &local_offset) ) {
      void* bb;
      md_h = gasnete_bb_md_h;
      /* get the addr of the start of the chunk */
      bb = ((uint8_t*)gasnete_bb_start + local_offset);
      /* copy the src data to the bounce buffer */
      memcpy(bb,src,nbytes);
      GASNETI_TRACE_PRINTF(P,("put_nb: from bb at 0x%lx offset %i",(uintptr_t)bb,(int)local_offset));
      GASNETI_TRACE_EVENT(C, PUT_NB_BB);
    } else {
      /* alloc a temp md for the source region */
      md_h = gasnete_alloc_tmpmd(src, nbytes);
      local_offset = 0;
      if (! isbulk) wait_for_local_completion = 1;
      GASNETI_TRACE_PRINTF(P,("put_nb: from tmpmd, src= 0x%lx",(uintptr_t)src));
      GASNETI_TRACE_EVENT(C, PUT_NB_TMPMD);
    }
    if (wait_for_local_completion) {
      /* increment local completion flag and indicate to event handler to decrement */
      gasneti_weakatomic_increment(&(mythread->local_completion_count), 0);
      lbits |= GASNETE_PTL_MSG_DOLC;
    }

    /* encode gasnet handle into match bits, upper bits ignored */
    gasnete_set_mbits_lowbits(&match_bits, lbits, (gasnete_op_t*)op);
    GASNETI_TRACE_PRINTF(P,("put_nb: match_bits = 0x%lx, remote_off=%i",(uint64_t)match_bits,(int)remote_offset));
    /* Issue Ptl Get operation */
    GASNETE_PTLSAFE(PtlPutRegion(md_h, local_offset, nbytes, PTL_ACK_REQ, target_id, GASNETE_PTL_RAR_PTE, ac_index, match_bits, remote_offset, hdr_data));

    /* poll here for local completion in non-bulk or non-bb case */
    if (wait_for_local_completion) {
      gasneti_pollwhile( (gasneti_weakatomic_read(&(mythread->local_completion_count), 0) > 0) );
    }
    return (gasnet_handle_t)op;

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
  gasnete_eop_t *op = gasnete_eop_new(GASNETE_MYTHREAD);

  GASNETI_SAFE(
    SHORT_REQ(4,6,(node, gasneti_handleridx(gasnete_memset_reqh),
                 (gasnet_handlerarg_t)val, (gasnet_handlerarg_t)nbytes,
                 PACK(dest), PACK(op))));

  return (gasnet_handle_t)op;
}

/* ------------------------------------------------------------------------------------ */
/*
  Synchronization for explicit-handle non-blocking operations:
  ===========================================================
*/

extern int  gasnete_try_syncnb(gasnet_handle_t handle) {
  GASNETI_SAFE(gasneti_AMPoll());

  if (gasnete_op_isdone(handle)) {
    gasneti_sync_reads();
    gasnete_op_free(handle);
    return GASNET_OK;
  }
  else return GASNET_ERR_NOT_READY;
}

extern int  gasnete_try_syncnb_some (gasnet_handle_t *phandle, size_t numhandles) {
  int success = 0;
  int empty = 1;
  GASNETI_SAFE(gasneti_AMPoll());

  gasneti_assert(phandle);

  { int i;
    for (i = 0; i < numhandles; i++) {
      gasnete_op_t *op = phandle[i];
      if (op != GASNET_INVALID_HANDLE) {
        empty = 0;
        if (gasnete_op_isdone(op)) {
	  gasneti_sync_reads();
          gasnete_op_free(op);
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
  GASNETI_SAFE(gasneti_AMPoll());

  gasneti_assert(phandle);

  { int i;
    for (i = 0; i < numhandles; i++) {
      gasnete_op_t *op = phandle[i];
      if (op != GASNET_INVALID_HANDLE) {
        if (gasnete_op_isdone(op)) {
	  gasneti_sync_reads();
          gasnete_op_free(op);
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

extern void gasnete_get_nbi_bulk (void *dest, gasnet_node_t node, void *src, size_t nbytes GASNETE_THREAD_FARG) {
  gasnete_threaddata_t * const mythread = GASNETE_MYTHREAD;
  gasnete_iop_t * const op = mythread->current_iop;
  ptl_process_id_t target_id = ptl_procid_map[node];
  ptl_handle_md_t md_h;
  ptl_ac_index_t ac_index = GASNETE_PTL_AC_ID;
  ptl_size_t local_offset;
  ptl_size_t remote_offset;
  ptl_match_bits_t match_bits = 0ULL;
  uint8_t lbits = GASNETE_PTL_RAR_BITS | GASNETE_PTL_MSG_GET;

  gasneti_assert(gasneti_weakatomic_read(&(mythread->local_completion_count), 0) == 0);

  gasnete_set_mbits_lowbits(&match_bits, lbits, (gasnete_op_t*)op);
  while (nbytes > 0) {
    size_t toget = MIN(nbytes,GASNETE_PTL_MAX_TRANS_SZ);
    local_offset = 0;
    remote_offset = GASNETE_PTL_OFFSET(node,src);
    /* encode gasnet handle into match bits, upper bits ignored */
    /* Determine destination MD for Ptl Get */
    if (gasnete_in_local_rar(dest,toget)) {
      md_h = gasnete_raram_md_h;
      local_offset = GASNETE_PTL_OFFSET(gasneti_mynode,dest);
      GASNETI_TRACE_EVENT(C, GET_NBI_RAR);
    } else if ( (toget <= (GASNETE_BB_CHUNKSIZE - (sizeof(void*))))  &&
		gasnete_bb_chunk_alloc(toget, &local_offset) ) {
      /* Encode dest addr in BB chunk for later copy */
      void* bb;
      md_h = gasnete_bb_md_h;
      /* get the addr of the start of the chunk */
      bb = ((uint8_t*)gasnete_bb_start + local_offset);
      /* store the dest address at this location */
      *(uintptr_t*)bb = (uintptr_t)dest;
      /* Let portals use the rest of the chunk */
      local_offset += sizeof(void*);
      GASNETI_TRACE_EVENT(C, GET_NBI_BB);
    } else {
      /* alloc a temp md for the destination region */
      md_h = gasnete_alloc_tmpmd(dest, toget);
      local_offset = 0;
      GASNETI_TRACE_EVENT(C, GET_NBI_TMPMD);
    }
    /* Issue Ptl Get operation */
    op->initiated_get_cnt++;
    GASNETE_PTLSAFE(PtlGetRegion(md_h, local_offset, toget, target_id, GASNETE_PTL_RAR_PTE, ac_index, match_bits, remote_offset));
    nbytes -= toget;
    dest = ((uint8_t*)dest + toget);
    src = ((uint8_t*)src + toget);
  }
  return;
}

GASNETI_INLINE(gasnete_put_nbi_inner)
void gasnete_put_nbi_inner(gasnet_node_t node, void *dest, void *src, size_t nbytes, int isbulk GASNETE_THREAD_FARG) {
  gasnete_threaddata_t * const mythread = GASNETE_MYTHREAD;
  gasnete_iop_t * const op = mythread->current_iop;
  ptl_handle_md_t md_h;
  ptl_process_id_t target_id = ptl_procid_map[node];
  ptl_ac_index_t ac_index = GASNETE_PTL_AC_ID;
  ptl_match_bits_t match_bits = 0ULL;
  int wait_for_local_completion = 0;
  ptl_hdr_data_t hdr_data = 0;

  gasneti_assert(gasneti_weakatomic_read(&(mythread->local_completion_count), 0) == 0);

  while (nbytes > 0) {
    size_t toput = MIN(nbytes,GASNETE_PTL_MAX_TRANS_SZ);
    ptl_size_t local_offset = 0;
    ptl_size_t remote_offset = GASNETE_PTL_OFFSET(node,dest);
    uint8_t lbits = GASNETE_PTL_RAR_BITS | GASNETE_PTL_MSG_PUT;

    /* Determine destination MD for Ptl Get */
    if (gasnete_in_local_rar(src,toput)) {
      md_h = gasnete_raram_md_h;
      local_offset = GASNETE_PTL_OFFSET(gasneti_mynode,src);
      if (! isbulk) {
	wait_for_local_completion = 1;
	lbits |= GASNETE_PTL_MSG_DOLC;
	gasneti_weakatomic_increment(&(mythread->local_completion_count), 0);
      }
      GASNETI_TRACE_EVENT(C, PUT_NBI_RAR);
    } else if ( (toput <= GASNETE_BB_CHUNKSIZE)  &&
		gasnete_bb_chunk_alloc(toput, &local_offset) ) {
      /* Encode dest addr in BB chunk for later copy */
      void* bb;
      md_h = gasnete_bb_md_h;
      /* get the addr of the start of the chunk */
      bb = ((uint8_t*)gasnete_bb_start + local_offset);
      /* copy the src data to the bounce buffer */
      memcpy(bb,src,toput);
      GASNETI_TRACE_EVENT(C, PUT_NBI_BB);
    } else {
      /* alloc a temp md for the source region */
      md_h = gasnete_alloc_tmpmd(src, toput);
      local_offset = 0;
      if (! isbulk) {
	wait_for_local_completion = 1;
	lbits |= GASNETE_PTL_MSG_DOLC;
	gasneti_weakatomic_increment(&(mythread->local_completion_count), 0);
      }
      GASNETI_TRACE_EVENT(C, PUT_NBI_TMPMD);
    }
    /* encode gasnet handle into match bits, upper bits ignored */
    gasnete_set_mbits_lowbits(&match_bits, lbits, (gasnete_op_t*)op);
    /* Issue Ptl Put operation */
    op->initiated_put_cnt++;
    GASNETE_PTLSAFE(PtlPutRegion(md_h, local_offset, toput, PTL_ACK_REQ, target_id, GASNETE_PTL_RAR_PTE, ac_index, match_bits, remote_offset, hdr_data));

    nbytes -= toput;
    src = ((uint8_t*)src + toput);
    dest = ((uint8_t*)dest + toput);
  }
  /* poll here for local completion in non-bulk or non-bb case */
  if (wait_for_local_completion) {
    gasneti_pollwhile( (gasneti_weakatomic_read(&(mythread->local_completion_count), 0) > 0) );
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

  GASNETI_SAFE(
    SHORT_REQ(4,6,(node, gasneti_handleridx(gasnete_memset_reqh),
                 (gasnet_handlerarg_t)val, (gasnet_handlerarg_t)nbytes,
                 PACK(dest), PACK(op))));
}

/* ------------------------------------------------------------------------------------ */
/*
  Synchronization for implicit-handle non-blocking operations:
  ===========================================================
*/

extern int  gasnete_try_syncnbi_gets(GASNETE_THREAD_FARG_ALONE) {
  #if 0
    /* polling for syncnbi now happens in header file to avoid duplication */
    GASNETI_SAFE(gasneti_AMPoll());
  #endif
  {
    gasnete_threaddata_t * const mythread = GASNETE_MYTHREAD;
    gasnete_iop_t *iop = mythread->current_iop;
    gasneti_assert(GASNETE_OP_THREADID(iop) == mythread->threadidx);
    gasneti_assert(OPTYPE(iop) == OPTYPE_IMPLICIT);
    #if GASNET_DEBUG
      if (iop->next != NULL)
        gasneti_fatalerror("VIOLATION: attempted to call gasnete_try_syncnbi_gets() inside an NBI access region");
    #endif

    if (gasneti_weakatomic_read(&(iop->completed_get_cnt), 0) == iop->initiated_get_cnt) {
      if_pf (iop->initiated_get_cnt > 65000) { /* make sure we don't overflow the counters */
        gasneti_weakatomic_set(&(iop->completed_get_cnt), 0, 0);
        iop->initiated_get_cnt = 0;
      }
      gasneti_sync_reads();
      return GASNET_OK;
    } else return GASNET_ERR_NOT_READY;
  }
}

extern int  gasnete_try_syncnbi_puts(GASNETE_THREAD_FARG_ALONE) {
  #if 0
    /* polling for syncnbi now happens in header file to avoid duplication */
    GASNETI_SAFE(gasneti_AMPoll());
  #endif
  {
    gasnete_threaddata_t * const mythread = GASNETE_MYTHREAD;
    gasnete_iop_t *iop = mythread->current_iop;
    gasneti_assert(GASNETE_OP_THREADID(iop) == mythread->threadidx);
    gasneti_assert(iop->next == NULL);
    gasneti_assert(OPTYPE(iop) == OPTYPE_IMPLICIT);
    #if GASNET_DEBUG
      if (iop->next != NULL)
        gasneti_fatalerror("VIOLATION: attempted to call gasnete_try_syncnbi_puts() inside an NBI access region");
    #endif


    if (gasneti_weakatomic_read(&(iop->completed_put_cnt), 0) == iop->initiated_put_cnt) {
      if_pf (iop->initiated_put_cnt > 65000) { /* make sure we don't overflow the counters */
        gasneti_weakatomic_set(&(iop->completed_put_cnt), 0, 0);
        iop->initiated_put_cnt = 0;
      }
      gasneti_sync_reads();
      return GASNET_OK;
    } else return GASNET_ERR_NOT_READY;
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
  return (gasnet_handle_t)iop;
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
  gasneti_boundscheck(node, src, nbytes);
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
  gasnete_threaddata_t * const thread = gasnete_threadtable[GASNETE_OP_THREADID(handle)];
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

/* use reference implementation of barrier */
#define GASNETI_GASNET_EXTENDED_REFBARRIER_C 1
#include "gasnet_extended_refbarrier.c"
#undef GASNETI_GASNET_EXTENDED_REFBARRIER_C

/* ------------------------------------------------------------------------------------ */
/*
  Vector, Indexed & Strided:
  =========================
*/

/* use reference implementation of scatter/gather and strided */
#include "gasnet_extended_refvis.h"

/* ------------------------------------------------------------------------------------ */
/*
  Collectives:
  ============
*/

/* use reference implementation of collectives */
#include "gasnet_extended_refcoll.h"

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
    GASNETE_REFVIS_HANDLERS()
  #endif
  #ifdef GASNETE_REFCOLL_HANDLERS
    GASNETE_REFCOLL_HANDLERS()
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


/*   $Source: /Users/kamil/work/gasnet-cvs2/gasnet/shmem-conduit/gasnet_extended.c,v $
 *     $Date: 2004/10/07 23:28:15 $
 * $Revision: 1.4 $
 * Description: GASNet Extended API SHMEM Implementation
 * Copyright 2003, Christian Bell <csbell@cs.berkeley.edu>
 * Terms of use are as specified in license.txt
 */

#include <gasnet.h>
#include <gasnet_core_internal.h>
#include <gasnet_extended_internal.h>
#include <gasnet_internal.h>
#include <gasnet_handler.h>

/*
 * Under shmem, nb gets and puts in the same phase get the same handle
 */
#define GASNETE_MAX_HANDLES	    4096
#define GASNETE_HANDLES_MASK	    4095

/*
 * NBs use up to GASNETE_MAX_HANDLES handles, and can have two states:
 *
 * 1. NB_POLL:  The NB is an active message and is currently waiting for a
 *              completion.
 * 2. NB_QUIET: The NB is part of a quiet phase that requires a shmem quiet.
 *              XXX It may be possible to turn NB_QUIET state into DONE state
 *              if a barrier is executed (this seems to depend on various shmem
 *              flavours).
 */

int	    gasnete_handles[GASNETE_MAX_HANDLES];
int	    gasnete_nbi_sync       = 0;
int	    gasnete_handleno_cur   = 1;
int	    gasnete_handleno_phase = 0;

/* 
 * NBIs use a single handle, and has a single phase.  However, it uses two
 * counters to differentiate substates from the NBI state.
 *
 * gasnete_nbi_sync:   Set by puts which require quiets.
 * gasnete_nbi_am_ctr: Incremented each time an AM is sent as part of the NBI.
 *
 */
static int	    gasnete_nbi_region_phase = 0;
static volatile int gasnete_nbi_am_ctr       = 0;
static int	    gasnete_nbi_handle       = GASNETE_HANDLE_DONE;

gasnet_node_t	    gasnete_mynode = (gasnet_node_t)-1;
gasnet_node_t	    gasnete_nodes = 0;
gasnet_seginfo_t *  gasnete_seginfo = NULL;
intptr_t	    gasnete_segment_base = 0;

#ifdef CRAY_SHMEM
uintptr_t gasnete_pe_bits_shift = 0;
uintptr_t gasnete_addr_bits_mask = 0;
#endif

/* shmem-conduit cannot be used with threads. */
#ifdef GASNETI_CLIENT_THREADS
  #error shmem-conduit currently does not support threads
#endif

gasnete_threaddata_t	gasnete_threaddata;
#define gasnete_mythread() (&gasnete_threaddata)

#define GASNETE_HANDLE_INC() do {					      \
    gasnete_handleno_cur = (gasnete_handleno_cur + 1) & GASNETE_HANDLES_MASK; \
    if_pf (gasnete_handles[gasnete_handleno_cur] != GASNETE_HANDLE_DONE)      \
	    gasneti_fatalerror("GASNet ran out of handles (max=%d)",	      \
			    GASNETE_MAX_HANDLES);			      \
    } while (0)
#define GASNETE_HANDLE_INC_PHASE()  do {		    \
	    gasnete_handleno_phase = gasnete_handleno_cur;  \
	    GASNETE_HANDLE_INC();			    \
	} while (0)

extern void 
gasnete_init() 
{
    int	    i;

    GASNETI_TRACE_PRINTF(C,("gasnete_init()"));
    gasneti_assert(gasnete_nodes == 0); /* we haven't been called before */

    gasneti_assert(GASNETC_POW_2(GASNETE_MAX_HANDLES));

    gasnete_mynode = gasnet_mynode();
    gasnete_nodes = gasnet_nodes();

    gasneti_assert(gasnete_nodes >= 1 && gasnete_mynode < gasnete_nodes);
    gasnete_seginfo = (gasnet_seginfo_t *)
		       gasneti_malloc(sizeof(gasnet_seginfo_t)*gasnete_nodes);
    gasnet_getSegmentInfo(gasnete_seginfo, gasnete_nodes);
    gasnete_segment_base = (intptr_t) gasnete_seginfo[gasnete_mynode].addr;

    for (i = 0; i < GASNETE_MAX_HANDLES; i++)
	gasnete_handles[i] = GASNETE_HANDLE_DONE;
}

extern gasnet_handle_t 
gasnete_shmem_put_nb_bulk(gasnet_node_t node, void *dest, void *src, 
		    size_t nbytes GASNETE_THREAD_FARG) 
{
    shmem_putmem(dest, src, nbytes, node);
    gasnete_handles[gasnete_handleno_phase] = GASNETE_HANDLE_NB_QUIET;
    return &gasnete_handles[gasnete_handleno_phase];
}

/*
 * Non-blocking gets are self-syncing, as there is no non-blocking shmem get.
 * We treat this as a special case of explicit non-blocking ops and give them a
 * whole handle to themselves.
 */
extern gasnet_handle_t 
gasnete_get_nb_bulk(void *dest, gasnet_node_t node, void *src, 
		    size_t nbytes GASNETE_THREAD_FARG)
{
    int	*handle = &gasnete_handles[gasnete_handleno_cur];
    gasnete_get_bulk(dest,node,src,nbytes);

    *handle = GASNETE_HANDLE_DONE;
    GASNETE_HANDLE_INC_PHASE();
    return handle;
}

extern gasnet_handle_t
gasnete_global_memset_nb(gasnet_node_t node, void *dest, int val, 
		    size_t nbytes) 
{
    int  *handle = &gasnete_handles[gasnete_handleno_cur];

    memset(dest, val, nbytes);
    gasneti_sync_writes();

    *handle = GASNETE_HANDLE_DONE;
    GASNETE_HANDLE_INC();
    return handle;
}

extern gasnet_handle_t
gasnete_am_memset_nb(gasnet_node_t node, void *dest, int val, 
		    size_t nbytes GASNETE_THREAD_FARG) 
{
    int  *handle = &gasnete_handles[gasnete_handleno_cur];
    int	 *ptr = GASNETE_SHMPTR_AM(dest,node);

    *handle = GASNETE_HANDLE_NB_POLL;

    GASNETE_SAFE(
	SHORT_REQ(4,6,(node, gasneti_handleridx(gasnete_memset_reqh),
		      (gasnet_handlerarg_t)val, (gasnet_handlerarg_t)nbytes, 
		      PACK(ptr), PACK(handle))));

    GASNETE_HANDLE_INC();
    return handle;
}
    
/* ------------------------------------------------------------------------ */
/*
  Synchronization for explicit-handle non-blocking operations:
  ===========================================================
*/
GASNET_INLINE_MODIFIER(gasnete_try_syncnb_inner)
int
gasnete_try_syncnb_inner(gasnet_handle_t handle)
{
    switch (*handle) {
	case GASNETE_HANDLE_DONE:
	    return GASNET_OK;
	break;

	case GASNETE_HANDLE_NB_POLL:
	    GASNETE_SAFE(gasnet_AMPoll());
	    if (*handle == GASNETE_HANDLE_DONE)
		return GASNET_OK;
	    else
		return GASNET_ERR_NOT_READY;
	    break;

	case GASNETE_HANDLE_NB_QUIET:
	    gasneti_assert(handle == 
			   &(gasnete_handles[gasnete_handleno_phase]));
	    shmem_quiet();
	    *handle = GASNETE_HANDLE_DONE;
	    GASNETE_HANDLE_INC_PHASE();
	    return GASNET_OK;
	    break;

	case GASNETE_HANDLE_NBI:
	    gasneti_assert(handle == &gasnete_nbi_handle);
	    /* Quiet iff at least one put */
	    if (gasnete_nbi_sync || GASNETE_NBISYNC_ALWAYS_QUIET) {
		shmem_quiet();
		gasnete_nbi_sync = 0;
	    }
	    if (gasnete_nbi_am_ctr == 0) {
		*handle = GASNETE_HANDLE_DONE;
		return GASNET_OK;
	    }
	    *handle = GASNETE_HANDLE_NBI_POLL;
	    /* Fallthrough, poll and poll only next time */
	
	case GASNETE_HANDLE_NBI_POLL:
	    gasneti_assert(handle == &gasnete_nbi_handle);
	    GASNETE_SAFE(gasnet_AMPoll());
	    if (gasnete_nbi_am_ctr == 0) {
		*handle = GASNETE_HANDLE_DONE;
		return GASNET_OK;
	    }
	    else
		return GASNET_ERR_NOT_READY;
	    break;

	default:
	    gasneti_fatalerror("Invalid handle value %d", (int)*handle);
	    break;
    }

    /* XXX can't reach */
    gasneti_fatalerror("can't reach in syncnb_inner");
    return GASNET_OK;
}

extern int
gasnete_try_syncnb(gasnet_handle_t handle)
{
	return gasnete_try_syncnb_inner(handle);
}

extern int  
gasnete_try_syncnb_some (gasnet_handle_t *phandle, size_t numhandles) 
{
    int	i;
    GASNETE_SAFE(gasnet_AMPoll());

    gasneti_assert(phandle != NULL);

    for (i = 0; i < numhandles; i++) {
	if_pf (phandle[i] == GASNET_INVALID_HANDLE)
	    continue;
	gasnete_try_syncnb_inner(phandle[i]);
    }

    return GASNET_OK;
}

/*
 * gasnete_try_syncnb_all() is the same as gasnete_try_syncnb_some()
 */

/* ------------------------------------------------------------------------ */
/*
  Non-blocking memory-to-memory transfers (implicit handle)
  ==========================================================
*/

extern void 
gasnete_global_memset_nbi(gasnet_node_t node, void *dest, int val, 
  		    size_t nbytes) 
{
      /* By doing a synchronous write and flushing the write buffer, there's no
       * need to poll for completion */
      memset(dest, val, nbytes);
      gasneti_sync_writes();
      return;
}

extern void 
gasnete_am_memset_nbi(gasnet_node_t node, void *dest, int val, 
		    size_t nbytes GASNETE_THREAD_FARG) 
{
    int	 *ptr = GASNETE_SHMPTR_AM(dest,node);
    int *p_nbi_handle = &gasnete_nbi_handle;
    gasnete_nbi_handle = GASNETE_HANDLE_NBI;
    GASNETE_SAFE(
	SHORT_REQ(4,6,(node, gasneti_handleridx(gasnete_memset_reqh),
		      (gasnet_handlerarg_t)val, (gasnet_handlerarg_t)nbytes, 
		      PACK(ptr), PACK(p_nbi_handle))));

    gasnete_nbi_am_ctr++;

    return;
}

/* ------------------------------------------------------------------------------------ */
/*
  Synchronization for implicit-handle non-blocking operations:
  ===========================================================
*/

extern int  
gasnete_try_syncnbi_gets(GASNETE_THREAD_FARG_ALONE) 
{
    #if GASNET_DEBUG
    if (gasnete_nbi_region_phase)
        gasneti_fatalerror(
	    "VIOLATION: attempted to call gasnete_try_syncnbi_gets() "
	    "inside an NBI access region");
    #endif

    /* All gets are blocking. Unless there are puts or ams in flight, the nbi
     * handle can be set as done. */
    if (gasnete_nbi_am_ctr == 0 && gasnete_nbi_sync == 0)
	gasnete_nbi_handle = GASNETE_HANDLE_DONE;

    return GASNET_OK;
}

extern int
gasnete_try_syncnbi_puts(GASNETE_THREAD_FARG_ALONE) 
{
    #if GASNET_DEBUG
    if (gasnete_nbi_region_phase)
        gasneti_fatalerror(
	    "VIOLATION: attempted to call gasnete_try_syncnbi_puts() "
	    "inside an NBI access region");

    #endif
    return gasnete_try_syncnb_inner(&gasnete_nbi_handle);
}

/* ------------------------------------------------------------------------------------ */
/*
  Implicit access region synchronization
  ======================================
*/
/*  This implementation allows recursive access regions, although the spec does not require that */
/*  operations are associated with the most immediately enclosing access region */
extern void            
gasnete_begin_nbi_accessregion(int allowrecursion GASNETE_THREAD_FARG) 
{
    GASNETI_TRACE_PRINTF(S,("BEGIN_NBI_ACCESSREGION"));
    #if GASNET_DEBUG
    if (!allowrecursion && gasnete_nbi_region_phase)
	gasneti_fatalerror(
	    "VIOLATION: tried to initiate a recursive NBI access region");
    #endif

    gasnete_nbi_region_phase = 1;

    /* Only reset the nbi handle if previous ops were sync'd */
    if (gasnete_nbi_handle == GASNETE_HANDLE_DONE) {
	gasnete_nbi_handle = GASNETE_HANDLE_NBI;
	gasnete_nbi_sync = 0;
	gasnete_nbi_am_ctr = 0;
    }
}

extern gasnet_handle_t 
gasnete_end_nbi_accessregion(GASNETE_THREAD_FARG_ALONE) 
{
  /* GASNETI_TRACE_EVENT_VAL(S,END_NBI_ACCESSREGION,iop->initiated_get_cnt + * iop->initiated_put_cnt); */
    #if GASNET_DEBUG
    if (!gasnete_nbi_region_phase)
	gasneti_fatalerror(
	    "VIOLATION: tried to end an accessregion before starting one");
    #endif

    gasnete_nbi_region_phase = 0;
    return &gasnete_nbi_handle;
}

/* ------------------------------------------------------------------------------------ */
/*
  Barriers:
  =========
*/

#ifndef GASNETE_SHMEM_BARRIER
  /* use reference implementation of barrier */
  #define GASNETI_GASNET_EXTENDED_REFBARRIER_C 1
  #define gasnete_refbarrier_notify  gasnete_barrier_notify
  #define gasnete_refbarrier_wait    gasnete_barrier_wait
  #define gasnete_refbarrier_try     gasnete_barrier_try
  #include "gasnet_extended_refbarrier.c"
  #undef GASNETI_GASNET_EXTENDED_REFBARRIER_C
/* ------------------------------------------------------------------------------------ */
#else /* GASNETE_SHMEM_BARRIER */

/*
 * Atomic-inc/compare-and-swap based shmem barrier algorithm.
 *
 * Both Altix/X1 provide low-latecy atomic operations.  We use them here for
 * both named and anonymous barriers.  The algorithm works as follows.
 *
 * Notify (X1 and Altix):
 *   if value is client mismatche, broadcast a mismatch bit on each node
 *   if named barrier 
 *      cswap value located on node 0.
 *      if value mismatches, broadcast a mismatch bit on each node
 *  atomic inc counter on node 0.
 *
 * Wait (X1):
 *   check for local mismatches between local notify and local wait
 *   if node==0, spin-poll on atomic_inc counter for all node increments
 *               broadcast done bit to each node
 *   if node!=0, spin-poll on local counter until updated by 0.
 *      
 * Wait (Altix):
 *   check for local mismatches between local notify and local wait
 *   if node==0, spin-poll on atomic_inc counter for all node increments
 *               broadcast LOCAL done bit
 *   if node!=0, spin-poll on counter living at node 0.
 * 
 * On Altix, each node reads the counter living at node 0 (on an only cache
 * line) as cc-NUMA directory broadcasts outperform user-invoked serial
 * broadcast.
 *
 * On X1, performance-critical code paths replace the shmem_ptr shmem library
 * translation function with GASNet's inllined GASNETE_TRANSLATE_X1 macro.
 */

#define BARRIER_PAD_CACHELINE_SIZE 128

#ifdef SGI_SHMEM
#define BARRIER_READ_NOTIFYCTR	1
#define _BARRIER_PAD(name)  \
	static char __barrier_pad ## name[BARRIER_PAD_CACHELINE_SIZE] = { 0 }
#else
#define BARRIER_READ_NOTIFYCTR	0
//#define _BARRIER_PAD(name) 
#define _BARRIER_PAD(name)  \
	static char __barrier_pad ## name[BARRIER_PAD_CACHELINE_SIZE] = { 0 }
#endif


#if GASNETI_STATS_OR_TRACE
  static gasneti_stattime_t barrier_notifytime; /* for statistical purposes */ 
#endif
static 
enum { OUTSIDE_BARRIER, INSIDE_BARRIER } 
barrier_splitstate = OUTSIDE_BARRIER;

typedef 
struct {
    long volatile barrier_value;
    long volatile barrier_flags;
} 
gasnete_barrier_state_t;

#define BARRIER_INITVAL 0x1234567800000000

static long barrier_value[2] = { BARRIER_INITVAL, BARRIER_INITVAL };
static int  barrier_mismatch[2] = { 0, 0 };
static int  barrier_phase = 0;

/* On Altix, ensure value, done and notify_ctr live on different cache lines */
_BARRIER_PAD(n0);
static long volatile		    barrier_done[2] = { 0, 0 };
_BARRIER_PAD(n1);
static long volatile		    barrier_notify_ctr[2] = { 0, 0 };
static gasnete_barrier_state_t	    barrier_state[2];

extern void
gasnete_barrier_notify(int id, int flags)
{
    int i;
    long curval;
    if_pf (barrier_splitstate == INSIDE_BARRIER)
	gasneti_fatalerror("gasnet_barrier_notify() called twice in a row");

    GASNETI_TRACE_PRINTF(B, ("BARRIER_NOTIFY(id=%i,flags=%i)", id, flags));
    #if GASNETI_STATS_OR_TRACE
      barrier_notifytime = GASNETI_STATTIME_NOW_IFENABLED(B);
    #endif

    barrier_phase = !barrier_phase;
    barrier_state[barrier_phase].barrier_value = id;
    barrier_state[barrier_phase].barrier_flags = flags;

    /*
     * Client-initiated mismatch -- broadcast to the mismatch flag.  Operation
     * is in a failure, non-optimized code path.
     */
    if (flags & GASNET_BARRIERFLAG_MISMATCH) {
	for (i=0; i < gasnete_nodes; i++) 
	    *((int *)shmem_ptr(&barrier_mismatch[barrier_phase], i)) = 1;
    }
    else if (!(flags & GASNET_BARRIERFLAG_ANONYMOUS)) {
	#ifdef CRAYX1
	    curval = _amo_acswap(
		    GASNETE_TRANSLATE_X1(&barrier_value[barrier_phase], 0), 
		    BARRIER_INITVAL, (long) id);
	#else
	    curval = shmem_long_cswap(&barrier_value[barrier_phase], 
				      BARRIER_INITVAL, (long) id, 0);
	#endif
	/*
	 * Value mismatch -- broadcast to the mismatch flag. Operation is in a
	 * failure, non-optimized path.
	 */
	if_pf (curval != BARRIER_INITVAL && curval != id) {
	    for (i=0; i < gasnete_nodes; i++)
		*((int *)shmem_ptr(&barrier_mismatch[barrier_phase], i)) = 1;
	}
    }
	
    /* Atomic increment at node 0 */
    #ifdef CRAYX1
	_amo_aadd(GASNETE_TRANSLATE_X1(&barrier_notify_ctr[barrier_phase], 0), 
		  1);
    #else
	shmem_long_finc((long*)&barrier_notify_ctr[barrier_phase], 0);
    #endif

    barrier_splitstate = INSIDE_BARRIER;
    gasneti_sync_writes();
}

extern int
gasnete_barrier_wait(int id, int flags)
{
    int  i, local_mismatch = 0;
    long volatile *done_ctr = &barrier_done[barrier_phase];

  #if GASNETI_STATS_OR_TRACE
    gasneti_stattime_t wait_start = GASNETI_STATTIME_NOW_IFENABLED(B);
  #endif
    gasneti_sync_reads();

    if_pf(barrier_splitstate == OUTSIDE_BARRIER) 
	gasneti_fatalerror(
	    "gasnet_barrier_wait() called without a matching notify");

    GASNETI_TRACE_EVENT_TIME(B,BARRIER_NOTIFYWAIT,
			       GASNETI_STATTIME_NOW()-barrier_notifytime);

    GASNETI_TRACE_EVENT_TIME(B,BARRIER_WAIT,0);

    barrier_splitstate = OUTSIDE_BARRIER;
    gasneti_sync_writes();

    /*
     * First check for local (non-collective) mismatches
     */
    if_pf (flags & GASNET_BARRIERFLAG_MISMATCH ||
	   flags != barrier_state[barrier_phase].barrier_flags ||
	   (!(flags & GASNET_BARRIERFLAG_ANONYMOUS) &&
	     id != barrier_state[barrier_phase].barrier_value)) {
	local_mismatch = 1;
    }

    if (gasnete_mynode == 0) {
	long volatile *not_ctr = &barrier_notify_ctr[barrier_phase];

	/* Wait until all nodes have updated value */
	GASNET_BLOCKUNTIL(*not_ctr == gasnete_nodes);
	*not_ctr = 0;

	/*
	 * Broadcast the return value for global mismatches
	 */
	#if BARRIER_READ_NOTIFYCTR
	    /* This is the only safe point to reset the barrier done flag of
	     * the barrier on "the other" phase. */
	    barrier_done[!barrier_phase] = 0;
	    *done_ctr = 1;
	#else
	    //GASNETC_VECTORIZE
	    for (i=0; i < gasnete_nodes; i++) 
		#ifdef CRAYX1
		    *((long *) GASNETE_TRANSLATE_X1(done_ctr, i)) = 1;
		#else
		    shmem_long_p((long *)done_ctr, 1, i);
		#endif
	#endif

	barrier_value[barrier_phase] = BARRIER_INITVAL;
    }
    else {
	#if BARRIER_READ_NOTIFYCTR
	    #ifdef CRAYX1
		done_ctr = GASNETE_TRANSLATE_X1((void *)done_ctr, 0);
	    #else
		done_ctr = shmem_ptr((void *)done_ctr, 0);
	    #endif
	    /* Wait for notification from node 0 */
	    GASNET_BLOCKUNTIL(*done_ctr != 0);
	    /* Don't reset counter, it lives on zero! */
	#else
	    /* Spin on a local counter and reset it is set by node 0 */
	    GASNET_BLOCKUNTIL(*done_ctr != 0);
	    *done_ctr = 0;
	#endif

    }

    gasneti_sync_writes();

    if (local_mismatch || barrier_mismatch[barrier_phase]) {
	barrier_mismatch[barrier_phase] = 0;
	return GASNET_ERR_BARRIER_MISMATCH;
    }
    else
	return GASNET_OK;
}

extern int 
gasnete_barrier_try(int id, int flags) 
{
    if_pf(barrier_splitstate == OUTSIDE_BARRIER)
	gasneti_fatalerror(
	    "gasnet_barrier_try() called without a matching notify");
    GASNETI_TRACE_EVENT_VAL(B,BARRIER_TRY,1);
    return gasnete_barrier_wait(id, flags);
}
#endif

/* ------------------------------------------------------------------------ */
GASNET_INLINE_MODIFIER(gasnete_memset_reqh_inner)
void 
gasnete_memset_reqh_inner(gasnet_token_t token, gasnet_handlerarg_t val, 
			  gasnet_handlerarg_t nbytes, void *dest, void *op) 
{
    memset(dest, (int)(uint32_t)val, nbytes);
    gasneti_sync_writes();

    GASNETE_SAFE(
	SHORT_REP(1,2,(token, gasneti_handleridx(gasnete_markdone_reph),
                  PACK(op))));
}
SHORT_HANDLER(gasnete_memset_reqh,4,6,
              (token, a0, a1, UNPACK(a2),      UNPACK(a3)     ),
              (token, a0, a1, UNPACK2(a2, a3), UNPACK2(a4, a5)));
/* ------------------------------------------------------------------------ */
GASNET_INLINE_MODIFIER(gasnete_markdone_reph_inner)
void 
gasnete_markdone_reph_inner(gasnet_token_t token, void *h) 
{
    int	*handle  = (int *) h;

    if (handle == &gasnete_nbi_handle)		/* NBI */ {
	    gasnete_nbi_am_ctr--;
    }
    else					/* NB */ {
	    *handle = GASNETE_HANDLE_DONE;
    }
    return;
}
SHORT_HANDLER(gasnete_markdone_reph,1,2,
              (token, UNPACK(a0)    ),
              (token, UNPACK2(a0, a1)));
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

/* ------------------------------------------------------------------------ */
/*
  Handlers:
  =========
*/
static gasnet_handlerentry_t const 
gasnete_handlers[] = {
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
    gasneti_handler_tableentry_with_bits(gasnete_memset_reqh),
    gasneti_handler_tableentry_with_bits(gasnete_markdone_reph),
  { 0, NULL }
};

extern gasnet_handlerentry_t const *
gasnete_get_handlertable() {
  return gasnete_handlers;
}

/* ------------------------------------------------------------------------ */

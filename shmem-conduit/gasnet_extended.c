/*  $Archive:: $
 *     $Date: 2003/11/23 12:58:50 $
 * $Revision: 1.1.2.3 $
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

static void gasnete_barrier_init();

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

static int	    gasnete_handles[GASNETE_MAX_HANDLES];
static int	    gasnete_handleno_cur   = 1;
static int	    gasnete_handleno_phase = 0;

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
int		    gasnete_nbi_sync	     = 0;
static int	    gasnete_nbi_handle       = GASNETE_HANDLE_DONE;

gasnet_node_t	    gasnete_mynode = (gasnet_node_t)-1;
gasnet_node_t	    gasnete_nodes = 0;
gasnet_seginfo_t *  gasnete_seginfo = NULL;
intptr_t	    gasnete_segment_base = 0;

#ifdef CRAY_SHMEM
uintptr_t gasnete_pe_bits_shift = 0;
uintptr_t gasnete_addr_bits_mask = 0;
#endif


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

    gasnete_barrier_init();
}

extern gasnet_handle_t 
gasnete_put_nb_bulk(gasnet_node_t node, void *dest, void *src, 
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
    gasnete_inline_get(dest,node,src,nbytes);

    *handle = GASNETE_HANDLE_DONE;
    GASNETE_HANDLE_INC_PHASE();
    return handle;
}

/*
 * Non-blocking memset
 */
#if 0 
//def GASNETE_SHMALLOC_SEGMENT
extern gasnet_handle_t
gasnete_memset_nb(gasnet_node_t node, void *dest, int val, 
		    size_t nbytes GASNETE_THREAD_FARG) 
{
    void *ptr = GASNETE_SHMPTR(dest, node);
    int  *handle = &gasnete_handles[gasnete_handleno_cur];

    printf("memset to %d,%p should send to %p shmptr = %p\n", node, dest, GASNETE_SHMPTR(dest,node), shmem_ptr(dest,node));
    memset(ptr, val, nbytes);
    gasneti_memsync();	/* XXX _gsync on Cray? */

    *handle = GASNETE_HANDLE_DONE;
    GASNETE_HANDLE_INC();
    return handle;
}
#else
extern gasnet_handle_t
gasnete_memset_nb(gasnet_node_t node, void *dest, int val, 
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
#endif
    
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

	case GASNETE_HANDLE_NBI:
	    gasneti_assert(handle = &gasnete_nbi_handle);
	    if (gasnete_nbi_sync) {
		shmem_quiet();
		gasnete_nbi_sync = 0;
	    }
	    if (gasnete_nbi_am_ctr == 0) {
		*handle = GASNETE_HANDLE_DONE;
		return GASNET_OK;
	    }
	    *handle = GASNETE_HANDLE_NBI_POLL;
	    /* Fallthrough, poll and poll next time */
	
	case GASNETE_HANDLE_NBI_POLL:
	    gasneti_assert(handle = &gasnete_nbi_handle);
	    GASNETE_SAFE(gasnet_AMPoll());
	    if (gasnete_nbi_am_ctr == 0) {
		*handle = GASNETE_HANDLE_DONE;
		return GASNET_OK;
	    }
	    else
		return GASNET_ERR_NOT_READY;
	break;

	default:
	    gasneti_fatalerror("Invalid handle %d", handle);
	    break;
    }

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

/* TODO: Find a way to detect the Altix 3000 at configure time, so it can be
 * set as a shmalloc segment
 */
#ifdef GASNETE_SHMALLOC_SEGMENT
extern void 
gasnete_memset_nbi(gasnet_node_t node, void *dest, int val, 
		    size_t nbytes GASNETE_THREAD_FARG) 
{
    void *ptr = GASNETE_SHMPTR(dest,node);
    printf("memset to %d,%p should send to %p\n", node, dest, GASNETE_SHMPTR(dest,node));
    memset(ptr, val, nbytes);
    shmem_quiet();
    gasneti_memsync();
    return;
}
#else
extern void 
gasnete_memset_nbi(gasnet_node_t node, void *dest, int val, 
		    size_t nbytes GASNETE_THREAD_FARG) 
{
    GASNETE_SAFE(
	SHORT_REQ(4,6,(node, gasneti_handleridx(gasnete_memset_reqh),
		      (gasnet_handlerarg_t)val, (gasnet_handlerarg_t)nbytes, 
		      PACK(GASNETE_SHMPTR(dest,node)), PACK(&gasnete_nbi_handle))));

    gasnete_nbi_am_ctr++;
    return;
}
#endif

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

    /* All gets are blocking ! */
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
    gasnete_try_syncnb_inner(&gasnete_nbi_handle);
    return GASNET_OK;

    shmem_quiet();

    /* Some AMs may still be outstanding, we don't wait for those yet */
    if (gasnete_nbi_am_ctr > 0)
	    gasnete_nbi_handle = GASNETE_HANDLE_NBI_POLL;
    else
	    gasnete_nbi_handle = GASNETE_HANDLE_DONE;

    return GASNET_OK;
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
    if (!allowrecursion || gasnete_nbi_handle == GASNETE_HANDLE_DONE) {
	gasnete_nbi_handle = GASNETE_HANDLE_NBI;

	gasnete_nbi_sync = 0;
	gasnete_nbi_am_ctr = 0;
	gasnete_nbi_region_phase = 1;
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
  Non-Blocking Value Get (explicit-handle)
  ========================================
*/

#if SIZEOF_LONG == 8
#define GASNET_SHMEM_GET_8  shmem_long_g
#define GASNET_SHMEM_PUT_8  shmem_long_p
#elif SIZEOF_DOUBLE == 8
#define GASNET_SHMEM_GET_8  shmem_double_g
#define GASNET_SHMEM_PUT_8  shmem_double_p
#endif

#if SIZEOF_LONG == 4
#define GASNET_SHMEM_GET_4  shmem_long_g
#define GASNET_SHMEM_PUT_4  shmem_long_p
#elif SIZEOF_INT == 4
#define GASNET_SHMEM_GET_4  shmem_int_g
#define GASNET_SHMEM_PUT_4  shmem_int_p
#elif SIZEOF_SHORT == 4
#define GASNET_SHMEM_GET_4  shmem_short_g
#define GASNET_SHMEM_PUT_4  shmem_short_p
#elif SIZEOF_FLOAT == 4
#define GASNET_SHMEM_GET_4  shmem_float_g
#define GASNET_SHMEM_PUT_4  shmem_float_p
#endif

#if SIZEOF_SHORT == 2
#define GASNET_SHMEM_GET_2  shmem_short_g
#define GASNET_SHMEM_PUT_2  shmem_short_p
#endif

static uint64_t		_gasnete_getval_temp64;
static uint32_t		_gasnete_getval_temp32;
static uint16_t		_gasnete_getval_temp16;
static uint8_t		_gasnete_getval_temp8;
static gasnet_register_value_t _gasnete_getval_tempA;

extern gasnet_valget_handle_t 
gasnete_get_nb_val(gasnet_node_t node, void *src, 
		   size_t nbytes GASNETE_THREAD_FARG) 
{
    switch (nbytes) {
	case 8:	
	    #ifdef GASNET_SHMEM_GET_8
		return (gasnet_valget_handle_t) GASNET_SHMEM_GET_8(src, node);
	    #else
		shmem_getmem((void *) &_gasnete_getval_temp64,src,8,node);
		return (gasnet_valget_handle_t) _gasnete_getval_temp64;
	    #endif

	case 4: 
	    #ifdef GASNET_SHMEM_GET_4
		return (gasnet_valget_handle_t) GASNET_SHMEM_GET_4(src, node);
	    #else
		shmem_getmem((void *) &_gasnete_getval_temp32,src,4,node);
		return (gasnet_valget_handle_t) _gasnete_getval_temp32;
	    #endif

	case 2: 
	    #ifdef GASNET_SHMEM_GET_2
		return (gasnet_valget_handle_t) GASNET_SHMEM_GET_2(src, node);
	    #else
		shmem_getmem((void *) &_gasnete_getval_temp16,src,2,node);
		return (gasnet_valget_handle_t) _gasnete_getval_temp16;
	    #endif
	case 1:
		{
			uint8_t	val;
			printf("%d> ptr is %p shmem_ptr says %p, i say %p\n", gasnete_mynode, src, shmem_ptr(src,node), GASNETE_SHMPTR(src,node));
			val = *((uint8_t *) shmem_ptr(src,node));
			return (gasnet_valget_handle_t) val;
		}
#if 0
		shmem_getmem((void *) &_gasnete_getval_temp8,src,1,node);
		return (gasnet_valget_handle_t) _gasnete_getval_temp8;
#endif

	case 0: return 0;
	default:
	    #if GASNET_DEBUG
	      if (nbytes > sizeof(gasnet_register_value_t))
		      gasneti_fatalerror(
			"VIOLATION: Unsupported size %d in valget", nbytes);
	    #endif
	    shmem_getmem((void *) &_gasnete_getval_tempA, src, nbytes, node);
	    return (gasnet_valget_handle_t) _gasnete_getval_tempA;
    }
}

/* 
 * Since shmem only has blocking valgets, we use the value as the handle and
 * the resulting value.
 */
extern gasnet_register_value_t 
gasnete_wait_syncnb_valget(gasnet_valget_handle_t handle) 
{
    return (gasnet_register_value_t) handle;
}

/*
  Non-Blocking and Blocking Value Put 
  ====================================
*/
GASNET_INLINE_MODIFIER(gasnet_put_val_inner)
void 
gasnete_put_val_inner(gasnet_node_t node, void *dest, 
		      gasnet_register_value_t value, 
		      size_t nbytes GASNETE_THREAD_FARG)
{
    static char	val_put[8];

    switch (nbytes) {
    #ifdef GASNET_SHMEM_PUT_8
	case 8:	GASNET_SHMEM_PUT_8(dest, value, node); return;
    #endif
    #ifdef GASNET_SHMEM_PUT_4
	case 4: GASNET_SHMEM_PUT_4(dest, value, node); return;
    #endif
    #ifdef GASNET_SHMEM_PUT_2
	case 2: GASNET_SHMEM_PUT_2(dest, value, node); return;
    #endif
	case 0: return;
	default:
	    #if GASNET_DEBUG
	      if (nbytes > sizeof(gasnet_register_value_t))
		      gasneti_fatalerror(
			"VIOLATION: Unsupported size %d in valput", nbytes);
	    #endif
	    memcpy(val_put, &value, nbytes);
	    shmem_putmem(dest, val_put, nbytes, node);
	    return;
    }
}

extern void 
gasnete_put_val(gasnet_node_t node, void *dest, gasnet_register_value_t value, 
		size_t nbytes GASNETE_THREAD_FARG)
{
    gasnete_put_val_inner(node, dest, value, nbytes GASNETE_THREAD_PASS);
    shmem_quiet();
}

extern gasnet_handle_t 
gasnete_put_nb_val(gasnet_node_t node, void *dest, gasnet_register_value_t value, 
		    size_t nbytes GASNETE_THREAD_FARG)
{
    gasnete_put_val_inner(node, dest, value, nbytes GASNETE_THREAD_PASS);
    gasnete_handles[gasnete_handleno_phase] = GASNETE_HANDLE_NB_QUIET;
    return &gasnete_handles[gasnete_handleno_phase];
}

extern void 
gasnete_put_nbi_val(gasnet_node_t node, void *dest, 
		    gasnet_register_value_t value, 
		    size_t nbytes GASNETE_THREAD_FARG)
{
    gasnete_put_val_inner(node, dest, value, nbytes GASNETE_THREAD_PASS);
    gasnete_nbi_sync = 1;
    return;
}

/* ------------------------------------------------------------------------------------ */
/*
  Barriers:
  =========
  Barrier adapted to shmem from elan-conduit
*/

#if GASNETI_STATS_OR_TRACE
  static gasneti_stattime_t barrier_notifytime; /* for statistical purposes */ 
#endif
static 
enum { OUTSIDE_BARRIER, INSIDE_BARRIER } 
barrier_splitstate = OUTSIDE_BARRIER;

#ifdef GASNETE_CRAYX1_BARRIER

/*
 * This barrier is optimized for nc-NUMA on the X1. In the notify phase, all
 * processors send a fetchinc to processor 0
 */
typedef 
struct {
    long volatile barrier_value;
    long volatile barrier_flags;
} 
gasnete_barrier_state_t;

static long			    gasnete_barrier_pSync[_SHMEM_BCAST_SYNC_SIZE];
static gasnete_barrier_state_t	    barrier_state = { 0, 0 };
static int volatile		    barrier_blocking = 0;
static long volatile		    barrier_notify_ctr[2] = { 0, 0 };
static int			    barrier_phase = 0;

void
gasnete_barrier_init()
{
    int i;

    for (i=0; i < _SHMEM_BCAST_SYNC_SIZE; i++)
	gasnete_barrier_pSync[i] = _SHMEM_SYNC_VALUE;
}

extern void
gasnete_barrier_notify(int id, int flags)
{
    if_pf (barrier_splitstate == INSIDE_BARRIER)
	gasneti_fatalerror("gasnet_barrier_notify() called twice in a row");

    GASNETI_TRACE_PRINTF(B, ("BARRIER_NOTIFY(id=%i,flags=%i)", id, flags));
    #if GASNETI_STATS_OR_TRACE
      barrier_notifytime = GASNETI_STATTIME_NOW_IFENABLED(B);
    #endif

    barrier_state.barrier_value = id;
    barrier_state.barrier_flags = flags;
    barrier_phase = !barrier_phase;

    if (gasnete_nodes > 1) {

	if (flags & GASNET_BARRIERFLAG_ANONYMOUS) {
	    long volatile *rphase = (long volatile *) 
		GASNETE_SHMPTR(&barrier_notify_ctr[barrier_phase], 0);

	    /* Make sure everyone sees the mismatch if such is the case */
	    if_pf (flags == GASNET_BARRIERFLAG_MISMATCH) {
		int i;
		#pragma _CRI ivdep
		for (i = 0; i < gasnete_nodes; i++) {
		    long volatile *rflags = (long volatile *) 
			    GASNETE_SHMPTR(&barrier_state.barrier_flags, i);
		    *rflags = barrier_state.barrier_flags;
		}
		shmem_quiet();	/* XXX gsync??? */
	    }
	    _amo_afadd(rphase, 1);
	}
	else {
	    
	    #if 0
	    if (gasnete_mynode == 0) {
		#pragma _CRI ivdep
		for (i = 1; i < gasnete_nodes; i++) {
		    long *val = (long *) GASNETE_SHMPTR(&barrier_state, i);
		    long *flags = val+1;

		    val   = barrier_state.value;
		    flags = barrier_state.flags;
		}
	    }
	    #endif

	    /* Have zero broadcast its value to all other processes */
	    #ifdef GASNETI_PTR32
	    shmem_broadcast32
	    #else
	    shmem_broadcast64
	    #endif
			    ((void *) &barrier_state, (void *) &barrier_state,
			    2, 0, 0, 0, gasnete_nodes, gasnete_barrier_pSync);

	    /* Check for possible mismatch */
	    if_pf (flags != barrier_state.barrier_flags 
		   || (!(flags & GASNET_BARRIERFLAG_ANONYMOUS) && 
		          barrier_state.barrier_value != id) 
		   || flags == GASNET_BARRIERFLAG_MISMATCH) {
		    int i;
		    #pragma _CRI ivdep
		    for (i = 0; i < gasnete_nodes; i++) {
			long volatile *flags = (long volatile *) 
			    GASNETE_SHMPTR(&barrier_state.barrier_flags, i);
			*flags = barrier_state.barrier_flags;
		    }
		    shmem_quiet();
	    }
	}
    }

    barrier_splitstate = INSIDE_BARRIER;
    gasneti_memsync();
}

extern int
gasnete_barrier_wait(int id, int flags)
{
  #if GASNETI_STATS_OR_TRACE
    gasneti_stattime_t wait_start = GASNETI_STATTIME_NOW_IFENABLED(B);
  #endif

    if_pf(barrier_splitstate == OUTSIDE_BARRIER) 
	gasneti_fatalerror(
	    "gasnet_barrier_wait() called without a matching notify");

    GASNETI_TRACE_EVENT_TIME(B,BARRIER_NOTIFYWAIT,
			       GASNETI_STATTIME_NOW()-barrier_notifytime);

    GASNETI_TRACE_EVENT_TIME(B,BARRIER_WAIT,0);

    barrier_splitstate = OUTSIDE_BARRIER;
    gasneti_memsync();

    if (flags & GASNET_BARRIERFLAG_ANONYMOUS) {
	    long volatile *ctr = &barrier_notify_ctr[barrier_phase];

	    if (gasnete_mynode == 0) {
		int i;

		while (*ctr != (long volatile) gasnete_nodes)
		    gasnetc_AMPoll();

		*ctr = 0;

		#pragma _CRI ivdep
		for (i = 1; i < gasnete_nodes; i++) {
			/*
		    shmem_long_p((long *) &barrier_notify_ctr[barrier_phase], 1, i);
			*/
		    long volatile *rctr = 
			GASNETE_SHMPTR(&barrier_notify_ctr[barrier_phase], i);
		    *rctr = 1;
		}
		//_gsync(0);
		shmem_quiet();
	    }
	    else {
		while (!*ctr)
		    gasnetc_AMPoll();
		*ctr = 0;
	    }

	    if_pf (barrier_state.barrier_flags == GASNET_ERR_BARRIER_MISMATCH)
		return GASNET_ERR_BARRIER_MISMATCH;
	    else {
		return GASNET_OK;
	    }
    }
    else {
	if_pf(barrier_state.barrier_flags == GASNET_ERR_BARRIER_MISMATCH 
		|| flags != barrier_state.barrier_flags 
		|| (!(flags & GASNET_BARRIERFLAG_ANONYMOUS) 
	    && id != barrier_state.barrier_value)) 
		return GASNET_ERR_BARRIER_MISMATCH;
	else {
	    gasnetc_AMPoll();
	    return GASNET_OK;
	}
    }
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

static void gasnete_barrier_init()
{
    return;
}

static void gasnete_barrier_notify_reqh(gasnet_token_t token, 
  gasnet_handlerarg_t phase, gasnet_handlerarg_t value, gasnet_handlerarg_t flags) {
  gasneti_assert(gasnete_mynode == GASNETE_BARRIER_MASTER);

  gasnet_hsl_lock(&barrier_lock);
  { int count = barrier_count[phase];
    if (flags == 0 && !barrier_consensus_value_present[phase]) {
      barrier_consensus_value[phase] = (int)value;
      barrier_consensus_value_present[phase] = 1;
    } else if (flags == GASNET_BARRIERFLAG_MISMATCH ||
               (flags == 0 && barrier_consensus_value[phase] != (int)value)) {
      barrier_consensus_mismatch[phase] = 1;
    }
    count++;
    if (count == gasnete_nodes) gasneti_memsync(); /* about to signal, ensure we flush state */
    barrier_count[phase] = count;
  }
  gasnet_hsl_unlock(&barrier_lock);
}

static void gasnete_barrier_done_reqh(gasnet_token_t token, 
  gasnet_handlerarg_t phase,  gasnet_handlerarg_t mismatch) {
  gasneti_assert(phase == barrier_phase);

  barrier_response_mismatch[phase] = mismatch;
  gasneti_memsync();
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
  #if GASNETI_STATS_OR_TRACE
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
  gasneti_memsync(); /* ensure all state changes committed before return */
}


extern int gasnete_barrier_wait(int id, int flags) {
  #if GASNETI_STATS_OR_TRACE
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
  gasneti_memsync(); /* ensure all state changes committed before return */
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

/* ------------------------------------------------------------------------ */
GASNET_INLINE_MODIFIER(gasnete_memset_reqh_inner)
void 
gasnete_memset_reqh_inner(gasnet_token_t token, gasnet_handlerarg_t val, 
			  gasnet_handlerarg_t nbytes, void *dest, void *op) 
{
    memset(dest, (int)(uint32_t)val, nbytes);
    gasneti_memsync();

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
/* ------------------------------------------------------------------------ */
/*
  Handlers:
  =========
*/
static gasnet_handlerentry_t const 
gasnete_handlers[] = {
  /* ptr-width independent handlers */
#ifndef GASNETE_CRAYX1_BARRIER
    gasneti_handler_tableentry_no_bits(gasnete_barrier_notify_reqh),
    gasneti_handler_tableentry_no_bits(gasnete_barrier_done_reqh),
#endif

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

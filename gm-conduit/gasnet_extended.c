/*  $Archive:: /Ti/GASNet/gm-conduit/gasnet_extended.c                  $
 *     $Date: 2004/05/02 08:05:15 $
 * $Revision: 1.25 $
 * Description: GASNet Extended API GM Implementation
 * Copyright 2002, Dan Bonachea <bonachea@cs.berkeley.edu>
 * Terms of use are as specified in license.txt
 */

#include <gasnet.h>
#include <gasnet_extended_internal.h>
#include <gasnet_internal.h>
#include <gasnet_handler.h>

GASNETI_IDENT(gasnete_IdentString_Version, "$GASNetExtendedLibraryVersion: " GASNET_EXTENDED_VERSION_STR " $");
GASNETI_IDENT(gasnete_IdentString_ExtendedName, "$GASNetExtendedLibraryName: " GASNET_EXTENDED_NAME_STR " $");

gasnet_node_t	gasnete_mynode = (gasnet_node_t)-1;
gasnet_node_t	gasnete_nodes = 0;

gasnet_seginfo_t	*gasnete_seginfo = NULL;
gasnete_threaddata_t	*gasnete_threadtable[256] = { 0 };
int 			 gasnete_numthreads = 0;
gasnet_hsl_t		 threadtable_lock = GASNET_HSL_INITIALIZER;
#if GASNETI_CLIENT_THREADS
	/*  pthread thread-specific ptr to our threaddata (or NULL for a thread
	 *  never-seen before) */
	pthread_key_t gasnete_threaddata; 
#endif
const gasnete_eopaddr_t	EOPADDR_NIL = { 0xFF, 0xFF };

/* ------------------------------------------------------------------------------------ */
/*
  Tuning Parameters
  =================
  Conduits may choose to override the default tuning parameters below by
  defining them in their gasnet_core_fwd.h
*/

/* the size threshold where gets/puts stop using medium messages and start
 * using longs */
#ifndef GASNETE_GETPUT_MEDIUM_LONG_THRESHOLD
#define GASNETE_GETPUT_MEDIUM_LONG_THRESHOLD   gasnet_AMMaxMedium()
#endif

/* true if we should try to use Long replies in gets (only possible if dest
 * falls in segment) */
#ifndef GASNETE_USE_LONG_GETS
#define GASNETE_USE_LONG_GETS 1
#endif

/* ------------------------------------------------------------------------------------ */
/*
  Thread Management
  =================
*/
static gasnete_threaddata_t * 
gasnete_new_threaddata() 
{
	gasnete_threaddata_t *threaddata = NULL;
	int idx;

	gasnet_hsl_lock(&threadtable_lock);
		idx = gasnete_numthreads;
		gasnete_numthreads++;
	gasnet_hsl_unlock(&threadtable_lock);

	#if GASNETI_CLIENT_THREADS
		if (idx >= 256) 
			gasneti_fatalerror("GASNet Extended API: "
			    "Too many local client threads (limit=256)");
	#else
		gasneti_assert(idx == 0);
	#endif
	gasneti_assert(gasnete_threadtable[idx] == NULL);

	threaddata = (gasnete_threaddata_t *)
	gasneti_calloc(1, sizeof(gasnete_threaddata_t));

	threaddata->threadidx = idx;
	threaddata->eop_free = EOPADDR_NIL;
	threaddata->in_gm_unknown = 0;

	gasnete_threadtable[idx] = threaddata;
	threaddata->current_iop = gasnete_iop_new(threaddata);

	return threaddata;
}
/* PURE function (returns same value for a given thread every time) 
*/
#if GASNETI_CLIENT_THREADS
extern gasnete_threaddata_t *
gasnete_mythread() 
{
	gasnete_threaddata_t *threaddata = pthread_getspecific(gasnete_threaddata);
	GASNETI_TRACE_EVENT(C, DYNAMIC_THREADLOOKUP);
        if_pt (threaddata) {
          gasneti_memcheck(threaddata);
          return threaddata;
        }

	/*	first time we've seen this thread - need to set it up */
	{ 
		int retval;
		gasnete_threaddata_t *threaddata = gasnete_new_threaddata();

		retval = pthread_setspecific(gasnete_threaddata, threaddata);
		gasneti_assert(!retval);
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
  gasneti_check_config_postattach();

  gasneti_assert_always(GASNETE_GETPUT_MEDIUM_LONG_THRESHOLD <= gasnet_AMMaxMedium());
  gasneti_assert_always(gasnete_eopaddr_isnil(EOPADDR_NIL));
}

extern void 
gasnete_init() 
{
	GASNETI_TRACE_PRINTF(C,("gasnete_init()"));
	gasneti_assert(gasnete_nodes == 0); /*make sure we haven't been called before */

	gasnete_check_config(); /* check for sanity */

	#if GASNETI_CLIENT_THREADS
	{/*	TODO: we could provide a non-NULL destructor and reap data
		structures from exiting threads */ 
		int retval = pthread_key_create(&gasnete_threaddata, NULL);
		if (retval) 
			gasneti_fatalerror("In gasnete_init(), "
			    "pthread_key_create()=%s",strerror(retval));
	}
	#endif

	gasnete_mynode = gasnet_mynode();
	gasnete_nodes = gasnet_nodes();
	gasneti_assert(gasnete_nodes >= 1 && gasnete_mynode < gasnete_nodes);
	gasnete_seginfo = (gasnet_seginfo_t*) 
	    gasneti_malloc(sizeof(gasnet_seginfo_t)*gasnete_nodes);
	gasnet_getSegmentInfo(gasnete_seginfo, gasnete_nodes);

	{ 
		gasnete_threaddata_t *threaddata = NULL;
		gasnete_eop_t *eop = NULL;
		#if GASNETI_CLIENT_THREADS
			/* register first thread (optimization) */
			threaddata = gasnete_mythread(); 
		#else
			/* register only thread (required) */
			threaddata = gasnete_new_threaddata();
		#endif

		/* cause the first pool of eops to be allocated optimization */
		eop = gasnete_eop_new(threaddata);
		gasnete_op_markdone((gasnete_op_t *)eop, 0);
		gasnete_op_free((gasnete_op_t *)eop);
	}
 
	/* Initialize barrier resources */
	gasnete_barrier_init();
}
/* ------------------------------------------------------------------------------------ */
/*
  Synchronization for explicit-handle non-blocking operations:
  ===========================================================
*/

extern int  gasnete_try_syncnb(gasnet_handle_t handle) {
	GASNETE_SAFE(gasnet_AMPoll());

	if (gasnete_op_isdone(handle)) {
		gasnete_op_free(handle);
		return GASNET_OK;
	}
	else return GASNET_ERR_NOT_READY;
}

extern int
gasnete_try_syncnb_some (gasnet_handle_t *phandle, size_t numhandles)
{
	int success = 0;
	int empty = 1;

	GASNETE_SAFE(gasnet_AMPoll());
	gasneti_assert(phandle);

	{ 
		int i;
		for (i = 0; i < numhandles; i++) {
			gasnete_op_t *op = phandle[i];
			if (op != GASNET_INVALID_HANDLE) {
				empty = 0;
				if (gasnete_op_isdone(op)) {
					gasnete_op_free(op);
					phandle[i] = GASNET_INVALID_HANDLE;
					success = 1;
				}
			}
		}
	}
	if (success || empty) 
		return GASNET_OK;
	else 
		return GASNET_ERR_NOT_READY;
}

extern int  
gasnete_try_syncnb_all (gasnet_handle_t *phandle, size_t numhandles)
{
	int success = 1;
	GASNETE_SAFE(gasnet_AMPoll());

	gasneti_assert(phandle);

	{ 
		int i;
		for (i = 0; i < numhandles; i++) {
			gasnete_op_t *op = phandle[i];
			if (op != GASNET_INVALID_HANDLE) {
				if (gasnete_op_isdone(op)) {
					gasnete_op_free(op);
					phandle[i] = GASNET_INVALID_HANDLE;
				} 
				else 
					success = 0;
			}
		}
	}
	if (success) 
		return GASNET_OK;
	else 
		return GASNET_ERR_NOT_READY;
}

/* ------------------------------------------------------------------------------------ */
/*
  Synchronization for implicit-handle non-blocking operations:
  ===========================================================
*/

extern int  gasnete_try_syncnbi_gets(GASNETE_THREAD_FARG_ALONE) {
  #if 0
    /* polling for syncnbi now happens in header file to avoid duplication */
    GASNETE_SAFE(gasnet_AMPoll());
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

    if (gasneti_atomic_read(&(iop->completed_get_cnt)) == iop->initiated_get_cnt) {
      if_pf (iop->initiated_get_cnt > 65000) { /* make sure we don't overflow the counters */
        gasneti_atomic_set(&(iop->completed_get_cnt), 0);
        iop->initiated_get_cnt = 0;
      }
      return GASNET_OK;
    } else return GASNET_ERR_NOT_READY;
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
    gasneti_assert(iop->threadidx == mythread->threadidx);
    gasneti_assert(iop->next == NULL);
    gasneti_assert(OPTYPE(iop) == OPTYPE_IMPLICIT);
    #if GASNET_DEBUG
      if (iop->next != NULL)
        gasneti_fatalerror("VIOLATION: attempted to call gasnete_try_syncnbi_puts() inside an NBI access region");
    #endif


    if (gasneti_atomic_read(&(iop->completed_put_cnt)) == iop->initiated_put_cnt) {
      if_pf (iop->initiated_put_cnt > 65000) { /* make sure we don't overflow the counters */
        gasneti_atomic_set(&(iop->completed_put_cnt), 0);
        iop->initiated_put_cnt = 0;
      }
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

extern void
gasnete_barrier_notify(int id, int flags) 
{
	gasnete_extref_barrier_notify(id,flags);
	return;
}
extern int 
gasnete_barrier_wait(int id, int flags)
{
	return gasnete_extref_barrier_wait(id,flags);
}
extern int 
gasnete_barrier_try(int id, int flags)
{
	return gasnete_extref_barrier_try(id,flags);
}

extern gasnet_handle_t 
gasnete_memset_nb(gasnet_node_t node, void *dest, int val, 
		  size_t nbytes   GASNETE_THREAD_FARG) {
	return gasnete_extref_memset_nb(node, dest, val, 
	    nbytes GASNETE_THREAD_PASS);
}

extern void
gasnete_memset_nbi(gasnet_node_t node, void *dest, int val, 
		  size_t nbytes   GASNETE_THREAD_FARG) {
	gasnete_extref_memset_nbi(node, dest, val, 
	    nbytes GASNETE_THREAD_PASS);
	return;
}

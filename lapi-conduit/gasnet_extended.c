/*  $Archive:: /Ti/GASNet/lapi-conduit/gasnet_extended.c                  $
 *     $Date: 2004/01/05 05:01:16 $
 * $Revision: 1.16 $
 * Description: GASNet Extended API Reference Implementation
 * Copyright 2002, Dan Bonachea <bonachea@cs.berkeley.edu>
 * Terms of use are as specified in license.txt
 */

#include <gasnet.h>
#include <gasnet_extended_internal.h>
#include <gasnet_internal.h>
#include <gasnet_handler.h>

GASNETI_IDENT(gasnete_IdentString_Version, "$GASNetExtendedLibraryVersion: " GASNET_EXTENDED_VERSION_STR " $");
GASNETI_IDENT(gasnete_IdentString_ExtendedName, "$GASNetExtendedLibraryName: " GASNET_EXTENDED_NAME_STR " $");

gasnet_node_t gasnete_mynode = (gasnet_node_t)-1;
gasnet_node_t gasnete_nodes = 0;
gasnet_seginfo_t *gasnete_seginfo = NULL;
static gasnete_threaddata_t *gasnete_threadtable[256] = { 0 };
static int gasnete_numthreads = 0;
static gasnet_hsl_t threadtable_lock = GASNET_HSL_INITIALIZER;
#if GASNETI_CLIENT_THREADS
static pthread_key_t gasnete_threaddata; /*  pthread thread-specific ptr to our threaddata (or NULL for a thread never-seen before) */
#endif
static const gasnete_eopaddr_t EOPADDR_NIL = { 0xFF, 0xFF };
extern void _gasnete_iop_check(gasnete_iop_t *iop) { gasnete_iop_check(iop); }

/* ====================================================================
 * LAPI Structures and constants
 * ====================================================================
 */
void** gasnete_remote_memset_hh;
void** gasnete_remote_barrier_hh;


/* ------------------------------------------------------------------------------------ */
/*
  Tuning Parameters
  =================
  Conduits may choose to override the default tuning parameters below by defining them
  in their gasnet_core_fwd.h
*/

/* ------------------------------------------------------------------------------------ */
/*
  Thread Management
  =================
*/
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

    return threaddata;
}
/* PURE function (returns same value for a given thread every time) 
 */
#if GASNETI_CLIENT_THREADS
extern gasnete_threaddata_t *gasnete_mythread() {
    gasnete_threaddata_t *threaddata = pthread_getspecific(gasnete_threaddata);
    GASNETI_TRACE_EVENT(C, DYNAMIC_THREADLOOKUP);
    if_pt (threaddata) {
      gasneti_memcheck(threaddata);
      return threaddata;
    }

    /*  first time we've seen this thread - need to set it up */
    { int retval;
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
    gasneti_assert(sizeof(int8_t) == 1);
    gasneti_assert(sizeof(uint8_t) == 1);
#if !defined(CRAYT3E)
    gasneti_assert(sizeof(int16_t) == 2);
    gasneti_assert(sizeof(uint16_t) == 2);
#endif
    gasneti_assert(sizeof(int32_t) == 4);
    gasneti_assert(sizeof(uint32_t) == 4);
    gasneti_assert(sizeof(int64_t) == 8);
    gasneti_assert(sizeof(uint64_t) == 8);

    gasneti_assert(sizeof(uintptr_t) >= sizeof(void *));

    /* check GASNET_PAGESIZE is a power of 2 and > 0 */
    gasneti_assert(GASNET_PAGESIZE > 0 && 
           (GASNET_PAGESIZE & (GASNET_PAGESIZE - 1)) == 0);

    gasneti_assert(SIZEOF_GASNET_REGISTER_VALUE_T == sizeof(gasnet_register_value_t));
    gasneti_assert(SIZEOF_GASNET_REGISTER_VALUE_T >= sizeof(int));
    gasneti_assert(SIZEOF_GASNET_REGISTER_VALUE_T >= sizeof(void *));

#if    defined(GASNETI_PTR32) && !defined(GASNETI_PTR64)
    gasneti_assert(sizeof(void*) == 4);
#elif !defined(GASNETI_PTR32) &&  defined(GASNETI_PTR64)
    gasneti_assert(sizeof(void*) == 8);
#else
#error must #define exactly one of GASNETI_PTR32 or GASNETI_PTR64
#endif

    gasneti_assert(gasnete_eopaddr_isnil(EOPADDR_NIL));

    /*  verify sanity of the core interface */
    gasneti_assert(gasnet_AMMaxArgs() >= 2*MAX(sizeof(int),sizeof(void*)));      
    gasneti_assert(gasnet_AMMaxMedium() >= 512);
    gasneti_assert(gasnet_AMMaxLongRequest() >= 512);
    gasneti_assert(gasnet_AMMaxLongReply() >= 512);
}

extern void gasnete_init() {
    GASNETI_TRACE_PRINTF(C,("gasnete_init()"));
    gasneti_assert(gasnete_nodes == 0); /*  make sure we haven't been called before */

    gasnete_check_config(); /*  check for sanity */

#if GASNETI_CLIENT_THREADS
    {/*  TODO: we could provide a non-NULL destructor and reap data structures from exiting threads */
	int retval = pthread_key_create(&gasnete_threaddata, NULL);
	if (retval) gasneti_fatalerror("In gasnete_init(), pthread_key_create()=%s",strerror(retval));
    }
#endif

    gasnete_mynode = gasnet_mynode();
    gasnete_nodes = gasnet_nodes();
    gasneti_assert(gasnete_nodes >= 1 && gasnete_mynode < gasnete_nodes);
    gasnete_seginfo = (gasnet_seginfo_t*)gasneti_malloc(sizeof(gasnet_seginfo_t)*gasnete_nodes);
    gasnet_getSegmentInfo(gasnete_seginfo, gasnete_nodes);

    /* Exchange LAPI addresses here */
    gasnete_remote_memset_hh = (void**)gasneti_malloc(gasnete_nodes*sizeof(void*));
    GASNETC_LCHECK(LAPI_Address_init(gasnetc_lapi_context,
				     (void*)&gasnete_lapi_memset_hh,
				     gasnete_remote_memset_hh));

    gasnete_remote_barrier_hh = (void**)gasneti_malloc(gasnete_nodes*sizeof(void*));
    GASNETC_LCHECK(LAPI_Address_init(gasnetc_lapi_context,
				     (void*)&gasnete_lapi_barrier_hh,
				     gasnete_remote_barrier_hh));



    {
	gasnete_threaddata_t *threaddata = NULL;
	gasnete_eop_t *eop = NULL;
#if GASNETI_CLIENT_THREADS
	/* register first thread (optimization) */
	threaddata = gasnete_mythread(); 
#else
	/* register only thread (required) */
	threaddata = gasnete_new_threaddata();
	gasnete_threadtable[0] = threaddata;
#endif

	/* cause the first pool of eops to be allocated (optimization) */
	eop = gasnete_eop_new(threaddata);
	gasnete_op_markdone((gasnete_op_t *)eop, 0);
	gasnete_op_free((gasnete_op_t *)eop);
    }
}

/* ------------------------------------------------------------------------------------ */
/*
  Op management
  =============
*/
/*  get a new op and mark it in flight */
gasnete_eop_t *gasnete_eop_new(gasnete_threaddata_t * const thread) {
    gasnete_eopaddr_t head = thread->eop_free;
    if_pt (!gasnete_eopaddr_isnil(head)) {
	gasnete_eop_t *eop = GASNETE_EOPADDR_TO_PTR(thread, head);
	thread->eop_free = eop->addr;
	eop->addr = head;
	gasneti_assert(!gasnete_eopaddr_equal(thread->eop_free,head));
	gasneti_assert(eop->threadidx == thread->threadidx);
	gasneti_assert(OPTYPE(eop) == OPTYPE_EXPLICIT);
	gasneti_assert(OPSTATE(eop) == OPSTATE_FREE);
	SET_OPSTATE(eop, OPSTATE_INFLIGHT);
	GASNETC_LCHECK(LAPI_Setcntr(gasnetc_lapi_context,&eop->cntr,0));
	eop->initiated_cnt = 0;
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

	return gasnete_eop_new(thread); /*  should succeed this time */
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
	iop = (gasnete_iop_t *)gasneti_malloc(sizeof(gasnete_iop_t));
	SET_OPTYPE((gasnete_op_t *)iop, OPTYPE_IMPLICIT);
	iop->threadidx = thread->threadidx;
    }
	iop->next = NULL;
	iop->initiated_get_cnt = 0;
	iop->initiated_put_cnt = 0;
	GASNETC_LCHECK(LAPI_Setcntr(gasnetc_lapi_context,&iop->get_cntr,0));
	GASNETC_LCHECK(LAPI_Setcntr(gasnetc_lapi_context,&iop->put_cntr,0));
	return iop;
}

/*  query an op for completeness - for iop this means both puts and gets */
int gasnete_op_isdone(gasnete_op_t *op) {
    int cnt = 0;
    gasneti_assert(op->threadidx == gasnete_mythread()->threadidx);
    if_pt (OPTYPE(op) == OPTYPE_EXPLICIT) {
	gasnete_eop_t *eop = (gasnete_eop_t*)op;
	gasneti_assert(OPSTATE(op) != OPSTATE_FREE);
        gasnete_eop_check(eop);
	if (eop->initiated_cnt > 0) {
	    GASNETC_LCHECK(LAPI_Getcntr(gasnetc_lapi_context,&eop->cntr,&cnt));
	    gasneti_assert(cnt <= eop->initiated_cnt);
	}
	return (eop->initiated_cnt == cnt);
    } else {
	/* only call getcntr if we need to */
	gasnete_iop_t *iop = (gasnete_iop_t*)op;
        gasnete_iop_check(iop);
	if (iop->initiated_get_cnt > 0) {
	    GASNETC_LCHECK(LAPI_Getcntr(gasnetc_lapi_context,&iop->get_cntr,&cnt));
	    gasneti_assert(cnt <= iop->initiated_get_cnt);
	}
	if (iop->initiated_get_cnt != cnt)
	    return 0;
	cnt = 0;
	if (iop->initiated_put_cnt > 0) {
	    GASNETC_LCHECK(LAPI_Getcntr(gasnetc_lapi_context,&iop->put_cntr,&cnt));
	    gasneti_assert(cnt <= iop->initiated_put_cnt);
	}
	return (iop->initiated_put_cnt == cnt);
    }
}

/* mark an op done
 * Not called by handlers in LAPI version, just here for completeness
 */
void gasnete_op_markdone(gasnete_op_t *op, int isget) {
    if (OPTYPE(op) == OPTYPE_EXPLICIT) {
	gasnete_eop_t *eop = (gasnete_eop_t *)op;
	int cnt = 0;
	gasneti_assert(OPSTATE(eop) == OPSTATE_INFLIGHT);
        gasnete_eop_check(eop);
	if (eop->initiated_cnt > 0) {
	    GASNETC_LCHECK(LAPI_Getcntr(gasnetc_lapi_context,&eop->cntr,&cnt));
	    gasneti_assert(eop->initiated_cnt == cnt);
	}
	SET_OPSTATE(eop, OPSTATE_COMPLETE);
    } else {
	/* gasnete_iop_t *iop = (gasnete_iop_t *)op; */
        gasneti_fatalerror("this should not happen");
    }
}

/*  free an op */
void gasnete_op_free(gasnete_op_t *op) {
    gasnete_threaddata_t * const thread = gasnete_threadtable[op->threadidx];
    gasneti_assert(thread == gasnete_mythread());
    if (OPTYPE(op) == OPTYPE_EXPLICIT) {
	gasnete_eop_t *eop = (gasnete_eop_t *)op;
	gasnete_eopaddr_t addr = eop->addr;
        /* DOB: OPSTATE_COMPLETE not currently used by lapi-conduit
          gasneti_assert(OPSTATE(eop) == OPSTATE_COMPLETE);*/
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

/* --------------------------------------------------------------------------
 * This is the LAPI Header Handler that executes a gasnet_memset operation
 * on the remote node.
 * --------------------------------------------------------------------------
 */
void* gasnete_lapi_memset_hh(lapi_handle_t *context, void *uhdr, uint *uhdr_len,
			     ulong *msg_len, compl_hndlr_t **comp_h, void **uinfo)
{
    gasnete_memset_uhdr_t *u = (gasnete_memset_uhdr_t*)uhdr;

    memset((void*)(u->destLoc),u->value,u->nbytes);

    *comp_h = NULL;
    *uinfo = NULL;
    return NULL;
}

/* ------------------------------------------------------------------------------------ */
/*
  Blocking memory-to-memory transfers
  ===================================
*/
/* ------------------------------------------------------------------------------------ */
extern void gasnete_get_bulk (void *dest, gasnet_node_t node, void *src,
			      size_t nbytes GASNETE_THREAD_FARG)
{
    lapi_cntr_t c_cntr;
    int num_get = 0;
    int cur_cntr;

    GASNETC_LCHECK(LAPI_Setcntr(gasnetc_lapi_context, &c_cntr, 0));

    /* Issue as many gets as required.
     * Will generally only be one */
    while (nbytes > 0) {
	size_t to_get = MIN(nbytes, gasnetc_max_lapi_data_size);
	GASNETC_LCHECK(LAPI_Get(gasnetc_lapi_context, (unsigned int)node, to_get,
				src,dest,NULL,&c_cntr));
	dest = (void*)((char*)dest + to_get);
	src = (void*)((char*)src + to_get);
	num_get++;
	nbytes -= to_get;
    }
    /* block until all gets complete */
    GASNETC_LCHECK(LAPI_Waitcntr(gasnetc_lapi_context,&c_cntr,num_get,&cur_cntr));
    gasneti_assert(cur_cntr == 0);
}

/* ------------------------------------------------------------------------------------ */
extern void gasnete_put_bulk (gasnet_node_t node, void *dest, void *src,
			      size_t nbytes GASNETE_THREAD_FARG)
{
    lapi_cntr_t  c_cntr;
    int num_put = 0;
    int cur_cntr;

    GASNETC_LCHECK(LAPI_Setcntr(gasnetc_lapi_context, &c_cntr, 0));

    /* Issue as many puts as required.
     * Will generally only be one */
    while (nbytes > 0) {
	size_t to_put = MIN(nbytes, gasnetc_max_lapi_data_size);
	/* use op lapi counter as completion counter,
	 * and o_cntr as origin counter */
	GASNETC_LCHECK(LAPI_Put(gasnetc_lapi_context, (unsigned int) node, to_put,
				dest,src,NULL,NULL,&c_cntr));
	dest = (void*)((char*)dest + to_put);
	src = (void*)((char*)src + to_put);
	num_put++;
	nbytes -= to_put;
    }

    /* block until all complete */
    GASNETC_LCHECK(LAPI_Waitcntr(gasnetc_lapi_context,&c_cntr,num_put,&cur_cntr));
    gasneti_assert(cur_cntr == 0);
}

/* ------------------------------------------------------------------------------------ */
extern void gasnete_memset(gasnet_node_t node, void *dest, int val,
			   size_t nbytes GASNETE_THREAD_FARG)
{
    lapi_cntr_t c_cntr;
    int cur_cntr = 0;
    gasnete_memset_uhdr_t uhdr;

    /* We will use a LAPI active message and have the remote header handler
     * perform the memset operation.  More efficient than GASNET AMs because
     * we do not have to schedule a completion handler to issue a reply
     */
    uhdr.destLoc = (uintptr_t)dest;
    uhdr.value = val;
    uhdr.nbytes = nbytes;
	
    GASNETC_LCHECK(LAPI_Setcntr(gasnetc_lapi_context, &c_cntr, 0));
    GASNETC_LCHECK(LAPI_Amsend(gasnetc_lapi_context, (unsigned int)node,
			       gasnete_remote_memset_hh[node],
			       &uhdr, sizeof(gasnete_memset_uhdr_t), NULL, 0,
			       NULL, NULL, &c_cntr));
   

    /* block until complete */
    GASNETC_LCHECK(LAPI_Waitcntr(gasnetc_lapi_context,&c_cntr,1,&cur_cntr));
    gasneti_assert(cur_cntr == 0);
}


/* ------------------------------------------------------------------------------------ */
/*
  Non-blocking memory-to-memory transfers (explicit handle)
  ==========================================================
*/
/* ------------------------------------------------------------------------------------ */
extern gasnet_handle_t gasnete_get_nb_bulk (void *dest, gasnet_node_t node, void *src,
					    size_t nbytes GASNETE_THREAD_FARG)
{
    gasnete_eop_t *op = gasnete_eop_new(GASNETE_MYTHREAD);

    /* Issue as many gets as required.
     * Will generally only be one */
    while (nbytes > 0) {
	size_t to_get = MIN(nbytes, gasnetc_max_lapi_data_size);
	GASNETC_LCHECK(LAPI_Get(gasnetc_lapi_context, (unsigned int) node, to_get,
				src,dest,NULL,&op->cntr));
	dest = (void*)((char*)dest + to_get);
	src = (void*)((char*)src + to_get);
	op->initiated_cnt++;
	nbytes -= to_get;
    }
    return (gasnet_handle_t)op;
}

/* ------------------------------------------------------------------------------------ */
extern gasnet_handle_t gasnete_put_nb_bulk (gasnet_node_t node, void *dest, void *src,
					    size_t nbytes GASNETE_THREAD_FARG)
{
    gasnete_eop_t *op = gasnete_eop_new(GASNETE_MYTHREAD);

    /* Issue as many puts as required.
     * Will generally only be one */
    while (nbytes > 0) {
	size_t to_put = MIN(nbytes, gasnetc_max_lapi_data_size);
	/* use op lapi counter as completion counter */
	GASNETC_LCHECK(LAPI_Put(gasnetc_lapi_context, (unsigned int)node, to_put,
				dest,src,NULL,NULL,&op->cntr));
	dest = (void*)((char*)dest + to_put);
	src = (void*)((char*)src + to_put);
	op->initiated_cnt++;
	nbytes -= to_put;
    }
    return (gasnet_handle_t)op;
}

/* ------------------------------------------------------------------------------------ */
extern gasnet_handle_t gasnete_put_nb (gasnet_node_t node, void *dest, void *src,
				       size_t nbytes GASNETE_THREAD_FARG)
{
    gasnete_eop_t *op = gasnete_eop_new(GASNETE_MYTHREAD);
    lapi_cntr_t  o_cntr;
    int num_put = 0;
    int cur_cntr;

    GASNETC_LCHECK(LAPI_Setcntr(gasnetc_lapi_context, &o_cntr, 0));

    /* Issue as many puts as required.
     * Will generally only be one */
    while (nbytes > 0) {
	size_t to_put = MIN(nbytes, gasnetc_max_lapi_data_size);
	/* use op lapi counter as completion counter,
	 * and o_cntr as origin counter */
	GASNETC_LCHECK(LAPI_Put(gasnetc_lapi_context, (unsigned int) node, to_put,
				dest,src,NULL,&o_cntr,&op->cntr));
	dest = (void*)((char*)dest + to_put);
	src = (void*)((char*)src + to_put);
	num_put++;
	nbytes -= to_put;
    }
    op->initiated_cnt += num_put;
    /* Client allowed to modify src data after return.  Make sure operation
     * is complete at origin
     */
    GASNETC_LCHECK(LAPI_Waitcntr(gasnetc_lapi_context,&o_cntr,num_put,&cur_cntr));
    gasneti_assert(cur_cntr == 0);
    
    return (gasnet_handle_t)op;
}

/* ------------------------------------------------------------------------------------ */
extern gasnet_handle_t gasnete_memset_nb   (gasnet_node_t node, void *dest, int val,
					    size_t nbytes GASNETE_THREAD_FARG) {
    gasnete_eop_t *op = gasnete_eop_new(GASNETE_MYTHREAD);
    lapi_cntr_t o_cntr;
    int cur_cntr = 0;
    gasnete_memset_uhdr_t uhdr;

    /* We will use a LAPI active message and have the remote header handler
     * perform the memset operation.  More efficient than GASNET AMs because
     * we do not have to schedule a completion handler to issue a reply
     */
    uhdr.destLoc = (uintptr_t)dest;
    uhdr.value = val;
    uhdr.nbytes = nbytes;
	
    GASNETC_LCHECK(LAPI_Setcntr(gasnetc_lapi_context, &o_cntr, 0));
    GASNETC_LCHECK(LAPI_Amsend(gasnetc_lapi_context, (unsigned int)node,
			       gasnete_remote_memset_hh[node],
			       &uhdr, sizeof(gasnete_memset_uhdr_t), NULL, 0,
			       NULL, &o_cntr, &op->cntr));
   
    op->initiated_cnt++;
    /* must insure operation has completed locally since uhdr is a stack variable.
     * This will ALMOST ALWAYS be true in the case of such a small message */
    GASNETC_LCHECK(LAPI_Waitcntr(gasnetc_lapi_context,&o_cntr,1,&cur_cntr));
    gasneti_assert(cur_cntr == 0);

    return (gasnet_handle_t)op;
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

extern int  gasnete_try_syncnb_some (gasnet_handle_t *phandle, size_t numhandles) {
    int success = 0;
    int empty = 1;
    GASNETE_SAFE(gasnet_AMPoll());

    gasneti_assert(phandle);

    { int i;
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

    if (success || empty) return GASNET_OK;
    else return GASNET_ERR_NOT_READY;
}

extern int  gasnete_try_syncnb_all (gasnet_handle_t *phandle, size_t numhandles) {
    int success = 1;
    GASNETE_SAFE(gasnet_AMPoll());

    gasneti_assert(phandle);

    { int i;
    for (i = 0; i < numhandles; i++) {
	gasnete_op_t *op = phandle[i];
	if (op != GASNET_INVALID_HANDLE) {
	    if (gasnete_op_isdone(op)) {
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
*/

extern void gasnete_get_nbi_bulk (void *dest, gasnet_node_t node, void *src,
				  size_t nbytes GASNETE_THREAD_FARG)
{
    gasnete_threaddata_t * const mythread = GASNETE_MYTHREAD;
    gasnete_iop_t *op = mythread->current_iop;

    while (nbytes > 0) {
	size_t to_get = MIN(nbytes, gasnetc_max_lapi_data_size);
	GASNETC_LCHECK(LAPI_Get(gasnetc_lapi_context, (unsigned int)node, to_get,
				src,dest,NULL,&op->get_cntr));
	dest = (void*)((char*)dest + to_get);
	src = (void*)((char*)src + to_get);
	op->initiated_get_cnt++;
	nbytes -= to_get;
    }
}

/* ------------------------------------------------------------------------------------ */
extern void gasnete_put_nbi_bulk (gasnet_node_t node, void *dest, void *src,
				  size_t nbytes GASNETE_THREAD_FARG)
{
    gasnete_threaddata_t * const mythread = GASNETE_MYTHREAD;
    gasnete_iop_t *op = mythread->current_iop;

    /* Issue as many puts as required.
     * Will generally only be one */
    while (nbytes > 0) {
	size_t to_put = MIN(nbytes, gasnetc_max_lapi_data_size);
	/* use op lapi counter as completion counter */
	GASNETC_LCHECK(LAPI_Put(gasnetc_lapi_context, (unsigned int)node, to_put,
				dest,src,NULL,NULL,&op->put_cntr));
	dest = (void*)((char*)dest + to_put);
	src = (void*)((char*)src + to_put);
	op->initiated_put_cnt++;
	nbytes -= to_put;
    }
}

/* ------------------------------------------------------------------------------------ */
extern void gasnete_put_nbi (gasnet_node_t node, void *dest, void *src,
			     size_t nbytes GASNETE_THREAD_FARG)
{
    gasnete_threaddata_t * const mythread = GASNETE_MYTHREAD;
    gasnete_iop_t *op = mythread->current_iop;
    lapi_cntr_t  o_cntr;
    int num_put = 0;
    int cur_cntr;

    GASNETC_LCHECK(LAPI_Setcntr(gasnetc_lapi_context, &o_cntr, 0));

    /* Issue as many puts as required.
     * Will generally only be one */
    while (nbytes > 0) {
	size_t to_put = MIN(nbytes, gasnetc_max_lapi_data_size);
	/* use op lapi counter as completion counter,
	 * and o_cntr as origin counter */
	GASNETC_LCHECK(LAPI_Put(gasnetc_lapi_context, (unsigned int)node, to_put,
				dest,src,NULL,&o_cntr,&op->put_cntr));
	dest = (void*)((char*)dest + to_put);
	src = (void*)((char*)src + to_put);
	num_put++;
	nbytes -= to_put;
    }
    op->initiated_put_cnt += num_put;
    /* Client allowed to modify src data after return.  Make sure operation
     * is complete at origin
     */
    GASNETC_LCHECK(LAPI_Waitcntr(gasnetc_lapi_context,&o_cntr,num_put,&cur_cntr));
    gasneti_assert(cur_cntr == 0);
}

/* ------------------------------------------------------------------------------------ */
extern void gasnete_memset_nbi (gasnet_node_t node, void *dest, int val,
				size_t nbytes GASNETE_THREAD_FARG) {
    gasnete_threaddata_t * const mythread = GASNETE_MYTHREAD;
    gasnete_iop_t *op = mythread->current_iop;
    lapi_cntr_t o_cntr;
    int cur_cntr = 0;
    gasnete_memset_uhdr_t uhdr;

    /* We will use a LAPI active message and have the remote header handler
     * perform the memset operation.  More efficient than GASNET AMs because
     * we do not have to schedule a completion handler to issue a reply
     */
    uhdr.destLoc = (uintptr_t)dest;
    uhdr.value = val;
    uhdr.nbytes = nbytes;
	
    GASNETC_LCHECK(LAPI_Setcntr(gasnetc_lapi_context, &o_cntr, 0));
    GASNETC_LCHECK(LAPI_Amsend(gasnetc_lapi_context, (unsigned int)node,
			       gasnete_remote_memset_hh[node],
			       &uhdr, sizeof(gasnete_memset_uhdr_t), NULL, 0,
			       NULL, &o_cntr, &op->put_cntr));
   
    op->initiated_put_cnt++;
    /* must insure operation has completed locally since uhdr is a stack variable.
     * This will ALMOST ALWAYS be true in the case of such a small message */
    GASNETC_LCHECK(LAPI_Waitcntr(gasnetc_lapi_context,&o_cntr,1,&cur_cntr));
    gasneti_assert(cur_cntr == 0);
}
/* ------------------------------------------------------------------------------------ */
/*
  Synchronization for implicit-handle non-blocking operations:
  ===========================================================
*/

extern int  gasnete_try_syncnbi_gets(GASNETE_THREAD_FARG_ALONE) {
    {
	gasnete_threaddata_t * const mythread = GASNETE_MYTHREAD;
	gasnete_iop_t *iop = mythread->current_iop;
	int cnt = 0;
	gasneti_assert(iop->threadidx == mythread->threadidx);
	gasneti_assert(OPTYPE(iop) == OPTYPE_IMPLICIT);
#if GASNET_DEBUG
	if (iop->next != NULL)
	    gasneti_fatalerror("VIOLATION: attempted to call gasnete_try_syncnbi_gets() inside an NBI access region");
#endif

	if (iop->initiated_get_cnt > 0) {
	    GASNETC_LCHECK(LAPI_Getcntr(gasnetc_lapi_context,&iop->get_cntr,&cnt));
	    gasneti_assert(cnt <= iop->initiated_get_cnt);
	}
        if (iop->initiated_get_cnt == cnt) {
            if (cnt > 65000) { /* make sure we don't overflow the counters */
	      GASNETC_LCHECK(LAPI_Setcntr(gasnetc_lapi_context,&iop->get_cntr,0));
              iop->initiated_get_cnt = 0;
            }
	    return GASNET_OK;
        } else return GASNET_ERR_NOT_READY;
    }
}

extern int  gasnete_try_syncnbi_puts(GASNETE_THREAD_FARG_ALONE) {
    {
	gasnete_threaddata_t * const mythread = GASNETE_MYTHREAD;
	gasnete_iop_t *iop = mythread->current_iop;
	int cnt = 0;
	gasneti_assert(iop->threadidx == mythread->threadidx);
	gasneti_assert(iop->next == NULL);
	gasneti_assert(OPTYPE(iop) == OPTYPE_IMPLICIT);
#if GASNET_DEBUG
	if (iop->next != NULL)
	    gasneti_fatalerror("VIOLATION: attempted to call gasnete_try_syncnbi_puts() inside an NBI access region");
#endif

	if (iop->initiated_put_cnt > 0) {
	    GASNETC_LCHECK(LAPI_Getcntr(gasnetc_lapi_context,&iop->put_cntr,&cnt));
	    gasneti_assert(cnt <= iop->initiated_put_cnt);
	}
        if (iop->initiated_put_cnt == cnt) {
          if (cnt > 65000) { /* make sure we don't overflow the counters */
	      GASNETC_LCHECK(LAPI_Setcntr(gasnetc_lapi_context,&iop->put_cntr,0));
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
extern void  gasnete_begin_nbi_accessregion(int allowrecursion GASNETE_THREAD_FARG) {
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

extern gasnet_valget_handle_t gasnete_get_nb_val(gasnet_node_t node, void *src,
						 size_t nbytes GASNETE_THREAD_FARG)
{
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
/*  TODO: optimize this */
/*  a silly, centralized barrier implementation:
    everybody sends notifies to a single node, where we count them up
    central node eventually notices the barrier is complete (probably
    when it calls wait) and then it broadcasts the completion to all the nodes
    The main problem is the need for the master to call wait before the barrier can
    make progress - we really need a way for the "last thread" to notify all 
    the threads when completion is detected, but AM semantics don't provide a 
    simple way to do this.
    The centralized nature also makes it non-scalable - we really want to use 
    a tree-based barrier or pairwise exchange algorithm for scalability
    (but these impose even greater potential delays due to the lack of attentiveness to
    barrier progress)
*/
/*  LAPI MODS:  Replaced GASNET CORE AM calls with LAPI Amsend calls.
    This should run faster because it eliminates the need to schedule a
    LAPI completion handler to run the AM handler.
    Also, header handler of last client Amsend operations to notify
    schedules a completion handler to inform all clients the barrier
    has been reached.  This eliminates the need for Master node to
    notice the barrier has been reached.
*/

static enum { OUTSIDE_BARRIER, INSIDE_BARRIER } barrier_splitstate = OUTSIDE_BARRIER;
static int volatile barrier_value; /*  local barrier value */
static int volatile barrier_flags; /*  local barrier flags */
static int volatile barrier_phase = 0;  /*  2-phase operation to improve pipelining */
static int volatile barrier_response_done[2] = { 0, 0 }; /*  non-zero when barrier is complete */
static int volatile barrier_response_mismatch[2] = { 0, 0 }; /*  non-zero if we detected a mismatch */
#if GASNETI_STATS_OR_TRACE
static gasneti_stattime_t barrier_notifytime; /* for statistical purposes */ 
#endif

/*  global state on P0 */
#define GASNETE_BARRIER_MASTER (gasnete_nodes-1)
static gasnet_hsl_t barrier_lock = GASNET_HSL_INITIALIZER;
static int volatile barrier_consensus_value[2]; /*  consensus barrier value */
static int volatile barrier_consensus_value_present[2] = { 0, 0 }; /*  consensus barrier value found */
static int volatile barrier_consensus_mismatch[2] = { 0, 0 }; /*  non-zero if we detected a mismatch */
static int volatile barrier_count[2] = { 0, 0 }; /*  count of how many remotes have notified (on P0) */
/* static lapi counters used to eliminate alloc and free of uhdr structure
 * during "broadcast" that barrier has been reached.  Its probably not necessary
 * to have two of these, but to be on the safe side...
 */
static gasnete_barrier_uhdr_t barrier_uhdr[2];

/* LAPI Completion handler scheduled by HH on master node when the last node
 * has performed the notify.
 * Job is to send done message to all other nodes.
 */
void gasnete_lapi_barrier_ch(lapi_handle_t *context, void* user_info)
{
    gasnete_barrier_uhdr_t *uhdr = (gasnete_barrier_uhdr_t*)user_info;
    int i;

    /* Note: uhdr is a pointer to a static structure.  It will not
     * be used again until the next barrier has been reached by all
     * nodes (with the same phase).  At that point, all clients will
     * have had to received the active message.  This implies
     * the these AM calls will have completed locally.  That is,
     * we do not have to protect re-use of this uhdr by waiting
     * on a local counter variable.
     */
    uhdr->is_notify = 0;
    uhdr->src = gasnete_mynode;

    GASNETI_TRACE_PRINTF(B,("BARRIER_CH: phase %d, value %d, flags %d, mismatch %d, g_val %d, g_count %d",
			    uhdr->phase,uhdr->value,uhdr->flags,uhdr->mismatch,
			    barrier_consensus_value[uhdr->phase],barrier_count[uhdr->phase]));

    /* inform all nodes (except local node) that barrier is complete */
    for (i=0; i < gasnete_nodes; i++) {
	if ( i == gasnete_mynode ) continue;
	GASNETC_LCHECK(LAPI_Amsend(*context, (unsigned int)i,
				   gasnete_remote_barrier_hh[i],
				   uhdr, sizeof(gasnete_barrier_uhdr_t), NULL, 0,
				   NULL, NULL, NULL));
    }

}

/* LAPI header handler to implement both notify and done requests on remote node */
void* gasnete_lapi_barrier_hh(lapi_handle_t *context, void *uhdr, uint *uhdr_len,
			      ulong *msg_len, compl_hndlr_t **comp_h, void **uinfo)
{
    gasnete_barrier_uhdr_t *u = (gasnete_barrier_uhdr_t*)uhdr;
    int phase = u->phase;
    int value = u->value;
    int is_done = !u->is_notify;

    GASNETI_TRACE_PRINTF(B,("BARRIER_HH: node %d, src %d, notify %d, phase %d, value %d, flags %d, mismatch %d, g_val %d, g_count %d",
			    gasnete_mynode,u->src,u->is_notify,u->phase,u->value,u->flags,u->mismatch,
			    barrier_consensus_value[u->phase],barrier_count[u->phase]));

    *comp_h = NULL;
    *uinfo = NULL;

    if (u->is_notify) {
	/* this is a notify header handler call */
	gasneti_assert(gasnete_mynode == GASNETE_BARRIER_MASTER);

	/* Do we need a lock here?  Don't think so.  Header handlers are
	 * run by LAPI dispatcher and thus guaranteed to run one-at-a-time.
	 * Varibles read and updated here are only done so in this function.
	 */
	{
	    int count = barrier_count[phase];
	    if (u->flags == 0 && !barrier_consensus_value_present[phase]) {
		barrier_consensus_value[phase] = (int)value;
		barrier_consensus_value_present[phase] = 1;
	    } else if (u->flags == GASNET_BARRIERFLAG_MISMATCH ||
		       (u->flags == 0 && barrier_consensus_value[phase] != (int)value)) {
		barrier_consensus_mismatch[phase] = 1;
	    }
	    barrier_count[phase] = count+1;

	    if (barrier_count[phase] == gasnete_nodes) {
		/* schedule completion handler to notify all nodes that
		 * the barrier has been reached.
		 * Question: can we use a static uhdr?
		 */
		gasnete_barrier_uhdr_t *uch = (gasnete_barrier_uhdr_t*)&barrier_uhdr[phase];
		uch->phase = phase;
		uch->value = value;
		uch->mismatch = barrier_consensus_mismatch[phase];
		uch->is_notify = 0;
		uch->src = gasnete_mynode;
		uch->flags = -1;
		*uinfo = (void*)uch;
		*comp_h = gasnete_lapi_barrier_ch;
		/* update the local state below.  Note that the completion
		 * handler will not send an AM to this node
		 */
		is_done = 1;
		u->mismatch = uch->mismatch;
		GASNETI_TRACE_PRINTF(B,("BARRIER_HH: REACHED %d, mismatch %d, SCHEDULING CH",
					gasnete_nodes,uch->mismatch));
	    }
	}
    };

    if (is_done) {
	/* this is a done header handler call... update local state */
	gasneti_assert(phase == barrier_phase);

	barrier_response_mismatch[phase] = u->mismatch;
        gasneti_memsync(); /* ensure mimatch committed before signal */
	barrier_response_done[phase] = 1;
    }
    return NULL;
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
	gasnete_barrier_uhdr_t uhdr;
	lapi_cntr_t o_cntr;
	int cur_cntr;

	uhdr.is_notify = 1;
	uhdr.phase = phase;
	uhdr.value = barrier_value;
	uhdr.flags = flags;
	uhdr.src = gasnete_mynode;
	uhdr.mismatch = 999;
	GASNETC_LCHECK(LAPI_Setcntr(gasnetc_lapi_context, &o_cntr, 0));
	
	/*  send notify msg to 0 */
	GASNETC_LCHECK(LAPI_Amsend(gasnetc_lapi_context,
				   (unsigned int)GASNETE_BARRIER_MASTER,
				   gasnete_remote_barrier_hh[GASNETE_BARRIER_MASTER],
				   &uhdr, sizeof(gasnete_barrier_uhdr_t), NULL, 0,
				   NULL, &o_cntr, NULL));
	/* wait for local completion */
	GASNETC_LCHECK(LAPI_Waitcntr(gasnetc_lapi_context,&o_cntr,1,&cur_cntr));
	gasneti_assert(cur_cntr == 0);
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
	GASNETE_SAFE(gasnet_AMPoll());
    }

    GASNETI_TRACE_EVENT_TIME(B,BARRIER_WAIT,GASNETI_STATTIME_NOW()-wait_start);

    /* if this is the master node, reset the global state */
    if (gasnete_mynode == GASNETE_BARRIER_MASTER) {
	barrier_count[phase] = 0;
	barrier_consensus_mismatch[phase] = 0;
	barrier_consensus_value_present[phase] = 0;
    }
    
    /*  update local state */
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

    /* should we kick the network if not done? */

    if (barrier_response_done[barrier_phase]) {
	GASNETI_TRACE_EVENT_VAL(B,BARRIER_TRY,1);
	return gasnete_barrier_wait(id, flags);
    }
    else {
	GASNETI_TRACE_EVENT_VAL(B,BARRIER_TRY,0);
	return GASNET_ERR_NOT_READY;
    }
}
/* ------------------------------------------------------------------------------------ */
/*
  Handlers:
  =========
*/
static gasnet_handlerentry_t const gasnete_handlers[] = {
    /* ptr-width independent handlers */

    /* ptr-width dependent handlers */
    { 0, NULL }
};

extern gasnet_handlerentry_t const *gasnete_get_handlertable() {
    return gasnete_handlers;
}

/* ------------------------------------------------------------------------------------ */

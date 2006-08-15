/*   $Source: /Users/kamil/work/gasnet-cvs2/gasnet/lapi-conduit/Attic/gasnet_extended.c,v $
 *     $Date: 2006/08/15 03:32:39 $
 * $Revision: 1.42.12.19 $
 * Description: GASNet Extended API Reference Implementation
 * Copyright 2002, Dan Bonachea <bonachea@cs.berkeley.edu>
 * Terms of use are as specified in license.txt
 */

#include <gasnet_internal.h>
#include <gasnet_extended_internal.h>
#include <gasnet_handler.h>

/* Because I haven't updated in a while and can't build with
   tracing on, here's my own tracing macro.  Later I'll do
   a search/replace */
/*#define GLTRACE(ignore_me,x) printf##x*/
#define GLTRACE(ignore_me,x)

#ifdef GASNETC_LAPI_RDMA
#include <sys/atomic_op.h>
#define TIME() gasnett_ticks_to_us(gasnett_ticks_now()) 
u_int64_t begin,end;
#endif

GASNETI_IDENT(gasnete_IdentString_Version, "$GASNetExtendedLibraryVersion: " GASNET_EXTENDED_VERSION_STR " $");
GASNETI_IDENT(gasnete_IdentString_ExtendedName, "$GASNetExtendedLibraryName: " GASNET_EXTENDED_NAME_STR " $");

static gasnete_threaddata_t *gasnete_threadtable[256] = { 0 };
static int gasnete_numthreads = 0;
static gasnet_hsl_t threadtable_lock = GASNET_HSL_INITIALIZER;
#if GASNETI_CLIENT_THREADS
  /* pthread thread-specific ptr to our threaddata (or NULL for a thread never-seen before) */
  static gasneti_threadkey_t gasnete_threaddata = GASNETI_THREADKEY_INITIALIZER;
#endif
static const gasnete_eopaddr_t EOPADDR_NIL = { { 0xFF, 0xFF } };
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

  gasneti_assert_always(gasnete_eopaddr_isnil(EOPADDR_NIL));
  GASNETE_WIREFLAGS_SANITYCHECK();
}

extern void gasnete_init() {
    static int firstcall = 1;
    GASNETI_TRACE_PRINTF(C,("gasnete_init()"));
    gasneti_assert(firstcall); /*  make sure we haven't been called before */
    firstcall = 0;

    gasnete_check_config(); /*  check for sanity */

    gasneti_assert(gasneti_nodes >= 1 && gasneti_mynode < gasneti_nodes);

    /* Exchange LAPI addresses here */
    gasnete_remote_memset_hh = (void**)gasneti_malloc(gasneti_nodes*sizeof(void*));
    GASNETC_LCHECK(LAPI_Address_init(gasnetc_lapi_context,
				     (void*)&gasnete_lapi_memset_hh,
				     gasnete_remote_memset_hh));

    gasnete_remote_barrier_hh = (void**)gasneti_malloc(gasneti_nodes*sizeof(void*));
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
#endif

	/* cause the first pool of eops to be allocated (optimization) */
	eop = gasnete_eop_new(threaddata);
	gasnete_op_markdone((gasnete_op_t *)eop, 0);
	gasnete_op_free((gasnete_op_t *)eop);
    }
     
#if 0
    /* Initialize barrier resources */
    gasnete_barrier_init();
#endif
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
	    if (gasneti_mynode == 0)
		for (i=0;i<256;i++) {                                   
		    fprintf(stderr,"%i:  %i: next=%i\n",gasneti_mynode,i,buf[i].addr.eopidx);
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
#if GASNETC_LAPI_RDMA
	GASNETC_LCHECK(LAPI_Setcntr(gasnetc_lapi_context,&(iop->rdma_put_cntr),0));
#endif
	GASNETC_LCHECK(LAPI_Setcntr(gasnetc_lapi_context,&iop->put_cntr,0));
        gasnete_iop_check(iop);
	return iop;
}

#if GASNETC_LAPI_RDMA

void gasnete_free_network_buffer(gasnete_lapi_nb *nb);
/* Use bounce buffers for transfers below the following threshold */
/* 4K for now, will/should be user controlled later */
int gasnete_pin_threshold = (4*1024); 

#endif

/*  query an op for completeness - for iop this means both puts and gets */
/* TODO
   For puts try to reap the tag yourself and don't poll */
#if GASNETC_LAPI_RDMA
void gasnetc_lapi_poll_tag_table(int numrounds);
#endif
int gasnete_op_isdone(gasnete_op_t *op) 
{
    int cnt = 0;
    int cnt2 = 0;
    gasneti_assert(op->threadidx == gasnete_mythread()->threadidx);
#if GASNETC_LAPI_RDMA
    /* Do some polling  (?) */
    /*gasnetc_lapi_poll_tag_table(xxxx);*/
#endif
    if_pt (OPTYPE(op) == OPTYPE_EXPLICIT) {
	gasnete_eop_t *eop = (gasnete_eop_t*)op;
	gasneti_assert(OPSTATE(op) != OPSTATE_FREE);
        gasnete_eop_check(eop);
#if GASNETC_LAPI_RDMA
        /*GLTRACE(C,("gasnete_op_isdone (explicit)\n"));*/
        if(eop->local_p) {
          return(1);
        }
	if(eop->get_p) {
	  GASNETC_LCHECK(LAPI_Getcntr(gasnetc_lapi_context,eop->origin_counter,&cnt));
          if(eop->num_transfers == cnt) {
	    return (1);
          } else {
	    return (0);
          }
	} else {
          GASNETC_LCHECK(LAPI_Getcntr(gasnetc_lapi_context,&(eop->completion_counter),&cnt));
	  if(eop->num_transfers == cnt) {
            return(1);
          } else {
	    return(0);
          }
	}
#else
	if (eop->initiated_cnt > 0) {
	    GASNETC_LCHECK(LAPI_Getcntr(gasnetc_lapi_context,&eop->cntr,&cnt));
	    gasneti_assert(cnt <= eop->initiated_cnt);
	}
	return (eop->initiated_cnt == cnt);
#endif
    } else {
	gasnete_iop_t *iop = (gasnete_iop_t*)op;
        gasnete_iop_check(iop);
#if GASNETC_LAPI_RDMA
      /* If the counters check out ok, then release all the pvos at once */
      /* A bad idea, I'm sure */
      /*GLTRACE(C,("gasnete_op_isdone (implicit)\n"));*/
      GASNETC_LCHECK(LAPI_Getcntr(gasnetc_lapi_context,&iop->get_cntr,&cnt));
      GASNETC_LCHECK(LAPI_Getcntr(gasnetc_lapi_context,&iop->rdma_put_cntr,&cnt2));
      if((cnt == iop->initiated_get_cnt) && (iop->initiated_put_cnt == cnt2)) {
        iop->initiated_get_cnt = iop->initiated_put_cnt = 0;
        return(1);
      } else {
        return(0);
      }
#else
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
#endif
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

#if GASNETC_LAPI_FED_POLLBUG_WORKAROUND
/*
 * MLW: HACK to get around LAPI FEDERATION bug.
 * We allow syncing some of the outstanding put operations to
 * throttle the number of outstanding in-flight messages.  
 */
static void gasnete_wait_syncnbi_myputs(int numputs GASNETE_THREAD_FARG)
{
    gasnete_threaddata_t * const mythread = GASNETE_MYTHREAD;
    gasnete_iop_t *iop = mythread->current_iop;
    int cnt = 0;
    gasneti_assert(iop->threadidx == mythread->threadidx);
    gasneti_assert(OPTYPE(iop) == OPTYPE_IMPLICIT);
    gasneti_assert(iop->initiated_put_cnt >= numputs);
    GASNETC_LCHECK(LAPI_Waitcntr(gasnetc_lapi_context,&iop->put_cntr,numputs,&cnt));
    iop->initiated_put_cnt -= numputs;
    gasneti_sync_reads();  /* MLW: is this needed? */
}
#endif

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

#if GASNETC_LAPI_RDMA
/* 
 * Try to resuse as much code as possible
 */

pthread_mutex_t nb_lock = PTHREAD_MUTEX_INITIALIZER;

/* Pin like there's no tomorrow! */
lapi_user_pvo_t gasnetc_lapi_get_local_pvo(lapi_long_t addr, size_t len)
{
	lapi_get_pvo_t new_pvo;
	new_pvo.Util_type = LAPI_XLATE_ADDRESS;
	new_pvo.length = len;
	new_pvo.usr_pvo = 0;
	new_pvo.address = (void *) addr;
	new_pvo.operation = LAPI_RDMA_ACQUIRE;
	/* Keep track of the PVOs and release the ones that have been around too long */
	GASNETC_LCHECK(LAPI_Util(gasnetc_lapi_context,(lapi_util_t *) &new_pvo));
	return(new_pvo.usr_pvo);
}

gasnetc_lapi_pvo *gasnetc_lapi_new_pvo()
{
   gasnetc_lapi_pvo *ret;
   int my_thread_id = gasnete_mythread()->threadidx;
   volatile gasnetc_lapi_pvo **current = (volatile gasnetc_lapi_pvo **) &(gasnetc_lapi_pvo_free_list[my_thread_id]);
   pthread_mutex_lock(&nb_lock);
   while(*current == NULL) {
     pthread_mutex_unlock(&nb_lock);
     gasneti_AMPoll();
     pthread_mutex_lock(&nb_lock);
   }
   ret = (gasnetc_lapi_pvo *) (*current);
   gasnetc_lapi_pvo_free_list[my_thread_id] = ret->next;
   ret->next = NULL;
   pthread_mutex_unlock(&nb_lock);
   return(ret);
}

void gasnetc_lapi_free_pvo(gasnetc_lapi_pvo *current)
{
  // Add current to the free list
  pthread_mutex_lock(&nb_lock);
  current->next = gasnetc_lapi_pvo_free_list[gasnete_mythread()->threadidx];
  gasnetc_lapi_pvo_free_list[gasnete_mythread()->threadidx] = current;
  pthread_mutex_unlock(&nb_lock);
}

void gasnetc_lapi_release_pvo(gasnetc_lapi_pvo *entry)
{
  lapi_get_pvo_t new_pvo;
  /* Unpin */
  new_pvo.Util_type = LAPI_XLATE_ADDRESS;
  new_pvo.length = 0;
  new_pvo.usr_pvo = entry->pvo;
  new_pvo.address = 0;
  new_pvo.operation = LAPI_RDMA_RELEASE;
  GASNETC_LCHECK(LAPI_Util(gasnetc_lapi_context, (lapi_util_t *) &new_pvo)); 
  /* Return this guy to the free list */
  gasnetc_lapi_free_pvo(entry);
}

/* 
 * TODO
 * ====
 * 
 * Add local firehose for better local pinning behaviour
 * Add remote firehose (for SEGMENT_EVERYTHING)
 * 
 */

/* BAD, BAD, BAD.  This guy spins until it finds a tag 
 * Should also not pay the atomic op price if running without threads
   Also, you may end up with multiple threads polling the same
   section of the table */

int gasnetc_lapi_last_polled = 0;
void gasnetc_lapi_poll_tag_table(int numrounds)
{
  int i,j;
  int old_val;
  /*GLTRACE(C,("gasnetc_lapi_poll_tag_table: Polling the tag table\n"));*/
  for(j=gasnetc_lapi_last_polled,i=0;i < numrounds;j = (j == (GASNETC_LAPI_MAX_TAGS-1) ? 0 : j+1),i++) {
    
    old_val = gasnetc_lapi_done;
    if(compare_and_swap((atomic_p) (gasnetc_lapi_local_target_counters + j),
		     &old_val, gasnetc_lapi_empty)) {
      /* Increment the completion counter for this guy */
      GLTRACE(C,("Polling the tag table, incrementing location %d\n",j));
      fetch_and_add((int *) gasnetc_lapi_completion_ptrs[j],1);
    }
  }
  gasnetc_lapi_last_polled = j;
  /*fetch_and_add(&gasnetc_lapi_last_polled, numrounds);*/
}

int gasnetc_lapi_get_unallocated_tag()
{
  int i = 0;
  int old_val;
  atomic_p current_loc;
  boolean_t result;
  int count=0;
  while(1) {
    current_loc =  (atomic_p) (gasnetc_lapi_local_target_counters + i);
    /* Compare and swap gasnetc_lapi_occupied != gasnetc_lapi_empty */
	
    old_val = gasnetc_lapi_empty; 
    if(compare_and_swap(current_loc, &old_val, gasnetc_lapi_occupied)) {
      return(i);
    } else {
      i = (i == (GASNETC_LAPI_MAX_TAGS-1)) ? 0 : i+1;
    }
    gasneti_AMPoll();
    count++;
    if(count > 10*GASNETC_LAPI_MAX_TAGS) {
      count = 0;
      printf("Stuck waiting for a tag???\n");
    }
  }

}

gasnete_lapi_nb *gasnete_free_nb_list_original = NULL;
char *gasnete_lapi_all_buffers = NULL;
gasnete_lapi_nb *gasnete_free_nb_list = NULL;
gasnete_lapi_nb *gasnete_active_nb_list = NULL;
#define GASNETE_LAPI_NUM_NB 1024
int gasnete_num_nb = GASNETE_LAPI_NUM_NB;
void gasnete_lapi_setup_nb()
{
  gasnete_free_nb_list_original = gasnete_free_nb_list = (gasnete_lapi_nb *) gasneti_malloc(gasnete_num_nb*sizeof(gasnete_lapi_nb));
  size_t total_pinned_region = gasnete_num_nb * gasnete_pin_threshold;
  char *all_data;
  size_t offset = 0;
  size_t size_pinned_region = 0;
  int i;
  int count = 0;
  lapi_get_pvo_t req;
  all_data = gasnete_lapi_all_buffers = (char *) gasneti_malloc(total_pinned_region);
  gasneti_assert(GASNETC_LAPI_PVO_EXTENT % gasnete_pin_threshold == 0);
  GLTRACE(C,("gasnete_lapi_setup_nb: node = %d pinned size = %ld #buffers = %d\n",gasneti_mynode,total_pinned_region,gasnete_num_nb));
  while(size_pinned_region < total_pinned_region) {
    int this_region_size = MIN(GASNETC_LAPI_PVO_EXTENT, total_pinned_region - size_pinned_region);
    int num_slices,s;
    req.Util_type = LAPI_XLATE_ADDRESS;
    req.length = this_region_size;
    req.usr_pvo =0;
    req.address = all_data + size_pinned_region;
    req.operation = LAPI_RDMA_ACQUIRE;
    GASNETC_LCHECK(LAPI_Util(gasnetc_lapi_context, (lapi_util_t *) &req));
    GLTRACE(C,("gasnete_lapi_setup_nb: %d got pvo %ld for network buffer\n",gasneti_mynode,req.usr_pvo)); 
    num_slices = this_region_size/gasnete_pin_threshold;
    for(s = 0; s < num_slices; s++) {
      gasnete_free_nb_list[count].data = all_data + size_pinned_region + s*gasnete_pin_threshold;
      gasnete_free_nb_list[count].offset = s*gasnete_pin_threshold;
      gasnete_free_nb_list[count].pvo = req.usr_pvo;
      gasnete_free_nb_list[count].prev = NULL;
      if(count < gasnete_num_nb-1) {
        gasnete_free_nb_list[count].next = &(gasnete_free_nb_list[count+1]);
      } else {
        gasnete_free_nb_list[count].next = NULL;
      }
      count++;
    }
    size_pinned_region += this_region_size;
  }
}

void gasnete_lapi_free_nb()
{
  int i;
  lapi_get_pvo_t req;
  for(i=0;i < gasnete_num_nb;i++) {
    /* Only release the PVO once.  The guy with offset == 0 is in some sense
       the "head" */
    if(gasnete_free_nb_list_original[i].offset == 0) {
      req.Util_type = LAPI_XLATE_ADDRESS;
      req.length = 0;
      req.address = 0;
      req.usr_pvo =gasnete_free_nb_list_original[i].pvo;
      req.operation = LAPI_RDMA_RELEASE;
      GASNETC_LCHECK(LAPI_Util(gasnetc_lapi_context, (lapi_util_t *) &req)); 
    }
  }
  gasneti_free(gasnete_lapi_all_buffers);
  gasneti_free(gasnete_free_nb_list_original);
}

/* Need back pointers etc. so that reaping happens */
gasnete_lapi_nb *gasnete_get_free_network_buffer()
{
  gasnete_lapi_nb *ret, *current;
  volatile gasnete_lapi_nb **fl_ptr = (volatile gasnete_lapi_nb **) &gasnete_free_nb_list;
  pthread_mutex_lock(&nb_lock);
  int cnt = 0;
  while(*fl_ptr == NULL) {
    /* Need to give up the lock for a while.  Need to tune this */
    pthread_mutex_unlock(&nb_lock);
    gasneti_AMPoll();
    cnt++;
    if(cnt > 100000) {
      cnt = 0;
      printf("Spinning too much waiting for a network buffer?\n");
    }
    pthread_mutex_lock(&nb_lock);
  }
  /* Remove from free list */
  ret = (gasnete_lapi_nb *) *fl_ptr;
  *fl_ptr = (*fl_ptr)->next;
  /* Place on active list */
  ret->next = gasnete_active_nb_list;
  ret->prev = NULL;
  if(gasnete_active_nb_list != NULL) {
    gasnete_active_nb_list->prev = ret;
  }
  gasnete_active_nb_list = ret;
 UNLOCK_AND_RETURN:
  pthread_mutex_unlock(&nb_lock);  /* This should take care of memory consistency nastiness, right? */
  return(ret);
}

void gasnete_free_network_buffer(gasnete_lapi_nb *nb)
{
  GLTRACE(C,("gasnete_free_network_buffer: node = %d id=%d old_id=%d\n",gasneti_mynode,nb->id,old_id));
  if(nb->get_p) {
    /* Copy out */
    memcpy(nb->user_buffer,nb->data,nb->user_length);
  }

  pthread_mutex_lock(&nb_lock);
  /* Remove from active list */
  if(gasnete_active_nb_list == nb) {
    gasnete_active_nb_list = nb->next;
    if(nb->next != NULL) {
      nb->next->prev = NULL;
    }
  } else {
    nb->prev->next = nb->next;
    if(nb->next != NULL) {
      nb->next->prev = nb->prev;
    }
  }

  /* Add back to free list */
  nb->next = gasnete_free_nb_list;
  gasnete_free_nb_list = nb; 
  pthread_mutex_unlock(&nb_lock);
}

lapi_long_t *gasnete_put_hndlr_table = NULL;
void* gasnete_put_hndlr(lapi_handle_t *context, void *uhdr, uint *uhdr_len,
			    ulong *msg_len, compl_hndlr_t **comp_h, void **uinfo)
{
  /* Reset the tag and bump up the pointer */
  int index = *((int *) uhdr);
  
#if 1
  /* Call LAPI instead to update the counter atomically??? */
  GASNETC_LCHECK(LAPI_Put(gasnetc_lapi_context,gasneti_mynode,0,NULL,NULL,NULL,gasnetc_lapi_completion_ptrs[index],NULL));
#else
  fetch_and_add(((int *) gasnetc_lapi_completion_ptrs[index]),1);
#endif
  gasnetc_lapi_local_target_counters[index]=gasnetc_lapi_empty;
  return(NULL);
}

void gasnete_setup_put_hndlr()
{
  gasnete_put_hndlr_table = (lapi_long_t *) gasneti_malloc(gasneti_nodes*sizeof(lapi_long_t));
  GASNETC_LCHECK(LAPI_Address_init64(gasnetc_lapi_context, (lapi_long_t) &gasnete_put_hndlr, gasnete_put_hndlr_table));
}

void gasnete_lapi_reap_network_buffer(lapi_handle_t *hndl, void *user_data, lapi_sh_info_t *info)
{
  gasnete_lapi_nb *nb_id = (gasnete_lapi_nb *) user_data;
  int oldval = fetch_and_add(&(nb_id->num_waiting),-1);
  if(oldval == 1) {
    /* Return this guy to the pool */
   gasnete_free_network_buffer(nb_id);
  }
}

void gasnete_lapi_reap_pvo(lapi_handle_t *hndl, void *user_data, lapi_sh_info_t *info)
{
  gasnetc_lapi_pvo *pvo_container = (gasnetc_lapi_pvo *) user_data; 
  gasnetc_lapi_release_pvo(pvo_container);
}

extern gasnete_eop_t *gasnete_lapi_do_rdma(void *dest, gasnet_node_t node, void *origin, size_t nbytes, int op, lapi_cntr_t *origin_counter, gasnete_iop_t *iop GASNETE_THREAD_FARG)
{
  lapi_long_t remote_p_to_long;
  lapi_remote_cxt_t rcxt;
  lapi_xfer_t xfer_struct;	/* From the LAPI docs, the structure holding all the information needed for an RDMA */
  int total_transfers = 0;
  size_t nbytes_transferred = 0;
  int first_call;
  int transfer_len;
  lapi_long_t local_p_to_long;
  lapi_user_pvo_t remote_pvo, source_pvo;
  int allocated_tag;
  gasnetc_lapi_pvo *pvo_container = NULL;
  lapi_cntr_t *cptr;
  gasnete_eop_t *new_eop;
  int using_network_buffer = 0;
  int length_to_boundary, length_to_remote_boundary, chunk_remaining, source_offset, remote_offset;
  int rctxt_index;

  /* Get an rctxt for this peer, round robin */
     
  rctxt_index = fetch_and_add(gasnetc_lapi_current_rctxt + node, 1) % gasnetc_rctxts_per_node;
#if 0
  /* For the single threaded case */
  rctxt_index = gasnetc_lapi_current_rctxt[node] % gasnetc_rctxts_per_node;
  gasnetc_lapi_current_rctxt[node] = gasnetc_lapi_current_rctxt[node] + 1;
#endif

  rcxt = gasnetc_remote_ctxts[node][rctxt_index];

  if(op == LAPI_RDMA_GET) {
    remote_p_to_long = (lapi_long_t) origin;
    local_p_to_long = (lapi_long_t) dest;
  } else {
    remote_p_to_long = (lapi_long_t) dest;
    local_p_to_long = (lapi_long_t) origin;
  }

  /* Create an eop for this operation */
  new_eop = gasnete_eop_new(GASNETE_MYTHREAD);
  new_eop->get_p = (op == LAPI_RDMA_GET);
  new_eop->network_buffer_id = NULL;
  new_eop->origin_counter = NULL;

  GLTRACE(C,("gasnete_lapi_do_rdma: dest = %ld node = %ld size = %ld op = %s\n",remote_p_to_long, node, nbytes, op == LAPI_RDMA_GET ? "GET" : "PUT"));
  
  gasnete_lapi_nb *nb_id = NULL;

  if(iop == NULL) {
    if(origin_counter != NULL) {
      new_eop->origin_counter = origin_counter;
    } else {
      new_eop->origin_counter = &(new_eop->cntr);
    }
    cptr = &(new_eop->completion_counter);
    GASNETC_LCHECK(LAPI_Setcntr(gasnetc_lapi_context,cptr,0));
    GASNETC_LCHECK(LAPI_Setcntr(gasnetc_lapi_context, new_eop->origin_counter, 0));
  } else {
    if(op == LAPI_RDMA_GET) {
      /* One typically doesn't pass iops between threads, right?
	 So this should be safe */
      new_eop->origin_counter = &(iop->get_cntr);
    } else {
      if(origin_counter != NULL) {
        new_eop->origin_counter = origin_counter;
      }
    }
    cptr = &(iop->rdma_put_cntr);
  }

  /* Cannot do an RDMA with yourself.  For now do a memcpy and fake everything else */
  if(node == gasneti_mynode) {
    memcpy(dest,origin,nbytes);
    if(iop == NULL) {
      new_eop->origin_counter = NULL;
      new_eop->local_p = 1;
    }
    return(new_eop);
  } else {
    if(iop == NULL) {
      new_eop->local_p = 0;
    }
  }
  

  /* Clean out the descriptor */
  bzero (&xfer_struct, sizeof (xfer_struct));
  /* If the transfer is really small (for some definition of "really small") */

  GLTRACE(C,("gasnete_lapi_do_rdma: nbytes = %ld threshold = %ld\n",nbytes,gasnete_pin_threshold));
  if(!(((local_p_to_long >= gasnetc_segbase_table[gasneti_mynode])
      && (local_p_to_long < gasnetc_segbase_table[gasneti_mynode] + gasneti_seginfo[gasneti_mynode].size))) && nbytes <= gasnete_pin_threshold) {
      using_network_buffer = 1;
      /* Get a free buffer */
      GLTRACE(C,("gasnete_lapi_do_rdma: looking for network buffer\n"));
      nb_id = gasnete_get_free_network_buffer();
      GLTRACE(C,("gasnete_lapi_do_rdma: got a network buffer pvo=%ld offset=%d id=%d\n",nb_id->pvo,nb_id->offset,nb_id->id)); 
      /* Copy in for puts */
      if(op != LAPI_RDMA_GET) {
        memcpy(nb_id->data,(void *) local_p_to_long,nbytes);
      }

      new_eop->network_buffer_id = nb_id;
      nb_id->user_buffer = (void *) local_p_to_long;
      nb_id->user_length = nbytes;
      nb_id->get_p = (op == LAPI_RDMA_GET);
  } 

  /* Do something special if the origin is within the pinned segment or we can use a network buffer */
  if (using_network_buffer || ((local_p_to_long >= gasnetc_segbase_table[gasneti_mynode])
      && (local_p_to_long < gasnetc_segbase_table[gasneti_mynode] + gasneti_seginfo[gasneti_mynode].size))) {

    /* No need to pin origin, but you need to deal with misalignment 
       (in terms of the PVO boundaries)
       of the pinned regions of the origin and destination */

    while(nbytes_transferred < nbytes) {

      /* The number of bytes to either the end of the current PVO region
         or the end of the data */


      if(using_network_buffer) {
        GLTRACE(C,("gasnete_lapi_do_rdma: using network buffer nbytes = %ld\n",nbytes));
        chunk_remaining = length_to_boundary = nbytes;
        source_offset = nb_id->offset;
        source_pvo = nb_id->pvo;
      } else {

        length_to_boundary = (int) MIN(GASNETC_LAPI_PVO_EXTENT - ((local_p_to_long + nbytes_transferred) % GASNETC_LAPI_PVO_EXTENT),
				     nbytes - nbytes_transferred);
      
        /* Try to transfer this chunk of bytes.  It will either take
           one or two RDMA calls depending on whether or not it is entirely
           within a single PVO region at the target */
       
        chunk_remaining = length_to_boundary;
        source_offset = ((local_p_to_long + nbytes_transferred - gasnetc_segbase_table[gasneti_mynode]) % GASNETC_LAPI_PVO_EXTENT);
        source_pvo = gasnetc_pvo_table[(local_p_to_long + nbytes_transferred - gasnetc_segbase_table[gasneti_mynode])/
				       GASNETC_LAPI_PVO_EXTENT][gasneti_mynode];  
      }
      first_call=1; 
      do {
	remote_pvo = gasnetc_pvo_table[(remote_p_to_long + nbytes_transferred -
				       gasnetc_segbase_table[node]) / GASNETC_LAPI_PVO_EXTENT][node];
	length_to_remote_boundary = MIN(GASNETC_LAPI_PVO_EXTENT - ((remote_p_to_long + nbytes_transferred) 
							      % GASNETC_LAPI_PVO_EXTENT),chunk_remaining);

        remote_offset = ((remote_p_to_long + nbytes_transferred - gasnetc_segbase_table[node]) % GASNETC_LAPI_PVO_EXTENT);

        if(using_network_buffer && first_call) {
          first_call=0;
          if(length_to_remote_boundary != nbytes) {
            nb_id->num_waiting = 2;
          } else {
            nb_id->num_waiting = 1;
          }
        }

	/* Send this off now */

	xfer_struct.HwXfer.src_pvo = source_pvo;
	xfer_struct.HwXfer.src_offset = source_offset;
	
	xfer_struct.HwXfer.tgt_pvo = remote_pvo;
	xfer_struct.HwXfer.tgt_offset = remote_offset;
	
	
	xfer_struct.HwXfer.Xfer_type = LAPI_RDMA_XFER;
	xfer_struct.HwXfer.tgt = node;
	xfer_struct.HwXfer.op = op;
	if(op == LAPI_RDMA_GET) {
	  xfer_struct.HwXfer.rdma_tag = GASNETC_LAPI_RDMA_GET_TAG;
	} else {
	  xfer_struct.HwXfer.op |= LAPI_RCALLBACK;
	  xfer_struct.HwXfer.rdma_tag = gasnetc_lapi_get_unallocated_tag();
	  /* Add the pointer to the completion counter here */
	  gasnetc_lapi_completion_ptrs[xfer_struct.HwXfer.rdma_tag] = cptr;
	}
	xfer_struct.HwXfer.remote_cxt = rcxt.usr_rcxt;
	xfer_struct.HwXfer.len = length_to_remote_boundary;
        if(using_network_buffer) {
	  xfer_struct.HwXfer.shdlr = gasnete_lapi_reap_network_buffer;
	  xfer_struct.HwXfer.sinfo = (void *) nb_id;
        } else {
	  xfer_struct.HwXfer.shdlr = (scompl_hndlr_t *) NULL;
	  xfer_struct.HwXfer.sinfo = (void *) NULL;
        }
	xfer_struct.HwXfer.org_cntr = new_eop->origin_counter;
        GLTRACE(C,("gasnete_lapi_do_rdma: transfer length = %d nbytes_transferred = %ld bytes remaining = %ld remote_offset=%d real remote offfset=%ld segment start = %ld GASNETC_LAPI_PVO_EXTENT=%d offset in remote pvo table = %d tag=%d remote pvo=%ld\n",length_to_remote_boundary, nbytes_transferred, chunk_remaining,remote_offset,remote_p_to_long + nbytes_transferred,gasnetc_segbase_table[node],GASNETC_LAPI_PVO_EXTENT,(remote_p_to_long + nbytes_transferred-gasnetc_segbase_table[node])/GASNETC_LAPI_PVO_EXTENT, xfer_struct.HwXfer.rdma_tag,remote_pvo));
        
	GASNETC_LCHECK (LAPI_Xfer (gasnetc_lapi_context, &xfer_struct));
       
	total_transfers++;
	nbytes_transferred += length_to_remote_boundary;
	chunk_remaining -= length_to_remote_boundary;
	source_offset += length_to_remote_boundary;
        GLTRACE(C,("gasnete_lapi_do_rdma: xfer call completed chunk_remaining=%ld nbytes_transferred=%ld nbytes=%ld using_network_buffer=%d\n",chunk_remaining,nbytes_transferred,nbytes,using_network_buffer,end-begin));
      } while (chunk_remaining > 0);
      
    }
             
  } else {
    first_call = 1;
    while (nbytes_transferred < nbytes) {
      /* Compute the number of bytes to be transferred in this step.  Try
       * to align the local and remote sides so that the maximum amount of data
       * is transferred with each call */
      
      if (first_call) {
	/* Transfer only up to the next PVO boundary on the remote side */
        remote_offset = ((remote_p_to_long + nbytes_transferred - gasnetc_segbase_table[node]) % GASNETC_LAPI_PVO_EXTENT);
	source_offset = 0;
	transfer_len = MIN (nbytes - nbytes_transferred, GASNETC_LAPI_PVO_EXTENT - remote_offset);
	xfer_struct.HwXfer.src_pvo = gasnetc_lapi_get_local_pvo(local_p_to_long + nbytes_transferred, transfer_len);
	xfer_struct.HwXfer.tgt_pvo =
	  gasnetc_pvo_table[(remote_p_to_long + nbytes_transferred -
			    gasnetc_segbase_table[node]) /
			   GASNETC_LAPI_PVO_EXTENT][node];
	xfer_struct.HwXfer.tgt_offset = remote_offset;
	xfer_struct.HwXfer.src_offset = source_offset;
      } else {
	/* Can now transfer in chunks of size GASNETC_LAPI_PVO_EXTENT */
	transfer_len = MIN (GASNETC_LAPI_PVO_EXTENT, nbytes - nbytes_transferred);
	xfer_struct.HwXfer.src_pvo = gasnetc_lapi_get_local_pvo (local_p_to_long + nbytes_transferred, transfer_len);
	xfer_struct.HwXfer.tgt_pvo = gasnetc_pvo_table[(remote_p_to_long + nbytes_transferred -
						       gasnetc_segbase_table[node]) / GASNETC_LAPI_PVO_EXTENT][node];
	xfer_struct.HwXfer.tgt_offset = 0;
	xfer_struct.HwXfer.src_offset = 0;
      }

      pvo_container = gasnetc_lapi_new_pvo();
      pvo_container->pvo = xfer_struct.HwXfer.src_pvo;

      first_call = 0;
      xfer_struct.HwXfer.Xfer_type = LAPI_RDMA_XFER;
      xfer_struct.HwXfer.tgt = node;
      xfer_struct.HwXfer.op = op;
      if(op == LAPI_RDMA_GET) {
	xfer_struct.HwXfer.rdma_tag = GASNETC_LAPI_RDMA_GET_TAG;
      } else {
	xfer_struct.HwXfer.op |= LAPI_RCALLBACK;
	xfer_struct.HwXfer.rdma_tag = gasnetc_lapi_get_unallocated_tag();
	/* Add the pointer to the completion counter here */
	gasnetc_lapi_completion_ptrs[xfer_struct.HwXfer.rdma_tag] = cptr;
      }
      xfer_struct.HwXfer.remote_cxt = rcxt.usr_rcxt;
      xfer_struct.HwXfer.len = transfer_len;
      xfer_struct.HwXfer.shdlr = gasnete_lapi_reap_pvo;
      xfer_struct.HwXfer.sinfo = (void *) pvo_container;
      xfer_struct.HwXfer.org_cntr = new_eop->origin_counter;
      
      /* Do the transfer */
      GASNETC_LCHECK (LAPI_Xfer (gasnetc_lapi_context, &xfer_struct));
      total_transfers++;
      nbytes_transferred += transfer_len;
    }
  }

  /* Return an eop  with all the required information */
  new_eop->num_transfers = total_transfers;
  if(iop == NULL) {
    return(new_eop);
  } else {
    if(op == LAPI_RDMA_GET) {
      iop->initiated_get_cnt += total_transfers;
    } else {
      iop->initiated_put_cnt += total_transfers;
    }
    return(new_eop);
  }
}
#endif

/* ------------------------------------------------------------------------------------ */
extern void gasnete_get_bulk (void *dest, gasnet_node_t node, void *src,
			      size_t nbytes GASNETE_THREAD_FARG)
{
    lapi_cntr_t c_cntr;
    int num_get = 0;
    int cur_cntr;

#if GASNETC_LAPI_RDMA
    gasnete_eop_t *eop;
    GLTRACE(C,("gasnete_get_bulk\n"));
    GASNETC_LCHECK(LAPI_Setcntr(gasnetc_lapi_context, &c_cntr, 0));
    gasneti_suspend_spinpollers();
    begin = TIME();    
    eop = gasnete_lapi_do_rdma(dest,node,src,nbytes,LAPI_RDMA_GET, &c_cntr,NULL GASNETE_THREAD_GET);
    end = TIME();
    /*printf("gasnete_get_bulk: do_rdma returned in %ld us\n",end-begin);*/
    gasneti_resume_spinpollers();
    /* Wait on this eop */
    GLTRACE(C,("gasnete_get_bulk: wait on sync\n"));
#if 0
    begin = TIME();    
    gasnete_wait_syncnb((gasnet_handle_t) eop);
    end = TIME();
#else
    begin = TIME();    
    GASNETC_LCHECK((LAPI_Waitcntr(gasnetc_lapi_context, eop->origin_counter,eop->num_transfers,&num_get)));
    end = TIME();
    /*printf("gasnete_get_bulk: wait_syncnb returned in %ld us\n",end-begin);*/
#endif
    gasnete_op_free((gasnete_op_t *)eop);
#else
    GASNETC_LCHECK(LAPI_Setcntr(gasnetc_lapi_context, &c_cntr, 0));

    /* Issue as many gets as required.
     * Will generally only be one */
    gasneti_suspend_spinpollers();
    while (nbytes > 0) {
	ulong to_get = MIN(nbytes, gasnetc_max_lapi_data_size);
	GASNETC_LCHECK(LAPI_Get(gasnetc_lapi_context, (unsigned int)node, to_get,
				src,dest,NULL,&c_cntr));
	dest = (void*)((char*)dest + to_get);
	src = (void*)((char*)src + to_get);
	num_get++;
	nbytes -= to_get;
    }
    gasneti_resume_spinpollers();
    /* block until all gets complete */
    GASNETC_WAITCNTR(&c_cntr,num_get,&cur_cntr);
    gasneti_assert(cur_cntr == 0);
#endif
}

/* ------------------------------------------------------------------------------------ */
extern void gasnete_put_bulk (gasnet_node_t node, void *dest, void *src,
			      size_t nbytes GASNETE_THREAD_FARG)
{
    lapi_cntr_t  c_cntr;
    int num_put = 0;
    int cur_cntr;

#if GASNETC_LAPI_RDMA
    gasnete_eop_t *eop;
    GLTRACE(C,("gasnete_put_bulk target node=%d\n",node));
    GASNETC_LCHECK(LAPI_Setcntr(gasnetc_lapi_context, &c_cntr, 0));
    gasneti_suspend_spinpollers();
    eop = gasnete_lapi_do_rdma(dest,node,src,nbytes,LAPI_RDMA_PUT, &c_cntr,NULL GASNETE_THREAD_GET);
    GLTRACE(C,("gasnete_put_bulk: do_rdma returned is %ld us\n",end-begin));
    gasneti_resume_spinpollers();
    /* Wait on this eop */
    GLTRACE(C,("gasnete_put_bulk: wait on sync\n"));
#if 1
    GASNETC_LCHECK((LAPI_Waitcntr(gasnetc_lapi_context, &(eop->completion_counter),eop->num_transfers,&num_put)));
    /* Free the pinned region */
#else
    gasnete_wait_syncnb((gasnet_handle_t) eop);
#endif
    GLTRACE(C,("gasnete_put_bulk: wait done\n"));
    gasnete_op_free((gasnete_op_t *)eop);
#else
    GASNETC_LCHECK(LAPI_Setcntr(gasnetc_lapi_context, &c_cntr, 0));

    /* Issue as many puts as required.
     * Will generally only be one */
    gasneti_suspend_spinpollers();
    while (nbytes > 0) {
	ulong to_put = MIN(nbytes, gasnetc_max_lapi_data_size);
	/* use op lapi counter as completion counter,
	 * and o_cntr as origin counter */
	GASNETC_LCHECK(LAPI_Put(gasnetc_lapi_context, (unsigned int) node, to_put,
				dest,src,NULL,NULL,&c_cntr));
	dest = (void*)((char*)dest + to_put);
	src = (void*)((char*)src + to_put);
	num_put++;
	nbytes -= to_put;
    }
    gasneti_resume_spinpollers();

    /* block until all complete */
    GASNETC_WAITCNTR_FBW(&c_cntr,num_put,&cur_cntr);
    gasneti_assert(cur_cntr == 0);
#endif
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
    gasneti_suspend_spinpollers();
    GASNETC_LCHECK(LAPI_Amsend(gasnetc_lapi_context, (unsigned int)node,
			       gasnete_remote_memset_hh[node],
			       &uhdr, sizeof(gasnete_memset_uhdr_t), NULL, 0,
			       NULL, NULL, &c_cntr));
    gasneti_resume_spinpollers();
   

    /* block until complete */
    GASNETC_WAITCNTR_FBW(&c_cntr,1,&cur_cntr);
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
#if GASNETC_LAPI_RDMA
    /* Need to clean up the calling sequence here */
    gasnete_eop_t *eop;
    GLTRACE(C,("gasnete_get_nb_bulk\n"));
    gasneti_suspend_spinpollers();
    eop = gasnete_lapi_do_rdma(dest,node,src,nbytes,LAPI_RDMA_GET, NULL, NULL GASNETE_THREAD_GET);
    gasneti_resume_spinpollers();
    return((gasnet_handle_t) eop);
#else
    gasnete_eop_t *op = gasnete_eop_new(GASNETE_MYTHREAD);

    /* Issue as many gets as required.
     * Will generally only be one */
    gasneti_suspend_spinpollers();
    while (nbytes > 0) {
	ulong to_get = MIN(nbytes, gasnetc_max_lapi_data_size);
	GASNETC_LCHECK(LAPI_Get(gasnetc_lapi_context, (unsigned int) node, to_get,
				src,dest,NULL,&op->cntr));
	dest = (void*)((char*)dest + to_get);
	src = (void*)((char*)src + to_get);
	op->initiated_cnt++;
	nbytes -= to_get;
    }
    gasneti_resume_spinpollers();
    return (gasnet_handle_t)op;
#endif
}

/* ------------------------------------------------------------------------------------ */
extern gasnet_handle_t gasnete_put_nb_bulk (gasnet_node_t node, void *dest, void *src,
					    size_t nbytes GASNETE_THREAD_FARG)
{
#if GASNETC_LAPI_RDMA
    gasnete_eop_t *eop;
    GLTRACE(C,("gasnete_put_nb_bulk\n"));
    gasneti_suspend_spinpollers();
    eop = gasnete_lapi_do_rdma(dest,node,src,nbytes,LAPI_RDMA_PUT, NULL, NULL GASNETE_THREAD_GET);
    gasneti_resume_spinpollers();
    /* Don't wait for source completion */
    return((gasnet_handle_t) eop);
#else
    gasnete_eop_t *op = gasnete_eop_new(GASNETE_MYTHREAD);

    /* Issue as many puts as required.
     * Will generally only be one */
    gasneti_suspend_spinpollers();
    while (nbytes > 0) {
	ulong to_put = MIN(nbytes, gasnetc_max_lapi_data_size);
	/* use op lapi counter as completion counter */
	GASNETC_LCHECK(LAPI_Put(gasnetc_lapi_context, (unsigned int)node, to_put,
				dest,src,NULL,NULL,&op->cntr));
	dest = (void*)((char*)dest + to_put);
	src = (void*)((char*)src + to_put);
	op->initiated_cnt++;
	nbytes -= to_put;
    }
    gasneti_resume_spinpollers();

#if GASNETC_LAPI_FED_POLLBUG_WORKAROUND
    gasnete_wait_syncnb((gasnet_handle_t)op);
    return GASNET_INVALID_HANDLE;
#else
    return (gasnet_handle_t)op;
#endif
#endif
}

/* ------------------------------------------------------------------------------------ */
extern gasnet_handle_t gasnete_put_nb (gasnet_node_t node, void *dest, void *src,
				       size_t nbytes GASNETE_THREAD_FARG)
{
#if GASNETC_LAPI_RDMA
    gasnete_eop_t *eop;
    int cur_cntr;
    GLTRACE(C,("gasnete_put_nb\n"));
    gasneti_suspend_spinpollers();
    eop = gasnete_lapi_do_rdma(dest,node,src,nbytes,LAPI_RDMA_PUT, NULL, NULL GASNETE_THREAD_GET);
    gasneti_resume_spinpollers();
    /* Wait for the origin counter to indicate local completion */
    GASNETC_WAITCNTR(eop->origin_counter,eop->num_transfers,&cur_cntr);
    gasneti_assert(cur_cntr == 0);
    return((gasnet_handle_t) eop);
#else
    gasnete_eop_t *op = gasnete_eop_new(GASNETE_MYTHREAD);
    lapi_cntr_t  o_cntr;
    int num_put = 0;
    int cur_cntr;

    GASNETC_LCHECK(LAPI_Setcntr(gasnetc_lapi_context, &o_cntr, 0));

    /* Issue as many puts as required.
     * Will generally only be one */
    gasneti_suspend_spinpollers();
    while (nbytes > 0) {
	ulong to_put = MIN(nbytes, gasnetc_max_lapi_data_size);
	/* use op lapi counter as completion counter,
	 * and o_cntr as origin counter */
	GASNETC_LCHECK(LAPI_Put(gasnetc_lapi_context, (unsigned int) node, to_put,
				dest,src,NULL,&o_cntr,&op->cntr));
	dest = (void*)((char*)dest + to_put);
	src = (void*)((char*)src + to_put);
	num_put++;
	nbytes -= to_put;
    }
    gasneti_resume_spinpollers();
    op->initiated_cnt += num_put;
    /* Client allowed to modify src data after return.  Make sure operation
     * is complete at origin
     */
    GASNETC_WAITCNTR(&o_cntr,num_put,&cur_cntr);
    gasneti_assert(cur_cntr == 0);
    
#if GASNETC_LAPI_FED_POLLBUG_WORKAROUND
    gasnete_wait_syncnb((gasnet_handle_t)op);
    return GASNET_INVALID_HANDLE;
#else
    return (gasnet_handle_t)op;
#endif
#endif
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
#if GASNETC_LAPI_RDMA
    gasneti_suspend_spinpollers();
    GASNETC_LCHECK(LAPI_Setcntr(gasnetc_lapi_context, &(op->completion_counter), 0));
    op->num_transfers = 1;
    op->get_p = 0;
    op->network_buffer_id = NULL;
    op->local_p = 0;
    GASNETC_LCHECK(LAPI_Amsend(gasnetc_lapi_context, (unsigned int)node,
			       gasnete_remote_memset_hh[node],
			       &uhdr, sizeof(gasnete_memset_uhdr_t), NULL, 0,
			       NULL, &o_cntr, &(op->completion_counter)));
    gasneti_resume_spinpollers();
#else
    gasneti_suspend_spinpollers();
    GASNETC_LCHECK(LAPI_Amsend(gasnetc_lapi_context, (unsigned int)node,
			       gasnete_remote_memset_hh[node],
			       &uhdr, sizeof(gasnete_memset_uhdr_t), NULL, 0,
			       NULL, &o_cntr, &op->cntr));
    gasneti_resume_spinpollers();
   
    op->initiated_cnt++;
#endif
    /* must insure operation has completed locally since uhdr is a stack variable.
     * This will ALMOST ALWAYS be true in the case of such a small message */
    GASNETC_WAITCNTR(&o_cntr,1,&cur_cntr);
    gasneti_assert(cur_cntr == 0);

#if GASNETC_LAPI_FED_POLLBUG_WORKAROUND
    gasnete_wait_syncnb((gasnet_handle_t)op);
    return GASNET_INVALID_HANDLE;
#else
    return (gasnet_handle_t)op;
#endif
}

/* ------------------------------------------------------------------------------------ */
/*
  Synchronization for explicit-handle non-blocking operations:
  ===========================================================
*/

extern int  gasnete_try_syncnb(gasnet_handle_t handle) {
    /*GLTRACE(C,("gasnete_try_syncnb\n"));*/
    GASNETI_SAFE(gasneti_AMPoll());
    /*GLTRACE(C,("gasnete_try_syncnb: after poll\n"));*/

    if (handle == GASNET_INVALID_HANDLE)
	return GASNET_OK;

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

#if GASNETC_LAPI_RDMA
extern void gasnete_wait_syncnb(gasnet_handle_t handle)
{
    gasnete_op_t *op = handle;
    int cnt = 0;
    int cnt2 = 0;
    gasneti_assert(op->threadidx == gasnete_mythread()->threadidx);
    if_pt (OPTYPE(op) == OPTYPE_EXPLICIT) {
	gasnete_eop_t *eop = (gasnete_eop_t*)op;
	gasneti_assert(OPSTATE(op) != OPSTATE_FREE);
        gasnete_eop_check(eop);
        if(eop->local_p) {
          goto END;
        }
	if(eop->get_p) {
          GASNETC_LCHECK((LAPI_Waitcntr(gasnetc_lapi_context, eop->origin_counter,eop->num_transfers,&cnt)));
	} else {
          GASNETC_LCHECK((LAPI_Waitcntr(gasnetc_lapi_context, &(eop->completion_counter),eop->num_transfers,&cnt)));
	}
    } else {
	gasnete_iop_t *iop = (gasnete_iop_t*)op;
        gasnete_iop_check(iop);
      /* If the counters check out ok, then release all the pvos at once */
      /* A bad idea, I'm sure */
      /*GLTRACE(C,("gasnete_op_isdone (implicit)\n"));*/
   
      /*printf("wait_syncnb for iop targets=%d,%d\n",iop->initiated_get_cnt,iop->initiated_put_cnt);*/
      GASNETC_LCHECK(LAPI_Waitcntr(gasnetc_lapi_context,&iop->get_cntr,iop->initiated_get_cnt,&cnt));
      GASNETC_LCHECK(LAPI_Waitcntr(gasnetc_lapi_context,&iop->rdma_put_cntr,iop->initiated_put_cnt,&cnt2));
      /*printf("wait_syncnb for iop targets (done) =%d,%d\n",iop->initiated_get_cnt,iop->initiated_put_cnt);*/
      /* Got to do this because rdma_put_cntr is decremented */
      iop->initiated_put_cnt=0;
      iop->initiated_get_cnt=0;
    }
END:
    gasneti_sync_reads();
    gasnete_op_free(handle);
  
}

extern void gasnete_wait_syncnb_all(gasnet_handle_t *phandle, size_t numhandles)
{
    gasneti_assert(phandle);

    { int i;
    for (i = 0; i < numhandles; i++) {
	gasnete_op_t *op = phandle[i];
	if (op != GASNET_INVALID_HANDLE) {
	    gasnete_wait_syncnb(op);
	    phandle[i] = GASNET_INVALID_HANDLE;
	}
    }
    }
}

extern void gasnete_wait_syncnbi_gets(GASNETE_THREAD_FARG_ALONE) 
{
    gasnete_threaddata_t * const mythread = GASNETE_MYTHREAD;
    gasnete_iop_t *iop = mythread->current_iop;
    int cnt = 0;
    gasneti_assert(iop->threadidx == mythread->threadidx);
    gasneti_assert(iop->next == NULL);
    gasneti_assert(OPTYPE(iop) == OPTYPE_IMPLICIT);
#if GASNET_DEBUG
    if (iop->next != NULL)
	gasneti_fatalerror("VIOLATION: attempted to call gasnete_wait_syncnbi_gets() inside an NBI access region");
#endif
    /*printf("wait_syncnbi_gets target=%d\n",iop->initiated_get_cnt);*/
    GASNETC_LCHECK(LAPI_Waitcntr(gasnetc_lapi_context,&iop->get_cntr,iop->initiated_get_cnt,&cnt));
    gasneti_sync_reads();
    iop->initiated_get_cnt = 0;
}

extern void gasnete_wait_syncnbi_puts(GASNETE_THREAD_FARG_ALONE) 
{
    gasnete_threaddata_t * const mythread = GASNETE_MYTHREAD;
    gasnete_iop_t *iop = mythread->current_iop;
    int cnt = 0;
    gasneti_assert(iop->threadidx == mythread->threadidx);
    gasneti_assert(iop->next == NULL);
    gasneti_assert(OPTYPE(iop) == OPTYPE_IMPLICIT);
#if GASNET_DEBUG
    if (iop->next != NULL)
	gasneti_fatalerror("VIOLATION: attempted to call gasnete_wait_syncnbi_puts() inside an NBI access region");
#endif
    /*printf("wait_syncnbi_puts target=%d\n",iop->initiated_put_cnt);*/
    GASNETC_LCHECK(LAPI_Waitcntr(gasnetc_lapi_context,&iop->rdma_put_cntr,iop->initiated_put_cnt,&cnt));
    gasneti_sync_reads();
    iop->initiated_put_cnt = 0;
}

extern void gasnete_wait_syncnbi_all(GASNETE_THREAD_FARG_ALONE) 
{
    gasnete_wait_syncnbi_puts(GASNETE_THREAD_GET_ALONE);
    gasnete_wait_syncnbi_gets(GASNETE_THREAD_GET_ALONE);
}
#endif

#if GASNETC_LAPI_FED_POLLBUG_WORKAROUND
extern void gasnete_wait_syncnb(gasnet_handle_t handle) {
#if GASNETC_LAPI_RDMA
   error("RDMA + FED_POLLBUG_WORKAROUND");
#endif
    int cnt = 0;
    gasnete_op_t *op = handle;
    if (handle == GASNET_INVALID_HANDLE)
	return;
    gasneti_assert(op->threadidx == gasnete_mythread()->threadidx);
    if_pt (OPTYPE(op) == OPTYPE_EXPLICIT) {
	gasnete_eop_t *eop = (gasnete_eop_t*)op;
	gasneti_assert(OPSTATE(op) != OPSTATE_FREE);
        gasnete_eop_check(eop);
	if (eop->initiated_cnt > 0) {
	    GASNETC_LCHECK(LAPI_Waitcntr(gasnetc_lapi_context,&eop->cntr,eop->initiated_cnt,&cnt));
	    gasneti_assert(cnt == 0);
	    eop->initiated_cnt =0;
	}
    } else {
	gasnete_iop_t *iop = (gasnete_iop_t*)op;
        gasnete_iop_check(iop);
	if (iop->initiated_get_cnt > 0) {
	    GASNETC_LCHECK(LAPI_Waitcntr(gasnetc_lapi_context,&iop->get_cntr,iop->initiated_get_cnt,&cnt));
	    gasneti_assert(cnt == 0);
	    iop->initiated_get_cnt = 0;
	}
	if (iop->initiated_put_cnt > 0) {
	    GASNETC_LCHECK(LAPI_Waitcntr(gasnetc_lapi_context,&iop->put_cntr,iop->initiated_put_cnt,&cnt));
	    gasneti_assert(cnt == 0);
	    iop->initiated_put_cnt = 0;
	}
    }
    gasneti_sync_reads();
    gasnete_op_free(handle);
}
extern void gasnete_wait_syncnb_some(gasnet_handle_t *phandle, size_t numhandles) {
    gasneti_assert(phandle);

    { int i;
    for (i = 0; i < numhandles; i++) {
	gasnete_op_t *op = phandle[i];
	if (op != GASNET_INVALID_HANDLE) {
	    gasnete_wait_syncnb(op);
	    phandle[i] = GASNET_INVALID_HANDLE;
	    /* got one, just return */
	    break;
	}
    }
    }
}
extern void gasnete_wait_syncnb_all(gasnet_handle_t *phandle, size_t numhandles) {
    gasneti_assert(phandle);

    { int i;
    for (i = 0; i < numhandles; i++) {
	gasnete_op_t *op = phandle[i];
	if (op != GASNET_INVALID_HANDLE) {
	    gasnete_wait_syncnb(op);
	    phandle[i] = GASNET_INVALID_HANDLE;
	}
    }
    }
}
#endif

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

#if GASNETC_LAPI_RDMA
    gasnete_eop_t *eop;
    GLTRACE(C,("gasnete_get_nbi_bulk\n"));
    gasneti_suspend_spinpollers();
    eop = gasnete_lapi_do_rdma(dest,node,src,nbytes,LAPI_RDMA_GET, NULL, op GASNETE_THREAD_GET);
    gasnete_op_free((gasnete_op_t *) eop); 
    gasneti_resume_spinpollers();
#else
    gasneti_suspend_spinpollers();
    while (nbytes > 0) {
	ulong to_get = MIN(nbytes, gasnetc_max_lapi_data_size);
	GASNETC_LCHECK(LAPI_Get(gasnetc_lapi_context, (unsigned int)node, to_get,
				src,dest,NULL,&op->get_cntr));
	dest = (void*)((char*)dest + to_get);
	src = (void*)((char*)src + to_get);
	op->initiated_get_cnt++;
	nbytes -= to_get;
    }
    gasneti_resume_spinpollers();
#endif
}

/* ------------------------------------------------------------------------------------ */
extern void gasnete_put_nbi_bulk (gasnet_node_t node, void *dest, void *src,
				  size_t nbytes GASNETE_THREAD_FARG)
{
    gasnete_threaddata_t * const mythread = GASNETE_MYTHREAD;
    gasnete_iop_t *op = mythread->current_iop;
#if GASNETC_LAPI_RDMA
    gasnete_eop_t *eop;
    gasneti_suspend_spinpollers();
    eop = gasnete_lapi_do_rdma(dest,node,src,nbytes,LAPI_RDMA_PUT, NULL, op GASNETE_THREAD_GET);
    gasnete_op_free((gasnete_op_t *) eop); 
    gasneti_resume_spinpollers();
#else
    int num_put = 0;

    /* Issue as many puts as required.
     * Will generally only be one */
    gasneti_suspend_spinpollers();
    while (nbytes > 0) {
	ulong to_put = MIN(nbytes, gasnetc_max_lapi_data_size);
	/* use op lapi counter as completion counter */
	GASNETC_LCHECK(LAPI_Put(gasnetc_lapi_context, (unsigned int)node, to_put,
				dest,src,NULL,NULL,&op->put_cntr));
	dest = (void*)((char*)dest + to_put);
	src = (void*)((char*)src + to_put);
	nbytes -= to_put;
	num_put++;
    }
    op->initiated_put_cnt += num_put;
    gasneti_resume_spinpollers();

#if GASNETC_LAPI_FED_POLLBUG_WORKAROUND
    gasnete_wait_syncnbi_myputs(num_put GASNETE_THREAD_GET);
#endif    
#endif
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

#if GASNETC_LAPI_RDMA
    gasnete_eop_t *result;
    GLTRACE(C,("gasnete_put_nbi\n"));
    GASNETC_LCHECK(LAPI_Setcntr(gasnetc_lapi_context, &o_cntr, 0));
    gasneti_suspend_spinpollers();
    /*printf("gasnete_put_nbi\n");*/
    result = gasnete_lapi_do_rdma(dest,node,src,nbytes,LAPI_RDMA_PUT, &o_cntr, op GASNETE_THREAD_GET);
    /*printf("gasnete_put_nbi do_rdma returned\n");*/
    gasneti_resume_spinpollers();
    /*printf("gasnete_put_nbi waiting on origin counter\n");*/
    GASNETC_LCHECK(LAPI_Waitcntr(gasnetc_lapi_context,result->origin_counter,result->num_transfers,&cur_cntr));
    /*printf("gasnete_put_nbi done\n");*/
    gasnete_op_free((gasnete_op_t *) result); 
    gasneti_assert(cur_cntr == 0);
#else
    GASNETC_LCHECK(LAPI_Setcntr(gasnetc_lapi_context, &o_cntr, 0));

    /* Issue as many puts as required.
     * Will generally only be one */
    gasneti_suspend_spinpollers();
    while (nbytes > 0) {
	ulong to_put = MIN(nbytes, gasnetc_max_lapi_data_size);
	/* use op lapi counter as completion counter,
	 * and o_cntr as origin counter */
	GASNETC_LCHECK(LAPI_Put(gasnetc_lapi_context, (unsigned int)node, to_put,
				dest,src,NULL,&o_cntr,&op->put_cntr));
	dest = (void*)((char*)dest + to_put);
	src = (void*)((char*)src + to_put);
	num_put++;
	nbytes -= to_put;
    }
    gasneti_resume_spinpollers();
    op->initiated_put_cnt += num_put;
    /* Client allowed to modify src data after return.  Make sure operation
     * is complete at origin
     */
    GASNETC_WAITCNTR(&o_cntr,num_put,&cur_cntr);
    gasneti_assert(cur_cntr == 0);

#if GASNETC_LAPI_FED_POLLBUG_WORKAROUND
    gasnete_wait_syncnbi_myputs(num_put GASNETE_THREAD_GET);
#endif    
#endif
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
#if GASNETC_LAPI_RDMA
    GASNETC_LCHECK(LAPI_Amsend(gasnetc_lapi_context, (unsigned int)node,
			       gasnete_remote_memset_hh[node],
			       &uhdr, sizeof(gasnete_memset_uhdr_t), NULL, 0,
			       NULL, &o_cntr, &(op->rdma_put_cntr)));
#else
    gasneti_suspend_spinpollers();
    GASNETC_LCHECK(LAPI_Amsend(gasnetc_lapi_context, (unsigned int)node,
			       gasnete_remote_memset_hh[node],
			       &uhdr, sizeof(gasnete_memset_uhdr_t), NULL, 0,
			       NULL, &o_cntr, &op->put_cntr));
    gasneti_resume_spinpollers();
#endif   
    op->initiated_put_cnt++;
    /* must insure operation has completed locally since uhdr is a stack variable.
     * This will ALMOST ALWAYS be true in the case of such a small message */
    GASNETC_WAITCNTR(&o_cntr,1,&cur_cntr);
    gasneti_assert(cur_cntr == 0);

#if GASNETC_LAPI_FED_POLLBUG_WORKAROUND
    gasnete_wait_syncnbi_myputs(1 GASNETE_THREAD_GET);
#endif    
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
        /*printf("try_syncnbi_gets: (%d,%d)\n",iop->initiated_get_cnt,cnt);*/
        if (iop->initiated_get_cnt == cnt) {
            if (cnt > 65000) { /* make sure we don't overflow the counters */
	      GASNETC_LCHECK(LAPI_Setcntr(gasnetc_lapi_context,&iop->get_cntr,0));
              iop->initiated_get_cnt = 0;
            }
	    gasneti_sync_reads();
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


#if GASNETC_LAPI_RDMA
        GLTRACE(C,("gasnete_try_syncnbi_puts\n"));
	GASNETC_LCHECK(LAPI_Getcntr(gasnetc_lapi_context,&iop->rdma_put_cntr,&cnt));
        /*printf("try_syncnbi_puts: (%d,%d)\n",iop->initiated_put_cnt,cnt);*/
        if(iop->initiated_put_cnt == cnt) {
	    gasneti_sync_reads();
            /*printf("try_syncnbi_puts: returning\n");*/
            return GASNET_OK;
        } else return GASNET_ERR_NOT_READY;
#else
	if (iop->initiated_put_cnt > 0) {
	    GASNETC_LCHECK(LAPI_Getcntr(gasnetc_lapi_context,&iop->put_cntr,&cnt));
	    gasneti_assert(cnt <= iop->initiated_put_cnt);
	}
        if (iop->initiated_put_cnt == cnt) {
            if (cnt > 65000) { /* make sure we don't overflow the counters */
	      GASNETC_LCHECK(LAPI_Setcntr(gasnetc_lapi_context,&iop->put_cntr,0));
              iop->initiated_put_cnt = 0;
            }
	    gasneti_sync_reads();
	    return GASNET_OK;
        } else return GASNET_ERR_NOT_READY;
#endif
    }
}

#if GASNETC_LAPI_FED_POLLBUG_WORKAROUND
/* don't poll for put operations, polling for gets is ok */
extern void gasnete_wait_syncnbi_puts(GASNETE_THREAD_FARG_ALONE) {
    gasnete_threaddata_t * const mythread = GASNETE_MYTHREAD;
    gasnete_iop_t *iop = mythread->current_iop;
    int cnt = 0;
    gasneti_assert(iop->threadidx == mythread->threadidx);
    gasneti_assert(iop->next == NULL);
    gasneti_assert(OPTYPE(iop) == OPTYPE_IMPLICIT);
#if GASNET_DEBUG
    if (iop->next != NULL)
	gasneti_fatalerror("VIOLATION: attempted to call gasnete_wait_syncnbi_puts() inside an NBI access region");
#endif
    if (iop->initiated_put_cnt > 0) {
	GASNETC_LCHECK(LAPI_Waitcntr(gasnetc_lapi_context,&iop->put_cntr,iop->initiated_put_cnt,&cnt));
	/* note that waitcntr decreemnts counter by amount waited for */
	gasneti_assert(cnt == 0);
	iop->initiated_put_cnt = 0;
	gasneti_sync_reads();
    } 
}

extern void gasnete_wait_syncnbi_all(GASNETE_THREAD_FARG_ALONE) {
    gasnete_wait_syncnbi_puts(GASNETE_THREAD_GET_ALONE);
    gasnete_wait_syncnbi_gets(GASNETE_THREAD_GET_ALONE);
}
#endif

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
static int volatile barrier_response_done[2] = { 0, 0 }; /*  non-zero when barrier is complete 
                                                             also has mismatch bit set if root detected mismatch */
#if GASNETI_STATS_OR_TRACE
static gasneti_stattime_t barrier_notifytime; /* for statistical purposes */ 
#endif

/*  global state on P0 */
#define GASNETE_BARRIER_MASTER (gasneti_nodes-1)
static gasneti_mutex_t barrier_lock = GASNETI_MUTEX_INITIALIZER;
static int volatile barrier_consensus_value[2]; /*  consensus barrier value */
static int volatile barrier_consensus_value_present[2] = { 0, 0 }; /*  consensus barrier value found */
static int volatile barrier_consensus_mismatch[2] = { 0, 0 }; /*  mismatch bit set if root detected a mismatch */
static int volatile barrier_count[2] = { 0, 0 }; /*  count of how many remotes have notified (on P0) */

/* LAPI Completion handler scheduled by HH on master node when the last node
 * has performed the notify.
 * Job is to send done message to all other nodes.
 */
void gasnete_lapi_barrier_ch(lapi_handle_t *context, void* user_info) {
    gasnete_barrier_uhdr_t * const uhdr = (gasnete_barrier_uhdr_t*)user_info;
    int i;
    #if GASNETE_BARRIER_USE_GFENCE
      /* when using Gfence, sender needs to ensure completion has run 
         (and broadcast any mismatches) before continuing
      */
      lapi_cntr_t comp_cntr;
      GASNETC_LCHECK(LAPI_Setcntr(gasnetc_lapi_context, &comp_cntr, 0));
      #define _GASNETE_BARRIER_COMPLETION &comp_cntr
    #else
      #define _GASNETE_BARRIER_COMPLETION NULL
    #endif

    /* Note: uhdr is a pointer to a static structure.  It will not
     * be used again until the next barrier has been reached by all
     * nodes (with the same phase).  At that point, all clients will
     * have had to received the active message.  This implies
     * the these AM calls will have completed locally.  That is,
     * we do not have to protect re-use of this uhdr by waiting
     * on a local counter variable.
     */
    #if GASNET_TRACE
      { const int phase = GASNETE_WIREFLAGS_PHASE(uhdr->wireflags);
        const int flags = GASNETE_WIREFLAGS_FLAGS(uhdr->wireflags);
        GASNETI_TRACE_PRINTF(B,("BARRIER_CH: phase %d, value %d, flags %d",
			        phase, uhdr->value, flags));
      }
    #endif

    /* inform all nodes (except local node) that barrier is complete */
    gasneti_suspend_spinpollers();
    for (i=0; i < gasneti_nodes; i++) {
	if ( i == gasneti_mynode ) continue;
	GASNETC_LCHECK(LAPI_Amsend(*context, (unsigned int)i,
				   gasnete_remote_barrier_hh[i],
				   uhdr, sizeof(gasnete_barrier_uhdr_t), NULL, 0,
				   NULL, NULL, _GASNETE_BARRIER_COMPLETION));
    }
    gasneti_resume_spinpollers();

    /* when using gfence mechanism, need to ensure everyone has acknowledged the mismatch
       before this completion handler can finish (and inform the remote mismatch 
       initiator to proceed with the Gfence)
    */
    #undef _GASNETE_BARRIER_COMPLETION
    #if GASNETE_BARRIER_USE_GFENCE
      { int cur_cntr = 0;          
        GASNETC_WAITCNTR(&comp_cntr,gasneti_nodes-1,&cur_cntr);
        gasneti_assert(cur_cntr == 0);
      }
    #endif
}

/* LAPI header handler to implement both notify and done requests on remote node */
void* gasnete_lapi_barrier_hh(lapi_handle_t *context, void *uhdr, uint *uhdr_len,
			      ulong *msg_len, compl_hndlr_t **comp_h, void **uinfo) {
    gasnete_barrier_uhdr_t const * const u = (gasnete_barrier_uhdr_t*)uhdr;
    const uint8_t wf = u->wireflags;
    const int value = u->value;
    const int phase = GASNETE_WIREFLAGS_PHASE(wf);
    const int is_notify = GASNETE_WIREFLAGS_ISNOTIFY(wf);
    #if GASNETE_BARRIER_USE_GFENCE
      int mismatch_firstdetect = 0;
    #endif

    GASNETI_TRACE_PRINTF(B,("BARRIER_HH: node %d, notify %d, phase %d, value %d, wireflags %d, g_val %d, g_count %d",
			    gasneti_mynode,is_notify,phase,value,GASNETE_WIREFLAGS_FLAGS(wf),
			    barrier_consensus_value[phase],barrier_count[phase]));

    *comp_h = NULL;
    *uinfo = NULL;

    if (is_notify) {
	/* this is a notify header handler call */
	gasneti_assert(gasneti_mynode == GASNETE_BARRIER_MASTER);

      #if GASNETE_BARRIER_BYPASS_LOOPBACK_AMSEND
        gasneti_mutex_lock(&barrier_lock);
      #else
	/* Don't need a lock here because header handlers are
	 * run by LAPI dispatcher and thus guaranteed to run one-at-a-time.
	 * Varibles read and updated here are only done so in this function.
	 */
      #endif
        {   const int flags = GASNETE_WIREFLAGS_FLAGS(wf);
	    const int count = barrier_count[phase] + 1;
	    if (!(flags & (GASNET_BARRIERFLAG_ANONYMOUS|GASNET_BARRIERFLAG_MISMATCH)) && 
                !barrier_consensus_value_present[phase]) { /* first named notify to arrive */
		barrier_consensus_value[phase] = value;
		barrier_consensus_value_present[phase] = 1;
	    } else if_pf ((flags & GASNET_BARRIERFLAG_MISMATCH) ||
		       (!(flags & GASNET_BARRIERFLAG_ANONYMOUS) 
                       && barrier_consensus_value[phase] != value)) { /* mismatch reported or detected */
                #if GASNETE_BARRIER_USE_GFENCE
                  if (barrier_consensus_mismatch[phase] == 0) /* mismatch first detected - broadcast it */
                    mismatch_firstdetect = 1;
                #endif
		barrier_consensus_mismatch[phase] = GASNET_BARRIERFLAG_MISMATCH;
	    }
	    barrier_count[phase] = count; /* update count */

          #if GASNETE_BARRIER_USE_GFENCE
            if (mismatch_firstdetect) {
          #else
	    if (count == gasneti_nodes) {
          #endif
		/* schedule completion handler to notify all nodes that barrier has been reached.
		 * use a static uhdr to avoid allocation cost, phased to ensure race-freedom
		 */
                static gasnete_barrier_uhdr_t barrier_uhdr[2] = /* init .phase, which never changes */
                  { {0, GASNETE_WIREFLAGS_SET(0, 0, 0)} , {0, GASNETE_WIREFLAGS_SET(0, 1, 0)} }; 
		gasnete_barrier_uhdr_t * const uch = (gasnete_barrier_uhdr_t*)&barrier_uhdr[phase];
                const int mismatch = barrier_consensus_mismatch[phase];
                const int wireflags = GASNETE_WIREFLAGS_SET(mismatch, phase, 0);

                #if !GASNETE_BARRIER_USE_GFENCE
	          gasneti_assert(phase == barrier_phase);
                #endif

		uch->value = barrier_consensus_value[phase];
                uch->wireflags = wireflags;
		*uinfo = (void*)uch;
		*comp_h = gasnete_lapi_barrier_ch;
		/* Perform local state update to get the effects of a local done handler  
                 * Note that the completion handler will not send an AM to this node
		 */
	        barrier_response_done[phase] = 1 | mismatch;

		GASNETI_TRACE_PRINTF(B,("BARRIER_HH: REACHED %d, mismatch %d, SCHEDULING CH",
					gasneti_nodes, 
                                        GASNETE_WIREFLAGS_FLAGS(uch->wireflags)&GASNET_BARRIERFLAG_MISMATCH));
	    }
	}
      #if GASNETE_BARRIER_BYPASS_LOOPBACK_AMSEND
        gasneti_mutex_unlock(&barrier_lock);
      #endif
    } else {
	/* this is a done header handler call... update local state */
        #if !GASNETE_BARRIER_USE_GFENCE
	  gasneti_assert(phase == barrier_phase);
        #endif

        /* local mismatch and done signals are folded into the same variable write,
           to avoid the need for a local write barrier here between the two writes */
	barrier_response_done[phase] = 1 | (GASNETE_WIREFLAGS_FLAGS(u->wireflags)&GASNET_BARRIERFLAG_MISMATCH);
    }
    return NULL;
}

extern void gasnete_barrier_notify(int id, int flags) {
  gasneti_sync_reads(); /* ensure we read correct barrier_splitstate */
  if_pf(barrier_splitstate == INSIDE_BARRIER) 
      gasneti_fatalerror("gasnet_barrier_notify() called twice in a row");

  GASNETI_TRACE_PRINTF(B, ("BARRIER_NOTIFY(id=%i,flags=%i)", id, flags));
#if GASNETI_STATS_OR_TRACE
  barrier_notifytime = GASNETI_STATTIME_NOW_IFENABLED(B);
#endif

  {
    const int phase = !barrier_phase; /*  enter new phase */
    barrier_phase = phase;
    barrier_value = id;
    barrier_flags = flags;

    if (gasneti_nodes > 1) {
    #if GASNETE_BARRIER_USE_GFENCE
      if (!(flags & GASNET_BARRIERFLAG_ANONYMOUS) ||
          (flags & GASNET_BARRIERFLAG_MISMATCH)) 
    #endif
     {
      /* use a static uhdr to avoid allocation cost, phased to ensure race-freedom */
      static gasnete_barrier_uhdr_t _uhdr[2]; 
      gasnete_barrier_uhdr_t * const uhdr = &_uhdr[phase];

      uhdr->value = barrier_value;
      uhdr->wireflags = GASNETE_WIREFLAGS_SET(flags, phase, 1);

      /*  send notify msg to master */
      /* tradeoff: we can check if mynode == GASNETE_BARRIER_MASTER and bypass some LAPI
         overhead for a loopback AM notification by invoking the header handler directly, 
         but this implies we need a lock in the header handler to ensure correctness
       */
    #if GASNETE_BARRIER_BYPASS_LOOPBACK_AMSEND
      if (gasneti_mynode == GASNETE_BARRIER_MASTER) {
        static uint uhdrlen = sizeof(gasnete_barrier_uhdr_t);
        static ulong msglen = 0;
        compl_hndlr_t *comp_h = NULL;
        void *uinfo = NULL;
        gasnete_lapi_barrier_hh(&gasnetc_lapi_context, uhdr, &uhdrlen,
                                &msglen, &comp_h, &uinfo);
        if (comp_h) (*comp_h)(&gasnetc_lapi_context, uinfo);
      } else 
    #endif
      {
        #if GASNETE_BARRIER_USE_GFENCE
          /* when using Gfence, sender needs to ensure completion has run 
             (and broadcast any mismatches) before continuing
          */
          lapi_cntr_t comp_cntr;
          GASNETC_LCHECK(LAPI_Setcntr(gasnetc_lapi_context, &comp_cntr, 0));
          #define _GASNETE_BARRIER_COMPLETION &comp_cntr
        #else
          #define _GASNETE_BARRIER_COMPLETION NULL
        #endif

        gasneti_suspend_spinpollers();
	GASNETC_LCHECK(LAPI_Amsend(gasnetc_lapi_context,
				   (unsigned int)GASNETE_BARRIER_MASTER,
				   gasnete_remote_barrier_hh[GASNETE_BARRIER_MASTER],
				   uhdr, sizeof(gasnete_barrier_uhdr_t), NULL, 0,
				   NULL, NULL, _GASNETE_BARRIER_COMPLETION));
        gasneti_resume_spinpollers();

        #undef _GASNETE_BARRIER_COMPLETION
        #if GASNETE_BARRIER_USE_GFENCE
          { int cur_cntr = 0;          
            GASNETC_WAITCNTR(&comp_cntr,1,&cur_cntr);
            gasneti_assert(cur_cntr == 0);
          }
        #endif
      }
     }

    #if GASNETE_BARRIER_USE_GFENCE
      LAPI_Gfence(gasnetc_lapi_context);
      barrier_response_done[phase] = 1 | barrier_response_done[phase];
    #endif

    } else {
	barrier_response_done[phase] = 1 | (flags & GASNET_BARRIERFLAG_MISMATCH);
    }

    /*  update state */
    barrier_splitstate = INSIDE_BARRIER;
    gasneti_sync_writes(); /* ensure all state changes committed before return */
  }
}


extern int gasnete_barrier_wait(int id, int flags) {
#if GASNETI_STATS_OR_TRACE
    gasneti_stattime_t wait_start = GASNETI_STATTIME_NOW_IFENABLED(B);
#endif
    int phase;
    gasneti_sync_reads(); /* ensure we read correct barrier_splitstate */
    phase = barrier_phase;
    if_pf(barrier_splitstate == OUTSIDE_BARRIER) 
	gasneti_fatalerror("gasnet_barrier_wait() called without a matching notify");

    GASNETI_TRACE_EVENT_TIME(B,BARRIER_NOTIFYWAIT,GASNETI_STATTIME_NOW()-barrier_notifytime);

    /*  wait for response */
    gasneti_polluntil(barrier_response_done[phase]);

    GASNETI_TRACE_EVENT_TIME(B,BARRIER_WAIT,GASNETI_STATTIME_NOW()-wait_start);

    /* if this is the master node, reset the global state */
    if (gasneti_mynode == GASNETE_BARRIER_MASTER) {
	barrier_count[phase] = 0;
	barrier_consensus_mismatch[phase] = 0;
	barrier_consensus_value_present[phase] = 0;
    }
    
    { const int global_mismatch = barrier_response_done[phase] & GASNET_BARRIERFLAG_MISMATCH;
      /*  update local state */
      barrier_splitstate = OUTSIDE_BARRIER;
      barrier_response_done[phase] = 0;
      gasneti_sync_writes(); /* ensure all state changes committed before return */
      if_pf((!(flags & GASNET_BARRIERFLAG_ANONYMOUS) && id != barrier_value) || /* local mismatch */
	    flags != barrier_flags || 
            global_mismatch) { /* global mismatch */
          return GASNET_ERR_BARRIER_MISMATCH;
      }
      else return GASNET_OK;
    }
}

extern int gasnete_barrier_try(int id, int flags) {
    gasneti_sync_reads(); /* ensure we read correct barrier_splitstate */
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
    { 0, NULL }
};

extern gasnet_handlerentry_t const *gasnete_get_handlertable() {
    return gasnete_handlers;
}

/* ------------------------------------------------------------------------------------ */

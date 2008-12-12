/*   $Source: /Users/kamil/work/gasnet-cvs2/gasnet/extended-ref/gasnet_extended_common.c,v $
 *     $Date: 2008/12/12 10:01:12 $
 * $Revision: 1.1.2.1 $
 * Description: GASNet Extended API Common code
 * Copyright 2002, Dan Bonachea <bonachea@cs.berkeley.edu>
 * Terms of use are as specified in license.txt
 */

GASNETI_IDENT(gasnete_IdentString_Version, "$GASNetExtendedLibraryVersion: " GASNET_EXTENDED_VERSION_STR " $");
GASNETI_IDENT(gasnete_IdentString_ExtendedName, "$GASNetExtendedLibraryName: " GASNET_EXTENDED_NAME_STR " $");

/* ------------------------------------------------------------------------------------ */
/*
  Thread Management
  =================
*/

#ifndef GASNETE_THREADING_CUSTOM /* top-level disable for all threading-related code */

gasnete_threaddata_t *gasnete_threadtable[GASNETI_MAX_THREADS] = { 0 };
static int gasnete_numthreads = 0;
int gasnete_maxthreadidx = 0;
static gasneti_mutex_t threadtable_lock = GASNETI_MUTEX_INITIALIZER;
#if GASNETI_CLIENT_THREADS
  /* pthread thread-specific ptr to our threaddata (or NULL for a thread never-seen before) */
  GASNETI_THREADKEY_DEFINE(gasnete_threaddata);
#endif

/* ------------------------------------------------------------------------------------ */
/* initing a thread's data upon thread discovery */

#ifndef GASNETE_INIT_THREADDATA
#define GASNETE_INIT_THREADDATA(thread) gasnete_init_threaddata(thread)
static void gasnete_init_threaddata(gasnete_threaddata_t *threaddata) {

  #ifndef GASNETE_NEW_THREADDATA_EOP_INIT
  #define GASNETE_NEW_THREADDATA_EOP_INIT(threaddata) \
          (threaddata)->eop_free = EOPADDR_NIL
  #endif
  GASNETE_NEW_THREADDATA_EOP_INIT(threaddata);

  #ifndef GASNETE_NEW_THREADDATA_IOP_INIT
  #define GASNETE_NEW_THREADDATA_IOP_INIT(threaddata) \
          (threaddata)->current_iop = gasnete_iop_new(threaddata)
  #endif
  GASNETE_NEW_THREADDATA_IOP_INIT(threaddata);

  /* give the conduit a chance to setup thread context via callbacks
     note gasnete_threaddata_t is zero-init, so only non-zero field inits are required
   */
  #ifdef GASNETC_NEW_THREADDATA_CALLBACK
    GASNETC_NEW_THREADDATA_CALLBACK(threaddata);
  #endif
  #ifdef GASNETE_NEW_THREADDATA_CALLBACK
    GASNETE_NEW_THREADDATA_CALLBACK(threaddata);
  #endif
}
#endif

/* ------------------------------------------------------------------------------------ */
/* freeing a thread's data upon thread exit */

#ifndef GASNETE_FREE_THREADDATA
#define GASNETE_FREE_THREADDATA(thread) gasnete_free_threaddata(thread)

#ifndef GASNETE_VALGET_CUSTOM
#define GASNETE_VALGET_FREEALL(thread) gasnete_valget_freeall(thread)
static void gasnete_valget_freeall(gasnete_threaddata_t *thread);
#endif

static void gasnete_free_threaddata(gasnete_threaddata_t *thread) {

  #ifndef GASNETE_IOP_ISDONE
  #define GASNETE_IOP_ISDONE(iop) gasnete_op_isdone((gasnete_op_t *)(iop))
  #endif

  #ifndef GASNETE_FREE_IOPS
  #define GASNETE_FREE_IOPS(thread) {                                           \
    /* active iop */                                                            \
    gasnete_iop_t *iop = thread->current_iop;                                   \
    gasneti_assert(iop->next == NULL); /* not inside an NBI access region */    \
    gasneti_assert(GASNETE_IOP_ISDONE(iop)); /* no outstanding NBI ops */       \
    gasneti_free(iop);                                                          \
                                                                                \
    /* iop free list */                                                         \
    iop = thread->iop_free;                                                     \
    while (iop) {                                                               \
      gasnete_iop_t *next = iop->next;                                          \
      gasneti_assert(GASNETE_IOP_ISDONE(iop)); /* active in free list == bad */ \
      gasneti_free(iop);                                                        \
      iop = next;                                                               \
    }                                                                           \
  }
  #endif
  GASNETE_FREE_IOPS(thread);

  #ifndef GASNETE_FREE_EOPS
  #define GASNETE_FREE_EOPS(thread) {            \
    /* TODO: check for in-flight eops */         \
    int i;                                       \
    for (i = 0; i < thread->eop_num_bufs; i++) { \
       gasneti_free(thread->eop_bufs[i]);        \
    }                                            \
  }
  #endif
  GASNETE_FREE_EOPS(thread);

  /* valgets */
  GASNETE_VALGET_FREEALL(thread);

  /* conduits needing additional cleanups should use gasnete_register_threadcleanup */

  /* threaddata itself */
  gasneti_free(thread);
}
#endif

/* ------------------------------------------------------------------------------------ */
/* thread table management */

#ifndef GASNETE_THREAD_CLEANUP_DELAY
#define GASNETE_THREAD_CLEANUP_DELAY 1 /* number of times to postpone pthread_setspecific callback */
#endif

static pthread_key_t gasnete_threaddata_cleanup;
static void gasnete_threaddata_cleanup_fn(void *_thread) {
  gasnete_threaddata_t *thread = _thread;
  int idx = thread->threadidx;

  #if GASNETI_CLIENT_THREADS
    /* ensure gasnete_threaddata remains valid for this thread while we run destructors */
    if (!gasneti_threadkey_get(gasnete_threaddata))
      gasneti_threadkey_set(gasnete_threaddata, thread);

    /* postpone destructor for specified iterations, to support GASNet client destructors */
    if (thread->thread_cleanup_delay < GASNETE_THREAD_CLEANUP_DELAY) {
      thread->thread_cleanup_delay++;
      pthread_setspecific(gasnete_threaddata_cleanup, thread);
      return;
    }
  #endif

  GASNETI_TRACE_PRINTF(C,("thread %i exiting\n", idx));
  /* run cleanups in LIFO order, with multiple passes to catch registrations within cleanupfns */
  { struct _gasnete_thread_cleanup *cleanuplist;
    while ((cleanuplist = thread->thread_cleanup) != NULL) {
      struct _gasnete_thread_cleanup *nextcleanup;
      thread->thread_cleanup = NULL;
      while ((nextcleanup = cleanuplist) != NULL) {
        cleanuplist = nextcleanup->next;
        nextcleanup->cleanupfn(nextcleanup->context);
        gasneti_free(nextcleanup);
      }
    }
  }

  GASNETE_FREE_THREADDATA(thread);

  gasneti_mutex_lock(&threadtable_lock);
    gasnete_threadtable[idx] = NULL;
    gasnete_numthreads--;
  gasneti_mutex_unlock(&threadtable_lock);
}

extern void gasnete_register_threadcleanup(void (*cleanupfn)(void *), void *context) {
  gasnete_threaddata_t * const thread = gasnete_mythread();
  struct _gasnete_thread_cleanup *cleanup = gasneti_malloc(sizeof(struct _gasnete_thread_cleanup));
  cleanup->cleanupfn = cleanupfn;
  cleanup->context = context;
  cleanup->next = thread->thread_cleanup;
  thread->thread_cleanup = cleanup;
}

static gasnete_threaddata_t * gasnete_new_threaddata() {
  gasnete_threaddata_t *threaddata = NULL;
  int idx;
  gasneti_assert(GASNETI_MAX_THREADS <= (1U<<(sizeof(gasnete_threadidx_t)*8)));

  gasneti_mutex_lock(&threadtable_lock);
    gasnete_numthreads++;
    #if GASNETI_CLIENT_THREADS
      if (gasnete_numthreads >= GASNETI_MAX_THREADS) 
        gasneti_fatalerror("GASNet Extended API: Too many simultaneous local client threads (limit=%i)",GASNETI_MAX_THREADS);
    #endif
    /* find a free slot */
    if (gasnete_threadtable[gasnete_numthreads-1] == NULL) idx = gasnete_numthreads-1;
    else { /* keep table somewhat compacted */
      for (idx = 0; idx < GASNETI_MAX_THREADS; idx++) {
        if (gasnete_threadtable[idx] == NULL) break;
      }
    }
    gasneti_assert(idx < GASNETI_MAX_THREADS && gasnete_threadtable[idx] == NULL);
    if (idx > gasnete_maxthreadidx) gasnete_maxthreadidx = idx;
    #if GASNETI_CLIENT_THREADS
    { static int keycreated = 0;
      if (!keycreated) {
        pthread_key_create(&gasnete_threaddata_cleanup, &gasnete_threaddata_cleanup_fn);
        keycreated = 1;
      }
    }
    #endif
  gasneti_mutex_unlock(&threadtable_lock);

  threaddata = (gasnete_threaddata_t *)gasneti_calloc(1,sizeof(gasnete_threaddata_t));

  threaddata->threadidx = idx;
  gasnete_threadtable[idx] = threaddata;

  #if GASNETI_CLIENT_THREADS
    /* setup TLS identification */
    gasneti_assert(gasneti_threadkey_get(gasnete_threaddata) == NULL);
    gasneti_threadkey_set(gasnete_threaddata, threaddata);

    /* register pthread destructor */
    pthread_setspecific(gasnete_threaddata_cleanup, threaddata);
  #endif

  GASNETE_INIT_THREADDATA(threaddata);

  return threaddata;
}
/* PURE function (returns same value for a given thread every time) 
*/
#if GASNETI_CLIENT_THREADS && !defined(gasnete_mythread)
  extern gasnete_threaddata_t *gasnete_mythread() {
    gasnete_threaddata_t *threaddata = gasneti_threadkey_get(gasnete_threaddata);
    GASNETI_STAT_EVENT(C, DYNAMIC_THREADLOOKUP); /* tracing here can cause inf recursion */
    if_pt (threaddata) {
      gasneti_memcheck(threaddata);
      return threaddata;
    }

    /* first time we've seen this thread - need to set it up */
    threaddata = gasnete_new_threaddata();
    gasneti_memcheck(threaddata);
    GASNETI_TRACE_PRINTF(C,("gasnete_new_threaddata: idx=%i, numthreads=%i", threaddata->threadidx, gasnete_numthreads));
    return threaddata;
  }
#endif

#endif /* GASNETE_THREADING_CUSTOM  */

/* ------------------------------------------------------------------------------------ */
/*
  Non-Blocking Value Get (explicit-handle)
  ========================================
*/
#ifndef GASNETE_VALGET_CUSTOM
typedef struct _gasnete_valget_op_t {
  gasnet_handle_t handle;
  gasnet_register_value_t val;

  struct _gasnete_valget_op_t* next; /* for free-list only */
  gasnete_threadidx_t threadidx;  /*  thread that owns me */
} gasnete_valget_op_t;

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
    retval = (gasnete_valget_op_t*)gasneti_malloc(sizeof(gasnete_valget_op_t));
    retval->threadidx = mythread->threadidx;
  }

  retval->val = 0;
  if (gasnete_islocal(node)) {
    GASNETE_FAST_ALIGNED_MEMCPY(GASNETE_STARTOFBITS(&(retval->val),nbytes), src, nbytes);
    retval->handle = GASNET_INVALID_HANDLE;
  } else {
    #ifndef GASNETE_VALGET_GETOP
    #define GASNETE_VALGET_GETOP gasnete_get_nb
    #endif
    retval->handle = GASNETE_VALGET_GETOP(GASNETE_STARTOFBITS(&(retval->val),nbytes), node, src, nbytes GASNETE_THREAD_PASS);
  }
  return retval;
}

extern gasnet_register_value_t gasnete_wait_syncnb_valget(gasnet_valget_handle_t handle) {
  gasnete_assert_valid_threadid(handle->threadidx);
  { gasnete_threaddata_t * const thread = gasnete_threadtable[handle->threadidx];
    gasnet_register_value_t val;
    gasneti_assert(thread == gasnete_mythread());
    handle->next = thread->valget_free; /* free before the wait to save time after the wait, */
    thread->valget_free = handle;       /*  safe because this thread is under our control */

    gasnete_wait_syncnb(handle->handle);
    val = handle->val;
    return val;
  }
}

static void gasnete_valget_freeall(gasnete_threaddata_t *thread) {
  gasnete_valget_op_t *vg = thread->valget_free;
  while (vg) {
    gasnete_valget_op_t *next = vg->next;
    gasneti_free(vg);  
    vg = next;
  }
}
#endif
/* ------------------------------------------------------------------------------------ */

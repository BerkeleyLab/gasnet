/*  $Archive:: /Ti/GASNet/extended-ref/gasnet_extended.c                  $
 *     $Date: 2003/08/30 07:16:53 $
 * $Revision: 1.7 $
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
#ifdef GASNETI_THREADS
  static pthread_key_t gasnete_threaddata; /*  pthread thread-specific ptr to our threaddata (or NULL for a thread never-seen before) */
#endif
static const gasnete_eopaddr_t EOPADDR_NIL = { { 0xFF, 0xFF } };

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
  #ifdef GASNETI_THREADS
    if (idx >= 256) gasneti_fatalerror("GASNet Extended API: Too many local client threads (limit=256)");
  #else
    assert(idx == 0);
  #endif
  assert(gasnete_threadtable[idx] == NULL);

  threaddata = (gasnete_threaddata_t *)gasneti_calloc(1,sizeof(gasnete_threaddata_t));

  threaddata->threadidx = idx;
  threaddata->eop_free = EOPADDR_NIL;

  gasnete_threadtable[idx] = threaddata;
  threaddata->default_iop = gasnete_iop_new(threaddata);
  threaddata->current_iop = threaddata->default_iop;

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
  Op management
  =============
*/
/*  get a new explicit op */
gasnete_eop_t *gasnete_eop_new(gasnete_threaddata_t * const thread) {
  gasnete_eopaddr_t head = thread->eop_free;
  if_pt (!gasnete_eopaddr_isnil(head)) {
    gasnete_eop_t *eop = GASNETE_EOPADDR_TO_PTR(thread, head);
    thread->eop_free = eop->addr;
    eop->addr = head;
    assert(!gasnete_eopaddr_equal(thread->eop_free,head));
    assert(eop->threadidx == thread->threadidx);
    assert(eop->type == gasnete_opExplicit);
    assert(gasneti_atomic_read(&(eop->req_oust)) == 0);
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
      #if 0 /* this can safely be skipped when values are zero */
        buf[i].type = gasnete_opExplicit; 
        gasneti_atomic_set(&(buf[i].req_oust), 0);
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
        assert(eop->type == gasnete_opExplicit);               
        assert(gasneti_atomic_read(&(eop->req_oust)) == 0);
        assert(eop->threadidx == threadidx);                  
        assert(addr.bufferidx == bufidx);
        assert(!seen[addr.eopidx]);/* see if we hit a cycle */
        seen[addr.eopidx] = 1;
        addr = eop->addr;                                     
      }                                                       
      assert(gasnete_eopaddr_isnil(addr)); 
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
    assert(iop->type == gasnete_opImplicit);
    assert(iop->threadidx == thread->threadidx);
    assert(gasneti_atomic_read(&(iop->get_req_oust)) == 0);
    assert(gasneti_atomic_read(&(iop->put_req_oust)) == 0);
  } else {
    iop = (gasnete_iop_t *)gasneti_malloc(sizeof(gasnete_iop_t));
    iop->type = gasnete_opImplicit;
    iop->threadidx = thread->threadidx;
    gasneti_atomic_set(&(iop->get_req_oust), 0);
    gasneti_atomic_set(&(iop->put_req_oust), 0);
  }
  iop->next = NULL;
  return iop;
}

GASNET_INLINE_MODIFIER(gasnete_eop_free)
void gasnete_eop_free(gasnete_eop_t *eop) {
  gasnete_threaddata_t * const thread = gasnete_threadtable[eop->threadidx];
  gasnete_eopaddr_t addr = eop->addr;
  assert(thread == gasnete_mythread());
  assert(eop->type == gasnete_opExplicit);
  assert(gasneti_atomic_read(&(eop->req_oust)) == 0);
  eop->addr = thread->eop_free;
  thread->eop_free = addr;
}

GASNET_INLINE_MODIFIER(gasnete_iop_free)
void gasnete_iop_free(gasnete_iop_t *iop) {
  gasnete_threaddata_t * const thread = gasnete_threadtable[iop->threadidx];
  assert(thread == gasnete_mythread());
  assert(iop->type == gasnete_opImplicit);
  assert(gasneti_atomic_read(&(iop->get_req_oust)) == 0);
  assert(gasneti_atomic_read(&(iop->put_req_oust)) == 0);
  iop->next = thread->iop_free;
  thread->iop_free = iop;
}

/* query an eop for completeness */
GASNET_INLINE_MODIFIER(gasnete_eop_test)
int gasnete_eop_test(gasnete_eop_t *eop) {
  assert (eop->type == gasnete_opExplicit);
  return gasnetc_counter_test(&eop->req_oust);
}

/* query an iop for completeness - this means both puts and gets */
GASNET_INLINE_MODIFIER(gasnete_iop_test)
int gasnete_iop_test(gasnete_iop_t *iop) {
  assert (iop->type == gasnete_opImplicit);
  return (gasnetc_counter_test(&(iop->get_req_oust)) && gasnetc_counter_test(&(iop->put_req_oust)));
}

/*  query an op for completeness 
 *  free it if complete
 *  returns 0 or 1 */
int gasnete_op_try_free(gasnet_handle_t handle) {
  gasnete_op_t *op = (gasnete_op_t *)handle;

  assert(op->threadidx == gasnete_mythread()->threadidx);
  if_pt (op->type == gasnete_opExplicit) {
    gasnete_eop_t *eop = (gasnete_eop_t*)op;

    if (gasnete_eop_test(eop)) {
      gasnete_eop_free(eop);
      return 1;
    }
    return 0;
  } else {
    gasnete_iop_t *iop = (gasnete_iop_t*)op;

    if (gasnete_iop_test(iop)) {
      gasnete_iop_free(iop);
      return 1;
    }
    return 0;
  }
}

/*  query an op for completeness 
 *  free it and clear the handle if complete
 *  returns 0 or 1 */
int gasnete_op_try_free_clear(gasnet_handle_t *handle_p) {
  gasnete_op_t *op = (gasnete_op_t *)(*handle_p);

  assert(op->threadidx == gasnete_mythread()->threadidx);
  if_pt (op->type == gasnete_opExplicit) {
    gasnete_eop_t *eop = (gasnete_eop_t*)op;

    if (gasnete_eop_test(eop)) {
      gasnete_eop_free(eop);
      *handle_p = GASNET_INVALID_HANDLE;
      return 1;
    }
    return 0;
  } else {
    gasnete_iop_t *iop = (gasnete_iop_t*)op;

    if (gasnete_iop_test(iop)) {
      gasnete_iop_free(iop);
      *handle_p = GASNET_INVALID_HANDLE;
      return 1;
    }
    return 0;
  }
}

/* Reply handler to complete an op - might be replaced w/ IB atomics one day */
GASNET_INLINE_MODIFIER(gasnete_done_reph_inner)
void gasnete_done_reph_inner(gasnet_token_t token, void *counter) {
  assert(gasneti_atomic_read((gasneti_atomic_t *)counter) > 0);
  gasneti_atomic_decrement((gasneti_atomic_t *)counter);
}
SHORT_HANDLER(gasnete_done_reph,1,2,
              (token, UNPACK(a0)    ),
              (token, UNPACK2(a0, a1)));
#define GASNETE_DONE(token, counter)                                               \
  GASNETE_SAFE(                                                                    \
    SHORT_REP(1,2,((token), gasneti_handleridx(gasnete_done_reph), PACK(counter))) \
  )

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

  { gasnete_threaddata_t *threaddata = NULL;
    #ifdef GASNETI_THREADS
      /* register first thread (optimization) */
      threaddata = gasnete_mythread(); 
    #else
      /* register only thread (required) */
      threaddata = gasnete_new_threaddata();
      gasnete_threadtable[0] = threaddata;
    #endif

    /* cause the first pool of eops to be allocated (optimization) */
    gasnete_eop_free(gasnete_eop_new(threaddata));
  }
}

/* ------------------------------------------------------------------------------------ */
/*
  Non-blocking memory-to-memory transfers (explicit handle)
  ==========================================================
*/
/* ------------------------------------------------------------------------------------ */
GASNET_INLINE_MODIFIER(gasnete_getmed_reqh_inner)
void gasnete_getmed_reqh_inner(gasnet_token_t token, 
  gasnet_handlerarg_t nbytes, void *dest, void *src, void *counter) {
  assert(nbytes <= gasnet_AMMaxMedium());
  GASNETE_SAFE(
    MEDIUM_REP(2,4,(token, gasneti_handleridx(gasnete_getmed_reph),
                  src, nbytes, 
                  PACK(dest), PACK(counter))));
}
SHORT_HANDLER(gasnete_getmed_reqh,4,7, 
              (token, a0, UNPACK(a1),      UNPACK(a2),      UNPACK(a3)     ),
              (token, a0, UNPACK2(a1, a2), UNPACK2(a3, a4), UNPACK2(a5, a6)));
/* ------------------------------------------------------------------------------------ */
GASNET_INLINE_MODIFIER(gasnete_getmed_reph_inner)
void gasnete_getmed_reph_inner(gasnet_token_t token, 
  void *addr, size_t nbytes,
  void *dest, void *counter) {
  GASNETE_FAST_UNALIGNED_MEMCPY(dest, addr, nbytes);
  gasneti_memsync();
  gasnete_done_reph_inner(token, counter);
}
MEDIUM_HANDLER(gasnete_getmed_reph,2,4,
              (token,addr,nbytes, UNPACK(a0),      UNPACK(a1)    ),
              (token,addr,nbytes, UNPACK2(a0, a1), UNPACK2(a2, a3)));
/* ------------------------------------------------------------------------------------ */
GASNET_INLINE_MODIFIER(gasnete_getlong_reqh_inner)
void gasnete_getlong_reqh_inner(gasnet_token_t token, 
  gasnet_handlerarg_t nbytes, void *dest, void *src, void *counter) {
  int rc;
  gasnet_node_t node;

  rc = gasnet_AMGetMsgSource(token, &node);
  assert(rc != 0);
  assert(node != gasnete_mynode);	/* loopback gets should not reach here */

  /* This is more-or-less a AMReplyLongAsync, except that the reply handler
   * doesn't get the "nbytes" and "addr" (they are not needed in this case).
   * XXX: Consider implementing a gasnetc_AMReplyLongAsyncM() as in gm-conduit.
   *      I have not bothered yet, since this is the only place it is used.
   *
   * The semantics allow this handler to return as soon as both the RDMA put
   * and the AM for the "done" have been queued.  Ordering ensures that those
   * will be delivered in the proper order.  There is nothing in the GASNet
   * spec that ensures that the "get" will read the memory atomically with
   * respect to the running of handlers, so we are permitted the "bulk put"
   * semantic in which we don't wait for the "safe reuse" of the source memory
   * before returning, and possibly running the next handler.
   */
  gasnetc_rdma_put(node, src, dest, nbytes, NULL, NULL);
  GASNETE_DONE(token, counter);
}
SHORT_HANDLER(gasnete_getlong_reqh,4,7, 
              (token, a0, UNPACK(a1),      UNPACK(a2),      UNPACK(a3)     ),
              (token, a0, UNPACK2(a1, a2), UNPACK2(a3, a4), UNPACK2(a5, a6)));
/* ------------------------------------------------------------------------------------ */
GASNET_INLINE_MODIFIER(gasnete_put_reqh_inner)
void gasnete_put_reqh_inner(gasnet_token_t token, 
  void *addr, size_t nbytes, void *dest, void *counter) {
  GASNETE_FAST_UNALIGNED_MEMCPY(dest, addr, nbytes);
  gasneti_memsync();
  GASNETE_DONE(token, counter);
}
MEDIUM_HANDLER(gasnete_put_reqh,2,4, 
               (token,addr,nbytes, UNPACK(a0),      UNPACK(a1)     ),
               (token,addr,nbytes, UNPACK2(a0, a1), UNPACK2(a2, a3)));
/* ------------------------------------------------------------------------------------ */
GASNET_INLINE_MODIFIER(gasnete_memset_reqh_inner)
void gasnete_memset_reqh_inner(gasnet_token_t token, 
  gasnet_handlerarg_t val, gasnet_handlerarg_t nbytes, void *dest, void *counter) {
  memset(dest, (int)(uint32_t)val, nbytes);
  gasneti_memsync();
  GASNETE_DONE(token, counter);
}
SHORT_HANDLER(gasnete_memset_reqh,4,6,
              (token, a0, a1, UNPACK(a2),      UNPACK(a3)     ),
              (token, a0, a1, UNPACK2(a2, a3), UNPACK2(a4, a5)));
/* ------------------------------------------------------------------------------------ */

extern gasnet_handle_t gasnete_get_nb_bulk (void *dest, gasnet_node_t node, void *src, size_t nbytes GASNETE_THREAD_FARG) {
  gasnete_eop_t *eop = gasnete_eop_new(GASNETE_MYTHREAD);

  /* XXX check error returns */
  gasnetc_rdma_get(node, src, dest, nbytes, &eop->req_oust);

  return (gasnet_handle_t)eop;
}

extern gasnet_handle_t gasnete_put_nb      (gasnet_node_t node, void *dest, void *src, size_t nbytes GASNETE_THREAD_FARG) {
  gasnete_eop_t *eop = gasnete_eop_new(GASNETE_MYTHREAD);
  gasneti_atomic_t mem_oust = gasneti_atomic_init(0);

  /* XXX check error returns */
  gasnetc_rdma_put(node, src, dest, nbytes, &mem_oust, &eop->req_oust);
  gasnetc_counter_wait(&mem_oust, 0);

  return (gasnet_handle_t)eop;
}

extern gasnet_handle_t gasnete_put_nb_bulk (gasnet_node_t node, void *dest, void *src, size_t nbytes GASNETE_THREAD_FARG) {
  gasnete_eop_t *eop = gasnete_eop_new(GASNETE_MYTHREAD);

  /* XXX check error returns */
  gasnetc_rdma_put(node, src, dest, nbytes, NULL, &eop->req_oust);

  return (gasnet_handle_t)eop;
}

extern gasnet_handle_t gasnete_memset_nb   (gasnet_node_t node, void *dest, int val, size_t nbytes GASNETE_THREAD_FARG) {
  gasnete_eop_t *eop = gasnete_eop_new(GASNETE_MYTHREAD);

  if ((GASNETE_MEMSET_PUT_LIMIT != 0 ) && (nbytes <= GASNETE_MEMSET_PUT_LIMIT)) {
    /* XXX check error returns */
    gasnetc_rdma_memset(node, dest, val, nbytes, &eop->req_oust);
  } else {
    gasneti_atomic_increment(&eop->req_oust);
    GASNETE_SAFE(
      SHORT_REQ(4,6,(node, gasneti_handleridx(gasnete_memset_reqh),
                   (gasnet_handlerarg_t)val, (gasnet_handlerarg_t)nbytes,
                   PACK(dest), PACK(&eop->req_oust))));
  } 

  return (gasnet_handle_t)eop;
}

/* ------------------------------------------------------------------------------------ */
/*
  Synchronization for explicit-handle non-blocking operations:
  ===========================================================
  
  Note that, other than gasnete_wait_syncnb, these routines do not check for INVALID_HANDLE
*/

/* Note that the handle might actually be an IMPLICIT one! */
extern void gasnete_wait_syncnb(gasnet_handle_t op) {
  if_pt (op != GASNET_INVALID_HANDLE) {
    assert(op->threadidx == gasnete_mythread()->threadidx);
    if_pt (op->type == gasnete_opExplicit) {
      gasnete_eop_t *eop = (gasnete_eop_t*)op;
      gasnetc_counter_wait(&eop->req_oust, 0);
      gasnete_eop_free(eop);
    } else {
      gasnete_iop_t *iop = (gasnete_iop_t*)op;
      gasnetc_counter_wait(&iop->get_req_oust, 0);
      gasnetc_counter_wait(&iop->put_req_oust, 0);
      gasnete_iop_free(iop);
    }
  }
}

extern int  gasnete_try_syncnb(gasnet_handle_t handle) {
  GASNETE_SAFE(gasnet_AMPoll());

  return gasnete_op_try_free(handle) ? GASNET_OK : GASNET_ERR_NOT_READY;
}

extern int  gasnete_try_syncnb_some (gasnet_handle_t *phandle, size_t numhandles) {
  int success = 0;
  int empty = 1;

  GASNETE_SAFE(gasnet_AMPoll());

  assert(phandle);

  { int i;
    for (i = 0; i < numhandles; i++) {
      if (phandle[i] != GASNET_INVALID_HANDLE) {
        empty = 0;
	success |= gasnete_op_try_free_clear(&phandle[i]);
      }
    }
  }

  return (success || empty) ? GASNET_OK : GASNET_ERR_NOT_READY;
}

extern int  gasnete_try_syncnb_all (gasnet_handle_t *phandle, size_t numhandles) {
  int success = 1;

  GASNETE_SAFE(gasnet_AMPoll());

  assert(phandle);

  { int i;
    for (i = 0; i < numhandles; i++) {
      if (phandle[i] != GASNET_INVALID_HANDLE) {
        success &= gasnete_op_try_free_clear(&phandle[i]);
      }
    }
  }

  return success ? GASNET_OK : GASNET_ERR_NOT_READY;
}

/* ------------------------------------------------------------------------------------ */
/*
  Non-blocking memory-to-memory transfers (implicit handle)
  ==========================================================
*/

extern void gasnete_get_nbi_bulk (void *dest, gasnet_node_t node, void *src, size_t nbytes GASNETE_THREAD_FARG) {
  gasnete_threaddata_t * const mythread = GASNETE_MYTHREAD;
  gasnete_iop_t *iop = mythread->current_iop;

  /* XXX check error returns */ 
  gasnetc_rdma_get(node, src, dest, nbytes, &iop->get_req_oust);
}

extern void gasnete_put_nbi (gasnet_node_t node, void *dest, void *src, size_t nbytes GASNETE_THREAD_FARG) {
  
  gasnete_threaddata_t * const mythread = GASNETE_MYTHREAD;
  gasnete_iop_t *iop = mythread->current_iop;
  gasneti_atomic_t mem_oust = gasneti_atomic_init(0);

  /* XXX check error returns */ 
  gasnetc_rdma_put(node, src, dest, nbytes, &mem_oust, &iop->put_req_oust);
  gasnetc_counter_wait(&mem_oust, 0);
}

extern void gasnete_put_nbi_bulk (gasnet_node_t node, void *dest, void *src, size_t nbytes GASNETE_THREAD_FARG) {
  gasnete_threaddata_t * const mythread = GASNETE_MYTHREAD;
  gasnete_iop_t *iop = mythread->current_iop;

  /* XXX check error returns */ 
  gasnetc_rdma_put(node, src, dest, nbytes, NULL, &iop->put_req_oust);
}

extern void gasnete_memset_nbi   (gasnet_node_t node, void *dest, int val, size_t nbytes GASNETE_THREAD_FARG) {
  gasnete_threaddata_t * const mythread = GASNETE_MYTHREAD;
  gasnete_iop_t *iop = mythread->current_iop;

  if ((GASNETE_MEMSET_PUT_LIMIT != 0 ) && (nbytes <= GASNETE_MEMSET_PUT_LIMIT)) {
    /* XXX check error returns */
    gasnetc_rdma_memset(node, dest, val, nbytes, &iop->put_req_oust);
  } else {
    gasneti_atomic_increment(&iop->put_req_oust);
    GASNETE_SAFE(
      SHORT_REQ(4,6,(node, gasneti_handleridx(gasnete_memset_reqh),
                   (gasnet_handlerarg_t)val, (gasnet_handlerarg_t)nbytes,
                   PACK(dest), PACK(&iop->put_req_oust))));
  } 
}

/* ------------------------------------------------------------------------------------ */
/*
  Synchronization for implicit-handle non-blocking operations:
  ===========================================================
*/

extern int  gasnete_try_syncnbi_gets(GASNETE_THREAD_FARG_ALONE) {
  gasnete_threaddata_t * const mythread = GASNETE_MYTHREAD;
  gasnete_iop_t *iop = mythread->current_iop;
  assert(iop->threadidx == mythread->threadidx);
  assert(iop->type == gasnete_opImplicit);
  #ifdef DEBUG
    if (iop != mythread->default_iop)
      gasneti_fatalerror("VIOLATION: attempted to call gasnete_try_syncnbi_gets() inside an NBI access region");
  #endif

  return gasnetc_counter_test(&iop->get_req_oust) ? GASNET_OK: GASNET_ERR_NOT_READY;
}

extern int  gasnete_try_syncnbi_puts(GASNETE_THREAD_FARG_ALONE) {
  gasnete_threaddata_t * const mythread = GASNETE_MYTHREAD;
  gasnete_iop_t *iop = mythread->current_iop;
  assert(iop->threadidx == mythread->threadidx);
  assert(iop->type == gasnete_opImplicit);
  #ifdef DEBUG
    if (iop != mythread->default_iop)
      gasneti_fatalerror("VIOLATION: attempted to call gasnete_try_syncnbi_puts() inside an NBI access region");
  #endif

  return gasnetc_counter_test(&iop->put_req_oust) ? GASNET_OK: GASNET_ERR_NOT_READY;
}

extern void gasnete_wait_syncnbi_gets(GASNETE_THREAD_FARG_ALONE) {
  gasnete_threaddata_t * const mythread = GASNETE_MYTHREAD;
  gasnete_iop_t *iop = mythread->current_iop;
  assert(iop->threadidx == mythread->threadidx);
  assert(iop->type == gasnete_opImplicit);
  #ifdef DEBUG
    if (iop != mythread->default_iop)
      gasneti_fatalerror("VIOLATION: attempted to call gasnete_wait_syncnbi_gets() inside an NBI access region");
  #endif

  gasnetc_counter_wait(&iop->get_req_oust, 0);
}

extern void gasnete_wait_syncnbi_puts(GASNETE_THREAD_FARG_ALONE) {
  gasnete_threaddata_t * const mythread = GASNETE_MYTHREAD;
  gasnete_iop_t *iop = mythread->current_iop;
  assert(iop->threadidx == mythread->threadidx);
  assert(iop->type == gasnete_opImplicit);
  #ifdef DEBUG
    if (iop != mythread->default_iop)
      gasneti_fatalerror("VIOLATION: attempted to call gasnete_wait_syncnbi_puts() inside an NBI access region");
  #endif

  gasnetc_counter_wait(&iop->put_req_oust, 0);
}

/* ------------------------------------------------------------------------------------ */
/*
  Implicit access region synchronization
  ======================================
*/
extern void            gasnete_begin_nbi_accessregion(int allowrecursion GASNETE_THREAD_FARG) {
  gasnete_threaddata_t * const mythread = GASNETE_MYTHREAD;
  gasnete_iop_t *iop = gasnete_iop_new(mythread);
  GASNETI_TRACE_PRINTF(S,("BEGIN_NBI_ACCESSREGION"));
  #ifdef DEBUG
    if (mythread->current_iop != mythread->default_iop)
      gasneti_fatalerror("VIOLATION: tried to initiate a recursive NBI access region");
  #endif
  mythread->current_iop = iop;
}

extern gasnet_handle_t gasnete_end_nbi_accessregion(GASNETE_THREAD_FARG_ALONE) {
  gasnete_threaddata_t * const mythread = GASNETE_MYTHREAD;
  gasnete_iop_t *iop = mythread->current_iop;
  GASNETI_TRACE_EVENT_VAL(S,END_NBI_ACCESSREGION,gasneti_atomic_read(&iop->get_req_oust) + gasneti_atomic_read(&iop->put_req_oust));
  #ifdef DEBUG
    if (iop == mythread->default_iop)
      gasneti_fatalerror("VIOLATION: call to gasnete_end_nbi_accessregion() outside access region");
  #endif
  mythread->current_iop = mythread->default_iop;
  return (gasnet_handle_t)iop;
}

/* ------------------------------------------------------------------------------------ */
/*
  Blocking memory-to-memory transfers
  ===================================
*/

extern void gasnete_get_bulk (void *dest, gasnet_node_t node, void *src,
			      size_t nbytes GASNETE_THREAD_FARG) {
  gasneti_atomic_t req_oust = gasneti_atomic_init(0);
  gasnetc_rdma_get(node, src, dest, nbytes, &req_oust);
  gasnetc_counter_wait(&req_oust, 0);
}

extern void gasnete_put_bulk (gasnet_node_t node, void* dest, void *src,
			      size_t nbytes GASNETE_THREAD_FARG) {
  gasneti_atomic_t req_oust = gasneti_atomic_init(0);
  gasnetc_rdma_put(node, src, dest, nbytes, NULL, &req_oust);
  gasnetc_counter_wait(&req_oust, 0);
}   

extern void gasnete_memset (gasnet_node_t node, void *dest, int val,
		            size_t nbytes GASNETE_THREAD_FARG) {
  gasneti_atomic_t req_oust = gasneti_atomic_init(0);

  if ((GASNETE_MEMSET_PUT_LIMIT != 0 ) && (nbytes <= GASNETE_MEMSET_PUT_LIMIT)) {
    /* XXX check error returns */
    gasnetc_rdma_memset(node, dest, val, nbytes, &req_oust);
  } else {
    gasneti_atomic_increment(&req_oust);
    GASNETE_SAFE(
      SHORT_REQ(4,6,(node, gasneti_handleridx(gasnete_memset_reqh),
                   (gasnet_handlerarg_t)val, (gasnet_handlerarg_t)nbytes,
                   PACK(dest), PACK(&req_oust))));
  } 

  gasnetc_counter_wait(&req_oust, 0);
}
/* ------------------------------------------------------------------------------------ */
/*
  Non-Blocking Value Get (explicit-handle)
  ========================================
*/
typedef struct _gasnet_valget_op_t {
  gasnete_eop_t *eop;
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
    retval->eop = (gasnete_eop_t *)GASNET_INVALID_HANDLE;
  } else {
    /* Small, aligned source, so would call gasnete_get_nb() here if such a thing existed */
    retval->eop = (gasnete_eop_t *)gasnete_get_nb_bulk(GASNETE_STARTOFBITS(&(retval->val),nbytes), node, src, nbytes GASNETE_THREAD_PASS);
  }
  return retval;
}

extern gasnet_register_value_t gasnete_wait_syncnb_valget(gasnet_valget_handle_t handle) {
  gasnet_register_value_t val;
  gasnete_threaddata_t * const thread = gasnete_threadtable[handle->threadidx];
  assert(thread == gasnete_mythread());
  handle->next = thread->valget_free; /* free before the wait to save time after the wait, */
  thread->valget_free = handle;       /*  safe because this thread is under our control */

  gasnete_wait_syncnb((gasnet_handle_t)handle->eop);

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

static enum { OUTSIDE_BARRIER, INSIDE_BARRIER } barrier_splitstate = OUTSIDE_BARRIER;
static int volatile barrier_value; /*  local barrier value */
static int volatile barrier_flags; /*  local barrier flags */
static int volatile barrier_phase = 0;  /*  2-phase operation to improve pipelining */
static int volatile barrier_response_done[2] = { 0, 0 }; /*  non-zero when barrier is complete */
static int volatile barrier_response_mismatch[2] = { 0, 0 }; /*  non-zero if we detected a mismatch */
#if defined(STATS) || defined(TRACE)
  static gasneti_stattime_t barrier_notifytime; /* for statistical purposes */ 
#endif

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
/* ------------------------------------------------------------------------------------ */
/*
  Handlers:
  =========
*/
static gasnet_handlerentry_t const gasnete_handlers[] = {
  /* ptr-width independent handlers */
  gasneti_handler_tableentry_no_bits(gasnete_barrier_notify_reqh),
  gasneti_handler_tableentry_no_bits(gasnete_barrier_done_reqh),

  /* ptr-width dependent handlers */
  gasneti_handler_tableentry_with_bits(gasnete_done_reph),
  gasneti_handler_tableentry_with_bits(gasnete_getmed_reqh),
  gasneti_handler_tableentry_with_bits(gasnete_getmed_reph),
  gasneti_handler_tableentry_with_bits(gasnete_getlong_reqh),
  gasneti_handler_tableentry_with_bits(gasnete_put_reqh),
  gasneti_handler_tableentry_with_bits(gasnete_memset_reqh),

  { 0, NULL }
};

extern gasnet_handlerentry_t const *gasnete_get_handlertable() {
  return gasnete_handlers;
}
/* ------------------------------------------------------------------------------------ */


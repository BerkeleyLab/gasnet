/*  $Archive:: /Ti/AMMPI/ammpi_ep.c                                       $
 *     $Date: 2003/12/22 08:48:30 $
 * $Revision: 1.15 $
 * Description: AMMPI Implementations of endpoint and bundle operations
 * Copyright 2000, Dan Bonachea <bonachea@cs.berkeley.edu>
 */

#include <stdlib.h>
#include <string.h>
#include <time.h>
#ifdef WIN32
  #define sched_yield() Sleep(0)
#else
  #include <unistd.h>
  #include <sched.h>
#endif

#include <ammpi_internal.h>

/* definitions for internal declarations */
int ammpi_Initialized = 0;
ammpi_handler_fn_t ammpi_unused_handler = (ammpi_handler_fn_t)&abort;
ammpi_handler_fn_t ammpi_defaultreturnedmsg_handler = (ammpi_handler_fn_t)&AMMPI_DefaultReturnedMsg_Handler;
int AMMPI_VerboseErrors = 0;
int AMMPI_SilentMode = 0; 
AMMPI_IDENT(AMMPI_IdentString_Version, "$AMMPILibraryVersion: " AMMPI_LIBRARY_VERSION_STR " $");

const ammpi_stats_t AMMPI_initial_stats = /* the initial state for stats type */
        { {0,0,0}, {0,0,0}, 
              {0,0,0}, {0,0,0},
              0,
              (uint64_t)-1, 0, 0,
              {0,0,0}, 0
              };

/* ------------------------------------------------------------------------------------ */
extern int enEqual(en_t en1, en_t en2) {
  return (en1.mpirank == en2.mpirank && en1.mpitag == en2.mpitag);
}
/*------------------------------------------------------------------------------------
 * Endpoint list handling for bundles
 *------------------------------------------------------------------------------------ */
int AMMPI_numBundles = 0;
eb_t AMMPI_bundles[AMMPI_MAX_BUNDLES] = {0};
/* ------------------------------------------------------------------------------------ */
static int AMMPI_ContainsEndpoint(eb_t eb, ep_t ep) {
  int i;
  for (i = 0; i < eb->n_endpoints; i++) {
    if (eb->endpoints[i] == ep) return TRUE;
    }
  return FALSE;
  }
/* ------------------------------------------------------------------------------------ */
static void AMMPI_InsertEndpoint(eb_t eb, ep_t ep) {
  AMMPI_assert(eb && ep);
  AMMPI_assert(eb->endpoints);
  if (eb->n_endpoints == eb->cursize) { /* need to grow array */
    int newsize = eb->cursize * 2;
    ep_t *newendpoints = (ep_t *)malloc(sizeof(ep_t)*newsize);
    memcpy(newendpoints, eb->endpoints, sizeof(ep_t)*eb->n_endpoints);
    free(eb->endpoints);
    eb->endpoints = newendpoints;
    eb->cursize = newsize;
    }
  eb->endpoints[eb->n_endpoints] = ep;
  eb->n_endpoints++;
  }
/* ------------------------------------------------------------------------------------ */
static void AMMPI_RemoveEndpoint(eb_t eb, ep_t ep) {
  AMMPI_assert(eb && ep);
  AMMPI_assert(eb->endpoints);
  AMMPI_assert(AMMPI_ContainsEndpoint(eb, ep));
  {
    int i;
    for (i = 0; i < eb->n_endpoints; i++) {
      if (eb->endpoints[i] == ep) {
        eb->endpoints[i] = eb->endpoints[eb->n_endpoints-1];
        eb->n_endpoints--;
        return;
        }
      }
    abort();
    }
  }
/*------------------------------------------------------------------------------------
 * Endpoint resource management
 *------------------------------------------------------------------------------------ */
static MPI_Comm currentComm = MPI_COMM_NULL;
extern int AMMPI_SetEndpointCommunicator(MPI_Comm *comm) {
  if (comm) currentComm = *comm;
  else currentComm = MPI_COMM_WORLD;
  return AM_OK;
}
/* ------------------------------------------------------------------------------------ */
static int AMMPI_AllocateEndpointResource(ep_t ep) {
  int procnum;
  int mpitag;
  int pid = getpid();
  AMMPI_assert(ep);

  ep->translation = calloc(AMMPI_INIT_NUMTRANSLATIONS, sizeof(ammpi_translation_t));
  if (ep->translation == NULL) 
    AMMPI_RETURN_ERRFR(RESOURCE, AMMPI_AllocateEndpointResource, "out of memory");
  ep->translationsz = AMMPI_INIT_NUMTRANSLATIONS;

  /* base MPI tag on pid to prevent receiving cross-talk messages sent to dead processes */
  mpitag = pid % (MPI_TAG_UB+1);
  if (mpitag == MPI_ANY_TAG) mpitag = (mpitag + 1) % (MPI_TAG_UB+1);

  MPI_SAFE(MPI_Comm_rank(currentComm, &procnum));
  ep->name.mpirank = procnum;
  ep->name.mpitag = mpitag;
  MPI_SAFE(MPI_Errhandler_set(currentComm, MPI_ERRORS_RETURN));
  ep->pmpicomm = malloc(sizeof(MPI_Comm));
  *(ep->pmpicomm) = currentComm;
  currentComm = MPI_COMM_NULL;

  return AM_OK;
  }
/* ------------------------------------------------------------------------------------ */
static int AMMPI_AllocateEndpointBuffers(ep_t ep) {
  int numBufs;
  int retval = TRUE;
  AMMPI_assert(ep);
  AMMPI_assert(ep->depth >= 1);
  AMMPI_assert(ep->translationsz >= AMMPI_INIT_NUMTRANSLATIONS);
  AMMPI_assert(ep->translationsz <= AMMPI_MAX_NUMTRANSLATIONS);
  AMMPI_assert(ep->totalP <= ep->translationsz);
  AMMPI_assert(sizeof(ammpi_buf_t) % sizeof(int) == 0); /* assume word-addressable machine */

  numBufs = ep->depth;

  /* compressed translation table */
  ep->perProcInfo = (ammpi_perproc_info_t *)malloc(ep->totalP * sizeof(ammpi_perproc_info_t));
  if (ep->perProcInfo == NULL) return FALSE;
  memset(ep->perProcInfo, 0, ep->totalP * sizeof(ammpi_perproc_info_t));

  #if AMMPI_PREPOST_RECVS 
    /* setup recv buffers */
    ep->rxBuf = (ammpi_buf_t *)malloc(numBufs * sizeof(ammpi_buf_t));
    ep->rxHandle = (MPI_Request *)malloc(numBufs * sizeof(MPI_Request));
    if (ep->rxBuf == NULL || ep->rxHandle == NULL) return FALSE;
    AMMPI_assert(((uintptr_t)ep->rxBuf) % 8 == 0);
    ep->rxNumBufs = numBufs;

    { int i;
      for(i=0;i<numBufs;i++) {
        ep->rxHandle[i] = MPI_REQUEST_NULL;
      }
      for(i=0;i<numBufs;i++) {
        retval &= MPI_SAFE_NORETURN(MPI_Irecv(&ep->rxBuf[i], AMMPI_MAX_NETWORK_MSG, MPI_BYTE, 
                           MPI_ANY_SOURCE, MPI_ANY_TAG, *(ep->pmpicomm), 
                           &ep->rxHandle[i]));
        AMMPI_assert(ep->rxHandle[i] != MPI_REQUEST_NULL);
      }
      ep->rxCurr = 0; /* oldest recv */
    }
  #endif

  #if AMMPI_NONBLOCKING_SENDS
    if (!AMMPI_AllocateSendBuffers(ep)) retval = FALSE;
  #endif

  return retval;
  }
/* ------------------------------------------------------------------------------------ */
static int AMMPI_FreeEndpointResource(ep_t ep) {
  AMMPI_assert(ep);
  AMMPI_assert(ep->translation);
  free(ep->translation);
  ep->translation = NULL;
  AMMPI_assert(ep->pmpicomm);
  free(ep->pmpicomm);
  ep->pmpicomm = NULL;
  return TRUE;
  }
/* ------------------------------------------------------------------------------------ */
static int AMMPI_FreeEndpointBuffers(ep_t ep) {
  int retval = TRUE;
  AMMPI_assert(ep);

  free(ep->perProcInfo);
  ep->perProcInfo = NULL;

  #if AMMPI_PREPOST_RECVS 
    { int i;
      for(i=0; i < ep->rxNumBufs; i++) {
        if (ep->rxHandle[i] != MPI_REQUEST_NULL) {
          MPI_Status mpistatus;
          retval &= MPI_SAFE_NORETURN(MPI_Cancel(&ep->rxHandle[i]));
          #ifdef CRAYT3E
            /* Cray MPI implementation sometimes hangs forever if you cancel-wait */
            retval &= MPI_SAFE_NORETURN(MPI_Request_free(&ep->rxHandle[i]));
          #else
            retval &= MPI_SAFE_NORETURN(MPI_Wait(&ep->rxHandle[i], &mpistatus));
          #endif
          ep->rxHandle[i] = MPI_REQUEST_NULL;
        }
      }
    }  


    free(ep->rxBuf);
    ep->rxBuf = NULL;

    free(ep->rxHandle);
    ep->rxHandle = NULL;

    ep->rxNumBufs = 0;
  #endif

  #if AMMPI_NONBLOCKING_SENDS
    retval &= AMMPI_ReleaseSendBuffers(ep);
  #endif

  return retval;
  }
/*------------------------------------------------------------------------------------
 * non-blocking send buffer management
 *------------------------------------------------------------------------------------ */
#if AMMPI_NONBLOCKING_SENDS
/* ------------------------------------------------------------------------------------ */
static int AMMPI_initSendBufferPool(ammpi_sendbuffer_pool_t* pool, int count, int bufsize) {
  char* tmp = NULL;
  int i;
  AMMPI_assert(pool && count > 0 && bufsize > 0);
  AMMPI_assert(bufsize % sizeof(int) == 0);
  pool->txHandle = (MPI_Request *)malloc(count*sizeof(MPI_Request));
  pool->txBuf = (ammpi_buf_t**)malloc(count*sizeof(ammpi_buf_t*)); 
  tmp = (char*)malloc(count*bufsize);
  pool->memBlocks = (char **)malloc(sizeof(char *));
  pool->tmpIndexArray = (int *)malloc(count * sizeof(int));
  pool->tmpStatusArray = (MPI_Status *)malloc(count * sizeof(MPI_Status));
  if (!tmp || !pool->txHandle || !pool->txBuf || 
      !pool->memBlocks || !pool->tmpIndexArray || !pool->tmpStatusArray) 
    return FALSE; /* out of mem */
  pool->numBlocks = 1;
  pool->memBlocks[0] = tmp;
  for (i=0; i < count; i++) {
    pool->txBuf[i] = (ammpi_buf_t*)tmp;
    tmp += bufsize;
    pool->txHandle[i] = MPI_REQUEST_NULL;
  }
  pool->numBufs = count;
  pool->numActive = 0;
  pool->bufSize = bufsize;

  return TRUE;
}
/* allocate non-blocking send buffers for this endpoint, 
 * return TRUE/FALSE to indicate success
 */
extern int AMMPI_AllocateSendBuffers(ep_t ep) {
  int retval = TRUE;
  AMMPI_assert(ep);
  AMMPI_assert(ep->depth >= 1);
  AMMPI_assert(ep->translationsz <= AMMPI_MAX_NUMTRANSLATIONS);
  AMMPI_assert(ep->totalP <= ep->translationsz);
  
  retval &= AMMPI_initSendBufferPool(&(ep->sendPool_smallRequest), ep->depth, AMMPI_MAX_SMALL_NETWORK_MSG);
  retval &= AMMPI_initSendBufferPool(&(ep->sendPool_smallReply),   ep->depth, AMMPI_MAX_SMALL_NETWORK_MSG);
  retval &= AMMPI_initSendBufferPool(&(ep->sendPool_largeRequest), ep->depth, AMMPI_MAX_NETWORK_MSG);
  retval &= AMMPI_initSendBufferPool(&(ep->sendPool_largeReply),   ep->depth, AMMPI_MAX_NETWORK_MSG);

  return retval;
}
/* ------------------------------------------------------------------------------------ */
static int AMMPI_freeSendBufferPool(ammpi_sendbuffer_pool_t* pool) {
  int retval = TRUE;
  AMMPI_assert(pool);

  /* terminate any outstanding communications */
  { int i;
    for(i=0; i < pool->numActive; i++) {
      if (pool->txHandle[i] != MPI_REQUEST_NULL) {
        MPI_Status mpistatus;
        #if 0
          /* the MPI spec states that MPI_Cancel is a local operation that must
           * complete immediately without blocking, but apparently too many 
           * implementations screw this up
           */
          retval &= MPI_SAFE_NORETURN(MPI_Cancel(&pool->txHandle[i]));
          #ifdef CRAYT3E
            /* Cray MPI implementation sometimes hangs forever if you cancel-wait */
            retval &= MPI_SAFE_NORETURN(MPI_Request_free(&pool->txHandle[i]));
          #else
            retval &= MPI_SAFE_NORETURN(MPI_Wait(&pool->txHandle[i], &mpistatus));
          #endif
        #else
          #if 0
            /* better to simply wait and hope the remote node hasn't crashed 
             * (in which case we might get stuck here) 
             */
            retval &= MPI_SAFE_NORETURN(MPI_Wait(&pool->txHandle[i], &mpistatus));
          #else
            { /* use a timeout to decide remote side is dead */
              int j;
              int flag;
              #define RETRIES 2
              for (j = 0; j < RETRIES; j++) {
                retval &= MPI_SAFE_NORETURN(MPI_Test(&pool->txHandle[i], &flag, &mpistatus));
                if (flag) break;
                else sleep(1);
              }
              if (j == RETRIES) {
                #if AMMPI_DEBUG_VERBOSE
                  fprintf(stderr,"WARNING: Giving up on a timed-out send during shutdown\n");
                #endif
                /* attempt to cancel */
                retval &= MPI_SAFE_NORETURN(MPI_Cancel(&pool->txHandle[i]));
                retval &= MPI_SAFE_NORETURN(MPI_Request_free(&pool->txHandle[i]));
              }
            }
          #endif
        #endif
        pool->txHandle[i] = MPI_REQUEST_NULL;
      }
    }
  }  
  
  /* free the mem */
  free(pool->txHandle);
  pool->txHandle = NULL;
  free(pool->txBuf);
  pool->txBuf = NULL;
  free(pool->tmpIndexArray);
  pool->tmpIndexArray = NULL;
  free(pool->tmpStatusArray);
  pool->tmpStatusArray = NULL;
  { int i;
    for (i=0; i < pool->numBlocks; i++) 
      free(pool->memBlocks[i]);
    free(pool->memBlocks);
    pool->memBlocks = NULL;
  }

  return retval;
}
/* cancel any outstanding non-blocking sends and release the associated buffers 
 * for this endpoint, return TRUE/FALSE to indicate success
 */
extern int AMMPI_ReleaseSendBuffers(ep_t ep) {
  int retval = TRUE;
  AMMPI_assert(ep);

  retval &= AMMPI_freeSendBufferPool(&(ep->sendPool_smallRequest));
  retval &= AMMPI_freeSendBufferPool(&(ep->sendPool_smallReply));
  retval &= AMMPI_freeSendBufferPool(&(ep->sendPool_largeRequest));
  retval &= AMMPI_freeSendBufferPool(&(ep->sendPool_largeReply));

  return retval;
}
/* ------------------------------------------------------------------------------------ */
/* acquire a buffer of at least the given size numBytes associated with ep, 
 * to be used in a subsequent non-blocking MPI send operation
 * return a pointer to the buffer and the location that should be used to store the MPI
 * handle when the operation is initiated
 * if isrequest, may poll for an unbounded amount of time until some buffers become free
 * for replies, will not poll and will return in a bounded amount of time
 * the non-blocking send should be initiated before this method is called again
 * return AM_OK to indicate success
 */
extern int AMMPI_AcquireSendBuffer(ep_t ep, int numBytes, int isrequest, 
                            ammpi_buf_t** pbuf, MPI_Request** pHandle) {
  ammpi_sendbuffer_pool_t* pool = NULL;
  AMMPI_assert(ep);
  AMMPI_assert(pbuf);
  AMMPI_assert(pHandle);
  AMMPI_assert(numBytes >= AMMPI_MIN_NETWORK_MSG && numBytes <= AMMPI_MAX_NETWORK_MSG);

  /* select the appropriate pool */
  if (isrequest) {
    if (numBytes <= AMMPI_MAX_SMALL_NETWORK_MSG) pool = &ep->sendPool_smallRequest;
    else pool = &ep->sendPool_largeRequest;
  }
  else {
    if (numBytes <= AMMPI_MAX_SMALL_NETWORK_MSG) pool = &ep->sendPool_smallReply;
    else pool = &ep->sendPool_largeReply;
  }

tryagain:
  /* reap any pending pool completions */
  if (pool->numActive > 0) {
    int numcompleted = 0;
    int i;
    MPI_SAFE(MPI_Testsome(pool->numActive, pool->txHandle, &numcompleted,
                          pool->tmpIndexArray, pool->tmpStatusArray));

    /* sort the completions in ascending order (simple insertion sort) */
    for (i=1; i < numcompleted; i++) {
      int x = pool->tmpIndexArray[i];
      int j;
      for (j = i; j > 0 && pool->tmpIndexArray[j-1] > x; j--) 
        pool->tmpIndexArray[j] = pool->tmpIndexArray[j-1];
      pool->tmpIndexArray[j] = x;
    }

    /* collect completed buffers - maintain invariant that active buffers are all at front */
    for (i=numcompleted-1; i >= 0; i--) {
      int doneidx = pool->tmpIndexArray[i];
      int activeidx = pool->numActive-1;
      AMMPI_assert(pool->txHandle[doneidx] == MPI_REQUEST_NULL);
      if (doneidx != activeidx) {
        /* swap a still-active buffer into this place */
        ammpi_buf_t* tmp = pool->txBuf[doneidx];
        AMMPI_assert(pool->txHandle[activeidx] != MPI_REQUEST_NULL);
        pool->txHandle[doneidx] = pool->txHandle[activeidx];
        pool->txBuf[doneidx] = pool->txBuf[activeidx];
        pool->txHandle[activeidx] = MPI_REQUEST_NULL;
        pool->txBuf[activeidx] = tmp;
      }
      pool->numActive--;
    }
  }

  /* find a free buffer to fulfill request */
  if (pool->numActive < pool->numBufs) { /* buffer available */
    int idx = pool->numActive;
    AMMPI_assert(pool->txBuf[idx] && pool->txHandle[idx] == MPI_REQUEST_NULL);
    *pbuf = pool->txBuf[idx];
    *pHandle = &pool->txHandle[idx];
    pool->numActive++;
    return AM_OK;
  }

  /* nothing immediately available */
  if (isrequest) { /* poll until something available */
    int junk;
    #if AMMPI_DEBUG
      { static int repeatcnt = 0; 
        static unsigned int reportmask = 0xFF;
        /* TODO: can we grow send buffer pool here? */
        repeatcnt++;
        if (AMMPI_DEBUG_VERBOSE || (repeatcnt & reportmask) == 0) {
          reportmask = (reportmask << 1) | 0x1;
          fprintf(stderr, "Out of request send buffers. polling...(has happenned %i times)\n", repeatcnt); fflush(stderr);
        }
      }
    #endif
    sched_yield(); 
    { int retval = AMMPI_ServiceIncomingMessages(ep, FALSE, &junk); /* NOTE this may actually cause reentrancy to this fn on reply pool */
      if (retval != AM_OK) AMMPI_RETURN(retval);
    }
    goto tryagain;
  }
  else { /* replies cannot poll - grow the pool (yuk) */
    int newnumBufs = pool->numBufs * 2;
    MPI_Request *newtxHandle = (MPI_Request *)malloc(newnumBufs*sizeof(MPI_Request));
    ammpi_buf_t**newtxBuf = (ammpi_buf_t**)malloc(newnumBufs*sizeof(ammpi_buf_t*));
    char **newmemBlocks = (char **)malloc(sizeof(char *)*(pool->numBlocks+1));
    char* newBlock = (char*)malloc(pool->numBufs*pool->bufSize);
    int * newtmpIndexArray = (int *)malloc(newnumBufs * sizeof(int));
    MPI_Status *newtmpStatusArray = (MPI_Status *)malloc(newnumBufs * sizeof(MPI_Status));
    int i;

    if (!newtxHandle || !newtxBuf || !newmemBlocks || !newBlock || 
        !newtmpIndexArray || !newtmpStatusArray) AMMPI_RETURN_ERR(RESOURCE); /* out of mem */

    #if AMMPI_DEBUG
      fprintf(stderr, "Out of reply send buffers. growing pool...\n"); fflush(stderr);
    #endif

    /* copy old values & preserve ordering */
    memcpy(newtxHandle, pool->txHandle, pool->numBufs*sizeof(MPI_Request));
    memcpy(newtxBuf, pool->txBuf, pool->numBufs*sizeof(ammpi_buf_t*));
    memcpy(newmemBlocks, pool->memBlocks, pool->numBlocks*sizeof(char*));
    newmemBlocks[pool->numBlocks] = newBlock;
    /* tmps needs not be preserved */

    for (i=pool->numBufs; i < newnumBufs; i++) {
      newtxBuf[i] = (ammpi_buf_t*)newBlock;
      newBlock += pool->bufSize;
      newtxHandle[i] = MPI_REQUEST_NULL;
    }

    free(pool->txHandle);
    pool->txHandle = newtxHandle;
    free(pool->txBuf);
    pool->txBuf = newtxBuf;
    free(pool->memBlocks);
    pool->memBlocks = newmemBlocks;
    free(pool->tmpIndexArray);
    pool->tmpIndexArray = newtmpIndexArray;
    free(pool->tmpStatusArray);
    pool->tmpStatusArray = newtmpStatusArray;

    pool->numBlocks++;
    pool->numBufs = newnumBufs;

    goto tryagain; /* now there should be room */
  }

  abort();
  return AM_OK;
}
/* ------------------------------------------------------------------------------------ */
#endif
/*------------------------------------------------------------------------------------
 * System initialization/termination
 *------------------------------------------------------------------------------------ */
extern int AM_Init() {
  {
    int initialized = 0;
    MPI_SAFE(MPI_Initialized(&initialized));
    if (!initialized) AMMPI_RETURN_ERRFR(RESOURCE, AM_Init, "MPI not initialized");
  }

  if (ammpi_Initialized == 0) { /* first call */
    /* check system attributes */
    AMMPI_assert(sizeof(int8_t) == 1);
    AMMPI_assert(sizeof(uint8_t) == 1);
    #if !defined(CRAYT3E)
      AMMPI_assert(sizeof(int16_t) == 2);
      AMMPI_assert(sizeof(uint16_t) == 2);
    #endif
    AMMPI_assert(sizeof(int32_t) == 4);
    AMMPI_assert(sizeof(uint32_t) == 4);
    AMMPI_assert(sizeof(int64_t) == 8);
    AMMPI_assert(sizeof(uint64_t) == 8);

    AMMPI_assert(sizeof(uintptr_t) >= sizeof(void *));

    #if 0
      #define DUMPSZ(T) printf("sizeof(" #T ")=%i\n", sizeof(T))
      DUMPSZ(ammpi_msg_t); DUMPSZ(ammpi_buf_t); DUMPSZ(en_t); DUMPSZ(ammpi_bufstatus_t);
    #endif 
    AMMPI_assert(sizeof(ammpi_msg_t) % 4 == 0);
    AMMPI_assert(sizeof(ammpi_buf_t) % 8 == 0); /* needed for payload alignment */

    { char *buffer;
      buffer = (char *)malloc(AMMPI_SENDBUFFER_SZ);
      MPI_SAFE(MPI_Buffer_attach(buffer, AMMPI_SENDBUFFER_SZ));
    }
  }


  ammpi_Initialized++;
  return AM_OK;
  }
/* ------------------------------------------------------------------------------------ */
extern int AM_Terminate() {
  int i;
  int retval = AM_OK;
  AMMPI_CHECKINIT();

  if (ammpi_Initialized == 1) { /* last termination call */
    for (i = 0; i < AMMPI_numBundles; i++) {
      if (AM_FreeBundle(AMMPI_bundles[i]) != AM_OK) 
        retval = AM_ERR_RESOURCE;
    }
    AMMPI_numBundles = 0;

    { char *buffer= NULL;
      int sz = 0;
      if (!MPI_SAFE_NORETURN(MPI_Buffer_detach(&buffer, &sz))) retval = AM_ERR_RESOURCE;
      free(buffer);
    }
  }

  ammpi_Initialized--;
  AMMPI_RETURN(retval);
  }
/*------------------------------------------------------------------------------------
 * endpoint/bundle management
 *------------------------------------------------------------------------------------ */
extern int AM_AllocateBundle(int type, eb_t *endb) {
  eb_t eb;
  AMMPI_CHECKINIT();
  if (type < 0 || type >= AM_NUM_BUNDLE_MODES) AMMPI_RETURN_ERR(BAD_ARG);
  if (type != AM_SEQ) AMMPI_RETURN_ERR(RESOURCE);
  if (AMMPI_numBundles == AMMPI_MAX_BUNDLES-1) AMMPI_RETURN_ERR(RESOURCE);
  if (!endb) AMMPI_RETURN_ERR(BAD_ARG);

  eb = (eb_t)malloc(sizeof(struct ammpi_eb));
  eb->endpoints = (ep_t *)malloc(AMMPI_INITIAL_NUMENDPOINTS*sizeof(ep_t));
  eb->cursize = AMMPI_INITIAL_NUMENDPOINTS;
  eb->n_endpoints = 0;
  eb->event_mask = AM_NOEVENTS;

  AMMPI_bundles[AMMPI_numBundles++] = eb; /* keep track of all bundles */
  *endb = eb;
  return AM_OK;
  }
/* ------------------------------------------------------------------------------------ */
extern int AM_FreeBundle(eb_t bundle) {
  if (!bundle) AMMPI_RETURN_ERR(BAD_ARG);
  {
    int i;

    /* free all constituent endpoints */
    for (i = 0; i < bundle->n_endpoints; i++) {
      int retval = AM_FreeEndpoint(bundle->endpoints[i]);
      if (retval != AM_OK) AMMPI_RETURN(retval);
      }
    AMMPI_assert(bundle->n_endpoints == 0);

    /* remove from bundle list */
    for (i = 0; i < AMMPI_numBundles; i++) {
      if (AMMPI_bundles[i] == bundle) { 
        AMMPI_bundles[i] = AMMPI_bundles[AMMPI_numBundles-1]; 
        break;
        }
      }
    AMMPI_assert(i < AMMPI_numBundles);
    AMMPI_numBundles--;

    free(bundle->endpoints);
    free(bundle);
    }
  return AM_OK;
  }
/* ------------------------------------------------------------------------------------ */
extern int AM_AllocateEndpoint(eb_t bundle, ep_t *endp, en_t *endpoint_name) {
  ep_t ep;
  int retval;

  AMMPI_CHECKINIT();
  if (!bundle || !endp || !endpoint_name) AMMPI_RETURN_ERR(BAD_ARG);

  if (currentComm == MPI_COMM_NULL) 
    AMMPI_RETURN_ERRFR(RESOURCE, AM_AllocateEndpoint, "required AMMPI_SetEndpointCommunicator() has not been called");

  ep = (ep_t)malloc(sizeof(struct ammpi_ep));
  if (!ep) AMMPI_RETURN_ERRFR(RESOURCE, AM_AllocateEndpoint, "out of memory");

  retval = AMMPI_AllocateEndpointResource(ep);
  if (retval != AM_OK) {
    free(ep);
    AMMPI_RETURN(retval);
    }

  /* setup eb<->ep link */
  AMMPI_InsertEndpoint(bundle, ep);
  ep->eb = bundle;

  /* initialize ep data */
  {
    int i;
    ep->handler[0] = ammpi_defaultreturnedmsg_handler;
    for (i = 1; i < AMMPI_MAX_NUMHANDLERS; i++) {
      ep->handler[i] = ammpi_unused_handler;
      }
    ep->controlMessageHandler = ammpi_unused_handler;
    ep->tag = AM_NONE;
    ep->segAddr = NULL;
    ep->segLength = 0;
    ep->totalP = 0; 
    ep->depth = -1;

    ep->stats = AMMPI_initial_stats;
    ep->preHandlerCallback = NULL;
    ep->postHandlerCallback = NULL;
    }

  *endp = ep;
  *endpoint_name = ep->name;
  return AM_OK;
  }
/* ------------------------------------------------------------------------------------ */
extern int AM_FreeEndpoint(ep_t ea) {
  int retval = AM_OK;
  AMMPI_CHECKINIT();
  if (!ea) AMMPI_RETURN_ERR(BAD_ARG);
  if (!AMMPI_ContainsEndpoint(ea->eb, ea)) AMMPI_RETURN_ERR(RESOURCE);

  if (!AMMPI_FreeEndpointResource(ea)) retval = AM_ERR_RESOURCE;
  if (!AMMPI_FreeEndpointBuffers(ea)) retval = AM_ERR_RESOURCE;

  AMMPI_RemoveEndpoint(ea->eb, ea);
  free(ea);
  AMMPI_RETURN(retval);
  }
/* ------------------------------------------------------------------------------------ */
extern int AM_MoveEndpoint(ep_t ea, eb_t from_bundle, eb_t to_bundle) {
  AMMPI_CHECKINIT();
  if (!ea || !from_bundle || !to_bundle) AMMPI_RETURN_ERR(BAD_ARG);
  if (!AMMPI_ContainsEndpoint(from_bundle, ea)) AMMPI_RETURN_ERR(RESOURCE);

  AMMPI_RemoveEndpoint(from_bundle, ea);
  AMMPI_InsertEndpoint(to_bundle, ea);
  return AM_OK;
  }
/*------------------------------------------------------------------------------------
 * Tag management
 *------------------------------------------------------------------------------------ */
extern int AM_SetTag(ep_t ea, tag_t tag) {
  AMMPI_CHECKINIT();
  if (!ea) AMMPI_RETURN_ERR(BAD_ARG);

  /*  TODO: return mismatched messages to sender */
  ea->tag = tag;
  return AM_OK;
  }
/* ------------------------------------------------------------------------------------ */
extern int AM_GetTag(ep_t ea, tag_t *tag) {
  AMMPI_CHECKINIT();
  if (!ea || !tag) AMMPI_RETURN_ERR(BAD_ARG);

  *tag = ea->tag;
  return AM_OK;
  }
/*------------------------------------------------------------------------------------
 * VM Segment management
 *------------------------------------------------------------------------------------ */
extern int AM_GetSeg(ep_t ea, void **addr, uintptr_t *nbytes) {
  AMMPI_CHECKINIT();
  if (!ea || !addr || !nbytes) AMMPI_RETURN_ERR(BAD_ARG);
  *addr = ea->segAddr;
  *nbytes = ea->segLength;
  return AM_OK;
  }
/* ------------------------------------------------------------------------------------ */
extern int AM_SetSeg(ep_t ea, void *addr, uintptr_t nbytes) {
  AMMPI_CHECKINIT();
  if (!ea) AMMPI_RETURN_ERR(BAD_ARG);
  if (nbytes > AMMPI_MAX_SEGLENGTH) AMMPI_RETURN_ERR(BAD_ARG);

  ea->segAddr = addr;
  ea->segLength = nbytes;
  return AM_OK;
  }
/* ------------------------------------------------------------------------------------ */
extern int AM_MaxSegLength(uintptr_t* nbytes) {
  AMMPI_CHECKINIT();
  if (!nbytes) AMMPI_RETURN_ERR(BAD_ARG);
  *nbytes = AMMPI_MAX_SEGLENGTH;
  return AM_OK;
}
/*------------------------------------------------------------------------------------
 * Translation management
 *------------------------------------------------------------------------------------ */
extern int AMMPI_Map(ep_t ea, int index, en_t *name, tag_t tag) {
  AMMPI_CHECKINIT();
  if (!ea) AMMPI_RETURN_ERR(BAD_ARG);
  if (index < 0 || index >= ea->translationsz) AMMPI_RETURN_ERR(BAD_ARG);
  if (ea->translation[index].inuse) AMMPI_RETURN_ERR(RESOURCE); /* it's an error to re-map */
  if (ea->depth != -1) AMMPI_RETURN_ERR(RESOURCE); /* it's an error to map after call to AM_SetExpectedResources */

  { int commsz; /* check communicator */
    MPI_SAFE(MPI_Comm_size(*(ea->pmpicomm), &commsz));
    if (name->mpirank < 0 || name->mpirank >= commsz)
      AMMPI_RETURN_ERRFR(RESOURCE, AM_Map, "Bad endpoint name - may be due to a MPI communicator mismatch");
  }

  ea->translation[index].inuse = TRUE;
  ea->translation[index].name = *name;
  ea->translation[index].tag = tag;
  ea->totalP++;  /* track num of translations */
  return AM_OK;
  }
/* ------------------------------------------------------------------------------------ */
extern int AMMPI_MapAny(ep_t ea, int *index, en_t *name, tag_t tag) {
  AMMPI_CHECKINIT();
  if (!ea || !index) AMMPI_RETURN_ERR(BAD_ARG);
  if (ea->depth != -1) AMMPI_RETURN_ERR(RESOURCE); /* it's an error to map after call to AM_SetExpectedResources */

  { int commsz; /* check communicator */
    MPI_SAFE(MPI_Comm_size(*(ea->pmpicomm), &commsz));
    if (name->mpirank < 0 || name->mpirank >= commsz)
      AMMPI_RETURN_ERRFR(RESOURCE, AM_Map, "Bad endpoint name - may be due to a MPI communicator mismatch");
  }

  {
    ammpi_node_t i;
    for (i = 0; i < ea->translationsz; i++) {
      if (!ea->translation[i].inuse) { /* use this one */
        ea->translation[i].inuse = TRUE;
        ea->translation[i].name = *name;
        ea->translation[i].tag = tag;
        ea->totalP++;  /* track num of translations */
        *index = i;
        return AM_OK;
        }
      }
    AMMPI_RETURN_ERR(RESOURCE); /* none available */
    }
  }
/* ------------------------------------------------------------------------------------ */
extern int AM_UnMap(ep_t ea, int index) {
  AMMPI_CHECKINIT();
  if (!ea) AMMPI_RETURN_ERR(BAD_ARG);
  if (index < 0 || index >= ea->translationsz) AMMPI_RETURN_ERR(BAD_ARG);
  if (!ea->translation[index].inuse) AMMPI_RETURN_ERR(RESOURCE); /* not mapped */
  if (ea->depth != -1) AMMPI_RETURN_ERR(RESOURCE); /* it's an error to unmap after call to AM_SetExpectedResources */

  ea->translation[index].inuse = FALSE;
  ea->totalP--;  /* track num of translations */
  return AM_OK;
  }
/* ------------------------------------------------------------------------------------ */
extern int AM_GetNumTranslations(ep_t ea, int *pntrans) {
  AMMPI_CHECKINIT();
  if (!ea) AMMPI_RETURN_ERR(BAD_ARG);
  AMMPI_assert(ea->translationsz <= AMMPI_MAX_NUMTRANSLATIONS);
  *(pntrans) = ea->translationsz;
  return AM_OK;
}
/* ------------------------------------------------------------------------------------ */
extern int AM_SetNumTranslations(ep_t ea, int ntrans) {
  ammpi_translation_t *temp;
  ammpi_node_t i;
  AMMPI_CHECKINIT();
  if (!ea) AMMPI_RETURN_ERR(BAD_ARG);
  if (ntrans < 0 || ntrans > AMMPI_MAX_NUMTRANSLATIONS) AMMPI_RETURN_ERR(RESOURCE);
  if (ntrans < AMMPI_INIT_NUMTRANSLATIONS) /* don't shrink beyond min value */
    ntrans = AMMPI_INIT_NUMTRANSLATIONS;
  if (ntrans == ea->translationsz) return AM_OK; /* no change */
  if (ea->depth != -1) AMMPI_RETURN_ERR(RESOURCE); /* it's an error to change translationsz after call to AM_SetExpectedResources */

  for (i = ntrans; i < ea->translationsz; i++) {
    if (ea->translation[i].inuse) 
      AMMPI_RETURN_ERR(RESOURCE); /* it's an error to truncate away live maps */
  }
  temp = calloc(sizeof(ammpi_translation_t), ntrans);
  if (!temp) AMMPI_RETURN_ERR(RESOURCE);
  /* we may be growing or truncating the table */
  memcpy(temp, ea->translation, 
         sizeof(ammpi_translation_t)*MIN(ea->translationsz,ntrans));
  free(ea->translation);
  ea->translation = temp;
  ea->translationsz = ntrans;

  return AM_OK;
}
/* ------------------------------------------------------------------------------------ */
extern int AM_GetTranslationInuse(ep_t ea, int i) {
  AMMPI_CHECKINIT();
  if (!ea) AMMPI_RETURN_ERR(BAD_ARG);
  if (i < 0 || i >= ea->translationsz) AMMPI_RETURN_ERR(BAD_ARG);

  if (ea->translation[i].inuse) return AM_OK; /* in use */
  else return AM_ERR_RESOURCE; /* don't complain here - it's a common case */
  }
/* ------------------------------------------------------------------------------------ */
extern int AM_GetTranslationTag(ep_t ea, int i, tag_t *tag) {
  AMMPI_CHECKINIT();
  if (!ea || !tag) AMMPI_RETURN_ERR(BAD_ARG);
  if (i < 0 || i >= ea->translationsz) AMMPI_RETURN_ERR(BAD_ARG);
  if (!ea->translation[i].inuse) AMMPI_RETURN_ERR(RESOURCE);

  (*tag) = ea->translation[i].tag;
  return AM_OK;
  }
/* ------------------------------------------------------------------------------------ */
extern int AMMPI_SetTranslationTag(ep_t ea, int index, tag_t tag) {
  AMMPI_CHECKINIT();
  if (!ea) AMMPI_RETURN_ERR(BAD_ARG);
  if (index < 0 || index >= ea->translationsz) AMMPI_RETURN_ERR(BAD_ARG);
  if (!ea->translation[index].inuse) AMMPI_RETURN_ERR(RESOURCE); /* can't change tag if not mapped */

  ea->translation[index].tag = tag;

  if (ea->depth != -1) { /* after call to AM_SetExpectedResources we must update compressed table */
    ea->perProcInfo[ea->translation[index].id].tag = tag;
    }

  return AM_OK;
  }
/* ------------------------------------------------------------------------------------ */
extern int AM_GetTranslationName(ep_t ea, int i, en_t *gan) {
  AMMPI_CHECKINIT();
  if (!ea || !gan) AMMPI_RETURN_ERR(BAD_ARG);
  if (i < 0 || i >= ea->translationsz) AMMPI_RETURN_ERR(BAD_ARG);
  if (!ea->translation[i].inuse) AMMPI_RETURN_ERR(RESOURCE);

  (*gan) = ea->translation[i].name; 
  return AM_OK;
  }
/* ------------------------------------------------------------------------------------ */
extern int AM_SetExpectedResources(ep_t ea, int n_endpoints, int n_outstanding_requests) {
  int retval = AM_OK;
  AMMPI_CHECKINIT();
  if (!ea) AMMPI_RETURN_ERR(BAD_ARG);
  if (ea->depth != -1) AMMPI_RETURN_ERR(RESOURCE); /* it's an error to call AM_SetExpectedResources again */
  /* n_endpoints ignored */
  /*if (n_endpoints < 1 || n_endpoints >= ea->translationsz) AMMPI_RETURN_ERR(BAD_ARG);*/
  if (n_outstanding_requests < 1 || n_outstanding_requests > AMMPI_MAX_NETWORKDEPTH) AMMPI_RETURN_ERR(BAD_ARG);

  ea->depth = n_outstanding_requests;

  if (!AMMPI_AllocateEndpointBuffers(ea)) retval = AM_ERR_RESOURCE;

  /*  compact a copy of the translation table into our perproc info array */
  { ammpi_node_t procid = 0;
    ammpi_node_t i;
    for (i=0; i < ea->translationsz; i++) {
      if (ea->translation[i].inuse) {
        ea->perProcInfo[procid].remoteName = ea->translation[i].name;
        ea->perProcInfo[procid].tag = ea->translation[i].tag;
        ea->translation[i].id = procid;
        procid++;
        if (procid == ea->totalP) break; /*  should have all of them now */
        }
      }
    }
  AMMPI_RETURN(retval);
  }
/*------------------------------------------------------------------------------------
 * Handler management
 *------------------------------------------------------------------------------------ */
extern int AM_SetHandler(ep_t ea, handler_t handler, ammpi_handler_fn_t function) {
  AMMPI_CHECKINIT();
  if (!ea || !function) AMMPI_RETURN_ERR(BAD_ARG);
  if (AMMPI_BADHANDLERVAL(handler)) AMMPI_RETURN_ERR(BAD_ARG);

  ea->handler[handler] = function;
  return AM_OK;
  }
/* ------------------------------------------------------------------------------------ */
extern int AM_SetHandlerAny(ep_t ea, handler_t *handler, ammpi_handler_fn_t function) {
  int i;
  AMMPI_CHECKINIT();
  if (!ea || !function || !handler) AMMPI_RETURN_ERR(BAD_ARG);

  for (i = 1 ; i < AMMPI_MAX_NUMHANDLERS; i++) {
    if (ea->handler[i] == ammpi_unused_handler) { /* find unused entry */
      ea->handler[i] = function;
      *handler = (handler_t)i;
      return AM_OK;
      }
    }
  AMMPI_RETURN_ERR(RESOURCE); /* all in use */
  }
/* ------------------------------------------------------------------------------------ */
extern int AMMPI_RegisterControlMessageHandler(ep_t ea, ammpi_handler_fn_t function) {
  AMMPI_CHECKINIT();
  if (!ea || !function) AMMPI_RETURN_ERR(BAD_ARG);
  ea->controlMessageHandler = function;
  return AM_OK;
}
/*------------------------------------------------------------------------------------
 * Event management
 *------------------------------------------------------------------------------------ */
extern int AM_GetEventMask(eb_t eb, int *mask) {
  AMMPI_CHECKINIT();
  if (!eb) AMMPI_RETURN_ERR(BAD_ARG);

  *mask = eb->event_mask;
  return AM_OK;
  }
/* ------------------------------------------------------------------------------------ */
extern int AM_SetEventMask(eb_t eb, int mask) {
  AMMPI_CHECKINIT();
  if (!eb) AMMPI_RETURN_ERR(BAD_ARG);
  if (mask < 0 || ((ammpi_eventmask_t)mask) >= AM_NUMEVENTMASKS) AMMPI_RETURN_ERR(BAD_ARG);

  eb->event_mask = (uint8_t)mask;
  return AM_OK;
  }
/* ------------------------------------------------------------------------------------ */
extern int AM_WaitSema(eb_t eb) {
  int retval;
  AMMPI_CHECKINIT();
  if (!eb) AMMPI_RETURN_ERR(BAD_ARG);
  
  if (eb->event_mask == AM_NOEVENTS) 
    abort(); /* it's an error to block when the mask is not set - will never return */

  /* block here until a message arrives - this polls too */
  retval = AMMPI_Block(eb);
  if (retval != AM_OK) eb->event_mask = AM_NOEVENTS;

  AMMPI_RETURN(retval);
  }
/*------------------------------------------------------------------------------------
 * Message interrogation
 *------------------------------------------------------------------------------------ */
extern int AM_GetSourceEndpoint(void *token, en_t *gan) {
  AMMPI_CHECKINIT();
  if (!token || !gan) AMMPI_RETURN_ERR(BAD_ARG);
  if (!((ammpi_buf_t *)token)->status.handlerRunning) 
    AMMPI_RETURN_ERRFR(RESOURCE,AM_GetSourceEndpoint,"handler not running");

  *gan = ((ammpi_buf_t *)token)->status.sourceAddr;
  return AM_OK;
  }
/* ------------------------------------------------------------------------------------ */
extern int AMMPI_GetSourceId(void *token, int *srcid) {
  AMMPI_CHECKINIT();
  if (!token || !srcid) AMMPI_RETURN_ERR(BAD_ARG);
  if (!((ammpi_buf_t *)token)->status.handlerRunning) 
    AMMPI_RETURN_ERRFR(RESOURCE,AM_GetSourceEndpoint,"handler not running");

  *srcid = ((ammpi_buf_t *)token)->status.sourceId;
  return AM_OK;
}
/* ------------------------------------------------------------------------------------ */
extern int AM_GetDestEndpoint(void *token, ep_t *endp) {
  AMMPI_CHECKINIT();
  if (!token || !endp) AMMPI_RETURN_ERR(BAD_ARG);
  if (!((ammpi_buf_t *)token)->status.handlerRunning) 
    AMMPI_RETURN_ERRFR(RESOURCE,AM_GetSourceEndpoint,"handler not running");

  *endp = ((ammpi_buf_t *)token)->status.dest;
  return AM_OK;
  }
/* ------------------------------------------------------------------------------------ */
extern int AM_GetMsgTag(void *token, tag_t *tagp) {
  AMMPI_CHECKINIT();
  if (!token || !tagp) AMMPI_RETURN_ERR(BAD_ARG);
  if (!((ammpi_buf_t *)token)->status.handlerRunning) 
    AMMPI_RETURN_ERRFR(RESOURCE,AM_GetSourceEndpoint,"handler not running");

  #if AMMPI_USE_AMTAGS
    *tagp = ((ammpi_buf_t *)token)->Msg.tag;
  #else
    #if DEBUG_VERBOSE
    { static int first = 1;
      if (first) { first = 0; fprintf(stderr,"WARNING: AM_GetMsgTag called when AM tags disabled (AMMPI_DISABLE_AMTAGS)\n");}
    }
    #endif
    *tagp = ((ammpi_buf_t *)token)->status.dest->tag;
  #endif
  return AM_OK;
  }
/* ------------------------------------------------------------------------------------ */
extern int AMMPI_SetHandlerCallbacks(ep_t ep, void (*preHandlerCallback)(), void (*postHandlerCallback)()) {
  AMMPI_CHECKINIT();
  if (!ep) AMMPI_RETURN_ERR(BAD_ARG);
  ep->preHandlerCallback = preHandlerCallback;
  ep->postHandlerCallback = postHandlerCallback;
  return AM_OK;
}
/*------------------------------------------------------------------------------------
 * Statistics API
 *------------------------------------------------------------------------------------ */
extern int AMMPI_GetEndpointStatistics(ep_t ep, ammpi_stats_t *stats) { /* called by user to get statistics */
  AMMPI_CHECKINIT();
  if (!ep || !stats) AMMPI_RETURN_ERR(BAD_ARG);
  memcpy(stats, &ep->stats, sizeof(ammpi_stats_t));
  return AM_OK;
  }
/* ------------------------------------------------------------------------------------ */
extern int AMMPI_ResetEndpointStatistics(ep_t ep) {
  AMMPI_CHECKINIT();
  if (!ep) AMMPI_RETURN_ERR(BAD_ARG);
  ep->stats = AMMPI_initial_stats;
  return AM_OK;
  }
/* ------------------------------------------------------------------------------------ */
extern int AMMPI_AggregateStatistics(ammpi_stats_t *runningsum, ammpi_stats_t *newvalues) {
  int category;
  AMMPI_CHECKINIT();
  if (!runningsum || !newvalues) AMMPI_RETURN_ERR(BAD_ARG);
  for (category = 0; category < ammpi_NumCategories; category++) {
    runningsum->RequestsSent[category] += newvalues->RequestsSent[category];
    runningsum->RequestsReceived[category] += newvalues->RequestsReceived[category];
    runningsum->RepliesSent[category] += newvalues->RepliesSent[category];
    runningsum->RepliesReceived[category] += newvalues->RepliesReceived[category];

    runningsum->DataBytesSent[category] += newvalues->DataBytesSent[category];
  }
  runningsum->ReturnedMessages += newvalues->ReturnedMessages;
  #if AMMPI_COLLECT_LATENCY_STATS
    runningsum->RequestSumLatency += newvalues->RequestSumLatency;
    if (newvalues->RequestMinLatency < runningsum->RequestMinLatency)
      runningsum->RequestMinLatency = newvalues->RequestMinLatency;
    if (newvalues->RequestMaxLatency > runningsum->RequestMaxLatency)
      runningsum->RequestMaxLatency = newvalues->RequestMaxLatency;
  #endif

  runningsum->TotalBytesSent += newvalues->TotalBytesSent;

  return AM_OK;
  }
/* ------------------------------------------------------------------------------------ */
extern const char *AMMPI_DumpStatistics(FILE *fp, ammpi_stats_t *stats, int globalAnalysis) {
  static char msg[4096];
  int64_t packetssent; 
  int64_t requestsSent = 0; 
  int64_t requestsReceived = 0; 
  int64_t repliesSent = 0; 
  int64_t repliesReceived = 0; 
  int64_t dataBytesSent = 0; 
  int category;

  AMMPI_assert(ammpi_Initialized);
  AMMPI_assert(stats != NULL);

  for (category = 0; category < ammpi_NumCategories; category++) {
    requestsSent += stats->RequestsSent[category];
    requestsReceived += stats->RequestsReceived[category];
    repliesSent += stats->RepliesSent[category];
    repliesReceived += stats->RepliesReceived[category];

    dataBytesSent += stats->DataBytesSent[category];
  }

  packetssent = (requestsSent + repliesSent);

  /* batch lines together to improve chance of output together */
  sprintf(msg, 
    " Requests: %8i sent, %8i received\n"
    " Replies:  %8i sent, %8i received\n"
    " Returned messages:%2i\n"
  #if AMMPI_COLLECT_LATENCY_STATS
    "Latency (request sent to reply received): \n"
    " min: %8i microseconds\n"
    " max: %8i microseconds\n"
    " avg: %8i microseconds\n"
  #endif

    "Message Breakdown:        Requests     Replies   Average data payload\n"
    " Small  (<=%5i bytes)   %8i    %8i        %9.3f bytes\n"
    " Medium (<=%5i bytes)   %8i    %8i        %9.3f bytes\n"
    " Large  (<=%5i bytes)   %8i    %8i        %9.3f bytes\n"
    " Total                                                %9.3f bytes\n"

    "Data bytes sent:      %9i bytes\n"
    "Total bytes sent:     %9i bytes (incl. AM overhead)\n"
    "Bandwidth overhead:   %9.2f%%\n"        
    "Average packet size:  %9.3f bytes (incl. AM overhead)\n"
    , 
    (int)requestsSent, (int)requestsReceived,
    (int)repliesSent, (int)repliesReceived,
    stats->ReturnedMessages,
  #if AMMPI_COLLECT_LATENCY_STATS
    (int)stats->RequestMinLatency,
    (int)stats->RequestMaxLatency,
    (requestsSent>0?(int)(stats->RequestSumLatency / requestsSent):-1),
  #endif

    /* Message breakdown */
    (int)(AMMPI_MAX_SHORT*sizeof(int)),
      (int)stats->RequestsSent[ammpi_Short], (int)stats->RepliesSent[ammpi_Short], 
      (stats->RequestsSent[ammpi_Short]+stats->RepliesSent[ammpi_Short] > 0 ?
        ((float)(int64_t)stats->DataBytesSent[ammpi_Short]) / 
        ((float)(int64_t)(stats->RequestsSent[ammpi_Short]+stats->RepliesSent[ammpi_Short])) : 0.0),
    (int)(AMMPI_MAX_SHORT*sizeof(int) + AMMPI_MAX_MEDIUM),
      (int)stats->RequestsSent[ammpi_Medium], (int)stats->RepliesSent[ammpi_Medium], 
      (stats->RequestsSent[ammpi_Medium]+stats->RepliesSent[ammpi_Medium] > 0 ?
        ((float)(int64_t)stats->DataBytesSent[ammpi_Medium]) / 
        ((float)(int64_t)(stats->RequestsSent[ammpi_Medium]+stats->RepliesSent[ammpi_Medium])) : 0.0),
    (int)(AMMPI_MAX_SHORT*sizeof(int) + AMMPI_MAX_LONG),
      (int)stats->RequestsSent[ammpi_Long], (int)stats->RepliesSent[ammpi_Long], 
      (stats->RequestsSent[ammpi_Long]+stats->RepliesSent[ammpi_Long] > 0 ?
        ((float)(int64_t)stats->DataBytesSent[ammpi_Long]) / 
        ((float)(int64_t)(stats->RequestsSent[ammpi_Long]+stats->RepliesSent[ammpi_Long])) : 0.0),

    /* avg data payload */
    (requestsSent+repliesSent > 0 ?
      ((float)(int64_t)dataBytesSent) / ((float)(int64_t)(requestsSent+repliesSent))
      : 0.0),

    (int)dataBytesSent,
    (int)(stats->TotalBytesSent),
    /* bandwidth overhead */
    (stats->TotalBytesSent > 0 ?
      100.0*((float)(int64_t)(stats->TotalBytesSent - dataBytesSent)) / 
      ((float)(int64_t)stats->TotalBytesSent) 
      : 0.0),
    /* avg packet size */
    (packetssent > 0 ?
      ((float)((int64_t)stats->TotalBytesSent)) / ((float)(int64_t)packetssent)
      : 0.0)
    );

  if (globalAnalysis) {
    int64_t packetsrecvd = (requestsReceived + repliesReceived);
    int64_t packetslost = packetssent - packetsrecvd;
    sprintf(msg+strlen(msg), "Packets unaccounted for: %6i", abs((int)packetslost));
    if (packetslost > 0) {
      sprintf(msg+strlen(msg), "  (%6.3f%%)\n", (100.0*packetslost)/packetssent);
    }
    else strcat(msg, "\n");
  } 

  if (fp != NULL) fprintf(fp, "%s", msg);
  return msg;
  }
/* ------------------------------------------------------------------------------------ */


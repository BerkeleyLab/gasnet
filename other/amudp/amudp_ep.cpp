/*  $Archive:: /Ti/AMUDP/amudp_ep.cpp                                     $
 *     $Date: 2004/02/13 18:00:18 $
 * $Revision: 1.4 $
 * Description: AMUDP Implementations of endpoint and bundle operations
 * Copyright 2000, Dan Bonachea <bonachea@cs.berkeley.edu>
 */

#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <signal.h>

#include <amudp.h>
#include <amudp_internal.h>

/* definitions for internal declarations */
int amudp_Initialized = 0;
amudp_handler_fn_t amudp_unused_handler = (amudp_handler_fn_t)&abort;
amudp_handler_fn_t amudp_defaultreturnedmsg_handler = (amudp_handler_fn_t)&AMUDP_DefaultReturnedMsg_Handler;
int AMUDP_VerboseErrors = 0;
int AMUDP_ExpectedBandwidth = AMUDP_DEFAULT_EXPECTED_BANDWIDTH;
int AMUDP_SilentMode = 0; 
AMUDP_IDENT(AMUDP_IdentString_Version, "$AMUDPLibraryVersion: " AMUDP_LIBRARY_VERSION_STR " $");
#ifdef UETH
  ep_t AMUDP_UETH_endpoint = NULL; /* the one-and-only UETH endpoint */
#endif

double AMUDP_FaultInjectionRate = 0.0;
double AMUDP_FaultInjectionEnabled = 0;

const amudp_stats_t AMUDP_initial_stats = /* the initial state for stats type */
        { {0,0,0}, {0,0,0}, {0,0,0}, {0,0,0}, 
              {0,0,0}, {0,0,0},
              0,
              (amudp_cputick_t)-1, 0, 0,
              {0,0,0}, 0
              };

/*------------------------------------------------------------------------------------
 * Endpoint list handling for bundles
 *------------------------------------------------------------------------------------ */
int AMUDP_numBundles = 0;
eb_t AMUDP_bundles[AMUDP_MAX_BUNDLES] = {0};
/* ------------------------------------------------------------------------------------ */
static int AMUDP_ContainsEndpoint(eb_t eb, ep_t ep) {
  int i;
  for (i = 0; i < eb->n_endpoints; i++) {
    if (eb->endpoints[i] == ep) return TRUE;
    }
  return FALSE;
  }
/* ------------------------------------------------------------------------------------ */
static void AMUDP_InsertEndpoint(eb_t eb, ep_t ep) {
  AMUDP_assert(eb && ep);
  AMUDP_assert(eb->endpoints != NULL);
  if (eb->n_endpoints == eb->cursize) { /* need to grow array */
    int newsize = eb->cursize * 2;
    ep_t *newendpoints = (ep_t *)malloc(sizeof(ep_t)*newsize);
    memcpy(newendpoints, eb->endpoints, sizeof(ep_t)*eb->n_endpoints);
    eb->endpoints = newendpoints;
    eb->cursize = newsize;
    }
  eb->endpoints[eb->n_endpoints] = ep;
  eb->n_endpoints++;
  }
/* ------------------------------------------------------------------------------------ */
static void AMUDP_RemoveEndpoint(eb_t eb, ep_t ep) {
  AMUDP_assert(eb && ep);
  AMUDP_assert(eb->endpoints != NULL);
  AMUDP_assert(AMUDP_ContainsEndpoint(eb, ep));
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
 * Endpoint bulk buffer management
 *------------------------------------------------------------------------------------ */
extern amudp_buf_t *AMUDP_AcquireBulkBuffer(ep_t ep) { // get a bulk buffer
  AMUDP_assert(ep != NULL);
  AMUDP_assert(ep->bulkBufferPoolFreeCnt <= ep->bulkBufferPoolSz);
  if (ep->bulkBufferPoolFreeCnt == 0) {
    // grow the pool
    int oldsz = ep->bulkBufferPoolSz;
    amudp_buf_t **temp = (amudp_buf_t **)malloc((oldsz+1)*sizeof(amudp_buf_t *));
    temp[0] = (amudp_buf_t *)malloc(AMUDP_MAXBULK_NETWORK_MSG);
    temp[0]->status.bulkBuffer = NULL;
    if (ep->bulkBufferPool) {
      memcpy(temp+1, ep->bulkBufferPool, sizeof(amudp_buf_t *)*oldsz);
      free(ep->bulkBufferPool);
      }
    ep->bulkBufferPool = temp;
    ep->bulkBufferPoolSz = oldsz + 1;
    ep->bulkBufferPoolFreeCnt = 1;
    }
  return ep->bulkBufferPool[--ep->bulkBufferPoolFreeCnt];
  }
/* ------------------------------------------------------------------------------------ */
extern void AMUDP_ReleaseBulkBuffer(ep_t ep, amudp_buf_t *buf) { // release a bulk buffer
  int i; // this is non-optimal, but the list should never get too long, so it won't matter
  AMUDP_assert(ep != NULL);
  AMUDP_assert(ep->bulkBufferPoolFreeCnt <= ep->bulkBufferPoolSz);
  for (i = ep->bulkBufferPoolFreeCnt; i < ep->bulkBufferPoolSz; i++) {
    if (ep->bulkBufferPool[i] == buf) {
      ep->bulkBufferPool[i] = ep->bulkBufferPool[ep->bulkBufferPoolFreeCnt];
      ep->bulkBufferPool[ep->bulkBufferPoolFreeCnt] = buf;
      ep->bulkBufferPoolFreeCnt++;
      return;
      }
    }
  ErrMessage("Internal error in AMUDP_ReleaseBulkBuffer()");
  abort();
  }
/* ------------------------------------------------------------------------------------ */
static void AMUDP_FreeAllBulkBuffers(ep_t ep) {
  int i;
  AMUDP_assert(ep != NULL);
  AMUDP_assert(ep->bulkBufferPoolFreeCnt <= ep->bulkBufferPoolSz);
  for (i=0; i < ep->bulkBufferPoolSz; i++) free(ep->bulkBufferPool[i]);
  if (ep->bulkBufferPool) free(ep->bulkBufferPool);
  ep->bulkBufferPool = NULL;
  ep->bulkBufferPoolFreeCnt = 0;
  ep->bulkBufferPoolSz = 0;
  }
/*------------------------------------------------------------------------------------
 * Endpoint resource management
 *------------------------------------------------------------------------------------ */
static uint32_t AMUDP_currentUDPInterface = INADDR_ANY;
extern int AMUDP_SetUDPInterface(uint32_t IPAddress) {
  AMUDP_currentUDPInterface = IPAddress;
  return AM_OK;
  }
/* ------------------------------------------------------------------------------------ */
#if !defined(UETH) && USE_SOCKET_RECVBUFFER_GROW
  #if 0
  #ifdef LINUX 
    #include <linux/unistd.h>
    #include <linux/sysctl.h>
  #endif
  #endif
extern void AMUDP_growSocketRecvBufferSize(ep_t ep, int targetsize) {
  int initialsize; /* original socket recv size */
  GETSOCKOPT_LENGTH_T junk = sizeof(int);
  if (getsockopt(ep->s, SOL_SOCKET, SO_RCVBUF, (char *)&initialsize, &junk) == SOCKET_ERROR) {
    #if AMUDP_DEBUG
      perror("getsockopt");
      ErrMessage("getsockopt(SOL_SOCKET, SO_RCVBUF) on UDP socket failed");
    #endif
    initialsize = 65535;
    }

  targetsize = MAX(initialsize, targetsize); /* never shrink buffer */

  #if 0 /* it appears this max means nothing */
  { int maxsize;
    #ifdef LINUX 
    { /*  try to determine the max we can use (reading /proc/sys/net/core/rmem_max may be more reliable) */
      int rmem_max[1] = { NET_CORE_RMEM_MAX };
      struct __sysctl_args args={&rmem_max,sizeof(rmem_max),&maxsize,sizeof(int),0,0};
      if (_sysctl(&args)) {
        #if AMUDP_DEBUG
          perror("sysctl");
          ErrMessage("sysctl() on UDP socket failed");
        #endif
        }
      size = MIN(size, maxsize);
      }
    #endif
    }
  #endif
  /* now set it to the largest value it will take */
  ep->socketRecvBufferMaxedOut = 0;
  while (targetsize > initialsize) {
    int sz = targetsize; /* prevent OS from tampering */
    if (setsockopt(ep->s, SOL_SOCKET, SO_RCVBUF, (char *)&sz, sizeof(int)) == SOCKET_ERROR) {
      #if AMUDP_DEBUG
        perror("setsockopt");
        ErrMessage("setsockopt(SOL_SOCKET, SO_RCVBUF, %i) on UDP socket failed", targetsize);
      #endif
      }
    else {
      int temp = targetsize;
      junk = sizeof(int);
      if (getsockopt(ep->s, SOL_SOCKET, SO_RCVBUF, (char *)&temp, &junk) == SOCKET_ERROR) {
        #if AMUDP_DEBUG
          perror("getsockopt");
          ErrMessage("getsockopt(SOL_SOCKET, SO_RCVBUF) on UDP socket failed");
        #endif
        }
      if (temp >= targetsize) {
        #if AMUDP_DEBUG_VERBOSE
          fprintf(stderr, "UDP recv buffer successfully set to %i bytes\n", targetsize); fflush(stderr);
        #endif
        ep->socketRecvBufferSize = temp;
        break; /* success */
        }
      }
    targetsize = (int)(0.9 * targetsize);
    ep->socketRecvBufferMaxedOut = 1;
    }
  }
#endif
/* ------------------------------------------------------------------------------------ */
static int AMUDP_AllocateEndpointResource(ep_t ep) {
  AMUDP_assert(ep != NULL);
  #ifdef UETH
    if (AMUDP_UETH_endpoint) return FALSE; /* only one endpoint per process */
    if (ueth_init() != UETH_OK) return FALSE;
    if (ueth_getaddress(&ep->name) != UETH_OK) {
      ueth_terminate();
      return FALSE;
      }
    /*  TODO this doesn't handle apps that dont use SPMD extensions */
    if (ueth_setaddresshook(&AMUDP_SPMDAddressChangeCallback) != UETH_OK) {
      ueth_terminate();
      return FALSE;
      }
    AMUDP_UETH_endpoint = ep;
  #else
    /* allocate socket */
    ep->s = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (ep->s == INVALID_SOCKET) 
      AMUDP_RETURN_ERRFR(RESOURCE, socket, sockErrDesc());

    #ifdef WIN32
    { /* check transport message size - UNIX doesn't seem to have a way for doing this */
      unsigned int maxmsg;
      int sz = sizeof(unsigned int);
      if (getsockopt(ep->s, SOL_SOCKET, SO_MAX_MSG_SIZE, (char*)&maxmsg, &sz) == SOCKET_ERROR)
        AMUDP_RETURN_ERRFR(RESOURCE, getsockopt, sockErrDesc());
      if (maxmsg < AMUDP_MAX_NETWORK_MSG) 
        AMUDP_RETURN_ERRFR(RESOURCE, AMUDP_AllocateEndpointResource, "max datagram size of UDP provider is too small");
      }
    #endif

    ep->name.sin_family = AF_INET;
    ep->name.sin_port = 0; /* any port */
    ep->name.sin_addr.s_addr = htonl(AMUDP_currentUDPInterface);
    memset(&ep->name.sin_zero, 0, sizeof(ep->name.sin_zero));

    if (bind(ep->s, (struct sockaddr*)&ep->name, sizeof(struct sockaddr)) == SOCKET_ERROR) {
      closesocket(ep->s);
      return FALSE;
      }
    {
      /*  danger: this might fail on multi-homed hosts if AMUDP_currentUDPInterface was not set*/
      GETSOCKNAME_LENGTH_T sz = sizeof(en_t);
      if (getsockname(ep->s, (struct sockaddr*)&ep->name, &sz) == SOCKET_ERROR) {
        closesocket(ep->s);
        return FALSE;
        }
      /* can't determine interface address */
      if (ep->name.sin_addr.s_addr == INADDR_ANY) {
        ErrMessage("AMUDP_AllocateEndpointResource failed to determine UDP endpoint interface address");
        return FALSE;
        }
      if (ep->name.sin_port == 0) {
        ErrMessage("AMUDP_AllocateEndpointResource failed to determine UDP endpoint interface port");
        return FALSE; 
        }
      }
  #endif
  return AM_OK;
  }
/* ------------------------------------------------------------------------------------ */
static int AMUDP_AllocateEndpointBuffers(ep_t ep) {
  int PD = ep->PD;
  amudp_buf_t *pool;

  AMUDP_assert(ep != NULL);
  AMUDP_assert(ep->depth >= 1);
  AMUDP_assert(ep->P > 0 && ep->P <= AMUDP_MAX_NUMTRANSLATIONS);
  AMUDP_assert(ep->PD == ep->P * ep->depth);

  #ifdef UETH
    AMUDP_assert(sizeof(amudp_buf_t) <= UETH_MAXPACKETSIZE);
    AMUDP_assert(sizeof(amudp_buf_t) % UETH_ALIGNMENT == 0);
    /* one extra for temporary buffer */
    if (ueth_allocatepool((void**)&pool, sizeof(amudp_buf_t), 
                          2*PD + 1, UETH_RECVPOOLFUDGEFACTOR * (2*PD + 1)) 
                          != UETH_OK) return FALSE;
    ep->requestBuf = pool;
    ep->replyBuf = &pool[PD];
    ep->temporaryBuf = &pool[2*PD];
    /*if (ueth_setrecvpool(&pool[2*PD], 2*PD, sizeof(amudp_buf_t));*/
  #else
    AMUDP_assert(sizeof(amudp_buf_t) % sizeof(int) == 0); /* assume word-addressable machine */
    if (2*PD+1 > 65535) return FALSE; /* would overflow rxNumBufs */
    /* one extra rx buffer for ease of implementing circular rx buf
     * one for temporary buffer
     */
    pool = (amudp_buf_t *)malloc((4 * PD + 2) * sizeof(amudp_buf_t));
    if (!pool) return FALSE;
    ep->requestBuf = pool;
    ep->replyBuf = &pool[PD];
    ep->rxBuf = &pool[2*PD];
    ep->rxNumBufs = (uint16_t)(2*PD + 1);
    ep->rxFreeIdx = 0;
    ep->rxReadyIdx = 0;
    ep->temporaryBuf = &pool[4*PD+1];
  
  #if USE_SOCKET_RECVBUFFER_GROW
  { int HPAMsize = 2*PD*AMUDP_MAX_NETWORK_MSG; /* theoretical max required by plain-vanilla HPAM */
    int padsize = 2*AMUDP_MAXBULK_NETWORK_MSG; /* some pad for non-HPAM true bulk xfers & retransmissions */
    
    AMUDP_growSocketRecvBufferSize(ep, HPAMsize+padsize);
    }
  #endif

  #endif
  ep->requestDesc = (amudp_bufdesc_t*)malloc(2 * PD * sizeof(amudp_bufdesc_t));
  ep->replyDesc = &ep->requestDesc[PD];
  /* init descriptor tables */
  memset(ep->requestDesc, 0, PD * sizeof(amudp_bufdesc_t));
  memset(ep->replyDesc,   0, PD * sizeof(amudp_bufdesc_t));
  ep->outstandingRequests = 0;
  ep->timeoutCheckPosn = 0;

  /* instance hint pointers & compressed translation table */
  ep->perProcInfo = (amudp_perproc_info_t *)malloc(ep->P * sizeof(amudp_perproc_info_t));
  memset(ep->perProcInfo, 0, ep->P * sizeof(amudp_perproc_info_t));

  { int i; /* need to init the reply bulk buffer ptrs */
    /* must do this regardless of USE_TRUE_BULK_XFERS, because we check it later even
     * if USE_TRUE_BULK_XFERS is off
     * */
    for (i = 0; i < PD; i++) ep->replyBuf[i].status.bulkBuffer = NULL;
    }

  ep->bulkBufferPool = NULL;
  ep->bulkBufferPoolSz = 0;
  ep->bulkBufferPoolFreeCnt = 0;
  return TRUE;
  }
/* ------------------------------------------------------------------------------------ */
static int AMUDP_FreeEndpointResource(ep_t ep) {
  AMUDP_assert(ep != NULL);
  #ifdef UETH
    if (!AMUDP_UETH_endpoint) return FALSE;
    if (ueth_terminate() != UETH_OK) return FALSE;
    AMUDP_UETH_endpoint = NULL;
  #else
    /*  close UDP port */
    if (closesocket(ep->s) == SOCKET_ERROR) return FALSE;
  #endif
  return TRUE;
  }
/* ------------------------------------------------------------------------------------ */
static int AMUDP_FreeEndpointBuffers(ep_t ep) {
  AMUDP_assert(ep != NULL);
  #ifdef UETH
    /* no explicit free required - handled by ueth_terminate */
  #else
    free(ep->requestBuf);
    ep->rxBuf = NULL;
  #endif
  ep->requestBuf = NULL;
  ep->replyBuf = NULL;

  free(ep->requestDesc);
  ep->requestDesc = NULL;
  ep->replyDesc = NULL;

  free(ep->perProcInfo);
  ep->perProcInfo = NULL;

  AMUDP_FreeAllBulkBuffers(ep);

  return TRUE;
  }
/*------------------------------------------------------------------------------------
 * System initialization/termination
 *------------------------------------------------------------------------------------ */
extern int AM_Init() {
  if (amudp_Initialized == 0) { /* first call */
    /* check system attributes */
    AMUDP_assert(sizeof(int8_t) == 1);
    AMUDP_assert(sizeof(uint8_t) == 1);
    #if !defined(CRAYT3E)
      AMUDP_assert(sizeof(int16_t) == 2);
      AMUDP_assert(sizeof(uint16_t) == 2);
    #endif
    AMUDP_assert(sizeof(int32_t) == 4);
    AMUDP_assert(sizeof(uint32_t) == 4);
    AMUDP_assert(sizeof(int64_t) == 8);
    AMUDP_assert(sizeof(uint64_t) == 8);

    AMUDP_assert(sizeof(uintptr_t) >= sizeof(void *));

    #ifdef WIN32
    {
      WSADATA wsa;
    #if 1
      WORD wVersionRequested = MAKEWORD( 1, 1 );
    #else
      WORD wVersionRequested = MAKEWORD( 2, 2 );
    #endif

      if (WSAStartup(wVersionRequested, &wsa)) AMUDP_RETURN_ERR(RESOURCE);
      }
    #endif

    { char *faultRate = AMUDP_getenv_prefixed("FAULT_RATE");
      if (faultRate && (AMUDP_FaultInjectionRate = atof(faultRate)) != 0.0) {
        AMUDP_FaultInjectionEnabled = 1;
        fprintf(stderr, "*** Warning: AMUDP running with fault injection enabled. Rate = %6.2f %%\n",
          100.0 * AMUDP_FaultInjectionRate);
        fflush(stderr);
        srand( (unsigned)time( NULL ) ); /* TODO: we should really be using a private rand num generator */
        }
      }
    #ifdef UETH
    { int retval = ueth_kill_link_on_signal(SIGUSR2);
      if (retval != UETH_OK)
        AMUDP_RETURN_ERRFR(RESOURCE, AM_Init, "ueth_kill_link_on_signal() failed");
    }
    #endif
    }
  amudp_Initialized++;
  return AM_OK;
  }
/* ------------------------------------------------------------------------------------ */
extern int AM_Terminate() {
  int i;
  int retval = AM_OK;
  AMUDP_CHECKINIT();

  if (amudp_Initialized == 1) { /* last termination call */
    for (i = 0; i < AMUDP_numBundles; i++) {
      if (AM_FreeBundle(AMUDP_bundles[i]) != AM_OK) 
        retval = AM_ERR_RESOURCE;
      }
    AMUDP_numBundles = 0;
    #ifdef WIN32
      if (WSACleanup()) retval = AM_ERR_RESOURCE;
    #endif
    }

  amudp_Initialized--;
  AMUDP_RETURN(retval);
  }
/*------------------------------------------------------------------------------------
 * endpoint/bundle management
 *------------------------------------------------------------------------------------ */
extern int AM_AllocateBundle(int type, eb_t *endb) {
  eb_t eb;
  AMUDP_CHECKINIT();
  if (type < 0 || type >= AM_NUM_BUNDLE_MODES) AMUDP_RETURN_ERR(BAD_ARG);
  if (type != AM_SEQ) AMUDP_RETURN_ERR(RESOURCE);
  if (AMUDP_numBundles == AMUDP_MAX_BUNDLES-1) AMUDP_RETURN_ERR(RESOURCE);
  if (!endb) AMUDP_RETURN_ERR(BAD_ARG);

  eb = (eb_t)malloc(sizeof(struct amudp_eb));
  eb->endpoints = (ep_t *)malloc(AMUDP_INITIAL_NUMENDPOINTS*sizeof(ep_t));
  eb->cursize = AMUDP_INITIAL_NUMENDPOINTS;
  eb->n_endpoints = 0;
  eb->event_mask = AM_NOEVENTS;

  AMUDP_bundles[AMUDP_numBundles++] = eb; /* keep track of all bundles */
  *endb = eb;
  return AM_OK;
  }
/* ------------------------------------------------------------------------------------ */
extern int AM_FreeBundle(eb_t bundle) {
  if (!bundle) AMUDP_RETURN_ERR(BAD_ARG);
  {
    int i;

    /* free all constituent endpoints */
    for (i = 0; i < bundle->n_endpoints; i++) {
      int retval = AM_FreeEndpoint(bundle->endpoints[i]);
      if (retval != AM_OK) AMUDP_RETURN(retval);
      }
    AMUDP_assert(bundle->n_endpoints == 0);

    /* remove from bundle list */
    for (i = 0; i < AMUDP_numBundles; i++) {
      if (AMUDP_bundles[i] == bundle) { 
        AMUDP_bundles[i] = AMUDP_bundles[AMUDP_numBundles-1]; 
        break;
        }
      }
    AMUDP_assert(i < AMUDP_numBundles);
    AMUDP_numBundles--;

    free(bundle->endpoints);
    free(bundle);
    }
  return AM_OK;
  }
/* ------------------------------------------------------------------------------------ */
extern int AM_AllocateEndpoint(eb_t bundle, ep_t *endp, en_t *endpoint_name) {
  ep_t ep;
  int retval;

  AMUDP_CHECKINIT();
  if (!bundle || !endp || !endpoint_name) AMUDP_RETURN_ERR(BAD_ARG);

  ep = (ep_t)malloc(sizeof(struct amudp_ep));
  retval = AMUDP_AllocateEndpointResource(ep);
  if (retval != AM_OK) {
    free(ep);
    AMUDP_RETURN(retval);
    }

  /* setup eb<->ep link */
  AMUDP_InsertEndpoint(bundle, ep);
  ep->eb = bundle;

  /* initialize ep data */
  {
    int i;
    for (i = 0; i < AMUDP_MAX_NUMTRANSLATIONS; i++) {
      ep->translation[i].inuse = FALSE;
      }
    ep->handler[0] = amudp_defaultreturnedmsg_handler;
    for (i = 1; i < AMUDP_MAX_NUMHANDLERS; i++) {
      ep->handler[i] = amudp_unused_handler;
      }
    ep->tag = AM_NONE;
    ep->segAddr = NULL;
    ep->segLength = 0;
    ep->P = 0; 
    ep->depth = -1;
    ep->PD = 0;

    ep->stats = AMUDP_initial_stats;
    }

  *endp = ep;
  *endpoint_name = ep->name;
  return AM_OK;
  }
/* ------------------------------------------------------------------------------------ */
extern int AM_FreeEndpoint(ep_t ea) {
  int retval = AM_OK;
  AMUDP_CHECKINIT();
  if (!ea) AMUDP_RETURN_ERR(BAD_ARG);
  if (!AMUDP_ContainsEndpoint(ea->eb, ea)) AMUDP_RETURN_ERR(RESOURCE);

  if (!AMUDP_FreeEndpointResource(ea)) retval = AM_ERR_RESOURCE;
  if (!AMUDP_FreeEndpointBuffers(ea)) retval = AM_ERR_RESOURCE;

  AMUDP_RemoveEndpoint(ea->eb, ea);
  free(ea);
  AMUDP_RETURN(retval);
  }
/* ------------------------------------------------------------------------------------ */
extern int AM_MoveEndpoint(ep_t ea, eb_t from_bundle, eb_t to_bundle) {
  AMUDP_CHECKINIT();
  if (!ea || !from_bundle || !to_bundle) AMUDP_RETURN_ERR(BAD_ARG);
  if (!AMUDP_ContainsEndpoint(from_bundle, ea)) AMUDP_RETURN_ERR(RESOURCE);

  AMUDP_RemoveEndpoint(from_bundle, ea);
  AMUDP_InsertEndpoint(to_bundle, ea);
  return AM_OK;
  }
/*------------------------------------------------------------------------------------
 * Tag management
 *------------------------------------------------------------------------------------ */
extern int AM_SetTag(ep_t ea, tag_t tag) {
  AMUDP_CHECKINIT();
  if (!ea) AMUDP_RETURN_ERR(BAD_ARG);

  /*  TODO: return mismatched messages to sender */
  ea->tag = tag;
  return AM_OK;
  }
/* ------------------------------------------------------------------------------------ */
extern int AM_GetTag(ep_t ea, tag_t *tag) {
  AMUDP_CHECKINIT();
  if (!ea || !tag) AMUDP_RETURN_ERR(BAD_ARG);

  *tag = ea->tag;
  return AM_OK;
  }
/*------------------------------------------------------------------------------------
 * VM Segment management
 *------------------------------------------------------------------------------------ */
extern int AM_GetSeg(ep_t ea, void **addr, uintptr_t *nbytes) {
  AMUDP_CHECKINIT();
  if (!ea || !addr || !nbytes) AMUDP_RETURN_ERR(BAD_ARG);
  *addr = ea->segAddr;
  *nbytes = ea->segLength;
  return AM_OK;
  }
/* ------------------------------------------------------------------------------------ */
extern int AM_SetSeg(ep_t ea, void *addr, uintptr_t nbytes) {
  AMUDP_CHECKINIT();
  if (!ea) AMUDP_RETURN_ERR(BAD_ARG);
  if (nbytes > AMUDP_MAX_SEGLENGTH) AMUDP_RETURN_ERR(BAD_ARG);

  ea->segAddr = addr;
  ea->segLength = nbytes;
  return AM_OK;
  }
/* ------------------------------------------------------------------------------------ */
extern int AM_MaxSegLength(uintptr_t* nbytes) {
  AMUDP_CHECKINIT();
  if (!nbytes) AMUDP_RETURN_ERR(BAD_ARG);
  *nbytes = AMUDP_MAX_SEGLENGTH;
  return AM_OK;
}
/*------------------------------------------------------------------------------------
 * Translation management
 *------------------------------------------------------------------------------------ */
extern int AM_Map(ep_t ea, int index, en_t name, tag_t tag) {
  AMUDP_CHECKINIT();
  if (!ea) AMUDP_RETURN_ERR(BAD_ARG);
  if (index < 0 || index >= AMUDP_MAX_NUMTRANSLATIONS) AMUDP_RETURN_ERR(BAD_ARG);
  if (ea->translation[index].inuse) AMUDP_RETURN_ERR(RESOURCE); /* it's an error to re-map */
  if (ea->depth != -1) AMUDP_RETURN_ERR(RESOURCE); /* it's an error to map after call to AM_SetExpectedResources */

  ea->translation[index].inuse = TRUE;
  ea->translation[index].name = name;
  ea->translation[index].tag = tag;
  ea->P++;  /* track num of translations */
  return AM_OK;
  }
/* ------------------------------------------------------------------------------------ */
extern int AM_MapAny(ep_t ea, int *index, en_t name, tag_t tag) {
  AMUDP_CHECKINIT();
  if (!ea || !index) AMUDP_RETURN_ERR(BAD_ARG);
  if (ea->depth != -1) AMUDP_RETURN_ERR(RESOURCE); /* it's an error to map after call to AM_SetExpectedResources */

  {
    int i;
    for (i = 0; i < AMUDP_MAX_NUMTRANSLATIONS; i++) {
      if (!ea->translation[i].inuse) { /* use this one */
        ea->translation[i].inuse = TRUE;
        ea->translation[i].name = name;
        ea->translation[i].tag = tag;
        ea->P++;  /* track num of translations */
        *index = i;
        return AM_OK;
        }
      }
    AMUDP_RETURN_ERR(RESOURCE); /* none available */
    }
  }
/* ------------------------------------------------------------------------------------ */
extern int AM_UnMap(ep_t ea, int index) {
  AMUDP_CHECKINIT();
  if (!ea) AMUDP_RETURN_ERR(BAD_ARG);
  if (index < 0 || index >= AMUDP_MAX_NUMTRANSLATIONS) AMUDP_RETURN_ERR(BAD_ARG);
  if (!ea->translation[index].inuse) AMUDP_RETURN_ERR(RESOURCE); /* not mapped */
  if (ea->depth != -1) AMUDP_RETURN_ERR(RESOURCE); /* it's an error to unmap after call to AM_SetExpectedResources */

  ea->translation[index].inuse = FALSE;
  ea->P--;  /* track num of translations */
  return AM_OK;
  }
/* ------------------------------------------------------------------------------------ */
extern int AM_GetTranslationInuse(ep_t ea, int i) {
  AMUDP_CHECKINIT();
  if (!ea) AMUDP_RETURN_ERR(BAD_ARG);
  if (i < 0 || i >= AMUDP_MAX_NUMTRANSLATIONS) AMUDP_RETURN_ERR(BAD_ARG);

  if (ea->translation[i].inuse) return AM_OK; /* in use */
  else return AM_ERR_RESOURCE; /* don't complain here - it's a common case */
  }
/* ------------------------------------------------------------------------------------ */
extern int AM_GetTranslationTag(ep_t ea, int i, tag_t *tag) {
  AMUDP_CHECKINIT();
  if (!ea || !tag) AMUDP_RETURN_ERR(BAD_ARG);
  if (i < 0 || i >= AMUDP_MAX_NUMTRANSLATIONS) AMUDP_RETURN_ERR(BAD_ARG);
  if (!ea->translation[i].inuse) AMUDP_RETURN_ERR(RESOURCE);

  (*tag) = ea->translation[i].tag;
  return AM_OK;
  }
/* ------------------------------------------------------------------------------------ */
extern int AMUDP_SetTranslationTag(ep_t ea, int index, tag_t tag) {
  AMUDP_CHECKINIT();
  if (!ea) AMUDP_RETURN_ERR(BAD_ARG);
  if (index < 0 || index >= AMUDP_MAX_NUMTRANSLATIONS) AMUDP_RETURN_ERR(BAD_ARG);
  if (!ea->translation[index].inuse) AMUDP_RETURN_ERR(RESOURCE); /* can't change tag if not mapped */

  ea->translation[index].tag = tag;

  if (ea->depth != -1) { /* after call to AM_SetExpectedResources we must update compressed table */
    ea->perProcInfo[ea->translation[index].id].tag = tag;
    }

  return AM_OK;
  }
/* ------------------------------------------------------------------------------------ */
extern int AM_GetTranslationName(ep_t ea, int i, en_t *gan) {
  AMUDP_CHECKINIT();
  if (!ea || !gan) AMUDP_RETURN_ERR(BAD_ARG);
  if (i < 0 || i >= AMUDP_MAX_NUMTRANSLATIONS) AMUDP_RETURN_ERR(BAD_ARG);
  if (!ea->translation[i].inuse) AMUDP_RETURN_ERR(RESOURCE);

  (*gan) = ea->translation[i].name; 
  return AM_OK;
  }
/* ------------------------------------------------------------------------------------ */
extern int AM_SetExpectedResources(ep_t ea, int n_endpoints, int n_outstanding_requests) {
  AMUDP_CHECKINIT();
  if (!ea) AMUDP_RETURN_ERR(BAD_ARG);
  if (ea->depth != -1) AMUDP_RETURN_ERR(RESOURCE); /* it's an error to call AM_SetExpectedResources again */
  /* n_endpoints ignored : P is set as we Map translations */
  /*if (n_endpoints < 1 || n_endpoints >= AMUDP_MAX_NUMTRANSLATIONS) AMUDP_RETURN_ERR(BAD_ARG);*/
  if (n_outstanding_requests < 1 || n_outstanding_requests > AMUDP_MAX_NETWORKDEPTH) AMUDP_RETURN_ERR(BAD_ARG);

  ea->depth = n_outstanding_requests;
  ea->PD = ea->P * ea->depth;

  if (!AMUDP_AllocateEndpointBuffers(ea)) AMUDP_RETURN_ERR(RESOURCE);

  /*  compact a copy of the translation table into our perproc info array */
  {
    int procid = 0;
    int i;
    for (i=0; i < AMUDP_MAX_NUMTRANSLATIONS; i++) {
      if (ea->translation[i].inuse) {
        ea->perProcInfo[procid].remoteName = ea->translation[i].name;
        ea->perProcInfo[procid].tag = ea->translation[i].tag;
        ea->translation[i].id = (uint8_t)procid;
        procid++;
        if (procid == ea->P) break; /*  should have all of them now */
        }
      }
    }

  #ifdef UETH
  { /* need to init the request/reply destinations */
    for (int inst = 0; inst < ea->depth; inst++) {
      for (int procid = 0; procid < ea->P; procid++) {
        amudp_buf_t *reqbuf = GET_REQ_BUF(ea, procid, inst);
        amudp_buf_t *repbuf = GET_REP_BUF(ea, procid, inst);
        if (ueth_set_packet_destination(reqbuf, &ea->perProcInfo[procid].remoteName) != UETH_OK)
          AMUDP_RETURN_ERRFR(RESOURCE, AM_SetExpectedResources, "ueth_set_packet_destination failed");
        if (ueth_set_packet_destination(repbuf, &ea->perProcInfo[procid].remoteName) != UETH_OK)
          AMUDP_RETURN_ERRFR(RESOURCE, AM_SetExpectedResources, "ueth_set_packet_destination failed");
      }
    }
  }
  #endif

  return AM_OK;
  }
/*------------------------------------------------------------------------------------
 * Handler management
 *------------------------------------------------------------------------------------ */
extern int _AM_SetHandler(ep_t ea, handler_t handler, amudp_handler_fn_t function) {
  AMUDP_CHECKINIT();
  if (!ea || !function) AMUDP_RETURN_ERR(BAD_ARG);
  if (AMUDP_BADHANDLERVAL(handler)) AMUDP_RETURN_ERR(BAD_ARG);

  ea->handler[handler] = function;
  return AM_OK;
  }
/* ------------------------------------------------------------------------------------ */
extern int _AM_SetHandlerAny(ep_t ea, handler_t *handler, amudp_handler_fn_t function) {
  int i;
  AMUDP_CHECKINIT();
  if (!ea || !function || !handler) AMUDP_RETURN_ERR(BAD_ARG);

  for (i = 1 ; i < AMUDP_MAX_NUMHANDLERS; i++) {
    if (ea->handler[i] == amudp_unused_handler) { /* find unused entry */
      ea->handler[i] = function;
      *handler = (handler_t)i;
      return AM_OK;
      }
    }
  AMUDP_RETURN_ERR(RESOURCE); /* all in use */
  }
/*------------------------------------------------------------------------------------
 * Event management
 *------------------------------------------------------------------------------------ */
extern int AM_GetEventMask(eb_t eb, int *mask) {
  AMUDP_CHECKINIT();
  if (!eb) AMUDP_RETURN_ERR(BAD_ARG);

  *mask = eb->event_mask;
  return AM_OK;
  }
/* ------------------------------------------------------------------------------------ */
extern int AM_SetEventMask(eb_t eb, int mask) {
  AMUDP_CHECKINIT();
  if (!eb) AMUDP_RETURN_ERR(BAD_ARG);
  if (mask < 0 || ((amudp_eventmask_t)mask) >= AM_NUMEVENTMASKS) AMUDP_RETURN_ERR(BAD_ARG);

  eb->event_mask = (uint8_t)mask;
  return AM_OK;
  }
/* ------------------------------------------------------------------------------------ */
extern int AM_WaitSema(eb_t eb) {
  int retval;
  AMUDP_CHECKINIT();
  if (!eb) AMUDP_RETURN_ERR(BAD_ARG);
  
  if (eb->event_mask == AM_NOEVENTS) 
    abort(); /* it's an error to block when the mask is not set - will never return */

  /* block here until a message arrives */
  retval = AMUDP_Block(eb);
  if (retval != AM_OK) eb->event_mask = AM_NOEVENTS;

  /* it's not clear from the spec whether we should poll here, 
     but it's probably safer to do so than not */
  if (retval == AM_OK) 
    retval = AM_Poll(eb);

  AMUDP_RETURN(retval);
  }
/*------------------------------------------------------------------------------------
 * Message interrogation
 *------------------------------------------------------------------------------------ */
extern int AM_GetSourceEndpoint(void *token, en_t *gan) {
  AMUDP_CHECKINIT();
  if (!token || !gan) AMUDP_RETURN_ERR(BAD_ARG);

  *gan = ((amudp_buf_t *)token)->status.sourceAddr;
  return AM_OK;
  }
/* ------------------------------------------------------------------------------------ */
extern int AMUDP_GetSourceId(void *token, int *srcid) {
  AMUDP_CHECKINIT();
  if (!token || !srcid) AMUDP_RETURN_ERR(BAD_ARG);

  *srcid = ((amudp_buf_t *)token)->status.sourceId;
  return AM_OK;
}
/* ------------------------------------------------------------------------------------ */
extern int AM_GetDestEndpoint(void *token, ep_t *endp) {
  AMUDP_CHECKINIT();
  if (!token || !endp) AMUDP_RETURN_ERR(BAD_ARG);

  *endp = ((amudp_buf_t *)token)->status.dest;
  return AM_OK;
  }
/* ------------------------------------------------------------------------------------ */
extern int AM_GetMsgTag(void *token, tag_t *tagp) {
  AMUDP_CHECKINIT();
  if (!token || !tagp) AMUDP_RETURN_ERR(BAD_ARG);
  
  if (((amudp_buf_t *)token)->status.bulkBuffer)
    *tagp = ((amudp_buf_t *)token)->status.bulkBuffer->Msg.tag;
  else *tagp = ((amudp_buf_t *)token)->Msg.tag;
  return AM_OK;
  }
/*------------------------------------------------------------------------------------
 * Statistics API
 *------------------------------------------------------------------------------------ */
extern int AMUDP_GetEndpointStatistics(ep_t ep, amudp_stats_t *stats) { /* called by user to get statistics */
  AMUDP_CHECKINIT();
  if (!ep || !stats) AMUDP_RETURN_ERR(BAD_ARG);
  memcpy(stats, &ep->stats, sizeof(amudp_stats_t));
  return AM_OK;
  }
/* ------------------------------------------------------------------------------------ */
extern int AMUDP_ResetEndpointStatistics(ep_t ep) {
  AMUDP_CHECKINIT();
  if (!ep) AMUDP_RETURN_ERR(BAD_ARG);
  ep->stats = AMUDP_initial_stats;
  return AM_OK;
  }
/* ------------------------------------------------------------------------------------ */
extern int AMUDP_AggregateStatistics(amudp_stats_t *runningsum, amudp_stats_t *newvalues) {
  int category;
  AMUDP_CHECKINIT();
  if (!runningsum || !newvalues) AMUDP_RETURN_ERR(BAD_ARG);
  for (category = 0; category < amudp_NumCategories; category++) {
    runningsum->RequestsSent[category] += newvalues->RequestsSent[category];
    runningsum->RequestsRetransmitted[category] += newvalues->RequestsRetransmitted[category];
    runningsum->RequestsReceived[category] += newvalues->RequestsReceived[category];
    runningsum->RepliesSent[category] += newvalues->RepliesSent[category];
    runningsum->RepliesRetransmitted[category] += newvalues->RepliesRetransmitted[category];
    runningsum->RepliesReceived[category] += newvalues->RepliesReceived[category];

    runningsum->DataBytesSent[category] += newvalues->DataBytesSent[category];
  }
  runningsum->ReturnedMessages += newvalues->ReturnedMessages;
  #if AMUDP_COLLECT_LATENCY_STATS
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
extern const char *AMUDP_DumpStatistics(FILE *fp, amudp_stats_t *stats, int globalAnalysis) {
  static char msg[4096];
  int64_t packetssent; 
  int64_t requestsSent = 0; 
  int64_t requestsRetransmitted = 0; 
  int64_t requestsReceived = 0; 
  int64_t repliesSent = 0; 
  int64_t repliesRetransmitted = 0; 
  int64_t repliesReceived = 0; 
  int64_t dataBytesSent = 0; 
  int64_t UDPIPheaderbytes; 
  int category;

  AMUDP_assert(amudp_Initialized);
  AMUDP_assert(stats != NULL);

  for (category = 0; category < amudp_NumCategories; category++) {
    requestsSent += stats->RequestsSent[category];
    requestsRetransmitted += stats->RequestsRetransmitted[category];
    requestsReceived += stats->RequestsReceived[category];
    repliesSent += stats->RepliesSent[category];
    repliesRetransmitted += stats->RepliesRetransmitted[category];
    repliesReceived += stats->RepliesReceived[category];

    dataBytesSent += stats->DataBytesSent[category];
  }

  packetssent = (requestsSent + requestsRetransmitted + 
                 repliesSent  + repliesRetransmitted);

#ifdef UETH 
  UDPIPheaderbytes = 0; /* n/a- all headers already included */
#else
  UDPIPheaderbytes = packetssent * (20 /* IP header */ + 8  /* UDP header*/);
#endif

  /* batch lines together to improve chance of output together */
  sprintf(msg, 
    " Requests: %8i sent, %4i retransmitted, %8i received\n"
    " Replies:  %8i sent, %4i retransmitted, %8i received\n"
    " Returned messages:  %8i\n"
  #if AMUDP_COLLECT_LATENCY_STATS
    "Latency (request sent to reply received): \n"
    " min: %8i microseconds\n"
    " max: %8i microseconds\n"
    " avg: %8i microseconds\n"
  #endif

    "Message Breakdown:        Requests     Replies   Average data payload\n"
    " Small  (<=%5i bytes)   %8i    %8i        %9.3f bytes\n"
    " Medium (<=%5i bytes)   %8i    %8i        %9.3f bytes\n"
    " Large  (<=%5i bytes)   %8i    %8i        %9.3f bytes\n"
#if !USE_TRUE_BULK_XFERS
    " ^^^^^ (Statistics for Large refer to internal fragments)\n"
#endif
    " Total                                                %9.3f bytes\n"

    "Data bytes sent:      %9i bytes\n"
    "Total bytes sent:     %9i bytes (incl. AM overhead)\n"
    "Bandwidth overhead:   %9.2f%%\n"        
    "Average packet size:  %9.3f bytes (incl. AM & transport-layer overhead)\n"
    , 
    (int)requestsSent, (int)requestsRetransmitted, (int)requestsReceived,
    (int)repliesSent, (int)repliesRetransmitted, (int)repliesReceived,
    stats->ReturnedMessages,
  #if AMUDP_COLLECT_LATENCY_STATS
    (int)ticks2us(stats->RequestMinLatency),
    (int)ticks2us(stats->RequestMaxLatency),
    (requestsSent>0?(int)(ticks2us(stats->RequestSumLatency) / requestsSent):-1),
  #endif

    /* Message breakdown */
    (int)(AMUDP_MAX_SHORT*sizeof(int)),
      (int)stats->RequestsSent[amudp_Short], (int)stats->RepliesSent[amudp_Short], 
      (stats->RequestsSent[amudp_Short]+stats->RepliesSent[amudp_Short] > 0 ?
        ((float)(int64_t)stats->DataBytesSent[amudp_Short]) / 
        ((float)(int64_t)(stats->RequestsSent[amudp_Short]+stats->RepliesSent[amudp_Short])) : 0.0),
    (int)(AMUDP_MAX_SHORT*sizeof(int) + AMUDP_MAX_MEDIUM),
      (int)stats->RequestsSent[amudp_Medium], (int)stats->RepliesSent[amudp_Medium], 
      (stats->RequestsSent[amudp_Medium]+stats->RepliesSent[amudp_Medium] > 0 ?
        ((float)(int64_t)stats->DataBytesSent[amudp_Medium]) / 
        ((float)(int64_t)(stats->RequestsSent[amudp_Medium]+stats->RepliesSent[amudp_Medium])) : 0.0),
#if USE_TRUE_BULK_XFERS
    (int)(AMUDP_MAX_SHORT*sizeof(int) + AMUDP_MAX_LONG),
#else
    (int)(AMUDP_MAX_SHORT*sizeof(int) + AMUDP_MAX_MEDIUM),
#endif
      (int)stats->RequestsSent[amudp_Long], (int)stats->RepliesSent[amudp_Long], 
      (stats->RequestsSent[amudp_Long]+stats->RepliesSent[amudp_Long] > 0 ?
        ((float)(int64_t)stats->DataBytesSent[amudp_Long]) / 
        ((float)(int64_t)(stats->RequestsSent[amudp_Long]+stats->RepliesSent[amudp_Long])) : 0.0),

    /* avg data payload */
    (requestsSent+repliesSent > 0 ?
      ((float)(int64_t)dataBytesSent) / ((float)(int64_t)(requestsSent+repliesSent))
      : 0.0),

    (int)dataBytesSent,
    (int)(stats->TotalBytesSent+UDPIPheaderbytes),
    /* bandwidth overhead */
    (stats->TotalBytesSent > 0 ?
      100.0*((float)(int64_t)(stats->TotalBytesSent + UDPIPheaderbytes - dataBytesSent)) / 
      ((float)(int64_t)stats->TotalBytesSent+UDPIPheaderbytes) 
      : 0.0),
    /* avg packet size */
    (packetssent > 0 ?
      ((float)((int64_t)stats->TotalBytesSent+UDPIPheaderbytes)) / ((float)(int64_t)packetssent)
      : 0.0)
    );

  if (globalAnalysis) {
    int64_t reqsent = requestsSent + requestsRetransmitted;
    int64_t reqlost = reqsent - requestsReceived;
    int64_t repsent = repliesSent + repliesRetransmitted;
    int64_t replost = repsent - repliesReceived;
    int64_t packetslost = reqlost + replost;
    int64_t extra_rexmits = requestsRetransmitted - reqlost;
    #define APPEND_PERCENT(num, denom)                                           \
      if (num > 0) sprintf(msg+strlen(msg), "  (%6.3f%%)\n", (100.0*num)/denom); \
      else strcat(msg, "\n")
    sprintf(msg+strlen(msg), "Requests lost:        %9i", (int)reqlost);
    APPEND_PERCENT(reqlost, reqsent);
    sprintf(msg+strlen(msg), "Replies lost:         %9i", (int)replost);
    APPEND_PERCENT(replost, repsent);
    sprintf(msg+strlen(msg), "Total packets lost:   %9i", (int)packetslost);
    APPEND_PERCENT(packetslost, packetssent);
    sprintf(msg+strlen(msg), "Useless retransmits:  %9i", (int)extra_rexmits);
    APPEND_PERCENT(extra_rexmits, requestsRetransmitted);
  } 

  if (fp != NULL) fprintf(fp, "%s", msg);
  return msg;
  }
/* ------------------------------------------------------------------------------------ */


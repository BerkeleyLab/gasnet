/*  $Archive:: /Ti/GASNet/mpi-conduit/gasnet_core.c                       $
 *     $Date: 2003/06/29 01:48:12 $
 * $Revision: 1.31 $
 * Description: GASNet MPI conduit Implementation
 * Copyright 2002, Dan Bonachea <bonachea@cs.berkeley.edu>
 * Terms of use are as specified in license.txt
 */

#include <gasnet.h>
#include <gasnet_internal.h>
#include <gasnet_handler.h>
#include <gasnet_core_internal.h>

#include <ammpi_spmd.h>

#include <errno.h>
#include <unistd.h>
#include <signal.h>
#include <sched.h>

GASNETI_IDENT(gasnetc_IdentString_Version, "$GASNetCoreLibraryVersion: " GASNET_CORE_VERSION_STR " $");
GASNETI_IDENT(gasnetc_IdentString_ConduitName, "$GASNetConduitName: " GASNET_CORE_NAME_STR " $");

gasnet_handlerentry_t const *gasnetc_get_handlertable();

gasnet_node_t gasnetc_mynode = (gasnet_node_t)-1;
gasnet_node_t gasnetc_nodes = 0;

uintptr_t gasnetc_MaxLocalSegmentSize = 0;
uintptr_t gasnetc_MaxGlobalSegmentSize = 0;

gasnet_seginfo_t *gasnetc_seginfo = NULL;
eb_t gasnetc_bundle;
ep_t gasnetc_endpoint;

static int gasnetc_init_done = 0; /*  true after init */
static int gasnetc_attach_done = 0; /*  true after attach */

gasneti_mutex_t gasnetc_AMlock = GASNETI_MUTEX_INITIALIZER; /*  protect access to AMMPI */

#ifdef GASNETC_HSL_ERRCHECK
  extern void gasnetc_enteringHandler_hook();
  extern void gasnetc_leavingHandler_hook();

  /* check a call is legally outside an NIS or HSL */
  void gasnetc_checkcallNIS();
  void gasnetc_checkcallHSL();
  #define CHECKCALLNIS() gasnetc_checkcallNIS()
  #define CHECKCALLHSL() gasnetc_checkcallHSL()
#else
  #define CHECKCALLNIS()
  #define CHECKCALLHSL()
#endif

/* ------------------------------------------------------------------------------------ */
/*
  Initialization
  ==============
*/
/* called at startup to check configuration sanity */
static void gasnetc_check_config() {
  assert(GASNET_MAXNODES <= AMMPI_MAX_SPMDPROCS);
  assert(AMMPI_MAX_NUMHANDLERS >= 256);
  assert(AMMPI_MAX_SEGLENGTH == (uintptr_t)-1);

  assert(GASNET_ERR_NOT_INIT == AM_ERR_NOT_INIT);
  assert(GASNET_ERR_RESOURCE == AM_ERR_RESOURCE);
  assert(GASNET_ERR_BAD_ARG  == AM_ERR_BAD_ARG);
}

void gasnetc_bootstrapExchange(void *src, size_t len, void *dest) {
  GASNETI_AM_SAFE_NORETURN(AMMPI_SPMDAllGather(src, dest, len));
}
void gasnetc_bootstrapBroadcast(void *src, size_t len, void *dest, int rootnode) {
  assert(gasnetc_nodes > 0 && gasnetc_mynode < gasnetc_nodes);
  if (gasnetc_mynode == rootnode) memcpy(dest, src, len);
  GASNETI_AM_SAFE_NORETURN(AMMPI_SPMDBroadcast(dest, len, rootnode));
}

#define INITERR(type, reason) do {                                      \
   if (gasneti_VerboseErrors) {                                         \
     fprintf(stderr, "GASNet initialization encountered an error: %s\n" \
      "  in %s at %s:%i\n",                                             \
      #reason, GASNETI_CURRENT_FUNCTION,  __FILE__, __LINE__);          \
   }                                                                    \
   retval = GASNET_ERR_ ## type;                                        \
   goto done;                                                           \
 } while (0)

static int gasnetc_init(int *argc, char ***argv) {
  int retval = GASNET_OK;
  int networkdepth = 0;

  AMLOCK();
    if (gasnetc_init_done) 
      INITERR(NOT_INIT, "GASNet already initialized");
    gasnetc_init_done = 1; /* enable early to allow tracing */

    /*  check system sanity */
    gasnetc_check_config();

    if (getenv("GASNET_FREEZE")) gasneti_freezeForDebugger();

    #if DEBUG_VERBOSE
      /* note - can't call trace macros during gasnet_init because trace system not yet initialized */
      fprintf(stderr,"gasnetc_init(): about to spawn...\n"); fflush(stderr);
    #endif

    /*  choose network depth */
    if (getenv("GASNET_NETWORKDEPTH")) { 
       networkdepth = atoi(getenv("GASNET_NETWORKDEPTH"));
    }

    AMMPI_VerboseErrors = gasneti_VerboseErrors;

    /*  perform job spawn */
    retval = AMMPI_SPMDStartup(argc, argv, networkdepth, NULL, &gasnetc_bundle, &gasnetc_endpoint);
    if (retval != AM_OK) INITERR(RESOURCE, "AMMPI_SPMDStartup() failed");

    gasnetc_mynode = AMMPI_SPMDMyProc();
    gasnetc_nodes = AMMPI_SPMDNumProcs();

    /* a number of MPI job spawners fail to propagate the environment to all compute nodes */
    /* do this before trace_init to make sure it gets right environment */
    gasneti_setupGlobalEnvironment(gasnetc_nodes, gasnetc_mynode, 
                                   gasnetc_bootstrapExchange, gasnetc_bootstrapBroadcast);

    /* enable tracing */
    gasneti_trace_init();

    #if DEBUG_VERBOSE
      fprintf(stderr,"gasnetc_init(): spawn successful - node %i/%i starting...\n", 
        gasnetc_mynode, gasnetc_nodes); fflush(stderr);
    #endif

    #if defined(GASNET_SEGMENT_FAST) || defined(GASNET_SEGMENT_LARGE)
      gasneti_segmentInit(&gasnetc_MaxLocalSegmentSize, 
                          &gasnetc_MaxGlobalSegmentSize,
                          (uintptr_t)-1,
                          gasnetc_nodes,
                          &gasnetc_bootstrapExchange);
    #elif defined(GASNET_SEGMENT_EVERYTHING)
      gasnetc_MaxLocalSegmentSize =  (uintptr_t)-1;
      gasnetc_MaxGlobalSegmentSize = (uintptr_t)-1;
    #else
      #error Bad segment config
    #endif

  AMUNLOCK();

  assert(retval == GASNET_OK);
  return retval;

done: /*  error return while locked */
  AMUNLOCK();
  GASNETI_RETURN(retval);
}

/* ------------------------------------------------------------------------------------ */
extern int gasnet_init(int *argc, char ***argv) {
  int retval = gasnetc_init(argc, argv);
  if (retval != GASNET_OK) GASNETI_RETURN(retval);
  #if 0
    /* called within gasnet_init to allow init tracing */
    gasneti_trace_init();
  #endif
  return GASNET_OK;
}

extern uintptr_t gasnetc_getMaxLocalSegmentSize() {
  GASNETC_CHECKINIT();
  return gasnetc_MaxLocalSegmentSize;
}
extern uintptr_t gasnetc_getMaxGlobalSegmentSize() {
  GASNETC_CHECKINIT();
  return gasnetc_MaxGlobalSegmentSize;
}
/* ------------------------------------------------------------------------------------ */
static char checkuniqhandler[256] = { 0 };
static int gasnetc_reghandlers(gasnet_handlerentry_t *table, int numentries,
                               int lowlimit, int highlimit,
                               int dontcare, int *numregistered) {
  int i;
  *numregistered = 0;
  for (i = 0; i < numentries; i++) {
    int newindex;

    if (table[i].index && dontcare) continue;
    else if (table[i].index) newindex = table[i].index;
    else { /* deterministic assignment of dontcare indexes */
      for (newindex = lowlimit; newindex <= highlimit; newindex++) {
        if (!checkuniqhandler[newindex]) break;
      }
      if (newindex > highlimit) {
        char s[255];
        sprintf(s,"Too many handlers. (limit=%i)", highlimit - lowlimit + 1);
        GASNETI_RETURN_ERRR(BAD_ARG, s);
      }
    }

    /*  ensure handlers fall into the proper range of pre-assigned values */
    if (newindex < lowlimit || newindex > highlimit) {
      char s[255];
      sprintf(s, "handler index (%i) out of range [%i..%i]", newindex, lowlimit, highlimit);
      GASNETI_RETURN_ERRR(BAD_ARG, s);
    }

    /* discover duplicates */
    if (checkuniqhandler[newindex] != 0) 
      GASNETI_RETURN_ERRR(BAD_ARG, "handler index not unique");
    checkuniqhandler[newindex] = 1;

    /* register the handler */
    if (AM_SetHandler(gasnetc_endpoint, (handler_t)newindex, table[i].fnptr) != AM_OK) 
      GASNETI_RETURN_ERRR(RESOURCE, "AM_SetHandler() failed while registering handlers");

    if (dontcare) table[i].index = newindex;
    (*numregistered)++;
  }
  return GASNET_OK;
}
/* ------------------------------------------------------------------------------------ */
extern int gasnetc_attach(gasnet_handlerentry_t *table, int numentries,
                          uintptr_t segsize, uintptr_t minheapoffset) {
  int retval = GASNET_OK;
  void *segbase = NULL;
  
  GASNETI_TRACE_PRINTF(C,("gasnetc_attach(table (%i entries), segsize=%lu, minheapoffset=%lu)",
                          numentries, (unsigned long)segsize, (unsigned long)minheapoffset));
  AMLOCK();
    if (!gasnetc_init_done) 
      INITERR(NOT_INIT, "GASNet attach called before init");
    if (gasnetc_attach_done) 
      INITERR(NOT_INIT, "GASNet already attached");

    /* pause to make sure all nodes have called attach 
       if a node calls gasnet_exit() between init/attach, then this allows us
       to process the AMMPI_SPMD control messages required for job shutdown
     */
    retval = AMMPI_SPMDBarrier();
    if (retval != AM_OK) INITERR(RESOURCE, "AMMPI_SPMDBarrier() failed");

    /*  check argument sanity */
    #if defined(GASNET_SEGMENT_FAST) || defined(GASNET_SEGMENT_LARGE)
      if ((segsize % GASNET_PAGESIZE) != 0) 
        INITERR(BAD_ARG, "segsize not page-aligned");
      if (segsize > gasnetc_getMaxLocalSegmentSize()) 
        INITERR(BAD_ARG, "segsize too large");
      if ((minheapoffset % GASNET_PAGESIZE) != 0) /* round up the minheapoffset to page sz */
        minheapoffset = ((minheapoffset / GASNET_PAGESIZE) + 1) * GASNET_PAGESIZE;
    #else
      segsize = 0;
      minheapoffset = 0;
    #endif

    /* ------------------------------------------------------------------------------------ */
    /*  register handlers */
    { /*  core API handlers */
      gasnet_handlerentry_t *ctable = (gasnet_handlerentry_t *)gasnetc_get_handlertable();
      int len = 0;
      int numreg = 0;
      assert(ctable);
      while (ctable[len].fnptr) len++; /* calc len */
      if (gasnetc_reghandlers(ctable, len, 1, 63, 0, &numreg) != GASNET_OK)
        INITERR(RESOURCE,"Error registering core API handlers");
      assert(numreg == len);
    }

    { /*  extended API handlers */
      gasnet_handlerentry_t *etable = (gasnet_handlerentry_t *)gasnete_get_handlertable();
      int len = 0;
      int numreg = 0;
      assert(etable);
      while (etable[len].fnptr) len++; /* calc len */
      if (gasnetc_reghandlers(etable, len, 64, 127, 0, &numreg) != GASNET_OK)
        INITERR(RESOURCE,"Error registering extended API handlers");
      assert(numreg == len);
    }

    if (table) { /*  client handlers */
      int numreg1 = 0;
      int numreg2 = 0;

      /*  first pass - assign all fixed-index handlers */
      if (gasnetc_reghandlers(table, numentries, 128, 255, 0, &numreg1) != GASNET_OK)
        INITERR(RESOURCE,"Error registering fixed-index client handlers");

      /*  second pass - fill in dontcare-index handlers */
      if (gasnetc_reghandlers(table, numentries, 128, 255, 1, &numreg2) != GASNET_OK)
        INITERR(RESOURCE,"Error registering fixed-index client handlers");

      assert(numreg1 + numreg2 == numentries);
    }

    /* ------------------------------------------------------------------------------------ */
    /*  register fatal signal handlers */

    /* catch fatal signals and convert to SIGQUIT */
    gasneti_registerSignalHandlers(gasneti_defaultSignalHandler);

    /* ------------------------------------------------------------------------------------ */
    /*  register segment  */

    /* use gasneti_malloc_inhandler during bootstrapping because we can't assume the 
       hold/resume interrupts functions are operational yet */
    gasnetc_seginfo = (gasnet_seginfo_t *)gasneti_malloc_inhandler(gasnetc_nodes*sizeof(gasnet_seginfo_t));

    #if defined(GASNET_SEGMENT_FAST) || defined(GASNET_SEGMENT_LARGE)
      gasneti_segmentAttach(segsize, minheapoffset, gasnetc_seginfo, &gasnetc_bootstrapExchange);
    #else /* GASNET_SEGMENT_EVERYTHING */
      { int i;
        for (i=0;i<gasnetc_nodes;i++) {
          gasnetc_seginfo[i].addr = (void *)0;
          gasnetc_seginfo[i].size = (uintptr_t)-1;
        }
      }
    #endif
    segbase = gasnetc_seginfo[gasnetc_mynode].addr;
    segsize = gasnetc_seginfo[gasnetc_mynode].size;

    /*  AMMPI allows arbitrary registration with no further action  */
    if (segsize) {
      retval = AM_SetSeg(gasnetc_endpoint, segbase, segsize);
      if (retval != AM_OK) INITERR(RESOURCE, "AM_SetSeg() failed");
    }
    #ifdef GASNETC_HSL_ERRCHECK
      GASNETI_AM_SAFE(AMMPI_SetHandlerCallbacks(gasnetc_endpoint,
        gasnetc_enteringHandler_hook, gasnetc_leavingHandler_hook));
    #endif
    /* ------------------------------------------------------------------------------------ */
    /*  primary attach complete */
    gasnetc_attach_done = 1;
    retval = AMMPI_SPMDBarrier();
    if (retval != AM_OK) INITERR(RESOURCE, "AMMPI_SPMDBarrier() failed");
  AMUNLOCK();

  GASNETI_TRACE_PRINTF(C,("gasnetc_attach(): primary attach complete\n"));

  gasnete_init(); /* init the extended API */

  /* ensure extended API is initialized across nodes */
  GASNETI_AM_SAFE(AMMPI_SPMDBarrier()); 
  
  assert(retval == GASNET_OK);
  return retval;

done: /*  error return while locked */
  AMUNLOCK();
  GASNETI_RETURN(retval);
}
/* ------------------------------------------------------------------------------------ */
extern void gasnetc_exit(int exitcode) {
  /* once we start a shutdown, ignore all future SIGQUIT signals or we risk reentrancy */
  gasneti_reghandler(SIGQUIT, SIG_IGN);

  {  /* ensure only one thread ever continues past this point */
    static gasneti_mutex_t exit_lock = GASNETI_MUTEX_INITIALIZER;
    gasneti_mutex_lock(&exit_lock);
  }

  GASNETI_TRACE_PRINTF(C,("gasnet_exit(%i)\n", exitcode));

  gasneti_trace_finish();

  if (fflush(stdout)) 
    gasneti_fatalerror("failed to flush stdout in gasnetc_exit: %s", strerror(errno));
  if (fflush(stderr)) 
    gasneti_fatalerror("failed to flush stderr in gasnetc_exit: %s", strerror(errno));
  gasneti_sched_yield();
  sleep(1); /* pause to ensure everyone has written trace if this is a collective exit */

  AMMPI_SPMDExit(exitcode);
  abort();
}

/* ------------------------------------------------------------------------------------ */
/*
  Job Environment Queries
  =======================
*/
extern int gasnetc_getSegmentInfo(gasnet_seginfo_t *seginfo_table, int numentries) {
  GASNETC_CHECKATTACH();
  CHECKCALLNIS();
  assert(gasnetc_seginfo && seginfo_table);
  if (!gasnetc_attach_done) GASNETI_RETURN_ERR(NOT_INIT);
  if (numentries < gasnetc_nodes) GASNETI_RETURN_ERR(BAD_ARG);
  memset(seginfo_table, 0, numentries*sizeof(gasnet_seginfo_t));
  memcpy(seginfo_table, gasnetc_seginfo, numentries*sizeof(gasnet_seginfo_t));
  return GASNET_OK;
}

/* ------------------------------------------------------------------------------------ */
/*
  Misc. Active Message Functions
  ==============================
*/
extern int gasnetc_AMGetMsgSource(gasnet_token_t token, gasnet_node_t *srcindex) {
  int retval;
  int sourceid;
  GASNETC_CHECKATTACH();
  if (!token) GASNETI_RETURN_ERRR(BAD_ARG,"bad token");
  if (!srcindex) GASNETI_RETURN_ERRR(BAD_ARG,"bad src ptr");

  retval = GASNETI_AM_SAFE_NORETURN(AMMPI_GetSourceId(token, &sourceid));

  if (retval) {
    assert(sourceid >= 0 && sourceid < gasnetc_nodes);
    *srcindex = sourceid;
    return GASNET_OK;
  } else GASNETI_RETURN_ERR(RESOURCE);
}

extern int gasnetc_AMPoll() {
  int retval;
  GASNETC_CHECKATTACH();
  CHECKCALLNIS();
  AMLOCK();
    retval = GASNETI_AM_SAFE_NORETURN(AM_Poll(gasnetc_bundle));
  AMUNLOCK();
  if (retval) return GASNET_OK;
  else GASNETI_RETURN_ERR(RESOURCE);
}

/* ------------------------------------------------------------------------------------ */
/*
  Active Message Request Functions
  ================================
*/

extern int gasnetc_AMRequestShortM( 
                            gasnet_node_t dest,       /* destination node */
                            gasnet_handler_t handler, /* index into destination endpoint's handler table */ 
                            int numargs, ...) {
  int retval;
  va_list argptr;
  GASNETC_CHECKATTACH();
  CHECKCALLNIS();
  if_pf (dest >= gasnetc_nodes) GASNETI_RETURN_ERRR(BAD_ARG,"node index too high");
  GASNETI_TRACE_AMREQUESTSHORT(dest,handler,numargs);
  va_start(argptr, numargs); /*  pass in last argument */
    AMLOCK();
      retval = GASNETI_AM_SAFE_NORETURN(
               AMMPI_RequestVA(gasnetc_endpoint, dest, handler, 
                               numargs, argptr));
    AMUNLOCK();
  va_end(argptr);
  if (retval) return GASNET_OK;
  else GASNETI_RETURN_ERR(RESOURCE);
}

extern int gasnetc_AMRequestMediumM( 
                            gasnet_node_t dest,      /* destination node */
                            gasnet_handler_t handler, /* index into destination endpoint's handler table */ 
                            void *source_addr, size_t nbytes,   /* data payload */
                            int numargs, ...) {
  int retval;
  va_list argptr;
  GASNETC_CHECKATTACH();
  CHECKCALLNIS();
  if_pf (dest >= gasnetc_nodes) GASNETI_RETURN_ERRR(BAD_ARG,"node index too high");
  GASNETI_TRACE_AMREQUESTMEDIUM(dest,handler,source_addr,nbytes,numargs);
  va_start(argptr, numargs); /*  pass in last argument */
    AMLOCK();
      retval = GASNETI_AM_SAFE_NORETURN(
               AMMPI_RequestIVA(gasnetc_endpoint, dest, handler, 
                                source_addr, nbytes, 
                                numargs, argptr));
    AMUNLOCK();
  va_end(argptr);
  if (retval) return GASNET_OK;
  else GASNETI_RETURN_ERR(RESOURCE);
}

extern int gasnetc_AMRequestLongM( gasnet_node_t dest,        /* destination node */
                            gasnet_handler_t handler, /* index into destination endpoint's handler table */ 
                            void *source_addr, size_t nbytes,   /* data payload */
                            void *dest_addr,                    /* data destination on destination node */
                            int numargs, ...) {
  int retval;
  uintptr_t dest_offset;
  va_list argptr;
  GASNETC_CHECKATTACH();
  CHECKCALLNIS();

  gasnetc_boundscheck(dest, dest_addr, nbytes);
  if_pf (dest >= gasnetc_nodes) GASNETI_RETURN_ERRR(BAD_ARG,"node index too high");
  if_pf (((uintptr_t)dest_addr) < ((uintptr_t)gasnetc_seginfo[dest].addr) ||
         ((uintptr_t)dest_addr) + nbytes > 
           ((uintptr_t)gasnetc_seginfo[dest].addr) + gasnetc_seginfo[dest].size) 
         GASNETI_RETURN_ERRR(BAD_ARG,"destination address out of segment range");
  dest_offset = ((uintptr_t)dest_addr) - ((uintptr_t)gasnetc_seginfo[dest].addr);

  GASNETI_TRACE_AMREQUESTLONG(dest,handler,source_addr,nbytes,dest_addr,numargs);
  va_start(argptr, numargs); /*  pass in last argument */
    AMLOCK();
      retval = GASNETI_AM_SAFE_NORETURN(
               AMMPI_RequestXferVA(gasnetc_endpoint, dest, handler, 
                                   source_addr, nbytes, 
                                   dest_offset, 0,
                                   numargs, argptr));
    AMUNLOCK();
  va_end(argptr);
  if (retval) return GASNET_OK;
  else GASNETI_RETURN_ERR(RESOURCE);
}

extern int gasnetc_AMReplyShortM( 
                            gasnet_token_t token,       /* token provided on handler entry */
                            gasnet_handler_t handler, /* index into destination endpoint's handler table */ 
                            int numargs, ...) {
  int retval;
  va_list argptr;
  CHECKCALLHSL();
  GASNETI_TRACE_AMREPLYSHORT(token,handler,numargs);
  va_start(argptr, numargs); /*  pass in last argument */
    retval = GASNETI_AM_SAFE_NORETURN(
              AMMPI_ReplyVA(token, handler, numargs, argptr));
  va_end(argptr);
  if (retval) return GASNET_OK;
  else GASNETI_RETURN_ERR(RESOURCE);
}

extern int gasnetc_AMReplyMediumM( 
                            gasnet_token_t token,       /* token provided on handler entry */
                            gasnet_handler_t handler, /* index into destination endpoint's handler table */ 
                            void *source_addr, size_t nbytes,   /* data payload */
                            int numargs, ...) {
  int retval;
  va_list argptr;
  CHECKCALLHSL();
  GASNETI_TRACE_AMREPLYMEDIUM(token,handler,source_addr,nbytes,numargs);
  va_start(argptr, numargs); /*  pass in last argument */
    retval = GASNETI_AM_SAFE_NORETURN(
              AMMPI_ReplyIVA(token, handler, source_addr, nbytes, numargs, argptr));
  va_end(argptr);
  if (retval) return GASNET_OK;
  else GASNETI_RETURN_ERR(RESOURCE);
}

extern int gasnetc_AMReplyLongM( 
                            gasnet_token_t token,       /* token provided on handler entry */
                            gasnet_handler_t handler, /* index into destination endpoint's handler table */ 
                            void *source_addr, size_t nbytes,   /* data payload */
                            void *dest_addr,                    /* data destination on destination node */
                            int numargs, ...) {
  int retval;
  uintptr_t dest_offset;
  gasnet_node_t dest;
  va_list argptr;
  
  CHECKCALLHSL();
  retval = gasnet_AMGetMsgSource(token, &dest);
  if (retval != GASNET_OK) GASNETI_RETURN(retval);
  gasnetc_boundscheck(dest, dest_addr, nbytes);
  if_pf (dest >= gasnetc_nodes) GASNETI_RETURN_ERRR(BAD_ARG,"node index too high");
  if_pf (((uintptr_t)dest_addr) < ((uintptr_t)gasnetc_seginfo[dest].addr) ||
         ((uintptr_t)dest_addr) + nbytes > 
           ((uintptr_t)gasnetc_seginfo[dest].addr) + gasnetc_seginfo[dest].size) 
         GASNETI_RETURN_ERRR(BAD_ARG,"destination address out of segment range");
  dest_offset = ((uintptr_t)dest_addr) - ((uintptr_t)gasnetc_seginfo[dest].addr);

  GASNETI_TRACE_AMREPLYLONG(token,handler,source_addr,nbytes,dest_addr,numargs);
  va_start(argptr, numargs); /*  pass in last argument */
    retval = GASNETI_AM_SAFE_NORETURN(
              AMMPI_ReplyXferVA(token, handler, source_addr, nbytes, dest_offset, numargs, argptr));
  va_end(argptr);
  if (retval) return GASNET_OK;
  else GASNETI_RETURN_ERR(RESOURCE);
}

/* ------------------------------------------------------------------------------------ */
/*
  No-interrupt sections
  =====================
*/
/* AMMPI does not use interrupts, but we provide an optional error-checking implementation of 
   handler-safe locks to assist in debugging client code
 */

#ifdef GASNETC_HSL_ERRCHECK
  typedef struct { /* pre-thread HSL err-checking info */
    gasnet_hsl_t *locksheld;
    int interruptsdisabled;
    int inhandler;
    int64_t NIStimestamp;
  } gasnetc_hsl_errcheckinfo_t;
  static gasnetc_hsl_errcheckinfo_t _info_init = { NULL, 0, 0 };

  #ifdef GASNETI_THREADS
    static pthread_key_t gasnetc_hsl_errcheckinfo; /*  pthread thread-specific ptr to our info (or NULL for a thread never-seen before) */
    static int gasnetc_hsl_errcheckinfo_firsttime = 1;
    static gasnetc_hsl_errcheckinfo_t *gasnetc_get_errcheckinfo() {
      gasnetc_hsl_errcheckinfo_t *info;
      if (gasnetc_hsl_errcheckinfo_firsttime) { 
        int retval = pthread_key_create(&gasnetc_hsl_errcheckinfo, NULL);
        if (retval) gasneti_fatalerror("Failure in pthread_key_create()=%s",strerror(retval));
        gasnetc_hsl_errcheckinfo_firsttime = 0;
      }
      info = pthread_getspecific(gasnetc_hsl_errcheckinfo);
      if_pt (info) return info;

      /*  first time we've seen this thread - need to set it up */
      { int retval;
        info = (gasnetc_hsl_errcheckinfo_t *)malloc(sizeof(gasnetc_hsl_errcheckinfo_t));
        memcpy(info, &_info_init, sizeof(gasnetc_hsl_errcheckinfo_t));
        retval = pthread_setspecific(gasnetc_hsl_errcheckinfo, info);
        assert(!retval);
        return info;
      }
    }
  #else
    static gasnetc_hsl_errcheckinfo_t *gasnetc_get_errcheckinfo() {
      return &_info_init;
    }
  #endif


  extern void gasnetc_hold_interrupts() {
    GASNETC_CHECKATTACH();
    { gasnetc_hsl_errcheckinfo_t *info = gasnetc_get_errcheckinfo();
      if (info->inhandler)
        gasneti_fatalerror("HSL USAGE VIOLATION: tried to disable interrupts while running a handler");
      if (info->locksheld)
        gasneti_fatalerror("HSL USAGE VIOLATION: tried to disable interrupts while holding an HSL");
      if (info->interruptsdisabled)
        gasneti_fatalerror("HSL USAGE VIOLATION: tried to disable interrupts when they were already disabled");
      info->interruptsdisabled = 1;
      info->NIStimestamp = gasneti_getMicrosecondTimeStamp();
    }
  }
  extern void gasnetc_resume_interrupts() {
    GASNETC_CHECKATTACH();
    { gasnetc_hsl_errcheckinfo_t *info = gasnetc_get_errcheckinfo();
      if (info->inhandler)
        gasneti_fatalerror("HSL USAGE VIOLATION: tried to resume interrupts while running a handler");
      if (info->locksheld)
        gasneti_fatalerror("HSL USAGE VIOLATION: tried to resume interrupts while holding an HSL");
      if (!info->interruptsdisabled)
        gasneti_fatalerror("HSL USAGE VIOLATION: tried to resume interrupts when they were not disabled");
      { float NIStime = (float)(gasneti_getMicrosecondTimeStamp() - info->NIStimestamp);
        if (NIStime > GASNETC_NISTIMEOUT_WARNING_THRESHOLD) {
          fprintf(stderr,"HSL USAGE WARNING: held a no-interrupt section for a long interval (%8.3f sec)\n", NIStime/1000000.0);
          fflush(stderr);
        }
      }
      info->interruptsdisabled = 0;
    }
  }

  void gasnetc_checkinit() {
    if (!gasnetc_init_done)
      gasneti_fatalerror("Illegal call to GASNet before gasnet_init() initialization");
  }
  void gasnetc_checkattach() {
    if (!gasnetc_attach_done)
      gasneti_fatalerror("Illegal call to GASNet before gasnet_attach() initialization");
  }
  void gasnetc_checkcallNIS() {
    gasnetc_hsl_errcheckinfo_t *info = gasnetc_get_errcheckinfo();
    if (info->interruptsdisabled)
      gasneti_fatalerror("Illegal call to GASNet within a No-Interrupt Section");
    if (info->inhandler)
      gasneti_fatalerror("Illegal call to GASNet within a No-Interrupt Section (imposed by handler context)");
    gasnetc_checkcallHSL();
  }
  void gasnetc_checkcallHSL() {
    gasnetc_hsl_errcheckinfo_t *info = gasnetc_get_errcheckinfo();
    if (info->locksheld)
      gasneti_fatalerror("Illegal call to GASNet while holding a Handler-Safe Lock");
  }
#endif

/* ------------------------------------------------------------------------------------ */
/*
  Handler-safe locks
  ==================
*/

extern void gasnetc_hsl_init   (gasnet_hsl_t *hsl) {
  GASNETC_CHECKATTACH();
  #ifdef GASNETC_HSL_ERRCHECK
  {
    if (hsl->tag == GASNETC_HSL_ERRCHECK_TAGINIT)
        gasneti_fatalerror("HSL USAGE VIOLATION: tried to gasnet_hsl_init() a statically-initialized HSL");
  #if 0
    /* this causes false errors in Titanium, because object destructors aren't implemented */
    if (hsl->tag == GASNETC_HSL_ERRCHECK_TAGDYN)
        gasneti_fatalerror("HSL USAGE VIOLATION: tried to gasnet_hsl_init() a previously-initialized HSL (or one you forgot to destroy)");
  #endif
    hsl->tag = GASNETC_HSL_ERRCHECK_TAGDYN;
    hsl->next = NULL;
    hsl->islocked = 0;
  }
  #endif
  #ifdef GASNETI_THREADS
  { int retval = pthread_mutex_init(&(hsl->lock), NULL);
    if (retval) 
      gasneti_fatalerror("In gasnetc_hsl_init(), pthread_mutex_init()=%s",strerror(retval));
  }
  #endif
}

extern void gasnetc_hsl_destroy(gasnet_hsl_t *hsl) {
  GASNETC_CHECKATTACH();
  #ifdef GASNETC_HSL_ERRCHECK
  {
    if (hsl->tag != GASNETC_HSL_ERRCHECK_TAGINIT && hsl->tag != GASNETC_HSL_ERRCHECK_TAGDYN)
        gasneti_fatalerror("HSL USAGE VIOLATION: tried to gasnet_hsl_destroy() an uninitialized HSL");
    if (hsl->islocked)
        gasneti_fatalerror("HSL USAGE VIOLATION: tried to gasnet_hsl_destroy() a locked HSL");
    hsl->tag = 0;
    assert(!hsl->next);
  }
  #endif
  #ifdef GASNETI_THREADS
  { int retval = pthread_mutex_destroy(&(hsl->lock));
    if (retval) 
      gasneti_fatalerror("In gasnetc_hsl_destroy(), pthread_mutex_destroy()=%s",strerror(retval));
  }
  #endif
}

extern void gasnetc_hsl_lock   (gasnet_hsl_t *hsl) {
  GASNETC_CHECKATTACH();
  #ifdef GASNETC_HSL_ERRCHECK
  { gasnetc_hsl_errcheckinfo_t *info = gasnetc_get_errcheckinfo();
    gasnet_hsl_t *heldhsl = info->locksheld;
    if (hsl->tag != GASNETC_HSL_ERRCHECK_TAGINIT && hsl->tag != GASNETC_HSL_ERRCHECK_TAGDYN)
        gasneti_fatalerror("HSL USAGE VIOLATION: tried to gasnet_hsl_lock() an uninitialized HSL");
    while (heldhsl) {
      if (heldhsl == hsl)
        gasneti_fatalerror("HSL USAGE VIOLATION: tried to recursively gasnet_hsl_lock() an HSL");
      heldhsl = heldhsl->next;
    }
  }
  #endif

  #ifdef GASNETI_THREADS
  { int retval; 
    #if defined(STATS) || defined(TRACE)
      gasneti_stattime_t startlock = GASNETI_STATTIME_NOW_IFENABLED(L);
    #endif
    #if GASNETC_HSL_SPINLOCK
      do {
        retval = pthread_mutex_trylock(&(hsl->lock));
      } while (retval == EBUSY);
    #else
        retval = pthread_mutex_lock(&(hsl->lock));
    #endif
    if (retval) 
      gasneti_fatalerror("In gasnetc_hsl_lock(), pthread_mutex_lock()=%s",strerror(retval));
    #if defined(STATS) || defined(TRACE)
      hsl->acquiretime = GASNETI_STATTIME_NOW_IFENABLED(L);
      GASNETI_TRACE_EVENT_TIME(L, HSL_LOCK, hsl->acquiretime-startlock);
    #endif
  }
  #elif defined(STATS) || defined(TRACE)
    hsl->acquiretime = GASNETI_STATTIME_NOW_IFENABLED(L);
    GASNETI_TRACE_EVENT_TIME(L, HSL_LOCK, 0);
  #endif


  #ifdef GASNETC_HSL_ERRCHECK
  { gasnetc_hsl_errcheckinfo_t *info = gasnetc_get_errcheckinfo();
    hsl->islocked = 1;
    hsl->next = info->locksheld;
    info->locksheld = hsl;
    hsl->timestamp = gasneti_getMicrosecondTimeStamp();
  }
  #endif
}

extern void gasnetc_hsl_unlock (gasnet_hsl_t *hsl) {
  GASNETC_CHECKATTACH();
  #ifdef GASNETC_HSL_ERRCHECK
  { gasnetc_hsl_errcheckinfo_t *info = gasnetc_get_errcheckinfo();
    gasnet_hsl_t *heldhsl = info->locksheld;
    if (hsl->tag != GASNETC_HSL_ERRCHECK_TAGINIT && hsl->tag != GASNETC_HSL_ERRCHECK_TAGDYN)
        gasneti_fatalerror("HSL USAGE VIOLATION: tried to gasnet_hsl_unlock() an uninitialized HSL");
    while (heldhsl) {
      if (heldhsl == hsl) break;
      heldhsl = heldhsl->next;
    }
    if (!heldhsl)
        gasneti_fatalerror("HSL USAGE VIOLATION: tried to gasnet_hsl_unlock() an HSL I didn't own");
    if (info->locksheld != hsl)
        gasneti_fatalerror("HSL USAGE VIOLATION: tried to gasnet_hsl_unlock() an HSL out of order");
    { float NIStime = (float)(gasneti_getMicrosecondTimeStamp() - hsl->timestamp);
      if (NIStime > GASNETC_NISTIMEOUT_WARNING_THRESHOLD) {
        fprintf(stderr,"HSL USAGE WARNING: held an HSL for a long interval (%8.3f sec)\n", NIStime/1000000.0);
        fflush(stderr);
      }
    }
    hsl->islocked = 0;
    info->locksheld = hsl->next;
  }
  #endif

  GASNETI_TRACE_EVENT_TIME(L, HSL_UNLOCK, GASNETI_STATTIME_NOW()-hsl->acquiretime);

  #ifdef GASNETI_THREADS
  { int retval = pthread_mutex_unlock(&(hsl->lock));
    if (retval) 
      gasneti_fatalerror("In gasnetc_hsl_unlock(), pthread_mutex_unlock()=%s",strerror(retval));
  }
  #endif
}

#ifdef GASNETC_HSL_ERRCHECK
  /* called when entering/leaving handler - also called when entering/leaving AM_Reply call */
  extern void gasnetc_enteringHandler_hook() {
    gasnetc_hsl_errcheckinfo_t *info = gasnetc_get_errcheckinfo();
    assert(!info->inhandler);
    if (info->locksheld)
        gasneti_fatalerror("HSL USAGE VIOLATION: tried to make a GASNet network call while holding an HSL");
    if (info->interruptsdisabled)
        gasneti_fatalerror("HSL USAGE VIOLATION: tried to make a GASNet network call with interrupts disabled");
    info->inhandler = 1;
  }
  extern void gasnetc_leavingHandler_hook() {
    gasnetc_hsl_errcheckinfo_t *info = gasnetc_get_errcheckinfo();
    assert(info->inhandler);
    assert(!info->interruptsdisabled);
    if (info->locksheld)
        gasneti_fatalerror("HSL USAGE VIOLATION: tried to exit a handler while holding an HSL");
    info->inhandler = 0;
  }
#endif

/* ------------------------------------------------------------------------------------ */
/*
  Private Handlers:
  ================
*/
static gasnet_handlerentry_t const gasnetc_handlers[] = {
  /* ptr-width independent handlers */

  /* ptr-width dependent handlers */

  { 0, NULL }
};

gasnet_handlerentry_t const *gasnetc_get_handlertable() {
  return gasnetc_handlers;
}

/* ------------------------------------------------------------------------------------ */

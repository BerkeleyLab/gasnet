/*  $Archive:: /Ti/GASNet/mpi-conduit/gasnet_core.c                       $
 *     $Date: 2004/01/05 05:01:25 $
 * $Revision: 1.4 $
 * Description: GASNet MPI conduit Implementation
 * Copyright 2002, Dan Bonachea <bonachea@cs.berkeley.edu>
 * Terms of use are as specified in license.txt
 */

#include <gasnet.h>
#include <gasnet_internal.h>
#include <gasnet_handler.h>
#include <gasnet_core_internal.h>

#include <amudp_spmd.h>

#include <errno.h>
#include <unistd.h>
#include <signal.h>
#include <sched.h>
#include <ctype.h>

GASNETI_IDENT(gasnetc_IdentString_Version, "$GASNetCoreLibraryVersion: " GASNET_CORE_VERSION_STR " $");
GASNETI_IDENT(gasnetc_IdentString_ConduitName, "$GASNetConduitName: " GASNET_CORE_NAME_STR " $");

gasnet_handlerentry_t const *gasnetc_get_handlertable();
static void gasnetc_atexit(void);
static void gasnetc_traceoutput(int);

gasnet_node_t gasnetc_mynode = (gasnet_node_t)-1;
gasnet_node_t gasnetc_nodes = 0;

uintptr_t gasnetc_MaxLocalSegmentSize = 0;
uintptr_t gasnetc_MaxGlobalSegmentSize = 0;

gasnet_seginfo_t *gasnetc_seginfo = NULL;
eb_t gasnetc_bundle;
ep_t gasnetc_endpoint;

gasneti_mutex_t gasnetc_AMlock = GASNETI_MUTEX_INITIALIZER; /*  protect access to AMUDP */


#if defined(GASNET_CSPAWN_CMD)
  #define GASNETC_DEFAULT_SPAWNFN C
  GASNETI_IDENT(AMUDP_DEFAULT_CSPAWNCMD_IDENT_STRING, "$GASNetCSpawnCommand: " GASNET_CSPAWN_CMD " $");
#elif defined(REXEC)
  #define GASNETC_DEFAULT_SPAWNFN R
#else /* AMUDP implicit ssh startup */
  #define GASNETC_DEFAULT_SPAWNFN S
#endif
GASNETI_IDENT(AMUDP_DEFAULT_SPAWNFN_IDENT_STRING, "$GASNetDefaultSpawnFunction: " _STRINGIFY(GASNETC_DEFAULT_SPAWNFN) " $");

/* ------------------------------------------------------------------------------------ */
/*
  Initialization
  ==============
*/
/* called at startup to check configuration sanity */
static void gasnetc_check_config() {
  gasneti_assert(GASNET_MAXNODES <= AMUDP_MAX_SPMDPROCS);
  gasneti_assert(AMUDP_MAX_NUMHANDLERS >= 256);
  gasneti_assert(AMUDP_MAX_SEGLENGTH == (uintptr_t)-1);

  gasneti_assert(GASNET_ERR_NOT_INIT == AM_ERR_NOT_INIT);
  gasneti_assert(GASNET_ERR_RESOURCE == AM_ERR_RESOURCE);
  gasneti_assert(GASNET_ERR_BAD_ARG  == AM_ERR_BAD_ARG);
}

#define gasnetc_bootstrapBarrier()                                  \
   AM_ASSERT_LOCKED(); /* need this because SPMDBarrier may poll */ \
   if (!GASNETI_AM_SAFE_NORETURN(AMUDP_SPMDBarrier()))              \
    gasneti_fatalerror("failure in gasnetc_bootstrapBarrier()")

void gasnetc_bootstrapExchange(void *src, size_t len, void *dest) {
  GASNETI_AM_SAFE_NORETURN(AMUDP_SPMDAllGather(src, dest, len));
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

  /*  check system sanity */
  gasnetc_check_config();

  /* --------- begin Master code ------------ */
  if (!AMUDP_SPMDIsWorker(*argv)) { 
    /* assume we're an implicit master 
       (we don't currently support explicit workers spawned 
        without using the AMUDP SPMD API)   
     */
    int networkdepth = 0;
    int num_nodes;
    int i;
    char spawnfn = (_STRINGIFY(GASNETC_DEFAULT_SPAWNFN))[0];
    amudp_spawnfn_t fp = (amudp_spawnfn_t)NULL;

    /*  choose network depth */
    if (getenv("GASNET_NETWORKDEPTH")) { 
       networkdepth = atoi(getenv("GASNET_NETWORKDEPTH"));
    }

    #if defined(GASNET_CSPAWN_CMD)
      if (!getenv("GASNET_CSPAWN_CMD")) /* set configure default cspawn cmd */
        putenv("GASNET_CSPAWN_CMD="GASNET_CSPAWN_CMD);
    #endif

    /* parse node count from command line */
    if (*argc < 2) {
      fprintf(stderr, "GASNet: Missing parallel node count\n");
      fprintf(stderr, "GASNet: Specify node count as first argument, or use upcrun/tcrun spawner script to start job\n");
      fprintf(stderr, "GASNet: Usage '%s <num_nodes> {program arguments}'\n", (*argv)[0]);
      exit(-1);
    }
    /*
     * argv[1] is number of nodes; argv[0] is program name; argv is
     * list of arguments including program name and number of nodes.
     * We need to remove argv[1] when the argument array is passed
     * to the tic_main().
     */
    num_nodes = atoi((*argv)[1]);
    if (num_nodes < 1) {
      fprintf (stderr, "GASNet: Invalid number of nodes: %s\n", (*argv)[1]);
      fprintf (stderr, "GASNet: Usage '%s <num_nodes> {program arguments}'\n", (*argv)[0]);
      exit (1);
    }

    /* remove the num_nodes argument */
    for (i = 1; i < (*argc)-1; i++) {
      (*argv)[i] = (*argv)[i+1];
    }
    (*argv)[(*argc)-1] = NULL;
    (*argc)--;

    /* get spawnfn */
    if (getenv("GASNET_SPAWNFN")) { 
      spawnfn = *getenv("GASNET_SPAWNFN");
    }
    { /* ensure we pass the effective spawnfn to worker env */
      char spawnstr[255];
      sprintf(spawnstr,"GASNET_SPAWNFN=%c",toupper(spawnfn));
      putenv(spawnstr);
    }

    for (i=0; AMUDP_Spawnfn_Desc[i].abbrev; i++) {
      if (toupper(spawnfn) == toupper(AMUDP_Spawnfn_Desc[i].abbrev)) {
        fp = AMUDP_Spawnfn_Desc[i].fnptr;
        break;
      }
    }

    if (!fp) {
      fprintf (stderr, "Tic: Invalid spawn function specified in GASNET_SPAWNFN\n");
      fprintf (stderr, "Tic: The following mechanisms are available:\n");
      for (i=0; AMUDP_Spawnfn_Desc[i].abbrev; i++) {
        fprintf(stderr, "    '%c'  %s\n",  
              toupper(AMUDP_Spawnfn_Desc[i].abbrev), AMUDP_Spawnfn_Desc[i].desc);
      }
      exit (1);
    }

    #if GASNET_DEBUG_VERBOSE
      /* note - can't call trace macros during gasnet_init because trace system not yet initialized */
      fprintf(stderr,"gasnetc_init(): about to spawn...\n"); fflush(stderr);
    #endif

    retval = AMUDP_SPMDStartup(argc, argv, 
      num_nodes, networkdepth, fp,
      NULL, &gasnetc_bundle, &gasnetc_endpoint);
    /* master startup should never return */
    gasneti_fatalerror("master AMUDP_SPMDStartup() failed");
  }

  /* --------- begin Worker code ------------ */
  AMLOCK();
    if (gasneti_init_done) 
      INITERR(NOT_INIT, "GASNet already initialized");
    gasneti_init_done = 1; /* enable early to allow tracing */

    if (getenv("GASNET_FREEZE")) gasneti_freezeForDebugger();

    AMUDP_VerboseErrors = gasneti_VerboseErrors;
    #if !GASNET_DEBUG_VERBOSE
      AMUDP_SilentMode = 1;
    #endif
    AMUDP_SPMDkillmyprocess = gasneti_killmyprocess;

    /*  perform job spawn */
    retval = AMUDP_SPMDStartup(argc, argv, 
      0, 0, NULL, /* dummies */
      NULL, &gasnetc_bundle, &gasnetc_endpoint);
    if (retval != AM_OK) INITERR(RESOURCE, "slave AMUDP_SPMDStartup() failed");

    gasnetc_mynode = AMUDP_SPMDMyProc();
    gasnetc_nodes = AMUDP_SPMDNumProcs();

    /* enable tracing */
    gasneti_trace_init();
    GASNETI_AM_SAFE(AMUDP_SPMDSetExitCallback(gasnetc_traceoutput));

    /* for local spawn, assume we want to wait-block */
    if (getenv("GASNET_SPAWNFN") && *getenv("GASNET_SPAWNFN") == 'L') { 
      GASNETI_TRACE_PRINTF(C,("setting gasnet_set_waitmode(GASNET_WAIT_BLOCK) for localhost spawn"));
      gasnet_set_waitmode(GASNET_WAIT_BLOCK);
    }

    #if GASNET_DEBUG_VERBOSE
      fprintf(stderr,"gasnetc_init(): spawn successful - node %i/%i starting...\n", 
        gasnetc_mynode, gasnetc_nodes); fflush(stderr);
    #endif

    #if GASNET_SEGMENT_FAST || GASNET_SEGMENT_LARGE
      gasneti_segmentInit(&gasnetc_MaxLocalSegmentSize, 
                          &gasnetc_MaxGlobalSegmentSize,
                          (uintptr_t)-1,
                          gasnetc_nodes,
                          &gasnetc_bootstrapExchange);
    #elif GASNET_SEGMENT_EVERYTHING
      gasnetc_MaxLocalSegmentSize =  (uintptr_t)-1;
      gasnetc_MaxGlobalSegmentSize = (uintptr_t)-1;
    #else
      #error Bad segment config
    #endif

  AMUNLOCK();

  gasneti_assert(retval == GASNET_OK);
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
  GASNETI_CHECKINIT();
  return gasnetc_MaxLocalSegmentSize;
}
extern uintptr_t gasnetc_getMaxGlobalSegmentSize() {
  GASNETI_CHECKINIT();
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
    if (!gasneti_init_done) 
      INITERR(NOT_INIT, "GASNet attach called before init");
    if (gasneti_attach_done) 
      INITERR(NOT_INIT, "GASNet already attached");

    /* pause to make sure all nodes have called attach 
       if a node calls gasnet_exit() between init/attach, then this allows us
       to process the AMUDP_SPMD control messages required for job shutdown
     */
    gasnetc_bootstrapBarrier();

    /*  check argument sanity */
    #if GASNET_SEGMENT_FAST || GASNET_SEGMENT_LARGE
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
      gasneti_assert(ctable);
      while (ctable[len].fnptr) len++; /* calc len */
      if (gasnetc_reghandlers(ctable, len, 1, 63, 0, &numreg) != GASNET_OK)
        INITERR(RESOURCE,"Error registering core API handlers");
      gasneti_assert(numreg == len);
    }

    { /*  extended API handlers */
      gasnet_handlerentry_t *etable = (gasnet_handlerentry_t *)gasnete_get_handlertable();
      int len = 0;
      int numreg = 0;
      gasneti_assert(etable);
      while (etable[len].fnptr) len++; /* calc len */
      if (gasnetc_reghandlers(etable, len, 64, 127, 0, &numreg) != GASNET_OK)
        INITERR(RESOURCE,"Error registering extended API handlers");
      gasneti_assert(numreg == len);
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

      gasneti_assert(numreg1 + numreg2 == numentries);
    }

    /* ------------------------------------------------------------------------------------ */
    /*  register fatal signal handlers */

    /* catch fatal signals and convert to SIGQUIT */
    gasneti_registerSignalHandlers(gasneti_defaultSignalHandler);

    atexit(gasnetc_atexit);

    /* ------------------------------------------------------------------------------------ */
    /*  register segment  */

    gasnetc_seginfo = (gasnet_seginfo_t *)gasneti_malloc(gasnetc_nodes*sizeof(gasnet_seginfo_t));

    #if GASNET_SEGMENT_FAST || GASNET_SEGMENT_LARGE
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

    /*  AMUDP allows arbitrary registration with no further action  */
    if (segsize) {
      retval = AM_SetSeg(gasnetc_endpoint, segbase, segsize);
      if (retval != AM_OK) INITERR(RESOURCE, "AM_SetSeg() failed");
    }

    /* ------------------------------------------------------------------------------------ */
    /*  primary attach complete */
    gasneti_attach_done = 1;
    gasnetc_bootstrapBarrier();
  AMUNLOCK();

  GASNETI_TRACE_PRINTF(C,("gasnetc_attach(): primary attach complete\n"));

  gasnete_init(); /* init the extended API */

  /* ensure extended API is initialized across nodes */
  AMLOCK();
  gasnetc_bootstrapBarrier();
  AMUNLOCK();

  gasneti_assert(retval == GASNET_OK);
  return retval;

done: /*  error return while locked */
  AMUNLOCK();
  GASNETI_RETURN(retval);
}
/* ------------------------------------------------------------------------------------ */
static void gasnetc_atexit(void) {
  gasnetc_exit(0);
}
static int gasnetc_exitcalled = 0;
static void gasnetc_traceoutput(int exitcode) {
  if (!gasnetc_exitcalled)
    gasneti_trace_finish();
}
extern void gasnetc_trace_finish() {
  /* dump AMUDP statistics */
  if (GASNETI_STATS_ENABLED(C) ) {
    const char *statdump;
    int isglobal = 0;
    amudp_stats_t stats = AMUDP_initial_stats;

    if (isglobal) {
      /* TODO: tricky bit - if this exit is collective, we can display more interesting and useful
         statistics with collective cooperation. But there's no easy way to know for sure whether
         the exit is collective.
       */

      /* TODO: want a bootstrap barrier here for global stats to ensure network is 
         quiescent, but no way to do this unless we know things are collective */

      if (gasnet_mynode() != 0) {
        AMLOCK();
          GASNETI_AM_SAFE_NORETURN(AMUDP_GetEndpointStatistics(gasnetc_endpoint, &stats)); /* get statistics */
        AMUNLOCK();
        /* TODO: send stats to zero */
      } else {
        amudp_stats_t *remote_stats = NULL;
        /* TODO: gather stats from all nodes */
        AMLOCK();
          GASNETI_AM_SAFE_NORETURN(AMUDP_AggregateStatistics(&stats, remote_stats));
        AMUNLOCK();
      }
    } else {
      AMLOCK();
        GASNETI_AM_SAFE_NORETURN(AMUDP_GetEndpointStatistics(gasnetc_endpoint, &stats)); /* get statistics */
      AMUNLOCK();
    }

    if (gasnet_mynode() == 0 || !isglobal) {
      GASNETI_STATS_PRINTF(C,("--------------------------------------------------------------------------------"));
      GASNETI_STATS_PRINTF(C,("AMUDP Statistics:"));
      if (!isglobal)
        GASNETI_STATS_PRINTF(C,("*** AMUDP stat dump reflects only local node info, because gasnet_exit is non-collective ***"));
      AMLOCK();
        statdump = AMUDP_DumpStatistics(NULL, &stats, isglobal);
        GASNETI_STATS_PRINTF(C,("\n%s",statdump)); /* note, dump has embedded '%' chars */
      AMUNLOCK();
      GASNETI_STATS_PRINTF(C,("--------------------------------------------------------------------------------"));
    }
  }
}
extern void gasnetc_fatalsignal_callback(int sig) {
  if (gasnetc_exitcalled) {
  /* if we get a fatal signal during exit, it's almost certainly a signal-safety or MPI shutdown
     issue and not a client bug, so don't bother reporting it verbosely, 
     just die silently
   */
    #if 0
      abort();
    #endif
    gasneti_killmyprocess(1);
  }
}

extern void gasnetc_exit(int exitcode) {
  /* once we start a shutdown, ignore all future SIGQUIT signals or we risk reentrancy */
  gasneti_reghandler(SIGQUIT, SIG_IGN);
  gasnetc_exitcalled = 1;

  {  /* ensure only one thread ever continues past this point */
    static gasneti_mutex_t exit_lock = GASNETI_MUTEX_INITIALIZER;
    gasneti_mutex_lock(&exit_lock);
  }

  GASNETI_TRACE_PRINTF(C,("gasnet_exit(%i)\n", exitcode));

  if (fflush(stdout)) 
    gasneti_fatalerror("failed to flush stdout in gasnetc_exit: %s", strerror(errno));
  if (fflush(stderr)) 
    gasneti_fatalerror("failed to flush stderr in gasnetc_exit: %s", strerror(errno));
  gasneti_trace_finish();
  gasneti_sched_yield();

  AMUDP_SPMDExit(exitcode);
  abort();
}

/* ------------------------------------------------------------------------------------ */
/*
  Job Environment Queries
  =======================
*/
extern int gasnetc_getSegmentInfo(gasnet_seginfo_t *seginfo_table, int numentries) {
  GASNETI_CHECKATTACH();
  gasneti_assert(seginfo_table);
  gasneti_memcheck(gasnetc_seginfo);
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
  GASNETI_CHECKATTACH();
  if (!token) GASNETI_RETURN_ERRR(BAD_ARG,"bad token");
  if (!srcindex) GASNETI_RETURN_ERRR(BAD_ARG,"bad src ptr");

  retval = GASNETI_AM_SAFE_NORETURN(AMUDP_GetSourceId(token, &sourceid));

  if (retval) {
    gasneti_assert(sourceid >= 0 && sourceid < gasnetc_nodes);
    *srcindex = sourceid;
    return GASNET_OK;
  } else GASNETI_RETURN_ERR(RESOURCE);
}

extern int gasnetc_AMPoll() {
  int retval;
  GASNETI_CHECKATTACH();
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
  GASNETI_CHECKATTACH();
  if_pf (dest >= gasnetc_nodes) GASNETI_RETURN_ERRR(BAD_ARG,"node index too high");
  GASNETI_TRACE_AMREQUESTSHORT(dest,handler,numargs);
  va_start(argptr, numargs); /*  pass in last argument */
    AMLOCK();
      retval = GASNETI_AM_SAFE_NORETURN(
               AMUDP_RequestVA(gasnetc_endpoint, dest, handler, 
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
  GASNETI_CHECKATTACH();
  if_pf (dest >= gasnetc_nodes) GASNETI_RETURN_ERRR(BAD_ARG,"node index too high");
  GASNETI_TRACE_AMREQUESTMEDIUM(dest,handler,source_addr,nbytes,numargs);
  va_start(argptr, numargs); /*  pass in last argument */
    AMLOCK();
      retval = GASNETI_AM_SAFE_NORETURN(
               AMUDP_RequestIVA(gasnetc_endpoint, dest, handler, 
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
  GASNETI_CHECKATTACH();

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
               AMUDP_RequestXferVA(gasnetc_endpoint, dest, handler, 
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
  GASNETI_TRACE_AMREPLYSHORT(token,handler,numargs);
  va_start(argptr, numargs); /*  pass in last argument */
    AM_ASSERT_LOCKED();
    retval = GASNETI_AM_SAFE_NORETURN(
              AMUDP_ReplyVA(token, handler, numargs, argptr));
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
  GASNETI_TRACE_AMREPLYMEDIUM(token,handler,source_addr,nbytes,numargs);
  va_start(argptr, numargs); /*  pass in last argument */
    AM_ASSERT_LOCKED();
    retval = GASNETI_AM_SAFE_NORETURN(
              AMUDP_ReplyIVA(token, handler, source_addr, nbytes, numargs, argptr));
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
    AM_ASSERT_LOCKED();
    retval = GASNETI_AM_SAFE_NORETURN(
              AMUDP_ReplyXferVA(token, handler, source_addr, nbytes, dest_offset, numargs, argptr));
  va_end(argptr);
  if (retval) return GASNET_OK;
  else GASNETI_RETURN_ERR(RESOURCE);
}

/* ------------------------------------------------------------------------------------ */
/*
  No-interrupt sections
  =====================
  This section is only required for conduits that may use interrupt-based handler dispatch
  See the GASNet spec and http://www.cs.berkeley.edu/~bonachea/upc/gasnet.html for
    philosophy and hints on efficiently implementing no-interrupt sections
  Note: the extended-ref implementation provides a thread-specific void* within the 
    gasnete_threaddata_t data structure which is reserved for use by the core 
    (and this is one place you'll probably want to use it)
*/
#if GASNETC_USE_INTERRUPTS
  #error interrupts not implemented
  extern void gasnetc_hold_interrupts() {
    GASNETI_CHECKATTACH();
    /* add code here to disable handler interrupts for _this_ thread */
  }
  extern void gasnetc_resume_interrupts() {
    GASNETI_CHECKATTACH();
    /* add code here to re-enable handler interrupts for _this_ thread */
  }
#endif

/* ------------------------------------------------------------------------------------ */
/*
  Handler-safe locks
  ==================
*/
#if !GASNETC_NULL_HSL
extern void gasnetc_hsl_init   (gasnet_hsl_t *hsl) {
  GASNETI_CHECKATTACH();
  gasneti_mutex_init(&(hsl->lock));

  #if GASNETC_USE_INTERRUPTS
    /* add code here to init conduit-specific HSL state */
    #error interrupts not implemented
  #endif
}

extern void gasnetc_hsl_destroy(gasnet_hsl_t *hsl) {
  GASNETI_CHECKATTACH();
  gasneti_mutex_destroy(&(hsl->lock));

  #if GASNETC_USE_INTERRUPTS
    /* add code here to cleanup conduit-specific HSL state */
    #error interrupts not implemented
  #endif
}

extern void gasnetc_hsl_lock   (gasnet_hsl_t *hsl) {
  GASNETI_CHECKATTACH();

  { int retval; 
    #if GASNETI_STATS_OR_TRACE
      gasneti_stattime_t startlock = GASNETI_STATTIME_NOW_IFENABLED(L);
    #endif
    #if GASNETC_HSL_SPINLOCK
      while (gasneti_mutex_trylock(&(hsl->lock)) == EBUSY) { }
    #else
      gasneti_mutex_lock(&(hsl->lock));
    #endif
    #if GASNETI_STATS_OR_TRACE
      hsl->acquiretime = GASNETI_STATTIME_NOW_IFENABLED(L);
      GASNETI_TRACE_EVENT_TIME(L, HSL_LOCK, hsl->acquiretime-startlock);
    #endif
  }

  #if GASNETC_USE_INTERRUPTS
    /* conduits with interrupt-based handler dispatch need to add code here to 
       disable handler interrupts on _this_ thread, (if this is the outermost
       HSL lock acquire and we're not inside an enclosing no-interrupt section)
     */
    #error interrupts not implemented
  #endif
}

extern void gasnetc_hsl_unlock (gasnet_hsl_t *hsl) {
  GASNETI_CHECKATTACH();

  #if GASNETC_USE_INTERRUPTS
    /* conduits with interrupt-based handler dispatch need to add code here to 
       re-enable handler interrupts on _this_ thread, (if this is the outermost
       HSL lock release and we're not inside an enclosing no-interrupt section)
     */
    #error interrupts not implemented
  #endif

  GASNETI_TRACE_EVENT_TIME(L, HSL_UNLOCK, GASNETI_STATTIME_NOW()-hsl->acquiretime);

  gasneti_mutex_unlock(&(hsl->lock));
}
#endif
/* ------------------------------------------------------------------------------------ */
/*
  Private Handlers:
  ================
  see mpi-conduit and extended-ref for examples on how to declare AM handlers here
  (for internal conduit use in bootstrapping, job management, etc.)
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

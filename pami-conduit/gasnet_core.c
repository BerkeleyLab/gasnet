/*   $Source: /Users/kamil/work/gasnet-cvs2/gasnet/pami-conduit/gasnet_core.c,v $
 *     $Date: 2012/03/16 07:46:11 $
 * $Revision: 1.1.2.7 $
 * Description: GASNet PAMI conduit Implementation
 * Copyright 2012, Lawrence Berkeley National Laboratory
 * Terms of use are as specified in license.txt
 */

#include <gasnet_internal.h>
#include <gasnet_handler.h>
#include <gasnet_core_internal.h>

#include <errno.h>
#include <unistd.h>
#include <signal.h>

GASNETI_IDENT(gasnetc_IdentString_Version, "$GASNetCoreLibraryVersion: " GASNET_CORE_VERSION_STR " $");
GASNETI_IDENT(gasnetc_IdentString_Name,    "$GASNetCoreLibraryName: " GASNET_CORE_NAME_STR " $");

gasnet_handlerentry_t const *gasnetc_get_handlertable(void);

static int gasnetc_exit_init(void);

gasneti_handler_fn_t gasnetc_handler[GASNETC_MAX_NUMHANDLERS]; /* handler table (recommended impl) */

/* Global Data */
pami_client_t      gasnetc_pami_client;
pami_context_t     gasnetc_context; /* XXX: More than one */
pami_geometry_t    gasnetc_world_geom;

/* ------------------------------------------------------------------------------------ */
/*
  Initialization
  ==============
*/
/* called at startup to check configuration sanity */
static void gasnetc_check_config(void) {
  gasneti_check_config_preinit();

  /* (###) add code to do some sanity checks on the number of nodes, handlers
   * and/or segment sizes */ 
}

/* callback to decrement a simple (non-atomic) counter */
extern void gasnetc_cb_dec(pami_context_t context, void *cookie, pami_result_t status) {
  int *counter_p = cookie;
  (*counter_p) -= 1;
}

/* callback to decrement an atomic counter */
extern void gasnetc_cb_dec_atomic(pami_context_t context, void *cookie, pami_result_t status) {
  gasneti_weakatomic_t *counter_p = cookie;
  gasneti_weakatomic_decrement(counter_p, 0);
}

/* callback to decrement an atomic counter, with release */
extern void gasnetc_cb_dec_release(pami_context_t context, void *cookie, pami_result_t status) {
  gasneti_weakatomic_t *counter_p = cookie;
  gasneti_weakatomic_decrement(counter_p, GASNETI_ATOMIC_REL);
}

/* Get the first "always works" algorithm for a given collective operation */
static void default_coll_alg(pami_xfer_type_t op, pami_algorithm_t *alg_p) {
  pami_result_t rc;
  size_t counts[2];
  pami_algorithm_t *req_algs, *opt_algs;
  pami_metadata_t *req_meta, *opt_meta;

  rc = PAMI_Geometry_algorithms_num(gasnetc_world_geom, op, counts);
  GASNETC_PAMI_CHECK(rc, "calling PAMI_Geometry_algorithms_num()");
  gasneti_assert_always(counts[0] != 0);

  /* Space for required ("always works") alogorithms and metadata */
  req_algs = alloca(counts[0] * sizeof(pami_algorithm_t));
  req_meta = alloca(counts[0] * sizeof(pami_metadata_t));

  /* Space for optional ("must query") alogorithms and metadata */
  opt_algs = alloca(counts[1] * sizeof(pami_algorithm_t));
  opt_meta = alloca(counts[1] * sizeof(pami_metadata_t));

  /* XXX: can we pass null or zero counts for "don't care" items */
  rc = PAMI_Geometry_algorithms_query(gasnetc_world_geom, op,
                                      req_algs, req_meta, counts[0],
                                      opt_algs, opt_meta, counts[1]);
  GASNETC_PAMI_CHECK(rc, "calling PAMI_Geometry_algorithms_query()");

  *alg_p = req_algs[0];
}

static void bootstrap_collective(pami_xfer_t *op_p) {
  pami_result_t rc;
  int counter = 1;

  op_p->cb_done = &gasnetc_cb_dec;
  op_p->cookie = &counter;
  op_p->options.multicontext = PAMI_HINT_DISABLE;

  rc = PAMI_Collective(gasnetc_context, op_p);
  GASNETC_PAMI_CHECK(rc, "initiating a bootstrap collective");

  while (counter) {
    rc = PAMI_Context_advance(gasnetc_context, 1);
    GASNETC_PAMI_CHECK(rc, "polling a bootstrap collective");
  }
}

static void gasnetc_bootstrapBarrier(void) {
  static pami_xfer_t op;
  static int is_init = 0;

  if_pf (!is_init) {
    memset(&op, 0, sizeof(op)); /* Shouldn't need for static, but let's be safe */
    default_coll_alg(PAMI_XFER_BARRIER, &op.algorithm);
    is_init = 1;
  }

  bootstrap_collective(&op);
}

static void gasnetc_bootstrapExchange(void *src, size_t len, void *dst) {
  static pami_xfer_t op;
  static int is_init = 0;

  if_pf (!is_init) {
    memset(&op, 0, sizeof(op)); /* Shouldn't need for static, but let's be safe */
    default_coll_alg(PAMI_XFER_ALLGATHER, &op.algorithm);
    is_init = 1;
  }

  op.cmd.xfer_allgather.sndbuf     = src;
  op.cmd.xfer_allgather.stype      = PAMI_TYPE_BYTE;
  op.cmd.xfer_allgather.stypecount = len;
  op.cmd.xfer_allgather.rcvbuf     = dst;
  op.cmd.xfer_allgather.rtype      = PAMI_TYPE_BYTE;
  op.cmd.xfer_allgather.rtypecount = len; /* times gasneti_nodes */

  bootstrap_collective(&op);
}

static int gasnetc_init(int *argc, char ***argv) {
  pami_result_t rc;

  /*  check system sanity */
  gasnetc_check_config();

  if (gasneti_init_done) 
    GASNETI_RETURN_ERRR(NOT_INIT, "GASNet already initialized");

  gasneti_freezeForDebugger();

  #if GASNET_DEBUG_VERBOSE
    /* note - can't call trace macros during gasnet_init because trace system not yet initialized */
    fprintf(stderr,"gasnetc_init(): about to spawn...\n"); fflush(stderr);
  #endif

  /* (###) add code here to bootstrap the nodes for your conduit */

  /* For error messages only */
  gasneti_mynode = -1;
  gasneti_nodes  = -1;

  rc = PAMI_Client_create("GASNet", &gasnetc_pami_client, NULL, 0);
  GASNETC_PAMI_CHECK(rc, "calling PAMI_Client_create");

  { pami_configuration_t conf[2];
    conf[0].name = PAMI_CLIENT_TASK_ID;
    conf[1].name = PAMI_CLIENT_NUM_TASKS;

    rc = PAMI_Client_query(gasnetc_pami_client, conf, 2);
    GASNETC_PAMI_CHECK(rc, "calling PAMI_Client_query() for TASK_ID and NUM_TASKS");

    gasneti_mynode = conf[0].value.intval;
    gasneti_nodes  = conf[1].value.intval;
  }

  { pami_context_t contexts[1];

    rc = PAMI_Context_createv(gasnetc_pami_client, NULL, 0, contexts, 1);
    GASNETC_PAMI_CHECK(rc, "calling PAMI_Context_createv");

    gasnetc_context = contexts[0];
  }

  rc = PAMI_Geometry_world(gasnetc_pami_client, &gasnetc_world_geom);
  GASNETC_PAMI_CHECK(rc, "calling PAMI_Geometry_world()");

  gasneti_assert_zeroret(gasnetc_exit_init());

  #if GASNET_DEBUG_VERBOSE
    fprintf(stderr,"gasnetc_init(): spawn successful - node %i/%i starting...\n", 
      gasneti_mynode, gasneti_nodes); fflush(stderr);
  #endif

  /* Now enable tracing of all the following steps */
  gasneti_init_done = 1; /* required to allow tracing */
  gasneti_trace_init(argc, argv);

  /* (###) Add code here to determine which GASNet nodes may share memory.
     The collection of nodes sharing memory are known as a "supernode".
     The (first) data structure to describe this is gasneti_nodemap[]:
        For all i: gasneti_nodemap[i] is the lowest node number collocated w/ node i
     where nodes are considered collocated if they have the same node "ID".
     Or in English:
       "gasneti_nodemap[] maps from node to first node on the same supernode."

     If the conduit has already communicated endpoint address information or
     a similar identifier that is unique per shared-memory compute node, then
     that info can be passed via arguments 2 through 4.
     Otherwise the conduit should pass a non-null gasnetc_bootstrapExchange
     as argument 1 to use platform-specific IDs, such as gethostid().
     See gasneti_nodemapInit() in gasnet_internal.c for more usage documentation.
     See below for info on gasnetc_bootstrapExchange()

     If the conduit can build gasneti_nodemap[] w/o assistance, it should
     call gasneti_nodemapParse() after constructing it (instead of nodemapInit()).
  */
  gasneti_nodemapInit(&gasnetc_bootstrapExchange, NULL, 0, 0);

  #if GASNET_PSHM
    /* (###) If your conduit will support PSHM, you should initialize it here.
     * The 1st argument is normally "&gasnetc_bootstrapExchange" (described below).
     * The 2nd argument is the amout of shared memory space needed for any
     * conduit-specific uses.  The return value is a pointer to the space
     * requested by the 2nd argument.
     */
    ### = gasneti_pshm_init(&gasnetc_bootstrapExchange, 0);
  #endif

  #if GASNET_SEGMENT_FAST || GASNET_SEGMENT_LARGE
    { 
      // XXX: platform specific query for upper limit
      uintptr_t limit = gasneti_mmapLimit((uintptr_t)-1, (uint64_t)-1,
                                          &gasnetc_bootstrapExchange,
                                          &gasnetc_bootstrapBarrier);
      gasneti_segmentInit(limit, &gasnetc_bootstrapExchange);
    }
  #elif GASNET_SEGMENT_EVERYTHING
    /* segment is everything - nothing to do */
  #else
    #error Bad segment config
  #endif

  #if 0 /* Current supported systems ensure environment is propogated */
    /* Enable this if you wish to use the default GASNet services for broadcasting 
        the environment from one compute node to all the others (for use in gasnet_getenv(),
        which needs to return environment variable values from the "spawning console").
        You need to provide two functions (gasnetc_bootstrapExchange and gasnetc_bootstrapBroadcast)
        which the system can safely and immediately use to broadcast and exchange information 
        between nodes (gasnetc_bootstrapBroadcast is optional but highly recommended).
        See gasnet/other/mpi-spawner/gasnet_bootstrap_mpi.c for definitions of these two
        functions in terms of MPI collective operations.
       This system assumes that at least one of the compute nodes has a copy of the 
        full environment from the "spawning console" (if this is not true, you'll need to
        implement something yourself to get the values from the spawning console)
       If your job system already always propagates environment variables to all the compute
        nodes, then you probably don't need this.
     */
    gasneti_setupGlobalEnvironment(gasneti_nodes, gasneti_mynode, 
                                   gasnetc_bootstrapExchange, gasnetc_bootstrapBroadcast);
  #endif

#if 0 /* was done above to allow early init of tracing */
  gasneti_init_done = 1;  
#endif

  gasneti_auxseg_init(); /* adjust max seg values based on auxseg */

  return GASNET_OK;
}

/* ------------------------------------------------------------------------------------ */
extern int gasnet_init(int *argc, char ***argv) {
  int retval = gasnetc_init(argc, argv);
  if (retval != GASNET_OK) GASNETI_RETURN(retval);
#if 0 /* was done in gasnetc_init() to allow tracing of init steps */
  gasneti_trace_init(argc, argv);
#endif
  return GASNET_OK;
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

    if ((table[i].index == 0 && !dontcare) || 
        (table[i].index && dontcare)) continue;
    else if (table[i].index) newindex = table[i].index;
    else { /* deterministic assignment of dontcare indexes */
      for (newindex = lowlimit; newindex <= highlimit; newindex++) {
        if (!checkuniqhandler[newindex]) break;
      }
      if (newindex > highlimit) {
        char s[255];
        snprintf(s, sizeof(s), "Too many handlers. (limit=%i)", highlimit - lowlimit + 1);
        GASNETI_RETURN_ERRR(BAD_ARG, s);
      }
    }

    /*  ensure handlers fall into the proper range of pre-assigned values */
    if (newindex < lowlimit || newindex > highlimit) {
      char s[255];
      snprintf(s, sizeof(s), "handler index (%i) out of range [%i..%i]", newindex, lowlimit, highlimit);
      GASNETI_RETURN_ERRR(BAD_ARG, s);
    }

    /* discover duplicates */
    if (checkuniqhandler[newindex] != 0) 
      GASNETI_RETURN_ERRR(BAD_ARG, "handler index not unique");
    checkuniqhandler[newindex] = 1;

    /* register the handler */
    gasnetc_handler[(gasnet_handler_t)newindex] = (gasneti_handler_fn_t)table[i].fnptr;

    /* The check below for !table[i].index is redundant and present
     * only to defeat the over-aggressive optimizer in pathcc 2.1
     */
    if (dontcare && !table[i].index) table[i].index = newindex;

    (*numregistered)++;
  }
  return GASNET_OK;
}
/* ------------------------------------------------------------------------------------ */
extern int gasnetc_attach(gasnet_handlerentry_t *table, int numentries,
                          uintptr_t segsize, uintptr_t minheapoffset) {
  void *segbase = NULL;
  
  GASNETI_TRACE_PRINTF(C,("gasnetc_attach(table (%i entries), segsize=%lu, minheapoffset=%lu)",
                          numentries, (unsigned long)segsize, (unsigned long)minheapoffset));

  if (!gasneti_init_done) 
    GASNETI_RETURN_ERRR(NOT_INIT, "GASNet attach called before init");
  if (gasneti_attach_done) 
    GASNETI_RETURN_ERRR(NOT_INIT, "GASNet already attached");

  /*  check argument sanity */
  #if GASNET_SEGMENT_FAST || GASNET_SEGMENT_LARGE
    if ((segsize % GASNET_PAGESIZE) != 0) 
      GASNETI_RETURN_ERRR(BAD_ARG, "segsize not page-aligned");
    if (segsize > gasneti_MaxLocalSegmentSize) 
      GASNETI_RETURN_ERRR(BAD_ARG, "segsize too large");
    if ((minheapoffset % GASNET_PAGESIZE) != 0) /* round up the minheapoffset to page sz */
      minheapoffset = ((minheapoffset / GASNET_PAGESIZE) + 1) * GASNET_PAGESIZE;
  #else
    segsize = 0;
    minheapoffset = 0;
  #endif

  segsize = gasneti_auxseg_preattach(segsize); /* adjust segsize for auxseg reqts */

  /* ------------------------------------------------------------------------------------ */
  /*  register handlers */
  { int i;
    for (i = 0; i < GASNETC_MAX_NUMHANDLERS; i++) 
      gasnetc_handler[i] = (gasneti_handler_fn_t)&gasneti_defaultAMHandler;
  }
  { /*  core API handlers */
    gasnet_handlerentry_t *ctable = (gasnet_handlerentry_t *)gasnetc_get_handlertable();
    int len = 0;
    int numreg = 0;
    gasneti_assert(ctable);
    while (ctable[len].fnptr) len++; /* calc len */
    if (gasnetc_reghandlers(ctable, len, 1, 63, 0, &numreg) != GASNET_OK)
      GASNETI_RETURN_ERRR(RESOURCE,"Error registering core API handlers");
    gasneti_assert(numreg == len);
  }

  { /*  extended API handlers */
    gasnet_handlerentry_t *etable = (gasnet_handlerentry_t *)gasnete_get_handlertable();
    int len = 0;
    int numreg = 0;
    gasneti_assert(etable);
    while (etable[len].fnptr) len++; /* calc len */
    if (gasnetc_reghandlers(etable, len, 64, 127, 0, &numreg) != GASNET_OK)
      GASNETI_RETURN_ERRR(RESOURCE,"Error registering extended API handlers");
    gasneti_assert(numreg == len);
  }

  if (table) { /*  client handlers */
    int numreg1 = 0;
    int numreg2 = 0;

    /*  first pass - assign all fixed-index handlers */
    if (gasnetc_reghandlers(table, numentries, 128, 255, 0, &numreg1) != GASNET_OK)
      GASNETI_RETURN_ERRR(RESOURCE,"Error registering fixed-index client handlers");

    /*  second pass - fill in dontcare-index handlers */
    if (gasnetc_reghandlers(table, numentries, 128, 255, 1, &numreg2) != GASNET_OK)
      GASNETI_RETURN_ERRR(RESOURCE,"Error registering fixed-index client handlers");

    gasneti_assert(numreg1 + numreg2 == numentries);
  }

  /* ------------------------------------------------------------------------------------ */
  /*  register fatal signal handlers */

  /* catch fatal signals and convert to SIGQUIT */
  gasneti_registerSignalHandlers(gasneti_defaultSignalHandler);

  /*  (###) register any custom signal handlers required by your conduit 
   *        (e.g. to support interrupt-based messaging)
   */

  /* ------------------------------------------------------------------------------------ */
  /*  register segment  */

  gasneti_seginfo = (gasnet_seginfo_t *)gasneti_malloc(gasneti_nodes*sizeof(gasnet_seginfo_t));
  gasneti_leak(gasneti_seginfo);

  #if GASNET_SEGMENT_FAST || GASNET_SEGMENT_LARGE
    if (segsize == 0) segbase = NULL; /* no segment */
    else {
      /* (###) add code here to choose and register a segment 
         (ensuring alignment across all nodes if this conduit sets GASNET_ALIGNED_SEGMENTS==1) 
         you can use gasneti_segmentAttach() here if you used gasneti_segmentInit() above
      */
      gasneti_segmentAttach(segsize, minheapoffset, gasneti_seginfo, &gasnetc_bootstrapExchange);
      segbase = gasneti_seginfo[gasneti_mynode].addr;
      segsize = gasneti_seginfo[gasneti_mynode].size;

      gasneti_assert(((uintptr_t)segbase) % GASNET_PAGESIZE == 0);
      gasneti_assert(segsize % GASNET_PAGESIZE == 0);
    }
  #else
    /* GASNET_SEGMENT_EVERYTHING */
    segbase = (void *)0;
    segsize = (uintptr_t)-1;
    /* (###) add any code here needed to setup GASNET_SEGMENT_EVERYTHING support */
  #endif

  /* ------------------------------------------------------------------------------------ */
  /*  gather segment information */

  /* (###) add code here to gather the segment assignment info into 
           gasneti_seginfo on each node (may be possible to use AMShortRequest here)
   */

  /* ------------------------------------------------------------------------------------ */
  /*  primary attach complete */
  gasneti_attach_done = 1;
  gasnetc_bootstrapBarrier();

  GASNETI_TRACE_PRINTF(C,("gasnetc_attach(): primary attach complete"));

  gasneti_assert(gasneti_seginfo[gasneti_mynode].addr == segbase &&
         gasneti_seginfo[gasneti_mynode].size == segsize);

  gasneti_auxseg_attach(); /* provide auxseg */

  gasnete_init(); /* init the extended API */

  gasneti_nodemapFini();

  /* ensure extended API is initialized across nodes */
  gasnetc_bootstrapBarrier();

  return GASNET_OK;
}
/* ------------------------------------------------------------------------------------ */
#if HAVE_ON_EXIT
static void gasnetc_on_exit(int exitcode, void *arg) {
    gasnetc_exit(exitcode);
}
#else
static void gasnetc_atexit(void) {
    gasnetc_exit(0);
}
#endif

/* Exit coordination timeouts */
#define GASNETC_DEFAULT_EXITTIMEOUT_MAX         360.0   /* 6 minutes! */
#define GASNETC_DEFAULT_EXITTIMEOUT_MIN         2       /* 2 seconds */
#define GASNETC_DEFAULT_EXITTIMEOUT_FACTOR      0.25    /* 1/4 second */
static double gasnetc_exittimeout = GASNETC_DEFAULT_EXITTIMEOUT_MAX;

/* Exit coordination vars */
static uint8_t gasnetc_exitcode = 0;
static pami_xfer_t gasnetc_exit_reduce_op;

static int gasnetc_exit_init(void) {
  gasnetc_exittimeout = gasneti_get_exittimeout(GASNETC_DEFAULT_EXITTIMEOUT_MAX,
                                                GASNETC_DEFAULT_EXITTIMEOUT_MIN,
                                                GASNETC_DEFAULT_EXITTIMEOUT_FACTOR,
                                                GASNETC_DEFAULT_EXITTIMEOUT_MIN);

  memset(&gasnetc_exit_reduce_op, 0, sizeof(gasnetc_exit_reduce_op));
  default_coll_alg(PAMI_XFER_ALLREDUCE, &gasnetc_exit_reduce_op.algorithm);

#if HAVE_ON_EXIT
  on_exit(gasnetc_on_exit, NULL);
#else
  atexit(gasnetc_atexit);
#endif

  return GASNET_OK;
}

static int gasnetc_exit_reduce(void) {
  gasneti_tick_t start_time = gasneti_ticks_now();
  int64_t timeout_ns = gasnetc_exittimeout * 1.0e9;
  uint8_t exitcode;
  pami_result_t rc;

  gasneti_weakatomic_t counter = gasneti_weakatomic_init(1);
  gasnetc_exit_reduce_op.cookie = (void *)&counter;
  gasnetc_exit_reduce_op.cb_done = &gasnetc_cb_dec_release;
  gasnetc_exit_reduce_op.options.multicontext = PAMI_HINT_DISABLE;

  gasnetc_exit_reduce_op.cmd.xfer_allreduce.sndbuf = &gasnetc_exitcode;
  gasnetc_exit_reduce_op.cmd.xfer_allreduce.stype = PAMI_TYPE_UNSIGNED_CHAR;
  gasnetc_exit_reduce_op.cmd.xfer_allreduce.stypecount = 1;
  gasnetc_exit_reduce_op.cmd.xfer_allreduce.rcvbuf = &exitcode;
  gasnetc_exit_reduce_op.cmd.xfer_allreduce.rtype = PAMI_TYPE_UNSIGNED_CHAR;
  gasnetc_exit_reduce_op.cmd.xfer_allreduce.rtypecount = 1;
  gasnetc_exit_reduce_op.cmd.xfer_allreduce.op = PAMI_DATA_MAX;
  gasnetc_exit_reduce_op.cmd.xfer_allreduce.data_cookie = NULL;
  gasnetc_exit_reduce_op.cmd.xfer_allreduce.commutative = 1;

  rc = PAMI_Collective(gasnetc_context, &gasnetc_exit_reduce_op);
  if (rc != PAMI_SUCCESS) return 1;

  while (gasneti_weakatomic_read(&counter, 0)) {
    if ((PAMI_SUCCESS != PAMI_Context_advance(gasnetc_context, 1)) ||
        (timeout_ns < gasneti_ticks_to_ns(gasneti_ticks_now() - start_time))) {
      return 1;
    }
  }

  gasneti_sync_reads();
  gasnetc_exitcode = exitcode;

  return 0;
}

extern void gasnetc_exit(int exitcode) {
  /* once we start a shutdown, ignore all future SIGQUIT signals or we risk reentrancy */
  gasneti_reghandler(SIGQUIT, SIG_IGN);

  {  /* ensure only one thread ever continues past this point */
    static gasneti_mutex_t exit_lock = GASNETI_MUTEX_INITIALIZER;
    gasneti_mutex_lock(&exit_lock);
  }

  GASNETI_TRACE_PRINTF(C,("gasnet_exit(%i)\n", exitcode));

  gasneti_flush_streams();
  gasneti_trace_finish();
  gasneti_sched_yield();

  /* (###) add code here to terminate the job across _all_ nodes 
           with gasneti_killmyprocess(exitcode) (not regular exit()), preferably
           after raising a SIGQUIT to inform the client of the exit
  */

  /* Detect collective exit while performing reduce(MAX(exitcode))
   * The reduction has a timeout to distinguish non-collective exits */
  gasnetc_exitcode = exitcode;
  if (0 != gasnetc_exit_reduce()) {
    /* Failed to coordinate shutdown */
    // XXX: can we raise SIGQUIT remotely, etc.
    gasnetc_exitcode = 1; /* on BG/Q this forces global termination */
  }
  gasneti_killmyprocess(gasnetc_exitcode);

  gasneti_fatalerror("gasnetc_exit failed!");
}

/* ------------------------------------------------------------------------------------ */
/*
  Misc. Active Message Functions
  ==============================
*/
#if GASNET_PSHM
/* (###) GASNETC_GET_HANDLER
 *   If your conduit will support PSHM, then there needs to be a way
 *   for PSHM to see your handler table.  If you use the recommended
 *   implementation (gasnetc_handler[]) then you don't need to do
 *   anything special.  Othwerwise, #define GASNETC_GET_HANDLER in
 *   gasnet_core_fwd.h and implement gasnetc_get_handler() here, or
 *   as a macro or inline in gasnet_core_internal.h
 *
 * (###) GASNETC_TOKEN_CREATE
 *   If your conduit will support PSHM, then there needs to be a way
 *   for the conduit-specific and PSHM token spaces to co-exist.
 *   The default PSHM implementation produces tokens with the least-
 *   significant bit set and assumes the conduit never will.  If that
 *   is true, you don't need to do anything special here.
 *   If your conduit cannot use the default PSHM token code, then
 *   #define GASNETC_TOKEN_CREATE in gasnet_core_fwd.h and implement
 *   the associated routines described in gasnet_pshm.h.  That code
 *   could be functions located here, or could be macros or inlines
 *   in gasnet_core_internal.h.
 */
#endif

extern int gasnetc_AMGetMsgSource(gasnet_token_t token, gasnet_node_t *srcindex) {
  gasnet_node_t sourceid;
  GASNETI_CHECKATTACH();
  GASNETI_CHECK_ERRR((!token),BAD_ARG,"bad token");
  GASNETI_CHECK_ERRR((!srcindex),BAD_ARG,"bad src ptr");

#if GASNET_PSHM
  /* (###) If your conduit will support PSHM, let the PSHM code
   * have a chance to recognize the token first, as shown here. */
  if (gasneti_AMPSHMGetMsgSource(token, &sourceid) != GASNET_OK)
#endif
  {
    /* (###) add code here to write the source index into sourceid. */
    sourceid = -1; // ###
  }

  gasneti_assert(sourceid < gasneti_nodes);
  *srcindex = sourceid;
  return GASNET_OK;
}

extern int gasnetc_AMPoll(void) {
  int retval;
  GASNETI_CHECKATTACH();

#if GASNET_PSHM
  /* (###) If your conduit will support PSHM, let it make progress here. */
  gasneti_AMPSHMPoll(0);
#endif

  /* (###) add code here to run your AM progress engine */

  return GASNET_OK;
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
  GASNETI_COMMON_AMREQUESTSHORT(dest,handler,numargs);
  va_start(argptr, numargs); /*  pass in last argument */
#if GASNET_PSHM
  /* (###) If your conduit will support PSHM, let it check the dest first. */
  if_pt (gasneti_pshm_in_supernode(dest)) {
    retval = gasneti_AMPSHM_RequestGeneric(gasnetc_Short, dest, handler,
                                           0, 0, 0,
                                           numargs, argptr);
  } else
#endif
  {
    /* (###) add code here to read the arguments using va_arg(argptr, gasnet_handlerarg_t) 
             and send the active message 
     */

    retval = GASNET_OK;
  }
  va_end(argptr);
  GASNETI_RETURN(retval);
}

extern int gasnetc_AMRequestMediumM( 
                            gasnet_node_t dest,      /* destination node */
                            gasnet_handler_t handler, /* index into destination endpoint's handler table */ 
                            void *source_addr, size_t nbytes,   /* data payload */
                            int numargs, ...) {
  int retval;
  va_list argptr;
  GASNETI_COMMON_AMREQUESTMEDIUM(dest,handler,source_addr,nbytes,numargs);
  va_start(argptr, numargs); /*  pass in last argument */
#if GASNET_PSHM
  /* (###) If your conduit will support PSHM, let it check the dest first. */
  if_pt (gasneti_pshm_in_supernode(dest)) {
    retval = gasneti_AMPSHM_RequestGeneric(gasnetc_Medium, dest, handler,
                                           source_addr, nbytes, 0,
                                           numargs, argptr);
  } else
#endif
  {
    /* (###) add code here to read the arguments using va_arg(argptr, gasnet_handlerarg_t) 
             and send the active message 
     */

    retval = GASNET_OK;
  }
  va_end(argptr);
  GASNETI_RETURN(retval);
}

extern int gasnetc_AMRequestLongM( gasnet_node_t dest,        /* destination node */
                            gasnet_handler_t handler, /* index into destination endpoint's handler table */ 
                            void *source_addr, size_t nbytes,   /* data payload */
                            void *dest_addr,                    /* data destination on destination node */
                            int numargs, ...) {
  int retval;
  va_list argptr;
  GASNETI_COMMON_AMREQUESTLONG(dest,handler,source_addr,nbytes,dest_addr,numargs);
  va_start(argptr, numargs); /*  pass in last argument */
#if GASNET_PSHM
  /* (###) If your conduit will support PSHM, let it check the dest first. */
  if_pt (gasneti_pshm_in_supernode(dest)) {
    retval = gasneti_AMPSHM_RequestGeneric(gasnetc_Long, dest, handler,
                                           source_addr, nbytes, dest_addr,
                                           numargs, argptr);
  } else
#endif
  {
    /* (###) add code here to read the arguments using va_arg(argptr, gasnet_handlerarg_t) 
             and send the active message 
     */

    retval = GASNET_OK;
  }
  va_end(argptr);
  GASNETI_RETURN(retval);
}

extern int gasnetc_AMRequestLongAsyncM( gasnet_node_t dest,        /* destination node */
                            gasnet_handler_t handler, /* index into destination endpoint's handler table */ 
                            void *source_addr, size_t nbytes,   /* data payload */
                            void *dest_addr,                    /* data destination on destination node */
                            int numargs, ...) {
  int retval;
  va_list argptr;
  GASNETI_COMMON_AMREQUESTLONGASYNC(dest,handler,source_addr,nbytes,dest_addr,numargs);
  va_start(argptr, numargs); /*  pass in last argument */
#if GASNET_PSHM
  /* (###) If your conduit will support PSHM, let it check the dest first. */
  if_pt (gasneti_pshm_in_supernode(dest)) {
    retval = gasneti_AMPSHM_RequestGeneric(gasnetc_Long, dest, handler,
                                           source_addr, nbytes, dest_addr,
                                           numargs, argptr);
  } else
#endif
  {
    /* (###) add code here to read the arguments using va_arg(argptr, gasnet_handlerarg_t) 
             and send the active message 
     */

    retval = GASNET_OK;
  }
  va_end(argptr);
  GASNETI_RETURN(retval);
}

extern int gasnetc_AMReplyShortM( 
                            gasnet_token_t token,       /* token provided on handler entry */
                            gasnet_handler_t handler, /* index into destination endpoint's handler table */ 
                            int numargs, ...) {
  int retval;
  va_list argptr;
  GASNETI_COMMON_AMREPLYSHORT(token,handler,numargs);
  va_start(argptr, numargs); /*  pass in last argument */
#if GASNET_PSHM
  /* (###) If your conduit will support PSHM, let it check the token first. */
  if_pt (gasnetc_token_is_pshm(token)) {
    retval = gasneti_AMPSHM_ReplyGeneric(gasnetc_Short, token, handler,
                                         0, 0, 0,
                                         numargs, argptr);
  } else
#endif
  { 
    /* (###) add code here to read the arguments using va_arg(argptr, gasnet_handlerarg_t) 
             and send the active message 
     */

    retval = GASNET_OK;
  }
  va_end(argptr);
  GASNETI_RETURN(retval);
}

extern int gasnetc_AMReplyMediumM( 
                            gasnet_token_t token,       /* token provided on handler entry */
                            gasnet_handler_t handler, /* index into destination endpoint's handler table */ 
                            void *source_addr, size_t nbytes,   /* data payload */
                            int numargs, ...) {
  int retval;
  va_list argptr;
  GASNETI_COMMON_AMREPLYMEDIUM(token,handler,source_addr,nbytes,numargs);
  va_start(argptr, numargs); /*  pass in last argument */
#if GASNET_PSHM
  /* (###) If your conduit will support PSHM, let it check the token first. */
  if_pt (gasnetc_token_is_pshm(token)) {
    retval = gasneti_AMPSHM_ReplyGeneric(gasnetc_Medium, token, handler,
                                         source_addr, nbytes, 0,
                                         numargs, argptr);
  } else
#endif
  {
    /* (###) add code here to read the arguments using va_arg(argptr, gasnet_handlerarg_t) 
             and send the active message 
     */

    retval = GASNET_OK;
  }
  va_end(argptr);
  GASNETI_RETURN(retval);
}

extern int gasnetc_AMReplyLongM( 
                            gasnet_token_t token,       /* token provided on handler entry */
                            gasnet_handler_t handler, /* index into destination endpoint's handler table */ 
                            void *source_addr, size_t nbytes,   /* data payload */
                            void *dest_addr,                    /* data destination on destination node */
                            int numargs, ...) {
  int retval;
  va_list argptr;
  GASNETI_COMMON_AMREPLYLONG(token,handler,source_addr,nbytes,dest_addr,numargs); 
  va_start(argptr, numargs); /*  pass in last argument */
#if GASNET_PSHM
  /* (###) If your conduit will support PSHM, let it check the token first. */
  if_pt (gasnetc_token_is_pshm(token)) {
    retval = gasneti_AMPSHM_ReplyGeneric(gasnetc_Long, token, handler,
                                         source_addr, nbytes, dest_addr,
                                         numargs, argptr);
  } else
#endif
  {
    /* (###) add code here to read the arguments using va_arg(argptr, gasnet_handlerarg_t) 
             and send the active message 
     */

    retval = GASNET_OK;
  }
  va_end(argptr);
  GASNETI_RETURN(retval);
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
  extern void gasnetc_hold_interrupts(void) {
    GASNETI_CHECKATTACH();
    /* add code here to disable handler interrupts for _this_ thread */
  }
  extern void gasnetc_resume_interrupts(void) {
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

  {
    #if GASNETI_STATS_OR_TRACE
      gasneti_tick_t startlock = GASNETI_TICKS_NOW_IFENABLED(L);
    #endif
    #if GASNETC_HSL_SPINLOCK
      if_pf (gasneti_mutex_trylock(&(hsl->lock)) == EBUSY) {
        if (gasneti_wait_mode == GASNET_WAIT_SPIN) {
          while (gasneti_mutex_trylock(&(hsl->lock)) == EBUSY) {
            gasneti_compiler_fence();
            gasneti_spinloop_hint();
          }
        } else {
          gasneti_mutex_lock(&(hsl->lock));
        }
      }
    #else
      gasneti_mutex_lock(&(hsl->lock));
    #endif
    #if GASNETI_STATS_OR_TRACE
      hsl->acquiretime = GASNETI_TICKS_NOW_IFENABLED(L);
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

  GASNETI_TRACE_EVENT_TIME(L, HSL_UNLOCK, GASNETI_TICKS_NOW_IFENABLED(L)-hsl->acquiretime);

  gasneti_mutex_unlock(&(hsl->lock));
}

extern int  gasnetc_hsl_trylock(gasnet_hsl_t *hsl) {
  GASNETI_CHECKATTACH();

  {
    int locked = (gasneti_mutex_trylock(&(hsl->lock)) == 0);

    GASNETI_TRACE_EVENT_VAL(L, HSL_TRYLOCK, locked);
    if (locked) {
      #if GASNETI_STATS_OR_TRACE
        hsl->acquiretime = GASNETI_TICKS_NOW_IFENABLED(L);
      #endif
      #if GASNETC_USE_INTERRUPTS
        /* conduits with interrupt-based handler dispatch need to add code here to 
           disable handler interrupts on _this_ thread, (if this is the outermost
           HSL lock acquire and we're not inside an enclosing no-interrupt section)
         */
        #error interrupts not implemented
      #endif
    }

    return locked ? GASNET_OK : GASNET_ERR_NOT_READY;
  }
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
  #ifdef GASNETC_AUXSEG_HANDLERS
    GASNETC_AUXSEG_HANDLERS(),
  #endif
  /* ptr-width independent handlers */

  /* ptr-width dependent handlers */

  { 0, NULL }
};

gasnet_handlerentry_t const *gasnetc_get_handlertable(void) {
  return gasnetc_handlers;
}

/* ------------------------------------------------------------------------------------ */

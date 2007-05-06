/*   $Source: /Users/kamil/work/gasnet-cvs2/gasnet/smp-conduit/gasnet_core.c,v $
 *     $Date: 2007/05/06 00:18:36 $
 * $Revision: 1.45.4.8 $
 * Description: GASNet smp conduit Implementation
 * Copyright 2002, Dan Bonachea <bonachea@cs.berkeley.edu>
 * Terms of use are as specified in license.txt
 */

#include <gasnet_internal.h>
#include <gasnet_handler.h>
#include <gasnet_core_internal.h>

#include <errno.h>
#include <unistd.h>
#include <signal.h>

#ifdef HAVE_MMAP
#  include <sys/mman.h>
#endif

GASNETI_IDENT(gasnetc_IdentString_Version, "$GASNetCoreLibraryVersion: " GASNET_CORE_VERSION_STR " $");
GASNETI_IDENT(gasnetc_IdentString_Name,    "$GASNetCoreLibraryName: " GASNET_CORE_NAME_STR " $");

#if GASNET_SYSV
  /* maximum number of processes that share a single shared memory region */
  #define GASNETC_MAX_SYSV_NODES 256

  /* Supernode data that lives in shared space */
  struct gasnetc_supernode_info_t {
    gasnet_node_t node2pid[GASNETC_MAX_SYSV_NODES]; /* pid lookup table */
    gasneti_atomic_t startup_counter;		    /* one-time barrier */
  };
  static struct gasnetc_supernode_info_t *gasnetc_sn_info;

  #define gasneti_sysv_node2pid gasnetc_sn_info->node2pid

  static void *gasnetc_sysvnet_region;
#endif /* GASNET_SYSV */

gasnet_handlerentry_t const *gasnetc_get_handlertable();
static void gasnetc_atexit(void);

#if !GASNETI_CLIENT_THREADS
  void *_gasnetc_mythread = NULL;
#endif

#define GASNETC_MAX_NUMHANDLERS   256
typedef void (*gasnetc_handler_fn_t)();  /* prototype for handler function */
gasnetc_handler_fn_t gasnetc_handler[GASNETC_MAX_NUMHANDLERS]; /* handler table */

gasneti_handler_fn_t gasneti_get_handler(int handler_id) {
  gasneti_assert(handler_id < GASNETC_MAX_NUMHANDLERS);
  return gasnetc_handler[handler_id];
}

/* ------------------------------------------------------------------------------------ */
/*
  Initialization
  ==============
*/
/* called at startup to check configuration sanity */
static void gasnetc_check_config() {
  gasneti_check_config_preinit();

  /* add code to do some sanity checks on the number of nodes, handlers
   * and/or segment sizes */ 
}

void gasnetc_bootstrapExchange(void *src, size_t len, void *dest) {
  #if GASNET_SYSV
    gasneti_assert(gasneti_request_sysvnet != NULL);
    gasneti_sysvnet_bootstrapExchange(gasneti_request_sysvnet, src, len, dest);
  #else
    gasneti_assert(gasneti_nodes == 1); /* trivial because we only have one node */
    memmove(dest, src, len);
  #endif
}
void gasnetc_bootstrapBroadcast(void *src, size_t len, void *dest, int rootnode) {
  /* NOTE: no GASNET_SYSV implemention, but that's OK 'cause this function
   * isn't getting used */
  gasneti_assert(gasneti_nodes == 1); /* trivial because we only have one node */
  gasneti_assert(rootnode == 0);
  memmove(dest, src, len);
}

static void gasnetc_bootstrapBarrier() {
  /* add code here to implement an external barrier 
      this barrier should not rely on AM or the GASNet API because it's used 
      during bootstrapping before such things are fully functional
     It need not be particularly efficient, because we only call it a few times
      and only during bootstrapping - it just has to work correctly
     If your underlying spawning or batch system provides barrier functionality,
      that would probably be a good choice for this
   */
  #if GASNET_SYSV
    /* HACK: use my patented "sleepy ostrich" algorithm ("race conditions go
     * away if you just take a sufficiently long nap").
     * - TODO: replace with a real barrier!
     */
     sleep(1);
  #else
    gasneti_assert(gasneti_nodes == 1); /* trivial because we only have one node */
  #endif
}

#if GASNET_SYSV

static int gasnetc_get_sysv_nodecount()
{
  gasnet_node_t nodes = gasneti_getenv_int_withdefault("GASNET_SYSV_NODES", 0, 0);
  int polite_wait, politedefault;

  if (nodes > GASNETC_MAX_SYSV_NODES) { 
    gasneti_fatalerror("Nodes requested (%d) > maximum (%d)", nodes,
                       GASNETC_MAX_SYSV_NODES);
  } else if (nodes == 0) {
    fprintf(stderr, "Warning: GASNET_SYSV_NODES not specified: running with 1 node\n");
    nodes = 1;
  }

  /* Set up 'polite' synchronization if nodes > CPU's and/or user specifies
   * setting */
  politedefault = gasnett_cpu_count() > 0 && nodes > gasnett_cpu_count();
  polite_wait = gasnett_getenv_yesno_withdefault("GASNET_POLITE_SYNC",politedefault);
  if (politedefault) {
    fprintf(stderr,
      "WARNING: Running more processes (%i) than there are physical CPU's (%i)\n",
       nodes, gasnett_cpu_count());
    if (polite_wait) {
      fprintf(stderr,
        "         enabling \"polite\" synchronization algorithms\n");
    } else {
      fprintf(stderr,
        "         but setting GASNET_POLITE_SYNC=\"%s\" in your environment has\n"
        "         disabled \"polite\" synchronization algorithms\n"
        "         Results of this run are not suitable for benchmarking\n",
        gasnet_getenv("GASNET_POLITE_SYNC"));
    }
  } else if (polite_wait) {
    fprintf(stderr,"WARNING: GASNET_POLITE_SYNC=\"%s\" is set in your environment\n"
        "         enabling \"polite\", low-performance synchronization algorithms\n",
        gasnet_getenv("GASNET_POLITE_SYNC"));
  }
  fflush(stderr);
  gasnet_set_waitmode(polite_wait ? GASNET_WAIT_BLOCK : GASNET_WAIT_SPIN);
  return nodes;
}

/* Our own, hand-rolled segmentInit function.
 * - The common version in gasnet_mmap.c is too complex to easily make
 *   supernode-aware, and the smp-case is fairly trivial.
 */
void gasnetc_sysv_segmentInit()
{
  gasneti_assert(gasneti_MaxLocalSegmentSize == 0);
  gasneti_assert(gasneti_MaxGlobalSegmentSize == 0);
  gasneti_assert(gasneti_nodes > 0);
  gasneti_assert(gasneti_mynode < gasneti_nodes);
  
  #ifndef HAVE_MMAP
    #error smp conduit cannot currently be built without mmap support
  #endif

  gasneti_segment = gasneti_mmap_segment_search(GASNETI_MMAP_LIMIT);
  GASNETI_TRACE_PRINTF(C, ("My segment: addr="GASNETI_LADDRFMT"  sz=%lu",
      GASNETI_LADDRSTR(gasneti_segment.addr), (unsigned long)gasneti_segment.size));

  gasneti_MaxLocalSegmentSize = GASNETI_PAGE_ALIGNDOWN(gasneti_segment.size/gasneti_nodes);
  gasneti_MaxGlobalSegmentSize = gasneti_MaxLocalSegmentSize;

  GASNETI_TRACE_PRINTF(C, ("MaxLocalSegmentSize = %lu   "
                     "MaxGlobalSegmentSize = %lu",
                     (unsigned long)gasneti_MaxLocalSegmentSize, 
                     (unsigned long)gasneti_MaxGlobalSegmentSize));
  gasneti_assert(gasneti_MaxLocalSegmentSize % GASNET_PAGESIZE == 0);
  gasneti_assert(gasneti_MaxGlobalSegmentSize % GASNET_PAGESIZE == 0);
  gasneti_assert(gasneti_MaxGlobalSegmentSize <= gasneti_MaxLocalSegmentSize);
}

static void gasnetc_sysv_segmentAttach(uintptr_t segsize, uintptr_t minheapoffset,
                                       gasnet_seginfo_t *seginfo,
                                       gasneti_bootstrapExchangefn_t exchangefn)
{
  /* Dimensions of max-sized segment we've already mmapped, and of new
   * subsegment that we'll actually be using */
  uintptr_t oldbase = (uintptr_t)gasneti_segment.addr;
  uintptr_t oldsize = gasneti_segment.size;
  uintptr_t oldend = (uintptr_t)gasneti_segment.addr + gasneti_segment.size;
  uintptr_t newbase, newsize, newend;
  uintptr_t topofheap = (uintptr_t)sbrk(0);
  if (topofheap == (uintptr_t)-1) 
    gasneti_fatalerror("Failed to sbrk(0):%s",strerror(errno));

  gasneti_assert(seginfo);
  gasneti_assert(segsize < gasneti_MaxGlobalSegmentSize);
  gasneti_assert(segsize % GASNET_PAGESIZE == 0);
  gasneti_assert(exchangefn);

  /* supernode size = per-node size * nodes in supernode */
  newsize = segsize * gasneti_sysvnodes;

  #if GASNETI_USE_HIGHSEGMENT
    newbase = oldend - newsize;
  #else
    newbase = oldbase;
  #endif
  newend = newbase + newsize;

  /* check if segment is above the heap (in its path) and too close */
  if ((newend > topofheap) && (topofheap + minheapoffset > newbase)) {
    uintptr_t maxsegsz;
    /* we're too close to the heap - readjust to prevent collision 
       note this allows us to return different segsizes on diff nodes
     */
    newbase = topofheap + minheapoffset;
    if (newbase >= oldend) 
      gasneti_fatalerror("minheapoffset too large to accomodate a segment");
    maxsegsz = oldend - newbase;
    if (newsize > maxsegsz) {
      GASNETI_TRACE_PRINTF(I, ("WARNING: gasneti_segmentAttach() reducing requested "
        "segsize (%lu=>%lu) to accomodate minheapoffset",
        (unsigned long)segsize, (unsigned long)(maxsegsz/gasneti_sysvnodes)));
      newsize = maxsegsz;
      segsize = maxsegsz/gasneti_sysvnodes;
    }
  }
  gasneti_assert(newbase >= oldbase && newend <= oldend);
  gasneti_assert((newbase) % GASNET_PAGESIZE == 0);
  gasneti_assert(segsize % GASNET_PAGESIZE == 0);

  /* trim off front of mmap region */
  if (newbase > oldbase)
    gasneti_munmap( (void *)oldbase, newbase - oldbase);
  /* trim off end of mmap region */
  if (newend < oldend)
    gasneti_munmap( (void *)newend, oldend - newend);

  GASNETI_TRACE_PRINTF(C, ("Final segment: segbase="GASNETI_LADDRFMT"  segsize=%lu",
    GASNETI_LADDRSTR(newbase), (unsigned long)segsize));

  /*  gather segment information */
  gasneti_segment.addr = (void*)(newbase + segsize*gasneti_mysysvnode);
  gasneti_segment.size = segsize;
  (*exchangefn)(&gasneti_segment, sizeof(gasnet_seginfo_t), seginfo);
}

static void gasnetc_init_sysv()
{
  #if GASNETI_NO_FORK
    #error GASNET_SYSV cannot yet be used with 'smp' conduit on platforms lacking fork()
  #endif

  size_t vnetsz, sninfosz, mmapsz;
  uintptr_t sysvsize;
  int i, fork_return;

  /* set up additional shared memory region for shared supernode data and AM
   * infrastructure.
   */
  vnetsz = gasneti_sysvnet_memory_needed(gasneti_nodes); 
  sninfosz = sizeof(struct gasnetc_supernode_info_t);
  sninfosz = GASNETI_ALIGNUP(sninfosz, GASNETI_SYSVNET_PAGESIZE);
  mmapsz = sninfosz + (2*vnetsz);
  gasnetc_sysvnet_region = gasneti_mmap_shared(mmapsz);
  if (gasnetc_sysvnet_region == MAP_FAILED)
    gasneti_fatalerror("mmap for shared memory Active Messages region failed!");
  gasnetc_sn_info = (struct gasnetc_supernode_info_t *)gasnetc_sysvnet_region;
  memset(gasnetc_sn_info, 0, sizeof(struct gasnetc_supernode_info_t));
  gasneti_sysv_node2pid[0] = getpid();
  gasneti_mysysvnode = gasneti_firstsysvnode = 0;
  /* Does fork() do a write flush?  Make sure */
  gasneti_atomic_set(&gasnetc_sn_info->startup_counter, 0, GASNETI_ATOMIC_WMB_POST);
  /* go fork yourself! */
  for (i = 1; i < gasneti_nodes; i++) {
    fork_return = fork();
    if (fork_return < 0) {
      gasneti_fatalerror("Fork failed!");
    } else if (fork_return > 0) {
      /* fill value into node->pid table */
      gasneti_sysv_node2pid[i] = fork_return;
    } else {
      /* child */
      gasneti_mynode = gasneti_mysysvnode = i;
      break;
    }
  }
  /* Collective call to initialize Shared AM "networks" */
  gasneti_sysvnet_init(&gasneti_request_sysvnet, ((char*)(gasnetc_sysvnet_region))+sninfosz,
                       vnetsz, 0, gasneti_nodes);
  gasneti_sysvnet_init(&gasneti_reply_sysvnet, ((char*)(gasnetc_sysvnet_region))+(sninfosz+vnetsz),
                       vnetsz, 0, gasneti_nodes);

  /* One-time 'barrier' */
  gasneti_atomic_increment(&gasnetc_sn_info->startup_counter, GASNETI_ATOMIC_REL);
  while (gasneti_atomic_read(&gasnetc_sn_info->startup_counter, GASNETI_ATOMIC_ACQ) 
            != gasneti_nodes)
    gasneti_sched_yield();
}



#endif /* GASNET_SYSV */

static int gasnetc_init(int *argc, char ***argv) {
  /*  check system sanity */
  gasnetc_check_config();

  if (gasneti_init_done) 
    GASNETI_RETURN_ERRR(NOT_INIT, "GASNet already initialized");
  gasneti_init_done = 1; /* enable early to allow tracing */

    gasneti_freezeForDebugger();

  #if GASNET_DEBUG_VERBOSE
    /* note - can't call trace macros during gasnet_init because trace system not yet initialized */
    fprintf(stderr,"gasnetc_init(): about to spawn...\n"); fflush(stderr);
  #endif

  /* add code here to bootstrap the nodes for your conduit */

  gasneti_mynode = 0;
  #if GASNET_SYSV
    gasneti_nodes = gasneti_sysvnodes = gasnetc_get_sysv_nodecount();
  #else
    gasneti_nodes = 1;
  #endif

  /* enable tracing */
  gasneti_trace_init(argc, argv);

  #if GASNET_DEBUG_VERBOSE
    fprintf(stderr,"gasnetc_init(): spawn successful - node %i/%i starting...\n", 
      gasneti_mynode, gasneti_nodes); fflush(stderr);
  #endif

  #if GASNET_SEGMENT_FAST || GASNET_SEGMENT_LARGE
    #if GASNET_SYSV
      gasnetc_sysv_segmentInit();
    #else
      gasneti_segmentInit((uintptr_t)-1, &gasnetc_bootstrapExchange);
    #endif
  #elif GASNET_SEGMENT_EVERYTHING
    /* segment is everything - nothing to do */
    #if GASNET_SYSV
      #error GASNET_SEGMENT_EVERYTHING cannot be used with GASNET_SYSV
    #endif
  #else
    #error Bad segment config
  #endif

  #if GASNET_SYSV
    gasnetc_init_sysv();
  #endif

  #if 0
    /* Enable this if you wish to use the default GASNet services for broadcasting 
        the environment from one compute node to all the others (for use in gasnet_getenv(),
        which needs to return environment variable values from the "spawning console").
        You need to provide two functions (gasnetc_bootstrapExchange and gasnetc_bootstrapBroadcast)
        which the system can safely and immediately use to broadcast and exchange information 
        between nodes (gasnetc_bootstrapBroadcast is optional but highly recommended).
       This system assumes that at least one of the compute nodes has a copy of the 
        full environment from the "spawning console" (if this is not true, you'll need to
        implement something yourself to get the values from the spawning console)
       If your job system already always propagates environment variables to all the compute
        nodes, then you probably don't need this.
     */
    gasneti_setupGlobalEnvironment(gasneti_nodes, gasneti_mynode, 
                                   gasnetc_bootstrapExchange, gasnetc_bootstrapBroadcast);
  #endif

  gasneti_auxseg_init(); /* adjust max seg values based on auxseg */

  return GASNET_OK;
}

/* ------------------------------------------------------------------------------------ */
extern int gasnet_init(int *argc, char ***argv) {
  int retval = gasnetc_init(argc, argv);
  if (retval != GASNET_OK) GASNETI_RETURN(retval);
  #if 0
    /* called within gasnet_init to allow init tracing */
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
    /*  add code here to register table[i].fnptr 
             on index (gasnet_handler_t)newindex */
    gasnetc_handler[(gasnet_handler_t)newindex] = (gasnetc_handler_fn_t)table[i].fnptr;

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
      gasnetc_handler[i] = (gasnetc_handler_fn_t)&gasneti_defaultAMHandler;
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

  /*   register any custom signal handlers required by your conduit 
   *        (e.g. to support interrupt-based messaging)
   */

  atexit(gasnetc_atexit);

  /* ------------------------------------------------------------------------------------ */
  /*  register segment  */

  gasneti_seginfo = (gasnet_seginfo_t *)gasneti_malloc(gasneti_nodes*sizeof(gasnet_seginfo_t));

  #if GASNET_SEGMENT_FAST || GASNET_SEGMENT_LARGE
    #if GASNET_SYSV
      gasnetc_sysv_segmentAttach(segsize, minheapoffset, gasneti_seginfo, &gasnetc_bootstrapExchange);
    #else
      gasneti_segmentAttach(segsize, minheapoffset, gasneti_seginfo, &gasnetc_bootstrapExchange);
    #endif
    gasneti_assert(((uintptr_t)gasneti_seginfo[gasneti_mynode].addr) % GASNET_PAGESIZE == 0);
    gasneti_assert(gasneti_seginfo[gasneti_mynode].size % GASNET_PAGESIZE == 0);
  #else
    /* GASNET_SEGMENT_EVERYTHING */
    { int i;
      for (i=0;i<gasneti_nodes;i++) {
        gasneti_seginfo[i].addr = (void *)0;
        gasneti_seginfo[i].size = (uintptr_t)-1;
      }
    }
  #endif
  segbase = gasneti_seginfo[gasneti_mynode].addr;
  segsize = gasneti_seginfo[gasneti_mynode].size;

  /* ------------------------------------------------------------------------------------ */
  /*  gather segment information */

  /*  add code here to gather the segment assignment info into 
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

  /* ensure extended API is initialized across nodes */
  gasnetc_bootstrapBarrier();

  return GASNET_OK;
}
/* ------------------------------------------------------------------------------------ */
static void gasnetc_atexit(void) {
    gasnetc_exit(0);
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

  /*  add code here to terminate the job across _all_ nodes 
           with gasneti_killmyprocess(exitcode) (not regular exit()), preferably
           after raising a SIGQUIT to inform the client of the exit
  */
  gasneti_killmyprocess(exitcode);
}

/* ------------------------------------------------------------------------------------ */
/*
  Misc. Active Message Functions
  ==============================
*/

/* Returns a (conduit-specific) token type, with (internal conduit-specific)
 * source and isRequest fields filled in.  The token is guaranteed to work
 * with gasnetc_AMGetMsgSource (which is conduit-specific). */
gasnet_token_t gasnetc_token_create(gasnet_node_t src, int isRequest)
{
  #if GASNET_DEBUG
    gasnetc_bufdesc_t *buf = gasneti_malloc(sizeof(gasnetc_bufdesc_t));
    buf->srcnode = src;
    buf->isReq = isRequest;
    return (gasnet_token_t)buf; 
  #else
    return (gasnet_token_t)src;
  #endif

}

/* Frees a token handed out by gasnetc_token_create() */
void gasnetc_token_destroy(gasnet_token_t token)
{
  #if GASNET_DEBUG
    gasneti_free(token);
  #endif
}

extern int gasnetc_AMGetMsgSource(gasnet_token_t token, gasnet_node_t *srcindex) {
  gasnet_node_t sourceid;
  GASNETI_CHECKATTACH();
  #if GASNET_DEBUG
    GASNETI_CHECK_ERRR((!token),BAD_ARG,"bad token");
  #else
    GASNETI_CHECK_ERRR((token),BAD_ARG,"bad token");
  #endif
  GASNETI_CHECK_ERRR((!srcindex),BAD_ARG,"bad src ptr");

  #if GASNET_SYSV
    #if GASNET_DEBUG
      sourceid = ((gasnetc_bufdesc_t *)token)->srcnode;
    #else
      sourceid = (gasnet_node_t)token;
    #endif
  #else 
    sourceid = 0;
  #endif
  gasneti_assert(sourceid < gasneti_nodes);
  *srcindex = sourceid;
  return GASNET_OK;
}

#if GASNET_SYSV
extern int gasnetc_AMPoll() 
{
  GASNETI_CHECKATTACH();
  return gasneti_AMSYSVPoll(0);
}
#endif

/* ------------------------------------------------------------------------------------ */
/*
  Active Message Request Functions
  ================================
*/

GASNETI_INLINE(gasnetc_ReqRepGeneric)
int gasnetc_ReqRepGeneric(gasnetc_category_t category, int isReq,
                         int dest, gasnet_handler_t handler, 
                         void *source_addr, int nbytes, void *dest_ptr, 
                         int numargs, va_list argptr) 
{
  gasnet_handlerarg_t pargs[GASNETC_MAX_ARGS];
  gasnet_token_t token = gasnetc_token_create(gasneti_mynode, isReq);
  #if GASNET_DEBUG  
    gasnetc_bufdesc_t *desc = token;
    desc->handlerRunning = 1;
    desc->replyIssued = 0;
  #endif

  gasneti_assert(dest == gasneti_mynode);
  gasneti_assert(numargs >= 0 && numargs <= GASNETC_MAX_ARGS);

  { int i;
    for(i=0; i < numargs; i++) {
      pargs[i] = (gasnet_handlerarg_t)va_arg(argptr, int);
    }
  }

  switch (category) {
    case gasnetc_Short:
      { 
        GASNETI_RUN_HANDLER_SHORT(isReq,handler,gasnetc_handler[handler],token,pargs,numargs);
      }
    break;
    case gasnetc_Medium:
      { 
        void **corethreadinfo = gasnetc_mythread();
        uint8_t *buf = NULL;
        gasneti_assert(corethreadinfo);
        if (!*corethreadinfo) { /* ensure 8-byte alignment of medium payload */
          void *tmp = gasneti_malloc(sizeof(gasnetc_threadinfo_t)+GASNETI_MEDBUF_ALIGNMENT);
          *corethreadinfo = (void*)GASNETI_ALIGNUP(tmp,GASNETI_MEDBUF_ALIGNMENT);
        }
        if (isReq) buf = ((gasnetc_threadinfo_t *)*corethreadinfo)->requestBuf;
        else       buf = ((gasnetc_threadinfo_t *)*corethreadinfo)->replyBuf;

        memcpy(buf, source_addr, nbytes);

        GASNETI_RUN_HANDLER_MEDIUM(isReq,handler,gasnetc_handler[handler],token,pargs,numargs,buf,nbytes);
      }
    break;
    case gasnetc_Long:
      { 
        if_pt(dest_ptr != source_addr) memcpy(dest_ptr, source_addr, nbytes);

        GASNETI_RUN_HANDLER_LONG(isReq,handler,gasnetc_handler[handler],token,pargs,numargs,dest_ptr,nbytes);
      }
    break;
    default: gasneti_fatalerror("bad AM category");
  }
  #if GASNET_DEBUG  
    desc->handlerRunning = 0;
  #endif
  gasnetc_token_destroy(token);
  return GASNET_OK;
}
/* ------------------------------------------------------------------------------------ */
static int gasnetc_RequestGeneric(gasnetc_category_t category, 
                         int dest, gasnet_handler_t handler, 
                         void *source_addr, int nbytes, void *dest_ptr, 
                         int numargs, va_list argptr) {
#if GASNET_SYSV
  /* smp conduit always within supernode, so skip check for 
   * gasneti_sysv_in_supernode(dest) */
  return gasneti_AMSYSV_RequestGeneric(category, dest, handler, source_addr, nbytes, 
                                       dest_ptr, numargs, argptr); 
#else
  gasneti_AMPoll(); /* ensure progress */

  return gasnetc_ReqRepGeneric(category, 1, dest, handler, 
                               source_addr, nbytes, dest_ptr, 
                               numargs, argptr); 
#endif
}
/* ------------------------------------------------------------------------------------ */
static int gasnetc_ReplyGeneric(gasnetc_category_t category, 
                         gasnet_token_t token, gasnet_handler_t handler, 
                         void *source_addr, int nbytes, void *dest_ptr, 
                         int numargs, va_list argptr) {
#if GASNET_SYSV
  /* smp conduit always within supernode, so skip check for 
   * gasneti_sysv_in_supernode(dest) */
  return gasneti_AMSYSV_ReplyGeneric(category, token, handler, source_addr, nbytes, 
                                     dest_ptr, numargs, argptr); 
#else
  int retval;
  gasnet_node_t sourceid = 0;
  #if GASNET_DEBUG  
    gasnetc_bufdesc_t *reqdesc = (gasnetc_bufdesc_t *)token;

    gasneti_assert(reqdesc->handlerRunning);
    gasneti_assert(!reqdesc->replyIssued);
    gasneti_assert(reqdesc->isReq);
    reqdesc->replyIssued = 1;
  #endif
  retval = gasnetc_ReqRepGeneric(category, 0, sourceid, handler, 
                                 source_addr, nbytes, dest_ptr, 
                                 numargs, argptr); 
  return retval;
#endif
}
/* ------------------------------------------------------------------------------------ */

extern int gasnetc_AMRequestShortM( 
                            gasnet_node_t dest,       /* destination node */
                            gasnet_handler_t handler, /* index into destination endpoint's handler table */ 
                            int numargs, ...) {
  int retval;
  va_list argptr;
  GASNETI_COMMON_AMREQUESTSHORT(dest,handler,numargs);
  va_start(argptr, numargs); /*  pass in last argument */

    /*  call the generic requestor */
    retval = gasnetc_RequestGeneric(gasnetc_Short, 
                                  dest, handler, 
                                  0, 0, 0,
                                  numargs, argptr);
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

    /*  call the generic requestor */
    retval = gasnetc_RequestGeneric(gasnetc_Medium, 
                                  dest, handler, 
                                  source_addr, nbytes, 0,
                                  numargs, argptr);
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

    /*  call the generic requestor */
    retval = gasnetc_RequestGeneric(gasnetc_Long, 
                                  dest, handler, 
                                  source_addr, nbytes, dest_addr,
                                  numargs, argptr);
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

    /*  call the generic requestor */
    retval = gasnetc_RequestGeneric(gasnetc_Long, 
                                  dest, handler, 
                                  source_addr, nbytes, dest_addr,
                                  numargs, argptr);
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

    /*  call the generic requestor */
    retval = gasnetc_ReplyGeneric(gasnetc_Short, 
                                  token, handler, 
                                  0, 0, 0,
                                  numargs, argptr);
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

    /*  call the generic requestor */
    retval = gasnetc_ReplyGeneric(gasnetc_Medium, 
                                  token, handler, 
                                  source_addr, nbytes, 0,
                                  numargs, argptr);
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

    /*  call the generic requestor */
    retval = gasnetc_ReplyGeneric(gasnetc_Long, 
                                  token, handler, 
                                  source_addr, nbytes, dest_addr,
                                  numargs, argptr);
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

gasnet_handlerentry_t const *gasnetc_get_handlertable() {
  return gasnetc_handlers;
}

/* ------------------------------------------------------------------------------------ */

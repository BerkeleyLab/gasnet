/*  $Archive:: /Ti/GASNet/template-conduit/gasnet_core.c                  $
 *     $Date: 2003/06/11 04:45:33 $
 * $Revision: 1.32 $
 * Description: GASNet lapi conduit Implementation
 * Copyright 2002, Dan Bonachea <bonachea@cs.berkeley.edu>
 * Terms of use are as specified in license.txt
 */

/* =======================================================================
 * LAPI Conduit Implementation for IBM SP.
 * Michael Welcome
 * Lawrence Berkeley National Laboratory
 * mlwelcome@lbl.gov
 * November, 2002
 * =======================================================================
 */

/* Select which exit method to use:
 * GASNETC_AM_EXIT
 *    uses active messages to propogate termination
 *    conditions to other tasks.  Returns proper error
 *    codes but may not always work if network is hosed.
 * GASNETC_SIGQUIT_EXIT
 *    kills itself with a QUIT signal, then POE notices
 *    this and kills other processes with a SIGTERM.
 *    Always produces ugly error messages from POE and
 *    Never returns proper error code.
 *
 */
#define GASNETC_AM_EXIT 1
#define GASNETC_SIGQUIT_EXIT 0
#define GASNETC_VERBOSE_EXIT 0

#include <gasnet.h>
#include <gasnet_internal.h>
#include <gasnet_handler.h>
#include <gasnet_core_internal.h>

#include <errno.h>
#include <unistd.h>
#include <string.h>
#include <sched.h>
#include <signal.h>

GASNETI_IDENT(gasnetc_IdentString_Version, "$GASNetCoreLibraryVersion: " GASNET_CORE_VERSION_STR " $");
GASNETI_IDENT(gasnetc_IdentString_ConduitName, "$GASNetConduitName: " GASNET_CORE_NAME_STR " $");

gasnet_handlerentry_t const *gasnetc_get_handlertable();

gasnet_node_t gasnetc_mynode = (gasnet_node_t)-1;
gasnet_node_t gasnetc_nodes = 0;

uintptr_t gasnetc_MaxLocalSegmentSize = 0;
uintptr_t gasnetc_MaxGlobalSegmentSize = 0;
gasnet_seginfo_t *gasnetc_seginfo = NULL;

static int gasnetc_init_done = 0; /*  true after init */
static int gasnetc_attach_done = 0; /*  true after attach */
void gasnetc_checkinit() {
    if (!gasnetc_init_done)
	gasneti_fatalerror("Illegal call to GASNet before gasnet_init() initialization");
}
void gasnetc_checkattach() {
    if (!gasnetc_attach_done)
	gasneti_fatalerror("Illegal call to GASNet before gasnet_attach() initialization");
}

/* -------------------------------------------------------------------
 * Begin: LAPI specific variables
 * -------------------------------------------------------------------
 */
lapi_handle_t  gasnetc_lapi_context;
lapi_info_t    gasnetc_lapi_info;
int            gasnetc_max_lapi_uhdr_size;
#if defined(__64BIT__)
ulong          gasnetc_max_lapi_data_size;
#else
int            gasnetc_max_lapi_data_size;
#endif

/* NOTE: this is not thread-safe */
int            gasnetc_lapi_errno;
char           gasnetc_lapi_msg[LAPI_MAX_ERR_STRING];

/* This is the official core AM handler table.  All registered
 * entries go here
 */
gasnetc_handler_fn_t gasnetc_handler[GASNETC_MAX_NUMHANDLERS] = { NULL };
void** gasnetc_remote_req_hh = NULL;
void** gasnetc_remote_reply_hh = NULL;

static volatile int got_exit_signal = 0;

#if GASNETC_AM_EXIT
/* functions and data needed for AM Exit code */
void** gasnetc_remote_amexit_hh = NULL;
void gasnetc_sigusr1_handler(int sig);
void* gasnetc_amexit_hh(lapi_handle_t *context, void *uhdr, uint *uhdr_len,
			ulong *msg_len, compl_hndlr_t **comp_h, void **uinfo);
void gasnetc_amexit_ch(lapi_handle_t *context, void *uinfo);

static volatile int gasnetc_called_exit = 0;
void gasnetc_atexit(void);
#endif

gasnetc_lapimode_t gasnetc_lapi_default_mode = gasnetc_Interrupt;

#if GASNETC_USE_IBH
volatile int gasnetc_interrupt_held[GASNETC_MAX_THREAD] = { 0 };
#endif

static gasnetc_token_queue_t gasnetc_req_q;
void gasnetc_run_handler(gasnetc_token_t *token);

gasnetc_uhdr_freelist_t gasnetc_uhdr_freelist;

/* -------------------------------------------------------------------
 * End: LAPI specific variables
 * -------------------------------------------------------------------
 */
/* ------------------------------------------------------------------------------------ */
/*
  Initialization
  ==============
*/
/* called at startup to check configuration sanity */
static void gasnetc_check_config() {
    /* (###) add code to do some sanity checks on the number of nodes, handlers
     * and/or segment sizes */ 
}

static void gasnetc_bootstrapBarrier() {
    /* (###) add code here to implement an external barrier 
       this barrier should not rely on AM or the GASNet API because it's used 
       during bootstrapping before such things are fully functional
       It need not be particularly efficient, because we only call it a few times
       and only during bootstrapping - it just has to work correctly
       If your underlying spawning or batch system provides barrier functionality,
       that would probably be a good choice for this
    */
    /* MLW: All calls to bootstrapBarrier occur after LAPI_init and thus
     * we can safely use the LAPI global fence operations
     */
    GASNETC_LCHECK(LAPI_Gfence(gasnetc_lapi_context));
}

/* --------------------------------------------------------------------------
 * NOTE: the POE job control system on the IBM SP guarantees that argc 
 *       and argv, as well as the environment variables, are distributed 
 *       to all tasks in the parallel jobs.  
 * --------------------------------------------------------------------------
 */
static int gasnetc_init(int *argc, char ***argv) {
    int task_id;
    int num_tasks;

    /*  check system sanity */
    gasnetc_check_config();

    if (gasnetc_init_done) 
	GASNETI_RETURN_ERRR(NOT_INIT, "GASNet already initialized");

    if (getenv("GASNET_FREEZE")) gasneti_freezeForDebugger();

#if DEBUG_VERBOSE
    /* note - can't call trace macros during gasnet_init because trace system not yet initialized */
    fprintf(stderr,"gasnetc_init(): about to spawn...\n"); fflush(stderr);
#endif

    /* Init the uhdr buffer free list used in GASNET AM calls */
    gasnetc_uhdr_init(GASNETC_UHDR_INIT_CNT);

    /* Init the token queue */
    gasnetc_token_queue_init(&gasnetc_req_q);

    /* (###) add code here to bootstrap the nodes for your conduit */
    memset(&gasnetc_lapi_info, 0, sizeof(lapi_info_t));
    gasnetc_lapi_info.err_hndlr = gasnetc_lapi_err_handler;
    {
	int rc = LAPI_Init(&gasnetc_lapi_context, &gasnetc_lapi_info);
	if (rc != LAPI_SUCCESS) {
	    char *errmsg = "\n*** GASNet FATAL ERROR: In the initialization of the LAPI communication layer\n\n"
		"This application must be run using the poe job scheduler with the following options: \n"
		"  poe appname -euilib us -msg_api lapi -rmpool 1 -procs nproc -nodes numnodes args...\n"
		"See the IBM poe documentation for details\n\n[NOTE: Error code %d at line %d in file %s]\n\n";
	    fprintf(stderr,errmsg,rc,__LINE__,__FILE__);
	    fflush(stderr);
	    abort();
	}
    }

    /* get task number and number of tasks in job */
    GASNETC_LCHECK(LAPI_Qenv(gasnetc_lapi_context, TASK_ID, &task_id));
    GASNETC_LCHECK(LAPI_Qenv(gasnetc_lapi_context, NUM_TASKS, &num_tasks));
    if (num_tasks < 0 || num_tasks > GASNET_MAXNODES) {
	gasneti_fatalerror("Invalid number of LAPI tasks: %d, must be < %d",
			   num_tasks,GASNET_MAXNODES);
    }
    if (task_id < 0 || task_id > GASNET_MAXNODES) {
	gasneti_fatalerror("Invalid LAPI id: %d, must be < %d",
			   task_id,GASNET_MAXNODES);
    }
    gasnetc_mynode = (gasnet_node_t)task_id;
    gasnetc_nodes = (gasnet_node_t)num_tasks;

    GASNETC_LCHECK(LAPI_Qenv(gasnetc_lapi_context, MAX_UHDR_SZ, &gasnetc_max_lapi_uhdr_size));
#if defined(__64BIT__)
    /* PSSP 3.4 is broken, LAPI_Qenv requires an int* arg but LAPI defines
     * the max data size as a ulong with value requiring 64 bit field
     */
    gasnetc_max_lapi_data_size = LAPI_MAX_MSG_SZ;
#else
    GASNETC_LCHECK(LAPI_Qenv(gasnetc_lapi_context, MAX_DATA_SZ, &gasnetc_max_lapi_data_size));
#endif
#if 0
    fprintf(stderr,"MAX LAPI UHDR SIZE = %d\n",gasnetc_max_lapi_uhdr_size);
    fprintf(stderr,"MAX LAPI DATA SIZE = %ld\n",(ulong)gasnetc_max_lapi_data_size);
    fprintf(stderr,"sizeof(gasnet_token_t) = %d\n",sizeof(gasnetc_token_t));
#endif
    if (sizeof(gasnetc_token_t) > gasnetc_max_lapi_uhdr_size) {
	gasneti_fatalerror("gasnetc_token_t is %d bytes > max lapi uhdr %d",
			   sizeof(gasnetc_token_t),gasnetc_max_lapi_uhdr_size);
    }
    if (gasnetc_max_lapi_data_size < GASNETC_AM_MAX_LONG) {
	gasneti_fatalerror("Must recompile with GASNETC_AM_MAX_LONG <= %d",
			   gasnetc_max_lapi_data_size);
    }

    /* Do we want to use polling or interrupt mode?  How to
     * communicate this?  Env variable?
     */
    {
	char *mode = NULL;
	if ( (mode=getenv("GASNET_LAPI_MODE")) != NULL ) {
	    if (strcmp(mode,"POLLING") == 0) {
		gasnetc_lapi_default_mode = gasnetc_Polling;
	    }
	}
    }
    if (gasnetc_lapi_default_mode == gasnetc_Interrupt) {
	/* turn on interrupt mode */
	GASNETC_LCHECK(LAPI_Senv(gasnetc_lapi_context, INTERRUPT_SET, 1));
    } else {
	/* polling mode, turn off interrupts */
	GASNETC_LCHECK(LAPI_Senv(gasnetc_lapi_context, INTERRUPT_SET, 0));
    }

    /* collect remote addresses of header handler function */
    gasnetc_remote_req_hh = (void**)gasneti_malloc_inhandler(num_tasks*sizeof(void*));
    gasnetc_remote_reply_hh = (void**)gasneti_malloc_inhandler(num_tasks*sizeof(void*));
    GASNETC_LCHECK(LAPI_Address_init(gasnetc_lapi_context,
				     (void*)&gasnetc_lapi_AMreq_hh,
				     gasnetc_remote_req_hh));
    GASNETC_LCHECK(LAPI_Address_init(gasnetc_lapi_context,
				     (void*)&gasnetc_lapi_AMreply_hh,
				     gasnetc_remote_reply_hh));
#if GASNETC_AM_EXIT
    gasnetc_remote_amexit_hh = (void**)gasneti_malloc_inhandler(num_tasks*sizeof(void*));
    GASNETC_LCHECK(LAPI_Address_init(gasnetc_lapi_context,
				     (void*)&gasnetc_amexit_hh,
				     gasnetc_remote_amexit_hh));
#endif
    
#if DEBUG_VERBOSE
    fprintf(stderr,"gasnetc_init(): spawn successful - node %i/%i starting...\n", 
	    gasnetc_mynode, gasnetc_nodes); fflush(stderr);
#endif

#if defined(GASNET_SEGMENT_FAST) || defined(GASNET_SEGMENT_LARGE)
    { 
	/* Add code here to determine optimistic maximum segment size and
	 * the MIN(MaxLocalSegmentSize) over all nodes 
	gasnetc_MaxLocalSegmentSize = ###;
	gasnetc_MaxGlobalSegmentSize = ###;
	 * - OR -
	 * it may be appropriate to use gasneti_segmentInit() here to set 
	   gasnetc_MaxLocalSegmentSize and gasnetc_MaxGlobalSegmentSize,
	   if your conduit can use memory anywhere in the address space
	   (you may want to tune GASNETI_MMAP_MAX_SIZE to limit the max size)
	*/
	/* On the SP, mmaped regions are allocated in segments distinct from
	 * static, stack and heap data.  gasneti_segmentInit should work
	 * well.
	 */
	gasneti_segmentInit(&gasnetc_MaxLocalSegmentSize,&gasnetc_MaxGlobalSegmentSize,
			    (uintptr_t)-1,gasnetc_nodes,gasnetc_lapi_exchange);
    }
#elif defined(GASNET_SEGMENT_EVERYTHING)
    gasnetc_MaxLocalSegmentSize =  (uintptr_t)-1;
    gasnetc_MaxGlobalSegmentSize = (uintptr_t)-1;
#else
#error Bad segment config
#endif

    /* set up atexit handler to call gasnet_exit in the
     * case where a program returns from main without
     * calling gasnet_exit()
     */
    atexit(gasnetc_atexit);

    gasnetc_init_done = 1;  

    return GASNET_OK;
}

/* ------------------------------------------------------------------------------------ */
extern int gasnet_init(int *argc, char ***argv) {
    int retval = gasnetc_init(argc, argv);
    if (retval != GASNET_OK) GASNETI_RETURN(retval);
    gasneti_trace_init();
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
	/* (###) add code here to register table[i].fnptr 
	   on index (gasnet_handler_t)newindex */
	gasnetc_handler[newindex] = table[i].fnptr;
	GASNETI_TRACE_PRINTF(C,("Registered handler %x at index %d",
				table[i].fnptr,newindex));

	if (dontcare) table[i].index = newindex;
	(*numregistered)++;
    }
    return GASNET_OK;
}
/* ------------------------------------------------------------------------------------ */
#ifdef GASNETC_SIGQUIT_EXIT
void gasnetc_sigterm_handler(int sig)
{
    got_exit_signal = 1;
#if GASNETC_VERBOSE_EXIT
    fprintf(stderr,">> GASNET_SIGTERM_HNDLR[%d]: in SIGTERM\n",gasnetc_mynode);
    fflush(stderr);
#endif
    pthread_kill(pthread_self(),SIGQUIT);
}
#endif

/* ------------------------------------------------------------------------------------ */
extern int gasnetc_attach(gasnet_handlerentry_t *table, int numentries,
                          uintptr_t segsize, uintptr_t minheapoffset) {
    void *segbase = NULL;
  
    GASNETI_TRACE_PRINTF(C,("gasnetc_attach(table (%i entries), segsize=%lu, minheapoffset=%lu)",
                            numentries, (unsigned long)segsize, (unsigned long)minheapoffset));

    if (!gasnetc_init_done) 
	GASNETI_RETURN_ERRR(NOT_INIT, "GASNet attach called before init");
    if (gasnetc_attach_done) 
	GASNETI_RETURN_ERRR(NOT_INIT, "GASNet already attached");

    /*  check argument sanity */
#if defined(GASNET_SEGMENT_FAST) || defined(GASNET_SEGMENT_LARGE)
    if ((segsize % GASNET_PAGESIZE) != 0) 
	GASNETI_RETURN_ERRR(BAD_ARG, "segsize not page-aligned");
    if (segsize > gasnetc_getMaxLocalSegmentSize()) 
	GASNETI_RETURN_ERRR(BAD_ARG, "segsize too large");
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
	    GASNETI_RETURN_ERRR(RESOURCE,"Error registering core API handlers");
	assert(numreg == len);
    }

    { /*  extended API handlers */
	gasnet_handlerentry_t *etable = (gasnet_handlerentry_t *)gasnete_get_handlertable();
	int len = 0;
	int numreg = 0;
	assert(etable);
	while (etable[len].fnptr) len++; /* calc len */
	if (gasnetc_reghandlers(etable, len, 64, 127, 0, &numreg) != GASNET_OK)
	    GASNETI_RETURN_ERRR(RESOURCE,"Error registering extended API handlers");
	assert(numreg == len);
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

	assert(numreg1 + numreg2 == numentries);
    }

    /* ------------------------------------------------------------------------------------ */
    /*  register fatal signal handlers */

    /* catch fatal signals and convert to SIGQUIT */
    gasneti_registerSignalHandlers(gasneti_defaultSignalHandler);

    /*  (###) register any custom signal handlers required by your conduit 
     *        (e.g. to support interrupt-based messaging)
     */
#if GASNETC_AM_EXIT
    gasneti_reghandler(SIGUSR1, gasnetc_sigusr1_handler );
#elif GASNETC_SIGQUIT_EXIT
    gasneti_reghandler(SIGTERM, gasnetc_sigterm_handler );
#endif
    
    /* ------------------------------------------------------------------------------------ */
    /*  register segment  */

    /* use gasneti_malloc_inhandler during bootstrapping because we can't assume the 
       hold/resume interrupts functions are operational yet */
    gasnetc_seginfo = (gasnet_seginfo_t *)gasneti_malloc_inhandler(gasnetc_nodes*sizeof(gasnet_seginfo_t));

#if defined(GASNET_SEGMENT_FAST) || defined(GASNET_SEGMENT_LARGE)
    if (segsize == 0) segbase = NULL; /* no segment */
    else {
	/* (###) add code here to choose and register a segment 
	   (ensuring alignment across all nodes if this conduit sets GASNET_ALIGNED_SEGMENTS==1) 
	   you can use gasneti_segmentAttach() here if you used gasneti_segmentInit() above
	*/
	assert(segsize % GASNET_PAGESIZE == 0);
	gasneti_segmentAttach(segsize,minheapoffset,gasnetc_seginfo,gasnetc_lapi_exchange);
	segbase = gasnetc_seginfo[gasnetc_mynode].addr;
	segsize = gasnetc_seginfo[gasnetc_mynode].size;
	assert(((uintptr_t)segbase) % GASNET_PAGESIZE == 0);
	assert(segsize % GASNET_PAGESIZE == 0);
    }
#else
    /* GASNET_SEGMENT_EVERYTHING */
    {
	int i;
	for (i=0;i<gasnetc_nodes;i++) {
	    gasnetc_seginfo[i].addr = (void *)0;
	    gasnetc_seginfo[i].size = (uintptr_t)-1;
	}
	segbase = gasnetc_seginfo[gasnetc_mynode].addr;
	segsize = gasnetc_seginfo[gasnetc_mynode].size;
    }
#endif

    /* ------------------------------------------------------------------------------------ */
    /*  primary attach complete */
    gasnetc_attach_done = 1;
    gasnetc_bootstrapBarrier();

    GASNETI_TRACE_PRINTF(C,("gasnetc_attach(): primary attach complete"));

    {
	int i;
	for (i = 0; i < gasnetc_nodes; i++) {
	    GASNETI_TRACE_PRINTF(C,("For node %d seginfo.addr = %x seginfo.size = %d",
				    i,gasnetc_seginfo[i].addr,
				    gasnetc_seginfo[i].size));
	}
    }

#if GASNET_ALIGNED_SEGMENTS == 1
    { int i; /*  check that segments are aligned */
    for (i=0; i < gasnetc_nodes; i++) {
        if (gasnetc_seginfo[i].size != 0 && gasnetc_seginfo[i].addr != segbase) 
	    gasneti_fatalerror("Failed to acquire aligned segments for GASNET_ALIGNED_SEGMENTS");
    }
    }
#endif

    gasnete_init(); /* init the extended API */

    /* ensure extended API is initialized across nodes */
    gasnetc_bootstrapBarrier();

    return GASNET_OK;
}
    
/* ------------------------------------------------------------------------------------ */
/* All the common cleanup tasks between the various exit routines */
static void gasnetc_exit_cleanup(void) {
    GASNETI_TRACE_PRINTF(C,("GASNETC_EXIT: UHDR_BUF HWM %d, numfree %d, numalloc %d",
			    gasnetc_uhdr_freelist.high_water_mark,
			    gasnetc_uhdr_freelist.numfree,
			    gasnetc_uhdr_freelist.numalloc));
#if DEBUG_VERBOSE
    fprintf(stderr,"GASNETC_EXIT: NODE %d UHDR_BUF HWM %d, numfree %d, numalloc %d\n",
	    gasnetc_mynode,
	    gasnetc_uhdr_freelist.high_water_mark,
	    gasnetc_uhdr_freelist.numfree,
	    gasnetc_uhdr_freelist.numalloc);
#endif

    gasneti_trace_finish();
    if (fflush(stdout)) 
      gasneti_fatalerror("failed to flush stdout in gasnetc_exit: %s", strerror(errno));
    if (fflush(stderr)) 
      gasneti_fatalerror("failed to flush stderr in gasnetc_exit: %s", strerror(errno));
}
/* ------------------------------------------------------------------------------------ */
#if GASNETC_AM_EXIT
static int amexit_exitcode = 0;
void gasnetc_sigusr1_handler(int sig)
{
#if GASNETC_VERBOSE_EXIT
    fprintf(stderr,">> GASNET_SIGUSR1_HNDLR[%d]: called\n",gasnetc_mynode);
    fflush(stderr);
#endif
    gasnet_exit(amexit_exitcode);
}
void gasnetc_sigalarm_handler(int sig)
{
#if GASNETC_VERBOSE_EXIT
    fprintf(stderr,">> GASNET_SIGALARM_HNDLR[%d]: called\n",gasnetc_mynode);
    fflush(stderr);
#endif
    gasneti_reghandler(SIGQUIT, SIG_DFL);
    kill(getpid(),SIGQUIT);
}


void* gasnetc_amexit_hh(lapi_handle_t *context, void *uhdr, uint *uhdr_len,
			ulong *msg_len, compl_hndlr_t **comp_h, void **uinfo)
{
    *comp_h = gasnetc_amexit_ch;
    *uinfo = NULL;
    amexit_exitcode = *(int*)uhdr;
    return NULL;
}
void gasnetc_amexit_ch(lapi_handle_t *context, void *uinfo)
{
    if (got_exit_signal) {
	/* Ignore... in process of exiting */
	return;
    }
    got_exit_signal = 1;
    /* force signal handler to execute so that it can call gasnet_exit.
     * Dont want to do it from within a LAPI thread... doesnt work.  */
    kill(getpid(),SIGUSR1);
}
void gasnetc_atexit(void)
{
    if (gasnetc_called_exit)
	return;
    gasnetc_exit(0);
}
    
extern void gasnetc_exit(int exitcode) {
    gasnetc_called_exit = 1;
    
    /* once we start a shutdown, ignore all future SIGQUIT signals or we risk reentrancy */
    gasneti_reghandler(SIGQUIT, SIG_IGN);
    gasneti_reghandler(SIGUSR1, SIG_IGN);
    gasneti_reghandler(SIGTERM, SIG_IGN);

    {  /* ensure only one thread ever continues past this point */
      static gasneti_mutex_t exit_lock = GASNETI_MUTEX_INITIALIZER;
      gasneti_mutex_lock(&exit_lock);
    }

#if GASNETC_VERBOSE_EXIT
    fprintf(stderr,">> GASNET_EXIT[%d]: reset sigquit,sigusr1,sigterm\n",gasnetc_mynode);
    fprintf(stderr,">> GASNET_EXIT[%d]: setting 5 second alarm\n");
    fflush(stderr);
#endif

    gasnetc_exit_cleanup();
    /* sleep a variable amount of time, attempting to avoid everyone
     * spraying AM messages to each other at once.
     */
    usleep((gasnetc_mynode % 10) * 100000 );

    /* Experience has shown that using AMs in error cases may hang
     * in many situations.  Register an alarm handler here which
     * will forcefully kill the task if called.
     */
    gasneti_reghandler(SIGALRM, gasnetc_sigalarm_handler );
    /* set the alarm for avoid hangs... this time may need to be
     * scaled by the number of tasks/threads
     */
    alarm(5);

    if (got_exit_signal) {
	/* async exit, remote thread died and we got here because of that.  Just exit */
#if GASNETC_VERBOSE_EXIT
	fprintf(stderr,">> GASNET_EXIT[%d]: async exit\n",gasnetc_mynode);
	fflush(stderr);
#endif

	/* NOTE: LAPI hangs if we call LAPI_Term() here, even in good, clean,
	 * collective exits.  Could wait for alarm to kill us forcefully, but
	 * just calling _exit does not seem to cause problems.
	 */
	_exit(exitcode);
	
    } else {
	/* synchronous exit */
	/* We are the first task to enter gasnet_exit... Send AM to all others */

	gasnet_node_t node;
	lapi_cntr_t cntr;

	/* Set got_exit_signal locally */
	got_exit_signal = 1;

#if GASNETC_VERBOSE_EXIT
	fprintf(stderr,">> GASNET_EXIT[%d]: Sending exit AM to all others\n",gasnetc_mynode);
	fflush(stderr);
#endif
	LAPI_Setcntr(gasnetc_lapi_context,&cntr,0);
	for (node=0; node < gasnetc_nodes; node++) {
	    if (node == gasnetc_mynode) continue;
	    GASNETC_LCHECK(LAPI_Amsend(gasnetc_lapi_context, node,
			       gasnetc_remote_amexit_hh[node],
			       (void*)&exitcode, sizeof(exitcode), NULL, 0,
			       NULL, &cntr, NULL));
	}

	/* wait for local completion so arg to Amsend does not go out of scope */
	GASNETC_LCHECK(LAPI_Waitcntr(gasnetc_lapi_context,&cntr,0,NULL));
    	    
#if GASNETC_VERBOSE_EXIT
	fprintf(stderr,">> GASNET_EXIT[%d]: Finished sending exit AMs\n",gasnetc_mynode);
	fprintf(stderr,">> GASNET_EXIT[%d]: about to call LAPI_TERM\n",gasnetc_mynode);
	fflush(stderr);
#endif
	
	GASNETC_LCHECK(LAPI_Term(gasnetc_lapi_context));

	/* cancel previous alarm and exit normally */
	alarm(0);

#if GASNETC_VERBOSE_EXIT
	fprintf(stderr,">> GASNET_EXIT[%d]: about to call _exit\n",gasnetc_mynode);
	fflush(stderr);
#endif

	_exit(exitcode);
    }

    /* should never get here */
    abort();
}

#elif GASNETC_SIGQUIT_EXIT
extern void gasnetc_exit(int exitcode) {
    /* once we start a shutdown, ignore all future SIGQUIT signals or we risk reentrancy */
    gasneti_reghandler(SIGQUIT, SIG_IGN);
    gasneti_reghandler(SIGTERM, SIG_IGN);

    {  /* ensure only one thread ever continues past this point */
      static gasneti_mutex_t exit_lock = GASNETI_MUTEX_INITIALIZER;
      gasneti_mutex_lock(&exit_lock);
    }

    gasnetc_exit_cleanup();

#if GASNETC_VERBOSE_EXIT
    fprintf(stderr,">> GASNET_EXIT[%d]: reset sigterm and sigquit handlers\n",gasnetc_mynode);
    fflush(stderr);
#endif

    if (got_exit_signal) {
	/* async exit, remote thread died */
#if GASNETC_VERBOSE_EXIT
	fprintf(stderr,">> GASNET_EXIT[%d]: async exit\n",gasnetc_mynode);
	fflush(stderr);
#endif

	/* NOTE: LAPI hangs if we call LAPI_Term() here... */
	_exit(0);
	
    } else {
	/* synchronous exit */
	sleep(1); /* pause to ensure everyone has written trace if this is a collective exit */
#if GASNETC_VERBOSE_EXIT
	fprintf(stderr,">> GASNET_EXIT[%d]: about to call LAPI_TERM\n",gasnetc_mynode);
	fflush(stderr);
#endif
	GASNETC_LCHECK(LAPI_Term(gasnetc_lapi_context));

#if GASNETC_VERBOSE_EXIT
	fprintf(stderr,">> GASNET_EXIT[%d]: LAPI_Term called\n",gasnetc_mynode);
	fflush(stderr);
#endif

	/* restore default SIGQUIT handler */
	gasneti_reghandler(SIGQUIT, SIG_DFL);
#if GASNETC_VERBOSE_EXIT
	fprintf(stderr,">> GASNET_EXIT[%d]: sync exit, set deflt sigquit and killing\n",gasnetc_mynode);
	fflush(stderr);
#endif
	/* Send myself SIGQUIT, this will cause POE to propogate SIGTERM to other tasks */
	kill(getpid(),SIGQUIT);
	
	_exit(exitcode);
    }

    /* should never get here */
    abort();
}

#else
/* This was the origional exit code, and it hangs on all but clean, collective exits */
extern void gasnetc_exit(int exitcode) {
    /* once we start a shutdown, ignore all future SIGQUIT signals or we risk reentrancy */
    gasneti_reghandler(SIGQUIT, SIG_IGN);

    {  /* ensure only one thread ever continues past this point */
      static gasneti_mutex_t exit_lock = GASNETI_MUTEX_INITIALIZER;
      gasneti_mutex_lock(&exit_lock);
    }

#if GASNETC_VERBOSE_EXIT
	fprintf(stderr,">> GASNET_EXIT[%d]: \n",gasnetc_mynode);
	fflush(stderr);
#endif

    gasnetc_exit_cleanup();

    /* orig exit code -- hangs in most error cases */
    gasneti_sched_yield();
    sleep(1); /* pause to ensure everyone has written trace if this is a collective exit */

    /* (###) add code here to terminate the job across all nodes with _exit(exitcode) */
    GASNETC_LCHECK(LAPI_Term(gasnetc_lapi_context));
    _exit(exitcode);
 
    /* should never get here */
    abort();
}
#endif

/* ------------------------------------------------------------------------------------ */
/*
  Job Environment Queries
  =======================
*/
extern int gasnetc_getSegmentInfo(gasnet_seginfo_t *seginfo_table, int numentries) {
    GASNETC_CHECKATTACH();
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
    gasnet_node_t sourceid;
    GASNETC_CHECKATTACH();
    if (!token) GASNETI_RETURN_ERRR(BAD_ARG,"bad token");
    if (!srcindex) GASNETI_RETURN_ERRR(BAD_ARG,"bad src ptr");

    /* (###) add code here to write the source index into sourceid */
    sourceid = ((gasnetc_token_t*)token)->buf.msg.sourceId;

    assert(sourceid < gasnetc_nodes);
    *srcindex = sourceid;
    return GASNET_OK;
}

extern int gasnetc_AMPoll() {
    int retval;
    GASNETC_CHECKATTACH();

    /* Check if any request handlers are queued for processing
     * and execute all on the list
     */
    {
	gasnetc_token_t *q_token = NULL;
	while ( (q_token = gasnetc_token_dequeue(&gasnetc_req_q, 0)) != NULL ) {
	    gasnetc_run_handler(q_token);
	    /* deallocate the token, it was allocated in the header handler */
	    gasnetc_uhdr_free(q_token);
	}
    }

    /* NOTE: a call to probe is not needed when LAPI is executing
     * in interrupt mode.  In that mode, polling can sometimes
     * decrease performance due to lock contention between the
     * notification thread and this thread.
     * If this funciton is called from a spinloop, LAPI should
     * be switched to POLLING mode before the AMPoll calls,
     * and switched back to the default mode afterwards.
     * We do this in the BLOCKUNTIL macro.
     */
    GASNETC_LCHECK(LAPI_Probe(gasnetc_lapi_context));

    /* Check if any request handlers are queued for processing
     * and execute all on the list
     */
    {
	gasnetc_token_t *q_token = NULL;
	while ( (q_token = gasnetc_token_dequeue(&gasnetc_req_q, 0)) != NULL ) {
	    gasnetc_run_handler(q_token);
	    /* deallocate the token, it was allocated in the header handler */
	    gasnetc_uhdr_free(q_token);
	}
    }
    
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
    lapi_cntr_t o_cntr;
    int cur_cntr, token_len, i;
    gasnetc_token_t token;
    gasnetc_msg_t  *msg = &token.buf.msg;
    va_list argptr;

    GASNETC_CHECKATTACH();
    if_pf (dest >= gasnetc_nodes) GASNETI_RETURN_ERRR(BAD_ARG,"node index too high");
    GASNETI_TRACE_AMREQUESTSHORT(dest,handler,numargs);

    msg->handlerId = handler;
    msg->sourceId = gasnetc_mynode;
    GASNETC_MSG_SETFLAGS(msg,1,gasnetc_Short,0,numargs);
    msg->destLoc = (uintptr_t)NULL;
    msg->dataLen = (size_t)0;
    msg->uhdrLoc = (uintptr_t)NULL;

    /* copy the arguments */
    va_start(argptr, numargs);
    for (i = 0; i < numargs; i++) {
	msg->args[i] = va_arg(argptr, gasnet_handlerarg_t);
    }
    va_end(argptr);

    /* Do Loopback check here */
#if GASNETC_ENABLE_LOOPBACK
    if (dest == gasnetc_mynode) {
	gasnetc_handler_fn_t pfn = gasnetc_handler[handler];
	RUN_HANDLER_SHORT(pfn,&token,&msg->args[0],numargs);
	GASNETI_RETURN(GASNET_OK);
    }
#endif
    
    /* only send as many much of token structure as necessary */
    token_len = TOKEN_LEN(numargs);
    
    /* issue the request for remote execution of the user handler */ 
    assert( token_len <= gasnetc_max_lapi_uhdr_size);
    GASNETC_LCHECK(LAPI_Setcntr(gasnetc_lapi_context,&o_cntr,0));
    GASNETC_LCHECK(LAPI_Amsend(gasnetc_lapi_context, dest,
			       gasnetc_remote_req_hh[dest],
			       (void*)&token, token_len, NULL, 0,
			       NULL, &o_cntr, NULL));
    
    /* wait for the Amsend call to complete locally */
    GASNETC_LCHECK(LAPI_Waitcntr(gasnetc_lapi_context,&o_cntr,1,&cur_cntr));

    retval = GASNET_OK;
    GASNETI_RETURN(retval);
}

extern int gasnetc_AMRequestMediumM( 
    gasnet_node_t dest,      /* destination node */
    gasnet_handler_t handler, /* index into destination endpoint's handler table */ 
    void *source_addr, size_t nbytes,   /* data payload */
    int numargs, ...) {
    int retval;
    lapi_cntr_t o_cntr;
    int cur_cntr, token_len, i;
    gasnetc_token_t token;
    gasnetc_msg_t  *msg = &token.buf.msg;
    void *udata_start = NULL;
    int udata_avail;
    int udata_packed = 0;
    va_list argptr;

    GASNETC_CHECKATTACH();
    if_pf (dest >= gasnetc_nodes) GASNETI_RETURN_ERRR(BAD_ARG,"node index too high");
    GASNETI_TRACE_AMREQUESTMEDIUM(dest,handler,source_addr,nbytes,numargs);

    msg->handlerId = handler;
    msg->sourceId = gasnetc_mynode;
    GASNETC_MSG_SETFLAGS(msg,1,gasnetc_Medium,0,numargs);
    msg->destLoc = (uintptr_t)NULL;
    msg->dataLen = (size_t)nbytes;
    msg->uhdrLoc = (uintptr_t)NULL;

    /* copy the arguments */
    va_start(argptr, numargs);
    for (i = 0; i < numargs; i++) {
	msg->args[i] = va_arg(argptr, gasnet_handlerarg_t);
    }
    va_end(argptr);
    udata_start = (void*)&msg->args[numargs];
    token_len = TOKEN_LEN(numargs);
    udata_avail = sizeof(gasnetc_token_t) - token_len;

    /* can we pack the data into the uhdr? */
    if (nbytes <= udata_avail) {
	memcpy(udata_start,source_addr,nbytes);
	token_len += nbytes;
	udata_packed = 1;
	GASNETC_MSG_SET_PACKED(msg);
    }

    /* Do Loopback check here */
#if GASNETC_ENABLE_LOOPBACK
    if (dest == gasnetc_mynode) {
	gasnetc_handler_fn_t pfn = gasnetc_handler[handler];
	void *destloc;
	if (udata_packed) {
	    destloc = udata_start;
	} else {
	    destloc = gasneti_malloc_inhandler(nbytes > 0 ? nbytes : 1);
	    memcpy(destloc,source_addr,nbytes);
	}
	RUN_HANDLER_MEDIUM(pfn,&token,&msg->args[0],numargs,destloc,nbytes);
	if (! udata_packed) {
	    gasneti_free(destloc);
	}
	GASNETI_RETURN(GASNET_OK);
    }
#endif

    /* issue the request for remote execution of the user handler */
    assert( token_len <= gasnetc_max_lapi_uhdr_size);
    GASNETC_LCHECK(LAPI_Setcntr(gasnetc_lapi_context,&o_cntr,0));
    GASNETC_LCHECK(LAPI_Amsend(gasnetc_lapi_context, dest,
			       gasnetc_remote_req_hh[dest],
			       (void*)&token, token_len,
			       (udata_packed ? NULL : source_addr),
			       (udata_packed ? 0    : nbytes),
			       NULL, &o_cntr, NULL));
    
    /* wait for the Amsend call to complete locally */
    GASNETC_LCHECK(LAPI_Waitcntr(gasnetc_lapi_context,&o_cntr,1,&cur_cntr));

    retval = GASNET_OK;
    GASNETI_RETURN(retval);
}

extern int gasnetc_AMRequestLongM( gasnet_node_t dest,        /* destination node */
				   gasnet_handler_t handler, /* index into destination endpoint's handler table */ 
				   void *source_addr, size_t nbytes,   /* data payload */
				   void *dest_addr,                    /* data destination on destination node */
				   int numargs, ...) {
    int retval;
    lapi_cntr_t o_cntr;
    int cur_cntr, token_len, i;
    gasnetc_token_t token;
    gasnetc_msg_t  *msg = &token.buf.msg;
    void *udata_start = NULL;
    int udata_avail;
    int udata_packed = 0;
    va_list argptr;

    GASNETC_CHECKATTACH();
    gasnetc_boundscheck(dest, dest_addr, nbytes);
    if_pf (dest >= gasnetc_nodes) GASNETI_RETURN_ERRR(BAD_ARG,"node index too high");
    if_pf (((uintptr_t)dest_addr) < ((uintptr_t)gasnetc_seginfo[dest].addr) ||
	   ((uintptr_t)dest_addr) + nbytes > 
           ((uintptr_t)gasnetc_seginfo[dest].addr) + gasnetc_seginfo[dest].size) 
	GASNETI_RETURN_ERRR(BAD_ARG,"destination address out of segment range");

    GASNETI_TRACE_AMREQUESTLONG(dest,handler,source_addr,nbytes,dest_addr,numargs);

    msg->handlerId = handler;
    msg->sourceId = gasnetc_mynode;
    GASNETC_MSG_SETFLAGS(msg,1,gasnetc_Long,0,numargs);
    msg->destLoc = (uintptr_t)dest_addr;
    msg->dataLen = (size_t)nbytes;
    msg->uhdrLoc = (uintptr_t)NULL;

    /* copy the arguments */
    va_start(argptr, numargs);
    for (i = 0; i < numargs; i++) {
	msg->args[i] = va_arg(argptr, gasnet_handlerarg_t);
    }
    va_end(argptr);
    udata_start = (void*)&msg->args[numargs];
    token_len = TOKEN_LEN(numargs);
    udata_avail = sizeof(gasnetc_token_t) - token_len;

    /* Do Loopback check here */
#if GASNETC_ENABLE_LOOPBACK
    if (dest == gasnetc_mynode) {
	gasnetc_handler_fn_t pfn = gasnetc_handler[handler];
	/* must do local copy of data from source to dest */
	memcpy((char*)dest_addr,source_addr,nbytes);
	RUN_HANDLER_LONG(pfn,&token,&msg->args[0],numargs,dest_addr,nbytes);
	GASNETI_RETURN(GASNET_OK);
    }
#endif

    /* can we pack the data into the uhdr? */
    if (nbytes <= udata_avail) {
	memcpy(udata_start,source_addr,nbytes);
	token_len += nbytes;
	udata_packed = 1;
	GASNETC_MSG_SET_PACKED(msg);
    }

    /* issue the request for remote execution of the user handler */
    assert( token_len <= gasnetc_max_lapi_uhdr_size);
    GASNETC_LCHECK(LAPI_Setcntr(gasnetc_lapi_context,&o_cntr,0));
    GASNETC_LCHECK(LAPI_Amsend(gasnetc_lapi_context, dest,
			       gasnetc_remote_req_hh[dest],
			       (void*)&token, token_len,
			       (udata_packed ? NULL : source_addr),
			       (udata_packed ? 0    : nbytes),
			       NULL, &o_cntr, NULL));
    
    /* wait for the Amsend call to complete locally */
    GASNETC_LCHECK(LAPI_Waitcntr(gasnetc_lapi_context,&o_cntr,1,&cur_cntr));

    retval = GASNET_OK;
    GASNETI_RETURN(retval);
}

extern int gasnetc_AMRequestLongAsyncM( gasnet_node_t dest,        /* destination node */
					gasnet_handler_t handler, /* index into destination endpoint's handler table */ 
					void *source_addr, size_t nbytes,   /* data payload */
					void *dest_addr,                    /* data destination on destination node */
					int numargs, ...) {

    int token_len, i;
    gasnetc_token_t *token;
    gasnetc_msg_t  *msg;
    void *udata_start = NULL;
    int udata_avail;
    int udata_packed = 0;
    int retval;
    va_list argptr;
    GASNETC_CHECKATTACH();
  
    gasnetc_boundscheck(dest, dest_addr, nbytes);
    if_pf (dest >= gasnetc_nodes) GASNETI_RETURN_ERRR(BAD_ARG,"node index too high");
    if_pf (((uintptr_t)dest_addr) < ((uintptr_t)gasnetc_seginfo[dest].addr) ||
	   ((uintptr_t)dest_addr) + nbytes > 
           ((uintptr_t)gasnetc_seginfo[dest].addr) + gasnetc_seginfo[dest].size) 
	GASNETI_RETURN_ERRR(BAD_ARG,"destination address out of segment range");

    GASNETI_TRACE_AMREQUESTLONGASYNC(dest,handler,source_addr,nbytes,dest_addr,numargs);

    token = gasnetc_uhdr_alloc();
    msg = &token->buf.msg;
    msg->handlerId = handler;
    msg->sourceId = gasnetc_mynode;
    GASNETC_MSG_SETFLAGS(msg,1,gasnetc_AsyncLong,0,numargs);
    msg->destLoc = (uintptr_t)dest_addr;
    msg->dataLen = (size_t)nbytes;
    /* stash the location of token, so that it can be deallocated
     * in the completion handler of the corresponding GASNET reply
     * when it executes on this node.
     */
    msg->uhdrLoc = (uintptr_t)token;

    /* copy the arguments */
    va_start(argptr, numargs);
    for (i = 0; i < numargs; i++) {
	msg->args[i] = va_arg(argptr, gasnet_handlerarg_t);
    }
    va_end(argptr);
    udata_start = (void*)&msg->args[numargs];
    token_len = TOKEN_LEN(numargs);
    udata_avail = sizeof(gasnetc_token_t) - token_len;

    /* Do Loopback check here */
#if GASNETC_ENABLE_LOOPBACK
    if (dest == gasnetc_mynode) {
	gasnetc_handler_fn_t pfn = gasnetc_handler[handler];
	/* must do local copy of data from source to dest */
	memcpy((char*)dest_addr,source_addr,nbytes);
	/* Note: we will deallocate the token below, just to be safe
	 * remove the address from the uhdrLoc field so that no-one
	 * else messes with it.
	 */
	msg->uhdrLoc = (uintptr_t)NULL;
	RUN_HANDLER_LONG(pfn,token,&msg->args[0],numargs,dest_addr,nbytes);
	gasnetc_uhdr_free(token);
	GASNETI_RETURN(GASNET_OK);
    }
#endif

    /* can we pack the data into the uhdr? */
    if (nbytes <= udata_avail) {
	memcpy(udata_start,source_addr,nbytes);
	token_len += nbytes;
	udata_packed = 1;
	GASNETC_MSG_SET_PACKED(msg);
    }
    
    /* issue the request for remote execution of the user handler */
    /* NOTE: no LAPI counters are used here, the token will be deallocated
     * later (by the completion handler when the reply handler is executed).
     * It is up to the client not to modify the source_addr data until his 
     * reply handler runs.
     */
    assert( token_len <= gasnetc_max_lapi_uhdr_size);
    GASNETC_LCHECK(LAPI_Amsend(gasnetc_lapi_context, dest,
			       gasnetc_remote_req_hh[dest],
			       (void*)token, token_len,
			       (udata_packed ? NULL : source_addr),
			       (udata_packed ? 0    : nbytes),
			       NULL, NULL, NULL));
    
    retval = GASNET_OK;
    GASNETI_RETURN(retval);
}

extern int gasnetc_AMReplyShortM( 
    gasnet_token_t token,       /* token provided on handler entry */
    gasnet_handler_t handler, /* index into destination endpoint's handler table */ 
    int numargs, ...) {
    int retval;

    gasnetc_msg_t *msg = &((gasnetc_token_t *)token)->buf.msg;
    uint requester = (uint)msg->sourceId;
    lapi_cntr_t o_cntr;
    int token_len, i, cur_cntr;

    va_list argptr;
    GASNETI_TRACE_AMREPLYSHORT(token,handler,numargs);
    va_start(argptr, numargs); /*  pass in last argument */

    /* we can re-use the token passed into us.  It was allocated in the
     * LAPI header handler to be large enough to contain the maximum
     * number of arguments.  Upon return from this funciton it will
     * no longer be used
     */
    GASNETC_MSG_SETFLAGS(msg,0,gasnetc_Short,0,numargs);
    msg->handlerId = handler;
    msg->sourceId = gasnetc_mynode;
    msg->destLoc = (uintptr_t)NULL;
    msg->dataLen = 0;
    /* do NOT modify the contents of uhdrLoc... needed at origin */
    va_start(argptr, numargs); /*  pass in last argument */
    for (i = 0; i < numargs; i++) {
	msg->args[i] = va_arg(argptr, gasnet_handlerarg_t);
    }
    va_end(argptr);

#if GASNETC_ENABLE_LOOPBACK
    if (requester == gasnetc_mynode) {
	gasnetc_handler_fn_t pfn = gasnetc_handler[handler];
	RUN_HANDLER_SHORT(pfn,token,&msg->args[0],numargs);
	GASNETI_RETURN(GASNET_OK);
    }
#endif

    /* only send as many much of token structure as necessary */
    token_len = TOKEN_LEN(numargs);
    
    /* issue the request for remote execution of the user handler */
    assert( token_len <= gasnetc_max_lapi_uhdr_size);
    GASNETC_LCHECK(LAPI_Setcntr(gasnetc_lapi_context,&o_cntr,0));
    GASNETC_LCHECK(LAPI_Amsend(gasnetc_lapi_context, requester,
			       gasnetc_remote_reply_hh[requester],
			       (void*)token, token_len, NULL, 0,
			       NULL, &o_cntr, NULL));
    
    /* wait for the Amsend call to complete locally */
    GASNETC_LCHECK(LAPI_Waitcntr(gasnetc_lapi_context,&o_cntr,1,&cur_cntr));

    retval = GASNET_OK;
    GASNETI_RETURN(retval);
}

extern int gasnetc_AMReplyMediumM( 
    gasnet_token_t token,       /* token provided on handler entry */
    gasnet_handler_t handler, /* index into destination endpoint's handler table */ 
    void *source_addr, size_t nbytes,   /* data payload */
    int numargs, ...) {

    int retval;
    gasnetc_msg_t *msg = &((gasnetc_token_t *)token)->buf.msg;
    uint requester = (uint)msg->sourceId;
    lapi_cntr_t o_cntr;
    int token_len, i, cur_cntr;
    void *udata_start = NULL;
    int udata_avail;
    int udata_packed = 0;
    
    va_list argptr;
    GASNETI_TRACE_AMREPLYMEDIUM(token,handler,source_addr,nbytes,numargs);

    /* we can re-use the token passed into us.  It was allocated in the
     * LAPI header handler to be large enough to contain the maximum
     * number of arguments.  Upon return from this funciton it will
     * no longer be used
     */
    GASNETC_MSG_SETFLAGS(msg,0,gasnetc_Medium,0,numargs);
    msg->handlerId = handler;
    msg->sourceId = gasnetc_mynode;
    msg->destLoc = (uintptr_t)NULL;
    msg->dataLen = nbytes;
    /* do NOT modify the contents of uhdrLoc... needed at origin */
    va_start(argptr, numargs); /*  pass in last argument */
    for (i = 0; i < numargs; i++) {
	msg->args[i] = va_arg(argptr, gasnet_handlerarg_t);
    }
    va_end(argptr);
    udata_start = (void*)&msg->args[numargs];
    token_len = TOKEN_LEN(numargs);
    udata_avail = sizeof(gasnetc_token_t) - token_len;

    /* can we pack the data into the uhdr? */
    if (nbytes <= udata_avail) {
	memcpy(udata_start,source_addr,nbytes);
	token_len += nbytes;
	udata_packed = 1;
	GASNETC_MSG_SET_PACKED(msg);
    }

#if GASNETC_ENABLE_LOOPBACK
    if (requester == gasnetc_mynode) {
	gasnetc_handler_fn_t pfn = gasnetc_handler[handler];
	void *destloc;
	if (nbytes > udata_avail) {
	    destloc = gasneti_malloc_inhandler(nbytes > 0 ? nbytes : 1);
	    memcpy(destloc,source_addr,nbytes);
	} else {
	    destloc = udata_start;
	}
	RUN_HANDLER_MEDIUM(pfn,token,&msg->args[0],numargs,destloc,nbytes);
	if (nbytes > udata_avail) {
	    gasneti_free(destloc);
	}
	GASNETI_RETURN(GASNET_OK);
    }
#endif

    /* issue the request for remote execution of the user handler */
    assert( token_len <= gasnetc_max_lapi_uhdr_size);
    GASNETC_LCHECK(LAPI_Setcntr(gasnetc_lapi_context,&o_cntr,0));
    GASNETC_LCHECK(LAPI_Amsend(gasnetc_lapi_context, requester,
			       gasnetc_remote_reply_hh[requester],
			       (void*)token, token_len,
			       (udata_packed ? NULL : source_addr),
			       (udata_packed ? 0    : nbytes),
			       NULL, &o_cntr, NULL));
    
    /* wait for the Amsend call to complete locally */
    GASNETC_LCHECK(LAPI_Waitcntr(gasnetc_lapi_context,&o_cntr,1,&cur_cntr));

    retval = GASNET_OK;
    GASNETI_RETURN(retval);
}

extern int gasnetc_AMReplyLongM( 
    gasnet_token_t token,       /* token provided on handler entry */
    gasnet_handler_t handler, /* index into destination endpoint's handler table */ 
    void *source_addr, size_t nbytes,   /* data payload */
    void *dest_addr,                    /* data destination on destination node */
    int numargs, ...) {
    int retval;

    gasnetc_msg_t *msg = &((gasnetc_token_t *)token)->buf.msg;
    gasnet_node_t dest;
    lapi_cntr_t o_cntr;
    int token_len, i, cur_cntr;
    void *udata_start = NULL;
    int udata_avail;
    int udata_packed = 0;
    va_list argptr;
  
    retval = gasnet_AMGetMsgSource(token, &dest);
    if (retval != GASNET_OK) GASNETI_RETURN(retval);
    gasnetc_boundscheck(dest, dest_addr, nbytes);
    if_pf (dest >= gasnetc_nodes) GASNETI_RETURN_ERRR(BAD_ARG,"node index too high");
    if_pf (((uintptr_t)dest_addr) < ((uintptr_t)gasnetc_seginfo[dest].addr) ||
	   ((uintptr_t)dest_addr) + nbytes > 
           ((uintptr_t)gasnetc_seginfo[dest].addr) + gasnetc_seginfo[dest].size) 
	GASNETI_RETURN_ERRR(BAD_ARG,"destination address out of segment range");

    GASNETI_TRACE_AMREPLYLONG(token,handler,source_addr,nbytes,dest_addr,numargs);

    /* re-use the token passed to us */
    GASNETC_MSG_SETFLAGS(msg,0,gasnetc_Long,0,numargs);
    msg->handlerId = handler;
    msg->sourceId = gasnetc_mynode;
    msg->destLoc = (uintptr_t)dest_addr;
    msg->dataLen = nbytes;
    /* do NOT modify the contents of uhdrLoc... needed at origin */
    va_start(argptr, numargs); /*  pass in last argument */
    for (i = 0; i < numargs; i++) {
	msg->args[i] = va_arg(argptr, gasnet_handlerarg_t);
    }
    va_end(argptr);
    udata_start = (void*)&msg->args[numargs];
    token_len = TOKEN_LEN(numargs);
    udata_avail = sizeof(gasnetc_token_t) - token_len;


#if GASNETC_ENABLE_LOOPBACK
    if (dest == gasnetc_mynode) {
	gasnetc_handler_fn_t pfn = gasnetc_handler[handler];
	/* copy from source to dest, then execute handler */
	memcpy((char*)dest_addr,source_addr,nbytes);
	RUN_HANDLER_LONG(pfn,token,&msg->args[0],numargs,dest_addr,nbytes);
	GASNETI_RETURN(GASNET_OK);
    }
#endif

    /* can we pack the data into the uhdr? */
    if (nbytes <= udata_avail) {
	memcpy(udata_start,source_addr,nbytes);
	token_len += nbytes;
	udata_packed = 1;
	GASNETC_MSG_SET_PACKED(msg);
    }

    /* issue the request for remote execution of the user handler */
    assert( token_len <= gasnetc_max_lapi_uhdr_size);
    GASNETC_LCHECK(LAPI_Setcntr(gasnetc_lapi_context,&o_cntr,0));
    GASNETC_LCHECK(LAPI_Amsend(gasnetc_lapi_context, dest,
			       gasnetc_remote_reply_hh[dest],
			       (void*)token, token_len,
			       (udata_packed ? NULL : source_addr),
			       (udata_packed ? 0    : nbytes),
			       NULL, &o_cntr, NULL));
    
    /* wait for the Amsend call to complete locally */
    GASNETC_LCHECK(LAPI_Waitcntr(gasnetc_lapi_context,&o_cntr,1,&cur_cntr));

    retval = GASNET_OK;

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

/* ============================================================================
 * LAPI Clarification:
 * These calls should not have to be used or defined.  Even when LAPI is executing
 * in "interrupt" mode, the resulting handler is not executed in a traditional
 * signal handler.  A seperate thread, called the notification thread created at
 * LAPI init time, executes the LAPI dispatcher to make progress on a new
 * incoming packet and other outstanding communications.  No client thread
 * will be interrupted to execute the handler.
 * In LAPI POLLING mode, all progress is made when the client explicitly
 * calls LAPI functions via GASNET calls.  Again, no interrupts will occur.
 * ============================================================================
 */
#if GASNETC_USE_IBH
extern void gasnetc_hold_interrupts() {
    GASNETC_CHECKATTACH();

    /* Check to see of interrupts are already being held */
    {
	int id = pthread_self();
	assert(id >= 0 && id < GASNETC_MAX_THREAD);
	if (gasnetc_interrupt_held[id]) {
	    gasneti_fatalerror("gasnetc_hold_interrupts: already held in thread %d",id);
	}
	gasnetc_interrupt_held[id] = 1;
    }

    /* (###) add code here to disable handler interrupts for _this_ thread */
}
extern void gasnetc_resume_interrupts() {
    GASNETC_CHECKATTACH();

    /* Check to insure that interrupts are being held */
    {
	int id = pthread_self();
	assert(id >= 0 && id < GASNETC_MAX_THREAD);
	if (gasnetc_interrupt_held[id] != 0) {
	    gasneti_fatalerror("gasnetc_resume_interrupts: Not held in thread %d",id);
	}
	gasnetc_interrupt_held[id] = 0;
    }

    /* (###) add code here to re-enable handler interrupts for _this_ thread */
}
#endif

/* ------------------------------------------------------------------------------------ */
/*
 * Handler-safe locks
 * ==================
 *
 * In the LAPI GASNET implementation, handlers are executed in exactly
 * two contexts:
 *
 * (1) In the LAPI completion handler thread.  Here, only one
 *     completion handler is executed at a time.
 * (2) By the client thread issuing an AM request call
 *     in which the target node is the same as the origin node
 *     and only if the 'GASNETC_ENABLE_LOOPBACK' macro is
 *     set to 1.
 *
 * In both of these cases, posix mutexes are sufficient for HSLs.
 * NOTE: removed the GASNETI_THREADS ifdefs because we need the
 *       mutex code even in the GASNET_SEQ case.  A handler
 *       can be executing in the LAPI completion handler thread
 *       and modifying data being used by the client thread.
 *
 * Note that we SHOULD add error checking....
*/

extern void gasnetc_hsl_init   (gasnet_hsl_t *hsl) {
    GASNETC_CHECKATTACH();

    {	int retval = gasnetc_spinlock_init(&(hsl->lock));
    if (retval) 
	gasneti_fatalerror("In gasnetc_hsl_init(), pthread_mutex_init()=%s",strerror(retval));
    }

    /* (###) add code here to init conduit-specific HSL state */
}

extern void gasnetc_hsl_destroy(gasnet_hsl_t *hsl) {
    GASNETC_CHECKATTACH();

    {	int retval = gasnetc_spinlock_destroy(&(hsl->lock));
    if (retval) 
	gasneti_fatalerror("In gasnetc_hsl_destroy(), pthread_mutex_destroy()=%s",strerror(retval));
    }

    /* (###) add code here to cleanup conduit-specific HSL state */
}

extern void gasnetc_hsl_lock   (gasnet_hsl_t *hsl) {
    GASNETC_CHECKATTACH();

    { int retval; 
#if defined(STATS) || defined(TRACE)
    gasneti_stattime_t startlock = GASNETI_STATTIME_NOW_IFENABLED(L);
#endif
    
    retval = gasnetc_spinlock_lock(&(hsl->lock));
    if (retval) 
	gasneti_fatalerror("In gasnetc_hsl_lock(), pthread_mutex_lock()=%s",strerror(retval));
#if defined(STATS) || defined(TRACE)
    hsl->acquiretime = GASNETI_STATTIME_NOW_IFENABLED(L);
    GASNETI_TRACE_EVENT_TIME(L, HSL_LOCK, hsl->acquiretime-startlock);
#endif
    }

    /* (###) conduits with interrupt-based handler dispatch need to add code here to 
       disable handler interrupts on _this_ thread, (if this is the outermost
       HSL lock acquire and we're not inside an enclosing no-interrupt section)
    */
}

extern void gasnetc_hsl_unlock (gasnet_hsl_t *hsl) {
    GASNETC_CHECKATTACH();

    /* (###) conduits with interrupt-based handler dispatch need to add code here to 
       re-enable handler interrupts on _this_ thread, (if this is the outermost
       HSL lock release and we're not inside an enclosing no-interrupt section)
    */

    GASNETI_TRACE_EVENT_TIME(L, HSL_UNLOCK, GASNETI_STATTIME_NOW()-hsl->acquiretime);

    {	int retval = gasnetc_spinlock_unlock(&(hsl->lock));
    if (retval) 
	gasneti_fatalerror("In gasnetc_hsl_unlock(), pthread_mutex_unlock()=%s",strerror(retval));
    }
}

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

/* ==========================================================================
 * MLW: Functions that could be in gasnet_core_internal.c
 * ==========================================================================
 */

/* --------------------------------------------------------------------------
 * This function will be involked in the event of an async error
 * from LAPI.
 * --------------------------------------------------------------------------
 */
static char* err_type_str[] = {
    "GET_ERR",
    "PUT_ERR",
    "RMW_ERR",
    "AM_ERR",
    "INTERRUPT_ERR"
};
void gasnetc_lapi_err_handler(lapi_handle_t *context, int *error_code,
			      lapi_err_t  *error_type, int *taskid, int *src)
{
    char msg[LAPI_MAX_ERR_STRING];

    LAPI_Msg_string(*error_code,msg);
    gasneti_fatalerror("Async LAPI Error on node %d from task %d of type %s code %d [%s]\n",
		       *src,*taskid,err_type_str[*error_type],*error_code,msg);
}

/* --------------------------------------------------------------------------
 * This function used to as a form of AllGather operation.  It is
 * collective across all the gasnet nodes.
 * Src is a pointer to a data structure of size len.
 * Dest is an array of the same kind of data structures, one element
 * for each gasnet node.
 * 
 * Each node puts a copy of its src into the dest[gasnetc_mynode]
 * on every other node.
 * --------------------------------------------------------------------------
 */
void gasnetc_lapi_exchange(void *src, size_t len, void *dest)
{
    void **dest_addr_tab;
    lapi_cntr_t c_cntr;
    int  node;
    int  num_nodes = (int)gasnetc_nodes;
    int  cur_val;

    /* First, need to determine address of dest on each node,
     * Note that this is a collective operation.
     */
    dest_addr_tab = gasneti_malloc_inhandler(num_nodes*sizeof(void*));
    GASNETC_LCHECK(LAPI_Address_init(gasnetc_lapi_context,dest,dest_addr_tab));
    
    /* Now, put my src value into all remote dest arrays */
    GASNETC_LCHECK(LAPI_Setcntr(gasnetc_lapi_context,&c_cntr,0));
    for (node = 0; node < num_nodes; node++) {
	void *ra = (void*)((char*)(dest_addr_tab[node]) + gasnetc_mynode*len);
	GASNETC_LCHECK(LAPI_Put(gasnetc_lapi_context,node,len,ra,
				src,NULL,NULL,&c_cntr));
    }

    /* Wait for all puts to complete locally and at targets */
    GASNETC_LCHECK(LAPI_Waitcntr(gasnetc_lapi_context,&c_cntr,num_nodes,&cur_val));
    assert(cur_val == 0);

    /* Must barrier to insure all nodes have completed
     * puts to my dest array */
    GASNETC_LCHECK(LAPI_Gfence(gasnetc_lapi_context));

    /* free up array used for remote address locations */
    gasneti_free(dest_addr_tab);
}

void gasnetc_token_queue_init(gasnetc_token_queue_t *q)
{
    q->head = q->tail = NULL;
    gasnetc_spinlock_init(&(q->lock));
    q->schedule = 1;
}

gasnetc_token_t* gasnetc_token_dequeue(gasnetc_token_queue_t *q, int update_schedule)
{
    gasnetc_token_t *p;

    /* spin until queue is available */
    gasnetc_spinlock_lock(&(q->lock));

    /* remove from head */
    p = q->head;

    if (p == NULL) {
	/* the queue was empty, check empty queue condition */
	assert(q->tail == NULL);
	/* called from completion handler which just empied the queue,
	 * tell the header handler to schedule completion handler again
	 * on next enqueue
	 */
	if (update_schedule) {
	    q->schedule = 1;
	}
    } else {
	q->head = p->next;
	p->next = NULL;
	if (p == q->tail) {
	    /* was the last element */
	    q->tail = NULL;
	    assert(q->head == NULL);
	}
    }
    /* unlock queue */
    gasnetc_spinlock_unlock(&(q->lock));

    return p;
}

void gasnetc_token_enqueue(gasnetc_token_queue_t *q, gasnetc_token_t *p, int *schedule)
{
    /* gain the queue lock */
    gasnetc_spinlock_lock(&(q->lock));

    /* add to tail of queue */
    p->next = NULL;
    if (q->tail == NULL) {
	/* was empty */
	assert(q->head == NULL);
	q->head = q->tail = p;
    } else {
	/* attach to current tail */
	q->tail->next = p;
	q->tail = p;
    }
    
    *schedule = q->schedule;
    q->schedule = 0;
    
    /* unlock queue */
    gasnetc_spinlock_unlock(&(q->lock));
}

void* gasnetc_lapi_AMreply_hh(lapi_handle_t *context, void *uhdr, uint *uhdr_len,
			      ulong *msg_len, compl_hndlr_t **comp_h, void **uinfo)
{
    gasnetc_token_t *token = (gasnetc_token_t*)uhdr;
    gasnetc_msg_t   *msg = &token->buf.msg;
    gasnetc_category_t cat;
    unsigned int is_packed;
    void* destloc = NULL;
    gasnetc_token_t *new_token;
    unsigned int numargs;
    int token_len = *uhdr_len;
    gasnet_handler_t func_ix = msg->handlerId;
    gasnetc_handler_fn_t am_func = gasnetc_handler[func_ix];
    gasnet_handlerarg_t *am_args = &msg->args[0];
    int done = 0;

    /* All AM request handlers MUST run in the LAPI completion handler
     * since they will issue an AM reply
     */

    is_packed = GASNETC_MSG_ISPACKED(msg);
    numargs = GASNETC_MSG_NUMARGS(msg);
    cat = GASNETC_MSG_CATEGORY(msg);

#if 0
    { /* For dubugging */
	unsigned int is_req;
	is_req = GASNETC_MSG_ISREQUEST(msg);
	assert(!is_req);
    
	GASNETI_TRACE_PRINTF(C,("Reply_HH received %s from %d is_req %d is_packed %d numargs %d uhdr_len %d msg_len %d destLoc %x dataLen %d",
				gasnetc_catname[cat],msg->sourceId,is_req,
				is_packed,numargs,*uhdr_len,*msg_len,
				(void*)msg->destLoc,msg->dataLen));
    }
#endif
    
    /* This is a reply. If the uhdrLoc field of the token is set
     * that means the origional GASNET call was an AsyncLong and we
     * should deallocate the origional uhdr memory.
     */
    if (msg->uhdrLoc != (uintptr_t)NULL) {
	gasnetc_token_t* loc = (gasnetc_token_t*)(msg->uhdrLoc);
	gasnetc_uhdr_free(loc);
	msg->uhdrLoc = (uintptr_t)NULL;
    }

    switch (cat) {
    case gasnetc_Short:
	/* can run the AM handler in-line */
	RUN_HANDLER_SHORT(am_func,token,am_args,numargs);
	done = 1;
	break;
    case gasnetc_Medium:
	if (is_packed) {
	    /* can run the AM handler in-line, data payload is packed in uhdr */
	    void *srcloc = (void*)&msg->args[numargs];
	    RUN_HANDLER_MEDIUM(am_func,token,am_args,numargs,srcloc,msg->dataLen);
	    done = 1;
	} else {
	    /* data payload not in uhdr, alloc space of it */
	    assert( msg->dataLen == *msg_len);
	    destloc = gasneti_malloc_inhandler( *msg_len > 0 ? *msg_len : 1 );
	    msg->destLoc = (uintptr_t)destloc;
	}
	break;
    case gasnetc_AsyncLong:
    case gasnetc_Long:
	destloc = (void*)msg->destLoc;
	if (is_packed) {
	    /* copy packed data to destination and report to LAPI
	     * dispatcher there is no more data coming
	     */
	    void* udata_start = (void*)&msg->args[numargs];
	    memcpy(destloc,udata_start,msg->dataLen);
	    RUN_HANDLER_LONG(am_func,token,am_args,numargs,destloc,msg->dataLen);
	    done = 1;
	}
	break;
    default:
	gasneti_fatalerror("Reply_HH: invalid message Category %d",(int)cat);
    }

    if (done) {
	*comp_h = NULL;
	*uinfo = NULL;
	destloc = NULL;
    } else {
	/* Must copy uhdr to allocated uhdr buffer */
	new_token = gasnetc_uhdr_alloc();
	memcpy((void*)new_token,uhdr,*uhdr_len);
	*uinfo = (void*)new_token;
	*comp_h = gasnetc_lapi_AMch;
    }

    return destloc;
}

void* gasnetc_lapi_AMreq_hh(lapi_handle_t *context, void *uhdr, uint *uhdr_len,
			    ulong *msg_len, compl_hndlr_t **comp_h, void **uinfo)
{
    gasnetc_token_t *token = (gasnetc_token_t*)uhdr;
    gasnetc_msg_t *msg = &token->buf.msg;
    gasnetc_category_t cat;
    unsigned int is_packed;
    void* destloc = NULL;
    void *udata_start = NULL;
    unsigned int numargs;
    int token_len = *uhdr_len;
    int is_ready = 0;
    unsigned int is_req;
    gasnetc_token_t *new_token;

    is_req = GASNETC_MSG_ISREQUEST(msg);
    assert(is_req);

    is_packed = GASNETC_MSG_ISPACKED(msg);
    numargs = GASNETC_MSG_NUMARGS(msg);
    cat = GASNETC_MSG_CATEGORY(msg);

#if 0
    { /* For dubugging */
    
	GASNETI_TRACE_PRINTF(C,("Req_HH received %s from %d is_req %d is_packed %d numargs %d uhdr_len %d msg_len %d destLoc %x dataLen %d",
				gasnetc_catname[cat],token->sourceId,is_req,
				is_packed,numargs,*uhdr_len,*msg_len,
				(void*)msg->destLoc,msg->dataLen));
    }
#endif
    
    switch (cat) {
    case gasnetc_Short:
	is_ready = 1;
	break;
    case gasnetc_Medium:
	if (is_packed) {
	    /* will use token space as local buffer */
	    assert( *msg_len == 0 );
	    msg->destLoc = (uintptr_t)NULL;
	    is_ready = 1;
	} else {
	    assert( msg->dataLen == *msg_len);
	    destloc = gasneti_malloc_inhandler( *msg_len > 0 ? *msg_len : 1 );
	    msg->destLoc = (uintptr_t)destloc;
	}
	break;
    case gasnetc_AsyncLong:
    case gasnetc_Long:
	destloc = (void*)msg->destLoc;
	if (is_packed) {
	    /* copy packed data to destination and report to LAPI
	     * dispatcher there is no more data coming
	     */
	    udata_start = (void*)&msg->args[numargs];
	    memcpy(destloc,udata_start,msg->dataLen);
	    token_len -= msg->dataLen;
	    is_ready = 1;
	}
	break;
    default:
	gasneti_fatalerror("Req_HH: invalid message Category %d",(int)cat);
    }

    /* alloc and copy only the necessary portion of the incoming token */
    new_token = gasnetc_uhdr_alloc();
    memcpy((void*)new_token,uhdr,token_len);

    /* By default, schedule the completion handler.  May null this later */
    *comp_h = gasnetc_lapi_AMch;

    if (is_ready) {
	int schedule;
	/* no more data arriving */
	destloc = NULL;
	/* signal to completion handler only to check queue */
	*uinfo = NULL;
	gasnetc_token_enqueue(&gasnetc_req_q,new_token,&schedule);
	/* only schedule completion handler if it has run since
	 * last enqueue */
	if (! schedule) {
	    *comp_h = NULL;
	}
    } else {
	/* waiting for more data to arrive */
	*uinfo = (void*)new_token;
    }

    return destloc;
}

void gasnetc_lapi_AMch(lapi_handle_t *context, void *uinfo)
{
    gasnetc_token_t *token = (gasnetc_token_t*)uinfo;
    gasnetc_token_t *q_token = NULL;
    int do_schedule = (token == NULL ? 1 : 0);


    /* first, process all items on the request queue to keep
     * latencies to a minimum
     */
    while ( (q_token = gasnetc_token_dequeue(&gasnetc_req_q, do_schedule)) != NULL ) {
	gasnetc_run_handler(q_token);
	/* deallocate the token, it was allocated in the header handler */
	gasnetc_uhdr_free(q_token);
    }

    if (token != NULL) {
	/* Our request was not put on the queue either because
	 * not all data was available or the queue was full.
	 * process it now.
	 */
	gasnetc_run_handler(token);
	/* deallocate the token, it was allocated in the header handler */
	gasnetc_uhdr_free(token);

	/* Check the request queue again */
	while ( (q_token = gasnetc_token_dequeue(&gasnetc_req_q, 1)) != NULL ) {
	    gasnetc_run_handler(q_token);
	    /* deallocate the token, it was allocated in the header handler */
	    gasnetc_uhdr_free(q_token);
	}
    }


}

void gasnetc_run_handler(gasnetc_token_t *token)
{
    gasnetc_msg_t *msg = &token->buf.msg;
    
    /* extract the token and paramater from the uinfo structure */
    gasnetc_category_t msg_type = GASNETC_MSG_CATEGORY(msg);
    unsigned int numargs = GASNETC_MSG_NUMARGS(msg);
    unsigned int is_request = GASNETC_MSG_ISREQUEST(msg);
    unsigned int is_packed = GASNETC_MSG_ISPACKED(msg);
    void *dataptr = (void*)(msg->destLoc);
    size_t datalen = msg->dataLen;
    gasnet_handler_t func_ix = msg->handlerId;
    gasnetc_handler_fn_t am_func = gasnetc_handler[func_ix];
    gasnet_handlerarg_t *am_args = &msg->args[0];

    assert(numargs <= GASNETC_AM_MAX_ARGS);

#if 0
    GASNETI_TRACE_PRINTF(C,("CH received %s from %d is_req %d is_packed %d numargs %d handlerix %d dataptr %x datalen %d",
			    gasnetc_catname[msg_type],msg->sourceId,is_request,
			    is_packed,numargs,func_ix,dataptr,datalen));
#endif

    if (am_func == NULL) {
	gasneti_fatalerror("lapi_AMch: node %d, invalid handler index %d",
			   gasnetc_mynode,func_ix);
    }

    /* run the GASNET handler */
    switch (msg_type) {
    case gasnetc_Short:
	RUN_HANDLER_SHORT(am_func,token,am_args,numargs);
	break;
	
    case gasnetc_Medium:
	if (is_packed) {
	    /* data is cached in this uhdr */
	    dataptr = (void*)&msg->args[numargs];
	}
	RUN_HANDLER_MEDIUM(am_func,token,am_args,numargs,dataptr,datalen);
	/* need to free this data memory (allocated in header handler) */
	if (! is_packed) {
	    /* we allocated a buffer for the payload in the header handler */
	    gasneti_free(dataptr);
	}
	break;

    case gasnetc_Long:
    case gasnetc_AsyncLong:
	RUN_HANDLER_LONG(am_func,token,am_args,numargs,dataptr,datalen);
	/* Note that the memory specified by dataptr and datalen must
	 * be in the segment registered on this node.
	 */
	break;
	
    }

}

/* --------------------------------------------------------------------------
 * gasnetc_uhdr_init:
 *
 * Init the LAPI uhdr free list.  Alloc and free called from within
 * LAPI header handler and completion handler.  Must not block
 * indefinately.
 * --------------------------------------------------------------------------
 */
void gasnetc_uhdr_init(int want)
{
    int got;
    gasnetc_spinlock_init(&(gasnetc_uhdr_freelist.lock));
    gasnetc_spinlock_lock(&(gasnetc_uhdr_freelist.lock));
    
    /* init the structure values */
    gasnetc_uhdr_freelist.high_water_mark = 0;
    gasnetc_uhdr_freelist.numfree = 0;
    gasnetc_uhdr_freelist.numalloc = 0;
    gasnetc_uhdr_freelist.head = NULL;
    
    got = gasnetc_uhdr_more(want);

    gasnetc_spinlock_unlock(&(gasnetc_uhdr_freelist.lock));

    if (got == 0) {
	gasneti_fatalerror("Unable to alloc ANY uhdr buffers");
    }
}

/* --------------------------------------------------------------------------
 * gasnetc_uhdr_alloc:
 *
 * return an element from the free list.  If freelist is empty,
 * attempt to alloc more.  If that fails, abort.
 * --------------------------------------------------------------------------
 */
gasnetc_token_t* gasnetc_uhdr_alloc(void)
{
    gasnetc_token_t *p = NULL;

    gasnetc_spinlock_lock(&(gasnetc_uhdr_freelist.lock));

    if (gasnetc_uhdr_freelist.numfree == 0) {
	gasnetc_uhdr_more(GASNETC_UHDR_ADDITIONAL);
	if (gasnetc_uhdr_freelist.numfree == 0) {
	    gasnetc_spinlock_unlock(&(gasnetc_uhdr_freelist.lock));
	    gasneti_fatalerror("Unable to alloc additional uhdr buffers");
	}
    }

    assert(gasnetc_uhdr_freelist.head != NULL);
    p = gasnetc_uhdr_freelist.head;
    gasnetc_uhdr_freelist.head = p->next;
    gasnetc_uhdr_freelist.numfree--;
    gasnetc_uhdr_freelist.numalloc++;
    p->next = NULL;
    if (gasnetc_uhdr_freelist.numalloc > gasnetc_uhdr_freelist.high_water_mark)
	gasnetc_uhdr_freelist.high_water_mark = gasnetc_uhdr_freelist.numalloc;

    gasnetc_spinlock_unlock(&(gasnetc_uhdr_freelist.lock));

    return p;
}

/* --------------------------------------------------------------------------
 * gasnetc_uhdr_free:
 *
 * put this uhdr back onto the free list.
 * --------------------------------------------------------------------------
 */
void gasnetc_uhdr_free(gasnetc_token_t *p)
{
    gasnetc_spinlock_lock(&(gasnetc_uhdr_freelist.lock));
    p->next = gasnetc_uhdr_freelist.head;
    gasnetc_uhdr_freelist.head = p;
    gasnetc_uhdr_freelist.numfree++;
    gasnetc_uhdr_freelist.numalloc--;
    gasnetc_spinlock_unlock(&(gasnetc_uhdr_freelist.lock));
}

/* --------------------------------------------------------------------------
 * gasnetc_uhdr_more
 *
 * This is the real allocation function.  It attempts to allocate
 * the requested number of buffers, but decreases the size by a
 * factor of two each time it fails.
 * The allocated buffers are added to the freelist.
 * The return value is the actual number of buffers allocated.
 * --------------------------------------------------------------------------
 */
int gasnetc_uhdr_more(int want)
{
    /* NOTE: assumes lock already held */

    int i;
#if 0
    gasnetc_token_t *free = (gasnetc_token_t*)gasneti_malloc_inhandler(want * sizeof(gasnetc_token_t));
#else
    gasnetc_token_t *free = (gasnetc_token_t*)malloc(want * sizeof(gasnetc_token_t));
#endif
    assert(gasnetc_uhdr_freelist.head == NULL);
    while (free == NULL) {
	if (want <= 1) {
	    return 0;
	} else {
	    want /= 2;
	}
#if 0
	free = (gasnetc_token_t*)gasneti_malloc_inhandler(want * sizeof(gasnetc_token_t));
#else
	free = (gasnetc_token_t*)malloc(want * sizeof(gasnetc_token_t));
#endif
    }

    /* link them onto freelist */
    for (i = 0; i < want; i++) {
	free->next = gasnetc_uhdr_freelist.head;
	gasnetc_uhdr_freelist.head = free;
	free++;
    }
    gasnetc_uhdr_freelist.numfree += want;

    GASNETI_TRACE_PRINTF(C,("Allocated %d more UHDR BUFFERS: numalloc %d numfree %d",
			    want,gasnetc_uhdr_freelist.numalloc,gasnetc_uhdr_freelist.numfree));
    return want;
}

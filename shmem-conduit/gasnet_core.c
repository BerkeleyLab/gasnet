/*  $Archive:: /Ti/GASNet/shmem-conduit/gasnet_core.c                  $
 *     $Date: 2004/03/11 11:19:13 $
 * $Revision: 1.2 $
 * Description: GASNet shmem conduit Implementation
 * Copyright 2002, Dan Bonachea <bonachea@cs.berkeley.edu>
 * Terms of use are as specified in license.txt
 */

#include <gasnet.h>
#include <gasnet_internal.h>
#include <gasnet_handler.h>
#include <gasnet_core_internal.h>

#include <errno.h>
#include <unistd.h>
#include <signal.h>
#include <sched.h>

GASNETI_IDENT(gasnetc_IdentString_Version, "$GASNetCoreLibraryVersion: " GASNET_CORE_VERSION_STR " $");
GASNETI_IDENT(gasnetc_IdentString_ConduitName, "$GASNetConduitName: " GASNET_CORE_NAME_STR " $");

gasnet_handlerentry_t const *gasnetc_get_handlertable();
static void gasnetc_atexit(void);

gasnet_node_t gasnetc_mynode = (gasnet_node_t)-1;
gasnet_node_t gasnetc_nodes = 0;

static gasnet_seginfo_t gasnetc_SHMallocSegmentSearch();

#define GASNETC_MAX_NUMHANDLERS   256
typedef void (*gasnetc_handler_fn_t)();  /* prototype for handler function */
gasnetc_handler_fn_t gasnetc_handler[GASNETC_MAX_NUMHANDLERS]; /* handler table */

uintptr_t gasnetc_MaxLocalSegmentSize = 0;
uintptr_t gasnetc_MaxGlobalSegmentSize = 0;

intptr_t		*gasnetc_segment_shptr_off;
gasnet_seginfo_t	 gasnetc_seginfo_init;
int			 gasnetc_seginfo_allocated = 0;
gasnet_seginfo_t	*gasnetc_seginfo = NULL;
gasnet_seginfo_t	*gasnetc_seginfo_shmem = NULL;
size_t			 gasnetc_pagesize;

int  gasnetc_amq_idx = 0;
int  gasnetc_amq_depth = 64;	/* max is GASNETC_AMQUEUE_MAX_DEPTH */
int  gasnetc_amq_mask;

gasnetc_am_packet_t  gasnetc_amq_reqs[GASNETC_AMQUEUE_MAX_DEPTH];

#ifdef CRAY_SHMEM
  volatile long	gasnetc_amq_reqfields[GASNETC_AMQUEUE_MAX_FIELDS];
  long		gasnetc_amq_numfields;
  extern uintptr_t gasnete_pe_bits_shift;
  extern uintptr_t gasnete_addr_bits_mask;
#endif

/* ------------------------------------------------------------------------------------ */
/*
  Initialization
  ==============
*/
/* called at startup to check configuration sanity */
static void gasnetc_check_config() {
  /* TODO ?? */
  /* add code to do some sanity checks on the number of nodes, handlers
   * and/or segment sizes */ 
}

static void gasnetc_bootstrapBarrier() {
	shmem_barrier_all();
}


static int gasnetc_init(int *argc, char ***argv) {
  char *qdepth;
  /*  check system sanity */
  gasnetc_check_config();

  if (gasneti_init_done) 
    GASNETI_RETURN_ERRR(NOT_INIT, "GASNet already initialized");

  if (getenv("GASNET_FREEZE")) gasneti_freezeForDebugger();

  qdepth = getenv("GASNET_SHMEM_QDEPTH");
  if (qdepth && *qdepth != '\0') {
	int qdepth_i = atoi(qdepth);

	if (!GASNETC_AMQUEUE_SIZE_VALID(qdepth_i))
	    GASNETI_RETURN_ERRR(BAD_ARG, "Invalid QDepth parameter");

	gasnetc_amq_depth = qdepth_i;
  }

  gasnetc_amq_mask = (gasnetc_amq_depth-1);

  #if CRAY_SHMEM
    gasnetc_amq_numfields = (gasnetc_amq_depth+63)/64;
  #endif

  #if GASNET_DEBUG_VERBOSE
    /* note - can't call trace macros during gasnet_init because trace system not yet initialized */
    fprintf(stderr,"gasnetc_init(): about to spawn...\n"); fflush(stderr);
  #endif

  #if defined(CRAY_SHMEM) || defined(SGI_SHMEM)
    start_pes(0);
  #elif defined(QUADRICS_SHMEM)
    shmem_init();
  #endif

  gasnetc_mynode = shmem_my_pe();
  gasnetc_nodes = shmem_n_pes();

  #if GASNET_DEBUG_VERBOSE
    fprintf(stderr,"gasnetc_init(): spawn successful - node %i/%i starting...\n", 
      gasnetc_mynode, gasnetc_nodes); fflush(stderr);
  #endif

  #if GASNET_SEGMENT_FAST || GASNET_SEGMENT_LARGE
    { 
	#if defined(CRAY_SHMEM) || defined(SGI_SHMEM)

	    /* XXX Currently SHMallocSegmentSearch takes about 1 second.  We
	     * may want to take another approach if it is too long.  Since Cray
	     * machines do not necessarily ship with much configuration
	     * variance, perhaps there's a static way of determining the amount
	     * of physical memory.
	     */

	    gasnetc_seginfo_init = gasnetc_SHMallocSegmentSearch();

	    /* Since shmalloc() is collective, local == global */
	    gasnetc_MaxLocalSegmentSize = gasnetc_MaxGlobalSegmentSize 
			= gasnetc_seginfo_init.size;
	    printf("seginfo.size = %d\n", gasnetc_seginfo_init.size);

	    gasnetc_seginfo_allocated = 1;

	    /* We keep the allocation live until gasnet_attach(), in which
	     * case we can simply use realloc to reduce its size */

	#elif defined(QUADRICS_SHMEM)
		#error Not implemented yet.  Should merge with code from elan-conduit
	#endif

    }
  #elif GASNET_SEGMENT_EVERYTHING
    gasnetc_MaxLocalSegmentSize =  (uintptr_t)-1;
    gasnetc_MaxGlobalSegmentSize = (uintptr_t)-1;
  #else
    #error Bad segment config
  #endif

  gasneti_init_done = 1;  

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
    /* add code here to register table[i].fnptr 
             on index (gasnet_handler_t)newindex */
    gasnetc_handler[(gasnet_handler_t)newindex] = (gasnetc_handler_fn_t)table[i].fnptr;

    if (dontcare) table[i].index = newindex;
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

  /*  register any custom signal handlers required by your conduit 
   *        (e.g. to support interrupt-based messaging)
   */

  atexit(gasnetc_atexit);

  /* ------------------------------------------------------------------------------------ */
  /*  register segment  */

  gasnetc_seginfo = (gasnet_seginfo_t *)gasneti_malloc(gasnetc_nodes*sizeof(gasnet_seginfo_t));
  memset(gasnetc_seginfo, 0, gasnetc_nodes*sizeof(gasnet_seginfo_t));


  #if GASNET_SEGMENT_FAST || GASNET_SEGMENT_LARGE
    if (segsize == 0) { 
	    segbase = NULL;
	    shfree(gasnetc_seginfo_init.addr);
    }
    else {
      gasneti_assert(((uintptr_t)segbase) % GASNET_PAGESIZE == 0);
      gasneti_assert(segsize % GASNET_PAGESIZE == 0);

      /* With Cray, we use our preallocated segment and optionally truncate it
       * with realloc if the user requests something smaller
       */
	#if defined(CRAY_SHMEM) || defined(SGI_SHMEM)
      	{
	    static long	pSync[_SHMEM_COLLECT_SYNC_SIZE];
	    int		i;
	    intptr_t	*shm_collect;

	    if (segsize < gasnetc_seginfo_init.size) {

		segbase = shrealloc(gasnetc_seginfo_init.addr, segsize);
		if (segbase == NULL) {
			shfree(gasnetc_seginfo_init.addr);
			gasneti_fatalerror("shrealloc() failed on initial GASNet segment");
		}
		gasnetc_seginfo_init.addr = (void *) segbase;
		gasnetc_seginfo_init.size = segsize;
	    }
	    else {
		segbase = gasnetc_seginfo_init.addr;
		segsize = gasnetc_seginfo_init.size;
	    }

	    for (i=0; i < _SHMEM_COLLECT_SYNC_SIZE; i++)
		pSync[i] = _SHMEM_SYNC_VALUE;

	    shmem_barrier_all();

	    gasneti_assert(segsize >= sizeof(uintptr_t) * gasnetc_nodes);

	    shm_collect = (intptr_t *) segbase;

	    shmem_fcollect64
		    ((void *) shm_collect, &segbase, 1, 0, 0, gasnetc_nodes, pSync);
	    gasneti_assert(shm_collect[gasnetc_mynode] == (intptr_t) segbase);

	    gasnetc_segment_shptr_off = (intptr_t *) 
		    gasneti_malloc(sizeof(intptr_t) * gasnetc_nodes);

	    { int i;
		for (i=0;i<gasnetc_nodes;i++) {
		    gasnetc_seginfo[i].addr = (void *) shm_collect[i];
		    gasnetc_seginfo[i].size = segsize;
		    gasnetc_segment_shptr_off[i] = 
			(intptr_t) shm_collect[i] - (intptr_t) segbase;
		    if (gasnetc_mynode == 0) {
			printf("%d> seg %2d: %p,%9d => base = %p, off = %d\n",
			    gasnetc_mynode, i, gasnetc_seginfo[i].addr, 
			    (unsigned int) gasnetc_seginfo[i].size, 
			    (void *) shm_collect[i],
			    (unsigned int) gasnetc_segment_shptr_off[i]);
			   fflush(stdout);
		    }
		}
	    }

	    #ifdef CRAY_SHMEM
	    {	long i;
		uintptr_t pemask;
		long lastnode;

		if (gasnetc_nodes == 1) {
			gasnete_pe_bits_shift = 0;
			gasnete_addr_bits_mask = (uintptr_t) -1;
		}
		else {
		    gasnete_pe_bits_shift  = 
			    63 - _leadz((long) shm_collect[1]);
		    gasnete_addr_bits_mask = 
			    (uintptr_t) (1UL<<gasnete_pe_bits_shift)-1;
		}
	    }
	    #endif

	    memset(shm_collect, 0,  sizeof(uintptr_t) * gasnetc_nodes);
	}

	#else
	    #error Currently only support shmalloc() and shrealloc() Segment allocators
	#endif

      /* add code here to choose and register a segment 
         (ensuring alignment across all nodes if this conduit sets GASNET_ALIGNED_SEGMENTS==1) 
         you can use gasneti_segmentAttach() here if you used gasneti_segmentInit() above
      */
    }
  #else
    /* GASNET_SEGMENT_EVERYTHING */
    segbase = (void *)0;
    segsize = (uintptr_t)-1;
    { int i;
      for (i=0;i<gasnetc_nodes;i++) {
        gasnetc_seginfo[i].addr = (void *)0;
        gasnetc_seginfo[i].size = (uintptr_t)-1;
      }
    }
  #endif

  /* ------------------------------------------------------------------------------------ */
  /*  gather segment information */

  /* add code here to gather the segment assignment info into 
           gasnetc_seginfo on each node (may be possible to use AMShortRequest here)
   */

  /* ------------------------------------------------------------------------------------ */
  /*  primary attach complete */
  gasneti_attach_done = 1;
  gasnetc_bootstrapBarrier();

  GASNETI_TRACE_PRINTF(C,("gasnetc_attach(): primary attach complete"));

  gasneti_assert(gasnetc_seginfo[gasnetc_mynode].addr == segbase &&
         gasnetc_seginfo[gasnetc_mynode].size == segsize);

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

  if (fflush(stdout)) 
    gasneti_fatalerror("failed to flush stdout in gasnetc_exit: %s", strerror(errno));
  if (fflush(stderr)) 
    gasneti_fatalerror("failed to flush stderr in gasnetc_exit: %s", strerror(errno));
  gasneti_trace_finish();
  gasneti_sched_yield();

  /* add code here to terminate the job across _all_ nodes 
           with gasneti_killmyprocess(exitcode) (not regular exit()), preferably
           after raising a SIGQUIT to inform the client of the exit
  */

  #if defined(CRAY_SHMEM) || defined(SGI_SHMEM)
    if (gasnetc_seginfo_allocated)
	shfree(gasnetc_seginfo_init.addr);
  #endif
  #if defined SGI_SHMEM
    exit(exitcode);
    abort();
  #else
    gasneti_killmyprocess(exitcode);
    abort();
  #endif
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
  gasnet_node_t sourceid;
  GASNETI_CHECKATTACH();
  if (!token) GASNETI_RETURN_ERRR(BAD_ARG,"bad token");
  if (!srcindex) GASNETI_RETURN_ERRR(BAD_ARG,"bad src ptr");

  /* add code here to write the source index into sourceid */
  sourceid = (gasnet_node_t) GASNETC_AMHEADER_NODEID(token);

  gasneti_assert(sourceid < gasnetc_nodes);
  *srcindex = sourceid;
  return GASNET_OK;
}

GASNET_INLINE_MODIFIER(gasnetc_AMProcess)
void
gasnetc_AMProcess(gasnetc_am_header_t *hdr, uint32_t *args /* header */)
{
	gasnetc_handler_fn_t	handler;
	gasnet_token_t		token;
	size_t			numargs = (size_t) hdr->numargs;


	handler = gasnetc_handler[hdr->handler];
	token = (uintptr_t) args[0];

	switch (hdr->type) {
	    case GASNETC_AMSHORT_T:
		{   gasnet_handlerarg_t *pargs =
			(gasnet_handlerarg_t *) &args[1];
		    if (GASNETC_AMHEADER_ISREQUEST(hdr->reqrep))
			GASNETI_TRACE_AMSHORT_REQHANDLER(
			    hdr->handler,token,numargs,pargs);
		    else
			GASNETI_TRACE_AMSHORT_REPHANDLER(
			    hdr->handler,token,numargs,pargs);
		    GASNETC_RUN_HANDLER_SHORT(handler,token,pargs,numargs);
		}
		break;
	    case GASNETC_AMMED_T:
		{   gasnet_handlerarg_t *pargs =
			(gasnet_handlerarg_t *) &args[2];
		    int nbytes = args[1];
		    void *pdata = (pargs + numargs + 
				    GASNETC_MEDHEADER_PADARG(numargs));
		    if (GASNETC_AMHEADER_ISREQUEST(hdr->reqrep))
			GASNETI_TRACE_AMMEDIUM_REQHANDLER(
			    hdr->handler,token,pdata,nbytes,numargs,pargs);
		    else
			GASNETI_TRACE_AMMEDIUM_REPHANDLER(
			    hdr->handler,token,pdata,nbytes,numargs,pargs);
		    GASNETC_RUN_HANDLER_MEDIUM(handler,token,pargs,numargs,
					       pdata, nbytes);
		}
		break;
	    case GASNETC_AMLONG_T:
		{   gasnet_handlerarg_t *pargs =
			(gasnet_handlerarg_t *) &args[4];
		    int nbytes = args[1];
		    void *pdata = (void *) &args[2];
		    if (GASNETC_AMHEADER_ISREQUEST(hdr->reqrep))
			GASNETI_TRACE_AMLONG_REQHANDLER(
			    hdr->handler,token,pdata,nbytes,numargs,pargs);
		    else
			GASNETI_TRACE_AMLONG_REPHANDLER(
			    hdr->handler,token,pdata,nbytes,numargs,pargs);
		    GASNETC_RUN_HANDLER_LONG(handler,token,pargs,numargs,
					     pdata,nbytes);
		}
		break;
	    default:
		abort();
		break;
	}

	return;
}

#ifdef CRAYX1
extern int
gasnetc_AMPoll()
{
    int	    retval;
    int	    i;
    long    idx, bits, index, off, mask;

    gasnetc_am_header_t	amhdr;

    for (i = 0, off = 0; i < gasnetc_amq_numfields; i++, off += 64) {
	
	bits = _amo_afax((volatile unsigned long *) &gasnetc_amq_reqfields[i], 
			 0xffffffffffffffff, 0);
	if (bits == 0)
		continue;

	/*
	 * Under Cray, we use the leadz intrinsics to process each field
	 */
	index = _leadz64(bits);

	do {
	    /* map the (field no,idx) --> index */
	    idx = index + off;

	    /* get the mask of the current field index */
	    mask = 0x8000000000000000ul >> index;

	    GASNETC_AMHEADER_UNPACK(
		gasnetc_amq_reqs[idx].header,
		amhdr.reqrep, amhdr.type, amhdr.numargs, 
		amhdr.handler, amhdr.pe);

	    gasnetc_AMProcess(&amhdr, &gasnetc_amq_reqs[idx].header);

	    /* Mask off the index in the global bitfield */
	    _amo_aax((volatile unsigned long *) &gasnetc_amq_reqfields[i], 
		    ~mask, 0);

	    /* Mark the slot as free */
	    gasnetc_amq_reqs[idx].state = GASNETC_AMQUEUE_FREE_S;

	    /* Mask off the index in the current bitfield */
	    bits &= ~mask;
	    index = _leadz64(bits);
	}
	while (index < 64);
    }
    return GASNET_OK;
}
#else
/* 
 * Unlike the Cray AMPoll, this (generic) poll has not been tuned yet.  It
 * currently cycles through every AM slot every time AMPoll is called.
 *
 */
extern int 
gasnetc_AMPoll() 
{
    int	    retval;
    int	    iters = gasnetc_amq_depth;
    int	    idx;
    gasnetc_am_header_t	amhdr;

    GASNETI_CHECKATTACH();

    idx = gasnetc_amq_idx & gasnetc_amq_mask;

    while (iters-- > 0) {
	if (gasnetc_amq_reqs[idx].state == GASNETC_AMQUEUE_DONE_S) {
	    GASNETC_AMHEADER_UNPACK(
		gasnetc_amq_reqs[idx].header,
		amhdr.reqrep, amhdr.type, amhdr.numargs, 
		amhdr.handler, amhdr.pe);

	    gasnetc_AMProcess(&amhdr, &gasnetc_amq_reqs[idx].header);

	    gasnetc_amq_reqs[idx].state = GASNETC_AMQUEUE_FREE_S;

	}

	idx = (idx - 1) & gasnetc_amq_mask;
    }

    return GASNET_OK;
}
#endif

/* ------------------------------------------------------------------------------------ */
/*
  Active Message Request Functions
  ================================
*/

extern int gasnetc_AMRequestShortM( 
                            gasnet_node_t dest,       /* destination node */
                            gasnet_handler_t handler, /* index into destination endpoint's handler table */ 
                            int numargs, ...) {
  int retval, myidx, i;
  size_t    len;
  va_list argptr;
  gasnetc_am_stub_t   _amstub;
  uint32_t  *args;

  GASNETI_CHECKATTACH();
  if_pf (dest >= gasnetc_nodes) GASNETI_RETURN_ERRR(BAD_ARG,"node index too high");
  gasneti_assert(numargs >= 0 && numargs <= gasnet_AMMaxArgs());
  GASNETI_TRACE_AMREQUESTSHORT(dest,handler,numargs);
  va_start(argptr, numargs); /*  pass in last argument */

  gasnetc_AMPoll();

#if 0 && defined(CRAYX1)
  myidx = gasnetc_AMQueueRequest(dest);

  args = (uint32_t *) shmem_ptr(&gasnetc_amq_reqs[myidx].header, dest);
  args[0] = GASNETC_AMHEADER_PACK(
			GASNETC_REQUEST_T, GASNETC_AMSHORT_T, 
			numargs, handler, gasnetc_mynode);

  GASNETC_VECTORIZE
  for (i = 1; i <= numargs; i++)
	  args[i] = (gasnet_handlerarg_t)va_arg(argptr, uint32_t);
#else

  /* Write header and pack args */
  _amstub.args[0] = GASNETC_AMHEADER_PACK(
			GASNETC_REQUEST_T, GASNETC_AMSHORT_T, 
			numargs, handler, gasnetc_mynode);

  GASNETC_VECTORIZE
  for (i = 1; i <= numargs; i++)
	  _amstub.args[i] = (gasnet_handlerarg_t)va_arg(argptr, uint32_t);
  len = GASNETC_SHORT_HEADERSZ + 4 * numargs;

  /* Get a slot in shared AMQueue */
  myidx = gasnetc_AMQueueRequest(dest);

  /* Put the header and arguments */
  shmem_putmem(&gasnetc_amq_reqs[myidx].header, &_amstub, len, dest);
  shmem_fence();

#endif

  /* Release a slot in shared AMQueue */
  gasnetc_AMQueueRelease(dest, myidx);

  retval = GASNET_OK;
  va_end(argptr);
  GASNETI_RETURN(retval);
}

extern int gasnetc_AMRequestMediumM( 
                            gasnet_node_t dest,      /* destination node */
                            gasnet_handler_t handler, /* index into destination endpoint's handler table */ 
                            void *source_addr, size_t nbytes,   /* data payload */
                            int numargs, ...) {
  int retval, myidx, i;
  size_t    len;
  va_list argptr;
  uint32_t *args, *pptr;
  gasnetc_am_stub_t   _amstub;

  GASNETI_CHECKATTACH();
  if_pf (dest >= gasnetc_nodes) GASNETI_RETURN_ERRR(BAD_ARG,"node index too high");
  gasneti_assert(numargs >= 0 && numargs <= gasnet_AMMaxArgs());
  if_pf (nbytes > gasnet_AMMaxMedium()) GASNETI_RETURN_ERRR(BAD_ARG,"nbytes too large");
  GASNETI_TRACE_AMREQUESTMEDIUM(dest,handler,source_addr,nbytes,numargs);
  va_start(argptr, numargs); /*  pass in last argument */

    /* add code here to read the arguments using va_arg(argptr, gasnet_handlerarg_t) 
             and send the active message 
     */
  gasnetc_AMPoll();

  /* Write args and pack a header */
  _amstub.args[0] = GASNETC_AMHEADER_PACK(
			GASNETC_REQUEST_T, GASNETC_AMMED_T, numargs, 
			handler, gasnetc_mynode);
  _amstub.args[1] = (uint32_t) nbytes;
  args = (uint32_t *) &_amstub.args[2];
  for (i = 0; i < numargs; i++)
	  args[i] = (gasnet_handlerarg_t)va_arg(argptr, uint32_t);
  len = GASNETC_MED_HEADERSZ + 4 * numargs;

  /* Get a slot in shared AMQueue */
  myidx = gasnetc_AMQueueRequest(dest);

  /* Adjust payload pointer according to numargs */
  pptr = (uint32_t *) &gasnetc_amq_reqs[myidx].payload + 1 + numargs +
	    GASNETC_MEDHEADER_PADARG(numargs);

  /* Put the header and arguments, followed by payload and a fence */
  shmem_putmem(&gasnetc_amq_reqs[myidx].header, &_amstub, len, dest);
  shmem_putmem(pptr, source_addr, nbytes, dest);
  shmem_fence();

  /* Release a slot in shared AMQueue */
  gasnetc_AMQueueRelease(dest, myidx);

  retval = GASNET_OK;
  va_end(argptr);
  GASNETI_RETURN(retval);
}

extern int gasnetc_AMRequestLongM( gasnet_node_t dest,        /* destination node */
                            gasnet_handler_t handler, /* index into destination endpoint's handler table */ 
                            void *source_addr, size_t nbytes,   /* data payload */
                            void *dest_addr,                    /* data destination on destination node */
                            int numargs, ...) {
  int retval, myidx, i;
  size_t    len;
  va_list argptr;
  uint32_t *args, *pptr;
  uintptr_t *rptr;
  gasnetc_am_stub_t   _amstub;

  GASNETI_CHECKATTACH();
  
  gasnetc_boundscheck(dest, dest_addr, nbytes);
  if_pf (dest >= gasnetc_nodes) GASNETI_RETURN_ERRR(BAD_ARG,"node index too high");
  gasneti_assert(numargs >= 0 && numargs <= gasnet_AMMaxArgs());
  if_pf (nbytes > gasnet_AMMaxLongRequest()) GASNETI_RETURN_ERRR(BAD_ARG,"nbytes too large");
  if_pf (((uintptr_t)dest_addr) < ((uintptr_t)gasnetc_seginfo[dest].addr) ||
         ((uintptr_t)dest_addr) + nbytes > 
           ((uintptr_t)gasnetc_seginfo[dest].addr) + gasnetc_seginfo[dest].size) 
         GASNETI_RETURN_ERRR(BAD_ARG,"destination address out of segment range");

  GASNETI_TRACE_AMREQUESTLONG(dest,handler,source_addr,nbytes,dest_addr,numargs);
  va_start(argptr, numargs); /*  pass in last argument */

    /* add code here to read the arguments using va_arg(argptr, gasnet_handlerarg_t) 
             and send the active message 
     */

  gasnetc_AMPoll();

  /* Write args and pack a header */
  _amstub.args[0] = GASNETC_AMHEADER_PACK(
			GASNETC_REQUEST_T, GASNETC_AMLONG_T, numargs, 
			handler, gasnetc_mynode);
  _amstub.args[1] = nbytes;
  *((uintptr_t *) &_amstub.args[2]) = (uintptr_t) dest_addr;
  args = &_amstub.args[4];
  for (i = 0; i < numargs; i++)
	  args[i] = (gasnet_handlerarg_t)va_arg(argptr, uint32_t);
  len = GASNETC_LONG_HEADERSZ + 4 * numargs;

  /* Get a slot in shared AMQueue */
  myidx = gasnetc_AMQueueRequest(dest);

#ifdef GASNETC_GLOBAL_ADDRESS
  #warning using memcpy
  memcpy(dest_addr, source_addr, nbytes);
#else
  shmem_putmem(dest_addr, source_addr, nbytes, dest);
#endif
  shmem_quiet();
  /* Put the header and arguments, followed by payload and a fence */
  shmem_putmem(&gasnetc_amq_reqs[myidx].header, &_amstub, len, dest);
  shmem_fence();

  /* Release a slot in shared AMQueue */
  gasnetc_AMQueueRelease(dest, myidx);

    retval = GASNET_OK;
  va_end(argptr);
  GASNETI_RETURN(retval);
}

extern int gasnetc_AMReplyShortM( 
                            gasnet_token_t token,       /* token provided on handler entry */
                            gasnet_handler_t handler, /* index into destination endpoint's handler table */ 
                            int numargs, ...) {
  int retval, myidx, i;
  size_t    len;
  va_list argptr;
  gasnet_node_t	dest;
  gasnetc_am_stub_t   _amstub;
  gasneti_assert(numargs >= 0 && numargs <= gasnet_AMMaxArgs());
  GASNETI_TRACE_AMREPLYSHORT(token,handler,numargs);
  va_start(argptr, numargs); /*  pass in last argument */

    /* add code here to read the arguments using va_arg(argptr, gasnet_handlerarg_t) 
             and send the active message 
     */

  /* Write header and pack args */
  dest = GASNETC_AMHEADER_NODEID(token);
  _amstub.args[0] = GASNETC_AMHEADER_PACK(
			GASNETC_REPLY_T, GASNETC_AMSHORT_T, 
			numargs, handler, gasnetc_mynode);
  for (i = 1; i <= numargs; i++)
	  _amstub.args[i] = (gasnet_handlerarg_t)va_arg(argptr, uint32_t);
  len = GASNETC_SHORT_HEADERSZ + 4 * numargs;

  /* Get a slot in shared AMQueue */
  myidx = gasnetc_AMQueueReply(dest);

  /* Put the header and arguments */
  shmem_putmem(&gasnetc_amq_reqs[myidx].header, &_amstub, len, dest);
  shmem_fence();

  /* Release a slot in shared AMQueue */
  gasnetc_AMQueueRelease(dest, myidx);

  retval = GASNET_OK;
  va_end(argptr);
  GASNETI_RETURN(retval);
}

extern int gasnetc_AMReplyMediumM( 
                            gasnet_token_t token,       /* token provided on handler entry */
                            gasnet_handler_t handler, /* index into destination endpoint's handler table */ 
                            void *source_addr, size_t nbytes,   /* data payload */
                            int numargs, ...) {
  int retval, i, myidx;
  va_list argptr;
  uint32_t *args, *pptr;
  size_t    len;
  gasnet_node_t	dest;
  gasnetc_am_stub_t   _amstub;
  gasneti_assert(numargs >= 0 && numargs <= gasnet_AMMaxArgs());
  if_pf (nbytes > gasnet_AMMaxMedium()) GASNETI_RETURN_ERRR(BAD_ARG,"nbytes too large");
  GASNETI_TRACE_AMREPLYMEDIUM(token,handler,source_addr,nbytes,numargs);
  va_start(argptr, numargs); /*  pass in last argument */

    /* add code here to read the arguments using va_arg(argptr, gasnet_handlerarg_t) 
             and send the active message 
     */

  /* Write args and pack a header */
  dest = GASNETC_AMHEADER_NODEID(token);
  _amstub.args[0] = GASNETC_AMHEADER_PACK(
			GASNETC_REPLY_T, GASNETC_AMMED_T, numargs, 
			handler, gasnetc_mynode);
  _amstub.args[1] = nbytes;
  args = (uint32_t *) &_amstub.args[2];
  for (i = 0; i < numargs; i++)
	  args[i] = (uint32_t) (gasnet_handlerarg_t)va_arg(argptr, int);
  len = GASNETC_MED_HEADERSZ + 4 * numargs;

  /* Get a slot in shared AMQueue */
  myidx = gasnetc_AMQueueReply(dest);

  /* Adjust payload pointer according to numargs */
  pptr = (uint32_t *) &gasnetc_amq_reqs[myidx].payload + numargs + 1 +
	    GASNETC_MEDHEADER_PADARG(numargs);

  /* Put the header and arguments, followed by payload and a fence */
  shmem_putmem(&gasnetc_amq_reqs[myidx].header, &_amstub, len, dest);
  shmem_putmem(pptr, source_addr, nbytes, dest);
  shmem_fence();

  /* Release a slot in shared AMQueue */
  gasnetc_AMQueueRelease(dest, myidx);

    retval = GASNET_OK;
  va_end(argptr);
  GASNETI_RETURN(retval);
}

extern int gasnetc_AMReplyLongM( 
                            gasnet_token_t token,       /* token provided on handler entry */
                            gasnet_handler_t handler, /* index into destination endpoint's handler table */ 
                            void *source_addr, size_t nbytes,   /* data payload */
                            void *dest_addr,                    /* data destination on destination node */
                            int numargs, ...) {
  int retval, i, myidx;
  gasnet_node_t dest;
  uint32_t *args;
  size_t len;
  va_list argptr;
  gasnetc_am_stub_t   _amstub;
  
  retval = gasnet_AMGetMsgSource(token, &dest);
  if (retval != GASNET_OK) GASNETI_RETURN(retval);
  gasnetc_boundscheck(dest, dest_addr, nbytes);
  if_pf (dest >= gasnetc_nodes) GASNETI_RETURN_ERRR(BAD_ARG,"node index too high");
  gasneti_assert(numargs >= 0 && numargs <= gasnet_AMMaxArgs());
  if_pf (nbytes > gasnet_AMMaxLongReply()) GASNETI_RETURN_ERRR(BAD_ARG,"nbytes too large");
  if_pf (((uintptr_t)dest_addr) < ((uintptr_t)gasnetc_seginfo[dest].addr) ||
         ((uintptr_t)dest_addr) + nbytes > 
           ((uintptr_t)gasnetc_seginfo[dest].addr) + gasnetc_seginfo[dest].size) 
         GASNETI_RETURN_ERRR(BAD_ARG,"destination address out of segment range");

  GASNETI_TRACE_AMREPLYLONG(token,handler,source_addr,nbytes,dest_addr,numargs);
  va_start(argptr, numargs); /*  pass in last argument */

    /* add code here to read the arguments using va_arg(argptr, gasnet_handlerarg_t) 
             and send the active message 
     */

  /* Write args and pack a header */
  dest = GASNETC_AMHEADER_NODEID(token);
  _amstub.args[0] = GASNETC_AMHEADER_PACK(
			GASNETC_REPLY_T, GASNETC_AMLONG_T, numargs, 
			handler, gasnetc_mynode);
  _amstub.args[1] = nbytes;
  *((uintptr_t *) &_amstub.args[2]) = (uintptr_t) dest_addr;
  args = &_amstub.args[4];
  for (i = 0; i < numargs; i++)
	  args[i] = (gasnet_handlerarg_t)va_arg(argptr, uint32_t);
  len = GASNETC_LONG_HEADERSZ + 4 * numargs;

  /* Get a slot in shared AMQueue */
  myidx = gasnetc_AMQueueReply(dest);

#ifdef GASNETC_GLOBAL_ADDRESS
  printf("long memcpy, addr = %p, src = %p, node = %d\n", dest_addr, source_addr, dest);
  memcpy(dest_addr, source_addr, nbytes);
#else
  shmem_putmem(dest_addr, source_addr, nbytes, dest);
#endif
  shmem_quiet();
  /* Put the header and arguments, followed by payload and a fence */
  shmem_putmem(&gasnetc_amq_reqs[myidx].header, &_amstub, len, dest);
  shmem_fence();

  /* Release a slot in shared AMQueue */
  gasnetc_AMQueueRelease(dest, myidx);

    retval = GASNET_OK;
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

/*
 * SegmentInit() uses a network-specific way of detecting the maximum segment
 * that can be allocated.
 *
 */
static
int
gasnetc_SegmentInit()
{
	return 1;
}
/* ------------------------------------------------------------------------------------ */
#define GASNETC_SHMALLOC_GRANULARITY	(10<<20)

static
gasnet_seginfo_t
gasnetc_SHMallocBinarySearch(size_t low, size_t high)
{
	gasnet_seginfo_t	si;

	if (high - low <= GASNETC_SHMALLOC_GRANULARITY) {
		si.addr = NULL;
		si.size = 0;
		return si;
	}

	si.size = GASNETI_PAGE_ALIGNDOWN(low + (high-low)/2);

	si.addr = shmalloc(si.size);

	if (si.addr == NULL)
		return gasnetc_SHMallocBinarySearch(low, si.size);
	else {
		gasnet_seginfo_t	si_temp;

		shfree(si.addr);
		si_temp = gasnetc_SHMallocBinarySearch(si.size, high);
		if (si_temp.size)
			return si_temp;
		else
			return si;
	}
}

#ifdef LINUX
long
gasnetc_getMaxMem()
{
	FILE		*fp;
	char		line[128];
	unsigned long	mem = 0;

	if ((fp = fopen("/proc/meminfo", "r")) == NULL)
		gasneti_fatalerror("Can't open /proc/meminfo");

	while (fgets(line, 128, fp)) {
		if (sscanf(line, "Mem: %lu", &mem) > 0)
			break;
	}
	fclose(fp);
	return (uintptr_t) mem;
}

#elif CRAY_SHMEM
size_t
gasnetc_getMaxMem()
{
	return (64UL<<30);
}
#else
  #error No gasnetc_getMaxMem() defined for this platform
#endif

static
gasnet_seginfo_t
gasnetc_SHMallocSegmentSearch()
{
	gasnet_seginfo_t    si;
	gasneti_stattime_t  starttime, endtime;
	int64_t		    start, end;
	size_t		    maxsz;

	maxsz = gasnetc_getMaxMem();

	if (gasnetc_mynode == 0)
		printf("maxsiz = %lu (%.2f GB), pagesize=%d\n\n", 
			maxsz, (float) maxsz / (1024*1024*1024),
			(int) gasnetc_pagesize);
#ifdef ALTIX
	{
	    size_t allocsz;
	    double  frac;

	    /* !!! */
	    maxsz /= 256;

	    starttime = GASNETI_STATTIME_NOW();
	    si.addr = NULL;
	    for (frac = 0.95; frac > 0.1; frac -= 0.1) {
		allocsz = (size_t) (frac * maxsz);

		if (gasnetc_mynode == 0)
		    printf("trying for %lu bytes\n", allocsz);
		si.addr = shmalloc(allocsz);
		if (gasnetc_mynode == 0)
		    printf("trying for %lu bytes. . . got %p and 0 has \n", allocsz, si.addr);

		if (si.addr != NULL)
			break;
	    }
	    endtime = GASNETI_STATTIME_NOW();

	    if (si.addr != NULL)
		si.size = allocsz;
	}
#else
	starttime = GASNETI_STATTIME_NOW();
	si = gasnetc_SHMallocBinarySearch(0UL, maxsz);
	endtime = GASNETI_STATTIME_NOW();
#endif

	if (gasnetc_mynode == 0)
		printf("shmalloc search for %lu bytes (max=%lu) took %d us\n", 
		    si.size, maxsz, 
		    GASNETI_STATTIME_TO_US(endtime-starttime));

	return si;
}


/*  $Archive:: /Ti/GASNet/shmem-conduit/gasnet_core.c                  $
 *     $Date: 2003/11/11 13:40:39 $
 * $Revision: 1.1.2.1 $
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

static gasnet_seginfo_t gasnetc_SHMallocSegmentSearch(size_t maxsz);

#define GASNETC_MAX_NUMHANDLERS   256
typedef void (*gasnetc_handler_fn_t)();  /* prototype for handler function */
gasnetc_handler_fn_t gasnetc_handler[GASNETC_MAX_NUMHANDLERS]; /* handler table */

uintptr_t gasnetc_MaxLocalSegmentSize = 0;
uintptr_t gasnetc_MaxGlobalSegmentSize = 0;

gasnet_seginfo_t	 gasnetc_seginfo_init;
gasnet_seginfo_t	*gasnetc_seginfo = NULL;
size_t			 gasnetc_pagesize;

int  gasnetc_amq_idx;
int  gasnetc_amq_depth;
int  gasnetc_amq_mask;

gasnetc_am_packet_t  gasnetc_amq_reqs[GASNETC_AMQUEUE_MAX_DEPTH];

#ifdef GASNETC_AMQUEUE_RELEASE_MSWAP
long gasnetc_amq_donevec[GASNETC_MAX_AMQUEUE_DEPTH_VEC];
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
  /*  check system sanity */
  gasnetc_check_config();

  if (gasneti_init_done) 
    GASNETI_RETURN_ERRR(NOT_INIT, "GASNet already initialized");

  if (getenv("GASNET_FREEZE")) gasneti_freezeForDebugger();

  #if GASNET_DEBUG_VERBOSE
    /* note - can't call trace macros during gasnet_init because trace system not yet initialized */
    fprintf(stderr,"gasnetc_init(): about to spawn...\n"); fflush(stderr);
  #endif

  gasnetc_mynode = shmem_my_pe();
  gasnetc_nodes = shmem_n_pes()

  #if GASNET_DEBUG_VERBOSE
    fprintf(stderr,"gasnetc_init(): spawn successful - node %i/%i starting...\n", 
      gasnetc_mynode, gasnetc_nodes); fflush(stderr);
  #endif

  #if GASNET_SEGMENT_FAST || GASNET_SEGMENT_LARGE
    { 

	#if defined(CRAY_SHMEM) || defined(SGI_SHMEM)
		/* XXX Currently SHMallocSegmentSearch takes about 1 second.
		 * We may want to take another approach if it is too long.
		 * Since Cray machines do not necessarily ship with much
		 * configuration variance, perhaps there's a static way of
		 * determining the amount of physical memory.
		 */
		gasnetc_seginfo_init = gasnetc_SHMallocSegmentSearch(64UL<<30);

		/* Since shmalloc() is collective, local == global */
		gasnetc_MaxLocalSegmentSize = gasnetc_MaxGlobalSegmentSize 
			= gasnetc_seginfo_init.size;

		/* We keep the allocation live until gasnet_attach(), in which
		 * case we can simply use realloc to reduce its size */

	#elif defined(ELAN_SHMEM)
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
    if (segsize == 0) segbase = NULL; /* no segment */
    else {

      gasneti_assert(((uintptr_t)segbase) % GASNET_PAGESIZE == 0);
      gasneti_assert(segsize % GASNET_PAGESIZE == 0);

      /* With Cray, we use our preallocated segment and optionally truncate it
       * with realloc if the user requests something smaller
       */
	#ifdef CRAY_SHMEM
	    if (segsize < gasnetc_seginfo_init.size) {
		void	*ret;

		ret = shrealloc(gasnetc_seginfo_init.addr, segsize);
		if (ret == NULL) {
			shfree(gasnetc_seginfo_init.addr);
			gasneti_fatalerror("shrealloc() failed on initial GASNet segment\n");
		}
		gasnetc_seginfo_init.addr = (void *) ret;
		gasnetc_seginfo_init.size = segsize;
	    }

	    /*
	     * Although remote pointers are translated to a unaligned local
	     * address on shmem, we consider the segment to be aligned, at
	     * least in the generic instatiation of shmem-conduit
	     *
	     */

	    { int i;
		for (i=0;i<gasnetc_nodes;i++) {
		gasnetc_seginfo[i].addr = gasnetc_seginfo_init.addr;
		gasnetc_seginfo[i].size = gasnetc_seginfo_init.size;
	    }

	#else
	    #error SHMEM TODO
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
  gasneti_killmyprocess(exitcode);
  abort();
}

/* ------------------------------------------------------------------------------------ */
/*
  Job Environment Queries
  =======================
*/
extern int gasnetc_getSegmentInfo(gasnet_seginfo_t *seginfo_table, int numentries) {
  GASNETI_CHECKATTACH();
  gasneti_assert(gasnetc_seginfo && seginfo_table);
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

GASNET_INLINE_MODIFIER(gasnetc_AMProcessRequest)
static int
gasnetc_AMProcess(gasnet_am_header_t *hdr, uint32_t *args /* header */)
{
	gasnetc_handler_fn_t	handler;

	handler = gasnetc_handler[hdr->handler];

	switch (hdr->type) {
	    case GASNETC_AMSHORT_T:
		{   gasnet_handlerarg_t *pargs =
			(gasnet_handlerarg_t *) &args[1];
		    if (GASNETC_AMHEADER_ISREQUEST(hdr->reqrep))
			GASNETI_TRACE_AMSHORT_REQHANDLER(
			    hdr->handler, args, hdr->numargs, pargs);
		    else
			GASNETI_TRACE_AMSHORT_REPHANDLER(
			    hdr->handler, args, hdr->numargs, pargs);
		    GASNETC_RUN_HANDLER_SHORT(handler,args,pargs,numargs);
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
			    hdr->handler,args,pdata,nbytes,hdr->numargs,pargs);
		    else
			GASNETI_TRACE_AMMEDIUM_REPHANDLER(
			    hdr->handler,args,pdata,nbytes,hdr->numargs,pargs);
		    GASNETC_RUN_HANDLER_MEDIUM(handler,args,pargs,numargs);
		}
		break;
	    case GASNETC_AMLONG_T:
		{   gasnet_handlerarg_t *pargs =
			(gasnet_handlerarg_t *) &args[4];
		    int nbytes = args[1];
		    void *pdata = (void *) &args[2];
		    if (GASNETC_AMHEADER_ISREQUEST(hdr->reqrep))
			GASNETI_TRACE_AMLONG_REQHANDLER(
			    hdr->handler,args,pdata,nbytes,hdr->numargs,pargs);
		    else
			GASNETI_TRACE_AMLONG_REPHANDLER(
			    hdr->handler,args,pdata,nbytes,hdr->numargs,pargs);
		    GASNETC_RUN_HANDLER_LONG(handler,args,pargs,numargs);
		}
		break;
	    default:
		abort();
		break;
	    }
	}
}

#ifdef GASNETC_AMQUEUE_RELEASE_PUT
extern int 
gasnetc_AMPoll() {
  int	    retval;
  int	    i, idx = 0

  gasnet_am_header_t	amhdr;

  GASNETI_CHECKATTACH();

    for (i = 0; i < gasnetc_amq_depth; i++) {
	
	if (gasnetc_amq_reqs[idx].state == GASNETC_AMQUEUE_DONE_S) {
	    GASNETC_AMHEADER_UNPACK(
		gasnetc_amq_reqs[idx].header,
		amhdr.reqrep, amhdr.type, amhdr.numargs, 
		amhdr.handler, (uint32_t) amhdr.pe);

	    gasnetc_AMProcess(&amhdr, gasnetc_amq_reqs[idx].header);

	    gasnetc_amq_reqs[idx].state = GASNETC_AMQUEUE_FREE_S;
	}
    }

    return GASNET_OK;
}
#endif

/* ------------------------------------------------------------------------------------ */
/*
  Active Message Request Functions
  ================================
*/

/* The stub is used globally right now. . */
static	gasnetc_am_stub_t   _amstub;

extern int gasnetc_AMRequestShortM( 
                            gasnet_node_t dest,       /* destination node */
                            gasnet_handler_t handler, /* index into destination endpoint's handler table */ 
                            int numargs, ...) {
  int retval, myidx, i;
  size_t    len;
  va_list argptr;

  GASNETI_CHECKATTACH();
  if_pf (dest >= gasnetc_nodes) GASNETI_RETURN_ERRR(BAD_ARG,"node index too high");
  gasneti_assert(numargs >= 0 && numargs <= gasnet_AMMaxArgs());
  GASNETI_TRACE_AMREQUESTSHORT(dest,handler,numargs);
  va_start(argptr, numargs); /*  pass in last argument */

  gasnetc_AMPoll();

  /* Write header and pack args */
  _amstub.args[0] = GASNETC_AMHEADER_PACK(
			GASNETC_REQUEST_T, GASNETC_AMSHORT_T, 
			numargs, handler, dest);
  for (i = 1; i <= numargs; i++)
	  _amstub.args[i] = (gasnet_handlerarg_t)va_arg(argptr, uint32_t);
  len = GASNETC_SHORT_HEADERSZ + 4 * numargs;

  /* Get a slot in shared AMQueue */
  myidx = gasnetc_AMQueueRequest(dest);

  /* Put the header and arguments */
  shmem_putmem(&gasnetc_amq_reqs[myidx].header, &_amstub, len, dest);
  shmem_fence();

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
			handler, dest);
  _amstub.args[1] = nbytes;
  args = &_amstub.args[2];
  for (i = 0; i < numargs; i++)
	  args[i] = (gasnet_handlerarg_t)va_arg(argptr, uint32_t);
  len = GASNETC_MED_HEADERSZ + 4 * numargs;

  /* Adjust payload pointer according to numargs */
  pptr = &gasnetc_amq_reqs[myidx].payload + numargs + 1 +
	    GASNETC_MEDHEADER_PADARG(numargs);

  /* Get a slot in shared AMQueue */
  myidx = gasnetc_AMQueueRequest(dest);

  /* Put the header and arguments, followed by payload and a fence */
  shmem_putmem(&gasnetc_amq_reqs[myidx].header, &_amstub, len, dest);
  shmem_putmem(pptr, source_addr, nbytes, dest);
  shmem_fence();

  /* Release a slot in shared AMQueue */
  gasnetc_AMQueueRelease(dest, myidx);

    retval = GASNET_OK
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
			handler, dest);
  _amstub.args[1] = nbytes;
  *((uintptr_t *) &_amstub.args[2]) = (uintptr_t) dest_addr;
  args = &_amstub.args[4];
  for (i = 0; i < numargs; i++)
	  args[i] = (gasnet_handlerarg_t)va_arg(argptr, uint32_t);
  len = GASNETC_LONG_HEADERSZ + 4 * numargs;

  /* Get a slot in shared AMQueue */
  myidx = gasnetc_AMQueueRequest(dest);

  /* Put the header and arguments, followed by payload and a fence */
  shmem_putmem(&gasnetc_amq_reqs[myidx].header, &_amstub, len, dest);
  shmem_putmem(dest_addr, source_addr, nbytes, dest);
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
  gasneti_assert(numargs >= 0 && numargs <= gasnet_AMMaxArgs());
  GASNETI_TRACE_AMREPLYSHORT(token,handler,numargs);
  va_start(argptr, numargs); /*  pass in last argument */

    /* add code here to read the arguments using va_arg(argptr, gasnet_handlerarg_t) 
             and send the active message 
     */

  /* Write header and pack args */
  _amstub.args[0] = GASNETC_AMHEADER_PACK(
			GASNETC_REPLY_T, GASNETC_AMSHORT_T, 
			numargs, handler, dest);
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
  int retval;
  va_list argptr;
  uint32_t *args, *pptr;
  gasneti_assert(numargs >= 0 && numargs <= gasnet_AMMaxArgs());
  if_pf (nbytes > gasnet_AMMaxMedium()) GASNETI_RETURN_ERRR(BAD_ARG,"nbytes too large");
  GASNETI_TRACE_AMREPLYMEDIUM(token,handler,source_addr,nbytes,numargs);
  va_start(argptr, numargs); /*  pass in last argument */

    /* add code here to read the arguments using va_arg(argptr, gasnet_handlerarg_t) 
             and send the active message 
     */

  /* Write args and pack a header */
  _amstub.args[0] = GASNETC_AMHEADER_PACK(
			GASNETC_REPLY_T, GASNETC_AMMED_T, numargs, 
			handler, dest);
  _amstub.args[1] = nbytes;
  args = &_amstub.args[2];
  for (i = 0; i < numargs; i++)
	  args[i] = (gasnet_handlerarg_t)va_arg(argptr, uint32_t);
  len = GASNETC_MED_HEADERSZ + 4 * numargs;

  /* Adjust payload pointer according to numargs */
  pptr = &gasnetc_amq_reqs[myidx].payload + numargs + 1 +
	    GASNETC_MEDHEADER_PADARG(numargs);

  /* Get a slot in shared AMQueue */
  myidx = gasnetc_AMQueueReply(dest);

  /* Put the header and arguments, followed by payload and a fence */
  shmem_putmem(&gasnetc_amq_reqs[myidx].header, &_amstub, len, dest);
  shmem_putmem(pptr, source_addr, nbytes, dest);
  shmem_fence();

  /* Release a slot in shared AMQueue */
  gasnetc_AMQueueRelease(dest, myidx);

    retval = GASNET_OK
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
  gasnet_node_t dest;
  va_list argptr;
  
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
  _amstub.args[0] = GASNETC_AMHEADER_PACK(
			GASNETC_REPLY_T, GASNETC_AMLONG_T, numargs, 
			handler, dest);
  _amstub.args[1] = nbytes;
  *((uintptr_t *) &_amstub.args[2]) = (uintptr_t) dest_addr;
  args = &_amstub.args[4];
  for (i = 0; i < numargs; i++)
	  args[i] = (gasnet_handlerarg_t)va_arg(argptr, uint32_t);
  len = GASNETC_LONG_HEADERSZ + 4 * numargs;

  /* Get a slot in shared AMQueue */
  myidx = gasnetc_AMQueueReply(dest);

  /* Put the header and arguments, followed by payload and a fence */
  shmem_putmem(&gasnetc_amq_reqs[myidx].header, &_amstub, len, dest);
  shmem_putmem(dest_addr, source_addr, nbytes, dest);
  shmem_fence();

  /* Release a slot in shared AMQueue */
  gasnetc_AMQueueRelease(dest, myidx);

    retval = GASNET_OK
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

/* ------------------------------------------------------------------------------------ */
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

	si.size = GASNETC_PAGE_ALIGNDOWN(low + (high-low)/2);

	/* possibly use shmemalign() */
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

static
gasnet_seginfo_t
gasnetc_SHMallocSegmentSearch(size_t maxsz)
{
	gasnet_seginfo_t    si;
	int64_t		start, end;

	if (_my_pe() == 0)
		printf("sizeof(size_t)=%d, maxsiz = %lu, pagesize=%d\n\n", 
			sizeof(size_t), maxsz, gasnetc_pagesize);

	start = TimeStamp();
	si = gasnetc_SHMallocBinarySearch(0UL, maxsz);
	end = TimeStamp();

	if (_my_pe() == 0)
		printf("shmalloc search for %d bytes (max=%lu) took %d us\n", 
		    si.size, maxsz, (end-start));

	return si;
}


/*   $Source: /Users/kamil/work/gasnet-cvs2/gasnet/portals-conduit/Attic/gasnet_core.c,v $
 *     $Date: 2006/12/15 01:31:49 $
 * $Revision: 1.1.2.4 $
 * Description: GASNet portals conduit Implementation
 * Copyright 2002, Dan Bonachea <bonachea@cs.berkeley.edu>
 *                 Michael Welcome <mlwelcome@lbl.gov>
 * Terms of use are as specified in license.txt
 */

#include <gasnet_internal.h>
#include <gasnet_handler.h>
#include <gasnet_core_internal.h>
#include <gasnet_portals.h>

#include <errno.h>
#include <unistd.h>
#include <signal.h>

GASNETI_IDENT(gasnetc_IdentString_Version, "$GASNetCoreLibraryVersion: " GASNET_CORE_VERSION_STR " $");
GASNETI_IDENT(gasnetc_IdentString_Name,    "$GASNetCoreLibraryName: " GASNET_CORE_NAME_STR " $");

gasnet_handlerentry_t const *gasnetc_get_handlertable();
static void gasnetc_atexit(void);
static void gasnetc_traceoutput(int);

#define GASNETC_MAX_NUMHANDLERS   256
gasnetc_handler_fn_t gasnetc_handler[GASNETC_MAX_NUMHANDLERS]; /* handler table (recommended impl) */

#if 0
/* MLW: remove? */
#if GASNETC_HSL_ERRCHECK || GASNET_TRACE
  extern void gasnetc_enteringHandler_hook(ammpi_category_t cat, int isReq, int handlerId, void *token, 
                                         void *buf, size_t nbytes, int numargs, uint32_t *args);
  extern void gasnetc_leavingHandler_hook(ammpi_category_t cat, int isReq);
#endif
#endif

/* ------------------------------------------------------------------------------------ */
/*
  Initialization
  ==============
*/
/* called at startup to check configuration sanity */
static void gasnetc_check_config() {
  gasneti_check_config_preinit();

  /* (###) add code to do some sanity checks on the number of nodes, handlers
   * and/or segment sizes */ 

  gasneti_assert_always(sizeof(gasnetc_chunk_t) == GASNETC_CHUNKSIZE);
}

static int gasnetc_init(int *argc, char ***argv) {
  /*  check system sanity */
  gasnetc_check_config();

  if (gasneti_init_done) 
    GASNETI_RETURN_ERRR(NOT_INIT, "GASNet already initialized");

  gasneti_freezeForDebugger();

  #if GASNET_DEBUG_VERBOSE
    /* note - can't call trace macros during gasnet_init because trace system not yet initialized */
    fprintf(stderr,"gasnetc_init(): about to spawn...\n"); fflush(stderr);
  #endif

    /* setup portals network */
  gasneti_mynode = cnos_get_rank();
  gasneti_nodes = cnos_get_size();
  printf("gasnetc_init: Mynode = %d, Total Nodes = %d\n",gasneti_mynode,gasneti_nodes);
  gasnetc_init_portals_network();
  printf("[%d] after gasnetc_init_portals_network()\n",gasneti_mynode);

  #if GASNET_DEBUG_VERBOSE
    fprintf(stderr,"gasnetc_init(): spawn successful - node %i/%i starting...\n", 
      gasneti_mynode, gasneti_nodes); fflush(stderr);
  #endif

  #if GASNET_SEGMENT_FAST || GASNET_SEGMENT_LARGE
    {
      /* try to determine the max amount of memory we can alloc and pin on each node */
      uintptr_t max_pin = gasnetc_portalsMaxPinMem();

      /* localSegmentLimit provides a conduit-specific limit on the max segment size.
       * can use (uintptr_t)-1 as unlimited.
       * In case of Portals/Catamount there is no mmap so both MaxLocalSegmentSize
       * and MaxGlobalSegmentSize are basically set to the min of localSegmentLimit
       * and GASNETI_MALLOCSEGMENT_MAX_SIZE, which defaults to 100MB.
       * So, it looks like we must come up with a reasonable value.
       * the problem is, we dont know how much non-shared memory the app will want to use.
       */
      gasneti_segmentInit( max_pin, &gasnetc_bootstrapExchange);
    }
  #elif GASNET_SEGMENT_EVERYTHING
    /* segment is everything - nothing to do */
  #else
    #error Bad segment config
  #endif

#if 0  /* MLW: tested this, and it seems Cray does propogate env on XT3 */
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
                                   &gasnetc_bootstrapExchange, &gasnetc_bootstrapBroadcast);
  #endif

  gasneti_init_done = 1;

  gasneti_auxseg_init(); /* adjust max seg values based on auxseg */

  return GASNET_OK;
}

/* ------------------------------------------------------------------------------------ */
extern int gasnet_init(int *argc, char ***argv) {
  int retval = gasnetc_init(argc, argv);
  if (retval != GASNET_OK) GASNETI_RETURN(retval);
  gasneti_trace_init(argc, argv);
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

  printf("[%d] gasnetc_attach segsize = %lu, max_local = %lu, max_global = %lu\n",
	 gasneti_mynode,(unsigned long)segsize,
	 (unsigned long)gasnet_getMaxLocalSegmentSize(),
	 (unsigned long)gasnet_getMaxGlobalSegmentSize()
	 );

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
    GASNETI_TRACE_PRINTF(C,("Registering %d default AM Handlers",GASNETC_MAX_NUMHANDLERS));
    for (i = 0; i < GASNETC_MAX_NUMHANDLERS; i++) 
      gasnetc_handler[i] = (gasnetc_handler_fn_t)&gasneti_defaultAMHandler;
  }
  { /*  core API handlers */
    gasnet_handlerentry_t *ctable = (gasnet_handlerentry_t *)gasnetc_get_handlertable();
    int len = 0;
    int numreg = 0;
    gasneti_assert(ctable);
    while (ctable[len].fnptr) len++; /* calc len */
    GASNETI_TRACE_PRINTF(C,("Registering %d core Handlers",len));
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
    GASNETI_TRACE_PRINTF(C,("Registering %d Extended API Handlers",len));
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
    GASNETI_TRACE_PRINTF(C,("Registering %d Client Handlers",numentries));

    gasneti_assert(numreg1 + numreg2 == numentries);
  }

  /* ------------------------------------------------------------------------------------ */
  /*  register fatal signal handlers */

  /* catch fatal signals and convert to SIGQUIT */
  gasneti_registerSignalHandlers(gasneti_defaultSignalHandler);

  atexit(gasnetc_atexit);

  /* ------------------------------------------------------------------------------------ */
  /*  register segment  */

  gasneti_seginfo = (gasnet_seginfo_t *)gasneti_malloc(gasneti_nodes*sizeof(gasnet_seginfo_t));

  #if GASNET_SEGMENT_FAST || GASNET_SEGMENT_LARGE
    if (segsize == 0) segbase = NULL; /* no segment */
    else {
      gasneti_segmentAttach(segsize, minheapoffset, gasneti_seginfo, &gasnetc_bootstrapExchange);
      segbase = gasneti_seginfo[gasneti_mynode].addr;
      printf("[%d] After segmentAttach base = %p, size = %llu  sbase = %p, ssize=%llu\n",
	     gasneti_mynode,
	     segbase,(unsigned long long)segsize,
	     gasneti_seginfo[gasneti_mynode].addr,
	     (unsigned long long)gasneti_seginfo[gasneti_mynode].size);
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
  /* This was done by segmentAttach above */

  /* ------------------------------------------------------------------------------------ */
  /*  primary attach complete */
  gasneti_attach_done = 1;
  gasnetc_bootstrapBarrier();

  GASNETI_TRACE_PRINTF(C,("gasnetc_attach(): primary attach complete"));

  gasneti_assert(gasneti_seginfo[gasneti_mynode].addr == segbase &&
         gasneti_seginfo[gasneti_mynode].size == segsize);

  gasneti_auxseg_attach(); /* provide auxseg */

  /* Init all the portals resources to allow for AMs, puts and gets */
  gasnetc_init_portals_resources();

  printf("[%d] before gasnete_init()\n",gasneti_mynode);
  gasnete_init(); /* init the extended API */
  printf("[%d] after gasnete_init()\n",gasneti_mynode);

  /* ensure extended API is initialized across nodes */
  gasnetc_bootstrapBarrier();

  printf("[%d] Leaving gasnetc_attach()\n",gasneti_mynode);

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

  /* (###) add code here to terminate the job across _all_ nodes 
           with gasneti_killmyprocess(exitcode) (not regular exit()), preferably
           after raising a SIGQUIT to inform the client of the exit
  */
  gasneti_fatalerror("gasnetc_exit failed!");
}

/* ------------------------------------------------------------------------------------ */
/*
  Misc. Active Message Functions
  ==============================
*/
extern int gasnetc_AMGetMsgSource(gasnet_token_t token, gasnet_node_t *srcindex) {
  gasnet_node_t sourceid;
  gasnetc_ptl_token_t *ptok = (gasnetc_ptl_token_t*)token;
  GASNETI_CHECKATTACH();
  GASNETI_CHECK_ERRR((!token),BAD_ARG,"bad token");
  GASNETI_CHECK_ERRR((!srcindex),BAD_ARG,"bad src ptr");

  /* MLW: for now, we sent node ID in data packet.  Could hash loopup on portals address */
  sourceid = ptok->srcnode;

  gasneti_assert(sourceid < gasneti_nodes);
  *srcindex = sourceid;
  return GASNET_OK;
}

extern int gasnetc_AMPoll() {
  int retval;
  GASNETI_CHECKATTACH();

  gasnetc_portals_poll();

  return GASNET_OK;
}

/* ------------------------------------------------------------------------------------ */
/*
  Active Message Request Functions
  ================================
*/

/* The NoOp AM handler function */
GASNETI_INLINE(gasnetc_noop_reph_inner)
void gasnetc_noop_reph_inner(gasnet_token_t token)
{
  GASNETI_TRACE_PRINTF(C,("Running No-Op Handler"));
}
SHORT_HANDLER(gasnetc_noop_reph,0,0,
              (token),
              (token) );

#define GASNETC_REQUEST_PACK_ARGS(amtype) do  \
    {  \
      gasnet_handlerarg_t   g_arg0 = 0;  \
      gasnet_handlerarg_t   g_arg1 = 0;  \
      \
      /* Construct match bits */  \
      mbits = ((uint64_t)local_offset << 32) | ((uint64_t)((amtype) | GASNETC_PTL_AM_REQUEST) << 24) \
	| ( ((uint64_t)numargs & GASNETC_MASK_BYTE0) << 16) | ((uint64_t)handler << 8)   \
	| GASNETC_PTL_REQRB_BITS  | GASNETC_PTL_MSG_AM;  \
      \
      /* Construct hdr_data */  \
      va_start(argptr, numargs); /*  pass in last argument */  \
      iarg = 0; /* MLW Debug */ \
      if (numargs > 0) {  \
	/* arg 0 into upper 32 bits of hdr_data */  \
	g_arg0 = va_arg(argptr,gasnet_handlerarg_t);  \
	numargs--;  \
	harg[iarg++] = g_arg0; /* MLW DEBUG */	\
      }  \
      if (numargs > 0) {  \
	/* arg 1 into lower 32 bits of hdr_data */  \
	g_arg1 = va_arg(argptr,gasnet_handlerarg_t);  \
	numargs--;  \
	harg[iarg++] = g_arg1; /* MLW DEBUG */	\
      }  \
      GASNETC_PACK_2INT(hdr_data,g_arg0,g_arg1);  \
      \
      /* First, put our gasnet node number as first item in data payload */  \
      memcpy(data,&gasneti_mynode,sizeof(gasnet_node_t));  \
      data += sizeof(gasnet_node_t);  \
      \
      /* load remaining args to data payload area, insure proper alignment and padding */  \
      msg_bytes = sizeof(gasnet_node_t) + numargs*sizeof(gasnet_handlerarg_t);  \
      while (numargs > 0) {  \
	g_arg0 = va_arg(argptr,gasnet_handlerarg_t);  \
	harg[iarg++] = g_arg0; /* MLW DEBUG */		     \
	memcpy(data, &g_arg0, sizeof(gasnet_handlerarg_t));  \
	data += sizeof(gasnet_handlerarg_t);  \
	numargs--;  \
      }  \
      va_end(argptr);  \
    } while(0)

extern int gasnetc_AMRequestShortM( 
                            gasnet_node_t dest,       /* destination node */
                            gasnet_handler_t handler, /* index into destination endpoint's handler table */ 
                            int numargs, ...) {
  int retval;
  va_list argptr;
  ptl_size_t         local_offset;
  ptl_size_t         remote_offset = 0;
  ptl_handle_md_t    md_h = gasnetc_ReqSB.md_h;
  ptl_process_id_t   target_id = gasnetc_procid_map[dest];
  ptl_ac_index_t     ac_index = GASNETC_PTL_AC_ID;
  ptl_match_bits_t   mbits;
  ptl_hdr_data_t     hdr_data;
  ptl_size_t         msg_bytes;
  int                pad;
  gasnetc_conn_t    *state = &gasnetc_conn_state[dest];
  uint8_t           *data;
  gasnet_handlerarg_t harg[16]; int iarg; /* MLW: debug */


  GASNETI_TRACE_PRINTF(C,("AMReq_Short to %d with handler %d and %d args",
			  dest,(int)handler,numargs));

  GASNETI_COMMON_AMREQUESTSHORT(dest,handler,numargs);

  /* poll until ok to send message, allocate ReqSB chunk */
  GASNETC_COMMON_AMSTART(state,local_offset);

  /* get the addr of the start of the chunk */
  data = (uint8_t*)gasnetc_ReqSB.start + local_offset;

  GASNETC_REQUEST_PACK_ARGS(GASNETC_PTL_AM_SHORT);

  /* pad if necessary */
  pad = msg_bytes % sizeof(double);
  pad = (pad == 0 ? 0 : sizeof(double)-pad);
  msg_bytes += pad;
  GASNETI_TRACE_PRINTF(C,("AMReq_Short to %d with pad = %d msg_bytes=%d",dest,pad,(int)msg_bytes));

  gasneti_assert( (msg_bytes % sizeof(double)) == 0 );

  /* send message */
  GASNETC_PTLSAFE(PtlPutRegion(md_h, local_offset, msg_bytes, PTL_NOACK_REQ, target_id, GASNETC_PTL_AM_PTE, ac_index, mbits, remote_offset, hdr_data));

  gasneti_weakatomic_increment(&state->AM_pending,0);

  GASNETI_RETURN(GASNET_OK);
}

extern int gasnetc_AMRequestMediumM( 
                            gasnet_node_t dest,      /* destination node */
                            gasnet_handler_t handler, /* index into destination endpoint's handler table */ 
                            void *source_addr, size_t nbytes,   /* data payload */
                            int numargs, ...) {
  int retval;
  va_list argptr;
  ptl_size_t         local_offset;
  ptl_size_t         remote_offset = 0;
  ptl_handle_md_t    md_h = gasnetc_ReqSB.md_h;
  ptl_process_id_t   target_id = gasnetc_procid_map[dest];
  ptl_ac_index_t     ac_index = GASNETC_PTL_AC_ID;
  ptl_match_bits_t   mbits;
  ptl_hdr_data_t     hdr_data;
  gasnetc_conn_t    *state = gasnetc_conn_state + dest;
  int                add_pad1, add_pad2;
  int                msg_bytes;
  int                hndlr_bytes = nbytes;  /* this will fit for medium message */
  uint8_t           *data;
  gasnet_handlerarg_t harg[16]; int iarg; /* MLW: debug */

  GASNETI_TRACE_PRINTF(C,("AMReq_Medium to %d with handler %d and %d args",dest,(int)handler,numargs));

  GASNETI_COMMON_AMREQUESTMEDIUM(dest,handler,source_addr,nbytes,numargs);

  /* poll until ok to send message, allocate ReqSB chunk */
  GASNETC_COMMON_AMSTART(state,local_offset);

  /* get the addr of the start of the chunk */
  data = (uint8_t*)gasnetc_ReqSB.start + local_offset;
  gasneti_assert( ((intptr_t)data % sizeof(double)) == 0 );

  GASNETC_REQUEST_PACK_ARGS(GASNETC_PTL_AM_MEDIUM);

  {
    int i;
    for (i = 0; i < iarg; i++) {
      GASNETI_TRACE_PRINTF(C,("AMReq_Medium arg[%d] = %d",i,harg[i]));
    }
  }

  /* send number of bytes in handler payload */
  memcpy(data,&hndlr_bytes,sizeof(int));
  data += sizeof(int);
  msg_bytes += sizeof(int);

  /* align to 8-byte boundary and add handler payload*/
  add_pad1 = msg_bytes % sizeof(double);
  add_pad1 = (add_pad1 == 0 ? 0 : sizeof(double)-add_pad1);
  data += add_pad1;
  memcpy(data,source_addr,hndlr_bytes);
  msg_bytes += add_pad1 + hndlr_bytes;

  /* insure message length is multiple of 8 bytes */
  add_pad2 = (msg_bytes % sizeof(double));
  add_pad2 = (add_pad2 == 0 ? 0 : sizeof(double)-add_pad2);
  msg_bytes += add_pad2;

  GASNETI_TRACE_PRINTF(C,("AMReq_Medium: add_pad1=%d, add_pad2=%d, msg_bytes=%d",add_pad1,add_pad2,msg_bytes));

  gasneti_assert( (msg_bytes % sizeof(double)) == 0 );
  gasneti_assert(msg_bytes <= GASNETC_CHUNKSIZE);

  /* send message */
  GASNETC_PTLSAFE(PtlPutRegion(md_h, local_offset, msg_bytes, PTL_NOACK_REQ, target_id, GASNETC_PTL_AM_PTE, ac_index, mbits, remote_offset, hdr_data));

  gasneti_weakatomic_increment(&state->AM_pending,0);

  GASNETI_RETURN(GASNET_OK);
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

    /* (###) add code here to read the arguments using va_arg(argptr, gasnet_handlerarg_t) 
             and send the active message 
     */

    retval = GASNET_OK;
  va_end(argptr);

  gasneti_fatalerror("gasnetc_AMRequestLongM not implemented");
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

    /* (###) add code here to read the arguments using va_arg(argptr, gasnet_handlerarg_t) 
             and send the active message 
     */

    retval = GASNET_OK;
  va_end(argptr);
  gasneti_fatalerror("gasnetc_AMRequestLongAsyncM not implemented");
  GASNETI_RETURN(retval);
}

#define GASNETC_REPLY_PACK_ARGS(amtype) do \
    { \
      gasnet_handlerarg_t g_arg0 = 0; \
      gasnet_handlerarg_t g_arg1 = 0; \
      /* Construct match bits, send back to initiator chunk in ReqSB */ \
      mbits = ((uint64_t)((amtype) | GASNETC_PTL_AM_REPLY) << 24)    \
	| ( ((uint64_t)numargs & GASNETC_MASK_BYTE0) << 16) | ((uint64_t)handler << 8)  \
	| GASNETC_PTL_REQSB_BITS  | GASNETC_PTL_MSG_AM;  \
      \
      /* Construct hdr_data */ \
      va_start(argptr, numargs); /*  pass in last argument */  \
      if (numargs > 0) { \
	/* arg 0 into upper 32 bits of hdr_data */ \
	g_arg0 = va_arg(argptr,gasnet_handlerarg_t);  \
	numargs--; \
      } \
      if (numargs > 0) { \
	/* arg 1 into lower 32 bits of hdr_data */ \
	g_arg1 = va_arg(argptr,gasnet_handlerarg_t);  \
	numargs--;  \
      }  \
      GASNETC_PACK_2INT(hdr_data,g_arg0,g_arg1);  \
      g_arg0 = 0;  \
      if (numargs > 0) {  \
	/* arg 3 into upper 32 bits of match_bits */  \
	g_arg0 = va_arg(argptr,gasnet_handlerarg_t);  \
	numargs--;  \
      }  \
      GASNETC_PACK_INT_UPPER(mbits,g_arg0);  \
      \
      /* First data item is our gasnet node id */  \
      memcpy(data, &gasneti_mynode, sizeof(gasnet_node_t));  \
      data += sizeof(gasnet_node_t);  \
      msg_bytes = sizeof(gasnet_node_t); \
      \
      /* remaining handler arguments */  \
      /* Note: both source and target buffer are known to be 8-byte aligned, so not padding */  \
      while (numargs > 0) {  \
	g_arg0 = va_arg(argptr, gasnet_handlerarg_t);  \
	memcpy(data,&g_arg0, sizeof(gasnet_handlerarg_t));  \
	data += sizeof(gasnet_handlerarg_t);  \
	msg_bytes += sizeof(gasnet_handlerarg_t);  \
	numargs--;  \
      }  \
      va_end(argptr);  \
    } while(0)

extern int gasnetc_AMReplyShortM( 
                            gasnet_token_t token,       /* token provided on handler entry */
                            gasnet_handler_t handler, /* index into destination endpoint's handler table */ 
                            int numargs, ...) {
  int retval;
  va_list argptr; 
  gasnetc_ptl_token_t *ptok = (gasnetc_ptl_token_t*)token;
  ptl_size_t         local_offset = ptok->rplsb_offset;
  ptl_size_t         remote_offset = ptok->initiator_offset;
  ptl_handle_md_t    md_h = gasnetc_RplSB.md_h;
  ptl_process_id_t   target_id = ptok->initiator;
  ptl_ac_index_t     ac_index = GASNETC_PTL_AC_ID;
  ptl_match_bits_t   mbits;
  ptl_hdr_data_t     hdr_data = 0;
  ptl_size_t         msg_bytes = 0;
  uint8_t           *data = (uint8_t*)gasnetc_RplSB.start + local_offset;

  GASNETI_TRACE_PRINTF(C,("AMReply_Short to %d with handler %d and %d args",
			  ptok->srcnode,(int)handler,numargs));

  GASNETI_COMMON_AMREPLYSHORT(token,handler,numargs);

  GASNETC_REPLY_PACK_ARGS(GASNETC_PTL_AM_SHORT);
  /* send message */
  GASNETC_PTLSAFE(PtlPutRegion(md_h, local_offset, msg_bytes, PTL_NOACK_REQ, target_id, GASNETC_PTL_AM_PTE, ac_index, mbits, remote_offset, hdr_data));

  /* Indicate to reply code that AM did in fact send a reply message */
  ptok->flags |= GASNETC_PTL_REPLY_SENT;

  GASNETI_RETURN(GASNET_OK);
}

extern int gasnetc_AMReplyMediumM( 
                            gasnet_token_t token,       /* token provided on handler entry */
                            gasnet_handler_t handler, /* index into destination endpoint's handler table */ 
                            void *source_addr, size_t nbytes,   /* data payload */
                            int numargs, ...) {
  int retval;
  va_list argptr;
  gasnetc_ptl_token_t *ptok = (gasnetc_ptl_token_t*)token;
  ptl_size_t         local_offset = ptok->rplsb_offset;
  ptl_size_t         remote_offset = ptok->initiator_offset;
  ptl_handle_md_t    md_h = gasnetc_RplSB.md_h;
  ptl_process_id_t   target_id = ptok->initiator;
  ptl_ac_index_t     ac_index = GASNETC_PTL_AC_ID;
  ptl_match_bits_t   mbits;
  ptl_hdr_data_t     hdr_data = 0;
  ptl_size_t         msg_bytes;
  int                payload_bytes = nbytes;
  uint8_t           *data = (uint8_t*)gasnetc_RplSB.start + local_offset;
  int                pad;

  GASNETI_TRACE_PRINTF(C,("AMReply_Medium to %d with handler %d and %d args %d byte payload",ptok->srcnode,(int)handler,numargs,(int)nbytes));

  GASNETI_COMMON_AMREPLYMEDIUM(token,handler,source_addr,nbytes,numargs);

  GASNETC_REPLY_PACK_ARGS(GASNETC_PTL_AM_MEDIUM);

  memcpy(data,&payload_bytes,sizeof(int));
  msg_bytes += sizeof(int);

  /* advance buffer for 8-byte alignment */
  pad = msg_bytes % sizeof(double);
  data += pad;
  msg_bytes += pad + nbytes;

  /* copy handler data payload here */
  memcpy(data,source_addr,nbytes);

  GASNETI_TRACE_PRINTF(C,("AMReply_Medium to %d msg_bytes=%d pad=%d",ptok->srcnode,(int)msg_bytes,pad));

  /* send message */
  GASNETC_PTLSAFE(PtlPutRegion(md_h, local_offset, msg_bytes, PTL_NOACK_REQ, target_id, GASNETC_PTL_AM_PTE, ac_index, mbits, remote_offset, hdr_data));

  /* Indicate to reply code that AM did in fact send a reply message */
  ptok->flags |= GASNETC_PTL_REPLY_SENT;

  GASNETI_RETURN(GASNET_OK);
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

    /* (###) add code here to read the arguments using va_arg(argptr, gasnet_handlerarg_t) 
             and send the active message 
     */

    retval = GASNET_OK;
  va_end(argptr);
  gasneti_fatalerror("gasnetc_AMReplyLongM not implemented");
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
      while (gasneti_mutex_trylock(&(hsl->lock)) == EBUSY) { }
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
    gasneti_handler_tableentry_with_bits(gasnetc_noop_reph),

  { 0, NULL }
};

gasnet_handlerentry_t const *gasnetc_get_handlertable() {
  return gasnetc_handlers;
}

/* ------------------------------------------------------------------------------------ */

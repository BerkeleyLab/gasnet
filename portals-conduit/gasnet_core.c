/*   $Source: /Users/kamil/work/gasnet-cvs2/gasnet/portals-conduit/Attic/gasnet_core.c,v $
 *     $Date: 2007/02/09 21:57:08 $
 * $Revision: 1.1.2.20 $
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
  gasneti_assert_always(sizeof(gasneti_weakatomic_val_t) == sizeof(uint32_t));
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
  gasnetc_init_portals_network();

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
  int retval;
#if GASNETC_USE_SANDIA_ACCEL
  gasneti_weakatomic_set(&gasnetc_got_signum,0,0);  
#endif
  retval = gasnetc_init(argc, argv);
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

  /* check for system messages */
  gasnetc_sys_poll();

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
#if GASNETC_USE_SANDIA_ACCEL
  gasneti_registerSignalHandlers(gasnetc_portalsSignalHandler);
#else
  gasneti_registerSignalHandlers(gasneti_defaultSignalHandler);
#endif

  atexit(gasnetc_atexit);

  /* ------------------------------------------------------------------------------------ */
  /*  register segment  */

  gasneti_seginfo = (gasnet_seginfo_t *)gasneti_malloc(gasneti_nodes*sizeof(gasnet_seginfo_t));

  #if GASNET_SEGMENT_FAST || GASNET_SEGMENT_LARGE
    if (segsize == 0) segbase = NULL; /* no segment */
    else {
      gasneti_segmentAttach(segsize, minheapoffset, gasneti_seginfo, &gasnetc_bootstrapExchange);
      segbase = gasneti_seginfo[gasneti_mynode].addr;
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

  /* should prevent us from entering again */
  gasnetc_shutdownInProgress = 1;

  /* send a shutdown message to everyone */
  {
    gasnet_node_t node;
    gasnetc_conn_state[gasneti_mynode].got_shutdown_msg = 1;
    GASNETI_TRACE_PRINTF(C,("Sending SHUTDOWN Messages to all nodes"));
    for (node = 0; node < gasneti_nodes; node++) {
      if (node != gasneti_mynode) 
	gasnetc_sys_SendMsg(node,GASNETC_SYS_SHUTDOWN_REQUEST, gasneti_mynode, exitcode, 0);
    }
  }

  /* Now, poll for a while to see if all nodes either sent us a shutdown request
   * or replied to our shutdown request
   */
  if (gasnetc_shutdown_seconds > 0) {
    gasnet_node_t node;
    int cnt = 0;
    uint64_t starttime = gasnett_ticks_to_ns(gasnett_ticks_now());  /* in nanoseconds */
    uint64_t stoptime= starttime;
    uint64_t shutdowntime = 1000000000UL * gasnetc_shutdown_seconds;
    while (( cnt < gasneti_nodes) && (stoptime-starttime<shutdowntime)) {
      cnt = 0;
      for (node = 0; node < gasneti_nodes; node++) cnt += gasnetc_conn_state[node].got_shutdown_msg;
      if (cnt < gasneti_nodes) gasnetc_sys_poll();
      stoptime = gasnett_ticks_to_ns(gasnett_ticks_now());
    } 

    if (cnt < gasneti_nodes) {
      /* have not heard back from some nodes, terminate job */
      printf("[%d] In shutdown, Only %d/%d nodes responded after %lu milliseconds\n",gasneti_mynode,cnt,gasneti_nodes,(stoptime-starttime)/1000000);
      gasneti_reghandler(SIGINT,SIG_DFL);  /* SIGINT causes launcher to kill job */
      raise(SIGINT);
    }
  }

  /* if we got here, this is a clean shutdown.  Clean up portals resources */
  gasnetc_portals_exit();

  /* preform cleanup operations */
  gasneti_flush_streams();
  gasneti_trace_finish();
  gasneti_sched_yield();
  
  /* kill myself without generateing core dumps, etc */
  gasneti_killmyprocess(exitcode);
  gasneti_fatalerror("gasnetc_exit: killmyprocess should not have returned");
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

  gasnetc_portals_poll(GASNETC_FULL_POLL);

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

extern int gasnetc_AMRequestShortM( 
                            gasnet_node_t dest,       /* destination node */
                            gasnet_handler_t handler, /* index into destination endpoint's handler table */ 
                            int numargs, ...) {
  int retval;
  va_list argptr;
  ptl_size_t         local_offset;
  ptl_size_t         remote_offset = 0;
  ptl_handle_md_t    md_h = gasnetc_ReqSB.md_h;
  ptl_process_id_t   target_id = gasnetc_procid_map[dest].ptl_id;
  ptl_ac_index_t     ac_index = GASNETC_PTL_AC_ID;
  uint64_t           amtype = GASNETC_PTL_AM_SHORT | GASNETC_PTL_AM_REQUEST;
  uint64_t           targ_mbits = GASNETC_PTL_REQRB_BITS  | GASNETC_PTL_MSG_AM;
  gasnet_handlerarg_t garg0 = 0;
  gasnet_handlerarg_t garg1 = 0;
  ptl_match_bits_t   mbits;
  ptl_hdr_data_t     hdr_data;
  ptl_size_t         msg_bytes = 0;
  int                i, pad;
  gasnetc_conn_t    *state = &gasnetc_conn_state[dest];
  uint8_t           *data;
  gasnetc_threaddata_t *th = gasnetc_mythread();

  GASNETI_COMMON_AMREQUESTSHORT(dest,handler,numargs);

  /* poll until ok to send message, allocate ReqSB chunk */
  GASNETC_COMMON_AMREQ_START(state,local_offset,th,1);

  /* get the addr of the start of the chunk */
  data = (uint8_t*)gasnetc_ReqSB.start + local_offset;

  /* construct the match bits */
  GASNETC_PACK_AM_MBITS(mbits,local_offset,numargs,handler,amtype,targ_mbits);

  va_start(argptr, numargs); /*  pass in last argument */

  /* pack first two args in hdr_data */
  if (numargs > 0) garg0 = va_arg(argptr,gasnet_handlerarg_t);
  if (numargs > 1) garg1 = va_arg(argptr,gasnet_handlerarg_t);
  GASNETC_PACK_2INT(hdr_data,garg0,garg1);

  /* pack remaining args in data payload */
  for (i=2; i < numargs; i++) {
    garg0 = va_arg(argptr,gasnet_handlerarg_t);
    memcpy(data,&garg0,sizeof(gasnet_handlerarg_t));
    data += sizeof(gasnet_handlerarg_t);
    msg_bytes += sizeof(gasnet_handlerarg_t);
  }
  va_end(argptr);
  
  /* pad message length to be multiple of sizeof(double) */
  GASNETC_COMPUTE_DOUBLE_PAD(msg_bytes,pad);
  msg_bytes += pad;

  GASNETI_TRACE_PRINTF(C,("AMReq_Short to %d with pad = %d msg_bytes=%d",dest,pad,(int)msg_bytes));
  gasneti_assert( (msg_bytes % sizeof(double)) == 0 );

  gasneti_assert(th->snd_tickets);
  th->snd_tickets--;

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
  ptl_process_id_t   target_id = gasnetc_procid_map[dest].ptl_id;
  ptl_ac_index_t     ac_index = GASNETC_PTL_AC_ID;
  uint64_t           amtype = GASNETC_PTL_AM_MEDIUM | GASNETC_PTL_AM_REQUEST;
  uint64_t           targ_mbits = GASNETC_PTL_REQRB_BITS  | GASNETC_PTL_MSG_AM;
  gasnet_handlerarg_t garg0 = 0;
  gasnet_handlerarg_t garg1 = 0;
  ptl_match_bits_t   mbits;
  ptl_hdr_data_t     hdr_data;
  gasnetc_conn_t    *state = gasnetc_conn_state + dest;
  int                i, pad1, pad2;
  int                msg_bytes = 0;
  uint32_t           hndlr_bytes = nbytes;  /* this will fit for medium message */
  uint8_t           *data;
  gasnetc_threaddata_t *th = gasnetc_mythread();

  GASNETI_COMMON_AMREQUESTMEDIUM(dest,handler,source_addr,nbytes,numargs);

  /* poll until ok to send message, allocate ReqSB chunk */
  GASNETC_COMMON_AMREQ_START(state,local_offset,th,1);

  /* get the addr of the start of the chunk */
  data = (uint8_t*)gasnetc_ReqSB.start + local_offset;
  gasneti_assert( ((intptr_t)data % sizeof(double)) == 0 );

  /* construct the match bits */
  GASNETC_PACK_AM_MBITS(mbits,local_offset,numargs,handler,amtype,targ_mbits);

  va_start(argptr, numargs); /*  pass in last argument */

  /* pack first two args in hdr_data */
  if (numargs > 0) garg0 = va_arg(argptr,gasnet_handlerarg_t);
  if (numargs > 1) garg1 = va_arg(argptr,gasnet_handlerarg_t);
  GASNETC_PACK_2INT(hdr_data,garg0,garg1);

  /* pack remaining args in data payload */
  for (i=2; i < numargs; i++) {
    garg0 = va_arg(argptr,gasnet_handlerarg_t);
    memcpy(data,&garg0,sizeof(gasnet_handlerarg_t));
    data += sizeof(gasnet_handlerarg_t);
    msg_bytes += sizeof(gasnet_handlerarg_t);
  }
  va_end(argptr);

  /* pack handler payload length */
  memcpy(data,&hndlr_bytes,sizeof(uint32_t));
  data += sizeof(uint32_t);
  msg_bytes += sizeof(uint32_t);

  /* pad so that data payload starts on double-aligned memory */
  GASNETC_COMPUTE_DOUBLE_PAD(msg_bytes,pad1);
  data += pad1;
  msg_bytes += pad1;

  /* copy data payload */
  memcpy(data,source_addr,nbytes);
  msg_bytes += nbytes;

  /* pad so that entire payload is multiple of double */
  GASNETC_COMPUTE_DOUBLE_PAD(msg_bytes,pad2);
  msg_bytes += pad2;

  GASNETI_TRACE_PRINTF(C,("AMReq_Medium: pad1=%d, pad2=%d, msg_bytes=%d",pad1,pad2,msg_bytes));

  gasneti_assert( (msg_bytes % sizeof(double)) == 0 );
  gasneti_assert(msg_bytes <= GASNETC_CHUNKSIZE);

  gasneti_assert(th->snd_tickets);
  th->snd_tickets--;

  /* send message */
  GASNETC_PTLSAFE(PtlPutRegion(md_h, local_offset, msg_bytes, PTL_NOACK_REQ, target_id, GASNETC_PTL_AM_PTE, ac_index, mbits, remote_offset, hdr_data));

  gasneti_weakatomic_increment(&state->AM_pending,0);

  GASNETI_RETURN(GASNET_OK);
}

/* Factored code common to both AMRequestLong and AMRequestAsync */
#define AM_LONG_COMMON(do_sync,isPacked,th) do {			\
    ptl_size_t           remote_offset = 0;				\
    ptl_handle_md_t      md_h = gasnetc_ReqSB.md_h;			\
    ptl_process_id_t     target_id = gasnetc_procid_map[dest].ptl_id;	\
    ptl_ac_index_t       ac_index = GASNETC_PTL_AC_ID;			\
    uint64_t             amtype = GASNETC_PTL_AM_LONG | GASNETC_PTL_AM_REQUEST;	\
    uint64_t             targ_mbits = GASNETC_PTL_REQRB_BITS  | GASNETC_PTL_MSG_AM; \
    gasnet_handlerarg_t  garg0 = 0;					\
    uint32_t             lid;						\
    ptl_match_bits_t     mbits;						\
    ptl_hdr_data_t       hdr_data;					\
    int                  i, pad;					\
    int                  msg_bytes = 0;					\
    uint8_t             *data;						\
									\
    if (isPacked) {							\
      /* pack message length in place of lid, will fit in 32 bits */	\
      lid = nbytes;							\
      amtype |= GASNETC_PTL_AM_PACKED;					\
    } else {								\
      lid = gasnetc_new_lid(dest);					\
    }									\
    /* construct the match bits */					\
    GASNETC_PACK_AM_MBITS(mbits,local_offset,numargs,handler,amtype,targ_mbits); \
									\
    /* get the addr of the start of the chunk */			\
    data = (uint8_t*)gasnetc_ReqSB.start + local_offset;		\
    gasneti_assert( ((intptr_t)data % sizeof(double)) == 0 );		\
									\
    va_start(argptr, numargs);						\
    if (numargs > 0) garg0 = va_arg(argptr,gasnet_handlerarg_t);	\
									\
    GASNETC_PACK_2INT(hdr_data,garg0,lid);				\
									\
    /* pack remaining args in data payload */				\
    for (i=1; i < numargs; i++) {					\
      garg0 = va_arg(argptr,gasnet_handlerarg_t);			\
      memcpy(data,&garg0,sizeof(gasnet_handlerarg_t));			\
      data += sizeof(gasnet_handlerarg_t);				\
      msg_bytes += sizeof(gasnet_handlerarg_t);				\
    }									\
    va_end(argptr);							\
									\
    if (isPacked) {							\
									\
      /* pack the target node RAR destination address for this message */ \
      memcpy(data,&dest_addr,sizeof(void*));				\
      data += sizeof(void*);						\
      msg_bytes += sizeof(void*);					\
									\
      /* copy the data payload */					\
      memcpy(data,source_addr,nbytes);					\
      data += nbytes;							\
      msg_bytes += nbytes;						\
									\
      /* pad so that message length is multiple of double */		\
      GASNETC_COMPUTE_DOUBLE_PAD(msg_bytes,pad);			\
      msg_bytes += pad;							\
									\
      gasneti_assert( (msg_bytes % sizeof(double)) == 0 );		\
      gasneti_assert(msg_bytes <= GASNETC_CHUNKSIZE);			\
									\
      gasneti_assert(th->snd_tickets > 0);				\
      th->snd_tickets--;						\
									\
      /* send message */						\
      GASNETI_TRACE_PRINTF(C,("AMLong PACKED Sync=%d to node=%d nbytes=%d",(int)do_sync,(int)dest,(int)msg_bytes)); \
      GASNETC_PTLSAFE(PtlPutRegion(md_h, local_offset, msg_bytes, PTL_NOACK_REQ, target_id, GASNETC_PTL_AM_PTE, ac_index, mbits, remote_offset, hdr_data)); \
									\
    } else {								\
      int is_req = 1;							\
									\
      gasneti_assert(th->snd_tickets > 1);				\
      /* first issue data put message and request sync */		\
      gasnetc_amlong_datasend(do_sync,is_req,lid,dest,source_addr,nbytes,dest_addr); \
									\
      /* now complete header message */					\
      /* pad so that message length is multiple of double */		\
      GASNETC_COMPUTE_DOUBLE_PAD(msg_bytes,pad);			\
      msg_bytes += pad;							\
									\
      gasneti_assert( (msg_bytes % sizeof(double)) == 0 );		\
      gasneti_assert(msg_bytes <= GASNETC_CHUNKSIZE);			\
      th->snd_tickets--;						\
									\
      /* send message */						\
      GASNETI_TRACE_PRINTF(C,("AMLong Header Sync=%d to node=%d lid=%d nbytes=%d",(int)do_sync,(int)dest,lid,(int)msg_bytes)); \
      GASNETC_PTLSAFE(PtlPutRegion(md_h, local_offset, msg_bytes, PTL_NOACK_REQ, target_id, GASNETC_PTL_AM_PTE, ac_index, mbits, remote_offset, hdr_data)); \
									\
      /* now wait for data put to complete locally */			\
      if (do_sync) {							\
	gasneti_pollwhile( gasneti_weakatomic_read(&th->amlong_data_inflight, 0) > 0 ); \
      }									\
    }									\
  } while(0)

extern int gasnetc_AMRequestLongM( gasnet_node_t dest,        /* destination node */
                            gasnet_handler_t handler, /* index into destination endpoint's handler table */ 
                            void *source_addr, size_t nbytes,   /* data payload */
                            void *dest_addr,                    /* data destination on destination node */
                            int numargs, ...) {
  va_list              argptr;
  ptl_size_t           local_offset;
  gasnetc_conn_t      *state = gasnetc_conn_state + dest;
  int                  do_sync = 1;
  int                  isPacked = (nbytes < GASNETC_MAX_AMLONG_PACKED);
  int                  nsend = (isPacked ? 1 : 2);
  gasnetc_threaddata_t *th = gasnetc_mythread();

  GASNETI_COMMON_AMREQUESTLONG(dest,handler,source_addr,nbytes,dest_addr,numargs);

  /* poll until ok to send message, allocate ReqSB chunk */
  GASNETC_COMMON_AMREQ_START(state,local_offset,th,nsend);

  /* do all the work in sending the messages(s) */
  AM_LONG_COMMON(do_sync,isPacked,th);
  
  gasneti_weakatomic_increment(&state->AM_pending,0);

  GASNETI_RETURN(GASNET_OK);
}
  

extern int gasnetc_AMRequestLongAsyncM( gasnet_node_t dest,        /* destination node */
                            gasnet_handler_t handler, /* index into destination endpoint's handler table */ 
                            void *source_addr, size_t nbytes,   /* data payload */
                            void *dest_addr,                    /* data destination on destination node */
                            int numargs, ...) {
  va_list              argptr;
  ptl_size_t           local_offset;
  gasnetc_conn_t      *state = gasnetc_conn_state + dest;
  int                  do_sync = 0;
  int                  isPacked = (nbytes < GASNETC_MAX_AMLONG_PACKED);
  int                  nsend = (isPacked ? 1 : 2);
  gasnetc_threaddata_t *th = gasnetc_mythread();

  GASNETI_COMMON_AMREQUESTLONGASYNC(dest,handler,source_addr,nbytes,dest_addr,numargs);

  /* poll until ok to send message, allocate ReqSB chunk */
  GASNETC_COMMON_AMREQ_START(state,local_offset,th,nsend);

  /* do all the work in sending the messages(s) */
  AM_LONG_COMMON(do_sync,isPacked,th);
  
  gasneti_weakatomic_increment(&state->AM_pending,0);

  GASNETI_RETURN(GASNET_OK);
}

#undef AM_LONG_COMMON

extern int gasnetc_AMReplyShortM( 
                            gasnet_token_t token,       /* token provided on handler entry */
                            gasnet_handler_t handler, /* index into destination endpoint's handler table */ 
                            int numargs, ...) {
  int retval;
  va_list argptr; 
  gasnetc_ptl_token_t *ptok = (gasnetc_ptl_token_t*)token;
  ptl_size_t           local_offset = ptok->rplsb_offset;
  ptl_size_t           remote_offset = ptok->initiator_offset;
  ptl_handle_md_t      md_h = gasnetc_RplSB.md_h;
  ptl_process_id_t     target_id = ptok->initiator;
  ptl_ac_index_t       ac_index = GASNETC_PTL_AC_ID;
  uint64_t             amtype = GASNETC_PTL_AM_SHORT;
  uint64_t             targ_mbits = GASNETC_PTL_REQSB_BITS | GASNETC_PTL_MSG_AM;
  gasnet_handlerarg_t  garg0 = 0;
  gasnet_handlerarg_t  garg1 = 0;
  ptl_match_bits_t     mbits;
  ptl_hdr_data_t       hdr_data = 0;
  ptl_size_t           msg_bytes = 0;
  uint8_t             *data = (uint8_t*)gasnetc_RplSB.start + local_offset;
  int                  i, nstart;
  gasnetc_threaddata_t *th = gasnetc_mythread();

  GASNETI_COMMON_AMREPLYSHORT(token,handler,numargs);

  va_start(argptr, numargs); /*  pass in last argument */

  /* pack first two args in hdr_data */
  if (numargs > 0) garg0 = va_arg(argptr,gasnet_handlerarg_t);
  if (numargs > 1) garg1 = va_arg(argptr,gasnet_handlerarg_t);
  GASNETC_PACK_2INT(hdr_data,garg0,garg1);

#if GASNETC_PACK_RPLOFF_MBITS
  /* The rplsb offset goes in the upper portion of the match bits */
  GASNETC_PACK_AM_MBITS(mbits,local_offset,numargs,handler,amtype,targ_mbits);
  nstart = 2;
#else
  /* The third arg, if it exists, goes in the upper portion of the match bits */
  if (numargs > 2) garg0 = va_arg(argptr,gasnet_handlerarg_t);
  GASNETC_PACK_AM_MBITS(mbits,garg0,numargs,handler,amtype,targ_mbits);
  nstart = 3;
#endif

  /* pack remaining args in data payload */
  for (i=nstart; i < numargs; i++) {
    garg0 = va_arg(argptr,gasnet_handlerarg_t);
    memcpy(data,&garg0,sizeof(gasnet_handlerarg_t));
    data += sizeof(gasnet_handlerarg_t);
    msg_bytes += sizeof(gasnet_handlerarg_t);
  }
  va_end(argptr);

  /* already allocated a send ticket */
  gasneti_assert(th->snd_tickets > 0);
  th->snd_tickets--;

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
  gasnetc_ptl_token_t  *ptok = (gasnetc_ptl_token_t*)token;
  ptl_size_t            local_offset = ptok->rplsb_offset;
  ptl_size_t            remote_offset = ptok->initiator_offset;
  ptl_handle_md_t       md_h = gasnetc_RplSB.md_h;
  ptl_process_id_t      target_id = ptok->initiator;
  ptl_ac_index_t        ac_index = GASNETC_PTL_AC_ID;
  uint64_t              amtype = GASNETC_PTL_AM_MEDIUM;
  uint64_t              targ_mbits = GASNETC_PTL_REQSB_BITS | GASNETC_PTL_MSG_AM;
  gasnet_handlerarg_t   garg0 = 0;
  gasnet_handlerarg_t   garg1 = 0;
  ptl_match_bits_t      mbits;
  ptl_hdr_data_t        hdr_data = 0;
  ptl_size_t            msg_bytes = 0;
  uint32_t              payload_bytes = nbytes;
  uint8_t              *data = (uint8_t*)gasnetc_RplSB.start + local_offset;
  int                   i, nstart, pad;
  gasnetc_threaddata_t *th = gasnetc_mythread();

  GASNETI_COMMON_AMREPLYMEDIUM(token,handler,source_addr,nbytes,numargs);

  va_start(argptr, numargs); /*  pass in last argument */

  /* pack first two args in hdr_data */
  if (numargs > 0) garg0 = va_arg(argptr,gasnet_handlerarg_t);
  if (numargs > 1) garg1 = va_arg(argptr,gasnet_handlerarg_t);
  GASNETC_PACK_2INT(hdr_data,garg0,garg1);

#if GASNETC_PACK_RPLOFF_MBITS
  /* The rplsb offset goes in the upper portion of the match bits */
  GASNETC_PACK_AM_MBITS(mbits,local_offset,numargs,handler,amtype,targ_mbits);
  nstart = 2;
#else
  if (numargs > 2) garg0 = va_arg(argptr,gasnet_handlerarg_t);
  GASNETC_PACK_AM_MBITS(mbits,garg0,numargs,handler,amtype,targ_mbits);
  nstart = 3;
#endif

  /* pack remaining args in data payload */
  for (i=nstart; i < numargs; i++) {
    garg0 = va_arg(argptr,gasnet_handlerarg_t);
    memcpy(data,&garg0,sizeof(gasnet_handlerarg_t));
    data += sizeof(gasnet_handlerarg_t);
    msg_bytes += sizeof(gasnet_handlerarg_t);
  }
  va_end(argptr);

  /* pack handler payload length */
  memcpy(data,&payload_bytes,sizeof(uint32_t));
  data += sizeof(uint32_t);
  msg_bytes += sizeof(uint32_t);
  
  /* pad to that data payload is double aligned */
  GASNETC_COMPUTE_DOUBLE_PAD(msg_bytes,pad);
  data += pad;
  msg_bytes += pad;

  /* copy handler data payload here */
  memcpy(data,source_addr,nbytes);
  msg_bytes += nbytes;

  GASNETI_TRACE_PRINTF(C,("AMReply_Medium to %d msg_bytes=%d pad=%d",ptok->srcnode,(int)msg_bytes,pad));

  /* already allocated a send ticket */
  gasneti_assert(th->snd_tickets > 0);
  th->snd_tickets--;

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
  gasnetc_ptl_token_t  *ptok = (gasnetc_ptl_token_t*)token;
  ptl_size_t            local_offset = ptok->rplsb_offset;
  ptl_size_t            remote_offset = ptok->initiator_offset;
  ptl_handle_md_t       md_h = gasnetc_RplSB.md_h;
  ptl_process_id_t      target_id = ptok->initiator;
  ptl_ac_index_t        ac_index = GASNETC_PTL_AC_ID;
  uint64_t              amtype = GASNETC_PTL_AM_LONG;
  uint64_t              targ_mbits = GASNETC_PTL_REQSB_BITS | GASNETC_PTL_MSG_AM;
  gasnet_handlerarg_t   garg = 0;
  uint32_t              lid;
  int                   isPacked = (nbytes < GASNETC_MAX_AMLONG_PACKED);
  ptl_match_bits_t      mbits;
  ptl_hdr_data_t        hdr_data = 0;
  ptl_size_t            msg_bytes = 0;
  int32_t               payload_bytes = nbytes;
  uint8_t              *data = (uint8_t*)gasnetc_RplSB.start + local_offset;
  int                   i, nstart, pad;
  gasnet_node_t         dest = ptok->srcnode;
  gasnetc_threaddata_t *th = gasnetc_mythread();

  GASNETI_COMMON_AMREPLYLONG(token,handler,source_addr,nbytes,dest_addr,numargs); 

  if (isPacked) amtype |= GASNETC_PTL_AM_PACKED;

  va_start(argptr, numargs); /*  pass in last argument */
  if (numargs > 0) garg = va_arg(argptr,gasnet_handlerarg_t);

  if (isPacked) {
    /* pack message length in place of lid */
    lid = nbytes;
  } else {
    lid = gasnetc_new_lid(dest);
  }
  GASNETC_PACK_2INT(hdr_data,garg,lid);

  /* pack second arg in upper bits of mbits */
#if GASNETC_PACK_RPLOFF_MBITS
  GASNETC_PACK_AM_MBITS(mbits,local_offset,numargs,handler,amtype,targ_mbits);
  nstart = 1;
#else
  if (numargs > 1) garg = va_arg(argptr,gasnet_handlerarg_t);
  GASNETC_PACK_AM_MBITS(mbits,garg,numargs,handler,amtype,targ_mbits);
  nstart = 2;
#endif

  /* pack remaining args in data payload */
  for (i=nstart; i < numargs; i++) {
    garg = va_arg(argptr,gasnet_handlerarg_t);
    memcpy(data,&garg,sizeof(gasnet_handlerarg_t));
    data += sizeof(gasnet_handlerarg_t);
    msg_bytes += sizeof(gasnet_handlerarg_t);
  }
  va_end(argptr);

  if (isPacked) {

    /* pack the target node RAR destination address for this message */
    memcpy(data,&dest_addr,sizeof(void*));
    data += sizeof(void*);
    msg_bytes += sizeof(void*);

    /* copy the data payload */
    memcpy(data,source_addr,nbytes);
    data += nbytes;
    msg_bytes += nbytes;

    gasneti_assert(msg_bytes <= GASNETC_CHUNKSIZE);

    /* already allocated a send ticket */
    gasneti_assert(th->snd_tickets > 0);
    th->snd_tickets--;

    /* send message */
    GASNETC_PTLSAFE(PtlPutRegion(md_h, local_offset, msg_bytes, PTL_NOACK_REQ, target_id, GASNETC_PTL_AM_PTE, ac_index, mbits, remote_offset, hdr_data));
    
  } else {

    /* AM Reply is always executed while polling, need to wait until data payload is
     * off-node before returning, but dont want to poll recursively.
     * Alloc tmp eq and md to cover src region, issue Put, then poll only on this
     * tmp eq for Put local completion.
     * NOTE: May be able to relax this and poll over other EQs as well.
     * NOTE: Data Reply sent to RARSRC MD !!!
     */
    int dp_eq_len = 2;
    ptl_handle_md_t dp_md_h;
    ptl_handle_eq_t dp_eq_h;
    ptl_event_t ev;
    ptl_match_bits_t dp_mbits = GASNETC_PTL_MSG_AMDATA | GASNETC_PTL_RARSRC_BITS;
    ptl_size_t remote_dataoffset = GASNETC_PTL_OFFSET(dest,dest_addr);
    ptl_hdr_data_t dp_hdr_data = (ptl_hdr_data_t) lid;

    /* already allocated a send ticket, spend them now */
    gasneti_assert(th->snd_tickets > 1);
    th->snd_tickets -= 2;

    /* alloc a short eq and a tmpmd to cover source region */
    GASNETC_PTLSAFE(PtlEQAlloc(gasnetc_ni_h, dp_eq_len, NULL, &dp_eq_h));
    gasneti_assert(th->flags & GASNETC_THREAD_HAVE_TMPMD);
    dp_md_h = gasnetc_alloc_tmpmd(source_addr,nbytes,dp_eq_h);

    /* issue data put message */
    /* NOTE: dp_mbits is explicitly does not include the REQUEST flag, since this is a reply */
    GASNETC_PTLSAFE(PtlPut(dp_md_h,PTL_NOACK_REQ,target_id,GASNETC_PTL_RAR_PTE, ac_index, dp_mbits, remote_dataoffset, dp_hdr_data));

    /* now complete header message */
    gasneti_assert(msg_bytes <= GASNETC_CHUNKSIZE);


    /* send message */
    GASNETC_PTLSAFE(PtlPutRegion(md_h, local_offset, msg_bytes, PTL_NOACK_REQ, target_id, GASNETC_PTL_AM_PTE, ac_index, mbits, remote_offset, hdr_data));

    while( !gasnetc_get_event(dp_eq_h, &ev) ) {
      gasnetc_portals_poll(GASNETC_SAFE_POLL);
    }
    if (ev.type != PTL_EVENT_SEND_END) {
      gasneti_fatalerror("gasnetc_AMReplyLong: got %s event on tmp EQ at %s",ptl_event_str[ev.type],gasneti_current_loc);
    }
    /* return the send ticket from the data payload message */
    if (gasnetc_msg_limit) gasnetc_return_ticket(&gasnetc_send_tickets);
    gasnetc_free_tmpmd(dp_md_h);
    GASNETC_PTLSAFE(PtlEQFree(dp_eq_h));
  }

  /* Indicate to reply code that AM did in fact send a reply message */
  ptok->flags |= GASNETC_PTL_REPLY_SENT;

  GASNETI_RETURN(GASNET_OK);
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
    gasneti_handler_tableentry_with_bits(gasnetc_noop_reph),

  { 0, NULL }
};

gasnet_handlerentry_t const *gasnetc_get_handlertable() {
  return gasnetc_handlers;
}

/* ------------------------------------------------------------------------------------ */

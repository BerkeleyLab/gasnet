/* $Id: gasnet_core.c,v 1.52 2004/01/28 16:35:00 phargrov Exp $
 * $Date: 2004/01/28 16:35:00 $
 * Description: GASNet GM conduit Implementation
 * Copyright 2002, Christian Bell <csbell@cs.berkeley.edu>
 * Copyright 2002, Dan Bonachea <bonachea@cs.berkeley.edu>
 * Terms of use are as specified in license.txt
 */

#include <gasnet.h>
#include <gasnet_internal.h>
#include <gasnet_handler.h>
#include <gasnet_core_internal.h>
#include <firehose.h>

#include <errno.h>
#include <unistd.h>
#include <sched.h>
#include <signal.h>

GASNETI_IDENT(gasnetc_IdentString_Version, 
		"$GASNetCoreLibraryVersion: " GASNET_CORE_VERSION_STR " $");
GASNETI_IDENT(gasnetc_IdentString_ConduitName, 
		"$GASNetConduitName: " GASNET_CORE_NAME_STR " $");

gasnet_node_t	gasnetc_mynode = (gasnet_node_t)-1;
gasnet_node_t	gasnetc_nodes = 0;
uintptr_t	gasnetc_MaxLocalSegmentSize = 0;
uintptr_t	gasnetc_MaxGlobalSegmentSize = 0;

gasnet_seginfo_t *gasnetc_seginfo = NULL;
firehose_info_t	  gasnetc_firehose_info;

gasneti_mutex_t gasnetc_lock_gm      = GASNETI_MUTEX_INITIALIZER;
gasneti_mutex_t gasnetc_lock_reqpool = GASNETI_MUTEX_INITIALIZER;
gasneti_mutex_t gasnetc_lock_amreq   = GASNETI_MUTEX_INITIALIZER;

gasnetc_state_t _gmc;

gasnet_handlerentry_t const		*gasnetc_get_handlertable();
extern gasnet_handlerentry_t const	*gasnete_get_handlertable();
extern gasnet_handlerentry_t const	*gasnete_get_extref_handlertable();

/*
  Initialization
  ==============
*/
/* called at startup to check configuration sanity */
static void gasnetc_check_config() {
  gasneti_assert(gm_min_size_for_length(GASNETC_AM_MEDIUM_MAX) <= GASNETC_AM_SIZE);
  gasneti_assert(gm_min_size_for_length(GASNETC_AM_LONG_REPLY_MAX) <= GASNETC_AM_SIZE);
  gasneti_assert(gm_max_length_for_size(GASNETC_AM_SIZE) <= GASNETC_AM_PACKET);
  gasneti_assert(GASNETC_AM_MEDIUM_MAX <= (uint16_t)(-1));
  gasneti_assert(GASNETC_AM_MAX_HANDLERS >= 256);
  return;
}

static int 
gasnetc_init(int *argc, char ***argv)
{
	/* check system sanity */
	gasnetc_check_config();

	if (gasneti_init_done) 
		GASNETI_RETURN_ERRR(NOT_INIT, "GASNet already initialized");

        if (getenv("GASNET_FREEZE")) gasneti_freezeForDebugger();

	#if GASNET_DEBUG_VERBOSE
	/* note - can't call trace macros during gasnet_init because trace
	 * system not yet initialized */
	fprintf(stderr,"gasnetc_init(): about to spawn...\n"); fflush(stderr);
	#endif

	gasnetc_getconf();
	gasnetc_AllocPinnedBufs();

	gasnetc_bootstrapBarrier();
	gasnetc_bootstrapBarrier();
	gasnetc_bootstrapBarrier();
	gasnetc_bootstrapBarrier();

	/* Find the upper bound on pinnable memory for firehose algorithm and
	 * segment fast */
	{
		int	i;
		uintptr_t *global_exch = (uintptr_t *)
		    gasneti_malloc(gasnetc_nodes*sizeof(uintptr_t));

		_gmc.pinnable_local =
			gasnetc_getPhysMem() * GASNETC_PHYSMEM_PINNABLE_RATIO;
		_gmc.pinnable_global = (uintptr_t) -1;

		gasnetc_bootstrapExchange(&_gmc.pinnable_local, 
		    sizeof(uintptr_t), global_exch);

		for (i = 0; i < gasnetc_nodes; i++) 
			_gmc.pinnable_global = MIN(_gmc.pinnable_global, 
						   global_exch[i]);
		gasneti_free(global_exch);
	}

	#if GASNET_DEBUG_VERBOSE
	printf("%d> done firehose exchange\n", gasnetc_mynode);
	#endif


	#if defined(GASNET_SEGMENT_FAST) 
	gasneti_segmentInit(
	    &gasnetc_MaxLocalSegmentSize,
	    &gasnetc_MaxGlobalSegmentSize,
	    _gmc.pinnable_global,
            gasnetc_nodes,
            &gasnetc_bootstrapExchange);

	#elif defined(GASNET_SEGMENT_LARGE)
	gasneti_segmentInit(
	    &gasnetc_MaxLocalSegmentSize,
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

	gasneti_init_done = 1;
	gasneti_trace_init();

	#if GASNET_DEBUG_VERBOSE
	printf("%d> done init\n", gasnetc_mynode);
	fflush(stdout);
	#endif
	return GASNET_OK;
}

extern uintptr_t gasnetc_getMaxLocalSegmentSize() 
{
	GASNETI_CHECKINIT();
	return gasnetc_MaxLocalSegmentSize;
}

extern uintptr_t gasnetc_getMaxGlobalSegmentSize() 
{
	GASNETI_CHECKINIT();
	return gasnetc_MaxGlobalSegmentSize;
}
/* ------------------------------------------------------------------------------------ */
static char checkuniqhandler[256] = { 0 };
void
gasnetc_AM_InitHandler()
{
	int	i;

	for (i = 0; i < GASNETC_AM_MAX_HANDLERS; i++) 
		_gmc.handlers[i] = (gasnetc_handler_fn_t) abort;  

	return;
}

int
gasnetc_AM_SetHandler(gasnet_handler_t handler, gasnetc_handler_fn_t func)
{
	if (!handler || func == NULL)
		GASNETI_RETURN_ERRR(BAD_ARG, "Invalid handler paramaters set");
		
	_gmc.handlers[handler] = func;
	return GASNET_OK;
}

int
gasnetc_AM_SetHandlerAny(gasnet_handler_t *handler, gasnetc_handler_fn_t func)
{
	int	i;

	if (handler == NULL || func == NULL)
		GASNETI_RETURN_ERRR(BAD_ARG, "Invalid handler paramaters set");

	for (i = 1; i < GASNETC_AM_MAX_HANDLERS; i++) {
		if (_gmc.handlers[i] == abort) {
			_gmc.handlers[i] = func;
			*handler = i;
			return GASNET_OK;
		}
	}
	return GASNET_OK;
}

static int 
gasnetc_reghandlers(gasnet_handlerentry_t *table, int numentries,
                               int lowlimit, int highlimit,
                               int dontcare, int *numregistered) {
  int i, retval;
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
    retval = gasnetc_AM_SetHandler((gasnet_handler_t) newindex, table[i].fnptr);
    if (retval != GASNET_OK)
        GASNETI_RETURN_ERRR(RESOURCE, "AM_SetHandler() failed while registering core handlers");

    if (dontcare) table[i].index = newindex;
    (*numregistered)++;
  }
  return GASNET_OK;
}
/* ------------------------------------------------------------------------------------ */
extern int 
gasnetc_attach(gasnet_handlerentry_t *table, int numentries, uintptr_t segsize,
	       uintptr_t minheapoffset)
{
	int i = 0, fidx = 0;

	#if GASNET_DEBUG_VERBOSE
	printf("%d> starting attach\n", gasnetc_mynode);
	fflush(stdout);
	#endif
	GASNETI_TRACE_PRINTF(C,
	    ("gasnetc_attach(table (%i entries), segsize=%lu, minheapoffset=%lu)",
	    numentries, (unsigned long)segsize, (unsigned long)minheapoffset));

	if (!gasneti_init_done) 
		GASNETI_RETURN_ERRR(NOT_INIT,
		    "GASNet attach called before init");
	if (gasneti_attach_done) 
		GASNETI_RETURN_ERRR(NOT_INIT, "GASNet already attached");

        #if GASNET_SEGMENT_FAST || GASNET_SEGMENT_LARGE
	if ((segsize % GASNET_PAGESIZE) != 0) 
		GASNETI_RETURN_ERRR(BAD_ARG, "segsize not page-aligned");
	if (segsize > gasnetc_getMaxLocalSegmentSize()) 
		GASNETI_RETURN_ERRR(BAD_ARG, "segsize too large");
	minheapoffset = 
	    GASNETI_ALIGNUP(minheapoffset, GASNETC_SEGMENT_ALIGN);
	#else
	segsize = 0;
	minheapoffset = 0;
	#endif
	/*  register handlers */
	gasnetc_AM_InitHandler();

	{ /*  core API handlers */
		gasnet_handlerentry_t *ctable = 
		    (gasnet_handlerentry_t *) gasnetc_get_handlertable();

		int c_len = 0;
		int c_numreg = 0;

		gasneti_assert(ctable);
		while (ctable[c_len].fnptr) c_len++; /* calc len */
		if (gasnetc_reghandlers(ctable, c_len, 1, 63, 0, &c_numreg)
		    != GASNET_OK)
			GASNETI_RETURN_ERRR(RESOURCE,
			    "Error registering core API handlers");
		gasneti_assert(c_numreg == c_len);
	}
	{ /*  extended API handlers */
		gasnet_handlerentry_t *ertable = 
		    (gasnet_handlerentry_t *)gasnete_get_extref_handlertable();
		gasnet_handlerentry_t *etable = 
		    (gasnet_handlerentry_t *)gasnete_get_handlertable();
		int er_len = 0, e_len = 0;
		int er_numreg = 0, e_numreg = 0;
		gasneti_assert(etable && ertable);
	
		while (ertable[er_len].fnptr) er_len++; /* calc len */
		while (etable[e_len].fnptr) e_len++; /* calc len */
		if (gasnetc_reghandlers(ertable, er_len, 64, 127, 0, 
		    &er_numreg) != GASNET_OK)
			GASNETI_RETURN_ERRR(RESOURCE,
			    "Error registering extended reference API handlers");
	    	gasneti_assert(er_numreg == er_len);
	
		if (gasnetc_reghandlers(etable, e_len, 64+er_len, 127, 0, 
		    &e_numreg) != GASNET_OK)
			GASNETI_RETURN_ERRR(RESOURCE,
			    "Error registering extended API handlers");
	    	gasneti_assert(e_numreg == e_len);
		fidx = 64+er_len+e_len;
	}
	{ /* firehose handlers */
		gasnet_handlerentry_t *ftable = firehose_get_handlertable();
		int f_len = 0;
		int f_numreg = 0;

		gasneti_assert(ftable);

		while (ftable[f_len].fnptr)
			f_len++;

		gasneti_assert(fidx + f_len <= 128);
		if (gasnetc_reghandlers(ftable, f_len, fidx, 127, 1, &f_numreg)
		    != GASNET_OK)
			GASNETI_RETURN_ERRR(RESOURCE,
			    "Error registering firehose handlers");
		gasneti_assert(f_numreg == f_len);
	}

	if (table) { /*  client handlers */
		int numreg1 = 0;
		int numreg2 = 0;

		/*  first pass - assign all fixed-index handlers */
		if (gasnetc_reghandlers(table, numentries, 128, 255, 0, &numreg1) 
		    != GASNET_OK)
			GASNETI_RETURN_ERRR(RESOURCE,
			    "Error registering fixed-index client handlers");

		/*  second pass - fill in dontcare-index handlers */
		if (gasnetc_reghandlers(table, numentries, 128, 255, 1, &numreg2) 
		    != GASNET_OK)
			GASNETI_RETURN_ERRR(RESOURCE,
			    "Error registering fixed-index client handlers");

		gasneti_assert(numreg1 + numreg2 == numentries);
	}

	/* -------------------------------------------------------------------- */
	/*  register fatal signal handlers */

	/*  catch fatal signals and convert to SIGQUIT */
	gasneti_registerSignalHandlers(gasneti_defaultSignalHandler);

	/* -------------------------------------------------------------------- */
	/*  register segment  */

	gasnetc_seginfo = (gasnet_seginfo_t *)
	    gasneti_calloc(gasnetc_nodes, sizeof(gasnet_seginfo_t));

	#if GASNET_DEBUG_VERBOSE
	printf("%d> before firehose init\n", gasnetc_mynode);
	fflush(stdout);
	#endif
	#if defined(GASNET_SEGMENT_FAST) 
	{
		firehose_region_t	prereg, *preg = NULL;
		int			pnum = 0;

		if (segsize > 0) {
			gasneti_segmentAttach(segsize, minheapoffset,
					gasnetc_seginfo,
					&gasnetc_bootstrapExchange);

			prereg.addr = (uintptr_t) 
				gasnetc_seginfo[gasnetc_mynode].addr;
			prereg.len = gasnetc_seginfo[gasnetc_mynode].size;

			if (gm_register_memory(_gmc.port, 
			    (void *) prereg.addr, prereg.len) != GM_SUCCESS)
				gasneti_fatalerror(
				    "Can't pin FAST Segment of %.2f MB", 
				    (float) prereg.len / (1024.0*1024.0));
			pnum++;
			preg = &prereg;
		}

		firehose_init(_gmc.pinnable_global, 0, preg, pnum,
					&gasnetc_firehose_info);
	}
	#else /* GASNET_SEGMENT_EVERYTHING | GASNET_SEGMENT_LARGE */
	{
		#if defined(GASNET_SEGMENT_LARGE)
		if (segsize > 0) {
			gasneti_segmentAttach(segsize, minheapoffset, 
			    gasnetc_seginfo, &gasnetc_bootstrapExchange);
		}
		#else
		    for (i=0;i<gasnetc_nodes;i++) {
			gasnetc_seginfo[i].addr = (void *)0;
			gasnetc_seginfo[i].size = (uintptr_t)-1;
		    }
		#endif

		firehose_init(_gmc.pinnable_global, 0, NULL, 0,
			&gasnetc_firehose_info);
	}
	#endif

        #if GASNET_TRACE
	for (i = 0; i < gasnetc_nodes; i++)
		GASNETI_TRACE_PRINTF(C, ("SEGINFO at %4d ("GASNETI_LADDRFMT", %d)", i,
		    GASNETI_LADDRSTR(gasnetc_seginfo[i].addr), 
		    (unsigned int) gasnetc_seginfo[i].size) );
	#endif

	/* -------------------------------------------------------------------- */
	/*  primary attach complete */
	gasneti_attach_done = 1;

	GASNETI_TRACE_PRINTF(C,("gasnetc_attach(): primary attach complete"));

	gasnetc_bootstrapBarrier();
	gasnete_init();
	gasnetc_bootstrapBarrier();

	gasnetc_dump_tokens();

	return GASNET_OK;
}

extern int 
gasnet_init(int *argc, char ***argv)
{
	int retval = gasnetc_init(argc, argv);
	if (retval != GASNET_OK) 
		GASNETI_RETURN(retval);
	return GASNET_OK;
}

/* ------------------------------------------------------------------------------------ */
extern void 
gasnetc_exit(int exitcode)
{
	/* once we start a shutdown, ignore all future SIGQUIT signals or we
	 * risk reentrancy */
	gasneti_reghandler(SIGQUIT, SIG_IGN);

        {  /* ensure only one thread ever continues past this point */
          static gasneti_mutex_t exit_lock = GASNETI_MUTEX_INITIALIZER;
          gasneti_mutex_lock(&exit_lock);
        }

	#if defined(GASNET_SEGMENT_FAST)
	if (gasneti_attach_done && gm_deregister_memory(_gmc.port, 
	    (void *) gasnetc_seginfo[gasnetc_mynode].addr,
	    gasnetc_seginfo[gasnetc_mynode].size) != GM_SUCCESS)
		fprintf(stderr, 
		    "%d> Couldn't deregister prepinned segment",
		    gasnetc_mynode);
	#endif	

	gasnetc_DestroyPinnedBufs();

	if (fflush(stdout)) 
		gasneti_fatalerror("failed to flush stdout in gasnetc_exit: %s", 
		    strerror(errno));
	if (fflush(stderr)) 
		gasneti_fatalerror("failed to flush stderr in gasnetc_exit: %s", 
		    strerror(errno));
        gasneti_trace_finish();
        gasneti_sched_yield();

	if (gasneti_init_done) {
  		gm_close(_gmc.port);
		if (gasneti_attach_done)
			firehose_fini();
	}
	gm_finalize();
	_exit(exitcode);
}

/* ------------------------------------------------------------------------------------ */
/*
  Job Environment Queries
  =======================
*/
extern int 
gasnetc_getSegmentInfo(gasnet_seginfo_t *seginfo_table, int numentries) {
	GASNETI_CHECKINIT();
        gasneti_assert(seginfo_table);
        gasneti_memcheck(gasnetc_seginfo);

	if (!gasneti_attach_done) GASNETI_RETURN_ERR(NOT_INIT);
	if (numentries < gasnetc_nodes) GASNETI_RETURN_ERR(BAD_ARG);

	memset(seginfo_table, 0, numentries*sizeof(gasnet_seginfo_t));
	memcpy(seginfo_table, gasnetc_seginfo, 
			numentries*sizeof(gasnet_seginfo_t));

	return GASNET_OK;
}

/* ------------------------------------------------------------------------------------ */
/*
  Misc. Core handlers
*/
void
gasnetc_am_medcopy_inner(gasnet_token_t token, void *addr, size_t nbytes, 
			 void *dest)
{
	memcpy(dest, addr, nbytes);
}
MEDIUM_HANDLER(gasnetc_am_medcopy,1,2,
              (token,addr,nbytes, UNPACK(a0)    ),
              (token,addr,nbytes, UNPACK2(a0, a1)));
/* ------------------------------------------------------------------------------------ */
/*
  Misc. Active Message Functions
  ==============================
*/
extern int gasnetc_AMGetMsgSource(gasnet_token_t token, gasnet_node_t *srcindex) {
  gasnet_node_t sourceid;
  gasnetc_bufdesc_t *bufd;

  GASNETI_CHECKINIT();
  if_pf (!token) GASNETI_RETURN_ERRR(BAD_ARG,"bad token");
  if_pf (!srcindex) GASNETI_RETURN_ERRR(BAD_ARG,"bad src ptr");

  bufd = (gasnetc_bufdesc_t *) token;
  if ((void *)token == (void *)-1) {
	  *srcindex = gasnetc_mynode;
	  return GASNET_OK;
  }
  if_pf (!bufd->gm_id) GASNETI_RETURN_ERRR(BAD_ARG, "No GM receive event");
  sourceid = bufd->node;

  gasneti_assert(sourceid < gasnetc_nodes);
  *srcindex = sourceid;
  return GASNET_OK;
}

/* ------------------------------------------------------------------------------------ */
/*
  Active Message Request Functions
  ================================
*/

extern int 
gasnetc_AMRequestShortM(gasnet_node_t dest, gasnet_handler_t handler, 
                            int numargs, ...) 
{
	va_list argptr;
	gasnetc_bufdesc_t *bufd;
	int len;

	GASNETI_CHECKINIT();
	gasneti_assert(numargs >= 0 && numargs <= gasnet_AMMaxArgs());

	if_pf (dest >= gasnetc_nodes) 
		GASNETI_RETURN_ERRR(BAD_ARG,"node index too high");
	GASNETI_TRACE_AMREQUESTSHORT(dest,handler,numargs);

	va_start(argptr, numargs);

	if (dest == gasnetc_mynode) { /* local handler */
		int argbuf[GASNETC_AM_MAX_ARGS];
		GASNETC_ARGS_WRITE(argbuf, argptr, numargs);
		GASNETC_RUN_HANDLER_SHORT(_gmc.handlers[handler], (void *) -1, 
					  argbuf, numargs);
	}
	else {
		bufd = gasnetc_AMRequestPool_block();
		len = gasnetc_write_AMBufferShort(bufd->buf, handler, numargs, 
		    		argptr, GASNETC_AM_REQUEST);
		gasnetc_GMSend_AMRequest(bufd->buf, len, _gmc.gm_nodes[dest].id,
					 _gmc.gm_nodes[dest].port, 
					 gasnetc_callback_lo, (void *)bufd, 0);
	}

	va_end(argptr);
	return GASNET_OK;
}

extern int gasnetc_AMRequestMediumM( 
                            gasnet_node_t dest,      /* destination node */
                            gasnet_handler_t handler, /* index into destination endpoint's handler table */ 
                            void *source_addr, size_t nbytes,   /* data payload */
                            int numargs, ...) {
  int retval;
  va_list argptr;
  gasnetc_bufdesc_t *bufd;
  int len;
  GASNETI_CHECKINIT();
  gasneti_assert(numargs >= 0 && numargs <= gasnet_AMMaxArgs());

  if_pf (dest >= gasnetc_nodes)
	  gasneti_fatalerror("node index too high, dest (%d) >= gasnetc_nodes (%d)\n",
	    dest, gasnetc_nodes);
  if_pf (dest >= gasnetc_nodes) GASNETI_RETURN_ERRR(BAD_ARG,"node index too high");
  gasneti_assert(numargs >= 0 && numargs <= gasnet_AMMaxArgs());
  if_pf (nbytes > gasnet_AMMaxMedium()) GASNETI_RETURN_ERRR(BAD_ARG,"nbytes too large");
  GASNETI_TRACE_AMREQUESTMEDIUM(dest,handler,source_addr,nbytes,numargs);
  va_start(argptr, numargs); /*  pass in last argument */

  gasneti_assert(nbytes <= GASNETC_AM_MEDIUM_MAX);
  retval = 1;
  if (dest == gasnetc_mynode) { /* local handler */
    void *loopbuf;
    int argbuf[GASNETC_AM_MAX_ARGS];
    loopbuf = gasnetc_alloca(nbytes);
    memcpy(loopbuf, source_addr, nbytes);
    GASNETC_ARGS_WRITE(argbuf, argptr, numargs);
    GASNETC_RUN_HANDLER_MEDIUM(_gmc.handlers[handler], (void *) -1,
				argbuf, numargs, loopbuf, nbytes);
  }
  else {
    bufd = gasnetc_AMRequestPool_block();
    len = gasnetc_write_AMBufferMedium(bufd->buf, handler, numargs, argptr, 
		 nbytes, source_addr, GASNETC_AM_REQUEST);
    gasnetc_GMSend_AMRequest(bufd->buf, len, 
		  gasnetc_nodeid(dest), gasnetc_portid(dest),
		  gasnetc_callback_lo, (void *)bufd, 0);
  }

  va_end(argptr);
  if (retval) return GASNET_OK;
  else GASNETI_RETURN_ERR(RESOURCE);
}

/* 
 * DMA_inner allows to DMA an AMLong when the local buffer isn't pinned and the
 * remote buffer is
 */
GASNET_INLINE_MODIFIER(gasnetc_AMRequestLongM_DMA_inner)
void
gasnetc_AMRequestLongM_DMA_inner(gasnet_node_t node, gasnet_handler_t handler,
		void *source_addr, size_t nbytes, const firehose_request_t *req,
		uintptr_t dest_addr, int numargs, va_list argptr)
{
	int	bytes_left = nbytes;
	int	port, id, len;
	uint8_t	*psrc, *pdest;
	gasnetc_bufdesc_t	*bufd;

	gasneti_assert(nbytes > 0);
	gasneti_assert(req != NULL);

	psrc  = (uint8_t *) source_addr;
	pdest = (uint8_t *) dest_addr;
	port  = gasnetc_portid(node);
	id    = gasnetc_nodeid(node);

	/* Until the remaining buffer size fits in a Long Buffer, get AM
	 * buffers and DMA out of them.  This assumes the remote destination is
	 * pinned and the local is not */
	while (bytes_left >GASNETC_AM_LEN-GASNETC_LONG_OFFSET) {
		bufd = gasnetc_AMRequestPool_block();
		gasnetc_write_AMBufferBulk(bufd->buf, 
			psrc, GASNETC_AM_LEN);
		gasnetc_GMSend_AMRequest(bufd->buf, 
		   GASNETC_AM_LEN, id, port, gasnetc_callback_lo,
		   (void *) bufd, (uintptr_t) pdest);
		psrc += GASNETC_AM_LEN;
		pdest += GASNETC_AM_LEN;
		bytes_left -= GASNETC_AM_LEN;
	}
	/* Write the header for the AM long buffer */
	bufd = gasnetc_AMRequestPool_block();
	bufd->node = node;
	len =
	    gasnetc_write_AMBufferLong(bufd->buf, 
	        handler, numargs, argptr, nbytes, source_addr, 
		(uintptr_t) dest_addr, GASNETC_AM_REQUEST);

	/* If bytes are left, write them in the remainder of the AM buffer */
	if (bytes_left > 0) {
		gasnetc_write_AMBufferBulk(
			(uint8_t *)bufd->buf+GASNETC_LONG_OFFSET, 
			psrc, (size_t) bytes_left);
		gasnetc_GMSend_AMRequest(
		    (uint8_t *)bufd->buf+GASNETC_LONG_OFFSET,
		    bytes_left, id, port, gasnetc_callback_lo, NULL,
		    (uintptr_t) pdest);
	}

	/* Set the firehose request type in the last bufd, so it may be
	 * released once the last AMRequest receives its reply */
	bufd->remote_req = req;
	gasnetc_GMSend_AMRequest(bufd->buf, len, id, 
	    port, gasnetc_callback_lo_rdma, (void *)bufd, 0);
}

/* When the local and remote regions are not pinned, AM buffers are used and
 * Mediums are sent for the entire payload.  Once the payloads are sent, an
 * AMLong header is sent (with no payload)
 */
GASNET_INLINE_MODIFIER(gasnetc_AMRequestLongM_inner)
void
gasnetc_AMRequestLongM_inner(gasnet_node_t node, gasnet_handler_t handler,
		void *source_addr, size_t nbytes, void *dest_addr, int numargs, 
		va_list argptr)
{

	int	bytes_left = nbytes;
	int	port, id, len, long_len;
	uint8_t	*psrc, *pdest;
	gasnetc_bufdesc_t	*bufd;

	psrc  = (uint8_t *) source_addr;
	pdest = (uint8_t *) dest_addr;
	port  = gasnetc_portid(node);
	id    = gasnetc_nodeid(node);

	/* If the length is greater than what we can fit in an AMLong buffer,
	 * send AM Mediums until that threshold is reached */
	while (bytes_left >GASNETC_AM_LEN-GASNETC_LONG_OFFSET) {
		bufd = gasnetc_AMRequestPool_block();
		len = gasnetc_write_AMBufferMediumMedcopy(bufd->buf,
			(void *) psrc, gasnet_AMMaxMedium(), (void *) pdest, 
			GASNETC_AM_REQUEST);
		gasnetc_GMSend_AMRequest(bufd->buf, len, id, 
		    port, gasnetc_callback_lo, (void *) bufd, 0);
		psrc += gasnet_AMMaxMedium();
		pdest += gasnet_AMMaxMedium();
		bytes_left -= gasnet_AMMaxMedium();
	}
	bufd = gasnetc_AMRequestPool_block();
	long_len =
	    gasnetc_write_AMBufferLong(bufd->buf, 
	        handler, numargs, argptr, nbytes, source_addr, 
		(uintptr_t) dest_addr, GASNETC_AM_REQUEST);

	if (bytes_left > 0) {
		uintptr_t	pbuf;
		pbuf = (uintptr_t) bufd->buf + (uintptr_t) long_len;

		len = gasnetc_write_AMBufferMediumMedcopy((void *)pbuf,
			(void *) psrc, bytes_left, (void *) pdest, 
			GASNETC_AM_REQUEST);

		gasnetc_GMSend_AMRequest((void *)pbuf, len, id, port,
		    gasnetc_callback_lo, NULL, 0);
	}
	gasnetc_GMSend_AMRequest(bufd->buf, long_len, id, port, 
	    gasnetc_callback_lo, (void *)bufd, 0);
	return;
}

extern int gasnetc_AMRequestLongM( gasnet_node_t node,        /* destination node */
                            gasnet_handler_t handler, /* index into destination endpoint's handler table */ 
                            void *source_addr, size_t nbytes,   /* data payload */
                            void *dest_addr,                    /* data destination on destination node */
                            int numargs, ...)
{
	int	retval;
	va_list	argptr;

	GASNETI_CHECKINIT();
  
	gasnetc_boundscheck(node, dest_addr, nbytes);
	gasneti_assert(numargs >= 0 && numargs <= gasnet_AMMaxArgs());

	if_pf (nbytes > gasnet_AMMaxLongRequest()) 
		GASNETI_RETURN_ERRR(BAD_ARG,"nbytes too large");
	if_pf (node >= gasnetc_nodes) 
		GASNETI_RETURN_ERRR(BAD_ARG,"node index too high");
	if_pf (((uintptr_t)dest_addr)< ((uintptr_t)gasnetc_seginfo[node].addr) ||
	    ((uintptr_t)dest_addr) + nbytes > 
	        ((uintptr_t)gasnetc_seginfo[node].addr) + gasnetc_seginfo[node].size) 
         	GASNETI_RETURN_ERRR(BAD_ARG,"destination address out of segment range");
	GASNETI_TRACE_AMREQUESTLONG(node,handler,source_addr,nbytes,dest_addr,numargs);
	va_start(argptr, numargs); /*  pass in last argument */

	retval = 1;
	gasneti_assert(nbytes <= GASNETC_AM_LONG_REQUEST_MAX);

	if (node == gasnetc_mynode) {
		int	argbuf[GASNETC_AM_MAX_ARGS];

		GASNETC_ARGS_WRITE(argbuf, argptr, numargs);
		GASNETC_AMPAYLOAD_WRITE(dest_addr, source_addr, nbytes);
		GASNETC_RUN_HANDLER_LONG(_gmc.handlers[handler], (void *) -1, 
		    argbuf, numargs, dest_addr, nbytes);
	}
	else {
		/* XXX gasneti_assert(GASNET_LONG_OFFSET >= LONG_HEADER) */
		if_pt (nbytes > 0) { /* Handle zero-length messages */
			const firehose_request_t	*req;
			
			req = firehose_try_remote_pin(node, 
				(uintptr_t) dest_addr, nbytes, 0, NULL);

			if (req != NULL)
				gasnetc_AMRequestLongM_DMA_inner(node, handler, 
				    source_addr, nbytes, req, 
				    (uintptr_t) dest_addr, numargs, argptr);
			else
				gasnetc_AMRequestLongM_inner(node, handler, 
				    source_addr, nbytes, dest_addr, numargs, 
				    argptr);
		}
		else {
			gasnetc_AMRequestLongM_inner(node, handler, source_addr, 
			    nbytes, dest_addr, numargs, argptr);
		}
	}

	va_end(argptr);
	if (retval) return GASNET_OK;
	else GASNETI_RETURN_ERR(RESOURCE);
}

extern int 
gasnetc_AMRequestLongAsyncM( 
	gasnet_node_t dest,        /* destination node */
	gasnet_handler_t handler,  /* index to handler */
	void *source_addr, size_t nbytes,   /* data payload */
	void *dest_addr,           /* data destination on destination node */
	int numargs, ...)
{
	int	retval;
	va_list	argptr;

	const firehose_request_t	*reql, *reqr;

	gasnetc_bufdesc_t	*bufd;
	GASNETI_CHECKINIT();
	gasneti_assert(numargs >= 0 && numargs <= gasnet_AMMaxArgs());
	gasnetc_boundscheck(dest, dest_addr, nbytes);

        gasneti_assert(numargs >= 0 && numargs <= gasnet_AMMaxArgs());
        if_pf (nbytes > gasnet_AMMaxLongRequest()) GASNETI_RETURN_ERRR(BAD_ARG,"nbytes too large");

	if_pf (dest >= gasnetc_nodes)
		GASNETI_RETURN_ERRR(BAD_ARG,"node index too high");
	if_pf (((uintptr_t)dest_addr)<((uintptr_t)gasnetc_seginfo[dest].addr) ||
	    ((uintptr_t)dest_addr) + nbytes > 
	        ((uintptr_t)gasnetc_seginfo[dest].addr)+gasnetc_seginfo[dest].size) 
		GASNETI_RETURN_ERRR(BAD_ARG,
		    "destination address out of segment range");

	GASNETI_TRACE_AMREQUESTLONGASYNC(
		dest,handler,source_addr,nbytes,dest_addr,numargs);

	va_start(argptr, numargs); /*  pass in last argument */
	retval = 1;

	/* If length is 0 or the remote local is not pinned, send using
	 * AMMedium payloads */
	if (nbytes == 0 || 
	    !(reqr = firehose_try_remote_pin(dest, (uintptr_t) dest_addr, 
	    nbytes, 0, NULL)))
		gasnetc_AMRequestLongM_inner(dest, handler, source_addr, 
		    nbytes, dest_addr, numargs, argptr);

	/* If we couldn't pin locally for free, use DMA method where we use AM
	 * buffers and copy+RDMA payloads out of them */
	else if (!(reql = 
	     firehose_try_local_pin((uintptr_t) source_addr, nbytes, NULL)))
		gasnetc_AMRequestLongM_DMA_inner(dest, handler, source_addr, 
		    nbytes, reqr, (uintptr_t) dest_addr, numargs, argptr);

	/* If both local and remote locations are pinned, use RDMA and send a
	 * header-only AMLong */
	else {
		uint16_t port, id;
		int	 len;

		gasneti_assert(reql != NULL && reqr != NULL);

		port = gasnetc_portid(dest);
		id   = gasnetc_nodeid(dest);
		bufd = gasnetc_AMRequestPool_block();
		len =
		    gasnetc_write_AMBufferLong(bufd->buf, 
		        handler, numargs, argptr, nbytes, source_addr, 
			(uintptr_t) dest_addr, GASNETC_AM_REQUEST);

		bufd->node = dest;
		bufd->local_req = reql;
		bufd->remote_req = reqr;

		/* send the DMA first */
		gasnetc_GMSend_AMRequest(source_addr, nbytes, id, 
		    port, gasnetc_callback_lo, NULL, 
		    (uintptr_t) dest_addr);

		/* followed by the Long Header which releases firehose */
		gasnetc_GMSend_AMRequest(bufd->buf, len, id, 
		    port, gasnetc_callback_lo_rdma, (void *)bufd, 0);
	}

	va_end(argptr);
	if (retval) return GASNET_OK;
	else GASNETI_RETURN_ERR(RESOURCE);
}

/* -------------------------------------------------------------------------- */
/* Replies */
/* -------------------------------------------------------------------------- */
void
gasnetc_GMSend_bufd(gasnetc_bufdesc_t *bufd)
{
	uintptr_t			send_ptr;
	uint32_t			len;

	gasneti_mutex_assertlocked(&gasnetc_lock_gm);
	gasneti_assert(bufd != NULL);
	gasneti_assert(bufd->buf != NULL);
	gasneti_assert(bufd->gm_id > 0);

	gasneti_assert(BUFD_ISSET(bufd, BUFD_REPLY));

	if (BUFD_ISSET(bufd, BUFD_PAYLOAD) && BUFD_ISSET(bufd, BUFD_DMA)) {
		gasneti_assert(bufd->dest_addr > 0);
		gasneti_assert(bufd->payload_len > 0);

		if (bufd->source_addr > 0)
			send_ptr = bufd->source_addr;
		else
			send_ptr = (uintptr_t) 
				   bufd->buf + bufd->payload_off;

		GASNETI_TRACE_PRINTF(C, ("gm_put (%d,%p <- %p,%d bytes)",
		    bufd->node, (void *) bufd->dest_addr, (void *) send_ptr,
		    bufd->payload_len));

		gm_directed_send_with_callback(_gmc.port, 
		    (void *) send_ptr,
		    (gm_remote_ptr_t) bufd->dest_addr,
		    bufd->payload_len,
		    GM_HIGH_PRIORITY,
		    (uint32_t) bufd->gm_id,
		    (uint32_t) bufd->gm_port,
		    gasnetc_callback_hi_rdma,
		    (void *) bufd);
	}
	else {
		void	*context;

		if (BUFD_ISSET(bufd, BUFD_PAYLOAD)) {
			len = bufd->payload_len;
			context = NULL;
			send_ptr = 
				(uintptr_t) bufd->buf + 
				(uintptr_t) bufd->payload_off;
		}
		else {
			len = bufd->len;
			send_ptr = (uintptr_t) bufd->buf;
			context = (void *)bufd;
		}

		GASNETI_TRACE_PRINTF(C, ("gm_send (gm id %d <- %p,%d bytes)",
		    (unsigned) bufd->gm_id, (void *) send_ptr, len));

		gasneti_assert(GASNETC_AM_IS_REPLY(*((uint8_t *) send_ptr)));
		gasneti_assert(len > 0 && len <= GASNETC_AM_PACKET);

		if (_gmc.my_port == bufd->gm_port)
		gm_send_to_peer_with_callback(_gmc.port, 
			(void *) send_ptr,
			GASNETC_AM_SIZE,
			len,
			GM_HIGH_PRIORITY,
			(uint32_t) bufd->gm_id,
			gasnetc_callback_hi,
			context);
		else
		gm_send_with_callback(_gmc.port, 
			(void *) send_ptr,
			GASNETC_AM_SIZE,
			len,
			GM_HIGH_PRIORITY,
			(uint32_t) bufd->gm_id,
			(uint32_t) bufd->gm_port,
			gasnetc_callback_hi,
			context);
	}
	return;
}

int
gasnetc_AMReplyLongTrySend(gasnetc_bufdesc_t *bufd)
{
	int	sends = 0;

	gasneti_mutex_lock(&gasnetc_lock_gm);

	gasneti_assert(BUFD_ISSET(bufd, BUFD_REPLY));

	if (gasnetc_token_hi_acquire()) {
		/* First send the payload */
		gasnetc_GMSend_bufd(bufd);
		sends++;

		if_pt (BUFD_ISSET(bufd, BUFD_PAYLOAD)) { 
			BUFD_UNSET(bufd, BUFD_PAYLOAD);

			if (gasnetc_token_hi_acquire()) {
				gasnetc_GMSend_bufd(bufd);
				sends++;
			}
			/* If we can't get the second token, unset only
			 * the payload bit and enqueue the header send
			 */
			else {
				gasnetc_fifo_insert(bufd);
			}
		}
	}

	/* We couldn't get a send token, enqueue the whole bufd and
	 * leave the flag bits as is */
	else {
		gasnetc_fifo_insert(bufd);
	}

	gasneti_mutex_unlock(&gasnetc_lock_gm);

	return sends;
}

extern int 
gasnetc_AMReplyShortM(gasnet_token_t token, gasnet_handler_t handler,
                            int numargs, ...) 
{
	va_list argptr;
	gasnetc_bufdesc_t *bufd;

	va_start(argptr, numargs); /*  pass in last argument */
	GASNETI_TRACE_AMREPLYSHORT(token,handler,numargs);
	gasneti_assert(numargs >= 0 && numargs <= gasnet_AMMaxArgs());

	if ((void *)token == (void*)-1) { /* local handler */
		int argbuf[GASNETC_AM_MAX_ARGS];
		GASNETC_ARGS_WRITE(argbuf, argptr, numargs);
		GASNETC_RUN_HANDLER_SHORT(_gmc.handlers[handler], 
				(void *) token, argbuf, numargs);
	}
	else {
		bufd = gasnetc_bufdesc_from_token(token);
		bufd->len = 
		    gasnetc_write_AMBufferShort(bufd->buf, handler, 
		    		numargs, argptr, GASNETC_AM_REPLY);
  
		gasneti_mutex_lock(&gasnetc_lock_gm);

		if (gasnetc_token_hi_acquire())
			gasnetc_GMSend_bufd(bufd);
		else
			gasnetc_fifo_insert(bufd);

		gasneti_mutex_unlock(&gasnetc_lock_gm);
	}

	va_end(argptr);
	return GASNET_OK;
}

extern int gasnetc_AMReplyMediumM( 
                            gasnet_token_t token,       /* token provided on handler entry */
                            gasnet_handler_t handler, /* index into destination endpoint's handler table */ 
                            void *source_addr, size_t nbytes,   /* data payload */
                            int numargs, ...) {
  int retval;
  va_list argptr;
  gasnetc_bufdesc_t *bufd;
  va_start(argptr, numargs); /*  pass in last argument */

  gasneti_assert(numargs >= 0 && numargs <= gasnet_AMMaxArgs());
  if_pf (nbytes > gasnet_AMMaxMedium()) GASNETI_RETURN_ERRR(BAD_ARG,"nbytes too large");
  GASNETI_TRACE_AMREPLYMEDIUM(token,handler,source_addr,nbytes,numargs);
  retval = 1;
  gasneti_assert(nbytes <= GASNETC_AM_MEDIUM_MAX);
  if ((void *)token == (void *)-1) { /* local handler */
    int argbuf[GASNETC_AM_MAX_ARGS];
    void *loopbuf;
    loopbuf = gasnetc_alloca(nbytes);
    memcpy(loopbuf, source_addr, nbytes);
    GASNETC_ARGS_WRITE(argbuf, argptr, numargs);
    GASNETC_RUN_HANDLER_MEDIUM(_gmc.handlers[handler], (void *) token,
				argbuf, numargs, loopbuf, nbytes);
  }
  else {
    if_pf (nbytes > GASNETC_AM_MEDIUM_MAX) 
	    GASNETI_RETURN_ERRR(BAD_ARG,"AMMedium Payload too large");
    bufd = gasnetc_bufdesc_from_token(token);
    bufd->len = 
	    gasnetc_write_AMBufferMedium(bufd->buf, handler, numargs, 
                    argptr, nbytes, source_addr, GASNETC_AM_REPLY);
    gasneti_mutex_lock(&gasnetc_lock_gm);
    if (gasnetc_token_hi_acquire()) {
       gasnetc_GMSend_bufd(bufd); 
    } else {
	gasneti_assert(bufd->gm_id > 0);
       gasnetc_fifo_insert(bufd);
    }
    gasneti_mutex_unlock(&gasnetc_lock_gm);
  }
  
  va_end(argptr);
  if (retval) return GASNET_OK;
  else GASNETI_RETURN_ERR(RESOURCE);
}

extern int gasnetc_AMReplyLongM( 
		gasnet_token_t token,       /* token provided on handler entry */
		gasnet_handler_t handler, /* index into destination endpoint's handler table */ 
		void *source_addr, size_t nbytes,   /* data payload */
		void *dest_addr,                    /* data destination on destination node */
		int numargs, ...)
{
	int	retval;
	va_list	argptr;
	gasnet_node_t		dest;
	gasnetc_bufdesc_t 	*bufd;

	retval = gasnet_AMGetMsgSource(token, &dest);
	if (retval != GASNET_OK) GASNETI_RETURN(retval);
	if_pf (dest >= gasnetc_nodes) 
		GASNETI_RETURN_ERRR(BAD_ARG,"node index too high");
        gasneti_assert(numargs >= 0 && numargs <= gasnet_AMMaxArgs());
        if_pf (nbytes > gasnet_AMMaxLongReply()) GASNETI_RETURN_ERRR(BAD_ARG,"nbytes too large");
	if_pf (((uintptr_t)dest_addr)< ((uintptr_t)gasnetc_seginfo[dest].addr)||
	    ((uintptr_t)dest_addr) + nbytes > 
	    ((uintptr_t)gasnetc_seginfo[dest].addr)+gasnetc_seginfo[dest].size)
		GASNETI_RETURN_ERRR(BAD_ARG,
		    "destination address out of segment range");

	va_start(argptr, numargs); /*  pass in last argument */
	GASNETI_TRACE_AMREPLYLONG(token,handler,source_addr,nbytes,dest_addr,
	    numargs);
	retval = 1;
	gasneti_assert(nbytes <= GASNETC_AM_LONG_REPLY_MAX);
	if ((void *)token == (void *)-1) {
		int	argbuf[GASNETC_AM_MAX_ARGS];
		GASNETC_AMTRACE_ReplyLong(Loopbk);
		GASNETC_ARGS_WRITE(argbuf, argptr, numargs);
		GASNETC_RUN_HANDLER_LONG(_gmc.handlers[handler], (void *)token, 
		    argbuf, numargs, dest_addr, nbytes);
	}
	else {
		uintptr_t	pbuf;
		unsigned int	len;

		const firehose_request_t	*req;
	
    		bufd            = gasnetc_bufdesc_from_token(token);
		bufd->dest_addr = (uintptr_t) dest_addr;
		bufd->node      = dest;

		if (nbytes > 0 &&
		   (req = firehose_try_remote_pin(dest, (uintptr_t) dest_addr, 
	    	            nbytes, 0,  NULL)) != NULL) {

			pbuf = (uintptr_t) bufd->buf + 
			    (uintptr_t) GASNETC_LONG_OFFSET;
			len =
			    gasnetc_write_AMBufferLong(bufd->buf, handler,
			        numargs, argptr, nbytes, source_addr, 
				(uintptr_t) dest_addr, GASNETC_AM_REPLY);
			gasnetc_write_AMBufferBulk((void *)pbuf, source_addr, 
			    nbytes);

			bufd->len = len;
			bufd->remote_req = req;
			bufd->local_req = NULL;
			bufd->source_addr = 0;

			bufd->payload_off = GASNETC_LONG_OFFSET;
			bufd->payload_len = nbytes;

    			BUFD_SET(bufd, BUFD_PAYLOAD | BUFD_DMA);
		}
		else {
			size_t	header_len;
	
			/* The AMLong Reply doesn't use DMA */
			bufd->remote_req = NULL;
			bufd->local_req = NULL;

			bufd->len = header_len = 
			    gasnetc_write_AMBufferLong(bufd->buf, 
			        handler, numargs, argptr, nbytes, source_addr, 
				(uintptr_t) dest_addr, GASNETC_AM_REPLY);
			pbuf = (uintptr_t)bufd->buf + (uintptr_t) header_len;

			if_pt (nbytes > 0) { /* Handle zero-length messages */
				len = gasnetc_write_AMBufferMediumMedcopy(
					(void *)pbuf, (void *)source_addr, nbytes,
					(void *)dest_addr, GASNETC_AM_REPLY);

				BUFD_SET(bufd, BUFD_PAYLOAD);

				bufd->payload_off = header_len;
				bufd->payload_len = len;
			}

			bufd->len = header_len;
		}

		#if !GASNET_TRACE
		(void) gasnetc_AMReplyLongTrySend(bufd);
		#else
		{
			int payload = BUFD_ISSET(bufd, BUFD_PAYLOAD);
			int sends = gasnetc_AMReplyLongTrySend(bufd);
	
			if (sends == 2)
				GASNETC_AMTRACE_ReplyLong(Send);
			else if (sends == 1) {
				if (payload)
					GASNETC_AMTRACE_ReplyLong(Queued);
				else
					GASNETC_AMTRACE_ReplyLong(Send);
			}
			else
				GASNETC_AMTRACE_ReplyLong(Queued);
		}
		#endif
	}
	va_end(argptr);
	if (retval) return GASNET_OK;
	else GASNETI_RETURN_ERR(RESOURCE);
}

/*
 * This is not officially part of the gasnet spec therefore not exported for the
 * user.  The extended API uses it in order to do directed sends followed by an
 * AMReply.  Therefore, there is no boundscheck of any sort.  The extended API
 * knows how/when to call this function and makes sure the source and
 * destination regions are pinned.
 */
int 
gasnetc_AMReplyLongAsyncM( 
		gasnet_token_t token,       /* token provided on handler entry */
		gasnet_handler_t handler, /* index into destination endpoint's handler table */ 
		void *source_addr, size_t nbytes,   /* data payload */
		void *dest_addr,                    /* data destination on destination node */
		int numargs, ...)
{
	int	retval;
	va_list	argptr;
	unsigned int		len;
	gasnet_node_t		dest;
	gasnetc_bufdesc_t 	*bufd;

	retval = gasnet_AMGetMsgSource(token, &dest);
	if (retval != GASNET_OK) GASNETI_RETURN(retval);
	if_pf (dest >= gasnetc_nodes) 
		GASNETI_RETURN_ERRR(BAD_ARG,"node index too high");
        gasneti_assert(numargs >= 0 && numargs <= gasnet_AMMaxArgs());
	va_start(argptr, numargs); /*  pass in last argument */
	GASNETI_TRACE_AMREPLYLONG(token,handler,source_addr,nbytes,dest_addr,
	    numargs);
	retval = 1;

	bufd = gasnetc_bufdesc_from_token(token);
	len =
	    gasnetc_write_AMBufferLong(bufd->buf, handler, numargs, argptr, 
	        nbytes, source_addr, (uintptr_t) dest_addr, GASNETC_AM_REPLY);

	bufd->len = len;
	bufd->node = dest;
	bufd->payload_off = 0;
	bufd->payload_len = nbytes;
	bufd->dest_addr = (uintptr_t) dest_addr;
	bufd->source_addr = (uintptr_t) source_addr;
	bufd->local_req = NULL;
	bufd->remote_req = NULL;

	gasneti_assert(nbytes > 0);

	if_pt (nbytes > 0) {
		/* Also manage loopback by simply copying to the local
		 * destination */
		if_pf (dest == gasnetc_mynode) {
			GASNETE_FAST_ALIGNED_MEMCPY(
			    dest_addr, source_addr, nbytes);
		}
		else {
			BUFD_SET(bufd, BUFD_PAYLOAD | BUFD_DMA);
		}
	}

	#if !GASNET_TRACE
	(void) gasnetc_AMReplyLongTrySend(bufd);
	#else
	{
		int payload = BUFD_ISSET(bufd, BUFD_PAYLOAD);
		int sends = gasnetc_AMReplyLongTrySend(bufd);

		if (sends == 2)
			GASNETC_AMTRACE_ReplyLong(Send);
		else if (sends == 1) {
			if (payload)
				GASNETC_AMTRACE_ReplyLong(Queued);
			else
				GASNETC_AMTRACE_ReplyLong(Send);
		}
		else
			GASNETC_AMTRACE_ReplyLong(Queued);
	}
	#endif

	va_end(argptr);
	if (retval) return GASNET_OK;
	else GASNETI_RETURN_ERR(RESOURCE);
}
/* -------------------------------------------------------------------------- */
/* Core misc. functions                                                       */
void
gasnetc_AllocPinnedBufs()
{
	int	i, nbufs;
	size_t	buflen;
	uint8_t	*ptr;
	void	*bufptr;

	int	idx_ammed, idx_amreq, idx_rlo, idx_rhi;

	gasnetc_bufdesc_t	*bufd;

	gasneti_assert(_gmc.port != NULL);
	gasneti_mutex_lock(&gasnetc_lock_gm);

	_gmc.stoks.max = gm_num_send_tokens(_gmc.port);
	_gmc.stoks.hi = _gmc.stoks.lo = _gmc.stoks.total = 0;

	_gmc.rtoks.max = gm_num_receive_tokens(_gmc.port);
	_gmc.rtoks.lo = _gmc.rtoks.hi = _gmc.rtoks.max/2;

	/* We need to allocate the following types of DMA'd buffers:
	 * A) 1 AMMedBuf (handling replies after an AMMediumRequest) 
	 * B) (stoks-1) AMRequest bufs
	 * C) rtoks.lo AMRequest receive buffers
	 * D) rtoks.hi AMReply receive buffers
	 *
	 * Note that each of these have a bufdesc_t attached to them
	 */
	nbufs = 1 + _gmc.stoks.max-1 + _gmc.rtoks.lo + _gmc.rtoks.hi;

	buflen = (size_t) (nbufs * GASNETC_AM_LEN);

	/* Allocate and register DMA buffers */ 
	_gmc.dma_bufs = gm_alloc_pages(buflen);
	if_pf (_gmc.dma_bufs == NULL)
		gasneti_fatalerror("gm_alloc_pages(%d) %s", (int)buflen,
		   gasneti_current_loc);

	if (gm_register_memory(_gmc.port, _gmc.dma_bufs, buflen) != GM_SUCCESS)
		gasneti_fatalerror("Can't pin GASNet buffers");

	/* Allocate the AMRequest send buffer pool stack */
	_gmc.reqs_pool = (int *) 
	    gasneti_malloc(sizeof(int) * (_gmc.stoks.max-1));

	/* Allocate a buffer descriptor (bufdesc_t) for each DMA'd buffer
	 * and fill in id/sendbuf for cheap reverse lookups */
	_gmc.bd_ptr = (gasnetc_bufdesc_t *)
	    gasneti_malloc(nbufs * sizeof(gasnetc_bufdesc_t));
	ptr = (uint8_t *) _gmc.dma_bufs;

	/* Initialize each of the buffer descriptors with their buffers */
	for (i = 0; i < nbufs; i++) {
		_gmc.bd_ptr[i].id = i;
		_gmc.bd_ptr[i].buf = (void *)(ptr + i*GASNETC_AM_LEN);
	}

	/* Token ids have the following assignments */
	idx_ammed = 0;
	idx_amreq = 1;
	idx_rlo   = 1 + (_gmc.stoks.max-1);
	idx_rhi   = idx_rlo + _gmc.rtoks.lo;
	gasneti_assert(idx_rhi + _gmc.rtoks.hi == nbufs);

	/* A) AMMedBuf for AMMediums */
	_gmc.AMMedBuf = &_gmc.bd_ptr[idx_ammed]; 
	BUFD_SETSTATE(&(_gmc.bd_ptr[idx_ammed]), BUFD_S_TEMPMED);

	/* B) stoks-1 AMRequest bufs */
	_gmc.reqs_pool_max = _gmc.stoks.max-1;
	_gmc.reqs_pool_cur = -1;
	for (i = idx_amreq; i < idx_rlo; i++) {
		bufptr = (void *)(ptr + (i<<GASNETC_AM_SIZE));
		bufd = GASNETC_BUFDESC_PTR(bufptr);
		GASNETC_ASSERT_BUFDESC_PTR(bufd, bufptr);

		BUFD_SETSTATE(bufd, BUFD_S_USED);
		gasnetc_provide_AMRequestPool(bufd);
	}
	gasneti_assert(_gmc.reqs_pool_cur == _gmc.reqs_pool_max-1);
	
	/* C) rtoks.lo AMRequest receive buffers given to GM */
	for (i = idx_rlo; i < idx_rhi; i++) {
		bufptr = (void *)(ptr + (i<<GASNETC_AM_SIZE));
		bufd = GASNETC_BUFDESC_PTR(bufptr);
		GASNETC_ASSERT_BUFDESC_PTR(bufd, bufptr);

		BUFD_SETSTATE(bufd, BUFD_S_USED);
		gasnetc_provide_AMReply(bufd);
	}

	/* D) rtoks.hi AMReply receive buffers given to GM */
	for (i = idx_rhi; i < nbufs; i++) { 
		bufptr = (void *)(ptr + (i<<GASNETC_AM_SIZE));
		bufd = GASNETC_BUFDESC_PTR(bufptr);
		GASNETC_ASSERT_BUFDESC_PTR(bufd, bufptr);

		BUFD_SETSTATE(bufd, BUFD_S_USED);
		gasnetc_provide_AMRequest(bufd);
	}

	if (gm_set_acceptable_sizes(_gmc.port, GM_HIGH_PRIORITY, 
			1<<GASNETC_AM_SIZE) != GM_SUCCESS)
		gasneti_fatalerror("can't set acceptable sizes for HIGH "
			"priority");
	if (gm_set_acceptable_sizes(_gmc.port, GM_LOW_PRIORITY, 
			1<<GASNETC_AM_SIZE) != GM_SUCCESS)
		gasneti_fatalerror("can't set acceptable sizes for LOW "
			"priority");
	gm_allow_remote_memory_access(_gmc.port);

	_gmc.bd_list_num = nbufs;

	gasneti_mutex_unlock(&gasnetc_lock_gm);

	return;
}

void
gasnetc_DestroyPinnedBufs()
{
	if (_gmc.dma_bufs != NULL)
		gm_free_pages(_gmc.dma_bufs, 
		    _gmc.bd_list_num << GASNETC_AM_SIZE);
	if (_gmc.bd_ptr != NULL)
		gasneti_free(_gmc.bd_ptr);
	if (_gmc.reqs_pool != NULL)
		gasneti_free(_gmc.reqs_pool);
}

int	
gasnetc_gm_nodes_compare(const void *k1, const void *k2)
{
	gasnetc_gm_nodes_rev_t	*a = (gasnetc_gm_nodes_rev_t *) k1;
	gasnetc_gm_nodes_rev_t	*b = (gasnetc_gm_nodes_rev_t *) k2;

	if (a->id > b->id)
		return 1;
	else if (a->id < b->id)
		return -1;
	else {
		if (a->port > b->port) return 1;
		if (a->port < b->port) return -1;
		else
			return 0;
	}
}

/* -------------------------------------------------------------------------- */

void
gasnetc_GMSend_AMRequest(void *buf, uint32_t len, 
		uint32_t id, uint32_t port,
		gm_send_completion_callback_t callback, 
		void *callback_ptr, uintptr_t dest_addr)
{
	int sent = 0;

	while (!sent) {
		/* don't force locking when polling */
		while (!GASNETC_TOKEN_LO_AVAILABLE())
			gasnetc_AMPoll();

		gasneti_mutex_lock(&gasnetc_lock_gm);
		/* assure last poll was successful */
		if (GASNETC_TOKEN_LO_AVAILABLE()) {
			if (dest_addr > 0)
				GASNETC_GM_PUT(_gmc.port, buf, dest_addr, 
					(unsigned int) len, GM_LOW_PRIORITY, 
					id, port, callback, callback_ptr);
			else {
				gasneti_assert(GASNETC_AM_IS_REQUEST(
				       *((uint8_t *) buf)));
				gasneti_assert(len <= GASNETC_AM_PACKET);

				if (_gmc.my_port == port)
				gm_send_to_peer_with_callback(_gmc.port, buf, 
					GASNETC_AM_SIZE, (unsigned int) len,
					GM_LOW_PRIORITY, id, callback,
					callback_ptr);
				else
				gm_send_with_callback(_gmc.port, buf, 
					GASNETC_AM_SIZE, (unsigned int) len,
					GM_LOW_PRIORITY, id, port, callback,
					callback_ptr);
			}
			_gmc.stoks.lo += 1;
			_gmc.stoks.total += 1;
			sent = 1;
		}
		gasneti_mutex_unlock(&gasnetc_lock_gm);
	}
}

void
gasnetc_GMSend_AMSystem(void *buf, size_t len, 
			uint16_t id, uint16_t port, void *context)
{
	gasneti_mutex_assertlocked(&gasnetc_lock_gm);
	gasneti_assert(buf != NULL);
	gasneti_assert(len >= 4); 
	gasneti_assert(id > 0);
	gasneti_assert(port > 0 && port < GASNETC_GM_MAXPORTS);
	gasneti_assert(len <= gm_max_length_for_size(GASNETC_AM_SIZE));

	gasnetc_token_lo_poll();

	gasneti_assert(GASNETC_AM_IS_SYSTEM(*((uint8_t *) buf)));
	gm_send_with_callback(_gmc.port, buf, GASNETC_AM_SIZE, len, 
			GM_LOW_PRIORITY, id, port, gasnetc_callback_system, 
			context);

	GASNETI_TRACE_PRINTF(C, 
	    ("AMSystem Send (id=%d,%d, msg=0x%x, len=%d)", id, port,
	     GASNETC_SYSHEADER_READ(buf), (int)len));
}


gasnetc_bufdesc_t *
gasnetc_AMRequestPool_block() 
{
	int			 bufd_idx = -1;
	gasnetc_bufdesc_t	*bufd;

	gasneti_mutex_assertunlocked(&gasnetc_lock_gm);

	/* Since every AMRequest send must go through the Pool, use this
	 * as an entry point to make progress in the Receive queue */
	gasnetc_AMPoll();

	while (bufd_idx < 0) {
		while (_gmc.reqs_pool_cur < 0)
			gasnetc_AMPoll();

		gasneti_mutex_lock(&gasnetc_lock_reqpool);
		if_pt (_gmc.reqs_pool_cur >= 0) {
			bufd_idx = _gmc.reqs_pool[_gmc.reqs_pool_cur];
			GASNETI_TRACE_PRINTF(C,
			    ("AMRequestPool (%d/%d) gave bufdesc id %d\n",
	    		    _gmc.reqs_pool_cur, _gmc.reqs_pool_max,
	    		    _gmc.reqs_pool[_gmc.reqs_pool_cur]));
			_gmc.reqs_pool_cur--;
		}
		gasneti_mutex_unlock(&gasnetc_lock_reqpool);
	}
	gasneti_assert(bufd_idx < _gmc.bd_list_num);
	bufd = &(_gmc.bd_ptr[bufd_idx]);

	gasneti_assert(BUFD_ISSTATE(bufd) == BUFD_S_AMREQ);
	BUFD_SETSTATE(bufd, BUFD_S_USED);
	gasneti_assert(bufd->buf != NULL);
	gasneti_assert(bufd->id == bufd_idx);

	return bufd;
}

/* -------------------------------------------------------------------------- */
/*
 * Bootstrap operations
 */

/*
 * Gather/Send operations are used for bootstrapBarrier and bootstrapExchange.
 *
 * bootstrap Barrier essentially exchanges only a single byte whereas
 * bootstrapExchange may exchange node information.  
 *
 * In the case of a barrier, the data should be NULL and size at 0.
 * In the case of an exchange, the data can be any size.
 */
int	gasnetc_bootstrapGather_phase	     =   0; /* start at even phase */
uint8_t gasnetc_bootstrapGather_buf[2][4096] = { { 0 } };
volatile int	gasnetc_bootstrapGather_recvd[2]     = { 0 };
volatile int	gasnetc_bootstrapBroadcast_recvd[2]  = { 0 };
volatile int	gasnetc_bootstrapGather_sent	     =   0;
volatile int	gasnetc_bootstrapBroadcast_sent      =   0;

static volatile int	gasnetc_bootstrapGatherSendInProgress = 0;

#define GASNETC_BARRIER_MASTER	0

void *
gasnetc_bootstrapGatherSend(void *data, size_t len)
{
	uint8_t	 *hdr, *payload;
	uint16_t *phase_ptr;
	int	 i, phase;

	gasnetc_bufdesc_t	*bufd;

	gasneti_mutex_lock(&gasnetc_lock_gm);
	if (gasnetc_bootstrapGatherSendInProgress)
		gasneti_fatalerror(
		    "Cannot issue two successive gasnetc_bootstrapExchange");
	else
		gasnetc_bootstrapGatherSendInProgress++;

	if ((len*gasnetc_nodes+4) > GASNETC_AM_PACKET)
		gasneti_fatalerror(
		    "bootstrapGatherSend: %i bytes too large\n", (int)len);

	phase = gasnetc_bootstrapGather_phase;
	gasnetc_bootstrapGather_phase ^= 1;
	
	gasneti_mutex_unlock(&gasnetc_lock_gm);
	bufd = gasnetc_AMRequestPool_block();
	gasneti_mutex_lock(&gasnetc_lock_gm);

	hdr = (uint8_t *) bufd->buf;
	phase_ptr = (uint16_t *)hdr + 1;
	payload = hdr + 4;

	if (gasnetc_mynode == GASNETC_BARRIER_MASTER) {

		GASNETC_SYSHEADER_WRITE(hdr, GASNETC_SYS_BROADCAST);
		*phase_ptr = phase;

		#if GASNET_DEBUG_VERBOSE
		printf("%d> waiting in %s phase!\n", 
		    GASNETC_BARRIER_MASTER, phase ? "odd" : "even");
		fflush(stdout);
		#endif

		GASNETC_BLOCKUNTIL(
		    gasnetc_bootstrapGather_recvd[phase] == gasnetc_nodes-1);
		gasnetc_bootstrapGather_recvd[phase] = 0;

		#if GASNET_DEBUG_VERBOSE
		printf("%d> done %s phase!\n", 
		    GASNETC_BARRIER_MASTER, phase ? "odd" : "even");
		fflush(stdout);
		#endif

		if (len > 0 && data != NULL) {
			gasneti_assert(len < 4096);
			memcpy(gasnetc_bootstrapGather_buf[phase], data, len);
			memcpy(payload, gasnetc_bootstrapGather_buf[phase], 
				    len*gasnetc_nodes);
		}

		if (gasnetc_nodes == 1)
			goto barrier_done;

		gasnetc_bootstrapBroadcast_sent = 0;
		for (i = 0; i < gasnetc_nodes; i++) {
			if (i == GASNETC_BARRIER_MASTER)
				continue;
			gasnetc_GMSend_AMSystem(hdr, len*gasnetc_nodes + 4,
			    _gmc.gm_nodes[i].id, _gmc.gm_nodes[i].port, 
			    (void *) &gasnetc_bootstrapBroadcast_sent);
		}
		GASNETC_BLOCKUNTIL(
			gasnetc_bootstrapBroadcast_sent == gasnetc_nodes-1);
		gasnetc_bootstrapBroadcast_sent = 0;
	}
	else {

		GASNETC_SYSHEADER_WRITE(hdr, GASNETC_SYS_GATHER);
		*phase_ptr = phase;
		if (len > 0 && data != NULL)
			memcpy(payload, data, len);
	
		gasnetc_bootstrapGather_sent = 0;
		gasnetc_GMSend_AMSystem(hdr, len + 4,
		    _gmc.gm_nodes[GASNETC_BARRIER_MASTER].id, 
		    _gmc.gm_nodes[GASNETC_BARRIER_MASTER].port, 
		    (void *) &gasnetc_bootstrapGather_sent);
	
		GASNETC_BLOCKUNTIL(gasnetc_bootstrapGather_sent == 1);
		gasnetc_bootstrapGather_sent = 0;

		/* Once we return from the block, the data is contained in
		 * gasnetc_bootstrapGather_buf */
		GASNETC_BLOCKUNTIL(gasnetc_bootstrapBroadcast_recvd[phase] == 1);
		gasnetc_bootstrapBroadcast_recvd[phase] = 0;
	}

	#if GASNET_DEBUG_VERBOSE
	printf("%d> done barrier!\n", gasnetc_mynode); fflush(stdout);
	#endif

barrier_done:

	gasnetc_bootstrapGatherSendInProgress = 0;
	gasnetc_provide_AMRequestPool(bufd);

	gasneti_mutex_unlock(&gasnetc_lock_gm);
	return gasnetc_bootstrapGather_buf[phase];
}

void
gasnetc_bootstrapBarrier() 
{
	gasnetc_bootstrapGatherSend(NULL, 0);
}


void
gasnetc_bootstrapExchange(void *src, size_t len, void *dest)
{
	void	*buf;

	buf = gasnetc_bootstrapGatherSend(src, len);
	memcpy(dest, buf, len*gasnetc_nodes);

	return;
}

void
gasnetc_dump_tokens()
{
	GASNETI_TRACE_PRINTF(C,
	    ("Send tokens: lo=%3d, hi=%3d, tot=%3d, max=%3d\n",
	    _gmc.stoks.lo, _gmc.stoks.hi, _gmc.stoks.total, _gmc.stoks.max));

	GASNETI_TRACE_PRINTF(C,
	    ("Recv tokens: lo=%3d, hi=%3d, tot=%3d, max=%3d\n",
	    _gmc.rtoks.lo, _gmc.rtoks.hi, _gmc.rtoks.total, _gmc.rtoks.max));
}

int
gasnetc_alloc_nodemap(int numnodes)
{
	_gmc.gm_nodes = (gasnetc_gm_nodes_t *) 
	    gasneti_malloc(numnodes*sizeof(gasnetc_gm_nodes_t));

	_gmc.gm_nodes_rev = (gasnetc_gm_nodes_rev_t *) 
	    gasneti_malloc(numnodes * sizeof(gasnetc_gm_nodes_rev_t));

	return (_gmc.gm_nodes != NULL && _gmc.gm_nodes_rev != NULL);
}

int
gasnetc_gmport_allocate(int *board, int *port)
{
	struct gm_port	*p;
	unsigned int	port_id, board_id;
	gm_status_t	status;

	gm_init();

	for (port_id = 2; port_id < GASNETC_GM_MAXPORTS; port_id++) {
		if (port_id == 3)
			continue;

		for (board_id = 0; board_id < GASNETC_GM_MAXBOARDS; board_id++) {

			status = gm_open(&p, board_id, port_id, 
					"GASNet/GM", GM_API_VERSION_1_4);

			switch (status) {
				case GM_SUCCESS:
					*board = board_id;
					*port = port_id;
					_gmc.port = p;
					return 1;
					break;
				case GM_INCOMPATIBLE_LIB_AND_DRIVER:
					gasneti_fatalerror("GM library and "
					    "driver are out of sync!");
					break;
				default:
					break;
			}

		}
	}
	return 0;
}

void
gasnetc_getconf_conffile()
{
	FILE		*fp;
	char		line[128];
	char		gmconf[128], *gmconfenv;
	char		gmhost[128], hostname[MAXHOSTNAMELEN+1];
	char		**hostnames;
	char		*homedir;
	int		lnum = 0, gmportnum, i;
	int		thisport = 0, thisid = 0, numnodes = 0, thisnode = -1;
	int		temp_id;
	gm_status_t	status;
	struct gm_port	*p;

	if ((homedir = getenv("HOME")) == NULL)
		gasneti_fatalerror("Couldn't find $HOME directory");

	if ((gmconfenv = getenv("GMPI_CONF")) != NULL)
		snprintf(gmconf, 128, "%s", gmconfenv);
	else
		snprintf(gmconf, 128, "%s/.gmpi/conf", homedir);

	if (gethostname(hostname, 128) < 0)
		gasneti_fatalerror("Couldn't get local hostname");

	if ((fp = fopen(gmconf, "r")) == NULL) {
		fprintf(stderr, "Couldn't open GMPI configuration file\n: %s", 
		    gmconf);
		return;
	}

	/* must do gm_init() from this point on since gm_host_name_to_node_id
	 * must use the port
	 */

	while (fgets(line, 128, fp)) {
	
		if (lnum == 0) {
	      		if ((sscanf(line, "%d\n", &numnodes)) < 1) 
				gasneti_fatalerror(
				    "job size not found in GMPI config file");
	      		else if (numnodes < 1) 
				gasneti_fatalerror(
				    "invalid numnodes in GMPI config file");

			if (!gasnetc_alloc_nodemap(numnodes))
				gasneti_fatalerror(
				    ("Can't allocate node mapping"));

			hostnames = (char **)
			    gasneti_malloc((numnodes+1)*sizeof(char *));
			hostnames[numnodes] = NULL;
			for (i = 0; i < numnodes; i++) {
				hostnames[i] =
				gasneti_malloc(MAXHOSTNAMELEN);
			}
			lnum++;
	      	}

		else if (lnum <= numnodes) {
			if ((sscanf(line,"%s %d\n",gmhost,&gmportnum)) == 2) {
				if (gmportnum < 1 || gmportnum > 7)
					gasneti_fatalerror(
					    "Invalid GM port");

				gasneti_assert(gmhost != NULL);

				_gmc.gm_nodes[lnum-1].port = gmportnum;
				memcpy(&hostnames[lnum-1][0], 
				    (void *)gmhost, MAXHOSTNAMELEN);

				if (strcasecmp(gmhost, hostname) == 0) {
					GASNETI_TRACE_PRINTF(C,
					    ("%s will bind to port %d\n", 
					    hostname, gmportnum) );
					thisnode = lnum-1;
					thisport = gmportnum;
				}
			}
                        else {
				fprintf(stderr, "couldn't parse: %s\n", line);
			}
			lnum++;
		}
	}
	
	fclose(fp);

	if (numnodes == 0 || thisnode == -1)
		gasneti_fatalerror("could not find myself in GMPI config file");
	gm_init();
	status = 
		gm_open(&p, GASNETC_DEFAULT_GM_BOARD_NUM, thisport,"GASNet/GM", 
		    GM_API_VERSION_1_4);
	if (status != GM_SUCCESS) {
		char	msg[64];
		sprintf(msg, "could not open GM port %d", thisport);
		gasneti_fatalerror(msg);
	}
	status = gm_get_node_id(p, (unsigned int *) &thisid);
	if (status != GM_SUCCESS)
		gasneti_fatalerror("could not get GM node id!");

#ifdef GASNETC_GM_2
	temp_id = _gmc.my_id;

	/* GM2 only stores local node ids, so a global has to be obtained */
	if (gm_node_id_to_global_id(_gmc.port, temp_id, &(_gmc.my_id)) 
	    != GM_SUCCESS)
		gasneti_fatalerror("Couldn't get GM global node id");
#endif

	for (i = 0; i < numnodes; i++) {
		_gmc.gm_nodes[i].id = 
		    gm_host_name_to_node_id(p, hostnames[i]);

		if (_gmc.gm_nodes[i].id == GM_NO_SUCH_NODE_ID) {
			fprintf(stderr, "%s (%d) has no id! Check mapper\n",
			    hostnames[i],
			    _gmc.gm_nodes[i].id);
			gasneti_fatalerror("Unknown GMid or GM mapper down");
		}
		_gmc.gm_nodes_rev[i].port = _gmc.gm_nodes[i].port;
		_gmc.gm_nodes_rev[i].node = (gasnet_node_t) i;

		_gmc.gm_nodes_rev[i].id = _gmc.gm_nodes[i].id;

		GASNETI_TRACE_PRINTF(C, ("%d> %s (gm %d, port %d)\n", 
		    i, hostnames[i], _gmc.gm_nodes[i].id, 
		    _gmc.gm_nodes[i].port));

	}

	gasnetc_mynode = thisnode;
	gasnetc_nodes = numnodes;
	for (i = 0; i < numnodes; i++)
		gasneti_free(hostnames[i]);
	gasneti_free(hostnames);

	/* sort out the gm_nodes_rev for bsearch, glibc qsort uses recursion,
	 * so stack memory in order to complete the sort.  We want to minimize
	 * the number of mallocs
	 */
	qsort(_gmc.gm_nodes_rev, numnodes, sizeof(gasnetc_gm_nodes_rev_t),
	    gasnetc_gm_nodes_compare);
	_gmc.port = p;
	return;
}

#ifdef LINUX
uintptr_t
gasnetc_getPhysMem()
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
#elif defined(FREEBSD)
#include <sys/types.h>
#include <sys/sysctl.h>
uintptr_t
gasnetc_getPhysMem()
{
	uintptr_t	mem = 0;
	size_t		len = sizeof(uintptr_t);

	if (sysctlbyname("hw.physmem", &mem, &len, NULL, NULL))
		gasneti_fatalerror("couldn't query systcl(hw.physmem");
	return mem;
}
#else
uintptr_t
gasnetc_getPhysMem()
{
	return (uintptr_t) 0;
}
#endif

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

  GASNETI_TRACE_EVENT_TIME(L, HSL_UNLOCK, GASNETI_STATTIME_NOW_IFENABLED(L)-hsl->acquiretime);

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
  gasneti_handler_tableentry_with_bits(gasnetc_am_medcopy),
  { 0, NULL }
};

gasnet_handlerentry_t const *gasnetc_get_handlertable() {
  return gasnetc_handlers;
}

/* ------------------------------------------------------------------------------------ */


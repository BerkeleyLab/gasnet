/* $Id: gasnet_core.c,v 1.39.2.4 2003/08/25 08:23:52 csbell Exp $
 * $Date: 2003/08/25 08:23:52 $
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

GASNETI_IDENT(gasnetc_IdentString_Version, "$GASNetCoreLibraryVersion: " GASNET_CORE_VERSION_STR " $");
GASNETI_IDENT(gasnetc_IdentString_ConduitName, "$GASNetConduitName: " GASNET_CORE_NAME_STR " $");

int		gasnetc_init_done = 0;   /*  true after init */
int		gasnetc_attach_done = 0; /*  true after attach */
gasnet_node_t	gasnetc_mynode = (gasnet_node_t)-1;
gasnet_node_t	gasnetc_nodes = 0;
uintptr_t	gasnetc_MaxLocalSegmentSize = 0;
uintptr_t	gasnetc_MaxGlobalSegmentSize = 0;

gasnet_seginfo_t *gasnetc_seginfo = NULL;
firehose_info_t	  gasnetc_firehose_info;

gasneti_mutex_t gasnetc_lock_gm = GASNETI_MUTEX_INITIALIZER;
gasneti_mutex_t gasnetc_lock_reqpool = GASNETI_MUTEX_INITIALIZER;
gasneti_mutex_t gasnetc_lock_amreq = GASNETI_MUTEX_INITIALIZER;
gasnetc_state_t _gmc;

gasnet_handlerentry_t const		*gasnetc_get_handlertable();
extern gasnet_handlerentry_t const	*gasnete_get_handlertable();
extern gasnet_handlerentry_t const	*gasnete_get_extref_handlertable();

void gasnetc_checkinit() {
  if (!gasnetc_init_done)
    gasneti_fatalerror("Illegal call to GASNet before gasnet_init() initialization");
}

void gasnetc_checkattach() {
  if (!gasnetc_attach_done)
    gasneti_fatalerror("Illegal call to GASNet before gasnet_attach() initialization");
}

/*
  Initialization
  ==============
*/
/* called at startup to check configuration sanity */
static void gasnetc_check_config() {
  assert(gm_min_size_for_length(GASNETC_AM_MEDIUM_MAX) <= GASNETC_AM_SIZE);
  assert(gm_min_size_for_length(GASNETC_AM_LONG_REPLY_MAX) <= GASNETC_AM_SIZE);
  assert(gm_max_length_for_size(GASNETC_AM_SIZE) <= GASNETC_AM_PACKET);
  assert(gm_max_length_for_size(GASNETC_SYS_SIZE) <= GASNETC_AM_PACKET);
  assert(GASNETC_AM_MEDIUM_MAX <= (uint16_t)(-1));
  assert(GASNETC_AM_MAX_HANDLERS >= 256);
  return;
}

static int 
gasnetc_init(int *argc, char ***argv)
{
	/* check system sanity */
	gasnetc_check_config();

	if (gasnetc_init_done) 
		GASNETI_RETURN_ERRR(NOT_INIT, "GASNet already initialized");

        if (getenv("GASNET_FREEZE")) gasneti_freezeForDebugger();

	#if DEBUG_VERBOSE
	/* note - can't call trace macros during gasnet_init because trace
	 * system not yet initialized */
	fprintf(stderr,"gasnetc_init(): about to spawn...\n"); fflush(stderr);
	#endif

	if (gasnetc_getconf() != GASNET_OK)
		gasneti_fatalerror("Couldn't bootstrap system");

	gasnetc_sendbuf_init();

	/* When not using everything, we must find the largest segment possible
	 * using a binary search of largest mmaps possible.  mmap (even for
	 * huge segments) happens to be a cheap operation on linux. */
	#if defined(GASNET_SEGMENT_FAST) || defined(GASNET_SEGMENT_LARGE)

		gasneti_segmentInit(&gasnetc_MaxLocalSegmentSize,
		    &gasnetc_MaxGlobalSegmentSize,
                    #if 0 && defined(GASNET_SEGMENT_FAST)
                       gasnetc_remappableMem.size,
                    #else
                       (uintptr_t)-1,
                    #endif
                    gasnetc_nodes,
                    &gasnetc_bootstrapExchange);

	#elif defined(GASNET_SEGMENT_EVERYTHING)
		gasnetc_MaxLocalSegmentSize =  (uintptr_t)-1;
		gasnetc_MaxGlobalSegmentSize = (uintptr_t)-1;
	#else
		#error Bad segment config
	#endif

	/*  grab GM buffers and make sure we have the maximum amount
	 *  possible */
	gasneti_mutex_lock(&gasnetc_lock_gm);
	while (_gmc.stoks.hi != 0) {
		if (gasnetc_SysPoll((void *)-1) != _NO_MSG)
		gasneti_fatalerror("Unexpected message during bootstrap");
	}
	gasneti_mutex_unlock(&gasnetc_lock_gm);

	gasnetc_init_done = 1;
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
	int retval = GASNET_OK, i = 0, fidx = 0;

	GASNETI_TRACE_PRINTF(C,
	    ("gasnetc_attach(table (%i entries), segsize=%lu, minheapoffset=%lu)",
	    numentries, (unsigned long)segsize, (unsigned long)minheapoffset));

	if (!gasnetc_init_done) 
		GASNETI_RETURN_ERRR(NOT_INIT,
		    "GASNet attach called before init");
	if (gasnetc_attach_done) 
		GASNETI_RETURN_ERRR(NOT_INIT, "GASNet already attached");

	#if defined(GASNET_SEGMENT_FAST) || defined(GASNET_SEGMENT_LARGE)
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

		assert(ctable);
		while (ctable[c_len].fnptr) c_len++; /* calc len */
		if (gasnetc_reghandlers(ctable, c_len, 1, 63, 0, &c_numreg)
		    != GASNET_OK)
			GASNETI_RETURN_ERRR(RESOURCE,
			    "Error registering core API handlers");
		assert(c_numreg == c_len);
	}
	{ /*  extended API handlers */
		gasnet_handlerentry_t *ertable = 
		    (gasnet_handlerentry_t *)gasnete_get_extref_handlertable();
		gasnet_handlerentry_t *etable = 
		    (gasnet_handlerentry_t *)gasnete_get_handlertable();
		int er_len = 0, e_len = 0;
		int er_numreg = 0, e_numreg = 0;
		assert(etable && ertable);
	
		while (ertable[er_len].fnptr) er_len++; /* calc len */
		while (etable[e_len].fnptr) e_len++; /* calc len */
		if (gasnetc_reghandlers(ertable, er_len, 64, 127, 0, 
		    &er_numreg) != GASNET_OK)
			GASNETI_RETURN_ERRR(RESOURCE,
			    "Error registering extended reference API handlers");
	    	assert(er_numreg == er_len);
	
		if (gasnetc_reghandlers(etable, e_len, 64+er_len, 127, 0, 
		    &e_numreg) != GASNET_OK)
			GASNETI_RETURN_ERRR(RESOURCE,
			    "Error registering extended API handlers");
	    	assert(e_numreg == e_len);
		fidx = 64+er_len+e_len;
	}
	{ /* firehose handlers */
		gasnet_handlerentry_t *ftable = firehose_get_handlertable();
		int f_len = 0;
		int f_numreg = 0;

		assert(ftable);

		while (ftable[f_len].fnptr)
			f_len++;

		assert(fidx + f_len <= 128);
		if (gasnetc_reghandlers(ftable, f_len, fidx, 127, 1, &f_numreg)
		    != GASNET_OK)
			GASNETI_RETURN_ERRR(RESOURCE,
			    "Error registering firehose handlers");
		assert(f_numreg == f_len);
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

		assert(numreg1 + numreg2 == numentries);
	}

	/* -------------------------------------------------------------------- */
	/*  register fatal signal handlers */

	/*  catch fatal signals and convert to SIGQUIT */
	gasneti_registerSignalHandlers(gasneti_defaultSignalHandler);

	/* -------------------------------------------------------------------- */
	/*  register segment  */

	/* use gasneti_malloc_inhandler during bootstrapping because we can't
	 * assume the hold/resume interrupts functions are operational yet */
	gasnetc_seginfo = (gasnet_seginfo_t *)
	    gasneti_malloc_inhandler(gasnetc_nodes*sizeof(gasnet_seginfo_t));
	memset(gasnetc_seginfo, 0, gasnetc_nodes*sizeof(gasnet_seginfo_t));

	#if defined(GASNET_SEGMENT_FAST) || defined(GASNET_SEGMENT_LARGE)
		if (segsize == 0) { /* no segment */
			int i;
			for (i=0;i<gasnetc_nodes;i++) {
				gasnetc_seginfo[i].addr = (void *)0;
				gasnetc_seginfo[i].size = (uintptr_t)-1;
			}
		}
		else {
			gasneti_segmentAttach(segsize, minheapoffset, 
			    gasnetc_seginfo, &gasnetc_bootstrapExchange);
		}
	#else
		/* GASNET_SEGMENT_EVERYTHING */
		{	int i;
			for (i=0;i<gasnetc_nodes;i++) {
				gasnetc_seginfo[i].addr = (void *)0;
				gasnetc_seginfo[i].size = (uintptr_t)-1;
			}
		}
	#endif

	#ifdef TRACE
	for (i = 0; i < gasnetc_nodes; i++)
		GASNETI_TRACE_PRINTF(C, ("SEGINFO at %4d (0x%x, %d)", i,
		    (uintptr_t) gasnetc_seginfo[i].addr, 
		    (unsigned int) gasnetc_seginfo[i].size) );
	#endif
	/* Firehose algorithm requires access to the global amount of physical
	 * memory in its calculation for upper bounds */
	{
		uintptr_t local_physmem = gasnetc_getPhysMem();
		uintptr_t global_physmem = (uintptr_t) -1;
		uintptr_t *global_exch = (uintptr_t *)
		    gasneti_malloc(gasnetc_nodes*sizeof(uintptr_t));
		gasnetc_bootstrapExchange(&local_physmem, sizeof(uintptr_t),
		    global_exch);
		for (i = 0; i < gasnetc_nodes; i++) 
			global_physmem = MIN(global_physmem, global_exch[i]);

		gasneti_free(global_exch);

		firehose_init(global_physmem, 0, NULL, 0, &gasnetc_firehose_info);
	}
			
	/* -------------------------------------------------------------------- */
	/*  primary attach complete */
	gasnetc_attach_done = 1;

	GASNETI_TRACE_PRINTF(C,("gasnetc_attach(): primary attach complete"));

	gasnetc_bootstrapBarrier();
	gasnete_init();
	gasnetc_bootstrapBarrier();

	/*  grab GM buffers and make sure we have the maximum amount possible */
	gasneti_mutex_lock(&gasnetc_lock_gm);
	while (_gmc.stoks.hi != 0) {
		if (gasnetc_SysPoll((void *)-1) != _NO_MSG)
		gasneti_fatalerror("Unexpected message during bootstrap");
	}
	gasnetc_provide_receive_buffers();
	gasneti_mutex_unlock(&gasnetc_lock_gm);

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

	gasnetc_sendbuf_finalize();

        gasneti_trace_finish();
	if (fflush(stdout)) 
		gasneti_fatalerror("failed to flush stdout in gasnetc_exit: %s", 
		    strerror(errno));
	if (fflush(stderr)) 
		gasneti_fatalerror("failed to flush stderr in gasnetc_exit: %s", 
		    strerror(errno));

        gasneti_sched_yield();
	sleep(1); /* pause to ensure everyone has written trace if this is a
		   * collective exit */

	if (gasnetc_init_done) {
  		gm_close(_gmc.port);
		if (gasnetc_attach_done)
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
extern int gasnetc_getSegmentInfo(gasnet_seginfo_t *seginfo_table, int numentries) {
  GASNETC_CHECKINIT();
  assert(gasnetc_seginfo && seginfo_table);
  if (!gasnetc_init_done) GASNETI_RETURN_ERR(NOT_INIT);
  if (numentries < gasnetc_nodes) GASNETI_RETURN_ERR(BAD_ARG);
  memset(seginfo_table, 0, numentries*sizeof(gasnet_seginfo_t));
  memcpy(seginfo_table, gasnetc_seginfo, numentries*sizeof(gasnet_seginfo_t));
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

  GASNETC_CHECKINIT();
  if_pf (!token) GASNETI_RETURN_ERRR(BAD_ARG,"bad token");
  if_pf (!srcindex) GASNETI_RETURN_ERRR(BAD_ARG,"bad src ptr");

  bufd = (gasnetc_bufdesc_t *) token;
  if ((void *)token == (void *)-1) {
	  *srcindex = gasnetc_mynode;
	  return GASNET_OK;
  }
  if_pf (!bufd->gm_id) GASNETI_RETURN_ERRR(BAD_ARG, "No GM receive event");
  sourceid = gasnetc_gm_nodes_search(bufd->gm_id, bufd->gm_port);

  assert(sourceid < gasnetc_nodes);
  *srcindex = sourceid;
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
  gasnetc_bufdesc_t *bufd;
  int len;

  GASNETC_CHECKINIT();
  if_pf (dest >= gasnetc_nodes) GASNETI_RETURN_ERRR(BAD_ARG,"node index too high");
  GASNETI_TRACE_AMREQUESTSHORT(dest,handler,numargs);
  va_start(argptr, numargs); /*  pass in last argument */

  retval = 1;
  if (dest == gasnetc_mynode) { /* local handler */
    int argbuf[GASNETC_AM_MAX_ARGS];
    GASNETC_ARGS_WRITE(argbuf, argptr, numargs);
    GASNETC_RUN_HANDLER_SHORT(_gmc.handlers[handler], (void *) -1, argbuf, numargs);
  }
  else {
    bufd = gasnetc_AMRequestPool_block();
    len = gasnetc_write_AMBufferShort(bufd->buf, handler, numargs, 
		    argptr, GASNETC_AM_REQUEST);
    gasnetc_tokensend_AMRequest(bufd->buf, len, 
		  gasnetc_nodeid(dest), gasnetc_portid(dest),
		  gasnetc_callback_lo_bufd, (void *)bufd, 0);
  }

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
  gasnetc_bufdesc_t *bufd;
  int len;
  GASNETC_CHECKINIT();

  if_pf (dest >= gasnetc_nodes)
	  gasneti_fatalerror("node index too high, dest (%d) >= gasnetc_nodes (%d)\n",
	    dest, gasnetc_nodes);
  if_pf (dest >= gasnetc_nodes) GASNETI_RETURN_ERRR(BAD_ARG,"node index too high");
  GASNETI_TRACE_AMREQUESTMEDIUM(dest,handler,source_addr,nbytes,numargs);
  va_start(argptr, numargs); /*  pass in last argument */

  assert(nbytes <= GASNETC_AM_MEDIUM_MAX);
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
    gasnetc_tokensend_AMRequest(bufd->buf, len, 
		  gasnetc_nodeid(dest), gasnetc_portid(dest),
		  gasnetc_callback_lo_bufd, (void *)bufd, 0);
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

	assert(nbytes > 0);
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
		gasnetc_tokensend_AMRequest(bufd->buf, 
		   GASNETC_AM_LEN, id, port, gasnetc_callback_lo_bufd,
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
		gasnetc_tokensend_AMRequest(
		    (uint8_t *)bufd->buf+GASNETC_LONG_OFFSET,
		    bytes_left, id, port, gasnetc_callback_lo, NULL,
		    (uintptr_t) pdest);
	}

	/* Set the firehose request type in the last bufd, so it may be
	 * released once the last AMRequest receives its reply */
	bufd->remote_req = req;
	gasnetc_tokensend_AMRequest(bufd->buf, len, id, 
	    port, gasnetc_callback_lo_bufd_rdma, (void *)bufd, 0);
}

/* When the local and remote regions are not pinned, AM buffers are used and
 * Mediums are sent for the entire payload.  Once the payloads are sent, an
 * AMLong header is sent (with no payload)
 */
GASNET_INLINE_MODIFIER(gasnetc_AMRequestLongM_inner)
void
gasnetc_AMRequestLongM_inner(gasnet_node_t dest, gasnet_handler_t handler,
		void *source_addr, size_t nbytes, void *dest_addr, int numargs, 
		va_list argptr)
{

	int	bytes_left = nbytes;
	int	port, id, len, long_len;
	int32_t	dest_addr_ptr[2];
	uint8_t	*psrc, *pdest;
	gasnetc_bufdesc_t	*bufd;

	psrc  = (uint8_t *) source_addr;
	pdest = (uint8_t *) dest_addr;
	port  = gasnetc_portid(dest);
	id    = gasnetc_nodeid(dest);

	/* If the length is greater than what we can fit in an AMLong buffer,
	 * send AM Mediums until that threshold is reached */
	while (bytes_left >GASNETC_AM_LEN-GASNETC_LONG_OFFSET) {
		bufd = gasnetc_AMRequestPool_block();
		GASNETC_ARGPTR(dest_addr_ptr, (uintptr_t) pdest);
		len = gasnetc_write_AMBufferMedium(bufd->buf,
		    gasneti_handleridx(gasnetc_am_medcopy), 
			GASNETC_ARGPTR_NUM, (va_list) dest_addr_ptr, 
			gasnet_AMMaxMedium(), (void *) psrc, GASNETC_AM_REQUEST);
		gasnetc_tokensend_AMRequest(bufd->buf, len, id, 
		    port, gasnetc_callback_lo_bufd, (void *) bufd, 0);
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
		GASNETC_ARGPTR(dest_addr_ptr, (uintptr_t) pdest);
		len = gasnetc_write_AMBufferMedium((void *)pbuf,
	    	    gasneti_handleridx(gasnetc_am_medcopy), 
		    GASNETC_ARGPTR_NUM, (va_list) dest_addr_ptr, 
		    bytes_left, (void *) psrc, GASNETC_AM_REQUEST);
		gasnetc_tokensend_AMRequest((void *)pbuf, len, id, port,
		    gasnetc_callback_lo, NULL, 0);
	}
	gasnetc_tokensend_AMRequest(bufd->buf, long_len, id, port, 
	    gasnetc_callback_lo_bufd, (void *)bufd, 0);
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

	GASNETC_CHECKINIT();
  
	gasnetc_boundscheck(node, dest_addr, nbytes);
	assert(nbytes <= gasnet_AMMaxLongRequest());
	if_pf (node >= gasnetc_nodes) 
		GASNETI_RETURN_ERRR(BAD_ARG,"node index too high");
	if_pf (((uintptr_t)dest_addr)< ((uintptr_t)gasnetc_seginfo[node].addr) ||
	    ((uintptr_t)dest_addr) + nbytes > 
	        ((uintptr_t)gasnetc_seginfo[node].addr) + gasnetc_seginfo[node].size) 
         	GASNETI_RETURN_ERRR(BAD_ARG,"destination address out of segment range");
	GASNETI_TRACE_AMREQUESTLONG(node,handler,source_addr,nbytes,dest_addr,numargs);
	va_start(argptr, numargs); /*  pass in last argument */

	retval = 1;
	assert(nbytes <= GASNETC_AM_LONG_REQUEST_MAX);

	if (node == gasnetc_mynode) {
		int	argbuf[GASNETC_AM_MAX_ARGS];

		GASNETC_ARGS_WRITE(argbuf, argptr, numargs);
		GASNETC_AMPAYLOAD_WRITE(dest_addr, source_addr, nbytes);
		GASNETC_RUN_HANDLER_LONG(_gmc.handlers[handler], (void *) -1, 
		    argbuf, numargs, dest_addr, nbytes);
	}
	else {
		/* XXX assert(GASNET_LONG_OFFSET >= LONG_HEADER) */
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
	GASNETC_CHECKINIT();
	
	gasnetc_boundscheck(dest, dest_addr, nbytes);
	assert(nbytes <= gasnet_AMMaxLongRequest());
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
		gasnetc_tokensend_AMRequest(source_addr, nbytes, id, 
		    port, gasnetc_callback_lo_rdma, (void *) bufd, 
		    (uintptr_t) dest_addr);

		/* followed by the Long Header */
		gasnetc_tokensend_AMRequest(bufd->buf, len, id, 
		    port, gasnetc_callback_lo_bufd, (void *)bufd, 0);
	}

	va_end(argptr);
	if (retval) return GASNET_OK;
	else GASNETI_RETURN_ERR(RESOURCE);
}

/* -------------------------------------------------------------------------- */
/* Replies */
/* -------------------------------------------------------------------------- */
void
gasnetc_gm_send_bufd(gasnetc_bufdesc_t *bufd)
{
	uintptr_t			send_ptr;
	uint32_t			len;
	gm_send_completion_callback_t	callback;

	gasneti_mutex_assertlocked(&gasnetc_lock_gm);
	assert(bufd != NULL);
	assert(bufd->buf != NULL);
	assert(bufd->gm_id > 0);

	if (GASNETC_BUFOPT_ISSET(bufd, GASNETC_FLAG_REPLY_PAYLOAD) &&
	    (bufd->remote_req != NULL || 
	     GASNETC_BUFOPT_ISSET(bufd, GASNETC_FLAG_REPLY_ASYNC))) {

		assert(bufd->dest_addr > 0);
		assert(bufd->payload_len > 0);

		if (bufd->source_addr > 0)
			send_ptr = bufd->source_addr;
		else
			send_ptr = (uintptr_t) 
				   bufd->buf + bufd->payload_off;

		GASNETI_TRACE_PRINTF(C, ("gm_put (%d,%p <- %p,%d bytes)",
		    bufd->node, (void *) bufd->dest_addr, (void *) send_ptr,
		    bufd->payload_len));

		if (GASNETC_BUFOPT_ISSET(bufd, GASNETC_FLAG_REPLY_ASYNC))
			callback = gasnetc_callback_hi;
		else
			callback = gasnetc_callback_hi_rdma;

		gm_directed_send_with_callback(_gmc.port, 
		    (void *) send_ptr,
		    (gm_remote_ptr_t) bufd->dest_addr,
		    bufd->payload_len,
		    GM_HIGH_PRIORITY,
		    (uint32_t) bufd->gm_id,
		    (uint32_t) bufd->gm_port,
		    callback,
		    (void *) bufd);
	}
	else {
		if (GASNETC_BUFOPT_ISSET(bufd, 
		    GASNETC_FLAG_REPLY_PAYLOAD)) {
			callback = gasnetc_callback_hi;
			len = bufd->payload_len;
			send_ptr = 
				(uintptr_t) bufd->buf + 
				(uintptr_t) bufd->payload_off;
		}
		else {
			assert(GASNETC_BUFOPT_ISSET(bufd, 
			       GASNETC_FLAG_REPLY_HEADER));
			callback = gasnetc_callback_hi_bufd;
			len = bufd->len;
			send_ptr = (uintptr_t) bufd->buf;
		}

		assert(GASNETC_AM_IS_REPLY(*((uint8_t *) bufd->buf)));
		assert(len <= GASNETC_AM_PACKET);
		GASNETI_TRACE_PRINTF(C, ("gm_send (gm id %d <- %p,%d bytes)",
		    (unsigned) bufd->gm_id, (void *) send_ptr, len));

		gm_send_with_callback(_gmc.port, 
			(void *) send_ptr,
			GASNETC_AM_SIZE,
			len,
			GM_HIGH_PRIORITY,
			(uint32_t) bufd->gm_id,
			(uint32_t) bufd->gm_port,
			callback,
			(void *) bufd);
	}
	return;
}

int
gasnetc_AMReplyLongTrySend(gasnetc_bufdesc_t *bufd)
{
	int	sends = 0;

	gasneti_mutex_lock(&gasnetc_lock_gm);

	if (gasnetc_token_hi_acquire()) {
		/* First send the payload */
		gasnetc_gm_send_bufd(bufd);
		sends++;

		if_pt (GASNETC_BUFOPT_ISSET(bufd, GASNETC_FLAG_REPLY_PAYLOAD)) {
			GASNETC_BUFOPT_UNSET(bufd, GASNETC_FLAG_REPLY_PAYLOAD);

			/* If we can get the second token, send and
			 * unset both header and payload bits */
			if (gasnetc_token_hi_acquire()) {
				gasnetc_gm_send_bufd(bufd);
				sends++;

				GASNETC_BUFOPT_UNSET(bufd,
				    GASNETC_FLAG_REPLY_HEADER);
			}
			/* If we can't get the second token, unset only
			 * the payload bit and enqueue the header send
			 */
			else {
				gasnetc_fifo_insert(bufd);
			}
		}
		/* If there was no payload, we are done sending only
		 * the header */
		else {
			GASNETC_BUFOPT_UNSET(bufd,
			    GASNETC_FLAG_REPLY_HEADER);
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


extern int gasnetc_AMReplyShortM( 
                            gasnet_token_t token,       /* token provided on handler entry */
                            gasnet_handler_t handler, /* index into destination endpoint's handler table */ 
                            int numargs, ...) {
  int retval;
  va_list argptr;
  gasnetc_bufdesc_t *bufd;

  va_start(argptr, numargs); /*  pass in last argument */
  GASNETI_TRACE_AMREPLYSHORT(token,handler,numargs);
  retval = 1;
  if ((void *)token == (void*)-1) { /* local handler */
    int argbuf[GASNETC_AM_MAX_ARGS];
    /* GASNETC_AMTRACE_ReplyShort(Loopbk); */
    GASNETC_ARGS_WRITE(argbuf, argptr, numargs);
    GASNETC_RUN_HANDLER_SHORT(_gmc.handlers[handler], (void *) token, argbuf, numargs);
  }
  else {
    bufd = gasnetc_bufdesc_from_token(token);
    bufd->len = gasnetc_write_AMBufferShort(bufd->buf, handler, 
		    numargs, argptr, GASNETC_AM_REPLY);
  
    GASNETC_BUFOPT_SET(bufd, GASNETC_FLAG_REPLY_HEADER);
    gasneti_mutex_lock(&gasnetc_lock_gm);
    if (gasnetc_token_hi_acquire()) {
       gasnetc_gm_send_bufd(bufd);
    } else {
	assert(bufd->gm_id > 0);
       gasnetc_fifo_insert(bufd);
    }
    gasneti_mutex_unlock(&gasnetc_lock_gm);
  }

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
  gasnetc_bufdesc_t *bufd;
  va_start(argptr, numargs); /*  pass in last argument */

  GASNETI_TRACE_AMREPLYMEDIUM(token,handler,source_addr,nbytes,numargs);
  retval = 1;
  assert(nbytes <= GASNETC_AM_MEDIUM_MAX);
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
    GASNETC_BUFOPT_SET(bufd, GASNETC_FLAG_REPLY_HEADER);
    gasneti_mutex_lock(&gasnetc_lock_gm);
    if (gasnetc_token_hi_acquire()) {
       gasnetc_gm_send_bufd(bufd); 
    } else {
	assert(bufd->gm_id > 0);
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
	int	hdr_len;
	va_list	argptr;
	gasnet_node_t		dest;
	gasnetc_bufdesc_t 	*bufd;

	retval = gasnet_AMGetMsgSource(token, &dest);
	if (retval != GASNET_OK) GASNETI_RETURN(retval);
	if_pf (dest >= gasnetc_nodes) 
		GASNETI_RETURN_ERRR(BAD_ARG,"node index too high");
	if_pf (((uintptr_t)dest_addr)< ((uintptr_t)gasnetc_seginfo[dest].addr)||
	    ((uintptr_t)dest_addr) + nbytes > 
	    ((uintptr_t)gasnetc_seginfo[dest].addr)+gasnetc_seginfo[dest].size)
		GASNETI_RETURN_ERRR(BAD_ARG,
		    "destination address out of segment range");

	va_start(argptr, numargs); /*  pass in last argument */
	GASNETI_TRACE_AMREPLYLONG(token,handler,source_addr,nbytes,dest_addr,
	    numargs);
	retval = 1;
	assert(nbytes <= GASNETC_AM_LONG_REPLY_MAX);
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

			bufd->payload_off = GASNETC_LONG_OFFSET;
			bufd->payload_len = nbytes;

			GASNETC_BUFOPT_SET(bufd, 
			    GASNETC_FLAG_REPLY_PAYLOAD | 
			    GASNETC_FLAG_REPLY_HEADER);
		}
		else {
			int32_t	dest_addr_ptr[2];
			size_t	header_len;
	
			/* The AMLong Reply doesn't use DMA */
			bufd->remote_req = NULL;
			bufd->local_req = NULL;

			header_len = 
			    gasnetc_write_AMBufferLong(bufd->buf, 
			        handler, numargs, argptr, nbytes, source_addr, 
				(uintptr_t) dest_addr, GASNETC_AM_REPLY);
			pbuf = (uintptr_t)bufd->buf 
				+ (uintptr_t) header_len;
			GASNETC_ARGPTR(dest_addr_ptr, (uintptr_t) dest_addr);

			if_pt (nbytes > 0) { /* Handle zero-length messages */
				len = gasnetc_write_AMBufferMedium((void *)pbuf,
				    gasneti_handleridx(gasnetc_am_medcopy), 
				    GASNETC_ARGPTR_NUM, (va_list) dest_addr_ptr, 
				    nbytes, (void *) source_addr, 
				    GASNETC_AM_REPLY);

				GASNETC_BUFOPT_SET(bufd, 
				    GASNETC_FLAG_REPLY_PAYLOAD | 
				    GASNETC_FLAG_REPLY_HEADER);

				bufd->payload_off = header_len;
				bufd->payload_len = len;
			}
			else
				GASNETC_BUFOPT_SET(bufd, 
				    GASNETC_FLAG_REPLY_HEADER);

			bufd->len = header_len;
		}

		#ifndef TRACE
		(void) gasnetc_AMReplyLongTrySend(bufd);
		#else
		{
			int payload = GASNETC_BUFOPT_ISSET(bufd, 
					GASNETC_FLAG_REPLY_PAYLOAD);
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
	int	hdr_len;
	va_list	argptr;
	unsigned int		len;
	gasnet_node_t		dest;
	gasnetc_bufdesc_t 	*bufd;

	retval = gasnet_AMGetMsgSource(token, &dest);
	if (retval != GASNET_OK) GASNETI_RETURN(retval);
	if_pf (dest >= gasnetc_nodes) 
		GASNETI_RETURN_ERRR(BAD_ARG,"node index too high");
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

	if_pf (nbytes > 0) {
		/* Also manage loopback by simply copying to the local
		 * destination */
		if_pf (dest == gasnetc_mynode) {
			GASNETE_FAST_ALIGNED_MEMCPY(
			    dest_addr, source_addr, nbytes);
			GASNETC_BUFOPT_SET(bufd, GASNETC_FLAG_REPLY_HEADER);
		}
		else {
			GASNETC_BUFOPT_SET(bufd, GASNETC_FLAG_REPLY_HEADER |
			    GASNETC_FLAG_REPLY_PAYLOAD |
			    GASNETC_FLAG_REPLY_ASYNC);
		}
	}
	else
		GASNETC_BUFOPT_SET(bufd, GASNETC_FLAG_REPLY_HEADER);

	#ifndef TRACE
	(void) gasnetc_AMReplyLongTrySend(bufd);
	#else
	{
		int payload = GASNETC_BUFOPT_ISSET(bufd, 
				GASNETC_FLAG_REPLY_PAYLOAD);
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
gasnetc_sendbuf_init()
{
	int	i, j;
	int	stoks, rtoks;
	int	dma_size;

	assert(_gmc.port != NULL);

	stoks = gm_num_send_tokens(_gmc.port);
	_gmc.stoks.max = stoks;
	_gmc.stoks.hi = _gmc.stoks.lo = _gmc.stoks.total = 0;

	rtoks = gm_num_receive_tokens(_gmc.port);
	_gmc.rtoks.max = rtoks;

	/* We need to allocate the following types of DMA'd buffers:
	 * 1. 1 AMReplyBuf (handling replies after an AMMediumRequest) 
	 * 2. (stoks-1) AMRequest bufs
	 * 3. rtoks AMRequest and AMReply receive bufs
	 * 4. 1 Scratch DMA buf (System Messages)
	 *
	 * Note that each of these have a bufdesc_t attached to them
	 */
	_gmc.bd_list_num = 1 + rtoks + stoks-1 + 1;
	dma_size = _gmc.bd_list_num << GASNETC_AM_SIZE;

	/* Allocate and register DMA buffers */ 
	_gmc.dma_bufs = gm_alloc_pages(dma_size);
	if_pf (_gmc.dma_bufs == NULL)
		gasneti_fatalerror("gm_alloc_pages(%d) %s", dma_size,
		   gasneti_current_loc);
	gm_register_memory(_gmc.port, _gmc.dma_bufs, dma_size);

	/* Allocate the AMRequest send buffer pool stack */
	_gmc.reqs_pool = (int *) 
	    gasneti_malloc(sizeof(int) * (stoks-1));

	/* Allocate a buffer descriptor (bufdesc_t) for each DMA'd buffer
	 * and fill in id/sendbuf for cheap reverse lookups */
	_gmc.bd_ptr = (gasnetc_bufdesc_t *)
	    gasneti_malloc(_gmc.bd_list_num * sizeof(gasnetc_bufdesc_t));
	for (i = 0; i < _gmc.bd_list_num; i++) {
		_gmc.bd_ptr[i].id = i;
		_gmc.bd_ptr[i].buf = (void *)
		    ((uint8_t *) _gmc.dma_bufs + (i<<GASNETC_AM_SIZE));
	}
	/* fifo_max is the last possible fifo index */
	_gmc.reqs_pool_cur = _gmc.reqs_pool_max = stoks-2;

	/* stoks-1 AMRequest send in FIFO */
	for (i = rtoks, j = 0; i < rtoks+stoks-1; i++, j++)
		_gmc.reqs_pool[j] = i;
	assert(j-1 == _gmc.reqs_pool_max);

	/* use the omitted send token for the AMReply buf */
	_gmc.AMReplyBuf = &_gmc.bd_ptr[i]; 
	_gmc.scratchBuf = (void *) ((uint8_t *) _gmc.dma_bufs + 
			    ((++i)<<GASNETC_AM_SIZE));
	_gmc.ReplyCount = 0;
	#if GASNETC_RROBIN_BUFFERS > 1
	_gmc.RRobinCount = 0;
	#endif
}

void
gasnetc_sendbuf_finalize()
{
	if (_gmc.dma_bufs != NULL)
		gm_free_pages(_gmc.dma_bufs, 
		    _gmc.bd_list_num << GASNETC_AM_SIZE);
	if (_gmc.bd_ptr != NULL)
		gasneti_free(_gmc.bd_ptr);
	if (_gmc.reqs_pool != NULL)
		gasneti_free(_gmc.reqs_pool);
}

void
gasnetc_provide_receive_buffers()
{
	int 	i;
	int	rtoks_hi, rtoks_lo;

	/* Extra check to make sure we recovered all our receive
	 * and send * tokens once the system is bootstrapped */
	assert(_gmc.stoks.max == gm_num_send_tokens(_gmc.port));
	assert(_gmc.rtoks.max == gm_num_receive_tokens(_gmc.port));

	_gmc.rtoks.lo = _gmc.rtoks.hi = _gmc.rtoks.total = 0;
	rtoks_lo = _gmc.rtoks.max/2;
	rtoks_hi = _gmc.rtoks.max - rtoks_lo;

	/* Provide GM with rtoks_lo AMRequest receive LOW */
	for (i = 0; i < rtoks_lo; i++) 
		gasnetc_provide_AMRequest_buffer(
		    (void *)((uint8_t *)_gmc.dma_bufs + (i<<GASNETC_AM_SIZE)));
	/* Provide GM with rtoks_hi AMReply receive HIGH */
	for (i = rtoks_lo; i < _gmc.rtoks.max; i++)
		gasnetc_provide_AMReply_buffer(
		    (void *)((uint8_t *)_gmc.dma_bufs + (i<<GASNETC_AM_SIZE)));

	if (gm_set_acceptable_sizes(_gmc.port, GM_HIGH_PRIORITY, 
			1<<GASNETC_AM_SIZE) != GM_SUCCESS)
		gasneti_fatalerror("can't set acceptable sizes for HIGH "
			"priority");
	if (gm_set_acceptable_sizes(_gmc.port, GM_LOW_PRIORITY, 
			1<<GASNETC_AM_SIZE) != GM_SUCCESS)
		gasneti_fatalerror("can't set acceptable sizes for LOW "
			"priority");
	gm_allow_remote_memory_access(_gmc.port);
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

void
gasnetc_tokensend_AMRequest(void *buf, uint32_t len, 
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
				assert(GASNETC_AM_IS_REQUEST(
				       *((uint8_t *) buf)));
				assert(len <= GASNETC_AM_PACKET);
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

gasnetc_bufdesc_t *
gasnetc_AMRequestPool_block() 
{
	int			 bufd_idx = -1;
	gasnetc_bufdesc_t	*bufd;

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
	assert(bufd_idx < _gmc.bd_list_num);
	assert(_gmc.bd_ptr[bufd_idx].buf != NULL);
	assert(_gmc.bd_ptr[bufd_idx].id == bufd_idx);
	return &_gmc.bd_ptr[bufd_idx];
}

/* This function is not thread safe as it is guarenteed to be called
 * from only one thread, during initialization */
void
gasnetc_bootstrapBarrier()
{
	int		count = 1;
	uintptr_t	*scratchPtr;

	gasneti_mutex_lock(&gasnetc_lock_gm);
	scratchPtr = (uintptr_t *) _gmc.scratchBuf;
	assert(scratchPtr != NULL);
	if (gasnetc_mynode == 0) {
		while (count < gasnetc_nodes) {
			gm_provide_receive_buffer(_gmc.port, 
			    (void *) scratchPtr, GASNETC_SYS_SIZE, 
			    GM_HIGH_PRIORITY);
			if (gasnetc_SysPoll((void *)&count) != BARRIER_GATHER)
				gasneti_fatalerror("System Barrier did not "
				    "receive a BARRIER_GATHER! fatal");
		}
		GASNETC_SYSHEADER_WRITE((uint8_t *)scratchPtr, BARRIER_NOTIFY);
		gasnetc_gm_send_AMSystem_broadcast((void *) scratchPtr, 1,
		    gasnetc_callback_hi, NULL, 0);
	}
	else {
		GASNETC_SYSHEADER_WRITE((uint8_t *)scratchPtr, BARRIER_GATHER);
		while (!gasnetc_token_hi_acquire()) {
			if (gasnetc_SysPoll((void *)-1) != _NO_MSG)
				gasneti_fatalerror("AMSystem_broadcast: "
					"unexpected message while "
					"recuperating tokens");
		}
		gasnetc_gm_send_AMSystem((void *) scratchPtr, 1,
			_gmc.gm_nodes[0].id, _gmc.gm_nodes[0].port, 
			gasnetc_callback_hi, NULL);
		GASNETI_TRACE_PRINTF(C, 
		    ("gasnetc_Sysbarrier: Sent GATHER, waiting for NOTIFY") );
		gm_provide_receive_buffer(_gmc.port, (void *) scratchPtr, 
		    GASNETC_SYS_SIZE, GM_HIGH_PRIORITY);
		if (gasnetc_SysPoll(NULL) != BARRIER_NOTIFY)
			gasneti_fatalerror("expected BARRIER_NOTIFY, fatal");
	}
	gasneti_mutex_unlock(&gasnetc_lock_gm);
	return;
}
			
/*
 * Rewrite the gather-type functions to work with exchangefunction typedef from
 * gasnet internal functions.
 *
 * The boostrapExchange function sends src of length 'len' to zero and waits
 * for a result from zero, which is copied in dest.
 *
 */
void
gasnetc_bootstrapExchange(void *src, size_t len, void *dest)
{
	void		*exch_hdr, *exch;
	void		*recv;

	GASNETI_TRACE_PRINTF(C,("gasnetc_bootstrapExchange(%i bytes)",len));
	gasneti_mutex_lock(&gasnetc_lock_gm);

	exch_hdr = (void *) _gmc.scratchBuf;
	exch = (void *) ((uint32_t *) _gmc.scratchBuf + 1);

	if ((len*gasnetc_nodes+4) > (1U<<GASNETC_SYS_SIZE))
		gasneti_fatalerror(
		    "bootstrapExchange: %i bytes too large for system message\n",
		    len);

	if (gasnetc_mynode == 0) {
		int count = 1;
		memcpy(dest, src, len);

		while (count < gasnetc_nodes) {

			gm_provide_receive_buffer(_gmc.port, exch_hdr,
			    GASNETC_SYS_SIZE, GM_HIGH_PRIORITY);

			if (gasnetc_SysPoll(dest) != EXCHANGE_GATHER)
				gasneti_fatalerror(
				    "expected EXCHANGE_GATHER, fatal");

			count++;
		}

		/* Prepare the global segment info to be broadcasted */
		GASNETC_SYSHEADER_WRITE((uint8_t *) exch_hdr, EXCHANGE_BROADCAST);
		memcpy(exch, dest, len*gasnetc_nodes);

		gasnetc_gm_send_AMSystem_broadcast(exch_hdr, len*gasnetc_nodes+4,
		    gasnetc_callback_hi, NULL, 0);
	}
	else {
		GASNETC_SYSHEADER_WRITE((uint8_t *) exch_hdr, EXCHANGE_GATHER);
		memcpy(exch, src, len);

		while (!gasnetc_token_hi_acquire()) {
			if (gasnetc_SysPoll((void *)-1) != _NO_MSG)
				gasneti_fatalerror("AMSystem_broadcast: "
				    "unexpected message while recovering tokens");
		}

		/* Send the seginfo message to the master (node 0) */
		gasnetc_gm_send_AMSystem(exch_hdr, len+4, 
		    _gmc.gm_nodes[0].id, _gmc.gm_nodes[0].port, 
		    gasnetc_callback_hi, NULL);

		gm_provide_receive_buffer(_gmc.port, exch_hdr, GASNETC_SYS_SIZE, 
		    GM_HIGH_PRIORITY);

		if (gasnetc_SysPoll((void *) dest) != EXCHANGE_BROADCAST)
			gasneti_fatalerror("expected EXCHANGE_BROADCAST, fatal");
	}
	gasneti_mutex_unlock(&gasnetc_lock_gm);
	return;
}

/* -------------------------------------------------------------------------- */
void
gasnetc_gm_send_AMSystem_broadcast(void *buf, size_t len,
		gm_send_completion_callback_t callback,
		void *callback_ptr, int recover)
{
	int			i;
	int			token_hi;
	gasnetc_sysmsg_t	sysmsg;

	gasneti_mutex_assertlocked(&gasnetc_lock_gm);
	assert(buf != NULL);
	assert(len >= 1);
	assert(callback != NULL);
	assert(gasnetc_mynode == 0);

	if (recover) 
		token_hi = _gmc.stoks.hi;

	GASNETI_TRACE_PRINTF(C, ("gm_send_AMSystemBroadcast len=%d data=0x%x",
	     len, *((uint8_t *)buf)) );

	for (i = 1; i < gasnetc_nodes; i++) {
		while (!gasnetc_token_hi_acquire()) {
			if (gasnetc_SysPoll((void *)-1) != _NO_MSG)
				gasneti_fatalerror("AMSystem_broadcast: "
				    "unexpected message while "
				    "recuperating tokens");
		}
		gasnetc_gm_send_AMSystem(buf, len, _gmc.gm_nodes[i].id,
		    _gmc.gm_nodes[i].port, callback, callback_ptr);
	}

	if (recover) {
		while (_gmc.stoks.hi != token_hi) {
			if (gasnetc_SysPoll((void *)-1) != _NO_MSG)
				gasneti_fatalerror("AMSystem_broadcast: "
				    "unexpected message while "
				    "recuperating tokens");
		}
	}
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
	unsigned int	port_id, board_id, i;
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

int
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
	gm_status_t	status;
	struct gm_port	*p;

	if ((homedir = getenv("HOME")) == NULL)
		GASNETI_RETURN_ERRR(RESOURCE, "Couldn't find $HOME directory");

	if ((gmconfenv = getenv("GMPI_CONF")) != NULL)
		snprintf(gmconf, 128, "%s", gmconfenv);
	else
		snprintf(gmconf, 128, "%s/.gmpi/conf", homedir);

	if (gethostname(hostname, 128) < 0)
		GASNETI_RETURN_ERRR(RESOURCE, "Couldn't get local hostname");

	if ((fp = fopen(gmconf, "r")) == NULL) {
		fprintf(stderr, "Couldn't open GMPI configuration file\n: %s", 
		    gmconf);
		return GASNET_ERR_RESOURCE;
	}

	/* must do gm_init() from this point on since gm_host_name_to_node_id
	 * must use the port
	 */

	while (fgets(line, 128, fp)) {
	
		if (lnum == 0) {
	      		if ((sscanf(line, "%d\n", &numnodes)) < 1) 
				GASNETI_RETURN_ERRR(RESOURCE, 
				    "job size not found in GMPI config file");
	      		else if (numnodes < 1) 
				GASNETI_RETURN_ERRR(RESOURCE, 
				    "invalid numnodes in GMPI config file");

			if (!gasnetc_alloc_nodemap(numnodes))
				GASNETI_RETURN_ERRR(RESOURCE, 
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
					GASNETI_RETURN_ERRR(RESOURCE, 
					    "Invalid GM port");

				assert(gmhost != NULL);

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
		GASNETI_RETURN_ERRR(RESOURCE, 
		    "could not find myself in GMPI config file");
	gm_init();
	status = 
		gm_open(&p, GASNETC_DEFAULT_GM_BOARD_NUM, thisport,"GASNet/GM", 
		    GM_API_VERSION_1_4);
	if (status != GM_SUCCESS) 
		GASNETI_RETURN_ERRR(RESOURCE, "could not open GM port");
	status = gm_get_node_id(p, (unsigned int *) &thisid);
	if (status != GM_SUCCESS)
		GASNETI_RETURN_ERRR(RESOURCE, "could not get GM node id");

	for (i = 0; i < numnodes; i++) {
		_gmc.gm_nodes[i].id = 
		    gm_host_name_to_node_id(p, hostnames[i]);

		if (_gmc.gm_nodes[i].id == GM_NO_SUCH_NODE_ID) {
			fprintf(stderr, "%s (%d) has no id! Check mapper\n",
			    hostnames[i],
			    _gmc.gm_nodes[i].id);
			GASNETI_RETURN_ERRR(RESOURCE, 
			    "Unknown GMid or GM mapper down");
		}
		_gmc.gm_nodes_rev[i].id = _gmc.gm_nodes[i].id;
		_gmc.gm_nodes_rev[i].port = _gmc.gm_nodes[i].port;
		_gmc.gm_nodes_rev[i].node = (gasnet_node_t) i;

		GASNETI_TRACE_PRINTF(C, ("%d> %s (gm %d, port %d)\n", 
		    i, hostnames[i], _gmc.gm_nodes[i].id, 
		    _gmc.gm_nodes[i].port));

	}

	gasnetc_mynode = thisnode;
	gasnetc_nodes = numnodes;
	for (i = 0; i < numnodes; i++)
		gasneti_free_inhandler(hostnames[i]);
	gasneti_free_inhandler(hostnames);

	/* sort out the gm_nodes_rev for bsearch, glibc qsort uses recursion,
	 * so stack memory in order to complete the sort.  We want to minimize
	 * the number of mallocs
	 */
	qsort(_gmc.gm_nodes_rev, numnodes, sizeof(gasnetc_gm_nodes_rev_t),
	    gasnetc_gm_nodes_compare);
	_gmc.port = p;
	return GASNET_OK;
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
		if (sscanf(line, "Mem: %ld", &mem) > 0)
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

/* -------------------------------------------------------------------------- */
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
#if NEED_INTERRUPTS
  extern void gasnetc_hold_interrupts() {
    GASNETC_CHECKINIT();
    /* (...) add code here to disable handler interrupts for _this_ thread */
  }
  extern void gasnetc_resume_interrupts() {
    GASNETC_CHECKINIT();
    /* (...) add code here to re-enable handler interrupts for _this_ thread */
  }
#endif

/* ------------------------------------------------------------------------------------ */
/*
  Handler-safe locks
  ==================
*/

extern void gasnetc_hsl_init   (gasnet_hsl_t *hsl) {
  GASNETC_CHECKINIT();

  #ifdef GASNETI_THREADS
  { int retval = pthread_mutex_init(&(hsl->lock), NULL);
    if (retval) 
      gasneti_fatalerror("In gasnetc_hsl_init(), pthread_mutex_init()=%s",strerror(retval));
  }
  #endif

  /* (...) add code here to init conduit-specific HSL state */
}

extern void gasnetc_hsl_destroy(gasnet_hsl_t *hsl) {
  GASNETC_CHECKINIT();
  #ifdef GASNETI_THREADS
  { int retval = pthread_mutex_destroy(&(hsl->lock));
    if (retval) 
      gasneti_fatalerror("In gasnetc_hsl_destroy(), pthread_mutex_destroy()=%s",strerror(retval));
  }
  #endif

  /* (...) add code here to cleanup conduit-specific HSL state */
}

extern void gasnetc_hsl_lock   (gasnet_hsl_t *hsl) {
  GASNETC_CHECKINIT();

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

  /* (###) conduits with interrupt-based handler dispatch need to add code here to 
           disable handler interrupts on _this_ thread, (if this is the outermost
           HSL lock acquire and we're not inside an enclosing no-interrupt section)
   */
}

extern void gasnetc_hsl_unlock (gasnet_hsl_t *hsl) {
  GASNETC_CHECKINIT();

  /* (...) conduits with interrupt-based handler dispatch need to add code here to 
           re-enable handler interrupts on _this_ thread, (if this is the outermost
           HSL lock release and we're not inside an enclosing no-interrupt section)
   */

  GASNETI_TRACE_EVENT_TIME(L, HSL_UNLOCK, GASNETI_STATTIME_NOW()-hsl->acquiretime);

  #ifdef GASNETI_THREADS
  { int retval = pthread_mutex_unlock(&(hsl->lock));
    if (retval) 
      gasneti_fatalerror("In gasnetc_hsl_unlock(), pthread_mutex_unlock()=%s",strerror(retval));
  }
  #endif
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
  gasneti_handler_tableentry_with_bits(gasnetc_am_medcopy),
  { 0, NULL }
};

gasnet_handlerentry_t const *gasnetc_get_handlertable() {
  return gasnetc_handlers;
}

/* ------------------------------------------------------------------------------------ */


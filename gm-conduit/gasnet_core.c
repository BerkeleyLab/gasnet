/* $Id: gasnet_core.c,v 1.39 2003/06/11 04:45:31 bonachea Exp $
 * $Date: 2003/06/11 04:45:31 $
 * Description: GASNet GM conduit Implementation
 * Copyright 2002, Christian Bell <csbell@cs.berkeley.edu>
 * Copyright 2002, Dan Bonachea <bonachea@cs.berkeley.edu>
 * Terms of use are as specified in license.txt
 */

#include <gasnet.h>
#include <gasnet_internal.h>
#include <gasnet_handler.h>
#include <gasnet_core_internal.h>

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

gasneti_mutex_t gasnetc_lock_gm = GASNETI_MUTEX_INITIALIZER;
gasneti_mutex_t gasnetc_lock_reqpool = GASNETI_MUTEX_INITIALIZER;
gasneti_mutex_t gasnetc_lock_amreq = GASNETI_MUTEX_INITIALIZER;
gasnetc_state_t _gmc;

gasnet_handlerentry_t const		*gasnetc_get_handlertable();
extern gasnet_handlerentry_t const	*gasnete_get_handlertable();
extern gasnet_handlerentry_t const	*gasnete_get_extref_handlertable();

extern gasnet_handlerentry_t const	*gasnetc_get_rdma_handlertable();

extern void	gasnetc_rdma_init(uintptr_t segbase, uintptr_t segsize, 
				  uintptr_t global_physmem);
extern void	gasnetc_rdma_finalize();


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
	int retval = GASNET_OK, i = 0;

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
		gasnet_handlerentry_t *cdtable = 
		    (gasnet_handlerentry_t *)gasnetc_get_rdma_handlertable();
		int c_len = 0, cd_len = 0;
		int c_numreg = 0, cd_numreg = 0;

		assert(ctable && cdtable);
		while (ctable[c_len].fnptr) c_len++; /* calc len */
		while (cdtable[cd_len].fnptr) cd_len++; /* calc len */
		if (gasnetc_reghandlers(ctable, c_len, 1, 63, 0, &c_numreg)
		    != GASNET_OK)
			GASNETI_RETURN_ERRR(RESOURCE,
			    "Error registering core API handlers");
		assert(c_numreg == c_len);
		if (gasnetc_reghandlers(cdtable, cd_len, 1+c_len, 63, 0, 
		    &cd_numreg) != GASNET_OK)
			GASNETI_RETURN_ERRR(RESOURCE,
			    "Error registering core RDMA API handlers");
		assert(cd_numreg == cd_len);
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
		gasnetc_rdma_init((uintptr_t) gasnetc_seginfo[gasnetc_mynode].addr,
				  gasnetc_seginfo[gasnetc_mynode].size,
				  global_physmem);
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
			gasnetc_rdma_finalize();
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
    len = gasnetc_write_AMBufferShort(bufd->sendbuf, handler, numargs, 
		    argptr, GASNETC_AM_REQUEST);
    gasnetc_tokensend_AMRequest(bufd->sendbuf, len, 
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
    len = gasnetc_write_AMBufferMedium(bufd->sendbuf, handler, numargs, argptr, 
		 nbytes, source_addr, GASNETC_AM_REQUEST);
    gasnetc_tokensend_AMRequest(bufd->sendbuf, len, 
		  gasnetc_nodeid(dest), gasnetc_portid(dest),
		  gasnetc_callback_lo_bufd, (void *)bufd, 0);
    /* GASNETC_AMTRACE_RequestMedium(Send); */
  }

  va_end(argptr);
  if (retval) return GASNET_OK;
  else GASNETI_RETURN_ERR(RESOURCE);
}

GASNET_INLINE_MODIFIER(gasnetc_AMRequestLongM_DMA_inner)
void
gasnetc_AMRequestLongM_DMA_inner(gasnet_node_t dest, gasnet_handler_t handler,
		void *source_addr, size_t nbytes, void *dest_addr, int numargs, 
		va_list argptr)
{
	int	bytes_left = nbytes;
	int	port, id, len;
	uint8_t	*psrc, *pdest;
	gasnetc_bufdesc_t	*bufd;

	psrc  = (uint8_t *) source_addr;
	pdest = (uint8_t *) dest_addr;
	port  = gasnetc_portid(dest);
	id    = gasnetc_nodeid(dest);

	while (bytes_left >GASNETC_AM_LEN-GASNETC_LONG_OFFSET) {
		bufd = gasnetc_AMRequestPool_block();
		gasnetc_write_AMBufferBulk(bufd->sendbuf, 
			psrc, GASNETC_AM_LEN);
		gasnetc_tokensend_AMRequest(bufd->sendbuf, 
		   GASNETC_AM_LEN, id, port, gasnetc_callback_lo_bufd,
		   (void *) bufd, (uintptr_t) pdest);
		psrc += GASNETC_AM_LEN;
		pdest += GASNETC_AM_LEN;
		bytes_left -= GASNETC_AM_LEN;
	}
	bufd = gasnetc_AMRequestPool_block();
	bufd->dest_addr = (uintptr_t) dest_addr;
	bufd->source_addr = 0;
	bufd->rdma_len = nbytes; 
	bufd->node = dest;
	len =
	    gasnetc_write_AMBufferLong(bufd->sendbuf, 
	        handler, numargs, argptr, nbytes, source_addr, 
		(uintptr_t) dest_addr, GASNETC_AM_REQUEST);
	if (bytes_left > 0) {
		gasnetc_write_AMBufferBulk(
			(uint8_t *)bufd->sendbuf+GASNETC_LONG_OFFSET, 
			psrc, (size_t) bytes_left);
		gasnetc_tokensend_AMRequest(
		    (uint8_t *)bufd->sendbuf+GASNETC_LONG_OFFSET,
		    bytes_left, id, port, gasnetc_callback_lo, NULL,
		    (uintptr_t) pdest);
	}
	gasnetc_tokensend_AMRequest(bufd->sendbuf, len, id, 
	    port, gasnetc_callback_lo_bufd_rdma, (void *)bufd, 0);
}

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

	while (bytes_left >GASNETC_AM_LEN-GASNETC_LONG_OFFSET) {
		bufd = gasnetc_AMRequestPool_block();
		GASNETC_ARGPTR(dest_addr_ptr, (uintptr_t) pdest);
		len = gasnetc_write_AMBufferMedium(bufd->sendbuf,
		    gasneti_handleridx(gasnetc_am_medcopy), 
			GASNETC_ARGPTR_NUM, (va_list) dest_addr_ptr, 
			gasnet_AMMaxMedium(), (void *) psrc, GASNETC_AM_REQUEST);
		gasnetc_tokensend_AMRequest(bufd->sendbuf, len, id, 
		    port, gasnetc_callback_lo_bufd, (void *) bufd, 0);
		psrc += gasnet_AMMaxMedium();
		pdest += gasnet_AMMaxMedium();
		bytes_left -= gasnet_AMMaxMedium();
	}
	bufd = gasnetc_AMRequestPool_block();
	long_len =
	    gasnetc_write_AMBufferLong(bufd->sendbuf, 
	        handler, numargs, argptr, nbytes, source_addr, 
		(uintptr_t) dest_addr, GASNETC_AM_REQUEST);

	if (bytes_left > 0) {
		uintptr_t	pbuf;
		pbuf = (uintptr_t) bufd->sendbuf + (uintptr_t) long_len;
		GASNETC_ARGPTR(dest_addr_ptr, (uintptr_t) pdest);
		len = gasnetc_write_AMBufferMedium((void *)pbuf,
	    	    gasneti_handleridx(gasnetc_am_medcopy), 
		    GASNETC_ARGPTR_NUM, (va_list) dest_addr_ptr, 
		    bytes_left, (void *) psrc, GASNETC_AM_REQUEST);
		gasnetc_tokensend_AMRequest((void *)pbuf, len, id, port,
		    gasnetc_callback_lo, NULL, 0);
	}
	gasnetc_tokensend_AMRequest(bufd->sendbuf, long_len, id, port, 
	    gasnetc_callback_lo_bufd, (void *)bufd, 0);
	return;
}

extern int gasnetc_AMRequestLongM( gasnet_node_t dest,        /* destination node */
                            gasnet_handler_t handler, /* index into destination endpoint's handler table */ 
                            void *source_addr, size_t nbytes,   /* data payload */
                            void *dest_addr,                    /* data destination on destination node */
                            int numargs, ...)
{
	int	retval;
	va_list	argptr;

	gasnetc_bufdesc_t	*bufd;
	GASNETC_CHECKINIT();
  
	gasnetc_boundscheck(dest, dest_addr, nbytes);
	assert(nbytes <= gasnet_AMMaxLongRequest());
	if_pf (dest >= gasnetc_nodes) 
		GASNETI_RETURN_ERRR(BAD_ARG,"node index too high");
	if_pf (((uintptr_t)dest_addr)< ((uintptr_t)gasnetc_seginfo[dest].addr) ||
	    ((uintptr_t)dest_addr) + nbytes > 
	        ((uintptr_t)gasnetc_seginfo[dest].addr) + gasnetc_seginfo[dest].size) 
         	GASNETI_RETURN_ERRR(BAD_ARG,"destination address out of segment range");
	GASNETI_TRACE_AMREQUESTLONG(dest,handler,source_addr,nbytes,dest_addr,numargs);
	va_start(argptr, numargs); /*  pass in last argument */

	retval = 1;
	assert(nbytes <= GASNETC_AM_LONG_REQUEST_MAX);

	if (dest == gasnetc_mynode) {
		int	argbuf[GASNETC_AM_MAX_ARGS];

		GASNETC_ARGS_WRITE(argbuf, argptr, numargs);
		GASNETC_AMPAYLOAD_WRITE(dest_addr, source_addr, nbytes);
		GASNETC_RUN_HANDLER_LONG(_gmc.handlers[handler], (void *) -1, 
		    argbuf, numargs, dest_addr, nbytes);
	}
	else {
		/* XXX assert(GASNET_LONG_OFFSET >= LONG_HEADER) */
		if_pt (nbytes > 0) { /* Handle zero-length messages */
			if (gasnetc_is_pinned(dest, (uintptr_t) dest_addr, nbytes))
				gasnetc_AMRequestLongM_DMA_inner(dest, handler, 
				    source_addr, nbytes, dest_addr, numargs, 
				    argptr);
			else 
				gasnetc_AMRequestLongM_inner(dest, handler, 
				    source_addr, nbytes, dest_addr, numargs, 
				    argptr);
		}
		else {
			gasnetc_AMRequestLongM_inner(dest, handler, source_addr, 
			    nbytes, dest_addr, numargs, argptr);
		}
	}

	va_end(argptr);
	if (retval) return GASNET_OK;
	else GASNETI_RETURN_ERR(RESOURCE);
}

extern int gasnetc_AMRequestLongAsyncM( gasnet_node_t dest,        /* destination node */
                            gasnet_handler_t handler, /* index into destination endpoint's handler table */ 
                            void *source_addr, size_t nbytes,   /* data payload */
                            void *dest_addr,                    /* data destination on destination node */
                            int numargs, ...)
{
	int	retval;
	va_list	argptr;

	gasnetc_bufdesc_t	*bufd;
	GASNETC_CHECKINIT();
	
	gasnetc_boundscheck(dest, dest_addr, nbytes);
	assert(nbytes <= gasnet_AMMaxLongRequest());
	if_pf (dest >= gasnetc_nodes)
		GASNETI_RETURN_ERRR(BAD_ARG,"node index too high");
	if_pf (((uintptr_t)dest_addr)<((uintptr_t)gasnetc_seginfo[dest].addr) ||
	    ((uintptr_t)dest_addr) + nbytes > 
	        ((uintptr_t)gasnetc_seginfo[dest].addr)+gasnetc_seginfo[dest].size) 
		GASNETI_RETURN_ERRR(BAD_ARG,"destination address out of segment range");
	GASNETI_TRACE_AMREQUESTLONGASYNC(dest,handler,source_addr,nbytes,dest_addr,numargs);
	va_start(argptr, numargs); /*  pass in last argument */
	retval = 1;

	/* If remote address is not pinned, and Handle zero-length messages */
	if (!gasnetc_is_pinned(dest, (uintptr_t) dest_addr, nbytes) || nbytes == 0)
		gasnetc_AMRequestLongM_inner(dest, handler, source_addr, 
		    nbytes, dest_addr, numargs, argptr);
	/* If remote address is pinned, but local is not pinned */
	else if (!gasnetc_is_pinned(gasnetc_mynode, (uintptr_t)source_addr, nbytes))
		gasnetc_AMRequestLongM_DMA_inner(dest, handler, source_addr, 
		    nbytes, dest_addr, numargs, argptr);
	/* If remote and local address are pinned */
	else {
		uint16_t port, id;
		int	 len;

		port = gasnetc_portid(dest);
		id   = gasnetc_nodeid(dest);
		bufd = gasnetc_AMRequestPool_block();
		len =
		    gasnetc_write_AMBufferLong(bufd->sendbuf, 
		        handler, numargs, argptr, nbytes, source_addr, 
			(uintptr_t) dest_addr, GASNETC_AM_REQUEST);
		/* send the DMA first */
		bufd->source_addr = (uintptr_t) source_addr;
		bufd->dest_addr = (uintptr_t) dest_addr;
		bufd->rdma_len = nbytes;
		bufd->node = dest;
		gasnetc_tokensend_AMRequest(source_addr, nbytes, id, 
		    port, gasnetc_callback_lo_rdma, (void *) bufd, 
		    (uintptr_t) dest_addr);
		/* followed by the Long Header */
		gasnetc_tokensend_AMRequest(bufd->sendbuf, len, id, 
		    port, gasnetc_callback_lo_bufd, (void *)bufd, 0);
	}

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
    bufd->len = gasnetc_write_AMBufferShort(bufd->sendbuf, handler, 
		    numargs, argptr, GASNETC_AM_REPLY);
  
    gasneti_mutex_lock(&gasnetc_lock_gm);
    if (gasnetc_token_hi_acquire()) {
       /* GASNETC_AMTRACE_ReplyShort(Send); */
       gasnetc_gm_send_bufd(bufd);
    } else {
       /* GASNETC_AMTRACE_ReplyShort(Queued); */
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
    /* GASNETC_AMTRACE_ReplyMedium(Loopbk); */
    GASNETC_ARGS_WRITE(argbuf, argptr, numargs);
    GASNETC_RUN_HANDLER_MEDIUM(_gmc.handlers[handler], (void *) token,
				argbuf, numargs, loopbuf, nbytes);
  }
  else {
    if_pf (nbytes > GASNETC_AM_MEDIUM_MAX) 
	    GASNETI_RETURN_ERRR(BAD_ARG,"AMMedium Payload too large");
    bufd = gasnetc_bufdesc_from_token(token);
    bufd->len = 
	    gasnetc_write_AMBufferMedium(bufd->sendbuf, handler, numargs, 
                    argptr, nbytes, source_addr, GASNETC_AM_REPLY);
    gasneti_mutex_lock(&gasnetc_lock_gm);
    if (gasnetc_token_hi_acquire()) {
       /* GASNETC_AMTRACE_ReplyMedium(Send); */
       gasnetc_gm_send_bufd(bufd); 
    } else {
       /* GASNETC_AMTRACE_ReplyMedium(Queued); */
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
	
    		bufd = gasnetc_bufdesc_from_token(token);
		if (nbytes > 0 && gasnetc_is_pinned(dest, (uintptr_t) dest_addr, nbytes)) {
			pbuf = (uintptr_t) bufd->sendbuf + 
			    (uintptr_t) GASNETC_LONG_OFFSET;
			len =
			    gasnetc_write_AMBufferLong(bufd->sendbuf, handler,
			        numargs, argptr, nbytes, source_addr, 
				(uintptr_t) dest_addr, GASNETC_AM_REPLY);
			gasnetc_write_AMBufferBulk((void *)pbuf, source_addr, 
			    nbytes);

			bufd->node = dest;
			bufd->len = len;
			bufd->rdma_off = GASNETC_LONG_OFFSET; 
			bufd->rdma_len = nbytes;
			bufd->dest_addr = (uintptr_t) dest_addr;
			bufd->source_addr = 0;
				GASNETC_BUFDESC_OPT_SET(bufd, GASNETC_FLAG_DMA_SEND);
		}
		else {
			int32_t	dest_addr_ptr[2];
			int	long_len;
	
			long_len =
			    gasnetc_write_AMBufferLong(bufd->sendbuf, 
			        handler, numargs, argptr, nbytes, source_addr, 
				(uintptr_t) dest_addr, GASNETC_AM_REPLY);
			pbuf = (uintptr_t)bufd->sendbuf + (uintptr_t) long_len;
			GASNETC_ARGPTR(dest_addr_ptr, (uintptr_t) dest_addr);
			if_pt (nbytes > 0) { /* Handle zero-length messages */
				len = gasnetc_write_AMBufferMedium((void *)pbuf,
				    gasneti_handleridx(gasnetc_am_medcopy), 
				    GASNETC_ARGPTR_NUM, (va_list) dest_addr_ptr, 
				    nbytes, (void *) source_addr, 
				    GASNETC_AM_REPLY);
				GASNETC_BUFDESC_OPT_SET(bufd, 
				    GASNETC_FLAG_LONG_SEND);
				bufd->rdma_off = long_len; 
				bufd->rdma_len = len;
			}
			else {
				bufd->rdma_off = 0; 
				bufd->rdma_len = 0;
			}
			bufd->len = long_len;
			bufd->dest_addr = 0;
			bufd->source_addr = 0;
		}

		gasneti_mutex_lock(&gasnetc_lock_gm);
		if (gasnetc_token_hi_acquire()) {
			gasnetc_gm_send_bufd(bufd);

			if_pt (GASNETC_BUFDESC_OPT_ISSET(bufd, /* True in cases where nbytes > 0 */
			    GASNETC_FLAG_LONG_SEND | GASNETC_FLAG_DMA_SEND)) {

				GASNETC_BUFDESC_OPT_UNSET(bufd, 
				    GASNETC_FLAG_LONG_SEND | GASNETC_FLAG_DMA_SEND);

				if (gasnetc_token_hi_acquire()) {
					GASNETC_AMTRACE_ReplyLong(Send);
					gasnetc_gm_send_bufd(bufd);
				} 
				else {
					GASNETC_AMTRACE_ReplyLong(Queued);
					gasnetc_fifo_insert(bufd);
				}
			}
		} 
		else {
			GASNETC_AMTRACE_ReplyLong(Queued);
			gasnetc_fifo_insert(bufd);
		}
		gasneti_mutex_unlock(&gasnetc_lock_gm);
		GASNETI_TRACE_PRINTF(C, ("after enqueue to token=%p, buf=%p %hd:%hd", 
			    bufd, bufd->sendbuf, bufd->gm_id, bufd->gm_port));
	}
	va_end(argptr);
	if (retval) return GASNET_OK;
	else GASNETI_RETURN_ERR(RESOURCE);
}

/*
 * This is not officially part of the gasnet spec therefore not exported for the
 * user.  The extended API uses it in order to do directed sends followed by an
 * AMReply.  Therefore, there is no boundscheck of any sort.  The extended API
 * knows how/when to call this function.
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
	    gasnetc_write_AMBufferLong(bufd->sendbuf, handler, numargs, argptr, 
	        nbytes, source_addr, (uintptr_t) dest_addr, GASNETC_AM_REPLY);
	bufd->len = len;
	bufd->node = dest;
	bufd->rdma_off = 0;
	bufd->rdma_len = nbytes;
	bufd->dest_addr = (uintptr_t) dest_addr;
	bufd->source_addr = (uintptr_t) source_addr;
	if_pt (bufd->rdma_len > 0) /* Handle zero-length messages */
		GASNETC_BUFDESC_OPT_SET(bufd, 
		    GASNETC_FLAG_EXTENDED_DMA_SEND | GASNETC_FLAG_DMA_SEND);

	GASNETI_TRACE_PRINTF(C, ("AsyncReply has flags %d", bufd->flag));
	gasneti_mutex_lock(&gasnetc_lock_gm);
	if (gasnetc_token_hi_acquire()) {
		GASNETI_TRACE_PRINTF(C, ("??? sent Reply Payload"));
        	gasnetc_gm_send_bufd(bufd);
		GASNETC_BUFDESC_OPT_UNSET(bufd, 
		    GASNETC_FLAG_EXTENDED_DMA_SEND | GASNETC_FLAG_DMA_SEND);
		bufd->dest_addr = 0;
		if (gasnetc_token_hi_acquire()) {
			GASNETC_AMTRACE_ReplyLong(Send);
			gasnetc_gm_send_bufd(bufd);
		} 
		else {
			GASNETC_AMTRACE_ReplyLong(Queued);
			gasnetc_fifo_insert(bufd);
		}
	} 
	else {
		GASNETC_AMTRACE_ReplyLong(Queued);
		gasnetc_fifo_insert(bufd);
		GASNETI_TRACE_PRINTF(C, ("??? queued Payload has flags %d", bufd->flag));
	}
	gasneti_mutex_unlock(&gasnetc_lock_gm);
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


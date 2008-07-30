/*   $Source: /Users/kamil/work/gasnet-cvs2/gasnet/dcmf-conduit/gasnet_core.c,v $
 *     $Date: 2008/07/30 00:01:41 $
 * $Revision: 1.1.2.13 $
 * Description: GASNet dcmf conduit Implementation
 * Copyright 2002, Dan Bonachea <bonachea@cs.berkeley.edu>
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

#define GASNET_DCMF_EAGER_LIMIT_DEFAULT 1024
#define GASNETC_DEFAULT_SEG_SIZE 256*1024*1024


/*turns on the ability for the conduit to send negative acknowledgments*/
/*without this we will always blindly accept all incoming active messages*/
#define GASNETC_FLOW_CONTROL_ENABLED 0

#if !GASNET_SEQ
#error ONLY SEQ BUILDS SUPPORTED FOR NOW... Stay tuned for PAR/PARSYNC support
#endif

gasnet_handlerentry_t const *gasnetc_get_handlertable(void);
static void gasnetc_atexit(void);

#define GASNETC_MAX_NUMHANDLERS   256
typedef void (*gasnetc_handler_fn_t)();  /* prototype for handler function */
gasnetc_handler_fn_t gasnetc_handler[GASNETC_MAX_NUMHANDLERS]; /* handler table (recommended impl) */


/*
  File-Scoped Global Variables
  (All variables need to be initialized in init)
  ==============
*/
#define GASNETC_INIT_NUM_REQ 100
#define GASNETC_INIT_NUM_TOKENS 100
/*each of the handlers is an object thats about 1k so limit the number we have initially*/
#define GASNETC_INIT_NUM_AMHANDLERS 25

static size_t gasnetc_active_amhandlers = 0;

static gasneti_lifo_head_t gasnetc_dcmf_req_free_list = GASNETI_LIFO_INITIALIZER;
static gasneti_lifo_head_t gasnetc_dcmf_nack_req_free_list = GASNETI_LIFO_INITIALIZER;
static gasneti_lifo_head_t gasnetc_token_free_list = GASNETI_LIFO_INITIALIZER;
static gasneti_lifo_head_t gasnetc_amhandler_free_list = GASNETI_LIFO_INITIALIZER;
static gasneti_lifo_head_t gasnetc_ambuf_free_list = GASNETI_LIFO_INITIALIZER;
static gasneti_lifo_head_t gasnetc_replay_buffer_free_list = GASNETI_LIFO_INITIALIZER;

static gasnetc_fifo_t gasnetc_nack_list = GASNETC_FIFO_INITIALIZER;
static gasnetc_fifo_t gasnetc_amhandler_active_list = GASNETC_FIFO_INITIALIZER;


static size_t   gasnetc_dcmf_eager_limit;
static unsigned gasnetc_curr_seq_number;

static DCMF_Protocol_t gasnetc_dcmf_ack_registration;
static DCMF_Protocol_t gasnetc_dcmf_nack_registration;


/*keep track of hte number of local send replay buffers we have left to allocate. 
 This value is always decremetned and never incremented. Once it hits 0 then we need to stall until we
get enough acks to clear old AMs.*/
#define GASNETC_DEFAULT_MAX_REPLAY_BUFFERS 1024
static gasneti_semaphore_t gasnetc_replay_buffers_left;

/*keep track of how many incoming buffers we have available. When this number reaches 0 that means that 
	we have to send NACKs back to the sender indicating a failure to recv the AM. When an active message
  is finished the buffer is cleared and this number is increased (in that order)*/
#define GASNETC_DEFAULT_MAX_INCOMING_BUFFERS 1024
static gasneti_semaphore_t gasnetc_incoming_buffers_available;

/* ------------------------------------------------------------------------------------ */

void gasnetc_dcmf_handle_am_short(void *clientdata,
				  const DCQuad *msginfo,
				  unsigned count, 
				  unsigned peer,
				  const char *src,
				  unsigned bytes);
DCMF_Request_t* gasnetc_dcmf_handle_am_header(void *clientdata,
					      const DCQuad *msginfo, unsigned count, 
					      unsigned peer, unsigned sendlen,
					      unsigned *rcvlen, char **rcvbuf,
					      DCMF_Callback_t *cb_done);

void gasnetc_free_dcmf_req_cb(void *req);

void gasnetc_ack_msg_cb(void *client_data,
		const DCMF_Control_t *info,
		unsigned peer);

void gasnetc_add_to_nack_list_cb(void *client_data,
		const DCMF_Control_t *info,
		unsigned peer);




static void gasnetc_inc_uint64_arg_cb(void* arg) {
  uint64_t *in = (uint64_t*) arg;
  (*in)++;
}

static void gasnetc_inc_uint32_arg_cb(void* arg) {
  uint32_t *in = (uint32_t*) arg;
  (*in)++;
}

static void gasnetc_inc_uint8_arg_cb(void* arg) {
  uint8_t *in = (uint8_t*) arg;
  (*in)++;
}

gasnetc_dcmf_amregistration_t *gasnetc_dcmf_amregistration[GASNETC_NUM_AMTYPES][GASNETC_NUM_AMCATS][GASNETC_DCMF_NUM_SENDCATS];



GASNETI_INLINE(gasnetc_get_protocol) 
DCMF_Send_Protocol gasnetc_get_protocol(gasnetc_dcmf_send_category_t sendcat) {
  gasneti_assert(sendcat < GASNETC_DCMF_NUM_SENDCATS);
  switch(sendcat) {
  case GASNETC_DCMF_SEND_DEFAULT: return DCMF_DEFAULT_SEND_PROTOCOL;
  case GASNETC_DCMF_SEND_EAGER: return DCMF_EAGER_SEND_PROTOCOL;
  case GASNETC_DCMF_SEND_RVOUS: return DCMF_RZV_SEND_PROTOCOL;
  default: gasneti_fatalerror("unknown send category"); return 0;
  }
}

#define REGISTER_SEND_HANDLER(AMTYPE, AMCATEGORY, SENDPROTOCOL) do {	\
    DCMF_Send_Configuration_t config;					\
    config.protocol = gasnetc_get_protocol(SENDPROTOCOL);			\
    config.cb_recv_short = gasnetc_dcmf_handle_am_short;		\
    config.cb_recv_short_clientdata = NULL;				\
    config.cb_recv = gasnetc_dcmf_handle_am_header;			\
    config.cb_recv_clientdata = NULL;					\
    DCMF_SAFE(DCMF_Send_register(&GASNETC_DCMF_AM_REGISTARTION(AMTYPE, AMCATEGORY,SENDPROTOCOL), &config)); \
  } while(0);

void gasnetc_dcmf_init(gasnet_node_t* mynode, gasnet_node_t *nodes) {
	int i,j,k;
	int ret;
  DCMF_CriticalSection_enter(0);
  ret = DCMF_Messager_initialize();
	
	if(ret==0) {
		GASNETI_TRACE_PRINTF(C,("DCMF already intialized... first from MPI?"));
  } else {
		GASNETI_TRACE_PRINTF(C,("DCMF successfully intialized"));
	}
    
  *mynode = DCMF_Messager_rank();
  *nodes = DCMF_Messager_size();
  DCMF_CriticalSection_exit(0);
    
  gasnetc_dcmf_bootstrap_coll_init();
    
    
  /***Initialize all the active message handlers*/
	GASNETI_TRACE_PRINTF(C,("amregistraion (%d bytes) each", (int) sizeof(gasnetc_dcmf_amregistration_t)));
  for(i=0; i<GASNETC_NUM_AMTYPES; i++) {
    for(j=0; j<GASNETC_NUM_AMCATS; j++) {
      for(k=0; k<GASNETC_DCMF_NUM_SENDCATS; k++) {
				gasnetc_dcmf_amregistration[i][j][k] = gasneti_malloc(sizeof(gasnetc_dcmf_amregistration_t));
				REGISTER_SEND_HANDLER(i,j,k);
      }	
    }
  }
    
  {
  	DCMF_Control_Configuration_t ack_config, nack_config;
  	nack_config.protocol = ack_config.protocol =DCMF_DEFAULT_CONTROL_PROTOCOL;
  	ack_config.cb_recv = gasnetc_ack_msg_cb;
  	nack_config.cb_recv = gasnetc_add_to_nack_list_cb;
  	ack_config.cb_recv_clientdata = nack_config.cb_recv_clientdata = NULL;
  	
  	DCMF_SAFE(DCMF_Control_register(&gasnetc_dcmf_ack_registration, &ack_config));
  	DCMF_SAFE(DCMF_Control_register(&gasnetc_dcmf_nack_registration, &nack_config));
  }
    

  do {
    DCMF_Hardware_t hw;
    DCMF_SAFE(DCMF_Hardware(&hw));
    GASNETI_TRACE_PRINTF(C,("(x,y,z,t) Coords: (%d,%d,%d,%d) Sizes: (%d,%d,%d,%d) isTorus?: (%d,%d,%d,%d)",
			    hw.xCoord, hw.yCoord, hw.zCoord, hw.tCoord,
			    hw.xSize, hw.ySize, hw.zSize, hw.tSize,
			    hw.xTorus, hw.yTorus, hw.zTorus, hw.tTorus));
	
  } while(0);

}

void gasnetc_dcmf_finalize() {
  DCMF_Messager_finalize();
}

/*
  Initialization
  ==============
*/
/* called at startup to check configuration sanity */
static void gasnetc_check_config() {
  gasneti_check_config_preinit();

  /* (###) add code to do some sanity checks on the number of nodes, handlers
   * and/or segment sizes */ 
}



static void gasnetc_bootstrapBarrier() {
  gasnetc_dcmf_bootstrapBarrier();
}

static void gasnetc_bootstrapBroadcast(void *src, size_t len, void *dest, int rootnode) {
  gasnetc_dcmf_bootstrapBroadcast(src,len,dest,rootnode);
}

static void gasnetc_bootstrapExchange(void *src, size_t len, void *dest) {
  gasnetc_dcmf_bootstrapExchange(src,len,dest);
}


static int gasnetc_init(int *argc, char ***argv) {
  int i;
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
  gasnetc_dcmf_init(&gasneti_mynode, &gasneti_nodes);

#if GASNET_DEBUG_VERBOSE
  fprintf(stderr,"gasnetc_init(): spawn successful - node %i/%i starting...\n", 
	  gasneti_mynode, gasneti_nodes); fflush(stderr);
#endif

#if GASNET_SEGMENT_FAST || GASNET_SEGMENT_LARGE
  { 
    gasneti_segmentInit(GASNETC_DEFAULT_SEG_SIZE, gasnetc_bootstrapExchange);
  }
#elif GASNET_SEGMENT_EVERYTHING
  /* segment is everything - nothing to do */
#else
#error Bad segment config
#endif

#if 1
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
  
  /*initialize file scoped global variables*/

	/*XXX: change these to be environment tunable values*/
	gasneti_semaphore_init(&gasnetc_replay_buffers_left, GASNETC_DEFAULT_MAX_REPLAY_BUFFERS, GASNETI_SEMAPHORE_MAX);
	gasneti_semaphore_init(&gasnetc_incoming_buffers_available, GASNETC_DEFAULT_MAX_INCOMING_BUFFERS, GASNETI_SEMAPHORE_MAX);

	gasnetc_active_amhandlers=0;
  gasnetc_curr_seq_number = 0;

  
	gasnetc_dcmf_eager_limit = gasneti_getenv_int_withdefault("GASNET_DCMF_EAGER_LIMIT", GASNET_DCMF_EAGER_LIMIT_DEFAULT, 1);
  
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

  /*  (###) register any custom signal handlers required by your conduit 
   *        (e.g. to support interrupt-based messaging)
   */

  atexit(gasnetc_atexit);

  /* ------------------------------------------------------------------------------------ */
  /*  register segment  */

  gasneti_seginfo = (gasnet_seginfo_t *)gasneti_malloc(gasneti_nodes*sizeof(gasnet_seginfo_t));

#if GASNET_SEGMENT_FAST || GASNET_SEGMENT_LARGE
  if (segsize == 0) segbase = NULL; /* no segment */
  else {
    gasneti_segmentAttach(segsize, minheapoffset, gasneti_seginfo,
			  gasnetc_bootstrapExchange);
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

  /*done through segmentAttach*/

  /* ------------------------------------------------------------------------------------ */
  /*  primary attach complete */
  gasneti_attach_done = 1;
  gasnetc_bootstrapBarrier();
  
  GASNETI_TRACE_PRINTF(C,("gasnetc_attach(): primary attach complete :"GASNETI_LADDRFMT" size: %lu", GASNETI_LADDRSTR(segbase), (unsigned long)segsize));
  
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
	
	fprintf(stderr, "%d> @ exit num active handelrs = %d\n", gasneti_mynode, gasnetc_active_amhandlers);

  GASNETI_TRACE_PRINTF(C,("gasnet_exit(%i)\n", exitcode));

  gasneti_flush_streams();
  gasneti_trace_finish();
  gasneti_sched_yield();


  gasnetc_dcmf_finalize();

  
  gasneti_killmyprocess(exitcode);
  /* (###) add code here to terminate the job across _all_ nodes 
     with gasneti_killmyprocess(exitcode) (not regular exit()), preferably
     after raising a SIGQUIT to inform the client of the exit
  */

  gasneti_fatalerror("gasnetc_exit failed!");
}

/* ------------------------------------------------------------------------------------ */
/*
  Utility Functions/Macros
  ================================
*/
/*
 * 1 bit for request/reply
 * 2 bits for active msg category (short, medium, long, longasync)
 * 5 bits for number of arguments
 * 8 bits for active message handler function
 */

/* bit 0: Request (=0) / Reply (=1)
 * bit 1:2 Active Msg Category (0 = short, 1=medium, 2=long, 3=longasync)*
 * bit 3:7 Number of Arguments (upto 32)
 * bit 8:15 Active Messager Handler Index
 */


#define GASNETC_GET_BITS(WORD, STARTBIT, NUMBITS) (((WORD) & (((1<<(NUMBITS))-1)<<(STARTBIT)))>>(STARTBIT))
#define GASNETC_GET_ACTIVE_MSG_TYPE(HEADER_QUAD) GASNETC_GET_BITS((HEADER_QUAD).w0, 0, 1)
#define GASNETC_GET_ACTIVE_MSG_CATEGORY(HEADER_QUAD) GASNETC_GET_BITS((HEADER_QUAD).w0, 1, 2)
#define GASNETC_GET_ACTIVE_MSG_NUMARGS(HEADER_QUAD) GASNETC_GET_BITS((HEADER_QUAD).w0, 3, 5)
#define GASNETC_GET_ACTIVE_MSG_HANDLERIDX(HEADER_QUAD) GASNETC_GET_BITS((HEADER_QUAD).w0, 8, 8)

#define GASNETC_GET_ACTIVE_MSG_DSTADDR(HEADER_QUAD) ((HEADER_QUAD).w1)
#define GASNETC_GET_ACTIVE_MSG_REMOTE_REPLAY_BUFFER(HEADER_QUAD) ((HEADER_QUAD).w2)
#define GASNETC_GET_ACTIVE_MSG_NBYTES(HEADER_QUAD) ((HEADER_QUAD).w3)

#define GASNETC_MAKE_HEADER_WORD(TYPE, CATEGORY, NUMARGS, HANDLERIDX)	\
  (gasneti_assert((NUMARGS) < GASNETC_MAX_AM_ARGS),			\
   (((HANDLERIDX)<<8) | ((NUMARGS)<<3) | ((CATEGORY)<<1) | (TYPE)))



#define GASNETC_PACK_ARG_QUADS(NUMARGS, ARGPTR, DCQUADS, NUMQUADS_PTR) do { \
    int i=(NUMARGS);							\
    int j=0;								\
    int numquads=0;							\
    gasneti_assert((NUMARGS)<=GASNETC_MAX_AM_ARGS);			\
    while(i>0) {							\
      switch(i) {							\
      case 3: /*only three args left... fill first three */		\
      	(DCQUADS)[j].w0 = va_arg((ARGPTR), gasnet_handlerarg_t);	\
				(DCQUADS)[j].w1 = va_arg((ARGPTR), gasnet_handlerarg_t);	\
				(DCQUADS)[j].w2 = va_arg((ARGPTR), gasnet_handlerarg_t);	\
				i-=3; numquads++; break;					\
      case 2: /*fill first two*/					\
      	(DCQUADS)[j].w0 = va_arg((ARGPTR), gasnet_handlerarg_t);	\
      	(DCQUADS)[j].w1 = va_arg((ARGPTR), gasnet_handlerarg_t);	\
      	i-=2; numquads++; break;					\
      case 1: /*fill only one*/						\
      	(DCQUADS)[j].w0 = va_arg((ARGPTR), gasnet_handlerarg_t);	\
      	numquads++; i-=1; break;					\
      default: /*at least four arguments left*/				\
      	(DCQUADS)[j].w0 = va_arg((ARGPTR), gasnet_handlerarg_t);	\
      	(DCQUADS)[j].w1 = va_arg((ARGPTR), gasnet_handlerarg_t);	\
      	(DCQUADS)[j].w2 = va_arg((ARGPTR), gasnet_handlerarg_t);	\
      	(DCQUADS)[j].w3 = va_arg((ARGPTR), gasnet_handlerarg_t);	\
      	numquads++; i-=4; j++; break;					\
    }}								\
    *(NUMQUADS_PTR) = numquads; gasneti_assert(numquads < GASNETC_MAXQUADS_PER_AM); \
  } while (0)

#define GASNETC_UNPACK_ARG_QUADS(NUMQUADS, DCQUADS, NUMARGS, HANDLERARGS) do { \
    int i=(NUMARGS);				                                			\
    int k=0;								                                      \
    int j=0;								                                      \
    gasneti_assert((NUMARGS)<=GASNETC_MAX_AM_ARGS);			          \
    gasneti_assert((NUMQUADS) < GASNETC_MAXQUADS_PER_AM);		      \
    while(i>0) {							                                    \
      switch(i) {							                                    \
      case 3: /*only three args left... extract first three */		\
      	HANDLERARGS[k] = (gasnet_handlerarg_t)(DCQUADS)[j].w0;		\
      	HANDLERARGS[k+1] = (gasnet_handlerarg_t)(DCQUADS)[j].w1;	\
      	HANDLERARGS[k+2] = (gasnet_handlerarg_t)(DCQUADS)[j].w2;	\
      	i-=3; numquads++; k+=3; break;					                  \
      case 2: /*extract first two*/					                      \
      	HANDLERARGS[k] = (gasnet_handlerarg_t)(DCQUADS)[j].w0;		\
      	HANDLERARGS[k+1] = (gasnet_handlerarg_t)(DCQUADS)[j].w1;	\
      	i-=2; numquads++; k+=2; break;					                  \
      case 1: /*extract only one*/					                      \
      	HANDLERARGS[k] = (gasnet_handlerarg_t)(DCQUADS)[j].w0;		\
      	numquads++; i-=1; k+=1; break;					                  \
      default: /*at least four arguments left..extract all*/		  \
      	HANDLERARGS[k] = (gasnet_handlerarg_t)(DCQUADS)[j].w0;		\
      	HANDLERARGS[k+1] = (gasnet_handlerarg_t)(DCQUADS)[j].w1;	\
      	HANDLERARGS[k+2] = (gasnet_handlerarg_t)(DCQUADS)[j].w2;	\
      	HANDLERARGS[k+3] = (gasnet_handlerarg_t)(DCQUADS)[j].w3;	\
      	numquads++; i-=4; j++; k+=4; break;				                \
      }}								                                          \
  } while (0)

#define GASNETC_MAKE_HEADER_QUAD(TYPE, CATEGORY, NUMARGS, HANDLERIDX, DST_ADDR,  REPLAY_BUFFER, NBYTES, DCQUAD_PTR) do { \
    DCQUAD_PTR[0].w0 = GASNETC_MAKE_HEADER_WORD(TYPE, CATEGORY, NUMARGS, HANDLERIDX); \
    DCQUAD_PTR[0].w1 = (unsigned) DST_ADDR;				\
    DCQUAD_PTR[0].w2 = (unsigned) REPLAY_BUFFER;						\
    DCQUAD_PTR[0].w3 = NBYTES;						\
  } while (0)



/*
  Managing Internal Data Structures
  ==============================
*/


GASNETI_INLINE(gasnetc_get_dcmf_req) 
gasnetc_dcmf_req_t * gasnetc_get_dcmf_req() {
  gasnetc_dcmf_req_t *req;

  req = gasneti_lifo_pop(&gasnetc_dcmf_req_free_list);
	if(!req) {
		/*must allocate new structure*/
		GASNETI_TRACE_PRINTF(C, ("malloc of req (%d bytes)", sizeof(gasnetc_dcmf_req_t)));
		req = (gasnetc_dcmf_req_t*) gasneti_malloc(sizeof(gasnetc_dcmf_req_t));
	}
  return req;
}

GASNETI_INLINE(gasnetc_free_dcmf_req) 
void gasnetc_free_dcmf_req(gasnetc_dcmf_req_t *req){
  /*add it back tot he free list*/
  gasneti_lifo_push(&gasnetc_dcmf_req_free_list,(void*) req);
}



/*callback passed to DCMF_Send() that just frees the associated dcmf request object*/
void gasnetc_free_dcmf_req_cb(void *arg){
  gasnetc_free_dcmf_req((gasnetc_dcmf_req_t*) arg);
}

GASNETI_INLINE(gasnetc_get_dcmf_nack_req) 
gasnetc_dcmf_nack_req_t * gasnetc_get_dcmf_nack_req(unsigned peer, unsigned remote_replay_buffer) {
  gasnetc_dcmf_nack_req_t *req;

  req = gasneti_lifo_pop(&gasnetc_dcmf_nack_req_free_list);
	if(!req) {
		/*must allocate new structure*/
		req = (gasnetc_dcmf_nack_req_t*) gasneti_malloc(sizeof(gasnetc_dcmf_nack_req_t));
	}
	req->peer = peer;
	req->remote_replay_buffer = remote_replay_buffer;
  return req;
}

GASNETI_INLINE(gasnetc_free_dcmf_nack_req) 
void gasnetc_free_dcmf_nack_req(gasnetc_dcmf_nack_req_t *req){
  /*add it back tot he free list*/
  gasneti_lifo_push(&gasnetc_dcmf_nack_req_free_list,(void*) req);
}

/*build a new token and fill the header information*/
GASNETI_INLINE(gasnetc_construct_token) 
gasnetc_token_t *gasnetc_construct_token(gasnet_node_t srcnode, gasnetc_dcmf_amtype_t amtype, 
						       gasnetc_dcmf_amcategory_t amcat, unsigned remote_replay_buffer, int allocate_dcmf_req){
  
  gasnetc_token_t *token;
  /*check the free list before allocating a new one*/

	token = gasneti_lifo_pop(&gasnetc_token_free_list);
	if(!token)  {
		GASNETI_TRACE_PRINTF(C, ("malloc of token (%d bytes)", sizeof(gasnetc_token_t)));
    token = (gasnetc_token_t*)gasneti_malloc(sizeof(gasnetc_token_t)); 
  }
  /*fill the token with the arguemtns passed in*/
  token->srcnode = srcnode;
  token->amcat = amcat;
  token->amtype = amtype;
  token->remote_replay_buffer=remote_replay_buffer;
  token->sent_reply = 0;

  /*if this token needs a DCMF request object associated with it, allocate one*/
  if(allocate_dcmf_req) {
    token->dcmf_req = gasnetc_get_dcmf_req();
  } else {
    token->dcmf_req = NULL;
  }
  return token;
}

/*free the dcmf request if we built one and then add it back to the free list*/
GASNETI_INLINE(gasnetc_free_token) 
void gasnetc_free_token(gasnetc_token_t* token){
  if(token->dcmf_req) gasnetc_free_dcmf_req(token->dcmf_req);
	gasneti_lifo_push(&gasnetc_token_free_list,(void*) token);
}

/*this function allocates the temporary buffer space for AM Mediums either
 * on the sending side or the receiving side. Both places where we call the function
 *  are protected by a semaphore that ensures we don't allocate too many
 */
GASNETI_INLINE(gasnetc_get_ambuf)
gasnetc_ambuf_t* gasnetc_get_ambuf(size_t nbytes) {
	gasnetc_ambuf_t *ret;
	/*size argument just used for arg checking and future use*/
	gasneti_assert(nbytes <= gasnet_AMMaxMedium());

	ret = gasneti_lifo_pop(&gasnetc_ambuf_free_list);
	if(!ret) {
		GASNETI_TRACE_PRINTF(C, ("malloc of am buffer"));
		ret = gasneti_malloc(sizeof(gasnetc_ambuf_t));
	}
	GASNETI_TRACE_PRINTF(C, ("allocating ambuffer: %p (%p)", ret, ret->data));
	return ret;
}

GASNETI_INLINE(gasnetc_free_ambuf)
void gasnetc_free_ambuf(gasnetc_ambuf_t *buffer) {
	GASNETI_TRACE_PRINTF(C, ("freeing ambuffer before: %p", buffer));
	gasneti_lifo_push(&gasnetc_ambuf_free_list, (void*) buffer);
}

GASNETI_INLINE(gasnetc_get_replay_buffer)
gasnetc_replay_buffer_t* gasnetc_get_replay_buffer(size_t nbytes, int allocate_buffer) {
	gasnetc_replay_buffer_t *ret;
	
	/*first see if we can pop one off the free list*/
	ret = gasneti_lifo_pop(&gasnetc_replay_buffer_free_list);
	
	if(!ret){ 
		/*the free list is empty so check if we are allowed to allocate a new one*/
		if(gasneti_semaphore_trydown(&gasnetc_replay_buffers_left)) {
			/*we haven't exceeded our max number of allocateable replay buffers so create one*/
			ret = (gasnetc_replay_buffer_t*) gasneti_malloc(sizeof(gasnetc_replay_buffer_t));
		} else {
			/* we have exceeeded our maxiumum allocateable number of buffers and there are none left
			 * so we have to wait until more are available*/
			ret = gasneti_lifo_pop(&gasnetc_replay_buffer_free_list);
			DCMF_CriticalSection_enter(0);
			while(ret==NULL) {
					DCMF_Messager_advance(); /*the polling loop will eventually run an ACK callback which will deallocate a 
					replay buffer*/
					ret = gasneti_lifo_pop(&gasnetc_replay_buffer_free_list);
			}
			DCMF_CriticalSection_exit(0);
		}
	}

	if(nbytes > 0 && allocate_buffer == 1) {
		ret->buffer = gasnetc_get_ambuf(nbytes);
	} else {
		ret->buffer = NULL;
	}
	
	ret->retry_count = 0;
	GASNETI_TRACE_PRINTF(C,("Constructing Replay buffer: %p (buffer: %p)\n", ret, ret->buffer));
	return ret;
}

GASNETI_INLINE(gasnetc_free_replay_buffer)
void gasnetc_free_replay_buffer(gasnetc_replay_buffer_t *replay_buf) {
	GASNETI_TRACE_PRINTF(C,("Free replay buffer: %p (%d,%d) dest: %d, numquads: %d, buffer: %p, buffer_size: %d, quads: %p\n",
													replay_buf, replay_buf->amtype, replay_buf->amcat, replay_buf->dest, replay_buf->numquads, replay_buf->buffer, replay_buf->buffer_size, replay_buf->quads));
	if(replay_buf->amcat == GASNETC_AMMED && replay_buf->buffer_size > 0) {
		gasneti_assert(replay_buf->buffer);
		gasnetc_free_ambuf(replay_buf->buffer); 
	} else {
		gasneti_assert(replay_buf->buffer == NULL);
	}

	gasneti_lifo_push(&gasnetc_replay_buffer_free_list, replay_buf);
}



/* 
  NACK List Management
  ====================
  Nack list is a FIFO so manage the head and tail pointers
 */

static uint32_t ack_counter=0;
#define GASNETC_SEND_ACK(PEER, REMOTE_REPLAY_BUFFER) do{\
	DCQuad outmsg; outmsg.w0 = (REMOTE_REPLAY_BUFFER); outmsg.w1 = ack_counter; \
GASNETI_TRACE_PRINTF(C,("sending Positive ACK (ACK) to %d w/ buffer 0x%x (%d)\n", PEER, REMOTE_REPLAY_BUFFER, ack_counter++));\
 DCMF_SAFE(DCMF_Control(&gasnetc_dcmf_ack_registration, DCMF_RELAXED_CONSISTENCY,(PEER),&outmsg));\
} while(0)

#define GASNETC_SEND_NACK(PEER, REMOTE_REPLAY_BUFFER) do{\
	DCQuad outmsg; outmsg.w0 = (REMOTE_REPLAY_BUFFER); outmsg.w1 = ack_counter;\
GASNETI_TRACE_PRINTF(C,("sending Negative ACK (NACK) to %d w/ buffer 0x%x (%d)\n", PEER, REMOTE_REPLAY_BUFFER, ack_counter++));\
	DCMF_SAFE(DCMF_Control(&gasnetc_dcmf_ack_registration, DCMF_RELAXED_CONSISTENCY,(PEER),&outmsg));\
} while(0)

void gasnetc_add_replay_to_nack_list(gasnetc_replay_buffer_t* replay_buf) {
	gasnetc_fifo_add(&gasnetc_nack_list, (void*) replay_buf);
}

gasnetc_replay_buffer_t* gasnetc_remove_from_nack_list() {
	return (gasnetc_replay_buffer_t*) gasnetc_fifo_remove(&gasnetc_nack_list);
}

void gasnetc_ack_msg_cb(void *client_data,
												const DCMF_Control_t *info,
												unsigned peer){
	gasnetc_replay_buffer_t* buffer = (gasnetc_replay_buffer_t*) (*info).w0;
	uint32_t seq = (uint32_t) (*info).w1;
	GASNETI_TRACE_PRINTF(C,("Freeing replay buffer: %p %d", buffer, seq));
	gasnetc_free_replay_buffer(buffer);
}


void gasnetc_add_to_nack_list_cb(void *client_data,
		const DCMF_Control_t *info,
		unsigned peer) {
	gasnetc_replay_buffer_t* buffer = (gasnetc_replay_buffer_t*) (*info).w0;
	uint32_t seq = (uint32_t) (*info).w1;
	GASNETI_TRACE_PRINTF(C,("adding replay buffer to nack: %p %d", buffer, seq));
	gasnetc_add_replay_to_nack_list(buffer);
}

/*
  Active Message Queues
  ==============================
*/

/*construct a new queue entry which contains information for a function to run*/
/*handler contains static array of arguments as well as all other information needed to execute am*/
GASNETI_INLINE(gasnetc_construct_new_amhandler) 
gasnetc_amhandler_t *gasnetc_construct_new_amhandler(gasnetc_token_t *token,
								   int handleridx, void *buffer, uint8_t buffer_needs_free, size_t nbytes,
								   DCQuad *argquads, unsigned numquads, int numargs){

  gasnetc_amhandler_t *ret;

  /*check the free list if we have one*/
	ret = gasneti_lifo_pop(&gasnetc_amhandler_free_list);
	if(!ret) {
		GASNETI_TRACE_PRINTF(C, ("malloc of handler (%d bytes)", sizeof(gasnetc_amhandler_t)));
		ret = (gasnetc_amhandler_t*) gasneti_malloc(sizeof(gasnetc_amhandler_t));
  }
    
  /*fill in teh data structure*/
  ret->token = token;
  ret->handleridx = handleridx;
  ret->buffer = buffer;
	ret->buffer_needs_free=buffer_needs_free;
  ret->nbytes = nbytes;
  /*unpack the arguemtns into the static array thats part of hte structure*/
  GASNETC_UNPACK_ARG_QUADS(numquads, argquads, numargs, ret->amargs);
  ret->numargs = numargs;
  ret->seq_number = gasnetc_curr_seq_number++; 
  ret->next = NULL;
  GASNETI_TRACE_PRINTF(C,("constructing handler: %p seq: %d(%d)\n", ret, ret->seq_number, gasnetc_curr_seq_number));
  return ret;	
}

GASNETI_INLINE(gasnetc_free_amhandler) 
void gasnetc_free_amhandler(gasnetc_amhandler_t *amhandler) {
	/*add the amhandler back to the free list*/
  gasnetc_free_token(amhandler->token);
  gasneti_lifo_push(&gasnetc_amhandler_free_list, (void*) amhandler);
  gasneti_semaphore_up(&gasnetc_incoming_buffers_available);
}


/*activate the new handler indicating that the payload has arrived 
  and adding it to the end of the active queue*/

GASNETI_INLINE(gasnetc_activate_amhandler) 
void gasnetc_activate_amhandler(gasnetc_amhandler_t *amhandler) {
	GASNETI_TRACE_PRINTF(C, ("dcmf activating handler before: amhandler: %p \n", amhandler));
	
	gasnetc_fifo_add(&gasnetc_amhandler_active_list, (void*) amhandler);
	gasnetc_active_amhandlers++;
}

/*return the head of the active handler queue*/
GASNETI_INLINE(gasnetc_remove_first_active_amhandler) 
gasnetc_amhandler_t* gasnetc_remove_first_active_amhandler() {
  gasnetc_amhandler_t *ret=NULL;
	ret = (gasnetc_amhandler_t*) gasnetc_fifo_remove(&gasnetc_amhandler_active_list);
	GASNETI_TRACE_PRINTF(C, ("dcmf remove&ret handler after: amhandler: %p\n",ret));
	
  return ret;
}


GASNETI_INLINE(gasnetc_run_amhandler_inner) 
void gasnetc_run_amhandler_inner(gasnetc_amhandler_t *handler) {
	gasneti_assert(handler);
	GASNETI_TRACE_PRINTF(C,("running handler: %p am: (%d,%d) seq: %d\n", handler, handler->token->amtype, 
													handler->token->amcat, handler->seq_number));
  switch(handler->token->amcat){
  case GASNETC_AMSHORT: 
    GASNETI_RUN_HANDLER_SHORT((handler->token->amtype==GASNETC_AMREQ), 
			      handler->handleridx,
			      gasnetc_handler[handler->handleridx],
			      handler->token,
			      handler->amargs,
			      handler->numargs);
    break;
  case GASNETC_AMMED: 
		{
			void *ambuffer = NULL;
			uint8_t free_later = 0;
			
			/*if we used a preallocated buffer makes ure to pass the right buffer into the medium arguments*/
			if(handler->buffer_needs_free) {
				gasnetc_ambuf_t *temp= (gasnetc_ambuf_t*) handler->buffer;
				gasneti_assert(temp);
				ambuffer= temp->data;
		
				free_later =1;
			} else {
				ambuffer = handler->buffer;
				free_later =0;
			}
			
			if(handler->nbytes > 0) gasneti_assert(ambuffer);
			
			GASNETI_RUN_HANDLER_MEDIUM((handler->token->amtype==GASNETC_AMREQ), 
																 handler->handleridx,
																 gasnetc_handler[handler->handleridx],
																 handler->token,
																 handler->amargs,
																 handler->numargs,
																 ambuffer,
																 handler->nbytes);
			/*medium replies we will not allocate an extra buffer so don't free; only free on requests*/
			if(free_later) gasnetc_free_ambuf((gasnetc_ambuf_t*) handler->buffer);
			handler->buffer=NULL;
			break;
		}
	case GASNETC_AMLONG: 
  case GASNETC_AMLONGASYNC: 
    if(handler->nbytes > 0) gasneti_assert(handler->buffer);
    GASNETI_RUN_HANDLER_LONG((handler->token->amtype==GASNETC_AMREQ), 
			     handler->handleridx,
			     gasnetc_handler[handler->handleridx],
			     handler->token,
			     handler->amargs,
			     handler->numargs,
			     handler->buffer, handler->nbytes);
	    
    break;
  default: gasneti_fatalerror("unknown AM category"); break;
  }
  
  if(handler->token->sent_reply == 0 && handler->token->amtype == GASNETC_AMREQ) {
  	/*the handler didn't send a reply so we have to send an explicit ACK over the control channel*/
  	GASNETC_SEND_ACK(handler->token->srcnode, handler->token->remote_replay_buffer);
  }
  
}


/*
  Misc. Active Message Functions
  ==============================
*/
extern int gasnetc_AMGetMsgSource(gasnet_token_t token, gasnet_node_t *srcindex) {
  gasnet_node_t sourceid;
  GASNETI_CHECKATTACH();
  GASNETI_CHECK_ERRR((!token),BAD_ARG,"bad token");
  GASNETI_CHECK_ERRR((!srcindex),BAD_ARG,"bad src ptr");
    
  sourceid = ((gasnetc_token_t*)token)->srcnode; 
    
  gasneti_assert(sourceid < gasneti_nodes);
  *srcindex = sourceid;
  return GASNET_OK;
}

extern int gasnetc_AMPoll() {
  int retval;
  gasnetc_amhandler_t *amhandler;
  GASNETI_CHECKATTACH();
    
  /* Make sure lock is aquired*/
	DCMF_CriticalSection_enter(0);
	GASNETI_TRACE_PRINTF(C,("DCMF POLL #1"));
  /*Run the DCMF active message handlers to queue whatever was left*/
  DCMF_Messager_advance();
  DCMF_CriticalSection_exit(0);
	GASNETI_TRACE_PRINTF(C,("DCMF POLL #1 done"));
	
  /*if there are active message handlers for us to run*/    
	amhandler = gasnetc_remove_first_active_amhandler();
	if(amhandler) {
    GASNETI_TRACE_PRINTF(C,("starting to clear active list numactive: %d", gasnetc_active_amhandlers));
		/*run any active message functions that got queued*/
    
    do {
        
      gasnetc_run_amhandler_inner(amhandler);
      /*Once these calls return then they have 
       * finished their calls and they will not be called again with thte 
       * current data set so just remove them from the active queue*/
    	
    	GASNETI_TRACE_PRINTF(C,("finished running handler: %p seq: %d\n", amhandler, amhandler->seq_number));
      gasnetc_free_amhandler(amhandler);
    	amhandler = gasnetc_remove_first_active_amhandler();
    } while(amhandler!=NULL);
		/*clear the entire active list at one shot*/
		gasnetc_active_amhandlers = 0;
		GASNETI_TRACE_PRINTF(C,("finishing clear active list"));
  }
  return GASNET_OK;
}




/*
  Active Message Handler Routines
  ================================
*/

void gasnetc_dcmf_handle_am_short(void *clientdata,
				  const DCQuad *msginfo,
				  unsigned count, 
				  unsigned peer,
				  const char *src,
				  unsigned bytes){
    
  DCQuad headerquad;
  gasnetc_handler_fn_t pfn;
  gasnet_handler_t handleridx;
  gasnetc_token_t *token;
  gasnetc_dcmf_amtype_t amtype;
  gasnetc_dcmf_amcategory_t amcat;
  void *dstaddr; 
  unsigned transfer_size;
  unsigned remote_replay_buffer;
  int numargs;
  gasnetc_amhandler_t *amhandler;
 
  DCQuad *argquads;
  int argquadcount = count -1;

	
  GASNETI_CHECKATTACH();
  gasneti_assert(count>0); /*we need to get at least one quad of header*/
  headerquad = msginfo[0];
	
  /*extract active message properties*/
  amcat = GASNETC_GET_ACTIVE_MSG_CATEGORY(headerquad);
  amtype = GASNETC_GET_ACTIVE_MSG_TYPE(headerquad);
	remote_replay_buffer = GASNETC_GET_ACTIVE_MSG_REMOTE_REPLAY_BUFFER(headerquad);
	
  /*extract and error check args*/
  dstaddr = (void*) GASNETC_GET_ACTIVE_MSG_DSTADDR(headerquad);
  transfer_size = GASNETC_GET_ACTIVE_MSG_NBYTES(headerquad);
	numargs = GASNETC_GET_ACTIVE_MSG_NUMARGS(headerquad);
	handleridx= GASNETC_GET_ACTIVE_MSG_HANDLERIDX(headerquad);


	GASNETI_TRACE_PRINTF(C, ("running short handler: (%d,%d) dst: %p transfersize: %d numargs: %d handleridx: %d replay: %x", 
													 amcat, amtype, dstaddr, transfer_size, numargs, handleridx, remote_replay_buffer));
#if GASNETC_FLOW_CONTROL_ENABLED
	/*first check if we can accept the message*/
	/*otherwise just drop the arguments on the floor and ask for a resend*/
	if(amtype == GASNETC_AMREQ) {
		int sem_result;
		sem_result = gasneti_semaphore_trydown(&gasnetc_incoming_buffers_available);
		if(sem_result == 0) {
			/*we can't accept the message so send a "NACK" back to teh sender*/
			/*if this was an AMLong copy the payload to the final destination but don't queue the AM*/
			if((amcat == GASNETC_AMLONG) || (amcat == GASNETC_AMLONGASYNC)) {
				GASNETE_FAST_UNALIGNED_MEMCPY(dstaddr, src, bytes);
			}
			GASNETC_SEND_NACK(peer, remote_replay_buffer);
			return;
		}
	 else 
#endif
		if (amtype == GASNETC_AMREP){
			/*this was a reply to one of our requests so go ahead and clear the associated replay buffer*/
			gasnetc_free_replay_buffer((gasnetc_replay_buffer_t*) remote_replay_buffer);
		}

	/*if we get this far this means we have permission to construct the token and construct the amhandler reply*/
  /*construct token no need to allocate DCMF Req since this is a short callback*/
	token = gasnetc_construct_token(peer, amtype, amcat, remote_replay_buffer, 0); /*pull a token off the free list if one is available*/		

  pfn = gasnetc_handler[handleridx];
	
	
  if(count == 1) /*no user arguments*/{
    gasneti_assert(numargs==0); /*check to make sure we get consistent information about the number of quads*/
    argquads = NULL;
  } else {
    argquads = (DCQuad*) &msginfo[1];
  }



  if(amcat == GASNETC_AMSHORT) {
    void *ambuf;
		gasneti_assert(dstaddr==NULL);
    gasneti_assert(transfer_size == 0);
    ambuf = NULL;
		amhandler = gasnetc_construct_new_amhandler(token, handleridx, NULL, 0, 0, argquads, argquadcount, numargs);
	} else if(amcat == GASNETC_AMMED){
		gasneti_assert(transfer_size == bytes);
    gasneti_assert(dstaddr == NULL);
		/*if the transfer is a medium request w/ a nonzero lenght 
			then we need to allocate a bounce buffer to receive the data*/
    if(amtype == GASNETC_AMREQ) { 
			gasnetc_ambuf_t *ambuf;
			if(transfer_size > 0) {
				ambuf = gasnetc_get_ambuf(transfer_size);
				//ambuf = (void*) gasneti_malloc(transfer_size);
				GASNETE_FAST_UNALIGNED_MEMCPY_CHECK(ambuf->data, src, bytes); /*copy data into bounce buffer*/
				amhandler = gasnetc_construct_new_amhandler(token, handleridx, (void*) ambuf, 1, transfer_size, argquads, argquadcount, numargs);
			} else {
				ambuf = NULL;
				amhandler = gasnetc_construct_new_amhandler(token, handleridx, NULL, 0, 0, argquads, argquadcount, numargs);
			}
			
		} else {
			/*sicne we are running the handlerinline no need to copy*/
			void* ambuf = (void*) src;
			amhandler = gasnetc_construct_new_amhandler(token, handleridx, (void*) ambuf, 0, transfer_size, argquads, argquadcount, numargs);
		}
	
	} else if((amcat == GASNETC_AMLONG) || (amcat == GASNETC_AMLONGASYNC)) {
		void *ambuf;
    gasneti_assert(transfer_size == bytes);
    if(transfer_size > 0) gasneti_assert(dstaddr);
    ambuf = dstaddr;
		/*even if handler is run inline we need to copy the data to the user specified buffer*/
    GASNETE_FAST_UNALIGNED_MEMCPY_CHECK(ambuf, src, bytes); /*copy data into bounce buffer*/
		amhandler = gasnetc_construct_new_amhandler(token, handleridx, (void*) ambuf, 0, transfer_size, argquads, argquadcount, numargs);
  } else {
    gasneti_fatalerror("unknown am category");
  }
	
  /*construct the callback and extract the args into the handler*/
	/*if this is an am medium request, the temporary buffere needs to be freed*/
  
  if(amtype == GASNETC_AMREP) {
		/*since replies cannot generate further communication it is
			safe to runt he reply handlers inline here rather than queue them*/
		gasnetc_run_amhandler_inner(amhandler);
		gasnetc_free_amhandler(amhandler);
	} else {
		gasneti_assert(amhandler->token->amtype == GASNETC_AMREQ);
		/*since this is a request callback we must queue the callback
		 to run later since it can generate a reply*/
		gasnetc_activate_amhandler(amhandler);
	}
	
}

/*this callback is invoked when the active message payload has been accepted but not the args*/
void gasnetc_dcmf_handle_am_fail(void *arg) {
	gasnetc_dcmf_nack_req_t *nack_req = (gasnetc_dcmf_nack_req_t*) arg;
	
	/*then send a nack to our peer asking for a resend arguments*/
	GASNETC_SEND_NACK(nack_req->peer, nack_req->remote_replay_buffer);
	gasnetc_free_dcmf_nack_req(nack_req);
}

/*this callback is invoked when the active message was accepted and can be put itno the queue*/
void gasnetc_dcmf_handle_am_done(void *arg){
  gasnetc_amhandler_t *amhandler = (gasnetc_amhandler_t*) arg;
  GASNETI_TRACE_PRINTF(C, ("dcmf am send done callback: %p\n", amhandler)); 
  /* Data transfer is complete and visible to the programmer. 
   * Handler is now ready to run so queue it up to run outside of the DCMF_Messager_advance 
   * Associated Request will automatically get freed when the handler is freed*/
	
  if(amhandler->token->amtype == GASNETC_AMREP) {
		/*since replies cannot generate further communication it is
			safe to run the reply handlers inline here rather than queue them*/
		gasnetc_run_amhandler_inner(amhandler);
		gasnetc_free_amhandler(amhandler);
	} else {
		gasneti_assert(amhandler->token->amtype == GASNETC_AMREQ);
		/*since this is a request callback and it can generate a reply we must queue the callback
		 to run later*/
		gasnetc_activate_amhandler(amhandler);
	}
	
}

DCMF_Request_t* gasnetc_dcmf_handle_am_header(void *clientdata,
					      const DCQuad *msginfo, unsigned count, 
					      unsigned peer, unsigned sendlen,
					      unsigned *rcvlen, char **rcvbuf,
					      DCMF_Callback_t *cb_done){
  DCQuad headerquad;
  gasnetc_handler_fn_t pfn;
  gasnet_handler_t handleridx;
  gasnetc_token_t *token;
  gasnetc_dcmf_amtype_t amtype; 
  gasnetc_dcmf_amcategory_t amcat;
  void *dstaddr;
  unsigned remote_replay_buffer;
  
  unsigned transfer_size;
  int numargs;
  gasnetc_amhandler_t *amhandler;
  DCQuad *argquads;
	unsigned temp = count;
  unsigned argquadcount = temp-1;
	
  GASNETI_CHECKATTACH();
  gasneti_assert(temp>0); /*we need to get at least one quad of header*/
  headerquad = msginfo[0];
	
  gasneti_assert(clientdata==NULL); /*we aren't using the client data field for anything yet*/
  /*extract and error check args*/
  dstaddr = (void*) GASNETC_GET_ACTIVE_MSG_DSTADDR(headerquad);
  transfer_size = GASNETC_GET_ACTIVE_MSG_NBYTES(headerquad);
  remote_replay_buffer = GASNETC_GET_ACTIVE_MSG_REMOTE_REPLAY_BUFFER(headerquad);
  numargs = GASNETC_GET_ACTIVE_MSG_NUMARGS(headerquad);
  handleridx= GASNETC_GET_ACTIVE_MSG_HANDLERIDX(headerquad);

  pfn = gasnetc_handler[handleridx];

  /*extract active message properties*/
  amcat = GASNETC_GET_ACTIVE_MSG_CATEGORY(headerquad);
  amtype = GASNETC_GET_ACTIVE_MSG_TYPE(headerquad);
	
	gasneti_assert(temp==count);
  if(count == 1) /*we actualy contain arguments*/{
    gasneti_assert(numargs==0); /*check to make sure both header and payload both don't have quads*/
    argquads = NULL;
  } else {
    argquads = (DCQuad*) &msginfo[1];
  }
	
#if GASNETC_FLOW_CONTROL_ENABLED
	if(amtype == GASNETC_AMREQ) {
		int sem_result;
		sem_result = gasneti_semaphore_trydown(&gasnetc_incoming_buffers_available);
		if(sem_result == 0) {
			gasnetc_dcmf_nack_req_t *request;
			request = gasnetc_get_dcmf_nack_req(peer, remote_replay_buffer);
			request->peer = peer;
			/*we can't accept the message so send a "NACK" back to teh sender*/
			/*if this was an AMLong copy the payload to the final destination but don't queue the AM*/
			if((amcat == GASNETC_AMLONG) || (amcat == GASNETC_AMLONGASYNC)) {
				*rcvbuf = dstaddr; 
				*rcvlen = sendlen;
			} else {
				*rcvbuf = NULL; 
				*rcvlen = 0;
			}
		  cb_done->function = gasnetc_dcmf_handle_am_fail;
		  cb_done->clientdata = (void*) request;
			return &request->req;
		}
	} else 
#endif
	if (amtype == GASNETC_AMREP){
		/*this was a reply to one of our requests so go ahead and clear the associated replay buffer*/
		gasnetc_free_replay_buffer((gasnetc_replay_buffer_t*) remote_replay_buffer);
	}
  /*construct token. make sure allocate a DCMF request so that we can return it*/
  
	/*in the case that this is a reply the remote_replay_buffer arguemnt will never be read again*/
	token = gasnetc_construct_token(peer, amtype, amcat,remote_replay_buffer,1); /*pull a token off the free list if one is available*/

	
  GASNETI_TRACE_PRINTF(C, ("starting dcmf header am handler: headerquad: (%x,%d,%d,%d) am(%d,%d) srcnode: %d dstaddr: %p len: %d numargs: %d(count: %d) handleridx: %d\n",
			   headerquad.w0,  headerquad.w1, headerquad.w2, headerquad.w3,
			   amtype, amcat, peer, dstaddr, transfer_size, numargs, count, handleridx));

  if(amcat == GASNETC_AMSHORT) {
    gasneti_assert(dstaddr==NULL);
    gasneti_assert(transfer_size == 0);
    /*no buffer needed to allocate since no payload*/	
    *rcvbuf = NULL;
    *rcvlen = 0;
		amhandler = gasnetc_construct_new_amhandler(token, handleridx, NULL, 0, 0,argquads, argquadcount, numargs);
	} else if(amcat == GASNETC_AMMED) {
		gasnetc_ambuf_t *ambuf;
		/*allocate a temporary buffer and queue the jobs*/
    gasneti_assert(transfer_size == sendlen);
    gasneti_assert(dstaddr == NULL);
		/*dcmf guarantees that this callback won't be called on a transfer size of 0 so need to handle that case here*/
		ambuf = gasnetc_get_ambuf(transfer_size);
		*rcvbuf = (char*) ambuf->data;
		*rcvlen = sendlen;
		amhandler = gasnetc_construct_new_amhandler(token, handleridx, (void*) ambuf, 1, transfer_size,argquads, argquadcount, numargs);
	} else if((amcat == GASNETC_AMLONG) || (amcat == GASNETC_AMLONGASYNC)) {
		void *ambuf;
    gasneti_assert(transfer_size == sendlen);
    if(transfer_size>0) gasneti_assert(dstaddr);
    *rcvbuf = ambuf = dstaddr;
    *rcvlen = sendlen;
		amhandler = gasnetc_construct_new_amhandler(token, handleridx, (void*) ambuf, 0, transfer_size,argquads, argquadcount, numargs);
  } else {
    gasneti_fatalerror("unknown amcat %d", amcat);
  }
	
	gasneti_assert(amhandler);
  /*construct a new queue entry but don't queue it up*/
	/*always need to malloc even for ammedium to provide landing zone so need to free it*/
  
	
  	
  /*have teh callback queue up teh entry once the transfer is completed*/
  cb_done->function = gasnetc_dcmf_handle_am_done;
  cb_done->clientdata = (void*) amhandler;
  return &token->dcmf_req->req;
}	


/*
  Active Message Functions
  ================================
*/


//GASNETI_INLINE(gasnetc_resend_am_req) 
void gasnetc_resend_am_req(gasnetc_replay_buffer_t *replay_buffer) {
	volatile uint8_t send_done=0;
	DCMF_Callback_t send_done_callback;
	DCMF_Request_t req;
	int ok_to_resend;
	
	/*will need to wait for send to be locally complete*/
	send_done_callback.function = gasnetc_inc_uint8_arg_cb;
	send_done_callback.clientdata = (void*)&send_done;
	

#if 0
	/*** LOGIC TO SEE if its safe to resend this replay buffer*/
	ok_to_reend = 1;
	if(!ok_to_resend) {
		return;
	}
#endif

	replay_buffer->retry_count++;

	
	/*arguments and  quads have already been packaged so send htem off*/
	/*and wait for send to complete*/
	/*only resend data if its an AM medium*/
		
	DCMF_CriticalSection_enter(0);
	
	/*Rendezvous doesn't seem to be delivering all the quads
		bug report has been sent to IBM until then always use "default" send protocol when over eager limit*/
	
	if(replay_buffer->amcat!= GASNETC_AMMED) {
		/*in the case of a short there's no payload and in the case of a LONG or LONGASYNC the payload 
		 is already ont he remote side*/
		DCMF_SAFE(DCMF_Send(&GASNETC_DCMF_AM_REGISTARTION(replay_buffer->amtype, replay_buffer->amcat, GASNETC_DCMF_SEND_EAGER),
												&req,
												send_done_callback,
												DCMF_RELAXED_CONSISTENCY,
												replay_buffer->dest_node, 0, NULL,
												replay_buffer->quads, replay_buffer->numquads));
		
	} else if (nbytes <= gasnetc_dcmf_eager_limit) {
		/*send eager message*/
		DCMF_SAFE(DCMF_Send(&GASNETC_DCMF_AM_REGISTARTION(replay_buffer->amtype, replay_buffer->amcat, GASNETC_DCMF_SEND_EAGER),
												&req,
												send_done_callback,
												DCMF_RELAXED_CONSISTENCY,
												replay_buffer->dest_node, replay_buffer->buffer_size, replay_buffer->buffer->data,
												replay_buffer->quads, replay_buffer->numquads));
		
	} else {
		DCMF_SAFE(DCMF_Send(&GASNETC_DCMF_AM_REGISTARTION(amtype, amcat, GASNETC_DCMF_SEND_DEFAULT),
												&req,
												send_done_callback,
												DCMF_RELAXED_CONSISTENCY,
												replay_buffer->dest_node, replay_buffer->buffer_size, replay_buffer->buffer->data,
												replay_buffer->quads, replay_buffer->numquads));
	}
	while(send_done == 0) {DCMF_Messager_advance();}
	DCMF_CriticalSection_exit(0);
}





GASNETI_INLINE(gasnetc_send_am_req)
void gasnetc_send_am_req(gasnetc_dcmf_amcategory_t amcat, gasnet_node_t dest_node, 
		int handler_idx, va_list argptr, int numargs, void *dst_addr, void *src_addr, size_t nbytes) {
  volatile uint8_t send_done=0;
  DCMF_Callback_t send_done_callback;
  gasnetc_dcmf_req_t *dcmf_req;
  gasnetc_replay_buffer_t *replay_buffer;
	gasnetc_dcmf_amtype_t amtype = GASNETC_AMREQ;
	dcmf_req  = gasnetc_get_dcmf_req();
	
	
	/*getting a replay buffer allocated implies that we have permission to send an AM from
	 * this node*/
	replay_buffer = gasnetc_get_replay_buffer(nbytes, (amcat == GASNETC_AMMED?1:0));
	
	replay_buffer->amtype = amtype; replay_buffer->amcat = amcat;
	replay_buffer->dest_node = dest_node; replay_buffer->buffer_size = nbytes;
	
	

	/*if this is request send the id of the replay buffer so we can deal with it on an ack or nack*/
	/*if it is a reply then we send back whatever the requester sent us*/
	GASNETC_MAKE_HEADER_QUAD(amtype, amcat, numargs, handler_idx, dst_addr, (unsigned) replay_buffer , nbytes, replay_buffer->quads);
	GASNETC_PACK_ARG_QUADS(numargs, argptr, replay_buffer->quads+1, &replay_buffer->numquads);
	replay_buffer->numquads++; /*add one for the header*/ 
	
	gasneti_assert(replay_buffer->numquads > 0); /*need at least one header*/
	if(numargs>0) gasneti_assert(replay_buffer->numquads > 1);	
	
	
	if(amcat != GASNETC_AMLONGASYNC) {
		/*will need to wait for send to be locally complete*/
		send_done_callback.function = gasnetc_inc_uint8_arg_cb;
		send_done_callback.clientdata = (void*)&send_done;
	} else {
		/*long async doens't wait for local send to complete before returning*/
		/*register the send done callback to free the request object when it is done running*/
		send_done_callback.function = gasnetc_free_dcmf_req_cb;
		send_done_callback.clientdata = (void*)dcmf_req;
	}
	
	
	
	DCMF_CriticalSection_enter(0);
	
	/*Rendezvous doesn't seem to be delivering all the quads
		bug report has been sent to IBM until then always use "default" send protocol when over eager limit*/
	
	if(nbytes == 0 && 0) {
		/*send control if the active message has no payload and no args?*/
	} else if (nbytes <= gasnetc_dcmf_eager_limit) {
		GASNETI_TRACE_PRINTF(C,("sending eager headerquad: (%x,%d,%d,%d) am(%d,%d) to %d src: %p nbytes: %d quads %p numquads %d", 
														replay_buffer->quads[0].w0, replay_buffer->quads[0].w1, replay_buffer->quads[0].w2, replay_buffer->quads[0].w3,
														amtype, amcat, dest_node, src_addr, nbytes, replay_buffer->quads, replay_buffer->numquads));
		
		/*send eager message*/
		DCMF_SAFE(DCMF_Send(&GASNETC_DCMF_AM_REGISTARTION(amtype, amcat, GASNETC_DCMF_SEND_EAGER),
												&dcmf_req->req,
												send_done_callback,
												DCMF_RELAXED_CONSISTENCY,
												dest_node, nbytes, src_addr,
												replay_buffer->quads, replay_buffer->numquads));
		
	} else {
		/*send default message*/
		GASNETI_TRACE_PRINTF(C,("sending rvous headerquad: (%x,%d,%d,%d) am(%d,%d) to %d src: %p nbyts: %d quads %p numquads %d", 
														replay_buffer->quads[0].w0, replay_buffer->quads[0].w1, replay_buffer->quads[0].w2, replay_buffer->quads[0].w3,
														amtype, amcat, dest_node, src_addr, nbytes,  replay_buffer->quads, replay_buffer->numquads));
		
		DCMF_SAFE(DCMF_Send(&GASNETC_DCMF_AM_REGISTARTION(amtype, amcat, GASNETC_DCMF_SEND_DEFAULT),
												&dcmf_req->req,
												send_done_callback,
												DCMF_RELAXED_CONSISTENCY,
												dest_node, nbytes, src_addr,
												replay_buffer->quads, replay_buffer->numquads));
	}
	/*if its an AMMedium Request copy the payload so that we can replay it later if needed*/
	/*perform the send here to overlap the memcpy with the send operation*/
	if(amcat== GASNETC_AMMED) 
		GASNETE_FAST_UNALIGNED_MEMCPY(replay_buffer->buffer->data, src_addr, nbytes);
	
	/*if its not a long async, no need to for local message completion handled by user*/
	if(amcat!=GASNETC_AMLONGASYNC) {
		while(send_done == 0) {DCMF_Messager_advance();}
		
	}
	
	DCMF_CriticalSection_exit(0);
	
	/*if its not a long async then we need to keep the request around around otherwise we can free it*/
	if(amcat!=GASNETC_AMLONGASYNC) {
		gasnetc_free_dcmf_req(dcmf_req);
	}
	
}
 
GASNETI_INLINE(gasnetc_send_am_rep) 
void gasnetc_send_am_rep(gasnetc_dcmf_amcategory_t amcat, 
												 int handler_idx, va_list argptr, int numargs, void *dst_addr, void *src_addr, size_t nbytes, gasnetc_token_t* token) {
	volatile uint8_t send_done=0;
	DCMF_Callback_t send_done_callback;
	
  gasnetc_dcmf_amtype_t amtype = GASNETC_AMREP;
  gasnet_node_t dest_node;
	unsigned numquads=0;
	DCQuad quads[GASNETC_MAXQUADS_PER_AM];
	DCMF_Request_t req;
	
	dest_node = token->srcnode;
	
  GASNETC_MAKE_HEADER_QUAD(amtype, amcat, numargs, handler_idx, dst_addr, token->remote_replay_buffer, nbytes, quads);
  GASNETC_PACK_ARG_QUADS(numargs, argptr, quads+1, &numquads);
  numquads++; /*add one for the header*/ 
  
  gasneti_assert(numquads > 0); /*need at least one header*/
  if(numargs>0) {gasneti_assert(numquads > 1);}
  
  
  /*since replys don't have async cat no need to worry about this case here*/
  /*will need to wait for send to be locally complete*/
  send_done_callback.function = gasnetc_inc_uint8_arg_cb;
  send_done_callback.clientdata = (void*)&send_done;
 
  

  DCMF_CriticalSection_enter(0);

	/*Rendezvous doesn't seem to be delivering all the quads
		bug report has been sent to IBM until then always use "default" send protocol when over eager limit*/

  if(nbytes == 0 && 0) {
    /*send control if the active message has no payload and no args?*/
  } else if (nbytes <= gasnetc_dcmf_eager_limit) {
		GASNETI_TRACE_PRINTF(C,("sending eager headerquad: (%x,%d,%d,%d) am(%d,%d) to %d src: %p nbytes: %d token: %p quads %p numquads %d", 
				quads[0].w0, quads[0].w1, quads[0].w2, quads[0].w3,
														amtype, amcat, dest_node, src_addr, nbytes, token, quads, numquads));
		
		/*send eager message*/
    DCMF_SAFE(DCMF_Send(&GASNETC_DCMF_AM_REGISTARTION(amtype, amcat, GASNETC_DCMF_SEND_EAGER),
												&req,
												send_done_callback,
												DCMF_RELAXED_CONSISTENCY,
												dest_node, nbytes, src_addr,
												quads, numquads));

  } else {
    /*send default message*/
    GASNETI_TRACE_PRINTF(C,("sending rvous headerquad: (%x,%d,%d,%d) am(%d,%d) to %d src: %p nbyts: %d token: %p quads %p numquads %d", 
														quads[0].w0, quads[0].w1, quads[0].w2, quads[0].w3,
														amtype, amcat, dest_node, src_addr, nbytes, token, quads, numquads));
		
			DCMF_SAFE(DCMF_Send(&GASNETC_DCMF_AM_REGISTARTION(amtype, amcat, GASNETC_DCMF_SEND_DEFAULT),
													&req,
													send_done_callback,
													DCMF_RELAXED_CONSISTENCY,
													dest_node, nbytes, src_addr,
													quads, numquads));

	}
 
  while(send_done == 0) {DCMF_Messager_advance();}
	DCMF_CriticalSection_exit(0);
  token->sent_reply = 1;
}

/* #if 0 */

/* void PACKAGE_AND_SEND_AM(gasnetc_amtype_t amtype, gasnetc_amcategory_t amcat, gasnet_node_t dest_node, */
/* 			int handler_idx, va_list agrptr, int numargs, void *dst_addr, void *src_addr, size_t nbytes, gasnetc_token_t* token){ */
/* 	gasnetc_replay_buffer_t *replay_buffer; */
	
/* 	if(amtype == GASNETC_AMREQ) { */
/* 		/\*requests have the possibility of being replayed so we need to copy them into their replay buffers*\/ */
/* 		replay_buffer = gasnetc_get_replay_buffer((amcat==GASNETC_AMMED?nbytes:0)); */
		
/* 	} else { */
/* 		/\*there's always space on the remote side for a reply since the remote side knows it sent a message so buffers won't get exhausted*\/ */
		
/* 	} */
	
/* 	/\*First allocate the replay buffer so we can pack the arg quads*\/ */
/* 	gasnetc_replay_buffer_t *replay_buf; */
	
/* 	if(amtype) */
	
/* } */


/* #define PACKAGE_AND_SEND_AM(AMTYPE, AMCAT, DEST_NODE, HANDLER_IDX, ARGPTR, NUMARGS, DST_ADDR, SRC_ADDR, NBYTES, TOKEN) do { \ */
/*     /\*statically allocate the arg quads*\/				\ */
/*     DCQuad quads[GASNETC_MAXQUADS_PER_AM];				\ */
/*     unsigned numactualquads=0;						\ */
/*     /\*package the header/data quqads*\/					\ */
/*     GASNETC_MAKE_HEADER_QUAD((AMTYPE), (AMCAT), (NUMARGS), (HANDLER_IDX), (DST_ADDR), (NBYTES), quads);	\ */
/*     GASNETC_PACK_ARG_QUADS((NUMARGS), (ARGPTR), quads+1, &numactualquads); \ */
/*     numactualquads++; /\*add one for the header*\/ if((NUMARGS)>0) gasneti_assert(numactualquads > 1);			\ */
/*     /\*fire off the AM*\/							\ */
/*     gasnetc_send_am((AMTYPE), (AMCAT), (DEST_NODE), quads, numactualquads, (SRC_ADDR), (NBYTES), (TOKEN)); \ */
/*   } while(0) */
/* #endif */


extern int gasnetc_AMRequestShortM( 
				   gasnet_node_t dest,       /* destination node */
				   gasnet_handler_t handler, /* index into destination endpoint's handler table */ 
				   int numargs, ...) {
  int retval;
  va_list argptr;
  GASNETI_COMMON_AMREQUESTSHORT(dest,handler,numargs);

  va_start(argptr, numargs); /*  pass in last argument */
  gasnetc_send_am_req(GASNETC_AMSHORT, dest, handler, argptr, numargs, NULL, NULL, 0);
  va_end(argptr);
  
  retval = GASNET_OK; 
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
  gasnetc_send_am_req(GASNETC_AMMED, dest, handler, argptr, numargs, NULL, source_addr, nbytes);
  retval = GASNET_OK; 
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
  gasnetc_send_am_req(GASNETC_AMLONG, dest, handler, argptr, numargs, dest_addr, source_addr, nbytes);
  retval = GASNET_OK;
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
  
	gasnetc_send_am_req(GASNETC_AMLONGASYNC, dest, handler, argptr, numargs, dest_addr, source_addr, nbytes);
	
  retval = GASNET_OK;
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
  gasneti_assert(token);
  va_start(argptr, numargs); /*  pass in last argument */
  gasnetc_send_am_rep(GASNETC_AMSHORT, handler, argptr, numargs, NULL, NULL, 0, (gasnetc_token_t*)token);
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
  GASNETI_COMMON_AMREPLYMEDIUM(token,handler,source_addr,nbytes,numargs);
  va_start(argptr, numargs); /*  pass in last argument */
  gasneti_assert(token);

  gasnetc_send_am_rep(GASNETC_AMMED, handler, argptr, numargs, NULL, source_addr, nbytes, (gasnetc_token_t*)token);
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
  int retval;
  va_list argptr;
  GASNETI_COMMON_AMREPLYLONG(token,handler,source_addr,nbytes,dest_addr,numargs); 
  va_start(argptr, numargs); /*  pass in last argument */
  gasneti_assert(token);

  gasnetc_send_am_rep(GASNETC_AMLONG, handler, argptr, numargs, dest_addr, source_addr, nbytes, (gasnetc_token_t*)token);
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

/*  $Archive:: /Ti/GASNet/template-conduit/gasnet_core.c                  $
 *     $Date: 2003/06/17 04:01:56 $
 * $Revision: 1.26 $
 * Description: GASNet elan conduit Implementation
 * Copyright 2002, Dan Bonachea <bonachea@cs.berkeley.edu>
 * Terms of use are as specified in license.txt
 */

#include <gasnet.h>
#include <gasnet_internal.h>
#include <gasnet_handler.h>
#include <gasnet_core_internal.h>

#include <elan3/elan3.h> /* for elan3_setperm */

#include <errno.h>
#include <unistd.h>
#include <signal.h>

#if GASNETC_USE_SIGNALING_EXIT
  #include <rms/rmsapi.h> /* for RMS calls in gasnetc_exit */

  /* signal used to propagate exit notification across job using RMS global signalling */
  #ifndef GASNETC_REMOTEEXIT_SIGNAL
    #define GASNETC_REMOTEEXIT_SIGNAL  SIGUSR1
  #endif
  static void gasnetc_remoteexithandler(int sig);
#endif

GASNETI_IDENT(gasnetc_IdentString_Version, "$GASNetCoreLibraryVersion: " GASNET_CORE_VERSION_STR " $");
GASNETI_IDENT(gasnetc_IdentString_ConduitName, "$GASNetConduitName: " GASNET_CORE_NAME_STR " $");

gasnet_handlerentry_t const *gasnetc_get_handlertable();

gasnet_node_t gasnetc_mynode = (gasnet_node_t)-1;
gasnet_node_t gasnetc_nodes = 0;

#if GASNETC_USE_STATIC_SEGMENT
  /* a hack to get a static shared segment */
  #define GASNETC_MAXSTATICSEG_SZ (10*1048576)
  uint8_t gasnetc_static_segment[GASNETC_MAXSTATICSEG_SZ+(16*1024)];
#endif

uintptr_t gasnetc_MaxLocalSegmentSize = 0;
uintptr_t gasnetc_MaxGlobalSegmentSize = 0;

static gasnet_seginfo_t gasnetc_remappableMem; /* elan-mapped segment we can move */

gasnet_seginfo_t *gasnetc_seginfo = NULL;

gasneti_mutex_t gasnetc_elanLock = GASNETI_MUTEX_INITIALIZER;
gasneti_mutex_t gasnetc_sendfifoLock = GASNETI_MUTEX_INITIALIZER;

/* ------------------------------------------------------------------------------------ */

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

/* ------------------------------------------------------------------------------------ */
/* elan-specific vars */
ELAN_BASE  *gasnetc_elan_base  = NULL;
ELAN_STATE *gasnetc_elan_state = NULL;
ELAN_GROUP *gasnetc_elan_group = NULL;
void *gasnetc_elan_ctx         = NULL;
ELAN_TPORT *gasnetc_elan_tport = NULL;

extern uint64_t gasnetc_clock() {
  if_pt (STATE()) {
    uint64_t val;
    #if 1
        /* verified by source code inspection that elan_clock is always thread-safe
           (just a 64-bit load of a NIC register)
        */
        val = elan_clock(STATE());
    #else
      LOCK_ELAN_WEAK();
        val = elan_clock(STATE());
      UNLOCK_ELAN_WEAK();
    #endif
    return val;
  }
  else 
    return 0;
}

gasnetc_handler_fn_t const gasnetc_unused_handler = (gasnetc_handler_fn_t)&abort;
gasnetc_handler_fn_t gasnetc_handler[GASNETC_MAX_NUMHANDLERS]; /* handler table */

/* ------------------------------------------------------------------------------------ */
/*
  Initialization
  ==============
*/
/* called at startup to check configuration sanity */
static void gasnetc_check_config() {
  /* add code to do some sanity checks on the number of nodes, handlers
   * and/or segment sizes */ 
  char *ver = elan_version();
  if (!elan_checkVersion(ver)) 
    gasneti_fatalerror("elan library version mismatch. linked version: %s", elan_version());
  { int major,minor,result;
    result = sscanf(ver,"libelan %i.%i", &major, &minor);
    if (result != 2) result = sscanf(ver,"%i.%i.%*i", &major, &minor);
    if (result != 2 || major != ELAN_VERSION_MAJOR || minor != ELAN_VERSION_MINOR)
      gasneti_fatalerror("unexpected elan library version.\n"
                         " Expected: libelan %i.%i\n"
                         " Actual  : libelan %s", ELAN_VERSION_MAJOR, ELAN_VERSION_MINOR, ver);
  }

  assert(sizeof(gasnetc_shortmsg_t) == GASNETC_SHORT_HEADERSZ);
  assert(sizeof(gasnetc_medmsg_t) == GASNETC_MED_HEADERSZ);
  assert(sizeof(gasnetc_longmsg_t) == GASNETC_LONG_HEADERSZ);

  assert(BASE()->tport_bigmsg >= GASNETC_MED_HEADERSZ + 4*GASNETC_MAX_SHORT + GASNETC_MAX_MEDIUM);
  assert(GASNETC_ELAN_MAX_QUEUEMSG >= GASNETC_LONG_HEADERSZ + GASNETC_MAX_SHORT*4);
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
  elan_gsync(GROUP());
}

static void gasnetc_bootstrapExchange(void *src, size_t len, void *dest) {
  ELAN_EVENT *evt;
  uint8_t *temp = NULL;

  /* we may be able to use an elan_reduce here, but the documentation is so poor 
     that I can't figure out how to use it (this is not performance critical anyhow) 
  */
  GASNETI_TRACE_PRINTF(D,("gasnetc_bootstrapExchange(%i bytes)",len));

  #ifdef ELAN_VER_1_2
    temp = elan_gallocMain(BASE()->galloc, GROUP(), 64, gasnetc_nodes*len);
  #else
    temp = elan_gallocMain(BASE(), GROUP(), 64, gasnetc_nodes*len);
  #endif
  if_pf(temp == NULL) 
    gasneti_fatalerror("error on elan_gallocMain in gasnetc_bootstrapExchange()");

  /* send info to 0 */
  evt = elan_put(STATE(), src, temp + gasnetc_mynode*len, len, 0);
  elan_wait(evt, BASE()->waitType);

  /* make 0 wait for puts to arrive */
  gasnetc_bootstrapBarrier();

  /* recv data from 0 */
  elan_hbcast(GROUP(), temp, gasnetc_nodes*len, 0, 1);    

  /* ensure operation complete */
  gasnetc_bootstrapBarrier();
  memcpy(dest, temp, gasnetc_nodes*len);
  #ifdef ELAN_VER_1_2
    elan_gallocFree(BASE()->galloc, temp);
  #else
    elan_gallocFree(BASE(), temp);
  #endif
}

static uintptr_t gasnetc_searchElanSeglength(void *base, uintptr_t lowsz, uintptr_t highsz) {
  uintptr_t sz;
  if (lowsz == highsz) return 0;
  sz = GASNETI_PAGE_ALIGNUP((lowsz + (highsz - lowsz) / 2));
  if (elan_addressable(STATE(), base, sz)) {
    uintptr_t temp = gasnetc_searchElanSeglength(base, sz, highsz);
    if (temp) return temp;
    else return sz;
  } else {
    if (sz == highsz) return 0;
    else return gasnetc_searchElanSeglength(base, lowsz, sz);
  }
}
/* return the length of the contiguous, elan-mapped memory segment starting at base */
static uintptr_t gasnetc_ElanSeglength(void *base) {
  return gasnetc_searchElanSeglength(base, 0, (uintptr_t)(uint32_t)-1);
}

static int gasnetc_init(int *argc, char ***argv) {
  if (gasnetc_init_done) 
    GASNETI_RETURN_ERRR(NOT_INIT, "GASNet already initialized");
  gasnetc_init_done = 1; /* enable early to allow tracing */

  if (getenv("GASNET_FREEZE")) gasneti_freezeForDebugger();

  #if DEBUG_VERBOSE
    /* note - can't call trace macros during gasnet_init because trace system not yet initialized */
    fprintf(stderr,"gasnetc_init(): about to spawn...\n"); fflush(stderr);
  #endif

  /* add code here to bootstrap the nodes for your conduit */

  gasnetc_elan_base = elan_baseInit();
  assert(gasnetc_elan_base);
  /*  check system sanity */
  gasnetc_check_config();

  gasnetc_elan_state = gasnetc_elan_base->state;
  gasnetc_elan_group = gasnetc_elan_base->allGroup;
  gasnetc_elan_ctx =   gasnetc_elan_state->ctx;

  gasnetc_mynode = STATE()->vp;
  gasnetc_nodes =  STATE()->nvp;

  #if 0
    /* unnecessary on elan - RMS takes care of the environment for us */
    /* do this before trace_init to make sure it gets right environment */
    gasneti_setupGlobalEnvironment(gasnetc_nodes, gasnetc_mynode, 
                                   gasnetc_bootstrapExchange, gasnetc_bootstrapBroadcast);
  #endif

  /* enable tracing */
  gasneti_trace_init();

  #if 0 
    assert(gasnetc_nodes > 0 && gasnetc_mynode >= 0); /* true by datatype */
  #endif
  assert(gasnetc_mynode < gasnetc_nodes);
  assert(gasnetc_nodes <= GASNET_MAXNODES);

  #if DEBUG_VERBOSE
    fprintf(stderr,"gasnetc_init(): spawn successful - node %i/%i starting...\n", 
      gasnetc_mynode, gasnetc_nodes); fflush(stderr);
  #endif

  if (!BASE()->group_hwbcast) {
    char const *msg = "WARNING: Hardware broadcasts/barriers are currently disabled."
      "This could be a result of environment settings, site configuration, or non-contiguous node allocation."
      "This is likely to affect barrier performance.";
    GASNETI_TRACE_PRINTF(I,("%s",msg));
    fprintf(stderr,"%s\n",msg);
  }

  #if defined(GASNET_SEGMENT_FAST) || defined(GASNET_SEGMENT_LARGE)
    #if GASNETC_USE_STATIC_SEGMENT
      /* allocate segment statically */
      gasnetc_MaxLocalSegmentSize = GASNETC_MAXSTATICSEG_SZ;
      gasnetc_MaxGlobalSegmentSize = GASNETC_MAXSTATICSEG_SZ;
    #else
      /* determine how much elan VM space we can safely re-map, if necessary */
      if (gasnetc_mynode == 0) {
        gasnetc_remappableMem.addr = sbrk(0);
        gasnetc_remappableMem.size = gasnetc_ElanSeglength(gasnetc_remappableMem.addr);
      }
      elan_hbcast(GROUP(), &gasnetc_remappableMem, sizeof(gasnetc_remappableMem), 0, 0);
      gasneti_segmentInit(&gasnetc_MaxLocalSegmentSize, 
                          &gasnetc_MaxGlobalSegmentSize,
                          #ifdef GASNET_SEGMENT_FAST
                            gasnetc_remappableMem.size,
                          #else
                            (uintptr_t)-1,
                          #endif
                          gasnetc_nodes,
                          &gasnetc_bootstrapExchange);
    #endif
  #elif defined(GASNET_SEGMENT_EVERYTHING)
    gasnetc_MaxLocalSegmentSize =  (uintptr_t)-1;
    gasnetc_MaxGlobalSegmentSize = (uintptr_t)-1;
  #else
    #error Bad segment config
  #endif

  #if GASNETC_USE_SIGNALING_EXIT
    /* register handlers early to handle calls to gasnet_exit between init and attach */
    gasneti_reghandler(GASNETC_REMOTEEXIT_SIGNAL, gasnetc_remoteexithandler);
    gasneti_registerSignalHandlers(gasneti_defaultSignalHandler);
    gasnetc_bootstrapBarrier(); /* ensure everybody has registered exit handler */
  #endif

  return GASNET_OK;
}

/* ------------------------------------------------------------------------------------ */
extern int gasnet_init(int *argc, char ***argv) {
  int retval = gasnetc_init(argc, argv);
  if (retval != GASNET_OK) GASNETI_RETURN(retval);
  #if 0
    /* called within gasnet_init to allow init tracing */
    gasneti_trace_init();
  #endif
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
    gasnetc_handler[newindex] = table[i].fnptr;

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

  if (!gasnetc_init_done) 
    GASNETI_RETURN_ERRR(NOT_INIT, "GASNet attach called before init");
  if (gasnetc_attach_done) 
    GASNETI_RETURN_ERRR(NOT_INIT, "GASNet already attached");

  /* wait for all nodes to arrive - increases system stability if there's a gasnet_exit()
     call between init and attach 
   */
  gasnetc_bootstrapBarrier(); 

  /* dump startup elan environment */
  if (GASNETI_STATS_ENABLED(C) && gasnetc_mynode == 0) {
    gasnetc_dump_envvars();
    GASNETI_STATS_PRINTF(C,("--------------------------------------------------------------------------------"));
    GASNETI_STATS_PRINTF(C,("ELAN Initialization State:"));
    gasnetc_dump_base();
    gasnetc_dump_group();
    gasnetc_dump_state();
    GASNETI_STATS_PRINTF(C,("--------------------------------------------------------------------------------"));
  }  

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
  { int i;
    for (i = 0; i < GASNETC_MAX_NUMHANDLERS; i++) 
      gasnetc_handler[i] = gasnetc_unused_handler;
  }
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
  #if !GASNETC_USE_SIGNALING_EXIT
    /* catch fatal signals and convert to SIGQUIT */
    gasneti_registerSignalHandlers(gasneti_defaultSignalHandler);
  #endif

  /* ------------------------------------------------------------------------------------ */
  /*  register segment  */

  /* use gasneti_malloc_inhandler during bootstrapping because we can't assume the 
     hold/resume interrupts functions are operational yet */
  gasnetc_seginfo = (gasnet_seginfo_t *)gasneti_malloc_inhandler(gasnetc_nodes*sizeof(gasnet_seginfo_t));

  #if defined(GASNET_SEGMENT_FAST) || defined(GASNET_SEGMENT_LARGE)
    #if GASNETC_USE_STATIC_SEGMENT
      /* allocate segment statically */
      if (segsize == 0) segbase = NULL; /* no segment */
      else {
          segbase = gasnetc_static_segment;
          segbase = (void *)((((uintptr_t)segbase) + (GASNET_PAGESIZE-1)) & ~(GASNET_PAGESIZE-1));
          assert(((uintptr_t)segbase) + segsize <= 
                 ((uintptr_t)gasnetc_static_segment) + sizeof(gasnetc_static_segment));
      }
      gasnetc_seginfo[gasnetc_mynode].addr = segbase;
      gasnetc_seginfo[gasnetc_mynode].size = segsize;
      gasnetc_bootstrapExchange(&gasnetc_seginfo[gasnetc_mynode], sizeof(gasnet_seginfo_t), gasnetc_seginfo);
    #else
      gasneti_segmentAttach(segsize, minheapoffset, gasnetc_seginfo, &gasnetc_bootstrapExchange);
      segbase = gasnetc_seginfo[gasnetc_mynode].addr;
      segsize = gasnetc_seginfo[gasnetc_mynode].size;
      { int i;
        uintptr_t maxsz = 0;
        for (i=0;i<gasnetc_nodes;i++) {
          if (gasnetc_seginfo[gasnetc_mynode].size > maxsz) 
            maxsz = gasnetc_seginfo[gasnetc_mynode].size;
        }
        if (elan_addressable(STATE(), gasnetc_seginfo[gasnetc_mynode].addr, maxsz)) {
          /* all segments already elan-mapped - nothing to do */
        } else {
          GASNETI_TRACE_PRINTF(I,("WARNING: changing elan mappings"));
          /* segment not completely elan-mapped on some nodes
             approach - 
              mmap area may not be elan-mapped, as most of the default libelan mapped address
                space is situated on the malloc heap, and stretches far beyond the sbrk
              so remove mappings from unmapped area above the sbrk and relocate them  
                to our shared segment
              TODO: this strategy likely needs modification on systems where the stack
                sits above the heap and grows towards it in a single elan-mapped area

             constraints -
              libelan (and our code) assumes the mappings are identical across nodes, 
                so we need update the mappings collectively
           */
          assert(elan_addressable(STATE(), gasnetc_remappableMem.addr, gasnetc_remappableMem.size));
          #ifdef GASNET_SEGMENT_FAST
            assert(maxsz <= gasnetc_remappableMem.size); /* guaranteed by init */
          #endif
          { int pagesAvail = gasnetc_remappableMem.size / GASNET_PAGESIZE;
            uint8_t *pfrom = (uint8_t *)gasnetc_remappableMem.addr;
            uint8_t *pto = (uint8_t *)segbase;
            if (pfrom+gasnetc_remappableMem.size > pto && 
                pfrom+gasnetc_remappableMem.size < pto + segsize) /* two areas overlap */
              pagesAvail -= (pfrom+gasnetc_remappableMem.size-pto)/GASNET_PAGESIZE;
            while (pagesAvail) {
              int numpages = 0;
              int size = 0;
              uint8_t *elanBase = NULL;
              while (pto < (uint8_t *)segbase+maxsz && /* skip mapped pages */
                     elan_addressable(STATE(), pto, GASNET_PAGESIZE)) pto += GASNET_PAGESIZE;

              /* find length of run of unmapped pages */
              while (numpages < pagesAvail && pto + numpages*GASNET_PAGESIZE < (uint8_t *)segbase+maxsz && 
                !elan_addressable(STATE(), pto + numpages*GASNET_PAGESIZE, GASNET_PAGESIZE)) numpages++;
              if (numpages == 0) break;

              elanBase = (uint8_t *)(uintptr_t)elan_main2elan(STATE(), pfrom);
              size = numpages*GASNET_PAGESIZE;
              assert((uint8_t *)(uintptr_t)elan_main2elan(STATE(), pfrom + size - 1) == elanBase + size - 1);

              /* TODO: this remapping needs to be done for each rail */
              #ifdef ELAN_VER_1_2
                if (elan3_clearperm(CTX(), (E3_Addr)(uintptr_t)elanBase, size) < 0)
		  gasneti_fatalerror("gasnet_attach failed elan3_clearperm elan 0x%08x (size 0x%08x) : %d : %s",
			         (uint32_t)(uintptr_t)elanBase, size, errno, strerror(errno));

              #else
	        /* Remove any previous Main mappings */
	        if (elan3_clearperm_main (CTX(), (caddr_t)pfrom, size) < 0) 
		  gasneti_fatalerror("gasnet_attach failed elan3_clearperm_main pfrom main "GASNETI_LADDRFMT" (size 0x%08x) : %d : %s",
			         GASNETI_LADDRSTR(pfrom), size, errno, strerror(errno));
	        if (elan3_clearperm_main (CTX(), (caddr_t)pto, size) < 0)
		  gasneti_fatalerror("gasnet_attach failed elan3_clearperm_main pto main "GASNETI_LADDRFMT" (size 0x%08x) : %d : %s",
			         GASNETI_LADDRSTR(pto), size, errno, strerror(errno));
	    
	        /* Remove any previous Elan mappings */
	        if (elan3_clearperm_elan (CTX(), (E3_Addr)(uintptr_t)elanBase, size) < 0)
		  gasneti_fatalerror("gasnet_attach failed elan3_clearperm_elan elan 0x%08x (size 0x%08x) : %d : %s",
			         (uint32_t)(uintptr_t)elanBase, size, errno, strerror(errno));
              #endif

	      /* Create a new mapping - elan docs wrong: elan3_setperm() returns -1 on failure */
	      if (elan3_setperm (CTX(), (caddr_t)pto, (E3_Addr)(uintptr_t)elanBase, size, ELAN_PERM_REMOTEALL) < 0)
		  gasneti_fatalerror("gasnet_attach failed elan3_setperm main "GASNETI_LADDRFMT" to elan 0x%08x (size 0x%08x) : %d : %s",
			         GASNETI_LADDRSTR(pto), (uint32_t)(uintptr_t)elanBase, size, errno, strerror(errno));

              pagesAvail -= numpages;
              pto += numpages*GASNET_PAGESIZE;
              pfrom += numpages*GASNET_PAGESIZE;
            }
            assert(elan_addressable(STATE(), segbase, MIN(maxsz,gasnetc_remappableMem.size)));
          }
        }
      }
    #endif
    assert(((uintptr_t)segbase) % GASNET_PAGESIZE == 0);
    assert(segsize % GASNET_PAGESIZE == 0);
  #else
    /* GASNET_SEGMENT_EVERYTHING - 
       on elan we just use the default elan mappings and drop back to AM for
       non-mapped accesses, and we assume the mappings are identical across nodes
     */
    { int i;
      for (i=0;i<gasnetc_nodes;i++) {
        gasnetc_seginfo[i].addr = (void *)0;
        gasnetc_seginfo[i].size = (uintptr_t)-1;
      }
      segbase = gasnetc_seginfo[gasnetc_mynode].addr;
      segsize = gasnetc_seginfo[gasnetc_mynode].size;
    }
  #endif

  /* setup network buffers */
  gasnetc_initbufs();

  /* ------------------------------------------------------------------------------------ */
  /*  primary attach complete */
  gasnetc_attach_done = 1;
  gasnetc_bootstrapBarrier();

  GASNETI_TRACE_PRINTF(C,("gasnetc_attach(): primary attach complete"));

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
#if GASNETC_USE_SIGNALING_EXIT
  #ifdef GASNETI_USE_GENERIC_ATOMICOPS
    #error need real atomic ops with signal-safety for signaling gasnet_exit...
  #endif
  /* send a signal to all nodes in the job */
  static void gasnetc_sendGlobalSignal(int sig) {
    char *batchid;
    char *p;
    int resourceid, retval;

    batchid = gasnet_getenv("RMS_RESOURCEID");
    if (!batchid) gasneti_fatalerror("failed to getenv(RMS_RESOURCEID)");

    p = strchr(batchid,'.');
    if (!p) gasneti_fatalerror("bad RMS_RESOURCEID: %s", batchid);
    resourceid = atoi(p+1);
    if (resourceid <= 0) gasneti_fatalerror("bad RMS_RESOURCEID: %s", batchid);

    retval = rms_killResource(resourceid, sig); /* global signal */
    if (retval) {
      /* rms_killResource fails intermittently, probably due to race conditions 
         with remote signaling
      */
      GASNETI_TRACE_PRINTF(C,("rms_killResource(%i) failed: %s", 
                              resourceid, rms_errorString(retval)));
      gasneti_sched_yield();
      sleep(1);
      if (GASNETC_REMOTEEXITINPROGRESS()) return; /* some other node beat us to it */

      /* retry */
      retval = rms_killResource(resourceid, sig); /* global signal */
      gasneti_fatalerror("rms_killResource(%i) failed twice: %s", resourceid, rms_errorString(retval));
    }

    gasneti_sched_yield(); /* allow signal to propagate */

    #if 0
      /* other global-signalling garbage that didn't work as documented */
      #include <rms/rmscall.h>/* need this for the calls below */

      batchid = getenv("RMS_JOBID");
      if (!batchid) gasneti_fatalerror("failed to getenv(RMS_JOBID)");
      resourceid = atoi(batchid);

      resourceid = rms_resourceId(batchid);
      if (resourceid < 0) gasneti_fatalerror("rms_resourceId(%s) failed:%i, %s", batchid,resourceid, rms_errorString(resourceid));

      retval = rms_getprgid(getpid(), &resourceid);
      if (retval) gasneti_fatalerror("rms_getprgid failed: %s", strerror(errno));

      #if 1
        retval = rms_prgsignal(resourceid, SIGQUIT);
        if (retval) gasneti_fatalerror("rms_prgsignal(%i,SIGQUIT) failed: %s", resourceid, strerror(errno));
      #else
        { int *ids = malloc(gasnetc_nodes*sizeof(int));
          int nids = 0;
          int i;
          retval = rms_prgids(gasnetc_nodes, ids, &nids);
          if (retval) gasneti_fatalerror("rms_prgids failed: %s", strerror(errno));
          if (nids != gasnetc_nodes) 
            gasneti_fatalerror("rms_prgids returned only %i ids, expected %i", 
            nids, gasnetc_nodes);
          for (i = 0; i < nids; i++) {
            if (ids[i] =! resourceid) {
              retval = rms_prgsignal(ids[i], SIGQUIT);
              if (retval) gasneti_fatalerror("rms_prgsignal(%i,SIGQUIT) failed: %s", ids[i], strerror(errno));
            }
          }
          free(ids);
        raise(SIGQUIT);
        }
      #endif
    #endif
  }
  /* ------------------------------------------------------------------------------------ */
  /* set to zero iff some node is known to be exiting */
  gasneti_atomic_t gasnetc_remoteexitflag = gasneti_atomic_init(1); 
  /* non-zero if some node has signalled us to exit */
  gasneti_atomic_t gasnetc_remoteexitrecvd = gasneti_atomic_init(0); 
  static void gasnetc_remoteexithandler(int sig) {
    if (sig != GASNETC_REMOTEEXIT_SIGNAL) 
      gasneti_fatalerror("recieved an unknown signal (%i) in gasnetc_remoteexithandler()", sig);

    /* record that some node signalled us */
    gasneti_atomic_increment(&gasnetc_remoteexitrecvd);

    if (gasneti_atomic_decrement_and_test(&gasnetc_remoteexitflag)) {
      /* some remote node just informed us that it's exiting, 
         and it's the first we've heard about an exit 
      */
      raise(SIGQUIT); 
      /* alternate design possibility:
         rather than raising SIQUIT here (within a signal handler) and pay 
         the instability consequences of running gasnet_exit in that context,
         we could defer the SIGQUIT and instead raise it synchronously within the
         next AMPoll. However, this has the potential to cause hangs or crashes
         if we don't poll soon enough (before the other nodes disappear). We could
         prevent this using a timer interrupt, but that's even more fragile complexity.
      */
    } else {
      /* we already know about pending exit (from other node or because we initiated it)
         so just ignore the signal... */
      return;
    }
  }

  extern void gasnetc_exit(int exitcode) {

    #if 1
      /* once we start a shutdown, ignore all future SIGQUIT signals or we risk reentrancy 
         unfortunately, it seems SIG_IGN masking is not effective when applied within 
         a signal handler context (which we may be), hence the gasnetc_remoteexitflag hack
       */
      gasneti_reghandler(SIGQUIT, SIG_IGN);
      #if 0
        /* leave this one enabled so we can detect remote exit signals and prevent redundancy */
        gasneti_reghandler(GASNETC_REMOTEEXIT_SIGNAL, SIG_IGN);
      #endif
    #endif

    /* inform the GASNETC_REMOTEEXIT_SIGNAL handler that we're working on it and 
       shouldn't be bothered further */
    gasneti_atomic_decrement(&gasnetc_remoteexitflag);

    {  /* ensure only one thread ever continues past this point */
      static gasneti_mutex_t exit_lock = GASNETI_MUTEX_INITIALIZER;
      gasneti_mutex_lock(&exit_lock);
    }

    { /* a very nasty hack - 
        We're in a signal handler and here to stay, so there's no way we can
         ever gracefully unlock any locks we may hold in earlier stack frames. 
        All we can really do is clear the locks out (to prevent local deadlocks/errors)
         and hope for the best. If any other threads are actively using the NIC this 
         will likely cause crashes, but there's really no alternative...
      */
      gasneti_mutex_t dummy_lock = GASNETI_MUTEX_INITIALIZER;
      memcpy(&gasnetc_elanLock, &dummy_lock, sizeof(gasneti_mutex_t));
      memcpy(&gasnetc_sendfifoLock, &dummy_lock, sizeof(gasneti_mutex_t));
    }

    GASNETI_TRACE_PRINTF(C,("gasnet_exit(%i)\n", exitcode));

    { /* final check for re-entrancy */
      static int exit_inProgress = 0;
      if (exit_inProgress) 
          gasneti_fatalerror("attempted a re-enter gasnetc_exit");
      exit_inProgress = 1;
    }

    gasneti_trace_finish();
    if (fflush(stdout)) 
      gasneti_fatalerror("failed to flush stdout in gasnetc_exit: %s", strerror(errno));
    if (fflush(stderr)) 
      gasneti_fatalerror("failed to flush stderr in gasnetc_exit: %s", strerror(errno));
    gasneti_sched_yield();
    sleep(1); /* pause to ensure everyone has written trace if this is a collective exit */

    if (gasneti_atomic_read(&gasnetc_remoteexitrecvd) == 0) { 
      /* we initiated this shutdown synchronously, and it appears that no remote node 
         has signaled yet (reduce duplication of global termination signalling, 
         esp for collective exit)
         send a GASNETC_REMOTEEXIT_SIGNAL to the entire global job 
      */
      gasnetc_sendGlobalSignal(GASNETC_REMOTEEXIT_SIGNAL);
    }

    /* flush and close streams to ensure we don't lose output */
    if (fflush(stdout)) 
      gasneti_fatalerror("failed to flush stdout in gasnetc_exit: %s", strerror(errno));
    if (fflush(stderr)) 
      gasneti_fatalerror("failed to flush stderr in gasnetc_exit: %s", strerror(errno));
    gasneti_sched_yield();
    if (fclose(stdin)) 
      gasneti_fatalerror("failed to fclose(stdin) in gasnetc_exit: %s", strerror(errno));
    if (fclose(stdout)) 
      gasneti_fatalerror("failed to fclose(stdout) in gasnetc_exit: %s", strerror(errno));
    if (fclose(stderr)) 
      gasneti_fatalerror("failed to fclose(stderr) in gasnetc_exit: %s", strerror(errno));
    gasneti_sched_yield();

    _exit(exitcode); /* use _exit to bypass atexit handlers */
    abort();
  }
  extern void gasnetc_fatalsignal_callback(int sig) {
    if (GASNETC_EXITINPROGRESS()) {
    /* if we get a fatal signal during exit, it's almost certainly a signal-safety
       issue and not a client bug, so don't bother reporting it verbosely, 
       just die silently
     */
      #if 0
        abort();
      #endif
      _exit(1);
    }
  }
#else /* !GASNETC_USE_SIGNALING_EXIT */
  extern void gasnetc_exit(int exitcode) {
    /* do a naive non-collective exit */
    gasneti_trace_finish();
    if (fflush(stdout)) 
      gasneti_fatalerror("failed to flush stdout in gasnetc_exit: %s", strerror(errno));
    if (fflush(stderr)) 
      gasneti_fatalerror("failed to flush stderr in gasnetc_exit: %s", strerror(errno));
    gasneti_sched_yield();
    sleep(1); /* pause to ensure everyone has written trace if this is a collective exit */
    _exit(exitcode); /* use _exit to bypass atexit handlers */
    abort();
  }
  extern void gasnetc_fatalsignal_callback(int sig) {}
#endif
/* ------------------------------------------------------------------------------------ */
extern void gasnetc_new_threaddata_callback(void **core_threadinfo) {
  #if GASNETC_PREALLOC_AMLONG_BOUNCEBUF
    *core_threadinfo = elan_allocMain(STATE(), 64, GASNETC_MAX_LONG);
    if (!*core_threadinfo) 
      gasneti_fatalerror("Failed to elan_allocMain(%i bytes) for thread AM Long buffer. "
                         "Try disabling GASNETC_PREALLOC_AMLONG_BOUNCEBUF.", 
                          GASNETC_MAX_LONG);
  #else
    *core_threadinfo = NULL;
  #endif
  #ifndef GASNETI_THREADS
    _gasnetc_mythread = core_threadinfo;
  #endif
}
/* ------------------------------------------------------------------------------------ */
extern void gasnetc_trace_finish() {
  /* dump elan statistics */
  if (GASNETI_STATS_ENABLED(C) ) {
    if (GASNETC_REMOTEEXITINPROGRESS()) {
      /* trying to grab stats from the NIC during a signalled shutdown causes crashes */
      GASNETI_STATS_PRINTF(C,("*** Elan stat dump omitted because remote gasnet_exit in progress ***"));
      return;
    }
    GASNETI_STATS_PRINTF(C,("--------------------------------------------------------------------------------"));
    GASNETI_STATS_PRINTF(C,("Elan Statistics:"));
    gasnetc_dump_tportstats();
    gasnetc_dump_groupstats();
    GASNETI_STATS_PRINTF(C,("--------------------------------------------------------------------------------"));
  }
}
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

  /* add code here to write the source index into sourceid */
  { gasnetc_bufdesc_t *desc = (gasnetc_bufdesc_t*)token;
    assert(desc->handlerRunning);
    sourceid = desc->buf->msg.sourceId;
  }

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
  GASNETC_CHECKATTACH();
  if_pf (dest >= gasnetc_nodes) GASNETI_RETURN_ERRR(BAD_ARG,"node index too high");
  GASNETI_TRACE_AMREQUESTSHORT(dest,handler,numargs);
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
  GASNETC_CHECKATTACH();
  if_pf (dest >= gasnetc_nodes) GASNETI_RETURN_ERRR(BAD_ARG,"node index too high");
  if_pf (nbytes > gasnet_AMMaxMedium()) GASNETI_RETURN_ERRR(BAD_ARG,"nbytes too large");
  GASNETI_TRACE_AMREQUESTMEDIUM(dest,handler,source_addr,nbytes,numargs);
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
  GASNETC_CHECKATTACH();
  
  gasnetc_boundscheck(dest, dest_addr, nbytes);
  if_pf (dest >= gasnetc_nodes) GASNETI_RETURN_ERRR(BAD_ARG,"node index too high");
  if_pf (nbytes > gasnet_AMMaxLongRequest()) GASNETI_RETURN_ERRR(BAD_ARG,"nbytes too large");
  if_pf (((uintptr_t)dest_addr) < ((uintptr_t)gasnetc_seginfo[dest].addr) ||
         ((uintptr_t)dest_addr) + nbytes > 
           ((uintptr_t)gasnetc_seginfo[dest].addr) + gasnetc_seginfo[dest].size) 
         GASNETI_RETURN_ERRR(BAD_ARG,"destination address out of segment range");

  GASNETI_TRACE_AMREQUESTLONG(dest,handler,source_addr,nbytes,dest_addr,numargs);
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
  GASNETC_CHECKATTACH();
  
  gasnetc_boundscheck(dest, dest_addr, nbytes);
  if_pf (dest >= gasnetc_nodes) GASNETI_RETURN_ERRR(BAD_ARG,"node index too high");
  if_pf (nbytes > gasnet_AMMaxLongRequest()) GASNETI_RETURN_ERRR(BAD_ARG,"nbytes too large");
  if_pf (((uintptr_t)dest_addr) < ((uintptr_t)gasnetc_seginfo[dest].addr) ||
         ((uintptr_t)dest_addr) + nbytes > 
           ((uintptr_t)gasnetc_seginfo[dest].addr) + gasnetc_seginfo[dest].size) 
         GASNETI_RETURN_ERRR(BAD_ARG,"destination address out of segment range");

  GASNETI_TRACE_AMREQUESTLONGASYNC(dest,handler,source_addr,nbytes,dest_addr,numargs);
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
  GASNETI_TRACE_AMREPLYSHORT(token,handler,numargs);
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
  if_pf (nbytes > gasnet_AMMaxMedium()) GASNETI_RETURN_ERRR(BAD_ARG,"nbytes too large");
  GASNETI_TRACE_AMREPLYMEDIUM(token,handler,source_addr,nbytes,numargs);
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
  gasnet_node_t dest;
  va_list argptr;
  
  retval = gasnet_AMGetMsgSource(token, &dest);
  if (retval != GASNET_OK) GASNETI_RETURN(retval);
  gasnetc_boundscheck(dest, dest_addr, nbytes);
  if_pf (dest >= gasnetc_nodes) GASNETI_RETURN_ERRR(BAD_ARG,"node index too high");
  if_pf (nbytes > gasnet_AMMaxLongReply()) GASNETI_RETURN_ERRR(BAD_ARG,"nbytes too large");
  if_pf (((uintptr_t)dest_addr) < ((uintptr_t)gasnetc_seginfo[dest].addr) ||
         ((uintptr_t)dest_addr) + nbytes > 
           ((uintptr_t)gasnetc_seginfo[dest].addr) + gasnetc_seginfo[dest].size) 
         GASNETI_RETURN_ERRR(BAD_ARG,"destination address out of segment range");

  GASNETI_TRACE_AMREPLYLONG(token,handler,source_addr,nbytes,dest_addr,numargs);
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
    GASNETC_CHECKATTACH();
    /* add code here to disable handler interrupts for _this_ thread */
  }
  extern void gasnetc_resume_interrupts() {
    GASNETC_CHECKATTACH();
    /* add code here to re-enable handler interrupts for _this_ thread */
  }
#endif

/* ------------------------------------------------------------------------------------ */
/*
  Handler-safe locks
  ==================
*/

extern void gasnetc_hsl_init   (gasnet_hsl_t *hsl) {
  GASNETC_CHECKATTACH();

  #ifdef GASNETI_THREADS
  { int retval = pthread_mutex_init(&(hsl->lock), NULL);
    if (retval) 
      gasneti_fatalerror("In gasnetc_hsl_init(), pthread_mutex_init()=%s",strerror(retval));
  }
  #endif

  /* add code here to init conduit-specific HSL state */
}

extern void gasnetc_hsl_destroy(gasnet_hsl_t *hsl) {
  GASNETC_CHECKATTACH();
  #ifdef GASNETI_THREADS
  { int retval = pthread_mutex_destroy(&(hsl->lock));
    if (retval) 
      gasneti_fatalerror("In gasnetc_hsl_destroy(), pthread_mutex_destroy()=%s",strerror(retval));
  }
  #endif

  /* add code here to cleanup conduit-specific HSL state */
}

extern void gasnetc_hsl_lock   (gasnet_hsl_t *hsl) {
  GASNETC_CHECKATTACH();

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

  #if GASNETC_USE_INTERRUPTS
    /*       conduits with interrupt-based handler dispatch need to add code here to 
             disable handler interrupts on _this_ thread, (if this is the outermost
             HSL lock acquire and we're not inside an enclosing no-interrupt section)
     */
    #error interrupts not implemented
  #endif
}

extern void gasnetc_hsl_unlock (gasnet_hsl_t *hsl) {
  GASNETC_CHECKATTACH();

  #if GASNETC_USE_INTERRUPTS
    /*       conduits with interrupt-based handler dispatch need to add code here to 
             re-enable handler interrupts on _this_ thread, (if this is the outermost
             HSL lock release and we're not inside an enclosing no-interrupt section)
     */
    #error interrupts not implemented
  #endif

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

  { 0, NULL }
};

gasnet_handlerentry_t const *gasnetc_get_handlertable() {
  return gasnetc_handlers;
}

/* ------------------------------------------------------------------------------------ */

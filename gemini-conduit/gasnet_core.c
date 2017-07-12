/*   $Source: bitbucket.org:berkeleylab/gasnet.git/gemini-conduit/gasnet_core.c $
 * Description: GASNet gemini conduit Implementation
 * Copyright 2002, Dan Bonachea <bonachea@cs.berkeley.edu>
 * Gemini conduit by Larry Stewart <stewart@serissa.com>
 * Terms of use are as specified in license.txt
 */

#include <gasnet_internal.h>
#include <gasnet_core_internal.h>
#include <gasnet_gemini.h>
/* #include <alps/libalpslli.h> */

#include <errno.h>
#include <unistd.h>
#include <signal.h>

#if !GASNET_PSHM
#include <alloca.h>
#endif

#include <sys/mman.h>
#include <hugetlbfs.h>

GASNETI_IDENT(gasnetc_IdentString_Version, "$GASNetCoreLibraryVersion: " GASNET_CORE_VERSION_STR " $");
GASNETI_IDENT(gasnetc_IdentString_Name,    "$GASNetCoreLibraryName: " GASNET_CORE_NAME_STR " $");

gex_AM_Entry_t const *gasnetc_get_handlertable(void);

gex_AM_Entry_t *gasnetc_handler; // TODO-EX: will be replaced with per-EP tables

#if HAVE_ON_EXIT
static void gasnetc_on_exit(int, void*);
#else
static void gasnetc_atexit(void);
#endif

gasneti_spawnerfn_t const *gasneti_spawner = NULL;

/* ------------------------------------------------------------------------------------ */
/*
  Initialization
  ==============
*/
/* called at startup to check configuration sanity */
static void gasnetc_check_config(void) {
  gasneti_check_config_preinit();

  /* (###) add code to do some sanity checks on the number of nodes, handlers
   * and/or segment sizes */ 

  gasneti_assert((1<<GASNETC_LOG2_MAXNODES) == GASNET_MAXNODES);

  /* Otherwise space is being wasted: */
  gasneti_assert(GASNETC_MSG_MAXSIZE ==
                 (GASNETC_HEADLEN(medium, GASNETC_MAX_ARGS) + GASNETC_MAX_MEDIUM));
  
  gasneti_assert((int)GC_CMD_AM_LONG_PACKED == ((int)GC_CMD_AM_LONG + 1));

  { gni_nic_device_t device_type;
#ifdef GASNET_CONDUIT_GEMINI
    const gni_nic_device_t expected = GNI_DEVICE_GEMINI;
#endif
#ifdef GASNET_CONDUIT_ARIES
    const gni_nic_device_t expected = GNI_DEVICE_ARIES;
#endif
    gni_return_t status = GNI_GetDeviceType(&device_type);
    if ((status != GNI_RC_SUCCESS) ||
        (device_type != expected)) {
      gasneti_fatalerror("You do not appear to be running on a node with " GASNET_CORE_NAME_STR " hardware");
    }
  }
}

static int gasnetc_bootstrapInit(int *argc, char ***argv) {
  const char *envval;

  gasneti_spawner = gasneti_spawnerInit(argc, argv, "PMI", &gasneti_nodes, &gasneti_mynode);
  if (!gasneti_spawner) GASNETI_RETURN_ERRR(NOT_INIT, "GASNet job spawn failed");

  /* Check for device and address (both or neither) in environment  */
  envval = getenv("PMI_GNI_DEV_ID");
  if (NULL != envval) {
    int i=0;
    char *p, *q;

    gasnetc_dev_id = atoi(envval);
    gasneti_assert_always(gasnetc_dev_id >= 0);

    /* Device id is an index into colon-separated vector of addresses in $PMI_GNI_LOC_ADDR */
    envval = getenv("PMI_GNI_LOC_ADDR");
    gasneti_assert_always(NULL != envval);
    q = gasneti_strdup(getenv("PMI_GNI_LOC_ADDR"));
    p = strtok(q, ":");
    for (i=0; i<gasnetc_dev_id; ++i) {
      p = strtok(NULL, ":");
      gasneti_assert_always(NULL != p);
    }
    gasnetc_address = (uint32_t) atoi(p);
    gasneti_free(q);
  } else {
    /* defer local address resolution */
    gasnetc_dev_id  = -1;
    gasnetc_address = (uint32_t) -1;
  }

  /* TODO: validation / error handling */
  /* TODO: these might be colon-separated vectors too, right? */
  gasnetc_ptag    = (uint8_t)  atoi(getenv("PMI_GNI_PTAG"));
  gasnetc_cookie  = (uint32_t) atoi(getenv("PMI_GNI_COOKIE"));

  /* In GASNet+MPI hybrid applications in each lib must have a distinct cookie.
   * There are three choice:
   * 1) If MPI is always initialized first, then enable the first variant below
   *    and (re)build the GASNet library.
   * 2) If GASNet is always initialized first, then enable the second variant
   *    below and (re)build the GASNet library.
   * 3) If you don't want a build of GASNet that assumes the order the two
   *    libaries will be initialized, then clone the third variant into the
   *    application, to run between the two initializations.
   */
  #if 0   /* Option 1) If MPI is initialized first enable this: */
    gasnetc_cookie += 1;
  #endif
  #if 0   /* Option 2) If GASNet is initialized first enable this: */
  { char cookie[10];
    sprintf(cookie, "%u", gasnetc_cookie + 1);
    gasnett_setenv("PMI_GNI_COOKIE", cookie);
  }
  #endif
  #if 0   /* Option 3) Copy this variant into your code between the initializations */
  { char cookie[10];
    sprintf(cookie, "%u", (1 + atoi(getenv("PMI_GNI_COOKIE"))));
    gasnett_setenv("PMI_GNI_COOKIE", cookie);
  }
  #endif

  /* As good a place as any for this: */
  if (!gasneti_mynode && !gasneti_getenv_yesno_withdefault("GASNET_QUIET",0)) {
    static const char *old_vars[][3] = {
        {"GASNET_PHYSMEM_PINNABLE_RATIO", "removed", "GASNET_PHYSMEM_MAX"},
        {"GASNETC_GNI_MIN_NUM_PD", "removed", "GASNET_GNI_NUM_PD"},
        {"GASNETC_GNI_MIN_BOUNCE_SIZE", "removed", "GASNET_GNI_BOUNCE_SIZE"},
        {"GASNETC_GNI_AM_MEM_CONSISTENCY", "removed", NULL},
        {"GASNETC_GNI_NUM_PD", "renamed", "GASNET_GNI_NUM_PD"},
        {"GASNETC_GNI_BOUNCE_SIZE", "renamed", "GASNET_GNI_BOUNCE_SIZE"},
        {"GASNETC_GNI_MEM_CONSISTENCY", "renamed","GASNET_GNI_MEM_CONSISTENCY"},
        {"GASNETC_GNI_FMA_RDMA_CUTOVER", "split into two variables",
                "GASNET_GNI_GET_FMA_RDMA_CUTOVER and/or GASNET_GNI_PUT_FMA_RDMA_CUTOVER"},
        {"GASNETC_GNI_BOUNCE_REGISTER_CUTOVER", "split into two variables",
                "GASNET_GNI_GET_BOUNCE_REGISTER_CUTOVER and/or GASNET_GNI_PUT_BOUNCE_REGISTER_CUTOVER"},
        {NULL,NULL,NULL}};
    int i;
    for (i=0; old_vars[i][0] != NULL; ++i) {
      const char *what = old_vars[i][0];
      const char *why  = old_vars[i][1];
      const char *who  = old_vars[i][2];
      if (NULL != getenv(what)) {
        fprintf(stderr, "WARNING: Detected a setting for environment variable %s, "
                        "which has been %s.\n", what, why);
        if (who)
          fprintf(stderr, "WARNING: Please set %s instead.\n", who);
      }
    }
  }

  return GASNET_OK;
}

/* ------------------------------------------------------------------------------------ */
/* Bootstrap Barrier and Exchange via AMs
 * Taken from vapi-conduit w/o the thread safetly complications 
 */

static gex_Rank_t gasnetc_dissem_peers = 0;
static gex_Rank_t *gasnetc_dissem_peer = NULL;
static gex_Rank_t *gasnetc_exchange_rcvd = NULL;
static gex_Rank_t *gasnetc_exchange_send = NULL;
static uint32_t gasnetc_sys_barrier_rcvd[2];

static void gasnetc_sys_barrier_reqh(gex_Token_t token, uint32_t arg)
{
    gasnetc_sys_barrier_rcvd[arg&1] |= arg;
}

GASNETI_NEVER_INLINE(gasnetc_bootstrapBarrier_gni,
void gasnetc_bootstrapBarrier_gni(void))
{
    static int phase = 0;
    int pre_attach = !gasneti_attach_done;
    int i;

#if GASNET_PSHM
    gasneti_pshmnet_bootstrapBarrier();
#endif
 
    if (pre_attach) gasneti_attach_done = 1; /* to use AMs before attach */

    for (i = 0; i < gasnetc_dissem_peers; ++i) { /* EMPTY for all but first per supernode */
      const uint32_t mask = 2 << i; /* (distance << 1) */

      gex_AM_RequestShort1(NULL, gasnetc_dissem_peer[i],
                               gasneti_handleridx(gasnetc_sys_barrier_reqh),
                               0, phase | mask);

      /* wait for completion of the proper receive, which might arrive out of order */
      while (!(gasnetc_sys_barrier_rcvd[phase] & mask)) {
         GASNETC_DIDX_POST(GASNETC_DEFAULT_DOMAIN);
         gasnetc_poll(GASNETC_DIDX_PASS_ALONE);  /* No PSHM progress required here */
      }
    }

    if (pre_attach) gasneti_attach_done = 0;

#if GASNET_PSHM
    gasneti_pshmnet_bootstrapBarrier();
#endif

    /* reset for next barrier */
    gasnetc_sys_barrier_rcvd[phase] = 0;
    phase ^= 1;
}

#define GASNETC_SYS_EXCHANGE_MAX GASNETC_GNI_MAX_MEDIUM
static unsigned int gasnetc_sys_exchange_rcvd[2][GASNETC_LOG2_MAXNODES];
static uint8_t *gasnetc_sys_exchange_buf[2] = { NULL, NULL };

static uint8_t *gasnetc_sys_exchange_addr(int phase, size_t elemsz)
{
  if (gasnetc_sys_exchange_buf[phase] == NULL) {
    gasnetc_sys_exchange_buf[phase] = gasneti_malloc(elemsz * gasneti_nodes);
  }
  return gasnetc_sys_exchange_buf[phase];
}

static void gasnetc_sys_exchange_reqh(gex_Token_t token, void *buf,
                                 size_t nbytes, uint32_t arg0,
                                 uint32_t elemsz)
{
  const int phase = arg0 & 1;
  const int step = (arg0 >> 1) & 0x1f;
  const int seq = (arg0 >> 6);
  const size_t offset = elemsz * gasnetc_exchange_rcvd[step];
  uint8_t *dest = gasnetc_sys_exchange_addr(phase, elemsz)
                  + offset + (seq * GASNETC_SYS_EXCHANGE_MAX);

  memcpy(dest, buf, nbytes);
  ++gasnetc_sys_exchange_rcvd[phase][step];
}

GASNETI_NEVER_INLINE(gasnetc_bootstrapExchange_gni,
void gasnetc_bootstrapExchange_gni(void *src, size_t len, void *dest))
{
    static int phase = 0;

    const int pre_attach = !gasneti_attach_done;
    uint8_t *temp = gasnetc_sys_exchange_addr(phase, len);
    int step;

#if GASNET_PSHM
    /* Construct supernode-local contribution */
    gasneti_pshmnet_bootstrapGather(gasneti_request_pshmnet, src, len, temp, 0);
    if (gasneti_nodemap_local_rank) goto end_network_comms;
#else
    /* Copy in local contribution */
    memcpy(temp, src, len);
#endif

    if (pre_attach) gasneti_attach_done = 1; /* to use AMs before attach */

    /* Bruck's concatenation algorithm: */
    for (step = 0; step < gasnetc_dissem_peers; ++step) {
      size_t nbytes = len * gasnetc_exchange_send[step];
      size_t offset = 0;
      uint32_t seq = 0;

      gasneti_assert(step < GASNETC_LOG2_MAXNODES);

      /* Send payload using one or more AMMediums */
      do {
        const size_t to_xfer = MIN(nbytes, GASNETC_SYS_EXCHANGE_MAX);

        gex_AM_RequestMedium2(NULL, gasnetc_dissem_peer[step],
                                  gasneti_handleridx(gasnetc_sys_exchange_reqh),
                                  temp + offset, to_xfer, GEX_EVENT_NOW, 0,
                                  phase | (step << 1) | (seq << 6), len);

        ++seq;
        offset += to_xfer;
        nbytes -= to_xfer;
        gasneti_assert(seq < (1<<(32-6)));
      } while (nbytes);

      /* poll until correct number of messages have been received */
      nbytes = len * (gasnetc_exchange_rcvd[step+1] - gasnetc_exchange_rcvd[step]);
      seq = (nbytes + GASNETC_SYS_EXCHANGE_MAX - 1) / GASNETC_SYS_EXCHANGE_MAX;
      gasneti_polluntil (gasnetc_sys_exchange_rcvd[phase][step] == seq);

      /* reset */
      gasnetc_sys_exchange_rcvd[phase][step] = 0;
    }

    if (pre_attach) gasneti_attach_done = 0;

    /* Copy to destination while performing the "rotation" */
    memcpy(dest, temp + len * (gasneti_nodes - gasneti_mynode), len * gasneti_mynode);
    memcpy((uint8_t*)dest + len * gasneti_mynode, temp, len * (gasneti_nodes - gasneti_mynode));

#if GASNET_PSHM
end_network_comms:
    gasneti_pshmnet_bootstrapBroadcast(gasneti_request_pshmnet, dest, len*gasneti_nodes, dest, 0);
#endif

    /* Prepare for next */
    gasneti_free(temp);
    gasnetc_sys_exchange_buf[phase] = NULL;
    phase ^= 1;

#if GASNET_DEBUG
  /* verify own data as a sanity check */
  if (memcmp(src, (void *) ((uintptr_t ) dest + (gasneti_mynode * len)), len) != 0) {
    gasnetc_GNIT_Abort("exchange failed: self data is incorrect");
  }
#endif
}

static void gasnetc_sys_coll_init(void)
{
  int i;

#if GASNET_PSHM
  const gex_Rank_t size = gasneti_nodemap_global_count;
  const gex_Rank_t rank = gasneti_nodemap_global_rank;

  if (gasneti_nodemap_local_rank) {
    /* No network comms */
    goto done;
  }
#else
  const gex_Rank_t size = gasneti_nodes;
  const gex_Rank_t rank = gasneti_mynode;
#endif

  if (size == 1) {
    /* No network comms */
    goto done;
  }

  /* Construct vector of the dissemination peers */
  for (i = 1; i < size; i *= 2) {
    ++gasnetc_dissem_peers;
  }
  gasnetc_dissem_peer = gasneti_malloc(gasnetc_dissem_peers * sizeof(gex_Rank_t));
  gasneti_leak(gasnetc_dissem_peer);
  for (i = 0; i < gasnetc_dissem_peers; ++i) {
    const gex_Rank_t peer = (rank + size - (1<<i)) % size;
  #if GASNET_PSHM
    /* Convert supernode numbers to node numbers */
    gasnetc_dissem_peer[i] = gasneti_pshm_firsts[peer];
  #else
    gasnetc_dissem_peer[i] = peer;
  #endif
  }

  /* Compute the recv offset and send count for each step of exchange */
  gasnetc_exchange_rcvd = gasneti_calloc(gasnetc_dissem_peers+1, sizeof(gex_Rank_t));
  gasnetc_exchange_send = gasneti_calloc(gasnetc_dissem_peers, sizeof(gex_Rank_t));
  { int step;
  #if GASNET_PSHM
    gex_Rank_t *width;
    gex_Rank_t sum1 = 0;
    gex_Rank_t sum2 = 0;
    gex_Rank_t last = (rank + size - (1<<(gasnetc_dissem_peers-1))) % size;

    /* Step 1: determine the "width" of each supernode */
    width = gasneti_calloc(size, sizeof(gex_Rank_t));
    for (i = 0; i < gasneti_nodes; ++i) {
      width[gasneti_nodeinfo[i].supernode] += 1;
    }
    /* Step 2: form the necessary partial sums */
    for (step = i = 0; step < gasnetc_dissem_peers; ++step) {
      const gex_Rank_t distance = 1 << step;
      for (/*empty*/; i < distance; ++i) {
        sum1 += width[ (rank + i) % size ];
        sum2 += width[ (last + i) % size ];
      }
      gasnetc_exchange_rcvd[step] = gasnetc_exchange_send[step] = sum1;
    }
    gasnetc_exchange_send[step-1] = gasneti_nodes - sum2;
    gasnetc_exchange_rcvd[step] = gasneti_nodes;
    gasneti_free(width);
  #else
    for (step = 0; step < gasnetc_dissem_peers; ++step) {
      gasnetc_exchange_rcvd[step] = gasnetc_exchange_send[step] = 1 << step;
    }
    gasnetc_exchange_send[step-1] = gasneti_nodes - gasnetc_exchange_send[step-1];
    gasnetc_exchange_rcvd[step] = gasneti_nodes;
  #endif
  }

done:
  gasneti_spawner->Cleanup(); /* No further use of PMI-based colelctives */
}

static void gasnetc_sys_coll_fini(void)
{
  gasneti_free(gasnetc_dissem_peer);
  gasneti_free(gasnetc_exchange_rcvd);
  gasneti_free(gasnetc_exchange_send);

#if GASNET_DEBUG
  gasnetc_dissem_peer = NULL;
  gasnetc_exchange_rcvd = NULL;
  gasnetc_exchange_send = NULL;
#endif
}

/* ------------------------------------------------------------------------------------ */

#if 0 /* Currently unused */
/* ---------------------------------------------------------------------------------
 * Helpers for gasnetc_MaxPinMem()
 * --------------------------------------------------------------------------------- */
static void *try_pin_region = NULL;
static uintptr_t try_pin_size = 0;

static void *try_pin_alloc_inner(const uintptr_t size) {
  void *addr = gasneti_huge_mmap(NULL, size);
  if (addr == MAP_FAILED) addr = NULL;
  return addr;
}
static void try_pin_free_inner(void *addr, const uintptr_t size) {
  gasneti_huge_munmap(addr, size);
}

static uintptr_t try_pin_alloc(uintptr_t size, const uintptr_t step) {
  void *addr = try_pin_alloc_inner(size);

  if (!addr) {
    /* Binary search */
    uintptr_t high = size;
    uintptr_t low = step;
    int found = 0;

    while ((high - low) > step) {
      uint64_t mid = (low + high)/2;
      addr = try_pin_alloc_inner(mid);
      if (addr) {
        try_pin_free_inner(addr, mid);
        low = mid;
        found = 1;
      } else {
        high = mid;
      }
    }

    if (!found) return 0;

    size = low;
    addr = try_pin_alloc_inner(low);
    gasneti_assert_always(addr);
  }

  try_pin_region = addr;
  try_pin_size = size;
  return size;
}

static void try_pin_free(void) {
  try_pin_free_inner(try_pin_region, try_pin_size);
  try_pin_region = NULL;
  try_pin_size = 0;
}

static int try_pin(uintptr_t size) {
  return gasnetc_try_pin(try_pin_region, size);
}
#endif

#ifndef GASNETC_DEFAULT_PHYSMEM_MAX
#define GASNETC_DEFAULT_PHYSMEM_MAX "0.8"
#endif
#ifndef GASNETC_PHYSMEM_MIN
#define GASNETC_PHYSMEM_MIN (128*1024*1024)
#endif

/* ---------------------------------------------------------------------------------
 * Determine the largest amount of memory that can be pinned on the node.
 * --------------------------------------------------------------------------------- */
extern uintptr_t gasnetc_MaxPinMem(uintptr_t msgspace)
{
#ifdef GASNETI_USE_HUGETLBFS
  uintptr_t granularity = MAX(GASNETI_MMAP_GRANULARITY, gethugepagesize());
#else
  uintptr_t granularity = GASNETI_MMAP_GRANULARITY;
#endif

  uintptr_t limit;

  /* On CNL, if we try to pin beyond what the OS will allow, the job is killed.
   * So, there is really no way (that we know of) to determine the EXACT maximum
   * pinnable memory under CNL without dire consequences.
   * For this platform, we will simply try a large fraction of the physical
   * memory.  If that is too big, then the job will be killed at startup.
   * The gasneti_mmapLimit() ensures limit is per compute node, not per process.
   */
  uintptr_t pm_limit = gasneti_getenv_memsize_withdefault(
                           "GASNET_PHYSMEM_MAX", GASNETC_DEFAULT_PHYSMEM_MAX,
                           GASNETC_PHYSMEM_MIN, gasneti_getPhysMemSz(1));

#if GASNET_CONDUIT_GEMINI
  /* Even on large memory nodes on Hopper, this appears to be the NIC's limit: */
  pm_limit = MIN(pm_limit, 24UL << 30 /* 24 GB */);
#endif

  /* msgspace is allocated from hugepages (granularity) in every proc */
  msgspace = GASNETI_ALIGNUP(msgspace, granularity) * gasneti_nodemap_local_count;

  if (pm_limit < msgspace || (pm_limit - msgspace) < (granularity * gasneti_nodemap_local_count)) {
    gasneti_fatalerror("Insufficient physical memory left for a GASNet segment");
  }
  pm_limit -= msgspace;

  limit = gasneti_mmapLimit((uintptr_t)-1, pm_limit,
                            &gasnetc_bootstrapExchange_gni,
                            &gasnetc_bootstrapBarrier_gni);

  if (limit < granularity) {
    gasnetc_GNIT_Abort("Unable to alloc and pin minimal memory of size %d bytes",(int)granularity);
  }
  GASNETI_TRACE_PRINTF(C,("MaxPinMem = %"PRIuPTR,limit));
  return (uintptr_t)limit;
}



static int gasnetc_init( gex_Client_t            *client_p,
                         gex_EP_t                *ep_p,
                         const char              *clientName,
                         int                     *argc,
                         char                    ***argv,
                         gex_Flags_t             flags)
{
  uintptr_t msgspace;
  int ret;
  int localranks;
  uint32_t  minlocalrank;
  uint32_t i;

  /*  check system sanity */
  gasnetc_check_config();
  
  if (gasneti_init_done) 
    GASNETI_RETURN_ERRR(NOT_INIT, "GASNet already initialized");

#if GASNET_DEBUG_VERBOSE
    /* note - can't call trace macros during gasnet_init because trace system not yet initialized */
    fprintf(stderr,"gasnetc_init(): about to call gasnetc_init...\n"); fflush(stderr);
#endif

  ret = gasnetc_bootstrapInit(argc, argv);
  if (ret != GASNET_OK) return ret;

  gasneti_init_done = 1; /* enable early to allow tracing */

  gasneti_freezeForDebugger();

  /* Must init timers after global env, and preferably before tracing */
  GASNETI_TICKS_INIT();

  /* Now enable tracing of all the following steps */
  gasneti_trace_init(argc, argv);

  #if GASNET_DEBUG_VERBOSE
    fprintf(stderr,"gasnetc_init(): gasnetc_init done - node %i/%i starting...\n", 
      gasneti_mynode, gasneti_nodes); fflush(stderr);
  #endif

  /* Retreive the nidlist to construct the gasneti_nodemap[]  */
  { int *nidlist;
    ret = PMI_Get_nidlist_ptr((void**) &nidlist);
    gasneti_assert(ret == PMI_SUCCESS);
    gasneti_nodemapInit(NULL, nidlist, sizeof(int), sizeof(int));
  }

  #if GASNET_PSHM
    /* If your conduit will support PSHM, you should initialize it here.
     * The 1st argument is normally "&gasnetc_bootstrapSNodeBroadcast" or equivalent
     * The 2nd argument is the amount of shared memory space needed for any
     * conduit-specific uses.  The return value is a pointer to the space
     * requested by the 2nd argument.
     */
    gasnetc_exitcodes = gasneti_pshm_init(gasneti_spawner->SNodeBroadcast,
                                          gasneti_nodemap_local_count * sizeof(gasnetc_exitcode_t));
    gasnetc_exitcodes[gasneti_nodemap_local_rank].present = 0;
  #endif

  //  Create first Client and EP *here*, for use in subsequent bootstrap collectives
  {
    //  allocate the client object
    gasneti_Client_t client = gasneti_alloc_client(clientName, flags);
    *client_p = gasneti_export_client(client);

    //  create the initial endpoint with internal handlers
    if (gasnetc_EP_Create(ep_p, *client_p, flags))
      GASNETI_RETURN_ERRR(RESOURCE,"Error creating initial endpoint");
    gasneti_EP_t ep = gasneti_import_ep(*ep_p);
    gasnetc_handler = ep->_amtbl; // TODO-EX: this global variable to be removed
  }

  #if GASNET_DEBUG_VERBOSE
    fprintf(stderr,"gasnetc_init(): node %i/%i calling gasnetc_init_messaging.\n", 
      gasneti_mynode, gasneti_nodes); fflush(stderr);
  #endif
  msgspace = gasnetc_init_messaging();
  #if GASNET_DEBUG_VERBOSE
    fprintf(stderr,"gasnetc_init(): node %i/%i finished gasnetc_init_messaging.\n", 
      gasneti_mynode, gasneti_nodes); fflush(stderr);
  #endif

  /* Now that messaging is available, use it for remaining bootstrap collectives */
  gasnetc_sys_coll_init();


  /* allocate and attach an aux segment */
  uintptr_t max_pin = gasnetc_MaxPinMem(msgspace);

  gasneti_auxsegAttach(max_pin, &gasnetc_bootstrapExchange_gni);
  max_pin -= gasneti_seginfo_aux[gasneti_mynode].size;

  /* register auxseg and setup subsystems using it */
  gasnetc_init_gni(gasneti_seginfo_aux[gasneti_mynode]);

  /* determine Max{Local,GLobal}SegmentSize */
  gasneti_segmentInit(max_pin, &gasnetc_bootstrapExchange_gni, flags);

  #if 0
    /* Enable this if you wish to use the default GASNet services for broadcasting 
        the environment from one compute node to all the others (for use in gasnet_getenv(),
        which needs to return environment variable values from the "spawning console").
        You need to provide two functions (gasnetc_bootstrapExchange and gasnetc_bootstrapBroadcast)
        which the system can safely and immediately use to broadcast and exchange information 
        between nodes (gasnetc_bootstrapBroadcast is optional but highly recommended).
        See gasnet/other/mpi-spawner/gasnet_bootstrap_mpi.c for definitions of these two
        functions in terms of MPI collective operations.
       This system assumes that at least one of the compute nodes has a copy of the 
        full environment from the "spawning console" (if this is not true, you'll need to
        implement something yourself to get the values from the spawning console)
       If your job system already always propagates environment variables to all the compute
        nodes, then you probably don't need this.
     */
    gasneti_setupGlobalEnvironment(gasneti_nodes, gasneti_mynode, 
                                   gasnetc_bootstrapExchange, gasnetc_bootstrapBroadcast);
  #endif

#if GASNET_DEBUG_VERBOSE
  fprintf(stderr, "node %i Leaving gasnetc_init\n",gasneti_mynode);
  fflush(stderr);
#endif

  return GASNET_OK;
}

/* ------------------------------------------------------------------------------------ */
static int gasnetc_attach_primary(void) {
  /* ------------------------------------------------------------------------------------ */
  /*  register fatal signal handlers */

  /* catch fatal signals and convert to SIGQUIT */
  gasneti_registerSignalHandlers(gasneti_defaultSignalHandler);

  /*  (###) register any custom signal handlers required by your conduit 
   *        (e.g. to support interrupt-based messaging)
   */

  /* set the number of seconds we poll until forceful shutdown. */
  gasnetc_shutdown_seconds = gasneti_get_exittimeout(120., 3., 0.125, 0.);
  #if HAVE_ON_EXIT
    on_exit(gasnetc_on_exit, NULL);
  #else
    atexit(gasnetc_atexit);
  #endif

  /* ------------------------------------------------------------------------------------ */
  /*  primary attach complete */
  gasneti_attach_done = 1;
  gasnetc_bootstrapBarrier_gni();

  GASNETI_TRACE_PRINTF(C,("gasnetc_attach_primary(): primary attach complete"));

  gasnete_init(); /* init the extended API */

  gasneti_nodemapFini();

  /* ensure extended API is initialized across nodes */
  gasnetc_bootstrapBarrier_gni();

  // tear down conduit-specific bootstrap collectives (not used after attach)
  gasnetc_sys_coll_fini();

  GASNETI_TRACE_PRINTF(C,("gasnetc_attach_primary: done\n"));
  return GASNET_OK;
}
/* ------------------------------------------------------------------------------------ */
static int gasnetc_attach_segment(gex_Segment_t                 *segment_p,
                                  gex_TM_t                      tm,
                                  uintptr_t                     segsize,
                                  gasneti_bootstrapExchangefn_t exchangefn,
                                  gex_Flags_t                   flags) {
  GASNETC_DIDX_POST(GASNETC_DEFAULT_DOMAIN);

  // TODO-EX: crude detection of multiple calls until we support them
  gasneti_assert(NULL == gasneti_seginfo[0].addr);

  /* ------------------------------------------------------------------------------------ */
  /*  register segment  */

  gasneti_segmentAttach(segsize, gasneti_seginfo, exchangefn, flags);

  void *segbase = gasneti_seginfo[gasneti_mynode].addr;
  segsize = gasneti_seginfo[gasneti_mynode].size;

  gasnetc_assert_aligned(segbase, GASNET_PAGESIZE);
  gasnetc_assert_aligned(segsize, GASNET_PAGESIZE);

  gasneti_EP_t ep = gasneti_import_tm(tm)->_ep;
  ep->_segment = gasneti_alloc_segment(ep->_client, segbase, segsize, flags);
  *segment_p = gasneti_export_segment(ep->_segment);

  /* After local segment is attached, call optional client-provided hook
     (###) should call BEFORE any conduit-specific pinning/registration of the segment
   */
  if (gasnet_client_attach_hook) {
    gasnet_client_attach_hook(segbase, segsize);
  }

  /* ------------------------------------------------------------------------------------ */
  /*  gather segment information */

  /* (###) add code here to gather the segment assignment info into
           gasneti_seginfo on each node (may be possible to use AMShortRequest here)
           If gasneti_segmentAttach() was used above, this is already done.
     (LCS) This was done by segmentAttach above
   */

  /* Register client segment */
  gasnetc_init_segment(gasneti_seginfo[gasneti_mynode]);

  gasneti_assert(gasneti_seginfo[gasneti_mynode].addr == segbase &&
                 gasneti_seginfo[gasneti_mynode].size == segsize);

  return GASNET_OK;
}
/* ------------------------------------------------------------------------------------ */
// TODO-EX: this is a candidate for factorization (once we understand the per-conduit variations)
extern int gasnetc_attach( gex_Client_t           *client_p,
                           gex_EP_t               *ep_p,
                           gex_TM_t               *tm_p,
                           gex_Segment_t          *segment_p,
                           gasnet_handlerentry_t  *table,
                           int                    numentries,
                           uintptr_t              segsize)
{
  GASNETI_TRACE_PRINTF(C,("gasnetc_attach(table (%i entries), segsize=%"PRIuPTR")",
                          numentries, segsize));
  gasneti_EP_t ep = gasneti_import_ep(*ep_p);

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
  #else
    segsize = 0;
  #endif

  /*  primary attach  */
  if (GASNET_OK != gasnetc_attach_primary())
    GASNETI_RETURN_ERRR(RESOURCE,"Error in primary attach");

  #if GASNET_SEGMENT_FAST || GASNET_SEGMENT_LARGE
    /*  register client segment  */
    if (GASNET_OK != gasnetc_attach_segment(segment_p, *tm_p, segsize, gasneti_defaultExchange, GASNETI_FLAG_INIT_LEGACY))
      GASNETI_RETURN_ERRR(RESOURCE,"Error attaching segment");
  #endif

  /*  register client handlers */
  if (table && gasneti_amregister_legacy(ep->_amtbl, table, numentries) != GASNET_OK)
    GASNETI_RETURN_ERRR(RESOURCE,"Error registering handlers");

  /* ensure everything is initialized across all nodes */
  gasnet_barrier(0, GASNET_BARRIERFLAG_UNNAMED);

  return GASNET_OK;
}
/* ------------------------------------------------------------------------------------ */
// TODO-EX: this is a candidate for factorization (once we understand the per-conduit variations)
extern int gasnetc_Client_Init(
                               gex_Client_t            *client_p,
                               gex_EP_t                *ep_p,
                               gex_TM_t                *tm_p,
                               const char              *clientName,
                               int                     *argc,
                               char                    ***argv,
                               gex_Flags_t             flags)
{
  gasneti_assert(client_p);
  gasneti_assert(ep_p);
  gasneti_assert(tm_p);
  gasneti_assert(clientName);
#if !GASNET_NULL_ARGV_OK
  gasneti_assert(argc);
  gasneti_assert(argv);
#endif

  //  main init
  // TODO-EX: must split off per-client and per-endpoint portions
  if (!gasneti_init_done) { // First client
    // NOTE: gasnetc_init() creates the first Client and EP for use in bootstrap comms
    int retval = gasnetc_init(client_p, ep_p, clientName, argc, argv, flags);
    if (retval != GASNET_OK) GASNETI_RETURN(retval);
  #if 0
    /* called within gasnetc_init to allow init tracing */
    gasneti_trace_init(argc, argv);
  #endif
  } else { // NOT first client
    //  allocate the client object
    gasneti_Client_t client = gasneti_alloc_client(clientName, flags);
    *client_p = gasneti_export_client(client);

    //  create the initial endpoint with internal handlers
    if (gasnetc_EP_Create(ep_p, *client_p, flags))
      GASNETI_RETURN_ERRR(RESOURCE,"Error creating initial endpoint");
  }
  gasneti_EP_t ep = gasneti_import_ep(*ep_p);

  // TODO-EX: create team
  gasneti_TM_t tm = gasneti_alloc_tm(ep, gasneti_mynode, gasneti_nodes, flags);
  *tm_p = gasneti_export_tm(tm);

  if (0 == (flags & GASNETI_FLAG_INIT_LEGACY)) {
    /*  primary attach  */
    if (GASNET_OK != gasnetc_attach_primary())
      GASNETI_RETURN_ERRR(RESOURCE,"Error in primary attach");

    /* ensure everything is initialized across all nodes */
    gasnet_barrier(0, GASNET_BARRIERFLAG_UNNAMED);
  }

  return GASNET_OK;
}

extern int gasnetc_Segment_Attach(
                gex_Segment_t          *segment_p,
                gex_TM_t               tm,
                uintptr_t              length)
{
  gasneti_assert(segment_p);

  // TODO-EX: remove when this limitation is removed
  static int once = 1;
  if (once) once = 0;
  else gasneti_fatalerror("gex_Segment_Attach: current implementation can be called at most once");

  /* create a segment collectively */
  // TODO-EX: this implementation only works *once*
  // TODO-EX: should be using the team's exchange function if possible
  // TODO-EX: need to pass proper flags (e.g. pshm and bind) instead of 0
  if (GASNET_OK != gasnetc_attach_segment(segment_p, tm, length, gasneti_defaultExchange, 0))
    GASNETI_RETURN_ERRR(RESOURCE,"Error attaching segment");

  void *segbase = gasneti_seginfo[gasneti_mynode].addr;
  uintptr_t segsize = gasneti_seginfo[gasneti_mynode].size;
  const gex_Flags_t flags = 0; /* TODO-EX: BIND, PSHM, etc. */
  gasneti_EP_t ep = gasneti_import_tm(tm)->_ep;
  ep->_segment = gasneti_alloc_segment(ep->_client, segbase, segsize, flags);
  *segment_p = gasneti_export_segment(ep->_segment);

  return GASNET_OK;
}

extern int gasnetc_EP_Create(gex_EP_t           *ep_p,
                             gex_Client_t       client,
                             gex_Flags_t        flags) {
  /* (###) add code here to create an endpoint belonging to the given client */
#if 1 // TODO-EX: This is a stub, which assumes 1 implicit call from ClientCreate
  static gasneti_mutex_t lock = GASNETI_MUTEX_INITIALIZER;
  gasneti_mutex_lock(&lock);
    static int once = 0;
    int prev = once;
    once = 1;
  gasneti_mutex_unlock(&lock);
  if (prev) gasneti_fatalerror("Multiple endpoints are not yet implemented");
#endif

  gasneti_EP_t ep = gasneti_alloc_ep(gasneti_import_client(client), flags);
  *ep_p = gasneti_export_ep(ep);

  { /*  core API handlers */
    gex_AM_Entry_t *ctable = (gex_AM_Entry_t *)gasnetc_get_handlertable();
    int len = 0;
    int numreg = 0;
    gasneti_assert(ctable);
    while (ctable[len].gex_fnptr) len++; /* calc len */
    if (gasneti_amregister(ep->_amtbl, ctable, len, GASNETC_HANDLER_BASE, GASNETE_HANDLER_BASE, 0, &numreg) != GASNET_OK)
      GASNETI_RETURN_ERRR(RESOURCE,"Error registering core API handlers");
    gasneti_assert(numreg == len);
  }

  { /*  extended API handlers */
    gex_AM_Entry_t *etable = (gex_AM_Entry_t *)gasnete_get_handlertable();
    int len = 0;
    int numreg = 0;
    gasneti_assert(etable);
    while (etable[len].gex_fnptr) len++; /* calc len */
    if (gasneti_amregister(ep->_amtbl, etable, len, GASNETE_HANDLER_BASE, GASNETI_CLIENT_HANDLER_BASE, 0, &numreg) != GASNET_OK)
      GASNETI_RETURN_ERRR(RESOURCE,"Error registering extended API handlers");
    gasneti_assert(numreg == len);
  }

  return GASNET_OK;
}

extern int gasnetc_EP_RegisterHandlers(gex_EP_t                ep,
                                       gex_AM_Entry_t          *table,
                                       size_t                  numentries) {
  return gasneti_amregister_client(gasneti_import_ep(ep)->_amtbl, table, numentries);
}
/* ------------------------------------------------------------------------------------ */
static int gasnetc_exit_in_signal = 0;  /* to avoid certain things in signal context */
extern void gasnetc_fatalsignal_callback(int sig) {
  gasnetc_exit_in_signal = 1;
}

static int gasnetc_remoteShutdown = 0;

#if HAVE_ON_EXIT
static void gasnetc_on_exit(int exitcode, void *arg) {
  if (!gasnetc_shutdownInProgress) gasnetc_exit(exitcode);
}
#else
static void gasnetc_atexit(void) {
  if (!gasnetc_shutdownInProgress) gasnetc_exit(0);
}
#endif

static void gasnetc_exit_reqh(gex_Token_t token, gex_AM_Arg_t exitcode) {
  if (!gasnetc_shutdownInProgress) {
    gasneti_sighandlerfn_t handler = gasneti_reghandler(SIGQUIT, SIG_IGN);
    gasnetc_remoteShutdown = 1;
    if ((handler != gasneti_defaultSignalHandler) &&
#ifdef SIG_HOLD
	(handler != (gasneti_sighandlerfn_t)SIG_HOLD) &&
#endif
	(handler != (gasneti_sighandlerfn_t)SIG_ERR) &&
	(handler != (gasneti_sighandlerfn_t)SIG_IGN) &&
	(handler != (gasneti_sighandlerfn_t)SIG_DFL)) {
      (void)gasneti_reghandler(SIGQUIT, handler);
      raise(SIGQUIT);
    }
    if (!gasnetc_shutdownInProgress) gasnetc_exit(exitcode);
  }
}

static void gasnetc_noop(void) { return; }
static void gasnetc_disable_AMs(void) {
  int i;
  for (i = 0; i < GASNETC_MAX_NUMHANDLERS; ++i) {
    gasnetc_handler[i].gex_fnptr = (gex_AM_Fn_t)&gasnetc_noop;
    gasnetc_handler[i].gex_flags = GASNETI_FLAG_AM_ANY;
  }
}

#if GASNET_DEBUG_VERBOSE
static void gasnetc_exit_alarm(int sig) {
  gasneti_reghandler(SIGALRM, SIG_DFL);
  alarm(5);
  gasnett_print_backtrace(STDERR_FILENO);
  gasneti_killmyprocess(SIGALRM);
}
#endif

extern void gasnetc_exit(int exitcode) {
  /* once we start a shutdown, ignore all future SIGQUIT signals or we risk reentrancy */
  gasneti_reghandler(SIGQUIT, SIG_IGN);

  {  /* ensure only one thread ever continues past this point */
    static gasneti_mutex_t exit_lock = GASNETI_MUTEX_INITIALIZER;
    gasneti_mutex_lock(&exit_lock);
  }

  GASNETI_TRACE_PRINTF(C,("gasnetc_exit(%i)\n", exitcode));

  /* LCS Code modelled after portals-conduit */
  /* should prevent us from entering again */
  gasnetc_shutdownInProgress = 1;

  gasnetc_disable_AMs();

  /* HACK borrowed from elan-conduit: release locks we might have held
     If we are exiting from a signal hander, we might already hold some locks.
     In a debug build we want to avoid the resulting assertions, and in all
     builds we don't want to deadlock.
     NOTE: there IS a risk that we make violate a non-reentrant restriction
           as a result.  However, we hope that is relatively small.
     TODO: make this conditional on being in a signal handler context
   */
  #if GASNETC_USE_SPINLOCK
    #define _GASNETC_CLOBBER_LOCK gasneti_spinlock_init
  #else
    #define _GASNETC_CLOBBER_LOCK(pl) do {                     \
          gasneti_mutex_t dummy_lock = GASNETI_MUTEX_INITIALIZER; \
          memcpy((pl), &dummy_lock, sizeof(gasneti_mutex_t));     \
        } while (0)
  #endif
  #if GASNET_DEBUG && !GASNETC_USE_SPINLOCK
    /* prevent deadlock and assertion failures ONLY if we already hold the lock */
    #define GASNETC_CLOBBER_LOCK(pl) \
          if ((pl)->owner == GASNETI_THREADIDQUERY()) gasneti_mutex_unlock(pl)
  #else
    /* clobber the lock, even if held by another thread! */
    #define GASNETC_CLOBBER_LOCK _GASNETC_CLOBBER_LOCK
  #endif
  GASNETC_CLOBBER_LOCK(gasnetc_gni_lock_addr);
  #if GASNETI_THROTTLE_POLLERS
  if (gasnetc_remoteShutdown) {
    /* This one we might hold even in non-signal context */
    GASNETC_CLOBBER_LOCK(&gasneti_throttle_spinpoller);
  }
  #endif
  /* TODO: AM subsystem locks */
  #undef GASNETC_CLOBBER_LOCK
  #undef _GASNETC_CLOBBER_LOCK

  #if GASNET_DEBUG_VERBOSE
    gasneti_reghandler(SIGALRM, &gasnetc_exit_alarm);
  #else
    gasneti_reghandler(SIGALRM, SIG_DFL);
  #endif
    alarm(2 + gasnetc_shutdown_seconds);

  if (gasnetc_remoteShutdown || gasnetc_sys_exit(&exitcode)) {
    /* reduce-with-timeout(exitcode) failed: this is a non-collective exit */
    const int pre_attach = !gasneti_attach_done;
    unsigned int distance;

    gasnetc_shutdown_seconds *= 2; /* allow twice as long as for the collective case */

    alarm(2 + gasnetc_shutdown_seconds);
    /* "best-effort" to induce a SIGQUIT on any nodes that aren't yet exiting.
       We send to log(N) peers and expect everyone will "eventually" hear.
       Those who are already exiting will ignore us, but will also be sending.
     */
    if (pre_attach) gasneti_attach_done = 1; /* so we can poll for credits */
    for (distance = 1; distance < gasneti_nodes; distance *= 2) {
      gex_Rank_t peer = (distance >= gasneti_nodes - gasneti_mynode)
                                ? gasneti_mynode - (gasneti_nodes - distance)
                                : gasneti_mynode + distance;
      gex_AM_RequestShort1(NULL, peer, gasneti_handleridx(gasnetc_exit_reqh), 0, exitcode);
    }
    if (pre_attach) gasneti_attach_done = 0;

    /* Now we try again, noting that any partial results from 1st attempt are harmless */
    alarm(2 + gasnetc_shutdown_seconds);
    if (gasnetc_sys_exit(&exitcode)) {
#if 0
      fprintf(stderr, "Failed to coordinate an orderly shutdown\n");
      fflush(stderr);
#endif

      /* Death of any process by a fatal signal will cause launcher to kill entire job.
       * We don't use INT or TERM since one could be blocked if we are in its handler. */
      gasnetc_sys_fini();
      raise(SIGALRM); /* Consistent */
      gasneti_killmyprocess(exitcode); /* last chance */
    }
  }

  alarm(2 + gasnetc_shutdown_seconds);
  gasnetc_sys_fini();

  gasneti_flush_streams();
  gasneti_trace_finish();
  gasneti_sched_yield();
  gasnetc_shutdown();

  gasneti_spawner->Fini();  /* normal exit */
  gasneti_killmyprocess(exitcode); /* last chance */
  gasnetc_GNIT_Abort("gasnetc_exit failed!");
}

/* ------------------------------------------------------------------------------------ */
/*
  Misc. Active Message Functions
  ==============================
*/
#if GASNET_PSHM
/* (###) GASNETC_GET_HANDLER
 *   If your conduit will support PSHM, then there needs to be a way
 *   for PSHM to see your handler table.  If you use the recommended
 *   implementation then you don't need to do anything special.
 *   Othwerwise, #define GASNETC_GET_HANDLER in gasnet_core_fwd.h and
 *   implement gasnetc_get_handler() as a macro in
 *   gasnet_core_internal.h
 *
 * (###) GASNETC_TOKEN_CREATE
 *   If your conduit will support PSHM, then there needs to be a way
 *   for the conduit-specific and PSHM token spaces to co-exist.
 *   The default PSHM implementation produces tokens with the least-
 *   significant bit set and assumes the conduit never will.  If that
 *   is true, you don't need to do anything special here.
 *   If your conduit cannot use the default PSHM token code, then
 *   #define GASNETC_TOKEN_CREATE in gasnet_core_fwd.h and implement
 *   the associated routines described in gasnet_pshm.h.  That code
 *   could be functions located here, or could be macros or inlines
 *   in gasnet_core_internal.h.
 */
#endif

extern gex_TI_t gasnetc_Token_Info(
                gex_Token_t         token,
                gex_Token_Info_t    *info,
                gex_TI_t            mask)
{
  gasneti_assert(token);
  gasneti_assert(info);
  gex_TI_t result = 0;

#if GASNET_PSHM
  if (gasnetc_token_is_pshm(token)) {
    return gasnetc_AMPSHM_TokenInfo(token, info, mask);
  }
#endif

  gasnetc_token_t *real_token = (gasnetc_token_t *)token;
  if (mask & GEX_TI_SRCRANK) {
    info->gex_srcrank = real_token->source;
    gasneti_assert(info->gex_srcrank < gasneti_nodes);
    result |= GEX_TI_SRCRANK;
  }
  if (mask & GEX_TI_ENTRY) {
    info->gex_entry = real_token->entry;
    result |= GEX_TI_ENTRY;
  }

  // TODO: real_token can (w/ fixes for identifier scope) answer the following:
  //   isShort = (gasnetc_am_command(real_token->notify) == GC_CMD_AM_SHORT)
  //   isRequest = (notify_get_type(real_token->notify) == notify_request)

  return result;
}

extern int gasnetc_AMPoll(GASNETI_THREAD_FARG_ALONE)
{
  GASNETC_DIDX_POST(GASNETI_MYTHREAD->domain_idx);
  GASNETI_CHECKATTACH();

#if GASNET_PSHM
  /* (###) If your conduit will support PSHM, let it make progress here. */
  gasneti_AMPSHMPoll(0 GASNETI_THREAD_PASS);
#endif

  /* (###) add code here to run your AM progress engine */
  gasnetc_poll(GASNETC_DIDX_PASS_ALONE);

  return GASNET_OK;
}

/* ------------------------------------------------------------------------------------ */
/*
  Active Message Request Functions
  ================================
*/

GASNETI_INLINE(gasnetc_general_am_send_common)
int gasnetc_general_am_send_common(gasnetc_post_descriptor_t *gpd)
{
  int retval;
  gasneti_suspend_spinpollers();
  retval = gasnetc_send_am(gpd);
  gasneti_resume_spinpollers();
  return(retval);
}

#define gasnetc_general_am_send_request gasnetc_general_am_send_common

GASNETI_INLINE(gasnetc_general_am_send_reply)
int gasnetc_general_am_send_reply(gasnetc_post_descriptor_t *gpd, gex_Token_t t)
{
  return ((gasnetc_token_t *)t)->deferred_reply
             ? GASNET_OK
             : gasnetc_general_am_send_common(gpd);
}

GASNETI_INLINE(reply_node)
gex_Rank_t reply_node(gex_Token_t t)
{
    return(((gasnetc_token_t *)t)->source);
}

#if GASNET_PSHM
/*------------------- local delivery cases (PSHM) ------------------ */
#define gasnetc_local_request(dest) gasneti_pshm_in_supernode(dest)
#define gasnetc_local_reply(token)  gasnetc_token_is_pshm(token)

GASNETI_INLINE(gasnetc_local_short_common)
int gasnetc_local_short_common(int is_req, gex_Token_t token, gex_Rank_t dest, gex_AM_Index_t handler,
                               gex_Flags_t flags, int numargs, va_list argptr)
{
  if (is_req) {
    return gasneti_AMPSHM_RequestGeneric(gasneti_Short, dest, handler,
                                         0, 0, 0,
                                         flags, numargs, argptr);
  } else {
    return gasneti_AMPSHM_ReplyGeneric(gasneti_Short, token, handler,
                                       0, 0, 0,
                                       flags, numargs, argptr);
  }
}

GASNETI_INLINE(gasnetc_local_medium_common)
int gasnetc_local_medium_common(int is_req, gex_Token_t token, gex_Rank_t dest, gex_AM_Index_t handler,
                                void *source_addr, size_t nbytes,
                                gex_Flags_t flags, int numargs, va_list argptr)
{
  if (is_req) {
    return gasneti_AMPSHM_RequestGeneric(gasneti_Medium, dest, handler,
                                         source_addr, nbytes, 0,
                                         flags, numargs, argptr);
  } else {
    return gasneti_AMPSHM_ReplyGeneric(gasneti_Medium, token, handler,
                                       source_addr, nbytes, 0,
                                       flags, numargs, argptr);
  }
}

GASNETI_INLINE(gasnetc_local_long_common)
int gasnetc_local_long_common(int is_req, gex_Token_t token, gex_Rank_t dest, gex_AM_Index_t handler,
                              void *source_addr, size_t nbytes,
                              void *dest_addr,
                              gex_Flags_t flags, int numargs, va_list argptr)
{
  if (is_req) {
    return gasneti_AMPSHM_RequestGeneric(gasneti_Long, dest, handler,
                                         source_addr, nbytes, dest_addr,
                                         flags, numargs, argptr);
  } else {
    return gasneti_AMPSHM_ReplyGeneric(gasneti_Long, token, handler,
                                       source_addr, nbytes, dest_addr,
                                       flags, numargs, argptr);
  }
}
#else
/*------------------- local delivery cases (non-PSHM) ------------------ */
#define gasnetc_local_request(dest) ((dest) == gasneti_mynode)
#define gasnetc_local_reply(token)  (reply_node(token) == gasneti_mynode)

GASNETI_INLINE(gasnetc_local_short_common)
int gasnetc_local_short_common(int is_req, gex_Token_t token_arg, gex_Rank_t dest, gex_AM_Index_t handler,
                               gex_Flags_t flags, int numargs, va_list argptr)
{
  const gex_AM_Entry_t * const handler_entry = &gasnetc_handler[handler]; // TODO-EX: per-EP table
  const gex_AM_Fn_t handler_fn = handler_entry->gex_fnptr;
  gasnetc_token_t the_token = { gasneti_mynode, handler_entry, is_req, 0, NULL };
  gex_Token_t token = (gex_Token_t)&the_token; /* RUN macros need an lvalue */
  gex_AM_Arg_t args[GASNETC_MAX_ARGS];
  
  gasneti_amtbl_check(handler_entry, numargs, gasneti_Short, is_req);
  for (int i = 0; i < numargs; i++) {
    args[i] = (gex_AM_Arg_t)va_arg(argptr, gex_AM_Arg_t);
  }
  GASNETI_RUN_HANDLER_SHORT(is_req,handler,handler_fn,token,args,numargs);
  return(GASNET_OK);
}

GASNETI_INLINE(gasnetc_local_medium_common)
int gasnetc_local_medium_common(int is_req, gex_Token_t token_arg, gex_Rank_t dest, gex_AM_Index_t handler,
                                void *source_addr, size_t nbytes,
                                gex_Flags_t flags, int numargs, va_list argptr)
{
  const gex_AM_Entry_t * const handler_entry = &gasnetc_handler[handler]; // TODO-EX: per-EP table
  const gex_AM_Fn_t handler_fn = handler_entry->gex_fnptr;
  gasnetc_token_t the_token = { gasneti_mynode, handler_entry, is_req, 0, NULL };
  gex_Token_t token = (gex_Token_t)&the_token; /* RUN macros need an lvalue */
  gex_AM_Arg_t args[GASNETC_MAX_ARGS];
  void *payload = alloca(nbytes);
  
  gasneti_amtbl_check(handler_entry, numargs, gasneti_Medium, is_req);
  for (int i = 0; i < numargs; i++) {
    args[i] = (gex_AM_Arg_t)va_arg(argptr, gex_AM_Arg_t);
  }
  memcpy(payload, source_addr, nbytes);
  GASNETI_RUN_HANDLER_MEDIUM(is_req,handler,handler_fn,token,args,numargs,payload,nbytes);
  return(GASNET_OK);
}


GASNETI_INLINE(gasnetc_local_long_common)
int gasnetc_local_long_common(int is_req, gex_Token_t token_arg, gex_Rank_t dest, gex_AM_Index_t handler,
                               void *source_addr, size_t nbytes,
                               void *dest_addr, 
                               gex_Flags_t flags, int numargs, va_list argptr)
{
  const gex_AM_Entry_t * const handler_entry = &gasnetc_handler[handler]; // TODO-EX: per-EP table
  const gex_AM_Fn_t handler_fn = handler_entry->gex_fnptr;
  gasnetc_token_t the_token = { gasneti_mynode, handler_entry, is_req, 0, NULL };
  gex_Token_t token = (gex_Token_t)&the_token; /* RUN macros need an lvalue */
  gex_AM_Arg_t args[GASNETC_MAX_ARGS];
  
  gasneti_amtbl_check(handler_entry, numargs, gasneti_Long, is_req);
  for (int i = 0; i < numargs; i++) {
    args[i] = (gex_AM_Arg_t)va_arg(argptr, gex_AM_Arg_t);
  }

  memcpy(dest_addr, source_addr, nbytes);
  gasneti_sync_writes(); /* sync memcpy */
  GASNETI_RUN_HANDLER_LONG(is_req,handler,handler_fn,token,args,numargs,dest_addr,nbytes);
  return(GASNET_OK);
}
#endif /* !GASNET_PSHM */

/*------------------- header formatting ------------------ */
GASNETI_INLINE(gasnetc_format_short)
void gasnetc_format_short(gasnetc_post_descriptor_t *gpd,
                         gex_AM_Index_t handler,
                         int numargs, 
                         va_list argptr)
{
  gasnetc_packet_t *m = (gasnetc_packet_t *)gpd->gpd_am_packet;
  int i;

  gpd->gpd_am_header |= gasnetc_build_am_header(GC_CMD_AM_SHORT, numargs, handler, 0);
  for (i = 0; i < numargs; i++) {
    m->gasp.args[i] = va_arg(argptr, gex_AM_Arg_t);
  }
}


GASNETI_INLINE(gasnetc_format_medium)
void gasnetc_format_medium(gasnetc_post_descriptor_t *gpd,
                           gex_AM_Index_t handler,
                           void *source_addr, 
                           size_t nbytes,
                           int numargs, 
                           va_list argptr)
{
  gasnetc_packet_t *m = (gasnetc_packet_t *)gpd->gpd_am_packet;
  int i;

  gpd->gpd_am_header |= gasnetc_build_am_header(GC_CMD_AM_MEDIUM, numargs, handler, nbytes);
  for (i = 0; i < numargs; i++) {
    m->gamp.args[i] = va_arg(argptr, gex_AM_Arg_t);
  }
  
  memcpy((void*)((uintptr_t)m + GASNETC_HEADLEN(medium, numargs)), source_addr, nbytes);
}


GASNETI_INLINE(gasnetc_format_long)
void gasnetc_format_long(gasnetc_post_descriptor_t *gpd,
                         int is_packed,
                         gex_AM_Index_t handler,
                         size_t nbytes,
                         void *dest_addr,
                         int numargs, va_list argptr)
{
  gasnetc_packet_t *m = (gasnetc_packet_t *)gpd->gpd_am_packet;
  int i;
  
  gpd->gpd_am_header |= gasnetc_build_am_header((int)GC_CMD_AM_LONG+is_packed, numargs, handler, 0);

  m->galp.data_length = nbytes;
  m->galp.data = dest_addr;
  for (i = 0; i < numargs; i++) {
    m->galp.args[i] = va_arg(argptr, gex_AM_Arg_t);
  }
}

/*------------------- long payloads ------------------ */

GASNETI_INLINE(gasnetc_wait_long_payload)
void gasnetc_wait_long_payload( const int initiated,
                                gasneti_weakatomic_t *completed_p
                                GASNETC_DIDX_FARG)
{
  gasnetc_poll_local_queue(GASNETC_DIDX_PASS_ALONE);
  while(initiated != gasneti_weakatomic_read(completed_p, 0)) {
    GASNETI_WAITHOOK();
    gasnetc_poll_local_queue(GASNETC_DIDX_PASS_ALONE);
  }
}

GASNETI_INLINE(gasnetc_put_long_payload)
int gasnetc_put_long_payload( gex_Rank_t dest,
                              void *dst_addr,
                              void *src_addr,
                              size_t nbytes,
                              gex_Flags_t flags,
                              gasneti_weakatomic_t *completed_p
                              GASNETC_DIDX_FARG)
{
  int initiated = 0;
  size_t chunk = nbytes;
  
  gasneti_suspend_spinpollers();
  for (;;) {
    gasnetc_post_descriptor_t *gpd = gasnetc_alloc_post_descriptor(flags GASNETC_DIDX_PASS);
    if_pf (!gpd) break;
    flags &= ~GEX_FLAG_IMMEDIATE;
    gpd->gpd_completion = (uintptr_t) completed_p;
    gpd->gpd_flags = GC_POST_COMPLETION_CNTR;
    chunk = gasnetc_rdma_put_bulk(dest, dst_addr, src_addr, chunk, gpd);
    initiated += 1;
    if_pt (0 == (nbytes -= chunk)) break; /* expect to finish in one pass */

    dst_addr = (char *)dst_addr + chunk;
    src_addr = (char *)src_addr + chunk;
  }
  gasneti_resume_spinpollers();
  return initiated;
}

GASNETI_INLINE(gasnetc_put_longasync_payload)
int gasnetc_put_longasync_payload( gex_Rank_t dest,
                                   void *dst_addr,
                                   void *src_addr,
                                   size_t nbytes,
                                   gasnetc_post_descriptor_t *header_gpd
                                   GASNETC_DIDX_FARG)
{
  int retval = GASNET_OK;
  size_t chunk = nbytes;
  gasneti_weakatomic_t * const counter = &header_gpd->u.counter;
  
  gasneti_weakatomic_set(counter, 2, 0);

  gasneti_suspend_spinpollers();
  for (;;) {
    gasnetc_post_descriptor_t *gpd = gasnetc_alloc_post_descriptor(0 GASNETC_DIDX_PASS);
    gpd->gpd_completion = (uintptr_t) header_gpd;
    gpd->gpd_flags = GC_POST_COMPLETION_SEND;
    chunk = gasnetc_rdma_put_bulk(dest, dst_addr, src_addr, chunk, gpd);
    if_pt (0 == (nbytes -= chunk)) break; /* expect to finish in one pass */

    dst_addr = (char *)dst_addr + chunk;
    src_addr = (char *)src_addr + chunk;
    gasneti_weakatomic_increment(counter, 0);
  }
  if (gasneti_weakatomic_decrement_and_test(counter, 0)) {
    retval = gasnetc_send_am(header_gpd);
  }
  gasneti_resume_spinpollers();
  
  return retval;
}

/*------------------- external requests ------------------ */
extern int gasnetc_AMRequestShortM(
                            gex_TM_t tm,/* local context */
                            gex_Rank_t dest,       /* with tm, defines remote context */
                            gex_AM_Index_t handler, /* index into destination endpoint's handler table */
                            gex_Flags_t flags
                            GASNETI_THREAD_FARG,
                            int numargs, ...) {
  int retval = 1; // assume IMMEDIATE fails
  va_list argptr;
  GASNETI_COMMON_AMREQUESTSHORT(tm,dest,handler,flags,numargs);
  gasneti_AMPoll(); /* poll at least once, to assure forward progress */
  va_start(argptr, numargs); /*  pass in last argument */
  if_pt (gasnetc_local_request(dest)) {
    retval = gasnetc_local_short_common(1, NULL, dest, handler, flags, numargs, argptr);
  } else {
    const size_t total_len = GASNETC_HEADLEN(short, numargs);
    gasnetc_post_descriptor_t *gpd = gasnetc_alloc_request_post_descriptor(dest, total_len, flags GASNETI_THREAD_PASS);
    if_pf (!gpd) goto out_immediate;

    gasnetc_format_short(gpd, handler, numargs, argptr);
    retval = gasnetc_general_am_send_request(gpd);
  }
out_immediate:
  va_end(argptr);
  return retval;
}

extern int gasnetc_AMRequestMediumM(
                            gex_TM_t tm,/* local context */
                            gex_Rank_t dest,       /* with tm, defines remote context */
                            gex_AM_Index_t handler, /* index into destination endpoint's handler table */
                            void *source_addr, size_t nbytes,   /* data payload */
                            gex_Event_t *lc_opt,       /* local completion of payload */
                            gex_Flags_t flags
                            GASNETI_THREAD_FARG,
                            int numargs, ...) {
  int retval = 1; // assume IMMEDIATE fails
  va_list argptr;
  GASNETI_COMMON_AMREQUESTMEDIUM(tm,dest,handler,source_addr,nbytes,lc_opt,flags,numargs);
  gasneti_AMPoll(); /* poll at least once, to assure forward progress */
  gasneti_leaf_finish(lc_opt); // TODO-EX: should support async local completion
  va_start(argptr, numargs); /*  pass in last argument */
  if_pt (gasnetc_local_request(dest)) {
    retval = gasnetc_local_medium_common(1, NULL, dest, handler, source_addr, nbytes, flags, numargs, argptr);
  } else {
    const size_t total_len = GASNETC_HEADLEN(medium, numargs) + nbytes;
    gasnetc_post_descriptor_t *gpd = gasnetc_alloc_request_post_descriptor(dest, total_len, flags GASNETI_THREAD_PASS);
    if_pf (!gpd) goto out_immediate;

    gasnetc_format_medium(gpd, handler,source_addr,nbytes,numargs,argptr);
    retval = gasnetc_general_am_send_request(gpd);
  }
out_immediate:
  va_end(argptr);
  return retval;
}

extern int gasnetc_AMRequestLongM(
                            gex_TM_t tm,/* local context */
                            gex_Rank_t dest,       /* with tm, defines remote context */
                            gex_AM_Index_t handler, /* index into destination endpoint's handler table */
                            void *source_addr, size_t nbytes,   /* data payload */
                            void *dest_addr,                    /* data destination on destination node */
                            gex_Event_t *lc_opt,       /* local completion of payload */
                            gex_Flags_t flags
                            GASNETI_THREAD_FARG,
                            int numargs, ...) {
  int retval = 1; // assume IMMEDIATE fails
  va_list argptr;
  GASNETI_COMMON_AMREQUESTLONG(tm,dest,handler,source_addr,nbytes,dest_addr,lc_opt,flags,numargs);
  gasneti_AMPoll(); /* poll at least once, to assure forward progress */
  gasneti_leaf_finish(lc_opt); // TODO-EX: should support async local completion
  va_start(argptr, numargs); /*  pass in last argument */
  if_pt (gasnetc_local_request(dest)) {
    retval = gasnetc_local_long_common(1, NULL, dest, handler, source_addr, nbytes, dest_addr, flags, numargs, argptr);
  } else {
    GASNETC_DIDX_POST((gasnete_mythread())->domain_idx);
    int initiated = 0;
    gasneti_weakatomic_t completed = gasneti_weakatomic_init(0);
    const int is_packed = (nbytes <= GASNETC_MAX_PACKED_LONG(numargs));
    const size_t head_len = GASNETC_HEADLEN(long, numargs);
    const size_t total_len = head_len + (is_packed ? nbytes : 0);
    gasnetc_post_descriptor_t *gpd;
    gasnetc_packet_t *p;

    if (!is_packed) { /* Launch RDMA put as early as possible */
      initiated = gasnetc_put_long_payload(dest, dest_addr, source_addr,
                                           nbytes, flags, &completed GASNETC_DIDX_PASS);
      if_pf (!initiated) goto out_immediate;
      flags &= ~GEX_FLAG_IMMEDIATE;
    }
    
    /* Overlap gpd and/or credit stalls, if any, w/ the RDMA */
    gpd = gasnetc_alloc_request_post_descriptor(dest, total_len, flags GASNETI_THREAD_PASS);
    if_pf (!gpd) goto out_immediate;

    gasnetc_format_long(gpd, is_packed, handler, nbytes, dest_addr, numargs, argptr);

    if (is_packed) {
      memcpy((void*)(gpd->gpd_am_packet + head_len), source_addr, nbytes);
    } else {
      /* Poll for the RDMA completion */
      gasnetc_wait_long_payload(initiated, &completed GASNETC_DIDX_PASS);
    }
    retval = gasnetc_general_am_send_request(gpd);
  }
out_immediate:
  va_end(argptr);
  return retval;
}

/*------------------- external replies ------------------ */

extern int gasnetc_AMReplyShortM(
                            gex_Token_t token,     /* token provided on handler entry */
                            gex_AM_Index_t handler, /* index into destination endpoint's handler table */
                            gex_Flags_t flags,
                            int numargs, ...) {
  int retval = 1; // assume IMMEDIATE fails
  va_list argptr;
  GASNETI_COMMON_AMREPLYSHORT(token,handler,flags,numargs);
  va_start(argptr, numargs); /*  pass in last argument */
  if_pt (gasnetc_local_reply(token)) {
    retval = gasnetc_local_short_common(0, token, 0, handler, flags, numargs, argptr);
  } else {
    const size_t total_len = GASNETC_HEADLEN(short, numargs);
    gasnetc_post_descriptor_t *gpd = gasnetc_alloc_reply_post_descriptor(token, total_len, flags);
    if_pf (!gpd) goto out_immediate;

    gasnetc_format_short(gpd, handler,numargs,argptr);
    retval = gasnetc_general_am_send_reply(gpd, token);
  }
out_immediate:
  va_end(argptr);
  return retval;
}

extern int gasnetc_AMReplyMediumM(
                            gex_Token_t token,     /* token provided on handler entry */
                            gex_AM_Index_t handler, /* index into destination endpoint's handler table */
                            void *source_addr, size_t nbytes,   /* data payload */
                            gex_Event_t *lc_opt,       /* local completion of payload */
                            gex_Flags_t flags,
                            int numargs, ...) {
  int retval = 1; // assume IMMEDIATE fails
  va_list argptr;

  GASNETI_COMMON_AMREPLYMEDIUM(token,handler,source_addr,nbytes,lc_opt,flags,numargs);
  gasneti_leaf_finish(lc_opt); // TODO-EX: should support async local completion
  va_start(argptr, numargs); /*  pass in last argument */
  if_pt (gasnetc_local_reply(token)) {
    retval = gasnetc_local_medium_common(0, token, 0, handler, source_addr, nbytes, flags, numargs, argptr);
  } else {
    const size_t total_len = GASNETC_HEADLEN(medium, numargs) + nbytes;
    gasnetc_post_descriptor_t *gpd = gasnetc_alloc_reply_post_descriptor(token, total_len, flags);
    if_pf (!gpd) goto out_immediate;

    gasnetc_format_medium(gpd, handler,source_addr,nbytes,numargs,argptr);
    retval = gasnetc_general_am_send_reply(gpd, token);
  }
out_immediate:
  va_end(argptr);
  return retval;
}

extern int gasnetc_AMReplyLongM(
                            gex_Token_t token,     /* token provided on handler entry */
                            gex_AM_Index_t handler, /* index into destination endpoint's handler table */
                            void *source_addr, size_t nbytes,   /* data payload */
                            void *dest_addr,                    /* data destination on destination node */
                            gex_Event_t *lc_opt,       /* local completion of payload */
                            gex_Flags_t flags,
                            int numargs, ...) {
  int retval = 1; // assume IMMEDIATE fails
  va_list argptr;
  GASNETI_COMMON_AMREPLYLONG(token,handler,source_addr,nbytes,dest_addr,lc_opt,flags,numargs);
  gasneti_leaf_finish(lc_opt); // TODO-EX: should support async local completion
  va_start(argptr, numargs); /*  pass in last argument */
  if_pt (gasnetc_local_reply(token)) {
    retval = gasnetc_local_long_common(0, token, 0, handler, source_addr, nbytes, dest_addr, flags, numargs, argptr);
  } else {
    GASNETC_DIDX_POST((gasnete_mythread())->domain_idx);
    int initiated = 0;
    gasneti_weakatomic_t completed = gasneti_weakatomic_init(0);
    const int is_packed = (nbytes <= GASNETC_MAX_PACKED_LONG(numargs));
    const size_t head_len = GASNETC_HEADLEN(long, numargs);
    const size_t total_len = head_len + (is_packed ? nbytes : 0);
    gasnetc_post_descriptor_t *gpd;

    if (!is_packed) { /* Launch RDMA put as early as possible */
      initiated = gasnetc_put_long_payload(reply_node(token), dest_addr, source_addr,
                                           nbytes, flags, &completed GASNETC_DIDX_PASS);
      if_pf (!initiated) goto out_immediate;
      flags &= ~GEX_FLAG_IMMEDIATE;
    }
    
    /* Overlap gpd stall, if any, w/ the RDMA */
    gpd = gasnetc_alloc_reply_post_descriptor(token, total_len, flags);
    if_pf (!gpd) goto out_immediate;

    gasnetc_format_long(gpd, is_packed, handler, nbytes, dest_addr, numargs, argptr);

    if (is_packed) {
      memcpy((void*)(gpd->gpd_am_packet + head_len), source_addr, nbytes);
    } else {    
      /* Poll for the RDMA completion */
      gasnetc_wait_long_payload(initiated, &completed GASNETC_DIDX_PASS);
    }
    retval = gasnetc_general_am_send_reply(gpd, token);
  }
out_immediate:
  va_end(argptr);
  return retval;
}

/* ------------------------------------------------------------------------------------ */
/*
  Handler-safe locks
  ==================
*/
#if !GASNETC_NULL_HSL
extern void gasnetc_hsl_init   (gex_HSL_t *hsl) {
  GASNETI_CHECKATTACH();
  gasneti_mutex_init(&(hsl->lock));
}

extern void gasnetc_hsl_destroy(gex_HSL_t *hsl) {
  GASNETI_CHECKATTACH();
  gasneti_mutex_destroy(&(hsl->lock));
}

extern void gasnetc_hsl_lock   (gex_HSL_t *hsl) {
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
}

extern void gasnetc_hsl_unlock (gex_HSL_t *hsl) {
  GASNETI_CHECKATTACH();

  GASNETI_TRACE_EVENT_TIME(L, HSL_UNLOCK, GASNETI_TICKS_NOW_IFENABLED(L)-hsl->acquiretime);

  gasneti_mutex_unlock(&(hsl->lock));
}

extern int  gasnetc_hsl_trylock(gex_HSL_t *hsl) {
  GASNETI_CHECKATTACH();

  {
    int locked = (gasneti_mutex_trylock(&(hsl->lock)) == 0);

    GASNETI_TRACE_EVENT_VAL(L, HSL_TRYLOCK, locked);
    if (locked) {
      #if GASNETI_STATS_OR_TRACE
        hsl->acquiretime = GASNETI_TICKS_NOW_IFENABLED(L);
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
static gex_AM_Entry_t const gasnetc_handlers[] = {
  #ifdef GASNETC_COMMON_HANDLERS
    GASNETC_COMMON_HANDLERS(),
  #endif

  /* ptr-width independent handlers */
    gasneti_handler_tableentry_no_bits(gasnetc_exit_reqh,1,REQUEST,SHORT,0),
    gasneti_handler_tableentry_no_bits(gasnetc_sys_barrier_reqh,1,REQUEST,SHORT,0),
    gasneti_handler_tableentry_no_bits(gasnetc_sys_exchange_reqh,2,REQUEST,MEDIUM,0),

  /* ptr-width dependent handlers */

    GASNETI_HANDLER_EOT
};

gex_AM_Entry_t const *gasnetc_get_handlertable(void) {
  return gasnetc_handlers;
}

#if defined(GASNETC_PTHREAD_CREATE_OVERRIDE)
#if !GASNETC_USE_MULTI_DOMAIN 
  #error Unexpected defn of GASNETC_PTHREAD_CREATE_OVERRIDE
#endif
extern int gasnetc_pthread_create(gasnetc_pthread_create_fn_t *create_fn, pthread_t *thread, const pthread_attr_t *attr, void * (*fn)(void *), void * arg) {
     /* One's already created, count extras.*/
     static int gasnetc_thread_count = 1;
     gasnetc_create_parallel_domain(gasnetc_thread_count++);
     return (*create_fn)(thread, attr, fn, arg);
}
#endif /* defined(GASNETC_PTHREAD_CREATE_OVERRIDE) */

/* ------------------------------------------------------------------------------------ */

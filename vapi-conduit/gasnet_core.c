/*   $Source: /Users/kamil/work/gasnet-cvs2/gasnet/vapi-conduit/Attic/gasnet_core.c,v $
 *     $Date: 2004/10/22 20:56:32 $
 * $Revision: 1.61 $
 * Description: GASNet vapi conduit Implementation
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

#include <sys/time.h>
#include <sys/resource.h>
 
GASNETI_IDENT(gasnetc_IdentString_Version, "$GASNetCoreLibraryVersion: " GASNET_CORE_VERSION_STR " $");
GASNETI_IDENT(gasnetc_IdentString_ConduitName, "$GASNetConduitName: " GASNET_CORE_NAME_STR " $");


/* ------------------------------------------------------------------------------------ */
/*
  Configuration
  ==============

  These calues can be overridden by environment variables.
  Variable names are formed by replacing GASNETC_DEFAULT_ by GASNET_
*/

/* Default HCA and Port */
#define GASNETC_DEFAULT_HCA_ID		""		/* NULL or empty = probe */
#define GASNETC_DEFAULT_PORT_NUM	0		/* 0 = use lowest-numbered active port */

/* Limits on in-flight (sent but not acknowledged) RDMA Ops */
#define GASNETC_DEFAULT_OP_OUST_LIMIT	1024	/* Max RDMA ops outstanding at source */
#define GASNETC_DEFAULT_OP_OUST_PP	64	/* Max RDMA ops outstanding to each peer */

/* Limits on in-flight (sent but not acknowledged) AM Requests */
#define GASNETC_DEFAULT_AM_OUST_LIMIT	32767	/* Max requests outstanding at source (NOT FULLY IMPLEMENTED) */
#define GASNETC_DEFAULT_AM_OUST_PP	32	/* Max requests outstanding to each peer */

/* Spare AM buffers used to accelerate flow control */
#if GASNETC_CLI_PAR
  #define GASNETC_DEFAULT_AM_SPARES	4	/* assume <= 4 threads in GASNet */
#elif GASNETC_RCV_THREAD
  #define GASNETC_DEFAULT_AM_SPARES	2	/* single client + AM recv thread */
#else
  #define GASNETC_DEFAULT_AM_SPARES	1	/* just a single client thread */
#endif

/* Limit on prepinned send bounce buffers */
#define GASNETC_DEFAULT_BBUF_LIMIT	1024	/* Max bounce buffers prepinned */

/*
  These calues cannot yet be overridden by environment variables.
*/
#define GASNETC_QP_PATH_MTU		MTU1024
#define GASNETC_QP_STATIC_RATE		0
#define GASNETC_QP_MIN_RNR_TIMER	IB_RNR_NAK_TIMER_0_08
#define GASNETC_QP_RNR_RETRY		7	/* retry forever, but almost never happens */
#define GASNETC_QP_TIMEOUT		18	/* about 1s */
#define GASNETC_QP_RETRY_COUNT		2

/* ------------------------------------------------------------------------------------ */

/* HCA-level resources */
gasnetc_cep_t	*gasnetc_cep;
VAPI_hca_hndl_t	gasnetc_hca;
VAPI_hca_cap_t	gasnetc_hca_cap;
VAPI_hca_port_t	gasnetc_hca_port;
VAPI_pd_hndl_t	gasnetc_pd;
#if GASNETC_PIN_SEGMENT
  gasnetc_memreg_t	gasnetc_seg_reg;
#endif
#if GASNETC_USE_FIREHOSE
  firehose_info_t	gasnetc_firehose_info;
  #if FIREHOSE_VAPI_USE_FMR
    EVAPI_fmr_t		gasnetc_fmr_props;
  #endif
#endif

/* Used only once, to exchange addresses at connection time */
typedef struct _gasnetc_addr_t {
  IB_lid_t	lid;
  VAPI_qp_num_t	qp_num;
} gasnetc_addr_t;

gasnet_handlerentry_t const *gasnetc_get_handlertable();

gasnet_node_t gasnetc_mynode = (gasnet_node_t)-1;
gasnet_node_t gasnetc_nodes = 0;

uintptr_t gasnetc_MaxLocalSegmentSize = 0;
uintptr_t gasnetc_MaxGlobalSegmentSize = 0;

gasnet_seginfo_t *gasnetc_seginfo = NULL;

char		*gasnetc_hca_id;
IB_port_t	gasnetc_port_num;
int		gasnetc_op_oust_limit;
int		gasnetc_op_oust_pp;
int		gasnetc_am_oust_limit;
int		gasnetc_am_oust_pp;
int		gasnetc_am_spares;
int		gasnetc_bbuf_limit;

/* Maximum pinning capabilities of the HCA */
typedef struct gasnetc_pin_info_t_ {
    uintptr_t	memory;
    uint32_t	regions;
} gasnetc_pin_info_t;
static gasnetc_pin_info_t gasnetc_pin_info;

gasnetc_handler_fn_t const gasnetc_unused_handler = (gasnetc_handler_fn_t)&abort;
gasnetc_handler_fn_t gasnetc_handler[GASNETC_MAX_NUMHANDLERS]; /* handler table */

static void gasnetc_atexit(void);
static void gasnetc_exit_sighandler(int sig);

/* ------------------------------------------------------------------------------------ */
/*
  Initialization
  ==============
*/
/* called at startup to check configuration sanity */
static void gasnetc_check_config() {
  gasneti_check_config_preinit();

  gasneti_assert(sizeof(gasnetc_medmsg_t) == (GASNETC_MEDIUM_HDRSZ + 4*GASNETC_MAX_ARGS));
  gasneti_assert(GASNETC_RCV_POLL || GASNETC_RCV_THREAD);
  gasneti_assert(GASNETC_PUT_COPY_LIMIT <= GASNETC_BUFSZ);
}

static void gasnetc_unpin(gasnetc_memreg_t *reg) {
  VAPI_ret_t vstat;

  vstat = VAPI_deregister_mr(gasnetc_hca, reg->handle);
  GASNETC_VAPI_CHECK(vstat, "from VAPI_deregister_mr()");
}

static VAPI_ret_t gasnetc_pin(void *addr, size_t size, VAPI_mrw_acl_t acl, gasnetc_memreg_t *reg) {
  VAPI_mr_t	mr_in;
  VAPI_mr_t	mr_out;
  VAPI_ret_t	vstat;

  mr_in.type    = VAPI_MR;
  mr_in.start   = (uintptr_t)addr;
  mr_in.size    = size;
  mr_in.pd_hndl = gasnetc_pd;
  mr_in.acl     = acl;

  vstat = VAPI_register_mr(gasnetc_hca, &mr_in, &reg->handle, &mr_out);

  reg->lkey     = mr_out.l_key;
  reg->rkey     = mr_out.r_key;
  reg->addr     = mr_out.start;
  reg->len      = mr_out.size;
  reg->end      = mr_out.start + (mr_out.size - 1);
  reg->req_addr = addr;
  reg->req_size = size;

  return vstat;
}

/* mmap and pin some memory, returning its address, or NULL */
extern void *gasnetc_alloc_pinned(size_t size, VAPI_mrw_acl_t acl, gasnetc_memreg_t *reg) {
  VAPI_ret_t vstat;
  void *addr;

  addr = gasneti_mmap(size);
  if (addr != (void *)-1) {
    vstat = gasnetc_pin(addr, size, acl, reg);
    if (vstat != VAPI_OK) {
      gasneti_munmap(addr, size);
      addr = NULL;
    }
  } else {
    addr = NULL;
  }

  return addr;
}

extern void gasnetc_free_pinned(gasnetc_memreg_t *reg) {
  gasnetc_unpin(reg);
  gasneti_munmap(reg->req_addr, reg->req_size);
}

#if defined(_SC_PHYS_PAGES)
static unsigned long gasnetc_get_physpages()
{
  long pages;

  pages = sysconf(_SC_PHYS_PAGES);
  if (pages == -1) {
    gasneti_fatalerror("sysconf(_SC_PHYS_PAGES) failed");
  }

  return pages;
}
#elif defined(LINUX)
#define _BUFSZ	120
/* XXX how does this fair on systems w/ >4GB */
static unsigned long gasnetc_get_physpages()
{
  FILE            *fp;
  char            line[_BUFSZ+1];
  unsigned long   mem = 0;

  if ((fp = fopen("/proc/meminfo", "r")) == NULL) {
    gasneti_fatalerror("Can't open /proc/meminfo");
  }

  while (fgets(line, _BUFSZ, fp)) {
    if (sscanf(line, "Mem: %lu", &mem) > 0) {
      break;
    }
  }
  fclose(fp);

  return (unsigned long)mem / GASNET_PAGESIZE;
}
#elif defined(__APPLE__) || defined(FREEBSD)
  #include <sys/types.h>
  #include <sys/sysctl.h>
  static unsigned long gasnetc_get_physpages()  { /* see "man 3 sysctl" */
    int mib[2];
    unsigned long mem;
    size_t len = sizeof(mem);

    mib[0] = CTL_HW;
    mib[1] = HW_PHYSMEM;
    if (sysctl(mib, 2, &mem, &len, NULL, 0)) 
      gasneti_fatalerror("sysctl(CTL_HW.HW_PHYSMEM) failed: %s(%i)",strerror(errno),errno);
    return mem / GASNET_PAGESIZE;
  }
#else
#error "Don't know how to get physical memory size on your O/S"
#endif

/* Some stuff not exported from gasnet_mmap.c: */
extern gasnet_seginfo_t gasneti_mmap_segment_search(uintptr_t maxsz);

/* Search for largest region we can allocate and pin */
static uintptr_t gasnetc_get_max_pinnable(void) {
  gasnet_seginfo_t si;
  uintptr_t lo, hi;
  uintptr_t size;
  unsigned long pages;
  void *addr;

  /* search for largest mmap() region
   * We bound our search by the smallest of:
   *   2/3 of physical memory
   *   HCA's capability
   *   User's current (soft) mlock limit
   *   GASNETI_MMAP_MAX_SIZE
   */
  pages = 2 * (gasnetc_get_physpages() / 3);
  pages = MIN(pages, gasnetc_hca_cap.max_mr_size / GASNET_PAGESIZE);
  #ifdef RLIMIT_MEMLOCK
  {
    struct rlimit r;
    if ((getrlimit(RLIMIT_MEMLOCK, &r) == 0) && (r.rlim_cur != RLIM_INFINITY)) {
      pages = MIN(pages, r.rlim_cur / GASNET_PAGESIZE);
    }
  }
  #endif
  si = gasneti_mmap_segment_search(MIN(pages*GASNET_PAGESIZE, GASNETI_MMAP_LIMIT));

  if (si.addr == NULL) return 0;

  /* Now search for largest pinnable region */
  addr = si.addr;
  lo = 0;
  hi = GASNETI_ALIGNDOWN(si.size, GASNETI_MMAP_GRANULARITY);
  #if defined(__APPLE__)
    /* work around bug #532: Pin requests >= 1GB kill Cluster X nodes */
    hi = MIN(hi, 0x40000000 - GASNETI_MMAP_GRANULARITY);
  #endif

#if 0 /* Binary search */
  size = hi;
  do {
    gasnetc_memreg_t reg;
    VAPI_ret_t vstat;

    vstat = gasnetc_pin(addr, size, 0, &reg);
    if (vstat != VAPI_OK) {
      hi = size;
    } else {
      gasnetc_unpin(&reg);
      lo = size;
    }

    size = GASNETI_PAGE_ALIGNDOWN(lo + (hi - lo) / 2);
  } while (size > lo);
#else /* Linear-descending search */
  for (size = hi; size >= lo; size -= GASNETI_MMAP_GRANULARITY) {
    gasnetc_memreg_t reg;
    VAPI_ret_t vstat;

    vstat = gasnetc_pin(addr, size, 0, &reg);
    if (vstat == VAPI_OK) {
      gasnetc_unpin(&reg);
      break;
    }
  }
#endif
  gasneti_munmap(si.addr, si.size);

  return size;
}

/* Process defaults and the environment to get configuration settings */
static int gasnetc_load_settings(void) {
  char	*tmp;

  gasnetc_hca_id = gasneti_strdup(
    gasneti_getenv_withdefault("GASNET_HCA_ID",GASNETC_DEFAULT_HCA_ID));

  gasnetc_port_num = atoi(
    gasneti_getenv_withdefault("GASNET_PORT_NUM", _STRINGIFY(GASNETC_DEFAULT_PORT_NUM)));

  #define GASNETC_ENVINT(program_var, env_key, default_val, minval) do { \
      char _defval[10];                                                  \
      sprintf(_defval,"%i",(default_val));                               \
      program_var = atoi(gasneti_getenv_withdefault(#env_key, _defval)); \
      if (program_var < minval)                                          \
        GASNETI_RETURN_ERRR(BAD_ARG, "("#env_key" < 1) in environment"); \
    } while (0)

  GASNETC_ENVINT(gasnetc_op_oust_limit, GASNET_OP_OUST_LIMIT, GASNETC_DEFAULT_OP_OUST_LIMIT, 1);
  GASNETC_ENVINT(gasnetc_op_oust_pp, GASNET_OP_OUST_PP, GASNETC_DEFAULT_OP_OUST_PP, 1);
  GASNETC_ENVINT(gasnetc_am_oust_limit, GASNET_AM_OUST_LIMIT, GASNETC_DEFAULT_AM_OUST_LIMIT, 1);
  GASNETC_ENVINT(gasnetc_am_oust_pp, GASNET_AM_OUST_PP, GASNETC_DEFAULT_AM_OUST_PP, 1);
  GASNETC_ENVINT(gasnetc_am_spares, GASNET_AM_SPARES, GASNETC_DEFAULT_AM_SPARES, 1);
  GASNETC_ENVINT(gasnetc_bbuf_limit, GASNET_BBUF_LIMIT, GASNETC_DEFAULT_BBUF_LIMIT, 1);

  GASNETI_TRACE_PRINTF(C,("vapi-conduit build time configuration settings = {"));
  GASNETI_TRACE_PRINTF(C,("  AM receives in internal thread %sabled (GASNETC_RCV_THREAD)",
				GASNETC_RCV_THREAD ? "en" : "dis"));
  GASNETI_TRACE_PRINTF(C,("  AM receives in gasnet_AMPoll() %sabled (GASNETC_RCV_POLL)",
			  	GASNETC_RCV_THREAD ? "en" : "dis"));
#if GASNETC_VAPI_FORCE_POLL_LOCK
  GASNETI_TRACE_PRINTF(C,("  Serialized CQ polls            forced (--enable-vapi-force-poll-lock)"));
#else
  GASNETI_TRACE_PRINTF(C,("  Serialized CQ polls            probe for buggy firmware (default)"));
#endif
#if GASNETC_VAPI_ENABLE_INLINE_PUTS
  GASNETI_TRACE_PRINTF(C,("  Use of EVAPI inline sends      enabled (default)"));
  GASNETI_TRACE_PRINTF(C,("    max. size for gasnet puts      %d bytes (GASNETC_PUT_INLINE_LIMIT)",
			  	GASNETC_PUT_INLINE_LIMIT));
  GASNETI_TRACE_PRINTF(C,("    max. size for AMs              %d bytes (GASNETC_AM_INLINE_LIMIT)",
			  	GASNETC_AM_INLINE_LIMIT));
#else
  GASNETI_TRACE_PRINTF(C,("  Use of EVAPI inline sends      disabled (--disable-vapi-inline-puts)"));
  GASNETI_TRACE_PRINTF(C,("    max. size for gasnet puts      N/A (GASNETC_PUT_INLINE_LIMIT)",
			  	GASNETC_PUT_INLINE_LIMIT));
  GASNETI_TRACE_PRINTF(C,("    max. size for AMs              N/A (GASNETC_AM_INLINE_LIMIT)",
			  	GASNETC_AM_INLINE_LIMIT));
#endif
  GASNETI_TRACE_PRINTF(C,("  Max. size for non-bulk copy    %d bytes (GASNETC_PUT_COPY_LIMIT)",
				GASNETC_PUT_COPY_LIMIT));
  GASNETI_TRACE_PRINTF(C,("  Max. snd completions per poll  %d (GASNETC_SND_REAP_LIMIT)",
				GASNETC_SND_REAP_LIMIT));
  GASNETI_TRACE_PRINTF(C,("  Max. rcv completions per poll  %d (GASNETC_RCV_REAP_LIMIT)",
				GASNETC_RCV_REAP_LIMIT));
  GASNETI_TRACE_PRINTF(C,  ("}"));

  GASNETI_TRACE_PRINTF(C,("vapi-conduit run time configuration settings = {"));
  if ((gasnetc_hca_id != NULL) && strlen(gasnetc_hca_id)) {
    GASNETI_TRACE_PRINTF(C,("  GASNET_HCA_ID        = '%s'", gasnetc_hca_id));
  } else {
    GASNETI_TRACE_PRINTF(C,("  GASNET_HCA_ID        unset or empty (will probe)"));
  }
  if (gasnetc_port_num != 0 ) {
    GASNETI_TRACE_PRINTF(C,("  GASNET_PORT_NUM      = %d", gasnetc_port_num));
  } else {
    GASNETI_TRACE_PRINTF(C,("  GASNET_PORT_NUM      unset or zero  (will probe)"));
  }
  GASNETI_TRACE_PRINTF(C,  ("  GASNET_OP_OUST_LIMIT = %d", gasnetc_op_oust_limit));
  GASNETI_TRACE_PRINTF(C,  ("  GASNET_OP_OUST_PP    = %d", gasnetc_op_oust_pp));
  GASNETI_TRACE_PRINTF(C,  ("  GASNET_AM_OUST_LIMIT = %d", gasnetc_am_oust_limit));
  GASNETI_TRACE_PRINTF(C,  ("  GASNET_AM_OUST_PP    = %d", gasnetc_am_oust_pp));
  GASNETI_TRACE_PRINTF(C,  ("  GASNET_AM_SPARES     = %d", gasnetc_am_spares));
  GASNETI_TRACE_PRINTF(C,  ("  GASNET_BBUF_LIMIT    = %d", gasnetc_bbuf_limit));
  GASNETI_TRACE_PRINTF(C,  ("}"));

  return GASNET_OK;
}

static int gasnetc_init(int *argc, char ***argv) {
  VAPI_hca_vendor_t	hca_vendor;
  gasnetc_addr_t	*local_addr;
  gasnetc_addr_t	*remote_addr;
  VAPI_ret_t		vstat;
  int 			i;

  /*  check system sanity */
  gasnetc_check_config();

  if (gasneti_init_done) 
    GASNETI_RETURN_ERRR(NOT_INIT, "GASNet already initialized");
  gasneti_init_done = 1; /* enable early to allow tracing */


  gasneti_freezeForDebugger();

  #if GASNET_DEBUG_VERBOSE
    /* note - can't call trace macros during gasnet_init because trace system not yet initialized */
    fprintf(stderr,"gasnetc_init(): about to spawn...\n"); fflush(stderr);
  #endif

  /* Initialize the bootstrapping support */
  gasnetc_bootstrapInit(argc, argv, &gasnetc_nodes, &gasnetc_mynode);
    
  /* Setup for gasneti_getenv() (must come before gasneti_trace_init() */
  gasneti_setupGlobalEnvironment(gasnetc_nodes, gasnetc_mynode, 
                                 gasnetc_bootstrapAllgather, gasnetc_bootstrapBroadcast);

  /* Now enable tracing of all the following steps */
  gasneti_trace_init(*argc, *argv);

  /* Process the environment for configuration/settings */
  i = gasnetc_load_settings();
  if (i != GASNET_OK) {
    return i;
  }

  /* allocate resources */
  gasnetc_cep = (gasnetc_cep_t *)GASNETI_ALIGNUP(gasneti_malloc(gasnetc_nodes*sizeof(gasnetc_cep_t)
								+ GASNETC_CACHE_LINE_SIZE - 1),
						 GASNETC_CACHE_LINE_SIZE);
  memset(gasnetc_cep, 0, gasnetc_nodes*sizeof(gasnetc_cep_t));
  local_addr = gasneti_calloc(gasnetc_nodes, sizeof(gasnetc_addr_t));
  remote_addr = gasneti_calloc(gasnetc_nodes, sizeof(gasnetc_addr_t));

  /* open the hca and get port & lid values */
  {
    if (!gasnetc_hca_id || !strlen(gasnetc_hca_id)) {
      /* Empty means probe for HCAs */
      VAPI_hca_id_t	*hca_ids;
      u_int32_t		num_hcas = 0;	/* Type specified by Mellanox */

      GASNETI_TRACE_PRINTF(C,("Probing for HCAs"));
      vstat = EVAPI_list_hcas(0, &num_hcas, NULL);
      if (((vstat != VAPI_OK) && (vstat != VAPI_EAGAIN)) || (num_hcas == 0)) {
        /* XXX cleanup */
        GASNETI_RETURN_ERRR(RESOURCE, "failed to locate any HCAs");
      }
      hca_ids = gasneti_calloc(num_hcas, sizeof(VAPI_hca_id_t));
      vstat = EVAPI_list_hcas(num_hcas, &num_hcas, hca_ids);
      GASNETC_VAPI_CHECK(vstat, "while enumerating HCAs");

      for (i = 0; i <= num_hcas; ++i) {
        vstat = VAPI_open_hca(hca_ids[i], &gasnetc_hca);
        if (vstat != VAPI_OK) {
          vstat = EVAPI_get_hca_hndl(hca_ids[i], &gasnetc_hca);
        }
        if (vstat == VAPI_OK) {
          GASNETI_TRACE_PRINTF(C,("Probe located HCA '%s'", hca_ids[i]));
	  break;
	} else {
          GASNETI_TRACE_PRINTF(C,("Probe failed to open HCA '%s'", hca_ids[i]));
	}
      }
      if (i >= num_hcas) {
        /* XXX cleanup */
        GASNETI_RETURN_ERRR(RESOURCE, "failed open any HCAs");
      }

      gasnetc_hca_id = gasneti_strdup(hca_ids[i]);
      gasneti_free(hca_ids);
    } else {
      vstat = VAPI_open_hca(gasnetc_hca_id, &gasnetc_hca);
      if (vstat != VAPI_OK) {
        vstat = EVAPI_get_hca_hndl(gasnetc_hca_id, &gasnetc_hca);
      }
      if (vstat != VAPI_OK) {
        /* XXX cleanup */
        GASNETI_RETURN_ERRR(RESOURCE, "failed open the specified HCA");
      }
    }

    vstat = VAPI_query_hca_cap(gasnetc_hca, &hca_vendor, &gasnetc_hca_cap);
    GASNETC_VAPI_CHECK(vstat, "from VAPI_query_hca_cap()");

    if (gasnetc_port_num == 0) {
      /* Zero means probe for the first active port */
      GASNETI_TRACE_PRINTF(C,("Probing for an active port"));
      for (gasnetc_port_num = 1; gasnetc_port_num <= gasnetc_hca_cap.phys_port_num; ++gasnetc_port_num) {
        (void)VAPI_query_hca_port_prop(gasnetc_hca, gasnetc_port_num, &gasnetc_hca_port);

        if (gasnetc_hca_port.state == PORT_ACTIVE) {
          GASNETI_TRACE_PRINTF(C,("Probe located port %d", gasnetc_port_num));
	  break;
        } else {
	  const char *state;

	  switch (gasnetc_hca_port.state) {
	  case PORT_DOWN:
		  state = "DOWN";
		  break;
	  case PORT_INITIALIZE:
		  state = "INITIALIZE";
		  break;
	  case PORT_ARMED:
		  state = "ARMED";
		  break;
	  default:
		  state = "unknown";
	  }
          GASNETI_TRACE_PRINTF(C,("Probe rejecting port %d (state = %s)", gasnetc_port_num, state));
	}
      }
      if ((gasnetc_port_num > gasnetc_hca_cap.phys_port_num) || (gasnetc_hca_port.state != PORT_ACTIVE)) {
	/* XXX cleanup */
        GASNETI_RETURN_ERRR(RESOURCE, "failed to probe for ACTIVE ports on the HCA");
      }
    } else {
      vstat = VAPI_query_hca_port_prop(gasnetc_hca, gasnetc_port_num, &gasnetc_hca_port);
      if ((vstat != VAPI_OK) || (gasnetc_hca_port.state != PORT_ACTIVE)) {
	/* XXX cleanup */
        GASNETI_RETURN_ERRR(RESOURCE, "failed to open specified port on the HCA");
      }
    }

    #if GASNET_DEBUG_VERBOSE
      fprintf(stderr, "gasnetc_init(): using HCA id='%s' port=%d on node %d/%d\n",
              gasnetc_hca_id, gasnetc_port_num, gasnetc_mynode, gasnetc_nodes);
      fflush(stderr);
    #endif
  }

  /* Report/check hca and port properties */
  GASNETI_TRACE_PRINTF(C,("vapi-conduit HCA/port properties = {"));
  GASNETI_TRACE_PRINTF(C,("  HCA id                   = '%s'", gasnetc_hca_id));
  GASNETI_TRACE_PRINTF(C,("  HCA port number          = %d", gasnetc_port_num));
  GASNETI_TRACE_PRINTF(C,("  max_num_qp               = %u", (unsigned int)gasnetc_hca_cap.max_num_qp));
  gasneti_assert_always(gasnetc_hca_cap.max_num_qp >= gasnetc_nodes);
  GASNETI_TRACE_PRINTF(C,("  max_qp_ous_wr            = %u", (unsigned int)gasnetc_hca_cap.max_qp_ous_wr));
  gasneti_assert_always(gasnetc_hca_cap.max_qp_ous_wr >= gasnetc_op_oust_pp);
  gasneti_assert_always(gasnetc_hca_cap.max_qp_ous_wr >= gasnetc_am_oust_pp * 2);
  GASNETI_TRACE_PRINTF(C,("  max_num_sg_ent           = %u", (unsigned int)gasnetc_hca_cap.max_num_sg_ent));
  gasneti_assert_always(gasnetc_hca_cap.max_num_sg_ent >= GASNETC_SND_SG);
  gasneti_assert_always(gasnetc_hca_cap.max_num_sg_ent >= GASNETC_RCV_SG);
  GASNETI_TRACE_PRINTF(C,("  max_num_sg_ent_rd        = %u", (unsigned int)gasnetc_hca_cap.max_num_sg_ent_rd));
  gasneti_assert_always(gasnetc_hca_cap.max_num_sg_ent_rd >= 1);	/* RDMA Read support required */
  #if 1 /* QP end points */
    GASNETI_TRACE_PRINTF(C,("  max_qp_init_rd_atom      = %u", (unsigned int)gasnetc_hca_cap.max_qp_init_rd_atom));
    gasneti_assert_always(gasnetc_hca_cap.max_qp_init_rd_atom >= 1);	/* RDMA Read support required */
    GASNETI_TRACE_PRINTF(C,("  max_qp_ous_rd_atom       = %u", (unsigned int)gasnetc_hca_cap.max_qp_ous_rd_atom));
    gasneti_assert_always(gasnetc_hca_cap.max_qp_ous_rd_atom >= 1);	/* RDMA Read support required */
  #else
    GASNETI_TRACE_PRINTF(C,("  max_ee_init_rd_atom      = %u", (unsigned int)gasnetc_hca_cap.max_ee_init_rd_atom));
    gasneti_assert_always(gasnetc_hca_cap.max_ee_init_rd_atom >= 1);	/* RDMA Read support required */
    GASNETI_TRACE_PRINTF(C,("  max_ee_ous_rd_atom       = %u", (unsigned int)gasnetc_hca_cap.max_ee_ous_rd_atom));
    gasneti_assert_always(gasnetc_hca_cap.max_ee_ous_rd_atom >= 1);	/* RDMA Read support required */
  #endif
  GASNETI_TRACE_PRINTF(C,("  max_num_cq               = %u", (unsigned int)gasnetc_hca_cap.max_num_cq));
  gasneti_assert_always(gasnetc_hca_cap.max_num_cq >= 2);
  GASNETI_TRACE_PRINTF(C,("  max_num_ent_cq           = %u", (unsigned int)gasnetc_hca_cap.max_num_ent_cq));
  gasneti_assert_always(gasnetc_hca_cap.max_num_ent_cq >= gasnetc_op_oust_limit);
  gasneti_assert_always(gasnetc_hca_cap.max_num_ent_cq >= gasnetc_am_oust_limit * 2); /* request + reply == 2 */

  /* Check for sufficient pinning resources */
  {
    int mr_needed = 2;		/* rcv bufs and snd bufs */
    #if FIREHOSE_USE_FMR
      int fmr_needed = 0;	/* none by default */
    #endif

    #if GASNETC_PIN_SEGMENT
      mr_needed++;		/* +1 for the segment */
    #endif
    #if GASNETC_USE_FIREHOSE
      #if FIREHOSE_USE_FMR
        fmr_needed += FIREHOSE_CLIENT_MAXREGIONS;	/* FMRs needed for firehoses */
      #else
        mr_needed += FIREHOSE_CLIENT_MAXREGIONS;	/* regular MRs needed for firehoses */
      #endif
    #endif

    GASNETI_TRACE_PRINTF(C,("  max_num_mr               = %u", (unsigned int)gasnetc_hca_cap.max_num_mr));
    gasneti_assert_always(gasnetc_hca_cap.max_num_mr >=  mr_needed);
    #if FIREHOSE_USE_FMR
      GASNETI_TRACE_PRINTF(C,("  max_num_fmr              = %u", (unsigned int)gasnetc_hca_cap.max_num_fmr));
      gasneti_assert_always(gasnetc_hca_cap.max_num_fmr >= fmr_needed);
    #endif
  }

  GASNETI_TRACE_PRINTF(C,("  max_msg_sz               = %u", (unsigned int)gasnetc_hca_port.max_msg_sz));
  gasneti_assert_always(gasnetc_hca_port.max_msg_sz >= GASNETC_PUT_COPY_LIMIT);
  GASNETI_TRACE_PRINTF(C,("  HCA Firmware version     = %u.%u.%u",
			    (unsigned int)(hca_vendor.fw_ver >> 32),
			    (unsigned int)(hca_vendor.fw_ver >> 16) & 0xffff,
			    (unsigned int)(hca_vendor.fw_ver) & 0xffff));

  /* For some firmware there is a thread safety bug with VAPI_poll_cq(). */
  #if GASNETC_VAPI_FORCE_POLL_LOCK
    /* The poll lock is used unconditionally */
    GASNETI_TRACE_PRINTF(C,("  Serialized CQ polls      : forced at compile time"));
  #else
    /* Use the poll lock only for known bad fw (<3.0.0): */
    gasnetc_use_poll_lock = (hca_vendor.fw_ver < (uint64_t)(0x300000000LL));
    GASNETI_TRACE_PRINTF(C,("  Serialized CQ polls      : %srequired for this firmware",
			    gasnetc_use_poll_lock ? "" : "not "));
  #endif

  /* For some firmware there is a performance bug with EVAPI_post_inline_sr(). */
  #if GASNETC_VAPI_ENABLE_INLINE_PUTS
  {  /* (1.18 <= fw_ver < 3.0) is known bad */
    int defect = ((hca_vendor.fw_ver >= (uint64_t)(0x100180000LL)) &&
		  (hca_vendor.fw_ver <  (uint64_t)(0x300000000LL)));
    if (defect) {
	fprintf(stderr,
		"WARNING: Your HCA firmware is suspected to include a performance defect\n"
		"when using EVAPI_post_inline_sr().  You may wish to either upgrade your\n"
		"firmware, or configure GASNet with '--disable-vapi-inline-puts'.\n");
    }
  
    GASNETI_TRACE_PRINTF(C,("  Inline perfomance defect : %ssuspected in this firmware",
			    defect ? "" : "not "));
  }
  #endif

  GASNETI_TRACE_PRINTF(C,("}")); /* end of HCA report */

  /* get a pd for the QPs and memory registration */
  vstat =  VAPI_alloc_pd(gasnetc_hca, &gasnetc_pd);
  GASNETC_VAPI_CHECK(vstat, "from VAPI_alloc_pd()");

  /* allocate/initialize transport resources */
  gasnetc_sndrcv_init();

  /* create all the endpoints */
  {
    VAPI_qp_init_attr_t	qp_init_attr;
    VAPI_qp_prop_t	qp_prop;

    qp_init_attr.cap.max_oust_wr_rq = gasnetc_am_oust_pp * 2;
    qp_init_attr.cap.max_oust_wr_sq = gasnetc_op_oust_pp;
    qp_init_attr.cap.max_sg_size_rq = GASNETC_RCV_SG;
    qp_init_attr.cap.max_sg_size_sq = GASNETC_SND_SG;
    qp_init_attr.pd_hndl            = gasnetc_pd;
    qp_init_attr.rdd_hndl           = 0;
    qp_init_attr.rq_cq_hndl         = gasnetc_rcv_cq;
    qp_init_attr.rq_sig_type        = VAPI_SIGNAL_REQ_WR;
    qp_init_attr.sq_cq_hndl         = gasnetc_snd_cq;
    qp_init_attr.sq_sig_type        = VAPI_SIGNAL_REQ_WR;
    qp_init_attr.ts_type            = VAPI_TS_RC;

    for (i = 0; i < gasnetc_nodes; ++i) {
      if (i == gasnetc_mynode) continue;

      /* create the QP */
      vstat = VAPI_create_qp(gasnetc_hca, &qp_init_attr, &gasnetc_cep[i].qp_handle, &qp_prop);
      GASNETC_VAPI_CHECK(vstat, "from VAPI_create_qp()");
      gasneti_assert(qp_prop.cap.max_oust_wr_rq >= gasnetc_am_oust_pp * 2);
      gasneti_assert(qp_prop.cap.max_oust_wr_sq >= gasnetc_op_oust_pp);

      local_addr[i].lid = gasnetc_hca_port.lid;
      local_addr[i].qp_num = qp_prop.qp_num;
    }
  }

  /* exchange endpoint info for connecting */
  gasnetc_bootstrapAlltoall(local_addr, sizeof(gasnetc_addr_t), remote_addr);

  /* connect the endpoints */
  {
    VAPI_qp_attr_t	qp_attr;
    VAPI_qp_attr_mask_t	qp_mask;
    VAPI_qp_cap_t	qp_cap;

    /* advance RST -> INIT */
    QP_ATTR_MASK_CLR_ALL(qp_mask);
    QP_ATTR_MASK_SET(qp_mask, QP_ATTR_QP_STATE);
    QP_ATTR_MASK_SET(qp_mask, QP_ATTR_PKEY_IX);
    QP_ATTR_MASK_SET(qp_mask, QP_ATTR_PORT);
    QP_ATTR_MASK_SET(qp_mask, QP_ATTR_REMOTE_ATOMIC_FLAGS);
    qp_attr.qp_state            = VAPI_INIT;
    qp_attr.pkey_ix             = 0;
    qp_attr.port                = gasnetc_port_num;
    qp_attr.remote_atomic_flags = VAPI_EN_REM_WRITE | VAPI_EN_REM_READ;
    for (i = 0; i < gasnetc_nodes; ++i) {
      if (i == gasnetc_mynode) continue;
      
      vstat = VAPI_modify_qp(gasnetc_hca, gasnetc_cep[i].qp_handle, &qp_attr, &qp_mask, &qp_cap);
      GASNETC_VAPI_CHECK(vstat, "from VAPI_modify_qp(INIT)");
    }

    /* post recv buffers and other local initialization */
    for (i = 0; i < gasnetc_nodes; ++i) {
      gasnetc_sndrcv_init_cep(&gasnetc_cep[i]);
    }

    /* advance INIT -> RTR */
    QP_ATTR_MASK_CLR_ALL(qp_mask);
    QP_ATTR_MASK_SET(qp_mask, QP_ATTR_QP_STATE);
    QP_ATTR_MASK_SET(qp_mask, QP_ATTR_AV);
    QP_ATTR_MASK_SET(qp_mask, QP_ATTR_PATH_MTU);
    QP_ATTR_MASK_SET(qp_mask, QP_ATTR_RQ_PSN);
    QP_ATTR_MASK_SET(qp_mask, QP_ATTR_QP_OUS_RD_ATOM);
    QP_ATTR_MASK_SET(qp_mask, QP_ATTR_DEST_QP_NUM);
    QP_ATTR_MASK_SET(qp_mask, QP_ATTR_MIN_RNR_TIMER);
    qp_attr.qp_state         = VAPI_RTR;
    qp_attr.av.sl            = 0;
    qp_attr.av.grh_flag      = FALSE;
    qp_attr.av.static_rate   = GASNETC_QP_STATIC_RATE;
    qp_attr.av.src_path_bits = 0;
    qp_attr.path_mtu         = MIN(GASNETC_QP_PATH_MTU, gasnetc_hca_port.max_mtu);
    qp_attr.qp_ous_rd_atom   = MIN(gasnetc_hca_cap.max_qp_init_rd_atom, gasnetc_hca_cap.max_qp_ous_rd_atom);
    qp_attr.min_rnr_timer    = GASNETC_QP_MIN_RNR_TIMER;
    for (i = 0; i < gasnetc_nodes; ++i) {
      if (i == gasnetc_mynode) continue;

      qp_attr.rq_psn         = i;
      qp_attr.av.dlid        = remote_addr[i].lid;
      qp_attr.dest_qp_num    = remote_addr[i].qp_num;
      vstat = VAPI_modify_qp(gasnetc_hca, gasnetc_cep[i].qp_handle, &qp_attr, &qp_mask, &qp_cap);
      GASNETC_VAPI_CHECK(vstat, "from VAPI_modify_qp(RTR)");
    }

    /* QPs must reach RTR before their peer can advance to RTS */
    gasnetc_bootstrapBarrier();

    /* advance RTR -> RTS */
    QP_ATTR_MASK_CLR_ALL(qp_mask);
    QP_ATTR_MASK_SET(qp_mask, QP_ATTR_QP_STATE);
    QP_ATTR_MASK_SET(qp_mask, QP_ATTR_SQ_PSN);
    QP_ATTR_MASK_SET(qp_mask, QP_ATTR_TIMEOUT);
    QP_ATTR_MASK_SET(qp_mask, QP_ATTR_RETRY_COUNT);
    QP_ATTR_MASK_SET(qp_mask, QP_ATTR_RNR_RETRY);
    QP_ATTR_MASK_SET(qp_mask, QP_ATTR_OUS_DST_RD_ATOM);
    qp_attr.qp_state         = VAPI_RTS;
    qp_attr.sq_psn           = gasnetc_mynode;
    qp_attr.timeout          = GASNETC_QP_TIMEOUT;
    qp_attr.retry_count      = GASNETC_QP_RETRY_COUNT;
    qp_attr.rnr_retry        = GASNETC_QP_RNR_RETRY;
    qp_attr.ous_dst_rd_atom  = MIN(gasnetc_hca_cap.max_qp_init_rd_atom, gasnetc_hca_cap.max_qp_ous_rd_atom);
    for (i = 0; i < gasnetc_nodes; ++i) {
      if (i == gasnetc_mynode) continue;

      vstat = VAPI_modify_qp(gasnetc_hca, gasnetc_cep[i].qp_handle, &qp_attr, &qp_mask, &qp_cap);
      GASNETC_VAPI_CHECK(vstat, "from VAPI_modify_qp(RTS)");
      gasneti_assert(qp_cap.max_inline_data_sq >= GASNETC_PUT_INLINE_LIMIT);
    }
  }

  #if GASNET_DEBUG_VERBOSE
    fprintf(stderr,"gasnetc_init(): spawn successful - node %i/%i starting...\n", 
      gasnetc_mynode, gasnetc_nodes); fflush(stderr);
  #endif

  /* Find max pinnable size before we start carving up memory w/ mmap()s.
   *
   * Take care that only one process per LID (node) performs the probe.
   * The result is then divided by the number of processes on the adapter,
   * which is easily determined from the connection information we exchanged above.
   *
   * XXX: The current solution is suboptimal because if mmap() was the limiting factor
   * (rather than physical memory or HCA resources) then the result is too small.
   * Doing better will require iteration and internode coordination to search the
   * interval (max_pinnable/num_local)...max_pinnable.
   */
  {
    gasnet_node_t	num_local;
    gasnet_node_t	first_local;
    gasnetc_pin_info_t	*all_info;

    /* Determine the number of local processes and distinguish one */
    num_local = 1;
    first_local = gasnetc_mynode;
    for (i = 0; i < gasnetc_nodes; ++i) {
      if (remote_addr[i].lid == gasnetc_hca_port.lid) {
        ++num_local;
        first_local = MIN(i, first_local);
      }
    }
    gasneti_assert(num_local != 0);
    gasneti_assert(first_local != gasnetc_nodes);

    /* Query the pinning limits of the HCA */
    if (first_local == gasnetc_mynode) {
      gasnetc_pin_info.memory  = GASNETI_ALIGNDOWN(gasnetc_get_max_pinnable() / num_local, GASNET_PAGESIZE);
    } else {
      gasnetc_pin_info.memory  = (uintptr_t)(-1);
    }
    gasnetc_pin_info.regions = gasnetc_hca_cap.max_num_mr;

    /* Find the local min-of-maxes over the pinning limits */
    all_info = gasneti_malloc(gasnetc_nodes * sizeof(gasnetc_pin_info_t));
    gasnetc_bootstrapAllgather(&gasnetc_pin_info, sizeof(gasnetc_pin_info_t), all_info);
    for (i = 0; i < gasnetc_nodes; i++) {
      gasnetc_pin_info.memory  = MIN(gasnetc_pin_info.memory,  all_info[i].memory );
      gasnetc_pin_info.regions = MIN(gasnetc_pin_info.regions, all_info[i].regions);
    }
    gasneti_free(all_info);
    gasneti_assert(gasnetc_pin_info.memory != 0);
    gasneti_assert(gasnetc_pin_info.memory != (uintptr_t)(-1));
    gasneti_assert(gasnetc_pin_info.regions != 0);
  }

  gasneti_free(remote_addr);
  gasneti_free(local_addr);

  #if GASNET_SEGMENT_FAST
  {
    /* XXX: This call replicates the mmap search and min-of-max done above */
    gasneti_segmentInit(&gasnetc_MaxLocalSegmentSize,
                        &gasnetc_MaxGlobalSegmentSize,
                        gasnetc_pin_info.memory,
                        gasnetc_nodes,
                        &gasnetc_bootstrapAllgather);
  }
  #elif GASNET_SEGMENT_LARGE
  {
    /* XXX: This call replicates the mmap search done above */
    gasneti_segmentInit(&gasnetc_MaxLocalSegmentSize,
                        &gasnetc_MaxGlobalSegmentSize,
                        (uintptr_t)(-1),
                        gasnetc_nodes,
                        &gasnetc_bootstrapAllgather);
  }
  #elif GASNET_SEGMENT_EVERYTHING
  {
    gasnetc_MaxLocalSegmentSize =  (uintptr_t)-1;
    gasnetc_MaxGlobalSegmentSize = (uintptr_t)-1;
  }
  #endif

  atexit(gasnetc_atexit);

  #if 0
    /* Done earlier to allow tracing */
    gasneti_init_done = 1;  
  #endif
  gasnetc_bootstrapBarrier();

  return GASNET_OK;
}

/* ------------------------------------------------------------------------------------ */
extern int gasnet_init(int *argc, char ***argv) {
  int retval = gasnetc_init(argc, argv);
  if (retval != GASNET_OK) GASNETI_RETURN(retval);
  #if 0
    /* Already done in gasnetc_init() to allow tracing of init steps */
    gasneti_trace_init(*argc, *argv);
  #endif
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
  int numreg = 0;
  
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
  #elif GASNET_SEGMENT_EVERYTHING
    segsize = 0;
    minheapoffset = 0;
  #endif

  /* ------------------------------------------------------------------------------------ */
  /*  register handlers */
  { /*  core API handlers */
    gasnet_handlerentry_t *ctable = (gasnet_handlerentry_t *)gasnetc_get_handlertable();
    int len = 0;
    gasneti_assert(ctable);
    while (ctable[len].fnptr) len++; /* calc len */
    if (gasnetc_reghandlers(ctable, len, 1, 63, 0, &numreg) != GASNET_OK)
      GASNETI_RETURN_ERRR(RESOURCE,"Error registering core API handlers");
    gasneti_assert(numreg == len);
  }

  { /*  extended API handlers */
    gasnet_handlerentry_t *etable = (gasnet_handlerentry_t *)gasnete_get_handlertable();
    int len = 0;
    gasneti_assert(etable);
    while (etable[len].fnptr) len++; /* calc len */
    if (gasnetc_reghandlers(etable, len, 64, 127, 0, &numreg) != GASNET_OK)
      GASNETI_RETURN_ERRR(RESOURCE,"Error registering extended API handlers");
    gasneti_assert(numreg == len);
  }

  #if GASNETC_USE_FIREHOSE
  { /* firehose handlers */
    gasnet_handlerentry_t *ftable = (gasnet_handlerentry_t *)firehose_get_handlertable();
    int len = 0;
    int base = 64 + numreg;	/* start right after etable */
    gasneti_assert(ftable);
    while (ftable[len].fnptr) len++; /* calc len */
    gasneti_assert(base + len <= 128);	/* enough space remaining after etable? */
    if (gasnetc_reghandlers(ftable, len, base, 127, 1, &numreg) != GASNET_OK)
      GASNETI_RETURN_ERRR(RESOURCE, "Error registering firehose handlers");
    gasneti_assert(numreg == len);
  }
  #endif

  if (table) { /*  client handlers */
    int numreg1 = 0;
    int numreg2 = 0;

    /*  first pass - assign all fixed-index handlers */
    if (gasnetc_reghandlers(table, numentries, 128, 255, 0, &numreg1) != GASNET_OK)
      GASNETI_RETURN_ERRR(RESOURCE,"Error registering fixed-index client handlers");

    /*  second pass - fill in dontcare-index handlers */
    if (gasnetc_reghandlers(table, numentries, 128, 255, 1, &numreg2) != GASNET_OK)
      GASNETI_RETURN_ERRR(RESOURCE,"Error registering variable-index client handlers");

    gasneti_assert(numreg1 + numreg2 == numentries);
  }

  /* ------------------------------------------------------------------------------------ */
  /*  register fatal signal handlers */

  /* catch fatal signals and convert to SIGQUIT */
  gasneti_registerSignalHandlers(gasneti_defaultSignalHandler);

  /*  (###) register any custom signal handlers required by your conduit 
   *        (e.g. to support interrupt-based messaging)
   */

  /* ------------------------------------------------------------------------------------ */
  /*  register segment  */

  gasnetc_seginfo = (gasnet_seginfo_t *)gasneti_malloc(gasnetc_nodes*sizeof(gasnet_seginfo_t));

  #if GASNET_SEGMENT_EVERYTHING
  {
    int i;
    for (i=0;i<gasnetc_nodes;i++) {
      gasnetc_seginfo[i].addr = (void *)0;
      gasnetc_seginfo[i].size = (uintptr_t)-1;
    }
    segbase = (void *)0;
    segsize = (uintptr_t)-1;
  }
  #elif GASNETC_PIN_SEGMENT
  {
    /* allocate the segment and exchange seginfo */
    gasneti_segmentAttach(segsize, minheapoffset, gasnetc_seginfo, &gasnetc_bootstrapAllgather);
    segbase = gasnetc_seginfo[gasnetc_mynode].addr;
    segsize = gasnetc_seginfo[gasnetc_mynode].size;

    /* pin the segment and exchange the RKeys */
    { VAPI_rkey_t	*rkeys;
      VAPI_ret_t	vstat;
      int		i;

      vstat = gasnetc_pin(segbase, segsize,
			  VAPI_EN_LOCAL_WRITE | VAPI_EN_REMOTE_WRITE | VAPI_EN_REMOTE_READ,
			  &gasnetc_seg_reg);
      GASNETC_VAPI_CHECK(vstat, "from VAPI_register_mr(segment)");

      rkeys = gasneti_calloc(gasnetc_nodes,sizeof(VAPI_rkey_t));
      gasneti_assert(rkeys != NULL);
      gasnetc_bootstrapAllgather(&gasnetc_seg_reg.rkey, sizeof(VAPI_rkey_t), rkeys);
      for (i=0;i<gasnetc_nodes;i++) {
        gasnetc_cep[i].rkey = rkeys[i];
      }
      gasneti_free(rkeys);
    }
  }
  #else	/* just allocate the segment but don't pin it */
  {
    /* allocate the segment and exchange seginfo */
    gasneti_segmentAttach(segsize, minheapoffset, gasnetc_seginfo, &gasnetc_bootstrapAllgather);
    segbase = gasnetc_seginfo[gasnetc_mynode].addr;
    segsize = gasnetc_seginfo[gasnetc_mynode].size;
  }
  #endif

  #if GASNETC_USE_FIREHOSE
  {
    int i, reg_count;
    firehose_region_t prereg[3];

    /* Setup prepinned regions list */
    prereg[0].addr          = gasnetc_snd_reg.addr;
    prereg[0].len           = gasnetc_snd_reg.len;
    prereg[0].client.handle = VAPI_INVAL_HNDL;	/* unreg must fail */
    prereg[0].client.lkey   = gasnetc_snd_reg.lkey;
    prereg[0].client.rkey   = gasnetc_snd_reg.rkey;
    reg_count = 1;
    if (gasnetc_nodes > 1) {
	prereg[reg_count].addr          = gasnetc_rcv_reg.addr;
	prereg[reg_count].len           = gasnetc_rcv_reg.len;
	prereg[reg_count].client.handle = VAPI_INVAL_HNDL;	/* unreg must fail */
	prereg[reg_count].client.lkey   = gasnetc_rcv_reg.lkey;
	prereg[reg_count].client.rkey   = gasnetc_rcv_reg.rkey;
	reg_count++;
    }
    #if GASNETC_PIN_SEGMENT
	prereg[reg_count].addr          = gasnetc_seg_reg.addr;
	prereg[reg_count].len           = gasnetc_seg_reg.len;
	prereg[reg_count].client.handle = VAPI_INVAL_HNDL;	/* unreg must fail */
	prereg[reg_count].client.lkey   = gasnetc_seg_reg.lkey;
	prereg[reg_count].client.rkey   = gasnetc_seg_reg.rkey;
	reg_count++;
    #endif

    #if FIREHOSE_VAPI_USE_FMR
    {
      /* Prepare FMR properties */
      gasnetc_fmr_props.pd_hndl = gasnetc_pd;
      gasnetc_fmr_props.acl = VAPI_EN_LOCAL_WRITE | VAPI_EN_REMOTE_WRITE | VAPI_EN_REMOTE_READ;
      gasnetc_fmr_props.log2_page_sz = GASNETI_PAGESHIFT;
      gasnetc_fmr_props.max_outstanding_maps = 1;
      gasnetc_fmr_props.max_pages = FIREHOSE_CLIENT_MAXREGION_SIZE / GASNETI_PAGESIZE;
    }
    #endif

    /* Now initialize firehose */
    firehose_init(gasnetc_pin_info.memory, gasnetc_pin_info.regions,
		  prereg, reg_count,
		  &gasnetc_firehose_info);
    gasnetc_fh_maxsz = MIN(gasnetc_hca_port.max_msg_sz,
			  MIN(gasnetc_firehose_info.max_LocalPinSize,
			      gasnetc_firehose_info.max_RemotePinSize));
    gasneti_assert(gasnetc_fh_maxsz >= (GASNETI_PAGESIZE + GASNETC_PUT_INLINE_LIMIT));

    /* Ensure the permanently pinned regions stay in the firehose table */
    for (i = 0; i < reg_count; ++i) {
	firehose_request_t r;
	const firehose_request_t *p;
	p = firehose_try_local_pin(prereg[i].addr, prereg[i].len, &r);
	gasneti_assert(p == &r);
	gasneti_assert(p->addr          == prereg[i].addr         );
	gasneti_assert(p->len           == prereg[i].len          );
	gasneti_assert(p->client.handle == prereg[i].client.handle);
	gasneti_assert(p->client.lkey   == prereg[i].client.lkey  );
	gasneti_assert(p->client.rkey   == prereg[i].client.rkey  );
    }
  }
  #endif

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
/*
  Exit handling code
*/

#ifndef GASNETI_HAVE_ATOMIC_CAS
  #error "required atomic compare-and-swap is not yet implemented for your CPU/OS/compiler"
#endif

gasneti_atomic_t gasnetc_exit_running = gasneti_atomic_init(0);		/* boolean used by GASNETC_IS_EXITING */

static gasneti_atomic_t gasnetc_exit_code = gasneti_atomic_init(0);	/* value to _exit() with */
static gasneti_atomic_t gasnetc_exit_reqs = gasneti_atomic_init(0);	/* count of remote exit requests */
static gasneti_atomic_t gasnetc_exit_reps = gasneti_atomic_init(0);	/* count of remote exit replies */
static gasneti_atomic_t gasnetc_exit_done = gasneti_atomic_init(0);	/* flag to show exit coordination done */
static gasnetc_counter_t gasnetc_exit_repl_oust = GASNETC_COUNTER_INITIALIZER; /* track send of our AM reply */

#define GASNETC_ROOT_NODE 0

enum {
  GASNETC_EXIT_ROLE_UNKNOWN,
  GASNETC_EXIT_ROLE_MASTER,
  GASNETC_EXIT_ROLE_SLAVE
};

static gasneti_atomic_t gasnetc_exit_role = gasneti_atomic_init(GASNETC_EXIT_ROLE_UNKNOWN);

/*
 * Code to disable user's AM handlers when exiting.  We need this because we must call
 * AMPoll to run system-level handlers, including ACKs for flow control.
 *
 * We do it this way because it adds absolutely nothing the normal execution path.
 * Thanks to Dan for the suggestion.
 */
static void gasnetc_noop(void) { return; }
static void gasnetc_disable_AMs(void) {
  int i;

  for (i = 0; i < GASNETC_MAX_NUMHANDLERS; ++i) {
    gasnetc_handler[i] = (gasnetc_handler_fn_t)&gasnetc_noop;
  }
}

/*
 * gasnetc_exit_role_reqh()
 *
 * This request handler (invoked only on the "root" node) handles the election
 * of a single exit "master", who will coordinate an orderly shutdown.
 */
static void gasnetc_exit_role_reqh(gasnet_token_t token, gasnet_handlerarg_t *args, int numargs) {
  gasnet_node_t src;
  int local_role, result;
  int rc;

  gasneti_assert(numargs == 0);
  gasneti_assert(gasnetc_mynode == GASNETC_ROOT_NODE);	/* May only send this request to the root node */

  
  /* What role would the local node get if the requester is made the master? */
  rc = gasnet_AMGetMsgSource(token, &src);
  gasneti_assert(rc == GASNET_OK);
  local_role = (src == GASNETC_ROOT_NODE) ? GASNETC_EXIT_ROLE_MASTER : GASNETC_EXIT_ROLE_SLAVE;

  /* Try atomically to assume the proper role.  Result determines role of requester */
  result = gasneti_atomic_compare_and_swap(&gasnetc_exit_role, GASNETC_EXIT_ROLE_UNKNOWN, local_role)
                ? GASNETC_EXIT_ROLE_MASTER : GASNETC_EXIT_ROLE_SLAVE;

  /* Inform the requester of the outcome. */
  rc = gasnetc_ReplySystem(token, NULL, gasneti_handleridx(gasnetc_SYS_exit_role_rep),
			   1, (gasnet_handlerarg_t)result);
  gasneti_assert(rc == GASNET_OK);
}

/*
 * gasnetc_exit_role_reph()
 *
 * This reply handler receives the result of the election of an exit "master".
 * The reply contains the exit "role" this node should assume.
 */
static void gasnetc_exit_role_reph(gasnet_token_t token, gasnet_handlerarg_t *args, int numargs) {
  int role;

  #if GASNET_DEBUG
  {
    gasnet_node_t src;
    int rc;

    rc = gasnet_AMGetMsgSource(token, &src);
    gasneti_assert(rc == GASNET_OK);
    gasneti_assert(src == GASNETC_ROOT_NODE);	/* May only receive this reply from the root node */
  }
  #endif

  /* What role has this node been assigned? */
  gasneti_assert(args != NULL);
  gasneti_assert(numargs == 1);
  role = (int)args[0];
  gasneti_assert((role == GASNETC_EXIT_ROLE_MASTER) || (role == GASNETC_EXIT_ROLE_SLAVE));

  /* Set the role if not yet set.  Then assert that the assigned role has been assumed.
   * This way the assertion is checking that if the role was obtained by other means
   * (namely by receiving an exit request) it must match the election result. */
  gasneti_atomic_compare_and_swap(&gasnetc_exit_role, GASNETC_EXIT_ROLE_UNKNOWN, role);
  gasneti_assert(gasneti_atomic_read(&gasnetc_exit_role) == role);
}

/*
 * gasnetc_get_exit_role()
 *
 * This function returns the exit role immediately if known.  Otherwise it sends an AMRequest
 * to determine its role and then polls the network until the exit role is determined, either
 * by the reply to that request, or by a remote exit request.
 *
 * Should be called with an alarm timer in-force in case we get hung sending or the root node
 * is not responsive.
 *
 * Note that if we get here as a result of a remote exit request then our role has already been
 * set to "slave" and we won't touch the network from inside the request handler.
 */
static int gasnetc_get_exit_role()
{
  int role;

  role = gasneti_atomic_read(&gasnetc_exit_role);
  if (role == GASNETC_EXIT_ROLE_UNKNOWN) {
    int rc;

    /* Don't know our role yet.  So, send a system-category AM Request to determine our role */
    rc = gasnetc_RequestSystem(GASNETC_ROOT_NODE, NULL,
		    	       gasneti_handleridx(gasnetc_SYS_exit_role_req), 0);
    gasneti_assert(rc == GASNET_OK);

    /* Now spin until somebody tells us what our role is */
    do {
      gasnetc_sndrcv_poll(); /* works even before _attach */
      role = gasneti_atomic_read(&gasnetc_exit_role);
    } while (role == GASNETC_EXIT_ROLE_UNKNOWN);
  }

  return role;
}

/* gasnetc_exit_head
 *
 * All exit paths pass through here as the first step.
 * This function ensures that gasnetc_exit_code is written only once
 * by the first call.
 * It also lets the handler for remote exit requests know if a local
 * request has already begun.
 *
 * returns non-zero on the first call only
 * returns zero on all subsequent calls
 */
static int gasnetc_exit_head(int exitcode) {
  static gasneti_atomic_t once = gasneti_atomic_init(1);
  int retval;

  gasneti_atomic_set(&gasnetc_exit_running, 1);

  retval = gasneti_atomic_decrement_and_test(&once);

  if (retval) {
    /* Store the exit code for later use */
    gasneti_atomic_set(&gasnetc_exit_code, exitcode);
  }

  return retval;
}

/* gasnetc_exit_now
 *
 * First we set the atomic variable gasnetc_exit_done to allow the exit
 * of any threads which are spinning on it in gasnetc_exit().
 * Then this function tries hard to actually terminate the calling thread.
 * If for some unlikely reason the _exit() call returns, we abort().
 *
 * DOES NOT RETURN
 */
static void gasnetc_exit_now(int) GASNET_NORETURN;
static void gasnetc_exit_now(int exitcode) {
  /* If anybody is still waiting, let them go */
  gasneti_atomic_set(&gasnetc_exit_done, 1);

  #if GASNET_DEBUG_VERBOSE
    fprintf(stderr,"gasnetc_exit(): node %i/%i calling killmyprocess...\n", 
      gasnetc_mynode, gasnetc_nodes); fflush(stderr);
  #endif
  gasneti_killmyprocess(exitcode);
  /* NOT REACHED */

  gasneti_reghandler(SIGABRT, SIG_DFL);
  abort();
  /* NOT REACHED */
}

/* gasnetc_exit_tail
 *
 * This the final exit code for the cases of local or remote requested exits.
 * It is not used for the return-from-main case.  Nor is this code used if a fatal
 * signal (including SIGALRM on timeout) is encountered while trying to shutdown.
 *
 * Just a wrapper around gasnetc_exit_now() to actually terminate.
 *
 * DOES NOT RETURN
 */
static void gasnetc_exit_tail(void) GASNET_NORETURN;
static void gasnetc_exit_tail(void) {
  gasnetc_exit_now((int)gasneti_atomic_read(&gasnetc_exit_code));
  /* NOT REACHED */
}

/* gasnetc_exit_sighandler
 *
 * This signal handler is for a last-ditch exit when a signal arrives while
 * attempting the graceful exit.  That includes SIGALRM if we get wedged.
 *
 * Just a signal-handler wrapper for gasnetc_exit_now().
 *
 * DOES NOT RETURN
 */
static void gasnetc_exit_sighandler(int sig) {
  #if GASNET_DEBUG
  /* note - can't call trace macros here, or even sprintf */
  {
    static const char msg1[] = "gasnet_exit(): signal ";
    static const char msg2[] = " received during exit... goodbye\n";
    char digit;

    write(STDERR_FILENO, msg1, sizeof(msg1) - 1);

    /* assume sig < 100 */
    if (sig > 9) {
      digit = '0' + ((sig / 10) % 10);
      write(STDERR_FILENO, &digit, 1);
    }
    digit = '0' + (sig % 10);
    write(STDERR_FILENO, &digit, 1);
    
    write(STDERR_FILENO, msg2, sizeof(msg2) - 1);
  }
  #endif

  gasnetc_exit_now((int)gasneti_atomic_read(&gasnetc_exit_code));
  /* NOT REACHED */
}

/* gasnetc_exit_master
 *
 * We say a polite goodbye to our peers and then listen for their replies.
 * This forms the root nodes portion of a barrier for graceful shutdown.
 *
 * The "goodbyes" are just a system-category AM containing the desired exit code.
 * The AM helps ensure that on non-collective exits the "other" nodes know to exit.
 * If we see a "goodbye" from all of our peers we know we've managed to coordinate
 * an orderly shutdown.  If not, then in gasnetc_exit_body() we can ask the bootstrap
 * support to kill the job in a less graceful way.
 *
 * Takes the exitcode and a timeout in us as arguments
 *
 * Returns 0 on success, non-zero on any sort of failure including timeout.
 */
static int gasnetc_exit_master(int exitcode, int64_t timeout_us) {
  int i, rc;
  int64_t start_time;

  gasneti_assert(timeout_us > 0); 

  start_time = gasneti_getMicrosecondTimeStamp();

  /* Notify phase */
  for (i = 0; i < gasnetc_nodes; ++i) {
    if (i == gasnetc_mynode) continue;

    if ((gasneti_getMicrosecondTimeStamp() - start_time) > timeout_us) return -1;

    rc = gasnetc_RequestSystem(i, NULL,
		    	       gasneti_handleridx(gasnetc_SYS_exit_req),
			       1, (gasnet_handlerarg_t)exitcode);
    if (rc != GASNET_OK) return -1;
  }

  /* Wait phase - wait for replies from our N-1 peers */
  while (gasneti_atomic_read(&gasnetc_exit_reps) < (gasnetc_nodes - 1)) {
    if ((gasneti_getMicrosecondTimeStamp() - start_time) > timeout_us) return -1;

    gasnetc_sndrcv_poll(); /* works even before _attach */
  }

  return 0;
}

/* gasnetc_exit_slave
 *
 * We wait for a polite goodbye from the exit master.
 *
 * Takes a timeout in us as arguments
 *
 * Returns 0 on success, non-zero on timeout.
 */
static int gasnetc_exit_slave(int64_t timeout_us) {
  int64_t start_time;

  gasneti_assert(timeout_us > 0); 

  start_time = gasneti_getMicrosecondTimeStamp();

  /* wait until the exit request is received from the master */
  while (gasneti_atomic_read(&gasnetc_exit_reqs) == 0) {
    if ((gasneti_getMicrosecondTimeStamp() - start_time) > timeout_us) return -1;

    gasnetc_sndrcv_poll(); /* works even before _attach */
  }

  /* wait until out reply has been placed on the wire */
  gasnetc_counter_wait(&gasnetc_exit_repl_oust, 1);

  return 0;
}

/* gasnetc_exit_body
 *
 * This code is common to all the exit paths and is used to perform a hopefully graceful exit in
 * all cases.  To coordinate a graceful shutdown gasnetc_get_exit_role() will select one node as
 * the "master".  That master node will then send a remote exit request to each of its peers to
 * ensure they know that it is time to exit.  If we fail to coordinate the shutdown, we ask the
 * bootstrap to shut us down agressively.  Otherwise we return to our caller.  Unless our caller
 * is the at-exit handler, we are typically followed by a call to gasnetc_exit_tail() to perform
 * the actual termination.  Note also that this function will block all calling threads other than
 * the first until the shutdown code has been completed.
 *
 * XXX: timouts contained here are entirely arbitrary
 */
static void gasnetc_exit_body(void) {
  int i, role, exitcode;
  int graceful = 0;
  int64_t timeout_us;

  /* once we start a shutdown, ignore all future SIGQUIT signals or we risk reentrancy */
  (void)gasneti_reghandler(SIGQUIT, SIG_IGN);

  /* Ensure only one thread ever continues past this point.
   * Others will spin here until time to die.
   * We can't/shouldn't use mutex code here since it is not signal-safe.
   */
  #ifdef GASNETI_USE_GENERIC_ATOMICOPS
    #error "We need real atomic ops with signal-safety for gasnet_exit..."
  #endif
  {
    static gasneti_atomic_t exit_lock = gasneti_atomic_init(1);
    if (!gasneti_atomic_decrement_and_test(&exit_lock)) {
      /* poll until it is time to exit */
      while (!gasneti_atomic_read(&gasnetc_exit_done)) {
	sleep(1);
      }
      gasnetc_exit_tail();
      /* NOT REACHED */
    }
  }

  /* read exit code, stored by first caller to gasnetc_exit_head() */
  exitcode = gasneti_atomic_read(&gasnetc_exit_code);

  /* Establish a last-ditch signal handler in case of failure. */
  alarm(0);
  gasneti_reghandler(SIGALRM, gasnetc_exit_sighandler);
  #if GASNET_DEBUG
    gasneti_reghandler(SIGABRT, SIG_DFL);
  #else
    gasneti_reghandler(SIGABRT, gasnetc_exit_sighandler);
  #endif
  gasneti_reghandler(SIGILL,  gasnetc_exit_sighandler);
  gasneti_reghandler(SIGSEGV, gasnetc_exit_sighandler);
  gasneti_reghandler(SIGFPE,  gasnetc_exit_sighandler);
  gasneti_reghandler(SIGBUS,  gasnetc_exit_sighandler);

  /* Disable processing of AMs, except system-level ones */
  gasnetc_disable_AMs();

  GASNETI_TRACE_PRINTF(C,("gasnet_exit(%i)\n", exitcode));

  /* Try to flush out all the output, allowing upto 30s */
  alarm(30);
  {
    gasneti_flush_streams();
    gasneti_trace_finish();
    alarm(0);
    gasneti_sched_yield();
  }

  /* Deterimine our role (master or slave) in the coordination of this shutdown */
  alarm(10);
  role = gasnetc_get_exit_role();

  /* Attempt a coordinated shutdown */
  timeout_us = 2000000 + gasnetc_nodes*250000; /* 2s + 0.25s * nodes */
  alarm(1 + timeout_us/1000000);
  switch (role) {
  case GASNETC_EXIT_ROLE_MASTER:
    /* send all the remote exit requests and wait for the replies */
    graceful = (gasnetc_exit_master(exitcode, timeout_us) == 0);
    break;

  case GASNETC_EXIT_ROLE_SLAVE:
    /* wait for the exit request and reply before proceeding */
    graceful = (gasnetc_exit_slave(timeout_us) == 0);
    /* XXX:
     * How do we know our reply has actually been sent on the wire before we trash the end point?
     * We probably need to use the send-drain that IB provides our use our own counters.
     * For now we rely on a short sleep() to be sufficient.
     */
    alarm(0); sleep(1);
    break;

  default:
      gasneti_fatalerror("invalid exit role");
  }

  /* Clean up transport resources, allowing upto 30s */
  alarm(30);
  {
    for (i = 0; i < gasnetc_nodes; ++i) {
      gasnetc_sndrcv_fini_cep(&gasnetc_cep[i]);
    }
    gasnetc_sndrcv_fini();
    if (gasneti_attach_done) {
#if GASNETC_PIN_SEGMENT
      gasnetc_unpin(&gasnetc_seg_reg);
#endif
#if GASNETC_USE_FIREHOSE
#if 0	/* Dump firehose table as pairs: page_number length_in_pages */
      {
	firehose_request_t r;
	const firehose_request_t *p;
	void *prev = NULL;
	uintptr_t segbase = (uintptr_t)gasnetc_seginfo[gasnetc_mynode].addr;
	int count = gasnetc_seginfo[gasnetc_mynode].size / 4096UL;
	int i;

	for (i = 0; i < count; ++i) {
	  p = firehose_try_local_pin(segbase+i*4096, 8, &r);
	  if (!p) {
	    /* MISS */
	    prev = NULL;
	  } else {
	    if ((p->addr == gasnetc_snd_reg.addr)
	 	|| ((gasnetc_nodes > 0) && (p->addr == gasnetc_rcv_reg.addr))
#if GASNETC_PIN_SEGMENT
		|| (p->addr == gasnetc_seg_reg.addr)
#endif
		   ) {
		/* Skip pre-pinned regions */
		i += (p->len / 4096 - 1);
	    } else if (p->internal != prev) {
	      fprintf(stderr, "%d> %d %d\n", gasnetc_mynode, i, (int)p->len/4096);
	    }
	    prev = p->internal;
	    firehose_release(&p, 1);
	  }
	}
}
#endif
      firehose_fini();
#endif
    }
    (void)VAPI_dealloc_pd(gasnetc_hca, gasnetc_pd);
#if !GASNETC_RCV_THREAD	/* can't release from inside the RCV thread */
    (void)EVAPI_release_hca_hndl(gasnetc_hca);
#endif
  }

  /* Try again to flush out any recent output, allowing upto 5s */
  alarm(5);
  {
    gasneti_flush_streams();
    #if !GASNET_DEBUG_VERBOSE
      gasneti_close_streams();
    #endif
  }

  /* XXX potential problems here if exiting from the "Wrong" thread, or from a signal handler */
  alarm(10);
  {
    if (graceful) {
      gasnetc_bootstrapFini();
    } else {
      /* We couldn't reach our peers, so hope the bootstrap code can kill the entire job */
      gasnetc_bootstrapAbort(exitcode);
      /* NOT REACHED */
    }
  }

  alarm(0);
}

/* gasnetc_exit_reqh
 *
 * This is a system-category AM handler and is therefore available as soon as gasnet_init()
 * returns, even before gasnet_attach().  This handler is responsible for receiving the
 * remote exit requests from the master node and replying.  We call gasnetc_exit_head()
 * with the exitcode seen in the remote exit request.  If this remote request is seen before
 * any local exit requests (normal or signal), then we are also responsible for starting the
 * exit procedure, via gasnetc_exit_{body,tail}().  Additionally, we are responsible for
 * firing off a SIGQUIT to let the user's handler, if any, run before we begin to exit.
 */
static void gasnetc_exit_reqh(gasnet_token_t token, gasnet_handlerarg_t *args, int numargs) {
  int rc;

  gasneti_assert(args != NULL);
  gasneti_assert(numargs == 1);

  /* The master will send this AM, but should _never_ receive it */
  gasneti_assert(gasneti_atomic_read(&gasnetc_exit_role) != GASNETC_EXIT_ROLE_MASTER);

  /* We should never receive this AM multiple times */
  gasneti_assert(gasneti_atomic_read(&gasnetc_exit_reqs) == 0);

  /* Count the exit requests, so gasnetc_exit_wait() knows when to return */
  gasneti_atomic_increment(&gasnetc_exit_reqs);

  /* If we didn't already know, we are now certain our role is "slave" */
  (void)gasneti_atomic_compare_and_swap(&gasnetc_exit_role, GASNETC_EXIT_ROLE_UNKNOWN, GASNETC_EXIT_ROLE_SLAVE);

  /* Send a reply so the master knows we are reachable */
  rc = gasnetc_ReplySystem(token, &gasnetc_exit_repl_oust,
		  	   gasneti_handleridx(gasnetc_SYS_exit_rep), /* no args */ 0);
  gasneti_assert(rc == GASNET_OK);

  /* XXX: save the identity of the master here so we can later drain the send queue of the reply? */

  /* Initiate an exit IFF this is the first we've heard of it */
  if (gasnetc_exit_head(args[0])) {
    gasneti_sighandlerfn_t handler;
    /* IMPORTANT NOTE
     * When we reach this point we are in a request handler which will never return.
     * Care should be taken to ensure this doesn't wedge the AM recv logic.
     *
     * This is currently safe because:
     * 1) request handlers are run w/ no locks held
     * 2) we always have an extra thread to recv AM requests
     */

    /* To try and be reasonably robust, want to avoid performing the shutdown and exit from signal
     * context if we can avoid it.  However, we must raise SIGQUIT if the user has registered a handler.
     * Therefore we inspect what is registered before calling raise().
     *
     * XXX we don't do this atomically w.r.t the signal
     * XXX we don't do the right thing w/ SIG_ERR and SIG_HOLD
     */
    handler = gasneti_reghandler(SIGQUIT, SIG_IGN);
    if ((handler != gasneti_defaultSignalHandler) &&
#ifdef SIG_HOLD
	(handler != (gasneti_sighandlerfn_t)SIG_HOLD) &&
#endif
	(handler != (gasneti_sighandlerfn_t)SIG_ERR) &&
	(handler != (gasneti_sighandlerfn_t)SIG_IGN) &&
	(handler != (gasneti_sighandlerfn_t)SIG_DFL)) {
      (void)gasneti_reghandler(SIGQUIT, handler);
      #if 1
        raise(SIGQUIT);
        /* Note: Both ISO C and POSIX assure us that raise() won't return until after the signal handler
         * (if any) has executed.  However, if that handler calls gasnetc_exit(), we'll never return here. */
      #elif 0
	kill(getpid(),SIGQUIT);
      #else
	handler(SIGQUIT);
      #endif
    } else {
      /* No need to restore the handler, since _exit_body will set it to SIG_IGN anyway. */
    }

    gasnetc_exit_body();
    gasnetc_exit_tail();
    /* NOT REACHED */
  }

  return;
}

/* gasnetc_exit_reph
 *
 * Simply count replies
 */
static void gasnetc_exit_reph(gasnet_token_t token, gasnet_handlerarg_t *args, int numargs) {
  gasneti_assert(numargs == 0);

  gasneti_atomic_increment(&gasnetc_exit_reps);
}
  
/* gasnetc_atexit
 *
 * This is a simple atexit() handler to achieve a hopefully graceful exit.
 * We use the functions gasnetc_exit_{head,body}() to coordinate the shutdown.
 * Note that we don't call gasnetc_exit_tail() since we anticipate the normal
 * exit() procedures to shutdown the multi-threaded process nicely and also
 * because we don't have access to the exit code!
 *
 * Unfortunately, we don't have access to the exit code to send to the other
 * nodes in the event this is a non-collective exit.  However, experience with at
 * lease one MPI suggests that when using MPI for bootstrap a non-zero return from
 * at least one executable is sufficient to produce that non-zero exit code from
 * the parallel job.  Therefore, we can "safely" pass 0 to our peers and still
 * expect to preserve a non-zero exit code for the GASNet job as a whole.  Of course
 * there is no _guarantee_ this will work with all bootstraps.
 */
static void gasnetc_atexit(void) {
  /* Check return from _head to avoid reentrance */
  if (gasnetc_exit_head(0)) { /* real exit code is outside our control */
    gasnetc_exit_body();
  }
  return;
}

/* gasnetc_exit
 *
 * This is the start of a locally requested exit from GASNet.
 * The caller might be the user, some part of the conduit which has detected an error,
 * or possibly gasneti_defaultSignalHandler() responding to a termination signal.
 */
extern void gasnetc_exit(int exitcode) {
  gasnetc_exit_head(exitcode);
  gasnetc_exit_body();
  gasnetc_exit_tail();
  /* NOT REACHED */
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
  Active Message Request Functions
  ================================
*/

extern int gasnetc_AMRequestShortM( 
                            gasnet_node_t dest,       /* destination node */
                            gasnet_handler_t handler, /* index into destination endpoint's handler table */ 
                            int numargs, ...) {
  int retval;
  va_list argptr;
  GASNETI_CHECKATTACH();
  if_pf (dest >= gasnetc_nodes) GASNETI_RETURN_ERRR(BAD_ARG,"node index too high");
  gasneti_assert(numargs >= 0 && numargs <= gasnet_AMMaxArgs());
  GASNETI_TRACE_AMREQUESTSHORT(dest,handler,numargs);
  va_start(argptr, numargs); /*  pass in last argument */

  retval = gasnetc_RequestGeneric(gasnetc_Short, dest, handler,
		  		  NULL, 0, NULL,
				  numargs, NULL, argptr);

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
  GASNETI_CHECKATTACH();
  if_pf (dest >= gasnetc_nodes) GASNETI_RETURN_ERRR(BAD_ARG,"node index too high");
  gasneti_assert(numargs >= 0 && numargs <= gasnet_AMMaxArgs());
  if_pf (nbytes > gasnet_AMMaxMedium()) GASNETI_RETURN_ERRR(BAD_ARG,"nbytes too large");
  GASNETI_TRACE_AMREQUESTMEDIUM(dest,handler,source_addr,nbytes,numargs);
  va_start(argptr, numargs); /*  pass in last argument */

  retval = gasnetc_RequestGeneric(gasnetc_Medium, dest, handler,
		  		  source_addr, nbytes, NULL,
				  numargs, NULL, argptr);

  va_end(argptr);
  GASNETI_RETURN(retval);
}

extern int gasnetc_AMRequestLongM( gasnet_node_t dest,        /* destination node */
                            gasnet_handler_t handler, /* index into destination endpoint's handler table */ 
                            void *source_addr, size_t nbytes,   /* data payload */
                            void *dest_addr,                    /* data destination on destination node */
                            int numargs, ...) {
  gasnetc_counter_t mem_oust = GASNETC_COUNTER_INITIALIZER;
  int retval;
  va_list argptr;
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

  retval = gasnetc_RequestGeneric(gasnetc_Long, dest, handler,
		  		  source_addr, nbytes, dest_addr,
				  numargs, &mem_oust, argptr);

  /* block for completion of RDMA transfer */
  gasnetc_counter_wait(&mem_oust, 0);

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
  GASNETI_CHECKATTACH();
  
  gasnetc_boundscheck(dest, dest_addr, nbytes);
  if_pf (dest >= gasnetc_nodes) GASNETI_RETURN_ERRR(BAD_ARG,"node index too high");
  gasneti_assert(numargs >= 0 && numargs <= gasnet_AMMaxArgs());
  if_pf (nbytes > gasnet_AMMaxLongRequest()) GASNETI_RETURN_ERRR(BAD_ARG,"nbytes too large");
  if_pf (((uintptr_t)dest_addr) < ((uintptr_t)gasnetc_seginfo[dest].addr) ||
         ((uintptr_t)dest_addr) + nbytes > 
           ((uintptr_t)gasnetc_seginfo[dest].addr) + gasnetc_seginfo[dest].size) 
         GASNETI_RETURN_ERRR(BAD_ARG,"destination address out of segment range");

  GASNETI_TRACE_AMREQUESTLONGASYNC(dest,handler,source_addr,nbytes,dest_addr,numargs);
  va_start(argptr, numargs); /*  pass in last argument */

  retval = gasnetc_RequestGeneric(gasnetc_Long, dest, handler,
		  		  source_addr, nbytes, dest_addr,
				  numargs, NULL, argptr);

  va_end(argptr);
  GASNETI_RETURN(retval);
}

extern int gasnetc_AMReplyShortM( 
                            gasnet_token_t token,       /* token provided on handler entry */
                            gasnet_handler_t handler, /* index into destination endpoint's handler table */ 
                            int numargs, ...) {
  int retval;
  va_list argptr;

  gasneti_assert(numargs >= 0 && numargs <= gasnet_AMMaxArgs());
  GASNETI_TRACE_AMREPLYSHORT(token,handler,numargs);
  va_start(argptr, numargs); /*  pass in last argument */

  retval = gasnetc_ReplyGeneric(gasnetc_Short, token, handler,
		  		NULL, 0, NULL,
				numargs, NULL, argptr);

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

  gasneti_assert(numargs >= 0 && numargs <= gasnet_AMMaxArgs());
  if_pf (nbytes > gasnet_AMMaxMedium()) GASNETI_RETURN_ERRR(BAD_ARG,"nbytes too large");
  GASNETI_TRACE_AMREPLYMEDIUM(token,handler,source_addr,nbytes,numargs);
  va_start(argptr, numargs); /*  pass in last argument */

  retval = gasnetc_ReplyGeneric(gasnetc_Medium, token, handler,
		  		source_addr, nbytes, NULL,
				numargs, NULL, argptr);

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
  
  retval = gasnetc_AMGetMsgSource(token, &dest);
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

  #if GASNETC_PIN_SEGMENT
  {
    gasnetc_counter_t mem_oust = GASNETC_COUNTER_INITIALIZER;

    retval = gasnetc_ReplyGeneric(gasnetc_Long, token, handler,
		  		  source_addr, nbytes, dest_addr,
				  numargs, &mem_oust, argptr);

    /* block for completion of RDMA transfer */
    gasnetc_counter_wait(&mem_oust, 1 /* calling from a request handler */);
  }
  #else
  retval = gasnetc_ReplyGeneric(gasnetc_Long, token, handler,
		  		source_addr, nbytes, dest_addr,
				numargs, NULL, argptr);

  #endif

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

extern int  gasnetc_hsl_trylock(gasnet_hsl_t *hsl) {
  GASNETI_CHECKATTACH();

  {
    int locked = (gasneti_mutex_trylock(&(hsl->lock)) == 0);

    GASNETI_TRACE_EVENT_VAL(L, HSL_TRYLOCK, locked);
    if (locked) {
      #if GASNETI_STATS_OR_TRACE
        hsl->acquiretime = GASNETI_STATTIME_NOW_IFENABLED(L);
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
  /* ptr-width independent handlers */

  /* ptr-width dependent handlers */

  { 0, NULL }
};

gasnet_handlerentry_t const *gasnetc_get_handlertable() {
  return gasnetc_handlers;
}

/*
  System handlers, available even between _init and _attach
*/

const gasnetc_sys_handler_fn_t gasnetc_sys_handler[GASNETC_MAX_NUMHANDLERS] = {
  NULL,	/* ACK: NULL -> do nothing */
  gasnetc_exit_role_reqh,
  gasnetc_exit_role_reph,
  gasnetc_exit_reqh,
  gasnetc_exit_reph,
  NULL,
};
  
/* ------------------------------------------------------------------------------------ */

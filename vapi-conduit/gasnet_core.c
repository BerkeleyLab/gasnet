/*  $Archive:: /Ti/GASNet/template-conduit/gasnet_core.c                  $
 *     $Date: 2003/09/02 21:20:41 $
 * $Revision: 1.19 $
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
#include <sched.h>

GASNETI_IDENT(gasnetc_IdentString_Version, "$GASNetCoreLibraryVersion: " GASNET_CORE_VERSION_STR " $");
GASNETI_IDENT(gasnetc_IdentString_ConduitName, "$GASNetConduitName: " GASNET_CORE_NAME_STR " $");

#if defined(GASNET_SEGMENT_LARGE) || defined(GASNET_SEGMENT_EVERYTHING)
  #warning "I don't do LARGE or EVERYTHING yet - almost certain to hang at runtime!!"
#endif

/* ------------------------------------------------------------------------------------ */
/*
  Configuration
  ==============

  These calues can be overridden by environment variables.
  Variable names are formed by replacing GASNETC_DEFAULT_ by GASNET_
*/

/* Default HCA and Port */
#define GASNETC_DEFAULT_HCA_ID		NULL		/* NULL or empty = probe */
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


/*
  These calues cannot yet be overridden by environment variables.
*/
#define GASNETC_QP_PATH_MTU		MTU1024
#define GASNETC_QP_STATIC_RATE		0
#define GASNETC_QP_MIN_RNR_TIMER	IB_RNR_NAK_TIMER_0_08
#define GASNETC_QP_RNR_RETRY		7	/* retry forever, but almost never happens */
#define GASNETC_QP_TIMEOUT		0x20
#define GASNETC_QP_RETRY_COUNT		2

/* ------------------------------------------------------------------------------------ */

/* HCA-level resources */
gasnetc_cep_t	*gasnetc_cep;
VAPI_hca_hndl_t	gasnetc_hca;
VAPI_hca_cap_t	gasnetc_hca_cap;
VAPI_hca_port_t	gasnetc_hca_port;
VAPI_pd_hndl_t	gasnetc_pd;
#if defined(GASNET_SEGMENT_FAST)
  gasnetc_memreg_t	gasnetc_seg_reg;
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

gasnetc_handler_fn_t const gasnetc_unused_handler = (gasnetc_handler_fn_t)&abort;
gasnetc_handler_fn_t gasnetc_handler[GASNETC_MAX_NUMHANDLERS]; /* handler table */

static pid_t gasnetc_mypid;
static void gasnetc_atexit(void);
static void gasnetc_exit_sighandler(int sig);

/* ------------------------------------------------------------------------------------ */
/*
  Initialization
  ==============
*/
/* called at startup to check configuration sanity */
static void gasnetc_check_config() {
  assert(sizeof(gasnetc_medmsg_t) == (GASNETC_MEDIUM_HDRSZ + 4*GASNETC_MAX_ARGS));
  assert(GASNETC_RCV_POLL || GASNETC_RCV_THREAD);
  assert(GASNETC_PUT_COPY_LIMIT <= GASNETC_BUFSZ);
}

extern gasnetc_memreg_t *gasnetc_local_reg(uintptr_t start, uintptr_t end) {
  #if defined(GASNET_SEGMENT_FAST)
    if ((start >= gasnetc_seg_reg.start) && (end <= gasnetc_seg_reg.end)) {
      return &gasnetc_seg_reg;
    }
  #else
    /* (###) implement firehose */
  #endif

  if ((start >= gasnetc_rcv_reg.start) && (end <= gasnetc_rcv_reg.end)) {
    return &gasnetc_rcv_reg;
  }

  if ((start >= gasnetc_snd_reg.start) && (end <= gasnetc_snd_reg.end)) {
    return &gasnetc_snd_reg;
  }

  /* Not pinned */
  return NULL;
}

GASNET_INLINE_MODIFIER(gasnetc_is_pinned_remote)
int gasnetc_is_pinned_remote(gasnet_node_t node, uintptr_t start, size_t len) {
  uintptr_t	end = (start + (len - 1)); /* subtact 1 first, to avoid overflows */

  #if defined(GASNET_SEGMENT_FAST)
  {
    /* check if the range is entirely in the remotely pinned segment */
    uintptr_t segbase = (uintptr_t)gasnetc_seginfo[node].addr;
    uintptr_t segsize = gasnetc_seginfo[node].size;

    if ((start >= segbase) && (end <= (segbase + (segsize - 1)))) {
      return 1;
    }
  }
  #else
    /* (###) implement firehose */
  #endif

  /* Not pinned */
  return 0;
}

static void gasnetc_unpin(gasnetc_memreg_t *reg) {
  VAPI_ret_t vstat;

  vstat = VAPI_deregister_mr(gasnetc_hca, reg->handle);
  assert(vstat == VAPI_OK);
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

  reg->lkey	= mr_out.l_key;
  reg->rkey	= mr_out.r_key;
  reg->start	= mr_out.start;
  reg->end	= mr_out.start + (mr_out.size - 1); /* subtract first to avoid overflow */
  reg->size	= mr_out.size;
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

#ifdef LINUX
#define _BUFSZ	120
/* XXX how does this fair on systems w/ >4GB */
static uintptr_t gasnetc_get_physmem()
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

  return (uintptr_t) mem;
}
#else
#error "Don't know how to get physical memory size on your O/S"
#endif

/* Search for largest region we can allocate and pin */
static uintptr_t gasnetc_max_pinnable(void) {
  uintptr_t lo, hi;
  uintptr_t mmap_size, pin_size;
  void *addr;

  /* binary search for largest mmap() region */
  lo = GASNET_PAGESIZE;
  hi = MIN(2 * (gasnetc_get_physmem() / 3), (uintptr_t)gasnetc_hca_cap.max_mr_size);
  mmap_size = GASNETI_PAGE_ALIGNDOWN(hi);
  do {
    addr = gasneti_mmap(mmap_size);
    if (addr == (void *)-1) {
      hi = mmap_size;
    } else {
      gasneti_munmap(addr, mmap_size);
      lo = mmap_size;
    }

    mmap_size = GASNETI_PAGE_ALIGNDOWN(lo + (hi - lo) / 2);
  } while (mmap_size > lo);
  addr = gasneti_mmap(mmap_size);

  /* Now search for largest pinnable region */
  lo = GASNET_PAGESIZE;
  pin_size = hi = mmap_size;
  do {
    gasnetc_memreg_t reg;
    VAPI_ret_t vstat;

    vstat = gasnetc_pin(addr, pin_size, 0, &reg);
    if (vstat != VAPI_OK) {
      hi = pin_size;
    } else {
      gasnetc_unpin(&reg);
      lo = pin_size;
    }

    pin_size = GASNETI_PAGE_ALIGNDOWN(lo + (hi - lo) / 2);
  } while (pin_size > lo);
  gasneti_munmap(addr, mmap_size);

  return pin_size;
}

/* Process defaults and the environment to get configuration settings */
static int gasnetc_load_settings(void) {
  char	*tmp;

  tmp = getenv("GASNET_HCA_ID");
  if (tmp) {
    gasnetc_hca_id = gasneti_strdup(tmp);
  } else {
    gasnetc_hca_id = GASNETC_DEFAULT_HCA_ID;
  }

  tmp = getenv("GASNET_PORT_NUM");
  if (tmp) {
    gasnetc_port_num = atoi(tmp);
  } else {
    gasnetc_port_num = GASNETC_DEFAULT_PORT_NUM;
  }

  tmp = gasneti_getenv("GASNET_OP_OUST_LIMIT");
  if (tmp) {
    gasnetc_op_oust_limit = atoi(tmp);
    if (gasnetc_op_oust_limit < 1) {
      GASNETI_RETURN_ERRR(BAD_ARG, "(GASNET_OP_OUST_LIMIT < 1) in environment");
    }
  } else {
    gasnetc_op_oust_limit = GASNETC_DEFAULT_OP_OUST_LIMIT;
  }

  tmp = gasneti_getenv("GASNET_OP_OUST_PP");
  if (tmp) {
    gasnetc_op_oust_pp = atoi(tmp);
    if (gasnetc_op_oust_pp < 1) {
      GASNETI_RETURN_ERRR(BAD_ARG, "(GASNET_OP_OUST_PP < 1) in environment");
    }
  } else {
    gasnetc_op_oust_pp = GASNETC_DEFAULT_OP_OUST_PP;
  }

  tmp = gasneti_getenv("GASNET_AM_OUST_LIMIT");
  if (tmp) {
    gasnetc_am_oust_limit = atoi(tmp);
    if (gasnetc_am_oust_limit < 1) {
      GASNETI_RETURN_ERRR(BAD_ARG, "(GASNET_AM_OUST_LIMIT < 1) in environment");
    }
  } else {
    gasnetc_am_oust_limit = GASNETC_DEFAULT_AM_OUST_LIMIT;
  }

  tmp = gasneti_getenv("GASNET_AM_OUST_PP");
  if (tmp) {
    gasnetc_am_oust_pp = atoi(tmp);
    if (gasnetc_am_oust_pp < 1) {
      GASNETI_RETURN_ERRR(BAD_ARG, "(GASNET_AM_OUST_PP < 1) in environment");
    }
  } else {
    gasnetc_am_oust_pp = GASNETC_DEFAULT_AM_OUST_PP;
  }

  tmp = gasneti_getenv("GASNET_AM_SPARES");
  if (tmp) {
    gasnetc_am_spares = atoi(tmp);
    if (gasnetc_am_spares < 1) {
      GASNETI_RETURN_ERRR(BAD_ARG, "(GASNET_AM_SPARES < 1) in environment");
    }
  } else {
    gasnetc_am_spares = GASNETC_DEFAULT_AM_SPARES;
  }

  return GASNET_OK;
}

static int gasnetc_init(int *argc, char ***argv) {
  gasnetc_addr_t	*local_addr;
  gasnetc_addr_t	*remote_addr;
  VAPI_ret_t		vstat;
  int 			i;

  /*  check system sanity */
  gasnetc_check_config();

  if (gasneti_init_done) 
    GASNETI_RETURN_ERRR(NOT_INIT, "GASNet already initialized");

  if (getenv("GASNET_FREEZE")) gasneti_freezeForDebugger();

  #if DEBUG_VERBOSE
    /* note - can't call trace macros during gasnet_init because trace system not yet initialized */
    fprintf(stderr,"gasnetc_init(): about to spawn...\n"); fflush(stderr);
  #endif

  /* Initialize the bootstrapping support */
  gasnetc_bootstrapInit(argc, argv, &gasnetc_nodes, &gasnetc_mynode);
    
  /* Setup for gasneti_getenv() */
  gasneti_setupGlobalEnvironment(gasnetc_nodes, gasnetc_mynode, 
                                 gasnetc_bootstrapAllgather, gasnetc_bootstrapBroadcast);

  /* Process the environment for configuration/settings */
  i = gasnetc_load_settings();
  if (i != GASNET_OK) {
    return i;
  }

  /* allocate resources */
  gasnetc_cep = gasneti_calloc(gasnetc_nodes, sizeof(gasnetc_cep_t));
  local_addr = gasneti_calloc(gasnetc_nodes, sizeof(gasnetc_addr_t));
  remote_addr = gasneti_calloc(gasnetc_nodes, sizeof(gasnetc_addr_t));

  /* open the hca and get port & lid values */
  {
    VAPI_hca_vendor_t hca_vendor;

    if (!gasnetc_hca_id || !strlen(gasnetc_hca_id)) {
      /* Empty means probe for HCAs */
      VAPI_hca_id_t	*hca_ids;
      u_int32_t		num_hcas;	/* Type specified by Mellanox */

      vstat = EVAPI_list_hcas(0, &num_hcas, NULL);
      assert(vstat == VAPI_EAGAIN);
      if (num_hcas == 0) {
        /* XXX cleanup */
        GASNETI_RETURN_ERRR(RESOURCE, "failed locate any HCAs");
      }
      hca_ids = gasneti_calloc(num_hcas, sizeof(VAPI_hca_id_t));
      vstat = EVAPI_list_hcas(num_hcas, &num_hcas, hca_ids);
      assert(vstat == VAPI_OK);

      for (i = 0; i <= num_hcas; ++i) {
        vstat = VAPI_open_hca(hca_ids[i], &gasnetc_hca);
        if (vstat != VAPI_OK) {
          vstat = EVAPI_get_hca_hndl(hca_ids[i], &gasnetc_hca);
        }
        if (vstat == VAPI_OK) {
	  break;
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
        GASNETI_RETURN_ERRR(RESOURCE, "failed open the HCA");
      }
    }

    vstat = VAPI_query_hca_cap(gasnetc_hca, &hca_vendor, &gasnetc_hca_cap);
    assert(vstat == VAPI_OK && "Unable to query HCA capabilities");

    if (gasnetc_port_num == 0) {
      /* Zero means probe for the first active port */
      for (gasnetc_port_num = 1; gasnetc_port_num <= gasnetc_hca_cap.phys_port_num; ++gasnetc_port_num) {
        (void)VAPI_query_hca_port_prop(gasnetc_hca, gasnetc_port_num, &gasnetc_hca_port);

        if (gasnetc_hca_port.state == PORT_ACTIVE) {
	  break;
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

    #if DEBUG_VERBOSE
      fprintf(stderr, "gasnetc_init(): located HCA id='%s' port=%d on node %d/%d\n",
              gasnetc_hca_id, gasnetc_port_num, gasnetc_mynode, gasnetc_nodes);
      fflush(stderr);
    #endif
  }

  /* check hca and port properties */
  assert(gasnetc_hca_cap.max_num_qp >= gasnetc_nodes);
  assert(gasnetc_hca_cap.max_qp_ous_wr >= gasnetc_op_oust_pp);
  assert(gasnetc_hca_cap.max_qp_ous_wr >= gasnetc_am_oust_pp * 2);
  assert(gasnetc_hca_cap.max_num_sg_ent >= GASNETC_SND_SG);
  assert(gasnetc_hca_cap.max_num_sg_ent >= GASNETC_RCV_SG);
  assert(gasnetc_hca_cap.max_num_sg_ent_rd >= 1);		/* RDMA Read support required */
  #if 1 /* QP end points */
    assert(gasnetc_hca_cap.max_qp_init_rd_atom >= 1);		/* RDMA Read support required */
    assert(gasnetc_hca_cap.max_qp_ous_rd_atom >= 1);		/* RDMA Read support required */
  #else
    assert(gasnetc_hca_cap.max_ee_init_rd_atom >= 1);		/* RDMA Read support required */
    assert(gasnetc_hca_cap.max_ee_ous_rd_atom >= 1);		/* RDMA Read support required */
  #endif
  assert(gasnetc_hca_cap.max_num_cq >= 2);
  assert(gasnetc_hca_cap.max_num_ent_cq >= gasnetc_op_oust_limit);
  assert(gasnetc_hca_cap.max_num_ent_cq >= gasnetc_am_oust_limit * 2); /* request + reply == 2 */
  #if defined(GASNET_SEGMENT_LARGE) || defined(GASNET_SEGMENT_EVERYTHING)
    assert(gasnetc_hca_cap.max_num_mr >= (3+gasnetc_nodes));	/* rcv bufs, snd bufs, segment, n*fh */
  #else
    assert(gasnetc_hca_cap.max_num_mr >= 3);			/* rcv bufs, snd bufs, segment */
  #endif
  assert(gasnetc_hca_port.max_msg_sz >= GASNETC_PUT_COPY_LIMIT);


  /* get a pd for the QPs and memory registration */
  vstat =  VAPI_alloc_pd(gasnetc_hca, &gasnetc_pd);
  assert(vstat == VAPI_OK);

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
      assert(vstat == VAPI_OK);
      assert(qp_prop.cap.max_oust_wr_rq >= gasnetc_am_oust_pp * 2);
      assert(qp_prop.cap.max_oust_wr_sq >= gasnetc_op_oust_pp);

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
      assert(vstat == VAPI_OK);
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
      assert(vstat == VAPI_OK);
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
      assert(vstat == VAPI_OK);
      assert(qp_cap.max_inline_data_sq >= GASNETC_PUT_INLINE_LIMIT);
    }
  }

  gasneti_free(remote_addr);
  gasneti_free(local_addr);

  #if DEBUG_VERBOSE
    fprintf(stderr,"gasnetc_init(): spawn successful - node %i/%i starting...\n", 
      gasnetc_mynode, gasnetc_nodes); fflush(stderr);
  #endif

  #if defined(GASNET_SEGMENT_FAST)
  {
    gasneti_segmentInit(&gasnetc_MaxLocalSegmentSize,
                        &gasnetc_MaxGlobalSegmentSize,
                        gasnetc_max_pinnable(),
                        gasnetc_nodes,
                        &gasnetc_bootstrapAllgather);
  }
  #elif defined(GASNET_SEGMENT_LARGE)
  {
    gasneti_segmentInit(&gasnetc_MaxLocalSegmentSize,
                        &gasnetc_MaxGlobalSegmentSize,
                        (uintptr_t)(-1),
                        gasnetc_nodes,
                        &gasnetc_bootstrapAllgather);
  }
  #elif defined(GASNET_SEGMENT_EVERYTHING)
  {
    gasnetc_MaxLocalSegmentSize =  (uintptr_t)-1;
    gasnetc_MaxGlobalSegmentSize = (uintptr_t)-1;
  }
  #endif

  /* Set up for exit handlers */
  gasnetc_mypid = getpid();
  gasneti_reghandler(SIGUSR1, gasnetc_exit_sighandler);
  atexit(gasnetc_atexit);

  gasneti_init_done = 1;  
  gasnetc_bootstrapBarrier();

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

  if (!gasneti_init_done) 
    GASNETI_RETURN_ERRR(NOT_INIT, "GASNet attach called before init");
  if (gasneti_attach_done) 
    GASNETI_RETURN_ERRR(NOT_INIT, "GASNet already attached");

  /*  check argument sanity */
  #if defined(GASNET_SEGMENT_FAST) || defined(GASNET_SEGMENT_LARGE)
    if ((segsize % GASNET_PAGESIZE) != 0) 
      GASNETI_RETURN_ERRR(BAD_ARG, "segsize not page-aligned");
    if (segsize > gasnetc_getMaxLocalSegmentSize()) 
      GASNETI_RETURN_ERRR(BAD_ARG, "segsize too large");
    if ((minheapoffset % GASNET_PAGESIZE) != 0) /* round up the minheapoffset to page sz */
      minheapoffset = ((minheapoffset / GASNET_PAGESIZE) + 1) * GASNET_PAGESIZE;
  #elif defined(GASNET_SEGMENT_EVERYTHING)
    segsize = 0;
    minheapoffset = 0;
  #endif

  /* ------------------------------------------------------------------------------------ */
  /*  register handlers */
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

  /* catch fatal signals and convert to SIGQUIT */
  gasneti_registerSignalHandlers(gasneti_defaultSignalHandler);

  /*  (###) register any custom signal handlers required by your conduit 
   *        (e.g. to support interrupt-based messaging)
   */

  /* ------------------------------------------------------------------------------------ */
  /*  register segment  */

  gasnetc_seginfo = (gasnet_seginfo_t *)gasneti_malloc(gasnetc_nodes*sizeof(gasnet_seginfo_t));

  #if defined(GASNET_SEGMENT_FAST)
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
      assert(vstat == VAPI_OK);

      rkeys = gasneti_calloc(gasnetc_nodes,sizeof(VAPI_rkey_t));
      assert(rkeys != NULL);
      gasnetc_bootstrapAllgather(&gasnetc_seg_reg.rkey, sizeof(VAPI_rkey_t), rkeys);
      for (i=0;i<gasnetc_nodes;i++) {
        gasnetc_cep[i].rkey = rkeys[i];
      }
      gasneti_free(rkeys);
    }
  }
  #elif defined(GASNET_SEGMENT_LARGE)
  {
    /* allocate the segment and exchange seginfo */
    gasneti_segmentAttach(segsize, minheapoffset, gasnetc_seginfo, &gasnetc_bootstrapAllgather);
    segbase = gasnetc_seginfo[gasnetc_mynode].addr;
    segsize = gasnetc_seginfo[gasnetc_mynode].size;

    /* (###) add any code here needed to setup firehose support */
  }
  #elif defined(GASNET_SEGMENT_EVERYTHING)
  {
    int i;
    for (i=0;i<gasnetc_nodes;i++) {
      gasnetc_seginfo[i].addr = (void *)0;
      gasnetc_seginfo[i].size = (uintptr_t)-1;
    }
    segbase = (void *)0;
    segsize = (uintptr_t)-1;

    /* (###) add any code here needed to setup firehose support */
  }
  #endif

  /* ------------------------------------------------------------------------------------ */
  /*  primary attach complete */
  gasneti_attach_done = 1;
  gasnetc_bootstrapBarrier();

  GASNETI_TRACE_PRINTF(C,("gasnetc_attach(): primary attach complete"));

  assert(gasnetc_seginfo[gasnetc_mynode].addr == segbase &&
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

static gasneti_atomic_t gasnetc_exit_code = gasneti_atomic_init(0);	/* value to _exit() with */
static gasneti_atomic_t gasnetc_exit_reqs = gasneti_atomic_init(0);	/* count of remote exit requests */
static gasneti_atomic_t gasnetc_exit_reps = gasneti_atomic_init(0);	/* count of remote exit replies */
static gasneti_atomic_t gasnetc_exit_done = gasneti_atomic_init(0);	/* flag to show exit coordination done */

#define GASNETC_ROOT_NODE 0

enum {
  GASNETC_EXIT_ROLE_UNKNOWN,
  GASNETC_EXIT_ROLE_MASTER,
  GASNETC_EXIT_ROLE_SLAVE
};

static gasneti_atomic_t gasnetc_exit_role = gasneti_atomic_init(GASNETC_EXIT_ROLE_UNKNOWN);

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

  assert(numargs == 0);
  assert(gasnetc_mynode == GASNETC_ROOT_NODE);	/* May only send this request to the root node */

  
  /* What role would the local node get if the requester is made the master? */
  rc = gasnet_AMGetMsgSource(token, &src);
  assert(rc == GASNET_OK);
  local_role = (src == GASNETC_ROOT_NODE) ? GASNETC_EXIT_ROLE_MASTER : GASNETC_EXIT_ROLE_SLAVE;

  /* Try atomically to assume the proper role.  Result determines role of requester */
  result = gasneti_atomic_swap(&gasnetc_exit_role, GASNETC_EXIT_ROLE_UNKNOWN, local_role)
                ? GASNETC_EXIT_ROLE_MASTER : GASNETC_EXIT_ROLE_SLAVE;

  /* Inform the requester of the outcome. */
  rc = gasnetc_ReplySystem(token, 1, gasneti_handleridx(gasnetc_SYS_exit_role_rep),
			   1, (gasnet_handlerarg_t)result);
  assert(rc == GASNET_OK);
}

/*
 * gasnetc_exit_role_reph()
 *
 * This reply handler receives the result of the election of an exit "master".
 * The reply contains the exit "role" this node should assume.
 */
static void gasnetc_exit_role_reph(gasnet_token_t token, gasnet_handlerarg_t *args, int numargs) {
  int role;

  #ifdef DEBUG
  {
    gasnet_node_t src;
    int rc;

    rc = gasnet_AMGetMsgSource(token, &src);
    assert(rc == GASNET_OK);
    assert(src == GASNETC_ROOT_NODE);	/* May only receive this reply from the root node */
  }
  #endif

  /* What role has this node been assigned? */
  assert(args != NULL);
  assert(numargs == 1);
  role = (int)args[0];
  assert((role == GASNETC_EXIT_ROLE_MASTER) || (role == GASNETC_EXIT_ROLE_SLAVE));

  /* Set the role if not yet set.  Then assert that the assigned role has been assumed.
   * This way the assertion is checking that if the role was obtained by other means
   * (namely by receiving an exit request) it must match the election result. */
  gasneti_atomic_swap(&gasnetc_exit_role, GASNETC_EXIT_ROLE_UNKNOWN, role);
  assert (gasneti_atomic_read(&gasnetc_exit_role) == role);
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
    rc = gasnetc_RequestSystem(GASNETC_ROOT_NODE, 1, gasneti_handleridx(gasnetc_SYS_exit_role_req), 0);
    assert(rc == GASNET_OK);

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

  _exit(exitcode);
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
 * This code tries to kill the full process in the presence of threads before
 * proceeding to gasnetc_exit_now() to actually terminate.
 *
 * DOES NOT RETURN
 */
static void gasnetc_exit_tail(void) GASNET_NORETURN;
static void gasnetc_exit_tail(void) {
  int exitcode = gasneti_atomic_read(&gasnetc_exit_code);

  /* We need to be certain that the entire multi-threaded process will exit.
   * POSIX threads say that exit() ensures this, but is silent (?) on _exit().
   * At least on some systems _exit() skips the at-exit handler that kills the other threads.
   * This is an attempt to get the main thread to exit unconditionally.
   */
  gasneti_reghandler(SIGUSR1, gasnetc_exit_sighandler);	/* redundant, but just in case */
  kill(gasnetc_mypid, SIGUSR1);

  /* goodbye... */
  gasnetc_exit_now(exitcode);
  /* NOT REACHED */
}

/* gasnetc_exit_sighandler
 *
 * This signal handler is for a last-ditch exit when a signal arrives while
 * attempting the graceful exit.  That includes SIGALRM if we get wedged.
 * It is also used, on SIGUSR1, as part of the mechanism for ensuring
 * that all threads will exit (we hope).
 *
 * Just a signal-handler wrapper for gasnetc_exit_now().
 *
 * DOES NOT RETURN
 */
static void gasnetc_exit_sighandler(int sig) {
  int exitcode = gasneti_atomic_read(&gasnetc_exit_code);

  #if DEBUG_VERBOSE
  /* note - can't call trace macros here, or even sprintf */
  if (sig != SIGUSR1) {
    static const char msg[] = "gasnet_exit(): signal received during exit... goodbye\n";
    write(STDERR_FILENO, msg, sizeof(msg));
    /* fflush(stderr);   NOT REENTRANT */
  }
  #endif

  gasnetc_exit_now(exitcode);
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

  assert(timeout_us > 0); 

  start_time = gasneti_getMicrosecondTimeStamp();

  /* Notify phase */
  for (i = 0; i < gasnetc_nodes; ++i) {
    if (i == gasnetc_mynode) continue;

    if ((gasneti_getMicrosecondTimeStamp() - start_time) > timeout_us) return -1;

    rc = gasnetc_RequestSystem(i, 1, gasneti_handleridx(gasnetc_SYS_exit_req), 1, (gasnet_handlerarg_t)exitcode);
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

  assert(timeout_us > 0); 

  start_time = gasneti_getMicrosecondTimeStamp();

  while (gasneti_atomic_read(&gasnetc_exit_reqs) == 0) {
    if ((gasneti_getMicrosecondTimeStamp() - start_time) > timeout_us) return -1;

    gasnetc_sndrcv_poll(); /* works even before _attach */
  }

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
  #if DEBUG
    gasneti_reghandler(SIGABRT, SIG_DFL);
  #else
    gasneti_reghandler(SIGABRT, gasnetc_exit_sighandler);
  #endif
  gasneti_reghandler(SIGILL,  gasnetc_exit_sighandler);
  gasneti_reghandler(SIGSEGV, gasnetc_exit_sighandler);
  gasneti_reghandler(SIGFPE,  gasnetc_exit_sighandler);
  gasneti_reghandler(SIGBUS,  gasnetc_exit_sighandler);

  GASNETI_TRACE_PRINTF(C,("gasnet_exit(%i)\n", exitcode));

  /* Try to flush out all the output, allowing upto 30s */
  alarm(30);
  {
    gasneti_trace_finish();
    if (fflush(stdout)) 
      gasneti_fatalerror("failed to flush stdout in gasnetc_exit: %s", strerror(errno));
    if (fflush(stderr)) 
      gasneti_fatalerror("failed to flush stderr in gasnetc_exit: %s", strerror(errno));
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
    /* wait until the exit request is received from the master before proceeding */
    graceful = (gasnetc_exit_slave(timeout_us) == 0);
    /* XXX:
     * How do we know our reply has actually been sent on the wire before we trash the end point?
     * We probably need to use the send-drain that IB provides our use our own counters.
     * For now we rely on a short sleep() to be sufficient.
     */
    alarm(0); sleep(1);
    break;

  default:
    assert(0);
  }

  /* Clean up transport resources, allowing upto 30s */
  alarm(30);
  {
    for (i = 0; i < gasnetc_nodes; ++i) {
      if (i == gasnetc_mynode) continue;

      VAPI_destroy_qp(gasnetc_hca, gasnetc_cep[i].qp_handle);
    }
    gasnetc_sndrcv_fini();
#if defined(GASNET_SEGMENT_FAST)
    if (gasneti_attach_done) {
      gasnetc_unpin(&gasnetc_seg_reg);
    }
#endif
    (void)VAPI_dealloc_pd(gasnetc_hca, gasnetc_pd);
#if !GASNETC_RCV_THREAD	/* can't release from inside the RCV thread */
    (void)EVAPI_release_hca_hndl(gasnetc_hca);
#endif
  }

  /* Try again to flush out any recent output, allowing upto 5s */
  alarm(5);
  {
    if (fflush(stdout)) 
      gasneti_fatalerror("failed to flush stdout in gasnetc_exit: %s", strerror(errno));
    if (fflush(stderr)) 
      gasneti_fatalerror("failed to flush stderr in gasnetc_exit: %s", strerror(errno));
    if (fclose(stdin)) 
      gasneti_fatalerror("failed to close stdin in gasnetc_exit: %s", strerror(errno));
    if (fclose(stdout)) 
      gasneti_fatalerror("failed to close stdout in gasnetc_exit: %s", strerror(errno));
    #if !DEBUG_VERBOSE
      if (fclose(stderr)) 
          gasneti_fatalerror("failed to close stderr in gasnetc_exit: %s", strerror(errno));
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

  assert(args != NULL);
  assert(numargs == 1);

  /* The master will send this AM, but should _never_ receive it */
  assert(gasneti_atomic_read(&gasnetc_exit_role) != GASNETC_EXIT_ROLE_MASTER);

  /* We should never receive this AM multiple times */
  assert(gasneti_atomic_read(&gasnetc_exit_reqs) == 0);

  /* Count the exit requests, so gasnetc_exit_wait() knows when to return */
  gasneti_atomic_increment(&gasnetc_exit_reqs);

  /* If we didn't already know, we are now certain our role is "slave" */
  (void)gasneti_atomic_swap(&gasnetc_exit_role, GASNETC_EXIT_ROLE_UNKNOWN, GASNETC_EXIT_ROLE_SLAVE);

  /* Send a reply so the master knows we are reachable */
  rc = gasnetc_ReplySystem(token, 1, gasneti_handleridx(gasnetc_SYS_exit_rep), /* no args */ 0);
  assert(rc == GASNET_OK);

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
  assert(numargs == 0);

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
  gasnetc_exit_head(0);	/* real exit code is outside our control */
  gasnetc_exit_body();
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
  assert(gasnetc_seginfo && seginfo_table);
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
  assert(numargs >= 0 && numargs <= gasnet_AMMaxArgs());
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
  assert(numargs >= 0 && numargs <= gasnet_AMMaxArgs());
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
  gasneti_atomic_t mem_oust = gasneti_atomic_init(0);
  int retval;
  va_list argptr;
  GASNETI_CHECKATTACH();
  
  gasnetc_boundscheck(dest, dest_addr, nbytes);
  if_pf (dest >= gasnetc_nodes) GASNETI_RETURN_ERRR(BAD_ARG,"node index too high");
  assert(numargs >= 0 && numargs <= gasnet_AMMaxArgs());
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
  assert(numargs >= 0 && numargs <= gasnet_AMMaxArgs());
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

  assert(numargs >= 0 && numargs <= gasnet_AMMaxArgs());
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

  assert(numargs >= 0 && numargs <= gasnet_AMMaxArgs());
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
  gasneti_atomic_t mem_oust = gasneti_atomic_init(0);
  int retval;
  gasnet_node_t dest;
  va_list argptr;
  
  retval = gasnetc_AMGetMsgSource(token, &dest);
  if (retval != GASNET_OK) GASNETI_RETURN(retval);
  gasnetc_boundscheck(dest, dest_addr, nbytes);
  if_pf (dest >= gasnetc_nodes) GASNETI_RETURN_ERRR(BAD_ARG,"node index too high");
  assert(numargs >= 0 && numargs <= gasnet_AMMaxArgs());
  if_pf (nbytes > gasnet_AMMaxLongReply()) GASNETI_RETURN_ERRR(BAD_ARG,"nbytes too large");
  if_pf (((uintptr_t)dest_addr) < ((uintptr_t)gasnetc_seginfo[dest].addr) ||
         ((uintptr_t)dest_addr) + nbytes > 
           ((uintptr_t)gasnetc_seginfo[dest].addr) + gasnetc_seginfo[dest].size) 
         GASNETI_RETURN_ERRR(BAD_ARG,"destination address out of segment range");

  GASNETI_TRACE_AMREPLYLONG(token,handler,source_addr,nbytes,dest_addr,numargs);
  va_start(argptr, numargs); /*  pass in last argument */

  retval = gasnetc_ReplyGeneric(gasnetc_Long, token, handler,
		  		source_addr, nbytes, dest_addr,
				numargs, &mem_oust, argptr);

  /* block for completion of RDMA transfer */
  gasnetc_counter_wait(&mem_oust, 1 /* calling from a request handler */);

  va_end(argptr);
  GASNETI_RETURN(retval);
}

/* ------------------------------------------------------------------------------------ */
/*
  Handler-safe locks
  ==================
*/

extern void gasnetc_hsl_init(gasnet_hsl_t *hsl) {
  GASNETI_CHECKATTACH();

  { int retval = pthread_mutex_init(&(hsl->lock), NULL);
    if (retval) 
      gasneti_fatalerror("In gasnetc_hsl_init(), pthread_mutex_init()=%s",strerror(retval));
  }
}

extern void gasnetc_hsl_destroy(gasnet_hsl_t *hsl) {
  GASNETI_CHECKATTACH();

  { int retval = pthread_mutex_destroy(&(hsl->lock));
    if (retval) 
      gasneti_fatalerror("In gasnetc_hsl_destroy(), pthread_mutex_destroy()=%s",strerror(retval));
  }
}

extern void gasnetc_hsl_lock(gasnet_hsl_t *hsl) {
  GASNETI_CHECKATTACH();

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
}

extern void gasnetc_hsl_unlock (gasnet_hsl_t *hsl) {
  GASNETI_CHECKATTACH();

  GASNETI_TRACE_EVENT_TIME(L, HSL_UNLOCK, GASNETI_STATTIME_NOW()-hsl->acquiretime);

  { int retval = pthread_mutex_unlock(&(hsl->lock));
    if (retval) 
      gasneti_fatalerror("In gasnetc_hsl_unlock(), pthread_mutex_unlock()=%s",strerror(retval));
  }
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

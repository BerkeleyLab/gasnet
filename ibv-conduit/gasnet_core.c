/*  $Archive:: /Ti/GASNet/template-conduit/gasnet_core.c                  $
 *     $Date: 2003/03/25 06:08:36 $
 * $Revision: 1.2.2.6 $
 * Description: GASNet vapi conduit Implementation
 * Copyright 2002, Dan Bonachea <bonachea@cs.berkeley.edu>
 * Terms of use are as specified in license.txt
 */

#define GASNETC_BOOTSTRAP_MPI	1

#include <gasnet.h>
#include <gasnet_internal.h>
#include <gasnet_handler.h>
#include <gasnet_core_internal.h>

#include <errno.h>
#include <unistd.h>

#if GASNETC_BOOTSTRAP_MPI
#  include <mpi.h>
#endif

GASNETI_IDENT(gasnetc_IdentString_Version, "$GASNetCoreLibraryVersion: " GASNET_CORE_VERSION_STR " $");
GASNETI_IDENT(gasnetc_IdentString_ConduitName, "$GASNetConduitName: " GASNET_CORE_NAME_STR " $");

gasnetc_cep_t		*gasnetc_cep;
gasnetc_snd_desc_t	*gasnetc_snd_desc_pool;

VAPI_hca_hndl_t	gasnetc_hca;
VAPI_pd_hndl_t	gasnetc_pd;
VAPI_cq_hndl_t	gasnetc_rcv_cq;
VAPI_cq_hndl_t	gasnetc_snd_cq;

gasnetc_regmem_t	gasnetc_rcv_reg;
gasnetc_regmem_t	gasnetc_snd_reg;


/* Used only once, to exchange addresses at connection time */
typedef struct _gasnetc_addr_t {
  IB_lid_t	lid;
  VAPI_qp_num_t	qp_num;
} gasnetc_addr_t;

gasnet_handlerentry_t const *gasnetc_get_handlertable();

gasnet_node_t gasnetc_mynode = -1;
gasnet_node_t gasnetc_nodes = 0;

uintptr_t gasnetc_MaxLocalSegmentSize = 0;
uintptr_t gasnetc_MaxGlobalSegmentSize = 0;

gasnet_seginfo_t *gasnetc_seginfo = NULL;

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
/*
  Initialization
  ==============
*/
/* called at startup to check configuration sanity */
static void gasnetc_check_config() {
  assert(sizeof(gasnetc_medmsg_t) == (GASNETC_MEDIUM_HDRSZ + 4*GASNETC_MAX_ARGS));
  assert((GASNET_MAXNODES * GASNETC_RCV_WQE) <= GASNETC_CQ_SIZE);

  /* (###) add code to do some sanity checks on the number of nodes, handlers
   * and/or segment sizes */ 
}

#if GASNETC_BOOTSTRAP_MPI
static void gasnetc_bootstrapInit(int *argc, char ***argv) {
  int err;

  err = MPI_Init(argc, argv);
  assert(err == MPI_SUCCESS);
}

static void gasnetc_bootstrapFini(void) {
  (void) MPI_Finalize();
}

static void gasnetc_bootstrapConf(void) {
  int err, tmp;

  err = MPI_Comm_rank(MPI_COMM_WORLD, &tmp);
  assert(err == MPI_SUCCESS);
  gasnetc_mynode = tmp;
    
  err = MPI_Comm_size(MPI_COMM_WORLD, &tmp);
  assert(err == MPI_SUCCESS);
  gasnetc_nodes = tmp;
}

static void gasnetc_bootstrapBarrier(void) {
  int err;

  err = MPI_Barrier(MPI_COMM_WORLD);
  assert(err == MPI_SUCCESS);
}

static void gasnetc_bootstrapAllgather(void *src, size_t len, void *dest) {
  int err;

  err = MPI_Allgather(src, len, MPI_CHAR, dest, len, MPI_CHAR, MPI_COMM_WORLD);
  assert(err == MPI_SUCCESS);
}

static void gasnetc_bootstrapAlltoall(void *src, size_t len, void *dest) {
  int err;

  err = MPI_Alltoall(src, len, MPI_CHAR, dest, len, MPI_CHAR, MPI_COMM_WORLD);
  assert(err == MPI_SUCCESS);
}
#else
extern void gasnetc_bootstrapInit(int *argc, char ***argv);
extern void gasnetc_bootstrapFini(void);
extern void gasnetc_bootstrapConf(void);
extern void gasnetc_bootstrapBarrier(void);
extern void gasnetc_bootstrapAllgather(void *src, size_t len, void *dest);
extern void gasnetc_bootstrapAlltoall(void *src, size_t len, void *dest);
#endif

static VAPI_ret_t gasnetc_pin(void *addr, size_t size, VAPI_mrw_acl_t acl, gasnetc_regmem_t *reg) {
  VAPI_mr_t	req_mr;

  req_mr.type    = VAPI_MR;
  req_mr.start   = (uintptr_t)addr;
  req_mr.size    = size;
  req_mr.pd_hndl = gasnetc_pd;
  req_mr.acl     = acl;

  return VAPI_register_mr(gasnetc_hca, &req_mr, &reg->handle, &reg->props);
}

static void *gasnetc_alloc_pinned(size_t size, VAPI_mrw_acl_t acl, gasnetc_regmem_t *reg) {
  VAPI_ret_t vstat;
  void *addr;

  addr = gasneti_mmap(size);
  if (addr) {
    vstat = gasnetc_pin(addr, size, acl, reg);
    if (vstat != VAPI_OK) {
      gasneti_munmap(addr, size);
      addr = NULL;
    }
  }

  return addr;
}

static void gasnetc_snd_init(void) {
  VAPI_cqe_num_t	act_size;
  VAPI_ret_t		vstat;
  gasnetc_buffer_t	*buf;
  gasnetc_snd_desc_t	*desc;
  int 			count, i;

  count = MIN(GASNETC_CQ_SIZE, GASNETC_SND_WQE * gasnetc_nodes);

  buf = gasnetc_alloc_pinned(count * sizeof(gasnetc_buffer_t), 0, &gasnetc_snd_reg);
  assert(buf != NULL);

  desc = calloc(count, sizeof(gasnetc_snd_desc_t));
  assert(desc != NULL);

  gasnetc_snd_desc_pool = desc;
  for (i = 0; i < count; ++i, ++desc) {
    desc->sr_desc.id        = (uintptr_t)desc;		/* CQE will point back to this request */
    desc->sr_desc.comp_type = VAPI_SIGNALED;		/* XXX: is this correct? */
    desc->sr_desc.sg_lst_p  = desc->sr_sg;
    desc->sr_desc.set_se    = FALSE;			/* XXX: is this correct? */
    /* REST OF sr_desc SET AT SEND TIME */
    desc->next = desc + 1;
  }
  desc->next = NULL;

  vstat = VAPI_create_cq(gasnetc_hca, count, &gasnetc_snd_cq, &act_size);
  assert(vstat == VAPI_OK);
  assert(act_size >= count);
}

static gasnetc_rcv_desc_t *gasnetc_rcv_init(void) {
  VAPI_cqe_num_t	act_size;
  VAPI_ret_t		vstat;
  gasnetc_buffer_t	*buf;
  gasnetc_rcv_desc_t	*desc;
  int 			count, i;

  count = GASNETC_RCV_WQE * gasnetc_nodes;

  buf = gasnetc_alloc_pinned(count * sizeof(gasnetc_buffer_t),
			     VAPI_EN_LOCAL_WRITE, &gasnetc_rcv_reg);
  assert(buf != NULL);

  desc = calloc(count, sizeof(gasnetc_rcv_desc_t));
  assert(desc != NULL);

  for (i = 0; i < count; ++i) {
    desc[i].rr_desc.id         = (uintptr_t)&desc[i];	/* CQE will point back to this request */
    desc[i].rr_desc.opcode     = VAPI_RECEIVE;
    desc[i].rr_desc.comp_type  = VAPI_SIGNALED;	/* XXX: is this right? */
    desc[i].rr_desc.sg_lst_len = 1;
    desc[i].rr_desc.sg_lst_p   = &desc[i].rr_sg;
    desc[i].rr_sg.len          = GASNETC_BUFSZ;
    desc[i].rr_sg.addr         = (uintptr_t)&buf[i];
    desc[i].rr_sg.lkey         = gasnetc_rcv_reg.props.l_key;
  }

  vstat = VAPI_create_cq(gasnetc_hca, count, &gasnetc_rcv_cq, &act_size);
  assert(vstat == VAPI_OK);
  assert(act_size >= count);

  return desc;
}

GASNET_INLINE_MODIFIER(gasnetc_rcv_post)
int gasnetc_rcv_post(gasnetc_cep_t *cep, gasnetc_rcv_desc_t *desc) {
  return (VAPI_OK != VAPI_post_rr(gasnetc_hca, cep->qp_handle, &desc->rr_desc));
}

static int gasnetc_init(int *argc, char ***argv) {
  gasnetc_addr_t	*local_addr;
  gasnetc_addr_t	*remote_addr;
  VAPI_hca_cap_t	hca_cap;
  VAPI_hca_port_t	hca_port;
  VAPI_cqe_num_t	rcv_buf_count;
  VAPI_cqe_num_t	snd_buf_count;
  IB_port_t		port;
  VAPI_ret_t		vstat;
  int 			i, rc;
  gasnetc_rcv_desc_t	*rcv_desc_ptr;

  /*  check system sanity */
  gasnetc_check_config();

  if (gasnetc_init_done) 
    GASNETI_RETURN_ERRR(NOT_INIT, "GASNet already initialized");

  if (getenv("GASNET_FREEZE")) gasneti_freezeForDebugger();

  #if DEBUG_VERBOSE
    /* note - can't call trace macros during gasnet_init because trace system not yet initialized */
    fprintf(stderr,"gasnetc_init(): about to spawn...\n"); fflush(stderr);
  #endif

  /* Initialize the bootstrapping support */
  gasnetc_bootstrapInit(argc, argv);

  /* Determine number of nodes and my own node number */
  gasnetc_bootstrapConf();
    
  /* allocate resources */
  gasnetc_cep = calloc(gasnetc_nodes, sizeof(gasnetc_cep_t));
  assert(gasnetc_cep != NULL);
  local_addr = calloc(gasnetc_nodes, sizeof(gasnetc_addr_t));
  assert(local_addr != NULL);
  remote_addr = calloc(gasnetc_nodes, sizeof(gasnetc_addr_t));
  assert(remote_addr != NULL);

  /* open the hca and get port & lid values */
  /* XXX: should also check args/env for non-default HCA and port */
  {
    VAPI_hca_vendor_t hca_vendor;

    vstat = VAPI_open_hca(GASNETC_HCA_ID, &gasnetc_hca);
    if (vstat != VAPI_OK) {
      vstat = EVAPI_get_hca_hndl(GASNETC_HCA_ID, &gasnetc_hca);
    }
    assert(vstat == VAPI_OK && "Unable to open the HCA");

    vstat = VAPI_query_hca_cap(gasnetc_hca, &hca_vendor, &hca_cap);
    assert(vstat == VAPI_OK && "Unable to query HCA capabilities");

    for (port = 1; port <= hca_cap.phys_port_num; ++port) {
      vstat = VAPI_query_hca_port_prop(gasnetc_hca, port, &hca_port);
      assert(vstat == VAPI_OK);

      if (hca_port.state == PORT_ACTIVE) {
	break;
      }
    }

    assert(port <= hca_cap.phys_port_num && "No ACTIVE ports found");
  }

  /* check hca and port properties */
  assert(hca_cap.max_num_qp >= gasnetc_nodes);
  assert(hca_cap.max_qp_ous_wr >= GASNETC_SND_WQE);
  assert(hca_cap.max_qp_ous_wr >= GASNETC_RCV_WQE);
  assert(hca_cap.max_num_sg_ent >= GASNETC_SND_SG);
  assert(hca_cap.max_num_sg_ent >= GASNETC_RCV_SG);
  assert(hca_cap.max_num_sg_ent_rd >= 1);		/* RDMA Read support required */
  #if 1 /* QP end points */
    assert(hca_cap.max_qp_ous_rd_atom >= 1);		/* RDMA Read support required */
  #else
    assert(hca_cap.max_ee_ous_rd_atom >= 1);		/* RDMA Read support required */
  #endif
  assert(hca_cap.max_num_cq >= 2);
  assert(hca_cap.max_num_ent_cq >= GASNETC_CQ_SIZE);
  #if defined(GASNET_SEGMENT_FAST)
    assert(hca_cap.max_num_mr >= 3);			/* rcv bufs, snd bufs, segment */
  #else
    assert(hca_cap.max_num_mr >= (3+gasnetc_nodes));	/* rcv bufs, snd bufs, segment, n*fh */
  #endif



  /* get a pd for the QPs and memory registration */
  vstat =  VAPI_alloc_pd(gasnetc_hca, &gasnetc_pd);
  assert(vstat == VAPI_OK);

  /* allocate/initialize receiver resources */
  rcv_desc_ptr = gasnetc_rcv_init();
  assert(rcv_desc_ptr != NULL);
 
  /* allocate/initialize sender resources */
  gasnetc_snd_init();

  /* create all the endpoints */
  {
    VAPI_qp_init_attr_t	qp_init_attr;
    VAPI_qp_prop_t	qp_prop;

    qp_init_attr.cap.max_oust_wr_rq = GASNETC_RCV_WQE;
    qp_init_attr.cap.max_oust_wr_sq = GASNETC_SND_WQE;
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
      /* create the QP */
      vstat = VAPI_create_qp(gasnetc_hca, &qp_init_attr, &gasnetc_cep[i].qp_handle, &qp_prop);
      assert(vstat == VAPI_OK);
      assert(qp_prop.cap.max_oust_wr_rq >= GASNETC_RCV_WQE);
      assert(qp_prop.cap.max_oust_wr_sq >= GASNETC_SND_WQE);

      local_addr[i].lid = hca_port.lid;
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
    qp_attr.port                = port;
    qp_attr.remote_atomic_flags = VAPI_EN_REM_WRITE | VAPI_EN_REM_READ;
    for (i = 0; i < gasnetc_nodes; ++i) {
      vstat = VAPI_modify_qp(gasnetc_hca, gasnetc_cep[i].qp_handle, &qp_attr, &qp_mask, &qp_cap);
      assert(vstat == VAPI_OK);
	
      /* post 1st rcv descriptor */
      rc = gasnetc_rcv_post(&gasnetc_cep[i], rcv_desc_ptr++); 
      assert(rc == 0);
	
      /* post 2nd rcv descriptor */
      rc = gasnetc_rcv_post(&gasnetc_cep[i], rcv_desc_ptr++); 
      assert(rc == 0);
    }

    /* advance INIT -> RTR */
    QP_ATTR_MASK_CLR_ALL(qp_mask);
    QP_ATTR_MASK_SET(qp_mask, QP_ATTR_QP_STATE);
    QP_ATTR_MASK_SET(qp_mask, QP_ATTR_AV);
    QP_ATTR_MASK_SET(qp_mask, QP_ATTR_PATH_MTU);
    QP_ATTR_MASK_SET(qp_mask, QP_ATTR_RQ_PSN);
    QP_ATTR_MASK_SET(qp_mask, QP_ATTR_QP_OUS_RD_ATOM);
    QP_ATTR_MASK_SET(qp_mask, QP_ATTR_DEST_QP_NUM);
    QP_ATTR_MASK_SET(qp_mask,QP_ATTR_MIN_RNR_TIMER);
    qp_attr.qp_state         = VAPI_RTR;
    qp_attr.av.sl            = 0;
    qp_attr.av.grh_flag      = FALSE;
    qp_attr.av.static_rate   = 2;	/* XXX: 1x? */
    qp_attr.av.src_path_bits = 0;
    qp_attr.path_mtu         = hca_port.max_mtu;
    qp_attr.qp_ous_rd_atom   = 4;	/* XXX: get max from HCA */
    qp_attr.min_rnr_timer    = 0;
    for (i = 0; i < gasnetc_nodes; ++i) {
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
    qp_attr.timeout          = 0x20;
    qp_attr.retry_count      = 1;
    qp_attr.rnr_retry        = 1;
    qp_attr.ous_dst_rd_atom  = 4; 	/* XXX get max from HCA*/
    for (i = 0; i < gasnetc_nodes; ++i) {
      vstat = VAPI_modify_qp(gasnetc_hca, gasnetc_cep[i].qp_handle, &qp_attr, &qp_mask, &qp_cap);
      assert(vstat == VAPI_OK);
    }
  }

  free(remote_addr);
  free(local_addr);

  #if DEBUG_VERBOSE
    fprintf(stderr,"gasnetc_init(): spawn successful - node %i/%i starting...\n", 
      gasnetc_mynode, gasnetc_nodes); fflush(stderr);
  #endif

  #if defined(GASNET_SEGMENT_FAST)
    /* XXX: should also examine how much O/S will allow pinned? */
    gasneti_segmentInit(&gasnetc_MaxLocalSegmentSize,
                        &gasnetc_MaxGlobalSegmentSize,
                        (uintptr_t)hca_cap.max_mr_size,
                        gasnetc_nodes,
                        &gasnetc_bootstrapAllgather);
  #elif defined(GASNET_SEGMENT_LARGE)
    /* XXX: Should use max mmap size and pin in multiple ranges
	Currently just using max region size */
    gasneti_segmentInit(&gasnetc_MaxLocalSegmentSize,
                        &gasnetc_MaxGlobalSegmentSize,
                        (uintptr_t)hca_cap.max_mr_size,	/* XXX: should be -1, see note above */
                        gasnetc_nodes,
                        &gasnetc_bootstrapAllgather);
  #elif defined(GASNET_SEGMENT_EVERYTHING)
    gasnetc_MaxLocalSegmentSize =  (uintptr_t)-1;
    gasnetc_MaxGlobalSegmentSize = (uintptr_t)-1;
  #else
    #error Bad segment config
  #endif

  gasnetc_init_done = 1;  

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
    /* (###) add code here to register table[i].fnptr 
             on index (gasnet_handler_t)newindex */

    if (dontcare) table[i].index = newindex;
    (*numregistered)++;
  }
  return GASNET_OK;
}
/* ------------------------------------------------------------------------------------ */
extern int gasnetc_attach(gasnet_handlerentry_t *table, int numentries,
                          uintptr_t segsize, uintptr_t minheapoffset) {
  void *segbase = NULL;
  
  GASNETI_TRACE_PRINTF(C,("gasnetc_attach(table (%i entries), segsize=%i, minheapoffset=%i)",
                          numentries, (int)segsize, (int)minheapoffset));

  if (!gasnetc_init_done) 
    GASNETI_RETURN_ERRR(NOT_INIT, "GASNet attach called before init");
  if (gasnetc_attach_done) 
    GASNETI_RETURN_ERRR(NOT_INIT, "GASNet already attached");

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
    if (gasnetc_reghandlers(etable, len, 63, 127, 0, &numreg) != GASNET_OK)
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

  /* use gasneti_malloc_inhandler during bootstrapping because we can't assume the 
     hold/resume interrupts functions are operational yet */
  gasnetc_seginfo = (gasnet_seginfo_t *)gasneti_malloc_inhandler(gasnetc_nodes*sizeof(gasnet_seginfo_t));

  #if defined(GASNET_SEGMENT_FAST) || defined(GASNET_SEGMENT_LARGE)
    gasneti_segmentAttach(segsize, minheapoffset, gasnetc_seginfo, &gasnetc_bootstrapAllgather);
  #else /* GASNET_SEGMENT_EVERYTHING */
    { int i;
      for (i=0;i<gasnetc_nodes;i++) {
        gasnetc_seginfo[i].addr = (void *)0;
        gasnetc_seginfo[i].size = (uintptr_t)-1;
      }
    }
    /* (###) add any code here needed to setup GASNET_SEGMENT_EVERYTHING support */
  #endif
  segbase = gasnetc_seginfo[gasnetc_mynode].addr;
  segsize = gasnetc_seginfo[gasnetc_mynode].size;

  /* ------------------------------------------------------------------------------------ */
  /*  primary attach complete */
  gasnetc_attach_done = 1;
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
extern void gasnetc_exit(int exitcode) {
  /* XXX: should force termination and same exitcode from all nodes? */
  gasneti_trace_finish();

  gasnetc_bootstrapFini();

  exit(exitcode);	
  abort();
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
  memcpy(seginfo_table, gasnetc_seginfo, gasnetc_nodes*sizeof(gasnet_seginfo_t));
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

  /* (###) add code here to write the source index into sourceid */
  sourceid = 0 /* ### */;

  assert(sourceid < gasnetc_nodes);
  *srcindex = sourceid;
  return GASNET_OK;
}

extern int gasnetc_AMPoll() {
  int retval;
  GASNETC_CHECKATTACH();

  /* (###) add code here to run your AM progress engine */

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

    /* (###) add code here to read the arguments using va_arg(argptr, gasnet_handlerarg_t) 
             and send the active message 
     */

    retval = 0 /* ### */;
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
  GASNETI_TRACE_AMREQUESTMEDIUM(dest,handler,source_addr,nbytes,numargs);
  va_start(argptr, numargs); /*  pass in last argument */

    /* (###) add code here to read the arguments using va_arg(argptr, gasnet_handlerarg_t) 
             and send the active message 
     */

    retval = 0 /* ### */;
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
  if_pf (((uintptr_t)dest_addr) < ((uintptr_t)gasnetc_seginfo[dest].addr) ||
         ((uintptr_t)dest_addr) + nbytes > 
           ((uintptr_t)gasnetc_seginfo[dest].addr) + gasnetc_seginfo[dest].size) 
         GASNETI_RETURN_ERRR(BAD_ARG,"destination address out of segment range");

  GASNETI_TRACE_AMREQUESTLONG(dest,handler,source_addr,nbytes,dest_addr,numargs);
  va_start(argptr, numargs); /*  pass in last argument */

    /* (###) add code here to read the arguments using va_arg(argptr, gasnet_handlerarg_t) 
             and send the active message 
     */

    retval = 0 /* ### */;
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
  if_pf (((uintptr_t)dest_addr) < ((uintptr_t)gasnetc_seginfo[dest].addr) ||
         ((uintptr_t)dest_addr) + nbytes > 
           ((uintptr_t)gasnetc_seginfo[dest].addr) + gasnetc_seginfo[dest].size) 
         GASNETI_RETURN_ERRR(BAD_ARG,"destination address out of segment range");

  GASNETI_TRACE_AMREQUESTLONGASYNC(dest,handler,source_addr,nbytes,dest_addr,numargs);
  va_start(argptr, numargs); /*  pass in last argument */

    /* (###) add code here to read the arguments using va_arg(argptr, gasnet_handlerarg_t) 
             and send the active message 
     */

    retval = 0 /* ### */;
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

    /* (###) add code here to read the arguments using va_arg(argptr, gasnet_handlerarg_t) 
             and send the active message 
     */

    retval = 0 /* ### */;
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
  GASNETI_TRACE_AMREPLYMEDIUM(token,handler,source_addr,nbytes,numargs);
  va_start(argptr, numargs); /*  pass in last argument */

    /* (###) add code here to read the arguments using va_arg(argptr, gasnet_handlerarg_t) 
             and send the active message 
     */

    retval = 0 /* ### */;
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
  if_pf (((uintptr_t)dest_addr) < ((uintptr_t)gasnetc_seginfo[dest].addr) ||
         ((uintptr_t)dest_addr) + nbytes > 
           ((uintptr_t)gasnetc_seginfo[dest].addr) + gasnetc_seginfo[dest].size) 
         GASNETI_RETURN_ERRR(BAD_ARG,"destination address out of segment range");

  GASNETI_TRACE_AMREPLYLONG(token,handler,source_addr,nbytes,dest_addr,numargs);
  va_start(argptr, numargs); /*  pass in last argument */

    /* (###) add code here to read the arguments using va_arg(argptr, gasnet_handlerarg_t) 
             and send the active message 
     */

    retval = 0 /* ### */;
  va_end(argptr);
  GASNETI_RETURN(retval);
}

/* ------------------------------------------------------------------------------------ */
/*
  Handler-safe locks
  ==================
*/

extern void gasnetc_hsl_init(gasnet_hsl_t *hsl) {
  GASNETC_CHECKATTACH();

  { int retval = pthread_mutex_init(&(hsl->lock), NULL);
    if (retval) 
      gasneti_fatalerror("In gasnetc_hsl_init(), pthread_mutex_init()=%s",strerror(retval));
  }
}

extern void gasnetc_hsl_destroy(gasnet_hsl_t *hsl) {
  GASNETC_CHECKATTACH();

  { int retval = pthread_mutex_destroy(&(hsl->lock));
    if (retval) 
      gasneti_fatalerror("In gasnetc_hsl_destroy(), pthread_mutex_destroy()=%s",strerror(retval));
  }
}

extern void gasnetc_hsl_lock(gasnet_hsl_t *hsl) {
  GASNETC_CHECKATTACH();

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
  GASNETC_CHECKATTACH();

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

/* ------------------------------------------------------------------------------------ */

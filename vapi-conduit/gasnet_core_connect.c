/*   $Source: /Users/kamil/work/gasnet-cvs2/gasnet/vapi-conduit/Attic/gasnet_core_connect.c,v $
 *     $Date: 2011/03/13 04:08:01 $
 * $Revision: 1.44.2.14 $
 * Description: Connection management code
 * Copyright 2011, E. O. Lawrence Berekely National Laboratory
 * Terms of use are as specified in license.txt
 */

#include <gasnet_internal.h>
#include <gasnet_core_internal.h>

/* For open(), stat(), O_CREAT, etc.: */
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>

/*
  The following configuration cannot yet be overridden by environment variables.
*/
#if GASNET_CONDUIT_VAPI
  #define GASNETC_QP_PATH_MTU           MTU1024
  #define GASNETC_QP_MIN_RNR_TIMER      IB_RNR_NAK_TIMER_0_08
#else
  #define GASNETC_QP_PATH_MTU           IBV_MTU_1024
  #define GASNETC_QP_MIN_RNR_TIMER      6       /*IB_RNR_NAK_TIMER_0_08*/
#endif
#define GASNETC_QP_STATIC_RATE          0
#define GASNETC_QP_RNR_RETRY            7       /* retry forever, but almost never happens */

#define GASNETC_PSN(_sender, _qpi)  ((_sender)*gasnetc_alloc_qps + (_qpi))

/* Convenience iterators */
#define GASNETC_FOR_EACH_QPI(_conn_info, _qpi, _cep)  \
  for((_cep) = (_conn_info)->cep, (_qpi) = 0; \
      (_qpi) < gasnetc_alloc_qps; ++(_cep), ++(_qpi))

/* ------------------------------------------------------------------------------------ */
/* Global data */

gasnetc_qpn_t gasnetc_conn_qpn = 0;
gasnetc_qp_hndl_t gasnetc_conn_qp = GASNETC_IB_CHOOSE(VAPI_INVAL_HNDL, NULL);

gasneti_semaphore_t gasnetc_zero_sema = GASNETI_SEMAPHORE_INITIALIZER(0, 0);

/* ------------------------------------------------------------------------------------ */

/* Common types */
typedef GASNETC_IB_CHOOSE(VAPI_qp_attr_t,       struct ibv_qp_attr)     gasnetc_qp_attr_t;
typedef GASNETC_IB_CHOOSE(VAPI_qp_attr_mask_t,  enum ibv_qp_attr_mask)  gasnetc_qp_mask_t;

/* Info used for connection establishment */
typedef struct {
  gasnetc_cep_t     *cep;        /* Vector of gasnet endpoints */
  gasnetc_qpn_t     *local_qpn;  /* Local qpns of connections */
  gasnetc_qpn_t     *remote_qpn; /* Remote qpns of connections */
#if GASNETC_IBV_XRC
  gasnetc_qpn_t     *local_xrc_qpn;  /* Local qpns of XRC rcv qps */
  gasnetc_qpn_t     *remote_xrc_qpn; /* Remote qpns of XRC rcv qps */
  uint32_t          *xrc_remote_srq_num; /* Remote SRQ numbers */
#endif
  const gasnetc_port_info_t **port;
} gasnetc_conn_info_t;

/* Info exchanged for connection setup */
#if GASNETC_IBV_XRC
  typedef struct {
    uint32_t        srq_num;
    gasnetc_qpn_t   xrc_qpn;
    gasnetc_qpn_t   qpn;
  } gasnetc_xrc_conn_data_t;
#else
  /* Just exchange qpn.  So no struct required */
#endif

/* UD recv */
typedef struct {
  gasnetc_rcv_wr_t wr;
  gasnetc_sge_t sg;
} gasnetc_ud_rcv_desc_t;

#if GASNETC_IBV_XRC
  #define GASNETC_SND_QP_NEEDS_MODIFY(_xrc_snd_qp,_state) \
	(!gasnetc_use_xrc || ((_xrc_snd_qp).state < (_state)))
#else
  #define GASNETC_SND_QP_NEEDS_MODIFY(_xrc_snd_qp,_state) 1
#endif

static int gasnetc_connectfile_in_base  = 10; /* Defaults to human readable/writable */
static int gasnetc_connectfile_out_base = 36; /* Defaults to most compact */

/* ------------------------------------------------------------------------------------ */

static gasneti_lifo_head_t sq_sema_freelist = GASNETI_LIFO_INITIALIZER;

static void
sq_sema_alloc(int count)
{
  gasneti_semaphore_t *p = gasneti_malloc(count * sizeof(gasneti_semaphore_t));
  int i;

  for (i=0; i<count; ++i, ++p) {
    gasneti_lifo_push(&sq_sema_freelist,p);
  }
}

static gasneti_semaphore_t * 
sq_sema_get(void)
{
  gasneti_semaphore_t *result;
 
  result = gasneti_lifo_pop(&sq_sema_freelist);
  if_pf (NULL == result) {
    sq_sema_alloc(1);
    result = gasneti_lifo_pop(&sq_sema_freelist);
    gasneti_assert(NULL != result);
  }

  return result;
}

/* ------------------------------------------------------------------------------------ */
static const char *
gasnetc_parse_filename(const char *filename)
{
  /* replace any '%' with node num */
  if (strchr(filename,'%')) {
    char *tmpname = gasneti_strdup(filename); /* must not modify callers string */
    char *p = strchr(tmpname,'%');
    do {
      size_t len = strlen(tmpname) + 16;
      char *buf = gasneti_malloc(len);
      *p = '\0';
      snprintf(buf,len,"%s%i%s",tmpname,(int)gasnet_mynode(),p+1);
      gasneti_free(tmpname);
      tmpname = buf;
    } while (NULL != (p = strchr(tmpname,'%')));
    filename = tmpname;
  }
  return filename;
}
/* ------------------------------------------------------------------------------------ */

#if GASNETC_IBV_XRC
typedef struct gasnetc_xrc_snd_qp_s {
  gasnetc_qp_hndl_t handle;
  enum ibv_qp_state state;
  gasneti_semaphore_t *sq_sema_p;
} gasnetc_xrc_snd_qp_t;

static gasnetc_xrc_snd_qp_t *gasnetc_xrc_snd_qp = NULL;
#define GASNETC_NODE2SND_QP(_node) \
	(&gasnetc_xrc_snd_qp[gasneti_nodeinfo[(_node)] * gasnetc_alloc_qps])

static gasnetc_qpn_t *gasnetc_xrc_rcv_qpn = NULL;

/* Create one XRC rcv QP */
static int
gasnetc_xrc_create_qp(struct ibv_xrc_domain *xrc_domain, gasnet_node_t node, int qpi) {
  const int cep_idx = node * gasnetc_alloc_qps + qpi;
  gasneti_atomic32_t *rcv_qpn_p = (gasneti_atomic32_t *)(&gasnetc_xrc_rcv_qpn[cep_idx]);
  gasnetc_qpn_t rcv_qpn = 0;
  int ret;

  gasneti_assert(gasnetc_xrc_rcv_qpn != NULL);
  gasneti_assert(sizeof(gasnetc_qpn_t) == sizeof(gasneti_atomic32_t));

  /* Create the RCV QPs once per supernode and register in the non-creating node(s) */
  /* QPN==1 is reserved, so we can use it as a "lock" value */
  if (gasneti_atomic32_compare_and_swap(rcv_qpn_p, 0, 1, 0)) {
    struct ibv_qp_init_attr init_attr;
    init_attr.xrc_domain = xrc_domain;
    ret = ibv_create_xrc_rcv_qp(&init_attr, &rcv_qpn);
    GASNETC_VAPI_CHECK(ret, "from ibv_create_xrc_rcv_qp()");

    gasneti_atomic32_set(rcv_qpn_p, rcv_qpn, 0);
  } else {
    gasneti_waituntil(1 != (rcv_qpn = gasneti_atomic32_read(rcv_qpn_p, 0)));
    ret = ibv_reg_xrc_rcv_qp(xrc_domain, rcv_qpn);
    GASNETC_VAPI_CHECK(ret, "from ibv_reg_xrc_rcv_qp()");
  }

  return GASNET_OK;
}

static int
gasnetc_xrc_modify_qp(
        struct ibv_xrc_domain *xrc_domain,
        gasnetc_qpn_t xrc_qp_num,
        struct ibv_qp_attr *attr,
        enum ibv_qp_attr_mask attr_mask)
{
  int retval;

  retval = ibv_modify_xrc_rcv_qp(xrc_domain, xrc_qp_num, attr, attr_mask);

  if_pf (retval == EINVAL) {
    struct ibv_qp_attr      qp_attr;
    struct ibv_qp_init_attr qp_init_attr;
    int rc;

    rc = ibv_query_xrc_rcv_qp(xrc_domain, xrc_qp_num, &qp_attr, IBV_QP_STATE, &qp_init_attr);
    if (!rc && (qp_attr.qp_state >= attr->qp_state)) {
      /* No actual error, just a race against another process */
      retval = 0;
    }
  }

  return retval;
}

/* Perform a supernode-scoped broadcast from first node of the supernode.
   Note that 'src' must be a valid address on ALL callers.
 */
static void
gasnetc_supernode_bcast(void *src, size_t len, void *dst) {
  #if GASNET_PSHM
    /* Can do w/ just supernode-scoped broadcast */
    gasneti_assert(gasneti_request_pshmnet != NULL);
    gasneti_pshmnet_bootstrapBroadcast(gasneti_request_pshmnet, src, len, dst, 0);
  #else
    /* Need global Exchange when PSHM is not available */
    /* TODO: If/when is one Bcast per supernode cheaper than 1 Exchange? */
    /* TODO: Push this case down to the Bootstrap support? */
    void *all_data = gasneti_malloc(gasneti_nodes * len);
    void *my_dst = (void *)((uintptr_t )all_data + (len * gasneti_nodemap_local[0]));
    gasneti_bootstrapExchange(src, len, all_data);
    memcpy(dst, my_dst, len);
    gasneti_free(all_data);
  #endif
}

static void
gasnetc_supernode_barrier(void) {
  #if GASNET_PSHM
    gasneti_pshmnet_bootstrapBarrier();
  #else
    gasneti_bootstrapBarrier();
  #endif
}

/* XXX: Requires that at least the first call is collective */
static char*
gasnetc_xrc_tmpname(gasnetc_lid_t mylid, int index) {
  static char *tmpdir = NULL;
  static int tmpdir_len = -1;
  static pid_t pid;
  static const char pattern[] = "/GASNETxrc-%04x%01x-%06x"; /* Max 11 + 5 + 1 + 6 + 1 = 24 */
  const int filename_len = 24;
  char *filename;

  gasneti_assert(index >= 0  &&  index <= 16);

  /* Initialize tmpdir and pid only on first call */
  if (!tmpdir) {
    struct stat s;
    tmpdir = gasneti_getenv_withdefault("TMPDIR", "/tmp");
    if (stat(tmpdir, &s) || !S_ISDIR(s.st_mode)) {
      gasneti_fatalerror("XRC support requires valid $TMPDIR or /tmp");
    }
    tmpdir_len = strlen(tmpdir);

    /* Get PID of first proc per supernode */
    pid = getpid(); /* Redundant, but harmless on other processes */
    gasnetc_supernode_bcast(&pid, sizeof(pid), &pid);
  }

  filename = gasneti_malloc(tmpdir_len + filename_len);
  strcpy(filename, tmpdir);
  sprintf(filename + tmpdir_len, pattern,
          (unsigned int)(mylid & 0xffff),
          (unsigned int)(index & 0xf),
          (unsigned int)(pid & 0xffffff));
  gasneti_assert(strlen(filename) < (tmpdir_len + filename_len));

  return filename;
}

/* Create an XRC domain per HCA (once per supernode) and a shared memory file */
/* XXX: Requires that the call is collective */
static int
gasnetc_xrc_init(void) {
  const gasnetc_lid_t mylid = gasnetc_port_tbl[0].port.lid;
  char *filename[GASNETC_IB_MAX_HCAS+1];
  size_t flen;
  int index, fd;

  /* Use per-supernode filename to create common XRC domain once per HCA */
  GASNETC_FOR_ALL_HCA_INDEX(index) {
    gasnetc_hca_t *hca = &gasnetc_hca[index];
    filename[index] = gasnetc_xrc_tmpname(mylid, index);
    fd = open(filename[index], O_CREAT, S_IWUSR|S_IRUSR);
    if (fd < 0) {
      gasneti_fatalerror("failed to create xrc domain file '%s': %d:%s", filename[index], errno, strerror(errno));
    }
    hca->xrc_domain = ibv_open_xrc_domain(hca->handle, fd, O_CREAT);
    GASNETC_VAPI_CHECK_PTR(hca->xrc_domain, "from ibv_open_xrc_domain()");
    (void) close(fd);
  }

  /* Use one more per-supernode filename to create common shared memory file */
  /* TODO: Should PSHM combine this w/ the AM segment? */
  gasneti_assert(index == gasnetc_num_hcas);
  filename[index] = gasnetc_xrc_tmpname(mylid, index);
  fd = open(filename[index], O_CREAT|O_RDWR, S_IWUSR|S_IRUSR);
  if (fd < 0) {
    gasneti_fatalerror("failed to create xrc shared memory file '%s': %d:%s", filename[index], errno, strerror(errno));
  }
  flen = GASNETI_PAGE_ALIGNUP(sizeof(gasnetc_qpn_t) * gasneti_nodes * gasnetc_alloc_qps);
  if (ftruncate(fd, flen) < 0) {
    gasneti_fatalerror("failed to resize xrc shared memory file '%s': %d:%s", filename[index], errno, strerror(errno));
  }
  /* XXX: Is there anything else that can/should be packed into the same shared memory file? */
  gasnetc_xrc_rcv_qpn = mmap(NULL, flen, PROT_READ|PROT_WRITE, MAP_SHARED, fd, 0);
  if (gasnetc_xrc_rcv_qpn == MAP_FAILED) {
    gasneti_fatalerror("failed to mmap xrc shared memory file '%s': %d:%s", filename[index], errno, strerror(errno));
  }
  (void) close(fd);

  /* Clean up once everyone is done w/ all files */
  gasnetc_supernode_barrier();
  GASNETC_FOR_ALL_HCA_INDEX(index) {
    (void)unlink(filename[index]); gasneti_free(filename[index]);
  }
  gasneti_assert(index == gasnetc_num_hcas);
  (void)unlink(filename[index]); gasneti_free(filename[index]);

  /* Allocate SND QP table */
  gasnetc_xrc_snd_qp = gasneti_calloc(gasneti_nodemap_global_count * gasnetc_alloc_qps,
                                      sizeof(gasnetc_xrc_snd_qp_t));

  return GASNET_OK;
}
#endif /* GASNETC_IBV_XRC */

/* Distribute the qps to each peer round-robin over the ports.
   Returns NULL for cases that should not have any connection */
static const gasnetc_port_info_t *
gasnetc_select_port(gasnet_node_t node, int qpi) {
    if (gasnetc_non_ib(node)) {
      return NULL;
    }
    if (GASNETC_QPI_IS_REQ(qpi)) {
      /* Second half of table (if any) duplicates first half. */
      qpi -= gasnetc_num_qps;
    }
    
    /* XXX: At some point we lost any attempt at true "round-robin"
     * that could have balanced port use when (num_qps % num_ports) != 0.
     * The problem there was that getting a distribution that both ends
     * agreed to was a big mess.  Perhaps we bring that idea back later.
     * However, for now the same sequence of gasnetc_num_qps values is
     * repeated for each node.
     * XXX: If this changes, gasnetc_sndrcv_limits() must change to match.
     */
    return  &gasnetc_port_tbl[qpi % gasnetc_num_ports];
}

/* Setup ports array for one node */
static int
gasnetc_setup_ports(gasnet_node_t node, gasnetc_conn_info_t *conn_info)
{
  static const gasnetc_port_info_t **ports = NULL;

  if_pf (!ports) {
    /* Distribute the qps over the ports, same for each node. */
    int qpi;
    ports = gasneti_malloc(gasnetc_alloc_qps * sizeof(gasnetc_port_info_t *));
    for (qpi = 0; qpi < gasnetc_num_qps; ++qpi) {
      ports[qpi] = gasnetc_select_port(node, qpi);
    #if GASNETC_IBV_SRQ
      if (gasnetc_use_srq) {
        /* Second half of table (if any) duplicates first half.
           This might NOT be the same as extending the loop */
        ports[qpi + gasnetc_num_qps] = ports[qpi];
      }
    #endif
    }
  }

  conn_info->port = ports;
  return GASNET_OK;
}

/* Create endpoint(s) for a given peer
 * Outputs the qpn values in the array provided.
 */
static int
gasnetc_qp_create(gasnet_node_t node, gasnetc_conn_info_t *conn_info)
{
    gasnetc_cep_t *cep;
    int qpi;

#if GASNET_CONDUIT_VAPI
    VAPI_qp_init_attr_t qp_init_attr;
    VAPI_qp_prop_t      qp_prop;
    int rc;

    qp_init_attr.cap.max_oust_wr_rq = gasnetc_am_oust_pp * 2;
    qp_init_attr.cap.max_oust_wr_sq = gasnetc_op_oust_pp;
    qp_init_attr.cap.max_sg_size_rq = 1;
    qp_init_attr.cap.max_sg_size_sq = GASNETC_SND_SG;
    qp_init_attr.rdd_hndl           = 0;
    qp_init_attr.rq_sig_type        = VAPI_SIGNAL_REQ_WR;
    qp_init_attr.sq_sig_type        = VAPI_SIGNAL_REQ_WR;
    qp_init_attr.ts_type            = VAPI_TS_RC;

    GASNETC_FOR_EACH_QPI(conn_info, qpi, cep) {
      const gasnetc_port_info_t *port = conn_info->port[qpi];
      gasnetc_hca_t *hca = &gasnetc_hca[port->hca_index];

      cep->hca = hca;
      cep->hca_handle = hca->handle;
    #if GASNETC_IB_MAX_HCAS > 1
      cep->hca_index = hca->hca_index;
    #endif
      cep->sq_sema_p = &gasnetc_zero_sema;

      qp_init_attr.pd_hndl         = hca->pd;
      qp_init_attr.rq_cq_hndl      = hca->rcv_cq;
      qp_init_attr.sq_cq_hndl      = hca->snd_cq;
      rc = VAPI_create_qp(hca->handle, &qp_init_attr, &cep->qp_handle, &qp_prop);
      GASNETC_VAPI_CHECK(rc, "from VAPI_create_qp()");
      gasneti_assert(qp_prop.cap.max_oust_wr_rq >= gasnetc_am_oust_pp * 2);
      gasneti_assert(qp_prop.cap.max_oust_wr_sq >= gasnetc_op_oust_pp);

      conn_info->local_qpn[qpi] = qp_prop.qp_num;
    }
#else
    struct ibv_qp_init_attr     qp_init_attr;
    const int                   max_recv_wr = gasnetc_use_srq ? 0 : gasnetc_am_oust_pp * 2;
    int                         max_send_wr = gasnetc_op_oust_pp;
  #if GASNETC_IBV_XRC
    gasnetc_xrc_snd_qp_t       *xrc_snd_qp = GASNETC_NODE2SND_QP(node);
  #endif

    qp_init_attr.cap.max_send_wr     = max_send_wr;
    qp_init_attr.cap.max_recv_wr     = max_recv_wr;
    qp_init_attr.cap.max_send_sge    = GASNETC_SND_SG;
    qp_init_attr.cap.max_recv_sge    = 1;
    qp_init_attr.qp_context          = NULL; /* XXX: Can/should we use this? */
  #if GASNETC_IBV_XRC
    qp_init_attr.qp_type             = gasnetc_use_xrc ? IBV_QPT_XRC : IBV_QPT_RC;
  #else
    qp_init_attr.qp_type             = IBV_QPT_RC;
  #endif
    qp_init_attr.sq_sig_all          = 1; /* XXX: Unless we drop 1-to-1 WQE/CQE relationship */
    qp_init_attr.srq                 = NULL;

    GASNETC_FOR_EACH_QPI(conn_info, qpi, cep) {
      gasnetc_qp_hndl_t hndl;
      const gasnetc_port_info_t *port = conn_info->port[qpi];
      gasnetc_hca_t *hca = &gasnetc_hca[port->hca_index];

      cep->hca = hca;
    #if GASNETC_IB_MAX_HCAS > 1
      cep->hca_index = hca->hca_index;
    #endif
      cep->sq_sema_p = &gasnetc_zero_sema;

      qp_init_attr.send_cq         = hca->snd_cq;
      qp_init_attr.recv_cq         = hca->rcv_cq;

    #if GASNETC_IBV_SRQ
      if (gasnetc_use_srq) {
        if (GASNETC_QPI_IS_REQ(qpi)) {
          qp_init_attr.srq = hca->rqst_srq;
          qp_init_attr.cap.max_send_wr = gasnetc_am_oust_pp;
          qp_init_attr.cap.max_send_sge = 1; /* only AMs on this QP */
        } else {
          qp_init_attr.srq = hca->repl_srq;
          qp_init_attr.cap.max_send_wr = gasnetc_op_oust_pp;
          qp_init_attr.cap.max_send_sge = GASNETC_SND_SG;
        }
        cep->srq = qp_init_attr.srq;
        max_send_wr = qp_init_attr.cap.max_send_wr;
      }
    #endif
    #if GASNETC_IBV_XRC
      if (gasnetc_use_xrc) {
        gasnetc_xrc_create_qp(hca->xrc_domain, node, qpi);

        hndl = xrc_snd_qp[qpi].handle;
        if (hndl) {
            /* per-supernode QP was already created - just reference it */
            goto finish;
        }
  
        qp_init_attr.xrc_domain = hca->xrc_domain;
        qp_init_attr.srq        = NULL;
      }
    #endif
  
      while (1) { /* No query for max_inline_data limit */
        qp_init_attr.cap.max_inline_data = gasnetc_inline_limit;
        hndl = ibv_create_qp(hca->pd, &qp_init_attr);
        if (hndl != NULL) break;
        if (qp_init_attr.cap.max_inline_data == -1) {
          /* Automatic max not working, fall back on manual search */
          gasnetc_inline_limit = 1024;
          continue;
        }
        if ((errno != EINVAL) || (gasnetc_inline_limit == 0)) {
          GASNETC_VAPI_CHECK_PTR(hndl, "from ibv_create_qp()");
          /* NOT REACHED */
        }
        gasnetc_inline_limit = MIN(1024, gasnetc_inline_limit - 1);
        /* Try again */
      }
      gasneti_assert(qp_init_attr.cap.max_recv_wr >= max_recv_wr);
      gasneti_assert(qp_init_attr.cap.max_send_wr >= max_send_wr);

    #if GASNETC_IBV_XRC
      if (gasnetc_use_xrc) {
        xrc_snd_qp[qpi].handle = hndl;
        xrc_snd_qp[qpi].state = IBV_QPS_RESET;
      }

    finish:
      cep->rcv_qpn = gasnetc_use_xrc ? conn_info->local_xrc_qpn[qpi] : hndl->qp_num;
    #elif GASNETC_IBV_SRQ
      cep->rcv_qpn = hndl->qp_num;
    #endif

      cep->qp_handle = hndl;
      conn_info->local_qpn[qpi] = hndl->qp_num;
    }
#endif

    return GASNET_OK;
} /* create */

/* Advance QP state from RESET to INIT */
static int
gasnetc_qp_reset2init(gasnet_node_t node, gasnetc_conn_info_t *conn_info)
{
    gasnetc_qp_attr_t qp_attr;
    gasnetc_qp_mask_t qp_mask;
    gasnetc_cep_t *cep;
    int qpi;
    int rc;

#if GASNET_CONDUIT_VAPI
    VAPI_qp_cap_t       qp_cap;

    QP_ATTR_MASK_CLR_ALL(qp_mask);
    QP_ATTR_MASK_SET(qp_mask, QP_ATTR_QP_STATE);
    QP_ATTR_MASK_SET(qp_mask, QP_ATTR_PKEY_IX);
    QP_ATTR_MASK_SET(qp_mask, QP_ATTR_PORT);
    QP_ATTR_MASK_SET(qp_mask, QP_ATTR_REMOTE_ATOMIC_FLAGS);
    qp_attr.qp_state            = VAPI_INIT;
    qp_attr.pkey_ix             = 0;
    qp_attr.remote_atomic_flags = VAPI_EN_REM_WRITE | VAPI_EN_REM_READ;

    GASNETC_FOR_EACH_QPI(conn_info, qpi, cep) {
      const gasnetc_port_info_t *port = conn_info->port[qpi];

      qp_attr.port = port->port_num;
      rc = VAPI_modify_qp(cep->hca_handle, cep->qp_handle, &qp_attr, &qp_mask, &qp_cap);
      GASNETC_VAPI_CHECK(rc, "from VAPI_modify_qp(INIT)");
    }
#else
  #if GASNETC_IBV_XRC
    gasnetc_xrc_snd_qp_t *xrc_snd_qp = GASNETC_NODE2SND_QP(node);
  #endif

    qp_mask = (enum ibv_qp_attr_mask)(IBV_QP_STATE | IBV_QP_PKEY_INDEX | IBV_QP_PORT | IBV_QP_ACCESS_FLAGS);
    qp_attr.qp_state        = IBV_QPS_INIT;
    qp_attr.pkey_index      = 0;
    qp_attr.qp_access_flags = IBV_ACCESS_REMOTE_WRITE | IBV_ACCESS_REMOTE_READ;

    GASNETC_FOR_EACH_QPI(conn_info, qpi, cep) {
      const gasnetc_port_info_t *port = conn_info->port[qpi];

    #if GASNETC_IBV_SRQ
      qp_attr.qp_access_flags = GASNETC_QPI_IS_REQ(qpi)
                                    ? IBV_ACCESS_REMOTE_WRITE
                                    : IBV_ACCESS_REMOTE_WRITE | IBV_ACCESS_REMOTE_READ;
    #endif
      qp_attr.port_num = port->port_num;

    #if GASNETC_IBV_XRC
      if (gasnetc_use_xrc) {
        rc = gasnetc_xrc_modify_qp(cep->hca->xrc_domain, conn_info->local_xrc_qpn[qpi], &qp_attr, qp_mask);
        GASNETC_VAPI_CHECK(rc, "from gasnetc_xrc_modify_qp(INIT)");
      }
    #endif

      if (GASNETC_SND_QP_NEEDS_MODIFY(xrc_snd_qp[qpi], IBV_QPS_INIT)) {
        rc = ibv_modify_qp(cep->qp_handle, &qp_attr, qp_mask);
        GASNETC_VAPI_CHECK(rc, "from ibv_modify_qp(INIT)");
      #if GASNETC_IBV_XRC
        if (gasnetc_use_xrc) xrc_snd_qp[qpi].state = IBV_QPS_INIT;
      #endif
      }
    }
#endif

    return GASNET_OK;
} /* reset2init */

/* Advance QP state from INIT to RTR */
static int
gasnetc_qp_init2rtr(gasnet_node_t node, gasnetc_conn_info_t *conn_info)
{
    gasnetc_qp_attr_t qp_attr;
    gasnetc_qp_mask_t qp_mask;
    gasnetc_cep_t *cep;
    int qpi;
    int rc;

#if GASNET_CONDUIT_VAPI
    VAPI_qp_cap_t       qp_cap;

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
    qp_attr.av.grh_flag      = 0;
    qp_attr.av.static_rate   = GASNETC_QP_STATIC_RATE;
    qp_attr.av.src_path_bits = 0;
    qp_attr.min_rnr_timer    = GASNETC_QP_MIN_RNR_TIMER;

    GASNETC_FOR_EACH_QPI(conn_info, qpi, cep) {
      const gasnetc_port_info_t *port = conn_info->port[qpi];

      qp_attr.qp_ous_rd_atom = port->rd_atom;
      qp_attr.path_mtu       = MIN(GASNETC_QP_PATH_MTU, port->port.max_mtu);
      qp_attr.rq_psn         = GASNETC_PSN(node, qpi);
      qp_attr.av.dlid        = port->remote_lids[node];
      qp_attr.dest_qp_num    = conn_info->remote_qpn[qpi];
      rc = VAPI_modify_qp(cep->hca_handle, cep->qp_handle, &qp_attr, &qp_mask, &qp_cap);
      GASNETC_VAPI_CHECK(rc, "from VAPI_modify_qp(RTR)");
    }
#else
  #if GASNETC_IBV_XRC
    gasnetc_xrc_snd_qp_t *xrc_snd_qp = GASNETC_NODE2SND_QP(node);
  #endif

    qp_mask = (enum ibv_qp_attr_mask)(IBV_QP_STATE | IBV_QP_AV | IBV_QP_PATH_MTU | IBV_QP_RQ_PSN | IBV_QP_MAX_DEST_RD_ATOMIC | IBV_QP_DEST_QPN | IBV_QP_MIN_RNR_TIMER);
    qp_attr.qp_state         = IBV_QPS_RTR;
    qp_attr.ah_attr.sl            = 0;
    qp_attr.ah_attr.is_global     = 0;
    qp_attr.ah_attr.static_rate   = GASNETC_QP_STATIC_RATE;
    qp_attr.ah_attr.src_path_bits = 0;

    qp_attr.min_rnr_timer    = GASNETC_QP_MIN_RNR_TIMER;

    GASNETC_FOR_EACH_QPI(conn_info, qpi, cep) {
      const gasnetc_port_info_t *port = conn_info->port[qpi];

      qp_attr.max_dest_rd_atomic = GASNETC_QPI_IS_REQ(qpi) ? 0 : port->rd_atom;
      qp_attr.path_mtu           = MIN(GASNETC_QP_PATH_MTU, port->port.max_mtu);
      qp_attr.rq_psn             = GASNETC_PSN(node, qpi);
      qp_attr.ah_attr.dlid       = port->remote_lids[node];
      qp_attr.ah_attr.port_num   = port->port_num;
      qp_attr.dest_qp_num        = conn_info->remote_qpn[qpi];

    #if GASNETC_IBV_XRC
      if (gasnetc_use_xrc) {
        rc = gasnetc_xrc_modify_qp(cep->hca->xrc_domain, cep->rcv_qpn, &qp_attr, qp_mask);
        GASNETC_VAPI_CHECK(rc, "from gasnetc_xrc_modify_qp(RTR)");

        /* The normal QP will connect, below, to the peer's XRC rcv QP */
        qp_attr.dest_qp_num = conn_info->remote_xrc_qpn[qpi];
      }
    #endif

      if (GASNETC_SND_QP_NEEDS_MODIFY(xrc_snd_qp[qpi], IBV_QPS_RTR)) {
        rc = ibv_modify_qp(cep->qp_handle, &qp_attr, qp_mask);
        GASNETC_VAPI_CHECK(rc, "from ibv_modify_qp(RTR)");
      #if GASNETC_IBV_XRC
        if (gasnetc_use_xrc) xrc_snd_qp[qpi].state = IBV_QPS_RTR;
      #endif
      }
    }
#endif

    return GASNET_OK;
} /* init2rtr */

/* Advance QP state from RTR to RTS */
static int
gasnetc_qp_rtr2rts(gasnet_node_t node, gasnetc_conn_info_t *conn_info)
{
    gasnetc_qp_attr_t qp_attr;
    gasnetc_qp_mask_t qp_mask;
    gasnetc_cep_t *cep;
    int qpi;
    int rc;

#if GASNET_CONDUIT_VAPI
    VAPI_qp_cap_t       qp_cap;

    QP_ATTR_MASK_CLR_ALL(qp_mask);
    QP_ATTR_MASK_SET(qp_mask, QP_ATTR_QP_STATE);
    QP_ATTR_MASK_SET(qp_mask, QP_ATTR_SQ_PSN);
    QP_ATTR_MASK_SET(qp_mask, QP_ATTR_TIMEOUT);
    QP_ATTR_MASK_SET(qp_mask, QP_ATTR_RETRY_COUNT);
    QP_ATTR_MASK_SET(qp_mask, QP_ATTR_RNR_RETRY);
    QP_ATTR_MASK_SET(qp_mask, QP_ATTR_OUS_DST_RD_ATOM);
    qp_attr.qp_state         = VAPI_RTS;
    qp_attr.timeout          = gasnetc_qp_timeout;
    qp_attr.retry_count      = gasnetc_qp_retry_count;
    qp_attr.rnr_retry        = GASNETC_QP_RNR_RETRY;

    GASNETC_FOR_EACH_QPI(conn_info, qpi, cep) {
      const gasnetc_port_info_t *port = conn_info->port[qpi];
      gasneti_semaphore_t *sq_sema_p = sq_sema_get();

      qp_attr.sq_psn           = GASNETC_PSN(gasneti_mynode, qpi);
      qp_attr.ous_dst_rd_atom  = port->rd_atom;
      rc = VAPI_modify_qp(cep->hca_handle, cep->qp_handle, &qp_attr, &qp_mask, &qp_cap);
      GASNETC_VAPI_CHECK(rc, "from VAPI_modify_qp(RTS)");
      gasnetc_inline_limit = MIN(gasnetc_inline_limit, qp_cap.max_inline_data_sq);

      /* XXX: When could/should we use the *allocated* length? */
      gasneti_semaphore_init(sq_sema_p, gasnetc_op_oust_pp, gasnetc_op_oust_pp);
      gasneti_sync_writes();
      cep->sq_sema_p = sq_sema_p;
    }
#else
  #if GASNETC_IBV_XRC
    gasnetc_xrc_snd_qp_t *xrc_snd_qp = GASNETC_NODE2SND_QP(node);
  #endif

    qp_mask = (enum ibv_qp_attr_mask)(IBV_QP_STATE | IBV_QP_SQ_PSN | IBV_QP_TIMEOUT | IBV_QP_RETRY_CNT | IBV_QP_RNR_RETRY | IBV_QP_MAX_QP_RD_ATOMIC);
    qp_attr.qp_state         = IBV_QPS_RTS;
    qp_attr.timeout          = gasnetc_qp_timeout;
    qp_attr.retry_cnt        = gasnetc_qp_retry_count;
    qp_attr.rnr_retry        = GASNETC_QP_RNR_RETRY;

    GASNETC_FOR_EACH_QPI(conn_info, qpi, cep) {
      gasneti_semaphore_t *sq_sema_p;

    #if GASNETC_IBV_XRC
      if (gasnetc_use_xrc) {
        cep->xrc_remote_srq_num = conn_info->xrc_remote_srq_num[qpi];
        if (NULL == xrc_snd_qp[qpi].sq_sema_p) {
          xrc_snd_qp[qpi].sq_sema_p = sq_sema_get();
        }
        sq_sema_p = xrc_snd_qp[qpi].sq_sema_p;
      } else
    #endif
      {
        sq_sema_p = sq_sema_get();
      }

      if (GASNETC_SND_QP_NEEDS_MODIFY(xrc_snd_qp[qpi], IBV_QPS_RTS)) {
        const gasnetc_port_info_t *port = conn_info->port[qpi];

        qp_attr.sq_psn           = GASNETC_PSN(gasneti_mynode, qpi);
        qp_attr.max_rd_atomic    = GASNETC_QPI_IS_REQ(qpi) ? 0 : port->rd_atom;
        rc = ibv_modify_qp(cep->qp_handle, &qp_attr, qp_mask);
        GASNETC_VAPI_CHECK(rc, "from ibv_modify_qp(RTS)");
      #if GASNETC_IBV_XRC
        if (gasnetc_use_xrc) xrc_snd_qp[qpi].state = IBV_QPS_RTS;
      #endif

        {
          struct ibv_qp_attr qp_attr2;
          struct ibv_qp_init_attr qp_init_attr;
          rc = ibv_query_qp(cep->qp_handle, &qp_attr2, IBV_QP_CAP, &qp_init_attr);
          GASNETC_VAPI_CHECK(rc, "from ibv_query_qp(RTS)");
          gasnetc_inline_limit = MIN(gasnetc_inline_limit, qp_attr2.cap.max_inline_data);
        }

        {
          int max_send_wr = (gasnetc_use_srq && GASNETC_QPI_IS_REQ(qpi))
                              ? gasnetc_am_oust_pp : gasnetc_op_oust_pp;

          /* XXX: When could/should we use the *allocated* length? */
          gasneti_semaphore_init(sq_sema_p, max_send_wr, max_send_wr);
        }
      }

      gasneti_sync_writes();
      cep->sq_sema_p = sq_sema_p;
    }
#endif

    return GASNET_OK;
} /* rtr2rts */


/* Post a work request to the receive queue of the UD QP */
/* XXX: much is in-flux w.r.t. UD */
static void
gasnetc_rcv_post_ud(gasnetc_rcv_wr_t *wr) {
  int vstat;

#if GASNET_CONDUIT_VAPI
  /* Not yet implemented */
  vstat = VAPI_post_rr(gasnetc_hca[0].handle, gasnetc_conn_qp, wr);
#else
  {
    gasnetc_rcv_wr_t *bad_wr;
    vstat = ibv_post_recv(gasnetc_conn_qp, wr, &bad_wr);
  }
#endif

  GASNETC_VAPI_CHECK(vstat, "while posting a UD receive work request");
}

/* Create UD QP and advance all the way to RTS
   Return QP handl on success, or zero on failure. */
/* XXX: still much in-flux w.r.t. UD */
static int
gasnetc_qp_setup_ud(gasnetc_port_info_t *port)
{
    gasnetc_hca_t *hca = &gasnetc_hca[port->hca_index];
    gasnetc_qp_attr_t qp_attr;
    gasnetc_qp_mask_t qp_mask;
    int rc;

    /* TODO: tune these?  honor env vars? */
    const int max_recv_wr = 4;
    const int max_send_wr = 4;

    /* TODO: somehow compute the actual size - but max_regs is not known until attach */
    const int recv_sz = 128;

    /* CREATE */
#if GASNET_CONDUIT_VAPI
    /* XXX: Not yet */
#else
  {
    struct ibv_qp_init_attr     qp_init_attr;

    qp_init_attr.cap.max_send_wr     = max_send_wr;
    qp_init_attr.cap.max_recv_wr     = max_recv_wr;
    qp_init_attr.cap.max_send_sge    = 1;
    qp_init_attr.cap.max_recv_sge    = 1;
    qp_init_attr.qp_context          = NULL; /* XXX: Can/should we use this? */
    qp_init_attr.qp_type             = IBV_QPT_UD;
    qp_init_attr.sq_sig_all          = 1; /* XXX: Unless we drop 1-to-1 WQE/CQE relationship */
    qp_init_attr.srq                 = NULL;
    qp_init_attr.send_cq             = hca->snd_cq;
    qp_init_attr.recv_cq             = hca->rcv_cq;
    qp_init_attr.cap.max_inline_data = 0; /* XXX: Really should consider using to avoid registration */

    gasnetc_conn_qp = ibv_create_qp(hca->pd, &qp_init_attr);
    if_pf (NULL == gasnetc_conn_qp) return GASNET_ERR_RESOURCE;
    gasnetc_conn_qpn = gasnetc_conn_qp->qp_num;
  }
#endif

#if GASNET_CONDUIT_VAPI
    /* XXX: Not yet */
#else
    /* RESET -> INIT */
    qp_mask = (enum ibv_qp_attr_mask)(IBV_QP_STATE | IBV_QP_PKEY_INDEX | IBV_QP_PORT | IBV_QP_QKEY);
    qp_attr.qp_state        = IBV_QPS_INIT;
    qp_attr.pkey_index      = 0;
    qp_attr.qkey            = 0;
    qp_attr.port_num        = port->port_num;
    rc = ibv_modify_qp(gasnetc_conn_qp, &qp_attr, qp_mask);
    GASNETC_VAPI_CHECK(rc, "from ibv_modify_qp(UD INIT)");
#endif

    /* Post RCVs */
    { const size_t size = GASNETI_PAGE_ALIGNUP(max_recv_wr * recv_sz);
      uint8_t *buf = gasneti_mmap(size);
      gasnetc_ud_rcv_desc_t *desc;
      gasnetc_memreg_t mem_reg;
      int i;

      if_pf (MAP_FAILED == buf) {
        gasneti_fatalerror("Failed to allocate recv space for dynamic connection setup");
      }
      rc = gasnetc_pin(&gasnetc_hca[0], buf, size,
                       GASNETC_ACL_LOC_WR, &mem_reg);
      GASNETC_VAPI_CHECK(rc, "while pinning recv space for dynamic connection setup");

      desc = gasneti_malloc(max_recv_wr * sizeof(gasnetc_ud_rcv_desc_t));
      for (i = 0; i < max_recv_wr; ++i, ++desc) {
        desc->wr.gasnetc_f_wr_num_sge = 1;
        desc->wr.gasnetc_f_wr_sg_list = &desc->sg;
        desc->wr.gasnetc_f_wr_id      = (uintptr_t)desc;   /* CQE will point back to this request */
      #if GASNET_CONDUIT_VAPI
        desc->wr.opcode               = VAPI_RECEIVE;
        desc->wr.comp_type            = VAPI_SIGNALED;
      #else   
        desc->wr.next                 = NULL;
      #endif  
        desc->sg.gasnetc_f_sg_len = recv_sz;
        desc->sg.addr             = (uintptr_t)buf + (i * recv_sz);
        desc->sg.lkey             = mem_reg.lkey;
        gasnetc_rcv_post_ud(&desc->wr);
      }
    }

    /* INIT -> RTR */
#if GASNET_CONDUIT_VAPI
    /* XXX: Not yet */
#else
    qp_mask = IBV_QP_STATE;
    qp_attr.qp_state        = IBV_QPS_RTR;
    rc = ibv_modify_qp(gasnetc_conn_qp, &qp_attr, qp_mask);
    GASNETC_VAPI_CHECK(rc, "from ibv_modify_qp(UD RTR)");
#endif

    /* RTR -> RTS */
#if GASNET_CONDUIT_VAPI
    /* XXX: Not yet */
#else
    qp_mask = (enum ibv_qp_attr_mask)(IBV_QP_STATE | IBV_QP_SQ_PSN);
    qp_attr.qp_state        = IBV_QPS_RTS;
    qp_attr.sq_psn          = 0xcafef00d;
    rc = ibv_modify_qp(gasnetc_conn_qp, &qp_attr, qp_mask);
    GASNETC_VAPI_CHECK(rc, "from ibv_modify_qp(UD RTS)");
#endif

    return GASNET_OK;
} /* setup_ud */

extern void
gasnetc_conn_rcv_wc(gasnetc_wc_t *comp)
{
  gasnetc_ud_rcv_desc_t *desc = (gasnetc_ud_rcv_desc_t *)(uintptr_t)comp->gasnetc_f_wr_id;
  uint8_t *buf = (uint8_t *)(uintptr_t)desc->sg.addr;

  /* TODO: we need to process the message here */

  gasnetc_rcv_post_ud(&desc->wr);
}

extern void
gasnetc_conn_snd_wc(gasnetc_wc_t *comp)
{
  void *desc = (void *)(uintptr_t)comp->gasnetc_f_wr_id;

  /* TODO: we need to recycle the descriptor */
}


#if GASNETC_DEBUG_CONNECT
#include <stdlib.h>
static void
permute_nodes(gasnet_node_t *node_list)
{
  static int done_init = 0;
  gasnet_node_t node,swap;

  if_pf (!done_init) srand(getpid());
  done_init = 1;

  for (node = 0; node < gasneti_nodes; ++node) {
    swap = (gasnet_node_t)(((double)(node+1))*rand()/(RAND_MAX+1.0));
    gasneti_assert(swap <= node);
    if (swap == node) {
      node_list[node] = node;
    } else {
      node_list[node] = node_list[swap];
      node_list[swap] = node;
    }
  }
}
#endif /* GASNETC_DEBUG_CONNECT */

/* ------------------------------------------------------------------------------------ */
/* Support code for gasneti_connect_all() */

/* Convert positive integer to string in base 2 to 36.
 * Returns count of digits actually written, or 0 on overflow.
 * Buffer is '\0' terminated except on overflow.
 * This is the "inverse" to strtol
 */
static int
ltostr(char *buf, int buflen, long val, int base) {
  const char digits[] = "0123456789abcdefghijklmnopqrstuvwxyz";
  int i = 0;
  
  gasneti_assert((base > 1) && (base <= strlen(digits)));

  buflen--; /* reserve space for '\0' */
  if_pt (buflen >= 1) {
    /* Work right-aligned in buf */
    char *p = buf + buflen - 1;

    do {
      ldiv_t ld = ldiv(val, base);
      *(p--) = digits[ld.rem];
      val = ld.quot;
    } while ((++i < buflen) && val);
    if_pf (val) return 0; /* Ran out of space */
  
    /* Move to be left-aligned in buf */
    memmove(buf, p+1, i);
    buf[i] = '\0';
  }

  return i;
}

static int
gen_tag(char *tag, int taglen, gasnet_node_t val, int base) {
  int len = ltostr(tag, taglen-1, val, base);
  gasneti_assert(len != 0);
  gasneti_assert(len < taglen-1);
  tag[len+0] = ':';
  tag[len+1] = '\0';
  return len + 1;
}

static long int
my_strtol(const char *ptr, char **endptr, int base) {
  long int result = strtol(ptr,endptr,base);
  if_pf ((*endptr == ptr) || !*endptr) {
    gasneti_fatalerror("Invalid token reading connection table file");
  }
  return result;
}

static gasnet_node_t
get_next_conn(FILE *fp)
{
  static gasnet_node_t range_lo = GASNET_MAXNODES;
  static gasnet_node_t range_hi = 0;

  if (range_lo > range_hi) {
    static char *tok = NULL;

    /* If there is no current token, find the next line with our tag */
    while (!tok || (*tok == '\0')) {
      static char tag[18]; /* even base-2 will fit */
      static size_t taglen = 0;
      if_pf (!taglen) { /* One-time initialization */
        taglen = snprintf(tag, sizeof(tag), "%x:", gasneti_mynode);
      }

      do {
        static int is_header = 1;
        static char *buf = NULL;
        static size_t buflen = 0;
        if (gasneti_getline(&buf, &buflen, fp) == -1) {
          gasneti_free(buf);
          return GASNET_MAXNODES;
        }
        if_pf (is_header) {
          if (!strncmp(buf, "size:", 5)) {
            gasnet_node_t size = my_strtol(buf+5, &tok, 10);
            if (size != gasneti_nodes) {
              gasneti_fatalerror("Connection table input file is for %d nodes rather than %d",
                                 (int)size, (int)gasneti_nodes);
            }
            continue;
          } else if (!strncmp(buf, "base:", 5)) {
            gasnetc_connectfile_in_base = my_strtol(buf+5, &tok, 10);
            taglen = gen_tag(tag, sizeof(tag), gasneti_mynode, gasnetc_connectfile_in_base);
            continue;
          }
          is_header = 0;
        }
        tok = buf;
      } while (strncmp(tok, tag, taglen));
      tok += taglen;
      while (*tok && isspace(*tok)) ++tok;
    }
    
   
    /* Parse the current token */
    { char *p, *q;
      range_lo = range_hi = my_strtol(tok, &p, gasnetc_connectfile_in_base);
      range_hi = (*p == '-') ? my_strtol(p+1, &q, gasnetc_connectfile_in_base) : range_lo;
    }

    /* Advance to next token or end ot line */
    while (*tok && !isspace(*tok)) ++tok;
    while (*tok && isspace(*tok)) ++tok;
  }
   
  return range_lo++;
}
/* ------------------------------------------------------------------------------------ */

/* Setup statically-connected communication */
extern int
gasnetc_connect_static(void)
{
  const int             ceps = gasneti_nodes * gasnetc_alloc_qps;
  gasnetc_qpn_t         *local_qpn = gasneti_calloc(ceps, sizeof(gasnetc_qpn_t));
  gasnetc_qpn_t         *remote_qpn = gasneti_calloc(ceps, sizeof(gasnetc_qpn_t));
  gasnetc_conn_info_t   *conn_info = gasneti_calloc(gasneti_nodes, sizeof(gasnetc_conn_info_t));
#if GASNETC_IBV_XRC
  gasnetc_qpn_t         *xrc_remote_rcv_qpn = NULL;
  uint32_t              *xrc_remote_srq_num = NULL;
#endif
  gasnet_node_t         node;
  gasnet_node_t         static_nodes = gasnetc_remote_nodes;
#if GASNETC_IBV_XRC
  gasnet_node_t         static_supernodes = gasneti_nodemap_global_count - 1;
#endif
  int                   i, qpi;
  size_t                orig_inline_limit = gasnetc_inline_limit;
  gasnetc_cep_t         *cep; /* First cep of given node */
  uint8_t               *peer_mask = NULL;
#if GASNETC_DEBUG_CONNECT
  gasnet_node_t *node_list = gasneti_malloc(gasneti_nodes * sizeof(gasnet_node_t));
  gasnet_node_t node_idx;
#endif

  if_pf (!gasnetc_remote_nodes) goto done;

  #define GASNETC_IS_REMOTE_NODE(_node) \
    (peer_mask ? peer_mask[_node] : !gasnetc_non_ib(_node))

  /* Debug build loops in random order to help ensure connect code is order-independent */
#if GASNETC_DEBUG_CONNECT
  #define GASNETC_FOR_EACH_REMOTE_NODE(_node) \
    for (permute_nodes(node_list), node_idx=0, (_node) = node_list[0]; \
         node_idx < gasneti_nodes; (_node) = node_list[++node_idx]) \
      if (GASNETC_IS_REMOTE_NODE(_node))
#else
  #define GASNETC_FOR_EACH_REMOTE_NODE(_node) \
    for ((_node) = 0; (_node) < gasneti_nodes; ++(_node)) \
      if (GASNETC_IS_REMOTE_NODE(_node))
#endif

  /* Honor user's connections file if given */
  { const char *envstr = gasnet_getenv("GASNET_CONNECTFILE_IN");
    if (envstr) {
      const char *filename = gasnetc_parse_filename(envstr);
      FILE *fp;
    #ifdef HAVE_FOPEN64
      fp = fopen64(filename, "r");
    #else
      fp = fopen(filename, "r");
    #endif
      if (!fp) {
        fprintf(stderr, "ERROR: unable to open connection table inout file '%s'\n", filename);
      }
      if (filename != envstr) gasneti_free((/* not const */ char *)filename);

      peer_mask = gasneti_calloc(gasneti_nodes, sizeof(uint8_t));
      while (GASNET_MAXNODES != (node = get_next_conn(fp))) {
        gasneti_assert(node < gasneti_nodes);
        peer_mask[node] = 1;
      }
      fclose(fp);

      /* Since conn table is not always symmetric we must transpose-and-OR */
      { uint8_t *transposed_mask = gasneti_malloc(gasneti_nodes * sizeof(uint8_t));
        gasneti_bootstrapAlltoall(peer_mask, sizeof(uint8_t), transposed_mask);
        for (static_nodes = node = 0; node < gasneti_nodes; ++node) {
          peer_mask[node] = !gasnetc_non_ib(node) && (peer_mask[node] || transposed_mask[node]);
          gasneti_assert((peer_mask[node] == 0) || (peer_mask[node] == 1));
          static_nodes += peer_mask[node];
       }
       gasneti_free(transposed_mask);
      }

    #if GASNETC_IBV_XRC
      if (gasnetc_use_xrc) {
        uint8_t *supernode_mask = gasneti_calloc(gasneti_nodemap_global_count, sizeof(uint8_t));
        for (node = 0; node < gasneti_nodes; ++node) {
          supernode_mask[gasneti_nodeinfo[node]] |= peer_mask[node];
        }
        for (static_supernodes = node = 0; node < gasneti_nodemap_global_count; ++node) {
          gasneti_assert((supernode_mask[node] == 0) || (supernode_mask[node] == 1));
          static_supernodes += supernode_mask[node];
        }
        gasneti_free(supernode_mask);
      }
    #endif
    }
  }

  /* Allocate the dense CEP table and populate the node2cep table. */
  {
    gasnetc_cep_t *cep_table = (gasnetc_cep_t *)
      gasnett_malloc_aligned(GASNETI_CACHE_LINE_BYTES,
                             static_nodes * gasnetc_alloc_qps * sizeof(gasnetc_cep_t));
    for (node = 0, cep = cep_table; node < gasneti_nodes; ++node) { /* NOT randomized */
      if (!GASNETC_IS_REMOTE_NODE(node)) continue;
      gasnetc_node2cep[node] = cep;
      memset(cep, 0, gasnetc_alloc_qps * sizeof(gasnetc_cep_t));
      cep +=  gasnetc_alloc_qps;
    }
    gasneti_assert((cep - cep_table) == (static_nodes * gasnetc_alloc_qps));
  }

  /* Preallocate the SQ semaphores */
#if GASNETC_IBV_XRC
  if (gasnetc_use_xrc) {
    sq_sema_alloc(static_supernodes * gasnetc_alloc_qps);
  } else
#endif
  {
    sq_sema_alloc(static_nodes * gasnetc_alloc_qps);
  }

  /* Initialize connection tracking info */
  GASNETC_FOR_EACH_REMOTE_NODE(node) {
    i = node * gasnetc_alloc_qps;
    conn_info[node].cep            = GASNETC_NODE2CEP(node);
    conn_info[node].local_qpn      = &local_qpn[i];
  #if GASNETC_IBV_XRC
    conn_info[node].local_xrc_qpn  = &gasnetc_xrc_rcv_qpn[i];
  #endif
    gasnetc_setup_ports(node, &conn_info[node]);
  }

  /* create all QPs */
  GASNETC_FOR_EACH_REMOTE_NODE(node) {
    (void)gasnetc_qp_create(node, &conn_info[node]);
  }

  /* exchange address info */
#if GASNETC_IBV_XRC
  if (gasnetc_use_xrc) {
    /* Use single larger exchange rather then multiple smaller ones */
    gasnetc_xrc_conn_data_t *local_tmp  = gasneti_calloc(ceps,  sizeof(gasnetc_xrc_conn_data_t));
    gasnetc_xrc_conn_data_t *remote_tmp = gasneti_malloc(ceps * sizeof(gasnetc_xrc_conn_data_t));
    for (i = node = 0; node < gasneti_nodes; ++node) {
      cep = GASNETC_NODE2CEP(node);
      for (qpi = 0; qpi < gasnetc_alloc_qps; ++qpi, ++i) {
        if (GASNETC_IS_REMOTE_NODE(node)) {
          gasnetc_hca_t *hca = cep[qpi].hca;
          struct ibv_srq *srq = GASNETC_QPI_IS_REQ(qpi) ? hca->rqst_srq : hca->repl_srq;
          local_tmp[i].srq_num = srq->xrc_srq_num;
        }
        local_tmp[i].xrc_qpn = gasnetc_xrc_rcv_qpn[i];
        local_tmp[i].qpn     = local_qpn[i];
      }
    }
    gasneti_bootstrapAlltoall(local_tmp,
                              gasnetc_alloc_qps * sizeof(gasnetc_xrc_conn_data_t),
                              remote_tmp);
    xrc_remote_rcv_qpn = gasneti_malloc(ceps * sizeof(gasnetc_qpn_t));
    xrc_remote_srq_num = gasneti_malloc(ceps * sizeof(uint32_t));
    for (i = 0; i < ceps; ++i) {
      xrc_remote_srq_num[i] = remote_tmp[i].srq_num;
      xrc_remote_rcv_qpn[i] = remote_tmp[i].xrc_qpn;
      remote_qpn[i]         = remote_tmp[i].qpn;
    }
    gasneti_free(remote_tmp);
    gasneti_free(local_tmp);
  } else
#endif
  gasneti_bootstrapAlltoall(local_qpn, gasnetc_alloc_qps*sizeof(gasnetc_qpn_t), remote_qpn);

  /* Advance state RESET -> INIT and perform local endpoint init.
     This could be overlapped with the AlltoAll if it were non-blocking*/
  GASNETC_FOR_EACH_REMOTE_NODE(node) {
    (void)gasnetc_qp_reset2init(node, &conn_info[node]);
    gasnetc_sndrcv_init_peer(node);
  }

  /* Would sync the AlltoAll here if it were non-blocking */
  GASNETC_FOR_EACH_REMOTE_NODE(node) {
    i = node * gasnetc_alloc_qps;
    conn_info[node].remote_qpn     = &remote_qpn[i];
  #if GASNETC_IBV_XRC
    conn_info[node].remote_xrc_qpn = &xrc_remote_rcv_qpn[i];
    conn_info[node].xrc_remote_srq_num = &xrc_remote_srq_num[i];
  #endif
  }

  /* Advance state INIT -> RTR */
  GASNETC_FOR_EACH_REMOTE_NODE(node) {
    (void)gasnetc_qp_init2rtr(node, &conn_info[node]);
  }

  /* QPs must reach RTS before we may continue
     (not strictly necessary in practice as long as we don't try to send until peers do.) */
  gasneti_bootstrapBarrier();

  /* Advance state RTR -> RTS */
  GASNETC_FOR_EACH_REMOTE_NODE(node) {
    (void)gasnetc_qp_rtr2rts(node, &conn_info[node]);
  }

  /* check inline limit */
  if ((orig_inline_limit != (size_t)-1) && (gasnetc_inline_limit < orig_inline_limit)) {
#if GASNET_CONDUIT_IBV
    if (gasnet_getenv("GASNET_INLINESEND_LIMIT") != NULL)
#endif
      fprintf(stderr,
              "WARNING: Requested GASNET_INLINESEND_LIMIT %d reduced to HCA limit %d\n",
              (int)orig_inline_limit, (int)gasnetc_inline_limit);
  }
  GASNETI_TRACE_PRINTF(I, ("Final/effective GASNET_INLINESEND_LIMIT = %d", (int)gasnetc_inline_limit));
  gasnetc_sndrcv_init_inline();

done:
#if GASNETC_IBV_XRC
  gasneti_free(xrc_remote_srq_num);
  gasneti_free(xrc_remote_rcv_qpn);
#endif
  gasneti_free(conn_info);
  gasneti_free(remote_qpn);
  gasneti_free(local_qpn);
#if GASNETC_DEBUG_CONNECT
  gasneti_free(node_list);
#endif
  gasneti_free(peer_mask);

#if GASNET_CONDUIT_IBV /* XXX: Not (yet?) implemented for VAPI */
  {
    gasnetc_qp_setup_ud(&gasnetc_port_tbl[0]);
  }
#endif

  return GASNET_OK;
} /* gasnetc_connect_static */

/* ------------------------------------------------------------------------------------ */

/* Early setup for connection resources */
extern int
gasnetc_connect_init(void)
{

  /* Allocate node->cep lookup table */
  { size_t size = gasneti_nodes*sizeof(gasnetc_cep_t *);
    gasnetc_node2cep = (gasnetc_cep_t **)
      gasnett_malloc_aligned(GASNETI_CACHE_LINE_BYTES, size);
    memset(gasnetc_node2cep, 0, size);
  }

#if GASNETC_IBV_XRC
  if (gasnetc_use_xrc) {
    int ret = gasnetc_xrc_init();
    if (ret) return ret;
  }
#endif

  gasnetc_connectfile_out_base =
        gasneti_getenv_int_withdefault("GASNET_CONNECTFILE_BASE",
                                       gasnetc_connectfile_out_base, 0);

  /* Must we disable barrier AMs from all but the supernode representative? */
  if (gasnet_getenv("GASNET_CONNECTFILE_IN") || gasnet_getenv("GASNET_CONNECTFILE_OUT")) {
    gasnete_barrier_fixed = 1;
  }

  return GASNET_OK;
} /* gasnetc_connect_init */

/* ------------------------------------------------------------------------------------ */
/* Support code for gasneti_conn_fini */

static char dump_conn_line[512] = "";
static gasnet_node_t dump_conn_first = GASNET_MAXNODES;
static gasnet_node_t dump_conn_prev;

static void
dump_conn_outln(int fd)
{
  static char fullline[96];
  static size_t taglen = 0;
  size_t len;

  if_pf (!taglen) {
    taglen = gen_tag(fullline, sizeof(fullline), gasneti_mynode, gasnetc_connectfile_out_base);
  }

  len = strlen(dump_conn_line+1);
  memcpy(fullline+taglen, dump_conn_line+1, len);

  len += taglen;
  fullline[len] = '\n';

  write(fd, fullline, len+1);
}

static void
dump_conn_out(int fd) {
  char elem[35];
  size_t len, tmp;

  /* Leading space */
  elem[0] = ' ';
  len = 1;

  /* dump_conn_first foramtted with desired base */
  tmp = ltostr(elem+len, sizeof(elem)-len, dump_conn_first, gasnetc_connectfile_out_base);
  gasneti_assert(tmp != 0);
  len += tmp;

  if (dump_conn_prev != dump_conn_first) {
    /* Choose ' ' or '-' as separator */
    elem[len++] = ((dump_conn_prev - dump_conn_first) > 1) ? '-' : ' ';
  
    /* dump_conn_prev foramtted with desired base */
    tmp = ltostr(elem+len, sizeof(elem)-len, dump_conn_prev, gasnetc_connectfile_out_base);
    gasneti_assert(tmp != 0);
    len += tmp;

    elem[len] = '\0';
  }

  gasneti_assert(len == strlen(elem));
  gasneti_assert(len < sizeof(elem));

  if (strlen(dump_conn_line) + len < sizeof(dump_conn_line)) {
    strcat(dump_conn_line, elem);
  } else {
    dump_conn_outln(fd);
    strcpy(dump_conn_line, elem);
  }
}

static void
dump_conn_next(int fd, gasnet_node_t n)
{
  if (dump_conn_first == GASNET_MAXNODES) {
    dump_conn_first = dump_conn_prev = n;
    return;
  } else if (n == dump_conn_prev+1) {
    dump_conn_prev = n;
    return;
  }

  dump_conn_out(fd);
  dump_conn_first = dump_conn_prev = n;
}

static void
dump_conn_done(int fd)
{
  if (dump_conn_first == GASNET_MAXNODES) return;
  dump_conn_out(fd);
  dump_conn_outln(fd);
  close(fd);
}

/* ------------------------------------------------------------------------------------ */

/* Fini currently just dumps connection table. */
extern int
gasnetc_connect_fini(void)
{
  gasnet_node_t n, count = 0;
  int fd = -1;

  /* Open file replacing any '%' in filename with node number */
  { const char *envstr = gasnet_getenv("GASNET_CONNECTFILE_OUT");
    if (envstr) {
      const char *filename = gasnetc_parse_filename(envstr);
      int flags = O_APPEND | O_CREAT | O_WRONLY;
    #ifdef O_LARGEFILE
      flags |= O_LARGEFILE;
    #endif
      fd = open(filename, flags, S_IRUSR | S_IWUSR);
      if (fd < 0) {
        fprintf(stderr, "ERROR: unable to open connection table output file '%s'\n", filename);
      }
      if (filename != envstr) gasneti_free((/* not const */ char *)filename);
      if (!gasneti_mynode || strchr(envstr, '%')) {
        char buf[16];
        size_t len;
        gasneti_assert_zeroret(ftruncate(fd,0));
        len = snprintf(buf, sizeof(buf), "size:%d\n", gasneti_nodes);
        write(fd, buf, len);
        len = snprintf(buf, sizeof(buf), "base:%d\n", gasnetc_connectfile_out_base);
        write(fd, buf, len);
      }
      gasneti_bootstrapBarrier();
    }
  }
 
  for (n = 0; n < gasneti_nodes; ++n) {
    gasnetc_cep_t *cep = GASNETC_NODE2CEP(n);
    int qpi;
    if (!cep) continue;
    for (qpi=0; qpi<gasnetc_alloc_qps; ++qpi, ++cep) {
      if (cep[qpi].used) {
        if (fd >= 0) dump_conn_next(fd, n);
        ++count;
        break;
      }
    }
  }
  if (fd >= 0) dump_conn_done(fd);
  GASNETI_TRACE_PRINTF(C, ("Network traffic sent to %d of %d remote nodes", (int)count, (int)gasnetc_remote_nodes));

  return GASNET_OK;
} /* gasnetc_connect_fini */

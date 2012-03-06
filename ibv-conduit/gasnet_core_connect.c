/*   $Source: /Users/kamil/work/gasnet-cvs2/gasnet/ibv-conduit/gasnet_core_connect.c,v $
 *     $Date: 2011/02/09 02:45:32 $
 * $Revision: 1.1 $
 * Description: Connection management code
 * Copyright 2011, E. O. Lawrence Berekely National Laboratory
 * Terms of use are as specified in license.txt
 */

#include <gasnet_internal.h>
#include <gasnet_core_internal.h>

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


/* Convenience iterator */
#define GASNETC_FOR_EACH_QPI(_node, _qpi, _cep_idx, _cep)  \
  for((_cep_idx) = (_node) * gasnetc_alloc_qps, (_cep) = &gasnetc_cep[(_cep_idx)], (_qpi) = 0; \
      (_qpi) < gasnetc_alloc_qps; ++(_cep_idx), ++(_cep), ++(_qpi))

/* Convenience macro */
#if GASNETC_IBV_SRQ 
  #define GASNETC_QPI_IS_REQ(_qpi) ((_qpi) >= gasnetc_num_qps)
#else
  #define GASNETC_QPI_IS_REQ(_qpi) (0)
#endif


/* Common types */
typedef GASNETC_IB_CHOOSE(VAPI_qp_attr_t,       struct ibv_qp_attr)     gasnetc_qp_attr_t;
typedef GASNETC_IB_CHOOSE(VAPI_qp_attr_mask_t,  enum ibv_qp_attr_mask)  gasnetc_qp_mask_t;

/* NOTES regarding work toward on-demand connections:
 *
 * All of the pointer arguments to these functions are to the node-specific
 * portion of any larger array.  Thus only a constant-length small array is
 * needed when we move to on-demand.
 *
 * There are a few global vars still currently indexed by cep_idx, which are
 * therefore FULL arrays.  You can assume any use of cep_idx is a reference
 * to a full array.  Currently the full arrays used include:
 *    gasnetc_cep - used in GASNETC_FOR_EACH_QPI and for "other" cep w/ XRC
 *    gasnetc_xrc_rcv_qpn_local - used with XRC
 *
 * The XRC related code regarding "other" is also written under the assumption
 * that the "other" QP has been handled already:
 *    gasnetc_qp_create() directly reuses fields from gasnetc_cep[other]
 *    State transitions all just skip non-first nodes.
 */


#if GASNETC_IBV_XRC
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
#endif



/* Create endpoint(s) for a given peer
 * Outputs the qpn values in the array provided.
 */
extern int
gasnetc_qp_create(
        gasnetc_qpn_t *local_qpn,
        gasnet_node_t node)
{
    gasnetc_cep_t *cep;
    int qpi, cep_idx;
    int rc;

#if GASNET_CONDUIT_VAPI
    VAPI_qp_init_attr_t qp_init_attr;
    VAPI_qp_prop_t      qp_prop;

    qp_init_attr.cap.max_oust_wr_rq = gasnetc_am_oust_pp * 2;
    qp_init_attr.cap.max_oust_wr_sq = gasnetc_op_oust_pp;
    qp_init_attr.cap.max_sg_size_rq = 1;
    qp_init_attr.cap.max_sg_size_sq = GASNETC_SND_SG;
    qp_init_attr.rdd_hndl           = 0;
    qp_init_attr.rq_sig_type        = VAPI_SIGNAL_REQ_WR;
    qp_init_attr.sq_sig_type        = VAPI_SIGNAL_REQ_WR;
    qp_init_attr.ts_type            = VAPI_TS_RC;

    GASNETC_FOR_EACH_QPI(node, qpi, cep_idx, cep) {
      gasnetc_hca_t *hca = cep->hca;
      gasneti_assert(hca);

      qp_init_attr.pd_hndl         = hca->pd;
      qp_init_attr.rq_cq_hndl      = hca->rcv_cq;
      qp_init_attr.sq_cq_hndl      = hca->snd_cq;
      rc = VAPI_create_qp(hca->handle, &qp_init_attr, &cep->qp_handle, &qp_prop);
      GASNETC_VAPI_CHECK(rc, "from VAPI_create_qp()");
      gasneti_assert(qp_prop.cap.max_oust_wr_rq >= gasnetc_am_oust_pp * 2);
      gasneti_assert(qp_prop.cap.max_oust_wr_sq >= gasnetc_op_oust_pp);
      /* XXX: When could/should we use the ENTIRE allocated length? */
      gasneti_semaphore_init(&cep->sq_sema, gasnetc_op_oust_pp, gasnetc_op_oust_pp);

      local_qpn[qpi] = qp_prop.qp_num;
    }
#else
    struct ibv_qp_init_attr     qp_init_attr;
    const int                   max_recv_wr = gasnetc_use_srq ? 0 : gasnetc_am_oust_pp * 2;
    int                         max_send_wr = gasnetc_op_oust_pp;

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

    GASNETC_FOR_EACH_QPI(node, qpi, cep_idx, cep) {
      gasnetc_qp_hndl_t hndl;
      gasnetc_hca_t *hca = cep->hca;
      gasneti_assert(hca);

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
        const gasnet_node_t first = gasneti_nodemap[node];
  
        if (node != first) {
          gasnetc_cep_t *other = &gasnetc_cep[(first * gasnetc_alloc_qps) + qpi];

          cep->qp_handle = other->qp_handle;
          cep->sq_sema_p = other->sq_sema_p;
          cep->rcv_qpn = gasnetc_xrc_rcv_qpn_local[cep_idx];
          local_qpn[qpi] = cep->qp_handle->qp_num;
          continue;
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
      cep->qp_handle = hndl;
      gasneti_assert(qp_init_attr.cap.max_recv_wr >= max_recv_wr);
      gasneti_assert(qp_init_attr.cap.max_send_wr >= max_send_wr);
      local_qpn[qpi] = cep->qp_handle->qp_num;
      /* XXX: When could/should we use the ENTIRE allocated length? */
      gasneti_semaphore_init(&cep->sq_sema, max_send_wr, max_send_wr);
    #if GASNETC_IBV_XRC
      cep->sq_sema_p = &cep->sq_sema;
    #endif
  
      /* Set cep->rcv_qpn */
    #if GASNETC_IBV_XRC
      cep->rcv_qpn = gasnetc_use_xrc ? gasnetc_xrc_rcv_qpn_local[cep_idx] : cep->qp_handle->qp_num;
    #elif GASNETC_IBV_SRQ
      cep->rcv_qpn = cep->qp_handle->qp_num;
    #endif
    }
#endif

    return GASNET_OK;
}

/* Advance QP state from RESET to INIT */
extern int
gasnetc_qp_reset2init(
        gasnet_node_t node,
        gasnetc_port_info_t **port_map)
{
    gasnetc_qp_attr_t qp_attr;
    gasnetc_qp_mask_t qp_mask;
    gasnetc_cep_t *cep;
    int qpi, cep_idx;
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

    GASNETC_FOR_EACH_QPI(node, qpi, cep_idx, cep) {
      gasnetc_hca_t *hca = cep->hca;
      gasneti_assert(hca);
      
      qp_attr.port = port_map[qpi]->port_num;
      rc = VAPI_modify_qp(cep->hca_handle, cep->qp_handle, &qp_attr, &qp_mask, &qp_cap);
      GASNETC_VAPI_CHECK(rc, "from VAPI_modify_qp(INIT)");
    }
#else
    qp_mask = (enum ibv_qp_attr_mask)(IBV_QP_STATE | IBV_QP_PKEY_INDEX | IBV_QP_PORT | IBV_QP_ACCESS_FLAGS);
    qp_attr.qp_state        = IBV_QPS_INIT;
    qp_attr.pkey_index      = 0;
    qp_attr.qp_access_flags = IBV_ACCESS_REMOTE_WRITE | IBV_ACCESS_REMOTE_READ;

    #if GASNETC_IBV_XRC
    if (!gasnetc_use_xrc || (node == gasneti_nodemap[node]))
    #endif
    GASNETC_FOR_EACH_QPI(node, qpi, cep_idx, cep) {
    #if GASNETC_IBV_SRQ
      qp_attr.qp_access_flags = GASNETC_QPI_IS_REQ(qpi)
                                    ? IBV_ACCESS_REMOTE_WRITE
                                    : IBV_ACCESS_REMOTE_WRITE | IBV_ACCESS_REMOTE_READ;
    #endif
      qp_attr.port_num = port_map[qpi]->port_num;

      rc = ibv_modify_qp(cep->qp_handle, &qp_attr, qp_mask);
      GASNETC_VAPI_CHECK(rc, "from ibv_modify_qp(INIT)");
    }

  #if GASNETC_IBV_XRC
    if (gasnetc_use_xrc) {
      GASNETC_FOR_EACH_QPI(node, qpi, cep_idx, cep) {
        qp_attr.qp_access_flags = GASNETC_QPI_IS_REQ(qpi)
                                      ? IBV_ACCESS_REMOTE_WRITE
                                      : IBV_ACCESS_REMOTE_WRITE | IBV_ACCESS_REMOTE_READ;
        qp_attr.port_num = port_map[qpi]->port_num;
        rc = gasnetc_xrc_modify_qp(cep->hca->xrc_domain, gasnetc_xrc_rcv_qpn_local[cep_idx], &qp_attr, qp_mask);
        GASNETC_VAPI_CHECK(rc, "from gasnetc_xrc_modify_qp(INIT)");
      }
    }
  #endif
#endif

    return GASNET_OK;
}

/* Advance QP state from INIT to RTR */
extern int
gasnetc_qp_init2rtr(
    gasnet_node_t node,
    gasnetc_port_info_t **port_map,
    gasnetc_qpn_t *remote_qpn)
{
    gasnetc_qp_attr_t qp_attr;
    gasnetc_qp_mask_t qp_mask;
    gasnetc_cep_t *cep;
    int qpi, cep_idx;
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

    GASNETC_FOR_EACH_QPI(node, qpi, cep_idx, cep) {
      gasnetc_hca_t *hca = cep->hca;

      qp_attr.qp_ous_rd_atom = port_map[qpi]->rd_atom;
      qp_attr.path_mtu       = MIN(GASNETC_QP_PATH_MTU, port_map[qpi]->port.max_mtu);
      qp_attr.rq_psn         = cep_idx;
      qp_attr.av.dlid        = port_map[qpi]->remote_lids[node];
      qp_attr.dest_qp_num    = remote_qpn[qpi];
      rc = VAPI_modify_qp(cep->hca_handle, cep->qp_handle, &qp_attr, &qp_mask, &qp_cap);
      GASNETC_VAPI_CHECK(rc, "from VAPI_modify_qp(RTR)");
    }
#else
    qp_mask = (enum ibv_qp_attr_mask)(IBV_QP_STATE | IBV_QP_AV | IBV_QP_PATH_MTU | IBV_QP_RQ_PSN | IBV_QP_MAX_DEST_RD_ATOMIC | IBV_QP_DEST_QPN | IBV_QP_MIN_RNR_TIMER);
    qp_attr.qp_state         = IBV_QPS_RTR;
    qp_attr.ah_attr.sl            = 0;
    qp_attr.ah_attr.is_global     = 0;
    qp_attr.ah_attr.static_rate   = GASNETC_QP_STATIC_RATE;
    qp_attr.ah_attr.src_path_bits = 0;

    qp_attr.min_rnr_timer    = GASNETC_QP_MIN_RNR_TIMER;

    #if GASNETC_IBV_XRC
    if (!gasnetc_use_xrc || (node == gasneti_nodemap[node]))
    #endif
    GASNETC_FOR_EACH_QPI(node, qpi, cep_idx, cep) {
      qp_attr.max_dest_rd_atomic = GASNETC_QPI_IS_REQ(qpi) ? 0 : port_map[qpi]->rd_atom;
      qp_attr.path_mtu           = MIN(GASNETC_QP_PATH_MTU, port_map[qpi]->port.max_mtu);
      qp_attr.rq_psn             = cep_idx;
      qp_attr.ah_attr.dlid       = port_map[qpi]->remote_lids[node];
      qp_attr.ah_attr.port_num   = port_map[qpi]->port_num;
    #if GASNETC_IBV_XRC
      qp_attr.dest_qp_num    = gasnetc_use_xrc ? gasnetc_xrc_rcv_qpn_remote[cep_idx] : remote_qpn[qpi];
    #else
      qp_attr.dest_qp_num    = remote_qpn[qpi];
    #endif

      rc = ibv_modify_qp(cep->qp_handle, &qp_attr, qp_mask);
      GASNETC_VAPI_CHECK(rc, "from ibv_modify_qp(RTR)");
    }

  #if GASNETC_IBV_XRC
    if (gasnetc_use_xrc) {
      GASNETC_FOR_EACH_QPI(node, qpi, cep_idx, cep) {
        qp_attr.max_dest_rd_atomic = GASNETC_QPI_IS_REQ(qpi) ? 0 : port_map[qpi]->rd_atom;
        qp_attr.path_mtu           = MIN(GASNETC_QP_PATH_MTU, port_map[qpi]->port.max_mtu);
        qp_attr.rq_psn             = cep_idx;
        qp_attr.ah_attr.dlid       = port_map[qpi]->remote_lids[node];
        qp_attr.ah_attr.port_num   = port_map[qpi]->port_num;
        qp_attr.dest_qp_num        = remote_qpn[qpi];
        rc = gasnetc_xrc_modify_qp(cep->hca->xrc_domain, gasnetc_xrc_rcv_qpn_local[cep_idx], &qp_attr, qp_mask);
        GASNETC_VAPI_CHECK(rc, "from gasnetc_xrc_modify_qp(RTR)");
      }
    }
  #endif
#endif

    return GASNET_OK;
}

/* Advance QP state from RTR to RTS */
extern int
gasnetc_qp_rtr2rts(
    gasnet_node_t node,
    gasnetc_port_info_t **port_map)
{
    gasnetc_qp_attr_t qp_attr;
    gasnetc_qp_mask_t qp_mask;
    gasnetc_cep_t *cep;
    int qpi, cep_idx;
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

    GASNETC_FOR_EACH_QPI(node, qpi, cep_idx, cep) {
      qp_attr.sq_psn           = gasneti_mynode*gasnetc_alloc_qps + qpi;
      qp_attr.ous_dst_rd_atom  = port_map[qpi]->rd_atom;
      rc = VAPI_modify_qp(cep->hca_handle, cep->qp_handle, &qp_attr, &qp_mask, &qp_cap);
      GASNETC_VAPI_CHECK(rc, "from VAPI_modify_qp(RTS)");
      gasnetc_inline_limit = MIN(gasnetc_inline_limit, qp_cap.max_inline_data_sq);
    }
#else
    qp_mask = (enum ibv_qp_attr_mask)(IBV_QP_STATE | IBV_QP_SQ_PSN | IBV_QP_TIMEOUT | IBV_QP_RETRY_CNT | IBV_QP_RNR_RETRY | IBV_QP_MAX_QP_RD_ATOMIC);
    qp_attr.qp_state         = IBV_QPS_RTS;
    qp_attr.timeout          = gasnetc_qp_timeout;
    qp_attr.retry_cnt        = gasnetc_qp_retry_count;
    qp_attr.rnr_retry        = GASNETC_QP_RNR_RETRY;

    #if GASNETC_IBV_XRC
    if (!gasnetc_use_xrc || (node == gasneti_nodemap[node]))
    #endif
    GASNETC_FOR_EACH_QPI(node, qpi, cep_idx, cep) {
      qp_attr.sq_psn           = gasneti_mynode*gasnetc_alloc_qps + qpi;
      qp_attr.max_rd_atomic    = GASNETC_QPI_IS_REQ(qpi) ? 0 : port_map[qpi]->rd_atom;
      rc = ibv_modify_qp(cep->qp_handle, &qp_attr, qp_mask);
      GASNETC_VAPI_CHECK(rc, "from ibv_modify_qp(RTS)");
      {
        struct ibv_qp_attr qp_attr2;
        struct ibv_qp_init_attr qp_init_attr;
        rc = ibv_query_qp(cep->qp_handle, &qp_attr2, IBV_QP_CAP, &qp_init_attr);
        GASNETC_VAPI_CHECK(rc, "from ibv_query_qp(RTS)");
        gasnetc_inline_limit = MIN(gasnetc_inline_limit, qp_attr2.cap.max_inline_data);
      }
    }

    #if !GASNET_PSHM && GASNET_DEBUG
    if (gasnetc_use_xrc && gasnetc_non_ib(node)) {
      GASNETC_FOR_EACH_QPI(node, qpi, cep_idx, cep) {
        cep->hca = NULL;
      }
    }
    #endif
#endif

    return GASNET_OK;
}

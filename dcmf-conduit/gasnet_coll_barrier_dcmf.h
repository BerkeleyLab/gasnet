/* $Source: /Users/kamil/work/gasnet-cvs2/gasnet/dcmf-conduit/gasnet_coll_barrier_dcmf.h,v $
 * $Date: 2009/07/07 00:00:49 $
 * $Revision: 1.1.4.2 $
 * Description:  GASNet barrier implementation on DCMF
 * LBNL 2009
 */

#ifndef GASNET_COLL_BARRIER_DCMF_H_
#define GASNET_COLL_BARRIER_DCMF_H_

// #include <gasnet_extended_coll_dcmf.h>

// #define G_DCMF_GLOBALBARRIER_PROTO_NUM 3 /**< see dcmf_globalcollectives.h */

/* Barrier protocols */
typedef enum {
  GI_BARRIER=0,
  TREE_BARRIER,
  TORUS_RECTANGLE_BARRIER,
  TORUS_RECTANGLE_LB_BARRIER,
  TORUS_BINOMIAL_BARRIER,
  LOCKBOX_BARRIER, /* local barrier */
  G_DCMF_BARRIER_PROTO_NUM
} gasnete_dcmf_barrier_proto_t;

/* #define G_DCMF_BARRIER_PROTO_NUM 10 */
/* typedef DCMF_Barrier_Protocol gasnete_dcmf_barrier_proto_t; */

extern DCMF_CollectiveProtocol_t g_dcmf_barrier_proto[G_DCMF_BARRIER_PROTO_NUM];

/** g_dcmf_barrier_enabled indicates whether a dcmf barrier protocol
 *  is enabled (1) or not (0).
 */
extern unsigned int g_dcmf_barrier_enabled[G_DCMF_BARRIER_PROTO_NUM];

extern DCMF_CollectiveProtocol_t *g_dcmf_barrier[G_DCMF_BARRIER_PROTO_NUM];
extern unsigned int g_dcmf_barrier_num; /**< num. of available barrier protocols */

extern DCMF_CollectiveProtocol_t *g_dcmf_localbarrier[G_DCMF_BARRIER_PROTO_NUM];
extern unsigned int g_dcmf_localbarrier_num; /**< num. of available local barrier protocols */

extern gasnete_dcmf_barrier_proto_t g_dcmf_barrier_kind_default;
extern gasnete_dcmf_barrier_proto_t g_dcmf_lbarrier_kind_default;

/** Register DCMF barrier protocols */
void gasnete_coll_barrier_proto_register();

void gasnete_coll_teambarrier_dcmf(gasnet_team_handle_t team);

void gasnete_coll_teambarrier_notify_dcmf(gasnet_team_handle_t team);

void gasnete_coll_teambarrier_wait_dcmf(gasnet_team_handle_t team);

int gasnete_coll_teambarrier_try_dcmf(gasnet_team_handle_t team);

#endif /* GASNET_COLL_BARRIER_DCMF_H_ */

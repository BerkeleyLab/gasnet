/* $Source: /Users/kamil/work/gasnet-cvs2/gasnet/dcmf-conduit/gasnet_coll_barrier_dcmf.h,v $
 * $Date: 2009/05/28 22:17:51 $
 * $Revision: 1.1.2.1 $
 * Description:  GASNet barrier implementation on DCMF
 * LBNL 2009
 */

#ifndef GASNET_COLL_BARRIER_DCMF_H_
#define GASNET_COLL_BARRIER_DCMF_H_

#include <gasnet_extended_coll_dcmf.h>

#define G_DCMF_GLOBALBARRIER_PROTO_NUM 3 /**< see dcmf_globalcollectives.h */

/* Barrier protocols */
typedef enum {
  GI_BARRIER=0,
  TREE_BARRIER,
  TORUS_BINOMIAL_BARRIER,
  TORUS_RECTANGLE_BARRIER, 
  LOCKBOX_BARRIER,
  TORUS_RECTANGLELOCKBOX_BARRIER, 
  G_DCMF_BARRIER_PROTO_NUM
} gasnete_dcmf_barrier_proto_t;

/** g_dcmf_barrier_enabled indicates whether a dcmf barrier protocol
 *  is enabled (1) or not (0).
 */
extern unsigned int g_dcmf_barrier_enabled[G_DCMF_BARRIER_PROTO_NUM];

extern DCMF_CollectiveProtocol_t *g_dcmf_barrier[G_DCMF_BARRIER_PROTO_NUM];
extern unsigned int g_dcmf_barrier_num; /**< num. of available barrier protocols */

extern DCMF_CollectiveProtocol_t *g_dcmf_localbarrier[G_DCMF_BARRIER_PROTO_NUM];
extern unsigned int g_dcmf_localbarrier_num; /**< num. of available local barrier protocols */

/** Register DCMF barrier protocols */
void gasnete_coll_barrier_proto_register();

#endif /* GASNET_COLL_BARRIER_DCMF_H_ */

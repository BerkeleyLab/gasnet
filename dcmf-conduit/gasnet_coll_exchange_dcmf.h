/* $Source: /Users/kamil/work/gasnet-cvs2/gasnet/dcmf-conduit/gasnet_coll_exchange_dcmf.h,v $
 * $Date: 2009/09/18 05:04:13 $
 * $Revision: 1.2.2.2 $
 * Description: GASNet exchange (alltoall) implementation for DCMF
 * LBNL 2009
 */

#ifndef GASNET_COLL_EXCHANGE_DCMF_H_
#define GASNET_COLL_EXCHANGE_DCMF_H_

#include <gasnet_extended_coll_dcmf.h>

typedef enum {
  TORUS_ALLTOALLV=0,
  G_DCMF_A2A_PROTO_NUM 
} gasnete_dcmf_a2a_proto_t;

/** data structure for storing information of a dcmf alltoall
    operation */
typedef struct {
  DCMF_CollectiveProtocol_t  *registration;
  DCMF_CollectiveRequest_t request;
  DCMF_Callback_t cb_done;
  DCMF_Consistency consistency;
  DCMF_Geometry_t *geometry;
  char *sndbuf;
  unsigned *sndlens;
  unsigned *sdispls;
  char *rcvbuf;
  unsigned *rcvlens;
  unsigned *rdispls;
  unsigned *sndcounters;
  unsigned *rcvcounters;
  unsigned active; /**< active flag of the operation */
} gasnete_dcmf_a2a_data_t;

/** alltoall protocol registration */
void gasnete_coll_a2a_proto_register();

/** initialize the data structure for an alltoall communication */
void gasnete_coll_dcmf_a2a_init(gasnete_dcmf_a2a_data_t *a2a,
                                DCMF_CollectiveProtocol_t *registration,
                                DCMF_Geometry_t *geometry,
                                int nprocs, 
                                void *src, 
                                void *dst,
                                size_t nbytes);

/** clean up (finialize) the data structure for an alltoall
    communication */
void gasnete_coll_dcmf_a2a_fini(gasnete_dcmf_a2a_data_t *a2a);

/** Non-blocking version of gasnete_coll_exchange */
gasnet_coll_handle_t gasnete_coll_exchange_nb_dcmf(gasnet_team_handle_t team, 
                                                   void *dst, void *src, 
                                                   size_t nbytes, int flags, 
                                                   uint32_t sequence
                                                   GASNETE_THREAD_FARG);


/** Blocking version of gasnete_coll_exchange */
void gasnete_coll_exchange_dcmf(gasnet_team_handle_t team, 
                                void *dst, void *src, 
                                size_t nbytes, int flags, 
                                uint32_t sequence
                                GASNETE_THREAD_FARG);

#endif /* GASNET_COLL_EXCHANGE_DCMF_H_ */

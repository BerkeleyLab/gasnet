/* $Source: /Users/kamil/work/gasnet-cvs2/gasnet/dcmf-conduit/gasnet_coll_bcast_dcmf.h,v $
 * $Date: 2009/07/07 00:00:49 $
 * $Revision: 1.1.4.2 $
 * Description: GASNet broadcast implementation on DCMF
 * LBNL 2009
 */

#ifndef GASNET_COLL_BCAST_DCMF_H_
#define GASNET_COLL_BCAST_DCMF_H_

#include <gasnet_extended_coll_dcmf.h>

/**
 * DCMF broadcast algorithms 
 */
typedef enum {
  TREE_BROADCAST,
  TORUS_RECTANGLE_BROADCAST,
  TORUS_BINOMIAL_BROADCAST,
  TORUS_ASYNCBROADCAST_RECTANGLE,
  TORUS_ASYNCBROADCAST_BINOMIAL,
  G_DCMF_BCAST_PROTO_NUM 
} gasnete_dcmf_bcast_kind_t;

/** 
 * Data structure for storing information of a dcmf broadcast operation
 */
typedef struct {
  union {
    DCMF_CollectiveRequest_t coll;
    DCMF_Request_t global;
  } request;
  DCMF_Callback_t cb_done;
  DCMF_Consistency consistency;
  DCMF_Geometry_t *geometry;
  unsigned root;
  char *src;
  char *dst;
  unsigned bytes;
  volatile unsigned active;
  gasnete_dcmf_bcast_kind_t kind;
  gasnet_team_handle_t team;
} gasnete_dcmf_bcast_data_t;

/** 
 * Register broadcast protocols.  It should be done before calling any
 * gasnet dcmf broacast function.
 */
void gasnete_coll_bcast_proto_register();

/**
 * Wrapper of gasnete_coll_bcast_dcmf_nb for the default gasnet
 * broadcast function.
 */
gasnet_coll_handle_t 
gasnete_coll_broadcast_nb_dcmf(gasnet_team_handle_t team,
                               void *dst,
                               gasnet_image_t srcimage, 
                               void *src,
                               size_t nbytes, 
                               int flags,
                               /* gasnete_coll_implementation_t coll_params, */
                               uint32_t sequence
                               GASNETE_THREAD_FARG);

/**
 * Non-Blocking version of broadcast, which provides better latency.
 */
gasnet_coll_handle_t 
gasnete_coll_bcast_nb_dcmf(gasnet_team_handle_t team,
                           void *dst,
                           gasnet_image_t srcimage, 
                           void *src,
                           size_t nbytes, int flags,
                           uint32_t sequence,
                           gasnete_dcmf_bcast_kind_t bcast_proto
                           GASNETE_THREAD_FARG);

/**
 * Wrapper of gasnete_coll_bcast_dcmf for the default gasnet broadcast function
 */
void gasnete_coll_broadcast_dcmf(gasnet_team_handle_t team,
                                 void *dst,
                                 gasnet_image_t srcimage, void *src,
                                 size_t nbytes, int flags GASNETE_THREAD_FARG);

/**
 * Blocking version of broadcast, which provides better latency.
 */
void gasnete_coll_bcast_dcmf(gasnet_team_handle_t team, void *dst,
                             gasnet_image_t srcimage, void *src,
                             size_t nbytes, int flags,
                             gasnete_dcmf_bcast_kind_t kind
                             GASNETE_THREAD_FARG);



/**
 * print out the internal information about the broadcast operation 
 */
void gasnete_coll_dcmf_bcast_print(FILE *fp, gasnete_dcmf_bcast_data_t *bcast);

extern enum gasnete_coll_proto_dcmf_t gasnet_dcmf_bcast_proto;

extern unsigned int g_dcmf_bcast_enabled[G_DCMF_BCAST_PROTO_NUM];

#endif /* GASNET_COLL_BCAST_DCMF_H_ */

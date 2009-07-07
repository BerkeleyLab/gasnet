/* $Source: /Users/kamil/work/gasnet-cvs2/gasnet/dcmf-conduit/gasnet_extended_coll_dcmf.h,v $
 * $Date: 2009/07/07 00:00:49 $
 * $Revision: 1.1.4.2 $
 * Description: GASNet extended collectives implementation on DCMF
 * LBNL 2009
 */

#ifndef GASNET_EXTENDED_COLL_DCMF_H_
#define GASNET_EXTENDED_COLL_DCMF_H_

#include <gasnet_core_internal.h>
#include <gasnet_extended_refcoll.h>
#include <gasnet_coll.h>
#include <gasnet_coll_internal.h>
#include <gasnet_coll_autotune_internal.h>

#include <gasnet_coll_barrier_dcmf.h>

/* The following function prototypes should be declared in
 * gasnet_extended_refcoll.h, however they only appear in
 * gasnet_extended_refcoll.c.  Therefore, they are included here for
 * referencing them in the dcmf collective code correctly. */
gasnet_coll_handle_t
gasnete_coll_exchange_nb_default(gasnet_team_handle_t team,
                                 void *dst, void *src,
                                 size_t nbytes, int flags, uint32_t sequence
                                 GASNETE_THREAD_FARG);


gasnet_coll_handle_t
gasnete_coll_broadcast_nb_default(gasnet_team_handle_t team,
                                  void *dst,
                                  gasnet_image_t srcimage, void *src,
                                  size_t nbytes, int flags, uint32_t sequence
                                  GASNETE_THREAD_FARG);

/* gasnet_coll_handle_t */
/* gasnete_coll_op_generic_init_with_scratch(gasnete_coll_team_t team, int flags, */
/*                                           gasnete_coll_generic_data_t *data,  */
/*                                           gasnete_coll_poll_fn poll_fn, */
/*                                           uint32_t sequence,  */
/*                                           gasnete_coll_scratch_req_t *scratch_req  */
/*                                           GASNETE_THREAD_FARG); */

extern gasnet_coll_handle_t
gasnete_coll_op_generic_init_with_scratch(gasnete_coll_team_t team, int flags,
                                          gasnete_coll_generic_data_t *data, 
                                          gasnete_coll_poll_fn poll_fn,
                                          uint32_t sequence, 
                                          gasnete_coll_scratch_req_t *scratch_req, 
                                          int num_params, 
                                          uint32_t *param_list, 
                                          gasnete_coll_tree_data_t *tree_info 
                                          GASNETE_THREAD_FARG);
/* end of adhoc function prototype declarations */

/**
 * data structure for storing dcmf team information  
 */
typedef struct {
  /* struct gasnete_coll_team_t_ baseteam; */
  DCMF_CollectiveRequest_t barrier_req;
  DCMF_Geometry_t geometry;
  volatile unsigned in_barrier;
} gasnete_coll_team_dcmf_t;  

extern int gasnete_coll_dcmf_inited;

/**
 * Initialize DCMF collective protocols. This should be called only once
 * when creating the default team GASNET_TEAM_ALL in gasnete_coll_init().
 */
void gasnete_coll_init_dcmf();

/**
 * Release the resources used by DCMF collective protocols
 */
void gasnete_coll_fini_dcmf();

/**
 * Initialize the dcmf data structures for gasnet team
 */
void gasnete_coll_team_init_dcmf(gasnet_team_handle_t team);
void gasnete_dcmf_team_init(gasnet_team_handle_t team,
                            gasnete_dcmf_barrier_proto_t barrier_kind,
                            gasnete_dcmf_barrier_proto_t lbarrier_kind);

/**
 * Finalize the dcmf data structures for gasnet team
 */
void gasnete_coll_team_fini_dcmf(gasnet_team_handle_t team);

/**
 * Get the DCMF geometry of the team with team_id.
 */
DCMF_Geometry_t * gasnete_dcmf_get_geometry(int team_id);

#define PRINT_ARRAY(fp, A, size, format)        \
  do {                                          \
    int i;                                      \
    for(i=0; i<(size); i++)                     \
      {                                         \
        fprintf((fp), "%s[%d]=", #A, i);        \
        fprintf((fp), format, (A)[i]);          \
        fprintf((fp), "\n");                    \
      }                                         \
    fprintf((fp), "\n");                        \
  } while(0);

#endif /* GASNET_EXTENDED_COLL_DCMF_H_ */

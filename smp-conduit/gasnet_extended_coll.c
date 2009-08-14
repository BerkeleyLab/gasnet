/*
 *  gasnet_extended_coll.c
 *  autotune_xcode
 *
 *  Created by Rajesh Nishtala on 7/27/09.
 *  Copyright 2009 __MyCompanyName__. All rights reserved.
 *
 */

#include <gasnet_extended_internal.h>
#include <gasnet_coll_internal.h>
#include <gasnet_coll_autotune_internal.h>
#include <../smp-collectives/smp_coll.h>
#include <../smp-collectives/smp_coll_bcast_scatter_gather.c>

#define GASNETE_COLL_EVERY_IN_SYNC_FLAG GASNET_COLL_IN_NOSYNC | GASNET_COLL_IN_MYSYNC | GASNET_COLL_IN_ALLSYNC 
#define GASNETE_COLL_EVERY_OUT_SYNC_FLAG GASNET_COLL_OUT_NOSYNC | GASNET_COLL_OUT_MYSYNC | GASNET_COLL_OUT_ALLSYNC 
#define GASNETE_COLL_EVERY_SYNC_FLAG GASNETE_COLL_EVERY_IN_SYNC_FLAG | GASNETE_COLL_EVERY_OUT_SYNC_FLAG

#if GASNETE_COLL_CONDUIT_COLLECTIVES
gasnet_coll_handle_t gasnete_coll_smp_bcast_flat(gasnet_team_handle_t team,
                                            void * const dstlist[],
                                            gasnet_image_t srcimage, void *src,
                                            size_t nbytes, int flags, 
                                            gasnete_coll_implementation_t coll_params, 
                                            uint32_t sequence
                                            GASNETE_THREAD_FARG) {
  
  gasnete_coll_threaddata_t *td = GASNETE_COLL_MYTHREAD;
  if(!(flags & GASNET_COLL_IN_NOSYNC)) smp_coll_barrier(td->smp_coll_handle,0);
  /*regardless of SINGLE or LOCAL dstlist contains as many addresses as the number of images*/
  if(td->my_local_image == srcimage) {
    gasnete_coll_local_broadcast(team->my_images, dstlist, src, nbytes); 
  }
  if(!(flags & GASNET_COLL_OUT_NOSYNC)) smp_coll_barrier(td->smp_coll_handle,0);
  return GASNET_COLL_INVALID_HANDLE;
}

gasnet_coll_handle_t gasnete_coll_smp_bcast_tree_intflags(gasnet_team_handle_t team,
                                            void * const dstlist[],
                                            gasnet_image_t srcimage, void *src,
                                            size_t nbytes, int flags, 
                                            gasnete_coll_implementation_t coll_params, 
                                            uint32_t sequence
                                            GASNETE_THREAD_FARG) {
  
  gasnete_coll_threaddata_t *td = GASNETE_COLL_MYTHREAD;
  gasneti_assert(coll_params->num_params >= 1);
  if(!(flags & GASNET_COLL_IN_NOSYNC)) smp_coll_barrier(td->smp_coll_handle,0);
  smp_coll_broadcast_tree_flag(td->smp_coll_handle, team->my_images, dstlist, src, 
                                 nbytes, flags, coll_params->param_list[0]);
  if(!(flags & GASNET_COLL_OUT_NOSYNC)) smp_coll_barrier(td->smp_coll_handle,0);
  return GASNET_COLL_INVALID_HANDLE;
}

void gasnete_coll_register_conduit_collectives(gasnete_coll_autotune_info_t* info) {
#ifdef GASNETE_COLL_BROADCAST_SMP_FLAT
  info->collective_algorithms[GASNET_COLL_BROADCASTM_OP][GASNETE_COLL_BROADCAST_SMP_FLAT] =
    gasnete_coll_autotune_register_algorithm(info->team, GASNET_COLL_BROADCASTM_OP, GASNETE_COLL_EVERY_SYNC_FLAG,
                                             0 /*works for all addresses since it's just a memcpy on a the local node*/,
                                             0, 0, 0, 0, NULL, 
                                             (void*) gasnete_coll_smp_bcast_flat, "SMP_BCAST_FLAT");
#endif

#ifdef GASNETE_COLL_BROADCAST_SMP_TREE_INTFLAGS
  {
    struct gasnet_coll_tuning_parameter_t tuning_params[1]=
    { 
      {GASNETE_COLL_SMP_COLL_TREE_RADIX, 2, MAX(2,info->team->my_images), 2, GASNET_COLL_TUNING_STRIDE_MULTIPLY}
    }; 
    
  info->collective_algorithms[GASNET_COLL_BROADCASTM_OP][GASNETE_COLL_BROADCAST_SMP_TREE_INTFLAGS] =
    gasnete_coll_autotune_register_algorithm(info->team, GASNET_COLL_BROADCASTM_OP, GASNETE_COLL_EVERY_SYNC_FLAG,
                                             0 /*works for all addresses since it's just a memcpy on a the local node*/,
                                             0, 0, 0, 1, tuning_params, 
                                             (void*) gasnete_coll_smp_bcast_tree_intflags, "SMP_BCAST_TREE_INTFLAGS");
  }
#endif
}
#endif

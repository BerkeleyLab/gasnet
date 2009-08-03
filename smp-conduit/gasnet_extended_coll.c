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

#define GASNETE_COLL_EVERY_IN_SYNC_FLAG GASNET_COLL_IN_NOSYNC | GASNET_COLL_IN_MYSYNC | GASNET_COLL_IN_ALLSYNC 
#define GASNETE_COLL_EVERY_OUT_SYNC_FLAG GASNET_COLL_OUT_NOSYNC | GASNET_COLL_OUT_MYSYNC | GASNET_COLL_OUT_ALLSYNC 
#define GASNETE_COLL_EVERY_SYNC_FLAG GASNETE_COLL_EVERY_IN_SYNC_FLAG | GASNETE_COLL_EVERY_OUT_SYNC_FLAG

#if GASNETE_COLL_CONDUIT_COLLECTIVES
gasnet_coll_handle_t gasnete_coll_smp_bcast(gasnet_team_handle_t team,
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
  if(!(flags & GASNET_COLL_IN_NOSYNC)) smp_coll_barrier(td->smp_coll_handle,0);
  return GASNET_COLL_INVALID_HANDLE;
}

void gasnete_coll_register_conduit_collectives(gasnete_coll_autotune_info_t* info) {
  info->collective_algorithms[GASNET_COLL_BROADCASTM_OP][GASNETE_COLL_BROADCAST_SMP] =
    gasnete_coll_autotune_register_algorithm(info->team, GASNET_COLL_BROADCASTM_OP, GASNETE_COLL_EVERY_SYNC_FLAG,
                                             0 /*works for all addresses since it's just a memcpy on a the local node*/,
                                             0, 0, 0, 0, NULL, 
                                             (void*) gasnete_coll_smp_bcast, "SMP_BCAST");
  
}
#endif

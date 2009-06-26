/*this file contains the implementation of hte DCMF specific conduit collectives*/

#include <gasnet_internal.h>

#include <gasnet_extended_internal.h>
#include <gasnet_handler.h>

#include <gasnet_core_internal.h>

#include <gasnet_coll.h>
#include <gasnet_coll_autotune.h>
#include <gasnet_coll_internal.h>
#include <gasnet_coll_autotune_internal.h>

#if GASNETE_COLL_CONDUIT_COLLECTIVES
#define DCMF_COLLECTIVE_CONSISTENCY DCMF_MATCH_CONSISTENCY


static DCMF_Protocol_t DCMF_GlobalBroadcast_registration;
static DCMF_CollectiveProtocol_t DCMF_Broadcast_registration;


static void gasnete_coll_inc_int_cb(void* arg, DCMF_Error_t* e) {
  volatile int *in = (volatile int*) arg;
  (*in)++;
}

static int gasnete_coll_pf_bcast_dcmf_tree(gasnete_coll_op_t *op GASNETE_THREAD_FARG) {
  gasnete_coll_generic_data_t *data = op->data;
  const gasnete_coll_broadcast_args_t *args = GASNETE_COLL_GENERIC_ARGS(data, broadcast);
  int result = 0;
  
  switch(data->state) {
  case 0:	/* Optional IN barrier 
             (this will eventually call the gasnet level barrier which already uses the DCMF barrier)
          */
    if (!gasnete_coll_generic_all_threads(data) ||
        !gasnete_coll_generic_insync(data)) {
      break;
    } 
    data->state++;
  case 1:
    if(!GASNETE_COLL_MAY_INIT_FOR(op)) break;
    /*initiate a DCMF level broadcast and have the callback advance the state*/
    {
      if(op->team->total_ranks > 1) {
        DCMF_Callback_t cb_done;

        gasnetc_dcmf_req_t *dcmf_coll_req = gasnetc_get_dcmf_req();

        cb_done.function = gasnete_coll_inc_int_cb;
        cb_done.clientdata = (void*) &(data->state);
        data->private_data = (void*) dcmf_coll_req;
        
        data->state++;
        /*advance the state before calling broadcast to prevent a race*/
        gasneti_assert(op->team == GASNET_TEAM_ALL);
        
        GASNETC_DCMF_LOCK();

        DCMF_SAFE(DCMF_GlobalBcast(&DCMF_GlobalBroadcast_registration,
                                   &dcmf_coll_req->req,
                                   cb_done,
                                   DCMF_COLLECTIVE_CONSISTENCY,
                                   args->srcnode,
                                   (op->team->myrank == args->srcnode ? args->src : args->dst),
                                   args->nbytes));

        GASNETC_DCMF_UNLOCK();
        if(op->team->myrank == args->srcnode) {
          GASNETE_FAST_UNALIGNED_MEMCPY(args->dst, args->src, args->nbytes);
        }
      } else {
        GASNETE_FAST_UNALIGNED_MEMCPY(args->dst, args->src, args->nbytes);
        data->state+=2; break;
      }
    }
  case 2:
    /*waiting for the DCMF Broadcast to finish nothing to do while we wait
      the callback will increment the state
      
    */
    break;
  case 3:
    /*DCMF Broadcast is complete so free the request*/
    {
      gasnetc_free_dcmf_req((gasnetc_dcmf_req_t*) data->private_data);
    }
    data->state++;
  case 4:
    if (!gasnete_coll_generic_outsync(data)) {
      break;
    }
    printf("%d> DCMF GlobalBCAST FINISHED!\n", gasneti_mynode);
    gasnete_coll_generic_free(data GASNETE_THREAD_PASS);
    result = (GASNETE_COLL_OP_COMPLETE | GASNETE_COLL_OP_INACTIVE);
  }
  return result;
}

extern gasnet_coll_handle_t
gasnete_coll_bcast_dcmf_tree(gasnet_team_handle_t team,
                       void * dst,
                       gasnet_image_t srcimage, void *src,
                       size_t nbytes, int flags,
                       gasnete_coll_implementation_t coll_params,
                       uint32_t sequence
                       GASNETE_THREAD_FARG)
{
  int options = GASNETE_COLL_GENERIC_OPT_INSYNC_IF ((flags & GASNET_COLL_IN_ALLSYNC)) |
    GASNETE_COLL_GENERIC_OPT_OUTSYNC_IF((flags & GASNET_COLL_OUT_ALLSYNC));
  
  printf("%d> DCMF GlobalBCAST!\n", gasneti_mynode);
  return gasnete_coll_generic_broadcast_nb(team, dst, srcimage, src, nbytes, flags,
                                           &gasnete_coll_pf_bcast_dcmf_tree, options, NULL, sequence,
                                           coll_params->num_params, coll_params->param_list GASNETE_THREAD_PASS);
}


static int gasnete_coll_pf_bcast_dcmf(gasnete_coll_op_t *op GASNETE_THREAD_FARG) {
  gasnete_coll_generic_data_t *data = op->data;
  const gasnete_coll_broadcast_args_t *args = GASNETE_COLL_GENERIC_ARGS(data, broadcast);
  int result = 0;
  
  switch(data->state) {
  case 0:	/* Optional IN barrier 
             (this will eventually call the gasnet level barrier which already uses the DCMF barrier)
          */
    if (!gasnete_coll_generic_all_threads(data) ||
        !gasnete_coll_generic_insync(data)) {
      break;
    } 
    data->state++;
  case 1:
    if(!GASNETE_COLL_MAY_INIT_FOR(op)) break;
    /*initiate a DCMF level broadcast and have the callback advance the state*/
    {
      if(op->team->total_ranks > 1) {
        DCMF_Callback_t cb_done;

        gasnetc_dcmf_coll_req_t *dcmf_coll_req = gasnetc_get_dcmf_coll_req();

        cb_done.function = gasnete_coll_inc_int_cb;
        cb_done.clientdata = (void*) &(data->state);
        data->private_data = dcmf_coll_req;
        
        data->state++;
        /*advance the state before calling broadcast to prevent a race*/
        gasneti_assert(op->team == GASNET_TEAM_ALL);
        
        GASNETC_DCMF_LOCK();
        DCMF_SAFE(DCMF_Broadcast(&DCMF_Broadcast_registration,
                                 &dcmf_coll_req->req,
                                 cb_done, DCMF_COLLECTIVE_CONSISTENCY,
                                 (DCMF_Geometry_t*)op->team->dcmf_geom, args->srcnode,
                                 (op->team->myrank == args->srcnode ? args->src : args->dst),
                                 args->nbytes));

        GASNETC_DCMF_UNLOCK();
        if(op->team->myrank == args->srcnode) {
          GASNETE_FAST_UNALIGNED_MEMCPY(args->dst, args->src, args->nbytes);
        }
      } else {
        GASNETE_FAST_UNALIGNED_MEMCPY(args->dst, args->src, args->nbytes);
        data->state+=2; break;
      }
    }
  case 2:
    /*waiting for the DCMF Broadcast to finish nothing to do while we wait
      the callback will increment the state
    */
    break;
  case 3:
    /*DCMF Broadcast is complete so free the request*/
    {
      gasnetc_free_dcmf_coll_req((gasnetc_dcmf_coll_req_t*) data->private_data);

    }
    data->state++;
  case 4:
    if (!gasnete_coll_generic_outsync(data)) {
      break;
    }
    printf("%d> DCMF BCAST FINISHED!\n", gasneti_mynode);
    gasnete_coll_generic_free(data GASNETE_THREAD_PASS);
    result = (GASNETE_COLL_OP_COMPLETE | GASNETE_COLL_OP_INACTIVE);
  }
  return result;
}

extern gasnet_coll_handle_t
gasnete_coll_bcast_dcmf(gasnet_team_handle_t team,
                       void * dst,
                       gasnet_image_t srcimage, void *src,
                       size_t nbytes, int flags,
                       gasnete_coll_implementation_t coll_params,
                       uint32_t sequence
                       GASNETE_THREAD_FARG)
{
  int options = GASNETE_COLL_GENERIC_OPT_INSYNC_IF ((flags & GASNET_COLL_IN_ALLSYNC)) |
    GASNETE_COLL_GENERIC_OPT_OUTSYNC_IF((flags & GASNET_COLL_OUT_ALLSYNC));
  
  printf("%d> DCMF BCAST!\n", gasneti_mynode);
  return gasnete_coll_generic_broadcast_nb(team, dst, srcimage, src, nbytes, flags,
                                           &gasnete_coll_pf_bcast_dcmf, options, NULL, sequence,
                                           coll_params->num_params, coll_params->param_list GASNETE_THREAD_PASS);
}


static DCMF_Geometry_t *getGeometry (int comm)
{
  /**XXX once teams are added this needs to change and we need to keep track of the geometries
     we create. FOR NOW just pass in the DCMF GEOM**/
  gasneti_assert(comm == 0);
  return GASNET_TEAM_ALL->dcmf_geom;
}


static DCMF_CollectiveProtocol_t bar_reg __attribute__((__aligned__(32))), local_bar_reg __attribute__((__aligned__(32)));
void gasnete_coll_init_dcmf_geom(gasnete_coll_team_t team) {
  unsigned int *ranks;
  DCMF_CollectiveProtocol_t *barrier_p, *local_barrier_p;
  gasnetc_dcmf_coll_req_t *geom_req = gasnetc_get_dcmf_coll_req();
  DCMF_Barrier_Configuration_t configuration;
  int i;
  
  
  gasneti_assert(team == GASNET_TEAM_ALL);
  gasneti_assert(team->dcmf_geom == NULL);
  ranks = gasneti_malloc(sizeof(int)*team->total_ranks);
  for(i=0; i<team->total_ranks; i++) {
    ranks[i] = i; /*XXX translate to actual node here*/
  }
  
  GASNETC_DCMF_LOCK();
  configuration.cb_geometry = getGeometry;
  configuration.protocol =  DCMF_GI_BARRIER_PROTOCOL ;        /**< Global Interrupt barrier. */
  
  barrier_p = &bar_reg;
  DCMF_SAFE(DCMF_Barrier_register(barrier_p, &configuration));
  
  
  configuration.protocol = DCMF_LOCKBOX_BARRIER_PROTOCOL;
  local_barrier_p = &local_bar_reg;
  DCMF_SAFE(DCMF_Barrier_register(local_barrier_p, &configuration));

  
  team->dcmf_geom = gasneti_malloc(sizeof(DCMF_Geometry_t));
  DCMF_SAFE(DCMF_Geometry_initialize((DCMF_Geometry_t*) team->dcmf_geom,
                                     0,
                                     ranks,
                                     team->total_ranks,
                                     &barrier_p,1,
                                     &local_barrier_p,1,
                                     (DCMF_CollectiveRequest_t*) geom_req->req, 0, (team == GASNET_TEAM_ALL)));
  GASNETC_DCMF_UNLOCK();
  gasneti_free(ranks);
    
}


void gasnete_coll_register_conduit_collectives(gasnete_coll_autotune_info_t* info) {
  /*this function is called from gasnet_coll_autotune_init 
    which is called by the first image to call coll_init so no re-entrancy 
    problems here
  */
  static gasneti_mutex_t init_lock = GASNETI_MUTEX_INITIALIZER;
  static volatile int init_done = 0;
  
  GASNETC_DCMF_LOCK();
  gasneti_mutex_lock(&init_lock);
  if(!init_done) {
    DCMF_SAFE(DCMF_Collective_initialize());
    init_done = 1;
  } 
  gasneti_mutex_unlock(&init_lock);
  GASNETC_DCMF_UNLOCK();
  
  

  gasnete_coll_init_dcmf_geom(info->team);


  /*register the broadcast protocol and save it*/

 {
   DCMF_GlobalBcast_Configuration_t config;
   config.protocol = DCMF_TREE_GLOBALBCAST_PROTOCOL;
   GASNETC_DCMF_LOCK();
   DCMF_SAFE(DCMF_GlobalBcast_register(&DCMF_GlobalBroadcast_registration, &config));
   GASNETC_DCMF_UNLOCK();
 }

 {
    DCMF_Broadcast_Configuration_t config;
    config.protocol = DCMF_TREE_BROADCAST_PROTOCOL;
    GASNETC_DCMF_LOCK();
    DCMF_SAFE(DCMF_Broadcast_register(&DCMF_Broadcast_registration, &config));
    GASNETC_DCMF_UNLOCK();
  }

  
  /*add the hardware collective op into the list of viable ops*/ 
 info->collective_algorithms[GASNET_COLL_BROADCAST_OP][GASNETE_COLL_BROADCAST_DCMF_TREE] = 
   gasnete_coll_autotune_register_algorithm(info->team, GASNET_COLL_BROADCAST_OP,
                                            GASNET_COLL_IN_NOSYNC | 
                                            GASNET_COLL_IN_MYSYNC | 
                                            GASNET_COLL_IN_ALLSYNC |
                                            GASNET_COLL_OUT_NOSYNC | 
                                            GASNET_COLL_OUT_MYSYNC | 
                                            GASNET_COLL_OUT_ALLSYNC,
                                            0, 0, 0, 0, 0, NULL,
                                            (void*) gasnete_coll_bcast_dcmf_tree,
                                            "BROADCAST_DCMF_TREE");

 info->collective_algorithms[GASNET_COLL_BROADCAST_OP][GASNETE_COLL_BROADCAST_DCMF] = 
    gasnete_coll_autotune_register_algorithm(info->team, GASNET_COLL_BROADCAST_OP,
                                             GASNET_COLL_IN_NOSYNC | 
                                             GASNET_COLL_IN_MYSYNC | 
                                             GASNET_COLL_IN_ALLSYNC |
                                             GASNET_COLL_OUT_NOSYNC | 
                                             GASNET_COLL_OUT_MYSYNC | 
                                             GASNET_COLL_OUT_ALLSYNC,
                                             0, 0, 0, 0, 0, NULL,
                                             (void*) gasnete_coll_bcast_dcmf,
                                             "BROADCAST_DCMF");
  

  
    
}
  
#endif

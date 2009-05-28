/* $Source: /Users/kamil/work/gasnet-cvs2/gasnet/dcmf-conduit/gasnet_extended_coll_dcmf.c,v $
 * $Date: 2009/05/28 22:17:51 $
 * $Revision: 1.1.2.1 $
 * Description: GASNet extended collectives implementation on DCMF
 * LBNL 2009
 */

#include <gasnet_extended_coll_dcmf.h>
#include <gasnet_coll_exchange_dcmf.h>
#include <gasnet_coll_bcast_dcmf.h>
#include <gasnet_coll_barrier_dcmf.h>

/* #define G_DCMF_TRACE */

int gasnete_coll_dcmf_inited = 0;

DCMF_Geometry_t * gasnete_dcmf_get_geometry(int team_id)
{
  gasnet_team_handle_t team = gasnete_coll_team_lookup(team_id);
  gasnete_coll_team_dcmf_t *dcmf_tp = (gasnete_coll_team_dcmf_t *)team->dcmf_tp;
#ifdef G_DCMF_TRACE
  fprintf(stderr, "gasnete_dcmf_get_geometry: geometry %p \n", 
          &dcmf_tp->geometry);
#endif
  return &dcmf_tp->geometry;
}
  
/* see mpich2/src/mpid/dcmfd/src/comm/collselect/mpid_coll.c */
void gasnete_coll_init_dcmf()
{
  static gasneti_mutex_t init_lock = GASNETI_MUTEX_INITIALIZER;
  
#ifdef G_DCMF_TRACE
  fprintf(stderr, "gasnete_coll_init_dcmf is executed!\n");
#endif
  
  gasneti_mutex_lock(&init_lock);
  if(gasnete_coll_dcmf_inited) {
    gasneti_mutex_unlock(&init_lock);
    return;
  }
  
  DCMF_Collective_initialize();
  
  gasnete_coll_barrier_proto_register();
  gasnete_coll_bcast_proto_register();
  gasnete_coll_a2a_proto_register();
  
  gasnete_coll_dcmf_inited = 1;
  gasneti_mutex_unlock(&init_lock);
}

void gasnete_coll_fini_dcmf()
{
#ifdef G_DCMF_TRACE
  fprintf(stderr, "gasnete_coll_fini_dcmf is executed!\n");
#endif
  gasneti_assert(gasnete_coll_dcmf_inited == 1);
}

static gasnete_coll_team_dcmf_t * gasnete_coll_team_dcmf_new()
{
  void *p;

#ifdef G_DCMF_TRACE
  fprintf(stderr, "gasnete_coll_team_dcmf_new is executed!\n");
#endif 
  p = gasneti_malloc(sizeof(gasnete_coll_team_dcmf_t));
  gasneti_assert(p != NULL);
  return (gasnete_coll_team_dcmf_t *)p;
}

static void gasnete_coll_team_dcmf_delete(gasnete_coll_team_dcmf_t * dcmf_tp)
{
#ifdef G_DCMF_TRACE
  fprintf(stderr, "gasnete_coll_team_dcmf_delete is executed!\n");
#endif 
  gasneti_free(dcmf_tp);
}

void gasnete_coll_team_init_dcmf(gasnet_team_handle_t team)
{
  DCMF_Result rv;
  gasnete_coll_team_dcmf_t *dcmf_tp;
  
#ifdef G_DCMF_TRACE
  fprintf(stderr, "gasnete_coll_team_init_dcmf is executed!\n");
#endif
  
  if (gasnete_coll_dcmf_inited == 0)
    gasnete_coll_init_dcmf();
  
  /* initialize the base gasnet team object */
  /* gasnete_coll_team_init_default(team); */
  
  dcmf_tp = gasnete_coll_team_dcmf_new();
  team->dcmf_tp = dcmf_tp;
  
#ifdef G_DCMF_TRACE
  if (team->myrank == 0)
    {
      fprintf(stderr, "team->team_id %d\n", team->team_id);
      fprintf(stderr, "team->myrank %d\n", team->myrank);
      fprintf(stderr, "team->total_ranks %d\n", team->total_ranks);
      {
        int i;
        fprintf(stderr, "team->ranks: ");
        for (i=0;i<team->total_ranks;i++)
          fprintf(stderr, "%d ", team->ranks[i]);
        fprintf(stderr, "\n");
      }
      fprintf(stderr, "g_coll_dcmf_num_barrier %d\n", g_coll_dcmf_num_barrier);
      fprintf(stderr, "g_coll_dcmf_num_localbarrier %d\n", g_coll_dcmf_num_localbarrier);
      fprintf(stderr, "&dcmf_tp->geometry %x\n", &dcmf_tp->geometry);
      fprintf(stderr, "&dcmf_tp->barrier_req %x\n", &dcmf_tp->barrier_req);
    }
#endif
  
  /* Initialize the dcmf-specific data members in the team object */
  rv = (DCMF_Result)DCMF_Geometry_initialize (&dcmf_tp->geometry,
                                              team->team_id, 
                                              team->ranks, 
                                              team->total_ranks,
                                              g_dcmf_barrier,
                                              g_dcmf_barrier_num,
                                              g_dcmf_localbarrier,
                                              g_dcmf_localbarrier_num,
                                              &dcmf_tp->barrier_req, 
                                              1, /* number of (bcast) colors */
                                              (team == GASNET_TEAM_ALL)); /* is globalcontext? */
                                              
  if(rv != DCMF_SUCCESS) {
    gasneti_fatalerror("DCMF_Geometry_initialize failed! %d\n", rv);
  }
}

void gasnete_coll_team_fini_dcmf(gasnet_team_handle_t  team)
{
  gasnete_coll_team_dcmf_t *dcmf_tp = (gasnete_coll_team_dcmf_t *)team->dcmf_tp;
  
#ifdef G_DCMF_TRACE
  fprintf(stderr, "gasnete_coll_team_fini_dcmf is executed!\n");
#endif
  
  /* clean up DCMF_Geometry */
  DCMF_Geometry_free(&dcmf_tp->geometry);
  gasnete_coll_team_dcmf_delete(dcmf_tp);
}

 
#if 0 /* from Rajesh's autotuning */
void gasnete_coll_register_conduit_collectives(gasnete_coll_autotune_info_t* info) 
{
  /*   this function is called from gasnet_coll_autotune_init  */
  /*     which is called by the first image to call coll_init so no re-entrancy  */
  /*     problems here */
 
  /*     add the hardware collective op into the list of viable ops  */
  if (g_coll_proto_dcmf_enabled[TREE_BROADCAST])
    {
      uint32_t flag;
        
      flag = GASNET_COLL_IN_NOSYNC | GASNET_COLL_IN_MYSYNC
        | GASNET_COLL_IN_ALLSYNC | GASNET_COLL_OUT_NOSYNC
        | GASNET_COLL_OUT_MYSYNC | GASNET_COLL_OUT_ALLSYNC;
        
      info->collective_algorithms[GASNET_COLL_BROADCAST_OP][GASNETE_COLL_BROADCAST_DCMF_TREE] =
        gasnete_coll_autotune_register_algorithm(GASNET_COLL_BROADCAST_OP,
                                                 flag, 0, 0, 0, 0, 0, NULL,
                                                 (void*)gasnete_coll_bcast_dcmf_tree);
    }
    
  info->collective_algorithms[GASNET_COLL_BROADCAST_OP][GASNETE_COLL_BROADCAST_DCMF] =
    gasnete_coll_autotune_register_algorithm(GASNET_COLL_BROADCAST_OP,
                                             GASNET_COLL_IN_NOSYNC |
                                             GASNET_COLL_IN_MYSYNC |
                                             GASNET_COLL_IN_ALLSYNC |
                                             GASNET_COLL_OUT_NOSYNC |
                                             GASNET_COLL_OUT_MYSYNC |
                                             GASNET_COLL_OUT_ALLSYNC,
                                             0, 0, 0, 0, 0, NULL,
                                             (void*) gasnete_coll_bcast_dcmf);
}
#endif

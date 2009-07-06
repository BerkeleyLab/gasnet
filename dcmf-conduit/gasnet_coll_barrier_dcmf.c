/* $Source: /Users/kamil/work/gasnet-cvs2/gasnet/dcmf-conduit/gasnet_coll_barrier_dcmf.c,v $
 * $Date: 2009/07/06 07:48:29 $
 * $Revision: 1.1.2.3 $
 * Description: GASNet barrier implementation on DCMF
 * LBNL 2009
 */

#include <gasnet_extended_coll_dcmf.h>

/* barrier protocol registration data */
DCMF_CollectiveProtocol_t g_dcmf_barrier_proto[G_DCMF_BARRIER_PROTO_NUM];
unsigned int g_dcmf_barrier_enabled[G_DCMF_BARRIER_PROTO_NUM];

DCMF_CollectiveProtocol_t *g_dcmf_barrier[G_DCMF_BARRIER_PROTO_NUM];
unsigned int g_dcmf_barrier_num; /**< num. of available barrier
                                    protocols */

DCMF_CollectiveProtocol_t *g_dcmf_localbarrier[G_DCMF_BARRIER_PROTO_NUM];
unsigned int g_dcmf_localbarrier_num; /**< num. of available local
                                         barrier protocols */

gasnete_dcmf_barrier_proto_t g_dcmf_barrier_kind_default;
gasnete_dcmf_barrier_proto_t g_dcmf_lbarrier_kind_default;

/* Global barrier protocol registration data */
/* static DCMF_Protocol_t g_dcmf_globalbarrier_proto[G_DCMF_GLOBALBARRIER_PROTO_NUM]; */
/* unsigned int g_dcmf_globalbarrier_enabled[G_DCMF_GLOBALBARRIER_PROTO_NUM]; */


/* callback function for DCMF_Barrier() */
static void gasnete_dcmf_coll_cb_done(void *clientdata, DCMF_Error_t *e)
{
  unsigned *p = (unsigned *)clientdata;
  *p = 0;
}

void gasnete_coll_barrier_proto_register()
{
  DCMF_Result rv;
  DCMF_Barrier_Configuration_t barrier_conf;
  char *tmp_str;

  GASNETC_DCMF_LOCK(); /* for DCMF_SAFE */

  g_dcmf_barrier_num = 0;
  g_dcmf_localbarrier_num = 0;

  /* global interrupt barrier */
  g_dcmf_barrier_enabled[GI_BARRIER] = 
    gasneti_getenv_yesno_withdefault("GASNET_DCMF_GI_BARRIER", 1);
  if (g_dcmf_barrier_enabled[GI_BARRIER])
    {
      barrier_conf.protocol = DCMF_GI_BARRIER_PROTOCOL;
      barrier_conf.cb_geometry = gasnete_dcmf_get_geometry;
      DCMF_SAFE(DCMF_Barrier_register(&g_dcmf_barrier_proto[GI_BARRIER], 
                                      &barrier_conf));
      g_dcmf_barrier[g_dcmf_barrier_num] = &g_dcmf_barrier_proto[GI_BARRIER];
      g_dcmf_barrier_num++;
    }

  /* torus rectangle barrier */
  g_dcmf_barrier_enabled[TORUS_RECTANGLE_BARRIER] = 
    gasneti_getenv_yesno_withdefault("GASNET_DCMF_TORUS_RECTANGLE_BARRIER", 1);
  if (g_dcmf_barrier_enabled[TORUS_RECTANGLE_BARRIER])
    {
      barrier_conf.protocol = DCMF_TORUS_RECTANGLE_BARRIER_PROTOCOL;
      barrier_conf.cb_geometry = gasnete_dcmf_get_geometry;
      DCMF_SAFE(DCMF_Barrier_register(&g_dcmf_barrier_proto[TORUS_RECTANGLE_BARRIER], 
                                      &barrier_conf));
      g_dcmf_barrier[g_dcmf_barrier_num] = 
        &g_dcmf_barrier_proto[TORUS_RECTANGLE_BARRIER];
      g_dcmf_barrier_num++;
    }

  /* torus rectangle lockbox barrier */
  g_dcmf_barrier_enabled[TORUS_RECTANGLE_LB_BARRIER] = 
    gasneti_getenv_yesno_withdefault("GASNET_DCMF_TORUS_RECTANGLE_LB_BARRIER", 1);
  if (g_dcmf_barrier_enabled[TORUS_RECTANGLE_LB_BARRIER])
    {
      barrier_conf.protocol = DCMF_TORUS_RECTANGLELOCKBOX_BARRIER_PROTOCOL_SINGLETH;
      barrier_conf.cb_geometry = gasnete_dcmf_get_geometry;
      DCMF_SAFE(DCMF_Barrier_register(&g_dcmf_barrier_proto[TORUS_RECTANGLE_LB_BARRIER], 
                                      &barrier_conf));
      g_dcmf_barrier[g_dcmf_barrier_num] = 
        &g_dcmf_barrier_proto[TORUS_RECTANGLE_LB_BARRIER];
      g_dcmf_barrier_num++;
    }

  /* torus binomial barrier */
  g_dcmf_barrier_enabled[TORUS_BINOMIAL_BARRIER] = 
    gasneti_getenv_yesno_withdefault("GASNET_DCMF_TORUS_BINOMIAL_BARRIER", 1);
  if (g_dcmf_barrier_enabled[TORUS_BINOMIAL_BARRIER])
    {
      barrier_conf.protocol = DCMF_TORUS_BINOMIAL_BARRIER_PROTOCOL;
      barrier_conf.cb_geometry = gasnete_dcmf_get_geometry;
      DCMF_SAFE(DCMF_Barrier_register(&g_dcmf_barrier_proto[TORUS_BINOMIAL_BARRIER], 
                                      &barrier_conf));
      g_dcmf_barrier[g_dcmf_barrier_num] = 
        &g_dcmf_barrier_proto[TORUS_BINOMIAL_BARRIER];
      g_dcmf_barrier_num++;
    }
  
  /* lockbox barrier */
  g_dcmf_barrier_enabled[LOCKBOX_BARRIER] = 
    gasneti_getenv_yesno_withdefault("GASNET_DCMF_LOCKBOX_BARRIER", 1);
  if (g_dcmf_barrier_enabled[LOCKBOX_BARRIER])
    {
      barrier_conf.protocol = DCMF_LOCKBOX_BARRIER_PROTOCOL;
      DCMF_SAFE(DCMF_Barrier_register(&g_dcmf_barrier_proto[LOCKBOX_BARRIER], 
                                      &barrier_conf));
      g_dcmf_localbarrier[g_dcmf_localbarrier_num] = 
        &g_dcmf_barrier_proto[LOCKBOX_BARRIER];
      g_dcmf_localbarrier_num++;
    }

  gasneti_assert(g_dcmf_barrier_num > 0);
  gasneti_assert(g_dcmf_localbarrier_num > 0);

  /* select a default team barrier algorithm */
  tmp_str = gasneti_getenv("GASNET_DCMF_BARRIER_PROTO");
  if (tmp_str == NULL) 
    g_dcmf_barrier_kind_default = TORUS_BINOMIAL_BARRIER;
  else if (!strcmp(tmp_str, "GI_BARRIER"))
    g_dcmf_barrier_kind_default = GI_BARRIER; 
  else if (!strcmp(tmp_str, "TORUS_RECTANGLE_BARRIER"))
    g_dcmf_barrier_kind_default = TORUS_RECTANGLE_BARRIER;
  else if (!strcmp(tmp_str, "TORUS_RECTANGLE_LB_BARRIER"))
    g_dcmf_barrier_kind_default = TORUS_RECTANGLE_LB_BARRIER;
  else if (!strcmp(tmp_str, "TORUS_BINOMIAL_BARRIER"))
    g_dcmf_barrier_kind_default = TORUS_BINOMIAL_BARRIER;
  else 
    g_dcmf_barrier_kind_default = TORUS_BINOMIAL_BARRIER;
  
  g_dcmf_lbarrier_kind_default = LOCKBOX_BARRIER;

  GASNETC_DCMF_UNLOCK();
}

void gasnete_coll_teambarrier_dcmf(gasnet_team_handle_t team)
{
  gasnete_coll_teambarrier_notify_dcmf(team);
  gasnete_coll_teambarrier_wait_dcmf(team);
}

void gasnete_coll_teambarrier_notify_dcmf(gasnet_team_handle_t team)
{
  gasnete_coll_team_dcmf_t *dcmf_tp = (gasnete_coll_team_dcmf_t *)team->dcmf_tp;
  DCMF_Callback_t cb_done;
  
  gasneti_sync_reads();
  
  cb_done.function = gasnete_dcmf_coll_cb_done;
  cb_done.clientdata = (void *)&(dcmf_tp->in_barrier);

  GASNETC_DCMF_LOCK();
  dcmf_tp->in_barrier = 1;
  DCMF_SAFE(DCMF_Barrier(&dcmf_tp->geometry, cb_done, DCMF_MATCH_CONSISTENCY));
  GASNETC_DCMF_UNLOCK();
  
  gasneti_sync_writes();
}

void gasnete_coll_teambarrier_wait_dcmf(gasnet_team_handle_t team)
{
  gasnete_coll_team_dcmf_t *dcmf_tp = (gasnete_coll_team_dcmf_t *)team->dcmf_tp;
  
  gasneti_sync_reads();
  
  GASNETC_DCMF_LOCK();
  while (dcmf_tp->in_barrier)
    DCMF_Messager_advance();
  GASNETC_DCMF_UNLOCK();
  
  gasneti_sync_writes();
}

int gasnete_coll_teambarrier_try_dcmf(gasnet_team_handle_t team)
{
  gasnete_coll_team_dcmf_t *dcmf_tp = (gasnete_coll_team_dcmf_t *)team->dcmf_tp;
 
  GASNETC_DCMF_LOCK();
  DCMF_Messager_advance();
  GASNETC_DCMF_UNLOCK();
  
  return (dcmf_tp->in_barrier);
}

/* $Source: /Users/kamil/work/gasnet-cvs2/gasnet/dcmf-conduit/gasnet_coll_barrier_dcmf.c,v $
 * $Date: 2009/05/28 22:17:51 $
 * $Revision: 1.1.2.1 $
 * Description: GASNet barrier implementation on DCMF
 * LBNL 2009
 */

#include <gasnet_coll_barrier_dcmf.h>

/* barrier protocol registration data */
static DCMF_CollectiveProtocol_t g_dcmf_barrier_proto[G_DCMF_BARRIER_PROTO_NUM];
unsigned int g_dcmf_barrier_enabled[G_DCMF_BARRIER_PROTO_NUM];

DCMF_CollectiveProtocol_t *g_dcmf_barrier[G_DCMF_BARRIER_PROTO_NUM];
unsigned int g_dcmf_barrier_num; /**< num. of available barrier
                                    protocols */

DCMF_CollectiveProtocol_t *g_dcmf_localbarrier[G_DCMF_BARRIER_PROTO_NUM];
unsigned int g_dcmf_localbarrier_num; /**< num. of available local
                                         barrier protocols */

/* Global barrier protocol registration data */
/* static DCMF_Protocol_t g_dcmf_globalbarrier_proto[G_DCMF_GLOBALBARRIER_PROTO_NUM]; */
/* unsigned int g_dcmf_globalbarrier_enabled[G_DCMF_GLOBALBARRIER_PROTO_NUM]; */

void gasnete_coll_barrier_proto_register()
{
  DCMF_Result rv;
  DCMF_Barrier_Configuration_t barrier_conf;
  
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

  GASNETC_DCMF_UNLOCK();
}

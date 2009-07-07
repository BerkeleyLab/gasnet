/* $Source: /Users/kamil/work/gasnet-cvs2/gasnet/dcmf-conduit/gasnet_coll_bcast_dcmf.c,v $
 * $Date: 2009/07/07 17:11:59 $
 * $Revision: 1.1.4.3 $
 * Description: GASNet broadcast implementation on DCMF
 * LBNL 2009
 */

#include <string.h>

#include <gasnet_coll_bcast_dcmf.h>

// #define G_DCMF_COLL_TRACE

/* Broadcast protocol registration data */
static DCMF_CollectiveProtocol_t g_dcmf_bcast_proto[G_DCMF_BCAST_PROTO_NUM];
unsigned int g_dcmf_bcast_enabled[G_DCMF_BCAST_PROTO_NUM];

/* Global Broadcast protocol registration data */
static DCMF_Protocol_t g_dcmf_globalbcast_proto; 

gasnete_dcmf_bcast_kind_t g_dcmf_bcast_kind_default;

void gasnete_coll_bcast_proto_register()
{
  DCMF_GlobalBcast_Configuration_t gbcast_conf;
  DCMF_Broadcast_Configuration_t bcast_conf;
  DCMF_AsyncBroadcast_Configuration_t a_bcast_conf;
  char *tmp_str;

  GASNETC_DCMF_LOCK(); /* for DCMF_SAFE */
  
  /* tree broadcast registration */
  g_dcmf_bcast_enabled[TREE_BROADCAST] =
    gasneti_getenv_yesno_withdefault("GASNET_DCMF_TREE_BROADCAST", 1);
  if (g_dcmf_bcast_enabled[TREE_BROADCAST]) 
    {
      gbcast_conf.protocol = DCMF_TREE_GLOBALBCAST_PROTOCOL;
      DCMF_SAFE(DCMF_GlobalBcast_register(&g_dcmf_globalbcast_proto, 
                                          &gbcast_conf));
      bcast_conf.protocol = DCMF_TREE_BROADCAST_PROTOCOL;
      DCMF_SAFE(DCMF_Broadcast_register(&g_dcmf_bcast_proto[TREE_BROADCAST], 
                                        &bcast_conf));
    }
  
  /* torus broadcast registration */
  g_dcmf_bcast_enabled[TORUS_RECTANGLE_BROADCAST] =
    gasneti_getenv_yesno_withdefault("GASNET_DCMF_TORUS_RECTANGLE_BROADCAST", 1);
  if (g_dcmf_bcast_enabled[TORUS_RECTANGLE_BROADCAST]) 
    {
      bcast_conf.protocol = DCMF_TORUS_RECTANGLE_BROADCAST_PROTOCOL;
      DCMF_SAFE(DCMF_Broadcast_register(&g_dcmf_bcast_proto[TORUS_RECTANGLE_BROADCAST],
                                        &bcast_conf));
    }
  
  g_dcmf_bcast_enabled[TORUS_BINOMIAL_BROADCAST] =
    gasneti_getenv_yesno_withdefault("GASNET_DCMF_TORUS_BINOMIAL_BROADCAST", 1);
  if (g_dcmf_bcast_enabled[TORUS_BINOMIAL_BROADCAST])
    {
      bcast_conf.protocol = DCMF_TORUS_BINOMIAL_BROADCAST_PROTOCOL;
      DCMF_SAFE(DCMF_Broadcast_register(&g_dcmf_bcast_proto[TORUS_BINOMIAL_BROADCAST],
                                        &bcast_conf));
    }
  
  /* asynchronous broadcast registration */
  g_dcmf_bcast_enabled[TORUS_ASYNCBROADCAST_RECTANGLE] =
    gasneti_getenv_yesno_withdefault("GASNET_DCMF_TORUS_ASYNCBROADCAST_RECTANGLE_BROADCAST", 1);
  if (g_dcmf_bcast_enabled[TORUS_ASYNCBROADCAST_RECTANGLE])
    {
      a_bcast_conf.protocol = DCMF_TORUS_ASYNCBROADCAST_RECTANGLE_PROTOCOL;
      a_bcast_conf.isBuffered = 1;
      a_bcast_conf.cb_geometry = gasnete_dcmf_get_geometry;
      DCMF_SAFE(DCMF_AsyncBroadcast_register(&g_dcmf_bcast_proto[TORUS_ASYNCBROADCAST_RECTANGLE],
                                             &a_bcast_conf));
    }

  g_dcmf_bcast_enabled[TORUS_ASYNCBROADCAST_BINOMIAL] =
    gasneti_getenv_yesno_withdefault("GASNET_DCMF_TORUS_ASYNCBROADCAST_BINOMIAL_BROADCAST", 1);
  if (g_dcmf_bcast_enabled[TORUS_ASYNCBROADCAST_BINOMIAL])
    {
      a_bcast_conf.protocol = DCMF_TORUS_ASYNCBROADCAST_BINOMIAL_PROTOCOL;
      a_bcast_conf.isBuffered = 1;
      a_bcast_conf.cb_geometry = gasnete_dcmf_get_geometry;
      DCMF_SAFE(DCMF_AsyncBroadcast_register(&g_dcmf_bcast_proto[TORUS_ASYNCBROADCAST_BINOMIAL],
                                             &a_bcast_conf));
    }

  /* select a default broadcast algorithm */
  tmp_str = gasneti_getenv("GASNET_DCMF_BCAST_PROTO");
  if (tmp_str == NULL)
    g_dcmf_bcast_kind_default = TORUS_BINOMIAL_BROADCAST;
  else if (!strcmp(tmp_str, "TREE_BROADCAST"))
    g_dcmf_bcast_kind_default = TREE_BROADCAST; 
  else if (!strcmp(tmp_str, "TORUS_RECTANGLE_BROADCAST"))
    g_dcmf_bcast_kind_default = TORUS_RECTANGLE_BROADCAST;
  else if (!strcmp(tmp_str, "TORUS_BINOMIAL_BROADCAST"))
    g_dcmf_bcast_kind_default = TORUS_BINOMIAL_BROADCAST;
  else /* default protocol */
    g_dcmf_bcast_kind_default = TORUS_BINOMIAL_BROADCAST;

  GASNETC_DCMF_UNLOCK(); 
}

static void gasnete_dcmf_bcast_cb_done(void *clientdata, DCMF_Error_t *e)
{
  unsigned *p = (unsigned *)clientdata;
  *p = 0;
}

static void gasnete_coll_dcmf_bcast_init(gasnete_dcmf_bcast_data_t *bcast,
                                         gasnet_team_handle_t team,
                                         gasnete_dcmf_bcast_kind_t kind,
                                         DCMF_Geometry_t *geometry,
                                         unsigned root,
                                         char *src,
                                         char *dst,
                                         unsigned bytes)
{
#ifdef G_DCMF_COLL_TRACE
  fprintf(stderr, "gasnete_coll_dcmf_bcast_init is executed.\n");
#endif
  bcast->team = team;
  bcast->kind = kind;
  bcast->geometry = geometry;
  bcast->cb_done.function = gasnete_dcmf_bcast_cb_done;
  bcast->cb_done.clientdata = (void *)&bcast->active;
  bcast->active = 1;
  bcast->consistency = DCMF_MATCH_CONSISTENCY;
  bcast->root = root;
  bcast->src = src; 
  bcast->dst = dst;
  bcast->bytes = bytes;
}

/* This function should be called by only one thread at a time. */
static int gasnete_coll_pf_bcast_dcmf(gasnete_coll_op_t *op GASNETE_THREAD_FARG) 
{
  gasnete_coll_generic_data_t *data = op->data;
  gasnet_team_handle_t team = op->team;
  gasnete_dcmf_bcast_data_t *bcast = (gasnete_dcmf_bcast_data_t *)data->private_data;
  static unsigned dcmf_busy=0; /* only 1 DCMF collective op can be in flight */

#ifdef G_DCMF_COLL_TRACE
  fprintf(stderr, "gasnete_coll_pf_bcast_dcmf is executed.\n");
#endif

  switch (data->state) {
  case 0:	
    /* DCMF_Broadcast has an explicit barrier for all processes at the
     * beginning.
     */
    /* if (!gasnete_coll_generic_all_threads(data)) */
    /*                 break; */
    if (dcmf_busy)
      break;
    data->state = 1;
    
  case 1:	/* Initiate data movement */
    dcmf_busy = 1;  
    if (bcast->kind==TREE_BROADCAST && team==GASNET_TEAM_ALL)
      {
        GASNETC_DCMF_LOCK();
        DCMF_SAFE(DCMF_GlobalBcast(&g_dcmf_globalbcast_proto,
                                   &bcast->request.global, 
                                   bcast->cb_done,
                                   DCMF_MATCH_CONSISTENCY,
                                   team->rel2act_map[bcast->root],
                                   (team->myrank==bcast->root) ? bcast->src : bcast->dst, 
                                   bcast->bytes));
        GASNETC_DCMF_UNLOCK();
      }
    else
      {
        GASNETC_DCMF_LOCK();
        DCMF_SAFE(DCMF_Broadcast(&g_dcmf_bcast_proto[bcast->kind], 
                                 &bcast->request.coll, 
                                 bcast->cb_done,
                                 bcast->consistency,
                                 bcast->geometry, 
                                 team->rel2act_map[bcast->root],
                                 (team->myrank==bcast->root) ? bcast->src : bcast->dst, 
                                 bcast->bytes)); 
        GASNETC_DCMF_UNLOCK();
      }
    data->state = 2;
    
  case 2:	/* Sync data movement */
    if (bcast->active)
      break;
    dcmf_busy = 0;
    data->state = 3;
    
  case 3: 
    if(team->myrank == bcast->root) 
      GASNETE_FAST_UNALIGNED_MEMCPY(bcast->dst, bcast->src, bcast->bytes);
    
    if (!gasnete_coll_generic_outsync(op->team, data))
      break;
    
    /* clean up storage space */
    gasneti_free(bcast);
    gasnete_coll_generic_free(op->team, data GASNETE_THREAD_PASS);
    
    return (GASNETE_COLL_OP_COMPLETE | GASNETE_COLL_OP_INACTIVE);

  defaul:
    gasneti_fatalerror("gasnete_coll_pf_bcast_dcmf: invalid state %d\n", data->state);
  } /* end of switch (data->state) */
  
  return 0;
}

gasnet_coll_handle_t gasnete_coll_bcast_nb_dcmf(gasnet_team_handle_t team,
                                                void * dst,
                                                gasnet_image_t srcimage, 
                                                void *src,
                                                size_t nbytes, 
                                                int flags,
                                                uint32_t sequence,
                                                gasnete_dcmf_bcast_kind_t kind
                                                GASNETE_THREAD_FARG)

{
  gasnete_dcmf_bcast_data_t *bcast;
  gasnete_coll_team_dcmf_t *dcmf_tp = (gasnete_coll_team_dcmf_t *)team->dcmf_tp;
  const unsigned root  = gasnete_coll_image_node(team, srcimage);
  gasnete_coll_generic_data_t *data;
    
#ifdef G_DCMF_COLL_TRACE
  fprintf(stderr, "gasnete_coll_bcast_nb_dcmf is executed. kind: %u\n",
          kind);
#endif

  gasneti_assert(team != NULL);
  gasneti_assert(dcmf_tp != NULL);
  gasneti_assert(gasnete_coll_dcmf_inited == 1);
  gasneti_assert(!(flags & GASNETE_COLL_SUBORDINATE));
  switch(kind)
    {
    case TREE_BROADCAST:
      if (team != GASNET_TEAM_ALL)
        gasneti_fatalerror("Tree broadcast protocol must be used with GASNET_TEAM_ALL!\n");
    case TORUS_RECTANGLE_BROADCAST:
    case TORUS_BINOMIAL_BROADCAST:
      break;
    default:
      gasneti_fatalerror("kind (%d) is an invalid broadcast protocol!\n",
                         kind);
    }
  
  /* Check the pre-conditions of using the broadcast collective.  If
   * not, use the default broadcast implementation when available.
   */
  if(!g_dcmf_bcast_enabled[kind])
    {
#ifdef G_DCMF_COLL_TRACE
      fprintf(stderr,"g_dcmf_bcast_enabled[%d] = %u\n", 
              kind, g_dcmf_bcast_enabled[kind]);
#endif
      gasneti_fatalerror("The DCMF broadcast protocol (%d) is not enabled!\n.",
                         kind);
    }  
  
  if (!DCMF_Geometry_analyze(&dcmf_tp->geometry, &g_dcmf_bcast_proto[kind]))
    {
      gasneti_fatalerror("The DCMF broadcast protocol (%d) is not supported on the calling team!\n.",
                         kind);
      // gasneti_fatalerror("gasnete_coll_broadcast_nb_default is called but it doesn't work!\n");
      /*       return gasnete_coll_broadcast_nb_default(team, dst, srcimage, src,  */
      /*                                                nbytes, flags, sequence */
      /*                                                GASNETE_THREAD_PASS); */
    }
          
  data = gasnete_coll_generic_alloc(GASNETE_THREAD_PASS_ALONE);
  gasneti_assert(data != NULL);
  GASNETE_COLL_GENERIC_SET_TAG(data, broadcast);
  data->options=GASNETE_COLL_GENERIC_OPT_OUTSYNC_IF(flags & GASNET_COLL_OUT_ALLSYNC);
  
  /* initialize DCMF specific data structures */
  bcast = (gasnete_dcmf_bcast_data_t *)gasneti_malloc(sizeof(gasnete_dcmf_bcast_data_t));
  gasnete_coll_dcmf_bcast_init(bcast, team, kind, &dcmf_tp->geometry,
                               root, src, dst, nbytes);
  data->private_data = bcast;
  
#ifdef G_DCMF_COLL_TRACE
  gasnete_coll_dcmf_bcast_print(stderr, bcast);
#endif

  return gasnete_coll_op_generic_init_with_scratch(team, flags, data, 
                                                   gasnete_coll_pf_bcast_dcmf, 
                                                   sequence, NULL, 0, NULL, NULL
                                                   GASNETE_THREAD_PASS);
}

gasnet_coll_handle_t 
gasnete_coll_broadcast_nb_dcmf(gasnet_team_handle_t team,
                               void * dst,
                               gasnet_image_t srcimage, 
                               void *src,
                               size_t nbytes, 
                               int flags,
                               /* gasnete_coll_implementation_t coll_params, */
                               uint32_t sequence
                               GASNETE_THREAD_FARG)
{
  gasnete_coll_team_dcmf_t *dcmf_tp = (gasnete_coll_team_dcmf_t *)team->dcmf_tp;
  gasnete_dcmf_bcast_kind_t kind = g_dcmf_bcast_kind_default;
  
  /* check if the prefered broadcast protocol is available, 
     if not then use the default protocol */
  if (!g_dcmf_bcast_enabled[kind] 
      || !(DCMF_Geometry_analyze(&dcmf_tp->geometry, 
                                 &g_dcmf_bcast_proto[kind])))
    {
      kind = TORUS_BINOMIAL_BROADCAST;
      gasneti_assert(g_dcmf_bcast_enabled[kind]);
      gasneti_assert(DCMF_Geometry_analyze(&dcmf_tp->geometry, 
                                           &g_dcmf_bcast_proto[kind]));
    }
  
#ifdef G_DCMF_COLL_TRACE
  fprintf(stderr, "gasnete_coll_bcast_dcmf_default: kind =%d\n", kind);
#endif
  
  return  gasnete_coll_bcast_nb_dcmf(team, dst, srcimage, src, nbytes, flags, 
                                     sequence, kind GASNETE_THREAD_PASS);
}

void gasnete_coll_bcast_dcmf(gasnet_team_handle_t team, void *dst,
                             gasnet_image_t srcimage, void *src,
                             size_t nbytes, int flags,
                             gasnete_dcmf_bcast_kind_t kind
                             GASNETE_THREAD_FARG)
{
  gasnete_coll_team_dcmf_t *dcmf_tp = (gasnete_coll_team_dcmf_t *)team->dcmf_tp;
  const unsigned root = gasnete_coll_image_node(team, srcimage);
  DCMF_Callback_t cb_done;
  volatile unsigned active;
    
  gasneti_assert(team != NULL);
  gasneti_assert(gasnete_coll_dcmf_inited == 1);
  gasneti_assert(!(flags & GASNETE_COLL_SUBORDINATE));
  
#ifdef G_DCMF_COLL_TRACE
  fprintf(stderr, "rank %d, gasnete_coll_broadcast_dcmf is executed. kind: %u\n",
          team->myrank, kind);
#endif
  
  switch(kind)
    {
    case TREE_BROADCAST:
      if (team != GASNET_TEAM_ALL)
        gasneti_fatalerror("Tree broadcast protocol must be used with GASNET_TEAM_ALL!\n");

    case TORUS_RECTANGLE_BROADCAST:
    case TORUS_BINOMIAL_BROADCAST:
      break;
    default:
      gasneti_fatalerror("kind (%d) is an invalid broadcast protocol!\n",
                         kind);
    }
  
  if(!g_dcmf_bcast_enabled[kind])
    gasneti_fatalerror("The broadcast type (%d) is not enabled at startup!\n",
                       kind);
  

  active = 1;
  cb_done.function = gasnete_dcmf_bcast_cb_done;
  cb_done.clientdata = (void *)&active;
  
  if (kind==TREE_BROADCAST && team==GASNET_TEAM_ALL)
    {
      DCMF_Request_t  request;
      
      /* Is is really necessary to lock ? */
      GASNETC_DCMF_LOCK();
      DCMF_SAFE(DCMF_GlobalBcast(&g_dcmf_globalbcast_proto,
                                 &request, 
                                 cb_done,
                                 DCMF_MATCH_CONSISTENCY,
                                 team->rel2act_map[root],
                                 (team->myrank == root) ? src : dst,
                                 nbytes));
      
      while (active)
        DCMF_Messager_advance();

      GASNETC_DCMF_UNLOCK();
    } 
  else 
    {
      DCMF_CollectiveRequest_t   request;
      
      if (!DCMF_Geometry_analyze(&dcmf_tp->geometry, &g_dcmf_bcast_proto[kind]))
        gasneti_fatalerror("The current team (geometry) doesn't support the broadcast type %d!\n",
                           kind);
      
      /* Is is really necessary to lock ? */
      GASNETC_DCMF_LOCK();
      DCMF_Broadcast(&g_dcmf_bcast_proto[kind],
                     &request,
                     cb_done,
                     DCMF_MATCH_CONSISTENCY,
                     &dcmf_tp->geometry,
                     team->rel2act_map[root],
                     (team->myrank == root) ? src : dst,
                     nbytes);

      while (active)
        DCMF_Messager_advance();

      GASNETC_DCMF_UNLOCK();
    }
  
  if (team->myrank == root)
    GASNETE_FAST_UNALIGNED_MEMCPY(dst, src, nbytes);

  /* optional barrier for the out_allsync mode, should be a team barrier */
  if (flags & GASNET_COLL_OUT_ALLSYNC)
    gasnetc_dcmf_bootstrapBarrier();
}

void gasnete_coll_broadcast_dcmf(gasnet_team_handle_t team, void *dst,
                                 gasnet_image_t srcimage, void *src,
                                 size_t nbytes, int flags GASNETE_THREAD_FARG)
{
  gasnete_coll_team_dcmf_t *dcmf_tp = (gasnete_coll_team_dcmf_t *)team->dcmf_tp;
  gasnete_dcmf_bcast_kind_t kind = g_dcmf_bcast_kind_default;;
  
  /* check if the prefered broadcast protocol is available, 
     if not then use the default protocol */
  if (!g_dcmf_bcast_enabled[kind] 
      || !(DCMF_Geometry_analyze(&dcmf_tp->geometry, 
                                 &g_dcmf_bcast_proto[kind])))
    {
      kind = TORUS_BINOMIAL_BROADCAST;
      gasneti_assert(g_dcmf_bcast_enabled[kind]);
      gasneti_assert(DCMF_Geometry_analyze(&dcmf_tp->geometry, 
                                           &g_dcmf_bcast_proto[kind]));
    }
  
#ifdef G_DCMF_COLL_TRACE
  fprintf(stderr, "gasnete_coll_broadcast_dcmf: kind =%d\n", kind);
#endif
  
  gasnete_coll_bcast_dcmf(team, dst, srcimage, src, nbytes, flags, kind 
                          GASNETE_THREAD_PASS);
}

void gasnete_coll_dcmf_bcast_print(FILE *fp, gasnete_dcmf_bcast_data_t *bcast)
{
  gasneti_assert(bcast != NULL);
  fprintf(stderr, "%d, bcast->kind %d\n", 
          bcast->team->myrank, bcast->kind);
  fprintf(stderr, "%d, bcast->root %u\n", bcast->team->myrank, bcast->root);
  fprintf(stderr, "%d, bcast->src %p\n", bcast->team->myrank, bcast->src);
  fprintf(stderr, "%d, bcast->bytes %d\n", bcast->team->myrank, bcast->bytes);
}

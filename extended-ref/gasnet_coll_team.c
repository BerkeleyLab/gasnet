/* $Source: /Users/kamil/work/gasnet-cvs2/gasnet/extended-ref/gasnet_coll_team.c,v $
 * $Date: 2009/07/06 07:48:26 $
 * $Revision: 1.1.2.1 $
 * Description: GASNet generic team implementation for collectives 
 * LBNL 2009
 */

#include <gasnet_internal.h>
#include <gasnet_coll.h>
#include <gasnet_coll_internal.h>
#include <gasnet_extended_refcoll.h>

#include <gasnet_coll_team.h>

/* #define DEBUG_TEAM */

HashTable_T *team_dir = NULL;

volatile uint32_t my_team_seq = 1;
volatile uint32_t new_team_id = 0; /* new_team_id is for communication
                                      between the AM handler
                                      (gasnete_coll_teamid_reqh) and
                                      the main thread, 0 means the new
                                      team id is not set. */

void gasnete_coll_team_init(gasnet_team_handle_t team, 
                            uint32_t team_id, 
                            uint32_t total_ranks,
                            gasnet_node_t myrank,
                            gasnet_node_t *rel2act_map)
{
#ifdef DEBUG_TEAM
  fprintf(stderr, "gasnete_coll_team_init: team %p, team_id %x, total_ranks %u, myrank %u\n", 
          team, team_id, total_ranks, myrank);
  fflush(stderr);
  if (myrank == 0) {
    PRINT_ARRAY(stderr, rel2act_map, total_ranks, "%u");
    fflush(stderr);
  }
#endif

  uint32_t i;
  team->team_id = team_id;
  team->total_ranks = total_ranks;
  team->myrank = myrank;
  team->rel2act_map = (gasnet_node_t *)gasneti_malloc(sizeof(gasnet_node_t)*total_ranks);
  gasneti_assert(team->rel2act_map != NULL);
  for (i=0; i<total_ranks; i++) 
    team->rel2act_map[i] = rel2act_map[i];

  /* lock the team direcotry (team_dir) */
  /* add the new team to the directory */
  if (team_dir == NULL) {
    team_dir = HashTable_create(TEAM_DIR_SIZE);
    gasneti_assert(team_dir != NULL);
  }
  HashTable_insert(team_dir, team_id, team);

#ifdef gasnete_coll_team_init_conduit
  /* conduit specific initialization for gasnet teams */
#ifdef DEBUG_TEAM
  fprintf(stderr, "gasnete_coll_team_init: calling gasnete_coll_team_init_conduit.\n");
  fflush(stderr);
#endif
  gasnete_coll_team_init_conduit(team);
#endif
  /* unlock */
}

void gasnete_coll_team_fini(gasnet_team_handle_t team)
{
  gasneti_assert(team != NULL);
  /* free data members of the team, such as scratch space and etc. */
  gasneti_free(team->rel2act_map);
  gasneti_assert(team_dir != NULL);
  HashTable_remove(team_dir, team->team_id, NULL);

#ifdef gasnete_coll_team_fini_conduit
  /* conduit specific initialization for gasnet teams */
#ifdef DEBUG_TEAM
  fprintf(stderr, "gasnete_coll_team_fini: calling gasnete_coll_team_fini_conduit.\n");
  fflush(stderr);
#endif
  gasnete_coll_team_fini_conduit(team);
#endif
}

void gasnete_coll_teamid_reqh(gasnet_token_t token,
                              gasnet_handlerarg_t team_id)
{
  new_team_id=(uint32_t)team_id;
#ifdef DEBUG_TEAM
  fprintf(stderr, "gasnete_coll_teamid_reqh: new_team_id %x\n", new_team_id);
  fflush(stderr);
#endif
}

/* collective function that should be called by all participating nodes */
gasnet_team_handle_t gasnete_coll_team_create(uint32_t total_ranks,
                                              gasnet_node_t myrank,
                                              gasnet_node_t *rel2act_map)
{
  gasnet_team_handle_t team;
  gasnet_node_t team_lead = rel2act_map[0];

#ifdef DEBUG_TEAM
  fprintf(stderr, "gasnete_coll_team_create: team_lead %u, total_ranks %u, myrank %u\n", team_lead, total_ranks, myrank);
  fflush(stderr);
  if (myrank == 0) {
    PRINT_ARRAY(stderr, rel2act_map, total_ranks, "%u");
    fflush(stderr);
  }
#endif

  /* need to lock for thread safety */

  if (myrank == 0) {
    /* the team leader (rank 0) computes the new team_id */
    /* gasneti_atomic_increment(&(my_team_seq), GASNETI_ATOMIC_NONE); */
    my_team_seq++; /* need to be an atomic operation */
    /* limitation: each root node can only allocate team sequence id
       4096 times */
    gasneti_assert(my_team_seq < 0xfff);
    new_team_id = ((team_lead << 12) | (my_team_seq & 0xfff));
    
    /* create the team locally */
    team = (gasnet_team_handle_t)gasneti_malloc(sizeof(struct gasnete_coll_team_t_));
    gasneti_assert(team != NULL);
    gasnete_coll_team_init(team, new_team_id, total_ranks, myrank, rel2act_map);
        
    /* send out team_id */
    for(uint32_t i=1; i<total_ranks; i++) {
      GASNETI_SAFE(SHORT_REQ(1,1,(rel2act_map[i],
                                  gasneti_handleridx(gasnete_coll_teamid_reqh),
                                  new_team_id)));
    }
  } else {
    /* wait for team_id from the team leader */
    while (new_team_id == 0)
      gasneti_AMPoll();

#ifdef DEBUG_TEAM
    fprintf(stderr, "myrank %u, get new_team_id %x\n", myrank, new_team_id);
    fflush(stderr);
#endif
    /* create the team locally */
    team = (gasnet_team_handle_t)gasneti_malloc(sizeof(struct gasnete_coll_team_t_));
    gasneti_assert(team != NULL);
    gasnete_coll_team_init(team, new_team_id, total_ranks, myrank, rel2act_map);
    new_team_id = 0;
  }
  
  /* unlock */
#ifdef DEBUG_TEAM
  gasnete_print_team(team, stderr);
#endif

  return team;
}

void gasnete_coll_team_free(gasnet_team_handle_t team)
{
  gasneti_assert(team != NULL);
  gasnete_coll_team_fini(team);
  gasneti_free(team);
}

gasnet_team_handle_t gasnete_coll_team_split(gasnet_team_handle_t team,
                                             gasnet_node_t mycolor,
                                             gasnet_node_t myrelrank,
                                             void *clientdata
                                             GASNETE_THREAD_FARG)
{
  gasnet_team_handle_t newteam;
  uint32_t new_total_ranks;
  gasnet_node_t *colors; /* gasnet_image_t for PAR mode*/
  gasnet_node_t *relranks; /* gasnet_image_t for PAR mode */
  gasnet_node_t *rel2act_map;

#ifdef DEBUG_TEAM
  fprintf(stderr, "gasnete_coll_team_split: team rank %u, parent team handle %p, mycolor %u, myrank %u\n",
          team->myrank, team, mycolor, myrelrank);
  fflush(stderr);
#endif

  colors = (gasnet_node_t *)gasneti_malloc(sizeof(mycolor)*team->total_ranks);
  gasneti_assert(colors != NULL);
  relranks = (gasnet_node_t *)gasneti_malloc(sizeof(myrelrank)*team->total_ranks);
  gasneti_assert(relranks != NULL);

  /* collect the color information */
  gasnete_coll_gather_all(team, colors, &mycolor, sizeof(gasnet_node_t), 0 GASNETE_THREAD_PASS);
  gasnete_coll_teambarrier(team);
  
  /* collect the relrank information */
  gasnete_coll_gather_all(team, relranks, &myrelrank, sizeof(gasnet_node_t), 0 GASNETE_THREAD_PASS);
  gasnete_coll_teambarrier(team);

  new_total_ranks = 0;
  rel2act_map = (gasnet_node_t *)gasneti_malloc(team->total_ranks*sizeof(gasnet_node_t));
  for (uint32_t i=0; i<team->total_ranks; i++) {
    if (mycolor == colors[i]) {
      rel2act_map[relranks[i]] = team->rel2act_map[i];
      new_total_ranks++;
    }
  }
  
  /* It would be better to add some sanity check for team correctness here. */
  
  /* create a team */
  new_team_id = 0;
  // gasnet_coll_barrier_notify(team, 0, 0);
  // gasnet_coll_barrier_wait(team, 0, 0); 
  gasnete_coll_teambarrier(team);

#ifdef DEBUG_TEAM
  fprintf(stderr, "gasnete_coll_team_split: new_total_ranks %u, myrelrank %u.\n",
          new_total_ranks, myrelrank);
  PRINT_ARRAY(stderr, rel2act_map, new_total_ranks, "%u");
  fflush(stderr);
#endif

  newteam = gasnete_coll_team_create(new_total_ranks, myrelrank, rel2act_map);
  
  gasneti_free(rel2act_map);
  return newteam;
}

gasnet_team_handle_t gasnete_coll_team_lookup(uint32_t team_id) 
{
  uint32_t rv;
  gasnet_team_handle_t team;
  
#ifdef DEBUG_TEAM
  fprintf(stderr, "gasnete_coll_team_lookup: team_id %x\n", team_id);
  fflush(stderr);
#endif

	if (team_id == 0)
    team = GASNET_TEAM_ALL;
  else {
    if (HashTable_search(team_dir, team_id, (void **)&team))
      team = NULL; /* cannot find team_id the hash table */
  }
  
  return team;
}

gasnet_node_t gasnete_coll_team_rank2node(gasnete_coll_team_t team, int rank) 
{
  gasneti_assert(team != NULL);
  gasneti_assert(rank < team->total_ranks);
  return team->rel2act_map[rank];
}

gasnet_node_t gasnete_coll_team_node2rank(gasnete_coll_team_t team, gasnet_node_t node) 
{
  gasneti_assert(team != NULL);
  for (uint32_t i=0; i<team->total_ranks; i++)
    if (team->rel2act_map[i] == node)
      return i;
   
  gasneti_fatalerror("Cannot find node %u in team %p with id %x!\n", 
                     node, team, team->team_id);
}

uint32_t gasnete_coll_team_id(gasnete_coll_team_t team) 
{
  return team->team_id;
}

void gasnete_print_team(gasnet_team_handle_t team, FILE *fp)
{
  int i;
  fprintf(fp, "team id %x, total ranks %u, my rank %u\n",
          team->team_id, team->total_ranks, team->myrank);
  fprintf(fp, "rel2act_map:\n");
  for (i=0; i<team->total_ranks; i++) {
    fprintf(fp, "%u -> %u\n", i, team->rel2act_map[i]);
  }
  fflush(fp);
}

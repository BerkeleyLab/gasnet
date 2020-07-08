/*   $Source: bitbucket.org:berkeleylab/gasnet.git/extended-ref/coll/gasnet_team.c $
 *
 * Description: GASNet generic team implementation for collectives 
 * Copyright 2009, The Regents of the University of California
 * Terms of use are as specified in license.txt 
 */

#include <coll/gasnet_coll_internal.h>
#include <coll/gasnet_refcoll.h>

#include <coll/gasnet_team.h>
#include <coll/gasnet_autotune_internal.h>
#include <coll/gasnet_scratch.h>
#include <coll/gasnet_trees.h>

#ifdef GASNETE_COLL_TEAM_CONDUIT_DECLS
GASNETE_COLL_TEAM_CONDUIT_DECLS
#endif

/* #define DEBUG_TEAM */

static 
gasnete_hashtable_t *team_dir = NULL;

static
gasneti_weakatomic32_t my_team_seq = gasneti_weakatomic32_init(0);


extern size_t gasnete_coll_auxseg_size;
extern size_t gasnete_coll_auxseg_offset;

/*called by only one thread*/
static void initialize_team_fields(
                        gasnete_coll_team_t team,  
                        size_t scratch_size,       // Single-valued
                        gex_Addr_t *scratch_addrs,
                        gex_Flags_t flags
                        GASNETI_THREAD_FARG)
{
  team->sequence = 0xfffffff8;  // Intentionally near to wrap-around

  if (team->team_id) {
    team->scratch_size = scratch_size;
    team->scratch_addrs = scratch_addrs;
    gasneti_assert(! team->scratch_segs);
    gasneti_assert(! team->symmetric_scratch_offset);
  } else {
    gasneti_assert(team == GASNET_TEAM_ALL);
    gasneti_assert(gasnete_coll_auxseg_size);
    team->scratch_size = gasnete_coll_auxseg_size;
    gasneti_assert(! team->scratch_addrs);
    team->scratch_segs = gasneti_seginfo_aux;
    team->symmetric_scratch_offset = gasnete_coll_auxseg_offset;
  }
  team->myscratch = (void *)gasnete_coll_scratch_base(team, team->myrank);

#if GASNET_PAR && GASNET_DEBUG
  gasneti_mutex_init(&team->threads_mutex);
#endif
#if GASNET_DEBUG
  gasneti_mutex_init(&team->barrier_lock);
#endif
  
  team->tree_geom_cache_head = NULL;
  team->tree_geom_cache_tail = NULL;
  gasneti_mutex_init(&team->tree_geom_cache_lock);
  team->tree_construction_scratch = NULL;
  team->dissem_cache_head = NULL;
  team->dissem_cache_tail = NULL;
  gasneti_mutex_init(&team->dissem_cache_lock);
  team->autotune_info = gasnete_coll_autotune_init(team GASNETI_THREAD_PASS);
  team->consensus_id = team->consensus_issued_id = 0xfffffff8;  // Intentionally near to wrap-around
  gasnete_coll_alloc_new_scratch_status(team);
  team->scratch_free_list = NULL;
  gex_HSL_Init(&team->child.lock);
  
#ifndef GASNETE_COLL_P2P_OVERRIDE
  gex_HSL_Init(&team->p2p_lock);
  team->p2p_freelist = NULL;
  for (int i = 0; i < GASNETE_COLL_P2P_TABLE_SIZE; ++i) {
    team->p2p_table[i] = NULL;
  }
  team->p2p_eager_buffersz = MAX(gasnete_coll_p2p_eager_min, team->total_ranks * gasnete_coll_p2p_eager_scale);
#endif
}

/* Helper for gasnete_coll_team_alloc() */
static int gasnete_node_pair_sort_fn(const void *a_p, const void *b_p) {
  const int a0 = ((const gex_Rank_t *)a_p)[0];
  const int b0 = ((const gex_Rank_t *)b_p)[0];
  const int d0 = (a0 - b0); /* sort first by supernode */
  if (d0) return d0;
  else {
    const int a1 = ((const gex_Rank_t *)a_p)[1];
    const int b1 = ((const gex_Rank_t *)b_p)[1];
    /* break ties by node - must be increasing order because
     * we use local rank to determine the active node
     */
    return (a1 - b1);
  }
}

void gasnete_coll_team_init(gasnet_team_handle_t team, 
                            size_t scratch_size,
                            gex_Addr_t *scratch_addrs,
                            gex_Flags_t flags
                            GASNETI_THREAD_FARG)
{
#ifdef DEBUG_TEAM
  fprintf(stderr, "gasnete_coll_team_init: team %p, team_id %x, total_ranks %u, myrank %u\n", 
          team, team_id, total_ranks, myrank);
  fflush(stderr);
  if (myrank == 0 && team_id) {
    PRINT_ARRAY(stderr, team->rel2act_map, total_ranks, "%u");
    fflush(stderr);
  }
#endif
  
  initialize_team_fields(team, scratch_size, scratch_addrs, flags GASNETI_THREAD_PASS); 

  /* lock the team directory (team_dir) */
  /* add the new team to the directory */
  if (team_dir == NULL) {
    team_dir = gasnete_hashtable_create(TEAM_DIR_SIZE);
    gasneti_assert(team_dir != NULL);
  }
  gasnete_hashtable_insert(team_dir, team->team_id, team);

#ifdef gasnete_coll_team_init_conduit
  /* conduit specific initialization for gasnet teams */
#ifdef DEBUG_TEAM
  fprintf(stderr, "gasnete_coll_team_init: calling gasnete_coll_team_init_conduit.\n");
  fflush(stderr);
#endif
  gasnete_coll_team_init_conduit(team);
#endif
  /* unlock */

  if (team->team_id) {
    gasnete_coll_barrier_init(team, GASNETE_COLL_BARRIER_ENVDEFAULT);
  }
}

void gasnete_coll_team_fini(gasnet_team_handle_t team)
{
  int i;
  gasneti_assert(team != NULL);
  /* free data members of the team, such as scratch space and etc. */
  gasneti_free(team->rel2act_map);
  gasneti_free(team->peers.fwd);
#if GASNET_PSHM
  gasneti_free(team->supernode_peers.fwd);
#endif

  gasneti_assert(team_dir != NULL);
  gasnete_hashtable_remove(team_dir, team->team_id, NULL);

#if !defined(GASNETE_COLL_P2P_OVERRIDE) && GASNET_DEBUG
  for (i = 0; i < GASNETE_COLL_P2P_TABLE_SIZE; ++i) {
    /* Check that table is actually empty */
    gasneti_assert(team->p2p_table[i] == NULL);
  }
#endif

#ifdef gasnete_coll_team_fini_conduit
  /* conduit specific initialization for gasnet teams */
#ifdef DEBUG_TEAM
  fprintf(stderr, "gasnete_coll_team_fini: calling gasnete_coll_team_fini_conduit.\n");
  fflush(stderr);
#endif
  gasnete_coll_team_fini_conduit(team);
#endif
}

// Non-collective call to allocate local data and initialize some key fields
gasnet_team_handle_t gasnete_coll_team_alloc(
                        gex_Rank_t total_ranks,
                        gex_Rank_t myrank,
                        gex_Rank_t *rel2act_map)
{
  gasnet_team_handle_t team = gasneti_calloc(1,sizeof(struct gasnete_coll_team_t_));

  team->myrank = myrank;
  team->total_ranks = total_ranks;

  // Build rel2act_map
  size_t alloc_size = total_ranks * sizeof(gex_Rank_t);
  team->rel2act_map = (gex_Rank_t *)gasneti_malloc(alloc_size);
  memcpy(team->rel2act_map, rel2act_map, alloc_size);

  // Build peer lists
  if (total_ranks > 1) {
    unsigned int count = 0;
    for (gex_Rank_t i=1; i<total_ranks; i*=2) ++count;
    team->peers.num = count;
    team->peers.fwd = gasneti_malloc(sizeof(gex_Rank_t) * count * 2);
    team->peers.bwd = team->peers.fwd + count;
    for (gex_Rank_t i=0; i<count; i++) {
      unsigned int dist = 1 << i;
      team->peers.fwd[i] = rel2act_map[(myrank + dist) % total_ranks];
      team->peers.bwd[i] = rel2act_map[(myrank + total_ranks - dist) % total_ranks];
    }
  }

#if GASNET_PSHM
  // Build supernode stats
  {
    gex_Rank_t *node_vector, *supernodes;
    int count, rank;

    // A list with a representative for each supernode (for hierarchical comms)
    supernodes = gasneti_malloc(gasneti_nodemap_global_count * sizeof(gex_Rank_t));

    /* Created a sorted vector of (supernode,node) for members of this team
     * while finding size of and rank in local supernode in the same pass
     */
    count = 0; rank = -1;
    node_vector = gasneti_malloc(2 * total_ranks * sizeof(gex_Rank_t));
    for (gex_Rank_t i = 0; i < total_ranks; ++i) {
      gex_Rank_t n = rel2act_map[i];
      if (gasneti_pshm_jobrank_in_supernode(n)) {
        if (n == gasneti_mynode) rank = count;
        ++count;
      }
      node_vector[2*i+0] = gasneti_node2supernode(n);
      node_vector[2*i+1] = n;
    }
    qsort(node_vector, total_ranks, 2*sizeof(gex_Rank_t), &gasnete_node_pair_sort_fn);

    gasneti_assert((count >  0) && (count <= gasneti_nodemap_local_count));
    gasneti_assert((rank  >= 0) && (rank  <  gasneti_nodemap_local_count));
    team->supernode.node_count = count;
    team->supernode.node_rank  = rank;

    /* Count and enumerate unique supernodes and find my supernode's rank */
    count = 1; rank = 0;
    supernodes[0] = node_vector[1];
    for (gex_Rank_t i = 1; i < total_ranks; ++i) {
      if (node_vector[2*i] != node_vector[2*(i-1)]) {
        if (node_vector[2*i] == gasneti_nodemap_global_rank) rank = count;
        supernodes[count] = node_vector[2*i+1];
        ++count;
      }
    }
    gasneti_free(node_vector);

    gasneti_assert((count >  0) && (count <= gasneti_nodemap_global_count));
    gasneti_assert((rank  >= 0) && (rank  <  gasneti_nodemap_global_count));
    team->supernode.grp_count = count;
    team->supernode.grp_rank  = rank;

    /* Construct a list of log(P) representatives at distance +2^i */
    /* NOTE: 'count' and 'rank' are in the supernode space */
    {
      unsigned int len = 0;
      for (gex_Rank_t i=1; i<count; i*=2) ++len;
      team->supernode_peers.num = len;
      team->supernode_peers.fwd = gasneti_malloc(sizeof(gex_Rank_t) * len);
      for (gex_Rank_t i=0; i<len; i++) {
        unsigned int dist = 1 << i;
        team->supernode_peers.fwd[i] = supernodes[(rank + dist) % count];
      }
    }

    gasneti_free(supernodes);
  }
#endif

  return team;
}

/* collective function that should be called by all participating nodes */
gasnet_team_handle_t gasnete_coll_team_create(
                        gasnet_team_handle_t parent,
                        gex_Rank_t total_ranks,
                        gex_Rank_t myrank,
                        gex_Rank_t *rel2act_map,
                        size_t scratch_size,
                        gex_Addr_t *scratch_addrs,
                        gex_Flags_t flags
                        GASNETI_THREAD_FARG)
{
#ifdef DEBUG_TEAM
  fprintf(stderr, "gasnete_coll_team_create: team_lead %u, total_ranks %u, myrank %u\n", rel2act_map[0], total_ranks, myrank);
  fflush(stderr);
  if (myrank == 0) {
    PRINT_ARRAY(stderr, rel2act_map, total_ranks, "%u");
    fflush(stderr);
  }
#endif

  gasnet_team_handle_t team = gasnete_coll_team_alloc(total_ranks, myrank, rel2act_map);

  // Allocatate/communicate the new team's ID
  // Must follow team_alloc and precede gasnete_subteam_*
  gasnete_subteam_ID(parent, team GASNETI_THREAD_PASS);

#ifdef DEBUG_TEAM
    fprintf(stderr, "myrank %u, get new_team_id %x\n", myrank, team->team_id);
    fflush(stderr);
#endif

  // Construct global scratch_addrs[] array
  gex_Addr_t *global_scratch_addrs = NULL;
  gex_Flags_t scratch_mask = GEX_FLAG_TM_GLOBAL_SCRATCH    |
                             GEX_FLAG_TM_LOCAL_SCRATCH     |
                             GEX_FLAG_TM_SYMMETRIC_SCRATCH |
                             GEX_FLAG_TM_NO_SCRATCH;
  switch (flags & scratch_mask) {
    case GEX_FLAG_TM_GLOBAL_SCRATCH:
      global_scratch_addrs = gasneti_malloc(total_ranks * sizeof(gex_Addr_t));
      memcpy(global_scratch_addrs, scratch_addrs, total_ranks * sizeof(gex_Addr_t));
      break;

    case GEX_FLAG_TM_LOCAL_SCRATCH:
      global_scratch_addrs = gasneti_malloc(total_ranks * sizeof(gex_Addr_t));
      gasnete_subteam_Exchange(parent, team,
                               scratch_addrs, sizeof(gex_Addr_t), global_scratch_addrs
                               GASNETI_THREAD_PASS);
      break;

    case GEX_FLAG_TM_SYMMETRIC_SCRATCH:
      // TODO-EX: when implementing GEX_FLAG_SCRATCH_SEG_OFFSET, do not instatate an array
      global_scratch_addrs = gasneti_malloc(total_ranks * sizeof(gex_Addr_t));
      for (gex_Rank_t r = 0; r < total_ranks; ++r) {
        global_scratch_addrs[r] = scratch_addrs[0];
      }
      break;

    case GEX_FLAG_TM_NO_SCRATCH:
      scratch_size = 0;
      break;

    case 0:
      gasneti_fatalerror("No GEX_FLAG_TM_*_SCRATCH flags provided");
      break;

    default:
      gasneti_fatalerror("Multiple GEX_FLAG_TM_*_SCRATCH flags provided");
      break;
  }

#if GASNET_DEBUG
  // Verify single-valued scratch_size
  size_t leader_scratch_size = myrank ? 0xcafef00d : scratch_size;
  gasnete_subteam_Broadcast(parent, team,
                            &leader_scratch_size, sizeof(leader_scratch_size)
                            GASNETI_THREAD_PASS);
  gasneti_assert_uint(scratch_size ,==, leader_scratch_size);
#endif

  gasnete_coll_team_init(team, scratch_size, global_scratch_addrs, flags GASNETI_THREAD_PASS);
#ifdef DEBUG_TEAM
  gasnete_print_team(team, stderr);
#endif
  gasnete_subteam_Barrier(parent, team GASNETI_THREAD_PASS);

  return team;
}

void gasnete_coll_team_free(gasnet_team_handle_t team)
{
  gasneti_assert(team != NULL);
  gasnete_coll_team_fini(team);
  gasneti_free(team);
}

typedef struct {
  int relrank;
  gex_Rank_t parent_rank;
} gasnete_coll_split_sort_t;

int gasnete_coll_split_sort_fn(const void *x, const void *y) {
  const gasnete_coll_split_sort_t *a = x;
  const gasnete_coll_split_sort_t *b = y;
  // Note that parent_ranks are distinct (never equal)
  if (a->relrank == b->relrank) return (a->parent_rank < b->parent_rank) ? -1 : 1;
  else return (a->relrank < b->relrank) ? -1 : 1;
}

gasnet_team_handle_t gasnete_coll_team_split(gasnet_team_handle_t parent,
                                             int mycolor,
                                             int myrelrank,
                                             size_t scratch_size,
                                             gex_Addr_t scratch_addr,
                                             gex_Flags_t flags
                                             GASNETI_THREAD_FARG)
{
  gasnet_team_handle_t newteam;
  gex_Rank_t new_total_ranks;
  gex_Rank_t new_myrank = GEX_RANK_INVALID;
  gex_Rank_t i, j;
#ifdef DEBUG_TEAM
  fprintf(stderr, "gasnete_coll_team_split: parent rank %u, parent team handle %p, mycolor %u, myrelrank %u\n",
          parent->myrank, parent, mycolor, myrelrank);
  fflush(stderr);
#endif

  
  /* collect the arguments */
  struct {
    gex_Rank_t color;
    gex_Rank_t relrank;
  } my_args, *all_args;
  my_args.color = mycolor;
  my_args.relrank = myrelrank;
  all_args = gasneti_malloc(sizeof(my_args) * parent->total_ranks);
  gasnet_coll_gather_all(parent, all_args, &my_args, sizeof(my_args),
                         GASNET_COLL_LOCAL|GASNET_COLL_IN_MYSYNC | GASNET_COLL_OUT_MYSYNC);

  /* short-circuit if excluded */
  if (mycolor == -1) {
    gasneti_free(all_args);
    return NULL;
  }

  /* pass 1: just count */
  new_total_ranks = 0;
  for (i=0; i<parent->total_ranks; i++) {
    new_total_ranks += (mycolor == all_args[i].color);
  }

  /* pass 2: collect and rank the members */
  gasnete_coll_split_sort_t *members = gasneti_malloc(new_total_ranks*sizeof(gasnete_coll_split_sort_t));
  for (i=j=0; i<parent->total_ranks; i++) {
    if (mycolor == all_args[i].color) {
      members[j].parent_rank = i;
      members[j].relrank = all_args[i].relrank;
      j += 1;
    }
  }
  gasneti_assert(j == new_total_ranks);
  qsort(members, new_total_ranks, sizeof(gasnete_coll_split_sort_t), &gasnete_coll_split_sort_fn);

  // pass 3: collect jobranks of sorted members
  gex_Rank_t *rel2act_map = (gex_Rank_t *)gasneti_malloc(new_total_ranks*sizeof(gex_Rank_t));
  for (i=0; i < new_total_ranks; i++) {
    j = members[i].parent_rank;
    if (j == parent->myrank) new_myrank = i;
    rel2act_map[i] = parent->team_id ? parent->rel2act_map[j] : j;
  }
  gasneti_assert(new_myrank != GEX_RANK_INVALID);
  gasneti_free(members);
  gasneti_free(all_args);

  /* It would be better to add some sanity check for team correctness here. */
  
  /* create a team */

#ifdef DEBUG_TEAM
  fprintf(stderr, "gasnete_coll_team_split: new_total_ranks %u, new_myrank %u.\n",
          new_total_ranks, new_myrank);
  PRINT_ARRAY(stderr, rel2act_map, new_total_ranks, "%u");
  fflush(stderr);
#endif

  // scratch address info is "local" by construction
  gasneti_assert(! (flags & (GEX_FLAG_TM_GLOBAL_SCRATCH    | GEX_FLAG_TM_LOCAL_SCRATCH |
                             GEX_FLAG_TM_SYMMETRIC_SCRATCH | GEX_FLAG_TM_NO_SCRATCH)));
  flags |= GEX_FLAG_TM_LOCAL_SCRATCH;

  newteam = gasnete_coll_team_create(parent, new_total_ranks, new_myrank, rel2act_map,
                                     scratch_size, &scratch_addr, flags GASNETI_THREAD_PASS);
  
  gasneti_free(rel2act_map);
  return newteam;
}

gasnet_team_handle_t gasnete_coll_team_lookup(uint32_t team_id) 
{
  gasnet_team_handle_t team;
  
#ifdef DEBUG_TEAM
  fprintf(stderr, "gasnete_coll_team_lookup: team_id %x\n", team_id);
  fflush(stderr);
#endif

  if (team_id == 0) {
    team = GASNET_TEAM_ALL;
  } else {
    if (gasnete_hashtable_search(team_dir, team_id, (void **)&team))
      team = NULL; /* cannot find team_id the hash table */
  }
  
  return team;
}

void gasnete_print_team(gasnet_team_handle_t team, FILE *fp)
{
  int i;
  fprintf(fp, "team id %x, total ranks %u, my rank %u\n",
          (unsigned int)team->team_id, (unsigned int)team->total_ranks, (unsigned int)team->myrank);
  fprintf(fp, "rel2act_map:\n");
  for (i=0; i<team->total_ranks; i++) {
    fprintf(fp, "%u -> %u\n", i, (unsigned int)team->rel2act_map[i]);
  }
  fflush(fp);
}

/* ------------------------------------------------------------------------------------ */
// AM-based Collective Operations over a SUBSET of an existing "parent" team
// Used in team construction
//
// TODO-EX: multi-EP, especially Create using EPs not in parent
// TODO-EX: remove THUNK_TM via gex_TM_Pair()
// TODO-EX: revisit use of phase[2] vs sequence numbers (or similar)
// TODO-EX: seek to remove requirement for an entry barrier over parent_tm
// TODO-EX: determine if one can remove memory fences from start/end of initiators

static uint8_t*
gasnete_subteam_op_data(gasnete_coll_team_t parent, int phase, size_t size) {
  uint8_t *data = parent->child.data[phase];
  if_pf (! data) {
    gex_HSL_Lock(&parent->child.lock);
    data = parent->child.data[phase];
    if (! data) {
      data = gasneti_malloc(size);
      parent->child.data[phase] = data;
    }
    gex_HSL_Unlock(&parent->child.lock);
  }
  return data;
}

enum {
  gasnete_subteam_op_barrier,
  gasnete_subteam_op_broadcast,
  gasnete_subteam_op_exchange,
  gasnete_subteam_op_team_id,
};

#define GASNETE_SUBTEAM_OP_ARG0(op,phase,step,seq) \
  (gasnete_subteam_op_##op | ((phase)<<2) | ((step)<<3) | ((seq)<<8))

void gasnete_subteam_op_reqh(
                        gex_Token_t token, void *buf, size_t nbytes,
                        gex_AM_Arg_t parent_id, gex_AM_Arg_t nranks,
                        gex_AM_Arg_t arg0, gex_AM_Arg_t arg1)
{
    const int op    =  arg0       & 0x3;
    const int phase = (arg0 >> 2) & 1;
    const int step  = (arg0 >> 3) & 0x1f; // Max 2^5 steps            => 2^32 ranks
    const int seq   = (arg0 >> 8);        // Max 2^24 fragments * 512 => 8GB (and largest msg is elemsz*nranks/2)
    const int distance = (1 << step);
    gasneti_assert_uint(distance ,<, nranks);

    gasnete_coll_team_t parent = gasnete_coll_team_lookup(parent_id);
    switch (op) {
      case gasnete_subteam_op_barrier:
        // arg1 is unused
        gasneti_assert_uint(nbytes ,==, 0);
        gasneti_assert_int((int)gasneti_weakatomic32_read(&parent->child.step_rcvd[phase][step], 0) ,==, 0);
        gasneti_weakatomic32_set(&parent->child.step_rcvd[phase][step], 1, 0);
        break;

      case gasnete_subteam_op_broadcast: {
        // arg1 is size of one element
        uint8_t *data = gasnete_subteam_op_data(parent, phase, arg1);
        if (nbytes) { // not every message carries data.  some are synchronization
          gasneti_assert(nbytes == arg1);
          GASNETI_MEMCPY(data, buf, nbytes);
        }
        gasneti_assert_int((int)gasneti_weakatomic32_read(&parent->child.step_rcvd[phase][step], 0) ,==, 0);
        gasneti_weakatomic32_set(&parent->child.step_rcvd[phase][step], 1, GASNETI_ATOMIC_REL);
        break;
      }

      case gasnete_subteam_op_exchange: {
        // arg1 is size of one element
        uint8_t *data = gasnete_subteam_op_data(parent, phase, nranks*arg1);
        uint8_t *dest = data + (arg1 * distance) + (seq * gex_AM_LUBRequestMedium());
        gasneti_assert_ptr(dest + nbytes ,<=, data + arg1 * nranks);
        GASNETI_MEMCPY(dest, buf, nbytes);
        gasneti_weakatomic32_increment(&parent->child.step_rcvd[phase][step], GASNETI_ATOMIC_REL);
        break;
      }

      case gasnete_subteam_op_team_id: {
        // arg1 is the (non-zero) new team id
        gasneti_assert_uint(nbytes ,==, 0);
        gasneti_assert_uint(arg1 ,!=, 0);
        gasneti_assert_int((int)gasneti_weakatomic32_read(&parent->child.team_id, 0) ,==, 0);
        gasneti_weakatomic32_set(&parent->child.team_id, arg1, 0);
        break;
      }

      default: gasneti_unreachable_error(("Unknown op: %i",op));
    }
}

void gasnete_subteam_Barrier(
                        gasnete_coll_team_t parent,
                        gasnete_coll_team_t child
                        GASNETI_THREAD_FARG)
{
  const int phase = parent->child.phase;
  gex_Rank_t nranks = child->total_ranks;
  gex_Rank_t myrank = child->myrank;
  gasneti_sync_reads();

  if (nranks == 1) goto out;

  // Dissemination barrier
  for (unsigned int step = 0, distance = 1; distance < nranks; ++step, distance *= 2) {
    gex_Rank_t jobrank = child->peers.fwd[step];

    // Send payload using 0-byte AMMedium due to sharing single handler
    gex_AM_Arg_t arg0 = GASNETE_SUBTEAM_OP_ARG0(barrier, phase, step, 0);
    gex_AM_RequestMedium(gasneti_THUNK_TM, jobrank, _hidx_gasnete_subteam_op_reqh,
                         NULL, 0, GEX_EVENT_NOW, 0,
                         parent->team_id, nranks, arg0, 0);

    // Poll until we have received for the current step
    GASNET_BLOCKUNTIL((int)gasneti_weakatomic32_read(&parent->child.step_rcvd[phase][step], 0) == 1);
    gasneti_weakatomic32_set(&parent->child.step_rcvd[phase][step], 0, 0); // reset
  }

out:
  gasneti_sync_writes();
  parent->child.phase ^= 1;
}

// Root is always rank 0
// In-place on root (and thus a single pointer argument)
//
// This implementation provides a full barrier synchronization required for the
// use of phase[], but this does NOT ensure all ranks have received the
// broadcast prior to return on any given ranks.
void gasnete_subteam_Broadcast(
                        gasnete_coll_team_t parent,
                        gasnete_coll_team_t child,
                        void *ptr, size_t elemsz
                        GASNETI_THREAD_FARG)
{
  gasneti_assert_uint(elemsz ,<=, gex_AM_LUBRequestMedium());

  const int phase = parent->child.phase;
  gex_Rank_t nranks = child->total_ranks;
  gex_Rank_t myrank = child->myrank;
  gasneti_sync_reads();

  if (nranks == 1) goto out;

  uint8_t *data = gasnete_subteam_op_data(parent, phase, elemsz);

  if (!myrank) {
    GASNETI_MEMCPY(data, ptr, elemsz);
  }

  // Essentially an AllgatherV with len=elemsz on rank 0, and zero on all others
  gex_Rank_t recv_count = 0;
  for (unsigned int step = 0, distance = 1; distance < nranks; ++step, distance *= 2) {
    gex_Rank_t jobrank = child->peers.fwd[step];

    // Send payload using AMMedium
    size_t nbytes = (recv_count >= myrank) ? elemsz : 0; // non-zero if have recv'd data from rank 0
    gex_AM_Arg_t arg0 = GASNETE_SUBTEAM_OP_ARG0(broadcast, phase, step, 0);
    gex_AM_RequestMedium(gasneti_THUNK_TM, jobrank, _hidx_gasnete_subteam_op_reqh,
                         data, nbytes, GEX_EVENT_NOW, 0,
                         parent->team_id, nranks, arg0, (uint32_t)elemsz);

    // Poll until we have received for the current step
    GASNET_BLOCKUNTIL((int)gasneti_weakatomic32_read(&parent->child.step_rcvd[phase][step], 0) == 1);
    gasneti_weakatomic32_set(&parent->child.step_rcvd[phase][step], 0, 0); // reset

    recv_count += distance;
  }

  if (myrank) {
    GASNETI_MEMCPY(ptr, data, elemsz);
  }

  gasneti_free(data);
  parent->child.data[phase] = NULL;

out:
  gasneti_sync_writes();
  parent->child.phase ^= 1;
}

void gasnete_subteam_Exchange(
                        gasnete_coll_team_t parent,
                        gasnete_coll_team_t child,
                        void *src, size_t elemsz, void *dst
                        GASNETI_THREAD_FARG)
{
  gasneti_assert(elemsz);

  const int phase = parent->child.phase;
  gex_Rank_t nranks = child->total_ranks;
  gex_Rank_t myrank = child->myrank;
  gasneti_sync_reads();

  if (nranks == 1) {
    GASNETI_MEMCPY_SAFE_IDENTICAL(dst, src, elemsz);
    goto out;
  }

  uint8_t *data = gasnete_subteam_op_data(parent, phase, nranks*elemsz);

  // copy in local contribution
  GASNETI_MEMCPY(data, src, elemsz);

  // Bruck's concatenation algorithm:
  for (unsigned int step = 0, distance = 1; distance < nranks; ++step, distance *= 2) {
    gex_Rank_t jobrank = child->peers.bwd[step];
    size_t nbytes = elemsz * MIN(distance, nranks - distance);
    size_t offset = 0;
    uint32_t seq = 0;

    // Send payload using AMMedium(s)
    do {
      const gex_AM_Arg_t arg0 = GASNETE_SUBTEAM_OP_ARG0(exchange, phase, step, seq);
      const size_t to_xfer = MIN(nbytes, gex_AM_LUBRequestMedium());
      gex_AM_RequestMedium(gasneti_THUNK_TM, jobrank, _hidx_gasnete_subteam_op_reqh,
                           data + offset, to_xfer, GEX_EVENT_NOW, 0,
                           parent->team_id, nranks, arg0, (uint32_t)elemsz);
      ++seq;
      offset += to_xfer;
      nbytes -= to_xfer;
    } while (nbytes);

    // Poll until we have received the same number of messages as we sent
    GASNET_BLOCKUNTIL((int)gasneti_weakatomic32_read(&parent->child.step_rcvd[phase][step], 0) >= (int)seq);
    gasneti_assert_int((int)gasneti_weakatomic32_read(&parent->child.step_rcvd[phase][step], 0) ,==, (int)seq);
    gasneti_weakatomic32_set(&parent->child.step_rcvd[phase][step], 0, 0); // reset
  }

  // Copy to final destination while performing the rotation
  const size_t a = elemsz * (nranks - myrank);
  const size_t b = elemsz * myrank;
  GASNETI_MEMCPY_SAFE_EMPTY(dst, data + a, b);
  GASNETI_MEMCPY_SAFE_EMPTY((uint8_t*)dst + b, data, a);
  gasneti_assert(!memcmp((uint8_t*)dst + myrank*elemsz, src, elemsz));

  gasneti_free(data);
  parent->child.data[phase] = NULL;

out:
  gasneti_sync_writes();
  parent->child.phase ^= 1;
}

void gasnete_subteam_ID(
                        gasnete_coll_team_t parent,
                        gasnete_coll_team_t child
                        GASNETI_THREAD_FARG)
{
  gex_Rank_t nranks = child->total_ranks;
  gex_Rank_t myrank = child->myrank;
  uint32_t new_team_id;

  // Reset sub-team collectives phase prior to use
  parent->child.phase = 0;

  if (!myrank) {
    // the team leader (rank 0) allocates the new team_id
    uint32_t tmp = gasneti_weakatomic32_add(&my_team_seq, 1, GASNETI_ATOMIC_NONE);

    // limitation: a leader can only allocate team id 4096 times
    gasneti_assert(tmp <= 0xfff);

    new_team_id = ((gasneti_mynode << 12) | (tmp & 0xfff));
  } else {
    // Non-leader polls until we have received the datum
    gasneti_polluntil (0 != (new_team_id = gasneti_weakatomic32_read(&parent->child.team_id, 0)));
    gasneti_weakatomic32_set(&parent->child.team_id, 0, 0);
  }

  // Send to children in a binomial tree
  gex_Rank_t subtree = myrank ? (myrank & -myrank) : nranks;
  if (subtree > 1) {
    for (int step = gasnete_coll_log2_rank(subtree - 1); step >= 0; --step) {
      if (myrank + (1 << step) >= nranks) continue;
      gex_Rank_t jobrank = child->peers.fwd[step];
      const gex_AM_Arg_t arg0 = GASNETE_SUBTEAM_OP_ARG0(team_id, 0, 0, 0);
      gex_AM_RequestMedium(gasneti_THUNK_TM, jobrank, _hidx_gasnete_subteam_op_reqh,
                           NULL, 0, GEX_EVENT_NOW, 0,
                           parent->team_id, nranks, arg0, new_team_id);
    }
  }

  child->team_id = new_team_id;
}

/* ------------------------------------------------------------------------------------ */

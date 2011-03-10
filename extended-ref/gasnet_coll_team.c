/* $Source: /Users/kamil/work/gasnet-cvs2/gasnet/extended-ref/gasnet_coll_team.c,v $
 * $Date: 2011/03/10 18:53:28 $
 * $Revision: 1.9.2.4 $
 *
 * Description: GASNet team implementation for collectives 
 * Copyright 2010, E. O. Lawrence Berekely National Laboratory
 * Terms of use are as specified in license.txt 
 */

#include <gasnet_internal.h>
#include <gasnet_coll_internal.h>
#include <gasnet_coll.h>
#include <gasnet_coll_team.h>
#include <gasnet_extended_refcoll.h>
#include <gasnet_extended_internal.h>
#include <gasnet_coll_autotune_internal.h>
#include <gasnet_coll_scratch.h>
#include <gasnet_coll_trees.h>

/* #define DEBUG_TEAM */

#ifdef GASNETE_COLL_TEAM_CONDUIT_DECLS
GASNETE_COLL_TEAM_CONDUIT_DECLS
#endif

/**
 * Print an array for debugging
 *
 * @param fp FILE descriptor for output
 * @param A a pointer to the array
 * @param size the length of array A
 * @param format the output format of array elements
 */
#define PRINT_ARRAY(fp, A, size, format)        \
  do {                                          \
    int i;                                      \
    for(i=0; i<(size); i++)                     \
      {                                         \
        fprintf((fp), "%s[%d]=", #A, i);        \
        fprintf((fp), format, (A)[i]);          \
        fprintf((fp), " ");                     \
      }                                         \
    fprintf((fp), "\n");                        \
  } while(0);


/* #define DEBUG_TEAM */

gasnete_hashtable_t *global_team_dir;

int gasnete_coll_smp_tune_barriers;

/* gasnete_coll_team_id_lock and gasnete_coll_team_id_seq are only
   meaninful on gasnet node 0 */
gasnet_hsl_t gasnete_coll_team_id_lock;
static uint32_t gasnete_coll_team_id_seq;

gasnet_node_t gasnete_coll_team_size(gasnet_team_handle_t team)
{
  return team->total_images;
}

void gasnete_coll_team_td_print(gasnete_coll_team_threaddata_t *team_td)
{
  fprintf(stderr, "[%u] team_td: my_image %u, my_local_image %u, threads_sequence %u threads_hold_lock %u smp_coll_handle %p\n",
          gasnete_coll_team_my_image(GASNET_TEAM_ALL), team_td->my_image, team_td->my_local_image, team_td->threads_sequence, team_td->threads_hold_lock,
          team_td->smp_coll_handle);
}

/**
 * Insert the thread-specific team handle into the per-thread team directory
 */
void gasnete_coll_team_td_init(uint32_t team_id, 
                               gasnet_team_handle_t team, 
                               gasnet_image_t my_image,
                               gasnet_image_t my_local_image,
                               smp_coll_t smp_coll_handle,
                               gasnete_coll_threaddata_t *td)
{
  gasnete_coll_team_threaddata_t *team_td;

  gasneti_assert (td->team_dir != NULL); 
  team_td = (gasnete_coll_team_threaddata_t *)gasneti_malloc(sizeof(gasnete_coll_team_threaddata_t));
  team_td->team = team;
  team_td->my_image = my_image;
  team_td->my_local_image = my_local_image;
  team_td->threads_sequence = 0;
  team_td->threads_hold_lock = 0;
  team_td->smp_coll_handle = smp_coll_handle;
  team_td->num_multi_addr_collectives_started = 0;
  gasnete_hashtable_insert(td->team_dir, team_id, team_td);
  /* Insert the team to the td->myteam list*/
  if (td->my_teams == NULL) {
    gasneti_assert(team == GASNET_TEAM_ALL);
    td->my_teams = team;
  } else {
    team->next = td->my_teams;
    td->my_teams = team;
  }

  /* gasnete_coll_team_td_print(team_td); */
  /* need to remove the team from the list when team is freed. */
}

gasnete_coll_team_threaddata_t *
gasnete_coll_team_get_threaddata(uint32_t team_id, 
                                 gasnete_coll_threaddata_t *td)
{
  gasnete_coll_team_threaddata_t *team_td;
  gasnete_hashtable_search(td->team_dir, team_id, (void **)&team_td);

  return team_td;
}

gasnet_team_handle_t gasnete_coll_team_lookup(uint32_t team_id)
{
  uint32_t rv;
  gasnet_team_handle_t team;

	if (team_id == 0) {
    team = GASNET_TEAM_ALL;
  } else {
    if (gasnete_hashtable_search(global_team_dir, team_id, (void **)&team)) {
      gasneti_fatalerror("[%u] gasnete_coll_team_lookup failed: team_id %d\n",
                         gasnete_coll_team_my_image(GASNET_TEAM_ALL), team_id);
    } else {
      gasneti_assert(team != NULL);
    }
  }
  
  return team;
}

void gasnete_coll_team_print(gasnet_team_handle_t team, FILE *fp)
{
  int i;
  fprintf(fp, "[%u] team id %u, total ranks %u, my rank %u, total images %u, my image %u\n",
          gasnete_coll_team_my_image(GASNET_TEAM_ALL), team->team_id, team->total_ranks, team->myrank, 
          team->total_images, gasnete_coll_team_my_image(team));
  fprintf(fp, "rel2act_map:\n");
  for (i=0; i<team->total_ranks; i++) {
    fprintf(fp, "%u -> %u\n", i, (unsigned int)team->rel2act_map[i]);
  }
  fflush(fp);
}

gasnet_image_t gasnete_coll_team_my_local_image(gasnet_team_handle_t team
                                                GASNETE_THREAD_FARG)
{
  gasnete_coll_threaddata_t *td = GASNETE_COLL_MYTHREAD_NOALLOC;
  gasnete_coll_team_threaddata_t *team_td;

#if GASNET_SEQ
  return 0;
#endif 

  gasneti_assert(team != NULL);
  gasneti_assert(td != NULL);
  
  if (team == GASNET_TEAM_ALL)
    return td->my_local_image; 

  if (gasnete_hashtable_search(td->team_dir, team->team_id, (void **)&team_td))
    return GASNET_IMAGE_UNDEFINED; /* if the image is not found is the team */
  
  return team_td->my_local_image;
}

gasnet_image_t gasnete_coll_team_my_image(gasnet_team_handle_t team)
{
  gasnete_coll_threaddata_t *td;
  gasnete_coll_team_threaddata_t *team_td;

#if GASNET_SEQ
  return gasnet_mynode();
#endif

  GASNETE_THREAD_LOOKUP; /* GASNETE_THREAD_FARG_ALONE = GASNETE_THREAD_GET_ALONE; */
    
  gasneti_assert(team != NULL);

  td = GASNETE_COLL_MYTHREAD_NOALLOC;
  gasneti_assert(td != NULL);

  if (team == GASNET_TEAM_ALL)
    return td->my_image;

  if (gasnete_hashtable_search(td->team_dir, team->team_id, (void **)&team_td))
    return GASNET_IMAGE_UNDEFINED; /* if the image is not found is the team */

  return team_td->my_image;
}

gasnet_node_t gasnete_coll_image2rank(gasnete_coll_team_t team,
                                      gasnet_image_t image)
{
  gasneti_assert(team != NULL);
  gasneti_assert(image < team->total_images);
  gasneti_assert(team->image2rank_map != NULL);

  return team->image2rank_map[image];
}

gasnet_node_t gasnete_coll_image2node(gasnete_coll_team_t team,
                                      gasnet_image_t image)
{
  gasneti_assert(team != NULL);
  gasneti_assert(image < team->total_images);
  gasneti_assert(team->rel2act_map != NULL);
  gasneti_assert(team->image2rank_map != NULL);

  return team->rel2act_map[team->image2rank_map[image]];
}

gasnet_node_t gasnete_coll_team_rank2node(gasnete_coll_team_t team, gasnet_node_t rank) 
{
  gasneti_assert(team != NULL);
  gasneti_assert(rank < team->total_ranks);
  return team->rel2act_map[rank];
}

gasnet_node_t gasnete_coll_team_node2rank(gasnete_coll_team_t team, gasnet_node_t node) 
{
  uint32_t i;
  gasneti_assert(team != NULL);
  for (i=0; i<team->total_ranks; i++)
    if (team->rel2act_map[i] == node)
      return i;
   
  gasneti_fatalerror("Cannot find node %u in team %p with id %x!\n", 
                     (unsigned int)node, team, (unsigned int)team->team_id);
  return (gasnet_node_t)0xFFFF; /* NOT REACHED */
}

/* copy the data to the mailbox of the images */
void gasnete_coll_team_mailbox_copy(gasnet_team_handle_t team, 
                                    void *data, 
                                    gasnet_image_t *images, 
                                    gasnet_image_t image_count)
{
  int i;

  for (i=0; i<image_count; i++) {
#ifdef DEBUG_TEAM
    fprintf(stderr, "[%u] gasnete_coll_team_mailbox_copy: data %p, image_count %u, dst local images:\n",
            gasnete_coll_team_my_image(team), data, image_count);
    PRINT_ARRAY(stderr, images, image_count, "%u");
#endif
    gasneti_assert(images[i] < team->my_images);
    if (team->mailbox[images[i]].empty == 1) {     
      team->mailbox[images[i]].data = data;
      team->mailbox[images[i]].empty = 0;
    } else {
      gasneti_fatalerror("[ %u] Error in gasnete_coll_team_mailbox_copy: fail to send data to the mailbox of team %u local_image %u\n", gasnete_coll_team_my_image(team), team->team_id, images[i]);
    }
  }
}

void gasnete_coll_local_bcast(void *src, void **dst, int dst_count, size_t nbytes)
{
  int i;
  
  for (i=0; i<dst_count; i++)
    memcpy(dst[i], src, nbytes);
}

void gasnete_coll_team_local_barrier(gasnet_team_handle_t team)
{
  gasnete_coll_threaddata_t *td;
  gasnete_coll_team_threaddata_t *team_td;
  GASNETE_THREAD_LOOKUP; /* GASNETE_THREAD_FARG_ALONE = GASNETE_THREAD_GET_ALONE; */

  if (team->my_images > 1) {
    td = GASNETE_COLL_MYTHREAD_NOALLOC;
    team_td = gasnete_coll_team_get_threaddata(team->team_id, td);
    gasneti_assert(team_td != NULL);
    smp_coll_barrier(team_td->smp_coll_handle, 0);
  }
}

void gasnete_coll_team_local_bcast(gasnet_team_handle_t team, void *buffer, 
                                   size_t nbytes, gasnet_image_t root)
{
  int i;
  void *src;
  
  if (gasnete_coll_team_my_image(team) == root) {
    gasneti_assert(team->mailbox[root].empty == 1);
    team->mailbox[root].data = buffer;
    team->mailbox[root].empty = 0;
  } else {
    gasneti_waituntil(team->mailbox[root].empty == 0);
    src = team->mailbox[root].data;
    memcpy(buffer, src, nbytes);
  }
  
  gasnete_coll_team_local_barrier(team);
}

void gasnete_coll_team_shuffle_pointers(void * const in_ptrs[], 
                                        void * out_ptrs[],
                                        size_t count,
                                        size_t in2out_map[])
{
  size_t i;

  for (i=0; i<count; i++) {
    out_ptrs[in2out_map[i]] = in_ptrs[i];
  }
}

void gasnete_coll_team_shuffle_array(void *src, 
                                     void *dst,
                                     size_t count,
                                     size_t nbytes,
                                     size_t in2out_map[])
{
  size_t i, total_size;
  char *buf;

  total_size = count * nbytes;

  if (src == dst) {
    buf = (char *)gasneti_malloc(total_size); /* in-place shuffling */
  } else {
    buf = (char *)dst;
  }

  for (i=0; i<count; i++) {
    /* shuffle the i'th chunk (nbytes) of data */
    memcpy(buf+nbytes*in2out_map[i], ((char *)src)+nbytes*i, nbytes);
  }

  if (src == dst) {
    memcpy(dst, buf, total_size);
    gasneti_free(buf);
  }
}

void gasnete_coll_team_unshuffle_array(void *src, 
                                       void *dst,
                                       size_t count,
                                       size_t nbytes,
                                       size_t in2out_map[])
{
  size_t i, total_size;
  char *buf;

  total_size = count * nbytes;

  if (src == dst) {
    buf = (char *)gasneti_malloc(total_size); /* in-place shuffling */
  } else {
    buf = (char *)dst;
  }

  for (i=0; i<count; i++) {
    /* shuffle the i'th chunk (nbytes) of data */
    memcpy(buf+nbytes*i, ((char *)src)+nbytes*in2out_map[i], nbytes);
  }

  if (src == dst) {
    memcpy(dst, buf, total_size);
    gasneti_free(buf);
  }
}


/*---------------------------------------------------------------------------------*/
/* Collective teams */

/* XXX: Teams are not yet fully designed
 *
 * Likely interface:
 *
 *  void gasnete_coll_team_ins(op)
 *	Add a team to the table
 *  void gasnete_coll_team_del(op)
 *	Remove a team from the table
 *  gasnete_coll_team_t gasnete_coll_team_find(team_id)
 *	Lookup a team by its 32-bit id, returning NULL if not found.
 *
 * Serialization done inside the implementation
 */

/* initialize_team_fields() should be called by only one thread in the
   node/process */
static void initialize_team_fields(gasnete_coll_team_t team,  
                                   gasnet_image_t total_images, 
                                   gasnet_image_t my_image, 
                                   gasnet_node_t total_ranks,
                                   gasnet_node_t myrank,
                                   const gasnet_image_t images[],
                                   gasnet_seginfo_t *scratch_segments 
                                   GASNETE_THREAD_FARG) 
{
  uint32_t i; 
  size_t image_size = total_ranks*sizeof(gasnet_image_t);
  static size_t smallest_scratch_seg;
#if GASNET_DEBUG
  static int team_all_made=0;
#endif
  gasnete_coll_threaddata_t *td = GASNETE_COLL_MYTHREAD_NOALLOC;

  team->global_team = (total_images == GASNET_TEAM_ALL->total_images) ? 1 : 0;
  team->sequence = 42;
  team->total_ranks = total_ranks;
  team->myrank = myrank;

  team->total_images = total_images;
  team->all_images = gasneti_malloc(image_size);
  team->all_offset = gasneti_malloc(image_size);
  if (images != NULL) {
    memcpy(team->all_images, images, image_size);
  } else  {
    for (i = 0; i < total_ranks; ++i) {
      team->all_images[i] = 1;
    }
  }

  gasneti_assert(global_team_dir != NULL);
  gasnete_hashtable_insert(global_team_dir, team->team_id, team);

  /*GASNET TEAM ALL already has a barrier attached to it*/
  if(team!=GASNET_TEAM_ALL) {
    gasnete_coll_barrier_init(team, GASNETE_COLL_BARRIER_ENVDEFAULT);
  }

  gasneti_weakatomic_set(&team->num_multi_addr_collectives_started, 0, GASNETT_ATOMIC_WMB_PRE);
  team->tree_geom_cache_head = NULL;
  team->tree_geom_cache_tail = NULL;
  gasneti_mutex_init(&team->tree_geom_cache_lock);
  team->tree_construction_scratch = NULL;

  team->dissem_cache_head = NULL;
  team->dissem_cache_tail = NULL;
  gasneti_mutex_init(&team->dissem_cache_lock);

  /* Initialize the per-team active op list */
  gasnete_coll_active_init(team);

  team->total_images = 0;
  team->max_images = 0;
  team->fixed_image_count=1;
  smallest_scratch_seg = scratch_segments[0].size;
  for (i = 0; i < total_ranks; i++) {
    team->all_offset[i] = team->total_images;
    team->total_images += team->all_images[i];
    team->max_images = MAX(team->max_images,team->all_images[i]);
    if(team->all_images[i] != team->all_images[0]) {
      team->fixed_image_count = 0;
    }
    smallest_scratch_seg = MIN(smallest_scratch_seg, scratch_segments[i].size);
  }
  team->my_images = team->all_images[myrank];
  team->my_offset = team->all_offset[myrank];
  
#if GASNET_PAR
  if (!images) {
    team->multi_images = 0;
    team->multi_images_any = 0;
  } else if (team->my_images != 1) {
    team->multi_images = 1;
    team->multi_images_any = 1;
  } else {
    team->multi_images = 0;
    team->multi_images_any = 0;
    for (i = 0; i < gasneti_nodes; ++i) {
      if (team->all_images[i] > 1) {
        team->multi_images_any = 1;
        break;
      }
    }
  }
#endif

  team->threads_sequence = 0;
  gasneti_mutex_init(&team->gasnete_coll_threads_mutex);
  team->consensus_issued_id = 0;
  team->consensus_id = 0;

  team->scratch_segs = scratch_segments;
  team->smallest_scratch_seg = smallest_scratch_seg;
  gasnete_coll_alloc_new_scratch_status(team);

  team->autotune_info = gasnete_coll_autotune_init(team, myrank, 
                                                   total_ranks, 
                                                   team->my_images, 
                                                   team->total_images,
                                                   smallest_scratch_seg 
                                                   GASNETE_THREAD_PASS);
  
  team->mailbox = (gasnete_coll_team_mailbox_t *)
    gasneti_malloc(team->my_images * sizeof(gasnete_coll_team_mailbox_t));
  for (i=0; i<team->my_images; i++) 
    team->mailbox[i].empty = 1;

#ifndef GASNETE_COLL_P2P_OVERRIDE
  gasnet_hsl_init(&team->p2p_lock);
  team->p2p_freelist = NULL;
  for (i = 0; i < GASNETE_COLL_P2P_TABLE_SIZE; ++i) {
    team->p2p_table[i] = NULL;
  }
#endif

  team->next = NULL;

#ifdef gasnete_coll_team_init_conduit
  /* conduit specific initialization for gasnet teams */
#ifdef DEBUG_TEAM
  fprintf(stderr, "gasnete_coll_team_init: calling gasnete_coll_team_init_conduit.\n");
  fflush(stderr);
#endif
  gasnete_coll_team_init_conduit(team);
#endif
  
  if (!team->fixed_image_count && team->myrank ==0) {
    fprintf(stderr, "WARNING: Current collective implementation requires a constant number\n");
    fprintf(stderr, "WARNING: of threads per process for optimized collectives.\n");
  }
}


void gasnete_coll_teamall_init(gasnet_node_t total_ranks,
                               gasnet_node_t myrank,
                               gasnet_image_t myimage,
                               const gasnet_image_t images[],
                               gasnet_seginfo_t *scratch_segs
                               GASNETE_THREAD_FARG)
{
  gasnet_node_t i;
  gasnet_image_t j;
  gasnete_coll_team_t team = GASNET_TEAM_ALL;
  gasnete_coll_team_threaddata_t *team_td;
  gasnet_image_t total_images, current_images;
  gasnete_coll_threaddata_t *td = GASNETE_COLL_MYTHREAD;

  gasnete_coll_team_id_seq = 1;
  gasnet_hsl_init(&gasnete_coll_team_id_lock);

  team->team_id = 0;  /* The team_id for GASNET_TEAM_ALL is default to 0. */
  team->total_ranks = total_ranks;
  team->myrank = myrank;
  team->rel2act_map = (gasnet_node_t *)gasneti_malloc(sizeof(gasnet_node_t) * total_ranks);

  if (images != NULL) {
    total_images = 0;
    for (i=0; i<total_ranks; i++) {
      team->rel2act_map[i] = i;
      total_images += images[i];
    }
    team->image2rank_map = (gasnet_node_t *)gasneti_malloc(sizeof(gasnet_image_t) * total_images);
    
    current_images = 0;
    for (i=0; i<total_ranks; i++) {
      for (j=0; j<images[i]; j++) {
        team->image2rank_map[current_images+j] = i;
      }
      current_images += images[i];
    }
  } else { /* images == NULL; 1 image per process */
    myimage = myrank;
    total_images = total_ranks;
    team->image2rank_map = (gasnet_node_t *)gasneti_malloc(sizeof(gasnet_image_t) * total_images);
    for (i=0; i<total_ranks; i++) {
      team->rel2act_map[i] = i;
      team->image2rank_map[i] = i;
    }
  }

  team->image_rel2act_map = (gasnet_image_t *)gasneti_malloc(sizeof(gasnet_image_t) * total_images);
  for (j=0; j<total_images; j++) {
    team->image_rel2act_map[j] = j;
  }

  initialize_team_fields(team, total_images, myimage, 
                         total_ranks, myrank, images,
                         scratch_segs GASNETE_THREAD_PASS);

  team->local_images = (gasnet_image_t *)gasneti_malloc(sizeof(gasnet_image_t) * team->my_images);
  for (j=0; j<team->my_images; j++) {
      team->local_images[j] = team->my_offset + j;
  }
}

void gasnete_coll_tree_geom_release(gasnete_coll_tree_geom_t *geom);
void gasnete_coll_team_fini(gasnet_team_handle_t team  GASNETE_THREAD_FARG)
{
  int i;
  gasnete_coll_threaddata_t *td = GASNETE_COLL_MYTHREAD_NOALLOC;
  gasnete_coll_team_threaddata_t *team_td;
  team_td = gasnete_coll_team_get_threaddata(team->team_id, td);

  gasneti_assert(team != NULL);

  /* free data members of the team, such as scratch space and etc. */
  
  /* undo gasnete_coll_alloc_new_scratch_status(team); */
  gasnete_coll_free_scratch_status(team->scratch_status GASNETE_THREAD_PASS); 
  
  /* free cached geometries */
  {    
    gasnete_coll_tree_geom_t *curr_geom = team->tree_geom_cache_head;
    gasnete_coll_tree_geom_t *next_geom;
    while(curr_geom != NULL) {
      next_geom = curr_geom->next;
      gasnete_coll_tree_geom_release(curr_geom);
      curr_geom = next_geom;
    }
  }

  {
    gasnete_coll_dissem_info_t *curr = team->dissem_cache_head;
    gasnete_coll_dissem_info_t *next;
    while(curr != NULL) {
      next = curr->next;
      gasnete_coll_release_dissemination(curr, team);
      curr = next;
    }
  }

#if !defined(GASNETE_COLL_P2P_OVERRIDE) && GASNET_DEBUG
  for (i = 0; i < GASNETE_COLL_P2P_TABLE_SIZE; ++i) {
    /* Check that table is actually empty */
    gasneti_assert(team->p2p_table[i] == NULL);
  }
#endif

  gasneti_free(team->rel2act_map);
  gasneti_assert(td->team_dir != NULL);
  gasnete_hashtable_remove(td->team_dir, team->team_id, NULL);

  if (gasnete_coll_team_my_local_image(team GASNETE_THREAD_PASS) == 0) {
    gasnete_hashtable_remove(global_team_dir, team->team_id, NULL);
  }


  /* remove team from the td->my_teams list */
  if (team == td->my_teams) {
    td->my_teams = team->next;
  } else {
    gasnet_team_handle_t team_tmp = td->my_teams;
    while (team_tmp->next != NULL && team_tmp->next != team) {
      team_tmp = team_tmp->next;
    }
    gasneti_assert(team_tmp->next == team);
    team_tmp->next = team->next;
  }

  gasneti_free(team->image2rank_map);

  gasnete_coll_autotune_fini(team);
  
  gasneti_free(team->all_offset);
  gasneti_free(team->all_images);

  /* Free thread-specific per-team data team_td */
  gasneti_free(team_td);

  /* YZ: Have I freed everything?? */
  gasneti_free(team->image_rel2act_map);
  gasneti_free(team->local_images);
  gasneti_free(team->image2rank_map);

  gasnete_coll_autotune_fini(team);
  
  gasneti_free(team->all_offset);
  gasneti_free(team->all_images);

#ifdef gasnete_coll_team_fini_conduit
  /* conduit specific initialization for gasnet teams */
#ifdef DEBUG_TEAM
  fprintf(stderr, "gasnete_coll_team_fini: calling gasnete_coll_team_fini_conduit.\n");
  fflush(stderr);
#endif
  gasnete_coll_team_fini_conduit(team);
#endif
}


/* AM Request handler for new team id */
void gasnete_coll_teamid_reqh_inner(gasnet_token_t token,
                                    gasnet_handlerarg_t team_id_count,
                                    void *clientdata)
{
  uint32_t teamid;

  gasneti_assert(gasnet_mynode() == 0);

  gasnet_hsl_lock(&gasnete_coll_team_id_lock);
  teamid = gasnete_coll_team_id_seq;
  gasnete_coll_team_id_seq += (uint32_t)team_id_count;
  gasnet_hsl_unlock(&gasnete_coll_team_id_lock);

#ifdef DEBUG_TEAM
  fprintf(stderr, "gasnete_coll_teamid_reqh: new_teamid %u, team_id_count %u\n", 
          teamid, (uint32_t)team_id_count);
  fflush(stderr);
#endif
  
  SHORT_REP(2,3,(token,gasneti_handleridx(gasnete_coll_teamid_reph),
                 teamid, PACK(clientdata)));
}
SHORT_HANDLER(gasnete_coll_teamid_reqh, 2, 3,
              (token, a0, UNPACK(a1)   ),
              (token, a0, UNPACK2(a1,a2)));

/* AM Reply handler for new team id */
void gasnete_coll_teamid_reph_inner(gasnet_token_t token,
                                    gasnet_handlerarg_t first_teamid,
                                    void *clientdata)
{
  uint32_t *teamid_ptr = (uint32_t *)clientdata;
  *teamid_ptr = (uint32_t)first_teamid;
#ifdef DEBUG_TEAM
  fprintf(stderr, "gasnete_coll_teamid_reph: new_teamid %u\n", 
          *teamid_ptr);
  fflush(stderr);
#endif
}
SHORT_HANDLER(gasnete_coll_teamid_reph, 2, 3,
              (token, a0, UNPACK(a1)   ),
              (token, a0, UNPACK2(a1,a2)));

/* 
 * Return 1 if target_img is in the array images; otherwise, return 0; 
 * The current implementation uses a linear search here but it can be optmized.
 */
int gasnete_coll_team_find_image(gasnet_image_t *images,
                                 gasnet_image_t image_count,
                                 gasnet_image_t target_img)
{
  gasnet_image_t i;

  for (i=0; i<image_count; i++) 
    if (target_img == images[i]) {
      return 1;
    }

  return 0;
}


/* Find the first local image in the team that is also in the image
   set */
static gasnet_image_t 
gasnete_coll_team_first_local_image_in_set(gasnet_team_handle_t team, 
                                           gasnet_image_t *image_set, 
                                           gasnet_image_t image_count)
{
  gasnet_image_t i;

  for (i=0; i<team->my_images; i++) {
#ifdef DEBUG_TEAM
    fprintf(stderr, "[%u] team->local_images[%u]=%u\n", gasnete_coll_team_my_image(GASNET_TEAM_ALL),
            i, team->local_images[i]);
#endif
    if (gasnete_coll_team_find_image(image_set, image_count,
                                     team->local_images[i])) {
      return team->local_images[i];
    }
  }

#ifdef DEBUG_TEAM
  gasnete_coll_team_print(team, stderr);
#endif

  gasneti_fatalerror("[%u] Cannot find local image in set!\n", gasnete_coll_team_my_image(GASNET_TEAM_ALL));
  return 0; /* never reach here */
}

                                           
typedef struct _gasnete_coll_team_member_tuple_t {
  gasnet_image_t team_imageid;
  gasnet_image_t act_imageid;
  gasnet_node_t act_rank;
} gasnete_coll_team_member_tuple_t;

/* team image id comparison function for use in qsort */
static int gasnete_coll_team_image_cmp_func(const void *a, const void *b)
{
  gasnete_coll_team_member_tuple_t *ta = (gasnete_coll_team_member_tuple_t *)a;
  gasnete_coll_team_member_tuple_t *tb = (gasnete_coll_team_member_tuple_t *)b;

  return ((int)ta->act_imageid - (int)tb->act_imageid);
}

/* collective function that should be called by all participating
   nodes of the parent team.  gasnete_coll_team_create() is used to
   create teams other than GASNET_TEAM_ALL which should have been
   specially created in gasnete_coll_init() in
   gasnet_extended_refcoll.c */
gasnet_team_handle_t gasnete_coll_team_create(gasnet_team_handle_t parent_team,
                                              gasnet_image_t total_images,
                                              gasnet_image_t *images,
                                              uint32_t new_team_id,
                                              gasnet_image_t my_new_imageid,
                                              gasnet_seginfo_t *scratch_segs 
                                              GASNETE_THREAD_FARG)
{
  gasnet_team_handle_t new_team;
  uint32_t i, local_image_count;
  gasnet_node_t myrank, total_ranks, my_act_rank, current_act_rank, current_rank;
  gasnet_image_t *all_images, first_local, my_offset, *new_team_local_images;
  gasnete_coll_threaddata_t *td = GASNETE_COLL_MYTHREAD_NOALLOC;
  gasnete_coll_team_member_tuple_t *new_team_members;
  gasnete_coll_team_threaddata_t *team_td;

  team_td = gasnete_coll_team_get_threaddata(parent_team->team_id, td);

  gasneti_assert(images != NULL && total_images > 0);
  gasneti_assert(my_new_imageid < total_images);

  if (my_new_imageid == GASNET_IMAGE_UNDEFINED)
    return NULL;

#ifdef DEBUG_TEAM
  for (i=0; i<parent_team->total_images; i++) {
    if (gasnete_coll_team_my_image(parent_team) == i) {
      fprintf(stderr, "[%u] gasnete_coll_team_create: parent team %p, total_images %u, my_new_imageid %u\n",gasnete_coll_team_my_image(GASNET_TEAM_ALL), parent_team, total_images, my_new_imageid);
      fprintf(stderr, "[%u] gasnete_coll_team_create: images:\n", gasnete_coll_team_my_image(GASNET_TEAM_ALL));
      PRINT_ARRAY(stderr, images, total_images, "%u");
      fprintf(stderr, "\n\n");
      fflush(stderr);
    }
    gasnete_coll_teambarrier(parent_team);
  }
#endif

  first_local = gasnete_coll_team_first_local_image_in_set(parent_team, images, total_images);

#ifdef DEBUG_TEAM
  fprintf(stderr, "[%u] my image in parent team %u, my local image in team %u, first local image %u\n", gasnete_coll_team_my_image(GASNET_TEAM_ALL), gasnete_coll_team_my_image(parent_team), gasnete_coll_team_my_local_image(parent_team GASNETE_THREAD_PASS), first_local);

#endif

  /* Only the first local thread in the process in the new team
     initializes the team data structure shared by other threadds in
     the same process in the team. */
  if (gasnete_coll_team_my_image(parent_team) == first_local) {
    new_team = (gasnet_team_handle_t)gasneti_calloc(1, sizeof(struct gasnete_coll_team_t_));
    new_team->image2rank_map = (gasnet_node_t *)gasneti_malloc(total_images * sizeof(gasnet_node_t));
    new_team->image_rel2act_map = (gasnet_image_t *)gasneti_malloc(total_images * sizeof(gasnet_image_t));
    new_team->team_id = new_team_id;
    all_images = (gasnet_image_t *)gasneti_malloc(total_images*sizeof(gasnet_image_t));
    new_team->rel2act_map = (gasnet_node_t *)gasneti_malloc(total_images*sizeof(gasnet_node_t)); /* to fix with total_ranks */

    new_team_members = (gasnete_coll_team_member_tuple_t *)
      gasneti_malloc(total_images*sizeof(gasnete_coll_team_member_tuple_t));

    for (i=0; i<total_images; i++) {
      new_team_members[i].team_imageid = i;
      new_team_members[i].act_imageid = parent_team->image_rel2act_map[images[i]];
      new_team_members[i].act_rank = gasnete_coll_image2node(parent_team, images[i]);
    }
    
    qsort(new_team_members, total_images, sizeof(gasnete_coll_team_member_tuple_t),
          &gasnete_coll_team_image_cmp_func);

    local_image_count=0;
    current_act_rank = new_team_members[0].act_rank;
    current_rank = 0;
    new_team->rel2act_map[0] = current_act_rank;
    for (i=0; i<total_images; i++) {
      new_team->image_rel2act_map[new_team_members[i].team_imageid] = i;
      if (current_act_rank == new_team_members[i].act_rank) {
        local_image_count++;
      } else {
        local_image_count = 1; /* reset the local_image_count for a new rank */
        current_rank++;
        current_act_rank =  new_team_members[i].act_rank;
        new_team->rel2act_map[current_rank] = current_act_rank;
      }
      all_images[current_rank] = local_image_count;
      new_team->image2rank_map[i] = current_rank;
    }                                                                    

    total_ranks = current_rank+1;
    myrank = new_team->image2rank_map[my_new_imageid];
    new_team->local_images = (gasnet_image_t *) gasneti_calloc(all_images[myrank], sizeof(gasnet_image_t));

    my_act_rank = new_team->rel2act_map[myrank];
    for (my_offset=0; my_offset<total_images; my_offset++) {
      if (new_team_members[my_offset].act_rank == my_act_rank)
        break;
    }
    gasneti_assert(my_offset < total_images);

    new_team_local_images = (gasnet_image_t *)gasneti_malloc(sizeof(gasnet_image_t) * all_images[myrank]);

    for (i=0; i<all_images[myrank]; i++) {
      new_team->local_images[i] = new_team_members[my_offset+i].team_imageid;
    }

#ifdef DEBUG_TEAM
    fprintf(stderr, "local images: ");
    PRINT_ARRAY(stderr, new_team->local_images, all_images[myrank], "%u");
#endif

    initialize_team_fields(new_team, total_images, my_new_imageid, total_ranks, 
                           myrank, all_images, scratch_segs 
                           GASNETE_THREAD_PASS);

    { 
      int current = 0;
      for (i=0; i<parent_team->my_images; i++) {
        /* search can be optimized to local only but need extra data structures */
        if (gasnete_coll_team_find_image(images,
                                         total_images,
                                         parent_team->local_images[i])) {
          new_team_local_images[current] = i;
          current++;
          gasneti_assert(current <= all_images[myrank]);
        }
      }
#ifdef DEBUG_TEAM
      fprintf(stderr, "[%u] new_team_local_images:", gasnete_coll_team_my_image(parent_team));
      PRINT_ARRAY(stderr, new_team_local_images, current, "%u");
      fflush(stderr);
#endif
    }

#ifdef DEBUG_TEAM
    if (new_team->myrank == 0) {
      fprintf(stderr, "[%u] gasnete_coll_team_create: new_team %p, total_images %u, my_new_imageid %u\n", 
              gasnete_coll_team_my_image(GASNET_TEAM_ALL), new_team, total_images, my_new_imageid);
      fprintf(stderr, "total_ranks %u, myrank %u\n", total_ranks, myrank);
      fprintf(stderr, "image_rel2act_map:\n");
      PRINT_ARRAY(stderr, new_team->image_rel2act_map, total_images, "%u");
      fprintf(stderr, "image2rank_map:\n");
      PRINT_ARRAY(stderr, new_team->image2rank_map, total_images, "%u");
      fprintf(stderr, "rel2act_map:\n");
      PRINT_ARRAY(stderr, new_team->rel2act_map, total_ranks, "%u");
      fflush(stderr);
    }
#endif  
    
    gasnete_coll_team_mailbox_copy(parent_team, &new_team, 
                                   new_team_local_images, 
                                   all_images[myrank]);
    gasnete_coll_team_my_mailbox(parent_team).empty = 1;

    gasneti_free(new_team_local_images);
    gasneti_free(all_images);
  } else { 
    /* Read the new team handle from the first local team */
#ifdef DEBUG_TEAM
    fprintf(stderr, "Thread %u before waiting my mailbox.\n", td->my_image);
#endif    
    gasneti_waituntil(gasnete_coll_team_my_mailbox(parent_team).empty == 0);
    new_team = *((gasnet_team_handle_t *)gasnete_coll_team_my_mailbox(parent_team).data);
    gasnete_coll_team_my_mailbox(parent_team).empty = 1;
#ifdef DEBUG_TEAM
    fprintf(stderr, "Thread %u after waiting my mailbox.\n", td->my_image);
#endif    
  }

#ifdef DEBUG_TEAM
  fprintf(stderr, "[%u] gasnete_coll_team_create: before smp init.\n", gasnete_coll_team_my_image(GASNET_TEAM_ALL));
#endif

  /* Initialize thread-specific per-team data */
  {
    gasnete_coll_team_threaddata_t *new_team_td;
    smp_coll_t smp_coll_handle;
    gasnet_image_t my_local_image;

    new_team_td = (gasnete_coll_team_threaddata_t *)gasneti_malloc(sizeof(gasnete_coll_team_threaddata_t));


    /* Find out my local image order */
    for (i=0; i<new_team->my_images; i++) {
      if (new_team->local_images[i] == my_new_imageid) {
        my_local_image = i;
        break;
      }
    }
  
    if (new_team->my_images > 1) { 
      smp_coll_handle = 
        smp_coll_init(1024*1024,
                      (gasnete_coll_smp_tune_barriers==1 ? 0 : SMP_COLL_SKIP_TUNE_BARRIERS),
                      new_team->my_images, my_local_image);
    } else {
      smp_coll_handle = 
        smp_coll_init(1024*1024,
                      (gasnete_coll_smp_tune_barriers==1 ? 0 : SMP_COLL_SKIP_TUNE_BARRIERS),
                      1, 0);
    }
    
    /* each thread inserts the team into its thread-specific direction */
    gasnete_coll_team_td_init(new_team_id, new_team, my_new_imageid, 
                              my_local_image, smp_coll_handle, td);
  }

#ifdef DEBUG_TEAM
  fprintf(stderr, "[%u] gasnete_coll_team_create: after smp init.\n", gasnete_coll_team_my_image(GASNET_TEAM_ALL));
  gasnete_coll_team_print(new_team, stderr);
#endif
      
  return new_team;
}

void gasnete_coll_team_free(gasnet_team_handle_t team GASNETE_THREAD_FARG)
{
  gasneti_assert(team != NULL);
  gasnete_coll_team_fini(team GASNETE_THREAD_PASS);
  gasneti_free(team);
}
 
typedef struct gasnete_coll_team_split_info_t_ {
  gasnet_image_t color;
  gasnet_image_t imageid;
  gasnet_seginfo_t seg_info;
} gasnete_coll_team_split_info_t;

static int gasnete_coll_team_color_cmp_func(const void *a, const void *b)
{
  gasnete_coll_team_split_info_t *pa = (gasnete_coll_team_split_info_t *)a;
  gasnete_coll_team_split_info_t *pb = (gasnete_coll_team_split_info_t *)b;
  
  return ((int)pa->color - (int)pb->color);
}

gasnet_team_handle_t gasnete_coll_team_split(gasnet_team_handle_t parent_team,
                                             gasnet_image_t mycolor,
                                             gasnet_image_t my_image,
                                             gasnet_seginfo_t *myscratch_seginfo
                                             GASNETE_THREAD_FARG)
{
  gasnet_team_handle_t new_team;
  gasnet_image_t *new_team_images;
  gasnet_image_t new_total_images;
  gasnet_seginfo_t *team_segs;
  gasnete_coll_threaddata_t *td = GASNETE_COLL_MYTHREAD;
  gasnete_coll_team_split_info_t my_split_info;
  gasnete_coll_team_split_info_t *all_split_info;
  uint32_t i, new_team_id, my_team_color_rank, new_team_count, current_color;
  volatile uint32_t first_new_team_id;


#ifdef DEBUG_TEAM
  fprintf(stderr, "[%u] gasnete_coll_team_split() begins: parent team handle %p, mycolor %u, my_image %u clientdata %p\n",
          gasnete_coll_team_my_image(GASNET_TEAM_ALL), parent_team, mycolor, my_image, myscratch_seginfo);
  fflush(stderr);
#endif

  /* collect the team split information from all threads */
  all_split_info = (gasnete_coll_team_split_info_t *)
    gasneti_calloc(parent_team->total_images, sizeof(gasnete_coll_team_split_info_t));
  
  new_team_images = (gasnet_image_t *)
    gasneti_calloc(parent_team->total_images, sizeof(gasnet_image_t));
  
  team_segs = (gasnet_seginfo_t *)
    gasneti_calloc(parent_team->total_images, sizeof(gasnet_seginfo_t));
  
  my_split_info.color = mycolor;
  my_split_info.imageid = my_image;
  memcpy(&my_split_info.seg_info, myscratch_seginfo, sizeof(gasnet_seginfo_t));

#ifdef DEBUG_TEAM
  fprintf(stderr, "[%u] gasnete_coll_team_split: before teambarrier. \n",
          gasnete_coll_team_my_image(GASNET_TEAM_ALL));
#endif
  
  gasnete_coll_teambarrier(parent_team);

#ifdef DEBUG_TEAM
  fprintf(stderr, "[%u] gasnete_coll_team_split: before gather_all. parent_team->total_images %u,  all_split_info %p, &all_split_info %p\n",
          gasnete_coll_team_my_image(GASNET_TEAM_ALL), parent_team->total_images, all_split_info, &all_split_info);
#endif


  /* gasnet_coll_gather_all is broken because it tries to use an
     GASNET_COLL_DST_IN_SEGMENT algorithm when the dst is not in
     segment. */
  /*
  gasnet_coll_gather_all(parent_team, all_split_info, &my_split_info, 
                         sizeof(gasnete_coll_team_split_info_t), 
                         GASNET_COLL_LOCAL | GASNET_COLL_IN_MYSYNC | GASNET_COLL_OUT_ALLSYNC);
  */
  gasnet_coll_gather(parent_team, 0, all_split_info, &my_split_info, 
                     sizeof(gasnete_coll_team_split_info_t), 
                     GASNET_COLL_LOCAL | GASNET_COLL_IN_MYSYNC | GASNET_COLL_OUT_ALLSYNC);
 
  gasnet_coll_broadcast(parent_team, all_split_info, 0, all_split_info,
                        sizeof(gasnete_coll_team_split_info_t) * parent_team->total_images, 
                        GASNET_COLL_LOCAL | GASNET_COLL_IN_MYSYNC | GASNET_COLL_OUT_ALLSYNC);

#ifdef DEBUG_TEAM
  fprintf(stderr, "[%u] gasnete_coll_team_split: after gather_all. \n",
          gasnete_coll_team_my_image(GASNET_TEAM_ALL));
#endif

  if (my_image != GASNET_IMAGE_UNDEFINED) {  
    new_total_images = 0;
    for (i=0; i<parent_team->total_images; i++) {
      if (mycolor == all_split_info[i].color) {
        gasneti_assert(all_split_info[i].imageid < parent_team->total_images);
        new_team_images[all_split_info[i].imageid] = i;
        team_segs[all_split_info[i].imageid] = all_split_info[i].seg_info;
        new_total_images++;
      }
    }

    qsort(all_split_info, parent_team->total_images,
          sizeof(gasnete_coll_team_split_info_t),
          &gasnete_coll_team_color_cmp_func);
    
    /* construct the new team based on the team split information and
       the parent team information */
    new_team_count = 0;
    current_color = GASNET_IMAGE_UNDEFINED;
    for (i=0; i<parent_team->total_images; i++) {
      if (all_split_info[i].color != current_color 
          && all_split_info[i].imageid != GASNET_IMAGE_UNDEFINED) {
        new_team_count++;
        current_color = all_split_info[i].color;
      }

      if (mycolor == all_split_info[i].color) {
        my_team_color_rank = new_team_count - 1;
      }
    }
    
    /* Sanity check to prevent unintended errors but some errors may not be detected. */
    for (i=0; i<parent_team->total_images; i++) {
      if (i < new_total_images)
        gasneti_assert(new_team_images[i] < parent_team->total_images);
    }
     
    /* create the new team */
#ifdef DEBUG_TEAM
    fprintf(stderr, "[%u] gasnete_coll_team_split: new_total_images %u, my_image %u, my_team_color %u, new_team_count %u.\n",
            gasnete_coll_team_my_image(GASNET_TEAM_ALL), new_total_images, my_image, my_team_color_rank, new_team_count);
    fprintf(stderr, "[%u] new_team_images:\n", gasnete_coll_team_my_image(GASNET_TEAM_ALL));
    PRINT_ARRAY(stderr, new_team_images, new_total_images, "%u");
    fflush(stderr);
#endif
  }
  
  /* The first image in the parent team request N unique team id's
     from node 0. */
  if (gasnete_coll_team_my_image(parent_team) == 0) {
    first_new_team_id = 0;
    SHORT_REQ(2, 3, (0, gasneti_handleridx(gasnete_coll_teamid_reqh), 
                     new_team_count, PACK(&first_new_team_id)));
    gasneti_waituntil(first_new_team_id != 0);
  }
  
#ifdef DEBUG_TEAM
  fprintf(stderr, "[%u] gasnete_coll_team_split: first_new_team_id %u.\n", 
          gasnete_coll_team_my_image(GASNET_TEAM_ALL), first_new_team_id);
#endif

  /* parent team root broadcast team_ids to all other threads */ 
  gasnet_coll_broadcast(parent_team, (void *)&first_new_team_id, 0, 
                        (void *)&first_new_team_id, sizeof(uint32_t),
                        GASNET_COLL_LOCAL | GASNET_COLL_IN_MYSYNC | GASNET_COLL_OUT_MYSYNC);
  
#ifdef DEBUG_TEAM
  fprintf(stderr, "[%u] gasnete_coll_team_split: first_new_team_id %u, my_team_color_rank %u.\n", gasnete_coll_team_my_image(GASNET_TEAM_ALL), first_new_team_id, my_team_color_rank);
#endif

  
  gasneti_assert(my_image != GASNET_IMAGE_UNDEFINED);
  new_team_id = first_new_team_id + my_team_color_rank;    
  for (i=0; i<new_team_count; i++) {
    /* Create one team at a time to avoid some race conditions in
       smp_coll due to the use of static variables */
    if (my_team_color_rank == i) {
      new_team = gasnete_coll_team_create(parent_team, 
                                          new_total_images, 
                                          new_team_images, 
                                          new_team_id,
                                          my_image,
                                          team_segs 
                                          GASNETE_THREAD_PASS);
    }
    gasnete_coll_team_local_barrier(parent_team);
  }

#ifdef DEBUG_TEAM
  fprintf(stderr, "[%u] gasnete_coll_team_split: finish gasnete_coll_team_create.\n",
          gasnete_coll_team_my_image(GASNET_TEAM_ALL));
#endif
 
  /* Free temporary storage spaces */
  /* Don't free team_segs because it is used in the team and should be
     freed when the team is freed. */
  gasneti_free(all_split_info);
  gasneti_free(new_team_images);

#ifdef DEBUG_TEAM
  fprintf(stderr, "[%u] gasnete_coll_team_split: return new_team %p\n", 
          gasnete_coll_team_my_image(GASNET_TEAM_ALL), new_team);
#endif
  
  gasnete_coll_teambarrier(parent_team);

  return new_team;
}

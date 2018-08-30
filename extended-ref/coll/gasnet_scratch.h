/*   $Source: bitbucket.org:berkeleylab/gasnet.git/extended-ref/coll/gasnet_scratch.h $
 * Description: Reference implemetation of GASNet Collectives team
 * Copyright 2009, Rajesh Nishtala <rajeshn@eecs.berkeley.edu>, Paul H. Hargrove <PHHargrove@lbl.gov>, Dan Bonachea <bonachea@cs.berkeley.edu>
 * Terms of use are as specified in license.txt
 */

/* in all the functions below i assume that the scratch space is no bigger than 4GB*/

#ifndef __GASNET_SCRATCH_H__
#define __GASNET_SCRATCH_H__ 1

#define GASNETE_COLL_SCRATCH_TREE_OP 0
#define GASNETE_COLL_SCRATCH_DISSEM_OP 1


struct gasnete_coll_node_scratch_status_t_;
typedef struct gasnete_coll_node_scratch_status_t_ gasnete_coll_node_scratch_status_t;

/* down tree means we send to relative ranks that are higher than us*/
/* up tree means that we send to relative ranks that are lower than us*/
typedef enum {GASNETE_COLL_UP_TREE=0, GASNETE_COLL_DOWN_TREE} gasnete_coll_tree_dir_t;

typedef enum {GASNETE_COLL_DISSEM_OP=0, GASNETE_COLL_TREE_OP} gasnete_coll_op_type_t;

struct gasnete_coll_scratch_req_t_ {
  // NOTE: freelist linkage may share space with the first field(s)

  gasnete_coll_tree_type_t tree_type;
  gex_Rank_t root;
  gasnete_coll_team_t team;
  /*notice that we don't need to keep track of the dissemination radix since we don't do anything withit */
  
  /* whether this is a tree op where peers are fixed from phase to phase*/
  gasnete_coll_op_type_t op_type;
  gasnete_coll_tree_dir_t tree_dir;
		
  /*this is the sum incoming space of all the peers sending to me*/
  /*for now, for non treeops this is the amount of data that everyone is requesting*/
  uintptr_t incoming_size;
  
  /*information for all the data for which i am the target*/
  /*for non tree ops these values not used*/
  int num_in_peers;
  gex_Rank_t *in_peers;
  
  
  /*information for all the data for which i am an initiator*/
  /*for non tree ops this information is not used*/
  int num_out_peers; 
  gex_Rank_t *out_peers;
  uintptr_t *out_sizes;
  
  // A short sizes array
  // TODO: this could be a flexible array member sized according to team size?
  #define GASNETE_COLL_NUM_SCRATCH_REQ_INLINE_SIZES 8
  uintptr_t inline_sizes[GASNETE_COLL_NUM_SCRATCH_REQ_INLINE_SIZES];
};

// Allocate and free scratch_requests
// TODO-EX:
// Storage is currently manged with a simple per-team freelist with no
// serialization for concurrency.  When multiple endpoints are added then either
// the calls need to be serialized by team->threads_mutex, or a lifo used here.
GASNETI_INLINE(gasnete_coll_scratch_alloc_req) GASNETI_MALLOC
gasnete_coll_scratch_req_t *gasnete_coll_scratch_alloc_req(gasnete_coll_team_t team)
{
  gasnete_coll_scratch_req_t *scratch_req = team->scratch_free_list;
  if_pf (! scratch_req) {
    scratch_req = gasneti_calloc(1,sizeof(gasnete_coll_scratch_req_t));
    scratch_req->team = team;
  } else {
    team->scratch_free_list = *(gasnete_coll_scratch_req_t **)scratch_req;
    gasneti_assert(scratch_req->team == team);
  }
  return scratch_req;
}
GASNETI_INLINE(gasnete_coll_scratch_free_req)
void gasnete_coll_scratch_free_req(gasnete_coll_scratch_req_t *scratch_req)
{
  gasnete_coll_team_t team = scratch_req->team;
  *(gasnete_coll_scratch_req_t **)scratch_req = team->scratch_free_list;
  team->scratch_free_list = scratch_req;
}

// Allocate and free space used for out_sizes
// For small sizes we use space within the scratch request itself
GASNETI_INLINE(gasnete_coll_scratch_alloc_out_sizes)
void gasnete_coll_scratch_alloc_out_sizes(gasnete_coll_scratch_req_t *req, size_t n)
{
  if_pf (n > GASNETE_COLL_NUM_SCRATCH_REQ_INLINE_SIZES) {
    req->out_sizes = gasneti_malloc(n * sizeof(uintptr_t));
  } else {
    req->out_sizes = req->inline_sizes;
  }
}
GASNETI_INLINE(gasnete_coll_scratch_free_out_sizes)
void gasnete_coll_scratch_free_out_sizes(gasnete_coll_scratch_req_t *req)
{
  if_pf (req->out_sizes && (req->out_sizes != req->inline_sizes)) {
    gasneti_free(req->out_sizes);
  }
}

/* try to allocate scratch space*/
/* returns 1 on success or zero on failure*/
int8_t gasnete_coll_scratch_alloc_nb(gasnete_coll_op_t* op GASNETI_THREAD_FARG);


/* release the associated scratch space with this op*/
void gasnete_coll_free_scratch(gasnete_coll_op_t *op);

/* function calls for coll init*/
void gasnete_coll_alloc_new_scratch_status(gasnete_coll_team_t team);
void gasnete_coll_free_scratch_status(gasnete_coll_scratch_status_t *in GASNETI_THREAD_FARG);


#endif

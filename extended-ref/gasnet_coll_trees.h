#ifndef ALREADY_SEEN_GASNET_COLL_TREES_H 
#define ALREADY_SEEN_GASNET_COLL_TREES_H 1

#include <gasnet_coll.h>
#include <gasnet_coll_internal.h>
#include <gasnet_extended_refcoll.h>



#define  GASNETE_COLL_DEFAULT_FANOUT 2
#define  GASNETE_COLL_DEFAULT_RADIX 2


/*ACCESSOR MACROS (all take a gasnete_coll_local_tree_geom_t)*/
#define GASNETE_COLL_TREE_GEOM_ROOT(GEOM) ((GEOM)->root)
#define GASNETE_COLL_TREE_GEOM_PARENT(GEOM) ((GEOM)->parent)
#define GASNETE_COLL_TREE_GEOM_CHILD_COUNT(GEOM) ((GEOM)->child_count)
#define GASNETE_COLL_TREE_GEOM_CHILDREN(GEOM) ((GEOM)->child_list)
#define GASNETE_COLL_TREE_GEOM_SIBLING_ID(GEOM) ((GEOM)->sibling_id)
#define GASNETE_COLL_TREE_GEOM_KIND(GEOM) ((GEOM)->kind)
#define GASNETE_COLL_TREE_GEOM_FANOUT(GEOM) ((GEOM)->fanout)

/* a local view of the tree goemetry */
struct gasnete_coll_local_tree_geom_t_ {
  int allocated;
  /** tree geometry**/
  int fanout;
  gasnet_node_t root;
  gasnete_coll_tree_kind_t kind;
  gasnet_node_t total_size; /*total number of nodes of this geometry*/
  gasnet_node_t parent; /*parent of this node*/
  gasnet_node_t child_count; /*number of children*/
  gasnet_node_t *child_list; /*list of children*/
  gasnet_node_t *subtree_sizes; /* the size of the subtrees under each of our children */
  gasnet_node_t mysubtree_size;
  gasnet_node_t parent_subtree_size; /* size of the subtree under our parent*/
  
  /** sibling information**/
  gasnet_node_t num_siblings;

  gasnet_node_t sibling_id; /*my sibling number*/
  
  /* if the subtree of the parent of this node were to be listed linearly in DFS order, this number indicates
    the position in the parent's list where this node's subtree starts */
  gasnet_node_t sibling_offset;
  
  /* DFS Order of the tree, only assigned at the root node */
  gasnet_node_t *dfs_order;

  /* A boolean variable that is set if the dfs_order of the tree is sequential*/
  /* I.E. No Reordering will be needed for scatter and gathers */
  uint8_t seq_dfs_order;
  
  gasnet_node_t *dissem_order;
  int dissem_count;
  
} ;

/*for now i will only assume that one gasnet thread will be involved in the tree communication 
 and thus assume no locks are needed since only one given thread in a node will ever access the tree*/
 
struct gasnete_coll_tree_geom_t_ {
   /* linked list pointers 
	  used in the caching of tree geometries
   */
   gasnete_coll_tree_geom_t *next;
   gasnete_coll_tree_geom_t *prev;
 /* gasneti_weakatomic_t	ref_count; */
   
   /*an array of local views that represents the global view*/
   gasnete_coll_local_tree_geom_t **local_views; 
   int local_views_allocated;
   
   /*** tree structure metadata*****/
   gasnete_coll_tree_kind_t kind;
   int fanout;
	/* don't need a root argument here since local_views[i] gives a tree rooted at i*/
 };


/* 
   build a full tree with the tree type root and fanout
   the fanout is only applicable for nary trees
 
   This routine first checks the cache for the object
       If the tree type fanout pair exists look in the localviews array and return the appropriate pointer
	   Else 
			if none of the local views are alloc create exactly one geometry at local_views[root]
			else create them all 
			
  (implementation note ... we might change this so that we allocate all the local views)
  (we might need to construct the intermediary views as we run the DFS for some of hte trees so 
  (may as well save the time and do it at one shot)
*/


gasnete_coll_local_tree_geom_t *gasnete_coll_local_tree_geom_fetch(gasnete_coll_tree_kind_t kind, gasnet_node_t root, int fanout, gasnete_coll_team_t team);
void gasnete_coll_local_tree_geom_release(gasnete_coll_local_tree_geom_t *geom);

/*testing functions*/
void gasnet_coll_set_tree_kind(char *treestr);
void gasnet_coll_set_fanout(int fanout);
gasnete_coll_tree_kind_t gasnete_coll_get_current_tree_kind();
int gasnete_coll_get_current_fanout();


/******** Dissemination Ordering **********/
#define GASNETE_COLL_DISSEM_GET_TOTAL_PHASES(DISSEM_INFO) ((DISSEM_INFO)->dissemination_phases)
#define GASNETE_COLL_DISSEM_GET_RADIX(DISSEM_INFO) ((DISSEM_INFO)->dissemination_radix)
#define GASNETE_COLL_DISSEM_MAX_BLOCKS(DISSEM_INFO) ((DISSEM_INFO)->max_dissem_blocks)
#define GASNETE_COLL_DISSEM_NBLOCKS(DISSEM_INFO) ((DISSEM_INFO)->n_blocks)
#define GASNETE_COLL_DISSEM_ALL_REDUCE_OK(DISSEM_INFO) ((DISSEM_INFO)->all_reduce_ok)
#define GASNETE_COLL_DISSEM_GET_PEERS(DISSEM_INFO, PHASE) ((DISSEM_INFO)->barrier_order[(PHASE)].elem_list)
#define GASNETE_COLL_DISSEM_GET_PEER_COUNT(DISSEM_INFO, PHASE) ((DISSEM_INFO)->barrier_order[(PHASE)].n)


struct gasnete_coll_dissem_vector_t_{
  gasnet_node_t *elem_list;
  int n;
};
struct gasnete_coll_dissem_info_t_ {
  gasnete_coll_dissem_info_t *prev;
  gasnete_coll_dissem_info_t *next;
  gasnete_coll_dissem_vector_t *barrier_order;
  gasnete_coll_dissem_vector_t *all_reduce_order;
  int dissemination_phases; /*log_radix(THREADS)*/
  int dissemination_radix;
  int max_dissem_blocks;
 
  /*an array that holds the number of blocks we send in each phase of the 
    dissemination all to all. Used to capture when nblocks is 1 to avoid copies*/
  int *n_blocks; 
  /*whether this dissem obj is designed to run the all_reduce*/
  /*only true when power of two proc count AND radix is 2*/
  int all_reduce_ok; 
};

gasnete_coll_dissem_info_t *gasnete_coll_fetch_dissemination(int radix, gasnete_coll_team_t team);
void gasnete_coll_release_dissemination(gasnete_coll_dissem_info_t* obj, gasnete_coll_team_t team);
/*****************************************/

#endif

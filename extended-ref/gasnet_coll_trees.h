#ifndef ALREADY_SEEN_GASNET_COLL_TREES_H 
#define ALREADY_SEEN_GASNET_COLL_TREES_H 1
#define  GASNETE_COLL_DEFAULT_FANOUT 2


typedef enum {GASNETE_COLL_NARY_TREE=0, GASNETE_COLL_BINOMIAL_TREE} gasnete_coll_tree_kind_t;

typedef struct gasnete_coll_tree_geom_t_ {
  /*** tree structure metadata*****/
  gasnete_coll_tree_kind_t kind;
  int fanout;
  gasnet_node_t root;					
  int threads_per_node;
  gasneti_weakatomic_t	ref_count;

  
  /** tree geometry**/
  gasnet_node_t parent; /*parent of this node*/
  int child_count; /*number of children*/
  gasnet_node_t *child_list; /*list of children*/
  
  /** sibling information**/
  int num_siblings;
  gasnet_node_t *sibling_list; /*list of siblings*/
  int *sibling_subtree_sizes; /*sizes of the subtrees under the siblings*/
  int sibling_id; /*my sibling number*/
  
  /*** subtree information***/
  int *subtree_sizes;
  gasnet_node_t *subtree;
  int subtree_count;

  gasnet_node_t *dissem_order;
  int dissem_count;
} gasnete_coll_tree_geom_t;



/* 
   build a full tree with the tree type root and fanout
   the fanout is only applicable for nary trees
   the function allocates and returns a tree geometry object
*/


gasnete_coll_tree_geom_t* gasnete_coll_tree_geom_init(gasnete_coll_tree_kind_t kind, int fanout, int root, int threads_per_node);


/*destroy the tree object*/

/*void free_tree(gasnete_coll_tree_geom_t *obj);*/
#endif

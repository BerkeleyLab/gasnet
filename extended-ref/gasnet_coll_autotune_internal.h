/* 
 * Description: Files for Autotuner
 * Copyright 2007, Rajesh Nishtala <rajeshn@eecs.berkeley.edu> Dan Bonachea <bonachea@cs.berkeley.edu>
 * Terms of use are as specified in license.txt
 */

/*During the collective initialization process all the algorithms are registered
 (including those defined by the conduit) with the auto-tuner
 
 given the capability requirements of the algorithms (i.e. synch modes, teh size, and whether or not the source and dest are inthe segment)
 the autotuner can returns a triple of (number of valid algorithms, number of tuning parameters, and the range of each tunign parameter)
 
 thus the query functions will return the number 
 */


#ifndef __GASNET_COLL_AUTOTUNE_INTERNAL_H__
#define __GASNET_COLL_AUTOTUNE_INTERNAL_H__ 1

#define GASNETE_COLL_DEFAULT_TREE_TYPE_STR "BINOMIAL_TREE"
#define GASNETE_COLL_DEFAULT_TREE_FANOUT 2
#define GASNETE_COLL_DEFAULT_DISSEM_LIMIT_PER_THREAD 1024
#include <gasnet_coll_autotune.h>


typedef gasnet_coll_handle_t (*gasnete_coll_bcast_fn_ptr_t)(gasnet_team_handle_t team,
                                            void * dst,
                                            gasnet_image_t srcimage, void *src,
                                            size_t nbytes, int flags,
                                            gasnete_coll_implementation_t coll_params,
                                            uint32_t sequence
                                            GASNETE_THREAD_FARG);


typedef enum {GASNETE_COLL_BROADCAST_PUT=0, 
              GASNETE_COLL_BROADCAST_GET,
              GASNETE_COLL_BROADCAST_TREE_PUT,
              GASNETE_COLL_BROADCAST_TREE_PUT_SCRATCH,
              GASNETE_COLL_BROADCAST_TREE_PUT_SEG,
              GASNETE_COLL_BROADCAST_EAGER,
              GASNETE_COLL_BROADCAST_TREE_EAGER,
              GASNETE_COLL_BROADCAST_RVOUS,
              GASNETE_COLL_BROADCAST_RVGET,
              /*check to see if the conduits have defined any new ops*/
              GASNETE_COLL_BROADCAST_NUM_ALGS} gasnete_coll_broadcast_alg_types_t;

typedef enum {GASNETE_COLL_BROADCASTM_PUT=0, 
  GASNETE_COLL_BROADCASTM_GET,
  /*check to see if the conduits have defined any new ops*/
GASNETE_COLL_BROADCASTM_NUM_ALGS} gasnete_coll_broadcastM_alg_types_t;

typedef enum {GASNETE_COLL_SCATTER_NUM_ALGS=0} gasnete_coll_scatter_alg_types_t;
typedef enum {GASNETE_COLL_SCATTERM_NUM_ALGS=0} gasnete_coll_scatterM_alg_types_t;

typedef enum {GASNETE_COLL_GATHER_NUM_ALGS=0} gasnete_coll_gather_alg_types_t;
typedef enum {GASNETE_COLL_GATHERM_NUM_ALGS=0} gasnete_coll_gatherM_alg_types_t;

typedef enum {GASNETE_COLL_GATHER_ALL_NUM_ALGS=0} gasnete_coll_gather_all_alg_types_t;
typedef enum {GASNETE_COLL_GATHER_ALLM_NUM_ALGS=0} gasnete_coll_gather_allM_alg_types_t;

typedef enum {GASNETE_COLL_EXCHANGE_NUM_ALGS=0} gasnete_coll_exchange_alg_types_t;
typedef enum {GASNETE_COLL_EXCHANGEM_NUM_ALGS=0} gasnete_coll_exchangeM_alg_types_t;

/*returns the implementation of the collectives including all the parameters to the algorithm*/
struct gasnete_coll_implementation_t_{
  void* fn_ptr;
  int num_params;
  uint32_t param_list[GASNET_COLL_NUM_PARAM_TYPES]; /*declare an array that can take all the possible param types*/
};

/*contains an entry in the function table*/
typedef struct gasnete_coll_allgorithm_t_ {
  struct gasnete_coll_allgorithm_t_ *next;
  
  /*what kind of op is this*/
  gasnet_coll_optype_t optype;
  
  /*for what synch flags does this algorithm work*/
  uint32_t syncflags;
  
  /*what other input flags are required for this algorithm to work*/
  /*thus if the input flags and the requirements are anded together
   and the result is equal to requirements then the collective will work
   for those flags*/
  uint32_t requirements;
  
  /*the maximum number of bytes as an argument that this algorithm can handle*/
  /*probably will be based on maximum AM lengths or lenghts of largest transfers*/
  /* a size of 0 indicates that it will work for all sizes*/
  size_t max_num_bytes;
  
  /*what are the parameters to the algorithm*/
  uint32_t num_parameters;
  
  struct gasnet_coll_tuning_parameter_t *parameter_list;
  
  union {
    gasnete_coll_bcast_fn_ptr_t bcast_fn;
  } fn_ptr;
  
} gasnete_coll_algorithm_t;




gasnete_coll_autotune_info_t* gasnete_coll_autotune_init(gasnet_team_handle_t team, gasnet_node_t mynode, gasnet_node_t total_nodes, 
                                                         gasnet_image_t my_images, gasnet_image_t total_images, 
                                                         size_t min_scratch_size);
/*testing functions*/

gasnete_coll_tree_type_t gasnete_coll_autotune_get_tree_type(gasnete_coll_autotune_info_t* autotune_info, 
                                                             gasnet_coll_optype_t op_type, 
                                                             gasnet_node_t root, size_t nbytes, int flags);

gasnete_coll_algorithm_t gasnete_coll_autotune_register_algorithm(gasnet_coll_optype_t optype, 
                                                                  uint32_t syncflags,
                                                                  uint32_t requirements,
                                                                  size_t max_size,
                                                                  uint32_t num_params,
                                                                  struct gasnet_coll_tuning_parameter_t *param_list,
                                                                  void *coll_fnptr);

size_t gasnete_coll_get_dissem_limit(gasnete_coll_autotune_info_t* autotune_info, gasnet_coll_optype_t op_type, int flags);

size_t gasnete_coll_get_pipe_seg_size(gasnete_coll_autotune_info_t* autotune_info, gasnet_coll_optype_t op_type, int flags);

gasnete_coll_implementation_t gasnete_coll_autotune_get_bcast_algorithm(gasnet_team_handle_t team, uint32_t flags, size_t nbytes);
#endif

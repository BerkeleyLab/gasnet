/* 
* Description: Files for Autotuner
* Copyright 2007, Rajesh Nishtala <rajeshn@eecs.berkeley.edu> Dan Bonachea <bonachea@cs.berkeley.edu>
* Terms of use are as specified in license.txt
*/

/* This is intended as a stub for the autotuner routines*/

#ifndef __GASNET_COLL_AUTOTUNE_H__
#define __GASNET_COLL_AUTOTUNE_H__ 1


/*---------------------------------------------------------------------------------*
 * Prototypes for external interface to try different collective trees (only works for GASNet Team All)
 * Note that the preffered way for changing these values is in the environment rather than these functions themselves
 *---------------------------------------------------------------------------------*/

typedef enum {GASNET_COLL_BROADCAST_OP=0, 
  GASNET_COLL_BROADCASTM_OP, 
  GASNET_COLL_SCATTER_OP, 
  GASNET_COLL_SCATTERM_OP, 
  GASNET_COLL_GATHER_OP, 
  GASNET_COLL_GATHERM_OP, 
  GASNET_COLL_GATHER_ALL_OP,
  GASNET_COLL_GATHER_ALLM_OP,
  GASNET_COLL_EXCHANGE_OP,
  GASNET_COLL_EXCHANGEM_OP, 
  GASNET_COLL_NUM_COLL_OPTYPES
} gasnet_coll_optype_t;

typedef enum {GASNET_COLL_TREE_CLASS=0, GASNET_COLL_TREE_FANOUT, GASNET_COLL_PIPE_SEG_SIZE, GASNET_COLL_DISSEM_RADIX, 
  /*check to see if hte conduit has added any new tuning parameters to this list*/
GASNET_COLL_NUM_PARAM_TYPES} gasnet_coll_tuning_param_type_t ;

#ifndef GASNET_COLL_MIN_PIPE_SEG_SIZE
#define GASNET_COLL_MIN_PIPE_SEG_SIZE 128
#endif

#ifndef GASNET_COLL_MAX_PIPE_SEG_SIZE
#define GASNET_COLL_MAX_PIPE_SEG_SIZE gasnet_AMMaxLongRequest()
#endif

/*flags to control how the search space looks like*/
#define GASNET_COLL_TUNING_STRIDE_ADD (1<<0)
#define GASNET_COLL_TUNING_STRIDE_MULTIPLY (1<<1)

struct gasnet_coll_tuning_parameter_t {
  gasnet_coll_tuning_param_type_t tuning_param;
  uint32_t start;
  uint32_t end;
  uint32_t stride;
  int flags;
};


extern uint32_t gasnet_coll_get_algs(gasnet_team_handle_t team, gasnet_coll_optype_t op, size_t nbytes, uint32_t flags, uint32_t** outlist, uint32_t* num_algs_ret);
extern int gasnet_coll_get_num_params(gasnet_team_handle_t team, gasnet_coll_optype_t op, uint32_t algorithm_num);
extern struct gasnet_coll_tuning_parameter_t gasnet_coll_get_param(gasnet_team_handle_t team, gasnet_coll_optype_t op, uint32_t algorithm_num, uint32_t param_idx);

extern int gasnet_coll_get_num_tree_classes(gasnet_team_handle_t team, gasnet_coll_optype_t optype);
extern void gasnet_coll_set_tree_kind(gasnet_team_handle_t team, int tree_type, int fanout, gasnet_coll_optype_t optype); 
extern void gasnet_coll_set_dissem_limit(gasnet_team_handle_t team, size_t dissemlimit, gasnet_coll_optype_t optype); 

#endif

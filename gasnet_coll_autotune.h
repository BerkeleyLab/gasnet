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
              GASNET_COLL_SCATTER_OP, 
              GASNET_COLL_GATHER_OP, 
              GASNET_COLL_GATHER_ALL_OP,
              GASNET_COLL_EXCHANGE_OP,
              GASNET_COLL_NUM_OP_TYPES} gasnet_coll_optype_t;

extern int gasnet_coll_get_num_tree_classes(gasnet_team_handle_t team, gasnet_coll_optype_t optype);
extern void gasnet_coll_set_tree_kind(gasnet_team_handle_t team, int tree_type, int fanout, gasnet_coll_optype_t optype); 
extern void gasnet_coll_set_dissem_limit(gasnet_team_handle_t team, size_t dissemlimit, gasnet_coll_optype_t optype); 

#endif

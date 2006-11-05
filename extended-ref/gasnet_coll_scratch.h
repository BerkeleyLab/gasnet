/*
 *  gasnet_coll_scratch.h
 *  gasnet_tree_colluint32_t
 *
 *  Created by Rajesh Nishtala on 10/23/06.
 *  Copyright 2006 Berkeley UPC. All rights reserved.
 *
 */
 
 /* in all the functions below i assume that the scratch space is no bigger than 4GB*/
 
#ifndef __GASNET_COLL_SCRATCH_H__
#define __GASNET_COLL_SCRATCH_H__ 1

#define GASNETE_COLL_NUM_SCRATCH_HANDLERS 1
#ifndef GASNETE_COLL_SCRATCH_HANDLER_BASE
#define GASNETE_COLL_SCRATCH_HANDLER_BASE (GASNETE_COLL_HANDLER_BASE-GASNETE_COLL_NUM_SCRATCH_HANDLERS)
#endif

#define _hidx_gasnete_coll_scratch_update_reqh (GASNETE_COLL_SCRATCH_HANDLER_BASE+0)


struct gasnete_coll_scratch_req_t_;
typedef struct gasnete_coll_scratch_req_t_ gasnete_coll_scratch_req_t;

struct gasnete_coll_node_scratch_status_t_;
typedef struct gasnete_coll_node_scratch_status_t_ gasnete_coll_node_scratch_status_t;

struct gasnete_coll_op_info_t_;
typedef struct gasnete_coll_op_info_t_ gasnete_coll_op_info_t;

struct gasnete_coll_scratch_req_t_ {

	gasnete_coll_tree_kind_t tree_type;
	int fanout;
	gasnet_node_t root;
	gasnete_coll_team_t team;
	/*notice that we don't need to keep track of the dissemination radix since we don't do anything withit */

	/* whether this is a tree op where peers are fixed from phase to phase*/
	int tree_op;
	
		
	/*this is the sum incoming space of all the peers sending to me*/
	/*for now, for non treeops this is the amount of data that everyone is requesting*/
	uint32_t incoming_size; 
	
	/*information for all the data for which i am the target*/
	/*for non tree ops these values*/
	int num_in_peers;
  gasnet_node_t *in_peers;

	
	/*information for all the data for which i am an initiator*/
	/*for non tree ops this information is not used*/
	int num_out_peers; 
	gasnet_node_t *out_peers;
	uint32_t *out_sizes;
	

};
struct gasnete_coll_node_scratch_status_t_  {
  /*head and tail of the circular buffer that represents the active scratch space on a particular node*/
  uint64_t head;
  
  /*since the tail is the only one that gets updated by the active message handlers it needs to be the atomic one*/
  gasnett_atomic64_t tail;
  gasnett_atomic_t new_val;
};

struct gasnete_coll_op_info_t_ {

	gasnete_coll_op_info_t *next;
	gasnete_coll_op_info_t *prev;
	
	gasnete_coll_tree_kind_t tree_type;
	int tree_fanout;
	gasnet_node_t root;
	
	int tree_op;
	
	/* a pointer to the actual op handle so that we can do a wait sync on it */
	gasnet_coll_handle_t op_handle; 
	
	/*amount of scratch space used locally*/
	uint32_t local_scratch_used;
	
	uint32_t seq_number;
	
	/*is this operation finished*/
	int done;


};

/*this structure describes an operation info*/
struct gasnete_coll_scratch_status_t_ {
  /*creates an array of node statuses*/
  /* for now allocate something that is gasneti_nodes in length*/
  /* could change this later*/
  gasnete_coll_node_scratch_status_t *node_status;
  
  /* a list of the active ops that use the scratch space*/
  gasnete_coll_op_info_t *active_scratch_op_head;
  gasnete_coll_op_info_t *active_scratch_op_tail;
  
  gasnete_coll_team_t team;
  
  gasnete_coll_tree_kind_t curr_tree_type;
  int curr_tree_fanout;
  gasnet_node_t curr_root;
  
  uint8_t perform_reset;
  
  /*an indicator telling you whether the upcoming collective op is the first after a barrier*/
  uint8_t first_collective;
  
  /*nodes that will send to me*/
  int numpeers;
  gasnet_node_t *peers;
  

};

void gasnete_coll_alloc_new_scratch_status(gasnete_coll_team_t team);
void gasnete_coll_free_scratch_status(gasnete_coll_scratch_status_t *in);

/* 
   upon a barrier or OUT_ALL_SYNC collective we can reset the scratch status since all nodes will 
   have finished their data movement and all ops before this will have completed
   
	This operation will go through and reset all head and tail pointers and throw away 
	the history list unconditionally
*/
void gasnete_coll_reset_scratch_status(gasnete_coll_scratch_status_t *in);

/*
  This operation in essance advances the position of my scratch position
  If this operation would advance the head past the tail (or advance the head past the end of the buffer)
	Look through the list of ops that have been registered on this scratch space and advance the tail through 
	all the ops the have finished
	(we also implicitly know that the parent is in the exact same situation)
	send the parent an updated view of the tail so that they can restart their algorithm
	
*/
uint64_t gasnete_coll_scratch_new_op(gasnete_coll_scratch_req_t *scratch_req, uint32_t seq, gasnet_coll_handle_t op_handle  GASNETE_THREAD_FARG);

/* 
   Get the latest pointer and advance the scratch space view 
   if i notice that there isn't enough scratch space available i have to wait until
   the child updates my view of that child
*/

uint64_t *gasnete_coll_scratch_get_peer_pos(gasnete_coll_scratch_req_t *scratch_req GASNETE_THREAD_FARG);

/* 
	This function will be called from within gasnet_coll_poll so it needs to be done quickly
	It will first go through the active list and find the op with the matching sequence number
	Then it will take that sequence number and mark it as finished.
*/

void gasnete_coll_free_scratch(gasnete_coll_op_t *op);
/*four args: team id, node id, seq number, head, tail*/
SHORT_HANDLER_NOBITS_DECL(gasnete_coll_scratch_update_reqh, 4);
#define GASNETE_COLL_SCRATCH_HANDLERS() gasneti_handler_tableentry_no_bits(gasnete_coll_scratch_update_reqh),

#endif

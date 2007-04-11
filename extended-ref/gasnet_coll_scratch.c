/*
 *  gasnet_coll_scratch.c
 *  gasnet_tree_coll
 *
 *  Created by Rajesh Nishtala on 10/23/06.
 *  Copyright 2006 Berkeley UPC. All rights reserved.
 *
 */


#include "gasnet_coll_scratch.h"
void gasnete_coll_alloc_new_scratch_status(gasnete_coll_team_t team) {
  gasnete_coll_scratch_status_t *stat;
  int i;
  
  stat = (gasnete_coll_scratch_status_t*) gasneti_malloc(sizeof(gasnete_coll_scratch_status_t));
  stat->node_status = (gasnete_coll_node_scratch_status_t*)gasneti_malloc(sizeof(gasnete_coll_node_scratch_status_t)*team->total_ranks);
  
  stat->active_scratch_op_head = NULL;
  stat->active_scratch_op_tail = NULL;
  stat->team = team;
  stat->perform_reset = 0;
  stat->numpeers = 0;
  stat->peers = NULL;
  stat->first_collective = 1;
  for(i=0; i<team->total_ranks; i++) {
    stat->node_status[i].head = 0;
    gasnett_atomic64_set(&(stat->node_status[i].tail),0,0); 
    gasnett_atomic_set(&(stat->node_status[i].new_val),0,0);
  }
  team->scratch_status = stat;
 
}


void gasnete_coll_free_scratch_status(gasnete_coll_scratch_status_t *in) {
  if(in !=NULL) {
    /*throws away the log*/
    gasnete_coll_reset_scratch_status(in);
    gasneti_free(in->node_status);
    gasneti_free(in);
  }

}

/* reset the scratch status ... change later to use a proper free list ... for now just use malloc and free*/
void gasnete_coll_scratch_wait_for_all_ops(gasnete_coll_team_t team GASNETE_THREAD_FARG) {

  gasnete_coll_scratch_status_t* stat = team->scratch_status;
  gasnete_coll_op_info_t *temp;	
  /*fprintf(stderr, "%d> waiting for all ops to drain\n", gasneti_mynode); */
  while(stat->active_scratch_op_head!=NULL) {
    temp = stat->active_scratch_op_head;
#if 0
    if(temp->done != 1) {
      gasnete_coll_wait_sync(temp->op_handle GASNETE_THREAD_PASS);
      temp->done = 1;
    }
#else 
    while(temp->done !=1) {
      gasnete_coll_poll();
    }
#endif
    stat->active_scratch_op_head = stat->active_scratch_op_head->next;
    gasneti_free(temp);
  }
 /* fprintf(stderr, "%d> all ops drained\n", gasneti_mynode); */
  stat->active_scratch_op_head = NULL;	
  stat->active_scratch_op_tail = NULL;

}

void gasnete_coll_reset_scratch_status(gasnete_coll_scratch_status_t *in) {
  gasnete_coll_op_info_t *temp;
  int i;
  gasnete_coll_scratch_wait_for_all_ops(in->team);
  /*reset all the node_status back to 0*/		
  for(i=0; i<in->team->total_ranks; i++) {
    in->node_status[i].head = 0; 
    gasnett_atomic64_set(&(in->node_status[i].tail),0,0); 
    gasnett_atomic_set(&(in->node_status[i].new_val),0,0);
  }


#if 0	
  temp = in->active_scratch_op_head;
  while(temp !=NULL) {
    in->active_scratch_op_head = in->active_scratch_op_head->next;
    gasneti_free(temp);
    temp = in->active_scratch_op_head;
  }
  in->active_scratch_op_head = NULL;
  in->active_scratch_op_tail = NULL;
#endif	
  /*
    notice that we do not throw away the pending status updates since the dst nodes could have sent
    the updates for the next barrier phase and therefore throwing htem away is incorrect.
	  
    Since the dst's view is consistent with our own applying the updates in order will still yield the correct
    result
  */
  in->perform_reset = 0;
}


/* 
   this operation looks through the local scratch and an incoming size and waits for enough ops to clear to be able to allocate
   the piece that is requested. This operation has to succeed since we have already checked that the incoming 
   scratch size is less than the total available scratch space
*/


/*
  This operation in essance advances the position of my scratch position
  If this operation would advance the head past the tail (or advance the head past the end of the buffer)
  Look through the list of ops that have been registered on this scratch space and advance the tail through 
  all the ops the have finished
  (we also implicitly know that the parent is in the exact same situation)
  send the parent an updated view of the tail so that they can restart their algorithm
	
*/

void gasnete_coll_scratch_send_updates(gasnete_coll_team_t team, uint64_t tail) {
  int i;
  
  /*Becareful with the teams here and how the peer list is specified*/
  /*for gasnet team all it doesn't matter but in other cases it does*/
  gasnete_coll_scratch_status_t *stat = team->scratch_status;
  for(i=0; i<stat->numpeers; i++) {
    /*		fprintf(stderr, "%d> sending %d  a clear signal\n", gasneti_mynode,stat->peers[i]); */
    GASNETI_SAFE(SHORT_REQ(4,4,(stat->peers[i],gasneti_handleridx(gasnete_coll_scratch_update_reqh),
				team->team_id, team->myrank, GASNETI_HIWORD(tail), GASNETI_LOWORD(tail))));
    
  }
}


uint64_t gasnete_coll_scratch_new_tree_op(gasnete_coll_scratch_req_t *scratch_req, uint32_t seq, gasnet_coll_handle_t op_handle GASNETE_THREAD_FARG) {
  
  gasnete_coll_scratch_status_t *stat = scratch_req->team->scratch_status;
  gasnete_coll_op_info_t *new_op;
  gasnete_coll_node_scratch_status_t node_stat;
  uint32_t my_head_pos;
  uint32_t my_tail_pos;
  uint64_t retpos;
  /*if the incoming size is greater than the total allocated scratch space signal an error*/
  if(scratch_req->incoming_size > scratch_req->team->scratch_segs[scratch_req->team->myrank].size) {
    gasneti_fatalerror("%d> collective requires temporary storage (%d bytes) is greater than total scratch space (%d bytes) \n consider using pipelined algorithms or increasing size of collective scratch space\n", scratch_req->team->myrank, (int)scratch_req->incoming_size, (int)scratch_req->team->scratch_segs[0].size); 
  }
  my_head_pos = stat->node_status[scratch_req->team->myrank].head;
  my_tail_pos = gasnett_atomic64_read(&(stat->node_status[scratch_req->team->myrank].tail),0);
  
  
  /*first time around register the tree geometry that is used*/
  
  if(stat->first_collective==1) {
    stat->curr_root = scratch_req->root;
    stat->curr_tree_type = scratch_req->tree_type;
    stat->numpeers = scratch_req->num_in_peers;
    stat->curr_tree_dir = scratch_req->tree_dir;
    if(scratch_req->num_in_peers>0) {
      stat->peers = (gasnet_node_t*) gasneti_malloc(sizeof(gasnet_node_t)*scratch_req->num_in_peers);
      GASNETE_FAST_UNALIGNED_MEMCPY(stat->peers,scratch_req->in_peers,sizeof(gasnet_node_t)*scratch_req->num_in_peers);
    } else {
      stat->peers = NULL;
    }
    stat->first_collective=0;
  }   else if((stat->curr_root !=scratch_req->root) || 
	      (stat->curr_tree_type.tree_class !=scratch_req->tree_type.tree_class) ||
	      ((stat->curr_tree_type.tree_class != GASNETE_COLL_BINOMIAL_TREE) && 
	       (stat->curr_tree_type.fanout != scratch_req->tree_type.fanout)) || 
              stat->last_op == GASNETE_COLL_SCRATCH_DISSEM_OP ||
              stat->curr_tree_dir != scratch_req->tree_dir) {
#if 0		
    if(gasneti_mynode ==0) fprintf(stderr, "TREE CHANGE w/o BARRIER! inserting barrier and reseting scratch\n");
#endif
    /* perform barrier and reset scratch */
    gasnete_coll_consensus_wait();
/*    gasnet_barrier_notify(0, GASNET_BARRIERFLAG_ANONYMOUS);
    gasnet_barrier_wait(0, GASNET_BARRIERFLAG_ANONYMOUS); */
    /*the barrier will have the call to trip the stat->perform_reset so we avoid the explicit call to reset here*/
    stat->curr_root = scratch_req->root;
    stat->curr_tree_type = scratch_req->tree_type;
    stat->numpeers = scratch_req->num_in_peers;
    stat->curr_tree_dir = scratch_req->tree_dir;
    
    if(scratch_req->num_in_peers>0) {
      stat->peers = (gasnet_node_t*) gasneti_malloc(sizeof(gasnet_node_t)*scratch_req->num_in_peers);
      GASNETE_FAST_UNALIGNED_MEMCPY(stat->peers,scratch_req->in_peers,sizeof(gasnet_node_t)*scratch_req->num_in_peers);
    } else {
      stat->peers = NULL;
    }
  } else { /* tree geometry has not changed between last op and this op */
  }
  stat->last_op = GASNETE_COLL_SCRATCH_TREE_OP;
  /*if we saw a barrier or out all_sync between our last op and this one perform the reset*/
  if(stat->perform_reset == 1) {
    gasnete_coll_reset_scratch_status(stat);
  }
  if(my_head_pos >= my_tail_pos) {
   /* fprintf(stderr, "%d> head: %d size: %d total size: %d\n", gasneti_mynode, (int) my_head_pos, (int)scratch_req->incoming_size, 
            (int) scratch_req->team->scratch_segs[scratch_req->team->myrank].size);*/
    /* if the tail is behind or equal to the head then check to see if there is enough scratch space to the end*/
    if(my_head_pos + scratch_req->incoming_size > scratch_req->team->scratch_segs[scratch_req->team->myrank].size) {
      /* wait for collective ops to clear <-- this function will update the head and the tail*/
      gasnete_coll_scratch_wait_for_all_ops(scratch_req->team GASNETE_THREAD_PASS);
      /* send a message to peers sending to me for updating my head and tail pointers */
      gasnete_coll_scratch_send_updates(scratch_req->team,0);
      stat->node_status[scratch_req->team->myrank].head = scratch_req->incoming_size;
      gasnett_atomic64_set(&(stat->node_status[scratch_req->team->myrank].tail),0,0);
      retpos = 0;
    } else {
      /* advance scratch pointers and move on*/
      retpos = stat->node_status[scratch_req->team->myrank].head;
      stat->node_status[scratch_req->team->myrank].head += scratch_req->incoming_size;
      
      
    }
  } else { /* tail position is farther ahead than head */
    /*check to see if there is enough space between the head and the tail.*/
    if(my_head_pos + scratch_req->incoming_size > my_tail_pos) {
      /* wait for collective ops to clear <-- this function will update the head and the tail*/
      gasnete_coll_scratch_wait_for_all_ops(scratch_req->team GASNETE_THREAD_PASS);
      /* send a message to peers sending to me for updating my head and tail pointers */
      gasnete_coll_scratch_send_updates(scratch_req->team,0);
      stat->node_status[scratch_req->team->myrank].head = scratch_req->incoming_size;
      gasnett_atomic64_set(&(stat->node_status[scratch_req->team->myrank].tail),0,0);
      retpos = 0;
    } else {
      /* adcance scratch pointers and move on*/
      retpos = stat->node_status[scratch_req->team->myrank].head;
      stat->node_status[scratch_req->team->myrank].head += scratch_req->incoming_size;
      
    }
  }
  
  /* allocate a new op */
  new_op = (gasnete_coll_op_info_t*) gasneti_malloc(sizeof(gasnete_coll_op_info_t));
  new_op->next = NULL;
  new_op->prev = NULL;
  new_op->tree_type  = scratch_req->tree_type;
  new_op->root = scratch_req->root;
  new_op->local_scratch_used = scratch_req->incoming_size;
  new_op->op_handle = op_handle;
  new_op->seq_number = seq;
  new_op->done = 0;
  
  /*link the new op in on the tail*/
  if(stat->active_scratch_op_head == NULL) {
    stat->active_scratch_op_head = stat->active_scratch_op_tail = new_op;
    new_op->next = NULL;
    new_op->prev = NULL;
  }  else {
    new_op->next = NULL;
    new_op->prev = stat->active_scratch_op_tail;
    stat->active_scratch_op_tail->next = new_op;
    stat->active_scratch_op_tail = new_op;
    
  }
  return retpos;
  
}



uint64_t gasnete_coll_scratch_tree_get_pos(gasnet_node_t dst, uint32_t req_size, gasnete_coll_tree_type_t tree_type, 
                                           gasnet_node_t root, gasnete_coll_team_t team GASNETE_THREAD_FARG) {
  gasnete_coll_scratch_status_t *stat= team->scratch_status;
  uint64_t dst_head_pos; 
  uint64_t dst_tail_pos; 
  uint32_t retpos;
	
  if(req_size > team->scratch_segs[dst].size) {
      gasneti_fatalerror("%d> collective temporary storage request(%d bytes) on dst %d is greater than total scratch space (%d bytes) \n consider using pipelined algorithms or increasing size of collective scratch space\n", team->myrank, (int)req_size, dst, (int)team->scratch_segs[dst].size); 
  }
  dst_head_pos = stat->node_status[dst].head;
  dst_tail_pos = gasnett_atomic64_read(&(stat->node_status[dst].tail),0);
  
  /* 
     no need to cehck the tree goemetry here since all nodes will call register themselves on
     the scratch space before requesting scratch space on dstren
  */
  if(dst_head_pos >= dst_tail_pos) {
    /* if the tail is behind or equal to the head then check to see if there is enough scratch space to the end*/
    if(dst_head_pos + req_size > team->scratch_segs[dst].size) {
      /* wait for dst to send updates of head and tail pointers <-- function will update head and tail pointers*/
   
      /*XXX Use compare and Swap here*/
      while(gasnett_atomic_read(&(stat->node_status[dst].new_val),0) ==0) gasnet_AMPoll();
      gasnett_atomic_set(&(stat->node_status[dst].new_val),0,0);
   
      stat->node_status[dst].head = req_size;
      return 0;
    } else {
      retpos = stat->node_status[dst].head;
      stat->node_status[dst].head += req_size;
      return retpos;
    }
  } else { /* tail position is farther ahead than head */
    /*check to see if there is enough space between the head and the tail.*/
    if(dst_head_pos + req_size > dst_tail_pos) {
      /* wait for dst to send updates of head and tail pointers <-- function will update head and tail pointers*/
      /* for now wait for the tail set to drop back to 0*/
      
      /*XXX Use compare and Swap here*/
      while(gasnett_atomic_read(&(stat->node_status[dst].new_val),0) ==0) gasnet_AMPoll();
      gasnett_atomic_set(&(stat->node_status[dst].new_val),0,0);

      stat->node_status[dst].head = req_size;
      return 0;
    } else {
      /* allocate scratch and move on*/
      retpos = stat->node_status[dst].head;
      stat->node_status[dst].head += req_size;
      return retpos;
      
    }
  } 
  gasneti_fatalerror("%d> should never get here in scratch get pos.\n", team->myrank);
  return -1;
  
}

void gasnete_coll_free_scratch(gasnete_coll_op_t *op) {
  uint32_t seqnum = op->sequence;
  gasnete_coll_scratch_status_t *stat= op->team->scratch_status;
  gasnete_coll_op_info_t *temp = stat->active_scratch_op_head;
  
  /*walk through the history of ops and mark the corresponding op as done*/
  while(temp != NULL) {
    if(temp->seq_number == seqnum) {
      temp->done = 1;
      break;
    } else {
      temp = temp->next;
    }
  }
}


uint64_t *gasnete_coll_scratch_get_tree_peer_pos(gasnete_coll_scratch_req_t *scratch_req GASNETE_THREAD_FARG) {
  uint64_t* ret;
  int i;
  ret = (uint64_t*) gasneti_malloc(sizeof(uint64_t)*scratch_req->num_out_peers);
  for(i=0; i<scratch_req->num_out_peers; i++) {
    ret[i] = gasnete_coll_scratch_tree_get_pos(scratch_req->out_peers[i], scratch_req->out_sizes[i],
					       scratch_req->tree_type, scratch_req->root,
					       scratch_req->team GASNETE_THREAD_PASS);
  }
  return ret;
}

void gasnete_coll_scratch_update_reqh(gasnet_token_t token,
				      
				      gasnet_handlerarg_t teamid,
				      gasnet_handlerarg_t node,
				      gasnet_handlerarg_t tail_high,
				      gasnet_handlerarg_t tail_low) {
  gasnete_coll_team_t team;
  gasnete_coll_scratch_status_t *stat;
  uint64_t tail;
  
  team = gasnete_coll_team_lookup(teamid);
  stat = team->scratch_status;
  /* create a new status and attach it on to the update list*/
  tail = GASNETI_MAKEWORD(tail_high, tail_low);
  /* for now signal the new val as 1*/
  gasnett_atomic_set(&(stat->node_status[node].new_val),1,0);
}

/**** Dissem Ops *****/
/*for now with dissem ops we'll take the simple solution*/
/*it is assumed that a team will collectivly ask for the same amount of scratch space*/
/*Since all the nodes will have a consistent picture of the world ... if any one node can not 
  allocate all the nodes will wait for all the ops to clear, reset the scratch space, and barrier
*/
/* we could imagine a scheme where the blocking collectives clear the previous scratch spaces*/
/* if a dissem op i finishes we can retire all ops <i (but not i)*/

uint64_t gasnete_coll_scratch_new_dissem_op(gasnete_coll_scratch_req_t *scratch_req, uint32_t seq, gasnet_coll_handle_t op_handle GASNETE_THREAD_FARG) {
  gasnet_node_t i;
  int need_to_reset = 0;
  gasnete_coll_op_info_t* new_op;
  gasnete_coll_scratch_status_t *stat= scratch_req->team->scratch_status;
  /*if the incoming size is greater than the total allocated scratch space signal an error*/
  if(scratch_req->incoming_size > scratch_req->team->scratch_segs[scratch_req->team->myrank].size) {
    gasneti_fatalerror("%d> collective requires temporary storage (%d bytes) is greater than total scratch space (%d bytes) \n consider using pipelined algorithms or increasing size of collective scratch space\n", scratch_req->team->myrank, (int)scratch_req->incoming_size, (int)scratch_req->team->scratch_segs[0].size); 
  }
  /*check whether or not we are switching between a tree op to a dissem op*/
  if(stat->last_op == GASNETE_COLL_SCRATCH_TREE_OP) {
    need_to_reset = 1;
  }
  /*walk through all the nodes asking if we can allocate the requested size (which will be stored in incoming size)*/
  for(i=0; i<scratch_req->team->total_ranks; i++)  {
    if(stat->node_status[i].head+scratch_req->incoming_size > scratch_req->team->scratch_segs[i].size) {
      need_to_reset = 1;
      break;
    } 
  }
  if(need_to_reset == 1) {
    /* fprintf(stderr, "%d> NEED TO RESET\n", gasneti_mynode); */
    /*perform a barrier which will trip the reset flag of the scratch status*/
    gasnete_coll_consensus_wait();
/*    gasnet_barrier_notify(0, GASNET_BARRIERFLAG_ANONYMOUS);
    gasnet_barrier_wait(0, GASNET_BARRIERFLAG_ANONYMOUS);*/
    /* fprintf(stderr, "%d> everyone resets\n", gasneti_mynode); */
  }
	
  if(stat->perform_reset == 1) {
    /*		fprintf(stderr, "%d> need to perform reset for alltoall\n", gasneti_mynode); */
    gasnete_coll_reset_scratch_status(stat GASNETE_THREAD_PASS);
    /*		fprintf(stderr, "%d> alltoall reset done\n", gasneti_mynode); */
  }
  /*by this time either the reset has been performed so the head is back down to 0*/
  /*or all the node statuses have been verfied so that the request can be allocated*/
  stat->last_op = GASNETE_COLL_SCRATCH_DISSEM_OP;

  /*create a new op and link it in*/
  new_op = (gasnete_coll_op_info_t*) gasneti_malloc(sizeof(gasnete_coll_op_info_t));
  new_op->next = NULL;
  new_op->prev = NULL;
  new_op->tree_op = 0;

  new_op->local_scratch_used = scratch_req->incoming_size;
  new_op->op_handle = op_handle;
  new_op->seq_number = seq;
  new_op->done = 0;
#if 1
  /*link the new op in on the tail*/
  if(stat->active_scratch_op_head == NULL) {
    stat->active_scratch_op_head = stat->active_scratch_op_tail = new_op;
    new_op->next = NULL;
    new_op->prev = NULL;
  }  else {
    new_op->next = NULL;
    new_op->prev = stat->active_scratch_op_tail;
    stat->active_scratch_op_tail->next = new_op;
    stat->active_scratch_op_tail = new_op;
		
  }
#endif

  /*for the dissem operations i assume that the get_dissem_peer_pos will update the position of my head too*/
  /*so i won't update it here*/
  return stat->node_status[scratch_req->team->myrank].head;
}

uint64_t *gasnete_coll_scratch_get_dissem_peer_pos(gasnete_coll_scratch_req_t *scratch_req GASNETE_THREAD_FARG) {
  /*i will assume that this function is called AFTER new op. If it is called before for the same request then the results are undefined*/
  /* we will just allocate an extra array and then update everyones position*/
  /* no need to check whether the incoming size is ok or send messages since the call to new_op will take care of that*/
  uint64_t* ret;
  int i;
  gasnete_coll_scratch_status_t *stat= scratch_req->team->scratch_status;
  ret = (uint64_t*) gasneti_malloc(sizeof(uint64_t)*scratch_req->team->total_ranks);
  for(i=0; i<scratch_req->team->total_ranks; i++) {
    ret[i] = stat->node_status[i].head;
    stat->node_status[i].head += scratch_req->incoming_size;
  }
  return ret;
}

uint64_t gasnete_coll_scratch_new_op(gasnete_coll_scratch_req_t *scratch_req, uint32_t seq, gasnet_coll_handle_t op_handle GASNETE_THREAD_FARG) {
  if(scratch_req->tree_op) {
    /*	fprintf(stderr, "%d,%d> creating new tree op with %d bytes\n", gasneti_mynode,seq, scratch_req->incoming_size); */
    return gasnete_coll_scratch_new_tree_op(scratch_req, seq, op_handle GASNETE_THREAD_PASS);
  } else {
    /*	fprintf(stderr, "%d,%d> creating new dissem op with %d bytes\n", gasneti_mynode, seq, scratch_req->incoming_size); */
    return gasnete_coll_scratch_new_dissem_op(scratch_req, seq, op_handle GASNETE_THREAD_PASS);
  }
  return 0;
}

uint64_t *gasnete_coll_scratch_get_peer_pos(gasnete_coll_scratch_req_t *scratch_req GASNETE_THREAD_FARG) {
  if(scratch_req->tree_op) {
    return gasnete_coll_scratch_get_tree_peer_pos(scratch_req GASNETE_THREAD_PASS);
  } else {
    return gasnete_coll_scratch_get_dissem_peer_pos(scratch_req GASNETE_THREAD_PASS);
  }
  return NULL;
}




  

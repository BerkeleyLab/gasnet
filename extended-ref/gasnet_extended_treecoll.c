#define GASNETE_COLL_OP_DEFAULT_PIPESEG_SIZE gasnet_AMMaxMedium() 
#define LOGMAXNODES 64
uint32_t gasnete_coll_pipe_seg_size=1024;

void gasnet_coll_set_pipe_seg_size(int pipesz) {
  gasnete_coll_pipe_seg_size = pipesz;
}

#define ACT2REL(actrank, root) ( (actrank >= root) ? actrank - root : actrank - root + gasnete_nodes )
#define REL2ACT(relrank, root) (((relrank < (gasnete_nodes-root)) ? relrank + root : relrank + root - gasnete_nodes))

/* //#define LOGMAXNODES 64 */
#define START(lev) ((1 << (lev))-1)
#define procs_per_node 2

typedef enum {
	GASNETE_COLL_TREE_KIND_CHAIN,		/* 0->1->2->... */
	GASNETE_COLL_TREE_KIND_BINARY,	
	GASNETE_COLL_TREE_KIND_BINOMIAL,
	GASNETE_COLL_TREE_KIND_SEQUENTIAL, 	/* 0->all */
	    
	GASNETE_COLL_TREE_KIND_CHAIN_SMP,
	GASNETE_COLL_TREE_KIND_BINARY_SMP,
	GASNETE_COLL_TREE_KIND_BINOMIAL_SMP,
	GASNETE_COLL_TREE_KIND_SEQUENTIAL_SMP
} gasnete_coll_tree_kind_t;

uint32_t gasnete_coll_curr_tree = GASNETE_COLL_TREE_KIND_BINOMIAL;

void gasnet_coll_set_curr_tree(gasnete_coll_tree_kind_t tree) {
  gasnete_coll_curr_tree = tree;
}

void get_tree(gasnete_coll_tree_kind_t tree, gasnet_node_t rootnode, gasnete_coll_tree_info_t *info) {
  int relrank = ACT2REL(gasnete_mynode, rootnode);
  int num_child=0;
  int i;
  info->parent = (gasnet_node_t)(-1);
  info->child_id = (gasnet_node_t)(-1);
  
  switch(tree) {
  case GASNETE_COLL_TREE_KIND_CHAIN:
    {   /* //chain tree; */
      if(relrank!=(gasnete_nodes-1)) {
	info->child_list = (gasnet_node_t *)gasneti_malloc(sizeof(gasnet_node_t));
	num_child = 1;
	info->child_list[0] = REL2ACT(relrank+1,rootnode);
      } else {
	num_child = 0;
	info->child_list = NULL;
      }
      if(relrank==0) {
	info->parent = -1;
      } else {
	info->parent = REL2ACT(relrank-1,rootnode);
      }
    }
    info->child_id = 0; /*only one child by def*/
    break;
  case GASNETE_COLL_TREE_KIND_CHAIN_SMP:
    if(relrank % procs_per_node == 0) {
      int start;
      if(relrank!=0)
	info->parent = relrank - procs_per_node;
      else
	info->parent = -1;
      if(relrank + procs_per_node < gasnete_nodes) {
	num_child ++;
      }
      for(i=1; i<procs_per_node; i++) {
	if(relrank+i < gasnete_nodes) {
	  num_child++;
	}
      }
      if(num_child > 0) {
	info->child_list = (gasnet_node_t *)gasneti_malloc(sizeof(gasnet_node_t)*num_child);
	if(relrank+procs_per_node < gasnete_nodes) {
	  info->child_list[0] = REL2ACT(relrank+procs_per_node, rootnode);
	  start = 1;
	} else {
	  start = 0;
	}
	for(i=start; i<num_child; i++) {
	  if(start == 0)
	    info->child_list[i] = REL2ACT(relrank+i+1, rootnode);
	  else
	    info->child_list[i] = REL2ACT(relrank+i, rootnode);
	}
	
      }
    } else {
      info->parent = (relrank / procs_per_node)*procs_per_node;
      num_child = 0;
      info->child_list = NULL;
    }
    break;
  case GASNETE_COLL_TREE_KIND_BINARY:
    {
      int level; 
      int tchild0;
      int tchild1;

      level = 0;
      while(1) { /* // has to terminate because of the semantics of the loop  */
	if(relrank >= START(level) &&
	   relrank < START(level+1))
	  break;
	else
	  level++;
      }
      if(relrank!=0) {
/* 	//we expect to recieve from some one */
	int relparent = (relrank-START(level))/2 + START(level-1);
	info->parent = REL2ACT(relparent,rootnode);
        info->child_id = (relrank+1)%2; /*odd nodes are left child even are right*/
      } else {
	/* info->parent = -1;  by default */
	/* info->child_id = -1;  by default */
      }
/*       // so now the level of the current node is set. */
/*       //now we figure out where to expect the message from */
/*       //special case is the root */
/*       //now we need to set the 2 destinations */
      
      tchild0= (relrank - START(level))*2 + START(level+1);
      tchild1= tchild0+1;
      if(tchild0<gasnete_nodes && tchild1<gasnete_nodes) {
	info->child_list = (gasnet_node_t *) gasneti_malloc(sizeof(gasnet_node_t)*2);
	num_child = 2;
	info->child_list[0] = REL2ACT(tchild0,rootnode);
	info->child_list[1] = REL2ACT(tchild1,rootnode);
      } else if(tchild0<gasnete_nodes && tchild1>=gasnete_nodes) {
	info->child_list = (gasnet_node_t *) gasneti_malloc(sizeof(gasnet_node_t));
	num_child = 1;
	info->child_list[0] = REL2ACT(tchild0,rootnode);
      } else if(tchild0>=gasnete_nodes && tchild1<gasnete_nodes) {
	info->child_list = (gasnet_node_t *) gasneti_malloc(sizeof(gasnet_node_t));
	num_child = 1;
	info->child_list[0] = REL2ACT(tchild1,rootnode);
      } else {
	info->child_list = NULL;
	num_child = 0;
      }
    }
    break;
  case GASNETE_COLL_TREE_KIND_BINARY_SMP:
#if 0
     if(relrank%procs_per_node==0) {
       int level; 
       int tchild0;
       int tchild1;
       int smprelrank = relrank/procs_per_node;
       level = 0;
       while(1) { // has to terminate because of the semantics of the loop
 	if(smprelrank >= START(level) &&
 	   smprelrank < START(level+1))
 	  break;
 	else
 	  level++;
       }
       if(relrank!=0) {
 	//we expect to recieve from some one
 	info->parent = ((smprelrank-START(level))/2 + START(level-1))*procs_per_node;
 	info->parent = REL2ACT(info->parent,rootnode);
       } else {
 	info->parent = -1;
       }
       // so now the level of the current node is set.
       //now we figure out where to expect the message from
       //special case is the root
       //now we need to set the 2 destinations
    
       tchild0= (smprelrank - START(level))*2 + START(level+1);
       tchild1= tchild0+1;
       tchild0 *= procs_per_node;
       tchild1 *= procs_per_node;
       if(tchild0<gasnete_nodes && tchild1<gasnete_nodes) {
 	num_child = 2;
 	for(i=1; i<procs_per_node; i++) {
 	  if(relrank + i < gasnete_nodes) {
 	    num_child++;
 	  }
 	}
 	info->child_list = (gasnet_node_t *) gasneti_malloc(sizeof(gasnet_node_t)*num_child);

 	info->child_list[0] = REL2ACT(tchild0,rootnode);
 	info->child_list[1] = REL2ACT(tchild1,rootnode);
	
 	for(i=2; i<num_child; i++) {
 	  info->child_list[i] = REL2ACT(relrank+i, rootnode);
 	}
       } else if(tchild0<gasnete_nodes && tchild1>=gasnete_nodes) {
 	num_child = 1;
 	for(i=1; i<procs_per_node; i++) {
 	  if(relrank + i < gasnete_nodes) {
 	    num_child++;
 	  }
 	}
 	info->child_list = (gasnet_node_t *) gasneti_malloc(sizeof(gasnet_node_t)*num_child);

 	info->child_list[0] = REL2ACT(tchild0,rootnode);

	
 	for(i=1; i<num_child; i++) {
 	  info->child_list[i] = REL2ACT(relrank+i, rootnode);
 	}

       } else if(tchild0>=gasnete_nodes && tchild1<gasnete_nodes) {
 	num_child = 1;
 	for(i=1; i<procs_per_node; i++) {
 	  if(relrank + i < gasnete_nodes) {
 	    num_child++;
 	  }
 	}
 	info->child_list = (gasnet_node_t *) gasneti_malloc(sizeof(gasnet_node_t)*num_child);

 	info->child_list[0] = REL2ACT(tchild1,rootnode);


 	for(i=1; i<num_child; i++) {
 	  info->child_list[i] = REL2ACT(relrank+i, rootnode);
 	}

       } else {
 	num_child = 0;
 	for(i=1; i<procs_per_node; i++) {
 	  if(relrank + i < gasnete_nodes) {
 	    num_child++;
 	  }
 	}
 	info->child_list = (gasnet_node_t *) gasneti_malloc(sizeof(gasnet_node_t)*num_child);

 	info->child_list[0] = REL2ACT(tchild1,rootnode);


 	for(i=0; i<num_child; i++) {
 	  info->child_list[i] = REL2ACT(relrank+i, rootnode);
 	}

       }
     } else {
       info->parent = (relrank / procs_per_node)*procs_per_node;
       num_child = 0;
       info->child_list = NULL;
     }
      info->child_id = /*???*/;
#endif
    break;
  case GASNETE_COLL_TREE_KIND_BINOMIAL:
    { 
      int child, src;
      int temp_dest_list[LOGMAXNODES];
      int mask = 1;
      num_child=0;
      
      mask = 0x1;
      while (mask < gasnete_nodes) {
	if (relrank & mask) {
	  src = gasnete_mynode - mask;
	  if (src < 0) src += gasnete_nodes;
	  info->parent = src;
	  break;
	}
	mask <<= 1;
      }
      
      mask >>= 1;
      while (mask > 0) {
	if (relrank + mask < gasnete_nodes) {
	  child = gasnete_mynode + mask;
	  if (child >= gasnete_nodes) child -= gasnete_nodes;	  
	  temp_dest_list[num_child]=child;
	  (num_child)++;
	
	}
	mask >>= 1;
      }
      if(num_child>0) {
        info->child_list = (gasnet_node_t *)gasneti_malloc(sizeof(gasnet_node_t )*num_child);
        for(child = 0; child<(num_child); child++) {
	  info->child_list[child] = temp_dest_list[child];
        }
      } else {
	info->child_list = NULL;
      }

      if (relrank != 0) {
	int id, i, j;
        i = relrank - ACT2REL(src, rootnode);
	/* compute floor(log_base_2(i)): */
	for (j=1, id=0; (i-j) >= j; ++id, j = j<<1) {/*nothing*/}
	info->child_id = id;
      } else {
	/* info->child_id = -1; by default */
      }
    }
    break;
  case GASNETE_COLL_TREE_KIND_SEQUENTIAL:
    {
      int i=0;
      if(gasnete_mynode ==  rootnode) {
	if(gasnete_nodes > 1)
	  info->child_list = (gasnet_node_t *)gasneti_malloc(sizeof(gasnet_node_t)*(gasnete_nodes-1));
	info->parent = -1;
	for(i=0; i<gasnete_nodes-1; i++) {
	  info->child_list[i] = REL2ACT(i+1,rootnode);
	}
	num_child = gasnete_nodes-1;
      } else {
	info->parent = rootnode;
	info->child_list = NULL;
	num_child = 0;
      }
      info->child_id = relrank-1;
    }
    break;
  default:
    fprintf(stderr, "unknown tree type\n");
    exit(1);
    
  }

  info->child_count = num_child;
}

void print_tree(gasnete_coll_generic_data_t *data, int tree) {
  int i;
  //  if(node<0 || node==gasnete_mynode) {
  fprintf(stderr, "%d:tree: %d parent %d\n", gasnete_mynode, tree, data->p2p->tree.parent); 
  for(i=0; i<data->p2p->tree.child_count; i++) {
    fprintf(stderr, "%d 's child%d: %d ", gasnete_mynode, i, data->p2p->tree.child_list[i]);
  }
  fprintf(stderr, "\n");
  //}
}


static int gasnete_coll_pf_bcast_sig_put(gasnete_coll_op_t *op GASNETE_THREAD_FARG)
{
  gasnete_coll_generic_data_t *data = op->data;
  const gasnete_coll_broadcast_args_t *args = GASNETE_COLL_GENERIC_ARGS(data, broadcast);
  int result = 0;
  int i;
  int child;
  int num_child;
  

  switch (data->state) {
 
  case 0:	/* Optional IN barrier */
    
    if (!gasnete_coll_generic_insync(data)) { 
      break;
    }
    if(gasnete_coll_pipe_seg_size !=0)
      data->p2p->pipe_seg_size = gasnete_coll_pipe_seg_size;
    
    get_tree(gasnete_coll_curr_tree, args->srcnode, &(data->p2p->tree));
    data->p2p->sent_bytes =0;
    data->state = 1;
    
  case 1:	
    if(args->nbytes == 0) {
      data->state = 2;
    } else if (gasnete_mynode == args->srcnode) {
      
      for(i=0; i<args->nbytes; i+=data->p2p->pipe_seg_size) {
	int msgsize = MIN(data->p2p->pipe_seg_size, 
			  args->nbytes-data->p2p->sent_bytes);
	for(child=0; child<data->p2p->tree.child_count; child++) {	
	 /*  //	  fprintf(stderr, "%d sending to %d\n", gasnete_mynode, data->p2p->tree.child_list[child]); */
	  gasnete_coll_p2p_signalling_put(op, data->p2p->tree.child_list[child], (char*)args->dst+i, (char*)args->src+i, msgsize, 0, i+msgsize);
	  
	}
	GASNETE_FAST_UNALIGNED_MEMCPY(args->dst, args->src, args->nbytes);
	data->p2p->sent_bytes += msgsize;
      }
    
      data->state = 2;
    } else if (data->p2p->state[0]>data->p2p->sent_bytes){
      
      int msgsize = MIN(data->p2p->pipe_seg_size, 
	       	args->nbytes-data->p2p->sent_bytes);
    
      for(child = 0; child<data->p2p->tree.child_count; child++) {
	
	gasnete_coll_p2p_signalling_put(op, data->p2p->tree.child_list[child], (char*)args->dst+data->p2p->sent_bytes, 
					(char*)args->dst+data->p2p->sent_bytes, msgsize, 0, data->p2p->sent_bytes+msgsize);
	
	
      }
      data->p2p->sent_bytes += msgsize;
      
      if(data->p2p->sent_bytes == args->nbytes) 
	data->state = 2;
      else
	break;
    
    } else {
      break;
    }
    
   
    
  case 2:	/* Optional  */
    if (!gasnete_coll_generic_outsync(data)) {
      break;
    }
    
    gasnete_coll_generic_free(data GASNETE_THREAD_PASS);
    result = (GASNETE_COLL_OP_COMPLETE | GASNETE_COLL_OP_INACTIVE);
  }
  
  return result;
}

static int gasnete_coll_pf_bcast_sig_get(gasnete_coll_op_t *op GASNETE_THREAD_FARG)
{
  gasnete_coll_generic_data_t *data = op->data;
  const gasnete_coll_broadcast_args_t *args = GASNETE_COLL_GENERIC_ARGS(data, broadcast);
  int result = 0;
  int i;
  int child;
  int num_child;
  

  switch (data->state) {
 
  case 0:	/* Optional IN barrier */
    if (!gasnete_coll_generic_insync(data)) { 
      break;
    }
    if(gasnete_coll_pipe_seg_size !=0)
      data->p2p->pipe_seg_size = gasnete_coll_pipe_seg_size;
    
    get_tree(gasnete_coll_curr_tree, args->srcnode, &(data->p2p->tree));

    data->p2p->sent_bytes =0;
    data->state = 1;
    
  case 1:	
    if(args->nbytes == 0) {
      data->state = 4;
    } else if (gasnete_mynode == args->srcnode) {
      /* //the root can send the address without since it has the data */

      for(child=0; child<data->p2p->tree.child_count; child++) {
	gasnete_coll_p2p_eager_addr(op, data->p2p->tree.child_list[child], args->src, 0, 1);
      }

      GASNETE_FAST_UNALIGNED_MEMCPY(args->dst, args->src, args->nbytes);
      
    
    
      data->state = 3;
    } else if (GASNETE_COLL_CHECK_OWNER(data) && data->p2p->state[0]){
      /*data is recived perform a get*/
      gasneti_sync_reads();
      data->handle = gasnete_get_nb_bulk(args->dst, data->p2p->tree.parent, 
					 *(void **)data->p2p->data,
					 args->nbytes GASNETE_THREAD_PASS);
          
      data->state = 2;
    } else {
      break;
    }
    
   
  case 2:
     if (!gasnete_coll_generic_syncnb(data GASNETE_THREAD_PASS)) { 
       break; 
     } 
    
    /*assumption that there is at least enough state slots for the number of children*/
    /*each child will keep putting the amount of data it got from the sender*/
    if(gasnete_mynode == args->srcnode) {
      /*transmit amoutn of message we got*/

      gasnete_coll_p2p_change_state(op, data->p2p->tree.parent, data->p2p->tree.child_id+1, args->nbytes);
 
    }
    for(child=0; child<data->p2p->tree.child_count; child++) {
      gasnete_coll_p2p_eager_addr(op, data->p2p->tree.child_list[child], args->dst, 0, 1);
    }
    data->state = 3;

  case 3: /* wait for all child nodes to acknowledge recpt */
    {
      int ack_child=0;
      int flag =1;
      for(i=1; i<=data->p2p->tree.child_count; i++) {
	if(data->p2p->state[i]!=args->nbytes) {
	  flag=0;
	  break;
	}
      }
 
      if(flag==0) {
	break;
      } else {
	data->state=4;
      }
    }
  case 4:	/* Optional  */
   
    if (!gasnete_coll_generic_outsync(data)) {
   
        break;
      
      }
    
    gasnete_coll_generic_free(data GASNETE_THREAD_PASS);
    result = (GASNETE_COLL_OP_COMPLETE | GASNETE_COLL_OP_INACTIVE);
  }
  return result;
}



static int gasnete_coll_pf_bcast_sig_get_pipe(gasnete_coll_op_t *op GASNETE_THREAD_FARG)
{
  gasnete_coll_generic_data_t *data = op->data;
  const gasnete_coll_broadcast_args_t *args = GASNETE_COLL_GENERIC_ARGS(data, broadcast);
  int result = 0;
  int i;
  int child;
  int num_child;
  
  
  switch (data->state) {
 
  case 0:	/* Optional IN barrier */
    if (!gasnete_coll_generic_insync(data)) { 
      break;
    }
    if(gasnete_coll_pipe_seg_size !=0)
      data->p2p->pipe_seg_size = gasnete_coll_pipe_seg_size;
    
    get_tree(gasnete_coll_curr_tree, args->srcnode, &(data->p2p->tree));

    data->p2p->sent_bytes =0;
    data->state = 1;
    
  case 1:	
    if(args->nbytes == 0) {
      data->state = 4;
      break;
    } else if (gasnete_mynode == args->srcnode) {
      for(child=0; child<data->p2p->tree.child_count; child++) {
	gasnete_coll_p2p_eager_addr(op, data->p2p->tree.child_list[child], args->src, 0, args->nbytes);
      }

      GASNETE_FAST_UNALIGNED_MEMCPY(args->dst, args->src, args->nbytes);
      
      data->state = 3;
      break;
    } else if (GASNETE_COLL_CHECK_OWNER(data) && (data->p2p->state[0]>data->p2p->sent_bytes)){
      /*data is recived perform a get*/
      gasneti_sync_reads();


      data->handle =
	gasnete_get_nb_bulk((char*)(args->dst)+data->p2p->sent_bytes, data->p2p->tree.parent, 
					 ((char*)(*(void **)data->p2p->data))+data->p2p->sent_bytes,
					 MIN(args->nbytes-data->p2p->sent_bytes, gasnete_coll_pipe_seg_size)
					 GASNETE_THREAD_PASS);
      
          
      data->state = 2;
    } else {
      break;
    }
    
    
  case 2:
    if (!gasnete_coll_generic_syncnb(data GASNETE_THREAD_PASS)) {
      break;
    }
    if(data->p2p->sent_bytes == 0) { /*first message*/
      for(child=0; child<data->p2p->tree.child_count; child++) {
	gasnete_coll_p2p_eager_addr(op, data->p2p->tree.child_list[child], args->dst, 0, MIN(args->nbytes-data->p2p->sent_bytes, gasnete_coll_pipe_seg_size));
      }
    }

    data->p2p->sent_bytes+=MIN(args->nbytes-data->p2p->sent_bytes, gasnete_coll_pipe_seg_size);
    /*assumption that there is at least enough state slots for the number of children*/
    /*each child will keep putting the amount of data it got from the sender*/
    
    for(child=0; child<data->p2p->tree.child_count; child++) {
      gasnete_coll_p2p_change_state(op, data->p2p->tree.child_list[child], 0, data->p2p->sent_bytes);
      
    }
    
    if(data->p2p->sent_bytes<args->nbytes) {
      data->state = 1;
      break;
    } else {
     
      gasnete_coll_p2p_change_state(op, data->p2p->tree.parent, data->p2p->tree.child_id+1, args->nbytes);
      
  data->state = 3;
    }

  case 3: /* wait for all child nodes to acknowledge recpt */
    {
      int ack_child=0;
      int flag =1;
      for(i=1; i<=data->p2p->tree.child_count; i++) {
	if(data->p2p->state[i]!=args->nbytes) {
	  flag=0;
	  break;
	}
      }
 
      if(flag==0) {
	break;
      } else {
	data->state=4;
      }
    }
  case 4:	/* Optional  */
   
    if (!gasnete_coll_generic_outsync(data)) {
   
        break;
      
      }
    
    gasnete_coll_generic_free(data GASNETE_THREAD_PASS);
    result = (GASNETE_COLL_OP_COMPLETE | GASNETE_COLL_OP_INACTIVE);
  }
  return result;
}


static int gasnete_coll_pf_bcast_eager_generic(gasnete_coll_op_t *op GASNETE_THREAD_FARG)
{
  gasnete_coll_generic_data_t *data = op->data;
  const gasnete_coll_broadcast_args_t *args = GASNETE_COLL_GENERIC_ARGS(data, broadcast);
  int result = 0;
  int child;
  int num_child;
 

  
  switch (data->state) {
 
  case 0:	/* Optional IN barrier */
    if (!gasnete_coll_generic_insync(data)) {
      break;
    }
    get_tree(gasnete_coll_curr_tree, args->srcnode, &(data->p2p->tree));
    data->state = 1;
    
  case 1:	/* Data movement (Recv data) */
    if(args->nbytes==0) {
      data->state=2;
    } else if (gasnete_mynode == args->srcnode) {
      for(child=0;child<data->p2p->tree.child_count; child++){
	
	gasnete_coll_p2p_eager_put(op,data->p2p->tree.child_list[child], args->src, args->nbytes, 0, 1);
      }
      GASNETE_FAST_UNALIGNED_MEMCPY(args->dst, args->src, args->nbytes);
    } else if (data->p2p->state[0]) {
      GASNETE_FAST_UNALIGNED_MEMCPY(args->dst, data->p2p->data, args->nbytes);
      for(child=0;child<data->p2p->tree.child_count;child++) {
	
	gasnete_coll_p2p_eager_put(op, data->p2p->tree.child_list[child], args->dst, args->nbytes, 0, 1);
      }
    } else {
       break;	/* Stalled until data arrives */
    }
    data->state = 2;
    
  case 2:	/* Optional  */
    if (!gasnete_coll_generic_outsync(data)) {
      break;
    }
    
    gasnete_coll_generic_free(data GASNETE_THREAD_PASS);
    result = (GASNETE_COLL_OP_COMPLETE | GASNETE_COLL_OP_INACTIVE);
  }
  
  return result;
}

gasnet_coll_handle_t
gasnete_coll_broadcast_autotune(gasnet_team_handle_t team,
		                void *dst,
				gasnet_node_t srcnode, void *src,
				size_t nbytes, int flags GASNETE_THREAD_FARG) {
  
  const size_t eager_limit = GASNETE_COLL_P2P_EAGER_MIN;
  gasnet_coll_handle_t ret;
  /* //  int options = GASNETE_COLL_GENERIC_OPT_INSYNC |	/\* unconditional barrier at start *\/ */
/*   //	GASNETE_COLL_GENERIC_OPT_OUTSYNC |	/\* unconditional barrier at end *\/ */
/*   //	GASNETE_COLL_GENERIC_OPT_P2P;		/\* unconditional allocation of p2p data *\/ */
  int options = GASNETE_COLL_GENERIC_OPT_P2P;
/* // fprintf(stderr, "HERE %d\n", gasnete_mynode); */
/*   //if(nbytes<=eager_limit) { */
/*     // return gasnete_coll_generic_broadcast_nb(team, dst, srcnode, src, nbytes, flags, */
/*   //				     &gasnete_coll_pf_bcast_eager_generic, options GASNETE_THREAD_PASS); */
/*   //} else { */
ret =     gasnete_coll_generic_broadcast_nb(team, dst, srcnode, src, nbytes, flags,
					     &gasnete_coll_pf_bcast_sig_get_pipe, options GASNETE_THREAD_PASS);

      return ret;
}


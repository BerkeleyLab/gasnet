#define GASNETE_COLL_OP_DEFAULT_PIPESEG_SIZE gasnet_AMMaxMedium() 
#define LOGMAXNODES 64
uint32_t gasnete_coll_pipe_seg_size=1024;
uint32_t gasnete_coll_curr_tree = 2;

void gasnet_coll_set_curr_tree(int tree) {
  gasnete_coll_curr_tree = tree;
}

void gasnet_coll_set_pipe_seg_size(int pipesz) {
  gasnete_coll_pipe_seg_size = pipesz;
}

/* #define GASNETE_COLL_P2P_EXTRA_FIELDS \ */
/* uint32_t pipe_seg_size; \ */
/* uint32_t copied_bytes; \ */
/* uint32_t sent_bytes; \ */
/* int *child_lst; \ */
/* int num_child; \ */
/* int parent; */

/* #define GASNETE_P2P_EXTRA_INIT \ */
/* p2p->pipe_seg_size = 1024; \ */
/* p2p->copied_bytes = 0; \ */
/* p2p->sent_bytes = 0; \ */
/* p2p->num_child = 0; \ */
/* p2p->parent = -1; \ */
/* p2p->child_lst=NULL;  */




/* extern void gasnete_coll_set_pipe_seg_size(uint32_t sz) {  */
/*   gasnete_coll_pipe_seg_size = sz;  */
/* }  */


/* extern */
/* void gasnete_coll_set_curr_tree(uint32_t tree) {  */
/*   gasnete_coll_curr_tree = tree;  */
/* }  */

/* GASNET_INLINE_MODIFIER(_gasnet_coll_set_curr_tree) */
/* void */
/* _gasnet_coll_set_curr_tree(uint32_t tree) { */
/*   gasnete_coll_curr_tree = tree;  */
/* } */
/* #define gasnet_coll_set_curr_tree(tree) _gasnet_coll_set_curr_tree(tree) */

#define ACT2REL(actrank, root) ( (actrank >= root) ? actrank - root : actrank - root + gasnete_nodes )
#define REL2ACT(relrank, root) (((relrank < (gasnete_nodes-root)) ? relrank + root : relrank + root - gasnete_nodes))

/* //#define LOGMAXNODES 64 */
#define START(lev) (pow(2, lev)-1)
#define procs_per_node 2

enum trees { CHAIN, BINARY, BINOMIAL, SEQUENTIAL, 
	     CHAIN_SMP, BINARY_SMP, BINOMIAL_SMP, SEQUENTIAL_SMP };
typedef enum trees gasnete_coll_tree_t;

int get_childid(gasnete_coll_tree_t tree, gasnet_node_t rootnode, gasnet_node_t parent) {
  int relrank = ACT2REL(gasnete_mynode, rootnode);
  int relparent = ACT2REL(parent, rootnode);
  int childid = 0;

  switch(tree) {
  case CHAIN:
    childid = 0; /*only one child by def*/
    break;
  case BINARY:
    childid = (relrank+1)%2; /*odd nodes are left child even are right*/
    break;
  case BINOMIAL:
    childid = (int)(log((float)(relrank - relparent))/log(2));
    break;
  case SEQUENTIAL:
    childid = relrank-1;
    break;
  default:
    childid = 0;
  }
  return childid;
}

int get_tree(gasnete_coll_tree_t tree, gasnet_node_t rootnode, int **child_lst, int *parent) {
  /*tree = 0 chain
           1 binary
	   2 binomials
	   3 sequential
	   4 chain_smp
	   5 binary_smp
	   6 binomial_smp
	   7 seq_smp
  */
  int relrank = ACT2REL(gasnete_mynode, rootnode);
  int num_child=0;
  int i;
  *parent = -1;
  
  switch(tree) {
  case CHAIN:
    {   /* //chain tree; */
      if(relrank!=(gasnete_nodes-1)) {
	*child_lst = (int*)gasneti_malloc(sizeof(int)*1);
	num_child = 1;
	(*child_lst)[0] = REL2ACT(relrank+1,rootnode);
      } else {
	num_child = 0;
	*child_lst = NULL;
      }
      if(relrank==0) {
	*parent = -1;
      } else {
	*parent = REL2ACT(relrank-1,rootnode);
      }
    }
    break;
  case CHAIN_SMP:
    if(relrank % procs_per_node == 0) {
      int start;
      if(relrank!=0)
	*parent = relrank - procs_per_node;
      else
	*parent = -1;
      if(relrank + procs_per_node < gasnete_nodes) {
	num_child ++;
      }
      for(i=1; i<procs_per_node; i++) {
	if(relrank+i < gasnete_nodes) {
	  num_child++;
	}
      }
      if(num_child > 0) {
	*child_lst = (int*)gasneti_malloc(sizeof(int)*num_child);
	if(relrank+procs_per_node < gasnete_nodes) {
	  (*child_lst)[0] = REL2ACT(relrank+procs_per_node, rootnode);
	  start = 1;
	} else {
	  start = 0;
	}
	for(i=start; i<num_child; i++) {
	  if(start == 0)
	    (*child_lst)[i] = REL2ACT(relrank+i+1, rootnode);
	  else
	    (*child_lst)[i] = REL2ACT(relrank+i, rootnode);
	}
	
      }
    } else {
      *parent = (relrank / procs_per_node)*procs_per_node;
      num_child = 0;
      *child_lst = NULL;
    }
    break;
  case BINARY:
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
	*parent = (relrank-START(level))/2 + START(level-1);
	*parent = REL2ACT(*parent,rootnode);
      } else {
	*parent = -1;
      }
/*       // so now the level of the current node is set. */
/*       //now we figure out where to expect the message from */
/*       //special case is the root */
/*       //now we need to set the 2 destinations */
      
      tchild0= (relrank - START(level))*2 + START(level+1);
      tchild1= tchild0+1;
      if(tchild0<gasnete_nodes && tchild1<gasnete_nodes) {
	*child_lst = (int*) gasneti_malloc(sizeof(int)*2);
	num_child = 2;
	(*child_lst)[0] = REL2ACT(tchild0,rootnode);
	(*child_lst)[1] = REL2ACT(tchild1,rootnode);
      } else if(tchild0<gasnete_nodes && tchild1>=gasnete_nodes) {
	*child_lst = (int*) gasneti_malloc(sizeof(int)*1);
	num_child = 1;
	(*child_lst)[0] = REL2ACT(tchild0,rootnode);
      } else if(tchild0>=gasnete_nodes && tchild1<gasnete_nodes) {
	*child_lst = (int*) gasneti_malloc(sizeof(int)*1);
	num_child = 1;
	(*child_lst)[0] = REL2ACT(tchild1,rootnode);
      } else {
	*child_lst = NULL;
	num_child = 0;
      }
    }
    break;
  case BINARY_SMP:
    /* if(relrank%procs_per_node==0) { */
/*       int level;  */
/*       int tchild0; */
/*       int tchild1; */
/*       int smprelrank = relrank/procs_per_node; */
/*       level = 0; */
/*       while(1) { // has to terminate because of the semantics of the loop */
/* 	if(smprelrank >= START(level) && */
/* 	   smprelrank < START(level+1)) */
/* 	  break; */
/* 	else */
/* 	  level++; */
/*       } */
/*       if(relrank!=0) { */
/* 	//we expect to recieve from some one */
/* 	*parent = ((smprelrank-START(level))/2 + START(level-1))*procs_per_node; */
/* 	*parent = REL2ACT(*parent,rootnode); */
/*       } else { */
/* 	*parent = -1; */
/*       } */
/*       // so now the level of the current node is set. */
/*       //now we figure out where to expect the message from */
/*       //special case is the root */
/*       //now we need to set the 2 destinations */
      
/*       tchild0= (smprelrank - START(level))*2 + START(level+1); */
/*       tchild1= tchild0+1; */
/*       tchild0 *= procs_per_node; */
/*       tchild1 *= procs_per_node; */
/*       if(tchild0<gasnete_nodes && tchild1<gasnete_nodes) { */
/* 	num_child = 2; */
/* 	for(i=1; i<procs_per_node; i++) { */
/* 	  if(relrank + i < gasnete_nodes) { */
/* 	    num_child++; */
/* 	  } */
/* 	} */
/* 	*child_lst = (int*) gasneti_malloc(sizeof(int)*num_child); */
	
/* 	(*child_lst)[0] = REL2ACT(tchild0,rootnode); */
/* 	(*child_lst)[1] = REL2ACT(tchild1,rootnode); */
	
/* 	for(i=2; i<num_child; i++) { */
/* 	  (*child_lst)[i] = REL2ACT(relrank+i, rootnode); */
/* 	} */
/*       } else if(tchild0<gasnete_nodes && tchild1>=gasnete_nodes) { */
/* 	num_child = 1; */
/* 	for(i=1; i<procs_per_node; i++) { */
/* 	  if(relrank + i < gasnete_nodes) { */
/* 	    num_child++; */
/* 	  } */
/* 	} */
/* 	*child_lst = (int*) gasneti_malloc(sizeof(int)*num_child); */
	
/* 	(*child_lst)[0] = REL2ACT(tchild0,rootnode); */

	
/* 	for(i=1; i<num_child; i++) { */
/* 	  (*child_lst)[i] = REL2ACT(relrank+i, rootnode); */
/* 	} */

/*       } else if(tchild0>=gasnete_nodes && tchild1<gasnete_nodes) { */
/* 	num_child = 1; */
/* 	for(i=1; i<procs_per_node; i++) { */
/* 	  if(relrank + i < gasnete_nodes) { */
/* 	    num_child++; */
/* 	  } */
/* 	} */
/* 	*child_lst = (int*) gasneti_malloc(sizeof(int)*num_child); */
	
/* 	(*child_lst)[0] = REL2ACT(tchild1,rootnode); */

	
/* 	for(i=1; i<num_child; i++) { */
/* 	  (*child_lst)[i] = REL2ACT(relrank+i, rootnode); */
/* 	} */

/*       } else { */
/* 	num_child = 0; */
/* 	for(i=1; i<procs_per_node; i++) { */
/* 	  if(relrank + i < gasnete_nodes) { */
/* 	    num_child++; */
/* 	  } */
/* 	} */
/* 	*child_lst = (int*) gasneti_malloc(sizeof(int)*num_child); */
	
/* 	(*child_lst)[0] = REL2ACT(tchild1,rootnode); */

	
/* 	for(i=0; i<num_child; i++) { */
/* 	  (*child_lst)[i] = REL2ACT(relrank+i, rootnode); */
/* 	} */

/*       } */
/*     } else { */
/*       *parent = (relrank / procs_per_node)*procs_per_node; */
/*       num_child = 0; */
/*       *child_lst = NULL; */
/*     } */
    break;
  case BINOMIAL:
    { 
      int child, src;
      int temp_dest_list[LOGMAXNODES];
      num_child=0;
      int mask = 1;
      
      mask = 0x1;
      while (mask < gasnete_nodes) {
	if (relrank & mask) {
	  src = gasnete_mynode - mask;
	  if (src < 0) src += gasnete_nodes;
	  *parent = src;
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
      *child_lst = (int*)gasneti_malloc(sizeof(int)*(num_child));
      for(child = 0; child<(num_child); child++) {
	(*child_lst)[child] = temp_dest_list[child];
      }
      } else {
	*child_lst = NULL;
      }
    }
    break;
  case SEQUENTIAL:
    {
      int i=0;
      if(gasnete_mynode ==  rootnode) {
	if(gasnete_nodes > 1)
	  *child_lst = (int*)gasneti_malloc(sizeof(int)*(gasnete_nodes-1));
	*parent = -1;
	for(i=0; i<gasnete_nodes-1; i++) {
	  (*child_lst)[i] = REL2ACT(i+1,rootnode);
	}
	num_child = gasnete_nodes-1;
      } else {
	*parent = rootnode;
	*child_lst = NULL;
	num_child = 0;
      }
    }
    break;
  default:
    fprintf(stderr, "unknown tree type\n");
    exit(1);
    
  }
  return num_child;
}

void print_tree(gasnete_coll_generic_data_t *data, int tree) {
  int i;
  //  if(node<0 || node==gasnete_mynode) {
  fprintf(stderr, "%d:tree: %d parent %d\n", gasnete_mynode, tree, data->p2p->parent,gasnete_mynode); 
  for(i=0; i<data->p2p->num_child; i++) {
    fprintf(stderr, "%d 's child%d: %d ", gasnete_mynode, i, data->p2p->child_lst[i]);
  }
  fprintf(stderr, "\n");
  //}
}


static int gasnete_coll_pf_bcast_sig_put(gasnete_coll_op_t *op GASNETE_THREAD_FARG)
{
  gasnete_coll_generic_data_t *data = op->data;
  const gasnete_coll_broadcast_args_t *args = GASNETE_COLL_GENERIC_ARGS(data, broadcast);
  int result = 0;
  int relrank = ACT2REL(gasnete_mynode, args->srcnode);
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
    
    data->p2p->num_child =  get_tree(gasnete_coll_curr_tree, args->srcnode, 
				     &(data->p2p->child_lst), &(data->p2p->parent));
    /* // print_tree(data,-1); */
    data->p2p->sent_bytes =0;
    data->state = 1;
    
  case 1:	
    if(args->nbytes == 0) {
      data->state = 2;
    } else if (relrank == 0) {
      
      for(i=0; i<args->nbytes; i+=data->p2p->pipe_seg_size) {
	int msgsize = MIN(data->p2p->pipe_seg_size, 
			  args->nbytes-data->p2p->sent_bytes);
	for(child=0; child<data->p2p->num_child; child++) {	
	 /*  //	  fprintf(stderr, "%d sending to %d\n", gasnete_mynode, data->p2p->child_lst[child]); */
	  gasnete_coll_p2p_signalling_put(op, data->p2p->child_lst[child], (char*)args->dst+i, (char*)args->src+i, msgsize, 0, i+msgsize);
	  
	}
	GASNETE_FAST_UNALIGNED_MEMCPY(args->dst, args->src, args->nbytes);
	data->p2p->sent_bytes += msgsize;
      }
    
      data->state = 2;
    } else if (data->p2p->state[0]>data->p2p->sent_bytes){
      
      int msgsize = MIN(data->p2p->pipe_seg_size, 
	       	args->nbytes-data->p2p->sent_bytes);
    
      for(child = 0; child<data->p2p->num_child; child++) {
	
	gasnete_coll_p2p_signalling_put(op, data->p2p->child_lst[child], (char*)args->dst+data->p2p->sent_bytes, 
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

/*mainly for the implementation of in_mysync out_mysnc*/
/*need to make sure destinations are in collective before we start sending*/
/*rendezvous done as follows: source node sends address to its children*/
/*child nodes pick up the address from eager buffer and then do a get to get the data and then post a done signal to the parent*/

void gasnete_coll_p2p_state_change(gasnete_coll_op_t *op, gasnet_node_t dstnode,
				     uint32_t offset, uint32_t state) {
      uint32_t team_id = gasnete_coll_team_id(op->team);
      size_t limit;
    
      
      GASNETE_SAFE(
	SHORT_REQ(6,6,(dstnode, gasneti_handleridx(gasnete_coll_p2p_eager_state_reqh),
			team_id, op->sequence, 1, 0, offset, state)));
}



static int gasnete_coll_pf_bcast_sig_get(gasnete_coll_op_t *op GASNETE_THREAD_FARG)
{
  gasnete_coll_generic_data_t *data = op->data;
  const gasnete_coll_broadcast_args_t *args = GASNETE_COLL_GENERIC_ARGS(data, broadcast);
  int result = 0;
  int relrank = ACT2REL(gasnete_mynode, args->srcnode);
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
    
    data->p2p->num_child =  get_tree(gasnete_coll_curr_tree, args->srcnode, 
				     &(data->p2p->child_lst), &(data->p2p->parent));

    data->p2p->sent_bytes =0;
    data->state = 1;
    
  case 1:	
    if(args->nbytes == 0) {
      data->state = 4;
    } else if (relrank == 0) {
      /* //the root can send the address without since it has the data */

      for(child=0; child<data->p2p->num_child; child++) {
	gasnete_coll_p2p_eager_addr(op, data->p2p->child_lst[child], args->src, 0, 1);
      }

      GASNETE_FAST_UNALIGNED_MEMCPY(args->dst, args->src, args->nbytes);
      
    
    
      data->state = 3;
    } else if (GASNETE_COLL_CHECK_OWNER(data) && data->p2p->state[0]){
      /*data is recived perform a get*/
      gasneti_sync_reads();
      data->handle = gasnete_get_nb_bulk(args->dst, data->p2p->parent, 
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
    if(relrank!=0) {
      /*transmit amoutn of message we got*/

      gasnete_coll_p2p_state_change(op, data->p2p->parent, get_childid(gasnete_coll_curr_tree, args->srcnode ,data->p2p->parent)+1, args->nbytes);
 
    }
    for(child=0; child<data->p2p->num_child; child++) {
      gasnete_coll_p2p_eager_addr(op, data->p2p->child_lst[child], args->dst, 0, 1);
    }
    data->state = 3;

  case 3: /* wait for all child nodes to acknowledge recpt */
    {
      int ack_child=0;
      int flag =1;
      for(i=1; i<=data->p2p->num_child; i++) {
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
  int relrank = ACT2REL(gasnete_mynode, args->srcnode);
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
    
    data->p2p->num_child =  get_tree(gasnete_coll_curr_tree, args->srcnode, 
				     &(data->p2p->child_lst), &(data->p2p->parent));

    data->p2p->sent_bytes =0;
    data->state = 1;
    
  case 1:	
    if(args->nbytes == 0) {
      data->state = 4;
      break;
    } else if (relrank == 0) {
      
      
      
      for(child=0; child<data->p2p->num_child; child++) {
	gasnete_coll_p2p_eager_addr(op, data->p2p->child_lst[child], args->src, 0, args->nbytes);
      }

      GASNETE_FAST_UNALIGNED_MEMCPY(args->dst, args->src, args->nbytes);
      
      data->state = 3;
      break;
    } else if (GASNETE_COLL_CHECK_OWNER(data) && (data->p2p->state[0]>data->p2p->sent_bytes)){
      /*data is recived perform a get*/
      gasneti_sync_reads();


      data->handle =
	gasnete_get_nb_bulk((char*)(args->dst)+data->p2p->sent_bytes, data->p2p->parent, 
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
      for(child=0; child<data->p2p->num_child; child++) {
	gasnete_coll_p2p_eager_addr(op, data->p2p->child_lst[child], args->dst, 0, MIN(args->nbytes-data->p2p->sent_bytes, gasnete_coll_pipe_seg_size));
      }
    }

    data->p2p->sent_bytes+=MIN(args->nbytes-data->p2p->sent_bytes, gasnete_coll_pipe_seg_size);
    /*assumption that there is at least enough state slots for the number of children*/
    /*each child will keep putting the amount of data it got from the sender*/
    
    for(child=0; child<data->p2p->num_child; child++) {
      gasnete_coll_p2p_state_change(op, data->p2p->child_lst[child], 0, data->p2p->sent_bytes);
      
    }
    
    if(data->p2p->sent_bytes<args->nbytes) {
      data->state = 1;
      break;
    } else {
     
      gasnete_coll_p2p_state_change(op, data->p2p->parent, get_childid(gasnete_coll_curr_tree, args->srcnode ,data->p2p->parent)+1, args->nbytes);
      
  data->state = 3;
    }

  case 3: /* wait for all child nodes to acknowledge recpt */
    {
      int ack_child=0;
      int flag =1;
      for(i=1; i<=data->p2p->num_child; i++) {
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
  int relrank = ACT2REL(gasnete_mynode, args->srcnode);
  int dest1 = relrank+1;
  int child;
  int num_child;
 

  
  switch (data->state) {
 
  case 0:	/* Optional IN barrier */
    if (!gasnete_coll_generic_insync(data)) {
      break;
    }
    data->p2p->num_child = get_tree(gasnete_coll_curr_tree, args->srcnode,
	       &(data->p2p->child_lst), &(data->p2p->parent));
    data->state = 1;
    
  case 1:	/* Data movement (Recv data) */
    if(args->nbytes==0) {
      data->state=2;
    } else if (relrank == 0) {
      for(child=0;child<data->p2p->num_child; child++){
	
	gasnete_coll_p2p_eager_put(op,data->p2p->child_lst[child], args->src, args->nbytes, 0, 1);
      }
      GASNETE_FAST_UNALIGNED_MEMCPY(args->dst, args->src, args->nbytes);
    } else if (data->p2p->state[0]) {
      GASNETE_FAST_UNALIGNED_MEMCPY(args->dst, data->p2p->data, args->nbytes);
      for(child=0;child<data->p2p->num_child;child++) {
	
	gasnete_coll_p2p_eager_put(op, data->p2p->child_lst[child], args->dst, args->nbytes, 0, 1);
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


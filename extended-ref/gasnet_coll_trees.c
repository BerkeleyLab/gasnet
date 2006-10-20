#include "gasnet_coll_trees.h"
/* accessor functions */

/* tree building code*/
int gasnete_coll_build_tree_mypow(int base, int pow) {
  int ret = 1;
  while(pow!=0) {
    ret *=base;
    pow--;
  }
    return ret;
}
int gasnete_coll_build_tree_mylog2(unsigned int num) {
  unsigned int ret=0;
  while (num >= 1) {
    ret++;
    num = num >> 1;
  }
  return MAX(1,ret);
}


void gasnete_coll_print_tree(gasnete_coll_local_tree_geom_t *geom, int gasnete_coll_tree_mythread) {
  int i;
  if(gasnete_coll_tree_mythread ==0) 
  fprintf(stderr, "%d> parent: %d\n", gasnete_coll_tree_mythread, geom->parent);
  for(i=0; i<geom->child_count; i++) {
    fprintf(stderr, "%d> child %d: %d\n", gasnete_coll_tree_mythread, i, geom->child_list[i]);
  }
  fprintf(stderr, "%d> My sibling id: %d\n", gasnete_coll_tree_mythread, geom->sibling_id);
  for(i=0; i<geom->num_siblings; i++) {
    fprintf(stderr, "%d> sibling %d: %d subtree size: %d\n", gasnete_coll_tree_mythread, i, geom->sibling_list[i], geom->sibling_subtree_sizes[i]);
  }
}

void gasnete_coll_set_dissemination_order(gasnete_coll_local_tree_geom_t *geom, int gasnete_coll_tree_mythread, int gasnete_coll_tree_threads) {
  
  int i = gasnete_coll_tree_threads;
  int k;
  int factor;
  int lognp;
  gasnet_node_t *proc_list;
 
  int j;
  
  
  lognp = 0;
  i = gasnete_coll_tree_threads;
  while(i > 1) {
    lognp ++;
    i = i/2;
  }
  
   proc_list = (gasnet_node_t*)gasneti_malloc(sizeof(gasnet_node_t)*lognp);
 
 
  k=0;
  factor = 2;
  for(i=0; i<lognp; i++) {
    j = (gasnete_coll_tree_mythread + (factor/2))%factor;
    j += (gasnete_coll_tree_mythread / factor) * factor;
    proc_list[i] = j;
    factor = factor * 2;
  }

 geom->dissem_order = proc_list;
 geom->dissem_count = lognp;
}


int gasnete_coll_build_tree_START(int level, int fanout) {
  int i;
  int ret=0;
  for(i=0; i<level; i++) {
    ret +=gasnete_coll_build_tree_mypow(fanout,i);
  }
  return ret;
}



gasnete_coll_local_tree_geom_t*  gasnete_coll_build_tree(gasnete_coll_tree_kind_t kind, int fanout, int root, int gasnete_coll_tree_mythread, int gasnete_coll_tree_threads, int threads_per_node) {
  
#define ACT2REL(actrank, root) ( (actrank >= root) ? actrank - root : actrank \
- root + gasnete_coll_tree_threads )
#define REL2ACT(relrank, root) (((relrank < (gasnete_coll_tree_threads-root)) ? relrank \
+ root : relrank + root - gasnete_coll_tree_threads)) 
  
  int relrank = ACT2REL(gasnete_coll_tree_mythread, root);
  gasnete_coll_local_tree_geom_t *geom = NULL;
  int numnodes = gasnete_coll_tree_threads / threads_per_node;
  int mynode = relrank / threads_per_node;
  if(root%threads_per_node!=0 && gasnete_coll_tree_mythread==0) {
    fprintf(stderr, "TREE WARNING: trees are not properly optimized for the case when root%%threads_per_node (i.e. %d %% %d) !=0\n",  root, threads_per_node);
    fprintf(stderr, "TREE WARNING: use threads_per_node = 1 instead\n");
  
  }
  geom = (gasnete_coll_local_tree_geom_t*)gasneti_malloc(sizeof(gasnete_coll_local_tree_geom_t));
  

  geom->parent = -1;
  /*initialize num_sibllings to zero so it can be set externally if need be*/
 
  geom->num_siblings = 0;
  switch(kind) {
  case GASNETE_COLL_NARY_TREE:
    {
      /*if we are the root of a node then we have to build the tree
	relative to others
      */
      if(relrank%threads_per_node == 0) {
	int level;
	gasnet_node_t *tchild;
	int i,j;
	level = 0;
	tchild = (gasnet_node_t*)gasneti_malloc(sizeof(gasnet_node_t)*fanout);
	while(1) { 
	  /* has to terminate because of the semantics of the loop  */
	  if (mynode >= gasnete_coll_build_tree_START(level,fanout) && 
	      mynode < gasnete_coll_build_tree_START(level+1,fanout)) {
	    break;
	  } else {
	    level++;
	  }
	}

	if (relrank!=0) {
	  /* we expect to recieve from some one */
	  int relparent = (mynode-gasnete_coll_build_tree_START(level,fanout))/fanout + 
	    gasnete_coll_build_tree_START(level-1,fanout);
	  geom->parent = REL2ACT(relparent*threads_per_node,root);


	}
		
	/* so now the level of the current node is set. */
	/* now we figure out where to expect the message from */
	/* special case is the 0 */
	/* now we need to set the n destinations */
	tchild[0]= ((mynode - gasnete_coll_build_tree_START(level,fanout))*fanout +
		    gasnete_coll_build_tree_START(level+1,fanout))*threads_per_node;
	for(i=1; i<fanout; i++) {
	  tchild[i] = tchild[i-1]+threads_per_node;
	} 
	geom->child_list = (gasnet_node_t*) gasneti_malloc(sizeof(gasnet_node_t)*(fanout+threads_per_node));
	geom->child_count=0;
	for(i=0; i<fanout; i++) {
	  if(tchild[i]<gasnete_coll_tree_threads) {
	    geom->child_list[geom->child_count]=REL2ACT(tchild[i], root);
	    geom->child_count++;
	  }
	}
	for(j=1 ;j<threads_per_node;  j++) {
	  if(relrank+j<gasnete_coll_tree_threads) {
	    geom->child_list[geom->child_count]=REL2ACT(relrank+j, root);
	    geom->child_count++;
	  }
	}
	  
	if(geom->child_count==0) {
	  gasneti_free(geom->child_list);
	  geom->child_list=NULL;
	} else {
	  geom->child_list = (gasnet_node_t*)gasneti_realloc(geom->child_list, 
					   sizeof(gasnet_node_t)*geom->child_count);
	}
	gasneti_free(tchild);

      }
      else {
      geom->parent = REL2ACT(relrank - (relrank % threads_per_node),root);
      geom->child_list = NULL;
      geom->child_count = 0;
      
      }
    }
    break;
  
    
  case GASNETE_COLL_BINOMIAL_TREE:
    {
      gasnet_node_t child, src;
      gasnet_node_t *temp_dest_list;
      int mask = 1;
      int num_child=0;
	  /*assume that the number of GASNET_NODES will fit into an unsigned 32-bit int as specified in gasnet.h*/
	  /*thus i assume there will be a max of 2^32 = 4,294,967,296 GASNET_NODES*/
      temp_dest_list = (gasnet_node_t*) gasneti_malloc(sizeof(gasnet_node_t)*sizeof(gasnete_coll_tree_threads));
     mask = 0x1;
      while (mask < gasnete_coll_tree_threads) {
        if (relrank & mask) {
          src = (gasnete_coll_tree_mythread >= mask) ? (gasnete_coll_tree_mythread - mask)
	    : (gasnete_coll_tree_mythread + (gasnete_coll_tree_threads - mask));
          geom->parent = src;
          break;
        }
        mask <<= 1;
      }
      
      mask >>= 1;
      while (mask > 0) {
        if (relrank + mask < gasnete_coll_tree_threads) {
          child = relrank + mask;
          if (child >= gasnete_coll_tree_threads) child -= gasnete_coll_tree_threads;
          temp_dest_list[num_child]=REL2ACT(child,root);
          num_child++;
        }
        mask >>= 1;
      }
      if (num_child > 0) {
        geom->child_list = (gasnet_node_t *)gasneti_malloc(sizeof(gasnet_node_t)*num_child);
        for (child = 0; child<(num_child); child++) {
          geom->child_list[child] = temp_dest_list[child];
        }
      } else {
	geom->child_list = NULL;
      }
      
      if (relrank != 0) {
	int id, i, j;
	i = relrank - ACT2REL(src, root);
	/* compute floor(log_base_2(i)): */

	
      } 
      geom->child_count = num_child;
     // geom->fanout = gasnete_coll_tree_threads;
      gasneti_free(temp_dest_list);

	}
    break;
  }
  
  return geom;
  #undef ACT2REL
  #undef REL2ACT
  
}




gasnet_node_t* gasnete_coll_get_sibling_list(gasnete_coll_local_tree_geom_t *geom, int gasnete_coll_tree_mythread, int gasnete_coll_tree_threads, int *num_siblings, int *sibling_id) {
  gasnete_coll_local_tree_geom_t *temp;
  int i;
  gasnet_node_t *ret_list;
  
  if(gasnete_coll_tree_mythread!=geom->root) {
    /*build a temporary tree with our parent as the root*/
    temp = gasnete_coll_build_tree(geom->kind, geom->fanout, geom->root,
		      geom->parent, gasnete_coll_tree_threads, 1);
    
    /*use the resultant tree to deduce the children (which are our siblings)*/
    *num_siblings = temp->child_count;
   
    ret_list = (gasnet_node_t*) gasneti_malloc(sizeof(gasnet_node_t) * (*num_siblings));
    
    /*create deep copy of sibling_list*/
    memcpy(ret_list, temp->child_list, sizeof(gasnet_node_t)*(*num_siblings));
    
    /*by definition there will be a child list since i asked for the parents child list*/
    /*thus since i am a child of my parent this list will be not null*/
    gasneti_free(temp->child_list);
    
    *sibling_id = -1;
    for(i=0; i<(*num_siblings); i++) {
      if(gasnete_coll_tree_mythread == ret_list[i]) {
	*sibling_id = i;
	break;
      }
    }
    if(*sibling_id == -1 && geom->root!=gasnete_coll_tree_mythread) {
      fprintf(stderr, "%d> FATAL TREE ERROR: I am not in my parents child list\n", gasnete_coll_tree_mythread);
      exit(1);
    }
    
    
  } else {
    *sibling_id = -1;
    *num_siblings = 0;
    ret_list = NULL;
  }
  return ret_list;
}



void gasnete_coll_get_sub_tree_helper(gasnete_coll_local_tree_geom_t *geom, int subtreeroot, int gasnete_coll_tree_threads, 
			 gasnet_node_t *list, int *num_added) {
  /*add this node to the list and update the number_added*/
  gasnete_coll_local_tree_geom_t *temp;
  int i;
  
  list[*num_added] = subtreeroot;
  (*num_added)++;
  
  /* for each child recursively run the depth first search */
  temp = gasnete_coll_build_tree(geom->kind, geom->fanout, geom->root, 
		    subtreeroot, gasnete_coll_tree_threads, 1);
  for(i=0; i<temp->child_count; i++) {
    gasnete_coll_get_sub_tree_helper(geom, temp->child_list[i], gasnete_coll_tree_threads,
			list, num_added);
  }
  gasneti_free(temp->child_list);
  gasneti_free(temp);
}

/*run depth first seach to get the list of children*/
gasnet_node_t *gasnete_coll_get_sub_tree(gasnete_coll_local_tree_geom_t *geom, int gasnete_coll_tree_mythread, int gasnete_coll_tree_threads,
		  int *child_count) {
  
  gasnet_node_t *child_list;
  int pos=0;
  int num_added=0;
  
  
  child_list = (gasnet_node_t*) gasneti_malloc(sizeof(gasnet_node_t)*gasnete_coll_tree_threads);
  
  gasnete_coll_get_sub_tree_helper(geom, gasnete_coll_tree_mythread, gasnete_coll_tree_threads, child_list, &num_added);
  child_list = (gasnet_node_t*) gasneti_realloc(child_list, num_added*sizeof(gasnet_node_t));
  *child_count = num_added;
  return child_list;
}


void gasnete_coll_set_sub_tree_info(gasnete_coll_local_tree_geom_t *geom, int gasnete_coll_tree_mythread, int gasnete_coll_tree_threads) {
  int i;
  if(geom->child_count > 0) {
	geom->subtree_sizes = (int*) gasneti_malloc(sizeof(gasnet_node_t)*geom->child_count);
	for(i=0; i<geom->child_count; i++) {
		gasnet_node_t *temp;
 
		temp = gasnete_coll_get_sub_tree(geom, geom->child_list[i], gasnete_coll_tree_threads, 
				geom->subtree_sizes+i);
		gasneti_free(temp);
    
	}
  
	geom->subtree = gasnete_coll_get_sub_tree(geom, gasnete_coll_tree_mythread, gasnete_coll_tree_threads, &(geom->subtree_count));
  } else {
	geom->subtree = NULL;
  }
}


void gasnete_coll_set_sibling_info(gasnete_coll_local_tree_geom_t *geom, int gasnete_coll_tree_mythread, int gasnete_coll_tree_threads) {
  gasnet_node_t *list;
  int i;
  list =gasnete_coll_get_sibling_list(geom, gasnete_coll_tree_mythread, gasnete_coll_tree_threads, &(geom->num_siblings), &(geom->sibling_id));
  geom->sibling_list = list;
  
  geom->sibling_subtree_sizes = (int*) gasneti_malloc(sizeof(int)*geom->num_siblings);
  for(i=0; i<geom->num_siblings; i++) {
    gasnet_node_t *temp;
    temp = gasnete_coll_get_sub_tree(geom, geom->sibling_list[i], gasnete_coll_tree_threads, geom->sibling_subtree_sizes+i);
    if(geom->sibling_subtree_sizes[i] > 0)
      gasneti_free(temp);
  }
}

#if 0
gasnete_coll_tree_geom_t* gasnete_coll_tree_geom_init(gasnete_coll_tree_kind_t kind, int fanout, int root, int threads_per_node){
   gasnete_coll_tree_geom_t* geom;
  fprintf(stderr, "%d> setting up tree geom\n", gasneti_mynode);
   geom = gasnete_coll_build_tree(kind, fanout, root, gasneti_mynode, gasneti_nodes, threads_per_node);
   
  // gasnete_coll_set_dissemination_order(geom, gasneti_mynode, gasneti_nodes);
  // gasnete_coll_set_sub_tree_info(geom, gasneti_mynode, gasneti_nodes);
  // gasnete_coll_set_sibling_info(geom, gasneti_mynode, gasneti_nodes);
	gasnete_coll_print_tree(geom, gasneti_mynode);
   return geom;
}
#endif

/* create a local view of the tree */
/* args: kind: what kind hte tree is
		 fanout: fanout for an nary tree
		 root: the root relative to this team
				 thus if the members of this team are  1 2 4 8 9 and we want a tree rooted at 4 we'd need to pass in 3
		 a team argument
*/
gasnete_coll_local_tree_geom_t *gasnete_coll_tree_geom_create_local(gasnete_coll_tree_kind_t kind, int fanout, int rootrank, gasnete_coll_team_t team)  {
	 gasnete_coll_local_tree_geom_t* geom;
  #if GASNET_COLL_TREE_DEBUG
  fprintf(stderr, "%d> setting up tree geom\n", gasneti_mynode);
#endif  
   geom = gasnete_coll_build_tree(kind, fanout, rootrank, team->myrank, team->total_ranks, 1);
     // gasnete_coll_set_dissemination_order(geom, gasneti_mynode, gasneti_nodes);
  // gasnete_coll_set_sub_tree_info(geom, gasneti_mynode, gasneti_nodes);
  // gasnete_coll_set_sibling_info(geom, gasneti_mynode, gasneti_nodes);
	#if GASNET_COLL_TREE_DEBUG
	gasnete_coll_print_tree(geom, gasneti_mynode);
#endif
   geom->root = rootrank;
   geom->kind = kind;
   geom->fanout = fanout;
   geom->allocated = 1;
	return geom;
}


/*---------------------------------------------------------------------------------*/
/* Operations to access the tree geometry cache */

uint32_t gasnete_coll_pipe_seg_size = 1024;

/*
	Just keep track of the number of refs to an object for debug reasons
	However according to our design we will never free a geometry that is created
	It will be leaked away once the GASNet program finishes.
*/
#if 0
static void gasnete_coll_tree_geom_release(gasnete_coll_tree_geom_t *geom) {
	gasneti_weakatomic_decrement(&(geom->ref_count), 0);
}
#endif

/* the helper function goes through the cache and then either returns the appropriate geometry
   or returns NULL indicating that the tree needs to be appended to the end of the cache 
*/
static gasnete_coll_tree_geom_t *gasnete_coll_tree_geom_fetch_helper(gasnete_coll_tree_kind_t in_kind, int in_fanout, gasnete_coll_tree_geom_t *geom_cache) {
  gasnete_coll_tree_geom_t *curr_geom = geom_cache;
  while(curr_geom != NULL) {
	if(curr_geom->kind == in_kind) {
		if(in_kind == GASNETE_COLL_BINOMIAL_TREE || curr_geom->fanout == in_fanout) 
			return curr_geom;
		else
			curr_geom = curr_geom->next;
	}
	curr_geom = curr_geom->next;
  }
  /*we've reached the end of the list without finding a match*/
  return NULL;

}
/* XXX: should per-team */

/*
	this routine will initially just return a pointer into a localview and create one if needed. 
	it will do the simple thing and not create new views and just keep reusing old views as needed
*/
gasnete_coll_local_tree_geom_t *gasnete_coll_local_tree_geom_fetch(gasnete_coll_tree_kind_t kind, gasnet_node_t root, int fanout, gasnete_coll_team_t team) {
	gasnete_coll_tree_geom_t *geom_cache_head = team->tree_geom_cache_head;
	gasnete_coll_tree_geom_t *geom_cache_tail = team->tree_geom_cache_tail;

	gasnete_coll_tree_geom_t *curr_geom;
	curr_geom = gasnete_coll_tree_geom_fetch_helper(kind, fanout, geom_cache_head);
	if(curr_geom == NULL) {
		int i;
		#if GASNET_COLL_TREE_DEBUG
		fprintf(stderr, "%d> new tree: %d kind %d fanout\n",gasneti_mynode, kind, fanout);
		#endif
		/* allocate new geometry */
		curr_geom = (gasnete_coll_tree_geom_t *) gasneti_malloc(sizeof(gasnete_coll_tree_geom_t));
		curr_geom->local_views = (gasnete_coll_local_tree_geom_t**) 
									gasneti_malloc(sizeof(gasnete_coll_local_tree_geom_t*)*team->total_ranks);
		for(i=0; i<team->total_ranks; i++) {
			curr_geom->local_views[i] = NULL;
		}
		curr_geom->next = NULL;
		curr_geom->kind = kind;
		curr_geom->fanout = fanout;

		/* link it into the cache*/
		if(geom_cache_head == NULL) {
			/*cache is empty*/
			curr_geom->prev = NULL;
			team->tree_geom_cache_head = curr_geom;
			team->tree_geom_cache_tail = curr_geom;
		} else {
			team->tree_geom_cache_tail->next = curr_geom;
			curr_geom->prev = team->tree_geom_cache_tail;
			team->tree_geom_cache_tail = curr_geom;
		}
		curr_geom->local_views[root] = gasnete_coll_tree_geom_create_local(kind, fanout, root, team);
		return curr_geom->local_views[root];
		/* create local view for the root that we request */
	} else {
		/* if it is already allocated for root go ahead and return it ... this should be the fast path*/
		
		if(curr_geom->local_views[root] == NULL) {
		#if GASNET_COLL_TREE_DEBUG
			fprintf(stderr, "%d> tree found: %d kind %d fanout\n", gasneti_mynode, kind, fanout);
			fprintf(stderr, "%d> new root: %d\n", gasneti_mynode, root); 
			#endif
		  curr_geom->local_views[root] = gasnete_coll_tree_geom_create_local(kind, fanout, root, team);
#if 0		  
			  /* create all the local views */
		   int i;
		   for(i=0; i<team->total_ranks; i++) {
				/* some local views might have already been allocated 
				   through other intermediary steps*/
				if(curr_geom->local_views[i] == NULL) {
					curr_geom->local_views[i] = gasnete_coll_tree_geom_create_local(kind, fanout, i, team);
					
				} 
		   }
#endif
		}
		return curr_geom->local_views[root];
	}
	/*shouldn't get here*/
	return NULL;
}

void gasnete_coll_local_tree_geom_release(gasnete_coll_local_tree_geom_t *geom) {
	
	/* for now don't do anything since we will reuse all our geometries*/
	
}




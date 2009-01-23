/* 
* Description: Files for Autotuner
 * Copyright 2007, Rajesh Nishtala <rajeshn@eecs.berkeley.edu> Dan Bonachea <bonachea@cs.berkeley.edu>
 * Terms of use are as specified in license.txt
 */

/* This is intended as a stub for the autotuner routines*/

#include "gasnet_coll_autotune.h"

/*this array is the maximum size of hte log2 array for fanouts*/
#define GASNETE_COLL_AUTOTUNE_RADIX_ARR_LEN 20

struct gasnete_coll_autotune_info_t_ {
  gasnete_coll_tree_type_t bcast_tree_type;
  gasnete_coll_tree_type_t scatter_tree_type;
  gasnete_coll_tree_type_t gather_tree_type;
  
  size_t gather_all_dissem_limit;
  size_t exchange_dissem_limit;
  
  size_t pipe_seg_size;
	
	/*array index i tells you what the tree fanout should be for 2^(i-1) < nbytes <= 2^(i) bytes*/
	int bcast_tree_radix_limits[GASNETE_COLL_AUTOTUNE_RADIX_ARR_LEN];
	
};

GASNETI_ALWAYS_INLINE(gasnete_coll_nextpower2)
size_t gasnete_coll_nextpower2(size_t n)
{
  size_t x;
  if(n==0) return 0;
  x=1;
  while(x < n) x<<=1;
  return x;
}


/* These "set" routines are only intended for testing purposes. Eventually 
   The "get tree" routines will be the primary method of picking trees*/

gasnete_coll_autotune_info_t* gasnete_coll_autotune_init(gasnet_node_t mynode, gasnet_node_t total_nodes, gasnet_image_t my_images, gasnet_image_t total_images, size_t min_scratch_size) {
  /* read all the environment variables and setup the defaults*/
  gasnete_coll_autotune_info_t* ret;
  char *default_tree_type;
  gasnet_node_t default_tree_fanout;
  size_t dissem_limit;
  size_t temp_size;
  size_t dissem_limit_per_thread;
	int i;
  
  ret = gasneti_malloc(sizeof(gasnete_coll_autotune_info_t));
  /* first read the environment variables for tree types*/
  default_tree_type = gasneti_getenv_withdefault("GASNET_COLL_ROOTED_GEOM", GASNETE_COLL_DEFAULT_TREE_TYPE_STR);
  default_tree_fanout = gasneti_getenv_int_withdefault("GASNET_COLL_ROOTED_ARITY", GASNETE_COLL_DEFAULT_TREE_FANOUT, 0);
  
  /* now over-ride the defaults w/ the collective specific tree types in the environment*/
  ret->bcast_tree_type = gasnete_coll_make_tree_type(gasneti_getenv_withdefault("GASNET_COLL_BROADCAST_GEOM", default_tree_type),
                                                     MIN(total_nodes, gasneti_getenv_int_withdefault("GASNET_COLL_BROADCAST_ARITY", default_tree_fanout, 0)));
  ret->scatter_tree_type = gasnete_coll_make_tree_type(gasneti_getenv_withdefault("GASNET_COLL_SCATTER_GEOM", default_tree_type),
                                                     MIN(total_nodes, gasneti_getenv_int_withdefault("GASNET_COLL_SCATTER_ARITY", default_tree_fanout, 0)));
  ret->gather_tree_type = gasnete_coll_make_tree_type(gasneti_getenv_withdefault("GASNET_COLL_GATHER_GEOM", default_tree_type),
                                                     MIN(total_nodes, gasneti_getenv_int_withdefault("GASNET_COLL_GATHER_ARITY", default_tree_fanout, 0)));
  
  dissem_limit_per_thread = gasneti_getenv_int_withdefault("GASNET_COLL_GATHER_ALL_DISSEM_LIMIT_PER_THREAD", GASNETE_COLL_DEFAULT_DISSEM_LIMIT_PER_THREAD, 1);
  temp_size = gasnete_coll_nextpower2(dissem_limit_per_thread*my_images);
  dissem_limit = gasneti_getenv_int_withdefault("GASNET_COLL_GATHER_ALL_DISSEM_LIMIT", temp_size, 1);
  if(temp_size != dissem_limit) {
    if(mynode == 0) {
      fprintf(stderr, "WARNING: Conflicting environment values for GASNET_COLL_GATHER_ALL_DISSEM_LIMIT (%ld) and GASNET_COLL_GATHER_ALL_DISSEM_LIMIT_PER_THREAD (%ld)\n", (long int) dissem_limit, (long int) dissem_limit_per_thread);
      fprintf(stderr, "WARNING: Using: %ld\n", (long int) MIN(dissem_limit, temp_size));
    }
  }
  ret->gather_all_dissem_limit = MIN(dissem_limit, temp_size);
  
  dissem_limit_per_thread = gasneti_getenv_int_withdefault("GASNET_COLL_EXCHANGE_DISSEM_LIMIT_PER_THREAD", GASNETE_COLL_DEFAULT_DISSEM_LIMIT_PER_THREAD, 1);
  temp_size = gasnete_coll_nextpower2(dissem_limit_per_thread*my_images*my_images);
  dissem_limit = gasneti_getenv_int_withdefault("GASNET_COLL_EXCHANGE_DISSEM_LIMIT", temp_size, 1);
  if(temp_size != dissem_limit) {
    if(mynode == 0) {
      fprintf(stderr, "WARNING: Conflicting environment values for GASNET_COLL_EXCHANGE_DISSEM_LIMIT (%ld) and GASNET_COLL_EXCHANGE_DISSEM_LIMIT_PER_THREAD (%ld)\n", (long int) dissem_limit, (long int) temp_size);
      fprintf(stderr, "WARNING: Using: %ld\n", (long int) MIN(dissem_limit, temp_size));
    }
  }
  ret->exchange_dissem_limit = MIN(dissem_limit, temp_size);
  
  if(min_scratch_size < total_images) {
    gasneti_fatalerror("SCRATCH SPACE TOO SMALL Please set it to at least (%ld bytes) through the GASNET_COLL_SCRATCH_SIZE environment variable", (long int) total_images);
  }
  ret->pipe_seg_size = gasneti_getenv_int_withdefault("GASNET_COLL_PIPE_SEG_SIZE", MIN(min_scratch_size, gasnet_AMMaxLongRequest())/total_images, 1);
/*  if(ret->pipe_seg_size == 0) {
      ret->pipe_seg_size = MIN(min_scratch_size, gasnet_AMMaxLongRequest())/total_images;
  } 
  */
  if(ret->pipe_seg_size*total_images > min_scratch_size) {
    if(mynode == 0) {
      fprintf(stderr, "WARNING: Conflicting evnironment values for scratch space allocated (%d bytes) and GASNET_COLL_PIPE_SEG_SIZE (%d bytes)\n", (int) min_scratch_size, (int)ret->pipe_seg_size);
      fprintf(stderr, "WARNING: Using %d bytes for GASNET_COLL_PIPE_SEG_SIZE\n", (int)(min_scratch_size/total_images));
    } 
    ret->pipe_seg_size = min_scratch_size/(total_images);
  } 
  
  if(ret->pipe_seg_size*total_images > gasnet_AMMaxLongRequest()) {
    if(mynode == 0) {
      fprintf(stderr, "WARNING: GASNET_COLL_PIPE_SEG_SIZE (%d bytes) * total images (%d) has to be less than max size for an AMLong for this conduit (%ld)\n", 
              (int)ret->pipe_seg_size, (int)total_images, (long int) gasnet_AMMaxLongRequest());
      fprintf(stderr, "WARNING: Using %ld bytes for GASNET_COLL_PIPE_SEG_SIZE instead\n", (long int) gasnet_AMMaxLongRequest()/total_images);
      ret->pipe_seg_size = gasnet_AMMaxLongRequest()/total_images;
    }
    
  } 
  if(ret->pipe_seg_size == 0) {
    if(mynode == 0) {
      fprintf(stderr, "WARNING: GASNET_COLL_PIPE_SEG_SIZE has been set to 0 bytes\n");
      fprintf(stderr, "WARNING: Disabling Optimized Rooted Collectives\n");
    } 
    
  }
  
	/*initialize the autotune size array to 2 so we always get a binary tree*/
	for(i=0; i<GASNETE_COLL_AUTOTUNE_RADIX_ARR_LEN; i++) {
		ret->bcast_tree_radix_limits[i] = 3;
	}
	
  return ret;
}


/*
	the following two functions to find the fast log2 of an int are adapted from:
	http://graphics.stanford.edu/~seander/bithacks.html#IntegerLogLookup (accessed July 10, 2008)
*/

static uint32_t fast_log2_64bit(uint64_t number) {
	
	static const char LogTable256[] = 
		{
			0, 0, 1, 1, 2, 2, 2, 2, 3, 3, 3, 3, 3, 3, 3, 3,
			4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4,
			5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5,
			5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5,
			6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6,
			6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6,
			6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6,
			6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6,
			7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7,
			7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7,
			7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7,
			7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7,
			7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7,
			7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7,
			7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7,
			7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7
		};
	
	uint64_t v=number; // 32-bit word to find the log of
	uint32_t r;     // r will be lg(v)
	uint64_t t, tt; // temporaries
	
	if ((tt = v>>48)) {
		r = ((t = tt>>8) ? 56 + LogTable256[t] : 48 + LogTable256[tt]); 
	} else if ((tt = v>>32)) {
		r = ((t = tt>>8) ? 40 + LogTable256[t] : 32 + LogTable256[tt]); 
	} else	if ((tt = v >> 16)) {
		r = ((t = tt >> 8) ? 24 + LogTable256[t] : 16 + LogTable256[tt]);
	}
	else {
		r = ((t = v >> 8) ? 8 + LogTable256[t] : LogTable256[v]);
	}
	
	return r;
	
}

static uint32_t fast_log2_32bit(uint32_t number) {
	
	static const char LogTable256[] = 
		{
			0, 0, 1, 1, 2, 2, 2, 2, 3, 3, 3, 3, 3, 3, 3, 3,
			4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4,
			5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5,
			5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5,
			6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6,
			6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6,
			6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6,
			6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6,
			7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7,
			7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7,
			7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7,
			7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7,
			7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7,
			7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7,
			7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7,
			7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7
		};
	
	uint32_t v=number; // 32-bit word to find the log of
	uint32_t r;     // r will be lg(v)
	uint32_t t, tt; // temporaries
	
	
	if ((tt = v >> 16)) {
		r = ((t = tt >> 8) ? 24 + LogTable256[t] : 16 + LogTable256[tt]);
	}
	else {
		r = ((t = v >> 8) ? 8 + LogTable256[t] : LogTable256[v]);
	}
	
	return r;
	
}

#define GASNETE_AUTOTUNE_BARRIER() do { \
 gasnete_barrier_notify(0,GASNET_BARRIERFLAG_ANONYMOUS); \
 gasnete_barrier_wait(0, GASNET_BARRIERFLAG_ANONYMOUS); \
	} while (0)

gasnete_coll_tree_type_t gasnete_coll_autotune_get_bcast_tree_type(gasnete_coll_autotune_info_t* autotune_info, 
                                                             gasnete_coll_autotune_optype_t op_type, 
                                                             gasnet_node_t root, size_t nbytes, int flags) {
	gasnete_coll_tree_type_t ret;
	/*first check if we've seen this size*/
	/*find the log of the transfer size we are interested in*/
	uint32_t log2_nbytes;
#if PLATFORM_ARCH_32
	log2_nbytes = fast_log2_32bit(nbytes);
#else
	log2_nbytes = fast_log2_64bit(nbytes);
#endif
	
	if(autotune_info->bcast_tree_radix_limits[log2_nbytes] == -1) {
		int radix = 0; 
		/*perform search across fanouts*/
		/* do a barrier to ensure all threads have arrived*/
		GASNETE_AUTOTUNE_BARRIER();
		
		
		
		GASNETE_AUTOTUNE_BARRIER();
	} else {
		ret = gasnete_coll_make_tree_type((char*) "DFS_RECURSIVE_TREE", autotune_info->bcast_tree_radix_limits[(log2_nbytes >= GASNETE_COLL_AUTOTUNE_RADIX_ARR_LEN ? GASNETE_COLL_AUTOTUNE_RADIX_ARR_LEN-1 : log2_nbytes)]);
	}
	
	return ret;
}
#define PERFORM_AUTOTUNE_BCAST 1
gasnete_coll_tree_type_t gasnete_coll_autotune_get_tree_type(gasnete_coll_autotune_info_t* autotune_info, 
                                                             gasnete_coll_autotune_optype_t op_type, 
                                                             gasnet_node_t root, size_t nbytes, int flags) {

  switch(op_type) {
#if PERFORM_AUTOTUNE_BCAST
	  case GASNETE_COLL_BROADCAST_OP: return gasnete_coll_autotune_get_bcast_tree_type(autotune_info, op_type, root, nbytes, flags);
#else
	  case GASNETE_COLL_BROADCAST_OP: return autotune_info->bcast_tree_type;  
#endif
	  case GASNETE_COLL_SCATTER_OP: return autotune_info->scatter_tree_type;
  	case GASNETE_COLL_GATHER_OP: return autotune_info->gather_tree_type;
  	default: gasneti_fatalerror("unknown tree based collective op type"); return autotune_info->bcast_tree_type;
  }
}

size_t gasnete_coll_get_dissem_limit(gasnete_coll_autotune_info_t* autotune_info, gasnete_coll_autotune_optype_t op_type, int flags) {
  switch(op_type) {
    case GASNETE_COLL_GATHER_ALL_OP: return autotune_info->gather_all_dissem_limit;
    case GASNETE_COLL_EXCHANGE_OP: return autotune_info->exchange_dissem_limit;
    default:  gasneti_fatalerror("unknown dissem based collective op type"); return 0;
  }
}


size_t gasnete_coll_get_pipe_seg_size(gasnete_coll_autotune_info_t* autotune_info, gasnete_coll_autotune_optype_t op_type, int flags){
  return autotune_info->pipe_seg_size;
}

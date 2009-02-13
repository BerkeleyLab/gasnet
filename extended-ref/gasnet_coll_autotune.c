/* 
 * Description: Files for Autotuner
 * Copyright 2007, Rajesh Nishtala <rajeshn@eecs.berkeley.edu> Dan Bonachea <bonachea@cs.berkeley.edu>
 * Terms of use are as specified in license.txt
 */

/* This is intended as a stub for the autotuner routines*/
#include <gasnet_coll_autotune.h>
#include <gasnet_coll_autotune_internal.h>


/*a small library to write and read XML style sheets for hte collective tuner*/
#include <../other/myxml/myxml.h>
#include <../other/myxml/myxml.c>

/*this array is the maximum size of hte log2 array for fanouts*/
#define GASNETE_COLL_AUTOTUNE_RADIX_ARR_LEN 20

struct gasnete_coll_autotune_info_t_ {
  gasnete_coll_tree_type_t bcast_tree_type;
  gasnete_coll_tree_type_t scatter_tree_type;
  gasnete_coll_tree_type_t gather_tree_type;
  
  size_t gather_all_dissem_limit;
  size_t exchange_dissem_limit;
  int exchange_dissem_radix;
  size_t pipe_seg_size;
	
	/*array index i tells you what the tree fanout should be for 2^(i-1) < nbytes <= 2^(i) bytes*/
	int bcast_tree_radix_limits[GASNETE_COLL_AUTOTUNE_RADIX_ARR_LEN];
  
  gasnete_coll_algorithm_t *collective_algorithms[GASNET_COLL_NUM_COLL_OPTYPES];
  gasnet_team_handle_t team;
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


gasnete_coll_algorithm_t gasnete_coll_autotune_register_algorithm(gasnet_coll_optype_t optype, 
                                                                  uint32_t syncflags,
                                                                  uint32_t requirements,
                                                                  size_t max_size,
                                                                  uint32_t num_params,
                                                                  struct gasnet_coll_tuning_parameter_t *param_list, 
                                                                  void *coll_fnptr) {
  gasnete_coll_algorithm_t ret;
  int i;
  ret.optype = optype;
  ret.syncflags = syncflags;
  ret.requirements = requirements;
  ret.num_parameters = num_params;
  /*create a deep copy of the param list*/
  if(num_params > 0) {
    ret.parameter_list = (struct gasnet_coll_tuning_parameter_t*) gasneti_malloc(sizeof(struct gasnet_coll_tuning_parameter_t)*num_params);
    for(i=0; i<num_params; i++) {
      ret.parameter_list[i].tuning_param = param_list[i].tuning_param;
      ret.parameter_list[i].start = param_list[i].start;
      ret.parameter_list[i].end = param_list[i].end;
      ret.parameter_list[i].stride = param_list[i].stride;
      ret.parameter_list[i].flags = param_list[i].flags;
    }
  } else {
    ret.parameter_list = NULL;
  }
  switch(optype) {
    case GASNET_COLL_BROADCAST_OP: ret.fn_ptr.bcast_fn = (gasnete_coll_bcast_fn_ptr_t) coll_fnptr; break;
    default: gasneti_fatalerror("not implemented yet");
  }
  return ret;
}

#define GASNETE_COLL_EVERY_IN_SYNC_FLAG GASNET_COLL_IN_NOSYNC | GASNET_COLL_IN_MYSYNC | GASNET_COLL_IN_ALLSYNC 
#define GASNETE_COLL_EVERY_OUT_SYNC_FLAG GASNET_COLL_OUT_NOSYNC | GASNET_COLL_OUT_MYSYNC | GASNET_COLL_OUT_ALLSYNC 
#define GASNETE_COLL_EVERY_SYNC_FLAG GASNETE_COLL_EVERY_IN_SYNC_FLAG | GASNETE_COLL_EVERY_OUT_SYNC_FLAG


void gasnete_coll_register_collectives(gasnete_coll_autotune_info_t* info) {
  
  /*first register all the broadcast algorithms*/
  
  info->collective_algorithms[GASNET_COLL_BROADCAST_OP] = gasneti_malloc(sizeof(gasnete_coll_algorithm_t)*GASNETE_COLL_BROADCAST_NUM_ALGS);
  
  
  info->collective_algorithms[GASNET_COLL_BROADCAST_OP][GASNETE_COLL_BROADCAST_PUT] = 
  gasnete_coll_autotune_register_algorithm(GASNET_COLL_BROADCAST_OP, GASNETE_COLL_EVERY_SYNC_FLAG,
                                           GASNET_COLL_DST_IN_SEGMENT | GASNET_COLL_SINGLE,
                                           0,
                                           0,NULL,gasnete_coll_bcast_Put);
  
  
  info->collective_algorithms[GASNET_COLL_BROADCAST_OP][GASNETE_COLL_BROADCAST_GET] = 
  gasnete_coll_autotune_register_algorithm(GASNET_COLL_BROADCAST_OP, GASNETE_COLL_EVERY_SYNC_FLAG,
                                           GASNET_COLL_SRC_IN_SEGMENT | GASNET_COLL_SINGLE, 
                                           0,
                                           0,NULL,gasnete_coll_bcast_Get);
  
  {
    struct gasnet_coll_tuning_parameter_t tuning_params[2] = 
    {{GASNET_COLL_TREE_CLASS, 0, GASNETE_COLL_NUM_TREE_CLASSES, 1, GASNET_COLL_TUNING_STRIDE_ADD}, 
      {GASNET_COLL_TREE_FANOUT, 2, info->team->total_ranks, 2, GASNET_COLL_TUNING_STRIDE_ADD}}; 
    
    
    info->collective_algorithms[GASNET_COLL_BROADCAST_OP][GASNETE_COLL_BROADCAST_TREE_PUT] = 
    gasnete_coll_autotune_register_algorithm(GASNET_COLL_BROADCAST_OP, 
                                             GASNET_COLL_IN_NOSYNC | GASNET_COLL_IN_ALLSYNC | GASNET_COLL_OUT_NOSYNC | GASNET_COLL_OUT_ALLSYNC,
                                             GASNET_COLL_DST_IN_SEGMENT | GASNET_COLL_SINGLE, 
                                             gasnet_AMMaxLongRequest(),
                                             2, tuning_params,gasnete_coll_bcast_TreePut);
  }
  
  {
    struct gasnet_coll_tuning_parameter_t tuning_params[2]=
    {{GASNET_COLL_TREE_CLASS, 0, GASNETE_COLL_NUM_TREE_CLASSES, 1, GASNET_COLL_TUNING_STRIDE_ADD}, 
      {GASNET_COLL_TREE_FANOUT, 2, info->team->total_ranks, 2, GASNET_COLL_TUNING_STRIDE_ADD}}; 
    
    info->collective_algorithms[GASNET_COLL_BROADCAST_OP][GASNETE_COLL_BROADCAST_TREE_PUT_SCRATCH] = 
    gasnete_coll_autotune_register_algorithm(GASNET_COLL_BROADCAST_OP, 
                                             GASNETE_COLL_EVERY_SYNC_FLAG,
                                             GASNET_COLL_DST_IN_SEGMENT, 
                                             gasnet_AMMaxLongRequest(),
                                             2,tuning_params,gasnete_coll_bcast_TreePutScratch);
    
    
  }
  
  {
    struct gasnet_coll_tuning_parameter_t tuning_params[3]=
    { 
      {GASNET_COLL_TREE_CLASS, 0, GASNETE_COLL_NUM_TREE_CLASSES, 1, GASNET_COLL_TUNING_STRIDE_ADD}, 
      {GASNET_COLL_TREE_FANOUT, 2, info->team->total_ranks, 2, GASNET_COLL_TUNING_STRIDE_ADD},
      {GASNET_COLL_PIPE_SEG_SIZE, GASNET_COLL_MIN_PIPE_SEG_SIZE, GASNET_COLL_MAX_PIPE_SEG_SIZE, 2, GASNET_COLL_TUNING_STRIDE_MULTIPLY}
    }; 
    
    info->collective_algorithms[GASNET_COLL_BROADCAST_OP][GASNETE_COLL_BROADCAST_TREE_PUT_SEG] = 
    gasnete_coll_autotune_register_algorithm(GASNET_COLL_BROADCAST_OP, 
                                             GASNETE_COLL_EVERY_SYNC_FLAG,
                                             GASNET_COLL_DST_IN_SEGMENT, 
                                             0,
                                             3,tuning_params,gasnete_coll_bcast_TreePutSeg);
    
    
  }
  
  info->collective_algorithms[GASNET_COLL_BROADCAST_OP][GASNETE_COLL_BROADCAST_EAGER] = 
  gasnete_coll_autotune_register_algorithm(GASNET_COLL_BROADCAST_OP, GASNETE_COLL_EVERY_SYNC_FLAG,
                                           0, /*works for all flags as long as size is small enough*/ 
                                           gasnete_coll_p2p_eager_min,
                                           0,NULL,gasnete_coll_bcast_Eager);
  
  {
    struct gasnet_coll_tuning_parameter_t tuning_params[2]=
    { 
      {GASNET_COLL_TREE_CLASS, 0, GASNETE_COLL_NUM_TREE_CLASSES, 1, GASNET_COLL_TUNING_STRIDE_ADD}, 
      {GASNET_COLL_TREE_FANOUT, 2, info->team->total_ranks, 2, GASNET_COLL_TUNING_STRIDE_ADD}
    }; 
    
    info->collective_algorithms[GASNET_COLL_BROADCAST_OP][GASNETE_COLL_BROADCAST_TREE_EAGER] = 
    gasnete_coll_autotune_register_algorithm(GASNET_COLL_BROADCAST_OP, 
                                             GASNETE_COLL_EVERY_SYNC_FLAG,
                                             0, /*works for all flags as long as size is small enough*/ 
                                             gasnete_coll_p2p_eager_min,
                                             2,tuning_params,gasnete_coll_bcast_TreeEager);
    
    
  }
  
  info->collective_algorithms[GASNET_COLL_BROADCAST_OP][GASNETE_COLL_BROADCAST_RVOUS] = 
  gasnete_coll_autotune_register_algorithm(GASNET_COLL_BROADCAST_OP, GASNETE_COLL_EVERY_SYNC_FLAG,
                                           0, /*works for all flags as long as size is small enough*/ 
                                           0, /*works for all sizes*/
                                           0,NULL,gasnete_coll_bcast_RVous);
  
  info->collective_algorithms[GASNET_COLL_BROADCAST_OP][GASNETE_COLL_BROADCAST_RVGET] = 
  gasnete_coll_autotune_register_algorithm(GASNET_COLL_BROADCAST_OP, GASNETE_COLL_EVERY_SYNC_FLAG,
                                           GASNET_COLL_SRC_IN_SEGMENT, 
                                           0, /*works for all sizes*/
                                           0,NULL,gasnete_coll_bcast_RVGet);
  
  
  {
    struct gasnet_coll_tuning_parameter_t tuning_params[2]=
    { 
      {GASNET_COLL_TREE_CLASS, 0, GASNETE_COLL_NUM_TREE_CLASSES, 1, GASNET_COLL_TUNING_STRIDE_ADD}, 
      {GASNET_COLL_TREE_FANOUT, 2, info->team->total_ranks, 2, GASNET_COLL_TUNING_STRIDE_ADD}
    }; 
    
    info->collective_algorithms[GASNET_COLL_BROADCAST_OP][GASNETE_COLL_BROADCAST_TREE_RVGET] = 
    gasnete_coll_autotune_register_algorithm(GASNET_COLL_BROADCAST_OP, 
                                             GASNETE_COLL_EVERY_SYNC_FLAG,
                                             GASNET_COLL_SRC_IN_SEGMENT | GASNET_COLL_DST_IN_SEGMENT, 
                                             0, /*works for all sizes*/
                                             2,tuning_params,gasnete_coll_bcast_TreeRVGet);
    
    
  }
  
  
  
  //  info->collective_algorithms[GASNET_COLL_BROADCASTM_OP] = gasneti_malloc(sizeof(gasnete_coll_algorithm_t)*GASNETE_COLL_BROADCASTM_NUM_ALGS);
}


/* These "set" routines are only intended for testing purposes. Eventually 
 The "get tree" routines will be the primary method of picking trees*/

gasnete_coll_autotune_info_t* gasnete_coll_autotune_init(gasnet_team_handle_t team, gasnet_node_t mynode, gasnet_node_t total_nodes, gasnet_image_t my_images, gasnet_image_t total_images, size_t min_scratch_size) {
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
  ret->bcast_tree_type = gasnete_coll_make_tree_type_str(gasneti_getenv_withdefault("GASNET_COLL_BROADCAST_GEOM", default_tree_type),
                                                         MIN(total_nodes, gasneti_getenv_int_withdefault("GASNET_COLL_BROADCAST_ARITY", default_tree_fanout, 0)));
  ret->scatter_tree_type = gasnete_coll_make_tree_type_str(gasneti_getenv_withdefault("GASNET_COLL_SCATTER_GEOM", default_tree_type),
                                                           MIN(total_nodes, gasneti_getenv_int_withdefault("GASNET_COLL_SCATTER_ARITY", default_tree_fanout, 0)));
  ret->gather_tree_type = gasnete_coll_make_tree_type_str(gasneti_getenv_withdefault("GASNET_COLL_GATHER_GEOM", default_tree_type),
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
  ret->exchange_dissem_radix = MIN(gasneti_getenv_int_withdefault("GASNET_COLL_EXCHANGE_DISSEM_RADIX", 2, 0),total_images);

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
  
  
  ret->team = team;
  gasnete_coll_register_collectives(ret);
  
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
                                                                   gasnet_coll_optype_t op_type, 
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
    /*for larger arrays just use the maximum setting that we've already found*/
		ret = gasnete_coll_make_tree_type_str((char*) "DFS_RECURSIVE_TREE", autotune_info->bcast_tree_radix_limits[(log2_nbytes >= GASNETE_COLL_AUTOTUNE_RADIX_ARR_LEN ? GASNETE_COLL_AUTOTUNE_RADIX_ARR_LEN-1 : log2_nbytes)]);
	}
	
	return ret;
  
}

gasnete_coll_tree_type_t gasnete_coll_autotune_get_tree_type(gasnete_coll_autotune_info_t* autotune_info, 
                                                             gasnet_coll_optype_t op_type, 
                                                             gasnet_node_t root, size_t nbytes, int flags) {
  
  switch(op_type) {
	  case GASNET_COLL_BROADCAST_OP:
    case GASNET_COLL_BROADCASTM_OP: 
      return autotune_info->bcast_tree_type;  
      
	  case GASNET_COLL_SCATTER_OP: 
    case GASNET_COLL_SCATTERM_OP:  
      return autotune_info->scatter_tree_type;
      
    case GASNET_COLL_GATHER_OP:
  	case GASNET_COLL_GATHERM_OP:
      return autotune_info->gather_tree_type;
      
  	default: gasneti_fatalerror("unknown tree based collective op type"); return autotune_info->bcast_tree_type;
  }
}


size_t gasnete_coll_get_dissem_limit(gasnete_coll_autotune_info_t* autotune_info, gasnet_coll_optype_t op_type, int flags) {
  switch(op_type) {
    case GASNET_COLL_GATHER_ALL_OP:
    case GASNET_COLL_GATHER_ALLM_OP: 
      return autotune_info->gather_all_dissem_limit;
    case GASNET_COLL_EXCHANGE_OP: 
    case GASNET_COLL_EXCHANGEM_OP: 
      return autotune_info->exchange_dissem_limit;
    default:  gasneti_fatalerror("unknown dissem based collective op type"); return 0;
  }
}

int gasnete_coll_get_dissem_radix(gasnete_coll_autotune_info_t* autotune_info, gasnet_coll_optype_t op_type, int flags) {
  switch(op_type) {
  case GASNET_COLL_EXCHANGE_OP: 
    case GASNET_COLL_EXCHANGEM_OP: 
      return autotune_info->exchange_dissem_radix;
  default: gasneti_fatalerror("op doesn't specify dissem radix");   return 0;
  }

}

size_t gasnete_coll_get_pipe_seg_size(gasnete_coll_autotune_info_t* autotune_info, gasnet_coll_optype_t op_type, int flags){
  return autotune_info->pipe_seg_size;
}


int gasnet_coll_get_num_tree_classes(gasnete_coll_team_t team, gasnet_coll_optype_t optype) {
  return (int) GASNETE_COLL_NUM_TREE_CLASSES;
}



void gasnet_coll_set_tree_kind(gasnete_coll_team_t team, int tree_class, int fanout, gasnet_coll_optype_t optype) {
  
  switch(optype) {
    case GASNET_COLL_BROADCAST_OP: 
    case GASNET_COLL_BROADCASTM_OP:    
      team->autotune_info->bcast_tree_type = gasnete_coll_make_tree_type(tree_class, fanout); break;
    case GASNET_COLL_SCATTER_OP:
    case GASNET_COLL_SCATTERM_OP:
      team->autotune_info->scatter_tree_type = gasnete_coll_make_tree_type(tree_class, fanout); break;
    case GASNET_COLL_GATHER_OP:
    case GASNET_COLL_GATHERM_OP:    
      team->autotune_info->gather_tree_type = gasnete_coll_make_tree_type(tree_class, fanout); break;
    default: gasneti_fatalerror("unknown tree based collective op");
  }
  return;
}

void gasnet_coll_set_dissem_limit(gasnete_coll_team_t team, size_t dissemlimit, gasnet_coll_optype_t optype) {
  switch(optype) {
    case GASNET_COLL_GATHER_ALL_OP:
    case GASNET_COLL_GATHER_ALLM_OP:
      team->autotune_info->gather_all_dissem_limit = dissemlimit; break;
    case GASNET_COLL_EXCHANGE_OP:
    case GASNET_COLL_EXCHANGEM_OP:
      team->autotune_info->exchange_dissem_limit = dissemlimit; break;
    default:  gasneti_fatalerror("unknown dissem based collective op type"); break;
  }
  return;
}



uint32_t gasnet_coll_get_algs(gasnet_team_handle_t team, gasnet_coll_optype_t op, size_t nbytes, uint32_t flags, uint32_t** outlist, uint32_t* num_algs_ret) {
  int num_algs;
  switch(op) {
    case GASNET_COLL_BROADCAST_OP:  num_algs =  GASNETE_COLL_BROADCAST_NUM_ALGS; break;
    case GASNET_COLL_BROADCASTM_OP: num_algs =  GASNETE_COLL_BROADCASTM_NUM_ALGS; break;
    case GASNET_COLL_SCATTER_OP: num_algs =  GASNETE_COLL_SCATTER_NUM_ALGS; break;
    case GASNET_COLL_SCATTERM_OP: num_algs =  GASNETE_COLL_SCATTERM_NUM_ALGS; break;
    case GASNET_COLL_GATHER_OP: num_algs =  GASNETE_COLL_GATHER_NUM_ALGS;break;
    case GASNET_COLL_GATHERM_OP: num_algs =  GASNETE_COLL_GATHERM_NUM_ALGS; break;
    case GASNET_COLL_GATHER_ALL_OP: num_algs =  GASNETE_COLL_GATHER_ALL_NUM_ALGS;break;
    case GASNET_COLL_GATHER_ALLM_OP: num_algs =  GASNETE_COLL_GATHER_ALLM_NUM_ALGS; break;
    case GASNET_COLL_EXCHANGE_OP: num_algs =  GASNETE_COLL_EXCHANGE_NUM_ALGS;break;
    case GASNET_COLL_EXCHANGEM_OP: num_algs =  GASNETE_COLL_EXCHANGEM_NUM_ALGS;    break;   
    default: gasneti_fatalerror("unknown optype"); break;
  }
  if(num_algs > 0) {
    int i;
    
    uint32_t sync_flags = (flags &  GASNET_COLL_SYNC_FLAG_MASK); /*strip the sync flags off the flags*/
    uint32_t req_flags = (flags & (~GASNET_COLL_SYNC_FLAG_MASK));
    *outlist = (uint32_t*) gasneti_malloc(sizeof(int)*num_algs);
    *num_algs_ret = 0;
    for(i=0; i<num_algs; i++) {
      int size_ok = (team->autotune_info->collective_algorithms[op][i].max_num_bytes==0 || nbytes <= team->autotune_info->collective_algorithms[op][i].max_num_bytes);
      /*ensure that all the flags required by the algorithm are passed in through the flags*/
      int req_flags_ok = ((req_flags & team->autotune_info->collective_algorithms[op][i].requirements) == req_flags);
      
      /*ensure that the synchronization flags exist in the list of possible synch flags for this algorithm*/
      int sync_flags_ok = ((sync_flags | team->autotune_info->collective_algorithms[op][i].syncflags) > 0);
      
      if(size_ok && req_flags_ok && sync_flags_ok/*match!*/) {
        (*outlist)[*num_algs_ret] = i;
        (*num_algs_ret)++;
      }
    }
    *outlist = (uint32_t*) gasneti_realloc(*outlist, sizeof(uint32_t)*(*num_algs_ret)); 
    return *num_algs_ret;
  } else {
    *outlist = NULL;
    return 0;
  }
}


int gasnet_coll_get_num_params(gasnet_team_handle_t team, gasnet_coll_optype_t op, uint32_t algorithm_num) {
  return team->autotune_info->collective_algorithms[op][algorithm_num].num_parameters;
}
struct gasnet_coll_tuning_parameter_t gasnet_coll_get_param(gasnet_team_handle_t team, gasnet_coll_optype_t op, uint32_t algorithm_num, uint32_t param_idx){
  gasneti_assert(param_idx < team->autotune_info->collective_algorithms[op][algorithm_num].num_parameters);
  return team->autotune_info->collective_algorithms[op][algorithm_num].parameter_list[param_idx];
}

static gasneti_lifo_head_t gasnete_coll_impl_free_list = GASNETI_LIFO_INITIALIZER;
gasnete_coll_implementation_t gasnete_coll_get_implementation() {
  gasnete_coll_implementation_t ret;

  ret = gasneti_lifo_pop(&gasnete_coll_impl_free_list);
  if(!ret) {
    ret = (gasnete_coll_implementation_t) gasneti_malloc(sizeof(struct gasnete_coll_implementation_t_));
  }
  return ret;
}

void gasnete_coll_free_implementation(gasnete_coll_implementation_t in){
  gasneti_lifo_push(&gasnete_coll_impl_free_list, in);
}

gasnete_coll_implementation_t gasnete_coll_autotune_get_bcast_algorithm(gasnet_team_handle_t team, uint32_t flags, size_t nbytes) {
  const size_t eager_limit = gasnete_coll_p2p_eager_min;
  gasnete_coll_implementation_t ret = gasnete_coll_get_implementation();
  
  ret->num_params = 2;
  ret->param_list[0] = GASNETE_COLL_BINOMIAL_TREE;
  ret->param_list[1] = 2;
  
  

  /*for now encode the original decision tree*/
  if ((nbytes <= eager_limit) &&
      (flags & (GASNET_COLL_IN_MYSYNC | GASNET_COLL_OUT_MYSYNC | GASNET_COLL_LOCAL))) {
    /* Small enough for Eager, which will eliminate any barriers for *_MYSYNC and
     * the need for passing addresses for _LOCAL
     * Eager is totally AM-based and thus safe regardless of *_IN_SEGMENT
     */
        ret->fn_ptr = team->autotune_info->collective_algorithms[GASNET_COLL_BROADCAST_OP][GASNETE_COLL_BROADCAST_TREE_EAGER].fn_ptr.bcast_fn; 
  } else if (flags & GASNET_COLL_DST_IN_SEGMENT) {
    /* run the segmented broadcast code 
     function internally checks synch flags and SINGLE/LOCAL flags
    */
    /*this should also be part of the spae*/
    /*ret->fn_ptr = team->autotune_info->collective_algorithms[GASNET_COLL_BROADCAST_OP][GASNETE_COLL_BROADCAST_TREE_RVGET].fn_ptr.bcast_fn;*/ 
    
    if(nbytes <= gasnete_coll_get_pipe_seg_size(team->autotune_info, GASNET_COLL_BROADCAST_OP, flags)) {
      if (flags & (GASNET_COLL_IN_MYSYNC | GASNET_COLL_OUT_MYSYNC | GASNET_COLL_LOCAL)) {
        ret->fn_ptr = team->autotune_info->collective_algorithms[GASNET_COLL_BROADCAST_OP][GASNETE_COLL_BROADCAST_TREE_PUT_SCRATCH].fn_ptr.bcast_fn;
      } else {
        ret->fn_ptr = team->autotune_info->collective_algorithms[GASNET_COLL_BROADCAST_OP][GASNETE_COLL_BROADCAST_TREE_PUT].fn_ptr.bcast_fn;
      }
    } else {
      ret->num_params = 3;
      ret->param_list[2] = gasnete_coll_get_pipe_seg_size(team->autotune_info, GASNET_COLL_BROADCAST_OP, flags);  
      ret->fn_ptr = team->autotune_info->collective_algorithms[GASNET_COLL_BROADCAST_OP][GASNETE_COLL_BROADCAST_TREE_PUT_SEG].fn_ptr.bcast_fn;
    }
  } else if (flags & GASNET_COLL_SRC_IN_SEGMENT) {
    if (flags & (GASNET_COLL_IN_MYSYNC | GASNET_COLL_OUT_MYSYNC | GASNET_COLL_LOCAL)) {
      /* We can use Rendezvous+Get to eliminate any barriers for *_MYSYNC.
       * The Rendezvous is needed for _LOCAL.
       */
      ret->num_params = 0;
      ret->fn_ptr = team->autotune_info->collective_algorithms[GASNET_COLL_BROADCAST_OP][GASNETE_COLL_BROADCAST_RVGET].fn_ptr.bcast_fn;
    } else {
      ret->num_params = 0;
      ret->fn_ptr = team->autotune_info->collective_algorithms[GASNET_COLL_BROADCAST_OP][GASNETE_COLL_BROADCAST_GET].fn_ptr.bcast_fn;
    }
  }  else {
    /* If we reach here then neither src nor dst is in-segment */
    ret->num_params = 0;
    ret->fn_ptr = team->autotune_info->collective_algorithms[GASNET_COLL_BROADCAST_OP][GASNETE_COLL_BROADCAST_RVOUS].fn_ptr.bcast_fn;
  }

  return ret;
}

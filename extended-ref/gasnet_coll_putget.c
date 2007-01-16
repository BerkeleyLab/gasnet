/*   $Source: /Users/kamil/work/gasnet-cvs2/gasnet/extended-ref/gasnet_coll_putget.c,v $
 *     $Date: 2007/01/16 22:05:02 $
 * $Revision: 1.29.6.27 $
 * Description: Reference implemetation of GASNet Collectives team
 * Copyright 2004, Dan Bonachea <bonachea@cs.berkeley.edu>
 * Terms of use are as specified in license.txt
 */

/* for now this file will be directly included in refcoll.c so no need to worry*/
/* about including the header files*/
#if 0
#define GASNET_COLL_TREE_DEBUG 0
#include <gasnet_internal.h>
#include <gasnet_coll.h>
#include <gasnet_coll_internal.h>
#include <gasnet_extended_refcoll.h>
#include <gasnet_vis.h>
#endif

/*---------------------------------------------------------------------------------*/
/* gasnete_coll_broadcast_nb() */

/* bcast Get: all nodes perform uncoordinated gets */
static int gasnete_coll_pf_bcast_Get(gasnete_coll_op_t *op GASNETE_THREAD_FARG) {
  gasnete_coll_generic_data_t *data = op->data;
  const gasnete_coll_broadcast_args_t *args = GASNETE_COLL_GENERIC_ARGS(data, broadcast);
  int result = 0;

  switch (data->state) {
    case 0:	/* Optional IN barrier */
      if (!gasnete_coll_generic_all_threads(data) ||
	  !gasnete_coll_generic_insync(data)) {
	break;
      }
      data->state = 1;

    case 1:	/* Initiate data movement */
      if (gasneti_mynode == args->srcnode) {
	GASNETE_FAST_UNALIGNED_MEMCPY_CHECK(args->dst, args->src, args->nbytes);
      } else {
	if (!GASNETE_COLL_MAY_INIT_FOR(op)) break;
	data->handle = gasnete_get_nb_bulk(args->dst, args->srcnode, args->src,
					   args->nbytes GASNETE_THREAD_PASS);
	gasnete_coll_save_handle(&data->handle GASNETE_THREAD_PASS);
      }
      data->state = 2;

    case 2:	/* Sync data movement */
      if (data->handle != GASNET_INVALID_HANDLE) {
	break;
      }
      data->state = 3;

    case 3:	/* Optional OUT barrier */
      if (!gasnete_coll_generic_outsync(data)) {
	break;
      }

      gasnete_coll_generic_free(data GASNETE_THREAD_PASS);
      result = (GASNETE_COLL_OP_COMPLETE | GASNETE_COLL_OP_INACTIVE);
  }

  return result;
}
extern gasnet_coll_handle_t
gasnete_coll_bcast_Get(gasnet_team_handle_t team,
		       void *dst,
		       gasnet_image_t srcimage, void *src,
		       size_t nbytes, int flags, uint32_t sequence
                       GASNETE_THREAD_FARG)
{
  int options = GASNETE_COLL_GENERIC_OPT_INSYNC_IF (!(flags & GASNET_COLL_IN_NOSYNC)) |
		GASNETE_COLL_GENERIC_OPT_OUTSYNC_IF(!(flags & GASNET_COLL_OUT_NOSYNC));

  gasneti_assert(flags & GASNET_COLL_SINGLE);
  gasneti_assert(flags & GASNET_COLL_SRC_IN_SEGMENT);

  return gasnete_coll_generic_broadcast_nb(team, dst, srcimage, src, nbytes, flags,
					   &gasnete_coll_pf_bcast_Get, options, NULL, sequence GASNETE_THREAD_PASS);
}

/* bcast Put: root node performs carefully ordered puts */
static int gasnete_coll_pf_bcast_Put(gasnete_coll_op_t *op GASNETE_THREAD_FARG) {
  gasnete_coll_generic_data_t *data = op->data;
  const gasnete_coll_broadcast_args_t *args = GASNETE_COLL_GENERIC_ARGS(data, broadcast);
  int result = 0;

  switch (data->state) {
    case 0:	/* Optional IN barrier */
      if (!gasnete_coll_generic_all_threads(data) ||
	  !gasnete_coll_generic_insync(data)) {
	break;
      }
      data->state = 1;

    case 1:	/* Initiate data movement */
      if (gasneti_mynode != args->srcnode) {
	/* Nothing to do */
      } else {
	void   *src   = args->src;
	void   *dst   = args->dst;
	size_t nbytes = args->nbytes;
	if (!GASNETE_COLL_MAY_INIT_FOR(op)) break;

	/* Queue PUTS in an NBI access region */
	gasnete_begin_nbi_accessregion(1 GASNETE_THREAD_PASS);
	{
	  int i;

	  /* Put to nodes to the "right" of ourself */
	  for (i = gasneti_mynode + 1; i < gasneti_nodes; ++i) {
	    gasnete_put_nbi_bulk(i, dst, src, nbytes GASNETE_THREAD_PASS);
	  }
	  /* Put to nodes to the "left" of ourself */
	  for (i = 0; i < gasneti_mynode; ++i) {
	    gasnete_put_nbi_bulk(i, dst, src, nbytes GASNETE_THREAD_PASS);
	  }
	}
	data->handle = gasnete_end_nbi_accessregion(GASNETE_THREAD_PASS_ALONE);
	gasnete_coll_save_handle(&data->handle GASNETE_THREAD_PASS);

	/* Do local copy LAST, perhaps overlapping with communication */
	GASNETE_FAST_UNALIGNED_MEMCPY_CHECK(dst, src, nbytes);
      }
      data->state = 2;

    case 2:	/* Sync data movement */
      if (data->handle != GASNET_INVALID_HANDLE) {
	break;
      }
      data->state = 3;

    case 3:	/* Optional OUT barrier */
      if (!gasnete_coll_generic_outsync(data)) {
	break;
      }

      gasnete_coll_generic_free(data GASNETE_THREAD_PASS);
      result = (GASNETE_COLL_OP_COMPLETE | GASNETE_COLL_OP_INACTIVE);
  }

  return result;
}
extern gasnet_coll_handle_t
gasnete_coll_bcast_Put(gasnet_team_handle_t team,
		       void *dst,
		       gasnet_image_t srcimage, void *src,
		       size_t nbytes, int flags, uint32_t sequence
                       GASNETE_THREAD_FARG)
{
  int options = GASNETE_COLL_GENERIC_OPT_INSYNC_IF (!(flags & GASNET_COLL_IN_NOSYNC)) |
		GASNETE_COLL_GENERIC_OPT_OUTSYNC_IF(!(flags & GASNET_COLL_OUT_NOSYNC));

  gasneti_assert(flags & GASNET_COLL_SINGLE);
  gasneti_assert(flags & GASNET_COLL_DST_IN_SEGMENT);

  return gasnete_coll_generic_broadcast_nb(team, dst, srcimage, src, nbytes, flags,
					   &gasnete_coll_pf_bcast_Put, options,
					   NULL, sequence GASNETE_THREAD_PASS);
}


/* bcast TreePut */
/* Requires GASNETE_COLL_GENERIC_OPT_P2P on non-root nodes */
/* Naturally IN_NOSYNC, OUT_MYSYNC */
/* max size is MaxLongRequest */
static int gasnete_coll_pf_bcast_TreePut(gasnete_coll_op_t *op GASNETE_THREAD_FARG) {
  gasnete_coll_generic_data_t *data = op->data;
  gasnete_coll_tree_data_t *tree = data->tree_info;
  const gasnete_coll_broadcast_args_t *args = GASNETE_COLL_GENERIC_ARGS(data, broadcast);
  gasnet_node_t * const children = GASNETE_COLL_TREE_GEOM_CHILDREN(tree->geom);
  const int child_count = GASNETE_COLL_TREE_GEOM_CHILD_COUNT(tree->geom);
  gasnet_node_t barrier_count;
  int result = 0;
  int child;

  switch (data->state) {
    case 0:	/* Optional IN barrier */
      if (!gasnete_coll_generic_all_threads(data)) {
	break;
      }
      data->state = 1; 

    case 1:
      if(!(op->flags & GASNET_COLL_IN_NOSYNC)) {
  	if (gasneti_weakatomic_read(&(data->p2p->counter), 0) != child_count) {
	  break;
	}
        if (gasneti_mynode != args->srcnode) {
	  gasnete_coll_p2p_advance(op, GASNETE_COLL_TREE_GEOM_PARENT(tree->geom));
	}
      }
      data->state = 2;

    case 2:
      if (gasneti_mynode == args->srcnode) {
	for (child = 0; child < child_count; child++) {
	  gasnete_coll_p2p_signalling_put(op, children[child], args->dst, 
					  args->src, args->nbytes, 0, 1);
	}
	GASNETE_FAST_UNALIGNED_MEMCPY_CHECK(args->dst, args->src, args->nbytes);
      } else if (data->p2p->state[0]) {
		
	gasneti_sync_reads();
	for (child = 0; child < child_count; child++) {
	  gasnete_coll_p2p_signalling_put(op, children[child], args->dst, 
					  args->dst, args->nbytes, 0, 1);
	}
      } else {
	break;	/* Waiting for parent to push data and signal */
      }
      data->state = 3;


    case 3:
      if(op->flags & GASNET_COLL_OUT_ALLSYNC) {
  
        /* if we had to do a barrier on the way in then the counter will advance to double the child_count*/
        barrier_count = child_count + (!(op->flags & GASNET_COLL_IN_NOSYNC) ? child_count : 0);
	if (gasneti_weakatomic_read(&(data->p2p->counter), 0) != barrier_count) {
	  break;
	}
        if (gasneti_mynode != args->srcnode) {
	  gasnete_coll_p2p_advance(op, GASNETE_COLL_TREE_GEOM_PARENT(tree->geom));
	}
        
      }
      data->state = 4;
      
    case 4:	/* Optional OUT barrier thread barrier*/
      if(op->flags & GASNET_COLL_OUT_ALLSYNC) {
        if (!gasnete_coll_generic_all_threads(data)) {
          break;
        }
      }
      data->state = 5;

    case 5: /*done*/
      gasnete_coll_generic_free(data GASNETE_THREAD_PASS);
      result = (GASNETE_COLL_OP_COMPLETE | GASNETE_COLL_OP_INACTIVE);
  }

  return result;
}
extern gasnet_coll_handle_t
gasnete_coll_bcast_TreePut(gasnet_team_handle_t team,
		           void *dst,
			   gasnet_image_t srcimage, void *src,
			   size_t nbytes, int flags,
			   gasnete_coll_tree_kind_t kind,
			   uint32_t sequence
			   GASNETE_THREAD_FARG)
{
  int options = /*GASNETE_COLL_GENERIC_OPT_INSYNC_IF(!(flags & GASNET_COLL_IN_NOSYNC))  |*/
    /*GASNETE_COLL_GENERIC_OPT_OUTSYNC_IF (flags & GASNET_COLL_OUT_ALLSYNC) |*/
    GASNETE_COLL_GENERIC_OPT_P2P;
  

  gasneti_assert(nbytes <= gasnet_AMMaxLongRequest());

  return gasnete_coll_generic_broadcast_nb(team, dst, srcimage, src, nbytes, flags,
					   &gasnete_coll_pf_bcast_TreePut, options,
					   gasnete_coll_tree_init(kind, gasnete_coll_current_fanout,
								  gasnete_coll_image_node(srcimage), team
								  GASNETE_THREAD_PASS),
					   sequence
					   GASNETE_THREAD_PASS);
}

/* bcast TreePutScratch */
/* Requires GASNETE_COLL_GENERIC_OPT_P2P on non-root nodes */
/* Naturally IN_MYSYNC, OUT_MYSYNC and should only be used for IN_MYSYNC*/
/* max size is MaxLongRequest */
static int gasnete_coll_pf_bcast_TreePutScratch(gasnete_coll_op_t *op GASNETE_THREAD_FARG) {
  gasnete_coll_generic_data_t *data = op->data;
  gasnete_coll_tree_data_t *tree = data->tree_info;
  const gasnete_coll_broadcast_args_t *args = GASNETE_COLL_GENERIC_ARGS(data, broadcast);
  gasnet_node_t * const children = GASNETE_COLL_TREE_GEOM_CHILDREN(tree->geom);
  const int child_count = GASNETE_COLL_TREE_GEOM_CHILD_COUNT(tree->geom);
  int result = 0;
  int child;

  switch (data->state) {
  case 0: /*thread barrier*/
    if (!gasnete_coll_generic_all_threads(data)) {
      break;
    }
    data->state = 1;
    


  case 1:
      if (gasneti_mynode == args->srcnode) {
	for (child = 0; child < child_count; child++) {

	  gasnete_coll_p2p_signalling_put(op, children[child], 
					  (int8_t*)op->team->scratch_segs[child].addr+op->scratchpos[child], args->src, args->nbytes, 0, 1);
	  
	}
	GASNETE_FAST_UNALIGNED_MEMCPY(args->dst, args->src, args->nbytes);
      } else if (data->p2p->state[0]) {
	gasneti_sync_reads();
	
	for (child = 0; child < child_count; child++) {

	  gasnete_coll_p2p_signalling_put(op, children[child], 
					  (int8_t*)op->team->scratch_segs[child].addr+op->scratchpos[child], 
					  (int8_t*)op->team->scratch_segs[op->team->myrank].addr+op->myscratchpos, 
					  args->nbytes, 0, 1);

	}

	GASNETE_FAST_UNALIGNED_MEMCPY(args->dst, (int8_t*)op->team->scratch_segs[op->team->myrank].addr+op->myscratchpos, args->nbytes);

      } else {
	break;	/* Waiting for parent to push data and signal */
      }
      data->state = 2;


    case 2:	/* Optional OUT barrier */
      if (!gasnete_coll_generic_outsync(data)) {
	break;
      }

      gasnete_coll_generic_free(data GASNETE_THREAD_PASS);
      result = (GASNETE_COLL_OP_COMPLETE | GASNETE_COLL_OP_INACTIVE);
      /*free up the scratch space used by this op*/
      gasnete_coll_free_scratch(op);
  }

  return result;
}
extern gasnet_coll_handle_t
gasnete_coll_bcast_TreePutScratch(gasnet_team_handle_t team,
		           void *dst,
			   gasnet_image_t srcimage, void *src,
			   size_t nbytes, int flags,
			   gasnete_coll_tree_kind_t kind,
			   uint32_t sequence
			   GASNETE_THREAD_FARG)
{
  /* never allocate an insync barrier since this function should not be used for IN_ALLSYNC*/
  /* use TreePut instead since an inall sync need not pay the extra copy costs to and from the scratch*/
  int options = /*GASNETE_COLL_GENERIC_OPT_INSYNC_IF(!(flags & (GASNET_COLL_IN_NOSYNC|GASNET_COLL_IN_MYSYNC)))  |*/
		GASNETE_COLL_GENERIC_OPT_OUTSYNC_IF (flags & GASNET_COLL_OUT_ALLSYNC) |
		GASNETE_COLL_GENERIC_OPT_P2P_IF(!gasnete_coll_image_is_local(srcimage)) | GASNETE_COLL_USE_SCRATCH;

  gasneti_assert(nbytes <= gasnet_AMMaxLongRequest());

  return gasnete_coll_generic_broadcast_nb(team, dst, srcimage, src, nbytes, flags,
					   &gasnete_coll_pf_bcast_TreePutScratch, options,
					   gasnete_coll_tree_init(kind, gasnete_coll_current_fanout,
								  gasnete_coll_image_node(srcimage), team
								  GASNETE_THREAD_PASS),
					   sequence
					   GASNETE_THREAD_PASS);
	
}

/* bcast TreeGet */
/* Requires GASNETE_COLL_GENERIC_OPT_P2P on all nodes */
/* Naturally IN_MYSYNC, OUT_MYSYNC */
/* size is unbounded */

static int gasnete_coll_pf_bcast_TreeGet(gasnete_coll_op_t *op GASNETE_THREAD_FARG) {
  gasnete_coll_generic_data_t *data = op->data;
  gasnete_coll_tree_data_t *tree = data->tree_info;
  const gasnete_coll_broadcast_args_t *args = GASNETE_COLL_GENERIC_ARGS(data, broadcast);
  int result = 0;
  int i;
  int child;
  
  switch (data->state) {
    case 0:	/* Optional IN barrier */
      if (!gasnete_coll_generic_all_threads(data) ||
	  !gasnete_coll_generic_insync(data)) {
	break;
      }
      data->state = 1;


    case 1:
      if (gasneti_mynode == args->srcnode) {
		      /* Sent my address to my children so they can issue their gets */
        for (child=0; child < GASNETE_COLL_TREE_GEOM_CHILD_COUNT(tree->geom); child++) {
	  gasnete_coll_p2p_eager_addr(op, GASNETE_COLL_TREE_GEOM_CHILDREN(tree->geom)[child], args->src, 0, 1);
        }

	GASNETE_FAST_UNALIGNED_MEMCPY_CHECK(args->dst, args->src, args->nbytes);
        data->state = 3;

	break;	/* skip state 2 */
      } else if (data->p2p->state[0]){
	if (!GASNETE_COLL_MAY_INIT_FOR(op)) break;
	/* I have address from my parent, so perform a get */
	gasneti_sync_reads();
	data->handle = gasnete_get_nb_bulk(args->dst, GASNETE_COLL_TREE_GEOM_PARENT(tree->geom),
					   *(void **)data->p2p->data,
					   args->nbytes GASNETE_THREAD_PASS);
	gasnete_coll_save_handle(&data->handle GASNETE_THREAD_PASS);
      } else {
	break;
      }
  data->state = 2;


    case 2:
      gasneti_assert(gasneti_mynode != args->srcnode);
      if (data->handle != GASNET_INVALID_HANDLE) {
	 break;
      }

      /* Send ack to my parent */
      gasnete_coll_p2p_change_state(op, GASNETE_COLL_TREE_GEOM_PARENT(tree->geom), GASNETE_COLL_TREE_GEOM_SIBLING_ID(tree->geom)+1, 1);
      /* Sent my address to my children so they can issue their gets */
      for (child=0; child < GASNETE_COLL_TREE_GEOM_CHILD_COUNT(tree->geom); child++) {
	gasnete_coll_p2p_eager_addr(op, GASNETE_COLL_TREE_GEOM_CHILDREN(tree->geom)[child], args->dst, 0, 1);
      }
      data->state = 3;


    case 3:	/* Wait for all children to ack */
    {
      int done = 1;
      for (i=1; i <= GASNETE_COLL_TREE_GEOM_CHILD_COUNT(tree->geom); i++) {
	if (data->p2p->state[i] == 0) {
	  done = 0;
	  break;
	}
      }

      if (done) {
	
	data->state = 4;
      } else {
	break;
      }
    }

    case 4:	/* Optional OUT barrier */
      if (!gasnete_coll_generic_outsync(data)) {
	break;
      }

/*      gasnete_coll_tree_free(tree GASNETE_THREAD_PASS); */
      gasnete_coll_generic_free(data GASNETE_THREAD_PASS);
      result = (GASNETE_COLL_OP_COMPLETE | GASNETE_COLL_OP_INACTIVE);
  }

  return result;
}
extern gasnet_coll_handle_t
gasnete_coll_bcast_TreeGet(gasnet_team_handle_t team,
		           void *dst,
			   gasnet_image_t srcimage, void *src,
			   size_t nbytes, int flags,
			   gasnete_coll_tree_kind_t kind,
			   uint32_t sequence
			   GASNETE_THREAD_FARG)
{
  int options = GASNETE_COLL_GENERIC_OPT_INSYNC_IF (flags & GASNET_COLL_IN_ALLSYNC)  |
		GASNETE_COLL_GENERIC_OPT_OUTSYNC_IF(flags & GASNET_COLL_OUT_ALLSYNC) |
		GASNETE_COLL_GENERIC_OPT_P2P;
		#if GASNET_COLL_TREE_DEBUG
	    fprintf(stderr, "%d> TreeGet %d\n", gasneti_mynode, (int)nbytes);
		#endif
  return gasnete_coll_generic_broadcast_nb(team, dst, srcimage, src, nbytes, flags,
					   &gasnete_coll_pf_bcast_TreeGet, options,
					    gasnete_coll_tree_init(kind, gasnete_coll_current_fanout,
								  gasnete_coll_image_node(srcimage), team 
								  GASNETE_THREAD_PASS),
					   sequence
					   GASNETE_THREAD_PASS);
}


/* XXX: broken for the following reasons.
   1) If nbytes is too big to fit in uint32_t, p2p->state will wrap
      Possible fix is to count segments, but would still have a potential
      problem if 4Billion * gasnet_AMMaxLongRequest :-)
   2) Use of signalling put currently is assuming in-order delivery!!
*/
static int gasnete_coll_pf_bcast_sig_TreePutPipe(gasnete_coll_op_t *op GASNETE_THREAD_FARG) {
  gasnete_coll_generic_data_t *data = op->data;
  gasnete_coll_tree_data_t *tree = data->tree_info;
  const gasnete_coll_broadcast_args_t *args = GASNETE_COLL_GENERIC_ARGS(data, broadcast);
  int result = 0;
  int i;
  int child;

  /* XXX: Current size limitation due to using p2p->state to count bytes */
  gasneti_assert(args->nbytes < (size_t)(~((uint32_t)0)));

  switch (data->state) {
    case 0:	/* Optional IN barrier */
      if (!gasnete_coll_generic_all_threads(data) ||
	  !gasnete_coll_generic_insync(data)) {
	break;
      }
      data->state = 1;


    case 1:
      if (gasneti_mynode == args->srcnode) {
	for (i=0; i<args->nbytes; i+=tree->pipe_seg_size) {
	  int msgsize = MIN(tree->pipe_seg_size, args->nbytes-tree->sent_bytes);
	  for (child=0; child<GASNETE_COLL_TREE_GEOM_CHILD_COUNT(tree->geom); child++) {
	    /*  printf(stderr, "%d sending to %d\n", gasneti_mynode, GASNETE_COLL_TREE_GEOM_CHILDREN(tree->geom)[child]); */
	    gasnete_coll_p2p_signalling_put(op, GASNETE_COLL_TREE_GEOM_CHILDREN(tree->geom)[child], (char*)args->dst+i, (char*)args->src+i, msgsize, 0, i+msgsize);
	  }
	  GASNETE_FAST_UNALIGNED_MEMCPY_CHECK(args->dst, args->src, args->nbytes);
	  tree->sent_bytes += msgsize;
	}
	data->state = 2;
      } else if (data->p2p->state[0] > tree->sent_bytes) {
	int msgsize = MIN(tree->pipe_seg_size, args->nbytes-tree->sent_bytes);

	gasneti_sync_reads();
	for (child = 0; child<GASNETE_COLL_TREE_GEOM_CHILD_COUNT(tree->geom); child++) {
	  gasnete_coll_p2p_signalling_put(op, GASNETE_COLL_TREE_GEOM_CHILDREN(tree->geom)[child], (char*)args->dst+tree->sent_bytes,
					  (char*)args->dst+tree->sent_bytes, msgsize, 0, tree->sent_bytes+msgsize);
	}
	tree->sent_bytes += msgsize;

	if (tree->sent_bytes == args->nbytes) {
	  data->state = 2;
	} else {
	  break;
	}
      } else {
	break;
      }


    case 2:	/* Optional OUT barrier */
      if (!gasnete_coll_generic_outsync(data)) {
	break;
      }

     /* gasnete_coll_tree_free(tree GASNETE_THREAD_PASS); */
      gasnete_coll_generic_free(data GASNETE_THREAD_PASS);
      result = (GASNETE_COLL_OP_COMPLETE | GASNETE_COLL_OP_INACTIVE);
  }

  return result;
}

/* XXX: broken for the following reasons.
   1) If nbytes is too big to fit in uint32_t, p2p->state will wrap
      Possible fix is to count segments, but would still have a potential
      problem if 4Billion * gasnet_AMMaxLongRequest :-)
   2) Use of AMs currently is assuming in-order delivery!!
*/
static int gasnete_coll_pf_bcast_TreeGetPipe(gasnete_coll_op_t *op GASNETE_THREAD_FARG) {
  gasnete_coll_generic_data_t *data = op->data;
  gasnete_coll_tree_data_t *tree = data->tree_info;
  const gasnete_coll_broadcast_args_t *args = GASNETE_COLL_GENERIC_ARGS(data, broadcast);
  int result = 0;
  int i;
  int child;

  /* XXX: Current size limitation due to using p2p->state to count bytes */
  gasneti_assert(args->nbytes < (size_t)(~((uint32_t)0)));

  switch (data->state) {
	case 0: /* alloc scratch*/
	case 1:	/* Optional IN barrier */
      if (!gasnete_coll_generic_all_threads(data) ||
	  !gasnete_coll_generic_insync(data)) {
	break;
      }
      data->state = 2;


    case 2:
      if (gasneti_mynode == args->srcnode) {
	for (child=0; child<GASNETE_COLL_TREE_GEOM_CHILD_COUNT(tree->geom); child++) {
	  gasnete_coll_p2p_eager_addr(op, GASNETE_COLL_TREE_GEOM_CHILDREN(tree->geom)[child], args->src, 0, args->nbytes);
	}

	GASNETE_FAST_UNALIGNED_MEMCPY_CHECK(args->dst, args->src, args->nbytes);

	data->state = 4;
	break;
      } else if (data->p2p->state[0] > tree->sent_bytes){
	if (!GASNETE_COLL_MAY_INIT_FOR(op)) break;
	/* I have address from my parent, so perform a get of 1 segment */
	gasneti_sync_reads();

	data->handle =
	  gasnete_get_nb_bulk((uint8_t*)(args->dst)+tree->sent_bytes, GASNETE_COLL_TREE_GEOM_PARENT(tree->geom),
			      ((uint8_t*)(*(void **)data->p2p->data))+tree->sent_bytes,
			      MIN(args->nbytes-tree->sent_bytes, gasnete_coll_pipe_seg_size)
			      GASNETE_THREAD_PASS);
	gasnete_coll_save_handle(&data->handle GASNETE_THREAD_PASS);
	data->state = 3;
      } else {
	break;
      }


    case 3:
      if (data->handle != GASNET_INVALID_HANDLE) {
	break;
      }

      if (tree->sent_bytes == 0) { /*first message*/
	for (child=0; child<GASNETE_COLL_TREE_GEOM_CHILD_COUNT(tree->geom); child++) {
	  gasnete_coll_p2p_eager_addr(op, GASNETE_COLL_TREE_GEOM_CHILDREN(tree->geom)[child], args->dst, 0, MIN(args->nbytes-tree->sent_bytes, gasnete_coll_pipe_seg_size));
	}
      }

      tree->sent_bytes+=MIN(args->nbytes-tree->sent_bytes, gasnete_coll_pipe_seg_size);

      for (child=0; child<GASNETE_COLL_TREE_GEOM_CHILD_COUNT(tree->geom); child++) {
	gasnete_coll_p2p_change_state(op, GASNETE_COLL_TREE_GEOM_CHILDREN(tree->geom)[child], 0, tree->sent_bytes);
      }

      if (tree->sent_bytes<args->nbytes) {
	data->state = 1;	/* still more data to recv */
	break;
      } else {
	gasnete_coll_p2p_change_state(op, GASNETE_COLL_TREE_GEOM_PARENT(tree->geom), GASNETE_COLL_TREE_GEOM_SIBLING_ID(tree->geom)+1, args->nbytes);
	data->state = 4;
      }


    case 4: /* wait for all child nodes to acknowledge recpt */
    {
      int done = 1;
      for (i=1; i<=GASNETE_COLL_TREE_GEOM_CHILD_COUNT(tree->geom); i++) {
	if (data->p2p->state[i]!=args->nbytes) {
	  done = 0;
	  break;
	}
      }

      if (done) {
	data->state=5;
      } else {
	break;
      }
    }


    case 5:	/* Optional OUT barrier */
      if (!gasnete_coll_generic_outsync(data)) {
	break;
      }

/*      gasnete_coll_tree_free(tree GASNETE_THREAD_PASS); */
      gasnete_coll_generic_free(data GASNETE_THREAD_PASS);
      result = (GASNETE_COLL_OP_COMPLETE | GASNETE_COLL_OP_INACTIVE);
  }

  return result;
}

/*---------------------------------------------------------------------------------*/
/* gasnete_coll_broadcastM_nb() */

/* bcastM Get: all nodes perform uncoordinated gets */
/* Valid for SINGLE only, any size */
static int gasnete_coll_pf_bcastM_Get(gasnete_coll_op_t *op GASNETE_THREAD_FARG) {
  gasnete_coll_generic_data_t *data = op->data;
  const gasnete_coll_broadcastM_args_t *args = GASNETE_COLL_GENERIC_ARGS(data, broadcastM);
  int result = 0;

  gasneti_assert(op->flags & GASNET_COLL_SINGLE);

  switch (data->state) {
    case 0:	/* Optional IN barrier */
      if (!gasnete_coll_threads_ready1(op, args->dstlist GASNETE_THREAD_PASS) ||
	  !gasnete_coll_generic_insync(data)) {
	break;
      }
      data->state = 1;

    case 1:	/* Initiate data movement */
      if (gasneti_mynode == args->srcnode) {
	gasnete_coll_local_broadcast(gasnete_coll_my_images,
				     &GASNETE_COLL_MY_1ST_IMAGE(args->dstlist, 0),
				     args->src, args->nbytes);
      } else {
	if (!GASNETE_COLL_MAY_INIT_FOR(op)) break;
        /* Get only the 1st local image */
	data->handle = gasnete_get_nb_bulk(GASNETE_COLL_MY_1ST_IMAGE(args->dstlist, 0),
					   args->srcnode, args->src, args->nbytes GASNETE_THREAD_PASS);
	gasnete_coll_save_handle(&data->handle GASNETE_THREAD_PASS);
      }
      data->state = 2;

    case 2:	/* Sync data movement and perform local copies */
      if (data->handle != GASNET_INVALID_HANDLE) {
	break;
      } else if (gasneti_mynode != args->srcnode) {
	void * const *p = &GASNETE_COLL_MY_1ST_IMAGE(args->dstlist, 0);
	gasneti_sync_reads();
	gasnete_coll_local_broadcast(gasnete_coll_my_images - 1, p + 1, *p, args->nbytes);
      }
      data->state = 3;

    case 3:	/* Optional OUT barrier */
      if (!gasnete_coll_generic_outsync(data)) {
	break;
      }

      gasnete_coll_generic_free(data GASNETE_THREAD_PASS);
      result = (GASNETE_COLL_OP_COMPLETE | GASNETE_COLL_OP_INACTIVE);
  }

  return result;
}
extern gasnet_coll_handle_t
gasnete_coll_bcastM_Get(gasnet_team_handle_t team,
			void * const dstlist[],
			gasnet_image_t srcimage, void *src,
			size_t nbytes, int flags, uint32_t sequence
                        GASNETE_THREAD_FARG)
{
  int options = GASNETE_COLL_GENERIC_OPT_INSYNC_IF (!(flags & GASNET_COLL_IN_NOSYNC)) |
		GASNETE_COLL_GENERIC_OPT_OUTSYNC_IF(!(flags & GASNET_COLL_OUT_NOSYNC));

  return gasnete_coll_generic_broadcastM_nb(team, dstlist, srcimage, src, nbytes, flags,
					    &gasnete_coll_pf_bcastM_Get, options,
					    NULL, sequence GASNETE_THREAD_PASS);
}

/* bcastM Put: root node performs carefully ordered puts */
/* Valid for SINGLE only, any size */
static int gasnete_coll_pf_bcastM_Put(gasnete_coll_op_t *op GASNETE_THREAD_FARG) {
  gasnete_coll_generic_data_t *data = op->data;
  const gasnete_coll_broadcastM_args_t *args = GASNETE_COLL_GENERIC_ARGS(data, broadcastM);
  int result = 0;

  gasneti_assert(op->flags & GASNET_COLL_SINGLE);

  switch (data->state) {
    case 0:	/* Optional IN barrier */
      if (!gasnete_coll_threads_ready1(op, args->dstlist GASNETE_THREAD_PASS) ||
	  !gasnete_coll_generic_insync(data)) {
	break;
      }
      data->state = 1;

    case 1: 	/* Initiate data movement */
      if (gasneti_mynode != args->srcnode) {
	/* Nothing to do */
      } else {
	void   *src   = args->src;
	size_t nbytes = args->nbytes;
	int i, j, limit;
	void * const *p;
	if (!GASNETE_COLL_MAY_INIT_FOR(op)) break;

	/* Queue PUTS in an NBI access region */
	/* We don't use VIS here, since that would send the same data multiple times */
	gasnete_begin_nbi_accessregion(1 GASNETE_THREAD_PASS);
	{
	  /* Put to nodes to the "right" of ourself */
	  if (gasneti_mynode < gasneti_nodes - 1) {
	    p = &GASNETE_COLL_1ST_IMAGE(args->dstlist, gasneti_mynode + 1);
	    for (i = gasneti_mynode + 1; i < gasneti_nodes; ++i) {
	      limit = gasnete_coll_all_images[i];
	      for (j = 0; j < limit; ++j) {
		gasnete_put_nbi_bulk(i, *p, src, nbytes GASNETE_THREAD_PASS);
		++p;
	      }
	    }
	  }
	  /* Put to nodes to the "left" of ourself */
	  if (gasneti_mynode != 0) {
	    p = &GASNETE_COLL_1ST_IMAGE(args->dstlist, 0);
	    for (i = 0; i < gasneti_mynode; ++i) {
	      limit = gasnete_coll_all_images[i];
	      for (j = 0; j < limit; ++j) {
		gasnete_put_nbi_bulk(i, *p, src, nbytes GASNETE_THREAD_PASS);
		++p;
	      }
	    }
	  }
	}
	data->handle = gasnete_end_nbi_accessregion(GASNETE_THREAD_PASS_ALONE);
	gasnete_coll_save_handle(&data->handle GASNETE_THREAD_PASS);

	/* Do local copy LAST, perhaps overlapping with communication */
	gasnete_coll_local_broadcast(gasnete_coll_my_images,
				     &GASNETE_COLL_MY_1ST_IMAGE(args->dstlist, 0),
				     src, nbytes);
      }
      data->state = 2;

    case 2:	/* Sync data movement */
      if (data->handle != GASNET_INVALID_HANDLE) {
	break;
      }
      data->state = 3;

    case 3:	/* Optional OUT barrier */
      if (!gasnete_coll_generic_outsync(data)) {
	break;
      }

      gasnete_coll_generic_free(data GASNETE_THREAD_PASS);
      result = (GASNETE_COLL_OP_COMPLETE | GASNETE_COLL_OP_INACTIVE);
  }

  return result;
}
extern gasnet_coll_handle_t
gasnete_coll_bcastM_Put(gasnet_team_handle_t team,
			void * const dstlist[],
			gasnet_image_t srcimage, void *src,
			size_t nbytes, int flags, uint32_t sequence
                        GASNETE_THREAD_FARG)
{
  int options = GASNETE_COLL_GENERIC_OPT_INSYNC_IF (!(flags & GASNET_COLL_IN_NOSYNC)) |
		GASNETE_COLL_GENERIC_OPT_OUTSYNC_IF(!(flags & GASNET_COLL_OUT_NOSYNC));

  return gasnete_coll_generic_broadcastM_nb(team, dstlist, srcimage, src, nbytes, flags,
					    &gasnete_coll_pf_bcastM_Put, options,
					    NULL, sequence GASNETE_THREAD_PASS);
}

/*---------------------------------------------------------------------------------*/
/* gasnete_coll_scatter_nb() */

/* scat Get: all nodes perform uncoordinated gets */
/* Valid for SINGLE only, any size */
static int gasnete_coll_pf_scat_Get(gasnete_coll_op_t *op GASNETE_THREAD_FARG) {
  gasnete_coll_generic_data_t *data = op->data;
  const gasnete_coll_scatter_args_t *args = GASNETE_COLL_GENERIC_ARGS(data, scatter);
  int result = 0;

  gasneti_assert(op->flags & GASNET_COLL_SINGLE);

  switch (data->state) {
    case 0:	/* Optional IN barrier */
      if (!gasnete_coll_generic_all_threads(data) ||
	  !gasnete_coll_generic_insync(data)) {
	break;
      }
      data->state = 1;

    case 1:	/* Initiate data movement */
      if (gasneti_mynode == args->srcnode) {
	GASNETE_FAST_UNALIGNED_MEMCPY_CHECK(args->dst,
				      gasnete_coll_scale_ptr(args->src, gasneti_mynode, args->nbytes),
				      args->nbytes);
      } else {
	if (!GASNETE_COLL_MAY_INIT_FOR(op)) break;
	data->handle = gasnete_get_nb_bulk(args->dst, args->srcnode,
					   gasnete_coll_scale_ptr(args->src, gasneti_mynode, args->nbytes),
					   args->nbytes GASNETE_THREAD_PASS);
        gasnete_coll_save_handle(&data->handle GASNETE_THREAD_PASS);
      }
      data->state = 2;

    case 2:	/* Sync data movement */
      if (data->handle != GASNET_INVALID_HANDLE) {
	break;
      }
      data->state = 3;

    case 3:	/* Optional OUT barrier */
      if (!gasnete_coll_generic_outsync(data)) {
	break;
      }

      gasnete_coll_generic_free(data GASNETE_THREAD_PASS);
      result = (GASNETE_COLL_OP_COMPLETE | GASNETE_COLL_OP_INACTIVE);
  }

  return result;
}
extern gasnet_coll_handle_t
gasnete_coll_scat_Get(gasnet_team_handle_t team,
		      void *dst,
		      gasnet_image_t srcimage, void *src,
		      size_t nbytes, int flags, uint32_t sequence
                      GASNETE_THREAD_FARG)
{
  int options = GASNETE_COLL_GENERIC_OPT_INSYNC_IF (!(flags & GASNET_COLL_IN_NOSYNC)) |
		GASNETE_COLL_GENERIC_OPT_OUTSYNC_IF(!(flags & GASNET_COLL_OUT_NOSYNC));

  return gasnete_coll_generic_scatter_nb(team, dst, srcimage, src, nbytes, flags,
					 &gasnete_coll_pf_scat_Get, options,
					 NULL, sequence GASNETE_THREAD_PASS);
}

/* scat Put: root node performs carefully ordered puts */
/* Valid for SINGLE only, any size */
static int gasnete_coll_pf_scat_Put(gasnete_coll_op_t *op GASNETE_THREAD_FARG) {
  gasnete_coll_generic_data_t *data = op->data;
  const gasnete_coll_scatter_args_t *args = GASNETE_COLL_GENERIC_ARGS(data, scatter);
  int result = 0;

  gasneti_assert(op->flags & GASNET_COLL_SINGLE);

  switch (data->state) {
    case 0:	/* Optional IN barrier */
      if (!gasnete_coll_generic_all_threads(data) ||
	  !gasnete_coll_generic_insync(data)) {
	break;
      }
      data->state = 1;

    case 1:
      if (gasneti_mynode != args->srcnode) {
	/* Nothing to do */
      } else {
	void   *dst   = args->dst;
	size_t nbytes = args->nbytes;
	uintptr_t p;
	if (!GASNETE_COLL_MAY_INIT_FOR(op)) break;

	/* Queue PUTS in an NBI access region */
	gasnete_begin_nbi_accessregion(1 GASNETE_THREAD_PASS);
	{
	  int i;

	  /* Put to nodes to the "right" of ourself */
	  p = (uintptr_t)gasnete_coll_scale_ptr(args->src, (gasneti_mynode + 1), nbytes);
	  for (i = gasneti_mynode + 1; i < gasneti_nodes; ++i, p += nbytes) {
	    gasnete_put_nbi_bulk(i, dst, (void *)p, nbytes GASNETE_THREAD_PASS);
	  }
	  /* Put to nodes to the "left" of ourself */
	  p = (uintptr_t)gasnete_coll_scale_ptr(args->src, 0, nbytes);
	  for (i = 0; i < gasneti_mynode; ++i, p += nbytes) {
	    gasnete_put_nbi_bulk(i, dst, (void *)p, nbytes GASNETE_THREAD_PASS);
	  }
	}
	data->handle = gasnete_end_nbi_accessregion(GASNETE_THREAD_PASS_ALONE);
        gasnete_coll_save_handle(&data->handle GASNETE_THREAD_PASS);

	/* Do local copy LAST, perhaps overlapping with communication */
	GASNETE_FAST_UNALIGNED_MEMCPY_CHECK(dst,
				      gasnete_coll_scale_ptr(args->src, gasneti_mynode, nbytes),
				      nbytes);
      }
      data->state = 2;

    case 2:	/* Sync data movement */
      if (data->handle != GASNET_INVALID_HANDLE) {
	break;
      }
      data->state = 3;

    case 3:	/* Optional OUT barrier */
      if (!gasnete_coll_generic_outsync(data)) {
	break;
      }

      gasnete_coll_generic_free(data GASNETE_THREAD_PASS);
      result = (GASNETE_COLL_OP_COMPLETE | GASNETE_COLL_OP_INACTIVE);

  }

  return result;
}
extern gasnet_coll_handle_t
gasnete_coll_scat_Put(gasnet_team_handle_t team,
		      void *dst,
		      gasnet_image_t srcimage, void *src,
		      size_t nbytes, int flags, uint32_t sequence
                      GASNETE_THREAD_FARG)
{
  int options = GASNETE_COLL_GENERIC_OPT_INSYNC_IF (!(flags & GASNET_COLL_IN_NOSYNC)) |
		GASNETE_COLL_GENERIC_OPT_OUTSYNC_IF(!(flags & GASNET_COLL_OUT_NOSYNC));

  return gasnete_coll_generic_scatter_nb(team, dst, srcimage, src, nbytes, flags,
					 &gasnete_coll_pf_scat_Put, options,
					 NULL, sequence GASNETE_THREAD_PASS);
}

/* scat Put: root node performs carefully ordered puts */
/* Valid for SINGLE and LOCAL, any size < scratch size ... since we write into the scratch space we need not worry about whether the sender knows the address */
static int gasnete_coll_pf_scat_TreePut(gasnete_coll_op_t *op GASNETE_THREAD_FARG) {
  gasnete_coll_generic_data_t *data = op->data;
  gasnete_coll_tree_data_t *tree = data->tree_info;
  const gasnete_coll_scatter_args_t *args = GASNETE_COLL_GENERIC_ARGS(data, scatter);
  gasnet_node_t * const children = GASNETE_COLL_TREE_GEOM_CHILDREN(tree->geom);
  const gasnet_node_t child_count = GASNETE_COLL_TREE_GEOM_CHILD_COUNT(tree->geom);
  gasnet_node_t barrier_count;
  
  int result = 0,p=1,i,j;
  uint64_t sent_bytes=0;
  
  gasneti_assert(op->flags & GASNET_COLL_SINGLE);
  
  switch (data->state) {
    case 0:	/* Optional IN barrier */
      if (!gasnete_coll_generic_all_threads(data)){
	break;
      }
    /*  fprintf(stderr, "%d> got here\n", gasneti_mynode); */
      data->state = 1;
      
    case 1:
      if(op->flags & GASNET_COLL_IN_ALLSYNC) {
  	if (gasneti_weakatomic_read(&(data->p2p->counter), 0) != child_count) {
	  break;
	}
        if (gasneti_mynode != args->srcnode) {
	  gasnete_coll_p2p_advance(op, GASNETE_COLL_TREE_GEOM_PARENT(tree->geom));
	}
      }
      data->state = 2;
      
    case 2:
      
      if (gasneti_mynode == args->srcnode) {
        for(p=1,i=0; i<child_count; i++) {
          /* reorder the data into the scratch space*/
          for(j=0; j<tree->geom->subtree_sizes[i]; j++, p++) {
             GASNETE_FAST_UNALIGNED_MEMCPY((int8_t*)op->team->scratch_segs[op->team->myrank].addr+op->myscratchpos+args->nbytes*(p-1), 
                                           (int8_t*)args->src+args->nbytes*tree->geom->dfs_order[p], args->nbytes);
            }
            /* put */
        /*  fprintf(stderr, "%d> sending to %d from %d to %d\n", gasneti_mynode, children[i], (int)(op->myscratchpos+sent_bytes), (int)(op->scratchpos[i])); */ 

          gasnete_coll_p2p_signalling_put(op, children[i], 
                                            (int8_t*)op->team->scratch_segs[i].addr+op->scratchpos[i], 
                                            (int8_t*)op->team->scratch_segs[op->team->myrank].addr+op->myscratchpos+sent_bytes, 
                                            args->nbytes*tree->geom->subtree_sizes[i], 0, 1);
          sent_bytes+=tree->geom->subtree_sizes[i]*args->nbytes;
            
          }
        GASNETE_FAST_UNALIGNED_MEMCPY(args->dst, args->src, args->nbytes);
	
      } else if(data->p2p->state[0]){
        gasneti_sync_reads();
        sent_bytes = args->nbytes;
        for(i=0; i<child_count; i++) {
      /*    fprintf(stderr, "%d> sending to %d from %d to %d\n", gasneti_mynode, children[i], (int)(op->myscratchpos+sent_bytes), (int)(op->scratchpos[i])); */

          gasnete_coll_p2p_signalling_put(op, children[i], 
                                          (int8_t*)op->team->scratch_segs[i].addr+op->scratchpos[i], 
                                          (int8_t*)op->team->scratch_segs[op->team->myrank].addr+op->myscratchpos+sent_bytes, 
                                          args->nbytes*tree->geom->subtree_sizes[i], 0, 1);
          sent_bytes+=tree->geom->subtree_sizes[i]*args->nbytes;
        }
        GASNETE_FAST_UNALIGNED_MEMCPY(args->dst, (int8_t*)op->team->scratch_segs[op->team->myrank].addr+op->myscratchpos, args->nbytes);
      
      } else {
        break; /* data not yet arrived*/
      }
      /* by going down the tree with the data we have folded the 
        in all sync barrier with the collective. Since we have performed a barrier we can reset the scratch space*/
      if(op->flags & GASNET_COLL_IN_ALLSYNC) {
        op->team->scratch_status->perform_reset = 1;
      }
      data->state = 3;
   
    case 3: /*node level barrier*/
      if(op->flags & GASNET_COLL_OUT_ALLSYNC) {
        
        /* if we had to do a barrier on the way in then the counter will advance to double the child_count*/
        barrier_count = child_count + ((op->flags & GASNET_COLL_IN_ALLSYNC) ? child_count : 0);
	if (gasneti_weakatomic_read(&(data->p2p->counter), 0) != barrier_count) {
	  break;
	}
        if (gasneti_mynode != args->srcnode) {
	  gasnete_coll_p2p_advance(op, GASNETE_COLL_TREE_GEOM_PARENT(tree->geom));
	}

      }
      data->state = 4;
      
  
    case 4:	/* thread level barrier */
      if(op->flags & GASNET_COLL_OUT_ALLSYNC) {
        if (!gasnete_coll_generic_all_threads(data)) {
          break;
        
        }
        /*again going up the tree with the barrier notifications is the second half of a barrier*/
        /*therefore we can assume a barrier is complete at this point and reset the scratch space*/
        op->team->scratch_status->perform_reset = 1;
      } 
      data->state = 5;
    
    case 5: /*done*/    
      gasnete_coll_generic_free(data GASNETE_THREAD_PASS);
      result = (GASNETE_COLL_OP_COMPLETE | GASNETE_COLL_OP_INACTIVE);
      gasnete_coll_free_scratch(op);
  }
  
  return result;
}
extern gasnet_coll_handle_t
gasnete_coll_scat_TreePut(gasnet_team_handle_t team,
		      void *dst,
		      gasnet_image_t srcimage, void *src,
		      size_t nbytes, int flags, 
                      gasnete_coll_tree_kind_t kind,    
                      uint32_t sequence
                      GASNETE_THREAD_FARG)
{
  int options = /*GASNETE_COLL_GENERIC_OPT_INSYNC_IF (!(flags & GASNET_COLL_IN_NOSYNC)) |
		GASNETE_COLL_GENERIC_OPT_OUTSYNC_IF(!(flags & GASNET_COLL_OUT_NOSYNC)) | */
                GASNETE_COLL_USE_SCRATCH | GASNETE_COLL_GENERIC_OPT_P2P_IF(1);
  
  return gasnete_coll_generic_scatter_nb(team, dst, srcimage, src, nbytes, flags,
					 &gasnete_coll_pf_scat_TreePut, options,
                                         gasnete_coll_tree_init(kind, gasnete_coll_current_fanout,
                                                                gasnete_coll_image_node(srcimage), team
                                                                GASNETE_THREAD_PASS),
                                         sequence GASNETE_THREAD_PASS);
}  
/*---------------------------------------------------------------------------------*/
/* gasnete_coll_scatterM_nb() */

/* scatM Get: all nodes perform uncoordinated gets */
/* Valid for SINGLE only, any size */
static int gasnete_coll_pf_scatM_Get(gasnete_coll_op_t *op GASNETE_THREAD_FARG) {
  gasnete_coll_generic_data_t *data = op->data;
  const gasnete_coll_scatterM_args_t *args = GASNETE_COLL_GENERIC_ARGS(data, scatterM);
  int result = 0;

  gasneti_assert(op->flags & GASNET_COLL_SINGLE);

  switch (data->state) {
    case 0:	/* Optional IN barrier */
      if (!gasnete_coll_threads_ready1(op, args->dstlist GASNETE_THREAD_PASS) ||
	  !gasnete_coll_generic_insync(data)) {
	break;
      }
      data->state = 1;

    case 1:	/* Initiate data movement */
      if (gasneti_mynode == args->srcnode) {
	gasnete_coll_local_scatter(gasnete_coll_my_images,
				   &GASNETE_COLL_MY_1ST_IMAGE(args->dstlist, 0),
				   gasnete_coll_scale_ptr(args->src, gasnete_coll_my_offset, args->nbytes),
				   args->nbytes);
      } else {
	if (!GASNETE_COLL_MAY_INIT_FOR(op)) break;
	data->private_data = gasnete_coll_scale_ptr(args->src, gasnete_coll_my_offset, args->nbytes),
	data->handle = gasnete_geti(gasnete_synctype_nb, gasnete_coll_my_images,
				    &GASNETE_COLL_MY_1ST_IMAGE(args->dstlist, 0), args->nbytes,
			  	    args->srcnode, 1, &(data->private_data),
				    gasnete_coll_my_images * args->nbytes GASNETE_THREAD_PASS);
        gasnete_coll_save_handle(&data->handle GASNETE_THREAD_PASS);
      }
      data->state = 2;

    case 2:	/* Sync data movement */
      if (data->handle != GASNET_INVALID_HANDLE) {
	break;
      }
      data->state = 3;

    case 3:	/* Optional OUT barrier */
      if (!gasnete_coll_generic_outsync(data)) {
	break;
      }

      gasnete_coll_generic_free(data GASNETE_THREAD_PASS);
      result = (GASNETE_COLL_OP_COMPLETE | GASNETE_COLL_OP_INACTIVE);
  }

  return result;
}
extern gasnet_coll_handle_t
gasnete_coll_scatM_Get(gasnet_team_handle_t team,
		       void * const dstlist[],
		       gasnet_image_t srcimage, void *src,
		       size_t nbytes, int flags, uint32_t sequence
                       GASNETE_THREAD_FARG)
{
  int options = GASNETE_COLL_GENERIC_OPT_INSYNC_IF (!(flags & GASNET_COLL_IN_NOSYNC)) |
		GASNETE_COLL_GENERIC_OPT_OUTSYNC_IF(!(flags & GASNET_COLL_OUT_NOSYNC));

  return gasnete_coll_generic_scatterM_nb(team, dstlist, srcimage, src, nbytes, flags,
					  &gasnete_coll_pf_scatM_Get, options,
					  NULL, sequence GASNETE_THREAD_PASS);
}

/* scatM Put: root node performs carefully ordered puts */
/* Valid for SINGLE only, any size */
static int gasnete_coll_pf_scatM_Put(gasnete_coll_op_t *op GASNETE_THREAD_FARG) {
  gasnete_coll_generic_data_t *data = op->data;
  const gasnete_coll_scatterM_args_t *args = GASNETE_COLL_GENERIC_ARGS(data, scatterM);
  int result = 0;

  gasneti_assert(op->flags & GASNET_COLL_SINGLE);

  switch (data->state) {
    case 0:	/* Optional IN barrier */
      if (!gasnete_coll_threads_ready1(op, args->dstlist GASNETE_THREAD_PASS) ||
	  !gasnete_coll_generic_insync(data)) {
	break;
      }
      data->state = 1;

    case 1:
      if (gasneti_mynode != args->srcnode) {
	/* Nothing to do */
      } else {
	size_t nbytes = args->nbytes;
	uintptr_t src_addr;
	int i;
	void ** srclist;
	void * const *p;
	if (!GASNETE_COLL_MAY_INIT_FOR(op)) break;

	/* Allocate a source vector for puti */
	/* XXX: Use freelist? */
	srclist = gasneti_malloc(gasneti_nodes * sizeof(void *));
	data->private_data = srclist;

	/* Queue PUTIs in an NBI access region */
	/* XXX: is gasnete_puti(gasnete_synctype_nbi,...) correct non-tracing variant of puti ? */
	gasnete_begin_nbi_accessregion(1 GASNETE_THREAD_PASS);
	{
	  void **q;

	  /* Put to nodes to the "right" of ourself */
	  src_addr = (uintptr_t)gasnete_coll_scale_ptr(args->src,
			  			       gasnete_coll_all_offset[gasneti_mynode + 1],
						       nbytes);
	  p = &GASNETE_COLL_1ST_IMAGE(args->dstlist, gasneti_mynode + 1);
	  q = &srclist[gasneti_mynode + 1];
	  for (i = gasneti_mynode + 1; i < gasneti_nodes; ++i) {
	    size_t count = gasnete_coll_all_images[i];
	    size_t len = count * nbytes;
	    *q = (void *)src_addr;
	    gasnete_puti(gasnete_synctype_nbi, i, count, p, nbytes, 1, q, len GASNETE_THREAD_PASS);
	    src_addr += len;
	    p += count;
	    ++q;
	  }
	  /* Put to nodes to the "left" of ourself */
	  src_addr = (uintptr_t)gasnete_coll_scale_ptr(args->src, 0, nbytes);
	  p = &GASNETE_COLL_1ST_IMAGE(args->dstlist, 0);
	  q = &srclist[0];
	  for (i = 0; i < gasneti_mynode; ++i) {
	    size_t count = gasnete_coll_all_images[i];
	    size_t len = count * nbytes;
	    *q = (void *)src_addr;
	    gasnete_puti(gasnete_synctype_nbi, i, count, p, nbytes, 1, q, len GASNETE_THREAD_PASS);
	    src_addr += len;
	    p += count;
	    ++q;
	  }
	}
	data->handle = gasnete_end_nbi_accessregion(GASNETE_THREAD_PASS_ALONE);
        gasnete_coll_save_handle(&data->handle GASNETE_THREAD_PASS);

	/* Do local copy LAST, perhaps overlapping with communication */
	gasnete_coll_local_scatter(gasnete_coll_my_images,
				   &GASNETE_COLL_MY_1ST_IMAGE(args->dstlist, 0),
				   gasnete_coll_scale_ptr(args->src, gasnete_coll_my_offset, nbytes),
				   nbytes);
      }
      data->state = 2;

    case 2:	/* Sync data movement */
      if (gasneti_mynode == args->srcnode) {
        if (data->handle != GASNET_INVALID_HANDLE) {
	  break;
        }
        gasneti_free(data->private_data);	/* the temporary srclist */
      }
      data->state = 3;

    case 3:	/* Optional OUT barrier */
      if (!gasnete_coll_generic_outsync(data)) {
	break;
      }

      gasnete_coll_generic_free(data GASNETE_THREAD_PASS);
      result = (GASNETE_COLL_OP_COMPLETE | GASNETE_COLL_OP_INACTIVE);
  }

  return result;
}
extern gasnet_coll_handle_t
gasnete_coll_scatM_Put(gasnet_team_handle_t team,
		       void * const dstlist[],
		       gasnet_image_t srcimage, void *src,
		       size_t nbytes, int flags, uint32_t sequence
                       GASNETE_THREAD_FARG)
{
  int options = GASNETE_COLL_GENERIC_OPT_INSYNC_IF (!(flags & GASNET_COLL_IN_NOSYNC)) |
		GASNETE_COLL_GENERIC_OPT_OUTSYNC_IF(!(flags & GASNET_COLL_OUT_NOSYNC));

  return gasnete_coll_generic_scatterM_nb(team, dstlist, srcimage, src, nbytes, flags,
					  &gasnete_coll_pf_scatM_Put, options,
					  NULL, sequence GASNETE_THREAD_PASS);
}

/*---------------------------------------------------------------------------------*/
/* gasnete_coll_gather_nb() */

/* gath Get: root node performs carefully ordered gets */
/* Valid for SINGLE only, any size */
static int gasnete_coll_pf_gath_Get(gasnete_coll_op_t *op GASNETE_THREAD_FARG) {
  gasnete_coll_generic_data_t *data = op->data;
  const gasnete_coll_gather_args_t *args = GASNETE_COLL_GENERIC_ARGS(data, gather);
  int result = 0;

  gasneti_assert(op->flags & GASNET_COLL_SINGLE);

  switch (data->state) {
    case 0:	/* Optional IN barrier */
      if (!gasnete_coll_generic_all_threads(data) ||
	  !gasnete_coll_generic_insync(data)) {
	break;
      }
      data->state = 1;

    case 1:	/* Initiate data movement */
      if (gasneti_mynode != args->dstnode) {
	/* Nothing to do */
      } else {
	if (!GASNETE_COLL_MAY_INIT_FOR(op)) break;
	/* Queue GETs in an NBI access region */
	gasnete_begin_nbi_accessregion(1 GASNETE_THREAD_PASS);
	{
	  int i;
	  uintptr_t p;

	  /* Get from nodes to the "right" of ourself */
	  p = (uintptr_t)gasnete_coll_scale_ptr(args->dst, (gasneti_mynode + 1), args->nbytes);
	  for (i = gasneti_mynode + 1; i < gasneti_nodes; ++i, p += args->nbytes) {
	    gasnete_get_nbi_bulk((void *)p, i, args->src, args->nbytes GASNETE_THREAD_PASS);
	  }
	  /* Get from nodes to the "left" of ourself */
	  p = (uintptr_t)gasnete_coll_scale_ptr(args->dst, 0, args->nbytes);
	  for (i = 0; i < gasneti_mynode; ++i, p += args->nbytes) {
	    gasnete_get_nbi_bulk((void *)p, i, args->src, args->nbytes GASNETE_THREAD_PASS);
	  }
	}
	data->handle = gasnete_end_nbi_accessregion(GASNETE_THREAD_PASS_ALONE);
        gasnete_coll_save_handle(&data->handle GASNETE_THREAD_PASS);

	/* Do local copy LAST, perhaps overlapping with communication */
	GASNETE_FAST_UNALIGNED_MEMCPY_CHECK(gasnete_coll_scale_ptr(args->dst, gasneti_mynode, args->nbytes),
				      args->src, args->nbytes);
      }
      data->state = 2;

    case 2:	/* Sync data movement */
      if (data->handle != GASNET_INVALID_HANDLE) {
	break;
      }
      data->state = 3;

    case 3:	/* Optional OUT barrier */
      if (!gasnete_coll_generic_outsync(data)) {
	break;
      }

      gasnete_coll_generic_free(data GASNETE_THREAD_PASS);
      result = (GASNETE_COLL_OP_COMPLETE | GASNETE_COLL_OP_INACTIVE);
  }

  return result;
}
extern gasnet_coll_handle_t
gasnete_coll_gath_Get(gasnet_team_handle_t team,
		      gasnet_image_t dstimage, void *dst,
		      void *src,
		      size_t nbytes, int flags, uint32_t sequence
                      GASNETE_THREAD_FARG)
{
  int options = GASNETE_COLL_GENERIC_OPT_INSYNC_IF (!(flags & GASNET_COLL_IN_NOSYNC)) |
		GASNETE_COLL_GENERIC_OPT_OUTSYNC_IF(!(flags & GASNET_COLL_OUT_NOSYNC));

  return gasnete_coll_generic_gather_nb(team, dstimage, dst, src, nbytes, flags,
					&gasnete_coll_pf_gath_Get, options,
					NULL, sequence GASNETE_THREAD_PASS);
}

/* gath Put: all nodes perform uncoordinated puts */
/* Valid for SINGLE only, any size */
static int gasnete_coll_pf_gath_Put(gasnete_coll_op_t *op GASNETE_THREAD_FARG) {
  gasnete_coll_generic_data_t *data = op->data;
  const gasnete_coll_gather_args_t *args = GASNETE_COLL_GENERIC_ARGS(data, gather);
  int result = 0;

  gasneti_assert(op->flags & GASNET_COLL_SINGLE);

  switch (data->state) {
    case 0:	/* Optional IN barrier */
      if (!gasnete_coll_generic_all_threads(data) ||
	  !gasnete_coll_generic_insync(data)) {
	break;
      }
      data->state = 1;

    case 1:	/* Initiate data movement */
      if (gasneti_mynode == args->dstnode) {
	GASNETE_FAST_UNALIGNED_MEMCPY_CHECK(gasnete_coll_scale_ptr(args->dst, gasneti_mynode, args->nbytes),
				      args->src, args->nbytes);
      } else {
	if (!GASNETE_COLL_MAY_INIT_FOR(op)) break;
	data->handle = gasnete_put_nb_bulk(args->dstnode, 
					   gasnete_coll_scale_ptr(args->dst, gasneti_mynode, args->nbytes),
					   args->src, args->nbytes GASNETE_THREAD_PASS);
        gasnete_coll_save_handle(&data->handle GASNETE_THREAD_PASS);
      }
      data->state = 2;

    case 2:	/* Sync data movement */
      if (data->handle != GASNET_INVALID_HANDLE) {
	break;
      }
      data->state = 3;

    case 3:	/* Optional OUT barrier */
      if (!gasnete_coll_generic_outsync(data)) {
	break;
      }

      gasnete_coll_generic_free(data GASNETE_THREAD_PASS);
      result = (GASNETE_COLL_OP_COMPLETE | GASNETE_COLL_OP_INACTIVE);
  }

  return result;
}
extern gasnet_coll_handle_t
gasnete_coll_gath_Put(gasnet_team_handle_t team,
		      gasnet_image_t dstimage, void *dst,
		      void *src,
		      size_t nbytes, int flags, uint32_t sequence
                      GASNETE_THREAD_FARG)
{
  int options = GASNETE_COLL_GENERIC_OPT_INSYNC_IF (!(flags & GASNET_COLL_IN_NOSYNC)) |
		GASNETE_COLL_GENERIC_OPT_OUTSYNC_IF(!(flags & GASNET_COLL_OUT_NOSYNC));

  return gasnete_coll_generic_gather_nb(team, dstimage, dst, src, nbytes, flags,
					&gasnete_coll_pf_gath_Put, options,
					NULL, sequence GASNETE_THREAD_PASS);
}
/* gath Put: all nodes perform uncoordinated puts */
/* Valid for SINGLE and LOCAL, any size < scratch size ... remote threads will not touch user buffers*/
/* XXX Note that an optimization can be made here where the data can be put into the user buffer at the root*/
/* XXX However this will make this implementation only valid for SINGLE and not LOCAL*/

static int gasnete_coll_pf_gath_TreePut(gasnete_coll_op_t *op GASNETE_THREAD_FARG) {
  gasnete_coll_generic_data_t *data = op->data;
  gasnete_coll_tree_data_t *tree = data->tree_info;
  const gasnete_coll_gather_args_t *args = GASNETE_COLL_GENERIC_ARGS(data, gather);
  gasnet_node_t * const children = GASNETE_COLL_TREE_GEOM_CHILDREN(tree->geom);
  gasnet_node_t parent = GASNETE_COLL_TREE_GEOM_PARENT(tree->geom);
  const gasnet_node_t child_count = GASNETE_COLL_TREE_GEOM_CHILD_COUNT(tree->geom);
  gasnet_node_t expected_count;
  int result = 0;
  int i=0;
  
  gasneti_assert(op->flags & GASNET_COLL_SINGLE);
  
  switch (data->state) {
    case 0:	/* Optional IN barrier */
      if (!gasnete_coll_generic_all_threads(data) ||
	  !gasnete_coll_generic_insync(data)) {
	break;
      }
      /* copy my data into the start of the scratch space */
      if(gasneti_mynode!=args->dstnode) {
        GASNETE_FAST_UNALIGNED_MEMCPY((int8_t*)op->team->scratch_segs[op->team->myrank].addr+op->myscratchpos, 
                                    (int8_t*)args->src, args->nbytes);
      } else {
        GASNETE_FAST_UNALIGNED_MEMCPY((int8_t*)args->dst, 
                                      (int8_t*)args->src, args->nbytes);
      
      }
      data->state = 1;
      
    case 1:	/* Local Data Movement */
      /* copy my data into the start of the scratch space */
      if(child_count > 0) {
      GASNETE_FAST_UNALIGNED_MEMCPY((int8_t*)op->team->scratch_segs[op->team->myrank].addr+op->myscratchpos, 
                                    (int8_t*)args->src, args->nbytes);
      }
      data->state = 2;
        
    case 2:
      expected_count = child_count;// + (op->flags & GASNET_COLL_IN_ALLSYNC ? child_count : 0);
      
      /* wait for all my children to send data to me*/
      if (gasneti_weakatomic_read(&(data->p2p->counter), 0) != expected_count) {
        break;
      }
      /* forward the data up to my parent if i am not the root node*/
      if(gasneti_mynode != args->dstnode) {
        if(child_count > 0) {
          gasnete_coll_p2p_counting_put(op, parent,
                                        (int8_t*)op->team->scratch_segs[parent].addr+op->scratchpos[0]+(tree->geom->sibling_offset+1)*args->nbytes,
                                        (int8_t*)op->team->scratch_segs[op->team->myrank].addr+op->myscratchpos,
                                        args->nbytes*tree->geom->mysubtree_size);
        } else {
          gasnete_coll_p2p_counting_put(op, parent,
                                        (int8_t*)op->team->scratch_segs[parent].addr+op->scratchpos[0]+(tree->geom->sibling_offset+1)*args->nbytes,
                                        args->src,
                                        args->nbytes);          
        }
      } else {        
          /* Sync data movement */
          /* reorder the information if i am not the root*/
            for(i=0; i<gasneti_nodes; i++) {
                /*used for temporary variables to aid GDB*/
              int8_t *curr_dest = args->dst;
              int8_t *curr_src = (int8_t*)op->team->scratch_segs[op->team->myrank].addr+op->myscratchpos;
              GASNETE_FAST_UNALIGNED_MEMCPY(curr_dest+args->nbytes*tree->geom->dfs_order[i],
                                            curr_src+args->nbytes*i,
                                            args->nbytes);

            }
      }
      data->state = 3;
          
    case 3:	/* Optional OUT barrier */
          if (!gasnete_coll_generic_outsync(data)) {
            break;
          }
          
          gasnete_coll_generic_free(data GASNETE_THREAD_PASS);
          result = (GASNETE_COLL_OP_COMPLETE | GASNETE_COLL_OP_INACTIVE);
          gasnete_coll_free_scratch(op);

  }
  
  return result;
}
extern gasnet_coll_handle_t
gasnete_coll_gath_TreePut(gasnet_team_handle_t team,
		      gasnet_image_t dstimage, void *dst,
		      void *src,
		      size_t nbytes, int flags,  gasnete_coll_tree_kind_t kind,  
                      uint32_t sequence
                      GASNETE_THREAD_FARG)
{
  int options = GASNETE_COLL_GENERIC_OPT_INSYNC_IF (!(flags & GASNET_COLL_IN_NOSYNC)) |
		GASNETE_COLL_GENERIC_OPT_OUTSYNC_IF(!(flags & GASNET_COLL_OUT_NOSYNC)) | 
               GASNETE_COLL_USE_SCRATCH | GASNETE_COLL_GENERIC_OPT_P2P_IF(1);
  
  return gasnete_coll_generic_gather_nb(team, dstimage, dst, src, nbytes, flags,
					&gasnete_coll_pf_gath_TreePut, options,
                                        gasnete_coll_tree_init(kind, gasnete_coll_current_fanout,
                                                               gasnete_coll_image_node(dstimage), team
                                                               GASNETE_THREAD_PASS), sequence GASNETE_THREAD_PASS);
}

/*---------------------------------------------------------------------------------*/
/* gasnete_coll_gatherM_nb() */

/* gathM Get: root node performs carefully ordered gets */
/* Valid for SINGLE only, any size */
static int gasnete_coll_pf_gathM_Get(gasnete_coll_op_t *op GASNETE_THREAD_FARG) {
  gasnete_coll_generic_data_t *data = op->data;
  const gasnete_coll_gatherM_args_t *args = GASNETE_COLL_GENERIC_ARGS(data, gatherM);
  int result = 0;

  gasneti_assert(op->flags & GASNET_COLL_SINGLE);

  switch (data->state) {
    case 0:	/* Optional IN barrier */
      if (!gasnete_coll_threads_ready1(op, args->srclist GASNETE_THREAD_PASS) ||
	  !gasnete_coll_generic_insync(data)) {
	break;
      }
      data->state = 1;

    case 1:	/* Initiate data movement */
      if (gasneti_mynode != args->dstnode) {
	/* Nothing to do */
      } else {
	size_t nbytes = args->nbytes;
	if (!GASNETE_COLL_MAY_INIT_FOR(op)) break;

	/* Queue GETIs in an NBI access region */
	gasnete_begin_nbi_accessregion(1 GASNETE_THREAD_PASS);
	{
	  void **q;
	  uintptr_t dst_addr;
	  int i;
	  void * const *p;
	  void ** dstlist = gasneti_malloc(gasneti_nodes * sizeof(void *));
	  data->private_data = dstlist;

	  /* Get from the "right" of ourself */
	  dst_addr = (uintptr_t)gasnete_coll_scale_ptr(args->dst,
			  			       gasnete_coll_all_offset[gasneti_mynode + 1],
						       nbytes);
	  p = &GASNETE_COLL_1ST_IMAGE(args->srclist, gasneti_mynode + 1);
	  q = &dstlist[gasneti_mynode + 1];
	  for (i = gasneti_mynode + 1; i < gasneti_nodes; ++i) {
	    size_t count = gasnete_coll_all_images[i];
	    size_t len = count * nbytes;
	    *q = (void *)dst_addr;
	    gasnete_geti(gasnete_synctype_nbi, 1, q, len, i, count, p, nbytes GASNETE_THREAD_PASS);
	    dst_addr += len;
	    p += count;
	    ++q;
	  }
	  /* Get from nodes to the "left" of ourself */
	  dst_addr = (uintptr_t)args->dst;
	  dst_addr = (uintptr_t)gasnete_coll_scale_ptr(args->dst, 0, nbytes);
	  p = &GASNETE_COLL_1ST_IMAGE(args->srclist, 0);
	  q = &dstlist[0];
	  for (i = 0; i < gasneti_mynode; ++i) {
	    size_t count = gasnete_coll_all_images[i];
	    size_t len = count * nbytes;
	    *q = (void *)dst_addr;
	    gasnete_geti(gasnete_synctype_nbi, 1, q, len, i, count, p, nbytes GASNETE_THREAD_PASS);
	    dst_addr += len;
	    p += count;
	    ++q;
	  }
	}
	data->handle = gasnete_end_nbi_accessregion(GASNETE_THREAD_PASS_ALONE);
        gasnete_coll_save_handle(&data->handle GASNETE_THREAD_PASS);

	/* Do local copy LAST, perhaps overlapping with communication */
	gasnete_coll_local_gather(gasnete_coll_my_images,
				  gasnete_coll_scale_ptr(args->dst, gasnete_coll_my_offset, nbytes),
				  &GASNETE_COLL_MY_1ST_IMAGE(args->srclist, 0), nbytes);
      }
      data->state = 2;

    case 2:	/* Sync data movement */
      if (gasneti_mynode == args->dstnode) {
        if (data->handle != GASNET_INVALID_HANDLE) {
	  break;
        }
        gasneti_free(data->private_data);	/* the temporary dstlist */
      }
      data->state = 3;

    case 3:	/* Optional OUT barrier */
      if (!gasnete_coll_generic_outsync(data)) {
	break;
      }

      gasnete_coll_generic_free(data GASNETE_THREAD_PASS);
      result = (GASNETE_COLL_OP_COMPLETE | GASNETE_COLL_OP_INACTIVE);
  }

  return result;
}
extern gasnet_coll_handle_t
gasnete_coll_gathM_Get(gasnet_team_handle_t team,
		       gasnet_image_t dstimage, void *dst,
		       void * const srclist[],
		       size_t nbytes, int flags, uint32_t sequence
                       GASNETE_THREAD_FARG)
{
  int options = GASNETE_COLL_GENERIC_OPT_INSYNC_IF (!(flags & GASNET_COLL_IN_NOSYNC)) |
		GASNETE_COLL_GENERIC_OPT_OUTSYNC_IF(!(flags & GASNET_COLL_OUT_NOSYNC));

  return gasnete_coll_generic_gatherM_nb(team, dstimage, dst, srclist, nbytes, flags,
					 &gasnete_coll_pf_gathM_Get, options,
					 NULL, sequence GASNETE_THREAD_PASS);
}

/* gathM Put: all nodes perform uncoordinated puts */
/* Valid for SINGLE only, any size */
static int gasnete_coll_pf_gathM_Put(gasnete_coll_op_t *op GASNETE_THREAD_FARG) {
  gasnete_coll_generic_data_t *data = op->data;
  const gasnete_coll_gatherM_args_t *args = GASNETE_COLL_GENERIC_ARGS(data, gatherM);
  int result = 0;

  gasneti_assert(op->flags & GASNET_COLL_SINGLE);

  switch (data->state) {
    case 0:	/* Optional IN barrier */
      if (!gasnete_coll_threads_ready1(op, args->srclist GASNETE_THREAD_PASS) ||
	  !gasnete_coll_generic_insync(data)) {
	break;
      }
      data->state = 1;

    case 1:	/* Initiate data movement */
      if (gasneti_mynode == args->dstnode) {
	gasnete_coll_local_gather(gasnete_coll_my_images,
				  gasnete_coll_scale_ptr(args->dst, gasnete_coll_my_offset, args->nbytes),
				  &GASNETE_COLL_MY_1ST_IMAGE(args->srclist, 0), args->nbytes);
      } else {
	if (!GASNETE_COLL_MAY_INIT_FOR(op)) break;
	data->private_data = gasnete_coll_scale_ptr(args->dst, gasnete_coll_my_offset, args->nbytes);
	data->handle = gasnete_puti(gasnete_synctype_nb, args->dstnode,
				    1, &(data->private_data), gasnete_coll_my_images * args->nbytes,
				    gasnete_coll_my_images, &GASNETE_COLL_MY_1ST_IMAGE(args->srclist, 0),
				    args->nbytes GASNETE_THREAD_PASS);
        gasnete_coll_save_handle(&data->handle GASNETE_THREAD_PASS);
      }
      data->state = 2;

    case 2:	/* Sync data movement */
      if (data->handle != GASNET_INVALID_HANDLE) {
	break;
      }

      data->state = 3;

    case 3:	/* Optional OUT barrier */
      if (!gasnete_coll_generic_outsync(data)) {
	break;
      }

      gasnete_coll_generic_free(data GASNETE_THREAD_PASS);
      result = (GASNETE_COLL_OP_COMPLETE | GASNETE_COLL_OP_INACTIVE);
  }

  return result;
}
extern gasnet_coll_handle_t
gasnete_coll_gathM_Put(gasnet_team_handle_t team,
		       gasnet_image_t dstimage, void *dst,
		       void * const srclist[],
		       size_t nbytes, int flags, uint32_t sequence
                       GASNETE_THREAD_FARG)
{
  int options = GASNETE_COLL_GENERIC_OPT_INSYNC_IF (!(flags & GASNET_COLL_IN_NOSYNC)) |
		GASNETE_COLL_GENERIC_OPT_OUTSYNC_IF(!(flags & GASNET_COLL_OUT_NOSYNC));

  return gasnete_coll_generic_gatherM_nb(team, dstimage, dst, srclist, nbytes, flags,
					 &gasnete_coll_pf_gathM_Put, options,
					 NULL, sequence GASNETE_THREAD_PASS);
}


/*---------------------------------------------------------------------------------*/
/* gasnete_coll_gather_all_nb() */

/*** Need Put/Get implementations .... see gasnet_extended_refcoll.c for reference gather_all*/


/*---------------------------------------------------------------------------------*/
/* gasnete_coll_exchange_nb() */

/* exchg Brucks: Implemented using Dissemination based exchange algorithm */
/*
  Based on Bruck et al. algorithm 
  (from IEEE Transactions on Parallel and Distributed Computiing Vol. 8 No. 11 Nov. 1997)
*/
GASNETI_INLINE(gasnete_coll_mypow)
int gasnete_coll_mypow(int base, int pow) {
  int ret = 1;
  while(pow!=0) {
    ret *=base;
    pow--;
  }
    return ret;
}
GASNETI_INLINE(gasnete_coll_pack_all_to_all_msg)
int gasnete_coll_pack_all_to_all_msg(void *src, void *dest, size_t nbytes,
                        int digit, int radix, int j, int total_ranks) {
  int i_idx;

  int ret=0;


  /*pack if the digit_th digit of the radix-r representation of block_id is j*/
  for(i_idx=0; i_idx<total_ranks; i_idx++) {

    if( ((i_idx / gasnete_coll_mypow(radix, digit)) % radix) == j ) {
      GASNETE_FAST_UNALIGNED_MEMCPY((int8_t*)dest+ret*nbytes, (int8_t*)src+i_idx*nbytes, nbytes);
      ret++;
    }
  }
  return ret;
}
GASNETI_INLINE(gasnete_coll_unpack_all_to_all_msg)
void gasnete_coll_unpack_all_to_all_msg(void *src, void *dest, size_t nbytes,
                           int digit, int radix, int j, int total_ranks) {
  int i_idx;

  int blk_count=0;

  for(i_idx=0; i_idx<total_ranks; i_idx++) {
    if( ((i_idx / gasnete_coll_mypow(radix, digit)) % radix) == j ) {
      GASNETE_FAST_UNALIGNED_MEMCPY((int8_t*)dest+i_idx*nbytes, (int8_t*)src+blk_count*nbytes, nbytes);
      blk_count++;
    }

  }

}


static int gasnete_coll_pf_exchg_Dissem(gasnete_coll_op_t *op GASNETE_THREAD_FARG) {
  gasnete_coll_generic_data_t *data = op->data;
  gasnete_coll_dissem_info_t *dissem = data->dissem_info;
  const gasnete_coll_exchange_args_t *args = GASNETE_COLL_GENERIC_ARGS(data, exchange);
  int result = 0;
  
  size_t offset;
  
  void *scratch2;
  void *scratch1;
   
  /*this will be a slightly different poll function than the other ones*/
  /*the state will be used to describe the dissemination phase*/
  /*reserving state 0 and dissem_phase*2+2+1 for the in/out barrier stages*/
  /*state 1 will be used for local memory copies*/
  /*state dissem_phases*2+2 will represent memory copies on the output side*/
  /*states 2 through dissem_phases*2+1 represent intermediary steps*/
  /*each dissem phase will get two steps, one for sending and one for recieiving*/
   if(data->state == 0) {
	if (!gasnete_coll_generic_all_threads(data) ||
	  !gasnete_coll_generic_insync(data)) {
	   return result;
	}
	data->state = 1;
  } 
  
  scratch1 = (int8_t*)op->team->scratch_segs[op->team->myrank].addr + op->scratchpos[op->team->myrank];
  scratch2 = (int8_t*)scratch1 + ((args->nbytes)*dissem->max_dissem_blocks)*((dissem->dissemination_phases+1)*(dissem->dissemination_radix-1));
  
  if(data->state == 1) {
	if(op->team->total_ranks == 1) {
		GASNETE_FAST_UNALIGNED_MEMCPY(args->dst, args->src, args->nbytes);
		data->state = dissem->dissemination_phases*2+3;
		return 0; 
	}
	/* perform local rotation*/
	GASNETE_FAST_UNALIGNED_MEMCPY((int8_t*)scratch2 + (op->team->total_ranks-op->team->myrank)*args->nbytes, 
				      (int8_t*)args->src, op->team->myrank*args->nbytes);
	
	GASNETE_FAST_UNALIGNED_MEMCPY((int8_t*)scratch2, (int8_t*)args->src+op->team->myrank*args->nbytes,
				      (op->team->total_ranks-op->team->myrank)*args->nbytes);
	data->state = 2;
  }
  
  if(data->state>=2 && data->state<=dissem->dissemination_phases*2+1) {
    /*data transfer stages*/
    /*global phase id */
    int destnode,nblocks;
    int phase = (data->state - 2)/2;
    int h,j;
    int distance = gasnete_coll_mypow(dissem->dissemination_radix, phase);
    offset = dissem->max_dissem_blocks*args->nbytes;
    if(phase == (dissem->dissemination_phases-1)) {
      h = op->team->total_ranks / distance;
      if(op->team->total_ranks % distance !=0) {
	h++;
      }
    } else {
      h = dissem->dissemination_radix;
    }
#define IDX_EXPR ((phase*(dissem->dissemination_radix-1) + (j-1))*offset)
#define IDXP1_EXPR (((phase+1)*(dissem->dissemination_radix-1) + (j-1))*offset)
    /*send in even sub phases*/
    if(data->state % 2 == 0) {
      for(j=1; j<h; j++) {
	destnode = (op->team->myrank + j*distance) % op->team->total_ranks;
	nblocks = 
	  gasnete_coll_pack_all_to_all_msg(scratch2, (int8_t*)scratch1+IDX_EXPR,args->nbytes,
					   phase, dissem->dissemination_radix, j, op->team->total_ranks);
	gasnete_coll_p2p_signalling_put(op, destnode, 
					(int8_t*)op->team->scratch_segs[destnode].addr+op->scratchpos[destnode]+IDXP1_EXPR, (int8_t*)scratch1+IDX_EXPR,
					nblocks*args->nbytes, phase, 1);
      }
      /*once all the change the state and return 0*/
      /*let the poll function bring us back here*/
      data->state++;
      return 0;
    } else { /*receive in odd sub phases*/
      /*wait for all the states to trip*/
      /*need to change this to an atomic state increment to do this properly for radix>2*/
      if(data->p2p->state[phase] == h-1) {
	for(j=1; j<h; j++) {
	  gasnete_coll_unpack_all_to_all_msg((int8_t*)scratch1+IDXP1_EXPR, (int8_t*)scratch2, args->nbytes, phase,
					     dissem->dissemination_radix, j, op->team->total_ranks);
	}			
	data->state++;
	return 0;
      } else {
	return 0;
      }
    }
    
#undef IDX_EXPR
#undef IDXP1_EXPR
    
  }
  
  if(data->state == dissem->dissemination_phases*2+2) {
    int i;
    int srcnode;
    for(i=0; i<op->team->total_ranks; i++) {
      srcnode  = (op->team->myrank - i) % op->team->total_ranks;
      if(srcnode < 0) {
	srcnode = op->team->total_ranks+srcnode;
      }
      
      GASNETE_FAST_UNALIGNED_MEMCPY((int8_t*)args->dst+i*args->nbytes,
				    (int8_t*)scratch2+srcnode*args->nbytes,
				    args->nbytes);
    }
    data->state +=1;
    
  }
  if(data->state == dissem->dissemination_phases*2+3) {
    if (!gasnete_coll_generic_outsync(data)) {
      return 0;
    }
    
    gasnete_coll_generic_free(data GASNETE_THREAD_PASS);
    result = (GASNETE_COLL_OP_COMPLETE | GASNETE_COLL_OP_INACTIVE);
    /*free up the scratch space used by this op*/
    gasnete_coll_free_scratch(op);
    
  }
  
  return result;
}

extern gasnet_coll_handle_t
gasnete_coll_exchg_Dissem(gasnet_team_handle_t team,
			void *dst, void *src,
			size_t nbytes, int flags, uint32_t sequence
			GASNETE_THREAD_FARG)
{
  int options =  GASNETE_COLL_USE_SCRATCH | GASNETE_COLL_GENERIC_OPT_P2P | GASNETE_COLL_GENERIC_OPT_INSYNC_IF (!(flags & GASNET_COLL_IN_NOSYNC)) |
		GASNETE_COLL_GENERIC_OPT_OUTSYNC_IF(!(flags & GASNET_COLL_OUT_NOSYNC));
  gasneti_assert(!(flags & GASNETE_COLL_SUBORDINATE));
  
  return gasnete_coll_generic_exchange_nb(team, dst, src, nbytes, flags,
					  &gasnete_coll_pf_exchg_Dissem, options,
					  NULL, gasnete_coll_fetch_dissemination(GASNETE_COLL_DEFAULT_RADIX,team), gasnete_coll_total_images GASNETE_THREAD_PASS);
}

/*---------------------------------------------------------------------------------*/
/* gasnete_coll_exchangeM_nb() */

/* no put/get implementations yet ... reference implementations in refcoll.c*/

/*---------------------------------------------------------------------------------*/



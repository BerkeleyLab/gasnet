/*   $Source: /Users/kamil/work/gasnet-cvs2/gasnet/extended-ref/gasnet_coll_internal.h,v $
 *     $Date: 2005/02/02 20:20:52 $
 * $Revision: 1.16 $
 * Description: GASNet Extended API Collective declarations
 * Copyright 2004, Dan Bonachea <bonachea@cs.berkeley.edu>
 * Terms of use are as specified in license.txt
 */

#ifndef _IN_GASNET_H
  #error This file is not meant to be included directly- clients should include gasnet.h
#endif

#ifndef _GASNET_EXTENDED_COLL_H
#define _GASNET_EXTENDED_COLL_H

/*---------------------------------------------------------------------------------*/
/* Flag values: */

/* Sync flags - NO DEFAULT */
#define GASNET_COLL_IN_NOSYNC	(1<<0)
#define GASNET_COLL_IN_MYSYNC	(1<<1)
#define GASNET_COLL_IN_ALLSYNC	(1<<2)
#define GASNET_COLL_OUT_NOSYNC	(1<<3)
#define GASNET_COLL_OUT_MYSYNC	(1<<4)
#define GASNET_COLL_OUT_ALLSYNC	(1<<5)

#define GASNET_COLL_SINGLE	(1<<6)
#define GASNET_COLL_LOCAL	(1<<7)

#define GASNET_COLL_AGGREGATE	(1<<8)

#define GASNET_COLL_DST_IN_SEGMENT	(1<<9)
#define GASNET_COLL_SRC_IN_SEGMENT	(1<<10)

#define GASNET_COLL_ALL_THREADS	(1<<11)
/* XXX: incomplete? */

#define GASNETE_COLL_IN_MODE(flags) \
	((flags) & (GASNET_COLL_IN_NOSYNC  | GASNET_COLL_IN_MYSYNC  | GASNET_COLL_IN_ALLSYNC))
#define GASNETE_COLL_OUT_MODE(flags) \
	((flags) & (GASNET_COLL_OUT_NOSYNC | GASNET_COLL_OUT_MYSYNC | GASNET_COLL_OUT_ALLSYNC))
#define GASNETE_COLL_SYNC_MODE(flags) \
	((flags) & (GASNET_COLL_OUT_NOSYNC | GASNET_COLL_OUT_MYSYNC | GASNET_COLL_OUT_ALLSYNC | \
		    GASNET_COLL_IN_NOSYNC  | GASNET_COLL_IN_MYSYNC  | GASNET_COLL_IN_ALLSYNC))


/* Internal flags */
#define GASNETE_COLL_OP_COMPLETE	0x1
#define GASNETE_COLL_OP_INACTIVE	0x2

/*---------------------------------------------------------------------------------*/

/* Forward type decls and typedefs: */
struct gasnete_coll_op_t_;
typedef struct gasnete_coll_op_t_ gasnete_coll_op_t;

struct gasnete_coll_p2p_t_;
typedef struct gasnete_coll_p2p_t_ gasnete_coll_p2p_t;

union gasnete_coll_p2p_entry_t_;
typedef union gasnete_coll_p2p_entry_t_ gasnete_coll_p2p_entry_t;

struct gasnete_coll_generic_data_t_;
typedef struct gasnete_coll_generic_data_t_ gasnete_coll_generic_data_t;

/*---------------------------------------------------------------------------------*/

#ifndef GASNETE_COLL_IMAGE_OVERRIDE
  /* gasnet_image_t must be large enough to index all threads that participate
   * in collectives.  A conduit may override this if a larger type is needed.
   */
  typedef uint32_t gasnet_image_t;
  #if GASNET_SEQ
    #define gasnete_coll_image_node(I)	I
  #else
    extern gasnet_node_t *gasnete_coll_image_to_node;
    #define gasnete_coll_image_node(I)	\
	(gasneti_assert(gasnete_coll_image_to_node != NULL), gasnete_coll_image_to_node[I])
  #endif
  #define gasnete_coll_image_is_local(I)	(gasnete_mynode == gasnete_coll_image_node(I))
#endif

/*---------------------------------------------------------------------------------*/

#ifndef GASNETE_COLL_HANDLE_OVERRIDE
  /* Handle type for collective ops: */
  typedef volatile uintptr_t *gasnet_coll_handle_t;
  #define GASNET_COLL_INVALID_HANDLE NULL
#endif

extern gasnet_coll_handle_t gasnete_coll_handle_create(GASNETE_THREAD_FARG_ALONE);
extern void gasnete_coll_handle_signal(gasnet_coll_handle_t handle GASNETE_THREAD_FARG);
extern int gasnete_coll_handle_done(gasnet_coll_handle_t handle GASNETE_THREAD_FARG);

/*---------------------------------------------------------------------------------*/

/* Functions for computational collectives */

#ifndef GASNET_COLL_FN_HANDLE_T
  typedef gasnet_handlerarg_t gasnet_coll_fn_handle_t;
#endif

typedef void (*gasnet_coll_fn_t)();

typedef struct {
    gasnet_coll_fn_t		fnptr;
    unsigned int		flags;
} gasnet_coll_fn_entry_t;

/*---------------------------------------------------------------------------------*/

/* Handle type for collective teams: */
#ifndef GASNETE_COLL_TEAMS_OVERRIDE
    struct gasnete_coll_team_t_;
    typedef struct gasnete_coll_team_t_ *gasnete_coll_team_t;
    typedef gasnete_coll_team_t gasnet_team_handle_t;
    #define GASNET_TEAM_ALL	NULL
#endif

/* Type for collective teams: */
struct gasnete_coll_team_t_ {
    #ifndef GASNETE_COLL_P2P_OVERRIDE
	/* Default implementation of point-to-point syncs
	 * does not (currently) have a team-specific portion.
	 */
    #endif

    /* read-only fields: */
    uint32_t			team_id;

    /* XXX: Design not complete yet */

    /* Hook for conduit-specific extensions/overrides */
    #ifdef GASNETE_COLL_TEAM_EXTRA
      GASNETE_COLL_TEAM_EXTRA
    #endif
};

/*---------------------------------------------------------------------------------*/

/* Function pointer type for polling collective ops: */
typedef int (*gasnete_coll_poll_fn)(gasnete_coll_op_t* GASNETE_THREAD_FARG);

/* Type for collective ops: */
struct gasnete_coll_op_t_ {
    /* Linkage used by the thread-specific active ops list. */
    #ifndef GASNETE_COLL_LIST_OVERRIDE
	/* Default implementation of coll_ops active list */
	gasnete_coll_op_t	*active_next, **active_prev_p;
    #endif

    /* Linkage used by aggregation.
     * Access is serialized by specification+client: */
    #ifndef GASNETE_COLL_AGG_OVERRIDE
	/* Defaule implementation of ops aggregation */
    	gasnete_coll_op_t		*agg_next, *agg_prev, *agg_head;
    #endif

    /* Read-only fields: */
    gasnete_coll_team_t		team;
    uint32_t			sequence;
    int				flags;
    gasnet_coll_handle_t	handle;

    /* Per-instance fields: */
    void			*data;
    gasnete_coll_poll_fn	poll_fn;

    /* Hook for conduit-specific extensions/overrides */
    #ifdef GASNETE_COLL_OP_EXTRA
      GASNETE_COLL_OP_EXTRA
    #endif
};

/*---------------------------------------------------------------------------------*/
/* Type for global synchronization */

#ifndef GASNETE_COLL_CONSENSUS_OVERRIDE
    /* Scalar type, could be a pointer to a struct */
    typedef uint32_t gasnete_coll_consensus_t;
#endif

extern gasnete_coll_consensus_t gasnete_coll_consensus_create(void);
extern int gasnete_coll_consensus_try(gasnete_coll_consensus_t id);

/*---------------------------------------------------------------------------------*/
/* Type for point-to-point synchronization */

#ifndef GASNETE_COLL_P2P_EAGER_SCALE
    /* Number of bytes per-image to allocate for eager data */
    #define GASNETE_COLL_P2P_EAGER_SCALE	16
#endif
#ifndef GASNETE_COLL_P2P_EAGER_MIN
    /* Minumum number of bytes to allocate for eager data */
    #define GASNETE_COLL_P2P_EAGER_MIN		16
#endif

#ifndef GASNETE_COLL_P2P_OVERRIDE
    struct gasnete_coll_p2p_t_ {
	/* Linkage and bookkeeping */
	gasnete_coll_p2p_t	*p2p_next;
	gasnete_coll_p2p_t	*p2p_prev;

	/* Unique (team_id, sequence) tuple for the associated op */
	/* XXX: could play games w/ a single 64-bit field to speed comparisions */
	uint32_t		team_id;
	uint32_t		sequence;

	/* Volatile arrays of data and state for the point-to-point synchronization */
	uint8_t			*data;
	volatile uint32_t	*state;
    };
#endif

extern void gasnete_coll_p2p_init(void);
extern void gasnete_coll_p2p_fini(void);
extern gasnete_coll_p2p_t *gasnete_coll_p2p_get(uint32_t team_id, uint32_t sequence);
extern void gasnete_coll_p2p_destroy(gasnete_coll_p2p_t *p2p);
extern void gasnete_coll_p2p_signalling_put(gasnete_coll_op_t *op, gasnet_node_t dstnode, void *dst,
                                            void *src, size_t nbytes, uint32_t pos, uint32_t state);
extern void gasnete_coll_p2p_signalling_putAsync(gasnete_coll_op_t *op, gasnet_node_t dstnode, void *dst,
						 void *src, size_t nbytes, uint32_t pos, uint32_t state);

/* Treat the eager buffer space at dstnode as an array of elements of length 'size'.
 * Copy 'count' elements to that buffer, starting at element 'offset' at the destination.
 * Set the corresponding entries of the state array to 'state'.
 */
extern void gasnete_coll_p2p_eager_putM(gasnete_coll_op_t *op, gasnet_node_t dstnode,
                                        void *src, uint32_t count, size_t size,
                                        uint32_t offset, uint32_t state);

/* Shorthand for gasnete_coll_p2p_eager_putM with count == 1 */
#ifndef gasnete_coll_p2p_eager_put
  GASNET_INLINE_MODIFIER(gasnete_coll_p2p_eager_put)
  void gasnete_coll_p2p_eager_put(gasnete_coll_op_t *op, gasnet_node_t dstnode,
                                  void *src, size_t size, uint32_t offset, uint32_t state) {
    gasnete_coll_p2p_eager_putM(op, dstnode, src, 1, size, offset, state);
  }
#endif
    
/* Treat the eager buffer space at dstnode as an array of (void *)s.
 * Copy 'count' elements to that buffer, starting at element 'offset' at the destination.
 * Set the corresponding entries of the state array to 'state'.
 */
#ifndef gasnete_coll_p2p_eager_addrM
  GASNET_INLINE_MODIFIER(gasnete_coll_p2p_eager_addrM)
  void gasnete_coll_p2p_eager_addrM(gasnete_coll_op_t *op, gasnet_node_t dstnode,
                                    void * addrlist[], uint32_t count,
				    uint32_t offset, uint32_t state) {
    gasnete_coll_p2p_eager_putM(op, dstnode, addrlist, count, sizeof(void *), offset, state);
  }
#endif

/* Shorthand for gasnete_coll_p2p_eager_addrM with count == 1, taking
 * the address argument by value rather than reference.
 */
#ifndef gasnete_coll_p2p_eager_addr
  GASNET_INLINE_MODIFIER(gasnete_coll_p2p_eager_addr)
  void gasnete_coll_p2p_eager_addr(gasnete_coll_op_t *op, gasnet_node_t dstnode,
                                   void *addr, uint32_t offset, uint32_t state) {
    gasnete_coll_p2p_eager_addrM(op, dstnode, &addr, 1, offset, state);
  }
#endif

/* Treat the eager buffer space on each node as an array of elements of length 'size'.
 * Send (to all but the local node) one element to position 'offset' of that array.
 * Set the corresponding entries of the state array to 'state'.
 * When 'scatter' == 0, the same local element is sent to all nodes (broadcast).
 * When 'scatter' != 0, the source is an array with elements of length 'size', with
 * the ith element sent to node i.
 */
#ifndef gasnete_coll_p2p_eager_put_all
  GASNET_INLINE_MODIFIER(gasnete_coll_p2p_eager_put_all)
  void gasnete_coll_p2p_eager_put_all(gasnete_coll_op_t *op, void *src, size_t size,
				      int scatter, uint32_t offset, uint32_t state) {
    gasnet_node_t i;

    if (scatter) {
      uintptr_t src_addr;

      /* Send to nodes to the "right" of ourself */
      src_addr = (uintptr_t)src + size * (gasnete_mynode + 1);
      for (i = gasnete_mynode + 1; i < gasnete_nodes; ++i, src_addr += size) {
        gasnete_coll_p2p_eager_put(op, i, (void *)src_addr, size, offset, state);
      }
      /* Send to nodes to the "left" of ourself */
      src_addr = (uintptr_t)src;
      for (i = 0; i < gasnete_mynode; ++i, src_addr += size) {
        gasnete_coll_p2p_eager_put(op, i, (void *)src_addr, size, offset, state);
      }
    } else {
      /* Send to nodes to the "right" of ourself */
      for (i = gasnete_mynode + 1; i < gasnete_nodes; ++i) {
        gasnete_coll_p2p_eager_put(op, i, src, size, offset, state);
      }
      /* Send to nodes to the "left" of ourself */
      for (i = 0; i < gasnete_mynode; ++i) {
        gasnete_coll_p2p_eager_put(op, i, src, size, offset, state);
      }
    }
  }
#endif

/* Loop over calls to gasnete_coll_p2p_eager_addr() to send the same
 * address to all nodes except the local node.
 */
#ifndef gasnete_coll_p2p_eager_addr_all
  GASNET_INLINE_MODIFIER(gasnete_coll_p2p_eager_addr_all)
  void gasnete_coll_p2p_eager_addr_all(gasnete_coll_op_t *op, void *addr,
				       uint32_t offset, uint32_t state) {
    gasnet_node_t i;

    /* Send to nodes to the "right" of ourself */
    for (i = gasnete_mynode + 1; i < gasnete_nodes; ++i) {
      gasnete_coll_p2p_eager_addr(op, i, addr, offset, state);
    }
    /* Send to nodes to the "left" of ourself */
    for (i = 0; i < gasnete_mynode; ++i) {
      gasnete_coll_p2p_eager_addr(op, i, addr, offset, state);
    }
  }
#endif

/*---------------------------------------------------------------------------------*/

/* Helper for scaling of void pointers */
GASNET_INLINE_MODIFIER(gasnete_coll_scale_ptr)
void *gasnete_coll_scale_ptr(const void *ptr, size_t elem_count, size_t elem_size) {
    return (void *)((uintptr_t)ptr + (elem_count * elem_size));
}

/* Helper to perform in-memory broadcast */
GASNET_INLINE_MODIFIER(gasnete_coll_local_broadcast)
void gasnete_coll_local_broadcast(size_t count, void * const dstlist[], const void *src, size_t nbytes) {
    /* XXX: this could/should be segemented to cache reuse */
    while (count--) {
	GASNETE_FAST_UNALIGNED_MEMCPY(*dstlist, src, nbytes);
	dstlist++;
    }
    gasneti_sync_writes();	/* Ensure result is visible on all threads */
}

/* Helper to perform in-memory scatter */
GASNET_INLINE_MODIFIER(gasnete_coll_local_scatter)
void gasnete_coll_local_scatter(size_t count, void * const dstlist[], const void *src, size_t nbytes) {
    const uint8_t *src_addr = (const uint8_t *)src;

    while (count--) {
	GASNETE_FAST_UNALIGNED_MEMCPY(*dstlist, src_addr, nbytes);
	dstlist++;
	src_addr += nbytes;
    }
    gasneti_sync_writes();	/* Ensure result is visible on all threads */
}

/* Helper to perform in-memory gather */
GASNET_INLINE_MODIFIER(gasnete_coll_local_gather)
void gasnete_coll_local_gather(size_t count, void * dst, void * const srclist[], size_t nbytes) {
    uint8_t *dst_addr = (uint8_t *)dst;

    while (count--) {
	GASNETE_FAST_UNALIGNED_MEMCPY(dst_addr, *srclist, nbytes);
	dst_addr += nbytes;
	srclist++;
    }
    gasneti_sync_writes();	/* Ensure result is visible on all threads */
}

/*---------------------------------------------------------------------------------*/
/* Thread-specific data: */
typedef struct {
    gasnet_image_t			my_image;
    gasnete_coll_op_t			*op_freelist;
    gasnete_coll_generic_data_t 	*generic_data_freelist;

    /* Linkage used by the thread-specific handle freelist . */
    #ifndef GASNETE_COLL_HANDLE_OVERRIDE
	/* Default implementation of handle freelist */
        gasnet_coll_handle_t		handle_freelist;
    #endif

    /* Linkage used by the thread-specific active ops list. */
    #ifndef GASNETE_COLL_LIST_OVERRIDE
	/* Default implementation of coll_ops active list */
    #endif

    /* XXX: more fields to come */

    /* Macro for conduit-specific extension */
    #ifdef GASNETE_COLL_THREADDATA_EXTRA
      GASNETE_COLL_THREADDATA_EXTRA
    #endif
} gasnete_coll_threaddata_t;

extern gasnete_coll_threaddata_t *gasnete_coll_new_threaddata(void);

/* At this point the type gasnete_threaddata_t might not be defined yet.
 * However, we know gasnete_coll_threaddata MUST be the second pointer.
 */
GASNET_INLINE_MODIFIER(_gasnete_coll_get_threaddata)
gasnete_coll_threaddata_t *
_gasnete_coll_get_threaddata(void *thread) {
    struct _prefix_of_gasnete_threaddata {
	void				*reserved_for_core;
	gasnete_coll_threaddata_t	*reserved_for_coll;
	/* We don't care about the rest */
    } *thread_local = (struct _prefix_of_gasnete_threaddata *)thread;
    gasnete_coll_threaddata_t *result = thread_local->reserved_for_coll;

    if_pf (result == NULL)
	thread_local->reserved_for_coll = result = gasnete_coll_new_threaddata();

    return result;
}

/* Used when thread data might not exist yet */
#define GASNETE_COLL_MYTHREAD	_gasnete_coll_get_threaddata(GASNETE_MYTHREAD)

/* Used when thread data must already exist */
#define GASNETE_COLL_MYTHREAD_NOALLOC \
		(gasneti_assert(GASNETE_MYTHREAD->gasnete_coll_threaddata != NULL), \
		 (gasnete_coll_threaddata_t *)GASNETE_MYTHREAD->gasnete_coll_threaddata)

/*---------------------------------------------------------------------------------*/

extern gasnete_coll_team_t gasnete_coll_team_lookup(uint32_t team_id);

extern gasnete_coll_op_t *
gasnete_coll_op_create(gasnete_coll_team_t team, uint32_t sequence, int flags GASNETE_THREAD_FARG);
extern void
gasnete_coll_op_destroy(gasnete_coll_op_t *op GASNETE_THREAD_FARG);

/* Aggregation interface: */
extern gasnet_coll_handle_t
gasnete_coll_op_submit(gasnete_coll_op_t *op, gasnet_coll_handle_t handle GASNETE_THREAD_FARG);
extern void gasnete_coll_op_complete(gasnete_coll_op_t *op, int poll_result GASNETE_THREAD_FARG);

extern void gasnete_coll_poll(GASNETE_THREAD_FARG_ALONE);

/*---------------------------------------------------------------------------------*/
/* Debugging and tracing macros */

#if GASNET_DEBUG
  /* Argument validation */
  extern void gasnete_coll_validate(gasnet_team_handle_t team,
                                    gasnet_image_t dstimage, const void *dstaddr, size_t dstlen, int dstisv,
                                    gasnet_image_t srcimage, const void *srcaddr, size_t srclen, int srcisv,
                                    int flags);
  #define GASNETE_COLL_VALIDATE gasnete_coll_validate
#else
  #define GASNETE_COLL_VALIDATE(T,DN,DA,DL,DV,SN,SA,SL,SV,F)
#endif

#define GASNETE_COLL_VALIDATE_BROADCAST(T,D,R,S,N,F)   \
	GASNETE_COLL_VALIDATE(T,gasnete_mynode,D,N,0,R,S,N,0,F)
#define GASNETE_COLL_VALIDATE_BROADCAST_M(T,D,R,S,N,F)   \
	GASNETE_COLL_VALIDATE(T,gasnete_mynode,D,N,1,R,S,N,0,F)

#define GASNETE_COLL_VALIDATE_SCATTER(T,D,R,S,N,F)   \
	GASNETE_COLL_VALIDATE(T,gasnete_mynode,D,N,0,R,S,(N)*gasnete_nodes,0,F)
#define GASNETE_COLL_VALIDATE_SCATTER_M(T,D,R,S,N,F)   \
	GASNETE_COLL_VALIDATE(T,gasnete_mynode,D,N,1,R,S,(N)*gasnete_nodes,0,F)

#define GASNETE_COLL_VALIDATE_GATHER(T,R,D,S,N,F)     \
	GASNETE_COLL_VALIDATE(T,R,D,(N)*gasnete_nodes,0,gasnete_mynode,S,N,0,F)
#define GASNETE_COLL_VALIDATE_GATHER_M(T,R,D,S,N,F)     \
	GASNETE_COLL_VALIDATE(T,R,D,(N)*gasnete_nodes,0,gasnete_mynode,S,N,1,F)

#define GASNETE_COLL_VALIDATE_GATHER_ALL(T,D,S,N,F)                \
	GASNETE_COLL_VALIDATE(T,gasnete_mynode,D,(N)*gasnete_nodes,0,gasnete_mynode,S,N,0,F)
#define GASNETE_COLL_VALIDATE_GATHER_ALL_M(T,D,S,N,F)                \
	GASNETE_COLL_VALIDATE(T,gasnete_mynode,D,(N)*gasnete_nodes,1,gasnete_mynode,S,N,1,F)

#define GASNETE_COLL_VALIDATE_EXCHANGE(T,D,S,N,F)                  \
        GASNETE_COLL_VALIDATE(T,gasnete_mynode,D,(N)*gasnete_nodes,0,gasnete_mynode,S,(N)*gasnete_nodes,0,F)
#define GASNETE_COLL_VALIDATE_EXCHANGE_M(T,D,S,N,F)                  \
        GASNETE_COLL_VALIDATE(T,gasnete_mynode,D,(N)*gasnete_nodes,1,gasnete_mynode,S,(N)*gasnete_nodes,1,F)

/*---------------------------------------------------------------------------------*/
/* In-segment checks */

/* Non-fatal check to determine if a given (node,addr,len) is legal as the
 * source of a gasnete_get*() AND the destination of a gasnete_put*().
 * By default this is just the in-segment bounds checks.
 *
 * However, for a purely AM based conduit this might always be true and other
 * conduits may also override this to allow for regions outside the normal
 * segment.  Note that this override relies on the fact that the gasnete_ calls
 * don't perform bounds checking on their own WHICH IS NOT THE CASE for geti and
 * puti at this time.
 */
#ifdef gasnete_coll_in_segment
  /* Keep the conduit-specific override */
#elif defined(GASNET_SEGMENT_EVERYTHING) || defined(GASNETI_SUPPORTS_OUTOFSEGMENT_PUTGET)
  #define gasnete_coll_in_segment(_node,_addr,_len)	1
#else
  #define gasnete_coll_in_segment(_node,_addr,_len)                                \
     (((uintptr_t)(_addr) >= (uintptr_t)gasnete_seginfo[_node].addr) &&            \
       (((uintptr_t)(_addr) + (_len)) <= ((uintptr_t)gasnete_seginfo[_node].addr + \
					  gasnete_seginfo[_node].size)))
#endif

/* The flags GASNET_COLL_SRC_IN_SEGMENT and GASNET_COLL_DST_IN_SEGMENT are just
 * hints from the caller.  If they are NOT set, we will try to determine (when
 * possible) if the addresses are in-segment to allow a one-sided implementation
 * to be used.
 * gasnete_coll_segment_check and gasnete_coll_segment_checkM return a new set of flags.
 */
#ifndef gasnete_coll_segment_check
  GASNET_INLINE_MODIFIER(_gasnete_coll_segment_check_aux)
  int _gasnete_coll_segment_check_aux(int rooted, gasnet_image_t root, const void *addr, size_t len) {
    #if GASNET_ALIGNED_SEGMENTS
      /* It is always sufficient to check against node 0. */
      return gasnete_coll_in_segment(0, addr, len);
    #else
      if (rooted) {
	/* Check the given address against the given node only */
	return gasnete_coll_in_segment(gasnete_coll_image_node(root), addr, len);
      } else {
	/* Check the given address against ALL nodes */
	int i;
	for (i = 0; i < gasnete_nodes; ++i) {
	  if (!gasnete_coll_in_segment(i, addr, len)) {
	    return 0;
	  }
	}
	return 1;
      }
    #endif
  }

  GASNET_INLINE_MODIFIER(gasnete_coll_segment_check)
  int gasnete_coll_segment_check(int flags, 
                                 int dstrooted, gasnet_image_t dstimage, const void *dst, size_t dstlen,
                                 int srcrooted, gasnet_image_t srcimage, const void *src, size_t srclen) {
    #if GASNET_SEGMENT_EVERYTHING
      /* Everything is in-segment, regardless of what the caller told us */
      flags |= (GASNET_COLL_DST_IN_SEGMENT | GASNET_COLL_SRC_IN_SEGMENT);
    #else 
      /* Check destination if caller hasn't asserted that it is in-segment */
      if_pf (!(flags & GASNET_COLL_DST_IN_SEGMENT)) {
	if ((flags & GASNET_COLL_SINGLE) && _gasnete_coll_segment_check_aux(dstrooted, dstimage, dst, dstlen)) {
	  flags |= GASNET_COLL_DST_IN_SEGMENT;
	}
      }
      /* Check source if caller hasn't asserted that it is in-segment */
      if_pf (!(flags & GASNET_COLL_SRC_IN_SEGMENT)) {
	if ((flags & GASNET_COLL_SINGLE) && _gasnete_coll_segment_check_aux(srcrooted, srcimage, src, srclen)) {
	  flags |= GASNET_COLL_SRC_IN_SEGMENT;
	}
      }
    #endif
    return flags;
  }
#endif

#ifndef gasnete_coll_segment_checkM
  GASNET_INLINE_MODIFIER(_gasnete_coll_segment_checkM_aux)
  int _gasnete_coll_segment_checkM_aux(int rooted, gasnet_image_t root, const void *addr, size_t len) {
    if (rooted) {
      /* Check the given address against the given node only */
      #if GASNET_ALIGNED_SEGMENTS /* always use node 0 for cache reuse */
        return gasnete_coll_in_segment(0, addr, len);
      #else
        return gasnete_coll_in_segment(gasnete_coll_image_node(root), addr, len);
      #endif
    } else {
      /* Check the given addresses against ALL nodes */
      void * const *addrlist = (void * const *)addr;
      int i;
      for (i = 0; i < gasnete_nodes; ++i) {
	#if GASNET_ALIGNED_SEGMENTS /* always use node 0 for cache reuse */
          if (!gasnete_coll_in_segment(0, addrlist[i], len)) {
            return 0;
          }
	#else
          if (!gasnete_coll_in_segment(i, addrlist[i], len)) {
            return 0;
          }
	#endif
      }
      return 1;
    }
  }

  GASNET_INLINE_MODIFIER(gasnete_coll_segment_checkM)
  int gasnete_coll_segment_checkM(int flags, 
                                  int dstrooted, gasnet_image_t dstimage, const void *dst, size_t dstlen,
                                  int srcrooted, gasnet_image_t srcimage, const void *src, size_t srclen) {
    #if GASNET_SEGMENT_EVERYTHING
      /* Everything is in-segment, regardless of what the caller told us */
      flags |= (GASNET_COLL_DST_IN_SEGMENT | GASNET_COLL_SRC_IN_SEGMENT);
    #else 
      /* Check destination if caller hasn't asserted that it is in-segment */
      if_pf (!(flags & GASNET_COLL_DST_IN_SEGMENT)) {
	if ((flags & GASNET_COLL_SINGLE) && _gasnete_coll_segment_checkM_aux(dstrooted, dstimage, dst, dstlen)) {
	  flags |= GASNET_COLL_DST_IN_SEGMENT;
	}
      }
      /* Check source if caller hasn't asserted that it is in-segment */
      if_pf (!(flags & GASNET_COLL_SRC_IN_SEGMENT)) {
	if ((flags & GASNET_COLL_SINGLE) && _gasnete_coll_segment_checkM_aux(srcrooted, srcimage, src, srclen)) {
	  flags |= GASNET_COLL_SRC_IN_SEGMENT;
	}
      }
    #endif
    return flags;
  }
#endif

/*------------------------------------------------------------------------------------*/

/* gasnet_coll_init: Initialize GASNet collectives
 *
 *  images:     Array of gasnet_nodes() elements giving the number of
 *              images present on each node.  This must have the
 *              same contents on all nodes or the behavior is undefined.
 *		If NULL, then there is one image per node.
 *		In GASNET_SEQ mode, NULL is the only legal value.
 *  my_image:   If 'images' is non-NULL, this gives the image number of
 *              the calling thread.
 *  fn_tbl:     An array of type gasnet_coll_fn_entry_t, specifying
 *              the functions which can be invoked for the
 *              computational collectives.  This may safely differ
 *              in contents (but not size) across nodes.
 *              Upon return the 'handle' field of each entry is set
 *              to the value that must be passed to the collective.
 *  fn_count:   The number of entries in 'fn_tbl'.  Must agree across
 *              all nodes or the behavior is undefined.
 *  init_flags: Presently unused.  Must be 0.
 */
#ifndef gasnet_coll_init
  extern void gasnete_coll_init(const gasnet_image_t images[], gasnet_image_t my_image,
		  		gasnet_coll_fn_entry_t fn_tbl[], size_t fn_count,
		  		int init_flags GASNETE_THREAD_FARG);
  #define gasnet_coll_init(im,mi,fn,fc,fl) \
		gasnete_coll_init(im,mi,fn,fc,fl GASNETE_THREAD_GET)
#endif

/*---------------------------------------------------------------------------------*/

extern int gasnete_coll_try_sync(gasnet_coll_handle_t handle GASNETE_THREAD_FARG);
GASNET_INLINE_MODIFIER(_gasnet_coll_try_sync)
int _gasnet_coll_try_sync(gasnet_coll_handle_t handle GASNETE_THREAD_FARG) {
  int result = GASNET_OK;
  if_pt (handle != GASNET_COLL_INVALID_HANDLE) {
    result = gasnete_coll_try_sync(handle GASNETE_THREAD_PASS);
    if (result)
      gasneti_sync_reads();
  }
  GASNETI_TRACE_COLL_TRYSYNC(COLL_TRY_SYNC,result);
  return result;
}
#define gasnet_coll_try_sync(handle) \
       _gasnet_coll_try_sync(handle GASNETE_THREAD_GET)

extern int gasnete_coll_try_sync_some(gasnet_coll_handle_t *phandle, size_t numhandles GASNETE_THREAD_FARG);
GASNET_INLINE_MODIFIER(_gasnet_coll_try_sync_some)
int _gasnet_coll_try_sync_some(gasnet_coll_handle_t *phandle, size_t numhandles GASNETE_THREAD_FARG) {
  int result = gasnete_coll_try_sync_some(phandle, numhandles GASNETE_THREAD_PASS);
  if (result)
    gasneti_sync_reads();
  GASNETI_TRACE_COLL_TRYSYNC(COLL_TRY_SYNC_SOME,result);
  return result;
}
#define gasnet_coll_try_sync_some(phandle,numhandles) \
       _gasnet_coll_try_sync_some(phandle,numhandles GASNETE_THREAD_GET)

extern int gasnete_coll_try_sync_all(gasnet_coll_handle_t *phandle, size_t numhandles GASNETE_THREAD_FARG);
GASNET_INLINE_MODIFIER(_gasnet_coll_try_sync_all)
int _gasnet_coll_try_sync_all(gasnet_coll_handle_t *phandle, size_t numhandles GASNETE_THREAD_FARG) {
  int result = gasnete_coll_try_sync_all(phandle, numhandles GASNETE_THREAD_PASS);
  if (result)
    gasneti_sync_reads();
  GASNETI_TRACE_COLL_TRYSYNC(COLL_TRY_SYNC_ALL,result);
  return result;
}
#define gasnet_coll_try_sync_all(phandle,numhandles) \
       _gasnet_coll_try_sync_all(phandle,numhandles GASNETE_THREAD_GET)

#ifdef gasnete_coll_wait_sync
  extern void
  gasnete_coll_wait_sync(gasnet_coll_handle_t handle GASNETE_THREAD_FARG);
#else
  GASNET_INLINE_MODIFIER(gasnete_coll_wait_sync)
  void gasnete_coll_wait_sync(gasnet_coll_handle_t handle GASNETE_THREAD_FARG) {
    if_pt (handle != GASNET_COLL_INVALID_HANDLE) {
      gasneti_waitwhile(gasnete_coll_try_sync(handle GASNETE_THREAD_PASS) == GASNET_ERR_NOT_READY);
    }
  }
#endif
GASNET_INLINE_MODIFIER(_gasnet_coll_wait_sync)
void _gasnet_coll_wait_sync(gasnet_coll_handle_t handle GASNETE_THREAD_FARG) {
  GASNETI_TRACE_COLL_WAITSYNC_BEGIN();
  gasnete_coll_wait_sync(handle GASNETE_THREAD_PASS);
  GASNETI_TRACE_COLL_WAITSYNC_END(COLL_WAIT_SYNC);
}
#define gasnet_coll_wait_sync(handle) \
       _gasnet_coll_wait_sync(handle GASNETE_THREAD_GET)

#ifdef gasnete_coll_wait_sync_some
  extern void
  gasnete_coll_wait_sync_some(gasnet_coll_handle_t *phandle, size_t numhandles GASNETE_THREAD_FARG);
#else
  GASNET_INLINE_MODIFIER(gasnete_coll_wait_sync_some)
  void gasnete_coll_wait_sync_some(gasnet_coll_handle_t *phandle, size_t numhandles GASNETE_THREAD_FARG) {
    gasneti_waitwhile(gasnete_coll_try_sync_some(phandle,numhandles GASNETE_THREAD_PASS) == GASNET_ERR_NOT_READY);
  }
#endif
GASNET_INLINE_MODIFIER(_gasnet_coll_wait_sync_some)
void _gasnet_coll_wait_sync_some(gasnet_coll_handle_t *phandle, size_t numhandles GASNETE_THREAD_FARG) {
  GASNETI_TRACE_COLL_WAITSYNC_BEGIN();
  gasnete_coll_wait_sync_some(phandle,numhandles GASNETE_THREAD_PASS);
  GASNETI_TRACE_COLL_WAITSYNC_END(COLL_WAIT_SYNC_SOME);
}
#define gasnet_coll_wait_sync_some(phandle,numhandles) \
       _gasnet_coll_wait_sync_some(phandle,numhandles GASNETE_THREAD_GET)

#ifdef gasnete_coll_wait_sync_all
  extern void
  gasnete_coll_wait_sync_all(gasnet_coll_handle_t *phandle, size_t numhandles GASNETE_THREAD_FARG);
#else
  GASNET_INLINE_MODIFIER(gasnete_coll_wait_sync_all)
  void gasnete_coll_wait_sync_all(gasnet_coll_handle_t *phandle, size_t numhandles GASNETE_THREAD_FARG) {
    gasneti_waitwhile(gasnete_coll_try_sync_all(phandle,numhandles GASNETE_THREAD_PASS) == GASNET_ERR_NOT_READY);
  }
#endif
GASNET_INLINE_MODIFIER(_gasnet_coll_wait_sync_all)
void _gasnet_coll_wait_sync_all(gasnet_coll_handle_t *phandle, size_t numhandles GASNETE_THREAD_FARG) {
  GASNETI_TRACE_COLL_WAITSYNC_BEGIN();
  gasnete_coll_wait_sync_all(phandle,numhandles GASNETE_THREAD_PASS);
  GASNETI_TRACE_COLL_WAITSYNC_END(COLL_WAIT_SYNC_ALL);
}
#define gasnet_coll_wait_sync_all(phandle,numhandles) \
       _gasnet_coll_wait_sync_all(phandle,numhandles GASNETE_THREAD_GET)

/*---------------------------------------------------------------------------------*/

extern gasnet_coll_handle_t
gasnete_coll_broadcast_nb(gasnet_team_handle_t team,
                          void *dst,
                          gasnet_image_t srcimage, void *src,
                          size_t nbytes, int flags GASNETE_THREAD_FARG);
GASNET_INLINE_MODIFIER(_gasnet_coll_broadcast_nb)
gasnet_coll_handle_t
_gasnet_coll_broadcast_nb(gasnet_team_handle_t team,
                          void *dst,
                          gasnet_image_t srcimage, void *src,
                          size_t nbytes, int flags GASNETE_THREAD_FARG) {
  gasnet_coll_handle_t handle;
  GASNETI_TRACE_COLL_BROADCAST(COLL_BROADCAST_NB,team,dst,srcimage,src,nbytes,flags);
  GASNETE_COLL_VALIDATE_BROADCAST(team,dst,srcimage,src,nbytes,flags);
  handle = gasnete_coll_broadcast_nb(team,dst,srcimage,src,nbytes,flags GASNETE_THREAD_PASS);
  gasneti_AMPoll(); gasnete_coll_poll(GASNETE_THREAD_PASS_ALONE);
  return handle;
}
#define gasnet_coll_broadcast_nb(team,dst,srcimage,src,nbytes,flags) \
       _gasnet_coll_broadcast_nb(team,dst,srcimage,src,nbytes,flags GASNETE_THREAD_GET)

#ifdef gasnete_coll_broadcast
  extern gasnet_coll_handle_t
  gasnete_coll_broadcast(gasnet_team_handle_t team,
                         void *dst,
                         gasnet_image_t srcimage, void *src,
                         size_t nbytes, int flags GASNETE_THREAD_FARG);
#else
  GASNET_INLINE_MODIFIER(gasnete_coll_broadcast)
  void gasnete_coll_broadcast(gasnet_team_handle_t team,
                              void *dst,
                              gasnet_image_t srcimage, void *src,
                              size_t nbytes, int flags GASNETE_THREAD_FARG) {
    gasnet_coll_handle_t handle;
    handle = gasnete_coll_broadcast_nb(team,dst,srcimage,src,nbytes,flags GASNETE_THREAD_PASS);
    gasnete_coll_wait_sync(handle GASNETE_THREAD_PASS);
  }
#endif
GASNET_INLINE_MODIFIER(_gasnet_coll_broadcast)
void _gasnet_coll_broadcast(gasnet_team_handle_t team,
                            void *dst,
                            gasnet_image_t srcimage, void *src,
                            size_t nbytes, int flags GASNETE_THREAD_FARG) {
  GASNETI_TRACE_COLL_BROADCAST(COLL_BROADCAST,team,dst,srcimage,src,nbytes,flags);
  GASNETE_COLL_VALIDATE_BROADCAST(team,dst,srcimage,src,nbytes,flags);
  gasnete_coll_broadcast(team,dst,srcimage,src,nbytes,flags GASNETE_THREAD_PASS);
}
#define gasnet_coll_broadcast(team,dst,srcimage,src,nbytes,flags) \
       _gasnet_coll_broadcast(team,dst,srcimage,src,nbytes,flags GASNETE_THREAD_GET)

/*---------------------------------------------------------------------------------*/

extern gasnet_coll_handle_t
gasnete_coll_broadcastM_nb(gasnet_team_handle_t team,
                           void * const dstlist[],
                           gasnet_image_t srcimage, void *src,
                           size_t nbytes, int flags GASNETE_THREAD_FARG);
GASNET_INLINE_MODIFIER(_gasnet_coll_broadcastM_nb)
gasnet_coll_handle_t
_gasnet_coll_broadcastM_nb(gasnet_team_handle_t team,
                           void * const dstlist[],
                           gasnet_image_t srcimage, void *src,
                           size_t nbytes, int flags GASNETE_THREAD_FARG) {
  gasnet_coll_handle_t handle;
  GASNETI_TRACE_COLL_BROADCAST_M(COLL_BROADCAST_M_NB,team,dstlist,srcimage,src,nbytes,flags);
  GASNETE_COLL_VALIDATE_BROADCAST_M(team,dstlist,srcimage,src,nbytes,flags);
  handle = gasnete_coll_broadcastM_nb(team,dstlist,srcimage,src,nbytes,flags GASNETE_THREAD_PASS);
  gasneti_AMPoll(); gasnete_coll_poll(GASNETE_THREAD_PASS_ALONE);
  return handle;
}
#define gasnet_coll_broadcastM_nb(team,dstlist,srcimage,src,nbytes,flags) \
       _gasnet_coll_broadcastM_nb(team,dstlist,srcimage,src,nbytes,flags GASNETE_THREAD_GET)

#ifdef gasnete_coll_broadcastM
  extern void
  gasnete_coll_broadcastM(gasnet_team_handle_t team,
                          void * const dstlist[],
                          gasnet_image_t srcimage, void *src,
                          size_t nbytes, int flags GASNETE_THREAD_FARG);
#else
  GASNET_INLINE_MODIFIER(gasnete_coll_broadcastM)
  void gasnete_coll_broadcastM(gasnet_team_handle_t team,
                               void * const dstlist[],
                               gasnet_image_t srcimage, void *src,
                               size_t nbytes, int flags GASNETE_THREAD_FARG) {
    gasnet_coll_handle_t handle;
    handle = gasnete_coll_broadcastM_nb(team,dstlist,srcimage,src,nbytes,flags GASNETE_THREAD_PASS);
    gasnete_coll_wait_sync(handle GASNETE_THREAD_PASS);
  }
#endif
GASNET_INLINE_MODIFIER(_gasnet_coll_broadcastM)
void _gasnet_coll_broadcastM(gasnet_team_handle_t team,
                             void * const dstlist[],
                             gasnet_image_t srcimage, void *src,
                             size_t nbytes, int flags GASNETE_THREAD_FARG) {
  GASNETI_TRACE_COLL_BROADCAST_M(COLL_BROADCAST_M,team,dstlist,srcimage,src,nbytes,flags);
  GASNETE_COLL_VALIDATE_BROADCAST_M(team,dstlist,srcimage,src,nbytes,flags);
  gasnete_coll_broadcastM(team,dstlist,srcimage,src,nbytes,flags GASNETE_THREAD_PASS);
}
#define gasnet_coll_broadcastM(team,dstlist,srcimage,src,nbytes,flags) \
       _gasnet_coll_broadcastM(team,dstlist,srcimage,src,nbytes,flags GASNETE_THREAD_GET)

/*---------------------------------------------------------------------------------*/

extern gasnet_coll_handle_t
gasnete_coll_scatter_nb(gasnet_team_handle_t team,
                        void *dst,
                        gasnet_image_t srcimage, void *src,
                        size_t nbytes, int flags GASNETE_THREAD_FARG);
GASNET_INLINE_MODIFIER(_gasnet_coll_scatter_nb)
gasnet_coll_handle_t
_gasnet_coll_scatter_nb(gasnet_team_handle_t team,
                        void *dst,
                        gasnet_image_t srcimage, void *src,
                        size_t nbytes, int flags GASNETE_THREAD_FARG) {
  gasnet_coll_handle_t handle;
  GASNETI_TRACE_COLL_SCATTER(COLL_SCATTER_NB,team,dst,srcimage,src,nbytes,flags);
  GASNETE_COLL_VALIDATE_SCATTER(team,dst,srcimage,src,nbytes,flags);
  handle = gasnete_coll_scatter_nb(team,dst,srcimage,src,nbytes,flags GASNETE_THREAD_PASS);
  gasneti_AMPoll(); gasnete_coll_poll(GASNETE_THREAD_PASS_ALONE);
  return handle;
}
#define gasnet_coll_scatter_nb(team,dst,srcimage,src,nbytes,flags) \
       _gasnet_coll_scatter_nb(team,dst,srcimage,src,nbytes,flags GASNETE_THREAD_GET)

#ifdef gasnete_coll_scatter
  extern void
  gasnete_coll_scatter(gasnet_team_handle_t team,
                       void *dst,
                       gasnet_image_t srcimage, void *src,
                       size_t nbytes, int flags GASNETE_THREAD_FARG);
#else
  GASNET_INLINE_MODIFIER(gasnete_coll_scatter)
  void gasnete_coll_scatter(gasnet_team_handle_t team,
                            void *dst,
                            gasnet_image_t srcimage, void *src,
                            size_t nbytes, int flags GASNETE_THREAD_FARG) {
    gasnet_coll_handle_t handle;
    handle = gasnete_coll_scatter_nb(team,dst,srcimage,src,nbytes,flags GASNETE_THREAD_PASS);
    gasnete_coll_wait_sync(handle GASNETE_THREAD_PASS);
  }
#endif
GASNET_INLINE_MODIFIER(_gasnet_coll_scatter)
void _gasnet_coll_scatter(gasnet_team_handle_t team,
                          void *dst,
                          gasnet_image_t srcimage, void *src,
                          size_t nbytes, int flags GASNETE_THREAD_FARG) {
  GASNETI_TRACE_COLL_SCATTER(COLL_SCATTER,team,dst,srcimage,src,nbytes,flags);
  GASNETE_COLL_VALIDATE_SCATTER(team,dst,srcimage,src,nbytes,flags);
  gasnete_coll_scatter(team,dst,srcimage,src,nbytes,flags GASNETE_THREAD_PASS);
}
#define gasnet_coll_scatter(team,dst,srcimage,src,nbytes,flags) \
       _gasnet_coll_scatter(team,dst,srcimage,src,nbytes,flags GASNETE_THREAD_GET)

/*---------------------------------------------------------------------------------*/

extern gasnet_coll_handle_t
gasnete_coll_scatterM_nb(gasnet_team_handle_t team,
                         void * const dstlist[],
                         gasnet_image_t srcimage, void *src,
                         size_t nbytes, int flags GASNETE_THREAD_FARG);
GASNET_INLINE_MODIFIER(_gasnet_coll_scatterM_nb)
gasnet_coll_handle_t
_gasnet_coll_scatterM_nb(gasnet_team_handle_t team,
                         void * const dstlist[],
                         gasnet_image_t srcimage, void *src,
                         size_t nbytes, int flags GASNETE_THREAD_FARG) {
  gasnet_coll_handle_t handle;
  GASNETI_TRACE_COLL_SCATTER_M(COLL_SCATTER_M_NB,team,dstlist,srcimage,src,nbytes,flags);
  GASNETE_COLL_VALIDATE_SCATTER_M(team,dstlist,srcimage,src,nbytes,flags);
  handle = gasnete_coll_scatterM_nb(team,dstlist,srcimage,src,nbytes,flags GASNETE_THREAD_PASS);
  gasneti_AMPoll(); gasnete_coll_poll(GASNETE_THREAD_PASS_ALONE);
  return handle;
}
#define gasnet_coll_scatterM_nb(team,dstlist,srcimage,src,nbytes,flags) \
       _gasnet_coll_scatterM_nb(team,dstlist,srcimage,src,nbytes,flags GASNETE_THREAD_GET)

#ifdef gasnete_coll_scatterM
  extern void
  gasnete_coll_scatterM(gasnet_team_handle_t team,
                        void * const dstlist[],
                        gasnet_image_t srcimage, void *src,
                        size_t nbytes, int flags GASNETE_THREAD_FARG);
#else
  GASNET_INLINE_MODIFIER(gasnete_coll_scatterM)
  void gasnete_coll_scatterM(gasnet_team_handle_t team,
                             void * const dstlist[],
                             gasnet_image_t srcimage, void *src,
                             size_t nbytes, int flags GASNETE_THREAD_FARG) {
    gasnet_coll_handle_t handle;
    handle = gasnete_coll_scatterM_nb(team,dstlist,srcimage,src,nbytes,flags GASNETE_THREAD_PASS);
    gasnete_coll_wait_sync(handle GASNETE_THREAD_PASS);
  }
#endif
GASNET_INLINE_MODIFIER(_gasnet_coll_scatterM)
void _gasnet_coll_scatterM(gasnet_team_handle_t team,
                           void * const dstlist[],
                           gasnet_image_t srcimage, void *src,
                           size_t nbytes, int flags GASNETE_THREAD_FARG) {
  GASNETI_TRACE_COLL_SCATTER_M(COLL_SCATTER_M,team,dstlist,srcimage,src,nbytes,flags);
  GASNETE_COLL_VALIDATE_SCATTER_M(team,dstlist,srcimage,src,nbytes,flags);
  gasnete_coll_scatterM(team,dstlist,srcimage,src,nbytes,flags GASNETE_THREAD_PASS);
}
#define gasnet_coll_scatterM(team,dstlist,srcimage,src,nbytes,flags) \
       _gasnet_coll_scatterM(team,dstlist,srcimage,src,nbytes,flags GASNETE_THREAD_GET)

/*---------------------------------------------------------------------------------*/

extern gasnet_coll_handle_t
gasnete_coll_gather_nb(gasnet_team_handle_t team,
                       gasnet_image_t dstimage, void *dst,
                       void *src,
                       size_t nbytes, int flags GASNETE_THREAD_FARG);
GASNET_INLINE_MODIFIER(_gasnet_coll_gather_nb)
gasnet_coll_handle_t
_gasnet_coll_gather_nb(gasnet_team_handle_t team,
                       gasnet_image_t dstimage, void *dst,
                       void *src,
                       size_t nbytes, int flags GASNETE_THREAD_FARG) {
  gasnet_coll_handle_t handle;
  GASNETI_TRACE_COLL_GATHER(COLL_GATHER_NB,team,dstimage,dst,src,nbytes,flags);
  GASNETE_COLL_VALIDATE_GATHER(team,dstimage,dst,src,nbytes,flags);
  handle = gasnete_coll_gather_nb(team,dstimage,dst,src,nbytes,flags GASNETE_THREAD_PASS);
  gasneti_AMPoll(); gasnete_coll_poll(GASNETE_THREAD_PASS_ALONE);
  return handle;
}
#define gasnet_coll_gather_nb(team,dstimage,dst,src,nbytes,flags) \
       _gasnet_coll_gather_nb(team,dstimage,dst,src,nbytes,flags GASNETE_THREAD_GET)

#ifdef gasnete_coll_gather
  extern void
  gasnete_coll_gather(gasnet_team_handle_t team,
                      gasnet_image_t dstimage, void *dst,
                      void *src,
                      size_t nbytes, int flags GASNETE_THREAD_FARG);
#else
  GASNET_INLINE_MODIFIER(gasnete_coll_gather)
  void gasnete_coll_gather(gasnet_team_handle_t team,
                           gasnet_image_t dstimage, void *dst,
                           void *src,
                           size_t nbytes, int flags GASNETE_THREAD_FARG) {
    gasnet_coll_handle_t handle;
    handle = gasnete_coll_gather_nb(team,dstimage,dst,src,nbytes,flags GASNETE_THREAD_PASS);
    gasnete_coll_wait_sync(handle GASNETE_THREAD_PASS);
  }
#endif
GASNET_INLINE_MODIFIER(_gasnet_coll_gather)
void _gasnet_coll_gather(gasnet_team_handle_t team,
                         gasnet_image_t dstimage, void *dst,
                         void *src,
                         size_t nbytes, int flags GASNETE_THREAD_FARG) {
  GASNETI_TRACE_COLL_GATHER(COLL_GATHER,team,dstimage,dst,src,nbytes,flags);
  GASNETE_COLL_VALIDATE_GATHER(team,dstimage,dst,src,nbytes,flags);
  gasnete_coll_gather(team,dstimage,dst,src,nbytes,flags GASNETE_THREAD_PASS);
}
#define gasnet_coll_gather(team,dstimage,dst,src,nbytes,flags) \
       _gasnet_coll_gather(team,dstimage,dst,src,nbytes,flags GASNETE_THREAD_GET)

/*---------------------------------------------------------------------------------*/

extern gasnet_coll_handle_t
gasnete_coll_gatherM_nb(gasnet_team_handle_t team,
                        gasnet_image_t dstimage, void *dst,
                        void * const srclist[],
                        size_t nbytes, int flags GASNETE_THREAD_FARG);
GASNET_INLINE_MODIFIER(_gasnet_coll_gatherM_nb)
gasnet_coll_handle_t
_gasnet_coll_gatherM_nb(gasnet_team_handle_t team,
                        gasnet_image_t dstimage, void *dst,
                        void * const srclist[],
                        size_t nbytes, int flags GASNETE_THREAD_FARG) {
  gasnet_coll_handle_t handle;
  GASNETI_TRACE_COLL_GATHER_M(COLL_GATHER_M_NB,team,dstimage,dst,srclist,nbytes,flags);
  GASNETE_COLL_VALIDATE_GATHER_M(team,dstimage,dst,srclist,nbytes,flags);
  handle = gasnete_coll_gatherM_nb(team,dstimage,dst,srclist,nbytes,flags GASNETE_THREAD_PASS);
  gasneti_AMPoll(); gasnete_coll_poll(GASNETE_THREAD_PASS_ALONE);
  return handle;
}
#define gasnet_coll_gatherM_nb(team,dstimage,dst,srclist,nbytes,flags) \
       _gasnet_coll_gatherM_nb(team,dstimage,dst,srclist,nbytes,flags GASNETE_THREAD_GET)

#ifdef gasnete_coll_gatherM
  extern void
  gasnete_coll_gatherM(gasnet_team_handle_t team,
                       gasnet_image_t dstimage, void *dst,
                       void * const srclist[],
                       size_t nbytes, int flags GASNETE_THREAD_FARG);
#else
  GASNET_INLINE_MODIFIER(gasnete_coll_gatherM)
  void gasnete_coll_gatherM(gasnet_team_handle_t team,
                            gasnet_image_t dstimage, void *dst,
                            void * const srclist[],
                            size_t nbytes, int flags GASNETE_THREAD_FARG) {
    gasnet_coll_handle_t handle;
    handle = gasnete_coll_gatherM_nb(team,dstimage,dst,srclist,nbytes,flags GASNETE_THREAD_PASS);
    gasnete_coll_wait_sync(handle GASNETE_THREAD_PASS);
  }
#endif
GASNET_INLINE_MODIFIER(_gasnet_coll_gatherM)
void _gasnet_coll_gatherM(gasnet_team_handle_t team,
                          gasnet_image_t dstimage, void *dst,
                          void * const srclist[],
                          size_t nbytes, int flags GASNETE_THREAD_FARG) {
  GASNETI_TRACE_COLL_GATHER_M(COLL_GATHER_M,team,dstimage,dst,srclist,nbytes,flags);
  GASNETE_COLL_VALIDATE_GATHER_M(team,dstimage,dst,srclist,nbytes,flags);
  gasnete_coll_gatherM(team,dstimage,dst,srclist,nbytes,flags GASNETE_THREAD_PASS);
}
#define gasnet_coll_gatherM(team,dstimage,dst,srclist,nbytes,flags) \
       _gasnet_coll_gatherM(team,dstimage,dst,srclist,nbytes,flags GASNETE_THREAD_GET)

/*---------------------------------------------------------------------------------*/

extern gasnet_coll_handle_t
gasnete_coll_gather_all_nb(gasnet_team_handle_t team,
                           void *dst, void *src,
                           size_t nbytes, int flags GASNETE_THREAD_FARG);
GASNET_INLINE_MODIFIER(_gasnet_coll_gather_all_nb)
gasnet_coll_handle_t
_gasnet_coll_gather_all_nb(gasnet_team_handle_t team,
                           void *dst, void *src,
                           size_t nbytes, int flags GASNETE_THREAD_FARG) {
  gasnet_coll_handle_t handle;
  GASNETI_TRACE_COLL_GATHER_ALL(COLL_GATHER_ALL_NB,team,dst,src,nbytes,flags);
  GASNETE_COLL_VALIDATE_GATHER_ALL(team,dst,src,nbytes,flags);
  handle = gasnete_coll_gather_all_nb(team,dst,src,nbytes,flags GASNETE_THREAD_PASS);
  gasneti_AMPoll(); gasnete_coll_poll(GASNETE_THREAD_PASS_ALONE);
  return handle;
}
#define gasnet_coll_gather_all_nb(team,dst,src,nbytes,flags) \
       _gasnet_coll_gather_all_nb(team,dst,src,nbytes,flags GASNETE_THREAD_GET)

#ifdef gasnete_coll_gather_all
  extern void
  gasnete_coll_gather_all(gasnet_team_handle_t team,
                          void *dst, void *src,
                          size_t nbytes, int flags GASNETE_THREAD_FARG);
#else
  GASNET_INLINE_MODIFIER(gasnete_coll_gather_all)
  void gasnete_coll_gather_all(gasnet_team_handle_t team,
                               void *dst, void *src,
                               size_t nbytes, int flags GASNETE_THREAD_FARG) {
    gasnet_coll_handle_t handle;
    handle = gasnete_coll_gather_all_nb(team,dst,src,nbytes,flags GASNETE_THREAD_PASS);
    gasnete_coll_wait_sync(handle GASNETE_THREAD_PASS);
  }
#endif
GASNET_INLINE_MODIFIER(_gasnet_coll_gather_all)
void _gasnet_coll_gather_all(gasnet_team_handle_t team,
                             void *dst, void *src,
                             size_t nbytes, int flags GASNETE_THREAD_FARG) {
  GASNETI_TRACE_COLL_GATHER_ALL(COLL_GATHER_ALL,team,dst,src,nbytes,flags);
  GASNETE_COLL_VALIDATE_GATHER_ALL(team,dst,src,nbytes,flags);
  gasnete_coll_gather_all(team,dst,src,nbytes,flags GASNETE_THREAD_PASS);
}
#define gasnet_coll_gather_all(team,dst,src,nbytes,flags) \
       _gasnet_coll_gather_all(team,dst,src,nbytes,flags GASNETE_THREAD_GET)

/*---------------------------------------------------------------------------------*/

extern gasnet_coll_handle_t
gasnete_coll_gather_allM_nb(gasnet_team_handle_t team,
                            void * const dstlist[], void * const srclist[],
                            size_t nbytes, int flags GASNETE_THREAD_FARG);
GASNET_INLINE_MODIFIER(_gasnet_coll_gather_allM_nb)
gasnet_coll_handle_t
_gasnet_coll_gather_allM_nb(gasnet_team_handle_t team,
                            void * const dstlist[], void * const srclist[],
                            size_t nbytes, int flags GASNETE_THREAD_FARG) {
  gasnet_coll_handle_t handle;
  GASNETI_TRACE_COLL_GATHER_ALL_M(COLL_GATHER_ALL_M_NB,team,dstlist,srclist,nbytes,flags);
  GASNETE_COLL_VALIDATE_GATHER_ALL_M(team,dstlist,srclist,nbytes,flags);
  handle = gasnete_coll_gather_allM_nb(team,dstlist,srclist,nbytes,flags GASNETE_THREAD_PASS);
  gasneti_AMPoll(); gasnete_coll_poll(GASNETE_THREAD_PASS_ALONE);
  return handle;
}
#define gasnet_coll_gather_allM_nb(team,dstlist,srclist,nbytes,flags) \
       _gasnet_coll_gather_allM_nb(team,dstlist,srclist,nbytes,flags GASNETE_THREAD_GET)

#ifdef gasnete_coll_gather_allM
  extern void
  gasnete_coll_gather_allM(gasnet_team_handle_t team,
                           void * const dstlist[], void * const srclist[],
                           size_t nbytes, int flags GASNETE_THREAD_FARG);
#else
  GASNET_INLINE_MODIFIER(gasnete_coll_gather_allM)
  void gasnete_coll_gather_allM(gasnet_team_handle_t team,
                                void * const dstlist[], void * const srclist[],
                                size_t nbytes, int flags GASNETE_THREAD_FARG) {
    gasnet_coll_handle_t handle;
    handle = gasnete_coll_gather_allM_nb(team,dstlist,srclist,nbytes,flags GASNETE_THREAD_PASS);
    gasnete_coll_wait_sync(handle GASNETE_THREAD_PASS);
  }
#endif
GASNET_INLINE_MODIFIER(_gasnet_coll_gather_allM)
void _gasnet_coll_gather_allM(gasnet_team_handle_t team,
                              void * const dstlist[], void * const srclist[],
                              size_t nbytes, int flags GASNETE_THREAD_FARG) {
  GASNETI_TRACE_COLL_GATHER_ALL_M(COLL_GATHER_ALL_M,team,dstlist,srclist,nbytes,flags);
  GASNETE_COLL_VALIDATE_GATHER_ALL_M(team,dstlist,srclist,nbytes,flags);
  gasnete_coll_gather_allM(team,dstlist,srclist,nbytes,flags GASNETE_THREAD_PASS);
}
#define gasnet_coll_gather_allM(team,dstlist,srclist,nbytes,flags) \
       _gasnet_coll_gather_allM(team,dstlist,srclist,nbytes,flags GASNETE_THREAD_GET)

/*---------------------------------------------------------------------------------*/

extern gasnet_coll_handle_t
gasnete_coll_exchange_nb(gasnet_team_handle_t team,
                         void *dst, void *src,
                         size_t nbytes, int flags GASNETE_THREAD_FARG);
GASNET_INLINE_MODIFIER(_gasnet_coll_exchange_nb)
gasnet_coll_handle_t
_gasnet_coll_exchange_nb(gasnet_team_handle_t team,
                         void *dst, void *src,
                         size_t nbytes, int flags GASNETE_THREAD_FARG) {
  gasnet_coll_handle_t handle;
  GASNETI_TRACE_COLL_EXCHANGE(COLL_EXCHANGE_NB,team,dst,src,nbytes,flags);
  GASNETE_COLL_VALIDATE_EXCHANGE(team,dst,src,nbytes,flags);
  handle = gasnete_coll_exchange_nb(team,dst,src,nbytes,flags GASNETE_THREAD_PASS);
  gasneti_AMPoll(); gasnete_coll_poll(GASNETE_THREAD_PASS_ALONE);
  return handle;
}
#define gasnet_coll_exchange_nb(team,dst,src,nbytes,flags) \
       _gasnet_coll_exchange_nb(team,dst,src,nbytes,flags GASNETE_THREAD_GET)

#ifdef gasnete_coll_exchange
  extern void
  gasnete_coll_exchange(gasnet_team_handle_t team,
                        void *dst, void *src,
                        size_t nbytes, int flags GASNETE_THREAD_FARG);
#else
  GASNET_INLINE_MODIFIER(gasnete_coll_exchange)
  void gasnete_coll_exchange(gasnet_team_handle_t team,
                             void *dst, void *src,
                             size_t nbytes, int flags GASNETE_THREAD_FARG) {
    gasnet_coll_handle_t handle;
    handle = gasnete_coll_exchange_nb(team,dst,src,nbytes,flags GASNETE_THREAD_PASS);
    gasnete_coll_wait_sync(handle GASNETE_THREAD_PASS);
  }
#endif
GASNET_INLINE_MODIFIER(_gasnet_coll_exchange)
void _gasnet_coll_exchange(gasnet_team_handle_t team,
                           void *dst, void *src,
                           size_t nbytes, int flags GASNETE_THREAD_FARG) {
  GASNETI_TRACE_COLL_EXCHANGE(COLL_EXCHANGE,team,dst,src,nbytes,flags);
  GASNETE_COLL_VALIDATE_EXCHANGE(team,dst,src,nbytes,flags);
  gasnete_coll_exchange(team,dst,src,nbytes,flags GASNETE_THREAD_PASS);
}
#define gasnet_coll_exchange(team,dst,src,nbytes,flags) \
       _gasnet_coll_exchange(team,dst,src,nbytes,flags GASNETE_THREAD_GET)

/*---------------------------------------------------------------------------------*/

extern gasnet_coll_handle_t
gasnete_coll_exchangeM_nb(gasnet_team_handle_t team,
                          void * const dstlist[], void * const srclist[],
                          size_t nbytes, int flags GASNETE_THREAD_FARG);
GASNET_INLINE_MODIFIER(_gasnet_coll_exchangeM_nb)
gasnet_coll_handle_t
_gasnet_coll_exchangeM_nb(gasnet_team_handle_t team,
                          void * const dstlist[], void * const srclist[],
                          size_t nbytes, int flags GASNETE_THREAD_FARG) {
  gasnet_coll_handle_t handle;
  GASNETI_TRACE_COLL_EXCHANGE_M(COLL_EXCHANGE_M_NB,team,dstlist,srclist,nbytes,flags);
  GASNETE_COLL_VALIDATE_EXCHANGE_M(team,dstlist,srclist,nbytes,flags);
  handle = gasnete_coll_exchangeM_nb(team,dstlist,srclist,nbytes,flags GASNETE_THREAD_PASS);
  gasneti_AMPoll(); gasnete_coll_poll(GASNETE_THREAD_PASS_ALONE);
  return handle;
}
#define gasnet_coll_exchangeM_nb(team,dstlist,srclist,nbytes,flags) \
       _gasnet_coll_exchangeM_nb(team,dstlist,srclist,nbytes,flags GASNETE_THREAD_GET)

#ifdef gasnete_coll_exchangeM
  extern void
  gasnete_coll_exchangeM(gasnet_team_handle_t team,
                         void * const dstlist[], void * const srclist[],
                         size_t nbytes, int flags GASNETE_THREAD_FARG);
#else
  GASNET_INLINE_MODIFIER(gasnete_coll_exchangeM)
  void gasnete_coll_exchangeM(gasnet_team_handle_t team,
                              void * const dstlist[], void * const srclist[],
                              size_t nbytes, int flags GASNETE_THREAD_FARG) {
    gasnet_coll_handle_t handle;
    handle = gasnete_coll_exchangeM_nb(team,dstlist,srclist,nbytes,flags GASNETE_THREAD_PASS);
    gasnete_coll_wait_sync(handle GASNETE_THREAD_PASS);
  }
#endif
GASNET_INLINE_MODIFIER(_gasnet_coll_exchangeM)
void _gasnet_coll_exchangeM(gasnet_team_handle_t team,
                            void * const dstlist[], void * const srclist[],
                            size_t nbytes, int flags GASNETE_THREAD_FARG) {
  GASNETI_TRACE_COLL_EXCHANGE_M(COLL_EXCHANGE_M,team,dstlist,srclist,nbytes,flags);
  GASNETE_COLL_VALIDATE_EXCHANGE_M(team,dstlist,srclist,nbytes,flags);
  gasnete_coll_exchangeM(team,dstlist,srclist,nbytes,flags GASNETE_THREAD_PASS);
}
#define gasnet_coll_exchangeM(team,dstlist,srclist,nbytes,flags) \
       _gasnet_coll_exchangeM(team,dstlist,srclist,nbytes,flags GASNETE_THREAD_GET)

/*---------------------------------------------------------------------------------*
 * Start of generic framework for reference implementations
 *---------------------------------------------------------------------------------*/

typedef struct {
    void *dst;
#if !GASNET_SEQ
    gasnet_image_t srcimage;
#endif
    gasnet_node_t srcnode;
    void *src;
    size_t nbytes;
} gasnete_coll_broadcast_args_t;

typedef struct  {
    void * const *dstlist;
#if !GASNET_SEQ
    gasnet_image_t srcimage;
#endif
    gasnet_node_t srcnode;
    void *src;
    size_t nbytes;
} gasnete_coll_broadcastM_args_t;

typedef struct {
    void *dst;
#if !GASNET_SEQ
    gasnet_image_t srcimage;
#endif
    gasnet_node_t srcnode;
    void *src;
    size_t nbytes;
} gasnete_coll_scatter_args_t;

typedef struct  {
    void * const *dstlist;
#if !GASNET_SEQ
    gasnet_image_t srcimage;
#endif
    gasnet_node_t srcnode;
    void *src;
    size_t nbytes;
} gasnete_coll_scatterM_args_t;

typedef struct {
#if !GASNET_SEQ
    gasnet_image_t dstimage;
#endif
    gasnet_node_t dstnode;
    void *dst;
    void *src;
    size_t nbytes;
} gasnete_coll_gather_args_t;

typedef struct  {
#if !GASNET_SEQ
    gasnet_image_t dstimage;
#endif
    gasnet_node_t dstnode;
    void *dst;
    void * const *srclist;
    size_t nbytes;
} gasnete_coll_gatherM_args_t;

typedef struct {
    void *dst;
    void *src;
    size_t nbytes;
} gasnete_coll_gather_all_args_t;

typedef struct  {
    void * const *dstlist;
    void * const *srclist;
    size_t nbytes;
} gasnete_coll_gather_allM_args_t;

typedef struct {
    void *dst;
    void *src;
    size_t nbytes;
} gasnete_coll_exchange_args_t;

typedef struct  {
    void * const *dstlist;
    void * const *srclist;
    size_t nbytes;
} gasnete_coll_exchangeM_args_t;

/* Options for gasnete_coll_generic_* */
#define GASNETE_COLL_GENERIC_OPT_INSYNC		0x0001
#define GASNETE_COLL_GENERIC_OPT_OUTSYNC	0x0002
#define GASNETE_COLL_GENERIC_OPT_P2P		0x0004

	/* Macros for conditionally setting flags in gasnete_coll_generic_* options */
#define GASNETE_COLL_GENERIC_OPT_INSYNC_IF(COND)	((COND) ? GASNETE_COLL_GENERIC_OPT_INSYNC : 0)
#define GASNETE_COLL_GENERIC_OPT_OUTSYNC_IF(COND)	((COND) ? GASNETE_COLL_GENERIC_OPT_OUTSYNC : 0)
#define GASNETE_COLL_GENERIC_OPT_P2P_IF(COND)		((COND) ? GASNETE_COLL_GENERIC_OPT_P2P : 0)

struct gasnete_coll_generic_data_t_ {
    #if GASNETI_USE_TRUE_MUTEXES || GASNET_DEBUG
      void				*owner;	/* gasnete_threaddata_t not yet defined */
    #endif
    #if GASNET_DEBUG
      #define GASNETE_COLL_GENERIC_TAG(T)	_CONCAT(GASNETE_COLL_GENERIC_TAG_,T)
      #define GASNETE_COLL_GENERIC_SET_TAG(D,T)	(D)->tag = GASNETE_COLL_GENERIC_TAG(T)

      enum {
	GASNETE_COLL_GENERIC_TAG(broadcast),
	GASNETE_COLL_GENERIC_TAG(broadcastM),
	GASNETE_COLL_GENERIC_TAG(scatter),
	GASNETE_COLL_GENERIC_TAG(scatterM),
	GASNETE_COLL_GENERIC_TAG(gather),
	GASNETE_COLL_GENERIC_TAG(gatherM),
	GASNETE_COLL_GENERIC_TAG(gather_all),
	GASNETE_COLL_GENERIC_TAG(gather_allM),
	GASNETE_COLL_GENERIC_TAG(exchange),
	GASNETE_COLL_GENERIC_TAG(exchangeM)
	/* XXX: still need a few more */

	/* Hook for conduit-specific extension */
	#ifdef GASNETE_COLL_GENERIC_TAG_EXTRA
	  , GASNETE_COLL_GENERIC_TAG_EXTRA
	#endif
      }					tag;

    #else
      #define GASNETE_COLL_GENERIC_SET_TAG(D,T)
    #endif

    int					state;
    int					options;
    gasnete_coll_consensus_t		in_barrier;
    gasnete_coll_consensus_t		out_barrier;
    gasnete_coll_p2p_t			*p2p;
    gasnet_handle_t			handle;
    void				*private_data;

    /* Hook for conduit-specific extension */
    #ifdef GASNETE_COLL_GENERIC_EXTRA
      GASNETE_COLL_GENERIC_EXTRA
    #endif

    union {
	gasnete_coll_broadcast_args_t		broadcast;
	gasnete_coll_broadcastM_args_t		broadcastM;
	gasnete_coll_scatter_args_t		scatter;
	gasnete_coll_scatterM_args_t		scatterM;
	gasnete_coll_gather_args_t		gather;
	gasnete_coll_gatherM_args_t		gatherM;
	gasnete_coll_gather_all_args_t		gather_all;
	gasnete_coll_gather_allM_args_t		gather_allM;
	gasnete_coll_exchange_args_t		exchange;
	gasnete_coll_exchangeM_args_t		exchangeM;
	/* XXX: still need a few more */

	/* Hook for conduit-specific extension */
	#ifdef GASNETE_COLL_GENERIC_ARGS_EXTRA
	  GASNETE_COLL_GENERIC_ARGS_EXTRA
	#endif
    }					args;
};

/* Extract pointer to correct member of args union
 * Also does some consistency checking when debugging is enabled
 */
#define GASNETE_COLL_GENERIC_ARGS(D,T) \
		(gasneti_assert((D) != NULL),                               \
		 gasneti_assert((D)->tag == GASNETE_COLL_GENERIC_TAG(T)),   \
		 &((D)->args.T))


extern gasnete_coll_generic_data_t *gasnete_coll_generic_alloc(GASNETE_THREAD_FARG_ALONE);
void gasnete_coll_generic_free(gasnete_coll_generic_data_t *data GASNETE_THREAD_FARG);
extern gasnet_coll_handle_t gasnete_coll_op_generic_init(gasnete_coll_team_t team, int flags,
							 gasnete_coll_generic_data_t *data,
							 gasnete_coll_poll_fn poll_fn
							 GASNETE_THREAD_FARG);
extern int gasnete_coll_generic_syncnb(gasnete_coll_generic_data_t *data GASNETE_THREAD_FARG);

GASNET_INLINE_MODIFIER(gasnete_coll_generic_insync)
int gasnete_coll_generic_insync(gasnete_coll_generic_data_t *data) {
  gasneti_assert(data != NULL);
  return (!(data->options & GASNETE_COLL_GENERIC_OPT_INSYNC) ||
	  (gasnete_coll_consensus_try(data->in_barrier) == GASNET_OK));
}

GASNET_INLINE_MODIFIER(gasnete_coll_generic_outsync)
int gasnete_coll_generic_outsync(gasnete_coll_generic_data_t *data) {
  gasneti_assert(data != NULL);
  return (!(data->options & GASNETE_COLL_GENERIC_OPT_OUTSYNC) ||
	  (gasnete_coll_consensus_try(data->out_barrier) == GASNET_OK));
}

extern int gasnete_coll_generic_coll_sync(gasnet_coll_handle_t *p, size_t count GASNETE_THREAD_FARG);


extern gasnet_coll_handle_t
gasnete_coll_generic_broadcast_nb(gasnet_team_handle_t team,
                                  void *dst,
                                  gasnet_image_t srcimage, void *src,
                                  size_t nbytes, int flags,
                                  gasnete_coll_poll_fn poll_fn, int options
                                  GASNETE_THREAD_FARG);

extern gasnet_coll_handle_t
gasnete_coll_generic_broadcastM_nb(gasnet_team_handle_t team,
                                   void * const dstlist[],
                                   gasnet_image_t srcimage, void *src,
                                   size_t nbytes, int flags,
                                   gasnete_coll_poll_fn poll_fn, int options
                                   GASNETE_THREAD_FARG);

extern gasnet_coll_handle_t
gasnete_coll_generic_scatter_nb(gasnet_team_handle_t team,
                                void *dst,
                                gasnet_image_t srcimage, void *src,
                                size_t nbytes, int flags,
                                gasnete_coll_poll_fn poll_fn, int options
                                GASNETE_THREAD_FARG);

extern gasnet_coll_handle_t
gasnete_coll_generic_scatterM_nb(gasnet_team_handle_t team,
                                 void * const dstlist[],
                                 gasnet_image_t srcimage, void *src,
                                 size_t nbytes, int flags,
                                 gasnete_coll_poll_fn poll_fn, int options
                                 GASNETE_THREAD_FARG);

extern gasnet_coll_handle_t
gasnete_coll_generic_gather_nb(gasnet_team_handle_t team,
                               gasnet_image_t dstimage, void *dst,
                               void *src,
                               size_t nbytes, int flags,
                               gasnete_coll_poll_fn poll_fn, int options
                               GASNETE_THREAD_FARG);

extern gasnet_coll_handle_t
gasnete_coll_generic_gatherM_nb(gasnet_team_handle_t team,
                                gasnet_image_t dstimage, void *dst,
                                void * const srclist[],
                                size_t nbytes, int flags,
                                gasnete_coll_poll_fn poll_fn, int options
                                GASNETE_THREAD_FARG);

extern gasnet_coll_handle_t
gasnete_coll_generic_gather_all_nb(gasnet_team_handle_t team,
                                   void *dst, void *src,
                                   size_t nbytes, int flags,
                                   gasnete_coll_poll_fn poll_fn, int options
                                   GASNETE_THREAD_FARG);

extern gasnet_coll_handle_t
gasnete_coll_generic_gather_allM_nb(gasnet_team_handle_t team,
                                    void * const dstlist[], void * const srclist[],
                                    size_t nbytes, int flags,
                                    gasnete_coll_poll_fn poll_fn, int options
                                    GASNETE_THREAD_FARG);

extern gasnet_coll_handle_t
gasnete_coll_generic_exchange_nb(gasnet_team_handle_t team,
                                 void *dst, void *src,
                                 size_t nbytes, int flags,
                                 gasnete_coll_poll_fn poll_fn, int options
                                 GASNETE_THREAD_FARG);

extern gasnet_coll_handle_t
gasnete_coll_generic_exchangeM_nb(gasnet_team_handle_t team,
                                  void * const dstlist[], void * const srclist[],
                                  size_t nbytes, int flags,
                                  gasnete_coll_poll_fn poll_fn, int options
                                  GASNETE_THREAD_FARG);

/*---------------------------------------------------------------------------------*
 * Start of protypes for reference implementations
 *---------------------------------------------------------------------------------*/

extern gasnet_coll_handle_t
gasnete_coll_bcast_Get(gasnet_team_handle_t team,
		       void *dst,
		       gasnet_image_t srcimage, void *src,
		       size_t nbytes, int flags GASNETE_THREAD_FARG);

extern gasnet_coll_handle_t
gasnete_coll_bcast_Put(gasnet_team_handle_t team,
		       void *dst,
		       gasnet_image_t srcimage, void *src,
		       size_t nbytes, int flags GASNETE_THREAD_FARG);

extern gasnet_coll_handle_t
gasnete_coll_bcast_Eager(gasnet_team_handle_t team,
			 void *dst,
			 gasnet_image_t srcimage, void *src,
			 size_t nbytes, int flags GASNETE_THREAD_FARG);

extern gasnet_coll_handle_t
gasnete_coll_bcast_RVGet(gasnet_team_handle_t team,
			 void *dst,
			 gasnet_image_t srcimage, void *src,
			 size_t nbytes, int flags GASNETE_THREAD_FARG);

/*---------------------------------------------------------------------------------*/

extern gasnet_coll_handle_t
gasnete_coll_bcastM_Get(gasnet_team_handle_t team,
			void * const dstlist[],
			gasnet_image_t srcimage, void *src,
			size_t nbytes, int flags GASNETE_THREAD_FARG);

extern gasnet_coll_handle_t
gasnete_coll_bcastM_Put(gasnet_team_handle_t team,
			void * const dstlist[],
			gasnet_image_t srcimage, void *src,
			size_t nbytes, int flags GASNETE_THREAD_FARG);

extern gasnet_coll_handle_t
gasnete_coll_bcastM_Eager(gasnet_team_handle_t team,
			  void * const dstlist[],
			  gasnet_image_t srcimage, void *src,
			  size_t nbytes, int flags GASNETE_THREAD_FARG);

extern gasnet_coll_handle_t
gasnete_coll_bcastM_RVGet(gasnet_team_handle_t team,
			  void * const dstlist[],
			  gasnet_image_t srcimage, void *src,
			  size_t nbytes, int flags GASNETE_THREAD_FARG);

/*---------------------------------------------------------------------------------*/

extern gasnet_coll_handle_t
gasnete_coll_scat_Get(gasnet_team_handle_t team,
		      void *dst,
		      gasnet_image_t srcimage, void *src,
		      size_t nbytes, int flags GASNETE_THREAD_FARG);

extern gasnet_coll_handle_t
gasnete_coll_scat_Put(gasnet_team_handle_t team,
		      void *dst,
		      gasnet_image_t srcimage, void *src,
		      size_t nbytes, int flags GASNETE_THREAD_FARG);

extern gasnet_coll_handle_t
gasnete_coll_scat_Eager(gasnet_team_handle_t team,
		        void *dst,
		        gasnet_image_t srcimage, void *src,
		        size_t nbytes, int flags GASNETE_THREAD_FARG);

extern gasnet_coll_handle_t
gasnete_coll_scat_RVGet(gasnet_team_handle_t team,
		        void *dst,
		        gasnet_image_t srcimage, void *src,
		        size_t nbytes, int flags GASNETE_THREAD_FARG);

/*---------------------------------------------------------------------------------*/

extern gasnet_coll_handle_t
gasnete_coll_scatM_Get(gasnet_team_handle_t team,
		       void * const dstlist[],
		       gasnet_image_t srcimage, void *src,
		       size_t nbytes, int flags GASNETE_THREAD_FARG);

extern gasnet_coll_handle_t
gasnete_coll_scatM_Put(gasnet_team_handle_t team,
		       void * const dstlist[],
		       gasnet_image_t srcimage, void *src,
		       size_t nbytes, int flags GASNETE_THREAD_FARG);

extern gasnet_coll_handle_t
gasnete_coll_scatM_Eager(gasnet_team_handle_t team,
		         void * const dstlist[],
		         gasnet_image_t srcimage, void *src,
		         size_t nbytes, int flags GASNETE_THREAD_FARG);

extern gasnet_coll_handle_t
gasnete_coll_scatM_RVGet(gasnet_team_handle_t team,
		         void * const dstlist[],
		         gasnet_image_t srcimage, void *src,
		         size_t nbytes, int flags GASNETE_THREAD_FARG);

/*---------------------------------------------------------------------------------*/

extern gasnet_coll_handle_t
gasnete_coll_gath_Get(gasnet_team_handle_t team,
		      gasnet_image_t dstimage, void *dst,
		      void *src,
		      size_t nbytes, int flags GASNETE_THREAD_FARG);

extern gasnet_coll_handle_t
gasnete_coll_gath_Put(gasnet_team_handle_t team,
		      gasnet_image_t dstimage, void *dst,
		      void *src,
		      size_t nbytes, int flags GASNETE_THREAD_FARG);

extern gasnet_coll_handle_t
gasnete_coll_gath_Eager(gasnet_team_handle_t team,
		        gasnet_image_t dstimage, void *dst,
		        void *src,
		        size_t nbytes, int flags GASNETE_THREAD_FARG);

extern gasnet_coll_handle_t
gasnete_coll_gath_RVPut(gasnet_team_handle_t team,
		        gasnet_image_t dstimage, void *dst,
		        void *src,
		        size_t nbytes, int flags GASNETE_THREAD_FARG);

/*---------------------------------------------------------------------------------*/

extern gasnet_coll_handle_t
gasnete_coll_gathM_Get(gasnet_team_handle_t team,
		       gasnet_image_t dstimage, void *dst,
		       void * const srclist[],
		       size_t nbytes, int flags GASNETE_THREAD_FARG);

extern gasnet_coll_handle_t
gasnete_coll_gathM_Put(gasnet_team_handle_t team,
		       gasnet_image_t dstimage, void *dst,
		       void * const srclist[],
		       size_t nbytes, int flags GASNETE_THREAD_FARG);

extern gasnet_coll_handle_t
gasnete_coll_gathM_Eager(gasnet_team_handle_t team,
		         gasnet_image_t dstimage, void *dst,
		         void * const srclist[],
		         size_t nbytes, int flags GASNETE_THREAD_FARG);

extern gasnet_coll_handle_t
gasnete_coll_gathM_RVPut(gasnet_team_handle_t team,
		         gasnet_image_t dstimage, void *dst,
		         void * const srclist[],
		         size_t nbytes, int flags GASNETE_THREAD_FARG);

/*---------------------------------------------------------------------------------*/

extern gasnet_coll_handle_t
gasnete_coll_gall_Gath(gasnet_team_handle_t team,
		       void *dst, void *src,
		       size_t nbytes, int flags GASNETE_THREAD_FARG);

/*---------------------------------------------------------------------------------*/

extern gasnet_coll_handle_t
gasnete_coll_gallM_Gath(gasnet_team_handle_t team,
			void * const dstlist[], void * const srclist[],
			size_t nbytes, int flags GASNETE_THREAD_FARG);

/*---------------------------------------------------------------------------------*/

extern gasnet_coll_handle_t
gasnete_coll_exchg_Gath(gasnet_team_handle_t team,
			void *dst, void *src,
			size_t nbytes, int flags GASNETE_THREAD_FARG);

/*---------------------------------------------------------------------------------*/

extern gasnet_coll_handle_t
gasnete_coll_exchgM_Gath(gasnet_team_handle_t team,
			 void * const dstlist[], void * const srclist[],
			 size_t nbytes, int flags GASNETE_THREAD_FARG);

/*---------------------------------------------------------------------------------*/

#endif

/*  $Archive:: /Ti/GASNet/extended-ref/gasnet_extended_refcoll.c $
 *     $Date: 2004/05/29 00:06:47 $
 * $Revision: 1.1.2.28 $
 * Description: Reference implemetation of GASNet Collectives
 * Copyright 2004, Dan Bonachea <bonachea@cs.berkeley.edu>
 * Terms of use are as specified in license.txt
 */

#ifndef GASNETI_GASNET_EXTENDED_COLL_C
  #error This file not meant to be compiled directly - included by gasnet_extended.c
#endif

/*---------------------------------------------------------------------------------*/
/* Forward decls and macros */

#define GASNETE_COLL_IN_MODE(flags) \
	((flags) & (GASNET_COLL_IN_NOSYNC  | GASNET_COLL_IN_MYSYNC  | GASNET_COLL_IN_ALLSYNC))
#define GASNETE_COLL_OUT_MODE(flags) \
	((flags) & (GASNET_COLL_OUT_NOSYNC | GASNET_COLL_OUT_MYSYNC | GASNET_COLL_OUT_ALLSYNC))
#define GASNETE_COLL_SYNC_MODE(flags) \
	((flags) & (GASNET_COLL_OUT_NOSYNC | GASNET_COLL_OUT_MYSYNC | GASNET_COLL_OUT_ALLSYNC | \
		    GASNET_COLL_IN_NOSYNC  | GASNET_COLL_IN_MYSYNC  | GASNET_COLL_IN_ALLSYNC))

#define GASNETE_COLL_OP_AM_VISIBLE	1

#define GASNETE_COLL_UNIMPLEMENTED() \
    (gasneti_fatalerror("%s unimplemented", GASNETI_CURRENT_FUNCTION), GASNET_COLL_INVALID_HANDLE)

static gasnete_coll_threaddata_t *gasnete_coll_new_threaddata(void);

/*---------------------------------------------------------------------------------*/

GASNET_INLINE_MODIFIER(gasnete_coll_get_threaddata)
gasnete_coll_threaddata_t *gasnete_coll_get_threaddata(gasnete_threaddata_t *thread) {
    gasnete_coll_threaddata_t *result = thread->gasnete_coll_threaddata;

    if_pf (result == NULL) {
	result = thread->gasnete_coll_threaddata = gasnete_coll_new_threaddata();
    }

    return result;
}

/*---------------------------------------------------------------------------------*/

int gasnete_coll_init_done = 0;
size_t *gasnete_coll_all_images;
size_t *gasnete_coll_all_offset;
size_t gasnete_coll_total_images;
size_t gasnete_coll_my_images;	/* count of local images */
size_t gasnete_coll_my_offset;	/* count of images before my first image */

#define GASNETE_COLL_1ST_IMAGE(LIST,NODE) \
	(((void * const *)(LIST))[gasnete_coll_all_offset[(NODE)]])
#define GASNETE_COLL_MY_1ST_IMAGE(LIST) \
	(((void * const *)(LIST))[gasnete_coll_my_offset])

void gasnete_coll_validate(gasnet_team_handle_t team,
			   gasnet_node_t dstnode, const void *dst, size_t dstlen, int dstisv,
			   gasnet_node_t srcnode, const void *src, size_t srclen, int srcisv,
			   unsigned int flags) {
  int i;

  if_pf (!gasnete_coll_init_done) {
    gasneti_fatalerror("Illegal call to GASNet collectives before gasnet_coll_init()\n");
  }

  /* XXX: temporary limitations: */
  gasneti_assert(flags & GASNET_COLL_DST_IN_SEGMENT);
  gasneti_assert(flags & GASNET_COLL_SRC_IN_SEGMENT);
  gasneti_assert(team == GASNET_TEAM_ALL);

  gasneti_assert(GASNETE_COLL_IN_MODE(flags) != 0);	/* IN mode has no default */
  gasneti_assert(GASNETE_COLL_OUT_MODE(flags) != 0);	/* OUT mode has no default */
  gasneti_assert(((flags & GASNET_COLL_SINGLE)?1:0) ^ ((flags & GASNET_COLL_LOCAL)?1:0));

  if (flags & GASNET_COLL_DST_IN_SEGMENT) {
    if (!dstisv) {
      gasnete_boundscheck(dstnode, dst, dstlen);
    } else if (flags & GASNET_COLL_SINGLE) {
      /* XXX: bounds check all images in list of (node,addr) tuples */
    } else /* (flags & GASNET_COLL_LOCAL) */ {
      /* XXX: bounds check local images in list of addrs */
    }
  }

  if (flags & GASNET_COLL_SRC_IN_SEGMENT) {
    if (!srcisv) {
      gasnete_boundscheck(srcnode, src, srclen);
    } else if (flags & GASNET_COLL_SINGLE) {
      /* XXX: bounds check all images in list of (node,addr) tuples */
    } else /* (flags & GASNET_COLL_LOCAL) */ {
      /* XXX: bounds check local images in list of addrs */
    }
  }

  /* XXX: TO DO
   * + check that team handle is valid (requires a teams interface)
   * + check that mynode is a member of the team (requires a teams interface)
   */
}

/*---------------------------------------------------------------------------------*/
/* Handles */

#ifndef GASNETE_COLL_HANDLE_OVERRIDE
  gasnet_hsl_t gasnete_coll_handle_lock = GASNET_HSL_INITIALIZER;
  gasnet_coll_handle_t gasnete_coll_handle_freelist = NULL;

  GASNET_INLINE_MODIFIER(gasnete_coll_hand_create)
  gasnet_coll_handle_t gasnete_coll_handle_create(void) {
    gasnet_coll_handle_t result;

    gasnet_hsl_lock(&gasnete_coll_handle_lock);
    if (gasnete_coll_handle_freelist) {
      result = gasnete_coll_handle_freelist;
      gasnete_coll_handle_freelist = (gasnet_coll_handle_t)(*result);
    } else {
      /* XXX: allocate in large chunks and scatter across cache lines */
      /* XXX: destroy freelist at exit */
      result = (gasnet_coll_handle_t)gasneti_malloc(sizeof(*result));
    }
    gasnet_hsl_unlock(&gasnete_coll_handle_lock);

    *result = 0;
    return result;
  }

  GASNET_INLINE_MODIFIER(gasnete_coll_hand_signal)
  void gasnete_coll_handle_signal(gasnet_coll_handle_t handle) {
    gasneti_assert(handle != GASNET_COLL_INVALID_HANDLE);
    *handle = 1;
  }

  extern int gasnete_coll_try_sync(gasnet_coll_handle_t handle) {
    int result = GASNET_ERR_NOT_READY;

    gasneti_assert(handle != GASNET_COLL_INVALID_HANDLE); /* caller must check */

    gasnete_coll_poll();

    if_pf (*handle != 0) {
      gasnet_hsl_lock(&gasnete_coll_handle_lock);
      *handle = (uintptr_t)gasnete_coll_handle_freelist;
      gasnete_coll_handle_freelist = handle;
      gasnet_hsl_unlock(&gasnete_coll_handle_lock);
      result = GASNET_OK;
    }

    return result;
  }
#endif

/*---------------------------------------------------------------------------------*/
/* Collective teams */

/* XXX: Teams are not yet fully designed
 *
 * Likely interface:
 *
 *  void gasnete_coll_team_ins(op)
 *	Add a team to the table
 *  void gasnete_coll_team_del(op)
 *	Remove a team from the table
 *  gasnete_coll_team_t gasnete_coll_team_find(team_id)
 *	Lookup a team by its 32-bit id, returning NULL if not found.
 *
 * Serialization done inside the implementation
 */

#ifndef GASNETE_COLL_TEAMS_OVERRIDE
    /* Called by by AM handlers to lookup the team by id */
    gasnete_coll_team_t gasnete_coll_team_lookup(uint32_t team_id) {
	/* XXX: no implementation of teams yet */
	if (team_id != 0) {
	    gasneti_fatalerror("Non-zero team id passed, but teams are not yet implemented.");
	}
	return GASNET_TEAM_ALL;
    }

    gasnet_node_t gasnete_coll_team_rank2node(gasnete_coll_team_t team, int rank) {
	gasneti_assert(team == NULL);
	return (gasnet_node_t)rank;
    }

    int gasnete_coll_team_node2rank(gasnete_coll_team_t team, gasnet_node_t node) {
	gasneti_assert(team == NULL);
	return (int)node;
    }

    uint32_t gasnete_coll_team_id(gasnete_coll_team_t team) {
	gasneti_assert(team == NULL);
	return 0;
    }
#endif

/*---------------------------------------------------------------------------------*/
/* XXX: sequence (and maybe other stuff) will need to be per-team scoped. */
uint32_t gasnete_coll_sequence = 12345;	/* arbitrary non-zero starting value */

gasnet_hsl_t gasnete_coll_table_lock = GASNET_HSL_INITIALIZER;

#if 0	/* XXX: REPLACEMENT IN PROGRESS */
#ifndef GASNETE_COLL_TABLE_OVERRIDE
    /* Default implementation of the coll_ops lookup table:
     *
     * Lookups are based on a fixed size table with slots used round-robin.
     * Conflicts are resolved by chaining with a circular doubly linked list.
     * A minor change would mix the team pointer with the sequence number.
     * An alternative to that mixing would be separate tables in each team
     * data structure.
     */

    #ifndef GASNETE_COLL_TABLE_SIZE
      #define GASNETE_COLL_TABLE_SIZE 16
    #endif
    #if 0
      /* This is one possible implementation when we have teams */
      #define GASNETE_COLL_TABLE_SLOT(T,S) \
	 (((uint32_t)(uintptr_t)(T) ^ (uint32_t)(S)) % GASNETE_COLL_TABLE_SIZE)
    #else
      /* Use this mapping until teams are implemented */
      #define GASNETE_COLL_TABLE_SLOT(T,S) \
	 (gasneti_assert(T==NULL), ((uint32_t)(S) % GASNETE_COLL_TABLE_SIZE))
    #endif

    static gasnete_coll_op_t gasnete_coll_table[GASNETE_COLL_TABLE_SIZE];

    gasnete_coll_op_t *
    gasnete_coll_op_table_find(gasnete_coll_team_t team, uint32_t sequence) {
      unsigned int slot_nr = GASNETE_COLL_TABLE_SLOT(team, sequence);
      const gasnete_coll_op_t *head = &(gasnete_coll_table[slot_nr]);
      gasnete_coll_op_t *op = NULL;

      /* Search table */
      op = head->table_next;
      while ((op != head) && ((op->team != team) || (op->sequence != sequence))) {
	op = op->table_next;
      }

      return (op == head) ? NULL : op;
    }

    void gasnete_coll_op_table_new(gasnete_coll_op_t *op) {
      op->table_next = op->table_prev = op;
    }

    void gasnete_coll_op_table_ins(gasnete_coll_op_t *op) {
      unsigned int slot_nr = GASNETE_COLL_TABLE_SLOT(op->team, op->sequence);
      gasnete_coll_op_t *head = &(gasnete_coll_table[slot_nr]);

      /* Add to circular doubly linked hash bucket */
      op->table_next = head;
      op->table_prev = head->table_prev;
      head->table_prev->table_next = op;
      head->table_prev = op;
    }

    void gasnete_coll_op_table_del(gasnete_coll_op_t *op) {
      /* Remove from cirular doubly linked hash bucket */
      op->table_next->table_prev = op->table_prev;
      op->table_prev->table_next = op->table_next;
    }

    void
    gasnete_coll_op_table_init(void) {
      int i;
      for (i = 0; i < GASNETE_COLL_TABLE_SIZE; ++i) {
	gasnete_coll_op_table_new(&(gasnete_coll_table[i]));
      }
    }

    void
    gasnete_coll_op_table_fini(void) {
      /* EMPTY */
    }
#endif
#endif

/*---------------------------------------------------------------------------------*/
/* The list of active collective ops (coll ops) */

/* There exists a per-thread "active list".
 * Ops in the active table will be polled to make progress.
 *
 * Operations of the active list
 *   void gasnete_coll_active_init(td)
 *   void gasnete_coll_active_fini(td)
 *   gasnete_coll_op_t *gasnete_coll_active_first(td)
 *	Return the first coll op in the active list.
 *   gasnete_coll_op_t *gasnete_coll_active_next(op)
 *	Iterate over the coll ops in the active list.
 *   void gasnete_coll_active_new(op)
 *	Init active list fields of a coll op.
 *   void gasnete_coll_active_ins(op)
 *	Add a coll op to the active list.
 *   void gasnete_coll_active_del(op)
 *	Delete a coll op from the active list.
 *
 */

#ifndef GASNETE_COLL_LIST_OVERRIDE
    /* Default implementation of coll_ops active list:
     *
     * Iteration over the active list is based on a linked list (queue).
     * Iteration starts from the head and new ops are added at the tail.
     *
     * XXX: use list macros?
     */

    gasnete_coll_op_t *gasnete_coll_active_first(gasnete_coll_threaddata_t *td) {
      return td->active_head;
    }

    gasnete_coll_op_t *gasnete_coll_active_next(gasnete_coll_op_t *op) {
      return op->active_next;
    }

    void gasnete_coll_active_new(gasnete_coll_op_t *op) {
      op->active_next = NULL;
      op->active_prev_p = &(op->active_next);
    }

    void gasnete_coll_active_ins(gasnete_coll_op_t *op) {
      gasnete_coll_threaddata_t *td = op->threaddata;
      *(td->active_tail_p) = op;
      op->active_prev_p = td->active_tail_p;
      td->active_tail_p = &(op->active_next);
    }

    void gasnete_coll_active_del(gasnete_coll_op_t *op) {
      gasnete_coll_op_t *next = op->active_next;
      *(op->active_prev_p) = next;
      if (next) {
	next->active_prev_p = op->active_prev_p;
      } else {
	op->threaddata->active_tail_p = op->active_prev_p;
      }
    }

    void
    gasnete_coll_active_init(gasnete_coll_threaddata_t *td) {
      td->active_head = NULL;
      td->active_tail_p = &(td->active_head);
    }

    void
    gasnete_coll_active_fini(gasnete_coll_threaddata_t *td) {
      gasneti_assert(td->active_head == NULL);
    }
#endif

static gasnete_coll_threaddata_t *gasnete_coll_new_threaddata(void) {
    gasnete_coll_threaddata_t *result = gasneti_calloc(1,sizeof(*result));
    gasnete_coll_active_init(result);
    return result;
}

/*---------------------------------------------------------------------------------*/
/* Aggregation/filtering */

/* interface:
 *   gasnet_coll_handle_t gasnete_coll_op_submit(op, handle)
 *	Place coll_op in active list or not, as desired/required.
 *   void gasnete_coll_op_complete(op, poll_result);
 *	Completion hook
 *
 *  Both are called with the table lock held.
 */

#ifndef GASNETE_COLL_AGG_OVERRIDE
    /* Default implementation of aggregation/filtering */

    /* XXX: how will teams interact w/ aggregation? */

    static gasnete_coll_op_t *gasnete_coll_agg = NULL;

    gasnet_coll_handle_t
    gasnete_coll_op_submit(gasnete_coll_op_t *op, gasnet_coll_handle_t handle) {
      int poll_result;

      op->agg_head = NULL;
      op->handle = handle;

      if_pf (op->flags & GASNET_COLL_AGGREGATE) {
	gasnete_coll_op_t *head = gasnete_coll_agg;

	gasneti_assert(handle == GASNET_COLL_INVALID_HANDLE);	/* check for handle leak */

	if (head == NULL) {
	  /* Build a container to hold the aggregate.
	   * The team, sequence and flags don't matter.
	   */
	  head = gasnete_coll_agg = gasnete_coll_op_create(op->team, 0, 0, op->threaddata);
	  head->agg_next = head->agg_prev = head;
	}

	/* Aggregate members go in a circular list */
	op->agg_next = head;
	op->agg_prev = head->agg_prev;
	head->agg_prev->agg_next = op;
	head->agg_prev = op;

	/* We don't set the agg_head yet.
	 * If the aggregation list becomes empty now it is
	 * only temporary and should not signal 'done'.
	 */
      } else if_pf (gasnete_coll_agg) {
	gasnete_coll_op_t *tmp;

	/* End of aggregate, place final op in the list */
	tmp = gasnete_coll_agg;
	op->agg_next = tmp;
	op->agg_prev = tmp->agg_prev;
	tmp->agg_prev->agg_next = op;
	tmp->agg_prev = op;

	/* Set all of the agg_head fields so we can signal
	 * the container op when the list becomes empty.
	 */
	gasneti_assert(tmp == gasnete_coll_agg);
	tmp = tmp->agg_next;
	do {
	   tmp->agg_head = gasnete_coll_agg;
	   tmp = tmp->agg_next;
	} while (tmp != gasnete_coll_agg);

	/* Return the container in place of the ops */
	gasneti_assert(tmp == gasnete_coll_agg);
	gasnete_coll_agg = NULL;
	tmp->handle = op->handle;
	op->handle = GASNET_COLL_INVALID_HANDLE;
      } else {
	/* An isolated coll_op (the normal case) */
	op->agg_next = NULL;
      }

      /* All ops go onto the active list */
      gasnete_coll_active_ins(op);

      return handle;
    }

    void gasnete_coll_op_complete(gasnete_coll_op_t *op, int poll_result) {

      if (poll_result & GASNETE_COLL_OP_COMPLETE) {
	if_pt (op->handle != GASNET_COLL_INVALID_HANDLE) {
	    /* Normal case, just signal the handle */
	    gasnete_coll_handle_signal(op->handle);
	    gasneti_assert(op->agg_head == NULL);
	} else if (op->agg_next) {
	  gasnete_coll_op_t *head;

	  /* Remove this member from the aggregate */
	  op->agg_next->agg_prev = op->agg_prev;
	  op->agg_prev->agg_next = op->agg_next;

	  /* If the container op exists and is now empty, mark it's handle as done. */
	  head = op->agg_head;
	  if (head && (head->agg_next == head)) {
	    gasnete_coll_handle_signal(head->handle);
	    gasnete_coll_op_destroy(head);
	  }
	}
      }

      if (poll_result & GASNETE_COLL_OP_INACTIVE) {
	/* delete from the active list and destoy */
	gasnete_coll_active_del(op);
	gasnete_coll_op_destroy(op);
      }
    }
#endif

/*---------------------------------------------------------------------------------*/
gasnete_coll_op_t *
gasnete_coll_op_create(gasnete_coll_team_t team, uint32_t sequence, unsigned int flags, gasnete_coll_threaddata_t *td) {
  gasnete_coll_op_t *op;

  op = td->op_freelist;
  if_pt (op != NULL) {
    td->op_freelist = *((gasnete_coll_op_t **)op);
  } else {
    /* XXX: allocate in chunks and scatter across cache lines */
    /* XXX: destroy freelist at exit */
    op = (gasnete_coll_op_t *)gasneti_malloc(sizeof(gasnete_coll_op_t));
  }
  op->threaddata = td;

  gasnete_coll_active_new(op);
  op->team     = team;
  op->sequence = sequence;
  op->flags    = flags;
  op->handle   = GASNET_COLL_INVALID_HANDLE;
  gasnet_hsl_init(&op->lock);
  op->poll_fn  = (gasnete_coll_poll_fn)NULL;

  /* The aggregation and 'data' fields are setup elsewhere */

  return op;
}

void
gasnete_coll_op_destroy(gasnete_coll_op_t *op) {
  gasnete_coll_threaddata_t *td = op->threaddata;
  *((gasnete_coll_op_t **)op) =  td->op_freelist;
  td->op_freelist = op;
}

void gasnete_coll_poll(void) {
  gasnete_coll_threaddata_t *td = gasnete_coll_get_threaddata(gasnete_mythread());	/* XXX */
  gasnete_coll_op_t *op;

  gasnet_AMPoll();	/* XXX: Do in caller? */

  op = gasnete_coll_active_first(td);

  while (op != NULL) {
    gasnete_coll_op_t *next = gasnete_coll_active_next(op);
    int poll_result = 0;

    /* Poll/kick the op */
    gasneti_assert(op->poll_fn != (gasnete_coll_poll_fn)NULL);
    poll_result = (*op->poll_fn)(op);
    if (poll_result != 0) {
      gasnete_coll_op_complete(op, poll_result);
    }

    /* Next... */
    op = next;
  }
}

extern void gasnete_coll_init(const size_t images[],
			      gasnet_coll_fn_entry_t fn_tbl[], size_t fn_count,
			      int init_flags) {
  size_t image_size = gasnete_nodes * sizeof(size_t);
  int i;

  GASNETI_CHECKATTACH();

  /* Sanity checks - performed only for debug builds */
  #if GASNET_DEBUG
    if (gasnete_coll_init_done) {
      gasneti_fatalerror("Multiple calls to gasnet_coll_init()\n");
    }
    if (init_flags) {
      gasneti_fatalerror("Invalid call to gasnet_coll_init() with non-zero flags\n");
    }
  #endif

  gasnete_coll_p2p_init();

  gasnete_coll_all_images = gasneti_malloc(image_size);
  gasnete_coll_all_offset = gasneti_malloc(image_size);
  if (images != NULL) {
    memcpy(gasnete_coll_all_images, images, image_size);
  } else  {
    for (i = 0; i < gasnete_nodes; ++i) {
      gasnete_coll_all_images[i] = 1;
    }
  }
  gasnete_coll_total_images = 0;
  for (i = 0; i < gasnete_nodes; ++i) {
    gasnete_coll_all_offset[i] = gasnete_coll_total_images;
    gasnete_coll_total_images += gasnete_coll_all_images[i];
  }
  gasnete_coll_my_images = gasnete_coll_all_images[gasnete_mynode];
  gasnete_coll_my_offset = gasnete_coll_all_offset[gasnete_mynode];

  if (fn_count != 0) {
    /* XXX: */
    gasneti_fatalerror("gasnet_coll_init: function registration is not yet supported");
  }

  gasnete_coll_init_done = 1;
  gasnet_barrier_notify((int)gasnete_coll_sequence,0);
  gasnet_barrier_wait((int)gasnete_coll_sequence,0);
}

/*---------------------------------------------------------------------------------*/
/* Synchronization primitives */

#ifndef GASNETE_COLL_CONSENSUS_OVERRIDE
    /* Scalar type, could be a pointer to a struct */
    typedef uint32_t gasnete_coll_consensus_t;

    static uint32_t gasnete_coll_issued_id = 0;
    static uint32_t gasnete_coll_consensus_id = 0;

    gasnete_coll_consensus_t gasnete_coll_consensus_create(void) {
      return gasnete_coll_issued_id++;
    }

    int gasnete_coll_consensus_try(gasnete_coll_consensus_t id) {
      uint32_t tmp = id << 1;	/* low bit is used for barrier phase (notify vs wait) */
#if GASNET_DEBUG
      const int barrier_flags = 0;
#else
      const int barrier_flags = GASNET_BARRIERFLAG_ANONYMOUS;
#endif

      if (tmp == gasnete_coll_consensus_id) {
	/* Exact match, so we notify and advance */
	++gasnete_coll_consensus_id;
	gasnet_barrier_notify(gasnete_coll_consensus_id, barrier_flags);
      }

      if (gasnete_coll_consensus_id & 1) {
	/* At a wait stage, so try the barrier */
	int rc = gasnet_barrier_try(gasnete_coll_consensus_id, barrier_flags);
	if (rc == GASNET_OK) {
	  /* A barrier is complete, advance */
	  ++gasnete_coll_consensus_id;
	}
#if GASNET_DEBUG
	else if (rc == GASNET_ERR_BARRIER_MISMATCH) {
	  gasneti_fatalerror("Named barrier mismatch detected in collectives");
	} else {
	  gasneti_assert(rc == GASNET_ERR_NOT_READY);
	}
#endif
      }

      /* Note that we need to be careful of wrapping, thus the (int32_t)(a-b) construct
       * must be used in place of simply (a-b).
       */
      return ((int32_t)(gasnete_coll_consensus_id - tmp) > 1) ? GASNET_OK
							      : GASNET_ERR_NOT_READY;
    }
#endif

#ifndef GASNETE_COLL_P2P_OVERRIDE
    #ifndef GASNETE_COLL_P2P_TABLE_SIZE
      #define GASNETE_COLL_P2P_TABLE_SIZE 16
    #endif
    #if 0
      /* This is one possible implementation when we have teams */
      #define GASNETE_COLL_P2P_TABLE_SLOT(T,S) \
	 (((uint32_t)(uintptr_t)(T) ^ (uint32_t)(S)) % GASNETE_COLL_P2P_TABLE_SIZE)
    #else
      /* Use this mapping until teams are implemented */
      #define GASNETE_COLL_P2P_TABLE_SLOT(T,S) \
	 (gasneti_assert(gasnete_coll_team_lookup(T)==NULL), ((uint32_t)(S) % GASNETE_COLL_P2P_TABLE_SIZE))
    #endif

    static gasnete_coll_p2p_t gasnete_coll_p2p_table[GASNETE_COLL_P2P_TABLE_SIZE];
    static gasnet_hsl_t gasnete_coll_p2p_table_lock = GASNET_HSL_INITIALIZER;

    void gasnete_coll_p2p_init() {
      int i;

      for (i = 0; i < GASNETE_COLL_P2P_TABLE_SIZE; ++i) {
	gasnete_coll_p2p_t *tmp = &(gasnete_coll_p2p_table[i]);
	tmp->p2p_next = tmp->p2p_prev = tmp;
      }
    }

    void gasnete_coll_p2p_fini() {
      int i;

      for (i = 0; i < GASNETE_COLL_P2P_TABLE_SIZE; ++i) {
	gasnete_coll_p2p_t *tmp = &(gasnete_coll_p2p_table[i]);
	/* Check that table is actually empty */
	gasneti_assert(tmp->p2p_next == tmp);
	gasneti_assert(tmp->p2p_prev == tmp);
      }
    }

    gasnete_coll_p2p_t *gasnete_coll_p2p_get(uint32_t team_id, uint32_t sequence) {
      unsigned int slot_nr = GASNETE_COLL_P2P_TABLE_SLOT(team_id, sequence);
      gasnete_coll_p2p_t *head = &(gasnete_coll_p2p_table[slot_nr]);
      gasnete_coll_p2p_t *p2p;

      gasneti_assert(gasnete_coll_team_lookup(team_id) == GASNET_TEAM_ALL);

      gasnet_hsl_lock(&gasnete_coll_p2p_table_lock);

      /* Search table */
      p2p = head->p2p_next;
      while ((p2p != head) && ((p2p->team_id != team_id) || (p2p->sequence != sequence))) {
	p2p = p2p->p2p_next;
      }

      /* If not found, create it with all zeros */
      if_pf (p2p == head) {
	size_t alloc_size;
	char *buf;

	/* XXX: use a (per-team?) free list since size is fixed */

	/* Round to 8-byte alignment of state array */
	alloc_size = ((sizeof(gasnete_coll_p2p_t) + 7) & ~7)
					+ gasnete_coll_total_images * sizeof(gasnete_coll_p2p_entry_t);
	buf = gasneti_calloc(1, alloc_size);
	p2p = (gasnete_coll_p2p_t *)buf;
	p2p->entry = (gasnete_coll_p2p_entry_t *)(buf + ((sizeof(gasnete_coll_p2p_t) + 7) & ~7));

	p2p->team_id = team_id;
	p2p->sequence = sequence;

	p2p->p2p_prev = head;
	p2p->p2p_next = head->p2p_next;
	head->p2p_next->p2p_prev = p2p;
	head->p2p_next = p2p;
      }

      gasnet_hsl_unlock(&gasnete_coll_p2p_table_lock);

      gasneti_assert(p2p != NULL);
      gasneti_assert(p2p->entry != NULL);

      return p2p;
    }

    void gasnete_coll_p2p_destroy(gasnete_coll_p2p_t *p2p) {
      gasnet_hsl_lock(&gasnete_coll_p2p_table_lock);
      p2p->p2p_prev->p2p_next = p2p->p2p_next;
      p2p->p2p_next->p2p_prev = p2p->p2p_prev;
      gasnet_hsl_unlock(&gasnete_coll_p2p_table_lock);

      /* XXX: use per-team free list */
      gasneti_free(p2p);
    }

    static void gasnete_coll_p2p_put_reqh(gasnet_token_t token, void *buf, size_t nbytes,
					  gasnet_handlerarg_t team_id,
					  gasnet_handlerarg_t sequence,
					  gasnet_handlerarg_t pos,
					  gasnet_handlerarg_t state) {
      gasnete_coll_p2p_t *p2p = gasnete_coll_p2p_get(team_id, sequence);
      gasnete_coll_p2p_entry_t *entry = &(p2p->entry[pos]);

      entry->state = state;
    }

    static void gasnete_coll_p2p_eager_reqh(gasnet_token_t token, void *buf, size_t nbytes,
					    gasnet_handlerarg_t team_id,
					    gasnet_handlerarg_t sequence,
					    gasnet_handlerarg_t pos,
					    gasnet_handlerarg_t state) {
      gasnete_coll_p2p_t *p2p = gasnete_coll_p2p_get(team_id, sequence);
      gasnete_coll_p2p_entry_t *entry = &(p2p->entry[pos]);

      if (nbytes) {
	gasneti_assert(nbytes <= GASNETE_COLL_P2P_EAGER_LIMIT);
	GASNETE_FAST_UNALIGNED_MEMCPY(entry->data, buf, nbytes);
	gasneti_memsync();
      }

      entry->state = state;
    }

    GASNET_INLINE_MODIFIER(gasnete_coll_p2p_addr_reqh_inner)
    void gasnete_coll_p2p_addr_reqh_inner(gasnet_token_t token,
					  gasnet_handlerarg_t team_id,
					  gasnet_handlerarg_t sequence,
					  gasnet_handlerarg_t pos,
					  gasnet_handlerarg_t state,
					  void *addr) {
      gasnete_coll_p2p_t *p2p = gasnete_coll_p2p_get(team_id, sequence);
      gasnete_coll_p2p_entry_t *entry = &(p2p->entry[pos]);

      *((void **)(entry->data)) = addr;
      gasneti_memsync();

      entry->state = state;
    }
    SHORT_HANDLER(gasnete_coll_p2p_addr_reqh,5,6,
		  (token, a0, a1, a2, a3, UNPACK (a4)    ),
		  (token, a0, a1, a2, a3, UNPACK2(a4, a5)));

    #define _hidx_gasnete_coll_p2p_put_reqh	125	/* XXX: kludge!!! */
    #define _hidx_gasnete_coll_p2p_eager_reqh	126	/* XXX: kludge!!! */
    #define _hidx_gasnete_coll_p2p_addr_reqh	127	/* XXX: kludge!!! */
    #define GASNETE_COLL_P2P_HANDLERS              \
	gasneti_handler_tableentry_no_bits(gasnete_coll_p2p_put_reqh),   \
	gasneti_handler_tableentry_no_bits(gasnete_coll_p2p_eager_reqh), \
	gasneti_handler_tableentry_with_bits(gasnete_coll_p2p_addr_reqh)

    /* Put up to gasnet_AMMaxLongRequest() bytes, signalling the recipient */
    void gasnet_coll_p2p_signalling_put(gasnete_coll_op_t *op, gasnet_node_t dstnode, void *dst,
					void *src, size_t nbytes, uint32_t pos, uint32_t state) {
      uint32_t team_id = gasnete_coll_team_id(op->team);

      gasneti_assert(nbytes <= gasnet_AMMaxLongRequest());

      GASNETE_SAFE(
	LONG_REQ(4,4,(dstnode, gasneti_handleridx(gasnete_coll_p2p_put_reqh),
		      src, nbytes, dst, team_id, op->sequence, pos, state)));
    }

    /* Send up to GASNETE_COLL_P2P_EAGER_LIMIT bytes to be buffered at the recipient */
    void gasnet_coll_p2p_eager_put(gasnete_coll_op_t *op, gasnet_node_t dstnode,
				   void *src, size_t nbytes, uint32_t pos, uint32_t state) {
      uint32_t team_id = gasnete_coll_team_id(op->team);

      gasneti_assert(nbytes <= GASNETE_COLL_P2P_EAGER_LIMIT);

      GASNETE_SAFE(
	MEDIUM_REQ(4,4,(dstnode, gasneti_handleridx(gasnete_coll_p2p_eager_reqh),
			src, nbytes, team_id, op->sequence, pos, state)));
    }

    /* Send a single address to be buffered at the recipient */
    void gasnet_coll_p2p_rendezvous(gasnete_coll_op_t *op, gasnet_node_t dstnode,
				    void *addr, uint32_t pos, uint32_t state) {
      uint32_t team_id = gasnete_coll_team_id(op->team);

      GASNETE_SAFE(
	SHORT_REQ(5,6,(dstnode, gasneti_handleridx(gasnete_coll_p2p_addr_reqh),
		       team_id, op->sequence, pos, state, PACK(addr))));
    }
#endif

/*---------------------------------------------------------------------------------*/
/* Types and functions for generic ops */

typedef struct {
    void *dst;
    gasnet_node_t srcnode;
    void *src;
    size_t nbytes;
} gasnete_coll_broadcast_args_t;

typedef struct  {
    void * const *dstlist;
    gasnet_node_t srcnode;
    void *src;
    size_t nbytes;
} gasnete_coll_broadcastM_args_t;

typedef gasnete_coll_broadcast_args_t gasnete_coll_scatter_args_t;
typedef gasnete_coll_broadcastM_args_t gasnete_coll_scatterM_args_t;

typedef struct {
    gasnet_node_t dstnode;
    void *dst;
    void *src;
    size_t nbytes;
} gasnete_coll_gather_args_t;

typedef struct  {
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

typedef gasnete_coll_gather_all_args_t gasnete_coll_exchange_args_t;
typedef gasnete_coll_gather_allM_args_t gasnete_coll_exchangeM_args_t;

struct gasnete_coll_generic_sync {
    int				enable;
    gasnete_coll_consensus_t	barrier;
};

typedef struct {
    int					state;
    struct gasnete_coll_generic_sync	in;
    struct gasnete_coll_generic_sync	out;
    gasnet_handle_t			handle;
    gasnete_coll_p2p_t			*p2p;
    void				*private;
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
    }					args;

    gasnete_coll_threaddata_t		*threaddata;
} gasnete_coll_generic_data_t;

GASNET_INLINE_MODIFIER(gasnete_coll_generic_alloc)
gasnete_coll_generic_data_t *gasnete_coll_generic_alloc(gasnete_coll_threaddata_t *td) {
    gasnete_coll_generic_data_t *result;

    gasneti_assert(td != NULL);

    result = td->generic_data_freelist;
    if_pt (result != NULL) {
	td->generic_data_freelist = *((gasnete_coll_generic_data_t **)result);
    } else {
	/* XXX: allocate in chunks and scatter across cache lines */
	/* XXX: destroy freelist at exit */
	result = (gasnete_coll_generic_data_t *)gasneti_malloc(sizeof(gasnete_coll_generic_data_t));
    }

    memset(result, 0, sizeof(*result));
    result->threaddata = td;

    return result;
}

GASNET_INLINE_MODIFIER(gasnete_coll_generic_free)
void gasnete_coll_generic_free(gasnete_coll_generic_data_t *data) {
    gasnete_coll_threaddata_t *td;

    gasneti_assert(data != NULL);
    gasneti_assert(data->threaddata != NULL);

    td = data->threaddata;
    *((gasnete_coll_generic_data_t **)data) =  td->generic_data_freelist;
    td->generic_data_freelist = data;
}

GASNET_INLINE_MODIFIER(gasnete_coll_generic_broadcast)
gasnete_coll_generic_data_t *
gasnete_coll_generic_broadcast(void *dst,
			       gasnet_node_t srcnode, void *src,
			       size_t nbytes,
			       gasnete_coll_threaddata_t *td) {
    gasnete_coll_generic_data_t *result = gasnete_coll_generic_alloc(td);
    result->args.broadcast.dst     = dst;
    result->args.broadcast.srcnode = srcnode;
    result->args.broadcast.src     = src;
    result->args.broadcast.nbytes  = nbytes;
    return result;
}

GASNET_INLINE_MODIFIER(gasnete_coll_generic_broadcastM)
gasnete_coll_generic_data_t *
gasnete_coll_generic_broadcastM(void * const dstlist[],
				gasnet_node_t srcnode, void *src,
				size_t nbytes,
				gasnete_coll_threaddata_t *td) {
    gasnete_coll_generic_data_t *result = gasnete_coll_generic_alloc(td);
    result->args.broadcastM.dstlist = dstlist;
    result->args.broadcastM.srcnode = srcnode;
    result->args.broadcastM.src     = src;
    result->args.broadcastM.nbytes  = nbytes;
    return result;
}

GASNET_INLINE_MODIFIER(gasnete_coll_generic_scatter)
gasnete_coll_generic_data_t *
gasnete_coll_generic_scatter(void *dst,
			     gasnet_node_t srcnode, void *src,
			     size_t nbytes,
			     gasnete_coll_threaddata_t *td) {
    gasnete_coll_generic_data_t *result = gasnete_coll_generic_alloc(td);
    result->args.scatter.dst     = dst;
    result->args.scatter.srcnode = srcnode;
    result->args.scatter.src     = src;
    result->args.scatter.nbytes  = nbytes;
    return result;
}

GASNET_INLINE_MODIFIER(gasnete_coll_generic_scatterM)
gasnete_coll_generic_data_t *
gasnete_coll_generic_scatterM(void * const dstlist[],
			      gasnet_node_t srcnode, void *src,
			      size_t nbytes,
			      gasnete_coll_threaddata_t *td) {
    gasnete_coll_generic_data_t *result = gasnete_coll_generic_alloc(td);
    result->args.scatterM.dstlist = dstlist;
    result->args.scatterM.srcnode = srcnode;
    result->args.scatterM.src     = src;
    result->args.scatterM.nbytes  = nbytes;
    return result;
}

GASNET_INLINE_MODIFIER(gasnete_coll_generic_gather)
gasnete_coll_generic_data_t *
gasnete_coll_generic_gather(gasnet_node_t dstnode, void *dst,
			    void *src,
			    size_t nbytes,
			    gasnete_coll_threaddata_t *td) {
    gasnete_coll_generic_data_t *result = gasnete_coll_generic_alloc(td);
    result->args.gather.dstnode = dstnode;
    result->args.gather.dst     = dst;
    result->args.gather.src     = src;
    result->args.gather.nbytes  = nbytes;
    return result;
}

GASNET_INLINE_MODIFIER(gasnete_coll_generic_gatherM)
gasnete_coll_generic_data_t *
gasnete_coll_generic_gatherM(gasnet_node_t dstnode, void *dst,
			     void * const srclist[],
			     size_t nbytes,
			     gasnete_coll_threaddata_t *td) {
    gasnete_coll_generic_data_t *result = gasnete_coll_generic_alloc(td);
    result->args.gatherM.dstnode = dstnode;
    result->args.gatherM.dst     = dst;
    result->args.gatherM.srclist = srclist;
    result->args.gatherM.nbytes  = nbytes;
    return result;
}

GASNET_INLINE_MODIFIER(gasnete_coll_generic_gather_all)
gasnete_coll_generic_data_t *
gasnete_coll_generic_gather_all(void *dst, void *src,
				size_t nbytes,
				gasnete_coll_threaddata_t *td) {
    gasnete_coll_generic_data_t *result = gasnete_coll_generic_alloc(td);
    result->args.gather_all.dst     = dst;
    result->args.gather_all.src     = src;
    result->args.gather_all.nbytes  = nbytes;
    return result;
}

GASNET_INLINE_MODIFIER(gasnete_coll_generic_gather_allM)
gasnete_coll_generic_data_t *
gasnete_coll_generic_gather_allM(void * const dstlist[], void * const srclist[],
				 size_t nbytes,
				 gasnete_coll_threaddata_t *td) {
    gasnete_coll_generic_data_t *result = gasnete_coll_generic_alloc(td);
    result->args.gather_allM.dstlist = dstlist;
    result->args.gather_allM.srclist = srclist;
    result->args.gather_allM.nbytes  = nbytes;
    return result;
}

GASNET_INLINE_MODIFIER(gasnete_coll_generic_exchange)
gasnete_coll_generic_data_t *
gasnete_coll_generic_exchange(void *dst, void *src,
			      size_t nbytes,
			      gasnete_coll_threaddata_t *td) {
    gasnete_coll_generic_data_t *result = gasnete_coll_generic_alloc(td);
    result->args.exchange.dst     = dst;
    result->args.exchange.src     = src;
    result->args.exchange.nbytes  = nbytes;
    return result;
}

GASNET_INLINE_MODIFIER(gasnete_coll_generic_exchangeM)
gasnete_coll_generic_data_t *
gasnete_coll_generic_exchangeM(void * const dstlist[], void * const srclist[],
			       size_t nbytes,
			       gasnete_coll_threaddata_t *td) {
    gasnete_coll_generic_data_t *result = gasnete_coll_generic_alloc(td);
    result->args.exchangeM.dstlist = dstlist;
    result->args.exchangeM.srclist = srclist;
    result->args.exchangeM.nbytes  = nbytes;
    return result;
}

GASNET_INLINE_MODIFIER(gasnete_coll_generic_syncnb)
int gasnete_coll_generic_syncnb(gasnete_coll_generic_data_t *data) {
  gasneti_assert(data != NULL);
  return (gasnet_try_syncnb(data->handle) == GASNET_OK);
}

GASNET_INLINE_MODIFIER(gasnete_coll_generic_insync)
int gasnete_coll_generic_insync(gasnete_coll_generic_data_t *data) {
  gasneti_assert(data != NULL);
  return (!data->in.enable ||
	  (gasnete_coll_consensus_try(data->in.barrier) == GASNET_OK));
}

GASNET_INLINE_MODIFIER(gasnete_coll_generic_outsync)
int gasnete_coll_generic_outsync(gasnete_coll_generic_data_t *data) {
  gasneti_assert(data != NULL);
  return (!data->out.enable ||
	  (gasnete_coll_consensus_try(data->out.barrier) == GASNET_OK));
}

/* Generic routine to create an op and enter it in the table.
 * Caller provides 'data' and 'poll_fn' specific to the operation.
 * Handle is allocated automatically if flags don't indicate aggregation.
 *
 * Just returns the handle.
 */
gasnet_coll_handle_t
gasnete_coll_op_generic_init(gasnete_coll_team_t team, unsigned int flags,
			     gasnete_coll_generic_data_t *data, gasnete_coll_poll_fn poll_fn,
			     int op_flags, gasnete_coll_threaddata_t *td) {
      gasnet_coll_handle_t handle = GASNET_COLL_INVALID_HANDLE;
      gasnete_coll_op_t *op;
      uint32_t sequence;

      gasneti_assert(team == GASNET_TEAM_ALL);
      gasneti_assert(data != NULL);

      /* Unconditionally allocate a sequence number */
      sequence = gasnete_coll_sequence++;	/* XXX: need team scope */

      /* Conditionally allocate barriers */
      /* XXX: this is where we could do some aggregation of syncs */
      if (data->in.enable) {
	data->in.barrier = gasnete_coll_consensus_create();
      }
      if (data->out.enable) {
	data->out.barrier = gasnete_coll_consensus_create();
      }

      /* Conditionally allocate a handle */
      if_pt (!(flags & GASNET_COLL_AGGREGATE)) {
	handle = gasnete_coll_handle_create();
      }

      /* Create the op */
      op = gasnete_coll_op_create(team, sequence, flags, td);
      op->data = data;
      op->poll_fn = poll_fn;

      /* Place the op in the global table if it is to be visible to AMs */
      if (op_flags & GASNETE_COLL_OP_AM_VISIBLE) {
#if 0
	gasnet_hsl_lock(&gasnete_coll_table_lock);
	gasnete_coll_op_table_ins(op);
	gasnet_hsl_unlock(&gasnete_coll_table_lock);
#else
	;
#endif
      }

      /* Submit the op */
      return gasnete_coll_op_submit(op, handle);
}

/*---------------------------------------------------------------------------------*/
#ifndef GASNETE_COLL_BROADCAST_OVERRIDE
    /* bcast Get: all nodes perform uncoordinated gets */
    /* Valid for SINGLE only, any size */
    static int gasnete_coll_pf_bcast_Get(gasnete_coll_op_t *op) {
      gasnete_coll_generic_data_t *data = op->data;
      const gasnete_coll_broadcast_args_t *args = &(data->args.broadcast);
      int result = 0;

      switch (data->state) {
	case 0:
	  if (!gasnete_coll_generic_insync(data)) {
	    break;
	  }

	  data->handle = gasnet_get_nb_bulk(args->dst, args->srcnode, args->src, args->nbytes);
	  data->state = 1;

	case 1:
	  if (!gasnete_coll_generic_syncnb(data)) {
	    break;
	  }
	  data->state = 2;

	case 2:
	  if (!gasnete_coll_generic_outsync(data)) {
	    break;
	  }

	  gasnete_coll_generic_free(data);
	  result = (GASNETE_COLL_OP_COMPLETE | GASNETE_COLL_OP_INACTIVE);
      }

      return result;
    }

    /* bcast Put: root node performs carefully ordered puts */
    /* Valid for SINGLE only, any size */
    static int gasnete_coll_pf_bcast_Put(gasnete_coll_op_t *op) {
      gasnete_coll_generic_data_t *data = op->data;
      const gasnete_coll_broadcast_args_t *args = &(data->args.broadcast);
      int result = 0;

      switch (data->state) {
	case 0:
	  if (!gasnete_coll_generic_insync(data)) {
	    break;
	  }

	  if (gasnete_mynode == args->srcnode) {
	    void   *src   = args->src;
	    void   *dst   = args->dst;
	    size_t nbytes = args->nbytes;

	    /* Queue PUTS in an NBI access region */
	    gasnet_begin_nbi_accessregion();
	    {
	      int i;

	      /* Put to nodes to the "right" of ourself */
	      for (i = gasnete_mynode + 1; i < gasnete_nodes; ++i) {
		gasnet_put_nbi_bulk(i, dst, src, nbytes);
	      }
	      /* Put to nodes to the "left" of ourself */
	      for (i = 0; i < gasnete_mynode; ++i) {
		gasnet_put_nbi_bulk(i, dst, src, nbytes);
	      }
	    }
	    data->handle = gasnet_end_nbi_accessregion();

	    /* Do local copy LAST, perhaps overlapping with communication */
	    GASNETE_FAST_UNALIGNED_MEMCPY(dst, src, nbytes);
	  }
	  data->state = 1;

	case 1:
	  if (!gasnete_coll_generic_syncnb(data)) {
	    break;
	  }
	  data->state = 2;

	case 2:
	  if (!gasnete_coll_generic_outsync(data)) {
	    break;
	  }

	  gasnete_coll_generic_free(data);
	  result = (GASNETE_COLL_OP_COMPLETE | GASNETE_COLL_OP_INACTIVE);
      }

      return result;
    }

    /* bcast Eager: root node performs carefully ordered eager puts */
    /* Valid for SINGLE and LOCAL, size <= GASNETE_COLL_P2P_EAGER_LIMIT */
    static int gasnete_coll_pf_bcast_Eager(gasnete_coll_op_t *op) {
      gasnete_coll_generic_data_t *data = op->data;
      const gasnete_coll_broadcast_args_t *args = &(data->args.broadcast);
      int result = 0;

      switch (data->state) {
	case 0:
	  if (!gasnete_coll_generic_insync(data)) {
	    break;
	  }

	  if (gasnete_mynode == args->srcnode) {
	    void   *src   = args->src;
	    size_t nbytes = args->nbytes;
	    int i;

	    /* Send to nodes to the "right" of ourself */
	    for (i = gasnete_mynode + 1; i < gasnete_nodes; ++i) {
	      gasnet_coll_p2p_eager_put(op, i, src, nbytes, 0, 1);
	    }
	    /* Send to nodes to the "left" of ourself */
	    for (i = 0; i < gasnete_mynode; ++i) {
	      gasnet_coll_p2p_eager_put(op, i, src, nbytes, 0, 1);
	    }

	    /* Do local copy, perhaps overlapping with communication */
	    GASNETE_FAST_UNALIGNED_MEMCPY(args->dst, src, nbytes);
	  } else {
	    /* XXX Move to generic_init? */
	    data->p2p = gasnete_coll_p2p_get(gasnete_coll_team_id(op->team), op->sequence);
	  }
	  data->state = 1;

	case 1:
	  if (gasnete_mynode != args->srcnode) {
	    gasnete_coll_p2p_entry_t *entry = &(data->p2p->entry[0]);

	    if (!entry->state) {
	      break;
	    }

	    GASNETE_FAST_UNALIGNED_MEMCPY(args->dst, entry->data, args->nbytes);
	    gasnete_coll_p2p_destroy(data->p2p);
	  }
	  data->state = 2;

	case 2:
	  if (!gasnete_coll_generic_outsync(data)) {
	    break;
	  }

	  gasnete_coll_generic_free(data);
	  result = (GASNETE_COLL_OP_COMPLETE | GASNETE_COLL_OP_INACTIVE);
      }

      return result;
    }

    /* bcast RVGet: root node broadcasts address, others get from that address */
    /* Valid for SINGLE and LOCAL, any size */
    static int gasnete_coll_pf_bcast_RVGet(gasnete_coll_op_t *op) {
      gasnete_coll_generic_data_t *data = op->data;
      const gasnete_coll_broadcast_args_t *args = &(data->args.broadcast);
      int result = 0;

      switch (data->state) {
	case 0:
	  if (!gasnete_coll_generic_insync(data)) {
	    break;
	  }

	  if (gasnete_mynode == args->srcnode) {
	    void *src = args->src;
	    int i;

	    /* Send to nodes to the "right" of ourself */
	    for (i = gasnete_mynode + 1; i < gasnete_nodes; ++i) {
	      gasnet_coll_p2p_rendezvous(op, i, src, 0, 1);
	    }
	    /* Send to nodes to the "left" of ourself */
	    for (i = 0; i < gasnete_mynode; ++i) {
	      gasnet_coll_p2p_rendezvous(op, i, src, 0, 1);
	    }

	    /* Do local copy, perhaps overlapping with communication */
	    GASNETE_FAST_UNALIGNED_MEMCPY(args->dst, src, args->nbytes);
	  } else {
	    /* XXX Move to generic_init? */
	    data->p2p = gasnete_coll_p2p_get(gasnete_coll_team_id(op->team), op->sequence);
	  }
	  data->state = 1;

	case 1:
	  if (gasnete_mynode != args->srcnode) {
	    gasnete_coll_p2p_entry_t *entry = &(data->p2p->entry[0]);
	    void *src;

	    if (!entry->state) {
	      break;
	    }

	    src = *((void **)entry->data);
	    gasnete_coll_p2p_destroy(data->p2p);

	    data->handle = gasnet_get_nb_bulk(args->dst, args->srcnode, src, args->nbytes);
	  }
	  data->state = 2;

	case 2:
	  if (!gasnete_coll_generic_syncnb(data)) {
	    break;
	  }
	  data->state = 3;

	case 3:
	  if (!gasnete_coll_generic_outsync(data)) {
	    break;
	  }

	  gasnete_coll_generic_free(data);
	  result = (GASNETE_COLL_OP_COMPLETE | GASNETE_COLL_OP_INACTIVE);
      }

      return result;
    }

    extern gasnet_coll_handle_t
    gasnete_coll_broadcast_nb(gasnet_team_handle_t team,
			      void *dst,
			      gasnet_node_t srcnode, void *src,
			      size_t nbytes, int flags GASNETE_THREAD_FARG)
    {
      gasnete_coll_threaddata_t *td = gasnete_coll_get_threaddata(GASNETE_MYTHREAD);
      gasnete_coll_generic_data_t *data;
      gasnete_coll_poll_fn poll_fn;
      int in_sync, out_sync;

      /* Unconditionally allocate and initialize op-specific data */
      data = gasnete_coll_generic_broadcast(dst, srcnode, src, nbytes, td);

      /* Choose algorithm based on arguments */
      /* XXX: this is where we will plug in conduit-specific algorithm selection */
      in_sync = GASNETE_COLL_IN_MODE(flags);
      out_sync = GASNETE_COLL_OUT_MODE(flags);
      data->in.enable = (in_sync == GASNET_COLL_IN_ALLSYNC);
      if ((in_sync == GASNET_COLL_IN_MYSYNC) || (flags & GASNET_COLL_LOCAL)) {
	if (nbytes <= GASNETE_COLL_P2P_EAGER_LIMIT) {
	  data->out.enable = (out_sync == GASNET_COLL_OUT_ALLSYNC);
	  poll_fn = &gasnete_coll_pf_bcast_Eager;
	} else {
	  data->out.enable = (out_sync != GASNET_COLL_OUT_NOSYNC);
	  poll_fn = &gasnete_coll_pf_bcast_RVGet;
	}
      } else if ((out_sync == GASNET_COLL_OUT_MYSYNC) && (nbytes <= GASNETE_COLL_P2P_EAGER_LIMIT)) {
	poll_fn = &gasnete_coll_pf_bcast_Eager;
      } else {
	data->out.enable = (out_sync != GASNET_COLL_OUT_NOSYNC);
	poll_fn = &gasnete_coll_pf_bcast_Put;
      }

      return gasnete_coll_op_generic_init(team, flags, data, poll_fn, 0, td);
    }
#endif

#ifndef GASNETE_COLL_BROADCAST_M_OVERRIDE
    /* bcastM Get: all nodes perform uncoordinated gets */
    /* Valid for SINGLE only, any size */
    static int gasnete_coll_pf_bcastM_Get(gasnete_coll_op_t *op) {
      gasnete_coll_generic_data_t *data = op->data;
      const gasnete_coll_broadcastM_args_t *args = &(data->args.broadcastM);
      int result = 0;

      switch (data->state) {
	case 0:
	  if (!gasnete_coll_generic_insync(data)) {
	    break;
	  }

	  /* Get only the 1st local image */
	  data->handle = gasnet_get_nb_bulk(GASNETE_COLL_MY_1ST_IMAGE(args->dstlist),
					    args->srcnode, args->src, args->nbytes);
	  data->state = 1;

	case 1:
	  if (!gasnete_coll_generic_syncnb(data)) {
	    break;
	  }

	  /* Copy our 1st image to any additional images */
	  if (gasnete_coll_my_images > 1) {
	    size_t nbytes = args->nbytes;
	    void *p0 = GASNETE_COLL_MY_1ST_IMAGE(args->dstlist);
	    void * const *p = &GASNETE_COLL_MY_1ST_IMAGE(args->dstlist) + 1;
	    int i;

	    /* XXX: for large sizes we should segment this in-memory broadcast */
	    for (i = 1; i < gasnete_coll_my_images; ++i, ++p) {
	      GASNETE_FAST_UNALIGNED_MEMCPY(*p, p0, nbytes);
	    }
	  }

	  data->state = 2;

	case 2:
	  if (!gasnete_coll_generic_outsync(data)) {
	    break;
	  }

	  gasnete_coll_generic_free(data);
	  result = (GASNETE_COLL_OP_COMPLETE | GASNETE_COLL_OP_INACTIVE);
      }

      return result;
    }

    /* bcastM Put: root node performs carefully ordered puts */
    /* Valid for SINGLE only, any size */
    static int gasnete_coll_pf_bcastM_Put(gasnete_coll_op_t *op) {
      gasnete_coll_generic_data_t *data = op->data;
      const gasnete_coll_broadcastM_args_t *args = &(data->args.broadcastM);
      int result = 0;

      switch (data->state) {
	case 0:
	  if (!gasnete_coll_generic_insync(data)) {
	    break;
	  }

	  if (gasnete_mynode == args->srcnode) {
	    void   *src   = args->src;
	    size_t nbytes = args->nbytes;
	    int i, j, limit;
	    void * const *p;

	    /* Queue PUTS in an NBI access region */
	    /* We don't use VIS here, since that would send the same data multiple times */
	    gasnet_begin_nbi_accessregion();
	    {
	      /* Put to nodes to the "right" of ourself */
	      if (gasnete_mynode < gasnete_nodes - 1) {
		p = &GASNETE_COLL_1ST_IMAGE(args->dstlist, gasnete_mynode + 1);
		for (i = gasnete_mynode + 1; i < gasnete_nodes; ++i) {
		  limit = gasnete_coll_all_images[i];
		  for (j = 0; j < limit; ++j) {
		    gasnet_put_nbi_bulk(i, *p, src, nbytes);
		    ++p;
		  }
		}
	      }
	      /* Put to nodes to the "left" of ourself */
	      if (gasnete_mynode != 0) {
		p = &GASNETE_COLL_1ST_IMAGE(args->dstlist, 0);
		for (i = 0; i < gasnete_mynode; ++i) {
		  limit = gasnete_coll_all_images[i];
		  for (j = 0; j < limit; ++j) {
		    gasnet_put_nbi_bulk(i, *p, src, nbytes);
		    ++p;
		  }
		}
	      }
	    }
	    data->handle = gasnet_end_nbi_accessregion();

	    /* Do local copy LAST, perhaps overlapping with communication */
	    /* XXX: for large sizes we should segment this in-memory broadcast */
	    p = &GASNETE_COLL_MY_1ST_IMAGE(args->dstlist);
	    for (j = 0; j < gasnete_coll_my_images; ++j, ++p) {
	      GASNETE_FAST_UNALIGNED_MEMCPY(*p, src, nbytes);
	    }
	  }
	  data->state = 1;

	case 1:
	  if (!gasnete_coll_generic_syncnb(data)) {
	    break;
	  }
	  data->state = 2;

	case 2:
	  if (!gasnete_coll_generic_outsync(data)) {
	    break;
	  }

	  gasnete_coll_generic_free(data);
	  result = (GASNETE_COLL_OP_COMPLETE | GASNETE_COLL_OP_INACTIVE);
      }

      return result;
    }

    /* bcastM Eager: root node performs carefully ordered eager puts */
    /* Valid for SINGLE and LOCAL, size <= GASNETE_COLL_P2P_EAGER_LIMIT */
    static int gasnete_coll_pf_bcastM_Eager(gasnete_coll_op_t *op) {
      gasnete_coll_generic_data_t *data = op->data;
      const gasnete_coll_broadcastM_args_t *args = &(data->args.broadcastM);
      int result = 0;

      switch (data->state) {
	case 0:
	  if (!gasnete_coll_generic_insync(data)) {
	    break;
	  }

	  if (gasnete_mynode == args->srcnode) {
	    void   *src   = args->src;
	    void * const *p;
	    size_t nbytes = args->nbytes;
	    int i, j, limit;

	    /* Send to nodes to the "right" of ourself */
	    for (i = gasnete_mynode + 1; i < gasnete_nodes; ++i) {
	      gasnet_coll_p2p_eager_put(op, i, src, nbytes, 0, 1);
	    }
	    /* Send to nodes to the "left" of ourself */
	    for (i = 0; i < gasnete_mynode; ++i) {
	      gasnet_coll_p2p_eager_put(op, i, src, nbytes, 0, 1);
	    }

	    /* Do local copy LAST, perhaps overlapping with communication */
	    p = &GASNETE_COLL_MY_1ST_IMAGE(args->dstlist);
	    for (j = 0; j < gasnete_coll_my_images; ++j, ++p) {
	      GASNETE_FAST_UNALIGNED_MEMCPY(*p, src, nbytes);
	    }
	  } else {
	    /* XXX Move to generic_init? */
	    data->p2p = gasnete_coll_p2p_get(gasnete_coll_team_id(op->team), op->sequence);
	  }
	  data->state = 1;

	case 1:
	  if (gasnete_mynode != args->srcnode) {
	    gasnete_coll_p2p_entry_t *entry = &(data->p2p->entry[0]);
	    size_t nbytes;
	    void * const *p;
	    int j;

	    if (!entry->state) {
	      break;
	    }

	    p = &GASNETE_COLL_MY_1ST_IMAGE(args->dstlist);
	    nbytes = args->nbytes;
	    for (j = 0; j < gasnete_coll_my_images; ++j, ++p) {
	      GASNETE_FAST_UNALIGNED_MEMCPY(*p, entry->data, nbytes);
	    }

	    gasnete_coll_p2p_destroy(data->p2p);
	  }
	  data->state = 2;

	case 2:
	  if (!gasnete_coll_generic_outsync(data)) {
	    break;
	  }

	  gasnete_coll_generic_free(data);
	  result = (GASNETE_COLL_OP_COMPLETE | GASNETE_COLL_OP_INACTIVE);
      }

      return result;
    }

    /* bcastM RVGet: root node broadcasts address, others get from that address */
    /* Valid for SINGLE and LOCAL, any size */
    static int gasnete_coll_pf_bcastM_RVGet(gasnete_coll_op_t *op) {
      gasnete_coll_generic_data_t *data = op->data;
      const gasnete_coll_broadcastM_args_t *args = &(data->args.broadcastM);
      int result = 0;

      switch (data->state) {
	case 0:
	  if (!gasnete_coll_generic_insync(data)) {
	    break;
	  }

	  if (gasnete_mynode == args->srcnode) {
	    void *src = args->src;
	    void * const *p;
	    size_t nbytes;
	    int i, j;

	    /* Send to nodes to the "right" of ourself */
	    for (i = gasnete_mynode + 1; i < gasnete_nodes; ++i) {
	      gasnet_coll_p2p_rendezvous(op, i, src, 0, 1);
	    }
	    /* Send to nodes to the "left" of ourself */
	    for (i = 0; i < gasnete_mynode; ++i) {
	      gasnet_coll_p2p_rendezvous(op, i, src, 0, 1);
	    }

	    /* Do local copy LAST, perhaps overlapping with communication */
	    /* XXX: for large sizes we should segment this in-memory broadcast */
	    p = &GASNETE_COLL_MY_1ST_IMAGE(args->dstlist);
	    nbytes = args->nbytes;
	    for (j = 0; j < gasnete_coll_my_images; ++j, ++p) {
	      GASNETE_FAST_UNALIGNED_MEMCPY(*p, src, nbytes);
	    }
	  } else {
	    /* XXX Move to generic_init? */
	    data->p2p = gasnete_coll_p2p_get(gasnete_coll_team_id(op->team), op->sequence);
	  }
	  data->state = 1;

	case 1:
	  if (gasnete_mynode != args->srcnode) {
	    gasnete_coll_p2p_entry_t *entry = &(data->p2p->entry[0]);
	    void *src;

	    if (!entry->state) {
	      break;
	    }

	    src = *((void **)entry->data);
	    gasnete_coll_p2p_destroy(data->p2p);

	    /* Get 1st image only */
	    data->handle = gasnet_get_nb_bulk(GASNETE_COLL_MY_1ST_IMAGE(args->dstlist),
					      args->srcnode, src, args->nbytes);
	  }
	  data->state = 2;

	case 2:
	  if (gasnete_mynode != args->srcnode) {
	    if (!gasnete_coll_generic_syncnb(data)) {
	      break;
	    } else {
	      void *p0 = GASNETE_COLL_MY_1ST_IMAGE(args->dstlist);
	      void * const *p = &GASNETE_COLL_MY_1ST_IMAGE(args->dstlist) + 1;
	      size_t nbytes = args->nbytes;
	      int j;

	      /* XXX: for large sizes we should segment this in-memory broadcast */
	      for (j = 1; j < gasnete_coll_my_images; ++j, ++p) {
	        GASNETE_FAST_UNALIGNED_MEMCPY(*p, p0, nbytes);
	      }
	    }
	  }
	  data->state = 3;

	case 3:
	  if (!gasnete_coll_generic_outsync(data)) {
	    break;
	  }

	  gasnete_coll_generic_free(data);
	  result = (GASNETE_COLL_OP_COMPLETE | GASNETE_COLL_OP_INACTIVE);
      }

      return result;
    }

    extern gasnet_coll_handle_t
    gasnete_coll_broadcastM_nb(gasnet_team_handle_t team,
			       void * const dstlist[],
			       gasnet_node_t srcnode, void *src,
			       size_t nbytes, int flags GASNETE_THREAD_FARG)
    {
      gasnete_coll_threaddata_t *td = gasnete_coll_get_threaddata(GASNETE_MYTHREAD);
      gasnete_coll_generic_data_t *data;
      gasnete_coll_poll_fn poll_fn;
      int in_sync, out_sync;

      /* XXX: temporary limitation: */
      gasneti_assert_always(flags & GASNET_COLL_SINGLE);

      /* Unconditionally allocate and initialize op-specific data */
      data = gasnete_coll_generic_broadcastM(dstlist, srcnode, src, nbytes, td);

      /* Choose algorithm based on arguments */
      /* XXX: this is where we will plug in conduit-specific algorithm selection */
      in_sync = GASNETE_COLL_IN_MODE(flags);
      out_sync = GASNETE_COLL_OUT_MODE(flags);
      data->in.enable = (in_sync == GASNET_COLL_IN_ALLSYNC);
      if ((in_sync == GASNET_COLL_IN_MYSYNC) || (flags & GASNET_COLL_LOCAL)) {
	if (nbytes <= GASNETE_COLL_P2P_EAGER_LIMIT) {
	  data->out.enable = (out_sync == GASNET_COLL_OUT_ALLSYNC);
	  poll_fn = &gasnete_coll_pf_bcastM_Eager;
	} else {
	  data->out.enable = (out_sync != GASNET_COLL_OUT_NOSYNC);
	  poll_fn = &gasnete_coll_pf_bcastM_RVGet;
	}
      } else if ((out_sync == GASNET_COLL_OUT_MYSYNC) && (nbytes <= GASNETE_COLL_P2P_EAGER_LIMIT)) {
	poll_fn = &gasnete_coll_pf_bcastM_Eager;
      } else {
	data->out.enable = (out_sync != GASNET_COLL_OUT_NOSYNC);
	poll_fn = &gasnete_coll_pf_bcastM_Get;
      }

      return gasnete_coll_op_generic_init(team, flags, data, poll_fn, 0, td);
    }
#endif

#ifndef GASNETE_COLL_SCATTER_OVERRIDE
    extern gasnet_coll_handle_t
    gasnete_coll_scatter_nb(gasnet_team_handle_t team,
			    void *dst,
			    gasnet_node_t srcnode, void *src,
			    size_t nbytes, int flags GASNETE_THREAD_FARG) {
      gasnete_coll_threaddata_t *td = gasnete_coll_get_threaddata(GASNETE_MYTHREAD);
      gasnete_coll_generic_data_t *data;
      gasnete_coll_poll_fn poll_fn;

      /* XXX: temporary limitation: */
      gasneti_assert_always(flags & GASNET_COLL_SINGLE);

      /* Unconditionally allocate and initialize op-specific data */
      data = gasnete_coll_generic_scatter(dst, srcnode, src, nbytes, td);

      /* We currently map MYSYNC->ALLSYNC unconditionally */
      data->in.enable   = (GASNETE_COLL_IN_MODE(flags)  != GASNET_COLL_IN_NOSYNC);
      data->out.enable  = (GASNETE_COLL_OUT_MODE(flags) != GASNET_COLL_OUT_NOSYNC);

      /* XXX: multiple choice here */
      poll_fn = NULL;

      return GASNETE_COLL_UNIMPLEMENTED();
      /* return gasnete_coll_op_generic_init(team, flags, data, poll_fn, 0, td); */
    }
#endif

#ifndef GASNETE_COLL_SCATTER_M_OVERRIDE
    extern gasnet_coll_handle_t
    gasnete_coll_scatterM_nb(gasnet_team_handle_t team,
			     void * const dstlist[],
			     gasnet_node_t srcnode, void *src,
			     size_t nbytes, int flags GASNETE_THREAD_FARG) {
      gasnete_coll_threaddata_t *td = gasnete_coll_get_threaddata(GASNETE_MYTHREAD);
      gasnete_coll_generic_data_t *data;
      gasnete_coll_poll_fn poll_fn;

      /* XXX: temporary limitation: */
      gasneti_assert_always(flags & GASNET_COLL_SINGLE);

      /* Unconditionally allocate and initialize op-specific data */
      data = gasnete_coll_generic_scatterM(dstlist, srcnode, src, nbytes, td);

      /* We currently map MYSYNC->ALLSYNC unconditionally */
      data->in.enable   = (GASNETE_COLL_IN_MODE(flags)  != GASNET_COLL_IN_NOSYNC);
      data->out.enable  = (GASNETE_COLL_OUT_MODE(flags) != GASNET_COLL_OUT_NOSYNC);

      /* XXX: multiple choice here */
      poll_fn = NULL;

      return GASNETE_COLL_UNIMPLEMENTED();
      /* return gasnete_coll_op_generic_init(team, flags, data, poll_fn, 0, td); */
    }
#endif

#ifndef GASNETE_COLL_GATHER_OVERRIDE
    extern gasnet_coll_handle_t
    gasnete_coll_gather_nb(gasnet_team_handle_t team,
			   gasnet_node_t dstnode, void *dst,
			   void *src,
			   size_t nbytes, int flags GASNETE_THREAD_FARG) {
      gasnete_coll_threaddata_t *td = gasnete_coll_get_threaddata(GASNETE_MYTHREAD);
      gasnete_coll_generic_data_t *data;
      gasnete_coll_poll_fn poll_fn;

      /* XXX: temporary limitation: */
      gasneti_assert_always(flags & GASNET_COLL_SINGLE);

      /* Unconditionally allocate and initialize op-specific data */
      data = gasnete_coll_generic_gather(dstnode, dst, src, nbytes, td);

      /* We currently map MYSYNC->ALLSYNC unconditionally */
      data->in.enable   = (GASNETE_COLL_IN_MODE(flags)  != GASNET_COLL_IN_NOSYNC);
      data->out.enable  = (GASNETE_COLL_OUT_MODE(flags) != GASNET_COLL_OUT_NOSYNC);

      /* XXX: multiple choice here */
      poll_fn = NULL;

      return GASNETE_COLL_UNIMPLEMENTED();
      /* return gasnete_coll_op_generic_init(team, flags, data, poll_fn, 0, td); */
    }
#endif

#ifndef GASNETE_COLL_GATHER_M_OVERRIDE
    extern gasnet_coll_handle_t
    gasnete_coll_gatherM_nb(gasnet_team_handle_t team,
			    gasnet_node_t dstnode, void *dst,
			    void * const srclist[],
			    size_t nbytes, int flags GASNETE_THREAD_FARG) {
      gasnete_coll_threaddata_t *td = gasnete_coll_get_threaddata(GASNETE_MYTHREAD);
      gasnete_coll_generic_data_t *data;
      gasnete_coll_poll_fn poll_fn;

      /* XXX: temporary limitation: */
      gasneti_assert_always(flags & GASNET_COLL_SINGLE);

      /* Unconditionally allocate and initialize op-specific data */
      data = gasnete_coll_generic_gatherM(dstnode, dst, srclist, nbytes, td);

      /* We currently map MYSYNC->ALLSYNC unconditionally */
      data->in.enable   = (GASNETE_COLL_IN_MODE(flags)  != GASNET_COLL_IN_NOSYNC);
      data->out.enable  = (GASNETE_COLL_OUT_MODE(flags) != GASNET_COLL_OUT_NOSYNC);

      /* XXX: multiple choice here */
      poll_fn = NULL;

      return GASNETE_COLL_UNIMPLEMENTED();
      /* return gasnete_coll_op_generic_init(team, flags, data, poll_fn, 0, td); */
    }
#endif

#ifndef GASNETE_COLL_GATHER_ALL_OVERRIDE
    extern gasnet_coll_handle_t
    gasnete_coll_gather_all_nb(gasnet_team_handle_t team,
			       void *dst, void *src,
			       size_t nbytes, int flags GASNETE_THREAD_FARG) {
      gasnete_coll_threaddata_t *td = gasnete_coll_get_threaddata(GASNETE_MYTHREAD);
      gasnete_coll_generic_data_t *data;
      gasnete_coll_poll_fn poll_fn;

      /* XXX: temporary limitation: */
      gasneti_assert_always(flags & GASNET_COLL_SINGLE);

      /* Unconditionally allocate and initialize op-specific data */
      data = gasnete_coll_generic_gather_all(dst, src, nbytes, td);

      /* We currently map MYSYNC->ALLSYNC unconditionally */
      data->in.enable   = (GASNETE_COLL_IN_MODE(flags)  != GASNET_COLL_IN_NOSYNC);
      data->out.enable  = (GASNETE_COLL_OUT_MODE(flags) != GASNET_COLL_OUT_NOSYNC);

      /* XXX: multiple choice here */
      poll_fn = NULL;

      return GASNETE_COLL_UNIMPLEMENTED();
      /* return gasnete_coll_op_generic_init(team, flags, data, poll_fn, 0, td); */
    }
#endif

#ifndef GASNETE_COLL_GATHER_ALL_M_OVERRIDE
    extern gasnet_coll_handle_t
    gasnete_coll_gather_allM_nb(gasnet_team_handle_t team,
				void * const dstlist[], void * const srclist[],
				size_t nbytes, int flags GASNETE_THREAD_FARG) {
      gasnete_coll_threaddata_t *td = gasnete_coll_get_threaddata(GASNETE_MYTHREAD);
      gasnete_coll_generic_data_t *data;
      gasnete_coll_poll_fn poll_fn;

      /* XXX: temporary limitation: */
      gasneti_assert_always(flags & GASNET_COLL_SINGLE);

      /* Unconditionally allocate and initialize op-specific data */
      data = gasnete_coll_generic_gather_allM(dstlist, srclist, nbytes, td);

      /* We currently map MYSYNC->ALLSYNC unconditionally */
      data->in.enable   = (GASNETE_COLL_IN_MODE(flags)  != GASNET_COLL_IN_NOSYNC);
      data->out.enable  = (GASNETE_COLL_OUT_MODE(flags) != GASNET_COLL_OUT_NOSYNC);

      /* XXX: multiple choice here */
      poll_fn = NULL;

      return GASNETE_COLL_UNIMPLEMENTED();
      /* return gasnete_coll_op_generic_init(team, flags, data, poll_fn, 0, td); */
    }
#endif

#ifndef GASNETE_COLL_EXCHANGE_OVERRIDE
    extern gasnet_coll_handle_t
    gasnete_coll_exchange_nb(gasnet_team_handle_t team,
			     void *dst, void *src,
			     size_t nbytes, int flags GASNETE_THREAD_FARG) {
      gasnete_coll_threaddata_t *td = gasnete_coll_get_threaddata(GASNETE_MYTHREAD);
      gasnete_coll_generic_data_t *data;
      gasnete_coll_poll_fn poll_fn;

      /* XXX: temporary limitation: */
      gasneti_assert_always(flags & GASNET_COLL_SINGLE);

      /* Unconditionally allocate and initialize op-specific data */
      data = gasnete_coll_generic_exchange(dst, src, nbytes, td);

      /* We currently map MYSYNC->ALLSYNC unconditionally */
      data->in.enable   = (GASNETE_COLL_IN_MODE(flags)  != GASNET_COLL_IN_NOSYNC);
      data->out.enable  = (GASNETE_COLL_OUT_MODE(flags) != GASNET_COLL_OUT_NOSYNC);

      /* XXX: multiple choice here */
      poll_fn = NULL;

      return GASNETE_COLL_UNIMPLEMENTED();
      /* return gasnete_coll_op_generic_init(team, flags, data, poll_fn, 0, td); */
    }
#endif

#ifndef GASNETE_COLL_EXCHANGE_M_OVERRIDE
    extern gasnet_coll_handle_t
    gasnete_coll_exchangeM_nb(gasnet_team_handle_t team,
			      void * const dstlist[], void * const srclist[],
			      size_t nbytes, int flags GASNETE_THREAD_FARG) {
      gasnete_coll_threaddata_t *td = gasnete_coll_get_threaddata(GASNETE_MYTHREAD);
      gasnete_coll_generic_data_t *data;
      gasnete_coll_poll_fn poll_fn;

      /* XXX: temporary limitation: */
      gasneti_assert_always(flags & GASNET_COLL_SINGLE);

      /* Unconditionally allocate and initialize op-specific data */
      data = gasnete_coll_generic_exchangeM(dstlist, srclist, nbytes, td);

      /* We currently map MYSYNC->ALLSYNC unconditionally */
      data->in.enable   = (GASNETE_COLL_IN_MODE(flags)  != GASNET_COLL_IN_NOSYNC);
      data->out.enable  = (GASNETE_COLL_OUT_MODE(flags) != GASNET_COLL_OUT_NOSYNC);

      /* XXX: multiple choice here */
      poll_fn = NULL;

      return GASNETE_COLL_UNIMPLEMENTED();
      /* return gasnete_coll_op_generic_init(team, flags, data, poll_fn, 0, td); */
    }
#endif

/*---------------------------------------------------------------------------------*/

#ifndef GASNETE_COLL_P2P_HANDLERS
  #define GASNETE_COLL_P2P_HANDLERS
#endif
#define GASNETE_REFCOLL_HANDLERS()                                 \
  GASNETE_COLL_P2P_HANDLERS

/*  $Archive:: /Ti/GASNet/extended-ref/gasnet_extended_refcoll.c $
 *     $Date: 2004/04/09 00:30:36 $
 * $Revision: 1.1.2.8 $
 * Description: Reference implemetation of GASNet Collectives
 * Copyright 2004, Dan Bonachea <bonachea@cs.berkeley.edu>
 * Terms of use are as specified in license.txt
 */

#ifndef GASNETI_GASNET_EXTENDED_COLL_C
  #error This file not meant to be compiled directly - included by gasnet_extended.c
#endif

/*---------------------------------------------------------------------------------*/
/* Forward decls */

#define GASNETE_COLL_IN_MODE(flags) \
	((flags) & (GASNET_COLL_IN_NOSYNC  | GASNET_COLL_IN_MYSYNC  | GASNET_COLL_IN_ALLSYNC))
#define GASNETE_COLL_OUT_MODE(flags) \
	((flags) & (GASNET_COLL_OUT_NOSYNC | GASNET_COLL_OUT_MYSYNC | GASNET_COLL_OUT_ALLSYNC))
#define GASNETE_COLL_SYNC_MODE(flags) \
	((flags) & (GASNET_COLL_OUT_NOSYNC | GASNET_COLL_OUT_MYSYNC | GASNET_COLL_OUT_ALLSYNC | \
	            GASNET_COLL_IN_NOSYNC  | GASNET_COLL_IN_MYSYNC  | GASNET_COLL_IN_ALLSYNC))

/*---------------------------------------------------------------------------------*/
/* Handles */

#ifndef GASNETE_COLL_HANDLE_OVERRIDE
  GASNET_INLINE_MODIFIER(gasnete_coll_hand_create)
  gasnet_coll_handle_t gasnete_coll_handle_create(void) {
    /* XXX: use free list, possibly per thread */
    gasnet_coll_handle_t result = (gasnet_coll_handle_t)gasneti_malloc(sizeof(int));
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

    gasnete_coll_poll();

    if_pf (handle == GASNET_COLL_INVALID_HANDLE) {
      result = GASNET_OK;
    } else if_pf (*handle != 0) {
      gasneti_free((void *)handle);
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
#endif

/*---------------------------------------------------------------------------------*/
/* The list/table of active collective ops (coll ops) */

/* The abstract coll ops table consists of both an "active list" and a lookup
 * table.  Ops in the active table will be polled to make progress.
 * The lookup table is used by AM handlers to locate a given op, possibly
 * creating it before the local initiation.  Therefore, all ops are added to the
 * the table when created, even if they are not initially in the active list.
 * (Duplicates might result otherwise).
 *
 * Operations on the lookup table:
 *   void gasnete_coll_op_table_init()
 *   void gasnete_coll_op_table_fini()
 *   gasnete_coll_op_t *gasnete_coll_op_table_find(team, sequence)
 *	Lookup a coll op by its (team, sequence), returning NULL if not found.
 *   void gasnete_coll_op_table_ins(op)
 *	Add a coll op to table by (op->team, op->sequence).
 *   void gasnete_coll_op_table_del(op)
 *	Delete a coll op from the table.
 *
 * Operations of the active list
 *   void gasnete_coll_op_active_init()
 *   void gasnete_coll_op_active_fini()
 *   gasnete_coll_op_t *gasnete_coll_op_active_first()
 *	Return the first coll op in the active list.
 *   gasnete_coll_op_t *gasnete_coll_op_active_next(op)
 *	Iterate over the coll ops in the active list.
 *   void gasnete_coll_op_active_ins(op)
 *	Add a coll op to the active list.
 *   void gasnete_coll_op_active_del(op)
 *	Delete a coll op from the active list.
 *
 *  All calls must be made with the gasnete_coll_table_lock held.
 */

/* XXX: sequence (and maybe other stuff) will need to be per-team scoped. */
uint32_t gasnete_coll_sequence = 12345;	/* arbitrary non-zero starting value */

gasnet_hsl_t gasnete_coll_table_lock = GASNET_HSL_INITIALIZER;

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

    void
    gasnete_coll_op_table_init(void) {
      int i;
      for (i = 0; i < GASNETE_COLL_TABLE_SIZE; ++i) {
        gasnete_coll_op_t *op = &(gasnete_coll_table[i]);
        op->table_next = op->table_prev = op;
      }
    }

    void
    gasnete_coll_op_table_fini(void) {
      /* EMPTY */
    }

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
#endif

#ifndef GASNETE_COLL_LIST_OVERRIDE
    /* Default implementation of coll_ops active list:
     *
     * Iteration over the active list is based on a circular doubly linked list.
     * Iteration starts from the head and new ops are added at the tail.
     */
    static gasnete_coll_op_t gasnete_coll_list_head;

    void
    gasnete_coll_op_active_init(void) {
      gasnete_coll_op_t *op = &gasnete_coll_list_head;
      op->list_next = op->list_prev = op;
    }

    void
    gasnete_coll_op_active_fini(void) {
      /* EMPTY */
    }

    gasnete_coll_op_t *gasnete_coll_op_active_first(void) {
      gasnete_coll_op_t *op = gasnete_coll_list_head.list_next;
      return (op == &gasnete_coll_list_head) ? NULL : op;
    }

    gasnete_coll_op_t *gasnete_coll_op_active_next(gasnete_coll_op_t *op) {
      op = op->list_next;
      return (op == &gasnete_coll_list_head) ? NULL : op;
    }

    void gasnete_coll_op_active_ins(gasnete_coll_op_t *op) {
      gasnete_coll_op_t *head = &gasnete_coll_list_head;

      /* Add at tail of cicular doubly linked active list */
      op->list_next = head;
      op->list_prev = head->list_prev;
      head->list_prev->list_next = op;
      head->list_prev = op;
    }
                                                                                                              
    void gasnete_coll_op_active_del(gasnete_coll_op_t *op) {
      /* Remove from cicular doubly linked active list */
      op->list_next->list_prev = op->list_prev;
      op->list_prev->list_next = op->list_next;
    }
#endif

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
      /* All ops go onto the active list */
      gasnete_coll_op_active_ins(op);

      op->agg_head = NULL;
      op->handle = handle;

      if_pf (op->flags & GASNET_COLL_AGGREGATE) {
	gasnete_coll_op_t *head = gasnete_coll_agg;

	gasneti_assert(handle == GASNET_COLL_INVALID_HANDLE);	/* check for handle leak */

	if (head == NULL) {
          /* Build a container to hold the aggregate.
	   * The team, sequence and flags don't matter.
	   */
	  head = gasnete_coll_agg = gasnete_coll_op_create(op->team, 0, 0);
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
      }

      return handle;
    }

    void gasnete_coll_op_complete(gasnete_coll_op_t *op, int poll_result) {
      if (poll_result & GASNETE_COLL_OP_COMPLETE) {
        if_pt (op->handle != GASNET_COLL_INVALID_HANDLE) {
	    /* Normal case, just signal the handle */
	    gasnete_coll_handle_signal(op->handle);
	    gasneti_assert(op->agg_head == NULL);
	} else if (op->agg_head) {
	  gasnete_coll_op_t *head = op->agg_head;

	  /* Remove this member from the aggregate */
	  op->agg_next->agg_prev = op->agg_prev;
	  op->agg_prev->agg_next = op->agg_next;

	  /* If the container op is now empty, mark it's handle as done. */
	  if (head->agg_next == head) {
	    gasnete_coll_handle_signal(head->handle);
	    gasnete_coll_op_destroy(head);
	  }
        } else if_pt (op->handle != GASNET_COLL_INVALID_HANDLE) {
	    /* Just signal the handle */
	    gasnete_coll_handle_signal(op->handle);
	}
      }

      if (poll_result & GASNETE_COLL_OP_INACTIVE) {
	/* Nothing extra to do */
      }
    }

#endif 

/*---------------------------------------------------------------------------------*/
gasnete_coll_op_t *
gasnete_coll_op_create(gasnete_coll_team_t team, uint32_t sequence, unsigned int flags) {
  gasnete_coll_op_t *op;

  /* ASSERT: table lock held */

  op = gasneti_malloc(sizeof(*op));	/* XXX: use a free list */

  op->team     = team;
  op->sequence = sequence;
  op->flags    = flags;
  op->handle   = GASNET_COLL_INVALID_HANDLE;
  gasnet_hsl_init(&op->lock);
  op->poll_fn  = (gasnete_coll_poll_fn)NULL;
  
  /* The aggregation and 'data' fields are setup elsewhere */
  /* The gasnete_coll_op_table_ins(op) is done elsewhere */

  return op;
}

void
gasnete_coll_op_destroy(gasnete_coll_op_t *op) {
  /* ASSERT: table lock held */
  gasneti_free(op);	/* Use free list */
}

void gasnete_coll_poll(void) {
  static gasneti_mutex_t poll_lock = GASNETI_MUTEX_INITIALIZER;
  gasnete_coll_op_t *op;

  /* Only one thread should poll */
  if (gasneti_mutex_trylock(&poll_lock) == 0) {

    gasnet_AMPoll();	/* XXX: do more often? */

    gasnet_hsl_lock(&gasnete_coll_table_lock);
    op = gasnete_coll_op_active_first();
    gasnet_hsl_unlock(&gasnete_coll_table_lock);

    while (op != NULL) {
      gasnete_coll_op_t *next;
      int poll_result = 0;

      /* Poll/kick the op */
#if 0
      gasneti_assert(op->poll_fn != (gasnete_coll_poll_fn)NULL);
      poll_result = (*op->poll_fn)(op);
#else
      if (gasnet_hsl_trylock(&op->lock) == GASNET_OK) {
        gasnete_coll_poll_fn poll_fn = op->poll_fn;
        gasnet_hsl_unlock(&op->lock);
	/* Note that we drop the lock before calling poll_fn */
	/* Otherwise poll_fn couldn't call GASNet functions to do useful work */
        gasneti_assert(poll_fn != (gasnete_coll_poll_fn)NULL);
        poll_result = (*poll_fn)(op);
      }
#endif

      /* Get the next op in the active list, removing current if done.
         This is the only place items are removed from the active list and table. */
      gasnet_hsl_lock(&gasnete_coll_table_lock);
      next = gasnete_coll_op_active_next(op);
      if (poll_result != 0) {
        /* invoke the completion hook */
        gasnete_coll_op_complete(op, poll_result);

	if (poll_result & GASNETE_COLL_OP_INACTIVE) {
          /* delete from active list and table */
          gasnete_coll_op_active_del(op);
          gasnete_coll_op_table_del(op);
          gasnete_coll_op_destroy(op);
	}
      }
      gasnet_hsl_unlock(&gasnete_coll_table_lock);

      op = next;
    }

    gasneti_mutex_unlock(&poll_lock);
  }
}

extern void gasnete_coll_init(void) {
  gasnete_coll_op_table_init();
  gasnete_coll_op_active_init();
  /* gasnete_coll_team_init(); */
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

      if (tmp == gasnete_coll_consensus_id) {
	/* Exact match, so we notify and advance */
	++gasnete_coll_consensus_id;
	gasnet_barrier_notify(gasnete_coll_consensus_id, 0);
      }

      if (gasnete_coll_consensus_id & 1) {
	/* At a wait stage, so try the barrier */
	if (gasnet_barrier_try(gasnete_coll_consensus_id, 0) == GASNET_OK) {
	  /* A barrier is complete, advance */
	  ++gasnete_coll_consensus_id;
	}
      }

      /* Note that we need to be careful of wrapping, thus the (int32_t)(a-b) construct
       * must be used in place of simply (a-b).
       */
      return ((int32_t)(gasnete_coll_consensus_id - tmp) > 1) ? GASNET_OK
                                                              : GASNET_ERR_NOT_READY;
    }
#endif

/*---------------------------------------------------------------------------------*/

/* Generic routine to create an op and enter it in the table.
 * Caller provides 'data' and 'poll_fn' specific to the operation.
 * Handle is allocated automatically if flags don't indicate aggregation.
 *
 * Just returns the handle.
 */
gasnet_coll_handle_t
gasnete_coll_op_generic_init(gasnete_coll_team_t team, uint32_t sequence, unsigned int flags,
			     void *data, gasnete_coll_poll_fn poll_fn) {
      gasnet_coll_handle_t handle = GASNET_COLL_INVALID_HANDLE;
      gasnete_coll_op_t *op;

      gasneti_assert(team == GASNET_TEAM_ALL);

      /* Conditionally allocate a handle */
      if_pt (!(flags & GASNET_COLL_AGGREGATE)) {
        handle = gasnete_coll_handle_create();
      }

      /* Atomically create and initialize the op, which might already exist partially initialized */
      gasnet_hsl_lock(&gasnete_coll_table_lock);
      op = gasnete_coll_op_table_find(team, sequence);
      if_pt (op == NULL) {
	/* Not in the table yet, allocate and initialize it (no per-instance lock needed) */
        op = gasnete_coll_op_create(team, sequence, flags);
        op->data = data;
        op->poll_fn = poll_fn;
	gasnete_coll_op_table_ins(op);
      } else {
	/* Exists in the table, acquire lock before initializing */
	/* XXX: wish we didn't need to assume the worst here */
        gasnet_hsl_lock(&(op->lock));
        op->data = data;
        op->poll_fn = poll_fn;
        gasnet_hsl_unlock(&(op->lock));
      }
      handle = gasnete_coll_op_submit(op, handle);
      gasnet_hsl_unlock(&gasnete_coll_table_lock);

      return handle;
}

#ifndef GASNETE_COLL_BROADCAST_OVERRIDE
    static int gasnete_coll_broadcast_poll_insync(gasnete_coll_op_t *op);
    static int gasnete_coll_broadcast_poll_rdma_sync(gasnete_coll_op_t *op);
    static int gasnete_coll_broadcast_poll_outsync(gasnete_coll_op_t *op);

    typedef struct {
      gasnet_node_t srcnode;
      void *src, *dst;
      size_t nbytes;

      gasnete_coll_consensus_t in_barrier, out_barrier;
      gasnet_handle_t put_handle;
    } gasnete_coll_broadcast_data_t;

    static int gasnete_coll_broadcast_fini(gasnete_coll_op_t *op) {
      gasneti_free(op->data);
      return (GASNETE_COLL_OP_COMPLETE | GASNETE_COLL_OP_INACTIVE);
    }

    static int gasnete_coll_broadcast_outsync(gasnete_coll_op_t *op) {
      int result;

      if (GASNETE_COLL_OUT_MODE(op->flags) == GASNET_COLL_OUT_NOSYNC) {
        /* DONE */
        result = gasnete_coll_broadcast_fini(op);
      } else {
	/* Advance state to output sync */
	op->poll_fn = &gasnete_coll_broadcast_poll_outsync;
	result = (*op->poll_fn)(op);
      }

      return result;
    }

    static void gasnete_coll_broadcast_do_rdma(gasnete_coll_broadcast_data_t *data) {
      void *src, *dst;
      size_t nbytes;
      gasnet_node_t i;

      gasneti_assert(data != NULL);
      gasneti_assert(gasnete_mynode == data->srcnode);

      src    = data->src;
      dst    = data->dst;
      nbytes = data->nbytes;

      /* Queue PUTS */
      /* XXX: Schedule this */
      gasnet_begin_nbi_accessregion();
      for (i = 0; i < gasnete_nodes; ++i) {
	gasnet_put_nbi_bulk(i, dst, src, nbytes);
      }
      data->put_handle = gasnet_end_nbi_accessregion();
    }

    static int gasnete_coll_broadcast_poll_insync(gasnete_coll_op_t *op) {
      gasnete_coll_broadcast_data_t *data = op->data;
      int result = 0;

      gasneti_assert(data != NULL);
      gasneti_assert(GASNETE_COLL_IN_MODE(op->flags) != GASNET_COLL_IN_NOSYNC);

      if_pf (gasnete_coll_consensus_try(data->in_barrier) == GASNET_OK) {
        if (gasnete_mynode == data->srcnode) {
	  /* root node: start the rdma */
          gasnete_coll_broadcast_do_rdma(op->data);

	  /* Advance state to rdma sync */
	  op->poll_fn = &gasnete_coll_broadcast_poll_rdma_sync;
	  result = (*op->poll_fn)(op);
	} else {
	  /* non-root node: advance state to output sync */
          result = gasnete_coll_broadcast_outsync(op);
        }
      }

      return result;
    }

    static int gasnete_coll_broadcast_poll_rdma_sync(gasnete_coll_op_t *op) {
      gasnete_coll_broadcast_data_t *data = op->data;
      int result = 0;

      gasneti_assert(data != NULL);
      gasneti_assert(gasnete_mynode == data->srcnode);

      if_pf (gasnet_try_syncnb(data->put_handle) == GASNET_OK) {
	/* advance state to output sync */
        result = gasnete_coll_broadcast_outsync(op);
      }

      return result;
    }

    static int gasnete_coll_broadcast_poll_outsync(gasnete_coll_op_t *op) {
      gasnete_coll_broadcast_data_t *data = op->data;
      int result = 0;

      gasneti_assert(data != NULL);
      gasneti_assert(GASNETE_COLL_OUT_MODE(op->flags) != GASNET_COLL_OUT_NOSYNC);

      if_pf (gasnete_coll_consensus_try(data->out_barrier) == GASNET_OK) {
	result = gasnete_coll_broadcast_fini(op);
      }

      return result;
    }

    extern gasnet_coll_handle_t
    gasnet_coll_broadcast_nb(gasnet_team_handle_t team,
                             void *dst,
                             gasnet_node_t srcnode, void *src,
                             size_t nbytes, int flags)
    {
      gasnete_coll_broadcast_data_t *data;
      gasnet_coll_handle_t handle;
      gasnete_coll_poll_fn poll_fn = NULL;
      uint32_t sequence;

      /* Present implementation is VERY limited: */
      gasneti_assert(team == GASNET_TEAM_ALL);
      gasneti_assert(flags & GASNET_COLL_DST_IN_SEGMENT);
      gasneti_assert(flags & GASNET_COLL_SRC_IN_SEGMENT);
      gasneti_assert(flags & GASNET_COLL_SINGLE);

      /* Unconditionally allocate a sequence number */
      sequence = gasnete_coll_sequence++;	/* XXX: need team scope */

      /* Unconditionally allocate and initialize op-specific data */
      /* In the future we might skip some of this conditionally */
      data = gasneti_malloc(sizeof(gasnete_coll_broadcast_data_t));
      data->srcnode = srcnode;
      data->src     = src;
      data->dst     = dst;
      data->nbytes  = nbytes;
      
      /* First deal with input sync */
      if (GASNETE_COLL_IN_MODE(flags) != GASNET_COLL_IN_NOSYNC) {
        data->in_barrier = gasnete_coll_consensus_create();
        poll_fn = &gasnete_coll_broadcast_poll_insync;
      } else if (srcnode == gasnete_mynode) {
	/* Queue dma right away */
        gasnete_coll_broadcast_do_rdma(data);
	poll_fn = &gasnete_coll_broadcast_poll_rdma_sync;
      }

      /* Now deal with output sync */
      if (GASNETE_COLL_OUT_MODE(flags) != GASNET_COLL_OUT_NOSYNC) {
        data->out_barrier = gasnete_coll_consensus_create();
	if (poll_fn == NULL) {
          poll_fn = &gasnete_coll_broadcast_poll_outsync;
	}
      }

      /* Now get things going */
      if (poll_fn == NULL) {
	/* NOSYNC/NOSYNC on non-root node == nothing to do */
        handle = GASNET_COLL_INVALID_HANDLE;
	gasneti_free(data);
      } else {
	handle = gasnete_coll_op_generic_init(team, sequence, flags, data, poll_fn);
      }

      return handle;
    }
#endif


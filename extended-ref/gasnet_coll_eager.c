/*  $Archive:: /Ti/GASNet/extended-ref/gasnet_extended_refcoll.c $
 *     $Date: 2004/05/11 23:56:42 $
 * $Revision: 1.1.2.11 $
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

int gasnete_coll_init_done = 0;

void gasnete_coll_validate(gasnet_team_handle_t team,
			   gasnet_node_t dstnode, void *dst, size_t dstlen, int dstisv,
                           gasnet_node_t srcnode, void *src, size_t srclen, int srcisv,
			   unsigned int flags) {
  if_pf (!gasnete_coll_init_done) {
    gasneti_fatalerror("Illegal call to GASNet collectives before gasnet_coll_init()\n");
  }

  gasneti_assert(GASNETE_COLL_IN_MODE(flags) != 0);	/* IN mode has no default */
  gasneti_assert(GASNETE_COLL_OUT_MODE(flags) != 0);	/* OUT mode has no default */
  gasneti_assert(flags & GASNET_COLL_DST_IN_SEGMENT);	/* XXX: Temporary limitation */
  gasneti_assert(flags & GASNET_COLL_SRC_IN_SEGMENT);	/* XXX: Temporary limitation */
  gasneti_assert(((flags & GASNET_COLL_SINGLE)?1:0) ^ ((flags & GASNET_COLL_LOCAL)?1:0));

  /* XXX: TO DO
   * + bounds check src and/or dst ranges as indicated by *_IN_SEGMENT
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
 *   void gasnete_coll_op_table_new(op)
 *	Init table fields of a coll op.
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
 *   void gasnete_coll_op_active_new(op)
 *	Init active list fields of a coll op.
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

#ifndef GASNETE_COLL_LIST_OVERRIDE
    /* Default implementation of coll_ops active list:
     *
     * Iteration over the active list is based on a circular doubly linked list.
     * Iteration starts from the head and new ops are added at the tail.
     */
    static gasnete_coll_op_t gasnete_coll_list_head;

    gasnete_coll_op_t *gasnete_coll_op_active_first(void) {
      gasnete_coll_op_t *op = gasnete_coll_list_head.list_next;
      return (op == &gasnete_coll_list_head) ? NULL : op;
    }

    gasnete_coll_op_t *gasnete_coll_op_active_next(gasnete_coll_op_t *op) {
      op = op->list_next;
      return (op == &gasnete_coll_list_head) ? NULL : op;
    }

    void gasnete_coll_op_active_new(gasnete_coll_op_t *op) {
      op->list_next = op->list_prev = op;
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

    void
    gasnete_coll_op_active_init(void) {
      gasnete_coll_op_active_new(&gasnete_coll_list_head);
    }

    void
    gasnete_coll_op_active_fini(void) {
      /* EMPTY */
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
	op->agg_next = NULL;
      }

      /* Poll the op once, releasing the lock momentarily  */
      gasnet_hsl_unlock(&gasnete_coll_table_lock);
      poll_result = (*op->poll_fn)(op);
      gasnet_hsl_lock(&gasnete_coll_table_lock);
      if (poll_result != 0) {
        gasnete_coll_op_complete(op, poll_result);
      }

      if (!(poll_result & GASNETE_COLL_OP_INACTIVE)) {
        /* Active ops go onto the active list */
        gasnete_coll_op_active_ins(op);
      }

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
        /* delete from active list and table and destoy */
        gasnete_coll_op_active_del(op);
        gasnete_coll_op_table_del(op);
        gasnete_coll_op_destroy(op);
      }
    }

#endif 

/*---------------------------------------------------------------------------------*/
gasnete_coll_op_t *
gasnete_coll_op_create(gasnete_coll_team_t team, uint32_t sequence, unsigned int flags) {
  gasnete_coll_op_t *op;

  /* ASSERT: table lock held */

  op = gasneti_malloc(sizeof(*op));	/* XXX: use a free list */

  gasnete_coll_op_table_new(op);
  gasnete_coll_op_active_new(op);
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
      gasneti_assert(op->poll_fn != (gasnete_coll_poll_fn)NULL);
      poll_result = (*op->poll_fn)(op);

      /* Get the next op in the active list, removing current if done. */
      gasnet_hsl_lock(&gasnete_coll_table_lock);
      next = gasnete_coll_op_active_next(op);
      if (poll_result != 0) {
        /* invoke the completion hook */
        gasnete_coll_op_complete(op, poll_result);
      }
      gasnet_hsl_unlock(&gasnete_coll_table_lock);

      op = next;
    }

    gasneti_mutex_unlock(&poll_lock);
  }
}

extern void gasnete_coll_init(const size_t images[], int init_flags) {
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

  gasnete_coll_op_table_init();
  gasnete_coll_op_active_init();
  /* gasnete_coll_team_init(); */

  gasnete_coll_init_done = 1;
  gasnet_barrier_notify(0,0);
  gasnet_barrier_wait(0,0);
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
        gasnet_hsl_unlock(&(op->lock));
        op->poll_fn = poll_fn;
      }
      handle = gasnete_coll_op_submit(op, handle);
      gasnet_hsl_unlock(&gasnete_coll_table_lock);

      return handle;
}

#ifndef GASNETE_COLL_BROADCAST_OVERRIDE
    typedef struct {
      gasnet_node_t srcnode;
      void *src, *dst;
      size_t nbytes;

      gasnete_coll_consensus_t in_barrier, out_barrier;
      gasnet_handle_t rdma_handle;
    } gasnete_coll_broadcast_data_t;

    /* XXX multiple algorithms can plug in here.
     * Currently on one-step (GET or PUT) fit in this framework.
     */
    static void gasnete_coll_broadcast_do_rdma(gasnete_coll_broadcast_data_t *data) {
      gasneti_assert(data != NULL);

      if (gasnete_mynode == data->srcnode) {
        gasnet_node_t i;
        void   *src   = data->src;
        void   *dst   = data->dst;
        size_t nbytes = data->nbytes;

        /* Queue PUTS */
        /* XXX: Schedule this */
        gasnet_begin_nbi_accessregion();
        for (i = 0; i < gasnete_nodes; ++i) {
	  gasnet_put_nbi_bulk(i, dst, src, nbytes);
        }
        data->rdma_handle = gasnet_end_nbi_accessregion();
      } else {
        data->rdma_handle = GASNET_INVALID_HANDLE;
      }
    }

    static int gasnete_coll_broadcast_fini(gasnete_coll_op_t *op) {
      gasneti_free(op->data);
      return (GASNETE_COLL_OP_COMPLETE | GASNETE_COLL_OP_INACTIVE);
    }

    static int gasnete_coll_broadcast_poll_outsync(gasnete_coll_op_t *op) {
      gasnete_coll_broadcast_data_t *data = op->data;
      int result = 0;

      gasneti_assert(data != NULL);

      if_pf ((GASNETE_COLL_OUT_MODE(op->flags) == GASNET_COLL_OUT_NOSYNC) ||
             (gasnete_coll_consensus_try(data->out_barrier) == GASNET_OK)) {
	op->poll_fn = &gasnete_coll_broadcast_fini;
	result = (*op->poll_fn)(op);
      }

      return result;
    }

    static int gasnete_coll_broadcast_poll_rdma_sync(gasnete_coll_op_t *op) {
      gasnete_coll_broadcast_data_t *data = op->data;
      int result = 0;

      gasneti_assert(data != NULL);

      if_pf (gasnet_try_syncnb(data->rdma_handle) == GASNET_OK) {
	/* advance state to output sync */
	op->poll_fn = &gasnete_coll_broadcast_poll_outsync;
	result = (*op->poll_fn)(op);
      }

      return result;
    }

    static int gasnete_coll_broadcast_poll_insync(gasnete_coll_op_t *op) {
      gasnete_coll_broadcast_data_t *data = op->data;
      int result = 0;

      gasneti_assert(data != NULL);

      if_pf ((GASNETE_COLL_IN_MODE(op->flags) == GASNET_COLL_IN_NOSYNC) ||
	     (gasnete_coll_consensus_try(data->in_barrier) == GASNET_OK)) {
	/* start the rdma */
        gasnete_coll_broadcast_do_rdma(op->data);

	/* Advance state to rdma sync */
	op->poll_fn = &gasnete_coll_broadcast_poll_rdma_sync;
	result = (*op->poll_fn)(op);
      }

      return result;
    }

    extern gasnet_coll_handle_t
    gasnete_coll_broadcast_nb(gasnet_team_handle_t team,
                              void *dst,
                              gasnet_node_t srcnode, void *src,
                              size_t nbytes, int flags GASNETE_THREAD_FARG)
    {
      gasnete_coll_broadcast_data_t *data;
      uint32_t sequence;

      /* Present implementation is VERY limited: */
      gasneti_assert(team == GASNET_TEAM_ALL);
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

      if (GASNETE_COLL_IN_MODE(flags) != GASNET_COLL_IN_NOSYNC) {
        data->in_barrier = gasnete_coll_consensus_create();
      }
      if (GASNETE_COLL_OUT_MODE(flags) != GASNET_COLL_OUT_NOSYNC) {
        data->out_barrier = gasnete_coll_consensus_create();
      }
      
      return gasnete_coll_op_generic_init(team, sequence, flags, data, &gasnete_coll_broadcast_poll_insync);
    }
#endif


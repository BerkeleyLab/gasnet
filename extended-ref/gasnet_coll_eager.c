/*  $Archive:: /Ti/GASNet/extended-ref/gasnet_extended_refcoll.c $
 *     $Date: 2004/04/07 18:05:29 $
 * $Revision: 1.1.2.6 $
 * Description: Reference implemetation of GASNet Collectives
 * Copyright 2004, Dan Bonachea <bonachea@cs.berkeley.edu>
 * Terms of use are as specified in license.txt
 */

#ifndef GASNETI_GASNET_EXTENDED_COLL_C
  #error This file not meant to be compiled directly - included by gasnet_extended.c
#endif

/*---------------------------------------------------------------------------------*/
/* Forward decls */

/*---------------------------------------------------------------------------------*/
/* Handles */

#ifndef GASNETE_COLL_HANDLE_OVERRIDE
  GASNET_INLINE_MODIFIER(gasnete_coll_hand_signal)
  void gasnete_coll_handle_signal(gasnet_coll_handle_t handle) {
    gasneti_assert(handle != GASNET_COLL_INVALID_HANDLE);
    *handle = 1;
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
        return NULL;
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
uint32_t gasnete_coll_sequence = 0;

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
      gasnete_coll_op_t *op;

      /* Search table */
      op = head->table_next;
      while ((op != head) && (op->team != team) && (op->sequence != sequence)) {
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
    gasnete_coll_op_list_init(void) {
      gasnete_coll_op_t *op = &gasnete_coll_list_head;
      op->list_next = op->list_prev = op;
    }

    void
    gasnete_coll_op_list_fini(void) {
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

	gasneti_assert(handle == NULL);	/* check for handle leak */

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
  static gasnet_hsl_t poll_lock = GASNET_HSL_INITIALIZER;
  gasnete_coll_op_t *op;

  /* Only one thread should poll */
  if (gasnet_hsl_trylock(&poll_lock) == GASNET_OK) {
    gasnet_hsl_lock(&gasnete_coll_table_lock);
    op = gasnete_coll_op_active_first();
    gasnet_hsl_unlock(&gasnete_coll_table_lock);

    while (op != NULL) {
      gasnete_coll_op_t *next;
      int poll_result = 0;

      /* Poll/kick the op, unless another thread (typically an AM) is modifying it */
      if (gasnet_hsl_trylock(&op->lock) == GASNET_OK) {
        if (op->poll_fn != NULL) {
          poll_result = (*op->poll_fn)(op);
        }
        gasnet_hsl_unlock(&op->lock);
      }

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

    gasnet_hsl_unlock(&poll_lock);
  }
}

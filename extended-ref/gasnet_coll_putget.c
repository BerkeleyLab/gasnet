/*  $Archive:: /Ti/GASNet/extended-ref/gasnet_extended_refcoll.c $
 *     $Date: 2004/04/02 18:53:19 $
 * $Revision: 1.1.2.4 $
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
/* Collective teams */

/* XXX: Teams are not yet fully designed
 *
 * Likely interface:
 *
 *  void gasnete_coll_team_ins(op)
 *	Add a team to the table
 *  void gasnete_coll_team_del(op)
 *	Remove a team from the table
 *  gasnete_coll_team_t *gasnete_coll_team_find(team_id)
 *	Lookup a team by its 32-bit id, returning NULL if not found.
 *
 * Serialization done inside the implementation
 */

#ifndef GASNETE_COLL_TEAMS_OVERRIDE
    /* Called by by AM handlers to lookup the team by id */
    gasnete_coll_team_t *gasnete_coll_team_lookup(uint32_t team_id) {
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
 *   gasnete_coll_op_t *gasnete_coll_op_table_find(team, sequence)
 *	Lookup a coll op by its (team, sequence), returning NULL if not found.
 *   void gasnete_coll_op_table_ins(op)
 *	Add a coll op to table by (op->team, op->sequence).
 *   void gasnete_coll_op_table_del(op)
 *	Delete a coll op from the table.
 *
 * Operations of the active list
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
    /* Default implementation:
     *
     * Iteration over the table is based on a doubly linked list.
     * Iteration starts from the head and new ops are added at the tail.
     *
     * Lookups are based on a fixed size table with slots used round-robin.
     * Conflicts, if any, are resolved by chaining with a doubly linked list.
     * Only the head is kept for these per-slot lists.
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

    static gasnete_coll_op_t *gasnete_coll_table_head = NULL;
    static gasnete_coll_op_t *gasnete_coll_table_tail = NULL;
    static gasnete_coll_op_t *gasnete_coll_table[GASNETE_COLL_TABLE_SIZE];

    /* GASNET_INLINE_MODIFIER(gasnete_coll_op_table_find) */
    gasnete_coll_op_t *
    gasnete_coll_op_table_find(gasnete_coll_team_t *team, uint32_t sequence) {
      unsigned int slot_nr = GASNETE_COLL_TABLE_SLOT(team, sequence);
      gasnete_coll_op_t *op;

      /* Search table */
      op = gasnete_coll_table[slot_nr];
      while ((op != NULL) && (op->team != team) && (op->sequence != sequence)) {
        op = op->hash_next;
      }

      return op;
    }

    /* GASNET_INLINE_MODIFIER(gasnete_coll_op_table_ins) */
    void gasnete_coll_op_table_ins(gasnete_coll_op_t *op) {
      unsigned int slot_nr = GASNETE_COLL_TABLE_SLOT(op->team, op->sequence);
      gasnete_coll_op_t **slot = &(gasnete_coll_table[slot_nr]);
      
      /* Add to doubly linked hash bucket */
      op->hash_prev = NULL;
      op->hash_next = *slot;
      if (*slot) {
        (*slot)->hash_prev = op;
      }
      *slot = op;
    }

    /* GASNET_INLINE_MODIFIER(gasnete_coll_op_table_del) */
    void gasnete_coll_op_table_del(gasnete_coll_op_t *op) {
      /* Remove from doubly linked hash bucket */
      if (op->hash_next) {
        op->hash_next->hash_prev = op->hash_prev;
      }
      if (op->hash_prev) {
        op->hash_prev->hash_next = op->hash_next;
      } else {
        unsigned int slot_nr = GASNETE_COLL_TABLE_SLOT(op->team, op->sequence);
        gasnete_coll_table[slot_nr] = op->hash_next;
      }
    }


    /* GASNET_INLINE_MODIFIER(gasnete_coll_op_active_first) */
    gasnete_coll_op_t *gasnete_coll_op_active_first(void) {
      return gasnete_coll_table_head;
    }

    /* GASNET_INLINE_MODIFIER(gasnete_coll_op_active_next) */
    gasnete_coll_op_t *gasnete_coll_op_active_next(gasnete_coll_op_t *op) {
      return op->list_next;
    }

    /* GASNET_INLINE_MODIFIER(gasnete_coll_op_active_ins) */
    void gasnete_coll_op_active_ins(gasnete_coll_op_t *op) {
      /* Add at tail of doubly linked active list */
      op->list_next = NULL;
      op->list_prev = gasnete_coll_table_tail;
      if (gasnete_coll_table_tail) {
        gasnete_coll_table_tail->list_next = op;
      } else {
        gasnete_coll_table_head = op;
      }
      gasnete_coll_table_tail = op;
    }
                                                                                                              
    /* GASNET_INLINE_MODIFIER(gasnete_coll_op_active_del) */
    void gasnete_coll_op_active_del(gasnete_coll_op_t *op) {
      /* Remove from doubly linked active list */
      if (op->list_next) {
        op->list_next->list_prev = op->list_prev;
      } else {
        gasnete_coll_table_tail = op->list_prev;
      }
      if (op->list_prev) {
        op->list_prev->list_next = op->list_next;
      } else {
        gasnete_coll_table_head = op->list_next;
      }
    }
#endif

/*---------------------------------------------------------------------------------*/

gasnete_coll_op_t *
gasnete_coll_op_create(gasnete_coll_team_t *team, uint32_t sequence, unsigned int flags)
{
  gasnete_coll_op_t *op;

  /* ASSERT: table lock is held */

  op = gasneti_malloc(sizeof(*op));	/* XXX: use a free list */

  op->team     = team;
  op->sequence = sequence;
  op->flags    = flags;
  op->done     = 0;
  gasnet_hsl_init(&op->lock);
  op->poll_fn  = (gasnete_coll_poll_fn)NULL;
                                                                                                              
  gasnete_coll_op_table_ins(op);
  
  #if GASNET_DEBUG
    /* The 'agg_prev' and 'data' fields are setup by the local collective
       initiation function and thus may remain uninitialized for some time
       if this coll op is created from an AM.
       When debugging they are intentionally set to bogus values here to
       help catch buggy code.
    */
    op->agg_prev = (gasnete_coll_op_t *)0xdeadbeef;
    op->data = (gasnete_coll_op_t *)0xcafef00d;
  #endif

  return op;
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
      int done = 0;

      /* Poll/kick the op, unless another thread (typically an AM) is modifying it */
      if (gasnet_hsl_trylock(&op->lock) == GASNET_OK) {
        if (op->poll_fn != NULL) {
          done = (*op->poll_fn)(op);
        }
        gasnet_hsl_unlock(&op->lock);
      }

      /* Get the next op in the active list, removing current if done.
         This is the only place items are removed from the active list and table. */
      gasnet_hsl_lock(&gasnete_coll_table_lock);
      next = gasnete_coll_op_active_next(op);
      if (done) {
        /* delete from active list and table */
        gasnete_coll_op_active_del(op);
        gasnete_coll_op_table_del(op);

        /* mark the op as completed */
        op->done = 1;
      }
      gasnet_hsl_unlock(&gasnete_coll_table_lock);

      op = next;
    }

    gasnet_hsl_unlock(&poll_lock);
  }
}

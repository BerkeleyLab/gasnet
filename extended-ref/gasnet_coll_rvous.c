/*  $Archive:: /Ti/GASNet/extended-ref/gasnet_extended_refcoll.c $
 *     $Date: 2004/04/01 17:51:59 $
 * $Revision: 1.1.2.3 $
 * Description: Reference implemetation of GASNet Collectives
 * Copyright 2004, Dan Bonachea <bonachea@cs.berkeley.edu>
 * Terms of use are as specified in license.txt
 */

#ifndef GASNETI_GASNET_EXTENDED_COLL_C
  #error This file not meant to be compiled directly - included by gasnet_extended.c
#endif

/*---------------------------------------------------------------------------------*/
/* Forward decls */

static gasnete_coll_op_t *
gasnete_coll_op_create(gasnete_coll_team_t *team, uint32_t sequence, unsigned int flags);

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

/* The interface to the abstract coll ops table consists of:
 *
 *  gasnete_coll_op_t *gasnete_coll_op_lookup(team, sequence, flags)
 *	Lookup a coll op by its (team, sequence), creating it if not found.
 *	Calls gasnete_coll_op_create() to perform the creation.
 *  gasnete_coll_op_t *gasnete_coll_op_first()
 *	Return the first coll op in the table.
 *  gasnete_coll_op_t *gasnete_coll_op_next(op)
 *	Iterate over the coll ops in the table.
 *  gasnete_coll_op_t *gasnete_coll_op_del(op)
 *	Delete a coll op from the table, returning its successor.
 */

/* XXX: sequence (and maybe other stuff) will need to be per-team scoped. */
uint32_t gasnete_coll_sequence = 0;

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

    static gasnet_hsl_t gasnete_coll_table_lock = GASNET_HSL_INITIALIZER;
    static gasnete_coll_op_t *gasnete_coll_table_head = NULL;
    static gasnete_coll_op_t *gasnete_coll_table_tail = NULL;
    static gasnete_coll_op_t *gasnete_coll_table[GASNETE_COLL_TABLE_SIZE];

    /* GASNET_INLINE_MODIFIER(gasnete_coll_op_lookup) */
    gasnete_coll_op_t *
    gasnete_coll_op_lookup(gasnete_coll_team_t *team, uint32_t sequence, unsigned int flags) {
      unsigned int slot_nr = GASNETE_COLL_TABLE_SLOT(team, sequence);
      gasnete_coll_op_t **slot = &(gasnete_coll_table[slot_nr]);
      gasnete_coll_op_t *op;

      gasnet_hsl_lock(&gasnete_coll_table_lock);

      /* Search table */
      for (op = *slot; op != NULL; op = op->hash_next) {
        if ((op->team == team) && (op->sequence == sequence)) {
          break;
        }
      }

      if (op == NULL) {
        op = gasnete_coll_op_create(team, sequence, flags);

        /* Add to doubly linked active list */
        {
          op->list_next = NULL;
          op->list_prev = gasnete_coll_table_tail;
          if (gasnete_coll_table_tail) {
            gasnete_coll_table_tail->list_next = op;
          } else {
            gasnete_coll_table_head = op;
          }
          gasnete_coll_table_tail = op;
        }

        /* Add to doubly linked hash bucket */
        {
	  op->hash_prev = NULL;
	  op->hash_next = *slot;
	  if (*slot) {
            (*slot)->hash_prev = op;
	  }
	  *slot = op;
        }
      }

      gasnet_hsl_unlock(&gasnete_coll_table_lock);
      return op;
    }

    /* GASNET_INLINE_MODIFIER(gasnete_coll_op_first) */
    gasnete_coll_op_t *gasnete_coll_op_first(void) {
      gasnete_coll_op_t *op;

      gasnet_hsl_lock(&gasnete_coll_table_lock);
      op = gasnete_coll_table_head;
      gasnet_hsl_unlock(&gasnete_coll_table_lock);

      return op;
    }

    /* GASNET_INLINE_MODIFIER(gasnete_coll_op_next) */
    gasnete_coll_op_t *gasnete_coll_op_next(gasnete_coll_op_t *op) {
      gasnet_hsl_lock(&gasnete_coll_table_lock);
      op = op->list_next;
      gasnet_hsl_unlock(&gasnete_coll_table_lock);
      return op;
    }

    /* GASNET_INLINE_MODIFIER(gasnete_coll_op_del) */
    gasnete_coll_op_t *gasnete_coll_op_del(gasnete_coll_op_t *op) {
      gasnet_hsl_lock(&gasnete_coll_table_lock);

      /* Remove from doubly linked active list */
      {
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

      /* Remove from doubly linked hash bucket */
      {
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

      /* Get successor */
      op = op->list_next;

      gasnet_hsl_unlock(&gasnete_coll_table_lock);
      return op;
    }

#endif

/*---------------------------------------------------------------------------------*/

static gasnete_coll_op_t *
gasnete_coll_op_create(gasnete_coll_team_t *team, uint32_t sequence, unsigned int flags)
{
  gasnete_coll_op_t *op;

  op = gasneti_malloc(sizeof(*op));	/* XXX: use a free list */

  op->team     = team;
  op->sequence = sequence;
  op->flags    = flags;
  gasneti_atomic_set(&op->done, 0);
  gasnet_hsl_init(&op->lock);
  op->poll_fn  = (gasnete_coll_poll_fn)NULL;
                                                                                                              
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
  gasnete_coll_op_t *op;

  op = gasnete_coll_op_first();

  while (op != NULL) {
    gasnete_coll_op_t *next;
    int done = 0;

    /* Poll/kick the op, unless another thread is doing so */
    /* XXX: hsl_trylock does exist yet! */
    if (gasnet_hsl_trylock(&op->lock) == GASNET_OK) {
      if (op->poll_fn != NULL) {
        done = (*op->poll_fn)(op);
      }
      gasnet_hsl_unlock(&op->lock);
    }

    /* Get the next op in the active list, removing current if done.
       This is the only place items are removed from the active list and table. */
    if (done) {
      /* delete from active list and table */
      next = gasnete_coll_op_del(op);

      /* mark the op as completed */
      gasneti_atomic_set(&op->done, 1);
    } else {
      next = gasnete_coll_op_next(op);
    }
    op = next;
  }
}

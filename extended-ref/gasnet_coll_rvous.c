/*  $Archive:: /Ti/GASNet/extended-ref/gasnet_extended_refcoll.c $
 *     $Date: 2004/04/01 01:16:20 $
 * $Revision: 1.1.2.2 $
 * Description: Reference implemetation of GASNet Collectives
 * Copyright 2004, Dan Bonachea <bonachea@cs.berkeley.edu>
 * Terms of use are as specified in license.txt
 */

#ifndef GASNETI_GASNET_EXTENDED_COLL_C
  #error This file not meant to be compiled directly - included by gasnet_extended.c
#endif

/*---------------------------------------------------------------------------------*/
/* The list/table of active collective ops (coll ops) */
/* XXX: some (or even most) of this will need to be per-team scoped */

gasnet_hsl_t gasnete_coll_table_lock = GASNET_HSL_INITIALIZER;

uint32_t gasnete_coll_sequence = 0;

#ifndef GASNETE_COLL_TABLE_OVERRIDE
    /* Default implementation of the coll_ops list/table */
    static gasnete_coll_op_t *gasnete_coll_table_head = NULL;
    static gasnete_coll_op_t *gasnete_coll_table_tail = NULL;

    GASNET_INLINE_MODIFIER(gasnete_coll_table_first)
    gasnete_coll_op_t *gasnete_coll_table_first(void) {
      /* assert: gasnete_coll_table_lock held by current thread */
      return gasnete_coll_table_head;
    }
    GASNET_INLINE_MODIFIER(gasnete_coll_table_next)
    gasnete_coll_op_t *gasnete_coll_table_next(gasnete_coll_op_t *op) {
      /* assert: gasnete_coll_table_lock held by current thread */
      return op->table_next;
    }
    GASNET_INLINE_MODIFIER(gasnete_coll_table_del)
    void gasnete_coll_table_del(gasnete_coll_op_t *op) {
      /* assert: gasnete_coll_table_lock held by current thread */
      if (op->table_next) {
        op->table_next->table_prev = op->table_prev;
      } else {
        gasnete_coll_table_tail = op->table_prev;
      }
      if (op->table_prev) {
        op->table_prev->table_next = op->table_next;
      } else {
        gasnete_coll_table_head = op->table_next;
      }
    }
    GASNET_INLINE_MODIFIER(gasnete_coll_table_ins)
    void gasnete_coll_table_ins(gasnete_coll_op_t *op) {
      /* assert: gasnete_coll_table_lock held by current thread */
      op->table_next = NULL;
      op->table_prev = gasnete_coll_table_tail;
      if (gasnete_coll_table_tail) {
        gasnete_coll_table_tail->table_next = op;
      } else {
        gasnete_coll_table_head = op;
      }
      gasnete_coll_table_tail = op;
    }
    GASNET_INLINE_MODIFIER(gasnete_coll_table_find)
    gasnete_coll_op_t *gasnete_coll_table_find(gasnete_coll_team_t *team,
                                               uint32_t sequence) {
      /* assert: gasnete_coll_table_lock held by current thread */
      /* XXX: unimplemented */
      return NULL;
    }
#endif

/*---------------------------------------------------------------------------------*/

gasnete_coll_op_t *
gasnete_coll_create(gasnete_coll_team_t *team, uint32_t sequence, unsigned int flags)
{
  gasnete_coll_op_t *op;

  /* assert: gasnete_coll_table_lock held by current thread */

  op = gasneti_malloc(sizeof(*op));

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

  gasnete_coll_table_ins(op);

  return op;
}

void gasnete_coll_poll(void) {
  gasnete_coll_op_t *op;

  gasnet_hsl_lock(&gasnete_coll_table_lock);
  op = gasnete_coll_table_first();
  gasnet_hsl_unlock(&gasnete_coll_table_lock);

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
    gasnet_hsl_lock(&gasnete_coll_table_lock);
    next = gasnete_coll_table_next(op);
    if (done) {
      /* delete from active list and table */
      gasnete_coll_table_del(op);

      /* mark the op as completed */
      gasneti_atomic_set(&op->done, 1);
    }
    gasnet_hsl_unlock(&gasnete_coll_table_lock);
    op = next;
  }
}

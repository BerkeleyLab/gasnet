/*  $Archive:: /Ti/GASNet/extended-ref/gasnet_extended_refcoll.c $
 *     $Date: 2004/06/02 18:36:51 $
 * $Revision: 1.1.2.31 $
 * Description: Reference implemetation of GASNet Collectives
 * Copyright 2004, Dan Bonachea <bonachea@cs.berkeley.edu>
 * Terms of use are as specified in license.txt
 */

#ifndef GASNETI_GASNET_EXTENDED_COLL_C
  #error This file not meant to be compiled directly - included by gasnet_extended.c
#endif

/*---------------------------------------------------------------------------------*/
/* Forward decls and macros */

#define GASNETE_COLL_UNIMPLEMENTED() \
    (gasneti_fatalerror("%s unimplemented", GASNETI_CURRENT_FUNCTION), GASNET_COLL_INVALID_HANDLE)

/*---------------------------------------------------------------------------------*/
/* XXX: sequence and other stuff that will need to be per-team scoped: */

uint32_t gasnete_coll_sequence = 12345;	/* arbitrary non-zero starting value */
size_t *gasnete_coll_all_images;
size_t *gasnete_coll_all_offset;
size_t gasnete_coll_total_images;
size_t gasnete_coll_my_images;	/* count of local images */
size_t gasnete_coll_my_offset;	/* count of images before my first image */

#define GASNETE_COLL_1ST_IMAGE(LIST,NODE) \
	(((void * const *)(LIST))[gasnete_coll_all_offset[(NODE)]])
#define GASNETE_COLL_MY_1ST_IMAGE(LIST,FLAGS) \
	(((void * const *)(LIST))[((FLAGS) & GASNET_COLL_LOCAL) ? 0 : gasnete_coll_my_offset])

/*---------------------------------------------------------------------------------*/

int gasnete_coll_init_done = 0;

void gasnete_coll_validate(gasnet_team_handle_t team,
			   gasnet_node_t dstnode, const void *dst, size_t dstlen, int dstisv,
			   gasnet_node_t srcnode, const void *src, size_t srclen, int srcisv,
			   int flags) {
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

  /* Bounds check any local portion of dst/dstlist*/
  if ((dstnode == gasnete_mynode) && (flags & GASNET_COLL_DST_IN_SEGMENT)) {
    if (!dstisv) {
      gasnete_boundscheck(gasnete_mynode, dst, dstlen);
    } else {
      void * const *p = &GASNETE_COLL_1ST_IMAGE(dst, flags & GASNET_COLL_LOCAL);
      size_t limit = gasnete_coll_my_images;
      for (i = 0; i < limit; ++i, ++p) {
	gasnete_boundscheck(gasnete_mynode, *p, dstlen);
      }
    }
  }

  /* Bounds check any local portion of src/srclist*/
  if ((srcnode == gasnete_mynode) && (flags & GASNET_COLL_SRC_IN_SEGMENT)) {
    if (!srcisv) {
      gasnete_boundscheck(gasnete_mynode, src, srclen);
    } else {
      void * const *p = &GASNETE_COLL_1ST_IMAGE(src, flags & GASNET_COLL_LOCAL);
      size_t limit = gasnete_coll_my_images;
      for (i = 0; i < limit; ++i, ++p) {
	gasnete_boundscheck(gasnete_mynode, *p, srclen);
      }
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
  extern gasnet_coll_handle_t gasnete_coll_handle_create(GASNETE_THREAD_FARG_ALONE) {
    gasnete_coll_threaddata_t *td = GASNETE_COLL_MYTHREAD;
    gasnet_coll_handle_t result;

    result = td->handle_freelist;
    if_pt (result) {
      td->handle_freelist = (gasnet_coll_handle_t)(*result);
    } else {
      /* XXX: allocate in large chunks and scatter across cache lines */
      /* XXX: destroy freelist at exit */
      result = (gasnet_coll_handle_t)gasneti_malloc(sizeof(*result));
    }

    *result = 0;
    return result;
  }

  extern void gasnete_coll_handle_signal(gasnet_coll_handle_t handle GASNETE_THREAD_FARG) {
    gasneti_assert(handle != GASNET_COLL_INVALID_HANDLE);
    *handle = 1;
  }

  GASNET_INLINE_MODIFIER(gasnete_coll_handle_done)
  int gasnete_coll_handle_done(gasnet_coll_handle_t handle GASNETE_THREAD_FARG) {
    int result = 0;
    gasneti_assert(handle != GASNET_COLL_INVALID_HANDLE);

    if_pf (*handle != 0) {
      gasnete_coll_threaddata_t *td = GASNETE_COLL_MYTHREAD_NOALLOC;
      *handle = (uintptr_t)(td->handle_freelist);
      td->handle_freelist = handle;
      result = 1;
    }

    return result;
  }

  extern int gasnete_coll_try_sync(gasnet_coll_handle_t handle GASNETE_THREAD_FARG) {
    gasneti_assert(handle != GASNET_COLL_INVALID_HANDLE); /* caller must check */

    gasnet_AMPoll();
    gasnete_coll_poll(GASNETE_THREAD_PASS_ALONE);

    return gasnete_coll_handle_done(handle GASNETE_THREAD_PASS) ? GASNET_OK : GASNET_ERR_NOT_READY;
  }

  extern int gasnete_coll_try_sync_some(gasnet_coll_handle_t *phandle, size_t numhandles GASNETE_THREAD_FARG) {
    int empty = 1;
    int result = GASNET_ERR_NOT_READY;
    int i;

    gasneti_assert(phandle != NULL);

    gasnet_AMPoll();
    gasnete_coll_poll(GASNETE_THREAD_PASS_ALONE);

    for (i = 0; i < numhandles; ++i, ++phandle) {
      if (*phandle != GASNET_COLL_INVALID_HANDLE) {
	empty = 0;
	if (gasnete_coll_handle_done(*phandle GASNETE_THREAD_PASS)) {
	  *phandle = GASNET_COLL_INVALID_HANDLE;
	  result = GASNET_OK;
	}
      }
    }

    return empty ? GASNET_OK : result;
  }

  extern int gasnete_coll_try_sync_all(gasnet_coll_handle_t *phandle, size_t numhandles GASNETE_THREAD_FARG) {
    int result = GASNET_OK;
    int i;

    gasneti_assert(phandle != NULL);

    gasnet_AMPoll();
    gasnete_coll_poll(GASNETE_THREAD_PASS_ALONE);

    for (i = 0; i < numhandles; ++i, ++phandle) {
      if (*phandle != GASNET_COLL_INVALID_HANDLE) {
	if (gasnete_coll_handle_done(*phandle GASNETE_THREAD_PASS)) {
	  *phandle = GASNET_COLL_INVALID_HANDLE;
	} else {
	  result = GASNET_ERR_NOT_READY;
	}
      }
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
/* The per-thread list of active collective ops (coll ops) */

/* There exists a per-thread "active list".
 * Ops in the active table will be polled to make progress.
 *
 * Operations of the active list
 *   void gasnete_coll_active_init_td(td)
 *   void gasnete_coll_active_fini_td(td)
 *   gasnete_coll_op_t *gasnete_coll_active_first(th)
 *	Return the first coll op in the active list.
 *   gasnete_coll_op_t *gasnete_coll_active_next(op)
 *	Iterate over the coll ops in the active list.
 *   void gasnete_coll_active_new(op)
 *	Init active list fields of a coll op.
 *   void gasnete_coll_active_ins(op, th)
 *	Add a coll op to the active list.
 *   void gasnete_coll_active_del(op, th)
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

    gasnete_coll_op_t *gasnete_coll_active_first(GASNETE_THREAD_FARG_ALONE) {
      gasnete_coll_threaddata_t *td = GASNETE_COLL_MYTHREAD;
      return td->active_head;
    }

    gasnete_coll_op_t *gasnete_coll_active_next(gasnete_coll_op_t *op) {
      return op->active_next;
    }

    void gasnete_coll_active_new(gasnete_coll_op_t *op) {
      op->active_next = NULL;
      op->active_prev_p = &(op->active_next);
    }

    void gasnete_coll_active_ins(gasnete_coll_op_t *op GASNETE_THREAD_FARG) {
      gasnete_coll_threaddata_t *td = GASNETE_COLL_MYTHREAD_NOALLOC;
      *(td->active_tail_p) = op;
      op->active_prev_p = td->active_tail_p;
      td->active_tail_p = &(op->active_next);
    }

    void gasnete_coll_active_del(gasnete_coll_op_t *op GASNETE_THREAD_FARG) {
      gasnete_coll_op_t *next = op->active_next;
      *(op->active_prev_p) = next;
      if (next) {
	next->active_prev_p = op->active_prev_p;
      } else {
	GASNETE_COLL_MYTHREAD_NOALLOC->active_tail_p = op->active_prev_p;
      }
    }

    void
    gasnete_coll_active_init_td(gasnete_coll_threaddata_t *td) {
      td->active_head = NULL;
      td->active_tail_p = &(td->active_head);
    }

    void
    gasnete_coll_active_fini_td(gasnete_coll_threaddata_t *td) {
      gasneti_assert(td->active_head == NULL);
    }
#endif

/*---------------------------------------------------------------------------------*/

extern gasnete_coll_threaddata_t *gasnete_coll_new_threaddata(void) {
    gasnete_coll_threaddata_t *result = gasneti_calloc(1,sizeof(*result));
    gasnete_coll_active_init_td(result);
    return result;
}

/*---------------------------------------------------------------------------------*/
/* Aggregation/filtering */

/* interface:
 *   gasnet_coll_handle_t gasnete_coll_op_submit(op, handle, th)
 *	Place coll_op in active list or not, as desired/required.
 *   void gasnete_coll_op_complete(op, poll_result);
 *	Completion hook
 *
 */

#ifndef GASNETE_COLL_AGG_OVERRIDE
    /* Default implementation of aggregation/filtering */

    /* XXX: how will teams interact w/ aggregation? */

    static gasnete_coll_op_t *gasnete_coll_agg = NULL;

    gasnet_coll_handle_t
    gasnete_coll_op_submit(gasnete_coll_op_t *op, gasnet_coll_handle_t handle GASNETE_THREAD_FARG) {
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
	  head = gasnete_coll_agg = gasnete_coll_op_create(op->team, 0, 0 GASNETE_THREAD_PASS);
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
      gasnete_coll_active_ins(op GASNETE_THREAD_PASS);

      return handle;
    }

    void gasnete_coll_op_complete(gasnete_coll_op_t *op, int poll_result GASNETE_THREAD_FARG) {

      if (poll_result & GASNETE_COLL_OP_COMPLETE) {
	if_pt (op->handle != GASNET_COLL_INVALID_HANDLE) {
	    /* Normal case, just signal the handle */
	    gasnete_coll_handle_signal(op->handle GASNETE_THREAD_PASS);
	    gasneti_assert(op->agg_head == NULL);
	} else if (op->agg_next) {
	  gasnete_coll_op_t *head;

	  /* Remove this member from the aggregate */
	  op->agg_next->agg_prev = op->agg_prev;
	  op->agg_prev->agg_next = op->agg_next;

	  /* If the container op exists and is now empty, mark it's handle as done. */
	  head = op->agg_head;
	  if (head && (head->agg_next == head)) {
	    gasnete_coll_handle_signal(head->handle GASNETE_THREAD_PASS);
	    gasnete_coll_op_destroy(head GASNETE_THREAD_PASS);
	  }
	}
      }

      if (poll_result & GASNETE_COLL_OP_INACTIVE) {
	/* delete from the active list and destoy */
	gasnete_coll_active_del(op GASNETE_THREAD_PASS);
	gasnete_coll_op_destroy(op GASNETE_THREAD_PASS);
      }
    }
#endif

/*---------------------------------------------------------------------------------*/
gasnete_coll_op_t *
gasnete_coll_op_create(gasnete_coll_team_t team, uint32_t sequence, int flags GASNETE_THREAD_FARG) {
  gasnete_coll_threaddata_t *td = GASNETE_COLL_MYTHREAD;
  gasnete_coll_op_t *op;

  op = td->op_freelist;
  if_pt (op != NULL) {
    td->op_freelist = *((gasnete_coll_op_t **)op);
  } else {
    /* XXX: allocate in chunks and scatter across cache lines */
    /* XXX: destroy freelist at exit */
    op = (gasnete_coll_op_t *)gasneti_malloc(sizeof(gasnete_coll_op_t));
  }

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
gasnete_coll_op_destroy(gasnete_coll_op_t *op GASNETE_THREAD_FARG) {
  gasnete_coll_threaddata_t *td = GASNETE_COLL_MYTHREAD_NOALLOC;
  *((gasnete_coll_op_t **)op) =  td->op_freelist;
  td->op_freelist = op;
}

void gasnete_coll_poll(GASNETE_THREAD_FARG_ALONE) {
  gasnete_coll_op_t *op;

  op = gasnete_coll_active_first(GASNETE_THREAD_PASS_ALONE);

  while (op != NULL) {
    gasnete_coll_op_t *next = gasnete_coll_active_next(op);
    int poll_result = 0;

    /* Poll/kick the op */
    gasneti_assert(op->poll_fn != (gasnete_coll_poll_fn)NULL);
    poll_result = (*op->poll_fn)(op GASNETE_THREAD_PASS);
    if (poll_result != 0) {
      gasnete_coll_op_complete(op, poll_result GASNETE_THREAD_PASS);
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

    /* XXX free list could/should be per team: */
    static gasnete_coll_p2p_t *gasnete_coll_p2p_freelist = NULL;

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
	size_t entry_size = gasnete_coll_total_images * sizeof(gasnete_coll_p2p_entry_t);

	p2p = gasnete_coll_p2p_freelist;	/* XXX: per-team */

	if_pf (p2p == NULL) {
	  /* Round to 8-byte alignment of entry array */
	  size_t alloc_size = ((sizeof(gasnete_coll_p2p_t) + 7) & ~7) + entry_size;
	  p2p = (gasnete_coll_p2p_t *)gasneti_malloc(alloc_size);
	  p2p->entry = (gasnete_coll_p2p_entry_t *)((uintptr_t)p2p + ((sizeof(gasnete_coll_p2p_t) + 7) & ~7));
	  p2p->p2p_next = NULL;
	}

	memset(p2p->entry, 0, entry_size);

	p2p->team_id = team_id;
	p2p->sequence = sequence;

	gasnete_coll_p2p_freelist = p2p->p2p_next;
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

    void gasnete_coll_p2p_free(gasnete_coll_p2p_t *p2p) {
      gasneti_assert(p2p != NULL);

      gasnet_hsl_lock(&gasnete_coll_p2p_table_lock);

      p2p->p2p_prev->p2p_next = p2p->p2p_next;
      p2p->p2p_next->p2p_prev = p2p->p2p_prev;

      p2p->p2p_next = gasnete_coll_p2p_freelist;	/* XXX: per-team */
      gasnete_coll_p2p_freelist = p2p;

      gasnet_hsl_unlock(&gasnete_coll_p2p_table_lock);
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
	GASNETE_FAST_UNALIGNED_MEMCPY(entry->u.data, buf, nbytes);
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

      entry->u.addr = addr;
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
    void gasnete_coll_p2p_signalling_put(gasnete_coll_op_t *op, gasnet_node_t dstnode, void *dst,
					 void *src, size_t nbytes, uint32_t pos, uint32_t state) {
      uint32_t team_id = gasnete_coll_team_id(op->team);

      gasneti_assert(nbytes <= gasnet_AMMaxLongRequest());

      GASNETE_SAFE(
	LONG_REQ(4,4,(dstnode, gasneti_handleridx(gasnete_coll_p2p_put_reqh),
		      src, nbytes, dst, team_id, op->sequence, pos, state)));
    }

    /* Send up to GASNETE_COLL_P2P_EAGER_LIMIT bytes to be buffered at the recipient */
    void gasnete_coll_p2p_eager_put(gasnete_coll_op_t *op, gasnet_node_t dstnode,
				    void *src, size_t nbytes, uint32_t pos, uint32_t state) {
      uint32_t team_id = gasnete_coll_team_id(op->team);

      gasneti_assert(nbytes <= GASNETE_COLL_P2P_EAGER_LIMIT);

      GASNETE_SAFE(
	MEDIUM_REQ(4,4,(dstnode, gasneti_handleridx(gasnete_coll_p2p_eager_reqh),
			src, nbytes, team_id, op->sequence, pos, state)));
    }

    /* Send a single address to be buffered at the recipient */
    void gasnete_coll_p2p_rendezvous(gasnete_coll_op_t *op, gasnet_node_t dstnode,
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

/* Flags for gasnete_coll_generic_data_t->options: */
#define GASNETE_COLL_GENERIC_OPT_INSYNC	0x0001
#define GASNETE_COLL_GENERIC_OPT_OUTSYNC	0x0002
#define GASNETE_COLL_GENERIC_OPT_P2P		0x0004

/* Macros for conditionally setting flags in gasnete_coll_generic_data_t->options; */
#define GASNETE_COLL_GENERIC_OPT_INSYNC_IF(COND)	((COND) ? GASNETE_COLL_GENERIC_OPT_INSYNC : 0)
#define GASNETE_COLL_GENERIC_OPT_OUTSYNC_IF(COND)	((COND) ? GASNETE_COLL_GENERIC_OPT_OUTSYNC : 0)
#define GASNETE_COLL_GENERIC_OPT_P2P_IF(COND)		((COND) ? GASNETE_COLL_GENERIC_OPT_P2P : 0)

typedef struct {
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

      gasnete_threaddata_t		*thread;
    #else
      #define GASNETE_COLL_GENERIC_SET_TAG(D,T)
    #endif

    int					state;
    int					options;
    gasnete_coll_consensus_t		in_barrier;
    gasnete_coll_consensus_t		out_barrier;
    gasnete_coll_p2p_t			*p2p;
    gasnet_handle_t			handle;
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

    /* Hook for conduit-specific extension */
    #ifdef GASNETE_COLL_GENERIC_EXTRA
      GASNETE_COLL_GENERIC_EXTRA
    #endif
} gasnete_coll_generic_data_t;

#define GASNETE_COLL_GENERIC_ARGS(D,T) \
		(gasneti_assert((D) != NULL),                               \
		 gasneti_assert((D)->thread == GASNETE_MYTHREAD),           \
		 gasneti_assert((D)->tag == GASNETE_COLL_GENERIC_TAG(T)),   \
		 &((D)->args.T))

GASNET_INLINE_MODIFIER(gasnete_coll_generic_alloc)
gasnete_coll_generic_data_t *gasnete_coll_generic_alloc(GASNETE_THREAD_FARG_ALONE) {
    gasnete_coll_threaddata_t *td = GASNETE_COLL_MYTHREAD;
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
    #if GASNET_DEBUG
      result->thread = GASNETE_MYTHREAD;
    #endif

    return result;
}

GASNET_INLINE_MODIFIER(gasnete_coll_generic_free)
void gasnete_coll_generic_free(gasnete_coll_generic_data_t *data GASNETE_THREAD_FARG) {
    gasnete_coll_threaddata_t *td = GASNETE_COLL_MYTHREAD_NOALLOC;

    gasneti_assert(data != NULL);
    gasneti_assert(data->thread == GASNETE_MYTHREAD);

    if (data->options & GASNETE_COLL_GENERIC_OPT_P2P) {
      gasnete_coll_p2p_free(data->p2p);
    }

    *((gasnete_coll_generic_data_t **)data) =  td->generic_data_freelist;
    td->generic_data_freelist = data;
}

/* Generic routine to create an op and enter it in the active list, etc..
 * Caller provides 'data' and 'poll_fn' specific to the operation.
 * Handle is allocated automatically if flags don't indicate aggregation.
 *
 * Just returns the handle.
 */
gasnet_coll_handle_t
gasnete_coll_op_generic_init(gasnete_coll_team_t team, int flags,
			     gasnete_coll_generic_data_t *data, gasnete_coll_poll_fn poll_fn
			     GASNETE_THREAD_FARG) {
      gasnet_coll_handle_t handle = GASNET_COLL_INVALID_HANDLE;
      gasnete_coll_op_t *op;
      uint32_t sequence;

      gasneti_assert(team == GASNET_TEAM_ALL);
      gasneti_assert(data != NULL);

      /* Unconditionally allocate a sequence number */
      sequence = gasnete_coll_sequence++;	/* XXX: need team scope */

      /* Conditionally allocate barriers */
      /* XXX: this is where we could do some aggregation of syncs */
      if (data->options & GASNETE_COLL_GENERIC_OPT_INSYNC) {
	data->in_barrier = gasnete_coll_consensus_create();
      }
      if (data->options & GASNETE_COLL_GENERIC_OPT_OUTSYNC) {
	data->out_barrier = gasnete_coll_consensus_create();
      }

      /* Conditionally allocate data for point-to-point syncs */
      if (data->options & GASNETE_COLL_GENERIC_OPT_P2P) {
	data->p2p = gasnete_coll_p2p_get(gasnete_coll_team_id(team), sequence);
      }

      /* Conditionally allocate a handle */
      if_pt (!(flags & GASNET_COLL_AGGREGATE)) {
	handle = gasnete_coll_handle_create(GASNETE_THREAD_PASS_ALONE);
      }

      /* Create the op */
      op = gasnete_coll_op_create(team, sequence, flags GASNETE_THREAD_PASS);
      op->data = data;
      op->poll_fn = poll_fn;

      /* Submit the op via aggregation filter */
      return gasnete_coll_op_submit(op, handle GASNETE_THREAD_PASS);
}

GASNET_INLINE_MODIFIER(gasnete_coll_generic_syncnb)
int gasnete_coll_generic_syncnb(gasnete_coll_generic_data_t *data) {
  gasnet_handle_t handle = data->handle;
  int result = 1;

  if_pt (handle != GASNET_INVALID_HANDLE)
    result = (gasnete_try_syncnb(handle) == GASNET_OK);

  return result;
}

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

/*---------------------------------------------------------------------------------*/
/* gasnete_coll_broadcast_nb() */

/* bcast Get: all nodes perform uncoordinated gets */
/* Valid for SINGLE only, any size */
extern int gasnete_coll_pf_bcast_Get(gasnete_coll_op_t *op GASNETE_THREAD_FARG) {
  gasnete_coll_generic_data_t *data = op->data;
  const gasnete_coll_broadcast_args_t *args = GASNETE_COLL_GENERIC_ARGS(data, broadcast);
  int result = 0;

  switch (data->state) {
    case 0:
      if (!gasnete_coll_generic_insync(data)) {
	break;
      }

      if (gasnete_mynode == args->srcnode) {
	GASNETE_FAST_UNALIGNED_MEMCPY(args->dst, args->src, args->nbytes);
      } else {
	data->handle = gasnete_get_nb_bulk(args->dst, args->srcnode, args->src,
					   args->nbytes GASNETE_THREAD_PASS);
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

      gasnete_coll_generic_free(data GASNETE_THREAD_PASS);
      result = (GASNETE_COLL_OP_COMPLETE | GASNETE_COLL_OP_INACTIVE);
  }

  return result;
}

/* bcast Put: root node performs carefully ordered puts */
/* Valid for SINGLE only, any size */
extern int gasnete_coll_pf_bcast_Put(gasnete_coll_op_t *op GASNETE_THREAD_FARG) {
  gasnete_coll_generic_data_t *data = op->data;
  const gasnete_coll_broadcast_args_t *args = GASNETE_COLL_GENERIC_ARGS(data, broadcast);
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
	gasnete_begin_nbi_accessregion(1 GASNETE_THREAD_PASS);
	{
	  int i;

	  /* Put to nodes to the "right" of ourself */
	  for (i = gasnete_mynode + 1; i < gasnete_nodes; ++i) {
	    gasnete_put_nbi_bulk(i, dst, src, nbytes GASNETE_THREAD_PASS);
	  }
	  /* Put to nodes to the "left" of ourself */
	  for (i = 0; i < gasnete_mynode; ++i) {
	    gasnete_put_nbi_bulk(i, dst, src, nbytes GASNETE_THREAD_PASS);
	  }
	}
	data->handle = gasnete_end_nbi_accessregion(GASNETE_THREAD_PASS_ALONE);

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

      gasnete_coll_generic_free(data GASNETE_THREAD_PASS);
      result = (GASNETE_COLL_OP_COMPLETE | GASNETE_COLL_OP_INACTIVE);
  }

  return result;
}

/* bcast Eager: root node performs carefully ordered eager puts */
/* Valid for SINGLE and LOCAL, size <= GASNETE_COLL_P2P_EAGER_LIMIT */
/* Requires GASNETE_COLL_GENERIC_OPT_P2P on non-root nodes */
extern int gasnete_coll_pf_bcast_Eager(gasnete_coll_op_t *op GASNETE_THREAD_FARG) {
  gasnete_coll_generic_data_t *data = op->data;
  const gasnete_coll_broadcast_args_t *args = GASNETE_COLL_GENERIC_ARGS(data, broadcast);
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
	  gasnete_coll_p2p_eager_put(op, i, src, nbytes, 0, 1);
	}
	/* Send to nodes to the "left" of ourself */
	for (i = 0; i < gasnete_mynode; ++i) {
	  gasnete_coll_p2p_eager_put(op, i, src, nbytes, 0, 1);
	}

	/* Do local copy, perhaps overlapping with communication */
	GASNETE_FAST_UNALIGNED_MEMCPY(args->dst, src, nbytes);
      }
      data->state = 1;

    case 1:
      if (gasnete_mynode != args->srcnode) {
	gasnete_coll_p2p_entry_t *entry;

	gasneti_assert(data->p2p);
	entry = &(data->p2p->entry[0]);

	if (!entry->state) {
	  break;
	}

	GASNETE_FAST_UNALIGNED_MEMCPY(args->dst, entry->u.data, args->nbytes);
      }
      data->state = 2;

    case 2:
      if (!gasnete_coll_generic_outsync(data)) {
	break;
      }

      gasnete_coll_generic_free(data GASNETE_THREAD_PASS);
      result = (GASNETE_COLL_OP_COMPLETE | GASNETE_COLL_OP_INACTIVE);
  }

  return result;
}

/* bcast RVGet: root node broadcasts address, others get from that address */
/* Valid for SINGLE and LOCAL, any size */
/* Requires GASNETE_COLL_GENERIC_OPT_P2P on non-root nodes */
extern int gasnete_coll_pf_bcast_RVGet(gasnete_coll_op_t *op GASNETE_THREAD_FARG) {
  gasnete_coll_generic_data_t *data = op->data;
  const gasnete_coll_broadcast_args_t *args = GASNETE_COLL_GENERIC_ARGS(data, broadcast);
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
	  gasnete_coll_p2p_rendezvous(op, i, src, 0, 1);
	}
	/* Send to nodes to the "left" of ourself */
	for (i = 0; i < gasnete_mynode; ++i) {
	  gasnete_coll_p2p_rendezvous(op, i, src, 0, 1);
	}

	/* Do local copy, perhaps overlapping with communication */
	GASNETE_FAST_UNALIGNED_MEMCPY(args->dst, src, args->nbytes);
      }
      data->state = 1;

    case 1:
      if (gasnete_mynode != args->srcnode) {
	gasnete_coll_p2p_entry_t *entry;
	void *src;

	gasneti_assert(data->p2p);
	entry = &(data->p2p->entry[0]);

	if (!entry->state) {
	  break;
	}

	data->handle = gasnete_get_nb_bulk(args->dst, args->srcnode, entry->u.addr,
					   args->nbytes GASNETE_THREAD_PASS);
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

      gasnete_coll_generic_free(data GASNETE_THREAD_PASS);
      result = (GASNETE_COLL_OP_COMPLETE | GASNETE_COLL_OP_INACTIVE);
  }

  return result;
}

extern gasnet_coll_handle_t
gasnete_coll_generic_broadcast_nb(gasnet_team_handle_t team,
				  void *dst,
				  gasnet_node_t srcnode, void *src,
				  size_t nbytes, int flags,
				  gasnete_coll_poll_fn poll_fn, int options
				  GASNETE_THREAD_FARG) {
    gasnete_coll_generic_data_t *data = gasnete_coll_generic_alloc(GASNETE_THREAD_PASS_ALONE);
    GASNETE_COLL_GENERIC_SET_TAG(data, broadcast);
    data->args.broadcast.dst     = dst;
    data->args.broadcast.srcnode = srcnode;
    data->args.broadcast.src     = src;
    data->args.broadcast.nbytes  = nbytes;
    data->options = options;
    return gasnete_coll_op_generic_init(team, flags, data, poll_fn GASNETE_THREAD_PASS);
}

#ifndef gasnete_coll_broadcast_nb
    extern gasnet_coll_handle_t
    gasnete_coll_broadcast_nb(gasnet_team_handle_t team,
			      void *dst,
			      gasnet_node_t srcnode, void *src,
			      size_t nbytes, int flags GASNETE_THREAD_FARG)
    {
      gasnete_coll_poll_fn poll_fn;
      int in_sync  = GASNETE_COLL_IN_MODE(flags);
      int out_sync = GASNETE_COLL_OUT_MODE(flags);
      int options;

      /* Choose algorithm based on arguments */
      if ((in_sync == GASNET_COLL_IN_MYSYNC) || (flags & GASNET_COLL_LOCAL)) {
	if (nbytes <= GASNETE_COLL_P2P_EAGER_LIMIT) {
	  options = GASNETE_COLL_GENERIC_OPT_OUTSYNC_IF(out_sync == GASNET_COLL_OUT_ALLSYNC) |
		    GASNETE_COLL_GENERIC_OPT_P2P_IF(gasnete_mynode != srcnode);
	  poll_fn = &gasnete_coll_pf_bcast_Eager;
	} else {
	  options = GASNETE_COLL_GENERIC_OPT_OUTSYNC_IF(out_sync != GASNET_COLL_OUT_NOSYNC) |
		    GASNETE_COLL_GENERIC_OPT_P2P_IF(gasnete_mynode != srcnode);
	  poll_fn = &gasnete_coll_pf_bcast_RVGet;
	}
      } else if ((out_sync == GASNET_COLL_OUT_MYSYNC) && (nbytes <= GASNETE_COLL_P2P_EAGER_LIMIT)) {
	options = GASNETE_COLL_GENERIC_OPT_P2P_IF(gasnete_mynode != srcnode);
	poll_fn = &gasnete_coll_pf_bcast_Eager;
      } else {
	options = GASNETE_COLL_GENERIC_OPT_OUTSYNC_IF(out_sync != GASNET_COLL_OUT_NOSYNC);
	poll_fn = &gasnete_coll_pf_bcast_Put;
      }
      options |= GASNETE_COLL_GENERIC_OPT_INSYNC_IF(in_sync == GASNET_COLL_IN_ALLSYNC);

      return gasnete_coll_generic_broadcast_nb(team, dst, srcnode, src, nbytes, flags,
					       poll_fn, options GASNETE_THREAD_PASS);

    }
#endif

/*---------------------------------------------------------------------------------*/
/* gasnete_coll_broadcastM_nb() */

/* bcastM Get: all nodes perform uncoordinated gets */
/* Valid for SINGLE only, any size */
extern int gasnete_coll_pf_bcastM_Get(gasnete_coll_op_t *op GASNETE_THREAD_FARG) {
  gasnete_coll_generic_data_t *data = op->data;
  const gasnete_coll_broadcastM_args_t *args = GASNETE_COLL_GENERIC_ARGS(data, broadcastM);
  int result = 0;

  switch (data->state) {
    case 0:
      if (!gasnete_coll_generic_insync(data)) {
	break;
      }

      /* Get only the 1st local image */
      if (gasnete_mynode == args->srcnode) {
	GASNETE_FAST_UNALIGNED_MEMCPY(GASNETE_COLL_MY_1ST_IMAGE(args->dstlist, op->flags),
				      args->src, args->nbytes);
      } else {
	data->handle = gasnete_get_nb_bulk(GASNETE_COLL_MY_1ST_IMAGE(args->dstlist, op->flags),
					   args->srcnode, args->src, args->nbytes GASNETE_THREAD_PASS);
      }
      data->state = 1;

    case 1:
      if (!gasnete_coll_generic_syncnb(data)) {
	break;
      }

      /* Copy our 1st image to any additional images */
      if (gasnete_coll_my_images > 1) {
	size_t nbytes = args->nbytes;
	void * const *p = &GASNETE_COLL_MY_1ST_IMAGE(args->dstlist, op->flags);
	void *p0 = *(p++);
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

      gasnete_coll_generic_free(data GASNETE_THREAD_PASS);
      result = (GASNETE_COLL_OP_COMPLETE | GASNETE_COLL_OP_INACTIVE);
  }

  return result;
}

/* bcastM Put: root node performs carefully ordered puts */
/* Valid for SINGLE only, any size */
extern int gasnete_coll_pf_bcastM_Put(gasnete_coll_op_t *op GASNETE_THREAD_FARG) {
  gasnete_coll_generic_data_t *data = op->data;
  const gasnete_coll_broadcastM_args_t *args = GASNETE_COLL_GENERIC_ARGS(data, broadcastM);
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
	gasnete_begin_nbi_accessregion(1 GASNETE_THREAD_PASS);
	{
	  /* Put to nodes to the "right" of ourself */
	  if (gasnete_mynode < gasnete_nodes - 1) {
	    p = &GASNETE_COLL_1ST_IMAGE(args->dstlist, gasnete_mynode + 1);
	    for (i = gasnete_mynode + 1; i < gasnete_nodes; ++i) {
	      limit = gasnete_coll_all_images[i];
	      for (j = 0; j < limit; ++j) {
		gasnete_put_nbi_bulk(i, *p, src, nbytes GASNETE_THREAD_PASS);
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
		gasnete_put_nbi_bulk(i, *p, src, nbytes GASNETE_THREAD_PASS);
		++p;
	      }
	    }
	  }
	}
	data->handle = gasnete_end_nbi_accessregion(GASNETE_THREAD_PASS_ALONE);

	/* Do local copy LAST, perhaps overlapping with communication */
	/* XXX: for large sizes we should segment this in-memory broadcast */
	p = &GASNETE_COLL_MY_1ST_IMAGE(args->dstlist, op->flags);
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

      gasnete_coll_generic_free(data GASNETE_THREAD_PASS);
      result = (GASNETE_COLL_OP_COMPLETE | GASNETE_COLL_OP_INACTIVE);
  }

  return result;
}

/* bcastM Eager: root node performs carefully ordered eager puts */
/* Valid for SINGLE and LOCAL, size <= GASNETE_COLL_P2P_EAGER_LIMIT */
/* Requires GASNETE_COLL_GENERIC_OPT_P2P on non-root nodes */
extern int gasnete_coll_pf_bcastM_Eager(gasnete_coll_op_t *op GASNETE_THREAD_FARG) {
  gasnete_coll_generic_data_t *data = op->data;
  const gasnete_coll_broadcastM_args_t *args = GASNETE_COLL_GENERIC_ARGS(data, broadcastM);
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
	  gasnete_coll_p2p_eager_put(op, i, src, nbytes, 0, 1);
	}
	/* Send to nodes to the "left" of ourself */
	for (i = 0; i < gasnete_mynode; ++i) {
	  gasnete_coll_p2p_eager_put(op, i, src, nbytes, 0, 1);
	}

	/* Do local copy LAST, perhaps overlapping with communication */
	p = &GASNETE_COLL_MY_1ST_IMAGE(args->dstlist, op->flags);
	for (j = 0; j < gasnete_coll_my_images; ++j, ++p) {
	  GASNETE_FAST_UNALIGNED_MEMCPY(*p, src, nbytes);
	}
      }
      data->state = 1;

    case 1:
      if (gasnete_mynode != args->srcnode) {
	gasnete_coll_p2p_entry_t *entry;
	size_t nbytes;
	void * const *p;
	int j;

	gasneti_assert(data->p2p);
	entry = &(data->p2p->entry[0]);

	if (!entry->state) {
	  break;
	}

	p = &GASNETE_COLL_MY_1ST_IMAGE(args->dstlist, op->flags);
	nbytes = args->nbytes;
	for (j = 0; j < gasnete_coll_my_images; ++j, ++p) {
	  GASNETE_FAST_UNALIGNED_MEMCPY(*p, entry->u.data, nbytes);
	}
      }
      data->state = 2;

    case 2:
      if (!gasnete_coll_generic_outsync(data)) {
	break;
      }

      gasnete_coll_generic_free(data GASNETE_THREAD_PASS);
      result = (GASNETE_COLL_OP_COMPLETE | GASNETE_COLL_OP_INACTIVE);
  }

  return result;
}

/* bcastM RVGet: root node broadcasts address, others get from that address */
/* Valid for SINGLE and LOCAL, any size */
/* Requires GASNETE_COLL_GENERIC_OPT_P2P on non-root nodes */
extern int gasnete_coll_pf_bcastM_RVGet(gasnete_coll_op_t *op GASNETE_THREAD_FARG) {
  gasnete_coll_generic_data_t *data = op->data;
  const gasnete_coll_broadcastM_args_t *args = GASNETE_COLL_GENERIC_ARGS(data, broadcastM);
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
	  gasnete_coll_p2p_rendezvous(op, i, src, 0, 1);
	}
	/* Send to nodes to the "left" of ourself */
	for (i = 0; i < gasnete_mynode; ++i) {
	  gasnete_coll_p2p_rendezvous(op, i, src, 0, 1);
	}

	/* Do local copy LAST, perhaps overlapping with communication */
	/* XXX: for large sizes we should segment this in-memory broadcast */
	p = &GASNETE_COLL_MY_1ST_IMAGE(args->dstlist, op->flags);
	nbytes = args->nbytes;
	for (j = 0; j < gasnete_coll_my_images; ++j, ++p) {
	  GASNETE_FAST_UNALIGNED_MEMCPY(*p, src, nbytes);
	}
      }
      data->state = 1;

    case 1:
      if (gasnete_mynode != args->srcnode) {
	gasnete_coll_p2p_entry_t *entry;

	gasneti_assert(data->p2p);
	entry = &(data->p2p->entry[0]);

	if (!entry->state) {
	  break;
	}

	/* Get 1st image only */
	data->handle = gasnete_get_nb_bulk(GASNETE_COLL_MY_1ST_IMAGE(args->dstlist, op->flags),
					   args->srcnode, entry->u.addr,
					   args->nbytes GASNETE_THREAD_PASS);
      }
      data->state = 2;

    case 2:
      if (gasnete_mynode != args->srcnode) {
	if (!gasnete_coll_generic_syncnb(data)) {
	  break;
	} else {
	  void * const *p = &GASNETE_COLL_MY_1ST_IMAGE(args->dstlist, op->flags);
	  void *p0 = *(p++);
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

      gasnete_coll_generic_free(data GASNETE_THREAD_PASS);
      result = (GASNETE_COLL_OP_COMPLETE | GASNETE_COLL_OP_INACTIVE);
  }

  return result;
}

extern gasnet_coll_handle_t
gasnete_coll_generic_broadcastM_nb(gasnet_team_handle_t team,
				   void * const dstlist[],
				   gasnet_node_t srcnode, void *src,
				   size_t nbytes, int flags,
				   gasnete_coll_poll_fn poll_fn, int options
				   GASNETE_THREAD_FARG) {
    gasnete_coll_generic_data_t *data = gasnete_coll_generic_alloc(GASNETE_THREAD_PASS_ALONE);
    GASNETE_COLL_GENERIC_SET_TAG(data, broadcastM);
    data->args.broadcastM.dstlist = dstlist;
    data->args.broadcastM.srcnode = srcnode;
    data->args.broadcastM.src     = src;
    data->args.broadcastM.nbytes  = nbytes;
    data->options = options;
    return gasnete_coll_op_generic_init(team, flags, data, poll_fn GASNETE_THREAD_PASS);
}

#ifndef gasnete_coll_broadcastM_nb
    extern gasnet_coll_handle_t
    gasnete_coll_broadcastM_nb(gasnet_team_handle_t team,
			       void * const dstlist[],
			       gasnet_node_t srcnode, void *src,
			       size_t nbytes, int flags GASNETE_THREAD_FARG)
    {
      gasnete_coll_poll_fn poll_fn;
      int in_sync  = GASNETE_COLL_IN_MODE(flags);
      int out_sync = GASNETE_COLL_OUT_MODE(flags);
      int options;

      /* Choose algorithm based on arguments */
      if ((in_sync == GASNET_COLL_IN_MYSYNC) || (flags & GASNET_COLL_LOCAL)) {
	if (nbytes <= GASNETE_COLL_P2P_EAGER_LIMIT) {
	  options = GASNETE_COLL_GENERIC_OPT_OUTSYNC_IF(out_sync == GASNET_COLL_OUT_ALLSYNC) |
		    GASNETE_COLL_GENERIC_OPT_P2P_IF(gasnete_mynode != srcnode);
	  poll_fn = &gasnete_coll_pf_bcastM_Eager;
	} else {
	  options = GASNETE_COLL_GENERIC_OPT_OUTSYNC_IF(out_sync != GASNET_COLL_OUT_NOSYNC) |
		    GASNETE_COLL_GENERIC_OPT_P2P_IF(gasnete_mynode != srcnode);
	  poll_fn = &gasnete_coll_pf_bcastM_RVGet;
	}
      } else if ((out_sync == GASNET_COLL_OUT_MYSYNC) && (nbytes <= GASNETE_COLL_P2P_EAGER_LIMIT)) {
	options = GASNETE_COLL_GENERIC_OPT_P2P_IF(gasnete_mynode != srcnode);
	poll_fn = &gasnete_coll_pf_bcastM_Eager;
      } else {
	options = GASNETE_COLL_GENERIC_OPT_OUTSYNC_IF(out_sync != GASNET_COLL_OUT_NOSYNC);
	poll_fn = &gasnete_coll_pf_bcastM_Get;
      }
      options |= GASNETE_COLL_GENERIC_OPT_INSYNC_IF(in_sync == GASNET_COLL_IN_ALLSYNC);

      return gasnete_coll_generic_broadcastM_nb(team, dstlist, srcnode, src, nbytes, flags,
						poll_fn, options GASNETE_THREAD_PASS);
    }
#endif

/*---------------------------------------------------------------------------------*/
/* gasnete_coll_scatter_nb() */

extern gasnet_coll_handle_t
gasnete_coll_generic_scatter_nb(gasnet_team_handle_t team,
				void *dst,
				gasnet_node_t srcnode, void *src,
				size_t nbytes, int flags,
				gasnete_coll_poll_fn poll_fn, int options
				GASNETE_THREAD_FARG) {
    gasnete_coll_generic_data_t *data = gasnete_coll_generic_alloc(GASNETE_THREAD_PASS_ALONE);
    GASNETE_COLL_GENERIC_SET_TAG(data, scatter);
    data->args.scatter.dst     = dst;
    data->args.scatter.srcnode = srcnode;
    data->args.scatter.src     = src;
    data->args.scatter.nbytes  = nbytes;
    data->options = options;
    return GASNETE_COLL_UNIMPLEMENTED();
    /* return gasnete_coll_op_generic_init(team, flags, data, poll_fn GASNETE_THREAD_PASS); */
}

#ifndef gasnete_coll_scatter_nb
    extern gasnet_coll_handle_t
    gasnete_coll_scatter_nb(gasnet_team_handle_t team,
			    void *dst,
			    gasnet_node_t srcnode, void *src,
			    size_t nbytes, int flags GASNETE_THREAD_FARG) {
      gasnete_coll_poll_fn poll_fn;
      int in_sync  = GASNETE_COLL_IN_MODE(flags);
      int out_sync = GASNETE_COLL_OUT_MODE(flags);
      int options;

      /* XXX: temporary limitation: */
      gasneti_assert_always(flags & GASNET_COLL_SINGLE);

      /* We currently map MYSYNC->ALLSYNC unconditionally */
      options = GASNETE_COLL_GENERIC_OPT_INSYNC_IF (in_sync  != GASNET_COLL_IN_NOSYNC) |
		GASNETE_COLL_GENERIC_OPT_OUTSYNC_IF(out_sync != GASNET_COLL_OUT_NOSYNC);

      /* XXX: multiple choice here */
      poll_fn = NULL;

      return gasnete_coll_generic_scatter_nb(team, dst, srcnode, src, nbytes, flags,
					     poll_fn, options GASNETE_THREAD_PASS);
    }
#endif

/*---------------------------------------------------------------------------------*/
/* gasnete_coll_scatterM_nb() */

extern gasnet_coll_handle_t
gasnete_coll_generic_scatterM_nb(gasnet_team_handle_t team,
				 void * const dstlist[],
				 gasnet_node_t srcnode, void *src,
				 size_t nbytes, int flags,
				 gasnete_coll_poll_fn poll_fn, int options
				 GASNETE_THREAD_FARG) {
    gasnete_coll_generic_data_t *data = gasnete_coll_generic_alloc(GASNETE_THREAD_PASS_ALONE);
    GASNETE_COLL_GENERIC_SET_TAG(data, scatterM);
    data->args.scatterM.dstlist = dstlist;
    data->args.scatterM.srcnode = srcnode;
    data->args.scatterM.src     = src;
    data->args.scatterM.nbytes  = nbytes;
    data->options = options;
    return GASNETE_COLL_UNIMPLEMENTED();
    /* return gasnete_coll_op_generic_init(team, flags, data, poll_fn GASNETE_THREAD_PASS); */
}

#ifndef gasnete_coll_scatterM_nb
    extern gasnet_coll_handle_t
    gasnete_coll_scatterM_nb(gasnet_team_handle_t team,
			     void * const dstlist[],
			     gasnet_node_t srcnode, void *src,
			     size_t nbytes, int flags GASNETE_THREAD_FARG) {
      gasnete_coll_poll_fn poll_fn;
      int in_sync  = GASNETE_COLL_IN_MODE(flags);
      int out_sync = GASNETE_COLL_OUT_MODE(flags);
      int options;

      /* XXX: temporary limitation: */
      gasneti_assert_always(flags & GASNET_COLL_SINGLE);

      /* We currently map MYSYNC->ALLSYNC unconditionally */
      options = GASNETE_COLL_GENERIC_OPT_INSYNC_IF (in_sync  != GASNET_COLL_IN_NOSYNC) |
		GASNETE_COLL_GENERIC_OPT_OUTSYNC_IF(out_sync != GASNET_COLL_OUT_NOSYNC);

      /* XXX: multiple choice here */
      poll_fn = NULL;

      return gasnete_coll_generic_scatterM_nb(team, dstlist, srcnode, src, nbytes, flags,
					      poll_fn, options GASNETE_THREAD_PASS);
    }
#endif

/*---------------------------------------------------------------------------------*/
/* gasnete_coll_gather_nb() */

extern gasnet_coll_handle_t
gasnete_coll_generic_gather_nb(gasnet_team_handle_t team,
			       gasnet_node_t dstnode, void *dst,
			       void *src,
			       size_t nbytes, int flags,
			       gasnete_coll_poll_fn poll_fn, int options
			       GASNETE_THREAD_FARG) {
    gasnete_coll_generic_data_t *data = gasnete_coll_generic_alloc(GASNETE_THREAD_PASS_ALONE);
    GASNETE_COLL_GENERIC_SET_TAG(data, gather);
    data->args.gather.dstnode = dstnode;
    data->args.gather.dst     = dst;
    data->args.gather.src     = src;
    data->args.gather.nbytes  = nbytes;
    data->options = options;
    return GASNETE_COLL_UNIMPLEMENTED();
    /* return gasnete_coll_op_generic_init(team, flags, data, poll_fn GASNETE_THREAD_PASS); */
}

#ifndef gasnete_coll_gather_nb
    extern gasnet_coll_handle_t
    gasnete_coll_gather_nb(gasnet_team_handle_t team,
			   gasnet_node_t dstnode, void *dst,
			   void *src,
			   size_t nbytes, int flags GASNETE_THREAD_FARG) {
      gasnete_coll_poll_fn poll_fn;
      int in_sync  = GASNETE_COLL_IN_MODE(flags);
      int out_sync = GASNETE_COLL_OUT_MODE(flags);
      int options;

      /* XXX: temporary limitation: */
      gasneti_assert_always(flags & GASNET_COLL_SINGLE);

      /* We currently map MYSYNC->ALLSYNC unconditionally */
      options = GASNETE_COLL_GENERIC_OPT_INSYNC_IF (in_sync  != GASNET_COLL_IN_NOSYNC) |
		GASNETE_COLL_GENERIC_OPT_OUTSYNC_IF(out_sync != GASNET_COLL_OUT_NOSYNC);

      /* XXX: multiple choice here */
      poll_fn = NULL;

      return gasnete_coll_generic_gather_nb(team, dstnode, dst, src, nbytes, flags,
					    poll_fn, options GASNETE_THREAD_PASS);
    }
#endif

/*---------------------------------------------------------------------------------*/
/* gasnete_coll_gatherM_nb() */

extern gasnet_coll_handle_t
gasnete_coll_generic_gatherM_nb(gasnet_team_handle_t team,
				gasnet_node_t dstnode, void *dst,
				void * const srclist[],
				size_t nbytes, int flags,
				gasnete_coll_poll_fn poll_fn, int options
				GASNETE_THREAD_FARG) {
    gasnete_coll_generic_data_t *data = gasnete_coll_generic_alloc(GASNETE_THREAD_PASS_ALONE);
    GASNETE_COLL_GENERIC_SET_TAG(data, gatherM);
    data->args.gatherM.dstnode = dstnode;
    data->args.gatherM.dst     = dst;
    data->args.gatherM.srclist = srclist;
    data->args.gatherM.nbytes  = nbytes;
    data->options = options;
    return GASNETE_COLL_UNIMPLEMENTED();
    /* return gasnete_coll_op_generic_init(team, flags, data, poll_fn GASNETE_THREAD_PASS); */
}

#ifndef gasnete_coll_gatherM_nb
    extern gasnet_coll_handle_t
    gasnete_coll_gatherM_nb(gasnet_team_handle_t team,
			    gasnet_node_t dstnode, void *dst,
			    void * const srclist[],
			    size_t nbytes, int flags GASNETE_THREAD_FARG) {
      gasnete_coll_poll_fn poll_fn;
      int in_sync  = GASNETE_COLL_IN_MODE(flags);
      int out_sync = GASNETE_COLL_OUT_MODE(flags);
      int options;

      /* XXX: temporary limitation: */
      gasneti_assert_always(flags & GASNET_COLL_SINGLE);

      /* We currently map MYSYNC->ALLSYNC unconditionally */
      options = GASNETE_COLL_GENERIC_OPT_INSYNC_IF (in_sync  != GASNET_COLL_IN_NOSYNC) |
		GASNETE_COLL_GENERIC_OPT_OUTSYNC_IF(out_sync != GASNET_COLL_OUT_NOSYNC);

      /* XXX: multiple choice here */
      poll_fn = NULL;

      return gasnete_coll_generic_gatherM_nb(team, dstnode, dst, srclist, nbytes, flags,
					     poll_fn, options GASNETE_THREAD_PASS);
    }
#endif

/*---------------------------------------------------------------------------------*/
/* gasnete_coll_gather_all_nb() */

extern gasnet_coll_handle_t
gasnete_coll_generic_gather_all_nb(gasnet_team_handle_t team,
				   void *dst, void *src,
				   size_t nbytes, int flags,
				   gasnete_coll_poll_fn poll_fn, int options
				   GASNETE_THREAD_FARG) {
    gasnete_coll_generic_data_t *data = gasnete_coll_generic_alloc(GASNETE_THREAD_PASS_ALONE);
    GASNETE_COLL_GENERIC_SET_TAG(data, gather_all);
    data->args.gather_all.dst     = dst;
    data->args.gather_all.src     = src;
    data->args.gather_all.nbytes  = nbytes;
    data->options = options;
    return GASNETE_COLL_UNIMPLEMENTED();
    /* return gasnete_coll_op_generic_init(team, flags, data, poll_fn GASNETE_THREAD_PASS); */
}

#ifndef gasnete_coll_gather_all_nb
    extern gasnet_coll_handle_t
    gasnete_coll_gather_all_nb(gasnet_team_handle_t team,
			       void *dst, void *src,
			       size_t nbytes, int flags GASNETE_THREAD_FARG) {
      gasnete_coll_poll_fn poll_fn;
      int in_sync  = GASNETE_COLL_IN_MODE(flags);
      int out_sync = GASNETE_COLL_OUT_MODE(flags);
      int options;

      /* XXX: temporary limitation: */
      gasneti_assert_always(flags & GASNET_COLL_SINGLE);

      /* We currently map MYSYNC->ALLSYNC unconditionally */
      options = GASNETE_COLL_GENERIC_OPT_INSYNC_IF (in_sync  != GASNET_COLL_IN_NOSYNC) |
		GASNETE_COLL_GENERIC_OPT_OUTSYNC_IF(out_sync != GASNET_COLL_OUT_NOSYNC);

      /* XXX: multiple choice here */
      poll_fn = NULL;

      return gasnete_coll_generic_gather_all_nb(team, dst, src, nbytes, flags,
						poll_fn, options GASNETE_THREAD_PASS);
    }
#endif

/*---------------------------------------------------------------------------------*/
/* gasnete_coll_gather_allM_nb() */

extern gasnet_coll_handle_t
gasnete_coll_generic_gather_allM_nb(gasnet_team_handle_t team,
				    void * const dstlist[], void * const srclist[],
				    size_t nbytes, int flags,
				    gasnete_coll_poll_fn poll_fn, int options
				    GASNETE_THREAD_FARG) {
    gasnete_coll_generic_data_t *data = gasnete_coll_generic_alloc(GASNETE_THREAD_PASS_ALONE);
    GASNETE_COLL_GENERIC_SET_TAG(data, gather_allM);
    data->args.gather_allM.dstlist = dstlist;
    data->args.gather_allM.srclist = srclist;
    data->args.gather_allM.nbytes  = nbytes;
    data->options = options;
    return GASNETE_COLL_UNIMPLEMENTED();
    /* return gasnete_coll_op_generic_init(team, flags, data, poll_fn GASNETE_THREAD_PASS); */
}

#ifndef gasnete_coll_gather_allM_nb
    extern gasnet_coll_handle_t
    gasnete_coll_gather_allM_nb(gasnet_team_handle_t team,
				void * const dstlist[], void * const srclist[],
				size_t nbytes, int flags GASNETE_THREAD_FARG) {
      gasnete_coll_poll_fn poll_fn;
      int in_sync  = GASNETE_COLL_IN_MODE(flags);
      int out_sync = GASNETE_COLL_OUT_MODE(flags);
      int options;

      /* XXX: temporary limitation: */
      gasneti_assert_always(flags & GASNET_COLL_SINGLE);

      /* We currently map MYSYNC->ALLSYNC unconditionally */
      options = GASNETE_COLL_GENERIC_OPT_INSYNC_IF (in_sync  != GASNET_COLL_IN_NOSYNC) |
		GASNETE_COLL_GENERIC_OPT_OUTSYNC_IF(out_sync != GASNET_COLL_OUT_NOSYNC);

      /* XXX: multiple choice here */
      poll_fn = NULL;

      return gasnete_coll_generic_gather_allM_nb(team, dstlist, srclist, nbytes, flags,
						 poll_fn, options GASNETE_THREAD_PASS);
    }
#endif

/*---------------------------------------------------------------------------------*/
/* gasnete_coll_exchange_nb() */

extern gasnet_coll_handle_t
gasnete_coll_generic_exchange_nb(gasnet_team_handle_t team,
				 void *dst, void *src,
				 size_t nbytes, int flags,
				 gasnete_coll_poll_fn poll_fn, int options
				 GASNETE_THREAD_FARG) {
    gasnete_coll_generic_data_t *data = gasnete_coll_generic_alloc(GASNETE_THREAD_PASS_ALONE);
    GASNETE_COLL_GENERIC_SET_TAG(data, exchange);
    data->args.exchange.dst     = dst;
    data->args.exchange.src     = src;
    data->args.exchange.nbytes  = nbytes;
    data->options = options;
    return GASNETE_COLL_UNIMPLEMENTED();
    /* return gasnete_coll_op_generic_init(team, flags, data, poll_fn GASNETE_THREAD_PASS); */
}

#ifndef gasnete_coll_exchange_nb
    extern gasnet_coll_handle_t
    gasnete_coll_exchange_nb(gasnet_team_handle_t team,
			     void *dst, void *src,
			     size_t nbytes, int flags GASNETE_THREAD_FARG) {
      gasnete_coll_poll_fn poll_fn;
      int in_sync  = GASNETE_COLL_IN_MODE(flags);
      int out_sync = GASNETE_COLL_OUT_MODE(flags);
      int options;

      /* XXX: temporary limitation: */
      gasneti_assert_always(flags & GASNET_COLL_SINGLE);

      /* We currently map MYSYNC->ALLSYNC unconditionally */
      options = GASNETE_COLL_GENERIC_OPT_INSYNC_IF (in_sync  != GASNET_COLL_IN_NOSYNC) |
		GASNETE_COLL_GENERIC_OPT_OUTSYNC_IF(out_sync != GASNET_COLL_OUT_NOSYNC);

      /* XXX: multiple choice here */
      poll_fn = NULL;

      return gasnete_coll_generic_exchange_nb(team, dst, src, nbytes, flags,
					      poll_fn, options GASNETE_THREAD_PASS);
    }
#endif

/*---------------------------------------------------------------------------------*/
/* gasnete_coll_exchangeM_nb() */

extern gasnet_coll_handle_t
gasnete_coll_generic_exchangeM_nb(gasnet_team_handle_t team,
				  void * const dstlist[], void * const srclist[],
				  size_t nbytes, int flags,
				  gasnete_coll_poll_fn poll_fn, int options
				  GASNETE_THREAD_FARG) {
    gasnete_coll_generic_data_t *data = gasnete_coll_generic_alloc(GASNETE_THREAD_PASS_ALONE);
    GASNETE_COLL_GENERIC_SET_TAG(data, exchangeM);
    data->args.exchangeM.dstlist = dstlist;
    data->args.exchangeM.srclist = srclist;
    data->args.exchangeM.nbytes  = nbytes;
    data->options = options;
    return GASNETE_COLL_UNIMPLEMENTED();
    /* return gasnete_coll_op_generic_init(team, flags, data, poll_fn GASNETE_THREAD_PASS); */
}

#ifndef gasnete_coll_exchangeM_nb
    extern gasnet_coll_handle_t
    gasnete_coll_exchangeM_nb(gasnet_team_handle_t team,
			      void * const dstlist[], void * const srclist[],
			      size_t nbytes, int flags GASNETE_THREAD_FARG) {
      gasnete_coll_poll_fn poll_fn;
      int in_sync  = GASNETE_COLL_IN_MODE(flags);
      int out_sync = GASNETE_COLL_OUT_MODE(flags);
      int options;

      /* XXX: temporary limitation: */
      gasneti_assert_always(flags & GASNET_COLL_SINGLE);

      /* We currently map MYSYNC->ALLSYNC unconditionally */
      options = GASNETE_COLL_GENERIC_OPT_INSYNC_IF (in_sync  != GASNET_COLL_IN_NOSYNC) |
		GASNETE_COLL_GENERIC_OPT_OUTSYNC_IF(out_sync != GASNET_COLL_OUT_NOSYNC);

      /* XXX: multiple choice here */
      poll_fn = NULL;

      return gasnete_coll_generic_exchangeM_nb(team, dstlist, srclist, nbytes, flags,
					       poll_fn, options GASNETE_THREAD_PASS);
    }
#endif

/*---------------------------------------------------------------------------------*/

#ifndef GASNETE_COLL_P2P_HANDLERS
  #define GASNETE_COLL_P2P_HANDLERS
#endif
#define GASNETE_REFCOLL_HANDLERS()                                 \
  GASNETE_COLL_P2P_HANDLERS

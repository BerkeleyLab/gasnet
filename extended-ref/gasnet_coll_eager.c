/*   $Source: /Users/kamil/work/gasnet-cvs2/gasnet/extended-ref/gasnet_coll_eager.c,v $
 *     $Date: 2005/02/02 20:20:52 $
 * $Revision: 1.20 $
 * Description: Reference implemetation of GASNet Collectives
 * Copyright 2004, Dan Bonachea <bonachea@cs.berkeley.edu>
 * Terms of use are as specified in license.txt
 */

#ifndef GASNETI_GASNET_EXTENDED_COLL_C
  #error This file not meant to be compiled directly - included by gasnet_extended.c
#endif

/*---------------------------------------------------------------------------------*/
/* Forward decls and macros */

#if GASNETI_USE_TRUE_MUTEXES || GASNET_DEBUG
  #define GASNETE_COLL_SET_OWNER(data)		(data)->owner = GASNETE_MYTHREAD
  #define GASNETE_COLL_CHECK_OWNER(data)	((data)->owner == GASNETE_MYTHREAD)
#else
  #define GASNETE_COLL_SET_OWNER(data)
  #define GASNETE_COLL_CHECK_OWNER(data)	1
#endif

/*---------------------------------------------------------------------------------*/
/* XXX: sequence and other stuff that will need to be per-team scoped: */

uint32_t gasnete_coll_sequence = 12345;	/* arbitrary non-zero starting value */
gasnet_image_t *gasnete_coll_all_images;
gasnet_image_t *gasnete_coll_all_offset;
gasnet_image_t gasnete_coll_total_images;
gasnet_image_t gasnete_coll_max_images;
gasnet_image_t gasnete_coll_my_images;	/* count of local images */
gasnet_image_t gasnete_coll_my_offset;	/* count of images before my first image */
#if !GASNET_SEQ
  gasnet_node_t *gasnete_coll_image_to_node = NULL;
#endif

#define GASNETE_COLL_1ST_IMAGE(LIST,NODE) \
	(((void * const *)(LIST))[gasnete_coll_all_offset[(NODE)]])
#define GASNETE_COLL_MY_1ST_IMAGE(LIST,FLAGS) \
	(((void * const *)(LIST))[((FLAGS) & GASNET_COLL_LOCAL) ? 0 : gasnete_coll_my_offset])

/*---------------------------------------------------------------------------------*/

int gasnete_coll_init_done = 0;

void gasnete_coll_validate(gasnet_team_handle_t team,
			   gasnet_image_t dstimage, const void *dst, size_t dstlen, int dstisv,
			   gasnet_image_t srcimage, const void *src, size_t srclen, int srcisv,
			   int flags) {
  gasnet_node_t dstnode = gasnete_coll_image_node(dstimage);
  gasnet_node_t srcnode = gasnete_coll_image_node(srcimage);
  int i;

  if_pf (!gasnete_coll_init_done) {
    gasneti_fatalerror("Illegal call to GASNet collectives before gasnet_coll_init()\n");
  }

  /* XXX: temporary limitation: */
  gasneti_assert(team == GASNET_TEAM_ALL);

  #if GASNET_PARSYNC
    gasneti_assert(!(flags & GASNET_COLL_ALL_THREADS));
  #else
    /* XXX: temporary limitation: */
    if (flags & GASNET_COLL_ALL_THREADS) {
      gasneti_fatalerror("GASNET_COLL_ALL_THREADS is unimplemented");
    }
  #endif

  #if GASNET_DEBUG
    /* Validate IN sync mode */
    switch (GASNETE_COLL_IN_MODE(flags)) {
      case 0:
	gasneti_fatalerror("No GASNET_COLL_IN_*SYNC flag given");
	break;
      case GASNET_COLL_IN_NOSYNC:
      case GASNET_COLL_IN_MYSYNC:
      case GASNET_COLL_IN_ALLSYNC:
        break; /* OK */
      default:
	gasneti_fatalerror("Multiple GASNET_COLL_IN_*SYNC flags given");
	break;
    }

    /* Validate OUT sync mode */
    switch (GASNETE_COLL_OUT_MODE(flags)) {
      case 0:
	gasneti_fatalerror("No GASNET_COLL_OUT_*SYNC flag given");
	break;
      case GASNET_COLL_OUT_NOSYNC:
      case GASNET_COLL_OUT_MYSYNC:
      case GASNET_COLL_OUT_ALLSYNC:
        break; /* OK */
      default:
	gasneti_fatalerror("Multiple GASNET_COLL_OUT_*SYNC flags given");
	break;
    }
  #endif
     
  gasneti_assert(((flags & GASNET_COLL_SINGLE)?1:0) ^ ((flags & GASNET_COLL_LOCAL)?1:0));

  /* Bounds check any local portion of dst/dstlist which user claims is in-segment */
  if ((dstnode == gasnete_mynode) && (flags & GASNET_COLL_DST_IN_SEGMENT)) {
    if (!dstisv) {
      gasnete_boundscheck(gasnete_mynode, dst, dstlen);
    } else {
      void * const *p = &GASNETE_COLL_MY_1ST_IMAGE(dst, flags);
      size_t limit = gasnete_coll_my_images;
      for (i = 0; i < limit; ++i, ++p) {
	gasnete_boundscheck(gasnete_mynode, *p, dstlen);
      }
    }
  }

  /* Bounds check any local portion of src/srclist which user claims is in-segment */
  if ((srcnode == gasnete_mynode) && (flags & GASNET_COLL_SRC_IN_SEGMENT)) {
    if (!srcisv) {
      gasnete_boundscheck(gasnete_mynode, src, srclen);
    } else {
      void * const *p = &GASNETE_COLL_MY_1ST_IMAGE(src, flags);
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

  /* NOTE: caller is responsible for a gasneti_flush_reads() on success */
  extern int gasnete_coll_handle_done(gasnet_coll_handle_t handle GASNETE_THREAD_FARG) {
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
#endif

#ifndef gasnete_coll_try_sync
  /* NOTE: caller is responsible for a gasneti_flush_reads() on success */
  extern int
  gasnete_coll_try_sync(gasnet_coll_handle_t handle GASNETE_THREAD_FARG) {
    gasneti_assert(handle != GASNET_COLL_INVALID_HANDLE); /* caller must check */

    gasneti_AMPoll();
    gasnete_coll_poll(GASNETE_THREAD_PASS_ALONE);

    return gasnete_coll_handle_done(handle GASNETE_THREAD_PASS) ? GASNET_OK : GASNET_ERR_NOT_READY;
  }
#endif

#ifndef gasnete_coll_try_sync_some
  /* Note caller is responsible for a gasneti_flush_reads() on success */
  extern int
  gasnete_coll_try_sync_some(gasnet_coll_handle_t *phandle, size_t numhandles GASNETE_THREAD_FARG) {
    int empty = 1;
    int result = GASNET_ERR_NOT_READY;
    int i;

    gasneti_assert(phandle != NULL);

    gasneti_AMPoll();
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
#endif

#ifndef gasnete_coll_try_sync_all
  /* NOTE: caller is responsible for a gasneti_flush_reads() on success */
  extern int
  gasnete_coll_try_sync_all(gasnet_coll_handle_t *phandle, size_t numhandles GASNETE_THREAD_FARG) {
    int result = GASNET_OK;
    int i;

    gasneti_assert(phandle != NULL);

    gasneti_AMPoll();
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
 *   void gasnete_coll_active_init()
 *   void gasnete_coll_active_fini()
 *   gasnete_coll_op_t *gasnete_coll_active_first()
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

gasneti_mutex_t gasnete_coll_active_lock = GASNETI_MUTEX_INITIALIZER;

#ifndef GASNETE_COLL_LIST_OVERRIDE
    /* Default implementation of coll_ops active list:
     *
     * Iteration over the active list is based on a linked list (queue).
     * Iteration starts from the head and new ops are added at the tail.
     *
     * XXX: use list macros?
     */
    static gasnete_coll_op_t	*gasnete_coll_active_head;
    static gasnete_coll_op_t	**gasnete_coll_active_tail_p;

    /* Caller must obtain lock */
    gasnete_coll_op_t *gasnete_coll_active_first(void) {
      return gasnete_coll_active_head;
    }

    /* Caller must obtain lock */
    gasnete_coll_op_t *gasnete_coll_active_next(gasnete_coll_op_t *op) {
      return op->active_next;
    }

    /* No lock needed */
    void gasnete_coll_active_new(gasnete_coll_op_t *op) {
      op->active_next = NULL;
      op->active_prev_p = &(op->active_next);
    }

    /* Caller must obtain lock */
    void gasnete_coll_active_ins(gasnete_coll_op_t *op) {
      *(gasnete_coll_active_tail_p) = op;
      op->active_prev_p = gasnete_coll_active_tail_p;
      gasnete_coll_active_tail_p = &(op->active_next);
    }

    /* Caller must obtain lock */
    void gasnete_coll_active_del(gasnete_coll_op_t *op) {
      gasnete_coll_op_t *next = op->active_next;
      *(op->active_prev_p) = next;
      if (next) {
	next->active_prev_p = op->active_prev_p;
      } else {
	gasnete_coll_active_tail_p = op->active_prev_p;
      }
    }

    void
    gasnete_coll_active_init(void) {
      gasnete_coll_active_head = NULL;
      gasnete_coll_active_tail_p = &(gasnete_coll_active_head);
    }

    void
    gasnete_coll_active_fini(void) {
      gasneti_assert(gasnete_coll_active_head == NULL);
    }
#endif

/*---------------------------------------------------------------------------------*/

extern gasnete_coll_threaddata_t *gasnete_coll_new_threaddata(void) {
    gasnete_coll_threaddata_t *result = gasneti_calloc(1,sizeof(*result));
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
      gasneti_mutex_lock(&gasnete_coll_active_lock);
      gasnete_coll_active_ins(op);
      gasneti_mutex_unlock(&gasnete_coll_active_lock);

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
	gasnete_coll_active_del(op);
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
  static gasneti_mutex_t poll_lock = GASNETI_MUTEX_INITIALIZER;

  /* XXX: We don't want an otherwise idle thread to contend for the lock
   * with a thread with useful work to do, especially since the only way
   * to make progress on RDMA is by letting the initiating thread poll.
   * What we want here is roughly
   * if ( have_useful_work(GASNETE_MYTHREAD) ? (gasneti_mutex_lock(&poll_lock), 1)
   * 					     : !gasneti_mutex_trylock(&poll_lock) )
   * Use of trylock alone is not sufficient, as starvation has been observed.
   */
  gasneti_mutex_lock(&poll_lock);
  {
    gasnete_coll_op_t *op;

    gasneti_mutex_lock(&gasnete_coll_active_lock);
    op = gasnete_coll_active_first();
    gasneti_mutex_unlock(&gasnete_coll_active_lock);

    while (op != NULL) {
      gasnete_coll_op_t *next;
      int poll_result = 0;

      /* Poll/kick the op */
      gasneti_assert(op->poll_fn != (gasnete_coll_poll_fn)NULL);
      poll_result = (*op->poll_fn)(op GASNETE_THREAD_PASS);

      /* Advance down the list, possibly deleting this current element */
      gasneti_mutex_lock(&gasnete_coll_active_lock);
      next = gasnete_coll_active_next(op);
      if (poll_result != 0) {
        gasnete_coll_op_complete(op, poll_result GASNETE_THREAD_PASS);
      }
      gasneti_mutex_unlock(&gasnete_coll_active_lock);

      /* Next... */
      op = next;
    }

  }
  gasneti_mutex_unlock(&poll_lock);
}

extern void gasnete_coll_init(const gasnet_image_t images[], gasnet_image_t my_image,
			      gasnet_coll_fn_entry_t fn_tbl[], size_t fn_count,
			      int init_flags GASNETE_THREAD_FARG) {
  gasnete_coll_threaddata_t *td = GASNETE_COLL_MYTHREAD;
  static gasneti_cond_t init_cond = GASNETI_COND_INITIALIZER;
  static gasneti_mutex_t init_lock = GASNETI_MUTEX_INITIALIZER;
  static gasnet_image_t remain = 0;
  size_t image_size = gasnete_nodes * sizeof(gasnet_image_t);
  int first;
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
    #if GASNET_SEQ
      gasneti_assert(images == NULL);
    #endif
  #endif

  if (images) {
    td->my_image = my_image;
    gasneti_mutex_lock(&init_lock);
    if (!remain) {
      /* First thread to arrive */
      remain = images[gasnete_mynode];
      first = 1;
    } else {
      first = 0;
    }
    gasneti_mutex_unlock(&init_lock);
  } else {
    td->my_image = gasnete_mynode;
    first = 1; /* only thread, so always first */
  }

  if (first) {
    gasnete_coll_active_init();
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
    gasnete_coll_max_images = 0;
    for (i = 0; i < gasnete_nodes; ++i) {
      gasnete_coll_all_offset[i] = gasnete_coll_total_images;
      gasnete_coll_total_images += gasnete_coll_all_images[i];
      gasnete_coll_max_images = MAX(gasnete_coll_max_images,gasnete_coll_all_images[i]);
    }
    gasnete_coll_my_images = gasnete_coll_all_images[gasnete_mynode];
    gasnete_coll_my_offset = gasnete_coll_all_offset[gasnete_mynode];

    #if !GASNET_SEQ
    {
      gasnet_image_t j;
      gasnete_coll_image_to_node = gasneti_malloc(gasnete_coll_total_images * sizeof(gasnet_node_t));
      for (j = 0, i = 0; j < gasnete_coll_total_images; ++j) {
	if (j >= (gasnete_coll_all_offset[i] + gasnete_coll_all_images[i])) {
	  i += 1;
	}
        gasnete_coll_image_to_node[j] = i;
      }
    }
    #endif

    if (fn_count != 0) {
      /* XXX: */
      gasneti_fatalerror("gasnet_coll_init: function registration is not yet supported");
    }

    gasnet_barrier_notify((int)gasnete_coll_sequence,0);
    gasnet_barrier_wait((int)gasnete_coll_sequence,0);
  }
  if (images) {
    /* Simple barrier */
    gasneti_mutex_lock(&init_lock);
    remain -= 1;
    if (remain == 0) {
      gasneti_cond_broadcast(&init_cond);
    } else {
      do {
        gasneti_cond_wait(&init_cond, &init_lock);
      } while (remain);
    }
    gasneti_mutex_unlock(&init_lock);
  }
  gasnete_coll_init_done = 1;
}

/*---------------------------------------------------------------------------------*/
/* Synchronization primitives */

#ifndef GASNETE_COLL_CONSENSUS_OVERRIDE
    static uint32_t gasnete_coll_issued_id = 0;
    static uint32_t gasnete_coll_consensus_id = 0;

    extern gasnete_coll_consensus_t gasnete_coll_consensus_create(void) {
      return gasnete_coll_issued_id++;
    }

    extern int gasnete_coll_consensus_try(gasnete_coll_consensus_t id) {
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
	size_t buffersz = MAX(GASNETE_COLL_P2P_EAGER_MIN,
			      gasnete_coll_total_images * GASNETE_COLL_P2P_EAGER_SCALE);
	size_t statesz = GASNETI_ALIGNUP(gasnete_coll_total_images * sizeof(uint32_t), 8);

	p2p = gasnete_coll_p2p_freelist;	/* XXX: per-team */

	if_pf (p2p == NULL) {
	  /* Round to 8-byte alignment of entry array */
	  size_t alloc_size = GASNETI_ALIGNUP(sizeof(gasnete_coll_p2p_t) + statesz,8) + buffersz;
	  uintptr_t p = (uintptr_t)gasneti_malloc(alloc_size);

	  p2p = (gasnete_coll_p2p_t *)p;
	  p += sizeof(gasnete_coll_p2p_t);

	  p2p->state = (uint32_t *)p;
	  p += statesz;

	  p = GASNETI_ALIGNUP(p,8);
	  p2p->data = (uint8_t *)p;

	  p2p->p2p_next = NULL;
	}

	memset((void *)p2p->state, 0, statesz);
	memset(p2p->data, 0, buffersz);

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
      gasneti_assert(p2p->state != NULL);
      gasneti_assert(p2p->data != NULL);

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
					  gasnet_handlerarg_t offset,
					  gasnet_handlerarg_t state) {
      gasnete_coll_p2p_t *p2p = gasnete_coll_p2p_get(team_id, sequence);

      if (nbytes) {
	gasneti_sync_writes();
      }

      p2p->state[offset] = state;
    }

    static void gasnete_coll_p2p_eager_reqh(gasnet_token_t token, void *buf, size_t nbytes,
					    gasnet_handlerarg_t team_id,
					    gasnet_handlerarg_t sequence,
					    gasnet_handlerarg_t count,
					    gasnet_handlerarg_t size,
					    gasnet_handlerarg_t offset,
					    gasnet_handlerarg_t state) {
      gasnete_coll_p2p_t *p2p = gasnete_coll_p2p_get(team_id, sequence);
      int i;

      if (size) {
	GASNETE_FAST_UNALIGNED_MEMCPY(p2p->data + offset*size, buf, nbytes);
	gasneti_sync_writes();
      }

      for (i = 0; i < count; ++i, ++offset) {
        p2p->state[offset] = state;
      }
    }

    #define _hidx_gasnete_coll_p2p_put_reqh	126	/* XXX: kludge!!! */
    #define _hidx_gasnete_coll_p2p_eager_reqh	127	/* XXX: kludge!!! */
    #define GASNETE_COLL_P2P_HANDLERS              \
	gasneti_handler_tableentry_no_bits(gasnete_coll_p2p_put_reqh),   \
	gasneti_handler_tableentry_no_bits(gasnete_coll_p2p_eager_reqh)

    /* Put up to gasnet_AMMaxLongRequest() bytes, signalling the recipient */
    /* Returns as soon as local buffer is reusable */
    void gasnete_coll_p2p_signalling_put(gasnete_coll_op_t *op, gasnet_node_t dstnode, void *dst,
					 void *src, size_t nbytes, uint32_t offset, uint32_t state) {
      uint32_t team_id = gasnete_coll_team_id(op->team);

      gasneti_assert(nbytes <= gasnet_AMMaxLongRequest());

      GASNETE_SAFE(
	LONG_REQ(4,4,(dstnode, gasneti_handleridx(gasnete_coll_p2p_put_reqh),
		      src, nbytes, dst, team_id, op->sequence, offset, state)));
    }

    /* Put up to gasnet_AMMaxLongRequest() bytes, signalling the recipient */
    /* Returns immediately even if the local buffer is not yet reusable */
    void gasnete_coll_p2p_signalling_putAsync(gasnete_coll_op_t *op, gasnet_node_t dstnode, void *dst,
					      void *src, size_t nbytes, uint32_t offset, uint32_t state) {
      uint32_t team_id = gasnete_coll_team_id(op->team);

      gasneti_assert(nbytes <= gasnet_AMMaxLongRequest());

      GASNETE_SAFE(
	LONGASYNC_REQ(4,4,(dstnode, gasneti_handleridx(gasnete_coll_p2p_put_reqh),
			   src, nbytes, dst, team_id, op->sequence, offset, state)));
    }

    /* Send data to be buffered by the recipient */
    void gasnete_coll_p2p_eager_putM(gasnete_coll_op_t *op, gasnet_node_t dstnode,
				     void *src, uint32_t count, size_t size,
				     uint32_t offset, uint32_t state) {
      uint32_t team_id = gasnete_coll_team_id(op->team);
      size_t limit;

      limit = gasnet_AMMaxMedium() / size;
      if_pf (count > limit) {
	size_t nbytes = limit * size;

	do {
          GASNETE_SAFE(
	    MEDIUM_REQ(6,6,(dstnode, gasneti_handleridx(gasnete_coll_p2p_eager_reqh),
			    src, nbytes, team_id, op->sequence, limit, size, offset, state)));
	  offset += limit;
	  src = (void *)((uintptr_t)src + nbytes);
	  count -= limit;
	} while (count > limit);
      }

      GASNETE_SAFE(
	MEDIUM_REQ(6,6,(dstnode, gasneti_handleridx(gasnete_coll_p2p_eager_reqh),
			src, count * size, team_id, op->sequence, count, size, offset, state)));
    }
#endif

/*---------------------------------------------------------------------------------*/
/* functions for generic ops */

extern gasnete_coll_generic_data_t *gasnete_coll_generic_alloc(GASNETE_THREAD_FARG_ALONE) {
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

    return result;
}

extern void gasnete_coll_generic_free(gasnete_coll_generic_data_t *data GASNETE_THREAD_FARG) {
    gasnete_coll_threaddata_t *td = GASNETE_COLL_MYTHREAD_NOALLOC;

    gasneti_assert(data != NULL);

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
extern gasnet_coll_handle_t
gasnete_coll_op_generic_init(gasnete_coll_team_t team, int flags,
			     gasnete_coll_generic_data_t *data, gasnete_coll_poll_fn poll_fn
			     GASNETE_THREAD_FARG) {
      gasnet_coll_handle_t handle = GASNET_COLL_INVALID_HANDLE;
      gasnete_coll_op_t *op;
      uint32_t sequence;

      gasneti_assert(team == GASNET_TEAM_ALL);
      gasneti_assert(data != NULL);

      /* Set owner */
      GASNETE_COLL_SET_OWNER(data);

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

extern int gasnete_coll_generic_syncnb(gasnete_coll_generic_data_t *data GASNETE_THREAD_FARG) {
  gasnet_handle_t handle = data->handle;
  int result = 1;

  /* Note the order of the checks is such that GASNET_INVALID_HANDLE
   * will produce a SUCCESS result regardless of the calling thread.
   */
  if_pt (handle != GASNET_INVALID_HANDLE)
    result = GASNETE_COLL_CHECK_OWNER(data) && (gasnete_try_syncnb(handle) == GASNET_OK);

  return result;
}

/* NOTE: caller is responsible for a gasneti_sync_reads() if they read any transferred data.  */
extern int gasnete_coll_generic_coll_sync(gasnet_coll_handle_t *p, size_t count GASNETE_THREAD_FARG) {
  int result = 1;
  int i;

  for (i = 0; i < count; ++i, ++p) {
    if (*p != GASNET_COLL_INVALID_HANDLE) {
      if (gasnete_coll_handle_done(*p GASNETE_THREAD_PASS)) {
        *p = GASNET_COLL_INVALID_HANDLE;
      } else {
        result = 0;
      }
    }
  }

  return result;
}

/*---------------------------------------------------------------------------------*/
/* gasnete_coll_broadcast_nb() */

/* bcast Get: all nodes perform uncoordinated gets */
static int gasnete_coll_pf_bcast_Get(gasnete_coll_op_t *op GASNETE_THREAD_FARG) {
  gasnete_coll_generic_data_t *data = op->data;
  const gasnete_coll_broadcast_args_t *args = GASNETE_COLL_GENERIC_ARGS(data, broadcast);
  int result = 0;

  switch (data->state) {
    case 0:	/* Optional IN barrier */
      if (!gasnete_coll_generic_insync(data)) {
	break;
      }
      data->state = 1;

    case 1:	/* Initiate data movement */
      if (gasnete_mynode == args->srcnode) {
	GASNETE_FAST_UNALIGNED_MEMCPY(args->dst, args->src, args->nbytes);
      } else if (GASNETE_COLL_CHECK_OWNER(data)) {
	data->handle = gasnete_get_nb_bulk(args->dst, args->srcnode, args->src,
					   args->nbytes GASNETE_THREAD_PASS);
      } else {
	break;	/* Stalled until owner thread initiates RDMA */
      }
      data->state = 2;

    case 2:	/* Sync data movement */
      if (!gasnete_coll_generic_syncnb(data GASNETE_THREAD_PASS)) {
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
		       size_t nbytes, int flags GASNETE_THREAD_FARG)
{
  int options = GASNETE_COLL_GENERIC_OPT_INSYNC_IF (!(flags & GASNET_COLL_IN_NOSYNC)) |
		GASNETE_COLL_GENERIC_OPT_OUTSYNC_IF(!(flags & GASNET_COLL_OUT_NOSYNC));

  gasneti_assert(flags & GASNET_COLL_SINGLE);
  gasneti_assert(flags & GASNET_COLL_SRC_IN_SEGMENT);

  return gasnete_coll_generic_broadcast_nb(team, dst, srcimage, src, nbytes, flags,
					   &gasnete_coll_pf_bcast_Get, options GASNETE_THREAD_PASS);
}

/* bcast Put: root node performs carefully ordered puts */
static int gasnete_coll_pf_bcast_Put(gasnete_coll_op_t *op GASNETE_THREAD_FARG) {
  gasnete_coll_generic_data_t *data = op->data;
  const gasnete_coll_broadcast_args_t *args = GASNETE_COLL_GENERIC_ARGS(data, broadcast);
  int result = 0;

  switch (data->state) {
    case 0:	/* Optional IN barrier */
      if (!gasnete_coll_generic_insync(data)) {
	break;
      }
      data->state = 1;

    case 1:	/* Initiate data movement */
      if (gasnete_mynode != args->srcnode) {
	/* Nothing to do */
      } else if (GASNETE_COLL_CHECK_OWNER(data)) {
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
      } else {
	break;	/* Stalled until owner thread initiates RDMA */
      }
      data->state = 2;

    case 2:	/* Sync data movement */
      if (!gasnete_coll_generic_syncnb(data GASNETE_THREAD_PASS)) {
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
		       size_t nbytes, int flags GASNETE_THREAD_FARG)
{
  int options = GASNETE_COLL_GENERIC_OPT_INSYNC_IF (!(flags & GASNET_COLL_IN_NOSYNC)) |
		GASNETE_COLL_GENERIC_OPT_OUTSYNC_IF(!(flags & GASNET_COLL_OUT_NOSYNC));

  gasneti_assert(flags & GASNET_COLL_SINGLE);
  gasneti_assert(flags & GASNET_COLL_DST_IN_SEGMENT);

  return gasnete_coll_generic_broadcast_nb(team, dst, srcimage, src, nbytes, flags,
					   &gasnete_coll_pf_bcast_Put, options GASNETE_THREAD_PASS);
}

/* bcast Eager: root node performs carefully ordered eager puts */
/* Requires GASNETE_COLL_GENERIC_OPT_P2P on non-root nodes */
static int gasnete_coll_pf_bcast_Eager(gasnete_coll_op_t *op GASNETE_THREAD_FARG) {
  gasnete_coll_generic_data_t *data = op->data;
  const gasnete_coll_broadcast_args_t *args = GASNETE_COLL_GENERIC_ARGS(data, broadcast);
  int result = 0;

  switch (data->state) {
    case 0:	/* Optional IN barrier */
      if (!gasnete_coll_generic_insync(data)) {
	break;
      }
      data->state = 1;

    case 1:	/* Data movement */
      if (gasnete_mynode == args->srcnode) {
	gasnete_coll_p2p_eager_put_all(op, args->src, args->nbytes, 0, 0, 1);	/* broadcast data */
	GASNETE_FAST_UNALIGNED_MEMCPY(args->dst, args->src, args->nbytes);
      } else if (data->p2p->state[0]) {
	gasneti_sync_reads();
	GASNETE_FAST_UNALIGNED_MEMCPY(args->dst, data->p2p->data, args->nbytes);
      } else {
	break;	/* Stalled until data arrives */
      }
      data->state = 2;

    case 2:	/* Optional OUT barrier */
      if (!gasnete_coll_generic_outsync(data)) {
	break;
      }

      gasnete_coll_generic_free(data GASNETE_THREAD_PASS);
      result = (GASNETE_COLL_OP_COMPLETE | GASNETE_COLL_OP_INACTIVE);
  }

  return result;
}
extern gasnet_coll_handle_t
gasnete_coll_bcast_Eager(gasnet_team_handle_t team,
			 void *dst,
			 gasnet_image_t srcimage, void *src,
			 size_t nbytes, int flags GASNETE_THREAD_FARG)
{
  int options = GASNETE_COLL_GENERIC_OPT_INSYNC_IF (flags & GASNET_COLL_IN_ALLSYNC)  |
		GASNETE_COLL_GENERIC_OPT_OUTSYNC_IF(flags & GASNET_COLL_OUT_ALLSYNC) |
		GASNETE_COLL_GENERIC_OPT_P2P_IF(!gasnete_coll_image_is_local(srcimage));

  gasneti_assert(nbytes <= GASNETE_COLL_P2P_EAGER_MIN);

  return gasnete_coll_generic_broadcast_nb(team, dst, srcimage, src, nbytes, flags,
					   &gasnete_coll_pf_bcast_Eager, options GASNETE_THREAD_PASS);
}

/* bcast RVGet: root node broadcasts address, others get from that address */
/* Requires GASNETE_COLL_GENERIC_OPT_P2P on non-root nodes */
static int gasnete_coll_pf_bcast_RVGet(gasnete_coll_op_t *op GASNETE_THREAD_FARG) {
  gasnete_coll_generic_data_t *data = op->data;
  const gasnete_coll_broadcast_args_t *args = GASNETE_COLL_GENERIC_ARGS(data, broadcast);
  int result = 0;

  switch (data->state) {
    case 0:	/* Optional IN barrier and rendezvous */
      if (!gasnete_coll_generic_insync(data)) {
	break;
      }
      data->state = 1;

    case 1:	/* Data movement */
      if (gasnete_mynode == args->srcnode) {
	gasnete_coll_p2p_eager_addr_all(op, args->src, 0, 1);	/* broadcast src address */
	GASNETE_FAST_UNALIGNED_MEMCPY(args->dst, args->src, args->nbytes);
      } else if (GASNETE_COLL_CHECK_OWNER(data) && data->p2p->state[0]) {
	gasneti_sync_reads();
	data->handle = gasnete_get_nb_bulk(args->dst, args->srcnode, 
					   *(void **)data->p2p->data,
					   args->nbytes GASNETE_THREAD_PASS);
      } else {
	break;	/* Stalled until owner thread receives the address */
      }
      data->state = 2;

    case 2:	/* Sync data movement */
      if (!gasnete_coll_generic_syncnb(data GASNETE_THREAD_PASS)) {
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
gasnete_coll_bcast_RVGet(gasnet_team_handle_t team,
			 void *dst,
			 gasnet_image_t srcimage, void *src,
			 size_t nbytes, int flags GASNETE_THREAD_FARG)
{
  int options = GASNETE_COLL_GENERIC_OPT_INSYNC_IF (flags & GASNET_COLL_IN_ALLSYNC) |
		GASNETE_COLL_GENERIC_OPT_OUTSYNC_IF(!(flags & GASNET_COLL_OUT_NOSYNC)) |
		GASNETE_COLL_GENERIC_OPT_P2P_IF(!gasnete_coll_image_is_local(srcimage));

  gasneti_assert(flags & GASNET_COLL_SRC_IN_SEGMENT);

  return gasnete_coll_generic_broadcast_nb(team, dst, srcimage, src, nbytes, flags,
					   &gasnete_coll_pf_bcast_RVGet, options GASNETE_THREAD_PASS);
}

/* XXX: IMPLEMENT or not? */
static gasnet_coll_handle_t
gasnete_coll_bcast_RVPut(gasnet_team_handle_t team,
			 void *dst,
			 gasnet_image_t srcimage, void *src,
			 size_t nbytes, int flags GASNETE_THREAD_FARG)
{
  if (!(flags & GASNET_COLL_LOCAL)) {
    return gasnete_coll_bcast_Put(team, dst, srcimage, src, nbytes, flags GASNETE_THREAD_PASS);
  } else {
    gasneti_fatalerror("gasnete_coll_bcast_RVPut is unimplemented");
    return GASNET_COLL_INVALID_HANDLE;
  }
}

/* XXX: IMPLEMENT */
static gasnet_coll_handle_t
gasnete_coll_bcast_AM(gasnet_team_handle_t team,
		      void *dst,
		      gasnet_image_t srcimage, void *src,
		      size_t nbytes, int flags GASNETE_THREAD_FARG)
{
  gasneti_fatalerror("gasnete_coll_bcast_AM is unimplemented");
  return GASNET_COLL_INVALID_HANDLE;
}

extern gasnet_coll_handle_t
gasnete_coll_generic_broadcast_nb(gasnet_team_handle_t team,
				  void *dst,
				  gasnet_image_t srcimage, void *src,
				  size_t nbytes, int flags,
				  gasnete_coll_poll_fn poll_fn, int options
				  GASNETE_THREAD_FARG) {
    gasnete_coll_generic_data_t *data = gasnete_coll_generic_alloc(GASNETE_THREAD_PASS_ALONE);
    GASNETE_COLL_GENERIC_SET_TAG(data, broadcast);
    data->args.broadcast.dst        = dst;
    #if !GASNET_SEQ
      data->args.broadcast.srcimage = srcimage;
    #endif
    data->args.broadcast.srcnode    = gasnete_coll_image_node(srcimage);
    data->args.broadcast.src        = src;
    data->args.broadcast.nbytes     = nbytes;
    data->options = options;
    return gasnete_coll_op_generic_init(team, flags, data, poll_fn GASNETE_THREAD_PASS);
}

#ifndef gasnete_coll_broadcast_nb
    extern gasnet_coll_handle_t
    gasnete_coll_broadcast_nb(gasnet_team_handle_t team,
			      void *dst,
			      gasnet_image_t srcimage, void *src,
			      size_t nbytes, int flags GASNETE_THREAD_FARG)
    {
      const size_t eager_limit = GASNETE_COLL_P2P_EAGER_MIN;

      /* "Discover" in-segment flags if needed/possible */
      flags = gasnete_coll_segment_check(flags, 0, 0, dst, nbytes, 1, srcimage, src, nbytes);

      /* Choose algorithm based on arguments */
      if ((nbytes <= eager_limit) &&
	  (flags & (GASNET_COLL_IN_MYSYNC | GASNET_COLL_OUT_MYSYNC | GASNET_COLL_LOCAL))) {
	/* Small enough for Eager, which will eliminate any barriers for *_MYSYNC and
	 * the need for passing addresses for _LOCAL
	 * Eager is totally AM-based and thus safe regardless if *_IN_SEGMENT
	 */
	return gasnete_coll_bcast_Eager(team, dst, srcimage, src, nbytes, flags GASNETE_THREAD_PASS);
      } else if (flags & GASNET_COLL_DST_IN_SEGMENT) {
	if (flags & (GASNET_COLL_IN_MYSYNC | GASNET_COLL_OUT_MYSYNC | GASNET_COLL_LOCAL)) {
	  /* We can use Rendezvous+Put to eliminate any barriers for *_MYSYNC.
	   * The Rendezvous is needed for _LOCAL.
	   */
	  return gasnete_coll_bcast_RVPut(team, dst, srcimage, src, nbytes, flags GASNETE_THREAD_PASS);
	} else {
	  /* We use a Put-based algorithm w/ full barriers for *_{MY,ALL}SYNC */
	  return gasnete_coll_bcast_Put(team, dst, srcimage, src, nbytes, flags GASNETE_THREAD_PASS);
	}
      } else if (flags & GASNET_COLL_SRC_IN_SEGMENT) {
	if (flags & (GASNET_COLL_IN_MYSYNC | GASNET_COLL_OUT_MYSYNC | GASNET_COLL_LOCAL)) {
	  /* We can use Rendezvous+Get to eliminate any barriers for *_MYSYNC.
	   * The Rendezvous is needed for _LOCAL.
	   */
	  return gasnete_coll_bcast_RVGet(team, dst, srcimage, src, nbytes, flags GASNETE_THREAD_PASS);
	} else {
	  return gasnete_coll_bcast_Get(team, dst, srcimage, src, nbytes, flags GASNETE_THREAD_PASS);
	}
      } else {
	/* If we reach here then neither src nor dst is in-segment */
	return gasnete_coll_bcast_AM(team, dst, srcimage, src, nbytes, flags GASNETE_THREAD_PASS);
      }
    }
#endif

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
      if (!gasnete_coll_generic_insync(data)) {
	break;
      }
      data->state = 1;

    case 1:	/* Initiate data movement */
      if (gasnete_mynode == args->srcnode) {
	gasnete_coll_local_broadcast(gasnete_coll_my_images,
				     &GASNETE_COLL_MY_1ST_IMAGE(args->dstlist, 0),
				     args->src, args->nbytes);
      } else if (GASNETE_COLL_CHECK_OWNER(data)) {
        /* Get only the 1st local image */
	data->handle = gasnete_get_nb_bulk(GASNETE_COLL_MY_1ST_IMAGE(args->dstlist, 0),
					   args->srcnode, args->src, args->nbytes GASNETE_THREAD_PASS);
      } else {
	break;	/* Stalled until owner thread initiates RDMA */
      }
      data->state = 2;

    case 2:	/* Sync data movement and perform local copies */
      if (!gasnete_coll_generic_syncnb(data GASNETE_THREAD_PASS)) {
	break;
      } else if (gasnete_mynode != args->srcnode) {
	void * const *p = &GASNETE_COLL_MY_1ST_IMAGE(args->dstlist, 0);
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
			size_t nbytes, int flags GASNETE_THREAD_FARG)
{
  int options = GASNETE_COLL_GENERIC_OPT_INSYNC_IF (!(flags & GASNET_COLL_IN_NOSYNC)) |
		GASNETE_COLL_GENERIC_OPT_OUTSYNC_IF(!(flags & GASNET_COLL_OUT_NOSYNC));

  return gasnete_coll_generic_broadcastM_nb(team, dstlist, srcimage, src, nbytes, flags,
					    &gasnete_coll_pf_bcastM_Get, options GASNETE_THREAD_PASS);
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
      if (!gasnete_coll_generic_insync(data)) {
	break;
      }
      data->state = 1;

    case 1: 	/* Initiate data movement */
      if (gasnete_mynode != args->srcnode) {
	/* Nothing to do */
      } else if (GASNETE_COLL_CHECK_OWNER(data)) {
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
	gasnete_coll_local_broadcast(gasnete_coll_my_images,
				     &GASNETE_COLL_MY_1ST_IMAGE(args->dstlist, 0),
				     src, nbytes);
      } else {
         break;	/* Stalled until owner thread initiates RDMA */
      }
      data->state = 2;

    case 2:	/* Sync data movement */
      if (!gasnete_coll_generic_syncnb(data GASNETE_THREAD_PASS)) {
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
			size_t nbytes, int flags GASNETE_THREAD_FARG)
{
  int options = GASNETE_COLL_GENERIC_OPT_INSYNC_IF (!(flags & GASNET_COLL_IN_NOSYNC)) |
		GASNETE_COLL_GENERIC_OPT_OUTSYNC_IF(!(flags & GASNET_COLL_OUT_NOSYNC));

  return gasnete_coll_generic_broadcastM_nb(team, dstlist, srcimage, src, nbytes, flags,
					    &gasnete_coll_pf_bcastM_Put, options GASNETE_THREAD_PASS);
}

/* bcastM Eager: root node performs carefully ordered eager puts */
/* Valid for SINGLE and LOCAL, size <= available eager buffer space */
/* Requires GASNETE_COLL_GENERIC_OPT_P2P on non-root nodes */
static int gasnete_coll_pf_bcastM_Eager(gasnete_coll_op_t *op GASNETE_THREAD_FARG) {
  gasnete_coll_generic_data_t *data = op->data;
  const gasnete_coll_broadcastM_args_t *args = GASNETE_COLL_GENERIC_ARGS(data, broadcastM);
  int result = 0;

  switch (data->state) {
    case 0:	/* Optional IN barrier */
      if (!gasnete_coll_generic_insync(data)) {
	break;
      }
      data->state = 1;

    case 1:	/* Data movement */
      if (gasnete_mynode == args->srcnode) {
	gasnete_coll_p2p_eager_put_all(op, args->src, args->nbytes, 0, 0, 1);	/* broadcast data */
	gasnete_coll_local_broadcast(gasnete_coll_my_images,
				     &GASNETE_COLL_MY_1ST_IMAGE(args->dstlist, op->flags),
				     args->src, args->nbytes);
      } else if (data->p2p->state[0]) {
	gasneti_sync_reads();
	gasnete_coll_local_broadcast(gasnete_coll_my_images,
				     &GASNETE_COLL_MY_1ST_IMAGE(args->dstlist, op->flags),
				     data->p2p->data, args->nbytes);
      } else {
        break;  /* Stalled until data arrives */
      }
      data->state = 2;

    case 2:	/* Optional OUT barrier */
      if (!gasnete_coll_generic_outsync(data)) {
	break;
      }

      gasnete_coll_generic_free(data GASNETE_THREAD_PASS);
      result = (GASNETE_COLL_OP_COMPLETE | GASNETE_COLL_OP_INACTIVE);
  }

  return result;
}
extern gasnet_coll_handle_t
gasnete_coll_bcastM_Eager(gasnet_team_handle_t team,
			  void * const dstlist[],
			  gasnet_image_t srcimage, void *src,
			  size_t nbytes, int flags GASNETE_THREAD_FARG)
{
  int options = GASNETE_COLL_GENERIC_OPT_INSYNC_IF (flags & GASNET_COLL_IN_ALLSYNC) |
		GASNETE_COLL_GENERIC_OPT_OUTSYNC_IF(flags & GASNET_COLL_OUT_ALLSYNC) |
		GASNETE_COLL_GENERIC_OPT_P2P_IF(!gasnete_coll_image_is_local(srcimage));

  return gasnete_coll_generic_broadcastM_nb(team, dstlist, srcimage, src, nbytes, flags,
					    &gasnete_coll_pf_bcastM_Eager, options GASNETE_THREAD_PASS);
}

/* bcastM RVGet: root node broadcasts address, others get from that address */
/* Valid for SINGLE and LOCAL, any size */
/* Requires GASNETE_COLL_GENERIC_OPT_P2P on non-root nodes */
static int gasnete_coll_pf_bcastM_RVGet(gasnete_coll_op_t *op GASNETE_THREAD_FARG) {
  gasnete_coll_generic_data_t *data = op->data;
  const gasnete_coll_broadcastM_args_t *args = GASNETE_COLL_GENERIC_ARGS(data, broadcastM);
  int result = 0;

  switch (data->state) {
    case 0:	/* Optional IN barrier */
      if (!gasnete_coll_generic_insync(data)) {
	break;
      }
      data->state = 1;

    case 1:	/* Initiate data movement */
      if (gasnete_mynode == args->srcnode) {
	gasnete_coll_p2p_eager_addr_all(op, args->src, 0, 1);	/* broadcast src address */
	/* Do local copy LAST, perhaps overlapping with communication */
	gasnete_coll_local_broadcast(gasnete_coll_my_images,
				     &GASNETE_COLL_MY_1ST_IMAGE(args->dstlist, op->flags),
				     args->src, args->nbytes);
      } else if (GASNETE_COLL_CHECK_OWNER(data) && data->p2p->state[0]) {
	/* Get 1st image only */
	gasneti_sync_reads();
	data->handle = gasnete_get_nb_bulk(GASNETE_COLL_MY_1ST_IMAGE(args->dstlist, op->flags),
					   args->srcnode, *(void **)data->p2p->data,
					   args->nbytes GASNETE_THREAD_PASS);
      } else {
        break;	/* Stalled until owner thread initiates RDMA */
      }
      data->state = 2;

    case 2:	/* Complete data movement */
      if (!gasnete_coll_generic_syncnb(data GASNETE_THREAD_PASS)) {
	  break;
      } else if (gasnete_mynode != args->srcnode) {
	void * const *p = &GASNETE_COLL_MY_1ST_IMAGE(args->dstlist, op->flags);
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
gasnete_coll_bcastM_RVGet(gasnet_team_handle_t team,
			  void * const dstlist[],
			  gasnet_image_t srcimage, void *src,
			  size_t nbytes, int flags GASNETE_THREAD_FARG)
{
  int options = GASNETE_COLL_GENERIC_OPT_INSYNC_IF (flags & GASNET_COLL_IN_ALLSYNC)   |
		GASNETE_COLL_GENERIC_OPT_OUTSYNC_IF(!(flags & GASNET_COLL_OUT_NOSYNC))|
		GASNETE_COLL_GENERIC_OPT_P2P_IF(!gasnete_coll_image_is_local(srcimage));

  return gasnete_coll_generic_broadcastM_nb(team, dstlist, srcimage, src, nbytes, flags,
					    &gasnete_coll_pf_bcastM_RVGet, options GASNETE_THREAD_PASS);
}

extern gasnet_coll_handle_t
gasnete_coll_generic_broadcastM_nb(gasnet_team_handle_t team,
				   void * const dstlist[],
				   gasnet_image_t srcimage, void *src,
				   size_t nbytes, int flags,
				   gasnete_coll_poll_fn poll_fn, int options
				   GASNETE_THREAD_FARG) {
    gasnete_coll_generic_data_t *data = gasnete_coll_generic_alloc(GASNETE_THREAD_PASS_ALONE);
    GASNETE_COLL_GENERIC_SET_TAG(data, broadcastM);
    data->args.broadcastM.dstlist    = dstlist;
    #if !GASNET_SEQ
      data->args.broadcastM.srcimage = srcimage;
    #endif
    data->args.broadcastM.srcnode    = gasnete_coll_image_node(srcimage);
    data->args.broadcastM.src        = src;
    data->args.broadcastM.nbytes     = nbytes;
    data->options = options;
    return gasnete_coll_op_generic_init(team, flags, data, poll_fn GASNETE_THREAD_PASS);
}

#ifndef gasnete_coll_broadcastM_nb
    extern gasnet_coll_handle_t
    gasnete_coll_broadcastM_nb(gasnet_team_handle_t team,
			       void * const dstlist[],
			       gasnet_image_t srcimage, void *src,
			       size_t nbytes, int flags GASNETE_THREAD_FARG)
    {
      const size_t eager_limit = GASNETE_COLL_P2P_EAGER_MIN;

      /* "Discover" in-segment flags if needed/possible */
      flags = gasnete_coll_segment_checkM(flags, 0, 0, dstlist, nbytes, 1, srcimage, src, nbytes);

      /* Choose algorithm based on arguments */
      if ((flags & GASNET_COLL_DST_IN_SEGMENT) && (flags & GASNET_COLL_SRC_IN_SEGMENT)) {
	/* Both ends are in-segment */
        if ((flags & GASNET_COLL_IN_MYSYNC) || (flags & GASNET_COLL_LOCAL)) {
	  if (nbytes <= eager_limit) {
	    return gasnete_coll_bcastM_Eager(team, dstlist, srcimage, src, nbytes, flags GASNETE_THREAD_PASS);
	  } else {
            return gasnete_coll_bcastM_RVGet(team, dstlist, srcimage, src, nbytes, flags GASNETE_THREAD_PASS);
	  }
        } else if ((flags & GASNET_COLL_OUT_MYSYNC) && (nbytes <= eager_limit)) {
	  return gasnete_coll_bcastM_Eager(team, dstlist, srcimage, src, nbytes, flags GASNETE_THREAD_PASS);
        } else {
	  return gasnete_coll_bcastM_Get(team, dstlist, srcimage, src, nbytes, flags GASNETE_THREAD_PASS);
        }
      } else if (flags & GASNET_COLL_DST_IN_SEGMENT) {
	/* Only the destination is in-segment */
	gasneti_fatalerror("Currently only in-segment data is supported for this operation");
	/* XXX: IMPLEMENT THIS */
	return GASNET_COLL_INVALID_HANDLE;
      } else if (flags & GASNET_COLL_SRC_IN_SEGMENT) {
	/* Only the source is in-segment */
	gasneti_fatalerror("Currently only in-segment data is supported for this operation");
	/* XXX: IMPLEMENT THIS */
	return GASNET_COLL_INVALID_HANDLE;
      } else {
	/* Nothing is in-segment */
	gasneti_fatalerror("Currently only in-segment data is supported for this operation");
	/* XXX: IMPLEMENT THIS */
	return GASNET_COLL_INVALID_HANDLE;
      }
    }
#endif

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
      if (!gasnete_coll_generic_insync(data)) {
	break;
      }
      data->state = 1;

    case 1:	/* Initiate data movement */
      if (gasnete_mynode == args->srcnode) {
	GASNETE_FAST_UNALIGNED_MEMCPY(args->dst,
				      gasnete_coll_scale_ptr(args->src, gasnete_mynode, args->nbytes),
				      args->nbytes);
      } else if (GASNETE_COLL_CHECK_OWNER(data)) {
	data->handle = gasnete_get_nb_bulk(args->dst, args->srcnode,
					   gasnete_coll_scale_ptr(args->src, gasnete_mynode, args->nbytes),
					   args->nbytes GASNETE_THREAD_PASS);
      } else {
	break;	/* Stalled until owner thread initiates RDMA */
      }
      data->state = 2;

    case 2:	/* Sync data movement */
      if (!gasnete_coll_generic_syncnb(data GASNETE_THREAD_PASS)) {
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
		      size_t nbytes, int flags GASNETE_THREAD_FARG)
{
  int options = GASNETE_COLL_GENERIC_OPT_INSYNC_IF (!(flags & GASNET_COLL_IN_NOSYNC)) |
		GASNETE_COLL_GENERIC_OPT_OUTSYNC_IF(!(flags & GASNET_COLL_OUT_NOSYNC));

  return gasnete_coll_generic_scatter_nb(team, dst, srcimage, src, nbytes, flags,
					 &gasnete_coll_pf_scat_Get, options GASNETE_THREAD_PASS);
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
      if (!gasnete_coll_generic_insync(data)) {
	break;
      }
      data->state = 1;

    case 1:
      if (gasnete_mynode != args->srcnode) {
	/* Nothing to do */
      } else if (GASNETE_COLL_CHECK_OWNER(data)) {
	void   *dst   = args->dst;
	size_t nbytes = args->nbytes;
	uintptr_t p;

	/* Queue PUTS in an NBI access region */
	gasnete_begin_nbi_accessregion(1 GASNETE_THREAD_PASS);
	{
	  int i;

	  /* Put to nodes to the "right" of ourself */
	  p = (uintptr_t)gasnete_coll_scale_ptr(args->src, (gasnete_mynode + 1), nbytes);
	  for (i = gasnete_mynode + 1; i < gasnete_nodes; ++i, p += nbytes) {
	    gasnete_put_nbi_bulk(i, dst, (void *)p, nbytes GASNETE_THREAD_PASS);
	  }
	  /* Put to nodes to the "left" of ourself */
	  p = (uintptr_t)gasnete_coll_scale_ptr(args->src, 0, nbytes);
	  for (i = 0; i < gasnete_mynode; ++i, p += nbytes) {
	    gasnete_put_nbi_bulk(i, dst, (void *)p, nbytes GASNETE_THREAD_PASS);
	  }
	}
	data->handle = gasnete_end_nbi_accessregion(GASNETE_THREAD_PASS_ALONE);

	/* Do local copy LAST, perhaps overlapping with communication */
	GASNETE_FAST_UNALIGNED_MEMCPY(dst,
				      gasnete_coll_scale_ptr(args->src, gasnete_mynode, nbytes),
				      nbytes);
      } else {
         break;	/* Stalled until owner thread initiates RDMA */
      }
      data->state = 2;

    case 2:	/* Sync data movement */
      if (!gasnete_coll_generic_syncnb(data GASNETE_THREAD_PASS)) {
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
		      size_t nbytes, int flags GASNETE_THREAD_FARG)
{
  int options = GASNETE_COLL_GENERIC_OPT_INSYNC_IF (!(flags & GASNET_COLL_IN_NOSYNC)) |
		GASNETE_COLL_GENERIC_OPT_OUTSYNC_IF(!(flags & GASNET_COLL_OUT_NOSYNC));

  return gasnete_coll_generic_scatter_nb(team, dst, srcimage, src, nbytes, flags,
					 &gasnete_coll_pf_scat_Put, options GASNETE_THREAD_PASS);
}

/* scat Eager: root node performs carefully ordered eager puts */
/* Valid for SINGLE and LOCAL, size <= available eager buffer space */
/* Requires GASNETE_COLL_GENERIC_OPT_P2P on non-root nodes */
static int gasnete_coll_pf_scat_Eager(gasnete_coll_op_t *op GASNETE_THREAD_FARG) {
  gasnete_coll_generic_data_t *data = op->data;
  const gasnete_coll_scatter_args_t *args = GASNETE_COLL_GENERIC_ARGS(data, scatter);
  int result = 0;

  switch (data->state) {
    case 0:	/* Optional IN barrier */
      if (!gasnete_coll_generic_insync(data)) {
	break;
      }
      data->state = 1;

    case 1:	/* Data movement */
      if (gasnete_mynode == args->srcnode) {
	gasnete_coll_p2p_eager_put_all(op, args->src, args->nbytes, 1, 0, 1);	/* scatter data */
	GASNETE_FAST_UNALIGNED_MEMCPY(args->dst,
				      gasnete_coll_scale_ptr(args->src, gasnete_mynode, args->nbytes),
				      args->nbytes);
      } else if (data->p2p->state[0]) {
	gasneti_sync_reads();
	GASNETE_FAST_UNALIGNED_MEMCPY(args->dst, data->p2p->data, args->nbytes);
      } else {
	break;	/* Stalled until data arrives */
      }
      data->state = 2;

    case 2:	/* Optional OUT barrier */
      if (!gasnete_coll_generic_outsync(data)) {
	break;
      }

      gasnete_coll_generic_free(data GASNETE_THREAD_PASS);
      result = (GASNETE_COLL_OP_COMPLETE | GASNETE_COLL_OP_INACTIVE);
  }

  return result;
}
extern gasnet_coll_handle_t
gasnete_coll_scat_Eager(gasnet_team_handle_t team,
			void *dst,
			gasnet_image_t srcimage, void *src,
			size_t nbytes, int flags GASNETE_THREAD_FARG)
{
  int options = GASNETE_COLL_GENERIC_OPT_INSYNC_IF (flags & GASNET_COLL_IN_ALLSYNC) |
		GASNETE_COLL_GENERIC_OPT_OUTSYNC_IF(flags & GASNET_COLL_OUT_ALLSYNC) |
		GASNETE_COLL_GENERIC_OPT_P2P_IF(!gasnete_coll_image_is_local(srcimage));

  return gasnete_coll_generic_scatter_nb(team, dst, srcimage, src, nbytes, flags,
					 &gasnete_coll_pf_scat_Eager, options GASNETE_THREAD_PASS);
}

/* scat RVGet: root node broadcasts address, others get from offsets from that address */
/* Valid for SINGLE and LOCAL, any size */
/* Requires GASNETE_COLL_GENERIC_OPT_P2P on non-root nodes */
static int gasnete_coll_pf_scat_RVGet(gasnete_coll_op_t *op GASNETE_THREAD_FARG) {
  gasnete_coll_generic_data_t *data = op->data;
  const gasnete_coll_scatter_args_t *args = GASNETE_COLL_GENERIC_ARGS(data, scatter);
  int result = 0;

  switch (data->state) {
    case 0:	/* Optional IN barrier */
      if (!gasnete_coll_generic_insync(data)) {
	break;
      }
      data->state = 1;

    case 1:	/* Initiate data movement */
      if (gasnete_mynode == args->srcnode) {
	gasnete_coll_p2p_eager_addr_all(op, args->src, 0, 1);	/* broadcast src address */
	GASNETE_FAST_UNALIGNED_MEMCPY(args->dst, 
				      gasnete_coll_scale_ptr(args->src, gasnete_mynode, args->nbytes),
				      args->nbytes);
      } else if (GASNETE_COLL_CHECK_OWNER(data) && data->p2p->state[0]) {
	gasneti_sync_reads();
	data->handle = gasnete_get_nb_bulk(args->dst, args->srcnode,
					   gasnete_coll_scale_ptr(*(void **)data->p2p->data,
								  gasnete_mynode, args->nbytes),
					   args->nbytes GASNETE_THREAD_PASS);
      } else {
	break;	/* Stalled until owner thread receives address */
      }
      data->state = 2;

    case 2:	/* Sync data movement */
      if (!gasnete_coll_generic_syncnb(data GASNETE_THREAD_PASS)) {
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
gasnete_coll_scat_RVGet(gasnet_team_handle_t team,
			void *dst,
			gasnet_image_t srcimage, void *src,
			size_t nbytes, int flags GASNETE_THREAD_FARG)
{
  int options = GASNETE_COLL_GENERIC_OPT_INSYNC_IF (flags & GASNET_COLL_IN_ALLSYNC) |
		GASNETE_COLL_GENERIC_OPT_OUTSYNC_IF(!(flags & GASNET_COLL_OUT_NOSYNC)) |
		GASNETE_COLL_GENERIC_OPT_P2P_IF(!gasnete_coll_image_is_local(srcimage));

  return gasnete_coll_generic_scatter_nb(team, dst, srcimage, src, nbytes, flags,
					 &gasnete_coll_pf_scat_RVGet, options GASNETE_THREAD_PASS);
}

extern gasnet_coll_handle_t
gasnete_coll_generic_scatter_nb(gasnet_team_handle_t team,
				void *dst,
				gasnet_image_t srcimage, void *src,
				size_t nbytes, int flags,
				gasnete_coll_poll_fn poll_fn, int options
				GASNETE_THREAD_FARG) {
    gasnete_coll_generic_data_t *data = gasnete_coll_generic_alloc(GASNETE_THREAD_PASS_ALONE);
    GASNETE_COLL_GENERIC_SET_TAG(data, scatter);
    data->args.scatter.dst        = dst;
    #if !GASNET_SEQ
      data->args.scatter.srcimage = srcimage;
    #endif
    data->args.scatter.srcnode    = gasnete_coll_image_node(srcimage);
    data->args.scatter.src        = src;
    data->args.scatter.nbytes     = nbytes;
    data->options = options;
    return gasnete_coll_op_generic_init(team, flags, data, poll_fn GASNETE_THREAD_PASS);
}

#ifndef gasnete_coll_scatter_nb
    extern gasnet_coll_handle_t
    gasnete_coll_scatter_nb(gasnet_team_handle_t team,
			    void *dst,
			    gasnet_image_t srcimage, void *src,
			    size_t nbytes, int flags GASNETE_THREAD_FARG) {
      const size_t eager_limit = GASNETE_COLL_P2P_EAGER_MIN;

      /* "Discover" in-segment flags if needed/possible */
      flags = gasnete_coll_segment_check(flags, 0, 0, dst, nbytes,
						1, srcimage, src, nbytes*gasnete_nodes);

      /* Choose algorithm based on arguments */
      if ((flags & GASNET_COLL_DST_IN_SEGMENT) && (flags & GASNET_COLL_SRC_IN_SEGMENT)) {
	/* Both ends are in-segment */
        if ((flags & GASNET_COLL_IN_MYSYNC) || (flags & GASNET_COLL_LOCAL)) {
	  if (nbytes <= eager_limit) {
	    return gasnete_coll_scat_Eager(team, dst, srcimage, src, nbytes, flags GASNETE_THREAD_PASS);
	  } else {
	    return gasnete_coll_scat_RVGet(team, dst, srcimage, src, nbytes, flags GASNETE_THREAD_PASS);
	  }
        } else if ((flags & GASNET_COLL_OUT_MYSYNC) && (nbytes <= eager_limit)) {
	  return gasnete_coll_scat_Eager(team, dst, srcimage, src, nbytes, flags GASNETE_THREAD_PASS);
        } else {
	  return gasnete_coll_scat_Put(team, dst, srcimage, src, nbytes, flags GASNETE_THREAD_PASS);
        }
      } else if (flags & GASNET_COLL_DST_IN_SEGMENT) {
	/* Only the destination is in-segment */
	gasneti_fatalerror("Currently only in-segment data is supported for this operation");
	/* XXX: IMPLEMENT THIS */
	return GASNET_COLL_INVALID_HANDLE;
      } else if (flags & GASNET_COLL_SRC_IN_SEGMENT) {
	/* Only the source is in-segment */
	gasneti_fatalerror("Currently only in-segment data is supported for this operation");
	/* XXX: IMPLEMENT THIS */
	return GASNET_COLL_INVALID_HANDLE;
      } else {
	/* Nothing is in-segment */
	gasneti_fatalerror("Currently only in-segment data is supported for this operation");
	/* XXX: IMPLEMENT THIS */
	return GASNET_COLL_INVALID_HANDLE;
      }
    }
#endif

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
      if (!gasnete_coll_generic_insync(data)) {
	break;
      }
      data->state = 1;

    case 1:	/* Initiate data movement */
      if (gasnete_mynode == args->srcnode) {
	gasnete_coll_local_scatter(gasnete_coll_my_images,
				   &GASNETE_COLL_MY_1ST_IMAGE(args->dstlist, 0),
				   gasnete_coll_scale_ptr(args->src, gasnete_coll_my_offset, args->nbytes),
				   args->nbytes);
      } else if (GASNETE_COLL_CHECK_OWNER(data)) {
	data->private_data = gasnete_coll_scale_ptr(args->src, gasnete_coll_my_offset, args->nbytes),
	data->handle = gasnete_geti(gasnete_synctype_nb, gasnete_coll_my_images,
				    &GASNETE_COLL_MY_1ST_IMAGE(args->dstlist, 0), args->nbytes,
			  	    args->srcnode, 1, &(data->private_data),
				    gasnete_coll_my_images * args->nbytes GASNETE_THREAD_PASS);
      } else {
	break;	/* Stalled until owner thread initiates RDMA */
      }
      data->state = 2;

    case 2:	/* Sync data movement */
      if (!gasnete_coll_generic_syncnb(data GASNETE_THREAD_PASS)) {
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
		       size_t nbytes, int flags GASNETE_THREAD_FARG)
{
  int options = GASNETE_COLL_GENERIC_OPT_INSYNC_IF (!(flags & GASNET_COLL_IN_NOSYNC)) |
		GASNETE_COLL_GENERIC_OPT_OUTSYNC_IF(!(flags & GASNET_COLL_OUT_NOSYNC));

  return gasnete_coll_generic_scatterM_nb(team, dstlist, srcimage, src, nbytes, flags,
					  &gasnete_coll_pf_scatM_Get, options GASNETE_THREAD_PASS);
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
      if (!gasnete_coll_generic_insync(data)) {
	break;
      }
      data->state = 1;

    case 1:
      if (gasnete_mynode != args->srcnode) {
	/* Nothing to do */
      } else if (GASNETE_COLL_CHECK_OWNER(data)) {
	size_t nbytes = args->nbytes;
	uintptr_t src_addr;
	int i;
	void ** srclist;
	void * const *p;

	/* Allocate a source vector for puti */
	/* XXX: Use freelist? */
	srclist = gasneti_malloc(gasnete_nodes * sizeof(void *));
	data->private_data = srclist;

	/* Queue PUTIs in an NBI access region */
	/* XXX: is gasnete_puti(gasnete_synctype_nbi,...) correct non-tracing variant of puti ? */
	gasnete_begin_nbi_accessregion(1 GASNETE_THREAD_PASS);
	{
	  void **q;

	  /* Put to nodes to the "right" of ourself */
	  src_addr = (uintptr_t)gasnete_coll_scale_ptr(args->src,
			  			       gasnete_coll_all_offset[gasnete_mynode + 1],
						       nbytes);
	  p = &GASNETE_COLL_1ST_IMAGE(args->dstlist, gasnete_mynode + 1);
	  q = &srclist[gasnete_mynode + 1];
	  for (i = gasnete_mynode + 1; i < gasnete_nodes; ++i) {
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
	  for (i = 0; i < gasnete_mynode; ++i) {
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

	/* Do local copy LAST, perhaps overlapping with communication */
	gasnete_coll_local_scatter(gasnete_coll_my_images,
				   &GASNETE_COLL_MY_1ST_IMAGE(args->dstlist, 0),
				   gasnete_coll_scale_ptr(args->src, gasnete_coll_my_offset, nbytes),
				   nbytes);
      } else {
         break;	/* Stalled until owner thread initiates RDMA */
      }
      data->state = 2;

    case 2:	/* Sync data movement */
      if (gasnete_mynode == args->srcnode) {
        if (!gasnete_coll_generic_syncnb(data GASNETE_THREAD_PASS)) {
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
		       size_t nbytes, int flags GASNETE_THREAD_FARG)
{
  int options = GASNETE_COLL_GENERIC_OPT_INSYNC_IF (!(flags & GASNET_COLL_IN_NOSYNC)) |
		GASNETE_COLL_GENERIC_OPT_OUTSYNC_IF(!(flags & GASNET_COLL_OUT_NOSYNC));

  return gasnete_coll_generic_scatterM_nb(team, dstlist, srcimage, src, nbytes, flags,
					  &gasnete_coll_pf_scatM_Put, options GASNETE_THREAD_PASS);
}

/* scatM Eager: root node performs carefully ordered eager puts */
/* Valid for SINGLE and LOCAL, size <= available eager buffer space */
/* Requires GASNETE_COLL_GENERIC_OPT_P2P on non-root nodes */
static int gasnete_coll_pf_scatM_Eager(gasnete_coll_op_t *op GASNETE_THREAD_FARG) {
  gasnete_coll_generic_data_t *data = op->data;
  const gasnete_coll_scatterM_args_t *args = GASNETE_COLL_GENERIC_ARGS(data, scatterM);
  int result = 0;

  switch (data->state) {
    case 0:	/* Optional IN barrier */
      if (!gasnete_coll_generic_insync(data)) {
	break;
      }
      data->state = 1;

    case 1:	/* Data movement */
      if (gasnete_mynode == args->srcnode) {
	const void * const src   = args->src;
	size_t nbytes = args->nbytes;
	uintptr_t src_addr;
	int i;

	/* Send to nodes to the "right" of ourself */
	if (gasnete_mynode < gasnete_nodes - 1) {
	  src_addr = (uintptr_t)gasnete_coll_scale_ptr(src, gasnete_coll_all_offset[gasnete_mynode + 1], nbytes);
	  for (i = gasnete_mynode + 1; i < gasnete_nodes; ++i) {
	    const size_t count = gasnete_coll_all_images[i];

	    gasnete_coll_p2p_eager_putM(op, i, (void *)src_addr, count, nbytes, 0, 1);
	    src_addr += count * nbytes;
	  }
	}
	/* Send to nodes to the "left" of ourself */
	if (gasnete_mynode > 0) {
	  src_addr = (uintptr_t)gasnete_coll_scale_ptr(src, 0, nbytes);
	  for (i = 0; i < gasnete_mynode; ++i) {
	    const size_t count = gasnete_coll_all_images[i];

	    gasnete_coll_p2p_eager_putM(op, i, (void *)src_addr, count, nbytes, 0, 1);
	    src_addr += count * nbytes;
	  }
	}

	/* Local data movement */
	gasnete_coll_local_scatter(gasnete_coll_my_images,
				   &GASNETE_COLL_MY_1ST_IMAGE(args->dstlist, op->flags),
				   gasnete_coll_scale_ptr(src, gasnete_coll_my_offset, nbytes), nbytes);
      } else {
	gasnete_coll_p2p_t *p2p = data->p2p;
	volatile uint32_t *state;
	size_t nbytes = args->nbytes;
	void * const *p;
	uintptr_t src_addr;
	int i, done;

	gasneti_assert(p2p != NULL);
	gasneti_assert(p2p->state != NULL);
	state = data->p2p->state;
	gasneti_assert(p2p->data != NULL);

	done = 1;
	p = &GASNETE_COLL_MY_1ST_IMAGE(args->dstlist, op->flags);
	src_addr = (uintptr_t)(p2p->data);
	for (i = 0; i < gasnete_coll_my_images; ++i, ++p, src_addr += nbytes) {
	  uint32_t s = state[i];

	  if (s == 0) {
	    /* Nothing received yet */
	    done = 0;
	  } else {
	    /* Received but not yet copied into place */
	    gasneti_sync_reads();
	    GASNETE_FAST_UNALIGNED_MEMCPY(*p, (void *)src_addr, nbytes);
	    state[i] = 2;
	  }
	}

	if (!done) { break; }
      }
      data->state = 2;

    case 2:	/* Optional OUT barrier */
      if (!gasnete_coll_generic_outsync(data)) {
	break;
      }

      gasnete_coll_generic_free(data GASNETE_THREAD_PASS);
      result = (GASNETE_COLL_OP_COMPLETE | GASNETE_COLL_OP_INACTIVE);
  }

  return result;
}
extern gasnet_coll_handle_t
gasnete_coll_scatM_Eager(gasnet_team_handle_t team,
			  void * const dstlist[],
			  gasnet_image_t srcimage, void *src,
			  size_t nbytes, int flags GASNETE_THREAD_FARG)
{
  int options = GASNETE_COLL_GENERIC_OPT_INSYNC_IF (flags & GASNET_COLL_IN_ALLSYNC) |
		GASNETE_COLL_GENERIC_OPT_OUTSYNC_IF(flags & GASNET_COLL_OUT_ALLSYNC)|
		GASNETE_COLL_GENERIC_OPT_P2P_IF(!gasnete_coll_image_is_local(srcimage));

  return gasnete_coll_generic_scatterM_nb(team, dstlist, srcimage, src, nbytes, flags,
					    &gasnete_coll_pf_scatM_Eager, options GASNETE_THREAD_PASS);
}

/* scatM RVGet: root node scatters address, others get from that address */
/* Valid for SINGLE and LOCAL, any size */
/* Requires GASNETE_COLL_GENERIC_OPT_P2P on non-root nodes */
static int gasnete_coll_pf_scatM_RVGet(gasnete_coll_op_t *op GASNETE_THREAD_FARG) {
  gasnete_coll_generic_data_t *data = op->data;
  const gasnete_coll_scatterM_args_t *args = GASNETE_COLL_GENERIC_ARGS(data, scatterM);
  int result = 0;

  switch (data->state) {
    case 0:	/* Optional IN barrier */
      if (!gasnete_coll_generic_insync(data)) {
	break;
      }

    case 1:	/* Initiate data movement */
      if (gasnete_mynode == args->srcnode) {
	gasnete_coll_p2p_eager_addr_all(op, args->src, 0, 1);	/* broadcast src address */
	gasnete_coll_local_scatter(gasnete_coll_my_images,
				   &GASNETE_COLL_MY_1ST_IMAGE(args->dstlist, op->flags),
				   gasnete_coll_scale_ptr(args->src, gasnete_coll_my_offset, args->nbytes),
				   args->nbytes);
      } else if (GASNETE_COLL_CHECK_OWNER(data) && data->p2p->state[0]) {
	gasneti_sync_reads();
	data->private_data = gasnete_coll_scale_ptr(*(void **)data->p2p->data,
					       gasnete_coll_my_offset,
					       args->nbytes);
	data->handle = gasnete_geti(gasnete_synctype_nb,
				    gasnete_coll_my_images,
				    &GASNETE_COLL_MY_1ST_IMAGE(args->dstlist, op->flags), args->nbytes,
				    args->srcnode, 1, &(data->private_data),
				    args->nbytes * gasnete_coll_my_images GASNETE_THREAD_PASS);
      } else {
	break;	/* Stalled until owner thread receives address */
      }
      data->state = 2;

    case 2:
      if (!gasnete_coll_generic_syncnb(data GASNETE_THREAD_PASS)) {
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
gasnete_coll_scatM_RVGet(gasnet_team_handle_t team,
			  void * const dstlist[],
			  gasnet_image_t srcimage, void *src,
			  size_t nbytes, int flags GASNETE_THREAD_FARG)
{
  int options = GASNETE_COLL_GENERIC_OPT_INSYNC_IF (flags & GASNET_COLL_IN_ALLSYNC) |
		GASNETE_COLL_GENERIC_OPT_OUTSYNC_IF(!(flags & GASNET_COLL_OUT_NOSYNC)) |
		GASNETE_COLL_GENERIC_OPT_P2P_IF(!gasnete_coll_image_is_local(srcimage));

  return gasnete_coll_generic_scatterM_nb(team, dstlist, srcimage, src, nbytes, flags,
					    &gasnete_coll_pf_scatM_RVGet, options GASNETE_THREAD_PASS);
}

extern gasnet_coll_handle_t
gasnete_coll_generic_scatterM_nb(gasnet_team_handle_t team,
				 void * const dstlist[],
				 gasnet_image_t srcimage, void *src,
				 size_t nbytes, int flags,
				 gasnete_coll_poll_fn poll_fn, int options
				 GASNETE_THREAD_FARG) {
    gasnete_coll_generic_data_t *data = gasnete_coll_generic_alloc(GASNETE_THREAD_PASS_ALONE);
    GASNETE_COLL_GENERIC_SET_TAG(data, scatterM);
    data->args.scatterM.dstlist    = dstlist;
    #if !GASNET_SEQ
      data->args.scatterM.srcimage = srcimage;
    #endif
    data->args.scatterM.srcnode    = gasnete_coll_image_node(srcimage);
    data->args.scatterM.src        = src;
    data->args.scatterM.nbytes     = nbytes;
    data->options = options;
    return gasnete_coll_op_generic_init(team, flags, data, poll_fn GASNETE_THREAD_PASS);
}

#ifndef gasnete_coll_scatterM_nb
    extern gasnet_coll_handle_t
    gasnete_coll_scatterM_nb(gasnet_team_handle_t team,
			       void * const dstlist[],
			       gasnet_image_t srcimage, void *src,
			       size_t nbytes, int flags GASNETE_THREAD_FARG)
    {
      const size_t eager_limit = GASNETE_COLL_P2P_EAGER_MIN;

      /* "Discover" in-segment flags if needed/possible */
      flags = gasnete_coll_segment_checkM(flags, 0, 0, dstlist, nbytes,
						 1, srcimage, src, nbytes*gasnete_nodes);

      /* Choose algorithm based on arguments */
      if ((flags & GASNET_COLL_DST_IN_SEGMENT) && (flags & GASNET_COLL_SRC_IN_SEGMENT)) {
	/* Both ends are in-segment */
        if ((flags & GASNET_COLL_IN_MYSYNC) || (flags & GASNET_COLL_LOCAL)) {
	  if (nbytes <= eager_limit) {
	    return gasnete_coll_scatM_Eager(team, dstlist, srcimage, src, nbytes, flags GASNETE_THREAD_PASS);
	  } else {
            return gasnete_coll_scatM_RVGet(team, dstlist, srcimage, src, nbytes, flags GASNETE_THREAD_PASS);
	  }
        } else if ((flags & GASNET_COLL_OUT_MYSYNC) && (nbytes <= eager_limit)) {
	  return gasnete_coll_scatM_Eager(team, dstlist, srcimage, src, nbytes, flags GASNETE_THREAD_PASS);
        } else {
	  return gasnete_coll_scatM_Get(team, dstlist, srcimage, src, nbytes, flags GASNETE_THREAD_PASS);
        }
      } else if (flags & GASNET_COLL_DST_IN_SEGMENT) {
	/* Only the destination is in-segment */
	gasneti_fatalerror("Currently only in-segment data is supported for this operation");
	/* XXX: IMPLEMENT THIS */
	return GASNET_COLL_INVALID_HANDLE;
      } else if (flags & GASNET_COLL_SRC_IN_SEGMENT) {
	/* Only the source is in-segment */
	gasneti_fatalerror("Currently only in-segment data is supported for this operation");
	/* XXX: IMPLEMENT THIS */
	return GASNET_COLL_INVALID_HANDLE;
      } else {
	/* Nothing is in-segment */
	gasneti_fatalerror("Currently only in-segment data is supported for this operation");
	/* XXX: IMPLEMENT THIS */
	return GASNET_COLL_INVALID_HANDLE;
      }
    }
#endif

/*---------------------------------------------------------------------------------*/
/* gasnete_coll_gather_nb() */

/* gath Get: all nodes perform uncoordinated gets */
/* Valid for SINGLE only, any size */
static int gasnete_coll_pf_gath_Get(gasnete_coll_op_t *op GASNETE_THREAD_FARG) {
  gasnete_coll_generic_data_t *data = op->data;
  const gasnete_coll_gather_args_t *args = GASNETE_COLL_GENERIC_ARGS(data, gather);
  int result = 0;

  gasneti_assert(op->flags & GASNET_COLL_SINGLE);

  switch (data->state) {
    case 0:	/* Optional IN barrier */
      if (!gasnete_coll_generic_insync(data)) {
	break;
      }
      data->state = 1;

    case 1:	/* Initiate data movement */
      if (gasnete_mynode != args->dstnode) {
	/* Nothing to do */
      } else if (GASNETE_COLL_CHECK_OWNER(data)) {
	/* Queue GETs in an NBI access region */
	gasnete_begin_nbi_accessregion(1 GASNETE_THREAD_PASS);
	{
	  int i;
	  uintptr_t p;

	  /* Get from nodes to the "right" of ourself */
	  p = (uintptr_t)gasnete_coll_scale_ptr(args->dst, (gasnete_mynode + 1), args->nbytes);
	  for (i = gasnete_mynode + 1; i < gasnete_nodes; ++i, p += args->nbytes) {
	    gasnete_get_nbi_bulk((void *)p, i, args->src, args->nbytes GASNETE_THREAD_PASS);
	  }
	  /* Get from nodes to the "left" of ourself */
	  p = (uintptr_t)gasnete_coll_scale_ptr(args->dst, 0, args->nbytes);
	  for (i = 0; i < gasnete_mynode; ++i, p += args->nbytes) {
	    gasnete_get_nbi_bulk((void *)p, i, args->src, args->nbytes GASNETE_THREAD_PASS);
	  }
	}
	data->handle = gasnete_end_nbi_accessregion(GASNETE_THREAD_PASS_ALONE);

	/* Do local copy LAST, perhaps overlapping with communication */
	GASNETE_FAST_UNALIGNED_MEMCPY(gasnete_coll_scale_ptr(args->dst, gasnete_mynode, args->nbytes),
				      args->src, args->nbytes);
      } else {
	break;	/* Stalled until owner thread initiates RDMA */
      }
      data->state = 2;

    case 2:	/* Sync data movement */
      if (!gasnete_coll_generic_syncnb(data GASNETE_THREAD_PASS)) {
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
		      size_t nbytes, int flags GASNETE_THREAD_FARG)
{
  int options = GASNETE_COLL_GENERIC_OPT_INSYNC_IF (!(flags & GASNET_COLL_IN_NOSYNC)) |
		GASNETE_COLL_GENERIC_OPT_OUTSYNC_IF(!(flags & GASNET_COLL_OUT_NOSYNC));

  return gasnete_coll_generic_gather_nb(team, dstimage, dst, src, nbytes, flags,
					&gasnete_coll_pf_gath_Get, options GASNETE_THREAD_PASS);
}

/* gath Put: root node performs carefully ordered puts */
/* Valid for SINGLE only, any size */
static int gasnete_coll_pf_gath_Put(gasnete_coll_op_t *op GASNETE_THREAD_FARG) {
  gasnete_coll_generic_data_t *data = op->data;
  const gasnete_coll_gather_args_t *args = GASNETE_COLL_GENERIC_ARGS(data, gather);
  int result = 0;

  gasneti_assert(op->flags & GASNET_COLL_SINGLE);

  switch (data->state) {
    case 0:	/* Optional IN barrier */
      if (!gasnete_coll_generic_insync(data)) {
	break;
      }
      data->state = 1;

    case 1:	/* Initiate data movement */
      if (gasnete_mynode == args->dstnode) {
	GASNETE_FAST_UNALIGNED_MEMCPY(gasnete_coll_scale_ptr(args->dst, gasnete_mynode, args->nbytes),
				      args->src, args->nbytes);
      } else if (GASNETE_COLL_CHECK_OWNER(data)) {
	data->handle = gasnete_put_nb_bulk(args->dstnode, 
					   gasnete_coll_scale_ptr(args->dst, gasnete_mynode, args->nbytes),
					   args->src, args->nbytes GASNETE_THREAD_PASS);
      } else {
	break;	/* Stalled until owner thread initiates RDMA */
      }
      data->state = 2;

    case 2:	/* Sync data movement */
      if (!gasnete_coll_generic_syncnb(data GASNETE_THREAD_PASS)) {
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
		      size_t nbytes, int flags GASNETE_THREAD_FARG)
{
  int options = GASNETE_COLL_GENERIC_OPT_INSYNC_IF (!(flags & GASNET_COLL_IN_NOSYNC)) |
		GASNETE_COLL_GENERIC_OPT_OUTSYNC_IF(!(flags & GASNET_COLL_OUT_NOSYNC));

  return gasnete_coll_generic_gather_nb(team, dstimage, dst, src, nbytes, flags,
					&gasnete_coll_pf_gath_Put, options GASNETE_THREAD_PASS);
}

/* gath Eager: all nodes perform uncoordinated eager puts */
/* Valid for SINGLE and LOCAL, size <= available eager buffer space */
/* Requires GASNETE_COLL_GENERIC_OPT_P2P on root node */
static int gasnete_coll_pf_gath_Eager(gasnete_coll_op_t *op GASNETE_THREAD_FARG) {
  gasnete_coll_generic_data_t *data = op->data;
  const gasnete_coll_gather_args_t *args = GASNETE_COLL_GENERIC_ARGS(data, gather);
  int result = 0;

  switch (data->state) {
    case 0:	/* Optional IN barrier */
      if (!gasnete_coll_generic_insync(data)) {
	break;
      }
      data->state = 1;

      /* Initiate data movement */
      if (gasnete_mynode != args->dstnode) {
	gasnete_coll_p2p_eager_put(op, args->dstnode, args->src, args->nbytes, gasnete_mynode, 1);
      } else {
	GASNETE_FAST_UNALIGNED_MEMCPY(gasnete_coll_scale_ptr(args->dst, gasnete_mynode, args->nbytes),
				      args->src, args->nbytes);
	data->p2p->state[gasnete_mynode] = 2;
      }

    case 1:	/* Complete data movement */
      if (gasnete_mynode == args->dstnode) {
	gasnete_coll_p2p_t *p2p = data->p2p;
	gasnete_coll_p2p_entry_t *entry;
	volatile uint32_t *state;
	uintptr_t dst_addr, src_addr;
	size_t nbytes = args->nbytes;
	int i, done;

	gasneti_assert(p2p != NULL);
	gasneti_assert(p2p->state != NULL);
	state = data->p2p->state;
	gasneti_assert(p2p->data != NULL);

	done = 1;
	dst_addr = (uintptr_t)(args->dst);
	src_addr = (uintptr_t)(p2p->data);
	for (i = 0; i < gasnete_nodes; ++i, dst_addr += nbytes, src_addr += nbytes) {
	  uint32_t s = state[i];

	  if (s == 0) {
	    /* Nothing received yet */
	    done = 0;
	  } else if (s == 1) {
	    /* Received but not yet copied into place */
	    gasneti_sync_reads();
	    GASNETE_FAST_UNALIGNED_MEMCPY((void *)dst_addr, (void *)src_addr, nbytes);
	    state[i] = 2;
	  }
	}

	if (!done) { break; }
      }
      data->state = 2;

    case 2:	/* Optional OUT barrier */
      if (!gasnete_coll_generic_outsync(data)) {
	break;
      }

      gasnete_coll_generic_free(data GASNETE_THREAD_PASS);
      result = (GASNETE_COLL_OP_COMPLETE | GASNETE_COLL_OP_INACTIVE);
  }

  return result;
}
extern gasnet_coll_handle_t
gasnete_coll_gath_Eager(gasnet_team_handle_t team,
			gasnet_image_t dstimage, void *dst,
			void *src,
			size_t nbytes, int flags GASNETE_THREAD_FARG)
{
  int options = GASNETE_COLL_GENERIC_OPT_INSYNC_IF (flags & GASNET_COLL_IN_ALLSYNC) |
		GASNETE_COLL_GENERIC_OPT_OUTSYNC_IF(flags & GASNET_COLL_OUT_ALLSYNC)|
		GASNETE_COLL_GENERIC_OPT_P2P_IF(gasnete_coll_image_is_local(dstimage));

  return gasnete_coll_generic_gather_nb(team, dstimage, dst, src, nbytes, flags,
					&gasnete_coll_pf_gath_Eager, options GASNETE_THREAD_PASS);
}

/* gath RVPut: root node broadcasts addresses, others put to that address (plus offset) */
/* Valid for SINGLE and LOCAL, any size */
/* Requires GASNETE_COLL_GENERIC_OPT_P2P on non-root nodes */
static int gasnete_coll_pf_gath_RVPut(gasnete_coll_op_t *op GASNETE_THREAD_FARG) {
  gasnete_coll_generic_data_t *data = op->data;
  const gasnete_coll_gather_args_t *args = GASNETE_COLL_GENERIC_ARGS(data, gather);
  int result = 0;

  switch (data->state) {
    case 0:	/* Optional IN barrier */
      if (!gasnete_coll_generic_insync(data)) {
	break;
      }
      data->state = 1;

    case 1:	/* Initiate data movement */
      if (gasnete_mynode == args->dstnode) {
	gasnete_coll_p2p_eager_addr_all(op, args->dst, 0, 1);	/* broadcast dst address */
	GASNETE_FAST_UNALIGNED_MEMCPY(gasnete_coll_scale_ptr(args->dst, gasnete_mynode, args->nbytes),
				      args->src, args->nbytes);
      } else if (GASNETE_COLL_CHECK_OWNER(data) && data->p2p->state[0]) {
	gasneti_sync_reads();
	data->handle = gasnete_put_nb_bulk(args->dstnode,
					   gasnete_coll_scale_ptr(*(void **)data->p2p->data,
								  gasnete_mynode, args->nbytes),
					   args->src, args->nbytes GASNETE_THREAD_PASS);
      } else {
	  break;	/* Stalled until owner thread receives address */
      }
      data->state = 2;

    case 2:	/* Sync data movement */
      if (!gasnete_coll_generic_syncnb(data GASNETE_THREAD_PASS)) {
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
gasnete_coll_gath_RVPut(gasnet_team_handle_t team,
			 gasnet_image_t dstimage, void *dst,
			 void *src,
			 size_t nbytes, int flags GASNETE_THREAD_FARG)
{
  int options = GASNETE_COLL_GENERIC_OPT_INSYNC_IF (flags & GASNET_COLL_IN_ALLSYNC) |
		GASNETE_COLL_GENERIC_OPT_OUTSYNC_IF(!(flags & GASNET_COLL_OUT_NOSYNC)) |
		GASNETE_COLL_GENERIC_OPT_P2P_IF(!gasnete_coll_image_is_local(dstimage));

  return gasnete_coll_generic_gather_nb(team, dstimage, dst, src, nbytes, flags,
					&gasnete_coll_pf_gath_RVPut, options GASNETE_THREAD_PASS);
}

extern gasnet_coll_handle_t
gasnete_coll_generic_gather_nb(gasnet_team_handle_t team,
			       gasnet_image_t dstimage, void *dst,
			       void *src,
			       size_t nbytes, int flags,
			       gasnete_coll_poll_fn poll_fn, int options
			       GASNETE_THREAD_FARG) {
    gasnete_coll_generic_data_t *data = gasnete_coll_generic_alloc(GASNETE_THREAD_PASS_ALONE);
    GASNETE_COLL_GENERIC_SET_TAG(data, gather);
    #if !GASNET_SEQ
      data->args.gather.dstimage = dstimage;
    #endif
    data->args.gather.dstnode    = gasnete_coll_image_node(dstimage);
    data->args.gather.dst        = dst;
    data->args.gather.src        = src;
    data->args.gather.nbytes     = nbytes;
    data->options = options;
    return gasnete_coll_op_generic_init(team, flags, data, poll_fn GASNETE_THREAD_PASS);
}

#ifndef gasnete_coll_gather_nb
    extern gasnet_coll_handle_t
    gasnete_coll_gather_nb(gasnet_team_handle_t team,
			   gasnet_image_t dstimage, void *dst,
			   void *src,
			   size_t nbytes, int flags GASNETE_THREAD_FARG) {
      const size_t eager_limit = GASNETE_COLL_P2P_EAGER_MIN;

      /* "Discover" in-segment flags if needed/possible */
      flags = gasnete_coll_segment_check(flags, 1, dstimage, dst, nbytes*gasnete_nodes,
						0, 0, src, nbytes);

      /* Choose algorithm based on arguments */
      if ((flags & GASNET_COLL_DST_IN_SEGMENT) && (flags & GASNET_COLL_SRC_IN_SEGMENT)) {
	/* Both ends are in-segment */
        if ((flags & GASNET_COLL_IN_MYSYNC) || (flags & GASNET_COLL_LOCAL)) {
	  if (nbytes <= eager_limit) {
	    return gasnete_coll_gath_Eager(team, dstimage, dst, src, nbytes, flags GASNETE_THREAD_PASS);
	  } else {
	    return gasnete_coll_gath_RVPut(team, dstimage, dst, src, nbytes, flags GASNETE_THREAD_PASS);
	  }
        } else if ((flags & GASNET_COLL_OUT_MYSYNC) && (nbytes <= eager_limit)) {
	  return gasnete_coll_gath_Eager(team, dstimage, dst, src, nbytes, flags GASNETE_THREAD_PASS);
        } else {
	  return gasnete_coll_gath_Put(team, dstimage, dst, src, nbytes, flags GASNETE_THREAD_PASS);
        }
      } else if (flags & GASNET_COLL_DST_IN_SEGMENT) {
	/* Only the destination is in-segment */
	gasneti_fatalerror("Currently only in-segment data is supported for this operation");
	/* XXX: IMPLEMENT THIS */
	return GASNET_COLL_INVALID_HANDLE;
      } else if (flags & GASNET_COLL_SRC_IN_SEGMENT) {
	/* Only the source is in-segment */
	gasneti_fatalerror("Currently only in-segment data is supported for this operation");
	/* XXX: IMPLEMENT THIS */
	return GASNET_COLL_INVALID_HANDLE;
      } else {
	/* Nothing is in-segment */
	gasneti_fatalerror("Currently only in-segment data is supported for this operation");
	/* XXX: IMPLEMENT THIS */
	return GASNET_COLL_INVALID_HANDLE;
      }
    }
#endif

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
      if (!gasnete_coll_generic_insync(data)) {
	break;
      }
      data->state = 1;

    case 1:	/* Initiate data movement */
      if (gasnete_mynode != args->dstnode) {
	/* Nothing to do */
      } else if (GASNETE_COLL_CHECK_OWNER(data)) {
	size_t nbytes = args->nbytes;

	/* Queue GETIs in an NBI access region */
	gasnete_begin_nbi_accessregion(1 GASNETE_THREAD_PASS);
	{
	  void **q;
	  uintptr_t dst_addr;
	  int i;
	  void * const *p;
	  void ** dstlist = gasneti_malloc(gasnete_nodes * sizeof(void *));
	  data->private_data = dstlist;

	  /* Get from the "right" of ourself */
	  dst_addr = (uintptr_t)gasnete_coll_scale_ptr(args->dst,
			  			       gasnete_coll_all_offset[gasnete_mynode + 1],
						       nbytes);
	  p = &GASNETE_COLL_1ST_IMAGE(args->srclist, gasnete_mynode + 1);
	  q = &dstlist[gasnete_mynode + 1];
	  for (i = gasnete_mynode + 1; i < gasnete_nodes; ++i) {
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
	  for (i = 0; i < gasnete_mynode; ++i) {
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

	/* Do local copy LAST, perhaps overlapping with communication */
	gasnete_coll_local_gather(gasnete_coll_my_images,
				  gasnete_coll_scale_ptr(args->dst, gasnete_coll_my_offset, nbytes),
				  &GASNETE_COLL_MY_1ST_IMAGE(args->srclist, 0), nbytes);
      } else {
	break;	/* Stalled until owner thread initiates RDMA */
      }
      data->state = 2;

    case 2:	/* Sync data movement */
      if (gasnete_mynode == args->dstnode) {
        if (!gasnete_coll_generic_syncnb(data GASNETE_THREAD_PASS)) {
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
		       size_t nbytes, int flags GASNETE_THREAD_FARG)
{
  int options = GASNETE_COLL_GENERIC_OPT_INSYNC_IF (!(flags & GASNET_COLL_IN_NOSYNC)) |
		GASNETE_COLL_GENERIC_OPT_OUTSYNC_IF(!(flags & GASNET_COLL_OUT_NOSYNC));

  return gasnete_coll_generic_gatherM_nb(team, dstimage, dst, srclist, nbytes, flags,
					 &gasnete_coll_pf_gathM_Get, options GASNETE_THREAD_PASS);
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
      if (!gasnete_coll_generic_insync(data)) {
	break;
      }
      data->state = 1;

    case 1:	/* Initiate data movement */
      if (gasnete_mynode == args->dstnode) {
	gasnete_coll_local_gather(gasnete_coll_my_images,
				  gasnete_coll_scale_ptr(args->dst, gasnete_coll_my_offset, args->nbytes),
				  &GASNETE_COLL_MY_1ST_IMAGE(args->srclist, 0), args->nbytes);
      } else if (GASNETE_COLL_CHECK_OWNER(data)) {
	data->private_data = gasnete_coll_scale_ptr(args->dst, gasnete_coll_my_offset, args->nbytes);
	data->handle = gasnete_puti(gasnete_synctype_nb, args->dstnode,
				    1, &(data->private_data), gasnete_coll_my_images * args->nbytes,
				    gasnete_coll_my_images, &GASNETE_COLL_MY_1ST_IMAGE(args->srclist, 0),
				    args->nbytes GASNETE_THREAD_PASS);
      } else {
	break;	/* Stalled until owner thread initiates RDMA */
      }
      data->state = 2;

    case 2:	/* Sync data movement */
      if (!gasnete_coll_generic_syncnb(data GASNETE_THREAD_PASS)) {
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
		       size_t nbytes, int flags GASNETE_THREAD_FARG)
{
  int options = GASNETE_COLL_GENERIC_OPT_INSYNC_IF (!(flags & GASNET_COLL_IN_NOSYNC)) |
		GASNETE_COLL_GENERIC_OPT_OUTSYNC_IF(!(flags & GASNET_COLL_OUT_NOSYNC));

  return gasnete_coll_generic_gatherM_nb(team, dstimage, dst, srclist, nbytes, flags,
					 &gasnete_coll_pf_gathM_Put, options GASNETE_THREAD_PASS);
}

/* gathM Eager: all nodes perform uncoordinated eager puts */
/* Valid for SINGLE and LOCAL, size <= available eager buffer space */
/* Requires GASNETE_COLL_GENERIC_OPT_P2P on root node */
static int gasnete_coll_pf_gathM_Eager(gasnete_coll_op_t *op GASNETE_THREAD_FARG) {
  gasnete_coll_generic_data_t *data = op->data;
  const gasnete_coll_gatherM_args_t *args = GASNETE_COLL_GENERIC_ARGS(data, gatherM);
  int result = 0;

  switch (data->state) {
    case 0:	/* Optional IN barrier */
      if (!gasnete_coll_generic_insync(data)) {
	break;
      }
      data->state = 1;

      /* Initiate data movement */
      if (gasnete_mynode != args->dstnode) {
	size_t nbytes = args->nbytes;
	void * tmp = gasneti_malloc(gasnete_coll_my_images * nbytes);
	gasnete_coll_local_gather(gasnete_coll_my_images, tmp,
				  &GASNETE_COLL_MY_1ST_IMAGE(args->srclist, op->flags), nbytes);
        gasnete_coll_p2p_eager_putM(op, args->dstnode, tmp, gasnete_coll_my_images,
				    nbytes, gasnete_coll_my_offset, 1);
	gasneti_free(tmp);
      } else {
	volatile uint32_t *s;
	int i;

	gasnete_coll_local_gather(gasnete_coll_my_images,
				  gasnete_coll_scale_ptr(args->dst, gasnete_coll_my_offset, args->nbytes),
				  &GASNETE_COLL_MY_1ST_IMAGE(args->srclist, op->flags), args->nbytes);
	s = &(data->p2p->state[gasnete_coll_my_offset]);
	for (i = 0; i < gasnete_coll_my_images; ++i) {
	  *(s++) = 2;
        }
      }

    case 1:	/* Complete data movement */
      if (gasnete_mynode == args->dstnode) {
	gasnete_coll_p2p_t *p2p = data->p2p;
	gasnete_coll_p2p_entry_t *entry;
	volatile uint32_t *state;
	uintptr_t dst_addr, src_addr;
	size_t nbytes = args->nbytes;
	int i, done;

	gasneti_assert(p2p != NULL);
	gasneti_assert(p2p->state != NULL);
	state = data->p2p->state;
	gasneti_assert(p2p->data != NULL);

	done = 1;
	dst_addr = (uintptr_t)(args->dst);
	src_addr = (uintptr_t)(p2p->data);
	for (i = 0; i < gasnete_coll_total_images; ++i, dst_addr += nbytes, src_addr += nbytes) {
	  uint32_t s = state[i];

	  if (s == 0) {
	    /* Nothing received yet */
	    done = 0;
	  } else if (s == 1) {
	    /* Received but not yet copied into place */
	    gasneti_sync_reads();
	    GASNETE_FAST_UNALIGNED_MEMCPY((void *)dst_addr, (void *)src_addr, nbytes);
	    state[i] = 2;
	  }
	}

	if (!done) { break; }
      }
      data->state = 2;

    case 2:	/* Optional OUT barrier */
      if (!gasnete_coll_generic_outsync(data)) {
	break;
      }

      gasnete_coll_generic_free(data GASNETE_THREAD_PASS);
      result = (GASNETE_COLL_OP_COMPLETE | GASNETE_COLL_OP_INACTIVE);
  }

  return result;
}
extern gasnet_coll_handle_t
gasnete_coll_gathM_Eager(gasnet_team_handle_t team,
			 gasnet_image_t dstimage, void *dst,
			 void * const srclist[],
			 size_t nbytes, int flags GASNETE_THREAD_FARG)
{
  int options = GASNETE_COLL_GENERIC_OPT_INSYNC_IF (flags & GASNET_COLL_IN_ALLSYNC) |
		GASNETE_COLL_GENERIC_OPT_OUTSYNC_IF(flags & GASNET_COLL_OUT_ALLSYNC) |
		GASNETE_COLL_GENERIC_OPT_P2P_IF(gasnete_coll_image_is_local(dstimage));

  return gasnete_coll_generic_gatherM_nb(team, dstimage, dst, srclist, nbytes, flags,
					 &gasnete_coll_pf_gathM_Eager, options GASNETE_THREAD_PASS);
}

/* gathM RVPut: root node broadcasts addresses, others put to that address (plus offset) */
/* Valid for SINGLE and LOCAL, any size */
/* Requires GASNETE_COLL_GENERIC_OPT_P2P on non-root nodes */
static int gasnete_coll_pf_gathM_RVPut(gasnete_coll_op_t *op GASNETE_THREAD_FARG) {
  gasnete_coll_generic_data_t *data = op->data;
  const gasnete_coll_gatherM_args_t *args = GASNETE_COLL_GENERIC_ARGS(data, gatherM);
  int result = 0;

  switch (data->state) {
    case 0:	/* Optional IN barrier */
      if (!gasnete_coll_generic_insync(data)) {
	break;
      }
      data->state = 1;

    case 1:
      if (gasnete_mynode == args->dstnode) {
	gasnete_coll_p2p_eager_addr_all(op, args->dst, 0, 1);	/* broadcast dst address */
	gasnete_coll_local_gather(gasnete_coll_my_images,
				  gasnete_coll_scale_ptr(args->dst, gasnete_coll_my_offset, args->nbytes),
				  &GASNETE_COLL_MY_1ST_IMAGE(args->srclist, op->flags), args->nbytes);
      } else if (GASNETE_COLL_CHECK_OWNER(data) && data->p2p->state[0]) {
	gasneti_sync_reads();
	data->private_data = gasnete_coll_scale_ptr(*(void **)data->p2p->data, gasnete_coll_my_offset, args->nbytes);
	data->handle = gasnete_puti(gasnete_synctype_nb, args->dstnode,
				    1, &(data->private_data), args->nbytes * gasnete_coll_my_images,
				    gasnete_coll_my_images,
				    &GASNETE_COLL_MY_1ST_IMAGE(args->srclist, op->flags),
				    args->nbytes GASNETE_THREAD_PASS);
      } else {
	break;	/* Stalled until owner thread initiates receives address */
      }
      data->state = 2;

    case 2:
      if (!gasnete_coll_generic_syncnb(data GASNETE_THREAD_PASS)) {
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
gasnete_coll_gathM_RVPut(gasnet_team_handle_t team,
			 gasnet_image_t dstimage, void *dst,
			 void * const srclist[],
			 size_t nbytes, int flags GASNETE_THREAD_FARG)
{
  int options = GASNETE_COLL_GENERIC_OPT_INSYNC_IF (flags & GASNET_COLL_IN_ALLSYNC) |
		GASNETE_COLL_GENERIC_OPT_OUTSYNC_IF(!(flags & GASNET_COLL_OUT_NOSYNC)) |
		GASNETE_COLL_GENERIC_OPT_P2P_IF(!gasnete_coll_image_is_local(dstimage));

  return gasnete_coll_generic_gatherM_nb(team, dstimage, dst, srclist, nbytes, flags,
					 &gasnete_coll_pf_gathM_RVPut, options GASNETE_THREAD_PASS);
}

extern gasnet_coll_handle_t
gasnete_coll_generic_gatherM_nb(gasnet_team_handle_t team,
				gasnet_image_t dstimage, void *dst,
				void * const srclist[],
				size_t nbytes, int flags,
				gasnete_coll_poll_fn poll_fn, int options
				GASNETE_THREAD_FARG) {
    gasnete_coll_generic_data_t *data = gasnete_coll_generic_alloc(GASNETE_THREAD_PASS_ALONE);
    GASNETE_COLL_GENERIC_SET_TAG(data, gatherM);
    #if !GASNET_SEQ
      data->args.gatherM.dstimage = dstimage;
    #endif
    data->args.gatherM.dstnode    = gasnete_coll_image_node(dstimage);
    data->args.gatherM.dst        = dst;
    data->args.gatherM.srclist    = srclist;
    data->args.gatherM.nbytes     = nbytes;
    data->options = options;
    return gasnete_coll_op_generic_init(team, flags, data, poll_fn GASNETE_THREAD_PASS);
}

#ifndef gasnete_coll_gatherM_nb
    extern gasnet_coll_handle_t
    gasnete_coll_gatherM_nb(gasnet_team_handle_t team,
			    gasnet_image_t dstimage, void *dst,
			    void * const srclist[],
			    size_t nbytes, int flags GASNETE_THREAD_FARG) {
      const size_t eager_limit = GASNETE_COLL_P2P_EAGER_MIN;

      /* "Discover" in-segment flags if needed/possible */
      flags = gasnete_coll_segment_checkM(flags, 1, dstimage, dst, nbytes*gasnete_nodes,
						 0, 0, srclist, nbytes);

      /* Choose algorithm based on arguments */
      if ((flags & GASNET_COLL_DST_IN_SEGMENT) && (flags & GASNET_COLL_SRC_IN_SEGMENT)) {
	/* Both ends are in-segment */
        if ((flags & GASNET_COLL_IN_MYSYNC) || (flags & GASNET_COLL_LOCAL)) {
	  if (nbytes <= eager_limit) {
	    return gasnete_coll_gathM_Eager(team, dstimage, dst, srclist, nbytes, flags GASNETE_THREAD_PASS);
	  } else {
	    return gasnete_coll_gathM_RVPut(team, dstimage, dst, srclist, nbytes, flags GASNETE_THREAD_PASS);
	  }
        } else if ((flags & GASNET_COLL_OUT_MYSYNC) && (nbytes <= eager_limit)) {
	  return gasnete_coll_gathM_Eager(team, dstimage, dst, srclist, nbytes, flags GASNETE_THREAD_PASS);
        } else {
	  return gasnete_coll_gathM_Put(team, dstimage, dst, srclist, nbytes, flags GASNETE_THREAD_PASS);
        }
      } else if (flags & GASNET_COLL_DST_IN_SEGMENT) {
	/* Only the destination is in-segment */
	gasneti_fatalerror("Currently only in-segment data is supported for this operation");
	/* XXX: IMPLEMENT THIS */
	return GASNET_COLL_INVALID_HANDLE;
      } else if (flags & GASNET_COLL_SRC_IN_SEGMENT) {
	/* Only the source is in-segment */
	gasneti_fatalerror("Currently only in-segment data is supported for this operation");
	/* XXX: IMPLEMENT THIS */
	return GASNET_COLL_INVALID_HANDLE;
      } else {
	/* Nothing is in-segment */
	gasneti_fatalerror("Currently only in-segment data is supported for this operation");
	/* XXX: IMPLEMENT THIS */
	return GASNET_COLL_INVALID_HANDLE;
      }
    }
#endif

/*---------------------------------------------------------------------------------*/
/* gasnete_coll_gather_all_nb() */

/* gall Gath: Implement gather_all in terms of simultaneous gathers */
/* This is meant mostly as an example and a short-term solution */
/* Valid wherever the underlying gather is valid */
static int gasnete_coll_pf_gall_Gath(gasnete_coll_op_t *op GASNETE_THREAD_FARG) {
  gasnete_coll_generic_data_t *data = op->data;
  const gasnete_coll_gather_all_args_t *args = GASNETE_COLL_GENERIC_ARGS(data, gather_all);
  int result = 0;

  switch (data->state) {
    case 0:	/* Optional IN barrier */
      if (!gasnete_coll_generic_insync(data)) {
	break;
      }
      data->state = 1;

    case 1:	/* Initiate data movement */
      if (GASNETE_COLL_CHECK_OWNER(data)) {
	gasnet_coll_handle_t *h;
        int flags = op->flags;
	gasnet_team_handle_t team = op->team;
	void *dst = args->dst;
	void *src = args->src;
	size_t nbytes = args->nbytes;
        gasnet_image_t i;

	/* XXX: freelist ? */
	h = gasneti_malloc(gasnete_nodes * sizeof(gasnet_coll_handle_t));
	data->private_data = h;

        flags &= (GASNET_COLL_SINGLE | GASNET_COLL_LOCAL |
		  GASNET_COLL_DST_IN_SEGMENT | GASNET_COLL_SRC_IN_SEGMENT);
	flags |= (GASNET_COLL_IN_NOSYNC | GASNET_COLL_OUT_NOSYNC);

        for (i = 0; i < gasnete_coll_total_images; ++i, ++h) {
          *h = gasnete_coll_gather_nb(team, i, dst, src, nbytes, flags GASNETE_THREAD_PASS);
        }
      } else {
	break;	/* Stalled until owner thread initiates gathers */
      }
      data->state = 2;

    case 2:	/* Sync data movement */
      if (!gasnete_coll_generic_coll_sync(data->private_data, gasnete_nodes GASNETE_THREAD_PASS)) {
	break;
      }
      data->state = 3;

    case 3:	/* Optional OUT barrier */
      if (!gasnete_coll_generic_outsync(data)) {
	break;
      }

      gasneti_free(data->private_data);
      gasnete_coll_generic_free(data GASNETE_THREAD_PASS);
      result = (GASNETE_COLL_OP_COMPLETE | GASNETE_COLL_OP_INACTIVE);
  }

  return result;
}
extern gasnet_coll_handle_t
gasnete_coll_gall_Gath(gasnet_team_handle_t team,
		       void *dst, void *src,
		       size_t nbytes, int flags GASNETE_THREAD_FARG)
{
  int options = GASNETE_COLL_GENERIC_OPT_INSYNC_IF (!(flags & GASNET_COLL_IN_NOSYNC)) |
		GASNETE_COLL_GENERIC_OPT_OUTSYNC_IF(!(flags & GASNET_COLL_OUT_NOSYNC));

  /* XXX: until gather deals w/ out-of-segment data */
  if_pf (!(flags & GASNET_COLL_DST_IN_SEGMENT) || !(flags & GASNET_COLL_SRC_IN_SEGMENT)) {
    gasneti_fatalerror("Currently only in-segment data is supported for this operation");
    return GASNET_COLL_INVALID_HANDLE;
  }

  return gasnete_coll_generic_gather_all_nb(team, dst, src, nbytes, flags,
					    &gasnete_coll_pf_gall_Gath, options GASNETE_THREAD_PASS);
}

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
    return gasnete_coll_op_generic_init(team, flags, data, poll_fn GASNETE_THREAD_PASS);
}

#ifndef gasnete_coll_gather_all_nb
    extern gasnet_coll_handle_t
    gasnete_coll_gather_all_nb(gasnet_team_handle_t team,
			       void *dst, void *src,
			       size_t nbytes, int flags GASNETE_THREAD_FARG) {
      /* "Discover" in-segment flags if needed/possible */
      flags = gasnete_coll_segment_check(flags, 0, 0, dst, nbytes*gasnete_nodes,
						0, 0, src, nbytes);

      /* XXX: need more implementations to choose from here */
      return gasnete_coll_gall_Gath(team, dst, src, nbytes, flags GASNETE_THREAD_PASS);
    }
#endif

/*---------------------------------------------------------------------------------*/
/* gasnete_coll_gather_allM_nb() */

/* gallM Gath: Implement gather_allM in terms of simultaneous gathers */
/* This is meant mostly as an example and a short-term solution */
/* Valid wherever the underlying gather is valid */
static int gasnete_coll_pf_gallM_Gath(gasnete_coll_op_t *op GASNETE_THREAD_FARG) {
  gasnete_coll_generic_data_t *data = op->data;
  const gasnete_coll_gather_allM_args_t *args = GASNETE_COLL_GENERIC_ARGS(data, gather_allM);
  int result = 0;

  switch (data->state) {
    case 0:	/* Optional IN barrier */
      if (!gasnete_coll_generic_insync(data)) {
	break;
      }
      data->state = 1;

    case 1:	/* Initiate data movement */
      if (GASNETE_COLL_CHECK_OWNER(data)) {
	gasnet_coll_handle_t *h;
        int flags = op->flags;
	gasnet_team_handle_t team = op->team;
	void * const *p = args->dstlist;
	void * const *srclist = args->srclist;
	size_t nbytes = args->nbytes;
	gasnet_image_t i;

	/* XXX: freelist ? */
	h = gasneti_malloc(gasnete_coll_total_images * sizeof(gasnet_coll_handle_t));
	data->private_data = h;

        flags &= (GASNET_COLL_SINGLE | GASNET_COLL_LOCAL |
		  GASNET_COLL_DST_IN_SEGMENT | GASNET_COLL_SRC_IN_SEGMENT);
	flags |= (GASNET_COLL_IN_NOSYNC | GASNET_COLL_OUT_NOSYNC);

        for (i = 0; i < gasnete_coll_total_images; ++i, ++h, ++p) {
          *h = gasnete_coll_gatherM_nb(team, i, *p, srclist, nbytes, flags GASNETE_THREAD_PASS);
        }
      } else {
	break;	/* Stalled until owner thread initiates gathers */
      }
      data->state = 2;

    case 2:	/* Sync data movement */
      if (!gasnete_coll_generic_coll_sync(data->private_data, gasnete_coll_total_images GASNETE_THREAD_PASS)) {
	break;
      }
      data->state = 3;

    case 3:	/* Optional OUT barrier */
      if (!gasnete_coll_generic_outsync(data)) {
	break;
      }

      gasneti_free(data->private_data);
      gasnete_coll_generic_free(data GASNETE_THREAD_PASS);
      result = (GASNETE_COLL_OP_COMPLETE | GASNETE_COLL_OP_INACTIVE);
  }

  return result;
}
extern gasnet_coll_handle_t
gasnete_coll_gallM_Gath(gasnet_team_handle_t team,
			void * const dstlist[], void * const srclist[],
			size_t nbytes, int flags GASNETE_THREAD_FARG)
{
  int options = GASNETE_COLL_GENERIC_OPT_INSYNC_IF (!(flags & GASNET_COLL_IN_NOSYNC)) |
		GASNETE_COLL_GENERIC_OPT_OUTSYNC_IF(!(flags & GASNET_COLL_OUT_NOSYNC));

  /* XXX: until gatherM deals w/ out-of-segment data */
  if_pf (!(flags & GASNET_COLL_DST_IN_SEGMENT) || !(flags & GASNET_COLL_SRC_IN_SEGMENT)) {
    gasneti_fatalerror("Currently only in-segment data is supported for this operation");
    return GASNET_COLL_INVALID_HANDLE;
  }

  return gasnete_coll_generic_gather_allM_nb(team, dstlist, srclist, nbytes, flags,
					     &gasnete_coll_pf_gallM_Gath, options GASNETE_THREAD_PASS);
}

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
    return gasnete_coll_op_generic_init(team, flags, data, poll_fn GASNETE_THREAD_PASS);
}

#ifndef gasnete_coll_gather_allM_nb
    extern gasnet_coll_handle_t
    gasnete_coll_gather_allM_nb(gasnet_team_handle_t team,
				void * const dstlist[], void * const srclist[],
				size_t nbytes, int flags GASNETE_THREAD_FARG) {
      /* "Discover" in-segment flags if needed/possible */
      flags = gasnete_coll_segment_checkM(flags, 0, 0, dstlist, nbytes*gasnete_nodes,
						 0, 0, srclist, nbytes);

      /* XXX: need more implementations to choose from here */
      return gasnete_coll_gallM_Gath(team, dstlist, srclist, nbytes, flags GASNETE_THREAD_PASS);
    }
#endif

/*---------------------------------------------------------------------------------*/
/* gasnete_coll_exchange_nb() */

/* exchg Gath: Implement exchange in terms of simultaneous gathers */
/* This is meant mostly as an example and a short-term solution */
/* Valid wherever the underlying gather is valid */
static int gasnete_coll_pf_exchg_Gath(gasnete_coll_op_t *op GASNETE_THREAD_FARG) {
  gasnete_coll_generic_data_t *data = op->data;
  const gasnete_coll_exchange_args_t *args = GASNETE_COLL_GENERIC_ARGS(data, exchange);
  int result = 0;

  switch (data->state) {
    case 0:	/* Optional IN barrier */
      if (!gasnete_coll_generic_insync(data)) {
	break;
      }
      data->state = 1;

    case 1:	/* Initiate data movement */
      if (GASNETE_COLL_CHECK_OWNER(data)) {
	gasnet_coll_handle_t *h;
        int flags = op->flags;
	gasnet_team_handle_t team = op->team;
	void *dst = args->dst;
	uintptr_t src_addr = (uintptr_t)args->src;
	size_t nbytes = args->nbytes;
        gasnet_image_t i;

	/* XXX: freelist ? */
	h = gasneti_malloc(gasnete_nodes * sizeof(gasnet_coll_handle_t));
	data->private_data = h;

        flags &= (GASNET_COLL_SINGLE | GASNET_COLL_LOCAL |
		  GASNET_COLL_DST_IN_SEGMENT | GASNET_COLL_SRC_IN_SEGMENT);
	flags |= (GASNET_COLL_IN_NOSYNC | GASNET_COLL_OUT_NOSYNC);

        for (i = 0; i < gasnete_coll_total_images; ++i, ++h, src_addr += nbytes) {
          *h = gasnete_coll_gather_nb(team, i, dst, (void *)src_addr, nbytes, flags GASNETE_THREAD_PASS);
        }
      } else {
	break;	/* Stalled until owner thread initiates gathers */
      }
      data->state = 2;

    case 2:	/* Sync data movement */
      if (!gasnete_coll_generic_coll_sync(data->private_data, gasnete_nodes GASNETE_THREAD_PASS)) {
	break;
      }
      data->state = 3;

    case 3:	/* Optional OUT barrier */
      if (!gasnete_coll_generic_outsync(data)) {
	break;
      }

      gasneti_free(data->private_data);
      gasnete_coll_generic_free(data GASNETE_THREAD_PASS);
      result = (GASNETE_COLL_OP_COMPLETE | GASNETE_COLL_OP_INACTIVE);
  }

  return result;
}
extern gasnet_coll_handle_t
gasnete_coll_exchg_Gath(gasnet_team_handle_t team,
			void *dst, void *src,
			size_t nbytes, int flags GASNETE_THREAD_FARG)
{
  int options = GASNETE_COLL_GENERIC_OPT_INSYNC_IF (!(flags & GASNET_COLL_IN_NOSYNC)) |
		GASNETE_COLL_GENERIC_OPT_OUTSYNC_IF(!(flags & GASNET_COLL_OUT_NOSYNC));


  /* XXX: until gather deals w/ out-of-segment data */
  if_pf (!(flags & GASNET_COLL_DST_IN_SEGMENT) || !(flags & GASNET_COLL_SRC_IN_SEGMENT)) {
    gasneti_fatalerror("Currently only in-segment data is supported for this operation");
    return GASNET_COLL_INVALID_HANDLE;
  }

  return gasnete_coll_generic_exchange_nb(team, dst, src, nbytes, flags,
					  &gasnete_coll_pf_exchg_Gath, options GASNETE_THREAD_PASS);
}

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
    return gasnete_coll_op_generic_init(team, flags, data, poll_fn GASNETE_THREAD_PASS);
}

#ifndef gasnete_coll_exchange_nb
    extern gasnet_coll_handle_t
    gasnete_coll_exchange_nb(gasnet_team_handle_t team,
			     void *dst, void *src,
			     size_t nbytes, int flags GASNETE_THREAD_FARG) {
      /* "Discover" in-segment flags if needed/possible */
      flags = gasnete_coll_segment_check(flags, 0, 0, dst, nbytes*gasnete_nodes,
						0, 0, src, nbytes*gasnete_nodes);

     /* XXX: need more implementations to choose from here */
     return gasnete_coll_exchg_Gath(team, dst, src, nbytes, flags GASNETE_THREAD_PASS);
    }
#endif

/*---------------------------------------------------------------------------------*/
/* gasnete_coll_exchangeM_nb() */

/* exchgM Gath: Implement exchangeM in terms of simultaneous gathers */
/* This is meant mostly as an example and a short-term solution */
/* Valid wherever the underlying gather is valid */
static int gasnete_coll_pf_exchgM_Gath(gasnete_coll_op_t *op GASNETE_THREAD_FARG) {
  gasnete_coll_generic_data_t *data = op->data;
  const gasnete_coll_exchangeM_args_t *args = GASNETE_COLL_GENERIC_ARGS(data, exchangeM);
  int result = 0;

  switch (data->state) {
    case 0:	/* Optional IN barrier */
      if (!gasnete_coll_generic_insync(data)) {
	break;
      }
      data->state = 1;

    case 1:	/* Initiate data movement */
      if (GASNETE_COLL_CHECK_OWNER(data)) {
	gasnet_coll_handle_t *h;
        int flags = op->flags;
	gasnet_team_handle_t team = op->team;
	void **srclist;
	void **p;
	void * const *q;
	void * tmp;
	size_t nbytes = args->nbytes;
        gasnet_image_t i, j;

	data->private_data = gasneti_malloc(gasnete_coll_total_images * sizeof(gasnet_coll_handle_t) +
				       gasnete_coll_total_images * gasnete_coll_total_images * sizeof(void *));
	h = (gasnet_coll_handle_t *)data->private_data;
	srclist = gasnete_coll_scale_ptr(data->private_data, sizeof(gasnet_coll_handle_t), gasnete_coll_total_images);

	/* XXX: A better design would not need N^2 temporary space */
	p = srclist;
	for (i = 0; i < gasnete_coll_total_images; ++i) {
	  q = args->srclist;
	  for (j = 0; j < gasnete_coll_total_images; ++j, ++p, ++q) {
	    *p = gasnete_coll_scale_ptr(*q, i, nbytes);
	  }
	}

        flags &= (GASNET_COLL_SINGLE | GASNET_COLL_LOCAL |
		  GASNET_COLL_DST_IN_SEGMENT | GASNET_COLL_SRC_IN_SEGMENT);
	flags |= (GASNET_COLL_IN_NOSYNC | GASNET_COLL_OUT_NOSYNC);

	p = srclist;
	q = args->dstlist;
        for (i = 0; i < gasnete_coll_total_images; ++i, ++h, ++q, p += gasnete_coll_total_images) {
          *h = gasnete_coll_gatherM_nb(team, i, *q, p, nbytes, flags GASNETE_THREAD_PASS);
        }
      } else {
	break;	/* Stalled until owner thread initiates gathers */
      }
      data->state = 2;

    case 2:	/* Sync data movement */
      if (!gasnete_coll_generic_coll_sync(data->private_data, gasnete_coll_total_images GASNETE_THREAD_PASS)) {
	break;
      }
      data->state = 3;

    case 3:	/* Optional OUT barrier */
      if (!gasnete_coll_generic_outsync(data)) {
	break;
      }

      gasneti_free(data->private_data);
      gasnete_coll_generic_free(data GASNETE_THREAD_PASS);
      result = (GASNETE_COLL_OP_COMPLETE | GASNETE_COLL_OP_INACTIVE);
  }

  return result;
}
extern gasnet_coll_handle_t
gasnete_coll_exchgM_Gath(gasnet_team_handle_t team,
			 void * const dstlist[], void * const srclist[],
			 size_t nbytes, int flags GASNETE_THREAD_FARG)
{
  int options = GASNETE_COLL_GENERIC_OPT_INSYNC_IF (!(flags & GASNET_COLL_IN_NOSYNC)) |
		GASNETE_COLL_GENERIC_OPT_OUTSYNC_IF(!(flags & GASNET_COLL_OUT_NOSYNC));

  /* XXX: until gatherM deals w/ out-of-segment data */
  if_pf (!(flags & GASNET_COLL_DST_IN_SEGMENT) || !(flags & GASNET_COLL_SRC_IN_SEGMENT)) {
    gasneti_fatalerror("Currently only in-segment data is supported for this operation");
    return GASNET_COLL_INVALID_HANDLE;
  }

  return gasnete_coll_generic_exchangeM_nb(team, dstlist, srclist, nbytes, flags,
					   &gasnete_coll_pf_exchgM_Gath, options GASNETE_THREAD_PASS);
}

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
    return gasnete_coll_op_generic_init(team, flags, data, poll_fn GASNETE_THREAD_PASS);
}

#ifndef gasnete_coll_exchangeM_nb
    extern gasnet_coll_handle_t
    gasnete_coll_exchangeM_nb(gasnet_team_handle_t team,
			      void * const dstlist[], void * const srclist[],
			      size_t nbytes, int flags GASNETE_THREAD_FARG) {
      /* "Discover" in-segment flags if needed/possible */
      flags = gasnete_coll_segment_checkM(flags, 0, 0, dstlist, nbytes*gasnete_nodes,
						 0, 0, srclist, nbytes*gasnete_nodes);

      /* XXX: need more implementations to choose from here */
      return gasnete_coll_exchgM_Gath(team, dstlist, srclist, nbytes, flags GASNETE_THREAD_PASS);
    }
#endif

/*---------------------------------------------------------------------------------*/

#ifndef GASNETE_COLL_P2P_HANDLERS
  #define GASNETE_COLL_P2P_HANDLERS
#endif
#define GASNETE_REFCOLL_HANDLERS()                                 \
  GASNETE_COLL_P2P_HANDLERS

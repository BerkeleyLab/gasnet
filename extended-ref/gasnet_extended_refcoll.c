/*   $Source: /Users/kamil/work/gasnet-cvs2/gasnet/extended-ref/gasnet_extended_refcoll.c,v $
 *     $Date: 2007/10/09 03:37:22 $
 * $Revision: 1.29.6.51 $
 * Description: Reference implemetation of GASNet Collectives team
 * Copyright 2004, Dan Bonachea <bonachea@cs.berkeley.edu>
 * Terms of use are as specified in license.txt
 */

#define GASNET_COLL_TREE_DEBUG 0
#include <gasnet_internal.h>
#include <gasnet_coll.h>
#include <gasnet_coll_internal.h>
#include <gasnet_extended_refcoll.h>
#include <gasnet_vis.h>

/*TEMPORARY (Need to eventually change it such that 
the files are compiled under their own .o files)*/
#include <gasnet_coll_trees.c>
/* gasnet_coll_autotune.c and gasnet_coll_scratch.c have 
   to be included after gasnet_coll_trees.c
*/
#include <gasnet_coll_autotune.c>
#include <gasnet_coll_scratch.c>

static size_t gasnete_coll_p2p_eager_min = 0;
static size_t gasnete_coll_p2p_eager_scale = 0;
/*set a std segment size of 1024 bytes*/
static size_t gasnete_coll_curr_seg_size;

/*---------------------------------------------------------------------------------*/
/* Set from environment variables by gasnete_coll_init(): */
int gasnete_coll_opt_enabled;


/*---------------------------------------------------------------------------------*/
/* Forward decls and macros */

#if GASNET_PAR
  #define GASNETE_COLL_FORWARD_FLAGS(flags) \
	(((flags) & ~(GASNET_COLL_IN_ALLSYNC|GASNET_COLL_IN_MYSYNC|\
		      GASNET_COLL_OUT_ALLSYNC|GASNET_COLL_OUT_MYSYNC|\
		      GASNET_COLL_AGGREGATE|GASNETE_COLL_THREAD_LOCAL)) \
	  | (GASNET_COLL_IN_NOSYNC|GASNET_COLL_OUT_NOSYNC|GASNETE_COLL_SUBORDINATE))
#else
  #define GASNETE_COLL_FORWARD_FLAGS(flags) \
	(((flags) & ~(GASNET_COLL_IN_ALLSYNC|GASNET_COLL_IN_MYSYNC|\
		      GASNET_COLL_OUT_ALLSYNC|GASNET_COLL_OUT_MYSYNC|\
		      GASNET_COLL_AGGREGATE)) \
	  | (GASNET_COLL_IN_NOSYNC|GASNET_COLL_OUT_NOSYNC|GASNETE_COLL_SUBORDINATE))
#endif

/* XXX: Until we have gasnete_poll, a thread will only poll the collectives when
   it does a try/wait on a collective handle.  So we must ensure a polling thread
   does not initiate any RDMA or subordinate collective operation unless we can be
   certain it will continue to poll until the RDMA or subordinate is done.
   The cases we must protect against include threads that get an INVALID_HANDLE
   for one or more ops and therefore may cease to poll before all collective ops
   are completed.
   The solution used here is to only allow a poller to initiate RDMA and/or
   subordinate operations if it has a handle for the current operation.  While
   we can't always know for sure, it is sufficient to only initiate when we
   can *prove* the current thread has a handle for the current op:
    + The thread that initiated to op is the "owner" and must have a handle.
      Assigning ownership in this manner ensures that there is always at least
      one thread that may make progress on each outstanding op.  In some cases
      this is the only thread which is certain to poll again, so tracking of
      the "owner" is required.
    + (flags & (OUT_MYSYNC|OUT_ALLSYNC)) implies that ALL threads must have a
      handle for this op (note that subordinate ops are initiated by a single
      thread, but this is OK since they always have NO/NO sync flags).  This
      is the only current improvement over the older CHECK_OWNER tests.
      The MY case won't work when we begin to signal threads individially as
      their data is delivered/consumed, but gasnete_poll() should be done
      before that.
   None of this is needed once gasnete_poll() will ensure that gasneti_AMPoll()
   will poll collectives as long as any remain unfinished.
*/
#if GASNETI_USE_TRUE_MUTEXES
  #define GASNETE_COLL_MAY_INIT_FOR(op)	((GASNETE_COLL_GENERIC_DATA(op)->owner == GASNETE_MYTHREAD) || \
					 ((op)->flags & (GASNET_COLL_OUT_MYSYNC | GASNET_COLL_OUT_ALLSYNC)))
  #define GASNETE_COLL_SET_OWNER(data)	(data)->owner = GASNETE_MYTHREAD
#else
  #define GASNETE_COLL_MAY_INIT_FOR(op)	1
  #if GASNET_DEBUG
    #define GASNETE_COLL_SET_OWNER(data)	(data)->owner = GASNETE_MYTHREAD
  #else
    #define GASNETE_COLL_SET_OWNER(data)	do {} while (0)
  #endif
#endif

/*---------------------------------------------------------------------------------*/
/* OLD dispatchers (implementations at end of file): */

extern gasnet_coll_handle_t
gasnete_coll_broadcast_nb_default_OLD(gasnet_team_handle_t team,
                                    void *dst,
                                    gasnet_image_t srcimage, void *src,
                                    size_t nbytes, int flags, uint32_t sequence
                                    GASNETE_THREAD_FARG);
extern gasnet_coll_handle_t
gasnete_coll_broadcastM_nb_default_OLD(gasnet_team_handle_t team,
                                     void * const dstlist[],
                                     gasnet_image_t srcimage, void *src,
                                     size_t nbytes, int flags, uint32_t sequence
                                     GASNETE_THREAD_FARG);
extern gasnet_coll_handle_t
gasnete_coll_scatter_nb_default_OLD(gasnet_team_handle_t team,
                                  void *dst,
                                  gasnet_image_t srcimage, void *src,
                                  size_t nbytes, int flags, uint32_t sequence
                                  GASNETE_THREAD_FARG);
extern gasnet_coll_handle_t
gasnete_coll_scatterM_nb_default_OLD(gasnet_team_handle_t team,
                                   void * const dstlist[],
                                   gasnet_image_t srcimage, void *src,
                                   size_t nbytes, int flags, uint32_t sequence
                                   GASNETE_THREAD_FARG);
extern gasnet_coll_handle_t
gasnete_coll_gather_nb_default_OLD(gasnet_team_handle_t team,
                                 gasnet_image_t dstimage, void *dst,
                                 void *src,
                                 size_t nbytes, int flags, uint32_t sequence
                                 GASNETE_THREAD_FARG);
extern gasnet_coll_handle_t
gasnete_coll_gatherM_nb_default_OLD(gasnet_team_handle_t team,
                                  gasnet_image_t dstimage, void *dst,
                                  void * const srclist[],
                                  size_t nbytes, int flags, uint32_t sequence
                                  GASNETE_THREAD_FARG);
extern gasnet_coll_handle_t
gasnete_coll_gather_all_nb_default_OLD(gasnet_team_handle_t team,
                                     void *dst, void *src,
                                     size_t nbytes, int flags, uint32_t sequence
                                     GASNETE_THREAD_FARG);
extern gasnet_coll_handle_t
gasnete_coll_gather_allM_nb_default_OLD(gasnet_team_handle_t team,
                            void * const dstlist[], void * const srclist[],
                            size_t nbytes, int flags, uint32_t sequence
                            GASNETE_THREAD_FARG);
extern gasnet_coll_handle_t
gasnete_coll_exchange_nb_default_OLD(gasnet_team_handle_t team,
                         void *dst, void *src,
                         size_t nbytes, int flags, uint32_t sequence
                         GASNETE_THREAD_FARG);
extern gasnet_coll_handle_t
gasnete_coll_exchangeM_nb_default_OLD(gasnet_team_handle_t team,
                          void * const dstlist[], void * const srclist[],
                          size_t nbytes, int flags, uint32_t sequence
                          GASNETE_THREAD_FARG);

/*---------------------------------------------------------------------------------*/
/* XXX: sequence and other stuff that will need to be per-team scoped: */

gasnet_coll_fn_entry_t *gasnete_coll_fn_tbl;
size_t gasnete_coll_fn_count;
uint32_t gasnete_coll_sequence = 42;	/* arbitrary non-zero starting value */
gasnet_image_t *gasnete_coll_all_images;
gasnet_image_t *gasnete_coll_all_offset;
uint8_t gasnete_coll_fixed_image_count; /* 1 if all the nodes have teh same number of images and 0 else*/ 
gasnet_image_t gasnete_coll_total_images;
gasnet_image_t gasnete_coll_max_images;
gasnet_image_t gasnete_coll_my_images;	/* count of local images */
gasnet_image_t gasnete_coll_my_offset;	/* count of images before my first image */
#if !GASNET_SEQ
  gasnet_node_t *gasnete_coll_image_to_node = NULL;
#endif
#if GASNET_PAR
  int gasnete_coll_multi_images;	/* count of local images > 1 */
  int gasnete_coll_multi_images_any;	/* count of any node's images > 1 */
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
			   int flags GASNETE_THREAD_FARG) {
  gasnete_coll_threaddata_t *td = GASNETE_COLL_MYTHREAD_NOALLOC;
  int i;

  if_pf (!gasnete_coll_init_done) {
    gasneti_fatalerror("Illegal call to GASNet collectives before gasnet_coll_init()\n");
  }

  if (dstimage == (gasnet_image_t)(-1)) {
    dstimage = td->my_image;
  }
  if (srcimage == (gasnet_image_t)(-1)) {
    srcimage = td->my_image;
  }

  /* Some flags should never come the user */
  gasneti_assert(!(flags & GASNETE_COLL_SUBORDINATE));
  #if GASNET_PAR
    gasneti_assert(!(flags & GASNETE_COLL_THREAD_LOCAL));
  #endif

  /* XXX: temporary limitation: */
#if 1
  gasneti_assert(team != NULL);
  gasneti_assert(team == GASNET_TEAM_ALL);
#endif

  #if GASNET_DEBUG
    #if GASNET_SEQ
      if ((flags & GASNET_COLL_LOCAL) && (dstisv || srcisv) && !gasneti_mynode) {
        static int once = 1;
        if_pf (once) {
          fprintf(stderr, "NOTICE: Use of GASNET_COLL_LOCAL is discouraged with multi-address collectives in a GASNET_SEQ build - use single-address collectives instead\n");
	  once = 0;
        }
      }
    #endif
    #if GASNET_PAR
      if ((flags & GASNET_COLL_SINGLE) && !(dstisv || srcisv) && gasnete_coll_multi_images_any) {
        gasneti_fatalerror("illegal use of GASNET_COLL_SINGLE with single-address collectives and multiple threads");
      }
    #endif

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
  gasneti_assert(dstlen > 0);
  if ((dstimage == td->my_image) && (flags & GASNET_COLL_DST_IN_SEGMENT)) {
    if (!dstisv) {
      gasneti_boundscheck(gasneti_mynode, dst, dstlen);
    } else {
      void * const *p = &GASNETE_COLL_MY_1ST_IMAGE(dst, flags);
      size_t limit = gasnete_coll_my_images;
      for (i = 0; i < limit; ++i, ++p) {
	gasneti_boundscheck(gasneti_mynode, *p, dstlen);
      }
    }
  }

  /* Bounds check any local portion of src/srclist which user claims is in-segment */
  gasneti_assert(srclen > 0);
  if ((srcimage == td->my_image) && (flags & GASNET_COLL_SRC_IN_SEGMENT)) {
    if (!srcisv) {
      gasneti_boundscheck(gasneti_mynode, src, srclen);
    } else {
      void * const *p = &GASNETE_COLL_MY_1ST_IMAGE(src, flags);
      size_t limit = gasnete_coll_my_images;
      for (i = 0; i < limit; ++i, ++p) {
	gasneti_boundscheck(gasneti_mynode, *p, srclen);
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
  #if GASNET_PAR
    #define GASNETE_COLL_HANDLE_DONE(h) ((h)->done)
    #define GASNETE_COLL_HANDLE_NEXT(h) ((h)->next)
  #else
    #define GASNETE_COLL_HANDLE_DONE(h)	(*h)
    #define GASNETE_COLL_HANDLE_NEXT(h) (*((gasnet_coll_handle_t *)(h)))
  #endif 

  extern gasnet_coll_handle_t gasnete_coll_handle_create(GASNETE_THREAD_FARG_ALONE) {
    gasnete_coll_threaddata_t *td = GASNETE_COLL_MYTHREAD;
    gasnet_coll_handle_t result;

    result = td->handle_freelist;
    if_pt (result) {
      td->handle_freelist = GASNETE_COLL_HANDLE_NEXT(result);
    } else {
      /* XXX: allocate in large chunks and scatter across cache lines */
      /* XXX: destroy freelist at exit */
      result = (gasnet_coll_handle_t)gasneti_malloc(sizeof(*result));
    }

    GASNETE_COLL_HANDLE_DONE(result) = 0;
    #if GASNET_PAR
      GASNETE_COLL_HANDLE_NEXT(result) = GASNET_COLL_INVALID_HANDLE;
    #endif
    return result;
  }

  extern void gasnete_coll_handle_signal(gasnet_coll_handle_t handle GASNETE_THREAD_FARG) {
    gasneti_assert(handle != GASNET_COLL_INVALID_HANDLE);
    gasneti_mutex_assertlocked(&gasnete_coll_active_lock);
    #if GASNET_PAR
      do {
	gasnet_coll_handle_t next = GASNETE_COLL_HANDLE_NEXT(handle);
        gasneti_sync_writes(); /* XXX? */
	GASNETE_COLL_HANDLE_DONE(handle) = 1;
	handle = next;
      } while (handle != GASNET_COLL_INVALID_HANDLE);
    #else
      GASNETE_COLL_HANDLE_DONE(handle) = 1;
    #endif
  }

  /* NOTE: caller is responsible for a gasneti_flush_reads() on success */
  extern int gasnete_coll_handle_done(gasnet_coll_handle_t handle GASNETE_THREAD_FARG) {
    int result = 0;
    gasneti_assert(handle != GASNET_COLL_INVALID_HANDLE);

    if_pf (GASNETE_COLL_HANDLE_DONE(handle)) {
      gasnete_coll_threaddata_t *td = GASNETE_COLL_MYTHREAD_NOALLOC;
      GASNETE_COLL_HANDLE_NEXT(handle) = td->handle_freelist;
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
/* Code to handle thread-specific list of handles to sync */

typedef struct {
    uintptr_t		addr;	/* least significant bit: 0 = handle, 1 = coll_handle */
    union {
	gasnet_handle_t		handle;
	gasnet_coll_handle_t	coll_handle;
    }		u;
} gasnete_coll_local_handle_t;

GASNETI_INLINE(gasnete_coll_local_handles)
gasnete_coll_local_handle_t *
gasnete_coll_local_handles(gasnete_coll_threaddata_t *td, int grow) {
    gasnete_coll_local_handle_t *result = (gasnete_coll_local_handle_t *)td->handles.array;

    if (grow) {
	int allocated = td->handles.allocated;
	if_pf (allocated == td->handles.used) {
	    allocated += 8;
	    td->handles.allocated = allocated;
	    result = gasneti_realloc(result, allocated*sizeof(gasnete_coll_local_handle_t));
	    td->handles.array = result;
	}
    }

    return result;
}

void gasnete_coll_save_handle(gasnet_handle_t *handle_p GASNETE_THREAD_FARG) {
    if (*handle_p != GASNET_INVALID_HANDLE) {
	gasnete_coll_threaddata_t *td = GASNETE_COLL_MYTHREAD;
	gasnete_coll_local_handle_t *p = gasnete_coll_local_handles(td, 1);
	p[td->handles.used].addr = (uintptr_t)handle_p;
	p[td->handles.used].u.handle = *handle_p;
	td->handles.used += 1;
    }
}

void gasnete_coll_save_coll_handle(gasnet_coll_handle_t *handle_p GASNETE_THREAD_FARG) {
    if (*handle_p != GASNET_COLL_INVALID_HANDLE) {
	gasnete_coll_threaddata_t *td = GASNETE_COLL_MYTHREAD;
	gasnete_coll_local_handle_t *p = gasnete_coll_local_handles(td, 1);
	p[td->handles.used].addr = 1 | (uintptr_t)handle_p;
	p[td->handles.used].u.coll_handle = *handle_p;
	td->handles.used += 1;
    }
}

void gasnete_coll_sync_saved_handles(GASNETE_THREAD_FARG_ALONE) {
    gasnete_coll_threaddata_t *td = GASNETE_COLL_MYTHREAD;
    int used = td->handles.used;

    if (used) {
	gasnete_coll_local_handle_t *curr = gasnete_coll_local_handles(td, 0);
	gasnete_coll_local_handle_t *last = curr + used - 1;
	int i;

	for (i = 0; i < used; ++i) {
	    uintptr_t addr = curr->addr;
	    int synced = 0;
	    if (addr & 1) {
		/* Coll handle - take care not to re-enter coll_poll()!! */
		addr &= ~1;
		synced = gasnete_coll_handle_done(curr->u.coll_handle GASNETE_THREAD_PASS);
		if (synced) {
		    gasneti_sync_writes();
		    *((gasnet_coll_handle_t *)addr) = GASNET_COLL_INVALID_HANDLE;
		}
	    } else {
		synced = (gasnete_try_syncnb(curr->u.handle) == GASNET_OK);
		if (synced) {
		    gasneti_sync_writes();
		    *((gasnet_handle_t *)addr) = GASNET_INVALID_HANDLE;
		}
	    }
	    if (synced) {
		*curr = *(last--);
		--td->handles.used;
	    } else {
		++curr;
	    }
	}
    }
}

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
	gasneti_assert(team == GASNET_TEAM_ALL);
	return (gasnet_node_t)rank;
    }

    int gasnete_coll_team_node2rank(gasnete_coll_team_t team, gasnet_node_t node) {
	gasneti_assert(team == GASNET_TEAM_ALL);
	return (int)node;
    }

    uint32_t gasnete_coll_team_id(gasnete_coll_team_t team) {
	gasneti_assert(team == GASNET_TEAM_ALL);
	return 0;
    }
#endif

/*---------------------------------------------------------------------------------*/
/* Synchronization for threads in PAR builds */

/* Current state:
 * In a SEQ or PARSYNC build this code compiles away.
 *
 * In a PAR build we have the following properties:
 * + First arrival takes lock and holds it until operations is *queued*.
 *   This is the shortest we can get away with if the later arrivals are
 *   to reliably locate the queued op.
 * + Late arrivals acquire the same lock to lookup the operation.  If
 *   found they call either an op-specific or default arrival function.
 *   The default decrements 'threads.remaining' (unless IN_NOSYNC) and
 *   creates a thread-specific handle (unless OUT_NOSYNC), adding it
 *   to the handle list (linked off the op).
 * + Just before signalling the handle(s) for a completed op, the lock is
 *   obtained to atomically remove the op from the list used by "late"
 *   arrivals to find in-flight ops.  The ensures that late arrivals are
 *   either signalled or see GASNET_COLL_INVALID_HANDLE (when not on list).
 * + Currenly IN_ALLSYNC and IN_MYSYNC involve pthread-level "barrier" before
 *   operation can "enter the network".
 * XXX Some per-op hook might be able to improve IN_MYSYNC slighty by allowing
 *     PARTS of the data to begin moving before all threads have arrived, but
 *     can never eliminate the need for all arrivals before op is internally
 *     completed.
 * + OUT_*SYNC handles:
 *   - OUT_NOSYNC will return INVALID_HANDLE to all but first arrival.
 *   - OUT_{MY,ALL}SYNC returns unique handles to any thread arriving before
 *     actual completion, but returns INVALID_HANDLE to any arriving later
 *     (which is only possible with IN_NOSYNC at the moment).
 * XXX Some per-op hook could relax things for OUT_MYSYNC.  An example would
 *     be a rooted operation - on the root node the non-root threads could
 *     be synced as soon as the local data movement is done.
 *
 * XXX: For "M_Eager" versions of Bcast and Scatter, the OUT_MYSYNC could be
 *	implemented to provide per-thread completion indications.  However,
 *	this requires short-cutting the current mechanism for signalling
 *	handles (by never returning GASNETE_COLL_OP_COMPLETE from the poll
 *	function) and tracking (in op->data?) of the handles assigned to
 *	each thread.
 * XXX: For "gathM_Eager" the IN_MYSYNC case can be implemented w/o a
 *	pthread-level "input" barrier by having each thread perform its
 *	own put.
 */

/* XXX - a work in progress */
#if GASNET_PAR
    gasneti_mutex_t gasnete_coll_threads_mutex = GASNETI_MUTEX_INITIALIZER;
    uint32_t gasnete_coll_threads_sequence = 0;	/* independent of collective sequence space */ /* XXX: TEAMS */
    gasnete_coll_op_t *gasnete_coll_threads_head = NULL;
    gasnete_coll_op_t **gasnete_coll_threads_tail_p = &(gasnete_coll_threads_head);

    void gasnete_coll_threads_lock(int flags GASNETE_THREAD_FARG) {
      gasnete_coll_threaddata_t *td = GASNETE_COLL_MYTHREAD_NOALLOC;
      if_pt (!gasnete_coll_multi_images || (flags & GASNETE_COLL_SUBORDINATE)) {
	/* I am only thread (and thus trivally the first) and therefore don't need the lock */
	gasneti_assert(td->threads.hold_lock == 0);
      } else {
	gasneti_mutex_lock(&gasnete_coll_threads_mutex);
	td->threads.hold_lock = 1;
      }
    }

    void gasnete_coll_threads_unlock(GASNETE_THREAD_FARG_ALONE) {
      gasnete_coll_threaddata_t *td = GASNETE_COLL_MYTHREAD_NOALLOC;
      if_pf (td->threads.hold_lock) {
        gasneti_mutex_assertlocked(&gasnete_coll_threads_mutex);
        gasneti_mutex_unlock(&gasnete_coll_threads_mutex);
	td->threads.hold_lock = 0;
      } else {
        gasneti_mutex_assertunlocked(&gasnete_coll_threads_mutex);
      }
    }

    /* Each thread calls this upon arrival.  First arrival gets non-zero */
    int gasnete_coll_threads_first(GASNETE_THREAD_FARG_ALONE) {
      gasnete_coll_threaddata_t *td = GASNETE_COLL_MYTHREAD;
      if_pt (td->threads.hold_lock == 0) {
	/* single-threaded */
	return 1;
      } else {
	/* multi-threaded */
        const uint32_t sequence = td->threads.sequence;

        ++td->threads.sequence;
        if (sequence == gasnete_coll_threads_sequence) {
	  ++gasnete_coll_threads_sequence;
	  return 1;
	} else {
	  return 0;
	}
      }
    }

    gasnete_coll_op_t *gasnete_coll_threads_get_op(GASNETE_THREAD_FARG_ALONE) {
      gasnete_coll_threaddata_t *td = GASNETE_COLL_MYTHREAD_NOALLOC;
      const uint32_t sequence = td->threads.sequence - 1;
      gasnete_coll_op_t *op;

      gasneti_mutex_assertlocked(&gasnete_coll_active_lock);
      op = gasnete_coll_active_first();
      while (op && ((op->flags & GASNETE_COLL_SUBORDINATE) || (op->threads.sequence != sequence))) {
	op = gasnete_coll_active_next(op);
      }
      return op;
    }

    gasnet_coll_handle_t gasnete_coll_threads_add_handle(gasnete_coll_op_t *op GASNETE_THREAD_FARG) {
      gasnet_coll_handle_t handle = GASNET_COLL_INVALID_HANDLE;

      gasneti_mutex_assertlocked(&gasnete_coll_active_lock);
      if (op->handle != GASNET_COLL_INVALID_HANDLE) {
        handle = gasnete_coll_handle_create(GASNETE_THREAD_PASS_ALONE);
        GASNETE_COLL_HANDLE_NEXT(handle) = op->handle;
        op->handle = handle;
      }

      return handle;
    }

    /* Default thread-arrival function */
    gasnet_coll_handle_t gasnete_coll_threads_get_handle(GASNETE_THREAD_FARG_ALONE) {
      gasnete_coll_op_t *op;
      gasnet_coll_handle_t result = GASNET_COLL_INVALID_HANDLE;

      gasneti_mutex_lock(&gasnete_coll_active_lock);
      op = gasnete_coll_threads_get_op(GASNETE_THREAD_PASS_ALONE);
      if_pt (op != NULL) {
        /* Deal with IN_*SYNC */
        if (op->flags & (GASNET_COLL_IN_ALLSYNC | GASNET_COLL_IN_MYSYNC)) {
	  /* signal thread barrier */
          gasneti_assert(op->data != NULL);
          gasneti_atomic_decrement(&GASNETE_COLL_GENERIC_DATA(op)->threads.remaining, 0);
        }

        /* Deal with OUT_*SYNC */
        if (op->flags & (GASNET_COLL_OUT_ALLSYNC | GASNET_COLL_OUT_MYSYNC)) {
	  result = gasnete_coll_threads_add_handle(op GASNETE_THREAD_PASS);
        }
      }
      gasneti_mutex_unlock(&gasnete_coll_active_lock);

      return result;
    }

    /* Thread-arrival function when collecting thread-local addrs */
    gasnet_coll_handle_t
    gasnete_coll_threads_get_handle_and_data(gasnete_coll_generic_data_t **data_p GASNETE_THREAD_FARG) {
      gasnete_coll_op_t *op;
      gasnet_coll_handle_t result;

      gasneti_mutex_lock(&gasnete_coll_active_lock);
      op = gasnete_coll_threads_get_op(GASNETE_THREAD_PASS_ALONE);
      gasneti_assert(op != NULL);
      if (op->flags & (GASNET_COLL_OUT_ALLSYNC | GASNET_COLL_OUT_MYSYNC)) {
        result = gasnete_coll_threads_add_handle(op GASNETE_THREAD_PASS);
      } else {
        result = GASNET_COLL_INVALID_HANDLE;
      }
      gasneti_mutex_unlock(&gasnete_coll_active_lock);

      *data_p = op->data;

      return result;
    }

    int gasnete_coll_threads_addrs_ready(void * volatile *list GASNETE_THREAD_FARG) {
      /* The idea is to scan a list of thread-local addresses to see that they have all
       * been set to non-NULL values.  This function tries to lessen the amount of ping-
       * ponging of cache lines by eliminating the potential "hot spot" at the beginning
       * of the list.  This only ends up making a difference if the list spans cachelines.
       *
       * Note that non-application threads are OK, and will have my_local_image == 0.
       */
      const gasnete_coll_threaddata_t *td = GASNETE_COLL_MYTHREAD;
      int i;

      for (i = td->my_local_image; i < gasnete_coll_my_images; ++i) { /* >= self */
	if (list[i] == NULL) {
	  return 0;
	}
      }
      for (i = 0; i < td->my_local_image; ++i) { /* < self */
	if (list[i] == NULL) {
	  return 0;
	}
      }
      return 1;
    }

    int gasnete_coll_threads_ready1(gasnete_coll_op_t *op, void **list GASNETE_THREAD_FARG) {
      if (op->flags & GASNETE_COLL_THREAD_LOCAL) {
	return gasnete_coll_threads_addrs_ready(list GASNETE_THREAD_PASS);
      } else {
	return gasnete_coll_generic_all_threads(op->data);
      }
    }

    int gasnete_coll_threads_ready2(gasnete_coll_op_t *op, void **list1, void **list2 GASNETE_THREAD_FARG) {
      if (op->flags & GASNETE_COLL_THREAD_LOCAL) {
	return gasnete_coll_threads_addrs_ready(list1 GASNETE_THREAD_PASS) &&
	       gasnete_coll_threads_addrs_ready(list2 GASNETE_THREAD_PASS);
      } else {
	return gasnete_coll_generic_all_threads(op->data);
      }
    }
#else
    #define gasnete_coll_threads_ready1(op,list1)	1
    #define gasnete_coll_threads_ready2(op,list1,list2)	1
#endif

/*---------------------------------------------------------------------------------*/
/* The list of active collective ops (coll ops) */

/* There exists a "active list".
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
	    op->handle = GASNET_COLL_INVALID_HANDLE;
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
	    head->handle = GASNET_COLL_INVALID_HANDLE;
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
  op->scratchpos = NULL;

  /* The aggregation and 'data' fields are setup elsewhere */

  return op;
}

void
gasnete_coll_op_destroy(gasnete_coll_op_t *op GASNETE_THREAD_FARG) {
  gasnete_coll_threaddata_t *td = GASNETE_COLL_MYTHREAD_NOALLOC;
  gasneti_free(op->scratchpos);
  *((gasnete_coll_op_t **)op) =  td->op_freelist;
  td->op_freelist = op;
}

void gasnete_coll_poll(GASNETE_THREAD_FARG_ALONE) {
  static gasneti_mutex_t poll_lock = GASNETI_MUTEX_INITIALIZER;

  gasneti_AMPoll();

  /* First try to make progress on any handles this thread has initiated */
  gasnete_coll_sync_saved_handles(GASNETE_THREAD_PASS_ALONE);

  /* XXX: We'd also like to have multiple pollers walk the list polling
   * distinct entries.
   */
  if (gasneti_mutex_trylock(&poll_lock) == 0) {
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
        /*if the op was using any scratch space indicate that the scratch is free to overwrite*/
		/*update my head and tail of the scratch space*/
		
		gasnete_coll_op_complete(op, poll_result GASNETE_THREAD_PASS);
		
      }
      gasneti_mutex_unlock(&gasnete_coll_active_lock);

      /* Next... */
      op = next;
    }

    gasneti_mutex_unlock(&poll_lock);
  }
}
static gasnet_seginfo_t *gasnete_coll_auxseg_save = NULL;

/* AuxSeg setup for distributed scratch space*/
gasneti_auxseg_request_t gasnete_coll_auxseg_alloc(gasnet_seginfo_t *auxseg_info) {
    gasneti_auxseg_request_t retval;
   
    int i, selftest=0;
    retval.minsz = gasneti_getenv_int_withdefault("GASNET_COLL_MIN_SCRATCH_SIZE",
                                                  GASNETE_COLL_MIN_SCRATCH_SIZE_DEFAULT,0);

    retval.optimalsz = gasneti_getenv_int_withdefault("GASNET_COLL_OPT_SCRATCH_SIZE",
                                                      GASNETE_COLL_OPT_SCRATCH_SIZE_DEFAULT,0);
	if (auxseg_info == NULL){
		return retval; /* initial query */
	}	
     else { /* auxseg granted */
      gasneti_assert(!gasnete_coll_auxseg_save);
      gasnete_coll_auxseg_save = gasneti_malloc(gasneti_nodes*sizeof(gasnet_seginfo_t));
      memcpy(gasnete_coll_auxseg_save, auxseg_info, gasneti_nodes*sizeof(gasnet_seginfo_t));
    }

    return retval;
  }



void (*gasnete_coll_old_barrier_notify)(int id, int flags); 

void gasnete_coll_barrier_notify_callback(int id, int flags) {
	(*gasnete_coll_old_barrier_notify)(id,flags);
	
	/*** Begin Custom Code that will hook into all the barriers **/
//	GASNET_TEAM_ALL->scratch_status->perform_reset = 1;
	/*	GASNET_TEAM_ALL->scratch_status->first_collective = 1; */
	/**** End Custom Code **/
}

extern void gasnete_coll_init(const gasnet_image_t images[], gasnet_image_t my_image,
			      gasnet_coll_fn_entry_t fn_tbl[], size_t fn_count,
			      int init_flags GASNETE_THREAD_FARG) {
  gasnete_coll_threaddata_t *td = GASNETE_COLL_MYTHREAD;
  static gasneti_cond_t init_cond = GASNETI_COND_INITIALIZER;
  static gasneti_mutex_t init_lock = GASNETI_MUTEX_INITIALIZER;
  static gasneti_mutex_t barrier_fn_lock = GASNETI_MUTEX_INITIALIZER;
  static gasneti_mutex_t team_all_setup_lock = GASNETI_MUTEX_INITIALIZER;
  static int team_all_set = 0;
  static int barrier_fn_set = 0;
  static gasnet_image_t remain = 0;
  size_t image_size = gasneti_nodes * sizeof(gasnet_image_t);
  int first;
  int i;

  GASNETI_CHECKATTACH();

  /* Sanity checks - performed only for debug builds */
  
  #if GASNET_DEBUG
//    fprintf(stderr, "%d> setting seg size to %d\n", gasneti_mynode, (int)gasnete_coll_curr_seg_size);
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
      remain = images[gasneti_mynode];
      first = 1;
    } else {
      first = 0;
    }
    gasneti_mutex_unlock(&init_lock);
  } else {
    td->my_image = gasneti_mynode;
    first = 1; /* only thread, so always first */
  }

  if (first) {
    gasnete_coll_opt_enabled = gasneti_getenv_yesno_withdefault("GASNET_COLL_OPT", 1);
    gasnete_coll_p2p_eager_min = gasneti_getenv_int_withdefault("GASNET_COLL_P2P_EAGER_MIN",
								GASNETE_COLL_P2P_EAGER_MIN_DEFAULT, 0);
    gasnete_coll_p2p_eager_scale = gasneti_getenv_int_withdefault("GASNET_COLL_P2P_EAGER_SCALE",
								  GASNETE_COLL_P2P_EAGER_SCALE_DEFAULT, 0);
    /*** XXX: An autotuner will eliminate the need for manually setting this ***/
    gasnete_coll_curr_seg_size = gasneti_getenv_int_withdefault("GASNET_COLL_SEG_SIZE",
								GASNETE_COLL_SEG_SIZE_DEFAULT,0);

    gasnete_coll_active_init();
    gasnete_coll_p2p_init();

    gasnete_coll_all_images = gasneti_malloc(image_size);
    gasnete_coll_all_offset = gasneti_malloc(image_size);
    if (images != NULL) {
      memcpy(gasnete_coll_all_images, images, image_size);
    } else  {
      for (i = 0; i < gasneti_nodes; ++i) {
        gasnete_coll_all_images[i] = 1;
      }
    }
    gasnete_coll_total_images = 0;
    gasnete_coll_max_images = 0;
    gasnete_coll_fixed_image_count=1;
    for (i = 0; i < gasneti_nodes; ++i) {
      gasnete_coll_all_offset[i] = gasnete_coll_total_images;
      gasnete_coll_total_images += gasnete_coll_all_images[i];
      gasnete_coll_max_images = MAX(gasnete_coll_max_images,gasnete_coll_all_images[i]);
      if(gasnete_coll_all_images[i] != gasnete_coll_all_images[0]) {
        gasnete_coll_fixed_image_count = 0;
      }
    }
    gasnete_coll_my_images = gasnete_coll_all_images[gasneti_mynode];
    gasnete_coll_my_offset = gasnete_coll_all_offset[gasneti_mynode];
    #if GASNET_PAR
      if (!images) {
        gasnete_coll_multi_images = 0;
        gasnete_coll_multi_images_any = 0;
      } else if (gasnete_coll_my_images != 1) {
        gasnete_coll_multi_images = 1;
        gasnete_coll_multi_images_any = 1;
      } else {
        gasnete_coll_multi_images = 0;
        gasnete_coll_multi_images_any = 0;
        for (i = 0; i < gasneti_nodes; ++i) {
          if (gasnete_coll_all_images[i] > 1) {
	    gasnete_coll_multi_images_any = 1;
	    break;
          }
        }
      }
    #endif

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

    gasnete_coll_fn_count = fn_count;
    if (fn_count != 0) {
      size_t tbl_size = sizeof(gasnet_coll_fn_entry_t) * fn_count;
      gasnete_coll_fn_tbl = gasneti_malloc(tbl_size);
      memcpy(gasnete_coll_fn_tbl, fn_tbl, tbl_size);
      #if GASNET_DEBUG
        for (i = 0; i < fn_count; ++i) {
	  if (fn_tbl[i].flags & ~(GASNET_COLL_AMSAFE | GASNET_COLL_NONCOMM)) {
	    gasneti_fatalerror("gasnet_coll_init: function table entry %i has unknown flag(s)", i);
          }
        }
      #endif
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
  #if GASNET_DEBUG
    /* Ensure agreement across threads */
    gasneti_assert(fn_count == gasnete_coll_fn_count);
    for (i = 0; i < fn_count; ++i) {
      gasneti_assert(fn_tbl[i].flags == gasnete_coll_fn_tbl[i].flags);
      gasneti_assert(fn_tbl[i].fnptr == gasnete_coll_fn_tbl[i].fnptr);
    }
  #endif
  if (images) {
    td->my_local_image = my_image - gasnete_coll_my_offset;
    gasneti_assert(td->my_local_image < gasnete_coll_my_images);
  } else {
    td->my_local_image = 0;
  }
  

  gasneti_mutex_lock(&team_all_setup_lock);
  if(!team_all_set) {
   
    team_all_set = 1;
    
    gasnete_coll_current_tree_kind = GASNETE_COLL_BINOMIAL_TREE;
    gasnete_coll_current_fanout = GASNETE_COLL_DEFAULT_FANOUT;
    /*setup information for the global team*/
    GASNET_TEAM_ALL = (struct gasnete_coll_team_t_*) gasneti_malloc(sizeof(struct gasnete_coll_team_t_));
    GASNET_TEAM_ALL->team_id = 0;
    GASNET_TEAM_ALL->global_team = 1;
    GASNET_TEAM_ALL->tree_geom_cache_head = NULL;
    GASNET_TEAM_ALL->tree_geom_cache_tail = NULL;
    GASNET_TEAM_ALL->dissem_cache_head = NULL;
    GASNET_TEAM_ALL->dissem_cache_tail = NULL;
    GASNET_TEAM_ALL->myrank = gasneti_mynode;
    GASNET_TEAM_ALL->total_ranks = gasneti_nodes;
    GASNET_TEAM_ALL->scratch_segs = gasnete_coll_auxseg_save;
    gasnete_coll_alloc_new_scratch_status(GASNET_TEAM_ALL);
    /* barrier to make sure that no one sends us scratch clear messages before our scratch space is initialized*/
    if(!gasnete_coll_fixed_image_count && gasneti_mynode ==0) {
      fprintf(stderr, "WARNING: Current collective implementation requires constant number of threads on each node\n");
      fprintf(stderr, "WARNING: for optimized collectives.\n");
    }
    gasnet_barrier_notify(0, GASNET_BARRIERFLAG_ANONYMOUS);
    gasnet_barrier_wait(0, GASNET_BARRIERFLAG_ANONYMOUS);
  }
  gasneti_mutex_unlock(&team_all_setup_lock);
  
  /*change the barrier code to hook in collective call back*/
#if 0
  gasneti_mutex_lock(&barrier_fn_lock);
  if(!barrier_fn_set) {
    /* We need exactly one thread to set this otherwise it'll be an infinite recursion*/
    gasnete_coll_old_barrier_notify = gasnete_barrier_notify;
    gasnete_barrier_notify = gasnete_coll_barrier_notify_callback;
    barrier_fn_set = 1;
  }
  gasneti_mutex_unlock(&barrier_fn_lock);
#endif

  gasnete_coll_init_done = 1;
   

}

/*---------------------------------------------------------------------------------*/
/* Synchronization primitives */

#ifndef GASNETE_COLL_CONSENSUS_OVERRIDE
    /* gasnete_coll_issued_id counts barrier sequence numbers as they are allocated
     * to collective operations. */
    static uint32_t gasnete_coll_issued_id = 0;

    /* gasnete_coll_consensus_id holds the current barrier state and sequence.
     * The upper 31 bits of gasnete_coll_issued_id holds the lower 31 bits of
     * the barrier sequence number of the current barrier.  This imposes a
     * limit of around 1 billion simultaneous outstanding collective ops before
     * counter overflow could introduce ambiguity.  Otherwise, careful use of
     * unsigned arithmetic eliminates problems due to wrap.
     * The least significant bit of gasnete_coll_consensus_id is 0 if the next
     * operation is to be a notify, or a 1 if the next is a try.
     * Any caller may issue a try (when the phase indicates a try) and must
     * advance gasnete_coll_issued_id by 1 if the try returns success.
     * Only the matching caller may issue a notify and must unconditionally
     * advance gasnete_coll_issued_id by 1.
     * In a debug build the gasnete_coll_issued_id is also used as the barrier
     * name to help detect bugs, but anonymous barriers are used in non-debug
     * builds for speed.
     */
    static uint32_t gasnete_coll_consensus_id = 0;

    extern gasnete_coll_consensus_t gasnete_coll_consensus_create(void) {
      return gasnete_coll_issued_id++;
    }

    GASNETI_INLINE(gasnete_coll_consensus_do_try)
    int gasnete_coll_consensus_do_try(void) {
#if GASNET_DEBUG
      int rc = gasnet_barrier_try(gasnete_coll_consensus_id, 0);
      if_pt (rc == GASNET_OK) {
	/* A barrier is complete, advance */
	++gasnete_coll_consensus_id;
	return 1;
      } else if (rc == GASNET_ERR_BARRIER_MISMATCH) {
	gasneti_fatalerror("Named barrier mismatch detected in collectives");
      } else {
	gasneti_assert(rc == GASNET_ERR_NOT_READY);
      }
      return 0;
#else
      int rc = gasnet_barrier_try(0, GASNET_BARRIERFLAG_ANONYMOUS);
      if_pt (rc == GASNET_OK) {
	/* A barrier is complete, advance */
	++gasnete_coll_consensus_id;
	return 1;
      }
      return 0;
#endif
    }

    GASNETI_INLINE(gasnete_coll_consensus_do_notify)
    void gasnete_coll_consensus_do_notify(void) {
	  ++gasnete_coll_consensus_id;
#if GASNET_DEBUG
	  gasnet_barrier_notify(gasnete_coll_consensus_id, 0);
#else
	  gasnet_barrier_notify(0, GASNET_BARRIERFLAG_ANONYMOUS);
#endif
    }


    extern int gasnete_coll_consensus_try(gasnete_coll_consensus_t id) {
      uint32_t tmp = id << 1;	/* low bit is used for barrier phase (notify vs wait) */
      /* We can only notify when our own turn comes up.
       * Thus, the most progress we could make in one call
       * would be to sucessfully 'try' for our predecessor,
       * 'notify' our our barrier, and then 'try' our own.
       */
      switch (tmp - gasnete_coll_consensus_id) {
        case 1:
	  /* Try for our predecessor, hoping we can then notify */
	  if (!gasnete_coll_consensus_do_try()) {
	    gasneti_assert((tmp - gasnete_coll_consensus_id) == 1);
	    /* Sucessor is not yet done */
	    break;
	  }
	  gasneti_assert(tmp == gasnete_coll_consensus_id);
	  /* ready to advance, so fall through... */
        case 0:
	  /* Our own turn has come - notify and try */
	  gasnete_coll_consensus_do_notify();
	  gasneti_assert((gasnete_coll_consensus_id - tmp) == 1);
	  gasnete_coll_consensus_do_try();
	  gasneti_assert(((gasnete_coll_consensus_id - tmp) == 1) ||
			 ((gasnete_coll_consensus_id - tmp) == 2));
	  break;

        default:
	  /* not our turn, but we can 'try' if the phase is right */
	  if (gasnete_coll_consensus_id & 1) {
	    gasnete_coll_consensus_do_try();
	  }
      }

      /* Note that we need to be careful of wrapping, thus the (int32_t)(a-b) construct
       * must be used in place of simply (a-b).
       */
      return ((int32_t)(gasnete_coll_consensus_id - tmp) > 1) ? GASNET_OK
							      : GASNET_ERR_NOT_READY;
    }
    /* Allocate a new barrier and wait for all barriers to finish before this id*/
    extern int gasnete_coll_consensus_wait(GASNETE_THREAD_FARG_ALONE) {
#if 1
      gasnete_coll_consensus_t mybarr;
  
      mybarr = gasnete_coll_consensus_create();
  
      while(gasnete_coll_consensus_try(mybarr)==GASNET_ERR_NOT_READY) {
        /*Try to make progress on other collectives*/
        gasnete_coll_poll(GASNETE_THREAD_PASS_ALONE);
      }
#else
      gasnet_barrier_notify(0, GASNET_BARRIERFLAG_ANONYMOUS);
      gasnet_barrier_wait(0, GASNET_BARRIERFLAG_ANONYMOUS); 
#endif
      return GASNET_OK;
    }
#endif

#ifndef GASNETE_COLL_P2P_OVERRIDE
    #ifndef GASNETE_COLL_P2P_TABLE_SIZE
      #define GASNETE_COLL_P2P_TABLE_SIZE 16
    #endif
	
    #if 1
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
	size_t buffersz = MAX(gasnete_coll_p2p_eager_min,
			      gasnete_coll_total_images * gasnete_coll_p2p_eager_scale);
	size_t statesz = GASNETI_ALIGNUP(2*gasnete_coll_total_images * sizeof(uint32_t), 8);

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
	gasneti_weakatomic_set(&p2p->counter, 0, 0);
        
        /*allocate an empty interval for the free list */
        p2p->seg_intervals = NULL;
          
	p2p->team_id = team_id;
	p2p->sequence = sequence;
	gasnet_hsl_init(&p2p->lock);

	gasnete_coll_p2p_freelist = p2p->p2p_next;

	/* XXX: searches out-number insertions.  So, we should do the work here to
	 * keep the list sorted, and take advantage in the search, above. */
	p2p->p2p_prev = head;
	p2p->p2p_next = head->p2p_next;
	head->p2p_next->p2p_prev = p2p;
	head->p2p_next = p2p;
	#ifdef GASNETE_P2P_EXTRA_INIT
	  GASNETE_P2P_EXTRA_INIT(p2p)
	#endif
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
      #ifdef GASNETE_P2P_EXTRA_FREE
        GASNETE_P2P_EXTRA_FREE(p2p)
      #endif

      p2p->p2p_next = gasnete_coll_p2p_freelist;	/* XXX: per-team */
      gasnete_coll_p2p_freelist = p2p;

      gasnet_hsl_unlock(&gasnete_coll_p2p_table_lock);
    }

    /*Management of the Intervals for Segments*/
    /* We use 32 bit ints to represent the segment ID*/
    /* If we need more than 2^32 segments (which should be rare)
       The collective will need to get broken up into multiple collectives
    */
    static gasnet_hsl_t gasnete_coll_p2p_seg_free_list_lock = GASNET_HSL_INITIALIZER;
    static gasnete_coll_seg_interval_t *gasnet_coll_p2p_seg_interval_free_list = NULL;

    gasnete_coll_seg_interval_t *gasnet_coll_p2p_alloc_seg_interval() {
      gasnete_coll_seg_interval_t *curr_interval;
           
      gasnet_hsl_lock(&gasnete_coll_p2p_seg_free_list_lock);
      if(gasnet_coll_p2p_seg_interval_free_list == NULL) {
        /*if the free list is empty allocate a new one*/
        curr_interval = gasneti_malloc(sizeof(gasnete_coll_seg_interval_t));
      } else {
        /* if there are extra on the free list grab it off the head of the free list*/
        curr_interval = gasnet_coll_p2p_seg_interval_free_list;
        gasnet_coll_p2p_seg_interval_free_list = gasnet_coll_p2p_seg_interval_free_list->next;
      }
      gasnet_hsl_unlock(&gasnete_coll_p2p_seg_free_list_lock);
      return curr_interval;
    }
    
    void gasnete_coll_p2p_free_seg_interval(gasnete_coll_seg_interval_t* interval) {
      gasnet_hsl_lock(&gasnete_coll_p2p_seg_free_list_lock);
      interval->next = gasnet_coll_p2p_seg_interval_free_list;
      gasnet_coll_p2p_seg_interval_free_list = interval;
      gasnet_hsl_unlock(&gasnete_coll_p2p_seg_free_list_lock);
    }

    extern void gasnete_coll_p2p_add_seg_interval(gasnete_coll_p2p_t *p2p, uint32_t seg_id) {
      
      gasnete_coll_seg_interval_t *curr_interval,*new_interval,*prev;
      gasneti_assert(p2p !=NULL);
      gasnet_hsl_lock(&p2p->lock);
      if(p2p->seg_intervals==NULL) {
        /*head of the current interval list is empty*/
        curr_interval = gasnet_coll_p2p_alloc_seg_interval();
        curr_interval->start = seg_id;
        curr_interval->end = seg_id;
        curr_interval->next = NULL;
        /*make this new interval the head of the interval list*/
        p2p->seg_intervals = curr_interval;
      } else {
        curr_interval = p2p->seg_intervals;
        prev = NULL;
        /*march through the intervals looking where to insert this value*/
        /*we are guaranteed to have at least onoe since we made the check above*/
        while(curr_interval!=NULL) {
          if(curr_interval->start - 1 == seg_id) {
            curr_interval->start = seg_id;
            break;
          } else if(curr_interval->end + 1 == seg_id) {
            /*attach it to the end of the current interval and */
            curr_interval->end = seg_id;
            break;
          } else if(seg_id < curr_interval->start) {
            /*add the new element in to the middle of the list*/
            new_interval = gasnet_coll_p2p_alloc_seg_interval();
            new_interval->start = seg_id;
            new_interval->end = seg_id;
            if(prev == NULL) {
              /* add to the head*/
              p2p->seg_intervals = new_interval;
            } else {
              /*add to the middle*/
              prev->next = new_interval;
            }
            new_interval->next = curr_interval;
            break;
          } else if(seg_id > curr_interval->end && curr_interval->next == NULL){
            new_interval = gasnet_coll_p2p_alloc_seg_interval();
            new_interval->start = seg_id;
            new_interval->end = seg_id;
            new_interval->next = NULL;
            curr_interval->next = new_interval;
            break;
          }  else {
            prev = curr_interval;
            curr_interval = curr_interval->next;
          }
        }
      }
      gasnet_hsl_unlock(&p2p->lock);
    }
    /*return the next segment interval in the list*/
    /*results are undefined if the seg_intervals list null*/
    extern uint32_t gasnete_coll_p2p_next_seg_interval(gasnete_coll_p2p_t *p2p) {
      gasnete_coll_seg_interval_t *curr_interval;
      size_t ret;
      gasneti_assert(p2p!=NULL);
      gasneti_assert(p2p->seg_intervals !=NULL);
      /*march through the intervals to find the next interval*/
      gasnet_hsl_lock(&p2p->lock);
      if(p2p->seg_intervals->start != p2p->seg_intervals->end) {
        /* the interval contains information for more than one segment*/
        /*read a segment and return it*/
        ret = p2p->seg_intervals->start;
        p2p->seg_intervals->start +=1; 
      } else {
        /*the interval contains exactly one segment*/
        /*read the value in it and return it*/
        ret = p2p->seg_intervals->start;
        curr_interval = p2p->seg_intervals;
        p2p->seg_intervals = p2p->seg_intervals->next;
        gasnete_coll_p2p_free_seg_interval(curr_interval);
      }
      gasnet_hsl_unlock(&p2p->lock);
      return ret;
    }
    /* Delivers a long payload and updates 1 or more states
       count: number of states to update
       offset: index of first state to update
       state: value to assign to states [offset, offset+count)
     */
    extern void gasnete_coll_p2p_long_reqh(gasnet_token_t token, void *buf, size_t nbytes,
					   gasnet_handlerarg_t team_id,
					   gasnet_handlerarg_t sequence,
					   gasnet_handlerarg_t count,
					   gasnet_handlerarg_t offset,
					   gasnet_handlerarg_t state) {
      gasnete_coll_p2p_t *p2p = gasnete_coll_p2p_get(team_id, sequence);
      int i;

      if (nbytes) {
	gasneti_sync_writes();
      }

      for (i = 0; i < count; ++i, ++offset) {
        p2p->state[offset] = state;
      }
    }

    /* Delivers a medium payload to the eager buffer space and updates 1 or more states
       count: number of states to update
       offset: index of first state to update
       state: value to assign to states [offset, offset+count)
       size: eager element size; payload is copied to (p2p->data + offset*size)
     */
    extern void gasnete_coll_p2p_med_reqh(gasnet_token_t token, void *buf, size_t nbytes,
					  gasnet_handlerarg_t team_id,
					  gasnet_handlerarg_t sequence,
					  gasnet_handlerarg_t count,
					  gasnet_handlerarg_t offset,
					  gasnet_handlerarg_t state,
					  gasnet_handlerarg_t size) {
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

    /* Delivers a medium payload to the eager buffer space and updates 1 state
       seqandteaem: a packed 32 bit int with both the team and the sequence number
       size: eager element size; payload is copied to (p2p->data)
     */
    extern void gasnete_coll_p2p_med_tree_reqh(gasnet_token_t token, void *buf, size_t nbytes,
					       gasnet_handlerarg_t seqandteam) {
      uint32_t team_id;
      uint32_t sequence;
      int i;
      gasnete_coll_p2p_t *p2p;
      
      team_id = seqandteam >> 28;
      sequence = seqandteam & 0x0fffffff;
      
      p2p = gasnete_coll_p2p_get(team_id, sequence);
      
      GASNETE_FAST_UNALIGNED_MEMCPY(p2p->data, buf, nbytes);
      gasneti_sync_writes();
      
      p2p->state[0] = 1;
      
    }

    /* No payload to deliver, just updates 1 or more states
       count: number of states to update
       offset: index of first state to update
       state: value to assign to states [offset, offset+count)
     */
    extern void gasnete_coll_p2p_short_reqh(gasnet_token_t token,
						  gasnet_handlerarg_t team_id,
						  gasnet_handlerarg_t sequence,
						  gasnet_handlerarg_t count,
						  gasnet_handlerarg_t offset,
						  gasnet_handlerarg_t state) {
      gasnete_coll_p2p_t *p2p = gasnete_coll_p2p_get(team_id, sequence);
      int i;

      for (i = 0; i < count; ++i, ++offset) {
        p2p->state[offset] = state;
      }
    }

    /* Increment atomic counter */
    extern void gasnete_coll_p2p_advance_reqh(gasnet_token_t token,
					      gasnet_handlerarg_t seqandteam) {
      uint32_t team_id = seqandteam >> 28;
      uint32_t sequence = seqandteam & 0x0fffffff;
      gasnete_coll_p2p_t *p2p = gasnete_coll_p2p_get(team_id, sequence);
      gasneti_weakatomic_increment(&p2p->counter, 0);
    }

    /* Send the data and increment atomic counter */
    extern void gasnete_coll_p2p_put_and_advance_reqh(gasnet_token_t token, void *buf, size_t nbytes,
                                          gasnet_handlerarg_t seqandteam) {
      uint32_t team_id; 
      uint32_t sequence; 
      gasnete_coll_p2p_t *p2p;
      
      team_id =  seqandteam >> 28;
      sequence = seqandteam & 0x0fffffff;

      if (nbytes) {
	gasneti_sync_writes();
      }
      
      p2p = gasnete_coll_p2p_get(team_id, sequence);
      
      gasneti_weakatomic_increment(&p2p->counter, 0);
    }

    extern void gasnete_coll_p2p_seg_put_reqh(gasnet_token_t token, void *buf, size_t nbytes,
                                              gasnet_handlerarg_t seqandteam, gasnet_handlerarg_t seg_id) {
      
      uint32_t team_id; 
      uint32_t sequence; 
      gasnete_coll_p2p_t *p2p;
      
      
      team_id =  seqandteam >> 28;
      sequence = seqandteam & 0x0fffffff;
      
      if (nbytes) {
	gasneti_sync_writes();
      }
      
      p2p = gasnete_coll_p2p_get(team_id, sequence);

      /*add this new segment to the ops list of segments*/
      /*This function takes care of any locking that is needed*/
      gasnete_coll_p2p_add_seg_interval(p2p, seg_id);
      
      /*increment P2P counter*/
      gasneti_weakatomic_increment(&p2p->counter, 0);
      
    }
    /* Memcopy payload and then decrement atomic counter if requested */
    GASNETI_INLINE(gasnete_coll_p2p_memcpy_reqh_inner)
    void gasnete_coll_p2p_memcpy_reqh_inner(gasnet_token_t token, void *buf, size_t nbytes,
						   void *dest,
						   gasnet_handlerarg_t team_id,
						   gasnet_handlerarg_t sequence,
						   gasnet_handlerarg_t decrement) {
      gasnete_coll_p2p_t *p2p = gasnete_coll_p2p_get(team_id, sequence);

      GASNETE_FAST_UNALIGNED_MEMCPY(dest, buf, nbytes);
      if (decrement) {
        gasneti_sync_writes();
	gasnet_hsl_lock(&p2p->lock);
        --(p2p->state[0]);
	gasnet_hsl_unlock(&p2p->lock);
      }
    }
    MEDIUM_HANDLER(gasnete_coll_p2p_memcpy_reqh,4,5,
                  (token,addr,nbytes, UNPACK(a0),      a1, a2, a3),
                  (token,addr,nbytes, UNPACK2(a0, a1), a2, a3, a4));

    /* Put up to gasnet_AMMaxLongRequest() bytes, signalling the recipient */
    /* Returns as soon as local buffer is reusable */
    void gasnete_coll_p2p_signalling_put(gasnete_coll_op_t *op, gasnet_node_t dstnode, void *dst,
					 void *src, size_t nbytes, uint32_t offset, uint32_t state) {
      uint32_t team_id = gasnete_coll_team_id(op->team);

      gasneti_assert(nbytes <= gasnet_AMMaxLongRequest());

      GASNETI_SAFE(
	LONG_REQ(5,5,(dstnode, gasneti_handleridx(gasnete_coll_p2p_long_reqh),
		      src, nbytes, dst, team_id, op->sequence, 1, offset, state)));
    }

    /* Put up to gasnet_AMMaxLongRequest() bytes, signalling the recipient */
    /* Returns immediately even if the local buffer is not yet reusable */
    void gasnete_coll_p2p_signalling_putAsync(gasnete_coll_op_t *op, gasnet_node_t dstnode, void *dst,
					      void *src, size_t nbytes, uint32_t offset, uint32_t state) {
      uint32_t team_id = gasnete_coll_team_id(op->team);

      gasneti_assert(nbytes <= gasnet_AMMaxLongRequest());

      GASNETI_SAFE(
	LONGASYNC_REQ(5,5,(dstnode, gasneti_handleridx(gasnete_coll_p2p_long_reqh),
			   src, nbytes, dst, team_id, op->sequence, 1, offset, state)));
    }
    /* Put up to gasnet_AMMaxLongRequest() bytes, signalling the recipient */
    /* Returns as soon as local buffer is reusable */
    void gasnete_coll_p2p_counting_put(gasnete_coll_op_t *op, gasnet_node_t dstnode, void *dst,
                                       void *src, size_t nbytes) {
      
      uint32_t seq_num = op->sequence;
      uint32_t team_id = gasnete_coll_team_id(op->team);
      uint32_t seqandteam = 0;
      seqandteam = team_id << 28;
      seqandteam += seq_num & 0x0fffffff;
  
      gasneti_assert(nbytes <= gasnet_AMMaxLongRequest());
  
      GASNETI_SAFE(
        LONG_REQ(1,1,(dstnode, gasneti_handleridx(gasnete_coll_p2p_put_and_advance_reqh),
                      src, nbytes, dst, seqandteam)));
    }
    /* Put up to gasnet_AMMaxLongRequest() bytes, signalling the recipient */
    /* Returns immediately even if the local buffer is not yet reusable */
    void gasnete_coll_p2p_counting_putAsync(gasnete_coll_op_t *op, gasnet_node_t dstnode, void *dst,
                                            void *src, size_t nbytes) {
  
      uint32_t seq_num = op->sequence;
      uint32_t team_id = gasnete_coll_team_id(op->team);
      uint32_t seqandteam = 0;
      seqandteam = team_id << 28;
      seqandteam += seq_num & 0x0fffffff;
  
      gasneti_assert(nbytes <= gasnet_AMMaxLongRequest());
  
      GASNETI_SAFE(
        LONGASYNC_REQ(1,1,(dstnode, gasneti_handleridx(gasnete_coll_p2p_put_and_advance_reqh),
                      src, nbytes, dst, seqandteam)));
    }
    
    /*
      Signalling Segmented Put 
      Takes a Segment ID as an argument and sends the message such that it will be put in the right location
      and update the list of active intervals indicating which chunk of the message has arrived
    */
    void gasnete_coll_p2p_sig_seg_put(gasnete_coll_op_t *op, gasnet_node_t dstnode, void *dst,
                                             void *src, size_t nbytes, size_t seg_id) {
      uint32_t seq_num = op->sequence;
      uint32_t team_id = gasnete_coll_team_id(op->team);
      uint32_t seqandteam = 0;
      seqandteam = team_id << 28;
      seqandteam += seq_num & 0x0fffffff;

      gasneti_assert(nbytes <= gasnet_AMMaxLongRequest());
      
      GASNETI_SAFE(
        LONG_REQ(2,2,(dstnode, gasneti_handleridx(gasnete_coll_p2p_seg_put_reqh),
                           src, nbytes, dst, seqandteam, seg_id)));
    }

    void gasnete_coll_p2p_sig_seg_putAsync(gasnete_coll_op_t *op, gasnet_node_t dstnode, void *dst,
                                      void *src, size_t nbytes, size_t seg_id) {
      uint32_t seq_num = op->sequence;
      uint32_t team_id = gasnete_coll_team_id(op->team);
      uint32_t seqandteam = 0;
      seqandteam = team_id << 28;
      seqandteam += seq_num & 0x0fffffff;
  
      gasneti_assert(nbytes <= gasnet_AMMaxLongRequest());
  
      GASNETI_SAFE(
        LONGASYNC_REQ(2,2,(dstnode, gasneti_handleridx(gasnete_coll_p2p_seg_put_reqh),
                           src, nbytes, dst, seqandteam, seg_id)));
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
          GASNETI_SAFE(
	    MEDIUM_REQ(6,6,(dstnode, gasneti_handleridx(gasnete_coll_p2p_med_reqh),
			    src, nbytes, team_id, op->sequence, limit, offset, state, size)));
	  offset += limit;
	  src = (void *)((uintptr_t)src + nbytes);
	  count -= limit;
	} while (count > limit);
      }

      GASNETI_SAFE(
	MEDIUM_REQ(6,6,(dstnode, gasneti_handleridx(gasnete_coll_p2p_med_reqh),
			src, count * size, team_id, op->sequence, count, offset, state, size)));
    }
    
/* a simplification for eager putM so that we send less bits on the wire*/ 
/* we hardcode the assumption that we want to send to state 0 and set a value of 1*/
/* for cases in which we are just sending down the tree (such as a broadcast) this is sufficient*/
/* we are also going to compress the teamid and the sequence number into one 32 bit int*/
    void gasnete_coll_p2p_eager_put_tree(gasnete_coll_op_t *op, gasnet_node_t dstnode, 
					 void *src, size_t size) {
      uint32_t seq_num = op->sequence;
      uint32_t team_id = gasnete_coll_team_id(op->team);
      uint32_t seqandteam = 0;

      /* lets shift up the team id by more than 16 bits to give mroe room for the sequence number*/
      /* shift it up by 28 bits leaving 4 bits (or 16 teams for now) for the team id and the rest is sequence number*/
      seqandteam = team_id << 28;
      seqandteam += seq_num & 0x0fffffff;

      gasneti_assert(size <= gasnet_AMMaxMedium());
      GASNETI_SAFE(MEDIUM_REQ(1,1,(dstnode, gasneti_handleridx(gasnete_coll_p2p_med_tree_reqh),
				  src, size, seqandteam)));
      
    }

    /* Update one or more states w/o delivering any data */
    void gasnete_coll_p2p_change_states(gasnete_coll_op_t *op, gasnet_node_t dstnode,
				        uint32_t count, uint32_t offset, uint32_t state) {
      uint32_t team_id = gasnete_coll_team_id(op->team);

      GASNETI_SAFE(
        SHORT_REQ(5,5,(dstnode, gasneti_handleridx(gasnete_coll_p2p_short_reqh),
                       team_id, op->sequence, count, offset, state)));
    }

    /* Advance state[0] */
    void gasnete_coll_p2p_advance(gasnete_coll_op_t *op, gasnet_node_t dstnode) {
      uint32_t team_id = gasnete_coll_team_id(op->team);
      uint32_t seqandteam = (team_id << 28) | (op->sequence & 0x0fffffff);

      GASNETI_SAFE(
        SHORT_REQ(1,1,(dstnode, gasneti_handleridx(gasnete_coll_p2p_advance_reqh),
                       seqandteam)));
    }

    /* Memcpy up to gasnet_AMMaxMedium() bytes, signalling the recipient */
    /* Returns as soon as local buffer is reusable */
    void gasnete_coll_p2p_memcpy(gasnete_coll_op_t *op, gasnet_node_t dstnode, void *dst,
				 void *src, size_t nbytes) {
      uint32_t team_id = gasnete_coll_team_id(op->team);

      gasneti_assert(nbytes <= gasnet_AMMaxMedium());

      GASNETI_SAFE(
	MEDIUM_REQ(4,5,(dstnode, gasneti_handleridx(gasnete_coll_p2p_memcpy_reqh),
			src, nbytes, PACK(dst), team_id, op->sequence, 1)));
    }

    struct gasnete_coll_p2p_send_struct { void *addr; size_t sent; };

    /* Indicate ready for a gasnete_coll_p2p_memcpy, placing request in slots "offset+" */
    /* XXX: we send addr+"0", when only the addr is needed (want "custom" AM, not eager_put) . */
    void gasnete_coll_p2p_send_rtrM(gasnete_coll_op_t *op, gasnete_coll_p2p_t *p2p,
				    uint32_t offset, void * const *dstlist,
				    gasnet_node_t node, size_t nbytes, uint32_t count) {
      struct gasnete_coll_p2p_send_struct *tmp =
		gasneti_malloc(count * sizeof(struct gasnete_coll_p2p_send_struct));
      int i;
      for (i = 0; i < count; ++i) {
        tmp[i].addr = dstlist[i];
        tmp[i].sent = 0;
      }
      gasnet_hsl_lock(&p2p->lock);
      /* Record the number of Mediums we know we'll receive. */
      p2p->state[0] += count * ((nbytes + gasnet_AMMaxMedium() - 1) / gasnet_AMMaxMedium());
      gasnet_hsl_unlock(&p2p->lock);
      gasnete_coll_p2p_eager_putM(op, node, tmp, count, sizeof(*tmp), offset, 1);
    }

    /* Indicate ready for a gasnete_coll_p2p_memcpy, placing request in slot "offset" */
    /* XXX: we send addr+"0", when only the addr is needed. */
    void gasnete_coll_p2p_send_rtr(gasnete_coll_op_t *op, gasnete_coll_p2p_t *p2p,
				   uint32_t offset, void *dst,
				   gasnet_node_t node, size_t nbytes) {
      gasnete_coll_p2p_send_rtrM(op, p2p, offset, &dst, node, nbytes, 1);
    }

    /* Check completion of a gasnete_coll_p2p_memcpy (on rcvr) */
    int gasnete_coll_p2p_send_done(gasnete_coll_p2p_t *p2p) {
      int result;
      gasnet_hsl_lock(&p2p->lock);
      result = !p2p->state[0];
      gasnet_hsl_unlock(&p2p->lock);
      return result;
    }

    /* Respond to a gasnete_coll_p2p_send_rtr */
    int gasnete_coll_p2p_send_data(gasnete_coll_op_t *op, gasnete_coll_p2p_t *p2p,
				   gasnet_node_t node, uint32_t offset,
				   const void *src, size_t nbytes) {
      struct gasnete_coll_p2p_send_struct *status = (struct gasnete_coll_p2p_send_struct *)p2p->data;
      if (p2p->state[offset] == 1) {
	size_t sent = status[offset].sent;
	size_t count = nbytes - sent;
	if_pt (count) {
	  void *tmp = (void *)((uintptr_t)src + sent);
	  void *addr = status[offset].addr;
	  count = MIN(count, gasnet_AMMaxMedium());
	  gasnete_coll_p2p_memcpy(op, node, addr, tmp, count);
	  status[offset].addr = (void *)((uintptr_t)addr + count);
	  status[offset].sent += count;
	} else {
	  p2p->state[offset] = 2;
	}
      }
      return (p2p->state[offset] == 2);
    }
#endif

/*---------------------------------------------------------------------------------*/
/* functions for generic ops */

#if PLATFORM_COMPILER_COMPAQ_C /* bug525 workaround - prevent inliner resource exhaustion with -inline all */
  #pragma noinline (gasnete_coll_generic_alloc,gasnete_coll_generic_free,gasnete_coll_op_generic_init)
#endif

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
	
	if(data->tree_info!=NULL) {
		gasnete_coll_tree_free(data->tree_info GASNETE_THREAD_PASS);
		data->tree_info=NULL; 
	}

	if (data->options & GASNETE_COLL_GENERIC_OPT_P2P) {
      gasnete_coll_p2p_free(data->p2p);
    }

    #if GASNET_PAR
    if (data->threads.data) {
      gasneti_free(data->threads.data);
    }
    #else 
    if(data->addrs) {
      gasneti_free(data->addrs);
    }
    #endif

    *((gasnete_coll_generic_data_t **)data) =  td->generic_data_freelist;
    td->generic_data_freelist = data;
}

/* Generic routine to create an op and enter it in the active list, etc..
 * Caller provides 'data' and 'poll_fn' specific to the operation.
 * Handle is allocated automatically if flags don't indicate aggregation.
 *
 * 'sequence' can have two meanings:
 *  w/ GASNETE_COLL_SUBORDINATE it is the pre-allocated sequence number to assign
 *  w/o GASNETE_COLL_SUBORDINATE is is the number of slots to pre-allocate
 *
 * Just returns the handle.
 */
 



extern gasnet_coll_handle_t
gasnete_coll_op_generic_init_with_scratch(gasnete_coll_team_t team, int flags,
			     gasnete_coll_generic_data_t *data, gasnete_coll_poll_fn poll_fn,
			     uint32_t sequence, gasnete_coll_scratch_req_t *scratch_req GASNETE_THREAD_FARG) {
      gasnet_coll_handle_t handle = GASNET_COLL_INVALID_HANDLE;
      gasnete_coll_op_t *op;
	  int i;
      gasneti_assert(team == GASNET_TEAM_ALL);
      gasneti_assert(data != NULL);

      GASNETE_COLL_SET_OWNER(data);

      if_pf (flags & GASNETE_COLL_SUBORDINATE) {
	/* Subordinates can't AGGREGATE (but maybe they should?) */
        gasneti_assert(!(flags & GASNET_COLL_AGGREGATE));
      } else {
	/* XXX: need team scope for sequence numbers */
	uint32_t tmp = gasnete_coll_sequence;
	gasnete_coll_sequence += (1 + sequence);
        sequence = tmp;
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
      op->flags = flags;

      #if GASNET_PAR
      if (gasnete_coll_multi_images && !(flags & GASNETE_COLL_SUBORDINATE)) {
        op->threads.sequence = gasnete_coll_threads_sequence - 1;
        gasneti_atomic_set(&data->threads.remaining, (flags & GASNET_COLL_IN_NOSYNC) ? 0 : (gasnete_coll_my_images - 1), 0);
      } else {
        gasneti_atomic_set(&data->threads.remaining, 0, 0);
      }
      #endif
		/*set up scratch space here as needed modify coll op to take an extra struct argument
		  if it is NULL it indicates that no scratch is required (default case)
		  if it isn't NULL then it means that we want to call it with scratch
		  MAKE SURE TO SETUP SCRATCH BEFORE THE OP IS SET TO BE ACTIVE
		  */

      op->scratch_req = scratch_req;
      
      /* Allocate the barriers AFTER SCRATCH SPACE*/
      /* This will allow the scratch space to use its own consensus barriers*/
      if_pf (flags & GASNETE_COLL_SUBORDINATE) {
        /* Subordinates can't allocate sequence numbers or barriers, due to non-collective calling */
	gasneti_assert(!(data->options & (GASNETE_COLL_GENERIC_OPT_INSYNC |
					  GASNETE_COLL_GENERIC_OPT_OUTSYNC)));
      } else {
	/* Conditionally allocate barriers */
	/* XXX: this is where we could do some aggregation of syncs */
	if (data->options & GASNETE_COLL_GENERIC_OPT_INSYNC) {
	  data->in_barrier = gasnete_coll_consensus_create();
	}
	if (data->options & GASNETE_COLL_GENERIC_OPT_OUTSYNC) {
	  data->out_barrier = gasnete_coll_consensus_create();
	}
      }
      
      op->waiting_scratch_op = 0;
      op->active_scratch_op = 0;
      op->waiting_for_reconfig_clear=0;
#if GASNET_DEBUG
      op->scratch_op_freed = 0;
#endif
      /* Submit the op via aggregation filter */
      handle = gasnete_coll_op_submit(op, handle GASNETE_THREAD_PASS);

      return handle;
}
extern gasnet_coll_handle_t gasnete_coll_op_generic_init(gasnete_coll_team_t team, int flags,
			     gasnete_coll_generic_data_t *data, gasnete_coll_poll_fn poll_fn,
			     uint32_t sequence GASNETE_THREAD_FARG) {
	return gasnete_coll_op_generic_init_with_scratch(team, flags, data, poll_fn, sequence, NULL GASNETE_THREAD_PASS);
}
/* NOTE: caller is responsible for a gasneti_sync_reads() if they read any transferred data.  */
extern int gasnete_coll_generic_coll_sync(gasnet_coll_handle_t *p, size_t count GASNETE_THREAD_FARG) {
  int result = 1;
  int i;

  for (i = 0; i < count; ++i) {
    if (p[i] != GASNET_COLL_INVALID_HANDLE) {
      result = 0;
      break;
    }
  }

  return result;
}

static gasnet_hsl_t gasnete_coll_tree_lock = GASNET_HSL_INITIALIZER;
	
/* XXX: should per-team */
extern gasnete_coll_tree_data_t *gasnete_coll_tree_init(gasnete_coll_tree_type_t tree_type, gasnet_node_t root, gasnete_coll_team_t team GASNETE_THREAD_FARG){
  gasnete_coll_threaddata_t *td = GASNETE_COLL_MYTHREAD;
  gasnete_coll_tree_data_t *data = NULL;
 /* lock aquisition and free in tree init*/
  if_pf (td->tree_data_freelist == NULL) {
    data = gasneti_malloc(sizeof(gasnete_coll_tree_data_t));
  } else {
    data = td->tree_data_freelist;
    td->tree_data_freelist = *(gasnete_coll_tree_data_t **)data;
  }
  
 /* unlock aquisition and free in tree init*/
  data->pipe_seg_size = gasnete_coll_pipe_seg_size ? gasnete_coll_pipe_seg_size : 1024;
  data->sent_bytes = 0;
  data->geom = gasnete_coll_local_tree_geom_fetch(tree_type, root, team);

  return data;
}

extern void gasnete_coll_tree_free(gasnete_coll_tree_data_t *tree GASNETE_THREAD_FARG) {
  
  gasnete_coll_threaddata_t *td = GASNETE_COLL_MYTHREAD;
/*  gasnet_hsl_lock(&gasnete_coll_tree_lock);*/
	gasnete_coll_local_tree_geom_release(tree->geom);
  *(gasnete_coll_tree_data_t **)tree = td->tree_data_freelist;
  td->tree_data_freelist = tree;
 /* gasnet_hsl_unlock(&gasnete_coll_tree_lock);*/
}


/** all the routines **/
#include "gasnet_coll_putget.c"
#include "gasnet_coll_rvous.c"
#include "gasnet_coll_eager.c"

/*---------------------------------------------------------------------------------*/
/* gasnete_coll_broadcast_nb() */

extern gasnet_coll_handle_t
gasnete_coll_generic_broadcast_nb(gasnet_team_handle_t team,
				  void *dst,
				  gasnet_image_t srcimage, void *src,
				  size_t nbytes, int flags,
				  gasnete_coll_poll_fn poll_fn, int options,
				  gasnete_coll_tree_data_t *tree_info, uint32_t sequence
                                  GASNETE_THREAD_FARG) {
  gasnet_coll_handle_t result;
  gasnete_coll_scratch_req_t *scratch_req=NULL;
   int i;
  
  /*fill out a scratch request "form" if you need scratch space with this operation*/
  if(options & (GASNETE_COLL_USE_SCRATCH)) {
    uint64_t *out_sizes;
    scratch_req = (gasnete_coll_scratch_req_t*) gasneti_calloc(1,sizeof(gasnete_coll_scratch_req_t));
    
    /*fill out the tree information*/
    scratch_req->tree_type = tree_info->geom->tree_type;
    scratch_req->root = tree_info->geom->root;
    scratch_req->tree_dir = GASNETE_COLL_DOWN_TREE;
    scratch_req->team = team;
    scratch_req->op_type = GASNETE_COLL_TREE_OP;
    /*fill out the peer information*/
	
    if(team->myrank == tree_info->geom->root) {
      scratch_req->incoming_size = nbytes;
      scratch_req->num_in_peers = 0;
      scratch_req->in_peers = NULL;

    } else {
      scratch_req->incoming_size = nbytes;
      scratch_req->num_in_peers = 1;
      scratch_req->in_peers = &(GASNETE_COLL_TREE_GEOM_PARENT(tree_info->geom));

    }
    out_sizes = (uint64_t*) gasneti_malloc(sizeof(uint64_t)*GASNETE_COLL_TREE_GEOM_CHILD_COUNT(tree_info->geom));
    scratch_req->num_out_peers = GASNETE_COLL_TREE_GEOM_CHILD_COUNT(tree_info->geom);
    scratch_req->out_peers = GASNETE_COLL_TREE_GEOM_CHILDREN(tree_info->geom);
    for(i=0; i< GASNETE_COLL_TREE_GEOM_CHILD_COUNT(tree_info->geom); i++) {
      out_sizes[i] = nbytes;
    }
    scratch_req->out_sizes = out_sizes;
  }
  gasnete_coll_threads_lock(flags GASNETE_THREAD_PASS);
  if_pt (gasnete_coll_threads_first(GASNETE_THREAD_PASS_ALONE)) {
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
    data->tree_info = tree_info;
    result = gasnete_coll_op_generic_init_with_scratch(team, flags, data, poll_fn, sequence, scratch_req GASNETE_THREAD_PASS);
  } else {
    result = gasnete_coll_threads_get_handle(GASNETE_THREAD_PASS_ALONE);
  }
  gasnete_coll_threads_unlock(GASNETE_THREAD_PASS_ALONE);

  return result;
}



extern gasnet_coll_handle_t
gasnete_coll_broadcast_nb_default(gasnet_team_handle_t team,
                                      void *dst,
                                      gasnet_image_t srcimage, void *src,
                                      size_t nbytes, int flags, uint32_t sequence
                                      GASNETE_THREAD_FARG)
{
  const size_t eager_limit = gasnete_coll_p2p_eager_min;

  if_pf (!gasnete_coll_opt_enabled) {
    return gasnete_coll_broadcast_nb_default_OLD(team, dst, srcimage, src, nbytes,
                                                 flags, sequence GASNETE_THREAD_PASS);
  }

  #if GASNET_PAR
  /* Thread-local addr(s) - forward to bcastM_nb() */
  if (flags & GASNET_COLL_LOCAL) {
    return gasnete_coll_broadcastM_nb(team, &dst, srcimage, src, nbytes,
                                      flags | GASNETE_COLL_THREAD_LOCAL, sequence
                                      GASNETE_THREAD_PASS);
  }
  #endif

  /* "Discover" in-segment flags if needed/possible */
  flags = gasnete_coll_segment_check(flags, 0, 0, dst, nbytes, 1, srcimage, src, nbytes);

  /* Choose algorithm based on arguments */
  if ((nbytes <= eager_limit) &&
      (flags & (GASNET_COLL_IN_MYSYNC | GASNET_COLL_OUT_MYSYNC | GASNET_COLL_LOCAL))) {
    /* Small enough for Eager, which will eliminate any barriers for *_MYSYNC and
     * the need for passing addresses for _LOCAL
     * Eager is totally AM-based and thus safe regardless of *_IN_SEGMENT
     */
#if 0
    if(gasneti_mynode ==0) fprintf(stderr, "%d> using tree eager for %d bytes\n", gasneti_mynode, (int)nbytes); 
#endif
    return gasnete_coll_bcast_TreeEager(team, dst, srcimage, src, nbytes, flags, gasnete_coll_get_current_tree_kind(), sequence GASNETE_THREAD_PASS);
  } else if (flags & GASNET_COLL_DST_IN_SEGMENT) {
    /* run the segmented broadcast code 
       function internally checks synch flags and SINGLE/LOCAL flags
    */
    return gasnete_coll_bcast_TreePutSeg(team, dst, srcimage, src, nbytes, flags,  gasnete_coll_get_current_tree_kind(), sequence GASNETE_THREAD_PASS);
#if 0
    /* if the transfer size is greater than the current segment size, use a pipelined algorithm*/
      if(nbytes > gasnete_coll_curr_seg_size) {
        return gasnete_coll_bcast_TreePutSeg(team, dst, srcimage, src, nbytes, flags,  gasnete_coll_get_current_tree_kind(), sequence GASNETE_THREAD_PASS);
      }
      /* We use a Put-based algorithm w/ full barriers for *_{MY,ALL}SYNC */
      if((flags & GASNET_COLL_SINGLE) && ((flags & (GASNET_COLL_IN_NOSYNC)) || (flags & (GASNET_COLL_IN_ALLSYNC)))) {
#if 0
	if(gasneti_mynode ==0) fprintf(stderr, "%d> in no or in all using tree put for %d bytes\n", gasneti_mynode, (int)nbytes); 
#endif 
	return gasnete_coll_bcast_TreePut(team, dst, srcimage, src, nbytes, flags, gasnete_coll_get_current_tree_kind(), sequence GASNETE_THREAD_PASS);
      }
      else {
#if 0
	if(gasneti_mynode ==0) fprintf(stderr, "%d> in my using tree put scratch for %d bytes\n", gasneti_mynode, (int)nbytes);
#endif 
	return gasnete_coll_bcast_TreePutScratch(team, dst, srcimage, src, nbytes, flags, gasnete_coll_get_current_tree_kind(), sequence GASNETE_THREAD_PASS);
      }
#endif   
  } else if (flags & GASNET_COLL_SRC_IN_SEGMENT) {
    if (flags & (GASNET_COLL_IN_MYSYNC | GASNET_COLL_OUT_MYSYNC | GASNET_COLL_LOCAL)) {
      /* We can use Rendezvous+Get to eliminate any barriers for *_MYSYNC.
       * The Rendezvous is needed for _LOCAL.
       */
      return gasnete_coll_bcast_RVGet(team, dst, srcimage, src, nbytes, flags, sequence GASNETE_THREAD_PASS);
    } else {
      return gasnete_coll_bcast_Get(team, dst, srcimage, src, nbytes, flags, sequence GASNETE_THREAD_PASS);
    }
  }  else {
    /* If we reach here then neither src nor dst is in-segment */
    return gasnete_coll_bcast_RVous(team, dst, srcimage, src, nbytes, flags, sequence GASNETE_THREAD_PASS);
  }
}



/*---------------------------------------------------------------------------------*/
/* gasnete_coll_broadcastM_nb() */

extern gasnet_coll_handle_t
gasnete_coll_generic_broadcastM_nb(gasnet_team_handle_t team,
				   void * const dstlist[],
				   gasnet_image_t srcimage, void *src,
				   size_t nbytes, int flags,
				   gasnete_coll_poll_fn poll_fn, int options,
				   gasnete_coll_tree_data_t *tree_info, uint32_t sequence
                                   GASNETE_THREAD_FARG) {
  gasnet_coll_handle_t result;
  gasnete_coll_scratch_req_t* scratch_req=NULL;
  int first_thread = 0;
    
  gasnete_coll_threads_lock(flags GASNETE_THREAD_PASS);
  /* figure out where to stick all scratch space management code */
  if_pt(gasnete_coll_threads_first(GASNETE_THREAD_PASS_ALONE)) {
    first_thread = 1;
    /* allocate scratch space*/
    if(options & (GASNETE_COLL_USE_SCRATCH)) {
      int i;
      uint64_t *out_sizes;
      scratch_req = (gasnete_coll_scratch_req_t*) gasneti_calloc(1,sizeof(gasnete_coll_scratch_req_t));
      /*fill out the tree information*/
      scratch_req->tree_type = tree_info->geom->tree_type;
      scratch_req->root = tree_info->geom->root;
      scratch_req->tree_dir = GASNETE_COLL_DOWN_TREE;
      scratch_req->team = team;
      scratch_req->op_type = GASNETE_COLL_TREE_OP;
      /*fill out the peer information*/
      
      if(team->myrank == tree_info->geom->root) {
        scratch_req->incoming_size = nbytes;
        scratch_req->num_in_peers = 0;
        scratch_req->in_peers = NULL;
        
      }
      else {
        scratch_req->incoming_size = nbytes;
        scratch_req->num_in_peers = 1;
        scratch_req->in_peers = &(GASNETE_COLL_TREE_GEOM_PARENT(tree_info->geom));
        
      }
      out_sizes = (uint64_t*) gasneti_malloc(sizeof(uint64_t)*GASNETE_COLL_TREE_GEOM_CHILD_COUNT(tree_info->geom));
      scratch_req->num_out_peers = GASNETE_COLL_TREE_GEOM_CHILD_COUNT(tree_info->geom);
      scratch_req->out_peers = GASNETE_COLL_TREE_GEOM_CHILDREN(tree_info->geom);
      for(i=0; i< GASNETE_COLL_TREE_GEOM_CHILD_COUNT(tree_info->geom); i++) {
        out_sizes[i] = nbytes;
      }
      scratch_req->out_sizes = out_sizes;
    }
    
  }
  #if GASNET_PAR
  if (flags & GASNETE_COLL_THREAD_LOCAL) {
    gasnete_coll_threaddata_t *td = GASNETE_COLL_MYTHREAD_NOALLOC;
    gasnete_coll_generic_data_t *data;

    if_pt (first_thread) {

      
      data = gasnete_coll_generic_alloc(GASNETE_THREAD_PASS_ALONE);
      GASNETE_COLL_GENERIC_SET_TAG(data, broadcastM);
      data->threads.data = gasneti_calloc(gasnete_coll_my_images, sizeof(void *));
      data->args.broadcastM.dstlist    = (void**)data->threads.data;
      data->args.broadcastM.srcimage   = srcimage;
      data->args.broadcastM.srcnode    = gasnete_coll_image_node(srcimage);
      data->args.broadcastM.src        = src; /* may get overwritten, but not worth a branch */
      data->args.broadcastM.nbytes     = nbytes;
      data->options = options;
      data->tree_info = tree_info;
      result = gasnete_coll_op_generic_init_with_scratch(team, flags, data, poll_fn, sequence, scratch_req GASNETE_THREAD_PASS);
    } else {
      result = gasnete_coll_threads_get_handle_and_data(&data GASNETE_THREAD_PASS);
      if (td->my_image == srcimage) {
	gasneti_assert(src != NULL);
        data->args.broadcastM.src = src;
        gasneti_sync_writes();
      }
    }
    gasneti_assert(*dstlist != NULL);
    data->args.broadcastM.dstlist[td->my_local_image] = *dstlist; /* signalling write */
  } else
  #endif

  if_pt (first_thread) {
    gasnete_coll_generic_data_t *data = gasnete_coll_generic_alloc(GASNETE_THREAD_PASS_ALONE);
    int num_addrs = (flags & GASNET_COLL_LOCAL ? gasnete_coll_my_images : gasnete_coll_total_images);
    GASNETE_COLL_GENERIC_SET_TAG(data, broadcastM);

    #if GASNET_PAR
    data->threads.data = gasneti_calloc(num_addrs, sizeof(void *));
    data->args.broadcastM.dstlist  = data->threads.data;
    #else
    data->addrs = gasneti_calloc(num_addrs, sizeof(void *));
    data->args.broadcastM.dstlist  = data->addrs;
    #endif    
    GASNETE_FAST_UNALIGNED_MEMCPY(data->args.broadcastM.dstlist , dstlist, sizeof(void*)*num_addrs);

    #if !GASNET_SEQ
      data->args.broadcastM.srcimage = srcimage;
    #endif
    data->args.broadcastM.srcnode    = gasnete_coll_image_node(srcimage);
    data->args.broadcastM.src        = src;
    data->args.broadcastM.nbytes     = nbytes;
    data->options = options;
    data->tree_info = tree_info;
    result = gasnete_coll_op_generic_init_with_scratch(team, flags, data, poll_fn, sequence, scratch_req GASNETE_THREAD_PASS);
  } else {
    result = gasnete_coll_threads_get_handle(GASNETE_THREAD_PASS_ALONE);
  }

  gasnete_coll_threads_unlock(GASNETE_THREAD_PASS_ALONE);
  return result;
}

extern gasnet_coll_handle_t
gasnete_coll_broadcastM_nb_default(gasnet_team_handle_t team,
				   void * const dstlist[],
				   gasnet_image_t srcimage, void *src,
				   size_t nbytes, int flags, uint32_t sequence
                                   GASNETE_THREAD_FARG)
{
  const size_t eager_limit = gasnete_coll_p2p_eager_min;

  if_pf (!gasnete_coll_opt_enabled) {
    return gasnete_coll_broadcastM_nb_default_OLD(team, dstlist, srcimage, src, nbytes,
				                  flags, sequence GASNETE_THREAD_PASS);
  }

  #if GASNET_SEQ
  /* Exactly one thread-local addr - forward to bcast_nb() */
  if (flags & GASNET_COLL_LOCAL) {
    return gasnete_coll_broadcast_nb(team, dstlist[0], srcimage, src, nbytes,
				     flags, sequence GASNETE_THREAD_PASS);
  }
  #endif


  /* "Discover" in-segment flags if needed/possible */
  flags = gasnete_coll_segment_checkM(flags, 0, 0, dstlist, nbytes, 1, srcimage, src, nbytes);

  /* Choose algorithm based on arguments */
  if (0 && (nbytes <= eager_limit) &&
      (flags & (GASNET_COLL_IN_MYSYNC | GASNET_COLL_OUT_MYSYNC | GASNET_COLL_LOCAL))) {
    /* Small enough for Eager, which will eliminate any barriers for *_MYSYNC and
     * the need for passing addresses for _LOCAL
     * Eager is totally AM-based and thus safe regardless of *_IN_SEGMENT
     */
    return gasnete_coll_bcastM_TreeEager(team, dstlist, srcimage, src, nbytes, flags, gasnete_coll_get_current_tree_kind(), sequence GASNETE_THREAD_PASS);
    //return gasnete_coll_bcastM_Eager(team, dstlist, srcimage, src, nbytes, flags, sequence GASNETE_THREAD_PASS);
  } else if (flags & GASNET_COLL_DST_IN_SEGMENT) {
#if 1
    return gasnete_coll_bcastM_TreePutSeg(team, dstlist, srcimage, src, nbytes, flags, gasnete_coll_get_current_tree_kind(), sequence GASNETE_THREAD_PASS);
#else
    if ((flags & GASNET_COLL_SINGLE) && ((flags & GASNET_COLL_IN_NOSYNC) || (flags & GASNET_COLL_IN_ALLSYNC))) {
      /* We use a Tree Put-based algorithm w/ tree barriers for *_ALLSYNC */
      return gasnete_coll_bcastM_TreePut(team, dstlist, srcimage, src, nbytes, flags, gasnete_coll_get_current_tree_kind(), sequence GASNETE_THREAD_PASS);
    } else {
      /* Tree Put Scratch  (valid for coll local and coll single mysync)*/
      /* XXX: could do better w/ RVPut since dst is writtable */
      return gasnete_coll_bcastM_RVous(team, dstlist, srcimage, src, nbytes, flags, sequence GASNETE_THREAD_PASS);
    }
#endif
  } else if (flags & GASNET_COLL_SRC_IN_SEGMENT) {
    if (flags & (GASNET_COLL_IN_MYSYNC | GASNET_COLL_OUT_MYSYNC | GASNET_COLL_LOCAL)) {
      /* We can use Rendezvous+Get to eliminate any barriers for *_MYSYNC.
       * The Rendezvous is needed for _LOCAL.
       */
      return gasnete_coll_bcastM_RVGet(team, dstlist, srcimage, src, nbytes, flags, sequence GASNETE_THREAD_PASS);
    } else {
      return gasnete_coll_bcastM_Get(team, dstlist, srcimage, src, nbytes, flags, sequence GASNETE_THREAD_PASS);
    }
  } else {
  /* If we reach here then neither src nor dst is in-segment */
    return gasnete_coll_bcastM_RVous(team, dstlist, srcimage, src, nbytes, flags, sequence GASNETE_THREAD_PASS);
  }
}

/*---------------------------------------------------------------------------------*/
/* gasnete_coll_scatter_nb() */


extern gasnet_coll_handle_t
gasnete_coll_generic_scatter_nb(gasnet_team_handle_t team,
				void *dst,
				gasnet_image_t srcimage, void *src,
				size_t nbytes, size_t dist, int flags,
				gasnete_coll_poll_fn poll_fn, int options,
                                gasnete_coll_tree_data_t *tree_info, uint32_t sequence
                                GASNETE_THREAD_FARG) {
  gasnet_coll_handle_t result;
  gasnete_coll_scratch_req_t *scratch_req=NULL;
  uint64_t *out_sizes;
  int i;

  if(options & (GASNETE_COLL_USE_SCRATCH)) {
    scratch_req = (gasnete_coll_scratch_req_t*) gasneti_calloc(1,sizeof(gasnete_coll_scratch_req_t));
    /*fill out the tree information*/
    scratch_req->tree_type = tree_info->geom->tree_type;
    scratch_req->root = tree_info->geom->root;
    scratch_req->team = team;
    scratch_req->tree_dir = GASNETE_COLL_DOWN_TREE;
    scratch_req->op_type = GASNETE_COLL_TREE_OP;
    /*fill out the peer information*/
    scratch_req->incoming_size = nbytes*tree_info->geom->mysubtree_size;
  /*  fprintf(stderr, "%d> requesting %d bytes as incoming\n", gasneti_mynode, scratch_req->incoming_size); */
    if(team->myrank == tree_info->geom->root) {
      scratch_req->num_in_peers = 0;
      scratch_req->in_peers = NULL;      
    }
    else {
      scratch_req->num_in_peers = 1;
      scratch_req->in_peers = &(GASNETE_COLL_TREE_GEOM_PARENT(tree_info->geom));
    }
    out_sizes = (uint64_t*) gasneti_malloc(sizeof(uint64_t)*GASNETE_COLL_TREE_GEOM_CHILD_COUNT(tree_info->geom));
    scratch_req->num_out_peers = GASNETE_COLL_TREE_GEOM_CHILD_COUNT(tree_info->geom);
    scratch_req->out_peers = GASNETE_COLL_TREE_GEOM_CHILDREN(tree_info->geom);
    for(i=0; i< GASNETE_COLL_TREE_GEOM_CHILD_COUNT(tree_info->geom); i++) {
      out_sizes[i] = nbytes*tree_info->geom->subtree_sizes[i];
/*      fprintf(stderr, "%d> requesting %d bytes on %d\n", gasneti_mynode, out_sizes[i], GASNETE_COLL_TREE_GEOM_CHILDREN(tree_info->geom)[i]);*/

    }
    scratch_req->out_sizes = out_sizes;
  }
  
  gasnete_coll_threads_lock(flags GASNETE_THREAD_PASS);
  if_pt (gasnete_coll_threads_first(GASNETE_THREAD_PASS_ALONE)) {
    gasnete_coll_generic_data_t *data = gasnete_coll_generic_alloc(GASNETE_THREAD_PASS_ALONE);
    GASNETE_COLL_GENERIC_SET_TAG(data, scatter);
    data->args.scatter.dst        = dst;
    #if !GASNET_SEQ
      data->args.scatter.srcimage = srcimage;
    #endif
    data->args.scatter.srcnode    = gasnete_coll_image_node(srcimage);
    data->args.scatter.src        = src;
    data->args.scatter.nbytes     = nbytes;
    data->args.scatter.dist     = dist;
    data->options = options;
    data->tree_info = tree_info;
    result = gasnete_coll_op_generic_init_with_scratch(team, flags, data, poll_fn, sequence, scratch_req GASNETE_THREAD_PASS);
  } else {
    result = gasnete_coll_threads_get_handle(GASNETE_THREAD_PASS_ALONE);
  }
  gasnete_coll_threads_unlock(GASNETE_THREAD_PASS_ALONE);
  return result;
}

extern gasnet_coll_handle_t
gasnete_coll_scatter_nb_default(gasnet_team_handle_t team,
				void *dst,
				gasnet_image_t srcimage, void *src,
				size_t nbytes, int flags, uint32_t sequence
                                GASNETE_THREAD_FARG)
{
  const size_t eager_limit = gasnete_coll_p2p_eager_min;

  if_pf (!gasnete_coll_opt_enabled) {
    return gasnete_coll_scatter_nb_default_OLD(team, dst, srcimage, src, nbytes,
				               flags, sequence GASNETE_THREAD_PASS);
  }
  
  #if GASNET_PAR
  /* Thread-local addr(s) - forward to scatM_nb() */
  if (flags & GASNET_COLL_LOCAL) {
    return gasnete_coll_scatterM_nb(team, &dst, srcimage, src, nbytes,
				    flags | GASNETE_COLL_THREAD_LOCAL, sequence
                                    GASNETE_THREAD_PASS);
  }
  #endif

  /* "Discover" in-segment flags if needed/possible */
  flags = gasnete_coll_segment_check(flags, 0, 0, dst, nbytes,
				     1, srcimage, src, nbytes*gasneti_nodes);

  /* Choose algorithm based on arguments */
  if ((flags & GASNET_COLL_DST_IN_SEGMENT) && (flags & GASNET_COLL_SRC_IN_SEGMENT)) {
    /* Both ends are in-segment */
#if 1
        return gasnete_coll_scat_TreePutSeg(team, dst, srcimage, src, nbytes, flags,
                                             gasnete_coll_get_current_tree_kind(), sequence GASNETE_THREAD_PASS);
#else
    if ((flags & GASNET_COLL_IN_MYSYNC) || (flags & GASNET_COLL_LOCAL)) {
     /* XXXX: fix this later however this should be safe as written */
      if (nbytes <= eager_limit) {
        return gasnete_coll_scat_Eager(team, dst, srcimage, src, nbytes, flags, sequence GASNETE_THREAD_PASS);
      } else {
        return gasnete_coll_scat_RVGet(team, dst, srcimage, src, nbytes, flags, sequence GASNETE_THREAD_PASS);
      }
    } else if ((flags & GASNET_COLL_OUT_MYSYNC) && (nbytes <= eager_limit)) {
      return gasnete_coll_scat_Eager(team, dst, srcimage, src, nbytes, flags, sequence GASNETE_THREAD_PASS);
    } else {
     return gasnete_coll_scat_Put(team, dst, srcimage, src, nbytes, flags,  sequence GASNETE_THREAD_PASS);
    }
#endif
  } else if (nbytes <= eager_limit) {
    /* Small enough for Eager, which works for out-of-segment src and/or dst */
    return gasnete_coll_scat_Eager(team, dst, srcimage, src, nbytes, flags, sequence GASNETE_THREAD_PASS);
  } else if (flags & GASNET_COLL_SRC_IN_SEGMENT) {
    /* Only the source is in-segment (and too big for Eager) */
    if ((flags & GASNET_COLL_IN_NOSYNC) && (flags & GASNET_COLL_SINGLE)) {
      return gasnete_coll_scat_Get(team, dst, srcimage, src, nbytes, flags, sequence GASNETE_THREAD_PASS);
    } else {
      return gasnete_coll_scat_RVGet(team, dst, srcimage, src, nbytes, flags, sequence GASNETE_THREAD_PASS);
    }
  } else if (flags & GASNET_COLL_DST_IN_SEGMENT) {
    /* Only the destination is in-segment (and too big for Eager) */
    if (flags & GASNET_COLL_SINGLE) {
      return gasnete_coll_scat_Put(team, dst, srcimage, src, nbytes, flags, sequence GASNETE_THREAD_PASS);
    } else {
      /* XXX: could do better since DST is in-segment */
      return gasnete_coll_scat_RVous(team, dst, srcimage, src, nbytes, flags, sequence GASNETE_THREAD_PASS);
    }
  } else {
    return gasnete_coll_scat_RVous(team, dst, srcimage, src, nbytes, flags, sequence GASNETE_THREAD_PASS);
  }
}

/*---------------------------------------------------------------------------------*/
/* gasnete_coll_scatterM_nb() */

extern gasnet_coll_handle_t
gasnete_coll_generic_scatterM_nb(gasnet_team_handle_t team,
				 void * const dstlist[],
				 gasnet_image_t srcimage, void *src,
				 size_t nbytes, size_t dist, int flags,
				 gasnete_coll_poll_fn poll_fn, int options,
                                 gasnete_coll_tree_data_t *tree_info, uint32_t sequence
                                 GASNETE_THREAD_FARG) {
  gasnet_coll_handle_t result;
  int first_thread = 0;
  
  gasnete_coll_scratch_req_t* scratch_req=NULL;

  gasnete_coll_threads_lock(flags GASNETE_THREAD_PASS);
  if(gasnete_coll_threads_first(GASNETE_THREAD_PASS_ALONE)) {
    first_thread =1;
    if(options & (GASNETE_COLL_USE_SCRATCH)) {
      int i;
      uint64_t *out_sizes;
      gasneti_assert(gasnete_coll_fixed_image_count);
      scratch_req = (gasnete_coll_scratch_req_t*) gasneti_calloc(1,sizeof(gasnete_coll_scratch_req_t));
      /*fill out the tree information*/
      scratch_req->tree_type = tree_info->geom->tree_type;
      scratch_req->root = tree_info->geom->root;
      scratch_req->team = team;
      scratch_req->tree_dir = GASNETE_COLL_DOWN_TREE;
      scratch_req->op_type = GASNETE_COLL_TREE_OP;
      /*fill out the peer information*/
      scratch_req->incoming_size = nbytes*tree_info->geom->mysubtree_size*gasnete_coll_my_images;
      /*  fprintf(stderr, "%d> requesting %d bytes as incoming\n", gasneti_mynode, scratch_req->incoming_size); */
      if(team->myrank == tree_info->geom->root) {
        scratch_req->num_in_peers = 0;
        scratch_req->in_peers = NULL;      
      }
      else {
        scratch_req->num_in_peers = 1;
        scratch_req->in_peers = &(GASNETE_COLL_TREE_GEOM_PARENT(tree_info->geom));
      }
      out_sizes = (uint64_t*) gasneti_malloc(sizeof(uint64_t)*GASNETE_COLL_TREE_GEOM_CHILD_COUNT(tree_info->geom));
      scratch_req->num_out_peers = GASNETE_COLL_TREE_GEOM_CHILD_COUNT(tree_info->geom);
      scratch_req->out_peers = GASNETE_COLL_TREE_GEOM_CHILDREN(tree_info->geom);
      for(i=0; i< GASNETE_COLL_TREE_GEOM_CHILD_COUNT(tree_info->geom); i++) {
        out_sizes[i] = nbytes*tree_info->geom->subtree_sizes[i]*gasnete_coll_my_images;
        /*      fprintf(stderr, "%d> requesting %d bytes on %d\n", gasneti_mynode, out_sizes[i], GASNETE_COLL_TREE_GEOM_CHILDREN(tree_info->geom)[i]);*/
      }
      scratch_req->out_sizes = out_sizes;
    }
  }
  #if GASNET_PAR
  if (flags & GASNETE_COLL_THREAD_LOCAL) {
    gasnete_coll_threaddata_t *td = GASNETE_COLL_MYTHREAD_NOALLOC;
    gasnete_coll_generic_data_t *data;
    
    if_pt (first_thread) {
      data = gasnete_coll_generic_alloc(GASNETE_THREAD_PASS_ALONE);
      GASNETE_COLL_GENERIC_SET_TAG(data, scatterM);
      data->threads.data = gasneti_calloc(gasnete_coll_my_images, sizeof(void *));
      data->args.scatterM.dstlist    = (void**)data->threads.data;
      data->args.scatterM.srcimage   = srcimage;
      data->args.scatterM.srcnode    = gasnete_coll_image_node(srcimage);
      data->args.scatterM.src        = src; /* may get overwritten, but not worth a branch */
      data->args.scatterM.nbytes     = nbytes;
      data->args.scatterM.dist       = dist;
      data->options = options;
      data->tree_info=tree_info;
      result = gasnete_coll_op_generic_init_with_scratch(team, flags, data, poll_fn, sequence, scratch_req GASNETE_THREAD_PASS);
    } else {
      result = gasnete_coll_threads_get_handle_and_data(&data GASNETE_THREAD_PASS);
      if (td->my_image == srcimage) {
	gasneti_assert(src != NULL);
        data->args.scatterM.src = src;
        gasneti_sync_writes();
      }
    }
    gasneti_assert(*dstlist != NULL);
    data->args.scatterM.dstlist[td->my_local_image] = *dstlist; /* signalling write */
  } else
  #endif

  if_pt (first_thread) {
    gasnete_coll_generic_data_t *data = gasnete_coll_generic_alloc(GASNETE_THREAD_PASS_ALONE);
    int num_addrs = (flags & GASNET_COLL_LOCAL ? gasnete_coll_my_images : gasnete_coll_total_images);
    GASNETE_COLL_GENERIC_SET_TAG(data, scatterM);
    
    #if GASNET_PAR
    data->threads.data = gasneti_calloc(num_addrs, sizeof(void *));
    data->args.scatterM.dstlist  = data->threads.data;
    #else
    data->addrs = gasneti_calloc(num_addrs, sizeof(void *));
    data->args.scatterM.dstlist  = data->addrs;
    #endif    
    GASNETE_FAST_UNALIGNED_MEMCPY(data->args.scatterM.dstlist , dstlist, sizeof(void*)*num_addrs);
    
    #if !GASNET_SEQ
      data->args.scatterM.srcimage = srcimage;
    #endif
    data->args.scatterM.srcnode    = gasnete_coll_image_node(srcimage);
    data->args.scatterM.src        = src;
    data->args.scatterM.nbytes     = nbytes;
    data->args.scatterM.dist       = dist;
    data->options = options;
    data->tree_info=tree_info;
    result = gasnete_coll_op_generic_init_with_scratch(team, flags, data, poll_fn, sequence, scratch_req GASNETE_THREAD_PASS);
  } else {
    result = gasnete_coll_threads_get_handle(GASNETE_THREAD_PASS_ALONE);
  }

  gasnete_coll_threads_unlock(GASNETE_THREAD_PASS_ALONE);
  return result;
}

extern gasnet_coll_handle_t
gasnete_coll_scatterM_nb_default(gasnet_team_handle_t team,
				 void * const dstlist[],
				 gasnet_image_t srcimage, void *src,
				 size_t nbytes, int flags, uint32_t sequence
                                 GASNETE_THREAD_FARG) {
  const size_t eager_limit = gasnete_coll_p2p_eager_min;

  if_pf (!gasnete_coll_opt_enabled) {
    return gasnete_coll_scatterM_nb_default_OLD(team, dstlist, srcimage, src, nbytes,
				                flags, sequence GASNETE_THREAD_PASS);
  }

  #if GASNET_SEQ
  /* Exactly one thread-local addr - forward to scat_nb() */
  if (flags & GASNET_COLL_LOCAL) {
    return gasnete_coll_scatter_nb(team, dstlist[0], srcimage, src, nbytes,
				   flags, sequence GASNETE_THREAD_PASS);
  }
  #endif

  /* "Discover" in-segment flags if needed/possible */
  flags = gasnete_coll_segment_checkM(flags, 0, 0, dstlist, nbytes,
				      1, srcimage, src, nbytes*gasneti_nodes);

  /* Choose algorithm based on arguments */
  if ((flags & GASNET_COLL_DST_IN_SEGMENT) && (flags & GASNET_COLL_SRC_IN_SEGMENT)) {
    /* Both ends are in-segment */
    if(gasnete_coll_fixed_image_count) {
      /* require that all ndoes have the same number of GASNet images*/
      return gasnete_coll_scatM_TreePutSeg(team, dstlist, srcimage, src, nbytes, flags,
                                           gasnete_coll_get_current_tree_kind(), sequence GASNETE_THREAD_PASS); 
    } else if ((flags & GASNET_COLL_IN_MYSYNC) || (flags & GASNET_COLL_LOCAL)) {
      if (nbytes <= eager_limit) {
        return gasnete_coll_scatM_Eager(team, dstlist, srcimage, src, nbytes, flags, sequence GASNETE_THREAD_PASS);
      } else {
        return gasnete_coll_scatM_RVGet(team, dstlist, srcimage, src, nbytes, flags, sequence GASNETE_THREAD_PASS);
      }
    } else if ((flags & GASNET_COLL_OUT_MYSYNC) && (nbytes <= eager_limit)) {
      return gasnete_coll_scatM_Eager(team, dstlist, srcimage, src, nbytes, flags, sequence GASNETE_THREAD_PASS);
    } else {
      return gasnete_coll_scatM_Get(team, dstlist, srcimage, src, nbytes, flags, sequence GASNETE_THREAD_PASS);
    }
  } else if (nbytes <= eager_limit) {
    /* Small enough for Eager, which works for out-of-segment src and/or dst */
    return gasnete_coll_scatM_Eager(team, dstlist, srcimage, src, nbytes, flags, sequence GASNETE_THREAD_PASS);
  } else if (flags & GASNET_COLL_SRC_IN_SEGMENT) {
    /* Only the source is in-segment (and too big for Eager) */
    if ((flags & GASNET_COLL_IN_NOSYNC) && (flags & GASNET_COLL_SINGLE)) {
      return gasnete_coll_scatM_Get(team, dstlist, srcimage, src, nbytes, flags, sequence GASNETE_THREAD_PASS);
    } else {
      return gasnete_coll_scatM_RVGet(team, dstlist, srcimage, src, nbytes, flags, sequence GASNETE_THREAD_PASS);
    }
  } else if (flags & GASNET_COLL_DST_IN_SEGMENT) {
    /* Only the destination is in-segment (and too big for Eager) */
    if (flags & GASNET_COLL_SINGLE) {
      return gasnete_coll_scatM_Put(team, dstlist, srcimage, src, nbytes, flags, sequence GASNETE_THREAD_PASS);
    } else {
      /* Could do better since dst is in-segment */
      return gasnete_coll_scatM_RVous(team, dstlist, srcimage, src, nbytes, flags, sequence GASNETE_THREAD_PASS);
    }
  } else {
    return gasnete_coll_scatM_RVous(team, dstlist, srcimage, src, nbytes, flags, sequence GASNETE_THREAD_PASS);
  }
}

/*---------------------------------------------------------------------------------*/
/* gasnete_coll_gather_nb() */

extern gasnet_coll_handle_t
gasnete_coll_generic_gather_nb(gasnet_team_handle_t team,
			       gasnet_image_t dstimage, void *dst,
			       void *src,
			       size_t nbytes, size_t dist, int flags,
			       gasnete_coll_poll_fn poll_fn, int options,
                               gasnete_coll_tree_data_t *tree_info, uint32_t sequence
                               GASNETE_THREAD_FARG) {
  gasnet_coll_handle_t result;
  gasnete_coll_scratch_req_t *scratch_req=NULL;
  uint64_t *out_sizes;
  int i;

  if(options & (GASNETE_COLL_USE_SCRATCH)) {
    scratch_req = (gasnete_coll_scratch_req_t*) gasneti_calloc(1,sizeof(gasnete_coll_scratch_req_t));
    /*fill out the tree information*/
    scratch_req->tree_type = tree_info->geom->tree_type;
    scratch_req->tree_dir = GASNETE_COLL_UP_TREE;
    scratch_req->root = tree_info->geom->root;

    scratch_req->team = team;
    scratch_req->op_type = GASNETE_COLL_TREE_OP;
    /*fill out the peer information*/
    scratch_req->incoming_size = nbytes*tree_info->geom->mysubtree_size;
    /*  fprintf(stderr, "%d> requesting %d bytes as incoming\n", gasneti_mynode, scratch_req->incoming_size); */
    scratch_req->num_in_peers = GASNETE_COLL_TREE_GEOM_CHILD_COUNT(tree_info->geom);
    if(scratch_req->num_in_peers > 0) {
      scratch_req->in_peers = GASNETE_COLL_TREE_GEOM_CHILDREN(tree_info->geom);      
    } else {
      scratch_req->in_peers = NULL;
    }
    if(team->myrank == tree_info->geom->root) {
      scratch_req->num_out_peers = 0;
      scratch_req->out_peers = NULL;      
      scratch_req->out_sizes = NULL;
    }
    else {
      scratch_req->num_out_peers = 1;
      scratch_req->out_peers = &(GASNETE_COLL_TREE_GEOM_PARENT(tree_info->geom));
      scratch_req->out_sizes = (uint64_t*) gasneti_malloc(sizeof(uint64_t)*1);
      scratch_req->out_sizes[0] = nbytes*tree_info->geom->parent_subtree_size;
    }
  }
  
  gasnete_coll_threads_lock(flags GASNETE_THREAD_PASS);
  if_pt (gasnete_coll_threads_first(GASNETE_THREAD_PASS_ALONE)) {
    gasnete_coll_generic_data_t *data = gasnete_coll_generic_alloc(GASNETE_THREAD_PASS_ALONE);
    GASNETE_COLL_GENERIC_SET_TAG(data, gather);
    #if !GASNET_SEQ
      data->args.gather.dstimage = dstimage;
    #endif
    data->args.gather.dstnode    = gasnete_coll_image_node(dstimage);
    data->args.gather.dst        = dst;
    data->args.gather.src        = src;
    data->args.gather.nbytes     = nbytes;
    data->args.gather.dist     = dist;
    data->options = options;
    data->private_data = NULL; data->tree_info=tree_info;
    result = gasnete_coll_op_generic_init_with_scratch(team, flags, data, poll_fn, sequence, scratch_req GASNETE_THREAD_PASS);
  } else {
    result = gasnete_coll_threads_get_handle(GASNETE_THREAD_PASS_ALONE);
  }
  gasnete_coll_threads_unlock(GASNETE_THREAD_PASS_ALONE);
  return result;
}

extern gasnet_coll_handle_t
gasnete_coll_gather_nb_default(gasnet_team_handle_t team,
			       gasnet_image_t dstimage, void *dst,
			       void *src,
			       size_t nbytes, int flags, uint32_t sequence
                               GASNETE_THREAD_FARG)
{
  const size_t eager_limit = gasnete_coll_p2p_eager_min;

  if_pf (!gasnete_coll_opt_enabled) {
    return gasnete_coll_gather_nb_default_OLD(team, dstimage, dst, src, nbytes,
				              flags, sequence GASNETE_THREAD_PASS);
  }

  #if GASNET_PAR
  /* Thread-local addr(s) - forward to gathM_nb() */
  if (flags & GASNET_COLL_LOCAL) {
    return gasnete_coll_gatherM_nb(team, dstimage, dst, &src, nbytes,
				   flags | GASNETE_COLL_THREAD_LOCAL, sequence
                                   GASNETE_THREAD_PASS);
  }
  #endif

  /* "Discover" in-segment flags if needed/possible */
  flags = gasnete_coll_segment_check(flags, 1, dstimage, dst, nbytes*gasneti_nodes,
				     0, 0, src, nbytes);

  /* Choose algorithm based on arguments */
  if ((flags & GASNET_COLL_DST_IN_SEGMENT) && (flags & GASNET_COLL_SRC_IN_SEGMENT)) {
    /* Both ends are in-segment */
    if(!(flags & GASNETE_COLL_SUBORDINATE)) {
      return gasnete_coll_gath_TreePutSeg(team, dstimage, dst, src, nbytes, flags, gasnete_coll_get_current_tree_kind(), sequence GASNETE_THREAD_PASS);
    }
    else if ((flags & GASNET_COLL_IN_MYSYNC) || (flags & GASNET_COLL_LOCAL)) {
      if (nbytes <= eager_limit) {
        return gasnete_coll_gath_Eager(team, dstimage, dst, src, nbytes, flags, sequence GASNETE_THREAD_PASS);
      } else {
        return gasnete_coll_gath_RVPut(team, dstimage, dst, src, nbytes, flags, sequence GASNETE_THREAD_PASS);
      }
    } else if ((flags & GASNET_COLL_OUT_MYSYNC) && (nbytes <= eager_limit)) {
      return gasnete_coll_gath_Eager(team, dstimage, dst, src, nbytes, flags, sequence GASNETE_THREAD_PASS);
    } else {
      return gasnete_coll_gath_Put(team, dstimage, dst, src, nbytes, flags, sequence GASNETE_THREAD_PASS);
    }
  } else if (nbytes <= eager_limit) {
    /* Small enough for Eager, which works for out-of-segment src and/or dst */
    return gasnete_coll_gath_Eager(team, dstimage, dst, src, nbytes, flags, sequence GASNETE_THREAD_PASS);
  } else if (flags & GASNET_COLL_DST_IN_SEGMENT) {
    /* Only the destination is in-segment (and too big for Eager) */
    if ((flags & GASNET_COLL_IN_NOSYNC) && (flags & GASNET_COLL_SINGLE)) {
      return gasnete_coll_gath_Put(team, dstimage, dst, src, nbytes, flags, sequence GASNETE_THREAD_PASS);
    } else {
      return gasnete_coll_gath_RVPut(team, dstimage, dst, src, nbytes, flags, sequence GASNETE_THREAD_PASS);
    }
  } else if (flags & GASNET_COLL_SRC_IN_SEGMENT) {
    /* Only the source is in-segment (and too big for Eager) */
    if (flags & GASNET_COLL_SINGLE) {
      return gasnete_coll_gath_Get(team, dstimage, dst, src, nbytes, flags, sequence GASNETE_THREAD_PASS);
    } else {
      /* XXX: could do better since src is in-segment */
      return gasnete_coll_gath_RVous(team, dstimage, dst, src, nbytes, flags, sequence GASNETE_THREAD_PASS);
    }
  } else {
    return gasnete_coll_gath_RVous(team, dstimage, dst, src, nbytes, flags, sequence GASNETE_THREAD_PASS);
  }
}

/*---------------------------------------------------------------------------------*/
/* gasnete_coll_gatherM_nb() */

extern gasnet_coll_handle_t
gasnete_coll_generic_gatherM_nb(gasnet_team_handle_t team,
				gasnet_image_t dstimage, void *dst,
				void * const srclist[],
				size_t nbytes, size_t dist, int flags,
				gasnete_coll_poll_fn poll_fn, int options,
                                gasnete_coll_tree_data_t *tree_info, uint32_t sequence
                                GASNETE_THREAD_FARG) {
  gasnet_coll_handle_t result;
  int first_thread = 0;
  
  gasnete_coll_scratch_req_t* scratch_req=NULL;
  
  gasnete_coll_threads_lock(flags GASNETE_THREAD_PASS);
  if(gasnete_coll_threads_first(GASNETE_THREAD_PASS_ALONE)) {
    first_thread  = 1;
    if(options & GASNETE_COLL_USE_SCRATCH) {
      int i;
      uint64_t *out_sizes;
      gasneti_assert(gasnete_coll_fixed_image_count);
      scratch_req = (gasnete_coll_scratch_req_t*) gasneti_calloc(1,sizeof(gasnete_coll_scratch_req_t));
      /*fill out the tree information*/
      scratch_req->tree_type = tree_info->geom->tree_type;
      scratch_req->tree_dir = GASNETE_COLL_UP_TREE;
      scratch_req->root = tree_info->geom->root;
      
      scratch_req->team = team;
      scratch_req->op_type = GASNETE_COLL_TREE_OP;
      /*fill out the peer information*/
      scratch_req->incoming_size = nbytes*tree_info->geom->mysubtree_size*gasnete_coll_my_images;
      /*  fprintf(stderr, "%d> requesting %d bytes as incoming\n", gasneti_mynode, scratch_req->incoming_size); */
      scratch_req->num_in_peers = GASNETE_COLL_TREE_GEOM_CHILD_COUNT(tree_info->geom);
      if(scratch_req->num_in_peers > 0) {
        scratch_req->in_peers = GASNETE_COLL_TREE_GEOM_CHILDREN(tree_info->geom);      
      } else {
        scratch_req->in_peers = NULL;
      }
      if(team->myrank == tree_info->geom->root) {
        scratch_req->num_out_peers = 0;
        scratch_req->out_peers = NULL;      
        scratch_req->out_sizes = NULL;
      }
      else {
        scratch_req->num_out_peers = 1;
        scratch_req->out_peers = &(GASNETE_COLL_TREE_GEOM_PARENT(tree_info->geom));
        scratch_req->out_sizes = (uint64_t*) gasneti_malloc(sizeof(uint64_t)*1);
        scratch_req->out_sizes[0] = nbytes*tree_info->geom->parent_subtree_size*gasnete_coll_my_images;
      }
      
    }
  }
  #if GASNET_PAR
  if (flags & GASNETE_COLL_THREAD_LOCAL) {
    gasnete_coll_threaddata_t *td = GASNETE_COLL_MYTHREAD_NOALLOC;
    gasnete_coll_generic_data_t *data;
    if_pt (first_thread) {
      data = gasnete_coll_generic_alloc(GASNETE_THREAD_PASS_ALONE);
      GASNETE_COLL_GENERIC_SET_TAG(data, gatherM);
      data->threads.data = gasneti_calloc(gasnete_coll_my_images, sizeof(void *));
      data->args.gatherM.srclist    = (void**)data->threads.data;
      data->args.gatherM.dstimage   = dstimage;
      data->args.gatherM.dstnode    = gasnete_coll_image_node(dstimage);
      data->args.gatherM.dst        = dst; /* may get overwritten, but not worth a branch */
      data->args.gatherM.nbytes     = nbytes;
      data->args.gatherM.dist       = dist;
      data->options = options;
      data->tree_info=tree_info;
      data->private_data = NULL; 
      result = gasnete_coll_op_generic_init_with_scratch(team, flags, data, poll_fn, sequence, scratch_req GASNETE_THREAD_PASS);
    } else {
      result = gasnete_coll_threads_get_handle_and_data(&data GASNETE_THREAD_PASS);
      if (td->my_image == dstimage) {
	gasneti_assert(dst != NULL);
        data->args.gatherM.dst = dst;
        gasneti_sync_writes();
      }
    }
    gasneti_assert(*srclist != NULL);
    data->args.gatherM.srclist[td->my_local_image] = *srclist; /* signalling write */
  } else
  #endif

  if_pt (first_thread) {
    gasnete_coll_generic_data_t *data = gasnete_coll_generic_alloc(GASNETE_THREAD_PASS_ALONE);    
    int num_addrs = (flags & GASNET_COLL_LOCAL ? gasnete_coll_my_images : gasnete_coll_total_images);

    GASNETE_COLL_GENERIC_SET_TAG(data, gatherM);

    #if GASNET_PAR
    data->threads.data = gasneti_calloc(num_addrs, sizeof(void *));
    data->args.gatherM.srclist = data->threads.data;
    #else
    data->addrs = gasneti_calloc(num_addrs, sizeof(void *));
    data->args.gatherM.srclist = data->addrs;
    #endif    
    GASNETE_FAST_UNALIGNED_MEMCPY(data->args.gatherM.srclist, srclist, sizeof(void*)*num_addrs);
    
    #if !GASNET_SEQ
      data->args.gatherM.dstimage = dstimage;
    #endif
    data->args.gatherM.dstnode    = gasnete_coll_image_node(dstimage);
    data->args.gatherM.dst        = dst;
    data->args.gatherM.nbytes     = nbytes;
    data->args.gatherM.dist       = dist;
    data->options = options;
    data->private_data = NULL; 
    data->tree_info=tree_info;
    result = gasnete_coll_op_generic_init_with_scratch(team, flags, data, poll_fn, sequence, scratch_req GASNETE_THREAD_PASS);
  } else {
    result = gasnete_coll_threads_get_handle(GASNETE_THREAD_PASS_ALONE);
  }

  gasnete_coll_threads_unlock(GASNETE_THREAD_PASS_ALONE);
  return result;
}

extern gasnet_coll_handle_t
gasnete_coll_gatherM_nb_default(gasnet_team_handle_t team,
				gasnet_image_t dstimage, void *dst,
				void * const srclist[],
				size_t nbytes, int flags, uint32_t sequence
                                GASNETE_THREAD_FARG)
{
  const size_t eager_limit = gasnete_coll_p2p_eager_min;

  if_pf(!gasnete_coll_opt_enabled) {
    return gasnete_coll_gatherM_nb_default_OLD(team, dstimage, dst, srclist, nbytes,
				               flags, sequence GASNETE_THREAD_PASS);
  }

  #if GASNET_SEQ
  /* Exactly one thread-local addr - forward to gath_nb() */
  if (flags & GASNET_COLL_LOCAL) {
    return gasnete_coll_gather_nb(team, dstimage, dst, srclist[0], nbytes,
				  flags, sequence GASNETE_THREAD_PASS);
  }
  #endif

  /* "Discover" in-segment flags if needed/possible */
  flags = gasnete_coll_segment_checkM(flags, 1, dstimage, dst, nbytes*gasneti_nodes,
				      0, 0, srclist, nbytes);

  /* Choose algorithm based on arguments */
  if ((flags & GASNET_COLL_DST_IN_SEGMENT) && (flags & GASNET_COLL_SRC_IN_SEGMENT)) {
    /* Both ends are in-segment */
    if(!(flags & GASNETE_COLL_SUBORDINATE) && gasnete_coll_fixed_image_count) {
      /*XXX: with the current implementation of the scratch space it does not make sense to use this for
             all-gather and exchange since it will serialize the gathers which is not what we want
      */
      return gasnete_coll_gathM_TreePutSeg(team, dstimage, dst, srclist, nbytes, flags, 
                                           gasnete_coll_get_current_tree_kind(), sequence GASNETE_THREAD_PASS);
    } else if ((flags & GASNET_COLL_IN_MYSYNC) || (flags & GASNET_COLL_LOCAL)) {
      if (nbytes <= eager_limit) {
        return gasnete_coll_gathM_Eager(team, dstimage, dst, srclist, nbytes, flags, sequence GASNETE_THREAD_PASS);
      } else {
        return gasnete_coll_gathM_RVPut(team, dstimage, dst, srclist, nbytes, flags, sequence GASNETE_THREAD_PASS);
      }
    } else if ((flags & GASNET_COLL_OUT_MYSYNC) && (nbytes <= eager_limit)) {
      return gasnete_coll_gathM_Eager(team, dstimage, dst, srclist, nbytes, flags, sequence GASNETE_THREAD_PASS);
    } else {
      return gasnete_coll_gathM_Put(team, dstimage, dst, srclist, nbytes, flags, sequence GASNETE_THREAD_PASS);
    }
  } else if (nbytes <= eager_limit) {
    /* Small enough for Eager, which works for out-of-segment src and/or dst */
    return gasnete_coll_gathM_Eager(team, dstimage, dst, srclist, nbytes, flags, sequence GASNETE_THREAD_PASS);
  } else if (flags & GASNET_COLL_DST_IN_SEGMENT) {
    /* Only the destination is in-segment (and too big for Eager) */
    if ((flags & GASNET_COLL_IN_NOSYNC) && (flags & GASNET_COLL_SINGLE)) {
      return gasnete_coll_gathM_Put(team, dstimage, dst, srclist, nbytes, flags, sequence GASNETE_THREAD_PASS);
    } else {
      return gasnete_coll_gathM_RVPut(team, dstimage, dst, srclist, nbytes, flags, sequence GASNETE_THREAD_PASS);
    }
  } else if (flags & GASNET_COLL_SRC_IN_SEGMENT) {
    /* Only the source is in-segment (and too big for Eager) */
    if (flags & GASNET_COLL_SINGLE) {
      return gasnete_coll_gathM_Get(team, dstimage, dst, srclist, nbytes, flags, sequence GASNETE_THREAD_PASS);
    } else {
      /* XXX: could do better since src is in-segment */
      return gasnete_coll_gathM_RVous(team, dstimage, dst, srclist, nbytes, flags, sequence GASNETE_THREAD_PASS);
    }
  } else {
    return gasnete_coll_gathM_RVous(team, dstimage, dst, srclist, nbytes, flags, sequence GASNETE_THREAD_PASS);
  }
}

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
      if (!gasnete_coll_generic_all_threads(data) ||
	  !gasnete_coll_generic_insync(data)) {
	break;
      }
      data->state = 1;

    case 1:	/* Initiate data movement */
      if (!GASNETE_COLL_MAY_INIT_FOR(op)) break;
      {
	gasnet_coll_handle_t *h;
        int flags = GASNETE_COLL_FORWARD_FLAGS(op->flags);
	gasnet_team_handle_t team = op->team;
	void *dst = args->dst;
	void *src = args->src;
	size_t nbytes = args->nbytes;
        gasnet_image_t i;

	/* XXX: freelist ? */
	h = gasneti_malloc(gasnete_coll_total_images * sizeof(gasnet_coll_handle_t));
	data->private_data = h;

        for (i = 0; i < gasnete_coll_total_images; ++i, ++h) {
          *h = gasnete_coll_gather_nb(team, i, dst, src, nbytes,
				      flags, op->sequence+i+1 GASNETE_THREAD_PASS);
          gasnete_coll_save_coll_handle(h GASNETE_THREAD_PASS);
        }
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
gasnete_coll_gall_Gath(gasnet_team_handle_t team,
		       void *dst, void *src,
		       size_t nbytes, int flags, uint32_t sequence
		       GASNETE_THREAD_FARG)
{
  int options = GASNETE_COLL_GENERIC_OPT_INSYNC_IF (!(flags & GASNET_COLL_IN_NOSYNC)) |
		GASNETE_COLL_GENERIC_OPT_OUTSYNC_IF(!(flags & GASNET_COLL_OUT_NOSYNC));
  gasneti_assert(!(flags & GASNETE_COLL_SUBORDINATE));

  return gasnete_coll_generic_gather_all_nb(team, dst, src, nbytes, flags,
					    &gasnete_coll_pf_gall_Gath, options,
					    NULL, gasnete_coll_total_images GASNETE_THREAD_PASS);
}

extern gasnet_coll_handle_t
gasnete_coll_generic_gather_all_nb(gasnet_team_handle_t team,
				   void *dst, void *src,
				   size_t nbytes, int flags,
				   gasnete_coll_poll_fn poll_fn, int options,
				   void *private_data,uint32_t sequence
				   GASNETE_THREAD_FARG) {
  gasnet_coll_handle_t result;
  gasnete_coll_scratch_req_t *scratch_req=NULL;
  uint32_t *out_sizes;
  int i;
  gasnete_coll_dissem_info_t *dissem = gasnete_coll_fetch_dissemination(2,team);
  
  if(options & (GASNETE_COLL_USE_SCRATCH)) {
    /*fill out a scratch request form*/	
    scratch_req = (gasnete_coll_scratch_req_t*) gasneti_calloc(1,sizeof(gasnete_coll_scratch_req_t));
    scratch_req->op_type = GASNETE_COLL_DISSEM_OP;
    scratch_req->team = team;
    scratch_req->tree_dir = GASNETE_COLL_UP_TREE;
    scratch_req->incoming_size = 
     nbytes*gasnete_coll_total_images;
    scratch_req->num_out_peers = scratch_req->num_in_peers = GASNETE_COLL_DISSEM_GET_PEER_COUNT(dissem);
    scratch_req->out_peers = GASNETE_COLL_DISSEM_GET_BEHIND_PEERS(dissem);
    scratch_req->in_peers = GASNETE_COLL_DISSEM_GET_FRONT_PEERS(dissem);
    scratch_req->out_sizes = (uint64_t*) gasneti_malloc(sizeof(uint64_t)*1);
    scratch_req->out_sizes[0] = scratch_req->incoming_size;
  }  
  gasnete_coll_threads_lock(flags GASNETE_THREAD_PASS);
  
  
  if_pt (gasnete_coll_threads_first(GASNETE_THREAD_PASS_ALONE)) {
    gasnete_coll_generic_data_t *data = gasnete_coll_generic_alloc(GASNETE_THREAD_PASS_ALONE);
    GASNETE_COLL_GENERIC_SET_TAG(data, gather_all);
    data->args.gather_all.dst     = dst;
    data->args.gather_all.src     = src;
    data->args.gather_all.nbytes  = nbytes;
    data->options = options;
    data->private_data = private_data; data->tree_info=NULL;
    data->dissem_info = dissem;
    result = gasnete_coll_op_generic_init_with_scratch(team, flags, data, poll_fn, sequence, scratch_req GASNETE_THREAD_PASS);
  } else {
    result = gasnete_coll_threads_get_handle(GASNETE_THREAD_PASS_ALONE);
  }
  gasnete_coll_threads_unlock(GASNETE_THREAD_PASS_ALONE);
  return result;
}

extern gasnet_coll_handle_t
gasnete_coll_gather_all_nb_default(gasnet_team_handle_t team,
				   void *dst, void *src,
				   size_t nbytes, int flags, uint32_t sequence
				   GASNETE_THREAD_FARG) {
  if_pf (!gasnete_coll_opt_enabled) {
    return gasnete_coll_gather_all_nb_default_OLD(team, dst, src, nbytes,
				                  flags, sequence GASNETE_THREAD_PASS);
  }

  #if GASNET_PAR
  /* Thread-local addr(s) - forward to gallM_nb() */
  if (flags & GASNET_COLL_LOCAL) {
    return gasnete_coll_gather_allM_nb(team, &dst, &src, nbytes,
				       flags | GASNETE_COLL_THREAD_LOCAL, sequence
				       GASNETE_THREAD_PASS);
  }
  #endif

  /* "Discover" in-segment flags if needed/possible */
  flags = gasnete_coll_segment_check(flags, 0, 0, dst, nbytes*gasneti_nodes,
				     0, 0, src, nbytes);

  /* XXX: need more implementations to choose from here */
  if(nbytes <= gasnete_coll_p2p_eager_min) {
    return gasnete_coll_gall_Dissem(team, dst, src, nbytes, flags, sequence GASNETE_THREAD_PASS); 
  } else {
    return gasnete_coll_gall_Gath(team, dst, src, nbytes, flags, sequence GASNETE_THREAD_PASS); 
  }
}

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
      if (!gasnete_coll_threads_ready2(op, args->dstlist, args->srclist GASNETE_THREAD_PASS) ||
	  !gasnete_coll_generic_insync(data)) {
	break;
      }
      data->state = 1;

    case 1:	/* Initiate data movement */
      if (!GASNETE_COLL_MAY_INIT_FOR(op)) break;
      {
	gasnet_coll_handle_t *h;
        int flags = GASNETE_COLL_FORWARD_FLAGS(op->flags);
	gasnet_team_handle_t team = op->team;
	void * const *srclist = args->srclist;
	size_t nbytes = args->nbytes;
	gasnet_image_t i;

	/* XXX: freelist ? */
	h = gasneti_malloc(gasnete_coll_total_images * sizeof(gasnet_coll_handle_t));
	data->private_data = h;

	if (op->flags & GASNET_COLL_SINGLE) {
	  void * const *p = args->dstlist;
          for (i = 0; i < gasnete_coll_total_images; ++i, ++h, ++p) {
            *h = gasnete_coll_gatherM_nb(team, i, *p, srclist, nbytes,
					 flags, op->sequence+i+1 GASNETE_THREAD_PASS);
            gasnete_coll_save_coll_handle(h GASNETE_THREAD_PASS);
          }
        } else {
          #if PLATFORM_COMPILER_SUN_C && PLATFORM_COMPILER_VERSION_GE(5,7,0)
	    /* Supress a harmless (and incorrect) warning - a known Sun C bug: 
	     *   6344975 cc reports spurious "dead part of constant expression is nonconstant" warnings.
	     * The warning here is spurious - it would be appropriate for a *static* initializer.
	     */
	    #pragma error_messages(off, E_DEAD_NONCONST)
	  #endif
	  void * const *p = &GASNETE_COLL_MY_1ST_IMAGE(args->dstlist,GASNET_COLL_LOCAL);
          #if PLATFORM_COMPILER_SUN_C && PLATFORM_COMPILER_VERSION_GE(5,7,0)
	    #pragma error_messages(default, E_DEAD_NONCONST)
	  #endif

          for (i = 0; i < gasnete_coll_total_images; ++i, ++h) {
            *h = gasnete_coll_gatherM_nb(team, i, *p, srclist, nbytes,
					 flags, op->sequence+i+1 GASNETE_THREAD_PASS);
            gasnete_coll_save_coll_handle(h GASNETE_THREAD_PASS);
	    if (gasnete_coll_image_is_local(i)) ++p;
          }
	}
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
			size_t nbytes, int flags, uint32_t sequence
			GASNETE_THREAD_FARG)
{
  int options = GASNETE_COLL_GENERIC_OPT_INSYNC_IF (!(flags & GASNET_COLL_IN_NOSYNC)) |
		GASNETE_COLL_GENERIC_OPT_OUTSYNC_IF(!(flags & GASNET_COLL_OUT_NOSYNC));
  gasneti_assert(!(flags & GASNETE_COLL_SUBORDINATE));

  return gasnete_coll_generic_gather_allM_nb(team, dstlist, srclist, nbytes, flags,
					     &gasnete_coll_pf_gallM_Gath, options,
					     NULL, gasnete_coll_total_images GASNETE_THREAD_PASS);
}

extern gasnet_coll_handle_t
gasnete_coll_generic_gather_allM_nb(gasnet_team_handle_t team,
				    void * const dstlist[], void * const srclist[],
				    size_t nbytes, int flags,
				    gasnete_coll_poll_fn poll_fn, int options,
				    void *private_data, uint32_t sequence
				    GASNETE_THREAD_FARG) {
  gasnet_coll_handle_t result;
  gasnete_coll_scratch_req_t *scratch_req=NULL;
  uint32_t *out_sizes;
  int i;
  int first=0;
  gasnete_coll_dissem_info_t *dissem = gasnete_coll_fetch_dissemination(2,team);
  

  
  gasnete_coll_threads_lock(flags GASNETE_THREAD_PASS);
  
  if(gasnete_coll_threads_first(GASNETE_THREAD_PASS_ALONE)) {
    first = 1;
    if(options & (GASNETE_COLL_USE_SCRATCH)) {
      /*fill out a scratch request form*/	
      scratch_req = (gasnete_coll_scratch_req_t*) gasneti_calloc(1,sizeof(gasnete_coll_scratch_req_t));
      scratch_req->op_type = GASNETE_COLL_DISSEM_OP;
      scratch_req->team = team;
      scratch_req->tree_dir = GASNETE_COLL_UP_TREE;
      scratch_req->incoming_size = 
        nbytes*gasnete_coll_total_images;
      scratch_req->num_out_peers = scratch_req->num_in_peers = GASNETE_COLL_DISSEM_GET_PEER_COUNT(dissem);
      scratch_req->out_peers = GASNETE_COLL_DISSEM_GET_BEHIND_PEERS(dissem);
      scratch_req->in_peers = GASNETE_COLL_DISSEM_GET_FRONT_PEERS(dissem);
      scratch_req->out_sizes = (uint64_t*) gasneti_malloc(sizeof(uint64_t)*1);
      scratch_req->out_sizes[0] = scratch_req->incoming_size;
    }  
  }
  #if GASNET_PAR
  if (flags & GASNETE_COLL_THREAD_LOCAL) {
    gasnete_coll_threaddata_t *td = GASNETE_COLL_MYTHREAD_NOALLOC;
    gasnete_coll_generic_data_t *data;
    if_pt (first) {
      data = gasnete_coll_generic_alloc(GASNETE_THREAD_PASS_ALONE);
      GASNETE_COLL_GENERIC_SET_TAG(data, gather_allM);
      data->threads.data = gasneti_calloc(2 * gasnete_coll_my_images, sizeof(void *));
      data->args.gather_allM.srclist    = (void**)data->threads.data;
      data->args.gather_allM.dstlist    = (void**)data->threads.data + gasnete_coll_my_images;
      data->args.gather_allM.nbytes     = nbytes;
      data->options = options;
      data->private_data = private_data; data->tree_info=NULL;
      data->dissem_info = dissem;
      result = gasnete_coll_op_generic_init_with_scratch(team, flags, data, poll_fn, sequence, scratch_req GASNETE_THREAD_PASS);
    } else {
      result = gasnete_coll_threads_get_handle_and_data(&data GASNETE_THREAD_PASS);
    }
    gasneti_assert(*srclist != NULL);
    data->args.gather_allM.srclist[td->my_local_image] = *srclist;
    gasneti_assert(*dstlist != NULL);
    data->args.gather_allM.dstlist[td->my_local_image] = *dstlist;
  } else
  #endif

  if_pt (first) {
    gasnete_coll_generic_data_t *data = gasnete_coll_generic_alloc(GASNETE_THREAD_PASS_ALONE);
    GASNETE_COLL_GENERIC_SET_TAG(data, gather_allM);
    data->args.gather_allM.dstlist = (void**)dstlist;
    data->args.gather_allM.srclist = (void**)srclist;
    data->args.gather_allM.nbytes  = nbytes;
    data->options = options;
    data->private_data = private_data; data->tree_info=NULL;
    data->dissem_info = dissem;
    result = gasnete_coll_op_generic_init_with_scratch(team, flags, data, poll_fn, sequence, scratch_req GASNETE_THREAD_PASS);
  } else {
    result = gasnete_coll_threads_get_handle(GASNETE_THREAD_PASS_ALONE);
  }

  gasnete_coll_threads_unlock(GASNETE_THREAD_PASS_ALONE);
  return result;
}

extern gasnet_coll_handle_t
gasnete_coll_gather_allM_nb_default(gasnet_team_handle_t team,
				    void * const dstlist[], void * const srclist[],
				    size_t nbytes, int flags, uint32_t sequence
				    GASNETE_THREAD_FARG)
{
  if_pf (!gasnete_coll_opt_enabled) {
    return gasnete_coll_gather_allM_nb_default_OLD(team, dstlist, srclist, nbytes,
				                   flags, sequence GASNETE_THREAD_PASS);
  }

  #if GASNET_SEQ
  /* Exactly one thread-local addr per list - forward to gall_nb() */
  if (flags & GASNET_COLL_LOCAL) {
    return gasnete_coll_gather_all_nb(team, dstlist[0], srclist[0], nbytes,
				      flags, sequence GASNETE_THREAD_PASS);
  }
  #endif

  /* "Discover" in-segment flags if needed/possible */
  flags = gasnete_coll_segment_checkM(flags, 0, 0, dstlist, nbytes*gasneti_nodes,
    					 0, 0, srclist, nbytes);

  if(nbytes > gasnete_coll_p2p_eager_min || !gasnete_coll_fixed_image_count || 1) {
    return gasnete_coll_gallM_Gath(team, dstlist, srclist, nbytes, flags, sequence GASNETE_THREAD_PASS);
  } else { 
    return gasnete_coll_gallM_Dissem(team, dstlist, srclist, nbytes, flags, sequence GASNETE_THREAD_PASS);
  }
}

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
      if (!gasnete_coll_generic_all_threads(data) ||
	  !gasnete_coll_generic_insync(data)) {
	break;
      }
      data->state = 1;

    case 1:	/* Initiate data movement */
      if (!GASNETE_COLL_MAY_INIT_FOR(op)) break;
      {
	gasnet_coll_handle_t *h;
        int flags = GASNETE_COLL_FORWARD_FLAGS(op->flags);
	gasnet_team_handle_t team = op->team;
	void *dst = args->dst;
	uintptr_t src_addr = (uintptr_t)args->src;
	size_t nbytes = args->nbytes;
        gasnet_image_t i;

	/* XXX: freelist ? */
	h = gasneti_malloc(gasnete_coll_total_images * sizeof(gasnet_coll_handle_t));
	data->private_data = h;

        for (i = 0; i < gasnete_coll_total_images; ++i, ++h, src_addr += nbytes) {
          *h = gasnete_coll_gather_nb(team, i, dst, (void *)src_addr, nbytes,
				      flags, op->sequence+i+1 GASNETE_THREAD_PASS);
          gasnete_coll_save_coll_handle(h GASNETE_THREAD_PASS);
        }
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
gasnete_coll_exchg_Gath(gasnet_team_handle_t team,
			void *dst, void *src,
			size_t nbytes, int flags, uint32_t sequence
			GASNETE_THREAD_FARG)
{
  int options = GASNETE_COLL_GENERIC_OPT_INSYNC_IF (!(flags & GASNET_COLL_IN_NOSYNC)) |
		GASNETE_COLL_GENERIC_OPT_OUTSYNC_IF(!(flags & GASNET_COLL_OUT_NOSYNC));
  gasneti_assert(!(flags & GASNETE_COLL_SUBORDINATE));

  return gasnete_coll_generic_exchange_nb(team, dst, src, nbytes, flags,
					  &gasnete_coll_pf_exchg_Gath, options,
					  NULL, NULL, gasnete_coll_total_images GASNETE_THREAD_PASS);
}

extern gasnet_coll_handle_t
gasnete_coll_generic_exchange_nb(gasnet_team_handle_t team,
				 void *dst, void *src,
				 size_t nbytes, int flags,
				 gasnete_coll_poll_fn poll_fn, int options,
				 void *private_data, gasnete_coll_dissem_info_t* dissem, uint32_t sequence
				 GASNETE_THREAD_FARG) {
  gasnet_coll_handle_t result; int i;
  gasnete_coll_scratch_req_t *scratch_req=NULL;
  if(options & GASNETE_COLL_USE_SCRATCH) {
    /*fill out a scratch request form*/	
      scratch_req = (gasnete_coll_scratch_req_t*) gasneti_calloc(1,sizeof(gasnete_coll_scratch_req_t));
      scratch_req->op_type = GASNETE_COLL_DISSEM_OP;
      scratch_req->team = team;
      scratch_req->tree_dir = GASNETE_COLL_DOWN_TREE;
      scratch_req->incoming_size = 
        gasnete_coll_my_images*nbytes*gasnete_coll_total_images+
        ((gasnete_coll_my_images*gasnete_coll_my_images*nbytes)*dissem->max_dissem_blocks
         *2*(dissem->dissemination_radix-1));   
//      scratch_req->incoming_size = nbytes*team->total_ranks+((nbytes)*dissem->max_dissem_blocks)*((dissem->dissemination_phases+1)*(dissem->dissemination_radix-1));
      scratch_req->num_out_peers = scratch_req->num_in_peers = GASNETE_COLL_DISSEM_GET_PEER_COUNT(dissem);
      scratch_req->out_peers = GASNETE_COLL_DISSEM_GET_FRONT_PEERS(dissem);
      scratch_req->in_peers = GASNETE_COLL_DISSEM_GET_BEHIND_PEERS(dissem);
      scratch_req->out_sizes = (uint64_t*) gasneti_malloc(sizeof(uint64_t)*1);
      scratch_req->out_sizes[0] = scratch_req->incoming_size;
  }
  
  gasnete_coll_threads_lock(flags GASNETE_THREAD_PASS);
  if_pt (gasnete_coll_threads_first(GASNETE_THREAD_PASS_ALONE)) {
    gasnete_coll_generic_data_t *data = gasnete_coll_generic_alloc(GASNETE_THREAD_PASS_ALONE);
    GASNETE_COLL_GENERIC_SET_TAG(data, exchange);
    data->args.exchange.dst     = dst;
    data->args.exchange.src     = src;
    data->args.exchange.nbytes  = nbytes;
    data->options = options;
    data->private_data = private_data; data->tree_info=NULL;
	data->dissem_info = dissem;
    result = gasnete_coll_op_generic_init_with_scratch(team, flags, data, poll_fn, sequence, scratch_req GASNETE_THREAD_PASS);
  } else {
    result = gasnete_coll_threads_get_handle(GASNETE_THREAD_PASS_ALONE);
  }
  gasnete_coll_threads_unlock(GASNETE_THREAD_PASS_ALONE);


  return result;
}

extern gasnet_coll_handle_t
gasnete_coll_exchange_nb_default(gasnet_team_handle_t team,
				 void *dst, void *src,
				 size_t nbytes, int flags, uint32_t sequence
				 GASNETE_THREAD_FARG)
{
  if_pf (!gasnete_coll_opt_enabled) {
    return gasnete_coll_exchange_nb_default_OLD(team, dst, src, nbytes,
				                flags, sequence GASNETE_THREAD_PASS);
  }

  #if GASNET_PAR
  /* Thread-local addr(s) - forward to exchgM_nb() */
  if (flags & GASNET_COLL_LOCAL) {
    return gasnete_coll_exchangeM_nb(team, &dst, &src, nbytes,
				     flags | GASNETE_COLL_THREAD_LOCAL, sequence
				     GASNETE_THREAD_PASS);
  }
  #endif

  /* "Discover" in-segment flags if needed/possible */
  flags = gasnete_coll_segment_check(flags, 0, 0, dst, nbytes*gasneti_nodes,
				     0, 0, src, nbytes*gasneti_nodes);

  /* XXX: need more implementations to choose from here */
  if(nbytes > gasnete_coll_p2p_eager_min/*XXX: should we add a check to see if it fits in scratch*/) {
    return gasnete_coll_exchg_Gath(team, dst, src, nbytes, flags, sequence GASNETE_THREAD_PASS);
  } else {
    return gasnete_coll_exchg_Dissem(team, dst, src, nbytes, flags, sequence GASNETE_THREAD_PASS);
  }
}

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
      if (!gasnete_coll_threads_ready2(op, args->dstlist, args->srclist GASNETE_THREAD_PASS) ||
	  !gasnete_coll_generic_insync(data)) {
	break;
      }
      data->state = 1;

    case 1:	/* Initiate data movement */
      if (!GASNETE_COLL_MAY_INIT_FOR(op)) break;
      {
	gasnet_coll_handle_t *h;
        int flags = GASNETE_COLL_FORWARD_FLAGS(op->flags);
	gasnet_team_handle_t team = op->team;
	void **srclist;
	void **p;
	void * const *q;
	size_t nbytes = args->nbytes;
        gasnet_image_t i, j;

	if (op->flags & GASNET_COLL_SINGLE) {
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

	  p = srclist;
	  q = args->dstlist;
          for (i = 0; i < gasnete_coll_total_images; ++i, ++h, ++q, p += gasnete_coll_total_images) {
            *h = gasnete_coll_gatherM_nb(team, i, *q, p, nbytes,
					 flags, op->sequence+i+1 GASNETE_THREAD_PASS);
	    gasnete_coll_save_coll_handle(h GASNETE_THREAD_PASS);
          }
	} else {
	  data->private_data = gasneti_malloc(gasnete_coll_total_images * sizeof(gasnet_coll_handle_t) +
				       gasnete_coll_total_images * gasnete_coll_my_images * sizeof(void *));
	  h = (gasnet_coll_handle_t *)data->private_data;
	  srclist = gasnete_coll_scale_ptr(data->private_data, sizeof(gasnet_coll_handle_t), gasnete_coll_total_images);

	  p = srclist;
	  for (i = 0; i < gasnete_coll_total_images; ++i) {
	    q = args->srclist;
	    for (j = 0; j < gasnete_coll_my_images; ++j, ++p, ++q) {
	      *p = gasnete_coll_scale_ptr(*q, i, nbytes);
	    }
	  }

	  p = srclist;
	  q = args->dstlist;
          for (i = 0; i < gasnete_coll_total_images; ++i, ++h, p += gasnete_coll_my_images) {
            *h = gasnete_coll_gatherM_nb(team, i, *q, p, nbytes,
					 flags, op->sequence+i+1 GASNETE_THREAD_PASS);
	    gasnete_coll_save_coll_handle(h GASNETE_THREAD_PASS);
	    if (gasnete_coll_image_is_local(i)) ++q;
          }
        }
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
			 size_t nbytes, int flags, uint32_t sequence
			 GASNETE_THREAD_FARG)
{
  int options = GASNETE_COLL_GENERIC_OPT_INSYNC_IF (!(flags & GASNET_COLL_IN_NOSYNC)) |
		GASNETE_COLL_GENERIC_OPT_OUTSYNC_IF(!(flags & GASNET_COLL_OUT_NOSYNC));
  gasneti_assert(!(flags & GASNETE_COLL_SUBORDINATE));

  return gasnete_coll_generic_exchangeM_nb(team, dstlist, srclist, nbytes, flags,
					   &gasnete_coll_pf_exchgM_Gath, options,
					   NULL, NULL, gasnete_coll_total_images GASNETE_THREAD_PASS);
}

extern gasnet_coll_handle_t
gasnete_coll_generic_exchangeM_nb(gasnet_team_handle_t team,
				  void * const dstlist[], void * const srclist[],
				  size_t nbytes, int flags,
				  gasnete_coll_poll_fn poll_fn, int options,
				  void *private_data, gasnete_coll_dissem_info_t *dissem, uint32_t sequence
				  GASNETE_THREAD_FARG) {
  gasnet_coll_handle_t result;
  gasnete_coll_scratch_req_t *scratch_req=NULL;
  int first = 0;
  
  gasnete_coll_threads_lock(flags GASNETE_THREAD_PASS);
  if(gasnete_coll_threads_first(GASNETE_THREAD_PASS_ALONE)) {
    first = 1;
    if(options & GASNETE_COLL_USE_SCRATCH) {
      /*fill out a scratch request form*/	
      scratch_req = (gasnete_coll_scratch_req_t*) gasneti_calloc(1,sizeof(gasnete_coll_scratch_req_t));
      scratch_req->op_type = GASNETE_COLL_DISSEM_OP;
      scratch_req->tree_dir = GASNETE_COLL_DOWN_TREE;
      scratch_req->team = team;
//      scratch_req->incoming_size = gasnete_coll_my_images*nbytes*team->total_ranks+((gasnete_coll_my_images*gasnete_coll_my_images*nbytes)*dissem->max_dissem_blocks)*((dissem->dissemination_phases+1)*(dissem->dissemination_radix-1));
      scratch_req->incoming_size = 
        gasnete_coll_my_images*nbytes*gasnete_coll_total_images+
        ((gasnete_coll_my_images*gasnete_coll_my_images*nbytes)*dissem->max_dissem_blocks
        *2*(dissem->dissemination_radix-1));     
      scratch_req->num_out_peers = scratch_req->num_in_peers = GASNETE_COLL_DISSEM_GET_PEER_COUNT(dissem);
      scratch_req->out_peers = GASNETE_COLL_DISSEM_GET_FRONT_PEERS(dissem);
      scratch_req->in_peers = GASNETE_COLL_DISSEM_GET_BEHIND_PEERS(dissem);
      scratch_req->out_sizes = (uint64_t*) gasneti_malloc(sizeof(uint64_t)*1);
      scratch_req->out_sizes[0] = scratch_req->incoming_size;
    }
  }
  
  #if GASNET_PAR
  if (flags & GASNETE_COLL_THREAD_LOCAL) {
    gasnete_coll_threaddata_t *td = GASNETE_COLL_MYTHREAD_NOALLOC;
    gasnete_coll_generic_data_t *data;
    if_pt (first) {
      data = gasnete_coll_generic_alloc(GASNETE_THREAD_PASS_ALONE);
      GASNETE_COLL_GENERIC_SET_TAG(data, exchangeM);
      data->threads.data = gasneti_calloc(2 * gasnete_coll_my_images, sizeof(void *));
      data->args.exchangeM.srclist    = (void**)data->threads.data;
      data->args.exchangeM.dstlist    = (void**)data->threads.data + gasnete_coll_my_images;
      data->args.exchangeM.nbytes     = nbytes;
      data->options = options;
      data->private_data = private_data; data->tree_info=NULL;
      data->dissem_info = dissem;
      result = gasnete_coll_op_generic_init_with_scratch(team, flags, data, poll_fn, sequence, scratch_req GASNETE_THREAD_PASS);
    } else {
      result = gasnete_coll_threads_get_handle_and_data(&data GASNETE_THREAD_PASS);
    }
    gasneti_assert(*srclist != NULL);
    data->args.exchangeM.srclist[td->my_local_image] = *srclist;
    gasneti_assert(*dstlist != NULL);
    data->args.exchangeM.dstlist[td->my_local_image] = *dstlist;
  } else
  #endif

  if_pt (first) {
    gasnete_coll_generic_data_t *data = gasnete_coll_generic_alloc(GASNETE_THREAD_PASS_ALONE);
    GASNETE_COLL_GENERIC_SET_TAG(data, exchangeM);
    data->args.exchangeM.dstlist = (void**)dstlist;
    data->args.exchangeM.srclist = (void**)srclist;
    data->args.exchangeM.nbytes  = nbytes;
    data->options = options;
    data->private_data = private_data; data->tree_info=NULL;
    data->dissem_info = dissem;
    result = gasnete_coll_op_generic_init_with_scratch(team, flags, data, poll_fn, sequence, scratch_req GASNETE_THREAD_PASS);
  } else {
    result = gasnete_coll_threads_get_handle(GASNETE_THREAD_PASS_ALONE);
  }

  gasnete_coll_threads_unlock(GASNETE_THREAD_PASS_ALONE);
  return result;
}

extern gasnet_coll_handle_t
gasnete_coll_exchangeM_nb_default(gasnet_team_handle_t team,
				  void * const dstlist[], void * const srclist[],
				  size_t nbytes, int flags, uint32_t sequence
				  GASNETE_THREAD_FARG)
{
  if_pf (!gasnete_coll_opt_enabled) {
    return gasnete_coll_exchangeM_nb_default_OLD(team, dstlist, srclist, nbytes,
				                 flags, sequence GASNETE_THREAD_PASS);
  }

  #if GASNET_SEQ
  /* Exactly one thread-local addr per list - forward to exchg_nb() */
  if (flags & GASNET_COLL_LOCAL) {
    return gasnete_coll_exchange_nb(team, dstlist[0], srclist[0], nbytes,
				    flags, sequence GASNETE_THREAD_PASS);
  }
  #endif

  /* "Discover" in-segment flags if needed/possible */
  flags = gasnete_coll_segment_checkM(flags, 0, 0, dstlist, nbytes*gasneti_nodes,
				      0, 0, srclist, nbytes*gasneti_nodes);

  /* XXX: need more implementations to choose from here */
  if(nbytes > gasnete_coll_p2p_eager_min || !gasnete_coll_fixed_image_count/*XXX: should we add a check to see if it fits in scratch*/) {
    return gasnete_coll_exchgM_Gath(team, dstlist, srclist, nbytes, flags, sequence GASNETE_THREAD_PASS);
  } else {
    return gasnete_coll_exchgM_Dissem(team, dstlist, srclist, nbytes, flags, sequence GASNETE_THREAD_PASS);
  }
  
}

/*---------------------------------------------------------------------------------*/

extern gasnet_coll_handle_t
gasnete_coll_reduce_nb_default(gasnet_team_handle_t team,
			       gasnet_image_t dstimage, void *dst,
			       void *src, size_t src_blksz, size_t src_offset,
			       size_t elem_size, size_t elem_count,
			       gasnet_coll_fn_handle_t func, int func_arg,
			       int flags GASNETE_THREAD_FARG)
{
  gasneti_fatalerror("%s UNIMPLEMENTED", GASNETI_CURRENT_FUNCTION);
  return GASNET_COLL_INVALID_HANDLE;
}

/*---------------------------------------------------------------------------------*/

extern gasnet_coll_handle_t
gasnete_coll_reduceM_nb_default(gasnet_team_handle_t team,
				gasnet_image_t dstimage, void *dst,
				void * const srclist[], size_t src_blksz, size_t src_offset,
				size_t elem_size, size_t elem_count,
				gasnet_coll_fn_handle_t func, int func_arg,
				int flags GASNETE_THREAD_FARG)
{
  gasneti_fatalerror("%s UNIMPLEMENTED", GASNETI_CURRENT_FUNCTION);
  return GASNET_COLL_INVALID_HANDLE;
}

/*---------------------------------------------------------------------------------*/

extern gasnet_coll_handle_t
gasnete_coll_scan_nb_default(gasnet_team_handle_t team,
			     void *dst, size_t dst_blksz, size_t dst_offset,
			     void *src, size_t src_blksz, size_t src_offset,
			     size_t elem_size, size_t elem_count,
			     gasnet_coll_fn_handle_t func, int func_arg,
			     int flags GASNETE_THREAD_FARG)
{
  gasneti_fatalerror("%s UNIMPLEMENTED", GASNETI_CURRENT_FUNCTION);
  return GASNET_COLL_INVALID_HANDLE;
}

/*---------------------------------------------------------------------------------*/

extern gasnet_coll_handle_t
gasnete_coll_scanM_nb_default(gasnet_team_handle_t team,
			      void * const dstlist[], size_t dst_blksz, size_t dst_offset,
			      void * const srclist[], size_t src_blksz, size_t src_offset,
			      size_t elem_size, size_t elem_count,
			      gasnet_coll_fn_handle_t func, int func_arg,
			      int flags GASNETE_THREAD_FARG)
{
  gasneti_fatalerror("%s UNIMPLEMENTED", GASNETI_CURRENT_FUNCTION);
  return GASNET_COLL_INVALID_HANDLE;
}

/*---------------------------------------------------------------------------------*/

#if GASNET_DEBUG
void gasnete_coll_stat_(GASNETE_THREAD_FARG_ALONE) {
  gasnete_coll_threaddata_t *td = GASNETE_COLL_MYTHREAD_NOALLOC;
  int used = td->handles.used;
  gasnete_coll_op_t *op;
  fprintf(stderr, "%d:%d> %d handles used\n", (int)gasneti_mynode, (int)td->my_local_image, used);

  if (used) {
    gasnete_coll_local_handle_t *curr = gasnete_coll_local_handles(td, 0);
    int i;

    for (i = 0; i < used; ++i, ++curr) {
      uintptr_t addr = curr->addr;
      fprintf(stderr, "%sHANDLE %p\n", (addr&1)?"COLL_":"", (void*)(curr->u.handle));
    }
  }

  /* gasneti_mutex_lock(&gasnete_coll_active_lock); */
  op = gasnete_coll_active_first();
  while (op) {
    gasnete_coll_generic_data_t *data = op->data;
    fprintf(stderr, "OP: %p in state %d\n", op, data->state);
    op = gasnete_coll_active_next(op);
  }
  /* gasneti_mutex_unlock(&gasnete_coll_active_lock); */
}

extern void gasnete_coll_stat(void) { gasnete_coll_stat_(GASNETE_THREAD_GET_ALONE); }
#endif

/*---------------------------------------------------------------------------------*/
/* OLD dispatchers: */

extern gasnet_coll_handle_t
gasnete_coll_broadcast_nb_default_OLD(gasnet_team_handle_t team,
				  void *dst,
				  gasnet_image_t srcimage, void *src,
				  size_t nbytes, int flags, uint32_t sequence
                                  GASNETE_THREAD_FARG)
{
  const size_t eager_limit = gasnete_coll_p2p_eager_min;

  #if GASNET_PAR
  /* Thread-local addr(s) - forward to bcastM_nb() */
  if (flags & GASNET_COLL_LOCAL) {
    return gasnete_coll_broadcastM_nb(team, &dst, srcimage, src, nbytes,
				      flags | GASNETE_COLL_THREAD_LOCAL, sequence
                                      GASNETE_THREAD_PASS);
  }
  #endif

  /* "Discover" in-segment flags if needed/possible */
  flags = gasnete_coll_segment_check(flags, 0, 0, dst, nbytes, 1, srcimage, src, nbytes);

  /* Choose algorithm based on arguments */
  if ((nbytes <= eager_limit) &&
      (flags & (GASNET_COLL_IN_MYSYNC | GASNET_COLL_OUT_MYSYNC | GASNET_COLL_LOCAL))) {
    /* Small enough for Eager, which will eliminate any barriers for *_MYSYNC and
     * the need for passing addresses for _LOCAL
     * Eager is totally AM-based and thus safe regardless of *_IN_SEGMENT
     */
    return gasnete_coll_bcast_Eager(team, dst, srcimage, src, nbytes, flags, sequence GASNETE_THREAD_PASS);
  } else if (flags & GASNET_COLL_SRC_IN_SEGMENT) {
    if (flags & (GASNET_COLL_IN_MYSYNC | GASNET_COLL_OUT_MYSYNC | GASNET_COLL_LOCAL)) {
      /* We can use Rendezvous+Get to eliminate any barriers for *_MYSYNC.
       * The Rendezvous is needed for _LOCAL.
       */
      return gasnete_coll_bcast_RVGet(team, dst, srcimage, src, nbytes, flags, sequence GASNETE_THREAD_PASS);
    } else {
      return gasnete_coll_bcast_Get(team, dst, srcimage, src, nbytes, flags, sequence GASNETE_THREAD_PASS);
    }
  } else if (flags & GASNET_COLL_DST_IN_SEGMENT) {
    if (flags & GASNET_COLL_SINGLE) {
      /* We use a Put-based algorithm w/ full barriers for *_{MY,ALL}SYNC */
      return gasnete_coll_bcast_Put(team, dst, srcimage, src, nbytes, flags, sequence GASNETE_THREAD_PASS);
    } else {
      /* XXX: could do better w/ RVPut since dst is writtable */
      return gasnete_coll_bcast_RVous(team, dst, srcimage, src, nbytes, flags, sequence GASNETE_THREAD_PASS);
    }
  } else {
    /* If we reach here then neither src nor dst is in-segment */
    return gasnete_coll_bcast_RVous(team, dst, srcimage, src, nbytes, flags, sequence GASNETE_THREAD_PASS);
  }
}

extern gasnet_coll_handle_t
gasnete_coll_broadcastM_nb_default_OLD(gasnet_team_handle_t team,
				   void * const dstlist[],
				   gasnet_image_t srcimage, void *src,
				   size_t nbytes, int flags, uint32_t sequence
                                   GASNETE_THREAD_FARG)
{
  const size_t eager_limit = gasnete_coll_p2p_eager_min;

  #if GASNET_SEQ
  /* Exactly one thread-local addr - forward to bcast_nb() */
  if (flags & GASNET_COLL_LOCAL) {
    return gasnete_coll_broadcast_nb(team, dstlist[0], srcimage, src, nbytes,
				     flags, sequence GASNETE_THREAD_PASS);
  }
  #endif


  /* "Discover" in-segment flags if needed/possible */
  flags = gasnete_coll_segment_checkM(flags, 0, 0, dstlist, nbytes, 1, srcimage, src, nbytes);

  /* Choose algorithm based on arguments */
  if ((nbytes <= eager_limit) &&
      (flags & (GASNET_COLL_IN_MYSYNC | GASNET_COLL_OUT_MYSYNC | GASNET_COLL_LOCAL))) {
    /* Small enough for Eager, which will eliminate any barriers for *_MYSYNC and
     * the need for passing addresses for _LOCAL
     * Eager is totally AM-based and thus safe regardless of *_IN_SEGMENT
     */
    return gasnete_coll_bcastM_Eager(team, dstlist, srcimage, src, nbytes, flags, sequence GASNETE_THREAD_PASS);
  } else if (flags & GASNET_COLL_SRC_IN_SEGMENT) {
    if (flags & (GASNET_COLL_IN_MYSYNC | GASNET_COLL_OUT_MYSYNC | GASNET_COLL_LOCAL)) {
      /* We can use Rendezvous+Get to eliminate any barriers for *_MYSYNC.
       * The Rendezvous is needed for _LOCAL.
       */
      return gasnete_coll_bcastM_RVGet(team, dstlist, srcimage, src, nbytes, flags, sequence GASNETE_THREAD_PASS);
    } else {
      return gasnete_coll_bcastM_Get(team, dstlist, srcimage, src, nbytes, flags, sequence GASNETE_THREAD_PASS);
    }
  } else if (flags & GASNET_COLL_DST_IN_SEGMENT) {
    if (flags & GASNET_COLL_SINGLE) {
      /* We use a Put-based algorithm w/ full barriers for *_{MY,ALL}SYNC */
      return gasnete_coll_bcastM_Put(team, dstlist, srcimage, src, nbytes, flags, sequence GASNETE_THREAD_PASS);
    } else {
      /* XXX: could do better w/ RVPut since dst is writtable */
      return gasnete_coll_bcastM_RVous(team, dstlist, srcimage, src, nbytes, flags, sequence GASNETE_THREAD_PASS);
    }
  } else {
    /* If we reach here then neither src nor dst is in-segment */
    return gasnete_coll_bcastM_RVous(team, dstlist, srcimage, src, nbytes, flags, sequence GASNETE_THREAD_PASS);
  }
}

extern gasnet_coll_handle_t
gasnete_coll_scatter_nb_default_OLD(gasnet_team_handle_t team,
				void *dst,
				gasnet_image_t srcimage, void *src,
				size_t nbytes, int flags, uint32_t sequence
                                GASNETE_THREAD_FARG)
{
  const size_t eager_limit = gasnete_coll_p2p_eager_min;

  #if GASNET_PAR
  /* Thread-local addr(s) - forward to scatM_nb() */
  if (flags & GASNET_COLL_LOCAL) {
    return gasnete_coll_scatterM_nb(team, &dst, srcimage, src, nbytes,
				    flags | GASNETE_COLL_THREAD_LOCAL, sequence
                                    GASNETE_THREAD_PASS);
  }
  #endif

  /* "Discover" in-segment flags if needed/possible */
  flags = gasnete_coll_segment_check(flags, 0, 0, dst, nbytes,
				     1, srcimage, src, nbytes*gasneti_nodes);

  /* Choose algorithm based on arguments */
  if ((flags & GASNET_COLL_DST_IN_SEGMENT) && (flags & GASNET_COLL_SRC_IN_SEGMENT)) {
    /* Both ends are in-segment */
    if ((flags & GASNET_COLL_IN_MYSYNC) || (flags & GASNET_COLL_LOCAL)) {
      if (nbytes <= eager_limit) {
        return gasnete_coll_scat_Eager(team, dst, srcimage, src, nbytes, flags, sequence GASNETE_THREAD_PASS);
      } else {
        return gasnete_coll_scat_RVGet(team, dst, srcimage, src, nbytes, flags, sequence GASNETE_THREAD_PASS);
      }
    } else if ((flags & GASNET_COLL_OUT_MYSYNC) && (nbytes <= eager_limit)) {
      return gasnete_coll_scat_Eager(team, dst, srcimage, src, nbytes, flags, sequence GASNETE_THREAD_PASS);
    } else {
      return gasnete_coll_scat_Put(team, dst, srcimage, src, nbytes, flags, sequence GASNETE_THREAD_PASS);
    }
  } else if (nbytes <= eager_limit) {
    /* Small enough for Eager, which works for out-of-segment src and/or dst */
    return gasnete_coll_scat_Eager(team, dst, srcimage, src, nbytes, flags, sequence GASNETE_THREAD_PASS);
  } else if (flags & GASNET_COLL_SRC_IN_SEGMENT) {
    /* Only the source is in-segment (and too big for Eager) */
    if ((flags & GASNET_COLL_IN_NOSYNC) && (flags & GASNET_COLL_SINGLE)) {
      return gasnete_coll_scat_Get(team, dst, srcimage, src, nbytes, flags, sequence GASNETE_THREAD_PASS);
    } else {
      return gasnete_coll_scat_RVGet(team, dst, srcimage, src, nbytes, flags, sequence GASNETE_THREAD_PASS);
    }
  } else if (flags & GASNET_COLL_DST_IN_SEGMENT) {
    /* Only the destination is in-segment (and too big for Eager) */
    if (flags & GASNET_COLL_SINGLE) {
      return gasnete_coll_scat_Put(team, dst, srcimage, src, nbytes, flags, sequence GASNETE_THREAD_PASS);
    } else {
      /* XXX: could do better since DST is in-segment */
      return gasnete_coll_scat_RVous(team, dst, srcimage, src, nbytes, flags, sequence GASNETE_THREAD_PASS);
    }
  } else {
    return gasnete_coll_scat_RVous(team, dst, srcimage, src, nbytes, flags, sequence GASNETE_THREAD_PASS);
  }
}

extern gasnet_coll_handle_t
gasnete_coll_scatterM_nb_default_OLD(gasnet_team_handle_t team,
				 void * const dstlist[],
				 gasnet_image_t srcimage, void *src,
				 size_t nbytes, int flags, uint32_t sequence
                                 GASNETE_THREAD_FARG)
{
  const size_t eager_limit = gasnete_coll_p2p_eager_min;

  #if GASNET_SEQ
  /* Exactly one thread-local addr - forward to scat_nb() */
  if (flags & GASNET_COLL_LOCAL) {
    return gasnete_coll_scatter_nb(team, dstlist[0], srcimage, src, nbytes,
				   flags, sequence GASNETE_THREAD_PASS);
  }
  #endif

  /* "Discover" in-segment flags if needed/possible */
  flags = gasnete_coll_segment_checkM(flags, 0, 0, dstlist, nbytes,
				      1, srcimage, src, nbytes*gasneti_nodes);

  /* Choose algorithm based on arguments */
  if ((flags & GASNET_COLL_DST_IN_SEGMENT) && (flags & GASNET_COLL_SRC_IN_SEGMENT)) {
    /* Both ends are in-segment */
    if ((flags & GASNET_COLL_IN_MYSYNC) || (flags & GASNET_COLL_LOCAL)) {
      if (nbytes <= eager_limit) {
        return gasnete_coll_scatM_Eager(team, dstlist, srcimage, src, nbytes, flags, sequence GASNETE_THREAD_PASS);
      } else {
        return gasnete_coll_scatM_RVGet(team, dstlist, srcimage, src, nbytes, flags, sequence GASNETE_THREAD_PASS);
      }
    } else if ((flags & GASNET_COLL_OUT_MYSYNC) && (nbytes <= eager_limit)) {
      return gasnete_coll_scatM_Eager(team, dstlist, srcimage, src, nbytes, flags, sequence GASNETE_THREAD_PASS);
    } else {
      return gasnete_coll_scatM_Get(team, dstlist, srcimage, src, nbytes, flags, sequence GASNETE_THREAD_PASS);
    }
  } else if (nbytes <= eager_limit) {
    /* Small enough for Eager, which works for out-of-segment src and/or dst */
    return gasnete_coll_scatM_Eager(team, dstlist, srcimage, src, nbytes, flags, sequence GASNETE_THREAD_PASS);
  } else if (flags & GASNET_COLL_SRC_IN_SEGMENT) {
    /* Only the source is in-segment (and too big for Eager) */
    if ((flags & GASNET_COLL_IN_NOSYNC) && (flags & GASNET_COLL_SINGLE)) {
      return gasnete_coll_scatM_Get(team, dstlist, srcimage, src, nbytes, flags, sequence GASNETE_THREAD_PASS);
    } else {
      return gasnete_coll_scatM_RVGet(team, dstlist, srcimage, src, nbytes, flags, sequence GASNETE_THREAD_PASS);
    }
  } else if (flags & GASNET_COLL_DST_IN_SEGMENT) {
    /* Only the destination is in-segment (and too big for Eager) */
    if (flags & GASNET_COLL_SINGLE) {
      return gasnete_coll_scatM_Put(team, dstlist, srcimage, src, nbytes, flags, sequence GASNETE_THREAD_PASS);
    } else {
      /* Could do better since dst is in-segment */
      return gasnete_coll_scatM_RVous(team, dstlist, srcimage, src, nbytes, flags, sequence GASNETE_THREAD_PASS);
    }
  } else {
    return gasnete_coll_scatM_RVous(team, dstlist, srcimage, src, nbytes, flags, sequence GASNETE_THREAD_PASS);
  }
}

extern gasnet_coll_handle_t
gasnete_coll_gather_nb_default_OLD(gasnet_team_handle_t team,
			       gasnet_image_t dstimage, void *dst,
			       void *src,
			       size_t nbytes, int flags, uint32_t sequence
                               GASNETE_THREAD_FARG)
{
  const size_t eager_limit = gasnete_coll_p2p_eager_min;

  #if GASNET_PAR
  /* Thread-local addr(s) - forward to gathM_nb() */
  if (flags & GASNET_COLL_LOCAL) {
    return gasnete_coll_gatherM_nb(team, dstimage, dst, &src, nbytes,
				   flags | GASNETE_COLL_THREAD_LOCAL, sequence
                                   GASNETE_THREAD_PASS);
  }
  #endif

  /* "Discover" in-segment flags if needed/possible */
  flags = gasnete_coll_segment_check(flags, 1, dstimage, dst, nbytes*gasneti_nodes,
				     0, 0, src, nbytes);

  /* Choose algorithm based on arguments */
  if ((flags & GASNET_COLL_DST_IN_SEGMENT) && (flags & GASNET_COLL_SRC_IN_SEGMENT)) {
    /* Both ends are in-segment */
    if ((flags & GASNET_COLL_IN_MYSYNC) || (flags & GASNET_COLL_LOCAL)) {
      if (nbytes <= eager_limit) {
        return gasnete_coll_gath_Eager(team, dstimage, dst, src, nbytes, flags, sequence GASNETE_THREAD_PASS);
      } else {
        return gasnete_coll_gath_RVPut(team, dstimage, dst, src, nbytes, flags, sequence GASNETE_THREAD_PASS);
      }
    } else if ((flags & GASNET_COLL_OUT_MYSYNC) && (nbytes <= eager_limit)) {
      return gasnete_coll_gath_Eager(team, dstimage, dst, src, nbytes, flags, sequence GASNETE_THREAD_PASS);
    } else {
      return gasnete_coll_gath_Put(team, dstimage, dst, src, nbytes, flags, sequence GASNETE_THREAD_PASS);
    }
  } else if (nbytes <= eager_limit) {
    /* Small enough for Eager, which works for out-of-segment src and/or dst */
    return gasnete_coll_gath_Eager(team, dstimage, dst, src, nbytes, flags, sequence GASNETE_THREAD_PASS);
  } else if (flags & GASNET_COLL_DST_IN_SEGMENT) {
    /* Only the destination is in-segment (and too big for Eager) */
    if ((flags & GASNET_COLL_IN_NOSYNC) && (flags & GASNET_COLL_SINGLE)) {
      return gasnete_coll_gath_Put(team, dstimage, dst, src, nbytes, flags, sequence GASNETE_THREAD_PASS);
    } else {
      return gasnete_coll_gath_RVPut(team, dstimage, dst, src, nbytes, flags, sequence GASNETE_THREAD_PASS);
    }
  } else if (flags & GASNET_COLL_SRC_IN_SEGMENT) {
    /* Only the source is in-segment (and too big for Eager) */
    if (flags & GASNET_COLL_SINGLE) {
      return gasnete_coll_gath_Get(team, dstimage, dst, src, nbytes, flags, sequence GASNETE_THREAD_PASS);
    } else {
      /* XXX: could do better since src is in-segment */
      return gasnete_coll_gath_RVous(team, dstimage, dst, src, nbytes, flags, sequence GASNETE_THREAD_PASS);
    }
  } else {
    return gasnete_coll_gath_RVous(team, dstimage, dst, src, nbytes, flags, sequence GASNETE_THREAD_PASS);
  }
}

extern gasnet_coll_handle_t
gasnete_coll_gatherM_nb_default_OLD(gasnet_team_handle_t team,
				gasnet_image_t dstimage, void *dst,
				void * const srclist[],
				size_t nbytes, int flags, uint32_t sequence
                                GASNETE_THREAD_FARG)
{
  const size_t eager_limit = gasnete_coll_p2p_eager_min;

  #if GASNET_SEQ
  /* Exactly one thread-local addr - forward to gath_nb() */
  if (flags & GASNET_COLL_LOCAL) {
    return gasnete_coll_gather_nb(team, dstimage, dst, srclist[0], nbytes,
				  flags, sequence GASNETE_THREAD_PASS);
  }
  #endif

  /* "Discover" in-segment flags if needed/possible */
  flags = gasnete_coll_segment_checkM(flags, 1, dstimage, dst, nbytes*gasneti_nodes,
				      0, 0, srclist, nbytes);

  /* Choose algorithm based on arguments */
  if ((flags & GASNET_COLL_DST_IN_SEGMENT) && (flags & GASNET_COLL_SRC_IN_SEGMENT)) {
    /* Both ends are in-segment */
    if ((flags & GASNET_COLL_IN_MYSYNC) || (flags & GASNET_COLL_LOCAL)) {
      if (nbytes <= eager_limit) {
        return gasnete_coll_gathM_Eager(team, dstimage, dst, srclist, nbytes, flags, sequence GASNETE_THREAD_PASS);
      } else {
        return gasnete_coll_gathM_RVPut(team, dstimage, dst, srclist, nbytes, flags, sequence GASNETE_THREAD_PASS);
      }
    } else if ((flags & GASNET_COLL_OUT_MYSYNC) && (nbytes <= eager_limit)) {
      return gasnete_coll_gathM_Eager(team, dstimage, dst, srclist, nbytes, flags, sequence GASNETE_THREAD_PASS);
    } else {
      return gasnete_coll_gathM_Put(team, dstimage, dst, srclist, nbytes, flags, sequence GASNETE_THREAD_PASS);
    }
  } else if (nbytes <= eager_limit) {
    /* Small enough for Eager, which works for out-of-segment src and/or dst */
    return gasnete_coll_gathM_Eager(team, dstimage, dst, srclist, nbytes, flags, sequence GASNETE_THREAD_PASS);
  } else if (flags & GASNET_COLL_DST_IN_SEGMENT) {
    /* Only the destination is in-segment (and too big for Eager) */
    if ((flags & GASNET_COLL_IN_NOSYNC) && (flags & GASNET_COLL_SINGLE)) {
      return gasnete_coll_gathM_Put(team, dstimage, dst, srclist, nbytes, flags, sequence GASNETE_THREAD_PASS);
    } else {
      return gasnete_coll_gathM_RVPut(team, dstimage, dst, srclist, nbytes, flags, sequence GASNETE_THREAD_PASS);
    }
  } else if (flags & GASNET_COLL_SRC_IN_SEGMENT) {
    /* Only the source is in-segment (and too big for Eager) */
    if (flags & GASNET_COLL_SINGLE) {
      return gasnete_coll_gathM_Get(team, dstimage, dst, srclist, nbytes, flags, sequence GASNETE_THREAD_PASS);
    } else {
      /* XXX: could do better since src is in-segment */
      return gasnete_coll_gathM_RVous(team, dstimage, dst, srclist, nbytes, flags, sequence GASNETE_THREAD_PASS);
    }
  } else {
    return gasnete_coll_gathM_RVous(team, dstimage, dst, srclist, nbytes, flags, sequence GASNETE_THREAD_PASS);
  }
}

extern gasnet_coll_handle_t
gasnete_coll_gather_all_nb_default_OLD(gasnet_team_handle_t team,
				   void *dst, void *src,
				   size_t nbytes, int flags, uint32_t sequence
				   GASNETE_THREAD_FARG) {
  #if GASNET_PAR
  /* Thread-local addr(s) - forward to gallM_nb() */
  if (flags & GASNET_COLL_LOCAL) {
    return gasnete_coll_gather_allM_nb(team, &dst, &src, nbytes,
				       flags | GASNETE_COLL_THREAD_LOCAL, sequence
				       GASNETE_THREAD_PASS);
  }
  #endif

  /* "Discover" in-segment flags if needed/possible */
  flags = gasnete_coll_segment_check(flags, 0, 0, dst, nbytes*gasneti_nodes,
				     0, 0, src, nbytes);

  /* XXX: need more implementations to choose from here */
  return gasnete_coll_gall_Gath(team, dst, src, nbytes, flags, sequence GASNETE_THREAD_PASS);
}

extern gasnet_coll_handle_t
gasnete_coll_gather_allM_nb_default_OLD(gasnet_team_handle_t team,
				    void * const dstlist[], void * const srclist[],
				    size_t nbytes, int flags, uint32_t sequence
				    GASNETE_THREAD_FARG)
{
  #if GASNET_SEQ
  /* Exactly one thread-local addr per list - forward to gall_nb() */
  if (flags & GASNET_COLL_LOCAL) {
    return gasnete_coll_gather_all_nb(team, dstlist[0], srclist[0], nbytes,
				      flags, sequence GASNETE_THREAD_PASS);
  }
  #endif

  /* "Discover" in-segment flags if needed/possible */
  flags = gasnete_coll_segment_checkM(flags, 0, 0, dstlist, nbytes*gasneti_nodes,
    					 0, 0, srclist, nbytes);

  /* XXX: need more implementations to choose from here */
  return gasnete_coll_gallM_Gath(team, dstlist, srclist, nbytes, flags, sequence GASNETE_THREAD_PASS);
}

extern gasnet_coll_handle_t
gasnete_coll_exchange_nb_default_OLD(gasnet_team_handle_t team,
				 void *dst, void *src,
				 size_t nbytes, int flags, uint32_t sequence
				 GASNETE_THREAD_FARG)
{
  #if GASNET_PAR
  /* Thread-local addr(s) - forward to exchgM_nb() */
  if (flags & GASNET_COLL_LOCAL) {
    return gasnete_coll_exchangeM_nb(team, &dst, &src, nbytes,
				     flags | GASNETE_COLL_THREAD_LOCAL, sequence
				     GASNETE_THREAD_PASS);
  }
  #endif

  /* "Discover" in-segment flags if needed/possible */
  flags = gasnete_coll_segment_check(flags, 0, 0, dst, nbytes*gasneti_nodes,
				     0, 0, src, nbytes*gasneti_nodes);

  /* XXX: need more implementations to choose from here */
  return gasnete_coll_exchg_Gath(team, dst, src, nbytes, flags, sequence GASNETE_THREAD_PASS);
}

extern gasnet_coll_handle_t
gasnete_coll_exchangeM_nb_default_OLD(gasnet_team_handle_t team,
				  void * const dstlist[], void * const srclist[],
				  size_t nbytes, int flags, uint32_t sequence
				  GASNETE_THREAD_FARG)
{
  #if GASNET_SEQ
  /* Exactly one thread-local addr per list - forward to exchg_nb() */
  if (flags & GASNET_COLL_LOCAL) {
    return gasnete_coll_exchange_nb(team, dstlist[0], srclist[0], nbytes,
				    flags, sequence GASNETE_THREAD_PASS);
  }
  #endif

  /* "Discover" in-segment flags if needed/possible */
  flags = gasnete_coll_segment_checkM(flags, 0, 0, dstlist, nbytes*gasneti_nodes,
				      0, 0, srclist, nbytes*gasneti_nodes);

  /* XXX: need more implementations to choose from here */
  return gasnete_coll_exchgM_Gath(team, dstlist, srclist, nbytes, flags, sequence GASNETE_THREAD_PASS);
}

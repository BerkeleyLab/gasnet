/*  $Archive:: /Ti/GASNet/extended/gasnet_extended_coll.h                 $
 *     $Date: 2004/05/26 00:31:40 $
 * $Revision: 1.1.2.16 $
 * Description: GASNet Extended API Collective declarations
 * Copyright 2004, Dan Bonachea <bonachea@cs.berkeley.edu>
 * Terms of use are as specified in license.txt
 */

#ifndef _IN_GASNET_H
  #error This file is not meant to be included directly- clients should include gasnet.h
#endif

#ifndef _GASNET_EXTENDED_COLL_H
#define _GASNET_EXTENDED_COLL_H

/*---------------------------------------------------------------------------------*/
/* Flag values: */

/* Sync flags - NO DEFAULT */
#define GASNET_COLL_IN_NOSYNC	(1<<0)
#define GASNET_COLL_IN_MYSYNC	(1<<1)
#define GASNET_COLL_IN_ALLSYNC	(1<<2)
#define GASNET_COLL_OUT_NOSYNC	(1<<3)
#define GASNET_COLL_OUT_MYSYNC	(1<<4)
#define GASNET_COLL_OUT_ALLSYNC	(1<<5)

#define GASNET_COLL_SINGLE	(1<<6)
#define GASNET_COLL_LOCAL	(1<<7)

#define GASNET_COLL_AGGREGATE	(1<<8)

#define GASNET_COLL_DST_IN_SEGMENT	(1<<9)
#define GASNET_COLL_SRC_IN_SEGMENT	(1<<10)

/* XXX: incomplete? */

#define GASNETE_COLL_OP_COMPLETE	0x1
#define GASNETE_COLL_OP_INACTIVE	0x2

/*---------------------------------------------------------------------------------*/

/* Forward type decls and typedefs: */
struct gasnete_coll_op_t_;
typedef struct gasnete_coll_op_t_ gasnete_coll_op_t;

/*---------------------------------------------------------------------------------*/

/* Thread-specific data: */
typedef struct {
    gasnete_coll_op_t	*op_freelist;
    void 		*generic_data_freelist;

    /* Linkage used by the thread-specific active ops list. */
    #ifdef GASNETE_COLL_LIST_OVERRIDE
	/* Custom implementation of coll_ops active list */
	GASNET_COLL_LIST_TD_FIELDS
    #else
	/* Default implementation of coll_ops active list */
	gasnete_coll_op_t	*active_head, **active_tail_p;
    #endif

    /* XXX: more fields to come */

    /* Macro for conduit-specific extension */
    #ifdef GASNETE_COLL_THREADDATA_EXTRA
      GASNETE_COLL_THREADDATA_EXTRA
    #endif
} gasnete_coll_threaddata_t;

/*---------------------------------------------------------------------------------*/

/* Handle type for collective teams: */
#ifndef GASNETE_COLL_TEAMS_OVERRIDE
    struct gasnete_coll_team_t_;
    typedef struct gasnete_coll_team_t_ *gasnete_coll_team_t;
    typedef gasnete_coll_team_t gasnet_team_handle_t;
    #define GASNET_TEAM_ALL	NULL
#endif

/* Type for collective teams: */
struct gasnete_coll_team_t_ {
    /* access serialized by gasnete_coll_table_lock: */
    #ifdef GASNETE_COLL_TABLE_OVERRIDE
	/* Custom implementation of coll_ops table */
	GASNET_COLL_TABLE_TEAM_FIELDS
    #else
	/* Default implementation of coll_ops table
	 * does not have a team-specific portion.
	 */
    #endif

    /* read-only fields: */
    uint32_t			team_id;

    /* XXX: Design not complete yet */
};

/*---------------------------------------------------------------------------------*/

#ifndef GASNETE_COLL_HANDLE_OVERRIDE
  /* Handle type for collective ops: */
  typedef volatile uintptr_t *gasnet_coll_handle_t;
  #define GASNET_COLL_INVALID_HANDLE NULL
#endif

/* Function pointer type for polling collective ops: */
typedef int (*gasnete_coll_poll_fn)(gasnete_coll_op_t *);

/* Type for collective ops: */
struct gasnete_coll_op_t_ {
    gasnete_coll_threaddata_t	*threaddata;	/* Data for initiating thread */

    /* Linkage used by the thread-specific active ops list. */
    #ifdef GASNETE_COLL_LIST_OVERRIDE
	/* Custom implementation of coll_ops active list */
	GASNET_COLL_LIST_OP_FIELDS
    #else
	/* Default implementation of coll_ops active list */
	gasnete_coll_op_t	*active_next, **active_prev_p;
    #endif

    /* Linkage used by aggregation.
     * Access is serialized by specification+client: */
    #ifdef GASNETE_COLL_AGG_OVERRIDE
	/* Custom implementation of ops aggregation */
	GASNET_COLL_AGG_OP_FIELDS
    #else
	/* Custom implementation of ops aggregation */
    	gasnete_coll_op_t		*agg_next, *agg_prev, *agg_head;
    #endif

    /* Read-only fields: */
    gasnete_coll_team_t		team;
    uint32_t			sequence;
    unsigned int		flags;
    gasnet_coll_handle_t	handle;

    /* Per-instance fields and associated HSL: */
    gasnet_hsl_t		lock;
    void			*data;
    gasnete_coll_poll_fn	poll_fn;
};

/*---------------------------------------------------------------------------------*/

extern gasnet_hsl_t gasnete_coll_table_lock;

extern gasnete_coll_team_t gasnete_coll_team_lookup(uint32_t team_id);

extern gasnete_coll_op_t *
gasnete_coll_op_lookup(gasnete_coll_team_t team, uint32_t sequence);

extern gasnete_coll_op_t *
gasnete_coll_op_create(gasnete_coll_team_t team, uint32_t sequence, unsigned int flags, gasnete_coll_threaddata_t *td);
extern void
gasnete_coll_op_destroy(gasnete_coll_op_t *op);

/* Aggregation interface: */
extern gasnet_coll_handle_t
gasnete_coll_op_submit(gasnete_coll_op_t *op, gasnet_coll_handle_t handle);
extern void gasnete_coll_op_complete(gasnete_coll_op_t *op, int poll_result);

extern void gasnete_coll_poll(void);

/*---------------------------------------------------------------------------------*/
/* Debugging and tracing macros */

#if GASNET_DEBUG
  /* Argument validation */
  extern void gasnete_coll_validate(gasnet_team_handle_t team,
                                    gasnet_node_t dstnode, void *dstaddr, size_t dstlen, int dstisv,
                                    gasnet_node_t srcnode, void *srcaddr, size_t srclen, int srcisv,
                                    unsigned int flags);
  #define GASNETE_COLL_VALIDATE gasnete_coll_validate
#else
  #define GASNETE_COLL_VALIDATE(T,DN,DA,DL,DV,SN,SA,SL,SV,F)
#endif

#define GASNETE_COLL_VALIDATE_BROADCAST(T,D,R,S,N,F)   \
	GASNETE_COLL_VALIDATE(T,gasnete_mynode,D,N,0,R,S,N,0,F)
#define GASNETE_COLL_VALIDATE_BROADCAST_M(T,D,R,S,N,F)   \
	GASNETE_COLL_VALIDATE(T,gasnete_mynode,D,N,1,R,S,N,0,F)

#define GASNETE_COLL_VALIDATE_SCATTER(T,D,R,S,N,F)   \
	GASNETE_COLL_VALIDATE(T,gasnete_mynode,D,N,0,R,S,(N)*gasnete_nodes,0,F)
#define GASNETE_COLL_VALIDATE_SCATTER_M(T,D,R,S,N,F)   \
	GASNETE_COLL_VALIDATE(T,gasnete_mynode,D,N,1,R,S,(N)*gasnete_nodes,0,F)

#define GASNETE_COLL_VALIDATE_GATHER(T,R,D,S,N,F)     \
	GASNETE_COLL_VALIDATE(T,R,D,(N)*gasnete_nodes,0,gasnete_mynode,S,N,0,F)
#define GASNETE_COLL_VALIDATE_GATHER_M(T,R,D,S,N,F)     \
	GASNETE_COLL_VALIDATE(T,R,D,(N)*gasnete_nodes,0,gasnete_mynode,S,N,1,F)

#define GASNETE_COLL_VALIDATE_GATHER_ALL(T,D,S,N,F)                \
	GASNETE_COLL_VALIDATE(T,gasnete_mynode,D,(N)*gasnete_nodes,0,gasnete_mynode,S,N,0,F)
#define GASNETE_COLL_VALIDATE_GATHER_ALL_M(T,D,S,N,F)                \
	GASNETE_COLL_VALIDATE(T,gasnete_mynode,D,(N)*gasnete_nodes,1,gasnete_mynode,S,N,1,F)

#define GASNETE_COLL_VALIDATE_EXCHANGE(T,D,S,N,F)                  \
        GASNETE_COLL_VALIDATE(T,gasnete_mynode,D,(N)*gasnete_nodes,0,gasnete_mynode,S,(N)*gasnete_nodes,0,F)
#define GASNETE_COLL_VALIDATE_EXCHANGE_M(T,D,S,N,F)                  \
        GASNETE_COLL_VALIDATE(T,gasnete_mynode,D,(N)*gasnete_nodes,1,gasnete_mynode,S,(N)*gasnete_nodes,1,F)


#define GASNETE_COLL_TRACE_BROADCAST(TYPE,TEAM,DST,ROOT,SRC,NBYTES,FLAGS) \
  /* XXX: fill this in */
#define GASNETE_COLL_TRACE_BROADCAST_M(TYPE,TEAM,DSTLIST,ROOT,SRC,NBYTES,FLAGS) \
  /* XXX: fill this in */

#define GASNETE_COLL_TRACE_SCATTER(TYPE,TEAM,DST,ROOT,SRC,NBYTES,FLAGS) \
  /* XXX: fill this in */
#define GASNETE_COLL_TRACE_SCATTER_M(TYPE,TEAM,DSTLIST,ROOT,SRC,NBYTES,FLAGS) \
  /* XXX: fill this in */

#define GASNETE_COLL_TRACE_GATHER(TYPE,TEAM,ROOT,DST,SRC,NBYTES,FLAGS) \
  /* XXX: fill this in */
#define GASNETE_COLL_TRACE_GATHER_M(TYPE,TEAM,ROOT,DST,SRCLIST,NBYTES,FLAGS) \
  /* XXX: fill this in */

#define GASNETE_COLL_TRACE_GATHER_ALL(TYPE,TEAM,DST,SRC,NBYTES,FLAGS) \
  /* XXX: fill this in */
#define GASNETE_COLL_TRACE_GATHER_ALL_M(TYPE,TEAM,DSTLIST,SRCLIST,NBYTES,FLAGS) \
  /* XXX: fill this in */

#define GASNETE_COLL_TRACE_EXCHANGE(TYPE,TEAM,DST,SRC,NBYTES,FLAGS) \
  /* XXX: fill this in */
#define GASNETE_COLL_TRACE_EXCHANGE_M(TYPE,TEAM,DSTLIST,SRCLIST,NBYTES,FLAGS) \
  /* XXX: fill this in */

/*------------------------------------------------------------------------------------*/
#define GASNETE_COLL_TRACE_TRYSYNC(name,success) \
	GASNETI_TRACE_EVENT_VAL(X,name,((success) == GASNET_OK?1:0))

#if GASNETI_STATS_OR_TRACE
    #define GASNETE_COLL_TRACE_WAITSYNC_BEGIN() \
	gasneti_stattime_t _waitstart = GASNETI_STATTIME_NOW_IFENABLED(X)
#else
    #define GASNETE_COLL_TRACE_WAITSYNC_BEGIN() \
	static char _dummy = (char)sizeof(_dummy)
#endif


#define GASNETE_COLL_TRACE_WAITSYNC_END(name) \
    GASNETI_TRACE_EVENT_TIME(X,name,GASNETI_STATTIME_NOW_IFENABLED(X) - _waitstart)

/*---------------------------------------------------------------------------------*/

/* gasnet_coll_init(const size_t images[], int init_flags)
 *
 *   images:     Array of gasnet_nodes() elements giving the number of
 *               images present on each node.
 *   init_flags: Presently unused.  Must be 0.
 */
#ifndef gasnet_coll_init
  extern void gasnete_coll_init(const size_t images[], int init_flags);
  #define gasnet_coll_init gasnete_coll_init
#endif

/*---------------------------------------------------------------------------------*/

#ifndef gasnete_coll_try_sync
  extern int gasnete_coll_try_sync(gasnet_coll_handle_t handle);
#endif
GASNET_INLINE_MODIFIER(gasnet_coll_try_sync)
int gasnet_coll_try_sync(gasnet_coll_handle_t handle) {
  int result = GASNET_OK;
  if_pt (handle != GASNET_COLL_INVALID_HANDLE) {
    result = gasnete_coll_try_sync(handle);
  }
  GASNETE_COLL_TRACE_TRYSYNC(COLL_TRY_SYNC,result);
  return result;
}

#ifndef gasnete_coll_wait_sync
  /* Default 1-line implementation */
  GASNET_INLINE_MODIFIER(gasnete_coll_wait_sync)
  void gasnete_coll_wait_sync(gasnet_coll_handle_t handle) {
    if_pt (handle != GASNET_COLL_INVALID_HANDLE) {
      gasneti_waitwhile(gasnete_coll_try_sync(handle) == GASNET_ERR_NOT_READY);
    }
  }
#endif
GASNET_INLINE_MODIFIER(gasnet_coll_wait_sync)
void gasnet_coll_wait_sync(gasnet_coll_handle_t handle) {
  GASNETE_COLL_TRACE_WAITSYNC_BEGIN();
  gasnete_coll_wait_sync(handle);
  GASNETE_COLL_TRACE_WAITSYNC_END(COLL_WAIT_SYNC);
}

/*---------------------------------------------------------------------------------*/

#ifndef gasnete_coll_broadcast_nb
  extern gasnet_coll_handle_t
  gasnete_coll_broadcast_nb(gasnet_team_handle_t team,
			    void *dst,
                            gasnet_node_t srcnode, void *src,
                            size_t nbytes, int flags GASNETE_THREAD_FARG);
#endif
GASNET_INLINE_MODIFIER(_gasnet_coll_broadcast_nb)
gasnet_coll_handle_t
_gasnet_coll_broadcast_nb(gasnet_team_handle_t team,
			  void *dst,
                          gasnet_node_t srcnode, void *src,
                          size_t nbytes, int flags GASNETE_THREAD_FARG) {
  gasnet_coll_handle_t handle;
  GASNETE_COLL_TRACE_BROADCAST(COLL_BROADCAST_NB,team,dst,srcnode,src,nbytes,flags);
  GASNETE_COLL_VALIDATE_BROADCAST(team,dst,srcnode,src,nbytes,flags);
  handle = gasnete_coll_broadcast_nb(team, dst, srcnode, src, nbytes, flags GASNETE_THREAD_PASS);
  gasnete_coll_poll();
  return handle;
}
#define gasnet_coll_broadcast_nb(team,dst,srcnode,src,nbytes,flags) \
       _gasnet_coll_broadcast_nb(team,dst,srcnode,src,nbytes,flags GASNETE_THREAD_GET)

#ifndef gasnete_coll_broadcast
  GASNET_INLINE_MODIFIER(gasnete_coll_broadcast)
  void gasnete_coll_broadcast(gasnet_team_handle_t team,
                              void *dst,
                              gasnet_node_t srcnode, void *src,
                              size_t nbytes, int flags GASNETE_THREAD_FARG) {
    gasnet_coll_handle_t handle;
    handle = gasnete_coll_broadcast_nb(team,dst,srcnode,src,nbytes,flags GASNETE_THREAD_PASS);
    gasnete_coll_wait_sync(handle);
  }
#endif
GASNET_INLINE_MODIFIER(_gasnet_coll_broadcast)
void _gasnet_coll_broadcast(gasnet_team_handle_t team,
                            void *dst,
                            gasnet_node_t srcnode, void *src,
                            size_t nbytes, int flags GASNETE_THREAD_FARG) {
  GASNETE_COLL_TRACE_BROADCAST(COLL_BROADCAST,team,dst,srcnode,src,nbytes,flags);
  GASNETE_COLL_VALIDATE_BROADCAST(team,dst,srcnode,src,nbytes,flags);
  gasnete_coll_broadcast(team, dst, srcnode, src, nbytes, flags GASNETE_THREAD_PASS);
}
#define gasnet_coll_broadcast(team,dst,srcnode,src,nbytes,flags) \
       _gasnet_coll_broadcast(team,dst,srcnode,src,nbytes,flags GASNETE_THREAD_GET)

#ifndef gasnete_coll_broadcastM_nb
  extern gasnet_coll_handle_t
  gasnete_coll_broadcastM_nb(gasnet_team_handle_t team,
			     void *dstlist[],
                             gasnet_node_t srcnode, void *src,
                             size_t nbytes, int flags GASNETE_THREAD_FARG);
#endif
GASNET_INLINE_MODIFIER(_gasnet_coll_broadcastM_nb)
gasnet_coll_handle_t
_gasnet_coll_broadcastM_nb(gasnet_team_handle_t team,
			   void *dstlist[],
                           gasnet_node_t srcnode, void *src,
                           size_t nbytes, int flags GASNETE_THREAD_FARG) {
  gasnet_coll_handle_t handle;
  GASNETE_COLL_TRACE_BROADCAST_M(COLL_BROADCASTM_NB,team,dstlist,srcnode,src,nbytes,flags);
  GASNETE_COLL_VALIDATE_BROADCAST_M(team,dstlist,srcnode,src,nbytes,flags);
  handle = gasnete_coll_broadcastM_nb(team, dstlist, srcnode, src, nbytes, flags GASNETE_THREAD_PASS);
  gasnete_coll_poll();
  return handle;
}
#define gasnet_coll_broadcastM_nb(team,dstlist,srcnode,src,nbytes,flags) \
       _gasnet_coll_broadcastM_nb(team,dstlist,srcnode,src,nbytes,flags GASNETE_THREAD_GET)

#ifndef gasnete_coll_broadcastM
  GASNET_INLINE_MODIFIER(gasnete_coll_broadcastM)
  void gasnete_coll_broadcastM(gasnet_team_handle_t team,
                               void *dstlist[],
                               gasnet_node_t srcnode, void *src,
                               size_t nbytes, int flags GASNETE_THREAD_FARG) {
    gasnet_coll_handle_t handle;
    handle = gasnete_coll_broadcastM_nb(team,dstlist,srcnode,src,nbytes,flags GASNETE_THREAD_PASS);
    gasnete_coll_wait_sync(handle);
  }
#endif
GASNET_INLINE_MODIFIER(_gasnet_coll_broadcastM)
void _gasnet_coll_broadcastM(gasnet_team_handle_t team,
                             void *dstlist[],
                             gasnet_node_t srcnode, void *src,
                             size_t nbytes, int flags GASNETE_THREAD_FARG) {
  GASNETE_COLL_TRACE_BROADCAST_M(COLL_BROADCASTM,team,dstlist,srcnode,src,nbytes,flags);
  GASNETE_COLL_VALIDATE_BROADCAST_M(team,dstlist,srcnode,src,nbytes,flags);
  gasnete_coll_broadcastM(team, dstlist, srcnode, src, nbytes, flags GASNETE_THREAD_PASS);
}
#define gasnet_coll_broadcastM(team,dstlist,srcnode,src,nbytes,flags) \
       _gasnet_coll_broadcastM(team,dstlist,srcnode,src,nbytes,flags GASNETE_THREAD_GET)

/*---------------------------------------------------------------------------------*/
#endif

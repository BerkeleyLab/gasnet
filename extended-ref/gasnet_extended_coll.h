/*  $Archive:: /Ti/GASNet/extended/gasnet_extended_coll.h                 $
 *     $Date: 2004/04/09 00:30:36 $
 * $Revision: 1.1.2.8 $
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

#define GASNET_COLL_IN_NOSYNC	0
#define GASNET_COLL_IN_MYSYNC	(1<<0)
#define GASNET_COLL_IN_ALLSYNC	(1<<1)
#define GASNET_COLL_OUT_NOSYNC	0
#define GASNET_COLL_OUT_MYSYNC	(1<<2)
#define GASNET_COLL_OUT_ALLSYNC	(1<<3)

#define GASNET_COLL_SINGLE	(1<<4)
#define GASNET_COLL_LOCAL	(1<<5)

#define GASNET_COLL_AGGREGATE	(1<<6)

#define GASNET_COLL_DST_IN_SEGMENT	(1<<7)
#define GASNET_COLL_SRC_IN_SEGMENT	(1<<8)

/* XXX: incomplete? */

#define GASNETE_COLL_OP_COMPLETE	0x1
#define GASNETE_COLL_OP_INACTIVE	0x2

/*---------------------------------------------------------------------------------*/

/* Forward type decls and typedefs: */
struct gasnete_coll_op_t_;
typedef struct gasnete_coll_op_t_ gasnete_coll_op_t;

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
  typedef volatile int *gasnet_coll_handle_t;
  #define GASNET_COLL_INVALID_HANDLE NULL
#endif

/* Function pointer type for polling collective ops: */
typedef int (*gasnete_coll_poll_fn)(gasnete_coll_op_t *);

/* Type for collective ops: */
struct gasnete_coll_op_t_ {
    /* Linkage used by the ops lookup table.
     * Access is serialized by gasnete_coll_table_lock: */
    #ifdef GASNETE_COLL_TABLE_OVERRIDE
	/* Custom implementation of coll_ops lookup table */
	GASNET_COLL_TABLE_OP_FIELDS
    #else
	/* Default implementation of coll_ops table */
	gasnete_coll_op_t	*table_next, *table_prev;
    #endif

    /* Linkage used by the ops active list.
     * Access is serialized by gasnete_coll_table_lock: */
    #ifdef GASNETE_COLL_LIST_OVERRIDE
	/* Custom implementation of coll_ops active list */
	GASNET_COLL_LIST_OP_FIELDS
    #else
	/* Default implementation of coll_ops active list */
	gasnete_coll_op_t	*list_next, *list_prev;
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
gasnete_coll_op_create(gasnete_coll_team_t team, uint32_t sequence, unsigned int flags);
extern void
gasnete_coll_op_destroy(gasnete_coll_op_t *op);

/* Aggregation interface: */
extern gasnet_coll_handle_t
gasnete_coll_op_submit(gasnete_coll_op_t *op, gasnet_coll_handle_t handle);
extern void gasnete_coll_op_complete(gasnete_coll_op_t *op, int poll_result);

extern void gasnete_coll_init(void);
extern void gasnete_coll_poll(void);

/*---------------------------------------------------------------------------------*/

#ifndef gasnet_coll_try_sync
  extern int gasnete_coll_try_sync(gasnet_coll_handle_t handle);
  #define gasnet_coll_try_sync	gasnete_coll_try_sync
#endif
#ifndef gasnet_coll_wait_sync
  GASNET_INLINE_MODIFIER(gasnet_coll_wait_sync)
  void gasnet_coll_wait_sync(gasnet_coll_handle_t handle) {
    gasneti_waitwhile(gasnet_coll_try_sync(handle) != GASNET_OK);
  }
#endif

#ifndef gasnet_coll_broadcast_nb
extern gasnet_coll_handle_t
  gasnet_coll_broadcast_nb(gasnet_team_handle_t team,
			   void *dst,
                           gasnet_node_t srcnode, void *src,
                           size_t nbytes, int flags);
#endif
#ifndef gasnet_coll_broadcast
  #define gasnet_coll_broadcast(team,dst,srcnode,src,nbytes,flags) \
	gasnet_coll_wait_sync(gasnet_coll_broadcast_nb(team,dst,srcnode,src,nbytes,flags))
#endif


/*---------------------------------------------------------------------------------*/
#endif

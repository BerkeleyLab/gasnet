/*  $Archive:: /Ti/GASNet/extended/gasnet_extended_coll.h                 $
 *     $Date: 2004/04/02 18:53:19 $
 * $Revision: 1.1.2.4 $
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

/* Forward type decls and typedefs: */
struct gasnete_coll_op_t_;
typedef struct gasnete_coll_op_t_ gasnete_coll_op_t;
struct gasnete_coll_team_t_;
typedef struct gasnete_coll_team_t_ gasnete_coll_team_t;

/*---------------------------------------------------------------------------------*/

/* Handle type for collective teams: */
typedef gasnete_coll_team_t *gasnet_team_handle_t;	/* XXX: or _coll_team_ ? */

/* Type for collective teams: */
struct gasnete_coll_team_t_ {
    /* access serialized by gasnete_coll_table_lock: */
    #ifdef GASNETE_COLL_TABLE_TEAM_FIELDS
	/* Custom implementation of coll ops table */
	GASNET_COLL_TABLE_TEAM_FIELDS
    #else
	/* Default implementation of coll ops table
	 * does not have a team-specific portion.
	 */
    #endif

    /* read-only fields: */
    uint32_t			team_id;

    /* XXX: Design not complete yet */
};

/*---------------------------------------------------------------------------------*/

/* Handle type for collective ops: */
typedef gasnete_coll_op_t *gasnet_coll_handle_t;

/* Function pointer type for polling collective ops: */
typedef int (*gasnete_coll_poll_fn)(gasnete_coll_op_t *);

/* Type for collective ops: */
struct gasnete_coll_op_t_ {
    /* Linkage used by the active list and ops table.
     * Access is serialized by gasnete_coll_table_lock: */
    #ifdef GASNETE_COLL_TABLE_OP_FIELDS
	/* Custom implementation of coll ops table */
	GASNET_COLL_TABLE_OP_FIELDS
    #else
	/* Default implementation of coll ops table */
	gasnete_coll_op_t	*list_next, *list_prev;
	gasnete_coll_op_t	*hash_next, *hash_prev;
    #endif

    /* Linkage used by aggregation.
     * Access is serialized by specification+client: */
    #ifdef GASNETE_COLL_AGG_OP_FIELDS
	/* Custom implementation of ops aggregation */
	GASNET_COLL_TABLE_OP_FIELDS
    #else
	/* Custom implementation of ops aggregation */
    	gasnete_coll_op_t		*agg_prev;
    #endif

    /* Read-only fields: */
    gasnete_coll_team_t		*team;
    uint32_t			sequence;
    unsigned int		flags;

    /* Per-instance fields and associated HSL: */
    gasnet_hsl_t		lock;
    void			*data;
    gasnete_coll_poll_fn	poll_fn;

    /* Not serialized: */
    int				done;	/* XXX: place on private cache line */
};

/*---------------------------------------------------------------------------------*/

extern gasnet_hsl_t gasnete_coll_table_lock;

extern gasnete_coll_team_t *
gasnete_coll_team_lookup(uint32_t team_id);

extern gasnete_coll_op_t *
gasnete_coll_op_lookup(gasnete_coll_team_t *team, uint32_t sequence);
extern gasnete_coll_op_t *
gasnete_coll_op_create(gasnete_coll_team_t *team, uint32_t sequence, unsigned int flags);

extern void gasnete_coll_poll(void);

/*---------------------------------------------------------------------------------*/
#endif

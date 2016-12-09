/*   $Source: bitbucket.org:berkeleylab/gasnet.git/ibv-conduit/gasnet_core.h $
 * Description: GASNet header for ibv conduit core
 * Copyright 2002, Dan Bonachea <bonachea@cs.berkeley.edu>
 * Terms of use are as specified in license.txt
 */

#ifndef _IN_GASNETEX_H
  #error This file is not meant to be included directly- clients should include gasnetex.h
#endif

#ifndef _GASNET_CORE_H
#define _GASNET_CORE_H

#include <gasnet_core_help.h>

/* ------------------------------------------------------------------------------------ */
/*
  Initialization
  ==============
*/
/* gasnet_init not inlined or renamed because we use redef-name trick on  
   it to ensure proper version linkage */
extern int gasnet_init(int *argc, char ***argv);

extern int gasnetc_attach(gasnet_handlerentry_t *table, int numentries,
                          uintptr_t segsize, uintptr_t minheapoffset);
#define gasnet_attach gasnetc_attach

extern int gasnetc_EPRegisterHandlers(
                gasnetex_endpoint_t     ep,
                gasnetex_handlerentry_t *table,
                int                     numentries);
#define gasnetex_EPRegisterHandlers gasnetc_EPRegisterHandlers

extern void gasnetc_exit(int exitcode) GASNETI_NORETURN;
GASNETI_NORETURNP(gasnetc_exit)
#define gasnet_exit gasnetc_exit

/* Some conduits permit gasnet_init(NULL,NULL).
   Define to 1 if this conduit supports this extension, or to 0 otherwise.  */
#if !HAVE_MPI_SPAWNER || (GASNETI_MPI_VERSION >= 2)
  #define GASNET_NULL_ARGV_OK 1
#else
  #define GASNET_NULL_ARGV_OK 0
#endif
/* ------------------------------------------------------------------------------------ */
/*
  Handler-safe locks
  ==================
*/
typedef struct _gasnetex_hsl_t {
  gasneti_mutex_t lock;

  #if GASNETI_STATS_OR_TRACE
    gasneti_tick_t acquiretime;
  #endif
} gasnetex_hsl_t;

#if GASNETI_STATS_OR_TRACE
  #define GASNETC_LOCK_STAT_INIT ,0 
#else
  #define GASNETC_LOCK_STAT_INIT  
#endif

#define GASNETEX_HSL_INITIALIZER { \
  GASNETI_MUTEX_INITIALIZER      \
  GASNETC_LOCK_STAT_INIT         \
  }

/* decide whether we have "real" HSL's */
#if GASNETI_THREADS ||                           /* need for safety */ \
    GASNET_DEBUG || GASNETI_STATS_OR_TRACE       /* or debug/tracing */
  #ifdef GASNETC_NULL_HSL 
    #error bad defn of GASNETC_NULL_HSL
  #endif
#else
  #define GASNETC_NULL_HSL 1
#endif

#if GASNETC_NULL_HSL
  /* HSL's unnecessary - compile away to nothing */
  #define gasnetex_hsl_init(hsl)
  #define gasnetex_hsl_destroy(hsl)
  #define gasnetex_hsl_lock(hsl)
  #define gasnetex_hsl_unlock(hsl)
  #define gasnetex_hsl_trylock(hsl)	GASNET_OK
#else
  extern void gasnetc_hsl_init   (gasnetex_hsl_t *hsl);
  extern void gasnetc_hsl_destroy(gasnetex_hsl_t *hsl);
  extern void gasnetc_hsl_lock   (gasnetex_hsl_t *hsl);
  extern void gasnetc_hsl_unlock (gasnetex_hsl_t *hsl);
  extern int  gasnetc_hsl_trylock(gasnetex_hsl_t *hsl) GASNETI_WARN_UNUSED_RESULT;

  #define gasnetex_hsl_init    gasnetc_hsl_init
  #define gasnetex_hsl_destroy gasnetc_hsl_destroy
  #define gasnetex_hsl_lock    gasnetc_hsl_lock
  #define gasnetex_hsl_unlock  gasnetc_hsl_unlock
  #define gasnetex_hsl_trylock gasnetc_hsl_trylock
#endif

/* ------------------------------------------------------------------------------------ */
/*
  Active Message Size Limits
  ==========================
*/

#if GASNETI_STATS_OR_TRACE
  #define GASNETC_HDR_TIMESTAMP	8
#else
  #define GASNETC_HDR_TIMESTAMP	0
#endif
#define GASNETC_MEDIUM_HDRSZ	(GASNETC_HDR_TIMESTAMP + 4)
#define GASNETC_LONG_HDRSZ	(GASNETC_HDR_TIMESTAMP + SIZEOF_VOID_P + 4)

#define GASNETC_MAX_ARGS_USER	16
#define GASNETC_MAX_ARGS_EXTRA	1	/* For flow-control info */
#define GASNETC_MAX_ARGS	(GASNETC_MAX_ARGS_USER + GASNETC_MAX_ARGS_EXTRA)

#define GASNETC_MAX_MEDIUM_(nargs) \
               (GASNETC_BUFSZ - \
                GASNETI_ALIGNUP_NOASSERT(GASNETC_MEDIUM_HDRSZ + 4*(GASNETC_MAX_ARGS_EXTRA+nargs), \
                                         8))
#define GASNETC_MAX_MEDIUM	GASNETC_MAX_MEDIUM_(GASNETC_MAX_ARGS_USER)
#define GASNETC_MAX_LONG_REQ	(0x7fffffff)
#define GASNETC_MAX_PACKEDLONG	(GASNETC_BUFSZ - GASNETC_LONG_HDRSZ - 4*GASNETC_MAX_ARGS)
#if GASNETC_PIN_SEGMENT
  #define GASNETC_MAX_LONG_REP	GASNETC_MAX_LONG_REQ
#else
  #define GASNETC_MAX_LONG_REP	GASNETC_MAX_PACKEDLONG
#endif

#define gasnet_AMMaxArgs()          ((size_t)GASNETC_MAX_ARGS_USER)
#define gasnetex_lub_AMRequestMedium() ((size_t)GASNETC_MAX_MEDIUM)
#define gasnetex_lub_AMReplyMedium()   ((size_t)GASNETC_MAX_MEDIUM)
#define gasnetex_lub_AMRequestLong()   ((size_t)GASNETC_MAX_LONG_REQ)
#define gasnetex_lub_AMReplyLong()     ((size_t)GASNETC_MAX_LONG_REP)

  // TODO-EX: Medium sizes can be further improved upon for PSHM case
#define gasnetex_max_AMRequestMedium(team,rank,lc_opt,flags,nargs) ((size_t)GASNETC_MAX_MEDIUM_(nargs))
#define gasnetex_max_AMReplyMedium(team,rank,lc_opt,flags,nargs)   ((size_t)GASNETC_MAX_MEDIUM_(nargs))
#define gasnetex_max_AMRequestLong(team,rank,lc_opt,flags,nargs) ((size_t)GASNETC_MAX_LONG_REQ)
#define gasnetex_max_AMReplyLong(team,rank,lc_opt,flags,nargs)   ((size_t)GASNETC_MAX_LONG_REP)

/* ------------------------------------------------------------------------------------ */
/*
  Misc. Active Message Functions
  ==============================
*/
extern int gasnetc_AMGetMsgSource(gasnetex_token_t token, gasnetex_rank_t *srcindex);

#define gasnet_AMGetMsgSource  gasnetc_AMGetMsgSource

#define GASNET_BLOCKUNTIL(cond) gasneti_polluntil(cond)

/* ------------------------------------------------------------------------------------ */
#ifdef GASNETI_BLCR_ENABLED
/* Collective checkpoints */
extern int gasnet_all_checkpoint(const char *dir);
extern int gasnet_all_rollback(const char *dir);
#define GASNET_BLCR 1
#endif
/* ------------------------------------------------------------------------------------ */

#endif

#include <gasnet_ammacros.h>

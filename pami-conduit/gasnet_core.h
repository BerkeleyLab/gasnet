/*   $Source: bitbucket.org:berkeleylab/gasnet.git/pami-conduit/gasnet_core.h $
 * Description: GASNet header for PAMI conduit core
 * Copyright 2012, Lawrence Berkeley National Laboratory
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
#define GASNET_NULL_ARGV_OK 1
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

#define GASNETC_MAX_ARGS            16
#define GASNETC_MAX_MED_ALLOC       4096 /* Space allocated per AM Medium */
#define GASNETC_MAX_MED_RESRV       128 /* Space used for header and bookkeeping (cache aligned) */
#define GASNETC_MAX_MED             (GASNETC_MAX_MED_ALLOC - GASNETC_MAX_MED_RESRV)
#define GASNETC_MAX_LONG_REQ        0x7FFFFFFF
#define GASNETC_MAX_LONG_REP        GASNETC_MAX_MED /* Uses same scheme as Medium */
#if GASNET_PSHM
  /* (###) If supporting PSHM a conduit must "negotiate" the maximum size of a
   * Medium message.  This can either be done by lowering the conduit's value to
   * the default PSHM value (as shown here), or GASNETI_MAX_MEDIUM_PSHM can be
   * defined in gasnet_core_fwd.h to give the conduit complete control. */
  #define GASNETC_MAX_MEDIUM      ((size_t)MIN(GASNETC_MAX_MED, GASNETI_MAX_MEDIUM_PSHM))
#else
  #define GASNETC_MAX_MEDIUM      ((size_t)GASNETC_MAX_MED)
#endif

#define gasnet_AMMaxArgs()          ((size_t)GASNETC_MAX_ARGS)
#define gasnetex_lub_AMRequestMedium() ((size_t)GASNETC_MAX_MEDIUM)
#define gasnetex_lub_AMReplyMedium()   ((size_t)GASNETC_MAX_MEDIUM)
#define gasnetex_lub_AMRequestLong()   ((size_t)GASNETC_MAX_LONG_REQ)
#define gasnetex_lub_AMReplyLong()     ((size_t)GASNETC_MAX_LONG_REP)

  // TODO-EX: Medium sizes can be improved upon for PSHM case
#define gasnetex_max_AMRequestMedium(team,rank,lc_opt,flags,nargs) ((size_t)GASNETC_MAX_MEDIUM)
#define gasnetex_max_AMReplyMedium(team,rank,lc_opt,flags,nargs)   ((size_t)GASNETC_MAX_MEDIUM)
#define gasnetex_max_AMRequestLong(team,rank,lc_opt,flags,nargs)   ((size_t)GASNETC_MAX_LONG_REQ)
#define gasnetex_max_AMReplyLong(team,rank,lc_opt,flags,nargs)     ((size_t)GASNETC_MAX_LONG_REP)

/* ------------------------------------------------------------------------------------ */
/*
  Misc. Active Message Functions
  ==============================
*/
extern int gasnetc_AMGetMsgSource(gasnetex_token_t token, gasnetex_rank_t *srcindex);

#define gasnet_AMGetMsgSource  gasnetc_AMGetMsgSource

#define GASNET_BLOCKUNTIL(cond) gasneti_polluntil(cond)

/* ------------------------------------------------------------------------------------ */

#endif

#include <gasnet_ammacros.h>

/*   $Source: bitbucket.org:berkeleylab/gasnet.git/template-conduit/gasnet_core.h $
 * Description: GASNet header for <conduitname> conduit core
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

extern void gasnetc_exit(int exitcode) GASNETI_NORETURN;
GASNETI_NORETURNP(gasnetc_exit)
#define gasnet_exit gasnetc_exit

/* Some conduits permit gasnet_init(NULL,NULL).
   Define to 1 if this conduit supports this extension, or to 0 otherwise.  */
#define GASNET_NULL_ARGV_OK ###
/* ------------------------------------------------------------------------------------ */
/*
  Handler-safe locks
  ==================
*/
typedef struct _gasnet_hsl_t {
  gasneti_mutex_t lock;

  #if GASNETI_STATS_OR_TRACE
    gasneti_tick_t acquiretime;
  #endif
} gasnet_hsl_t GASNETI_THREAD_TYPEDEF;

#if GASNETI_STATS_OR_TRACE
  #define GASNETC_LOCK_STAT_INIT ,0 
#else
  #define GASNETC_LOCK_STAT_INIT  
#endif

#define GASNET_HSL_INITIALIZER { \
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
  #define gasnet_hsl_init(hsl)
  #define gasnet_hsl_destroy(hsl)
  #define gasnet_hsl_lock(hsl)
  #define gasnet_hsl_unlock(hsl)
  #define gasnet_hsl_trylock(hsl)	GASNET_OK
#else
  extern void gasnetc_hsl_init   (gasnet_hsl_t *hsl);
  extern void gasnetc_hsl_destroy(gasnet_hsl_t *hsl);
  extern void gasnetc_hsl_lock   (gasnet_hsl_t *hsl);
  extern void gasnetc_hsl_unlock (gasnet_hsl_t *hsl);
  extern int  gasnetc_hsl_trylock(gasnet_hsl_t *hsl) GASNETI_WARN_UNUSED_RESULT;

  #define gasnet_hsl_init    gasnetc_hsl_init
  #define gasnet_hsl_destroy gasnetc_hsl_destroy
  #define gasnet_hsl_lock    gasnetc_hsl_lock
  #define gasnet_hsl_unlock  gasnetc_hsl_unlock
  #define gasnet_hsl_trylock gasnetc_hsl_trylock
#endif
/* ------------------------------------------------------------------------------------ */
/*
  Active Message Size Limits
  ==========================
*/

#define gasnet_AMMaxArgs()          ((size_t)###)

  /* Define least-upper-bound (worst case) limits on payload sizes */
#if GASNET_PSHM
  /* (###) If supporting PSHM a conduit must "negotiate" the maximum size of a
   * Medium message.  This can either be done by lowering the conduit's value to
   * the default PSHM value (as shown here), or GASNETI_MAX_MEDIUM_PSHM can be
   * defined in gasnet_core_fwd.h to give the conduit complete control. */
  #define gasnetex_lub_AMRequestMedium() ((size_t)MIN(###, GASNETI_MAX_MEDIUM_PSHM))
  #define gasnetex_lub_AMReplyMedium()   ((size_t)MIN(###, GASNETI_MAX_MEDIUM_PSHM))
#else
  #define gasnetex_lub_AMRequestMedium() ((size_t)###)
  #define gasnetex_lub_AMReplyMedium()   ((size_t)###)
#endif
#define gasnetex_lub_AMRequestLong()   ((size_t)###)
#define gasnetex_lub_AMReplyLong()     ((size_t)###)

  /* Provide tigher bounds based on parameters (*/
/* Example for closed-form macros:
#define gasnetex_max_AMRequestMedium(team,rank,lc_opt,flags,nargs) ((size_t)###)
#define gasnetex_max_AMReplyMedium(team,rank,lc_opt,flags,nargs)   ((size_t)###)
#define gasnetex_max_AMRequestLong(team,rank,lc_opt,flags,nargs)   ((size_t)###)
#define gasnetex_max_AMReplyLong(team,rank,lc_opt,flags,nargs)     ((size_t)###)
 */
/* Example for true functions:
extern size_t gasnetex_max_AMRequestMedium(
           gasnetex_team_member_t team, gasnetex_rank_t rank,
           gasnetex_handle_t *lc_opt, gasnetex_flags_t flags, int nargs);
extern size_t gasnetex_max_AMReplyMedium(
           gasnetex_team_member_t team, gasnetex_rank_t rank,
           gasnetex_handle_t *lc_opt, gasnetex_flags_t flags, int nargs);
extern size_t gasnetex_max_AMRequestLong(
           gasnetex_team_member_t team, gasnetex_rank_t rank,
           gasnetex_handle_t *lc_opt, gasnetex_flags_t flags, int nargs);
extern size_t gasnetex_max_AMReplyLong(
           gasnetex_team_member_t team, gasnetex_rank_t rank,
           gasnetex_handle_t *lc_opt, gasnetex_flags_t flags, int nargs);
 */

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

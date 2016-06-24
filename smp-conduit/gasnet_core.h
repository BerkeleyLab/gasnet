/*   $Source: bitbucket.org:berkeleylab/gasnet.git/smp-conduit/gasnet_core.h $
 * Description: GASNet header for smp conduit core
 * Copyright 2002, Dan Bonachea <bonachea@cs.berkeley.edu>
 * Terms of use are as specified in license.txt
 */

#ifndef _IN_GASNETEX_H
  #error This file is not meant to be included directly- clients should include gasnetex.h
#endif

#ifndef _GASNET_CORE_H
#define _GASNET_CORE_H

#include <gasnet_core_help.h>

GASNETI_BEGIN_EXTERNC

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
#define GASNET_NULL_ARGV_OK 1
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

#define gasnet_AMMaxArgs()          ((size_t)16)
#define gasnetex_lub_AMRequestMedium() ((size_t)GASNETC_MAX_MEDIUM)
#define gasnetex_lub_AMReplyMedium()   ((size_t)GASNETC_MAX_MEDIUM)
#define gasnetex_lub_AMRequestLong()   ((size_t)GASNETC_MAX_LONG)
#define gasnetex_lub_AMReplyLong()     ((size_t)GASNETC_MAX_LONG)

  // TODO-EX: can these be improved upon?
#define gasnetex_max_AMRequestMedium(team,rank,lc_opt,flags,nargs) gasnetex_lub_AMRequestMedium()
#define gasnetex_max_AMReplyMedium(team,rank,lc_opt,flags,nargs)   gasnetex_lub_AMReplyMedium()
#define gasnetex_max_AMRequestLong(team,rank,lc_opt,flags,nargs)   gasnetex_lub_AMRequestLong()
#define gasnetex_max_AMReplyLong(team,rank,lc_opt,flags,nargs)     gasnetex_lub_AMReplyLong()

/* ------------------------------------------------------------------------------------ */
/*
  Misc. Active Message Functions
  ==============================
*/
extern int gasnetc_AMGetMsgSource(gasnetex_token_t token, gasnetex_rank_t *srcindex);

#define gasnet_AMGetMsgSource  gasnetc_AMGetMsgSource

#if GASNET_PSHM
  #define GASNET_BLOCKUNTIL(cond) gasneti_polluntil(cond)
#else
  #define GASNET_BLOCKUNTIL(cond) do { \
      while (!(cond)) {                \
        GASNETI_WAITHOOK();            \
      }                                \
      gasneti_sync_reads();            \
    } while (0)
#endif
/* ------------------------------------------------------------------------------------ */

GASNETI_END_EXTERNC

#endif

#include <gasnet_ammacros.h>

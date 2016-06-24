/*   $Source: bitbucket.org:berkeleylab/gasnet.git/mpi-conduit/gasnet_core.h $
 * Description: GASNet header for MPI conduit core
 * Copyright 2002, Dan Bonachea <bonachea@cs.berkeley.edu>
 * Terms of use are as specified in license.txt
 */

#ifndef _IN_GASNETEX_H
  #error This file is not meant to be included directly- clients should include gasnetex.h
#endif

#ifndef _GASNET_CORE_H
#define _GASNET_CORE_H

#include <ammpi.h>

#include <gasnet_core_help.h>

GASNETI_BEGIN_EXTERNC

/*  TODO enhance AMMPI to support thread-safe MPI libraries */
/*  TODO add MPI bypass to loopback messages */

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
#if (GASNETI_MPI_VERSION >= 2)
  #define GASNET_NULL_ARGV_OK 1
#else
  #define GASNET_NULL_ARGV_OK 0
#endif
/* ------------------------------------------------------------------------------------ */
/*
  Handler-safe locks
  ==================
*/
#if GASNETC_HSL_ERRCHECK
  /* "magic" tag bit patterns that let us probabilistically detect
     the attempted use of uninitialized locks, or re-initialization of locks
   */
  #define GASNETC_HSL_ERRCHECK_TAGINIT ((uint64_t)0x5C9B5F7E9272EBA5ULL)
  #define GASNETC_HSL_ERRCHECK_TAGDYN  ((uint64_t)0xB82F6C0DE19C8F3DULL)
#endif

typedef struct _gasnet_hsl_t {
  gasneti_mutex_t lock;

  #if GASNETI_STATS_OR_TRACE
    gasneti_tick_t acquiretime;
  #endif

  #if GASNETC_HSL_ERRCHECK
    uint64_t tag;
    int islocked;
    gasneti_tick_t timestamp;
    struct _gasnet_hsl_t *next;
  #endif
} gasnet_hsl_t GASNETI_THREAD_TYPEDEF;

#if GASNETI_STATS_OR_TRACE
  #define GASNETC_LOCK_STAT_INIT ,0 
#else
  #define GASNETC_LOCK_STAT_INIT  
#endif

#if GASNETC_HSL_ERRCHECK
  #define GASNETC_LOCK_ERRCHECK_INIT , GASNETC_HSL_ERRCHECK_TAGINIT, 0, 0, NULL
#else
  #define GASNETC_LOCK_ERRCHECK_INIT 
#endif

#define GASNET_HSL_INITIALIZER { \
  GASNETI_MUTEX_INITIALIZER      \
  GASNETC_LOCK_STAT_INIT         \
  GASNETC_LOCK_ERRCHECK_INIT     \
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

#if GASNET_PSHM && GASNETC_HSL_ERRCHECK && !GASNETC_NULL_HSL
  extern void gasnetc_enteringHandler_hook_hsl(int cat, int isReq, int handlerId, gasnetex_token_t token,
                                               void *buf, size_t nbytes, int numargs,
                                               gasnetex_handlerarg_t *args);
  extern void gasnetc_leavingHandler_hook_hsl(int cat, int isReq);

  #define GASNETC_ENTERING_HANDLER_HOOK gasnetc_enteringHandler_hook_hsl
  #define GASNETC_LEAVING_HANDLER_HOOK  gasnetc_leavingHandler_hook_hsl
#endif


/* ------------------------------------------------------------------------------------ */
/*
  Active Message Size Limits
  ==========================
*/

#define gasnet_AMMaxArgs()          ((size_t)AM_MaxShort())
#if GASNET_PSHM
  #define gasnetex_lub_AMRequestMedium() ((size_t)MIN(AM_MaxMedium(), GASNETI_MAX_MEDIUM_PSHM))
  #define gasnetex_lub_AMReplyMedium()   ((size_t)MIN(AM_MaxMedium(), GASNETI_MAX_MEDIUM_PSHM))
#else
  #define gasnetex_lub_AMRequestMedium() ((size_t)AM_MaxMedium())
  #define gasnetex_lub_AMReplyMedium()   ((size_t)AM_MaxMedium())
#endif
#define gasnetex_lub_AMRequestLong()     ((size_t)AM_MaxLong())
#define gasnetex_lub_AMReplyLong()       ((size_t)AM_MaxLong())

  // TODO-EX: Can these be improved upon, at least for PSHM case
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

#define GASNET_BLOCKUNTIL(cond) gasneti_polluntil(cond)

/* ------------------------------------------------------------------------------------ */

GASNETI_END_EXTERNC

#endif

#include <gasnet_ammacros.h>

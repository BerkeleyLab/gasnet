/*   $Source: /Users/kamil/work/gasnet-cvs2/gasnet/vapi-conduit/Attic/gasnet_core.h,v $
 *     $Date: 2004/10/08 07:47:33 $
 * $Revision: 1.24 $
 * Description: GASNet header for vapi conduit core
 * Copyright 2002, Dan Bonachea <bonachea@cs.berkeley.edu>
 * Terms of use are as specified in license.txt
 */

#ifndef _IN_GASNET_H
  #error This file is not meant to be included directly- clients should include gasnet.h
#endif

#ifndef _GASNET_CORE_H
#define _GASNET_CORE_H

#include <gasnet_core_help.h>

BEGIN_EXTERNC

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

extern void gasnetc_exit(int exitcode) GASNET_NORETURN;
#define gasnet_exit gasnetc_exit

extern uintptr_t gasnetc_getMaxLocalSegmentSize();
extern uintptr_t gasnetc_getMaxGlobalSegmentSize();
#define gasnet_getMaxLocalSegmentSize   gasnetc_getMaxLocalSegmentSize 
#define gasnet_getMaxGlobalSegmentSize gasnetc_getMaxGlobalSegmentSize 

/* ------------------------------------------------------------------------------------ */
/*
  Job Environment Queries
  =======================
*/
extern int gasnetc_getSegmentInfo(gasnet_seginfo_t *seginfo_table, int numentries);

GASNET_INLINE_MODIFIER(gasnet_mynode)
gasnet_node_t gasnet_mynode() {
  GASNETI_CHECKINIT();
  return gasnetc_mynode;
}
 
GASNET_INLINE_MODIFIER(gasnet_nodes)
gasnet_node_t gasnet_nodes() {
  GASNETI_CHECKINIT();
  return gasnetc_nodes;
}

#define gasnet_getSegmentInfo gasnetc_getSegmentInfo

/* ------------------------------------------------------------------------------------ */
/*
  No-interrupt sections
  =====================
*/
/* conduit may or may not need this based on whether interrupts are used for running handlers */
#if GASNETC_USE_INTERRUPTS
  extern void gasnetc_hold_interrupts();
  extern void gasnetc_resume_interrupts();

  #define gasnet_hold_interrupts    gasnetc_hold_interrupts
  #define gasnet_resume_interrupts  gasnetc_resume_interrupts
#else
  #define gasnet_hold_interrupts()
  #define gasnet_resume_interrupts()
#endif

/* ------------------------------------------------------------------------------------ */
/*
  Handler-safe locks
  ==================
*/
typedef struct _gasnet_hsl_t {
  gasneti_mutex_t lock;

  #if GASNETI_STATS_OR_TRACE
    gasneti_stattime_t acquiretime;
  #endif

  #if GASNETC_USE_INTERRUPTS
    /* more state may be required for conduits using interrupts */
    #error interrupts not implemented
  #endif
} gasnet_hsl_t;

#if GASNETI_STATS_OR_TRACE
  #define GASNETC_LOCK_STAT_INIT ,0 
#else
  #define GASNETC_LOCK_STAT_INIT  
#endif

#if GASNETC_USE_INTERRUPTS
  #error interrupts not implemented
  #define GASNETC_LOCK_INTERRUPT_INIT 
#else
  #define GASNETC_LOCK_INTERRUPT_INIT  
#endif

#define GASNET_HSL_INITIALIZER { \
  GASNETI_MUTEX_INITIALIZER      \
  GASNETC_LOCK_STAT_INIT         \
  GASNETC_LOCK_INTERRUPT_INIT    \
  }

/* decide whether we have "real" HSL's */
#if GASNETI_THREADS || GASNETC_USE_INTERRUPTS || /* need for safety */ \
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
  extern int  gasnetc_hsl_trylock(gasnet_hsl_t *hsl);

  #define gasnet_hsl_init    gasnetc_hsl_init
  #define gasnet_hsl_destroy gasnetc_hsl_destroy
  #define gasnet_hsl_lock    gasnetc_hsl_lock
  #define gasnet_hsl_unlock  gasnetc_hsl_unlock
  #define gasnet_hsl_trylock gasnetc_hsl_trylock
#endif
/* ------------------------------------------------------------------------------------ */
/* Type and ops for rdma counters */
#include <gasnet_atomicops.h> /* must come after hsl defs */
typedef gasneti_atomic_t gasnetc_counter_t;
#define GASNETC_COUNTER_INITIALIZER	gasneti_atomic_init(0)
#define gasnetc_counter_reset(P)	gasneti_atomic_set((P), 0)
#define gasnetc_counter_done(P)		(gasneti_atomic_read(P) == 0)
#define gasnetc_counter_inc(P)		gasneti_atomic_increment(P)
#define gasnetc_counter_dec(P)		gasneti_atomic_decrement(P)
#if GASNETI_STATS_OR_TRACE
  #define gasnetc_counter_val(P)	gasneti_atomic_read(P)
#endif

/* Wait until given counter is marked as done.
 * Note that no AMPoll is done in the best case.
 */
extern void gasnetc_counter_wait_aux(gasnetc_counter_t *counter, int handler_context);
GASNET_INLINE_MODIFIER(gasnetc_counter_wait)
void gasnetc_counter_wait(gasnetc_counter_t *counter, int handler_context) { 
  if_pf (!gasnetc_counter_done(counter)) {
    gasnetc_counter_wait_aux(counter, handler_context);
  }
  gasneti_sync_reads();
} 
/* ------------------------------------------------------------------------------------ */
/*
  Active Message Size Limits
  ==========================
*/

/* Want to use GASNETI_ALIGN*, but those have not been seen yet */
#define GASNETC_ALIGNDOWN(p,P)	((uintptr_t)(p)&~((uintptr_t)(P)-1))
#define GASNETC_ALIGNUP(p,P)	(GASNETC_ALIGNDOWN((uintptr_t)(p)+((P)-1),P))

#define GASNETC_BUFSZ		4096
#define GASNETC_MEDIUM_HDRSZ	4
#define GASNETC_LONG_HDRSZ	(4 + SIZEOF_VOID_P)

#define GASNETC_MAX_ARGS	16
#define GASNETC_MAX_MEDIUM	\
		(GASNETC_BUFSZ - GASNETC_ALIGNUP(GASNETC_MEDIUM_HDRSZ + 4*GASNETC_MAX_ARGS, 8))
#define GASNETC_MAX_LONG_REQ	((size_t)0x7ffffff)
#if GASNETC_PIN_SEGMENT
  #define GASNETC_MAX_LONG_REP  GASNETC_MAX_LONG_REQ
#else
  #define GASNETC_MAX_LONG_REP  (GASNETC_BUFSZ - GASNETC_LONG_HDRSZ - 4*GASNETC_MAX_ARGS)
#endif

#define gasnet_AMMaxArgs()          ((size_t)GASNETC_MAX_ARGS)
#define gasnet_AMMaxMedium()        ((size_t)GASNETC_MAX_MEDIUM)
#define gasnet_AMMaxLongRequest()   ((size_t)GASNETC_MAX_LONG_REQ)	
#define gasnet_AMMaxLongReply()     ((size_t)GASNETC_MAX_LONG_REP)

/* ------------------------------------------------------------------------------------ */
/*
  Misc. Active Message Functions
  ==============================
*/
extern int gasnetc_AMGetMsgSource(gasnet_token_t token, gasnet_node_t *srcindex);
extern int gasnetc_AMPoll();

#define gasnet_AMGetMsgSource  gasnetc_AMGetMsgSource

#define GASNET_BLOCKUNTIL(cond) gasneti_polluntil(cond)

/* ------------------------------------------------------------------------------------ */
/*
  System AM Request/Reply Functions
  =================================
*/

extern int gasnetc_RequestSystem( 
                            gasnet_node_t dest,       /* destination node */
			    gasnetc_counter_t *req_oust, /* counter to wait for send */
                            gasnet_handler_t handler, /* index into destination endpoint's handler table */ 
                            int numargs, ...);

extern int gasnetc_ReplySystem( 
                            gasnet_token_t token,     /* token provided on handler entry */
			    gasnetc_counter_t *req_oust, /* counter to wait for send */
                            gasnet_handler_t handler, /* index into destination endpoint's handler table */ 
                            int numargs, ...);

/* ------------------------------------------------------------------------------------ */
/*
  RDMA ops
  =====================
 */

/* RDMA initiation operations */
extern int gasnetc_rdma_put(int node, void *src_ptr, void *dst_ptr, uintptr_t nbytes, gasnetc_counter_t *mem_oust, gasnetc_counter_t *req_oust);
extern int gasnetc_rdma_get(int node, void *src_ptr, void *dst_ptr, size_t nbytes, gasnetc_counter_t *req_oust);
extern int gasnetc_rdma_memset(int node, void *dst_ptr, int val, size_t nbytes, gasnetc_counter_t *req_oust);

/* ------------------------------------------------------------------------------------ */

END_EXTERNC

#endif

#include <gasnet_ammacros.h>

/*  $Archive:: /Ti/GASNet/extended-ref/gasnet_extended_amambarrier.c                  $
 *     $Date: 2003/12/06 13:25:47 $
 * $Revision: 1.1 $
 * Description: Reference implemetation of GASNet Barrier, using Active Messages
 * Copyright 2002, Dan Bonachea <bonachea@cs.berkeley.edu>
 * Terms of use are as specified in license.txt
 */

#ifndef GASNETI_GASNET_EXTENDED_REFBARRIER_C
  #error This file not meant to be compiled directly - included by gasnet_extended.c
#endif

/*  TODO: add more reference barrier implementation options (bug 264) */

/* ------------------------------------------------------------------------------------ */
/* use the AM-based reference implementation of barrier */
#define gasnete_ambarrier_notify      gasnete_refbarrier_notify
#define gasnete_ambarrier_wait        gasnete_refbarrier_wait
#define gasnete_ambarrier_try         gasnete_refbarrier_try
#define GASNETE_REFBARRIER_HANDLERS   GASNETE_AMBARRIER_HANDLERS

/*  a silly, centralized barrier implementation:
     everybody sends notifies to a single node, where we count them up
     central node eventually notices the barrier is complete (probably
     when it calls wait) and then it broadcasts the completion to all the nodes
    The main problem is the need for the master to call wait before the barrier can
     make progress - we really need a way for the "last thread" to notify all 
     the threads when completion is detected, but AM semantics don't provide a 
     simple way to do this.
    The centralized nature also makes it non-scalable - we really want to use 
     a tree-based barrier or pairwise exchange algorithm for scalability
     (but these impose even greater potential delays due to the lack of attentiveness to
     barrier progress)
 */

static enum { OUTSIDE_AMBARRIER, INSIDE_AMBARRIER } ambarrier_splitstate = OUTSIDE_AMBARRIER;
static int volatile ambarrier_value; /*  local ambarrier value */
static int volatile ambarrier_flags; /*  local ambarrier flags */
static int volatile ambarrier_phase = 0;  /*  2-phase operation to improve pipelining */
static int volatile ambarrier_response_done[2] = { 0, 0 }; /*  non-zero when ambarrier is complete */
static int volatile ambarrier_response_mismatch[2] = { 0, 0 }; /*  non-zero if we detected a mismatch */
#if GASNETI_STATS_OR_TRACE
  static gasneti_stattime_t ambarrier_notifytime; /* for statistical purposes */ 
#endif

/*  global state on P0 */
#ifndef GASNETE_AMBARRIER_MASTER
  #define GASNETE_AMBARRIER_MASTER (gasnete_nodes-1)
#endif
static gasnet_hsl_t ambarrier_lock = GASNET_HSL_INITIALIZER;
static int volatile ambarrier_consensus_value[2]; /*  consensus ambarrier value */
static int volatile ambarrier_consensus_value_present[2] = { 0, 0 }; /*  consensus ambarrier value found */
static int volatile ambarrier_consensus_mismatch[2] = { 0, 0 }; /*  non-zero if we detected a mismatch */
static int volatile ambarrier_count[2] = { 0, 0 }; /*  count of how many remotes have notified (on P0) */

static void gasnete_ambarrier_notify_reqh(gasnet_token_t token, 
  gasnet_handlerarg_t phase, gasnet_handlerarg_t value, gasnet_handlerarg_t flags) {
  gasneti_assert(gasnete_mynode == GASNETE_AMBARRIER_MASTER);

  gasnet_hsl_lock(&ambarrier_lock);
  { int count = ambarrier_count[phase];
    if (flags == 0 && !ambarrier_consensus_value_present[phase]) {
      ambarrier_consensus_value[phase] = (int)value;
      ambarrier_consensus_value_present[phase] = 1;
    } else if (flags == GASNET_BARRIERFLAG_MISMATCH ||
               (flags == 0 && ambarrier_consensus_value[phase] != (int)value)) {
      ambarrier_consensus_mismatch[phase] = 1;
    }
    count++;
    if (count == gasnete_nodes) gasneti_memsync(); /* about to signal, ensure we flush state */
    ambarrier_count[phase] = count;
  }
  gasnet_hsl_unlock(&ambarrier_lock);
}

static void gasnete_ambarrier_done_reqh(gasnet_token_t token, 
  gasnet_handlerarg_t phase,  gasnet_handlerarg_t mismatch) {
  gasneti_assert(phase == ambarrier_phase);

  ambarrier_response_mismatch[phase] = mismatch;
  gasneti_memsync();
  ambarrier_response_done[phase] = 1;
}

/*  make some progress on the ambarrier */
static void gasnete_ambarrier_kick() {
  int phase = ambarrier_phase;
  GASNETE_SAFE(gasnet_AMPoll());

  if (gasnete_mynode != GASNETE_AMBARRIER_MASTER) return;

  /*  master does all the work */
  if (ambarrier_count[phase] == gasnete_nodes) {
    /*  ambarrier is complete */
    int i;
    int mismatch = ambarrier_consensus_mismatch[phase];

    /*  inform the nodes */
    for (i=0; i < gasnete_nodes; i++) {
      GASNETE_SAFE(
        gasnet_AMRequestShort2(i, gasneti_handleridx(gasnete_ambarrier_done_reqh), 
                             phase, mismatch));
    }

    /*  reset state */
    ambarrier_count[phase] = 0;
    ambarrier_consensus_mismatch[phase] = 0;
    ambarrier_consensus_value_present[phase] = 0;
  }
}

extern void gasnete_ambarrier_notify(int id, int flags) {
  int phase;
  if_pf(ambarrier_splitstate == INSIDE_AMBARRIER) 
    gasneti_fatalerror("gasnet_barrier_notify() called twice in a row");

  GASNETI_TRACE_PRINTF(B, ("AMBARRIER_NOTIFY(id=%i,flags=%i)", id, flags));
  #if GASNETI_STATS_OR_TRACE
    ambarrier_notifytime = GASNETI_STATTIME_NOW_IFENABLED(B);
  #endif

  ambarrier_value = id;
  ambarrier_flags = flags;
  phase = !ambarrier_phase; /*  enter new phase */
  ambarrier_phase = phase;

  if (gasnete_nodes > 1) {
    /*  send notify msg to 0 */
    GASNETE_SAFE(
      gasnet_AMRequestShort3(GASNETE_AMBARRIER_MASTER, gasneti_handleridx(gasnete_ambarrier_notify_reqh), 
                           phase, ambarrier_value, flags));
  } else {
    ambarrier_response_mismatch[phase] = (flags & GASNET_BARRIERFLAG_MISMATCH);
    ambarrier_response_done[phase] = 1;
  }

  /*  update state */
  ambarrier_splitstate = INSIDE_AMBARRIER;
  gasneti_memsync(); /* ensure all state changes committed before return */
}


extern int gasnete_ambarrier_wait(int id, int flags) {
  #if GASNETI_STATS_OR_TRACE
    gasneti_stattime_t wait_start = GASNETI_STATTIME_NOW_IFENABLED(B);
  #endif
  int phase = ambarrier_phase;
  if_pf(ambarrier_splitstate == OUTSIDE_AMBARRIER) 
    gasneti_fatalerror("gasnet_ambarrier_wait() called without a matching notify");

  GASNETI_TRACE_EVENT_TIME(B,BARRIER_NOTIFYWAIT,GASNETI_STATTIME_NOW()-ambarrier_notifytime);

  /*  wait for response */
  while (!ambarrier_response_done[phase]) {
    gasnete_ambarrier_kick();
  }

  GASNETI_TRACE_EVENT_TIME(B,BARRIER_WAIT,GASNETI_STATTIME_NOW()-wait_start);

  /*  update state */
  ambarrier_splitstate = OUTSIDE_AMBARRIER;
  ambarrier_response_done[phase] = 0;
  gasneti_memsync(); /* ensure all state changes committed before return */
  if_pf((!(flags & GASNET_BARRIERFLAG_ANONYMOUS) && id != ambarrier_value) || 
        flags != ambarrier_flags || 
        ambarrier_response_mismatch[phase]) {
        ambarrier_response_mismatch[phase] = 0;
        return GASNET_ERR_BARRIER_MISMATCH;
  }
  else return GASNET_OK;
}

extern int gasnete_ambarrier_try(int id, int flags) {
  if_pf(ambarrier_splitstate == OUTSIDE_AMBARRIER) 
    gasneti_fatalerror("gasnet_ambarrier_try() called without a matching notify");

  gasnete_ambarrier_kick();

  if (ambarrier_response_done[ambarrier_phase]) {
    GASNETI_TRACE_EVENT_VAL(B,BARRIER_TRY,1);
    return gasnete_ambarrier_wait(id, flags);
  }
  else {
    GASNETI_TRACE_EVENT_VAL(B,BARRIER_TRY,0);
    return GASNET_ERR_NOT_READY;
  }
}

#define GASNETE_AMBARRIER_HANDLERS()                                 \
  gasneti_handler_tableentry_no_bits(gasnete_ambarrier_notify_reqh), \
  gasneti_handler_tableentry_no_bits(gasnete_ambarrier_done_reqh)  

/* ------------------------------------------------------------------------------------ */

/*  $Archive:: /Ti/GASNet/extended-ref/gasnet_extended_amambarrier.c                  $
 *     $Date: 2004/03/12 18:32:06 $
 * $Revision: 1.10 $
 * Description: Reference implemetation of GASNet Barrier, using Active Messages
 * Copyright 2002, Dan Bonachea <bonachea@cs.berkeley.edu>
 * Terms of use are as specified in license.txt
 */

#ifndef GASNETI_GASNET_EXTENDED_REFBARRIER_C
  #error This file not meant to be compiled directly - included by gasnet_extended.c
#endif

#include <limits.h>

/*  TODO: add more reference barrier implementation options (bug 264) */

/* Default is the original AM-based centralized barrier, the last one in this file */

#ifdef GASNETE_USE_AMDISSEMINATION_REFBARRIER
/* ------------------------------------------------------------------------------------ */
/* use the AM-based Dissemination implementation of barrier */
#define gasnete_ambarrier_notify      gasnete_refbarrier_notify
#define gasnete_ambarrier_wait        gasnete_refbarrier_wait
#define gasnete_ambarrier_try         gasnete_refbarrier_try
#define GASNETE_REFBARRIER_HANDLERS   GASNETE_AMBARRIER_HANDLERS

/*  an AM-based Dissemination barrier implementation:
     With N nodes, the barrier takes ceil(lg(N)) steps (lg = log-base-2).
     At step i (i=0..):
	node n first sends to node ((n + 2^i) mod N)
	then node n waits to receive (from node ((n + N - 2^i) mod N))
	once we receive for step i, we can move the step i+1 (or finish)
    The distributed nature makes this barrier more scalable than a centralized
     barrier, but also more sensitive to any lack of attentiveness to the
     network.
    Also since we don't currently have an _init hook for the barriers, we use a
     static allocation, limiting us to 2^GASNETE_AMBARRIER_MAXSTEP nodes.

    Algorithm is described in section 3.3 of
    John M. Mellor-Crummey and Michael L. Scott. "Algorithms for scalable synchronization
    on shared-memory multiprocessors." ACM ToCS, 9(1):21 65, 1991.
 */

#ifndef GASNETE_AMBARRIER_MAXSTEP
  #define GASNETE_AMBARRIER_MAXSTEP 32
#endif

static gasnet_hsl_t ambarrier_lock = GASNET_HSL_INITIALIZER;
static enum { OUTSIDE_AMBARRIER, INSIDE_AMBARRIER } ambarrier_splitstate = OUTSIDE_AMBARRIER;
static int volatile ambarrier_value; /*  local ambarrier value */
static int volatile ambarrier_flags; /*  local ambarrier flags */
static int volatile ambarrier_step;  /*  local ambarrier step */
static int volatile ambarrier_size = -1;  /*  ceil(lg(nodes)), or -1 if uninitialized */
static int volatile ambarrier_phase = 0;  /*  2-phase operation to improve pipelining */
static int volatile ambarrier_step_done[2][GASNETE_AMBARRIER_MAXSTEP] = { {0} }; /*  non-zero when a step is complete */
static int volatile ambarrier_mismatch[2] = { 0, 0 }; /*  non-zero if we detected a mismatch */
static int volatile ambarrier_recv_value[2]; /*  consensus ambarrier value */
static int volatile ambarrier_recv_value_present[2] = { 0, 0 }; /*  consensus ambarrier value is present */
#if GASNETI_STATS_OR_TRACE
  static gasneti_stattime_t ambarrier_notifytime; /* for statistical purposes */ 
#endif

static void gasnete_ambarrier_init(void)
{
  int i, j;

  gasneti_assert(ambarrier_size < 0);

  /* determine barrier size (number of steps) */
  for (i=0, j=1; j < gasnete_nodes; ++i, j*=2) ;

  ambarrier_size = i;
}

static void gasnete_ambarrier_notify_reqh(gasnet_token_t token, 
  gasnet_handlerarg_t phase, gasnet_handlerarg_t step, gasnet_handlerarg_t value, gasnet_handlerarg_t flags) {

  gasnet_hsl_lock(&ambarrier_lock);
  if_pf (ambarrier_size < 0) {
    gasnete_ambarrier_init();
  }
  { 
    /* Note we might not receive the steps in the numbered order.
     * We record the value received on the first one to actually arrive.
     * In subsequent steps we check for mismatch of received values.
     * The local value is compared at the start of step == 1.
     */
    if (flags == 0 && !ambarrier_recv_value_present[phase]) {
      ambarrier_recv_value_present[phase] = 1;
      ambarrier_recv_value[phase] = (int)value;
    } else if (flags == GASNET_BARRIERFLAG_MISMATCH ||
               (flags == 0 && ambarrier_recv_value[phase] != (int)value)) {
      ambarrier_mismatch[phase] = 1;
    }
    
    /* gasneti_assert(ambarrier_step_done[phase][step] == 0); */
    
    gasneti_memsync();
    ambarrier_step_done[phase][step] = 1;
  }
  gasnet_hsl_unlock(&ambarrier_lock);
}

static void gasnete_ambarrier_kick() {
  int phase = ambarrier_phase;
  int step = ambarrier_step;
  GASNETE_SAFE(gasnet_AMPoll());

  if_pt (step != ambarrier_size) {
    gasnet_hsl_lock(&ambarrier_lock);
    if (ambarrier_step_done[phase][step]) {
      /* If step==0 this is the first time we are certain we have both a local and remote id */
      if_pf ((step == 0) &&
	     (ambarrier_flags == 0) && 
	     (ambarrier_recv_value[phase] != ambarrier_value)) {
        ambarrier_flags = GASNET_BARRIERFLAG_MISMATCH;
      }

      ++step;
      if (step == ambarrier_size) {
	/* We have the last recv.  There is nothing more to send. */
        gasnet_hsl_unlock(&ambarrier_lock);
	gasneti_memsync();
      } else {
        gasnet_node_t peer;
        gasnet_handlerarg_t flags = ambarrier_mismatch[phase] ? GASNET_BARRIERFLAG_MISMATCH
							      : ambarrier_flags;
        gasnet_hsl_unlock(&ambarrier_lock);

	/* No need for a full mod because worst case is < 2*gasnete_nodes.
	 * However, we must take care for overflow if we try to do the
	 * arithmetic in gasnet_node_t.  An example is gasnet_node_t
	 * of uint8_t and gasnete_nodes=250 nodes.  The largest value of
	 * gasnet_mynode is 249 and the largest value of 2^step is 128.
	 * We can't compute (249 + 128) mod 250 in 8-bit arithmetic.
	 * If we are using GASNET_MAXNODES <= INT_MAX then we can
	 * fit the arithmetic into unsigned integers (32-bit example is
	 * 0x7ffffffe + 0x40000000 = 0xbffffffe).  Otherwise we are
	 * confident that 64-bit integers are ALWAYS large enough.
	 */
	{
	  #if (GASNET_MAXNODES <= INT_MAX)
	    unsigned int tmp;
	  #else
	    uint64_t tmp;
	  #endif
	  tmp = (1 << step) + gasnete_mynode;
	  peer = (tmp >= gasnete_nodes) ? (tmp - gasnete_nodes)
                                        : tmp;
	  gasneti_assert(peer < gasnete_nodes);
	}

        GASNETE_SAFE(
          gasnet_AMRequestShort4(peer, gasneti_handleridx(gasnete_ambarrier_notify_reqh), 
                                 phase, step, ambarrier_value, flags));
      }
      ambarrier_step = step;
    } else {
      gasnet_hsl_unlock(&ambarrier_lock);
    }
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

  if_pf (ambarrier_size < 0) {
    gasnete_ambarrier_init();
    gasneti_assert (ambarrier_size <= GASNETE_AMBARRIER_MAXSTEP);
  }

  /* If we are on an ILP64 platform, this cast will ensure we truncate the same
   * bits locally as we do when passing over the network.
   */
  ambarrier_value = (gasnet_handlerarg_t)id;

  ambarrier_flags = flags;
  phase = !ambarrier_phase; /*  enter new phase */
  ambarrier_phase = phase;
  ambarrier_step = 0;

  if (gasnete_nodes > 1) {
    /*  send notify msg to peer */
    gasnet_node_t peer = ((gasnete_mynode + 1) < gasnete_nodes) ? (gasnete_mynode + 1) : 0;
    GASNETE_SAFE(
      gasnet_AMRequestShort4(peer, gasneti_handleridx(gasnete_ambarrier_notify_reqh), 
                             phase, 0, id, flags));
  } else {
    ambarrier_recv_value[phase] = id;	/* to similify checking in _wait */
  }

  if_pf (flags & GASNET_BARRIERFLAG_MISMATCH) {
    ambarrier_mismatch[phase] = 1;
  }

  /*  update state */
  ambarrier_splitstate = INSIDE_AMBARRIER;
  gasneti_memsync(); /* ensure all state changes committed before return */
}


extern int gasnete_ambarrier_wait(int id, int flags) {
  int retval = GASNET_OK;
  int i;

  #if GASNETI_STATS_OR_TRACE
    gasneti_stattime_t wait_start = GASNETI_STATTIME_NOW_IFENABLED(B);
  #endif
  int phase = ambarrier_phase;
  if_pf(ambarrier_splitstate == OUTSIDE_AMBARRIER) 
    gasneti_fatalerror("gasnet_ambarrier_wait() called without a matching notify");

  GASNETI_TRACE_EVENT_TIME(B,BARRIER_NOTIFYWAIT,GASNETI_STATTIME_NOW_IFENABLED(B)-ambarrier_notifytime);

  /*  wait for response */
  if (ambarrier_step != ambarrier_size) {
    GASNET_BLOCKUNTIL((gasnete_ambarrier_kick(), (ambarrier_step == ambarrier_size)));
  }

  GASNETI_TRACE_EVENT_TIME(B,BARRIER_WAIT,GASNETI_STATTIME_NOW_IFENABLED(B)-wait_start);

  /* determine return value */
  if_pf((!(flags & GASNET_BARRIERFLAG_ANONYMOUS) && (gasnet_handlerarg_t)id != ambarrier_value) || 
        flags != ambarrier_flags ||
	ambarrier_mismatch[phase]) {
        ambarrier_mismatch[phase] = 0;
	retval = GASNET_ERR_BARRIER_MISMATCH;
  }

  /*  update state */
  ambarrier_splitstate = OUTSIDE_AMBARRIER;
  for (i=0; i < ambarrier_size; ++i) {
    ambarrier_step_done[phase][i] = 0;
  }
  ambarrier_recv_value_present[phase] = 0;
  gasneti_memsync(); /* ensure all state changes committed before return */

  return retval;
}

extern int gasnete_ambarrier_try(int id, int flags) {
  if_pf(ambarrier_splitstate == OUTSIDE_AMBARRIER) 
    gasneti_fatalerror("gasnet_ambarrier_try() called without a matching notify");

  gasnete_ambarrier_kick();

  if (ambarrier_step == ambarrier_size) {
    GASNETI_TRACE_EVENT_VAL(B,BARRIER_TRY,1);
    return gasnete_ambarrier_wait(id, flags);
  }
  else {
    GASNETI_TRACE_EVENT_VAL(B,BARRIER_TRY,0);
    return GASNET_ERR_NOT_READY;
  }
}

#define GASNETE_AMBARRIER_HANDLERS()                                 \
  gasneti_handler_tableentry_no_bits(gasnete_ambarrier_notify_reqh)

/* ------------------------------------------------------------------------------------ */
#else	/* default */
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

  /* If we are on an ILP64 platform, this cast will ensure we truncate the same
   * bits locally as we do when passing over the network.
   */
  ambarrier_value = (gasnet_handlerarg_t)id;

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

  GASNETI_TRACE_EVENT_TIME(B,BARRIER_NOTIFYWAIT,GASNETI_STATTIME_NOW_IFENABLED(B)-ambarrier_notifytime);

  /*  wait for response */
  if (!ambarrier_response_done[phase])
    GASNET_BLOCKUNTIL((gasnete_ambarrier_kick(), ambarrier_response_done[phase]));

  GASNETI_TRACE_EVENT_TIME(B,BARRIER_WAIT,GASNETI_STATTIME_NOW_IFENABLED(B)-wait_start);

  /*  update state */
  ambarrier_splitstate = OUTSIDE_AMBARRIER;
  ambarrier_response_done[phase] = 0;
  gasneti_memsync(); /* ensure all state changes committed before return */
  if_pf((!(flags & GASNET_BARRIERFLAG_ANONYMOUS) && (gasnet_handlerarg_t)id != ambarrier_value) || 
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
#endif

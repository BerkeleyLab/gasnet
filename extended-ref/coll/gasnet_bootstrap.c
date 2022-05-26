/*   $Source: bitbucket.org:berkeleylab/gasnet.git/extended-ref/coll/gasnet_bootstrap.c $
 * Description: Conduit-independent AM-based bootstrap collectives
 * Copyright 2022, The Regents of the University of California
 * Terms of use are as specified in license.txt
 */

#include <coll/gasnet_coll_internal.h>
#include <gasnet_core_internal.h> // for handler indices

/* ------------------------------------------------------------------------------------ */
// Return a vector of peers for dissemination barrier or Bruck's GatherAll
// returns length, writes pointer to *out_p
// when length is 0, *out_p will be NULL
//
// Note that the only thread-safety provisions are guards against concurrent
// handler execution if GASNETI_CONDUIT_THREADS is asserted.  This is under
// the assumption that the only callers are serial initialization code and
// AM handlers such as those in this file.
gex_Rank_t gasneti_get_dissem_peers(gex_Rank_t **out_p)
{
  static gex_Rank_t result_len = 0;
  static gex_Rank_t *result_vec = NULL;
  static int is_init = 0;

#if GASNETI_CONDUIT_THREADS
  static gasneti_mutex_t lock = GASNETI_MUTEX_INITIALIZER;
  gasneti_mutex_lock(&lock);
#endif

  if (!is_init) {
    is_init = 1;

    gex_Rank_t size = gasneti_nodes;
    gex_Rank_t rank = gasneti_mynode;

    if (size > 1) {
      gasneti_assert_uint(result_len ,==, 0);
      for (gex_Rank_t i = 1; i < size; i *= 2) result_len += 1;

      result_vec = gasneti_malloc(result_len * sizeof(gex_Rank_t));
      gasneti_leak(result_vec);
      for (gex_Rank_t i = 0; i < result_len; ++i) {
        gex_Rank_t distance = 1 << i;
        result_vec[i] = (distance <= rank) ? (rank - distance) : (rank + (size - distance));
      }
    }
  }

#if GASNETI_CONDUIT_THREADS
  gasneti_mutex_unlock(&lock);
#endif

  *out_p = result_vec;
  return result_len;
}

#if GASNET_PSHM
// As above but consisting of just one "leader" per nbrhd
gex_Rank_t gasneti_get_dissem_peers_pshm(gex_Rank_t **out_p)
{
  static gex_Rank_t result_len = 0;
  static gex_Rank_t *result_vec = NULL;
  static int is_init = 0;

#if GASNETI_CONDUIT_THREADS
  static gasneti_mutex_t lock = GASNETI_MUTEX_INITIALIZER;
  gasneti_mutex_lock(&lock);
#endif

  if (!is_init) {
    is_init = 1;

    gex_Rank_t size = gasneti_nodemap_global_count;
    gex_Rank_t rank = gasneti_nodemap_global_rank;

    if (size > 1) {
      gasneti_assert_uint(result_len ,==, 0);
      for (gex_Rank_t i = 1; i < size; i *= 2) result_len += 1;

      result_vec = gasneti_malloc(result_len * sizeof(gex_Rank_t));
      gasneti_leak(result_vec);
      for (gex_Rank_t i = 0; i < result_len; ++i) {
        gex_Rank_t distance = 1 << i;
        gex_Rank_t peer = (distance <= rank) ? (rank - distance) : (rank + (size - distance));
        result_vec[i] = gasneti_pshm_firsts[peer];
      }
    }
  }

#if GASNETI_CONDUIT_THREADS
  gasneti_mutex_unlock(&lock);
#endif

  *out_p = result_vec;
  return result_len;
}
#endif

/* ------------------------------------------------------------------------------------ */
// Host-scoped (potentially superset of supernode) barrier
//
// Note a lack explicit thread-safety provisions, under the assumption that the
// only callers are serial initialization.  The use of atomics in the handler
// are sufficient to guard against concurrent hander execution by a conduit thread.

static gasneti_weakatomic32_t gasneti_hbarr_rcvd[2][32]; // Implicitly zero-initialized

extern void gasnetc_hbarr_reqh(gex_Token_t token, gex_AM_Arg_t arg0)
{
  const int phase = arg0 & 1;
  const int step = (arg0 >> 1) & 0x1f; // Max 2^5 steps => 2^32 proc/host!
  const int distance = (1 << step);
  gasneti_assert_uint(distance ,<, gasneti_myhost.node_count);
  gasneti_weakatomic32_increment(&gasneti_hbarr_rcvd[phase][step], GASNETI_ATOMIC_REL);
}

void gasneti_host_barrier(void)
{
  // Simple dissemination barrier with two phase
  static int phase = 0;
  const gex_Rank_t rank = gasneti_myhost.node_rank;
  const gex_Rank_t size = gasneti_myhost.node_count;
  for (unsigned int step = 0, distance = 1; distance < size; ++step, distance *= 2) {
    gex_Rank_t peer = (distance <= rank) ? rank - distance : rank + (size - distance);
    gex_AM_Arg_t arg0 = phase | (step << 1);

    gex_AM_RequestShort(gasneti_THUNK_TM, gasneti_myhost.nodes[peer],
                        gasneti_handleridx(gasnetc_hbarr_reqh), 0, arg0);

    // Poll until we have received the same phase we've just sent
    GASNET_BLOCKUNTIL((int)gasneti_weakatomic32_read(&gasneti_hbarr_rcvd[phase][step], 0));
    gasneti_assert_int((int)gasneti_weakatomic32_read(&gasneti_hbarr_rcvd[phase][step], 0) ,==, 1);
    gasneti_weakatomic32_set(&gasneti_hbarr_rcvd[phase][step], 0, 0);
  }

#if GASNET_PSHM
  // Cannot use AMPSHM and pshm bootstrap collectives in same pshmnet barrier phase
  gasneti_pshmnet_bootstrapBarrierPoll();
#endif

  phase ^= 1;
}

/* ------------------------------------------------------------------------------------ */
// Host-scoped (potentially superset of supernode) reduce-to-all(SUM, uint64_t)
//
// Note a lack explicit thread-safety provisions, under the assumption that the
// only callers are serial initialization.  The use of atomics in the handler
// are sufficient to guard against concurrent hander execution by a conduit thread.

// Following are both implicitly zero-initialized:
static gasneti_weakatomic32_t gasneti_hsumu64_rcvd;
static gasneti_weakatomic64_t gasneti_hsumu64_sum;

extern void gasnetc_hsumu64_reqh(gex_Token_t token, gex_AM_Arg_t arg0, gex_AM_Arg_t arg1)
{
  uint64_t operand = GASNETI_MAKEWORD(arg0, arg1);
  gasneti_weakatomic64_add(&gasneti_hsumu64_sum, operand, 0);
  gasneti_weakatomic32_increment(&gasneti_hsumu64_rcvd, GASNETI_ATOMIC_REL);
}

uint64_t gasneti_host_sumu64(uint64_t operand)
{
  // Sum on the way up a binimial tree, and then broadcast down the same tree
  const gex_Rank_t rank = gasneti_myhost.node_rank;
  const gex_Rank_t size = gasneti_myhost.node_count;
  gasneti_assert_uint(size ,<, 0x80000000); // otherwise some of the math below goes wrong
  const gex_Rank_t remain = size - rank;
  const gex_Rank_t fullsize = (rank & (-rank));
  const gex_Rank_t subsize = (!fullsize || (fullsize > remain)) ? remain : fullsize;
  const gex_Rank_t children = 1 + gasnete_coll_log2_rank(subsize - 1);
  const gex_Rank_t parent = rank - fullsize;

  gasneti_weakatomic32_t *rcvd_counter = &gasneti_hsumu64_rcvd;
  gasneti_weakatomic64_t *sum = &gasneti_hsumu64_sum;
  uint64_t result = operand;

  if (children) {
    // 1. wait for contributions from children
    GASNET_BLOCKUNTIL((gex_Rank_t)gasneti_weakatomic32_read(rcvd_counter,0) == children);
    result += gasneti_weakatomic64_read(sum,0);
    gasneti_weakatomic64_set(sum,0,0);
  }

  if (rank) {
    // 2. send partial result to parent
    gex_AM_RequestShort(gasneti_THUNK_TM, gasneti_myhost.nodes[parent],
                        gasneti_handleridx(gasnetc_hsumu64_reqh), 0,
                        GASNETI_HIWORD(result), GASNETI_LOWORD(result));
    // 3. wait for final result from parent
    GASNET_BLOCKUNTIL((gex_Rank_t)gasneti_weakatomic32_read(rcvd_counter,0) == children + 1);
    result = gasneti_weakatomic64_read(sum,0);
    gasneti_weakatomic64_set(sum,0,0);
  }

  // 4. reset state for next time
  gasneti_weakatomic32_set(rcvd_counter,0,0);

  if (children) {
    // 5. forward result to children
    uint32_t arg0 = GASNETI_HIWORD(result);
    uint32_t arg1 = GASNETI_LOWORD(result);
    for (int idx = children - 1; idx >= 0; --idx) { // Reverse order for deepest subtree first
      gex_Rank_t peer = gasneti_myhost.nodes[rank + (1 << idx)];
      gex_AM_RequestShort(gasneti_THUNK_TM, peer,
                          gasneti_handleridx(gasnetc_hsumu64_reqh), 0,
                          arg0, arg1);
    }
  }

  return result;
}

/* ------------------------------------------------------------------------------------ */
// Bootstrap collectives using AM Requests
// These require Short and/or Medium Requests, plus GASNET_BLOCKUNTIL.
// No use is currently made of Reply or Long.
//
// Currently Barrier is the only operation implemented.
// TODO: more operations
//
// Note that the only thread-safety provisions are those needed to allow for
// concurrent handler execution, and only if GASNETI_CONDUIT_THREADS is
// asserted.  This is under the assumption that the only callers are serial
// initialization code.

//
// BARRIER
//

static int gasneti_am_barrier_phase = 0;

static uint32_t gasneti_am_barrier_rcvd[2] = {0, 0};
#if GASNETI_CONDUIT_THREADS
  // MUTEX
  static gasneti_mutex_t gasneti_am_barrier_lock = GASNETI_MUTEX_INITIALIZER;
  static void gasneti_am_barrier_arrival(int phase, uint32_t bit) {
    gasneti_mutex_lock(&gasneti_am_barrier_lock);
    gasneti_am_barrier_rcvd[phase] |= bit;
    gasneti_mutex_unlock(&gasneti_am_barrier_lock);
  }
  static uint32_t gasneti_am_barrier_read(int phase) {
    gasneti_mutex_lock(&gasneti_am_barrier_lock);
    uint32_t result = gasneti_am_barrier_rcvd[phase];
    gasneti_mutex_unlock(&gasneti_am_barrier_lock);
    return result;
  }
  static void gasneti_am_barrier_reset(int phase) {
    gasneti_mutex_lock(&gasneti_am_barrier_lock);
    gasneti_am_barrier_rcvd[phase] = 0;
    gasneti_mutex_unlock(&gasneti_am_barrier_lock);
  }
#else
  // SERIAL
  #define gasneti_am_barrier_arrival(phase,bit) \
    ((void)(gasneti_am_barrier_rcvd[phase] |= (bit)))
  #define gasneti_am_barrier_read(phase) \
    (gasneti_am_barrier_rcvd[phase])
  #define gasneti_am_barrier_reset(phase) \
    ((void)(gasneti_am_barrier_rcvd[phase] = 0))
#endif

extern void gasnetc_am_barrier_reqh(gex_Token_t token, gex_AM_Arg_t arg)
{
  uint32_t phase = arg & 1;
  uint32_t bit = arg ^ phase;
  gasneti_assert(GASNETI_POWEROFTWO(bit));
  gasneti_am_barrier_arrival(phase,bit);
}

extern void gasneti_bootstrapBarrier_am(void)
{
    gex_Rank_t *peer;
#if GASNET_PSHM
    gasneti_pshmnet_bootstrapBarrier();
    gex_Rank_t size = gasneti_nodemap_local_rank
                    ? 0 // not leader -> no network comms
                    : gasneti_get_dissem_peers_pshm(&peer);
#else
    gex_Rank_t size = gasneti_get_dissem_peers(&peer);
#endif

    int phase = gasneti_am_barrier_phase;
    for (gex_Rank_t i = 0; i < size; ++i) { // empty for PSHM/non-leader
      const uint32_t bit = 2 << i; // (distance << 1), since phase is low bit

      (void) gex_AM_RequestShort1(gasneti_THUNK_TM, peer[i],
                                  gasneti_handleridx(gasnetc_am_barrier_reqh),
                                  0, phase | bit);

      // wait for completion of the proper receive, which might arrive out of order
      GASNET_BLOCKUNTIL(gasneti_am_barrier_read(phase) & bit);
    }

#if GASNET_PSHM
    gasneti_pshmnet_bootstrapBarrier();
#endif

    // reset for next barrier
    gasneti_am_barrier_reset(phase);
    gasneti_am_barrier_phase = !phase;
}

/* ------------------------------------------------------------------------------------ */

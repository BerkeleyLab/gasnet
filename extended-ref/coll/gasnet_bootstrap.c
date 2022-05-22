/*   $Source: bitbucket.org:berkeleylab/gasnet.git/extended-ref/coll/gasnet_bootstrap.c $
 * Description: Conduit-independent AM-based bootstrap collectives
 * Copyright 2022, The Regents of the University of California
 * Terms of use are as specified in license.txt
 */

#include <coll/gasnet_coll_internal.h>
#include <gasnet_core_internal.h> // for handler indices

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

/*   $Source: bitbucket.org:berkeleylab/gasnet.git/extended-ref/coll/gasnet_bootstrap.c $
 * Description: Conduit-independent AM-based bootstrap collectives
 * Copyright 2022, The Regents of the University of California
 * Terms of use are as specified in license.txt
 */

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

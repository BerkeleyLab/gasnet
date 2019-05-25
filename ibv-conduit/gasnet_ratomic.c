/*   $Source: bitbucket.org:berkeleylab/gasnet.git/ibv-conduit/gasnet_ratomic.c $
 * Description: GASNet Remote Atomics Implementation using IBV NIC offload
 * Copyright 2025, The Regents of the University of California
 * Terms of use are as specified in license.txt
 */

#define GASNETI_NEED_GASNET_RATOMIC_H 1
#include <gasnet_internal.h>

#if GASNETC_BUILD_IBVRATOMIC // Else entire file is empty

#include <gasnet_ratomic_internal.h>

//
// Create-hook to install the dispatch tables (aka algorithm selection)
//
void gasnete_ibvratomic_init_hook(gasneti_AD_t real_ad)
{
    // TODO-EX: this is a stub, always selecting AM-based reference implementation.
    gasnete_amratomic_init_hook(real_ad);
    return;
}

#endif // GASNETC_BUILD_IBVRATOMIC

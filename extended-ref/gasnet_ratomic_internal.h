/*   $Source: bitbucket.org:berkeleylab/gasnet.git/extended-ref/gasnet_ratomic_internal.h $
 * Description: GASNet Remote Atomics Internal Header
 * Copyright 2017, The Regents of the University of California
 * Terms of use are as specified in license.txt
 */

#ifndef _GASNET_RATOMIC_INTERNAL_H
#define _GASNET_RATOMIC_INTERNAL_H

#include <gasnet_internal.h>
#include <gasnet_ratomic.h>

#define GASNETI_AD_MAGIC           GASNETI_MAKE_MAGIC('A','D','_','t')
#define GASNETI_AD_BAD_MAGIC       GASNETI_MAKE_BAD_MAGIC('A','D','_','t')

extern gasneti_AD_t gasneti_alloc_ad(
                       gasneti_TM_t tm,
                       gex_DT_t dt,
                       gex_OP_t ops,
                       gex_Flags_t flags,
                       size_t alloc_size);
void gasneti_free_ad(gasneti_AD_t ad);

#endif

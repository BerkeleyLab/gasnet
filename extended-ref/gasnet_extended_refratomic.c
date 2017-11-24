/*   $Source: bitbucket.org:berkeleylab/gasnet.git/extended-ref/gasnet_extended_refratomic.c $
 * Description: Reference implemetation of GASNet Remote Atomics, using Active Messages
 * Copyright 2017, The Regents of the University of California
 * Terms of use are as specified in license.txt
 */

#include <gasnet_internal.h>


#ifndef _GEX_AD_T

#ifndef gasneti_import_ad
gasneti_AD_t gasneti_import_ad(gex_AD_t _ad) {
  const gasneti_AD_t _real_ad = GASNETI_IMPORT_POINTER(gasneti_AD_t,_ad);
  GASNETI_CHECK_MAGIC(_real_ad, GASNETI_AD_MAGIC);
  return _real_ad;
}
#endif

#ifndef gasneti_export_ad
gex_AD_t gasneti_export_ad(gasneti_AD_t _real_ad) {
  GASNETI_CHECK_MAGIC(_real_ad, GASNETI_AD_MAGIC);
  return GASNETI_EXPORT_POINTER(gex_AD_t, _real_ad);
}
#endif

extern gasneti_AD_t gasneti_alloc_ad(
                       gasneti_TM_t tm,
                       gex_DT_t dt,
                       gex_OP_t ops,
                       gex_Flags_t flags,
                       size_t alloc_size)
{
  gasneti_AD_t ad = gasneti_malloc(alloc_size ? alloc_size : sizeof(*ad));
  gasneti_assert(!alloc_size || alloc_size >= sizeof(*ad));
  GASNETI_INIT_MAGIC(ad, GASNETI_AD_MAGIC);
  ad->_cdata = NULL;
  ad->_tm = tm;
  ad->_rank = tm->_rank; // Used often enough to justify caching
  ad->_flags = flags;
  ad->_dt = dt;
  ad->_ops = ops;
#ifdef GASNETI_AD_ALLOC_EXTRA
  GASNETI_AD_ALLOC_EXTRA(ad);
#endif
  return ad;
}

void gasneti_free_ad(gasneti_AD_t ad)
{
#ifdef GASNETI_AD_FREE_EXTRA
  GASNETI_AD_FREE_EXTRA(ad);
#endif
  GASNETI_INIT_MAGIC(ad, GASNETI_AD_BAD_MAGIC);
  gasneti_free(ad);
}

#endif // _GEX_AD_T

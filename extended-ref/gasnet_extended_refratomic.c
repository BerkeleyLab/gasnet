/*   $Source: bitbucket.org:berkeleylab/gasnet.git/extended-ref/gasnet_extended_refratomic.c $
 * Description: Reference implemetation of GASNet Remote Atomics, using Active Messages
 * Copyright 2017, The Regents of the University of California
 * Terms of use are as specified in license.txt
 */

#include <gasnet_internal.h>
#include <gasnet_extended_internal.h>
#include <gasnet_ratomic_internal.h>
#include <gasnet_extended_refratomic.h>

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

void gasneti_AD_Create(
        gex_AD_t                   *ad_p,
        gex_TM_t                   tm,
        gex_DT_t                   dt,
        gex_OP_t                   ops,
        gex_Flags_t                flags)
{
  gasneti_TM_t real_tm = gasneti_import_tm(tm);

  // Argument validation is done here, rather than gasneti_alloc_ad(), to
  // allow conduit-specific extensions (such as additional types or ops).
  // However, this leaves a significant amount of code to be cloned into
  // the conduit.
  // TODO: refactor if/when we have a conduit-specific type or op?

#if GASNET_DEBUG
  // Verify that call is collective and single-valued
  // TODO-EX: should use normal collectives and just a Gather.
  // TODO-EX: needs to be scoped to proper team, of course.
  {
    struct {
        gex_DT_t       dt;
        gex_OP_t       ops;
        gex_Flags_t    flags;
    } myargs, *allargs;
    allargs = gasneti_malloc(real_tm->_size * sizeof(myargs));
    myargs.dt    = dt;
    myargs.ops   = ops;
    myargs.flags = flags;
    gasneti_defaultExchange(&myargs, sizeof(myargs), allargs);
    if (!real_tm->_rank) {
      for (gex_Rank_t r = 0; r < real_tm->_size; ++r) {
        gasneti_assert(allargs[r].dt    == dt);
        gasneti_assert(allargs[r].ops   == ops);
        gasneti_assert(allargs[r].flags == flags);
      }
    }
    gasneti_free(allargs);
    GASNETI_SAFE(gasnet_barrier(0, GASNET_BARRIERFLAG_UNNAMED));
  }

  // Does the 'dt' arument name a single valid data type?
  gasneti_assert(gasneti_dt_valid(dt));

  // Does ops specify a non-empty set with ALL members valid for remote atomics on the data type?
  gasneti_assert(ops); // Not empty
  gasneti_assert(gasneti_op_atomic_mask(ops));
  gasneti_assert(gasneti_op_fp_mask(ops)  || !gasneti_dt_fp(dt));
  gasneti_assert(gasneti_op_int_mask(ops) || !gasneti_dt_int(dt));

  // Verify we agree on the size of the FP type, if any
  gasneti_assert((dt != GEX_DT_FLT) || sizeof(float) == 4);
  gasneti_assert((dt != GEX_DT_DBL) || sizeof(double) == 8);
#endif

  gasneti_AD_t real_ad = gasneti_alloc_ad(real_tm, dt, ops, flags, 0);
  *ad_p = gasneti_export_ad(real_ad);
  return;
}

void gasneti_AD_Destroy(gex_AD_t ad)
{
  gasneti_AD_t real_ad = gasneti_import_ad(ad);

#if GASNET_DEBUG
  // Try to verify that call is collective.
  // TODO-EX: must be scoped to real_ad->_tm
  GASNETI_SAFE(gasnet_barrier(0xcafef00d ^ __LINE__, 0));

  // TODO: can/should we verify that there are no incompete ops?
#endif

  gasneti_free_ad(real_ad);
  return;
}

#endif // _GEX_AD_T

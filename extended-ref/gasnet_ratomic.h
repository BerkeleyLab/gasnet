/*   $Source: bitbucket.org:berkeleylab/gasnet.git/gasnet_ratomic.h $
 * Description: GASNet Remote Atomics API Header
 * Copyright 2017, The Regents of the University of California
 * Terms of use are as specified in license.txt
 */

#ifndef _IN_GASNETEX_H
  #error This file is not meant to be included directly- clients should include gasnetex.h
#endif

#ifndef _GASNET_RATOMIC_H
#define _GASNET_RATOMIC_H

// gex_AD_t is an opaque scalar handle
struct gasneti_ad_t;
typedef struct gasneti_ad_t *gex_AD_t;

#ifndef _GEX_AD_T
  #define GASNETI_AD_COMMON \
    GASNETI_OBJECT_HEADER              \
    gasneti_TM_t       _tm;            \
    gex_Rank_t         _rank;          \
    gex_DT_t           _dt;            \
    gex_OP_t           _ops;
  typedef struct { GASNETI_AD_COMMON } *gasneti_AD_t;
  #if GASNET_DEBUG
    extern gasneti_AD_t gasneti_import_ad(gex_AD_t _ad);
    extern gex_AD_t gasneti_export_ad(gasneti_AD_t _real_ad);
  #else
    #define gasneti_import_ad(x) ((gasneti_AD_t)(x))
    #define gasneti_export_ad(x) ((gex_AD_t)(x))
  #endif
  #define gex_AD_SetCData(ad,val)  ((void)(gasneti_import_ad(ad)->_cdata = (val)))
  #define gex_AD_QueryCData(ad)    ((void*)gasneti_import_ad(ad)->_cdata)
  #define gex_AD_QueryTM(ad)       gasneti_export_tm(gasneti_import_ad(ad)->_tm)
  #define gex_AD_QueryOps(ad)      ((gex_OP_t)gasneti_import_ad(ad)->_ops)
  #define gex_AD_QueryDT(ad)       ((gex_DT_t)gasneti_import_ad(ad)->_dt)
  #define gex_AD_QueryFlags(ad)    ((gex_Flags_t)gasneti_import_ad(ad)->_flags)
#endif

// Collective creation of an atomic domain - default implementation
#ifndef gex_AD_Create
  extern void gasneti_AD_Create(
        gex_AD_t                   *ad_p,            // Output
        gex_TM_t                   tm,               // The team
        gex_DT_t                   dt,               // The data type
        gex_OP_t                   ops,              // OR of operations to be supported
        gex_Flags_t                flags             // flags
        );
  #define gex_AD_Create gasneti_AD_Create
#endif

#endif

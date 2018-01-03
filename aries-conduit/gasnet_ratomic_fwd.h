/*   $Source: bitbucket.org:berkeleylab/gasnet.git/aries-conduit/gasnet_ratomic_fwd.h $
 * Description: GASNet Remote Atomics API Header (aries-conduit specific forward decls)
 * Copyright 2017, The Regents of the University of California
 * Terms of use are as specified in license.txt
 */

#ifndef _IN_GASNETEX_H
  #error This file is not meant to be included directly- clients should include gasnetex.h
#endif

#ifndef _GASNET_RATOMIC_FWD_H
#define _GASNET_RATOMIC_FWD_H

// gemini should be using amref version of this file
#if !GASNET_CONDUIT_ARIES
#error "Improper use of aries-conduit/gasnet_ratomic_fwd.h"
#endif

// Build GNI remote atomics by default
#if defined(GASNETC_BUILD_GNIRATOMIC) && !GASNETC_BUILD_GNIRATOMIC
  #undef GASNETC_BUILD_GNIRATOMIC
#else
  #undef GASNETC_BUILD_GNIRATOMIC
  #define GASNETC_BUILD_GNIRATOMIC 1
#endif

#if GASNETC_BUILD_GNIRATOMIC
  //#define GASNETE_BUILD_AMRATOMIC 0 - cannot disable because needed for DBL
  #define GASNETE_HAVE_RATOMIC_EXTRA_H

  // #define public API *directly* to GNI-based one
  // No array of function pointers is needed when there is only 1 implementation.
  //
  // We take some care not to inline a big switch if opcode is non-constant.
  #define GASNETE_GNIRATOMIC_FN(stem,ad,result_p,rank,addr,opcode,op1,op2,flags) \
    (gasneti_constant_p(opcode) \
     ? gasnete_gniratomic_##stem(ad,result_p,rank,addr,opcode,op1,op2,flags GASNETI_THREAD_GET) \
     : gasnete_gniratomic_##stem##_external(ad,result_p,rank,addr,opcode,op1,op2,flags GASNETI_THREAD_GET))
  //
  #define gex_AD_OpNB_I32(ad,result_p,rank,addr,opcode,op1,op2,flags) \
        GASNETE_GNIRATOMIC_FN(gex_dt_I32_NB,ad,result_p,rank,addr,opcode,op1,op2,flags)
  #define gex_AD_OpNBI_I32(ad,result_p,rank,addr,opcode,op1,op2,flags) \
        GASNETE_GNIRATOMIC_FN(gex_dt_I32_NBI,ad,result_p,rank,addr,opcode,op1,op2,flags)
  #define gex_AD_OpNB_U32(ad,result_p,rank,addr,opcode,op1,op2,flags) \
        GASNETE_GNIRATOMIC_FN(gex_dt_U32_NB,ad,result_p,rank,addr,opcode,op1,op2,flags)
  #define gex_AD_OpNBI_U32(ad,result_p,rank,addr,opcode,op1,op2,flags) \
        GASNETE_GNIRATOMIC_FN(gex_dt_U32_NBI,ad,result_p,rank,addr,opcode,op1,op2,flags)
  #define gex_AD_OpNB_I64(ad,result_p,rank,addr,opcode,op1,op2,flags) \
        GASNETE_GNIRATOMIC_FN(gex_dt_I64_NB,ad,result_p,rank,addr,opcode,op1,op2,flags)
  #define gex_AD_OpNBI_I64(ad,result_p,rank,addr,opcode,op1,op2,flags) \
        GASNETE_GNIRATOMIC_FN(gex_dt_I64_NBI,ad,result_p,rank,addr,opcode,op1,op2,flags)
  #define gex_AD_OpNB_U64(ad,result_p,rank,addr,opcode,op1,op2,flags) \
        GASNETE_GNIRATOMIC_FN(gex_dt_U64_NB,ad,result_p,rank,addr,opcode,op1,op2,flags)
  #define gex_AD_OpNBI_U64(ad,result_p,rank,addr,opcode,op1,op2,flags) \
        GASNETE_GNIRATOMIC_FN(gex_dt_U64_NBI,ad,result_p,rank,addr,opcode,op1,op2,flags)
  #define gex_AD_OpNB_FLT(ad,result_p,rank,addr,opcode,op1,op2,flags) \
        GASNETE_GNIRATOMIC_FN(gex_dt_FLT_NB,ad,result_p,rank,addr,opcode,op1,op2,flags)
  #define gex_AD_OpNBI_FLT(ad,result_p,rank,addr,opcode,op1,op2,flags) \
        GASNETE_GNIRATOMIC_FN(gex_dt_FLT_NBI,ad,result_p,rank,addr,opcode,op1,op2,flags)

  #if 0 // No DBL because DP addition is known to be broken (e.g. see OFI GNI provider)
  #define gex_AD_OpNB_DBL(ad,result_p,rank,addr,opcode,op1,op2,flags) \
        GASNETE_GNIRATOMIC_FN(gex_dt_DBL_NB,ad,result_p,rank,addr,opcode,op1,op2,flags)
  #define gex_AD_OpNBI_DBL(ad,result_p,rank,addr,opcode,op1,op2,flags) \
        GASNETE_GNIRATOMIC_FN(gex_dt_DBL_NBI,ad,result_p,rank,addr,opcode,op1,op2,flags)
  #endif

  /* stats needed by the GNI-specific atomics implementation */
  #ifndef GASNETI_RATOMIC_STATS
    #define GASNETI_RATOMIC_STATS(CNT,VAL,TIME)    \
        /* Currently empty */
  #endif
#else // NOT building GNI-specific atomics
  /* stats needed by the RAtomic reference implementation */
  #ifndef GASNETI_RATOMIC_STATS
    #define GASNETI_RATOMIC_STATS(CNT,VAL,TIME)    \
        /* Currently empty */
  #endif
#endif

#endif // _GASNET_RATOMIC_FWD_H

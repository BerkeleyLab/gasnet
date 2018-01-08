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
  #define GASNETE_HAVE_RATOMIC_EXTRA_H 1

  #define GASNETI_AD_CREATE_HOOK gasnete_gniratomic_create_hook

  /* stats needed by the GNI-specific atomics implementation */
  #ifndef GASNETI_RATOMIC_STATS
    #define GASNETI_RATOMIC_STATS(CNT,VAL,TIME)    \
        /* Currently empty */
  #endif

  // Cannot assume always cpusafe - need to chech each AD
  #define GASNETE_RATOMIC_ALWAYS_CPUSAFE_gex_dt_I32 0
  #define GASNETE_RATOMIC_ALWAYS_CPUSAFE_gex_dt_U32 0
  #define GASNETE_RATOMIC_ALWAYS_CPUSAFE_gex_dt_I64 0
  #define GASNETE_RATOMIC_ALWAYS_CPUSAFE_gex_dt_U64 0
  #define GASNETE_RATOMIC_ALWAYS_CPUSAFE_gex_dt_FLT 0
  #define GASNETE_RATOMIC_ALWAYS_CPUSAFE_gex_dt_DBL 0
#else // NOT building GNI-specific atomics
  /* stats needed by the RAtomic reference implementation */
  #ifndef GASNETI_RATOMIC_STATS
    #define GASNETI_RATOMIC_STATS(CNT,VAL,TIME)    \
        /* Currently empty */
  #endif

  #define GASNETE_RATOMIC_ALWAYS_CPUSAFE_gex_dt_I32 1
  #define GASNETE_RATOMIC_ALWAYS_CPUSAFE_gex_dt_U32 1
  #define GASNETE_RATOMIC_ALWAYS_CPUSAFE_gex_dt_I64 1
  #define GASNETE_RATOMIC_ALWAYS_CPUSAFE_gex_dt_U64 1
  #define GASNETE_RATOMIC_ALWAYS_CPUSAFE_gex_dt_FLT 1
  #define GASNETE_RATOMIC_ALWAYS_CPUSAFE_gex_dt_DBL 1
#endif

#endif // _GASNET_RATOMIC_FWD_H

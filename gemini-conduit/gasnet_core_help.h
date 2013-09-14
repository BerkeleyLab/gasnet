/*   $Source: /Users/kamil/work/gasnet-cvs2/gasnet/gemini-conduit/gasnet_core_help.h,v $
 *     $Date: 2013/09/14 02:05:37 $
 * $Revision: 1.1.1.2.18.1 $
 * Description: GASNet gemini conduit core Header Helpers (Internal code, not for client use)
 * Copyright 2002, Dan Bonachea <bonachea@cs.berkeley.edu>
 * Terms of use are as specified in license.txt
 */

#ifndef _IN_GASNET_H
  #error This file is not meant to be included directly- clients should include gasnet.h
#endif

#ifndef _GASNET_CORE_HELP_H
#define _GASNET_CORE_HELP_H

GASNETI_BEGIN_EXTERNC

#if defined(GASNET_PAR) && GASNETC_GNI_MULTI_DOMAIN
  /* TODO:
   * It we ever support multi-domain and throttle-pollers together
   * then this is in need of some per-domain throttling logic.
   */
  #if GASNET_PSHM
    #define GASNETC_MAYBE_PSHM_POLL() gasneti_AMPSHMPoll(0)
  #else
    #define GASNETC_MAYBE_PSHM_POLL() /*empty*/
  #endif
  extern void gasnetc_poll(int didx);
  extern int gasnetc_my_domain_index(void) GASNETI_CONST;
  GASNETI_CONSTP(gasnetc_my_domain_index)
  #define gasneti_pollwhile(cnd) do {                \
    GASNETI_CHECKATTACH();                           \
    if (cnd) {                                       \
      const int my_didx = gasnetc_my_domain_index(); \
      gasneti_memcheck_one();                        \
      GASNETC_MAYBE_PSHM_POLL();                     \
      gasnetc_poll(my_didx);                         \
      GASNETI_PROGRESSFNS_RUN();                     \
      while (cnd) {                                  \
        GASNETI_WAITHOOK();                          \
        gasneti_memcheck_one();                      \
        GASNETC_MAYBE_PSHM_POLL();                   \
        gasnetc_poll(my_didx);                       \
        GASNETI_PROGRESSFNS_RUN();                   \
      }                                              \
    }                                                \
    gasneti_local_rmb();                             \
  } while (0)
#endif

#include <gasnet_help.h>

GASNETI_END_EXTERNC

#endif

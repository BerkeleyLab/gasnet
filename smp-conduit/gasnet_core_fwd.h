/*   $Source: bitbucket.org:berkeleylab/gasnet.git/smp-conduit/gasnet_core_fwd.h $
 * Description: GASNet header for smp conduit core (forward definitions)
 * Copyright 2002, Dan Bonachea <bonachea@cs.berkeley.edu>
 * Terms of use are as specified in license.txt
 */

#ifndef _IN_GASNETEX_H
  #error This file is not meant to be included directly- clients should include gasnetex.h
#endif

#ifndef _GASNET_CORE_FWD_H
#define _GASNET_CORE_FWD_H

#define GASNET_CORE_VERSION      1.10
#define GASNET_CORE_VERSION_STR  _STRINGIFY(GASNET_CORE_VERSION)
#define GASNET_CORE_NAME         SMP
#define GASNET_CORE_NAME_STR     _STRINGIFY(GASNET_CORE_NAME)
#define GASNET_CONDUIT_NAME      GASNET_CORE_NAME
#define GASNET_CONDUIT_NAME_STR  _STRINGIFY(GASNET_CONDUIT_NAME)

#define GASNET_CONDUIT_SMP       1

  /* GASNET_PSHM defined 1 if this conduit supports PSHM. leave undefined otherwise. */
#if GASNETI_PSHM_ENABLED
  #define GASNET_PSHM 1
#endif

  /*  defined to be 1 if gasnet_init guarantees that the remote-access memory segment will be aligned  */
  /*  at the same virtual address on all nodes. defined to 0 otherwise */
/* segment alignment on smp is moot, as there is only a single node */
#if GASNETI_PSHM_ENABLED
  #define GASNET_ALIGNED_SEGMENTS   0 /* use of PSHM prevents segment alignment */
#else
  #define GASNET_ALIGNED_SEGMENTS   1
#endif

#if !defined(GASNETE_PUTGET_ALWAYSREMOTE) && !defined(GASNETE_PUTGET_ALWAYSLOCAL)
 #if GASNET_PSHM
  #define GASNETE_PUTGET_ALWAYSREMOTE 1
 #else
  #define GASNETE_PUTGET_ALWAYSLOCAL 1
 #endif
#endif

#if !GASNET_PSHM
  #define GASNETI_GASNETC_AMPOLL /* empty */
  #define gasnetc_AMPoll GASNET_OK GASNETI_THREAD_SWALLOW
#endif

  /* define to 1 if conduit allows internal GASNet fns to issue put/get for remote
     addrs out of segment - not true when PSHM is used */
#if 0
#define GASNETI_SUPPORTS_OUTOFSEGMENT_PUTGET 1
#endif

  /* conduits should define GASNETI_CONDUIT_THREADS to 1 if they have one or more 
     "private" threads which may be used to run AM handlers, even under GASNET_SEQ
     this ensures locking is still done correctly, etc
   */
/* #define GASNETI_CONDUIT_THREADS 1 */

  /* define these to 1 if your conduit needs to augment the implementation
     of gasneti_reghandler() (in gasnet_internal.c)
   */
/* #define GASNETC_AMREGISTER 1 */

  /* define these to 1 if your conduit supports PSHM, but cannot use the
     default interfaces. (see template-conduit/gasnet_core.c and gasnet_pshm.h)
   */
/* #define GASNETC_GET_HANDLER 1 */
/* #define GASNETC_TOKEN_CREATE 1 */

  /* this can be used to add conduit-specific 
     statistical collection values (see gasnet_trace.h) */
#define GASNETC_CONDUIT_STATS(CNT,VAL,TIME) 

#if GASNET_PSHM
  #define GASNETC_FATALSIGNAL_CLEANUP_CALLBACK(sig) gasnetc_fatalsignal_cleanup_callback(sig)
  extern void gasnetc_fatalsignal_cleanup_callback(int sig);
#endif

#endif

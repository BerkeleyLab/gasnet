/*  $Archive:: /Ti/GASNet/lapi-conduit/gasnet_core_fwd.h              $
 *     $Date: 2003/11/10 10:06:27 $
 * $Revision: 1.11 $
 * Description: GASNet header for lapi conduit core (forward definitions)
 * Copyright 2002, Dan Bonachea <bonachea@cs.berkeley.edu>
 * Terms of use are as specified in license.txt
 */

#ifndef _IN_GASNET_H
  #error This file is not meant to be included directly- clients should include gasnet.h
#endif

#ifndef _GASNET_CORE_FWD_H
#define _GASNET_CORE_FWD_H

#define GASNET_CORE_VERSION      1.3
#define GASNET_CORE_VERSION_STR  _STRINGIFY(GASNET_CORE_VERSION)
#define GASNET_CORE_NAME         LAPI
#define GASNET_CORE_NAME_STR     _STRINGIFY(GASNET_CORE_NAME)
#define GASNET_CONDUIT_LAPI      1

/* defined to be 1 if gasnet_init guarantees that the remote-access
 * memory segment will be aligned at the same virtual address on all
 * nodes. defined to 0 otherwise.
 *
 * We should be able to guarantee aligned segments on SP.  Segmented
 * memory guarantees heap and mmaped regions don't overlap.
 */
#define GASNET_ALIGNED_SEGMENTS   1

  /* conduits should define GASNETI_CONDUIT_THREADS to 1 if they have one or more 
     "private" threads which may be used to run AM handlers, even under GASNET_SEQ
     this ensures locking is still done correctly, etc
   */
/* lapi-conduit always has the LAPI completion and notification threads */
#define GASNETI_CONDUIT_THREADS 1

  /* define to 1 if your conduit may interrupt an application thread 
     (e.g. with a signal) to run AM handlers (interrupt-based handler dispatch)
   */
/* #define GASNETC_USE_INTERRUPTS 1 */

  /* this can be used to add conduit-specific 
     statistical collection values (see gasnet_trace.h) */
#define CONDUIT_CORE_STATS(CNT,VAL,TIME) 

#endif

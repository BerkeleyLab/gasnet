/*  $Archive:: /Ti/GASNet/template-conduit/gasnet_core_fwd.h              $
 *     $Date: 2003/06/29 08:09:46 $
 * $Revision: 1.8 $
 * Description: GASNet header for lapi conduit core (forward definitions)
 * Copyright 2002, Dan Bonachea <bonachea@cs.berkeley.edu>
 * Terms of use are as specified in license.txt
 */

#ifndef _IN_GASNET_H
  #error This file is not meant to be included directly- clients should include gasnet.h
#endif

#ifndef _GASNET_CORE_FWD_H
#define _GASNET_CORE_FWD_H

#define GASNET_CORE_VERSION      1.2
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

#define GASNETI_FORCE_TRUE_MUTEXES 1

  /* this can be used to add conduit-specific 
     statistical collection values (see gasnet_trace.h) */
#define CONDUIT_CORE_STATS(CNT,VAL,TIME) 

#endif

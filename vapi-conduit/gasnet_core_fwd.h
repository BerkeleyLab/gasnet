/*  $Archive:: /Ti/GASNet/template-conduit/gasnet_core_fwd.h              $
 *     $Date: 2003/03/21 01:12:37 $
 * $Revision: 1.2.2.1 $
 * Description: GASNet header for vapi conduit core (forward definitions)
 * Copyright 2002, Dan Bonachea <bonachea@cs.berkeley.edu>
 * Terms of use are as specified in license.txt
 */

#ifndef _IN_GASNET_H
  #error This file is not meant to be included directly- clients should include gasnet.h
#endif

#ifndef _GASNET_CORE_FWD_H
#define _GASNET_CORE_FWD_H

#define GASNET_CORE_VERSION      0.1
#define GASNET_CORE_VERSION_STR  _STRINGIFY(GASNET_CORE_VERSION)
#define GASNET_CORE_NAME         VAPI
#define GASNET_CORE_NAME_STR     _STRINGIFY(GASNET_CORE_NAME)

  /*  defined to be 1 if gasnet_init guarantees that the remote-access memory segment will be aligned  */
  /*  at the same virtual address on all nodes. defined to 0 otherwise */
#define GASNET_ALIGNED_SEGMENTS   1

  /* this can be used to add conduit-specific 
     statistical collection values (see gasnet_trace.h) */
#define CONDUIT_CORE_STATS(CNT,VAL,TIME) 

/* XXX:
 * The VAPI conduit requires real locking, even for GASNET_SYNC,
 * because there is a network progress thread.
 * By defining this here we can be sure the extended-ref impl
 * is compiled with threads support.
 * This will go away when we implement the extended API directly.
 */
#ifndef GASNETI_THREADS
  #define GASNETI_THREADS
#endif

#endif

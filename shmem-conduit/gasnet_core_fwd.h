/*  $Archive:: /Ti/GASNet/shmem-conduit/gasnet_core_fwd.h              $
 *     $Date: 2003/11/11 13:40:39 $
 * $Revision: 1.1.2.1 $
 * Description: GASNet header for shmem conduit core (forward definitions)
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
#define GASNET_CORE_NAME         SHMEM
#define GASNET_CORE_NAME_STR     _STRINGIFY(GASNET_CORE_NAME)
#define GASNET_CONDUIT_SHMEM 1

  /*  defined to be 1 if gasnet_init guarantees that the remote-access memory segment will be aligned  */
  /*  at the same virtual address on all nodes. defined to 0 otherwise */
#define GASNET_ALIGNED_SEGMENTS   1

  /* conduits should define GASNETI_CONDUIT_THREADS to 1 if they have one or more 
     "private" threads which may be used to run AM handlers, even under GASNET_SEQ
     this ensures locking is still done correctly, etc
   */
#define GASNETI_CONDUIT_THREADS 0

  /* define to 1 if your conduit may interrupt an application thread 
     (e.g. with a signal) to run AM handlers (interrupt-based handler dispatch)
   */
#define GASNETC_USE_INTERRUPTS 0

  /* this can be used to add conduit-specific 
     statistical collection values (see gasnet_trace.h) */
#define CONDUIT_CORE_STATS(CNT,VAL,TIME) 

#endif

/*  $Archive:: /Ti/GASNet/mpi-conduit/gasnet_core_fwd.h                   $
 *     $Date: 2003/06/29 08:09:48 $
 * $Revision: 1.11 $
 * Description: GASNet header for MPI conduit core (forward definitions)
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
#define GASNET_CORE_NAME         MPI
#define GASNET_CORE_NAME_STR     _STRINGIFY(GASNET_CORE_NAME)
#define GASNET_CONDUIT_MPI       1

  /*  defined to be 1 if gasnet_init guarantees that the remote-access memory segment will be aligned  */
  /*  at the same virtual address on all nodes. defined to 0 otherwise */
#ifndef GASNET_ALIGNED_SEGMENTS
  /* mpi-conduit supports both aligned and un-aligned */
  #ifdef HAVE_MMAP
    #define GASNET_ALIGNED_SEGMENTS   1  
  #else
    #define GASNET_ALIGNED_SEGMENTS   0
  #endif
#endif

/*  override default error values to use those defined by AMMPI */
#define _GASNET_ERRORS
#define _GASNET_ERR_BASE 10000
#define GASNET_ERR_NOT_INIT             1
#define GASNET_ERR_RESOURCE             3
#define GASNET_ERR_BAD_ARG              2
#define GASNET_ERR_NOT_READY            (_GASNET_ERR_BASE+4)
#define GASNET_ERR_BARRIER_MISMATCH     (_GASNET_ERR_BASE+5)

  /* this can be used to add conduit-specific 
     statistical collection values (see gasnet_help.h) */
#define CONDUIT_CORE_STATS(CNT,VAL,TIME) 

#endif

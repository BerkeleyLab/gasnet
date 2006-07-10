/*   $Source: /Users/kamil/work/gasnet-cvs2/gasnet/portals-conduit/Attic/gasnet_extended_fwd.h,v $
 *     $Date: 2006/07/10 23:56:57 $
 * $Revision: 1.1.2.2 $
 * Description: GASNet Extended API Header (forward decls)
 * Copyright 2002, Dan Bonachea <bonachea@cs.berkeley.edu>
 * Terms of use are as specified in license.txt
 */

#ifndef _IN_GASNET_H
  #error This file is not meant to be included directly- clients should include gasnet.h
#endif

#ifndef _GASNET_EXTENDED_FWD_H
#define _GASNET_EXTENDED_FWD_H

#define GASNET_EXTENDED_VERSION      1.0
#define GASNET_EXTENDED_VERSION_STR  _STRINGIFY(GASNET_EXTENDED_VERSION)
#define GASNET_EXTENDED_NAME         PORTALS
#define GASNET_EXTENDED_NAME_STR     _STRINGIFY(GASNET_EXTENDED_NAME)

/* Hijack the CORE definitions of these as well, to prevent misleading messages */
#ifdef GASNET_CORE_NAME
#undef GASNET_CORE_NAME
#endif
#ifdef GASNET_CORE_NAME_STR
#undef GASNET_CORE_NAME_STR
#endif
#ifdef GASNET_CORE_VERSION
#undef GASNET_CORE_VERSION
#endif
#ifdef GASNET_CONDUIT_MPI
#undef GASNET_CONDUIT_MPI
#endif
#define GASNET_CORE_VERSION    GASNET_EXTENDED_VERSION
#define GASNET_CORE_NAME       PORTALS
#define GASNET_CORE_NAME_STR  _STRINGIFY(GASNET_CORE_NAME)

#define _GASNET_HANDLE_T
/*  an opaque type representing a non-blocking operation in-progress initiated using the extended API */
struct _gasnete_op_t;
typedef struct _gasnete_op_t *gasnet_handle_t;
#define GASNET_INVALID_HANDLE ((gasnet_handle_t)0)
#define GASNETI_EOP_IS_HANDLE 1

  /* this can be used to add statistical collection values 
     specific to the extended API implementation (see gasnet_help.h) */
#define GASNETE_CONDUIT_STATS(CNT,VAL,TIME)  \
        GASNETI_VIS_STATS(CNT,VAL,TIME)      \
        GASNETI_COLL_STATS(CNT,VAL,TIME)     \
        CNT(C, DYNAMIC_THREADLOOKUP, cnt)    

/* When defining a Portals Event Queue, we have the option of installing
 * an event handler for the Queue that will be executed for each event
 * when other queues are polled.  There are restrictions on the operations
 * that can be performed when used in this manner, but our usage conforms to
 * those restrictions.
 * Since the MPI-Conduit over (with MPI over Portals) does poll queues,
 * we have the option of using a Portals EQ Handler.
 * If we choose not to use the EQ Handler, we must hook into a progress
 * function.
 * MLW: 07/10/2006: USE of EQ handler does not seem to work.  Hangs.
 */
#ifndef GASNETE_USE_EQ_HANDLER
extern void gasnete_portals_poll();
#define GASNETE_PROGRESSFN_EXTRA(FN)					\
  FN(gasnete_pf_portals_poll, BOOLEAN, gasnete_portals_poll)
#endif

#endif


/*   $Source: /Users/kamil/work/gasnet-cvs2/gasnet/portals-conduit/Attic/gasnet_extended_fwd.h,v $
 *     $Date: 2006/07/07 23:51:58 $
 * $Revision: 1.1.2.1 $
 * Description: GASNet Extended API Header (forward decls)
 * Copyright 2002, Dan Bonachea <bonachea@cs.berkeley.edu>
 * Terms of use are as specified in license.txt
 */

#ifndef _IN_GASNET_H
  #error This file is not meant to be included directly- clients should include gasnet.h
#endif

#ifndef _GASNET_EXTENDED_FWD_H
#define _GASNET_EXTENDED_FWD_H

#define GASNET_EXTENDED_VERSION      1.7
#define GASNET_EXTENDED_VERSION_STR  _STRINGIFY(GASNET_EXTENDED_VERSION)
#define GASNET_EXTENDED_NAME         REFERENCE
#define GASNET_EXTENDED_NAME_STR     _STRINGIFY(GASNET_EXTENDED_NAME)


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

#ifndef GASNETE_USE_EQ_HANDLER
  #ifndef GASNETE_PROGRESSFNS_LIST
    #ifndef GASNETE_BARRIER_PROGRESSFN
      extern void gasnete_ambarrier_kick();
      #define GASNETE_BARRIER_PROGRESSFN(FN) \
        FN(gasneti_pf_barrier, BOOLEAN, gasnete_ambarrier_kick)
    #endif
    #ifndef GASNETE_PORTALS_PROGRESSFN
      extern void gasnete_portals_poll(void);
      #define GASNETE_PORTALS_PROGRESSFN(FN) \
        FN(gasnete_pf_portals_poll, BOOLEAN, gasnete_portals_poll)
    #endif

    #define GASNETE_PROGRESSFNS_LIST(FN) \
      GASNETE_BARRIER_PROGRESSFN(FN)    \
      GASNETE_PORTALS_PROGRESSFN(FN)
  #endif
#endif

#endif


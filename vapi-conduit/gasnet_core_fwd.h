/*  $Archive:: /Ti/GASNet/template-conduit/gasnet_core_fwd.h              $
 *     $Date: 2003/06/30 17:31:43 $
 * $Revision: 1.2.2.8 $
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
#define GASNET_CONDUIT_VAPI      1

/* This is the limit on the LID space... */
#define GASNET_MAXNODES	16384

/* Explicitly set some types because we depend on their sizes when encoding them */
#define _GASNET_NODE_T
typedef uint16_t gasnet_node_t;
#define _GASNET_HANDLER_T
typedef uint8_t gasnet_handler_t;

  /*  defined to be 1 if gasnet_init guarantees that the remote-access memory segment will be aligned  */
  /*  at the same virtual address on all nodes. defined to 0 otherwise */
#define GASNET_ALIGNED_SEGMENTS   1

  /* this can be used to add conduit-specific 
     statistical collection values (see gasnet_trace.h) */
#define CONDUIT_CORE_STATS(CNT,VAL,TIME)          \
        CNT(C, SYSTEM_REQUEST, cnt)               \
        CNT(C, SYSTEM_REPLY, cnt)                 \
        CNT(C, SYSTEM_REQHANDLER, cnt)            \
        CNT(C, SYSTEM_REPHANDLER, cnt)            \
        CNT(C, GET_AMREQ_CREDIT, cnt)             \
	TIME(C, GET_AMREQ_CREDIT_STALL, stalled time) \
	CNT(C, GET_SBUF, cnt)                     \
	TIME(C, GET_SBUF_STALL, stalled time)     \
	CNT(C, SND_REAP, calls)                   \
	VAL(C, SND_REAP_CNT, reaped)              \
	CNT(C, RCV_REAP, calls)                   \
	VAL(C, RCV_REAP_CNT, reaped)

/*
 * The VAPI conduit requires real HSLs, even for GASNET_SYNC, because there is a network progress thread.
 */
#define GASNETI_FORCE_TRUE_MUTEXES 1

#endif

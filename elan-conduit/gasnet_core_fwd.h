/*  $Archive:: /Ti/GASNet/template-conduit/gasnet_core_fwd.h              $
 *     $Date: 2003/06/29 08:09:42 $
 * $Revision: 1.10 $
 * Description: GASNet header for elan conduit core (forward definitions)
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
#define GASNET_CORE_NAME         ELAN
#define GASNET_CORE_NAME_STR     _STRINGIFY(GASNET_CORE_NAME)
#define GASNET_CONDUIT_ELAN      1

  /*  defined to be 1 if gasnet_init guarantees that the remote-access memory segment will be aligned  */
  /*  at the same virtual address on all nodes. defined to 0 otherwise */
#define GASNET_ALIGNED_SEGMENTS   1 

  /* this can be used to add conduit-specific 
     statistical collection values (see gasnet_trace.h) */
#define CONDUIT_CORE_STATS(CNT,VAL,TIME) \
        VAL(C, AMLONG_DIRECT, sz)        \
        VAL(C, AMLONG_BUFFERED, sz)      \
        VAL(C, PUT_DIRECT, sz)           \
        VAL(C, PUT_BULK_DIRECT, sz)      \
        VAL(C, PUT_BUFFERED, sz)         \
        VAL(C, PUT_BULK_BUFFERED, sz)    \
        VAL(C, PUT_AMMEDIUM, sz)         \
        VAL(C, PUT_BULK_AMMEDIUM, sz)    \
        VAL(C, PUT_AMLONG, sz)           \
        VAL(C, PUT_BULK_AMLONG, sz)      \
        VAL(C, GET_DIRECT, sz)           \
        VAL(C, GET_BUFFERED, sz)         \
        VAL(C, GET_AMMEDIUM, sz)         \
        VAL(C, GET_AMLONG, sz)         

/* ------------------------------------------------------------------------------------ */
/* use ELAN-specific high-performance nanosecond timer */
#define GASNETC_CONDUIT_SPECIFIC_TIMERS
typedef uint64_t gasneti_stattime_t;
#define GASNETI_STATTIME_MIN        ((gasneti_stattime_t)0)
#define GASNETI_STATTIME_MAX        ((gasneti_stattime_t)-1)
extern uint64_t gasnetc_clock();
#define GASNETI_STATTIME_NOW()      (gasnetc_clock())
#define GASNETI_STATTIME_TO_US(st)  ((st)/1000)
/* ------------------------------------------------------------------------------------ */

#define GASNETC_TRACE_FINISH()  gasnetc_trace_finish()
extern void gasnetc_trace_finish();

#define GASNETC_FATALSIGNAL_CALLBACK(sig) gasnetc_fatalsignal_callback(sig)
extern void gasnetc_fatalsignal_callback(int sig);

#define GASNETC_USE_INTERRUPTS  0

#endif

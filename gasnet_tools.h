/*  $Archive:: /Ti/GASNet/gasnet_tools.h                                   $
 *     $Date: 2003/05/22 09:21:20 $
 * $Revision: 1.7 $
 * Description: GASNet Tools library 
 * Copyright 2002, Dan Bonachea <bonachea@cs.berkeley.edu>
 * Terms of use are as specified in license.txt
 */
#ifndef _GASNET_TOOLS_H
#define _GASNET_TOOLS_H
#define _IN_GASNET_TOOLS_H
#define _INCLUDED_GASNET_TOOLS_H
#if !defined(_INCLUDED_GASNET_H) && \
    (defined(GASNET_SEQ) || defined(GASNET_PARSYNC) || defined(GASNET_PAR))
  #error Applications that use both GASNet and GASNet tools must   \
         include gasnet.h before gasnet_tools.h and must include   \
         _both_ headers in ALL files that need either header  
#endif

#include <gasnet_config.h>
#include <gasnet_basic.h>

/* ------------------------------------------------------------------------------------ */
/* portable high-performance, low-overhead timers */

#include <gasnet_timer.h>

#define gasnett_tick_t               gasneti_stattime_t
#define GASNETT_TICK_MIN             GASNETI_STATTIME_MIN
#define GASNETT_TICK_MAX             GASNETI_STATTIME_MAX
#define gasnett_ticks_to_us(ticks)   GASNETI_STATTIME_TO_US(ticks)
#define gasnett_ticks_now()          GASNETI_STATTIME_NOW()
#define gasnett_timer_granularityus()   GASNETI_STATTIME_GRANULARITY()
#define gasnett_timer_overheadus()      GASNETI_STATTIME_OVERHEAD()

#ifdef GASNETI_USING_GETTIMEOFDAY
#define GASNETT_USING_GETTIMEOFDAY
#endif

/* ------------------------------------------------------------------------------------ */
/* portable atomic increment/decrement */

#include <gasnet_atomicops.h>

#define gasnett_atomic_t             gasneti_atomic_t
#define gasnett_atomic_read(p)       gasneti_atomic_read(p)
#define gasnett_atomic_init(v)       gasneti_atomic_init(v)
#define gasnett_atomic_set(p,v)      gasneti_atomic_set(p,v) 
#define gasnett_atomic_increment(p)  gasneti_atomic_increment(p)
#define gasnett_atomic_decrement(p)  gasneti_atomic_decrement(p)
#define gasnett_atomic_decrement_and_test(p)  \
                                     gasneti_atomic_decrement_and_test(p)

/* portable memory barrier */

#define gasnett_local_membar()       gasneti_local_membar()

/* ------------------------------------------------------------------------------------ */

/* misc */
#define gasnett_sched_yield()     gasneti_sched_yield() 

#define GASNETT_IDENT(identName, identText) GASNETI_IDENT(identName, identText)

#undef _IN_GASNET_TOOLS_H
#endif

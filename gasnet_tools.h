/*   $Source: /Users/kamil/work/gasnet-cvs2/gasnet/gasnet_tools.h,v $
 *     $Date: 2004/09/01 20:23:02 $
 * $Revision: 1.10.2.4 $
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

/* portable memory barriers */

#define gasnett_local_wmb()          gasneti_local_wmb()
#define gasnett_local_rmb()          gasneti_local_rmb()
#define gasnett_local_mb()           gasneti_local_mb()
#define gasnett_compiler_fence()     gasneti_compiler_fence()

/* tight spin loop CPU hint */
#define gasnett_spinloop_hint()      gasneti_spinloop_hint() 

/* ------------------------------------------------------------------------------------ */

/* misc */
#define gasnett_sched_yield()     gasneti_sched_yield() 

#define GASNETT_IDENT(identName, identText) GASNETI_IDENT(identName, identText)

#if defined(GASNET_PAGESIZE)
  #define GASNETT_PAGESIZE GASNET_PAGESIZE
#elif defined(GASNETI_PAGESIZE)
  #define GASNETT_PAGESIZE GASNETI_PAGESIZE
#endif

#if defined(GASNETI_PAGESHIFT)
  #define GASNETT_PAGESHIFT GASNETI_PAGESHIFT
#endif


/* ------------------------------------------------------------------------------------ */

/* misc internal GASNet things we wish to expose when available */
BEGIN_EXTERNC

#if defined(_INCLUDED_GASNET_H) && defined(GASNET_TRACE)
  #define GASNETT_TRACE_SETSOURCELINE GASNETI_TRACE_SETSOURCELINE
  #define GASNETT_TRACE_GETSOURCELINE GASNETI_TRACE_GETSOURCELINE
  #define GASNETT_TRACE_PRINTF  _gasnett_trace_printf
  extern void _gasnett_trace_printf(const char *format, ...) __attribute__((__format__ (__printf__, 1, 2)));
#else
  #define GASNETT_TRACE_SETSOURCELINE(file,line) 
  #define GASNETT_TRACE_GETSOURCELINE(pfile,pline) 
  #define GASNETT_TRACE_PRINTF  _gasnett_trace_printf
  /*GASNET_INLINE_MODIFIER(_gasnett_trace_printf) 
   * causes many warnings because vararg fns cannot be inlined */
  static void _gasnett_trace_printf(const char *format, ...) { return; }
#endif

#if defined(_INCLUDED_GASNET_H) && defined(GASNET_STATS)
  /* GASNETT_STATS_INIT can be called at any time to register a callback function, which 
     will be invoked at stats dumping time (provided H stats are enabled)
     and passed a printf-like function that can be used to write output into the stats
   */
  #define GASNETT_STATS_INIT(callbackfn) \
    (gasnett_stats_callback = (callbackfn), GASNETI_STATS_ENABLED(H))
#else
  #define GASNETT_STATS_INIT(callbackfn) 0
#endif

#if defined(_INCLUDED_GASNET_H) 
  /* these tools ONLY available when linking a libgasnet.a */
  extern int gasneti_cpu_count();
  #define gasnett_cpu_count() gasneti_cpu_count()
  #ifdef HAVE_MMAP
    extern void *gasneti_mmap(uintptr_t segsize);
    #define gasnett_mmap(sz) gasneti_mmap(sz)
  #else
    #define gasnett_mmap(sz) abort()
  #endif
#else
  #define gasnett_cpu_count() abort()
  #define gasnett_mmap(sz) abort()
#endif

END_EXTERNC

#undef _IN_GASNET_TOOLS_H
#endif

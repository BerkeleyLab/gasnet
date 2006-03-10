/*   $Source: /Users/kamil/work/gasnet-cvs2/gasnet/gasnet_tools.h,v $
 *     $Date: 2006/03/10 23:17:50 $
 * $Revision: 1.56.2.2 $
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

#include <stdio.h>
#include <stdlib.h>

/* ------------------------------------------------------------------------------------ */
/* stub versions of selected gasnet internal macros needed by the headers included below */

#if !defined(_INCLUDED_GASNET_H) 
  #if defined(GASNET_NDEBUG) || defined(NDEBUG)
    #define gasneti_assert(expr) ((void)0)
  #else
    GASNET_INLINE_MODIFIER(gasneti_assert_fail)
    void gasneti_assert_fail(const char *file, int line, const char *cond) {
      fprintf(stderr, "*** FATAL ERROR: Assertion failure at %s:%i: %s\n", file, line, cond);
      abort();
    }
    #define gasneti_assert(expr) \
      (PREDICT_TRUE(expr) ? (void)0 : gasneti_assert_fail(__FILE__, __LINE__, #expr))
  #endif
#endif

/* ------------------------------------------------------------------------------------ */
/* portable memory barriers */

#include <gasnet_membar.h>

#define gasnett_local_wmb()          gasneti_local_wmb()
#define gasnett_local_rmb()          gasneti_local_rmb()
#define gasnett_local_mb()           gasneti_local_mb()
#define gasnett_compiler_fence()     gasneti_compiler_fence()

/* ------------------------------------------------------------------------------------ */
/* portable atomic increment/decrement */

#include <gasnet_atomicops.h>

#ifdef GASNET_SEQ
  /* safe to use weak atomics here, because the client is single-threaded and 
     should only be modifying atomics from the host CPU (using these calls). 
     TODO: consider exposing "signal-safe" atomics (only avail on some platforms)
  */
  #define gasnett_atomic_t             gasneti_weakatomic_t
  #define gasnett_atomic_read(p)       gasneti_weakatomicX_read(p,0)
  #define gasnett_atomic_init(v)       gasneti_weakatomic_init(v)
  #define gasnett_atomic_set(p,v)      gasneti_weakatomicX_set(p,v,0)
  #define gasnett_atomic_increment(p)  gasneti_weakatomicX_increment(p,0)
  #define gasnett_atomic_decrement(p)  gasneti_weakatomicX_decrement(p,0)
  #define gasnett_atomic_decrement_and_test(p)  \
                                       gasneti_weakatomicX_decrement_and_test(p,0)
  #ifdef gasneti_weakatomicX_compare_and_swap
    #define GASNETT_HAVE_ATOMIC_CAS 1
    #define gasnett_atomic_compare_and_swap(p,oldval,newval)  \
                                       gasneti_weakatomicX_compare_and_swap(p,oldval,newval,0)
  #endif
#else
  /* PAR, PARSYNC and non-libgasnet clients (which may have threads) */
  #define gasnett_atomic_t             gasneti_atomic_t
  #define gasnett_atomic_read(p)       gasneti_atomicX_read(p,0)
  #define gasnett_atomic_init(v)       gasneti_atomic_init(v)
  #define gasnett_atomic_set(p,v)      gasneti_atomicX_set(p,v,0)
  #define gasnett_atomic_increment(p)  gasneti_atomicX_increment(p,0)
  #define gasnett_atomic_decrement(p)  gasneti_atomicX_decrement(p,0)
  #define gasnett_atomic_decrement_and_test(p)  \
                                       gasneti_atomicX_decrement_and_test(p,0)
  #ifdef GASNETI_HAVE_ATOMIC_CAS
    #define GASNETT_HAVE_ATOMIC_CAS 1
    #define gasnett_atomic_compare_and_swap(p,oldval,newval)  \
                                       gasneti_atomicX_compare_and_swap(p,oldval,newval,0)
  #endif
#endif

/* tight spin loop CPU hint */
#define gasnett_spinloop_hint()      gasneti_spinloop_hint() 

#ifdef GASNETI_USE_GENERIC_ATOMICOPS
#define GASNETT_USING_GENERIC_ATOMICOPS
#endif
#define GASNETT_CONFIG_STRING                    \
       "PTR=" _STRINGIFY(GASNETI_PTR_CONFIG) "," \
       _STRINGIFY(GASNETI_TIMER_CONFIG) ","      \
       _STRINGIFY(GASNETI_ATOMIC_CONFIG)         

/* ------------------------------------------------------------------------------------ */
/* portable high-performance, low-overhead timers */

#include <gasnet_timer.h>

#define gasnett_tick_t               gasneti_stattime_t
#define GASNETT_TICK_MIN             GASNETI_STATTIME_MIN
#define GASNETT_TICK_MAX             GASNETI_STATTIME_MAX
#define gasnett_ticks_to_us(ticks)  (GASNETI_STATTIME_TO_NS(ticks)/1000)
#define gasnett_ticks_to_ns(ticks)   GASNETI_STATTIME_TO_NS(ticks)
#define gasnett_ticks_now()          GASNETI_STATTIME_NOW()
#define gasnett_timer_granularityus()   GASNETI_STATTIME_GRANULARITY()
#define gasnett_timer_overheadus()      GASNETI_STATTIME_OVERHEAD()

#ifdef GASNETI_USING_GETTIMEOFDAY
#define GASNETT_USING_GETTIMEOFDAY
#endif

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

#define GASNETT_CACHE_LINE_BYTES GASNETI_CACHE_LINE_BYTES

/* various configure-detected C compiler features available in only some compilers */
#define GASNETT_INLINE_MODIFIER(fnname) GASNET_INLINE_MODIFIER(fnname) 
#define GASNETT_RESTRICT                GASNETI_RESTRICT
#define GASNETT_NORETURN                GASNETI_NORETURN
#define GASNETT_NORETURNP               GASNETI_NORETURNP
#define GASNETT_MALLOC                  GASNETI_MALLOC
#define GASNETT_WARN_UNUSED_RESULT      GASNETI_WARN_UNUSED_RESULT

#define GASNETT_CURRENT_FUNCTION        GASNETI_CURRENT_FUNCTION

/* ------------------------------------------------------------------------------------ */

/* misc internal GASNet things we wish to expose when available */
BEGIN_EXTERNC

#if defined(_INCLUDED_GASNET_H) && defined(GASNET_SRCLINES)
  #define GASNETT_TRACE_SETSOURCELINE      GASNETI_TRACE_SETSOURCELINE
  #define GASNETT_TRACE_GETSOURCELINE      GASNETI_TRACE_GETSOURCELINE
  #define GASNETT_TRACE_FREEZESOURCELINE   GASNETI_TRACE_FREEZESOURCELINE
  #define GASNETT_TRACE_UNFREEZESOURCELINE GASNETI_TRACE_UNFREEZESOURCELINE
#else
  #define GASNETT_TRACE_SETSOURCELINE(file,line)    ((void)0)
  #define GASNETT_TRACE_GETSOURCELINE(pfile,pline)  ((void)0)
  #define GASNETT_TRACE_FREEZESOURCELINE()          ((void)0)
  #define GASNETT_TRACE_UNFREEZESOURCELINE()        ((void)0)
#endif

#if defined(_INCLUDED_GASNET_H) && defined(GASNET_TRACE)
  #define GASNETT_TRACE_ENABLED  GASNETI_TRACE_ENABLED(H)
  #define GASNETT_TRACE_PRINTF        _gasnett_trace_printf
  #define GASNETT_TRACE_PRINTF_FORCE  _gasnett_trace_printf_force
  extern void _gasnett_trace_printf(const char *format, ...) __attribute__((__format__ (__printf__, 1, 2)));
  extern void _gasnett_trace_printf_force(const char *format, ...) __attribute__((__format__ (__printf__, 1, 2)));
  #define GASNETT_TRACE_GETMASK()     GASNETI_TRACE_GETMASK()
  #define GASNETT_TRACE_SETMASK(mask) GASNETI_TRACE_SETMASK(mask)
  #define GASNETT_TRACE_GET_TRACELOCAL()        GASNETI_TRACE_GET_TRACELOCAL()
  #define GASNETT_TRACE_SET_TRACELOCAL(newval)  GASNETI_TRACE_SET_TRACELOCAL(newval)
#else
  #define GASNETT_TRACE_ENABLED  0
  #define GASNETT_TRACE_PRINTF        _gasnett_trace_printf
  #define GASNETT_TRACE_PRINTF_FORCE  _gasnett_trace_printf
  /*GASNET_INLINE_MODIFIER(_gasnett_trace_printf) 
   * causes many warnings because vararg fns cannot be inlined */
  static void _gasnett_trace_printf(const char *_format, ...) { 
    #ifdef __PGI
      va_list _ap; va_start(_ap,_format); va_end(_ap); /* avoid a silly warning */
    #endif
    return; 
  }
  #define GASNETT_TRACE_GETMASK()               ""
  #define GASNETT_TRACE_SETMASK(mask)           ((void)0)
  #define GASNETT_TRACE_GET_TRACELOCAL()        (0)
  #define GASNETT_TRACE_SET_TRACELOCAL(newval)  ((void)0)
#endif

#if defined(_INCLUDED_GASNET_H) && defined(GASNET_STATS)
  /* GASNETT_STATS_INIT can be called at any time to register a callback function, which 
     will be invoked at stats dumping time (provided H stats are enabled)
     and passed a printf-like function that can be used to write output into the stats
   */
  #define GASNETT_STATS_INIT(callbackfn) \
    (gasnett_stats_callback = (callbackfn), GASNETI_STATS_ENABLED(H))
  #define GASNETT_STATS_GETMASK()     GASNETI_STATS_GETMASK()
  #define GASNETT_STATS_SETMASK(mask) GASNETI_STATS_SETMASK(mask)
#else
  #define GASNETT_STATS_INIT(callbackfn) 0
  #define GASNETT_STATS_GETMASK()     ""
  #define GASNETT_STATS_SETMASK(mask) ((void)0)
#endif

#if defined(_INCLUDED_GASNET_H) 
  /* these tools ONLY available when linking a libgasnet.a */
  extern int gasneti_cpu_count();
  #define gasnett_cpu_count() gasneti_cpu_count()
  extern void gasneti_set_affinity(int);
  #define gasnett_set_affinity(r) gasneti_set_affinity(r) 
  #ifdef HAVE_MMAP
    extern void *gasneti_mmap(uintptr_t segsize);
    #define gasnett_mmap(sz) gasneti_mmap(sz)
  #else
    #define gasnett_mmap(sz) abort()
  #endif
  extern void gasneti_flush_streams();
  #define gasnett_flush_streams() gasneti_flush_streams()
  #define gasnett_print_backtrace gasneti_print_backtrace
  extern int gasneti_run_diagnostics(int iters, int threadcnt, gasnet_seginfo_t const *seginfo);
  extern void gasneti_diagnostic_gethandlers(gasnet_handlerentry_t **htable, int *htable_cnt);
  #define gasnett_run_diagnostics gasneti_run_diagnostics
  #define gasnett_diagnostic_gethandlers gasneti_diagnostic_gethandlers

  #define gasnett_threadkey_t           gasneti_threadkey_t
  #define GASNETT_THREADKEY_INITIALIZER GASNETI_THREADKEY_INITIALIZER
  #define gasnett_threadkey_get(key)                gasneti_threadkey_get(key)
  #define gasnett_threadkey_set(key,newval)         gasneti_threadkey_set(key,newval)
  #define gasnett_threadkey_init(pkey)              gasneti_threadkey_init(pkey)
  #define gasnett_threadkey_get_noinit(key)         gasneti_threadkey_get_noinit(key)
  #define gasnett_threadkey_set_noinit(key,newval)  gasneti_threadkey_set_noinit(key,newval)

  #define GASNETT_FAST_ALIGNED_MEMCPY   GASNETE_FAST_ALIGNED_MEMCPY
  #define GASNETT_FAST_UNALIGNED_MEMCPY GASNETE_FAST_UNALIGNED_MEMCPY
  #define GASNETT_VALUE_ASSIGN          GASNETE_VALUE_ASSIGN
  #define GASNETT_VALUE_RETURN          GASNETE_VALUE_RETURN

  #if GASNET_DEBUG
    #define gasnett_debug_malloc(sz)      gasneti_extern_malloc(sz) 
    #define gasnett_debug_realloc(ptr,sz) gasneti_extern_realloc((ptr),(sz))
    #define gasnett_debug_calloc(N,S)     gasneti_extern_calloc((N),(S))
    #define gasnett_debug_free(ptr)       gasneti_extern_free(ptr)  
    #define gasnett_debug_strdup(s)       gasneti_extern_strdup(s)
    #define gasnett_debug_strndup(s,n)    gasneti_extern_strndup(s,n)
    #define gasnett_debug_memcheck(ptr)   gasneti_memcheck(ptr)
    #define gasnett_debug_memcheck_one()  gasneti_memcheck_one()
    #define gasnett_debug_memcheck_all()  gasneti_memcheck_all()
    #define gasnett_heapstats_t           gasneti_heapstats_t
    #define gasnett_getheapstats(pstat)   gasneti_getheapstats(pstat)
  #endif
#else
  #define gasnett_cpu_count()     abort()
  #define gasnett_set_affinity(r) abort()
  #define gasnett_mmap(sz)        abort()
  #define gasnett_flush_streams() abort()

  #define gasnett_threadkey_t           char
  #define GASNETT_THREADKEY_INITIALIZER 0
  #define gasnett_threadkey_get(key)                abort()
  #define gasnett_threadkey_set(key,newval)         abort()
  #define gasnett_threadkey_init(pkey)              abort()
  #define gasnett_threadkey_get_noinit(key)         abort()
  #define gasnett_threadkey_set_noinit(key,newval)  abort()
#endif

END_EXTERNC

#undef _IN_GASNET_TOOLS_H
#endif

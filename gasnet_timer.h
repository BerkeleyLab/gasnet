/*   $Source: /Users/kamil/work/gasnet-cvs2/gasnet/gasnet_timer.h,v $
 *     $Date: 2004/10/12 11:04:51 $
 * $Revision: 1.28 $
 * Description: GASNet Timer library (Internal code, not for client use)
 * Copyright 2002, Dan Bonachea <bonachea@cs.berkeley.edu>
 * Terms of use are as specified in license.txt
 */

#if !defined(_IN_GASNET_H) && !defined(_IN_GASNET_TOOLS_H)
  #error This file is not meant to be included directly- clients should include gasnet.h or gasnet_tools.h
#endif

#ifndef _GASNET_TIMER_H
#define _GASNET_TIMER_H

#include <assert.h> /* permit assert.h and assert for gasnet_tools headers */
/* all of this to support gasneti_getMicrosecondTimeStamp */
#include <time.h>
#include <sys/time.h>
#include <unistd.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>

BEGIN_EXTERNC

/* ------------------------------------------------------------------------------------ */
/* High-performance system timer library 

  Implements high-granularity, low-overhead timers using system-specific support, where available

  Interface:
    gasneti_stattime_t - timer datatype representing an integer number of "ticks"
      where a "tick" has a system-specific interpretation
      safe to be handled using integer operations (+,-,<,>,==)
    GASNETI_STATTIME_NOW() - returns the current tick count as a gasneti_stattime_t
    GASNETI_STATTIME_TO_US(stattime) - convert ticks to microseconds as a uint64_t
    GASNETI_STATTIME_MIN - a value representing the minimum value storable in a gasneti_stattime_t
    GASNETI_STATTIME_MAX - a value representing the maximum value storable in a gasneti_stattime_t
*/

/* completely portable (low-performance) microsecond granularity wall-clock timer */
GASNET_INLINE_MODIFIER(gasneti_getMicrosecondTimeStamp)
int64_t gasneti_getMicrosecondTimeStamp(void) {
  int64_t retval;
  struct timeval tv;
  retry:
  if (gettimeofday(&tv, NULL)) {
      perror("gettimeofday");
      abort();
  }
  retval = ((int64_t)tv.tv_sec) * 1000000 + tv.tv_usec;
  #ifdef __crayx1
    /* fix an empirically observed bug in UNICOS gettimeofday(),
       which occasionally returns ridiculously incorrect values
       SPR 728120, fixed in kernel 2.4.34 
     */
    if_pf(retval < (((int64_t)3) << 48)) goto retry;
  #endif
  return retval;
}

#if defined(GASNETC_CONDUIT_SPECIFIC_TIMERS)
  #if !defined(GASNETI_STATTIME_TO_US) || !defined(GASNETI_STATTIME_NOW)
    #error Incomplete conduit-specific timer impl.
  #endif
#elif defined(AIX)
  #include <sys/time.h>
  #include <sys/systemcfg.h>

  /* we want to avoid expensive divide and conversion operations during collection, 
     but timebasestruct_t structs are too difficult to perform arithmetic on
     we stuff the internal cycle counter into a 64-bit holder and expand to realtime later */
  typedef uint64_t gasneti_stattime_t;
  GASNET_INLINE_MODIFIER(gasneti_stattime_now)
  gasneti_stattime_t gasneti_stattime_now() {
    timebasestruct_t t;
    read_real_time(&t,TIMEBASE_SZ);
    return (((uint64_t)t.tb_high) << 32) | ((uint64_t)t.tb_low);
  }
  GASNET_INLINE_MODIFIER(gasneti_stattime_to_us)
  uint64_t gasneti_stattime_to_us(gasneti_stattime_t st) {
    timebasestruct_t t;
    read_real_time(&t,TIMEBASE_SZ);
    assert(t.flag == RTC_POWER_PC); /* otherwise timer arithmetic (min/max/sum) is compromised */

    t.tb_high = (uint32_t)(st >> 32);
    t.tb_low =  (uint32_t)(st);
    time_base_to_time(&t,TIMEBASE_SZ);
    return (((uint64_t)t.tb_high) * 1000000) + (t.tb_low/1000);
  }
  #define GASNETI_STATTIME_TO_US(st)  (gasneti_stattime_to_us(st))
  #define GASNETI_STATTIME_NOW()      (gasneti_stattime_now())
#elif defined(CRAYT3E) || defined(CRAYX1)
  #if defined(CRAYT3E) 
    #include <sys/machinfo.h>
    /* 75 Mhz sys. clock, according to docs */
    #define GASNETI_UNICOS_SYS_CLOCK 75 
  #elif defined(CRAYX1)
    #include <intrinsics.h>
    /* 100 Mhz sys. clock, according to Fortran IRTC_RATE() */
    #define GASNETI_UNICOS_SYS_CLOCK 100
  #endif
  #ifdef __GNUC__
    #define _rtc rtclock
  #else
    long    _rtc();
  #endif

  typedef uint64_t gasneti_stattime_t;

  #if 0
    #define GASNETI_STATTIME_TO_US(st)  ((st) * 1000000 / GetMachineInfo(mi_hz))
  #else
    /* 75 Mhz sys. clock */
    #define GASNETI_STATTIME_TO_US(st)  ((st) / GASNETI_UNICOS_SYS_CLOCK)
  #endif
  #define GASNETI_STATTIME_NOW()      (_rtc())
#elif defined(IRIX)
  #include <time.h>
  #include <sys/ptimers.h>

  typedef uint64_t gasneti_stattime_t;
  GASNET_INLINE_MODIFIER(gasneti_stattime_now)
  gasneti_stattime_t gasneti_stattime_now() {
    struct timespec t;
    if (clock_gettime(CLOCK_SGI_CYCLE, &t) == -1) abort();
    return ((((uint64_t)t.tv_sec) & 0xFFFF) * 1000000000) + t.tv_nsec;
  }
  #define GASNETI_STATTIME_TO_US(st)  ((st)/1000)
  #define GASNETI_STATTIME_NOW()      (gasneti_stattime_now())
#elif defined(SOLARIS)
#if 1
  /* workaround bizarre failures on gcc 3.2.1 - seems they sometimes use a
     union to implement longlong_t and hence hrtime_t, and the test to
     determine this is (__STDC__ - 0 == 0) which is totally bogus */
  typedef uint64_t gasneti_stattime_t;
  GASNET_INLINE_MODIFIER(gasneti_stattime_now)
  gasneti_stattime_t gasneti_stattime_now() {
    hrtime_t t = gethrtime();
    return *(gasneti_stattime_t *)&t;
  }
  #define GASNETI_STATTIME_TO_US(st)  ((st)/1000)
  #define GASNETI_STATTIME_NOW()      (gasneti_stattime_now())
#else
  typedef hrtime_t gasneti_stattime_t;
  GASNET_INLINE_MODIFIER(gasneti_stattime_to_us)
  uint64_t gasneti_stattime_to_us(gasneti_stattime_t st) {
    assert(sizeof(gasneti_stattime_t) == 8);
    return (*(uint64_t*)&st)/1000;
  }
  #define GASNETI_STATTIME_MAX        ((gasneti_stattime_t)(((uint64_t)-1)>>1))
  #define GASNETI_STATTIME_TO_US(st)  (gasneti_stattime_to_us(st))
  #define GASNETI_STATTIME_NOW()      (gethrtime())
#endif
#elif defined(LINUX) && defined(__GNUC__) && \
     (defined(__i386__) || defined(__x86_64__) || defined(__ia64__))
  #include <stdio.h>
  #include <stdlib.h>
  #include <string.h>
  #include <math.h>
  #if defined(__ia64__) && defined(__INTEL_COMPILER)
    #include <ia64intrin.h>
  #endif
  typedef uint64_t gasneti_stattime_t;
  GASNET_INLINE_MODIFIER(gasneti_stattime_now)
  uint64_t gasneti_stattime_now (void) {
    uint64_t ret;
    #if defined(__i386__)
      __asm__ __volatile__("rdtsc"
                           : "=A" (ret)
                           : /* no inputs */); 
    #elif defined(__ia64__) && defined(__INTEL_COMPILER)
      ret = (uint64_t)__getReg(_IA64_REG_AR_ITC);
    #elif defined(__ia64__) 
      __asm__ __volatile__("mov %0=ar.itc" 
                           : "=r"(ret) 
                           : /* no inputs */);
    #elif defined(__x86_64__)
      uint32_t lo, hi;
      __asm__ __volatile__("rdtsc"
                           : "=a" (lo), "=d" (hi)
                           : /* no inputs */); 
      ret = ((uint64_t)lo) | (((uint64_t)hi)<<32);
    #else
      #error "unsupported CPU"
    #endif
    return ret;
  } 
  GASNET_INLINE_MODIFIER(gasneti_stattime_to_us)
  uint64_t gasneti_stattime_to_us(gasneti_stattime_t st) {
    static int firstTime = 1;
    static double Tick = 0.0;
    if_pf (firstTime) {
      FILE *fp = fopen("/proc/cpuinfo","r");
      char input[255];
      if (!fp) {
        fprintf(stderr,"*** ERROR: Failure in fopen('/proc/cpuinfo','r')=%s",strerror(errno));
        abort();
      }
      while (!feof(fp) && fgets(input, 255, fp)) {
        if (strstr(input,"cpu MHz")) {
          char *p = strchr(input,':');
	  double MHz = 0.0;
          if (p) MHz = atof(p+1);
          assert(MHz > 1 && MHz < 100000); /* ensure it looks reasonable */
          Tick = 1. / MHz;
          break;
        }
      }
      fclose(fp);
      assert(Tick != 0.0);
      firstTime = 0;
    }
    return (uint64_t)(st * Tick);
  }
  #define GASNETI_STATTIME_TO_US(st)  (gasneti_stattime_to_us(st))
  #define GASNETI_STATTIME_NOW()      (gasneti_stattime_now())
#elif 0 && defined(OSF)
  /* the precision for this is no better than gettimeofday (~1 ms) */
  /* TODO: use elan real-time counter, or rpcc instruction (which returns
     a 32-bit cycle count that wraps too quickly to be useful by itself)
     luckily, the Quadrics NIC provides a nanosecond clock (with ~1us overhead)
   */
  #include <time.h>

  typedef uint64_t gasneti_stattime_t;
  GASNET_INLINE_MODIFIER(gasneti_stattime_now)
  gasneti_stattime_t gasneti_stattime_now() {
    struct timespec t;
    if (clock_gettime(CLOCK_REALTIME, &t) == -1) abort();
    return ((((uint64_t)t.tv_sec) & 0xFFFF) * 1000000000) + t.tv_nsec;
  }
  #define GASNETI_STATTIME_TO_US(st)  ((st)/1000)
  #define GASNETI_STATTIME_NOW()      (gasneti_stattime_now())
#elif defined(CYGWIN)
  #include <windows.h>
  /* note: QueryPerformanceCounter is a Win32 system call and thus has ~1us overhead
     Most systems have a QueryPerformanceFrequency() == 3,579,545, which is the
     ACPI counter that should be reliable across CPU cycle speedstepping, etc.
     rdtsc has lower overhead, but only works on Pentium or later,
     produces wildly incorrect results if the  CPU decides to change clock rate 
     mid-run (and there's no reliable way to get the correct cycle multiplier 
     short of timing a known-length delay and hoping for the best)
     See http://www.geisswerks.com/ryan/FAQS/timing.html
         http://softwareforums.intel.com/ids/board/message?board.id=16&message.id=1509
  */
  typedef uint64_t gasneti_stattime_t;
  GASNET_INLINE_MODIFIER(gasneti_stattime_now)
  gasneti_stattime_t gasneti_stattime_now() {
    LARGE_INTEGER val;
    if_pf (!QueryPerformanceCounter(&val)) abort();
    assert(val.QuadPart > 0);
    return (gasneti_stattime_t)val.QuadPart;
  }
  GASNET_INLINE_MODIFIER(gasneti_stattime_to_us)
  uint64_t gasneti_stattime_to_us(gasneti_stattime_t st) {
    static int firsttime = 1;
    static double freq = 0;
    if_pf (firsttime) {
      LARGE_INTEGER temp;
      if (!QueryPerformanceFrequency(&temp)) abort();
      freq = ((double)temp.QuadPart) / 1000000.0;
      freq = 1 / freq;
      firsttime = 0;
    }
    return (uint64_t)(st * freq);
  }
  #define GASNETI_STATTIME_TO_US(st)  (gasneti_stattime_to_us(st))
  #define GASNETI_STATTIME_NOW()      (gasneti_stattime_now())
#else
  #define GASNETI_USING_GETTIMEOFDAY
  /* portable microsecond granularity wall-clock timer */
  typedef uint64_t gasneti_stattime_t;
  #define GASNETI_STATTIME_TO_US(st)  (st)
  #define GASNETI_STATTIME_NOW()      ((gasneti_stattime_t)gasneti_getMicrosecondTimeStamp())
#endif

#ifndef GASNETI_STATTIME_MIN
#define GASNETI_STATTIME_MIN        ((gasneti_stattime_t)0)
#endif
#ifndef GASNETI_STATTIME_MAX
#define GASNETI_STATTIME_MAX        ((gasneti_stattime_t)-1)
#endif

/* return a double value representing the approximate microsecond
   overhead and granularity of the current timers. Overhead is the 
   average wall-clock time consumed while reading the timer value once,
   and granularity is the minimum observable non-zero interval between 
   two timer readings (which may be limited only by overhead, or may 
   be significantly higher on systems where the underlying timer 
   advances in discrete "jumps" of time much larger than the overhead)
   When measuring an event of length (L) using two surrounding timer calls,
   the measured time interval will be: L + overhead +- granularity
*/
#define GASNETI_STATTIME_GRANULARITY() gasneti_stattime_metric(0)
#define GASNETI_STATTIME_OVERHEAD()    gasneti_stattime_metric(1)
#if !defined(__cplusplus)
  /* use a tentative definition, so all files can share the same metric
     data structures, and to ensure we pay the timing overhead at most once per run */
  extern double *_gasneti_stattime_metric;
  double *_gasneti_stattime_metric; 
#else
  /* C++ outlaws tentative definitions, and we have no other place to 
     reliably place this data in gasnet_tools mode. 
     So we're forced to place it in each compilation unit and possibly
     pay one timing overhead for each compilation unit that asks
   */
  static double *_gasneti_stattime_metric; 
#endif
GASNET_INLINE_MODIFIER(gasneti_stattime_metric)
double gasneti_stattime_metric(unsigned int idx) {
  assert(idx <= 1);
  if_pf (_gasneti_stattime_metric == NULL) {
    int i, ticks, iters = 1000, minticks = 10;
    gasneti_stattime_t min = GASNETI_STATTIME_MAX;
    gasneti_stattime_t start = GASNETI_STATTIME_NOW();
    gasneti_stattime_t last = start;
    for (i=0,ticks=0; i < iters || ticks < minticks; i++) {
      gasneti_stattime_t x = GASNETI_STATTIME_NOW();
      gasneti_stattime_t curr = (x - last);
      if_pt (curr > 0) { 
        ticks++;
        if_pf (curr < min) min = curr;
      }
      last = x;
    }
    _gasneti_stattime_metric = (double *)malloc(2*sizeof(double));
    assert(_gasneti_stattime_metric != NULL);
    /* granularity */
    _gasneti_stattime_metric[0] = ((double)GASNETI_STATTIME_TO_US(min*1000))/1000.0;
    /* overhead */
    _gasneti_stattime_metric[1] = ((double)(GASNETI_STATTIME_TO_US(last - start)))/i;
  }
  return _gasneti_stattime_metric[idx];
}
/* ------------------------------------------------------------------------------------ */

END_EXTERNC

#endif

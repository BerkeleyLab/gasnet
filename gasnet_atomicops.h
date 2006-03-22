/*   $Source: /Users/kamil/work/gasnet-cvs2/gasnet/gasnet_atomicops.h,v $
 *     $Date: 2006/03/22 00:24:31 $
 * $Revision: 1.94.2.4 $
 * Description: GASNet header for portable atomic memory operations
 * Copyright 2002, Dan Bonachea <bonachea@cs.berkeley.edu>
 * Terms of use are as specified in license.txt
 */

#if !defined(_IN_GASNET_TOOLS_H) && !defined(_IN_GASNET_H)
  #error This file is not meant to be included directly- clients should include gasnet.h or gasnet_tools.h
#endif

#ifndef _GASNET_ATOMICOPS_H
#define _GASNET_ATOMICOPS_H

/* ------------------------------------------------------------------------------------ */
/* portable atomic increment/decrement 
   -----------------------------------
   these provide a special datatype (gasneti_atomic_t) representing an atomically
    updated unsigned integer value and a set of atomic ops
   atomicity is guaranteed only if ALL accesses to the gasneti_atomic_t data happen
    through the provided operations (i.e. it is an error to directly access the 
    contents of a gasneti_atomic_t), and if the gasneti_atomic_t data is only  
    addressable by the current process (e.g. not in a System V shared memory segment)

    gasneti_atomic_init(v)        initializer for an gasneti_atomic_t to value v
    gasneti_atomic_set(p,v,f)     atomically sets *p to value v
    gasneti_atomic_read(p,f)      atomically read and return the value of *p
    gasneti_atomic_increment(p,f) atomically increment *p (no return value)
    gasneti_atomic_decrement(p,f) atomically decrement *p (no return value)
    gasneti_atomic_decrement_and_test(p,f) 
      atomically decrement *p, return non-zero iff the new value is 0

   Semi-portable atomic compare and swap:
   -------------------------------------
   This useful operation is not available on all platforms
   On platforms where it is implemented

     gasneti_atomic_compare_and_swap(p, oldval, newval, flags)

   is the atomic equivalent of:

    if (*p == oldval) {
      *p = newval;
      return NONZERO;
    } else {
      return 0;
    }

   GASNETI_HAVE_ATOMIC_CAS will be defined to 1 on platforms supporting this operation.


   NOTE: Atomic operations have no default memory fence properties, as this
   varies by platform.  Every atomic operation except _init() includes a final
   argument (f or flags) to indicate the caller's minimum fence requirements.
   Depending on the platform, the implementation may use fences stronger than
   those requested, but never weaker.
 */

#if defined(GASNETI_FORCE_GENERIC_ATOMICOPS) || /* for debugging */          \
    defined(CRAYT3E)   || /* T3E seems to have no atomic ops */              \
    defined(_SX)       || /* NEC SX-6 atomics not available to user code? */ \
    (defined(__PGI) && defined(BROKEN_LINUX_ASM_ATOMIC_H)) || /* haven't implemented atomics for PGI */ \
    defined(__SUNPRO_C) || defined(__SUNPRO_CC) /* haven't implemented atomics for SunCC */
  #define GASNETI_USE_GENERIC_ATOMICOPS
#endif
/* misc rerequisites to detection logic below */
#if defined(__linux__) && !defined(BROKEN_LINUX_ASM_ATOMIC_H)
  #include <linux/config.h>
#endif

/* ------------------------------------------------------------------------------------ */
/* Flags for memory fences */
#define GASNETI_ATOMIC_NONE			0x00
#define GASNETI_ATOMIC_RMB_PRE			0x01
#define GASNETI_ATOMIC_WMB_PRE			0x02
#define GASNETI_ATOMIC_RMB_POST			0x04
#define GASNETI_ATOMIC_WMB_POST			0x08
#define GASNETI_ATOMIC_RMB_POST_IF_TRUE		0x10
#define GASNETI_ATOMIC_RMB_POST_IF_FALSE	0x20

#define GASNETI_ATOMIC_MB_PRE		(GASNETI_ATOMIC_WMB_PRE | GASNETI_ATOMIC_RMB_PRE)
#define GASNETI_ATOMIC_MB_POST		(GASNETI_ATOMIC_WMB_POST | GASNETI_ATOMIC_RMB_POST)

#define GASNETI_ATOMIC_REL		GASNETI_ATOMIC_WMB_PRE
#define GASNETI_ATOMIC_ACQ		GASNETI_ATOMIC_RMB_POST
#define GASNETI_ATOMIC_ACQ_IF_TRUE	GASNETI_ATOMIC_RMB_POST_IF_TRUE
#define GASNETI_ATOMIC_ACQ_IF_FALSE	GASNETI_ATOMIC_RMB_POST_IF_FALSE

/* ------------------------------------------------------------------------------------ */
#ifdef GASNETI_USE_GENERIC_ATOMICOPS
  /* a very slow but portable implementation of atomic ops */
  #define GASNETI_ATOMICOPS_NOT_SIGNALSAFE 1
  #ifdef _INCLUDED_GASNET_H
    extern void *gasneti_patomicop_lock; /* bug 693: avoid header dependency cycle */
    typedef struct { volatile uint32_t ctr; } gasneti_atomic_t;
    #define _gasneti_atomic_read(p)      ((p)->ctr)
    #define _gasneti_atomic_init(v)      { (v) }
    #define _gasneti_atomic_set(p,v) do {                         \
        gasnet_hsl_lock((gasnet_hsl_t*)gasneti_patomicop_lock);   \
        (p)->ctr = (v);                                           \
        gasnet_hsl_unlock((gasnet_hsl_t*)gasneti_patomicop_lock); \
      } while (0)
    #define _gasneti_atomic_increment(p) do {                     \
        gasnet_hsl_lock((gasnet_hsl_t*)gasneti_patomicop_lock);   \
        ((p)->ctr)++;                                             \
        gasnet_hsl_unlock((gasnet_hsl_t*)gasneti_patomicop_lock); \
      } while (0)
    #define _gasneti_atomic_decrement(p) do {                     \
        gasnet_hsl_lock((gasnet_hsl_t*)gasneti_patomicop_lock);   \
        ((p)->ctr)--;                                             \
        gasnet_hsl_unlock((gasnet_hsl_t*)gasneti_patomicop_lock); \
      } while (0)
    extern int _gasneti_atomic_decrement_and_test(gasneti_atomic_t *p);
    #define GASNETI_GENERIC_DEC_AND_TEST_DEF                      \
    int _gasneti_atomic_decrement_and_test(gasneti_atomic_t *p) { \
      uint32_t newval;                                            \
      gasnet_hsl_lock((gasnet_hsl_t*)gasneti_patomicop_lock);     \
      newval = p->ctr - 1;                                        \
      p->ctr = newval;                                            \
      gasnet_hsl_unlock((gasnet_hsl_t*)gasneti_patomicop_lock);   \
      return (newval == 0);                                       \
    }
    extern int _gasneti_atomic_compare_and_swap(gasneti_atomic_t *p, uint32_t oldval, uint32_t newval);
    #define GASNETI_GENERIC_CAS_DEF                              \
    int _gasneti_atomic_compare_and_swap(gasneti_atomic_t *p,    \
                                        uint32_t oldval,         \
                                        uint32_t newval) {       \
      int retval;                                                \
      gasnet_hsl_lock((gasnet_hsl_t*)gasneti_patomicop_lock);    \
      retval = (p->ctr == oldval);                               \
      if_pt (retval) {                                           \
        p->ctr = newval;                                         \
      }                                                          \
      gasnet_hsl_unlock((gasnet_hsl_t*)gasneti_patomicop_lock);  \
      return retval;                                             \
    }
    #define GASNETI_HAVE_ATOMIC_CAS 1
    #if (GASNET_PAR || GASNETI_CONDUIT_THREADS)
      /* Using real HSLs which yeild an ACQ/RMB before and REL/WMB after the atomic */
      #define GASNETI_ATOMIC_FENCE_SET (GASNETI_ATOMIC_RMB_PRE | GASNETI_ATOMIC_WMB_POST)
      #define GASNETI_ATOMIC_FENCE_RMB (GASNETI_ATOMIC_RMB_PRE | GASNETI_ATOMIC_WMB_POST)
    #else
      /* HSLs compile away, so use defaults */
    #endif
  #elif defined(_REENTRANT) || defined(_THREAD_SAFE) || \
        defined(PTHREAD_MUTEX_INITIALIZER) ||           \
        defined(HAVE_PTHREAD) || defined(HAVE_PTHREAD_H)
    /* a version for pthreads which is independent of GASNet HSL's 
       requires the client to #define GASNETT_MAIN in exactly one linked file 
     */
    #include <pthread.h>
    extern pthread_mutex_t gasneti_atomicop_mutex; 
    extern int gasneti_atomicop_initcheck;
    #ifdef GASNETT_MAIN
      pthread_mutex_t gasneti_atomicop_mutex = PTHREAD_MUTEX_INITIALIZER;
      int gasneti_atomicop_initcheck = 1;
    #endif
    #if !defined(NDEBUG) && !defined(GASNET_NDEBUG)
      #define GASNETI_ATOMICOP_INITCHECK() do {                                          \
        if (!gasneti_atomicop_initcheck) {                                               \
          fprintf(stderr, "ERROR: on this platform, gasnet_tools.h "                     \
           "requires exactly one file to #define GASNETT_MAIN in order to use atomics"); \
          abort();                                                                       \
        }                                                                                \
      } while (0)
    #else
      #define GASNETI_ATOMICOP_INITCHECK() ((void)0)
    #endif
    /* intentionally make these a different size than regular 
       GASNet atomics, to cause a link error on attempts to mix them
     */
    typedef struct { volatile uint32_t ctr; char _pad; } gasneti_atomic_t;
    #define _gasneti_atomic_read(p)      ((p)->ctr)
    #define _gasneti_atomic_init(v)      { (v) }
    #define _gasneti_atomic_set(p,v) do {              \
        GASNETI_ATOMICOP_INITCHECK();                  \
        pthread_mutex_lock(&gasneti_atomicop_mutex);   \
        (p)->ctr = (v);                                \
        pthread_mutex_unlock(&gasneti_atomicop_mutex); \
      } while (0)
    #define _gasneti_atomic_increment(p) do {          \
        GASNETI_ATOMICOP_INITCHECK();                  \
        pthread_mutex_lock(&gasneti_atomicop_mutex);   \
        ((p)->ctr)++;                                  \
        pthread_mutex_unlock(&gasneti_atomicop_mutex); \
      } while (0)
    #define _gasneti_atomic_decrement(p) do {          \
        GASNETI_ATOMICOP_INITCHECK();                  \
        pthread_mutex_lock(&gasneti_atomicop_mutex);   \
        ((p)->ctr)--;                                  \
        pthread_mutex_unlock(&gasneti_atomicop_mutex); \
      } while (0)
    GASNETI_INLINE(_gasneti_atomic_decrement_and_test)
    int _gasneti_atomic_decrement_and_test(gasneti_atomic_t *p) {
      uint32_t newval;
      GASNETI_ATOMICOP_INITCHECK();
      pthread_mutex_lock(&gasneti_atomicop_mutex);
      newval = p->ctr - 1;
      p->ctr = newval;
      pthread_mutex_unlock(&gasneti_atomicop_mutex);
      return (newval == 0);
    }
    GASNETI_INLINE(_gasneti_atomic_compare_and_swap)
    int _gasneti_atomic_compare_and_swap(gasneti_atomic_t *p, 
                           uint32_t oldval, uint32_t newval) {
      int retval;
      GASNETI_ATOMICOP_INITCHECK();
      pthread_mutex_lock(&gasneti_atomicop_mutex);
      retval = (p->ctr == oldval);
      if_pt (retval) {
        p->ctr = newval;
      }
      pthread_mutex_unlock(&gasneti_atomicop_mutex);
      return retval;
    }
    #define GASNETI_HAVE_ATOMIC_CAS 1
    #if (defined(__APPLE__) && defined(__MACH__))
      /* OSX/Darwin tries to be too smart when only 1 thread is running, so use defaults */
      /* XXX: determine what fence (if any) might still be present? */
    #else
      /* Using real mutexes which yeild an ACQ/RMB before and REL/WMB after the atomic */
      #define GASNETI_ATOMIC_FENCE_SET (GASNETI_ATOMIC_RMB_PRE | GASNETI_ATOMIC_WMB_POST)
      #define GASNETI_ATOMIC_FENCE_RMB (GASNETI_ATOMIC_RMB_PRE | GASNETI_ATOMIC_WMB_POST)
    #endif
  #else
    /* only one thread - everything atomic by definition */
    /* attempt to generate a compile error if pthreads actually are in use */
    #define PTHREAD_MUTEX_INITIALIZER ERROR_include_pthread_h_before_gasnet_tools_h
    extern int pthread_mutex_lock; 

    typedef volatile int gasneti_atomic_t;
    #define _gasneti_atomic_read(p)      (*(p))
    #define _gasneti_atomic_init(v)      (v)
    #define _gasneti_atomic_set(p,v)     (*(p) = (v))
    #define _gasneti_atomic_increment(p) ((*(p))++)
    #define _gasneti_atomic_decrement(p) ((*(p))--)
    #define _gasneti_atomic_decrement_and_test(p) ((--(*(p))) == 0)
    #define _gasneti_atomic_compare_and_swap(p,oldval,newval) \
              (*(p) == (oldval) ? *(p) = (newval), 1 : 0)
    /* bug1405: using default fences */
  #endif
#else
  /* ------------------------------------------------------------------------------------
   * Prefer OS-provided atomics, which should be CPU-independent and
   * which should work regardless of the compiler's inline assembly support
   * The exception is the sometimes broken Linux asm/atomic.h
   * ------------------------------------------------------------------------------------ */
  #if defined(AIX)
      #include <sys/atomic_op.h>
      typedef struct { volatile int ctr; } gasneti_atomic_t;
      #define _gasneti_atomic_increment(p) (fetch_and_add((atomic_p)&((p)->ctr),1))
      #define _gasneti_atomic_decrement(p) (fetch_and_add((atomic_p)&((p)->ctr),-1))
      #define _gasneti_atomic_read(p)      ((p)->ctr)
      #define _gasneti_atomic_set(p,v)     ((p)->ctr = (v))
      #define _gasneti_atomic_init(v)      { (v) }
      #define _gasneti_atomic_decrement_and_test(p) \
			(fetch_and_add((atomic_p)&((p)->ctr),-1) == 1)
      GASNETI_INLINE(_gasneti_atomic_compare_and_swap)
      int _gasneti_atomic_compare_and_swap(gasneti_atomic_t *p, int oldval, int newval) {
        return compare_and_swap( (atomic_p)p, &oldval, newval );
      } 
      #define GASNETI_HAVE_ATOMIC_CAS 1
      /* bug1405: no syncs in these calls, so use default fences */
  #elif defined(IRIX)
      #include <mutex.h>
      typedef __uint32_t gasneti_atomic_t;
      #define _gasneti_atomic_increment(p) (test_then_add32((p),1))
      #define _gasneti_atomic_decrement(p) (test_then_add32((p),(uint32_t)-1))
      #define _gasneti_atomic_read(p)      (*(volatile __uint32_t *)(p))
      #define _gasneti_atomic_set(p,v)     (*(volatile __uint32_t *)(p) = (v))
      #define _gasneti_atomic_init(v)      (v)
      #define _gasneti_atomic_decrement_and_test(p) \
                                          (add_then_test32((p),(uint32_t)-1) == 0) 
      GASNETI_INLINE(_gasneti_atomic_compare_and_swap)
      int _gasneti_atomic_compare_and_swap(gasneti_atomic_t *p, int oldval, int newval) {
        return __compare_and_swap( p, oldval, newval );
      } 
      #define GASNETI_HAVE_ATOMIC_CAS 1
      /* XXX bug1405: using default fences (TODO: VERIFY THAT WE NEED THEM) */
  #elif defined(__MTA__)
      /* use MTA intrinsics */
      typedef int64_t gasneti_atomic_t;
      #define _gasneti_atomic_increment(p) (int_fetch_add((p),1))
      #define _gasneti_atomic_decrement(p) (int_fetch_add((p),-1))
      #define _gasneti_atomic_read(p)      (*(volatile int64_t)(p))
      #define _gasneti_atomic_set(p,v)     (*(volatile int64_t)(p) = (v))
      #define _gasneti_atomic_init(v)      (v)
      #define _gasneti_atomic_decrement_and_test(p) \
                                          (int_fetch_add((p),-1) == 1) 
      /* XXX bug1405: using default fences (TODO: VERIFY THAT WE NEED THEM) */
  #elif 0 && defined(SOLARIS) /* DISABLED */
      /* $%*(! Solaris has atomic functions in the kernel but refuses to expose them
         to the user... after all, what application would be interested in performance? */
      #include <sys/atomic.h>
      typedef struct { volatile uint32_t ctr; } gasneti_atomic_t;
      #define _gasneti_atomic_increment(p) (atomic_add_32((uint32_t *)&((p)->ctr),1))
      #define _gasneti_atomic_read(p)      ((p)->ctr)
      #define _gasneti_atomic_set(p,v)     ((p)->ctr = (v))
      #define _gasneti_atomic_init(v)      { (v) }
  #elif defined(CYGWIN)
      #include <windows.h>
      typedef struct { volatile uint32_t ctr; } gasneti_atomic_t;
      #define _gasneti_atomic_increment(p) InterlockedIncrement((LONG *)&((p)->ctr))
      #define _gasneti_atomic_decrement(p) InterlockedDecrement((LONG *)&((p)->ctr))
      #define _gasneti_atomic_read(p)      ((p)->ctr)
      #define _gasneti_atomic_set(p,v)     ((p)->ctr = (v))
      #define _gasneti_atomic_init(v)      { (v) }
      #define _gasneti_atomic_decrement_and_test(p) \
                                          (InterlockedDecrement((LONG *)&((p)->ctr)) == 0)
      #define _gasneti_atomic_compare_and_swap(p,oval,nval) \
	   (InterlockedCompareExchange((LONG *)&((p)->ctr),nval,oval) == (oval))
      #define GASNETI_HAVE_ATOMIC_CAS 1
      /* bug1405: x86 and x86_64 include full memory fence in locked RMW insns */
      #define GASNETI_ATOMIC_FENCE_RMW (GASNETI_ATOMIC_MB_PRE | GASNETI_ATOMIC_MB_POST)
  /* ------------------------------------------------------------------------------------
   * No OS-provided atomics, so try to provide our own, based on the CPU and compiler 
   * support for inline assembly code
   * ------------------------------------------------------------------------------------ */
  #elif defined(__i386__) || defined(__x86_64__) /* x86 and Athlon/Opteron */
    #if defined(__GNUC__) || defined(__INTEL_COMPILER) || defined(__PATHCC__)
      #ifdef GASNETI_UNI_BUILD
        #define GASNETI_LOCK ""
      #else
        #define GASNETI_LOCK "lock\n\t"
      #endif
      #if defined(__PATHCC__)
        /* Pathscale optimizer is buggy and fails to clobber memory output location correctly
           unless we include an extraneous full memory clobber 
         */
        #define GASNETI_ATOMIC_MEM_CLOBBER ,"memory"
      #else
        #define GASNETI_ATOMIC_MEM_CLOBBER
      #endif
      typedef struct { volatile int ctr; } gasneti_atomic_t;
      #define _gasneti_atomic_read(p)      ((p)->ctr)
      #define _gasneti_atomic_init(v)      { (v) }
      #define _gasneti_atomic_set(p,v)     ((p)->ctr = (v))
      GASNETI_INLINE(_gasneti_atomic_increment)
      void _gasneti_atomic_increment(gasneti_atomic_t *v) {
        __asm__ __volatile__(
                GASNETI_LOCK "incl %0"
                : "=m" (v->ctr)
                : "m" (v->ctr)
                : "cc" GASNETI_ATOMIC_MEM_CLOBBER);
      }
      GASNETI_INLINE(_gasneti_atomic_decrement)
      void _gasneti_atomic_decrement(gasneti_atomic_t *v) {
        __asm__ __volatile__(
                GASNETI_LOCK "decl %0"
                : "=m" (v->ctr)
                : "m" (v->ctr) 
                : "cc" GASNETI_ATOMIC_MEM_CLOBBER);
      }
      GASNETI_INLINE(_gasneti_atomic_decrement_and_test)
      int _gasneti_atomic_decrement_and_test(gasneti_atomic_t *v) {
          unsigned char c;
          __asm__ __volatile__(
	          GASNETI_LOCK "decl %0\n\tsete %1"
	          : "=m" (v->ctr), "=mq" (c)
	          : "m" (v->ctr) 
                  : "cc" GASNETI_ATOMIC_MEM_CLOBBER);
          return (c != 0);
      }
      GASNETI_INLINE(_gasneti_atomic_compare_and_swap)
      int _gasneti_atomic_compare_and_swap(gasneti_atomic_t *v, uint32_t oldval, uint32_t newval) {
        register unsigned char retval;
        register uint32_t readval;
        __asm__ __volatile__ (GASNETI_LOCK "cmpxchgl %3, %1\n\tsete %0"
			          : "=mq" (retval), "=m" (v->ctr), "=a" (readval)
			          : "r" (newval), "m" (v->ctr), "a" (oldval)
			          : "cc", "memory");
        return (int)retval;
      }
      #define GASNETI_HAVE_ATOMIC_CAS 1
      /* bug1405: x86 and x86_64 include full memory fence in locked RMW insns */
      #define GASNETI_ATOMIC_FENCE_RMW (GASNETI_ATOMIC_MB_PRE | GASNETI_ATOMIC_MB_POST)
    #else
      #error unrecognized x86 compiler - need to implement GASNet atomics (or #define GASNETI_USE_GENERIC_ATOMICOPS)
    #endif
  /* ------------------------------------------------------------------------------------ */
  #elif defined(__ia64__) || defined(__ia64) /* Itanium */
    #if defined(__INTEL_COMPILER)
      /* Intel compiler's inline assembly broken on Itanium (bug 384) - use intrinsics instead */
      #include <ia64intrin.h>
      typedef struct { volatile uint32_t ctr; } gasneti_atomic_t;
      #define _gasneti_atomic_increment(p) _InterlockedIncrement((volatile int *)&((p)->ctr))
      #define _gasneti_atomic_decrement(p) _InterlockedDecrement((volatile int *)&((p)->ctr))
      #define _gasneti_atomic_read(p)      ((p)->ctr)
      #define _gasneti_atomic_set(p,v)     ((p)->ctr = (v))
      #define _gasneti_atomic_init(v)      { (v) }
      #define _gasneti_atomic_decrement_and_test(p) \
                    (_InterlockedDecrement((volatile int *)&((p)->ctr)) == 0)
      #define _gasneti_atomic_compare_and_swap(p,oval,nval) \
                    (_InterlockedCompareExchange((volatile int *)&((p)->ctr),nval,oval) == (oval))
      #define GASNETI_HAVE_ATOMIC_CAS 1
      /* XXX bug1405: using default fences (TODO: VERIFY THAT WE NEED THEM) */
    #elif defined(__GNUC__)
      #if GASNET_DEBUG
        #include <stdio.h>
        #include <stdlib.h>
        #define GASNETI_CMPXCHG_BUGCHECK_DECL  int _cmpxchg_bugcheck_count = 128;
        #define GASNETI_CMPXCHG_BUGCHECK(v) do {                                         \
            if (_cmpxchg_bugcheck_count-- <= 0) {                                        \
              void *ip;                                                                  \
              asm ("mov %0=ip" : "=r"(ip));                                              \
              fprintf(stderr,"CMPXCHG_BUGCHECK: stuck at %p on word %p\n", ip, (v));     \
              abort();                                                                   \
            }                                                                            \
          } while (0)
      #else
        #define GASNETI_CMPXCHG_BUGCHECK_DECL
        #define GASNETI_CMPXCHG_BUGCHECK(v)  ((void)0)
      #endif

      GASNETI_INLINE(gasneti_cmpxchg)
      int32_t gasneti_cmpxchg(int32_t volatile *ptr, int32_t oldval, int32_t newval) {                                                                                      \
        int64_t _o_, _r_;
         _o_ = (int64_t)oldval;
         __asm__ __volatile__ ("mov ar.ccv=%0;;" :: "rO"(_o_));
         __asm__ __volatile__ ("mf; cmpxchg4.acq %0=[%1],%2,ar.ccv"
                                : "=r"(_r_) : "r"(ptr), "r"(newval) : "memory");
        return (int32_t) _r_;
      }
      GASNETI_INLINE(gasneti_atomic_addandfetch_32)
      int32_t gasneti_atomic_addandfetch_32(int32_t volatile *v, int32_t op) {
        int32_t oldctr, newctr;
        GASNETI_CMPXCHG_BUGCHECK_DECL

        do {
          GASNETI_CMPXCHG_BUGCHECK(v);
          oldctr = *v;
          newctr = oldctr + op;
        } while (gasneti_cmpxchg(v, oldctr, newctr) != oldctr);
        return newctr;
      }
      typedef struct { volatile int32_t ctr; } gasneti_atomic_t;
      #define _gasneti_atomic_increment(p) (gasneti_atomic_addandfetch_32(&((p)->ctr),1))
      #define _gasneti_atomic_decrement(p) (gasneti_atomic_addandfetch_32(&((p)->ctr),-1))
      #define _gasneti_atomic_read(p)      ((p)->ctr)
      #define _gasneti_atomic_set(p,v)     ((p)->ctr = (v))
      #define _gasneti_atomic_init(v)      { (v) }
      #define _gasneti_atomic_decrement_and_test(p) (gasneti_atomic_addandfetch_32(&((p)->ctr),-1) == 0)
      #define _gasneti_atomic_compare_and_swap(p,oval,nval) \
        (gasneti_cmpxchg((volatile int *)&((p)->ctr),oval,nval) == (oval))
      #define GASNETI_HAVE_ATOMIC_CAS 1
      /* XXX bug1405: our CAS includes the following fences (TODO: CUSTOMIZE OR WEAKEN?) */
      #define GASNETI_ATOMIC_FENCE_RMW (GASNETI_ATOMIC_MB_PRE | GASNETI_ATOMIC_ACQ)
    #elif defined(__HP_cc) || defined(__HP_aCC) /* HP C/C++ Itanium intrinsics */
      #include <machine/sys/inline.h>
      /* legal values for imm are -16, -8, -4, -1, 1, 4, 8, and 16 
         returns *old* value */
      #define gasneti_atomic_addandfetch_32(ptr, imm) \
         _Asm_fetchadd(_FASZ_W, _SEM_ACQ,             \
                       ptr, imm,                      \
                       _LDHINT_NONE, (_Asm_fence)(_UP_MEM_FENCE | _DOWN_MEM_FENCE))
      GASNETI_INLINE(gasneti_cmpxchg)
      int32_t gasneti_cmpxchg(int32_t volatile *ptr, int32_t oldval, int32_t newval) {                                                                                      \
        register int64_t _r_;
        _Asm_mov_to_ar(_AREG_CCV, (int64_t)oldval);
        _r_ = _Asm_cmpxchg(_SZ_W, _SEM_ACQ, 
                           ptr, newval, 
                           _LDHINT_NONE, (_Asm_fence)(_UP_MEM_FENCE | _DOWN_MEM_FENCE));
        return (int32_t) _r_;
      }
      typedef struct { volatile int32_t ctr; } gasneti_atomic_t;
      #define _gasneti_atomic_increment(p) (gasneti_atomic_addandfetch_32(&((p)->ctr),1))
      #define _gasneti_atomic_decrement(p) (gasneti_atomic_addandfetch_32(&((p)->ctr),-1))
      #define _gasneti_atomic_read(p)      ((p)->ctr)
      #define _gasneti_atomic_set(p,v)     ((p)->ctr = (v))
      #define _gasneti_atomic_init(v)      { (v) }
      #define _gasneti_atomic_decrement_and_test(p) (gasneti_atomic_addandfetch_32(&((p)->ctr),-1) == 1)
      #define _gasneti_atomic_compare_and_swap(p,oval,nval) \
        (gasneti_cmpxchg((volatile int *)&((p)->ctr),oval,nval) == (oval))
      #define GASNETI_HAVE_ATOMIC_CAS 1
      /* XXX bug1405: built-in ACQ on RMW (TODO: WEAKEN OR CUSTOMIZE CODE ABOVE?) */
      #define GASNETI_ATOMIC_FENCE_RMW GASNETI_ATOMIC_ACQ
    #else
      #error unrecognized Itanium compiler - need to implement GASNet atomics (or #define GASNETI_USE_GENERIC_ATOMICOPS)
    #endif
  /* ------------------------------------------------------------------------------------ */
  #elif defined(__alpha__) || defined(__alpha) /* DEC Alpha */
    #if defined(__GNUC__)
      GASNETI_INLINE(gasneti_atomic_fetchandadd_32)
      int32_t gasneti_atomic_fetchandadd_32(int32_t volatile *v, int32_t op) {
        register int32_t temp;
        register int32_t result;
        __asm__ __volatile__(
          "1: ldl_l %1, %2\n\t"
          "addl %1, %3, %0\n\t"
          "stl_c %0, %2\n\t"
          "beq %0, 1b"
          : "=&r" (temp), "=&r" (result), "=m" (*v) /* outputs */
          : "IOr" (op)       /* inputs */
          : "memory", "cc");             /* kills */
        return result;
      }
      GASNETI_INLINE(gasneti_atomic_add_32)
      void gasneti_atomic_add_32(int32_t volatile *v, int32_t op) {
        register int32_t temp;
        __asm__ __volatile__(
          "1: ldl_l %0, %1\n\t"
          "addl %0, %2, %0\n\t"
          "stl_c %0, %1\n\t"
          "beq %0, 1b"
          : "=&r" (temp), "=m" (*v) /* outputs */
          : "IOr" (op)       /* inputs */
          : "memory", "cc");             /* kills */
      }
     typedef struct { volatile int32_t ctr; } gasneti_atomic_t;
     #define _gasneti_atomic_increment(p) (gasneti_atomic_add_32(&((p)->ctr),1))
     #define _gasneti_atomic_decrement(p) (gasneti_atomic_add_32(&((p)->ctr),-1))
     #define _gasneti_atomic_read(p)      ((p)->ctr)
     #define _gasneti_atomic_set(p,v)     ((p)->ctr = (v))
     #define _gasneti_atomic_init(v)      { (v) }
     #define _gasneti_atomic_decrement_and_test(p) (gasneti_atomic_fetchandadd_32(&((p)->ctr),-1) == 1)
     GASNETI_INLINE(_gasneti_atomic_compare_and_swap)
     int _gasneti_atomic_compare_and_swap(gasneti_atomic_t *p, uint32_t oldval, uint32_t newval) {
       unsigned long ret;
       __asm__ __volatile__ (
		"1:	ldl_l	%0,%1\n"	/* Load-linked of current value */
		"	cmpeq	%0,%2,%0\n"	/* compare to oldval */
		"	beq	%0,2f\n"	/* done/fail on mismatch (success/fail in ret) */
		"	mov	%3,%0\n"	/* copy newval to ret */
		"	stl_c	%0,%1\n"	/* Store-conditional of newval (success/fail in ret) */
		"	beq	%0,1b\n"	/* Retry on stl_c failure */
		"2:	"
       		: "=&r"(ret), "=m"(*p)
		: "r"(oldval), "r"(newval)
		: "memory", "cc");

       return ret;
     }
     #define GASNETI_HAVE_ATOMIC_CAS 1
     /* bug1405: no fences in our asm, so using default fences */
    #elif (defined(__DECC) || defined(__DECCXX)) && defined(__osf__)
       /* Compaq C / OSF atomics are compiler built-ins */
       #include <sys/machine/builtins.h>
       typedef struct { volatile int32_t ctr; } gasneti_atomic_t;
       #define _gasneti_atomic_increment(p) (__ATOMIC_INCREMENT_LONG(&((p)->ctr)))
       #define _gasneti_atomic_decrement(p) (__ATOMIC_DECREMENT_LONG(&((p)->ctr)))
       #define _gasneti_atomic_read(p)      ((p)->ctr)
       #define _gasneti_atomic_set(p,v)     ((p)->ctr = (v))
       #define _gasneti_atomic_init(v)      { (v) }
       #define _gasneti_atomic_decrement_and_test(p) \
                                          (__ATOMIC_DECREMENT_LONG(&((p)->ctr)) == 1)
       /* The __CMP_STORE_LONG built-in is insufficient alone because it returns
	  a failure indication if the LL/SC is interrupted by another write to the
          same cache line (it does not retry).
       */
       GASNETI_INLINE(_gasneti_atomic_compare_and_swap)
       int _gasneti_atomic_compare_and_swap(gasneti_atomic_t *p, uint32_t oldval, uint32_t newval) {
         return asm("1:	ldl_l	%v0,(%a0);"	/* Load-linked of current value to %v0 */
		    "	cmpeq	%v0,%a1,%v0;"	/* compare %v0 to oldval w/ result to %v0 */
		    "	beq	%v0,2f;"	/* done/fail on mismatch (success/fail in %v0) */
		    "	mov	%a2,%v0;"	/* copy newval to %v0 */
		    "	stl_c	%v0,(%a0);"	/* Store-conditional of newval (success/fail in %v0) */
		    "	beq	%v0,1b;"	/* Retry on stl_c failure */
		    "2:	", p, oldval, newval);  /* Returns value from %v0 */
       }
       #define GASNETI_HAVE_ATOMIC_CAS 1
       /* XXX bug1405: using default fences (TODO: VERIFY THAT WE NEED THEM) */
    #else
      #error unrecognized Alpha compiler - need to implement GASNet atomics (or #define GASNETI_USE_GENERIC_ATOMICOPS)
    #endif
  /* ------------------------------------------------------------------------------------ */
  #elif defined(__sparc) || defined(__sparc__)
    #if defined(__sparcv9) || defined(__sparcv9cpu) || defined(GASNETI_ARCH_SPARCV9) /* SPARC v9 */
      #if defined(__GNUC__)
        static __inline__ int32_t gasneti_atomic_addandfetch_32(int32_t volatile *v, int32_t op) {
          /* SPARC v9 architecture manual, p.333 
           * This function requires the cas instruction in Sparc V9, and therefore gcc -mcpu=ultrasparc
	   * The manual says (sec A.9) no memory fences in CAS (in conflict w/ JMM web page).
           */
          register int32_t volatile * addr = (int32_t volatile *)v;
          register int32_t oldval;
          register int32_t newval;
          __asm__ __volatile__ ( 
            "0:\t" 
            "membar #StoreLoad | #LoadLoad    \n\t" /* complete all previous ops before next load */
            "ld       [%2],%0 \n\t"    /* oldval = *addr; */
            "add      %0,%3,%1 \n\t"   /* newval = oldval + op; */
            "cas      [%2],%0,%1 \n\t" /* if (*addr == oldval) { *addr = newval; }  newval = *addr; */
            "cmp      %0, %1 \n\t"     /* check if newval == oldval (swap succeeded) */
            "bne,pn   %%icc, 0b \n\t"         /* otherwise, try again (,pn == predict not taken) */
            "membar #StoreLoad | #StoreStore    \n\t" /* complete previous cas store before all subsequent ops */
            : "=&r"(oldval), "=&r"(newval)
            : "r" (addr), "rn"(op) 
            : "memory");
          return oldval;
        }
        typedef struct { volatile int32_t ctr; } gasneti_atomic_t;
        #define _gasneti_atomic_increment(p) (gasneti_atomic_addandfetch_32(&((p)->ctr),1))
        #define _gasneti_atomic_decrement(p) (gasneti_atomic_addandfetch_32(&((p)->ctr),-1))
        #define _gasneti_atomic_read(p)      ((p)->ctr)
        #define _gasneti_atomic_set(p,v)     ((p)->ctr = (v))
        #define _gasneti_atomic_init(v)      { (v) }
        #define _gasneti_atomic_decrement_and_test(p) (gasneti_atomic_addandfetch_32(&((p)->ctr),-1) == 1)

        GASNETI_INLINE(_gasneti_atomic_compare_and_swap)
        int _gasneti_atomic_compare_and_swap(gasneti_atomic_t *v, uint32_t oldval, uint32_t newval) {
          register volatile uint32_t * addr = (volatile uint32_t *)&(v->ctr);
          __asm__ __volatile__ ( 
              "membar #StoreLoad | #LoadLoad   \n\t" /* complete all previous ops before next load */
              "cas      [%2],%1,%0 \n\t"             /* if (*addr == oldval) { *addr = newval; }  newval = *addr; */
              "membar #StoreLoad | #StoreStore \n\t" /* complete previous cas store before all subsequent ops */
              : "+r"(newval)
              : "r"(oldval), "r" (addr)
              : "memory");
          return (int)(newval == oldval);
        }
        #define GASNETI_HAVE_ATOMIC_CAS 1
        /* XXX bug1405: our asm includes the following: (TODO: CUSTOMIZE?) */
	#define GASNETI_ATOMIC_FENCE_RMW (GASNETI_ATOMIC_RMB_PRE | GASNETI_ATOMIC_WMB_POST)
      #else
        #error unrecognized Sparc v9 compiler - need to implement GASNet atomics (or #define GASNETI_USE_GENERIC_ATOMICOPS)
      #endif
    #else /* SPARC pre-v9 lacks RMW instructions - 
             all we get is atomic swap, but that's actually just barely enough  */
      #define GASNETI_ATOMICOPS_NOT_SIGNALSAFE 1 /* not signal-safe because of "checkout" semantics */
      #if defined(__GNUC__)
        GASNETI_INLINE(gasneti_loadandclear_32)
        uint32_t gasneti_loadandclear_32(int32_t volatile *v) {
          register int32_t volatile * addr = (int32_t volatile *)v;
          register int32_t val = 0;
          __asm__ __volatile__ ( 
            "swap [%1], %0 \n"   
            : "+r"(val)
            : "r" (addr)
            : "memory");
          return val;
        }
        #define GASNETI_ATOMIC_PRESENT    ((int32_t)0x80000000)
        #define GASNETI_ATOMIC_INIT_MAGIC ((uint64_t)0x8BDEF66BAD1E3F3AULL)
        typedef struct { volatile uint64_t initflag; volatile int32_t ctr; } gasneti_atomic_t;
        #define _gasneti_atomic_init(v)      { GASNETI_ATOMIC_INIT_MAGIC, (GASNETI_ATOMIC_PRESENT|(v)) }
        /* would like to use gasneti_waituntil here, but it requires libgasnet for waitmode */
        #define gasneti_atomic_spinuntil(cond) do {       \
                while (!(cond)) gasneti_compiler_fence(); \
                gasneti_local_rmb();                      \
                } while (0)
        GASNETI_INLINE(gasneti_atomic_addandfetch_32)
        int32_t gasneti_atomic_addandfetch_32(gasneti_atomic_t *p, int32_t op) {
          int32_t tmp;
          gasneti_assert(p->initflag == GASNETI_ATOMIC_INIT_MAGIC);
          gasneti_local_wmb();
          gasneti_atomic_spinuntil(p->ctr && (tmp = gasneti_loadandclear_32(&(p->ctr))));
          gasneti_assert(tmp & GASNETI_ATOMIC_PRESENT);
          p->ctr = (GASNETI_ATOMIC_PRESENT | (tmp + op));
          return (tmp & ~GASNETI_ATOMIC_PRESENT);
        }
        #if 0
          /* this version fails if set is used in a race with addandfetch */
          GASNETI_INLINE(_gasneti_atomic_set)
          void _gasneti_atomic_set(gasneti_atomic_t *p, int32_t val) {
            gasneti_local_wmb();
            p->ctr = (GASNETI_ATOMIC_PRESENT | val);
          }
        #else
          GASNETI_INLINE(_gasneti_atomic_set)
          void _gasneti_atomic_set(gasneti_atomic_t *p, int32_t val) {
            int32_t tmp;
            gasneti_local_wmb();
            if_pf (p->initflag != GASNETI_ATOMIC_INIT_MAGIC) {
              p->ctr = (GASNETI_ATOMIC_PRESENT | val);
              gasneti_local_wmb();
              p->initflag = GASNETI_ATOMIC_INIT_MAGIC;
            } else {
              gasneti_atomic_spinuntil(p->ctr && (tmp = gasneti_loadandclear_32(&(p->ctr))));
              gasneti_assert(tmp & GASNETI_ATOMIC_PRESENT);
              p->ctr = (GASNETI_ATOMIC_PRESENT | val);
            }
          }
        #endif
        GASNETI_INLINE(_gasneti_atomic_read)
        int32_t _gasneti_atomic_read(gasneti_atomic_t *p) {
          int32_t tmp;
          gasneti_assert(p->initflag == GASNETI_ATOMIC_INIT_MAGIC);
          gasneti_atomic_spinuntil((tmp = p->ctr));
          gasneti_assert(tmp & GASNETI_ATOMIC_PRESENT);
          return (tmp & ~GASNETI_ATOMIC_PRESENT);
        }
        #define _gasneti_atomic_increment(p) (gasneti_atomic_addandfetch_32(p,1))
        #define _gasneti_atomic_decrement(p) (gasneti_atomic_addandfetch_32(p,-1))
        #define _gasneti_atomic_decrement_and_test(p) (gasneti_atomic_addandfetch_32(p,-1) == 1)

        GASNETI_INLINE(_gasneti_atomic_compare_and_swap)
        int32_t _gasneti_atomic_compare_and_swap(gasneti_atomic_t *p, uint32_t oldval, uint32_t newval) {
          int32_t tmp;
          int retval;
          gasneti_assert(p->initflag == GASNETI_ATOMIC_INIT_MAGIC);
          gasneti_local_wmb();
          gasneti_atomic_spinuntil(p->ctr && (tmp = gasneti_loadandclear_32(&(p->ctr))));
          gasneti_assert(tmp & GASNETI_ATOMIC_PRESENT);
          retval = ((tmp & ~GASNETI_ATOMIC_PRESENT) == oldval);
          if_pt (retval) {
            tmp = (GASNETI_ATOMIC_PRESENT | newval);
          }
          p->ctr = tmp;
          return retval;
        }
        #define GASNETI_HAVE_ATOMIC_CAS 1
        /* bug1405: Our asm has the following fences */
	#define GASNETI_ATOMIC_FENCE_READ	GASNETI_ATOMIC_RMB_PRE
	#define GASNETI_ATOMIC_FENCE_SET	GASNETI_ATOMIC_WMB_PRE
	#define GASNETI_ATOMIC_FENCE_RMW	GASNETI_ATOMIC_MB_PRE
        /* XXX bug1405: TODO: Our set also has RMB_PRE unless uninitialized */
      #else
        #error unrecognized Sparc pre-v9 compiler - need to implement GASNet atomics (or #define GASNETI_USE_GENERIC_ATOMICOPS)
      #endif
    #endif
  /* ------------------------------------------------------------------------------------ */
  #elif defined(__hppa) || defined(__hppa__)
    /* all we get is atomic load-and-clear, but that's actually just barely enough  */
    #define GASNETI_ATOMICOPS_NOT_SIGNALSAFE 1 /* not signal-safe because of "checkout" semantics */
    #if defined(__HP_aCC) /* HP C++ compiler */
      extern "C" uint32_t gasneti_slow_loadandclear_32(int32_t volatile *v);
      #define gasneti_loadandclear_32 gasneti_slow_loadandclear_32
      #define GASNETI_USING_SLOW_ATOMICS 1
    #else
      GASNETI_INLINE(gasneti_loadandclear_32)
      uint32_t gasneti_loadandclear_32(int32_t volatile *v) {
        register int32_t volatile * addr = (int32_t volatile *)v;
        register int32_t val = 0;
        gasneti_assert(!(((uintptr_t)addr) & 0xF)); /* ldcws requires 16-byte alignment */
        *(volatile char *)(v+1) = 0; /* fetch this cache line as a dirty word - speeds up ldcw */
        #if defined(__GNUC__)
          __asm__ __volatile__ ( 
          #if 0
            "ldcws 0(%1), %0 \n"  
            /* should be using "ldcws,co" here for better performance, 
               but GNU assembler rejects it (works with system assembler) 
             */
          #else
            "ldcw,co 0(%1), %0 \n"  
            /* this alternate, undocumented pseudo-op instruction appears to do the right thing */
          #endif
            : "=r"(val)
            : "r" (addr)
            : "memory");
        #elif defined(__HP_cc) /* HP C compiler */
          _asm("LDCWS,CO",0,0,addr,val);
        #else
          #error unrecognized PA-RISC compiler - need to implement GASNet atomics (or #define GASNETI_USE_GENERIC_ATOMICOPS)
        #endif
        return val;
      }
    #endif
      #define GASNETI_ATOMIC_CTR(p)     ((volatile int32_t *)GASNETI_ALIGNUP(&(p->_ctr),16))
      #define GASNETI_ATOMIC_PRESENT    ((int32_t)0x80000000)
      #define GASNETI_ATOMIC_INIT_MAGIC ((uint64_t)0x8BDEF66BAD1E3F3AULL)
      typedef struct { volatile uint64_t initflag; volatile int32_t _ctr[4]; char _pad; } gasneti_atomic_t;
      #define _gasneti_atomic_init(v)      {    \
              GASNETI_ATOMIC_INIT_MAGIC,       \
              { (GASNETI_ATOMIC_PRESENT|(v)),  \
                (GASNETI_ATOMIC_PRESENT|(v)),  \
                (GASNETI_ATOMIC_PRESENT|(v)),  \
                (GASNETI_ATOMIC_PRESENT|(v)) } \
              }
      /* would like to use gasneti_waituntil here, but it requires libgasnet for waitmode */
      #define gasneti_atomic_spinuntil(cond) do {       \
              while (!(cond)) gasneti_compiler_fence(); \
              gasneti_local_rmb();                      \
              } while (0)
      GASNETI_INLINE(gasneti_atomic_addandfetch_32)
      int32_t gasneti_atomic_addandfetch_32(gasneti_atomic_t *p, int32_t op) {
        int32_t tmp;
        volatile int32_t * const pctr = GASNETI_ATOMIC_CTR(p);
        gasneti_assert(p->initflag == GASNETI_ATOMIC_INIT_MAGIC);
        gasneti_local_wmb();
        gasneti_atomic_spinuntil(*pctr && (tmp = gasneti_loadandclear_32(pctr)));
        gasneti_assert(tmp & GASNETI_ATOMIC_PRESENT);
        *pctr = (GASNETI_ATOMIC_PRESENT | (tmp + op));
        return (tmp & ~GASNETI_ATOMIC_PRESENT);
      }
      #if 0
        /* this version fails if set is used in a race with addandfetch */
        GASNETI_INLINE(_gasneti_atomic_set)
        void _gasneti_atomic_set(gasneti_atomic_t *p, int32_t val) {
          volatile int32_t * const pctr = GASNETI_ATOMIC_CTR(p);
          gasneti_local_wmb();
          *pctr = (GASNETI_ATOMIC_PRESENT | val);
        }
      #else
        GASNETI_INLINE(_gasneti_atomic_set)
        void _gasneti_atomic_set(gasneti_atomic_t *p, int32_t val) {
          int32_t tmp;
          volatile int32_t * const pctr = GASNETI_ATOMIC_CTR(p);
          gasneti_local_wmb();
          if_pf (p->initflag != GASNETI_ATOMIC_INIT_MAGIC) {
            *pctr = (GASNETI_ATOMIC_PRESENT | val);
            gasneti_local_wmb();
            p->initflag = GASNETI_ATOMIC_INIT_MAGIC;
          } else {
            gasneti_atomic_spinuntil(*pctr && (tmp = gasneti_loadandclear_32(pctr)));
            gasneti_assert(tmp & GASNETI_ATOMIC_PRESENT);
            *pctr = (GASNETI_ATOMIC_PRESENT | val);
          }
        }
      #endif
      GASNETI_INLINE(_gasneti_atomic_read)
      int32_t _gasneti_atomic_read(gasneti_atomic_t *p) {
        int32_t tmp;
        volatile int32_t * const pctr = GASNETI_ATOMIC_CTR(p);
        gasneti_assert(p->initflag == GASNETI_ATOMIC_INIT_MAGIC);
        gasneti_atomic_spinuntil((tmp = *pctr));
        gasneti_assert(tmp & GASNETI_ATOMIC_PRESENT);
        return (tmp & ~GASNETI_ATOMIC_PRESENT);
      }
      #define _gasneti_atomic_increment(p) (gasneti_atomic_addandfetch_32(p,1))
      #define _gasneti_atomic_decrement(p) (gasneti_atomic_addandfetch_32(p,-1))
      #define _gasneti_atomic_decrement_and_test(p) (gasneti_atomic_addandfetch_32(p,-1) == 1)

      GASNETI_INLINE(_gasneti_atomic_compare_and_swap)
      int32_t _gasneti_atomic_compare_and_swap(gasneti_atomic_t *p, uint32_t oldval, uint32_t newval) {
        volatile int32_t * const pctr = GASNETI_ATOMIC_CTR(p);
        int32_t tmp;
        int retval;
        gasneti_assert(p->initflag == GASNETI_ATOMIC_INIT_MAGIC);
        gasneti_local_wmb();
        gasneti_atomic_spinuntil(*pctr && (tmp = gasneti_loadandclear_32(pctr)));
        gasneti_assert(tmp & GASNETI_ATOMIC_PRESENT);
        retval = ((tmp & ~GASNETI_ATOMIC_PRESENT) == oldval);
        if_pt (retval) {
          tmp = (GASNETI_ATOMIC_PRESENT | newval);
        }
        *pctr = tmp;
        return retval;
      }
      #define GASNETI_HAVE_ATOMIC_CAS 1
      /* bug1405: Our asm has the following fences */
      #define GASNETI_ATOMIC_FENCE_READ	GASNETI_ATOMIC_RMB_PRE
      #define GASNETI_ATOMIC_FENCE_SET	GASNETI_ATOMIC_WMB_PRE
      #define GASNETI_ATOMIC_FENCE_RMW	GASNETI_ATOMIC_MB_PRE
      /* XXX bug1405: TODO: Our set also has RMB_PRE unless uninitialized */
  /* ------------------------------------------------------------------------------------ */
  #elif defined(__crayx1) /* This works on X1, but NOT the T3E */
    #include <intrinsics.h>
    typedef volatile long gasneti_atomic_t;
    /* man pages for atomic ops claim gsync is required for using atomic ops,
       but it's unclear when exactly it is required for our purposes - 
       experimentally determined using testtools that it's only required after the amo.
       Note gsync call MUST be _gsync(0x1) - the manpage docs are gratuitiously wrong, 
       everything else gives a bus error
     */
    #define gasneti_atomic_presync()  ((void)0)
    #define gasneti_atomic_postsync()  _gsync(0x1)

    #define _gasneti_atomic_increment(p)	\
      (gasneti_atomic_presync(),_amo_aadd((p),(long)1),gasneti_atomic_postsync())
    #define _gasneti_atomic_decrement(p)	\
      (gasneti_atomic_presync(),_amo_aadd((p),(long)-1),gasneti_atomic_postsync())
    #define _gasneti_atomic_read(p)      (*(p))
    #define _gasneti_atomic_set(p,v)     (*(p) = (v))
    #define _gasneti_atomic_init(v)      (v)
    GASNETI_INLINE(_gasneti_atomic_decrement_and_test)
    int _gasneti_atomic_decrement_and_test(gasneti_atomic_t *p) {
       int retval;
       gasneti_atomic_presync();
       retval = (_amo_afadd((p),(long)-1) == 1);
       gasneti_atomic_postsync();
       return retval;
    }
    GASNETI_INLINE(_gasneti_atomic_compare_and_swap)
    int _gasneti_atomic_compare_and_swap(gasneti_atomic_t *p, long oldval, long newval) {
      long result;
      gasneti_atomic_presync();
      result = _amo_acswap(p, oldval, newval);
      gasneti_atomic_postsync();
      return (result == oldval); 
    }
    #define GASNETI_HAVE_ATOMIC_CAS 1
    /* XXX bug1405: using default fences (TODO: VERIFY THAT WE NEED THEM) */
  /* ------------------------------------------------------------------------------------ */
  #elif defined(_SX) /* NEC SX-6 */
    /* these are disabled for now because they don't link */
    typedef struct { volatile uint32_t ctr; } gasneti_atomic_t;
   #if 0
    #include <sys/mplock.h>
    #define _gasneti_atomic_increment(p) (atomic_add4(((p)->ctr),1))
    #define _gasneti_atomic_decrement(p) (atomic_add4(((p)->ctr),-1))
    #define _gasneti_atomic_read(p)      (atomic_read4((p)->ctr))
    #define _gasneti_atomic_set(p,v)     (atomic_set4((p)->ctr,(v)))
    #define _gasneti_atomic_init(v)      { (v) }
    #define _gasneti_atomic_decrement_and_test(p) \
                                        (atomic_add4(((p)->ctr),-1) == 0)
   #else
    #define _gasneti_atomic_increment(p) (muadd(&((p)->ctr),1))
    #define _gasneti_atomic_decrement(p) (muadd(&((p)->ctr),-1))
    #define _gasneti_atomic_read(p)      (muget(&((p)->ctr)))
    #define _gasneti_atomic_set(p,v)     (muset(&((p)->ctr),(v)))
    #define _gasneti_atomic_init(v)      { (v) }
    #define _gasneti_atomic_decrement_and_test(p) \
                                        (muadd(&((p)->ctr),-1) == 0)
   #endif
    /* XXX bug1405: using default fences (TODO: VERIFY THAT WE NEED THEM) */
  /* ------------------------------------------------------------------------------------ */
  #elif (defined(__APPLE__) && defined(__MACH__) && (defined(__ppc__) || defined(__ppc64__))) /* PowerPC OSX */ || \
        (defined(__linux__) && defined(__PPC__)) /* PowerPC Linux */ || \
        (defined(__blrts__) && defined(__PPC__)) /* BlueGene/L */
    #if defined(__xlC__)
      /* XLC machine code functions are very rigid, thus we produce all
       * three read-modify-write ops as distinct functions in order to
       * get anything near to optimal code.
       */
      static void gasneti_atomic_inc_32(int32_t volatile *v);
      #pragma mc_func gasneti_atomic_inc_32 {\
	/* ARGS: r3 = v  LOCAL: r4 = tmp */ \
	"7c801828"	/* 0: lwarx	r4,0,r3		*/ \
	"38840001"	/*    addi	r4,r4,0x1	*/ \
	"7c80192d"	/*    stwcx.	r4,0,r3		*/ \
	"40a2fff4"	/*    bne-	0b		*/ \
      }
      #pragma reg_killed_by gasneti_atomic_inc_32 cr0, gr4

      static void gasneti_atomic_dec_32(int32_t volatile *v);
      #pragma mc_func gasneti_atomic_dec_32 {\
	/* ARGS: r3 = v  LOCAL: r4 = tmp */ \
	"7c801828"	/* 0: lwarx	r4,0,r3		*/ \
	"3884ffff"	/*    subi	r4,r4,0x1	*/ \
	"7c80192d"	/*    stwcx.	r4,0,r3		*/ \
	"40a2fff4"	/*    bne-	0b		*/ \
      }
      #pragma reg_killed_by gasneti_atomic_dec_32 cr0, gr4

      static int32_t gasneti_atomic_decandfetch_32(int32_t volatile *v);
      #pragma mc_func gasneti_atomic_decandfetch_32 {\
	/* ARGS: r3 = v  LOCAL: r4 = tmp */ \
	"7c801828"	/* 0: lwarx	r4,0,r3		*/ \
	"3884ffff"	/*    subi	r4,r4,0x1	*/ \
	"7c80192d"	/*    stwcx.	r4,0,r3		*/ \
	"40a2fff4"	/*    bne-	0b		*/ \
	"7c832378"	/*    mr	r3,r4		*/ \
	/* RETURN in r3 = result after dec */ \
      }
      #pragma reg_killed_by gasneti_atomic_decandfetch_32 cr0, gr4

      typedef struct { volatile int32_t ctr; } gasneti_atomic_t;
      #define _gasneti_atomic_increment(p) (gasneti_atomic_inc_32(&((p)->ctr)))
      #define _gasneti_atomic_decrement(p) (gasneti_atomic_dec_32(&((p)->ctr)))
      #define _gasneti_atomic_read(p)      ((p)->ctr)
      #define _gasneti_atomic_set(p,v)     ((p)->ctr = (v))
      #define _gasneti_atomic_init(v)      { (v) }
      #define _gasneti_atomic_decrement_and_test(p) (gasneti_atomic_decandfetch_32(&((p)->ctr)) == 0)

      static int32_t gasneti_atomic_swap_not_32(volatile int32_t *v, int32_t oldval, int32_t newval);
      #pragma mc_func gasneti_atomic_swap_not_32 {\
	/* ARGS: r3 = p, r4=oldval, r5=newval   LOCAL: r0 = tmp */ \
	"7c001828"	/* 0: lwarx	r0,0,r3		*/ \
	"7c002279"	/*    xor.	r0,r0,r4	*/ \
	"40820010"	/*    bne	1f		*/ \
	"7ca0192d"	/*    stwcx.	r5,0,r3		*/ \
	"40a2fff0"	/*    bne-	0b		*/ \
	"7c030378"	/* 1: mr	r3,r0		*/ \
	/* RETURN in r3 = 0 iff swap took place */ \
      }
      #pragma reg_killed_by gasneti_atomic_swap_not_32 cr0, gr0
      #define _gasneti_atomic_compare_and_swap(p, oldval, newval) \
	(gasneti_atomic_swap_not_32(&((p)->ctr),(oldval),(newval)) == 0)
      #define GASNETI_HAVE_ATOMIC_CAS 1
      /* bug1405: using default fences as we have none in our asms */
    #elif defined(__GNUC__)
      static __inline__ int32_t gasneti_atomic_addandfetch_32(int32_t volatile *v, int32_t op) {
        register int32_t volatile * addr = (int32_t volatile *)v;
        register int32_t result;
        __asm__ __volatile__ ( 
          "0:\t" 
          "lwarx    %0,0,%1 \n\t" 
          "add%I2   %0,%0,%2 \n\t"
          "stwcx.   %0,0,%1 \n\t"
          "bne-     0b \n\t" 
          : "=&b"(result)		/* constraint b = "b"ase register (not r0) */
          : "r" (addr), "Ir"(op) 
          : "cr0", "memory");
        return result;
      }
      typedef struct { volatile int32_t ctr; } gasneti_atomic_t;
      #define _gasneti_atomic_increment(p) (gasneti_atomic_addandfetch_32(&((p)->ctr),1))
      #define _gasneti_atomic_decrement(p) (gasneti_atomic_addandfetch_32(&((p)->ctr),-1))
      #define _gasneti_atomic_read(p)      ((p)->ctr)
      #define _gasneti_atomic_set(p,v)     ((p)->ctr = (v))
      #define _gasneti_atomic_init(v)      { (v) }
      #define _gasneti_atomic_decrement_and_test(p) (gasneti_atomic_addandfetch_32(&((p)->ctr),-1) == 0)

      GASNETI_INLINE(_gasneti_atomic_compare_and_swap)
      int _gasneti_atomic_compare_and_swap(gasneti_atomic_t *p, uint32_t oldval, uint32_t newval) {
        register uint32_t result;
        __asm__ __volatile__ (
	  "0:\t"
	  "lwarx    %0,0,%1 \n\t"         /* load to result */
	  "xor.     %0,%0,%2 \n\t"        /* xor result w/ oldval */
	  "bne      1f \n\t"              /* branch on mismatch */
	  "stwcx.   %3,0,%1 \n\t"         /* store newval */
	  "bne-     0b \n\t"              /* retry on conflict */
	  "1:	"
	  : "=&r"(result)
	  : "r" (p), "r"(oldval), "r"(newval)
	  : "cr0", "memory");
  
        return (result == 0);
      } 
      #define GASNETI_HAVE_ATOMIC_CAS 1
      /* bug1405: using default fences as we have none in our asms */
    #else
      #error Unrecognized PowerPC - need to implement GASNet atomics (or #define GASNETI_USE_GENERIC_ATOMICOPS)
    #endif
  /* ------------------------------------------------------------------------------------
   * Linux provides an asm/atomic.h that is sometimes just useless
   * and other times supplies all but compare-and-swap (even when
   * it is implemented).  Therefore we use it only as the last resort.
   * ------------------------------------------------------------------------------------ */
  #elif defined(__linux__) && !defined(BROKEN_LINUX_ASM_ATOMIC_H) && \
      (defined(CONFIG_SMP) || defined(GASNETI_UNI_BUILD))
      /* some versions of the linux kernel ship with a broken atomic.h
         Disable using this code if this is a gasnet-smp build and the 
         linux/config.h settings disagree (due to system config problem or 
         cross-compiling on a uniprocessor frontend for smp nodes)
       */
      #ifdef __alpha__
        /* work-around for a puzzling header bug in alpha Linux */
        #define extern static
      #endif
      #ifdef __cplusplus
        /* work around a really stupid C++ header bug observed in HP Linux */
        #define new new_
      #endif
      #include <asm/bitops.h>
      #include <asm/system.h>
      #include <asm/atomic.h>
      #ifdef __alpha__
        #undef extern
      #endif
      #ifdef __cplusplus
        #undef new
      #endif
      typedef atomic_t gasneti_atomic_t;
      #define _gasneti_atomic_increment(p) atomic_inc(p)
      #define _gasneti_atomic_decrement(p) atomic_dec(p)
      #define _gasneti_atomic_read(p)      atomic_read(p)
      #define _gasneti_atomic_set(p,v)     atomic_set(p,v)
      #define _gasneti_atomic_init(v)      ATOMIC_INIT(v)
      #define _gasneti_atomic_decrement_and_test(p) \
                                          atomic_dec_and_test(p)
      #ifdef cmpxchg
        /* we must violate the Linux atomic_t abstraction below and pass
           cmpxchg a pointer to the struct field, otherwise cmpxchg will
           stupidly attempt to cast its result to a struct type and fail
         */
        #define _gasneti_atomic_compare_and_swap(p,oval,nval) \
             (cmpxchg(&((p)->counter),oval,nval) == (oval))
        #define GASNETI_HAVE_ATOMIC_CAS 1
      #endif
      /* bug1405: using default fences as we can't hope to know what to expect on new platforms */
  #else
    #error Unrecognized platform - need to implement GASNet atomics (or #define GASNETI_USE_GENERIC_ATOMICOPS)
  #endif
#endif

#ifdef GASNETI_USE_GENERIC_ATOMICOPS
  #define GASNETI_ATOMIC_CONFIG   atomics_os
#else
  #define GASNETI_ATOMIC_CONFIG   atomics_native
#endif

/* ------------------------------------------------------------------------------------ */
/* Uniform memory fences for GASNet atomics.
 */

/* Fences included in a given implementation, default to none */
#ifndef GASNETI_ATOMIC_FENCE_SET
  #define GASNETI_ATOMIC_FENCE_SET	GASNETI_ATOMIC_NONE
#endif
#ifndef GASNETI_ATOMIC_FENCE_READ
  #define GASNETI_ATOMIC_FENCE_READ	GASNETI_ATOMIC_NONE
#endif
#ifndef GASNETI_ATOMIC_FENCE_RMW
  #define GASNETI_ATOMIC_FENCE_RMW	GASNETI_ATOMIC_NONE
#endif

/* Default implementations of fences in atomics. */

/* _mb_{pre,post}(): check for call to mb() */
#if GASNETI_MB_IS_SUM && !(GASNETI_RMB_IS_EMPTY || GASNETI_WMB_IS_EMPTY)
  #define _gasneti_atomic_mb_pre(__f) /* nothing */
  #define _gasneti_atomic_mb_post(__f) /* nothing */
#elif GASNETI_RMB_IS_MB && GASNETI_WMB_IS_MB
  #define _gasneti_atomic_mb_pre(__f) \
    if (__f & GASNETI_ATOMIC_MB_PRE) gasneti_local_mb();
  #define _gasneti_atomic_mb_post(__f) \
    if (__f & GASNETI_ATOMIC_MB_POST) gasneti_local_mb();
#elif  GASNETI_RMB_IS_MB
  #define _gasneti_atomic_mb_pre(__f) \
    if (__f & GASNETI_ATOMIC_RMB_PRE) gasneti_local_mb();
  #define _gasneti_atomic_mb_post(__f) \
    if (__f & GASNETI_ATOMIC_RMB_POST) gasneti_local_mb();
#elif  GASNETI_WMB_IS_MB
  #define _gasneti_atomic_mb_pre(__f) \
    if (__f & GASNETI_ATOMIC_WMB_PRE) gasneti_local_mb();
  #define _gasneti_atomic_mb_post(__f) \
    if (__f & GASNETI_ATOMIC_WMB_POST) gasneti_local_mb();
#else
  #define _gasneti_atomic_mb_pre(__f) \
    if ((__f & GASNETI_ATOMIC_MB_PRE) == GASNETI_ATOMIC_MB_PRE) gasneti_local_mb();
  #define _gasneti_atomic_mb_post(__f) \
    if ((__f & GASNETI_ATOMIC_MB_POST) == GASNETI_ATOMIC_MB_POST) gasneti_local_mb();
#endif

/* _rmb_{pre,post}(): check for call to rmb() */
#if GASNETI_MB_IS_SUM && !(GASNETI_RMB_IS_EMPTY || GASNETI_WMB_IS_EMPTY)
  #define _gasneti_atomic_rmb_pre(__f) \
    if (__f & GASNETI_ATOMIC_RMB_PRE) gasneti_local_rmb();
  #define _gasneti_atomic_rmb_post(__f) \
    if (__f & GASNETI_ATOMIC_RMB_POST) gasneti_local_rmb();
#elif GASNETI_RMB_IS_MB || GASNETI_RMB_IS_EMPTY
  #define _gasneti_atomic_rmb_pre(__f) /* nothing */
  #define _gasneti_atomic_rmb_post(__f) /* nothing */
#else
  #define _gasneti_atomic_rmb_pre(__f) \
    else if (__f & GASNETI_ATOMIC_RMB_PRE) gasneti_local_rmb();
  #define _gasneti_atomic_rmb_post(__f) \
    else if (__f & GASNETI_ATOMIC_RMB_POST) gasneti_local_rmb();
#endif

/* _wmb_{pre,post}(): check for call to wmb() */
#if GASNETI_MB_IS_SUM && !(GASNETI_RMB_IS_EMPTY || GASNETI_WMB_IS_EMPTY)
  #define _gasneti_atomic_wmb_pre(__f) \
    if (__f & GASNETI_ATOMIC_WMB_PRE) gasneti_local_wmb();
  #define _gasneti_atomic_wmb_post(__f) \
    if (__f & GASNETI_ATOMIC_WMB_POST) gasneti_local_wmb();
#elif GASNETI_WMB_IS_MB || GASNETI_WMB_IS_EMPTY
  #define _gasneti_atomic_wmb_pre(__f) /* nothing */
  #define _gasneti_atomic_wmb_post(__f) /* nothing */
#else
  #define _gasneti_atomic_wmb_pre(__f) \
    else if (__f & GASNETI_ATOMIC_WMB_PRE) gasneti_local_wmb();
  #define _gasneti_atomic_wmb_post(__f) \
    else if (__f & GASNETI_ATOMIC_WMB_POST) gasneti_local_wmb();
#endif

/* _rmb_bool(): handles flags containing RMB_POST_IF_* */
#if GASNETI_RMB_IS_EMPTY || (GASNETI_ATOMIC_FENCE_RMW & GASNETI_ATOMIC_FENCE_RMB_POST)
  #define _gasneti_atomic_rmb_bool(__f, __v) /* nothing */
#else
  #define _gasneti_atomic_rmb_bool(__f, __v)  \
    if ((__f & GASNETI_ATOMIC_RMB_POST_IF_TRUE) && __v) gasneti_local_rmb(); \
    if ((__f & GASNETI_ATOMIC_RMB_POST_IF_FALSE) && !__v) gasneti_local_rmb();

  /* Several optimizations possible when a conditional rmb() is combined
   * with an unconditional POST fence.  Such optimizations would prevent
   * imposing a "double" mb() in shuch cases.  However: 
   * 1) There are no current callers that mix *MB_POST with a
   *    conditional RMB_POST_IF*, and no likely reason to.
   * 2) Though they all reduce a great deal at compile-time,
   *    such "optimizations" look very large to the inliner
   *    before any dead code can be eliminated.
   * Therefore, they are not currently implemented.
   */
#endif

#define _gasneti_atomic_fence_before(__f) \
    _gasneti_atomic_mb_pre(__f)  /* no semi */ \
    _gasneti_atomic_rmb_pre(__f) /* no semi */ \
    _gasneti_atomic_wmb_pre(__f) /* no semi */
#define _gasneti_atomic_fence_after(__f) \
    _gasneti_atomic_mb_post(__f)  /* no semi */ \
    _gasneti_atomic_rmb_post(__f) /* no semi */ \
    _gasneti_atomic_wmb_post(__f) /* no semi */
#define _gasneti_atomic_fence_after_bool(__f, __v) \
    _gasneti_atomic_mb_post(__f)  /* no semi */    \
    _gasneti_atomic_rmb_post(__f) /* no semi */    \
    _gasneti_atomic_wmb_post(__f) /* no semi */    \
    _gasneti_atomic_rmb_bool(__f, __v) /* no semi */

#ifndef gasneti_atomic_init
  #define gasneti_atomic_init(v)	_gasneti_atomic_init(v)
#endif
#ifndef gasneti_atomic_set
  #define gasneti_atomic_set(p,v,f) do {                 \
    const int __flags = (f) & ~GASNETI_ATOMIC_FENCE_SET; \
    _gasneti_atomic_fence_before(__flags) /* no semi */  \
    _gasneti_atomic_set((p),(v));                        \
    _gasneti_atomic_fence_after(__flags) /* no semi */   \
  } while (0)
#endif
#ifndef gasneti_atomic_read
  GASNETI_INLINE(gasneti_atomic_read)
  uint32_t gasneti_atomic_read(gasneti_atomic_t *p, int flags) {
    const int __flags = flags & ~GASNETI_ATOMIC_FENCE_READ;
    _gasneti_atomic_fence_before(__flags) /* no semi */
    { const uint32_t retval = _gasneti_atomic_read(p);
      _gasneti_atomic_fence_after(__flags) /* no semi */
      return retval;
    }
  }
#endif
#ifndef gasneti_atomic_increment
  #define gasneti_atomic_increment(p,f) do {             \
    const int __flags = (f) & ~GASNETI_ATOMIC_FENCE_RMW; \
    _gasneti_atomic_fence_before(__flags) /* no semi */  \
    _gasneti_atomic_increment(p);                        \
    _gasneti_atomic_fence_after(__flags) /* no semi */   \
  } while (0)
#endif
#ifndef gasneti_atomic_decrement
  #define gasneti_atomic_decrement(p,f) do {             \
    const int __flags = (f) & ~GASNETI_ATOMIC_FENCE_RMW; \
    _gasneti_atomic_fence_before(__flags) /* no semi */  \
    _gasneti_atomic_decrement(p);                        \
    _gasneti_atomic_fence_after(__flags) /* no semi */   \
  } while (0)
#endif
#ifndef gasneti_atomic_decrement_and_test
  GASNETI_INLINE(gasneti_atomic_decrement_and_test)
  int gasneti_atomic_decrement_and_test(gasneti_atomic_t *p, int flags) {
    const int __flags = flags & ~GASNETI_ATOMIC_FENCE_RMW;
    _gasneti_atomic_fence_before(__flags) /* no semi */
    { const int retval = _gasneti_atomic_decrement_and_test(p);
      _gasneti_atomic_fence_after_bool(__flags, retval) /* no semi */
      return retval;
    }
  }
#endif
#if defined(GASNETI_HAVE_ATOMIC_CAS) && !defined(gasneti_atomic_compare_and_swap)
  GASNETI_INLINE(gasneti_atomic_compare_and_swap)
  int gasneti_atomic_compare_and_swap(gasneti_atomic_t *p, uint32_t oldval, uint32_t newval, int flags) {
    const int __flags = flags & ~GASNETI_ATOMIC_FENCE_RMW;
    _gasneti_atomic_fence_before(__flags) /* no semi */
    { const int retval = _gasneti_atomic_compare_and_swap(p,oldval,newval);
      _gasneti_atomic_fence_after_bool(__flags, retval) /* no semi */
      return retval;
    }
  }
#endif

/* ------------------------------------------------------------------------------------ */
/* GASNet weak atomics - these operations are guaranteed to be atomic if and only if 
    the sole updates are from the host processor(s), with no signals involved.
   if !GASNETI_THREADS, they compile away to a non-atomic counter
    thereby saving the overhead of unnecessary atomic-memory CPU instructions. 
   Otherwise, they expand to regular gasneti_atomic_t's
 */
#if GASNETI_THREADS || defined(GASNETI_FORCE_TRUE_WEAKATOMICS)
  typedef gasneti_atomic_t gasneti_weakatomic_t;
  #define gasneti_weakatomic_init(v)                  gasneti_atomic_init(v)
  #define gasneti_weakatomic_set(p,v,f)               gasneti_atomic_set(p,v,f)
  #define gasneti_weakatomic_read(p,f)                gasneti_atomic_read(p,f)
  #define gasneti_weakatomic_increment(p,f)           gasneti_atomic_increment(p,f)
  #define gasneti_weakatomic_decrement(p,f)           gasneti_atomic_decrement(p,f)
  #define gasneti_weakatomic_decrement_and_test(p,f)  gasneti_atomic_decrement_and_test(p,f)
  #ifdef GASNETI_HAVE_ATOMIC_CAS
    #define GASNETI_HAVE_WEAKATOMIC_CAS 1
    #define gasneti_weakatomic_compare_and_swap(p,oldval,newval,f)  \
            gasneti_atomic_compare_and_swap(p,oldval,newval,f)
  #endif
#else
  /* May not need any exclusion mechanism, but we still want to include any fences that
     the caller has requested, since any memory in the gasnet segment "protected" by a
     fenced atomic may be written by a network adapter.
   */
  typedef volatile int gasneti_weakatomic_t;
  #define gasneti_weakatomic_init(v)                  (v)
  #define gasneti_weakatomic_set(p,v,f) do { \
    const int __flags = (f);                 \
    _gasneti_atomic_fence_before(__flags);   \
    (*(p) = (v));                            \
    _gasneti_atomic_fence_after(__flags);    \
  } while (0)
  GASNETI_INLINE(gasneti_weakatomic_read)
  int gasneti_weakatomic_read(gasneti_weakatomic_t *p, const int flags) {
    _gasneti_atomic_fence_before(flags);
    { const int retval = *(p);
      _gasneti_atomic_fence_after(flags);
      return retval;
    }
  }
  #define gasneti_weakatomic_increment(p,f) do { \
    const int __flags = (f);                     \
    _gasneti_atomic_fence_before(__flags);       \
    (*(p))++;                                    \
    _gasneti_atomic_fence_after(__flags);        \
  } while (0)
  #define gasneti_weakatomic_decrement(p,f) do { \
    const int __flags = (f);                     \
    _gasneti_atomic_fence_before(__flags);       \
    (*(p))--;                                    \
    _gasneti_atomic_fence_after(__flags);        \
  } while (0)
  GASNETI_INLINE(gasneti_weakatomic_decrement_and_test)
  int gasneti_weakatomic_decrement_and_test(gasneti_weakatomic_t *p, const int flags) {
    _gasneti_atomic_fence_before(flags);
    { const int retval = !(--(*p));
      _gasneti_atomic_fence_after_bool(flags, retval);
      return retval;
    }
  }
  #define GASNETI_HAVE_WEAKATOMIC_CAS 1
  GASNETI_INLINE(gasneti_weakatomic_compare_and_swap)
  int gasneti_weakatomic_compare_and_swap(gasneti_weakatomic_t *p, uint32_t oldval, uint32_t newval, const int flags) {
    _gasneti_atomic_fence_before(flags);
    { const int retval = ((*p == (oldval)) ? (*p = (newval), 1) : 0);
      _gasneti_atomic_fence_after_bool(flags, retval);
      return retval;
    }
  }
#endif

/* ------------------------------------------------------------------------------------ */
#endif

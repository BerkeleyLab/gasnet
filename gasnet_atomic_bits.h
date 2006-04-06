/*   $Source: /Users/kamil/work/gasnet-cvs2/gasnet/gasnet_atomic_bits.h,v $
 *     $Date: 2006/04/06 20:06:30 $
 * $Revision: 1.135 $
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

   Semi-portable atomic operations
   --------------------------------
   These useful operations are available on most, but not all, platforms.

   + addition and subtraction
     gasneti_atomic_add(p, op, flags)
     gasneti_atomic_subtract(p, op, flags)

     These implement atomic addition and subtraction, where op must be non-negative.
     The result is platform dependent if the value of op is negative or out of the
     range of gasneti_atomic_t, or if the resulting value is out of range.
     Both return the value after the addition or subtraction.

    GASNETI_HAVE_ATOMIC_ADD_SUB will be defined to 1 when these operations are available.
    They are always either both available, or neither is available.

   + compare and swap
     gasneti_atomic_compare_and_swap(p, oldval, newval, flags)

     This operation is the atomic equivalent of:

      if (*p == oldval) {
        *p = newval;
        return NONZERO;
      } else {
        return 0;
      }

     GASNETI_HAVE_ATOMIC_CAS will be defined to 1 when this operation is available

   Memory fence properties of atomic operations
   --------------------------------------------

   NOTE: Atomic operations have no default memory fence properties, as this
   varies by platform.  Every atomic operation except _init() includes a final
   argument (f or flags) to indicate the caller's minimum fence requirements.
   Depending on the platform, the implementation may use fences stronger than
   those requested, but never weaker.
 */

#if defined(GASNETI_FORCE_GENERIC_ATOMICOPS) || /* for debugging */          \
    defined(CRAYT3E)   || /* T3E seems to have no atomic ops */              \
    defined(_SX)       || /* NEC SX-6 atomics not available to user code? */ \
    ((defined(__SUNPRO_C) || defined(__SUNPRO_CC)) && defined(__sparc) && !defined(GASNETI_ARCH_ULTRASPARC))
			  /* We've not implemented Sun C support for pre-Ultrasparc CPUs */
  #define GASNETI_USE_GENERIC_ATOMICOPS
#elif defined(GASNETI_FORCE_OS_ATOMICOPS) || /* for debugging */          \
    defined(MTA)   ||  \
    defined(_SGI_COMPILER_VERSION)
  #define GASNETI_USE_OS_ATOMICOPS
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
/* Yuck */
#if defined(__x86_64__) || /* x86 and Athlon/Opteron */ \
    defined(__i386__) || defined(__i386) || defined(i386) || \
    defined(__i486__) || defined(__i486) || defined(i486) || \
    defined(__i586__) || defined(__i586) || defined(i586) || \
    defined(__i686__) || defined(__i686) || defined(i686)
  #ifdef GASNETI_UNI_BUILD
    #define GASNETI_X86_LOCK_PREFIX ""
  #else
    #define GASNETI_X86_LOCK_PREFIX "lock\n\t"
  #endif
#endif
/* ------------------------------------------------------------------------------------ */
#if defined(GASNETI_USE_GENERIC_ATOMICOPS)
  /* a very slow but portable implementation of atomic ops using mutexes */
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
    extern int _gasneti_atomic_decrement_and_test_32(gasneti_atomic_t *p);
    #define GASNETI_GENERIC_DEC_AND_TEST_DEF                      \
    int _gasneti_atomic_decrement_and_test_32(gasneti_atomic_t *p) { \
      uint32_t newval;                                            \
      gasnet_hsl_lock((gasnet_hsl_t*)gasneti_patomicop_lock);     \
      newval = p->ctr - 1;                                        \
      p->ctr = newval;                                            \
      gasnet_hsl_unlock((gasnet_hsl_t*)gasneti_patomicop_lock);   \
      return (newval == 0);                                       \
    }
    #define _gasneti_atomic_decrement_and_test _gasneti_atomic_decrement_and_test_32

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

    extern uint32_t gasneti_atomic_addfetch_32(gasneti_atomic_t *p, int32_t op);
    #define GASNETI_GENERIC_ADD_SUB_DEF                            \
    extern uint32_t gasneti_atomic_addfetch_32(gasneti_atomic_t *p,\
                                               int32_t op) {       \
      uint32_t retval;                                             \
      gasnet_hsl_lock((gasnet_hsl_t*)gasneti_patomicop_lock);      \
      retval = (((p)->ctr) += (op));                               \
      gasnet_hsl_unlock((gasnet_hsl_t*)gasneti_patomicop_lock);    \
      return retval;                                               \
    }
    #define _gasneti_atomic_addfetch gasneti_atomic_addfetch_32

    #if (GASNET_PAR || GASNETI_CONDUIT_THREADS)
      /* Using real HSLs which yeild an ACQ/RMB before and REL/WMB after the atomic */
      #define GASNETI_ATOMIC_FENCE_SET (GASNETI_ATOMIC_RMB_PRE | GASNETI_ATOMIC_WMB_POST)
      #define GASNETI_ATOMIC_FENCE_RMW (GASNETI_ATOMIC_RMB_PRE | GASNETI_ATOMIC_WMB_POST)
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
    GASNETI_INLINE(_gasneti_atomic_decrement_and_test_32)
    int _gasneti_atomic_decrement_and_test_32(gasneti_atomic_t *p) {
      uint32_t newval;
      GASNETI_ATOMICOP_INITCHECK();
      pthread_mutex_lock(&gasneti_atomicop_mutex);
      newval = p->ctr - 1;
      p->ctr = newval;
      pthread_mutex_unlock(&gasneti_atomicop_mutex);
      return (newval == 0);
    }
    #define _gasneti_atomic_decrement_and_test _gasneti_atomic_decrement_and_test_32

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

    GASNETI_INLINE(gasneti_atomic_addfetch_32)
    uint32_t gasneti_atomic_addfetch_32(gasneti_atomic_t *p, int32_t op) {
      uint32_t retval;
      GASNETI_ATOMICOP_INITCHECK();
      pthread_mutex_lock(&gasneti_atomicop_mutex);
      retval = (((p)->ctr) += op);
      pthread_mutex_unlock(&gasneti_atomicop_mutex);
      return retval;
    }
    #define _gasneti_atomic_addfetch gasneti_atomic_addfetch_32

    #if (defined(__APPLE__) && defined(__MACH__))
      /* OSX/Darwin tries to be too smart when only 1 thread is running, so use defaults */
      /* XXX: determine what fence (if any) might still be present? */
    #else
      /* Using real mutexes which yeild an ACQ/RMB before and REL/WMB after the atomic */
      #define GASNETI_ATOMIC_FENCE_SET (GASNETI_ATOMIC_RMB_PRE | GASNETI_ATOMIC_WMB_POST)
      #define GASNETI_ATOMIC_FENCE_RMW (GASNETI_ATOMIC_RMB_PRE | GASNETI_ATOMIC_WMB_POST)
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
    #define GASNETI_HAVE_ATOMIC_CAS 1

    #define _gasneti_atomic_add(p,op)      ((*(p))+=(op))
    #define _gasneti_atomic_subtract(p,op) ((*(p))-=(op))
    #define GASNETI_HAVE_ATOMIC_ADD_SUB 1

    /* Using default fences */
  #endif
#elif defined(GASNETI_USE_OS_ATOMICOPS)
  /* ------------------------------------------------------------------------------------
   * Use OS-provided atomics, which should be CPU-independent and
   * which should work regardless of the compiler's inline assembly support.
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

      #define _gasneti_atomic_fetchadd(p,op) fetch_and_add((atomic_p)&((p)->ctr), op)

      /* No syncs in these calls, so use default fences */
  #elif defined(IRIX)
      #include <mutex.h>
      #include <ulocks.h>
      typedef __uint32_t gasneti_atomic_t;
      #define _gasneti_atomic_increment(p) (test_then_add32((p),1))
      #define _gasneti_atomic_decrement(p) (test_then_add32((p),(uint32_t)-1))
      #define _gasneti_atomic_read(p)      (*(volatile __uint32_t *)(p))
      #define _gasneti_atomic_set(p,v)     (*(volatile __uint32_t *)(p) = (v))
      #define _gasneti_atomic_init(v)      (v)
      #define _gasneti_atomic_decrement_and_test(p) \
                                          (add_then_test32((p),(uint32_t)-1) == 0) 

      #if defined(_SGI_COMPILER_VERSION)
        GASNETI_INLINE(_gasneti_atomic_compare_and_swap)
        int _gasneti_atomic_compare_and_swap(gasneti_atomic_t *p, int oldval, int newval) {
            return __compare_and_swap( p, oldval, newval ); /* bug1534: compiler built-in */
        }
      #elif defined(__GNUC__)
        GASNETI_INLINE(_gasneti_atomic_compare_and_swap)
        int _gasneti_atomic_compare_and_swap(gasneti_atomic_t *p, uint32_t oldval, uint32_t newval) {
           uint32_t temp;
           int retval;
           __asm__ __volatile__ (
                "1:\n\t"
                "ll        %1,%5\n\t"          /* Load from *p */
                "move      %0,$0\n\t"          /* Assume mismatch */
                "bne       %1,%3,2f\n\t"       /* Break loop on mismatch */
                "move      %0,%4\n\t"          /* Move newval to retval */
                "sc        %0,%2\n\t"          /* Try SC to store retval */
                "beqz      %0,1b\n"            /* Retry on contention */
                "2:\n\t"
                : "=&r" (retval), "=&r" (temp), "=m" (*p)
                : "r" (oldval), "r" (newval), "m" (*p)
                : "memory");
          return retval;
        }
      #else /* flaky OS-provided CAS */
        usptr_t * volatile _gasneti_usmem_ptr;
        gasneti_atomic_t _gasneti_usmem_ptr_init;
        GASNETI_INLINE(_gasneti_atomic_compare_and_swap)
        int _gasneti_atomic_compare_and_swap(gasneti_atomic_t *p, int oldval, int newval) {
          if_pf (!_gasneti_usmem_ptr) { /* need exactly one call to usinit on first invocation */
            if (test_and_set32(&_gasneti_usmem_ptr_init,1) == 0) {
              _gasneti_usmem_ptr = usinit("/dev/zero");
            } else while (!_gasneti_usmem_ptr);
          }
          return uscas32( p, oldval, newval, _gasneti_usmem_ptr ); /* from libc */
        }
      #endif
      #define GASNETI_HAVE_ATOMIC_CAS 1

      /* Almost the default add/sub: */
      #define _gasneti_atomic_addfetch(p,op) (add_then_test32((p),(uint32_t)(op))) 

      /* Using default fences - the docs claim acquire or release "barriers" for the various 
         intrinsics, but those are only compiler fences and not architectural sync instructions */
  #elif defined(__MTA__)
      /* use MTA intrinsics */
      typedef int64_t gasneti_atomic_t;
      #define _gasneti_atomic_increment(p) (int_fetch_add((p),1))
      #define _gasneti_atomic_decrement(p) (int_fetch_add((p),-1))
      #define _gasneti_atomic_read(p)      ((int64_t)*(volatile int64_t*)(p))
      #define _gasneti_atomic_set(p,v)     ((*(volatile int64_t*)(p)) = (v))
      #define _gasneti_atomic_init(v)      (v)
      #define _gasneti_atomic_decrement_and_test(p) \
                                          (int_fetch_add((p),-1) == 1) 

      #define _gasneti_atomic_fetchadd int_fetch_add

      /* Using default fences, but this machine is Sequential Consistent anyway */
  #elif defined(SOLARIS)	/* BROKEN (and incomplete) */
      /* $%*(! Solaris has atomic functions in the kernel but refuses to expose them
         to the user... after all, what application would be interested in performance? */
      #include <sys/atomic.h>
      typedef struct { volatile uint32_t ctr; } gasneti_atomic_t;
      #define _gasneti_atomic_increment(p) (atomic_add_32((uint32_t *)&((p)->ctr),1))
      #define _gasneti_atomic_read(p)      ((p)->ctr)
      #define _gasneti_atomic_set(p,v)     ((p)->ctr = (v))
      #define _gasneti_atomic_init(v)      { (v) }
  #elif defined(CYGWIN)
      /* These are *NOT* Cywgin calls, but Windows API calls that may actually
       * be intrinsics in the MS compilers on 64-bit systems. */
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

      #define _gasneti_atomic_fetchadd(p, op) InterlockedExchangeAdd((LONG *)&((p)->ctr), op)

      /* MSDN docs ensure memory fence in these calls, even on ia64 */
      #define GASNETI_ATOMIC_FENCE_RMW (GASNETI_ATOMIC_MB_PRE | GASNETI_ATOMIC_MB_POST)
  #elif defined(__linux__) 
      /* ------------------------------------------------------------------------------------
       * Linux provides an asm/atomic.h that is sometimes just useless
       * and other times supplies all but compare-and-swap (even when
       * it is implemented).  So, this code is probably only useful when
       * we encounter a new Linux platform.
       * ------------------------------------------------------------------------------------ */
      /* Disable using this code if this is a gasnet-smp build and the 
         linux/config.h settings disagree (due to system config problem or 
         cross-compiling on a uniprocessor frontend for smp nodes) */
      #include <linux/config.h>
      #if !(defined(CONFIG_SMP) || defined(GASNETI_UNI_BUILD))
        #error Building against a uniprocessor kernel.  Configure with --disable-smp-safe (for uniprocessor compute nodes), or build on an SMP host.
      #endif
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

      /* Using default fences as we can't hope to know what to expect on new platforms */
  #else
    #error GASNETI_USE_OS_ATOMICS defined on unsupported OS - need to implement GASNet atomics (or #define GASNETI_USE_GENERIC_ATOMICOPS)
  #endif
#else
  /* ------------------------------------------------------------------------------------
   * Not using GENERIC (mutex) or OS-provided atomics, so provide our own based on the
   * CPU and compiler support for inline assembly code
   * ------------------------------------------------------------------------------------ */
  #if defined(__x86_64__) || defined(__amd64) || /* x86 and Athlon/Opteron */ \
      defined(__i386__) || defined(__i386) || defined(i386) || \
      defined(__i486__) || defined(__i486) || defined(i486) || \
      defined(__i586__) || defined(__i586) || defined(i586) || \
      defined(__i686__) || defined(__i686) || defined(i686)
    #if defined(__GNUC__) || defined(__INTEL_COMPILER) || defined(__PATHCC__) || defined(PGI_WITH_REAL_ASM)
     typedef struct { volatile int ctr; } gasneti_atomic_t;
     #define _gasneti_atomic_init(v)      { (v) }
     #if defined(PGI_WITH_REAL_ASM) && defined(__cplusplus) /* PGI C++ lacks inline assembly */
        #define GASNETI_USING_SLOW_ATOMICS 1
     #else
      #if defined(__PATHCC__)
        /* Pathscale optimizer is buggy and fails to clobber memory output location correctly
           unless we include an extraneous full memory clobber 
         */
        #define GASNETI_ATOMIC_MEM_CLOBBER ,"memory"
      #else
        #define GASNETI_ATOMIC_MEM_CLOBBER
      #endif
      #define _gasneti_atomic_read(p)      ((p)->ctr)
      #define _gasneti_atomic_set(p,v)     ((p)->ctr = (v))

      GASNETI_INLINE(_gasneti_atomic_increment_32)
      void _gasneti_atomic_increment_32(gasneti_atomic_t *v) {
        __asm__ __volatile__(
                GASNETI_X86_LOCK_PREFIX
		"incl %0"
                : "=m" (v->ctr)
                : "m" (v->ctr)
                : "cc" GASNETI_ATOMIC_MEM_CLOBBER);
      }
      #define _gasneti_atomic_increment _gasneti_atomic_increment_32
      GASNETI_INLINE(_gasneti_atomic_decrement_32)
      void _gasneti_atomic_decrement_32(gasneti_atomic_t *v) {
        __asm__ __volatile__(
                GASNETI_X86_LOCK_PREFIX
		"decl %0"
                : "=m" (v->ctr)
                : "m" (v->ctr) 
                : "cc" GASNETI_ATOMIC_MEM_CLOBBER);
      }
      #define _gasneti_atomic_decrement _gasneti_atomic_decrement_32
      GASNETI_INLINE(_gasneti_atomic_decrement_and_test_32)
      int _gasneti_atomic_decrement_and_test_32(gasneti_atomic_t *v) {
          register unsigned char retval;
          __asm__ __volatile__(
	          GASNETI_X86_LOCK_PREFIX
		  "decl %0		\n\t"
		  "sete %1"
	          : "=m" (v->ctr), "=mq" (retval)
	          : "m" (v->ctr) 
                  : "cc" GASNETI_ATOMIC_MEM_CLOBBER);
          return retval;
      }
      #define _gasneti_atomic_decrement_and_test _gasneti_atomic_decrement_and_test_32

      GASNETI_INLINE(_gasneti_atomic_compare_and_swap)
      int _gasneti_atomic_compare_and_swap(gasneti_atomic_t *v, uint32_t oldval, uint32_t newval) {
        register unsigned char retval;
        register uint32_t readval;
        __asm__ __volatile__ (
		GASNETI_X86_LOCK_PREFIX
		"cmpxchgl %3, %1	\n\t"
		"sete %0"
		: "=mq" (retval), "=m" (v->ctr), "=a" (readval)
		: "r" (newval), "m" (v->ctr), "a" (oldval)
		: "cc", "memory");
        return (int)retval;
      }

      GASNETI_INLINE(gasneti_atomic_fetchadd_32)
      uint32_t gasneti_atomic_fetchadd_32(gasneti_atomic_t *v, int32_t op) {
	uint32_t tmp = op;
        __asm__ __volatile__(
                GASNETI_X86_LOCK_PREFIX
		"xaddl %1, %0"
                : "=m" (v->ctr), "=&r" (tmp)
                : "1" (tmp), "m" (v->ctr)
                : "cc" GASNETI_ATOMIC_MEM_CLOBBER);
	return tmp;
      }

      /* x86 and x86_64 include full memory fence in locked RMW insns */
      #define GASNETI_ATOMIC_FENCE_RMW (GASNETI_ATOMIC_MB_PRE | GASNETI_ATOMIC_MB_POST)
     #endif /* !slow atomics */

     #define GASNETI_HAVE_ATOMIC_CAS 1
     #define _gasneti_atomic_fetchadd gasneti_atomic_fetchadd_32
    #elif defined(__SUNPRO_C) || defined(__SUNPRO_CC) || defined(__PGI)
      /* First, some macros to hide the x86 vs. x86-64 ABI differences */
      #if defined(__x86_64__) || defined(__amd64)
        #define _gasneti_atomic_addr		"(%rdi)"
        #define _gasneti_atomic_load_arg0	""	/* arg0 in rdi */
        #define _gasneti_atomic_load_arg1	"movl %esi, %eax	\n\t"
	#define _gasneti_atomic_load_arg2	""	/* arg2 in rdx */
      #else
        #define _gasneti_atomic_addr		"(%ecx)"
        #define _gasneti_atomic_load_arg0	"movl 8(%ebp), %ecx	\n\t"
        #define _gasneti_atomic_load_arg1	"movl 12(%ebp), %eax	\n\t"
	#define _gasneti_atomic_load_arg2	"movl 16(%ebp), %edx	\n\t"
      #endif

      typedef struct { volatile int ctr; } gasneti_atomic_t;
      #define _gasneti_atomic_init(v)      { (v) }
      #define _gasneti_atomic_read(p)      ((p)->ctr)
      #define _gasneti_atomic_set(p,v)     ((p)->ctr = (v))

      #define GASNETI_ATOMIC_INCREMENT_BODY				\
	  GASNETI_ASM( _gasneti_atomic_load_arg0			\
		       GASNETI_X86_LOCK_PREFIX				\
		       "incl " _gasneti_atomic_addr );

      #define GASNETI_ATOMIC_DECREMENT_BODY				\
	  GASNETI_ASM( _gasneti_atomic_load_arg0			\
		       GASNETI_X86_LOCK_PREFIX				\
		       "decl " _gasneti_atomic_addr );

      #define GASNETI_ATOMIC_DECREMENT_AND_TEST_BODY			\
	  GASNETI_ASM( _gasneti_atomic_load_arg0			\
		       GASNETI_X86_LOCK_PREFIX				\
		       "decl " _gasneti_atomic_addr			"\n\t" \
		       "sete %cl					\n\t" \
		       "movzbl %cl, %eax" );

      #define GASNETI_ATOMIC_COMPARE_AND_SWAP_BODY			\
	  GASNETI_ASM( _gasneti_atomic_load_arg0			\
		       _gasneti_atomic_load_arg1			\
		       _gasneti_atomic_load_arg2			\
		       GASNETI_X86_LOCK_PREFIX				\
		       "cmpxchgl %edx, " _gasneti_atomic_addr		"\n\t" \
		       "sete  %cl					\n\t" \
		       "movzbl  %cl, %eax" );
      #define GASNETI_HAVE_ATOMIC_CAS 1

#if 1
      /* Fetch-add version is faster for calls that ignore the result and
       * for subtraction of constants.  In both cases because the "extra"
       * work is done in C code that the optimizer can discard.
       */
      #define GASNETI_ATOMIC_FETCHADD_BODY				\
	  GASNETI_ASM( _gasneti_atomic_load_arg0			\
		       _gasneti_atomic_load_arg1			\
		       GASNETI_X86_LOCK_PREFIX				\
		       "xadd %eax, " _gasneti_atomic_addr	);
#else
      #define GASNETI_ATOMIC_ADD_BODY					\
	  GASNETI_ASM( _gasneti_atomic_load_arg0			\
		       _gasneti_atomic_load_arg1			\
		       "movl %eax, %edx					\n\t" \
		       GASNETI_X86_LOCK_PREFIX				\
		       "xadd %eax, " _gasneti_atomic_addr		"\n\t" \
		       "addl %edx, %eax"	);

      #define GASNETI_ATOMIC_SUBTRACT_BODY				\
	  GASNETI_ASM( _gasneti_atomic_load_arg0			\
		       _gasneti_atomic_load_arg1			\
		       "movl %eax, %edx					\n\t" \
		       "negl %eax					\n\t" \
		       GASNETI_X86_LOCK_PREFIX				\
		       "xadd %eax, " _gasneti_atomic_addr		"\n\t" \
		       "subl %edx, %eax"	);

      #define GASNETI_HAVE_ATOMIC_ADD_SUB 1
#endif

      /* x86 and x86_64 include full memory fence in locked RMW insns */
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
                    (_InterlockedCompareExchange_acq((volatile unsigned int *)&((p)->ctr),nval,oval) == (oval))
      #define GASNETI_HAVE_ATOMIC_CAS 1

      /* The default c-a-s based add and subtract are already the best we can do. */

      /* See fence treatment after #endif */
    #elif defined(__GNUC__)
      GASNETI_INLINE(gasneti_cmpxchg)
      uint32_t gasneti_cmpxchg(int32_t volatile *ptr, int32_t oldval, int32_t newval) {
        int64_t tmp = (int64_t)oldval;

	/* Load "ar.ccv", the special register used for "oldval" in c-a-s */
        __asm__ __volatile__ ("mov ar.ccv=%0;;" :: "rO"(tmp));

        __asm__ __volatile__ ("cmpxchg4.acq %0=[%1],%2,ar.ccv"
                                : "=r"(tmp) : "r"(ptr), "r"(newval) : "memory");
        return (uint32_t) tmp;
      }
      GASNETI_INLINE(gasneti_atomic_fetchandinc_32)
      uint32_t gasneti_atomic_fetchandinc_32(int32_t volatile *ptr) {
        uint64_t result;\
        asm volatile ("fetchadd4.acq %0=[%1],%2"
                                : "=r"(result) : "r"(ptr), "i" (1)
                                : "memory");
        return (uint32_t) result;
      }
      GASNETI_INLINE(gasneti_atomic_fetchanddec_32)
      uint32_t gasneti_atomic_fetchanddec_32(int32_t volatile *ptr) {
        uint64_t result;\
        asm volatile ("fetchadd4.acq %0=[%1],%2"
                                : "=r"(result) : "r"(ptr), "i" (-1)
                                : "memory");
        return (uint32_t) result;
      }
      typedef struct { volatile int32_t ctr; } gasneti_atomic_t;
      #define _gasneti_atomic_increment(p) (gasneti_atomic_fetchandinc_32(&((p)->ctr)))
      #define _gasneti_atomic_decrement(p) (gasneti_atomic_fetchanddec_32(&((p)->ctr)))
      #define _gasneti_atomic_read(p)      ((p)->ctr)
      #define _gasneti_atomic_set(p,v)     ((p)->ctr = (v))
      #define _gasneti_atomic_init(v)      { (v) }
      #define _gasneti_atomic_decrement_and_test(p) (gasneti_atomic_fetchanddec_32(&((p)->ctr)) == 1)

      #define _gasneti_atomic_compare_and_swap(p,oval,nval) \
        (gasneti_cmpxchg((volatile int *)&((p)->ctr),oval,nval) == (oval))
      #define GASNETI_HAVE_ATOMIC_CAS 1

      /* The default c-a-s based add and subtract are already the best we can do. */

      /* See fence treatment after #endif */
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

      /* The default c-a-s based add and subtract are already the best we can do. */

      /* See fence treatment after #endif */
    #else
      #error unrecognized Itanium compiler - need to implement GASNet atomics (or #define GASNETI_USE_GENERIC_ATOMICOPS)
    #endif

    /* Since all 3 compilers are generating r-m-w with .acq variants, we can customize
     * the atomic fencing implementation by noting that "mf;; foo.acq" is a full memory
     * barrier both before and after. */
    #define _gasneti_atomic_fence_before_rmw(flags) \
	if (flags & (GASNETI_ATOMIC_MB_PRE | GASNETI_ATOMIC_MB_POST)) gasneti_local_mb();
    #define _gasneti_atomic_fence_after_rmw(flags) \
	/* Nothing */
    #define _gasneti_atomic_fence_after_bool(flags, val) \
	if (!(flags & (GASNETI_ATOMIC_MB_PRE | GASNETI_ATOMIC_MB_POST))) \
	  { _gasneti_atomic_rmb_bool(flags, val) } 
  /* ------------------------------------------------------------------------------------ */
  #elif defined(__alpha__) || defined(__alpha) /* DEC Alpha */
    #if defined(__GNUC__)
      GASNETI_INLINE(gasneti_atomic_fetchandadd_32)
      int32_t gasneti_atomic_fetchandadd_32(int32_t volatile *v, int32_t op) {
        register int32_t temp;
        register int32_t result;
        __asm__ __volatile__ (
		"1:	ldl_l	%1, %2		\n"	/* result = *addr */
		"	addl	%1, %3, %0	\n"	/* temp = result + op */
		"	stl_c	%0, %2		\n"	/* *addr = temp; temp = store_OK */
		"	beq	%0, 1b"			/* Retry on ll/ss failure */
		: "=&r" (temp), "=&r" (result), "=m" (*v) /* outputs */
		: "IOr" (op)       /* inputs */
		: "memory", "cc"); /* kills */
        return result;
      }
      GASNETI_INLINE(gasneti_atomic_add_32)
      void gasneti_atomic_add_32(int32_t volatile *v, int32_t op) {
        register int32_t temp;
        __asm__ __volatile__ (
		"1:	ldl_l	%0, %1		\n"	/* temp = *addr */
		"	addl	%0, %2, %0	\n"	/* temp += op */
		"	stl_c	%0, %1		\n"	/* *addr = temp; temp = store_OK */
		"	beq	%0, 1b"			/* Retry on ll/ss failure */
		: "=&r" (temp), "=m" (*v) /* outputs */
		: "IOr" (op)              /* inputs */
		: "memory", "cc");        /* kills */
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

     #define _gasneti_atomic_fetchadd(p,op) gasneti_atomic_fetchandadd_32(&((p)->ctr), op)

     /* No fences in our asm, so using default fences */
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

       #define _gasneti_atomic_fetchadd(p, op) __ATOMIC_ADD_LONG(&((p)->ctr), op)

       /* Both the instrisics and our asm lack built-in fences.  So, using default fences */
    #else
      #error unrecognized Alpha compiler - need to implement GASNet atomics (or #define GASNETI_USE_GENERIC_ATOMICOPS)
    #endif
  /* ------------------------------------------------------------------------------------ */
  #elif defined(__sparc) || defined(__sparc__)
    #if defined(__sparcv9) || defined(__sparcv9cpu) || defined(GASNETI_ARCH_ULTRASPARC) /* SPARC v9 ISA */
      #if defined(__GNUC__)
        static __inline__ int32_t gasneti_atomic_fetchandadd_32(int32_t volatile *v, int32_t op) {
          /* SPARC v9 architecture manual, p.333 
           * This function requires the cas instruction in Sparc V9, and therefore gcc -mcpu=ultrasparc
	   * The manual says (sec A.9) no memory fences in CAS (in conflict w/ JMM web page).
           */
          register int32_t volatile * addr = (int32_t volatile *)v;
          register int32_t oldval;
          register int32_t newval;
          __asm__ __volatile__ ( 
            "ld       [%2],%0    \n\t" /* oldval = *addr; */
            "0:			 \t" 
            "add      %0,%3,%1   \n\t" /* newval = oldval + op; */
            "cas      [%2],%0,%1 \n\t" /* if (*addr == oldval) SWAP(*addr,newval); else newval = *addr; */
            "cmp      %0, %1     \n\t" /* check if newval == oldval (swap succeeded) */
            "bne,a,pn %%icc, 0b  \n\t" /* otherwise, retry (,pn == predict not taken; ,a == annul) */
            "  mov    %1, %0     "     /* oldval = newval; (branch delay slot, annulled if not taken) */
            : "=&r"(oldval), "=&r"(newval)
            : "r" (addr), "rn"(op) 
            : "memory");
          return oldval;
        }
        typedef struct { volatile int32_t ctr; } gasneti_atomic_t;
        #define _gasneti_atomic_increment(p) (gasneti_atomic_fetchandadd_32(&((p)->ctr),1))
        #define _gasneti_atomic_decrement(p) (gasneti_atomic_fetchandadd_32(&((p)->ctr),-1))
        #define _gasneti_atomic_read(p)      ((p)->ctr)
        #define _gasneti_atomic_set(p,v)     ((p)->ctr = (v))
        #define _gasneti_atomic_init(v)      { (v) }
        #define _gasneti_atomic_decrement_and_test(p) (gasneti_atomic_fetchandadd_32(&((p)->ctr),-1) == 1)

        GASNETI_INLINE(_gasneti_atomic_compare_and_swap)
        int _gasneti_atomic_compare_and_swap(gasneti_atomic_t *v, uint32_t oldval, uint32_t newval) {
          register volatile uint32_t * addr = (volatile uint32_t *)&(v->ctr);
          __asm__ __volatile__ ( 
              "cas      [%2],%1,%0"  /* if (*addr == oldval) SWAP(*addr,newval); else newval = *addr; */
              : "+r"(newval)
              : "r"(oldval), "r" (addr)
              : "memory");
          return (int)(newval == oldval);
        }
        #define GASNETI_HAVE_ATOMIC_CAS 1

        #define _gasneti_atomic_fetchadd(p,op) gasneti_atomic_fetchandadd_32(&((p)->ctr),op)

	/* Using default fences, as our asm includes none */
      #elif defined(__SUNPRO_C) || defined(__SUNPRO_CC)
	typedef struct { volatile uint32_t ctr; } gasneti_atomic_t;
	#define _gasneti_atomic_init(v)      { (v) }
        #define _gasneti_atomic_read(p)      ((p)->ctr)
        #define _gasneti_atomic_set(p,v)     ((p)->ctr = (v))
        #define _gasneti_atomic_increment(p) (_gasneti_atomic_fetchadd(p,1))
        #define _gasneti_atomic_decrement(p) (_gasneti_atomic_fetchadd(p,-1))
        #define _gasneti_atomic_decrement_and_test(p) (_gasneti_atomic_fetchadd(p,-1) == 1)

        #define GASNETI_ATOMIC_COMPARE_AND_SWAP_BODY					\
	    GASNETI_ASM(								\
		/* if (*addr == oldval) SWAP(*addr,newval); else newval = *addr; */	\
		     "cas	[%i0], %i1, %i2		\n\t"				\
		/* retval = (oldval == newval) ? 1 : 0				*/	\
		     "xor	%i2, %i1, %g1		\n\t" /* g1 = 0 IFF old==new */ \
		     "cmp	%g0, %g1		\n\t" /* Set/clear carry bit */	\
		     "subx	%g0, -1, %i0 " );	      /* Subtract w/ carry */
        #define GASNETI_HAVE_ATOMIC_CAS 1

        #define GASNETI_ATOMIC_FETCHADD_BODY /* see gcc asm, above, for more detail */	\
	    GASNETI_ASM(								\
		/* oldval = *addr;						*/	\
		     "ld	[%i0], %g1		\n"				\
		/* while (!cas(addr, oldval, oldval + op)) { oldval = *addr; }	*/	\
		     "0:				\n\t"				\
		     "add	%g1, %i1, %i5		\n\t"				\
		     "cas	[%i0], %g1, %i5		\n\t"				\
		     "cmp	%g1, %i5		\n\t"				\
		     "bne,a,pn	%icc, 0b		\n\t"				\
		     "  mov	%i5, %g1		\n\t" /* annulled delay slot */	\
		/* Retval = oldval						*/	\
		     "mov	%i5, %i0" );

	/* Using default fences, as our asm includes none */
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
        GASNETI_INLINE(gasneti_atomic_fetchandadd_32)
        int32_t gasneti_atomic_fetchandadd_32(gasneti_atomic_t *p, int32_t op) {
          int32_t tmp;
          gasneti_assert(p->initflag == GASNETI_ATOMIC_INIT_MAGIC);
          gasneti_local_wmb();
          gasneti_atomic_spinuntil(p->ctr && (tmp = gasneti_loadandclear_32(&(p->ctr))));
          gasneti_assert(tmp & GASNETI_ATOMIC_PRESENT);
          p->ctr = (GASNETI_ATOMIC_PRESENT | (tmp + op));
          return (tmp & ~GASNETI_ATOMIC_PRESENT);
        }
        #if 0
          /* this version fails if set is used in a race with fetchandadd */
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
        #define _gasneti_atomic_increment(p) (gasneti_atomic_fetchandadd_32(p,1))
        #define _gasneti_atomic_decrement(p) (gasneti_atomic_fetchandadd_32(p,-1))
        #define _gasneti_atomic_decrement_and_test(p) (gasneti_atomic_fetchandadd_32(p,-1) == 1)

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

        #define _gasneti_atomic_fetchadd gasneti_atomic_fetchandadd_32

        /* Our code has the following fences: (noting that RMB is empty) */
	#define GASNETI_ATOMIC_FENCE_SET	GASNETI_ATOMIC_MB_PRE
	#define GASNETI_ATOMIC_FENCE_RMW	GASNETI_ATOMIC_MB_PRE
      #else
        #error unrecognized Sparc pre-v9 compiler - need to implement GASNet atomics (or #define GASNETI_USE_GENERIC_ATOMICOPS)
      #endif
    #endif
  /* ------------------------------------------------------------------------------------ */
  #elif defined(__hppa) || defined(__hppa__) /* PA-RISC */
    /* all we get is atomic load-and-clear, but that's actually just barely enough  */
    #define GASNETI_ATOMICOPS_NOT_SIGNALSAFE 1 /* not signal-safe because of "checkout" semantics */
    /* The load-and-clear requires 16-byte alignment.  Therefore the type (and its
     * initializer) replicate the value field 4 times.  The actual ops will only use
     * the one of them that turns out to be 16-byte aligned.
     */
    typedef struct { volatile uint64_t initflag; volatile int32_t _ctr[4]; char _pad; } gasneti_atomic_t;
    #define _gasneti_atomic_init(v)      {    \
            GASNETI_ATOMIC_INIT_MAGIC,       \
            { (GASNETI_ATOMIC_PRESENT|(v)),  \
              (GASNETI_ATOMIC_PRESENT|(v)),  \
              (GASNETI_ATOMIC_PRESENT|(v)),  \
              (GASNETI_ATOMIC_PRESENT|(v)) } \
            }
    #if defined(__HP_aCC) /* HP C++ compiler */
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
      #define GASNETI_ATOMIC_CTR(p)     ((volatile int32_t *)GASNETI_ALIGNUP(&(p->_ctr),16))
      #define GASNETI_ATOMIC_PRESENT    ((int32_t)0x80000000)
      #define GASNETI_ATOMIC_INIT_MAGIC ((uint64_t)0x8BDEF66BAD1E3F3AULL)
      /* would like to use gasneti_waituntil here, but it requires libgasnet for waitmode */
      #define gasneti_atomic_spinuntil(cond) do {       \
              while (!(cond)) gasneti_compiler_fence(); \
              gasneti_local_rmb();                      \
              } while (0)
      GASNETI_INLINE(gasneti_atomic_fetchandadd_32)
      int32_t gasneti_atomic_fetchandadd_32(gasneti_atomic_t *p, int32_t op) {
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
        /* this version fails if set is used in a race with fetchandadd */
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
      #define _gasneti_atomic_increment(p) (gasneti_atomic_fetchandadd_32(p,1))
      #define _gasneti_atomic_decrement(p) (gasneti_atomic_fetchandadd_32(p,-1))
      #define _gasneti_atomic_decrement_and_test(p) (gasneti_atomic_fetchandadd_32(p,-1) == 1)

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

      /* Our code has the following fences: (noting that RMB is empty) */
      #define GASNETI_ATOMIC_FENCE_SET	GASNETI_ATOMIC_MB_PRE
      #define GASNETI_ATOMIC_FENCE_RMW	GASNETI_ATOMIC_MB_PRE
    #endif /* ! slow atomics */
    #define gasneti_atomic_fetchandadd gasneti_atomic_fetchandadd_32
    #define GASNETI_HAVE_ATOMIC_CAS 1
  /* ------------------------------------------------------------------------------------ */
  #elif defined(__crayx1) /* This works on X1, but NOT the T3E */
    #include <intrinsics.h>
    typedef volatile long gasneti_atomic_t;
    /* man pages for atomic ops claim gsync is required for using atomic ops,
       but it's unclear when exactly it is required for our purposes - 
       technically we shouldn't need any sync for a bare unfenced AMO, but 
       experimentally determined using testtools that without a gsync in the vicinity
       of the AMO call, the compiler/architecture will break the semantics of the AMO
       with respect to the atomic location, even with only a single thread!
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
    GASNETI_INLINE(_gasneti_atomic_decrement_and_test_64)
    int _gasneti_atomic_decrement_and_test_64(gasneti_atomic_t *p) {
       int retval;
       gasneti_atomic_presync();
       retval = (_amo_afadd((p),(long)-1) == 1);
       gasneti_atomic_postsync();
       return retval;
    }
    #define _gasneti_atomic_decrement_and_test _gasneti_atomic_decrement_and_test_64

    GASNETI_INLINE(_gasneti_atomic_compare_and_swap)
    int _gasneti_atomic_compare_and_swap(gasneti_atomic_t *p, long oldval, long newval) {
      long result;
      gasneti_atomic_presync();
      result = _amo_acswap(p, oldval, newval);
      gasneti_atomic_postsync();
      return (result == oldval); 
    }
    #define GASNETI_HAVE_ATOMIC_CAS 1

    GASNETI_INLINE(gasneti_atomic_addfetch_64)
    uint32_t gasneti_atomic_addfetch_64(gasneti_atomic_t *p, int32_t op) {
       uint32_t retval;
       gasneti_atomic_presync();
       retval = _amo_afadd((p),(long)(op));
       gasneti_atomic_postsync();
       return retval;
    }
    #define _gasneti_atomic_addfetch gasneti_atomic_addfetch_32

       /* Both the instrisics and our asm lack built-in fences.  So, using default fences */
    #define GASNETI_ATOMIC_FENCE_RMW	GASNETI_ATOMIC_MB_POST
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

    #define _gasneti_atomic_addfetch(p,op) atomic_add4(&((p)->ctr),op)
   #else
    #define _gasneti_atomic_increment(p) (muadd(&((p)->ctr),1))
    #define _gasneti_atomic_decrement(p) (muadd(&((p)->ctr),-1))
    #define _gasneti_atomic_read(p)      (muget(&((p)->ctr)))
    #define _gasneti_atomic_set(p,v)     (muset(&((p)->ctr),(v)))
    #define _gasneti_atomic_init(v)      { (v) }
    #define _gasneti_atomic_decrement_and_test(p) \
                                        (muadd(&((p)->ctr),-1) == 0)

    #define _gasneti_atomic_addfetch(p,op) muadd(&((p)->ctr),op)
    /* Using default fences (TODO: VERIFY THAT WE NEED THEM) */
   #endif
  /* ------------------------------------------------------------------------------------ */
  /* PowerPPC ids:
   * AIX: _POWER
   * Darwin: __ppc__ or __ppc64__
   * Linux: __PPC__
   */
  #elif defined(_POWER) || defined(__PPC__) || defined(__ppc__) || defined(__ppc64__)
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

      static int32_t gasneti_atomic_addandfetch_32(int32_t volatile *v, int32_t op);
      #pragma mc_func gasneti_atomic_addandfetch_32 {\
	/* ARGS: r3 = v  LOCAL: r4 = op, r5 = tmp */ \
	"7ca01828"	/* 0: lwarx	r5,0,r3		*/ \
	"7ca52214"	/*    add	r5,r5,r4	*/ \
	"7ca0192d"	/*    stwcx.	r5,0,r3		*/ \
	"40a2fff4"	/*    bne-	0b		*/ \
	"7ca32b78"	/*    mr	r3,r5		*/ \
	/* RETURN in r3 = result after addition */ \
      }
      #pragma reg_killed_by gasneti_atomic_addandfetch_32 cr0, gr5
      #define _gasneti_atomic_addfetch(p,op) gasneti_atomic_addandfetch_32(&((p)->ctr),op)

      /* Using default fences as we have none in our asms */
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

      #define _gasneti_atomic_addfetch(p,op) gasneti_atomic_addandfetch_32(&((p)->ctr),op)

      /* Using default fences as we have none in our asms */
    #else
      #error Unrecognized PowerPC - need to implement GASNet atomics (or #define GASNETI_USE_GENERIC_ATOMICOPS)
    #endif
  #elif defined(__mips__) || defined(__mips) || defined(mips) || defined(_MIPS_ISA)
    #if defined(__GNUC__)
      typedef struct { volatile int ctr; } gasneti_atomic_t;
      #define _gasneti_atomic_read(p)      ((p)->ctr)
      #define _gasneti_atomic_init(v)      { (v) }
      #define _gasneti_atomic_set(p,v)     ((p)->ctr = (v))
      GASNETI_INLINE(_gasneti_atomic_increment_32)
      void _gasneti_atomic_increment_32(gasneti_atomic_t *p) {
	uint32_t tmp;
	__asm__ __volatile__(
		"1:		\n\t"
		"ll	%0,%1	\n\t"
		"addu	%0,1	\n\t"
		"sc	%0,%1	\n\t"
		"beqz	%0,1b	"
		: "=&r" (tmp), "=m" (p->ctr)
		: "m" (p->ctr)
		: "memory");
      } 
      #define _gasneti_atomic_increment _gasneti_atomic_increment_32
      GASNETI_INLINE(_gasneti_atomic_decrement_32)
      void _gasneti_atomic_decrement_32(gasneti_atomic_t *p) {
	uint32_t tmp;
	__asm__ __volatile__(
		"1:		\n\t"
		"ll	%0,%1	\n\t"
		"subu	%0,1 	\n\t"
		"sc	%0,%1 	\n\t"
		"beqz	%0,1b	"
		: "=&r" (tmp), "=m" (p->ctr)
		: "m" (p->ctr)
		: "memory");
      }
      #define _gasneti_atomic_decrement _gasneti_atomic_decrement_32
      GASNETI_INLINE(gasneti_atomic_fetchandadd_32)
      uint32_t gasneti_atomic_fetchandadd_32(gasneti_atomic_t *p, int32_t op) {
	uint32_t tmp, retval;
	__asm__ __volatile__(
		"1:			\n\t"
		"ll	%0,%2		\n\t"
		"addu	%1,%0,%3	\n\t"
		"sc	%1,%2		\n\t"
		"beqz	%1,1b		"
		: "=&r" (retval), "=&r" (tmp), "=m" (p->ctr)
		: "Ir" (op), "m" (p->ctr)
		: "memory");
	return retval;
      }
      #define _gasneti_atomic_decrement_and_test(p) (gasneti_atomic_fetchandadd_32((p),-1) == 1)

      GASNETI_INLINE(_gasneti_atomic_compare_and_swap)
      int _gasneti_atomic_compare_and_swap(gasneti_atomic_t *p, uint32_t oldval, uint32_t newval) {
         uint32_t temp;
         int retval;
         __asm__ __volatile__ (
		"1:			\n\t"
		"ll	%1,%5		\n\t"	/* Load from *p */
		"move	%0,$0		\n\t"	/* Assume mismatch */
		"bne	%1,%3,2f	\n\t"	/* Break loop on mismatch */
		"move	%0,%4		\n\t"	/* Move newval to retval */
		"sc	%0,%2		\n\t"	/* Try SC to store retval */
		"beqz	%0,1b		\n"	/* Retry on contention */
		"2:			"
                : "=&r" (retval), "=&r" (temp), "=m" (p->ctr)
                : "r" (oldval), "r" (newval), "m" (p->ctr)
                : "memory");
        return retval;
      }
      #define GASNETI_HAVE_ATOMIC_CAS 1

      #define _gasneti_atomic_fetchadd gasneti_atomic_fetchandadd_32

      /* No memory fences in our asm, so using default fences */
    #endif
  #else
    #error Unrecognized platform - need to implement GASNet atomics (or #define GASNETI_USE_GENERIC_ATOMICOPS)
  #endif
#endif

/* ------------------------------------------------------------------------------------ */
/* Wrappers for "special" atomics, if any */
#ifdef GASNETI_ATOMIC_READ_BODY
  GASNETI_EXTERNC void _gasneti_special_atomic_read(void);
  #define _gasneti_atomic_read \
	(*(uint32_t (*)(gasneti_atomic_t *p))(&_gasneti_special_atomic_read))
#endif
#ifdef GASNETI_ATOMIC_SET_BODY
  GASNETI_EXTERNC void _gasneti_special_atomic_set(void);
  #define _gasneti_atomic_set \
	(*(void (*)(gasneti_atomic_t *p, uint32_t))(&_gasneti_special_atomic_increment))
#endif
#ifdef GASNETI_ATOMIC_INCREMENT_BODY
  GASNETI_EXTERNC void _gasneti_special_atomic_increment(void);
  #define _gasneti_atomic_increment \
	(*(void (*)(gasneti_atomic_t *p))(&_gasneti_special_atomic_increment))
#endif
#ifdef GASNETI_ATOMIC_DECREMENT_BODY
  GASNETI_EXTERNC void _gasneti_special_atomic_decrement(void);
  #define _gasneti_atomic_decrement \
	(*(void (*)(gasneti_atomic_t *p))(&_gasneti_special_atomic_decrement))
#endif
#ifdef GASNETI_ATOMIC_DECREMENT_AND_TEST_BODY
  GASNETI_EXTERNC void _gasneti_special_atomic_decrement_and_test(void);
  #define _gasneti_atomic_decrement_and_test \
	(*(int (*)(gasneti_atomic_t *p))(&_gasneti_special_atomic_decrement_and_test))
#endif
#ifdef GASNETI_ATOMIC_COMPARE_AND_SWAP_BODY
  GASNETI_EXTERNC void _gasneti_special_atomic_compare_and_swap(void);
  #define _gasneti_atomic_compare_and_swap \
	(*(int (*)(gasneti_atomic_t *, uint32_t, uint32_t))(&_gasneti_special_atomic_compare_and_swap))
  #ifndef GASNETI_HAVE_ATOMIC_CAS
    #error GASNETI_ATOMIC_COMPARE_AND_SWAP_BODY defined when GASNETI_HAVE_ATOMIC_CAS is not.
  #endif
#endif
#ifdef GASNETI_ATOMIC_ADD_BODY
  GASNETI_EXTERNC void _gasneti_special_atomic_add(void);
  #define _gasneti_atomic_add \
    (*(uint32_t (*)(gasneti_atomic_t *, uint32_t))(&_gasneti_special_atomic_add))
  #ifndef GASNETI_HAVE_ATOMIC_ADD_SUB
    #error GASNETI_ATOMIC_ADD_BODY defined when GASNETI_HAVE_ATOMIC_ADD_SUB is not.
  #endif
#endif
#ifdef GASNETI_ATOMIC_SUBTRACT_BODY
  GASNETI_EXTERNC void _gasneti_special_atomic_subtract(void);
  #define _gasneti_atomic_subtract \
    (*(uint32_t (*)(gasneti_atomic_t *, uint32_t))(&_gasneti_special_atomic_subtract))
  #ifndef GASNETI_HAVE_ATOMIC_ADD_SUB
    #error GASNETI_ATOMIC_SUBTACT_BODY defined when GASNETI_HAVE_ATOMIC_ADD_SUB is not.
  #endif
#endif
#ifdef GASNETI_ATOMIC_FETCHADD_BODY
  GASNETI_EXTERNC void _gasneti_special_atomic_fetchadd(void);
  #define _gasneti_atomic_fetchadd \
    (*(uint32_t (*)(gasneti_atomic_t *, uint32_t))(&_gasneti_special_atomic_fetchadd))
#endif
#ifdef GASNETI_ATOMIC_ADDFETCH_BODY
  GASNETI_EXTERNC void _gasneti_special_atomic_addfetch(void);
  #define _gasneti_atomic_addfetch \
    (*(uint32_t (*)(gasneti_atomic_t *, uint32_t))(&_gasneti_special_atomic_addfetch))
#endif

/* ------------------------------------------------------------------------------------ */
/* Default add and subtract atomics in terms of addfetch, fetachadd or compare-and-swap
 *
 * NOTE: _gasneti_atomic_{add,subtract} are only called w/ args free of side-effects.
 * So, these macros can safely expand the arguments multiple times. 
 */

#if defined (GASNETI_HAVE_ATOMIC_ADD_SUB)
  /* Have a platform-specific version */
#elif defined(_gasneti_atomic_addfetch)
  #define _gasneti_atomic_add(p,op)      ((uint32_t)_gasneti_atomic_addfetch(p,op))
  #define _gasneti_atomic_subtract(p,op) ((uint32_t)_gasneti_atomic_addfetch(p,-op))
  #define GASNETI_HAVE_ATOMIC_ADD_SUB 	1
#elif defined(_gasneti_atomic_fetchadd)
  #define _gasneti_atomic_add(p,op)      ((uint32_t)(_gasneti_atomic_fetchadd(p,op) + op))
  #define _gasneti_atomic_subtract(p,op) ((uint32_t)(_gasneti_atomic_fetchadd(p,-op) - op))
  #define GASNETI_HAVE_ATOMIC_ADD_SUB 	1
#elif defined(GASNETI_HAVE_ATOMIC_CAS)
  GASNETI_INLINE(_gasneti_atomic_addfetch)
  uint32_t _gasneti_atomic_addfetch(gasneti_atomic_t *p, int32_t op) {
    uint32_t _old, _new;
    do {
      _new = (_old = _gasneti_atomic_read(p)) + op;
    } while (!_gasneti_atomic_compare_and_swap(p, _old, _new));
    return _new;
  }
  #define _gasneti_atomic_add(p,op)      _gasneti_atomic_addfetch(p,op)
  #define _gasneti_atomic_subtract(p,op) _gasneti_atomic_addfetch(p,-op)
  #define GASNETI_HAVE_ATOMIC_ADD_SUB 	1
#endif

/* ------------------------------------------------------------------------------------ */
/* Slow function-call based atomics
 * Used at client compile time for any compiler w/o inline asm support
 */

#if defined(GASNETI_USING_SLOW_ATOMICS)
  GASNETI_EXTERNC uint32_t gasneti_slow_atomic_read(gasneti_atomic_t *p, const int flags);
  #define gasneti_atomic_read gasneti_slow_atomic_read
  GASNETI_EXTERNC void gasneti_slow_atomic_set(gasneti_atomic_t *p, uint32_t v, const int flags);
  #define gasneti_atomic_set gasneti_slow_atomic_set
  GASNETI_EXTERNC void gasneti_slow_atomic_increment(gasneti_atomic_t *p, const int flags);
  #define gasneti_atomic_increment gasneti_slow_atomic_increment
  GASNETI_EXTERNC void gasneti_slow_atomic_decrement(gasneti_atomic_t *p, const int flags);
  #define gasneti_atomic_decrement gasneti_slow_atomic_decrement
  GASNETI_EXTERNC int gasneti_slow_atomic_decrement_and_test(gasneti_atomic_t *p, const int flags);
  #define gasneti_atomic_decrement_and_test gasneti_slow_atomic_decrement_and_test
  #if defined(GASNETI_HAVE_ATOMIC_CAS)
    GASNETI_EXTERNC int gasneti_slow_atomic_compare_and_swap(gasneti_atomic_t *p, uint32_t oldval, uint32_t newval, const int flags);
    #define gasneti_atomic_compare_and_swap gasneti_slow_atomic_compare_and_swap
  #endif
  #if defined(GASNETI_HAVE_ATOMIC_ADD_SUB)
    GASNETI_EXTERNC uint32_t gasneti_slow_atomic_add(gasneti_atomic_t *p, uint32_t op, const int flags);
    #define gasneti_atomic_add gasneti_slow_atomic_add
    GASNETI_EXTERNC uint32_t gasneti_slow_atomic_subtract(gasneti_atomic_t *p, uint32_t op, const int flags);
    #define gasneti_atomic_subtract gasneti_slow_atomic_subtract
  #endif
#endif

/* ------------------------------------------------------------------------------------ */

#if defined(GASNETI_USE_GENERIC_ATOMICOPS)
  #define GASNETI_ATOMIC_CONFIG   atomics_mutex
#elif defined(GASNETI_USE_OS_ATOMICOPS)
  #define GASNETI_ATOMIC_CONFIG   atomics_os
#else
  #define GASNETI_ATOMIC_CONFIG   atomics_native
#endif

/* ------------------------------------------------------------------------------------ */
/* Uniform memory fences for GASNet atomics.
 */

/* The following groups of preprocessor directives are designed to perform
 * as much elimination of unreachable code as possible at preprocess time.
 * While much could be done more naturally (and with far less code) by the
 * compiler, there are 3 major reasons we want to go to this trouble:
 * 1) The inliner for gcc (and probably most other compilers) applies some
 *    heuristics and limits when deciding which functions to inline.  Since
 *    these decisions are typically(?) made based on the "size" of the
 *    candidate function *before* dead code can be eliminated.  Therefore,
 *    any possible reduction in the size of the inline atomic functions is
 *    desirable.
 * 2) The "annotations" of memory fence properties are known to us, but
 *    are not always apparent to the optimizer.  For instance when the mb()
 *    and rmb() are the same, the following can (and will be) simplified by
 *    our preprocessor logic:
 *      if ((f & (R_flag|W_flag)) == (R_flag|W_flag)) mb();
 *      else if (f & R_flag) rmb();
 *      else if (f & W_flag) wmb();
 *    can become
 *	if (f & R_flag) rmb();
 *      else if (f & W_flag) wmb();
 *    while we are going to conservatively assume the compilers optimizer
 *    will not, either because it can't tell that mb() and rmb() are
 *    equal, or because merging the two conditionals is a non-obvious
 *    transformation.
 * 3) We are going to assume the least we can about the optimizer, giving
 *    it the simplest code possible for the final compile-time removal of
 *    dead code, even when the transformation are more obvious than the
 *    example in #2.
 *    
 * There are two levels of information available to us to perform our
 * transformations.  The first is the memory fence properties, which allow
 * us to make simplifications like the example in (2), above.  The macros
 * resulting from this level of macros provide a fence implementation
 * which is applicable to both the normal and weak atomics.  The second
 * level of information is the memory fenceing side-effects of the atomic
 * ops, and is applicable to the non-weak atomics only.
 *
 * The following preprocessor code is lengthy, and divided in to three
 * distinct parts for clarity:
 * Part 1.  Performs the simplifications based on membar properties.
 * Part 2.  Defines weakatomic fence macros in terms of the macros of Part 1.
 * Part 3.  Defines atomic fence macros in terms of the macros of Part 1,
 *          while applying simplications based on atomic side-effects.
 * Both Parts 2 and 3 can be overridden by platform-specific definitions
 * of their respective macros.
 */


/* Part 1.  Removal of fences which are empty/redundant on a given platform
 *	_gasneti_atomic_{mb,rmb,wmb}_{before,after}(flags)
 *	_gasneti_atomic_fence_after_bool(flags, value)
 *
 * This level of macros serves to remove at, preprocess-time, any tests for
 * memory fences that are no-ops on a given platform, or which are redundant
 * due to the relationships among fences.  For example, on a platform with
 * a single fence instruction that is mb(), rmb() and wmb() these macros
 * will reduce from three conditionals to just one.
 */

/* Part 1A. Tests for full mb() requested before/after */
#if GASNETI_MB_IS_EMPTY || (GASNETI_MB_IS_SUM && !(GASNETI_RMB_IS_EMPTY || GASNETI_WMB_IS_EMPTY))
  /* (1Ai)
   * + MB_IS_EMPTY
   *   Sequentially consistent, so no check for a full mb() is needed.
   * + (MB_IS_SUM && !(RMB_IS_EMPTY || WMB_IS_EMPTY))
   *   Since mb() == rmb()+wmb(), and both are non-empty, the rmb() and wmb() checks are
   *   sufficient to implement a request for mb(), rmb() or wmb() in just two tests.
   */
  #define _gasneti_atomic_mb_before(f)	/* nothing */
  #define _gasneti_atomic_mb_after(f)	/* nothing */
#elif GASNETI_RMB_IS_MB && GASNETI_WMB_IS_MB
  /* (1Aii)
   * + RMB_IS_MB and WMB_IS_MB
   *   Since mb(), rmb() and wmb() are all the same, a single test for either the
   *   RMB or WMB bits is sufficient for all three.
   */
  #define _gasneti_atomic_mb_before(f)	if (f & GASNETI_ATOMIC_MB_PRE) gasneti_local_mb();
  #define _gasneti_atomic_mb_after(f)	if (f & GASNETI_ATOMIC_MB_POST) gasneti_local_mb();
#elif  GASNETI_RMB_IS_MB
  /* (1Aiii)
   * + RMB_IS_MB (and !WMB_IS_MB)
   *   A single test for the RMB bit is sufficient for both mb() and rmb()
   */
  #define _gasneti_atomic_mb_before(f)	if (f & GASNETI_ATOMIC_RMB_PRE) gasneti_local_rmb();
  #define _gasneti_atomic_mb_after(f)	if (f & GASNETI_ATOMIC_RMB_POST) gasneti_local_rmb();
#elif  GASNETI_WMB_IS_MB
  /* (1Aiv)
   * + WMB_IS_MB (and !RMB_IS_MB)
   *   A single test for the WMB bit is sufficient for both mb() and wmb()
   */
  #define _gasneti_atomic_mb_before(f)	if (f & GASNETI_ATOMIC_WMB_PRE) gasneti_local_wmb();
  #define _gasneti_atomic_mb_after(f)	if (f & GASNETI_ATOMIC_WMB_POST) gasneti_local_wmb();
#else
  /* (1Av)
   * + Default
   *   The full mb() is non-empty, nor is it equal to rmb(), wmb() or their sum.
   *   Therefore we test for presence of both RMB and WMB bits to trigger a mb().
   */
  #define _gasneti_atomic_mb_before(f)	if ((f & GASNETI_ATOMIC_MB_PRE) == GASNETI_ATOMIC_MB_PRE) gasneti_local_mb();
  #define _gasneti_atomic_mb_after(f)	if ((f & GASNETI_ATOMIC_MB_POST) == GASNETI_ATOMIC_MB_POST) gasneti_local_mb();
#endif

/* Part 1B. Tests for rmb() requested before/after */
#if GASNETI_MB_IS_SUM && !(GASNETI_RMB_IS_EMPTY || GASNETI_WMB_IS_EMPTY)
  /* (1Bi)
   * + MB_IS_SUM && !(RMB_IS_EMPTY || WMB_IS_EMPTY) [1Ai (2nd half)]
   *   Full mb() == rmb()+wmb(), so rmb() when RMB bit is set.
   */
  #define _gasneti_atomic_rmb_before(f)	if (f & GASNETI_ATOMIC_RMB_PRE) gasneti_local_rmb();
  #define _gasneti_atomic_rmb_after(f)	if (f & GASNETI_ATOMIC_RMB_POST) gasneti_local_rmb();
#elif GASNETI_RMB_IS_MB || GASNETI_RMB_IS_EMPTY
  /* (1Bii)
   * + RMB_IS_MB [1Aii or 1Aiii]
   *   rmb() == mb(), which was already caught in the mb() tests.
   * + RMB_IS_EMPTY [1Ai (1st half), 1Aiv or 1Av]
   *   No need to test
   */
  #define _gasneti_atomic_rmb_before(f)	/* nothing */
  #define _gasneti_atomic_rmb_after(f)	/* nothing */
#else
  /* (1Biii)
   * + Default [1Aiv or 1Av]
   *   mb/rmb/wmb are distinct, so trigger rmb() if RMB bit is set and WMB is NOT.
   *   The 'else' here follows the 'if' of (1Aiv) or (1Av), to handle the "WMB is NOT".
   */
  #define _gasneti_atomic_rmb_before(f)	else if (f & GASNETI_ATOMIC_RMB_PRE) gasneti_local_rmb();
  #define _gasneti_atomic_rmb_after(f)	else if (f & GASNETI_ATOMIC_RMB_POST) gasneti_local_rmb();
#endif

/* Part 1C. Tests for wmb() requested before/after */
#if GASNETI_MB_IS_SUM && !(GASNETI_RMB_IS_EMPTY || GASNETI_WMB_IS_EMPTY)
  /* (1Ci)
   * + analagous to 1Bi
   */
  #define _gasneti_atomic_wmb_before(f)	if (f & GASNETI_ATOMIC_WMB_PRE) gasneti_local_wmb();
  #define _gasneti_atomic_wmb_after(f)	if (f & GASNETI_ATOMIC_WMB_POST) gasneti_local_wmb();
#elif GASNETI_WMB_IS_MB || GASNETI_WMB_IS_EMPTY
  /* (1Cii)
   * + analagous to 1Bii
   */
  #define _gasneti_atomic_wmb_before(f)	/* nothing */
  #define _gasneti_atomic_wmb_after(f)	/* nothing */
#else
  /* (1Ciii)
   * + analagous to 1Biii
   */
  #define _gasneti_atomic_wmb_before(f)	else if (f & GASNETI_ATOMIC_WMB_PRE) gasneti_local_wmb();
  #define _gasneti_atomic_wmb_after(f)	else if (f & GASNETI_ATOMIC_WMB_POST) gasneti_local_wmb();
#endif

/* Part 1D. Tests for conditional rmb() after a boolean op */
#if GASNETI_RMB_IS_EMPTY
  /* (1Di)
   * + No test needed if RMB is empty
   */
  #define _gasneti_atomic_rmb_bool(f, v)	/* nothing */
#else
  /* (1Dii)
   *
   * Several optimizations are possible when a conditional rmb() is combined
   * with an unconditional POST fence.  Such optimizations would prevent
   * imposing a "double" mb() in shuch cases.  However: 
   * 1) There are no current callers that mix *MB_POST with a
   *    conditional RMB_POST_IF*, and no likely reason to.
   * 2) Though they all reduce a great deal at compile-time,
   *    such "optimizations" look very large to the inliner
   *    before any dead code can be eliminated.
   * Therefore, they are not currently implemented.
   */
  #define _gasneti_atomic_rmb_bool(f, v) \
    if (((f & GASNETI_ATOMIC_RMB_POST_IF_TRUE ) &&  v) || \
        ((f & GASNETI_ATOMIC_RMB_POST_IF_FALSE) && !v)) gasneti_local_rmb();
#endif


/* Part 2.  Convienience macros for weakatomics
 *	_gasneti_weakatomic_fence_{before,after}(flags)
 *	_gasneti_weakatomic_fence_after_bool(flags, value)
 *
 * These are defined for readability, and are defined unconditionally,
 * because presently there are no fencing side-effects for the weak
 * atomic code.
 * One could implement GASNETI_WEAKATOMIC_FENCE_{SET,READ,RMW} and
 * replicate the logic in Part 3, below, if this were to ever become
 * necessary.
 */
#if defined(_gasneti_weakatomic_fence_before) && \
    defined(_gasneti_weakatomic_fence_after)  && \
    defined(_gasneti_weakatomic_fence_after_bool)
  /* Use platform-specific definitions */
#elif defined(_gasneti_weakatomic_fence_before) || \
      defined(_gasneti_weakatomic_fence_after)  || \
      defined(_gasneti_weakatomic_fence_after_bool)
  #error "A platform must define either ALL or NONE of _gasneti_weakatomic_fence_{before,after,after_bool}"
#else
  #define _gasneti_weakatomic_fence_before(f)	_gasneti_atomic_mb_before(f)  \
						_gasneti_atomic_rmb_before(f) \
						_gasneti_atomic_wmb_before(f)
  #define _gasneti_weakatomic_fence_after(f)	_gasneti_atomic_mb_after(f)  \
						_gasneti_atomic_rmb_after(f) \
						_gasneti_atomic_wmb_after(f)
  #define _gasneti_weakatomic_fence_after_bool	_gasneti_atomic_rmb_bool
#endif


/* Part 3.  Removal of fences which are redundant before/after atomic ops.
 *	_gasneti_atomic_fence_{before,after}_{set,read,rmb}(flags)
 *	_gasneti_atomic_fence_after_bool(flags, value)
 *
 * This level of macros serves to remove at, preprocess-time, any tests
 * that correspond to memory fences that are known to be side-effects
 * of the atomic operations, as determined by the bits of the masks
 * GASNETI_ATOMIC_FENCE_{SET,READ,RMW}.
 */

/* Part 3A.  Default masks
 *	GASNETI_ATOMIC_FENCE_{SET,READ,RMW}
 * 
 * If the per-platform atomics code has left any of these unset, then
 * they default to GASNETI_ATOMIC_NONE (0).
 */
#ifndef GASNETI_ATOMIC_FENCE_SET
  #define GASNETI_ATOMIC_FENCE_SET	GASNETI_ATOMIC_NONE
#endif
#ifndef GASNETI_ATOMIC_FENCE_READ
  #define GASNETI_ATOMIC_FENCE_READ	GASNETI_ATOMIC_NONE
#endif
#ifndef GASNETI_ATOMIC_FENCE_RMW
  #define GASNETI_ATOMIC_FENCE_RMW	GASNETI_ATOMIC_NONE
#endif

/* Part 3B.  Compile away tests for fences that are side-effects of Set */
#if defined(_gasneti_atomic_fence_before_set)
  /* Use platform-specific definitions */
#elif (GASNETI_ATOMIC_FENCE_SET & GASNETI_ATOMIC_MB_PRE) == GASNETI_ATOMIC_MB_PRE
  #define _gasneti_atomic_fence_before_set(f)	/* nothing */
#elif (GASNETI_ATOMIC_FENCE_SET & GASNETI_ATOMIC_RMB_PRE)
  #define _gasneti_atomic_fence_before_set(f)	_gasneti_atomic_mb_before(f)  \
						_gasneti_atomic_wmb_before(f)
#elif (GASNETI_ATOMIC_FENCE_SET & GASNETI_ATOMIC_WMB_PRE)
  #define _gasneti_atomic_fence_before_set(f)	_gasneti_atomic_mb_before(f)  \
						_gasneti_atomic_rmb_before(f)
 #else
  #define _gasneti_atomic_fence_before_set(f)	_gasneti_atomic_mb_before(f)  \
						_gasneti_atomic_rmb_before(f) \
						_gasneti_atomic_wmb_before(f)
#endif
#if defined(_gasneti_atomic_fence_after_set)
  /* Use platform-specific definitions */
#elif (GASNETI_ATOMIC_FENCE_SET & GASNETI_ATOMIC_MB_POST) == GASNETI_ATOMIC_MB_POST
  #define _gasneti_atomic_fence_after_set(f)	/* nothing */
#elif (GASNETI_ATOMIC_FENCE_SET & GASNETI_ATOMIC_RMB_POST)
  #define _gasneti_atomic_fence_after_set(f)	_gasneti_atomic_mb_after(f)  \
						_gasneti_atomic_wmb_after(f)
#elif (GASNETI_ATOMIC_FENCE_SET & GASNETI_ATOMIC_WMB_POST)
  #define _gasneti_atomic_fence_after_set(f)	_gasneti_atomic_mb_after(f)  \
						_gasneti_atomic_rmb_after(f)
#else
  #define _gasneti_atomic_fence_after_set(f)	_gasneti_atomic_mb_after(f)  \
						_gasneti_atomic_rmb_after(f) \
						_gasneti_atomic_wmb_after(f)
#endif

/* Part 3C.  Compile away tests for fences that are side-effects of Read */
#if defined(_gasneti_atomic_fence_before_read)
  /* Use platform-specific definitions */
#elif (GASNETI_ATOMIC_FENCE_READ & GASNETI_ATOMIC_MB_PRE) == GASNETI_ATOMIC_MB_PRE
  #define _gasneti_atomic_fence_before_read(f)	/* nothing */
#elif (GASNETI_ATOMIC_FENCE_READ & GASNETI_ATOMIC_RMB_PRE)
  #define _gasneti_atomic_fence_before_read(f)	_gasneti_atomic_mb_before(f)  \
						_gasneti_atomic_wmb_before(f)
#elif (GASNETI_ATOMIC_FENCE_READ & GASNETI_ATOMIC_WMB_PRE)
  #define _gasneti_atomic_fence_before_read(f)	_gasneti_atomic_mb_before(f)  \
						_gasneti_atomic_rmb_before(f)
#else
  #define _gasneti_atomic_fence_before_read(f)	_gasneti_atomic_mb_before(f)  \
						_gasneti_atomic_rmb_before(f) \
						_gasneti_atomic_wmb_before(f)
#endif
#if defined(_gasneti_atomic_fence_after_read)
  /* Use platform-specific definitions */
#elif (GASNETI_ATOMIC_FENCE_READ & GASNETI_ATOMIC_MB_POST) == GASNETI_ATOMIC_MB_POST
  #define _gasneti_atomic_fence_after_read(f)	/* nothing */
#elif (GASNETI_ATOMIC_FENCE_READ & GASNETI_ATOMIC_RMB_POST)
  #define _gasneti_atomic_fence_after_read(f)	_gasneti_atomic_mb_after(f)  \
						_gasneti_atomic_wmb_after(f)
#elif (GASNETI_ATOMIC_FENCE_READ & GASNETI_ATOMIC_WMB_POST)
  #define _gasneti_atomic_fence_after_read(f)	_gasneti_atomic_mb_after(f)  \
						_gasneti_atomic_rmb_after(f)
#else
  #define _gasneti_atomic_fence_after_read(f)	_gasneti_atomic_mb_after(f)  \
						_gasneti_atomic_rmb_after(f) \
						_gasneti_atomic_wmb_after(f)
#endif

/* Part 3D.  Compile away tests for fences that are side-effects of Read-Modify-Write */
#if defined(_gasneti_atomic_fence_before_rmw)
  /* Use platform-specific definitions */
#elif (GASNETI_ATOMIC_FENCE_RMW & GASNETI_ATOMIC_MB_PRE) == GASNETI_ATOMIC_MB_PRE
  #define _gasneti_atomic_fence_before_rmw(f)	/* nothing */
#elif (GASNETI_ATOMIC_FENCE_RMW & GASNETI_ATOMIC_RMB_PRE)
  #define _gasneti_atomic_fence_before_rmw(f)	_gasneti_atomic_mb_before(f)  \
						_gasneti_atomic_wmb_before(f)
#elif (GASNETI_ATOMIC_FENCE_RMW & GASNETI_ATOMIC_WMB_PRE)
  #define _gasneti_atomic_fence_before_rmw(f)	_gasneti_atomic_mb_before(f)  \
						_gasneti_atomic_rmb_before(f)
#else
  #define _gasneti_atomic_fence_before_rmw(f)	_gasneti_atomic_mb_before(f)  \
						_gasneti_atomic_rmb_before(f) \
						_gasneti_atomic_wmb_before(f)
#endif
#if defined(_gasneti_atomic_fence_after_rmw) && defined(_gasneti_atomic_fence_after_bool)
  /* Use platform-specific definitions */
#elif defined(_gasneti_atomic_fence_after_rmw) || defined(_gasneti_atomic_fence_after_bool)
  #error "Platform must define BOTH or NEITHER of _gasneti_atomic_fence_after_{rwm,bool}"
#elif (GASNETI_ATOMIC_FENCE_RMW & GASNETI_ATOMIC_MB_POST) == GASNETI_ATOMIC_MB_POST
  #define _gasneti_atomic_fence_after_rmw(f)	/* nothing */
  #define _gasneti_atomic_fence_after_bool(f,v)	/* nothing */
#elif (GASNETI_ATOMIC_FENCE_RMW & GASNETI_ATOMIC_RMB_POST)
  #define _gasneti_atomic_fence_after_rmw(f)	_gasneti_atomic_mb_after(f)  \
						_gasneti_atomic_wmb_after(f)
  #define _gasneti_atomic_fence_after_bool(f,v)	_gasneti_atomic_mb_after(f)  \
						_gasneti_atomic_wmb_after(f)
#elif (GASNETI_ATOMIC_FENCE_RMW & GASNETI_ATOMIC_WMB_POST)
  #define _gasneti_atomic_fence_after_rmw(f)	_gasneti_atomic_mb_after(f)  \
						_gasneti_atomic_rmb_after(f)
  #define _gasneti_atomic_fence_after_bool(f,v)	_gasneti_atomic_mb_after(f)  \
						_gasneti_atomic_rmb_after(f) \
						_gasneti_atomic_rmb_bool(f,v)
#else
  #define _gasneti_atomic_fence_after_rmw(f)	_gasneti_atomic_mb_after(f)  \
						_gasneti_atomic_rmb_after(f) \
						_gasneti_atomic_wmb_after(f)
  #define _gasneti_atomic_fence_after_bool(f,v)	_gasneti_atomic_mb_after(f)  \
						_gasneti_atomic_rmb_after(f) \
						_gasneti_atomic_wmb_after(f) \
						_gasneti_atomic_rmb_bool(f,v)
#endif

/* ------------------------------------------------------------------------------------ */
/* GASNet atomic ops, using per-platform defns and the fencing macros of Part 3, above.
 */

#ifndef gasneti_atomic_init
  #define gasneti_atomic_init(v)	_gasneti_atomic_init(v)
#endif
#ifndef gasneti_atomic_set
  #define gasneti_atomic_set(p,v,f) do {                     \
    const int __flags = (f);                                 \
    _gasneti_atomic_fence_before_set(__flags)  /* no semi */ \
    _gasneti_atomic_set((p),(v));                            \
    _gasneti_atomic_fence_after_set(__flags)  /* no semi */  \
  } while (0)
#endif
#ifndef gasneti_atomic_read
  GASNETI_INLINE(gasneti_atomic_read)
  uint32_t gasneti_atomic_read(gasneti_atomic_t *p, const int flags) {
    _gasneti_atomic_fence_before_read(flags)  /* no semi */
    { const uint32_t retval = _gasneti_atomic_read(p);
      _gasneti_atomic_fence_after_read(flags)  /* no semi */
      return retval;
    }
  }
#endif
#ifndef gasneti_atomic_increment
  #define gasneti_atomic_increment(p,f) do {                 \
    const int __flags = (f);                                 \
    _gasneti_atomic_fence_before_rmw(__flags)  /* no semi */ \
    _gasneti_atomic_increment(p);                            \
    _gasneti_atomic_fence_after_rmw(__flags)  /* no semi */  \
  } while (0)
#endif
#ifndef gasneti_atomic_decrement
  #define gasneti_atomic_decrement(p,f) do {                 \
    const int __flags = (f);                                 \
    _gasneti_atomic_fence_before_rmw(__flags)  /* no semi */ \
    _gasneti_atomic_decrement(p);                            \
    _gasneti_atomic_fence_after_rmw(__flags)  /* no semi */  \
  } while (0)
#endif
#ifndef gasneti_atomic_decrement_and_test
  GASNETI_INLINE(gasneti_atomic_decrement_and_test)
  int gasneti_atomic_decrement_and_test(gasneti_atomic_t *p, const int flags) {
    _gasneti_atomic_fence_before_rmw(flags)  /* no semi */
    { const int retval = _gasneti_atomic_decrement_and_test(p);
      _gasneti_atomic_fence_after_bool(flags, retval) /* no semi */
      return retval;
    }
  }
#endif
#if defined(GASNETI_HAVE_ATOMIC_CAS) && !defined(gasneti_atomic_compare_and_swap)
  GASNETI_INLINE(gasneti_atomic_compare_and_swap)
  int gasneti_atomic_compare_and_swap(gasneti_atomic_t *p, uint32_t oldval, uint32_t newval, const int flags) {
    _gasneti_atomic_fence_before_rmw(flags)  /* no semi */
    { const int retval = _gasneti_atomic_compare_and_swap(p,oldval,newval);
      _gasneti_atomic_fence_after_bool(flags, retval) /* no semi */
      return retval;
    }
  }
#endif
#if defined(GASNETI_HAVE_ATOMIC_ADD_SUB)
  #ifndef gasneti_atomic_add
    GASNETI_INLINE(gasneti_atomic_add)
    uint32_t gasneti_atomic_add(gasneti_atomic_t *p, uint32_t op, const int flags) {
      gasneti_assert((int32_t)op >= 0); /* TODO: prohibit zero as well? */
      _gasneti_atomic_fence_before_rmw(flags)  /* no semi */
      { const uint32_t retval = _gasneti_atomic_add(p, op);
        _gasneti_atomic_fence_after_rmw(flags) /* no semi */
        return retval;
      }
    }
  #endif
  #ifndef gasneti_atomic_subtract
    GASNETI_INLINE(gasneti_atomic_subtract)
    uint32_t gasneti_atomic_subtract(gasneti_atomic_t *p, uint32_t op, const int flags) {
      gasneti_assert((int32_t)op >= 0); /* TODO: prohibit zero as well? */
      _gasneti_atomic_fence_before_rmw(flags)  /* no semi */
      { const uint32_t retval = _gasneti_atomic_subtract(p, op);
        _gasneti_atomic_fence_after_rmw(flags) /* no semi */
        return retval;
      }
    }
  #endif
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
  #ifdef GASNETI_HAVE_ATOMIC_ADD_SUB
    #define GASNETI_HAVE_WEAKATOMIC_ADD_SUB 1
    #define gasneti_weakatomic_add(p,op,f)            gasneti_atomic_add(p,op,f)
    #define gasneti_weakatomic_subtract(p,op,f)       gasneti_atomic_subtract(p,op,f)
  #endif
#else
  /* May not need any exclusion mechanism, but we still want to include any fences that
     the caller has requested, since any memory in the gasnet segment "protected" by a
     fenced atomic may be written by a network adapter.
   */
  typedef volatile int gasneti_weakatomic_t;
  #define gasneti_weakatomic_init(v)                  (v)
  #define gasneti_weakatomic_set(p,v,f) do {                 \
    const int __flags = (f);                                 \
    _gasneti_weakatomic_fence_before(__flags)  /* no semi */ \
    (*(p) = (v));                                            \
    _gasneti_weakatomic_fence_after(__flags)  /* no semi */  \
  } while (0)
  GASNETI_INLINE(gasneti_weakatomic_read)
  int gasneti_weakatomic_read(gasneti_weakatomic_t *p, const int flags) {
    _gasneti_weakatomic_fence_before(flags)  /* no semi */
    { const int retval = *(p);
      _gasneti_weakatomic_fence_after(flags)  /* no semi */
      return retval;
    }
  }
  #define gasneti_weakatomic_increment(p,f) do {             \
    const int __flags = (f);                                 \
    _gasneti_weakatomic_fence_before(__flags)  /* no semi */ \
    (*(p))++;                                                \
    _gasneti_weakatomic_fence_after(__flags)  /* no semi */  \
  } while (0)
  #define gasneti_weakatomic_decrement(p,f) do {             \
    const int __flags = (f);                                 \
    _gasneti_weakatomic_fence_before(__flags)  /* no semi */ \
    (*(p))--;                                                \
    _gasneti_weakatomic_fence_after(__flags)  /* no semi */  \
  } while (0)
  GASNETI_INLINE(gasneti_weakatomic_decrement_and_test)
  int gasneti_weakatomic_decrement_and_test(gasneti_weakatomic_t *p, const int flags) {
    _gasneti_weakatomic_fence_before(flags)  /* no semi */
    { const int retval = !(--(*p));
      _gasneti_weakatomic_fence_after(flags)  /* no semi */
      _gasneti_weakatomic_fence_after_bool(flags, retval)  /* no semi */
      return retval;
    }
  }
  #define GASNETI_HAVE_WEAKATOMIC_CAS 1
  GASNETI_INLINE(gasneti_weakatomic_compare_and_swap)
  int gasneti_weakatomic_compare_and_swap(gasneti_weakatomic_t *p, uint32_t oldval, uint32_t newval, const int flags) {
    _gasneti_weakatomic_fence_before(flags)  /* no semi */
    { const int retval = (((uint32_t)*p == oldval) ? (*p = newval, 1) : 0);
      _gasneti_weakatomic_fence_after(flags)  /* no semi */
      _gasneti_weakatomic_fence_after_bool(flags, retval)  /* no semi */
      return retval;
    }
  }
  #define GASNETI_HAVE_WEAKATOMIC_ADD_SUB 1
  GASNETI_INLINE(gasneti_weakatomic_add)
  uint32_t gasneti_weakatomic_add(gasneti_weakatomic_t *p, int32_t op, const int flags) {
    { const uint32_t retval = *(uint32_t *)(p) += (op);
      _gasneti_weakatomic_fence_after(flags)  /* no semi */
      return retval;
    }
  }
  GASNETI_INLINE(gasneti_weakatomic_subtract)
  uint32_t gasneti_weakatomic_subtract(gasneti_weakatomic_t *p, int32_t op, const int flags) {
    { const uint32_t retval = *(uint32_t *)(p) += (op);
      _gasneti_weakatomic_fence_after(flags)  /* no semi */
      return retval;
    }
  }
#endif

/* ------------------------------------------------------------------------------------ */
/* The following are NOT for use outside this file: */
#undef GASNETI_ATOMIC_FENCE_SET
#undef GASNETI_ATOMIC_FENCE_READ
#undef GASNETI_ATOMIC_FENCE_RMW

/* ------------------------------------------------------------------------------------ */
#endif

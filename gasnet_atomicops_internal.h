/*  $Archive:: /Ti/GASNet/gasnet_atomicops_internal.h                               $
 *     $Date: 2004/08/12 17:12:55 $
 * $Revision: 1.1.2.1 $
 * Description: GASNet header for semi-portable atomic memory operations
 * Copyright 2002, Dan Bonachea <bonachea@cs.berkeley.edu>
 * Terms of use are as specified in license.txt
 */

#ifndef _GASNET_ATOMICOPS_INTERNAL_H
#define _GASNET_ATOMICOPS_INTERNAL_H

#if !defined(_IN_GASNET_INTERNAL_H)
  #error This file is not meant to be included by clients
#endif

/* ------------------------------------------------------------------------------------ */
/* semi-portable atomic compare and swap
   This useful operation is not avaialble on all platforms and it therefore reserved 
   for interal use only.

   On platforms where it is implemented

     gasneti_atomic_compare_and_swap(p, oldval, newval)

   is the atomic equivalent of:

    if (*p == oldval) {
      *p = newval;
      return NONZERO;
    } else {
      return 0;
    }

    GASNETI_HAVE_ATOMIC_CAS will be defined non-zero on platforms supporting this operation.
    
TODO: We are inconsistent (when compared to other atomics) with respect to including any
required rmb() calls in these implementations.  Presently the Alpha and PPC (the only two
platforms with real rmb()s) include the rmb() in the CAS.  Ideally the callers should
insert them only where the operation has acquire semantics.
 */

#if defined(SOLARIS) || /* SPARC seems to have no atomic ops */ \
    defined(CRAYT3E) || /* TODO: no atomic ops on T3e? */       \
    defined(_SX) || /* NEC SX-6 atomics not available to user code? */ \
    defined(HPUX)    || /* HPUX seems to have no atomic ops */  \
    defined(__crayx1) || /* X1 atomics currently broken */ \
    (defined(__PGI) && defined(BROKEN_LINUX_ASM_ATOMIC_H)) || /* haven't implemented atomics for PGI */ \
    (defined(OSF) && !defined(__DECC) && !defined(__GNUC__)) /* only implemented for these compilers */
  #define GASNETI_USE_GENERIC_ATOMICOPS
#endif

#ifdef GASNETI_USE_GENERIC_ATOMICOPS
  /* a very slow but portable implementation of atomic ops */
  #ifdef _INCLUDED_GASNET_H
    extern int gasneti_atomic_compare_and_swap(gasneti_atomic_t *p, uint32_t oldval, uint32_t newval);
    #define GASNETI_GENERIC_CAS_DEF                              \
    int gasneti_atomic_compare_and_swap(gasneti_atomic_t *p,     \
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
  #elif defined(_REENTRANT) || defined(_THREAD_SAFE) || \
        defined(PTHREAD_MUTEX_INITIALIZER) ||           \
        defined(HAVE_PTHREAD) || defined(HAVE_PTHREAD_H)
    /* a version for pthreads which is independent of GASNet HSL's */
    GASNET_INLINE_MODIFIER(gasneti_atomic_compare_and_swap)
    int gasneti_atomic_compare_and_swap(gasneti_atomic_t *p, uint32_t oldval, uint32_t newval) {
      int retval;

      pthread_mutex_lock(&gasneti_atomicop_mutex);
      retval = (p->ctr == oldval);
      if_pt (retval) {
        p->ctr = newval;
      }
      pthread_mutex_unlock(&gasneti_atomicop_mutex);
      return retval;
    }
    #define GASNETI_HAVE_ATOMIC_CAS 1
  #else
    /* only one thread - everything atomic by definition */
    GASNET_INLINE_MODIFIER(gasneti_atomic_compare_and_swap)
    int gasneti_atomic_compare_and_swap(gasneti_atomic_t *p, uint32_t oldval, uint32_t newval) {
      int retval = (p->ctr == oldval);
      if_pt (retval) {
        p->ctr = newval;
      }
      return retval;
    }
    #define GASNETI_HAVE_ATOMIC_CAS 1
  #endif
#else
  #if defined(LINUX) && defined(__INTEL_COMPILER) && defined(__ia64__)
    /* Intel compiler's inline assembly broken on Itanium (bug 384) - use intrinsics instead */
    #define gasneti_atomic_compare_and_swap(p,oval,nval) \
			(_InterlockedCompareExchange((volatile int *)&((p)->ctr),nval,oval) == (oval))
    #define GASNETI_HAVE_ATOMIC_CAS 1
  #elif defined(LINUX)
    #if defined(BROKEN_LINUX_ASM_ATOMIC_H) || \
        (!defined(GASNETI_UNI_BUILD) && !defined(CONFIG_SMP))
      /* some versions of the linux kernel ship with a broken atomic.h
         this code based on a non-broken version of the header. 
         Also force using this code if this is a gasnet-smp build and the 
         linux/config.h settings disagree (due to system config problem or 
         cross-compiling on a uniprocessor frontend for smp nodes)
       */
      #if defined(__i386__) || defined(__x86_64__) /* x86 and Athlon/Opteron */
        GASNET_INLINE_MODIFIER(gasneti_atomic_compare_and_swap)
        int gasneti_atomic_compare_and_swap(gasneti_atomic_t *v, uint32_t oldval, uint32_t newval) {
          register unsigned char retval;
          register uint32_t readval;

          __asm__ __volatile__ (GASNETI_LOCK "cmpxchgl %3, %1; sete %0"
				    : "=q" (retval), "=m" (v->counter), "=a" (readval)
				    : "r" (newval), "m" (v->counter), "a" (oldval)
				    : "memory");
          return (int)retval;
        }
        #define GASNETI_HAVE_ATOMIC_CAS 1
      #elif defined(__ia64__)
        #define gasneti_atomic_compare_and_swap(p,oval,nval) (gasneti_cmpxchg(p,oval,nval) == (oval))
        #define GASNETI_HAVE_ATOMIC_CAS 1
      #endif
    #else
      #ifdef __alpha__
        /* work-around for a puzzling header bug in alpha Linux */
        #define extern static
      #endif
      #ifdef __cplusplus
        /* work around a really stupid C++ header bug observed in HP Linux */
        #define new new_
      #endif
      #include <asm/system.h>
      #ifdef __alpha__
        #undef extern
      #endif
      #ifdef __cplusplus
        #undef new
      #endif
      #ifdef cmpxchg
        #define gasneti_atomic_compare_and_swap(p,oval,nval) (cmpxchg(p,oval,nval) == (oval))
        #define GASNETI_HAVE_ATOMIC_CAS 1
      #endif
    #endif
  #elif defined(FREEBSD)
    /* FreeBSD is lacking atomic ops that return a value */
    #ifdef __i386__
      GASNET_INLINE_MODIFIER(gasneti_atomic_compare_and_swap)
      int gasneti_atomic_compare_and_swap(volatile uint32_t *ctr, uint32_t oldval, uint32_t newval) {
        register unsigned char c;
        register uint32_t readval;

        __asm__ __volatile__ (
		_STRINGIFY(MPLOCKED) "cmpxchgl %3, %1; sete %0"
		: "=qm" (c), "=m" (*ctr), "=a" (readval)
		: "r" (newval), "m" (*ctr), "a" (oldval) : "memory");
        return (int)c;
      }
      #define GASNETI_HAVE_ATOMIC_CAS 1
    #endif
  #elif defined(CYGWIN)
    #define gasneti_atomic_compare_and_swap(p,oval,nval) \
			(InterlockedCompareExchange((LONG *)&((p)->ctr),nval,oval) == (oval))
    #define GASNETI_HAVE_ATOMIC_CAS 1
  #elif defined(AIX)
    GASNET_INLINE_MODIFIER(gasneti_atomic_compare_and_swap)
    int gasneti_atomic_compare_and_swap(gasneti_atomic_t *p, int oldval, int newval) {
      int retval;

      retval = compare_and_swap( (atomic_p)p, &oldval, newval );
      if (retval)
        gasneti_local_rmb();
      return retval;
    } 
    #define GASNETI_HAVE_ATOMIC_CAS 1
  #elif defined(OSF)
   #ifdef __DECC
     /* OSF atomics are compiler built-ins */
     #define gasneti_atomic_compare_and_swap(p,oval,nval) do { \
	__CMP_STORE_LONG(&((p)->ctr),ocal,nval,&((p)->ctr));   \
	__MB();                                                \
     } while (0)
     #define GASNETI_HAVE_ATOMIC_CAS 1
   #elif defined(__GNUC__)
     GASNET_INLINE_MODIFIER(gasneti_atomic_compare_and_swap)
     int gasneti_atomic_compare_and_swap(gasneti_atomic_t *p, uint32_t oldval, uint32_t newval) {
       unsigned long ret;

       __asm__ __volatile__ (
		"1:	ldl_l	%0,%4\n"	/* Load-linked of current value */
		"	cmpeq	%0,%2,%0\n"	/* compare to oldval */
		"	beq	%0,2f\n"	/* done/fail on mismatch (success/fail in ret) */
		"	mov	%3,%0"		/* copy newval to ret */
		"	stl_c	%0,%1\n"	/* Store-conditional of newval (success/fail in ret) */
		"	beq	%0,1b\n"	/* Retry on stl_c failure */
		"2:	mb"			/* memory flush */
       		: "=&r"(ret), "=m"(*p)
		: "r"(oldval), "r"(newval)
		: "memory");

       return ret;
     }
   #endif
  #elif defined(IRIX)
    /* TODO: Can we supprt this platform? */
  #elif defined(__crayx1) /* This works on X1, but NOT the T3E */
    /* TODO: Can we supprt this platform? */
  #elif defined(_SX)
    /* TODO: Can we supprt this platform? */
  #elif 0 && defined(SOLARIS)
    /* $%*(! Solaris has atomic functions in the kernel but refuses to expose them
       to the user... after all, what application would be interested in performance? */
    /* TODO: Can we supprt this platform? */
  #elif defined(__APPLE__) && defined(__MACH__) && defined(__ppc__)
    #if defined(__xlC__)
      static int32_t gasneti_atomic_swap_not_32(volatile int32_t *v, int32_t oldval, int32_t newval);
      #pragma mc_func gasneti_atomic_swap_not_32 {\
	/* ARGS: r3 = p, r4=oldval, r5=newval   LOCAL: r2 = tmp */ \
	"7c401828"	/* 0: lwarx	r2,0,r3		*/ \
	"7c422279"	/*    xor.	r2,r2,r4	*/ \
	"40820010"	/*    bne	1f		*/ \
	"7ca0192d"	/*    stwcx.	r5,0,r3		*/ \
	"40a2fff0"	/*    bne-	0b		*/ \
	"4c00012c"	/*    isync			*/ \
	"7c431378"	/* 1: mr	r3,r2		*/ \
	/* RETURN in r3 = 0 iff swap took place */ \
      }
      #pragma reg_killed_by gasneti_atomic_swap_not_32
      #define gasneti_atomic_compare_and_swap(p, oldval, newval) \
	(gasneti_atomic_swap_not_32(&((p)->ctr),(oldval),(newval)) == 0)
      #define GASNETI_HAVE_ATOMIC_CAS 1
    #else
      GASNET_INLINE_MODIFIER(gasneti_atomic_compare_and_swap)
      int gasneti_atomic_compare_and_swap(gasneti_atomic_t *p, uint32_t oldval, uint32_t newval) {
        register uint32_t result;
        __asm__ __volatile__ (
	  "0:\t"
	  "lwarx    %0,0,%1 \n\t"         /* load to result */
	  "xor.     %0,%0,%2 \n\t"        /* xor result w/ oldval */
	  "bne      1f \n\t"              /* branch on mismatch */
	  "stwcx.   %3,0,%1 \n\t"         /* store newval */
	  "bne-     0b \n\t"              /* retry on conflict */
	  "isync\n"
	  "1:\t"
	  : "=&r"(result)
	  : "r" (p), "r"(oldval), "r"(newval)
	  : "cr0", "memory");
  
        return (result == 0);
      } 
      #define GASNETI_HAVE_ATOMIC_CAS 1
    #endif
  #endif
#endif
#ifndef GASNETI_HAVE_ATOMIC_CAS
  #define GASNETI_HAVE_ATOMIC_CAS 0
#endif

#endif

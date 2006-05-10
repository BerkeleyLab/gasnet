/*   $Source: /Users/kamil/work/gasnet-cvs2/gasnet/gasnet_atomicops.h,v $
 *     $Date: 2006/05/10 07:08:08 $
 * $Revision: 1.166.4.1 $
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
/* Portable atomic operations
   --------------------------
   These provide a special datatype (gasneti_atomic_t) representing an atomically
    updated unsigned integer value and a set of atomic ops
   Atomicity is guaranteed only if ALL accesses to the gasneti_atomic_t data happen
    through the provided operations (i.e. it is an error to directly access the
    contents of a gasneti_atomic_t), and if the gasneti_atomic_t data is only
    addressable by the current process (e.g. not in a System V shared memory segment)
   It is also an error to access an unintialized gasneti_atomic_t with any operation
    other than gasneti_atomic_set().
   We define an unsigned type (gasneti_atomic_val_t) and a signed type
   (gasneti_atomic_sval_t) and provide the following operations on all platforms:

    gasneti_atomic_init(gasneti_atomic_val_t v)
        Static initializer (macro) for an gasneti_atomic_t to value v.

    void gasneti_atomic_set(gasneti_atomic_t *p,
                            gasneti_atomic_val_t v,
                            int flags);
        Atomically sets *p to value v.

    gasneti_atomic_val_t gasneti_atomic_read(gasneti_atomic_t *p, int flags);
        Atomically read and return the value of *p.

    void gasneti_atomic_increment(gasneti_atomic_t *p, int flags);
        Atomically increment *p (no return value).

    void gasneti_atomic_decrement(gasneti_atomic_t *p, int flags);
        Atomically decrement *p (no return value).

    int gasneti_atomic_decrement_and_test(gasneti_atomic_t *p, int flags);
        Atomically decrement *p, return non-zero iff the new value is 0.


   Semi-portable atomic operations
   --------------------------------
   These useful operations are available on most, but not all, platforms.

    gasneti_atomic_val_t gasneti_atomic_add(gasneti_atomic_t *p,
                                            gasneti_atomic_val_t op,
                                            int flags);
    gasneti_atomic_val_t gasneti_atomic_subtract(gasneti_atomic_t *p,
                                                 gasneti_atomic_val_t op,
                                                 int flags);

     These implement atomic addition and subtraction, where op must be non-negative.
     The result is platform dependent if the value of op is negative or out of the
     range of gasneti_atomic_val_t, or if the resulting value is out of range.
     Both return the value after the addition or subtraction.

    GASNETI_HAVE_ATOMIC_ADD_SUB will be defined to 1 when these operations are available.
    They are always either both available, or neither is available.

    int gasneti_atomic_compare_and_swap(gasneti_atomic_t *p,
                                        gasneti_atomic_val_t oldval,
                                        gasneti_atomic_val_t newval,
                                        int flags);

     This operation is the atomic equivalent of:
      if (*p == oldval) {
        *p = newval;
        return NONZERO;
      } else {
        return 0;
      }

     GASNETI_HAVE_ATOMIC_CAS will be defined to 1 when this operation is available


   Range of atomic type
   --------------------
   Internally an atomic type is an unsigned type of at least 24-bits.  No special
   action is needed to store signed values via gasneti_atomic_set(), however because
   the type may use less than a full word, gasneti_atomic_signed() is provided to
   perform any required sign extension if a value read from a gasneti_atomic_t is
   to be used as a signed type.

    gasneti_atomic_signed(v)      Converts a gasneti_atomic_val_t returned by
                                  gasneti_atomic_{read,add,subtract} to a signed
                                  gasneti_atomic_sval_t.
    GASNETI_ATOMIC_MAX            The largest representable unsigned value
                                  (the smallest representable unsigned value is always 0).
    GASNETI_ATOMIC_SIGNED_MIN     The smallest (most negative) representable signed value.
    GASNETI_ATOMIC_SIGNED_MAX     The largest (most positive) representable signed value.

   The atomic type is guaranteed to wrap around at it's minimum and maximum values in
   the normal manner expected of two's-complement integers.  This includes the 'oldval'
   and 'newval' arguments to gasneti_atomic_compare_and_swap(), and the 'v' arguments
   to gasneti_atomic_init() and gasneti_atomic_set() which are wrapped (not clipped)
   to the proper range prior to assignment (for 'newval' and 'v') or comparison (for
   'oldval').


   Memory fence properties of atomic operations
   --------------------------------------------
   NOTE: Atomic operations have no default memory fence properties, as this
   varies by platform.  Every atomic operation except _init() includes a 'flags'
   argument to indicate the caller's minimum fence requirements.
   Depending on the platform, the implementation may use fences stronger than
   those requested, but never weaker.


   Storage of atomic type
   ----------------------
   Internally an atomic type may use storage significantly larger than the number
   of significant bits.  This additional space may be needed, for instance, to
   meet platform-specific alignment constraints, or to hold a mutex on platforms
   lacking any other means of ensuring atomicity.


   Signal safety of atomic operations
   ----------------------------------
   On most, but not all, platforms these atomic operations are signal safe.  On
   the few platforms where this is not the case GASNETI_ATOMICOPS_NOT_SIGNALSAFE
   will be defined to 1.

   Mutexes
   -------
   If GASNETI_USE_GENERIC_ATOMICOPS is defined, then the gasnet atomics are
   implemented using mutexes.  Therefore, one may wish to consider using other
   algorithms when this symbol is defined.



   Fixed-width types
   -----------------
   The following fixed-width (32- and 64-bit) types/operations are available
   on all platforms.  These are guaranteed to consume exactly the "natural"
   storage, without padding or any extra alignment.  However, one or both may
   use mutexes or lack signal-safety, even where gasneti_atomic_t does not.

    + gasneti_atomic32_t
    + gasneti_atomic64_t
        Typedef
    + gasneti_atomic32_init(uint32_t v)
    + gasneti_atomic64_init(uint64_t v)
        Static initializer (macro).
    + void gasneti_atomic32_set(gasneti_atomic32_t *p, uint32_t v, int flags);
    + void gasneti_atomic64_set(gasneti_atomic64_t *p, uint64_t v, int flags);
        Atomically sets *p to value v.
    + uint32_t gasneti_atomic32_read(gasneti_atomic32_t *p, int flags);
    + uint64_t gasneti_atomic64_read(gasneti_atomic64_t *p, int flags);
        Atomically read and return the value of *p.
    + int gasneti_atomic32_compare_and_swap(gasneti_atomic32_t *p, uint32_t oldval,
                                            uint32_t newval, int flags);
    + int gasneti_atomic64_compare_and_swap(gasneti_atomic64_t *p, uint64_t oldval,
                                            uint64_t newval, int flags);
        Atomic compare-and-swap of *p from oldval to newval.
    + GASNETI_USE_GENERIC_ATOMIC32
    + GASNETI_USE_GENERIC_ATOMIC64
        Defined IFF implementation uses mutexes.
        Note that these two are set independently.
    + GASNETI_ATOMIC32_NOT_SIGNALSAFE
    + GASNETI_ATOMIC64_NOT_SIGNALSAFE
        Defined IFF implementation is not signal-safe.
        Note that these two are set independently.

 */

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
/* All the platform-specific parts */
#include <gasnet_atomic_bits.h>

/* ------------------------------------------------------------------------------------ */
/* Typeless unfenced operations on a pointer to a (volatile) scalar */

#define _gasneti_scalar_atomic_init(v)               (v)
#define _gasneti_scalar_atomic_set(p,v)              (*(p) = (v))
#define _gasneti_scalar_atomic_read(p)               (*(p))
#define _gasneti_scalar_atomic_increment(p)          ((*(p))++)
#define _gasneti_scalar_atomic_decrement(p)          ((*(p))--)
#define _gasneti_scalar_atomic_decrement_and_test(p) ((--(*(p))) == 0)
#define _gasneti_scalar_atomic_compare_and_swap(p,oval,nval) \
                                                     (*(p) == (oval) ? (*(p) = (nval), 1) : 0)
#define _gasneti_scalar_atomic_addfetch(p,op)        (*(p) += (op))
#define _gasneti_scalar_atomic_add(p,op)             (*(p) += (op))
#define _gasneti_scalar_atomic_subtract(p,op)        (*(p) -= (op))

/* ------------------------------------------------------------------------------------ */
/* Define the generic (mutex based) 32- and/or 64-bit opaque types if requested
 */

#if defined(GASNETI_USE_GENERIC_ATOMIC32) || defined(GASNETI_USE_GENERIC_ATOMIC64)
  #if defined(GASNETI_GENATOMIC_LOCK) /* Mutex-based (HSL or pthread mutex) versions */
    #define _GASNETI_GENATOMIC_DECL(_sz)                                                      \
      typedef struct { volatile uint##_sz##_t ctr; }  gasneti_genatomic##_sz##_t;             \
      extern int _gasneti_genatomic##_sz##_decrement_and_test(gasneti_genatomic##_sz##_t *p); \
      extern int _gasneti_genatomic##_sz##_compare_and_swap(gasneti_genatomic##_sz##_t *p,    \
                                                            uint##_sz##_t oldval,             \
                                                            uint##_sz##_t newval);            \
      extern uint##_sz##_t _gasneti_genatomic##_sz##_addfetch(gasneti_genatomic##_sz##_t *p,  \
                                                              int##_sz##_t op);
    #define _GASNETI_GENATOMIC_INIT(v)      { (v) }
    #define _GASNETI_GENATOMIC_READ(p)      ((p)->ctr)
    #define _GASNETI_GENATOMIC_SET(p,v)     do { \
        GASNETI_GENATOMIC_LOCK();                \
        (p)->ctr = (v);                          \
        GASNETI_GENATOMIC_UNLOCK();              \
      } while (0)
    #define _GASNETI_GENATOMIC_INCREMENT(p) do { \
        GASNETI_GENATOMIC_LOCK();                \
        ((p)->ctr)++;                            \
        GASNETI_GENATOMIC_UNLOCK();              \
    } while (0)
    #define _GASNETI_GENATOMIC_DECREMENT(p) do { \
        GASNETI_GENATOMIC_LOCK();                \
        ((p)->ctr)--;                            \
        GASNETI_GENATOMIC_UNLOCK();              \
      } while (0)
    #define _GASNETI_GENATOMIC_DEFN(_sz)                                      \
      /* Decrement-and-test: */                                               \
      int _gasneti_genatomic##_sz##_decrement_and_test(gasneti_genatomic##_sz##_t *p) {  \
        uint##_sz##_t newval;                                                 \
        GASNETI_GENATOMIC_LOCK();                                             \
        newval = p->ctr - 1;                                                  \
        p->ctr = newval;                                                      \
        GASNETI_GENATOMIC_UNLOCK();                                           \
        return (newval == 0);                                                 \
      }                                                                       \
      /* Compare-and-swap: */                                                 \
      int _gasneti_genatomic##_sz##_compare_and_swap(gasneti_genatomic##_sz##_t *p, \
                                                     uint##_sz##_t oldval,          \
                                                     uint##_sz##_t newval) {        \
        int retval;                                                           \
        GASNETI_GENATOMIC_LOCK();                                             \
        retval = (p->ctr == oldval);                                          \
        if_pt (retval) {                                                      \
          p->ctr = newval;                                                    \
        }                                                                     \
        GASNETI_GENATOMIC_UNLOCK();                                           \
        return retval;                                                        \
      }                                                                       \
      /* Add-and-fetch: */                                                    \
      uint##_sz##_t _gasneti_genatomic##_sz##_addfetch(gasneti_genatomic##_sz##_t *p, \
                                                       int##_sz##_t op) {             \
        uint##_sz##_t retval;                                                 \
        GASNETI_GENATOMIC_LOCK();                                             \
        retval = (((p)->ctr) += (op));                                        \
        GASNETI_GENATOMIC_UNLOCK();                                           \
        return retval;                                                        \
      }
  #else /* Fully serial version */
    #define _GASNETI_GENATOMIC_DECL(_sz)                                      \
      typedef volatile uint##_sz##_t gasneti_genatomic##_sz##_t;
    #define _GASNETI_GENATOMIC_INIT             _gasneti_scalar_atomic_init
    #define _GASNETI_GENATOMIC_READ             _gasneti_scalar_atomic_read
    #define _GASNETI_GENATOMIC_SET              _gasneti_scalar_atomic_set
    #define _GASNETI_GENATOMIC_INCREMENT        _gasneti_scalar_atomic_increment
    #define _GASNETI_GENATOMIC_DECREMENT        _gasneti_scalar_atomic_decrement
  #endif

  #ifdef GASNETI_USE_GENERIC_ATOMIC32
    _GASNETI_GENATOMIC_DECL(32)
    #define _gasneti_genatomic32_init(v)      _GASNETI_GENATOMIC_INIT(v)
    #define _gasneti_genatomic32_read(p)      _GASNETI_GENATOMIC_READ(p)
    #define _gasneti_genatomic32_set(p,v)     _GASNETI_GENATOMIC_SET(p,v)
    #define _gasneti_genatomic32_increment(p) _GASNETI_GENATOMIC_INCREMENT(p)
    #define _gasneti_genatomic32_decrement(p) _GASNETI_GENATOMIC_DECREMENT(p)

    #ifdef GASNETI_GENATOMIC_LOCK
      #define GASNETI_GENATOMIC32_DEFN        _GASNETI_GENATOMIC_DEFN(32)
    #else
      #define _gasneti_genatomic32_decrement_and_test _gasneti_scalar_atomic_decrement_and_test
      #define _gasneti_genatomic32_compare_and_swap   _gasneti_scalar_atomic_compare_and_swap
      #define _gasneti_genatomic32_addfetch           _gasneti_scalar_atomic_addfetch
    #endif

    /* Define 32-bit opaque atomics in terms of full-fenced generics */
    #define GASNETI_ATOMIC32_NOT_SIGNALSAFE 1
    #define gasneti_atomic32_t                   gasneti_genatomic32_t
    #define _gasneti_atomic32_init               _gasneti_genatomic32_init
    #define gasneti_atomic32_set                 gasneti_genatomic32_set
    #define gasneti_atomic32_read                gasneti_genatomic32_read
    #define gasneti_atomic32_increment           gasneti_genatomic32_increment
    #define gasneti_atomic32_decrement           gasneti_genatomic32_decrement
    #define gasneti_atomic32_decrement_and_test  gasneti_genatomic32_decrement_and_test
    #define gasneti_atomic32_compare_and_swap    gasneti_genatomic32_compare_and_swap
    #define gasneti_atomic32_addfetch            gasneti_genatomic32_addfetch
  #endif

  #ifdef GASNETI_USE_GENERIC_ATOMIC64
    _GASNETI_GENATOMIC_DECL(64)
    #define _gasneti_genatomic64_init(v)      _GASNETI_GENATOMIC_INIT(v)
    #define _gasneti_genatomic64_read(p)      _GASNETI_GENATOMIC_READ(p)
    #define _gasneti_genatomic64_set(p,v)     _GASNETI_GENATOMIC_SET(p,v)
    #define _gasneti_genatomic64_increment(p) _GASNETI_GENATOMIC_INCREMENT(p)
    #define _gasneti_genatomic64_decrement(p) _GASNETI_GENATOMIC_DECREMENT(p)

    #ifdef GASNETI_GENATOMIC_LOCK
      #define GASNETI_GENATOMIC64_DEFN        _GASNETI_GENATOMIC_DEFN(64)
    #else
      #define _gasneti_genatomic64_decrement_and_test _gasneti_scalar_atomic_decrement_and_test
      #define _gasneti_genatomic64_compare_and_swap   _gasneti_scalar_atomic_compare_and_swap
      #define _gasneti_genatomic64_addfetch           _gasneti_scalar_atomic_addfetch
    #endif

    /* Define 64-bit opaque atomics in terms of full-fenced generics */
    #define GASNETI_ATOMIC64_NOT_SIGNALSAFE 1
    #define gasneti_atomic64_t                   gasneti_genatomic64_t
    #define _gasneti_atomic64_init               _gasneti_genatomic64_init
    #define gasneti_atomic64_set                 gasneti_genatomic64_set
    #define gasneti_atomic64_read                gasneti_genatomic64_read
    #define gasneti_atomic64_increment           gasneti_genatomic64_increment
    #define gasneti_atomic64_decrement           gasneti_genatomic64_decrement
    #define gasneti_atomic64_decrement_and_test  gasneti_genatomic64_decrement_and_test
    #define gasneti_atomic64_compare_and_swap    gasneti_genatomic64_compare_and_swap
    #define gasneti_atomic64_addfetch            gasneti_genatomic64_addfetch
  #endif
#endif

/* ------------------------------------------------------------------------------------ */
/* We can derive gasneti_atomic_t from either the 32-bit or 64-bit opaque types.
 */

#if defined(GASNETI_HAVE_PRIVATE_ATOMIC_T)
  /* Use platform-specific type, even though atomic32_t or atomic64_t might be present. */
#elif !defined(GASNETI_FORCE_64BIT_ATOMICOPS) && /* Not forcing 64-bits */ \
      (!defined(GASNETI_USE_GENERIC_ATOMIC32) || defined(GASNETI_USE_GENERIC_ATOMIC64)) /* No worse than 64 bit */
  typedef uint32_t				gasneti_atomic_val_t;
  typedef int32_t				gasneti_atomic_sval_t;
  #define GASNETI_ATOMIC_MAX			((gasneti_atomic_val_t)0xFFFFFFFFU)
  #define GASNETI_ATOMIC_SIGNED_MIN		((gasneti_atomic_sval_t)0x80000000)
  #define GASNETI_ATOMIC_SIGNED_MAX		((gasneti_atomic_sval_t)0x7FFFFFFF)

  /* Required parts: */
  #define gasneti_atomic_t			gasneti_atomic32_t
  #define _gasneti_atomic_init			_gasneti_atomic32_init
  #ifdef gasneti_atomic32_set
    #define gasneti_atomic_set			gasneti_atomic32_set
  #else
    #define _gasneti_atomic_set			_gasneti_atomic32_set
  #endif
  #ifdef gasneti_atomic32_read
    #define gasneti_atomic_read			gasneti_atomic32_read
  #else
    #define _gasneti_atomic_read		_gasneti_atomic32_read
  #endif
  #ifdef gasneti_atomic32_compare_and_swap
    #define gasneti_atomic_compare_and_swap	gasneti_atomic32_compare_and_swap
  #else
    #define _gasneti_atomic_compare_and_swap	_gasneti_atomic32_compare_and_swap
  #endif
  #define GASNETI_HAVE_ATOMIC_CAS		1

  /* Optional parts: */
  #if defined(gasneti_atomic32_increment)
    #define gasneti_atomic_increment		gasneti_atomic32_increment
  #elif defined(_gasneti_atomic32_increment)
    #define _gasneti_atomic_increment		_gasneti_atomic32_increment
  #endif
  #if defined(gasneti_atomic32_decrement)
    #define gasneti_atomic_decrement		gasneti_atomic32_decrement
  #elif defined(_gasneti_atomic32_decrement)
    #define _gasneti_atomic_decrement		_gasneti_atomic32_decrement
  #endif
  #if defined(gasneti_atomic32_decrement_and_test)
    #define gasneti_atomic_decrement_and_test	gasneti_atomic32_decrement_and_test
  #elif defined(_gasneti_atomic32_decrement_and_test)
    #define _gasneti_atomic_decrement_and_test	_gasneti_atomic32_decrement_and_test
  #endif
  #if defined(gasneti_atomic32_add)
    #define gasneti_atomic_add			gasneti_atomic32_add
  #elif defined(_gasneti_atomic32_add)
    #define _gasneti_atomic_add			_gasneti_atomic32_add
  #endif
  #if defined(gasneti_atomic32_subtract)
    #define gasneti_atomic_subtract		gasneti_atomic32_subtract
  #elif defined(_gasneti_atomic32_subtract)
    #define _gasneti_atomic_subtract		_gasneti_atomic32_subtract
  #endif

  /* Optional internal parts: */
  #if defined(_gasneti_atomic32_addfetch)
    #define _gasneti_atomic_addfetch		_gasneti_atomic32_addfetch
  #elif defined(_gasneti_atomic32_fetchadd)
    #define _gasneti_atomic_fetchadd		_gasneti_atomic32_fetchadd
  #elif defined(gasneti_atomic32_addfetch)
    #define gasneti_atomic_addfetch		gasneti_atomic32_addfetch
  #endif

  #if defined(GASNETI_USE_GENERIC_ATOMIC32)
    #ifndef GASNETI_USE_GENERIC_ATOMICOPS
      #defined GASNETI_USE_GENERIC_ATOMICOPS 1
    #endif
    #define GASNETI_HAVE_ATOMIC_ADDSUB 1
  #endif
  #ifdef GASNETI_ATOMIC32_NOT_SIGNALSAFE
    #define GASNETI_ATOMICOPS_NOT_SIGNALSAFE 1
  #endif
#else
  typedef uint64_t			gasneti_atomic_val_t;
  typedef int64_t			gasneti_atomic_sval_t;
  #define GASNETI_ATOMIC_MAX		((gasneti_atomic_val_t)0xFFFFFFFFFFFFFFFFLLU)
  #define GASNETI_ATOMIC_SIGNED_MIN	((gasneti_atomic_sval_t)0x8000000000000000LL)
  #define GASNETI_ATOMIC_SIGNED_MAX	((gasneti_atomic_sval_t)0x7FFFFFFFFFFFFFFFLL)

  /* Required parts: */
  #define gasneti_atomic_t			gasneti_atomic64_t
  #define _gasneti_atomic_init			_gasneti_atomic64_init
  #ifdef gasneti_atomic64_set
    #define gasneti_atomic_set			gasneti_atomic64_set
  #else
    #define _gasneti_atomic_set			_gasneti_atomic64_set
  #endif
  #ifdef gasneti_atomic64_read
    #define gasneti_atomic_read			gasneti_atomic64_read
  #else
    #define _gasneti_atomic_read		_gasneti_atomic64_read
  #endif
  #ifdef gasneti_atomic64_compare_and_swap
    #define gasneti_atomic_compare_and_swap	gasneti_atomic64_compare_and_swap
  #else
    #define _gasneti_atomic_compare_and_swap	_gasneti_atomic64_compare_and_swap
  #endif
  #define GASNETI_HAVE_ATOMIC_CAS		1

  /* Optional parts: */
  #if defined(gasneti_atomic64_increment)
    #define gasneti_atomic_increment		gasneti_atomic64_increment
  #elif defined(_gasneti_atomic64_increment)
    #define _gasneti_atomic_increment		_gasneti_atomic64_increment
  #endif
  #if defined(gasneti_atomic64_decrement)
    #define gasneti_atomic_decrement		gasneti_atomic64_decrement
  #elif defined(_gasneti_atomic64_decrement)
    #define _gasneti_atomic_decrement		_gasneti_atomic64_decrement
  #endif
  #if defined(gasneti_atomic64_decrement_and_test)
    #define gasneti_atomic_decrement_and_test	gasneti_atomic64_decrement_and_test
  #elif defined(_gasneti_atomic64_decrement_and_test)
    #define _gasneti_atomic_decrement_and_test	_gasneti_atomic64_decrement_and_test
  #endif
  #if defined(gasneti_atomic64_add)
    #define gasneti_atomic_add			gasneti_atomic64_add
  #elif defined(_gasneti_atomic64_add)
    #define _gasneti_atomic_add			_gasneti_atomic64_add
  #endif
  #if defined(gasneti_atomic64_subtract)
    #define gasneti_atomic_subtract		gasneti_atomic64_subtract
  #elif defined(_gasneti_atomic64_subtract)
    #define _gasneti_atomic_subtract		_gasneti_atomic64_subtract
  #endif

  /* Optional internal parts: */
  #if defined(_gasneti_atomic64_addfetch)
    #define _gasneti_atomic_addfetch		_gasneti_atomic64_addfetch
  #elif defined(_gasneti_atomic64_fetchadd)
    #define _gasneti_atomic_fetchadd		_gasneti_atomic64_fetchadd
  #elif defined(gasneti_atomic64_addfetch)
    #define gasneti_atomic_addfetch		gasneti_atomic64_addfetch
  #endif

  #if defined(GASNETI_USE_GENERIC_ATOMIC64)
    #ifndef GASNETI_USE_GENERIC_ATOMICOPS
      #defined GASNETI_USE_GENERIC_ATOMICOPS 1
    #endif
    #define GASNETI_HAVE_ATOMIC_ADDSUB 1
  #endif
  #ifdef GASNETI_ATOMIC64_NOT_SIGNALSAFE
    #define GASNETI_ATOMICOPS_NOT_SIGNALSAFE 1
  #endif
#endif

/* ------------------------------------------------------------------------------------ */
/* Slow function-call based atomics
 * Used at client compile time for any compiler w/o inline asm support
 */

#if defined(GASNETI_USING_SLOW_ATOMICS)
  GASNETI_EXTERNC gasneti_atomic_val_t gasneti_slow_atomic_read(gasneti_atomic_t *p, const int flags);
  #define gasneti_atomic_read gasneti_slow_atomic_read
  GASNETI_EXTERNC void gasneti_slow_atomic_set(gasneti_atomic_t *p, gasneti_atomic_val_t v, const int flags);
  #define gasneti_atomic_set gasneti_slow_atomic_set
  GASNETI_EXTERNC void gasneti_slow_atomic_increment(gasneti_atomic_t *p, const int flags);
  #define gasneti_atomic_increment gasneti_slow_atomic_increment
  GASNETI_EXTERNC void gasneti_slow_atomic_decrement(gasneti_atomic_t *p, const int flags);
  #define gasneti_atomic_decrement gasneti_slow_atomic_decrement
  GASNETI_EXTERNC int gasneti_slow_atomic_decrement_and_test(gasneti_atomic_t *p, const int flags);
  #define gasneti_atomic_decrement_and_test gasneti_slow_atomic_decrement_and_test
  #if defined(GASNETI_HAVE_ATOMIC_CAS)
    GASNETI_EXTERNC int gasneti_slow_atomic_compare_and_swap(gasneti_atomic_t *p, gasneti_atomic_val_t oldval, gasneti_atomic_val_t newval, const int flags);
    #define gasneti_atomic_compare_and_swap gasneti_slow_atomic_compare_and_swap
  #endif
  #if defined(GASNETI_HAVE_ATOMIC_ADD_SUB)
    GASNETI_EXTERNC gasneti_atomic_val_t gasneti_slow_atomic_add(gasneti_atomic_t *p, gasneti_atomic_val_t op, const int flags);
    #define gasneti_atomic_add gasneti_slow_atomic_add
    GASNETI_EXTERNC gasneti_atomic_val_t gasneti_slow_atomic_subtract(gasneti_atomic_t *p, gasneti_atomic_val_t op, const int flags);
    #define gasneti_atomic_subtract gasneti_slow_atomic_subtract
  #endif
#endif

/* ------------------------------------------------------------------------------------ */
/* Default increment, decrement, decrement-and-test, add and subtract atomics in
 * terms of addfetch, fetachadd or compare-and-swap.
 */

#if defined(GASNETI_USING_SLOW_ATOMICS)
  /* No default atomics built when using "slow" atomics. */
#elif defined(gasneti_atomic_addfetch)
  #ifndef gasneti_atomic_increment
    #define gasneti_atomic_increment(p,f)	((void)gasneti_atomic_addfetch(((p),(f)),1))
  #endif
  #ifndef gasneti_atomic_decrement
    #define gasneti_atomic_decrement(p,f)	((void)gasneti_atomic_addfetch(((p),(f)),-1))
  #endif
  #ifndef gasneti_atomic_decrement_and_test
    #define gasneti_atomic_decrement_and_test(p,f) \
						(gasneti_atomic_addfetch(((p),(f)),-1) == 0)
  #endif
  #ifndef GASNETI_HAVE_ATOMIC_ADD_SUB
    #define gasneti_atomic_add(p,op,f)		((gasneti_atomic_val_t)(gasneti_atomic_addfetch((p),(op),(f))))
    #define gasneti_atomic_subtract(p,op,f)	((gasneti_atomic_val_t)(gasneti_atomic_addfetch((p),-(op),(f))))
    #define GASNETI_HAVE_ATOMIC_ADD_SUB 	1
  #endif
#elif defined(_gasneti_atomic_fetchadd)	
  #ifndef _gasneti_atomic_increment
    #define _gasneti_atomic_increment(p)	((void)_gasneti_atomic_fetchadd((p),1))
  #endif
  #ifndef _gasneti_atomic_decrement
    #define _gasneti_atomic_decrement(p)	((void)_gasneti_atomic_fetchadd((p),-1))
  #endif
  #ifndef _gasneti_atomic_decrement_and_test
    #define _gasneti_atomic_decrement_and_test(p) \
						(_gasneti_atomic_fetchadd((p),-1) == 1)
  #endif
  #ifndef GASNETI_HAVE_ATOMIC_ADD_SUB
    /* NOTE: _gasneti_atomic_{add,subtract} are only called w/ args free of side-effects.
     * So, these macros can safely expand the arguments multiple times. */
    #define _gasneti_atomic_add(p,op)		((gasneti_atomic_val_t)(_gasneti_atomic_fetchadd(p,op) + op))
    #define _gasneti_atomic_subtract(p,op)	((gasneti_atomic_val_t)(_gasneti_atomic_fetchadd(p,-op) - op))
    #define GASNETI_HAVE_ATOMIC_ADD_SUB 	1
  #endif
#elif defined(_gasneti_atomic_addfetch) || defined (GASNETI_HAVE_ATOMIC_CAS)
  #if !defined(_gasneti_atomic_addfetch)
    /* If needed, build addfetch from compare-and-swap. */
    GASNETI_INLINE(gasneti_atomic_addfetch)
    gasneti_atomic_val_t gasneti_atomic_addfetch(gasneti_atomic_t *p, gasneti_atomic_sval_t op) {
      gasneti_atomic_val_t _old, _new;
      do {
        _new = (_old = _gasneti_atomic_read(p)) + op;
      } while (!_gasneti_atomic_compare_and_swap(p, _old, _new));
      return _new;
    }
    #define _gasneti_atomic_addfetch gasneti_atomic_addfetch
  #endif

  #ifndef _gasneti_atomic_increment
    #define _gasneti_atomic_increment(p)	((void)_gasneti_atomic_addfetch((p),1))
  #endif
  #ifndef _gasneti_atomic_decrement
    #define _gasneti_atomic_decrement(p)	((void)_gasneti_atomic_addfetch((p),-1))
  #endif
  #ifndef _gasneti_atomic_decrement_and_test
    #define _gasneti_atomic_decrement_and_test(p) \
						(_gasneti_atomic_addfetch((p),-1) == 0)
  #endif
  #ifndef GASNETI_HAVE_ATOMIC_ADD_SUB
    #define _gasneti_atomic_add(p,op)		((gasneti_atomic_val_t)_gasneti_atomic_addfetch(p,op))
    #define _gasneti_atomic_subtract(p,op)	((gasneti_atomic_val_t)_gasneti_atomic_addfetch(p,-op))
    #define GASNETI_HAVE_ATOMIC_ADD_SUB 	1
  #endif
#endif

/* ------------------------------------------------------------------------------------ */
/* Uniform memory fences for GASNet atomics.
 */

/* The following groups of preprocessor directives are designed to perform
 * as much elimination of unreachable code as possible at preprocess time.
 * While much could be done more naturally (and with far less code) by the
 * compiler, there are 3 major reasons we want to go to this trouble:
 * 1) The inliner for gcc (and probably most other compilers) applies some
 *    heuristics and limits when deciding which functions to inline.
 *    These decisions are typically(?) made based on the "size" of the
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
 * The following preprocessor code is lengthy, and divided in to four
 * distinct parts for (some resemblance of) clarity:
 * Part 1.  Performs the simplifications based on membar properties.
 * Part 2.  Defines weakatomic fence macros in terms of the macros of Part 1.
 * Part 3.  Defines atomic fence macros in terms of the macros of Part 1,
 *          while applying simplications based on atomic side-effects.
 * Part 4.  Defines templates for defining the fenced atomics, of which there
 *          may be up to 3 families (32-bit, 64-bit and "other").
 * Both Parts 2 and 3 can be overridden by platform-specific definitions
 * of their respective macros.
 */

#define GASNETI_ATOMIC_MASK_PRE		(GASNETI_ATOMIC_WMB_PRE | GASNETI_ATOMIC_RMB_PRE)
#define GASNETI_ATOMIC_MASK_POST	(GASNETI_ATOMIC_WMB_POST | GASNETI_ATOMIC_RMB_POST)
#define GASNETI_ATOMIC_MASK_BOOL	(GASNETI_ATOMIC_WMB_POST | \
					 GASNETI_ATOMIC_RMB_POST | \
					 GASNETI_ATOMIC_RMB_POST_IF_TRUE | \
					 GASNETI_ATOMIC_RMB_POST_IF_FALSE)


/* Part 1.  Removal of fences which are redundant on a given platform
 *	_gasneti_atomic_{mb,rmb,wmb}_{before,after}(flags)
 *	_gasneti_atomic_fence_after_bool(flags, value)
 *
 * This level of macros serves to remove at, preprocess-time, any tests
 * which are redundant due to the relationships among fences.  For example,
 * on a platform with a single fence instruction that is mb(), rmb() and
 * wmb() these macros will reduce three conditionals to just one.
 */

#if GASNETI_RMB_IS_MB && GASNETI_WMB_IS_MB
  /* Since mb() == rmb() == wmb() (including case that all are empty), only
   * a single check is needed for all three.
   */
  #define _gasneti_atomic_mb_before(f)	if (f & GASNETI_ATOMIC_MASK_PRE) gasneti_local_mb();
  #define _gasneti_atomic_rmb_before(f)	/* nothing */
  #define _gasneti_atomic_wmb_before(f)	/* nothing */
  #define _gasneti_atomic_mb_after(f)	if (f & GASNETI_ATOMIC_MASK_POST) gasneti_local_mb();
  #define _gasneti_atomic_rmb_after(f)	/* nothing */
  #define _gasneti_atomic_wmb_after(f)	/* nothing */
#elif GASNETI_MB_IS_SUM
  /* Since mb() == rmb()+wmb(), distinct rmb() and wmb() checks are
   * sufficient to implement a request for mb(), rmb() or wmb().
   */
  #define _gasneti_atomic_mb_before(f)	/* nothing */
  #define _gasneti_atomic_rmb_before(f)	if (f & GASNETI_ATOMIC_RMB_PRE) gasneti_local_rmb();
  #define _gasneti_atomic_wmb_before(f)	if (f & GASNETI_ATOMIC_WMB_PRE) gasneti_local_wmb();
  #define _gasneti_atomic_mb_after(f)	/* nothing */
  #define _gasneti_atomic_rmb_after(f)	if (f & GASNETI_ATOMIC_RMB_POST) gasneti_local_rmb();
  #define _gasneti_atomic_wmb_after(f)	if (f & GASNETI_ATOMIC_WMB_POST) gasneti_local_wmb();
#else
  /*  With distinct mb(), rmb() and wmb(), we make the most general 3 checks (like a "switch").
   */
  #define _gasneti_atomic_mb_before(f)	if ((f & GASNETI_ATOMIC_MASK_PRE) == GASNETI_ATOMIC_MB_PRE) gasneti_local_mb();
  #define _gasneti_atomic_rmb_before(f)	else if (f & GASNETI_ATOMIC_RMB_PRE) gasneti_local_rmb();
  #define _gasneti_atomic_wmb_before(f)	else if (f & GASNETI_ATOMIC_WMB_PRE) gasneti_local_wmb();
  #define _gasneti_atomic_mb_after(f)	if ((f & GASNETI_ATOMIC_MASK_POST) == GASNETI_ATOMIC_MB_POST) gasneti_local_mb();
  #define _gasneti_atomic_rmb_after(f)	else if (f & GASNETI_ATOMIC_RMB_POST) gasneti_local_rmb();
  #define _gasneti_atomic_wmb_after(f)	else if (f & GASNETI_ATOMIC_WMB_POST) gasneti_local_wmb();
#endif

#if 1
  /*
   * Several optimizations are possible when a conditional rmb() is combined
   * with an unconditional POST fence.  Such optimizations would prevent
   * imposing a "double" mb() in such cases.  However:
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

#if 1 /* No current need/desire to override these */
  #define _gasneti_atomic_cf_before(f)	if (f & GASNETI_ATOMIC_MASK_PRE) gasneti_compiler_fence();
  #define _gasneti_atomic_cf_after(f)	if (f & GASNETI_ATOMIC_MASK_POST) gasneti_compiler_fence();
  #define _gasneti_atomic_cf_bool(f)	if (f & GASNETI_ATOMIC_MASK_BOOL) gasneti_compiler_fence();
#endif

/* Part 2.  Convienience macros for weakatomics
 *	_gasneti_weakatomic_fence_{before,after}_{set,read,rmw}(flags)
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
  #define _gasneti_weakatomic_fence_after_bool(f,v) \
						_gasneti_atomic_mb_after(f)  \
						_gasneti_atomic_rmb_after(f) \
						_gasneti_atomic_wmb_after(f) \
						_gasneti_atomic_rmb_bool(f,v)
#endif
#define _gasneti_weakatomic_fence_before_set  _gasneti_weakatomic_fence_before
#define _gasneti_weakatomic_fence_after_set   _gasneti_weakatomic_fence_after
#define _gasneti_weakatomic_fence_before_read _gasneti_weakatomic_fence_before
#define _gasneti_weakatomic_fence_after_read  _gasneti_weakatomic_fence_after
#define _gasneti_weakatomic_fence_before_rmw  _gasneti_weakatomic_fence_before
#define _gasneti_weakatomic_fence_after_rmw   _gasneti_weakatomic_fence_after


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
#elif (GASNETI_ATOMIC_FENCE_SET & GASNETI_ATOMIC_MASK_PRE) == GASNETI_ATOMIC_MB_PRE
  #define _gasneti_atomic_fence_before_set(f)	_gasneti_atomic_cf_before(f)
#elif (GASNETI_ATOMIC_FENCE_SET & GASNETI_ATOMIC_MASK_PRE) == GASNETI_ATOMIC_RMB_PRE
  #define _gasneti_atomic_fence_before_set(f)	_gasneti_atomic_mb_before(f)  \
						_gasneti_atomic_wmb_before(f)
#elif (GASNETI_ATOMIC_FENCE_SET & GASNETI_ATOMIC_MASK_PRE) == GASNETI_ATOMIC_WMB_PRE
  #define _gasneti_atomic_fence_before_set(f)	_gasneti_atomic_mb_before(f)  \
						_gasneti_atomic_rmb_before(f)
#else
  #define _gasneti_atomic_fence_before_set(f)	_gasneti_atomic_mb_before(f)  \
						_gasneti_atomic_rmb_before(f) \
						_gasneti_atomic_wmb_before(f)
#endif
#if defined(_gasneti_atomic_fence_after_set)
  /* Use platform-specific definitions */
#elif (GASNETI_ATOMIC_FENCE_SET & GASNETI_ATOMIC_MASK_POST) == GASNETI_ATOMIC_MB_POST
  #define _gasneti_atomic_fence_after_set(f)	_gasneti_atomic_cf_after(f)
#elif (GASNETI_ATOMIC_FENCE_SET & GASNETI_ATOMIC_MASK_POST) == GASNETI_ATOMIC_RMB_POST
  #define _gasneti_atomic_fence_after_set(f)	_gasneti_atomic_mb_after(f)  \
						_gasneti_atomic_wmb_after(f)
#elif (GASNETI_ATOMIC_FENCE_SET & GASNETI_ATOMIC_MASK_POST) == GASNETI_ATOMIC_WMB_POST
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
#elif (GASNETI_ATOMIC_FENCE_READ & GASNETI_ATOMIC_MASK_PRE) == GASNETI_ATOMIC_MB_PRE
  #define _gasneti_atomic_fence_before_read(f)	_gasneti_atomic_cf_before(f)
#elif (GASNETI_ATOMIC_FENCE_READ & GASNETI_ATOMIC_MASK_PRE) == GASNETI_ATOMIC_RMB_PRE
  #define _gasneti_atomic_fence_before_read(f)	_gasneti_atomic_mb_before(f)  \
						_gasneti_atomic_wmb_before(f)
#elif (GASNETI_ATOMIC_FENCE_READ & GASNETI_ATOMIC_MASK_PRE) == GASNETI_ATOMIC_WMB_PRE
  #define _gasneti_atomic_fence_before_read(f)	_gasneti_atomic_mb_before(f)  \
						_gasneti_atomic_rmb_before(f)
#else
  #define _gasneti_atomic_fence_before_read(f)	_gasneti_atomic_mb_before(f)  \
						_gasneti_atomic_rmb_before(f) \
						_gasneti_atomic_wmb_before(f)
#endif
#if defined(_gasneti_atomic_fence_after_read)
  /* Use platform-specific definitions */
#elif (GASNETI_ATOMIC_FENCE_READ & GASNETI_ATOMIC_MASK_POST) == GASNETI_ATOMIC_MB_POST
  #define _gasneti_atomic_fence_after_read(f)	_gasneti_atomic_cf_after(f)
#elif (GASNETI_ATOMIC_FENCE_READ & GASNETI_ATOMIC_MASK_POST) == GASNETI_ATOMIC_RMB_POST
  #define _gasneti_atomic_fence_after_read(f)	_gasneti_atomic_mb_after(f)  \
						_gasneti_atomic_wmb_after(f)
#elif (GASNETI_ATOMIC_FENCE_READ & GASNETI_ATOMIC_MASK_POST) == GASNETI_ATOMIC_WMB_POST
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
#elif (GASNETI_ATOMIC_FENCE_RMW & GASNETI_ATOMIC_MASK_PRE) == GASNETI_ATOMIC_MB_PRE
  #define _gasneti_atomic_fence_before_rmw(f)	_gasneti_atomic_cf_before(f)
#elif (GASNETI_ATOMIC_FENCE_RMW & GASNETI_ATOMIC_MASK_PRE) == GASNETI_ATOMIC_RMB_PRE
  #define _gasneti_atomic_fence_before_rmw(f)	_gasneti_atomic_mb_before(f)  \
						_gasneti_atomic_wmb_before(f)
#elif (GASNETI_ATOMIC_FENCE_RMW & GASNETI_ATOMIC_MASK_PRE) == GASNETI_ATOMIC_WMB_PRE
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
#elif (GASNETI_ATOMIC_FENCE_RMW & GASNETI_ATOMIC_MASK_POST) == GASNETI_ATOMIC_MB_POST
  #define _gasneti_atomic_fence_after_rmw(f)	_gasneti_atomic_cf_after(f)
  #define _gasneti_atomic_fence_after_bool(f,v)	_gasneti_atomic_cf_bool(f)
#elif (GASNETI_ATOMIC_FENCE_RMW & GASNETI_ATOMIC_MASK_POST) == GASNETI_ATOMIC_RMB_POST
  #define _gasneti_atomic_fence_after_rmw(f)	_gasneti_atomic_mb_after(f)  \
						_gasneti_atomic_wmb_after(f)
  #define _gasneti_atomic_fence_after_bool(f,v)	_gasneti_atomic_mb_after(f)  \
						_gasneti_atomic_wmb_after(f)
#elif (GASNETI_ATOMIC_FENCE_RMW & GASNETI_ATOMIC_MASK_POST) == GASNETI_ATOMIC_WMB_POST
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
/* Part 4.  Fenced atomic templates, using the fencing macros of Part 3, above.
 */

#define GASNETI_ATOMIC_FENCED_SET(group,_func,p,v,f)                \
  do {                                                              \
    const int __flags = (f);                                        \
    _gasneti_##group##_fence_before_set(__flags)                    \
    _func((p),(v));                                                 \
    _gasneti_##group##_fence_after_set(__flags)                     \
  } while (0)
#define GASNETI_ATOMIC_FENCED_READ_DEFN(group,func,_func,stem)      \
  GASNETI_INLINE(func)                                              \
  stem##val_t func(stem##t *p, const int flags) {                   \
    _gasneti_##group##_fence_before_read(flags)                     \
    { const stem##val_t retval = _func(p);                          \
      _gasneti_##group##_fence_after_read(flags)                    \
      return retval;                                                \
    }                                                               \
  }
#define GASNETI_ATOMIC_FENCED_INCDEC(group,_func,p,f)               \
  do {                                                              \
    const int __flags = (f);                                        \
    _gasneti_##group##_fence_before_rmw(__flags)                    \
    _func(p);                                                       \
    _gasneti_##group##_fence_after_rmw(__flags)                     \
  } while (0)
#define GASNETI_ATOMIC_FENCED_DECTEST_DEFN(group,func,_func,stem)   \
  GASNETI_INLINE(func)                                              \
  int func(stem##t *p, const int flags) {                           \
    _gasneti_##group##_fence_before_rmw(flags)                      \
    { const int retval = _func(p);                                  \
      _gasneti_##group##_fence_after_bool(flags, retval)            \
      return retval;                                                \
    }                                                               \
  }
#define GASNETI_ATOMIC_FENCED_CAS_DEFN(group,func,_func,stem)       \
  GASNETI_INLINE(func)                                              \
  int func(stem##t *p, stem##val_t oldval,                          \
           stem##val_t newval, const int flags) {                   \
    _gasneti_##group##_fence_before_rmw(flags)                      \
    { const int retval = _func(p,oldval,newval);                    \
      _gasneti_##group##_fence_after_bool(flags, retval)            \
      return retval;                                                \
    }                                                               \
  }
#define GASNETI_ATOMIC_FENCED_ADDSUB_DEFN(group,func,_func,stem)    \
  GASNETI_INLINE(func)                                              \
  stem##val_t func(stem##t *p, stem##val_t op, const int flags) {   \
    /* TODO: prohibit zero as well? */                              \
    gasneti_assert((stem##sval_t)op >= 0);                          \
    _gasneti_##group##_fence_before_rmw(flags)                      \
    { const stem##val_t retval = _func(p, op);                      \
      _gasneti_##group##_fence_after_rmw(flags)                     \
      return retval;                                                \
    }                                                               \
  }
#define GASNETI_ATOMIC_FENCED_ADDFETCH_DEFN(group,func,_func,stem)  \
  GASNETI_INLINE(func)                                              \
  stem##val_t func(stem##t *p, stem##sval_t op, const int flags) {  \
    _gasneti_##group##_fence_before_rmw(flags)                      \
    { const stem##val_t retval = _func(p, op);                      \
      _gasneti_##group##_fence_after_rmw(flags)                     \
      return retval;                                                \
    }                                                               \
  }

/* ------------------------------------------------------------------------------------ */
/* GASNet atomic ops, using per-platform defns and the macros of Part 4, above.
 */

#if !defined(GASNETI_USING_SLOW_ATOMICS)
  /* Fence the opqaue (non-arithmetic) 32-bit atomic type */
  typedef uint32_t gasneti_atomic32_val_t;	/* For consistency in fencing macros */
  typedef int32_t gasneti_atomic32_sval_t;	/* For consistency in fencing macros */
  #ifndef gasneti_atomic32_init
    #define gasneti_atomic32_init(v)	_gasneti_atomic32_init(v)
  #endif
  #ifndef gasneti_atomic32_set
    #define gasneti_atomic32_set(p,v,f)	GASNETI_ATOMIC_FENCED_SET(atomic,_gasneti_atomic32_set,p,v,f)
  #endif
  #ifndef gasneti_atomic32_read
    GASNETI_ATOMIC_FENCED_READ_DEFN(atomic,gasneti_atomic32_read,_gasneti_atomic32_read,gasneti_atomic32_)
  #endif
  #ifndef gasneti_atomic32_compare_and_swap
    GASNETI_ATOMIC_FENCED_CAS_DEFN(atomic,gasneti_atomic32_compare_and_swap,_gasneti_atomic32_compare_and_swap,gasneti_atomic32_)
  #endif

  /* Fence the opqaue (non-arithmetic) 64-bit atomic type */
  typedef uint64_t gasneti_atomic64_val_t;	/* For consistency in fencing macros */
  typedef int64_t gasneti_atomic64_sval_t;	/* For consistency in fencing macros */
  #ifndef gasneti_atomic64_init
    #define gasneti_atomic64_init(v)	_gasneti_atomic64_init(v)
  #endif
  #ifndef gasneti_atomic64_set
    #define gasneti_atomic64_set(p,v,f)	GASNETI_ATOMIC_FENCED_SET(atomic,_gasneti_atomic64_set,p,v,f)
  #endif
  #ifndef gasneti_atomic64_read
    GASNETI_ATOMIC_FENCED_READ_DEFN(atomic,gasneti_atomic64_read,_gasneti_atomic64_read,gasneti_atomic64_)
  #endif
  #ifndef gasneti_atomic64_compare_and_swap
    GASNETI_ATOMIC_FENCED_CAS_DEFN(atomic,gasneti_atomic64_compare_and_swap,_gasneti_atomic64_compare_and_swap,gasneti_atomic64_)
  #endif
#endif

#if defined(GASNETI_USE_GENERIC_ATOMIC32) || defined(GASNETI_USE_GENERIC_ATOMIC64)
  /* Fences for the generics */
  #ifndef GASNETI_GENATOMIC_LOCK
    /* Not locking, so use full fences */
    #define _gasneti_genatomic_fence_before_rmw(f)	_gasneti_atomic_mb_before(f)  \
							_gasneti_atomic_rmb_before(f) \
							_gasneti_atomic_wmb_before(f)
    #define _gasneti_genatomic_fence_after_rmw(f)	_gasneti_atomic_mb_after(f)   \
							_gasneti_atomic_rmb_after(f)  \
							_gasneti_atomic_wmb_after(f)
    #define _gasneti_genatomic_fence_after_bool(f,v)	_gasneti_atomic_mb_after(f)   \
							_gasneti_atomic_rmb_after(f)  \
							_gasneti_atomic_wmb_after(f)  \
							_gasneti_atomic_rmb_bool(f,v)
    #define _gasneti_genatomic_fence_before_set		_gasneti_genatomic_fence_before_rmw
    #define _gasneti_genatomic_fence_after_set		_gasneti_genatomic_fence_after_rmw
  #else
    /* The lock acquire includes RMB and release includes WMB */
    #define _gasneti_genatomic_fence_before_rmw(f)	_gasneti_atomic_mb_before(f)  \
							_gasneti_atomic_wmb_before(f)
    #define _gasneti_genatomic_fence_after_rmw(f)	_gasneti_atomic_mb_after(f)   \
							_gasneti_atomic_rmb_after(f)
    #define _gasneti_genatomic_fence_after_bool(f,v)	_gasneti_atomic_mb_after(f)   \
							_gasneti_atomic_rmb_after(f)  \
							_gasneti_atomic_rmb_bool(f,v)
    #define _gasneti_genatomic_fence_before_set		_gasneti_genatomic_fence_before_rmw
    #define _gasneti_genatomic_fence_after_set		_gasneti_genatomic_fence_after_rmw
  #endif
  /* READ is currently always performed *without* the lock (if any) held */
  #define _gasneti_genatomic_fence_before_read(f)	_gasneti_atomic_mb_before(f)  \
							_gasneti_atomic_rmb_before(f) \
							_gasneti_atomic_wmb_before(f)
  #define _gasneti_genatomic_fence_after_read(f)	_gasneti_atomic_mb_after(f)   \
							_gasneti_atomic_rmb_after(f)  \
							_gasneti_atomic_wmb_after(f)

  #if defined(GASNETI_USE_GENERIC_ATOMIC32)
    #define gasneti_genatomic32_set(p,v,f) \
				GASNETI_ATOMIC_FENCED_SET(genatomic,_gasneti_genatomic32_set,p,v,f)
    #define gasneti_genatomic32_increment(p,f) \
				GASNETI_ATOMIC_FENCED_INCDEC(genatomic,_gasneti_genatomic32_increment,p,f)
    #define gasneti_genatomic32_decrement(p,f) \
				GASNETI_ATOMIC_FENCED_INCDEC(genatomic,_gasneti_genatomic32_decrement,p,f)
    GASNETI_ATOMIC_FENCED_READ_DEFN(genatomic,gasneti_genatomic32_read,_gasneti_genatomic32_read,gasneti_atomic32_)
    GASNETI_ATOMIC_FENCED_DECTEST_DEFN(genatomic,gasneti_genatomic32_decrement_and_test,_gasneti_genatomic32_decrement_and_test,gasneti_atomic32_)
    GASNETI_ATOMIC_FENCED_CAS_DEFN(genatomic,gasneti_genatomic32_compare_and_swap,_gasneti_genatomic32_compare_and_swap,gasneti_atomic32_)
    GASNETI_ATOMIC_FENCED_ADDFETCH_DEFN(genatomic,gasneti_genatomic32_addfetch,_gasneti_genatomic32_addfetch,gasneti_atomic32_)
  #endif

  #if defined(GASNETI_USE_GENERIC_ATOMIC64)
    #define gasneti_genatomic64_set(p,v,f) \
				GASNETI_ATOMIC_FENCED_SET(genatomic,_gasneti_genatomic64_set,p,v,f)
    #define gasneti_genatomic64_increment(p,f) \
				GASNETI_ATOMIC_FENCED_INCDEC(genatomic,_gasneti_genatomic64_increment,p,f)
    #define gasneti_genatomic64_decrement(p,f) \
				GASNETI_ATOMIC_FENCED_INCDEC(genatomic,_gasneti_genatomic64_decrement,p,f)
    GASNETI_ATOMIC_FENCED_READ_DEFN(genatomic,gasneti_genatomic64_read,_gasneti_genatomic64_read,gasneti_atomic64_)
    GASNETI_ATOMIC_FENCED_DECTEST_DEFN(genatomic,gasneti_genatomic64_decrement_and_test,_gasneti_genatomic64_decrement_and_test,gasneti_atomic64_)
    GASNETI_ATOMIC_FENCED_CAS_DEFN(genatomic,gasneti_genatomic64_compare_and_swap,_gasneti_genatomic64_compare_and_swap,gasneti_atomic64_)
    GASNETI_ATOMIC_FENCED_ADDFETCH_DEFN(genatomic,gasneti_genatomic64_addfetch,_gasneti_genatomic64_addfetch,gasneti_atomic64_)
  #endif
#endif

/* ------------------------------------------------------------------------------------ */
/* "Normal" fenced atomics
 */

#ifndef gasneti_atomic_init
  #define gasneti_atomic_init(v)         _gasneti_atomic_init(v)
#endif
#ifndef gasneti_atomic_set
  #define gasneti_atomic_set(p,v,f)      GASNETI_ATOMIC_FENCED_SET(atomic,_gasneti_atomic_set,p,v,f)
#endif
#ifndef gasneti_atomic_increment
  #define gasneti_atomic_increment(p,f)  GASNETI_ATOMIC_FENCED_INCDEC(atomic,_gasneti_atomic_increment,p,f)
#endif
#ifndef gasneti_atomic_decrement
  #define gasneti_atomic_decrement(p,f)  GASNETI_ATOMIC_FENCED_INCDEC(atomic,_gasneti_atomic_decrement,p,f)
#endif
#ifndef gasneti_atomic_read
  GASNETI_ATOMIC_FENCED_READ_DEFN(atomic,gasneti_atomic_read,_gasneti_atomic_read,gasneti_atomic_)
#endif
#ifndef gasneti_atomic_decrement_and_test
  GASNETI_ATOMIC_FENCED_DECTEST_DEFN(atomic,gasneti_atomic_decrement_and_test,_gasneti_atomic_decrement_and_test,gasneti_atomic_)
#endif
#if defined(GASNETI_HAVE_ATOMIC_CAS) && !defined(gasneti_atomic_compare_and_swap)
  GASNETI_ATOMIC_FENCED_CAS_DEFN(atomic,gasneti_atomic_compare_and_swap,_gasneti_atomic_compare_and_swap,gasneti_atomic_)
#endif
#if defined(GASNETI_HAVE_ATOMIC_ADD_SUB) && !defined(gasneti_atomic_add)
  GASNETI_ATOMIC_FENCED_ADDSUB_DEFN(atomic,gasneti_atomic_add,_gasneti_atomic_add,gasneti_atomic_)
#endif
#if defined(GASNETI_HAVE_ATOMIC_ADD_SUB) && !defined(gasneti_atomic_subtract)
  GASNETI_ATOMIC_FENCED_ADDSUB_DEFN(atomic,gasneti_atomic_subtract,_gasneti_atomic_subtract,gasneti_atomic_)
#endif
#ifndef gasneti_atomic_signed
  #define gasneti_atomic_signed(val)	((gasneti_atomic_sval_t)(val))
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
  typedef gasneti_atomic_val_t gasneti_weakatomic_val_t;
  typedef gasneti_atomic_sval_t gasneti_weakatomic_sval_t;
  #define gasneti_weakatomic_init(v)                  gasneti_atomic_init(v)
  #define gasneti_weakatomic_signed(v)                gasneti_atomic_signed(v)
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

  typedef gasneti_atomic32_t gasneti_weakatomic32_t;
  #define gasneti_weakatomic32_init(v)              gasneti_atomic32_init(v)
  #define gasneti_weakatomic32_set(p,v,f)           gasneti_atomic32_set(p,v,f)
  #define gasneti_weakatomic32_read(p,f)            gasneti_atomic32_read(p,f)
  #define gasneti_weakatomic32_compare_and_swap(p,oldval,newval,f)  \
            gasneti_atomic32_compare_and_swap(p,oldval,newval,f)

  typedef gasneti_atomic64_t gasneti_weakatomic64_t;
  #define gasneti_weakatomic64_init(v)              gasneti_atomic64_init(v)
  #define gasneti_weakatomic64_set(p,v,f)           gasneti_atomic64_set(p,v,f)
  #define gasneti_weakatomic64_read(p,f)            gasneti_atomic64_read(p,f)
  #define gasneti_weakatomic64_compare_and_swap(p,oldval,newval,f)  \
            gasneti_atomic64_compare_and_swap(p,oldval,newval,f)
#else
  /* May not need any exclusion mechanism, but we still want to include any fences that
     the caller has requested, since any memory in the gasnet segment "protected" by a
     fenced atomic may be written by a network adapter.
   */

  #define _GASNETI_WEAKATOMIC_DEFN(_type,_sz)                   \
    typedef volatile uint##_sz##_t gasneti_##_type##_t; \
    typedef uint##_sz##_t gasneti_##_type##_val_t;      \
    typedef int##_sz##_t gasneti_##_type##_sval_t;

  /* Build gasneti_weakatomic_t */
  #if defined(GASNETI_FORCE_64BIT_ATOMICOPS)
    _GASNETI_WEAKATOMIC_DEFN(weakatomic,64)
    #define GASNETI_WEAKATOMIC_MAX            ((gasneti_weakatomic_val_t)0xFFFFFFFFFFFFFFFFLLU)
    #define GASNETI_WEAKATOMIC_SIGNED_MIN     ((gasneti_weakatomic_sval_t)0x8000000000000000LL)
    #define GASNETI_WEAKATOMIC_SIGNED_MAX     ((gasneti_weakatomic_sval_t)0x7FFFFFFFFFFFFFFFLL)
  #else
    _GASNETI_WEAKATOMIC_DEFN(weakatomic,32)
    #define GASNETI_WEAKATOMIC_MAX            ((gasneti_weakatomic_val_t)0xFFFFFFFFU)
    #define GASNETI_WEAKATOMIC_SIGNED_MIN     ((gasneti_weakatomic_sval_t)0x80000000)
    #define GASNETI_WEAKATOMIC_SIGNED_MAX     ((gasneti_weakatomic_sval_t)0x7FFFFFFF)
  #endif
  #define gasneti_weakatomic_init            _gasneti_scalar_atomic_init
  #define gasneti_weakatomic_signed(v)       gasneti_atomic_signed(v)
  #define gasneti_weakatomic_set(p,v,f)      GASNETI_ATOMIC_FENCED_SET(weakatomic,_gasneti_scalar_atomic_set,p,v,f)
  #define gasneti_weakatomic_increment(p,f)  GASNETI_ATOMIC_FENCED_INCDEC(weakatomic,_gasneti_scalar_atomic_increment,p,f)
  #define gasneti_weakatomic_decrement(p,f)  GASNETI_ATOMIC_FENCED_INCDEC(weakatomic,_gasneti_scalar_atomic_decrement,p,f)
  GASNETI_ATOMIC_FENCED_READ_DEFN(weakatomic,gasneti_weakatomic_read,_gasneti_scalar_atomic_read,gasneti_weakatomic_)
  GASNETI_ATOMIC_FENCED_DECTEST_DEFN(weakatomic,gasneti_weakatomic_decrement_and_test,_gasneti_scalar_atomic_decrement_and_test,gasneti_weakatomic_)
  GASNETI_ATOMIC_FENCED_CAS_DEFN(weakatomic,gasneti_weakatomic_compare_and_swap,_gasneti_scalar_atomic_compare_and_swap,gasneti_weakatomic_)
  GASNETI_ATOMIC_FENCED_ADDSUB_DEFN(weakatomic,gasneti_weakatomic_add,_gasneti_scalar_atomic_add,gasneti_weakatomic_)
  GASNETI_ATOMIC_FENCED_ADDSUB_DEFN(weakatomic,gasneti_weakatomic_subtract,_gasneti_scalar_atomic_subtract,gasneti_weakatomic_)
  #define GASNETI_HAVE_WEAKATOMIC_CAS 1
  #define GASNETI_HAVE_WEAKATOMIC_ADD_SUB 1

  /* Build gasneti_weakatomic32_t */
  _GASNETI_WEAKATOMIC_DEFN(weakatomic32,32)
  #define gasneti_weakatomic32_init        _gasneti_scalar_atomic_init
  #define gasneti_weakatomic32_set(p,v,f)  GASNETI_ATOMIC_FENCED_SET(weakatomic,_gasneti_scalar_atomic_set,p,v,f)
  GASNETI_ATOMIC_FENCED_READ_DEFN(weakatomic,gasneti_weakatomic32_read,_gasneti_scalar_atomic_read,gasneti_weakatomic32_)
  GASNETI_ATOMIC_FENCED_CAS_DEFN(weakatomic,gasneti_weakatomic32_compare_and_swap,_gasneti_scalar_atomic_compare_and_swap,gasneti_weakatomic32_)

  /* Build gasneti_weakatomic64_t */
  _GASNETI_WEAKATOMIC_DEFN(weakatomic64,64)
  #define gasneti_weakatomic64_init        _gasneti_scalar_atomic_init
  #define gasneti_weakatomic64_set(p,v,f)  GASNETI_ATOMIC_FENCED_SET(weakatomic,_gasneti_scalar_atomic_set,p,v,f)
  GASNETI_ATOMIC_FENCED_READ_DEFN(weakatomic,gasneti_weakatomic64_read,_gasneti_scalar_atomic_read,gasneti_weakatomic64_)
  GASNETI_ATOMIC_FENCED_CAS_DEFN(weakatomic,gasneti_weakatomic64_compare_and_swap,_gasneti_scalar_atomic_compare_and_swap,gasneti_weakatomic64_)
#endif

/* ------------------------------------------------------------------------------------ */
/* Configuration strings */

#if defined(GASNETI_USE_GENERIC_ATOMICOPS)
  #define GASNETI_ATOMIC_CONFIG   atomics_mutex
#elif defined(GASNETI_USE_OS_ATOMICOPS)
  #define GASNETI_ATOMIC_CONFIG   atomics_os
#else
  #define GASNETI_ATOMIC_CONFIG   atomics_native
#endif

#if defined(GASNETI_USE_GENERIC_ATOMIC32)
  #define GASNETI_ATOMIC32_CONFIG   atomic32_mutex
#elif defined(GASNETI_USE_OS_ATOMICOPS)
  #define GASNETI_ATOMIC32_CONFIG   atomic32_os
#else
  #define GASNETI_ATOMIC32_CONFIG   atomic32_native
#endif

#if defined(GASNETI_USE_GENERIC_ATOMIC64)
  #define GASNETI_ATOMIC64_CONFIG   atomic64_mutex
#elif defined(GASNETI_USE_OS_ATOMICOPS)
  #define GASNETI_ATOMIC64_CONFIG   atomic64_os
#else
  #define GASNETI_ATOMIC64_CONFIG   atomic64_native
#endif

/* ------------------------------------------------------------------------------------ */
/* The following are NOT for use outside this file: */
#undef GASNETI_ATOMIC_FENCE_SET
#undef GASNETI_ATOMIC_FENCE_READ
#undef GASNETI_ATOMIC_FENCE_RMW

/* ------------------------------------------------------------------------------------ */
#endif

/*  $Archive:: /Ti/GASNet/template-conduit/gasnet_core_help.h             $
 *     $Date: 2003/04/28 20:12:41 $
 * $Revision: 1.7 $
 * Description: GASNet lapi conduit core Header Helpers (Internal code, not for client use)
 * Copyright 2002, Dan Bonachea <bonachea@cs.berkeley.edu>
 * Terms of use are as specified in license.txt
 */

#ifndef _IN_GASNET_H
  #error This file is not meant to be included directly- clients should include gasnet.h
#endif

#ifndef _GASNET_CORE_HELP_H
#define _GASNET_CORE_HELP_H

BEGIN_EXTERNC

#include <gasnet_help.h>
#include <lapi.h>
#include <sys/atomic_op.h>
#include <gasnet_atomicops.h>

/* we dont need no stinkin Interrupt Based Handlers in LAPI */
#define GASNETC_USE_IBH 0

/* NOTE: this should be dependent on whether we compile in
 * 32 or 64 bit mode
 */
#define GASNETC_AM_MAX_ARGS 16

/* This should be the size of the largest uhdr argument LAPI will
 * allow.  Note that this value is only availible from the LAPI
 * at runtime.  In future releases of LAPI it may have to be
 * adjusted.
 */
#if defined(__64BIT__)
#define GASNETC_UHDR_SIZE 908
#else
#define GASNETC_UHDR_SIZE 944
#endif

/* The max size of a medium message.  Can really be arbitrary size
 * but the receiving task must malloc space for the incoming
 * message.  Note that messages that fit into a single token
 * are optimized.
 */
#define GASNETC_AM_MAX_MEDIUM 16384

/* In 32 bit mode, this is 2^31 - 1 bytes.  */
#define GASNETC_AM_MAX_LONG 2147483647

/* stuff needed for the BLOCKUNTIL macro */
typedef enum {
    gasnetc_Interrupt = 0,
    gasnetc_Polling
} gasnetc_lapimode_t;

extern lapi_handle_t      gasnetc_lapi_context;
extern gasnetc_lapimode_t gasnetc_lapi_default_mode;

extern gasnet_node_t gasnetc_mynode;
extern gasnet_node_t gasnetc_nodes;

/* --------------------------------------------------------------------
 * A simple spinlock implementation
 * --------------------------------------------------------------------
 */
#ifndef GASNETC_USE_SPINLOCKS
/* default to using spinlocks over pthread mutex */
#define GASNETC_USE_SPINLOCKS 1
#endif

#if GASNETC_USE_SPINLOCKS
typedef volatile int gasnetc_spinlock_t;
#define GASNETC_SPINLOCK_INITIALIZER 0
/* NOTE: Make these inline functions that always return 0 to
 * match the use of the corresponding pthread_mutex functions.
 */
GASNET_INLINE_MODIFIER(gasnetc_spinlock_init)
int gasnetc_spinlock_init(gasnetc_spinlock_t *lock) {
    *lock = 0;
    gasneti_local_membar();
    return 0;
}

#define gasnetc_spinlock_destroy(lock) 0    

GASNET_INLINE_MODIFIER(gasnetc_spinlock_lock)
int gasnetc_spinlock_lock(gasnetc_spinlock_t *lock) {
      int avail = 0;
      int locked = 1;
      while (! compare_and_swap( (atomic_p)lock, &avail, locked ) ) {
	  assert(avail == 1);
          avail = 0;
      }
      return 0;
}
#if 1
GASNET_INLINE_MODIFIER(gasnetc_spinlock_unlock)
int gasnetc_spinlock_unlock(gasnetc_spinlock_t *lock) {
    int avail = 0;
    int locked = 1;
    gasneti_local_membar();
    if (!compare_and_swap( (atomic_p)lock, &locked, avail ) )
          assert(0); /* this should not happen */
    return 0;
}
#else
GASNET_INLINE_MODIFIER(gasnetc_spinlock_unlock)
int gasnetc_spinlock_unlock(gasnetc_spinlock_t *lock) {
    assert( *lock == 1 );
    *lock = 0;
    gasneti_local_membar();
    return 0;
}
#endif

#else  /* Use pthread mutex for spinlock */
typedef pthread_mutex_t gasnetc_spinlock_t;
#define GASNETC_SPINLOCK_INITIALIZER PTHREAD_MUTEX_INITIALIZER
#define gasnetc_spinlock_init(lock) pthread_mutex_init((lock), NULL)
#define gasnetc_spinlock_destroy(lock) pthread_mutex_destroy((lock))
#define gasnetc_spinlock_lock(lock) pthread_mutex_lock((lock))
#define gasnetc_spinlock_unlock(lock) pthread_mutex_unlock((lock))
#endif


END_EXTERNC

#endif

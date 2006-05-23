/*   $Source: /Users/kamil/work/gasnet-cvs2/gasnet/gasnet_membar.h,v $
 *     $Date: 2006/05/23 08:48:17 $
 * $Revision: 1.104.8.2 $
 * Description: GASNet header for portable memory barrier operations
 * Copyright 2002, Dan Bonachea <bonachea@cs.berkeley.edu>
 * Terms of use are as specified in license.txt
 */

#if !defined(_IN_GASNET_TOOLS_H) && !defined(_IN_GASNET_H)
  #error This file is not meant to be included directly- clients should include gasnet.h or gasnet_tools.h
#endif

#ifndef _GASNET_MEMBAR_H
#define _GASNET_MEMBAR_H

/* ------------------------------------------------------------------------------------ */
/* portable memory barrier support */

/*
 gasneti_local_wmb:
   A local memory write barrier - ensure all stores to local mem from this thread are
   globally completed across this SMP before issuing any subsequent loads or stores.
   (i.e. all loads issued from any CPU subsequent to this call
      returning will see the new value for any previously issued
      stores from this proc, and any subsequent stores from this CPU
      are guaranteed to become globally visible after all previously issued
      stores from this CPU)
   This must also include whatever is needed to prevent the compiler from reordering
   loads and stores across this point.

 gasneti_local_rmb:
   A local memory read barrier - ensure all subsequent loads from local mem from this thread
   will observe previously issued stores from any CPU which have globally completed.  
   For instance, on the Alpha this ensures
   that queued cache invalidations are processed and on the PPC this discards any loads
   that were executed speculatively.
   This must also include whatever is needed to prevent the compiler from reordering
   loads and stores across this point.
 
 gasneti_local_mb:
   A "full" local memory barrer.  This is equivalent to both a wmb() and rmb().
   All oustanding loads and stores must be completed before any subsequent ones
   may begin.

 gasneti_compiler_fence:
   A barrier to compiler optimizations that would reorder any memory references across
   this point in the code.

  Note that for all four memory barriers, we require only that a given architecture's
  "normal" loads and stores are ordered as required.  "Extended" instructions such as
  MMX, SSE, SSE2, Altivec and vector ISAs on various other machines often bypass some
  or all of the machine's memory hierarchy and therefore may not be ordered by the same
  instructions.  Authors of MMX-based memcpy and similar code must therefore take care
  to add appropriate flushes to their code.

  To reduce duplicated assembly code and needless empty macros the following are the
  default behaviors unless a given arch/compiler defines something else.
   + gasneti_compiler_fence() defaults to an empty "volatile" asm section
   + gasneti_local_wmb() is implemented on all architectures
   + gasneti_local_rmb() defaults to just a compiler fence, as only a few architectures
       need more than this
   + gasneti_local_mb() defaults to { gasneti_local_wmb(); gasneti_local_rmb(); }.
       Only a few architectures (notable Alpha) can do this less expensively.

  For more info on memory barriers: http://gee.cs.oswego.edu/dl/jmm/cookbook.html
 */
#include <gasnet_asm.h>

#if PLATFORM_COMPILER_SUN_CXX || \
   (PLATFORM_COMPILER_HP_CXX && PLATFORM_ARCH_PARISC) || \
   (PLATFORM_COMPILER_PGI_CXX && PGI_WITH_REAL_ASM)
  /* no inline assembly in these C++ compilers, so pay a function call overhead */
  #define GASNETI_USING_SLOW_MEMBARS 1
/* ------------------------------------------------------------------------------------ */
#elif PLATFORM_ARCH_SPARC
  #if defined(__sparcv9) || defined(__sparcv9cpu) || defined(GASNETI_ARCH_ULTRASPARC) /* SPARC v9 ISA */
    GASNETI_INLINE(gasneti_local_wmb)
    void gasneti_local_wmb(void) {
      /* For TSO SPARCs this is technically oversynced, but costs us nothing extra. */
      GASNETI_ASM("membar #StoreLoad | #StoreStore"); 
    }
    GASNETI_INLINE(_gasneti_local_mb)
    void _gasneti_local_mb(void) {
      /* For TSO SPARCs this is technically oversynced, but costs us nothing extra. */
      GASNETI_ASM("membar #LoadStore | #LoadLoad | #StoreLoad | #StoreStore");
    }
    #define gasneti_local_mb() _gasneti_local_mb()
    #ifdef GASNETI_ARCH_SPARC_RMO /* Provide an option for SPARC RMO mode (no OS support?) */
      GASNETI_INLINE(_gasneti_local_rmb)
      void _gasneti_local_rmb(void) {
        GASNETI_ASM("membar #LoadStore | #LoadLoad"); 
      }
      #define gasneti_local_rmb() _gasneti_local_rmb()
      #define GASNETI_MB_IS_SUM	/* close enough, since the alternative involves an extra branch */
    #else /* In the default TSO mode, RMB is a no-op */
      #define GASNETI_WMB_IS_MB
    #endif
  #else /* SPARC v7/8 */
    GASNETI_INLINE(gasneti_local_wmb)
    void gasneti_local_wmb(void) {
      GASNETI_ASM("stbar"); /* SPARC store barrier */
    }
  #endif
/* ------------------------------------------------------------------------------------ */
#elif PLATFORM_ARCH_MIPS
  #if PLATFORM_COMPILER_SGI
    /* bug1534: issue a full architectural sync for the compiler fence - 
       this is overkill, but the compiler seems to lack any stand-alone optimization
       barrier, and the other synchronizing intrinsics (atomics) are even more expensive */
    #define gasneti_compiler_fence() __synchronize()
    #define gasneti_local_wmb() __synchronize()
    #define gasneti_local_rmb() __synchronize()
    #define gasneti_local_mb()  __synchronize()
    #define GASNETI_WMB_IS_MB
    #define GASNETI_RMB_IS_MB
  #else
    GASNETI_INLINE(_gasneti_local_mb)
    void _gasneti_local_mb(void) {
      GASNETI_ASM("sync");  /* MIPS II+ memory barrier */ 
    }
    #define gasneti_local_mb()  _gasneti_local_mb()
    #define gasneti_local_wmb() _gasneti_local_mb()
    #define gasneti_local_rmb() _gasneti_local_mb()
    #define GASNETI_WMB_IS_MB
    #define GASNETI_RMB_IS_MB
  #endif
/* ------------------------------------------------------------------------------------ */
#elif PLATFORM_ARCH_PARISC /* HP PA-RISC */
   GASNETI_INLINE(gasneti_local_wmb)
   void gasneti_local_wmb(void) {
     #if PLATFORM_COMPILER_HP_C
       _flush_globals();
     #endif
     GASNETI_ASM("SYNC");  /* PA RISC load/store ordering */ 
   }
   #if PLATFORM_COMPILER_HP_C
     #if 0
       /* HP C doesn't like an empty asm statement */
       #define gasneti_compiler_fence() _asm("OR",0,0,0) /* NOP */
     #else
       #define gasneti_compiler_fence() _flush_globals() /* compiler intrinsic forces spills */
     #endif
   #endif
/* ------------------------------------------------------------------------------------ */
#elif PLATFORM_ARCH_X86
   GASNETI_INLINE(gasneti_local_wmb)
   void gasneti_local_wmb(void) {
     /* The instruction here can be any locked read-modify-write operation.
      * This one is chosen because it does not change any registers and is
      * available on all the Intel and clone CPUs.  Also, since it touches
      * only the stack, it is highly unlikely to result in extra coherence
      * traffic.
      * Unfortunately, all read-modify-write operations also set condition
      * codes.  So, we have an extra messy case for gcc, icc, etc.
      */
     #if (PLATFORM_COMPILER_PGI && !PGI_WITH_REAL_ASM) || PLATFORM_COMPILER_SUN_C
       GASNETI_ASM("lock; addl $0,0(%esp)");
     #elif PLATFORM_COMPILER_GNU || PLATFORM_COMPILER_INTEL || \
          (PLATFORM_COMPILER_PGI && PGI_WITH_REAL_ASM)
       /* For gcc, icc and other gcc look-alikes */
       __asm__ __volatile__ ("lock; addl $0,0(%%esp)" : : : "memory", "cc");
     #else
       /* Others? */
       GASNETI_ASM("lock; addl $0,0(%%esp)");
     #endif
   }
/* ------------------------------------------------------------------------------------ */
#elif PLATFORM_ARCH_X86_64 /* Athlon/Opteron */
   GASNETI_INLINE(gasneti_local_wmb)
   void gasneti_local_wmb(void) {
     GASNETI_ASM("sfence");
   }
   GASNETI_INLINE(_gasneti_local_rmb)
   void _gasneti_local_rmb(void) {
     GASNETI_ASM("lfence");
   }
   #define gasneti_local_rmb() _gasneti_local_rmb()
   GASNETI_INLINE(_gasneti_local_mb)
   void _gasneti_local_mb(void) {
     GASNETI_ASM("mfence");
   }
   #define gasneti_local_mb() _gasneti_local_mb()
/* ------------------------------------------------------------------------------------ */
#elif PLATFORM_ARCH_IA64 /* Itanium */
    /* Empirically observed that IA64 requires a full "mf" for both wmb and rmb (see bug 1000).
     * The reason is that the Itanium memeory model only ensures ordering in one direction when
     * using st.rel or ld.acq.  In particular, they implement the minimum required for proper
     * mutex implementation.  While preventing loads and stores from moving OUT of the creitical
     * section, this still allows for loads before the lock and stored after the unlock to reorder
     * INTO the critical section.  We need more than that.
     */
   #if PLATFORM_COMPILER_INTEL
      /* Intel compiler's inline assembly broken on Itanium (bug 384) - use intrinsics instead */
      #include <ia64intrin.h>
      #define gasneti_compiler_fence() \
             __memory_barrier() /* compiler optimization barrier */
      #define gasneti_local_wmb() do {      \
        gasneti_compiler_fence();           \
        __mf();  /* memory fence instruction */  \
      } while (0)
      #define gasneti_local_rmb() gasneti_local_wmb()
      #define gasneti_local_mb()  gasneti_local_wmb()
      #define GASNETI_RMB_IS_MB
      #define GASNETI_WMB_IS_MB
   #elif PLATFORM_COMPILER_HP
      #include <machine/sys/inline.h>
      /* HP compilers have no inline assembly on Itanium - use intrinsics */
      #define gasneti_compiler_fence() \
         _Asm_sched_fence((_Asm_fence)(_UP_MEM_FENCE | _DOWN_MEM_FENCE)) 
      #define gasneti_local_mb() _Asm_mf((_Asm_fence)(_UP_MEM_FENCE | _DOWN_MEM_FENCE))
      #define gasneti_local_wmb gasneti_local_mb
      #define gasneti_local_rmb gasneti_local_mb
      #define GASNETI_RMB_IS_MB
      #define GASNETI_WMB_IS_MB
   #else
      #define gasneti_local_wmb() GASNETI_ASM("mf")
      #define gasneti_local_rmb() gasneti_local_wmb()
      #define gasneti_local_mb()  gasneti_local_wmb()
      #define GASNETI_RMB_IS_MB
      #define GASNETI_WMB_IS_MB
   #endif
/* ------------------------------------------------------------------------------------ */
#elif PLATFORM_ARCH_POWERPC
 #if PLATFORM_COMPILER_XLC
   /* VisualAge C compiler (mpcc_r) has no support for inline symbolic assembly
    * you have to hard-code the opcodes in a pragma that defines an assembly 
    * function - see /usr/include/sys/atomic_op.h on AIX for examples
    * opcodes can be aquired by placing the mnemonics in inline.s and running:
    * as -sinline.lst inline.s
    */ 

   /* "lwsync" = "sync 1", executed as "sync" on older CPUs */
   #define GASNETI_PPC_WMB_ASM "7c2004ac"
   #ifdef GASNETI_TUNE_PPC970
     /* On PPC970 (and nowhere else so far) lwsync is faster than isync */
     #define GASNETI_PPC_RMB_ASM GASNETI_PPC_WMB_ASM
     #define GASNETI_RMB_IS_MB
   #else
     /* "isync" (instruction sync to squash speculative loads) */
     #define GASNETI_PPC_RMB_ASM "4c00012c"
   #endif

   #pragma mc_func _gasneti_do_wmb { GASNETI_PPC_WMB_ASM }
   #pragma reg_killed_by _gasneti_do_wmb
   #define gasneti_local_wmb() _gasneti_do_wmb()

   #pragma mc_func _gasneti_do_rmb { GASNETI_PPC_RMB_ASM }
   #pragma reg_killed_by _gasneti_do_rmb
   #define gasneti_local_rmb() _gasneti_do_rmb()

   #define gasneti_local_mb() _gasneti_do_wmb()
   #define GASNETI_WMB_IS_MB

   #pragma mc_func _gasneti_do_compilerfence { "" }
   #pragma reg_killed_by _gasneti_do_compilerfence
   #define gasneti_compiler_fence() _gasneti_do_compilerfence()
 #else
   /* "lwsync" = "sync 1", executed as "sync" on older CPUs */
   /* XXX: Can't count on older assemblers to recognize "lwsync" mnemonic */
   #define GASNETI_PPC_WMB_ASM ".long 0x7c2004ac"
   #ifdef GASNETI_TUNE_PPC970
     /* On PPC970 (and nowhere else so far) lwsync is faster than isync */
     #define GASNETI_PPC_RMB_ASM GASNETI_PPC_WMB_ASM
     #define GASNETI_RMB_IS_MB
   #else
     /* "isync" (instruction sync to squash speculative loads) */
     #define GASNETI_PPC_RMB_ASM "isync"
   #endif

   GASNETI_INLINE(gasneti_local_wmb)
   void gasneti_local_wmb(void) {
     GASNETI_ASM(GASNETI_PPC_WMB_ASM);
   }

   GASNETI_INLINE(_gasneti_local_rmb)
   void _gasneti_local_rmb(void) {
     GASNETI_ASM(GASNETI_PPC_RMB_ASM);
   }
   #define gasneti_local_rmb() _gasneti_local_rmb()

   #define gasneti_local_mb() gasneti_local_wmb()
   #define GASNETI_WMB_IS_MB
 #endif
/* ------------------------------------------------------------------------------------ */
#elif PLATFORM_ARCH_ALPHA
 #if 1 /* tested on OSF1, LINUX, FreeBSD */
   GASNETI_INLINE(gasneti_local_wmb)
   void gasneti_local_wmb(void) {
     GASNETI_ASM("wmb");
   }
   GASNETI_INLINE(_gasneti_local_rmb)
   void _gasneti_local_rmb(void) {
     GASNETI_ASM("mb");
   }
   #define gasneti_local_rmb() _gasneti_local_rmb()
   GASNETI_INLINE(_gasneti_local_mb)
   void _gasneti_local_mb(void) {
     GASNETI_ASM("mb");
   }
   #define gasneti_local_mb() _gasneti_local_mb()
   #define GASNETI_RMB_IS_MB
 #elif PLATFORM_COMPILER_COMPAQ && 0
   /* Use compaq C built-ins */
   /* Note this is heavier weight than required */
   #include <machine/builtins.h>
   #define gasneti_local_wmb() __MB()
   #define gasneti_local_rmb() __MB()
   #define gasneti_local_mb() __MB()
   #define GASNETI_RMB_IS_MB
   #define GASNETI_WMB_IS_MB
 #endif
/* ------------------------------------------------------------------------------------ */
#elif PLATFORM_ARCH_CRAYT3E /* Takes care of e-regs also */
   #include <intrinsics.h>
   #define gasneti_local_wmb() _memory_barrier()
   #define gasneti_local_rmb() _memory_barrier()
   #define gasneti_local_mb() _memory_barrier()
   #define gasneti_compiler_fence() do { int volatile x = 0; } while (0)
   #define GASNETI_RMB_IS_MB
   #define GASNETI_WMB_IS_MB
/* ------------------------------------------------------------------------------------ */
#elif PLATFORM_ARCH_CRAYX1
  GASNETI_INLINE(_gasneti_compiler_fence)
  void _gasneti_compiler_fence(void) {
    static int volatile x;
    x = 1;
  }
  #define gasneti_compiler_fence _gasneti_compiler_fence
  #pragma _CRI suppress _gasneti_compiler_fence
  /* bug1195: Many memory barrier intrinsics on the X1, but none seem to actually
   * deliver what we need in a local (scalar-scalar) membar. Not even gsync is sufficient.
   * shmem_quiet and pthread_mutex_lock/unlock are both sufficient, but shmem_quiet is cheaper.
   */
  #if 1
    #include <mpp/shmem.h>
    #define gasneti_local_mb() shmem_quiet()
    #define gasneti_local_rmb  gasneti_local_mb
    #define gasneti_local_wmb  gasneti_local_mb
    #define GASNETI_RMB_IS_MB
    #define GASNETI_WMB_IS_MB
  #elif 1
    #include <pthread.h>
    GASNETI_INLINE(gasneti_local_wmb)
    void gasneti_local_mb(void) {
     #if 1
      pthread_mutex_t m = PTHREAD_MUTEX_INITIALIZER;
      pthread_mutex_lock(&m);
      pthread_mutex_unlock(&m);
     #else
      pthread_testcancel(); /* also seems to work */
     #endif
    }
    #define gasneti_local_rmb gasneti_local_mb
    #define gasneti_local_wmb gasneti_local_mb
    #define GASNETI_RMB_IS_MB
    #define GASNETI_WMB_IS_MB
  #else /* NOT safe */
    #define gasneti_local_wmb gasneti_compiler_fence
  #endif
/* ------------------------------------------------------------------------------------ */
#elif PLATFORM_ARCH_MTA
   #if 0 /* causes warnings */
     #define gasneti_compiler_fence() (_Pragma("mta fence"))
   #else
     GASNETI_INLINE(_gasneti_compiler_fence)
     void _gasneti_compiler_fence(void) {
       (void)0;
       #pragma mta fence
       (void)0;
     }
     #define gasneti_compiler_fence() _gasneti_compiler_fence()
   #endif
   /* MTA has no caches or write buffers - just need a compiler reordering fence */
   #define gasneti_local_wmb() gasneti_compiler_fence()
   #define gasneti_local_rmb() gasneti_compiler_fence()
   #define gasneti_local_mb()  gasneti_compiler_fence()
   #define GASNETI_RMB_IS_MB
   #define GASNETI_WMB_IS_MB
/* ------------------------------------------------------------------------------------ */
#elif PLATFORM_ARCH_NECSX
   GASNETI_INLINE(gasneti_local_wmb)
   void gasneti_local_wmb(void) {
     /* TODO: probably need more here */
     static int volatile x;
     x = 1;
     /* GASNETI_ASM("nop"); - leads to "FATAL COMPILER ERROR, Unknown statement. c++: Internal Error: Please report." */
   }
/* ------------------------------------------------------------------------------------ */
#elif PLATFORM_ARCH_MICROBLAZE
   /* no SMP support */
   #define gasneti_local_wmb() gasneti_compiler_fence()
   #define gasneti_local_rmb() gasneti_compiler_fence()
   #define gasneti_local_mb()  gasneti_compiler_fence()
   #define GASNETI_RMB_IS_MB
   #define GASNETI_WMB_IS_MB
/* ------------------------------------------------------------------------------------ */
#else
 #error unknown CPU - dont know how to do a local memory barrier for your CPU/OS
#endif

#if GASNETI_USING_SLOW_MEMBARS
  #ifndef __cplusplus
    #error Slow membars are only a hack-around for C++ compilers lacking inline assembly support
  #endif
  GASNETI_EXTERNC void gasneti_slow_local_wmb();
  #define gasneti_local_wmb() gasneti_slow_local_wmb()
  GASNETI_EXTERNC void gasneti_slow_local_rmb();
  #define gasneti_local_rmb() gasneti_slow_local_rmb()
  GASNETI_EXTERNC void gasneti_slow_local_mb();
  #define gasneti_local_mb() gasneti_slow_local_mb()
  GASNETI_EXTERNC void gasneti_slow_compiler_fence();
  #define gasneti_compiler_fence() gasneti_slow_compiler_fence()
#endif

/* ------------------------------------------------------------------------------------ */
/* Default gasneti_compiler_fence() */
#ifndef gasneti_compiler_fence
  #define gasneti_compiler_fence() GASNETI_ASM("")
#endif

/* Default gasneti_local_rmb() */
#ifndef gasneti_local_rmb
  #define gasneti_local_rmb() gasneti_compiler_fence()
  #define GASNETI_RMB_IS_DEFAULT 1
#endif

/* NO Default for gasneti_local_wmb() to avoid mistakes - it must be explicitly provided */

/* Default gasneti_local_mb() */
#ifndef gasneti_local_mb
  #define gasneti_local_mb() do { gasneti_local_wmb(); gasneti_local_rmb(); } while (0)
  #define GASNETI_MB_IS_DEFAULT 1
#endif


/* ------------------------------------------------------------------------------------ */
/* Properties of the memory barriers (as boolean preprocessor tokens)
	GASNETI_RMB_IS_MB	rmb() is sufficient for mb()
	GASNETI_WMB_IS_MB	wmb() is sufficient for mb()
	GASNETI_MB_IS_SUM	wmb()+rmb() is mb(), as opposed to a double mb()
   These tokens are used by the fenced atomics to produce minimal code.
   What follows "normalizes" these tokens to 0 or 1 and applies defaults.
   The defaults are always safe, but perhaps sub-optimal.
   The defns of the membars should define non-defaults appropriately.

   THESE ARE *NOT* INTENDED FOR GENERAL USE IN CONDUIT CODE.
 */
#ifndef GASNETI_RMB_IS_MB
  /* Default: assume rmb() is not a full mb(). */
  #define GASNETI_RMB_IS_MB	0
#else
  #undef GASNETI_RMB_IS_MB
  #define GASNETI_RMB_IS_MB	1
#endif
#ifndef GASNETI_WMB_IS_MB
  /* Default: assume wmb() is a full mb() if using default impls. */
  #if defined(GASNETI_MB_IS_DEFAULT) && defined(GASNETI_RMB_IS_DEFAULT)
    #define GASNETI_WMB_IS_MB	1
  #else
    #define GASNETI_WMB_IS_MB	0
  #endif
#else
  #undef GASNETI_WMB_IS_MB
  #define GASNETI_WMB_IS_MB	1
#endif
#ifndef GASNETI_MB_IS_SUM
  /* Default: assume mb() = rmb() + wmb() if using the default impl. */
  #if defined(GASNETI_MB_IS_DEFAULT) 
    #define GASNETI_MB_IS_SUM	1
  #else
    #define GASNETI_MB_IS_SUM	0
  #endif
#else
  #undef GASNETI_MB_IS_SUM
  #define GASNETI_MB_IS_SUM	1
#endif
#undef GASNETI_MB_IS_DEFAULT
#undef GASNETI_RMB_IS_DEFAULT
 
/* ------------------------------------------------------------------------------------ */
/* Conditionally compiled memory barriers -

   gasneti_sync_{reads,writes,mem} are like gasneti_local_{rmb,wmb,mb} except that when
   not using threads we want them to compile away to nothing, and when compiling for
   threads on a uniprocessor we want only a compiler optimization barrier

   Note these should *only* be used when synchronizing node-private memory
   between local pthreads - they are not guaranteed to provide synchonization with
   respect to put/gets by remote nodes (in the presence of RDMA), and therefore
   are generally unsuitable for synchronizing memory locations in the gasnet segment
*/

#ifndef gasneti_sync_writes
  #if GASNET_SEQ && !GASNETI_THREADS
    #define gasneti_sync_writes() ((void)0)
  #elif GASNETI_UNI_BUILD
    #define gasneti_sync_writes() gasneti_compiler_fence()
  #else
    #define gasneti_sync_writes() gasneti_local_wmb()
  #endif
#endif

#ifndef gasneti_sync_reads
  #if GASNET_SEQ && !GASNETI_THREADS
    #define gasneti_sync_reads() ((void)0)
  #elif GASNETI_UNI_BUILD
    #define gasneti_sync_reads() gasneti_compiler_fence()
  #else
    #define gasneti_sync_reads() gasneti_local_rmb()
  #endif
#endif

#ifndef gasneti_sync_mem
  #if GASNET_SEQ && !GASNETI_THREADS
    #define gasneti_sync_mem() ((void)0)
  #elif GASNETI_UNI_BUILD
    #define gasneti_sync_mem() gasneti_compiler_fence()
  #else
    #define gasneti_sync_mem() gasneti_local_mb()
  #endif
#endif

/* ------------------------------------------------------------------------------------ */

#ifndef gasneti_spinloop_hint
 #if defined(GASNETI_PAUSE_INSTRUCTION)
   /* Pentium 4 processors get measurably better performance when a "pause" instruction
    * is inserted in spin-loops - this instruction is documented as a "spin-loop hint"
    * which avoids a memory hazard stall on spin loop exit and reduces power consumption
    * Other Intel CPU's treat this instruction as a no-op
    *
    * IA64 includes a "hint" for use in spinloops
   */
   #define gasneti_spinloop_hint() GASNETI_ASM(GASNETI_PAUSE_INSTRUCTION)
 #elif PLATFORM_ARCH_IA64 && PLATFORM_COMPILER_INTEL && 0 /* DISABLED */
   /* Intel compiler's inline assembly broken on Itanium (bug 384) - use intrinsics instead */
   #include <ia64intrin.h>
   #define gasneti_spinloop_hint() __hint(__hint_pause)
 #else
   #define gasneti_spinloop_hint() ((void)0)
 #endif
#endif

/* ------------------------------------------------------------------------------------ */

#endif

/*   $Source: /Users/kamil/work/gasnet-cvs2/gasnet/gasnet_membar.h,v $
 *     $Date: 2006/03/23 07:06:22 $
 * $Revision: 1.86.2.4 $
 * Description: GASNet header for portable memory barrier operations
 * Copyright 2002, Dan Bonachea <bonachea@cs.berkeley.edu>
 * Terms of use are as specified in license.txt
 */

#if !defined(_IN_GASNET_TOOLS_H) && !defined(_IN_GASNET_H) && !defined(_IN_CONFIGURE)
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
#ifdef __GNUC__
  #define GASNETI_ASM(mnemonic) __asm__ __volatile__ (mnemonic : : : "memory")
#elif defined(__INTEL_COMPILER)
  #define GASNETI_ASM(mnemonic) __asm__ __volatile__ (mnemonic : : : "memory")
#elif defined(__PGI) /* note this requires compiler flag -Masmkeyword */
  #define GASNETI_ASM(mnemonic) asm(mnemonic)
#elif defined(__DECC) || defined(__DECCXX)
  #include <c_asm.h>
  #define GASNETI_ASM(mnemonic) asm(mnemonic)
#elif defined(MIPSPRO_COMPILER)
  #define GASNETI_ASM(mnemonic)  ERROR_NO_INLINE_ASSEMBLY_AVAIL /* not supported or used */
#elif defined(__SUNPRO_C) /* Sun C works, Sun C++ lacks inline assembly support (man inline) */
  #define GASNETI_ASM(mnemonic)  __asm(mnemonic)
#elif defined(_SX)  
  #define GASNETI_ASM(mnemonic)  asm(mnemonic)
#elif defined(__HP_cc) /* HP C */
  #define GASNETI_ASM(mnemonic)  _asm(mnemonic)
#elif defined(__HP_aCC)
  #define GASNETI_ASM(mnemonic)  ERROR_NO_INLINE_ASSEMBLY_AVAIL /* not supported or used */
#elif defined(__SUNPRO_CC)
  #define GASNETI_ASM(mnemonic)  ERROR_NO_INLINE_ASSEMBLY_AVAIL /* not supported or used */
#elif defined(__xlC__)  
  #define GASNETI_ASM(mnemonic)  ERROR_NO_INLINE_ASSEMBLY_AVAIL /* not supported or used */
#elif defined(_CRAY)  
  #define GASNETI_ASM(mnemonic)  ERROR_NO_INLINE_ASSEMBLY_AVAIL /* not supported or used */
#elif defined(__MTA__)  
  #define GASNETI_ASM(mnemonic)  ERROR_NO_INLINE_ASSEMBLY_AVAIL /* not supported or used */
#else
  #error "Don't know how to use inline assembly for your compiler"
#endif


#ifdef _IN_CONFIGURE
  /* the file effectively ends here */
#else

#if defined(__SUNPRO_CC) || defined(__HP_aCC)
  /* no inline assembly in these C++ compilers, so pay a function call overhead */
  #define GASNETI_USING_SLOW_ATOMICS 1
#elif defined(__sparc__) || defined(__sparc) || defined(sparc)
  #if defined(__sparcv9) || defined(__sparcv9cpu) || defined(GASNETI_ARCH_SPARCV9) /* SPARC v9 */
    GASNETI_INLINE(gasneti_local_wmb)
    void gasneti_local_wmb(void) {
      GASNETI_ASM("membar #StoreLoad | #StoreStore"); 
    }
    GASNETI_INLINE(_gasneti_local_rmb)
    void _gasneti_local_rmb(void) {
      GASNETI_ASM("membar #LoadStore | #LoadLoad"); 
    }
    #define gasneti_local_rmb() _gasneti_local_rmb()
    GASNETI_INLINE(_gasneti_local_mb)
    void _gasneti_local_mb(void) {
      GASNETI_ASM("membar #LoadStore | #LoadLoad | #StoreLoad | #StoreStore");
    }
    #define gasneti_local_mb() _gasneti_local_mb()
    #define GASNETI_MB_IS_SUM	/* close enough, since the alternative involves an extra branch */
  #else /* SPARC v7/8 */
    GASNETI_INLINE(gasneti_local_wmb)
    void gasneti_local_wmb(void) {
      GASNETI_ASM("stbar"); /* SPARC store barrier */
    }
  #endif
#elif defined(__mips__) || defined(__mips) || defined(mips) || defined(_MIPS_ISA)
  #if defined(MIPSPRO_COMPILER)
    GASNETI_INLINE(_gasneti_compiler_fence)
    void _gasneti_compiler_fence(void) {
      volatile int x; x = 1;
    }
    #define gasneti_compiler_fence() _gasneti_compiler_fence()
    #define gasneti_local_wmb() __synchronize()
    #define gasneti_local_mb()  __synchronize()
    #define GASNETI_WMB_IS_MB
  #else
    GASNETI_INLINE(gasneti_local_wmb)
    void gasneti_local_wmb(void) {
      GASNETI_ASM("sync");  /* MIPS II+ memory barrier */ 
    }
  #endif
#elif defined(_PA_RISC1_1) || defined(__hppa) /* HP PA-RISC */
   GASNETI_INLINE(gasneti_local_wmb)
   void gasneti_local_wmb(void) {
     #if defined(__HP_cc) 
       _flush_globals();
     #endif
     GASNETI_ASM("SYNC");  /* PA RISC load/store ordering */ 
   }
   #if defined(__HP_cc) 
     #if 0
       /* HP C doesn't like an empty asm statement */
       #define gasneti_compiler_fence() _asm("OR",0,0,0) /* NOP */
     #else
       #define gasneti_compiler_fence() _flush_globals() /* compiler intrinsic forces spills */
     #endif
   #endif
#elif defined(__i386__) || defined(__i386) || defined(i386) || \
      defined(__i486__) || defined(__i486) || defined(i486) || \
      defined(__i586__) || defined(__i586) || defined(i586) || \
      defined(__i686__) || defined(__i686) || defined(i686)
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
     #if defined(__PGI) || defined(__SUNPRO_C)
       GASNETI_ASM("lock; addl $0,0(%esp)");
     #elif defined(__GNUC__) || defined(__INTEL_COMPILER)
       /* For gcc, icc and other gcc look-alikes */
       __asm__ __volatile__ ("lock; addl $0,0(%%esp)" : : : "memory", "cc");
     #else
       /* Others? */
       GASNETI_ASM("lock; addl $0,0(%%esp)");
     #endif
   }
#elif defined(__x86_64__) /* Athlon/Opteron */
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
#elif defined(__ia64__) || defined(__ia64) /* Itanium */
   #ifdef __INTEL_COMPILER
      /* Intel compiler's inline assembly broken on Itanium (bug 384) - use intrinsics instead */
      #include <ia64intrin.h>
      #define gasneti_compiler_fence() \
             __memory_barrier() /* compiler optimization barrier */
      #define gasneti_local_wmb() do {      \
        gasneti_compiler_fence();           \
        __mf();  /* memory fence instruction */  \
      } while (0)
      /* bug 1000: empirically observed that IA64 requires a full memory fence for both wmb and rmb */
      #define gasneti_local_rmb() gasneti_local_wmb()
      #define gasneti_local_mb()  gasneti_local_wmb()
      #define GASNETI_RMB_IS_MB
      #define GASNETI_WMB_IS_MB
   #elif defined(__HP_cc) || defined(__HP_aCC)
      #include <machine/sys/inline.h>
      /* HP compilers have no inline assembly on Itanium - use intrinsics */
      #define gasneti_compiler_fence() \
         _Asm_sched_fence((_Asm_fence)(_UP_MEM_FENCE | _DOWN_MEM_FENCE)) 
      #define gasneti_local_mb() _Asm_mf((_Asm_fence)(_UP_MEM_FENCE | _DOWN_MEM_FENCE))
      /* bug 1000: empirically observed that IA64 requires a full memory fence for both wmb and rmb */
      #define gasneti_local_wmb gasneti_local_mb
      #define gasneti_local_rmb gasneti_local_mb
      #define GASNETI_RMB_IS_MB
      #define GASNETI_WMB_IS_MB
   #else
    #if 1
      #define gasneti_local_wmb() GASNETI_ASM("mf")
      /* bug 1000: empirically observed that IA64 requires a full memory fence for both wmb and rmb */
      #define gasneti_local_rmb() gasneti_local_wmb()
      #define gasneti_local_mb()  gasneti_local_wmb()
      #define GASNETI_RMB_IS_MB
      #define GASNETI_WMB_IS_MB
    #else
      /* according to section 4.4.7 in:
         "Intel Itanium Architecture Software Developer's Manual, Vol 2 System Architecture"
         the following should work, but for some reason it does not
       */
      GASNETI_INLINE(_gasneti_local_wmb)
      void _gasneti_local_wmb() {
        int tmp;
        __asm__ __volatile__(
                ";;\n\tst4.rel %0 = r0\n\t;;"
                :"=m" (tmp)
                :);
      }
      #define gasneti_local_wmb _gasneti_local_wmb

      GASNETI_INLINE(_gasneti_local_rmb)
      void _gasneti_local_rmb() {
        register int r;
        int tmp;
        __asm__ __volatile__(
                ";;\n\tld4.acq %0 = %1\n\t;;"
                : "=r" (r) : "m" (tmp) : "memory");
      }
      #define gasneti_local_rmb _gasneti_local_rmb

      #define gasneti_local_mb() GASNETI_ASM("mf")
    #endif
   #endif
#elif defined(_POWER) /* IBM SP POWER[234] */ || \
     (defined(__APPLE__) && defined(__MACH__) && (defined(__ppc__) || defined(__ppc64__))) /* PowerPC OSX */ || \
     (defined(__linux__) && defined(__PPC__)) /* PPC Linux */ || \
     (defined(__blrts__) && defined(__PPC__)) /* BlueGene/L */
 #ifdef __xlC__
   /* VisualAge C compiler (mpcc_r) has no support for inline symbolic assembly
    * you have to hard-code the opcodes in a pragma that defines an assembly 
    * function - see /usr/include/sys/atomic_op.h on AIX for examples
    * opcodes can be aquired by placing the mnemonics in inline.s and running:
    * as -sinline.lst inline.s
    */ 
   #pragma mc_func _gasneti_do_wmb { \
     "7c0004ac" /* sync (same opcode used for dcs) */ \
   }
   #pragma reg_killed_by _gasneti_do_wmb
   #define gasneti_local_wmb() _gasneti_do_wmb()

   #pragma mc_func _gasneti_do_rmb { \
     "4c00012c" /* isync (instruction sync to squash speculative loads) */ \
   }
   #pragma reg_killed_by _gasneti_do_rmb
   #define gasneti_local_rmb() _gasneti_do_rmb()

   #pragma mc_func _gasneti_do_compilerfence { "" }
   #pragma reg_killed_by _gasneti_do_compilerfence
   #define gasneti_compiler_fence() _gasneti_do_compilerfence()
 #else
   GASNETI_INLINE(gasneti_local_wmb)
   void gasneti_local_wmb(void) {
     GASNETI_ASM("sync");
   }
   GASNETI_INLINE(_gasneti_local_rmb)
   void _gasneti_local_rmb(void) {
     GASNETI_ASM("isync");
   }
   #define gasneti_local_rmb() _gasneti_local_rmb()
 #endif
#elif defined(__alpha)
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
 #elif defined(__osf__) && 0
   /* Use compaq C built-ins */
   /* Note this is heavier weight than required */
   #include <machine/builtins.h>
   #define gasneti_local_wmb() __MB()
   #define gasneti_local_rmb() __MB()
   #define gasneti_local_mb() __MB()
   #define GASNETI_RMB_IS_MB
   #define GASNETI_WMB_IS_MB
 #endif
#elif defined(_CRAYT3E) /* Takes care of e-regs also */
   #include <intrinsics.h>
   #define gasneti_local_wmb() _memory_barrier()
   #define gasneti_local_rmb() _memory_barrier()
   #define gasneti_local_mb() _memory_barrier()
   #define gasneti_compiler_fence() do { int volatile x = 0; } while (0)
   #define GASNETI_RMB_IS_MB
   #define GASNETI_WMB_IS_MB
#elif defined(__crayx1)
  GASNETI_INLINE(_gasneti_compiler_fence)
  void _gasneti_compiler_fence(void) {
    static int volatile x;
    x = 1;
  }
  #define gasneti_compiler_fence _gasneti_compiler_fence
  #pragma _CRI suppress _gasneti_compiler_fence
  #ifdef GASNET_X1_SHMEM_MB 
    /* using shmem for memory barriers seems effective, but the performance
       and usability impact is unclear */
    #include <mpp/shmem.h>
    GASNETI_INLINE(gasneti_local_wmb)
    void gasneti_local_wmb(void) {
      shmem_quiet(); /* bug 1195: this is the only option that appears to be effective */
    }
    #define gasneti_local_rmb gasneti_local_wmb
    #define gasneti_local_mb  gasneti_local_wmb
    #define GASNETI_RMB_IS_MB
    #define GASNETI_WMB_IS_MB
  #else
    /* Many memory barrier intrinsics on the X1, but none seem to actually
     * deliver what we need in a local (scalar-scalar) membar */
     #define gasneti_local_wmb gasneti_compiler_fence
     #define GASNETI_WMB_IS_EMPTY
  #endif
#elif defined(__MTA__)
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
   #define gasnet_local_wmb() gasneti_compiler_fence()
   #define gasnet_local_rmb() gasneti_compiler_fence()
   #define gasnet_local_mb()  gasneti_compiler_fence()
   #define GASNETI_RMB_IS_MB
   #define GASNETI_WMB_IS_MB
   #define GASNETI_RMB_IS_EMPTY
   #define GASNETI_WMB_IS_EMPTY
   #define GASNETI_MB_IS_EMPTY
#elif defined(_SX)
   GASNETI_INLINE(gasneti_local_wmb)
   void gasneti_local_wmb(void) {
     /* TODO: probably need more here */
     static int volatile x;
     x = 1;
     /* GASNETI_ASM("nop"); - leads to "FATAL COMPILER ERROR, Unknown statement. c++: Internal Error: Please report." */
   }
   #define GASNETI_WMB_IS_EMPTY
#else
 #error unknown CPU - dont know how to do a local memory barrier for your CPU/OS
#endif

#if GASNETI_USING_SLOW_ATOMICS
  #ifndef __cplusplus
    #error Slow atomics are only a hack-around for C++ compilers lacking inline assembly support
  #endif
  extern "C" void gasneti_slow_local_wmb();
  #define gasneti_local_wmb() gasneti_slow_local_wmb()
  extern "C" void gasneti_slow_local_rmb();
  #define gasneti_local_rmb() gasneti_slow_local_rmb()
  extern "C" void gasneti_slow_local_mb();
  #define gasneti_local_mb() gasneti_slow_local_mb()
  extern "C" void gasneti_slow_compiler_fence();
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
  #define GASNETI_RMB_IS_EMPTY
#endif

/* NO Default for gasneti_local_wmb() to avoid mistakes - it must be explicitly provided */

/* Default gasneti_local_mb() */
#ifndef gasneti_local_mb
  #define gasneti_local_mb() do { gasneti_local_wmb(); gasneti_local_rmb(); } while (0)
  #define GASNETI_MB_IS_SUM
#endif


/* ------------------------------------------------------------------------------------ */
/* Properties of the memory barriers (as boolean preprocessor tokens)
	GASNETI_RMB_IS_MB	rmb() is sufficient for mb()
	GASNETI_WMB_IS_MB	wmb() is sufficient for mb()
	GASNETI_RMB_IS_EMPTY	rmb() is nothing but a compiler fence
	GASNETI_WMB_IS_EMPTY	wmb() is nothing but a compiler fence
	GASNETI_MB_IS_EMPTY	mb() is nothing but a compiler fence
	GASNETI_MB_IS_SUM	wmb()+rmb() is mb(), as opposed to a double mb()
   These tokens are used by the fenced atomics to produce minimal code.
   What follows "normalizes" these tokens to 0 or 1 and applies defaults.
   The defaults are always safe, but perhaps sub-optimal.
   The defns of the membars should define non-defaults appropriately.

   THESE ARE *NOT* INTENDED FOR GENERAL USE IN CONDUIT CODE.
 */
#ifndef GASNETI_RMB_IS_EMPTY
  /* Default: assume rmb() is non-empty. */
  #define GASNETI_RMB_IS_EMPTY	0
#else
  #undef GASNETI_RMB_IS_EMPTY
  #define GASNETI_RMB_IS_EMPTY	1
#endif
#ifndef GASNETI_WMB_IS_EMPTY
  /* Default: assume wmb() is non-empty. */
  #define GASNETI_WMB_IS_EMPTY	0
#else
  #undef GASNETI_WMB_IS_EMPTY
  #define GASNETI_WMB_IS_EMPTY	1
#endif
#ifndef GASNETI_MB_IS_EMPTY
  /* Default: assume mb() is empty IFF rmb() and wmb() are both empty */
  #if (GASNETI_RMB_IS_EMPTY && GASNETI_WMB_IS_EMPTY)
    #define GASNETI_MB_IS_EMPTY	1
  #else
    #define GASNETI_MB_IS_EMPTY	0
  #endif
#else
  #undef GASNETI_MB_IS_EMPTY
  #define GASNETI_MB_IS_EMPTY	1
#endif
#ifndef GASNETI_RMB_IS_MB
  /* Default: assume rmb() is a full mb() if:
   *  Either mb() is empty (sequential consistency)
   *  Or mb() = rmb() + wmb(), while wmb() is known empty */
  #if GASNETI_MB_IS_EMPTY || (GASNETI_WMB_IS_EMPTY && defined(GASNETI_MB_IS_SUM))
    #define GASNETI_RMB_IS_MB	1
  #else
    #define GASNETI_RMB_IS_MB	0
  #endif
#else
  #undef GASNETI_RMB_IS_MB
  #define GASNETI_RMB_IS_MB	1
#endif
#ifndef GASNETI_WMB_IS_MB
  /* Default: assume wmb() is a full mb() if:
   *  Either mb() is empty (sequential consistency)
   *  Or mb() = rmb() + wmb(), while rmb() is known empty */
  #if GASNETI_MB_IS_EMPTY || (GASNETI_RMB_IS_EMPTY && defined(GASNETI_MB_IS_SUM))
    #define GASNETI_WMB_IS_MB	1
  #else
    #define GASNETI_WMB_IS_MB	0
  #endif
#else
  #undef GASNETI_WMB_IS_MB
  #define GASNETI_WMB_IS_MB	1
#endif
#ifndef GASNETI_MB_IS_SUM
  /* Default: assume mb() = rmb() + wmb() if:
   *  Either mb() = rmb(), while wmb() is known empty
   *  Or mb() = wmb(), while rmb() is known empty */
  #if ((GASNETI_RMB_IS_MB && GASNETI_WMB_IS_EMPTY) || \
       (GASNETI_WMB_IS_MB && GASNETI_RMB_IS_EMPTY))
    #define GASNETI_MB_IS_SUM	1
  #else
    #define GASNETI_MB_IS_SUM	0
  #endif
#else
  #undef GASNETI_MB_IS_SUM
  #define GASNETI_MB_IS_SUM	1
#endif
 
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
 #elif (defined(__ia64__) || defined(__ia64)) && defined(__INTEL_COMPILER) && 0 /* DISABLED */
   /* Intel compiler's inline assembly broken on Itanium (bug 384) - use intrinsics instead */
   #include <ia64intrin.h>
   #define gasneti_spinloop_hint() __hint(__hint_pause)
 #else
   #define gasneti_spinloop_hint() ((void)0)
 #endif
#endif

/* ------------------------------------------------------------------------------------ */

#endif

#endif

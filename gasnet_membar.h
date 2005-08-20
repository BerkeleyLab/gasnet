/*   $Source: /Users/kamil/work/gasnet-cvs2/gasnet/gasnet_membar.h,v $
 *     $Date: 2005/08/20 06:51:50 $
 * $Revision: 1.72.2.2 $
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

  Note that for all five memory barriers, we require only that a given architecture's
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
#elif defined(__xlC__)  
  #define GASNETI_ASM(mnemonic)  ERROR_NO_INLINE_ASSEMBLY_AVAIL /* not supported or used */
#elif defined(_CRAY)  
  #define GASNETI_ASM(mnemonic)  ERROR_NO_INLINE_ASSEMBLY_AVAIL /* not supported or used */
#elif defined(__MTA__)  
  #define GASNETI_ASM(mnemonic)  ERROR_NO_INLINE_ASSEMBLY_AVAIL /* not supported or used */
#else
  #error "Don't know how to use inline assembly for your compiler"
#endif


#if defined(__sparc__) || defined(__sparc) || defined(sparc)
  #if defined(__sparcv9) || defined(__sparcv9cpu) || defined(GASNETI_SPARCV9) /* SPARC v9 */
    GASNET_INLINE_MODIFIER(gasneti_local_wmb)
    void gasneti_local_wmb(void) {
      GASNETI_ASM("membar #StoreLoad | #StoreStore"); 
    }
    GASNET_INLINE_MODIFIER(_gasneti_local_rmb)
    void _gasneti_local_rmb(void) {
      GASNETI_ASM("membar #LoadStore | #LoadLoad"); 
    }
    #define gasneti_local_rmb() _gasneti_local_rmb()
    GASNET_INLINE_MODIFIER(_gasneti_local_mb)
    void _gasneti_local_mb(void) {
      GASNETI_ASM("membar #LoadStore | #LoadLoad | #StoreLoad | #StoreStore");
    }
    #define gasneti_local_mb() _gasneti_local_mb()
  #else /* SPARC v7/8 */
    GASNET_INLINE_MODIFIER(gasneti_local_wmb)
    void gasneti_local_wmb(void) {
      GASNETI_ASM("stbar"); /* SPARC store barrier */
    }
  #endif
#elif defined(__mips__) || defined(__mips) || defined(mips) || defined(_MIPS_ISA)
  #if defined(MIPSPRO_COMPILER)
    GASNET_INLINE_MODIFIER(_gasneti_compiler_fence)
    void _gasneti_compiler_fence(void) {
      volatile int x; x = 1;
    }
    #define gasneti_compiler_fence() _gasneti_compiler_fence()
    #define gasneti_local_wmb() __synchronize()
    #define gasneti_local_mb()  __synchronize()
  #else
    GASNET_INLINE_MODIFIER(gasneti_local_wmb)
    void gasneti_local_wmb(void) {
      GASNETI_ASM("sync");  /* MIPS II+ memory barrier */ 
    }
  #endif
#elif defined(_PA_RISC1_1) || defined(__hppa) /* HP PA-RISC */
 #if defined(__HP_aCC)
   extern "C" void gasneti_slow_local_wmb();
   #define gasneti_local_wmb() gasneti_slow_local_wmb()
   #define GASNETI_USING_SLOW_ATOMICS 1
 #else
   GASNET_INLINE_MODIFIER(gasneti_local_wmb)
   void gasneti_local_wmb(void) {
     #if defined(__HP_cc) 
       _flush_globals();
     #endif
     GASNETI_ASM("SYNC");  /* PA RISC load/store ordering */ 
   }
 #endif
 #if defined(__HP_aCC)
   extern "C" void gasneti_slow_compiler_fence();
   #define gasneti_compiler_fence() gasneti_slow_compiler_fence()
   #define GASNETI_USING_SLOW_ATOMICS 1
 #elif defined(__HP_cc) 
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
   GASNET_INLINE_MODIFIER(gasneti_local_wmb)
   void gasneti_local_wmb(void) {
     /* The instruction here can be any locked read-modify-write operation.
      * This one is chosen because it does not change any registers and is
      * available on all the Intel and clone CPUs.  Also, since it touches
      * only the stack, it is highly unlikely to result in extra coherence
      * traffic.
      */
     #if defined(__PGI)
       GASNETI_ASM("lock; addl $0,0(%esp)");
     #else
       GASNETI_ASM("lock; addl $0,0(%%esp)");
     #endif
   }
#elif defined(__x86_64__) /* Athlon/Opteron */
   GASNET_INLINE_MODIFIER(gasneti_local_wmb)
   void gasneti_local_wmb(void) {
     /* XXX: sfence probably sufficient - test after release */
     GASNETI_ASM("mfence");
   }
   GASNET_INLINE_MODIFIER(_gasneti_local_rmb)
   void _gasneti_local_rmb(void) {
     GASNETI_ASM("lfence");
   }
   #define gasneti_local_rmb() _gasneti_local_rmb()
   GASNET_INLINE_MODIFIER(_gasneti_local_mb)
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
   #elif defined(__HP_cc) || defined(__HP_aCC)
      #include <machine/sys/inline.h>
      /* HP compilers have no inline assembly on Itanium - use intrinsics */
      #define gasneti_compiler_fence() \
         _Asm_sched_fence((_Asm_fence)(_UP_MEM_FENCE | _DOWN_MEM_FENCE)) 
      /* bug 1000: empirically observed that IA64 requires a full memory fence for both wmb and rmb */
      #define gasneti_local_wmb() _Asm_mf((_Asm_fence)(_UP_MEM_FENCE))
      #define gasneti_local_rmb() _Asm_mf((_Asm_fence)(_DOWN_MEM_FENCE))
      #define gasneti_local_mb() _Asm_mf((_Asm_fence)(_UP_MEM_FENCE | _DOWN_MEM_FENCE))
   #else
    #if 1
      #define gasneti_local_wmb() GASNETI_ASM("mf")
      /* bug 1000: empirically observed that IA64 requires a full memory fence for both wmb and rmb */
      #define gasneti_local_rmb() gasneti_local_wmb()
      #define gasneti_local_mb()  gasneti_local_wmb()
    #else
      /* according to section 4.4.7 in:
         "Intel Itanium Architecture Software Developer's Manual, Vol 2 System Architecture"
         the following should work, but for some reason it does not
       */
      GASNET_INLINE_MODIFIER(_gasneti_local_wmb)
      void _gasneti_local_wmb() {
        int tmp;
        __asm__ __volatile__(
                ";;\n\tst4.rel %0 = r0\n\t;;"
                :"=m" (tmp)
                :);
      }
      #define gasneti_local_wmb _gasneti_local_wmb

      GASNET_INLINE_MODIFIER(_gasneti_local_rmb)
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
#elif defined(_POWER) || (defined(__APPLE__) && defined(__MACH__) && defined(__ppc__)) || (defined(__linux__) && defined(__PPC__)) || (defined(__blrts__) && defined(__PPC__))
 /* (_POWER) == IBM SP POWER[234]
  * (__APPLE__ && __MACH__ && __ppc__) == Darwin, OS/X
  * (__linux__ && __PPC__) == Linux
  * (__blrts__ && __PPC__) == BlueGene/L
  */
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
   GASNET_INLINE_MODIFIER(gasneti_local_wmb)
   void gasneti_local_wmb(void) {
     GASNETI_ASM("sync");
   }
   GASNET_INLINE_MODIFIER(_gasneti_local_rmb)
   void _gasneti_local_rmb(void) {
     GASNETI_ASM("isync");
   }
   #define gasneti_local_rmb() _gasneti_local_rmb()
 #endif
#elif defined(__alpha)
 #if 1 /* tested on OSF1, LINUX, FreeBSD */
   GASNET_INLINE_MODIFIER(gasneti_local_wmb)
   void gasneti_local_wmb(void) {
     GASNETI_ASM("wmb");
   }
   GASNET_INLINE_MODIFIER(_gasneti_local_rmb)
   void _gasneti_local_rmb(void) {
     GASNETI_ASM("mb");
   }
   #define gasneti_local_rmb() _gasneti_local_rmb()
   GASNET_INLINE_MODIFIER(_gasneti_local_mb)
   void _gasneti_local_mb(void) {
     GASNETI_ASM("mb");
   }
   #define gasneti_local_mb() _gasneti_local_mb()
 #elif defined(__osf__) && 0
   /* Use compaq C built-ins */
   /* Note this is heavier weight than required */
   #include <machine/builtins.h>
   #define gasneti_local_wmb() __MB()
   #define gasneti_local_rmb() __MB()
   #define gasneti_local_mb() __MB()
 #endif
#elif defined(_CRAYT3E) /* Takes care of e-regs also */
   #include <intrinsics.h>
   #define gasneti_local_wmb() _memory_barrier()
   #define gasneti_local_rmb() _memory_barrier()
   #define gasneti_local_mb() _memory_barrier()
   #define gasneti_compiler_fence() do { int volatile x = 0; } while (0)
#elif defined(__crayx1)
  GASNET_INLINE_MODIFIER(_gasneti_compiler_fence)
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
    GASNET_INLINE_MODIFIER(gasneti_local_wmb)
    void gasneti_local_wmb(void) {
      shmem_quiet(); /* bug 1195: this is the only option that appears to be effective */
    }
    #define gasneti_local_rmb gasneti_local_wmb
    #define gasneti_local_mb  gasneti_local_wmb
  #else
    /* Many memory barrier intrinsics on the X1, but none seem to actually
     * deliver what we need in a local (scalar-scalar) membar */
     #define gasneti_local_wmb gasneti_compiler_fence
  #endif
#elif defined(__MTA__)
   #if 0 /* causes warnings */
     #define gasneti_compiler_fence() (_Pragma("mta fence"))
   #else
     GASNET_INLINE_MODIFIER(_gasneti_compiler_fence)
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
#elif defined(_SX)
   GASNET_INLINE_MODIFIER(gasneti_local_wmb)
   void gasneti_local_wmb(void) {
     /* TODO: probably need more here */
     static int volatile x;
     x = 1;
     /* GASNETI_ASM("nop"); - leads to "FATAL COMPILER ERROR, Unknown statement. c++: Internal Error: Please report." */
   }
#else
 #error unknown CPU - dont know how to do a local memory barrier for your CPU/OS
#endif

/* Default gasneti_compiler_fence() */
#ifndef gasneti_compiler_fence
  #define gasneti_compiler_fence() GASNETI_ASM("")
#endif

/* Default gasneti_local_rmb() */
#ifndef gasneti_local_rmb
  #define gasneti_local_rmb() gasneti_compiler_fence()
#endif

/* NO Default for gasneti_local_wmb() to avoid mistakes - it must be explicitly provided */

/* Default gasneti_local_mb() */
#ifndef gasneti_local_mb
  #define gasneti_local_mb() do { gasneti_local_wmb(); gasneti_local_rmb(); } while (0)
#endif

#ifndef gasneti_spinloop_hint
 #ifdef HAVE_X86_PAUSE_INSTRUCTION
   /* Pentium 4 processors get measurably better performance when a "pause" instruction
      is inserted in spin-loops - this instruction is documented as a "spin-loop hint"
      which avoids a memory hazard stall on spin loop exit and reduces power consumption
      Other Intel CPU's treat this instruction as a no-op
   */
   #define gasneti_spinloop_hint() GASNETI_ASM("pause")
 #else
   #define gasneti_spinloop_hint() ((void)0)
 #endif
#endif

/* ------------------------------------------------------------------------------------ */

#endif

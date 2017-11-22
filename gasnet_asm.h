/*   $Source: bitbucket.org:berkeleylab/gasnet.git/gasnet_asm.h $
 * Description: GASNet header for semi-portable inline asm support
 * Copyright 2002, Dan Bonachea <bonachea@cs.berkeley.edu>
 * Terms of use are as specified in license.txt
 */

#if !defined(_IN_GASNET_TOOLS_H) && !defined(_IN_GASNETEX_H) && !defined(_IN_CONFIGURE)
  #error This file is not meant to be included directly- clients should include gasnetex.h or gasnet_tools.h
#endif

#ifndef _GASNET_ASM_H
#define _GASNET_ASM_H

#undef _PORTABLE_PLATFORM_H
#include "gasnet_portable_platform.h"

/* Sort out the per-compiler support for asm and atomics */
#ifdef GASNETI_COMPILER_HAS
  #if GASNETI_COMPILER_HAS(GCC_ASM)
      #define GASNETI_HAVE_GCC_ASM 1
  #endif
  #if GASNETI_COMPILER_HAS(XLC_ASM)
      #define GASNETI_HAVE_XLC_ASM 1
  #endif
  #if GASNETI_COMPILER_HAS(SYNC_ATOMICS_32)
      #define GASNETI_HAVE_SYNC_ATOMICS_32 1
  #endif
  #if GASNETI_COMPILER_HAS(SYNC_ATOMICS_64)
      #define GASNETI_HAVE_SYNC_ATOMICS_64 1
  #endif
#elif !defined(_IN_CONFIGURE)
  #error header inclusion error: missing GASNETI_COMPILER_HAS
#endif

#define GASNETI_ASM_AVAILABLE 1
#if GASNETI_HAVE_GCC_ASM
  /* Configure detected support for GCC-style inline asm */
#elif PLATFORM_COMPILER_GNU || PLATFORM_COMPILER_INTEL || PLATFORM_COMPILER_PATHSCALE || \
      PLATFORM_COMPILER_TINY || PLATFORM_COMPILER_OPEN64 || PLATFORM_COMPILER_CLANG
  #define GASNETI_HAVE_GCC_ASM 1
#elif PLATFORM_COMPILER_PGI && PLATFORM_ARCH_POWERPC
  #define GASNETI_HAVE_GCC_ASM 1
  #if PLATFORM_COMPILER_VERSION_LT(17,3,0) // All known versions prior to 17.3
    // PGI "tpr 23290"
    // Does not grok the immediate modifier "%I" in an asm template
    #define GASNETI_PGI_ASM_TPR23290 1
  #endif
  #if PLATFORM_COMPILER_PGI_CXX && \
      PLATFORM_COMPILER_VERSION_LT(17,1,0) // All known versions prior to 17.1
    // PGI "tpr 23291"
    // C++ compiler does not grok "cr0", though C compiler does
    #define GASNETI_PGI_ASM_TPR23291 1
  #endif
  #if PLATFORM_COMPILER_PGI_CXX && GASNET_NDEBUG && \
      PLATFORM_COMPILER_VERSION_GE(16,0,0) && PLATFORM_COMPILER_VERSION_LT(17,7,0)
    // PGI "tpr 24514"
    // C++ compiler fails with certain asm constructs at -O2
    #define GASNETI_PGI_ASM_TPR24514 1
  #endif
#elif PLATFORM_COMPILER_PGI /* x86 and x86-64 */
  #define GASNETI_HAVE_GCC_ASM 1
  #if PLATFORM_COMPILER_VERSION_LT(7,2,5)
    #error "GASNet does not support PGI compilers prior to 7.2-5"
  #endif
  #if PLATFORM_ARCH_32 && \
      PLATFORM_COMPILER_VERSION_GE(7,1,5) && PLATFORM_COMPILER_VERSION_LT(8,0,6)
    /* Compiler suffers from "tpr 14969" in which extended asm() output constraints can't
     * be met unless they appear in a specific order.  This is on 32-bit targets only.
     *
     * NOTE: PGI reports TPR 14969 was fixed in 8.0-1.
     * However, we have only been able to test 8.0-6 and later.
     */
    #define GASNETI_PGI_ASM_BUG2294 1
  #endif
  #if PLATFORM_COMPILER_VERSION_GE(7,0,0) && PLATFORM_COMPILER_VERSION_LT(10,8,0)
    /* Compiler suffers from "tpr 17075" in which extended asm() may load only 32 bits of
     * a 64-bit operand at -O1 (but is OK at -O0 and -O2).
     */
    #define GASNETI_PGI_ASM_BUG2843 1
  #endif
  #if PLATFORM_COMPILER_PGI_CXX && PLATFORM_COMPILER_VERSION_GE(17,0,0)
    // C++ compiler generates code that is consistent with having lost the volatile
    // qualifier from the integer member of the atomic type struct.
    #define GASNETI_PGI_ASM_BUG3674 1
  #endif
  #if PLATFORM_COMPILER_PGI_CXX && PLATFORM_COMPILER_VERSION_GT(17,4,0)
    // C++ compiler generates code that promotes 8-bit asm output to
    // 32-bits without clearing the other 24 bits.
    // The work-around is the same as for an older (unrelated) bug 1754.
    // Present in 17.10 and not in 17.4, but uncertain about in between.
    #define GASNETI_PGI_ASM_BUG1754 1
  #endif
#elif PLATFORM_COMPILER_SUN 
  #ifdef __cplusplus 
    #if PLATFORM_OS_LINUX
      #define GASNETI_ASM(mnemonic)  asm(mnemonic)
    #else /* Sun C++ on Solaris lacks inline assembly support (man inline) */
      #define GASNETI_ASM(mnemonic)  ERROR_NO_INLINE_ASSEMBLY_AVAIL /* not supported or used */
      #undef GASNETI_ASM_AVAILABLE
    #endif
  #else /* Sun C */
    #define GASNETI_ASM(mnemonic)  __asm(mnemonic)
  #endif
#elif PLATFORM_COMPILER_XLC || PLATFORM_COMPILER_CRAY
  /* platforms where inline assembly not supported or used */
  #define GASNETI_ASM(mnemonic)  ERROR_NO_INLINE_ASSEMBLY_AVAIL 
  #undef GASNETI_ASM_AVAILABLE
  #if PLATFORM_COMPILER_CRAY && PLATFORM_ARCH_X86_64
    #include "intrinsics.h"
  #endif
#else
  #error "Don't know how to use inline assembly for your compiler"
#endif

#if GASNETI_HAVE_GCC_ASM
  #define GASNETI_ASM(mnemonic) __asm__ __volatile__ (mnemonic : : : "memory")
#endif

#ifndef GASNETI_ASM_SPECIAL
  #define GASNETI_ASM_SPECIAL GASNETI_ASM
#endif

#if PLATFORM_OS_BGQ && (PLATFORM_COMPILER_GNU || PLATFORM_COMPILER_XLC)
  /* The situation on BG/Q is either as bad as on BG/P, or perhaps worse.
   * The use of 'extern inline' means we can't get what we need at all in
   * a debug build.  At least on BG/P there was a lib we could have linked.
   */
  #ifndef __INLINE__
    #if GASNET_DEBUG
      #define GASNETI_DEFINE__INLINE__ static
    #elif defined(__cplusplus)
      #define GASNETI_DEFINE__INLINE__ inline
    #else
      #define GASNETI_DEFINE__INLINE__ GASNETI_COMPILER_FEATURE(INLINE_MODIFIER,static)
    #endif
    #define __INLINE__ GASNETI_DEFINE__INLINE__
  #endif
  #include "cnk/include/SPI_syscalls.h"
  #include "hwi/include/bqc/A2_inlines.h"
  #ifdef GASNETI_DEFINE__INLINE__
    #undef __INLINE__
  #endif
  #define GASNETI_HAVE_BGQ_INLINES 1
#endif

#if PLATFORM_ARCH_ARM && PLATFORM_OS_LINUX
  /* This helper macro hides ISA differences going from ARMv4 to ARMv5 */
  #if defined(__thumb__) && !defined(__thumb2__)
    #error "GASNet does not support ARM Thumb1 mode"
    #define GASNETI_ARM_ASMCALL(_tmp, _offset) "choke me"
  #elif defined(__ARM_ARCH_2__)
    #error "GASNet does not support ARM versions earlier than ARMv3"
    #define GASNETI_ARM_ASMCALL(_tmp, _offset) "choke me"
  #elif defined(__ARM_ARCH_3__) || defined(__ARM_ARCH_4__) || defined(__ARM_ARCH_4T__)
    #define GASNETI_ARM_ASMCALL(_tmp, _offset) \
	"	mov	" #_tmp ", #0xffff0fff              @ _tmp = base addr    \n" \
	"	mov	lr, pc                              @ lr = return addr    \n" \
	"	sub	pc, " #_tmp ", #" #_offset "        @ call _tmp - _offset \n"
  #else
    #define GASNETI_ARM_ASMCALL(_tmp, _offset) \
	"	mov	" #_tmp ", #0xffff0fff              @ _tmp = base addr    \n" \
	"	sub	" #_tmp ", " #_tmp ", #" #_offset " @ _tmp -= _offset     \n" \
	"	blx	" #_tmp "                           @ call _tmp           \n"
  #endif
#endif

#if PLATFORM_ARCH_MIPS && defined(HAVE_SGIDEFS_H)
  /* For _MIPS_ISA and _MIPS_SIM values on some MIPS platforms */
  #include <sgidefs.h>
#endif

#endif /* _GASNET_ASM_H */

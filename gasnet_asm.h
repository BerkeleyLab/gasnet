/*   $Source: /Users/kamil/work/gasnet-cvs2/gasnet/gasnet_asm.h,v $
 *     $Date: 2006/05/28 02:27:54 $
 * $Revision: 1.109 $
 * Description: GASNet header for semi-portable inline asm support
 * Copyright 2002, Dan Bonachea <bonachea@cs.berkeley.edu>
 * Terms of use are as specified in license.txt
 */

#if !defined(_IN_GASNET_TOOLS_H) && !defined(_IN_GASNET_H) && !defined(_IN_CONFIGURE)
  #error This file is not meant to be included directly- clients should include gasnet.h or gasnet_tools.h
#endif

#ifndef _GASNET_ASM_H
#define _GASNET_ASM_H

#include "portable_platform.h"

#if PLATFORM_COMPILER_GNU || PLATFORM_COMPILER_INTEL || PLATFORM_COMPILER_PATHSCALE || \
    PLATFORM_COMPILER_TINY || PLATFORM_COMPILER_LCC
  #define GASNETI_ASM(mnemonic) __asm__ __volatile__ (mnemonic : : : "memory")
#elif PLATFORM_COMPILER_PGI 
  #if PLATFORM_COMPILER_VERSION_GE(6,1,1) && 0 /* DISABLED (see bug 1621) */
    #define PGI_WITH_REAL_ASM 1
    #define GASNETI_ASM(mnemonic) __asm__ __volatile__ (mnemonic : : : "memory")
  #else /* note this requires compiler flag -Masmkeyword */
    #define GASNETI_ASM(mnemonic) asm(mnemonic)
  #endif
#elif PLATFORM_COMPILER_COMPAQ
  #include <c_asm.h>
  #define GASNETI_ASM(mnemonic) asm(mnemonic)
#elif PLATFORM_COMPILER_SUN 
  #ifdef __cplusplus /* Sun C works, Sun C++ lacks inline assembly support (man inline) */
    #define GASNETI_ASM(mnemonic)  ERROR_NO_INLINE_ASSEMBLY_AVAIL /* not supported or used */
  #else
    #define GASNETI_ASM(mnemonic)  __asm(mnemonic)
  #endif
#elif PLATFORM_COMPILER_NEC 
  #define GASNETI_ASM(mnemonic)  asm(mnemonic)
#elif dPLATFORM_COMPILER_HP
  #define GASNETI_ASM(mnemonic)  _asm(mnemonic)
#elif PLATFORM_COMPILER_SGI || PLATFORM_COMPILER_HP || PLATFORM_COMPILER_XLC || \
      PLATFORM_COMPILER_CRAY || PLATFORM_COMPILER_MTA
  /* platforms where inline assembly not supported or used */
  #define GASNETI_ASM(mnemonic)  ERROR_NO_INLINE_ASSEMBLY_AVAIL 
#else
  #error "Don't know how to use inline assembly for your compiler"
#endif

#endif /* _GASNET_ASM_H */

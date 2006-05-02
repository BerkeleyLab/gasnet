/*   $Source: /Users/kamil/work/gasnet-cvs2/gasnet/gasnet_asm.h,v $
 *     $Date: 2006/04/28 00:19:29 $
 * $Revision: 1.105 $
 * Description: GASNet header for semi-portable inline asm support
 * Copyright 2002, Dan Bonachea <bonachea@cs.berkeley.edu>
 * Terms of use are as specified in license.txt
 */

#if !defined(_IN_GASNET_TOOLS_H) && !defined(_IN_GASNET_H) && !defined(_IN_CONFIGURE)
  #error This file is not meant to be included directly- clients should include gasnet.h or gasnet_tools.h
#endif

#ifndef _GASNET_ASM_H
#define _GASNET_ASM_H

#if defined(__GNUC__)
  #define GASNETI_ASM(mnemonic) __asm__ __volatile__ (mnemonic : : : "memory")
#elif defined(__INTEL_COMPILER)
  #define GASNETI_ASM(mnemonic) __asm__ __volatile__ (mnemonic : : : "memory")
#elif defined(PGI_WITH_REAL_ASM)
  #define GASNETI_ASM(mnemonic) __asm__ __volatile__ (mnemonic : : : "memory")
#elif defined(__PGI) /* note this requires compiler flag -Masmkeyword */
  #define GASNETI_ASM(mnemonic) asm(mnemonic)
#elif defined(__DECC) || defined(__DECCXX)
  #include <c_asm.h>
  #define GASNETI_ASM(mnemonic) asm(mnemonic)
#elif defined(_SGI_COMPILER_VERSION) /* MIPSPro C */
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

#endif /* _GASNET_ASM_H */

/*   $Source: /Users/kamil/work/gasnet-cvs2/gasnet/gasnet_basic.h,v $
 *     $Date: 2005/04/11 04:22:37 $
 * $Revision: 1.32.2.2 $
 * Description: GASNet basic header utils
 * Copyright 2002, Dan Bonachea <bonachea@cs.berkeley.edu>
 * Terms of use are as specified in license.txt
 */

#if !defined(_IN_GASNET_H) && !defined(_IN_GASNET_TOOLS_H)
  #error This file is not meant to be included directly- clients should include gasnet.h or gasnet_tools.h
#endif

#ifndef _GASNET_BASIC_H
#define _GASNET_BASIC_H

/* ------------------------------------------------------------------------------------ */
/* include files that may conflict with macros defined later */
#if defined(IRIX) || defined(HPUX) || defined(UNICOS) || defined(NETBSD)
  #include <sys/param.h>
#endif

#include "portable_inttypes.h"

#if SIZEOF_VOID_P == 4
  #define GASNETI_PTR32
  #define GASNETI_PTR_CONFIG 32bit
#elif SIZEOF_VOID_P == 8
  #define GASNETI_PTR64
  #define GASNETI_PTR_CONFIG 64bit
#else
  #error GASNet currently only supports 32-bit and 64-bit platforms
#endif

  /* miscellaneous macro helpers */
#ifndef BEGIN_EXTERNC
  #ifdef __cplusplus
    #define BEGIN_EXTERNC extern "C" {
    #define END_EXTERNC }
  #else
    #define BEGIN_EXTERNC 
    #define END_EXTERNC 
  #endif
#endif

#ifndef _STRINGIFY
#define _STRINGIFY_HELPER(x) #x
#define _STRINGIFY(x) _STRINGIFY_HELPER(x)
#endif

#ifndef _CONCAT
#define _CONCAT_HELPER(a,b) a ## b
#define _CONCAT(a,b) _CONCAT_HELPER(a,b)
#endif

#ifndef TRUE
#define TRUE 1
#endif
#ifndef FALSE
#define FALSE 0
#endif
#ifndef MIN
#define MIN(x,y)  ((x)<(y)?(x):(y))
#endif
#ifndef MAX
#define MAX(x,y)  ((x)>(y)?(x):(y))
#endif

#ifdef __MTA__
   #include <machine/runtime.h>
   #define _gasneti_sched_yield() mta_yield()
#elif defined(HAVE_SCHED_YIELD)
   #include <sched.h>
   #define _gasneti_sched_yield() sched_yield()
#else
   #include <unistd.h>
   #define _gasneti_sched_yield() (sleep(0),0)
#endif
#define gasneti_sched_yield() _gasneti_sched_yield()

#include <stddef.h> /* get standard types, esp size_t */

/* splitting and reassembling 64-bit quantities */
#define GASNETI_MAKEWORD(hi,lo) ((((uint64_t)(hi)) << 32) | (((uint64_t)(lo)) & 0xFFFFFFFF))
#define GASNETI_HIWORD(arg)     ((uint32_t)(((uint64_t)(arg)) >> 32))
#define GASNETI_LOWORD(arg)     ((uint32_t)((uint64_t)(arg)))

#define GASNETI_PRAGMA(x) _Pragma ( #x )

#if defined(__cplusplus)
  #define GASNET_INLINE_MODIFIER(fnname) inline
#elif defined(STATIC_INLINE_WORKS)
  #define GASNET_INLINE_MODIFIER(fnname) static CC_INLINE_MODIFIER
#elif defined(CC_INLINE_MODIFIER)
  #define GASNET_INLINE_MODIFIER(fnname) CC_INLINE_MODIFIER
#elif defined(_CRAYC)
  /* CrayC has a really #&#$&! stupidly designed #pragma for inlining functions 
     that requires providing the function name 
     (the only way to request inlining a particular fn from C) */
  #define GASNET_INLINE_MODIFIER(fnname) GASNETI_PRAGMA(_CRI inline fnname) static
#elif defined(__MTA__)
  #define GASNET_INLINE_MODIFIER(fnname) GASNETI_PRAGMA(mta inline) static
#else
  #define GASNET_INLINE_MODIFIER(fnname) static
#endif

/* ------------------------------------------------------------------------------------ */
/* GASNETI_IDENT() takes a unique identifier and a textual string and embeds the textual
   string in the executable file
 */
#define _GASNETI_IDENT(identName, identText)                         \
  extern char volatile identName[];                                  \
  char volatile identName[] = identText;                             \
  extern char *_##identName##_identfn() { return (char*)identName; } \
  static int _dummy_##identName = sizeof(_dummy_##identName)
#if defined(_CRAYC)
  #define GASNETI_IDENT(identName, identText) \
    GASNETI_PRAGMA(_CRI ident identText);     \
    _GASNETI_IDENT(identName, identText)
#elif defined(__xlC__)
    /* #pragma comment(user,"text...") 
         or
       _Pragma ( "comment (user,\"text...\")" );
       are both supposed to work according to compiler docs, but both appear to be broken
     */
  #define GASNETI_IDENT(identName, identText)   \
    _GASNETI_IDENT(identName, identText)
#else
  #define GASNETI_IDENT _GASNETI_IDENT
#endif
/* ------------------------------------------------------------------------------------ */
/* Branch prediction:
   these macros return the value of the expression given, but pass on
   a hint that you expect the value to be true or false.
   Use them to wrap the conditional expression in an if stmt when
   you have strong reason to believe the branch will frequently go
   in one direction and the branch is a bottleneck
 */
#ifndef PREDICT_TRUE
  #if defined(__GNUC__) && defined(HAVE_BUILTIN_EXPECT)
    /* cast to uintptr_t avoids warnings on some compilers about passing 
       non-integer arguments to __builtin_expect(), and we don't use (int)
       because on some systems this is smaller than (void*) and causes 
       other warnings
     */
   #define PREDICT_TRUE(exp)  __builtin_expect( ((uintptr_t)(exp)), 1 )
   #define PREDICT_FALSE(exp) __builtin_expect( ((uintptr_t)(exp)), 0 )
  #else
   #define PREDICT_TRUE(exp)  (exp)
   #define PREDICT_FALSE(exp) (exp)
  #endif
#endif

/* if with branch prediction */
#ifndef if_pf
#ifdef __MTA__
  #define if_pf(cond) _Pragma("mta expect false") if (cond)
  #define if_pt(cond) _Pragma("mta expect true")  if (cond)
#else
  #define if_pf(cond) if (PREDICT_FALSE(cond))
  #define if_pt(cond) if (PREDICT_TRUE(cond))
#endif
#endif

/* ------------------------------------------------------------------------------------ */
/* Non-binding prefetch hints:
   These macros take a single address expression and provide a hint to prefetch the
   corresponding memory to L1 cache for either reading or for writing.
   These are non-binding hints and so the argument need not always be a valid pointer.
   For instance, GASNETI_PREFETCH_{READ,WRITE}_HINT(NULL) is explicitly permitted.
   The macros may expand to nothing, so the argument must not have side effects.
 */
#if HAVE_BUILTIN_PREFETCH
  #define GASNETI_PREFETCH_READ_HINT(P) __builtin_prefetch((P),0)
  #define GASNETI_PREFETCH_WRITE_HINT(P) __builtin_prefetch((P),1)
#else
  #define GASNETI_PREFETCH_READ_HINT(P)
  #define GASNETI_PREFETCH_WRITE_HINT(P)
#endif


#endif

/*   $Archive::                                                            $ */
/*      $Date: 2004/04/19 23:22:52 $ */
/*  $Revision: 1.1 $ */
/*  Description: portable_inttypes.h  */
/*  Copyright 2004, Dan Bonachea <bonachea@cs.berkeley.edu> */

/* inttypes.h is part of the POSIX and C99 specs, but in practice support for it 
   varies wildly across systems. We need a totally portable way to unambiguously
   get the fixed-bit-width integral types, and this file provides that via the 
   following typedefs:
      int8_t, uint8_t     signed/unsigned 8-bit integral types
     int16_t, uint16_t    signed/unsigned 16-bit integral types
     int32_t, uint32_t    signed/unsigned 32-bit integral types
     int64_t, uint64_t    signed/unsigned 64-bit integral types
     intptr_t, uintptr_t  signed/unsigned types big enough to hold any pointer offset
   In general, it uses the system inttypes.h when it's known to be available,
   (as reported by configure via a previously-included config.h file), otherwise
   it uses configure-detected sizes for the types to try and auto construct the
   types. Some specific systems with known issues are handled specially.
*/


#ifndef _PORTABLE_INTTYPES_H
#define _PORTABLE_INTTYPES_H

#ifndef _INTTYPES_DEFINED
#define _INTTYPES_DEFINED
  /* first, certain known systems are handled specially */
  #if defined(WIN32) && defined(_MSC_VER)
    typedef signed __int8      int8_t;
    typedef unsigned __int8   uint8_t;
    typedef __int16           int16_t;
    typedef unsigned __int16 uint16_t;
    typedef __int32           int32_t;
    typedef unsigned __int32 uint32_t;
    typedef __int64           int64_t;
    typedef unsigned __int64 uint64_t;

    typedef          int     intptr_t; 
    typedef unsigned int    uintptr_t; 
  #elif defined(_CRAYT3E)
    /* oddball architecture lacks a 16-bit type */
    typedef signed char        int8_t;
    typedef unsigned char     uint8_t;
    typedef short             int16_t; /* This is 32-bits, should be 16 !!! */
    typedef unsigned short   uint16_t; /* This is 32-bits, should be 16 !!! */
    typedef short             int32_t;
    typedef unsigned short   uint32_t;
    typedef int               int64_t;
    typedef unsigned int     uint64_t;

    typedef          int     intptr_t; 
    typedef unsigned int    uintptr_t; 
  #elif defined(_SX)
    #include <sys/types.h> /* provides int32_t and uint32_t - use to prevent conflict */
    typedef signed char        int8_t;
    typedef unsigned char     uint8_t;
    typedef short             int16_t;
    typedef unsigned short   uint16_t;
    typedef long              int64_t;
    typedef unsigned long    uint64_t;

    typedef          long    intptr_t; 
    typedef unsigned long   uintptr_t; 
  #elif defined(__CYGWIN__)
    /* inttypes.h is incomplete or missing on some versions of cygwin */
    #include <sys/types.h>
    #ifndef __uint8_t_defined
      typedef u_int8_t     uint8_t;
    #endif
    #ifndef __uint16_t_defined
      typedef u_int16_t   uint16_t; 
    #endif
    #ifndef __uint32_t_defined
      typedef u_int32_t   uint32_t;
    #endif
    #ifndef __uint64_t_defined
      typedef u_int64_t   uint64_t;
    #endif
    #ifndef __intptr_t_defined
      typedef          int     intptr_t; 
    #endif
    #ifndef __uintptr_t_defined
      typedef unsigned int    uintptr_t; 
    #endif
  #elif defined(HAVE_INTTYPES_H)
    /* configure says the system header is available, so use it */
    #include <inttypes.h>
    #ifndef _USING_INTTYPES_H
    #define _USING_INTTYPES_H
    #endif
  #elif defined(SIZEOF_CHAR) && \
        defined(SIZEOF_SHORT) && \
        defined(SIZEOF_INT) && \
        defined(SIZEOF_LONG) && \
        defined(SIZEOF_VOID_P)
      /* configure-detected integer sizes are available, so use those to automatically detect the sizes */
      #if SIZEOF_CHAR == 1
        typedef signed   char  int8_t;
        typedef unsigned char uint8_t;
      #else
        #error Cannot find an 8-bit type for your platform
      #endif

      #if SIZEOF_CHAR == 2
        typedef signed   char  int16_t;
        typedef unsigned char uint16_t;
      #elif SIZEOF_SHORT == 2
        typedef          short  int16_t;
        typedef unsigned short uint16_t;
      #elif SIZEOF_INT == 2
        typedef          int  int16_t;
        typedef unsigned int uint16_t;
      #else
        #error Cannot find a 16-bit type for your platform
      #endif

      #if SIZEOF_SHORT == 4
        typedef          short  int32_t;
        typedef unsigned short uint32_t;
      #elif SIZEOF_INT == 4
        typedef          int  int32_t;
        typedef unsigned int uint32_t;
      #elif SIZEOF_LONG == 4
        typedef          long  int32_t;
        typedef unsigned long uint32_t;
      #else
        #error Cannot find a 32-bit type for your platform
      #endif

      #if SIZEOF_INT == 8
        typedef          int  int64_t;
        typedef unsigned int uint64_t;
      #elif SIZEOF_LONG == 8
        typedef          long  int64_t;
        typedef unsigned long uint64_t;
      #elif SIZEOF_LONG_LONG == 8
        typedef          long long  int64_t;
        typedef unsigned long long uint64_t;
      #else
        #error Cannot find a 64-bit type for your platform
      #endif

      #if SIZEOF_VOID_P == SIZEOF_SHORT
        typedef          short  intptr_t;
        typedef unsigned short uintptr_t;
      #elif SIZEOF_VOID_P == SIZEOF_INT
        typedef          int  intptr_t;
        typedef unsigned int uintptr_t;
      #elif SIZEOF_VOID_P == SIZEOF_LONG
        typedef          long  intptr_t;
        typedef unsigned long uintptr_t;
      #elif SIZEOF_VOID_P == SIZEOF_LONG_LONG
        typedef          long long  intptr_t;
        typedef unsigned long long uintptr_t;
      #else
        #error Cannot find a integral pointer-sized type for your platform
      #endif  
  #else
    /* no information available, so try inttypes.h and hope for the best 
       if we die here, the correct fix is to detect the sizes using configure 
       (and include *config.h before this file).
     */
    #include <inttypes.h>
    #ifndef _USING_INTTYPES_H
    #define _USING_INTTYPES_H
    #endif
  #endif

  #if defined(HPUX) && defined(__STDC_32_MODE__) && defined(_USING_INTTYPES_H)
    /* HPUX inttypes.h stupidly omits these in some cases */
    typedef          long long  int64_t;
    typedef unsigned long long uint64_t;
  #endif
#endif /* _INTTYPES_DEFINED */

#endif /* _PORTABLE_INTTYPES_H */

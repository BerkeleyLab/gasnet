/*  $Archive:: /Ti/GASNet/extended/gasnet_extended_help.h                 $
 *     $Date: 2003/06/16 10:24:13 $
 * $Revision: 1.13 $
 * Description: GASNet Extended API Header Helpers (Internal code, not for client use)
 * Copyright 2002, Dan Bonachea <bonachea@cs.berkeley.edu>
 * Terms of use are as specified in license.txt
 */

#ifndef _IN_GASNET_H
  #error This file is not meant to be included directly- clients should include gasnet.h
#endif

#ifndef _GASNET_EXTENDED_HELP_H
#define _GASNET_EXTENDED_HELP_H

BEGIN_EXTERNC

#include <gasnet_help.h>

/* ------------------------------------------------------------------------------------ */
extern gasnet_node_t gasnete_mynode;
extern gasnet_node_t gasnete_nodes;
extern gasnet_seginfo_t *gasnete_seginfo;

#ifdef GASNETI_THREADS
  struct _gasnete_threaddata_t;
  extern struct _gasnete_threaddata_t *gasnete_mythread() __attribute__ ((const));
  #if defined(__xlC__)
    #if 0
      /* this should work according to the &*@&#$! IBM compiler docs, 
         but of course it doesn't... */
      #pragma options pure=gasnete_mythread
    #else
      #pragma isolated_call(gasnete_mythread)
    #endif
  #endif
  /* TODO: mark gasnete_mythread() as a pure function for other compilers */
#endif

/* gasnete_islocal() is used by put/get fns to decide whether shared memory on 
   a given node is "local". By default this is based on comparing the nodeid to
   the local node id, but clients can override this to remove the check overhead
   by defining either GASNETE_PUTGET_ALWAYSLOCAL or GASNETE_PUTGET_ALWAYSREMOTE
 */
#if defined(GASNETE_PUTGET_ALWAYSLOCAL)
  #define gasnete_islocal(nodeid) (1) /* always local */
#elif defined(GASNETE_PUTGET_ALWAYSREMOTE)
  #define gasnete_islocal(nodeid) (0) /* always remote */
#else
  #define gasnete_islocal(nodeid) (nodeid == gasnete_mynode)
#endif
#define gasnete_boundscheck(node,ptr,nbytes) gasneti_boundscheck(node,ptr,nbytes,e)

/* busy-waits, with no implicit polling (cnd should include an embedded poll)
   differs from GASNET_BLOCKUNTIL because it may be waiting for an event
     caused by the receipt of a non-AM message
 */
#define gasnete_waituntil(cnd) gasnete_waitwhile(!(cnd)) 
#define gasnete_waitwhile(cnd) do { /* could add something here */ } while (cnd) 

#define gasnete_polluntil(cnd) gasnete_pollwhile(!(cnd)) 
#define gasnete_pollwhile(cnd) while (cnd) gasnet_AMPoll() 

/* ------------------------------------------------------------------------------------ */
#if defined(_CRAYC) || (SIZEOF_SHORT > 2)  /* deal with Cray's crappy lack of 16-bit types */
  #define OMIT_WHEN_MISSING_16BIT(code) 
#else
  #define OMIT_WHEN_MISSING_16BIT(code) code
#endif
/*  undefined results if the regions are overlapping */
#define GASNETE_FAST_ALIGNED_MEMCPY(dest, src, nbytes) do { \
  switch(nbytes) {                                          \
    case 0:                                                 \
      break;                                                \
    case sizeof(uint8_t):                                   \
      *((uint8_t *)(dest)) = *((uint8_t *)(src));           \
      break;                                                \
  OMIT_WHEN_MISSING_16BIT(                                  \
    case sizeof(uint16_t):                                  \
      *((uint16_t *)(dest)) = *((uint16_t *)(src));         \
      break;                                                \
  )                                                         \
    case sizeof(uint32_t):                                  \
      *((uint32_t *)(dest)) = *((uint32_t *)(src));         \
      break;                                                \
    case sizeof(uint64_t):                                  \
      *((uint64_t *)(dest)) = *((uint64_t *)(src));         \
      break;                                                \
    default:                                                \
      memcpy(dest, src, nbytes);                            \
  } } while(0)

#define GASNETE_FAST_UNALIGNED_MEMCPY(dest, src, nbytes) memcpy(dest, src, nbytes)

/* given the address of a gasnet_register_value_t object and the number of
   significant bytes, return the byte address where significant bytes begin */
#ifdef WORDS_BIGENDIAN
  #define GASNETE_STARTOFBITS(regvalptr,nbytes) \
    (((uint8_t*)(regvalptr)) + ((sizeof(gasnet_register_value_t)-nbytes)))
#else /* little-endian */
  #define GASNETE_STARTOFBITS(regvalptr,nbytes) (regvalptr)
#endif

/* The value written to the target address is a direct byte copy of the 
   8*nbytes low-order bits of value, written with the endianness appropriate 
   for an nbytes integral value on the current architecture
   */
#define GASNETE_VALUE_ASSIGN(dest, value, nbytes) do {                  \
  switch (nbytes) {                                                     \
    case 0:                                                             \
      break;                                                            \
    case sizeof(uint8_t):                                               \
      *((uint8_t *)(dest)) = (uint8_t)(value);                          \
      break;                                                            \
  OMIT_WHEN_MISSING_16BIT(                                              \
    case sizeof(uint16_t):                                              \
      *((uint16_t *)(dest)) = (uint16_t)(value);                        \
      break;                                                            \
  )                                                                     \
    case sizeof(uint32_t):                                              \
      *((uint32_t *)(dest)) = (uint32_t)(value);                        \
      break;                                                            \
    case sizeof(uint64_t):                                              \
      *((uint64_t *)(dest)) = (uint64_t)(value);                        \
      break;                                                            \
    default:  /* no such native nbytes integral type */                 \
      memcpy((dest), GASNETE_STARTOFBITS(&(value),nbytes), nbytes);     \
  } } while (0)

#ifdef NDEBUG
  #define gasnete_aligncheck(ptr,nbytes)
#else
  #if 0
    #define gasnete_aligncheck(ptr,nbytes) do {       \
        if ((nbytes) <= 8 && (nbytes) % 2 == 0)       \
          assert(((uintptr_t)(ptr)) % (nbytes) == 0); \
      } while (0)
  #else
    static uint8_t _gasnete_aligncheck[600];
    #define gasnete_aligncheck(ptr,nbytes) do {                                         \
        uint8_t *_gasnete_alignbuf =                                                    \
          (uint8_t *)((((uintptr_t)&_gasnete_aligncheck) + 0xFF) & ~((uintptr_t)0xFF)); \
        uintptr_t offset = ((uintptr_t)(ptr)) & 0xFF;                                   \
        uint8_t *p = _gasnete_alignbuf + offset;                                        \
        assert(p >= _gasnete_aligncheck &&                                              \
              (p + 8) < (_gasnete_aligncheck+sizeof(_gasnete_aligncheck)));             \
        /* NOTE: a runtime bus error in this code indicates the relevant pointer        \
            was not "properly aligned for accessing objects of size nbytes", as         \
            required by the GASNet spec for src/dest addresses in non-bulk puts/gets    \
         */                                                                             \
        switch (nbytes) {                                                               \
          case 1: *(uint8_t *)p = 0; break;                                             \
        OMIT_WHEN_MISSING_16BIT(                                                        \
          case 2: *(uint16_t *)p = 0; break;                                            \
        )                                                                               \
          case 4: *(uint32_t *)p = 0; break;                                            \
          case 8: *(uint64_t *)p = 0; break;                                            \
        }                                                                               \
      } while (0)
  #endif
#endif

/* get membar */
#include <gasnet_atomicops.h>

#ifndef gasneti_memsync
  #ifdef GASNETI_THREADS
    #define gasneti_memsync() gasneti_local_membar()
  #else
    #define gasneti_memsync() 
  #endif
#endif

/* ------------------------------------------------------------------------------------ */
/* thread-id optimization support */
#ifdef GASNETI_THREADINFO_OPT
  #define GASNETE_THREAD_FARG_ALONE   gasnet_threadinfo_t const _threadinfo
  #define GASNETE_THREAD_FARG         , GASNETE_THREAD_FARG_ALONE
  #define GASNETE_THREAD_GET_ALONE    GASNET_GET_THREADINFO()
  #define GASNETE_THREAD_GET          , GASNETE_THREAD_GET_ALONE
  #define GASNETE_THREAD_PASS_ALONE   (_threadinfo)
  #define GASNETE_THREAD_PASS         , GASNETE_THREAD_PASS_ALONE
  #define GASNETE_THREAD_SWALLOW(x)
  #define GASNETE_TISTARTOFBITS(ptr,nbytes,ti) GASNETE_STARTOFBITS(ptr,nbytes)
  #define GASNETE_MYTHREAD            ((gasnete_threaddata_t *)_threadinfo)
#else
  #define GASNETE_THREAD_FARG_ALONE   
  #define GASNETE_THREAD_FARG         
  #define GASNETE_THREAD_GET_ALONE   
  #define GASNETE_THREAD_GET         
  #define GASNETE_THREAD_PASS_ALONE   
  #define GASNETE_THREAD_PASS         
  #define GASNETE_THREAD_SWALLOW(x)
  #define GASNETE_TISTARTOFBITS       GASNETE_STARTOFBITS
  #define GASNETE_MYTHREAD            (gasnete_mythread())
#endif
/* ------------------------------------------------------------------------------------ */

END_EXTERNC

#endif

/*  $Archive:: /Ti/GASNet/gasnet.h                                        $
 *     $Date: 2003/06/29 08:09:40 $
 * $Revision: 1.16 $
 * Description: GASNet Header
 * Copyright 2002, Dan Bonachea <bonachea@cs.berkeley.edu>
 * Terms of use are as specified in license.txt
 */

#ifndef _GASNET_H
#define _GASNET_H
#define _IN_GASNET_H
#define _INCLUDED_GASNET_H
#ifdef _INCLUDED_GASNET_TOOLS_H
  #error Applications that use both GASNet and GASNet tools must   \
         include gasnet.h before gasnet_tools.h and must include   \
         _both_ headers in any files that need either header
#endif

/* Usage:
   see the GASNet specification for details on how to use the GASNet interface
   clients should #define NDEBUG when compiling this implementation for production use
     or #define DEBUG for extra debugging safety checks
*/

/* Conventions:
* All entry points required by GASNet spec are lower-case identifiers with the prefix gasnet_ 
* All constants required by GASNet spec are upper-case and preceded with the prefix GASNET_
* All private symbols in the reference extended API implementation are prefixed with gasnete_ (or GASNETE_ for macros)
* All private symbols in a core API implementation are prefixed with gasnetc_ (or GASNETC_ for macros)
* All private symbols shared throughout GASNet are prefixed with gasneti_ (or GASNETI_ for macros)
*/

/* ------------------------------------------------------------------------------------ */
/* check threading configuration */
#if defined(GASNET_SEQ) && !defined(GASNET_PARSYNC) && !defined(GASNET_PAR)
  #define GASNETI_THREADMODEL SEQ
#elif !defined(GASNET_SEQ) && defined(GASNET_PARSYNC) && !defined(GASNET_PAR)
  #define GASNETI_THREADMODEL PARSYNC
#elif !defined(GASNET_SEQ) && !defined(GASNET_PARSYNC) && defined(GASNET_PAR)
  #define GASNETI_THREADMODEL PAR
#else
  #error Client code must #define exactly one of (GASNET_PAR, GASNET_PARSYNC, GASNET_SEQ) before #including gasnet.h
#endif

#if defined(GASNET_PAR) || defined(GASNET_PARSYNC)
  #define GASNETI_THREADS
#endif

#if !((defined(DEBUG) && !defined(NDEBUG)) || (!defined(DEBUG) && defined(NDEBUG)))
  #error Client code #define exactly one of (DEBUG or NDEBUG) to select GASNet build configuration
#endif

/* codify other configuration settings */
#ifdef DEBUG
  #define GASNETI_DEBUG_CONFIG "debug"
#else
  #define GASNETI_DEBUG_CONFIG "nodebug"
#endif

#ifdef TRACE
  #define GASNETI_TRACE_CONFIG "trace"
#else
  #define GASNETI_TRACE_CONFIG "notrace"
#endif

#ifdef STATS
  #define GASNETI_STATS_CONFIG "stats"
#else
  #define GASNETI_STATS_CONFIG "nostats"
#endif

/* autoconf-generated configuration header */
#include <gasnet_config.h>

/* basic utilities used in the headers */
#include <gasnet_basic.h>

/* ------------------------------------------------------------------------------------ */
/* check segment configuration */

#if defined(GASNET_SEGMENT_FAST) && !defined(GASNET_SEGMENT_LARGE) && !defined(GASNET_SEGMENT_EVERYTHING)
  #define GASNETI_SEGMENT_CONFIG FAST
#elif !defined(GASNET_SEGMENT_FAST) && defined(GASNET_SEGMENT_LARGE) && !defined(GASNET_SEGMENT_EVERYTHING)
  #define GASNETI_SEGMENT_CONFIG LARGE
#elif !defined(GASNET_SEGMENT_FAST) && !defined(GASNET_SEGMENT_LARGE) && defined(GASNET_SEGMENT_EVERYTHING)
  #define GASNETI_SEGMENT_CONFIG EVERYTHING
#else
  #error Segment configuration must be exactly one of (GASNET_SEGMENT_FAST, GASNET_SEGMENT_LARGE, GASNET_SEGMENT_EVERYTHING) 
#endif

/* ensure that client links the correct library */
#ifdef GASNET_SEGMENT_EVERYTHING
  #define gasnet_init _CONCAT(_CONCAT(gasnet_init_GASNET_,GASNETI_THREADMODEL),GASNETI_SEGMENT_CONFIG)
#else
  #define gasnet_init _CONCAT(gasnet_init_GASNET_,GASNETI_THREADMODEL)
#endif

/* ------------------------------------------------------------------------------------ */
/* GASNet forward definitions, which may override some of the defaults below */
#include <gasnet_core_fwd.h>
#include <gasnet_extended_fwd.h>

/* ------------------------------------------------------------------------------------ */
/* constants */
#ifndef GASNET_VERSION
  /*  an integer representing the major version of the GASNet spec to which this implementation complies */
  #define GASNET_VERSION 1
#endif

#ifndef GASNETI_RELEASE_VERSION
  /* the public distribution release identifier */
  #define GASNETI_RELEASE_VERSION 1.2
#endif

#ifndef GASNET_MAXNODES
  /*  an integer representing the maximum number of nodes supported in a single GASNet job */
  #define GASNET_MAXNODES (0x7FFFFFFFu)
#endif

#ifndef GASNET_ALIGNED_SEGMENTS
  /*  defined to be 1 if gasnet_init guarantees that the remote-access memory segment will be aligned  */
  /*  at the same virtual address on all nodes. defined to 0 otherwise */
  #error GASNet core failed to define GASNET_ALIGNED_SEGMENTS to 0 or 1
#endif

#if GASNET_ALIGNED_SEGMENTS
  #define GASNETI_ALIGN_CONFIG "align"
#else 
  #define GASNETI_ALIGN_CONFIG "noalign"
#endif

#ifndef GASNET_BARRIERFLAG_ANONYMOUS
  /* barrier flags */
  #define GASNET_BARRIERFLAG_ANONYMOUS 1
  #define GASNET_BARRIERFLAG_MISMATCH  2
#endif

/* Errors: GASNET_OK must be zero */
#define GASNET_OK   0 

#ifndef _GASNET_ERRORS
#define _GASNET_ERRORS
  #define _GASNET_ERR_BASE 10000
  #define GASNET_ERR_NOT_INIT             (_GASNET_ERR_BASE+1)
  #define GASNET_ERR_RESOURCE             (_GASNET_ERR_BASE+2)
  #define GASNET_ERR_BAD_ARG              (_GASNET_ERR_BASE+3)
  #define GASNET_ERR_NOT_READY            (_GASNET_ERR_BASE+4)
  #define GASNET_ERR_BARRIER_MISMATCH     (_GASNET_ERR_BASE+5)
#endif

#ifndef _GASNET_ERRORNAME
#define _GASNET_ERRORNAME
GASNET_INLINE_MODIFIER(gasnet_ErrorName)
char *gasnet_ErrorName(int errval) {
  switch (errval) {
    case GASNET_OK:           return "GASNET_OK";      
    case GASNET_ERR_NOT_INIT: return "GASNET_ERR_NOT_INIT";      
    case GASNET_ERR_BAD_ARG:  return "GASNET_ERR_BAD_ARG";       
    case GASNET_ERR_RESOURCE: return "GASNET_ERR_RESOURCE";      
    case GASNET_ERR_BARRIER_MISMATCH: return "GASNET_ERR_BARRIER_MISMATCH";      
    case GASNET_ERR_NOT_READY: return "GASNET_ERR_NOT_READY";      
    default: return "*unknown*";
  }
}
#endif

#ifndef _GASNET_ERRORDESC
#define _GASNET_ERRORDESC
GASNET_INLINE_MODIFIER(gasnet_ErrorDesc)
char *gasnet_ErrorDesc(int errval) {
  switch (errval) {
    case GASNET_OK:           return "No error";      
    case GASNET_ERR_NOT_INIT: return "GASNet message layer not initialized"; 
    case GASNET_ERR_BAD_ARG:  return "Invalid function parameter passed";    
    case GASNET_ERR_RESOURCE: return "Problem with requested resource";      
    case GASNET_ERR_BARRIER_MISMATCH: return "Barrier id's mismatched";      
    case GASNET_ERR_NOT_READY: return "Non-blocking operation not complete";      
    default: return "no description available";
  }
}
#endif

/* ------------------------------------------------------------------------------------ */
/* core types */

#ifndef _GASNET_NODE_T
#define _GASNET_NODE_T
  /*  unsigned integer type representing a unique 0-based node index */
  typedef uint32_t gasnet_node_t;
#endif

#ifndef _GASNET_HANDLER_T
#define _GASNET_HANDLER_T
  /*  an unsigned integer type representing an index into the core API AM handler table */
  typedef uint8_t gasnet_handler_t;
#endif

#ifndef _GASNET_TOKEN_T
#define _GASNET_TOKEN_T
  /*  an opaque type passed to core API handlers which may be used to query message information  */
  typedef void *gasnet_token_t;
#endif

#ifndef _GASNET_HANDLERARG_T
#define _GASNET_HANDLERARG_T
  /*  a 32-bit signed integer type which is used to express the user-provided arguments to all AM handlers. Platforms lacking a native 32-bit type may define this to a 64-bit type, but only the lower 32-bits are transmitted during an AM message send (and sign-extended on the receiver). */
  typedef int32_t gasnet_handlerarg_t;
#endif

#ifndef _GASNET_HANDLERENTRY_T
#define _GASNET_HANDLERENTRY_T
  /*  struct type used to negotiate handler registration in gasnet_init() */
  typedef struct {
    gasnet_handler_t index; /*  == 0 for don't care  */
    void (*fnptr)();    
  } gasnet_handlerentry_t;
#endif

#ifndef _GASNET_SEGINFO_T
#define _GASNET_SEGINFO_T
  typedef struct {
    void *addr;
    uintptr_t size;
  } gasnet_seginfo_t;
#endif

#ifndef _GASNET_THREADINFO_T
#define _GASNET_THREADINFO_T
  typedef void *gasnet_threadinfo_t;
#endif

#ifndef GASNET_PAGESIZE
  #ifdef GASNETI_PAGESIZE
    #define GASNET_PAGESIZE GASNETI_PAGESIZE
  #elif defined(CRAYT3E)
    /* on Cray: shmemalign allocates mem aligned across nodes, 
        but there seems to be no fixed page size (man pagesize)
        this is probably because they don't support VM
       actual page size is set separately for each linker section, 
        ranging from 512KB(default) to 8MB
       Here we return 1 to reflect the lack of page alignment constraints
   */

    #define GASNET_PAGESIZE 1
  #else
    #error GASNET_PAGESIZE unknown and not set by conduit
  #endif
#endif

/* ------------------------------------------------------------------------------------ */
/* extended types */

#ifndef _GASNET_HANDLE_T
#define _GASNET_HANDLE_T
  /*  an opaque type representing a non-blocking operation in-progress initiated using the extended API */
  typedef void *gasnet_handle_t;
  #define GASNET_INVALID_HANDLE ((gasnet_handle_t)0)
#endif

#ifndef _GASNET_REGISTER_VALUE_T
#define _GASNET_REGISTER_VALUE_T
  /*  the largest unsigned integer type that can fit entirely in a single CPU register for the current architecture and ABI.  */
  /*  SIZEOF_GASNET_REGISTER_VALUE_T is a preprocess-time literal integer constant (i.e. not "sizeof()")indicating the size of this type in bytes */
  typedef uintptr_t gasnet_register_value_t;
  #define SIZEOF_GASNET_REGISTER_VALUE_T  SIZEOF_VOID_P
#endif

#ifndef _GASNET_THREADINFO_T
#define _GASNET_THREADINFO_T
typedef void *gasnet_threadinfo_t;
#endif

/* ------------------------------------------------------------------------------------ */

/* Main core header */
#include <gasnet_core.h>

/* Main extended header */
#include <gasnet_extended.h>

/* ------------------------------------------------------------------------------------ */

#ifndef GASNET_BEGIN_FUNCTION
  #error GASNet extended API failed to define GASNET_BEGIN_FUNCTION
#endif

#ifndef GASNET_HSL_INITIALIZER
  #error GASNet core failed to define GASNET_HSL_INITIALIZER
#endif

#ifndef GASNET_BLOCKUNTIL
  #error GASNet core failed to define GASNET_BLOCKUNTIL
#endif

#ifndef SIZEOF_GASNET_REGISTER_VALUE_T
  #error GASNet failed to define SIZEOF_GASNET_REGISTER_VALUE_T
#endif

/* GASNET_CONFIG_STRING
   a string representing all the relevant GASNet configuration settings 
   that can be compared using string compare to verify version compatibility
   The string is also embedded into the library itself such that it can be 
   scanned for within a binary executable.
*/
#ifndef GASNET_CONFIG_STRING
  /* allow extension by core and extended (all extensions should follow the same 
     basic comma-delimited feature format below, and include an initial comma)
   */
  #ifndef GASNETC_EXTRA_CONFIG_INFO 
    #define GASNETC_EXTRA_CONFIG_INFO
  #endif
  #ifndef GASNETE_EXTRA_CONFIG_INFO
    #define GASNETE_EXTRA_CONFIG_INFO
  #endif
  #define GASNET_CONFIG_STRING                                            \
             "RELEASE=" _STRINGIFY(GASNETI_RELEASE_VERSION) ","           \
             "SPEC=" _STRINGIFY(GASNET_VERSION) ","                       \
             "CONDUIT="                                                   \
             GASNET_CORE_NAME_STR "-" GASNET_CORE_VERSION_STR "/"         \
             GASNET_EXTENDED_NAME_STR "-" GASNET_EXTENDED_VERSION_STR "," \
             "THREADMODEL=" _STRINGIFY(GASNETI_THREADMODEL) ","           \
             "SEGMENT=" _STRINGIFY(GASNETI_SEGMENT_CONFIG) ","            \
             GASNETI_ALIGN_CONFIG ","                                     \
             GASNETI_DEBUG_CONFIG ","                                     \
             GASNETI_TRACE_CONFIG ","                                     \
             GASNETI_STATS_CONFIG                                         \
             GASNETC_EXTRA_CONFIG_INFO                                    \
             GASNETE_EXTRA_CONFIG_INFO                                    
#endif
/* ------------------------------------------------------------------------------------ */

#undef _IN_GASNET_H
#endif

/*   $Source: /Users/kamil/work/gasnet-cvs2/gasnet/gasnet.h,v $
 *     $Date: 2004/09/20 12:10:48 $
 * $Revision: 1.30 $
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
   clients should #define GASNET_NDEBUG when compiling this implementation for production use
     or #define GASNET_DEBUG for extra debugging safety checks
*/

/* ------------------------------------------------------------------------------------ */
/* check threading configuration */
#if defined(GASNET_SEQ) && !defined(GASNET_PARSYNC) && !defined(GASNET_PAR)
  #undef GASNET_SEQ
  #define GASNET_SEQ 1
  #define GASNETI_THREADMODEL SEQ
#elif !defined(GASNET_SEQ) && defined(GASNET_PARSYNC) && !defined(GASNET_PAR)
  #undef GASNET_PARSYNC
  #define GASNET_PARSYNC 1
  #define GASNETI_THREADMODEL PARSYNC
#elif !defined(GASNET_SEQ) && !defined(GASNET_PARSYNC) && defined(GASNET_PAR)
  #undef GASNET_PAR
  #define GASNET_PAR 1
  #define GASNETI_THREADMODEL PAR
#else
  #error Client code must #define exactly one of (GASNET_PAR, GASNET_PARSYNC, GASNET_SEQ) before #including gasnet.h
#endif

/* GASNETI_CLIENT_THREADS = GASNet client has multiple application threads */
#if GASNET_PAR || GASNET_PARSYNC
  #define GASNETI_CLIENT_THREADS 1
#elif defined(GASNETI_CLIENT_THREADS)
  #error bad defn of GASNETI_CLIENT_THREADS
#endif

#if !((defined(GASNET_DEBUG) && !defined(GASNET_NDEBUG)) || (!defined(GASNET_DEBUG) && defined(GASNET_NDEBUG)))
  #error Client code #define exactly one of (GASNET_DEBUG or GASNET_NDEBUG) to select GASNet build configuration
#endif

/* codify other configuration settings */
#ifdef GASNET_DEBUG
  #undef GASNET_DEBUG
  #define GASNET_DEBUG 1
  #define GASNETI_DEBUG_CONFIG debug
#else
  #define GASNETI_DEBUG_CONFIG nodebug
#endif

#ifdef GASNET_TRACE
  #undef GASNET_TRACE
  #define GASNET_TRACE 1
  #define GASNETI_TRACE_CONFIG trace
#else
  #define GASNETI_TRACE_CONFIG notrace
#endif

#ifdef GASNET_STATS
  #undef GASNET_STATS
  #define GASNET_STATS 1
  #define GASNETI_STATS_CONFIG stats
#else
  #define GASNETI_STATS_CONFIG nostats
#endif

#if defined(GASNET_STATS) || defined(GASNET_TRACE)
  #define GASNETI_STATS_OR_TRACE 1
#elif defined(GASNETI_STATS_OR_TRACE)
  #error bad def of GASNETI_STATS_OR_TRACE
#endif

/* autoconf-generated configuration header */
#include <gasnet_config.h>

/* basic utilities used in the headers */
#include <gasnet_basic.h>

/* ------------------------------------------------------------------------------------ */
/* check segment configuration */

#if defined(GASNET_SEGMENT_FAST) && !defined(GASNET_SEGMENT_LARGE) && !defined(GASNET_SEGMENT_EVERYTHING)
  #undef GASNET_SEGMENT_FAST
  #define GASNET_SEGMENT_FAST 1
  #define GASNETI_SEGMENT_CONFIG FAST
#elif !defined(GASNET_SEGMENT_FAST) && defined(GASNET_SEGMENT_LARGE) && !defined(GASNET_SEGMENT_EVERYTHING)
  #undef GASNET_SEGMENT_LARGE
  #define GASNET_SEGMENT_LARGE 1
  #define GASNETI_SEGMENT_CONFIG LARGE
#elif !defined(GASNET_SEGMENT_FAST) && !defined(GASNET_SEGMENT_LARGE) && defined(GASNET_SEGMENT_EVERYTHING)
  #undef GASNET_SEGMENT_EVERYTHING
  #define GASNET_SEGMENT_EVERYTHING 1
  #define GASNETI_SEGMENT_CONFIG EVERYTHING
#else
  #error Segment configuration must be exactly one of (GASNET_SEGMENT_FAST, GASNET_SEGMENT_LARGE, GASNET_SEGMENT_EVERYTHING) 
#endif

/* additional safety check, in case a very smart linker removes all of the checks at the end of this file */
#define gasnet_init _CONCAT(_CONCAT(_CONCAT(_CONCAT(_CONCAT(gasnet_init_GASNET_,GASNETI_THREADMODEL),GASNETI_SEGMENT_CONFIG),GASNETI_DEBUG_CONFIG),GASNETI_TRACE_CONFIG),GASNETI_STATS_CONFIG)

/* ------------------------------------------------------------------------------------ */
/* GASNet forward definitions, which may override some of the defaults below */
#include <gasnet_core_fwd.h>
#include <gasnet_extended_fwd.h>

/* GASNETI_CONDUIT_THREADS = GASNet conduit has one or more private threads
                             which may be used to run AM handlers */
#if defined(GASNETI_CONDUIT_THREADS) && (GASNETI_CONDUIT_THREADS != 1)
  #error bad defn of GASNETI_CONDUIT_THREADS
#endif

#if GASNETI_CLIENT_THREADS || GASNETI_CONDUIT_THREADS
  #define GASNETI_THREADS 1
#elif defined(GASNETI_THREADS)
  #error bad defn of GASNETI_THREADS
#endif

/* ------------------------------------------------------------------------------------ */
/* constants */
#ifndef GASNET_VERSION
  /*  an integer representing the major version of the GASNet spec to which this implementation complies */
  #define GASNET_VERSION 1
#endif

#ifndef GASNETI_RELEASE_VERSION
  /* the public distribution release identifier */
  #define GASNETI_RELEASE_VERSION 1.5
#endif

#ifndef GASNET_MAXNODES
  /*  an integer representing the maximum number of nodes supported in a single GASNet job */
  #define GASNET_MAXNODES (0x7FFFFFFFu)
#endif

#if !defined(GASNET_ALIGNED_SEGMENTS) || \
    (defined(GASNET_ALIGNED_SEGMENTS) && GASNET_ALIGNED_SEGMENTS != 0 && GASNET_ALIGNED_SEGMENTS != 1)
  /*  defined to be 1 if gasnet_init guarantees that the remote-access memory segment will be aligned  */
  /*  at the same virtual address on all nodes. defined to 0 otherwise */
  #error GASNet core failed to define GASNET_ALIGNED_SEGMENTS to 0 or 1
#endif

#if GASNET_ALIGNED_SEGMENTS
  #define GASNETI_ALIGN_CONFIG align
#else 
  #define GASNETI_ALIGN_CONFIG noalign
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
const char *gasnet_ErrorName(int errval) {
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
const char *gasnet_ErrorDesc(int errval) {
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

#ifndef _GASNET_WAITMODE
#define _GASNET_WAITMODE
  #define GASNET_WAIT_SPIN      0 /* contend aggressively for CPU resources while waiting (spin) */
  #define GASNET_WAIT_BLOCK     1 /* yield CPU resources immediately while waiting (block) */
  #define GASNET_WAIT_SPINBLOCK 2 /* spin for an implementation-dependent period, then block */
  extern int gasneti_set_waitmode(int wait_mode);
  #define gasnet_set_waitmode(wait_mode) gasneti_set_waitmode(wait_mode)
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
  #if GASNET_PAGESIZE <= 0
    #error bad defn of GASNET_PAGESIZE
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

#ifndef _GASNET_MEMVEC_T
#define _GASNET_MEMVEC_T
  typedef struct {
    void *addr;
    size_t len;
  } gasnet_memvec_t;
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
             "PTR=" _STRINGIFY(GASNETI_PTR_CONFIG) ","                    \
             _STRINGIFY(GASNETI_ALIGN_CONFIG) ","                         \
             _STRINGIFY(GASNETI_DEBUG_CONFIG) ","                         \
             _STRINGIFY(GASNETI_TRACE_CONFIG) ","                         \
             _STRINGIFY(GASNETI_STATS_CONFIG)                             \
             GASNETC_EXTRA_CONFIG_INFO                                    \
             GASNETE_EXTRA_CONFIG_INFO                                    
#endif

/* ensure that the client links the correct library configuration
 * all objects in a given executable (client and library) must agree on all
 * of the following configuration settings, otherwise MANY things break,
 * often in very subtle and confusing ways (eg GASNet mutexes, threadinfo, etc.)
 */
#define GASNETI_LINKCONFIG_IDIOTCHECK(name) _CONCAT(gasneti_linkconfig_idiotcheck_,name)
extern int GASNETI_LINKCONFIG_IDIOTCHECK(GASNETI_THREADMODEL);
extern int GASNETI_LINKCONFIG_IDIOTCHECK(GASNETI_SEGMENT_CONFIG);
extern int GASNETI_LINKCONFIG_IDIOTCHECK(GASNETI_DEBUG_CONFIG);
extern int GASNETI_LINKCONFIG_IDIOTCHECK(GASNETI_TRACE_CONFIG);
extern int GASNETI_LINKCONFIG_IDIOTCHECK(GASNETI_STATS_CONFIG);
extern int GASNETI_LINKCONFIG_IDIOTCHECK(GASNETI_ALIGN_CONFIG);
extern int GASNETI_LINKCONFIG_IDIOTCHECK(GASNETI_PTR_CONFIG);

static int *gasneti_linkconfig_idiotcheck();
static int *(*_gasneti_linkconfig_idiotcheck)() = &gasneti_linkconfig_idiotcheck;
static int *gasneti_linkconfig_idiotcheck() {
  static int val;
  val +=  GASNETI_LINKCONFIG_IDIOTCHECK(GASNETI_THREADMODEL)
        + GASNETI_LINKCONFIG_IDIOTCHECK(GASNETI_SEGMENT_CONFIG)
        + GASNETI_LINKCONFIG_IDIOTCHECK(GASNETI_DEBUG_CONFIG)
        + GASNETI_LINKCONFIG_IDIOTCHECK(GASNETI_TRACE_CONFIG)
        + GASNETI_LINKCONFIG_IDIOTCHECK(GASNETI_STATS_CONFIG)
        + GASNETI_LINKCONFIG_IDIOTCHECK(GASNETI_ALIGN_CONFIG)
        + GASNETI_LINKCONFIG_IDIOTCHECK(GASNETI_PTR_CONFIG);
  if (_gasneti_linkconfig_idiotcheck != gasneti_linkconfig_idiotcheck)
    val += *_gasneti_linkconfig_idiotcheck();
  return &val;
}

#if defined(GASNET_DEBUG) && (defined(__OPTIMIZE__) || defined(NDEBUG))
  #ifndef GASNET_ALLOW_OPTIMIZED_DEBUG
    #error Tried to compile GASNet client code with optimization enabled but also GASNET_DEBUG (which seriously hurts performance). Reconfigure/rebuild GASNet without --enable-debug
  #endif
#endif

/* ------------------------------------------------------------------------------------ */

#undef _IN_GASNET_H
#endif

/*  $Archive:: /Ti/GASNet/gasnet_help.h                                   $
 *     $Date: 2003/04/05 06:39:38 $
 * $Revision: 1.14 $
 * Description: GASNet Header Helpers (Internal code, not for client use)
 * Copyright 2002, Dan Bonachea <bonachea@cs.berkeley.edu>
 * Terms of use are as specified in license.txt
 */

#ifndef _IN_GASNET_H
  #error This file is not meant to be included directly- clients should include gasnet.h
#endif

#ifndef _GASNET_HELP_H
#define _GASNET_HELP_H

#include <assert.h>

BEGIN_EXTERNC

extern void gasneti_fatalerror(char *msg, ...) GASNET_NORETURN;
extern char *gasneti_getenv(const char *keyname);

#if defined(__GNUC__) || defined(__FUNCTION__)
  #define GASNETI_CURRENT_FUNCTION __FUNCTION__
#elif defined(HAVE_FUNC)
  /* __func__ should also work for ISO C99 compilers */
  #define GASNETI_CURRENT_FUNCTION __func__
#else
  #define GASNETI_CURRENT_FUNCTION ""
#endif
extern char *gasneti_build_loc_str(const char *funcname, const char *filename, int linenum);
#define gasneti_current_loc gasneti_build_loc_str(GASNETI_CURRENT_FUNCTION,__FILE__,__LINE__)

#ifdef NDEBUG
  #define gasneti_boundscheck(node,ptr,nbytes,T) 
#else
  #define gasneti_boundscheck(node,ptr,nbytes,T) do {                                                              \
      gasnet_node_t _node = node;                                                                                  \
      uintptr_t _ptr = (uintptr_t)ptr;                                                                             \
      size_t _nbytes = nbytes;                                                                                     \
      if_pf (_node > gasnet##T##_nodes)                                                                            \
        gasneti_fatalerror("Node index out of range (%i >= %i) at %s",                                             \
                           _node, gasnet##T##_nodes, gasneti_current_loc);                                         \
      if_pf (_ptr < (uintptr_t)gasnet##T##_seginfo[_node].addr ||                                                  \
             (_ptr + _nbytes) > (((uintptr_t)gasnet##T##_seginfo[_node].addr) + gasnet##T##_seginfo[_node].size))  \
        gasneti_fatalerror("Remote address out of range (node=%i ptr=0x%08x nbytes=%i "                            \
                           "segment=(0x%08x...0x%08x)) at %s",                                                     \
                           _node, _ptr, _nbytes, gasnet##T##_seginfo[_node].addr,                                  \
                           ((uint8_t*)gasnet##T##_seginfo[_node].addr) + gasnet##T##_seginfo[_node].size,          \
                           gasneti_current_loc);                                                                   \
    } while(0)
#endif

/* high-performance timer library */
#include <gasnet_timer.h>

/* tracing utilities */
#include <gasnet_trace.h>

/* ------------------------------------------------------------------------------------ */

END_EXTERNC

#endif

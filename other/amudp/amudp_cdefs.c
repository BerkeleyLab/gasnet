/*   $Source: /Users/kamil/work/gasnet-cvs2/gasnet/other/amudp/amudp_cdefs.c,v $
 *     $Date: 2005/03/15 13:54:52 $
 * $Revision: 1.1 $
 * Description: AMUDP definitions that must be compiled in C mode
 * Copyright 2005, Dan Bonachea <bonachea@cs.berkeley.edu>
 */

#include <stddef.h>
#include <amudp.h>

#if AMUDP_DEBUG
  /* use the gasnet debug malloc functions if a debug libgasnet is linked
   * these *must* be tentative definitions for this linker trick to work, 
   * and C++ annoying provides apparently no way to express that
   */
  void *(*gasnett_debug_malloc_fn)(size_t sz, const char *curloc);
  void *(*gasnett_debug_calloc_fn)(size_t N, size_t S, const char *curloc);
  void (*gasnett_debug_free_fn)(void *ptr, const char *curloc);
#endif

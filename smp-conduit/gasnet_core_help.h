/*   $Source: /Users/kamil/work/gasnet-cvs2/gasnet/smp-conduit/gasnet_core_help.h,v $
 *     $Date: 2007/05/06 04:47:25 $
 * $Revision: 1.6.34.2 $
 * Description: GASNet smp conduit core Header Helpers (Internal code, not for client use)
 * Copyright 2002, Dan Bonachea <bonachea@cs.berkeley.edu>
 * Terms of use are as specified in license.txt
 */

#ifndef _IN_GASNET_H
  #error This file is not meant to be included directly- clients should include gasnet.h
#endif

#ifndef _GASNET_CORE_HELP_H
#define _GASNET_CORE_HELP_H

GASNETI_BEGIN_EXTERNC

#include <gasnet_help.h>

#define GASNETC_MAX_ARGS   16
#if GASNET_SYSV
  /* HACK: set max medium to size known to be smaller than
   * GASNETI_SYSVNET_MAX_PAYLOAD (which isn't visible to this file yet) 
   * - TODO: fix sysV max medium to be automatically defined to equal
   *   GASNETI_SYSVNET_MAX_PAYLOAD, and make that a bigger value!
   */
  #define GASNETC_MAX_MEDIUM 3984
#else
  /* limited only by buffering constraints */
  #define GASNETC_MAX_MEDIUM 65536   
#endif
#define GASNETC_MAX_LONG   ((size_t)0x7fffffff) /* unlimited */

GASNETI_END_EXTERNC

#endif

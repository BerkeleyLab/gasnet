/*   $Source: /Users/kamil/work/gasnet-cvs2/gasnet/smp-conduit/gasnet_core_help.h,v $
 *     $Date: 2009/08/27 00:22:45 $
 * $Revision: 1.6.60.4 $
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

#ifdef GASNETI_MAX_MEDIUM_SYSV
  #define GASNETC_MAX_MEDIUM MIN(65536, GASNETI_MAX_MEDIUM_SYSV)
#else
  #define GASNETC_MAX_MEDIUM 65536  /* limited only by buffering constraints */
#endif

#define GASNETC_MAX_LONG   ((size_t)0x7fffffff) /* unlimited */

GASNETI_END_EXTERNC

#endif

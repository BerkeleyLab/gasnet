/*   $Source: /Users/kamil/work/gasnet-cvs2/gasnet/ibv-conduit/gasnet_core_help.h,v $
 *     $Date: 2005/03/22 19:19:30 $
 * $Revision: 1.10 $
 * Description: GASNet vapi conduit core Header Helpers (Internal code, not for client use)
 * Copyright 2002, Dan Bonachea <bonachea@cs.berkeley.edu>
 * Terms of use are as specified in license.txt
 */

#ifndef _IN_GASNET_H
  #error This file is not meant to be included directly- clients should include gasnet.h
#endif

#ifndef _GASNET_CORE_HELP_H
#define _GASNET_CORE_HELP_H

BEGIN_EXTERNC

#include <gasnet_help.h>

#if defined(GASNET_SEGMENT_FAST)
  #define GASNETC_PIN_SEGMENT 1
#else
  #define GASNETC_PIN_SEGMENT 0
#endif

END_EXTERNC

#endif

/*   $Source: /Users/kamil/work/gasnet-cvs2/gasnet/template-conduit/gasnet_core_help.h,v $
 *     $Date: 2004/08/30 06:58:06 $
 * $Revision: 1.3.8.1 $
 * Description: GASNet <conduitname> conduit core Header Helpers (Internal code, not for client use)
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

extern gasnet_node_t gasnetc_mynode;
extern gasnet_node_t gasnetc_nodes;

END_EXTERNC

#endif

/*  $Archive:: /Ti/GASNet/vapi-conduit/gasnet_core_help.h             $
 *     $Date: 2003/10/14 16:34:00 $
 * $Revision: 1.3.6.1 $
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
#include <gasnet_atomicops.h>

extern gasnet_node_t gasnetc_mynode;
extern gasnet_node_t gasnetc_nodes;

END_EXTERNC

#endif

/*   $Source: /Users/kamil/work/gasnet-cvs2/gasnet/portals-conduit/Attic/gasnet_core_internal.h,v $
 *     $Date: 2008/03/17 01:03:42 $
 * $Revision: 1.3.16.1 $
 * Description: GASNet PORTALS conduit header for internal definitions in Core API
 * Copyright 2002, Dan Bonachea <bonachea@cs.berkeley.edu>
 * Terms of use are as specified in license.txt
 */

#ifndef _GASNET_CORE_INTERNAL_H
#define _GASNET_CORE_INTERNAL_H

#include <gasnet_internal.h>

/*  whether or not to use spin-locking for HSL's */
#define GASNETC_HSL_SPINLOCK 1

/* ------------------------------------------------------------------------------------ */

#if GASNET_SEGMENT_FAST || GASNET_SEGMENT_LARGE
  #define GASNETC_FIREHOSE_LOCAL 1
#else
  #define GASNETC_FIREHOSE_LOCAL 0
#endif

#if GASNETC_FIREHOSE_LOCAL /* || GASNETC_FIREHOSE_REMOTE */
  #include <firehose.h>
  extern int gasnetc_use_firehose;
  extern firehose_info_t gasnetc_firehose_info;
#endif

/* ------------------------------------------------------------------------------------ */
#define GASNETC_HANDLER_BASE  1 /* reserve 1-63 for the core API */
#define _hidx_gasnetc_auxseg_reqh             (GASNETC_HANDLER_BASE+0)
#define _hidx_gasnetc_noop_reph               (GASNETC_HANDLER_BASE+1)
/* add new core API handlers here and to the bottom of gasnet_core.c */


#endif

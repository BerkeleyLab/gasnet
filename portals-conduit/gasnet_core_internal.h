/*   $Source: /Users/kamil/work/gasnet-cvs2/gasnet/portals-conduit/Attic/gasnet_core_internal.h,v $
 *     $Date: 2009/08/30 19:47:47 $
 * $Revision: 1.4.10.3 $
 * Description: GASNet PORTALS conduit header for internal definitions in Core API
 * Copyright 2002, Dan Bonachea <bonachea@cs.berkeley.edu>
 * Terms of use are as specified in license.txt
 */

#ifndef _GASNET_CORE_INTERNAL_H
#define _GASNET_CORE_INTERNAL_H

#include <gasnet_internal.h>
#include <gasnet_handler.h>

/*  whether or not to use spin-locking for HSL's */
#define GASNETC_HSL_SPINLOCK 1

/* ------------------------------------------------------------------------------------ */
#define GASNETC_HANDLER_BASE  1 /* reserve 1-63 for the core API */
#define _hidx_gasnetc_auxseg_reqh             (GASNETC_HANDLER_BASE+0)
#define _hidx_gasnetc_noop_reph               (GASNETC_HANDLER_BASE+1)
/* add new core API handlers here and to the bottom of gasnet_core.c */

/* ------------------------------------------------------------------------------------ */
/* handler table (recommended impl) */
#define GASNETC_MAX_NUMHANDLERS   256
extern gasneti_handler_fn_t gasnetc_handler[GASNETC_MAX_NUMHANDLERS];

/* ------------------------------------------------------------------------------------ */
/* Unconditionally evalute second arg.
 * When debugging, also assert that it equals the first argument.
 * NOTE: first arg is only evaluated contitionally.
 */
#if GASNET_DEBUG
  #define gasnetc_assert_value(_val, _expr)	gasneti_assert((_val) == (_expr))
#else
  #define gasnetc_assert_value(_val, _expr)	_expr
#endif


#endif

/*   $Source: /Users/kamil/work/gasnet-cvs2/gasnet/template-conduit/gasnet_core_internal.h,v $
 *     $Date: 2009/09/02 04:28:35 $
 * $Revision: 1.12.80.2 $
 * Description: GASNet <conduitname> conduit header for internal definitions in Core API
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
/* add new core API handlers here and to the bottom of gasnet_core.c */

/* ------------------------------------------------------------------------------------ */
/* handler table (recommended impl) */
#define GASNETC_MAX_NUMHANDLERS   256
extern gasneti_handler_fn_t gasnetc_handler[GASNETC_MAX_NUMHANDLERS];

/* ------------------------------------------------------------------------------------ */
/* AM category (recommended impl if supporting PSHM) */
typedef enum {
  gasnetc_Short=0,
  gasnetc_Medium=1,
  gasnetc_Long=2
} gasnetc_category_t;

#endif

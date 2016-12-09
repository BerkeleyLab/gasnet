/*   $Source: bitbucket.org:berkeleylab/gasnet.git/template-conduit/gasnet_core_internal.h $
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
#define _hidx_gasnetc_auxseg_reqh             (GASNETC_HANDLER_BASE+0)
/* add new core API handlers here and to the bottom of gasnet_core.c */

/* ------------------------------------------------------------------------------------ */
/* handler table (recommended impl) */
extern gasneti_handler_fn_t gasnetc_handler[GASNETC_MAX_NUMHANDLERS];

/* ------------------------------------------------------------------------------------ */
/* AM category (recommended impl if supporting PSHM) */
typedef enum {
  gasnetc_Short=0,
  gasnetc_Medium=1,
  gasnetc_Long=2
} gasnetc_category_t;

/* ------------------------------------------------------------------------------------ */
/* Configure gasnet_handle_internal.h and gasnet_handle.c */
// TODO-EX: prefix needs to move from "extended" to "core"

// (###) Define as needed if iop counters should use something other than weakatomics:
/* #define gasnete_op_atomic_(_id) gasnetc_atomic_##_id */

// (###) Define if conduit performs local-completion detection:
/* #define GASNETE_HAVE_LC */

#endif

/*   $Source: /Users/kamil/work/gasnet-cvs2/gasnet/pami-conduit/gasnet_core_internal.h,v $
 *     $Date: 2012/03/16 06:54:19 $
 * $Revision: 1.1.2.5 $
 * Description: GASNet PAMI conduit header for internal definitions in Core API
 * Copyright 2012, Lawrence Berkeley National Laboratory
 * Terms of use are as specified in license.txt
 */

#ifndef _GASNET_CORE_INTERNAL_H
#define _GASNET_CORE_INTERNAL_H

#include <gasnet_internal.h>
#include <gasnet_handler.h>

#include <pami.h>

#define GASNETC_PAMI_CHECK(rc,msg) \
  if_pf ((rc) != PAMI_SUCCESS) \
    { gasneti_fatalerror("Unexpected error %d on node %i/%i %s",\
                         (rc), gasneti_mynode, gasneti_nodes, (msg)); }

/* ------------------------------------------------------------------------------------ */
/*  whether or not to use spin-locking for HSL's */
#define GASNETC_HSL_SPINLOCK 0

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

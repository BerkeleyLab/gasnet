/*   $Source: /Users/kamil/work/gasnet-cvs2/gasnet/extended-ref/gasnet_extended_refcoll.h,v $
 *     $Date: 2006/11/04 05:58:28 $
 * $Revision: 1.1.10.6 $
 * Description: GASNet Collectives conduit header
 * Copyright 2002, Dan Bonachea <bonachea@cs.berkeley.edu>
 * Terms of use are as specified in license.txt
 */

#ifndef _GASNET_EXTENDED_REFCOLL_H
#define _GASNET_EXTENDED_REFCOLL_H

#include <gasnet_handler.h>
#include <gasnet_coll_trees.h>
#include <gasnet_coll_scratch.h>

/*---------------------------------------------------------------------------------*/
/* ***  Parameters *** */
/*---------------------------------------------------------------------------------*/

/*---------------------------------------------------------------------------------*/
/* ***  Handlers *** */
/*---------------------------------------------------------------------------------*/
/* conduits may override this to relocate the ref-coll handlers */
#ifndef GASNETE_COLL_HANDLER_BASE
#define GASNETE_COLL_HANDLER_BASE 123
#endif

#define _hidx_gasnete_coll_p2p_memcpy_reqh  (GASNETE_COLL_HANDLER_BASE+0)
#define _hidx_gasnete_coll_p2p_short_reqh   (GASNETE_COLL_HANDLER_BASE+1)
#define _hidx_gasnete_coll_p2p_med_reqh	    (GASNETE_COLL_HANDLER_BASE+2)
#define _hidx_gasnete_coll_p2p_long_reqh    (GASNETE_COLL_HANDLER_BASE+3)
#define _hidx_gasnete_coll_p2p_med_tree_reqh (GASNETE_COLL_HANDLER_BASE+4)

/*---------------------------------------------------------------------------------*/

#ifndef GASNETE_COLL_P2P_OVERRIDE

  MEDIUM_HANDLER_DECL(gasnete_coll_p2p_memcpy_reqh,4,5);
  SHORT_HANDLER_NOBITS_DECL(gasnete_coll_p2p_short_reqh, 5);
  MEDIUM_HANDLER_NOBITS_DECL(gasnete_coll_p2p_med_reqh,6);
  LONG_HANDLER_NOBITS_DECL(gasnete_coll_p2p_long_reqh,5);
  MEDIUM_HANDLER_NOBITS_DECL(gasnete_coll_p2p_med_tree_reqh,1);

  #define GASNETE_COLL_P2P_HANDLERS() \
      gasneti_handler_tableentry_with_bits(gasnete_coll_p2p_memcpy_reqh), \
      gasneti_handler_tableentry_no_bits(gasnete_coll_p2p_short_reqh),    \
      gasneti_handler_tableentry_no_bits(gasnete_coll_p2p_med_reqh),      \
      gasneti_handler_tableentry_no_bits(gasnete_coll_p2p_long_reqh),     \
      gasneti_handler_tableentry_no_bits(gasnete_coll_p2p_med_tree_reqh), 

#elif !defined(GASNETE_COLL_P2P_HANDLERS)
  #define GASNETE_COLL_P2P_HANDLERS()
#endif

#define GASNETE_REFCOLL_HANDLERS()                           \
  /* ptr-width independent handlers */                       \
  /*  gasneti_handler_tableentry_no_bits(gasnete__reqh) */   \
                                                             \
  /* ptr-width dependent handlers */                         \
  /*  gasneti_handler_tableentry_with_bits(gasnete__reqh) */ \
                                                             \
  GASNETE_COLL_P2P_HANDLERS() GASNETE_COLL_SCRATCH_HANDLERS()                      

/*---------------------------------------------------------------------------------*/
/* Data for a given tree-based operation */
struct gasnete_coll_tree_data_t_ {
    uint32_t			pipe_seg_size;
    uint32_t			sent_bytes;
    gasnete_coll_local_tree_geom_t	*geom;
};
#define GASNETE_COLL_MIN_SCRATCH_SIZE 8192
#define GASNETE_COLL_MAX_SCRATCH_SIZE 0xffffffff
#ifndef GASNETE_COLL_OPT_SCRATCH_SIZE
/*set defult to 1 MB*/
#define GASNETE_COLL_OPT_SCRATCH_SIZE (1*(1024*1024))
#endif
#endif

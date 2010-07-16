/* $Source: /Users/kamil/work/gasnet-cvs2/gasnet/extended-ref/Attic/gasnet_gpu_amhandlers.h,v $
 * $Date: 2010/07/16 18:35:42 $
 * $Revision: 1.1.4.2 $
 *
 * Description: AM Handlers for GASNet GPU extensions
 * 
 * Yili Zheng
 * LBNL 2010
 */

#ifndef GASNET_GPU_AMHANDLERS_H_
#define GASNET_GPU_AMHANDLERS_H_

/* ------------------------------------------------------------------------------------
 * Handlers:                                                                           
 * ------------------------------------------------------------------------------------ 
 */
#ifndef GASNETE_GPU_HANDLER_BASE
#define GASNETE_GPU_HANDLER_BASE 80
#endif
#define _hidx_gasnete_get_hosttogpu_reqh    (GASNETE_GPU_HANDLER_BASE+0)
#define _hidx_gasnete_get_hosttogpu_reqh2   (GASNETE_GPU_HANDLER_BASE+1)
#define _hidx_gasnete_get_gputogpu_reqh     (GASNETE_GPU_HANDLER_BASE+2)
#define _hidx_gasnete_get_gputogpu_reqh2    (GASNETE_GPU_HANDLER_BASE+3)
#define _hidx_gasnete_get_togpu_reph        (GASNETE_GPU_HANDLER_BASE+4)
#define _hidx_gasnete_get_togpu_reph2       (GASNETE_GPU_HANDLER_BASE+5)
#define _hidx_gasnete_get_gputohost_reqh    (GASNETE_GPU_HANDLER_BASE+6)
#define _hidx_gasnete_gpu_getremotebuf_reqh (GASNETE_GPU_HANDLER_BASE+7)
#define _hidx_gasnete_gpu_getremotebuf_reph (GASNETE_GPU_HANDLER_BASE+8)
#define _hidx_gasnete_put_togpu_reqh        (GASNETE_GPU_HANDLER_BASE+9)
#define _hidx_gasnete_put_togpu_reqh2       (GASNETE_GPU_HANDLER_BASE+10)
#define _hidx_gasnete_put_tohost_reqh       (GASNETE_GPU_HANDLER_BASE+11) 
#define _hidx_gasnete_memset_togpu_reqh     (GASNETE_GPU_HANDLER_BASE+12)
#define _hidx_gasnete_put_markdone_reph     (GASNETE_GPU_HANDLER_BASE+13)
#define _hidx_gasnete_get_markdone_reph     (GASNETE_GPU_HANDLER_BASE+14)

SHORT_HANDLER_DECL(gasnete_get_hosttogpu_reqh,4,8);
SHORT_HANDLER_DECL(gasnete_get_hosttogpu_reqh2,5,10);
SHORT_HANDLER_DECL(gasnete_get_gputogpu_reqh,4,8);
SHORT_HANDLER_DECL(gasnete_get_gputogpu_reqh2,5,10);
MEDIUM_HANDLER_DECL(gasnete_get_togpu_reph,2,4);
LONG_HANDLER_DECL(gasnete_get_togpu_reph2,2,4);
SHORT_HANDLER_DECL(gasnete_get_gputohost_reqh,4,8);
SHORT_HANDLER_DECL(gasnete_gpu_getremotebuf_reqh,3,6);
SHORT_HANDLER_DECL(gasnete_gpu_getremotebuf_reph,3,6);  
MEDIUM_HANDLER_DECL(gasnete_put_togpu_reqh,2,4);
LONG_HANDLER_DECL(gasnete_put_togpu_reqh2,2,4);
LONG_HANDLER_DECL(gasnete_put_tohost_reqh, 1, 2); 
SHORT_HANDLER_DECL(gasnete_memset_togpu_reqh,4,7);
SHORT_HANDLER_DECL(gasnete_put_markdone_reph,1,2);
LONG_HANDLER_DECL(gasnete_get_markdone_reph,1,2);

#define GASNETE_GPU_HANDLERS()                                          \
  /* ptr-width independent handlers */                                  \
  /* N/A */                                                             \
  /* ptr-width dependent handlers */                                    \
  gasneti_handler_tableentry_with_bits(gasnete_get_hosttogpu_reqh),     \
    gasneti_handler_tableentry_with_bits(gasnete_get_hosttogpu_reqh2),  \
    gasneti_handler_tableentry_with_bits(gasnete_get_gputogpu_reqh),    \
    gasneti_handler_tableentry_with_bits(gasnete_get_gputogpu_reqh2),   \
    gasneti_handler_tableentry_with_bits(gasnete_get_togpu_reph),       \
    gasneti_handler_tableentry_with_bits(gasnete_get_togpu_reph2),      \
    gasneti_handler_tableentry_with_bits(gasnete_get_gputohost_reqh),   \
    gasneti_handler_tableentry_with_bits(gasnete_gpu_getremotebuf_reqh), \
    gasneti_handler_tableentry_with_bits(gasnete_gpu_getremotebuf_reph), \
    gasneti_handler_tableentry_with_bits(gasnete_put_togpu_reqh),       \
    gasneti_handler_tableentry_with_bits(gasnete_put_togpu_reqh2),      \
    gasneti_handler_tableentry_with_bits(gasnete_put_tohost_reqh),      \
    gasneti_handler_tableentry_with_bits(gasnete_memset_togpu_reqh),    \
    gasneti_handler_tableentry_with_bits(gasnete_put_markdone_reph),    \
    gasneti_handler_tableentry_with_bits(gasnete_get_markdone_reph),  

#endif /* GASNET_GPU_AMHANDLERS_H_ */

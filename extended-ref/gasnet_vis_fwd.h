/*   $Source: /Users/kamil/work/gasnet-cvs2/gasnet/extended-ref/gasnet_vis_fwd.h,v $
 *     $Date: 2006/05/03 15:03:09 $
 * $Revision: 1.1.14.2 $
 * Description: GASNet Non-contiguous Access Header (forward decls)
 * Copyright 2002, Dan Bonachea <bonachea@cs.berkeley.edu>
 * Terms of use are as specified in license.txt
 */

#ifndef _IN_GASNET_H
  #error This file is not meant to be included directly- clients should include gasnet.h
#endif

#ifndef _GASNET_VIS_FWD_H
#define _GASNET_VIS_FWD_H

extern void gasneti_vis_progressfn();
#define GASNETI_VIS_PROGRESSFNS(FN) \
    FN(gasneti_pf_vis, COUNTED, gasneti_vis_progressfn) 

/* conduits may replace the following types, 
   but they should at least include all the following fields */
#ifndef GASNETI_MEMVECLIST_STATS_T
  typedef struct {
    size_t minsz;
    size_t maxsz;
    uintptr_t totalsz;
    void *minaddr;
    void *maxaddr;
  } gasneti_memveclist_stats_t;
#endif

#ifndef GASNETI_ADDRLIST_STATS_T
  typedef struct {
    void *minaddr;
    void *maxaddr;
  } gasneti_addrlist_stats_t;
#endif

/* stats needed by the VIS reference implementation */
#ifndef GASNETI_VIS_STATS
  #define GASNETI_VIS_STATS(CNT,VAL,TIME)    \
                                             \
        CNT(C, PUTV_GATHER, cnt)             \
        CNT(C, GETV_SCATTER, cnt)            \
        CNT(C, PUTV_AMPIPELINE, cnt)         \
        CNT(C, GETV_AMPIPELINE, cnt)         \
        CNT(C, PUTV_REF_INDIV, cnt)          \
        CNT(C, GETV_REF_INDIV, cnt)          \
                                             \
        CNT(C, PUTI_GATHER, cnt)             \
        CNT(C, GETI_SCATTER, cnt)            \
        CNT(C, PUTI_AMPIPELINE, cnt)         \
        CNT(C, GETI_AMPIPELINE, cnt)         \
        CNT(C, PUTI_REF_INDIV, cnt)          \
        CNT(C, GETI_REF_INDIV, cnt)          \
        CNT(C, PUTI_REF_VECTOR, cnt)         \
        CNT(C, GETI_REF_VECTOR, cnt)         \
                                             \
        CNT(C, PUTS_GATHER, cnt)             \
        CNT(C, GETS_SCATTER, cnt)            \
        CNT(C, PUTS_AMPIPELINE, cnt)         \
        CNT(C, GETS_AMPIPELINE, cnt)         \
        CNT(C, PUTS_REF_INDIV, cnt)          \
        CNT(C, GETS_REF_INDIV, cnt)          \
        CNT(C, PUTS_REF_VECTOR, cnt)         \
        CNT(C, GETS_REF_VECTOR, cnt)         \
        CNT(C, PUTS_REF_INDEXED, cnt)        \
        CNT(C, GETS_REF_INDEXED, cnt)        \

#endif

/* conduits may override this to relocate the ref-vis handlers */
#ifndef GASNETE_VIS_HANDLER_BASE
#define GASNETE_VIS_HANDLER_BASE 110
#endif

#define _hidx_gasnete_putv_AMPipeline_reqh  (GASNETE_VIS_HANDLER_BASE+0)
#define _hidx_gasnete_putv_AMPipeline_reph  (GASNETE_VIS_HANDLER_BASE+1)

#endif

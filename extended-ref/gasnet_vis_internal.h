/*  $Archive:: /Ti/GASNet/extended-ref/gasnet_extended_refbarrier.c                  $
 *     $Date: 2004/03/03 13:47:04 $
 * $Revision: 1.1 $
 * Description: Reference implemetation of GASNet Vector, Indexed & Strided
 * Copyright 2002, Dan Bonachea <bonachea@cs.berkeley.edu>
 * Terms of use are as specified in license.txt
 */

#ifndef GASNETI_GASNET_EXTENDED_VIS_C
  #error This file not meant to be compiled directly - included by gasnet_extended.c
#endif

/*---------------------------------------------------------------------------------*/
/* ***  Parameters *** */
/*---------------------------------------------------------------------------------*/
/* following is non-zero iff it's safe to destroy the metadata input arrays
   immediately after initiating each function
*/
#ifndef GASNETE_PUTV_ALLOWS_VOLATILE_METADATA
#define GASNETE_PUTV_ALLOWS_VOLATILE_METADATA 1
#endif
#ifndef GASNETE_GETV_ALLOWS_VOLATILE_METADATA
#define GASNETE_GETV_ALLOWS_VOLATILE_METADATA 1
#endif
#ifndef GASNETE_PUTI_ALLOWS_VOLATILE_METADATA
#define GASNETE_PUTI_ALLOWS_VOLATILE_METADATA 1
#endif
#ifndef GASNETE_GETI_ALLOWS_VOLATILE_METADATA
#define GASNETE_GETI_ALLOWS_VOLATILE_METADATA 1
#endif
#ifndef GASNETE_PUTS_ALLOWS_VOLATILE_METADATA
#define GASNETE_PUTS_ALLOWS_VOLATILE_METADATA 1
#endif
#ifndef GASNETE_GETS_ALLOWS_VOLATILE_METADATA
#define GASNETE_GETS_ALLOWS_VOLATILE_METADATA 1
#endif

/*---------------------------------------------------------------------------------*/
/* ***  Helpers *** */
/*---------------------------------------------------------------------------------*/
/* helper for vis functions implemented atop other GASNet operations
   start a recursive NBI access region, if appropriate */
#define GASNETE_START_NBIREGION(synctype, islocal) do {    \
  if (synctype != gasnete_synctype_nbi && !islocal)        \
    gasnete_begin_nbi_accessregion(1 GASNETE_THREAD_PASS); \
  } while(0)
/* finish a region started with GASNETE_START_NBIREGION,
   block if required, and return the appropriate handle */
#define GASNETE_END_NBIREGION_AND_RETURN(synctype, islocal) do {                      \
    if (islocal) return GASNET_INVALID_HANDLE;                                        \
    switch (synctype) {                                                               \
      case gasnete_synctype_nb:                                                       \
        return gasnete_end_nbi_accessregion(GASNETE_THREAD_PASS_ALONE);               \
      case gasnete_synctype_b:                                                        \
        gasnete_wait_syncnb(gasnete_end_nbi_accessregion(GASNETE_THREAD_PASS_ALONE)); \
        return GASNET_INVALID_HANDLE;                                                 \
      case gasnete_synctype_nbi:                                                      \
        return GASNET_INVALID_HANDLE;                                                 \
      default: gasneti_fatalerror("bad synctype");                                    \
    }                                                                                 \
  } while(0)

#define GASNETE_PUT_INDIV(islocal, dstnode, dstaddr, srcaddr, nbytes) do {      \
    gasneti_assert(nbytes > 0);                                                 \
    gasnete_boundscheck(dstnode, dstaddr, nbytes);                              \
    gasneti_assert(islocal == (dstnode == gasnete_mynode));                     \
    if (islocal) GASNETE_FAST_UNALIGNED_MEMCPY((dstaddr), (srcaddr), (nbytes)); \
    else gasnete_put_nbi_bulk((dstnode), (dstaddr), (srcaddr), (nbytes)         \
                                GASNETE_THREAD_PASS);                           \
  } while (0)

#define GASNETE_GET_INDIV(islocal, dstaddr, srcnode, srcaddr, nbytes) do {      \
    gasneti_assert(nbytes > 0);                                                 \
    gasnete_boundscheck(srcnode, srcaddr, nbytes);                              \
    gasneti_assert(islocal == (srcnode == gasnete_mynode));                     \
    if (islocal) GASNETE_FAST_UNALIGNED_MEMCPY((dstaddr), (srcaddr), (nbytes)); \
    else gasnete_get_nbi_bulk((dstaddr), (srcnode), (srcaddr), (nbytes)         \
                                GASNETE_THREAD_PASS);                           \
  } while (0)

/*---------------------------------------------------------------------------------*/
/* ***  Vector *** */
/*---------------------------------------------------------------------------------*/

/* reference versions that use individual put/gets */
gasnet_handle_t gasnete_putv_ref_indiv(gasnete_synctype_t synctype,
                                   gasnet_node_t dstnode,
                                   size_t dstcount, gasnet_memvec_t const dstlist[], 
                                   size_t srccount, gasnet_memvec_t const srclist[] GASNETE_THREAD_FARG) {
  const int islocal = (dstnode == gasnete_mynode);
  GASNETI_TRACE_EVENT(C, PUTV_REF_INDIV);
  gasneti_assert(srccount > 0 && dstcount > 0);
  GASNETE_START_NBIREGION(synctype, islocal);

  if (dstcount == 1) { /* dst is contiguous buffer */
    uintptr_t pdst = (uintptr_t)(dstlist[0].addr);
    int i;
    for (i = 0; i < srccount; i++) {
      const size_t srclen = srclist[i].len;
      if_pt (srclen > 0)
        GASNETE_PUT_INDIV(islocal, dstnode, (void *)pdst, srclist[i].addr, srclen);
      pdst += srclen;
    }
    gasneti_assert(pdst == (uintptr_t)(dstlist[0].addr)+dstlist[0].len);
  } else if (srccount == 1) { /* src is contiguous buffer */
    uintptr_t psrc = (uintptr_t)(srclist[0].addr);
    int i;
    for (i = 0; i < dstcount; i++) {
      const size_t dstlen = dstlist[i].len;
      if_pt (dstlen > 0)
        GASNETE_PUT_INDIV(islocal, dstnode, dstlist[i].addr, (void *)psrc, dstlen);
      psrc += dstlen;
    }
    gasneti_assert(psrc == (uintptr_t)(srclist[0].addr)+srclist[0].len);
  } else { /* general case */
    size_t srcidx = 0;
    size_t dstidx = 0;
    size_t srcoffset = 0;
    size_t dstoffset = 0;
    
    while (srcidx < srccount && srclist[srcidx].len == 0) srcidx++;
    while (dstidx < dstcount && dstlist[dstidx].len == 0) dstidx++;
    while (srcidx < srccount) {
      const size_t srcremain = srclist[srcidx].len - srcoffset;
      const size_t dstremain = dstlist[dstidx].len - dstoffset;
      gasneti_assert(dstidx < dstcount);
      gasneti_assert(srcremain > 0 && dstremain > 0);
      if (srcremain < dstremain) {
        GASNETE_PUT_INDIV(islocal, dstnode, 
          (void *)(((uintptr_t)dstlist[dstidx].addr)+dstoffset), 
          (void *)(((uintptr_t)srclist[srcidx].addr)+srcoffset), 
          srcremain);
        srcidx++;
        while (srcidx < srccount && srclist[srcidx].len == 0) srcidx++;
        srcoffset = 0;
        dstoffset += srcremain;
      } else {
        GASNETE_PUT_INDIV(islocal, dstnode, 
          (void *)(((uintptr_t)dstlist[dstidx].addr)+dstoffset), 
          (void *)(((uintptr_t)srclist[srcidx].addr)+srcoffset), 
          dstremain);
        dstidx++;
        while (dstidx < dstcount && dstlist[dstidx].len == 0) dstidx++;
        dstoffset = 0;
        if (srcremain == dstremain) {
          srcidx++;
          while (srcidx < srccount && srclist[srcidx].len == 0) srcidx++;
          srcoffset = 0;
        } else srcoffset += dstremain;
      }
    } 
    gasneti_assert(srcidx == srccount && dstidx == dstcount && srcoffset == 0 && dstoffset == 0);
  }
  GASNETE_END_NBIREGION_AND_RETURN(synctype, islocal);
}

gasnet_handle_t gasnete_getv_ref_indiv(gasnete_synctype_t synctype,
                                   size_t dstcount, gasnet_memvec_t const dstlist[], 
                                   gasnet_node_t srcnode,
                                   size_t srccount, gasnet_memvec_t const srclist[] GASNETE_THREAD_FARG) {
  const int islocal = (srcnode == gasnete_mynode);
  GASNETI_TRACE_EVENT(C, GETV_REF_INDIV);
  gasneti_assert(srccount > 0 && dstcount > 0);
  GASNETE_START_NBIREGION(synctype, islocal);

  if (dstcount == 1) { /* dst is contiguous buffer */
    uintptr_t pdst = (uintptr_t)(dstlist[0].addr);
    int i;
    for (i = 0; i < srccount; i++) {
      const size_t srclen = srclist[i].len;
      if_pt (srclen > 0)
        GASNETE_GET_INDIV(islocal, (void *)pdst, srcnode, srclist[i].addr, srclen);
      pdst += srclen;
    }
    gasneti_assert(pdst == (uintptr_t)(dstlist[0].addr)+dstlist[0].len);
  } else if (srccount == 1) { /* src is contiguous buffer */
    uintptr_t psrc = (uintptr_t)(srclist[0].addr);
    int i;
    for (i = 0; i < dstcount; i++) {
      const size_t dstlen = dstlist[i].len;
      if_pt (dstlen > 0)
        GASNETE_GET_INDIV(islocal, dstlist[i].addr, srcnode, (void *)psrc, dstlen);
      psrc += dstlen;
    }
    gasneti_assert(psrc == (uintptr_t)(srclist[0].addr)+srclist[0].len);
  } else { /* general case */
    size_t srcidx = 0;
    size_t dstidx = 0;
    size_t srcoffset = 0;
    size_t dstoffset = 0;
    
    while (srcidx < srccount && srclist[srcidx].len == 0) srcidx++;
    while (dstidx < dstcount && dstlist[dstidx].len == 0) dstidx++;
    while (srcidx < srccount) {
      const size_t srcremain = srclist[srcidx].len - srcoffset;
      const size_t dstremain = dstlist[dstidx].len - dstoffset;
      gasneti_assert(dstidx < dstcount);
      gasneti_assert(srcremain > 0 && dstremain > 0);
      if (srcremain < dstremain) {
        GASNETE_GET_INDIV(islocal, 
          (void *)(((uintptr_t)dstlist[dstidx].addr)+dstoffset), 
          srcnode, 
          (void *)(((uintptr_t)srclist[srcidx].addr)+srcoffset), 
          srcremain);
        srcidx++;
        while (srcidx < srccount && srclist[srcidx].len == 0) srcidx++;
        srcoffset = 0;
        dstoffset += srcremain;
      } else {
        GASNETE_GET_INDIV(islocal, 
          (void *)(((uintptr_t)dstlist[dstidx].addr)+dstoffset), 
          srcnode, 
          (void *)(((uintptr_t)srclist[srcidx].addr)+srcoffset), 
          dstremain);
        dstidx++;
        while (dstidx < dstcount && dstlist[dstidx].len == 0) dstidx++;
        dstoffset = 0;
        if (srcremain == dstremain) {
          srcidx++;
          while (srcidx < srccount && srclist[srcidx].len == 0) srcidx++;
          srcoffset = 0;
        } else srcoffset += dstremain;
      }
    } 
    gasneti_assert(srcidx == srccount && dstidx == dstcount && srcoffset == 0 && dstoffset == 0);
  }

  GASNETE_END_NBIREGION_AND_RETURN(synctype, islocal);
}
/*---------------------------------------------------------------------------------*/

#ifndef GASNETE_PUTV_OVERRIDE
extern gasnet_handle_t gasnete_putv(gasnete_synctype_t synctype,
                                   gasnet_node_t dstnode,
                                   size_t dstcount, gasnet_memvec_t const dstlist[], 
                                   size_t srccount, gasnet_memvec_t const srclist[] GASNETE_THREAD_FARG) {
  /* catch silly degenerate cases */
  if_pf (dstcount == 0 || srccount == 0) /* empty (may miss some cases) */
    return GASNET_INVALID_HANDLE; 
  if_pf (dstcount + srccount <= 2 ||  /* fully contiguous */
         dstnode == gasnete_mynode) { /* purely local */ 
    return gasnete_putv_ref_indiv(synctype,dstnode,dstcount,dstlist,srccount,srclist GASNETE_THREAD_PASS);
  }

  /* select algorithm */
  #ifdef GASNETE_PUTV_SELECTOR
    GASNETE_PUTV_SELECTOR(synctype,dstnode,dstcount,dstlist,srccount,srclist);
  #else
    return gasnete_putv_ref_indiv(synctype,dstnode,dstcount,dstlist,srccount,srclist GASNETE_THREAD_PASS);
  #endif
  gasneti_fatalerror("failure in GASNETE_PUTV_SELECTOR - should never reach here");
}
#endif

#ifndef GASNETE_GETV_OVERRIDE
extern gasnet_handle_t gasnete_getv(gasnete_synctype_t synctype,
                                   size_t dstcount, gasnet_memvec_t const dstlist[], 
                                   gasnet_node_t srcnode,
                                   size_t srccount, gasnet_memvec_t const srclist[] GASNETE_THREAD_FARG) {
  /* catch silly degenerate cases */
  if_pf (dstcount == 0 || srccount == 0) /* empty (may miss some cases) */
    return GASNET_INVALID_HANDLE; 
  if_pf (dstcount + srccount <= 2 ||  /* fully contiguous */
         srcnode == gasnete_mynode) { /* purely local */ 
    if (dstcount == 0 || srccount == 0) return GASNET_INVALID_HANDLE;
    return gasnete_getv_ref_indiv(synctype,dstcount,dstlist,srcnode,srccount,srclist GASNETE_THREAD_PASS);
  }

  /* select algorithm */
  #ifdef GASNETE_GETV_SELECTOR
    GASNETE_GETV_SELECTOR(synctype,dstcount,dstlist,srcnode,srccount,srclist);
  #else
    return gasnete_getv_ref_indiv(synctype,dstcount,dstlist,srcnode,srccount,srclist GASNETE_THREAD_PASS);
  #endif
  gasneti_fatalerror("failure in GASNETE_GETV_SELECTOR - should never reach here");
}
#endif

/*---------------------------------------------------------------------------------*/
/* ***  Indexed *** */
/*---------------------------------------------------------------------------------*/

/* reference versions that use individual put/gets */
gasnet_handle_t gasnete_puti_ref_indiv(gasnete_synctype_t synctype,
                                   gasnet_node_t dstnode, 
                                   size_t dstcount, void * const dstlist[], size_t dstlen,
                                   size_t srccount, void * const srclist[], size_t srclen GASNETE_THREAD_FARG) {
  const int islocal = (dstnode == gasnete_mynode);
  GASNETI_TRACE_EVENT(C, PUTI_REF_INDIV);
  gasneti_assert(srccount > 0 && dstcount > 0 && dstcount*dstlen == srccount*srclen);
  gasneti_assert(srclen > 0 && dstlen > 0);
  GASNETE_START_NBIREGION(synctype, islocal);

  if (dstlen == srclen) { /* matched sizes (fast case) */
    int i;
    gasneti_assert(dstcount == srccount);
    for (i = 0; i < dstcount; i++) {
      GASNETE_PUT_INDIV(islocal, dstnode, dstlist[i], srclist[i], dstlen);
    }
  } else if (dstcount == 1) { /* dst is contiguous buffer */
    uintptr_t pdst = (uintptr_t)(dstlist[0]);
    int i;
    for (i = 0; i < srccount; i++) {
      GASNETE_PUT_INDIV(islocal, dstnode, (void *)pdst, srclist[i], srclen);
      pdst += srclen;
    }
    gasneti_assert(pdst == (uintptr_t)(dstlist[0])+dstlen);
  } else if (srccount == 1) { /* src is contiguous buffer */
    uintptr_t psrc = (uintptr_t)(srclist[0]);
    int i;
    for (i = 0; i < dstcount; i++) {
      GASNETE_PUT_INDIV(islocal, dstnode, dstlist[i], (void *)psrc, dstlen);
      psrc += dstlen;
    }
    gasneti_assert(psrc == (uintptr_t)(srclist[0])+srclen);
  } else { /* mismatched sizes (general case) */
    size_t srcidx = 0;
    size_t dstidx = 0;
    size_t srcoffset = 0;
    size_t dstoffset = 0;
    while (srcidx < srccount) {
      const size_t srcremain = srclen - srcoffset;
      const size_t dstremain = dstlen - dstoffset;
      gasneti_assert(dstidx < dstcount);
      gasneti_assert(srcremain > 0 && dstremain > 0);
      if (srcremain < dstremain) {
        GASNETE_PUT_INDIV(islocal, dstnode, 
          (void *)(((uintptr_t)dstlist[dstidx])+dstoffset), 
          (void *)(((uintptr_t)srclist[srcidx])+srcoffset), 
          srcremain);
        srcidx++;
        srcoffset = 0;
        dstoffset += srcremain;
      } else {
        GASNETE_PUT_INDIV(islocal, dstnode, 
          (void *)(((uintptr_t)dstlist[dstidx])+dstoffset), 
          (void *)(((uintptr_t)srclist[srcidx])+srcoffset), 
          dstremain);
        dstidx++;
        dstoffset = 0;
        if (srcremain == dstremain) {
          srcidx++;
          srcoffset = 0;
        } else srcoffset += dstremain;
      }
    } 
    gasneti_assert(srcidx == srccount && dstidx == dstcount && srcoffset == 0 && dstoffset == 0);
  }

  GASNETE_END_NBIREGION_AND_RETURN(synctype, islocal);
}

gasnet_handle_t gasnete_geti_ref_indiv(gasnete_synctype_t synctype,
                                   size_t dstcount, void * const dstlist[], size_t dstlen,
                                   gasnet_node_t srcnode,
                                   size_t srccount, void * const srclist[], size_t srclen GASNETE_THREAD_FARG) {
  const int islocal = (srcnode == gasnete_mynode);
  GASNETI_TRACE_EVENT(C, GETI_REF_INDIV);
  gasneti_assert(srccount > 0 && dstcount > 0 && dstcount*dstlen == srccount*srclen);
  gasneti_assert(srclen > 0 && dstlen > 0);
  GASNETE_START_NBIREGION(synctype, islocal);

  if (dstlen == srclen) { /* matched sizes (fast case) */
    int i;
    gasneti_assert(dstcount == srccount);
    for (i = 0; i < dstcount; i++) {
      GASNETE_GET_INDIV(islocal, dstlist[i], srcnode, srclist[i], dstlen);
    }
  } else if (dstcount == 1) { /* dst is contiguous buffer */
    uintptr_t pdst = (uintptr_t)(dstlist[0]);
    int i;
    for (i = 0; i < srccount; i++) {
      GASNETE_GET_INDIV(islocal, (void *)pdst, srcnode, srclist[i], srclen);
      pdst += srclen;
    }
    gasneti_assert(pdst == (uintptr_t)(dstlist[0])+dstlen);
  } else if (srccount == 1) { /* src is contiguous buffer */
    uintptr_t psrc = (uintptr_t)(srclist[0]);
    int i;
    for (i = 0; i < dstcount; i++) {
      GASNETE_GET_INDIV(islocal, dstlist[i], srcnode, (void *)psrc, dstlen);
      psrc += dstlen;
    }
    gasneti_assert(psrc == (uintptr_t)(srclist[0])+srclen);
  } else { /* mismatched sizes (general case) */
    size_t srcidx = 0;
    size_t dstidx = 0;
    size_t srcoffset = 0;
    size_t dstoffset = 0;
    while (srcidx < srccount) {
      const size_t srcremain = srclen - srcoffset;
      const size_t dstremain = dstlen - dstoffset;
      gasneti_assert(dstidx < dstcount);
      gasneti_assert(srcremain > 0 && dstremain > 0);
      if (srcremain < dstremain) {
        GASNETE_GET_INDIV(islocal, 
          (void *)(((uintptr_t)dstlist[dstidx])+dstoffset), 
          srcnode, 
          (void *)(((uintptr_t)srclist[srcidx])+srcoffset), 
          srcremain);
        srcidx++;
        srcoffset = 0;
        dstoffset += srcremain;
      } else {
        GASNETE_GET_INDIV(islocal,  
          (void *)(((uintptr_t)dstlist[dstidx])+dstoffset), 
          srcnode, 
          (void *)(((uintptr_t)srclist[srcidx])+srcoffset), 
          dstremain);
        dstidx++;
        dstoffset = 0;
        if (srcremain == dstremain) {
          srcidx++;
          srcoffset = 0;
        } else srcoffset += dstremain;
      }
    } 
    gasneti_assert(srcidx == srccount && dstidx == dstcount && srcoffset == 0 && dstoffset == 0);
  }

  GASNETE_END_NBIREGION_AND_RETURN(synctype, islocal);
}

/*---------------------------------------------------------------------------------*/
/* reference versions that use vector interface */
gasnet_handle_t gasnete_puti_ref_vector(gasnete_synctype_t synctype,
                                   gasnet_node_t dstnode, 
                                   size_t dstcount, void * const dstlist[], size_t dstlen,
                                   size_t srccount, void * const srclist[], size_t srclen GASNETE_THREAD_FARG) {
  gasnet_memvec_t *newdstlist = gasneti_malloc(sizeof(gasnet_memvec_t)*dstcount);
  gasnet_memvec_t *newsrclist = gasneti_malloc(sizeof(gasnet_memvec_t)*srccount);
  gasnet_handle_t retval;
  size_t i;
  GASNETI_TRACE_EVENT(C, PUTI_REF_VECTOR);
  gasneti_assert(GASNETE_PUTV_ALLOWS_VOLATILE_METADATA);
  for (i=0; i < dstcount; i++) {
    newdstlist[i].addr = dstlist[i];
    newdstlist[i].len = dstlen;
  }
  for (i=0; i < srccount; i++) {
    newsrclist[i].addr = srclist[i];
    newsrclist[i].len = srclen;
  }
  retval = gasnete_putv(synctype,dstnode,dstcount,newdstlist,srccount,newsrclist GASNETE_THREAD_PASS);
  gasneti_free(newdstlist);
  gasneti_free(newsrclist);
  return retval;
}

gasnet_handle_t gasnete_geti_ref_vector(gasnete_synctype_t synctype,
                                   size_t dstcount, void * const dstlist[], size_t dstlen,
                                   gasnet_node_t srcnode,
                                   size_t srccount, void * const srclist[], size_t srclen GASNETE_THREAD_FARG) {
  gasnet_memvec_t *newdstlist = gasneti_malloc(sizeof(gasnet_memvec_t)*dstcount);
  gasnet_memvec_t *newsrclist = gasneti_malloc(sizeof(gasnet_memvec_t)*srccount);
  gasnet_handle_t retval;
  size_t i;
  GASNETI_TRACE_EVENT(C, GETI_REF_VECTOR);
  gasneti_assert(GASNETE_GETV_ALLOWS_VOLATILE_METADATA);
  for (i=0; i < dstcount; i++) {
    newdstlist[i].addr = dstlist[i];
    newdstlist[i].len = dstlen;
  }
  for (i=0; i < srccount; i++) {
    newsrclist[i].addr = srclist[i];
    newsrclist[i].len = srclen;
  }
  retval = gasnete_getv(synctype,dstcount,newdstlist,srcnode,srccount,newsrclist GASNETE_THREAD_PASS);
  gasneti_free(newdstlist);
  gasneti_free(newsrclist);
  return retval;
}

/*---------------------------------------------------------------------------------*/
#ifndef GASNETE_PUTI_OVERRIDE
extern gasnet_handle_t gasnete_puti(gasnete_synctype_t synctype,
                                   gasnet_node_t dstnode, 
                                   size_t dstcount, void * const dstlist[], size_t dstlen,
                                   size_t srccount, void * const srclist[], size_t srclen GASNETE_THREAD_FARG) {
  /* catch silly degenerate cases */
  if_pf (dstcount + srccount <= 2 ||  /* empty or fully contiguous */
         dstnode == gasnete_mynode) { /* purely local */ 
    if (dstcount == 0) return GASNET_INVALID_HANDLE;
    else return gasnete_puti_ref_indiv(synctype,dstnode,dstcount,dstlist,dstlen,srccount,srclist,srclen GASNETE_THREAD_PASS);
  }

  /* select algorithm */
  #ifdef GASNETE_PUTI_SELECTOR
    GASNETE_PUTI_SELECTOR(synctype,dstnode,dstcount,dstlist,dstlen,srccount,srclist,srclen);
  #else
    switch (rand() % 2) {
      case 0:
        return gasnete_puti_ref_indiv(synctype,dstnode,dstcount,dstlist,dstlen,srccount,srclist,srclen GASNETE_THREAD_PASS);
      case 1:
        return gasnete_puti_ref_vector(synctype,dstnode,dstcount,dstlist,dstlen,srccount,srclist,srclen GASNETE_THREAD_PASS);
    }
  #endif
  gasneti_fatalerror("failure in GASNETE_PUTI_SELECTOR - should never reach here");
}
#endif

#ifndef GASNETE_GETI_OVERRIDE
extern gasnet_handle_t gasnete_geti(gasnete_synctype_t synctype,
                                   size_t dstcount, void * const dstlist[], size_t dstlen,
                                   gasnet_node_t srcnode,
                                   size_t srccount, void * const srclist[], size_t srclen GASNETE_THREAD_FARG) {
  /* catch silly degenerate cases */
  if_pf (dstcount + srccount <= 2 ||  /* empty or fully contiguous */
         srcnode == gasnete_mynode) { /* purely local */ 
    if (dstcount == 0) return GASNET_INVALID_HANDLE;
    else return gasnete_geti_ref_indiv(synctype,dstcount,dstlist,dstlen,srcnode,srccount,srclist,srclen GASNETE_THREAD_PASS);
  }

  /* select algorithm */
  #ifdef GASNETE_GETI_SELECTOR
    GASNETE_GETI_SELECTOR(synctype,dstcount,dstlist,dstlen,srcnode,srccount,srclist,srclen);
  #else
    switch (rand() % 2) {
      case 0:
        return gasnete_geti_ref_indiv(synctype,dstcount,dstlist,dstlen,srcnode,srccount,srclist,srclen GASNETE_THREAD_PASS);
      case 1:
        return gasnete_geti_ref_vector(synctype,dstcount,dstlist,dstlen,srcnode,srccount,srclist,srclen GASNETE_THREAD_PASS);
    }
  #endif
  gasneti_fatalerror("failure in GASNETE_GETI_SELECTOR - should never reach here");
}
#endif
/*---------------------------------------------------------------------------------*/
/* ***  Strided *** */
/*---------------------------------------------------------------------------------*/
/* helper macros */
/* The macros below expand to code like the following: 
    size_t _i0;
    size_t const _count0 = count[contiglevel+1];
    size_t const _srcbump0 = srcstrides[contiglevel];
    size_t const _dstbump0 = dststrides[contiglevel];

    size_t _i1;
    size_t const _count1 = count[contiglevel+1+1];
    size_t const _srcbump1 = srcstrides[contiglevel+1] - _count0*srcstrides[contiglevel+1-1];
    size_t const _dstbump1 = dststrides[contiglevel+1] - _count0*dststrides[contiglevel+1-1];

    size_t _i2;
    size_t const _count2 = count[contiglevel+2+1];
    size_t const _srcbump2 = srcstrides[contiglevel+2] - _count1*srcstrides[contiglevel+2-1];
    size_t const _dstbump2 = dststrides[contiglevel+2] - _count1*dststrides[contiglevel+2-1];

    for (_i2 = _count2; _i2; _i2--) {

    for (_i1 = _count1; _i1; _i1--) {

    for (_i0 = _count0; _i0; _i0--) {
      GASNETE_STRIDED_HELPER_LOOPBODY(psrc,pdst);
      psrc += _srcbump0;
      pdst += _dstbump0;
    }

      psrc += _srcbump1;
      pdst += _dstbump1;
    }

      psrc += _srcbump2;
      pdst += _dstbump2;
    }
    */
/* This would be cleaner if we could use recursive macro expansion, but it seems at least gcc disallows
   this - if a macro invocation X(...) is found while expanding a different invocation of X (even with 
   different arguments), the nested invocation is left unexpanded 
*/
#define GASNETE_STRIDED_HELPER_SETUP0                 \
    size_t _i0;                                       \
    size_t const _count0 = count[contiglevel+1];      \
    size_t const _srcbump0 = srcstrides[contiglevel]; \
    size_t const _dstbump0 = dststrides[contiglevel];

#define GASNETE_STRIDED_HELPER_SETUPINT(curr, lower)                                                           \
    size_t _i##curr;                                                                                           \
    size_t const _count##curr = count[contiglevel+curr+1];                                                     \
    size_t const _srcbump##curr = srcstrides[contiglevel+curr] - _count##lower*srcstrides[contiglevel+curr-1]; \
    size_t const _dstbump##curr = dststrides[contiglevel+curr] - _count##lower*dststrides[contiglevel+curr-1];

#define GASNETE_STRIDED_HELPER_SETUP1  GASNETE_STRIDED_HELPER_SETUP0 GASNETE_STRIDED_HELPER_SETUPINT(1,0)
#define GASNETE_STRIDED_HELPER_SETUP2  GASNETE_STRIDED_HELPER_SETUP1 GASNETE_STRIDED_HELPER_SETUPINT(2,1)
#define GASNETE_STRIDED_HELPER_SETUP3  GASNETE_STRIDED_HELPER_SETUP2 GASNETE_STRIDED_HELPER_SETUPINT(3,2)
#define GASNETE_STRIDED_HELPER_SETUP4  GASNETE_STRIDED_HELPER_SETUP3 GASNETE_STRIDED_HELPER_SETUPINT(4,3)
#define GASNETE_STRIDED_HELPER_SETUP5  GASNETE_STRIDED_HELPER_SETUP4 GASNETE_STRIDED_HELPER_SETUPINT(5,4)
#define GASNETE_STRIDED_HELPER_SETUP6  GASNETE_STRIDED_HELPER_SETUP5 GASNETE_STRIDED_HELPER_SETUPINT(6,5)
#define GASNETE_STRIDED_HELPER_SETUP7  GASNETE_STRIDED_HELPER_SETUP6 GASNETE_STRIDED_HELPER_SETUPINT(7,6)


#define GASNETE_STRIDED_HELPER_LOOP0              \
    for (_i0 = _count0; _i0; _i0--) {             \
      GASNETE_STRIDED_HELPER_LOOPBODY(psrc,pdst); \
      psrc += _srcbump0;                          \
      pdst += _dstbump0;                          \
    }

#define GASNETE_STRIDED_HELPER_LOOPHEAD(curr)     \
    for (_i##curr = _count##curr; _i##curr; _i##curr--) { 

#define GASNETE_STRIDED_HELPER_LOOPTAIL(curr) \
      psrc += _srcbump##curr;                 \
      pdst += _dstbump##curr;                 \
    }                                         \

#define GASNETE_STRIDED_HELPER_LOOP1  GASNETE_STRIDED_HELPER_LOOPHEAD(1) GASNETE_STRIDED_HELPER_LOOP0 GASNETE_STRIDED_HELPER_LOOPTAIL(1)
#define GASNETE_STRIDED_HELPER_LOOP2  GASNETE_STRIDED_HELPER_LOOPHEAD(2) GASNETE_STRIDED_HELPER_LOOP1 GASNETE_STRIDED_HELPER_LOOPTAIL(2)
#define GASNETE_STRIDED_HELPER_LOOP3  GASNETE_STRIDED_HELPER_LOOPHEAD(3) GASNETE_STRIDED_HELPER_LOOP2 GASNETE_STRIDED_HELPER_LOOPTAIL(3)
#define GASNETE_STRIDED_HELPER_LOOP4  GASNETE_STRIDED_HELPER_LOOPHEAD(4) GASNETE_STRIDED_HELPER_LOOP3 GASNETE_STRIDED_HELPER_LOOPTAIL(4)
#define GASNETE_STRIDED_HELPER_LOOP5  GASNETE_STRIDED_HELPER_LOOPHEAD(5) GASNETE_STRIDED_HELPER_LOOP4 GASNETE_STRIDED_HELPER_LOOPTAIL(5)
#define GASNETE_STRIDED_HELPER_LOOP6  GASNETE_STRIDED_HELPER_LOOPHEAD(6) GASNETE_STRIDED_HELPER_LOOP5 GASNETE_STRIDED_HELPER_LOOPTAIL(6)
#define GASNETE_STRIDED_HELPER_LOOP7  GASNETE_STRIDED_HELPER_LOOPHEAD(7) GASNETE_STRIDED_HELPER_LOOP6 GASNETE_STRIDED_HELPER_LOOPTAIL(7)

#define GASNETE_STRIDED_HELPER_CASE(curr) case curr+1: { \
    uint8_t *psrc = srcaddr;                             \
    uint8_t *pdst = dstaddr;                             \
    GASNETE_STRIDED_HELPER_SETUP##curr                   \
    GASNETE_STRIDED_HELPER_LOOP##curr                    \
    } break;

#define GASNETE_DIRECT_DIMS 15
#if GASNET_DEBUG
  #define GASNETE_CHECK_PTR(ploc, addr, strides, idx, dim) do { \
      int i;                                                    \
      uint8_t *ptest = (addr);                                  \
      for (i=0; i < dim; i++) {                                 \
        ptest += (idx)[i]*(strides)[i-1];                       \
      }                                                         \
      gasneti_assert(ptest == ploc);                            \
    } while (0)
#else
  #define GASNETE_CHECK_PTR(ploc, addr, strides, idx, dim) 
#endif

#define GASNETE_STRIDED_HELPER_CASES(limit,contiglevel)                \
    GASNETE_STRIDED_HELPER_CASE(0)                                     \
    GASNETE_STRIDED_HELPER_CASE(1)                                     \
    GASNETE_STRIDED_HELPER_CASE(2)                                     \
    GASNETE_STRIDED_HELPER_CASE(3)                                     \
    GASNETE_STRIDED_HELPER_CASE(4)                                     \
    GASNETE_STRIDED_HELPER_CASE(5)                                     \
    GASNETE_STRIDED_HELPER_CASE(6)                                     \
    GASNETE_STRIDED_HELPER_CASE(7)                                     \
    default: {                                                         \
      uint8_t *psrc = srcaddr;                                         \
      uint8_t *pdst = dstaddr;                                         \
      size_t const dim = (limit) - (contiglevel);                      \
      size_t const * const _count = count + contiglevel + 1;           \
      size_t const * const _srcstrides = srcstrides + contiglevel + 1; \
      size_t const * const _dststrides = dststrides + contiglevel + 1; \
      ssize_t curdim = 0; /* must be signed */                         \
      uint8_t *_srcptr_start[GASNETE_DIRECT_DIMS];                     \
      uint8_t ** const srcptr_start = (dim <= GASNETE_DIRECT_DIMS ?    \
         _srcptr_start : gasneti_malloc(sizeof(uint8_t *)*dim));       \
      uint8_t *_dstptr_start[GASNETE_DIRECT_DIMS];                     \
      uint8_t ** const dstptr_start = (dim <= GASNETE_DIRECT_DIMS ?    \
         _dstptr_start : gasneti_malloc(sizeof(uint8_t *)*dim));       \
      size_t *_idx[GASNETE_DIRECT_DIMS];                               \
      size_t * const idx = (dim <= GASNETE_DIRECT_DIMS ?               \
         _idx : gasneti_malloc(sizeof(size_t)*dim));                   \
      for (curdim = 0; curdim < dim; curdim++) {                       \
        idx[curdim] = 0;                                               \
        srcptr_start[curdim] = psrc;                                   \
        dstptr_start[curdim] = pdst;                                   \
      }                                                                \
      while (1) {                                                      \
        GASNETE_CHECK_PTR(psrc, srcaddr, _srcstrides, idx, dim);       \
        GASNETE_CHECK_PTR(pdst, dstaddr, _dststrides, idx, dim);       \
        GASNETE_STRIDED_HELPER_LOOPBODY(psrc,pdst);                    \
        for (curdim = 0; curdim < dim; curdim++) {                     \
          if (idx[curdim] < _count[curdim]-1) {                        \
            idx[curdim]++;                                             \
            psrc += _srcstrides[curdim-1];                             \
            pdst += _dststrides[curdim-1];                             \
            break;                                                     \
          } else {                                                     \
            idx[curdim] = 0;                                           \
            psrc = srcptr_start[curdim];                               \
            pdst = dstptr_start[curdim];                               \
          }                                                            \
        }                                                              \
        if (curdim == dim) break;                                      \
        for (curdim--; curdim >= 0; curdim--) {                        \
          srcptr_start[curdim] = psrc;                                 \
          dstptr_start[curdim] = pdst;                                 \
        }                                                              \
      }                                                                \
      if (dim > GASNETE_DIRECT_DIMS) {                                 \
        gasneti_free(idx);                                             \
        gasneti_free(srcptr_start);                                    \
        gasneti_free(dstptr_start);                                    \
      }                                                                \
    }

/*---------------------------------------------------------------------------------*/
/* reference versions that use individual put/gets */
gasnet_handle_t gasnete_puts_ref_indiv(gasnete_synctype_t synctype,
                                  gasnet_node_t dstnode,
                                   void *dstaddr, const size_t dststrides[],
                                   void *srcaddr, const size_t srcstrides[],
                                   const size_t count[], size_t stridelevels GASNETE_THREAD_FARG) {
  const int islocal = (dstnode == gasnete_mynode);
  size_t const contiglevel = gasnete_strided_dualcontiguity(dststrides, srcstrides, count, stridelevels);
  GASNETI_TRACE_EVENT(C, PUTS_REF_INDIV);
  gasneti_assert(!gasnete_strided_empty(count, stridelevels));
  GASNETE_START_NBIREGION(synctype, islocal);

  if (contiglevel == stridelevels) { /* fully contiguous at both ends */
    GASNETE_PUT_INDIV(islocal, dstnode, dstaddr, srcaddr, gasnete_strided_datasize(count, stridelevels));
  } else {
    size_t const limit = stridelevels - gasnete_strided_nulldims(count, stridelevels);
    size_t const contigsz = (contiglevel == 0 ? count[0] : count[contiglevel]*srcstrides[contiglevel-1]);

    gasneti_assert(limit > contiglevel);
    switch (limit - contiglevel) {
      #define GASNETE_STRIDED_HELPER_LOOPBODY(psrc,pdst)  \
        GASNETE_PUT_INDIV(islocal, dstnode, pdst, psrc, contigsz)
      GASNETE_STRIDED_HELPER_CASES(limit,contiglevel)
      #undef GASNETE_STRIDED_HELPER_LOOPBODY
    }
  }
  GASNETE_END_NBIREGION_AND_RETURN(synctype, islocal);
}

gasnet_handle_t gasnete_gets_ref_indiv(gasnete_synctype_t synctype,
                                   void *dstaddr, const size_t dststrides[],
                                   gasnet_node_t srcnode, 
                                   void *srcaddr, const size_t srcstrides[],
                                   const size_t count[], size_t stridelevels GASNETE_THREAD_FARG) {
  const int islocal = (srcnode == gasnete_mynode);
  size_t const contiglevel = gasnete_strided_dualcontiguity(dststrides, srcstrides, count, stridelevels);
  GASNETI_TRACE_EVENT(C, GETS_REF_INDIV);
  gasneti_assert(!gasnete_strided_empty(count, stridelevels));
  GASNETE_START_NBIREGION(synctype, islocal);

  if (contiglevel == stridelevels) { /* fully contiguous at both ends */
    GASNETE_GET_INDIV(islocal, dstaddr, srcnode, srcaddr, gasnete_strided_datasize(count, stridelevels));
  } else {
    size_t const limit = stridelevels - gasnete_strided_nulldims(count, stridelevels);
    size_t const contigsz = (contiglevel == 0 ? count[0] : count[contiglevel]*srcstrides[contiglevel-1]);

    gasneti_assert(limit > contiglevel);
    switch (limit - contiglevel) {
      #define GASNETE_STRIDED_HELPER_LOOPBODY(psrc,pdst)  \
        GASNETE_GET_INDIV(islocal, pdst, srcnode, psrc, contigsz)
      GASNETE_STRIDED_HELPER_CASES(limit,contiglevel)
      #undef GASNETE_STRIDED_HELPER_LOOPBODY
    }
  }
  GASNETE_END_NBIREGION_AND_RETURN(synctype, islocal);
}
/*---------------------------------------------------------------------------------*/
/* helpers for reference versions */

static void gasnete_convert_strided(const int tomemvec, void *_srclist, void *_dstlist, 
                                    gasnete_strided_stats_t *stats,
                                    void *dstaddr, const size_t dststrides[],
                                    void *srcaddr, const size_t srcstrides[],
                                    const size_t count[], size_t stridelevels) {
  size_t const contiglevel = stats->dualcontiguity;
  size_t const limit = stridelevels - stats->nulldims;
  size_t const srccontigsz = stats->srccontigsz;
  size_t const dstcontigsz = stats->dstcontigsz;

  gasneti_assert(_srclist != NULL && _dstlist != NULL && stats != NULL);
  gasneti_assert(!gasnete_strided_empty(count, stridelevels));
  gasneti_assert(limit > contiglevel);

  if (!tomemvec) { /* indexed case */
    void * * const srclist = (void * *)_srclist;
    void * * const dstlist = (void * *)_dstlist;
    void * * srcpos = srclist;
    void * * dstpos = dstlist;

    if (srccontigsz == dstcontigsz) {
      switch (limit - contiglevel) {
        #define GASNETE_STRIDED_HELPER_LOOPBODY(psrc,pdst)  do { \
          *(srcpos) = psrc;                                      \
          srcpos++;                                              \
          *(dstpos) = pdst;                                      \
          dstpos++;                                              \
        } while(0)
        GASNETE_STRIDED_HELPER_CASES(limit,contiglevel)
        #undef GASNETE_STRIDED_HELPER_LOOPBODY
      }
    } else if (srccontigsz < dstcontigsz) {
      size_t const looplim = dstcontigsz / srccontigsz;
      size_t loopcnt = 0;
      gasneti_assert(looplim*srccontigsz == dstcontigsz);
      /* TODO: this loop could be made more efficient */
      switch (limit - contiglevel) {
        #define GASNETE_STRIDED_HELPER_LOOPBODY(psrc,pdst)  do { \
          *(srcpos) = psrc;                                      \
          srcpos++;                                              \
          if (loopcnt == 0) {                                    \
            *(dstpos) = pdst;                                    \
            dstpos++;                                            \
            loopcnt = looplim;                                   \
          }                                                      \
          loopcnt--;                                             \
        } while(0)
        GASNETE_STRIDED_HELPER_CASES(limit,contiglevel)
        #undef GASNETE_STRIDED_HELPER_LOOPBODY
      }
    } else { /* srccontigsz > dstcontigsz */
      size_t const looplim = srccontigsz / dstcontigsz;
      size_t loopcnt = 0;
      gasneti_assert(looplim*dstcontigsz == srccontigsz);
      /* TODO: this loop could be made more efficient */
      switch (limit - contiglevel) {
        #define GASNETE_STRIDED_HELPER_LOOPBODY(psrc,pdst)  do { \
          if (loopcnt == 0) {                                    \
            *(srcpos) = psrc;                                    \
            srcpos++;                                            \
            loopcnt = looplim;                                   \
          }                                                      \
          loopcnt--;                                             \
          *(dstpos) = pdst;                                      \
          dstpos++;                                              \
        } while(0)
        GASNETE_STRIDED_HELPER_CASES(limit,contiglevel)
        #undef GASNETE_STRIDED_HELPER_LOOPBODY
      }
    }
    gasneti_assert(srcpos == srclist+stats->srcsegments);
    gasneti_assert(dstpos == dstlist+stats->dstsegments);
  } else { /* memvec case */
    gasnet_memvec_t * const srclist = (gasnet_memvec_t *)_srclist;
    gasnet_memvec_t * const dstlist = (gasnet_memvec_t *)_dstlist;
    gasnet_memvec_t * srcpos = srclist;
    gasnet_memvec_t * dstpos = dstlist;

    if (srccontigsz == dstcontigsz) {
      switch (limit - contiglevel) {
        #define GASNETE_STRIDED_HELPER_LOOPBODY(psrc,pdst)  do { \
          srcpos->addr = psrc;                                   \
          srcpos->len = srccontigsz;                             \
          srcpos++;                                              \
          dstpos->addr = pdst;                                   \
          dstpos->len = dstcontigsz;                             \
          dstpos++;                                              \
        } while(0)
        GASNETE_STRIDED_HELPER_CASES(limit,contiglevel)
        #undef GASNETE_STRIDED_HELPER_LOOPBODY
      }
    } else if (srccontigsz < dstcontigsz) {
      size_t const looplim = dstcontigsz / srccontigsz;
      size_t loopcnt = 0;
      gasneti_assert(looplim*srccontigsz == dstcontigsz);
      /* TODO: this loop could be made more efficient */
      switch (limit - contiglevel) {
        #define GASNETE_STRIDED_HELPER_LOOPBODY(psrc,pdst)  do { \
          srcpos->addr = psrc;                                   \
          srcpos->len = srccontigsz;                             \
          srcpos++;                                              \
          if (loopcnt == 0) {                                    \
            dstpos->addr = pdst;                                 \
            dstpos->len = dstcontigsz;                           \
            dstpos++;                                            \
            loopcnt = looplim;                                   \
          }                                                      \
          loopcnt--;                                             \
        } while(0)
        GASNETE_STRIDED_HELPER_CASES(limit,contiglevel)
        #undef GASNETE_STRIDED_HELPER_LOOPBODY
      }
    } else { /* srccontigsz > dstcontigsz */
      size_t const looplim = srccontigsz / dstcontigsz;
      size_t loopcnt = 0;
      gasneti_assert(looplim*dstcontigsz == srccontigsz);
      /* TODO: this loop could be made more efficient */
      switch (limit - contiglevel) {
        #define GASNETE_STRIDED_HELPER_LOOPBODY(psrc,pdst)  do { \
          if (loopcnt == 0) {                                    \
            srcpos->addr = psrc;                                 \
            srcpos->len = srccontigsz;                           \
            srcpos++;                                            \
            loopcnt = looplim;                                   \
          }                                                      \
          loopcnt--;                                             \
          dstpos->addr = pdst;                                   \
          dstpos->len = dstcontigsz;                             \
          dstpos++;                                              \
        } while(0)
        GASNETE_STRIDED_HELPER_CASES(limit,contiglevel)
        #undef GASNETE_STRIDED_HELPER_LOOPBODY
      }
    }
    gasneti_assert(srcpos == srclist+stats->srcsegments);
    gasneti_assert(dstpos == dstlist+stats->dstsegments);
  }
}
/*---------------------------------------------------------------------------------*/
/* reference versions that use vector interface */

gasnet_handle_t gasnete_puts_ref_vector(gasnete_synctype_t synctype,
                                  gasnet_node_t dstnode,
                                   void *dstaddr, const size_t dststrides[],
                                   void *srcaddr, const size_t srcstrides[],
                                   const size_t count[], size_t stridelevels GASNETE_THREAD_FARG) {
  gasnete_strided_stats_t stats;
  GASNETI_TRACE_EVENT(C, PUTS_REF_VECTOR);
  gasneti_assert(!gasnete_strided_empty(count, stridelevels));
  gasneti_assert(GASNETE_PUTV_ALLOWS_VOLATILE_METADATA);
  gasnete_strided_stats(&stats, dststrides, srcstrides, count, stridelevels);

  if (stats.dualcontiguity == stridelevels) { /* fully contiguous at both ends */
    const int islocal = (dstnode == gasnete_mynode);
    GASNETE_START_NBIREGION(synctype, islocal);
      GASNETE_PUT_INDIV(islocal, dstnode, dstaddr, srcaddr, stats.totalsz);
    GASNETE_END_NBIREGION_AND_RETURN(synctype, islocal);
  } else {
    gasnet_handle_t retval;
    gasnet_memvec_t * const srclist = gasneti_malloc(sizeof(gasnet_memvec_t)*stats.srcsegments);
    gasnet_memvec_t * const dstlist = gasneti_malloc(sizeof(gasnet_memvec_t)*stats.dstsegments);

    gasnete_convert_strided(1, srclist, dstlist, &stats, 
      dstaddr, dststrides, srcaddr, srcstrides, count, stridelevels);

    retval = gasnete_putv(synctype, dstnode, 
                          stats.dstsegments, dstlist, 
                          stats.srcsegments, srclist GASNETE_THREAD_PASS);
    gasneti_free(srclist);
    gasneti_free(dstlist);
    return retval; 
  }
}

gasnet_handle_t gasnete_gets_ref_vector(gasnete_synctype_t synctype,
                                   void *dstaddr, const size_t dststrides[],
                                   gasnet_node_t srcnode, 
                                   void *srcaddr, const size_t srcstrides[],
                                   const size_t count[], size_t stridelevels GASNETE_THREAD_FARG) {
  gasnete_strided_stats_t stats;
  GASNETI_TRACE_EVENT(C, GETS_REF_VECTOR);
  gasneti_assert(!gasnete_strided_empty(count, stridelevels));
  gasneti_assert(GASNETE_GETV_ALLOWS_VOLATILE_METADATA);
  gasnete_strided_stats(&stats, dststrides, srcstrides, count, stridelevels);

  if (stats.dualcontiguity == stridelevels) { /* fully contiguous at both ends */
    const int islocal = (srcnode == gasnete_mynode);
    GASNETE_START_NBIREGION(synctype, islocal);
      GASNETE_GET_INDIV(islocal, dstaddr, srcnode, srcaddr, stats.totalsz);
    GASNETE_END_NBIREGION_AND_RETURN(synctype, islocal);
  } else {
    gasnet_handle_t retval;
    gasnet_memvec_t * const srclist = gasneti_malloc(sizeof(gasnet_memvec_t)*stats.srcsegments);
    gasnet_memvec_t * const dstlist = gasneti_malloc(sizeof(gasnet_memvec_t)*stats.dstsegments);

    gasnete_convert_strided(1, srclist, dstlist, &stats, 
      dstaddr, dststrides, srcaddr, srcstrides, count, stridelevels);

    retval = gasnete_getv(synctype, 
                          stats.dstsegments, dstlist, 
                          srcnode,
                          stats.srcsegments, srclist GASNETE_THREAD_PASS);
    gasneti_free(srclist);
    gasneti_free(dstlist);
    return retval; 
  }
}
/*---------------------------------------------------------------------------------*/
/* reference versions that use indexed interface */
gasnet_handle_t gasnete_puts_ref_indexed(gasnete_synctype_t synctype,
                                  gasnet_node_t dstnode,
                                   void *dstaddr, const size_t dststrides[],
                                   void *srcaddr, const size_t srcstrides[],
                                   const size_t count[], size_t stridelevels GASNETE_THREAD_FARG) {
  gasnete_strided_stats_t stats;
  GASNETI_TRACE_EVENT(C, PUTS_REF_INDEXED);
  gasneti_assert(!gasnete_strided_empty(count, stridelevels));
  gasneti_assert(GASNETE_PUTI_ALLOWS_VOLATILE_METADATA);
  gasnete_strided_stats(&stats, dststrides, srcstrides, count, stridelevels);

  if (stats.dualcontiguity == stridelevels) { /* fully contiguous at both ends */
    const int islocal = (dstnode == gasnete_mynode);
    GASNETE_START_NBIREGION(synctype, islocal);
      GASNETE_PUT_INDIV(islocal, dstnode, dstaddr, srcaddr, stats.totalsz);
    GASNETE_END_NBIREGION_AND_RETURN(synctype, islocal);
  } else {
    gasnet_handle_t retval;
    void * * const srclist = gasneti_malloc(sizeof(void *)*stats.srcsegments);
    void * * const dstlist = gasneti_malloc(sizeof(void *)*stats.dstsegments);

    gasnete_convert_strided(0, srclist, dstlist, &stats, 
      dstaddr, dststrides, srcaddr, srcstrides, count, stridelevels);

    retval = gasnete_puti(synctype, dstnode, 
                          stats.dstsegments, dstlist, stats.dstcontigsz,
                          stats.srcsegments, srclist, stats.srccontigsz GASNETE_THREAD_PASS);
    gasneti_free(srclist);
    gasneti_free(dstlist);
    return retval; 
  }
}

gasnet_handle_t gasnete_gets_ref_indexed(gasnete_synctype_t synctype,
                                   void *dstaddr, const size_t dststrides[],
                                   gasnet_node_t srcnode, 
                                   void *srcaddr, const size_t srcstrides[],
                                   const size_t count[], size_t stridelevels GASNETE_THREAD_FARG) {
  gasnete_strided_stats_t stats;
  GASNETI_TRACE_EVENT(C, GETS_REF_INDEXED);
  gasneti_assert(!gasnete_strided_empty(count, stridelevels));
  gasneti_assert(GASNETE_GETI_ALLOWS_VOLATILE_METADATA);
  gasnete_strided_stats(&stats, dststrides, srcstrides, count, stridelevels);

  if (stats.dualcontiguity == stridelevels) { /* fully contiguous at both ends */
    const int islocal = (srcnode == gasnete_mynode);
    GASNETE_START_NBIREGION(synctype, islocal);
      GASNETE_GET_INDIV(islocal, dstaddr, srcnode, srcaddr, stats.totalsz);
    GASNETE_END_NBIREGION_AND_RETURN(synctype, islocal);
  } else {
    gasnet_handle_t retval;
    void * * const srclist = gasneti_malloc(sizeof(void *)*stats.srcsegments);
    void * * const dstlist = gasneti_malloc(sizeof(void *)*stats.dstsegments);

    gasnete_convert_strided(0, srclist, dstlist, &stats, 
      dstaddr, dststrides, srcaddr, srcstrides, count, stridelevels);

    retval = gasnete_geti(synctype,
                          stats.dstsegments, dstlist, stats.dstcontigsz,
                          srcnode,
                          stats.srcsegments, srclist, stats.srccontigsz GASNETE_THREAD_PASS);
    gasneti_free(srclist);
    gasneti_free(dstlist);
    return retval; 
  }
}
/*---------------------------------------------------------------------------------*/
#ifndef GASNETE_PUTS_OVERRIDE
extern gasnet_handle_t gasnete_puts(gasnete_synctype_t synctype,
                                  gasnet_node_t dstnode,
                                   void *dstaddr, const size_t dststrides[],
                                   void *srcaddr, const size_t srcstrides[],
                                   const size_t count[], size_t stridelevels GASNETE_THREAD_FARG) {
  #if GASNET_DEBUG
   gasnete_strided_stats_t stats; /* solely for debugging */
   gasnete_strided_stats(&stats, dststrides, srcstrides, count, stridelevels);
  #endif
  /* catch silly degenerate cases */
  if_pf (gasnete_strided_empty(count, stridelevels)) /* empty */
    return GASNET_INVALID_HANDLE;
  if_pf (dstnode == gasnete_mynode || /* purely local */ 
         stridelevels == 0 || /* fully contiguous */
         gasnete_strided_dualcontiguity(dststrides, srcstrides, count, stridelevels) == stridelevels) { 
    return gasnete_puts_ref_indiv(synctype,dstnode,dstaddr,dststrides,srcaddr,srcstrides,count,stridelevels GASNETE_THREAD_PASS);
  }

  /* select algorithm */
  #ifdef GASNETE_PUTS_SELECTOR
    GASNETE_PUTS_SELECTOR(synctype,dstnode,dstaddr,dststrides,srcaddr,srcstrides,count,stridelevels);
  #else
    switch (rand() % 3) {
      case 0:
        return gasnete_puts_ref_indiv(synctype,dstnode,dstaddr,dststrides,srcaddr,srcstrides,count,stridelevels GASNETE_THREAD_PASS);
      case 1:
        return gasnete_puts_ref_vector(synctype,dstnode,dstaddr,dststrides,srcaddr,srcstrides,count,stridelevels GASNETE_THREAD_PASS);
      case 2:
        return gasnete_puts_ref_indexed(synctype,dstnode,dstaddr,dststrides,srcaddr,srcstrides,count,stridelevels GASNETE_THREAD_PASS);
    }
  #endif
  gasneti_fatalerror("failure in GASNETE_PUTS_SELECTOR - should never reach here");
}
#endif

#ifndef GASNETE_GETS_OVERRIDE
extern gasnet_handle_t gasnete_gets(gasnete_synctype_t synctype,
                                   void *dstaddr, const size_t dststrides[],
                                   gasnet_node_t srcnode, 
                                   void *srcaddr, const size_t srcstrides[],
                                   const size_t count[], size_t stridelevels GASNETE_THREAD_FARG) {
  #if GASNET_DEBUG
   gasnete_strided_stats_t stats; /* solely for debugging */
   gasnete_strided_stats(&stats, dststrides, srcstrides, count, stridelevels);
  #endif
  /* catch silly degenerate cases */
  if_pf (gasnete_strided_empty(count, stridelevels)) /* empty */
    return GASNET_INVALID_HANDLE;
  if_pf (srcnode == gasnete_mynode || /* purely local */ 
         stridelevels == 0 || /* fully contiguous */
         gasnete_strided_dualcontiguity(dststrides, srcstrides, count, stridelevels) == stridelevels) { 
    return gasnete_gets_ref_indiv(synctype,dstaddr,dststrides,srcnode,srcaddr,srcstrides,count,stridelevels GASNETE_THREAD_PASS);
  }

  /* select algorithm */
  #ifdef GASNETE_GETS_SELECTOR
    GASNETE_GETS_SELECTOR(synctype,dstaddr,dststrides,srcnode,srcaddr,srcstrides,count,stridelevels);
  #else
    switch (rand() % 3) {
      case 0:
        return gasnete_gets_ref_indiv(synctype,dstaddr,dststrides,srcnode,srcaddr,srcstrides,count,stridelevels GASNETE_THREAD_PASS);
      case 1:
        return gasnete_gets_ref_vector(synctype,dstaddr,dststrides,srcnode,srcaddr,srcstrides,count,stridelevels GASNETE_THREAD_PASS);
      case 2:
        return gasnete_gets_ref_indexed(synctype,dstaddr,dststrides,srcnode,srcaddr,srcstrides,count,stridelevels GASNETE_THREAD_PASS);
    }
  #endif
  gasneti_fatalerror("failure in GASNETE_GETS_SELECTOR - should never reach here");
}
#endif

/*---------------------------------------------------------------------------------*/
/* ***  Handlers *** */
/*---------------------------------------------------------------------------------*/
#if 0
#define GASNETE_REFVIS_HANDLERS()                                 \
/*  gasneti_handler_tableentry_no_bits(gasnete__reqh) */
#endif
/*---------------------------------------------------------------------------------*/


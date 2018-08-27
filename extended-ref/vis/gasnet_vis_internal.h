/*   $Source: bitbucket.org:berkeleylab/gasnet.git/extended-ref/vis/gasnet_vis_internal.h $
 * Description: Internal definitions for GASNet Vector, Indexed & Strided implementation
 * Copyright 2002, Dan Bonachea <bonachea@cs.berkeley.edu>
 * Terms of use are as specified in license.txt
 */

#ifndef _GASNET_VIS_INTERNAL_H
#define _GASNET_VIS_INTERNAL_H

#include <gasnet_internal.h>
#include <gasnet_vis.h>

/*---------------------------------------------------------------------------------*/
/* ***  VIS state *** */
/*---------------------------------------------------------------------------------*/
/* represents a VIS operation in flight */
typedef struct gasneti_vis_op_S {
  struct gasneti_vis_op_S *next;
  uint8_t type;
  void *addr;
  #if GASNETI_HAVE_EOP_INTERFACE
    gasneti_eop_t *eop;
    gasneti_iop_t *iop;
  #endif
  gasneti_weakatomic_t packetcnt;
  size_t count;
  size_t len;
  gasnet_handle_t handle;
} gasneti_vis_op_t;

/* per-thread state for VIS */
typedef struct {
  gasneti_vis_op_t *active_ops;
  gasneti_vis_op_t *free_ops;
  int progressfn_active;
  #ifdef GASNETE_VIS_THREADDATA_EXTRA
    GASNETE_VIS_THREADDATA_EXTRA
  #endif
} gasnete_vis_threaddata_t;

static void gasnete_vis_cleanup_threaddata(void *_td) {
  gasnete_vis_threaddata_t *td = (gasnete_vis_threaddata_t *)_td;
  gasneti_vis_op_t *op;
  #ifdef GASNETE_VIS_THREADDATA_EXTRA_CLEANUP
    GASNETE_VIS_THREADDATA_EXTRA_CLEANUP(td);
  #endif
  gasneti_assert(td->active_ops == NULL);
  while ((op = td->free_ops) != NULL) {
    td->free_ops = op->next;
    gasneti_free(op);
  }
  gasneti_free(td);
}

GASNETI_INLINE(gasnete_vis_new_threaddata) GASNETI_MALLOC
gasnete_vis_threaddata_t *gasnete_vis_new_threaddata(void) {
  gasnete_vis_threaddata_t *result = gasneti_calloc(1,sizeof(*result));
  #ifdef GASNETE_VIS_THREADDATA_EXTRA_INIT
    GASNETE_VIS_THREADDATA_EXTRA_INIT(result);
  #endif
  gasnete_register_threadcleanup(gasnete_vis_cleanup_threaddata, result);
  return result;
}

#define _GASNETE_VIS_MYTHREAD(mythread)     \
        (mythread->gasnete_vis_threaddata ? \
         mythread->gasnete_vis_threaddata : \
        (mythread->gasnete_vis_threaddata = gasnete_vis_new_threaddata()))
#define GASNETE_VIS_MYTHREAD        _GASNETE_VIS_MYTHREAD(GASNETI_MYTHREAD)

#define GASNETI_VIS_CAT_PUTV_GATHER       1
#define GASNETI_VIS_CAT_GETV_SCATTER      2
#define GASNETI_VIS_CAT_PUTI_GATHER       3
#define GASNETI_VIS_CAT_GETI_SCATTER      4
#define GASNETI_VIS_CAT_PUTS_GATHER       5
#define GASNETI_VIS_CAT_GETS_SCATTER      6
#define GASNETI_VIS_CAT_PUTV_AMPIPELINE   7
#define GASNETI_VIS_CAT_GETV_AMPIPELINE   8
#define GASNETI_VIS_CAT_PUTI_AMPIPELINE   9
#define GASNETI_VIS_CAT_GETI_AMPIPELINE   10
#define GASNETI_VIS_CAT_PUTS_AMPIPELINE   11
#define GASNETI_VIS_CAT_GETS_AMPIPELINE   12

/*---------------------------------------------------------------------------------*/
/* VISOP manipulation */
#if GASNETI_HAVE_EOP_INTERFACE
/* create a dummy eop/iop based on synctype, save it in visop */
#define GASNETE_VISOP_SETUP(visop, synctype, isget) do {              \
    if (synctype == gasnete_synctype_nbi) {                           \
      visop->eop = NULL;                                              \
      visop->iop = gasneti_iop_register(1,isget GASNETI_THREAD_PASS); \
    } else {                                                          \
      visop->eop = gasneti_eop_create(GASNETI_THREAD_PASS_ALONE);     \
      visop->iop = NULL;                                              \
    }                                                                 \
} while (0)

/* Must not reference visop, which may no longer exist */
#define GASNETE_VISOP_RETURN_VOLATILE(eop, synctype) do {            \
    switch (synctype) {                                              \
      case gasnete_synctype_b: {                                     \
        gasnet_handle_t h = gasneti_eop_to_handle(eop);              \
        gasnete_wait_syncnb(h);                                      \
        return GASNET_INVALID_HANDLE;                                \
      }                                                              \
      case gasnete_synctype_nb:                                      \
        return gasneti_eop_to_handle(eop);                           \
      case gasnete_synctype_nbi:                                     \
        return GASNET_INVALID_HANDLE;                                \
      default: gasneti_unreachable_error(("bad synctype: 0x%x",(int)synctype)); \
        return GASNET_INVALID_HANDLE; /* avoid warning on MIPSPro */ \
    }                                                                \
} while (0)

#define GASNETE_VISOP_RETURN(visop, synctype) \
    GASNETE_VISOP_RETURN_VOLATILE(visop->eop, synctype)

/* signal a visop dummy eop/iop */
#define GASNETE_VISOP_SIGNAL(visop, isget) do {       \
    gasneti_assert(visop->eop || visop->iop);         \
    if (visop->eop) gasneti_eop_markdone(visop->eop); \
    else gasneti_iop_markdone(visop->iop, 1, isget);  \
  } while (0)
#else
#define GASNETE_ERROR_NO_EOP_INTERFACE() gasneti_fatalerror("Tried to invoke GASNETE_VISOP_SIGNAL without GASNETI_HAVE_EOP_INTERFACE at %s:%i",__FILE__,__LINE__)
#define GASNETE_VISOP_SIGNAL(visop, isget) GASNETE_ERROR_NO_EOP_INTERFACE()
#define GASNETE_VISOP_SIGNAL(visop, isget) GASNETE_ERROR_NO_EOP_INTERFACE()
#define GASNETE_VISOP_SIGNAL(visop, isget) GASNETE_ERROR_NO_EOP_INTERFACE()
#endif

/* do GASNETE_VISOP_SETUP, push the visop on the thread-specific list 
   and do GASNETE_VISOP_RETURN */
#define GASNETE_PUSH_VISOP_RETURN(td, visop, synctype, isget) do {   \
    GASNETE_VISOP_SETUP(visop, synctype, isget);                     \
    GASNETI_PROGRESSFNS_ENABLE(gasneti_pf_vis,COUNTED);              \
    visop->next = td->active_ops; /* push on thread-specific list */ \
    td->active_ops = visop;                                          \
    GASNETE_VISOP_RETURN(visop, synctype);                           \
} while (0)
/*---------------------------------------------------------------------------------*/
/* ***  Individual put/get helpers *** */
/*---------------------------------------------------------------------------------*/
/* helper for vis functions implemented atop other GASNet operations
   start a recursive NBI access region, if appropriate */
#define GASNETE_START_NBIREGION(synctype, islocal) do {    \
  if (synctype != gasnete_synctype_nbi && !islocal)        \
    gasnete_begin_nbi_accessregion(1 GASNETI_THREAD_PASS); \
  } while(0)
/* finish a region started with GASNETE_START_NBIREGION,
   block if required, and return the appropriate handle */
#define GASNETE_END_NBIREGION_AND_RETURN(synctype, islocal) do {                      \
    if (islocal) return GASNET_INVALID_HANDLE;                                        \
    switch (synctype) {                                                               \
      case gasnete_synctype_nb:                                                       \
        return gasnete_end_nbi_accessregion(GASNETI_THREAD_PASS_ALONE);               \
      case gasnete_synctype_b:                                                        \
        gasnete_wait_syncnb(gasnete_end_nbi_accessregion(GASNETI_THREAD_PASS_ALONE)); \
        return GASNET_INVALID_HANDLE;                                                 \
      case gasnete_synctype_nbi:                                                      \
        return GASNET_INVALID_HANDLE;                                                 \
      default: gasneti_unreachable_error(("bad synctype: 0x%x",(int)synctype));       \
        return GASNET_INVALID_HANDLE; /* avoid warning on MIPSPro */                  \
    }                                                                                 \
  } while(0)

#define GASNETE_PUT_INDIV(islocal, dstnode, dstaddr, srcaddr, nbytes) do {      \
    gasneti_assert(nbytes > 0);                                                 \
    gasneti_boundscheck_allowoutseg(dstnode, dstaddr, nbytes);                  \
    gasneti_assert(islocal == (dstnode == gasneti_mynode));                     \
    if (islocal) GASNETI_MEMCPY((dstaddr), (srcaddr), (nbytes));                \
    else gasnete_put_nbi_bulk((dstnode), (dstaddr), (srcaddr), (nbytes)         \
                                GASNETI_THREAD_PASS);                           \
  } while (0)

#define GASNETE_GET_INDIV(islocal, dstaddr, srcnode, srcaddr, nbytes) do {      \
    gasneti_assert(nbytes > 0);                                                 \
    gasneti_boundscheck_allowoutseg(srcnode, srcaddr, nbytes);                  \
    gasneti_assert(islocal == (srcnode == gasneti_mynode));                     \
    if (islocal) GASNETI_MEMCPY((dstaddr), (srcaddr), (nbytes));                \
    else gasnete_get_nbi_bulk((dstaddr), (srcnode), (srcaddr), (nbytes)         \
                                GASNETI_THREAD_PASS);                           \
  } while (0)


/*---------------------------------------------------------------------------------*/
/* packing/unpacking helpers */
#define _GASNETE_PACK_HELPER(packed, unpacked, sz)   GASNETI_MEMCPY((packed), (unpacked), (sz))
#define _GASNETE_UNPACK_HELPER(packed, unpacked, sz) GASNETI_MEMCPY((unpacked), (packed), (sz))

/*---------------------------------------------------------------------------------*/
/* packetization */
typedef struct {
  size_t firstidx;
  size_t firstoffset;
  size_t lastidx;
  size_t lastlen;
} gasnete_packetdesc_t;

extern void gasnete_packetize_verify(gasnete_packetdesc_t *pt, size_t ptidx, int lastpacket,
                              size_t count, size_t len, gasnet_memvec_t const *list);

/*---------------------------------------------------------------------------------*/

#if PLATFORM_COMPILER_SUN_C
  /* disable a harmless warning */
  #pragma error_messages(off, E_STATEMENT_NOT_REACHED)
#endif

#endif

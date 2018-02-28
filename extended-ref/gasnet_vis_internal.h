/*   $Source: bitbucket.org:berkeleylab/gasnet.git/extended-ref/gasnet_vis_internal.h $
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
  gex_Event_t event;
} gasneti_vis_op_t;

#define SMD_SELF  0
#define SMD_PEER 1
// gasneti_vis_smd_dim_t represents the meta data parameters for a particular dimension
typedef struct {
  size_t    count;     // dimensional extent
  ptrdiff_t stride[2]; // dimensional stride in bytes, [0]=self [1]=peer
} gasneti_vis_smd_dim_t;

// gasneti_vis_smd_t represents complete information about a strided transfer
// in a format convenient for applying transformations
typedef struct {
  // -----------------------------------------------------------------------
  // post-analysis stats:
  #if GASNET_DEBUG
    int have_stats;             // true iff the fields in this section are valid
  #endif
  size_t totalsz;               // the total bytes of data in the transfer
  size_t elemcnt;               // number of elements in the transfer, aka dual-lcontig_segments
                                // Note that post-optimization the following properties hold:
                                //   dual-lcontig_sz == elemsz
                                //   dual-lcontig_dims == 0
  size_t lcontig_dims[2];       // highest stridelevel with linear contiguity in this region
                                // eg. zero if only the bottom level is linear contiguous,
                                // and stridelevels if the entire region is linear contiguous
  size_t lcontig_sz[2];         // size of the linear contiguous segments in this region
  size_t lcontig_segments[2];   // number of linear contiguous segments in this region
  // -----------------------------------------------------------------------
  // normative metadata:
  size_t stridelevels;          // dimensional cardinality
  size_t elemsz;                // dual-lcontig_sz (post-optimization)
  void  *addr[2];               // base addresses [0]=self [1]=peer
  gasneti_vis_smd_dim_t dim[1]; // per-dimension metadata,
                                // actually [stridelevels] entries (flexible array member)
  // DO NOT PUT ANYTHING HERE
} gasneti_vis_smd_t;

// gasneti_strided_op_t "is a" gasneti_vis_op_t that represents a strided operation in flight
// the embedded metadata may have been optimized/transformed relative to user's input
typedef struct {
  gasneti_vis_op_t visop; // must be first
  void *bouncebuf;        // separate subobject to free on destruction, otherwise NULL
  void *scratch;          // scratch space at the end of this object
  gasneti_vis_smd_t smd;  // variable-length strided metadata, must be last!
  // DO NOT PUT ANYTHING HERE
} gasneti_strided_op_t;

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

/* gasnete_threaddata_t might not be defined yet, but VIS ptr must be 3rd */
#define GASNETE_VIS_MYTHREAD (((void **)GASNETE_MYTHREAD)[2] ? \
        ((void **)GASNETE_MYTHREAD)[2] :                       \
        (((void **)GASNETE_MYTHREAD)[2] = gasnete_vis_new_threaddata()))

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
      visop->iop = gasneti_iop_register(1,isget GASNETE_THREAD_PASS); \
    } else {                                                          \
      visop->eop = gasneti_eop_create(GASNETE_THREAD_PASS_ALONE);     \
      visop->iop = NULL;                                              \
    }                                                                 \
} while (0)

/* Must not reference visop, which may no longer exist */
#define GASNETE_VISOP_RETURN_VOLATILE(eop, synctype) do {            \
    switch (synctype) {                                              \
      case gasnete_synctype_b: {                                     \
        gex_Event_t h = gasneti_eop_to_event(eop);                   \
        gasnete_wait(h GASNETI_THREAD_PASS);                         \
        return GEX_EVENT_INVALID;                                    \
      }                                                              \
      case gasnete_synctype_nb:                                      \
        return gasneti_eop_to_event(eop);                            \
      case gasnete_synctype_nbi:                                     \
        return GEX_EVENT_INVALID;                                    \
      default: gasneti_unreachable();                                \
        return GEX_EVENT_INVALID; /* avoid warning on MIPSPro */     \
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
// TODO-EX: rework these after removing GASNETE_OLD_STRIDED
/* helper for vis functions implemented atop other GASNet operations
   start a recursive NBI access region, if appropriate */
#define GASNETE_START_NBIREGION(synctype, islocal) do {    \
  if (islocal) GASNETE_ASSERT_OLD_STRIDED();               \
  if (synctype != gasnete_synctype_nbi && !islocal)        \
    gasnete_begin_nbi_accessregion(0,1 GASNETE_THREAD_PASS); \
  } while(0)
/* finish a region started with GASNETE_START_NBIREGION,
   block if required, and return the appropriate event */
#define GASNETE_END_NBIREGION_AND_RETURN(synctype, islocal) do {                      \
    if (islocal) return GEX_EVENT_INVALID;                                            \
    switch (synctype) {                                                               \
      case gasnete_synctype_nb:                                                       \
        return gasnete_end_nbi_accessregion(0 GASNETE_THREAD_PASS);                   \
      case gasnete_synctype_b:                                                        \
        gasnete_wait(gasnete_end_nbi_accessregion(0 GASNETE_THREAD_PASS) GASNETE_THREAD_PASS); \
        return GEX_EVENT_INVALID;                                                     \
      case gasnete_synctype_nbi:                                                      \
        return GEX_EVENT_INVALID;                                                     \
      default: gasneti_unreachable();                                                 \
        return GEX_EVENT_INVALID; /* avoid warning on MIPSPro */                      \
    }                                                                                 \
  } while(0)

#define GASNETE_PUT_INDIV(islocal, dstnode, dstaddr, srcaddr, nbytes) do {      \
    gasneti_assert(nbytes > 0);                                                 \
    gasneti_boundscheck_allowoutseg(gasneti_THUNK_TM, dstnode, dstaddr, nbytes);    \
    gasneti_assert(islocal == (dstnode == gasneti_mynode));                     \
    if (islocal) GASNETE_FAST_UNALIGNED_MEMCPY((dstaddr), (srcaddr), (nbytes)); \
    else gasnete_put_nbi(gasneti_THUNK_TM, (dstnode), (dstaddr), (srcaddr), (nbytes), \
                         GEX_EVENT_DEFER, 0 GASNETE_THREAD_PASS);              \
  } while (0)

#define GASNETE_GET_INDIV(islocal, dstaddr, srcnode, srcaddr, nbytes) do {      \
    gasneti_assert(nbytes > 0);                                                 \
    gasneti_boundscheck_allowoutseg(gasneti_THUNK_TM, srcnode, srcaddr, nbytes);    \
    gasneti_assert(islocal == (srcnode == gasneti_mynode));                     \
    if (islocal) GASNETE_FAST_UNALIGNED_MEMCPY((dstaddr), (srcaddr), (nbytes)); \
    else gasnete_get_nbi(gasneti_THUNK_TM, (dstaddr), (srcnode), (srcaddr), (nbytes), \
                         0 GASNETE_THREAD_PASS);                                \
  } while (0)

// Put/get for degenerate case, where this single op represents the entire operation
// Casts from int -> gex_Event_t in NBI/Blocking cases are valid because they only
// care about zero versus non-zero.
// NOTE: cannot use gasnete_* variants here, as they are currently non-functional on smp/nopshm

#define GASNETE_PUT_DEGEN(retval, synctype, tm, rank, dstaddr, srcaddr, nbytes, flags) do { \
    gasneti_assert((nbytes) > 0);                                                   \
    gasneti_boundscheck_allowoutseg((tm), (rank), (dstaddr), (nbytes));             \
    switch (synctype) {                                                             \
      case gasnete_synctype_nb:                                                     \
        (retval) = _gex_RMA_PutNB ((tm), (rank), (dstaddr), (srcaddr), (nbytes),    \
                         GEX_EVENT_DEFER, (flags) GASNETE_THREAD_PASS);             \
        break;                                                                      \
      case gasnete_synctype_nbi:                                                    \
        (retval) = (gex_Event_t)(intptr_t)                                          \
                   _gex_RMA_PutNBI((tm), (rank), (dstaddr), (srcaddr), (nbytes),    \
                         GEX_EVENT_DEFER, (flags) GASNETE_THREAD_PASS);             \
        break;                                                                      \
      case gasnete_synctype_b:                                                      \
        (retval) = (gex_Event_t)(intptr_t)                                          \
              _gex_RMA_PutBlocking((tm), (rank), (dstaddr), (srcaddr), (nbytes),    \
                                          (flags) GASNETE_THREAD_PASS);             \
        break;                                                                      \
      default: gasneti_unreachable();                                               \
    }                                                                               \
  } while (0)

#define GASNETE_GET_DEGEN(retval, synctype, tm, dstaddr, rank, srcaddr, nbytes, flags) do { \
    gasneti_assert((nbytes) > 0);                                                   \
    gasneti_boundscheck_allowoutseg((tm), (rank), (srcaddr), (nbytes));             \
    switch (synctype) {                                                             \
      case gasnete_synctype_nb:                                                     \
        (retval) = _gex_RMA_GetNB ((tm), (dstaddr), (rank), (srcaddr), (nbytes),    \
                                          (flags) GASNETE_THREAD_PASS);             \
        break;                                                                      \
      case gasnete_synctype_nbi:                                                    \
        (retval) = (gex_Event_t)(intptr_t)                                          \
                   _gex_RMA_GetNBI((tm), (dstaddr), (rank), (srcaddr), (nbytes),    \
                                          (flags) GASNETE_THREAD_PASS);             \
        break;                                                                      \
      case gasnete_synctype_b:                                                      \
        (retval) = (gex_Event_t)(intptr_t)                                          \
              _gex_RMA_GetBlocking((tm), (dstaddr), (rank), (srcaddr), (nbytes),    \
                                          (flags) GASNETE_THREAD_PASS);             \
        break;                                                                      \
      default: gasneti_unreachable();                                               \
    }                                                                               \
  } while (0)

/*---------------------------------------------------------------------------------*/
/* packing/unpacking helpers */
#define _GASNETE_PACK_HELPER(packed, unpacked, sz) \
        GASNETE_FAST_UNALIGNED_MEMCPY((packed), (unpacked), (sz))
#define _GASNETE_UNPACK_HELPER(packed, unpacked, sz) \
        GASNETE_FAST_UNALIGNED_MEMCPY((unpacked), (packed), (sz))

/*---------------------------------------------------------------------------------*/
/* packetization */
typedef struct {
  size_t firstidx;
  size_t firstoffset;
  size_t lastidx;
  size_t lastlen;
} gasnete_packetdesc_t;

extern void gasnete_packetize_verify(gasnete_packetdesc_t *pt, size_t ptidx, int lastpacket,
                              size_t count, size_t len, gex_Memvec_t const *list);

/*---------------------------------------------------------------------------------*/
// AM helpers
#if PLATFORM_ARCH_32
#define HARGS(c32,c64) c32
#else
#define HARGS(c32,c64) c64
#endif
// GASNETE_VIS_NPAM:
// 0 = Use FP AM
// 1 = Use NP AM with a fixed-payload-size algorithm
// 2 = Use NP AM with a negotiated-payload size
#ifndef GASNETE_VIS_NPAM
#define GASNETE_VIS_NPAM 1
#endif

/*---------------------------------------------------------------------------------*/
/* GASNETE_METAMACRO_ASC/DESC##maxval(fn) is a meta-macro that iteratively expands the fn_INT(x,y) macro 
   with ascending or descending integer arguments. The base case (value zero) is expanded as fn_BASE().
   maxval must be an integer in the range 0..GASNETE_METAMACRO_DEPTH_MAX
   This would be cleaner if we could use recursive macro expansion, but it seems at least gcc disallows
   this - if a macro invocation X(...) is found while expanding a different invocation of X (even with 
   different arguments), the nested invocation is left unexpanded 
*/

#define GASNETE_METAMACRO_DEPTH_MAX 8

#define GASNETE_METAMACRO_ASC0(fn) fn##_BASE()
#define GASNETE_METAMACRO_ASC1(fn) GASNETE_METAMACRO_ASC0(fn) fn##_INT(1,0)
#define GASNETE_METAMACRO_ASC2(fn) GASNETE_METAMACRO_ASC1(fn) fn##_INT(2,1)
#define GASNETE_METAMACRO_ASC3(fn) GASNETE_METAMACRO_ASC2(fn) fn##_INT(3,2)
#define GASNETE_METAMACRO_ASC4(fn) GASNETE_METAMACRO_ASC3(fn) fn##_INT(4,3)
#define GASNETE_METAMACRO_ASC5(fn) GASNETE_METAMACRO_ASC4(fn) fn##_INT(5,4)
#define GASNETE_METAMACRO_ASC6(fn) GASNETE_METAMACRO_ASC5(fn) fn##_INT(6,5)
#define GASNETE_METAMACRO_ASC7(fn) GASNETE_METAMACRO_ASC6(fn) fn##_INT(7,6)
#define GASNETE_METAMACRO_ASC8(fn) GASNETE_METAMACRO_ASC7(fn) fn##_INT(8,7)

#define GASNETE_METAMACRO_DESC0(fn) fn##_BASE()
#define GASNETE_METAMACRO_DESC1(fn) fn##_INT(1,0) GASNETE_METAMACRO_DESC0(fn) 
#define GASNETE_METAMACRO_DESC2(fn) fn##_INT(2,1) GASNETE_METAMACRO_DESC1(fn) 
#define GASNETE_METAMACRO_DESC3(fn) fn##_INT(3,2) GASNETE_METAMACRO_DESC2(fn) 
#define GASNETE_METAMACRO_DESC4(fn) fn##_INT(4,3) GASNETE_METAMACRO_DESC3(fn) 
#define GASNETE_METAMACRO_DESC5(fn) fn##_INT(5,4) GASNETE_METAMACRO_DESC4(fn) 
#define GASNETE_METAMACRO_DESC6(fn) fn##_INT(6,5) GASNETE_METAMACRO_DESC5(fn) 
#define GASNETE_METAMACRO_DESC7(fn) fn##_INT(7,6) GASNETE_METAMACRO_DESC6(fn) 
#define GASNETE_METAMACRO_DESC8(fn) fn##_INT(8,7) GASNETE_METAMACRO_DESC7(fn) 

// Extended variant that also threads three arbitrary arguments though the expansion chain
#define GASNETE_METAMACRO3_ASC0(fn,a1,a2,a3) fn##_BASE(a1,a2,a3)
#define GASNETE_METAMACRO3_ASC1(fn,a1,a2,a3) GASNETE_METAMACRO3_ASC0(fn,a1,a2,a3) fn##_INT(1,0,a1,a2,a3)
#define GASNETE_METAMACRO3_ASC2(fn,a1,a2,a3) GASNETE_METAMACRO3_ASC1(fn,a1,a2,a3) fn##_INT(2,1,a1,a2,a3)
#define GASNETE_METAMACRO3_ASC3(fn,a1,a2,a3) GASNETE_METAMACRO3_ASC2(fn,a1,a2,a3) fn##_INT(3,2,a1,a2,a3)
#define GASNETE_METAMACRO3_ASC4(fn,a1,a2,a3) GASNETE_METAMACRO3_ASC3(fn,a1,a2,a3) fn##_INT(4,3,a1,a2,a3)
#define GASNETE_METAMACRO3_ASC5(fn,a1,a2,a3) GASNETE_METAMACRO3_ASC4(fn,a1,a2,a3) fn##_INT(5,4,a1,a2,a3)
#define GASNETE_METAMACRO3_ASC6(fn,a1,a2,a3) GASNETE_METAMACRO3_ASC5(fn,a1,a2,a3) fn##_INT(6,5,a1,a2,a3)
#define GASNETE_METAMACRO3_ASC7(fn,a1,a2,a3) GASNETE_METAMACRO3_ASC6(fn,a1,a2,a3) fn##_INT(7,6,a1,a2,a3)
#define GASNETE_METAMACRO3_ASC8(fn,a1,a2,a3) GASNETE_METAMACRO3_ASC7(fn,a1,a2,a3) fn##_INT(8,7,a1,a2,a3)

/*---------------------------------------------------------------------------------*/

#if PLATFORM_COMPILER_SUN_C
  /* disable a harmless warning */
  #pragma error_messages(off, E_STATEMENT_NOT_REACHED)
#endif

#endif

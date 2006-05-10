/*   $Source: /Users/kamil/work/gasnet-cvs2/gasnet/extended-ref/gasnet_vis_internal.h,v $
 *     $Date: 2006/05/10 06:53:29 $
 * $Revision: 1.15.4.9 $
 * Description: Reference implemetation of GASNet Vector, Indexed & Strided
 * Copyright 2002, Dan Bonachea <bonachea@cs.berkeley.edu>
 * Terms of use are as specified in license.txt
 */

#ifndef GASNETI_GASNET_EXTENDED_VIS_C
  #error This file not meant to be compiled directly - included by gasnet_extended.c
#endif

#include <gasnet_vis.h>

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

/* GASNETE_LOOPING_DIMS: first level of strided performance:
  number of non-trivial striding dimensions to support using an N deep loop nest 
*/
#ifndef GASNETE_LOOPING_DIMS
  #define GASNETE_LOOPING_DIMS 8
#endif

#if defined(__HP_cc) && GASNETE_LOOPING_DIMS > 7
  /* avoid bugs in HP C preprocessor */
  #undef GASNETE_LOOPING_DIMS
  #define GASNETE_LOOPING_DIMS 7  
#elif defined(_CRAYC) && GASNETE_LOOPING_DIMS > 4
  /* avoid bugs in Cray C compiler */
  #undef GASNETE_LOOPING_DIMS
  #define GASNETE_LOOPING_DIMS 4  
#elif defined(__DECC) && GASNETE_LOOPING_DIMS > 4
  /* avoid bugs in Compaq C optimizer */
  #undef GASNETE_LOOPING_DIMS
  #define GASNETE_LOOPING_DIMS 4 
#endif

/* GASNETE_DIRECT_DIMS: second level of strided performance:
  number of non-trivial striding dimensions to support using statically allocated metadata 
  (only affects the operation of requests with non-trivial dimensions > GASNETE_LOOPING_DIMS)
*/
#ifndef GASNETE_DIRECT_DIMS
#define GASNETE_DIRECT_DIMS 15
#endif

/* GASNETE_RANDOM_SELECTOR: use random VIS algorithm selection 
  (mostly useful for correctness debugging) */
#ifndef GASNETE_RANDOM_SELECTOR
#define GASNETE_RANDOM_SELECTOR 0
#endif

#ifndef GASNETE_USE_REMOTECONTIG_GATHER_SCATTER
  #if GASNETI_HAVE_EOP_INTERFACE
    #define GASNETE_USE_REMOTECONTIG_GATHER_SCATTER 1
  #else
    #define GASNETE_USE_REMOTECONTIG_GATHER_SCATTER 0
  #endif
#endif

#ifndef GASNETE_USE_AMPIPELINE
  #if GASNETI_HAVE_EOP_INTERFACE
    #define GASNETE_USE_AMPIPELINE 1
  #else
    #define GASNETE_USE_AMPIPELINE 0
  #endif
#endif

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

gasnete_vis_threaddata_t *gasnete_vis_new_threaddata(void) {
  gasnete_vis_threaddata_t *result = gasneti_calloc(1,sizeof(*result));
  #ifdef GASNETE_VIS_THREADDATA_EXTRA_INIT
    GASNETE_VIS_THREADDATA_EXTRA_INIT(result)
  #endif
  return result;
}

#define GASNETE_VIS_MYTHREAD (GASNETE_MYTHREAD->gasnete_vis_threaddata ? \
        GASNETE_MYTHREAD->gasnete_vis_threaddata :                       \
        (GASNETE_MYTHREAD->gasnete_vis_threaddata = gasnete_vis_new_threaddata()))

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
        return GASNET_INVALID_HANDLE; /* avoid warning on MIPSPro */                  \
    }                                                                                 \
  } while(0)

#define GASNETE_PUT_INDIV(islocal, dstnode, dstaddr, srcaddr, nbytes) do {      \
    gasneti_assert(nbytes > 0);                                                 \
    gasneti_boundscheck_allowoutseg(dstnode, dstaddr, nbytes);                  \
    gasneti_assert(islocal == (dstnode == gasneti_mynode));                     \
    if (islocal) GASNETE_FAST_UNALIGNED_MEMCPY((dstaddr), (srcaddr), (nbytes)); \
    else gasnete_put_nbi_bulk((dstnode), (dstaddr), (srcaddr), (nbytes)         \
                                GASNETE_THREAD_PASS);                           \
  } while (0)

#define GASNETE_GET_INDIV(islocal, dstaddr, srcnode, srcaddr, nbytes) do {      \
    gasneti_assert(nbytes > 0);                                                 \
    gasneti_boundscheck_allowoutseg(srcnode, srcaddr, nbytes);                  \
    gasneti_assert(islocal == (srcnode == gasneti_mynode));                     \
    if (islocal) GASNETE_FAST_UNALIGNED_MEMCPY((dstaddr), (srcaddr), (nbytes)); \
    else gasnete_get_nbi_bulk((dstaddr), (srcnode), (srcaddr), (nbytes)         \
                                GASNETE_THREAD_PASS);                           \
  } while (0)

#if GASNETI_HAVE_EOP_INTERFACE
/* create a dummy eop/iop based on synctype, save it in visop */
#define GASNETE_VISOP_SETUP(visop, synctype, isget) do {          \
    if (synctype == gasnete_synctype_nbi) {                           \
      visop->eop = NULL;                                              \
      visop->iop = gasneti_iop_register(1,isget GASNETE_THREAD_PASS); \
    } else {                                                          \
      visop->eop = gasneti_eop_create(GASNETE_THREAD_PASS_ALONE);     \
      visop->iop = NULL;                                              \
    }                                                                 \
} while (0)

#define GASNETE_VISOP_RETURN(visop, synctype) do {                   \
    switch (synctype) {                                              \
      case gasnete_synctype_b: {                                     \
        gasnet_handle_t h = gasneti_eop_to_handle(visop->eop);       \
        gasnete_wait_syncnb(h);                                      \
        return GASNET_INVALID_HANDLE;                                \
      }                                                              \
      case gasnete_synctype_nb:                                      \
        return gasneti_eop_to_handle(visop->eop);                    \
      case gasnete_synctype_nbi:                                     \
        return GASNET_INVALID_HANDLE;                                \
      default: gasneti_fatalerror("bad synctype");                   \
        return GASNET_INVALID_HANDLE; /* avoid warning on MIPSPro */ \
    }                                                                \
} while (0)

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

#ifdef __SUNPRO_C
  /* disable a harmless warning */
  #pragma error_messages(off, E_STATEMENT_NOT_REACHED)
#endif

/*---------------------------------------------------------------------------------*/
/* packing/unpacking helpers */
/* TODO: add max byte length? */
#define _GASNETE_PACK_HELPER(packed, unpacked, sz) \
        GASNETE_FAST_UNALIGNED_MEMCPY((packed), (unpacked), (sz))
#define _GASNETE_UNPACK_HELPER(packed, unpacked, sz) \
        GASNETE_FAST_UNALIGNED_MEMCPY((unpacked), (packed), (sz))
#define _GASNETE_MEMVEC_PACK(copy) {                               \
  size_t i;                                                        \
  uint8_t *ploc = (uint8_t *)buf;                                  \
  gasneti_assert(count > 0 && list && buf);                        \
  if (last_len == (size_t)-1) last_len = list[count-1].len;        \
  if (count == 1) {                                                \
    copy(ploc, ((uint8_t*)list[0].addr)+first_offset, last_len);   \
    ploc += last_len;                                              \
  } else {                                                         \
    if (first_offset < list[0].len) { /* len might be zero */      \
      size_t const firstlen = list[0].len - first_offset;          \
      copy(ploc, ((uint8_t*)list[0].addr)+first_offset, firstlen); \
      ploc += firstlen;                                            \
    } else gasneti_assert(list[0].len == 0);                       \
    for (i = 1; i < count-1; i++) {                                \
      size_t const len = list[i].len;                              \
      if (len > 0) {                                               \
        copy(ploc, list[i].addr, len);                             \
        ploc += len;                                               \
      }                                                            \
    }                                                              \
    copy(ploc, list[count-1].addr, last_len);                      \
    ploc += last_len;                                              \
  }                                                                \
  return ploc;                                                     \
}
#define _GASNETE_ADDRLIST_PACK(copy) {                                       \
  size_t i;                                                                  \
  uint8_t *ploc = (uint8_t *)buf;                                            \
  gasneti_assert(count > 0 && list && len > 0 && buf && first_offset < len); \
  if (last_len == (size_t)-1) last_len = len;                                \
  if (count == 1) {                                                          \
    copy(ploc, ((uint8_t*)list[0])+first_offset, last_len);                  \
    ploc += last_len;                                                        \
  } else {                                                                   \
    size_t firstlen = len - first_offset;                                    \
    copy(ploc, ((uint8_t*)list[0])+first_offset, firstlen);                  \
    ploc += firstlen;                                                        \
    for (i = 1; i < count-1; i++) {                                          \
      copy(ploc, list[i], len);                                              \
      ploc += len;                                                           \
    }                                                                        \
    copy(ploc, list[count-1], last_len);                                     \
    ploc += last_len;                                                        \
  }                                                                          \
  return ploc;                                                               \
}

/* pack a memvec list into a contiguous buffer, using the provided byte offset into the first memvec
   if last_len is (size_t)-1, then last_len is ignored
   otherwise, last_len is used in place of the last memvec length 
     (and is never adjusted based on first_offset, even if count == 1)
   return a pointer into the packed buffer, which points just after the last byte used
 */
void *gasnete_memvec_pack(size_t count, gasnet_memvec_t const *list, void *buf,
                           size_t first_offset, size_t last_len) _GASNETE_MEMVEC_PACK(_GASNETE_PACK_HELPER)
void *gasnete_memvec_unpack(size_t count, gasnet_memvec_t const *list, void const *buf,
                             size_t first_offset, size_t last_len) _GASNETE_MEMVEC_PACK(_GASNETE_UNPACK_HELPER)

/* pack a addrlist list into a contiguous buffer, using the provided byte offset into the first element
   if last_len is (size_t)-1, then last_len is ignored
   otherwise, last_len is used in place of len for the last entry
     (and is never adjusted based on first_offset, even if count == 1)
   return a pointer into the packed buffer, which points just after the last byte used
*/
void *gasnete_addrlist_pack(size_t count, void * const list[], size_t len, void *buf, 
                           size_t first_offset, size_t last_len) _GASNETE_ADDRLIST_PACK(_GASNETE_PACK_HELPER)
void *gasnete_addrlist_unpack(size_t count, void * const list[], size_t len, void const *buf, 
                             size_t first_offset, size_t last_len) _GASNETE_ADDRLIST_PACK(_GASNETE_UNPACK_HELPER)

/*---------------------------------------------------------------------------------*/
/* ***  Vector *** */
/*---------------------------------------------------------------------------------*/

/* simple gather put, remotely contiguous */
#ifndef GASNETE_PUTV_GATHER_SELECTOR
#if GASNETE_USE_REMOTECONTIG_GATHER_SCATTER
gasnet_handle_t gasnete_putv_gather(gasnete_synctype_t synctype,
                                   gasnet_node_t dstnode,
                                   size_t dstcount, gasnet_memvec_t const dstlist[], 
                                   size_t srccount, gasnet_memvec_t const srclist[] GASNETE_THREAD_FARG) {
  gasnete_vis_threaddata_t * const td = GASNETE_VIS_MYTHREAD;
  size_t const nbytes = dstlist[0].len;
  gasneti_assert(dstcount == 1 && srccount > 1); /* only supports gather put */
  gasneti_assert(dstnode != gasneti_mynode); /* silly to use for local cases */
  if_pf (nbytes == 0) return GASNET_INVALID_HANDLE; /* handle empty */
  GASNETI_TRACE_EVENT(C, PUTV_GATHER);

  { gasneti_vis_op_t * const visop = gasneti_malloc(sizeof(gasneti_vis_op_t)+nbytes);
    void * const packedbuf = visop + 1;
    gasnete_memvec_pack(srccount, srclist, packedbuf, 0, (size_t)-1);
    visop->type = GASNETI_VIS_CAT_PUTV_GATHER;
    visop->handle = gasnete_put_nb_bulk(dstnode, dstlist[0].addr, packedbuf, nbytes GASNETE_THREAD_PASS);
    GASNETE_PUSH_VISOP_RETURN(td, visop, synctype, 0);
  }
}
  #define GASNETE_PUTV_GATHER_SELECTOR(synctype,dstnode,dstcount,dstlist,srccount,srclist) \
    if (dstcount == 1 && srccount > 1)                                                     \
      return gasnete_putv_gather(synctype,dstnode,dstcount,dstlist,srccount,srclist GASNETE_THREAD_PASS)
#else
  #define GASNETE_PUTV_GATHER_SELECTOR(synctype,dstnode,dstcount,dstlist,srccount,srclist) ((void)0)
#endif
#endif

/* simple scatter get, remotely contiguous */
#ifndef GASNETE_GETV_SCATTER_SELECTOR
#if GASNETE_USE_REMOTECONTIG_GATHER_SCATTER
gasnet_handle_t gasnete_getv_scatter(gasnete_synctype_t synctype,
                                   size_t dstcount, gasnet_memvec_t const dstlist[], 
                                   gasnet_node_t srcnode,
                                   size_t srccount, gasnet_memvec_t const srclist[] GASNETE_THREAD_FARG) {
  gasnete_vis_threaddata_t * const td = GASNETE_VIS_MYTHREAD;
  size_t const nbytes = srclist[0].len;
  gasneti_assert(srccount == 1 && dstcount > 1); /* only supports scatter get */
  gasneti_assert(srcnode != gasneti_mynode); /* silly to use for local cases */
  if_pf (nbytes == 0) return GASNET_INVALID_HANDLE; /* handle empty */
  GASNETI_TRACE_EVENT(C, GETV_SCATTER);

  { gasneti_vis_op_t * const visop = gasneti_malloc(sizeof(gasneti_vis_op_t)+dstcount*sizeof(gasnet_memvec_t)+nbytes);
    gasnet_memvec_t * const savedlst = (gasnet_memvec_t *)(visop + 1);
    void * const packedbuf = savedlst + dstcount;
    memcpy(savedlst, dstlist, dstcount*sizeof(gasnet_memvec_t));
    visop->type = GASNETI_VIS_CAT_GETV_SCATTER;
    visop->count = dstcount;
    visop->handle = gasnete_get_nb_bulk(packedbuf, srcnode, srclist[0].addr, nbytes GASNETE_THREAD_PASS);
    GASNETE_PUSH_VISOP_RETURN(td, visop, synctype, 1);
  }
}
  #define GASNETE_GETV_SCATTER_SELECTOR(synctype,dstcount,dstlist,srcnode,srccount,srclist) \
    if (srccount == 1 && dstcount > 1)                                                      \
      return gasnete_getv_scatter(synctype,dstcount,dstlist,srcnode,srccount,srclist GASNETE_THREAD_PASS)
#else
  #define GASNETE_GETV_SCATTER_SELECTOR(synctype,dstcount,dstlist,srcnode,srccount,srclist) ((void)0)
#endif
#endif
/*---------------------------------------------------------------------------------*/

typedef struct {
  size_t firstidx;
  size_t firstoffset;
  size_t lastidx;
  size_t lastlen;
} gasnete_packetdesc_t;

static void gasnete_packetize_verify(gasnete_packetdesc_t *pt, size_t ptidx, int lastpacket,
                              size_t count, size_t len, gasnet_memvec_t const *list) {
  size_t firstidx = pt[ptidx].firstidx;
  size_t firstoffset = pt[ptidx].firstoffset;
  size_t lastidx = pt[ptidx].lastidx;
  size_t lastlen = pt[ptidx].lastlen;
  size_t entries = lastidx - firstidx + 1;
  gasneti_assert(firstidx <= lastidx);
  gasneti_assert(lastidx < count);
  if (ptidx == 0) gasneti_assert(firstidx == 0 && firstoffset == 0); /* first packet */
  else if (firstidx == lastidx && lastlen == 0) ; /* empty local packet */
  else if (firstidx == pt[ptidx-1].lastidx) { /* continued from last packet */
    gasneti_assert(firstoffset > 0 && firstoffset < (list?list[firstidx].len:len));
    if (pt[ptidx-1].lastidx == pt[ptidx-1].firstidx)
      gasneti_assert(firstoffset == pt[ptidx-1].lastlen+pt[ptidx-1].firstoffset);
    else
      gasneti_assert(firstoffset == pt[ptidx-1].lastlen);
  } else { /* packet starts a new entry */
    gasneti_assert(firstidx == pt[ptidx-1].lastidx + 1);
    gasneti_assert(firstoffset == 0);
    if (pt[ptidx-1].lastidx == pt[ptidx-1].firstidx)
      gasneti_assert(pt[ptidx-1].lastlen == (list?list[firstidx-1].len:len)-pt[ptidx-1].firstoffset);
    else
      gasneti_assert(pt[ptidx-1].lastlen == (list?list[firstidx-1].len:len));
  }
  if (lastpacket) {
    if (lastidx == firstidx) {
      if (lastlen == 0) ; /* empty local packet */
      else gasneti_assert(lastlen == (list?list[lastidx].len:len)-firstoffset);
    }
    else gasneti_assert(lastlen == (list?list[lastidx].len:len));
  }
}

/* Packetizes remotelist into a list of gasnete_packetdesc_t entries based on maxpayload packet size
     sharedpacket  => metadata and corresponding data travel together in unified packets (put)
                      so that for each packet i: datasz_i + metadatasz_i <= maxpayload
     !sharedpacket => metadata and corresponding data travel in separate packets (get)
                      so that for each packet i: MAX(datasz_i,metadatasz_i) <= maxpayload
   A local packet table is also computed to match the remote packetization boundaries of the data
     on a byte-for-byte basis
   Allocates and populates the plocalpt and premotept arrays with the packetization information
   Returns the number of packets described by the resulting plocalpt and premotept arrays
 */
size_t gasnete_packetize_memvec(size_t remotecount, gasnet_memvec_t const remotelist[],
                                size_t localcount, gasnet_memvec_t const locallist[],
                                gasnete_packetdesc_t **premotept,
                                gasnete_packetdesc_t **plocalpt,
                                size_t maxpayload, int sharedpacket) {
  size_t ptidx;
  int done = 0;
  size_t ridx = 0, roffset = 0, lidx = 0, loffset = 0;
  size_t const metadatasz = sizeof(gasnet_memvec_t);
  size_t ptsz = 4; /* initial size guess - no fast way to know for sure */
  gasnete_packetdesc_t *remotept = gasneti_malloc(ptsz*sizeof(gasnete_packetdesc_t));
  gasnete_packetdesc_t *localpt = gasneti_malloc(ptsz*sizeof(gasnete_packetdesc_t));
  gasneti_assert(premotept && plocalpt && remotecount && localcount);
  gasneti_assert(gasnete_memveclist_totalsz(remotecount,remotelist) == 
                 gasnete_memveclist_totalsz(localcount,locallist));

  for (ptidx = 0; ; ptidx++) {
    ssize_t packetremain = maxpayload;
    ssize_t packetdata = 0;
    size_t rdatasz, ldatasz; 

    if (ptidx == ptsz) { /* grow the packet tables */
      ptsz *= 2;
      remotept = gasneti_realloc(remotept, ptsz*sizeof(gasnete_packetdesc_t));
      localpt = gasneti_realloc(localpt, ptsz*sizeof(gasnete_packetdesc_t));
    }

    /* begin remote packet */
    remotept[ptidx].firstidx = ridx;
    remotept[ptidx].firstoffset = roffset;
    /* begin local packet */
    if_pf (lidx == localcount) localpt[ptidx].firstidx = lidx-1; /* might happen if remote has trailing empties */
    else                       localpt[ptidx].firstidx = lidx;
    localpt[ptidx].firstoffset = loffset;

    while (packetremain > metadatasz) { /* room for more entries */
      gasneti_assert(roffset < remotelist[ridx].len || (remotelist[ridx].len == 0 && roffset == 0));
      rdatasz = remotelist[ridx].len - roffset; /* data left in current entry */
      /* try to add the entire entry to packet */
      if (sharedpacket) packetremain -= (metadatasz + rdatasz);
      else              packetremain -= MAX(metadatasz, rdatasz);
      if (packetremain < 0) { /* overflowed - finished a packet, and spill to next */
        rdatasz += packetremain; /* compute truncated datasz that fits in this packet */
        roffset += rdatasz; /* update offset into current entry */
        packetdata += rdatasz;
        break;
      } else {
        packetdata += rdatasz;
        roffset = 0; /* finished an entry */
        ridx++;
        if (ridx == remotecount) { done = 1; break; } /* done - this is last packet */
      }
    }
    /* end remote packet */
    if (roffset == 0) remotept[ptidx].lastidx = ridx-1;
    else              remotept[ptidx].lastidx = ridx;
    remotept[ptidx].lastlen = rdatasz;

    #if GASNET_DEBUG /* verify packing properties */
      gasnete_packetize_verify(remotept, ptidx, done, remotecount, 0, remotelist);
      { size_t datachk = 0, i;
        size_t entries = remotept[ptidx].lastidx - remotept[ptidx].firstidx + 1;
        for (i = remotept[ptidx].firstidx; i <= remotept[ptidx].lastidx; i++) {
          if (i == remotept[ptidx].lastidx) datachk += remotept[ptidx].lastlen;
          else if (i == remotept[ptidx].firstidx) datachk += (remotelist[i].len - remotept[ptidx].firstoffset);
          else datachk += remotelist[i].len;
        }
        gasneti_assert(packetdata == datachk);
        if (sharedpacket) { 
          gasneti_assert((metadatasz*entries + packetdata) <= maxpayload); /* not overfull */
          gasneti_assert(((metadatasz*entries + packetdata) >= maxpayload - metadatasz) || done); /* not underfull */
        } else {
          gasneti_assert(MAX(metadatasz*entries,packetdata) <= maxpayload); /* not overfull */
          /* algorithm currently may underfill for !sharedpacket, because it effectively always 
             subtracts the MAX(metadatasz, datasz) from *both* packets being managed simultaneously in packetremain,
             rather than maintaining independent packetremains and updating each accordingly (increasing arithmetic complexity)
             In vectors whose entries are dominated by datasz or metadatasz, the effect should be neglible
             In perverse cases we might end up with a packet which where the maximal packet is only 2/3 full
             this means in datasz dominated vectors with a few entries where datasz < metadatasz (or vice-versa)
           */
          gasneti_assert((MAX(metadatasz*entries,packetdata) >= (maxpayload - metadatasz)/2) || done); /* not underfull */
        }
      }
    #endif

    ldatasz = 0;
    while (packetdata > 0 || (lidx < localcount && locallist[lidx].len == 0)) {
      gasneti_assert(loffset < locallist[lidx].len || (locallist[lidx].len == 0 && loffset == 0));
      ldatasz = locallist[lidx].len - loffset; /* data left in current entry */
      packetdata -= ldatasz;
      if (packetdata < 0) { /* overflowed - this entry spills into next packet */
        ldatasz += packetdata; /* compute truncated datasz that fits in this packet */
        loffset += ldatasz; /* update offset into current entry */
        break;
      } else {
        loffset = 0; /* finished an entry */
        lidx++;
      }
    }
    /* end local packet */
    if (loffset == 0) localpt[ptidx].lastidx = lidx-1;
    else              localpt[ptidx].lastidx = lidx;
    localpt[ptidx].lastlen = ldatasz;

    #if GASNET_DEBUG /* verify packing properties */
      gasnete_packetize_verify(localpt, ptidx, done, localcount, 0, locallist);
    #endif

    if (done) {
      gasneti_assert(ridx == remotecount && roffset == 0 && lidx == localcount && loffset == 0);
      *premotept = remotept;
      *plocalpt = localpt;
      return ptidx+1;
    }
  }
}
/*---------------------------------------------------------------------------------*/
size_t gasnete_packetize_addrlist(size_t remotecount, size_t remotelen,
                                  size_t localcount, size_t locallen,
                                  gasnete_packetdesc_t **premotept,
                                  gasnete_packetdesc_t **plocalpt,
                                  size_t maxpayload, int sharedpacket) {
  size_t ptidx;
  int done = 0;
  size_t ridx = 0, roffset = 0, lidx = 0, loffset = 0;
  size_t const metadatasz = sizeof(void *);
  size_t const runit = (sharedpacket ? metadatasz + remotelen : MAX(metadatasz,remotelen));
  size_t ptsz = (runit <= maxpayload ? /* conservative upper bound on packet count */
                 remotecount / (maxpayload / runit) + 1 : 
                 remotelen*remotecount / (maxpayload - 2*metadatasz) + 1); 
  gasnete_packetdesc_t *remotept = gasneti_malloc(ptsz*sizeof(gasnete_packetdesc_t));
  gasnete_packetdesc_t *localpt = gasneti_malloc(ptsz*sizeof(gasnete_packetdesc_t));
  gasneti_assert(premotept && plocalpt && remotecount && remotelen && localcount && locallen);
  gasneti_assert(remotecount*remotelen == localcount*locallen);
  gasneti_assert(remotecount*remotelen > 0);

  for (ptidx = 0; ; ptidx++) {
    ssize_t packetremain = maxpayload;
    ssize_t packetdata = 0;
    size_t rdatasz, ldatasz; 

    gasneti_assert(ptidx < ptsz);

    /* begin remote packet */
    remotept[ptidx].firstidx = ridx;
    remotept[ptidx].firstoffset = roffset;
    /* begin local packet */
    if_pf (lidx == localcount) localpt[ptidx].firstidx = lidx-1; 
    else                       localpt[ptidx].firstidx = lidx;
    localpt[ptidx].firstoffset = loffset;

    if (roffset > 0) { /* initial partial entry */
      gasneti_assert(roffset < remotelen);
      rdatasz = remotelen - roffset; /* data left in current entry */
      /* try to add the entire entry to packet */
      if (sharedpacket) packetremain -= (metadatasz + rdatasz);
      else              packetremain -= MAX(metadatasz, rdatasz);
      if (packetremain < 0) { /* overflowed - finished a packet, and spill to next */
        rdatasz += packetremain; /* compute truncated datasz that fits in this packet */
        roffset += rdatasz; /* update offset into current entry */
        packetdata += rdatasz;
        goto rend;
      } else {
        packetdata += rdatasz;
        roffset = 0; /* finished an entry */
        ridx++;
        if (ridx == remotecount) { done = 1; goto rend; } /* done - this is last packet */
      }
    }
    if (packetremain >= runit) { /* whole entries */
      size_t numunits = packetremain / runit;
      if (ridx + numunits > remotecount) numunits = remotecount - ridx;
      rdatasz = remotelen;
      packetremain -= runit*numunits;
      packetdata += remotelen*numunits;
      ridx += numunits;
      gasneti_assert(roffset == 0);
      if (ridx == remotecount) { done = 1; goto rend; } /* done - this is last packet */
    }
    if (packetremain > metadatasz) { /* trailing partial entry */
      gasneti_assert(packetremain < runit);
      if (sharedpacket) rdatasz = packetremain - metadatasz;
      else              rdatasz = packetremain;
      packetdata += rdatasz;
      roffset = rdatasz;
    }
    rend:
    /* end remote packet */
    if (roffset == 0) remotept[ptidx].lastidx = ridx-1;
    else              remotept[ptidx].lastidx = ridx;
    remotept[ptidx].lastlen = rdatasz;

    #if GASNET_DEBUG /* verify packing properties */
      gasnete_packetize_verify(remotept, ptidx, done, remotecount, remotelen, 0);
      { size_t datachk = 0, i;
        size_t entries = remotept[ptidx].lastidx - remotept[ptidx].firstidx + 1;
        for (i = remotept[ptidx].firstidx; i <= remotept[ptidx].lastidx; i++) {
          if (i == remotept[ptidx].lastidx) datachk += remotept[ptidx].lastlen;
          else if (i == remotept[ptidx].firstidx) datachk += (remotelen - remotept[ptidx].firstoffset);
          else datachk += remotelen;
        }
        gasneti_assert(packetdata == datachk);
        if (sharedpacket) { 
          gasneti_assert((metadatasz*entries + packetdata) <= maxpayload); /* not overfull */
          gasneti_assert(((metadatasz*entries + packetdata) >= maxpayload - metadatasz) || done); /* not underfull */
        } else {
          gasneti_assert(MAX(metadatasz*entries,packetdata) <= maxpayload); /* not overfull */
          gasneti_assert((MAX(metadatasz*entries,packetdata) >= maxpayload - 2*metadatasz) || done); /* not underfull */
        }
      }
    #endif

    ldatasz = 0;
    if (loffset > 0) { /* initial partial entry */
      gasneti_assert(loffset < locallen);
      ldatasz = locallen - loffset; /* data left in current entry */
      packetdata -= ldatasz;
      if (packetdata < 0) { /* overflowed - this entry spills into next packet */
        ldatasz += packetdata; /* compute truncated datasz that fits in this packet */
        loffset += ldatasz; /* update offset into current entry */
        packetdata = 0;
      } else {
        loffset = 0; /* finished an entry */
        lidx++;
        gasneti_assert(lidx < localcount || (lidx == localcount && packetdata == 0));
      }
    }
    if (packetdata >= locallen) { /* whole entries */
      size_t numunits = packetdata / locallen;
      if (lidx + numunits > localcount) numunits = localcount - lidx;
      ldatasz = locallen;
      packetdata -= locallen*numunits;
      lidx += numunits;
      gasneti_assert(lidx < localcount || (lidx == localcount && packetdata == 0));
      gasneti_assert(loffset == 0);
    }
    if (packetdata > 0) { /* trailing partial entry */
      gasneti_assert(packetdata < locallen);
      ldatasz = packetdata;
      loffset = ldatasz;
    }
    /* end local packet */
    if (loffset == 0) localpt[ptidx].lastidx = lidx-1;
    else              localpt[ptidx].lastidx = lidx;
    localpt[ptidx].lastlen = ldatasz;

    #if GASNET_DEBUG /* verify packing properties */
      gasnete_packetize_verify(localpt, ptidx, done, localcount, locallen, 0);
    #endif

    if (done) {
      gasneti_assert(ridx == remotecount && roffset == 0 && lidx == localcount && loffset == 0);
      *premotept = remotept;
      *plocalpt = localpt;
      return ptidx+1;
    }
  }
}
/*---------------------------------------------------------------------------------*/
/* Pipelined AM gather-scatter put */
#ifndef GASNETE_PUTV_AMPIPELINE_SELECTOR
#if GASNETE_USE_AMPIPELINE
gasnet_handle_t gasnete_putv_AMPipeline(gasnete_synctype_t synctype,
                                   gasnet_node_t dstnode,
                                   size_t dstcount, gasnet_memvec_t const dstlist[], 
                                   size_t srccount, gasnet_memvec_t const srclist[] GASNETE_THREAD_FARG) {
  gasneti_assert(dstcount > 1); /* supports scatter put */
  gasneti_assert(dstnode != gasneti_mynode); /* silly to use for local cases */
  GASNETI_TRACE_EVENT(C, PUTV_AMPIPELINE);
  { size_t i; /* detect empty list */
    for (i = 0; i < srccount; i++) { 
      if (srclist[i].len > 0) break;
    }
    if_pf (i == srccount) return GASNET_INVALID_HANDLE;
  }
  GASNETE_START_NBIREGION(synctype, 0);

  { gasnet_memvec_t * const packedbuf = gasneti_malloc(gasnet_AMMaxMedium());
    gasnete_packetdesc_t *remotept;
    gasnete_packetdesc_t *localpt;
    size_t packetidx;
    size_t const packetcnt = gasnete_packetize_memvec(dstcount, dstlist, srccount, srclist, 
                                                &remotept, &localpt, gasnet_AMMaxMedium(), 1);
    gasneti_iop_t *iop = gasneti_iop_register(packetcnt,0 GASNETE_THREAD_PASS);

    for (packetidx = 0; packetidx < packetcnt; packetidx++) {
      gasnete_packetdesc_t * const rpacket = &remotept[packetidx];
      gasnete_packetdesc_t * const lpacket = &localpt[packetidx];
      size_t const rnum = rpacket->lastidx - rpacket->firstidx + 1;
      size_t const lnum = lpacket->lastidx - lpacket->firstidx + 1;
      uint8_t *end;
      /* fill packet with remote metadata */
      memcpy(packedbuf, &dstlist[rpacket->firstidx], rnum*sizeof(gasnet_memvec_t));
      if (rpacket->firstoffset) {
        packedbuf[0].addr = ((uint8_t *)packedbuf[0].addr) + rpacket->firstoffset;
        packedbuf[0].len -= rpacket->firstoffset;
      }
      packedbuf[rnum-1].len = rpacket->lastlen;
      /* gather data payload from sourcelist into packet */
      end = gasnete_memvec_pack(lnum, &srclist[lpacket->firstidx], &packedbuf[rnum], 
                                lpacket->firstoffset, lpacket->lastlen);

      /* send AM(rnum, iop) from packedbuf */
      GASNETI_SAFE(
        MEDIUM_REQ(2,3,(dstnode, gasneti_handleridx(gasnete_putv_AMPipeline_reqh),
                      packedbuf, end - (uint8_t *)packedbuf,
                      PACK(iop), rnum)));
    }

    gasneti_free(remotept);
    gasneti_free(localpt);
    gasneti_free(packedbuf);
    GASNETE_END_NBIREGION_AND_RETURN(synctype, 0);
  }
}
  #define GASNETE_PUTV_AMPIPELINE_SELECTOR(synctype,dstnode,dstcount,dstlist,srccount,srclist) \
    if (dstcount > 1)                                      \
      return gasnete_putv_AMPipeline(synctype,dstnode,dstcount,dstlist,srccount,srclist GASNETE_THREAD_PASS)
#else
  #define GASNETE_PUTV_AMPIPELINE_SELECTOR(synctype,dstnode,dstcount,dstlist,srccount,srclist) ((void)0)
#endif
#endif
/* ------------------------------------------------------------------------------------ */
#if GASNETE_USE_AMPIPELINE
GASNETI_INLINE(gasnete_putv_AMPipeline_reqh_inner)
void gasnete_putv_AMPipeline_reqh_inner(gasnet_token_t token, 
  void *addr, size_t nbytes,
  void *iop, gasnet_handlerarg_t rnum) {
  gasnet_memvec_t * const rlist = addr;
  uint8_t * const data = (uint8_t *)(&rlist[rnum]);
  uint8_t * const end = gasnete_memvec_unpack(rnum, rlist, data, 0, (size_t)-1);
  gasneti_assert(end - (uint8_t *)addr <= gasnet_AMMaxMedium());
  gasneti_sync_writes();
  /* TODO: coalesce acknowledgements - need a per-srcnode, per-op seqnum & packetcnt */
  GASNETI_SAFE(
    SHORT_REP(1,2,(token, gasneti_handleridx(gasnete_putvis_AMPipeline_reph),
                  PACK(iop))));
}
MEDIUM_HANDLER(gasnete_putv_AMPipeline_reqh,2,4, 
              (token,addr,nbytes, UNPACK(a0),      a1),
              (token,addr,nbytes, UNPACK2(a0, a1), a2));
/* ------------------------------------------------------------------------------------ */
GASNETI_INLINE(gasnete_putvis_AMPipeline_reph_inner)
void gasnete_putvis_AMPipeline_reph_inner(gasnet_token_t token, 
  void *iop) {
  gasneti_iop_markdone(iop, 1, 0);
}
SHORT_HANDLER(gasnete_putvis_AMPipeline_reph,1,2, 
              (token, UNPACK(a0)),
              (token, UNPACK2(a0, a1)));
#endif
/* ------------------------------------------------------------------------------------ */
/* Pipelined AM gather-scatter get */
#ifndef GASNETE_GETV_AMPIPELINE_SELECTOR
#if GASNETE_USE_AMPIPELINE
gasnet_handle_t gasnete_getv_AMPipeline(gasnete_synctype_t synctype,
                                   size_t dstcount, gasnet_memvec_t const dstlist[], 
                                   gasnet_node_t srcnode,
                                   size_t srccount, gasnet_memvec_t const srclist[] GASNETE_THREAD_FARG) {
  gasneti_assert(srccount > 1); /* supports gather get */
  gasneti_assert(srcnode != gasneti_mynode); /* silly to use for local cases */
  GASNETI_TRACE_EVENT(C, GETV_AMPIPELINE);
  { size_t i; /* detect empty list */
    for (i = 0; i < dstcount; i++) { 
      if (dstlist[i].len > 0) break;
    }
    if_pf (i == dstcount) return GASNET_INVALID_HANDLE;
  }

  { gasneti_vis_op_t * const visop = gasneti_malloc(sizeof(gasneti_vis_op_t) +
                                                    dstcount*sizeof(gasnet_memvec_t) + 
                                                    gasnet_AMMaxMedium());
    gasnet_memvec_t * const savedlst = (gasnet_memvec_t *)(visop + 1);
    gasnet_memvec_t * const packedbuf = savedlst + dstcount;
    gasnete_packetdesc_t *remotept;
    gasnete_packetdesc_t *localpt;
    size_t packetidx;
    size_t const packetcnt = gasnete_packetize_memvec(srccount, srclist, dstcount, dstlist,  
                                                &remotept, &localpt, gasnet_AMMaxMedium(), 0);
    GASNETE_VISOP_SETUP(visop, synctype, 1);
    #if GASNET_DEBUG
      visop->type = GASNETI_VIS_CAT_GETV_AMPIPELINE;
      visop->count = dstcount;
    #endif
    gasneti_assert(packetcnt <= GASNETI_ATOMIC_MAX);
    gasneti_assert(packetcnt == (gasnet_handlerarg_t)packetcnt);
    visop->addr = localpt;
    memcpy(savedlst, dstlist, dstcount*sizeof(gasnet_memvec_t));
    gasneti_weakatomic_set(&(visop->packetcnt), packetcnt, GASNETI_ATOMIC_WMB_POST);

    for (packetidx = 0; packetidx < packetcnt; packetidx++) {
      gasnete_packetdesc_t * const rpacket = &remotept[packetidx];
      size_t const rnum = rpacket->lastidx - rpacket->firstidx + 1;
      /* fill packet with remote metadata */
      memcpy(packedbuf, &srclist[rpacket->firstidx], rnum*sizeof(gasnet_memvec_t));
      if (rpacket->firstoffset) {
        packedbuf[0].addr = ((uint8_t *)packedbuf[0].addr) + rpacket->firstoffset;
        packedbuf[0].len -= rpacket->firstoffset;
      }
      packedbuf[rnum-1].len = rpacket->lastlen;

      /* send AM(visop) from packedbuf */
      GASNETI_SAFE(
        MEDIUM_REQ(2,3,(srcnode, gasneti_handleridx(gasnete_getv_AMPipeline_reqh),
                      packedbuf, rnum*sizeof(gasnet_memvec_t),
                      PACK(visop), packetidx)));
    }

    gasneti_free(remotept);
    GASNETE_VISOP_RETURN(visop, synctype);
  }
}
  #define GASNETE_GETV_AMPIPELINE_SELECTOR(synctype,dstcount,dstlist,srcnode,srccount,srclist) \
    if (srccount > 1)                                      \
      return gasnete_getv_AMPipeline(synctype,dstcount,dstlist,srcnode,srccount,srclist GASNETE_THREAD_PASS)
#else
  #define GASNETE_GETV_AMPIPELINE_SELECTOR(synctype,dstcount,dstlist,srcnode,srccount,srclist) ((void)0)
#endif
#endif
/* ------------------------------------------------------------------------------------ */
#if GASNETE_USE_AMPIPELINE
GASNETI_INLINE(gasnete_getv_AMPipeline_reqh_inner)
void gasnete_getv_AMPipeline_reqh_inner(gasnet_token_t token, 
  void *addr, size_t nbytes,
  void *_visop, gasnet_handlerarg_t packetidx) {
  gasnet_memvec_t * const rlist = addr;
  size_t const rnum = nbytes / sizeof(gasnet_memvec_t);
  gasneti_vis_op_t * const visop = _visop;
  uint8_t * const packedbuf = gasneti_malloc(gasnet_AMMaxMedium());
  /* gather data payload from sourcelist into packet */
  uint8_t * const end = gasnete_memvec_pack(rnum, rlist, packedbuf, 0, (size_t)-1);
  size_t const repbytes = end - packedbuf;
  gasneti_assert(repbytes <= gasnet_AMMaxMedium());
  GASNETI_SAFE(
    MEDIUM_REP(2,3,(token, gasneti_handleridx(gasnete_getv_AMPipeline_reph),
                  packedbuf, repbytes,
                  PACK(visop),packetidx)));
  gasneti_free(packedbuf);
}
MEDIUM_HANDLER(gasnete_getv_AMPipeline_reqh,2,3, 
              (token,addr,nbytes, UNPACK(a0),      a1),
              (token,addr,nbytes, UNPACK2(a0, a1), a2));
/* ------------------------------------------------------------------------------------ */
GASNETI_INLINE(gasnete_getv_AMPipeline_reph_inner)
void gasnete_getv_AMPipeline_reph_inner(gasnet_token_t token, 
  void *addr, size_t nbytes,
  void *_visop, gasnet_handlerarg_t packetidx) {
  gasneti_vis_op_t * const visop = _visop;
  gasnet_memvec_t * const savedlst = (gasnet_memvec_t *)(visop + 1);
  gasnete_packetdesc_t * const lpacket = ((gasnete_packetdesc_t *)visop->addr) + packetidx;
  size_t const lnum = lpacket->lastidx - lpacket->firstidx + 1;
  gasneti_assert(visop->type == GASNETI_VIS_CAT_GETV_AMPIPELINE);
  gasneti_assert(lpacket->lastidx < visop->count);
  { uint8_t *end = gasnete_memvec_unpack(lnum, savedlst+lpacket->firstidx, addr, lpacket->firstoffset, lpacket->lastlen);
    gasneti_assert(end - (uint8_t *)addr == nbytes);
  }
  if (gasneti_weakatomic_decrement_and_test(&(visop->packetcnt), GASNETI_ATOMIC_WMB_PRE)) {
    /* last response packet completes operation and cleans up */
    GASNETE_VISOP_SIGNAL(visop, 1);
    gasneti_free(visop->addr); /* free localpt */
    gasneti_free(visop); /* free visop, savedlst and send buffer */
  }
}
MEDIUM_HANDLER(gasnete_getv_AMPipeline_reph,2,3, 
              (token,addr,nbytes, UNPACK(a0),      a1),
              (token,addr,nbytes, UNPACK2(a0, a1), a2));
#endif
/*---------------------------------------------------------------------------------*/
/* reference version that uses individual puts */
gasnet_handle_t gasnete_putv_ref_indiv(gasnete_synctype_t synctype,
                                   gasnet_node_t dstnode,
                                   size_t dstcount, gasnet_memvec_t const dstlist[], 
                                   size_t srccount, gasnet_memvec_t const srclist[] GASNETE_THREAD_FARG) {
  const int islocal = (dstnode == gasneti_mynode);
  GASNETI_TRACE_EVENT(C, PUTV_REF_INDIV);
  gasneti_assert(srccount > 0 && dstcount > 0);
  GASNETE_START_NBIREGION(synctype, islocal);

  if (dstcount == 1) { /* dst is contiguous buffer */
    uintptr_t pdst = (uintptr_t)(dstlist[0].addr);
    size_t i;
    for (i = 0; i < srccount; i++) {
      const size_t srclen = srclist[i].len;
      if_pt (srclen > 0)
        GASNETE_PUT_INDIV(islocal, dstnode, (void *)pdst, srclist[i].addr, srclen);
      pdst += srclen;
    }
    gasneti_assert(pdst == (uintptr_t)(dstlist[0].addr)+dstlist[0].len);
  } else if (srccount == 1) { /* src is contiguous buffer */
    uintptr_t psrc = (uintptr_t)(srclist[0].addr);
    size_t i;
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

/* reference version that uses individual gets */
gasnet_handle_t gasnete_getv_ref_indiv(gasnete_synctype_t synctype,
                                   size_t dstcount, gasnet_memvec_t const dstlist[], 
                                   gasnet_node_t srcnode,
                                   size_t srccount, gasnet_memvec_t const srclist[] GASNETE_THREAD_FARG) {
  const int islocal = (srcnode == gasneti_mynode);
  GASNETI_TRACE_EVENT(C, GETV_REF_INDIV);
  gasneti_assert(srccount > 0 && dstcount > 0);
  GASNETE_START_NBIREGION(synctype, islocal);

  if (dstcount == 1) { /* dst is contiguous buffer */
    uintptr_t pdst = (uintptr_t)(dstlist[0].addr);
    size_t i;
    for (i = 0; i < srccount; i++) {
      const size_t srclen = srclist[i].len;
      if_pt (srclen > 0)
        GASNETE_GET_INDIV(islocal, (void *)pdst, srcnode, srclist[i].addr, srclen);
      pdst += srclen;
    }
    gasneti_assert(pdst == (uintptr_t)(dstlist[0].addr)+dstlist[0].len);
  } else if (srccount == 1) { /* src is contiguous buffer */
    uintptr_t psrc = (uintptr_t)(srclist[0].addr);
    size_t i;
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
/* top-level gasnet_putv_* entry point */
#ifndef GASNETE_PUTV_OVERRIDE
extern gasnet_handle_t gasnete_putv(gasnete_synctype_t synctype,
                                   gasnet_node_t dstnode,
                                   size_t dstcount, gasnet_memvec_t const dstlist[], 
                                   size_t srccount, gasnet_memvec_t const srclist[] GASNETE_THREAD_FARG) {
  /* catch silly degenerate cases */
  if_pf (dstcount == 0 || srccount == 0) /* empty (may miss some cases) */
    return GASNET_INVALID_HANDLE; 
  if_pf (dstcount + srccount <= 2 ||  /* fully contiguous */
         dstnode == gasneti_mynode) { /* purely local */ 
    return gasnete_putv_ref_indiv(synctype,dstnode,dstcount,dstlist,srccount,srclist GASNETE_THREAD_PASS);
  }

  /* select algorithm */
  #ifndef GASNETE_PUTV_SELECTOR
    #define GASNETE_PUTV_SELECTOR(synctype,dstnode,dstcount,dstlist,srccount,srclist)   \
      GASNETE_PUTV_GATHER_SELECTOR(synctype,dstnode,dstcount,dstlist,srccount,srclist); \
      GASNETE_PUTV_AMPIPELINE_SELECTOR(synctype,dstnode,dstcount,dstlist,srccount,srclist); \
      return gasnete_putv_ref_indiv(synctype,dstnode,dstcount,dstlist,srccount,srclist GASNETE_THREAD_PASS)
  #endif
  GASNETE_PUTV_SELECTOR(synctype,dstnode,dstcount,dstlist,srccount,srclist);
  gasneti_fatalerror("failure in GASNETE_PUTV_SELECTOR - should never reach here");
}
#endif
/* top-level gasnet_getv_* entry point */
#ifndef GASNETE_GETV_OVERRIDE
extern gasnet_handle_t gasnete_getv(gasnete_synctype_t synctype,
                                   size_t dstcount, gasnet_memvec_t const dstlist[], 
                                   gasnet_node_t srcnode,
                                   size_t srccount, gasnet_memvec_t const srclist[] GASNETE_THREAD_FARG) {
  /* catch silly degenerate cases */
  if_pf (dstcount == 0 || srccount == 0) /* empty (may miss some cases) */
    return GASNET_INVALID_HANDLE; 
  if_pf (dstcount + srccount <= 2 ||  /* fully contiguous */
         srcnode == gasneti_mynode) { /* purely local */ 
    return gasnete_getv_ref_indiv(synctype,dstcount,dstlist,srcnode,srccount,srclist GASNETE_THREAD_PASS);
  }

  /* select algorithm */
  #ifndef GASNETE_GETV_SELECTOR
    #define GASNETE_GETV_SELECTOR(synctype,dstcount,dstlist,srcnode,srccount,srclist)    \
      GASNETE_GETV_SCATTER_SELECTOR(synctype,dstcount,dstlist,srcnode,srccount,srclist); \
      GASNETE_GETV_AMPIPELINE_SELECTOR(synctype,dstcount,dstlist,srcnode,srccount,srclist); \
      return gasnete_getv_ref_indiv(synctype,dstcount,dstlist,srcnode,srccount,srclist GASNETE_THREAD_PASS)
  #endif
  GASNETE_GETV_SELECTOR(synctype,dstcount,dstlist,srcnode,srccount,srclist);
  gasneti_fatalerror("failure in GASNETE_GETV_SELECTOR - should never reach here");
}
#endif

/*---------------------------------------------------------------------------------*/
/* ***  Indexed *** */
/*---------------------------------------------------------------------------------*/

/* simple gather put, remotely contiguous */
#ifndef GASNETE_PUTI_GATHER_SELECTOR
#if GASNETE_USE_REMOTECONTIG_GATHER_SCATTER
gasnet_handle_t gasnete_puti_gather(gasnete_synctype_t synctype,
                                   gasnet_node_t dstnode, 
                                   size_t dstcount, void * const dstlist[], size_t dstlen,
                                   size_t srccount, void * const srclist[], size_t srclen GASNETE_THREAD_FARG) {
  gasnete_vis_threaddata_t * const td = GASNETE_VIS_MYTHREAD;
  size_t const nbytes = dstlen;
  gasneti_assert(dstcount == 1 && srccount > 1); /* only supports gather put */
  gasneti_assert(dstnode != gasneti_mynode); /* silly to use for local cases */
  gasneti_assert(nbytes > 0);
  GASNETI_TRACE_EVENT(C, PUTI_GATHER);

  { gasneti_vis_op_t * const visop = gasneti_malloc(sizeof(gasneti_vis_op_t)+nbytes);
    void * const packedbuf = visop + 1;
    gasnete_addrlist_pack(srccount, srclist, srclen, packedbuf, 0, (size_t)-1);
    visop->type = GASNETI_VIS_CAT_PUTI_GATHER;
    visop->handle = gasnete_put_nb_bulk(dstnode, dstlist[0], packedbuf, nbytes GASNETE_THREAD_PASS);
    GASNETE_PUSH_VISOP_RETURN(td, visop, synctype, 0);
  }
}
  #define GASNETE_PUTI_GATHER_SELECTOR(synctype,dstnode,dstcount,dstlist,dstlen,srccount,srclist,srclen) \
    if (dstcount == 1 && srccount > 1)                                                                   \
      return gasnete_puti_gather(synctype,dstnode,dstcount,dstlist,dstlen,srccount,srclist,srclen GASNETE_THREAD_PASS)
#else
  #define GASNETE_PUTI_GATHER_SELECTOR(synctype,dstnode,dstcount,dstlist,dstlen,srccount,srclist,srclen) ((void)0)
#endif
#endif

/* simple scatter get, remotely contiguous */
#ifndef GASNETE_GETI_SCATTER_SELECTOR
#if GASNETE_USE_REMOTECONTIG_GATHER_SCATTER
gasnet_handle_t gasnete_geti_scatter(gasnete_synctype_t synctype,
                                   size_t dstcount, void * const dstlist[], size_t dstlen,
                                   gasnet_node_t srcnode,
                                   size_t srccount, void * const srclist[], size_t srclen GASNETE_THREAD_FARG) {
  gasnete_vis_threaddata_t * const td = GASNETE_VIS_MYTHREAD;
  size_t const nbytes = srclen;
  gasneti_assert(srccount == 1 && dstcount > 1); /* only supports scatter get */
  gasneti_assert(srcnode != gasneti_mynode); /* silly to use for local cases */
  gasneti_assert(nbytes > 0);
  GASNETI_TRACE_EVENT(C, GETI_SCATTER);

  { gasneti_vis_op_t * const visop = gasneti_malloc(sizeof(gasneti_vis_op_t)+dstcount*sizeof(void *)+nbytes);
    void * * const savedlst = (void * *)(visop + 1);
    void * const packedbuf = (void *)(savedlst + dstcount);
    memcpy(savedlst, dstlist, dstcount*sizeof(void *));
    visop->type = GASNETI_VIS_CAT_GETI_SCATTER;
    visop->count = dstcount;
    visop->len = dstlen;
    visop->handle = gasnete_get_nb_bulk(packedbuf, srcnode, srclist[0], nbytes GASNETE_THREAD_PASS);
    GASNETE_PUSH_VISOP_RETURN(td, visop, synctype, 1);
  }
}
  #define GASNETE_GETI_SCATTER_SELECTOR(synctype,dstcount,dstlist,dstlen,srcnode,srccount,srclist,srclen) \
    if (srccount == 1 && dstcount > 1)                                                                    \
      return gasnete_geti_scatter(synctype,dstcount,dstlist,dstlen,srcnode,srccount,srclist,srclen GASNETE_THREAD_PASS)
#else
  #define GASNETE_GETI_SCATTER_SELECTOR(synctype,dstcount,dstlist,dstlen,srcnode,srccount,srclist,srclen) ((void)0)
#endif
#endif
/*---------------------------------------------------------------------------------*/
/* Pipelined AM gather-scatter put */
#ifndef GASNETE_PUTI_AMPIPELINE_SELECTOR
#if GASNETE_USE_AMPIPELINE
gasnet_handle_t gasnete_puti_AMPipeline(gasnete_synctype_t synctype,
                                   gasnet_node_t dstnode, 
                                   size_t dstcount, void * const dstlist[], size_t dstlen,
                                   size_t srccount, void * const srclist[], size_t srclen GASNETE_THREAD_FARG) {
  gasneti_assert(dstcount > 1); /* supports scatter put */
  gasneti_assert(dstnode != gasneti_mynode); /* silly to use for local cases */
  GASNETI_TRACE_EVENT(C, PUTI_AMPIPELINE);
  GASNETE_START_NBIREGION(synctype, 0);

  { void * * packedbuf = gasneti_malloc(gasnet_AMMaxMedium());
    gasnete_packetdesc_t *remotept;
    gasnete_packetdesc_t *localpt;
    size_t packetidx;
    size_t const packetcnt = gasnete_packetize_addrlist(dstcount, dstlen, srccount, srclen, 
                                                &remotept, &localpt, gasnet_AMMaxMedium(), 1);
    gasneti_iop_t *iop = gasneti_iop_register(packetcnt,0 GASNETE_THREAD_PASS);

    for (packetidx = 0; packetidx < packetcnt; packetidx++) {
      gasnete_packetdesc_t * const rpacket = &remotept[packetidx];
      gasnete_packetdesc_t * const lpacket = &localpt[packetidx];
      size_t const rnum = rpacket->lastidx - rpacket->firstidx + 1;
      size_t const lnum = lpacket->lastidx - lpacket->firstidx + 1;
      uint8_t *end;
      /* fill packet with remote metadata */
      memcpy(packedbuf, &dstlist[rpacket->firstidx], rnum*sizeof(void *));
      /* gather data payload from sourcelist into packet */
      end = gasnete_addrlist_pack(lnum, &srclist[lpacket->firstidx], srclen, &packedbuf[rnum], 
                                  lpacket->firstoffset, lpacket->lastlen);

      /* send AM(rnum, iop) from packedbuf */
      GASNETI_SAFE(
        MEDIUM_REQ(5,6,(dstnode, gasneti_handleridx(gasnete_puti_AMPipeline_reqh),
                      packedbuf, end - (uint8_t *)packedbuf,
                      PACK(iop), rnum, dstlen, rpacket->firstoffset, rpacket->lastlen)));
    }

    gasneti_free(remotept);
    gasneti_free(localpt);
    gasneti_free(packedbuf);
    GASNETE_END_NBIREGION_AND_RETURN(synctype, 0);
  }
}
  #define GASNETE_PUTI_AMPIPELINE_SELECTOR(synctype,dstnode,dstcount,dstlist,dstlen,srccount,srclist,srclen) \
    if (dstcount > 1 && dstlen == (uint32_t)(dstlen))                                                        \
      return gasnete_puti_AMPipeline(synctype,dstnode,dstcount,dstlist,dstlen,srccount,srclist,srclen GASNETE_THREAD_PASS)
#else
  #define GASNETE_PUTI_AMPIPELINE_SELECTOR(synctype,dstnode,dstcount,dstlist,dstlen,srccount,srclist,srclen) ((void)0)
#endif
#endif
/* ------------------------------------------------------------------------------------ */
#if GASNETE_USE_AMPIPELINE
GASNETI_INLINE(gasnete_puti_AMPipeline_reqh_inner)
void gasnete_puti_AMPipeline_reqh_inner(gasnet_token_t token, 
  void *addr, size_t nbytes,
  void *iop, gasnet_handlerarg_t rnum, 
  gasnet_handlerarg_t dstlen, gasnet_handlerarg_t firstoffset, gasnet_handlerarg_t lastlen) {
  void * const * const rlist = addr;
  uint8_t * const data = (uint8_t *)(&rlist[rnum]);
  uint8_t * const end = gasnete_addrlist_unpack(rnum, rlist, dstlen, data, firstoffset, lastlen);
  gasneti_assert(end - (uint8_t *)addr <= gasnet_AMMaxMedium());
  gasneti_sync_writes();
  /* TODO: coalesce acknowledgements - need a per-srcnode, per-op seqnum & packetcnt */
  GASNETI_SAFE(
    SHORT_REP(1,2,(token, gasneti_handleridx(gasnete_putvis_AMPipeline_reph),
                  PACK(iop))));
}
MEDIUM_HANDLER(gasnete_puti_AMPipeline_reqh,5,6, 
              (token,addr,nbytes, UNPACK(a0),      a1,a2,a3,a4),
              (token,addr,nbytes, UNPACK2(a0, a1), a2,a3,a4,a5));
#endif
/* ------------------------------------------------------------------------------------ */
/* Pipelined AM gather-scatter get */
#ifndef GASNETE_GETI_AMPIPELINE_SELECTOR
#if GASNETE_USE_AMPIPELINE
gasnet_handle_t gasnete_geti_AMPipeline(gasnete_synctype_t synctype,
                                   size_t dstcount, void * const dstlist[], size_t dstlen,
                                   gasnet_node_t srcnode,
                                   size_t srccount, void * const srclist[], size_t srclen GASNETE_THREAD_FARG) {
  gasneti_assert(srccount > 1); /* supports gather get */
  gasneti_assert(srcnode != gasneti_mynode); /* silly to use for local cases */
  GASNETI_TRACE_EVENT(C, GETI_AMPIPELINE);

  { gasneti_vis_op_t * const visop = gasneti_malloc(sizeof(gasneti_vis_op_t) +
                                                    dstcount*sizeof(void *) + 
                                                    gasnet_AMMaxMedium());
    void * * const savedlst = (void * *)(visop + 1);
    void * * const packedbuf = savedlst + dstcount;
    gasnete_packetdesc_t *remotept;
    gasnete_packetdesc_t *localpt;
    size_t packetidx;
    size_t const packetcnt = gasnete_packetize_addrlist(srccount, srclen, dstcount, dstlen,  
                                                &remotept, &localpt, gasnet_AMMaxMedium(), 0);
    GASNETE_VISOP_SETUP(visop, synctype, 1);
    #if GASNET_DEBUG
      visop->type = GASNETI_VIS_CAT_GETI_AMPIPELINE;
      visop->count = dstcount;
    #endif
    gasneti_assert(packetcnt <= GASNETI_ATOMIC_MAX);
    gasneti_assert(packetcnt == (gasnet_handlerarg_t)packetcnt);
    visop->len = dstlen;
    visop->addr = localpt;
    memcpy(savedlst, dstlist, dstcount*sizeof(void *));
    gasneti_weakatomic_set(&(visop->packetcnt), packetcnt, GASNETI_ATOMIC_WMB_POST);

    for (packetidx = 0; packetidx < packetcnt; packetidx++) {
      gasnete_packetdesc_t * const rpacket = &remotept[packetidx];
      size_t const rnum = rpacket->lastidx - rpacket->firstidx + 1;
      /* fill packet with remote metadata */
      memcpy(packedbuf, &srclist[rpacket->firstidx], rnum*sizeof(void *));

      /* send AM(visop) from packedbuf */
      GASNETI_SAFE(
        MEDIUM_REQ(5,6,(srcnode, gasneti_handleridx(gasnete_geti_AMPipeline_reqh),
                      packedbuf, rnum*sizeof(void *),
                      PACK(visop), packetidx, srclen, rpacket->firstoffset, rpacket->lastlen)));
    }

    gasneti_free(remotept);
    GASNETE_VISOP_RETURN(visop, synctype);
  }
}
  #define GASNETE_GETI_AMPIPELINE_SELECTOR(synctype,dstcount,dstlist,dstlen,srcnode,srccount,srclist,srclen) \
    if (srccount > 1)                                                                                        \
      return gasnete_geti_AMPipeline(synctype,dstcount,dstlist,dstlen,srcnode,srccount,srclist,srclen GASNETE_THREAD_PASS)
#else
  #define GASNETE_GETI_AMPIPELINE_SELECTOR(synctype,dstcount,dstlist,dstlen,srcnode,srccount,srclist,srclen) ((void)0)
#endif
#endif
/* ------------------------------------------------------------------------------------ */
#if GASNETE_USE_AMPIPELINE
GASNETI_INLINE(gasnete_geti_AMPipeline_reqh_inner)
void gasnete_geti_AMPipeline_reqh_inner(gasnet_token_t token, 
  void *addr, size_t nbytes,
  void *_visop, gasnet_handlerarg_t packetidx,
  gasnet_handlerarg_t dstlen, gasnet_handlerarg_t firstoffset, gasnet_handlerarg_t lastlen) {
  void * const * const rlist = addr;
  size_t const rnum = nbytes / sizeof(void *);
  uint8_t * const packedbuf = gasneti_malloc(gasnet_AMMaxMedium());
  /* gather data payload from sourcelist into packet */
  uint8_t * const end = gasnete_addrlist_pack(rnum, rlist, dstlen, packedbuf, firstoffset, lastlen);
  size_t const repbytes = end - packedbuf;
  gasneti_assert(repbytes <= gasnet_AMMaxMedium());
  GASNETI_SAFE(
    MEDIUM_REP(2,3,(token, gasneti_handleridx(gasnete_geti_AMPipeline_reph),
                  packedbuf, repbytes,
                  PACK(_visop),packetidx)));
  gasneti_free(packedbuf);
}
MEDIUM_HANDLER(gasnete_geti_AMPipeline_reqh,5,6, 
              (token,addr,nbytes, UNPACK(a0),      a1,a2,a3,a4),
              (token,addr,nbytes, UNPACK2(a0, a1), a2,a3,a4,a5));
/* ------------------------------------------------------------------------------------ */
GASNETI_INLINE(gasnete_geti_AMPipeline_reph_inner)
void gasnete_geti_AMPipeline_reph_inner(gasnet_token_t token, 
  void *addr, size_t nbytes,
  void *_visop, gasnet_handlerarg_t packetidx) {
  gasneti_vis_op_t * const visop = _visop;
  void * const * const savedlst = (void * *)(visop + 1);
  gasnete_packetdesc_t * const lpacket = ((gasnete_packetdesc_t *)visop->addr) + packetidx;
  size_t const lnum = lpacket->lastidx - lpacket->firstidx + 1;
  gasneti_assert(visop->type == GASNETI_VIS_CAT_GETI_AMPIPELINE);
  gasneti_assert(lpacket->lastidx < visop->count);
  { uint8_t *end = gasnete_addrlist_unpack(lnum, savedlst+lpacket->firstidx, visop->len, addr, lpacket->firstoffset, lpacket->lastlen);
    gasneti_assert(end - (uint8_t *)addr == nbytes);
  }
  if (gasneti_weakatomic_decrement_and_test(&(visop->packetcnt), GASNETI_ATOMIC_WMB_PRE)) {
    /* last response packet completes operation and cleans up */
    GASNETE_VISOP_SIGNAL(visop, 1);
    gasneti_free(visop->addr); /* free localpt */
    gasneti_free(visop); /* free visop, savedlst and send buffer */
  }
}
MEDIUM_HANDLER(gasnete_geti_AMPipeline_reph,2,3, 
              (token,addr,nbytes, UNPACK(a0),      a1),
              (token,addr,nbytes, UNPACK2(a0, a1), a2));
#endif
/*---------------------------------------------------------------------------------*/
/* reference version that uses individual puts */
gasnet_handle_t gasnete_puti_ref_indiv(gasnete_synctype_t synctype,
                                   gasnet_node_t dstnode, 
                                   size_t dstcount, void * const dstlist[], size_t dstlen,
                                   size_t srccount, void * const srclist[], size_t srclen GASNETE_THREAD_FARG) {
  const int islocal = (dstnode == gasneti_mynode);
  GASNETI_TRACE_EVENT(C, PUTI_REF_INDIV);
  gasneti_assert(srccount > 0 && dstcount > 0 && ((uintptr_t)dstcount)*dstlen == ((uintptr_t)srccount)*srclen);
  gasneti_assert(srclen > 0 && dstlen > 0);
  GASNETE_START_NBIREGION(synctype, islocal);

  if (dstlen == srclen) { /* matched sizes (fast case) */
    size_t i;
    gasneti_assert(dstcount == srccount);
    for (i = 0; i < dstcount; i++) {
      GASNETE_PUT_INDIV(islocal, dstnode, dstlist[i], srclist[i], dstlen);
    }
  } else if (dstcount == 1) { /* dst is contiguous buffer */
    uintptr_t pdst = (uintptr_t)(dstlist[0]);
    size_t i;
    for (i = 0; i < srccount; i++) {
      GASNETE_PUT_INDIV(islocal, dstnode, (void *)pdst, srclist[i], srclen);
      pdst += srclen;
    }
    gasneti_assert(pdst == (uintptr_t)(dstlist[0])+dstlen);
  } else if (srccount == 1) { /* src is contiguous buffer */
    uintptr_t psrc = (uintptr_t)(srclist[0]);
    size_t i;
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

/* reference version that uses individual gets */
gasnet_handle_t gasnete_geti_ref_indiv(gasnete_synctype_t synctype,
                                   size_t dstcount, void * const dstlist[], size_t dstlen,
                                   gasnet_node_t srcnode,
                                   size_t srccount, void * const srclist[], size_t srclen GASNETE_THREAD_FARG) {
  const int islocal = (srcnode == gasneti_mynode);
  GASNETI_TRACE_EVENT(C, GETI_REF_INDIV);
  gasneti_assert(srccount > 0 && dstcount > 0 && ((uintptr_t)dstcount)*dstlen == ((uintptr_t)srccount)*srclen);
  gasneti_assert(srclen > 0 && dstlen > 0);
  GASNETE_START_NBIREGION(synctype, islocal);

  if (dstlen == srclen) { /* matched sizes (fast case) */
    size_t i;
    gasneti_assert(dstcount == srccount);
    for (i = 0; i < dstcount; i++) {
      GASNETE_GET_INDIV(islocal, dstlist[i], srcnode, srclist[i], dstlen);
    }
  } else if (dstcount == 1) { /* dst is contiguous buffer */
    uintptr_t pdst = (uintptr_t)(dstlist[0]);
    size_t i;
    for (i = 0; i < srccount; i++) {
      GASNETE_GET_INDIV(islocal, (void *)pdst, srcnode, srclist[i], srclen);
      pdst += srclen;
    }
    gasneti_assert(pdst == (uintptr_t)(dstlist[0])+dstlen);
  } else if (srccount == 1) { /* src is contiguous buffer */
    uintptr_t psrc = (uintptr_t)(srclist[0]);
    size_t i;
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
/* reference version that uses vector interface */
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
/* reference version that uses vector interface */
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
/* top-level gasnet_puti_* entry point */
#ifndef GASNETE_PUTI_OVERRIDE
extern gasnet_handle_t gasnete_puti(gasnete_synctype_t synctype,
                                   gasnet_node_t dstnode, 
                                   size_t dstcount, void * const dstlist[], size_t dstlen,
                                   size_t srccount, void * const srclist[], size_t srclen GASNETE_THREAD_FARG) {
  /* catch silly degenerate cases */
  if_pf (dstcount + srccount <= 2 ||  /* empty or fully contiguous */
         dstnode == gasneti_mynode) { /* purely local */ 
    if (dstcount == 0) return GASNET_INVALID_HANDLE;
    else return gasnete_puti_ref_indiv(synctype,dstnode,dstcount,dstlist,dstlen,srccount,srclist,srclen GASNETE_THREAD_PASS);
  }

  /* select algorithm */
  #ifndef GASNETE_PUTI_SELECTOR
    #if GASNETE_RANDOM_SELECTOR
      #define GASNETE_PUTI_SELECTOR(synctype,dstnode,dstcount,dstlist,dstlen,srccount,srclist,srclen) do {                        \
        switch (rand() % 3) {                                                                                                     \
          case 0:                                                                                                                 \
            GASNETE_PUTI_GATHER_SELECTOR(synctype,dstnode,dstcount,dstlist,dstlen,srccount,srclist,srclen);                       \
          case 1:                                                                                                                 \
            GASNETE_PUTI_AMPIPELINE_SELECTOR(synctype,dstnode,dstcount,dstlist,dstlen,srccount,srclist,srclen);                   \
          case 2:                                                                                                                 \
            return gasnete_puti_ref_indiv(synctype,dstnode,dstcount,dstlist,dstlen,srccount,srclist,srclen GASNETE_THREAD_PASS);  \
          case 3:                                                                                                                 \
            return gasnete_puti_ref_vector(synctype,dstnode,dstcount,dstlist,dstlen,srccount,srclist,srclen GASNETE_THREAD_PASS); \
        } } while (0)
    #else
      #define GASNETE_PUTI_SELECTOR(synctype,dstnode,dstcount,dstlist,dstlen,srccount,srclist,srclen)       \
        GASNETE_PUTI_GATHER_SELECTOR(synctype,dstnode,dstcount,dstlist,dstlen,srccount,srclist,srclen);     \
        GASNETE_PUTI_AMPIPELINE_SELECTOR(synctype,dstnode,dstcount,dstlist,dstlen,srccount,srclist,srclen); \
        return gasnete_puti_ref_indiv(synctype,dstnode,dstcount,dstlist,dstlen,srccount,srclist,srclen GASNETE_THREAD_PASS)
    #endif
  #endif
  GASNETE_PUTI_SELECTOR(synctype,dstnode,dstcount,dstlist,dstlen,srccount,srclist,srclen);
  gasneti_fatalerror("failure in GASNETE_PUTI_SELECTOR - should never reach here");
  return GASNET_INVALID_HANDLE; /* avoid warning on MIPSPro */
}
#endif
/* top-level gasnet_geti_* entry point */
#ifndef GASNETE_GETI_OVERRIDE
extern gasnet_handle_t gasnete_geti(gasnete_synctype_t synctype,
                                   size_t dstcount, void * const dstlist[], size_t dstlen,
                                   gasnet_node_t srcnode,
                                   size_t srccount, void * const srclist[], size_t srclen GASNETE_THREAD_FARG) {
  /* catch silly degenerate cases */
  if_pf (dstcount + srccount <= 2 ||  /* empty or fully contiguous */
         srcnode == gasneti_mynode) { /* purely local */ 
    if (dstcount == 0) return GASNET_INVALID_HANDLE;
    else return gasnete_geti_ref_indiv(synctype,dstcount,dstlist,dstlen,srcnode,srccount,srclist,srclen GASNETE_THREAD_PASS);
  }

  /* select algorithm */
  #ifndef GASNETE_GETI_SELECTOR
    #if GASNETE_RANDOM_SELECTOR
      #define GASNETE_GETI_SELECTOR(synctype,dstcount,dstlist,dstlen,srcnode,srccount,srclist,srclen) do {                        \
        switch (rand() % 3) {                                                                                                     \
          case 0:                                                                                                                 \
            GASNETE_GETI_SCATTER_SELECTOR(synctype,dstcount,dstlist,dstlen,srcnode,srccount,srclist,srclen);                      \
          case 1:                                                                                                                 \
            GASNETE_GETI_AMPIPELINE_SELECTOR(synctype,dstcount,dstlist,dstlen,srcnode,srccount,srclist,srclen);                   \
          case 2:                                                                                                                 \
            return gasnete_geti_ref_indiv(synctype,dstcount,dstlist,dstlen,srcnode,srccount,srclist,srclen GASNETE_THREAD_PASS);  \
          case 3:                                                                                                                 \
            return gasnete_geti_ref_vector(synctype,dstcount,dstlist,dstlen,srcnode,srccount,srclist,srclen GASNETE_THREAD_PASS); \
        } } while (0)
    #else
      #define GASNETE_GETI_SELECTOR(synctype,dstcount,dstlist,dstlen,srcnode,srccount,srclist,srclen)       \
        GASNETE_GETI_SCATTER_SELECTOR(synctype,dstcount,dstlist,dstlen,srcnode,srccount,srclist,srclen);    \
        GASNETE_GETI_AMPIPELINE_SELECTOR(synctype,dstcount,dstlist,dstlen,srcnode,srccount,srclist,srclen); \
        return gasnete_geti_ref_indiv(synctype,dstcount,dstlist,dstlen,srcnode,srccount,srclist,srclen GASNETE_THREAD_PASS)
    #endif
  #endif
  GASNETE_GETI_SELECTOR(synctype,dstcount,dstlist,dstlen,srcnode,srccount,srclist,srclen);
  gasneti_fatalerror("failure in GASNETE_GETI_SELECTOR - should never reach here");
  return GASNET_INVALID_HANDLE; /* avoid warning on MIPSPro */
}
#endif
/*---------------------------------------------------------------------------------*/
/* ***  Strided *** */
/*---------------------------------------------------------------------------------*/
/* helper macros */
/* increment the values in init[] by incval chunks, 
   using provided count[], contiglevel and limit
   when contiglevel=i, chunks are assumed to have size count[i]*count[i-1]*...*count[0]
*/
#define GASNETE_STRIDED_VECTOR_INC(init, incval, count, contiglevel, limit) do { \
    size_t const _contiglevel = (contiglevel);                                   \
    size_t const _dimlim = (limit) - _contiglevel;                               \
    size_t const * const _count = (count);                                       \
    size_t * const _init = (init);                                               \
    size_t _dim;                                                                 \
    _init[0] += (incval);                                                        \
    for ( _dim = 0; _dim < _dimlim; _dim++) {                                    \
      size_t const _thismax = _count[_dim+_contiglevel+1];                       \
      if (_init[_dim] < _thismax) break;                                         \
      else {                                                                     \
        size_t const _carries = _init[_dim] / _thismax;                          \
        gasneti_assert(_dim != _dimlim-1); /* indicates an overflow */           \
        _init[_dim] -= _carries * _thismax;                                      \
        _init[_dim+1] += _carries;                                               \
      }                                                                          \
    }                                                                            \
  } while(0)

/* The GASNETE_STRIDED_HELPER(limit,contiglevel) macro expands to code based on 
     the template below (shown for the case of limit - contiglevel == 3, 
     eg 4-D area contiguous only in the smallest dimension).
   If (limit - contiglevel) > GASNETE_LOOPING_DIMS, then we use the generalized 
     striding code shown in the default case of GASNETE_STRIDED_HELPER.
   Parameters: 
     * if caller scope contains the declaration: 
         GASNETE_STRIDED_HELPER_DECLARE_NODST;
       then in all cases the dst pointer is not calculated, and pdst is always NULL
     * if caller scope contains the declaration: 
         GASNETE_STRIDED_HELPER_DECLARE_PARTIAL(numchunks, init, addr_already_offset, update_addr_init);
       then the traversal will iterate over a total of numchunks contiguous chunks, 
       beginning at chunk coordinate indicated by init[0...(limit - contiglevel - 1)]
       if addr_already_offset is nonzero, the code assumes srcaddr/dstaddr already reference the first chunk,
       otherwise, the srcaddr/dstaddr values are offset based on init to reach the first chunk
       iff update_addr_init is nonzero, then srcaddr/dstaddr/init are updated on exit to point to the next unused chunk
       
    uint8_t * psrc = srcaddr;
    uint8_t * pdst = dstaddr;
    size_t _chunkcnt = numchunks;

    if (HAVE_PARTIAL && !srcdst_already_offset) {
      size_t _dim;
      for (_dim = contiglevel; _dim < limit; _dim++) {
        psrc += srcstrides[_dim] * init[_dim-contiglevel];
        pdst += dststrides[_dim] * init[_dim-contiglevel];
      }
    }

    size_t const _count0 = count[contiglevel+1];
    size_t _i0 = (HAVE_PARTIAL ? _count0 - init[0] : 0);
    size_t const _srcbump0 = srcstrides[contiglevel];
    size_t const _dstbump0 = dststrides[contiglevel];

    size_t const _count1 = count[contiglevel+1+1];
    size_t _i1 = (HAVE_PARTIAL ? _count1 - init[1] : 0);
    size_t const _srcbump1 = srcstrides[contiglevel+1] - _count0*srcstrides[contiglevel+1-1];
    size_t const _dstbump1 = dststrides[contiglevel+1] - _count0*dststrides[contiglevel+1-1];

    size_t const _count2 = count[contiglevel+2+1];
    size_t _i2 = (HAVE_PARTIAL ? _count2 - init[2] : 0);
    size_t const _srcbump2 = srcstrides[contiglevel+2] - _count1*srcstrides[contiglevel+2-1];
    size_t const _dstbump2 = dststrides[contiglevel+2] - _count1*dststrides[contiglevel+2-1];

    if (HAVE_PARTIAL) goto body;

    for (_i2 = _count2; _i2; _i2--) { 

    for (_i1 = _count1; _i1; _i1--) {

    for (_i0 = _count0; _i0; _i0--) {
      body:
      GASNETE_STRIDED_HELPER_LOOPBODY(psrc,pdst);
      psrc += _srcbump0;
      pdst += _dstbump0;
      if_pf (HAVE_PARTIAL && --_chunkcnt == 0) goto done;
    }

      psrc += _srcbump1;
      pdst += _dstbump1;
    }

      psrc += _srcbump2;
      pdst += _dstbump2;
    }
    done: ;
    if (HAVE_PARTIAL && update_addr_init) {
      if (!_i0) ; // loop nest terminated 
      else if (!--_i0) { _i0 = _count0; 

        psrc += _srcbump1; 
        pdst += _dstbump1; 
        if (!--_i1) { _i1 = _count1; 

          psrc += _srcbump2; 
          pdst += _dstbump2; 
          if (!--_i2) { _i2 = _count2; 

          }
        }
      }
      srcaddr = psrc;
      dstaddr = pdst;

      init[0] = _count0 - _i0;
      init[1] = _count1 - _i1;
      init[2] = _count2 - _i2;
    }
*/

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

#define GASNETE_STRIDED_HELPER_DECLARE_NODST \
       size_t dststrides[1];                 \
       void * dstaddr = NULL;                \
       static int8_t _gasnete_strided_helper_nodst = (int8_t)sizeof(_gasnete_strided_helper_nodst)

static int32_t _gasnete_strided_helper_nodst = (int32_t)sizeof(_gasnete_strided_helper_nodst);
#define GASNETE_STRIDED_HELPER_HAVEDST (sizeof(_gasnete_strided_helper_nodst) == 4)

#define GASNETE_STRIDED_HELPER_DECLARE_PARTIAL(numchunks, init, addr_already_offset, update_addr_init) \
       size_t * const _gasnete_strided_init = (init);                                                  \
       size_t _gasnete_strided_chunkcnt = (numchunks);                                                 \
       int const _gasnete_strided_addr_already_offset = (addr_already_offset);                         \
       int const _gasnete_strided_update_addr_init = (update_addr_init);                               \
       static int8_t _gasnete_strided_helper_havepartial = (int8_t)sizeof(_gasnete_strided_helper_havepartial)

static int32_t * const _gasnete_strided_init = (sizeof(_gasnete_strided_init)?NULL:NULL);
static int32_t _gasnete_strided_chunkcnt = (int32_t)sizeof(_gasnete_strided_chunkcnt);
static int32_t const _gasnete_strided_addr_already_offset = (int32_t)sizeof(_gasnete_strided_addr_already_offset);
static int32_t const _gasnete_strided_update_addr_init = (int32_t)sizeof(_gasnete_strided_update_addr_init);
static int32_t const _gasnete_strided_helper_havepartial = (int32_t)sizeof(_gasnete_strided_helper_havepartial);
#define GASNETE_STRIDED_HELPER_HAVEPARTIAL (sizeof(_gasnete_strided_helper_havepartial) == 1)

#define _GASNETE_STRIDED_LABEL(idx,name) \
        _CONCAT(_GASNETE_STRIDED_LABEL_##name##_##idx,_CONCAT(_,__LINE__))

#define GASNETE_STRIDED_HELPER_SETUP_BASE()                                                  \
    size_t const _count0 = count[contiglevel+1];                                             \
    size_t const _srcbump0 = srcstrides[contiglevel];                                        \
    size_t const _dstbump0 = (GASNETE_STRIDED_HELPER_HAVEDST ? dststrides[contiglevel] : 0); \
    size_t _i0 = (GASNETE_STRIDED_HELPER_HAVEPARTIAL ? _count0 - _gasnete_strided_init[0] : 0); 

#define GASNETE_STRIDED_HELPER_SETUP_INT(curr, lower)                                                               \
    size_t const _count##curr = count[contiglevel+curr+1];                                                          \
    size_t const _srcbump##curr = srcstrides[contiglevel+curr] - _count##lower*srcstrides[contiglevel+curr-1];      \
    size_t const _dstbump##curr = (GASNETE_STRIDED_HELPER_HAVEDST ?                                                 \
                                  dststrides[contiglevel+curr] - _count##lower*dststrides[contiglevel+curr-1] : 0); \
    size_t _i##curr = (GASNETE_STRIDED_HELPER_HAVEPARTIAL ? _count##curr - _gasnete_strided_init[curr] : 0);          


#define GASNETE_STRIDED_HELPER_LOOPHEAD_BASE()
#define GASNETE_STRIDED_HELPER_LOOPHEAD_INT(curr,junk) \
    for (_i##curr = _count##curr; _i##curr; _i##curr--) { 

#define GASNETE_STRIDED_HELPER_LOOPTAIL_BASE()
#define GASNETE_STRIDED_HELPER_LOOPTAIL_INT(curr,junk)            \
      psrc += _srcbump##curr;                                     \
      if (GASNETE_STRIDED_HELPER_HAVEDST) pdst += _dstbump##curr; \
      else gasneti_assert(pdst == NULL);                          \
    }

#define GASNETE_STRIDED_HELPER_CLEANUPHEAD_BASE()
#define GASNETE_STRIDED_HELPER_CLEANUPHEAD_INT(curr,junk)         \
      psrc += _srcbump##curr;                                     \
      if (GASNETE_STRIDED_HELPER_HAVEDST) pdst += _dstbump##curr; \
      if (!--_i##curr) { _i##curr = _count##curr;

#define GASNETE_STRIDED_HELPER_CLEANUPTAIL_BASE()
#define GASNETE_STRIDED_HELPER_CLEANUPTAIL_INT(curr,junk) }

#define GASNETE_STRIDED_HELPER_CLEANUPINIT_BASE()
#define GASNETE_STRIDED_HELPER_CLEANUPINIT_INT(curr,junk) \
  _gasnete_strided_init[curr] = _count##curr - _i##curr;

#define GASNETE_STRIDED_HELPER_CASE_BASE() 
#define GASNETE_STRIDED_HELPER_CASE_INT(junk,curr) case curr+1: {               \
    GASNETE_METAMACRO_ASC##curr(GASNETE_STRIDED_HELPER_SETUP)                   \
    if (GASNETE_STRIDED_HELPER_HAVEPARTIAL)                                     \
      goto _GASNETE_STRIDED_LABEL(curr,BODY);                                   \
    GASNETE_METAMACRO_DESC##curr(GASNETE_STRIDED_HELPER_LOOPHEAD)               \
    for (_i0 = _count0; _i0; _i0--) {                                           \
      _GASNETE_STRIDED_LABEL(curr,BODY): ;                                      \
      GASNETE_STRIDED_HELPER_LOOPBODY(psrc,pdst);                               \
      psrc += _srcbump0;                                                        \
      if (GASNETE_STRIDED_HELPER_HAVEDST) pdst += _dstbump0;                    \
      if_pf (GASNETE_STRIDED_HELPER_HAVEPARTIAL &&                              \
          --_gasnete_strided_chunkcnt == 0)                                     \
        goto _GASNETE_STRIDED_LABEL(curr,DONE);                                 \
    }                                                                           \
    GASNETE_METAMACRO_ASC##curr(GASNETE_STRIDED_HELPER_LOOPTAIL)                \
    _GASNETE_STRIDED_LABEL(curr,DONE): ;                                        \
    if (GASNETE_STRIDED_HELPER_HAVEPARTIAL &&                                   \
        _gasnete_strided_update_addr_init) {                                    \
      if (!_i0) ; /* loop nest terminated */                                    \
      else if (!--_i0) { _i0 = _count0;                                         \
        GASNETE_METAMACRO_ASC##curr(GASNETE_STRIDED_HELPER_CLEANUPHEAD)         \
        GASNETE_METAMACRO_ASC##curr(GASNETE_STRIDED_HELPER_CLEANUPTAIL)         \
      }                                                                         \
      srcaddr = psrc;                                                           \
      if (GASNETE_STRIDED_HELPER_HAVEDST) dstaddr = pdst;                       \
      else gasneti_assert(pdst == NULL);                                        \
      _gasnete_strided_init[0] = _count0 - _i0;                                 \
      GASNETE_METAMACRO_ASC##curr(GASNETE_STRIDED_HELPER_CLEANUPINIT)           \
    }                                                                           \
  } break;

#if GASNET_DEBUG
  /* assert the generalized looping code is functioning properly */
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

#if GASNETE_LOOPING_DIMS > GASNETE_METAMACRO_DEPTH_MAX
#error GASNETE_LOOPING_DIMS must be <= GASNETE_METAMACRO_DEPTH_MAX
#endif

#define GASNETE_STRIDED_HELPER(limit,contiglevel) do {                 \
  /* general setup code */                                             \
  uint8_t *psrc = srcaddr;                                             \
  uint8_t *pdst = dstaddr;                                             \
  gasneti_assert((limit) > (contiglevel));                             \
  if (GASNETE_STRIDED_HELPER_HAVEPARTIAL &&                            \
      !_gasnete_strided_addr_already_offset) {                         \
    size_t _dim;                                                       \
    for (_dim = contiglevel; _dim < limit; _dim++) {                   \
      psrc += srcstrides[_dim] *                                       \
              _gasnete_strided_init[_dim-contiglevel];                 \
      if (GASNETE_STRIDED_HELPER_HAVEDST)                              \
        pdst += dststrides[_dim] *                                     \
                _gasnete_strided_init[_dim-contiglevel];               \
    }                                                                  \
  }                                                                    \
  switch ((limit) - (contiglevel)) {                                   \
    _CONCAT(GASNETE_METAMACRO_ASC,                                     \
            GASNETE_LOOPING_DIMS)(GASNETE_STRIDED_HELPER_CASE)         \
    default: { /* arbitrary dimensions > GASNETE_LOOPING_DIMS */       \
      size_t const dim = (limit) - (contiglevel);                      \
      size_t const * const _count = count + contiglevel + 1;           \
      size_t const * const _srcstrides = srcstrides + contiglevel + 1; \
      size_t const * const _dststrides =                               \
      (GASNETE_STRIDED_HELPER_HAVEDST?dststrides + contiglevel + 1:0); \
      ssize_t curdim = 0; /* must be signed */                         \
      /* Psrc,dst}ptr_start save the address of the first element */   \
      /* in the current row at each dimension */                       \
      uint8_t *_srcptr_start[GASNETE_DIRECT_DIMS];                     \
      uint8_t ** const srcptr_start = (dim <= GASNETE_DIRECT_DIMS ?    \
         _srcptr_start : gasneti_malloc(sizeof(uint8_t *)*dim));       \
      uint8_t *_dstptr_start[GASNETE_DIRECT_DIMS];                     \
      uint8_t ** const dstptr_start = ((dim <= GASNETE_DIRECT_DIMS ||  \
                                    !GASNETE_STRIDED_HELPER_HAVEDST) ? \
         _dstptr_start : gasneti_malloc(sizeof(uint8_t *)*dim));       \
      size_t _idx[GASNETE_DIRECT_DIMS];                                \
      size_t * const idx = (dim <= GASNETE_DIRECT_DIMS ?               \
         _idx : gasneti_malloc(sizeof(size_t)*dim));                   \
      uint8_t *psrc_base = psrc; /* hold true base of strided area */  \
      uint8_t *pdst_base = pdst;                                       \
      if (GASNETE_STRIDED_HELPER_HAVEPARTIAL) {                        \
        for (curdim = 0; curdim < dim; curdim++) {                     \
          size_t thisval = _gasnete_strided_init[curdim];              \
          gasneti_assert(thisval < _count[curdim]);                    \
          idx[curdim] = thisval;                                       \
          psrc_base -= thisval*_srcstrides[curdim-1];                  \
          srcptr_start[curdim] = psrc_base;                            \
          if (GASNETE_STRIDED_HELPER_HAVEDST) {                        \
            pdst_base -= thisval*_dststrides[curdim-1];                \
            dstptr_start[curdim] = pdst_base;                          \
          }                                                            \
        }                                                              \
      } else {                                                         \
        for (curdim = 0; curdim < dim; curdim++) {                     \
          idx[curdim] = 0;                                             \
          srcptr_start[curdim] = psrc;                                 \
          if (GASNETE_STRIDED_HELPER_HAVEDST)                          \
            dstptr_start[curdim] = pdst;                               \
        }                                                              \
      }                                                                \
      while (1) {                                                      \
        GASNETE_CHECK_PTR(psrc, psrc_base, _srcstrides, idx, dim);     \
        if (GASNETE_STRIDED_HELPER_HAVEDST)                            \
          GASNETE_CHECK_PTR(pdst, pdst_base, _dststrides, idx, dim);   \
        else gasneti_assert(pdst == NULL);                             \
        GASNETE_STRIDED_HELPER_LOOPBODY(psrc,pdst);                    \
        for (curdim = 0; curdim < dim; curdim++) {                     \
          if (idx[curdim] < _count[curdim]-1) {                        \
            idx[curdim]++; /* advance to next row in this dim */       \
            psrc += _srcstrides[curdim-1];                             \
            if (GASNETE_STRIDED_HELPER_HAVEDST)                        \
              pdst += _dststrides[curdim-1];                           \
            break;                                                     \
          } else { /* row complete at this dim, prop to higher dim */  \
            idx[curdim] = 0;                                           \
            psrc = srcptr_start[curdim];                               \
            if (GASNETE_STRIDED_HELPER_HAVEDST)                        \
              pdst = dstptr_start[curdim];                             \
          }                                                            \
        }                                                              \
        if_pf ((GASNETE_STRIDED_HELPER_HAVEPARTIAL &&                  \
                --_gasnete_strided_chunkcnt == 0) ||                   \
               curdim == dim) break; /* traversal complete */          \
        for (curdim--; curdim >= 0; curdim--) {                        \
          srcptr_start[curdim] = psrc; /* save updated row starts */   \
          if (GASNETE_STRIDED_HELPER_HAVEDST)                          \
            dstptr_start[curdim] = pdst;                               \
        }                                                              \
      }                                                                \
      if (GASNETE_STRIDED_HELPER_HAVEPARTIAL &&                        \
          _gasnete_strided_update_addr_init) {                         \
        if (curdim == dim) { /* end of traversal */                    \
          psrc += _srcstrides[dim-2];                                  \
          if (GASNETE_STRIDED_HELPER_HAVEDST)                          \
            pdst += _dststrides[dim-2];                                \
        }                                                              \
        srcaddr = psrc;                                                \
        if (GASNETE_STRIDED_HELPER_HAVEDST) dstaddr = pdst;            \
        for (curdim = 0; curdim < dim; curdim++) {                     \
          gasneti_assert(idx[curdim] < _count[curdim]);                \
          _gasnete_strided_init[curdim] = idx[curdim];                 \
        }                                                              \
      }                                                                \
      if (dim > GASNETE_DIRECT_DIMS) {                                 \
        gasneti_free(idx);                                             \
        gasneti_free(srcptr_start);                                    \
        if (GASNETE_STRIDED_HELPER_HAVEDST)                            \
          gasneti_free(dstptr_start);                                  \
      }                                                                \
    } /* default */                                                    \
  } /* switch */                                                       \
} while (0)

/*---------------------------------------------------------------------------------*/
/* reference version that uses individual puts of the dualcontiguity size */
gasnet_handle_t gasnete_puts_ref_indiv(gasnete_strided_stats_t const *stats, gasnete_synctype_t synctype,
                                  gasnet_node_t dstnode,
                                   void *dstaddr, const size_t dststrides[],
                                   void *srcaddr, const size_t srcstrides[],
                                   const size_t count[], size_t stridelevels GASNETE_THREAD_FARG) {
  const int islocal = (dstnode == gasneti_mynode);
  size_t const contiglevel = stats->dualcontiguity;
  GASNETI_TRACE_EVENT(C, PUTS_REF_INDIV);
  gasneti_assert(!gasnete_strided_empty(count, stridelevels));
  GASNETE_START_NBIREGION(synctype, islocal);

  if (contiglevel == stridelevels) { /* fully contiguous at both ends */
    GASNETE_PUT_INDIV(islocal, dstnode, dstaddr, srcaddr, stats->totalsz);
  } else {
    size_t const limit = stridelevels - stats->nulldims;
    size_t const contigsz = MIN(stats->srccontigsz, stats->dstcontigsz);

    #define GASNETE_STRIDED_HELPER_LOOPBODY(psrc,pdst)  \
      GASNETE_PUT_INDIV(islocal, dstnode, pdst, psrc, contigsz)
    GASNETE_STRIDED_HELPER(limit,contiglevel);
    #undef GASNETE_STRIDED_HELPER_LOOPBODY
  }
  GASNETE_END_NBIREGION_AND_RETURN(synctype, islocal);
}
/* reference version that uses individual gets of the dualcontiguity size */
gasnet_handle_t gasnete_gets_ref_indiv(gasnete_strided_stats_t const *stats, gasnete_synctype_t synctype,
                                   void *dstaddr, const size_t dststrides[],
                                   gasnet_node_t srcnode, 
                                   void *srcaddr, const size_t srcstrides[],
                                   const size_t count[], size_t stridelevels GASNETE_THREAD_FARG) {
  const int islocal = (srcnode == gasneti_mynode);
  size_t const contiglevel = stats->dualcontiguity;
  GASNETI_TRACE_EVENT(C, GETS_REF_INDIV);
  gasneti_assert(!gasnete_strided_empty(count, stridelevels));
  GASNETE_START_NBIREGION(synctype, islocal);

  if (contiglevel == stridelevels) { /* fully contiguous at both ends */
    GASNETE_GET_INDIV(islocal, dstaddr, srcnode, srcaddr, stats->totalsz);
  } else {
    size_t const limit = stridelevels - stats->nulldims;
    size_t const contigsz = MIN(stats->srccontigsz, stats->dstcontigsz);

    #define GASNETE_STRIDED_HELPER_LOOPBODY(psrc,pdst)  \
      GASNETE_GET_INDIV(islocal, pdst, srcnode, psrc, contigsz)
    GASNETE_STRIDED_HELPER(limit,contiglevel);
    #undef GASNETE_STRIDED_HELPER_LOOPBODY
  }
  GASNETE_END_NBIREGION_AND_RETURN(synctype, islocal);
}
/*---------------------------------------------------------------------------------*/
/* strided full packing */

#define _GASNETE_STRIDED_PACKALL() {                                                   \
  uint8_t *ploc = buf;                                                                 \
  size_t const contiglevel = gasnete_strided_contiguity(strides, count, stridelevels); \
  size_t const limit = stridelevels - gasnete_strided_nulldims(count, stridelevels);   \
  size_t const contigsz = (contiglevel == 0 ? count[0] :                               \
                           count[contiglevel]*strides[contiglevel-1]);                 \
  /* macro interface */                                                                \
  void * srcaddr = addr;                                                               \
  size_t const * const srcstrides = strides;                                           \
  GASNETE_STRIDED_HELPER_DECLARE_NODST;                                                \
  GASNETE_STRIDED_HELPER(limit,contiglevel);                                           \
}

#define GASNETE_STRIDED_HELPER_LOOPBODY(psrc,pdst)  do { \
  GASNETE_FAST_UNALIGNED_MEMCPY(ploc, psrc, contigsz);   \
  ploc += contigsz;                                      \
} while (0)
void gasnete_strided_pack_all(void *addr, const size_t strides[],
                              const size_t count[], size_t stridelevels, 
                              void *buf) _GASNETE_STRIDED_PACKALL()
#undef GASNETE_STRIDED_HELPER_LOOPBODY

#define GASNETE_STRIDED_HELPER_LOOPBODY(psrc,pdst)  do { \
  GASNETE_FAST_UNALIGNED_MEMCPY(psrc, ploc, contigsz);   \
  ploc += contigsz;                                      \
} while (0)
void gasnete_strided_unpack_all(void *addr, const size_t strides[],
                                const size_t count[], size_t stridelevels, 
                                void *buf) _GASNETE_STRIDED_PACKALL()
#undef GASNETE_STRIDED_HELPER_LOOPBODY

/*---------------------------------------------------------------------------------*/
/* strided partial packing */
#define _GASNETE_STRIDED_PACKPARTIAL_INNER(_contiglevel,_limit) {                                    \
  size_t const contiglevel = (_contiglevel);                                                         \
  size_t const limit = (_limit);                                                                     \
  size_t const contigsz = (contiglevel == 0 ? count[0] : count[contiglevel]*strides[contiglevel-1]); \
  uint8_t *ploc = buf;                                                                               \
  /* macro interface */                                                                              \
  void *srcaddr = *addr;                                                                             \
  size_t const * const srcstrides = strides;                                                         \
  GASNETE_STRIDED_HELPER_DECLARE_NODST;                                                              \
  GASNETE_STRIDED_HELPER_DECLARE_PARTIAL(numchunks,init,addr_already_offset,update_addr_init);       \
  GASNETE_STRIDED_HELPER(limit,contiglevel);                                                         \
  if (update_addr_init) *addr = srcaddr;                                                             \
  return ploc;                                                                                       \
}
#if 0
#define _GASNETE_STRIDED_PACKPARTIAL()                                                       \
  _GASNETE_STRIDED_PACKPARTIAL_INNER(                                                        \
     /*limit=*/ stridelevels - gasnete_strided_nulldims(count, stridelevels),                \
     /*contiglevel=*/ gasnete_strided_contiguity(strides, count, stridelevels),              \
     /*contigsz=*/ (contiglevel == 0 ? count[0] : count[contiglevel]*strides[contiglevel-1]) \
  )
#endif
/* if addr_already_offset is nonzero, the code assumes srcaddr/dstaddr already reference the first chunk,
    otherwise, the srcaddr/dstaddr values are offset based on init to reach the first chunk
   iff update_addr_init is nonzero, then srcaddr/dstaddr/init are updated on exit to point to the next unused chunk
   foldedstrided variants operate on a "folded" strided metadata - one where the nulldims have been 
    removed, and all contiguous trailing dimensions have been folded into count[0] 
 */
#define GASNETE_STRIDED_HELPER_LOOPBODY(psrc,pdst)  do { \
  GASNETE_FAST_UNALIGNED_MEMCPY(ploc, psrc, contigsz);   \
  ploc += contigsz;                                      \
} while (0)
void *gasnete_strided_pack_partial(void **addr, const size_t strides[],
                              const size_t count[], size_t __contiglevel, size_t __limit, 
                              size_t numchunks, size_t init[], 
                              int addr_already_offset, int update_addr_init,
                              void *buf) _GASNETE_STRIDED_PACKPARTIAL_INNER(__contiglevel, __limit)
void *gasnete_foldedstrided_pack_partial(void **addr, const size_t strides[],
                              const size_t count[], size_t stridelevels, 
                              size_t numchunks, size_t init[], 
                              int addr_already_offset, int update_addr_init,
                              void *buf) {
  gasneti_assert(gasnete_strided_contiguity(strides, count, stridelevels) == 0);
  gasneti_assert(gasnete_strided_nulldims(count, stridelevels) == 0);
  _GASNETE_STRIDED_PACKPARTIAL_INNER(0, stridelevels)
}
#undef GASNETE_STRIDED_HELPER_LOOPBODY

#define GASNETE_STRIDED_HELPER_LOOPBODY(psrc,pdst)  do { \
  GASNETE_FAST_UNALIGNED_MEMCPY(psrc, ploc, contigsz);   \
  ploc += contigsz;                                      \
} while (0)
void *gasnete_strided_unpack_partial(void **addr, const size_t strides[],
                              const size_t count[], size_t __contiglevel, size_t __limit, 
                              size_t numchunks, size_t init[], 
                              int addr_already_offset, int update_addr_init,
                              void *buf) _GASNETE_STRIDED_PACKPARTIAL_INNER(__contiglevel, __limit)
void *gasnete_foldedstrided_unpack_partial(void **addr, const size_t strides[],
                              const size_t count[], size_t stridelevels, 
                              size_t numchunks, size_t init[], 
                              int addr_already_offset, int update_addr_init,
                              void *buf) {
  gasneti_assert(gasnete_strided_contiguity(strides, count, stridelevels) == 0);
  gasneti_assert(gasnete_strided_nulldims(count, stridelevels) == 0);
  _GASNETE_STRIDED_PACKPARTIAL_INNER(0,stridelevels)
}
#undef GASNETE_STRIDED_HELPER_LOOPBODY

/*---------------------------------------------------------------------------------*/
/* convert strided metadata to memvec or addrlist metadata for the equivalent operation */
static void gasnete_convert_strided(const int tomemvec, void *_srclist, void *_dstlist, 
                                    gasnete_strided_stats_t const *stats,
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
      #define GASNETE_STRIDED_HELPER_LOOPBODY(psrc,pdst)  do { \
        *(srcpos) = psrc;                                      \
        srcpos++;                                              \
        *(dstpos) = pdst;                                      \
        dstpos++;                                              \
      } while(0)
      GASNETE_STRIDED_HELPER(limit,contiglevel);
      #undef GASNETE_STRIDED_HELPER_LOOPBODY
    } else if (srccontigsz < dstcontigsz) {
      size_t const looplim = dstcontigsz / srccontigsz;
      size_t loopcnt = 0;
      gasneti_assert(looplim*srccontigsz == dstcontigsz);
      /* TODO: this loop could be made more efficient */
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
      GASNETE_STRIDED_HELPER(limit,contiglevel);
      #undef GASNETE_STRIDED_HELPER_LOOPBODY
    } else { /* srccontigsz > dstcontigsz */
      size_t const looplim = srccontigsz / dstcontigsz;
      size_t loopcnt = 0;
      gasneti_assert(looplim*dstcontigsz == srccontigsz);
      /* TODO: this loop could be made more efficient */
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
      GASNETE_STRIDED_HELPER(limit,contiglevel);
      #undef GASNETE_STRIDED_HELPER_LOOPBODY
    }
    gasneti_assert(srcpos == srclist+stats->srcsegments);
    gasneti_assert(dstpos == dstlist+stats->dstsegments);
  } else { /* memvec case */
    gasnet_memvec_t * const srclist = (gasnet_memvec_t *)_srclist;
    gasnet_memvec_t * const dstlist = (gasnet_memvec_t *)_dstlist;
    gasnet_memvec_t * srcpos = srclist;
    gasnet_memvec_t * dstpos = dstlist;

    if (srccontigsz == dstcontigsz) {
      #define GASNETE_STRIDED_HELPER_LOOPBODY(psrc,pdst)  do { \
        srcpos->addr = psrc;                                   \
        srcpos->len = srccontigsz;                             \
        srcpos++;                                              \
        dstpos->addr = pdst;                                   \
        dstpos->len = dstcontigsz;                             \
        dstpos++;                                              \
      } while(0)
      GASNETE_STRIDED_HELPER(limit,contiglevel);
      #undef GASNETE_STRIDED_HELPER_LOOPBODY
    } else if (srccontigsz < dstcontigsz) {
      size_t const looplim = dstcontigsz / srccontigsz;
      size_t loopcnt = 0;
      gasneti_assert(looplim*srccontigsz == dstcontigsz);
      /* TODO: this loop could be made more efficient */
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
      GASNETE_STRIDED_HELPER(limit,contiglevel);
      #undef GASNETE_STRIDED_HELPER_LOOPBODY
    } else { /* srccontigsz > dstcontigsz */
      size_t const looplim = srccontigsz / dstcontigsz;
      size_t loopcnt = 0;
      gasneti_assert(looplim*dstcontigsz == srccontigsz);
      /* TODO: this loop could be made more efficient */
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
      GASNETE_STRIDED_HELPER(limit,contiglevel);
      #undef GASNETE_STRIDED_HELPER_LOOPBODY
    }
    gasneti_assert(srcpos == srclist+stats->srcsegments);
    gasneti_assert(dstpos == dstlist+stats->dstsegments);
  }
}
/*---------------------------------------------------------------------------------*/
/* simple gather put, remotely contiguous */
#ifndef GASNETE_PUTS_GATHER_SELECTOR
#if GASNETE_USE_REMOTECONTIG_GATHER_SCATTER
gasnet_handle_t gasnete_puts_gather(gasnete_strided_stats_t const *stats, gasnete_synctype_t synctype,
                                    gasnet_node_t dstnode,
                                    void *dstaddr, const size_t dststrides[],
                                    void *srcaddr, const size_t srcstrides[],
                                    const size_t count[], size_t stridelevels GASNETE_THREAD_FARG) {
  gasnete_vis_threaddata_t * const td = GASNETE_VIS_MYTHREAD;
  size_t const nbytes = stats->totalsz;
  gasneti_assert(stats->dstcontiguity == stridelevels && stats->srccontiguity < stridelevels); /* only supports gather put */
  gasneti_assert(dstnode != gasneti_mynode); /* silly to use for local cases */
  gasneti_assert(nbytes > 0);
  gasneti_assert(stats->totalsz == (size_t)stats->totalsz); /* check for size_t truncation */
  GASNETI_TRACE_EVENT(C, PUTS_GATHER);

  { gasneti_vis_op_t * const visop = gasneti_malloc(sizeof(gasneti_vis_op_t)+nbytes);
    void * const packedbuf = visop + 1;
    gasnete_strided_pack_all(srcaddr, srcstrides, count, stridelevels, packedbuf);
    visop->type = GASNETI_VIS_CAT_PUTS_GATHER;
    visop->handle = gasnete_put_nb_bulk(dstnode, dstaddr, packedbuf, nbytes GASNETE_THREAD_PASS);
    GASNETE_PUSH_VISOP_RETURN(td, visop, synctype, 0);
  }
}
  #define GASNETE_PUTS_GATHER_SELECTOR(stats,synctype,dstnode,dstaddr,dststrides,srcaddr,srcstrides,count,stridelevels) \
    if ((stats)->dstcontiguity == stridelevels && (stats)->srccontiguity < stridelevels)                                \
      return gasnete_puts_gather(stats,synctype,dstnode,dstaddr,dststrides,srcaddr,srcstrides,count,stridelevels GASNETE_THREAD_PASS)
#else
  #define GASNETE_PUTS_GATHER_SELECTOR(stats,synctype,dstnode,dstaddr,dststrides,srcaddr,srcstrides,count,stridelevels) ((void)0)
#endif
#endif

/* simple scatter get, remotely contiguous */
#ifndef GASNETE_GETS_SCATTER_SELECTOR
#if GASNETE_USE_REMOTECONTIG_GATHER_SCATTER
gasnet_handle_t gasnete_gets_scatter(gasnete_strided_stats_t const *stats, gasnete_synctype_t synctype,
                                     void *dstaddr, const size_t dststrides[],
                                     gasnet_node_t srcnode, 
                                     void *srcaddr, const size_t srcstrides[],
                                     const size_t count[], size_t stridelevels GASNETE_THREAD_FARG) {
  gasnete_vis_threaddata_t * const td = GASNETE_VIS_MYTHREAD;
  size_t const nbytes = stats->totalsz;
  gasneti_assert(stats->srccontiguity == stridelevels && stats->dstcontiguity < stridelevels); /* only supports scatter get */
  gasneti_assert(srcnode != gasneti_mynode); /* silly to use for local cases */
  gasneti_assert(nbytes > 0);
  gasneti_assert(stats->totalsz == (size_t)stats->totalsz); /* check for size_t truncation */
  GASNETI_TRACE_EVENT(C, GETS_SCATTER);

  { gasneti_vis_op_t * const visop = gasneti_malloc(sizeof(gasneti_vis_op_t)+(2*stridelevels+1)*sizeof(size_t)+nbytes);
    size_t * const savedstrides = (size_t *)(visop + 1);
    size_t * const savedcount = savedstrides + stridelevels;
    void * const packedbuf = (void *)(savedcount + stridelevels + 1);
    memcpy(savedstrides, dststrides, stridelevels*sizeof(size_t));
    memcpy(savedcount, count, (stridelevels+1)*sizeof(size_t));
    visop->type = GASNETI_VIS_CAT_GETS_SCATTER;
    visop->addr = dstaddr;
    visop->len = stridelevels;
    visop->handle = gasnete_get_nb_bulk(packedbuf, srcnode, srcaddr, nbytes GASNETE_THREAD_PASS);
    GASNETE_PUSH_VISOP_RETURN(td, visop, synctype, 1);
  }
}
  #define GASNETE_GETS_SCATTER_SELECTOR(stats,synctype,dstaddr,dststrides,srcnode,srcaddr,srcstrides,count,stridelevels) \
    if ((stats)->srccontiguity == stridelevels && (stats)->dstcontiguity < stridelevels)                                 \
      return gasnete_gets_scatter(stats,synctype,dstaddr,dststrides,srcnode,srcaddr,srcstrides,count,stridelevels GASNETE_THREAD_PASS)
#else
  #define GASNETE_GETS_SCATTER_SELECTOR(stats,synctype,dstaddr,dststrides,srcnode,srcaddr,srcstrides,count,stridelevels) ((void)0)
#endif
#endif
/*---------------------------------------------------------------------------------*/
/* Pipelined AM gather-scatter put */
#ifndef GASNETE_PUTS_AMPIPELINE_SELECTOR
#if GASNETE_USE_AMPIPELINE
#define GASNETE_PUTS_AMPIPELINE_MAXPAYLOAD(stridelevels) (gasnet_AMMaxMedium() - (3*(stridelevels) + 1)*sizeof(size_t))
gasnet_handle_t gasnete_puts_AMPipeline(gasnete_strided_stats_t const *stats, gasnete_synctype_t synctype,
                                  gasnet_node_t dstnode,
                                   void *dstaddr, const size_t dststrides[],
                                   void *srcaddr, const size_t srcstrides[],
                                   const size_t count[], size_t stridelevels GASNETE_THREAD_FARG) {
  gasneti_assert(stats->dstsegments > 1); /* supports scatter put */
  gasneti_assert(dstnode != gasneti_mynode); /* silly to use for local cases */
  GASNETI_TRACE_EVENT(C, PUTS_AMPIPELINE);
  GASNETE_START_NBIREGION(synctype, 0);

  { size_t * const init = gasneti_malloc(stridelevels*sizeof(size_t) + gasnet_AMMaxMedium());
    size_t * const packetbase = init + stridelevels;
    size_t * const packetinit = packetbase;
    size_t * const packetcount = packetinit + stridelevels;
    size_t * const packetstrides = packetcount + stridelevels + 1;
    size_t * const packedbuf = packetstrides + stridelevels;
    size_t const maxpayload = GASNETE_PUTS_AMPIPELINE_MAXPAYLOAD(stridelevels);
    size_t const packetoverhead = gasnet_AMMaxMedium() - maxpayload;
    size_t const chunksz = stats->dualcontigsz;
    size_t const totalchunks = MAX(stats->srcsegments,stats->dstsegments);
    size_t const chunksperpacket = maxpayload / chunksz;
    size_t const packetcnt = (totalchunks + chunksperpacket - 1)/chunksperpacket;
    size_t remaining = totalchunks;
    gasneti_iop_t *iop = gasneti_iop_register(packetcnt,0 GASNETE_THREAD_PASS);
    gasneti_assert(chunksz*totalchunks == stats->totalsz);
    gasneti_assert(chunksperpacket >= 1);
    memset(init, 0, stridelevels*sizeof(size_t)); /* init[] = [0..0] */
    memcpy(packetcount, count, (stridelevels+1)*sizeof(size_t));
    memcpy(packetstrides, dststrides, stridelevels*sizeof(size_t));
    while (remaining) {
      size_t const packetchunks = MIN(chunksperpacket, remaining);
      size_t * const adjinit = init+stats->dualcontiguity;
      uint8_t *end;
      size_t nbytes;
      remaining -= packetchunks;
      memcpy(packetinit, init, stridelevels*sizeof(size_t));
      if (stats->srccontiguity < stridelevels) { /* gather data payload from source into packet */
        end = gasnete_strided_pack_partial(&srcaddr, srcstrides, count, 
                                     stats->dualcontiguity, stridelevels - stats->nulldims, 
                                     packetchunks, adjinit, 
                                     1, remaining, packedbuf);
        nbytes = end - (uint8_t *)packetbase;
        gasneti_assert((end - (uint8_t *)packedbuf) == packetchunks * chunksz);
        gasneti_assert((end - (uint8_t *)packedbuf) <= GASNETE_PUTS_AMPIPELINE_MAXPAYLOAD(stridelevels));
        gasneti_assert((end - (uint8_t *)packedbuf) + packetoverhead == nbytes);
        #if GASNET_DEBUG
          if (remaining) {
            size_t * const tmp = gasneti_malloc(stridelevels*sizeof(size_t));
            memcpy(tmp, packetinit, stridelevels*sizeof(size_t));
            GASNETE_STRIDED_VECTOR_INC(tmp, packetchunks*chunksz/count[0], count, 0, stridelevels);
            gasneti_assert(!memcmp(tmp, init, stridelevels*sizeof(size_t)));
            gasneti_free(tmp);
          }
        #endif
      } else { /* source is contiguous */
        nbytes = packetchunks*chunksz;
        memcpy(packedbuf, srcaddr, nbytes);
        srcaddr = ((uint8_t *)srcaddr) + nbytes;
        if (remaining) GASNETE_STRIDED_VECTOR_INC(init, nbytes/count[0], count, 0, stridelevels);
        nbytes += packetoverhead;
      }
      /* fill packet with remote metadata */
      GASNETI_SAFE(
        MEDIUM_REQ(5,7,(dstnode, gasneti_handleridx(gasnete_puts_AMPipeline_reqh),
                      packetbase, nbytes,
                      PACK(iop), PACK(dstaddr), stridelevels, stats->dualcontiguity, packetchunks)));
    }
    gasneti_free(init);
    GASNETE_END_NBIREGION_AND_RETURN(synctype, 0);
  }
}
  #define GASNETE_PUTS_AMPIPELINE_SELECTOR(stats,synctype,dstnode,dstaddr,dststrides,srcaddr,srcstrides,count,stridelevels) \
    if ((stats)->dstsegments > 1 && (stats)->dualcontigsz <= GASNETE_PUTS_AMPIPELINE_MAXPAYLOAD(stridelevels))              \
      return gasnete_puts_AMPipeline(stats,synctype,dstnode,dstaddr,dststrides,srcaddr,srcstrides,count,stridelevels GASNETE_THREAD_PASS)
#else
  #define GASNETE_PUTS_AMPIPELINE_SELECTOR(stats,synctype,dstnode,dstaddr,dststrides,srcaddr,srcstrides,count,stridelevels) ((void)0)
#endif
#endif
/* ------------------------------------------------------------------------------------ */
#if GASNETE_USE_AMPIPELINE
GASNETI_INLINE(gasnete_puts_AMPipeline_reqh_inner)
void gasnete_puts_AMPipeline_reqh_inner(gasnet_token_t token, 
  void *addr, size_t nbytes,
  void *iop, void *dstaddr, 
  gasnet_handlerarg_t stridelevels, gasnet_handlerarg_t contiglevel, 
  gasnet_handlerarg_t packetchunks) {
  size_t * const packetinit = addr;
  size_t * const packetcount = packetinit + stridelevels;
  size_t * const packetstrides = packetcount + stridelevels + 1;
  size_t * const packedbuf = packetstrides + stridelevels;
  size_t const limit = stridelevels - gasnete_strided_nulldims(packetcount, stridelevels);
  uint8_t * const end = gasnete_strided_unpack_partial(&dstaddr, packetstrides, packetcount, contiglevel, limit,
                                                       packetchunks, packetinit+contiglevel, 0, 0, packedbuf);
  gasneti_assert(end - (uint8_t *)addr == nbytes);
  gasneti_sync_writes();
  /* TODO: coalesce acknowledgements - need a per-srcnode, per-op seqnum & packetcnt */
  GASNETI_SAFE(
    SHORT_REP(1,2,(token, gasneti_handleridx(gasnete_putvis_AMPipeline_reph),
                  PACK(iop))));
}
MEDIUM_HANDLER(gasnete_puts_AMPipeline_reqh,5,7, 
              (token,addr,nbytes, UNPACK(a0),      UNPACK(a1),      a2,a3,a4),
              (token,addr,nbytes, UNPACK2(a0, a1), UNPACK2(a2, a3), a4,a5,a6));
#endif
/*---------------------------------------------------------------------------------*/
/* Pipelined AM gather-scatter get */
#ifndef GASNETE_GETS_AMPIPELINE_SELECTOR
#if GASNETE_USE_AMPIPELINE
#define GASNETE_GETS_AMPIPELINE_MAXPAYLOAD(stridelevels) (gasnet_AMMaxMedium())
gasnet_handle_t gasnete_gets_AMPipeline(gasnete_strided_stats_t const *stats, gasnete_synctype_t synctype,
                                   void *dstaddr, const size_t dststrides[],
                                   gasnet_node_t srcnode, 
                                   void *srcaddr, const size_t srcstrides[],
                                   const size_t count[], size_t stridelevels GASNETE_THREAD_FARG) {
  gasneti_assert(stats->srcsegments > 1); /* supports gather get */
  gasneti_assert(srcnode != gasneti_mynode); /* silly to use for local cases */
  GASNETI_TRACE_EVENT(C, GETS_AMPIPELINE);

  { size_t const chunksz = stats->dualcontigsz;
    size_t const adjchunksz = stats->dualcontigsz/count[0];
    size_t const totalchunks = MAX(stats->srcsegments,stats->dstsegments);
    size_t const chunksperpacket = gasnet_AMMaxMedium() / chunksz;
    size_t const packetcnt = (totalchunks + chunksperpacket - 1)/chunksperpacket;
    size_t const packetnbytes = (3*stridelevels+1)*sizeof(size_t);
    size_t packetidx;

    gasneti_vis_op_t * const visop = gasneti_malloc(sizeof(gasneti_vis_op_t) +
                                                   (2*stridelevels + 1)*sizeof(size_t) + /* tablecount, tablestrides */
                                                   packetcnt*stridelevels*sizeof(size_t) + /* tableinit */
                                                   packetnbytes); /* packet metadata */
    size_t * const tablebase = (size_t *)(visop + 1);
    size_t * const tablecount = tablebase;
    size_t * const tablestrides = tablecount + stridelevels + 1;
    size_t * tableinit = tablestrides + stridelevels;
    size_t * const packetbase = tableinit + packetcnt*stridelevels;
    size_t * const packetinit = packetbase;
    size_t * const packetcount = packetinit + stridelevels;
    size_t * const packetstrides = packetcount + stridelevels + 1;
    size_t remaining = totalchunks;

    gasneti_assert(chunksz*totalchunks == stats->totalsz);
    gasneti_assert(chunksperpacket >= 1);

    GASNETE_VISOP_SETUP(visop, synctype, 1);
    visop->addr = dstaddr;
    visop->count = stridelevels;
    #if GASNET_DEBUG
      visop->type = GASNETI_VIS_CAT_GETS_AMPIPELINE;
    #endif
    gasneti_assert(packetcnt <= GASNETI_ATOMIC_MAX);
    gasneti_assert(packetcnt == (gasnet_handlerarg_t)packetcnt);
    gasneti_weakatomic_set(&(visop->packetcnt), packetcnt, GASNETI_ATOMIC_WMB_POST);

    memcpy(tablecount, count, (stridelevels+1)*sizeof(size_t)); /* TODO: merge with packetcount? */
    memcpy(packetcount, count, (stridelevels+1)*sizeof(size_t)); 
    memcpy(tablestrides, dststrides, stridelevels*sizeof(size_t));
    memcpy(packetstrides, srcstrides, stridelevels*sizeof(size_t));
    memset(tableinit, 0, stridelevels*sizeof(size_t)); /* init[] = [0..0] */

    for (packetidx = 0; packetidx < packetcnt; packetidx++) {
      size_t const packetchunks = MIN(chunksperpacket, remaining);
      size_t * const nexttableinit = tableinit + stridelevels;
      size_t const adjnbytes = packetchunks*adjchunksz;
      remaining -= packetchunks;
      memcpy(packetinit, tableinit, stridelevels*sizeof(size_t));
      GASNETI_SAFE(
        MEDIUM_REQ(6,8,(srcnode, gasneti_handleridx(gasnete_gets_AMPipeline_reqh),
                      packetbase, packetnbytes,
                      PACK(visop), PACK(srcaddr), stridelevels, stats->dualcontiguity, packetchunks, packetidx)));

      if (remaining) {
        memcpy(nexttableinit, tableinit, stridelevels*sizeof(size_t));
        GASNETE_STRIDED_VECTOR_INC(nexttableinit, adjnbytes, count, 0, stridelevels);
      }
      tableinit = nexttableinit;
    }
    gasneti_assert(remaining == 0);
    gasneti_assert(tableinit == packetbase);
    GASNETE_VISOP_RETURN(visop, synctype);
  }
}
  #define GASNETE_GETS_AMPIPELINE_SELECTOR(stats,synctype,dstaddr,dststrides,srcnode,srcaddr,srcstrides,count,stridelevels) \
    if ((stats)->srcsegments > 1 && (stats)->dualcontigsz <= gasnet_AMMaxMedium())                                          \
      return gasnete_gets_AMPipeline(stats,synctype,dstaddr,dststrides,srcnode,srcaddr,srcstrides,count,stridelevels GASNETE_THREAD_PASS)
#else
  #define GASNETE_GETS_AMPIPELINE_SELECTOR(stats,synctype,dstaddr,dststrides,srcnode,srcaddr,srcstrides,count,stridelevels) ((void)0)
#endif
#endif
/* ------------------------------------------------------------------------------------ */
#if GASNETE_USE_AMPIPELINE
GASNETI_INLINE(gasnete_gets_AMPipeline_reqh_inner)
void gasnete_gets_AMPipeline_reqh_inner(gasnet_token_t token, 
  void *addr, size_t nbytes,
  void *_visop, void *srcaddr, 
  gasnet_handlerarg_t stridelevels, gasnet_handlerarg_t contiglevel, 
  gasnet_handlerarg_t packetchunks, gasnet_handlerarg_t packetidx) {

  size_t * const packetinit = addr;
  size_t * const packetcount = packetinit + stridelevels;
  size_t * const packetstrides = packetcount + stridelevels + 1;
  size_t const limit = stridelevels - gasnete_strided_nulldims(packetcount, stridelevels);

  gasneti_assert((uint8_t *)(packetstrides+stridelevels) - (uint8_t *)addr == nbytes);
  gasneti_assert(gasnete_strided_contiguity(packetstrides, packetcount, stridelevels) >= contiglevel);
  gasneti_assert(contiglevel < limit);
  #if GASNET_DEBUG
  { size_t i;
    size_t chunksz = packetcount[0];
    for (i = 0; i < stridelevels; i++) {
      gasneti_assert(packetinit[i] < packetcount[i+1]);
      if (i < contiglevel) chunksz *= packetcount[i+1];
    }
    gasneti_assert(packetchunks * chunksz <= gasnet_AMMaxMedium());
  }
  #endif
  { /* gather data payload from source into packet */
    uint8_t * const packedbuf = gasneti_malloc(gasnet_AMMaxMedium());
    uint8_t * const end = gasnete_strided_pack_partial(&srcaddr, packetstrides, packetcount, 
                               contiglevel, limit, 
                               packetchunks, packetinit+contiglevel, 
                               0, 0, packedbuf);
    size_t nbytes = end - (uint8_t *)packedbuf;

    GASNETI_SAFE(
      MEDIUM_REP(4,5,(token, gasneti_handleridx(gasnete_gets_AMPipeline_reph),
                    packedbuf, nbytes,
                    PACK(_visop),packetidx,contiglevel,packetchunks)));
    gasneti_free(packedbuf);
  }
}
MEDIUM_HANDLER(gasnete_gets_AMPipeline_reqh,6,8, 
              (token,addr,nbytes, UNPACK(a0),      UNPACK(a1),      a2,a3,a4,a5),
              (token,addr,nbytes, UNPACK2(a0, a1), UNPACK2(a2, a3), a4,a5,a6,a7));
/* ------------------------------------------------------------------------------------ */
GASNETI_INLINE(gasnete_gets_AMPipeline_reph_inner)
void gasnete_gets_AMPipeline_reph_inner(gasnet_token_t token, 
  void *addr, size_t nbytes,
  void *_visop, gasnet_handlerarg_t packetidx,
  gasnet_handlerarg_t contiglevel, gasnet_handlerarg_t packetchunks) {
  gasneti_vis_op_t * const visop = _visop;
  void *dstaddr = visop->addr;
  size_t const stridelevels = visop->count;
  size_t * const tablebase = (size_t *)(visop + 1);
  size_t * const tablecount = tablebase;
  size_t * const tablestrides = tablecount + stridelevels + 1;
  size_t * const tableinit = tablestrides + stridelevels + stridelevels * packetidx;
  size_t const limit = stridelevels - gasnete_strided_nulldims(tablecount, stridelevels);

  gasneti_assert(visop->type == GASNETI_VIS_CAT_GETS_AMPIPELINE);

  { uint8_t * const end = gasnete_strided_unpack_partial(&dstaddr, tablestrides, tablecount, contiglevel, limit,
                                                       packetchunks, tableinit+contiglevel, 0, 0, addr);
    gasneti_assert(end - (uint8_t *)addr == nbytes);
  }
  
  if (gasneti_weakatomic_decrement_and_test(&(visop->packetcnt), GASNETI_ATOMIC_WMB_PRE)) {
    /* last response packet completes operation and cleans up */
    GASNETE_VISOP_SIGNAL(visop, 1);
    gasneti_free(visop); /* free visop, saved metadata and send buffer */
  }
}
MEDIUM_HANDLER(gasnete_gets_AMPipeline_reph,4,5, 
              (token,addr,nbytes, UNPACK(a0),      a1,a2,a3),
              (token,addr,nbytes, UNPACK2(a0, a1), a2,a3,a4));
#endif
/*---------------------------------------------------------------------------------*/
/* reference version that uses vector interface */
gasnet_handle_t gasnete_puts_ref_vector(gasnete_strided_stats_t const *stats, gasnete_synctype_t synctype,
                                  gasnet_node_t dstnode,
                                   void *dstaddr, const size_t dststrides[],
                                   void *srcaddr, const size_t srcstrides[],
                                   const size_t count[], size_t stridelevels GASNETE_THREAD_FARG) {
  GASNETI_TRACE_EVENT(C, PUTS_REF_VECTOR);
  gasneti_assert(!gasnete_strided_empty(count, stridelevels));
  gasneti_assert(GASNETE_PUTV_ALLOWS_VOLATILE_METADATA);

  if (stats->dualcontiguity == stridelevels) { /* fully contiguous at both ends */
    const int islocal = (dstnode == gasneti_mynode);
    gasneti_assert(stats->totalsz == (size_t)stats->totalsz); /* check for size_t truncation */
    GASNETE_START_NBIREGION(synctype, islocal);
      GASNETE_PUT_INDIV(islocal, dstnode, dstaddr, srcaddr, stats->totalsz);
    GASNETE_END_NBIREGION_AND_RETURN(synctype, islocal);
  } else {
    gasnet_handle_t retval;
    gasnet_memvec_t * const srclist = gasneti_malloc(sizeof(gasnet_memvec_t)*stats->srcsegments);
    gasnet_memvec_t * const dstlist = gasneti_malloc(sizeof(gasnet_memvec_t)*stats->dstsegments);

    gasnete_convert_strided(1, srclist, dstlist, stats, 
      dstaddr, dststrides, srcaddr, srcstrides, count, stridelevels);

    retval = gasnete_putv(synctype, dstnode, 
                          stats->dstsegments, dstlist, 
                          stats->srcsegments, srclist GASNETE_THREAD_PASS);
    gasneti_free(srclist);
    gasneti_free(dstlist);
    return retval; 
  }
}
/* reference version that uses vector interface */
gasnet_handle_t gasnete_gets_ref_vector(gasnete_strided_stats_t const *stats, gasnete_synctype_t synctype,
                                   void *dstaddr, const size_t dststrides[],
                                   gasnet_node_t srcnode, 
                                   void *srcaddr, const size_t srcstrides[],
                                   const size_t count[], size_t stridelevels GASNETE_THREAD_FARG) {
  GASNETI_TRACE_EVENT(C, GETS_REF_VECTOR);
  gasneti_assert(!gasnete_strided_empty(count, stridelevels));
  gasneti_assert(GASNETE_GETV_ALLOWS_VOLATILE_METADATA);

  if (stats->dualcontiguity == stridelevels) { /* fully contiguous at both ends */
    const int islocal = (srcnode == gasneti_mynode);
    gasneti_assert(stats->totalsz == (size_t)stats->totalsz); /* check for size_t truncation */
    GASNETE_START_NBIREGION(synctype, islocal);
      GASNETE_GET_INDIV(islocal, dstaddr, srcnode, srcaddr, stats->totalsz);
    GASNETE_END_NBIREGION_AND_RETURN(synctype, islocal);
  } else {
    gasnet_handle_t retval;
    gasnet_memvec_t * const srclist = gasneti_malloc(sizeof(gasnet_memvec_t)*stats->srcsegments);
    gasnet_memvec_t * const dstlist = gasneti_malloc(sizeof(gasnet_memvec_t)*stats->dstsegments);

    gasnete_convert_strided(1, srclist, dstlist, stats, 
      dstaddr, dststrides, srcaddr, srcstrides, count, stridelevels);

    retval = gasnete_getv(synctype, 
                          stats->dstsegments, dstlist, 
                          srcnode,
                          stats->srcsegments, srclist GASNETE_THREAD_PASS);
    gasneti_free(srclist);
    gasneti_free(dstlist);
    return retval; 
  }
}
/*---------------------------------------------------------------------------------*/
/* reference version that uses indexed interface */
gasnet_handle_t gasnete_puts_ref_indexed(gasnete_strided_stats_t const *stats, gasnete_synctype_t synctype,
                                  gasnet_node_t dstnode,
                                   void *dstaddr, const size_t dststrides[],
                                   void *srcaddr, const size_t srcstrides[],
                                   const size_t count[], size_t stridelevels GASNETE_THREAD_FARG) {
  GASNETI_TRACE_EVENT(C, PUTS_REF_INDEXED);
  gasneti_assert(!gasnete_strided_empty(count, stridelevels));
  gasneti_assert(GASNETE_PUTI_ALLOWS_VOLATILE_METADATA);

  if (stats->dualcontiguity == stridelevels) { /* fully contiguous at both ends */
    const int islocal = (dstnode == gasneti_mynode);
    gasneti_assert(stats->totalsz == (size_t)stats->totalsz); /* check for size_t truncation */
    GASNETE_START_NBIREGION(synctype, islocal);
      GASNETE_PUT_INDIV(islocal, dstnode, dstaddr, srcaddr, stats->totalsz);
    GASNETE_END_NBIREGION_AND_RETURN(synctype, islocal);
  } else {
    gasnet_handle_t retval;
    void * * const srclist = gasneti_malloc(sizeof(void *)*stats->srcsegments);
    void * * const dstlist = gasneti_malloc(sizeof(void *)*stats->dstsegments);

    gasnete_convert_strided(0, srclist, dstlist, stats, 
      dstaddr, dststrides, srcaddr, srcstrides, count, stridelevels);

    retval = gasnete_puti(synctype, dstnode, 
                          stats->dstsegments, dstlist, stats->dstcontigsz,
                          stats->srcsegments, srclist, stats->srccontigsz GASNETE_THREAD_PASS);
    gasneti_free(srclist);
    gasneti_free(dstlist);
    return retval; 
  }
}
/* reference version that uses indexed interface */
gasnet_handle_t gasnete_gets_ref_indexed(gasnete_strided_stats_t const *stats, gasnete_synctype_t synctype,
                                   void *dstaddr, const size_t dststrides[],
                                   gasnet_node_t srcnode, 
                                   void *srcaddr, const size_t srcstrides[],
                                   const size_t count[], size_t stridelevels GASNETE_THREAD_FARG) {
  GASNETI_TRACE_EVENT(C, GETS_REF_INDEXED);
  gasneti_assert(!gasnete_strided_empty(count, stridelevels));
  gasneti_assert(GASNETE_GETI_ALLOWS_VOLATILE_METADATA);

  if (stats->dualcontiguity == stridelevels) { /* fully contiguous at both ends */
    const int islocal = (srcnode == gasneti_mynode);
    gasneti_assert(stats->totalsz == (size_t)stats->totalsz); /* check for size_t truncation */
    GASNETE_START_NBIREGION(synctype, islocal);
      GASNETE_GET_INDIV(islocal, dstaddr, srcnode, srcaddr, stats->totalsz);
    GASNETE_END_NBIREGION_AND_RETURN(synctype, islocal);
  } else {
    gasnet_handle_t retval;
    void * * const srclist = gasneti_malloc(sizeof(void *)*stats->srcsegments);
    void * * const dstlist = gasneti_malloc(sizeof(void *)*stats->dstsegments);

    gasnete_convert_strided(0, srclist, dstlist, stats, 
      dstaddr, dststrides, srcaddr, srcstrides, count, stridelevels);

    retval = gasnete_geti(synctype,
                          stats->dstsegments, dstlist, stats->dstcontigsz,
                          srcnode,
                          stats->srcsegments, srclist, stats->srccontigsz GASNETE_THREAD_PASS);
    gasneti_free(srclist);
    gasneti_free(dstlist);
    return retval; 
  }
}
/*---------------------------------------------------------------------------------*/
/* top-level gasnet_puts_* entry point */
#ifndef GASNETE_PUTS_OVERRIDE
extern gasnet_handle_t gasnete_puts(gasnete_synctype_t synctype,
                                  gasnet_node_t dstnode,
                                   void *dstaddr, const size_t dststrides[],
                                   void *srcaddr, const size_t srcstrides[],
                                   const size_t count[], size_t stridelevels GASNETE_THREAD_FARG) {
  gasnete_strided_stats_t stats;
  gasnete_strided_stats(&stats, dststrides, srcstrides, count, stridelevels);

  /* catch silly degenerate cases */
  if_pf (stats.totalsz == 0) /* empty */
    return GASNET_INVALID_HANDLE;
  if_pf (dstnode == gasneti_mynode || /* purely local */ 
         stats.dualcontiguity == stridelevels) {/* fully contiguous */
    return gasnete_puts_ref_indiv(&stats,synctype,dstnode,dstaddr,dststrides,srcaddr,srcstrides,count,stridelevels GASNETE_THREAD_PASS);
  }

  /* select algorithm */
  #ifndef GASNETE_PUTS_SELECTOR
    #if GASNETE_RANDOM_SELECTOR
      #define GASNETE_PUTS_SELECTOR(stats,synctype,dstnode,dstaddr,dststrides,srcaddr,srcstrides,count,stridelevels) do {                         \
        switch (rand() % 4) {                                                                                                                     \
          case 0:                                                                                                                                 \
            GASNETE_PUTS_GATHER_SELECTOR(stats,synctype,dstnode,dstaddr,dststrides,srcaddr,srcstrides,count,stridelevels);                        \
          case 1:                                                                                                                                 \
            GASNETE_PUTS_AMPIPELINE_SELECTOR(stats,synctype,dstnode,dstaddr,dststrides,srcaddr,srcstrides,count,stridelevels);                    \
          case 2:                                                                                                                                 \
            return gasnete_puts_ref_indiv(stats,synctype,dstnode,dstaddr,dststrides,srcaddr,srcstrides,count,stridelevels GASNETE_THREAD_PASS);   \
          case 3:                                                                                                                                 \
            return gasnete_puts_ref_vector(stats,synctype,dstnode,dstaddr,dststrides,srcaddr,srcstrides,count,stridelevels GASNETE_THREAD_PASS);  \
          case 4:                                                                                                                                 \
            return gasnete_puts_ref_indexed(stats,synctype,dstnode,dstaddr,dststrides,srcaddr,srcstrides,count,stridelevels GASNETE_THREAD_PASS); \
        } } while (0)
    #else
      #define GASNETE_PUTS_SELECTOR(stats,synctype,dstnode,dstaddr,dststrides,srcaddr,srcstrides,count,stridelevels)       \
        GASNETE_PUTS_GATHER_SELECTOR(stats,synctype,dstnode,dstaddr,dststrides,srcaddr,srcstrides,count,stridelevels);     \
        GASNETE_PUTS_AMPIPELINE_SELECTOR(stats,synctype,dstnode,dstaddr,dststrides,srcaddr,srcstrides,count,stridelevels); \
        return gasnete_puts_ref_indiv(stats,synctype,dstnode,dstaddr,dststrides,srcaddr,srcstrides,count,stridelevels GASNETE_THREAD_PASS)
    #endif
  #endif
  GASNETE_PUTS_SELECTOR(&stats,synctype,dstnode,dstaddr,dststrides,srcaddr,srcstrides,count,stridelevels);
  gasneti_fatalerror("failure in GASNETE_PUTS_SELECTOR - should never reach here");
  return GASNET_INVALID_HANDLE; /* avoid warning on MIPSPro */
}
#endif
/* top-level gasnet_gets_* entry point */
#ifndef GASNETE_GETS_OVERRIDE
extern gasnet_handle_t gasnete_gets(gasnete_synctype_t synctype,
                                   void *dstaddr, const size_t dststrides[],
                                   gasnet_node_t srcnode, 
                                   void *srcaddr, const size_t srcstrides[],
                                   const size_t count[], size_t stridelevels GASNETE_THREAD_FARG) {
  gasnete_strided_stats_t stats;
  gasnete_strided_stats(&stats, dststrides, srcstrides, count, stridelevels);
  /* catch silly degenerate cases */
  if_pf (stats.totalsz == 0) /* empty */
    return GASNET_INVALID_HANDLE;
  if_pf (srcnode == gasneti_mynode || /* purely local */ 
         stats.dualcontiguity == stridelevels) {/* fully contiguous */
    return gasnete_gets_ref_indiv(&stats,synctype,dstaddr,dststrides,srcnode,srcaddr,srcstrides,count,stridelevels GASNETE_THREAD_PASS);
  }

  /* select algorithm */
  #ifndef GASNETE_GETS_SELECTOR
    #if GASNETE_RANDOM_SELECTOR
      #define GASNETE_GETS_SELECTOR(stats,synctype,dstaddr,dststrides,srcnode,srcaddr,srcstrides,count,stridelevels) do {                         \
        switch (rand() % 4) {                                                                                                                     \
          case 0:                                                                                                                                 \
            GASNETE_GETS_SCATTER_SELECTOR(stats,synctype,dstaddr,dststrides,srcnode,srcaddr,srcstrides,count,stridelevels);                       \
          case 1:                                                                                                                                 \
            GASNETE_GETS_AMPIPELINE_SELECTOR(stats,synctype,dstaddr,dststrides,srcnode,srcaddr,srcstrides,count,stridelevels);                    \
          case 2:                                                                                                                                 \
            return gasnete_gets_ref_indiv(stats,synctype,dstaddr,dststrides,srcnode,srcaddr,srcstrides,count,stridelevels GASNETE_THREAD_PASS);   \
          case 3:                                                                                                                                 \
            return gasnete_gets_ref_vector(stats,synctype,dstaddr,dststrides,srcnode,srcaddr,srcstrides,count,stridelevels GASNETE_THREAD_PASS);  \
          case 4:                                                                                                                                 \
            return gasnete_gets_ref_indexed(stats,synctype,dstaddr,dststrides,srcnode,srcaddr,srcstrides,count,stridelevels GASNETE_THREAD_PASS); \
        } } while (0)
    #else 
      #define GASNETE_GETS_SELECTOR(stats,synctype,dstaddr,dststrides,srcnode,srcaddr,srcstrides,count,stridelevels)       \
        GASNETE_GETS_SCATTER_SELECTOR(stats,synctype,dstaddr,dststrides,srcnode,srcaddr,srcstrides,count,stridelevels);    \
        GASNETE_GETS_AMPIPELINE_SELECTOR(stats,synctype,dstaddr,dststrides,srcnode,srcaddr,srcstrides,count,stridelevels); \
        return gasnete_gets_ref_indiv(stats,synctype,dstaddr,dststrides,srcnode,srcaddr,srcstrides,count,stridelevels GASNETE_THREAD_PASS)
    #endif
  #endif
  GASNETE_GETS_SELECTOR(&stats,synctype,dstaddr,dststrides,srcnode,srcaddr,srcstrides,count,stridelevels);
  gasneti_fatalerror("failure in GASNETE_GETS_SELECTOR - should never reach here");
  return GASNET_INVALID_HANDLE; /* avoid warning on MIPSPro */
}
#endif
/*---------------------------------------------------------------------------------*/
/* ***  Progress Function *** */
/*---------------------------------------------------------------------------------*/
/* signal a visop dummy eop/iop, unlink it and free it */
#define GASNETE_VISOP_SIGNAL_AND_FREE(visop, isget) do { \
    GASNETE_VISOP_SIGNAL(visop, isget);                  \
    GASNETI_PROGRESSFNS_DISABLE(gasneti_pf_vis,COUNTED); \
    *lastp = visop->next; /* unlink */                   \
    gasneti_free(visop);                                 \
    goto visop_removed;                                  \
  } while (0)

extern void gasneti_vis_progressfn() { 
  GASNETE_THREAD_LOOKUP /* TODO: remove this lookup */
  gasnete_vis_threaddata_t *td = GASNETE_VIS_MYTHREAD; 
  gasneti_vis_op_t **lastp = &(td->active_ops);
  if (td->progressfn_active) return; /* prevent recursion */
  td->progressfn_active = 1;
  for (lastp = &(td->active_ops); *lastp; ) {
    gasneti_vis_op_t * const visop = *lastp;
    #ifdef GASNETE_VIS_PROGRESSFN_EXTRA
           GASNETE_VIS_PROGRESSFN_EXTRA(visop, lastp)
    #endif
    switch (visop->type) {
    #ifdef GASNETE_PUTV_GATHER_SELECTOR
      case GASNETI_VIS_CAT_PUTV_GATHER:
        if (gasnete_try_syncnb(visop->handle) == GASNET_OK) { /* TODO: remove recursive poll */
          GASNETE_VISOP_SIGNAL_AND_FREE(visop, 0);
        }
      break;
    #endif
    #ifdef GASNETE_GETV_SCATTER_SELECTOR
      case GASNETI_VIS_CAT_GETV_SCATTER:
        if (gasnete_try_syncnb(visop->handle) == GASNET_OK) {
          gasnet_memvec_t const * const savedlst = (gasnet_memvec_t const *)(visop + 1);
          void const * const packedbuf = savedlst + visop->count;
          gasnete_memvec_unpack(visop->count, savedlst, packedbuf, 0, (size_t)-1);
          GASNETE_VISOP_SIGNAL_AND_FREE(visop, 1);
        }
      break;
    #endif
    #ifdef GASNETE_PUTI_GATHER_SELECTOR
      case GASNETI_VIS_CAT_PUTI_GATHER:
        if (gasnete_try_syncnb(visop->handle) == GASNET_OK) { 
          GASNETE_VISOP_SIGNAL_AND_FREE(visop, 0);
        }
      break;
    #endif
    #ifdef GASNETE_GETI_SCATTER_SELECTOR
      case GASNETI_VIS_CAT_GETI_SCATTER:
        if (gasnete_try_syncnb(visop->handle) == GASNET_OK) {
          void * const * const savedlst = (void * const *)(visop + 1);
          void const * const packedbuf = savedlst + visop->count;
          gasnete_addrlist_unpack(visop->count, savedlst, visop->len, packedbuf, 0, (size_t)-1);
          GASNETE_VISOP_SIGNAL_AND_FREE(visop, 1);
        }
      break;
    #endif
    #ifdef GASNETE_PUTS_GATHER_SELECTOR
      case GASNETI_VIS_CAT_PUTS_GATHER:
        if (gasnete_try_syncnb(visop->handle) == GASNET_OK) { 
          GASNETE_VISOP_SIGNAL_AND_FREE(visop, 0);
        }
      break;
    #endif
    #ifdef GASNETE_GETS_SCATTER_SELECTOR
      case GASNETI_VIS_CAT_GETS_SCATTER:
        if (gasnete_try_syncnb(visop->handle) == GASNET_OK) {
          size_t stridelevels = visop->len;
          size_t * const savedstrides = (size_t *)(visop + 1);
          size_t * const savedcount = savedstrides + stridelevels;
          void * const packedbuf = (void *)(savedcount + stridelevels + 1);
          gasnete_strided_unpack_all(visop->addr, savedstrides, savedcount, stridelevels, packedbuf);
          GASNETE_VISOP_SIGNAL_AND_FREE(visop, 1);
        }
      break;
    #endif
      default: gasneti_fatalerror("unrecognized visop category: %i", visop->type);
    }
    lastp = &(visop->next); /* advance */
    visop_removed: ;
  }
  td->progressfn_active = 0;
}

/*---------------------------------------------------------------------------------*/
/* ***  Handlers *** */
/*---------------------------------------------------------------------------------*/
#if GASNETE_USE_AMPIPELINE
  #define GASNETE_VIS_AMPIPELINE_HANDLERS()                               \
    gasneti_handler_tableentry_with_bits(gasnete_putv_AMPipeline_reqh),   \
    gasneti_handler_tableentry_with_bits(gasnete_putvis_AMPipeline_reph), \
    gasneti_handler_tableentry_with_bits(gasnete_getv_AMPipeline_reqh),   \
    gasneti_handler_tableentry_with_bits(gasnete_getv_AMPipeline_reph),   \
    gasneti_handler_tableentry_with_bits(gasnete_puti_AMPipeline_reqh),   \
    gasneti_handler_tableentry_with_bits(gasnete_geti_AMPipeline_reqh),   \
    gasneti_handler_tableentry_with_bits(gasnete_geti_AMPipeline_reph),   \
    gasneti_handler_tableentry_with_bits(gasnete_puts_AMPipeline_reqh),   \
    gasneti_handler_tableentry_with_bits(gasnete_gets_AMPipeline_reqh),   \
    gasneti_handler_tableentry_with_bits(gasnete_gets_AMPipeline_reph)     
#else
  #define GASNETE_VIS_AMPIPELINE_HANDLERS()
#endif

#if GASNETE_USE_AMPIPELINE
  #define GASNETE_REFVIS_HANDLERS()                            \
    /* ptr-width independent handlers */                       \
    /*  gasneti_handler_tableentry_no_bits(gasnete__reqh) */   \
                                                               \
    /* ptr-width dependent handlers */                         \
    /*  gasneti_handler_tableentry_with_bits(gasnete__reqh) */ \
                                                               \
    GASNETE_VIS_AMPIPELINE_HANDLERS()                        
#endif
/*---------------------------------------------------------------------------------*/


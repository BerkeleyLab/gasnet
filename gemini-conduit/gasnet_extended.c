/*   $Source: bitbucket.org:berkeleylab/gasnet.git/gemini-conduit/gasnet_extended.c $
 * Description: GASNet Extended API over Gemini Implementation
 * Copyright 2002, Dan Bonachea <bonachea@cs.berkeley.edu>
 * Terms of use are as specified in license.txt
 */

#include <gasnet_internal.h>
#include <gasnet_extended_internal.h>
#include <gasnet_gemini.h>

/* ------------------------------------------------------------------------------------ */
/*
  Tuning Parameters
  =================
*/

/* Maximum length of an RDMA op with local address INside the segment.
   GNI_PostRdma() as a limit of 2^32-1, but we pick a 4MB aligned value
 */
#ifndef GC_MAXRDMA_IN 
#define GC_MAXRDMA_IN 0xFFC00000
#endif

/* Maximum length of an RDMA op with local address OUTside the segment.
   Choice determines length of dynamic memory registrations
   These should be no smaller than AMMaxLong{Request,Reply}().
 */
#ifndef GC_MAXRDMA_OUT 
  #ifdef GASNET_CONDUIT_ARIES
    #define GC_MAXRDMA_OUT 0x800000
  #else
    #define GC_MAXRDMA_OUT 0x100000
  #endif
#endif

/* ------------------------------------------------------------------------------------ */
/*
  Extended API Common Code
  ========================
  Factored bits of extended API code common to most conduits, overridable when necessary
*/

#if GASNETC_USE_MULTI_DOMAIN
#define GASNETE_NEW_THREADDATA_CALLBACK(td) \
    (td)->domain_idx = gasnetc_get_domain_idx((td)->threadidx);
#endif

#include "gasnet_extended_common.c"

/* ------------------------------------------------------------------------------------ */
/*
  Initialization
  ==============
*/
/* called at startup to check configuration sanity */
static void gasnete_check_config(void) {
  gasneti_check_config_postattach();
  gasnete_check_config_amref();

  gasneti_assert(sizeof(gasnete_eop_t) >= sizeof(void*));
}

extern void gasnete_init(void) {
  static int firstcall = 1;
  GASNETI_TRACE_PRINTF(C,("gasnete_init()"));
  gasneti_assert(firstcall); /*  make sure we haven't been called before */
  firstcall = 0;

  gasnete_check_config(); /*  check for sanity */

  gasneti_assert(gasneti_nodes >= 1 && gasneti_mynode < gasneti_nodes);

  { gasnete_threaddata_t *threaddata = NULL;
  #if GASNETI_MAX_THREADS > 1
    /* register first thread (optimization) */
    threaddata = gasnete_mythread();
  #else
    /* register only thread (required) */
    threaddata = gasnete_new_threaddata();
  #endif
  #if !GASNETI_DISABLE_REFERENCE_EOP
    /* cause the first pool of eops to be allocated (optimization) */
    GASNET_POST_THREADINFO(threaddata);
    gasnete_eop_t *eop = gasnete_eop_new(threaddata);
    GASNETE_EOP_MARKDONE(eop);
    gasnete_eop_free(eop GASNETI_THREAD_GET);
  #endif
  }

  /* Initialize barrier resources */
  gasnete_barrier_init();

  /* Initialize VIS subsystem */
  gasnete_vis_init();
}

/* ------------------------------------------------------------------------------------ */
/*
  Get/Put:
  ========
*/

#define PACK_EOP_DONE(_eop)         PACK(&(_eop)->completed_cnt)
#define PACK_IOP_DONE(_iop,_getput) PACK(&(_iop)->completed_##_getput##_cnt)
#define MARK_DONE(_ptr,_isget)      gasneti_weakatomic_increment((gasneti_weakatomic_t *)(_ptr), \
                                                                 (_isget) ? GASNETI_ATOMIC_REL : 0)

/* Use some or all of the reference implementation of get/put in terms of AMs
 * Configuration appears in gasnet_extended_fwd.h
 */
#include "gasnet_extended_amref.c"

/* ------------------------------------------------------------------------------------ */

/* Gemini requires 4-byte alignment of local address, while Aries doesn't.
   However, intial testing shows that Aries performance is poor w/o alignment */
#if defined(GASNET_CONDUIT_GEMINI) || 1
  #define GASNETE_GET_IS_UNALIGNED(_nbytes, _src, _dest) \
      (3 & ((uintptr_t)(_nbytes) | (uintptr_t)(_src) | (uintptr_t)(_dest)))
#else
  #define GASNETE_GET_IS_UNALIGNED(_nbytes, _src, _dest) \
      (3 & ((uintptr_t)(_nbytes) | (uintptr_t)(_src)))
#endif

/* Some common idioms */

GASNETI_INLINE(gasnete_cntr_gpd)
gasnetc_post_descriptor_t *
gasnete_cntr_gpd(gasneti_weakatomic_val_t *initiated_p, gasnete_op_t *op,
                 uint32_t gpd_flags, gex_Flags_t flags GASNETC_DIDX_FARG)
{
  gasnetc_post_descriptor_t *gpd = gasnetc_alloc_post_descriptor(flags GASNETC_DIDX_PASS);
  if_pt (gpd) {
    gpd->gpd_flags = gpd_flags;
    gpd->gpd_completion = (uintptr_t) op;
    (*initiated_p) += 1;
  }
  return gpd;
}

// Allocate an eop with the initiated_cnt pre-incremented
GASNETI_INLINE(gasnete_eop_new_cnt)
gasnete_eop_t *gasnete_eop_new_cnt(gasnete_threaddata_t * const thread) {
  gasnete_eop_t *eop = gasnete_eop_new(thread);
  eop->initiated_cnt++;
  return eop;
}

// Free a never-used eop
GASNETI_INLINE(gasnete_consume_eop)
void gasnete_consume_eop(gasnete_eop_t *eop GASNETI_THREAD_FARG) {
  // decrement the initiated counter rather than atomically increment the completed counter
  eop->initiated_cnt -= 1;
  gasneti_assert(GASNETC_EOP_CNT_DONE(eop));
  SET_EVENT_DONE(eop, 0);
  gasnete_eop_free(eop GASNETI_THREAD_PASS);
}

#define GASNETE_EOP_CNTRS(_eop) \
        &(_eop)->initiated_cnt, ((gasnete_op_t*)(_eop)), GC_POST_COMPLETION_EOP
#define GASNETE_IOP_CNTRS_put(_iop) \
        &(_iop)->initiated_put_cnt, ((gasnete_op_t*)(_iop)), GC_POST_COMPLETION_IPUT
#define GASNETE_IOP_CNTRS_get(_iop) \
        &(_iop)->initiated_get_cnt, ((gasnete_op_t*)(_iop)), GC_POST_COMPLETION_IGET
#define GASNETE_IOP_CNTRS_rmw(_iop) \
        &(_iop)->initiated_rmw_cnt, ((gasnete_op_t*)(_iop)), GC_POST_COMPLETION_IRMW
#define GASNETE_IOP_CNTRS(_iop,_putget) \
        GASNETE_IOP_CNTRS_##_putget(_iop)

/* Main xfer functions */

GASNETI_WARN_UNUSED_RESULT // Returns non-zero in IMMEDIATE case only
static int /* XXX: Inlining left to compiler's discretion */
gasnete_get_bulk_inner(void *dest, gex_Rank_t node, void *src, size_t nbytes, gex_Flags_t flags,
                       gasneti_weakatomic_val_t *initiated_p, gasnete_op_t * const op,
                       uint32_t gpd_flags GASNETC_DIDX_FARG)
{
  gasnetc_post_descriptor_t *gpd;
  size_t chunksz;

  chunksz = gasneti_in_segment(NULL/*tm*/, gasneti_mynode, dest, nbytes) ? GC_MAXRDMA_IN : GC_MAXRDMA_OUT;

  if (nbytes > 2*chunksz) {
    /* If need more than 2 chunks, then size first one to achieve page alignment of remainder */
    size_t tmp, xfer_len;
retry:
    xfer_len = chunksz - ((uintptr_t)src & (GASNETI_PAGESIZE-1));
    gasneti_assert(xfer_len != 0);
    gasneti_assert(xfer_len < nbytes);
    gpd = gasnete_cntr_gpd(initiated_p, op, gpd_flags, flags GASNETC_DIDX_PASS);
    if_pf (!gpd) return 1;
    flags &= ~GEX_FLAG_IMMEDIATE;
    tmp = gasnetc_rdma_get(node, dest, src, xfer_len, gpd);
    dest = (char *) dest + tmp;
    src  = (char *) src  + tmp;
    nbytes -= tmp;

    if_pf (tmp != xfer_len) { /* MemRegister failed */
      gasneti_assert(chunksz == GC_MAXRDMA_OUT); /* out-of-seg and not looping */
      chunksz = tmp; /* Will avoid more MemRegister failures */
      goto retry;
    }
  }

  gasneti_assert(nbytes);
  do {
    const size_t xfer_len = MIN(nbytes, chunksz);
    gpd = gasnete_cntr_gpd(initiated_p, op, gpd_flags, flags GASNETC_DIDX_PASS);
    if_pf (!gpd) return 1;
    flags &= ~GEX_FLAG_IMMEDIATE;
    chunksz = gasnetc_rdma_get(node, dest, src, xfer_len, gpd);
    dest = (char *) dest + chunksz;
    src  = (char *) src  + chunksz;
    nbytes -= chunksz;
  } while (nbytes);

  return 0;
}

GASNETI_WARN_UNUSED_RESULT // Returns non-zero in IMMEDIATE case only
static int /* XXX: Inlining left to compiler's discretion */
gasnete_get_bulk_unaligned(void *dest, gex_Rank_t node, void *src, size_t nbytes, gex_Flags_t flags,
                           gasneti_weakatomic_val_t *initiated_p, gasnete_op_t * const op,
                           uint32_t gpd_flags GASNETC_DIDX_FARG)
{
  gasnetc_post_descriptor_t *gpd;
  const size_t max_chunk = gasnetc_max_get_unaligned;

#ifdef GASNET_CONDUIT_GEMINI
  /* Upto 1300 bytes or so, larger alignment helps */
  const size_t mask = (nbytes <= 1300) ? 63 : 3;
#else
  /* Larger alignment always helps */
  const size_t mask = 63;
#endif
  const size_t src_offset = mask & (uintptr_t) src;

  /* first chunk achieves alignment to as much as 64-bytes if necessary */
  gasneti_assert(src_offset < max_chunk);
  if (src_offset != 0) {
    const size_t chunksz = MIN(nbytes, (max_chunk - src_offset));
    gpd = gasnete_cntr_gpd(initiated_p, op, gpd_flags, flags GASNETC_DIDX_PASS);
    if_pf (!gpd) return 1;
    flags &= ~GEX_FLAG_IMMEDIATE;
    gasnetc_rdma_get_unaligned(node, dest, src, chunksz, gpd);
    dest = (char *) dest + chunksz;
    src  = (char *) src  + chunksz;
    nbytes -= chunksz;
  }
  if (!nbytes) return 0;
  gasneti_assert(0 == (3 & (uintptr_t)src));
  
  if (! GASNETE_GET_IS_UNALIGNED(0,0,dest) && (nbytes > max_chunk)) {
    /* dest address is sufficiently aligned - may use zero-copy (if applicable) 
       however, must exclude any "tail" of unaligned length */
    const size_t tailsz = (nbytes & 3) ? (nbytes % GASNETC_GNI_IMMEDIATE_BOUNCE_SIZE) : 0;
    const size_t chunksz = nbytes - tailsz;
    if (chunksz) {
      gasneti_assert(0 == (3 & chunksz));
      gasneti_assert(! GASNETE_GET_IS_UNALIGNED(chunksz,src,dest));
      int imm = gasnete_get_bulk_inner(dest, node, src, chunksz, flags,
                                       initiated_p, op, gpd_flags GASNETC_DIDX_PASS);
      if_pf (imm) return 1;
      flags &= ~GEX_FLAG_IMMEDIATE;
      dest = (char *) dest + chunksz;
      src  = (char *) src  + chunksz;
      nbytes = tailsz;
    }
  }

  /* dest address and/or nbytes is unaligned - must use bounce buffers for remainder */
  while (nbytes) {
    const size_t chunksz = MIN(nbytes, max_chunk);
    gpd = gasnete_cntr_gpd(initiated_p, op, gpd_flags, flags GASNETC_DIDX_PASS);
    if_pf (!gpd) return 1;
    flags &= ~GEX_FLAG_IMMEDIATE;
    gasnetc_rdma_get_unaligned(node, dest, src, chunksz, gpd);
    dest = (char *) dest + chunksz;
    src  = (char *) src  + chunksz;
    nbytes -= chunksz;
  }

  return 0;
}

// gasnete_put_bulk()
// Non-bulk (everything but GEX_EVENT_DEFER) => requests signalling of LC
GASNETI_WARN_UNUSED_RESULT // Returns non-zero in IMMEDIATE case only
static int /* XXX: Inlining left to compiler's discretion */
gasnete_put_inner(gex_Rank_t node, void *dest, void *src, size_t nbytes, gex_Flags_t flags,
                  gasneti_weakatomic_val_t *initiated_lc, void *lc_completion,
                  gasneti_weakatomic_val_t *initiated_p, gasnete_op_t * const op,
                  uint32_t gpd_flags GASNETC_DIDX_FARG)
{
  gasnetc_post_descriptor_t *gpd;
  size_t chunksz;

  chunksz = gasneti_in_segment(NULL/*tm*/, gasneti_mynode, src, nbytes) ? GC_MAXRDMA_IN : GC_MAXRDMA_OUT;

  gasneti_suspend_spinpollers();

  if (nbytes > 2*chunksz) {
    /* If need more than 2 chunks, then size first one to achieve page alignment of remainder */
    size_t tmp, xfer_len;
retry:
    xfer_len = chunksz - ((uintptr_t)src & (GASNETI_PAGESIZE-1));
    gasneti_assert(xfer_len != 0);
    gasneti_assert(xfer_len < nbytes);
    gpd = gasnete_cntr_gpd(initiated_p, op, gpd_flags, flags GASNETC_DIDX_PASS);
    if_pf (!gpd) goto out_immediate;
    gpd->gpd_put_lc = (uint64_t) lc_completion;
    flags &= ~GEX_FLAG_IMMEDIATE;
    tmp = gasnetc_rdma_put_lc(node, dest, src, xfer_len, initiated_lc, 0, gpd);
    dest = (char *) dest + tmp;
    src  = (char *) src  + tmp;
    nbytes -= tmp;

    if_pf (tmp != xfer_len) { /* MemRegister failed */
      gasneti_assert(chunksz == GC_MAXRDMA_OUT); /* out-of-seg and not looping */
      chunksz = tmp; /* Will avoid more MemRegister failures */
      goto retry;
    }
  }

  gasneti_assert(nbytes);
  const int is_eop = (gpd_flags & GC_POST_COMPLETION_EOP);
  do {
    const size_t xfer_len = MIN(nbytes, chunksz);
    gpd = gasnete_cntr_gpd(initiated_p, op, gpd_flags, flags GASNETC_DIDX_PASS);
    if_pf (!gpd) goto out_immediate;
    gpd->gpd_put_lc = (uint64_t) lc_completion;
    flags &= ~GEX_FLAG_IMMEDIATE;
    int eop_last_chunk = is_eop && (nbytes == xfer_len);
    chunksz = gasnetc_rdma_put_lc(node, dest, src, xfer_len, initiated_lc, eop_last_chunk, gpd);
    dest = (char *) dest + chunksz;
    src  = (char *) src  + chunksz;
    nbytes -= chunksz;
  } while (nbytes);

  gasneti_resume_spinpollers();
  return 0;

out_immediate:
  gasneti_resume_spinpollers();
  return 1;
}

// gasnete_put_bulk_inner()
// Bulk (aka GEX_EVENT_DEFER) => no signalling of LC
GASNETI_WARN_UNUSED_RESULT // Returns non-zero in IMMEDIATE case only
static int /* XXX: Inlining left to compiler's discretion */
gasnete_put_bulk_inner(gex_Rank_t node, void *dest, void *src, size_t nbytes, gex_Flags_t flags,
                       gasneti_weakatomic_val_t *initiated_p, gasnete_op_t * const op,
                       uint32_t gpd_flags GASNETC_DIDX_FARG)
{
  gasnetc_post_descriptor_t *gpd;
  size_t chunksz;

  chunksz = gasneti_in_segment(NULL/*tm*/, gasneti_mynode, src, nbytes) ? GC_MAXRDMA_IN : GC_MAXRDMA_OUT;

  gasneti_suspend_spinpollers();

  if (nbytes > 2*chunksz) {
    /* If need more than 2 chunks, then size first one to achieve page alignment of remainder */
    size_t tmp, xfer_len;
retry:
    xfer_len = chunksz - ((uintptr_t)src & (GASNETI_PAGESIZE-1));
    gasneti_assert(xfer_len != 0);
    gasneti_assert(xfer_len < nbytes);
    gpd = gasnete_cntr_gpd(initiated_p, op, gpd_flags, flags GASNETC_DIDX_PASS);
    if_pf (!gpd) goto out_immediate;
    flags &= ~GEX_FLAG_IMMEDIATE;
    tmp = gasnetc_rdma_put_bulk(node, dest, src, xfer_len, gpd);
    dest = (char *) dest + tmp;
    src  = (char *) src  + tmp;
    nbytes -= tmp;

    if_pf (tmp != xfer_len) { /* MemRegister failed */
      gasneti_assert(chunksz == GC_MAXRDMA_OUT); /* out-of-seg and not looping */
      chunksz = tmp; /* Will avoid more MemRegister failures */
      goto retry;
    }
  }

  gasneti_assert(nbytes);
  do {
    const size_t xfer_len = MIN(nbytes, chunksz);
    gpd = gasnete_cntr_gpd(initiated_p, op, gpd_flags, flags GASNETC_DIDX_PASS);
    if_pf (!gpd) goto out_immediate;
    flags &= ~GEX_FLAG_IMMEDIATE;
    chunksz = gasnetc_rdma_put_bulk(node, dest, src, xfer_len, gpd);
    dest = (char *) dest + chunksz;
    src  = (char *) src  + chunksz;
    nbytes -= chunksz;
  } while (nbytes);

  gasneti_resume_spinpollers();
  return 0;

out_immediate:
  gasneti_resume_spinpollers();
  return 1;
}

/* ------------------------------------------------------------------------------------ */
/*
  Non-blocking memory-to-memory transfers (explicit event)
  ==========================================================
*/
/* ------------------------------------------------------------------------------------ */

/* Conduits not using the gasnete_amref_ versions should implement at least the following:
     gasnete_get_nb
     gasnete_put_nb
*/

extern
gex_Event_t gasnete_get_nb(
                     gex_TM_t tm,
                     void *dest,
                     gex_Rank_t rank, void *src,
                     size_t nbytes,
                     gex_Flags_t flags GASNETI_THREAD_FARG)
{
  GASNETI_CHECKPSHM_GET(H);
  {
    int imm;
    gasnete_threaddata_t * const mythread = GASNETI_MYTHREAD;
    gasnete_eop_t *eop = gasnete_eop_new_cnt(mythread);
    GASNETC_DIDX_POST(mythread->domain_idx);
    gasneti_suspend_spinpollers();
    if_pf (GASNETE_GET_IS_UNALIGNED(nbytes, src, dest)) {
      imm = gasnete_get_bulk_unaligned(dest, rank, src, nbytes, flags,
                                       GASNETE_EOP_CNTRS(eop) GASNETC_DIDX_PASS);
    } else {
      imm = gasnete_get_bulk_inner(dest, rank, src, nbytes, flags,
                                   GASNETE_EOP_CNTRS(eop) GASNETC_DIDX_PASS);
    }
    gasneti_resume_spinpollers();
    if_pf (imm) return (gasnete_consume_eop(eop GASNETI_THREAD_PASS), GEX_EVENT_NO_OP);
    GASNETC_EOP_CNT_FINISH(eop); // TODO-EX: optimize away this extra atomic op under some conditions?
    return (gex_Event_t) eop;
  }
}

extern
gex_Event_t gasnete_put_nb(
                     gex_TM_t tm,
                     gex_Rank_t rank, void *dest,
                     void *src,
                     size_t nbytes, gex_Event_t *lc_opt,
                     gex_Flags_t flags GASNETI_THREAD_FARG)
{
  GASNETI_CHECKPSHM_PUT(H);

  gasnete_threaddata_t * const mythread = GASNETI_MYTHREAD;
  gasnete_eop_t *eop = gasnete_eop_new_cnt(mythread);
  GASNETC_DIDX_POST(mythread->domain_idx);

  if (lc_opt == GEX_EVENT_DEFER) {
    int imm = gasnete_put_bulk_inner(rank, dest, src, nbytes, flags,
                                     GASNETE_EOP_CNTRS(eop) GASNETC_DIDX_PASS);
    if (imm) goto out_immediate;
  } else {
  #if GASNET_DEBUG
    if ((lc_opt != GEX_EVENT_NOW) && !gasneti_leaf_is_pointer(lc_opt)) {
      gasneti_fatalerror("Invalid lc_opt argument to Put_nb");
    }
  #endif

    GASNETE_EOP_LC_START(eop);
    const gasneti_weakatomic_val_t start_alc = eop->initiated_alc;
    eop->initiated_alc += 1;

    int imm = gasnete_put_inner(rank, dest, src, nbytes, flags,
                                &eop->initiated_alc, (void *)eop,
                                GASNETE_EOP_CNTRS(eop)
                                GASNETC_DIDX_PASS);
    if (imm) {
      eop->initiated_alc = start_alc;
      goto out_immediate;
    }

    if (lc_opt == GEX_EVENT_NOW) {
      gasneti_polluntil(GASNETE_EOP_LC_DONE(eop));
    } else {
      *lc_opt = gasneti_op_event(eop, gasnete_eop_event_alc);
    }
  }

  GASNETC_EOP_CNT_FINISH(eop); // TODO-EX: optimize away this extra atomic op under some conditions?
  return (gex_Event_t) eop;

out_immediate:
  gasneti_assert(GASNETC_EOP_ALC_DONE(eop));
  gasnete_consume_eop(eop GASNETI_THREAD_PASS);
  return GEX_EVENT_NO_OP;
}

/* ------------------------------------------------------------------------------------ */
/*
  Non-blocking memory-to-memory transfers (implicit event)
  ==========================================================
*/
/* ------------------------------------------------------------------------------------ */

/* Conduits not using the gasnete_amref_ versions should implement at least the following:
     gasnete_get_nbi
     gasnete_put_nbi
*/

extern
int gasnete_get_nbi( gex_TM_t tm,
                     void *dest,
                     gex_Rank_t rank, void *src,
                     size_t nbytes,
                     gex_Flags_t flags GASNETI_THREAD_FARG)
{
  GASNETI_CHECKPSHM_GET(I);
  {
    int imm;
    gasnete_threaddata_t * const mythread = GASNETI_MYTHREAD;
    gasnete_iop_t * const iop = mythread->current_iop;
    GASNETC_DIDX_POST(mythread->domain_idx);
    gasneti_suspend_spinpollers();
    if_pf (GASNETE_GET_IS_UNALIGNED(nbytes, src, dest)) {
      imm = gasnete_get_bulk_unaligned(dest, rank, src, nbytes, flags,
                                       GASNETE_IOP_CNTRS(iop, get) GASNETC_DIDX_PASS);
    } else {
      imm = gasnete_get_bulk_inner(dest, rank, src, nbytes, flags,
                                   GASNETE_IOP_CNTRS(iop, get) GASNETC_DIDX_PASS);
    }
    gasneti_resume_spinpollers();
    return imm;
  }
}

int gasnete_put_nbi( gex_TM_t tm,
                     gex_Rank_t rank, void *dest,
                     void *src,
                     size_t nbytes, gex_Event_t *lc_opt,
                     gex_Flags_t flags GASNETI_THREAD_FARG)
{
  GASNETI_CHECKPSHM_PUT(I);

  gasnete_threaddata_t * const mythread = GASNETI_MYTHREAD;
  gasnete_iop_t * const iop = mythread->current_iop;
  GASNETC_DIDX_POST(mythread->domain_idx);

  int imm;
  if (lc_opt == GEX_EVENT_DEFER) {
    imm = gasnete_put_bulk_inner(rank, dest, src, nbytes, flags,
                                 GASNETE_IOP_CNTRS(iop, put) GASNETC_DIDX_PASS);
  } else {
    gasneti_weakatomic_val_t my_initiated_lc = 0;
    volatile gasneti_weakatomic_val_t my_completed_lc = 0;

    gasneti_weakatomic_val_t *initiated_lc;
    void *lc_completion;
    uint32_t extra_flags = 0;

    if (lc_opt == GEX_EVENT_NOW) {
      // Use a non-atomic counter and avoid over synchronizing
      initiated_lc = &my_initiated_lc;
      lc_completion = (void *) &my_completed_lc;
      extra_flags = GC_POST_LC_NOW;
    } else {
    #if GASNET_DEBUG
      if (lc_opt != GEX_EVENT_GROUP) {
        gasneti_fatalerror("Invalid lc_opt argument to Put_nbi");
      }
    #endif
      initiated_lc = &iop->initiated_alc_cnt;
      lc_completion = (void *) iop;
    }

    imm = gasnete_put_inner(rank, dest, src, nbytes, flags,
                            initiated_lc, lc_completion,
                            GASNETE_IOP_CNTRS(iop, put) | extra_flags
                            GASNETC_DIDX_PASS);
    gasneti_polluntil(my_initiated_lc == my_completed_lc);
  }

  return imm;
}

/* ------------------------------------------------------------------------------------ */
/*
  Blocking and non-blocking register-to-memory Puts
  =================================================
*/
/* ------------------------------------------------------------------------------------ */

extern int gasnete_put_val(
                gex_TM_t tm,
                gex_Rank_t rank, void *dest,
                gex_RMA_Value_t value,
                size_t nbytes, gex_Flags_t flags
                GASNETI_THREAD_FARG)
{
  GASNETI_CHECKPSHM_PUTVAL(I);
  {
    GASNETC_DIDX_POST(GASNETI_MYTHREAD->domain_idx);
    gasnetc_post_descriptor_t *gpd;
    volatile int done = 0;
    gasneti_suspend_spinpollers();
    gpd = gasnetc_alloc_post_descriptor(0 GASNETC_DIDX_PASS);
    gpd->gpd_completion = (uintptr_t) &done;
    gpd->gpd_flags = GC_POST_COMPLETION_FLAG;
    gpd->u.put_val = value;
    gasnetc_rdma_put_buff(rank, dest, GASNETE_STARTOFBITS(&gpd->u.put_val, nbytes), nbytes, gpd);
    gasneti_resume_spinpollers();
    gasneti_polluntil(done);
    return 0;
  }
}

GASNETI_WARN_UNUSED_RESULT // Returns non-zero in IMMEDIATE case only
GASNETI_INLINE(gasnete_put_val_inner)
int gasnete_put_val_inner(
                gex_Rank_t rank, void *dest,
                gex_RMA_Value_t value, size_t nbytes,
                gasneti_weakatomic_val_t *initiated_p, gasnete_op_t *op,
                uint32_t gpd_flags, gex_Flags_t flags GASNETC_DIDX_FARG)
{
    gasnetc_post_descriptor_t *gpd;

    gasneti_suspend_spinpollers();
    gpd = gasnete_cntr_gpd(initiated_p, op, gpd_flags, flags GASNETC_DIDX_PASS);
    if (!gpd) goto out_immediate;
    gpd->u.put_val = value;
    gasnetc_rdma_put_buff(rank, dest, GASNETE_STARTOFBITS(&gpd->u.put_val, nbytes), nbytes, gpd);
    gasneti_resume_spinpollers();
    return 0;

out_immediate:
    gasneti_resume_spinpollers();
    return 1;
}

extern gex_Event_t gasnete_put_nb_val(
                gex_TM_t tm,
                gex_Rank_t rank, void *dest,
                gex_RMA_Value_t value,
                size_t nbytes, gex_Flags_t flags
                GASNETI_THREAD_FARG)
{
    GASNETI_CHECKPSHM_PUTVAL(H);

    gasnete_threaddata_t * const mythread = GASNETI_MYTHREAD;
    GASNETC_DIDX_POST(mythread->domain_idx);
    gasnete_eop_t * const eop = gasnete_eop_new(mythread); // not _cnt
    int imm = gasnete_put_val_inner(rank, dest, value, nbytes,
                                    GASNETE_EOP_CNTRS(eop), flags GASNETC_DIDX_PASS);
    if (imm) {
        SET_EVENT_DONE(eop, 0);
        gasnete_eop_free(eop GASNETI_THREAD_PASS);
        return GEX_EVENT_NO_OP;
    }
    return((gex_Event_t) eop);
}

extern int gasnete_put_nbi_val(
                gex_TM_t tm,
                gex_Rank_t rank, void *dest,
                gex_RMA_Value_t value,
                size_t nbytes, gex_Flags_t flags
                GASNETI_THREAD_FARG)
{
    GASNETI_CHECKPSHM_PUTVAL(I);

    gasnete_threaddata_t * const mythread = GASNETI_MYTHREAD;
    GASNETC_DIDX_POST(mythread->domain_idx);
    gasnete_iop_t * const iop = mythread->current_iop;
    int imm = gasnete_put_val_inner(rank, dest, value, nbytes,
                                    GASNETE_IOP_CNTRS(iop,put), flags GASNETC_DIDX_PASS);
    return imm;
}

/* ------------------------------------------------------------------------------------ */
/*
  Blocking and non-blocking memory-to-register Gets
  =================================================
*/
/* ------------------------------------------------------------------------------------ */

GASNETI_INLINE(gasnete_get_val_help)
gex_RMA_Value_t gasnete_get_val_help(void *src, size_t nbytes) {
#if PLATFORM_ARCH_LITTLE_ENDIAN
  /* Note that this is OK only on little-endian and when unaligned loads are "OKAY" */
  return *(gex_RMA_Value_t *)src & (~0UL >> (8*(SIZEOF_VOID_P-nbytes)));
#else
  /* XXX: could do load+shift but don't care given the lack of big-endian GNI systems */
  GASNETE_VALUE_RETURN(src, nbytes);
#endif
}
 
extern gex_RMA_Value_t gasnete_get_val(
                gex_TM_t tm,
                gex_Rank_t rank, void *src,
                size_t nbytes, gex_Flags_t flags
                GASNETI_THREAD_FARG)
{
  GASNETI_CHECKPSHM_GETVAL();
  {
    gex_RMA_Value_t result;
    GASNETC_DIDX_POST(GASNETI_MYTHREAD->domain_idx);
    gasnetc_post_descriptor_t *gpd;
    volatile int done = 0;
    uint8_t *buffer;
    gasneti_suspend_spinpollers();
    gpd = gasnetc_alloc_post_descriptor(0 GASNETC_DIDX_PASS);
    gpd->gpd_completion = (uintptr_t) &done;
    gpd->gpd_flags = GC_POST_COMPLETION_FLAG | GC_POST_KEEP_GPD;
    buffer = gpd->u.immediate;
    buffer += gasnetc_rdma_get_buff(rank, buffer, src, nbytes, gpd);
    gasneti_resume_spinpollers();
    gasneti_polluntil(done);
    result = gasnete_get_val_help(buffer, nbytes);
    gasnetc_free_post_descriptor(gpd);
    return result;
  }
}
/* ------------------------------------------------------------------------------------ */

#if GASNETC_BUILD_GNIRATOMIC
//
// GASNet-EX Remote Atomics via offload to Aries NIC (not Gemini)
//
// TODO-EX: Add logic to avoid bounce-copy for in-segment *result_p?
// The branches and lookups to set up the fetch directly to the client's
// memory are almost certainly more expensive than the 4- or 8-byte copy.
//
// TODO-EX: The following should be addressed once we have dynamic/run-time
// selection of implementation based on the set of operations specified to
// gex_AD_Create():
//  + Since Aries lacks a MULT operation we have chosen to defer support for
//    MULT *globally*.
//  + There is known[1] problem with double-precision addition on the Aries NIC
//    and use of either GNI_FMA_ATOMIC2_FFPADD or GNI_FMA_ATOMIC2_FPADD return
//    a GNI_RC_ILLEGAL_OP error.  Therefore, gex_DT_DBL is unconditionally
//    using the AM-based implementation.
//
// Aries (but not Gemini) NICs support the I32, I64, FLT and DBL types directly.
// For the operations of interest there is no difference between Ixx and Uxx.
//
// Notes on mapping from GEX to GNI opcodes:
//  + INC, DEC and SUB are mapped to ADD w/ the appropriate operand
//  + CSWAP operations use 32- and 64-bit integer types with the result
//    that IEEE +0 and -0 will *not* compare as equal
//  + Atomic SET is implemented as a non-fetching integer SWAP (follows [1])
//  + Atomic GET is implemented as an integer FAND(~0) (follows [1])
// TODO: Follow up w/ Howard Prichard regarding the rationale behind the
// mappings used for SET and GET.  In particular why is GET not FOR(0)?
//
// [1] libfabric-1.5.1:prov/gni/src/gnix_atomic.c

#include <gasnet_ratomic_internal.h>

// Value intended to trigger GNI_RC_ILLEGAL_OP if used as amo_cmd
#define GASNETC_INVALID_ATOMIC ((gni_fma_cmd_type_t)0xffff)

// Tables translating GEX atomic opcode to GNI equivalents
// TODO: is there a less fragile way to do this?
static gni_fma_cmd_type_t amo_cmd_tbl_gex_dt_I32[] = {
    GNI_FMA_ATOMIC2_AND_S,
    GNI_FMA_ATOMIC2_OR_S,
    GNI_FMA_ATOMIC2_XOR_S,
    GNI_FMA_ATOMIC2_IADD_S,
    GNI_FMA_ATOMIC2_IADD_S,  // SUB: via ADD w/ negated arg
    GASNETC_INVALID_ATOMIC,  // MULT: unavailable
    GNI_FMA_ATOMIC2_IMIN_S,
    GNI_FMA_ATOMIC2_IMAX_S,
    GNI_FMA_ATOMIC2_IADD_S,  // INC: via ADD +1
    GNI_FMA_ATOMIC2_IADD_S,  // DEC: via ADD -1
    //
    GNI_FMA_ATOMIC2_FAND_S,
    GNI_FMA_ATOMIC2_FOR_S,
    GNI_FMA_ATOMIC2_FXOR_S,
    GNI_FMA_ATOMIC2_FIADD_S,
    GNI_FMA_ATOMIC2_FIADD_S, // FSUB: via FADD w/ negated arg
    GASNETC_INVALID_ATOMIC,  // FMULT: unavailable
    GNI_FMA_ATOMIC2_FIMIN_S,
    GNI_FMA_ATOMIC2_FIMAX_S,
    GNI_FMA_ATOMIC2_FIADD_S, // FINC: via FADD +1
    GNI_FMA_ATOMIC2_FIADD_S, // FDEC: via FADD -1
    //
    GNI_FMA_ATOMIC2_SWAP_S,  // SET: via (non-fetching) SWAP
    GNI_FMA_ATOMIC2_FAND_S,  // GET: via FAND(~0)
    GNI_FMA_ATOMIC2_FSWAP_S,
    GNI_FMA_ATOMIC2_FCSWAP_S
};
#define amo_cmd_tbl_gex_dt_U32 amo_cmd_tbl_gex_dt_I32

static gni_fma_cmd_type_t amo_cmd_tbl_gex_dt_I64[] = {
    GNI_FMA_ATOMIC2_AND,
    GNI_FMA_ATOMIC2_OR,
    GNI_FMA_ATOMIC2_XOR,
    GNI_FMA_ATOMIC2_IADD,
    GNI_FMA_ATOMIC2_IADD,   // SUB: via ADD w/ negated arg
    GASNETC_INVALID_ATOMIC, // MULT: unavailable
    GNI_FMA_ATOMIC2_IMIN,
    GNI_FMA_ATOMIC2_IMAX,
    GNI_FMA_ATOMIC2_IADD,   // INC: via ADD +1
    GNI_FMA_ATOMIC2_IADD,   // DEC: via ADD -1
    //
    GNI_FMA_ATOMIC2_FAND,
    GNI_FMA_ATOMIC2_FOR,
    GNI_FMA_ATOMIC2_FXOR,
    GNI_FMA_ATOMIC2_FIADD,
    GNI_FMA_ATOMIC2_FIADD,  // FSUB: via FADD w/ negated arg
    GASNETC_INVALID_ATOMIC, // FMULT: unavailable
    GNI_FMA_ATOMIC2_FIMIN,
    GNI_FMA_ATOMIC2_FIMAX,
    GNI_FMA_ATOMIC2_FIADD,  // FINC: via FADD +1
    GNI_FMA_ATOMIC2_FIADD,  // FDEC: via FADD -1
    //
    GNI_FMA_ATOMIC2_SWAP,   // SET: via (non-fetching) SWAP
    GNI_FMA_ATOMIC2_FAND,   // GET: via FAND(~0)
    GNI_FMA_ATOMIC2_FSWAP,
    GNI_FMA_ATOMIC2_FCSWAP
};
#define amo_cmd_tbl_gex_dt_U64 amo_cmd_tbl_gex_dt_I64

static gni_fma_cmd_type_t amo_cmd_tbl_gex_dt_FLT[] = {
    GASNETC_INVALID_ATOMIC,   // AND: UNUSED
    GASNETC_INVALID_ATOMIC,   // OR:  UNUSED
    GASNETC_INVALID_ATOMIC,   // XOR: UNUSED
    GNI_FMA_ATOMIC2_FPADD_S,
    GNI_FMA_ATOMIC2_FPADD_S,  // SUB: via ADD w/ negated arg
    GASNETC_INVALID_ATOMIC,   // MULT: unavailable
    GNI_FMA_ATOMIC2_FPMIN_S,
    GNI_FMA_ATOMIC2_FPMAX_S,
    GNI_FMA_ATOMIC2_FPADD_S,  // INC: via ADD +1
    GNI_FMA_ATOMIC2_FPADD_S,  // DEC: via ADD -1
    //
    GASNETC_INVALID_ATOMIC,   // FAND: UNUSED
    GASNETC_INVALID_ATOMIC,   // FOR:  UNUSED
    GASNETC_INVALID_ATOMIC,   // FXOR: UNUSED
    GNI_FMA_ATOMIC2_FFPADD_S,
    GNI_FMA_ATOMIC2_FFPADD_S, // FSUB: via FADD w/ negated arg
    GASNETC_INVALID_ATOMIC,   // FMULT: unavailable
    GNI_FMA_ATOMIC2_FFPMIN_S,
    GNI_FMA_ATOMIC2_FFPMAX_S,
    GNI_FMA_ATOMIC2_FFPADD_S, // FINC: via FADD +1
    GNI_FMA_ATOMIC2_FFPADD_S, // FDEC: via FADD -1
    //
    GNI_FMA_ATOMIC2_SWAP_S,   // SET: via (non-fetching) SWAP
    GNI_FMA_ATOMIC2_FAND_S,   // GET: via FAND(~0)
    GNI_FMA_ATOMIC2_FSWAP_S,
    GNI_FMA_ATOMIC2_FCSWAP_S
};

// NOTE: (F)FPADD is known to be broken!
static gni_fma_cmd_type_t amo_cmd_tbl_gex_dt_DBL[] = {
    GASNETC_INVALID_ATOMIC, // AND: UNUSED
    GASNETC_INVALID_ATOMIC, // OR:  UNUSED
    GASNETC_INVALID_ATOMIC, // XOR: UNUSED
    GNI_FMA_ATOMIC2_FPADD,
    GNI_FMA_ATOMIC2_FPADD,  // SUB: via ADD w/ negated arg
    GASNETC_INVALID_ATOMIC, // MULT: unavailable
    GNI_FMA_ATOMIC2_FPMIN,
    GNI_FMA_ATOMIC2_FPMAX,
    GNI_FMA_ATOMIC2_FPADD,  // INC: via ADD +1
    GNI_FMA_ATOMIC2_FPADD,  // DEC: via ADD -1
    //
    GASNETC_INVALID_ATOMIC, // FAND: UNUSED
    GASNETC_INVALID_ATOMIC, // FOR:  UNUSED
    GASNETC_INVALID_ATOMIC, // FXOR: UNUSED
    GNI_FMA_ATOMIC2_FFPADD,
    GNI_FMA_ATOMIC2_FFPADD, // FSUB: via FADD w/ negated arg
    GASNETC_INVALID_ATOMIC, // FMULT: unavailable
    GNI_FMA_ATOMIC2_FFPMIN,
    GNI_FMA_ATOMIC2_FFPMAX,
    GNI_FMA_ATOMIC2_FFPADD, // FINC: via FADD +1
    GNI_FMA_ATOMIC2_FFPADD, // FDEC: via FADD -1
    //
    GNI_FMA_ATOMIC2_SWAP,   // SET: via (non-fetching) SWAP
    GNI_FMA_ATOMIC2_FAND,   // GET: via FAND(~0)
    GNI_FMA_ATOMIC2_FSWAP,
    GNI_FMA_ATOMIC2_FCSWAP
};

// Generic post of a GNI AMO
//
// First 3 params (fetching, op_cnt, length) will be manifest constants
// which to lead to specialization of the code upon inlining.
GASNETI_WARN_UNUSED_RESULT // Returns non-zero in IMMEDIATE case only
GASNETI_INLINE(gasnete_ratomic_inner)
int gasnete_ratomic_inner(
        const int fetching, const int op_cnt, const int length,
        void *result_p, gex_Rank_t tgt_rank, void *tgt_addr,
        gni_fma_cmd_type_t cmd, uint64_t operand1, uint64_t operand2,
        gasneti_weakatomic_val_t *initiated_p, gasnete_op_t * const op,
        uint32_t gpd_flags, gex_Flags_t flags GASNETC_DIDX_FARG)
{
  gasneti_suspend_spinpollers();
  gasnetc_post_descriptor_t * const gpd =
                gasnete_cntr_gpd(initiated_p, op, gpd_flags, flags GASNETC_DIDX_PASS);
  if (gpd) {
    if (fetching) {
      gpd->gpd_amo_result = (uintptr_t) result_p;
      gpd->gpd_flags |= ((length == 4) ? GC_POST_COPY_AMO4 : GC_POST_COPY_AMO8);
    }
    gpd->gpd_amo_len = length;
    gpd->gpd_amo_cmd = cmd;
    switch (op_cnt) {
      case 2: gpd->gpd_amo_op2 = operand2; // fall through...
      case 1: gpd->gpd_amo_op1 = operand1;
              break;
      default: gasneti_unreachable();
    }
    gasnetc_post_amo(tgt_rank, tgt_addr, gpd);
  }
  gasneti_resume_spinpollers();
  return !gpd;
}

// NB-specific wrapper around gasnete_ratomic_inner()
GASNETI_INLINE(gasnete_ratomic_nb)
gex_Event_t gasnete_ratomic_nb(
        const int fetching, const int op_cnt, const int length,
        void *result_p, gex_Rank_t tgt_rank, void *tgt_addr,
        gni_fma_cmd_type_t cmd, uint64_t operand1, uint64_t operand2,
        gex_Flags_t flags GASNETI_THREAD_FARG)
{
  gasnete_threaddata_t * const mythread = GASNETI_MYTHREAD;
  GASNETC_DIDX_POST(mythread->domain_idx);
  gasnete_eop_t * const eop = gasnete_eop_new(mythread);
  int imm = gasnete_ratomic_inner(fetching, op_cnt, length,
                                  result_p, tgt_rank, tgt_addr,
                                  cmd, operand1, operand2,
                                  GASNETE_EOP_CNTRS(eop),
                                  flags GASNETC_DIDX_PASS);
  if (imm) {
      SET_EVENT_DONE(eop, 0);
      gasnete_eop_free(eop GASNETI_THREAD_PASS);
      return GEX_EVENT_NO_OP;
  }
  return (gex_Event_t) eop;
}

// NBI-specific wrapper around gasnete_ratomic_inner()
GASNETI_INLINE(gasnete_ratomic_nbi)
int gasnete_ratomic_nbi(
        const int fetching, const int op_cnt, const int length,
        void *result_p, gex_Rank_t tgt_rank, void *tgt_addr,
        gni_fma_cmd_type_t cmd, uint64_t operand1, uint64_t operand2,
        gex_Flags_t flags GASNETI_THREAD_FARG)
{
  gasnete_threaddata_t * const mythread = GASNETI_MYTHREAD;
  GASNETC_DIDX_POST(mythread->domain_idx);
  gasnete_iop_t * const iop = mythread->current_iop;
  int imm = gasnete_ratomic_inner(fetching, op_cnt, length,
                                  result_p, tgt_rank, tgt_addr,
                                  cmd, operand1, operand2,
                                  GASNETE_IOP_CNTRS(iop, rmw),
                                  flags GASNETC_DIDX_PASS);
  return imm;
}

//
// Functions (called by the top-level dispatch functions) which invoke
// the gasnete_ratomic_{nb,nbi}() functions, above.
//
// The [FNSG][012] interface is described in gasnet_ratomic.h, with
// the definition of the GASNETE_RATOMIC_DECL() macro.
//
// TODO-EX: Move to per-operation (table of function pointers) interface.  For
// this initial implementation it was expedient to adopt the same interface as
// used in the initial AM-based code.  However, there is a clear "impedance
// mismatch" seen in the need to branch on the opcode index to setup operands
// (1 for INC, -1 for DEC, negate operand1 for SUB).  This change will probably
// be required anyway, for the implementation of run-time implementation
// selection.  Note that the macros below have not been factored as much as may
// be possible due to this plan to discard this implementation.
//
#define GASNETE_GNIRATOMIC_DEFN(dtcode) \
        _GASNETE_GNIRATOMIC_DEFN1(gasnete_gniratomic##dtcode, dtcode##_isint, dtcode##_type, dtcode)
// This extra pass expands the "isint" token prior to additional concatenation
#define _GASNETE_GNIRATOMIC_DEFN1(prefix, isint, type, dtcode) \
        _GASNETE_GNIRATOMIC_DEFN2(prefix, isint, type, dtcode)
#define _GASNETE_GNIRATOMIC_DEFN2(prefix, isint, type, dtcode)    \
    gex_Event_t prefix##_NB_N0(                                   \
                gasneti_AD_t         ad,                          \
                gex_Rank_t     tgt_rank,  void       *tgt_addr,   \
                gasneti_op_idx_t op_idx,  gex_Flags_t flags       \
                GASNETI_THREAD_FARG)                              \
    {                                                             \
        gasneti_assert(gasneti_op_0arg(((gex_OP_t)1 << op_idx))); \
        _GASNETE_GNIRATOMIC_PREP_INC##isint(type, op_idx);        \
        gni_fma_cmd_type_t cmd = amo_cmd_tbl##dtcode[op_idx];     \
        return gasnete_ratomic_nb(0, 1, sizeof(type),             \
                                  NULL, tgt_rank, tgt_addr,       \
                                  cmd, inc, 0,                    \
                                  flags GASNETI_THREAD_PASS);     \
    } \
    gex_Event_t prefix##_NB_N1(                                   \
                gasneti_AD_t         ad,                          \
                gex_Rank_t     tgt_rank,  void       *tgt_addr,   \
                type           operand1,                          \
                gasneti_op_idx_t op_idx,  gex_Flags_t flags       \
                GASNETI_THREAD_FARG)                              \
    {                                                             \
        gasneti_assert(gasneti_op_1arg(((gex_OP_t)1 << op_idx))); \
        _GASNETE_GNIRATOMIC_PREP_NOP##isint(type, op_idx);        \
        gni_fma_cmd_type_t cmd = amo_cmd_tbl##dtcode[op_idx];     \
        return gasnete_ratomic_nb(0, 1, sizeof(type),             \
                                  NULL, tgt_rank, tgt_addr,       \
                                  cmd, op1, 0,                    \
                                  flags GASNETI_THREAD_PASS);     \
    } \
    gex_Event_t prefix##_NB_F0(                                   \
                gasneti_AD_t         ad,                          \
                type          *result_p,                          \
                gex_Rank_t     tgt_rank,  void       *tgt_addr,   \
                gasneti_op_idx_t op_idx,  gex_Flags_t flags       \
                GASNETI_THREAD_FARG)                              \
    {                                                             \
        gasneti_assert(gasneti_op_0arg(((gex_OP_t)1 << op_idx))); \
        _GASNETE_GNIRATOMIC_PREP_FINC##isint(type, op_idx);       \
        gni_fma_cmd_type_t cmd = amo_cmd_tbl##dtcode[op_idx];     \
        return gasnete_ratomic_nb(1, 1, sizeof(type),             \
                                  result_p, tgt_rank, tgt_addr,   \
                                  cmd, inc, 0,                    \
                                  flags GASNETI_THREAD_PASS);     \
    } \
    gex_Event_t prefix##_NB_F1(                                   \
                gasneti_AD_t         ad,                          \
                type          *result_p,                          \
                gex_Rank_t     tgt_rank,  void       *tgt_addr,   \
                type           operand1,                          \
                gasneti_op_idx_t op_idx,  gex_Flags_t flags       \
                GASNETI_THREAD_FARG)                              \
    {                                                             \
        gasneti_assert(gasneti_op_1arg(((gex_OP_t)1 << op_idx))); \
        _GASNETE_GNIRATOMIC_PREP_FOP##isint(type, op_idx);        \
        gni_fma_cmd_type_t cmd = amo_cmd_tbl##dtcode[op_idx];     \
        return gasnete_ratomic_nb(1, 1, sizeof(type),             \
                                  result_p, tgt_rank, tgt_addr,   \
                                  cmd, op1, 0,                    \
                                  flags GASNETI_THREAD_PASS);     \
    } \
    gex_Event_t prefix##_NB_F2(                                   \
                gasneti_AD_t         ad,                          \
                type          *result_p,                          \
                gex_Rank_t     tgt_rank,  void       *tgt_addr,   \
                type           operand1,  type        operand2,   \
                gasneti_op_idx_t op_idx,  gex_Flags_t flags       \
                GASNETI_THREAD_FARG)                              \
    {                                                             \
        gasneti_assert(gasneti_op_2arg(((gex_OP_t)1 << op_idx))); \
        _GASNETE_GNIRATOMIC_PREP_CAS##isint(type);                \
        gni_fma_cmd_type_t cmd = amo_cmd_tbl##dtcode[op_idx];     \
        return gasnete_ratomic_nb(1, 2, sizeof(type),             \
                                  result_p, tgt_rank, tgt_addr,   \
                                  cmd, op1, op2,                  \
                                  flags GASNETI_THREAD_PASS);     \
    } \
    gex_Event_t prefix##_NB_S1(                                   \
                gasneti_AD_t         ad,                          \
                gex_Rank_t     tgt_rank,  void       *tgt_addr,   \
                type           operand1,                          \
                gex_Flags_t flags                                 \
                GASNETI_THREAD_FARG)                              \
    {                                                             \
        _GASNETE_GNIRATOMIC_PREP_SET##isint(type);                \
        gni_fma_cmd_type_t cmd = amo_cmd_tbl##dtcode[op_idx];     \
        return gasnete_ratomic_nb(0, 1, sizeof(type),             \
                                  NULL, tgt_rank, tgt_addr,       \
                                  cmd, val, 0,                    \
                                  flags GASNETI_THREAD_PASS);     \
    } \
    gex_Event_t prefix##_NB_G0(                                   \
                gasneti_AD_t         ad,                          \
                type          *result_p,                          \
                gex_Rank_t     tgt_rank,  void       *tgt_addr,   \
                gex_Flags_t flags                                 \
                GASNETI_THREAD_FARG)                              \
    {                                                             \
        _GASNETE_GNIRATOMIC_PREP_GET();                           \
        gni_fma_cmd_type_t cmd = amo_cmd_tbl##dtcode[op_idx];     \
        return gasnete_ratomic_nb(1, 1, sizeof(type),             \
                                  result_p, tgt_rank, tgt_addr,   \
                                  cmd, op1, 0,                    \
                                  flags GASNETI_THREAD_PASS);     \
    } \
    gex_Event_t prefix##_NB_external(                             \
        gex_AD_t            ad,        type           *result_p,  \
        gex_Rank_t          tgt_rank,  void           *tgt_addr,  \
        gex_OP_t            opcode,    type           operand1,   \
        type                operand2,  gex_Flags_t    flags       \
        GASNETI_THREAD_FARG)                                      \
    {                                                             \
      return prefix##_NB(ad, result_p, tgt_rank, tgt_addr,        \
                         opcode, operand1, operand2, flags        \
                         GASNETI_THREAD_PASS);                    \
    } \
    int prefix##_NBI_N0(                                          \
                gasneti_AD_t         ad,                          \
                gex_Rank_t     tgt_rank,  void       *tgt_addr,   \
                gasneti_op_idx_t op_idx,  gex_Flags_t flags       \
                GASNETI_THREAD_FARG)                              \
    {                                                             \
        gasneti_assert(gasneti_op_0arg(((gex_OP_t)1 << op_idx))); \
        _GASNETE_GNIRATOMIC_PREP_INC##isint(type, op_idx);        \
        gni_fma_cmd_type_t cmd = amo_cmd_tbl##dtcode[op_idx];     \
        return gasnete_ratomic_nbi(0, 1, sizeof(type),            \
                                   NULL, tgt_rank, tgt_addr,      \
                                   cmd, inc, 0,                   \
                                   flags GASNETI_THREAD_PASS);    \
    } \
    int prefix##_NBI_N1(                                          \
                gasneti_AD_t         ad,                          \
                gex_Rank_t     tgt_rank,  void       *tgt_addr,   \
                type           operand1,                          \
                gasneti_op_idx_t op_idx,  gex_Flags_t flags       \
                GASNETI_THREAD_FARG)                              \
    {                                                             \
        gasneti_assert(gasneti_op_1arg(((gex_OP_t)1 << op_idx))); \
        _GASNETE_GNIRATOMIC_PREP_NOP##isint(type, op_idx);        \
        gni_fma_cmd_type_t cmd = amo_cmd_tbl##dtcode[op_idx];     \
        return gasnete_ratomic_nbi(0, 1, sizeof(type),            \
                                   NULL, tgt_rank, tgt_addr,      \
                                   cmd, op1, 0,                   \
                                   flags GASNETI_THREAD_PASS);    \
    } \
    int prefix##_NBI_F0(                                          \
                gasneti_AD_t         ad,                          \
                type          *result_p,                          \
                gex_Rank_t     tgt_rank,  void       *tgt_addr,   \
                gasneti_op_idx_t op_idx,  gex_Flags_t flags       \
                GASNETI_THREAD_FARG)                              \
    {                                                             \
        gasneti_assert(gasneti_op_0arg(((gex_OP_t)1 << op_idx))); \
        _GASNETE_GNIRATOMIC_PREP_FINC##isint(type, op_idx);       \
        gni_fma_cmd_type_t cmd = amo_cmd_tbl##dtcode[op_idx];     \
        return gasnete_ratomic_nbi(1, 1, sizeof(type),            \
                                   result_p, tgt_rank, tgt_addr,  \
                                   cmd, inc, 0,                   \
                                   flags GASNETI_THREAD_PASS);    \
    } \
    int prefix##_NBI_F1(                                          \
                gasneti_AD_t         ad,                          \
                type           *result_p,                         \
                gex_Rank_t     tgt_rank,  void       *tgt_addr,   \
                type           operand1,                          \
                gasneti_op_idx_t op_idx,  gex_Flags_t flags       \
                GASNETI_THREAD_FARG)                              \
    {                                                             \
        gasneti_assert(gasneti_op_1arg(((gex_OP_t)1 << op_idx))); \
        _GASNETE_GNIRATOMIC_PREP_FOP##isint(type, op_idx);        \
        gni_fma_cmd_type_t cmd = amo_cmd_tbl##dtcode[op_idx];     \
        return gasnete_ratomic_nbi(1, 1, sizeof(type),            \
                                   result_p, tgt_rank, tgt_addr,  \
                                   cmd, op1, 0,                   \
                                   flags GASNETI_THREAD_PASS);    \
    } \
    int prefix##_NBI_F2(                                          \
                gasneti_AD_t         ad,                          \
                type          *result_p,                          \
                gex_Rank_t     tgt_rank,  void       *tgt_addr,   \
                type           operand1,  type        operand2,   \
                gasneti_op_idx_t op_idx,  gex_Flags_t flags       \
                GASNETI_THREAD_FARG)                              \
    {                                                             \
        gasneti_assert(gasneti_op_2arg(((gex_OP_t)1 << op_idx))); \
        _GASNETE_GNIRATOMIC_PREP_CAS##isint(type);                \
        gni_fma_cmd_type_t cmd = amo_cmd_tbl##dtcode[op_idx];     \
        return gasnete_ratomic_nbi(1, 2, sizeof(type),            \
                                   result_p, tgt_rank, tgt_addr,  \
                                   cmd, op1, op2,                 \
                                   flags GASNETI_THREAD_PASS);    \
    } \
    int prefix##_NBI_S1(                                          \
                gasneti_AD_t         ad,                          \
                gex_Rank_t     tgt_rank,  void       *tgt_addr,   \
                type           operand1,                          \
                gex_Flags_t flags                                 \
                GASNETI_THREAD_FARG)                              \
    {                                                             \
        _GASNETE_GNIRATOMIC_PREP_SET##isint(type);                \
        gni_fma_cmd_type_t cmd = amo_cmd_tbl##dtcode[op_idx];     \
        return gasnete_ratomic_nbi(0, 1, sizeof(type),            \
                                   NULL, tgt_rank, tgt_addr,      \
                                   cmd, val, 0,                   \
                                   flags GASNETI_THREAD_PASS);    \
    } \
    int prefix##_NBI_G0(                                          \
                gasneti_AD_t         ad,                          \
                type          *result_p,                          \
                gex_Rank_t     tgt_rank,  void       *tgt_addr,   \
                gex_Flags_t flags                                 \
                GASNETI_THREAD_FARG)                              \
    {                                                             \
        _GASNETE_GNIRATOMIC_PREP_GET();                           \
        gni_fma_cmd_type_t cmd = amo_cmd_tbl##dtcode[op_idx];     \
        return gasnete_ratomic_nbi(1, 1, sizeof(type),            \
                                   result_p, tgt_rank, tgt_addr,  \
                                   cmd, op1, 0,                   \
                                   flags GASNETI_THREAD_PASS);    \
    } \
    int prefix##_NBI_external(                                    \
        gex_AD_t            ad,        type           *result_p,  \
        gex_Rank_t          tgt_rank,  void           *tgt_addr,  \
        gex_OP_t            opcode,    type           operand1,   \
        type                operand2,  gex_Flags_t    flags       \
        GASNETI_THREAD_FARG)                                      \
    {                                                             \
      return prefix##_NBI(ad, result_p, tgt_rank, tgt_addr,       \
                          opcode, operand1, operand2, flags       \
                          GASNETI_THREAD_PASS);                   \
    }
//
// Operand wrangling
// GNI uses uint64_t to pass all atomic operands
//
// + non-fetching single-operand: need to negate operand for SUB
#define _GASNETE_GNIRATOMIC_PREP_NOP1(type, op_idx) /* integer */ \
        uint64_t op1 = (op_idx == gasneti_op_idx_SUB) ? -operand1 : operand1
#define _GASNETE_GNIRATOMIC_PREP_NOP0(type, op_idx) /* floating-point */ \
        union { type fp; uint64_t u64; } u1;                           \
        u1.fp = (op_idx == gasneti_op_idx_SUB) ? -operand1 : operand1; \
        uint64_t op1 = u1.u64
// + fetching single-operand: need to negate operand for FSUB
#define _GASNETE_GNIRATOMIC_PREP_FOP1(type, op_idx) /* integer */ \
        uint64_t op1 = (op_idx == gasneti_op_idx_FSUB) ? -operand1 : operand1
#define _GASNETE_GNIRATOMIC_PREP_FOP0(type, op_idx) /* floating-point */ \
        union { type fp; uint64_t u64; } u1;                           \
        u1.fp = (op_idx == gasneti_op_idx_FSUB) ? -operand1 : operand1; \
        uint64_t op1 = u1.u64
// + CAS
#define _GASNETE_GNIRATOMIC_PREP_CAS1(type) /* integer */ \
        uint64_t op1 = operand1; uint64_t op2 = operand2
#define _GASNETE_GNIRATOMIC_PREP_CAS0(type) /* floating-point */ \
        union { type fp; uint64_t u64; } u1, u2; \
        u1.fp = operand1; uint64_t op1 = u1.u64; \
        u2.fp = operand2; uint64_t op2 = u2.u64
// + SET via non-fetcing SWAP
#define _GASNETE_GNIRATOMIC_PREP_SET1(type) /* integer */ \
        const gasneti_op_idx_t op_idx = gasneti_op_idx_SET; \
        uint64_t val = operand1
#define _GASNETE_GNIRATOMIC_PREP_SET0(type) /* floating-point */ \
        const gasneti_op_idx_t op_idx = gasneti_op_idx_SET; \
        union { type fp; uint64_t u64; } u1;                \
        u1.fp = operand1; uint64_t val = u1.u64
// + GET via FAND(~0)
#define _GASNETE_GNIRATOMIC_PREP_GET() /* any type */ \
        const gasneti_op_idx_t op_idx = gasneti_op_idx_GET; \
        uint64_t op1 = -1
// + non-fetching INC or DEC: must generate +/- 1 operand for ADD
#define _GASNETE_GNIRATOMIC_PREP_INC1(type, op_idx) /* integer */ \
        gasneti_assert((op_idx == gasneti_op_idx_INC) ||        \
                       (op_idx == gasneti_op_idx_DEC));         \
        uint64_t inc = (op_idx == gasneti_op_idx_INC) ? 1 : -1
#define _GASNETE_GNIRATOMIC_PREP_INC0(type, op_idx) /* floating-point */ \
        gasneti_assert((op_idx == gasneti_op_idx_INC) ||        \
                       (op_idx == gasneti_op_idx_DEC));         \
        union { type fp; uint64_t u64; } u1;                    \
        u1.fp = (op_idx == gasneti_op_idx_INC) ? 1. : -1.;      \
        uint64_t inc = u1.u64
// + fetching INC or DEC: must generate +/1 1 operand for FADD
#define _GASNETE_GNIRATOMIC_PREP_FINC1(type, op_idx) /* integer */ \
        gasneti_assert((op_idx == gasneti_op_idx_FINC) ||       \
                       (op_idx == gasneti_op_idx_FDEC));        \
        uint64_t inc = (op_idx == gasneti_op_idx_FINC) ? 1 : -1
#define _GASNETE_GNIRATOMIC_PREP_FINC0(type, op_idx) /* floating-point */ \
        gasneti_assert((op_idx == gasneti_op_idx_FINC) ||       \
                       (op_idx == gasneti_op_idx_FDEC));        \
        union { type fp; uint64_t u64; } u1;                    \
        u1.fp = (op_idx == gasneti_op_idx_FINC) ? 1. : -1.;     \
        uint64_t inc = u1.u64
//
GASNETE_DT_APPLY(GASNETE_GNIRATOMIC_DEFN)

#endif // GASNETC_BUILD_GNIRATOMIC

/* ------------------------------------------------------------------------------------ */
/*
  Barriers:
  =========
  "gd" = GNI Dissemination
*/

#if defined(GASNET_CONDUIT_GEMINI) && PLATFORM_COMPILER_CRAY
/* Don't trust GNIDISSEM barrier with CCE - see bug 3191 */
#define GASNETE_BARRIER_DEFAULT "RDMADISSEM"
#else
#define GASNETE_BARRIER_DEFAULT "GNIDISSEM"
#endif

/* Forward decls for init function(s): */
static void gasnete_gdbarrier_init(gasnete_coll_team_t team);

#define GASNETE_BARRIER_READENV() do {                                   \
  if (GASNETE_ISBARRIER("GNIDISSEM"))                                    \
    gasnete_coll_default_barrier_type = GASNETE_COLL_BARRIER_GNIDISSEM;  \
} while (0)

#define GASNETE_BARRIER_INIT(TEAM, TYPE, NODES, SUPERNODES) do { \
    if ((TYPE) == GASNETE_COLL_BARRIER_GNIDISSEM &&              \
        (TEAM) == GASNET_TEAM_ALL) {                             \
      gasnete_gdbarrier_init(TEAM);                              \
    }                                                            \
  } while (0)

/* Can use the auxseg allocation from the generic implementation: */
static int gasnete_conduit_rdmabarrier(const char *barrier, gasneti_auxseg_request_t *result);
#define GASNETE_CONDUIT_RDMABARRIER gasnete_conduit_rdmabarrier

/* use reference implementation of barrier */
#define GASNETI_GASNET_EXTENDED_REFBARRIER_C 1
#include "gasnet_extended_refbarrier.c"
#undef GASNETI_GASNET_EXTENDED_REFBARRIER_C

static int gasnete_conduit_rdmabarrier(const char *barrier, gasneti_auxseg_request_t *result) {
  if (0 == strcmp(barrier, "GNIDISSEM")) {
    /* TODO: could keep the full space and allocate some to additional teams */
    size_t request;
#if GASNETI_PSHM_BARRIER_HIER
    const int is_hier = gasneti_getenv_yesno_withdefault("GASNET_PSHM_BARRIER_HIER", 1);
    const int size = is_hier ? gasneti_nodemap_global_count : gasneti_nodes;
#else
    const int size = gasneti_nodes;
#endif
    int steps, i;

    for (steps=0, i=1; i<size; ++steps, i*=2) /* empty */ ;

    request = 2 * steps * GASNETE_RDMABARRIER_INBOX_SZ;
    gasneti_assert_always(GASNETE_RDMABARRIER_INBOX_SZ >= sizeof(uint64_t));
    gasneti_assert_always(request <= result->optimalsz);
    result->minsz = request;
    result->optimalsz = request;
    return (steps != 0);
  }

  return 0;
}

/* ------------------------------------------------------------------------------------ */
/* GNI-specific RDMA-based Dissemination implementation of barrier
 * This is an adaptation of the "rmd" barrier in exteneded-ref.
 * Key differences:
 *  + no complications due to thread-specific events
 *  + simple 64-bit put since (aligned) 64-bit puts are atomic
 */

#if !GASNETI_THREADS
  #define GASNETE_GDBARRIER_LOCK(_var)		/* empty */
  #define gasnete_gdbarrier_lock_init(_var)	((void)0)
  #define gasnete_gdbarrier_trylock(_var)	(0/*success*/)
  #define gasnete_gdbarrier_unlock(_var)	((void)0)
#elif GASNETI_HAVE_SPINLOCK
  #define GASNETE_GDBARRIER_LOCK(_var)		gasneti_atomic_t _var;
  #define gasnete_gdbarrier_lock_init(_var)	gasneti_spinlock_init(_var)
  #define gasnete_gdbarrier_trylock(_var)	gasneti_spinlock_trylock(_var)
  #define gasnete_gdbarrier_unlock(_var)	gasneti_spinlock_unlock(_var)
#else
  #define GASNETE_GDBARRIER_LOCK(_var)		gasneti_mutex_t _var;
  #define gasnete_gdbarrier_lock_init(_var)	gasneti_mutex_init(_var)
  #define gasnete_gdbarrier_trylock(_var)	gasneti_mutex_trylock(_var)
  #define gasnete_gdbarrier_unlock(_var)	gasneti_mutex_unlock(_var)
#endif

typedef struct {
  GASNETE_GDBARRIER_LOCK(barrier_lock) /* no semicolon */
  struct {
    gex_Rank_t node;
    uint64_t      *addr;
  } *barrier_peers;           /*  precomputed list of peers to communicate with */
#if GASNETI_PSHM_BARRIER_HIER
  gasnete_pshmbarrier_data_t *barrier_pshm; /* non-NULL if using hierarchical code */
  int barrier_passive;        /*  2 if some other node makes progress for me, 0 otherwise */
#endif
  int barrier_goal;           /*  (1+ceil(lg(nodes)) << 1) == final barrier_state for phase=0 */
  int volatile barrier_state; /*  (step << 1) | phase, where step is 1-based (0 is pshm notify) */
  int volatile barrier_value; /*  barrier value (evolves from local value) */
  int volatile barrier_flags; /*  barrier flags (evolves from local value) */
  volatile uint64_t *barrier_inbox; /*  in-segment memory to recv notifications */
} gasnete_coll_gdbarrier_t;

/* Unlike the extended-ref version we CAN assume that a 64-bit write
 * will be atomic, and so can use a "present" bit in flags.
 */
#define GASNETE_GDBARRIER_PRESENT GASNETE_BARRIERFLAG_CONDUIT0
#define GASNETE_GDBARRIER_VALUE(_u64) GASNETI_HIWORD(_u64)
#define GASNETE_GDBARRIER_FLAGS(_u64) GASNETI_LOWORD(_u64)
#define GASNETE_GDBARRIER_BUILD(_value,_flags) \
                GASNETI_MAKEWORD((_value),GASNETE_GDBARRIER_PRESENT|(_flags))
  
/* Pad struct to a specfic size and interleave */
#define GASNETE_GDBARRIER_INBOX_WORDS (GASNETE_RDMABARRIER_INBOX_SZ/sizeof(uint64_t))
#define GASNETE_GDBARRIER_INBOX(_bd,_state)     \
            (((_bd)->barrier_inbox) \
                       + (unsigned)((_state)-2) * GASNETE_GDBARRIER_INBOX_WORDS)
#define GASNETE_GDBARRIER_INBOX_REMOTE(_bd,_step,_state)  \
            (((_bd)->barrier_peers[(unsigned)(_step)].addr \
                       + (unsigned)((_state)-2) * GASNETE_GDBARRIER_INBOX_WORDS))
#define GASNETE_GDBARRIER_INBOX_NEXT(_addr)    \
            ((_addr) + 2U * GASNETE_GDBARRIER_INBOX_WORDS)

GASNETI_INLINE(gasnete_gdbarrier_send)
void gasnete_gdbarrier_send(gasnete_coll_gdbarrier_t *barrier_data,
                             int numsteps, unsigned int state,
                             gex_AM_Arg_t value, gex_AM_Arg_t flags) {
  unsigned int step = state >> 1;
  const uint64_t payload = GASNETE_GDBARRIER_BUILD(value, flags);
  int i;

  gasneti_assert(sizeof(payload) <= sizeof(gex_RMA_Value_t));

  for (i = 0; i < numsteps; ++i, state += 2, step += 1) {
    const gex_Rank_t node = barrier_data->barrier_peers[step].node;
    uint64_t * const dst = GASNETE_GDBARRIER_INBOX_REMOTE(barrier_data, step, state);
#if GASNET_PSHM
    if (gasneti_pshm_in_supernode(node)) {
      *(uint64_t*)gasneti_pshm_addr2local(node, dst) = payload;
    } else
#endif
    {
      GASNETC_DIDX_POST((gasnete_mythread())->domain_idx);
      gasnetc_post_descriptor_t * const gpd = gasnetc_alloc_post_descriptor(0 GASNETC_DIDX_PASS);
      uint64_t * const src = (uint64_t *)GASNETE_STARTOFBITS(gpd->u.immediate, sizeof(uint64_t));

      gpd->gpd_flags = 0; /* fire and forget */
      *src = payload;
      gasnetc_rdma_put_buff(node, dst, src, sizeof(*src), gpd);
    }
  }
}

#if GASNETI_PSHM_BARRIER_HIER
static int gasnete_gdbarrier_kick_pshm(gasnete_coll_team_t team) {
  gasnete_coll_gdbarrier_t *barrier_data = team->barrier_data;
  int done = (barrier_data->barrier_state > 1);

  if (!done && !gasnete_gdbarrier_trylock(&barrier_data->barrier_lock)) {
    const int state = barrier_data->barrier_state;
    done = (state > 1);
    if (!done) {
      PSHM_BDATA_DECL(pshm_bdata, barrier_data->barrier_pshm);
      if (gasnete_pshmbarrier_kick(pshm_bdata)) {
        const int value = pshm_bdata->shared->value;
        const int flags = pshm_bdata->shared->flags;
        barrier_data->barrier_value = value;
        barrier_data->barrier_flags = flags;
        gasneti_sync_writes();
        barrier_data->barrier_state = state + 2;
        gasnete_gdbarrier_unlock(&barrier_data->barrier_lock); /* Cannot send while holding HSL */
        if ((barrier_data->barrier_goal > 2) && !barrier_data->barrier_passive) {
          gasnete_gdbarrier_send(barrier_data, 1, state+2, value, flags);
        } else {
          gasnete_barrier_pf_disable(team);
        }
        return 1;
      }
    }
    gasnete_gdbarrier_unlock(&barrier_data->barrier_lock);
  }

  return done;
}
#endif

void gasnete_gdbarrier_kick(gasnete_coll_team_t team) {
  gasnete_coll_gdbarrier_t *barrier_data = team->barrier_data;
  volatile uint64_t *inbox;
  uint64_t result;
  int numsteps = 0;
  int state, new_state;
  int flags, value;

  /* early unlocked read: */
  state = barrier_data->barrier_state;
  if (state >= barrier_data->barrier_goal)
    return; /* nothing to do */

  gasneti_assert(team->total_ranks > 1); /* singleton should have matched (state >= goal), above */

#if GASNETI_PSHM_BARRIER_HIER
  if (barrier_data->barrier_pshm) {
    /* Cannot begin to probe until local notify is complete */
    if (!gasnete_gdbarrier_kick_pshm(team)) return;
  }
#endif

  if (gasnete_gdbarrier_trylock(&barrier_data->barrier_lock))
    return; /* another thread is currently in kick */

  /* reread w/ lock held and/or because kick_pshm may have advanced it */
  state = barrier_data->barrier_state;

#if GASNETI_PSHM_BARRIER_HIER
  if_pf (state < 2) { /* local notify has not completed */
    gasnete_gdbarrier_unlock(&barrier_data->barrier_lock);
    return;
  } else if (barrier_data->barrier_passive) {
    gasnete_barrier_pf_disable(team);
    gasnete_gdbarrier_unlock(&barrier_data->barrier_lock);
    return;
  }
  gasneti_assert(!barrier_data->barrier_passive);
#endif

#if GASNETI_THREADS
  if_pf (state < 4) {/* need to pick up value/flags from notify */
    gasneti_sync_reads(); /* value/flags were written by the non-locked notify */
  }
#endif

  value = barrier_data->barrier_value;
  flags = barrier_data->barrier_flags;

  /* process all consecutive steps which have arrived since we last ran */
  inbox = GASNETE_GDBARRIER_INBOX(barrier_data, state);
  for (new_state = state; new_state < barrier_data->barrier_goal && (0 != (result = *inbox)); new_state+=2) {
    const int step_value = GASNETE_GDBARRIER_VALUE(result);
    const int step_flags = GASNETE_GDBARRIER_FLAGS(result);

#if PLATFORM_COMPILER_CRAY
    /* Cray C (at least 8.1.x) is droping the (0 != ...) check in the while().
     * Adding this line works-around the problem.
     * Note that (!result) doesn't work here because it gets dropped too!
     */
    if (!step_flags) break;
#else
    gasneti_assert(step_flags);
#endif

    *inbox = 0;

    if ((flags | step_flags) & GASNET_BARRIERFLAG_MISMATCH) {
      flags = GASNET_BARRIERFLAG_MISMATCH; 
    } else if (flags & GASNET_BARRIERFLAG_ANONYMOUS) {
      flags = step_flags; 
      value = step_value; 
    } else if (!(step_flags & GASNET_BARRIERFLAG_ANONYMOUS) && (step_value != value)) {
      flags = GASNET_BARRIERFLAG_MISMATCH; 
    }

    ++numsteps;
    inbox = GASNETE_GDBARRIER_INBOX_NEXT(inbox);
  }

  if (numsteps) { /* completed one or more steps */
    barrier_data->barrier_flags = flags; 
    barrier_data->barrier_value = value; 

    if (new_state >= barrier_data->barrier_goal) { /* We got the last recv - barrier locally complete */
      gasnete_barrier_pf_disable(team);
      gasneti_sync_writes(); /* flush state before the write to barrier_state below */
      numsteps -= 1; /* no send at last step */
    } 
    /* notify all threads of the step increase - 
       this may allow other local threads to proceed on the barrier and even indicate
       barrier completion while we overlap outgoing notifications to other nodes
    */
    barrier_data->barrier_state = new_state;
  } 

  gasnete_gdbarrier_unlock(&barrier_data->barrier_lock);

  if (numsteps) { /* need to issue one or more Puts */
    gasnete_gdbarrier_send(barrier_data, numsteps, state+2, value, flags);
  }
}

static void gasnete_gdbarrier_notify(gasnete_coll_team_t team, int id, int flags) {
  gasnete_coll_gdbarrier_t *barrier_data = team->barrier_data;
  int state = 2 + ((barrier_data->barrier_state & 1) ^ 1); /* enter new phase */
  int do_send = 1;
  int want_pf = 1;

  GASNETE_SPLITSTATE_NOTIFY_ENTER(team);

#if GASNETI_PSHM_BARRIER_HIER
  if (barrier_data->barrier_pshm) {
    PSHM_BDATA_DECL(pshm_bdata, barrier_data->barrier_pshm);
    if (gasnete_pshmbarrier_notify_inner(pshm_bdata, id, flags)) {
      id = pshm_bdata->shared->value;
      flags = pshm_bdata->shared->flags;
      want_pf = do_send = !barrier_data->barrier_passive;
    } else {
      do_send = 0;
      state -= 2;
    }
  }
#endif

  barrier_data->barrier_value = id;
  barrier_data->barrier_flags = flags;

  gasneti_sync_writes();
  barrier_data->barrier_state = state;

  if (do_send) gasnete_gdbarrier_send(barrier_data, 1, state, id, flags);
  if (want_pf) gasnete_barrier_pf_enable(team);

  /*  update state */
  gasneti_sync_writes(); /* ensure all state changes committed before return */
}

/* Notify specialized to one (super)node case (reduced branches in BOTH variants) */
static void gasnete_gdbarrier_notify_singleton(gasnete_coll_team_t team, int id, int flags) {
  gasnete_coll_gdbarrier_t *barrier_data = team->barrier_data;
#if GASNETI_PSHM_BARRIER_HIER
  int state = 2;
#endif

  GASNETE_SPLITSTATE_NOTIFY_ENTER(team);

#if GASNETI_PSHM_BARRIER_HIER
  if (barrier_data->barrier_pshm) {
    PSHM_BDATA_DECL(pshm_bdata, barrier_data->barrier_pshm);
    if (gasnete_pshmbarrier_notify_inner(pshm_bdata, id, flags)) {
      id = pshm_bdata->shared->value;
      flags = pshm_bdata->shared->flags;
    } else {
      state = 0;
    }
  }
#endif

  barrier_data->barrier_value = id;
  barrier_data->barrier_flags = flags;

#if GASNETI_PSHM_BARRIER_HIER
  gasneti_sync_writes();
  barrier_data->barrier_state = state;
  if (!state) gasnete_barrier_pf_enable(team);
#endif

  /*  update state */
  gasneti_sync_writes(); /* ensure all state changes committed before return */
}

static int gasnete_gdbarrier_wait(gasnete_coll_team_t team, int id, int flags) {
  gasnete_coll_gdbarrier_t *barrier_data = team->barrier_data;
#if GASNETI_PSHM_BARRIER_HIER
  PSHM_BDATA_DECL(pshm_bdata, barrier_data->barrier_pshm);
#endif
  int retval = GASNET_OK;

  gasneti_sync_reads(); /* ensure we read correct state */
  GASNETE_SPLITSTATE_WAIT_LEAVE(team);

#if GASNETI_PSHM_BARRIER_HIER
  if (pshm_bdata) {
    const int passive_shift = barrier_data->barrier_passive;
    gasneti_polluntil(gasnete_gdbarrier_kick_pshm(team));
    retval = gasnete_pshmbarrier_wait_inner(pshm_bdata, id, flags, passive_shift);
    if (passive_shift) {
      /* Once the active peer signals done, we can return */
      barrier_data->barrier_value = pshm_bdata->shared->value;
      barrier_data->barrier_flags = pshm_bdata->shared->flags;
      gasneti_sync_writes(); /* ensure all state changes committed before return */
      return retval;
    }
  }
#endif

  if (barrier_data->barrier_state >= barrier_data->barrier_goal) {
    /* completed asynchronously before wait (via progressfns or try) */
    GASNETI_TRACE_EVENT_TIME(B,BARRIER_ASYNC_COMPLETION,GASNETI_TICKS_NOW_IFENABLED(B)-gasnete_barrier_notifytime);
  } else {
    /* kick once, and if still necessary, wait for a response */
    gasnete_gdbarrier_kick(team);
    /* cannot BLOCKUNTIL since progess may occur on non-AM events */
    while (barrier_data->barrier_state < barrier_data->barrier_goal) {
      GASNETI_WAITHOOK();
      GASNETI_SAFE(gasneti_AMPoll());
      gasnete_gdbarrier_kick(team);
    }
  }
  gasneti_sync_reads(); /* ensure correct state will be read */

  /* determine return value */
  if_pf (barrier_data->barrier_flags & GASNET_BARRIERFLAG_MISMATCH) {
    retval = GASNET_ERR_BARRIER_MISMATCH;
  } else
  if_pf(/* try/wait value must match consensus value, if both are present */
        !((flags|barrier_data->barrier_flags) & GASNET_BARRIERFLAG_ANONYMOUS) &&
	 ((gex_AM_Arg_t)id != barrier_data->barrier_value)) {
    retval = GASNET_ERR_BARRIER_MISMATCH;
  }

  /*  update state */
#if GASNETI_PSHM_BARRIER_HIER
  if (pshm_bdata) {
    /* Signal any passive peers w/ the final result */
    pshm_bdata->shared->value = barrier_data->barrier_value;
    pshm_bdata->shared->flags = barrier_data->barrier_flags;
    PSHM_BSTATE_SIGNAL(pshm_bdata, retval, pshm_bdata->private.two_to_phase << 2); /* includes a WMB */
    gasneti_assert(!barrier_data->barrier_passive);
  } else
#endif
  gasneti_sync_writes(); /* ensure all state changes committed before return */

  return retval;
}

static int gasnete_gdbarrier_try(gasnete_coll_team_t team, int id, int flags) {
  gasnete_coll_gdbarrier_t *barrier_data = team->barrier_data;
  gasneti_sync_reads(); /* ensure we read correct state */

  GASNETE_SPLITSTATE_TRY(team);

  GASNETI_SAFE(gasneti_AMPoll());

#if GASNETI_PSHM_BARRIER_HIER
  if (barrier_data->barrier_pshm) {
    const int passive_shift = barrier_data->barrier_passive;
    if (!gasnete_gdbarrier_kick_pshm(team) ||
        !gasnete_pshmbarrier_try_inner(barrier_data->barrier_pshm, passive_shift))
      return GASNET_ERR_NOT_READY;
    if (passive_shift)
      return gasnete_gdbarrier_wait(team, id, flags);
  }
  if (!barrier_data->barrier_passive)
#endif
    gasnete_gdbarrier_kick(team);

  if (barrier_data->barrier_state >= barrier_data->barrier_goal)
    return gasnete_gdbarrier_wait(team, id, flags);
  else return GASNET_ERR_NOT_READY;
}

static int gasnete_gdbarrier_result(gasnete_coll_team_t team, int *id) {
  gasneti_sync_reads();
  GASNETE_SPLITSTATE_RESULT(team);

  { const gasnete_coll_gdbarrier_t * const barrier_data = team->barrier_data;
    *id = barrier_data->barrier_value;
    return (GASNET_BARRIERFLAG_ANONYMOUS & barrier_data->barrier_flags);
  }
}

void gasnete_gdbarrier_kick_team_all(void) {
  gasnete_gdbarrier_kick(GASNET_TEAM_ALL);
}

static void gasnete_gdbarrier_init(gasnete_coll_team_t team) {
  gasnete_coll_gdbarrier_t *barrier_data;
  int steps;
  int total_ranks = team->total_ranks;
  int myrank = team->myrank;
  gasnete_coll_peer_list_t *peers = &team->peers;

#if GASNETI_PSHM_BARRIER_HIER
  PSHM_BDATA_DECL(pshm_bdata, gasnete_pshmbarrier_init_hier(team, &total_ranks, &myrank, &peers));
#endif

  barrier_data = gasneti_malloc_aligned(GASNETI_CACHE_LINE_BYTES, sizeof(gasnete_coll_gdbarrier_t));
  gasneti_leak_aligned(barrier_data);
  memset(barrier_data, 0, sizeof(gasnete_coll_gdbarrier_t));
  team->barrier_data = barrier_data;

#if GASNETI_PSHM_BARRIER_HIER
  if (pshm_bdata) {
    barrier_data->barrier_passive = (pshm_bdata->private.rank != 0) ? 2 : 0; /* precompute shift */
    barrier_data->barrier_pshm = pshm_bdata;
  }
#endif

  gasneti_assert(team == GASNET_TEAM_ALL); /* TODO: deal w/ in-segment allocation */

  gasnete_gdbarrier_lock_init(&barrier_data->barrier_lock);

  steps = peers->num;
  barrier_data->barrier_goal = (1+steps) << 1;

  if (steps) {
    int step;

    gasneti_assert(gasnete_rdmabarrier_auxseg);
    barrier_data->barrier_inbox = gasnete_rdmabarrier_auxseg[gasneti_mynode].addr;

    barrier_data->barrier_peers = gasneti_malloc((1+steps) * sizeof(* barrier_data->barrier_peers));
    gasneti_leak(barrier_data->barrier_peers);
  
    for (step = 0; step < steps; ++step) {
      gex_Rank_t node = peers->fwd[step];
      barrier_data->barrier_peers[1+step].node = node;
      barrier_data->barrier_peers[1+step].addr = gasnete_rdmabarrier_auxseg[node].addr;
    }
  } else {
    barrier_data->barrier_state = barrier_data->barrier_goal;
  }

  gasneti_free(gasnete_rdmabarrier_auxseg);

#if GASNETI_PSHM_BARRIER_HIER
  if (pshm_bdata && (pshm_bdata->shared->size == 1)) {
    /* With singleton proc on local supernode we can short-cut the PSHM code.
     * This does not require alteration of the barrier_peers[] contructed above
     */
    gasnete_pshmbarrier_fini_inner(pshm_bdata);
    barrier_data->barrier_pshm = NULL;
  }
#endif

  team->barrier_notify = steps ? &gasnete_gdbarrier_notify : &gasnete_gdbarrier_notify_singleton;
  team->barrier_wait =   &gasnete_gdbarrier_wait;
  team->barrier_try =    &gasnete_gdbarrier_try;
  team->barrier_result = &gasnete_gdbarrier_result;
  team->barrier_pf =     (team == GASNET_TEAM_ALL) ? &gasnete_gdbarrier_kick_team_all : NULL;
}

/* ------------------------------------------------------------------------------------ */
/*
  Vector, Indexed & Strided:
  =========================
*/

/* use reference implementation of scatter/gather and strided */
#include "gasnet_extended_refvis.h"

/* ------------------------------------------------------------------------------------ */
/*
  Collectives:
  ============
*/

/* use reference implementation of collectives */
#include "gasnet_extended_refcoll.h"

/* ------------------------------------------------------------------------------------ */
/*
  Remote Atomics:
  ==============
*/

/* use reference implementation of remote atomics */
#include "gasnet_extended_refratomic.h"

/* ------------------------------------------------------------------------------------ */
/*
  Handlers:
  =========
*/
static gex_AM_Entry_t const gasnete_handlers[] = {
  #ifdef GASNETE_REFBARRIER_HANDLERS
    GASNETE_REFBARRIER_HANDLERS(),
  #endif
  #ifdef GASNETE_REFVIS_HANDLERS
    GASNETE_REFVIS_HANDLERS()
  #endif
  #ifdef GASNETE_REFCOLL_HANDLERS
    GASNETE_REFCOLL_HANDLERS()
  #endif
  #ifdef GASNETE_AMREF_HANDLERS
    GASNETE_AMREF_HANDLERS()
  #endif
  #ifdef GASNETE_AMRATOMIC_HANDLERS
    GASNETE_AMRATOMIC_HANDLERS()
  #endif

  /* ptr-width independent handlers */

  /* ptr-width dependent handlers */

  GASNETI_HANDLER_EOT
};

extern gex_AM_Entry_t const *gasnete_get_handlertable(void) {
  return gasnete_handlers;
}
/* ------------------------------------------------------------------------------------ */


/*   $Source: bitbucket.org:berkeleylab/gasnet.git/ibv-conduit/gasnet_ratomic.c $
 * Description: GASNet Remote Atomics Implementation using IBV NIC offload
 * Copyright 2025, The Regents of the University of California
 * Terms of use are as specified in license.txt
 */

#define GASNETI_NEED_GASNET_RATOMIC_H 1
#include <gasnet_internal.h>

#if GASNETC_BUILD_IBVRATOMIC // Else entire file is empty

#include <gasnet_core_internal.h>
#include <gasnet_ratomic_internal.h>
#include <gasnet_event_internal.h>

//
// completion callbacks
//

extern void gasnetc_cb_eop_rmw(gasnetc_atomic_val_t *p) {
  gasnete_eop_t *eop = gasneti_container_of(p, gasnete_eop_t, initiated_cnt);
  gasnete_eop_check(eop);
  // sync_writes orders our caller's (optional) write of the fetched value prior to eop completion
  gasneti_sync_writes();
  // Non-atomic decrement is chaper than atomic increment of eop->completed_cnt.
  // Correct because (unlike Put and Get) there cannot be concurrency between
  // advancing this counter at AMO injection and the corresponding callback.
  (*p) -= 1;
  GASNETE_EOP_MARKDONE(eop);
}

extern void gasnetc_cb_iop_rmw(gasnetc_atomic_val_t *p) {
  gasnete_iop_t *iop = gasneti_container_of(p, gasnete_iop_t, initiated_rmw_cnt);
  gasnete_iop_check(iop);
  // REL fence orders our caller's (optional) write of the fetched value prior to iop completion
  GASNETE_IOP_CNT_FINISH(iop, rmw, 1, GASNETI_ATOMIC_REL);
}

//
// Misc. helpers
//

GASNETI_INLINE(gasnete_ratomic_jobrank)
gex_Rank_t gasnete_ratomic_jobrank(gasneti_TM_t i_tm, gex_Rank_t tgt_rank, gex_Flags_t flags)
{
  if (flags & GEX_FLAG_RANK_IS_JOBRANK) {
    gasneti_assert(GEX_RANK_INVALID != gasneti_i_tm_jobrank_to_rank(i_tm, tgt_rank));
    return tgt_rank;
  } else {
    return gasneti_i_tm_rank_to_jobrank(i_tm, tgt_rank);
  }
}

//
// low-level OP injection
// Subject to specialization on 'opcode' and 'fetching' when inlined
//
GASNETI_INLINE(gasnete_ratomic_inner)
int gasnete_ratomic_inner(
                gasneti_TM_t i_tm, int fetching, void *result_p,
                gex_Rank_t tgt_rank, void *tgt_addr, gex_Flags_t flags,
                enum ibv_wr_opcode opcode, uint64_t operand1, uint64_t operand2,
                gasnetc_atomic_val_t *initiated_cnt, gasnetc_cb_t completion_cb
                GASNETI_THREAD_FARG)
{
  void *bbuf = NULL;
  if (fetching) {
    // TODO: should not be using pool of 4K bounce buffers for 8-byte results
    bbuf = gasnetc_get_bbuf(!(flags & GEX_FLAG_IMMEDIATE) GASNETI_THREAD_PASS);
    if (!bbuf) return 1;
  }

  gasnetc_EP_t ep = (gasnetc_EP_t) i_tm->_ep;
  gex_Rank_t jobrank = gasnete_ratomic_jobrank(i_tm, tgt_rank, flags);
  gasnetc_epid_t epid = gasnetc_epid(jobrank,0); // Always using only first CEP per jobrank
  const int rem_auxseg = gasneti_in_auxsegment(jobrank, tgt_addr, sizeof(uint64_t));

  gasnetc_sreq_t *sreq = gasnetc_get_sreq(GASNETC_OP_INVALID GASNETI_THREAD_PASS);
  gasnetc_cep_t *cep = gasnetc_bind_cep(ep, epid, sreq);

  GASNETC_DECL_SR_DESC(sr_desc, 1);

  sr_desc->wr.atomic.remote_addr = (uintptr_t)tgt_addr;
  sr_desc->wr.atomic.rkey = rem_auxseg ? cep->hca->aux_rkeys[jobrank]
                                       : GASNETC_SEG_RKEY(cep);
  sr_desc->num_sge = 1;
  sr_desc->sg_list[0].length = sizeof(uint64_t);

  if (!fetching) {
    gasneti_assert(! result_p);
    sreq->opcode = GASNETC_OP_ATOMIC;
    sr_desc->sg_list[0].addr = (uintptr_t)GASNETC_RATOMIC_SINK(cep);
    sr_desc->sg_list[0].lkey = cep->hca->aux_reg.handle->lkey;
  } else {
    // TODO: zero-copy for in-segment result_p?
    gasneti_assert(result_p);
    gasneti_assert(bbuf);
    sreq->opcode = GASNETC_OP_ATOMIC_BOUNCE;
    sreq->amo_result = result_p;
    sreq->amo_bbuf = bbuf;
    sr_desc->sg_list[0].addr = (uintptr_t)sreq->amo_bbuf;
    sr_desc->sg_list[0].lkey = GASNETC_SND_LKEY(cep);
  }

  sr_desc->opcode = opcode;
  sr_desc->wr.atomic.compare_add = operand1;
  if (opcode == IBV_WR_ATOMIC_CMP_AND_SWP) {
    sr_desc->wr.atomic.swap = operand2;
  }

  sreq->comp.cb = completion_cb;
  sreq->comp.data = initiated_cnt;

  (*initiated_cnt) += 1;
  gasnetc_snd_post_common(sreq, sr_desc, 0 GASNETI_THREAD_PASS);

  return 0;
}

GASNETI_INLINE(gasnete_ratomic_nb)
gex_Event_t gasnete_ratomic_nb(
                gasneti_TM_t i_tm, int fetching, void *result_p,
                gex_Rank_t tgt_rank, void *tgt_addr, gex_Flags_t flags,
                enum ibv_wr_opcode opcode, uint64_t operand1, uint64_t operand2
                GASNETI_THREAD_FARG)
{
  gasnete_eop_t *eop = gasnete_eop_new(GASNETI_MYTHREAD);
  int imm = gasnete_ratomic_inner(
                     i_tm, fetching, result_p, tgt_rank, tgt_addr,
                     flags, opcode, operand1, operand2,
                     &eop->initiated_cnt, gasnetc_cb_eop_rmw
                     GASNETI_THREAD_PASS);
  if (imm) {
    SET_EVENT_DONE(eop, 0);
    gasnete_eop_free(eop GASNETI_THREAD_PASS);
    return GEX_EVENT_NO_OP;
  }
  return (gex_Event_t)eop;
}

GASNETI_INLINE(gasnete_ratomic_nbi)
int gasnete_ratomic_nbi(
                gasneti_TM_t i_tm, int fetching, void *result_p,
                gex_Rank_t tgt_rank, void *tgt_addr, gex_Flags_t flags,
                enum ibv_wr_opcode opcode, uint64_t operand1, uint64_t operand2
                GASNETI_THREAD_FARG)
{
  gasnete_iop_t * const iop = GASNETI_MYTHREAD->current_iop;
  return gasnete_ratomic_inner(
                     i_tm, fetching, result_p, tgt_rank, tgt_addr,
                     flags, opcode, operand1, operand2,
                     &iop->initiated_rmw_cnt, gasnetc_cb_iop_rmw
                     GASNETI_THREAD_PASS);
}

//
// Mid-level (inline) functions marshalling arguments to atomic op injection
//
#define GASNETE_IBVRATOMIC_MID(dtcode) \
        _GASNETE_IBVRATOMIC_MID1(gasnete_ibvratomic##dtcode, dtcode##_type)
#define _GASNETE_IBVRATOMIC_MID1(prefix, type) \
  GASNETI_INLINE(prefix##_NB_Nadd) \
  gex_Event_t prefix##_NB_Nadd(type addend, GASNETE_RATOMIC_ARGS_N0(type))  \
  {                                                                         \
    gasneti_TM_t _i_tm = _real_ad->_tm;                                     \
    return gasnete_ratomic_nb (_i_tm, 0, NULL, _tgt_rank, _tgt_addr,        \
                               _flags, IBV_WR_ATOMIC_FETCH_AND_ADD,         \
                               addend, 0 GASNETI_THREAD_PASS);              \
  } \
  GASNETI_INLINE(prefix##_NBI_Nadd) \
  int prefix##_NBI_Nadd(type addend, GASNETE_RATOMIC_ARGS_N0(type))         \
  {                                                                         \
    gasneti_TM_t _i_tm = _real_ad->_tm;                                     \
    return gasnete_ratomic_nbi(_i_tm, 0, NULL, _tgt_rank, _tgt_addr,        \
                               _flags, IBV_WR_ATOMIC_FETCH_AND_ADD,         \
                               addend, 0 GASNETI_THREAD_PASS);              \
  } \
  \
  GASNETI_INLINE(prefix##_NB_Fadd) \
  gex_Event_t prefix##_NB_Fadd(type addend, GASNETE_RATOMIC_ARGS_F0(type))  \
  {                                                                         \
    gasneti_TM_t _i_tm = _real_ad->_tm;                                     \
    return gasnete_ratomic_nb (_i_tm, 1, _result_p, _tgt_rank, _tgt_addr,   \
                               _flags, IBV_WR_ATOMIC_FETCH_AND_ADD,         \
                               addend, 0 GASNETI_THREAD_PASS);              \
  } \
  GASNETI_INLINE(prefix##_NBI_Fadd) \
  int prefix##_NBI_Fadd(type addend, GASNETE_RATOMIC_ARGS_F0(type))         \
  {                                                                         \
    gasneti_TM_t _i_tm = _real_ad->_tm;                                     \
    return gasnete_ratomic_nbi(_i_tm, 1, _result_p, _tgt_rank, _tgt_addr,   \
                               _flags, IBV_WR_ATOMIC_FETCH_AND_ADD,         \
                               addend, 0 GASNETI_THREAD_PASS);              \
  } \
  \
  GASNETI_INLINE(prefix##_NB_Ncas) \
  gex_Event_t prefix##_NB_Ncas(GASNETE_RATOMIC_ARGS_N2(type))               \
  {                                                                         \
    gasneti_TM_t _i_tm = _real_ad->_tm;                                     \
    return gasnete_ratomic_nb (_i_tm, 0, NULL, _tgt_rank, _tgt_addr,        \
                               _flags, IBV_WR_ATOMIC_CMP_AND_SWP,           \
                               _operand1, _operand2 GASNETI_THREAD_PASS);   \
  } \
  GASNETI_INLINE(prefix##_NBI_Ncas) \
  int prefix##_NBI_Ncas(GASNETE_RATOMIC_ARGS_N2(type))                      \
  {                                                                         \
    gasneti_TM_t _i_tm = _real_ad->_tm;                                     \
    return gasnete_ratomic_nbi(_i_tm, 0, NULL, _tgt_rank, _tgt_addr,        \
                               _flags, IBV_WR_ATOMIC_CMP_AND_SWP,           \
                               _operand1, _operand2 GASNETI_THREAD_PASS);   \
  } \
  \
  GASNETI_INLINE(prefix##_NB_Fcas) \
  gex_Event_t prefix##_NB_Fcas(GASNETE_RATOMIC_ARGS_F2(type))               \
  {                                                                         \
    gasneti_TM_t _i_tm = _real_ad->_tm;                                     \
    return gasnete_ratomic_nb (_i_tm, 1, _result_p, _tgt_rank, _tgt_addr,   \
                               _flags, IBV_WR_ATOMIC_CMP_AND_SWP,           \
                               _operand1, _operand2 GASNETI_THREAD_PASS);   \
  } \
  GASNETI_INLINE(prefix##_NBI_Fcas) \
  int prefix##_NBI_Fcas(GASNETE_RATOMIC_ARGS_F2(type))                      \
  {                                                                         \
    gasneti_TM_t _i_tm = _real_ad->_tm;                                     \
    return gasnete_ratomic_nbi(_i_tm, 1, _result_p, _tgt_rank, _tgt_addr,   \
                               _flags, IBV_WR_ATOMIC_CMP_AND_SWP,           \
                               _operand1, _operand2 GASNETI_THREAD_PASS);   \
  }
//
GASNETE_IBVRATOMIC_MID(_gex_dt_U64)
GASNETE_IBVRATOMIC_MID(_gex_dt_I64)

// GET operations (type independent)
// These are use nop CAS or FADD as an atomic Get
//
// These do not just call gasnete_ratomic_{nb,nbi}, since at least NBI
// needs to use different counter and callback.

#ifdef GASNETC_RATOMIC_GET_OP
  // Preserve existing value
#else
  // Uncomment exactly one:
  //#define GASNETC_RATOMIC_GET_OP CMP_AND_SWP
  #define GASNETC_RATOMIC_GET_OP FETCH_AND_ADD
#endif
#define GASNETC_RATOMIC_GET_OPCODE _CONCAT(IBV_WR_ATOMIC_,GASNETC_RATOMIC_GET_OP)

static
gex_Event_t gasnete_ratomic_get_nb(
        gasneti_TM_t i_tm, void *result_p,
        gex_Rank_t tgt_rank, void *tgt_addr,
        size_t nbytes,
        gex_Flags_t flags GASNETI_THREAD_FARG)
{
  gasnete_eop_t *eop = gasnete_eop_new(GASNETI_MYTHREAD);
  int imm = gasnete_ratomic_inner(
                     i_tm, 1, result_p, tgt_rank, tgt_addr,
                     flags, GASNETC_RATOMIC_GET_OPCODE, 0, 0,
                     &eop->initiated_cnt, gasnetc_cb_eop_get
                     GASNETI_THREAD_PASS);
  if (imm) {
    SET_EVENT_DONE(eop, 0);
    gasnete_eop_free(eop GASNETI_THREAD_PASS);
    return GEX_EVENT_NO_OP;
  }
  return (gex_Event_t)eop;
}

static
int gasnete_ratomic_get_nbi(
        gasneti_TM_t i_tm, void *result_p,
        gex_Rank_t tgt_rank, void *tgt_addr,
        size_t nbytes,
        gex_Flags_t flags GASNETI_THREAD_FARG)
{
  gasnete_iop_t * const iop = GASNETI_MYTHREAD->current_iop;
  return gasnete_ratomic_inner(
                     i_tm, 1, result_p, tgt_rank, tgt_addr,
                     flags, GASNETC_RATOMIC_GET_OPCODE, 0, 0,
                     &iop->initiated_get_cnt,
                     iop->next ? gasnetc_cb_nar_get : gasnetc_cb_iop_get
                     GASNETI_THREAD_PASS);
}

//
// Ratomic OP functions (called by top-level dispatch functions) which
// call the GET or mid-level inline functions defined above.
//

// Accessors (GET)
#define _GASNETE_IBVRATOMIC_DEF_GET(dtcode) \
  static gex_Event_t gasnete_ibvratomic##dtcode##_NB_GET(GASNETE_RATOMIC_ARGS_F0(dtcode##_type)) { \
    return gasnete_ratomic_get_nb (_real_ad->_tm, _result_p, _tgt_rank, _tgt_addr,                 \
                                   sizeof(dtcode##_type), _flags GASNETI_THREAD_PASS);             \
  } \
  static int gasnete_ibvratomic##dtcode##_NBI_GET(GASNETE_RATOMIC_ARGS_F0(dtcode##_type)) {        \
    return gasnete_ratomic_get_nbi(_real_ad->_tm, _result_p, _tgt_rank, _tgt_addr,                 \
                                   sizeof(dtcode##_type), _flags GASNETI_THREAD_PASS);             \
  }
// (F)ADD operations
#define _GASNETE_IBVRATOMIC_DEF_ADD(dtcode,opstem,nargs,addend) \
        _GASNETE_IBVRATOMIC_DEF_ADD1(dtcode,   opstem,N##nargs,N,addend) \
        _GASNETE_IBVRATOMIC_DEF_ADD1(dtcode,F##opstem,F##nargs,F,addend)
#define _GASNETE_IBVRATOMIC_DEF_ADD1(dtcode,opname,args,fetching,addend) \
  static gex_Event_t gasnete_ibvratomic##dtcode##_NB_##opname(GASNETE_RATOMIC_ARGS_##args(dtcode##_type)) { \
    return gasnete_ibvratomic##dtcode##_NB_##fetching##add(addend,GASNETE_RATOMIC_PASS_##fetching##0);      \
  } \
  static int gasnete_ibvratomic##dtcode##_NBI_##opname(GASNETE_RATOMIC_ARGS_##args(dtcode##_type)) {        \
    return gasnete_ibvratomic##dtcode##_NBI_##fetching##add(addend,GASNETE_RATOMIC_PASS_##fetching##0);     \
  }
// (F)CAS operations
#define _GASNETE_IBVRATOMIC_DEF_CAS(dtcode) \
        _GASNETE_IBVRATOMIC_DEF_CAS1(dtcode, CAS,N2,N) \
        _GASNETE_IBVRATOMIC_DEF_CAS1(dtcode,FCAS,F2,F)
#define _GASNETE_IBVRATOMIC_DEF_CAS1(dtcode,opname,args,fetching) \
  static gex_Event_t gasnete_ibvratomic##dtcode##_NB_##opname(GASNETE_RATOMIC_ARGS_##args(dtcode##_type)) { \
    return gasnete_ibvratomic##dtcode##_NB_##fetching##cas(GASNETE_RATOMIC_PASS_##args);                    \
  } \
  static int gasnete_ibvratomic##dtcode##_NBI_##opname(GASNETE_RATOMIC_ARGS_##args(dtcode##_type)) {        \
    return gasnete_ibvratomic##dtcode##_NBI_##fetching##cas(GASNETE_RATOMIC_PASS_##args);                   \
  }
// Unreachable functions for non-offloadable ops
#define _GASNETE_IBVRATOMIC_BAD2(dtcode,opstem,nargs) \
        _GASNETE_IBVRATOMIC_BAD1(dtcode,opstem,N##nargs) \
        _GASNETE_IBVRATOMIC_BAD1(dtcode,F##opstem,F##nargs)
#define _GASNETE_IBVRATOMIC_BAD1(dtcode,opname,args) \
  static gex_Event_t gasnete_ibvratomic##dtcode##_NB_##opname(GASNETE_RATOMIC_ARGS_##args(dtcode##_type)) { \
    gasneti_unreachable_error(("Invalid offload of gex_AD_OpNB_" dtcode##_string "(GEX_OP_" #opname ")"));  \
    return GEX_EVENT_INVALID;                                                                               \
  } \
  static int gasnete_ibvratomic##dtcode##_NBI_##opname(GASNETE_RATOMIC_ARGS_##args(dtcode##_type)) {        \
    gasneti_unreachable_error(("Invalid offload of gex_AD_OpNBI_" dtcode##_string "(GEX_OP_" #opname ")")); \
    return 0;                                                                                               \
  }

#define GASNETE_IBVRATOMIC_DEFS(dtcode) \
  _GASNETE_IBVRATOMIC_DEF_GET(dtcode)       \
  \
  _GASNETE_IBVRATOMIC_DEF_ADD(dtcode,ADD,1,_operand1)  \
  _GASNETE_IBVRATOMIC_DEF_ADD(dtcode,SUB,1,-_operand1) \
  _GASNETE_IBVRATOMIC_DEF_ADD(dtcode,INC,0,1)          \
  _GASNETE_IBVRATOMIC_DEF_ADD(dtcode,DEC,0,-1)         \
  \
  _GASNETE_IBVRATOMIC_DEF_CAS(dtcode)       \
  \
  _GASNETE_IBVRATOMIC_BAD2(dtcode,AND,1)    \
  _GASNETE_IBVRATOMIC_BAD2(dtcode,OR,1)     \
  _GASNETE_IBVRATOMIC_BAD2(dtcode,XOR,1)    \
  _GASNETE_IBVRATOMIC_BAD2(dtcode,MULT,1)   \
  _GASNETE_IBVRATOMIC_BAD2(dtcode,MIN,1)    \
  _GASNETE_IBVRATOMIC_BAD2(dtcode,MAX,1)    \
  _GASNETE_IBVRATOMIC_BAD1(dtcode,SET,N1)   \
  _GASNETE_IBVRATOMIC_BAD1(dtcode,SWAP,F1)
//
GASNETE_IBVRATOMIC_DEFS(_gex_dt_U64)
GASNETE_IBVRATOMIC_DEFS(_gex_dt_I64)

//
// Build the dispatch tables
//
#define GASNETE_IBVRATOMIC_TBL(dtcode) \
    gasnete_ratomic##dtcode##_fn_tbl_t gasnete_ibvratomic##dtcode##_fn_tbl = \
        GASNETE_RATOMIC_FN_TBL_INIT(gasnete_ibvratomic##dtcode,dtcode);
GASNETE_IBVRATOMIC_TBL(_gex_dt_U64)
GASNETE_IBVRATOMIC_TBL(_gex_dt_I64)

//
// Masks of available capabilities
//
#define GASNETE_IBVRATOMIC_BASE_OPS ( GEX_OP_CAS | GEX_OP_FCAS | GEX_OP_GET ) // Note lack of SET
#define GASNETE_IBVRATOMIC_FADD_OPS ( GEX_OP_ADD | GEX_OP_FADD | GEX_OP_SUB | GEX_OP_FSUB | \
                                      GEX_OP_INC | GEX_OP_FINC | GEX_OP_DEC | GEX_OP_FDEC )
#define GASNETE_IBVRATOMIC_TYPES ( GEX_DT_I64 | GEX_DT_U64 )

//
// Init-hook to install the dispatch tables (aka algorithm selection)
//
void gasnete_ibvratomic_init_hook(gasneti_AD_t real_ad)
{
    gex_Flags_t flags = real_ad->_flags;
    gasneti_TM_t real_tm = real_ad->_tm;
    gex_DT_t dt = real_ad->_dt;
    gex_OP_t ops = real_ad->_ops;

    // Check for unsupported ops or dt
    gex_OP_t avail_ops = GASNETE_IBVRATOMIC_BASE_OPS;
    if (gasneti_dt_int(dt)) avail_ops |= GASNETE_IBVRATOMIC_FADD_OPS;
    if (ops & ~avail_ops) goto use_am;
    if (dt  & ~GASNETE_IBVRATOMIC_TYPES) goto use_am;

    // Check for singleton, which is a pain to support for no clear benefit
    if (real_tm->_size == 1) goto use_am;

    // Check for supported cases that should favor AM over NIC
    if (! (flags & GEX_FLAG_AD_FAVOR_REMOTE)) {
        if (flags & (GEX_FLAG_AD_FAVOR_MY_RANK | GEX_FLAG_AD_FAVOR_MY_NBRHD)) {
            // Client's flags favor AM-based atomics
            goto use_am;
        }
    #if GASNET_PSHM
        // TODO-EX: this closed form does not generalize for multi-EP nor TM_Split
        else if (gasneti_mysupernode.node_count == gasneti_nodes) {
            // Single-neighborhood case favors AM-based *if* the datatype is
            // "tools safe" (and thus not actually using AM).  Otherwise, we
            // will assume that the NIC is a better option since it does not
            // rely on target attentiveness.
            switch (dt) {
                case GEX_DT_U64:
                    if (GASNETE_RATOMIC_PSHMSAFE_gex_dt_U64) goto use_am;
                    break;
                case GEX_DT_I64:
                    if (GASNETE_RATOMIC_PSHMSAFE_gex_dt_I64) goto use_am;
                    break;
                default:
                    gasneti_unreachable_error(("unknown data type %d", dt));
            }
        }
    #endif
    }

    // Checks for HCA atomics support
    #define LOG_ONCE(msg) \
      { static int once = 0; \
        if (!once) { once = 1; GASNETI_TRACE_PRINTF(C,("gex_AD_Create: " msg)); } \
      }
    gasnetc_hca_t *hca;
    GASNETC_FOR_ALL_HCA(hca) {
      switch (hca->hca_cap.atomic_cap)
      {
        case IBV_ATOMIC_GLOB:
          // TODO: GLOB is CPU-coherent
          //   Can set _tools_safe and enable AM-based for rest of OPs
        case IBV_ATOMIC_HCA:
          break;

        default:
          LOG_ONCE("HCA atomics support absent or unknown type");
          goto use_am;
      }
    }
    #undef LOG_ONCE

    switch (dt) {
        case GEX_DT_U64:
            real_ad->_fn_tbl = (gasnete_ratomic_fn_tbl_t)&gasnete_ibvratomic_gex_dt_U64_fn_tbl;
            break;
        case GEX_DT_I64:
            real_ad->_fn_tbl = (gasnete_ratomic_fn_tbl_t)&gasnete_ibvratomic_gex_dt_I64_fn_tbl;
            break;
        default:
            gasneti_unreachable_error(("unknown data type %d", dt));
    }

    GASNETI_TRACE_PRINTF(O,("gex_AD_Create(dt=%d, ops=0x%x) -> IBV", (int)dt, (unsigned int)ops));
    real_ad->_tools_safe = 0;
    return;

use_am:
    gasnete_amratomic_init_hook(real_ad);
    return;
}

#endif // GASNETC_BUILD_IBVRATOMIC

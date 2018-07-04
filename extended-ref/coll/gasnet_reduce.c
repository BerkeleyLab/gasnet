/*   $Source: bitbucket.org:berkeleylab/gasnet.git/extended-ref/coll/gasnet_reduce.c $
 * Description: Reference implemetation of GASNet-EX Reductions
 * Copyright (c) 2018 The Regents of the University of California.
 * Terms of use are as specified in license.txt
 */

#include <coll/gasnet_coll_internal.h>

/*---------------------------------------------------------------------------------*/

// TODO-EX: factor the following, which is common to Reduce and Atomics
//
// Macro for applying a 1-argument macro (FN) to each datatype
//
// Since the GEX_DT_* tokens are macros, they cannot safely be used as arguments.
// Instead a family of _gex_dt_* tokens are used, which can be mapped to
// several related tokens via concatenation to generate one of the macros
// which immediately follow.
#define GASNETE_DT_APPLY(FN) \
        FN(_gex_dt_I32) FN(_gex_dt_U32) \
        FN(_gex_dt_I64) FN(_gex_dt_U64) \
        FN(_gex_dt_FLT) FN(_gex_dt_DBL)
//
#define _gex_dt_I32_isint 1
#define _gex_dt_U32_isint 1
#define _gex_dt_I64_isint 1
#define _gex_dt_U64_isint 1
#define _gex_dt_FLT_isint 0
#define _gex_dt_DBL_isint 0
//
#define _gex_dt_I32_type  int32_t
#define _gex_dt_U32_type  uint32_t
#define _gex_dt_I64_type  int64_t
#define _gex_dt_U64_type  uint64_t
#define _gex_dt_FLT_type  float
#define _gex_dt_DBL_type  double
//
#define _gex_dt_I32_dtype GEX_DT_I32
#define _gex_dt_U32_dtype GEX_DT_U32
#define _gex_dt_I64_dtype GEX_DT_I64
#define _gex_dt_U64_dtype GEX_DT_U64
#define _gex_dt_FLT_dtype GEX_DT_FLT
#define _gex_dt_DBL_dtype GEX_DT_DBL

/*---------------------------------------------------------------------------------*/

// Macros for built-in opcodes:
#define GASNETE_REDUCE_OP_ADD(a,b)  (a + b)
#define GASNETE_REDUCE_OP_MULT(a,b) (a * b)
#define GASNETE_REDUCE_OP_AND(a,b)  (a & b)
#define GASNETE_REDUCE_OP_OR(a,b)   (a | b)
#define GASNETE_REDUCE_OP_XOR(a,b)  (a ^ b)
#define GASNETE_REDUCE_OP_MIN(a,b)  MIN(a, b)
#define GASNETE_REDUCE_OP_MAX(a,b)  MAX(a, b)

// GASNETE_REDUCE_OP_APPLY(dtcode,FN)
//
// This macro expands to
//    FN(dtcode,opname)
// repeated for all reduce op valid for dtcode.
// opname is the portion following 'GEX_OP_'.
#define GASNETE_REDUCE_OP_APPLY(dtcode,FN) \
       _GASNETE_REDUCE_OP_APPLY1(dtcode,dtcode##_isint,FN)
// This extra pass expands the "isint" token prior to additional concatenation
#define _GASNETE_REDUCE_OP_APPLY1(dtcode,isint,FN) \
        _GASNETE_REDUCE_OP_APPLY2(dtcode,isint,FN)
#define _GASNETE_REDUCE_OP_APPLY2(dtcode,isint,FN) \
  FN(dtcode,ADD) FN(dtcode,MULT) FN(dtcode,MIN) FN(dtcode,MAX) \
  GASNETE_REDUCE_OP_APPLY_INT##isint(dtcode,FN)
#define GASNETE_REDUCE_OP_APPLY_INT0(dtcode,FN) /*empty*/
#define GASNETE_REDUCE_OP_APPLY_INT1(dtcode,FN) \
  FN(dtcode,AND) FN(dtcode,OR) FN(dtcode,XOR)

/*---------------------------------------------------------------------------------*/
// "Shrink ray" - reduces its targets
//
// TODO-EX: replace this switch-intensive implementation.
// The cringe-worthy name is intended to encourage a short lifetime.

#define GASNETE_SHRINKRAY_CASE(dtcode,opname) \
    case GEX_OP_##opname:                                 \
      for (size_t i = 0; i < count; ++i) {                \
         y[i] = GASNETE_REDUCE_OP_##opname(x[i], y[i]);   \
      }                                                   \
      break;
#define GASNETE_SHRINKRAY_DEFN(dtcode) \
void gasnete_shrinkray##dtcode (                            \
            const void * op1,                               \
            void *       op2_and_out,                       \
            size_t       count,                             \
            const void * cdata)                             \
{                                                           \
  const gex_OP_t opcode = (gex_OP_t)(uintptr_t)cdata;       \
  const dtcode##_type * GASNETI_RESTRICT x = op1;           \
  dtcode##_type * GASNETI_RESTRICT y = op2_and_out;         \
  switch (opcode) {                                         \
    GASNETE_REDUCE_OP_APPLY(dtcode, GASNETE_SHRINKRAY_CASE) \
    default: gasneti_unreachable();                         \
  }                                                         \
}
GASNETE_DT_APPLY(GASNETE_SHRINKRAY_DEFN)
#undef GASNETE_SHRINKRAY_CASE
#undef GASNETE_SHRINKRAY_DEFN

/*---------------------------------------------------------------------------------*/

// GEX Reduce-to-one via Eager messages on a binomial tree
static int gasnete_coll_pf_tm_reduce_BinomialEager(gasnete_coll_op_t *op GASNETI_THREAD_FARG) {
  gex_TM_t const tm = op->e_tm;
  gasnete_coll_generic_data_t *data = op->data;
  const gasnete_tm_reduce_args_t *args = GASNETE_COLL_GENERIC_ARGS(data, tm_reduce);
  gasnete_coll_p2p_t *p2p = data->p2p;
  gex_Flags_t flags = 0;
  void *payload;
  int result = 0;

  // TODO-EX: pre-compute quantities such as these and (dt_sz*dt_cnt) once
  //          at injection, rather than repeatedly upon every poll.
  gex_Rank_t rel_rank = gasnete_tm_binom_rel_root(tm, args->root);
  gex_Rank_t child_cnt = gasnete_tm_binom_children(tm, rel_rank);

  gasneti_assert(p2p != NULL);
  gasneti_assert(p2p->state != NULL);
  gasneti_assert(p2p->data != NULL);
  
  switch (data->state) {
    case 0: {   // Wait for arrival of data from children, if any
      volatile uint32_t *state = p2p->state;
      for (gex_Rank_t r = 0; r < child_cnt; ++r) {
        if (! state[r]) return 0; // At least one child has not contributed their value
      } 
      gasneti_sync_reads();
      data->state = 1; GASNETI_FALLTHROUGH
    }
      
    case 1:
      // Compute reduction (if any)
      if (child_cnt) {
        gex_Coll_ReduceFn_t const op_fnptr = args->op_fnptr;
        void * const op_cdata = args->op_cdata;
        size_t const dt_cnt = args->dt_cnt;
        size_t const nbytes = dt_cnt * args->dt_sz;
        const void *prev = args->src;
        void *curr = p2p->data;
        for (gex_Rank_t r = 0; r < child_cnt; ++r) {
          (*op_fnptr)(prev, curr, dt_cnt, op_cdata);
          prev = curr;
          curr = (void*)(nbytes + (uintptr_t)curr);
        }
        gasneti_assert(prev == gasnete_coll_scale_ptr(p2p->data, child_cnt-1, nbytes));
        payload = (/*non-const*/ void*) prev;
      } else {
        payload = (/*non-const*/ void*) args->src;
      }

      // Data movement, either local or first try to parent
      if (! rel_rank) { // I am root
        const size_t nbytes = args->dt_sz * args->dt_cnt; // TODO-EX: compute *once*
        GASNETI_MEMCPY(args->dst, payload, nbytes);
        goto done;
      }
      flags = GEX_FLAG_IMMEDIATE;
      data->private_data = payload;
      data->state = 2; GASNETI_FALLTHROUGH

    case 2: {   // Data movement to parent (IMM on first try only)
      const size_t nbytes = args->dt_sz * args->dt_cnt;
      gex_Rank_t parent = gasnete_tm_binom_parent(tm, rel_rank);
      gex_Rank_t offset = gasnete_tm_binom_age(tm, rel_rank);
      payload = data->private_data;
      // TODO-EX: use lc_opt for async injection
      if (gasnete_tm_p2p_eager_put(op, tm, parent, payload, nbytes,
                                   GEX_EVENT_NOW, flags, offset, 1
                                   GASNETI_THREAD_PASS)) {
        break; // back pressure
      }

    done:
      // Done
      gasnete_coll_generic_free(op->team, data GASNETI_THREAD_PASS);
      result = (GASNETE_COLL_OP_COMPLETE | GASNETE_COLL_OP_INACTIVE);
    }
  }
  
  return result;
}

GASNETE_TM_DECLARE_REDUCE_ALG(BinomialEager)
{
#if GASNET_DEBUG // make sure this is a valid choice of algorithm
  gex_Rank_t rel_rank = gasnete_tm_binom_rel_root(tm, root);
  gex_Rank_t child_cnt = gasnete_tm_binom_children(tm, rel_rank);
  gasneti_assert(gasnete_coll_p2p_eager_buffersz >= dt_sz * dt_cnt * child_cnt);
  gasneti_assert(gex_AM_LUBRequestMedium() >= dt_sz * dt_cnt );
#endif

  const int options = GASNETE_COLL_GENERIC_OPT_P2P_IF(1);
  return gasnete_tm_generic_reduce_nb(tm, root, dst, src, dt, dt_sz, dt_cnt,
                                      op, op_fnptr, op_cdata, coll_flags,
                                      &gasnete_coll_pf_tm_reduce_BinomialEager,
                                      options, NULL, 0, 0, NULL, NULL
                                      GASNETI_THREAD_PASS);
}

/*   $Source: bitbucket.org:berkeleylab/gasnet.git/extended-ref/coll/gasnet_reduce.c $
 * Description: Reference implemetation of GASNet-EX Reductions
 * Copyright (c) 2018 The Regents of the University of California.
 * Terms of use are as specified in license.txt
 */

#include <gasnet_internal.h>
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

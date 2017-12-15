/*   $Source: bitbucket.org:berkeleylab/gasnet.git/extended-ref/gasnet_ratomic.h $
 * Description: GASNet Remote Atomics API Header
 * Copyright 2017, The Regents of the University of California
 * Terms of use are as specified in license.txt
 */

#ifndef _GASNET_RATOMIC_H
#define _GASNET_RATOMIC_H
#define _IN_GASNET_RATOMIC_H

#include <gasnetex.h>

GASNETI_BEGIN_EXTERNC
GASNETI_BEGIN_NOWARN

// gex_AD_t is an opaque scalar handle
struct gasneti_ad_t;
typedef struct gasneti_ad_t *gex_AD_t;

#ifndef _GEX_AD_T
  #define GASNETI_AD_COMMON \
    GASNETI_OBJECT_HEADER              \
    gasneti_TM_t       _tm;            \
    gex_Rank_t         _rank;          \
    gex_DT_t           _dt;            \
    gex_OP_t           _ops;
  typedef struct { GASNETI_AD_COMMON } *gasneti_AD_t;
  #if GASNET_DEBUG
    extern gasneti_AD_t gasneti_import_ad(gex_AD_t _ad);
    extern gex_AD_t gasneti_export_ad(gasneti_AD_t _real_ad);
  #else
    #define gasneti_import_ad(x) ((gasneti_AD_t)(x))
    #define gasneti_export_ad(x) ((gex_AD_t)(x))
  #endif
  #define gex_AD_SetCData(ad,val)  ((void)(gasneti_import_ad(ad)->_cdata = (val)))
  #define gex_AD_QueryCData(ad)    ((void*)gasneti_import_ad(ad)->_cdata)
  #define gex_AD_QueryTM(ad)       gasneti_export_tm(gasneti_import_ad(ad)->_tm)
  #define gex_AD_QueryOps(ad)      ((gex_OP_t)gasneti_import_ad(ad)->_ops)
  #define gex_AD_QueryDT(ad)       ((gex_DT_t)gasneti_import_ad(ad)->_dt)
  #define gex_AD_QueryFlags(ad)    ((gex_Flags_t)gasneti_import_ad(ad)->_flags)
#endif

// Collective creation of an atomic domain - default implementation
#ifndef gex_AD_Create
  extern void gasneti_AD_Create(
        gex_AD_t                   *_ad_p,            // Output
        gex_TM_t                   _tm,               // The team
        gex_DT_t                   _dt,               // The data type
        gex_OP_t                   _ops,              // OR of operations to be supported
        gex_Flags_t                _flags             // flags
        );
  #define gex_AD_Create gasneti_AD_Create
#endif

// Collective destruction of an atomic domain - default implementation
#ifndef gex_AD_Destroy
  extern void gasneti_AD_Destroy(gex_AD_t _ad);
  #define gex_AD_Destroy gasneti_AD_Destroy
#endif


/*---------------------------------------------------------------------------------*/
//
// Common logic for RAtomic implementatons
//

#ifndef _GEX_AD_T
  #if GASNET_DEBUG
    extern void gasnete_ratomic_validate(
        gex_AD_t            _ad,        void             *_result_p,
        gex_Rank_t          _tgt_rank,  void             *_tgt_addr,
        gex_OP_t            _opcode,    gex_DT_t         _datatype,
        gex_Flags_t         _flags);
  #else
    #define gasnete_ratomic_validate(ad,result_p,rank,addr,op,dt,flags) ((void)0)
  #endif
#endif


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
#define _gex_dt_I32_bits  32
#define _gex_dt_U32_bits  32
#define _gex_dt_I64_bits  64
#define _gex_dt_U64_bits  64
#define _gex_dt_FLT_bits  32
#define _gex_dt_DBL_bits  64
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


//
// Macros for building local atomic RMW operations using (strong) atomics
// TODO: generalize to weak atomic too?
//
// GASNETE_RATOMIC_CAS(output, tgt, dtcode, op1, operator)
//   Use appropriate fixed-width CAS to apply an operator to the given data type
//
// Ex1:
//   GASNETE_RATOMIC_CAS(x, tgt, _gex_dt_I32, op1, GASNETE_RATOMIC_CAS_OP_MULT)
//   Atomically performs { x = *tgt; *tgt = *tgt * op1; } on values of type int32_t
//
// Ex2:
//   GASNETE_RATOMIC_CAS(x, tgt, _gex_dt_DBL, op1, GASNETE_RATOMIC_CAS_OP_MAX)
//   Atomically performs { x = *tgt; *tgt = MAX(*tgt, op1); } on values of type double
//
// Care is taken to evaluate 'output', 'tgt' and 'op1' exactly once.
// All operations are "fetching" (writing to an output variable).
// INC (DEC) are available indirectly using ADD (SUB) with an 'op1' of 1.
//
// Operators:
#define GASNETE_RATOMIC_CAS_OP_ADD(a,b)  (a + b)
#define GASNETE_RATOMIC_CAS_OP_SUB(a,b)  (a - b)
#define GASNETE_RATOMIC_CAS_OP_MULT(a,b) (a * b)
#define GASNETE_RATOMIC_CAS_OP_AND(a,b)  (a & b)
#define GASNETE_RATOMIC_CAS_OP_OR(a,b)   (a | b)
#define GASNETE_RATOMIC_CAS_OP_XOR(a,b)  (a ^ b)
#define GASNETE_RATOMIC_CAS_OP_MIN(a,b)  MIN(a, b)
#define GASNETE_RATOMIC_CAS_OP_MAX(a,b)  MAX(a, b)
//
#define GASNETE_RATOMIC_CAS(output, tgt, dtcode, op1, operator) \
        _GASNETE_RATOMIC_CAS1(output, tgt, dtcode##_type, dtcode##_bits, op1, dtcode##_isint, operator)
// This extra pass expands the "bits" and "isint" tokens prior to additional concatenation
#define _GASNETE_RATOMIC_CAS1(output, tgt, type, bits, op1, isint, operator) \
        _GASNETE_RATOMIC_CAS2(output, tgt, type, bits, op1, isint, operator)
#define _GASNETE_RATOMIC_CAS2(output, tgt, type, bits, op1, isint, operator) \
        _GASNETE_RATOMIC_CAS3##isint(output, tgt, type, bits, op1, operator)
#define _GASNETE_RATOMIC_CAS31(output, tgt, type, bits, op1, operator) do { /* integer DTs */ \
        type const _op1 = (op1);                                                     \
        gasneti_atomic##bits##_t *_tgt = (gasneti_atomic##bits##_t*)(tgt);           \
        type _newval, _oldval;                                                       \
        do {                                                                         \
            _oldval = gasneti_atomic##bits##_read(_tgt, 0);                          \
            _newval = operator(_oldval, _op1);                                       \
        } while (! gasneti_atomic##bits##_compare_and_swap(_tgt,_oldval,_newval,0)); \
        (output) = _oldval;                                                          \
    } while (0);
#define _GASNETE_RATOMIC_CAS30(output, tgt, type, bits, op1, operator) do { /* FP DTs */ \
        type const _op1 = (op1);                                                     \
        gasneti_atomic##bits##_t *_tgt = (gasneti_atomic##bits##_t*)(tgt);           \
        union { type _fp; uint##bits##_t _ui; } _newval, _oldval;                    \
        do {                                                                         \
            _oldval._ui = gasneti_atomic##bits##_read(_tgt, 0);                      \
            _newval._fp = operator(_oldval._fp, _op1);                               \
        } while (! gasneti_atomic##bits##_compare_and_swap(_tgt, _oldval._ui, _newval._ui, 0)); \
        (output) = _oldval._fp;                                                      \
    } while (0);

//
// Define inline functions to perform local atomic operations
// Always fetching (w/ only SET having an unspecified value)
//
// TODO: it may be possible to further factor to reduce duplication between
// integer and floating-point ("SW1" and "SW0, respectively).
// Attempts so far have either been fragile, unmaintainable or made excessive
// use of type-punning and/or temporaries (which could harm optimization).
// Issues obstructing further factorization include:
//  + Desire to use gasneti_atomicNN_{add,sub}() with integer types (not CAS)
//  + Desire to apply union-based type-punning *only* with floating-point types
//
#define GASNETE_RATOMIC_FN_DEFN(dtcode) \
        _GASNETE_RATOMIC_FN_DEFN1(gasnete_ratomicfn##dtcode, dtcode, \
                                  dtcode##_type, dtcode##_bits, dtcode##_isint)
// This extra pass expands the "bits" and "isint" tokens prior to additional concatenation
#define _GASNETE_RATOMIC_FN_DEFN1(fname, dtcode, type, bits, isint) \
        _GASNETE_RATOMIC_FN_DEFN2(fname, dtcode, type, bits, isint)
#define _GASNETE_RATOMIC_FN_DEFN2(fname, dtcode, type, bits, isint) \
  GASNETI_INLINE(fname)                                                         \
  type fname(type *_tgt_addr, type _operand1, type _operand2, gex_OP_t _opcode) \
  { type _result;                                                               \
    gasneti_atomic##bits##_t *_ratgt = (gasneti_atomic##bits##_t *)(_tgt_addr); \
    typedef union { type _ratype; uint##bits##_t _raui; } _raunion;             \
    switch(_opcode) {                                                           \
      _GASNETE_RATOMIC_FN_SW##isint(dtcode, type, bits);                        \
      _GASNETE_RATOMIC_FN_CAS_CASE(dtcode, MULT);                               \
      _GASNETE_RATOMIC_FN_CAS_CASE(dtcode, MIN);                                \
      _GASNETE_RATOMIC_FN_CAS_CASE(dtcode, MAX);                                \
      default: gasneti_unreachable();                                           \
    }                                                                           \
    return _result;                                                             \
  }
// Big SWitch cases for integer types (isint == 1)
#define _GASNETE_RATOMIC_FN_SW1(dtcode,type,bits) \
      case GEX_OP_SET:                                                           \
        gasneti_atomic##bits##_set(_ratgt, _operand1, 0);                        \
        _result = 0; /* Sigh. Just to silence warnings */                        \
        break;                                                                   \
      case GEX_OP_GET:                                                           \
        _result = gasneti_atomic##bits##_read(_ratgt, 0);                        \
        break;                                                                   \
      case GEX_OP_SWAP:                                                          \
        _result = gasneti_atomic##bits##_swap(_ratgt, _operand1, 0);             \
        break;                                                                   \
      case GEX_OP_CSWAP: {                                                       \
        type _ratmp = _operand1;                                                 \
        do {                                                                     \
          if (gasneti_atomic##bits##_compare_and_swap(_ratgt, _ratmp, _operand2, 0)) { \
            break;                                                               \
          }                                                                      \
          _ratmp = gasneti_atomic##bits##_read(_ratgt, 0);                       \
        } while (_ratmp == _operand1);                                           \
        _result = _ratmp;                                                        \
        break;                                                                   \
      }                                                                          \
      case GEX_OP_INC: case GEX_OP_FINC:                                         \
        _operand1 = 1; /* fall through to ADD ... */                             \
      case GEX_OP_ADD: case GEX_OP_FADD:                                         \
        _result = gasneti_atomic##bits##_add(_ratgt, _operand1, 0) - _operand1;  \
        break;                                                                   \
      case GEX_OP_DEC: case GEX_OP_FDEC:                                         \
         _operand1 = 1; /* fall through to SUB ... */                            \
      case GEX_OP_SUB: case GEX_OP_FSUB:                                         \
        _result = gasneti_atomic##bits##_subtract(_ratgt, _operand1, 0) + _operand1; \
        break;                                                                   \
      _GASNETE_RATOMIC_FN_CAS_CASE(dtcode, AND);                                 \
      _GASNETE_RATOMIC_FN_CAS_CASE(dtcode, OR);                                  \
      _GASNETE_RATOMIC_FN_CAS_CASE(dtcode, XOR);
// Big SWitch cases for floating-point types (isint == 0)
#define _GASNETE_RATOMIC_FN_SW0(dtcode,type,bits) \
      case GEX_OP_SET: {                                                         \
        _raunion _ratmp; _ratmp._ratype = _operand1;                             \
        gasneti_atomic##bits##_set(_ratgt, _ratmp._raui, 0);                     \
        _result = 0; /* Sigh. Just to silence warnings */                        \
        break;                                                                   \
      }                                                                          \
      case GEX_OP_GET: {                                                         \
        _raunion _ratmp;                                                         \
        _ratmp._raui = gasneti_atomic##bits##_read(_ratgt, 0);                   \
        _result = _ratmp._ratype;                                                \
        break;                                                                   \
      }                                                                          \
      case GEX_OP_SWAP: {                                                        \
        _raunion _ratmp; _ratmp._ratype = _operand1;                             \
        _ratmp._raui = gasneti_atomic##bits##_swap(_ratgt, _ratmp._raui, 0);     \
        _result = _ratmp._ratype;                                                \
        break;                                                                   \
      }                                                                          \
      case GEX_OP_CSWAP: {                                                       \
        _raunion _raold; _raold._ratype = _operand1;                             \
        _raunion _ranew; _ranew._ratype = _operand2;                             \
        do {                                                                     \
          if (gasneti_atomic##bits##_compare_and_swap(_ratgt, _raold._raui, _ranew._raui, 0)) { \
            break;                                                               \
          }                                                                      \
          _raold._raui = gasneti_atomic##bits##_read(_ratgt, 0);                 \
        } while (_raold._ratype == _operand1);                                   \
        _result = _raold._ratype;                                                \
        break;                                                                   \
      }                                                                          \
      case GEX_OP_INC: case GEX_OP_FINC:                                         \
         _operand1 = 1.; /* fall through to ADD ... */                           \
      _GASNETE_RATOMIC_FN_CAS_CASE(dtcode, ADD);                                 \
      case GEX_OP_DEC: case GEX_OP_FDEC:                                         \
         _operand1 = 1.; /* fall through to SUB ... */                           \
      _GASNETE_RATOMIC_FN_CAS_CASE(dtcode, SUB);
// Case for a CAS-based operation (both fetching and non-fetching)
#define _GASNETE_RATOMIC_FN_CAS_CASE(dtcode, opname) \
      case GEX_OP_##opname: case GEX_OP_F##opname: \
        GASNETE_RATOMIC_CAS(_result,_ratgt,dtcode,_operand1,GASNETE_RATOMIC_CAS_OP_##opname); \
        break;
//
GASNETE_DT_APPLY(GASNETE_RATOMIC_FN_DEFN)

//
// Macro expanding to declaration of a full family of "back-end" functions
// which together constitute one implementation of remote atomics.
//
// Example usage for a "fooratomic" implementation:
//    #define GASNETE_FOORATOMIC_DECL(dtcode) GASNETE_RATOMIC_DECL(gasnete_fooratomic, dtcode)
//    GASNETE_DT_APPLY(GASNETE_FOORATOMIC_DECL)
//
// Interface is intended to eliminate all opcode-dependent branches, but
// to still be significantly narrower than one function per opcode.
//
// gasnete_fooratomic_{typecode}_{NB,NBI}_{N,F,S,G}{0,1,2}
//         N  Non-fetching operation (excluding Set)
//         F  Fetching operation (excluding Get)
//         S  Set (only S1)
//         G  Get (only G0)
//   {0,1,2}  Operand count
// For NBI one wants S and G due to their differing Event Categories.
// However, they are also included for NB under the belief that we
// will eventually want to map these to Put/Get for some combinations
// of data type and network.
//
// Currently there is no "N2" case, though "AX" would belong there.
//
#define GASNETE_RATOMIC_DECL(prefix, dtcode) \
        _GASNETE_RATOMIC_DECL(prefix##dtcode, dtcode##_type)
#define _GASNETE_RATOMIC_DECL(prefix, type) \
    extern gex_Event_t prefix##_NB_N0(gasneti_AD_t, gex_Rank_t, void*,                    \
                                      gasneti_op_idx_t, gex_Flags_t GASNETI_THREAD_FARG); \
    extern gex_Event_t prefix##_NB_N1(gasneti_AD_t, gex_Rank_t, void*, type,              \
                                      gasneti_op_idx_t, gex_Flags_t GASNETI_THREAD_FARG); \
    extern gex_Event_t prefix##_NB_F0(gasneti_AD_t, type*, gex_Rank_t, void*,             \
                                      gasneti_op_idx_t, gex_Flags_t GASNETI_THREAD_FARG); \
    extern gex_Event_t prefix##_NB_F1(gasneti_AD_t, type*, gex_Rank_t, void*, type,       \
                                      gasneti_op_idx_t, gex_Flags_t GASNETI_THREAD_FARG); \
    extern gex_Event_t prefix##_NB_F2(gasneti_AD_t, type*, gex_Rank_t, void*, type, type, \
                                      gasneti_op_idx_t, gex_Flags_t GASNETI_THREAD_FARG); \
    extern gex_Event_t prefix##_NB_S1(gasneti_AD_t, gex_Rank_t, void*, type,              \
                                      gex_Flags_t GASNETI_THREAD_FARG);                   \
    extern gex_Event_t prefix##_NB_G0(gasneti_AD_t, type*, gex_Rank_t, void*,             \
                                      gex_Flags_t GASNETI_THREAD_FARG);                   \
    extern int        prefix##_NBI_N0(gasneti_AD_t, gex_Rank_t, void*,                    \
                                      gasneti_op_idx_t, gex_Flags_t GASNETI_THREAD_FARG); \
    extern int        prefix##_NBI_N1(gasneti_AD_t, gex_Rank_t, void*, type,              \
                                      gasneti_op_idx_t, gex_Flags_t GASNETI_THREAD_FARG); \
    extern int        prefix##_NBI_F0(gasneti_AD_t, type*, gex_Rank_t, void*,             \
                                      gasneti_op_idx_t, gex_Flags_t GASNETI_THREAD_FARG); \
    extern int        prefix##_NBI_F1(gasneti_AD_t, type*, gex_Rank_t, void*, type,       \
                                      gasneti_op_idx_t, gex_Flags_t GASNETI_THREAD_FARG); \
    extern int        prefix##_NBI_F2(gasneti_AD_t, type*, gex_Rank_t, void*, type, type, \
                                      gasneti_op_idx_t, gex_Flags_t GASNETI_THREAD_FARG); \
    extern int        prefix##_NBI_S1(gasneti_AD_t, gex_Rank_t, void*, type,              \
                                      gex_Flags_t GASNETI_THREAD_FARG);                   \
    extern int        prefix##_NBI_G0(gasneti_AD_t, type*, gex_Rank_t, void*,             \
                                      gex_Flags_t GASNETI_THREAD_FARG);

//
// Macro expanding to definition of a full family of "dispatch" functions
// which together constitute one implementation of remote atomics.
//
// Example usage for a "fooratomic" implementation:
//    #define GASNETE_FOORATOMIC_DISP(dtcode) GASNETE_RATOMIC_DISP(gasnete_fooratomic, dtcode, cpusafe)
//    GASNETE_DT_APPLY(GASNETE_FOORATOMIC_DISP)
// Where the 'cpusafe' argument is a literal 1 to enable "loopback" calls to
// use the gasnete_ratomicfn##dtcode() functions, or literal 0 to disable.
// Alternatively cpusafe might be an expression on "_real_ad" if useful.
//
// TODO: should honor is-self flag once it is defined
// TODO: need local memory fences once flags are defined
// TODO: need trace/stats at this layer or one higher
// TODO: The "cpusafe" logic could use gasneti_op_fetch() if it were
//       to be defined in a client-facing header.
//
// GASNETE_RATOMIC_DISP(prefix, dtcode, cpusafe):
//   Expands to definitions of two inline functions:
//       inline gex_Event_t prefix##dtcode##_NB() { ... }
//       inline int         prefix##dtcode##_NBI() { ... }
//     and extern declarations two functions of identical functionality:
//       extern gex_Event_t prefix##dtcode##_NB_external();
//       extern int         prefix##dtcode##_NBI_external();
//     Each has the signature of a gex_AD_* op-initiation call for 'type'.
//
#define GASNETE_RATOMIC_DISP(prefix, dtcode, cpusafe) \
        _GASNETE_RATOMIC_DISP1(prefix##dtcode, dtcode##_isint, dtcode##_type, dtcode, cpusafe)
// This extra pass expands the "isint" token prior to additional concatenation
#define _GASNETE_RATOMIC_DISP1(prefix, isint, type, dtcode, cpusafe) \
        _GASNETE_RATOMIC_DISP2(prefix##_NB, isint, type, dtcode, cpusafe, gex_Event_t, GEX_EVENT_INVALID) \
        _GASNETE_RATOMIC_DISP2(prefix##_NBI, isint, type, dtcode, cpusafe, int, 0)
#define _GASNETE_RATOMIC_DISP2(fname,isint,type,dtcode,cpusafe,rettype,retdone) \
    extern rettype fname##_external _GASNETE_RATOMIC_DISP_ARGS(type);    \
    GASNETI_ALWAYS_INLINE(fname)                                         \
    rettype fname _GASNETE_RATOMIC_DISP_ARGS(type)                       \
    {                                                                    \
        gasnete_ratomic_validate(_ad,_result_p,_tgt_rank,_tgt_addr,      \
                                 _opcode, dtcode##_dtype, _flags);       \
        gasneti_AD_t _real_ad = gasneti_import_ad(_ad);                  \
        if (cpusafe && (_tgt_rank == _real_ad->_rank)) {                 \
            type _result = gasnete_ratomicfn##dtcode((type *)_tgt_addr,  \
                                                     _operand1,          \
                                                     _operand2,          \
                                                     _opcode);           \
            if (_opcode & (GEX_OP_FADD | GEX_OP_FSUB | GEX_OP_FMULT |    \
                           GEX_OP_FMIN | GEX_OP_FMAX |                   \
                           GEX_OP_FINC | GEX_OP_FDEC |                   \
                           GEX_OP_FAND | GEX_OP_FOR  | GEX_OP_FXOR |     \
                           GEX_OP_GET  | GEX_OP_SWAP | GEX_OP_CSWAP)) {  \
                *_result_p = _result;                                    \
            }                                                            \
            return retdone;                                              \
        }                                                                \
        switch(_opcode) {                                                \
            _GASNETE_RATOMIC_DISP_CASE(fname, GET,   G0)                 \
            _GASNETE_RATOMIC_DISP_CASE(fname, SET,   S1)                 \
            _GASNETE_RATOMIC_DISP_CASE(fname, SWAP,  F1)                 \
            _GASNETE_RATOMIC_DISP_CASE(fname, CSWAP, F2)                 \
            _GASNETE_RATOMIC_DISP_CASE(fname, ADD,   N1)                 \
            _GASNETE_RATOMIC_DISP_CASE(fname, FADD,  F1)                 \
            _GASNETE_RATOMIC_DISP_CASE(fname, SUB,   N1)                 \
            _GASNETE_RATOMIC_DISP_CASE(fname, FSUB,  F1)                 \
            _GASNETE_RATOMIC_DISP_CASE(fname, MULT,  N1)                 \
            _GASNETE_RATOMIC_DISP_CASE(fname, FMULT, F1)                 \
            _GASNETE_RATOMIC_DISP_CASE(fname, MIN,   N1)                 \
            _GASNETE_RATOMIC_DISP_CASE(fname, FMIN,  F1)                 \
            _GASNETE_RATOMIC_DISP_CASE(fname, MAX,   N1)                 \
            _GASNETE_RATOMIC_DISP_CASE(fname, FMAX,  F1)                 \
            _GASNETE_RATOMIC_DISP_CASE(fname, INC,   N0)                 \
            _GASNETE_RATOMIC_DISP_CASE(fname, FINC,  F0)                 \
            _GASNETE_RATOMIC_DISP_CASE(fname, DEC,   N0)                 \
            _GASNETE_RATOMIC_DISP_CASE(fname, FDEC,  F0)                 \
            _GASNETE_RATOMIC_DISP_INT##isint(fname, type)                \
            default: gasneti_unreachable();                              \
        }                                                                \
        gasneti_unreachable();                                           \
        return retdone;                                                  \
    }
#define _GASNETE_RATOMIC_DISP_ARGS(type) \
        (gex_AD_t            _ad,        type             *_result_p,    \
         gex_Rank_t          _tgt_rank,  void             *_tgt_addr,    \
         gex_OP_t            _opcode,    type             _operand1,     \
         type                _operand2,  gex_Flags_t      _flags         \
         GASNETI_THREAD_FARG)
#define _GASNETE_RATOMIC_DISP_INT0(prefix, type) /*empty*/
#define _GASNETE_RATOMIC_DISP_INT1(prefix, type) /*bitwise ops*/ \
            _GASNETE_RATOMIC_DISP_CASE(prefix, AND,   N1) \
            _GASNETE_RATOMIC_DISP_CASE(prefix, FAND,  F1) \
            _GASNETE_RATOMIC_DISP_CASE(prefix, OR,    N1) \
            _GASNETE_RATOMIC_DISP_CASE(prefix, FOR,   F1) \
            _GASNETE_RATOMIC_DISP_CASE(prefix, XOR,   N1) \
            _GASNETE_RATOMIC_DISP_CASE(prefix, FXOR,  F1)
#define _GASNETE_RATOMIC_DISP_CASE(prefix,opname,args) \
            case GEX_OP_##opname: \
                return prefix##_##args _GASNETE_RATOMIC_DISP_ARGS_##args(gasneti_op_idx_##opname);
#define _GASNETE_RATOMIC_DISP_ARGS_N0(opidx) \
        (_real_ad,         _tgt_rank,_tgt_addr,                     opidx,_flags GASNETI_THREAD_PASS)
#define _GASNETE_RATOMIC_DISP_ARGS_N1(opidx) \
        (_real_ad,         _tgt_rank,_tgt_addr,_operand1,           opidx,_flags GASNETI_THREAD_PASS)
#define _GASNETE_RATOMIC_DISP_ARGS_F0(opidx) \
        (_real_ad,_result_p,_tgt_rank,_tgt_addr,                    opidx,_flags GASNETI_THREAD_PASS)
#define _GASNETE_RATOMIC_DISP_ARGS_F1(opidx) \
        (_real_ad,_result_p,_tgt_rank,_tgt_addr,_operand1,          opidx,_flags GASNETI_THREAD_PASS)
#define _GASNETE_RATOMIC_DISP_ARGS_F2(opidx) \
        (_real_ad,_result_p,_tgt_rank,_tgt_addr,_operand1,_operand2,opidx,_flags GASNETI_THREAD_PASS)
#define _GASNETE_RATOMIC_DISP_ARGS_G0(opidx) \
        (_real_ad,_result_p,_tgt_rank,_tgt_addr,                          _flags GASNETI_THREAD_PASS)
#define _GASNETE_RATOMIC_DISP_ARGS_S1(opidx) \
        (_real_ad,          _tgt_rank,_tgt_addr,_operand1,                _flags GASNETI_THREAD_PASS)

/*---------------------------------------------------------------------------------*/
//
// AM-based implementation
// Built unless GASNETE_BUILD_AMRATOMIC is defined to 0
//
// TODO: per-datatype control over what gets built
//
#ifndef GASNETE_BUILD_AMRATOMIC
  #define GASNETE_BUILD_AMRATOMIC 1
#endif

#if GASNETE_BUILD_AMRATOMIC

// AM-based implementation
// Part I.  Declare functions to send AM w/ NB or NBI completion upon reply.
//
#define GASNETE_AMRATOMIC_DECL(dtcode) GASNETE_RATOMIC_DECL(gasnete_amratomic, dtcode)
GASNETE_DT_APPLY(GASNETE_AMRATOMIC_DECL)

// AM-based implementation
// Part II.  Define inline functions dispatching to the back-end functions
//
#define GASNETE_AMRATOMIC_DISP(dtcode) GASNETE_RATOMIC_DISP(gasnete_amratomic, dtcode, 1)
GASNETE_DT_APPLY(GASNETE_AMRATOMIC_DISP)

// AM-based implementation
// Part III.  #define public API *directly* to AM-based one (unless already defined).
// No array of function pointers is needed when there is only 1 implementation.
//
// We take some care not to inline a big switch if opcode is non-constant.
// We require NB and NBI are overridden together (both or neither).
//
#define GASNETE_AMRATOMIC_FN(stem,ad,result_p,rank,addr,opcode,op1,op2,flags) \
    (gasneti_constant_p(opcode) \
     ? gasnete_amratomic_##stem(ad,result_p,rank,addr,opcode,op1,op2,flags GASNETI_THREAD_GET) \
     : gasnete_amratomic_##stem##_external(ad,result_p,rank,addr,opcode,op1,op2,flags GASNETI_THREAD_GET))
//
#if !defined(gex_AD_OpNB_I32) || !defined(gex_AD_OpNBI_I32)
  #if defined(gex_AD_OpNB_I32) || defined(gex_AD_OpNBI_I32)
    #error "Conduit must override gex_AD_OpNB_I32 and gex_AD_OpNBI_I32_NBI together"
  #endif
  #define gex_AD_OpNB_I32(ad,result_p,rank,addr,opcode,op1,op2,flags) \
          GASNETE_AMRATOMIC_FN(gex_dt_I32_NB,ad,result_p,rank,addr,opcode,op1,op2,flags)
  #define gex_AD_OpNBI_I32(ad,result_p,rank,addr,opcode,op1,op2,flags) \
          GASNETE_AMRATOMIC_FN(gex_dt_I32_NBI,ad,result_p,rank,addr,opcode,op1,op2,flags)
#endif
#if !defined(gex_AD_OpNB_U32) || !defined(gex_AD_OpNBI_U32)
  #if defined(gex_AD_OpNB_U32) || defined(gex_AD_OpNBI_U32)
    #error "Conduit must override gex_AD_OpNB_U32 and gex_AD_OpNBI_U32_NBI together"
  #endif
  #define gex_AD_OpNB_U32(ad,result_p,rank,addr,opcode,op1,op2,flags) \
          GASNETE_AMRATOMIC_FN(gex_dt_U32_NB,ad,result_p,rank,addr,opcode,op1,op2,flags)
  #define gex_AD_OpNBI_U32(ad,result_p,rank,addr,opcode,op1,op2,flags) \
          GASNETE_AMRATOMIC_FN(gex_dt_U32_NBI,ad,result_p,rank,addr,opcode,op1,op2,flags)
#endif
#if !defined(gex_AD_OpNB_I64) || !defined(gex_AD_OpNBI_I64)
  #if defined(gex_AD_OpNB_I64) || defined(gex_AD_OpNBI_I64)
    #error "Conduit must override gex_AD_OpNB_I64_NB and gex_AD_OpNBI_I64_NBI together"
  #endif
  #define gex_AD_OpNB_I64(ad,result_p,rank,addr,opcode,op1,op2,flags) \
          GASNETE_AMRATOMIC_FN(gex_dt_I64_NB,ad,result_p,rank,addr,opcode,op1,op2,flags)
  #define gex_AD_OpNBI_I64(ad,result_p,rank,addr,opcode,op1,op2,flags) \
          GASNETE_AMRATOMIC_FN(gex_dt_I64_NBI,ad,result_p,rank,addr,opcode,op1,op2,flags)
#endif
#if !defined(gex_AD_OpNB_U64) || !defined(gex_AD_OpNBI_U64)
  #if defined(gex_AD_OpNB_U64) || defined(gex_AD_OpNBI_U64)
    #error "Conduit must override gex_AD_OpNB_U64_NB and gex_AD_OpNBI_U64_NBI together"
  #endif
  #define gex_AD_OpNB_U64(ad,result_p,rank,addr,opcode,op1,op2,flags) \
          GASNETE_AMRATOMIC_FN(gex_dt_U64_NB,ad,result_p,rank,addr,opcode,op1,op2,flags)
  #define gex_AD_OpNBI_U64(ad,result_p,rank,addr,opcode,op1,op2,flags) \
          GASNETE_AMRATOMIC_FN(gex_dt_U64_NBI,ad,result_p,rank,addr,opcode,op1,op2,flags)
#endif
#if !defined(gex_AD_OpNB_FLT) || !defined(gex_AD_OpNBI_FLT)
  #if defined(gex_AD_OpNB_FLT) || defined(gex_AD_OpNBI_FLT)
    #error "Conduit must override gex_AD_OpNB_FLT_NB and gex_AD_OpNBI_FLT_NBI together"
  #endif
  #define gex_AD_OpNB_FLT(ad,result_p,rank,addr,opcode,op1,op2,flags) \
          GASNETE_AMRATOMIC_FN(gex_dt_FLT_NB,ad,result_p,rank,addr,opcode,op1,op2,flags)
  #define gex_AD_OpNBI_FLT(ad,result_p,rank,addr,opcode,op1,op2,flags) \
          GASNETE_AMRATOMIC_FN(gex_dt_FLT_NBI,ad,result_p,rank,addr,opcode,op1,op2,flags)
#endif
#if !defined(gex_AD_OpNB_DBL) || !defined(gex_AD_OpNBI_DBL)
  #if defined(gex_AD_OpNB_DBL) || defined(gex_AD_OpNBI_DBL)
    #error "Conduit must override gex_AD_OpNB_DBL_NB and gex_AD_OpNBI_DBL_NBI together"
  #endif
  #define gex_AD_OpNB_DBL(ad,result_p,rank,addr,opcode,op1,op2,flags) \
          GASNETE_AMRATOMIC_FN(gex_dt_DBL_NB,ad,result_p,rank,addr,opcode,op1,op2,flags)
  #define gex_AD_OpNBI_DBL(ad,result_p,rank,addr,opcode,op1,op2,flags) \
          GASNETE_AMRATOMIC_FN(gex_dt_DBL_NBI,ad,result_p,rank,addr,opcode,op1,op2,flags)
#endif

#endif // GASNETE_BUILD_AMRATOMIC

/*---------------------------------------------------------------------------------*/
//
// Hook for conduit-specific additions
//
// To use, #define GASNETE_HAVE_RATOMIC_EXTRA_H in the conduit's
// gasnet_extended_fwd.h or gasnet_ratomic_fwd.h.
//

#ifdef GASNETE_HAVE_RATOMIC_EXTRA_H
  #include <gasnet_ratomic_extra.h>
#endif

/*---------------------------------------------------------------------------------*/

GASNETI_END_NOWARN
GASNETI_END_EXTERNC

#undef _IN_GASNET_RATOMIC_H
#endif

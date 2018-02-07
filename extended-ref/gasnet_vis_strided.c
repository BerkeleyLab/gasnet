/*   $Source: bitbucket.org:berkeleylab/gasnet.git/extended-ref/gasnet_vis_strided.c $
 * Description: GASNet Strided implementation
 * Copyright 2002, Dan Bonachea <bonachea@cs.berkeley.edu>
 * Terms of use are as specified in license.txt
 */

#ifndef GASNETI_GASNET_EXTENDED_REFVIS_C
  #error This file not meant to be compiled directly - included by gasnet_extended_refvis.c
#endif

#if !defined(GASNETE_OLD_STRIDED) || GASNETE_OLD_STRIDED
#error Internal error: wrong GASNETE_OLD_STRIDED defn
#endif

/*---------------------------------------------------------------------------------*/
/* *** GASNet-EX Strided Implementation *** */
/*---------------------------------------------------------------------------------*/

/* Clang can be picky */
#if PLATFORM_COMPILER_CLANG && PLATFORM_COMPILER_VERSION_GE(2,8,0)
  #pragma clang diagnostic push
  #pragma clang diagnostic ignored "-Wconstant-logical-operand"
#endif

/* helper macros */
/* increment the values in init[0..(stridelevels-1)] by incval chunks, 
   using provided count[0..(stridelevels-1)] dimensional extents.
   incval uses the same units as count[0] (ie elements, not bytes), 
   and carries are propagated in ascending order through the dimensions of init
*/
#define GASNETE_STRIDED_VECTOR_INC(init, incval, countiter, stridelevels) do { \
    size_t const _stridelevels = (stridelevels);                           \
    size_t * const _init = (init);                                         \
    _init[0] += (incval);                                                  \
    for (size_t _dim = 0; _dim < _stridelevels; _dim++) {                  \
      size_t const _thisinit = _init[_dim];                                \
      size_t const _thismax = countiter(_dim);                             \
      if (_thisinit < _thismax) break;                                     \
      else {                                                               \
        gasneti_assert(_dim != _stridelevels-1); /* indicates overflow */  \
        /* TODO-EX: possibly use ldiv() here? */                           \
        size_t const _carries = _thisinit / _thismax;                      \
        _init[_dim] = _thisinit % _thismax;                                \
        _init[_dim+1] += _carries;                                         \
      }                                                                    \
    }                                                                      \
  } while(0)

// Striding metadata iterators
#define _SITER_ARRAY_HELPER(idx)                 [(idx)]
#define SITER_ARRAY(parray)                      (parray)_SITER_ARRAY_HELPER

#define _SITER_SDIM_COUNT_HELPER(idx)            [(idx)].count
#define SITER_SDIM_COUNT(pdim)                   (pdim)_SITER_SDIM_COUNT_HELPER

#define _SITER_SDIM_STRIDE_HELPER_SMD_SELF(idx)  [(idx)].stride[SMD_SELF]
#define _SITER_SDIM_STRIDE_HELPER_SMD_PEER(idx)  [(idx)].stride[SMD_PEER]
#define SITER_SDIM_STRIDE(pdim, selfpeer)        (pdim)_SITER_SDIM_STRIDE_HELPER_##selfpeer

#define _SITER_NOOP_HELPER(idx) 
#define SITER_NOOP()                             (0)_SITER_NOOP_HELPER

/* 
   GASNETE_1STRIDED_HELPER(size_t stridelevels, countiter, void *p1base, stride1iter) 
   GASNETE_2STRIDED_HELPER(size_t stridelevels, countiter, void *p1base, stride1iter, 
                                                           void *p2base, stride2iter) 
    expands to code based on the template below (shown for the case of stridelevels == 3), 

   Iter arguments should be one of the following iterator macros. 
   Arguments may be expressions, eg to offset by contiglevel
     SITER_ARRAY({size_t,ptrdiff_t} *parray) 
       iterates over parray[0..(stridelevels-1)]
     SITER_SDIM_COUNT(gasneti_vis_smd_dim_t *pdim)
       iterates over pdim[0..(stridelevels-1)].count
     SITER_SDIM_STRIDE(gasneti_vis_smd_dim_t *pdim, SMD_{SELF,PEER})
       iterates over pdim[0..(stridelevels-1)].stride[SMD_SELF|SMD_PEER]

   If stridelevels > GASNETE_LOOPING_DIMS, then we use the generalized 
     striding code shown in the default case of GASNETE_STRIDED_HELPER.
   Parameters: 
     * if caller scope contains the declaration: 
         GASNETE_STRIDED_HELPER_DECLARE_PARTIAL(size_t numchunks, size_t *init, 
                                                int addr_already_offset, int update_addr_init);
       then the traversal will iterate over a total of numchunks contiguous chunks, 
       beginning at chunk coordinate indicated by init[0..(stridelevels-1)]
       if addr_already_offset is nonzero, the code assumes p1base/p2base already reference the first chunk,
       otherwise, the pointer values are advanced based on init to reach the first chunk
       iff update_addr_init is nonzero and there are chunks remaining, 
       then p1base/p2base/init are updated on exit to point to the next unused chunk
       
    uint8_t * _p1 = p1base;
    uint8_t * _p2 = p2base; // only for 2STRIDED, otherwise always NULL
    size_t _chunkcnt = numchunks;

    if (HAVE_PARTIAL && !addr_already_offset) {
      for (size_t _dim = 0; _dim < stridelevels; _dim++) {
        _p1 += stride1iter(_dim) * (ptrdiff_t)init[_dim];
        _p2 += stride2iter(_dim) * (ptrdiff_t)init[_dim];
      }
    }

    size_t const _count0 = countiter(0);
    ptrdiff_t const _p1bump0 = stride1iter(0);
    ptrdiff_t const _p2bump0 = stride2iter(0);
    size_t _i0 = (HAVE_PARTIAL ? _count0 - init[0] : _count0);

    size_t const _count1 = countiter(1);
    ptrdiff_t const _p1bump1 = stride1iter(1) - (ptrdiff_t)_count0 * stride1iter(0);
    ptrdiff_t const _p2bump1 = stride2iter(1) - (ptrdiff_t)_count0 * stride2iter(0);
    size_t _i1 = (HAVE_PARTIAL ? _count1 - init[1] : _count1);

    size_t const _count2 = countiter(2);
    ptrdiff_t const _p1bump2 = stride1iter(2) - (ptrdiff_t)_count1 * stride1iter(1);
    ptrdiff_t const _p2bump2 = stride2iter(2) - (ptrdiff_t)_count1 * stride2iter(1);
    size_t _i2 = (HAVE_PARTIAL ? _count2 - init[2] : _count2);

    goto body;

    for (_i2 = _count2; _i2; _i2--) { 

    for (_i1 = _count1; _i1; _i1--) {

    for (_i0 = _count0; _i0; _i0--) {
      body:
      GASNETE_STRIDED_HELPER_LOOPBODY(_p1,_p2);
      _p1 += _p1bump0;
      _p2 += _p2bump0;
      if_pf (HAVE_PARTIAL && --_chunkcnt == 0) goto done;
    }

      _p1 += _p1bump1;
      _p2 += _p2bump1;
    }

      _p1 += _p1bump2;
      _p2 += _p2bump2;
    }
    done: ;
    if (HAVE_PARTIAL && update_addr_init) {
      if (!_i0) ; // loop nest terminated 
      else if (!--_i0) { _i0 = _count0; 

        _p1 += _p1bump1; 
        _p2 += _p2bump1; 
        if (!--_i1) { _i1 = _count1; 

          _p1 += _p1bump2; 
          _p2 += _p2bump2; 
          if (!--_i2) { _i2 = _count2; 

          }
        }
      }
      p1base = _p1;
      p2base = _p2;

      init[0] = _count0 - _i0;
      init[1] = _count1 - _i1;
      init[2] = _count2 - _i2;
    }
*/

#define _STRIDED_HELPER_DECLARE_NO2 \
 static int8_t _strided_helper_no2 = (int8_t)sizeof(_strided_helper_no2)
static int32_t _strided_helper_no2 = (int32_t)sizeof(_strided_helper_no2);
#define _STRIDED_HELPER_HAVE2 (sizeof(_strided_helper_no2) == 4)

#define GASNETE_STRIDED_HELPER_DECLARE_PARTIAL(numchunks, init, addr_already_offset, update_addr_init) \
       size_t * const _strided_init = (init);                                                  \
       size_t _strided_chunkcnt = (numchunks);                                                 \
       int const _strided_addr_already_offset = (addr_already_offset);                         \
       int const _strided_update_addr_init = (update_addr_init);                               \
       static int8_t _strided_helper_havepartial = (int8_t)sizeof(_strided_helper_havepartial)

static int32_t * const _strided_init = 
   (sizeof(_strided_init)?NULL:(void*)&_strided_init); /* NULL:NULL triggers gcc -O1 bug on sysx */
static int32_t _strided_chunkcnt = (int32_t)sizeof(_strided_chunkcnt);
static int32_t const _strided_addr_already_offset = (int32_t)sizeof(_strided_addr_already_offset);
static int32_t const _strided_update_addr_init = (int32_t)sizeof(_strided_update_addr_init);
static int32_t const _strided_helper_havepartial = (int32_t)sizeof(_strided_helper_havepartial);
#define _STRIDED_HELPER_HAVEPARTIAL (sizeof(_strided_helper_havepartial) == 1)

#define _STRIDED_LABELHLP2(idx,name,line) _STRIDED_LABEL_##name##_##idx##_##line
#define _STRIDED_LABELHLP(idx,name,line)  _STRIDED_LABELHLP2(idx,name,line)
#define _STRIDED_LABEL(idx,name) _STRIDED_LABELHLP(idx,name,__LINE__)

#define _STRIDED_HELPER_SETUP_BASE(countiter, stride1iter, stride2iter)      \
    size_t const _count0 = countiter(0);                                     \
    ptrdiff_t const _p1bump0 = stride1iter(0);                               \
    ptrdiff_t const _p2bump0 = (_STRIDED_HELPER_HAVE2 ? stride2iter(0) : 0); \
    size_t _i0 = _count0;                                                    \
    if (_STRIDED_HELPER_HAVEPARTIAL) {                                       \
      gasneti_assert(_strided_init[0] < _count0);                            \
      _i0 -= _strided_init[0];                                               \
    }

#define _STRIDED_HELPER_SETUP_INT(curr, lower, countiter, stride1iter, stride2iter)  \
    size_t const _count##curr = countiter(curr);                                     \
    ptrdiff_t const _p1bump##curr = stride1iter(curr) -                              \
                                    ((ptrdiff_t)_count##lower) * stride1iter(lower); \
    ptrdiff_t const _p2bump##curr = (_STRIDED_HELPER_HAVE2 ?                         \
          stride2iter(curr) - ((ptrdiff_t)_count##lower) * stride2iter(lower) : 0);  \
    size_t _i##curr = _count##curr;                                                  \
    if (_STRIDED_HELPER_HAVEPARTIAL) {                                               \
      gasneti_assert(_strided_init[curr] < _count##curr);                            \
      _i##curr -= _strided_init[curr];                                               \
    }

#define _STRIDED_HELPER_LOOPHEAD_BASE()
#define _STRIDED_HELPER_LOOPHEAD_INT(curr,junk) \
    for (_i##curr = _count##curr; _i##curr; _i##curr--) { 

#define _STRIDED_HELPER_LOOPTAIL_BASE()
#define _STRIDED_HELPER_LOOPTAIL_INT(curr,junk)        \
      _p1 += _p1bump##curr;                            \
      if (_STRIDED_HELPER_HAVE2) _p2 += _p2bump##curr; \
      else gasneti_assert(_p2 == NULL);                \
    }

#define _STRIDED_HELPER_CLEANUPHEAD_BASE()
#define _STRIDED_HELPER_CLEANUPHEAD_INT(curr,junk)     \
      _p1 += _p1bump##curr;                            \
      if (_STRIDED_HELPER_HAVE2) _p2 += _p2bump##curr; \
      if (!--_i##curr) { _i##curr = _count##curr;

#define _STRIDED_HELPER_CLEANUPTAIL_BASE()
#define _STRIDED_HELPER_CLEANUPTAIL_INT(curr,junk) }

#define _STRIDED_HELPER_CLEANUPINIT_BASE()
#define _STRIDED_HELPER_CLEANUPINIT_INT(curr,junk) \
  _strided_init[curr] = _count##curr - _i##curr;   \
  gasneti_assert(_strided_init[curr] < _count##curr);

#define _STRIDED_HELPER_CASE_BASE(countiter, stride1iter, stride2iter) 
#define _STRIDED_HELPER_CASE_INT(junk,curr,countiter, stride1iter, stride2iter) \
  case curr+1: {                                                    \
    GASNETE_METAMACRO3_ASC##curr(_STRIDED_HELPER_SETUP,             \
                  countiter, stride1iter, stride2iter)              \
    goto _STRIDED_LABEL(curr,BODY);                                 \
    GASNETE_METAMACRO_DESC##curr(_STRIDED_HELPER_LOOPHEAD)          \
    for (_i0 = _count0; _i0; _i0--) {                               \
      _STRIDED_LABEL(curr,BODY): ;                                  \
      GASNETE_STRIDED_HELPER_LOOPBODY(_p1,_p2);                     \
      _p1 += _p1bump0;                                              \
      if (_STRIDED_HELPER_HAVE2) _p2 += _p2bump0;                   \
      if_pf (_STRIDED_HELPER_HAVEPARTIAL &&                         \
             --_strided_chunkcnt == 0)                              \
        goto _STRIDED_LABEL(curr,DONE);                             \
    }                                                               \
    GASNETE_METAMACRO_ASC##curr(_STRIDED_HELPER_LOOPTAIL)           \
    _STRIDED_LABEL(curr,DONE): ;                                    \
    if (_STRIDED_HELPER_HAVEPARTIAL && _strided_update_addr_init) { \
      if (!_i0) ; /* loop nest terminated */                        \
      else if (!--_i0) { _i0 = _count0;                             \
        GASNETE_METAMACRO_ASC##curr(_STRIDED_HELPER_CLEANUPHEAD)    \
        GASNETE_METAMACRO_ASC##curr(_STRIDED_HELPER_CLEANUPTAIL)    \
      }                                                             \
      *_pp1base = _p1;                                              \
      if (_STRIDED_HELPER_HAVE2) *_pp2base = _p2;                   \
      else gasneti_assert(_p2 == NULL);                             \
      _strided_init[0] = _count0 - _i0;                             \
      GASNETE_METAMACRO_ASC##curr(_STRIDED_HELPER_CLEANUPINIT)      \
    }                                                               \
  } break;

#if GASNETE_LOOPING_DIMS > GASNETE_METAMACRO_DEPTH_MAX
#error GASNETE_LOOPING_DIMS must be <= GASNETE_METAMACRO_DEPTH_MAX
#endif

#if GASNET_DEBUG
#define GASNETE_IS_DEBUG 1
#else
#define GASNETE_IS_DEBUG 0
#endif

#define GASNETE_1STRIDED_HELPER(stridelevels, countiter, p1base, stride1iter) do { \
    _STRIDED_HELPER_DECLARE_NO2;                                                   \
    void *_dummy2 = NULL;                                                          \
    GASNETE_2STRIDED_HELPER(stridelevels, countiter, p1base, stride1iter,          \
                            _dummy2, SITER_NOOP());                                \
  } while (0)

#define GASNETE_2STRIDED_HELPER(stridelevels, countiter, p1base, stride1iter, p2base, stride2iter) do { \
  void ** const _pp1base = &(p1base);                                  \
  void ** const _pp2base = &(p2base);                                  \
  /* general setup code */                                             \
  uint8_t *_p1 = *_pp1base;                                            \
  uint8_t *_p2 = *_pp2base;                                            \
  size_t const _stridelevels = (stridelevels);                         \
  gasneti_assert(_stridelevels > 0); /* should never use for degen */  \
  if (_STRIDED_HELPER_HAVEPARTIAL && !_strided_addr_already_offset) {  \
    for (size_t _dim = 0; _dim < _stridelevels; _dim++) {              \
      _p1 += stride1iter(_dim) * (ptrdiff_t)_strided_init[_dim];       \
      if (_STRIDED_HELPER_HAVE2)                                       \
        _p2 += stride2iter(_dim) * (ptrdiff_t)_strided_init[_dim];     \
    }                                                                  \
  }                                                                    \
  switch (_stridelevels) {                                             \
    _CONCAT(GASNETE_METAMACRO3_ASC, GASNETE_LOOPING_DIMS)(             \
      _STRIDED_HELPER_CASE, countiter, stride1iter, stride2iter)       \
    default: { /* arbitrary dimensions > GASNETE_LOOPING_DIMS */       \
      size_t    __idx[GASNETE_DIRECT_DIMS];                            \
      ptrdiff_t __p1bump[GASNETE_DIRECT_DIMS];                         \
      ptrdiff_t __p2bump[GASNETE_DIRECT_DIMS];                         \
      size_t * const _idx =                                            \
                   (_stridelevels <= GASNETE_DIRECT_DIMS ? __idx :     \
                    gasneti_malloc(_stridelevels*sizeof(size_t)));     \
      ptrdiff_t * const _p1bump =                                      \
                   (_stridelevels <= GASNETE_DIRECT_DIMS ? __p1bump :  \
                    gasneti_malloc(_stridelevels*sizeof(ptrdiff_t)));  \
      ptrdiff_t * const _p2bump = ((!_STRIDED_HELPER_HAVE2 ||          \
                    _stridelevels <= GASNETE_DIRECT_DIMS) ? __p2bump : \
                    gasneti_malloc(_stridelevels*sizeof(ptrdiff_t)));  \
      _idx[0] = countiter(0);                                          \
      _p1bump[0] = stride1iter(0);                                     \
      _p2bump[0] = (_STRIDED_HELPER_HAVE2 ? stride2iter(0) : 0);       \
      for (size_t _d = 1; _d < _stridelevels; _d++) {                  \
        _idx[_d] = countiter(_d);                                      \
        _p1bump[_d] = stride1iter(_d) -                                \
                 ((ptrdiff_t)_idx[_d-1]) * stride1iter(_d-1);          \
        if (_STRIDED_HELPER_HAVE2)                                     \
          _p2bump[_d] = stride2iter(_d) -                              \
                 ((ptrdiff_t)_idx[_d-1]) * stride2iter(_d-1);          \
      }                                                                \
      if (_STRIDED_HELPER_HAVEPARTIAL) {                               \
        for (size_t _d = 0; _d < _stridelevels; _d++) {                \
          gasneti_assert(_strided_init[_d] < _idx[_d]);                \
          _idx[_d] -= _strided_init[_d];                               \
        }                                                              \
      }                                                                \
      uint8_t const *_p1_truebase = *_pp1base;                         \
      uint8_t const *_p2_truebase = *_pp2base;                         \
      if (GASNETE_IS_DEBUG &&                                           \
        _STRIDED_HELPER_HAVEPARTIAL && _strided_addr_already_offset) { \
        for (size_t _d = 0; _d < _stridelevels; _d++) {                \
         _p1_truebase -= (ptrdiff_t)_strided_init[_d] * stride1iter(_d); \
         if (_STRIDED_HELPER_HAVE2)                                    \
          _p2_truebase -= (ptrdiff_t)_strided_init[_d] * stride2iter(_d); \
        }                                                              \
      }                                                                \
      while (1) { /* main iteration loop */                            \
       _STRIDED_LABEL(general,BODY): ;                                 \
        GASNETE_CHECK_PTR(_p1, _p1_truebase, stride1iter, countiter,   \
                          _idx, 1, _stridelevels);                     \
        if (_STRIDED_HELPER_HAVE2)                                     \
          GASNETE_CHECK_PTR(_p2, _p2_truebase, stride2iter, countiter, \
                            _idx, 1, _stridelevels);                   \
        else gasneti_assert(_p2 == NULL);                              \
        GASNETE_STRIDED_HELPER_LOOPBODY(_p1,_p2);                      \
        _p1 += _p1bump[0];                                             \
        if (_STRIDED_HELPER_HAVE2) _p2 += _p2bump[0];                  \
        if_pf (_STRIDED_HELPER_HAVEPARTIAL &&                          \
               --_strided_chunkcnt == 0) break;                        \
        if (--_idx[0] == 0) { /* end 0-level body */                   \
          for (size_t _d=1; _d < _stridelevels; _d++) {                \
            _p1 += _p1bump[_d];                                        \
            if (_STRIDED_HELPER_HAVE2) _p2 += _p2bump[_d];             \
            if (--_idx[_d]) { /* begin _d-level body */                \
              for (size_t _e=_d-1; ; _e--) { /* reset lower idx */     \
                _idx[_e] = countiter(_e);                              \
                if (!_e) goto _STRIDED_LABEL(general,BODY);            \
              } gasneti_unreachable();                                 \
            }                                                          \
          }                                                            \
          for (size_t _d=0; _d < _stridelevels; _d++)                  \
            gasneti_assert(_idx[_d] == 0);                             \
          break; /* all _idx[] zero, iteration complete */             \
        }                                                              \
      }                                                                \
      /* loop cleanup code */                                          \
      if (_STRIDED_HELPER_HAVEPARTIAL && _strided_update_addr_init) {  \
        if (_idx[0]) { /* early termination */                         \
          for (size_t _d=0; _d < _stridelevels; _d++) {                \
            if (--_idx[_d] == 0) { _idx[_d] = countiter(_d);           \
              if (_d+1 < _stridelevels) {                              \
                _p1 += _p1bump[_d+1];                                  \
                if (_STRIDED_HELPER_HAVE2) _p2 += _p2bump[_d+1];       \
              }                                                        \
            } else break;                                              \
          }                                                            \
          *_pp1base = _p1;                                             \
          if (_STRIDED_HELPER_HAVE2) *_pp2base = _p2;                  \
          else gasneti_assert(_p2 == NULL);                            \
          for (size_t _d = 0; _d < _stridelevels; _d++) {              \
            _strided_init[_d] = countiter(_d) - _idx[_d];              \
            gasneti_assert(_strided_init[_d] < countiter(_d));         \
          }                                                            \
        }                                                              \
      }                                                                \
      if (_stridelevels > GASNETE_DIRECT_DIMS) {                       \
        gasneti_free(_idx);                                            \
        gasneti_free(_p1bump);                                         \
        if (_STRIDED_HELPER_HAVE2) gasneti_free(_p2bump);              \
      }                                                                \
    } /* default */                                                    \
  } /* switch */                                                       \
} while (0)

#if GASNET_DEBUG
  /* assert the generalized looping code is functioning properly */
  #define GASNETE_CHECK_PTR(ploc, truebase, strideiter, countiter, idx, invertidx, stridelevels) do { \
      uint8_t const *_ptest = (truebase);                       \
      for (size_t _d=0; _d < (stridelevels); _d++) {            \
        size_t _thisidx = (idx)[_d];                            \
        if (invertidx) _thisidx = countiter(_d) - _thisidx;     \
        gasneti_assert(_thisidx < countiter(_d));               \
        _ptest += _thisidx * strideiter(_d);                    \
      }                                                         \
      gasneti_assert(_ptest == (ploc));                         \
    } while (0)
#else
  #define GASNETE_CHECK_PTR(ploc, truebase, strideiter, countiter, idx, invertidx, stridelevels) ((void)0)
#endif

/*---------------------------------------------------------------------------------*/
/* reference version that uses individual puts of the dualcontiguity size */
gex_Event_t gasnete_puts_ref_indiv(gasneti_strided_op_t * const sop, 
                                   gasnete_synctype_t const synctype, 
                                   gex_TM_t const tm, gex_Rank_t const rank, 
                                   gex_Flags_t flags GASNETE_THREAD_FARG) {
  gasneti_vis_smd_t * const smd = &(sop->smd);
  GASNETI_TRACE_EVENT(C, PUTS_REF_INDIV);
  gasneti_assert(smd->elemsz > 0);
  gasneti_assert(smd->stridelevels > 0);
  gasneti_assert(!GASNETI_SUPERNODE_LOCAL(rank));
  gasneti_assert(!(flags & ~GEX_FLAG_IMMEDIATE)); // TODO-EX
  // TODO-EX: Team support
  GASNETE_START_NBIREGION(synctype, 0);

    gasneti_vis_smd_dim_t const * const sdim = smd->dim;
    size_t const elemsz = smd->elemsz;
    #define GASNETE_STRIDED_HELPER_LOOPBODY(p1,p2)  \
      GASNETE_PUT_INDIV(0, rank, p2, p1, elemsz)
    GASNETE_2STRIDED_HELPER(smd->stridelevels,   SITER_SDIM_COUNT(sdim),
                            smd->addr[SMD_SELF], SITER_SDIM_STRIDE(sdim, SMD_SELF),
                            smd->addr[SMD_PEER], SITER_SDIM_STRIDE(sdim, SMD_PEER));
    #undef GASNETE_STRIDED_HELPER_LOOPBODY

  GASNETE_END_NBIREGION_AND_RETURN(synctype, 0);
}

/* reference version that uses individual gets of the dualcontiguity size */
gex_Event_t gasnete_gets_ref_indiv(gasneti_strided_op_t * const sop, 
                                   gasnete_synctype_t const synctype, 
                                   gex_TM_t const tm, gex_Rank_t const rank, 
                                   gex_Flags_t flags GASNETE_THREAD_FARG) {
  gasneti_vis_smd_t * const smd = &(sop->smd);
  GASNETI_TRACE_EVENT(C, GETS_REF_INDIV);
  gasneti_assert(smd->elemsz > 0);
  gasneti_assert(smd->stridelevels > 0);
  gasneti_assert(!GASNETI_SUPERNODE_LOCAL(rank));
  gasneti_assert(!(flags & ~GEX_FLAG_IMMEDIATE)); // TODO-EX
  // TODO-EX: Team support
  GASNETE_START_NBIREGION(synctype, 0);

    gasneti_vis_smd_dim_t const * const sdim = smd->dim;
    size_t const elemsz = smd->elemsz;
    #define GASNETE_STRIDED_HELPER_LOOPBODY(p1,p2)  \
      GASNETE_GET_INDIV(0, p1, rank, p2, elemsz)
    GASNETE_2STRIDED_HELPER(smd->stridelevels,   SITER_SDIM_COUNT(sdim),
                            smd->addr[SMD_SELF], SITER_SDIM_STRIDE(sdim, SMD_SELF),
                            smd->addr[SMD_PEER], SITER_SDIM_STRIDE(sdim, SMD_PEER));
    #undef GASNETE_STRIDED_HELPER_LOOPBODY

  GASNETE_END_NBIREGION_AND_RETURN(synctype, 0);
}

// perform a loopback/PSHM memcpy of a strided area
void gasnete_strided_memcpy(void * dstbase, void * srcbase, 
                            size_t const stridelevels, size_t const elemsz,
                            gasneti_vis_smd_dim_t const * const sdim, int srcside) {
  gasneti_assert(elemsz > 0);
  gasneti_assert(stridelevels > 0);

  #define SITER_SRC_STRIDE(idx) sdim[idx].stride[srcside]
  #define SITER_DST_STRIDE(idx) sdim[idx].stride[!srcside]
  #define GASNETE_STRIDED_HELPER_LOOPBODY(p1,p2)  \
    GASNETE_FAST_UNALIGNED_MEMCPY(p1, p2, elemsz)

    GASNETE_2STRIDED_HELPER(stridelevels, SITER_SDIM_COUNT(sdim),
                            dstbase, SITER_DST_STRIDE,
                            srcbase, SITER_SRC_STRIDE);
  #undef SITER_SRC_STRIDE
  #undef SITER_DST_STRIDE
  #undef GASNETE_STRIDED_HELPER_LOOPBODY
}

#if GASNETE_PARTIALPACK_TEST && GASNET_DEBUG
// Test code for partial packing
void gasnete_partialpack_memcpy(void * dstbase, void * srcbase, 
                            size_t const stridelevels, size_t const elemsz,
                            gasneti_vis_smd_dim_t const * const sdim, int srcside) {
  gasneti_assert(elemsz > 0);
  gasneti_assert(stridelevels > 0);

  #define SITER_SRC_STRIDE(idx) sdim[idx].stride[srcside]
  #define SITER_DST_STRIDE(idx) sdim[idx].stride[!srcside]
  #define GASNETE_STRIDED_HELPER_LOOPBODY(p1,p2)  \
    do { GASNETE_FAST_UNALIGNED_MEMCPY(p1, p2, elemsz); invchunks++; } while (0)

  size_t total_chunks = 1;
  for (size_t d = 0; d < stridelevels; d++)
    total_chunks *= sdim[d].count;
  size_t *init = gasneti_calloc(stridelevels, sizeof(size_t));
  size_t *tmpv = gasneti_calloc(stridelevels, sizeof(size_t));
  int iter = 0;
  void *psrc = srcbase;
  void *pdst = dstbase;
  while (total_chunks) {
    size_t numchunks = (total_chunks + 1)/2;
    size_t invchunks = 0;
    int addr_already_offset = iter % 2;
    if (!addr_already_offset) { psrc = srcbase; pdst = dstbase; }
    GASNETE_STRIDED_HELPER_DECLARE_PARTIAL(numchunks, init, addr_already_offset, 1);

    GASNETE_2STRIDED_HELPER(stridelevels, SITER_SDIM_COUNT(sdim),
                            pdst, SITER_DST_STRIDE,
                            psrc, SITER_SRC_STRIDE);

    gasneti_assert(invchunks == numchunks);
    if (total_chunks > numchunks) { // partial outputs only valid when there are trailing elements
      GASNETE_STRIDED_VECTOR_INC(tmpv, numchunks, SITER_SDIM_COUNT(sdim), stridelevels);
      GASNETE_CHECK_PTR(psrc, srcbase, SITER_SRC_STRIDE, SITER_SDIM_COUNT(sdim), tmpv, 0, stridelevels); 
      GASNETE_CHECK_PTR(pdst, dstbase, SITER_DST_STRIDE, SITER_SDIM_COUNT(sdim), tmpv, 0, stridelevels); 
      for (size_t d = 0; d < stridelevels; d++) gasneti_assert(init[d] == tmpv[d]);
    }
   
    total_chunks -= numchunks;
    iter++;
  }
  gasneti_free(init);
  gasneti_free(tmpv);
  #undef SITER_SRC_STRIDE
  #undef SITER_DST_STRIDE
  #undef GASNETE_STRIDED_HELPER_LOOPBODY
}
#define gasnete_strided_memcpy gasnete_partialpack_memcpy
#endif

#if _DISABLED_STUFF_

/*---------------------------------------------------------------------------------*/
// TODO-EX REMOVE THESE HACKS
#define gasnete_strided_empty(a,b) 0
#define gasnete_strided_nulldims(c,sl) (gasnete_strided_nulldims)(c+1,sl)
#define gasnete_strided_contiguity(s,c,sl) (gasnete_strided_contiguity)((ptrdiff_t *)s,c[0],c+1,sl)
#define gasnete_strided_stats(pstats, dststrides, srcstrides, count, stridelevels) \
  (gasnete_strided_stats)(pstats, (ptrdiff_t*)dststrides, (ptrdiff_t*)srcstrides, count[0], count+1, stridelevels)
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
/* convert strided metadata to addrlist metadata for the equivalent operation */
static void gasnete_convert_strided_to_addrlist(void * * srclist, void * * dstlist, 
                                    gasnete_strided_stats_t const *stats,
                                    void *_dstaddr, const size_t _dststrides[],
                                    void *_srcaddr, const size_t _srcstrides[],
                                    const size_t count[], size_t stridelevels) {
  size_t const contiglevel = stats->_dualcontiguity;
  size_t const limit = stridelevels - stats->_nulldims;
  size_t const srccontigsz = stats->_srccontigsz;
  size_t const dstcontigsz = stats->_dstcontigsz;

  gasneti_assert(srclist != NULL && dstlist != NULL && stats != NULL);
  gasneti_assert(!gasnete_strided_empty(count, stridelevels));
  gasneti_assert(limit > contiglevel);

  if (srccontigsz == dstcontigsz) {
    void * * srcpos = srclist; void * * dstpos = dstlist;
    void *srcaddr = _srcaddr; const size_t * const srcstrides = _srcstrides; 
    void *dstaddr = _dstaddr; const size_t * const dststrides = _dststrides; 
    #define GASNETE_STRIDED_HELPER_LOOPBODY(psrc,pdst)  do { \
      *(srcpos++) = psrc;                                    \
      *(dstpos++) = pdst;                                    \
    } while(0)
    GASNETE_STRIDED_HELPER(limit,contiglevel);
    #undef GASNETE_STRIDED_HELPER_LOOPBODY
    gasneti_assert(srcpos == srclist+stats->_srcsegments);
    gasneti_assert(dstpos == dstlist+stats->_dstsegments);
  } else {
    size_t _looplim; 
    void * * srcpos; void * * dstpos;
    void *srcaddr; const size_t * __srcstrides; 
    void *dstaddr; const size_t * __dststrides; 
    if (srccontigsz < dstcontigsz) {
      _looplim = dstcontigsz / srccontigsz;
      gasneti_assert(_looplim*srccontigsz == dstcontigsz);
      srcpos = srclist; dstpos = dstlist;
      srcaddr = _srcaddr; dstaddr = _dstaddr;
      __srcstrides = _srcstrides; __dststrides = _dststrides;
    } else { /* dstcontigsz < srccontigsz : swap metadata to allow unified loop nest below */
      _looplim = srccontigsz / dstcontigsz;
      gasneti_assert(_looplim*dstcontigsz == srccontigsz);
      srcpos = dstlist; dstpos = srclist;
      srcaddr = _dstaddr; dstaddr = _srcaddr;
      __srcstrides = _dststrides; __dststrides = _srcstrides;
    }
    { size_t const looplim = _looplim;
      size_t loopcnt = 1;
      const size_t * const srcstrides = __srcstrides; 
      const size_t * const dststrides = __dststrides; 
      #define GASNETE_STRIDED_HELPER_LOOPBODY(psrc,pdst)  do { \
        *(srcpos++) = psrc;                                    \
        if (--loopcnt == 0) {                                  \
          *(dstpos++) = pdst;                                  \
          loopcnt = looplim;                                   \
        }                                                      \
      } while(0)
      GASNETE_STRIDED_HELPER(limit,contiglevel);
      #undef GASNETE_STRIDED_HELPER_LOOPBODY
    }
    if (srccontigsz < dstcontigsz) {
      gasneti_assert(srcpos == srclist+stats->_srcsegments);
      gasneti_assert(dstpos == dstlist+stats->_dstsegments);
    } else {
      gasneti_assert(dstpos == srclist+stats->_srcsegments);
      gasneti_assert(srcpos == dstlist+stats->_dstsegments);
    }
  }
}
/*---------------------------------------------------------------------------------*/
/* convert strided metadata to memvec metadata for the equivalent operation */
static void gasnete_convert_strided_to_memvec(gex_Memvec_t *srclist, gex_Memvec_t *dstlist, 
                                    gasnete_strided_stats_t const *stats,
                                    void *_dstaddr, const size_t _dststrides[],
                                    void *_srcaddr, const size_t _srcstrides[],
                                    const size_t count[], size_t stridelevels) {
  size_t const contiglevel = stats->_dualcontiguity;
  size_t const limit = stridelevels - stats->_nulldims;
  size_t const srccontigsz = stats->_srccontigsz;
  size_t const dstcontigsz = stats->_dstcontigsz;

  gasneti_assert(srclist != NULL && dstlist != NULL && stats != NULL);
  gasneti_assert(!gasnete_strided_empty(count, stridelevels));
  gasneti_assert(limit > contiglevel);

  if (srccontigsz == dstcontigsz) {
    gex_Memvec_t *srcpos = srclist; gex_Memvec_t *dstpos = dstlist;
    void *srcaddr = _srcaddr; const size_t * const srcstrides = _srcstrides; 
    void *dstaddr = _dstaddr; const size_t * const dststrides = _dststrides; 
    #define GASNETE_STRIDED_HELPER_LOOPBODY(psrc,pdst)  do { \
      srcpos->gex_addr = psrc;                               \
      srcpos->gex_len = srccontigsz;                         \
      srcpos++;                                              \
      dstpos->gex_addr = pdst;                               \
      dstpos->gex_len = dstcontigsz;                         \
      dstpos++;                                              \
    } while(0)
    GASNETE_STRIDED_HELPER(limit,contiglevel);
    #undef GASNETE_STRIDED_HELPER_LOOPBODY
    gasneti_assert(srcpos == srclist+stats->_srcsegments);
    gasneti_assert(dstpos == dstlist+stats->_dstsegments);
  } else {
    size_t _looplim; 
    gex_Memvec_t *srcpos; gex_Memvec_t *dstpos;
    size_t _srccontigsz; size_t _dstcontigsz;
    void *srcaddr; const size_t * __srcstrides; 
    void *dstaddr; const size_t * __dststrides; 
    if (srccontigsz < dstcontigsz) {
      _looplim = dstcontigsz / srccontigsz;
      gasneti_assert(_looplim*srccontigsz == dstcontigsz);
      srcpos = srclist; dstpos = dstlist;
      srcaddr = _srcaddr; dstaddr = _dstaddr;
      _srccontigsz = srccontigsz; _dstcontigsz = dstcontigsz;
      __srcstrides = _srcstrides; __dststrides = _dststrides;
    } else { /* dstcontigsz < srccontigsz : swap metadata to allow unified loop nest below */
      _looplim = srccontigsz / dstcontigsz;
      gasneti_assert(_looplim*dstcontigsz == srccontigsz);
      srcpos = dstlist; dstpos = srclist;
      srcaddr = _dstaddr; dstaddr = _srcaddr;
      _srccontigsz = dstcontigsz; _dstcontigsz = srccontigsz;
      __srcstrides = _dststrides; __dststrides = _srcstrides;
    }
    { size_t const looplim = _looplim;
      size_t loopcnt = 1;
      const size_t * const srcstrides = __srcstrides; 
      const size_t * const dststrides = __dststrides; 
      #define GASNETE_STRIDED_HELPER_LOOPBODY(psrc,pdst)  do { \
        srcpos->gex_addr = psrc;                               \
        srcpos->gex_len = _srccontigsz;                        \
        srcpos++;                                              \
        if (--loopcnt == 0) {                                  \
          dstpos->gex_addr = pdst;                             \
          dstpos->gex_len = _dstcontigsz;                      \
          dstpos++;                                            \
          loopcnt = looplim;                                   \
        }                                                      \
      } while(0)
      GASNETE_STRIDED_HELPER(limit,contiglevel);
      #undef GASNETE_STRIDED_HELPER_LOOPBODY
    }
    if (srccontigsz < dstcontigsz) {
      gasneti_assert(srcpos == srclist+stats->_srcsegments);
      gasneti_assert(dstpos == dstlist+stats->_dstsegments);
    } else {
      gasneti_assert(dstpos == srclist+stats->_srcsegments);
      gasneti_assert(srcpos == dstlist+stats->_dstsegments);
    }
  }
}
/*---------------------------------------------------------------------------------*/
/* simple gather put, remotely contiguous */
#ifndef GASNETE_PUTS_GATHER_SELECTOR
#if GASNETE_USE_REMOTECONTIG_GATHER_SCATTER
gex_Event_t gasnete_puts_gather(gasnete_strided_stats_t const *stats, gasnete_synctype_t synctype,
                                    gex_Rank_t dstnode,
                                    void *dstaddr, const size_t dststrides[],
                                    void *srcaddr, const size_t srcstrides[],
                                    const size_t count[], size_t stridelevels GASNETE_THREAD_FARG) {
  gasnete_vis_threaddata_t * const td = GASNETE_VIS_MYTHREAD;
  size_t const nbytes = stats->_totalsz;
  gasneti_assert(stats->_dstcontiguity == stridelevels && stats->_srccontiguity < stridelevels); /* only supports gather put */
  gasneti_assert(dstnode != gasneti_mynode); /* silly to use for local cases */
  gasneti_assert(nbytes > 0);
  GASNETI_TRACE_EVENT(C, PUTS_GATHER);

  { gasneti_vis_op_t * const visop = gasneti_malloc(sizeof(gasneti_vis_op_t)+nbytes);
    void * const packedbuf = visop + 1;
    gasnete_strided_pack_all(srcaddr, srcstrides, count, stridelevels, packedbuf);
    visop->type = GASNETI_VIS_CAT_PUTS_GATHER;
    visop->event = gasnete_put_nb(gasneti_THUNK_TM, dstnode, dstaddr, packedbuf, nbytes, GEX_EVENT_DEFER, 0 GASNETE_THREAD_PASS);
    gasneti_assert(visop->event != GEX_EVENT_INVALID);
    GASNETE_PUSH_VISOP_RETURN(td, visop, synctype, 0);
  }
}
  #define GASNETE_PUTS_GATHER_SELECTOR(stats,synctype,dstnode,dstaddr,dststrides,srcaddr,srcstrides,count,stridelevels) \
    if (gasnete_vis_use_remotecontig &&                                                                                 \
        (stats)->_dstcontiguity == stridelevels && (stats)->_srccontiguity < stridelevels)                              \
      return gasnete_puts_gather(stats,synctype,dstnode,dstaddr,dststrides,srcaddr,srcstrides,count,stridelevels GASNETE_THREAD_PASS)
#else
  #define GASNETE_PUTS_GATHER_SELECTOR(stats,synctype,dstnode,dstaddr,dststrides,srcaddr,srcstrides,count,stridelevels) ((void)0)
#endif
#endif

/* simple scatter get, remotely contiguous */
#ifndef GASNETE_GETS_SCATTER_SELECTOR
#if GASNETE_USE_REMOTECONTIG_GATHER_SCATTER
gex_Event_t gasnete_gets_scatter(gasnete_strided_stats_t const *stats, gasnete_synctype_t synctype,
                                     void *dstaddr, const size_t dststrides[],
                                     gex_Rank_t srcnode,
                                     void *srcaddr, const size_t srcstrides[],
                                     const size_t count[], size_t stridelevels GASNETE_THREAD_FARG) {
  gasnete_vis_threaddata_t * const td = GASNETE_VIS_MYTHREAD;
  size_t const nbytes = stats->_totalsz;
  gasneti_assert(stats->_srccontiguity == stridelevels && stats->_dstcontiguity < stridelevels); /* only supports scatter get */
  gasneti_assert(srcnode != gasneti_mynode); /* silly to use for local cases */
  gasneti_assert(nbytes > 0);
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
    visop->event = gasnete_get_nb(gasneti_THUNK_TM, packedbuf, srcnode, srcaddr, nbytes, 0 GASNETE_THREAD_PASS);
    gasneti_assert(visop->event != GEX_EVENT_INVALID);
    GASNETE_PUSH_VISOP_RETURN(td, visop, synctype, 1);
  }
}
  #define GASNETE_GETS_SCATTER_SELECTOR(stats,synctype,dstaddr,dststrides,srcnode,srcaddr,srcstrides,count,stridelevels) \
    if (gasnete_vis_use_remotecontig &&                                                                                  \
        (stats)->_srccontiguity == stridelevels && (stats)->_dstcontiguity < stridelevels)                               \
      return gasnete_gets_scatter(stats,synctype,dstaddr,dststrides,srcnode,srcaddr,srcstrides,count,stridelevels GASNETE_THREAD_PASS)
#else
  #define GASNETE_GETS_SCATTER_SELECTOR(stats,synctype,dstaddr,dststrides,srcnode,srcaddr,srcstrides,count,stridelevels) ((void)0)
#endif
#endif
/*---------------------------------------------------------------------------------*/
/* Pipelined AM gather-scatter put */
#ifndef GASNETE_PUTS_AMPIPELINE_SELECTOR
#if GASNETE_USE_AMPIPELINE
#define GASNETE_PUTS_AMPIPELINE_MAXPAYLOAD(stridelevels) (gex_AM_LUBRequestMedium() - (3*(stridelevels) + 1)*sizeof(size_t))
gex_Event_t gasnete_puts_AMPipeline(gasnete_strided_stats_t const *stats, gasnete_synctype_t synctype,
                                  gex_Rank_t dstnode,
                                   void *dstaddr, const size_t dststrides[],
                                   void *srcaddr, const size_t srcstrides[],
                                   const size_t count[], size_t stridelevels GASNETE_THREAD_FARG) {
  gasneti_assert(stats->_dstsegments > 1); /* supports scatter put */
  gasneti_assert(dstnode != gasneti_mynode); /* silly to use for local cases */
  GASNETI_TRACE_EVENT(C, PUTS_AMPIPELINE);
  GASNETE_START_NBIREGION(synctype, 0);

  // temporary storage:
  // local init[stridelevels] |  packet data
  //
  // packet data:
  //   packet_init[stridelevels] - cloned from local init[] during packing
  //   count[stridelevels+1] - copied from user count[]
  //   remote_strides[stridelevels] - copied from user dststrides[]
  //   packed data, to fill up to LUBRequestMedium()
  //     packetchunks complete chunks of dualcontigsz granularity: MIN(srccontigsz,dstcontigsz)
  //     no partial chunks
  //
  // AM Request args:
  //   &access_region_iop, dstaddr, stridelevels, dualcontiguity, packetchunks
  //
  // Each request handler unpacks into destination and sends a reply to markdone(1) the iop
  //
  { size_t * const init = gasneti_malloc(stridelevels*sizeof(size_t) + gex_AM_LUBRequestMedium());
    size_t * const packetbase = init + stridelevels;
    size_t * const packetinit = packetbase;
    size_t * const packetcount = packetinit + stridelevels;
    size_t * const packetstrides = packetcount + stridelevels + 1;
    size_t * const packedbuf = packetstrides + stridelevels;
    size_t const maxpayload = GASNETE_PUTS_AMPIPELINE_MAXPAYLOAD(stridelevels);
    size_t const packetoverhead = gex_AM_LUBRequestMedium() - maxpayload;
    size_t const chunksz = stats->_dualcontigsz;
    size_t const totalchunks = MAX(stats->_srcsegments,stats->_dstsegments);
    size_t const chunksperpacket = maxpayload / chunksz;
    size_t const packetcnt = (totalchunks + chunksperpacket - 1)/chunksperpacket;
    size_t remaining = totalchunks;
    gasneti_iop_t *iop = gasneti_iop_register(packetcnt,0 GASNETE_THREAD_PASS);
    gasneti_assert(chunksz*totalchunks == stats->_totalsz);
    gasneti_assert(chunksperpacket >= 1);
    memset(init, 0, stridelevels*sizeof(size_t)); /* init[] = [0..0] */
    memcpy(packetcount, count, (stridelevels+1)*sizeof(size_t));
    memcpy(packetstrides, dststrides, stridelevels*sizeof(size_t));
    while (remaining) {
      size_t const packetchunks = MIN(chunksperpacket, remaining);
      size_t * const adjinit = init+stats->_dualcontiguity;
      uint8_t *end;
      size_t nbytes;
      remaining -= packetchunks;
      memcpy(packetinit, init, stridelevels*sizeof(size_t));
      if (stats->_srccontiguity < stridelevels) { /* gather data payload from source into packet */
        end = gasnete_strided_pack_partial(&srcaddr, srcstrides, count, 
                                     stats->_dualcontiguity, stridelevels - stats->_nulldims, 
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
      gex_AM_RequestMedium(gasneti_THUNK_TM, dstnode, gasneti_handleridx(gasnete_puts_AMPipeline_reqh),
                               packetbase, nbytes, GEX_EVENT_NOW, 0,
                               PACK(iop), PACK(dstaddr), stridelevels, stats->_dualcontiguity, packetchunks);
    }
    gasneti_free(init);
    GASNETE_END_NBIREGION_AND_RETURN(synctype, 0);
  }
}
  #define GASNETE_PUTS_AMPIPELINE_SELECTOR(stats,synctype,dstnode,dstaddr,dststrides,srcaddr,srcstrides,count,stridelevels) \
    if (gasnete_vis_use_ampipe &&                                                                                           \
        (stats)->_dstsegments > 1 &&                                                                                        \
        (stats)->_dualcontigsz <= gasnete_vis_maxchunk &&                                                                   \
        (stats)->_dualcontigsz <= GASNETE_PUTS_AMPIPELINE_MAXPAYLOAD(stridelevels))                                         \
      return gasnete_puts_AMPipeline(stats,synctype,dstnode,dstaddr,dststrides,srcaddr,srcstrides,count,stridelevels GASNETE_THREAD_PASS)
#else
  #define GASNETE_PUTS_AMPIPELINE_SELECTOR(stats,synctype,dstnode,dstaddr,dststrides,srcaddr,srcstrides,count,stridelevels) ((void)0)
#endif
#endif
/* ------------------------------------------------------------------------------------ */
#if GASNETE_USE_AMPIPELINE
GASNETI_INLINE(gasnete_puts_AMPipeline_reqh_inner)
void gasnete_puts_AMPipeline_reqh_inner(gex_Token_t token,
  void *addr, size_t nbytes,
  void *iop, void *dstaddr, 
  gex_AM_Arg_t stridelevels, gex_AM_Arg_t contiglevel,
  gex_AM_Arg_t packetchunks) {
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
  gex_AM_ReplyShort(token, gasneti_handleridx(gasnete_putvis_AMPipeline_reph), 0, PACK(iop));
}
MEDIUM_HANDLER(gasnete_puts_AMPipeline_reqh,5,7, 
              (token,addr,nbytes, UNPACK(a0),      UNPACK(a1),      a2,a3,a4),
              (token,addr,nbytes, UNPACK2(a0, a1), UNPACK2(a2, a3), a4,a5,a6));
#endif
/*---------------------------------------------------------------------------------*/
/* Pipelined AM gather-scatter get */
#ifndef GASNETE_GETS_AMPIPELINE_SELECTOR
#if GASNETE_USE_AMPIPELINE
gex_Event_t gasnete_gets_AMPipeline(gasnete_strided_stats_t const *stats, gasnete_synctype_t synctype,
                                   void *dstaddr, const size_t dststrides[],
                                   gex_Rank_t srcnode,
                                   void *srcaddr, const size_t srcstrides[],
                                   const size_t count[], size_t stridelevels GASNETE_THREAD_FARG) {
  gasneti_assert(stats->_srcsegments > 1); /* supports gather get */
  gasneti_assert(srcnode != gasneti_mynode); /* silly to use for local cases */
  GASNETI_TRACE_EVENT(C, GETS_AMPIPELINE);

  // visop storage:
  //   table count[stridelevels+1] - copied from user count[]
  //   table local_strides[stridelevels] - copied from user dststrides[]
  //   table init x [packetcnt]:
  //     packet_init[stridelevels] - cloned from local init[] during packetization
  //   Request packet data
  //
  // Request packet data:
  //   packet_init[stridelevels] - cloned from table init[] during packetization
  //   count[stridelevels+1] - copied from user count[]
  //   remote_strides[stridelevels] - copied from user srcstrides[]
  //
  // AM Request args:
  //   &visop, srcaddr, stridelevels, dualcontiguity, packetchunks, packetidx
  //
  // Packetization partitions data to fit replies into LUBReplyMedium():
  //   packetchunks complete chunks of dualcontigsz granularity: MIN(srccontigsz,dstcontigsz)
  //   no partial chunks
  //   table inits are computed using full stridelevels dimensions
  //
  // Each request handler mallocs LUBReplyMedium() temporary storage, 
  //   packs source data there, sends and then frees.
  //
  // AM Reply args:
  //   &visop, packetidx, dualcontiguity, packetchunks
  //   Payload is just raw packed data
  //
  // Reply handler retrieves count[], local_strides[] and packet_init[] from visop
  //   and uses it to unpack data into destination
  //   weakatomic dec-and-test on visop->packetcnt to VISOP_SIGNAL
  //
  { size_t const chunksz = stats->_dualcontigsz;
    size_t const adjchunksz = stats->_dualcontigsz/count[0];
    size_t const totalchunks = MAX(stats->_srcsegments,stats->_dstsegments);
    size_t const chunksperpacket = gex_AM_LUBReplyMedium() / chunksz;
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
    gasneti_eop_t *eop;

    gasneti_assert(chunksz*totalchunks == stats->_totalsz);
    gasneti_assert(chunksperpacket >= 1);

    GASNETE_VISOP_SETUP(visop, synctype, 1);
    visop->addr = dstaddr;
    visop->count = stridelevels;
    #if GASNET_DEBUG
      visop->type = GASNETI_VIS_CAT_GETS_AMPIPELINE;
    #endif
    gasneti_assert(packetcnt <= GASNETI_ATOMIC_MAX);
    gasneti_assert(packetcnt == (gex_AM_Arg_t)packetcnt);
    gasneti_weakatomic_set(&(visop->packetcnt), packetcnt, GASNETI_ATOMIC_WMB_POST);

    memcpy(tablecount, count, (stridelevels+1)*sizeof(size_t)); /* TODO: merge with packetcount? */
    memcpy(packetcount, count, (stridelevels+1)*sizeof(size_t)); 
    memcpy(tablestrides, dststrides, stridelevels*sizeof(size_t));
    memcpy(packetstrides, srcstrides, stridelevels*sizeof(size_t));
    memset(tableinit, 0, stridelevels*sizeof(size_t)); /* init[] = [0..0] */
    eop = visop->eop; /* visop may disappear once the last AM is launched */

    for (packetidx = 0; packetidx < packetcnt; packetidx++) {
      size_t const packetchunks = MIN(chunksperpacket, remaining);
      size_t * const nexttableinit = tableinit + stridelevels;
      size_t const adjnbytes = packetchunks*adjchunksz;
      remaining -= packetchunks;
      memcpy(packetinit, tableinit, stridelevels*sizeof(size_t));
      gex_AM_RequestMedium(gasneti_THUNK_TM, srcnode, gasneti_handleridx(gasnete_gets_AMPipeline_reqh),
                      packetbase, packetnbytes, GEX_EVENT_NOW, 0,
                      PACK(visop), PACK(srcaddr), stridelevels, stats->_dualcontiguity, packetchunks, packetidx);

      if (remaining) {
        memcpy(nexttableinit, tableinit, stridelevels*sizeof(size_t));
        GASNETE_STRIDED_VECTOR_INC(nexttableinit, adjnbytes, count, 0, stridelevels);
      }
      tableinit = nexttableinit;
    }
    gasneti_assert(remaining == 0);
    gasneti_assert(tableinit == packetbase);
    GASNETE_VISOP_RETURN_VOLATILE(eop, synctype);
  }
}
  #define GASNETE_GETS_AMPIPELINE_SELECTOR(stats,synctype,dstaddr,dststrides,srcnode,srcaddr,srcstrides,count,stridelevels) \
    if (gasnete_vis_use_ampipe &&                                                                                           \
        (stats)->_srcsegments > 1 &&                                                                                        \
        (stats)->_dualcontigsz <= gasnete_vis_maxchunk &&                                                                   \
        (stats)->_dualcontigsz <= gex_AM_LUBReplyMedium())                                                                  \
      return gasnete_gets_AMPipeline(stats,synctype,dstaddr,dststrides,srcnode,srcaddr,srcstrides,count,stridelevels GASNETE_THREAD_PASS)
#else
  #define GASNETE_GETS_AMPIPELINE_SELECTOR(stats,synctype,dstaddr,dststrides,srcnode,srcaddr,srcstrides,count,stridelevels) ((void)0)
#endif
#endif
/* ------------------------------------------------------------------------------------ */
#if GASNETE_USE_AMPIPELINE
GASNETI_INLINE(gasnete_gets_AMPipeline_reqh_inner)
void gasnete_gets_AMPipeline_reqh_inner(gex_Token_t token,
  void *addr, size_t nbytes,
  void *_visop, void *srcaddr, 
  gex_AM_Arg_t stridelevels, gex_AM_Arg_t contiglevel,
  gex_AM_Arg_t packetchunks, gex_AM_Arg_t packetidx) {

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
    gasneti_assert(packetchunks * chunksz <= gex_AM_LUBReplyMedium());
  }
  #endif
  { /* gather data payload from source into packet */
    uint8_t * const packedbuf = gasneti_malloc(gex_AM_LUBReplyMedium());
    uint8_t * const end = gasnete_strided_pack_partial(&srcaddr, packetstrides, packetcount, 
                               contiglevel, limit, 
                               packetchunks, packetinit+contiglevel, 
                               0, 0, packedbuf);
    size_t nbytes = end - (uint8_t *)packedbuf;

    gex_AM_ReplyMedium(token, gasneti_handleridx(gasnete_gets_AMPipeline_reph),
                           packedbuf, nbytes, GEX_EVENT_NOW, 0,
                           PACK(_visop),packetidx,contiglevel,packetchunks);
    gasneti_free(packedbuf);
  }
}
MEDIUM_HANDLER(gasnete_gets_AMPipeline_reqh,6,8, 
              (token,addr,nbytes, UNPACK(a0),      UNPACK(a1),      a2,a3,a4,a5),
              (token,addr,nbytes, UNPACK2(a0, a1), UNPACK2(a2, a3), a4,a5,a6,a7));
/* ------------------------------------------------------------------------------------ */
GASNETI_INLINE(gasnete_gets_AMPipeline_reph_inner)
void gasnete_gets_AMPipeline_reph_inner(gex_Token_t token,
  void *addr, size_t nbytes,
  void *_visop, gex_AM_Arg_t packetidx,
  gex_AM_Arg_t contiglevel, gex_AM_Arg_t packetchunks) {
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
  
  if (gasneti_weakatomic_decrement_and_test(&(visop->packetcnt), 
                                            GASNETI_ATOMIC_WMB_PRE|GASNETI_ATOMIC_WEAK_FENCE)) {
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
gex_Event_t gasnete_puts_ref_vector(gasnete_strided_stats_t const *stats, gasnete_synctype_t synctype,
                                  gex_Rank_t dstnode,
                                   void *dstaddr, const size_t dststrides[],
                                   void *srcaddr, const size_t srcstrides[],
                                   const size_t count[], size_t stridelevels GASNETE_THREAD_FARG) {
  GASNETI_TRACE_EVENT(C, PUTS_REF_VECTOR);
  gasneti_assert(!gasnete_strided_empty(count, stridelevels));
  gasneti_assert(GASNETE_PUTV_ALLOWS_VOLATILE_METADATA);

  if (stats->_dualcontiguity == stridelevels) { /* fully contiguous at both ends */
    const int islocal = (dstnode == gasneti_mynode);
    GASNETE_START_NBIREGION(synctype, islocal);
      GASNETE_PUT_INDIV(islocal, dstnode, dstaddr, srcaddr, stats->_totalsz);
    GASNETE_END_NBIREGION_AND_RETURN(synctype, islocal);
  } else {
    gex_Event_t retval;
    gex_Memvec_t * const srclist = gasneti_malloc(sizeof(gex_Memvec_t)*stats->_srcsegments);
    gex_Memvec_t * const dstlist = gasneti_malloc(sizeof(gex_Memvec_t)*stats->_dstsegments);

    gasnete_convert_strided_to_memvec(srclist, dstlist, stats, 
      dstaddr, dststrides, srcaddr, srcstrides, count, stridelevels);

    retval = gasnete_putv(synctype, gasneti_THUNK_TM, dstnode, 
                          stats->_dstsegments, dstlist, 
                          stats->_srcsegments, srclist,
                          0/*flags*/ GASNETE_THREAD_PASS);
    gasneti_free(srclist);
    gasneti_free(dstlist);
    return retval; 
  }
}
/* reference version that uses vector interface */
gex_Event_t gasnete_gets_ref_vector(gasnete_strided_stats_t const *stats, gasnete_synctype_t synctype,
                                   void *dstaddr, const size_t dststrides[],
                                   gex_Rank_t srcnode,
                                   void *srcaddr, const size_t srcstrides[],
                                   const size_t count[], size_t stridelevels GASNETE_THREAD_FARG) {
  GASNETI_TRACE_EVENT(C, GETS_REF_VECTOR);
  gasneti_assert(!gasnete_strided_empty(count, stridelevels));
  gasneti_assert(GASNETE_GETV_ALLOWS_VOLATILE_METADATA);

  if (stats->_dualcontiguity == stridelevels) { /* fully contiguous at both ends */
    const int islocal = (srcnode == gasneti_mynode);
    GASNETE_START_NBIREGION(synctype, islocal);
      GASNETE_GET_INDIV(islocal, dstaddr, srcnode, srcaddr, stats->_totalsz);
    GASNETE_END_NBIREGION_AND_RETURN(synctype, islocal);
  } else {
    gex_Event_t retval;
    gex_Memvec_t * const srclist = gasneti_malloc(sizeof(gex_Memvec_t)*stats->_srcsegments);
    gex_Memvec_t * const dstlist = gasneti_malloc(sizeof(gex_Memvec_t)*stats->_dstsegments);

    gasnete_convert_strided_to_memvec(srclist, dstlist, stats, 
      dstaddr, dststrides, srcaddr, srcstrides, count, stridelevels);

    retval = gasnete_getv(synctype, gasneti_THUNK_TM,
                          stats->_dstsegments, dstlist, 
                          srcnode,
                          stats->_srcsegments, srclist,
                          0/*flags*/ GASNETE_THREAD_PASS);
    gasneti_free(srclist);
    gasneti_free(dstlist);
    return retval; 
  }
}
/*---------------------------------------------------------------------------------*/
/* reference version that uses indexed interface */
gex_Event_t gasnete_puts_ref_indexed(gasnete_strided_stats_t const *stats, gasnete_synctype_t synctype,
                                  gex_Rank_t dstnode,
                                   void *dstaddr, const size_t dststrides[],
                                   void *srcaddr, const size_t srcstrides[],
                                   const size_t count[], size_t stridelevels GASNETE_THREAD_FARG) {
  GASNETI_TRACE_EVENT(C, PUTS_REF_INDEXED);
  gasneti_assert(!gasnete_strided_empty(count, stridelevels));
  gasneti_assert(GASNETE_PUTI_ALLOWS_VOLATILE_METADATA);

  if (stats->_dualcontiguity == stridelevels) { /* fully contiguous at both ends */
    const int islocal = (dstnode == gasneti_mynode);
    GASNETE_START_NBIREGION(synctype, islocal);
      GASNETE_PUT_INDIV(islocal, dstnode, dstaddr, srcaddr, stats->_totalsz);
    GASNETE_END_NBIREGION_AND_RETURN(synctype, islocal);
  } else {
    gex_Event_t retval;
    void * * const srclist = gasneti_malloc(sizeof(void *)*stats->_srcsegments);
    void * * const dstlist = gasneti_malloc(sizeof(void *)*stats->_dstsegments);

    gasnete_convert_strided_to_addrlist(srclist, dstlist, stats, 
      dstaddr, dststrides, srcaddr, srcstrides, count, stridelevels);

    retval = gasnete_puti(synctype, gasneti_THUNK_TM, dstnode, 
                          stats->_dstsegments, dstlist, stats->_dstcontigsz,
                          stats->_srcsegments, srclist, stats->_srccontigsz,
                          0/*flags*/ GASNETE_THREAD_PASS);
    gasneti_free(srclist);
    gasneti_free(dstlist);
    return retval; 
  }
}
/* reference version that uses indexed interface */
gex_Event_t gasnete_gets_ref_indexed(gasnete_strided_stats_t const *stats, gasnete_synctype_t synctype,
                                   void *dstaddr, const size_t dststrides[],
                                   gex_Rank_t srcnode,
                                   void *srcaddr, const size_t srcstrides[],
                                   const size_t count[], size_t stridelevels GASNETE_THREAD_FARG) {
  GASNETI_TRACE_EVENT(C, GETS_REF_INDEXED);
  gasneti_assert(!gasnete_strided_empty(count, stridelevels));
  gasneti_assert(GASNETE_GETI_ALLOWS_VOLATILE_METADATA);

  if (stats->_dualcontiguity == stridelevels) { /* fully contiguous at both ends */
    const int islocal = (srcnode == gasneti_mynode);
    GASNETE_START_NBIREGION(synctype, islocal);
      GASNETE_GET_INDIV(islocal, dstaddr, srcnode, srcaddr, stats->_totalsz);
    GASNETE_END_NBIREGION_AND_RETURN(synctype, islocal);
  } else {
    gex_Event_t retval;
    void * * const srclist = gasneti_malloc(sizeof(void *)*stats->_srcsegments);
    void * * const dstlist = gasneti_malloc(sizeof(void *)*stats->_dstsegments);

    gasnete_convert_strided_to_addrlist(srclist, dstlist, stats, 
      dstaddr, dststrides, srcaddr, srcstrides, count, stridelevels);

    retval = gasnete_geti(synctype, gasneti_THUNK_TM,
                          stats->_dstsegments, dstlist, stats->_dstcontigsz,
                          srcnode,
                          stats->_srcsegments, srclist, stats->_srccontigsz,
                          0/*flags*/ GASNETE_THREAD_PASS);
    gasneti_free(srclist);
    gasneti_free(dstlist);
    return retval; 
  }
}
#endif  // DISABLED
#if PLATFORM_COMPILER_CLANG && PLATFORM_COMPILER_VERSION_GE(2,8,0)
  #pragma clang diagnostic pop
#endif

/*---------------------------------------------------------------------------------*/
// returns the size of the bounding box containing all the elements in memory, and optionally the baseptr
GASNETI_INLINE(gasnete_smd_querybounds)
size_t gasnete_smd_querybounds(gasneti_vis_smd_t const *smd, int rside, const void **baseptr) {
  uint8_t *lo = (uint8_t*)smd->addr[rside]; // low and high element addresses
  uint8_t *hi = lo;
  for (size_t d = 0; d < smd->stridelevels; d++) {
     ptrdiff_t const stride = smd->dim[d].stride[rside];
     size_t const count = smd->dim[d].count;     
     if (stride >= 0) hi += stride * (count - 1);
     else             lo += stride * (count - 1);
  }
  if (baseptr) *baseptr = lo;
  return hi - lo + smd->elemsz; // adjust for length of last element
}

// Allocate a strided op and perform metadata normalization
// returns NULL for degenerate empty operation
GASNETI_INLINE(gasnete_build_strided_op)
gasneti_strided_op_t *gasnete_build_strided_op(
    int isput, // for tracing
    void * const selfaddr, const ptrdiff_t selfstrides[],
    void * const peeraddr, const ptrdiff_t peerstrides[],
    const size_t elemsz, const size_t count[], const size_t stridelevels
  ) {
  gasneti_assert(elemsz > 0);
  gasneti_assert(stridelevels > 0);
  gasneti_assert(selfstrides && peerstrides && count);
  gasneti_assert(selfaddr && peeraddr);
  size_t scratch_offset = offsetof(gasneti_strided_op_t, smd.dim[stridelevels]);
  size_t sopsz = scratch_offset + 
                 3*stridelevels*MAX(sizeof(ptrdiff_t),sizeof(size_t)); // TODO-EX: this may need to grow
  gasneti_strided_op_t * const sop = gasneti_malloc(sopsz);
  sop->scratch = (uint8_t*)sop + scratch_offset;
  sop->bouncebuf = NULL;

  gasneti_vis_smd_t * const smd = &(sop->smd);
  smd->elemsz = elemsz;
  smd->addr[SMD_SELF] = selfaddr;
  smd->addr[SMD_PEER] = peeraddr;

  // SMD normalization pass 1: 
  // ------------------------
  // populate SMD
  // handle empty count degeneracy
  // remove null dimensions
  // perform stride inversion to normalize peer stride
  size_t opt_stridelevels = stridelevels;
  gasneti_vis_smd_dim_t *outdim = &(smd->dim[0]);
  ptrdiff_t last_peerstride = -1;
  int is_sorted = 1;
  for (size_t d = 0; d < stridelevels; d++) {
    size_t const cnt = count[d];
    if_pf (cnt == 0) { // degenerate no-op
      gasneti_free(sop);
      return NULL;
    } else if_pf (cnt == 1) { // null dimension
      GASNETI_TRACE_PRINTF(D,("STRIDED_OPT: Null dimension removed"));
      opt_stridelevels--; // remove it
    } else {
      outdim->count = cnt;
      ptrdiff_t selfstride = selfstrides[d];
      ptrdiff_t peerstride = peerstrides[d];
      if (peerstride < 0) { // invert stride
        GASNETI_TRACE_PRINTF(D,("STRIDED_OPT: Stride inversion"));
        smd->addr[SMD_SELF] = (uint8_t*)smd->addr[SMD_SELF] + selfstride * (cnt-1);
        smd->addr[SMD_PEER] = (uint8_t*)smd->addr[SMD_PEER] + peerstride * (cnt-1);
        peerstride = -peerstride;
        selfstride = -selfstride;
      }
      outdim->stride[SMD_SELF] = selfstride;
      outdim->stride[SMD_PEER] = peerstride;
      is_sorted &= (peerstride >= last_peerstride);
      last_peerstride = peerstride;
      outdim++;
    }
  }
  smd->stridelevels = opt_stridelevels;

  if_pf (opt_stridelevels == 0) goto out_sop; // degenerate contiguity

  // SMD normalization pass 2
  // ------------------------
  // sort dimensions by ascending peer stride
  // use a simple N^2 in-place selection sort, expected to perform best for small stridelevels
  // TODO-EX: Replace with an NlogN library sort for larger stridelevels
  if (!is_sorted) {
    for (size_t i=0; i < opt_stridelevels-1; i++) {
      ptrdiff_t minstride = smd->dim[i].stride[SMD_PEER];
      size_t mindim = i;
      for (size_t j=i+1; j < opt_stridelevels; j++) { 
        ptrdiff_t const stride = smd->dim[j].stride[SMD_PEER];
        if (stride < minstride) { minstride = stride; mindim = j; }
      }
      if (mindim != i) { // swap
        gasneti_vis_smd_dim_t const tmp = smd->dim[i]; 
        smd->dim[i] = smd->dim[mindim];
        smd->dim[mindim] = tmp;
      }
    }
    GASNETI_TRACE_PRINTF(D,("STRIDED_OPT: Stride sort"));
  }

  // SMD normalization pass 3
  // ------------------------
  // Fold trailing dual-contiguous dimensions into elemsz
  {
    size_t opt_elemsz = elemsz;
    gasneti_vis_smd_dim_t *indim = &(smd->dim[0]);
    gasneti_vis_smd_dim_t *outdim = indim;
    size_t const in_stridelevels = opt_stridelevels;
    size_t d;
    for (d = 0; d < in_stridelevels; d++) {
      if ( indim->stride[SMD_SELF] == opt_elemsz &&
           indim->stride[SMD_PEER] == opt_elemsz) {
        GASNETI_TRACE_PRINTF(D,("STRIDED_OPT: Dualcontig dimension folded into elemsz"));
        opt_elemsz *= indim->count;
        opt_stridelevels--;
        indim++;
      } else break;
    }
    if (indim != outdim) {
      for ( ; d < in_stridelevels; d++) {
        *outdim++ = *indim++;
      }
      gasneti_assert(outdim == &(smd->dim[opt_stridelevels]));
      smd->elemsz = opt_elemsz;
      smd->stridelevels = opt_stridelevels;
    } else gasneti_assert(opt_stridelevels == in_stridelevels);
  }

  if_pf (opt_stridelevels == 0) goto out_sop; // degenerate contiguity

  // SMD normalization pass 4
  // ------------------------
  // Perform dimensional folding to combine trivial inner dimensions
  {
    gasneti_vis_smd_dim_t *lodim = &(smd->dim[0]);
    gasneti_vis_smd_dim_t *hidim = lodim + 1;
    size_t const in_stridelevels = opt_stridelevels;
    for (size_t d = in_stridelevels-1; d; d--) {
      size_t const locnt = lodim->count;
      if ( hidim->stride[SMD_SELF] == lodim->stride[SMD_SELF] * locnt &&
           hidim->stride[SMD_PEER] == lodim->stride[SMD_PEER] * locnt ) {
        GASNETI_TRACE_PRINTF(D,("STRIDED_OPT: Combined trivial inner dimension"));
        lodim->count = locnt * hidim->count;
        opt_stridelevels--;
      } else {
        lodim++;
        if (lodim != hidim) {
          *lodim = *hidim;
        }
      }
      hidim++;
    }
    gasneti_assert(lodim == &(smd->dim[opt_stridelevels-1]));
    if (opt_stridelevels != in_stridelevels) smd->stridelevels = opt_stridelevels;
  }

out_sop:
  #if GASNET_DEBUG || GASNET_TRACE  // trace and sanity check the resulting sop
  {
    gasneti_assert(opt_stridelevels == sop->smd.stridelevels);
    size_t const opt_elemsz = sop->smd.elemsz;
    ptrdiff_t * const opt_strides[2] = { sop->scratch, (ptrdiff_t*)sop->scratch + opt_stridelevels };
    size_t * const opt_count = (size_t*)(opt_strides[1] + opt_stridelevels);
    int change = (opt_elemsz != elemsz) || (opt_stridelevels != stridelevels) ||
                 (sop->smd.addr[SMD_SELF] != selfaddr) ||
                 (sop->smd.addr[SMD_PEER] != peeraddr);
    gasneti_vis_smd_dim_t const * dim = &(sop->smd.dim[0]);
    for (size_t d = 0; d < opt_stridelevels; d++) {
      opt_strides[SMD_PEER][d] = dim->stride[SMD_PEER];
      opt_strides[SMD_SELF][d] = dim->stride[SMD_SELF];
      opt_count[d] = dim->count;
      change |= (opt_strides[SMD_PEER][d] != peerstrides[d]);
      change |= (opt_strides[SMD_SELF][d] != selfstrides[d]);
      change |= (opt_count[d] != count[d]);
      dim++;
    }
    #if GASNET_TRACE
    if (change && GASNETI_TRACE_ENABLED(D)) { // trace the optimized metadata
      char *str = gasneti_malloc(gasneti_format_putsgets_bufsz(opt_stridelevels));
      int dstid = (isput?SMD_PEER:SMD_SELF);
      int srcid = (isput?SMD_SELF:SMD_PEER);
      gasneti_format_putsgets(str,NULL,(gex_Rank_t)-1,
                              sop->smd.addr[dstid],opt_strides[dstid],
                              sop->smd.addr[srcid],opt_strides[srcid],
                              opt_elemsz,opt_count,opt_stridelevels);
      GASNETI_TRACE_PRINTF(D,("%s: %s",(isput?"PUTS_OPT":"GETS_OPT"),str));
      gasneti_free(str);
    }
    #endif
    #if GASNET_DEBUG
    { // sanity check our strided metadata optimizations
      // data size
      size_t const opt_totalsz = gasnete_strided_datasize(opt_elemsz, opt_count, opt_stridelevels);
      size_t const totalsz = gasnete_strided_datasize(elemsz, count, stridelevels);
      gasneti_assert(opt_totalsz == totalsz);
      // self bounds
      void const *selfbase = selfaddr; 
      size_t const selflen = gasnete_strided_bounds(selfstrides, elemsz, count, stridelevels, &selfbase);
      void const *opt_selfbase; 
      size_t const opt_selflen = gasnete_smd_querybounds(&(sop->smd), SMD_SELF, &opt_selfbase);
      gasneti_assert(selflen == opt_selflen && selfbase == opt_selfbase);
      // peer bounds
      void const *peerbase = peeraddr; 
      size_t const peerlen = gasnete_strided_bounds(peerstrides, elemsz, count, stridelevels, &peerbase);
      void const *opt_peerbase; 
      size_t const opt_peerlen = gasnete_smd_querybounds(&(sop->smd), SMD_PEER, &opt_peerbase);
      gasneti_assert(peerlen == opt_peerlen && peerbase == opt_peerbase);
    }
    #endif
  }
  #endif // DEBUG || TRACE

  return sop;
}
/*---------------------------------------------------------------------------------*/
/* top-level gasnet_puts_* entry point */
#ifndef GASNETE_PUTS_OVERRIDE
extern gex_Event_t gasnete_puts(gasnete_synctype_t synctype,
                                   gex_TM_t tm, gex_Rank_t rank,
                                   void *dstaddr, const ptrdiff_t dststrides[],
                                   void *srcaddr, const ptrdiff_t srcstrides[],
                                   size_t elemsz, const size_t count[], size_t stridelevels,
                                   gex_Flags_t flags GASNETE_THREAD_FARG) {
  gasneti_assert(gasnete_vis_isinit);

  /* catch silly degenerate cases */
  gasneti_assert(elemsz > 0); // this degenerate case handled in public header
  gasneti_assert(stridelevels > 0); // this degenerate case handled in public header
  gasneti_strided_op_t * const sop = 
    gasnete_build_strided_op(1, srcaddr, srcstrides, dstaddr, dststrides, 
                             elemsz, count, stridelevels);
  if_pf (!sop) { // degenerate count[i] == 0, for some i
    GASNETI_TRACE_EVENT(C, PUTS_DEGENERATE);
    return GEX_EVENT_INVALID; 
  }

  gasneti_vis_smd_t const * const smd = &(sop->smd);
  #if GASNET_DEBUG // Bounds check - currently only handle SEG_BOUND
    if (!(flags & (GEX_FLAG_PEER_SEG_UNKNOWN|GEX_FLAG_PEER_SEG_SOME))) {
      const void *base; 
      size_t len = gasnete_smd_querybounds(smd, SMD_PEER, &base);
      gasneti_boundscheck(tm, rank, base, len);
    }
  #endif
  
  gex_Event_t result;
  void *peeraddr;
  if_pf (sop->smd.stridelevels == 0) {
    // folded to fully contiguous
    GASNETI_TRACE_EVENT(C, PUTS_DEGENERATE);
    GASNETE_PUT_DEGEN(result, synctype, tm, rank, smd->addr[SMD_PEER], smd->addr[SMD_SELF], smd->elemsz, flags);
  } else if ((peeraddr = GASNETI_SUPERNODE_LOCAL_ADDR_OR_NULL(rank, smd->addr[SMD_PEER]))) {
    // shared memory - use shared-memory bypass
    GASNETI_TRACE_EVENT(C, PUTS_NBRHD);
    gasnete_strided_memcpy(peeraddr, smd->addr[SMD_SELF], smd->stridelevels, smd->elemsz, smd->dim, SMD_SELF);
    result = GEX_EVENT_INVALID;
  } else {
    result = gasnete_puts_ref_indiv(sop, synctype, tm, rank, flags GASNETE_THREAD_PASS);
  }

  gasneti_free(sop);
  return result;
#if 0
  /* select algorithm */
  #ifndef GASNETE_PUTS_SELECTOR
    #if GASNETE_RANDOM_SELECTOR
      #define GASNETE_PUTS_SELECTOR(stats,synctype,dstnode,dstaddr,dststrides,srcaddr,srcstrides,count,stridelevels) do {                         \
        switch (rand() % 5) {                                                                                                                     \
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
          default: gasneti_unreachable();                                                                                                         \
        } } while (0)
    #else
      #define GASNETE_PUTS_SELECTOR(stats,synctype,dstnode,dstaddr,dststrides,srcaddr,srcstrides,count,stridelevels)       \
        GASNETE_PUTS_GATHER_SELECTOR(stats,synctype,dstnode,dstaddr,dststrides,srcaddr,srcstrides,count,stridelevels);     \
        GASNETE_PUTS_AMPIPELINE_SELECTOR(stats,synctype,dstnode,dstaddr,dststrides,srcaddr,srcstrides,count,stridelevels); \
        return gasnete_puts_ref_indiv(stats,synctype,dstnode,dstaddr,dststrides,srcaddr,srcstrides,count,stridelevels GASNETE_THREAD_PASS)
    #endif
  #endif
  GASNETE_PUTS_SELECTOR(&stats,synctype,dstnode,dstaddr,dststrides,srcaddr,srcstrides,count,stridelevels);
  gasneti_unreachable();
#endif
}
#endif
/* top-level gasnet_gets_* entry point */
#ifndef GASNETE_GETS_OVERRIDE
extern gex_Event_t gasnete_gets(gasnete_synctype_t synctype,
                                   gex_TM_t tm,
                                   void *dstaddr, const ptrdiff_t dststrides[],
                                   gex_Rank_t rank,
                                   void *srcaddr, const ptrdiff_t srcstrides[],
                                   size_t elemsz, const size_t count[], size_t stridelevels,
                                   gex_Flags_t flags GASNETE_THREAD_FARG) {
  gasneti_assert(gasnete_vis_isinit);

  /* catch silly degenerate cases */
  gasneti_assert(elemsz > 0); // this degenerate case handled in public header
  gasneti_assert(stridelevels > 0); // this degenerate case handled in public header
  gasneti_strided_op_t * const sop = 
    gasnete_build_strided_op(0, dstaddr, dststrides, srcaddr, srcstrides, 
                             elemsz, count, stridelevels);
  if_pf (!sop) { // degenerate count[i] == 0, for some i
    GASNETI_TRACE_EVENT(C, GETS_DEGENERATE);
    return GEX_EVENT_INVALID; 
  }

  gasneti_vis_smd_t const * const smd = &(sop->smd);
  #if GASNET_DEBUG // Bounds check - currently only handle SEG_BOUND
    if (!(flags & (GEX_FLAG_PEER_SEG_UNKNOWN|GEX_FLAG_PEER_SEG_SOME))) {
      const void *base;
      size_t len = gasnete_smd_querybounds(smd, SMD_PEER, &base);
      gasneti_boundscheck(tm, rank, base, len);
    }
  #endif

  gex_Event_t result;
  void *peeraddr;
  if_pf (sop->smd.stridelevels == 0) {
    // folded to fully contiguous
    GASNETI_TRACE_EVENT(C, GETS_DEGENERATE);
    GASNETE_GET_DEGEN(result, synctype, tm, smd->addr[SMD_SELF], rank, smd->addr[SMD_PEER], smd->elemsz, flags);
  } else if ((peeraddr = GASNETI_SUPERNODE_LOCAL_ADDR_OR_NULL(rank, smd->addr[SMD_PEER]))) {
    // shared memory - use shared-memory bypass
    GASNETI_TRACE_EVENT(C, GETS_NBRHD);
    gasnete_strided_memcpy(smd->addr[SMD_SELF], peeraddr, smd->stridelevels, smd->elemsz, smd->dim, SMD_PEER);
    result = GEX_EVENT_INVALID;
  } else {
    result = gasnete_gets_ref_indiv(sop, synctype, tm, rank, flags GASNETE_THREAD_PASS);
  }

  gasneti_free(sop);
  return result;
#if 0
  /* select algorithm */
  #ifndef GASNETE_GETS_SELECTOR
    #if GASNETE_RANDOM_SELECTOR
      #define GASNETE_GETS_SELECTOR(stats,synctype,dstaddr,dststrides,srcnode,srcaddr,srcstrides,count,stridelevels) do {                         \
        switch (rand() % 5) {                                                                                                                     \
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
          default: gasneti_unreachable();                                                                                                         \
        } } while (0)
    #else 
      #define GASNETE_GETS_SELECTOR(stats,synctype,dstaddr,dststrides,srcnode,srcaddr,srcstrides,count,stridelevels)       \
        GASNETE_GETS_SCATTER_SELECTOR(stats,synctype,dstaddr,dststrides,srcnode,srcaddr,srcstrides,count,stridelevels);    \
        GASNETE_GETS_AMPIPELINE_SELECTOR(stats,synctype,dstaddr,dststrides,srcnode,srcaddr,srcstrides,count,stridelevels); \
        return gasnete_gets_ref_indiv(stats,synctype,dstaddr,dststrides,srcnode,srcaddr,srcstrides,count,stridelevels GASNETE_THREAD_PASS)
    #endif
  #endif
  GASNETE_GETS_SELECTOR(&stats,synctype,dstaddr,dststrides,srcnode,srcaddr,srcstrides,count,stridelevels);
  gasneti_unreachable();
#endif
}
#endif
/*---------------------------------------------------------------------------------*/


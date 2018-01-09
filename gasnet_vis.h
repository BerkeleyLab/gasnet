/*   $Source: bitbucket.org:berkeleylab/gasnet.git/gasnet_vis.h $
 * Description: GASNet Extended API Vector, Indexed & Strided declarations
 * Copyright 2002, Dan Bonachea <bonachea@cs.berkeley.edu>
 * Terms of use are as specified in license.txt
 */

#ifndef _GASNET_VIS_H
#define _GASNET_VIS_H

#include <gasnetex.h>

GASNETI_BEGIN_EXTERNC
GASNETI_BEGIN_NOWARN

/*---------------------------------------------------------------------------------*/
GASNETI_INLINE(gasnete_memveclist_totalsz)
uintptr_t gasnete_memveclist_totalsz(size_t _count, gex_Memvec_t const *_list) {
  uintptr_t _retval = 0;
  for (size_t _i = 0; _i < _count; _i++) {
    _retval += _list[_i].gex_len;
  }
  return _retval;
}

GASNETI_INLINE(gasnete_memveclist_stats)
gasneti_memveclist_stats_t gasnete_memveclist_stats(size_t _count, gex_Memvec_t const *_list) {
  gasneti_memveclist_stats_t _retval;
  size_t _minsz = (size_t)-1, _maxsz = 0;
  uintptr_t _totalsz = 0;
  char *_minaddr = (char *)(intptr_t)(uintptr_t)-1;
  char *_maxaddr = (char *)0;
  for (size_t _i = 0; _i < _count; _i++) {
    size_t const _len = _list[_i].gex_len;
    char * const _addr = (char *)_list[_i].gex_addr;
    char * const _end = _addr + _len - 1;
    if (_len > 0) {
      if (_len < _minsz) _minsz = _len;
      if (_len > _maxsz) _maxsz = _len;
      if (_addr < _minaddr) _minaddr = _addr;
      if (_end > _maxaddr) _maxaddr = _end;
      _totalsz += _len;
    }
  }
  _retval._minsz = _minsz;
  _retval._maxsz = _maxsz;
  _retval._minaddr = _minaddr;
  _retval._maxaddr = _maxaddr;
  _retval._totalsz = _totalsz;
  gasneti_assert(_totalsz == gasnete_memveclist_totalsz(_count, _list));
  return _retval;
}
/*---------------------------------------------------------------------------------*/

GASNETI_INLINE(gasnete_addrlist_stats)
gasneti_addrlist_stats_t gasnete_addrlist_stats(size_t _count, void * const *_list, size_t _len) {
  gasneti_addrlist_stats_t _retval;
  char *_minaddr = (char *)(intptr_t)(uintptr_t)-1;
  char *_maxaddr = (char *)0;
#if PLATFORM_COMPILER_GNU && PLATFORM_COMPILER_VERSION_EQ(4,5,1)
  ssize_t _i; /* size_t triggers an ICE exclusive to gcc-4.5.1, but this gets a warning instead */
#else
  size_t _i;
#endif
  for (_i = 0; _i < _count; _i++) {
    char * const _addr = (char *)_list[_i];
    char * const _end = _addr + _len - 1;
    if (_addr < _minaddr) _minaddr = _addr;
    if (_end > _maxaddr) _maxaddr = _end;
  }
  _retval._minaddr = _minaddr;
  _retval._maxaddr = _maxaddr;
  return _retval;
}

/*---------------------------------------------------------------------------------*/

/* returns non-zero iff the specified strided region is empty */
GASNETI_INLINE(gasnete_strided_empty)
int gasnete_strided_empty(size_t _elemsz, size_t const *_count, size_t _stridelevels) {
  if_pf (!_elemsz) return 1;
  for (size_t _i = 0; _i < _stridelevels; _i++) {
    if_pf (_count[_i] == 0) return 1;
  }
  return 0;
}

/* returns the number of top-level dimensions with a count of 1 */
GASNETI_INLINE(gasnete_strided_nulldims)
size_t gasnete_strided_nulldims(size_t const *_count, size_t _stridelevels) {
  for (ssize_t _i = _stridelevels-1; _i >= 0; _i--) {
    if_pt (_count[_i] != 1) return _stridelevels-(_i+1);
  }
  return _stridelevels;
}

/* returns the length of the bounding box containing all the data */
GASNETI_INLINE(gasnete_strided_extent)
size_t gasnete_strided_extent(ptrdiff_t const *_strides, size_t _elemsz, size_t const *_count, size_t _stridelevels) {
  /* Calculating the bounding rectangle for a strided section is subtle.
     The obvious choice:
       count[stridelevels]*strides[stridelevels-1]
     is an upper bound which is too large in many cases
     The true exact length for non-transpositional is:
       elemsz + SUM(i=[0..stridelevels-1] | (count[i]-1)*strides[i])
   */
  size_t _sz = _elemsz;
  if_pf (_sz == 0) return 0;
  for (size_t _i = 0; _i < _stridelevels; _i++) {
    size_t const _cnt = _count[_i];
    if_pf (_cnt == 0) return 0;
    _sz += (_cnt-1)*(size_t)_strides[_i]; // TODO-EX: trans
  }
  return _sz;
}

/* returns the total bytes of data in the transfer */
GASNETI_INLINE(gasnete_strided_datasize)
size_t gasnete_strided_datasize(size_t _elemsz, size_t const *_count, size_t _stridelevels) {
  size_t _sz = _elemsz;
  for (size_t _i = 0; _i < _stridelevels; _i++) {
    size_t const _cnt = _count[_i];
    if_pf (_sz == 0) return 0;
    _sz *= _cnt;
  }
  return _sz;
}

/* returns the size of the contiguous segments in the transfer
 */
GASNETI_INLINE(gasnete_strided_contigsz)
size_t gasnete_strided_contigsz(ptrdiff_t const *_strides, size_t _elemsz, size_t const *_count, size_t _stridelevels) {
  /* querying the contiguity of an empty region probably signifies a bug */
  gasneti_assert(!gasnete_strided_empty(_elemsz,_count,_stridelevels)); 

  size_t _limit = _stridelevels;
  for ( ; _limit > 0; _limit--) // ignore null dims
    if_pt (_count[_limit-1] != 1) break;
  if_pf (_limit == 0) return _elemsz; /* trivially fully contiguous */

  size_t _sz = _elemsz;
  for (size_t _i = 0; _i < _limit; _i++) {
    if (_strides[_i] > (ptrdiff_t)_sz) return _sz;
    gasneti_assert(_strides[_i] == (ptrdiff_t)_sz);  // TODO-EX: trans
    _sz *= _count[_i];
  }
  return _sz;
}

/* returns the highest stridelevel with data contiguity 
   eg. returns zero if only the bottom level is contiguous,
   and stridelevels if the entire region is contiguous
 */
GASNETI_INLINE(gasnete_strided_contiguity)
size_t gasnete_strided_contiguity(ptrdiff_t const *_strides, size_t _elemsz, size_t const *_count, size_t _stridelevels) {
  /* querying the contiguity of an empty region probably signifies a bug */
  gasneti_assert(!gasnete_strided_empty(_elemsz,_count,_stridelevels)); 

  size_t _limit = _stridelevels;
  for ( ; _limit > 0; _limit--) // ignore null dims
    if_pt (_count[_limit-1] != 1) break;
  if_pf (_limit == 0) return _stridelevels; /* trivially fully contiguous */

  size_t _sz = _elemsz;
  for (size_t _i = 0; _i < _limit; _i++) {
    if (_strides[_i] > (ptrdiff_t)_sz) return _i;
    gasneti_assert(_strides[_i] == (ptrdiff_t)_sz);  // TODO-EX: trans
    _sz *= _count[_i];
  }
  return _stridelevels;
}

/* returns the highest stridelevel with data contiguity in both regions
   eg. returns zero if only the bottom level is contiguous in one or both regions,
   and stridelevels if the both regions are entirely contiguous
   this can computed more efficiently than checking contiguity of each separately
 */
GASNETI_INLINE(gasnete_strided_dualcontiguity)
size_t gasnete_strided_dualcontiguity(ptrdiff_t const *_strides1, ptrdiff_t const *_strides2, 
                                      size_t _elemsz, size_t const *_count, size_t _stridelevels) {
  /* querying the contiguity of an empty region probably signifies a bug */
  gasneti_assert(!gasnete_strided_empty(_elemsz,_count,_stridelevels)); 

  size_t _limit = _stridelevels;
  for ( ; _limit > 0; _limit--) // ignore null dims
    if_pt (_count[_limit-1] != 1) break;
  if_pf (_limit == 0) return _stridelevels; /* trivially fully contiguous */

  size_t _sz = _elemsz<<1;
  for (size_t _i = 0; _i < _limit; _i++) {
    size_t const _temp = (_strides1[_i]+_strides2[_i]);  // TODO-EX: trans
    if (_temp > _sz) return _i;
    gasneti_assert(_temp == _sz);
    _sz *= _count[_i];
  }
  return _stridelevels;
}

/* returns the size of the contiguous region at the dualcontiguity level */
GASNETI_INLINE(gasnete_strided_dualcontigsz)
size_t gasnete_strided_dualcontigsz(ptrdiff_t const *_strides1, ptrdiff_t const *_strides2, 
                                        size_t _elemsz, size_t const *_count, size_t _stridelevels) {
  /* querying the contiguity of an empty region probably signifies a bug */
  gasneti_assert(!gasnete_strided_empty(_elemsz,_count,_stridelevels)); 

  size_t _limit = _stridelevels;
  for ( ; _limit > 0; _limit--) // ignore null dims
    if_pt (_count[_limit-1] != 1) break;
  if_pf (_limit == 0) return _elemsz; /* trivially fully contiguous */

  // TODO-EX: this algorithm might cause overflow in 32-bit
  size_t _sz = _elemsz<<1;
  for (size_t _i = 0; _i < _limit; _i++) {
    ptrdiff_t const _temp = (_strides1[_i]+_strides2[_i]);  // TODO-EX: trans
    if (_temp > (ptrdiff_t)_sz) return _sz>>1;
    gasneti_assert(_temp == (ptrdiff_t)_sz);
    _sz *= _count[_i];
  }
  return _sz>>1;
}

/* returns the number of contiguous segments in the transfer */
GASNETI_INLINE(gasnete_strided_segments)
size_t gasnete_strided_segments(ptrdiff_t const *_strides, size_t _elemsz, 
                                size_t const *_count, size_t _stridelevels) {
  size_t _contiglevel = gasnete_strided_contiguity(_strides, _elemsz, _count, _stridelevels);
  size_t _cnt = 1;
  for (size_t _i = _contiglevel; _i < _stridelevels; _i++) {
    _cnt *= _count[_i];
  }
  return _cnt;
}

typedef struct {  // TODO-EX: trans
  size_t _srcextent; /* the length of the bounding box containing all the src data */
  size_t _dstextent; /* the length of the bounding box containing all the dst data */

  size_t _totalsz;   /* the total bytes of data in the transfer */

  size_t _nulldims;  /* number of top-level dimensions with a count of 1 -
                       these dimensions can be ignored for most purposes 
                       (stridelevels means all counts are one)
                     */

  size_t _srccontiguity; /* the highest stridelevel with data contiguity in the src region
                           eg. zero if only the bottom level is contiguous,
                           and stridelevels if the entire region is contiguous
                         */
  size_t _dstcontiguity; /* the highest stridelevel with data contiguity in the dst region
                           eg. zero if only the bottom level is contiguous,
                           and stridelevels if the entire region is contiguous
                         */
  size_t _dualcontiguity; /* MIN(srccontiguity, dstcontiguity) */

  size_t _srcsegments;   /* number of contiguous segments in the src region */
  size_t _dstsegments;   /* number of contiguous segments in the dst region */

  size_t _srccontigsz;   /* size of the contiguous segments in the src region */
  size_t _dstcontigsz;   /* size of the contiguous segments in the dst region */
  size_t _dualcontigsz;   /* MIN(srccontigsz,dstcontigsz) */

} gasnete_strided_stats_t;

/* calculate a number of useful shape properties over the given regions */
GASNETI_INLINE(gasnete_strided_stats)
void gasnete_strided_stats(gasnete_strided_stats_t *_result, 
                           ptrdiff_t const *_dststrides, ptrdiff_t const *_srcstrides, 
                           size_t _elemsz, size_t const *_count, size_t _stridelevels) {
  if_pf (_elemsz == 0) {
    memset(_result, 0, sizeof(*_result));
    return;
  } else if_pf (_stridelevels == 0) {
    gasneti_assert(!gasnete_strided_empty(_elemsz, _count, _stridelevels));
    _result->_srcextent = _elemsz;
    _result->_dstextent = _elemsz;
    _result->_totalsz = _elemsz;
    _result->_nulldims = 0;
    _result->_srccontiguity = 0;
    _result->_dstcontiguity = 0;
    _result->_dualcontiguity = 0;
    _result->_srcsegments = 1;
    _result->_dstsegments = 1;
    _result->_srccontigsz = _elemsz;
    _result->_dstcontigsz = _elemsz;
    _result->_dualcontigsz = _elemsz;
    return;
  } else { // TODO-EX: trans
    int _srcbreak = 0;
    int _dstbreak = 0;
    size_t _srcextent = _elemsz;
    size_t _dstextent = _elemsz;
    size_t _srcsegments = 1;
    size_t _dstsegments = 1;
    size_t _srccontigsz = _elemsz;
    size_t _dstcontigsz = _elemsz;
    size_t _limit;
    for (_limit = _stridelevels; _limit > 0; _limit--) // ignore null dims
      if_pt (_count[_limit-1] != 1) break;
    _result->_nulldims = _stridelevels - _limit;
    _result->_srccontiguity = _stridelevels;
    _result->_dstcontiguity = _stridelevels;

    for (size_t _i=0; _i < _limit; _i++) {
      size_t const _cnt = _count[_i];
      ptrdiff_t const _srcstride = _srcstrides[_i];
      ptrdiff_t const _dststride = _dststrides[_i];
      _srcextent += (_cnt-1)*_srcstride;
      _dstextent += (_cnt-1)*_dststride;

      if (_srcbreak) _srcsegments *= _cnt;
      else if (_srcstride > (ptrdiff_t)_srccontigsz) {
          _srcbreak = 1;
          _result->_srccontiguity = _i;
          _srcsegments *= _cnt;
      } else _srccontigsz *= _cnt;

      if (_dstbreak) _dstsegments *= _cnt;
      else if (_dststride > (ptrdiff_t)_dstcontigsz) {
          _dstbreak = 1;
          _result->_dstcontiguity = _i;
          _dstsegments *= _cnt;
      } else _dstcontigsz *= _cnt;
    }

    _result->_totalsz = _srcsegments*_srccontigsz;
    if_pf (_result->_totalsz == 0) { /* empty xfer */
      gasneti_assert(gasnete_strided_empty(_elemsz, _count, _stridelevels));
      gasneti_assert(gasnete_strided_datasize(_elemsz, _count, _stridelevels) == 0);
      _result->_srcextent = 0;
      _result->_dstextent = 0;
      _result->_nulldims = 0;
      _result->_srccontiguity = 0;
      _result->_dstcontiguity = 0;
      _result->_dualcontiguity = 0;
      _result->_srcsegments = 0;
      _result->_dstsegments = 0;
      _result->_srccontigsz = 0;
      _result->_dstcontigsz = 0;
      return;
    }
    _result->_srccontigsz = _srccontigsz;
    _result->_dstcontigsz = _dstcontigsz;
    _result->_srcsegments = _srcsegments;
    _result->_dstsegments = _dstsegments;
    _result->_srcextent = _srcextent;
    _result->_dstextent = _dstextent;
    _result->_dualcontiguity = MIN(_result->_srccontiguity, _result->_dstcontiguity);
    _result->_dualcontigsz = MIN(_result->_srccontigsz, _result->_dstcontigsz);
    /* sanity check */
    gasneti_assert(!gasnete_strided_empty(_elemsz, _count, _stridelevels));
    gasneti_assert(_result->_srcextent == gasnete_strided_extent(_srcstrides, _elemsz, _count, _stridelevels));
    gasneti_assert(_result->_dstextent == gasnete_strided_extent(_dststrides, _elemsz, _count, _stridelevels));
    gasneti_assert(_result->_totalsz == gasnete_strided_datasize(_elemsz, _count, _stridelevels));
    gasneti_assert(_result->_nulldims == gasnete_strided_nulldims(_count, _stridelevels));
    gasneti_assert(_result->_srccontiguity == gasnete_strided_contiguity(_srcstrides, _elemsz, _count, _stridelevels));
    gasneti_assert(_result->_dstcontiguity == gasnete_strided_contiguity(_dststrides, _elemsz, _count, _stridelevels));
    gasneti_assert(_result->_dualcontiguity == gasnete_strided_dualcontiguity(_srcstrides, _dststrides, _elemsz, _count, _stridelevels));
    gasneti_assert(_result->_srcsegments == gasnete_strided_segments(_srcstrides, _elemsz, _count, _stridelevels));
    gasneti_assert(_result->_dstsegments == gasnete_strided_segments(_dststrides, _elemsz, _count, _stridelevels));
    gasneti_assert(_result->_srccontigsz == gasnete_strided_contigsz(_srcstrides, _elemsz, _count, _stridelevels));
    gasneti_assert(_result->_dstcontigsz == gasnete_strided_contigsz(_dststrides, _elemsz, _count, _stridelevels));
    gasneti_assert(_result->_dualcontigsz == gasnete_strided_dualcontigsz(_srcstrides, _dststrides, _elemsz, _count, _stridelevels));
  }
  return;
}

#if GASNET_NDEBUG
  #define gasnete_boundscheck_memveclist(tm, node, count, list)
  #define gasnete_memveclist_checksizematch(dstcount, dstlist, srccount, srclist)
  #define gasnete_boundscheck_addrlist(tm, node, count, list, len) 
  #define gasnete_addrlist_checksizematch(dstcount, dstlen, srccount, srclen)
  #define gasnete_boundscheck_strided(tm, node, addr, strides, elemsz, count, stridelevels)
  #define gasnete_check_strides(dststrides, srcstrides, elemsz, count, stridelevels)
  #define gasnete_check_stridesNT(dststrides, srcstrides, count, stridelevels)
#else
  #define gasnete_boundscheck_memveclist(tm, rank, count, list) do { \
    gex_TM_t __tm = (tm);                                            \
    gex_Rank_t __node = (rank);                                      \
    size_t __count = (count);                                        \
    gex_Memvec_t const * const __list = (list);                      \
    for (size_t _i=0; _i < __count; _i++) {                          \
      if (__list[_i].gex_len > 0)                                    \
        gasneti_boundscheck(__tm, __node,                            \
                            __list[_i].gex_addr, __list[_i].gex_len);\
    }                                                                \
  } while (0)

  #define gasnete_memveclist_checksizematch(dstcount, dstlist, srccount, srclist) do {         \
    gasneti_memveclist_stats_t _dststats = gasnete_memveclist_stats((dstcount), (dstlist));    \
    gasneti_memveclist_stats_t _srcstats = gasnete_memveclist_stats((srccount), (srclist));    \
    if_pf (_dststats._totalsz != _srcstats._totalsz) {                                         \
      char * _dstlist_str =                                                                    \
             (char *)gasneti_extern_malloc(gasneti_format_memveclist_bufsz(dstcount));         \
      char * _srclist_str =                                                                    \
             (char *)gasneti_extern_malloc(gasneti_format_memveclist_bufsz(srccount));         \
      gasneti_format_memveclist(_dstlist_str, (dstcount), (dstlist));                          \
      gasneti_format_memveclist(_srclist_str, (srccount), (srclist));                          \
      gasneti_fatalerror("Source and destination memvec lists disagree on total size at %s:\n" \
                         "  srclist: %s\n"                                                     \
                         "  dstlist: %s\n",                                                    \
                         gasneti_current_loc, _dstlist_str, _srclist_str);                     \
      /* gasneti_extern_free(_dstlist_str); -- dead code */                                    \
      /* gasneti_extern_free(_srclist_str); -- dead code */                                    \
    }                                                                                          \
    if_pf (_dststats._totalsz != 0 &&                                                          \
      ((uintptr_t)_dststats._minaddr) + _dststats._totalsz - 1 > ((uintptr_t)_dststats._maxaddr)) { \
      char * _dstlist_str =                                                                    \
             (char *)gasneti_extern_malloc(gasneti_format_memveclist_bufsz(dstcount));         \
      gasneti_format_memveclist(_dstlist_str, (dstcount), (dstlist));                          \
      gasneti_fatalerror("Destination memvec list has overlapping elements at %s:\n"           \
                         "  dstlist: %s\n"                                                     \
                         "(note this test is currently conservative "                          \
                         "and may fail to detect some illegal cases)",                         \
                         gasneti_current_loc, _dstlist_str);                                   \
      /* gasneti_extern_free(_dstlist_str); -- dead code */                                    \
    }                                                                                          \
  } while (0)

  #define gasnete_boundscheck_addrlist(tm, rank, count, list, len) do { \
    gex_TM_t __tm = (tm);                                           \
    gex_Rank_t __node = (rank);                                     \
    size_t __count = (count);                                       \
    void * const * const __list = (list);                           \
    size_t __len = (len);                                           \
    if_pt (__len > 0) {                                             \
      for (size_t __i=0; __i < __count; __i++) {                    \
        gasneti_boundscheck(__tm, __node, __list[__i], __len);      \
      }                                                             \
    }                                                               \
  } while (0)

  #define gasnete_addrlist_checksizematch(dstcount, dstlen, srccount, srclen) do {        \
    if_pf ((dstlen) == 0) gasneti_fatalerror("dstlen == 0 at: %s\n",gasneti_current_loc); \
    if_pf ((srclen) == 0) gasneti_fatalerror("srclen == 0 at: %s\n",gasneti_current_loc); \
    if_pf ((dstcount)*(dstlen) != (srccount)*(srclen)) {                                  \
      gasneti_fatalerror("Total data size mismatch at: %s\n"                              \
       "dstcount(%" PRIuSZ ")*dstlen(%" PRIuSZ ") != srccount(%" PRIuSZ ")*srclen(%" PRIuSZ ")", \
                         gasneti_current_loc,                                             \
                         dstcount, dstlen, srccount, srclen);                             \
    }                                                                                     \
  } while (0)

  #define gasnete_boundscheck_strided(tm, node, addr, strides, elemsz, count, stridelevels) do { \
    const ptrdiff_t * const __strides = (strides);                                               \
    const size_t * const __count = (count);                                                      \
    const size_t __elemsz = (elemsz);                                                            \
    size_t __stridelevels = (stridelevels);                                                      \
    if_pt (!gasnete_strided_empty(__elemsz, __count, __stridelevels)) {                          \
      gasneti_boundscheck((tm), (node), (addr),                                                  \
        gasnete_strided_extent(__strides,__elemsz,__count,__stridelevels));                      \
    }                                                                                            \
  } while (0)

  #define gasnete_check_strides(dststrides, srcstrides, elemsz, count, stridelevels) do {               \
    const ptrdiff_t * const __dststrides = (dststrides);                                                \
    const ptrdiff_t * const __srcstrides = (srcstrides);                                                \
    const size_t __elemsz = (elemsz);                                                                   \
    const size_t * const __count = (count);                                                             \
    const size_t __stridelevels = (stridelevels);                                                       \
    if_pt (!gasnete_strided_empty(__elemsz, __count, __stridelevels)) {  /* TODO-EX: trans */           \
      for (size_t _i = 0; _i < __stridelevels; _i++) {                                                  \
        if_pf (__srcstrides[_i] <= 0 || __dststrides[_i] <= 0)                                          \
          gasneti_fatalerror("stride array elements must be positive in this release (at %s)",          \
                             gasneti_current_loc);                                                      \
      }                                                                                                 \
      if_pf (__stridelevels > 0 && __dststrides[0] < (ptrdiff_t)__elemsz)                               \
          gasneti_fatalerror("dststrides[0](%" PRIdPD ") < elemsz(%" PRIuSZ ") at: %s",                 \
                        __dststrides[0],__elemsz, gasneti_current_loc);                                 \
      if_pf (__stridelevels > 0 && __srcstrides[0] < (ptrdiff_t)__elemsz)                               \
          gasneti_fatalerror("srcstrides[0](%" PRIdPD ") < elemsz(%" PRIuSZ ") at: %s",                 \
                        __srcstrides[0],__elemsz, gasneti_current_loc);                                 \
      for (size_t _i = 2; _i < __stridelevels; _i++) {                                                  \
        if_pf (__dststrides[_i] < (ptrdiff_t)(__count[_i-1] * __dststrides[_i-1]))                      \
          gasneti_fatalerror("dststrides[%" PRIuSZ "](%" PRIdPD ") < "                                  \
                  "(count[%" PRIuSZ "](%" PRIuSZ ") * dststrides[%" PRIuSZ "](%" PRIdPD ")) at: %s",    \
                     _i,__dststrides[_i], _i-1,__count[_i]-1, _i-1,__dststrides[_i-1], gasneti_current_loc); \
        if_pf (__srcstrides[_i] < (ptrdiff_t)(__count[_i-1] * __srcstrides[_i-1]))                      \
          gasneti_fatalerror("srcstrides[%" PRIuSZ "](%" PRIdPD ") < "                                  \
                  "(count[%" PRIuSZ "](%" PRIuSZ ") * srcstrides[%" PRIuSZ "](%" PRIdPD ")) at: %s",    \
                     _i,__srcstrides[_i], _i-1,__count[_i]-1, _i-1,__srcstrides[_i-1], gasneti_current_loc); \
      }                                                                                                 \
    }                                                                                                   \
  } while (0)

  // g2ex check for non-transpositional strides on LEGACY-format metadata
  #define gasnete_check_stridesNT(dststrides, srcstrides, count, stridelevels) do {                     \
    const size_t * const __dststrides = (dststrides);                                                   \
    const size_t * const __srcstrides = (srcstrides);                                                   \
    const size_t * const __count = (count);                                                             \
    const size_t __stridelevels = (stridelevels);                                                       \
    if_pt (!gasnete_strided_empty(__count[0], __count+1, __stridelevels)) {                             \
      if_pf (__stridelevels > 0 && __dststrides[0] < __count[0])                                        \
          gasneti_fatalerror("dststrides[0](%" PRIuSZ ") < count[0](%" PRIuSZ ") at: %s",               \
                        __dststrides[0],__count[0], gasneti_current_loc);                               \
      if_pf (__stridelevels > 0 && __srcstrides[0] < __count[0])                                        \
          gasneti_fatalerror("srcstrides[0](%" PRIuSZ ") < count[0](%" PRIuSZ ") at: %s",               \
                        __srcstrides[0],__count[0], gasneti_current_loc);                               \
      for (size_t _i = 1; _i < __stridelevels; _i++) {                                                  \
        if_pf (__dststrides[_i] < (__count[_i] * __dststrides[_i-1]))                                   \
          gasneti_fatalerror("dststrides[%" PRIuSZ "](%" PRIuSZ ") < "                                  \
                  "(count[%" PRIuSZ "](%" PRIuSZ ") * dststrides[%" PRIuSZ "](%" PRIuSZ ")) at: %s",    \
                     _i,__dststrides[_i], _i,__count[_i], _i-1,__dststrides[_i-1], gasneti_current_loc); \
        if_pf (__srcstrides[_i] < (__count[_i] * __srcstrides[_i-1]))                                   \
          gasneti_fatalerror("srcstrides[%" PRIuSZ "](%" PRIuSZ ") < "                                  \
                  "(count[%" PRIuSZ "](%" PRIuSZ ") * srcstrides[%" PRIuSZ "](%" PRIuSZ ")) at: %s",    \
                     _i,__srcstrides[_i], _i,__count[_i], _i-1,__srcstrides[_i-1], gasneti_current_loc); \
      }                                                                                                 \
    }                                                                                                   \
  } while (0)

#endif

typedef enum _gasnete_synctype_t {
  gasnete_synctype_b,
  gasnete_synctype_nb,
  gasnete_synctype_nbi
} gasnete_synctype_t;

/*---------------------------------------------------------------------------------*/
/* Vector */
#ifndef gasnete_putv
  extern gex_Event_t gasnete_putv(
        gasnete_synctype_t _synctype,
        gex_TM_t _tm, gex_Rank_t _dstrank,
        size_t _dstcount, gex_Memvec_t const _dstlist[],
        size_t _srccount, gex_Memvec_t const _srclist[],
        gex_Flags_t _flags GASNETE_THREAD_FARG);
#endif
#ifndef gasnete_getv
  extern gex_Event_t gasnete_getv(
        gasnete_synctype_t _synctype,
        gex_TM_t _tm,
        size_t _dstcount, gex_Memvec_t const _dstlist[],
        gex_Rank_t _srcrank,
        size_t _srccount, gex_Memvec_t const _srclist[],
        gex_Flags_t _flags GASNETE_THREAD_FARG);
#endif

#if 1 // blocking interfaces removed in EX?
GASNETI_INLINE(_gex_VIS_VectorPutBlocking)
int _gex_VIS_VectorPutBlocking(
        gex_TM_t _tm, gex_Rank_t _dstrank,
        size_t _dstcount, gex_Memvec_t const _dstlist[],
        size_t _srccount, gex_Memvec_t const _srclist[],
        gex_Flags_t _flags GASNETE_THREAD_FARG) {
  gasnete_boundscheck_memveclist(_tm, _dstrank, _dstcount, _dstlist);
  gasnete_memveclist_checksizematch(_dstcount, _dstlist, _srccount, _srclist);
  GASNETI_TRACE_PUTV(PUTV_BULK,_dstrank,_dstcount,_dstlist,_srccount,_srclist);
  return gasnete_putv(gasnete_synctype_b,_tm,_dstrank,_dstcount,_dstlist,_srccount,_srclist,_flags GASNETE_THREAD_PASS) == GEX_EVENT_NO_OP;
}
#define gex_VIS_VectorPutBlocking(tm,dstrank,dstcount,dstlist,srccount,srclist,flags) \
       _gex_VIS_VectorPutBlocking(tm,dstrank,dstcount,dstlist,srccount,srclist,flags GASNETE_THREAD_GET)

GASNETI_INLINE(_gex_VIS_VectorGetBlocking)
int _gex_VIS_VectorGetBlocking(
        gex_TM_t _tm,
        size_t _dstcount, gex_Memvec_t const _dstlist[],
        gex_Rank_t _srcrank,
        size_t _srccount, gex_Memvec_t const _srclist[],
        gex_Flags_t _flags GASNETE_THREAD_FARG) {
  gasnete_boundscheck_memveclist(_tm, _srcrank, _srccount, _srclist);
  gasnete_memveclist_checksizematch(_dstcount, _dstlist, _srccount, _srclist);
  GASNETI_TRACE_GETV(GETV_BULK,_srcrank,_dstcount,_dstlist,_srccount,_srclist);
  return gasnete_getv(gasnete_synctype_b,_tm,_dstcount,_dstlist,_srcrank,_srccount,_srclist,_flags GASNETE_THREAD_PASS) == GEX_EVENT_NO_OP;
}
#define gex_VIS_VectorGetBlocking(tm,dstcount,dstlist,srcrank,srccount,srclist,flags) \
       _gex_VIS_VectorGetBlocking(tm,dstcount,dstlist,srcrank,srccount,srclist,flags GASNETE_THREAD_GET)
#endif


GASNETI_INLINE(_gex_VIS_VectorPutNB) GASNETI_WARN_UNUSED_RESULT
gex_Event_t _gex_VIS_VectorPutNB(
        gex_TM_t _tm, gex_Rank_t _dstrank,
        size_t _dstcount, gex_Memvec_t const _dstlist[],
        size_t _srccount, gex_Memvec_t const _srclist[],
        gex_Flags_t _flags GASNETE_THREAD_FARG) {
  gasnete_boundscheck_memveclist(_tm, _dstrank, _dstcount, _dstlist);
  gasnete_memveclist_checksizematch(_dstcount, _dstlist, _srccount, _srclist);
  GASNETI_TRACE_PUTV(PUTV_NB_BULK,_dstrank,_dstcount,_dstlist,_srccount,_srclist);
  return gasnete_putv(gasnete_synctype_nb,_tm,_dstrank,_dstcount,_dstlist,_srccount,_srclist,_flags GASNETE_THREAD_PASS);
}
#define gex_VIS_VectorPutNB(tm,dstrank,dstcount,dstlist,srccount,srclist,flags) \
       _gex_VIS_VectorPutNB(tm,dstrank,dstcount,dstlist,srccount,srclist,flags GASNETE_THREAD_GET)

GASNETI_INLINE(_gex_VIS_VectorGetNB) GASNETI_WARN_UNUSED_RESULT
gex_Event_t _gex_VIS_VectorGetNB(
        gex_TM_t _tm,
        size_t _dstcount, gex_Memvec_t const _dstlist[],
        gex_Rank_t _srcrank,
        size_t _srccount, gex_Memvec_t const _srclist[],
        gex_Flags_t _flags GASNETE_THREAD_FARG) {
  gasnete_boundscheck_memveclist(_tm, _srcrank, _srccount, _srclist);
  gasnete_memveclist_checksizematch(_dstcount, _dstlist, _srccount, _srclist);
  GASNETI_TRACE_GETV(GETV_NB_BULK,_srcrank,_dstcount,_dstlist,_srccount,_srclist);
  return gasnete_getv(gasnete_synctype_nb,_tm,_dstcount,_dstlist,_srcrank,_srccount,_srclist,_flags GASNETE_THREAD_PASS);
}
#define gex_VIS_VectorGetNB(tm,dstcount,dstlist,srcrank,srccount,srclist,flags) \
       _gex_VIS_VectorGetNB(tm,dstcount,dstlist,srcrank,srccount,srclist,flags GASNETE_THREAD_GET)

GASNETI_INLINE(_gex_VIS_VectorPutNBI)
int _gex_VIS_VectorPutNBI(
        gex_TM_t _tm, gex_Rank_t _dstrank,
        size_t _dstcount, gex_Memvec_t const _dstlist[],
        size_t _srccount, gex_Memvec_t const _srclist[],
        gex_Flags_t _flags GASNETE_THREAD_FARG) {
  gasnete_boundscheck_memveclist(_tm, _dstrank, _dstcount, _dstlist);
  gasnete_memveclist_checksizematch(_dstcount, _dstlist, _srccount, _srclist);
  GASNETI_TRACE_PUTV(PUTV_NBI_BULK,_dstrank,_dstcount,_dstlist,_srccount,_srclist);
  return gasnete_putv(gasnete_synctype_nbi,_tm,_dstrank,_dstcount,_dstlist,_srccount,_srclist,_flags GASNETE_THREAD_PASS) == GEX_EVENT_NO_OP;
}
#define gex_VIS_VectorPutNBI(tm,dstrank,dstcount,dstlist,srccount,srclist,flags) \
       _gex_VIS_VectorPutNBI(tm,dstrank,dstcount,dstlist,srccount,srclist,flags GASNETE_THREAD_GET)

GASNETI_INLINE(_gex_VIS_VectorGetNBI)
int _gex_VIS_VectorGetNBI(
        gex_TM_t _tm,
        size_t _dstcount, gex_Memvec_t const _dstlist[],
        gex_Rank_t _srcrank,
        size_t _srccount, gex_Memvec_t const _srclist[],
        gex_Flags_t _flags GASNETE_THREAD_FARG) {
  gasnete_boundscheck_memveclist(_tm, _srcrank, _srccount, _srclist);
  gasnete_memveclist_checksizematch(_dstcount, _dstlist, _srccount, _srclist);
  GASNETI_TRACE_GETV(GETV_NBI_BULK,_srcrank,_dstcount,_dstlist,_srccount,_srclist);
  return gasnete_getv(gasnete_synctype_nbi,_tm,_dstcount,_dstlist,_srcrank,_srccount,_srclist,_flags GASNETE_THREAD_PASS) == GEX_EVENT_NO_OP;
}
#define gex_VIS_VectorGetNBI(tm,dstcount,dstlist,srcrank,srccount,srclist,flags) \
       _gex_VIS_VectorGetNBI(tm,dstcount,dstlist,srcrank,srccount,srclist,flags GASNETE_THREAD_GET)

/*---------------------------------------------------------------------------------*/
/* Indexed */
#ifndef gasnete_puti
  extern gex_Event_t gasnete_puti(
        gasnete_synctype_t _synctype,
        gex_TM_t _tm, gex_Rank_t _dstrank,
        size_t _dstcount, void * const _dstlist[], size_t _dstlen,
        size_t _srccount, void * const _srclist[], size_t _srclen,
        gex_Flags_t _flags GASNETE_THREAD_FARG);
#endif
#ifndef gasnete_geti
  extern gex_Event_t gasnete_geti(
        gasnete_synctype_t _synctype,
        gex_TM_t _tm,
        size_t _dstcount, void * const _dstlist[], size_t _dstlen,
        gex_Rank_t _srcrank,
        size_t _srccount, void * const _srclist[], size_t _srclen,
        gex_Flags_t _flags GASNETE_THREAD_FARG);
#endif

#if 1 // blocking interfaces removed in EX?
GASNETI_INLINE(_gex_VIS_IndexedPutBlocking)
int _gex_VIS_IndexedPutBlocking(
        gex_TM_t _tm, gex_Rank_t _dstrank,
        size_t _dstcount, void * const _dstlist[], size_t _dstlen,
        size_t _srccount, void * const _srclist[], size_t _srclen,
        gex_Flags_t _flags GASNETE_THREAD_FARG) {
  gasnete_boundscheck_addrlist(_tm, _dstrank, _dstcount, _dstlist, _dstlen);
  gasnete_addrlist_checksizematch(_dstcount, _dstlen, _srccount, _srclen);
  GASNETI_TRACE_PUTI(PUTI_BULK,_dstrank,_dstcount,_dstlist,_dstlen,_srccount,_srclist,_srclen);
  return gasnete_puti(gasnete_synctype_b,_tm,_dstrank,_dstcount,_dstlist,_dstlen,_srccount,_srclist,_srclen,_flags GASNETE_THREAD_PASS) == GEX_EVENT_NO_OP;
}
#define gex_VIS_IndexedPutBlocking(tm,dstrank,dstcount,dstlist,dstlen,srccount,srclist,srclen,flags) \
       _gex_VIS_IndexedPutBlocking(tm,dstrank,dstcount,dstlist,dstlen,srccount,srclist,srclen,flags GASNETE_THREAD_GET)

GASNETI_INLINE(_gex_VIS_IndexedGetBlocking)
int _gex_VIS_IndexedGetBlocking(
        gex_TM_t _tm,
        size_t _dstcount, void * const _dstlist[], size_t _dstlen,
        gex_Rank_t _srcrank,
        size_t _srccount, void * const _srclist[], size_t _srclen,
        gex_Flags_t _flags GASNETE_THREAD_FARG) {
  gasnete_boundscheck_addrlist(_tm, _srcrank, _srccount, _srclist, _srclen);
  gasnete_addrlist_checksizematch(_dstcount, _dstlen, _srccount, _srclen);
  GASNETI_TRACE_GETI(GETI_BULK,_srcrank,_dstcount,_dstlist,_dstlen,_srccount,_srclist,_srclen);
  return gasnete_geti(gasnete_synctype_b,_tm,_dstcount,_dstlist,_dstlen,_srcrank,_srccount,_srclist,_srclen,_flags GASNETE_THREAD_PASS) == GEX_EVENT_NO_OP;
}
#define gex_VIS_IndexedGetBlocking(tm,dstcount,dstlist,dstlen,srcrank,srccount,srclist,srclen,flags) \
       _gex_VIS_IndexedGetBlocking(tm,dstcount,dstlist,dstlen,srcrank,srccount,srclist,srclen,flags GASNETE_THREAD_GET)
#endif

GASNETI_INLINE(_gex_VIS_IndexedPutNB) GASNETI_WARN_UNUSED_RESULT
gex_Event_t _gex_VIS_IndexedPutNB(
        gex_TM_t _tm, gex_Rank_t _dstrank,
        size_t _dstcount, void * const _dstlist[], size_t _dstlen,
        size_t _srccount, void * const _srclist[], size_t _srclen,
        gex_Flags_t _flags GASNETE_THREAD_FARG) {
  gasnete_boundscheck_addrlist(_tm, _dstrank, _dstcount, _dstlist, _dstlen);
  gasnete_addrlist_checksizematch(_dstcount, _dstlen, _srccount, _srclen);
  GASNETI_TRACE_PUTI(PUTI_NB_BULK,_dstrank,_dstcount,_dstlist,_dstlen,_srccount,_srclist,_srclen);
  return gasnete_puti(gasnete_synctype_nb,_tm,_dstrank,_dstcount,_dstlist,_dstlen,_srccount,_srclist,_srclen,_flags GASNETE_THREAD_PASS);
}
#define gex_VIS_IndexedPutNB(tm,dstrank,dstcount,dstlist,dstlen,srccount,srclist,srclen,flags) \
       _gex_VIS_IndexedPutNB(tm,dstrank,dstcount,dstlist,dstlen,srccount,srclist,srclen,flags GASNETE_THREAD_GET)

GASNETI_INLINE(_gex_VIS_IndexedGetNB) GASNETI_WARN_UNUSED_RESULT
gex_Event_t _gex_VIS_IndexedGetNB(
        gex_TM_t _tm,
        size_t _dstcount, void * const _dstlist[], size_t _dstlen,
        gex_Rank_t _srcrank,
        size_t _srccount, void * const _srclist[], size_t _srclen,
        gex_Flags_t _flags GASNETE_THREAD_FARG) {
  gasnete_boundscheck_addrlist(_tm, _srcrank, _srccount, _srclist, _srclen);
  gasnete_addrlist_checksizematch(_dstcount, _dstlen, _srccount, _srclen);
  GASNETI_TRACE_GETI(GETI_NB_BULK,_srcrank,_dstcount,_dstlist,_dstlen,_srccount,_srclist,_srclen);
  return gasnete_geti(gasnete_synctype_nb,_tm,_dstcount,_dstlist,_dstlen,_srcrank,_srccount,_srclist,_srclen,_flags GASNETE_THREAD_PASS);
}
#define gex_VIS_IndexedGetNB(tm,dstcount,dstlist,dstlen,srcrank,srccount,srclist,srclen,flags) \
       _gex_VIS_IndexedGetNB(tm,dstcount,dstlist,dstlen,srcrank,srccount,srclist,srclen,flags GASNETE_THREAD_GET)

GASNETI_INLINE(_gex_VIS_IndexedPutNBI)
int _gex_VIS_IndexedPutNBI(
        gex_TM_t _tm, gex_Rank_t _dstrank,
        size_t _dstcount, void * const _dstlist[], size_t _dstlen,
        size_t _srccount, void * const _srclist[], size_t _srclen,
        gex_Flags_t _flags GASNETE_THREAD_FARG) {
  gasnete_boundscheck_addrlist(_tm, _dstrank, _dstcount, _dstlist, _dstlen);
  gasnete_addrlist_checksizematch(_dstcount, _dstlen, _srccount, _srclen);
  GASNETI_TRACE_PUTI(PUTI_NBI_BULK,_dstrank,_dstcount,_dstlist,_dstlen,_srccount,_srclist,_srclen);
  return gasnete_puti(gasnete_synctype_nbi,_tm,_dstrank,_dstcount,_dstlist,_dstlen,_srccount,_srclist,_srclen,_flags GASNETE_THREAD_PASS) == GEX_EVENT_NO_OP;
}
#define gex_VIS_IndexedPutNBI(tm,dstrank,dstcount,dstlist,dstlen,srccount,srclist,srclen,flags) \
       _gex_VIS_IndexedPutNBI(tm,dstrank,dstcount,dstlist,dstlen,srccount,srclist,srclen,flags GASNETE_THREAD_GET)

GASNETI_INLINE(_gex_VIS_IndexedGetNBI)
int _gex_VIS_IndexedGetNBI(
        gex_TM_t _tm,
        size_t _dstcount, void * const _dstlist[], size_t _dstlen,
        gex_Rank_t _srcrank,
        size_t _srccount, void * const _srclist[], size_t _srclen,
        gex_Flags_t _flags GASNETE_THREAD_FARG) {
  gasnete_boundscheck_addrlist(_tm, _srcrank, _srccount, _srclist, _srclen);
  gasnete_addrlist_checksizematch(_dstcount, _dstlen, _srccount, _srclen);
  GASNETI_TRACE_GETI(GETI_NBI_BULK,_srcrank,_dstcount,_dstlist,_dstlen,_srccount,_srclist,_srclen);
  return gasnete_geti(gasnete_synctype_nbi,_tm,_dstcount,_dstlist,_dstlen,_srcrank,_srccount,_srclist,_srclen,_flags GASNETE_THREAD_PASS) == GEX_EVENT_NO_OP;
}
#define gex_VIS_IndexedGetNBI(tm,dstcount,dstlist,dstlen,srcrank,srccount,srclist,srclen,flags) \
       _gex_VIS_IndexedGetNBI(tm,dstcount,dstlist,dstlen,srcrank,srccount,srclist,srclen,flags GASNETE_THREAD_GET)

/*---------------------------------------------------------------------------------*/
/* Strided */
#ifndef gasnete_puts
  extern gex_Event_t gasnete_puts(
        gasnete_synctype_t _synctype,
        gex_TM_t _tm, gex_Rank_t _dstrank,
        void *_dstaddr, const size_t _dststrides[],
        void *_srcaddr, const size_t _srcstrides[],
        const size_t _count[], size_t _stridelevels,
        gex_Flags_t _flags GASNETE_THREAD_FARG);

#endif
#ifndef gasnete_gets
  extern gex_Event_t gasnete_gets(
        gasnete_synctype_t _synctype,
        gex_TM_t _tm,
        void *_dstaddr, const size_t _dststrides[],
        gex_Rank_t _srcrank,
        void *_srcaddr, const size_t _srcstrides[],
        const size_t _count[], size_t _stridelevels,
        gex_Flags_t _flags GASNETE_THREAD_FARG);
#endif

// This is a TEMPORARY thunk for the purposes of the 12/17 beta release
// Emulates the new strided metadata format (with non-transpositional preconditions)
// over the old metadata format that the internal implementation still uses.
// Clients needing more than the default striding dimensions can override
// with -DGASNETE_STRIDED_BETATHUNK_DIMS=N
#ifndef GASNETE_STRIDED_BETATHUNK_DIMS
#define GASNETE_STRIDED_BETATHUNK_DIMS 32
#endif
#if GASNET_DEBUG
// technically strides are permitted to be garbage if any legacy count[i]=0
#define GASNETE_STRIDED_BETATHUNK_STRIDECHECK                         \
  if (!gasnete_strided_empty(_elemsz,_count,_stridelevels)) {         \
    for (size_t _i=0; _i < _stridelevels; _i++) {                     \
      gasneti_assert(_srcstrides[_i] >= 0);                           \
      gasneti_assert(_dststrides[_i] >= 0);                           \
    }                                                                 \
  }
#else
#define GASNETE_STRIDED_BETATHUNK_STRIDECHECK
#endif
#define GASNETE_STRIDED_BETATHUNK                                     \
  GASNETE_STRIDED_BETATHUNK_STRIDECHECK                               \
  gasneti_assert(sizeof(size_t) == sizeof(ptrdiff_t));                \
  const size_t *__srcstrides = (const size_t *)_srcstrides;           \
  const size_t *__dststrides = (const size_t *)_dststrides;           \
  gasneti_assert(_stridelevels < GASNETE_STRIDED_BETATHUNK_DIMS);     \
  size_t _count_thunk[GASNETE_STRIDED_BETATHUNK_DIMS];                \
  _count_thunk[0] = _elemsz;                                          \
  if (_stridelevels > 0)                                              \
    memcpy(&(_count_thunk[1]),_count,_stridelevels*sizeof(size_t));   \
  _count = _count_thunk;

#if 1 // blocking interfaces removed in EX?
GASNETI_INLINE(_gex_VIS_StridedPutBlocking)
int _gex_VIS_StridedPutBlocking(
        gex_TM_t _tm, gex_Rank_t _dstrank,
        void *_dstaddr, const ptrdiff_t _dststrides[],
        void *_srcaddr, const ptrdiff_t _srcstrides[],
        size_t _elemsz, const size_t _count[], size_t _stridelevels,
        gex_Flags_t _flags GASNETE_THREAD_FARG) {
  gasnete_check_strides(_dststrides, _srcstrides, _elemsz, _count, _stridelevels);
  gasnete_boundscheck_strided(_tm, _dstrank, _dstaddr, _dststrides, _elemsz, _count, _stridelevels);
  GASNETI_TRACE_PUTS(PUTS_BULK,_dstrank,_dstaddr,_dststrides,_srcaddr,_srcstrides,_elemsz,_count,_stridelevels);
  GASNETE_STRIDED_BETATHUNK
  return gasnete_puts(gasnete_synctype_b,_tm,_dstrank,_dstaddr,__dststrides,_srcaddr,__srcstrides,_count,_stridelevels,_flags GASNETE_THREAD_PASS) == GEX_EVENT_NO_OP;
}
#define gex_VIS_StridedPutBlocking(tm,dstrank,dstaddr,dststrides,srcaddr,srcstrides,elemsz,count,stridelevels,flags) \
       _gex_VIS_StridedPutBlocking(tm,dstrank,dstaddr,dststrides,srcaddr,srcstrides,elemsz,count,stridelevels,flags GASNETE_THREAD_GET)

GASNETI_INLINE(_gex_VIS_StridedGetBlocking)
int _gex_VIS_StridedGetBlocking(
        gex_TM_t _tm,
        void *_dstaddr, const ptrdiff_t _dststrides[],
        gex_Rank_t _srcrank,
        void *_srcaddr, const ptrdiff_t _srcstrides[],
        size_t _elemsz, const size_t _count[], size_t _stridelevels,
        gex_Flags_t _flags GASNETE_THREAD_FARG) {
  gasnete_check_strides(_dststrides, _srcstrides, _elemsz, _count, _stridelevels);
  gasnete_boundscheck_strided(_tm, _srcrank, _srcaddr, _srcstrides, _elemsz, _count, _stridelevels);
  GASNETI_TRACE_GETS(GETS_BULK,_srcrank,_dstaddr,_dststrides,_srcaddr,_srcstrides,_elemsz,_count,_stridelevels);
  GASNETE_STRIDED_BETATHUNK
  return gasnete_gets(gasnete_synctype_b,_tm,_dstaddr,__dststrides,_srcrank,_srcaddr,__srcstrides,_count,_stridelevels,_flags GASNETE_THREAD_PASS) == GEX_EVENT_NO_OP;
}
#define gex_VIS_StridedGetBlocking(tm,dstaddr,dststrides,srcrank,srcaddr,srcstrides,elemsz,count,stridelevels,flags) \
       _gex_VIS_StridedGetBlocking(tm,dstaddr,dststrides,srcrank,srcaddr,srcstrides,elemsz,count,stridelevels,flags GASNETE_THREAD_GET)
#endif

GASNETI_INLINE(_gex_VIS_StridedPutNB) GASNETI_WARN_UNUSED_RESULT
gex_Event_t _gex_VIS_StridedPutNB(
        gex_TM_t _tm, gex_Rank_t _dstrank,
        void *_dstaddr, const ptrdiff_t _dststrides[],
        void *_srcaddr, const ptrdiff_t _srcstrides[],
        size_t _elemsz, const size_t _count[], size_t _stridelevels,
        gex_Flags_t _flags GASNETE_THREAD_FARG) {
  gasnete_check_strides(_dststrides, _srcstrides, _elemsz, _count, _stridelevels);
  gasnete_boundscheck_strided(_tm, _dstrank, _dstaddr, _dststrides, _elemsz, _count, _stridelevels);
  GASNETI_TRACE_PUTS(PUTS_NB_BULK,_dstrank,_dstaddr,_dststrides,_srcaddr,_srcstrides,_elemsz,_count,_stridelevels);
  GASNETE_STRIDED_BETATHUNK
  return gasnete_puts(gasnete_synctype_nb,_tm,_dstrank,_dstaddr,__dststrides,_srcaddr,__srcstrides,_count,_stridelevels,_flags GASNETE_THREAD_PASS);
}
#define gex_VIS_StridedPutNB(tm,dstrank,dstaddr,dststrides,srcaddr,srcstrides,elemsz,count,stridelevels,flags) \
       _gex_VIS_StridedPutNB(tm,dstrank,dstaddr,dststrides,srcaddr,srcstrides,elemsz,count,stridelevels,flags GASNETE_THREAD_GET)

GASNETI_INLINE(_gex_VIS_StridedGetNB) GASNETI_WARN_UNUSED_RESULT
gex_Event_t _gex_VIS_StridedGetNB(
        gex_TM_t _tm,
        void *_dstaddr, const ptrdiff_t _dststrides[],
        gex_Rank_t _srcrank,
        void *_srcaddr, const ptrdiff_t _srcstrides[],
        size_t _elemsz, const size_t _count[], size_t _stridelevels,
        gex_Flags_t _flags GASNETE_THREAD_FARG) {
  gasnete_check_strides(_dststrides, _srcstrides, _elemsz, _count, _stridelevels);
  gasnete_boundscheck_strided(_tm, _srcrank, _srcaddr, _srcstrides, _elemsz, _count, _stridelevels);
  GASNETI_TRACE_GETS(GETS_NB_BULK,_srcrank,_dstaddr,_dststrides,_srcaddr,_srcstrides,_elemsz,_count,_stridelevels);
  GASNETE_STRIDED_BETATHUNK
  return gasnete_gets(gasnete_synctype_nb,_tm,_dstaddr,__dststrides,_srcrank,_srcaddr,__srcstrides,_count,_stridelevels,_flags GASNETE_THREAD_PASS);
}
#define gex_VIS_StridedGetNB(tm,dstaddr,dststrides,srcrank,srcaddr,srcstrides,elemsz,count,stridelevels,flags) \
       _gex_VIS_StridedGetNB(tm,dstaddr,dststrides,srcrank,srcaddr,srcstrides,elemsz,count,stridelevels,flags GASNETE_THREAD_GET)

GASNETI_INLINE(_gex_VIS_StridedPutNBI)
int _gex_VIS_StridedPutNBI(
        gex_TM_t _tm, gex_Rank_t _dstrank,
        void *_dstaddr, const ptrdiff_t _dststrides[],
        void *_srcaddr, const ptrdiff_t _srcstrides[],
        size_t _elemsz, const size_t _count[], size_t _stridelevels,
        gex_Flags_t _flags GASNETE_THREAD_FARG) {
  gasnete_check_strides(_dststrides, _srcstrides, _elemsz, _count, _stridelevels);
  gasnete_boundscheck_strided(_tm, _dstrank, _dstaddr, _dststrides, _elemsz, _count, _stridelevels);
  GASNETI_TRACE_PUTS(PUTS_NBI_BULK,_dstrank,_dstaddr,_dststrides,_srcaddr,_srcstrides,_elemsz,_count,_stridelevels);
  GASNETE_STRIDED_BETATHUNK
  return gasnete_puts(gasnete_synctype_nbi,_tm,_dstrank,_dstaddr,__dststrides,_srcaddr,__srcstrides,_count,_stridelevels,_flags GASNETE_THREAD_PASS) == GEX_EVENT_NO_OP;
}
#define gex_VIS_StridedPutNBI(tm,dstrank,dstaddr,dststrides,srcaddr,srcstrides,elemsz,count,stridelevels,flags) \
       _gex_VIS_StridedPutNBI(tm,dstrank,dstaddr,dststrides,srcaddr,srcstrides,elemsz,count,stridelevels,flags GASNETE_THREAD_GET)

GASNETI_INLINE(_gex_VIS_StridedGetNBI)
int _gex_VIS_StridedGetNBI(
        gex_TM_t _tm,
        void *_dstaddr, const ptrdiff_t _dststrides[],
        gex_Rank_t _srcrank,
        void *_srcaddr, const ptrdiff_t _srcstrides[],
        size_t _elemsz, const size_t _count[], size_t _stridelevels,
        gex_Flags_t _flags GASNETE_THREAD_FARG) {
  gasnete_check_strides(_dststrides, _srcstrides, _elemsz, _count, _stridelevels);
  gasnete_boundscheck_strided(_tm, _srcrank, _srcaddr, _srcstrides, _elemsz, _count, _stridelevels);
  GASNETI_TRACE_GETS(GETS_NBI_BULK,_srcrank,_dstaddr,_dststrides,_srcaddr,_srcstrides,_elemsz,_count,_stridelevels);
  GASNETE_STRIDED_BETATHUNK
  return gasnete_gets(gasnete_synctype_nbi,_tm,_dstaddr,__dststrides,_srcrank,_srcaddr,__srcstrides,_count,_stridelevels,_flags GASNETE_THREAD_PASS) == GEX_EVENT_NO_OP;
}
#define gex_VIS_StridedGetNBI(tm,dstaddr,dststrides,srcrank,srcaddr,srcstrides,elemsz,count,stridelevels,flags) \
       _gex_VIS_StridedGetNBI(tm,dstaddr,dststrides,srcrank,srcaddr,srcstrides,elemsz,count,stridelevels,flags GASNETE_THREAD_GET)

/*---------------------------------------------------------------------------------*/
// g2ex Strided wrappers
// These translate the Strided metadata from legacy to EX format

GASNETI_INLINE(_gasnet_puts_bulk)
void _gasnet_puts_bulk(
        gex_TM_t _tm, gex_Rank_t _dstrank,
        void *_dstaddr, const size_t _dststrides[],
        void *_srcaddr, const size_t _srcstrides[],
        const size_t _count[], size_t _stridelevels,
        gex_Flags_t _flags GASNETE_THREAD_FARG) {
  gasneti_assert(!(_flags & GEX_FLAG_IMMEDIATE));
  gasneti_assert(sizeof(size_t) == sizeof(ptrdiff_t));
  gasnete_check_stridesNT(_dststrides, _srcstrides, _count, _stridelevels);
  _gex_VIS_StridedPutBlocking(_tm,_dstrank,_dstaddr,(ptrdiff_t*)_dststrides,_srcaddr,(ptrdiff_t*)_srcstrides,_count[0],_count+1,_stridelevels,_flags GASNETE_THREAD_PASS);
}
GASNETI_INLINE(_gasnet_gets_bulk)
void _gasnet_gets_bulk(
        gex_TM_t _tm,
        void *_dstaddr, const size_t _dststrides[],
        gex_Rank_t _srcrank,
        void *_srcaddr, const size_t _srcstrides[],
        const size_t _count[], size_t _stridelevels,
        gex_Flags_t _flags GASNETE_THREAD_FARG) {
  gasneti_assert(!(_flags & GEX_FLAG_IMMEDIATE));
  gasneti_assert(sizeof(size_t) == sizeof(ptrdiff_t));
  gasnete_check_stridesNT(_dststrides, _srcstrides, _count, _stridelevels);
  _gex_VIS_StridedGetBlocking(_tm,_dstaddr,(ptrdiff_t*)_dststrides,_srcrank,_srcaddr,(ptrdiff_t*)_srcstrides,_count[0],_count+1,_stridelevels,_flags GASNETE_THREAD_PASS);
}
GASNETI_INLINE(_gasnet_puts_nb_bulk) GASNETI_WARN_UNUSED_RESULT
gex_Event_t _gasnet_puts_nb_bulk(
        gex_TM_t _tm, gex_Rank_t _dstrank,
        void *_dstaddr, const size_t _dststrides[],
        void *_srcaddr, const size_t _srcstrides[],
        const size_t _count[], size_t _stridelevels,
        gex_Flags_t _flags GASNETE_THREAD_FARG) {
  gasneti_assert(!(_flags & GEX_FLAG_IMMEDIATE));
  gasneti_assert(sizeof(size_t) == sizeof(ptrdiff_t));
  gasnete_check_stridesNT(_dststrides, _srcstrides, _count, _stridelevels);
  return _gex_VIS_StridedPutNB(_tm,_dstrank,_dstaddr,(ptrdiff_t*)_dststrides,_srcaddr,(ptrdiff_t*)_srcstrides,_count[0],_count+1,_stridelevels,_flags GASNETE_THREAD_PASS);
}
GASNETI_INLINE(_gasnet_gets_nb_bulk) GASNETI_WARN_UNUSED_RESULT
gex_Event_t _gasnet_gets_nb_bulk(
        gex_TM_t _tm,
        void *_dstaddr, const size_t _dststrides[],
        gex_Rank_t _srcrank,
        void *_srcaddr, const size_t _srcstrides[],
        const size_t _count[], size_t _stridelevels,
        gex_Flags_t _flags GASNETE_THREAD_FARG) {
  gasneti_assert(!(_flags & GEX_FLAG_IMMEDIATE));
  gasneti_assert(sizeof(size_t) == sizeof(ptrdiff_t));
  gasnete_check_stridesNT(_dststrides, _srcstrides, _count, _stridelevels);
  return _gex_VIS_StridedGetNB(_tm,_dstaddr,(ptrdiff_t*)_dststrides,_srcrank,_srcaddr,(ptrdiff_t*)_srcstrides,_count[0],_count+1,_stridelevels,_flags GASNETE_THREAD_PASS);
}
GASNETI_INLINE(_gasnet_puts_nbi_bulk)
void _gasnet_puts_nbi_bulk(
        gex_TM_t _tm, gex_Rank_t _dstrank,
        void *_dstaddr, const size_t _dststrides[],
        void *_srcaddr, const size_t _srcstrides[],
        const size_t _count[], size_t _stridelevels,
        gex_Flags_t _flags GASNETE_THREAD_FARG) {
  gasneti_assert(!(_flags & GEX_FLAG_IMMEDIATE));
  gasneti_assert(sizeof(size_t) == sizeof(ptrdiff_t));
  gasnete_check_stridesNT(_dststrides, _srcstrides, _count, _stridelevels);
  _gex_VIS_StridedPutNBI(_tm,_dstrank,_dstaddr,(ptrdiff_t*)_dststrides,_srcaddr,(ptrdiff_t*)_srcstrides,_count[0],_count+1,_stridelevels,_flags GASNETE_THREAD_PASS);
}
GASNETI_INLINE(_gasnet_gets_nbi_bulk)
void _gasnet_gets_nbi_bulk(
        gex_TM_t _tm,
        void *_dstaddr, const size_t _dststrides[],
        gex_Rank_t _srcrank,
        void *_srcaddr, const size_t _srcstrides[],
        const size_t _count[], size_t _stridelevels,
        gex_Flags_t _flags GASNETE_THREAD_FARG) {
  gasneti_assert(!(_flags & GEX_FLAG_IMMEDIATE));
  gasneti_assert(sizeof(size_t) == sizeof(ptrdiff_t));
  gasnete_check_stridesNT(_dststrides, _srcstrides, _count, _stridelevels);
  _gex_VIS_StridedGetNBI(_tm,_dstaddr,(ptrdiff_t*)_dststrides,_srcrank,_srcaddr,(ptrdiff_t*)_srcstrides,_count[0],_count+1,_stridelevels,_flags GASNETE_THREAD_PASS);
}

/*---------------------------------------------------------------------------------*/

GASNETI_END_NOWARN
GASNETI_END_EXTERNC

#endif

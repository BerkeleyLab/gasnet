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
uintptr_t gasnete_memveclist_totalsz(size_t count, gex_Memvec_t const *list) {
  uintptr_t retval = 0;
  size_t i;
  for (i = 0; i < count; i++) {
    retval += list[i].gex_len;
  }
  return retval;
}

GASNETI_INLINE(gasnete_memveclist_stats)
gasneti_memveclist_stats_t gasnete_memveclist_stats(size_t count, gex_Memvec_t const *list) {
  gasneti_memveclist_stats_t retval;
  size_t minsz = (size_t)-1, maxsz = 0;
  uintptr_t totalsz = 0;
  char *minaddr = (char *)(intptr_t)(uintptr_t)-1;
  char *maxaddr = (char *)0;
  size_t i;
  for (i = 0; i < count; i++) {
    size_t const len = list[i].gex_len;
    char * const addr = (char *)list[i].gex_addr;
    if (len > 0) {
      if (len < minsz) minsz = len;
      if (len > maxsz) maxsz = len;
      if (addr < minaddr) minaddr = addr;
      if (addr + len - 1 > maxaddr) maxaddr = addr + len - 1;
      totalsz += len;
    }
  }
  retval.minsz = minsz;
  retval.maxsz = maxsz;
  retval.minaddr = minaddr;
  retval.maxaddr = maxaddr;
  retval.totalsz = totalsz;
  gasneti_assert(totalsz == gasnete_memveclist_totalsz(count, list));
  return retval;
}
/*---------------------------------------------------------------------------------*/

GASNETI_INLINE(gasnete_addrlist_stats)
gasneti_addrlist_stats_t gasnete_addrlist_stats(size_t count, void * const *list, size_t len) {
  gasneti_addrlist_stats_t retval;
  char *minaddr = (char *)(intptr_t)(uintptr_t)-1;
  char *maxaddr = (char *)0;
#if PLATFORM_COMPILER_GNU && PLATFORM_COMPILER_VERSION_EQ(4,5,1)
  ssize_t i; /* size_t triggers an ICE exclusive to gcc-4.5.1, but this gets a warning instead */
#else
  size_t i;
#endif
  for (i = 0; i < count; i++) {
    char * const addr = (char *)list[i];
    if (addr < minaddr) minaddr = addr;
    if (addr + len - 1 > maxaddr) maxaddr = addr + len - 1;
  }
  retval.minaddr = minaddr;
  retval.maxaddr = maxaddr;
  return retval;
}

/*---------------------------------------------------------------------------------*/

/* returns non-zero iff the specified strided region is empty */
GASNETI_INLINE(gasnete_strided_empty)
int gasnete_strided_empty(size_t const *count, size_t stridelevels) {
  size_t i;
  for (i = 0; i <= stridelevels; i++) {
    if_pf (count[i] == 0) return 1;
  }
  return 0;
}

/* returns the number of top-level dimensions with a count of 1 */
GASNETI_INLINE(gasnete_strided_nulldims)
size_t gasnete_strided_nulldims(size_t const *count, size_t stridelevels) {
  ssize_t i;
  for (i = stridelevels; i >= 0; i--) {
    if_pt (count[i] != 1) return stridelevels-i;
  }
  return stridelevels+1;
}

/* returns the length of the bounding box containing all the data */
GASNETI_INLINE(gasnete_strided_extent)
size_t gasnete_strided_extent(size_t const *strides, size_t const *count, size_t stridelevels) {
  /* Calculating the bounding rectangle for a strided section is subtle.
     The obvious choice:
       count[stridelevels]*strides[stridelevels-1]
     is an upper bound which is too large in many cases
     The true exact length is:
       count[0] + SUM(i=[1..stridelevels] | (count[i]-1)*strides[i-1])
   */
  size_t i;
  size_t sz = count[0];
  if_pf (count[0] == 0) return 0;
  for (i = 1; i <= stridelevels; i++) {
    if_pf (count[i] == 0) return 0;
    sz += (count[i]-1)*strides[i-1];
  }
  return sz;
}

/* returns the total bytes of data in the transfer */
GASNETI_INLINE(gasnete_strided_datasize)
size_t gasnete_strided_datasize(size_t const *count, size_t stridelevels) {
  size_t i;
  size_t sz = count[0];
  for (i = 1; i <= stridelevels; i++) {
    size_t const cnt = count[i];
    if_pf (sz == 0) return 0;
    if_pf (cnt != 1) sz *= cnt;
  }
  return sz;
}

/* returns the size of the contiguous segments in the transfer
 */
GASNETI_INLINE(gasnete_strided_contigsz)
size_t gasnete_strided_contigsz(size_t const *strides, size_t const *count, size_t stridelevels) {
  size_t limit = stridelevels;
  size_t sz = count[0];
  size_t i;

  /* querying the contiguity of an empty region probably signifies a bug */
  gasneti_assert(!gasnete_strided_empty(count,stridelevels)); 

  while (limit && count[limit] == 1) limit--; /* ignore null dimensions */
  if (strides[0] > sz || limit == 0) return sz;
  for (i = 1; i < limit; i++) {
    sz *= count[i];
    if (strides[i] > sz) return sz;
    gasneti_assert(strides[i] == (count[i]*strides[i-1]));
  }
  sz *= count[limit];
  return sz;
}

/* returns the highest stridelevel with data contiguity 
   eg. returns zero if only the bottom level is contiguous,
   and stridelevels if the entire region is contiguous
 */
GASNETI_INLINE(gasnete_strided_contiguity)
size_t gasnete_strided_contiguity(size_t const *strides, size_t const *count, size_t stridelevels) {
  size_t i;
  size_t limit = stridelevels;

  /* querying the contiguity of an empty region probably signifies a bug */
  gasneti_assert(!gasnete_strided_empty(count,stridelevels)); 

  while (limit && count[limit] == 1) limit--; /* ignore null dimensions */
  if_pf (limit == 0) return stridelevels; /* trivially fully contiguous */

  if (strides[0] > count[0]) return 0;
  gasneti_assert(strides[0] == count[0]);
  for (i = 1; i < limit; i++) {
    if (strides[i] > (count[i]*strides[i-1])) return i;
    gasneti_assert(strides[i] == (count[i]*strides[i-1]));
  }
  return stridelevels;
}

/* returns the highest stridelevel with data contiguity in both regions
   eg. returns zero if only the bottom level is contiguous in one or both regions,
   and stridelevels if the both regions are entirely contiguous
   this can computed more efficiently than checking contiguity of each separately
 */
GASNETI_INLINE(gasnete_strided_dualcontiguity)
size_t gasnete_strided_dualcontiguity(size_t const *strides1, size_t const *strides2, size_t const *count, size_t stridelevels) {
  size_t i;
  size_t temp;
  size_t limit = stridelevels;

  /* querying the contiguity of an empty region probably signifies a bug */
  gasneti_assert(!gasnete_strided_empty(count,stridelevels)); 

  while (limit && count[limit] == 1) limit--; /* ignore null dimensions */
  if_pf (limit == 0) return stridelevels; /* trivially fully contiguous */

  temp = (strides1[0]+strides2[0]);
  if (temp > (count[0]<<1)) {
    gasneti_assert(strides1[0] > count[0] || strides2[0] > count[0]);
    return 0;
  }
  gasneti_assert(strides1[0] == count[0] && strides1[0] == count[0]);
  /* loop invariant: temp == strides1[i-1]*2 == strides2[i-1]*2 */
  for (i = 1; i < limit; i++) {
    size_t const newtemp = (strides1[i]+strides2[i]);
    if (newtemp > (count[i]*temp)) {
      gasneti_assert(strides1[i] > (count[i]*strides1[i-1]) || strides2[i] > (count[i]*strides2[i-1]));
      return i;
    }
    gasneti_assert(strides1[i] == (count[i]*strides1[i-1]) || strides2[i] == (count[i]*strides2[i-1]));
    temp = newtemp;
  }
  return stridelevels;
}

/* returns the size of the contiguous region at the dualcontiguity level */
GASNETI_INLINE(gasnete_strided_dualcontigsz)
size_t gasnete_strided_dualcontigsz(size_t const *strides1, size_t const *strides2, size_t const *count, size_t stridelevels) {
  size_t i;
  size_t temp;
  size_t limit = stridelevels;

  /* querying the contiguity of an empty region probably signifies a bug */
  gasneti_assert(!gasnete_strided_empty(count,stridelevels)); 

  while (limit && count[limit] == 1) limit--; /* ignore null dimensions */
  if_pf (limit == 0) return count[0]; /* trivially fully contiguous */

  temp = (strides1[0]+strides2[0]);
  if (temp > (count[0]<<1)) {
    gasneti_assert(strides1[0] > count[0] || strides2[0] > count[0]);
    return count[0];
  }
  gasneti_assert(strides1[0] == count[0] && strides1[0] == count[0]);
  /* loop invariant: temp == strides1[i-1]*2 == strides2[i-1]*2 */
  for (i = 1; i < limit; i++) {
    size_t const newtemp = (strides1[i]+strides2[i]);
    temp *= count[i];
    if (newtemp > temp) {
      gasneti_assert(strides1[i] > (count[i]*strides1[i-1]) || strides2[i] > (count[i]*strides2[i-1]));
      return temp>>1;
    }
    gasneti_assert(strides1[i] == (count[i]*strides1[i-1]) || strides2[i] == (count[i]*strides2[i-1]));
    temp = newtemp;
  }
  temp *= count[limit];
  return temp>>1;
}

/* returns the number of contiguous segments in the transfer */
GASNETI_INLINE(gasnete_strided_segments)
size_t gasnete_strided_segments(size_t const *strides, size_t const *count, size_t stridelevels) {
  size_t contiglevel = gasnete_strided_contiguity(strides, count, stridelevels);
  if (contiglevel == stridelevels) return 1;
  else {
    size_t i;
    size_t sz = count[contiglevel+1];
    for (i = contiglevel+2; i <= stridelevels; i++) {
      sz *= count[i];
    }
    return sz;
  }
}

typedef struct {
  size_t srcextent; /* the length of the bounding box containing all the src data */
  size_t dstextent; /* the length of the bounding box containing all the dst data */

  uintptr_t totalsz;   /* the total bytes of data in the transfer */

  size_t nulldims;  /* number of top-level dimensions with a count of 1 -
                       these dimensions can be ignored for most purposes 
                       (stridelevels+1 means all counts are one)
                     */

  size_t srccontiguity; /* the highest stridelevel with data contiguity in the src region
                           eg. zero if only the bottom level is contiguous,
                           and stridelevels if the entire region is contiguous
                         */
  size_t dstcontiguity; /* the highest stridelevel with data contiguity in the dst region
                           eg. zero if only the bottom level is contiguous,
                           and stridelevels if the entire region is contiguous
                         */
  size_t dualcontiguity; /* MIN(srccontiguity, dstcontiguity) */

  size_t srcsegments;   /* number of contiguous segments in the src region */
  size_t dstsegments;   /* number of contiguous segments in the dst region */

  size_t srccontigsz;   /* size of the contiguous segments in the src region */
  size_t dstcontigsz;   /* size of the contiguous segments in the dst region */
  size_t dualcontigsz;   /* MIN(srccontigsz,dstcontigsz) */

} gasnete_strided_stats_t;

/* calculate a number of useful shape properties over the given regions */
GASNETI_INLINE(gasnete_strided_stats)
void gasnete_strided_stats(gasnete_strided_stats_t *result, 
                           size_t const *dststrides, size_t const *srcstrides, 
                           size_t const *count, size_t stridelevels) {
  if_pf (stridelevels == 0 && count[0] != 0) {
    size_t const sz = count[0];
    gasneti_assert(!gasnete_strided_empty(count, stridelevels));
    result->srcextent = sz;
    result->dstextent = sz;
    result->totalsz = sz;
    result->nulldims = 0;
    result->srccontiguity = 0;
    result->dstcontiguity = 0;
    result->dualcontiguity = 0;
    result->srcsegments = 1;
    result->dstsegments = 1;
    result->srccontigsz = sz;
    result->dstcontigsz = sz;
    result->dualcontigsz = sz;
    return;
  } else {
    ssize_t limit;
    ssize_t i;
    int srcbreak = 0;
    int dstbreak = 0;
    size_t const sz = count[0];
    size_t srcextent = sz;
    size_t dstextent = sz;
    size_t srcsegments = 1;
    size_t dstsegments = 1;
    size_t srccontigsz = sz;
    size_t dstcontigsz = sz;
    for (limit = stridelevels; limit >= 0; limit--) if (count[limit] != 1) break;
    result->nulldims = stridelevels - limit;
    result->srccontiguity = stridelevels;
    result->dstcontiguity = stridelevels;

    for (i=0; i < limit; i++) {
      size_t const nextcount = count[i+1];
      size_t const srcstride = srcstrides[i];
      size_t const dststride = dststrides[i];
      srcextent += (nextcount-1)*srcstride;
      dstextent += (nextcount-1)*dststride;

      if (srcbreak) srcsegments *= nextcount;
      else if (srcstride > srccontigsz) {
          srcbreak = 1;
          result->srccontiguity = i;
          srcsegments *= nextcount;
      } else srccontigsz *= nextcount;

      if (dstbreak) dstsegments *= nextcount;
      else if (dststride > dstcontigsz) {
          dstbreak = 1;
          result->dstcontiguity = i;
          dstsegments *= nextcount;
      } else dstcontigsz *= nextcount;
    }

    result->totalsz = ((uintptr_t)srcsegments)*srccontigsz;
    if (result->totalsz == 0) { /* empty xfer */
      gasneti_assert(gasnete_strided_empty(count, stridelevels));
      gasneti_assert(gasnete_strided_datasize(count, stridelevels) == 0);
      result->srcextent = 0;
      result->dstextent = 0;
      result->nulldims = 0;
      result->srccontiguity = 0;
      result->dstcontiguity = 0;
      result->dualcontiguity = 0;
      result->srcsegments = 0;
      result->dstsegments = 0;
      result->srccontigsz = 0;
      result->dstcontigsz = 0;
      return;
    }
    result->srccontigsz = srccontigsz;
    result->dstcontigsz = dstcontigsz;
    result->srcsegments = srcsegments;
    result->dstsegments = dstsegments;
    result->srcextent = srcextent;
    result->dstextent = dstextent;
    result->dualcontiguity = MIN(result->srccontiguity, result->dstcontiguity);
    result->dualcontigsz = MIN(result->srccontigsz, result->dstcontigsz);
    /* sanity check */
    gasneti_assert(!gasnete_strided_empty(count, stridelevels));
    gasneti_assert(result->srcextent == gasnete_strided_extent(srcstrides, count, stridelevels));
    gasneti_assert(result->dstextent == gasnete_strided_extent(dststrides, count, stridelevels));
    gasneti_assert(result->totalsz == gasnete_strided_datasize(count, stridelevels));
    gasneti_assert(result->nulldims == gasnete_strided_nulldims(count, stridelevels));
    gasneti_assert(result->srccontiguity == gasnete_strided_contiguity(srcstrides, count, stridelevels));
    gasneti_assert(result->dstcontiguity == gasnete_strided_contiguity(dststrides, count, stridelevels));
    gasneti_assert(result->dualcontiguity == gasnete_strided_dualcontiguity(srcstrides, dststrides, count, stridelevels));
    gasneti_assert(result->srcsegments == gasnete_strided_segments(srcstrides, count, stridelevels));
    gasneti_assert(result->dstsegments == gasnete_strided_segments(dststrides, count, stridelevels));
    gasneti_assert(result->srccontigsz == gasnete_strided_contigsz(srcstrides, count, stridelevels));
    gasneti_assert(result->dstcontigsz == gasnete_strided_contigsz(dststrides, count, stridelevels));
    gasneti_assert(result->dualcontigsz == gasnete_strided_dualcontigsz(srcstrides, dststrides, count, stridelevels));
  }
  return;
}

#if GASNET_NDEBUG
  #define gasnete_boundscheck_memveclist(tm, node, count, list)
  #define gasnete_memveclist_checksizematch(dstcount, dstlist, srccount, srclist)
  #define gasnete_boundscheck_addrlist(tm, node, count, list, len) 
  #define gasnete_addrlist_checksizematch(dstcount, dstlen, srccount, srclen)
  #define gasnete_boundscheck_strided(tm, node, addr, strides, count, stridelevels)
  #define gasnete_check_strides(dststrides, srcstrides, count, stridelevels)
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
    gasneti_memveclist_stats_t dststats = gasnete_memveclist_stats((dstcount), (dstlist));     \
    gasneti_memveclist_stats_t srcstats = gasnete_memveclist_stats((srccount), (srclist));     \
    if_pf (dststats.totalsz != srcstats.totalsz) {                                             \
      char * dstlist_str =                                                                     \
             (char *)gasneti_extern_malloc(gasneti_format_memveclist_bufsz(dstcount));         \
      char * srclist_str =                                                                     \
             (char *)gasneti_extern_malloc(gasneti_format_memveclist_bufsz(srccount));         \
      gasneti_format_memveclist(dstlist_str, (dstcount), (dstlist));                           \
      gasneti_format_memveclist(srclist_str, (srccount), (srclist));                           \
      gasneti_fatalerror("Source and destination memvec lists disagree on total size at %s:\n" \
                         "  srclist: %s\n"                                                     \
                         "  dstlist: %s\n",                                                    \
                         gasneti_current_loc, dstlist_str, srclist_str);                       \
      /* gasneti_extern_free(dstlist_str); -- dead code */                                     \
      /* gasneti_extern_free(srclist_str); -- dead code */                                     \
    }                                                                                          \
    if_pf (dststats.totalsz != 0 &&                                                            \
      ((uintptr_t)dststats.minaddr) + dststats.totalsz - 1 > ((uintptr_t)dststats.maxaddr)) {  \
      char * dstlist_str =                                                                     \
             (char *)gasneti_extern_malloc(gasneti_format_memveclist_bufsz(dstcount));         \
      gasneti_format_memveclist(dstlist_str, (dstcount), (dstlist));                           \
      gasneti_fatalerror("Destination memvec list has overlapping elements at %s:\n"           \
                         "  dstlist: %s\n"                                                     \
                         "(note this test is currently conservative "                          \
                         "and may fail to detect some illegal cases)",                         \
                         gasneti_current_loc, dstlist_str);                                    \
      /* gasneti_extern_free(dstlist_str); -- dead code */                                     \
    }                                                                                          \
  } while (0)

  #define gasnete_boundscheck_addrlist(tm, rank, count, list, len) do { \
    gex_TM_t __tm = (tm);                                           \
    gex_Rank_t __node = (rank); /* TOOD-EX: tm support */    \
    size_t _count = (count);                                        \
    void * const * const _list = (list);                            \
    size_t _len = (len);                                            \
    size_t _i;                                                      \
    if_pt (_len > 0) {                                              \
      for (_i=0; _i < _count; _i++) {                               \
        gasneti_boundscheck(__tm, __node, _list[_i], _len);         \
      }                                                             \
    }                                                               \
  } while (0)

  #define gasnete_addrlist_checksizematch(dstcount, dstlen, srccount, srclen) do {        \
    if_pf ((dstlen) == 0) gasneti_fatalerror("dstlen == 0 at: %s\n",gasneti_current_loc); \
    if_pf ((srclen) == 0) gasneti_fatalerror("srclen == 0 at: %s\n",gasneti_current_loc); \
    if_pf ((dstcount)*(dstlen) != (srccount)*(srclen)) {                                  \
      gasneti_fatalerror("Total data size mismatch at: %s\n"                              \
                         "dstcount(%i)*dstlen(%i) != srccount(%i)*srclen(%i)",            \
                         gasneti_current_loc,                                             \
                         (int)dstcount, (int)dstlen, (int)srccount, (int)srclen);         \
    }                                                                                     \
  } while (0)

  #define gasnete_check_strides(dststrides, srcstrides, count, stridelevels) do {                       \
    const size_t * const __dststrides = (dststrides);                                                   \
    const size_t * const __srcstrides = (srcstrides);                                                   \
    const size_t * const __count = (count);                                                             \
    const size_t __stridelevels = (stridelevels);                                                       \
    if_pt (!gasnete_strided_empty(__count, __stridelevels)) {                                           \
      if_pf (__stridelevels > 0 && __dststrides[0] < __count[0])                                        \
          gasneti_fatalerror("dststrides[0](%i) < count[0](%i) at: %s",                                 \
                        (int)__dststrides[0],(int)__count[0], gasneti_current_loc);                     \
      if_pf (__stridelevels > 0 && __srcstrides[0] < __count[0])                                        \
          gasneti_fatalerror("srcstrides[0](%i) < count[0](%i) at: %s",                                 \
                        (int)__srcstrides[0],(int)__count[0], gasneti_current_loc);                     \
      for (size_t _i = 1; _i < __stridelevels; _i++) {                                                  \
        if_pf (__dststrides[_i] < (__count[_i] * __dststrides[_i-1]))                                   \
          gasneti_fatalerror("dststrides[%i](%i) < (count[%i](%i) * dststrides[%i](%i)) at: %s",        \
                     (int)_i,(int)__dststrides[_i],                                                     \
                     (int)_i,(int)__count[_i], (int)_i-1,(int)__dststrides[_i-1], gasneti_current_loc); \
        if_pf (__srcstrides[_i] < (__count[_i] * __srcstrides[_i-1]))                                   \
          gasneti_fatalerror("srcstrides[%i](%i) < (count[%i](%i) * srcstrides[%i](%i)) at: %s",        \
                     (int)_i,(int)__srcstrides[_i],                                                     \
                     (int)_i,(int)__count[_i], (int)_i-1,(int)__srcstrides[_i-1], gasneti_current_loc); \
      }                                                                                                 \
    }                                                                                                   \
  } while (0)

  #define gasnete_boundscheck_strided(tm, node, addr, strides, count, stridelevels) do { \
    size_t __stridelevels = (stridelevels);                                              \
    if_pt (!gasnete_strided_empty((count), __stridelevels)) {                            \
      gasneti_boundscheck((tm), (node), (addr),                                          \
        gasnete_strided_extent((strides),(count),__stridelevels));                       \
    }                                                                                    \
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
  if (!gasnete_strided_empty(_count,_stridelevels)) {                 \
    for (size_t _i=0; _i < _stridelevels; _i++) {                     \
      gasneti_assert(__srcstrides[_i] >= 0);                          \
      gasneti_assert(__dststrides[_i] >= 0);                          \
    }                                                                 \
  }
#else
#define GASNETE_STRIDED_BETATHUNK_STRIDECHECK
#endif
#define GASNETE_STRIDED_BETATHUNK                                     \
  GASNETE_STRIDED_BETATHUNK_STRIDECHECK                               \
  const size_t *_srcstrides = (const size_t *)__srcstrides;           \
  const size_t *_dststrides = (const size_t *)__dststrides;           \
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
        void *_dstaddr, const ssize_t __dststrides[],
        void *_srcaddr, const ssize_t __srcstrides[],
        size_t _elemsz, const size_t _count[], size_t _stridelevels,
        gex_Flags_t _flags GASNETE_THREAD_FARG) {
  GASNETE_STRIDED_BETATHUNK
  gasnete_check_strides(_dststrides, _srcstrides, _count, _stridelevels);
  gasnete_boundscheck_strided(_tm, _dstrank, _dstaddr, _dststrides, _count, _stridelevels);
  GASNETI_TRACE_PUTS(PUTS_BULK,_dstrank,_dstaddr,_dststrides,_srcaddr,_srcstrides,_count,_stridelevels);
  return gasnete_puts(gasnete_synctype_b,_tm,_dstrank,_dstaddr,_dststrides,_srcaddr,_srcstrides,_count,_stridelevels,_flags GASNETE_THREAD_PASS) == GEX_EVENT_NO_OP;
}
#define gex_VIS_StridedPutBlocking(tm,dstrank,dstaddr,dststrides,srcaddr,srcstrides,elemsz,count,stridelevels,flags) \
       _gex_VIS_StridedPutBlocking(tm,dstrank,dstaddr,dststrides,srcaddr,srcstrides,elemsz,count,stridelevels,flags GASNETE_THREAD_GET)

GASNETI_INLINE(_gex_VIS_StridedGetBlocking)
int _gex_VIS_StridedGetBlocking(
        gex_TM_t _tm,
        void *_dstaddr, const ssize_t __dststrides[],
        gex_Rank_t _srcrank,
        void *_srcaddr, const ssize_t __srcstrides[],
        size_t _elemsz, const size_t _count[], size_t _stridelevels,
        gex_Flags_t _flags GASNETE_THREAD_FARG) {
  GASNETE_STRIDED_BETATHUNK
  gasnete_check_strides(_dststrides, _srcstrides, _count, _stridelevels);
  gasnete_boundscheck_strided(_tm, _srcrank, _srcaddr, _srcstrides, _count, _stridelevels);
  GASNETI_TRACE_GETS(GETS_BULK,_srcrank,_dstaddr,_dststrides,_srcaddr,_srcstrides,_count,_stridelevels);
  return gasnete_gets(gasnete_synctype_b,_tm,_dstaddr,_dststrides,_srcrank,_srcaddr,_srcstrides,_count,_stridelevels,_flags GASNETE_THREAD_PASS) == GEX_EVENT_NO_OP;
}
#define gex_VIS_StridedGetBlocking(tm,dstaddr,dststrides,srcrank,srcaddr,srcstrides,elemsz,count,stridelevels,flags) \
       _gex_VIS_StridedGetBlocking(tm,dstaddr,dststrides,srcrank,srcaddr,srcstrides,elemsz,count,stridelevels,flags GASNETE_THREAD_GET)
#endif

GASNETI_INLINE(_gex_VIS_StridedPutNB) GASNETI_WARN_UNUSED_RESULT
gex_Event_t _gex_VIS_StridedPutNB(
        gex_TM_t _tm, gex_Rank_t _dstrank,
        void *_dstaddr, const ssize_t __dststrides[],
        void *_srcaddr, const ssize_t __srcstrides[],
        size_t _elemsz, const size_t _count[], size_t _stridelevels,
        gex_Flags_t _flags GASNETE_THREAD_FARG) {
  GASNETE_STRIDED_BETATHUNK
  gasnete_check_strides(_dststrides, _srcstrides, _count, _stridelevels);
  gasnete_boundscheck_strided(_tm, _dstrank, _dstaddr, _dststrides, _count, _stridelevels);
  GASNETI_TRACE_PUTS(PUTS_NB_BULK,_dstrank,_dstaddr,_dststrides,_srcaddr,_srcstrides,_count,_stridelevels);
  return gasnete_puts(gasnete_synctype_nb,_tm,_dstrank,_dstaddr,_dststrides,_srcaddr,_srcstrides,_count,_stridelevels,_flags GASNETE_THREAD_PASS);
}
#define gex_VIS_StridedPutNB(tm,dstrank,dstaddr,dststrides,srcaddr,srcstrides,elemsz,count,stridelevels,flags) \
       _gex_VIS_StridedPutNB(tm,dstrank,dstaddr,dststrides,srcaddr,srcstrides,elemsz,count,stridelevels,flags GASNETE_THREAD_GET)

GASNETI_INLINE(_gex_VIS_StridedGetNB) GASNETI_WARN_UNUSED_RESULT
gex_Event_t _gex_VIS_StridedGetNB(
        gex_TM_t _tm,
        void *_dstaddr, const ssize_t __dststrides[],
        gex_Rank_t _srcrank,
        void *_srcaddr, const ssize_t __srcstrides[],
        size_t _elemsz, const size_t _count[], size_t _stridelevels,
        gex_Flags_t _flags GASNETE_THREAD_FARG) {
  GASNETE_STRIDED_BETATHUNK
  gasnete_check_strides(_dststrides, _srcstrides, _count, _stridelevels);
  gasnete_boundscheck_strided(_tm, _srcrank, _srcaddr, _srcstrides, _count, _stridelevels);
  GASNETI_TRACE_GETS(GETS_NB_BULK,_srcrank,_dstaddr,_dststrides,_srcaddr,_srcstrides,_count,_stridelevels);
  return gasnete_gets(gasnete_synctype_nb,_tm,_dstaddr,_dststrides,_srcrank,_srcaddr,_srcstrides,_count,_stridelevels,_flags GASNETE_THREAD_PASS);
}
#define gex_VIS_StridedGetNB(tm,dstaddr,dststrides,srcrank,srcaddr,srcstrides,elemsz,count,stridelevels,flags) \
       _gex_VIS_StridedGetNB(tm,dstaddr,dststrides,srcrank,srcaddr,srcstrides,elemsz,count,stridelevels,flags GASNETE_THREAD_GET)

GASNETI_INLINE(_gex_VIS_StridedPutNBI)
int _gex_VIS_StridedPutNBI(
        gex_TM_t _tm, gex_Rank_t _dstrank,
        void *_dstaddr, const ssize_t __dststrides[],
        void *_srcaddr, const ssize_t __srcstrides[],
        size_t _elemsz, const size_t _count[], size_t _stridelevels,
        gex_Flags_t _flags GASNETE_THREAD_FARG) {
  GASNETE_STRIDED_BETATHUNK
  gasnete_check_strides(_dststrides, _srcstrides, _count, _stridelevels);
  gasnete_boundscheck_strided(_tm, _dstrank, _dstaddr, _dststrides, _count, _stridelevels);
  GASNETI_TRACE_PUTS(PUTS_NBI_BULK,_dstrank,_dstaddr,_dststrides,_srcaddr,_srcstrides,_count,_stridelevels);
  return gasnete_puts(gasnete_synctype_nbi,_tm,_dstrank,_dstaddr,_dststrides,_srcaddr,_srcstrides,_count,_stridelevels,_flags GASNETE_THREAD_PASS) == GEX_EVENT_NO_OP;
}
#define gex_VIS_StridedPutNBI(tm,dstrank,dstaddr,dststrides,srcaddr,srcstrides,elemsz,count,stridelevels,flags) \
       _gex_VIS_StridedPutNBI(tm,dstrank,dstaddr,dststrides,srcaddr,srcstrides,elemsz,count,stridelevels,flags GASNETE_THREAD_GET)

GASNETI_INLINE(_gex_VIS_StridedGetNBI)
int _gex_VIS_StridedGetNBI(
        gex_TM_t _tm,
        void *_dstaddr, const ssize_t __dststrides[],
        gex_Rank_t _srcrank,
        void *_srcaddr, const ssize_t __srcstrides[],
        size_t _elemsz, const size_t _count[], size_t _stridelevels,
        gex_Flags_t _flags GASNETE_THREAD_FARG) {
  GASNETE_STRIDED_BETATHUNK
  gasnete_check_strides(_dststrides, _srcstrides, _count, _stridelevels);
  gasnete_boundscheck_strided(_tm, _srcrank, _srcaddr, _srcstrides, _count, _stridelevels);
  GASNETI_TRACE_GETS(GETS_NBI_BULK,_srcrank,_dstaddr,_dststrides,_srcaddr,_srcstrides,_count,_stridelevels);
  return gasnete_gets(gasnete_synctype_nbi,_tm,_dstaddr,_dststrides,_srcrank,_srcaddr,_srcstrides,_count,_stridelevels,_flags GASNETE_THREAD_PASS) == GEX_EVENT_NO_OP;
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
  //gasnete_check_stridesNT(_dststrides, _srcstrides, _count, _stridelevels);
  _gex_VIS_StridedPutBlocking(_tm,_dstrank,_dstaddr,(ssize_t*)_dststrides,_srcaddr,(ssize_t*)_srcstrides,_count[0],_count+1,_stridelevels,_flags GASNETE_THREAD_PASS);
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
  //gasnete_check_stridesNT(_dststrides, _srcstrides, _count, _stridelevels);
  _gex_VIS_StridedGetBlocking(_tm,_dstaddr,(ssize_t*)_dststrides,_srcrank,_srcaddr,(ssize_t*)_srcstrides,_count[0],_count+1,_stridelevels,_flags GASNETE_THREAD_PASS);
}
GASNETI_INLINE(_gasnet_puts_nb_bulk) GASNETI_WARN_UNUSED_RESULT
gex_Event_t _gasnet_puts_nb_bulk(
        gex_TM_t _tm, gex_Rank_t _dstrank,
        void *_dstaddr, const size_t _dststrides[],
        void *_srcaddr, const size_t _srcstrides[],
        const size_t _count[], size_t _stridelevels,
        gex_Flags_t _flags GASNETE_THREAD_FARG) {
  //gasnete_check_stridesNT(_dststrides, _srcstrides, _count, _stridelevels);
  return _gex_VIS_StridedPutNB(_tm,_dstrank,_dstaddr,(ssize_t*)_dststrides,_srcaddr,(ssize_t*)_srcstrides,_count[0],_count+1,_stridelevels,_flags GASNETE_THREAD_PASS);
}
GASNETI_INLINE(_gasnet_gets_nb_bulk) GASNETI_WARN_UNUSED_RESULT
gex_Event_t _gasnet_gets_nb_bulk(
        gex_TM_t _tm,
        void *_dstaddr, const size_t _dststrides[],
        gex_Rank_t _srcrank,
        void *_srcaddr, const size_t _srcstrides[],
        const size_t _count[], size_t _stridelevels,
        gex_Flags_t _flags GASNETE_THREAD_FARG) {
  //gasnete_check_stridesNT(_dststrides, _srcstrides, _count, _stridelevels);
  return _gex_VIS_StridedGetNB(_tm,_dstaddr,(ssize_t*)_dststrides,_srcrank,_srcaddr,(ssize_t*)_srcstrides,_count[0],_count+1,_stridelevels,_flags GASNETE_THREAD_PASS);
}
GASNETI_INLINE(_gasnet_puts_nbi_bulk)
void _gasnet_puts_nbi_bulk(
        gex_TM_t _tm, gex_Rank_t _dstrank,
        void *_dstaddr, const size_t _dststrides[],
        void *_srcaddr, const size_t _srcstrides[],
        const size_t _count[], size_t _stridelevels,
        gex_Flags_t _flags GASNETE_THREAD_FARG) {
  gasneti_assert(!(_flags & GEX_FLAG_IMMEDIATE));
  //gasnete_check_stridesNT(_dststrides, _srcstrides, _count, _stridelevels);
  _gex_VIS_StridedPutNBI(_tm,_dstrank,_dstaddr,(ssize_t*)_dststrides,_srcaddr,(ssize_t*)_srcstrides,_count[0],_count+1,_stridelevels,_flags GASNETE_THREAD_PASS);
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
  //gasnete_check_stridesNT(_dststrides, _srcstrides, _count, _stridelevels);
  _gex_VIS_StridedGetNBI(_tm,_dstaddr,(ssize_t*)_dststrides,_srcrank,_srcaddr,(ssize_t*)_srcstrides,_count[0],_count+1,_stridelevels,_flags GASNETE_THREAD_PASS);
}

/*---------------------------------------------------------------------------------*/

GASNETI_END_NOWARN
GASNETI_END_EXTERNC

#endif

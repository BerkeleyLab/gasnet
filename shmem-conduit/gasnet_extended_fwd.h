/*   $Source: /Users/kamil/work/gasnet-cvs2/gasnet/shmem-conduit/gasnet_extended_fwd.h,v $
 *     $Date: 2004/10/13 00:18:50 $
 * $Revision: 1.5 $
 * Description: GASNet Extended API Header (forward decls)
 * Copyright 2002, Dan Bonachea <bonachea@cs.berkeley.edu>
 * Terms of use are as specified in license.txt
 */

#ifndef _IN_GASNET_H
  #error This file is not meant to be included directly- clients should include gasnet.h
#endif

#if defined(CRAY_SHMEM) || defined(SGI_SHMEM)
#include <mpp/shmem.h>
#else
#include <shmem.h>
#endif

#include <string.h> /* memcpy */

#ifndef _GASNET_EXTENDED_FWD_H
#define _GASNET_EXTENDED_FWD_H

#define GASNET_EXTENDED_VERSION      0.1
#define GASNET_EXTENDED_VERSION_STR  _STRINGIFY(GASNET_EXTENDED_VERSION)
#define GASNET_EXTENDED_NAME         SHMEM
#define GASNET_EXTENDED_NAME_STR     _STRINGIFY(GASNET_EXTENDED_NAME)

/*
 * Defining GASNETE_NBISYNC_ALWAYS_QUIET causes a quiet to be generated at
 * every nbi sync operation instead of only NBIs that contain puts.  This
 * allows puts to be be completed as 1-store instead of 2-stores.
 */
#ifdef CRAYX1
#define GASNETE_NBISYNC_ALWAYS_QUIET	1
#else
#define GASNETE_NBISYNC_ALWAYS_QUIET	0
#endif

#define _GASNET_HANDLE_T
typedef int *gasnet_handle_t;
#define GASNET_INVALID_HANDLE ((gasnet_handle_t)0)

#define _GASNET_VALGET_HANDLE_T
typedef uintptr_t gasnet_valget_handle_t;

#define _GASNET_REGISTER_VALUE_T
#define SIZEOF_GASNET_REGISTER_VALUE_T SIZEOF_VOID_P
typedef uintptr_t gasnet_register_value_t;

  /* this can be used to add statistical collection values 
     specific to the extended API implementation (see gasnet_help.h) */
#define CONDUIT_EXTENDED_STATS(CNT,VAL,TIME) \
        CNT(C, DYNAMIC_THREADLOOKUP, cnt)    \
	GASNETI_REFVIS_STATS(CNT,VAL,TIME)   \
	GASNETI_REFCOLL_STATS(CNT,VAL,TIME)

#define GASNET_POST_THREADINFO(info)   \
  static uint8_t gasnete_dummy = sizeof(gasnete_dummy) /* prevent a parse error */
#define GASNET_GET_THREADINFO() (NULL)
#define GASNET_BEGIN_FUNCTION() GASNET_POST_THREADINFO(GASNET_GET_THREADINFO())

#define	GASNETE_HANDLE_DONE	    1
#define	GASNETE_HANDLE_NB_POLL	    2
#define	GASNETE_HANDLE_NB_QUIET	    3
#define GASNETE_HANDLE_NBI	    4
#define GASNETE_HANDLE_NBI_POLL	    5

#if defined(CRAY_SHMEM)
  #include <intrinsics.h>
  #include <strings.h>
  #define GASNETE_PRAGMA_IVDEP	_Pragma("_CRI ivdep")

  extern uintptr_t gasnete_pe_bits_shift;
  extern uintptr_t gasnete_addr_bits_mask;

  #define GASNETE_TRANSLATE_X1(addr,pe)				    \
    (void *) ( (((uintptr_t) (addr)) & gasnete_addr_bits_mask) |    \
               (((uintptr_t) (pe)) << gasnete_pe_bits_shift))

  #define GASNETE_SHMPTR_AM(addr,pe) GASNETE_TRANSLATE_X1(addr,pe)

  #ifdef GASNET_SEGMENT_EVERYTHING
    #define GASNETE_SHMPTR(addr,pe) GASNETE_TRANSLATE_X1(addr,pe)
  #else
    #define GASNETE_SHMPTR(addr,pe) (addr)
  #endif

#elif defined(SGI_SHMEM)
  #ifdef GASNET_SEGMENT_EVERYTHING
    #define GASNETE_SHMPTR(addr,pe) shmem_ptr(addr,pe)
    #define GASNETE_SHMPTR_AM(addr,pe) shmem_ptr(addr,pe)
  #else
    #define GASNETE_SHMPTR(addr,pe) (addr)
    #define GASNETE_SHMPTR_AM(addr,pe) (addr)
  #endif

  #define GASNETE_PRAGMA_IVDEP	  /* no ivdep is useful here */
#endif

#if GASNETE_NBISYNC_ALWAYS_QUIET
#define GASNETE_NBISYNC_HAS_PUT
#else
#define GASNETE_NBISYNC_HAS_PUT	(gasnete_nbi_sync = 1)
#endif

/*
 * On X1, we attempt to vectorize for certain sizes
 * XXX this is not yet enabled
 */
#ifdef CRAYX1
  #define GASNETE_VECTOR_THRESHOLD    80

  #ifdef GASNETE_ENABLE_8_BYTE_VECTOR
    #define _GASNETE_DESTSRC_ALIGNED_8_BULK(dest,src,nbytes)			  \
	    (!(((uintptr_t)dest)&0x7) && !(((uintptr_t)src)&0x7) && !(nbytes&0x7))
    #define _GASNETE_DESTSRC_ALIGNED_8_NONBULK(dest,src,nbytes)			  \
	    (gasneti_assert(!(((uintptr_t)dest)&0x7) && !(((uintptr_t)src)&0x7)), \
	     !(nbytes&0x7))
  #else
    #define _GASNETE_DESTSRC_ALIGNED_8_BULK(dest,src) 0
    #define _GASNETE_DESTSRC_ALIGNED_8_NONBULK(dest,src) 0
  #endif

  #define _GASNETE_DESTSRC_ALIGNED_4_BULK(dest,src,nbytes)			\
	  (!(((uintptr_t)dest)&0x3) && !(((uintptr_t)src)&0x3) && !(nbytes&0x3))
  #define _GASNETE_DESTSRC_ALIGNED_4_NONBULK(dest,src,nbytes)			\
	  (gasneti_assert(!(((uintptr_t)dest)&0x3) && !(((uintptr_t)src)&0x3)), \
	  !(nbytes&0x3))

  #define _GASNETE_INLINE_VECTOR_LOOP(dest,src,nbytes,shift,type)   \
	do {							    \
	    unsigned long i, sz;				    \
	    sz = ((unsigned long)(nbytes))>>(shift);		    \
	    for (i=0; i<sz; i++) { 				    \
		((type *)dest)[i] = ((type *)src)[i];		    \
	    }							    \
	} while (0)

  #define _GASNETE_INLINE_VECTOR_LDST(dest,src,nbytes,bulk)	    \
	if (_GASNETE_DESTSRC_ALIGNED_8_ ## bulk(dest,src) &&	    \
	    nbytes <= GASNETE_VECTOR_THRESHOLD)			    \
	    _GASNET_INLINE_VECTOR_LOOP(dest,src,nbytes,3,uint64_t); \
	else if (_GASNETE_DESTSRC_ALIGNED_4_ ## bulk(dest,src) &&   \
	    nbytes <= GASNETE_VECTOR_THRESHOLD)			    \
	    _GASNET_INLINE_VECTOR_LOOP(dest,src,nbytes,2,uint32_t); \
	else							    \
	    bcopy(src,dest,nbytes)
#else
  #define _GASNETE_INLINE_VECTOR_LDST(dest,src,nbytes,bulk)	    \
	    memcpy(dest,src,nbytes)
#endif

#ifdef CRAYX1
#define _GASNETE_CRAYX1_ONLY(x)  x
#else
#define _GASNETE_CRAYX1_ONLY(x)
#endif
	    
#define _gasnete_global_ldst(dest,src,nbytes)			    \
	do {							    \
	    uint64_t *pDest = (uint64_t *)dest;			    \
	    uint64_t *pSrc = (uint64_t *)src;			    \
	    switch(nbytes) {					    \
		_GASNETE_CRAYX1_ONLY(				    \
		case 64:					    \
		    pDest[0] = pSrc[0];	pDest[1] = pSrc[1];	    \
		    pDest[2] = pSrc[2];	pDest[3] = pSrc[3];	    \
		    pDest[4] = pSrc[4];	pDest[5] = pSrc[5];	    \
		    pDest[6] = pSrc[6];	pDest[7] = pSrc[7];	    \
		    break;					    \
		case 56:					    \
		    pDest[0] = pSrc[0];	pDest[1] = pSrc[1];	    \
		    pDest[2] = pSrc[2];	pDest[3] = pSrc[3];	    \
		    pDest[4] = pSrc[4];	pDest[5] = pSrc[5];	    \
		    pDest[6] = pSrc[6];				    \
		    break;					    \
		case 48:					    \
		    pDest[0] = pSrc[0];	pDest[1] = pSrc[1];	    \
		    pDest[2] = pSrc[2];	pDest[3] = pSrc[3];	    \
		    pDest[4] = pSrc[4];	pDest[5] = pSrc[5];	    \
		    break;					    \
		case 40:					    \
		    pDest[0] = pSrc[0];	pDest[1] = pSrc[1];	    \
		    pDest[2] = pSrc[2];	pDest[3] = pSrc[3];	    \
		    pDest[4] = pSrc[4];				    \
		    break;					    \
		case 32:					    \
		    pDest[0] = pSrc[0];	pDest[1] = pSrc[1];	    \
		    pDest[2] = pSrc[2];	pDest[3] = pSrc[3];	    \
		    break;					    \
		case 24:					    \
		    pDest[0] = pSrc[0];	pDest[1] = pSrc[1];	    \
		    pDest[2] = pSrc[2];				    \
		    break;					    \
		case 16:					    \
		    pDest[0] = pSrc[0];	pDest[1] = pSrc[1];	    \
		    break;					    \
		)						    \
		case 8:						    \
		    pDest[0] = pSrc[0];				    \
		    break;					    \
		case 4:						    \
		    *((uint32_t *)dest) = *((uint32_t *)src);	    \
		    break;					    \
		case 2:						    \
		    *((uint16_t *)dest) = *((uint16_t *)src);	    \
		    break;					    \
		case 1:						    \
		    *((uint8_t *)dest) = *((uint8_t *)src);	    \
		    break;					    \
		default:					    \
		    _GASNETE_CRAYX1_ONLY(			    \
		    if (nbytes <= 256 && !(nbytes&0x7))		    \
			_GASNETE_INLINE_VECTOR_LOOP(dest,src,nbytes,\
					               3,uint64_t); \
		    else					    \
		    )						    \
			bcopy(src, dest, nbytes);		    \
		    break;					    \
	    }							    \
	} while (0)

#ifdef CRAYX1
  /*
   * X1 is more picky about alignment.  Size of the dereference must 
   * match it's alignment boundary.
   */
  #define _GASNETE_DESTSRC_ALIGNED(dest,src,al)			    \
	    (!(((uintptr_t)dest)&(al)) && !(((uintptr_t)src)&(al)))

  #define _gasnete_x1_global_ldst_bulk(dest,src,nbytes)		    \
	do {							    \
	    uint64_t *pDest = (uint64_t *)dest;			    \
	    uint64_t *pSrc = (uint64_t *)src;			    \
	    switch(nbytes) {					    \
		case 8:						    \
		    if (_GASNETE_DESTSRC_ALIGNED(dest,src,0x7))	    \
			*((uint64_t *)dest) = *((uint64_t *)src);   \
		    else					    \
			memcpy(dest,src,nbytes);		    \
		    break;					    \
		case 4:						    \
		    if (_GASNETE_DESTSRC_ALIGNED(dest,src,0x3))	    \
			*((uint32_t *)dest) = *((uint32_t *)src);   \
		    else					    \
			memcpy(dest,src,nbytes);		    \
		    break;					    \
		case 2:						    \
		    if (_GASNETE_DESTSRC_ALIGNED(dest,src,0x1))	    \
			*((uint16_t *)dest) = *((uint16_t *)src);   \
		    else					    \
			memcpy(dest,src,nbytes);		    \
		    break;					    \
		case 1:						    \
		    *((uint8_t *)dest) = *((uint8_t *)src);	    \
		    break;					    \
		default:					    \
		    if (_GASNETE_DESTSRC_ALIGNED(dest,src,0x7) &&   \
			nbytes <= 256 && !(nbytes&0x7))	            \
			_GASNETE_INLINE_VECTOR_LOOP(dest,src,nbytes,\
					               3,uint64_t); \
		    else					    \
			bcopy(src, dest, nbytes);		    \
		    break;					    \
	    }							    \
	} while (0)

  #define _gasnete_global_ldst_bulk _gasnete_x1_global_ldst_bulk
#else
  #define _gasnete_global_ldst_bulk _gasnete_global_ldst
#endif

#define gasnete_global_get(dest,src,nbytes,pe)			    \
	    _gasnete_global_ldst(dest,GASNETE_SHMPTR(src,pe),nbytes)
#define gasnete_global_get_bulk(dest,src,nbytes,pe)		    \
	    _gasnete_global_ldst_bulk(dest,GASNETE_SHMPTR(src,pe),nbytes)
#define gasnete_global_put(dest,src,nbytes,pe)			    \
	    _gasnete_global_ldst(GASNETE_SHMPTR(dest,pe),src,nbytes)
#define gasnete_global_put_bulk(dest,src,nbytes,pe)		    \
	    _gasnete_global_ldst_bulk(GASNETE_SHMPTR(dest,pe),src,nbytes)

/* 
 * Blocking 
 */
#ifdef GASNETE_GLOBAL_ADDRESS
  #define gasnete_put(pe,dest,src,nbytes)		    \
	  do { gasnete_global_put(dest,src,nbytes,pe); shmem_quiet(); } while (0)
  #define gasnete_put_bulk(pe,dest,src,nbytes)		    \
	  do { gasnete_global_put_bulk(dest,src,nbytes,pe); shmem_quiet(); } while (0)
  #define gasnete_get(dest,pe,src,nbytes)      gasnete_global_get(dest,src,nbytes,pe)
  #define gasnete_get_bulk(dest,pe,src,nbytes) gasnete_global_get_bulk(dest,src,nbytes,pe)
#else
  #define gasnete_put(pe,dest,src,nbytes)      shmem_putmem(dest,src,nbytes,pe)
  #define gasnete_put_bulk(pe,dest,src,nbytes) shmem_putmem(dest,src,nbytes,pe)
  #define gasnete_get(dest,pe,src,nbytes)      shmem_getmem(dest,src,nbytes,pe)
  #define gasnete_get_bulk(dest,pe,src,nbytes) shmem_getmem(dest,src,nbytes,pe)
#endif

#define gasnete_putTI gasnete_put

/*
 * NBI
 */
extern int	    gasnete_nbi_sync;
extern int	    gasnete_handles[];
extern int	    gasnete_handleno_cur;
extern int	    gasnete_handleno_phase;

#ifdef GASNETE_GLOBAL_ADDRESS
  #define gasnete_put_nbi(pe,dest,src,nbytes)			\
	    do { gasnete_global_put(dest,src,nbytes,pe);	\
		 GASNETE_NBISYNC_HAS_PUT;			\
	    } while (0)
  #define gasnete_put_nbi_bulk(pe,dest,src,nbytes)		\
	    do { gasnete_global_put_bulk(dest,src,nbytes,pe);	\
		 GASNETE_NBISYNC_HAS_PUT;			\
	    } while (0)

  #define gasnete_get_nbi(dest,pe,src,nbytes)	   gasnete_global_get(dest,src,nbytes,pe)
  #define gasnete_get_nbi_bulk(dest,pe,src,nbytes) gasnete_global_get_bulk(dest,src,nbytes,pe)
#else
  #define gasnete_put_nbi(pe,dest,src,nbytes)			\
	    do { shmem_putmem(dest,src,nbytes,pe);		\
		 GASNETE_NBISYNC_HAS_PUT;			\
	    } while (0)
  #define gasnete_put_nbi_bulk gasnete_put_nbi

  #define gasnete_get_nbi(dest,pe,src,nbytes)	   shmem_getmem(dest,src,nbytes,pe)
  #define gasnete_get_nbi_bulk(dest,pe,src,nbytes) shmem_getmem(dest,src,nbytes,pe)
#endif

/*
 * NB
 */
#ifdef GASNETE_GLOBAL_ADDRESS

GASNET_INLINE_MODIFIER(_gasnete_put_nb_bulk)
gasnet_handle_t 
_gasnete_put_nb_bulk(gasnet_node_t node, void *dest, void *src, 
		    size_t nbytes) 
{
    gasnete_global_put_bulk(dest,src,nbytes,node);
    gasnete_handles[gasnete_handleno_phase] = GASNETE_HANDLE_NB_QUIET;
    return &gasnete_handles[gasnete_handleno_phase];
}
#define gasnete_put_nb_bulk(pe,dest,src,nbytes) _gasnete_put_nb_bulk(pe,dest,src,nbytes)
#else
  #define gasnete_put(pe,dest,src,nbytes)				    \
	    do { shmem_putmem(dest,src,nbytes,pe); shmem_quiet(); } while (0)
  extern gasnet_handle_t 
         gasnete_shmem_put_nb_bulk(gasnet_node_t node, void *dest, void *src, size_t nbytes);

  #define gasnete_put_nb_bulk(pe,dest,src,nbytes) gasnete_shmem_put_nb_bulk(pe,dest,src,nbytes)
#endif


/*
 * Memsets on global addresses
 */
#ifdef GASNETE_GLOBAL_ADDRESS
  extern gasnet_handle_t
         gasnete_global_memset_nb(gasnet_node_t node, void *dest, int val, size_t nbytes);
  #define gasnete_memset_nb(node,dest,val,nbytes) \
	    gasnete_global_memset_nb(node,GASNETE_SHMPTR(dest,node),val,nbytes)

  extern void 
  gasnete_global_memset_nbi(gasnet_node_t node, void *dest, int val, size_t nbytes);
  #define gasnete_memset_nbi(node,dest,val,nbytes) \
	    gasnete_global_memset_nbi(node,GASNETE_SHMPTR(dest,node),val,nbytes)
#else
  extern gasnet_handle_t
         gasnete_am_memset_nb(gasnet_node_t node, void *dest, int val, size_t nbytes);
  extern void 
         gasnete_am_memset_nbi(gasnet_node_t node, void *dest, int val, size_t nbytes); 
  #define gasnete_memset_nb  gasnete_am_memset_nb
  #define gasnete_memset_nbi gasnete_am_memset_nbi
#endif


/*
 * Non-bulk are the same as bulk, except on X1
 */

#ifdef CRAYX1
  GASNET_INLINE_MODIFIER(_gasnete_get_nb)
  gasnet_handle_t 
  _gasnete_get_nb(void *dest, gasnet_node_t node, void *src, size_t nbytes)
  {
    gasnete_global_get(dest,src,nbytes,node);
    return (gasnet_handle_t) 0;
  }

  GASNET_INLINE_MODIFIER(_gasnete_put_nb)
  gasnet_handle_t 
  _gasnete_put_nb(gasnet_node_t node, void *dest, void *src, size_t nbytes)
  {
    gasnete_global_put(dest,src,nbytes,node);
    gasnete_handles[gasnete_handleno_phase] = GASNETE_HANDLE_NB_QUIET;
    return &gasnete_handles[gasnete_handleno_phase];
  }
  #define gasnete_get_nb	_gasnete_get_nb
  #define gasnete_put_nb	_gasnete_put_nb
#else
  #define gasnete_get_nb	gasnete_get_nb_bulk
  #define gasnete_put_nb	gasnete_put_nb_bulk
#endif

/* 
 * Some sync ops are the same
 */
#define gasnete_try_syncnb_all gasnete_try_syncnb_some

/* 
 * Value gets and puts are more tricky
 *
 * They can't map directly to shmem functions as the gasnet interface too
 * general to map to the type-specific elemental shmem_g and shmem_p variants.
 *
 */
#define GASNETI_DIRECT_PUT_VAL     1
#define GASNETI_DIRECT_PUT_NB_VAL  1
#define GASNETI_DIRECT_PUT_NBI_VAL 1

/*
  Non-Blocking Value Get (explicit-handle)
  ========================================
*/

#if SIZEOF_LONG == 8
#define GASNET_SHMEM_GET_8  shmem_long_g
#define GASNET_SHMEM_PUT_8  shmem_long_p
#elif SIZEOF_DOUBLE == 8
#define GASNET_SHMEM_GET_8  shmem_double_g
#define GASNET_SHMEM_PUT_8  shmem_double_p
#endif

#if SIZEOF_LONG == 4
#define GASNET_SHMEM_GET_4  shmem_long_g
#define GASNET_SHMEM_PUT_4  shmem_long_p
#elif SIZEOF_INT == 4
#define GASNET_SHMEM_GET_4  shmem_int_g
#define GASNET_SHMEM_PUT_4  shmem_int_p
#elif SIZEOF_SHORT == 4
#define GASNET_SHMEM_GET_4  shmem_short_g
#define GASNET_SHMEM_PUT_4  shmem_short_p
#elif SIZEOF_FLOAT == 4
#define GASNET_SHMEM_GET_4  shmem_float_g
#define GASNET_SHMEM_PUT_4  shmem_float_p
#endif

#if SIZEOF_SHORT == 2
#define GASNET_SHMEM_GET_2  shmem_short_g
#define GASNET_SHMEM_PUT_2  shmem_short_p
#endif

#ifdef GASNETE_GLOBAL_ADDRESS
GASNET_INLINE_MODIFIER(gasnete_get_nb_val)
gasnet_valget_handle_t 
_gasnete_get_nb_val(gasnet_node_t node, void *src, 
		   size_t nbytes) 
{
    switch (nbytes) {
	case 8:
	    return (gasnet_valget_handle_t) 
		    *((uint64_t *) GASNETE_SHMPTR(src,node));
	case 4:
	    return (gasnet_valget_handle_t) 
		    *((uint32_t *) GASNETE_SHMPTR(src,node));
	case 2:
	    return (gasnet_valget_handle_t) 
		    *((uint16_t *) GASNETE_SHMPTR(src,node));
	case 1:
	    return (gasnet_valget_handle_t) 
		    *((uint8_t *) GASNETE_SHMPTR(src,node));
	default:
	    return (gasnet_valget_handle_t) 0;
	    #if 0
	    gasneti_fatalerror(
		"VIOLATION: Unsupported size %d in valget", 
		nbytes);
	    #endif
	    break;
    }
    return (gasnet_valget_handle_t) 0;
}
#define gasnete_get_nb_val _gasnete_get_nb_val

#else /* !GASNETE_GLOBAL_ADDRESS */

GASNET_INLINE_MODIFIER(gasnete_get_nb_val)
gasnet_valget_handle_t 
gasnete_get_nb_val(gasnet_node_t node, void *src, 
		   size_t nbytes) 
{
    switch (nbytes) {
	case 8:	
	    #ifdef GASNET_SHMEM_GET_8
		return (gasnet_valget_handle_t) GASNET_SHMEM_GET_8(src, node);
	    #else
	    {
		static uint64_t	temp64;
		shmem_getmem((void *) &temp64,src,8,node);
		return (gasnet_valget_handle_t) temp64;
	    }
	    #endif

	case 4: 
	    #ifdef GASNET_SHMEM_GET_4
		return (gasnet_valget_handle_t) GASNET_SHMEM_GET_4(src, node);
	    #else
	    {
		static uint32_t	temp32;
		shmem_getmem((void *) &temp32,src,4,node);
		return (gasnet_valget_handle_t) temp32;
	    }
	    #endif

	case 2: 
	    #ifdef GASNET_SHMEM_GET_2
		return (gasnet_valget_handle_t) GASNET_SHMEM_GET_2(src, node);
	    #else
	    {
		static uint16_t temp16;
		shmem_getmem((void *) &temp16,src,2,node);
		return (gasnet_valget_handle_t) temp16;
	    }
	    #endif
	case 1:
	    {
		uint8_t	val;
		val = *((uint8_t *) shmem_ptr(src,node));
		return (gasnet_valget_handle_t) val;
	    }
#if 0
		shmem_getmem((void *) &temp8,src,1,node);
		return (gasnet_valget_handle_t) temp8;
#endif

	case 0: return 0;
	default:
	    {
		static uint64_t	tempA;
		#if 0 && defined(GASNET_DEBUG)
		if (nbytes > sizeof(gasnet_register_value_t))
		      gasneti_fatalerror(
			"VIOLATION: Unsupported size %d in valget", nbytes);
		#endif
		shmem_getmem((void *) &tempA, src, nbytes, node);
		return (gasnet_valget_handle_t) tempA;
	    }
    }
}

#endif

/* 
 * Since shmem only has blocking valgets, we use the value as the handle and
 * the resulting value.
 */
GASNET_INLINE_MODIFIER(gasnete_wait_syncnb_valget)
gasnet_register_value_t 
gasnete_wait_syncnb_valget(gasnet_valget_handle_t handle) 
{
    return (gasnet_register_value_t) handle;
}

#define gasnete_get_val	(gasnet_register_value_t) gasnete_get_nb_val

/* ------------------------------------------------------------------------------------ */
/*
  Non-Blocking and Blocking Value Put 
  ====================================
*/
#ifdef GASNETE_GLOBAL_ADDRESS
GASNET_INLINE_MODIFIER(gasnet_put_val_inner)
void 
gasnete_put_val_inner(gasnet_node_t node, void *dest, 
		      gasnet_register_value_t value, 
		      size_t nbytes)
{
    switch (nbytes) {
	case 8:
	    *((uint64_t *)GASNETE_SHMPTR(dest,node)) = (uint64_t)value;
	    return;
	case 4:
	    *((uint32_t *)GASNETE_SHMPTR(dest,node)) = (uint32_t)value;
	    return;
	case 2:
	    *((uint16_t *)GASNETE_SHMPTR(dest,node)) = (uint16_t)value;
	    return;
	case 1:
	    *((uint8_t *)GASNETE_SHMPTR(dest,node)) = (uint8_t)value;
	    return;
	default:
	    #if 0
	    gasneti_fatalerror(
		"VIOLATION: Unsupported size %d in valput", 
		nbytes);
	    #endif
	    break;
    }
    return;
}
#else
GASNET_INLINE_MODIFIER(gasnet_put_val_inner)
void 
gasnete_put_val_inner(gasnet_node_t node, void *dest, 
		      gasnet_register_value_t value, 
		      size_t nbytes)
{
    static char	val_put[8];

    switch (nbytes) {
    #ifdef GASNET_SHMEM_PUT_8
	case 8:	GASNET_SHMEM_PUT_8(dest, value, node); return;
    #endif
    #ifdef GASNET_SHMEM_PUT_4
	case 4: GASNET_SHMEM_PUT_4(dest, value, node); return;
    #endif
    #ifdef GASNET_SHMEM_PUT_2
	case 2: GASNET_SHMEM_PUT_2(dest, value, node); return;
    #endif
	case 0: return;
	default:
	    #if 0 && defined(GASNET_DEBUG)
	      if (nbytes > sizeof(gasnet_register_value_t))
		      gasneti_fatalerror(
			"VIOLATION: Unsupported size %d in valput", nbytes);
	    #endif
	    memcpy(val_put, &value, nbytes);
	    shmem_putmem(dest, val_put, nbytes, node);
	    return;
    }
}
#endif

GASNET_INLINE_MODIFIER(gasnete_put_val)
void 
_gasnete_put_val(gasnet_node_t node, void *dest, gasnet_register_value_t value, 
		size_t nbytes)
{
    gasnete_put_val_inner(node, dest, value, nbytes);
#ifndef GASNETE_GLOBAL_ADDRESS
    shmem_quiet();
#endif
}
#define gasnete_put_val _gasnete_put_val

GASNET_INLINE_MODIFIER(gasnete_put_nb_val)
gasnet_handle_t 
_gasnete_put_nb_val(gasnet_node_t node, void *dest, gasnet_register_value_t value, 
		    size_t nbytes)
{
    gasnete_put_val_inner(node, dest, value, nbytes);
    gasnete_handles[gasnete_handleno_phase] = GASNETE_HANDLE_NB_QUIET;
    return &gasnete_handles[gasnete_handleno_phase];
}
#define gasnete_put_nb_val _gasnete_put_nb_val 

GASNET_INLINE_MODIFIER(gasnete_put_nb_val)
void 
_gasnete_put_nbi_val(gasnet_node_t node, void *dest, 
		    gasnet_register_value_t value, 
		    size_t nbytes)
{
    gasnete_put_val_inner(node, dest, value, nbytes);
    gasnete_nbi_sync = 1;
    return;
}
#define gasnete_put_nbi_val _gasnete_put_nbi_val 

#endif


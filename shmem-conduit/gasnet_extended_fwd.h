/*  $Archive:: /Ti/GASNet/extended/gasnet_extended_fwd.h                  $
 *     $Date: 2004/03/11 11:19:14 $
 * $Revision: 1.2 $
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

#ifndef _GASNET_EXTENDED_FWD_H
#define _GASNET_EXTENDED_FWD_H

#define GASNET_EXTENDED_VERSION      0.1
#define GASNET_EXTENDED_VERSION_STR  _STRINGIFY(GASNET_EXTENDED_VERSION)
#define GASNET_EXTENDED_NAME         SHMEM
#define GASNET_EXTENDED_NAME_STR     _STRINGIFY(GASNET_EXTENDED_NAME)


#define _GASNET_HANDLE_T
typedef int *gasnet_handle_t;
#define GASNET_INVALID_HANDLE ((gasnet_handle_t)0)

#define _GASNET_VALGET_HANDLE_T
typedef uintptr_t gasnet_valget_handle_t;

#define _GASNET_REGISTER_VALUE_T
#define SIZEOF_GASNET_REGISTER_VALUE_T SIZEOF_VOID_p
typedef uintptr_t gasnet_register_value_t;


  /* this can be used to add statistical collection values 
     specific to the extended API implementation (see gasnet_help.h) */
#define CONDUIT_EXTENDED_STATS(CNT,VAL,TIME) \
        CNT(C, DYNAMIC_THREADLOOKUP, cnt)           

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

  /*
   * Some clients, such as the UPC Runtime, issue puts/gets on global addresses
   */
  #ifdef GASNETE_GLOBAL_ADDRESS
    #define GASNETE_SHMPTR(addr,pe)
  #else
    #define GASNETE_SHMPTR(addr,pe) GASNETE_TRANSLATE_X1(addr,pe)
  #endif

  #define GASNETE_SHMPTR_AM GASNETE_TRANSLATE_X1

#elif defined(SGI_SHMEM)
  extern intptr_t   *gasnetc_segment_shptr_off;

  #ifdef GASNETE_GLOBAL_ADDRESS
    #define GASNETE_SHMPTR(addr,pe)
  #else
    #define GASNETE_SHMPTR(addr,pe) shmem_ptr(addr,pe)
  #endif

  #define GASNETE_SHMPTR_AM(addr,pe)				    \
	 ((void *)(((intptr_t)(addr)+gasnetc_segment_shptr_off[pe])))
  #define GASNETE_PRAGMA_IVDEP	  /* no ivdep is useful here */
#endif

/*
 * A generic approach for load/store based puts and gets.  We define thresholds
 * for which to prefer 
 */
#define GASNETE_GET_BCOPY_THRESH_uint64_t   80
#define GASNETE_GET_BCOPY_THRESH_uint8_t    16
#define GASNETE_GET_BCOPY_THRESH_void	    0

#define GASNETE_PUT_BCOPY_THRESH_uint64_t   80
#define GASNETE_PUT_BCOPY_THRESH_uint8_t    16
#define GASNETE_PUT_BCOPY_THRESH_void	    0

#define gasnete_inline_ldst_generic(PG,TYPE,TRG,SRC,LEN,PE)	    \
	do {							    \
	    void *ptr = (void *)SRC;				    \
	    ptrdiff_t i;					    \
	    size_t size = LEN;					    \
	    if (size <= GASNETE_ ## PG ## _BCOPY_THRESH_ ## TYPE) { \
		GASNETE_PRAGMA_IVDEP 				    \
		for (i=0; i<size; i++)				    \
		    ((TYPE * )TRG)[i] = ((TYPE * )ptr)[i];	    \
	    }							    \
	    else						    \
               bcopy((void *)ptr, TRG, size * sizeof(TYPE));	    \
	} while (0)

#define gasnete_global_put(dest,src,nbytes)			    \
	do {							    \
	    switch(nbytes) {					    \
		case 8:						    \
		    *((uint64_t *)dest) = *((uint64_t *)src);	    \
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
		    memcpy(dest, src, nbytes);			    \
		    break;					    \
	    }							    \
	} while (0)

#define gasnete_global_get(dest,src,nbytes) gasnete_global_put(dest,src,nbytes)

#define gasnete_inline_ldst_put(TYPE,TRG,SRC,LEN,PE)	    \
	    gasnete_inline_ldst_generic( \
		    PUT,TYPE,GASNETE_SHMPTR(TRG,PE),SRC,LEN,PE)

#define gasnete_inline_ldst_get(TYPE,TRG,SRC,LEN,PE)	    \
	    gasnete_inline_ldst_generic( \
		    GET,TYPE,TRG,GASNETE_SHMPTR(SRC,PE),LEN,PE)

/* 
 * Blocking operations map directly to shmem functions
 *
 */
#ifdef GASNETE_GLOBAL_ADDRESS
#define gasnete_get(dest,pe,src,nbytes)	gasnete_global_put(dest,src,nbytes)
#else
#define gasnete_get(dest,pe,src,nbytes) shmem_getmem(dest,src,nbytes,pe)
#endif

#define gasnete_get_bulk	    gasnete_get

#ifdef GASNETE_GLOBAL_ADDRESS
#define gasnete_put(pe,dest,src,nbytes)	gasnete_global_put(dest,src,nbytes)
#else
#define gasnete_put(pe,dest,src,nbytes)				    \
	    do { shmem_putmem(dest,src,nbytes,pe); shmem_quiet(); } while (0)
#endif

#define gasnete_putTI gasnete_put
#define gasnete_put_bulk gasnete_put

/*
 * Implicit ops also map directly to shmem functions.
 *
 * The NBI will require synchronization if the nbi region contains a put or a
 * memset.
 */
extern int	    gasnete_nbi_sync;
extern int	    gasnete_handles[];
extern int	    gasnete_handleno_cur;
extern int	    gasnete_handleno_phase;

#ifdef GASNETE_GLOBAL_ADDRESS
#define gasnete_put_nbi(pe,dest,src,nbytes)		 \
	    gasnete_global_put(dest,src,nbytes)
#else
#define gasnete_put_nbi(pe,dest,src,nbytes)		 \
	    do { shmem_putmem(dest,src,nbytes,node);	 \
		 gasnete_nbi_sync = 1;			 \
	    } while (0)
#endif

#define gasnete_put_nbi_bulk gasnete_put_nbi

#ifdef GASNETE_GLOBAL_ADDRESS
#define gasnete_get_nbi_bulk(dest,pe,src,nbytes)	\
	    gasnete_global_put(dest,src,nbytes)
#else
#define gasnete_get_nbi_bulk(dest,pe,src,nbytes)	\
	    shmem_getmem(dest,src,nbytes,node)
#endif

/* get_nbi is already defined as get_nbi_bulk */

/*
 * Non-bulk are the same as bulk, except on X1
 */

#ifdef CRAYX1
  GASNET_INLINE_MODIFIER(gasnete_get_nb)
  gasnet_handle_t 
  _gasnete_get_nb(void *dest, gasnet_node_t node, void *src, size_t nbytes)
  {
    gasnete_global_get(dest,src,nbytes);
    return (gasnet_handle_t) 0;
  }

  GASNET_INLINE_MODIFIER(gasnete_put_nb)
  gasnet_handle_t 
  _gasnete_put_nb(gasnet_node_t node, void *dest, void *src, size_t nbytes)
  {
    gasnete_global_put(dest,src,nbytes);
    return (gasnet_handle_t) 0;
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
#warning global address inner for gets
GASNET_INLINE_MODIFIER(gasnete_get_nb_val)
gasnet_valget_handle_t 
_gasnete_get_nb_val(gasnet_node_t node, void *src, 
		   size_t nbytes) 
{
    switch (nbytes) {
	case 8:
	    return (gasnet_valget_handle_t) *((uint64_t *) src);
	case 4:
	    return (gasnet_valget_handle_t) *((uint32_t *) src);
	case 2:
	    return (gasnet_valget_handle_t) *((uint16_t *) src);
	case 1:
	    return (gasnet_valget_handle_t) *((uint8_t *) src);
	default:
	    abort();
	    /*
	    gasneti_fatalerror(
		"VIOLATION: Unsupported size %d in valget", 
		nbytes);
		*/
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
		#if GASNET_DEBUG
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
#warning global address inner
GASNET_INLINE_MODIFIER(gasnet_put_val_inner)
void 
gasnete_put_val_inner(gasnet_node_t node, void *dest, 
		      gasnet_register_value_t value, 
		      size_t nbytes)
{
    switch (nbytes) {
	case 8:
	    *((uint64_t *)dest) = (uint64_t)value;
	    return;
	case 4:
	    *((uint32_t *)dest) = (uint32_t)value;
	    return;
	case 2:
	    *((uint16_t *)dest) = (uint16_t)value;
	    return;
	case 1:
	    *((uint8_t *)dest) = (uint8_t)value;
	    return;
	default:
	    gasneti_fatalerror(
		"VIOLATION: Unsupported size %d in valput", 
		nbytes);
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
	    #if GASNET_DEBUG
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


/*  $Archive:: /Ti/GASNet/extended/gasnet_extended_fwd.h                  $
 *     $Date: 2003/11/23 12:58:50 $
 * $Revision: 1.1.2.3 $
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

/*
 * XXX The segment is not always incore on sgi, but I'm still looking for a way
 * to find out that the machine is an Altix 3000 at compile time!
 */
#if defined(CRAY_SHMEM)
  #include <intrinsics.h>
  #include <strings.h>
  #define GASNETE_SHMALLOC_SEGMENT
  #define GASNETE_SHMALLOC_LD_ST
  #define GASNETE_PRAGMA_IVDEP	_Pragma("_CRI ivdep")

  extern uintptr_t gasnete_pe_bits_shift;
  extern uintptr_t gasnete_addr_bits_mask;
  #define GASNETE_SHMPTR(addr,pe)					    \
	    (void *) ( (((uintptr_t) (addr)) & gasnete_addr_bits_mask) |    \
	               (((uintptr_t) (pe)) << gasnete_pe_bits_shift))
  #define GASNETE_SHMPTR_AM GASNETE_SHMPTR

#elif defined(SGI_SHMEM)
  #define GASNETE_SHMALLOC_SEGMENT
  #define GASNETE_SHMALLOC_LD_ST
  extern intptr_t   *gasnetc_segment_shptr_off;
  /* Still unable to decipher SGI's SHMPTR */
  #define GASNETE_SHMPTR(addr,pe)   shmem_ptr(addr,pe)
  #define GASNETE_SHMPTR_AM(addr,pe)				    \
	 ((void *)(((intptr_t)(addr)+gasnetc_segment_shptr_off[pe])))
  #define GASNETE_PRAGMA_IVDEP
#endif

/*
 * A generic approach for load/store based puts and gets.  We define thresholds
 * for which to prefer 
 */
#define GASNETE_GET_BCOPY_THRESH_uint64_t   80
#define GASNETE_GET_BCOPY_THRESH_uint8_t    16

#define GASNETE_PUT_BCOPY_THRESH_uint64_t   80
#define GASNETE_PUT_BCOPY_THRESH_uint8_t    16

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
#define GASNETI_INLINE_GET 1
#ifdef GASNETE_SHMALLOC_LD_ST
#define gasnete_inline_get(dest,pe,src,nbytes)			      \
	    gasnete_inline_ldst_get(uint8_t,dest,src,nbytes,pe)
#else
#define gasnete_inline_get(dest,pe,src,nbytes) shmem_getmem(dest,src,nbytes,pe)
#endif

#define GASNETI_INLINE_GET_BULK 1
#define gasnete_inline_get_bulk	    gasnete_inline_get

#define GASNETI_INLINE_PUT 1
#ifdef GASNETE_SHMALLOC_LD_ST
#define gasnete_inline_put(pe,dest,src,nbytes)				    \
	    do { gasnete_inline_ldst_put(uint8_t,dest,src,nbytes,pe); shmem_quiet(); } while (0)
#else
#define gasnete_inline_put(pe,dest,src,nbytes)				    \
	    do { shmem_putmem(dest,src,nbytes,pe); shmem_quiet(); } while (0)
#endif

#define GASNETI_INLINE_PUT_BULK 1
#define gasnete_inline_put_bulk gasnete_inline_put

/*
 * Implicit ops also map directly to shmem functions.
 *
 * The NBI will require synchronization if the nbi region contains a put or a
 * memset.
 */
extern int	    gasnete_nbi_sync;

#define GASNETI_INLINE_PUT_NBI	1
#define gasnete_inline_put_nbi(pe,dest,src,nbytes)		 \
	    do { shmem_putmem(dest,src,nbytes,node);		 \
		 gasnete_nbi_sync = 1;				 \
	    } while (0)

#define GASNETI_INLINE_PUT_NBI_BULK 1
#define gasnete_inline_put_nbi_bulk gasnete_inline_put_nbi

#define GASNETI_INLINE_GET_NBI_BULK 1
#define gasnete_inline_get_nbi_bulk(dest,pe,src,nbytes)		\
	    shmem_getmem(dest,src,nbytes,node)

/* get_nbi is already defined as get_nbi_bulk */

/*
 * Non-bulk are the same as bulk
 */
#define GASNETI_INLINE_GET_NB 1
#define gasnete_inline_get_nb	gasnete_get_nb_bulk
#define GASNETI_INLINE_PUT_NB 1
#define gasnete_inline_put_nb	gasnete_put_nb_bulk

/* 
 * Some sync ops are the same
 */
#define GASNETI_INLINE_TRY_SYNCNB_ALL 1
#define gasnete_inline_try_syncnb_all gasnete_try_syncnb_some

/* 
 * Value gets and puts are more tricky
 *
 * They can't map directly to shmem functions as the gasnet interface too
 * general to map to the type-specific elemental shmem_g and shmem_p variants.
 *
 */
#define GASNETI_INLINE_GET_VAL 1
#define gasnete_inline_get_val	(gasnet_register_value_t) gasnete_get_nb_val
#define GASNETI_DIRECT_PUT_VAL     1
#define GASNETI_DIRECT_PUT_NB_VAL  1
#define GASNETI_DIRECT_PUT_NBI_VAL 1

#endif


/*   $Source: /Users/kamil/work/gasnet-cvs2/gasnet/shmem-conduit/gasnet_core_internal.h,v $
 *     $Date: 2004/10/08 07:47:25 $
 * $Revision: 1.5 $
 * Description: GASNet shmem conduit header for internal definitions in Core API
 * Copyright 2002, Dan Bonachea <bonachea@cs.berkeley.edu>
 * Terms of use are as specified in license.txt
 */

#ifndef _GASNET_CORE_INTERNAL_H
#define _GASNET_CORE_INTERNAL_H

#include <gasnet.h>
#include <gasnet_internal.h>
#if defined(CRAY_SHMEM) || defined(SGI_SHMEM)
#include <mpp/shmem.h>
#elif defined(QUADRICS_SHMEM)
#include <shmem.h>
#endif

extern gasnet_seginfo_t *gasnetc_seginfo;
extern intptr_t		*gasnetc_segment_shptr_off;

#ifndef GASNETE_GLOBAL_ADDRESS
#define gasnetc_boundscheck(node,ptr,nbytes)		    \
	    gasneti_boundscheck(node,ptr,nbytes,c)
#else
#define gasnetc_boundscheck(node,ptr,nbytes)
#endif

/*  whether or not to use spin-locking for HSL's */
#define GASNETC_HSL_SPINLOCK 1

/* -------------------------------------------------------------------- */
/* make a GASNet call - if it fails, print error message and return */
#define GASNETC_SAFE(fncall) do {                            \
   int retcode = (fncall);                                   \
   if_pf (gasneti_VerboseErrors && retcode != GASNET_OK) {   \
     char msg[1024];                                         \
     sprintf(msg, "\nGASNet encountered an error: %s(%i)\n", \
        gasnet_ErrorName(retcode), retcode);                 \
     GASNETI_RETURN_ERRFR(RESOURCE, fncall, msg);            \
   }                                                         \
 } while (0)

/*
 * These settings are based on benchmarks executed over various implementations
 * of shmem, and can be reproduced by the shmem_core.c file in contrib/
 */
/*
 * Quadrics has higher latency and is hence more sensitive to remote atomic
 * opeartions.  Turns out randomly chosing an index in the shared queue
 * provides much better performance without impacting performance under high
 * contention.
 */
#ifdef QUADRICS_SHMEM
#define GASNETC_VECTORIZE

/*
 * Cray does very well with the mswap operation, which essentially allows us to
 * reduce the unsuccessful AMPoll case to a single word read (if queue <= 64).
 */
#elif defined(CRAY_SHMEM) 
#define GASNETC_VECTORIZE		_Pragma("_CRI concurrent")
#define GASNETE_SHMEM_BARRIER

/* 
 * SGI does not implement shmem_int_mswap (even though it exists in the header
 * file!).  We use the put-based mechanism instead.
 */
#elif defined(SGI_SHMEM)
#define GASNETC_VECTORIZE
#define GASNETE_SHMEM_BARRIER
#endif

/* -------------------------------------------------------------------- */
#define GASNETC_HANDLER_BASE  1 /* reserve 1-63 for the core API */
#define _hidx_                              (GASNETC_HANDLER_BASE+)
/* add new core API handlers here and to the bottom of gasnet_core.c */

/*
 * Active Message header encoding (always 4 bytes)
 *
 * Currently scales to 65K PEs.  This may be too conservative in a not too
 * distance future.
 */
#define GASNETC_REQREP_M    0x80000000
#define GASNETC_TYPE_M	    0x60000000
#define GASNETC_NUMARGS_M   0x1f000000
#define GASNETC_HANDLER_M   0x00ff0000
#define GASNETC_NODEID_M    0x0000ffff

#define GASNETC_REQUEST_T   0x00000000
#define GASNETC_REPLY_T	    0x80000000

#define GASNETC_AMSHORT_T   0x00000000
#define GASNETC_AMMED_T	    0x20000000
#define GASNETC_AMLONG_T    0x40000000

#define GASNETC_HANDLER_SHIFT	16
#define GASNETC_NUMARGS_SHIFT	24

/*
 * Pack/Unpack AM Headers
 */
#define GASNETC_AMHEADER_PACK(req,type,numargs,handler,nodeid)		\
	    (gasneti_assert((numargs)<=GASNETC_MAX_ARGS),		\
	    ((req) | (type) | ((handler)<<GASNETC_HANDLER_SHIFT) |	\
	     ((numargs)<<GASNETC_NUMARGS_SHIFT) | (nodeid)))

#define GASNETC_AMHEADER_UNPACK(hdr,req,type,numargs,handler,nodeid) do {   \
	 (req) = (hdr) & GASNETC_REQREP_M;				    \
	 (type) = (hdr) & GASNETC_TYPE_M;				    \
	 (numargs) = ((hdr) & GASNETC_NUMARGS_M) >> GASNETC_NUMARGS_SHIFT;  \
	 (handler) = ((hdr) & GASNETC_HANDLER_M) >> GASNETC_HANDLER_SHIFT;  \
	 (nodeid) = (hdr) & GASNETC_NODEID_M;			            \
	} while (0)

#define GASNETC_MEDHEADER_PADARG(numargs) \
        (!((numargs & 0x1) ^ ((GASNETC_MED_HEADERSZ>>2) & 0x1)))

/*
 * Simpler header queries
 */
#define GASNETC_AMHEADER_ISREQUEST(hdr)			\
	    (((hdr) & GASNETC_REQREP_M) == GASNETC_REQUEST_T)

#define GASNETC_AMHEADER_ISREPLY(hdr)	!GASNETC_AMHEADER_ISREQUEST(hdr)

#define GASNETC_AMHEADER_NODEID(hdr)	((hdr) & GASNETC_NODEID_M)

#define GASNETC_ARGS_WRITE(buf, argptr, numargs)                        \
        do {    int _i; int32_t *_pbuf = (int32_t *) buf;               \
                for (_i = 0; _i < (numargs); _i++)      {               \
                        _pbuf[_i] = (int32_t) va_arg((argptr), int);    \
                }                                                       \
           } while (0)

/*
 * AMQUEUE DEPTH and maximum sizes
 */
#define GASNETC_AMQUEUE_DEFAULT_DEPTH	64
#define GASNETC_AMQUEUE_MAX_DEPTH	256
#define GASNETC_AMQUEUE_MAX_FIELDS	(GASNETC_AMQUEUE_MAX_DEPTH/sizeof(uintptr_t))
#define GASNETC_AMQUEUE_FREE_S		0
#define GASNETC_AMQUEUE_USED_S		1
#define GASNETC_AMQUEUE_DONE_S		2

#define GASNETC_POW_2(n)		(!((n)&((n)-1)))
#define GASNETC_AMQUEUE_SIZE_VALID(q)	(GASNETC_POW_2(q) && (q)>1 && \
					    (q)<=GASNETC_AMQUEUE_MAX_DEPTH)

/*
 * Each queue slot requires some payload area to store AM arguments and
 * AMMedium payloads.
 */
typedef
struct _gasnetc_am_packet
{
	int	    state;  /* One of FREE, USED, DONE */
	uint32_t    header;
	char	    payload[GASNETC_MAX_MEDIUM_TOTAL];
}
gasnetc_am_packet_t;

/*
 * Senders really only need stubs that are large enough to hold a header and up
 * to 16 args
 */
typedef
struct _gasnetc_am_stub
{
	uint32_t    args[GASNETC_MAX_ARGS+4];
	/* 
	 * arg[0] holds the AM header
	 *
	 * Short use arg[1-16] for args
	 *
	 * Longs use arg[1]    for length
	 *           arg[2-3]  for ptr to payload
	 *           arg[4-19] for args
	 *
	 * Meds  use arg[1]    for length
	 *           arg[2-17] for args
	 *           arg[18... for payload
	 *           (payload may be earlier if <16 args are used)
	 */
}
gasnetc_am_stub_t;

/*
 * The header is sometimes useful in its unpacked form
 */
typedef
struct _gasnetc_am_header
{
	uint32_t	reqrep;
	uint32_t	type;
	uint32_t	numargs;
	uint32_t	handler;
	gasnet_node_t	pe;
}
gasnetc_am_header_t;

extern int  gasnetc_amq_idx;
extern int  gasnetc_amq_depth;
extern int  gasnetc_amq_mask;

extern gasnetc_am_packet_t  gasnetc_amq_reqs[GASNETC_AMQUEUE_MAX_DEPTH];

#ifdef CRAY_SHMEM
extern volatile long	gasnetc_amq_reqfields[GASNETC_AMQUEUE_MAX_FIELDS];
extern long		gasnetc_amq_numfields;
#endif

GASNET_INLINE_MODIFIER(gasnetc_AMQueueRequest)
int gasnetc_AMQueueRequest(gasnet_node_t pe)
{
    int	idx;

    #ifdef QUADRICS_SHMEM
        idx = random() & gasnetc_amq_mask;
    #else
        idx = shmem_int_finc(&gasnetc_amq_idx, (int) pe) & gasnetc_amq_mask;
    #endif

    /* Once we have the ID, cswap until the selected slot is free  */

    while (shmem_int_cswap(&gasnetc_amq_reqs[idx].state, 
	    GASNETC_AMQUEUE_FREE_S, GASNETC_AMQUEUE_USED_S, (int) pe) 
	    != GASNETC_AMQUEUE_FREE_S)
	gasnetc_AMPoll();

    return idx;
}

/*
 * AMQueueReply is exactly like AMQueueRequest, but it doesn't Poll.
 */
GASNET_INLINE_MODIFIER(gasnetc_AMQueueReply)
int gasnetc_AMQueueReply(gasnet_node_t pe)
{
    int	idx;

    #ifdef QUADRICS_SHMEM
    idx = random() & gasnetc_amq_mask;
    #else /* ! QUADRICS_SHMEM */
    idx = shmem_int_finc(&gasnetc_amq_idx, (int) pe) & gasnetc_amq_mask;
    #endif

    /* Once we have the ID, cswap until the selected slot is free  */
    while (shmem_int_cswap(&gasnetc_amq_reqs[idx].state, 
	    GASNETC_AMQUEUE_FREE_S, GASNETC_AMQUEUE_USED_S, (int) pe) 
	    != GASNETC_AMQUEUE_FREE_S)
	{}

    return idx;
}


GASNET_INLINE_MODIFIER(gasnetc_AMQueueRelease)
void gasnetc_AMQueueRelease(gasnet_node_t pe, int idx)
{
    #ifdef CRAY_SHMEM
    /*
     * We are trying to find to which id in the bitvector the current idx will
     * map to.  For a long of 64 bits, bits map to bit vector indeces as
     * follows:
     *   0- 63 => bit vector id 0
     *  64-127 => bit vector id 1
     *  . .
     *
     * The intuition is to get the number of leading zeros in idx, which should
     * be at the least (64-9=55) for indexes between 256 and 511 and 64 if the
     * index is 0.  
     *
     * If we subtract the number of leading zeros from 64, the value is between
     * 0 and 9.  Since we really want a value between 0 and 4, we simply make
     * sure that the smallest idx passed into _leadz has its sixth bit set.
     *
     */
    int  field_no; 
    long field_mask;

    field_no = (unsigned long) idx >> 6;
    field_mask = 0x8000000000000000ul >> (idx & 63);

    gasneti_assert(idx >= 0 && idx < gasnetc_amq_depth);
    gasneti_assert(field_no >= 0 && field_no < GASNETC_AMQUEUE_MAX_FIELDS);

    shmem_long_mswap((long *) &gasnetc_amq_reqfields[field_no], 
		     field_mask, field_mask, pe);

    #else /* ! CRAY_SHMEM */
    gasneti_assert(idx >= 0 && idx < gasnetc_amq_depth);
    gasneti_assert(sizeof(int) == sizeof(uint32_t));
    shmem_int_p(&(gasnetc_amq_reqs[idx].state), 
		GASNETC_AMQUEUE_DONE_S, (int) pe);
    #endif

    return;
}

/* -------------------------------------------------------------------------- */
typedef void (*gasnetc_HandlerShort) (void *token, ...);
typedef void (*gasnetc_HandlerMedium)(void *token, void *buf, int nbytes, ...);
typedef void (*gasnetc_HandlerLong)  (void *token, void *buf, int nbytes, ...);

#define GASNETC_RUN_HANDLER_SHORT(pfn, token, pArgs, numargs) do { 	       \
	gasneti_assert(pfn);						       \
  	if (numargs == 0) (*(gasnetc_HandlerShort)pfn)((void *)token); 	       \
	else {								       \
    		uint32_t *args = (uint32_t *)(pArgs); /* eval only once */     \
    		switch (numargs) {					       \
		case 1: (*(gasnetc_HandlerShort)pfn)((void *)token, args[0]);  \
			break;						       \
		case 2: (*(gasnetc_HandlerShort)pfn)((void *)token, args[0],   \
			args[1]); break;	       			       \
		case 3: (*(gasnetc_HandlerShort)pfn)((void *)token, args[0],   \
			args[1], args[2]); break; 			       \
		case 4: (*(gasnetc_HandlerShort)pfn)((void *)token, args[0],   \
			args[1], args[2], args[3]); break; 		       \
	     	case 5: (*(gasnetc_HandlerShort)pfn)((void *)token, args[0],   \
			args[1], args[2], args[3], args[4]); break;   	       \
	     	case 6: (*(gasnetc_HandlerShort)pfn)((void *)token, args[0],   \
			args[1], args[2],  args[3], args[4], args[5]); break;  \
	     	case 7: (*(gasnetc_HandlerShort)pfn)((void *)token, args[0],   \
			args[1], args[2], args[3], args[4], args[5], args[6]); \
			break;	       					       \
	     	case 8: (*(gasnetc_HandlerShort)pfn)((void *)token, args[0],   \
			args[1], args[2], args[3], args[4], args[5], args[6],  \
			args[7]); break;   				       \
	     	case 9: (*(gasnetc_HandlerShort)pfn)((void *)token, args[0],   \
			args[1], args[2], args[3], args[4], args[5], args[6],  \
			args[7], args[8]); break; 			       \
	     	case 10:(*(gasnetc_HandlerShort)pfn)((void *)token, args[0],   \
			args[1], args[2], args[3], args[4], args[5], args[6],  \
			args[7], args[8], args[9]); break; 		       \
	     	case 11:(*(gasnetc_HandlerShort)pfn)((void *)token, args[0],   \
			args[1], args[2], args[3], args[4], args[5], args[6],  \
			args[7], args[8], args[9], args[10]); break; 	       \
	     	case 12:(*(gasnetc_HandlerShort)pfn)((void *)token, args[0],   \
			args[1], args[2], args[3], args[4], args[5], args[6],  \
			args[7], args[8], args[9], args[10], args[11]); break; \
	     	case 13:(*(gasnetc_HandlerShort)pfn)((void *)token, args[0],   \
			args[1], args[2], args[3], args[4], args[5], args[6],  \
			args[7], args[8], args[9], args[10], args[11],         \
			args[12]); break; 				       \
	     	case 14:(*(gasnetc_HandlerShort)pfn)((void *)token, args[0],   \
			args[1], args[2], args[3], args[4], args[5], args[6],  \
			args[7], args[8], args[9], args[10], args[11],         \
			args[12], args[13]); break; 			       \
	     	case 15:(*(gasnetc_HandlerShort)pfn)((void *)token, args[0],   \
			args[1], args[2], args[3], args[4], args[5], args[6],  \
			args[7], args[8], args[9], args[10], args[11],         \
			args[12], args[13], args[14]); break; 		       \
	     	case 16:(*(gasnetc_HandlerShort)pfn)((void *)token, args[0],   \
			args[1], args[2], args[3], args[4], args[5], args[6],  \
			args[7], args[8], args[9], args[10], args[11],         \
			args[12], args[13], args[14], args[15]); break;        \
	     	default: abort();  					       \
	     }								       \
	   }								       \
	 } while (0)

/* -------------------------------------------------------------------------- */
#define _GASNETC_RUN_HANDLER_MEDLONG(phandlerfn, token, pArgs, numargs,        \
		pData, datalen) do {					       \
	gasneti_assert(phandlerfn);					       \
  	if (numargs == 0) (*phandlerfn)(token, pData, datalen);	               \
	else {								       \
    		uint32_t *args = (uint32_t *)(pArgs); /* eval only once */     \
    		switch (numargs) {					       \
      		case 1: (*phandlerfn)(token, pData, datalen, args[0]); break;  \
		case 2: (*phandlerfn)(token, pData, datalen, args[0], args[1]);\
			break;						       \
		case 3: (*phandlerfn)(token, pData, datalen, args[0], args[1], \
			  args[2]); break; 				       \
		case 4: (*phandlerfn)(token, pData, datalen, args[0], args[1], \
			  args[2], args[3]); break; 			       \
	     	case 5: (*phandlerfn)(token, pData, datalen, args[0], args[1], \
			  args[2], args[3], args[4]); break; 		       \
	     	case 6: (*phandlerfn)(token, pData, datalen, args[0], args[1], \
			  args[2], args[3], args[4], args[5]); break;          \
	     	case 7: (*phandlerfn)(token, pData, datalen, args[0], args[1], \
			 args[2], args[3], args[4], args[5], args[6]);         \
			 break;						       \
	     	case 8: (*phandlerfn)(token, pData, datalen, args[0], args[1], \
			  args[2], args[3], args[4], args[5], args[6],         \
			  args[7]); break; 				       \
	     	case 9: (*phandlerfn)(token, pData, datalen, args[0], args[1], \
			  args[2], args[3], args[4], args[5], args[6],         \
			  args[7], args[8]); break; 			       \
	     	case 10:(*phandlerfn)(token, pData, datalen, args[0], args[1], \
			  args[2], args[3], args[4], args[5], args[6],         \
			  args[7], args[8], args[9]); break; 		       \
	     	case 11:(*phandlerfn)(token, pData, datalen, args[0], args[1], \
			  args[2], args[3], args[4], args[5], args[6],         \
			  args[7], args[8], args[9], args[10]); break;         \
	     	case 12:(*phandlerfn)(token, pData, datalen, args[0], args[1], \
			  args[2], args[3], args[4], args[5], args[6],         \
			  args[7], args[8], args[9], args[10], args[11]);      \
			  break; 					       \
	     	case 13:(*phandlerfn)(token, pData, datalen, args[0], args[1], \
			  args[2], args[3], args[4], args[5], args[6],         \
			  args[7], args[8], args[9], args[10], args[11],       \
			  args[12]); break; 				       \
	     	case 14:(*phandlerfn)(token, pData, datalen, args[0], args[1], \
			  args[2], args[3], args[4], args[5], args[6],         \
			  args[7], args[8], args[9], args[10], args[11],       \
			  args[12], args[13]); break; 			       \
	     	case 15:(*phandlerfn)(token, pData, datalen, args[0], args[1], \
			  args[2], args[3], args[4], args[5], args[6], 	       \
			  args[7], args[8], args[9], args[10], args[11],       \
			  args[12], args[13], args[14]); break; 	       \
	     	case 16:(*phandlerfn)(token, pData, datalen, args[0], args[1], \
			  args[2], args[3], args[4], args[5], args[6],         \
			  args[7], args[8], args[9], args[10], args[11],       \
			  args[12], args[13], args[14], args[15]); break;      \
	     	default: abort();  					       \
	     }								       \
	   }								       \
	 } while (0)

/* -------------------------------------------------------------------------- */
#define GASNETC_RUN_HANDLER_MEDIUM(phandlerfn, token, pArgs, numargs, pData,   \
		                   datalen)				       \
        _GASNETC_RUN_HANDLER_MEDLONG((gasnetc_HandlerMedium)phandlerfn,        \
				 (void *)token, pArgs, numargs, (void *)pData, \
				 (int)datalen)

#define GASNETC_RUN_HANDLER_LONG(phandlerfn, token, pArgs, numargs, pData,     \
		                   datalen)				       \
        _GASNETC_RUN_HANDLER_MEDLONG((gasnetc_HandlerLong)phandlerfn,          \
				 (void *)token, pArgs, numargs, (void *)pData, \
				 (int)datalen)
/* -------------------------------------------------------------------------- */

#endif

/* $Id: gasnet_core_help.h,v 1.26 2004/01/19 11:13:58 bonachea Exp $
 * $Date: 2004/01/19 11:13:58 $
 * $Revision: 1.26 $
 * Description: GASNet gm conduit core Header Helpers (Internal code, not for client use)
 * Copyright 2002, Christian Bell <csbell@cs.berkeley.edu>
 * Copyright 2002, Dan Bonachea <bonachea@cs.berkeley.edu>
 * Terms of use are as specified in license.txt
 */

#ifndef _IN_GASNET_H
  #error This file is not meant to be included directly- clients should include gasnet.h
#endif

#ifndef _GASNET_CORE_HELP_H
#define _GASNET_CORE_HELP_H

BEGIN_EXTERNC

#include <gasnet_help.h>
#include <gm.h>

extern gasnet_node_t 		gasnetc_mynode;
extern gasnet_node_t 		gasnetc_nodes;

/* handler prototypes */
typedef void (*gasnetc_HandlerShort) (void *token, ...);
typedef void (*gasnetc_HandlerMedium)(void *token, void *buf, int nbytes, ...);
typedef void (*gasnetc_HandlerLong)  (void *token, void *buf, int nbytes, ...);

/* -------------------------------------------------------------------------- */
/* A few more obscure configurable parameters.                                */
#define GASNETC_GM_MAXPORTS	gm_num_ports(NULL)
#define GASNETC_GM_MAXBOARDS	3

#if defined(GM_API_VERSION_2_0) && GM_API_VERSION >= GM_API_VERSION_2_0
#define GASNETC_GM_2
#endif

/* Amount of physical memory that is pinnable. . reported to be 
 * 70% by Myricom.  We use 60% to be safe. */
#define GASNETC_PHYSMEM_PINNABLE_RATIO       0.6

#ifdef GASNET_SEGMENT_FAST
#define GASNETC_SEGMENT_PINNED	1
#else 
#define GASNETC_SEGMENT_PINNED	0
#endif

/* Puts changed to gm_put in the GM 2.x API revision */
#ifdef GASNETC_GM_2
#define GASNETC_RDMA_GETS		1
#define GASNETE_GET_NON_DMA_CUTOFF	0
#define GASNETC_GM_PUT	gm_put
#else
#define GASNETC_RDMA_GETS		0
#define GASNETE_GET_NON_DMA_CUTOFF	8192
#define GASNETC_GM_PUT	gm_directed_send_with_callback
#endif

#define GASNETE_PUT_NON_DMA_CUTOFF	0	
#define GASNETE_PUT_NON_BULK_CUTOFF	GASNETC_AM_LEN
#define GASNETE_GETPUT_MEDIUM_LONG_THRESHOLD	8192

#define GASNETC_SEGMENT_ALIGN	GASNETI_PAGESIZE
#define GASNETC_BLOCKUNTIL(cond)	do {				\
	while (!(cond)) { 						\
		gasneti_mutex_unlock(&gasnetc_lock_gm);			\
		gasnetc_AMPoll(); 					\
		gasneti_mutex_lock(&gasnetc_lock_gm); 			\
	} } while (0)

/* -------------------------------------------------------------------------- */
/* These should not be modified */
/* AM Header stores the following fields
 *
 * 0b76543210
 *
 * 7-6: AM Message type (00=small, 01=medium, 10=long, 11=system)
 * 5-1: AM Number of arguments (00000=0, 00001=1, 00010=2, ...)
 * 0:   AM Request/Reply (0=reply, 1=request)
 *
 * AM Index stores the handler index and is one byte next to the 
 * Header
 */

#define GASNETC_AM_SHORT	0x00
#define GASNETC_AM_MEDIUM	0x40
#define GASNETC_AM_LONG		0x80
#define GASNETC_AM_SYSTEM	0xc0

/* Two system messages so far */
#define GASNETC_SYS_GATHER	0x10
#define GASNETC_SYS_BROADCAST	0x20

#define	GASNETC_AM_REPLY	0x00
#define GASNETC_AM_REQUEST	0x01

#define GASNETC_AM_SIZE		16
#define GASNETC_AM_LEN		(1<<GASNETC_AM_SIZE)
#define GASNETC_AM_PACKET	(GASNETC_AM_LEN-8)

#define GASNETC_LONG_OFFSET	GASNETI_PAGESIZE

#define GASNETC_AM_MAX_ARGS	16
#define GASNETC_AM_MAX_HANDLERS 256

#define GASNETC_AM_SHORT_ARGS_OFF	4
#define GASNETC_AM_MEDIUM_ARGS_OFF	4
#define GASNETC_AM_LONG_ARGS_OFF	(8+sizeof(uintptr_t))

#define GASNETC_AM_MEDIUM_HEADER_PAD(numargs) ((((numargs)&0x1)==1) ? 0 : 4)
#ifdef GASNETI_PTR32
#define GASNETC_AM_LONG_PAD(numargs) ((((numargs)&0x1)==1) ? 0 : 4)
#elif defined(GASNETI_PTR64)
#define GASNETC_AM_LONG_PAD(numargs) ((((numargs)&0x1)==0) ? 0 : 4)
#endif

#define GASNETC_AM_SHORT_HEADER_LEN(numargs)                     \
			((numargs)*4 + GASNETC_AM_SHORT_ARGS_OFF)
#define GASNETC_AM_MEDIUM_HEADER_LEN(numargs)                     \
			((numargs)*4 + GASNETC_AM_MEDIUM_ARGS_OFF \
			+ GASNETC_AM_MEDIUM_HEADER_PAD(numargs))
#define GASNETC_AM_LONG_HEADER_LEN(numargs)                     \
			((numargs)*4 + GASNETC_AM_LONG_ARGS_OFF \
			+ GASNETC_AM_LONG_PAD(numargs))

/* -------------------------------------------------------------------------- */
/* Maximum sizes for mediums and Longs */
#define GASNETC_AM_MEDIUM_MAX                                     \
		(GASNETC_AM_PACKET -                              \
		 GASNETC_AM_MEDIUM_HEADER_LEN(GASNETC_AM_MAX_ARGS))
#define GASNETC_AM_LONG_REPLY_MAX   (GASNETC_AM_LEN - GASNETC_LONG_OFFSET)
#define GASNETC_AM_LONG_REQUEST_MAX (3*GASNETC_AM_LEN - GASNETC_LONG_OFFSET)

#define GASNETC_AM_MAX_NON_DMA_PAYLOAD	((GM_MTU-8)-GASNETC_AM_MEDIUM_MAX)
/* -------------------------------------------------------------------------- */
#define GASNETC_AM_NUMARGS(c)   (((c) >> 1) & 0x1f)
#define GASNETC_AM_TYPE(c)      ((c) & 0xc0)
#define GASNETC_AM_IS_SYSTEM(c)  (GASNETC_AM_TYPE(c) == GASNETC_AM_SYSTEM)
#define GASNETC_AM_IS_REQUEST(c) (!GASNETC_AM_IS_SYSTEM((c)) && \
		                  ((c) & GASNETC_AM_REQUEST))
#define GASNETC_AM_IS_REPLY(c)   (!GASNETC_AM_IS_SYSTEM((c)) && \
		                  !((c) & GASNETC_AM_REQUEST))
#define GASNETC_AM_TYPE_STRING(buf)                                            \
                (GASNETC_AM_TYPE(buf) == GASNETC_AM_SHORT ? "Short" :	       \
                        (GASNETC_AM_TYPE(buf) == GASNETC_AM_MEDIUM ? "Medium": \
                                (GASNETC_AM_TYPE(buf) == GASNETC_AM_LONG ?     \
				 "Long" : "Error!")))
/* -------------------------------------------------------------------------- */
#define GASNETC_ASSERT_AMSHORT(buf, type, handler, args, req)  \
        do {    gasneti_assert(buf != NULL);                   \
                gasneti_assert(type >= GASNETC_AM_SHORT &&     \
                                type <= GASNETC_AM_SYSTEM);    \
                gasneti_assert(req == 0 || req == 1);          \
                gasneti_assert(numargs >= 0 && numargs <= 16); \
	} while (0)

#define GASNETC_ASSERT_AMMEDIUM(buf, type, handler, args, req, len, src) \
	    GASNETC_ASSERT_AMSHORT(buf, type, handler, args, req); 

#define GASNETC_ASSERT_AMLONG(buf, type, handler, args, req, len, src, dest) \
	    GASNETC_ASSERT_AMMEDIUM(buf, type, handler, args, req, len,  src);

/* -------------------------------------------------------------------------- */
/* Debug, tracing */
#if GASNET_TRACE
/* Generic Trace debug for AM handlers (gasnet_core) or elsewhere */
#define GASNETC_TRACE_SHORT(reqrep, type, dest, token, idx, args)	       \
		GASNETI_TRACE_PRINTF(C,("%s%s\t%d token="GASNETI_LADDRFMT" index=%d args=%d",\
			#reqrep, #type, dest, GASNETI_LADDRSTR(token), idx, args)) 

#define GASNETC_TRACE_MEDIUM(reqrep, type, dest, token, idx, args, sAddr,len)  \
		GASNETI_TRACE_PRINTF(C,("%s%s\t%d token="GASNETI_LADDRFMT" index=%d args=%d "\
			"src="GASNETI_LADDRFMT" len=%d", #reqrep, #type, dest,               \
			GASNETI_LADDRSTR(token), idx, args, GASNETI_LADDRSTR(sAddr), len))

#define GASNETC_TRACE_LONG(reqrep, type, dest, token, idx, args, sAddr, dAddr, \
		len) GASNETI_TRACE_PRINTF(C,("%s%s\t%d token="GASNETI_LADDRFMT" index=%d "   \
			"args=%d src="GASNETI_LADDRFMT" dst="GASNETI_LADDRFMT" len=%d", #reqrep, #type,    \
			dest, GASNETI_LADDRSTR(token), idx, args, GASNETI_LADDRSTR(sAddr), \
			GASNETI_LADDRSTR(dAddr), (int)(len)))

/* Formats AM-only debug tracing for _GASNET_TRACE functions, can only be
 * called from gasnet_AM...M() functions from gasnet_core.c
 */
#define _GASNETC_AMTRACE_SHORT(reqrep, type, dest, token) 		       \
		GASNETC_TRACE_SHORT(reqrep, type, dest, token, handler, numargs)
#define _GASNETC_AMTRACE_MEDIUM(reqrep, type, dest, token) 		       \
		GASNETC_TRACE_MEDIUM(reqrep, type, dest, token, handler,       \
		numargs, source_addr, nbytes)
#define _GASNETC_AMTRACE_LONG(reqrep, type, dest, token) 		       \
		GASNETC_TRACE_LONG(reqrep, type, dest, token, handler, numargs,\
		source_addr, dest_addr, nbytes)

#define GASNETC_AMTRACE_ReplyShort(str) _GASNETC_AMTRACE_SHORT(str,            \
		AMReplyShort, bufd->node, token)
#define GASNETC_AMTRACE_RequestShort(str) _GASNETC_AMTRACE_SHORT(str,          \
		AMRequestShort, dest, 0)

#define GASNETC_AMTRACE_ReplyMedium(str) _GASNETC_AMTRACE_MEDIUM(str,          \
		AMReplyMedium, bufd->node, token)
#define GASNETC_AMTRACE_RequestMedium(str) _GASNETC_AMTRACE_MEDIUM(str,        \
		AMReplyMedium, dest, 0)

#define GASNETC_AMTRACE_ReplyLong(str) _GASNETC_AMTRACE_LONG(str, AMReplyLong, \
		bufd->node, token)
#define GASNETC_AMTRACE_RequestLong(str) _GASNETC_AMTRACE_LONG(str,	       \
		AMReplyLong, dest, 0)

#define GASNETC_AMTRACE_ReplyLongAsyncM GASNETC_AMTRACE_ReplyLongM
#define GASNETC_AMTRACE_RequestLongAsyncM GASNETC_AMTRACE_RequestLongM

#else
/* NO tracing enabled */
#define GASNETI_TRACE_PRINTF(type,args)
#define GASNETC_TRACE_SHORT(reqrep, type, dest, token, idx, args)
#define GASNETC_TRACE_MEDIUM(reqrep, type, dest, token, idx, args, sAddr,len) 
#define GASNETC_TRACE_LONG(reqrep, type, dest, token, idx, args, sAddr, dAddr, \
		len)
#define GASNETC_AMTRACE_ReplyShort(str) 
#define GASNETC_AMTRACE_RequestShort(str) 
#define GASNETC_AMTRACE_ReplyMedium(str) 
#define GASNETC_AMTRACE_RequestMedium(str) 
#define GASNETC_AMTRACE_ReplyLong(str) 
#define GASNETC_AMTRACE_RequestLong(str) 
#define GASNETC_AMTRACE_ReplyLongAsyncM
#define GASNETC_AMTRACE_RequestLongAsyncM
#endif

/* Need GASNETC_DPRINTF for init functions since gasnet tracing
 * is not enabled yet
 */
#if	GASNET_DEBUG
#define GASNETC_DPRINTF(x)	printf x
#else
#define GASNETC_DPRINTF(x)      ((void)0)
#endif

/* -------------------------------------------------------------------------- */
#define GASNETC_ASSERT_BUFDESC_PTR(bufd, ptr) do {                \
                gasneti_assert((bufd)->id ==                      \
                (((uintptr_t)(ptr) - (uintptr_t)_gmc.dma_bufs) >> \
                        GASNETC_AM_SIZE));                        \
		} while (0)
#define GASNETC_BUFDESC_PTR(x) ((gasnetc_bufdesc_t *) &_gmc.bd_ptr[	       \
				(((uintptr_t)(x) - (uintptr_t)_gmc.dma_bufs)>> \
				 GASNETC_AM_SIZE)])
#define GASNETC_GM_RECV_PTR(e,fast)				\
	(fast) ? (uint8_t *) gm_ntohp((e)->recv.message) :	\
	    (uint8_t *) gm_ntohp((e)->recv.buffer)

#define GASNETC_SYSHEADER_WRITE(buf, index)				\
	*((uint8_t *)(buf)) = (GASNETC_AM_SYSTEM | ((index) & 0x3f))
#define GASNETC_SYSHEADER_READ(buf) (uint8_t)(*((uint8_t *)(buf)) & 0x3f)

#define GASNETC_AMHEADER_WRITE(buf, type, args, req) 			\
	*((uint8_t *)(buf)) = (((uint8_t)(type) & 0xc0) | 		\
			       ((uint8_t)(args)<< 1) | ((uint8_t)(req) & 0x01) )
#define GASNETC_AMHANDLER_WRITE(buf, handler)				\
	*((uint8_t *)(buf)) = (uint8_t)(handler)
#define GASNETC_AMHANDLER_READ(buf, handler)				\
	(uint8_t)(handler) = *((uint8_t *)(buf))

#define GASNETC_AMDESTADDR_WRITE(buf, dest_addr)         		\
	*((uintptr_t *)(buf)) = (uintptr_t)(dest_addr)
#define GASNETC_AMDESTADDR_READ(buf, dest_addr)                     	\
	(uintptr_t)(dest_addr) = *((uintptr_t *)(buf))

#define GASNETC_AMLENGTH_WRITE(buf, len) *((uint16_t *)(buf)) = (uint16_t)(len)
#define GASNETC_AMLENGTH_READ(buf, len)	 (uint16_t)(len) = *((uint16_t *)(buf))
#define GASNETC_AMLENGTH_WRITE4(buf, len) *((uint32_t *)(buf)) = (uint32_t)(len)
#define GASNETC_AMLENGTH_READ4(buf, len) (uint32_t)(len) = *((uint32_t *) (buf))

#define GASNETC_ARGS_WRITE(buf, argptr, numargs)			\
	do { 	int _i; int32_t *_pbuf = (int32_t *) buf;		\
		for (_i = 0; _i < (numargs); _i++)	{		\
			_pbuf[_i] = (int32_t) va_arg((argptr), int);  	\
		}							\
	   } while (0)

#define GASNETC_AMPAYLOAD_WRITE(dest, src, len)	memcpy((dest),(src),(len))
#define GASNETC_AMPAYLOAD_READ GASNETC_AMPAYLOAD_WRITE

#if defined(GASNETI_PTR32)
#define GASNETC_ARGPTR_NUM	1
#define GASNETC_ARGPTR(ptr, addr)	(*((int32_t *)(ptr)) = (int32_t) addr)
#elif defined(GASNETI_PTR64)
#define GASNETC_ARGPTR_NUM	2
#define GASNETC_ARGPTR(ptr, addr)					    \
	do { (int32_t) *((int32_t *)ptr) = (int32_t) GASNETI_HIWORD(addr);  \
	     (int32_t) *((int32_t *)ptr+1) = (int32_t) GASNETI_LOWORD(addr);\
	   } while (0)
#endif

/* -------------------------------------------------------------------------- */
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

END_EXTERNC

#endif

/*  $Archive:: /Ti/GASNet/template-conduit/gasnet_core_internal.h         $
 *     $Date: 2003/04/01 22:26:31 $
 * $Revision: 1.1.2.13 $
 * Description: GASNet vapi conduit header for internal definitions in Core API
 * Copyright 2002, Dan Bonachea <bonachea@cs.berkeley.edu>
 * Terms of use are as specified in license.txt
 */

#ifndef _GASNET_CORE_INTERNAL_H
#define _GASNET_CORE_INTERNAL_H

#include <stddef.h>	/* for offsetof() */

#include <gasnet.h>
#include <gasnet_internal.h>

#include <vapi.h>
#include <evapi.h>
#include <vapi_common.h>


extern gasnet_seginfo_t *gasnetc_seginfo;

#define gasnetc_boundscheck(node,ptr,nbytes) gasneti_boundscheck(node,ptr,nbytes,c)

/*  whether or not to use spin-locking for HSL's */
#define GASNETC_HSL_SPINLOCK 1

#if defined(DEBUG) && !defined(GASNET_QUIET)
  #define DEBUG_VERBOSE               1
#else
  #define DEBUG_VERBOSE               0
#endif

/* ------------------------------------------------------------------------------------ */
/* make a GASNet call - if it fails, print error message and return */
#define GASNETC_SAFE(fncall) do {                            \
   int retcode = (fncall);                                   \
   if_pf (gasneti_VerboseErrors && retcode != GASNET_OK) {   \
     char msg[1024];                                         \
     sprintf(msg, "\nGASNet encountered an error: %s(%i)\n", \
        gasneti_ErrorName(retcode), retcode);                \
     GASNETI_RETURN_ERRFR(RESOURCE, fncall, msg);            \
   }                                                         \
 } while (0)

/* ------------------------------------------------------------------------------------ */
#define GASNETC_HANDLER_BASE  1 /* reserve 1-63 for the core API */
#define _hidx_                              (GASNETC_HANDLER_BASE+)
/* add new core API handlers here and to the bottom of gasnet_core.c */

/* ------------------------------------------------------------------------------------ */

typedef struct {
  gasnet_handlerarg_t	args[GASNETC_MAX_ARGS];	
} gasnetc_shortmsg_t;

typedef struct {
  uint16_t		nBytes;
  uint16_t		_pad0;
  gasnet_handlerarg_t	args[GASNETC_MAX_ARGS];	
} gasnetc_medmsg_t;

typedef struct {
  uintptr_t		destLoc;
  uint32_t		nBytes;
  gasnet_handlerarg_t	args[GASNETC_MAX_ARGS];	
} gasnetc_longmsg_t;

typedef union {
  uint8_t		raw[GASNETC_BUFSZ];
  gasnetc_shortmsg_t	shortmsg;
  gasnetc_medmsg_t	medmsg;
  gasnetc_longmsg_t	longmsg;
} gasnetc_buffer_t;

/* Use of IB's 32-bit immediate data:
 *   0-1: category
 *     2: request or reply
 *   3-7: numargs
 *  8-15: handerID
 * 16-31: source index
 */

typedef enum {
  gasnetc_Short=0,
  gasnetc_Medium=1,
  gasnetc_Long=2,
  gasnetc_System=3
} gasnetc_category_t;

#define GASNETC_MSG_GENFLAGS(isreq, cat, nargs, hand, srcidx)	\
  (uint32_t)(  (((srcidx) & 0xffff) << 16)	\
	     | (((hand)   & 0xff)   << 8 )	\
	     | (((nargs)  & 0x1f)   << 3 )	\
	     | ((!(isreq) & 0x1)    << 2 )	\
	     | (((cat)    & 0x3)         ))

#define GASNETC_MSG_NUMARGS(flags)	(((flags) >> 3) & 0x1f)
#define GASNETC_MSG_ISREQUEST(flags)	(!((flags) & 0x4))
#define GASNETC_MSG_CATEGORY(flags)	((gasnetc_category_t)((flags) & 0x3))
#define GASNETC_MSG_HANDLERID(flags)	((gasnet_handler_t)((flags) >> 8))
#define GASNETC_MSG_SRCIDX(flags)	((gasnet_node_t)((flags) >> 16))

#define GASNETC_MSG_MED_OFFSET(nargs)	\
	(offsetof(gasnetc_medmsg_t,args) + nargs + ((nargs & 0x1) ^ ((GASNETC_MEDIUM_HDRSZ>>2) & 0x1)))

#define GASNETC_MSG_MED_DATA(msg, nargs) \
	((void *)((uintptr_t)(msg) + GASNETC_MSG_MED_OFFSET(nargs)))

/* ------------------------------------------------------------------------------------ */

#define RUN_HANDLER_SHORT(phandlerfn, token, args, numargs) do {                       \
  assert(phandlerfn);                                                                   \
  switch (numargs) {                                                                  \
    case 0:  (*(gasnetc_HandlerShort)phandlerfn)((gasnet_token_t)token); break;        \
    case 1:  (*(gasnetc_HandlerShort)phandlerfn)((gasnet_token_t)token, args[0]); break;         \
    case 2:  (*(gasnetc_HandlerShort)phandlerfn)((gasnet_token_t)token, args[0], args[1]); break;\
    case 3:  (*(gasnetc_HandlerShort)phandlerfn)((gasnet_token_t)token, args[0], args[1], args[2]); break; \
    case 4:  (*(gasnetc_HandlerShort)phandlerfn)((gasnet_token_t)token, args[0], args[1], args[2], args[3]); break; \
    case 5:  (*(gasnetc_HandlerShort)phandlerfn)((gasnet_token_t)token, args[0], args[1], args[2], args[3], args[4]); break; \
    case 6:  (*(gasnetc_HandlerShort)phandlerfn)((gasnet_token_t)token, args[0], args[1], args[2], args[3], args[4], args[5]); break; \
    case 7:  (*(gasnetc_HandlerShort)phandlerfn)((gasnet_token_t)token, args[0], args[1], args[2], args[3], args[4], args[5], args[6]); break; \
    case 8:  (*(gasnetc_HandlerShort)phandlerfn)((gasnet_token_t)token, args[0], args[1], args[2], args[3], args[4], args[5], args[6], args[7]); break; \
    case 9:  (*(gasnetc_HandlerShort)phandlerfn)((gasnet_token_t)token, args[0], args[1], args[2], args[3], args[4], args[5], args[6], args[7], args[8]); break; \
    case 10: (*(gasnetc_HandlerShort)phandlerfn)((gasnet_token_t)token, args[0], args[1], args[2], args[3], args[4], args[5], args[6], args[7], args[8], args[9]); break; \
    case 11: (*(gasnetc_HandlerShort)phandlerfn)((gasnet_token_t)token, args[0], args[1], args[2], args[3], args[4], args[5], args[6], args[7], args[8], args[9], args[10]); break; \
    case 12: (*(gasnetc_HandlerShort)phandlerfn)((gasnet_token_t)token, args[0], args[1], args[2], args[3], args[4], args[5], args[6], args[7], args[8], args[9], args[10], args[11]); break; \
    case 13: (*(gasnetc_HandlerShort)phandlerfn)((gasnet_token_t)token, args[0], args[1], args[2], args[3], args[4], args[5], args[6], args[7], args[8], args[9], args[10], args[11], args[12]); break; \
    case 14: (*(gasnetc_HandlerShort)phandlerfn)((gasnet_token_t)token, args[0], args[1], args[2], args[3], args[4], args[5], args[6], args[7], args[8], args[9], args[10], args[11], args[12], args[13]); break; \
    case 15: (*(gasnetc_HandlerShort)phandlerfn)((gasnet_token_t)token, args[0], args[1], args[2], args[3], args[4], args[5], args[6], args[7], args[8], args[9], args[10], args[11], args[12], args[13], args[14]); break; \
    case 16: (*(gasnetc_HandlerShort)phandlerfn)((gasnet_token_t)token, args[0], args[1], args[2], args[3], args[4], args[5], args[6], args[7], args[8], args[9], args[10], args[11], args[12], args[13], args[14], args[15]); break; \
    default: abort();                                                                 \
    }                                                                                 \
  } while (0)

#define _RUN_HANDLER_MEDLONG(phandlerfn, token, args, numargs, pData, datalen) do {   \
  assert(phandlerfn);                                                         \
  switch (numargs) {                                                        \
    case 0:  (*phandlerfn)(token, pData, datalen); break;                    \
    case 1:  (*phandlerfn)(token, pData, datalen, args[0]); break;           \
    case 2:  (*phandlerfn)(token, pData, datalen, args[0], args[1]); break;  \
    case 3:  (*phandlerfn)(token, pData, datalen, args[0], args[1], args[2]); break; \
    case 4:  (*phandlerfn)(token, pData, datalen, args[0], args[1], args[2], args[3]); break; \
    case 5:  (*phandlerfn)(token, pData, datalen, args[0], args[1], args[2], args[3], args[4]); break; \
    case 6:  (*phandlerfn)(token, pData, datalen, args[0], args[1], args[2], args[3], args[4], args[5]); break; \
    case 7:  (*phandlerfn)(token, pData, datalen, args[0], args[1], args[2], args[3], args[4], args[5], args[6]); break; \
    case 8:  (*phandlerfn)(token, pData, datalen, args[0], args[1], args[2], args[3], args[4], args[5], args[6], args[7]); break; \
    case 9:  (*phandlerfn)(token, pData, datalen, args[0], args[1], args[2], args[3], args[4], args[5], args[6], args[7], args[8]); break; \
    case 10: (*phandlerfn)(token, pData, datalen, args[0], args[1], args[2], args[3], args[4], args[5], args[6], args[7], args[8], args[9]); break; \
    case 11: (*phandlerfn)(token, pData, datalen, args[0], args[1], args[2], args[3], args[4], args[5], args[6], args[7], args[8], args[9], args[10]); break; \
    case 12: (*phandlerfn)(token, pData, datalen, args[0], args[1], args[2], args[3], args[4], args[5], args[6], args[7], args[8], args[9], args[10], args[11]); break; \
    case 13: (*phandlerfn)(token, pData, datalen, args[0], args[1], args[2], args[3], args[4], args[5], args[6], args[7], args[8], args[9], args[10], args[11], args[12]); break; \
    case 14: (*phandlerfn)(token, pData, datalen, args[0], args[1], args[2], args[3], args[4], args[5], args[6], args[7], args[8], args[9], args[10], args[11], args[12], args[13]); break; \
    case 15: (*phandlerfn)(token, pData, datalen, args[0], args[1], args[2], args[3], args[4], args[5], args[6], args[7], args[8], args[9], args[10], args[11], args[12], args[13], args[14]); break; \
    case 16: (*phandlerfn)(token, pData, datalen, args[0], args[1], args[2], args[3], args[4], args[5], args[6], args[7], args[8], args[9], args[10], args[11], args[12], args[13], args[14], args[15]); break; \
    default: abort();                                                                 \
    }                                                                                 \
  } while (0)

#define RUN_HANDLER_MEDIUM(phandlerfn, token, args, numargs, pData, datalen) do {      \
    assert(((uintptr_t)pData) % 8 == 0);  /* we guarantee double-word alignment for data payload of medium xfers */ \
    _RUN_HANDLER_MEDLONG((gasnetc_HandlerMedium)phandlerfn, (gasnet_token_t)token, args, numargs, (void *)pData, (size_t)datalen); \
  } while(0)

#define RUN_HANDLER_LONG(phandlerfn, token, args, numargs, pData, datalen)             \
  _RUN_HANDLER_MEDLONG((gasnetc_HandlerLong)phandlerfn, (gasnet_token_t)token, args, numargs, (void *)pData, (size_t)datalen)

/* ------------------------------------------------------------------------------------ */

#define GASNETC_MAX_NUMHANDLERS   256
typedef void (*gasnetc_handler_fn_t)();  /* prototype for handler function */
extern gasnetc_handler_fn_t gasnetc_handler[GASNETC_MAX_NUMHANDLERS];

/* ------------------------------------------------------------------------------------ */

#define GASNETC_HCA_ID  "InfiniHost0"
#define GASNETC_CQ_SIZE 65535   	/* maximum entries in a CQ */
#define GASNETC_SQ_SIZE 1024	   	/* maximum send entries to queue */

#define GASNETC_SND_WQE GASNETC_SQ_SIZE /* maximum unreaped entries on a snd work queue */
#define GASNETC_SND_SG  2               /* maximum number of segments to gather on send */

#define GASNETC_RCV_WQE 2               /* maximum unreaped entries on a rcv work queue */
#define GASNETC_RCV_SG  1               /* maximum number of segments to scatter on rcv */

/* Structure for a cep (connection end-point)
 * Include whatever per-node data we need.
 */
typedef struct {
  /* ### Need more here */
  VAPI_qp_hndl_t	qp_handle;
  #if defined(GASNET_SEGMENT_FAST)
    /* RKey for the segment, registered at attach time */
    VAPI_rkey_t		rkey;
  #endif
} gasnetc_cep_t;

/* Description of a receive buffer */
typedef struct {
  /* ### Need more here ? */
  VAPI_rr_desc_t	rr_desc;	/* recv request descriptor */
  VAPI_sg_lst_entry_t	rr_sg;		/* single-entry scatter list */
  uint32_t		flags;		/* filled in at recv time */
  int			reply_sent;
} gasnetc_rcv_desc_t;

/* Description of a send buffer */
typedef struct _gasnetc_snd_desc_t {
  /* ### Need more here ? */
  gasneti_atomic_t		done;
  struct _gasnetc_snd_desc_t	*next;
  struct _gasnetc_snd_desc_t	*tail;
  gasnetc_buffer_t		*buffer;
  VAPI_sr_desc_t		sr_desc;		/* send request descriptor */
  VAPI_sg_lst_entry_t		sr_sg[GASNETC_SND_SG];	/* send request gather list */
} gasnetc_snd_desc_t;

/* Description of a registered (pinned) memory region */
typedef struct {
  VAPI_mr_hndl_t	handle;
  VAPI_lkey_t		lkey;
  VAPI_rkey_t		rkey;
  uintptr_t		start;
  uintptr_t		end;	/* inclusive */
  size_t		size;
} gasnetc_memreg_t;

/* Bootstrap helper routines in gasnet_bootstrap_*.c */
extern void gasnetc_bootstrapInit(int *argc, char ***argv);
extern void gasnetc_bootstrapFini(void);
extern void gasnetc_bootstrapConf(void);
extern void gasnetc_bootstrapBarrier(void);
extern void gasnetc_bootstrapAllgather(void *src, size_t len, void *dest);
extern void gasnetc_bootstrapAlltoall(void *src, size_t len, void *dest);

/* Send routines in gasnet_core_snd.c */
extern void gasnetc_snd_init(void);
extern void gasnetc_snd_fini(void);
extern gasnetc_snd_desc_t *gasnetc_rdma_put(gasnetc_cep_t *cep, uintptr_t src, uintptr_t dst, uintptr_t nbytes);
extern void gasnetc_snd_wait(gasnetc_snd_desc_t *desc);
extern int gasnetc_RequestGeneric(gasnetc_category_t category,
				  int dest, gasnet_handler_t handler,
				  void *src_addr, int nbytes, void *dst_addr,
				  int numargs, gasnetc_snd_desc_t **rdma_desc, va_list argptr);
extern int gasnetc_ReplyGeneric(gasnetc_category_t category,
				gasnet_token_t token, gasnet_handler_t handler,
				void *src_addr, int nbytes, void *dst_addr,
				int numargs, gasnetc_snd_desc_t **rdma_desc, va_list argptr);

/* General routines in gasnet_core.c */
extern gasnetc_memreg_t *gasnetc_local_reg(uintptr_t start);
extern void *gasnetc_alloc_pinned(size_t size, VAPI_mrw_acl_t acl, gasnetc_memreg_t *reg);

/* Global variables */
extern gasnetc_cep_t	*gasnetc_cep;
extern VAPI_hca_hndl_t	gasnetc_hca;
extern VAPI_hca_cap_t	gasnetc_hca_cap;
extern VAPI_hca_port_t	gasnetc_hca_port;
extern VAPI_pd_hndl_t	gasnetc_pd;
extern gasnetc_memreg_t		gasnetc_snd_reg;
extern gasnetc_memreg_t		gasnetc_rcv_reg;
#if defined(GASNET_SEGMENT_FAST)
  extern gasnetc_memreg_t	gasnetc_seg_reg;
#endif
extern VAPI_cq_hndl_t	gasnetc_snd_cq;
extern VAPI_cq_hndl_t	gasnetc_rcv_cq;


#endif

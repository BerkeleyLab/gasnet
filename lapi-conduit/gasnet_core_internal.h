/*  $Archive:: /Ti/GASNet/lapi-conduit/gasnet_core_internal.h         $
 *     $Date: 2003/10/24 01:37:34 $
 * $Revision: 1.20 $
 * Description: GASNet lapi conduit header for internal definitions in Core API
 * Copyright 2002, Dan Bonachea <bonachea@cs.berkeley.edu>
 * Terms of use are as specified in license.txt
 */

/* =======================================================================
 * LAPI Conduit Implementation for IBM SP.
 * Michael Welcome
 * Lawrence Berkeley National Laboratory
 * mlwelcome@lbl.gov
 * November, 2002
 * =======================================================================
 */

#ifndef _GASNET_CORE_INTERNAL_H
#define _GASNET_CORE_INTERNAL_H

#include <gasnet.h>
#include <gasnet_internal.h>

extern gasnet_seginfo_t *gasnetc_seginfo;

/* LAPI Specific decls */
#include <stddef.h>

extern lapi_info_t        gasnetc_lapi_info;
extern int                gasnetc_lapi_errno;
extern char               gasnetc_lapi_msg[];
extern int                gasnetc_max_lapi_uhdr_size;
#if defined(__64BIT__)
extern ulong              gasnetc_max_lapi_data_size;
#else
extern int                gasnetc_max_lapi_data_size;
#endif
extern void**             gasnetc_remote_req_hh;
extern void**             gasnetc_remote_reply_hh;

/* Enable loopback by setting to 1, disable by setting to 0 */
#define GASNETC_ENABLE_LOOPBACK 1

#define GASNETC_MAX_NUMHANDLERS   256
typedef void (*gasnetc_handler_fn_t)();  /* prototype for handler function */
extern gasnetc_handler_fn_t gasnetc_handler[]; /* handler table */

extern void gasnetc_lapi_exchange(void *src, size_t len, void *dest);

extern void gasnetc_lapi_err_handler(lapi_handle_t *context, int *error_code,
				     lapi_err_t  *error_type, int *taskid, int *src);

extern void* gasnetc_lapi_AMreq_hh(lapi_handle_t *context, void *uhdr, uint *uhdr_len,
				   ulong *msg_len, compl_hndlr_t **comp_h, void **uinfo);
extern void* gasnetc_lapi_AMreply_hh(lapi_handle_t *context, void *uhdr, uint *uhdr_len,
				     ulong *msg_len, compl_hndlr_t **comp_h, void **uinfo);
extern void gasnetc_lapi_AMch(lapi_handle_t *context, void *uinfo);

/* what type of message are we sending/receiving */
typedef enum {
    gasnetc_Short=0, 
    gasnetc_Medium=1, 
    gasnetc_Long=2,
    gasnetc_AsyncLong=3
} gasnetc_category_t;

static const char *gasnetc_catname[] = {"Short","Medium","Long","AsyncLong"};

#if GASNETC_USE_INTERRUPTS
#define GASNETC_MAX_THREAD 20
extern volatile int gasnetc_interrupt_held[];
#endif

/* the important contents of a gasnet token */
typedef unsigned int gasnetc_flag_t;
typedef struct {
    gasnetc_flag_t       flags;
    gasnet_handler_t     handlerId;
    gasnet_node_t        sourceId;
    uintptr_t            destLoc;
    size_t               dataLen;
    uintptr_t            uhdrLoc;    /* only used on AsyncLong messages */
    gasnet_handlerarg_t  args[GASNETC_AM_MAX_ARGS];
} gasnetc_msg_t;

#define GASNETC_MSG_SETFLAGS(pmsg, isreq, cat, packed, numargs) \
  ((pmsg)->flags = (gasnetc_flag_t) (                   \
                   (((numargs) & 0xFF) << 16)           \
                 | (((isreq) & 0x1)    << 8)            \
                 | (((packed) & 0x1)   << 3)            \
                 |  ((cat) & 0x3)                       \
                   ))

#define GASNETC_MSG_NUMARGS(pmsg)   ( ( ((unsigned int)(pmsg)->flags) >> 16 ) & 0xFF)
#define GASNETC_MSG_ISREQUEST(pmsg) ( ( ((unsigned int)(pmsg)->flags) >>  8 ) & 0x1)
#define GASNETC_MSG_CATEGORY(pmsg)  ((gasnetc_category_t)((pmsg)->flags & 0x3))
#define GASNETC_MSG_ISPACKED(pmsg)  ((unsigned int)((pmsg)->flags & 0x8))
#define GASNETC_MSG_SET_PACKED(pmsg) (pmsg)->flags |= 0x8

/* --------------------------------------------------------------------
 * the following structure is use as a LAPI-conduit gasnet_token_t.
 * It is also the uhdr structure used in all CORE LAPI Amsend calls.
 *
 * The next pointer allow for the re-use of allocated tokens
 * by stringing them on a free list.  Also, it is used to
 * place tokens on a queue for fast AM request processing in polling
 * mode.
 *
 * --------------------------------------------------------------------
 */
#define GASNETC_TOKEN_SIZE 1024
typedef struct gasnetc_token_rec {
    struct gasnetc_token_rec  *next;
    gasnetc_msg_t    msg;
} gasnetc_token_t;

#define TOKEN_LEN(narg) offsetof(gasnetc_token_t,msg) \
                      + offsetof(gasnetc_msg_t,args) \
                      + (narg)*sizeof(gasnet_handlerarg_t)

/* --------------------------------------------------------------------
 * A freelist structure for the re-use of gasnetc_buf_t structures.
 * --------------------------------------------------------------------
 */
#define GASNETC_UHDR_INIT_CNT 256
#define GASNETC_UHDR_ADDITIONAL 256
typedef struct {
    int    high_water_mark;
    int    numfree;
    int    numalloc;
    gasnetc_token_t *head;
    gasnetc_spinlock_t  lock;
} gasnetc_uhdr_freelist_t;

extern gasnetc_uhdr_freelist_t gasnetc_uhdr_freelist;
extern void   gasnetc_uhdr_init(int want);
extern gasnetc_token_t*  gasnetc_uhdr_alloc(void);
extern void   gasnetc_uhdr_free(gasnetc_token_t* uhdr);
extern int    gasnetc_uhdr_more(int want);

/* --------------------------------------------------------------------
 * Fast mutial exclusion queue using spinlocks
 * --------------------------------------------------------------------
 */
typedef struct {
    gasnetc_token_t *head;
    gasnetc_token_t *tail;
    gasnetc_spinlock_t lock;
    int              schedule;
} gasnetc_token_queue_t;
extern void gasnetc_token_queue_init(gasnetc_token_queue_t *q);
extern gasnetc_token_t* gasnetc_token_dequeue(gasnetc_token_queue_t *q, int update_schedule);
extern void gasnetc_token_enqueue(gasnetc_token_queue_t *q, gasnetc_token_t *p, int *schedule);
/* MLW: Need more descriptive name for this macro */
#define GASNETC_LCHECK(func) { \
    int lapi_errno; \
    if ((lapi_errno = func) != LAPI_SUCCESS) {   \
       LAPI_Msg_string(lapi_errno,gasnetc_lapi_msg);       \
       gasneti_fatalerror("LAPI Error on node %d in file %s at line %d, [%s] return code = %d\n", \
       	                  gasnetc_mynode,__FILE__,__LINE__,gasnetc_lapi_msg,lapi_errno); \
    } \
    }

#define gasnetc_boundscheck(node,ptr,nbytes) gasneti_boundscheck(node,ptr,nbytes,c)

/* ------------------------------------------------------------------------------------ */
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

/* -------------------------------------------------------------------- */
#define GASNETC_HANDLER_BASE  1 /* reserve 1-63 for the core API */
#define _hidx_                              (GASNETC_HANDLER_BASE+)
/* add new core API handlers here and to the bottom of gasnet_core.c */



typedef void (*gasnetc_HandlerShort) (gasnet_token_t token, ...);
typedef void (*gasnetc_HandlerMedium)(gasnet_token_t token, void *buf, size_t nbytes, ...);
typedef void (*gasnetc_HandlerLong)  (gasnet_token_t token, void *buf, size_t nbytes, ...);

/* ---------------------------------------------------------------------
 * UGLY macros to involk GASNET Request/Reply handlers.
 * (Courtesy of elan conduit)
 * ---------------------------------------------------------------------
 */

#define RUN_HANDLER_SHORT(phandlerfn, token, pArgs, numargs) do {                       \
  gasneti_assert(phandlerfn);                                                           \
  if (numargs == 0) (*(gasnetc_HandlerShort)phandlerfn)((void *)token);                 \
  else {                                                                                \
    gasnet_handlerarg_t *args = (gasnet_handlerarg_t *)(pArgs); /* eval only once */    \
    switch (numargs) {                                                                  \
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
    }                                                                                   \
  } while (0)
/* ------------------------------------------------------------------------------------ */
#define _RUN_HANDLER_MEDLONG(phandlerfn, token, pArgs, numargs, pData, datalen) do {   \
  gasneti_assert(phandlerfn);                                                 \
  if (numargs == 0) (*phandlerfn)(token, pData, datalen);                     \
  else {                                                                      \
    gasnet_handlerarg_t *args = (gasnet_handlerarg_t *)(pArgs); /* eval only once */    \
    switch (numargs) {                                                        \
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
    }                                                                                   \
  } while (0)
#define RUN_HANDLER_MEDIUM(phandlerfn, token, pArgs, numargs, pData, datalen) do {      \
    /* gasneti_assert(((int)pData) % 8 == 0);  we guarantee double-word alignment for data payload of medium xfers */ \
    _RUN_HANDLER_MEDLONG((gasnetc_HandlerMedium)phandlerfn, (gasnet_token_t)token, pArgs, numargs, (void *)pData, (int)datalen); \
    } while(0)
#define RUN_HANDLER_LONG(phandlerfn, token, pArgs, numargs, pData, datalen)             \
  _RUN_HANDLER_MEDLONG((gasnetc_HandlerLong)phandlerfn, (gasnet_token_t)token, pArgs, numargs, (void *)pData, (int)datalen)
/* ------------------------------------------------------------------------------------ */

#endif

/*  $Archive:: /Ti/GASNet/template-conduit/gasnet_core_internal.h         $
 *     $Date: 2003/06/29 02:33:01 $
 * $Revision: 1.15 $
 * Description: GASNet elan conduit header for internal definitions in Core API
 * Copyright 2002, Dan Bonachea <bonachea@cs.berkeley.edu>
 * Terms of use are as specified in license.txt
 */

#ifndef _GASNET_CORE_INTERNAL_H
#define _GASNET_CORE_INTERNAL_H

#include <gasnet.h>
#include <gasnet_internal.h>

#include <elan/elan.h>

#ifndef ELAN_VERSION_MAJOR
#define ELAN_VERSION_MAJOR 1
#endif
#ifndef ELAN_VERSION_MINOR
#define ELAN_VERSION_MINOR 3
#endif
#if ELAN_VERSION_MAJOR == 1 && ELAN_VERSION_MINOR == 2
#define ELAN_VER_1_2
#elif ELAN_VERSION_MAJOR == 1 && ELAN_VERSION_MINOR == 3
#define ELAN_VER_1_3
#elif ELAN_VERSION_MAJOR == 1 && ELAN_VERSION_MINOR == 4
#define ELAN_VER_1_4
#else
#error unknown elan version
#endif

extern gasnet_seginfo_t *gasnetc_seginfo;

#define gasnetc_boundscheck(node,ptr,nbytes) gasneti_boundscheck(node,ptr,nbytes,c)

/*  whether or not to use spin-locking for HSL's */
#define GASNETC_HSL_SPINLOCK 1

/* use a (small) statically-allocated shared segment */
#define GASNETC_USE_STATIC_SEGMENT 0

/* ------------------------------------------------------------------------------------ */
/* make a GASNet call - if it fails, print error message and return */
#define GASNETC_SAFE(fncall) do {                            \
   int retcode = (fncall);                                   \
   if_pf (gasneti_VerboseErrors && retcode != GASNET_OK) {                               \
     char msg[1024];                                         \
     sprintf(msg, "\nGASNet encountered an error: %s(%i)\n", \
        gasnet_ErrorName(retcode), retcode);                 \
     GASNETI_RETURN_ERRFR(RESOURCE, fncall, msg);            \
   }                                                         \
 } while (0)

/* ------------------------------------------------------------------------------------ */
#define GASNETC_HANDLER_BASE  1 /* reserve 1-63 for the core API */
/*
#define _hidx_                              (GASNETC_HANDLER_BASE+)
*/
/* add new core API handlers here and to the bottom of gasnet_core.c */

extern ELAN_BASE  *gasnetc_elan_base;
extern ELAN_STATE *gasnetc_elan_state;
extern ELAN_GROUP *gasnetc_elan_group;
extern void *gasnetc_elan_ctx;
extern ELAN_TPORT *gasnetc_elan_tport;

#define BASE()  (gasnetc_elan_base)
#define STATE() (gasnetc_elan_state)
#define GROUP() (gasnetc_elan_group)
#define CTX()   ((ELAN3_CTX *)gasnetc_elan_ctx)
#define TPORT() (gasnetc_elan_tport)

#define GASNETI_EADDRFMT "0x%08x"
#define GASNETI_EADDRSTR(ptr) ((uint32_t)(uintptr_t)(ptr))

/* GASNet-elan system configuration parameters */
#define GASNETC_MAX_RECVMSGS_PER_POLL 10  /* max number of waiting messages serviced per poll (0 for unlimited) */
#define GASNETC_PREPOST_RECVS         1   /* pre-post non-blocking tport recv's */
#define GASNETC_ELAN_MAX_QUEUEMSG   320   /* max message in a mainqueue */
#define GASNETC_ELAN_SMALLPUTSZ      64   /* max put that elan_put copies to an elan buffer */

#ifndef GASNETC_PREALLOC_AMLONG_BOUNCEBUF
#define GASNETC_PREALLOC_AMLONG_BOUNCEBUF 1
#endif

#ifndef GASNETC_USE_SIGNALING_EXIT
  #ifdef GASNETI_USE_GENERIC_ATOMICOPS
    #define GASNETC_USE_SIGNALING_EXIT 0 /* need real atomic ops for signalling exit */
  #else
    #define GASNETC_USE_SIGNALING_EXIT 1
  #endif
#endif

#if GASNETC_USE_SIGNALING_EXIT
  extern gasneti_atomic_t gasnetc_remoteexitflag;
  extern gasneti_atomic_t gasnetc_remoteexitrecvd; 
  #define GASNETC_EXITINPROGRESS()       (gasneti_atomic_read(&gasnetc_remoteexitflag) != 1)
  #define GASNETC_REMOTEEXITINPROGRESS() (gasneti_atomic_read(&gasnetc_remoteexitrecvd) != 0)
#else 
  #define GASNETC_EXITINPROGRESS() 0
  #define GASNETC_REMOTEEXITINPROGRESS() 0
#endif


/* message flags */
 /* 0-1: category
  * 2:   request vs. reply 
  * 3-7: numargs
  */
typedef unsigned char gasnetc_flag_t;
typedef enum {
  gasnetc_Short=0, 
  gasnetc_Medium=1, 
  gasnetc_Long=2,
  gasnetc_System=3
  } gasnetc_category_t;

#define GASNETC_MSG_SETFLAGS(pmsg, isreq, cat, numargs) \
  ((pmsg)->flags = (gasnetc_flag_t) (                   \
                   (((numargs) & 0x1F) << 3)            \
                 | (((isreq) & 0x1) << 2)               \
                 |  ((cat) & 0x3)                       \
                   ))
#define GASNETC_MSG_NUMARGS(pmsg)   ( ( ((unsigned char)(pmsg)->flags) >> 3 ) & 0x1F)
#define GASNETC_MSG_ISREQUEST(pmsg) (!!(((unsigned char)(pmsg)->flags) & 0x4))
#define GASNETC_MSG_CATEGORY(pmsg)  ((gasnetc_category_t)((pmsg)->flags & 0x3))

/* active message header & meta info fields */
typedef struct {
  gasnetc_flag_t    flags;
  gasnet_handler_t  handlerId;
  uint16_t          sourceId;
} gasnetc_msg_t;
typedef gasnetc_msg_t gasnetc_shortmsg_t;

typedef struct {
  gasnetc_flag_t    flags;
  gasnet_handler_t  handlerId;
  uint16_t          sourceId;

  uint16_t          nBytes;
  uint16_t          _pad;
} gasnetc_medmsg_t;

typedef struct {
  gasnetc_flag_t    flags;
  gasnet_handler_t  handlerId;
  uint16_t          sourceId;

  uint32_t          nBytes;
  uintptr_t	    destLoc;
} gasnetc_longmsg_t;


/* active message buffer, including message and space for data payload */
typedef struct gasnetc_buf {
  union {
    gasnetc_msg_t     msg;
    gasnetc_medmsg_t  medmsg;
    gasnetc_longmsg_t longmsg;
  };
  uint8_t     _Data[(4*GASNETC_MAX_SHORT)+GASNETC_MAX_MEDIUM]; /* holds args and data */
} gasnetc_buf_t;

/* buffer descriptor and bookkeeping info 
 */
typedef struct _gasnetc_bufdesc_t {
  gasnetc_buf_t *buf;        /* buf currently associated w/ descriptor (may be a system buf) */
  gasnetc_buf_t *buf_owned;  /* buf permanently owned by this desc (rx bufs only) */
  ELAN_EVENT *event;       /* packets in tport rx or tx queue, or NULL */
  struct _gasnetc_bufdesc_t *next;       /* tport rx or tx queue list */
  int8_t   handlerRunning; /* received packets only */
  int8_t   replyIssued;    /* received packets only */
} gasnetc_bufdesc_t;

typedef void (*gasnetc_HandlerShort) (gasnet_token_t token, ...);
typedef void (*gasnetc_HandlerMedium)(gasnet_token_t token, void *buf, size_t nbytes, ...);
typedef void (*gasnetc_HandlerLong)  (gasnet_token_t token, void *buf, size_t nbytes, ...);

#define GASNETC_MAX_NUMHANDLERS   256
typedef void (*gasnetc_handler_fn_t)();  /* prototype for handler function */
gasnetc_handler_fn_t gasnetc_handler[GASNETC_MAX_NUMHANDLERS]; /* handler table */

extern int gasnetc_RequestGeneric(gasnetc_category_t category, 
                         int dest, gasnet_handler_t handler, 
                         void *source_addr, int nbytes, void *dest_ptr, 
                         int numargs, va_list argptr);
extern int gasnetc_ReplyGeneric(gasnetc_category_t category, 
                         gasnet_token_t token, gasnet_handler_t handler, 
                         void *source_addr, int nbytes, void *dest_ptr, 
                         int numargs, va_list argptr);

extern void gasnetc_initbufs();

#ifdef GASNETI_THREADS
  #define gasnetc_mythread() ((void**)(gasnete_mythread()))
#else
  void **_gasnetc_mythread;
  #define gasnetc_mythread() _gasnetc_mythread
#endif

/* status dumping functions */
extern void gasnetc_dump_base();
extern void gasnetc_dump_state();
extern void gasnetc_dump_group();
extern void gasnetc_dump_envvars();
extern void gasnetc_dump_tportstats();
extern void gasnetc_dump_groupstats();
/* ------------------------------------------------------------------------------------ */
#define RUN_HANDLER_SHORT(phandlerfn, token, pArgs, numargs) do {                       \
  assert(phandlerfn);                                                                   \
  if (numargs == 0) (*(gasnetc_HandlerShort)phandlerfn)((void *)token);                   \
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
  assert(phandlerfn);                                                         \
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
    assert(((uintptr_t)pData) % 8 == 0);  /* we guarantee double-word alignment for data payload of medium xfers */ \
    _RUN_HANDLER_MEDLONG((gasnetc_HandlerMedium)phandlerfn, (gasnet_token_t)token, pArgs, numargs, (void *)pData, (int)datalen); \
    } while(0)
#define RUN_HANDLER_LONG(phandlerfn, token, pArgs, numargs, pData, datalen)             \
  _RUN_HANDLER_MEDLONG((gasnetc_HandlerLong)phandlerfn, (gasnet_token_t)token, pArgs, numargs, (void *)pData, (int)datalen)
/* ------------------------------------------------------------------------------------ */

/* Elan conduit locks:
    elan lock - protects all elan calls and tport rx fifo
    sendfifo lock - protects tport tx fifo
   if you need both, must acquire sendfifo lock first
 */
extern gasneti_mutex_t gasnetc_elanLock;
extern gasneti_mutex_t gasnetc_sendfifoLock;
#define LOCK_ELAN()       gasneti_mutex_lock(&gasnetc_elanLock)
#define UNLOCK_ELAN()     gasneti_mutex_unlock(&gasnetc_elanLock)
#define LOCK_SENDFIFO()   gasneti_mutex_lock(&gasnetc_sendfifoLock)
#define UNLOCK_SENDFIFO() gasneti_mutex_unlock(&gasnetc_sendfifoLock)

#define ASSERT_ELAN_LOCKED() gasneti_mutex_assertlocked(&gasnetc_elanLock)
#define ASSERT_ELAN_UNLOCKED() gasneti_mutex_assertunlocked(&gasnetc_elanLock)
#define ASSERT_SENDFIFO_LOCKED() gasneti_mutex_assertlocked(&gasnetc_sendfifoLock)
#define ASSERT_SENDFIFO_UNLOCKED() gasneti_mutex_assertunlocked(&gasnetc_sendfifoLock)

/* (UN)LOCK_ELAN_WEAK is used when we only need mutual exclusion for the
   purposes of an elan call (which quadrics claims are all thread-safe)
   OK - so apparently the elan library is only threadsafe starting in v1.4
 */
#ifdef GASNETI_THREADS
  #if defined(ELAN_VER_1_2) || defined(ELAN_VER_1_3)
    /* use real locks to provide thread-safety */
    #define LOCK_ELAN_WEAK()    LOCK_ELAN()
    #define UNLOCK_ELAN_WEAK()  UNLOCK_ELAN()
    #define ASSERT_ELAN_LOCKED_WEAK() gasneti_mutex_assertlocked(&gasnetc_elanLock)
  #else
    /* elan library v1.4+ thread-safe - no weak locking required */
    #define LOCK_ELAN_WEAK()
    #define UNLOCK_ELAN_WEAK()
    #define ASSERT_ELAN_LOCKED_WEAK()
  #endif
#else
  /* doesn't actually lock anything - just preserves debug checking */
  #define LOCK_ELAN_WEAK()    LOCK_ELAN()
  #define UNLOCK_ELAN_WEAK()  UNLOCK_ELAN()
  #define ASSERT_ELAN_LOCKED_WEAK() gasneti_mutex_assertlocked(&gasnetc_elanLock)
#endif

#define UNLOCKRELOCK_ELAN_WEAK(cmd) do { \
    UNLOCK_ELAN_WEAK();                  \
    cmd;                                 \
    LOCK_ELAN_WEAK();                    \
  } while (0)

#if defined(TRACE) || defined(STATS)
  /* wrap around trace calls in locked sections to prevent 
     weak lock violation on elan_clock() */
  #define UNLOCKRELOCK_ELAN_WEAK_IFTRACE(cmd) UNLOCKRELOCK_ELAN_WEAK(cmd)
#else
  #define UNLOCKRELOCK_ELAN_WEAK_IFTRACE(cmd) 
#endif

#endif

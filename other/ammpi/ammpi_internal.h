/*  $Archive:: /Ti/AMMPI/ammpi_internal.h                                 $
 *     $Date: 2004/01/05 15:34:34 $
 * $Revision: 1.14 $
 * Description: AMMPI internal header file
 * Copyright 2000, Dan Bonachea <bonachea@cs.berkeley.edu>
 */

#ifndef _AMMPI_INTERNAL_H
#define _AMMPI_INTERNAL_H

#include <stdio.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>

#ifdef UNIX
  #include <unistd.h>
  #include <errno.h>
#endif

#ifdef __AMMPI_H
#error AMMPI library files should not include ammpi.h directly
#endif
#define AMMPI_INTERNAL
#include <ammpi.h>

#if ! defined (__GNUC__) && ! defined (__attribute__)
#define __attribute__(flags)
#endif

/* AMMPI system configuration parameters */
#define AMMPI_MAX_RECVMSGS_PER_POLL 10  /* max number of waiting messages serviced per poll (0 for unlimited) */
#define AMMPI_INITIAL_NUMENDPOINTS  1   /* initial size of bundle endpoint table */
#define AMMPI_DEFAULT_NETWORKDEPTH  4   /* default depth if none specified */
#define AMMPI_MPIIRECV_ORDERING_WORKS 1 /* assume recv matching correctly ordered as reqd by MPI spec */
#define AMMPI_PREPOST_RECVS         1   /* pre-post non-blocking MPI recv's */
#define AMMPI_NONBLOCKING_SENDS     1   /* use non-blocking MPI send's */
#if AMMPI_NONBLOCKING_SENDS
#define AMMPI_SENDBUFFER_SZ         2*AMMPI_MAX_NETWORK_MSG /* size of MPI send buffer (used for rejections) */
#else
#define AMMPI_SENDBUFFER_SZ         1048576 /* size of MPI send buffer */
#endif

/* AMMPI-SPMD system configuration parameters */
#define USE_MPI_COMMUNICATORS       1   /* use MPI communicators to isolate endpoints */
#define ABORT_JOB_ON_NODE_FAILURE   1   /* kill everyone if any slave drops the TCP coord */
#define USE_BLOCKING_SPMD_BARRIER   1   /* use blocking AM calls in SPMDBarrier() */

#ifndef AMMPI_DEBUG_VERBOSE
  #if GASNET_DEBUG_VERBOSE
    #define AMMPI_DEBUG_VERBOSE       1
  #else
    #define AMMPI_DEBUG_VERBOSE       0
  #endif
#endif

#if !defined(AMMPI_DEBUG) && !defined(AMMPI_NDEBUG)
  #if defined(GASNET_DEBUG)
    #define AMMPI_DEBUG 1
  #elif defined(GASNET_NDEBUG)
    #define AMMPI_NDEBUG 1
  #endif
#endif
#if defined(AMMPI_DEBUG) && !defined(AMMPI_NDEBUG)
  #undef AMMPI_DEBUG
  #define AMMPI_DEBUG 1
#elif !defined(AMMPI_DEBUG) && defined(AMMPI_NDEBUG)
  #undef AMMPI_NDEBUG
  #define AMMPI_NDEBUG 1
#else
  #error bad defns of AMMPI_DEBUG and AMMPI_NDEBUG
#endif

#ifndef TRUE
#define TRUE 1
#endif
#ifndef FALSE
#define FALSE 0
#endif
#ifndef MIN
#define MIN(x,y)  ((x)<(y)?(x):(y))
#endif
#ifndef MAX
#define MAX(x,y)  ((x)>(y)?(x):(y))
#endif
#if defined(__GNUC__) || defined(__FUNCTION__) /* try to get the function name from GCC */
  #define __CURR_FUNCTION __FUNCTION__
#else
  #define __CURR_FUNCTION ((const char *) 0) /* could use __func__ for C99 compilers.. */
#endif

#if ! defined (__GNUC__) && ! defined (__attribute__)
  #define __attribute__(flags)
#endif

/* ------------------------------------------------------------------------------------ */
/* Branch prediction:
   these macros return the value of the expression given, but pass on
   a hint that you expect the value to be true or false.
   Use them to wrap the conditional expression in an if stmt when
   you have strong reason to believe the branch will frequently go
   in one direction and the branch is a bottleneck
 */
#ifndef PREDICT_TRUE
  #if defined(__GNUC__) && defined(HAVE_BUILTIN_EXPECT)
    /* cast to uintptr_t avoids warnings on some compilers about passing 
       non-integer arguments to __builtin_expect(), and we don't use (int)
       because on some systems this is smaller than (void*) and causes 
       other warnings
     */
   #define PREDICT_TRUE(exp)  __builtin_expect( ((uintptr_t)(exp)), 1 )
   #define PREDICT_FALSE(exp) __builtin_expect( ((uintptr_t)(exp)), 0 )
  #else
   #define PREDICT_TRUE(exp)  (exp)
   #define PREDICT_FALSE(exp) (exp)
  #endif
#endif

/* if with branch prediction */
#ifndef if_pf
  #define if_pf(cond) if (PREDICT_FALSE(cond))
  #define if_pt(cond) if (PREDICT_TRUE(cond))
#endif

BEGIN_EXTERNC

/*------------------------------------------------------------------------------------
 * Error reporting
 *------------------------------------------------------------------------------------ */
#ifdef _MSC_VER
  #pragma warning(disable: 4127)
#endif
#ifdef __SUNPRO_C
  #pragma error_messages(off, E_END_OF_LOOP_CODE_NOT_REACHED)
#endif
static const char *AMMPI_ErrorName(int errval) {
  switch (errval) {
    case AM_ERR_NOT_INIT: return "AM_ERR_NOT_INIT";      
    case AM_ERR_BAD_ARG:  return "AM_ERR_BAD_ARG";       
    case AM_ERR_RESOURCE: return "AM_ERR_RESOURCE";      
    case AM_ERR_NOT_SENT: return "AM_ERR_NOT_SENT";      
    case AM_ERR_IN_USE:   return "AM_ERR_IN_USE";       
    default: return "*unknown*";
    }
  }
static const char *AMMPI_ErrorDesc(int errval) {
  switch (errval) {
    case AM_ERR_NOT_INIT: return "Active message layer not initialized"; 
    case AM_ERR_BAD_ARG:  return "Invalid function parameter passed";    
    case AM_ERR_RESOURCE: return "Problem with requested resource";      
    case AM_ERR_NOT_SENT: return "Synchronous message not sent";  
    case AM_ERR_IN_USE:   return "Resource currently in use";     
    default: return "no description available";
    }
  }
static const char *MPI_ErrorName(int errval) {
  const char *code = NULL;
  char systemErrDesc[MPI_MAX_ERROR_STRING+10];
  int len = MPI_MAX_ERROR_STRING;
  static char msg[MPI_MAX_ERROR_STRING+100];
  switch (errval) {
    case MPI_ERR_BUFFER:    code = "MPI_ERR_BUFFER"; break;     
    case MPI_ERR_COUNT:     code = "MPI_ERR_COUNT"; break;     
    case MPI_ERR_TYPE:      code = "MPI_ERR_TYPE"; break;      
    case MPI_ERR_TAG:       code = "MPI_ERR_TAG"; break;      
    case MPI_ERR_COMM:      code = "MPI_ERR_COMM"; break;      
    case MPI_ERR_RANK:      code = "MPI_ERR_RANK"; break;      
    case MPI_ERR_REQUEST:   code = "MPI_ERR_REQUEST"; break;      
    case MPI_ERR_ROOT:      code = "MPI_ERR_ROOT"; break;      
    case MPI_ERR_GROUP:     code = "MPI_ERR_GROUP"; break;      
    case MPI_ERR_OP:        code = "MPI_ERR_OP"; break;      
    case MPI_ERR_TOPOLOGY:  code = "MPI_ERR_TOPOLOGY"; break;      
    case MPI_ERR_DIMS:      code = "MPI_ERR_DIMS"; break;      
    case MPI_ERR_ARG:       code = "MPI_ERR_ARG"; break;      
    case MPI_ERR_UNKNOWN:   code = "MPI_ERR_UNKNOWN"; break;      
    case MPI_ERR_TRUNCATE:  code = "MPI_ERR_TRUNCATE"; break;      
    case MPI_ERR_OTHER:     code = "MPI_ERR_OTHER"; break;      
    case MPI_ERR_INTERN:    code = "MPI_ERR_INTERN"; break;      
    case MPI_ERR_PENDING:   code = "MPI_ERR_PENDING"; break;      
    case MPI_ERR_IN_STATUS: code = "MPI_ERR_IN_STATUS"; break;      
    case MPI_ERR_LASTCODE:  code = "MPI_ERR_LASTCODE";  break;     
    default: code = "*unknown MPI error*";
    }
  if (MPI_Error_string(errval, systemErrDesc, &len) != MPI_SUCCESS || len == 0) 
    strcpy(systemErrDesc, "(no description available)");
  sprintf(msg, "%s(%i): %s", code, errval, systemErrDesc);
  return msg;
  }
/* ------------------------------------------------------------------------------------ */
/* macros for returning errors that allow verbose error tracking */
#define AMMPI_RETURN_ERR(type) do {                                      \
  if (AMMPI_VerboseErrors) {                                             \
    fprintf(stderr, "AMMPI %s returning an error code: AM_ERR_%s (%s)\n" \
      "  at %s:%i\n"                                                     \
      ,(__CURR_FUNCTION ? __CURR_FUNCTION : "")                          \
      , #type, AMMPI_ErrorDesc(AM_ERR_##type), __FILE__, __LINE__);      \
    fflush(stderr);                                                      \
    }                                                                    \
  return AM_ERR_ ## type;                                                \
  } while (0)
#define AMMPI_RETURN_ERRF(type, fromfn) do {                                 \
  if (AMMPI_VerboseErrors) {                                                 \
    fprintf(stderr, "AMMPI %s returning an error code: AM_ERR_%s (%s)\n"     \
      "  from function %s\n"                                                 \
      "  at %s:%i\n"                                                         \
      ,(__CURR_FUNCTION ? __CURR_FUNCTION : "")                              \
      , #fromfn, #type, AMMPI_ErrorDesc(AM_ERR_##type), __FILE__, __LINE__); \
    fflush(stderr);                                                          \
    }                                                                        \
  return AM_ERR_ ## type;                                                    \
  } while (0)
#define AMMPI_RETURN_ERRFR(type, fromfn, reason) do {                                \
  if (AMMPI_VerboseErrors) {                                                         \
    fprintf(stderr, "AMMPI %s returning an error code: AM_ERR_%s (%s)\n"             \
      "  from function %s\n"                                                         \
      "  at %s:%i\n"                                                                 \
      "  reason: %s\n"                                                               \
      ,(__CURR_FUNCTION ? __CURR_FUNCTION : "")                                      \
      , #type, AMMPI_ErrorDesc(AM_ERR_##type), #fromfn, __FILE__, __LINE__, reason); \
    fflush(stderr);                                                                  \
    }                                                                                \
  return AM_ERR_ ## type;                                                            \
  } while (0)

/* make an MPI call - if it fails, print error message and return */
#define MPI_SAFE(fncall) do {                                                                     \
   int retcode = (fncall);                                                                        \
   if_pf (retcode != MPI_SUCCESS) {                                                               \
     char msg[1024];                                                                              \
     sprintf(msg, "\nAMMPI encountered an MPI Error: %s(%i)\n", MPI_ErrorName(retcode), retcode); \
     AMMPI_RETURN_ERRFR(RESOURCE, fncall, msg);                                                   \
   }                                                                                              \
 } while (0)

/* make an MPI call - 
 * if it fails, print error message and value of expression is FALSE, 
 * otherwise, the value of this expression will be TRUE 
 */
#define MPI_SAFE_NORETURN(fncall) (AMMPI_VerboseErrors ?                                 \
      AMMPI_checkMPIreturn(fncall, #fncall,                                              \
                          (__CURR_FUNCTION ? __CURR_FUNCTION : ""), __FILE__, __LINE__): \
      (fncall) == MPI_SUCCESS)
static int AMMPI_checkMPIreturn(int retcode, const char *fncallstr, 
                                const char *context, const char *file, int line) {
   if_pf (retcode != MPI_SUCCESS) {  
     fprintf(stderr, "\nAMMPI %s encountered an MPI Error: %s(%i)\n"
                     "  at %s:%i\n", 
       context, MPI_ErrorName(retcode), retcode, file, line); 
     fflush(stderr);
     return FALSE;
   }
   else return TRUE;
}

/* return a possible error */
#define AMMPI_RETURN(val) do {                                           \
  if_pf (AMMPI_VerboseErrors && val != AM_OK) {                          \
    fprintf(stderr, "AMMPI %s returning an error code: %s (%s)\n"        \
      "  at %s:%i\n"                                                     \
      ,(__CURR_FUNCTION ? __CURR_FUNCTION : "")                          \
      , AMMPI_ErrorName(val), AMMPI_ErrorDesc(val), __FILE__, __LINE__); \
    fflush(stderr);                                                      \
    }                                                                    \
  return val;                                                            \
  } while (0)

static int ErrMessage(const char *msg, ...) __attribute__((__format__ (__printf__, 1, 2)));
static int ErrMessage(const char *msg, ...) {
  static va_list argptr;
  char *expandedmsg = (char *)malloc(strlen(msg)+50);
  int retval;

  va_start(argptr, msg); /*  pass in last argument */
  sprintf(expandedmsg, "*** AMMPI ERROR: %s\n", msg);
  retval = vfprintf(stderr, expandedmsg, argptr);
  fflush(stderr);
  free(expandedmsg);

  va_end(argptr);
  return retval; /*  this MUST be only return in this function */
  }

#include <assert.h>
#undef assert
#define assert(x) !!! Error - use AMMPI_assert() !!!
#if AMMPI_NDEBUG
  #define AMMPI_assert(expr) ((void)0)
#else
  static void AMMPI_assertfail(const char *fn, const char *file, int line, const char *expr) {
    fprintf(stderr, "Assertion failure at %s %s:%i: %s\n", fn, file, line, expr);
    fflush(stderr);
    abort();
  }
  #define AMMPI_assert(expr)                                     \
    (PREDICT_TRUE(expr) ? (void)0 :                              \
      AMMPI_assertfail((__CURR_FUNCTION ? __CURR_FUNCTION : ""), \
                        __FILE__, __LINE__, #expr))
#endif

extern int enEqual(en_t en1, en_t en2);
extern int64_t getMicrosecondTimeStamp();
/* ------------------------------------------------------------------------------------ */
/*  global data */
extern int AMMPI_numBundles;
extern eb_t AMMPI_bundles[AMMPI_MAX_BUNDLES];

extern int ammpi_Initialized;
#define AMMPI_CHECKINIT() if_pf (!ammpi_Initialized) AMMPI_RETURN_ERR(NOT_INIT)
/* ------------------------------------------------------------------------------------ */
/*  global helper functions */
extern int AMMPI_Block(eb_t eb); 
  /* block until some endpoint receive buffer becomes non-empty with a user message
   * may poll, and does handle SPMD control events
   */
extern int AMMPI_ServiceIncomingMessages(ep_t ep, int blockForActivity, int *numUserHandlersRun);

/*  debugging printouts */
extern char *AMMPI_enStr(en_t en, char *buf);
extern char *AMMPI_tagStr(tag_t tag, char *buf);

void abort();
extern ammpi_handler_fn_t ammpi_unused_handler;
extern void AMMPI_DefaultReturnedMsg_Handler(int status, op_t opcode, void *token);

/* ------------------------------------------------------------------------------------ */
/* interface for allowing control messages to be sent between mutually mapped endpoints 
 * up to AMMPI_MAX_SHORT integer arguments are passed verbatim to the registered handler,
 * which should NOT call any AMMPI functions (including poll, reply, etc)
 */
extern int AMMPI_SendControlMessage(ep_t from, en_t to, int numargs, ...);
  /* beware - cast all optional args of AMMPI_SendControlMessage to int32_t */
extern int AMMPI_RegisterControlMessageHandler(ep_t ea, ammpi_handler_fn_t function);

#if AMMPI_NONBLOCKING_SENDS
extern int AMMPI_AllocateSendBuffers(ep_t ep);
extern int AMMPI_ReleaseSendBuffers(ep_t ep);
extern int AMMPI_AcquireSendBuffer(ep_t ep, int numBytes, int isrequest, 
                            ammpi_buf_t** pbuf, MPI_Request** pHandle);
#endif

/* ------------------------------------------------------------------------------------ */
/* AMMPI_IDENT() takes a unique identifier and a textual string and embeds the textual
   string in the executable file
 */
#define AMMPI_PRAGMA(x) _Pragma ( #x )
#define _AMMPI_IDENT(identName, identText)  \
  extern char volatile identName[];         \
  char volatile identName[] = identText;    \
  extern char *_get_##identName() { return (char*)identName; } \
  static int _dummy_##identName = sizeof(_dummy_##identName)
#if defined(_CRAYC)
  #define AMMPI_IDENT(identName, identText) \
    AMMPI_PRAGMA(_CRI ident identText);     \
    _AMMPI_IDENT(identName, identText)
#elif defined(__xlC__)
    /* #pragma comment(user,"text...") 
         or
       _Pragma ( "comment (user,\"text...\")" );
       are both supposed to work according to compiler docs, but both appear to be broken
     */
  #define AMMPI_IDENT(identName, identText)   \
    _AMMPI_IDENT(identName, identText)
#else
  #define AMMPI_IDENT _AMMPI_IDENT
#endif
/* ------------------------------------------------------------------------------------ */

/*  handler prototypes */
typedef void (*AMMPI_HandlerShort)(void *token, ...);
typedef void (*AMMPI_HandlerMedium)(void *token, void *buf, int nbytes, ...);
typedef void (*AMMPI_HandlerLong)(void *token, void *buf, int nbytes, ...);
typedef void (*AMMPI_HandlerReturned)(int status, op_t opcode, void *token);


/* system message type field:
 *  low  4 bits are actual type
 *  high 4 bits are bulk transfer slot (all zero for non-bulk messages)
 * slot is only used if bulk xfer is spanned - more than one packet (arg > 0)
 */

typedef enum {
  ammpi_system_user=0,      /*  not a system message */
  ammpi_system_autoreply,   /*  automatically generated reply */
  ammpi_system_returnedmessage, /*  arg is reason code, req/rep represents the type of message refused */
  ammpi_system_controlmessage, /*  used to pass system control information - arg is reserved */

  ammpi_system_numtypes
  } ammpi_system_messagetype_t;


/* ------------------------------------------------------------------------------------ */

END_EXTERNC

#endif

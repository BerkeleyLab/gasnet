/*   $Source: /Users/kamil/work/gasnet-cvs2/gasnet/gasnet_internal.h,v $
 *     $Date: 2005/04/28 02:08:27 $
 * $Revision: 1.61.2.4 $
 * Description: GASNet header for internal definitions used in GASNet implementation
 * Copyright 2002, Dan Bonachea <bonachea@cs.berkeley.edu>
 * Terms of use are as specified in license.txt
 */

#ifndef _GASNET_INTERNAL_H
#define _GASNET_INTERNAL_H
#define _IN_GASNET_INTERNAL_H
#define _INCLUDED_GASNET_INTERNAL_H
#ifdef _INCLUDED_GASNET_H
  #error Internal GASNet code should not directly include gasnet.h, just gasnet_internal.h
#endif

#include <gasnet.h> /* MUST come first to ensure correct inttypes behavior */
#include <gasnet_tools.h>
#include <gasnet_atomicops_internal.h>

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <ctype.h>

BEGIN_EXTERNC

#if defined(_AIX)
  /* AIX's stdio.h won't provide prototypes for snprintf() and vsnprintf()
   * by default since they are in C99 but not C89.
   */
  extern int snprintf(char * s, size_t n, const char * format, ...)
					__attribute__((__format__ (__printf__, 3, 4)));
  extern int vsnprintf(char * s, size_t n, const char * format, va_list ap)
					__attribute__((__format__ (__printf__, 3, 0)));
#endif

#ifdef __SUNPRO_C
  #pragma error_messages(off, E_END_OF_LOOP_CODE_NOT_REACHED)
#endif

#ifdef __osf__
  /* replace a stupidly broken implementation of toupper on Tru64 
     (fails to correctly implement required integral promotion of
      character-typed arguments, leading to bogus warnings)
   */
  #undef toupper
  #define toupper(c) ((c) >= 'a' && (c) <= 'z' ? (c) & 0x5F:(c))
#endif

extern int gasneti_init_done; /*  true after init */
extern int gasneti_attach_done; /*  true after attach */

/* conduit-independent sanity checks */
extern void gasneti_check_config_preinit();
extern void gasneti_check_config_postattach();

/* Safe memory allocation/deallocation 
   Beware - in debug mode, gasneti_malloc/gasneti_calloc/gasneti_free are NOT
   compatible with malloc/calloc/free
   (freeing memory allocated from one using the other is likely to crash)
 */
#define gasneti_malloc(sz)           _gasneti_malloc((sz) GASNETI_CURLOCAARG)
#define gasneti_malloc_allowfail(sz) _gasneti_malloc_allowfail((sz) GASNETI_CURLOCAARG)
#define gasneti_calloc(N,S)          _gasneti_calloc(N,S GASNETI_CURLOCAARG)
#define gasneti_realloc(ptr,sz)      _gasneti_realloc((ptr),(sz) GASNETI_CURLOCAARG)
#define gasneti_free(ptr)	     _gasneti_free((ptr) GASNETI_CURLOCAARG)
#define gasneti_strdup(ptr)	     _gasneti_strdup((ptr) GASNETI_CURLOCAARG)
#define gasneti_strndup(ptr,sz)      _gasneti_strndup((ptr),(sz) GASNETI_CURLOCAARG)
/* corresponding gasneti_memcheck fns are in gasnet_help.h */

#if GASNET_DEBUG
  extern void *_gasneti_malloc(size_t nbytes, const char *curloc) __attribute__((__malloc__));
  extern void *_gasneti_malloc_allowfail(size_t nbytes, const char *curloc) __attribute__((__malloc__));
  extern void _gasneti_free(void *ptr, const char *curloc);
  extern void *_gasneti_realloc(void *ptr, size_t sz, const char *curloc);
  extern void *_gasneti_calloc(size_t N, size_t S, const char *curloc) __attribute__((__malloc__));
  extern size_t _gasneti_memcheck(void *ptr, const char *curloc, int checktype);
#else
  #ifdef __GNUC__
    /* provide gcc with additional information about the aliasing qualities
       of the return value (being malloc-like) to improve caller optimization */
    GASNET_INLINE_MODIFIER(_gasneti_malloc)
    void *_gasneti_malloc(size_t nbytes) __attribute__((__malloc__));
    GASNET_INLINE_MODIFIER(_gasneti_malloc_allowfail)
    void *_gasneti_malloc_allowfail(size_t nbytes) __attribute__((__malloc__));
    GASNET_INLINE_MODIFIER(_gasneti_calloc)
    void *_gasneti_calloc(size_t N, size_t S) __attribute__((__malloc__));
  #endif
  GASNET_INLINE_MODIFIER(_gasneti_malloc)
  void *_gasneti_malloc(size_t nbytes) {
    void *ret = NULL;
    GASNETI_STAT_EVENT_VAL(I, GASNET_MALLOC, nbytes);
    if_pt (gasneti_attach_done) gasnet_hold_interrupts();
    ret = malloc(nbytes);
    if_pf (ret == NULL && nbytes > 0) 
      gasneti_fatalerror("gasneti_malloc(%d) failed", (int)nbytes);
    if_pt (gasneti_attach_done) gasnet_resume_interrupts();
    return ret;
  }
  GASNET_INLINE_MODIFIER(_gasneti_malloc_allowfail)
  void *_gasneti_malloc_allowfail(size_t nbytes) {
    void *ret = NULL;
    GASNETI_STAT_EVENT_VAL(I, GASNET_MALLOC, nbytes);
    if_pt (gasneti_attach_done) gasnet_hold_interrupts();
    ret = malloc(nbytes);
    if_pf (ret == NULL && nbytes > 0) /* allow a NULL return for out-of-memory */
      GASNETI_TRACE_PRINTF(I,("Warning: returning NULL for a failed gasneti_malloc(%i)",(int)nbytes));
    if_pt (gasneti_attach_done) gasnet_resume_interrupts();
    return ret;
  }
  GASNET_INLINE_MODIFIER(_gasneti_calloc)
  void *_gasneti_calloc(size_t N, size_t S) {
    void *ret = NULL;
    GASNETI_STAT_EVENT_VAL(I, GASNET_MALLOC, (N*S));
    if_pt (gasneti_attach_done) gasnet_hold_interrupts();
    ret = calloc(N,S);
    if_pf (ret == NULL && N*S > 0) 
      gasneti_fatalerror("gasneti_calloc(%d,%d) failed", (int)N, (int)S);
    if_pt (gasneti_attach_done) gasnet_resume_interrupts();
    return ret;
  }
  GASNET_INLINE_MODIFIER(_gasneti_realloc)
  void *_gasneti_realloc(void *ptr, size_t nbytes) {
    void *ret = NULL;
    GASNETI_STAT_EVENT_VAL(I, GASNET_MALLOC, nbytes);
    if_pt (gasneti_attach_done) gasnet_hold_interrupts();
    ret = realloc(ptr, nbytes);
    if_pf (ret == NULL && nbytes > 0) 
      gasneti_fatalerror("gasneti_realloc(%d) failed", (int)nbytes);
    if_pt (gasneti_attach_done) gasnet_resume_interrupts();
    return ret;
  }
  GASNET_INLINE_MODIFIER(_gasneti_free)
  void _gasneti_free(void *ptr) {
    GASNETI_STAT_EVENT_VAL(I, GASNET_FREE, 0); /* don't track free size in ndebug mode */
    if_pf (ptr == NULL) return;
    if_pt (gasneti_attach_done) gasnet_hold_interrupts();
    free(ptr);
    if_pt (gasneti_attach_done) gasnet_resume_interrupts();
  }
  #ifdef __SUNPRO_C
    #pragma returns_new_memory(_gasneti_malloc,_gasneti_malloc_allowfail,_gasneti_calloc)
  #endif
#endif

#ifdef malloc
#undef malloc
#endif
#define malloc_error  ERROR__GASNet_conduit_code_must_use_gasneti_malloc
#define malloc() malloc_error
#ifdef calloc
#undef calloc
#endif
#define calloc_error  ERROR__GASNet_conduit_code_must_use_gasneti_calloc
#define calloc() calloc_error
#ifdef realloc
#undef realloc
#endif
#define realloc_error  ERROR__GASNet_conduit_code_must_use_gasneti_realloc
#define realloc() realloc_error
#ifdef strdup
#undef strdup
#endif
#define strdup_error  ERROR__GASNet_conduit_code_must_use_gasneti_strdup
#define strdup() strdup_error
#ifdef strndup
#undef strndup
#endif
#define strndup_error  ERROR__GASNet_conduit_code_must_use_gasneti_strndup
#define strndup() strndup_error
#ifdef free
#undef free
#endif
#define free_error    ERROR__GASNet_conduit_code_must_use_gasneti_free
#define free() free_error

#include <assert.h>
#undef assert
#define assert(x)     ERROR__GASNet_conduit_code_should_use_gasneti_assert

/* ------------------------------------------------------------------------------------ */
/* Version of strdup() which is compatible w/ gasneti_free(), instead of plain free() */
#ifdef __GNUC__ 
  GASNET_INLINE_MODIFIER(_gasneti_strdup)
  char *_gasneti_strdup(const char *s GASNETI_CURLOCFARG) __attribute__((__malloc__));
  GASNET_INLINE_MODIFIER(_gasneti_strndup)
  char *_gasneti_strndup(const char *s, size_t n GASNETI_CURLOCFARG) __attribute__((__malloc__));
#endif
GASNET_INLINE_MODIFIER(_gasneti_strdup)
char *_gasneti_strdup(const char *s GASNETI_CURLOCFARG) {
  char *retval;
  if_pf (s == NULL) {
    /* special case to avoid strlen(NULL) */
    retval = (char *)_gasneti_malloc(1 GASNETI_CURLOCPARG);
    retval[0] = '\0';
  } else {
    size_t sz = strlen(s) + 1;
    retval = (char *)memcpy((char *)_gasneti_malloc(sz GASNETI_CURLOCPARG), s, sz);
  }
  return retval;
}
/* Like gasneti_strdup, but copy is limited to at most n characters.
 * Note allocation is upto n+1 bytes, due to the '\0' termination.
 */
GASNET_INLINE_MODIFIER(_gasneti_strndup)
char *_gasneti_strndup(const char *s, size_t n GASNETI_CURLOCFARG) {
  char *retval;
  if_pf (s == NULL) {
    retval = (char *)_gasneti_malloc(1 GASNETI_CURLOCPARG);
    retval[0] = '\0';
  } else {
    size_t len;
    for (len = 0; len < n && s[len]; len++) ;
    retval = _gasneti_malloc(len + 1 GASNETI_CURLOCPARG);
    memcpy(retval, s, len);
    retval[len] = '\0';
  }
  return retval;
}

/* ------------------------------------------------------------------------------------ */
/* page alignment macros */
#define GASNETI_POWEROFTWO(P)    (((P)&((P)-1)) == 0)

#define GASNETI_ALIGNDOWN(p,P)    (gasneti_assert(GASNETI_POWEROFTWO(P)), \
                                   ((uintptr_t)(p))&~((uintptr_t)((P)-1)))
#define GASNETI_ALIGNUP(p,P)     (GASNETI_ALIGNDOWN((uintptr_t)(p)+((uintptr_t)((P)-1)),P))

#define GASNETI_PAGE_ALIGNDOWN(p) (GASNETI_ALIGNDOWN(p,GASNET_PAGESIZE))
#define GASNETI_PAGE_ALIGNUP(p)   (GASNETI_ALIGNUP(p,GASNET_PAGESIZE))
/* ------------------------------------------------------------------------------------ */

extern void gasneti_freezeForDebugger();
extern void gasneti_killmyprocess(int exitcode) GASNET_NORETURN;
extern void gasneti_flush_streams(); /* flush all open streams */
extern void gasneti_close_streams(); /* close standard streams (for shutdown) */

/* GASNET_DEBUG_VERBOSE is set by configure to request job startup and general 
   status messages on stderr 
*/
#ifndef GASNET_DEBUG_VERBOSE
  #define GASNET_DEBUG_VERBOSE               0
#endif

/* ------------------------------------------------------------------------------------ */
/* memory segment registration and management */

typedef void (*gasneti_sighandlerfn_t)(int);
void gasneti_registerSignalHandlers(gasneti_sighandlerfn_t handler);
void gasneti_defaultSignalHandler(int sig);
gasneti_sighandlerfn_t gasneti_reghandler(int sigtocatch, gasneti_sighandlerfn_t fp);

#ifdef HAVE_MMAP
  extern gasnet_seginfo_t gasneti_mmap_segment_search(uintptr_t maxsz);
  extern void gasneti_mmap_fixed(void *segbase, uintptr_t segsize);
  extern void *gasneti_mmap(uintptr_t segsize);
  extern void gasneti_munmap(void *segbase, uintptr_t segsize);
#endif

#ifndef GASNETI_MMAP_MAX_SIZE
  /* GASNETI_MMAP_MAX_SIZE controls the maz size segment attempted by the mmap binary search
     can't use a full 2 GB due to sign bit problems 
     on the int argument to mmap() for some 32-bit systems
   */
  #define GASNETI_MMAP_MAX_SIZE	  ((((uint64_t)1)<<31) - GASNET_PAGESIZE)  /* 2 GB */
#endif
#define GASNETI_MMAP_LIMIT ((uintptr_t)GASNETI_PAGE_ALIGNDOWN((uint64_t)(GASNETI_MMAP_MAX_SIZE)))

#ifndef GASNETI_MMAP_GRANULARITY
  /* GASNETI_MMAP_GRANULARITY is the minimum increment used by the mmap binary search */
  #define GASNETI_MMAP_GRANULARITY  (((size_t)2)<<21)  /* 4 MB */
#endif
#ifndef GASNETI_MAX_MALLOCSEGMENT_SZ
#define GASNETI_MAX_MALLOCSEGMENT_SZ (100*1048576) /* Max segment sz to use when mmap not avail */
#endif
#ifndef GASNETI_USE_HIGHSEGMENT
#define GASNETI_USE_HIGHSEGMENT 1  /* use the high end of mmap segments */
#endif

typedef void (*gasneti_bootstrapExchangefn_t)(void *src, size_t len, void *dest);
typedef void (*gasneti_bootstrapBroadcastfn_t)(void *src, size_t len, void *dest, int rootnode);

#if !GASNET_SEGMENT_EVERYTHING
void gasneti_segmentInit(uintptr_t localSegmentLimit,
                         gasneti_bootstrapExchangefn_t exchangefn);
void gasneti_segmentAttach(uintptr_t segsize, uintptr_t minheapoffset,
                           gasnet_seginfo_t *seginfo,
                           gasneti_bootstrapExchangefn_t exchangefn);
#endif
void gasneti_setupGlobalEnvironment(gasnet_node_t numnodes, gasnet_node_t mynode,
                                     gasneti_bootstrapExchangefn_t exchangefn,
                                     gasneti_bootstrapBroadcastfn_t broadcastfn);

/* signature for internally-registered functions that need auxseg space -
   space in the gasnet-registered heap which is hidden from the client
   each function is called twice:
   first call is between init/attach with auxseg_info == NULL, 
    function should return the absolute minimum and desired auxseg space
    currently, all nodes MUST return the same value (may be relaxed in the future)
   second call is after attach and before gasnete_init, with auxseg_info
    set to the array (gasnet_nodes() elements) of auxseg components on each node.
    callee must copy the array if it wants to keep it
 */
typedef struct {
  uintptr_t minsz;
  uintptr_t optimalsz;
} gasneti_auxseg_request_t;

typedef gasneti_auxseg_request_t (*gasneti_auxsegregfn_t)(gasnet_seginfo_t *auxseg_info);

/* collect required auxseg sizes and subtract them from the max values to report to client */
void gasneti_auxseg_init();

/* consume the client's segsize request and return the 
   value to acquire including auxseg requirements */
uintptr_t gasneti_auxseg_preattach(uintptr_t client_request_sz);

/* provide auxseg to GASNet components and init secondary segment arrays 
   requires gasneti_seginfo has been initialized to the correct values
 */
void gasneti_auxseg_attach();

#if GASNET_SEGMENT_EVERYTHING
  extern void gasnetc_auxseg_reqh(gasnet_token_t token, void *buf, size_t nbytes, gasnet_handlerarg_t msg);
  #define GASNETC_AUXSEG_HANDLERS() \
    gasneti_handler_tableentry_no_bits(gasnetc_auxseg_reqh)
#endif

/* ------------------------------------------------------------------------------------ */
/* macros for returning errors that allow verbose error tracking */
extern int gasneti_VerboseErrors;
#define GASNETI_RETURN_ERR(type) do {                                        \
  if (gasneti_VerboseErrors) {                                                 \
    fprintf(stderr, "GASNet %s returning an error code: GASNET_ERR_%s (%s)\n" \
      "  at %s:%i\n"                                                         \
      ,GASNETI_CURRENT_FUNCTION                                              \
      , #type, gasnet_ErrorDesc(GASNET_ERR_##type), __FILE__, __LINE__);     \
    fflush(stderr);                                                          \
    }                                                                        \
  return GASNET_ERR_ ## type;                                                \
  } while (0)
#define GASNETI_RETURN_ERRF(type, fromfn) do {                                     \
  if (gasneti_VerboseErrors) {                                                     \
    fprintf(stderr, "GASNet %s returning an error code: GASNET_ERR_%s (%s)\n"      \
      "  from function %s\n"                                                       \
      "  at %s:%i\n"                                                               \
      ,GASNETI_CURRENT_FUNCTION                                                    \
      , #type, gasnet_ErrorDesc(GASNET_ERR_##type), #fromfn, __FILE__, __LINE__);  \
    fflush(stderr);                                                                \
    }                                                                              \
  return GASNET_ERR_ ## type;                                                      \
  } while (0)
#define GASNETI_RETURN_ERRR(type, reason) do {                                             \
  if (gasneti_VerboseErrors) {                                                             \
    fprintf(stderr, "GASNet %s returning an error code: GASNET_ERR_%s (%s)\n"              \
      "  at %s:%i\n"                                                                       \
      "  reason: %s\n"                                                                     \
      ,GASNETI_CURRENT_FUNCTION                                                            \
      , #type, gasnet_ErrorDesc(GASNET_ERR_##type), __FILE__, __LINE__, reason);           \
    fflush(stderr);                                                                        \
    }                                                                                      \
  return GASNET_ERR_ ## type;                                                              \
  } while (0)
#define GASNETI_RETURN_ERRFR(type, fromfn, reason) do {                                    \
  if (gasneti_VerboseErrors) {                                                             \
    fprintf(stderr, "GASNet %s returning an error code: GASNET_ERR_%s (%s)\n"              \
      "  from function %s\n"                                                               \
      "  at %s:%i\n"                                                                       \
      "  reason: %s\n"                                                                     \
      ,GASNETI_CURRENT_FUNCTION                                                            \
      , #type, gasnet_ErrorDesc(GASNET_ERR_##type), #fromfn, __FILE__, __LINE__, reason);  \
    fflush(stderr);                                                                        \
    }                                                                                      \
  return GASNET_ERR_ ## type;                                                              \
  } while (0)

/* return a possible error */
#define GASNETI_RETURN(val) do {                                             \
  if (gasneti_VerboseErrors && val != GASNET_OK) {                           \
    fprintf(stderr, "GASNet %s returning an error code: %s (%s)\n"           \
      "  at %s:%i\n"                                                         \
      ,GASNETI_CURRENT_FUNCTION                                              \
      , gasnet_ErrorName(val), gasnet_ErrorDesc(val), __FILE__, __LINE__);   \
    fflush(stderr);                                                          \
    }                                                                        \
  return val;                                                                \
  } while (0)

/* make a GASNet call - if it fails, print error message and return error */
#define GASNETI_SAFE_PROPAGATE(fncall) do {                  \
   int retcode = (fncall);                                   \
   if_pf (gasneti_VerboseErrors && retcode != GASNET_OK) {   \
     char msg[1024];                                         \
     sprintf(msg, "\nGASNet encountered an error: %s(%i)\n", \
        gasnet_ErrorName(retcode), retcode);                 \
     GASNETI_RETURN_ERRFR(RESOURCE, fncall, msg);            \
   }                                                         \
 } while (0)

/* ------------------------------------------------------------------------------------ */
/* utility macros for dispatching AM handlers */

typedef void (*gasneti_HandlerShort) (gasnet_token_t token, ...);
typedef void (*gasneti_HandlerMedium)(gasnet_token_t token, void *buf, size_t nbytes, ...);
typedef void (*gasneti_HandlerLong)  (gasnet_token_t token, void *buf, size_t nbytes, ...);

/* ------------------------------------------------------------------------------------ */
#define GASNETI_RUN_HANDLER_SHORT(isReq, hid, phandlerfn, token, pArgs, numargs) do { \
  gasneti_assert(phandlerfn);                                                         \
  if (isReq) GASNETI_TRACE_AMSHORT_REQHANDLER(hid, token, numargs, pArgs);            \
  else       GASNETI_TRACE_AMSHORT_REPHANDLER(hid, token, numargs, pArgs);            \
  if (numargs == 0) (*(gasneti_HandlerShort)phandlerfn)((gasnet_token_t)token);       \
  else {                                                                              \
    gasnet_handlerarg_t *_args = (gasnet_handlerarg_t *)(pArgs); /* eval only once */ \
    switch (numargs) {                                                                \
      case 1:  (*(gasneti_HandlerShort)phandlerfn)((gasnet_token_t)token, _args[0]); break; \
      case 2:  (*(gasneti_HandlerShort)phandlerfn)((gasnet_token_t)token, _args[0], _args[1]); break;\
      case 3:  (*(gasneti_HandlerShort)phandlerfn)((gasnet_token_t)token, _args[0], _args[1], _args[2]); break; \
      case 4:  (*(gasneti_HandlerShort)phandlerfn)((gasnet_token_t)token, _args[0], _args[1], _args[2], _args[3]); break; \
      case 5:  (*(gasneti_HandlerShort)phandlerfn)((gasnet_token_t)token, _args[0], _args[1], _args[2], _args[3], _args[4]); break; \
      case 6:  (*(gasneti_HandlerShort)phandlerfn)((gasnet_token_t)token, _args[0], _args[1], _args[2], _args[3], _args[4], _args[5]); break; \
      case 7:  (*(gasneti_HandlerShort)phandlerfn)((gasnet_token_t)token, _args[0], _args[1], _args[2], _args[3], _args[4], _args[5], _args[6]); break; \
      case 8:  (*(gasneti_HandlerShort)phandlerfn)((gasnet_token_t)token, _args[0], _args[1], _args[2], _args[3], _args[4], _args[5], _args[6], _args[7]); break; \
      case 9:  (*(gasneti_HandlerShort)phandlerfn)((gasnet_token_t)token, _args[0], _args[1], _args[2], _args[3], _args[4], _args[5], _args[6], _args[7], _args[8]); break; \
      case 10: (*(gasneti_HandlerShort)phandlerfn)((gasnet_token_t)token, _args[0], _args[1], _args[2], _args[3], _args[4], _args[5], _args[6], _args[7], _args[8], _args[9]); break; \
      case 11: (*(gasneti_HandlerShort)phandlerfn)((gasnet_token_t)token, _args[0], _args[1], _args[2], _args[3], _args[4], _args[5], _args[6], _args[7], _args[8], _args[9], _args[10]); break; \
      case 12: (*(gasneti_HandlerShort)phandlerfn)((gasnet_token_t)token, _args[0], _args[1], _args[2], _args[3], _args[4], _args[5], _args[6], _args[7], _args[8], _args[9], _args[10], _args[11]); break; \
      case 13: (*(gasneti_HandlerShort)phandlerfn)((gasnet_token_t)token, _args[0], _args[1], _args[2], _args[3], _args[4], _args[5], _args[6], _args[7], _args[8], _args[9], _args[10], _args[11], _args[12]); break; \
      case 14: (*(gasneti_HandlerShort)phandlerfn)((gasnet_token_t)token, _args[0], _args[1], _args[2], _args[3], _args[4], _args[5], _args[6], _args[7], _args[8], _args[9], _args[10], _args[11], _args[12], _args[13]); break; \
      case 15: (*(gasneti_HandlerShort)phandlerfn)((gasnet_token_t)token, _args[0], _args[1], _args[2], _args[3], _args[4], _args[5], _args[6], _args[7], _args[8], _args[9], _args[10], _args[11], _args[12], _args[13], _args[14]); break; \
      case 16: (*(gasneti_HandlerShort)phandlerfn)((gasnet_token_t)token, _args[0], _args[1], _args[2], _args[3], _args[4], _args[5], _args[6], _args[7], _args[8], _args[9], _args[10], _args[11], _args[12], _args[13], _args[14], _args[15]); break; \
      default: abort();                                                                                    \
      }                                                                                                    \
    }                                                                                                      \
    GASNETI_TRACE_PRINTF(A,("AM%s_SHORT_HANDLER: handler execution complete", (isReq?"REQUEST":"REPLY"))); \
  } while (0)
/* ------------------------------------------------------------------------------------ */
#define _GASNETI_RUN_HANDLER_MEDLONG(phandlerfn, token, pArgs, numargs, pData, datalen) do { \
  gasneti_assert(phandlerfn);                                                                \
  if (numargs == 0) (*phandlerfn)(token, pData, datalen);                                    \
  else {                                                                                     \
    gasnet_handlerarg_t *_args = (gasnet_handlerarg_t *)(pArgs); /* eval only once */        \
    switch (numargs) {                                                                       \
      case 1:  (*phandlerfn)(token, pData, datalen, _args[0]); break;                        \
      case 2:  (*phandlerfn)(token, pData, datalen, _args[0], _args[1]); break;              \
      case 3:  (*phandlerfn)(token, pData, datalen, _args[0], _args[1], _args[2]); break;    \
      case 4:  (*phandlerfn)(token, pData, datalen, _args[0], _args[1], _args[2], _args[3]); break; \
      case 5:  (*phandlerfn)(token, pData, datalen, _args[0], _args[1], _args[2], _args[3], _args[4]); break; \
      case 6:  (*phandlerfn)(token, pData, datalen, _args[0], _args[1], _args[2], _args[3], _args[4], _args[5]); break; \
      case 7:  (*phandlerfn)(token, pData, datalen, _args[0], _args[1], _args[2], _args[3], _args[4], _args[5], _args[6]); break; \
      case 8:  (*phandlerfn)(token, pData, datalen, _args[0], _args[1], _args[2], _args[3], _args[4], _args[5], _args[6], _args[7]); break; \
      case 9:  (*phandlerfn)(token, pData, datalen, _args[0], _args[1], _args[2], _args[3], _args[4], _args[5], _args[6], _args[7], _args[8]); break; \
      case 10: (*phandlerfn)(token, pData, datalen, _args[0], _args[1], _args[2], _args[3], _args[4], _args[5], _args[6], _args[7], _args[8], _args[9]); break; \
      case 11: (*phandlerfn)(token, pData, datalen, _args[0], _args[1], _args[2], _args[3], _args[4], _args[5], _args[6], _args[7], _args[8], _args[9], _args[10]); break; \
      case 12: (*phandlerfn)(token, pData, datalen, _args[0], _args[1], _args[2], _args[3], _args[4], _args[5], _args[6], _args[7], _args[8], _args[9], _args[10], _args[11]); break; \
      case 13: (*phandlerfn)(token, pData, datalen, _args[0], _args[1], _args[2], _args[3], _args[4], _args[5], _args[6], _args[7], _args[8], _args[9], _args[10], _args[11], _args[12]); break; \
      case 14: (*phandlerfn)(token, pData, datalen, _args[0], _args[1], _args[2], _args[3], _args[4], _args[5], _args[6], _args[7], _args[8], _args[9], _args[10], _args[11], _args[12], _args[13]); break; \
      case 15: (*phandlerfn)(token, pData, datalen, _args[0], _args[1], _args[2], _args[3], _args[4], _args[5], _args[6], _args[7], _args[8], _args[9], _args[10], _args[11], _args[12], _args[13], _args[14]); break; \
      case 16: (*phandlerfn)(token, pData, datalen, _args[0], _args[1], _args[2], _args[3], _args[4], _args[5], _args[6], _args[7], _args[8], _args[9], _args[10], _args[11], _args[12], _args[13], _args[14], _args[15]); break; \
      default: abort();                                                                 \
      }                                                                                 \
    }                                                                                   \
  } while (0)

/* be default, we guarantee double-word alignment for data payload of medium xfers 
 */
#ifndef GASNETI_MEDBUF_ALIGNMENT
#define GASNETI_MEDBUF_ALIGNMENT 8
#endif

#define GASNETI_RUN_HANDLER_MEDIUM(isReq, hid, phandlerfn, token, pArgs, numargs, pData, datalen) do {      \
    gasneti_assert(((uintptr_t)pData) % GASNETI_MEDBUF_ALIGNMENT == 0);                                     \
    if (isReq) GASNETI_TRACE_AMMEDIUM_REQHANDLER(hid, token, pData, datalen, numargs, pArgs);               \
    else       GASNETI_TRACE_AMMEDIUM_REPHANDLER(hid, token, pData, datalen, numargs, pArgs);               \
    _GASNETI_RUN_HANDLER_MEDLONG((gasneti_HandlerMedium)phandlerfn, (gasnet_token_t)token,                  \
                                 pArgs, numargs, (void *)pData, (int)datalen);                              \
    GASNETI_TRACE_PRINTF(A,("AM%s_MEDIUM_HANDLER: handler execution complete", (isReq?"REQUEST":"REPLY"))); \
  } while (0)
#define GASNETI_RUN_HANDLER_LONG(isReq, hid, phandlerfn, token, pArgs, numargs, pData, datalen) do {      \
    if (isReq) GASNETI_TRACE_AMLONG_REQHANDLER(hid, token, pData, datalen, numargs, pArgs);               \
    else       GASNETI_TRACE_AMLONG_REPHANDLER(hid, token, pData, datalen, numargs, pArgs);               \
    _GASNETI_RUN_HANDLER_MEDLONG((gasneti_HandlerLong)phandlerfn, (gasnet_token_t)token,                  \
                                 pArgs, numargs, (void *)pData, (int)datalen);                            \
    GASNETI_TRACE_PRINTF(A,("AM%s_LONG_HANDLER: handler execution complete", (isReq?"REQUEST":"REPLY"))); \
  } while (0)
/* ------------------------------------------------------------------------------------ */

END_EXTERNC

#undef _IN_GASNET_INTERNAL_H
#endif

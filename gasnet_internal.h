/*  $Archive:: /Ti/GASNet/gasnet_internal.h                               $
 *     $Date: 2004/01/13 21:49:10 $
 * $Revision: 1.49 $
 * Description: GASNet header for internal definitions used in GASNet implementation
 * Copyright 2002, Dan Bonachea <bonachea@cs.berkeley.edu>
 * Terms of use are as specified in license.txt
 */

#ifndef _GASNET_INTERNAL_H
#define _GASNET_INTERNAL_H

#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <unistd.h>
#include <string.h>

#include <gasnet.h>

BEGIN_EXTERNC

#ifdef __SUNPRO_C
  #pragma error_messages(off, E_END_OF_LOOP_CODE_NOT_REACHED)
#endif

extern int gasneti_init_done; /*  true after init */
extern int gasneti_attach_done; /*  true after attach */


/*  safe memory allocation/deallocation */
#if GASNET_DEBUG
  extern void *_gasneti_malloc(size_t nbytes, int allowfail, const char *curloc) __attribute__((__malloc__));
  extern void _gasneti_free(void *ptr, const char *curloc);
  extern void *_gasneti_calloc(size_t N, size_t S, const char *curloc) __attribute__((__malloc__));
  extern size_t _gasneti_memcheck(void *ptr, const char *curloc, int isfree);
  #define gasneti_malloc(nbytes) _gasneti_malloc(nbytes, 0, __FILE__ ":" _STRINGIFY(__LINE__))
  #define gasneti_malloc_allowfail(nbytes) _gasneti_malloc(nbytes, 1, __FILE__ ":" _STRINGIFY(__LINE__))
  #define gasneti_calloc(N,S)    _gasneti_calloc(N,S, __FILE__ ":" _STRINGIFY(__LINE__))
  #define gasneti_free(ptr)	 _gasneti_free(ptr, __FILE__ ":" _STRINGIFY(__LINE__))
  #define gasneti_memcheck(ptr)  (gasneti_assert(ptr != NULL), \
         (void)_gasneti_memcheck(ptr, __FILE__ ":" _STRINGIFY(__LINE__), 0))
#else
  #ifdef __GNUC__
    /* provide gcc with additional information about the aliasing qualities
       of the return value (being malloc-like) to improve caller optimization */
    GASNET_INLINE_MODIFIER(gasneti_malloc)
    void *gasneti_malloc(size_t nbytes) __attribute__((__malloc__));
    GASNET_INLINE_MODIFIER(gasneti_malloc_allowfail)
    void *gasneti_malloc_allowfail(size_t nbytes) __attribute__((__malloc__));
    GASNET_INLINE_MODIFIER(gasneti_calloc)
    void *gasneti_calloc(size_t N, size_t S) __attribute__((__malloc__));
  #endif
  GASNET_INLINE_MODIFIER(gasneti_malloc)
  void *gasneti_malloc(size_t nbytes) {
    void *ret = NULL;
    if_pt (gasneti_attach_done) gasnet_hold_interrupts();
    ret = malloc(nbytes);
    if_pf (ret == NULL) 
      gasneti_fatalerror("gasneti_malloc(%d) failed", (int)nbytes);
    if_pt (gasneti_attach_done) gasnet_resume_interrupts();
    return ret;
  }
  GASNET_INLINE_MODIFIER(gasneti_malloc_allowfail)
  void *gasneti_malloc_allowfail(size_t nbytes) {
    void *ret = NULL;
    if_pt (gasneti_attach_done) gasnet_hold_interrupts();
    ret = malloc(nbytes);
    if_pf (ret == NULL) /* allow a NULL return for out-of-memory */
      GASNETI_TRACE_PRINTF(I,("Warning: returning NULL for a failed gasneti_malloc(%i)",(int)nbytes));
    if_pt (gasneti_attach_done) gasnet_resume_interrupts();
    return ret;
  }
  GASNET_INLINE_MODIFIER(gasneti_calloc)
  void *gasneti_calloc(size_t N, size_t S) {
    void *ret = NULL;
    if_pt (gasneti_attach_done) gasnet_hold_interrupts();
    ret = calloc(N,S);
    if_pf (ret == NULL) 
      gasneti_fatalerror("gasneti_calloc(%d,%d) failed", (int)N, (int)S);
    if_pt (gasneti_attach_done) gasnet_resume_interrupts();
    return ret;
  }
  GASNET_INLINE_MODIFIER(gasneti_free)
  void gasneti_free(void *ptr) {
    if_pf (ptr == NULL) return;
    if_pt (gasneti_attach_done) gasnet_hold_interrupts();
    free(ptr);
    if_pt (gasneti_attach_done) gasnet_resume_interrupts();
  }
  #define gasneti_memcheck(ptr)   ((void)0)
#endif
/* Beware - in debug mode, 
   gasneti_malloc/gasneti_calloc/gasneti_free are NOT
   compatible with malloc/calloc/free
   (freeing memory allocated from one using the other is likely to crash)
 */
#define malloc !!! ERROR: GASNet conduit code must use gasneti_malloc !!!
#define calloc !!! ERROR: GASNet conduit code must use gasneti_calloc !!!
#define free   !!! ERROR: GASNet conduit code must use gasneti_free   !!!

#include <assert.h>
#undef assert
#define assert(x)   !!! ERROR: GASNet conduit code should use gasneti_assert()   !!!

/* ------------------------------------------------------------------------------------ */
/* Version of strdup() which is compatible w/ gasneti_free(), instead of plain free() */
#ifdef __GNUC__ 
  GASNET_INLINE_MODIFIER(gasneti_strdup)
  char *gasneti_strdup(const char *s) __attribute__((__malloc__));
  GASNET_INLINE_MODIFIER(gasneti_strndup)
  char *gasneti_strndup(const char *s, size_t n) __attribute__((__malloc__));
#endif
GASNET_INLINE_MODIFIER(gasneti_strdup)
char *gasneti_strdup(const char *s) {
  char *retval;

  if_pf (s == NULL) {
    /* special case to avoid strlen(NULL) */
    retval = (char *)gasneti_malloc(1);
    retval[0] = '\0';
  } else {
    size_t sz = strlen(s) + 1;
    retval = (char *)memcpy((char *)gasneti_malloc(sz), s, sz);
  }

  return retval;
}
/* Like gasneti_strdup, but copy is limited to at most n characters.
 * Note allocation is upto n+1 bytes, due to the '\0' termination.
 */
GASNET_INLINE_MODIFIER(gasneti_strndup)
char *gasneti_strndup(const char *s, size_t n) {
  char *retval;

  if_pf ((s == NULL) || (n == 0)) {
    /* special case to avoid strlen(NULL) */
    retval = (char *)gasneti_malloc(1);
    retval[0] = '\0';
  } else {
    size_t len = strlen(s);

    if (len > n) { len = n; }
    retval = gasneti_malloc(len + 1);
    retval[len] = '\0';  /* memcpy won't overwrite this byte */

    (void)memcpy(retval, s, len);
  }

  return retval;
}

/* ------------------------------------------------------------------------------------ */
/* page alignment macros */
#define GASNETI_ALIGNDOWN(p,P)    ((uintptr_t)(p)&~((uintptr_t)(P)-1))
#define GASNETI_ALIGNUP(p,P)     (GASNETI_ALIGNDOWN((uintptr_t)(p)+((P)-1),P))

#define GASNETI_PAGE_ALIGNDOWN(p) (GASNETI_ALIGNDOWN(p,GASNET_PAGESIZE))
#define GASNETI_PAGE_ALIGNUP(p)   (GASNETI_ALIGNUP(p,GASNET_PAGESIZE))
/* ------------------------------------------------------------------------------------ */

extern void gasneti_freezeForDebugger();
extern void gasneti_killmyprocess(int exitcode) GASNET_NORETURN;

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
  extern void gasneti_mmap_fixed(void *segbase, size_t segsize);
  extern void *gasneti_mmap(size_t segsize);
  extern void gasneti_munmap(void *segbase, size_t segsize);
#endif

#ifndef GASNETI_MAX_MALLOCSEGMENT_SZ
#define GASNETI_MAX_MALLOCSEGMENT_SZ (100*1048576) /* Max segment sz to use when mmap not avail */
#endif
#ifndef GASNETI_USE_HIGHSEGMENT
#define GASNETI_USE_HIGHSEGMENT 1  /* use the high end of mmap segments */
#endif

typedef void (*gasneti_bootstrapExchangefn_t)(void *src, size_t len, void *dest);
typedef void (*gasneti_bootstrapBroadcastfn_t)(void *src, size_t len, void *dest, int rootnode);

void gasneti_segmentInit(uintptr_t *MaxLocalSegmentSize, 
                         uintptr_t *MaxGlobalSegmentSize,
                         uintptr_t localSegmentLimit,
                         gasnet_node_t numnodes,
                         gasneti_bootstrapExchangefn_t exchangefn);
void gasneti_segmentAttach(uintptr_t segsize, uintptr_t minheapoffset,
                           gasnet_seginfo_t *seginfo,
                           gasneti_bootstrapExchangefn_t exchangefn);
void gasneti_setupGlobalEnvironment(gasnet_node_t numnodes, gasnet_node_t mynode,
                                     gasneti_bootstrapExchangefn_t exchangefn,
                                     gasneti_bootstrapBroadcastfn_t broadcastfn);

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

/* ------------------------------------------------------------------------------------ */

END_EXTERNC

#endif

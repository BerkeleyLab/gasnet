/*   $Source: /Users/kamil/work/gasnet-cvs2/gasnet/gasnet_toolhelp.h,v $
 *     $Date: 2006/05/23 12:42:14 $
 * $Revision: 1.5 $
 * Description: misc declarations needed by both gasnet_tools and libgasnet
 * Copyright 2002, Dan Bonachea <bonachea@cs.berkeley.edu>
 * Terms of use are as specified in license.txt
 */

#if !defined(_IN_GASNET_H) && !defined(_IN_GASNET_TOOLS_H)
  #error This file is not meant to be included directly- clients should include gasnet.h or gasnet_tools.h
#endif

#ifndef _GASNET_TOOLHELP_H
#define _GASNET_TOOLHELP_H

GASNETI_BEGIN_EXTERNC

#if PLATFORM_OS_MTA
   #include <machine/runtime.h>
   #define _gasneti_sched_yield() mta_yield()
#elif defined(HAVE_SCHED_YIELD) && !PLATFORM_OS_BLRTS && !PLATFORM_OS_CATAMOUNT
   #include <sched.h>
   #define _gasneti_sched_yield() sched_yield()
#else
   #include <unistd.h>
   #define _gasneti_sched_yield() (sleep(0),0)
#endif
#define gasneti_sched_yield() _gasneti_sched_yield()

#if defined(__GNUC__) || defined(__FUNCTION__)
  #define GASNETI_CURRENT_FUNCTION __FUNCTION__
#elif defined(HAVE_FUNC) && !defined(__cplusplus)
  /* __func__ should also work for ISO C99 compilers */
  #define GASNETI_CURRENT_FUNCTION __func__
#else
  #define GASNETI_CURRENT_FUNCTION ""
#endif
extern char *gasneti_build_loc_str(const char *funcname, const char *filename, int linenum);
#define gasneti_current_loc gasneti_build_loc_str(GASNETI_CURRENT_FUNCTION,__FILE__,__LINE__)

/* gasneti_assert_always():
 * an assertion that never compiles away - for sanity checks in non-critical paths 
 */
#define gasneti_assert_always(expr) \
    (PREDICT_TRUE(expr) ? (void)0 : gasneti_fatalerror("Assertion failure at %s: %s", gasneti_current_loc, #expr))

/* gasneti_assert():
 * an assertion that compiles away in non-debug mode - for sanity checks in critical paths 
 */
#if GASNET_NDEBUG
  #define gasneti_assert(expr) ((void)0)
#else
  #define gasneti_assert(expr) gasneti_assert_always(expr)
#endif

/* gasneti_assert_zeroret(), gasneti_assert_nzeroret():
 * evaluate an expression (always), and in debug mode additionally 
 * assert that it returns zero or non-zero
 * useful for making system calls and checking the result
 */
#if GASNET_DEBUG
  #define gasneti_assert_zeroret(op) do {                   \
    int _retval = (op);                                     \
    if_pf(_retval)                                          \
      gasneti_fatalerror(#op": %s(%i), errno=%s(%i) at %s", \
        strerror(_retval), _retval, strerror(errno), errno, \
        gasneti_current_loc);                               \
  } while (0)
  #define gasneti_assert_nzeroret(op) do {                  \
    int _retval = (op);                                     \
    if_pf(!_retval)                                         \
      gasneti_fatalerror(#op": %s(%i), errno=%s(%i) at %s", \
        strerror(_retval), _retval, strerror(errno), errno, \
        gasneti_current_loc);                               \
  } while (0)
#else
  #define gasneti_assert_zeroret(op)  op
  #define gasneti_assert_nzeroret(op) op
#endif

/* return physical memory of machine
   on failure, failureIsFatal nonzero => fatal error, failureIsFatal zero => return 0 */
extern uint64_t gasneti_getPhysMemSz(int failureIsFatal); 

GASNETI_FORMAT_PRINTF(gasneti_fatalerror,1,2,
extern void gasneti_fatalerror(const char *msg, ...) GASNETI_NORETURN);
GASNETI_NORETURNP(gasneti_fatalerror)

extern void gasneti_killmyprocess(int exitcode) GASNETI_NORETURN;
GASNETI_NORETURNP(gasneti_killmyprocess)

extern void gasneti_flush_streams(); /* flush all open streams */
extern void gasneti_close_streams(); /* close standard streams (for shutdown) */

extern int gasneti_cpu_count();

extern void gasneti_set_affinity(int rank);

extern int gasneti_isLittleEndian();

typedef void (*gasneti_sighandlerfn_t)(int);
gasneti_sighandlerfn_t gasneti_reghandler(int sigtocatch, gasneti_sighandlerfn_t fp);

/* return a fast but simple/insecure 64-bit checksum of arbitrary data */
extern uint64_t gasneti_checksum(void *p, int numbytes);

/* format a integer value as a human-friendly string, with appropriate mem suffix */
extern char *gasneti_format_number(int64_t val, char *buf, size_t bufsz, int is_mem_size);
/* parse an integer value back out again
  if mem_size_multiplier==0, it's a unitless quantity
  otherwise, it's a memory size quantity, and mem_size_multiplier provides the 
    default memory unit (ie 1024=1KB) if the string provides none  */
extern int64_t gasneti_parse_int(const char *str, uint64_t mem_size_multiplier);

/* set/unset an environment variable, for the local process ONLY */
extern void gasneti_setenv(const char *key, const char *value);
extern void gasneti_unsetenv(const char *key);

#if PLATFORM_OS_AIX
  /* AIX's stdio.h won't provide prototypes for snprintf() and vsnprintf()
   * by default since they are in C99 but not C89.
   */
  GASNETI_FORMAT_PRINTF(snprintf,3,4,
  extern int snprintf(char * s, size_t n, const char * format, ...));
  GASNETI_FORMAT_PRINTF(vsnprintf,3,0,
  extern int vsnprintf(char * s, size_t n, const char * format, va_list ap));
#endif

GASNETI_END_EXTERNC

#endif

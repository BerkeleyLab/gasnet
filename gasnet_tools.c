/*   $Source: /Users/kamil/work/gasnet-cvs2/gasnet/gasnet_tools.c,v $
 *     $Date: 2006/04/04 21:55:03 $
 * $Revision: 1.149.2.8 $
 * Description: GASNet implementation of internal helpers
 * Copyright 2002, Dan Bonachea <bonachea@cs.berkeley.edu>
 * Terms of use are as specified in license.txt
 */

#include <gasnet_internal.h>
#include <gasnet_tools.h>

#include <unistd.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <ctype.h>
#include <string.h>
#include <errno.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <fcntl.h>

#include <signal.h>
#if HAVE_MALLOC_H
  #include <malloc.h>
#endif
#if HAVE_EXECINFO_H
  #include <execinfo.h>
#endif

#ifdef IRIX
#define signal(a,b) bsd_signal(a,b)
#endif

#ifndef STDIN_FILENO
  #define STDIN_FILENO 0
#endif
#ifndef STDOUT_FILENO
  #define STDOUT_FILENO 1
#endif
#ifndef STDERR_FILENO
  #define STDERR_FILENO 2
#endif

/* set to non-zero for verbose error reporting */
int gasneti_VerboseErrors = 1;

/* atomics support */
#ifdef GASNETI_USE_GENERIC_ATOMICOPS
  gasnet_hsl_t gasneti_atomicop_lock = GASNET_HSL_INITIALIZER;
  void *gasneti_patomicop_lock = (void*)&gasneti_atomicop_lock;
  GASNETI_GENERIC_DEC_AND_TEST_DEF
  #ifdef GASNETI_GENERIC_CAS_DEF
    GASNETI_GENERIC_CAS_DEF
  #endif
  #ifdef GASNETI_GENERIC_ADD_SUB_DEF
    GASNETI_GENERIC_ADD_SUB_DEF
  #endif
#endif

/* ------------------------------------------------------------------------------------ */
/* call-based atomic support for C compilers with limited inline assembly */

#if defined(GASNETI_ATOMIC_SET_BODY)
  GASNETI_NEVER_INLINE(_gasneti_special_atomic_set)
  extern void _gasneti_special_atomic_set() {
    GASNETI_ATOMIC_SET_BODY
  }
#endif
#if defined(GASNETI_ATOMIC_READ_BODY)
  GASNETI_NEVER_INLINE(_gasneti_special_atomic_read)
  extern void _gasneti_special_atomic_read() {
    GASNETI_ATOMIC_READ_BODY
  }
#endif
#if defined(GASNETI_ATOMIC_INCREMENT_BODY)
  GASNETI_NEVER_INLINE(_gasneti_special_atomic_increment)
  extern void _gasneti_special_atomic_increment() {
    GASNETI_ATOMIC_INCREMENT_BODY
  }
#endif
#if defined(GASNETI_ATOMIC_DECREMENT_BODY)
  GASNETI_NEVER_INLINE(_gasneti_special_atomic_decrement)
  extern void _gasneti_special_atomic_decrement() {
    GASNETI_ATOMIC_DECREMENT_BODY
  }
#endif
#if defined(GASNETI_ATOMIC_DECREMENT_AND_TEST_BODY)
  GASNETI_NEVER_INLINE(_gasneti_special_atomic_decrement_and_test)
  extern void _gasneti_special_atomic_decrement_and_test() {
    GASNETI_ATOMIC_DECREMENT_AND_TEST_BODY
  }
#endif
#if defined(GASNETI_ATOMIC_COMPARE_AND_SWAP_BODY)
  GASNETI_NEVER_INLINE(_gasneti_special_atomic_compare_and_swap)
  extern void _gasneti_special_atomic_compare_and_swap() {
    GASNETI_ATOMIC_COMPARE_AND_SWAP_BODY
  }
#endif
#if defined(GASNETI_ATOMIC_ADD_BODY)
  GASNETI_NEVER_INLINE(_gasneti_special_atomic_add)
  extern void _gasneti_special_atomic_add() {
    GASNETI_ATOMIC_ADD_BODY
  }
#endif
#if defined(GASNETI_ATOMIC_SUBTRACT_BODY)
  GASNETI_NEVER_INLINE(_gasneti_special_atomic_subtract)
  extern void _gasneti_special_atomic_subtract() {
    GASNETI_ATOMIC_SUBTRACT_BODY
  }
#endif
#if defined(GASNETI_ATOMIC_FETCHADD_BODY)
  GASNETI_NEVER_INLINE(_gasneti_special_atomic_fetchadd)
  extern void _gasneti_special_atomic_fetchadd() {
    GASNETI_ATOMIC_FETCHADD_BODY
  }
#endif
#if defined(GASNETI_ATOMIC_ADDFETCH_BODY)
  GASNETI_NEVER_INLINE(_gasneti_special_atomic_addfetch)
  extern void _gasneti_special_atomic_addfetch() {
    GASNETI_ATOMIC_ADDFETCH_BODY
  }
#endif

/* ------------------------------------------------------------------------------------ */
/* call-based membar/atomic support for C++ compilers which lack inline assembly */
#if defined(GASNETI_USING_SLOW_ATOMICS) || \
    defined(GASNETI_USING_SLOW_MEMBARS) || \
    defined(GASNETI_USING_SLOW_TIMERS)
#error gasnet_internal.c must be compiled with support for inline assembly
#endif

extern void gasneti_slow_compiler_fence() {
  gasneti_compiler_fence();
}
extern void gasneti_slow_local_wmb() {
  gasneti_local_wmb();
}
extern void gasneti_slow_local_rmb() {
  gasneti_local_rmb();
}
extern void gasneti_slow_local_mb() {
  gasneti_local_mb();
}

#if defined(GASNETI_STATTIME_NOW_BODY)
  GASNETI_NEVER_INLINE(gasneti_slow_stattime_now)
  extern void gasneti_slow_stattime_now() {
    GASNETI_STATTIME_NOW_BODY
  }
#else
  extern gasneti_stattime_t gasneti_slow_stattime_now() {
    return GASNETI_STATTIME_NOW();
  }
#endif

extern uint32_t gasneti_slow_atomic_read(gasneti_atomic_t *p, const int flags) {
  return gasneti_atomic_read(p,flags);
}

extern void gasneti_slow_atomic_set(gasneti_atomic_t *p, uint32_t v, const int flags) {
  gasneti_atomic_set(p, v, flags);
}

extern void gasneti_slow_atomic_increment(gasneti_atomic_t *p, const int flags) {
  gasneti_atomic_increment(p, flags);
}

extern void gasneti_slow_atomic_decrement(gasneti_atomic_t *p, const int flags) {
  gasneti_atomic_decrement(p, flags);
}

extern int gasneti_slow_atomic_decrement_and_test(gasneti_atomic_t *p, const int flags) {
  return gasneti_atomic_decrement_and_test(p, flags);
}

#if defined(GASNETI_HAVE_ATOMIC_CAS)
  extern int gasneti_slow_atomic_compare_and_swap(gasneti_atomic_t *p, uint32_t oldval, uint32_t newval, const int flags) {
    return gasneti_atomic_compare_and_swap(p,oldval,newval,flags);
  }
#endif

#if defined(GASNETI_HAVE_ATOMIC_ADD_SUB)
  extern uint32_t gasneti_slow_atomic_add(gasneti_atomic_t *p, uint32_t op, const int flags) {
    return gasneti_atomic_add(p,op,flags);
  }
  extern uint32_t gasneti_slow_atomic_subtract(gasneti_atomic_t *p, uint32_t op, const int flags) {
    return gasneti_atomic_subtract(p,op,flags);
  }
#endif

/* ------------------------------------------------------------------------------------ */

#if GASNETI_THROTTLE_POLLERS
  gasneti_atomic_t gasneti_throttle_haveusefulwork = gasneti_atomic_init(0);
  gasneti_mutex_t gasneti_throttle_spinpoller = GASNETI_MUTEX_INITIALIZER;
#endif
#if GASNET_DEBUG && GASNETI_THREADS
  gasneti_threadkey_t gasneti_throttledebug_key = GASNETI_THREADKEY_INITIALIZER;
#elif GASNET_DEBUG
  int gasneti_throttledebug_cnt = 0;
#endif

#define GASNET_VERSION_STR  _STRINGIFY(GASNET_VERSION)
GASNETI_IDENT(gasneti_IdentString_APIVersion, "$GASNetAPIVersion: " GASNET_VERSION_STR " $");

#define GASNETI_THREADMODEL_STR _STRINGIFY(GASNETI_THREADMODEL)
GASNETI_IDENT(gasneti_IdentString_ThreadModel, "$GASNetThreadModel: GASNET_" GASNETI_THREADMODEL_STR " $");

#define GASNETI_SEGMENT_CONFIG_STR _STRINGIFY(GASNETI_SEGMENT_CONFIG)
GASNETI_IDENT(gasneti_IdentString_SegConfig, "$GASNetSegment: GASNET_SEGMENT_" GASNETI_SEGMENT_CONFIG_STR " $");

/* embed a string with complete configuration info to support versioning checks */
GASNETI_IDENT(gasneti_IdentString_libraryConfig, "$GASNetConfig: (libgasnet.a) " GASNET_CONFIG_STRING " $");

GASNETI_IDENT(gasneti_IdentString_BuildTimestamp, 
             "$GASNetBuildTimestamp: " __DATE__ " " __TIME__ " $");

GASNETI_IDENT(gasneti_IdentString_BuildID, 
             "$GASNetBuildId: " GASNETI_BUILD_ID " $");
GASNETI_IDENT(gasneti_IdentString_ConfigureArgs, 
             "$GASNetConfigureArgs: " GASNETI_CONFIGURE_ARGS " $");
GASNETI_IDENT(gasneti_IdentString_SystemTuple, 
             "$GASNetSystemTuple: " GASNETI_SYSTEM_TUPLE " $");
GASNETI_IDENT(gasneti_IdentString_SystemName, 
             "$GASNetSystemName: " GASNETI_SYSTEM_NAME " $");

int gasneti_init_done = 0; /*  true after init */
int gasneti_attach_done = 0; /*  true after attach */
extern void gasneti_checkinit() {
  if (!gasneti_init_done)
    gasneti_fatalerror("Illegal call to GASNet before gasnet_init() initialization");
}
extern void gasneti_checkattach() {
   gasneti_checkinit();
   if (!gasneti_attach_done)
    gasneti_fatalerror("Illegal call to GASNet before gasnet_attach() initialization");
}

int gasneti_wait_mode = GASNET_WAIT_SPIN;

double *_gasneti_stattime_metric = NULL;

int GASNETI_LINKCONFIG_IDIOTCHECK(GASNETI_THREADMODEL) = 1;
int GASNETI_LINKCONFIG_IDIOTCHECK(GASNETI_SEGMENT_CONFIG) = 1;
int GASNETI_LINKCONFIG_IDIOTCHECK(GASNETI_DEBUG_CONFIG) = 1;
int GASNETI_LINKCONFIG_IDIOTCHECK(GASNETI_TRACE_CONFIG) = 1;
int GASNETI_LINKCONFIG_IDIOTCHECK(GASNETI_STATS_CONFIG) = 1;
int GASNETI_LINKCONFIG_IDIOTCHECK(GASNETI_SRCLINES_CONFIG) = 1;
int GASNETI_LINKCONFIG_IDIOTCHECK(GASNETI_ALIGN_CONFIG) = 1;
int GASNETI_LINKCONFIG_IDIOTCHECK(GASNETI_PTR_CONFIG) = 1;
int GASNETI_LINKCONFIG_IDIOTCHECK(GASNETI_TIMER_CONFIG) = 1;
int GASNETI_LINKCONFIG_IDIOTCHECK(GASNETI_ATOMIC_CONFIG) = 1;
int GASNETI_LINKCONFIG_IDIOTCHECK(_CONCAT(CORE_,GASNET_CORE_NAME)) = 1;
int GASNETI_LINKCONFIG_IDIOTCHECK(_CONCAT(EXTENDED_,GASNET_EXTENDED_NAME)) = 1;

/* Default global definitions of GASNet-wide internal variables
   if conduits override one of these, they must
   still provide variable or macro definitions for these tokens */
#ifdef _GASNET_MYNODE_DEFAULT
  gasnet_node_t gasneti_mynode = (gasnet_node_t)-1;
#endif
#ifdef _GASNET_NODES_DEFAULT
  gasnet_node_t gasneti_nodes = 0;
#endif

#if defined(_GASNET_GETMAXSEGMENTSIZE_DEFAULT) && !GASNET_SEGMENT_EVERYTHING
  uintptr_t gasneti_MaxLocalSegmentSize = 0;
  uintptr_t gasneti_MaxGlobalSegmentSize = 0;
#endif

#ifdef _GASNETI_PROGRESSFNS_DEFAULT
  GASNETI_PROGRESSFNS_LIST(_GASNETI_PROGRESSFNS_DEFINE_FLAGS)
#endif

#if GASNET_DEBUG
  void (*gasneti_debug_progressfn_bool)() = abort;
  void (*gasneti_debug_progressfn_counted)() = abort;
#endif

#ifdef _GASNETI_SEGINFO_DEFAULT
  gasnet_seginfo_t *gasneti_seginfo = NULL;
  gasnet_seginfo_t *gasneti_seginfo_client = NULL;
  void **gasneti_seginfo_ub = NULL; /* cached result of gasneti_seginfo[i].addr + gasneti_seginfo[i].size */
  void **gasneti_seginfo_client_ub = NULL;
#endif

static int gasneti_isLittleEndian() {
  union {
    int i;                  /* machine word */
    unsigned char b[sizeof(int)];    /* b[0] overlaid with first byte of i */
  } x;
  x.i = 0xFF;    /* set lsb, zero all others */
  return x.b[0] == 0xFF;
}

/* ------------------------------------------------------------------------------------ */
/* conduit-independent sanity checks */
extern void gasneti_check_config_preinit() {
  gasneti_assert_always(sizeof(int8_t) == 1);
  gasneti_assert_always(sizeof(uint8_t) == 1);
  gasneti_assert_always(sizeof(gasnete_anytype8_t) == 1);
  #ifndef INTTYPES_16BIT_MISSING
    gasneti_assert_always(sizeof(int16_t) == 2);
    gasneti_assert_always(sizeof(uint16_t) == 2);
    gasneti_assert_always(sizeof(gasnete_anytype16_t) == 2);
  #endif
  gasneti_assert_always(sizeof(int32_t) == 4);
  gasneti_assert_always(sizeof(uint32_t) == 4);
  gasneti_assert_always(sizeof(gasnete_anytype32_t) == 4);
  gasneti_assert_always(sizeof(int64_t) == 8);
  gasneti_assert_always(sizeof(uint64_t) == 8);
  gasneti_assert_always(sizeof(gasnete_anytype64_t) == 8);

  gasneti_assert_always(sizeof(uintptr_t) >= sizeof(void *));

  #if WORDS_BIGENDIAN
    gasneti_assert_always(!gasneti_isLittleEndian());
  #else
    gasneti_assert_always(gasneti_isLittleEndian());
  #endif

  /* check GASNET_PAGESIZE is a power of 2 and > 0 */
  gasneti_assert_always(GASNET_PAGESIZE > 0);
  gasneti_assert_always(GASNETI_POWEROFTWO(GASNET_PAGESIZE));

  gasneti_assert_always(SIZEOF_GASNET_REGISTER_VALUE_T == sizeof(gasnet_register_value_t));
  gasneti_assert_always(SIZEOF_GASNET_REGISTER_VALUE_T >= sizeof(int));
  gasneti_assert_always(SIZEOF_GASNET_REGISTER_VALUE_T >= sizeof(void *));

  #if    defined(GASNETI_PTR32) && !defined(GASNETI_PTR64)
    gasneti_assert_always(sizeof(void*) == 4);
  #elif !defined(GASNETI_PTR32) &&  defined(GASNETI_PTR64)
    gasneti_assert_always(sizeof(void*) == 8);
  #else
    #error must #define exactly one of GASNETI_PTR32 or GASNETI_PTR64
  #endif

  #if defined(GASNETI_UNI_BUILD)
    if (gasneti_cpu_count() > 1) 
      gasneti_fatalerror("GASNet was built in uniprocessor (non-SMP-safe) configuration, "
        "but executed on an SMP. Please re-run GASNet configure with --enable-smp-safe and rebuild");
  #endif

  { static int firstcall = 1;
    if (firstcall) { /* miscellaneous conduit-independent initializations */
      firstcall = 0;
      #if GASNET_DEBUG && GASNETI_THREADS
        gasneti_threadkey_init(&gasneti_throttledebug_key);
      #endif
      gasneti_memcheck_all();
    }
  }
}

extern void gasneti_check_config_postattach() {
  gasneti_check_config_preinit();

  /*  verify sanity of the core interface */
  gasneti_assert_always(gasnet_AMMaxArgs() >= 2*MAX(sizeof(int),sizeof(void*)));      
  gasneti_assert_always(gasnet_AMMaxMedium() >= 512);
  gasneti_assert_always(gasnet_AMMaxLongRequest() >= 512);
  gasneti_assert_always(gasnet_AMMaxLongReply() >= 512);  

  gasneti_assert_always(gasnet_nodes() >= 1);
  gasneti_assert_always(gasnet_mynode() < gasnet_nodes());
  { static int firstcall = 1;
    if (firstcall) { /* miscellaneous conduit-independent initializations */
      firstcall = 0;

      if (gasneti_getenv_yesno_withdefault("GASNET_DISABLE_MUNMAP",0)) {
        #if HAVE_PTMALLOC                                        
          mallopt(M_TRIM_THRESHOLD, -1);
          mallopt(M_MMAP_MAX, 0);
          GASNETI_TRACE_PRINTF(I,("Setting mallopt M_TRIM_THRESHOLD=-1 and M_MMAP_MAX=0"));
        #else
          GASNETI_TRACE_PRINTF(I,("WARNING: GASNET_DISABLE_MUNMAP set on an unsupported platform"));
          if (gasneti_verboseenv()) 
            fprintf(stderr, "WARNING: GASNET_DISABLE_MUNMAP set on an unsupported platform\n");
        #endif
      }
    }
  }
  gasneti_memcheck_all();
}

/* ------------------------------------------------------------------------------------ */
#ifndef _GASNET_ERRORNAME
extern const char *gasnet_ErrorName(int errval) {
  switch (errval) {
    case GASNET_OK:           return "GASNET_OK";      
    case GASNET_ERR_NOT_INIT: return "GASNET_ERR_NOT_INIT";      
    case GASNET_ERR_BAD_ARG:  return "GASNET_ERR_BAD_ARG";       
    case GASNET_ERR_RESOURCE: return "GASNET_ERR_RESOURCE";      
    case GASNET_ERR_BARRIER_MISMATCH: return "GASNET_ERR_BARRIER_MISMATCH";      
    case GASNET_ERR_NOT_READY: return "GASNET_ERR_NOT_READY";      
    default: return "*unknown*";
  }
}
#endif

#ifndef _GASNET_ERRORDESC
extern const char *gasnet_ErrorDesc(int errval) {
  switch (errval) {
    case GASNET_OK:           return "No error";      
    case GASNET_ERR_NOT_INIT: return "GASNet message layer not initialized"; 
    case GASNET_ERR_BAD_ARG:  return "Invalid function parameter passed";    
    case GASNET_ERR_RESOURCE: return "Problem with requested resource";      
    case GASNET_ERR_BARRIER_MISMATCH: return "Barrier id's mismatched";      
    case GASNET_ERR_NOT_READY: return "Non-blocking operation not complete";      
    default: return "no description available";
  }
}
#endif
/* ------------------------------------------------------------------------------------ */
extern void gasneti_fatalerror(const char *msg, ...) {
  va_list argptr;
  char expandedmsg[255];

  strcpy(expandedmsg, "*** FATAL ERROR: ");
  strcat(expandedmsg, msg);
  strcat(expandedmsg, "\n");
  va_start(argptr, msg); /*  pass in last argument */
    vfprintf(stderr, expandedmsg, argptr);
    fflush(stderr);
  va_end(argptr);

  abort();
}
/* ------------------------------------------------------------------------------------ */
extern void gasneti_killmyprocess(int exitcode) {
  /* wrapper for _exit() that does the "right thing" to immediately kill this process */
  #if GASNETI_THREADS && defined(HAVE_PTHREAD_KILL_OTHER_THREADS_NP)
    /* on LinuxThreads we need to explicitly kill other threads before calling _exit() */
    pthread_kill_other_threads_np();
  #endif
  _exit(exitcode); /* use _exit to bypass atexit handlers */
  gasneti_fatalerror("gasneti_killmyprocess failed to kill the process!");
}
extern void gasneti_flush_streams() {
  if (fflush(NULL)) /* passing NULL to fflush causes it to flush all open FILE streams */
    gasneti_fatalerror("failed to fflush(NULL): %s", strerror(errno));
  if (fflush(stdout)) 
    gasneti_fatalerror("failed to flush stdout: %s", strerror(errno));
  if (fflush(stderr)) 
    gasneti_fatalerror("failed to flush stderr: %s", strerror(errno));
  gasneti_sched_yield();
}
extern void gasneti_close_streams() {
  gasneti_reghandler(SIGPIPE, SIG_IGN); /* In case we still try to generate output */
  if (fclose(stdin)) 
    gasneti_fatalerror("failed to fclose(stdin) in gasnetc_exit: %s", strerror(errno));
  if (fclose(stdout)) 
    gasneti_fatalerror("failed to fclose(stdout) in gasnetc_exit: %s", strerror(errno));
  if (fclose(stderr)) 
    gasneti_fatalerror("failed to fclose(stderr) in gasnetc_exit: %s", strerror(errno));
  gasneti_sched_yield();
}
/* ------------------------------------------------------------------------------------ */
#if defined(__sgi) || defined(__crayx1)
#define _SC_NPROCESSORS_ONLN _SC_NPROC_ONLN
#elif defined(_CRAYT3E)
#define _SC_NPROCESSORS_ONLN _SC_CRAY_MAXPES
#elif defined(HPUX)
#include <sys/param.h>
#include <sys/pstat.h>
#elif defined(__APPLE__) || defined(FREEBSD) || defined(NETBSD)
#include <sys/param.h>
#include <sys/sysctl.h>
#endif
/* return the physical count of CPU's on this node, 
   or zero if that cannot be determined */
extern int gasneti_cpu_count() {
  static int hwprocs = -1;
  if (hwprocs >= 0) return hwprocs;

  #if defined(__APPLE__) || defined(FREEBSD) || defined(NETBSD)
      {
        int mib[2];
        size_t len;

        mib[0] = CTL_HW;
        mib[1] = HW_NCPU;
        len = sizeof(hwprocs);
        if (sysctl(mib, 2, &hwprocs, &len, NULL, 0)) {
           perror("sysctl");
           abort();
        }
        if (hwprocs < 1) hwprocs = 0;
      }
  #elif defined(HPUX) 
      {
        struct pst_dynamic psd;
        if (pstat_getdynamic(&psd, sizeof(psd), (size_t)1, 0) == -1) {
          perror("pstat_getdynamic");
          abort();
        } else {
          hwprocs = psd.psd_proc_cnt;
        }
      }
  #elif defined(SUPERUX) || defined(__MTA__)
      hwprocs = 0; /* appears to be no way to query CPU count on these */
  #else
      hwprocs = sysconf(_SC_NPROCESSORS_ONLN);
      if (hwprocs < 1) hwprocs = 0; /* catch failures on Solaris/Cygwin */
  #endif

  gasneti_assert_always(hwprocs >= 0);
  return hwprocs;
}
/* ------------------------------------------------------------------------------------ */
#if HAVE_SCHED_SETAFFINITY
  #include <sched.h>
#endif
void gasneti_set_affinity_default(int rank) {
  #if !HAVE_SCHED_SETAFFINITY
    /* NO-OP */
    return;
  #else
    int cpus = gasneti_cpu_count();

    if_pf (cpus == 0) {
      static int once = 1;
      if (once) {
	once = 0;
        fprintf(stderr, "WARNING: gasneti_set_affinity called, but cannot determine cpu count.\n");
        fflush(stderr);
      }
      /* becomes a NO-OP */
    } else {
	int local_rank = rank % cpus;
      #if GASNET_SCHED_SETAFFINITY_ARGS == 1
	unsigned long int *mask;
	const int bits_per_long = 8*sizeof(*mask);
	int len = (cpus + bits_per_long - 1) / bits_per_long;
	mask = gasneti_calloc(len, sizeof(*mask));
	mask[local_rank / bits_per_long] = 1 << (local_rank % bits_per_long);
        gasneti_assert_zeroret(sched_setaffinity(0, len*sizeof(*mask), mask));
	gasneti_free(mask);
      #elif GASNET_SCHED_SETAFFINITY_ARGS == 2
        cpu_set_t mask;
        memset(&mask,0,sizeof(mask)); /* in place of CPU_ZERO which is sometimes broken */
        CPU_SET(local_rank % cpus, &mask);
        gasneti_assert_zeroret(sched_setaffinity(0, &mask));
      #elif GASNET_SCHED_SETAFFINITY_ARGS == 3
        cpu_set_t mask;
        memset(&mask,0,sizeof(mask)); /* in place of CPU_ZERO which is sometimes broken */
        CPU_SET(local_rank % cpus, &mask);
        gasneti_assert_zeroret(sched_setaffinity(0, sizeof(mask), &mask));
      #else
	#error "Unknown sched_setaffinity prototype"
      #endif
    }
  #endif
}
#ifndef GASNETC_SET_AFFINITY
  #define GASNETC_SET_AFFINITY(rank) gasneti_set_affinity_default(rank)
#else
  /* Will use conduit-specific GASNETC_SET_AFFINITY() */
#endif
void gasneti_set_affinity(int rank) {
  GASNETI_TRACE_PRINTF(I,("gasnett_set_affinity(%d)", rank));
  GASNETC_SET_AFFINITY(rank);
}
/* ------------------------------------------------------------------------------------ */
/* build a code-location string (used by gasnete_current_loc) */
char *gasneti_build_loc_str(const char *funcname, const char *filename, int linenum) {
  int sz;
  char *loc;
  int fnlen;
  if (!funcname) funcname = "";
  if (!filename) filename = "*unknown file*";
  fnlen = strlen(funcname);
  sz = fnlen + strlen(filename) + 20;
  loc = gasneti_malloc(sz);
  if (*funcname)
    sprintf(loc,"%s%s at %s:%i",
           funcname,
           (fnlen && funcname[fnlen-1] != ')'?"()":""),
           filename, linenum);
  else
    sprintf(loc,"%s:%i", filename, linenum);
  return loc;
}
/* ------------------------------------------------------------------------------------ */
#ifndef GASNETI_UNFREEZE_SIGNAL
/* signal to use for unfreezing, could also use SIGUSR1/2 or several others */
#define GASNETI_UNFREEZE_SIGNAL SIGCONT
#define GASNETI_UNFREEZE_SIGNAL_STR "SIGCONT"
#endif

static volatile int gasnet_frozen = TRUE;
static void gasneti_unfreezeHandler(int sig) {
  gasnet_frozen = 0;
}
/*  all this to make sure we get a full stack frame for debugger */
static void _freezeForDebugger(int depth) {
  if (!depth) _freezeForDebugger(1);
  else {
    volatile int i=0;
    gasneti_sighandlerfn_t old = gasneti_reghandler(GASNETI_UNFREEZE_SIGNAL, gasneti_unfreezeHandler);
    while (gasnet_frozen) {
      i++;
      sleep(1);
    }
    gasneti_reghandler(GASNETI_UNFREEZE_SIGNAL, old);
  }
}
extern void gasneti_freezeForDebugger() {
  char name[255];
  if (gasneti_getenv_yesno_withdefault("GASNET_FREEZE",0)) {
    gethostname(name, 255);
    fprintf(stderr,"GASNet node frozen for debugger: host=%s  pid=%i\n"
                   "To unfreeze, attach a debugger and set 'gasnet_frozen' to 0, or send a "
                   GASNETI_UNFREEZE_SIGNAL_STR "\n", 
                   name, (int)getpid()); 
    fflush(stderr);
    _freezeForDebugger(0);
  }
}
/* ------------------------------------------------------------------------------------ */
gasneti_sighandlerfn_t gasneti_reghandler(int sigtocatch, gasneti_sighandlerfn_t fp) {
  gasneti_sighandlerfn_t fpret = (gasneti_sighandlerfn_t)signal(sigtocatch, fp); 
  if (fpret == (gasneti_sighandlerfn_t)SIG_ERR) {
    gasneti_fatalerror("Got a SIG_ERR while registering handler for signal %i : %s", 
                       sigtocatch,strerror(errno));
    return NULL;
  }
  #ifdef SIG_HOLD
    else if (fpret == (gasneti_sighandlerfn_t)SIG_HOLD) {
      gasneti_fatalerror("Got a SIG_HOLD while registering handler for signal %i : %s", 
                         sigtocatch,strerror(errno));
      return NULL;
    }
  #endif
  return fpret;
}

#define DEF_SIGNAL(name) { name, #name, NULL }
static struct {
  int signum;
  const char *signame;
  gasneti_sighandlerfn_t oldhandler;
} gasneti_signals[] = {
  /* abort signals */
  DEF_SIGNAL(SIGABRT),
  DEF_SIGNAL(SIGILL),
  DEF_SIGNAL(SIGSEGV),
  DEF_SIGNAL(SIGBUS),
  DEF_SIGNAL(SIGFPE),
  /* termination signals */
  DEF_SIGNAL(SIGQUIT),
  DEF_SIGNAL(SIGINT),
  DEF_SIGNAL(SIGTERM),
  DEF_SIGNAL(SIGHUP),
  DEF_SIGNAL(SIGPIPE)
};

#ifndef GASNETC_FATALSIGNAL_CALLBACK
#define GASNETC_FATALSIGNAL_CALLBACK(sig)
#endif

void gasneti_defaultSignalHandler(int sig) {
  gasneti_sighandlerfn_t oldhandler = NULL;
  const char *signame = NULL;
  int i;
  for (i = 0; i < sizeof(gasneti_signals)/sizeof(gasneti_signals[0]); i++) {
    if (gasneti_signals[i].signum == sig) {
      oldhandler = gasneti_signals[i].oldhandler;
      signame = gasneti_signals[i].signame;
    }
  }
  gasneti_assert(signame);

  switch (sig) {
    case SIGQUIT:
      /* client didn't register a SIGQUIT handler, so just exit */
      gasnet_exit(1);
      break;
    case SIGABRT:
    case SIGILL:
    case SIGSEGV:
    case SIGBUS:
    case SIGFPE:
      GASNETC_FATALSIGNAL_CALLBACK(sig); /* give conduit first crack at it */
      fprintf(stderr,"*** Caught a fatal signal: %s(%i) on node %i/%i\n",
        signame, sig, (int)gasnet_mynode(), (int)gasnet_nodes()); 
      fflush(stderr);
      gasneti_print_backtrace(STDERR_FILENO);
      signal(sig, SIG_DFL); /* restore default core-dumping handler and re-raise */
      #if 1
        raise(sig);
      #elif 0
        kill(getpid(),sig);
      #else
        oldhandler(sig);
      #endif
      break;
    default: 
      /* translate signal to SIGQUIT */
      { static int sigquit_raised = 0;
        if (sigquit_raised) {
          /* sigquit was already raised - we cannot safely reraise it, so just die */
          _exit(1);
        } else sigquit_raised = 1;
      }
      fprintf(stderr,"*** Caught a signal: %s(%i) on node %i/%i\n",
        signame, sig, (int)gasnet_mynode(), (int)gasnet_nodes()); 
      fflush(stderr);
      #if 1
        raise(SIGQUIT);
      #elif 0
        kill(getpid(),SIGQUIT);
      #else
        oldhandler(SIGQUIT);
      #endif
      break;
  }
}

void gasneti_registerSignalHandlers(gasneti_sighandlerfn_t handler) {
  int i;
  for (i = 0; i < sizeof(gasneti_signals)/sizeof(gasneti_signals[0]); i++) {
    gasneti_signals[i].oldhandler = 
      gasneti_reghandler(gasneti_signals[i].signum, handler);
  }
}

extern int gasneti_set_waitmode(int wait_mode) {
  const char *desc = NULL;
  GASNETI_CHECKINIT();
  switch (wait_mode) {
    case GASNET_WAIT_SPIN:      desc = "GASNET_WAIT_SPIN"; break;
    case GASNET_WAIT_BLOCK:     desc = "GASNET_WAIT_BLOCK"; break;
    case GASNET_WAIT_SPINBLOCK: desc = "GASNET_WAIT_SPINBLOCK"; break;
    default:
      GASNETI_RETURN_ERRR(BAD_ARG, "illegal wait mode");
  }
  GASNETI_TRACE_PRINTF(I, ("gasnet_set_waitmode(%s)", desc));
  gasneti_wait_mode = wait_mode;
  return GASNET_OK;
}

/* ------------------------------------------------------------------------------------ */
/* Global environment variable handling */

extern uint64_t gasneti_checksum(void *p, int numbytes) {
 uint8_t *buf = (uint8_t *)p;
 uint64_t result = 0;
 int i;
 for (i=0;i<numbytes;i++) {
   result = ((result << 4) | ((result >> 60) & 0x0F) ) ^ *buf;
   buf++;
 }
 return result;
}

extern char **environ; 

static void gasneti_serializeEnvironment(uint8_t **pbuf, int *psz) {
  /* flatten a snapshot of the environment to make it suitable for transmission
   * here we assume the standard representation where a pointer to the environment 
   * is stored in a global variable 'environ' and the environment is represented as an array 
   * of null-terminated strings where each has the form 'key=value' and value may be empty, 
   * and the final string pointer is a NULL pointer
   * we flatten this into a list of null-terminated 'key=value' strings, 
   * terminated with a double-null
   */
  uint8_t *buf; 
  uint8_t *p;
  int i;
  int totalEnvSize = 0;
  if (!environ) {
    /* T3E stupidly omits environ support, despite documentation to the contrary */
    GASNETI_TRACE_PRINTF(I,("WARNING: environ appears to be empty -- ignoring it"));
    *pbuf = NULL;
    *psz = 0;
    return;
  }
  for(i = 0; environ[i]; i++) 
    totalEnvSize += strlen(environ[i]) + 1;
  totalEnvSize++;

  buf = (uint8_t *)gasneti_malloc(totalEnvSize);
  p = buf;
  p[0] = 0;
  for(i = 0; environ[i]; i++) {
    strcpy((char*)p, environ[i]);
    p += strlen((char*)p) + 1;
    }
  *p = 0;
  gasneti_assert((p+1) - buf == totalEnvSize);

  *pbuf = buf;
  *psz = totalEnvSize;
}

static char *gasneti_globalEnv = NULL;

typedef struct {
  int sz;
  uint64_t checksum;
} gasneti_envdesc_t;

/* do the work necessary to setup the global environment for use by gasneti_getenv
   broadcast the environment variables from one node to all nodes
   Note this currently assumes that at least one of the compute nodes has the full
    environment - systems where the environment is not propagated to any compute node
    will need something more sophisticated.
   exchangefn is required function for exchanging data 
   broadcastfn is optional (can be NULL) but highly recommended for scalability
 */
extern void gasneti_setupGlobalEnvironment(gasnet_node_t numnodes, gasnet_node_t mynode,
                                           gasneti_bootstrapExchangefn_t exchangefn,
                                           gasneti_bootstrapBroadcastfn_t broadcastfn) {
  uint8_t *myenv; 
  int sz; 
  uint64_t checksum;
  gasneti_envdesc_t myenvdesc;
  gasneti_envdesc_t *allenvdesc;

  gasneti_assert(exchangefn);

  gasneti_serializeEnvironment(&myenv,&sz);
  checksum = gasneti_checksum(myenv,sz);

  myenvdesc.sz = sz;
  myenvdesc.checksum = checksum;

  allenvdesc = gasneti_malloc(numnodes*sizeof(gasneti_envdesc_t));
  /* gather environment description from all nodes */
  (*exchangefn)(&myenvdesc, sizeof(gasneti_envdesc_t), allenvdesc);

  { /* see if the node environments differ and find the largest */
    int i;
    int rootid = 0;
    int identical = 1;
    gasneti_envdesc_t rootdesc = allenvdesc[rootid];
    for (i=1; i < numnodes; i++) {
      if (rootdesc.checksum != allenvdesc[i].checksum || 
          rootdesc.sz != allenvdesc[i].sz) 
          identical = 0;
      if (allenvdesc[i].sz > rootdesc.sz) { 
        /* assume the largest env is the one we want */
        rootdesc = allenvdesc[i];
        rootid = i;
      }
    }
    if (identical) { /* node environments all identical - don't bother to propagate */
      gasneti_free(allenvdesc);
      gasneti_free(myenv);
      return;
    } else {
      int envsize = rootdesc.sz;
      gasneti_globalEnv = gasneti_malloc(envsize);
      if (broadcastfn) {
        (*broadcastfn)(myenv, envsize, gasneti_globalEnv, rootid);
      } else {
        /* this is wasteful of memory and bandwidth, and non-scalable */
        char *tmp = gasneti_malloc(envsize*numnodes);
        memcpy(tmp+mynode*envsize, myenv, sz);
        (*exchangefn)(tmp+mynode*envsize, envsize, tmp);
        memcpy(gasneti_globalEnv, tmp+rootid*envsize, envsize);
        gasneti_free(tmp);
      }
      gasneti_assert(gasneti_checksum(gasneti_globalEnv,envsize) == rootdesc.checksum);
      gasneti_free(allenvdesc);
      gasneti_free(myenv);
      return;
    }
  }

}

/* decode src into dst, arguments permitted to overlap exactly */
extern size_t gasneti_decodestr(char *dst, const char *src) {
  #define IS_HEX_DIGIT(c)  (isdigit(c) || (isalpha(c) && toupper(c) <= 'F'))
  #define VAL_HEX_DIGIT(c) ((unsigned int)(isdigit(c) ? (c)-'0' : 10 + toupper(c) - 'A'))
  size_t dstidx = 0;
  const char *p = src;
  gasneti_assert(src && dst);
  while (*p) {
    char c;
    if (p[0] == '%' && p[1] == '0' && 
        p[2] && IS_HEX_DIGIT(p[2]) && p[3] && IS_HEX_DIGIT(p[3])) {
      c = (char)(VAL_HEX_DIGIT(p[2]) << 4) | VAL_HEX_DIGIT(p[3]);
      p += 4;
    } else c = *(p++);
    dst[dstidx++] = c;
  }
  dst[dstidx] = '\0';
  return dstidx;
  #undef IS_HEX_DIGIT
}

static const char *gasneti_decode_envval(const char *val) {
  static struct _gasneti_envtable_S {
    const char *pre;
    char *post;
    struct _gasneti_envtable_S *next;
  } *gasneti_envtable = NULL;
  static gasneti_mutex_t gasneti_envtable_lock = GASNETI_MUTEX_INITIALIZER;
  if (strstr(val,"%0")) {
    struct _gasneti_envtable_S *p;
    gasneti_mutex_lock(&gasneti_envtable_lock);
      p = gasneti_envtable;
      while (p) {
        if (!strcmp(val, p->pre)) break;
        p = p->next;
      }
      if (p) val = p->post;
      else { /* decode it and save the result (can't trust setenv to safely set it back) */
        struct _gasneti_envtable_S *newentry = gasneti_malloc(sizeof(struct _gasneti_envtable_S));
        newentry->pre = gasneti_strdup(val);
        newentry->post = gasneti_malloc(strlen(val)+1);
        gasneti_decodestr(newentry->post, newentry->pre);
        if (!strcmp(newentry->post, newentry->pre)) { 
          gasneti_free(newentry); 
        } else {
          newentry->next = gasneti_envtable;
          gasneti_envtable = newentry;
          val = newentry->post;
        }
      }
    gasneti_mutex_unlock(&gasneti_envtable_lock);
  }
  return val;
}
/* expose environment decode to external packages in case we ever need it */
extern const char * (*gasnett_decode_envval_fn)(const char *);
const char * (*gasnett_decode_envval_fn)(const char *) = &gasneti_decode_envval;

extern void gasneti_decode_args(int *argc, char ***argv) {
  static int firsttime = 1;
  if (!firsttime) return; /* ignore subsequent calls, to allow early decode */
  firsttime = 0;
  if (!gasneti_getenv_yesno_withdefault("GASNET_DISABLE_ARGDECODE",0)) {
    int argidx;
    char **origargv = *argv;
    for (argidx = 0; argidx < *argc; argidx++) {
      if (strstr((*argv)[argidx], "%0")) {
        char *tmp = gasneti_strdup((*argv)[argidx]);
        int newsz = gasneti_decodestr(tmp, tmp);
        if (newsz == strlen((*argv)[argidx])) gasneti_free(tmp); /* no change */
        else {
          int i, newcnt = 0;
          for (i = 0; i < newsz; i++) if (!tmp[i]) newcnt++; /* count growth due to inserted NULLs */
          if (newcnt == 0) { /* simple parameter replacement */
            (*argv)[argidx] = tmp;
          } else { /* need to grow argv */
            char **newargv = gasneti_malloc(sizeof(char *)*(*argc+1+newcnt));
            memcpy(newargv, *argv, sizeof(char *)*argidx);
            newargv[argidx] = tmp; /* base arg */
            memcpy(newargv+argidx+newcnt, (*argv)+argidx, sizeof(char *)*(*argc - argidx - 1));
            for (i = 0; i < newsz; i++) /* hook up new args */
              if (!tmp[i]) newargv[1+argidx++] = &(tmp[i+1]); 
            *argc += newcnt;
            if (*argv != origargv) gasneti_free(*argv);
            *argv = newargv;
            (*argv)[*argc] = NULL; /* ensure null-termination of arg list */
          }
        } 
      }
    }
  }
}

gasneti_getenv_fn_t *gasneti_conduit_getenv = NULL;

extern char *gasneti_getenv(const char *keyname) {
  char *retval = NULL;
  static int firsttime = 1;
  static int decodeenv = 1;
  if (firsttime && strcmp(keyname, "GASNET_DISABLE_ENVDECODE") /* prevent inf recursion */
                && strcmp(keyname, "GASNET_VERBOSEENV")) {
    decodeenv = !gasneti_getenv("GASNET_DISABLE_ENVDECODE");
    if (gasneti_init_done) {
      gasneti_envstr_display("GASNET_DISABLE_ENVDECODE",(decodeenv?"NO":"YES"),decodeenv);
      gasneti_sync_writes();
      firsttime = 0;
    }
  } else gasneti_sync_reads();

  if (keyname && gasneti_conduit_getenv) {
    /* highest priority given to conduit-specific getenv */
    retval = (*gasneti_conduit_getenv)(keyname);
  }

  if (keyname && !retval && gasneti_globalEnv) { 
    /* global environment takes precedence 
     * (callers who want the local environment can call getenv directly)
     */
    char *p = gasneti_globalEnv;
    int keylen = strlen(keyname);
    while (*p) {
      if (!strncmp(keyname, p, keylen) && p[keylen] == '=') {
        retval = p + keylen + 1;
        break;
      }
      p += strlen(p) + 1;
    }
  }

  if (keyname && !retval) /* try local environment */
    retval = getenv(keyname);
  
  if (retval && decodeenv) { /* check if environment value needs decoding */
    retval = (char *)gasneti_decode_envval(retval);
  }

  GASNETI_TRACE_PRINTF(I,("gasnet_getenv(%s) => '%s'",
                          (keyname?keyname:"NULL"),(retval?retval:"NULL")));

  return retval;
}

extern int64_t gasneti_parse_int(const char *str, uint64_t mem_size_multiplier) {
  uint64_t val = 0;
  int base = 10;
  int neg = 0;
  const char *p = str;
  #define GASNETI_NUMBUF_SZ 80
  int isfrac = 0;
  char numbuf[GASNETI_NUMBUF_SZ+1];
  int i = 0;

  if (!str) return 0; /* null returns 0 */

  if (*p == '+') p++; /* check for sign */
  else if (*p == '-') { neg=1; p++; }
  while (*p && isspace(*p)) p++; /* eat spaces */
  if (!*p) return 0; /* empty string returns 0 */
  if (*p == '0' && toupper(*(p+1)) == 'X') { base = 16; p += 2; } /* check for hex */

  while (*p && i < GASNETI_NUMBUF_SZ &&
         ( (isdigit(*p) && *p < ('0'+base)) ||
           (isalpha(*p) && toupper(*p) < ('A'+base-10)) || *p == '.') ) {
    if (isdigit(*p)) { val = (val * base) + (*p - '0'); }
    else if (isalpha(*p)) { val = (val * base) + (10 + toupper(*p) - 'A'); }
    else if (*p == '.') { 
      isfrac = 1; /* user value is a fraction */
      if (base != 10) gasneti_fatalerror("Format error in numerical string: %s", str);
    }
    numbuf[i++] = *p;
    p++;
  }
  numbuf[i] = '\0';
  while (*p && isspace(*p)) p++; /* eat spaces */
  if (mem_size_multiplier) { /* its a mem size, check for provided unit overridder */
    if      (*p == 'T' || *p == 't') mem_size_multiplier = ((uint64_t)1)<<40;
    else if (*p == 'G' || *p == 'g') mem_size_multiplier = ((uint64_t)1)<<30;
    else if (*p == 'M' || *p == 'm') mem_size_multiplier = ((uint64_t)1)<<20;
    else if (*p == 'K' || *p == 'k') mem_size_multiplier = ((uint64_t)1)<<10;
    else if (*p == 'B' || *p == 'b') mem_size_multiplier = 1;
    /* else - default to the context-sensitive mem_size_multiplier of the caller */
  } else {
    mem_size_multiplier = 1;
  }
  if (isfrac) {
    double dval = atof(numbuf);
    val = (uint64_t)(int64_t)(dval*(double)mem_size_multiplier);
  } else {
    val = val * mem_size_multiplier;
  }

  if (neg) return -((int64_t)val);
  return (int64_t)val;
  #undef GASNETI_NUMBUF_SZ
}

extern char *gasneti_format_number(int64_t val, char *buf, size_t bufsz, int is_mem_size) {
  const char *unit = "";
  const char *neg = "";
  int64_t divisor = 1;
  if (val < 0) { val = -val; neg = "-"; }
  if (val >= ((int64_t)1) << 50) divisor = -1; /* use hex for huge vals */
  else if (is_mem_size) {
    /* Try to strike a compromise between digits and round off */
    #define GASNETI_USE_DIV(div, unit_str)                           \
      if ((val >= 10*(div)) || ((val >= (div)) && !(val % (div)))) { \
        divisor = (div); unit = (unit_str); break;                   \
      }
    do {
      GASNETI_USE_DIV(((int64_t)1) << 40, " TB");
      GASNETI_USE_DIV(((int64_t)1) << 30, " GB");
      GASNETI_USE_DIV(((int64_t)1) << 20, " MB");
      GASNETI_USE_DIV(((int64_t)1) << 10, " KB");
      GASNETI_USE_DIV(((int64_t)1), " B");
    } while (0);
    #undef GASNETI_USE_DIV
  } 

  if (divisor > 0) {
    snprintf(buf, bufsz, "%s%llu%s", neg, (unsigned long long)(val/divisor), unit);
  } else if (divisor == -1) {
    if (neg) val = -val;
    snprintf(buf, bufsz, "0x%llx", (unsigned long long)val);
  } else gasneti_fatalerror("internal error in gasneti_format_number");
  return buf;
}

/* expression that defines whether the given process should report to the console
   on env queries - needs to work before gasnet_init
 */
#ifndef GASNETI_ENV_OUTPUT_NODE
#define GASNETI_ENV_OUTPUT_NODE() \
        (gasneti_mynode == 0 || gasneti_mynode == (gasnet_node_t)-1)
#endif

/* return true iff GASNET_VERBOSEENV reporting is enabled on this node */
extern int gasneti_verboseenv() {
  static int firsttime = 1;
  static int verboseenv = 0;
  if (firsttime) {
    #if GASNET_DEBUG_VERBOSE
      verboseenv = GASNETI_ENV_OUTPUT_NODE();
    #else
      verboseenv = !!gasneti_getenv("GASNET_VERBOSEENV") && GASNETI_ENV_OUTPUT_NODE();
    #endif
    gasneti_sync_writes();
    if (gasneti_init_done) firsttime = 0;
  } else gasneti_sync_reads();
  return verboseenv;
}
/* display an integral/string environment setting iff gasneti_verboseenv() */
extern void gasneti_envstr_display(const char *key, const char *val, int is_dflt) {
  const char *dflt = (is_dflt?"   (default)":"");
  if (gasneti_verboseenv()) {
    const char *displayval = val;
    int width;
    if (strlen(val) == 0) displayval = "*empty*";
    width = MAX(10,55 - strlen(key) - strlen(displayval));
    fprintf(stderr, "ENV parameter: %s = %s%*s\n", key, displayval, width, dflt);
    fflush(stderr);
  }
  GASNETI_TRACE_PRINTF(I,("ENV parameter: %s = %s%s", key, val, dflt));
}
extern void gasneti_envint_display(const char *key, int64_t val, int is_dflt, int is_mem_size) {
  char valstr[80];
  char displayval[80];
  if (!gasneti_verboseenv() && !GASNETI_TRACE_ENABLED(I)) return;

  gasneti_format_number(val, valstr, 80, is_mem_size);

  if (is_dflt) { /* Use the numerical value */
    strcpy(displayval, valstr);
  } else { /* Use the environment string and numerical value */
    snprintf(displayval, sizeof(displayval), "%s (%s)", gasneti_getenv(key), valstr);
  }
  gasneti_envstr_display(key, displayval, is_dflt);
}

static char *_gasneti_getenv_withdefault(const char *keyname, const char *defaultval, int valmode, int64_t *val) {
  const char * retval = NULL;
  int is_dflt = 0;
  gasneti_assert(defaultval != NULL);
  retval = gasneti_getenv(keyname);
  if (retval == NULL) { retval = defaultval; is_dflt = 1; }

  if (valmode == 0) {
    /* just a string value */
    gasneti_envstr_display(keyname, retval, is_dflt);
  } else if (valmode == 1) { /* yes/no value */
    char s[10];
    int i;
    strncpy(s, retval, 10); s[9] = '\0';
    for (i = 0; i < 10; i++) s[i] = toupper(s[i]);
    if (!strcmp(s, "N") || !strcmp(s, "NO") || !strcmp(s, "0")) retval = "NO";
    else if (!strcmp(s, "Y") || !strcmp(s, "YES") || !strcmp(s, "1")) retval = "YES";
    else gasneti_fatalerror("If used, environment variable '%s' must be set to 'Y|YES|y|yes|1' or 'N|n|NO|no|0'", keyname);
    gasneti_envstr_display(keyname, retval, is_dflt);
  } else if (valmode == 2 || valmode == 3) { /* int value, regular or memsize */
    int is_mem_size = (valmode == 3);
    gasneti_assert(val);
    *val = gasneti_parse_int(retval, *val);
    gasneti_envint_display(keyname, *val, is_dflt, is_mem_size);
  } else gasneti_fatalerror("internal error in _gasneti_getenv_withdefault");

  return (char *)retval;
}
extern char *gasneti_getenv_withdefault(const char *keyname, const char *defaultval) {
  return _gasneti_getenv_withdefault(keyname, defaultval, 0, NULL);
}
extern int gasneti_getenv_yesno_withdefault(const char *keyname, int defaultval) {
  return !strcmp(_gasneti_getenv_withdefault(keyname, (defaultval?"YES":"NO"), 1, NULL), "YES");
}
extern int64_t gasneti_getenv_int_withdefault(const char *keyname, int64_t defaultval, uint64_t mem_size_multiplier) {
  int64_t val = mem_size_multiplier;
  char defstr[80];
  gasneti_format_number(defaultval, defstr, 80, mem_size_multiplier);
  _gasneti_getenv_withdefault(keyname, defstr, (mem_size_multiplier?3:2), &val);
  return val;
}

/* set an environment variable, for the local process ONLY */
extern void gasneti_setenv(const char *key, const char *value) {
  /* both are POSIX - prefer setenv because it manages memory for us */
  #if HAVE_SETENV
    int retval = setenv(key, value, 1);
    if (retval) gasneti_fatalerror("Failed to setenv(\"%s\",\"%s\",1) in gasneti_setenv => %s(%i)",
                                     key, value, strerror(errno), errno);
  #elif HAVE_PUTENV 
    char *tmp = gasneti_malloc(strlen(key) + strlen(value) + 2);
    int retval;
    strcpy(tmp, key);
    strcat(tmp, "=");
    strcat(tmp, value);
    retval = putenv(tmp);
    if (retval) gasneti_fatalerror("Failed to putenv(\"%s\") in gasneti_setenv => %s(%i)",
                                     tmp, strerror(errno), errno);
  #else
    gasneti_fatalerror("Got a call to gasneti_setenv, but don't know how to do that on your system");
  #endif
}

/* unset an environment variable, for the local process ONLY */
extern void gasneti_unsetenv(const char *key) {
  /* prefer unsetenv because it's documented to remove env vars */
  #if HAVE_UNSETENV
   #if 0
    /* bug 1135: POSIX requires unsetenv to return int, but several OS's (at least Linux and BSD)
                 are non-compliant and return void. It's not worth our trouble to detect
                 this (since the possible errors are few) so ignore the return value */
    int retval = unsetenv(key);
    if (!retval) gasneti_fatalerror("Failed to unsetenv(\"%s\") in gasneti_unsetenv => %s(%i)",
                                     key, strerror(errno), errno);
   #else
    /* check for a few error cases ourselves */
    if (!key || strlen(key)==0 || strchr(key,'=')) 
       gasneti_fatalerror("Bad key (\"%s\") passed to gasneti_unsetenv",key);
    unsetenv(key);
   #endif
  #elif HAVE_PUTENV
    /* this relies on undocumented putenv behavior, and may or may not work */
    char *tmp = gasneti_malloc(strlen(key) + 2);
    int retval;
    strcpy(tmp, key);
    strcat(tmp, "=");
    retval = putenv(tmp);
    if (retval) gasneti_fatalerror("Failed to putenv(\"%s\") in gasneti_unsetenv => %s(%i)",
                                     key, strerror(errno), errno);
  #else
    gasneti_fatalerror("Got a call to gasneti_unsetenv, but don't know how to do that on your system");
  #endif
}

/* ------------------------------------------------------------------------------------ */
/* Bits for conduits which want/need to override pthread_create() */

#if defined(PTHREAD_MUTEX_INITIALIZER) /* only if pthread.h available */ && !GASNET_SEQ
  #ifndef GASNETC_PTHREAD_CREATE_OVERRIDE
    /* Default is just pass through */
    #define GASNETC_PTHREAD_CREATE_OVERRIDE(create_fn, thread, attr, start_routine, arg) \
      (*create_fn)(thread, attr, start_routine, arg)
  #endif

  int gasneti_pthread_create(gasneti_pthread_create_fn_t *create_fn, pthread_t *thread, const pthread_attr_t *attr, void *(*start_routine)(void *), void *arg) {
    GASNETI_TRACE_PRINTF(I, ("gasneti_pthread_create(%p, %p, %p, %p, %p)", create_fn, thread, attr, start_routine, arg));
    return GASNETC_PTHREAD_CREATE_OVERRIDE(create_fn, thread, attr, start_routine, arg);
  }
#endif

/* ------------------------------------------------------------------------------------ */
/* Dynamic backtrace support */

/* Logic to pick which backtrace functions to try.
 * Two cases to allow for debuggers that might not dump the proper thread
*/
#if GASNETI_THREADS && HAVE_BACKTRACE
  /* Since we have libc support, only enable debuggers that do OK w/ threads. */
  #define GASNETI_BT_EXECINFO	&gasneti_bt_execinfo
  #if defined(GDB_PATH) && !GASNETI_NO_FORK
    #define GASNETI_BT_GDB	&gasneti_bt_gdb
  #endif
  #if defined(LADEBUG_PATH) && !GASNETI_NO_FORK
    #define GASNETI_BT_LADEBUG	&gasneti_bt_ladebug
  #endif
#else
  /* Either no threads or no libc support, enable any debugger we might have. */
  #if HAVE_BACKTRACE
    #define GASNETI_BT_EXECINFO	&gasneti_bt_execinfo
  #endif
  #if defined(GDB_PATH) && !GASNETI_NO_FORK
    #define GASNETI_BT_GDB	&gasneti_bt_gdb
  #endif
  #if defined(LADEBUG_PATH) && !GASNETI_NO_FORK
    #define GASNETI_BT_LADEBUG	&gasneti_bt_ladebug
  #endif
  #if defined(DBX_PATH) && !GASNETI_NO_FORK
    #define GASNETI_BT_DBX	&gasneti_bt_dbx
  #endif
#endif

/* Format for labelling output lines */
#ifndef GASNETI_BT_LABEL_FMT
  #define GASNETI_BT_LABEL_FMT "[%d] "
#endif

#if !GASNETI_NO_FORK
/* Execute system w/ stdout redirected to 'fd' and std{in,err} to /dev/null */
static int gasneti_system_redirected(const char *cmd, int stdout_fd) {
  int rc;
  int saved_stdin, saved_stdout, saved_stderr;

  /* Redirect output to 'fd' and std{in,err} to /dev/null */
  saved_stdin = dup(STDIN_FILENO);
  saved_stdout = dup(STDOUT_FILENO);
  saved_stderr = dup(STDERR_FILENO);
  dup2(stdout_fd, STDOUT_FILENO);
  rc = open("/dev/null", O_WRONLY); dup2(rc, STDERR_FILENO); close(rc);
  rc = open("/dev/null", O_RDONLY); dup2(rc, STDIN_FILENO); close(rc);

  /* Run the command */
  rc = system(cmd);

  /* Restore I/O */
  dup2(saved_stdout, STDOUT_FILENO); close(saved_stdout);
  dup2(saved_stderr, STDERR_FILENO); close(saved_stderr);
  dup2(saved_stdin, STDIN_FILENO); close(saved_stdin);
  return rc;
}
#endif

#ifdef GASNETI_BT_LADEBUG
  static int gasneti_bt_ladebug(int fd) {
    #if GASNETI_THREADS
      const char fmt[] = "echo 'set $stoponattach; attach %d; show thread *; where thread *; quit' | %s '%s'"; 
    #else
      const char fmt[] = "echo 'set $stoponattach; attach %d; where; quit' | %s '%s'"; 
    #endif
    static char cmd[1024];
    /* Try to be smart if not in same place as at configure time */
    const char *ladebug = (access(LADEBUG_PATH, X_OK) ? "ladebug" : LADEBUG_PATH);
    int rc = sprintf(cmd, fmt, (int)getpid(), ladebug, gasneti_exename);
    if (rc < 0) return -1;
    return gasneti_system_redirected(cmd, fd);
  }
#endif

#ifdef GASNETI_BT_DBX
  static int gasneti_bt_dbx(int fd) {
    /* dbx's thread support is poor and not easily scriptable */
    const char fmt[] = "echo 'attach %d; where; quit' | %s '%s'";  
    static char cmd[1024];
    const char *dbx = (access(DBX_PATH, X_OK) ? "dbx" : DBX_PATH);
    int rc = sprintf(cmd, fmt, (int)getpid(), dbx, gasneti_exename);
    if (rc < 0) return -1;
    return gasneti_system_redirected(cmd, fd);
  }
#endif

#ifdef GASNETI_BT_GDB
  static int gasneti_bt_gdb(int fd) {
    /* Change "backtrace" to "backtrace full" to also see local vars from each frame */
    #if GASNETI_THREADS
      const char commands[] = "info threads\nthread apply all backtrace\ndetach\nquit\n";
    #else
      const char commands[] = "backtrace\ndetach\nquit\n";
    #endif
    const char fmt[] = "%s -nx -batch -x %s '%s' %d";
    static char cmd[1024];
    char filename[255];
    const char *gdb = (access(GDB_PATH, X_OK) ? "gdb" : GDB_PATH);
    int rc;

    /* Build gdb commands file, since it won't take commands on stdin */
    {
      int tmpfd, len;

      if (getenv("TMPDIR")) strcpy(filename,getenv("TMPDIR"));
      else strcpy(filename,"/tmp");
      strcat(filename,"/gasnet_XXXXXX");
      tmpfd = mkstemp(filename);
      if (tmpfd < 0) return -1;

      len = sizeof(commands) - 1;
      rc = write(tmpfd, commands, len);
      if (rc != len) return -1;

      rc = close(tmpfd);
      if (rc < 0) return -1;
    }

    rc = sprintf(cmd, fmt, gdb, filename, gasneti_exename, (int)getpid());
    if (rc < 0) return -1;

    rc = gasneti_system_redirected(cmd, fd);

    (void)unlink(filename);

    return rc;
  }
#endif

#ifdef GASNETI_BT_EXECINFO
  static int gasneti_bt_execinfo(int fd) {
    #define MAXBT 1024
    static void *btaddrs[MAXBT];
    int entries;
    char **fnnames = NULL;
    int i;
    entries = backtrace(btaddrs, MAXBT);
    #if HAVE_BACKTRACE_SYMBOLS
      fnnames = backtrace_symbols(btaddrs, entries);
    #endif
    for (i=0; i < entries; i++) {
      FILE *xlate;
      #define XLBUF 1024
      static char xlstr[XLBUF];
      static char linebuf[XLBUF];
      int len;
      xlstr[0] = '\0';
      #if defined(ADDR2LINE_PATH) && !GASNETI_NO_FORK
        /* use addr2line when available to retrieve symbolic info */
        { static char cmd[255];
          sprintf(cmd,"%s -f -e '%s' %p", ADDR2LINE_PATH, gasneti_exename, btaddrs[i]);
          xlate = popen(cmd, "r");
          if (xlate) {
            char *p = xlstr;
            int sz = XLBUF;
            while (fgets(p, sz, xlate)) {
              p += strlen(p) - 1;
              if (*p != '\n') p++;
              strcpy(p, " ");
              p += strlen(p);
            }
            pclose(xlate);
          }
        }
      #endif
      sprintf(linebuf, "%i: %s ", i, (fnnames?fnnames[i]:""));
      write(fd, linebuf, strlen(linebuf));
      write(fd, xlstr, strlen(xlstr));
      write(fd, "\n", 1);
    }
    /* if (fnnames) free(fnnames); */
    return 0;
  }
#endif

/* "best effort" to produce a backtrace
 * Returns 0 on apparent success, non-zero otherwise.
 * NOTE: If fd corresponds to a FILE*, caller should fflush() it first.
 */
int _gasneti_print_backtrace(int fd) {
  /* declare fn_table as static array of const pointer to function(int) returning int */
  static int (* const fn_table[])(int) = {
    #ifdef GASNETI_BT_LADEBUG
      GASNETI_BT_LADEBUG,
    #endif
    #ifdef GASNETI_BT_GDB
      GASNETI_BT_GDB,
    #endif
    #ifdef GASNETI_BT_DBX
      GASNETI_BT_DBX,
    #endif
    #ifdef GASNETI_BT_EXECINFO
      GASNETI_BT_EXECINFO,
    #endif
    NULL	/* Avoids empty initializer and trailing commas */
  };
  static gasneti_mutex_t btlock = GASNETI_MUTEX_INITIALIZER;
  int count = (sizeof(fn_table)/sizeof(fn_table[0])) - 1; /* excludes the NULL */
  gasneti_sighandlerfn_t old_ABRT, old_ILL, old_SEGV, old_BUS, old_FPE;
  int retval = 1;
  int i;

  /* Save signal handlers to avoid recursion */
  old_ABRT = (gasneti_sighandlerfn_t)signal(SIGABRT, SIG_DFL);
  old_ILL  = (gasneti_sighandlerfn_t)signal(SIGILL,  SIG_DFL);
  old_SEGV = (gasneti_sighandlerfn_t)signal(SIGSEGV, SIG_DFL);
  old_BUS  = (gasneti_sighandlerfn_t)signal(SIGBUS,  SIG_DFL);
  old_FPE  = (gasneti_sighandlerfn_t)signal(SIGFPE,  SIG_DFL);

  if (!gasneti_getenv_yesno_withdefault("GASNET_BACKTRACE",0)) {
    if (count) {
      /* XXX: Should this be going to the caller-provided fd instead of stderr? */
      fprintf(stderr, "NOTICE: Before reporting bugs, run with GASNET_BACKTRACE=1 in the environment to generate a backtrace. \n");
      fflush(stderr);
    } else {
      /* We don't support any backtrace methods, so avoid false advertising. */
    }
    retval = 1;
  } else {
    FILE *file;

    gasneti_mutex_lock(&btlock);

    /* Create a tmpfile to hold the backtrace */
    file = tmpfile ();

    if (file) {
      int tmpfd = fileno(file);
      /* Loop over table until success or end */
      for (i = 0; i < count; ++i) {
        retval = (*fn_table[i])(tmpfd);
        if (retval == 0) {
	  static char linebuf[1024];
	  char *p;
	  int len;
          int tracefd = -1;
          FILE *tracefp = NULL;
#if GASNET_TRACE
          tracefp = gasneti_tracefile;
	  if (tracefp) tracefd = fileno(tracefp);
#endif
          sprintf(linebuf, GASNETI_BT_LABEL_FMT, (int)gasneti_mynode);
          len = strlen(linebuf);
          p = linebuf + len;
	  len = sizeof(linebuf) - len;

	  /* Send to requested destination (and tracefile if any) */
	  if (tracefd >= 0) {
	    GASNETI_TRACE_PRINTF(U,("========== BEGIN BACKTRACE ==========")); fflush(tracefp);
	  }
	  rewind(file);
	  while (fgets(p, len, file)) {
	    write(fd, linebuf, strlen(linebuf)); /* w/ node prefix */
	    if (tracefd >= 0) {
	      write(tracefd, p, strlen(p)); /* w/o node prefix */
	    }
	  }
	  if (tracefd >= 0) {
	    GASNETI_TRACE_PRINTF(U,("========== END BACKTRACE ==========")); fflush(tracefp);
          }
          break;
        } else {
	  rewind(file);
        }
      }

      fclose(file);
    }

    gasneti_mutex_unlock(&btlock);
  }

  signal(SIGABRT, old_ABRT);
  signal(SIGILL,  old_ILL);
  signal(SIGSEGV, old_SEGV);
  signal(SIGBUS,  old_BUS);
  signal(SIGFPE,  old_FPE);

  return retval;
}

int (* gasneti_print_backtrace)(int) = &_gasneti_print_backtrace;

/* ------------------------------------------------------------------------------------ */
/* Debug memory management
   debug memory format:
  | prev | next | allocdesc (pad to 8 bytes) | datasz | BEGINPOST | <user data> | ENDPOST |
                                             ptr returned by malloc ^
 */
#if GASNET_DEBUG
  typedef struct gasneti_memalloc_desc {  
    struct gasneti_memalloc_desc * volatile prevdesc;
    struct gasneti_memalloc_desc * volatile nextdesc;
    uint64_t allocdesc; /* actually a (void*) */
    uint64_t datasz;
    uint64_t beginpost;
  } gasneti_memalloc_desc_t;
  static uint64_t gasneti_memalloc_allocatedbytes = 0;   /* num bytes ever allocated */
  static uint64_t gasneti_memalloc_freedbytes = 0;       /* num bytes ever freed */
  static uint64_t gasneti_memalloc_allocatedobjects = 0; /* num objects ever allocated */
  static uint64_t gasneti_memalloc_freedobjects = 0;     /* num objects ever freed */
  static uint64_t gasneti_memalloc_ringobjects = 0;      /* num objects in the ring */
  static uint64_t gasneti_memalloc_ringbytes = 0;        /* num bytes in the ring */
  static size_t   gasneti_memalloc_maxobjectsize = 0;    /* max object size ever allocated */
  static uintptr_t gasneti_memalloc_maxobjectloc = 0;    /* max address ever allocated */
  static uint64_t gasneti_memalloc_maxlivebytes = 0;     /* max num bytes live at any given time */
  static uint64_t gasneti_memalloc_maxliveobjects = 0;   /* max num bytes live at any given time */
  static int gasneti_memalloc_extracheck = 0;
  static int gasneti_memalloc_init = -1;
  static uint64_t gasneti_memalloc_initval = 0;
  static int gasneti_memalloc_clobber = -1;
  static uint64_t gasneti_memalloc_clobberval = 0;
  static int gasneti_memalloc_leakall = -1;
  static int gasneti_memalloc_scanfreed = -1;
  static int gasneti_memalloc_envisinit = 0;
  static gasneti_mutex_t gasneti_memalloc_lock = GASNETI_MUTEX_INITIALIZER;
  static gasneti_memalloc_desc_t *gasneti_memalloc_pos = NULL;
  #define GASNETI_MEM_BEGINPOST   ((uint64_t)0xDEADBABEDEADBABEllu)
  #define GASNETI_MEM_ENDPOST     ((uint64_t)0xCAFEDEEDCAFEDEEDllu)
  #define GASNETI_MEM_FREEMARK    ((uint64_t)0xBEEFEFADBEEFEFADllu)
  #define GASNETI_MEM_HEADERSZ    (sizeof(gasneti_memalloc_desc_t))
  #define GASNETI_MEM_TAILSZ      8     
  #define GASNETI_MEM_EXTRASZ     (GASNETI_MEM_HEADERSZ+GASNETI_MEM_TAILSZ)     
  #define GASNETI_MEM_MALLOCALIGN 4
  #define gasneti_looksaligned(p) (!(((uintptr_t)(p)) & (GASNETI_MEM_MALLOCALIGN-1)))

  static uint64_t gasneti_memalloc_envint(const char *name, const char *deflt) {
    /* Signaling NaN: any bit pattern between 0x7ff0000000000001 and 0x7ff7ffffffffffff  
                   or any bit pattern between 0xfff0000000000001 and 0xfff7ffffffffffff
       Quiet NaN: any bit pattern between 0x7ff8000000000000 and 0x7fffffffffffffff 
               or any bit pattern between 0xfff8000000000000 and 0xffffffffffffffff
    */
    uint64_t sNAN = 0x7ff7ffffffffffffllu; 
    uint64_t qNAN = 0x7fffffffffffffffllu;
    uint64_t val = 0;
    const char *envval = gasneti_getenv_withdefault(name, deflt);
    const char *p = envval;
    char tmp[255];
    int i = 0;
    for ( ; *p; p++) {
      if (!isspace(*p)) tmp[i++] = toupper(*p);
      if (i == 254) break;
    }
    tmp[i] = '\0';
    if (!strcmp(tmp, "NAN")) return sNAN;
    else if (!strcmp(tmp, "SNAN")) return sNAN;
    else if (!strcmp(tmp, "QNAN")) return qNAN;
    else val = gasneti_parse_int(tmp, 0);
    if (val <= 0xFF) {
      int i;
      uint64_t byte = val;
      for (i = 0; i < 7; i++) {
        val = (val << 8) | byte;
      }
    }
    return val;
  }
  static void gasneti_memalloc_valset(void *p, size_t len, uint64_t val) {
    uint64_t *output = p;
    size_t blocks = len/8;
    size_t extra = len%8;
    size_t i;
    for (i = 0; i < blocks; i++) {
      *output = val; 
      output++;
    }
    if (extra) memcpy(output, &val, extra);
  }
  static const void *gasneti_memalloc_valcmp(const void *p, size_t len, uint64_t val) {
    const uint64_t *input = p;
    size_t blocks = len/8;
    size_t extra = len%8;
    size_t i;
    for (i = 0; i < blocks; i++) {
      if (*input != val) {
        const uint8_t *in = (uint8_t *)input;
        const uint8_t *cmp = (uint8_t *)&val;
        for (i = 0; i < 8; i++, in++, cmp++)
          if (*in != *cmp) return in;
        gasneti_fatalerror("bizarre failure in gasneti_memalloc_valcmp");
      }
      input++;
    }
    if (extra) {
      const uint8_t *in = (uint8_t *)input;
      const uint8_t *cmp = (uint8_t *)&val;
      for (i = 0; i < extra; i++, in++, cmp++)
        if (*in != *cmp) return in;
    }
    return NULL;
  }

  GASNETI_INLINE(gasneti_memalloc_envinit)
  void gasneti_memalloc_envinit() {
    if (!gasneti_memalloc_envisinit) {
      gasneti_mutex_lock(&gasneti_memalloc_lock);
        if (!gasneti_memalloc_envisinit && gasneti_init_done) {
          gasneti_memalloc_envisinit = 1; /* set first, because getenv might call malloc when tracing */
          gasneti_memalloc_init =       gasneti_getenv_yesno_withdefault("GASNET_MALLOC_INIT",0);
          gasneti_memalloc_initval =    gasneti_memalloc_envint("GASNET_MALLOC_INITVAL","NAN");
          gasneti_memalloc_clobber =    gasneti_getenv_yesno_withdefault("GASNET_MALLOC_CLOBBER",0);
          gasneti_memalloc_clobberval = gasneti_memalloc_envint("GASNET_MALLOC_CLOBBERVAL","NAN");
          gasneti_memalloc_leakall =    gasneti_getenv_yesno_withdefault("GASNET_MALLOC_LEAKALL", 0);
          gasneti_memalloc_scanfreed =  gasneti_getenv_yesno_withdefault("GASNET_MALLOC_SCANFREED", 0);
          gasneti_memalloc_extracheck = gasneti_getenv_yesno_withdefault("GASNET_MALLOC_EXTRACHECK", 0);
          if (gasneti_memalloc_scanfreed && !gasneti_memalloc_clobber) {
            gasneti_memalloc_clobber = 1;
            if (gasneti_mynode == 0) { 
              fprintf(stderr, "WARNING: GASNET_MALLOC_SCANFREED requires GASNET_MALLOC_CLOBBER: enabling it.\n");
              fflush(stderr);
            }
          }
          if (gasneti_memalloc_scanfreed && !gasneti_memalloc_leakall) {
            gasneti_memalloc_leakall = 1;
            if (gasneti_mynode == 0) { 
              fprintf(stderr, "WARNING: GASNET_MALLOC_SCANFREED requires GASNET_MALLOC_LEAKALL: enabling it.\n");
              fflush(stderr);
            }
          }
        }
      gasneti_mutex_unlock(&gasneti_memalloc_lock);
    }
  }

  extern void _gasneti_memcheck_one(const char *curloc) {
    if (gasneti_memalloc_extracheck) _gasneti_memcheck_all(curloc);
    else {
      if_pt (gasneti_attach_done) gasnet_hold_interrupts();
      gasneti_mutex_lock(&gasneti_memalloc_lock);
        if (gasneti_memalloc_pos) {
          _gasneti_memcheck(gasneti_memalloc_pos+1, curloc, 2);
          gasneti_memalloc_pos = gasneti_memalloc_pos->nextdesc;
        } else gasneti_assert(gasneti_memalloc_ringobjects == 0 && gasneti_memalloc_ringbytes == 0);
      gasneti_mutex_unlock(&gasneti_memalloc_lock);
      if_pt (gasneti_attach_done) gasnet_resume_interrupts();
    }
  }
  extern void _gasneti_memcheck_all(const char *curloc) {
    if_pt (gasneti_attach_done) gasnet_hold_interrupts();
    gasneti_mutex_lock(&gasneti_memalloc_lock);
      if (gasneti_memalloc_pos) {
        gasneti_memalloc_desc_t *begin = gasneti_memalloc_pos;
        uint64_t cnt;
        uint64_t sumsz = 0;
        for (cnt=0; cnt < gasneti_memalloc_ringobjects; cnt++) {
          sumsz += _gasneti_memcheck(gasneti_memalloc_pos+1, curloc, 2);
          gasneti_memalloc_pos = gasneti_memalloc_pos->nextdesc;
          if (gasneti_memalloc_pos == begin) break;
        } 
        if (cnt+1 != gasneti_memalloc_ringobjects || gasneti_memalloc_pos != begin || 
            sumsz != gasneti_memalloc_ringbytes) {
          gasneti_fatalerror("Debug malloc memcheck_all (called at %s) detected an error "
                             "in the memory ring linkage, most likely as a result of memory corruption.", 
                             curloc);
        }
      } else gasneti_assert(gasneti_memalloc_ringobjects == 0 && gasneti_memalloc_ringbytes == 0);
    gasneti_mutex_unlock(&gasneti_memalloc_lock);
    if_pt (gasneti_attach_done) gasnet_resume_interrupts();
  }

  /* assert the integrity of given memory block and return size of the user object 
      checktype == 0: check a live object
      checktype == 1: check an object which is about to be freed
      checktype == 2: check an object which resides in the ring (and may be dead)
  */
  extern size_t _gasneti_memcheck(void *ptr, const char *curloc, int checktype) {
    const char *corruptstr = NULL;
    char tmpstr[255];
    size_t nbytes = 0;
    char *allocptr = NULL;
    uint64_t beginpost = 0;
    uint64_t endpost = 0;
    int doscan = 0;
    gasneti_assert(checktype >= 0 && checktype <= 2);
    if (gasneti_looksaligned(ptr)) {
      gasneti_memalloc_desc_t *desc = ((gasneti_memalloc_desc_t *)ptr) - 1;
      beginpost = desc->beginpost;
      nbytes = (size_t)desc->datasz;
      if (nbytes == 0 || nbytes > gasneti_memalloc_maxobjectsize || 
          ((uintptr_t)ptr)+nbytes > gasneti_memalloc_maxobjectloc ||
          !desc->prevdesc || !desc->nextdesc || 
          !gasneti_looksaligned(desc->prevdesc) || 
          !gasneti_looksaligned(desc->nextdesc)) {
            nbytes = 0; /* bad metadata, don't trust any of it */
      } else {
        allocptr = (void *)(uintptr_t)desc->allocdesc;
        memcpy(&endpost,((char*)ptr)+nbytes,GASNETI_MEM_TAILSZ);
      }
    }
    if (beginpost == GASNETI_MEM_FREEMARK) {
      switch (checktype) {
        case 0: /* should be a live object */
          corruptstr = "Debug malloc memcheck() called on freed memory (may indicate local heap corruption)";
          break;
        case 1: /* about to be freed - should still be a live object */
          corruptstr = "Debug free detected a duplicate free() or local heap corruption";
          break;
        case 2:
          if (gasneti_memalloc_scanfreed <= 0) /* freed objects should not be in ring */
            corruptstr = "Debug malloc found a freed object in the memory ring, indicating local heap corruption";
          else doscan = 1;
          break;
      }
    }  
    if (beginpost != GASNETI_MEM_FREEMARK && 
        (beginpost != GASNETI_MEM_BEGINPOST || endpost != GASNETI_MEM_ENDPOST)) {
      const char *diagnosis = "a bad pointer or local heap corruption";
      if (nbytes && beginpost == GASNETI_MEM_BEGINPOST && endpost != GASNETI_MEM_ENDPOST)
        diagnosis = "local heap corruption (probable buffer overflow)";
      else if (nbytes && beginpost != GASNETI_MEM_BEGINPOST && endpost == GASNETI_MEM_ENDPOST)
        diagnosis = "local heap corruption (probable buffer underflow)";
      if (checktype == 1) {
        sprintf(tmpstr, "Debug free detected %s", diagnosis);
      } else {
        sprintf(tmpstr, "Debug malloc memcheck() detected %s", diagnosis);
      }
      corruptstr = tmpstr;
    }
    if (corruptstr == NULL && doscan) {
      const void *badloc = gasneti_memalloc_valcmp(ptr, nbytes, gasneti_memalloc_clobberval);
      if (badloc) {
        sprintf(tmpstr, "Debug malloc memcheck() detected a write to freed memory at object offset: %i bytes",
                        (int)((uintptr_t)badloc - (uintptr_t)ptr));
        corruptstr = tmpstr;
      }
    }

    if (corruptstr != NULL) {
      char nbytesstr[80];
      if (allocptr != NULL && memchr(allocptr,'\0',255) == 0) /* allocptr may be bad */
        allocptr = NULL; 
      if (allocptr == NULL) nbytesstr[0] = '\0';
      else sprintf(nbytesstr," nbytes=%i",(int)nbytes);
      gasneti_fatalerror("%s\n   ptr="GASNETI_LADDRFMT"%s%s%s%s%s",
           corruptstr,
           GASNETI_LADDRSTR(ptr), nbytesstr,
           (allocptr!=NULL?"\n   allocated at: ":""), (allocptr!=NULL?allocptr:""),
           (curloc!=NULL?(checktype == 1?"\n   freed at: ":"\n   detected at: "):""), 
           (curloc!=NULL?curloc:"")
           );
    }
    return nbytes;
  }

  /* get access to system malloc/free */
  #undef malloc
  #undef free
  static void *_gasneti_malloc_inner(int allowfail, size_t nbytes, const char *curloc) {
    void *ret = NULL;
    gasneti_memalloc_envinit();
    _gasneti_memcheck_one(curloc);
    GASNETI_STAT_EVENT_VAL(I, GASNET_MALLOC, nbytes);
    if_pt (gasneti_attach_done) gasnet_hold_interrupts();
    if_pf (nbytes == 0) {
      if_pt (gasneti_attach_done) gasnet_resume_interrupts();
      return NULL;
    }
    ret = malloc(nbytes+GASNETI_MEM_EXTRASZ);
    gasneti_assert((((uintptr_t)ret) & 0x3) == 0); /* should have at least 4-byte alignment */
    if_pf (ret == NULL) {
      if (allowfail) {
        if_pt (gasneti_attach_done) gasnet_resume_interrupts();
        GASNETI_TRACE_PRINTF(I,("Warning: returning NULL for a failed gasneti_malloc(%i): %s",
                                (int)nbytes, (curloc == NULL ? (const char *)"" : curloc)));
        return NULL;
      }
      gasneti_fatalerror("Debug malloc(%d) failed (%lu bytes in use, in %lu objects): %s", 
        (int)nbytes, (unsigned long)(gasneti_memalloc_allocatedbytes - gasneti_memalloc_freedbytes),
                     (unsigned long)(gasneti_memalloc_allocatedobjects - gasneti_memalloc_freedobjects),
                     (curloc == NULL ? (const char *)"" : curloc));
    } else {
      uint64_t gasneti_endpost_ref = GASNETI_MEM_ENDPOST;
      gasneti_memalloc_desc_t *desc = ret;
      desc->allocdesc = (uint64_t)(uintptr_t)curloc;
      desc->datasz = (uint64_t)nbytes;
      desc->beginpost = GASNETI_MEM_BEGINPOST;
      memcpy(((char*)ret)+nbytes+GASNETI_MEM_HEADERSZ, &gasneti_endpost_ref, GASNETI_MEM_TAILSZ);

      gasneti_mutex_lock(&gasneti_memalloc_lock);
        gasneti_memalloc_allocatedbytes += nbytes;
        gasneti_memalloc_allocatedobjects++;
        gasneti_memalloc_ringobjects++;
        gasneti_memalloc_ringbytes += nbytes;
        if (nbytes > gasneti_memalloc_maxobjectsize) gasneti_memalloc_maxobjectsize = nbytes;
        if (((uintptr_t)ret)+nbytes+GASNETI_MEM_HEADERSZ > gasneti_memalloc_maxobjectloc) 
          gasneti_memalloc_maxobjectloc = ((uintptr_t)ret)+nbytes+GASNETI_MEM_HEADERSZ;
        gasneti_memalloc_maxlivebytes = 
          MAX(gasneti_memalloc_maxlivebytes, gasneti_memalloc_allocatedbytes-gasneti_memalloc_freedbytes);
        gasneti_memalloc_maxliveobjects = 
          MAX(gasneti_memalloc_maxliveobjects, gasneti_memalloc_allocatedobjects-gasneti_memalloc_freedobjects);
        if (gasneti_memalloc_pos == NULL) { /* first object */
          gasneti_memalloc_pos = desc;
          desc->prevdesc = desc;
          desc->nextdesc = desc;
        } else { /* link into ring */
          desc->prevdesc = gasneti_memalloc_pos->prevdesc;
          desc->nextdesc = gasneti_memalloc_pos;
          gasneti_memalloc_pos->prevdesc->nextdesc = desc;
          gasneti_memalloc_pos->prevdesc = desc;
        }
      gasneti_mutex_unlock(&gasneti_memalloc_lock);

      ret = desc+1;
      if (gasneti_memalloc_init > 0) gasneti_memalloc_valset(ret, nbytes, gasneti_memalloc_initval);
    }
    if_pt (gasneti_attach_done) gasnet_resume_interrupts();
    _gasneti_memcheck(ret,curloc,0);
    return ret;
  }
  extern void *_gasneti_malloc(size_t nbytes, const char *curloc) {
    return _gasneti_malloc_inner(0, nbytes, curloc);
  }
  extern void *_gasneti_malloc_allowfail(size_t nbytes, const char *curloc) {
    return _gasneti_malloc_inner(1, nbytes, curloc);
  }

  extern void _gasneti_free(void *ptr, const char *curloc) {
    size_t nbytes;
    gasneti_memalloc_desc_t *desc;
    gasneti_memalloc_envinit();
    _gasneti_memcheck_one(curloc);
    if_pf (ptr == NULL) return;
    if_pt (gasneti_attach_done) gasnet_hold_interrupts();
    nbytes = _gasneti_memcheck(ptr, curloc, 1);
    GASNETI_STAT_EVENT_VAL(I, GASNET_FREE, nbytes);
    desc = ((gasneti_memalloc_desc_t *)ptr) - 1;
    if (gasneti_memalloc_clobber > 0) gasneti_memalloc_valset(desc+1, nbytes, gasneti_memalloc_clobberval);

    gasneti_mutex_lock(&gasneti_memalloc_lock);
      desc->beginpost = GASNETI_MEM_FREEMARK;
      gasneti_memalloc_freedbytes += nbytes;
      gasneti_memalloc_freedobjects++;
      if (gasneti_memalloc_scanfreed <= 0) {
        gasneti_memalloc_ringobjects--;
        gasneti_memalloc_ringbytes -= nbytes;
        if (desc->nextdesc == desc) { /* last item in list */
          gasneti_assert(desc->prevdesc == desc && gasneti_memalloc_ringobjects == 0);
          gasneti_memalloc_pos = NULL;
        } else {
          if (gasneti_memalloc_pos == desc) gasneti_memalloc_pos = desc->nextdesc;
          desc->prevdesc->nextdesc = desc->nextdesc;
          desc->nextdesc->prevdesc = desc->prevdesc;
        }
      }
    gasneti_mutex_unlock(&gasneti_memalloc_lock);

    if (gasneti_memalloc_leakall <= 0) free(desc);
    if_pt (gasneti_attach_done) gasnet_resume_interrupts();
  }

  extern void *_gasneti_calloc(size_t N, size_t S, const char *curloc) {
    void *ret;
    size_t nbytes = N*S;
    if_pf (nbytes == 0) return NULL;
    ret = _gasneti_malloc(nbytes, curloc);
    memset(ret,0,nbytes);
    _gasneti_memcheck(ret,curloc,0);
    return ret;
  }
  extern void *_gasneti_realloc(void *ptr, size_t sz, const char *curloc) {
    void *ret = _gasneti_malloc(sz, curloc);
    if_pt (ptr != NULL) {
      size_t nbytes = _gasneti_memcheck(ptr, curloc, 0);
      memcpy(ret, ptr, MIN(nbytes, sz));
      _gasneti_free(ptr, curloc);
    }
    _gasneti_memcheck(ret,curloc,0);
    return ret;
  }
  extern int gasneti_getheapstats(gasneti_heapstats_t *pstat) {
    pstat->allocated_bytes = gasneti_memalloc_allocatedbytes;
    pstat->freed_bytes = gasneti_memalloc_freedbytes;
    pstat->live_bytes = gasneti_memalloc_allocatedbytes - gasneti_memalloc_freedbytes;
    pstat->live_bytes_max = gasneti_memalloc_maxlivebytes;
    pstat->allocated_objects = gasneti_memalloc_allocatedobjects;
    pstat->freed_objects = gasneti_memalloc_freedobjects;
    pstat->live_objects = gasneti_memalloc_allocatedobjects - gasneti_memalloc_freedobjects;
    pstat->live_objects_max = gasneti_memalloc_maxliveobjects;
    pstat->overhead_bytes = gasneti_memalloc_ringbytes - pstat->live_bytes + 
                            gasneti_memalloc_ringobjects*GASNETI_MEM_EXTRASZ;
    return 0;
  }
#endif
/* extern versions of gasnet malloc fns for use in public headers */
extern void *_gasneti_extern_malloc(size_t sz GASNETI_CURLOCFARG) {
  return _gasneti_malloc(sz GASNETI_CURLOCPARG);
}
extern void *_gasneti_extern_realloc(void *ptr, size_t sz GASNETI_CURLOCFARG) {
  return _gasneti_realloc(ptr, sz GASNETI_CURLOCPARG);
}
extern void *_gasneti_extern_calloc(size_t N, size_t S GASNETI_CURLOCFARG) {
  return _gasneti_calloc(N,S GASNETI_CURLOCPARG);
}
extern void _gasneti_extern_free(void *ptr GASNETI_CURLOCFARG) {
  _gasneti_free(ptr GASNETI_CURLOCPARG);
}
extern char *_gasneti_extern_strdup(const char *s GASNETI_CURLOCFARG) {
  return _gasneti_strdup(s GASNETI_CURLOCPARG);
}
extern char *_gasneti_extern_strndup(const char *s, size_t n GASNETI_CURLOCFARG) {
  return _gasneti_strndup(s,n GASNETI_CURLOCPARG);
}

#if GASNET_DEBUG
  extern void *(*gasnett_debug_malloc_fn)(size_t sz GASNETI_CURLOCFARG);
  extern void *(*gasnett_debug_calloc_fn)(size_t N, size_t S GASNETI_CURLOCFARG);
  extern void (*gasnett_debug_free_fn)(void *ptr GASNETI_CURLOCFARG);
  void *(*gasnett_debug_malloc_fn)(size_t sz GASNETI_CURLOCFARG) =
         &_gasneti_extern_malloc;
  void *(*gasnett_debug_calloc_fn)(size_t N, size_t S GASNETI_CURLOCFARG) =
         &_gasneti_extern_calloc;
  void (*gasnett_debug_free_fn)(void *ptr GASNETI_CURLOCFARG) =
         &_gasneti_extern_free;
#endif

/* don't put anything here - malloc stuff must come last */

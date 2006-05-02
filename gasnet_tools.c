/*   $Source: /Users/kamil/work/gasnet-cvs2/gasnet/gasnet_tools.c,v $
 *     $Date: 2006/05/01 21:02:42 $
 * $Revision: 1.161 $
 * Description: GASNet implementation of internal helpers
 * Copyright 2002, Dan Bonachea <bonachea@cs.berkeley.edu>
 * Terms of use are as specified in license.txt
 */

#if defined(GASNET_PARSYNC) || defined(GASNET_PAR) 
  #define GASNETT_THREAD_SAFE 1
#endif
#undef GASNET_SEQ
#undef GASNET_PAR
#undef GASNET_PARSYNC

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
#include <time.h> /* gasneti_gettimeofday_us */
#include <sys/time.h> /* gasneti_gettimeofday_us */
#include <signal.h>

#ifdef IRIX
#define signal(a,b) bsd_signal(a,b)
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

/* ------------------------------------------------------------------------------------ */
/* generic atomics support */
#ifdef GASNETI_USE_GENERIC_ATOMICOPS
  #if defined(_REENTRANT) || defined(_THREAD_SAFE) || \
        defined(PTHREAD_MUTEX_INITIALIZER) ||           \
        defined(HAVE_PTHREAD) || defined(HAVE_PTHREAD_H)
    pthread_mutex_t gasneti_atomicop_mutex = PTHREAD_MUTEX_INITIALIZER;
  #endif
#endif

/* ------------------------------------------------------------------------------------ */
/* call-based atomic support for C compilers with limited inline assembly */

#ifdef GASNETI_ATOMIC_SPECIALS
  GASNETI_ATOMIC_SPECIALS
#endif

/* ------------------------------------------------------------------------------------ */
/* call-based membar/atomic support for C++ compilers which lack inline assembly */
#if defined(GASNETI_USING_SLOW_ATOMICS) || \
    defined(GASNETI_USING_SLOW_MEMBARS) || \
    defined(GASNETI_USING_SLOW_TIMERS)
#error gasnet_tools.c must be compiled with support for inline assembly
#endif

#ifdef GASNETI_TICKS_NOW_BODY
  GASNETI_SPECIAL_ASM_DEFN(gasneti_slow_ticks_now, GASNETI_TICKS_NOW_BODY)
#else
  extern gasneti_tick_t gasneti_slow_ticks_now() {
    return gasneti_ticks_now();
  }
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
/* ident strings and idiot checks */
#define GASNETT_THREADMODEL_STR _STRINGIFY(GASNETT_THREAD_MODEL)
GASNETI_IDENT(gasnett_IdentString_ThreadModel, "$GASNetToolsThreadModel: " GASNETT_THREADMODEL_STR " $");

GASNETI_IDENT(gasnett_IdentString_Config, "$GASNetToolsConfig: " GASNETT_CONFIG_STRING " $");

GASNETI_IDENT(gasnett_IdentString_BuildTimestamp, 
             "$GASNetBuildTimestamp: " __DATE__ " " __TIME__ " $");

GASNETI_IDENT(gasnett_IdentString_BuildID, 
             "$GASNetBuildId: " GASNETI_BUILD_ID " $");
GASNETI_IDENT(gasnett_IdentString_ConfigureArgs, 
             "$GASNetConfigureArgs: " GASNETI_CONFIGURE_ARGS " $");
GASNETI_IDENT(gasnett_IdentString_SystemTuple, 
             "$GASNetSystemTuple: " GASNETI_SYSTEM_TUPLE " $");
GASNETI_IDENT(gasnett_IdentString_SystemName, 
             "$GASNetSystemName: " GASNETI_SYSTEM_NAME " $");

int GASNETT_LINKCONFIG_IDIOTCHECK(GASNETT_THREADMODEL) = 1;
int GASNETT_LINKCONFIG_IDIOTCHECK(GASNETT_DEBUG_CONFIG) = 1;
int GASNETT_LINKCONFIG_IDIOTCHECK(GASNETT_PTR_CONFIG) = 1;
int GASNETT_LINKCONFIG_IDIOTCHECK(GASNETT_TIMER_CONFIG) = 1;
int GASNETT_LINKCONFIG_IDIOTCHECK(GASNETT_ATOMIC_CONFIG) = 1;

/* ------------------------------------------------------------------------------------ */
/* timer support */

extern uint64_t gasneti_gettimeofday_us(void) {
  uint64_t retval;
  struct timeval tv;
  #ifdef __crayx1
  retry:
  #endif
  gasneti_assert_zeroret(gettimeofday(&tv, NULL));
  retval = ((uint64_t)tv.tv_sec) * 1000000 + (uint64_t)tv.tv_usec;
  #ifdef __crayx1
    /* fix an empirically observed bug in UNICOS gettimeofday(),
       which occasionally returns ridiculously incorrect values
       SPR 728120, fixed in kernel 2.4.34 
     */
    if_pf(retval < (((uint64_t)3) << 48)) goto retry;
  #endif
  return retval;
}

extern double gasneti_tick_metric(int idx) {
  static double *_gasneti_tick_metric = NULL;
  gasneti_assert(idx <= 1);
  if_pf (_gasneti_tick_metric == NULL) {
    int i, ticks, iters = 1000, minticks = 10;
    double *_tmp_metric;
    gasneti_tick_t min = GASNETI_TICK_MAX;
    gasneti_tick_t start = gasneti_ticks_now();
    gasneti_tick_t last = start;
    for (i=0,ticks=0; i < iters || ticks < minticks; i++) {
      gasneti_tick_t x = gasneti_ticks_now();
      gasneti_tick_t curr = (x - last);
      if_pt (curr > 0) { 
        ticks++;
        if_pf (curr < min) min = curr;
      }
      last = x;
    }
    _tmp_metric = (double *)malloc(2*sizeof(double));
    gasneti_assert(_tmp_metric != NULL);
    /* granularity */
    _tmp_metric[0] = ((double)gasneti_ticks_to_ns(min))/1000.0;
    /* overhead */
    _tmp_metric[1] = ((double)(gasneti_ticks_to_ns(last - start)))/(i*1000.0);
    gasneti_sync_writes();
    _gasneti_tick_metric = _tmp_metric;
  } else gasneti_sync_reads();
  return _gasneti_tick_metric[idx];
}
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
  fsync(STDOUT_FILENO); /* ignore errors for output is a console */
  fsync(STDERR_FILENO); /* ignore errors for output is a console */
  #ifndef __LIBCATAMOUNT__
    sync();
  #endif
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
extern gasneti_sighandlerfn_t gasneti_reghandler(int sigtocatch, gasneti_sighandlerfn_t fp) {
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
/* ------------------------------------------------------------------------------------ */
extern uint64_t gasneti_checksum(void *p, int numbytes) {
 uint8_t *buf = (uint8_t *)p;
 uint64_t result = 0;
 int i;
 for (i=0;i<numbytes;i++) {
   result = ((result << 8) | ((result >> 56) & 0xFF) ) ^ *buf;
   buf++;
 }
 return result;
}
/* ------------------------------------------------------------------------------------ */
extern int gasneti_isLittleEndian() {
  union {
    int i;                  /* machine word */
    unsigned char b[sizeof(int)];    /* b[0] overlaid with first byte of i */
  } x;
  x.i = 0xFF;    /* set lsb, zero all others */
  return x.b[0] == 0xFF;
}
/* ------------------------------------------------------------------------------------ */
/* build a code-location string */
extern char *gasneti_build_loc_str(const char *funcname, const char *filename, int linenum) {
  int sz;
  char *loc;
  int fnlen;
  if (!funcname) funcname = "";
  if (!filename) filename = "*unknown file*";
  fnlen = strlen(funcname);
  sz = fnlen + strlen(filename) + 20;
  loc = malloc(sz);
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
/* number/size formatting */
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
    if (*neg) val = -val;
    snprintf(buf, bufsz, "0x%llx", (unsigned long long)val);
  } else gasneti_fatalerror("internal error in gasneti_format_number");
  return buf;
}
/* ------------------------------------------------------------------------------------ */
/* environment support */
/* set an environment variable, for the local process ONLY */
extern void gasneti_setenv(const char *key, const char *value) {
  /* both are POSIX - prefer setenv because it manages memory for us */
  #if HAVE_SETENV
    int retval = setenv(key, value, 1);
    if (retval) gasneti_fatalerror("Failed to setenv(\"%s\",\"%s\",1) in gasneti_setenv => %s(%i)",
                                     key, value, strerror(errno), errno);
  #elif HAVE_PUTENV 
    char *tmp = malloc(strlen(key) + strlen(value) + 2);
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
    char *tmp = malloc(strlen(key) + 2);
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
/* Physical CPU query */
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
        gasneti_assert_zeroret(sysctl(mib, 2, &hwprocs, &len, NULL, 0));
        if (hwprocs < 1) hwprocs = 0;
      }
  #elif defined(HPUX) 
      {
        struct pst_dynamic psd;
        gasneti_assert_zeroret(pstat_getdynamic(&psd, sizeof(psd), (size_t)1, 0) == -1);
        hwprocs = psd.psd_proc_cnt;
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
/* Physical memory query */
#ifdef _SC_PHYS_PAGES
  /* if the sysconf exists, try to use it */
  static uint64_t _gasneti_getPhysMemSysconf(void) {
    long pages = sysconf(_SC_PHYS_PAGES);
    if (pages < 0) pages = 0;
    return (((uint64_t)pages)*GASNET_PAGESIZE);
  }
#else
  #define _gasneti_getPhysMemSysconf() 0
#endif
#if defined(__APPLE__) || defined(__FreeBSD__)
  #include <sys/types.h>
  #include <sys/sysctl.h>
#endif
extern uint64_t gasneti_getPhysMemSz(int failureIsFatal) {
  uint64_t retval = _gasneti_getPhysMemSysconf();
  if (retval) return retval;
  #if defined(__linux__) && !defined(__LIBCATAMOUNT__)
    #define _BUFSZ        120
    { FILE *fp;
      char line[_BUFSZ+1];

      if ((fp = fopen("/proc/meminfo", "r")) == NULL)
        gasneti_fatalerror("Failed to open /proc/meminfo in gasneti_getPhysMemSz()");

      while (fgets(line, _BUFSZ, fp)) {
        unsigned long memul = 0;
        unsigned long long memull = 0;
        /* MemTotal: on 2.4 and 2.6 kernels - preferred because less chance of scanf overflow */
        if (sscanf(line, "MemTotal: %lu kB", &memul) > 0 && memul > 0) {
          retval = ((uint64_t)memul) * 1024;
        }
        /* Mem: only on 2.4 kernels */
        else if (sscanf(line, "Mem: %llu", &memull) > 0 && memull > 0 && !retval) {
          retval = (uint64_t)memull;
        }
      }
      fclose(fp);
    }
    #undef _BUFSZ
  #elif defined(__APPLE__) || defined(__FreeBSD__)
    { /* see "man 3 sysctl" */    
      int mib[2];
      size_t len = 0;
      mib[0] = CTL_HW;
      mib[1] = HW_PHYSMEM;
      sysctl(mib, 2, NULL, &len, NULL, 0);
      switch (len) { /* accomodate both 32 and 64-bit systems */
        case 4: { 
          uint32_t retval32 = 0;
          if (sysctl(mib, 2, &retval32, &len, NULL, 0)) 
            gasneti_fatalerror("sysctl(CTL_HW.HW_PHYSMEM) failed: %s(%i)",strerror(errno),errno);
          if (retval32) retval = (uint64_t)retval32;
          break;
        }
        case 8:
          if (sysctl(mib, 2, &retval, &len, NULL, 0)) 
            gasneti_fatalerror("sysctl(CTL_HW.HW_PHYSMEM) failed: %s(%i)",strerror(errno),errno);
          break;
        default:
          gasneti_fatalerror("sysctl(CTL_HW.HW_PHYSMEM) failed to get required size, got len=%i: %s(%i)",
            (int)len, strerror(errno), errno);
      }
    }
  #elif defined(_AIX)
    { /* returns amount of real memory in kilobytes */
      long int val = sysconf(_SC_AIX_REALMEM);
      if (val > 0) retval = (1024 * (uint64_t)val);
    }
  #else  /* unknown OS */
    { }
  #endif

  if (!retval && failureIsFatal) 
    gasneti_fatalerror("Failed to determine physical memory size in gasneti_getPhysMemSz()");
  return retval;
}
/* ------------------------------------------------------------------------------------ */
/* CPU affinity control */
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
	mask = calloc(len, sizeof(*mask));
	mask[local_rank / bits_per_long] = 1 << (local_rank % bits_per_long);
        gasneti_assert_zeroret(sched_setaffinity(0, len*sizeof(*mask), mask));
	free(mask);
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
  GASNETT_TRACE_PRINTF("gasnett_set_affinity(%d)", rank);
  GASNETC_SET_AFFINITY(rank);
}
/* ------------------------------------------------------------------------------------ */

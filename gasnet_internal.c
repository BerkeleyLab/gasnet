/*  $Archive:: /Ti/GASNet/gasnet_internal.c                               $
 *     $Date: 2004/01/05 05:01:10 $
 * $Revision: 1.45 $
 * Description: GASNet implementation of internal helpers
 * Copyright 2002, Dan Bonachea <bonachea@cs.berkeley.edu>
 * Terms of use are as specified in license.txt
 */

#include <time.h>
#include <sys/time.h>
#include <unistd.h>
#include <stdarg.h>
#include <stdio.h>
#include <ctype.h>
#include <errno.h>

#include <signal.h>
#ifdef IRIX
#define signal(a,b) bsd_signal(a,b)
#endif

#include <gasnet.h>
#include <gasnet_internal.h>

/* set to non-zero for verbose error reporting */
int gasneti_VerboseErrors = 1;

#ifdef GASNETI_USE_GENERIC_ATOMICOPS
  gasnet_hsl_t gasneti_atomicop_lock = GASNET_HSL_INITIALIZER;
#endif

#define GASNET_VERSION_STR  _STRINGIFY(GASNET_VERSION)
GASNETI_IDENT(gasneti_IdentString_APIVersion, "$GASNetAPIVersion: " GASNET_VERSION_STR " $");

#define GASNETI_THREADMODEL_STR _STRINGIFY(GASNETI_THREADMODEL)
GASNETI_IDENT(gasneti_IdentString_ThreadModel, "$GASNetThreadModel: GASNET_" GASNETI_THREADMODEL_STR " $");

#define GASNETI_SEGMENT_CONFIG_STR _STRINGIFY(GASNETI_SEGMENT_CONFIG)
GASNETI_IDENT(gasneti_IdentString_SegConfig, "$GASNetSegment: GASNET_SEGMENT_" GASNETI_SEGMENT_CONFIG_STR " $");

/* embed a string with complete configuration info to support versioning checks */
GASNETI_IDENT(gasneti_IdentString_libraryConfig, "$GASNetConfig: (libgasnet.a) " GASNET_CONFIG_STRING " $");


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

/* ------------------------------------------------------------------------------------ */
extern void gasneti_fatalerror(const char *msg, ...) {
  va_list argptr;
  char expandedmsg[255];

  strcpy(expandedmsg, "*** GASNet FATAL ERROR: ");
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
  abort();
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
  gethostname(name, 255);
  fprintf(stderr,"GASNet node frozen for debugger: host=%s  pid=%i\n"
                 "To unfreeze, attach a debugger and set 'gasnet_frozen' to 0, or send a "
                 GASNETI_UNFREEZE_SIGNAL_STR "\n", 
                 name, (int)getpid()); 
  fflush(stderr);
  _freezeForDebugger(0);
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

extern char *gasneti_getenv(const char *keyname) {
  char *retval = NULL;

  if (keyname && gasneti_globalEnv) { 
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
  
  GASNETI_TRACE_PRINTF(I,("gasnet_getenv(%s) => '%s'",
                          (keyname?keyname:"NULL"),(retval?retval:"NULL")));

  return retval;
}

/* ------------------------------------------------------------------------------------ */
/* GASNet Tracing and Statistics */

#if GASNET_TRACE
  GASNETI_IDENT(gasneti_IdentString_trace, "$GASNetTracingEnabled: 1 $");
#endif
#if GASNET_STATS
  GASNETI_IDENT(gasneti_IdentString_stats, "$GASNetStatisticsEnabled: 1 $");
#endif

static gasneti_mutex_t gasneti_tracelock = GASNETI_MUTEX_INITIALIZER;
char gasneti_tracetypes[256];
char gasneti_statstypes[256];
FILE *gasneti_tracefile = NULL;
FILE *gasneti_statsfile = NULL;
gasneti_stattime_t starttime;

#if GASNETI_STATS_OR_TRACE
  #define BUILD_STATS(type,name,desc) { #type, #name, #desc },
  gasneti_statinfo_t gasneti_stats[] = {
    GASNETI_ALL_STATS(BUILD_STATS, BUILD_STATS, BUILD_STATS)
    {NULL, NULL, NULL}
  };

  #define BUFSZ     1024
  #define NUMBUFS   32
  static char gasneti_printbufs[NUMBUFS][BUFSZ];
  static int gasneti_curbuf = 0;
  static gasneti_mutex_t gasneti_buflock = GASNETI_MUTEX_INITIALIZER;

  /* give gcc enough information to type-check our format strings */
  static void gasneti_file_vprintf(FILE *fp, const char *format, va_list argptr) __attribute__((__format__ (__printf__, 2, 0)));
  static void gasneti_trace_printf(const char *format, ...) __attribute__((__format__ (__printf__, 1, 2)));
  static void gasneti_stats_printf(const char *format, ...) __attribute__((__format__ (__printf__, 1, 2)));
  static void gasneti_tracestats_printf(const char *format, ...) __attribute__((__format__ (__printf__, 1, 2)));

  static char *gasneti_getbuf() {
    int bufidx;

    gasneti_mutex_lock(&gasneti_buflock);

    bufidx = gasneti_curbuf;
    gasneti_curbuf = (gasneti_curbuf + 1) % NUMBUFS;

    gasneti_mutex_unlock(&gasneti_buflock);
    return gasneti_printbufs[bufidx];
  }

  /* format and return a string result
     caller should not deallocate string, they are recycled automatically
  */
  extern char *gasneti_dynsprintf(const char *format, ...) {
    va_list argptr;
    char *output = gasneti_getbuf();

    va_start(argptr, format); /*  pass in last argument */
      { int sz = vsnprintf(output, BUFSZ, format, argptr);
        gasneti_assert(sz <= BUFSZ);
        if (sz >= (BUFSZ-5)) strcpy(output+(BUFSZ-5),"...");
      }
    va_end(argptr);
    return output;
  }

  #define BYTES_PER_LINE 16
  #define MAX_LINES 10
  /* format a block of data into a string and return it - 
     caller should not deallocate string, they are recycled automatically
   */
  extern char *gasneti_formatdata(void *p, int nbytes) { 
    uint8_t *data = (uint8_t *)p;
    char *output = gasneti_getbuf();
    *output = '\0';
    if (nbytes <= 8) { /* fits on one line */
      int i;
      for (i=0; i < nbytes; i++) {
        char temp[5];
        sprintf(temp,"%02x ",(int)data[i]);
        strcat(output, temp);
      }
    } else {
      int line;
      int col;
      int byteidx = 0;
      strcat(output,"\n");
      for (line=0;line<MAX_LINES && byteidx<nbytes;line++) {
        char nicefmt[BYTES_PER_LINE+1];
        char lineheader[10];
        nicefmt[0] = '\0';
        sprintf(lineheader, "  0x%-2x:  ", byteidx);
        strcat(output, lineheader);
        for (col=0;col<BYTES_PER_LINE && byteidx<nbytes;col++) {
          char temp[5];
          sprintf(temp,"%02x ",(int)data[byteidx]);
          strcat(output, temp);
          if (isprint(data[byteidx])) nicefmt[col] = data[byteidx];
          else nicefmt[col] = '.';
          byteidx++;
        }
        nicefmt[col] = '\0';
        for(;col < BYTES_PER_LINE; col++) strcat(output, "   ");
        strcat(output, "  ");
        strcat(output, nicefmt);
        strcat(output, "\n");
      }
      if (byteidx < nbytes) strcat(output, "         (output truncated)\n");
    }
    return output;
  }
  
  /* private helper for gasneti_trace/stats_output */
  static void gasneti_file_output(FILE *fp, double time, const char *type, const char *msg, int traceheader) {
    gasneti_mutex_assertlocked(&gasneti_tracelock);
    gasneti_assert(fp);
    if (traceheader) {
      #if GASNETI_THREADS
        fprintf(fp, "%i(%x) %8.6fs> (%c) %s%s", 
          (int)gasnet_mynode(), (int)(uintptr_t)pthread_self(), time, *type, msg,
          (msg[strlen(msg)-1]=='\n'?"":"\n"));
      #else
        fprintf(fp, "%i %8.6fs> (%c) %s%s", (int)gasnet_mynode(), time, *type, msg,
                (msg[strlen(msg)-1]=='\n'?"":"\n"));
      #endif
    } else {
        fprintf(fp, "%i> (%c) %s%s", (int)gasnet_mynode(), *type, msg,
                (msg[strlen(msg)-1]=='\n'?"":"\n"));
    }
    fflush(fp);
  }

  /* dump message to tracefile */
  extern void gasneti_trace_output(const char *type, const char *msg, int traceheader) {
    if (gasneti_tracefile) {
      double time = GASNETI_STATTIME_TO_US(GASNETI_STATTIME_NOW() - starttime) / 1000000.0;
      gasneti_mutex_lock(&gasneti_tracelock);
        if (gasneti_tracefile) 
          gasneti_file_output(gasneti_tracefile, time, type, msg, traceheader);
      gasneti_mutex_unlock(&gasneti_tracelock);
    }
  }
  extern void gasneti_stats_output(const char *type, const char *msg, int traceheader) {
    if (gasneti_tracefile || gasneti_statsfile) {
      double time = GASNETI_STATTIME_TO_US(GASNETI_STATTIME_NOW() - starttime) / 1000000.0;
      gasneti_mutex_lock(&gasneti_tracelock);
        if (gasneti_statsfile) 
          gasneti_file_output(gasneti_statsfile, time, type, msg, traceheader);
        #if GASNETI_STATS_ECHOED_TO_TRACEFILE
        if (gasneti_tracefile) /* stat output also goes to trace */
          gasneti_file_output(gasneti_tracefile, time, type, msg, traceheader);
        #endif
      gasneti_mutex_unlock(&gasneti_tracelock);
    }
  }
  extern void gasneti_tracestats_output(const char *type, const char *msg, int traceheader) {
    if (gasneti_tracefile || gasneti_statsfile) {
      double time = GASNETI_STATTIME_TO_US(GASNETI_STATTIME_NOW() - starttime) / 1000000.0;
      gasneti_mutex_lock(&gasneti_tracelock);
        if (gasneti_statsfile) 
          gasneti_file_output(gasneti_statsfile, time, type, msg, traceheader);
        if (gasneti_tracefile) 
          gasneti_file_output(gasneti_tracefile, time, type, msg, traceheader);
      gasneti_mutex_unlock(&gasneti_tracelock);
    }
  }

  /* private helper for gasneti_trace/stats_printf */
  static void gasneti_file_vprintf(FILE *fp, const char *format, va_list argptr) {
    gasneti_mutex_assertlocked(&gasneti_tracelock);
    gasneti_assert(fp);
    fprintf(fp, "%i> ", (int)gasnet_mynode());
    vfprintf(fp, format, argptr);
    if (format[strlen(format)-1]!='\n') fprintf(fp, "\n");
    fflush(fp);
  }

  /* dump message to tracefile with simple header */
  static void gasneti_trace_printf(const char *format, ...) {
    va_list argptr;
    if (gasneti_tracefile) {
      gasneti_mutex_lock(&gasneti_tracelock);
      if (gasneti_tracefile) {
        va_start(argptr, format); /*  pass in last argument */
        gasneti_file_vprintf(gasneti_tracefile, format, argptr);
        va_end(argptr);
      }
      gasneti_mutex_unlock(&gasneti_tracelock);
    }
  }
  static void gasneti_stats_printf(const char *format, ...) {
    va_list argptr;
    if (gasneti_tracefile || gasneti_statsfile) {
      gasneti_mutex_lock(&gasneti_tracelock);
      if (gasneti_statsfile) {
        va_start(argptr, format); /*  pass in last argument */
        gasneti_file_vprintf(gasneti_statsfile, format, argptr);
        va_end(argptr);
      }
      #if GASNETI_STATS_ECHOED_TO_TRACEFILE
      if (gasneti_tracefile) { /* stat output also goes to trace */
        va_start(argptr, format); /*  pass in last argument */
        gasneti_file_vprintf(gasneti_tracefile, format, argptr);
        va_end(argptr);
      }
      #endif
      gasneti_mutex_unlock(&gasneti_tracelock);
    }
  }
  static void gasneti_tracestats_printf(const char *format, ...) {
    va_list argptr;
    if (gasneti_tracefile || gasneti_statsfile) {
      gasneti_mutex_lock(&gasneti_tracelock);
      if (gasneti_statsfile) {
        va_start(argptr, format); /*  pass in last argument */
        gasneti_file_vprintf(gasneti_statsfile, format, argptr);
        va_end(argptr);
      }
      if (gasneti_tracefile) {
        va_start(argptr, format); /*  pass in last argument */
        gasneti_file_vprintf(gasneti_tracefile, format, argptr);
        va_end(argptr);
      }
      gasneti_mutex_unlock(&gasneti_tracelock);
    }
  }
#endif

static FILE *gasneti_open_outputfile(const char *filename, const char *desc) {
  FILE *fp = NULL;
  char pathtemp[255];
  if (!strcmp(filename, "stderr") ||
      !strcmp(filename, "-")) {
    filename = "stderr";
    fp = stderr;
  } else if (!strcmp(filename, "stdout")) {
    filename = "stdout";
    fp = stdout;
  } else {
    strcpy(pathtemp,filename);
    while (strchr(pathtemp,'%')) { /* replace any '%' with node num */
      char temp[255];
      char *p = strchr(pathtemp,'%');
      *p = '\0';
      sprintf(temp,"%s%i%s",pathtemp,(int)gasnet_mynode(),p+1);
      strcpy(pathtemp,temp);
    }
    filename = pathtemp;
    fp = fopen(filename, "wt");
    if (!fp) {
      fprintf(stderr, "ERROR: Failed to open '%s' for %s output (%s). Redirecting output to stderr.\n",
              filename, desc, strerror(errno));
      filename = "stderr";
      fp = stderr;
    }
  }
  fprintf(stderr, "GASNet reporting enabled - %s output directed to %s\n", 
          desc, filename);
  return fp;
}

extern void gasneti_trace_init() {

  #if GASNETI_STATS_OR_TRACE
  const char *tracetypes = NULL;
  const char *statstypes = NULL;
  { /* setup tracetypes */
    const char *types;
    types = gasnet_getenv("GASNET_TRACEMASK");
    if (!types) types = GASNETI_ALLTYPES;
    tracetypes = types;
    while (*types) {
      gasneti_tracetypes[(int)(*types)] = 1;
      types++;
    }
    types = gasnet_getenv("GASNET_STATSMASK");
    if (!types) types = GASNETI_ALLTYPES;
    statstypes = types;
    while (*types) {
      gasneti_statstypes[(int)(*types)] = 1;
      types++;
    }
  }

  { /* setup tracefile */
    char *tracefilename = gasnet_getenv("GASNET_TRACEFILE");
    char *statsfilename = gasnet_getenv("GASNET_STATSFILE");
    if (tracefilename && !strcmp(tracefilename, "")) tracefilename = NULL;
    if (statsfilename && !strcmp(statsfilename, "")) statsfilename = NULL;
    #if GASNET_TRACE || (GASNET_STATS && GASNETI_STATS_ECHOED_TO_TRACEFILE)
      if (tracefilename) {
        gasneti_tracefile = gasneti_open_outputfile(tracefilename, 
        #if GASNET_TRACE
          "tracing"
          #if GASNET_STATS && GASNETI_STATS_ECHOED_TO_TRACEFILE
            " and "
          #endif
        #endif
        #if GASNET_STATS && GASNETI_STATS_ECHOED_TO_TRACEFILE
          "statistical"
        #endif
        );
      } else 
    #endif
      gasneti_tracefile = NULL;
    #if GASNET_STATS
      if (statsfilename) gasneti_statsfile = gasneti_open_outputfile(statsfilename, "statistical");
      else 
    #endif
        gasneti_statsfile = NULL;
  }

  { time_t ltime;
    char temp[255];
    time(&ltime); 
    strcpy(temp, ctime(&ltime));
    if (temp[strlen(temp)-1] == '\n') temp[strlen(temp)-1] = '\0';
    gasneti_tracestats_printf("Program starting at: %s", temp);
    #if GASNET_STATS
      gasneti_stats_printf("GASNET_STATSMASK: %s", statstypes);
    #endif
    #if GASNET_TRACE
      gasneti_trace_printf("GASNET_TRACEMASK: %s", tracetypes);
    #endif
  }

  #if GASNET_NDEBUG
  { char *NDEBUG_warning =
     "WARNING: tracing/statistical collection may adversely affect application performance.";
    gasneti_tracestats_printf(NDEBUG_warning);
    if (gasneti_tracefile != stdout && gasneti_tracefile != stderr &&
        gasneti_statsfile != stdout && gasneti_statsfile != stderr) {
      fprintf(stderr,NDEBUG_warning);
      fprintf(stderr,"\n");
    }
  }
  #endif

  #ifdef GASNETI_USING_GETTIMEOFDAY
    gasneti_tracestats_printf("WARNING: using gettimeofday() for timing measurement - "
      "all short-term time measurements will be very rough and include significant timer overheads");
  #endif
  gasneti_tracestats_printf("Timer granularity: ~ %.3f us, overhead: ~ %.3f us",
   GASNETI_STATTIME_GRANULARITY(), GASNETI_STATTIME_OVERHEAD());

  if (!gasneti_tracefile) /* clear types entries if we're not tracing */
    memset(gasneti_tracetypes, 0, 256);
  if (!gasneti_statsfile && !gasneti_tracefile)
    memset(gasneti_statstypes, 0, 256);

    starttime = GASNETI_STATTIME_NOW();
  #endif
}

#define AGGRNAME(cat,type) gasneti_aggregate_##cat##_##type
#define AGGR(type)                                       \
  static gasneti_statctr_t AGGRNAME(ctr,type) = 0;       \
  static gasneti_stat_intval_t AGGRNAME(intval,type) =   \
    { 0, GASNETI_STATCTR_MAX, GASNETI_STATCTR_MIN, 0 };  \
  static gasneti_stat_timeval_t AGGRNAME(timeval,type) = \
    { 0, GASNETI_STATTIME_MAX, GASNETI_STATTIME_MIN, 0 }
AGGR(G);
AGGR(P);
AGGR(S);
AGGR(B);
AGGR(L);
AGGR(A);
AGGR(I);
AGGR(C);
AGGR(D);

extern void gasneti_trace_finish() {
#if GASNETI_STATS_OR_TRACE
  static gasneti_mutex_t gasneti_tracefinishlock = GASNETI_MUTEX_INITIALIZER;
  gasneti_mutex_lock(&gasneti_tracefinishlock);
  if (gasneti_tracefile || gasneti_statsfile) {

    double time = GASNETI_STATTIME_TO_US(GASNETI_STATTIME_NOW() - starttime) / 1000000.0;
    gasneti_tracestats_printf("Total application run time: %10.6fs", time);

    #if GASNET_STATS
    { /* output statistical summary */

      gasneti_stats_printf("--------------------------------------------------------------------------------");
      gasneti_stats_printf("GASNet Statistical Summary:");
    
      #define ACCUM(pacc, pintval) do {                                       \
          pacc->count += pintval->count;                                      \
          if (pintval->minval < pacc->minval) pacc->minval = pintval->minval; \
          if (pintval->maxval > pacc->maxval) pacc->maxval = pintval->maxval; \
          pacc->sumval += pintval->sumval;                                    \
      } while (0)
      #define CALC_AVG(sum,count) ((count) == 0 ? -1 : (sum) / (count))
      #define DUMP_CTR(type,name,desc)                     \
        if (GASNETI_STATS_ENABLED(type)) {                 \
          gasneti_statctr_t *p = &gasneti_stat_ctr_##name; \
          gasneti_stats_printf(" %-25s %6i",               \
                  #name" "#desc":", (int)*p);              \
          AGGRNAME(ctr,type) += *p;                        \
        }
      #define DUMP_INTVAL(type,name,desc)                                          \
        if (GASNETI_STATS_ENABLED(type)) {                                         \
          gasneti_stat_intval_t *p = &gasneti_stat_intval_##name;                  \
          const char *pdesc = #desc;                                               \
          if (!p->count)                                                           \
            gasneti_stats_printf(" %-25s %6i", #name":", 0);                       \
          else                                                                     \
            gasneti_stats_printf(" %-25s %6i  avg/min/max/total %s = %i/%i/%i/%i", \
                  #name":", (int)p->count,                                         \
                  pdesc,                                                           \
                  (int)CALC_AVG(p->sumval,p->count),                               \
                  (int)p->minval,                                                  \
                  (int)p->maxval,                                                  \
                  (int)p->sumval);                                                 \
          ACCUM((&AGGRNAME(intval,type)), p);                                      \
        }
      #define DUMP_TIMEVAL(type,name,desc)                                         \
        if (GASNETI_STATS_ENABLED(type)) {                                         \
          gasneti_stat_timeval_t *p = &gasneti_stat_timeval_##name;                \
          const char *pdesc = #desc;                                               \
          if (!p->count)                                                           \
            gasneti_stats_printf(" %-25s %6i", #name":", 0);                       \
          else                                                                     \
            gasneti_stats_printf(" %-25s %6i  avg/min/max/total %s (us) = %i/%i/%i/%i", \
                  #name":", (int)p->count,                                         \
                  pdesc,                                                           \
                  (int)GASNETI_STATTIME_TO_US(CALC_AVG(p->sumval, p->count)),      \
                  (int)GASNETI_STATTIME_TO_US(p->minval),                          \
                  (int)GASNETI_STATTIME_TO_US(p->maxval),                          \
                  (int)GASNETI_STATTIME_TO_US(p->sumval));                         \
          ACCUM((&AGGRNAME(timeval,type)), p);                                     \
        }

      GASNETI_ALL_STATS(DUMP_CTR, DUMP_INTVAL, DUMP_TIMEVAL);

      gasneti_stats_printf(" ");
      gasneti_stats_printf(" ");

      if (GASNETI_STATS_ENABLED(G)) {
        gasneti_stat_intval_t *p = &AGGRNAME(intval,G);
        if (!p->count)
          gasneti_stats_printf("%-25s  %6i","Total gets:",0);
        else
          gasneti_stats_printf("%-25s  %6i  avg/min/max/total sz = %i/%i/%i/%i", "Total gets:",
            (int)p->count,
            (int)CALC_AVG(p->sumval, p->count),
            (int)p->minval,
            (int)p->maxval,
            (int)p->sumval);
      }
      if (GASNETI_STATS_ENABLED(P)) {
        gasneti_stat_intval_t *p = &AGGRNAME(intval,P);
        if (!p->count)
          gasneti_stats_printf("%-25s  %6i","Total puts:",0);
        else
          gasneti_stats_printf("%-25s  %6i  avg/min/max/total sz = %i/%i/%i/%i", "Total puts:",
            (int)p->count,
            (int)CALC_AVG(p->sumval, p->count),
            (int)p->minval,
            (int)p->maxval,
            (int)p->sumval);
      }
      if (GASNETI_STATS_ENABLED(S)) {
        gasneti_stat_intval_t *try_succ = &AGGRNAME(intval,S);
        gasneti_stat_timeval_t *wait_time = &AGGRNAME(timeval,S);
        if (!try_succ->count)
          gasneti_stats_printf("%-25s  %6i","Total try sync. calls:",0);
        else
          gasneti_stats_printf("%-25s  %6i  try success rate = %f%%  \n",
            "Total try sync. calls:",  ((int)try_succ->count),
            (float)(CALC_AVG((float)try_succ->sumval, try_succ->count) * 100.0));
        if (!wait_time->count)
          gasneti_stats_printf("%-25s  %6i","Total wait sync. calls:",0);
        else
          gasneti_stats_printf("%-25s  %6i  avg/min/max/total waittime (us) = %i/%i/%i/%i", 
            "Total wait sync. calls:", ((int)wait_time->count),
            (int)GASNETI_STATTIME_TO_US(CALC_AVG(wait_time->sumval, wait_time->count)),
            (int)GASNETI_STATTIME_TO_US(wait_time->minval),
            (int)GASNETI_STATTIME_TO_US(wait_time->maxval),
            (int)GASNETI_STATTIME_TO_US(wait_time->sumval));
      }
      if (GASNETI_STATS_ENABLED(A)) 
        gasneti_stats_printf("%-25s  %6i", "Total AM's:", (int)AGGRNAME(ctr,A));

      gasneti_stats_printf("--------------------------------------------------------------------------------");
    }
    #endif

    GASNETC_TRACE_FINISH(); /* allow for final output of conduit-specific statistics */

    gasneti_mutex_lock(&gasneti_tracelock);
    if (gasneti_tracefile && gasneti_tracefile != stdout && gasneti_tracefile != stderr) 
      fclose(gasneti_tracefile);
    if (gasneti_statsfile && gasneti_statsfile != stdout && gasneti_statsfile != stderr) 
      fclose(gasneti_statsfile);
    gasneti_tracefile = NULL;
    gasneti_statsfile = NULL;
    gasneti_mutex_unlock(&gasneti_tracelock);
    gasneti_mutex_unlock(&gasneti_tracefinishlock);
    sleep(1); /* pause to ensure everyone has written trace if this is a collective exit */
  }
#endif
}


/* ------------------------------------------------------------------------------------ */
#if GASNET_STATS
  #define DEF_CTR(type,name,desc)                   \
    gasneti_statctr_t gasneti_stat_ctr_##name = 0;
  #define DEF_INTVAL(type,name,desc)                   \
    gasneti_stat_intval_t gasneti_stat_intval_##name = \
      { 0, GASNETI_STATCTR_MAX, GASNETI_STATCTR_MIN, 0 };
  #define DEF_TIMEVAL(type,name,desc)                    \
    gasneti_stat_timeval_t gasneti_stat_timeval_##name = \
      { 0, GASNETI_STATTIME_MAX, GASNETI_STATTIME_MIN, 0 };
  GASNETI_ALL_STATS(DEF_CTR, DEF_INTVAL, DEF_TIMEVAL)

static gasneti_mutex_t gasneti_statlock = GASNETI_MUTEX_INITIALIZER;
#define STAT_LOCK() gasneti_mutex_lock(&gasneti_statlock);
#define STAT_UNLOCK() gasneti_mutex_unlock(&gasneti_statlock);

extern void gasneti_stat_count_accumulate(gasneti_statctr_t *pctr) {
  STAT_LOCK();
    (*pctr)++;
  STAT_UNLOCK();
}
extern void gasneti_stat_intval_accumulate(gasneti_stat_intval_t *pintval, gasneti_statctr_t val) {
  STAT_LOCK();
    pintval->count++;
    pintval->sumval += val;
    if_pf (val > pintval->maxval) pintval->maxval = val;
    if_pf (val < pintval->minval) pintval->minval = val;
  STAT_UNLOCK();
}
extern void gasneti_stat_timeval_accumulate(gasneti_stat_timeval_t *pintval, gasneti_stattime_t val) {
  STAT_LOCK();
    pintval->count++;
    pintval->sumval += val;
    if_pf (val > pintval->maxval) pintval->maxval = val;
    if_pf (val < pintval->minval) pintval->minval = val;
  STAT_UNLOCK();
}
#endif
/* ------------------------------------------------------------------------------------ */
#define GASNETI_GASNET_INTERNAL_C
#include "gasnet_mmap.c"
/* ------------------------------------------------------------------------------------ */
/* Debug memory management
   debug memory format:
  | allocdesc (pad to 8 bytes) | data sz | BEGINPOST | <user data> | ENDPOST |
                               ptr returned by malloc ^
 */
#if GASNET_DEBUG
  static uint64_t gasneti_memalloc_cnt = 0;
  static size_t   gasneti_memalloc_maxbytes = 0;
  static uintptr_t gasneti_memalloc_maxloc = 0;
  static gasneti_mutex_t gasneti_memalloc_lock = GASNETI_MUTEX_INITIALIZER;
  #define GASNETI_MEM_BEGINPOST   ((uint32_t)0xDEADBABE)
  #define GASNETI_MEM_ENDPOST     ((uint32_t)0xCAFED00D)
  #define GASNETI_MEM_FREEMARK    ((uint32_t)0xBEEFEFAD)
  #define GASNETI_MEM_HEADERSZ    16     
  #define GASNETI_MEM_TAILSZ      4     
  #define GASNETI_MEM_EXTRASZ     (GASNETI_MEM_HEADERSZ+GASNETI_MEM_TAILSZ)     

  /* assert the integrity of given memory block and return size of the user object */
  extern size_t _gasneti_memcheck(void *ptr, const char *curloc, int isfree) {
    uint32_t beginpost = *(((uint32_t *)ptr)-1);
    size_t nbytes = *(((uint32_t *)ptr)-2);
    char *allocptr = (void *)(uintptr_t)*(((uint64_t *)ptr)-2);
    uint32_t endpost = 0;
    const char *corruptstr = NULL;
    if (nbytes > gasneti_memalloc_maxbytes || 
      ((uintptr_t)ptr)+nbytes > gasneti_memalloc_maxloc) {
      allocptr = NULL; /* bad nbytes, don't trust allocptr */
      nbytes = 0;
    } else memcpy(&endpost,((char*)ptr)+nbytes,4);

    if (beginpost == GASNETI_MEM_FREEMARK) {
      if (isfree)
        corruptstr = "detected a duplicate gasneti_free() or memory corruption";
      else
        corruptstr = "gasneti_memcheck() called on freed memory (may indicate memory corruption)";
    } else if (beginpost != GASNETI_MEM_BEGINPOST || endpost != GASNETI_MEM_ENDPOST) {
      if (isfree)
        corruptstr = "gasneti_free() detected bad ptr or memory corruption";
      else
        corruptstr = "gasneti_memcheck() detected bad ptr or memory corruption";
    }

    if (corruptstr != NULL) {
      char nbytesstr[80];
      if (allocptr != NULL && memchr(allocptr,'\0',255) == 0) /* allocptr may be bad */
        allocptr = NULL; 
      if (allocptr == NULL) nbytesstr[0] = '\0';
      else sprintf(nbytesstr,", nbytes=%i",(int)nbytes);
      gasneti_fatalerror("%s\n   ptr="GASNETI_LADDRFMT"%s%s%s%s%s",
           corruptstr,
           GASNETI_LADDRSTR(ptr), nbytesstr,
           (allocptr!=NULL?",\n   allocated at: ":""), (allocptr!=NULL?allocptr:""),
           (curloc!=NULL?(isfree?",\n   freed at: ":",\n   detected at: "):""), 
           (curloc!=NULL?curloc:"")
           );
    }
    return nbytes;
  }

  /* get access to system malloc/free */
  #undef malloc
  #undef free
  extern void *_gasneti_malloc(size_t nbytes, int allowfail, const char *curloc) {
    void *ret = NULL;
    if_pt (gasneti_attach_done) gasnet_hold_interrupts();
    ret = malloc(nbytes+GASNETI_MEM_EXTRASZ);
    if_pf (ret == NULL) {
      if (allowfail) {
        if_pt (gasneti_attach_done) gasnet_resume_interrupts();
        GASNETI_TRACE_PRINTF(I,("Warning: returning NULL for a failed gasneti_malloc(%i)",(int)nbytes));
        return NULL;
      }
      gasneti_fatalerror("gasneti_malloc(%d) failed (%lu bytes allocated): %s", 
        (int)nbytes, (unsigned long)gasneti_memalloc_cnt, 
        (curloc == NULL ? "" : curloc));
    } else {
      uint32_t gasneti_endpost_ref = GASNETI_MEM_ENDPOST;
      gasneti_mutex_lock(&gasneti_memalloc_lock);
      gasneti_memalloc_cnt += nbytes+GASNETI_MEM_EXTRASZ;
      if (nbytes > gasneti_memalloc_maxbytes) gasneti_memalloc_maxbytes = nbytes;
      if (((uintptr_t)ret)+nbytes+GASNETI_MEM_HEADERSZ > gasneti_memalloc_maxloc) 
        gasneti_memalloc_maxloc = ((uintptr_t)ret)+nbytes+GASNETI_MEM_HEADERSZ;
      gasneti_mutex_unlock(&gasneti_memalloc_lock);
      ((uint64_t *)ret)[0] = (uint64_t)(uintptr_t)curloc;
      ((uint32_t *)ret)[2] = (uint32_t)nbytes;
      ((uint32_t *)ret)[3] = GASNETI_MEM_BEGINPOST;
      memcpy(((char*)ret)+nbytes+GASNETI_MEM_HEADERSZ, &gasneti_endpost_ref, 4);
      ret = (void *)(((uintptr_t)ret) + GASNETI_MEM_HEADERSZ);
    }
    if_pt (gasneti_attach_done) gasnet_resume_interrupts();
    _gasneti_memcheck(ret,curloc,0);
    return ret;
  }

  extern void _gasneti_free(void *ptr, const char *curloc) {
    size_t nbytes;
    if_pf (ptr == NULL) return;
    if_pt (gasneti_attach_done) gasnet_hold_interrupts();
    nbytes = _gasneti_memcheck(ptr, curloc, 1);
    *(((uint32_t *)ptr)-1) = GASNETI_MEM_FREEMARK;
    gasneti_mutex_lock(&gasneti_memalloc_lock);
    gasneti_memalloc_cnt -= nbytes+GASNETI_MEM_EXTRASZ;
    gasneti_mutex_unlock(&gasneti_memalloc_lock);
    free(((uint32_t *)ptr)-4);
    if_pt (gasneti_attach_done) gasnet_resume_interrupts();
  }

  extern void *_gasneti_calloc(size_t N, size_t S, const char *curloc) {
    size_t nbytes = N*S;
    void *ret = _gasneti_malloc(nbytes, 0, curloc);
    memset(ret,0,nbytes);
    _gasneti_memcheck(ret,curloc,0);
    return ret;
  }
#endif
/* don't put anything here - malloc stuff must come last */

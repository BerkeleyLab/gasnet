//  $Archive:: /Ti/AMUDP/sig.cpp                                          $
//     $Date: 2004/03/05 15:02:50 $
// $Revision: 1.3 $
// Description: signal handling module
// Copyright 1999, Dan Bonachea

#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include "sig.h"

//------------------------------------------------------------------------------------
LPSIGHANDLER reghandler(int sigtocatch, LPSIGHANDLER fp) {
  LPSIGHANDLER fpret = signal(sigtocatch, fp); 
  if (fpret == (LPSIGHANDLER)SIG_ERR) {
    printf("Got a SIG_ERR while registering handler for signal %s. Errno = %i\n", 
    sigstr(sigtocatch), errno);
    return NULL;
    }
#ifdef SIG_HOLD
  else if (fpret == (LPSIGHANDLER)SIG_HOLD) {
    printf("Got a SIG_HOLD while registering handler for signal %s(%i).\n", 
    sigstr(sigtocatch), errno);
    return NULL;
    }
#endif
  return fpret;
  }
//------------------------------------------------------------------------------------
static struct {
  int sig;
  const char* desc;
  SIGTYPE sigtype; 
  } sigdesctable[] = {
    {SIGABRT, "SIGABRT: Process abort signal.", ST_PROGRAM_ERROR}, // (abort())
    {SIGFPE,  "SIGFPE: Erroneous arithmetic operation.", ST_PROGRAM_ERROR}, // (FP error)
    {SIGILL,  "SIGILL: Illegal instruction.", ST_PROGRAM_ERROR}, // (bad instruction)
    {SIGINT,  "SIGINT: Terminal interrupt signal.", ST_TERM_INT}, // (control-c)
    {SIGSEGV, "SIGSEGV: Invalid memory reference.", ST_PROGRAM_ERROR}, // (seg fault)
    {SIGTERM, "SIGTERM: Termination signal.", ST_SYS_INT}, // (kill command)
#ifndef WIN32
    {SIGALRM, "SIGALRM: Alarm clock.", ST_OTHER},
    {SIGHUP,  "SIGHUP: Hangup.", ST_SYS_INT},
    {SIGKILL, "SIGKILL: Kill (cannot be caught or ignored).", ST_FATAL}, // (kill -9 command)
    {SIGPIPE, "SIGPIPE: Write on a pipe with no one to read it.", ST_OTHER}, // (send() after close)
    {SIGQUIT, "SIGQUIT: Terminal quit signal.", ST_TERM_INT}, // (control-\)
    {SIGUSR1, "SIGUSR1: User-defined signal 1.", ST_OTHER},
    {SIGUSR2, "SIGUSR2: User-defined signal 2.", ST_OTHER},
    {SIGCHLD, "SIGCHLD: Child process terminated or stopped.", ST_OTHER}, // (sent to parent proc)
    {SIGCONT, "SIGCONT: Continue executing, if stopped.", ST_OTHER}, // (also sent by kill command)
    {SIGSTOP, "SIGSTOP: Stop executing (cannot be caught or ignored).", ST_FATAL},
    {SIGTSTP, "SIGTSTP: Terminal stop signal.", ST_TERM_INT}, // (control-z)
    {SIGTTIN, "SIGTTIN: Background process attempting read.", ST_OTHER},
    {SIGTTOU, "SIGTTOU: Background process attempting write.", ST_OTHER},
    {SIGBUS,  "SIGBUS: Bus error.", ST_PROGRAM_ERROR}, // (alignment error)
  #if !defined(UNICOS) && !defined(MACOSX) && !defined(DARWIN)
  #if !defined(FREEBSD)
    {SIGPOLL, "SIGPOLL: Pollable event.", ST_OTHER},
  #endif
    {SIGXFSZ, "SIGXFSZ:  File size limit exceeded.", ST_PROGRAM_ERROR},
  #endif
    {SIGPROF, "SIGPROF: Profiling timer expired.", ST_OTHER},
  #ifdef SIGSYS
    {SIGSYS,  "SIGSYS: Bad system call.", ST_PROGRAM_ERROR},
  #endif
    {SIGTRAP, "SIGTRAP: Trace/breakpoint trap.", ST_PROGRAM_ERROR},
    {SIGURG,  "SIGURG: High bandwidth data is available at a socket.", ST_OTHER},
    {SIGVTALRM,"SIGVTALRM: Virtual timer expired.", ST_OTHER},
    {SIGXCPU, "SIGXCPU: CPU time limit exceeded.", ST_PROGRAM_ERROR},
#endif
#ifdef SOLARIS
    {SIGEMT,     "SIGEMT: Emulation Trap", ST_OTHER},
    {SIGPWR,     "SIGPWR: Power Fail or Restart", ST_OTHER},
    {SIGWINCH,   "SIGWINCH: Window Size Change", ST_OTHER},
    {SIGWAITING, "SIGWAITING: Concurrency signal reserved  by threads library", ST_OTHER},
    {SIGLWP,     "SIGLWP: Inter-LWP  signal  reserved  by threads library", ST_OTHER},
    {SIGFREEZE,  "SIGFREEZE: Check point Freeze", ST_OTHER},
    {SIGTHAW,    "SIGTHAW: Check point Thaw", ST_OTHER},
    {SIGCANCEL,  "SIGCANCEL: Cancellation signal reserved by threads library", ST_OTHER},
// SIGRTMIN ... SIGRTMAX (real-time signals) ignored
#endif
    {0, NULL, ST_OTHER}
    };
//------------------------------------------------------------------------------------
const char* sigstr(int sig) {
  for (int i=0; sigdesctable[i].desc; i++) {
    if (sigdesctable[i].sig == sig) return sigdesctable[i].desc;
    }
  return "Unknown Signal";
  }
//------------------------------------------------------------------------------------
void regallhandler(LPSIGHANDLER fp, SIGTYPE sigtype) {
  for (int i=0; sigdesctable[i].desc; i++) {
    if (sigdesctable[i].sigtype == sigtype || 
        (sigtype == ST_ALL_CATCHABLE && sigdesctable[i].sigtype != ST_FATAL))
        reghandler(sigdesctable[i].sig, fp);
    }
  return;
  }
//------------------------------------------------------------------------------------
SIGTYPE getsigtype(int sig) {
  for (int i=0; sigdesctable[i].desc; i++) {
    if (sigdesctable[i].sig == sig) return sigdesctable[i].sigtype;
    }
  return ST_OTHER;
  }
//------------------------------------------------------------------------------------

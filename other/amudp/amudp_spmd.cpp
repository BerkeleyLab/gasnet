/*  $Archive:: /Ti/AMUDP/amudp_spmd.cpp                                   $
 *     $Date: 2004/01/28 03:46:53 $
 * $Revision: 1.4 $
 * Description: AMUDP Implementations of SPMD operations (bootstrapping and parallel job control)
 * Copyright 2000, Dan Bonachea <bonachea@cs.berkeley.edu>
 */

#include <errno.h>
#include <stdio.h>
#ifdef WIN32
  #define sched_yield() Sleep(0)
  #define sleep(x) Sleep(x*1000)
  #include <process.h>
#else
  #include <unistd.h>
  #include <sched.h>
  #if defined(LINUX) && !defined(__USE_GNU)
    /* some Linuxes need this to pull in F_SETSIG */
    #define __USE_GNU
    #include <fcntl.h>
    #undef __USE_GNU
  #else
    #include <fcntl.h>
  #endif
  #ifdef _CRAYT3E
    #define sched_yield() sleep(0)
  #endif
#endif

extern char **environ; 

#include <amudp.h>
#include <amudp_spmd.h>
#include <amudp_internal.h>
#include "sockutil.h"
#include "socklist.h"
#include "sig.h"

#define FD_STDIN 0
#define FD_STDOUT 1
#define FD_STDERR 2

#ifndef FREEZE_SLAVE
#define FREEZE_SLAVE  0
#endif
static volatile bool amudp_frozen = true;
/*  all this to make sure we get a full stack frame for debugger */
static void _freezeForDebugger(int depth) {
  if (!depth) _freezeForDebugger(1);
  else {
    volatile int i = 0;
    while (amudp_frozen) {
      i++;
      sleep(1);
      }
    }
  }
static void freezeForDebugger() {
  char name[255];
  gethostname(name, 255);
  fprintf(stderr,"slave frozen for debugger: host=%s  pid=%i\n", name, getpid()); fflush(stderr);
  _freezeForDebugger(0);
}

#if AMUDP_DEBUG_VERBOSE
  #define DEBUG_SLAVE(msg)  do { fprintf(stderr,"slave %i: %s\n", AMUDP_SPMDMYPROC, msg); fflush(stderr); } while(0)
  #define DEBUG_MASTER(msg) do { fprintf(stderr,"master: %s\n", msg); fflush(stderr); } while(0)
#else
  #define DEBUG_SLAVE(msg)  do {} while(0) /* prevent silly warnings about empty statements */
  #define DEBUG_MASTER(msg) do {} while(0)
#endif

#define AMUDP_SPMDSLAVE_FLAG "__AMUDP_SLAVE_PROCESS__"

static int AMUDP_SPMDShutdown(int exitcode);

/* master only */
  static SOCKET AMUDP_SPMDListenSocket = INVALID_SOCKET; /* TCP bootstrapping listener */
  static SOCKET AMUDP_SPMDStdinListenSocket = INVALID_SOCKET; 
  static SOCKET AMUDP_SPMDStdoutListenSocket = INVALID_SOCKET; 
  static SOCKET AMUDP_SPMDStderrListenSocket = INVALID_SOCKET; 
  static SOCKET *AMUDP_SPMDSlaveSocket = NULL; /* table of TCP control sockets */
  static amudp_translation_t *AMUDP_SPMDTranslationTable = NULL;
  int AMUDP_SPMDSpawnRunning = FALSE; /* true while spawn is active */
  #if DISABLE_STDSOCKET_REDIRECT
    int AMUDP_SPMDRedirectStdsockets = FALSE; /* true if stdin/stdout/stderr should be redirected */
  #else
    int AMUDP_SPMDRedirectStdsockets = TRUE; /* true if stdin/stdout/stderr should be redirected */
  #endif

/* slave only */
  SOCKET AMUDP_SPMDControlSocket = INVALID_SOCKET; 
  static ep_t AMUDP_SPMDEndpoint = NULL;
  static eb_t AMUDP_SPMDBundle = NULL;
  static en_t AMUDP_SPMDName = {0};
  volatile int AMUDP_SPMDIsActiveControlSocket = 0; 
  static SOCKET newstdin = INVALID_SOCKET;
  static SOCKET newstdout = INVALID_SOCKET;
  static SOCKET newstderr = INVALID_SOCKET;
  static int AMUDP_SPMDMYPROC = -1;
  static volatile int AMUDP_SPMDBarrierDone = 0; /* flag barrier as complete */
  static volatile int AMUDP_SPMDGatherDone = 0;  /* flag gather as complete */
  static volatile int AMUDP_SPMDGatherLen = 0;
  static void * volatile AMUDP_SPMDGatherData = NULL;
  int AMUDP_SPMDwakeupOnControlActivity = 0;
  int AMUDP_FailoverAcksOutstanding = 0;


/* master & slave */
  static int AMUDP_SPMDStartupCalled = 0;
  static int AMUDP_SPMDNUMPROCS = -1;
  static char *AMUDP_SPMDMasterEnvironment = NULL;

// used to pass info
typedef struct {
  int32_t procid;       // id for this processor
  int32_t numprocs;     // num procs in job
  uint64_t networkpid;  // globally unique pid
  int32_t depth;        // network depth
  tag_t tag;            // tag for this processor
  double faultInjectionRate; // AMUDP_FaultInjectionRate
  uint16_t stdinMaster; // address of stdin listener
  uint16_t stdoutMaster; // address of stdout listener
  uint16_t stderrMaster; // address of stderr listener
  uint32_t translationtablesz; // size of translation table we're about to send
  uint32_t environtablesz; // size of environment table we're about to send
  } AMUDP_SPMDBootstrapInfo_t;

/*
  Protocol for TCP bootstrapping/control sockets
  initialization: 
    slave->master (en_t) - send my endpoint name for init
    master->slave (AMUDP_SPMDBootstrapInfo_t) 
    master->slave (AMUDP_SPMDTranslationTable (variable size)) 
    master->slave (AMUDP_SPMDMasterEnvironment (variable size)) 

  master->slave messages
    "E"(int exitcode) - die now with this exit code
    "F"(i)(old en_t)(new en_t) - slave i's NIC just failed over to new en_t
    "A"(i) - (to slave i) slave acknowledged fail-over of slave i's NIC
    "B" - barrier complete
    "G"(perproclen)(data) - end an AllGather, here's the result

  slave->master messages
    "E"(int exitcode) - exit with this code
    "F"(i)(old en_t)(new en_t) - slave i's NIC just failed over to new en_t
    "A"(i) - acknowledge fail-over of slave i's NIC
    "B" - enter barrier
    "G"(i)(perproclen)(data) - slave i begin an AllGather, here's the length and my data
*/
/* ------------------------------------------------------------------------------------ 
 *  misc helpers
 * ------------------------------------------------------------------------------------ */
static void flushStreams(const char *context) {
  if (!context) context = "flushStreams()";

  if (fflush(stdout)) {
    ErrMessage("failed to flush stdout in %s", context); 
    perror("fflush");
    exit(1);
    }
  if (fflush(stderr)) {
    ErrMessage("failed to flush stderr in %s", context); 
    perror("fflush");
    exit(1);
    }
  sched_yield();
  }
//------------------------------------------------------------------------------------
extern char *AMUDP_enStr(en_t en, char *buf) {
  AMUDP_assert(buf != NULL);
  #ifdef UETH
    sprintf(buf, "(fixed: %i variable: %i)", en.fixed, en.variable.index);
  #else
    SockAddr tmp((sockaddr*)&en);
    sprintf(buf, "(%s:%i)", tmp.IPStr(), tmp.port());
  #endif
  return buf;
  }
extern char *AMUDP_tagStr(tag_t tag, char *buf) {
  AMUDP_assert(buf != NULL);
  sprintf(buf, "0x%08x%08x", 
    (uint32_t)(tag >> 32), 
    (uint32_t)(tag & 0xFFFFFFFF));
  return buf;
  }
extern const char *sockErrDesc() {
  return errorCodeString(getSocketErrorCode());
  }
//------------------------------------------------------------------------------------
static void setupStdSocket(SOCKET& ls, SocketList& list, SocketList& allList) {
  if ((int)list.getCount() < AMUDP_SPMDNUMPROCS) {
    SockAddr remoteAddr;
    SOCKET newsock = accept_socket(ls, remoteAddr);
    list.insert(newsock);
    allList.insert(newsock);
    if ((int)list.getCount() == AMUDP_SPMDNUMPROCS) {
      // close listener
      close_socket(ls);
      allList.remove(ls);
      ls = INVALID_SOCKET;
      }
    }
  else ErrMessage("master detected some unrecognized activity on a std listener");
  }
//------------------------------------------------------------------------------------
static void handleStdOutput(FILE *fd, fd_set *psockset, SocketList& list, SocketList& allList, int nproc) {
  int numset;
  static SOCKET *tempSockArr = NULL;
  if (!tempSockArr) tempSockArr = new SOCKET[nproc];
  if ((numset = list.getIntersection(psockset, tempSockArr, nproc))) { // we have some active std sockets
    for (int i=0; i < numset; i++) {
      SOCKET s = tempSockArr[i];
      AMUDP_assert(FD_ISSET(s, psockset));
      if (isClosed(s)) {
        DEBUG_MASTER("dropping a std output socket...");
        list.remove(s);
        allList.remove(s);
        continue;
        }
      // TODO: line-by-line buffering
      int sz = numBytesWaiting(s);
      char *buf = new char[sz+2];
      recvAll(s, buf, sz);
      buf[sz] = '\0';
      #if AMUDP_DEBUG_VERBOSE
        fprintf(fd, "got some output: %s", buf);
      #else
        fwrite(buf, sz, 1, fd);
      #endif
      fflush(fd);
      delete buf;
      }
    }
  }
//------------------------------------------------------------------------------------
#if USE_ASYNC_TCP_CONTROL
  static void AMUDP_SPMDControlSocketCallback(int sig) {
    AMUDP_SPMDIsActiveControlSocket = TRUE;
    #if AMUDP_DEBUG_VERBOSE
      fprintf(stderr, "got an AMUDP_SIGIO signal\n");fflush(stderr);
    #endif
    reghandler(AMUDP_SIGIO, AMUDP_SPMDControlSocketCallback);
    }
#endif
/* ------------------------------------------------------------------------------------ 
 *  basic inquiries
 * ------------------------------------------------------------------------------------ */
extern int AMUDP_SPMDNumProcs() {
  if (!AMUDP_SPMDStartupCalled) {
    ErrMessage("called AMUDP_SPMDNumProcs before AMUDP_SPMDStartup()");
    return -1;
    }
  AMUDP_assert(AMUDP_SPMDNUMPROCS >= 1);
  return AMUDP_SPMDNUMPROCS;
  }
/* ------------------------------------------------------------------------------------ */
extern int AMUDP_SPMDMyProc() {
  if (!AMUDP_SPMDStartupCalled) {
    ErrMessage("called AMUDP_SPMDMyProc before AMUDP_SPMDStartup()");
    return -1;
    }
  AMUDP_assert(AMUDP_SPMDMYPROC >= 0);
  return AMUDP_SPMDMYPROC;
  }
/* ------------------------------------------------------------------------------------ */
extern int AMUDP_SPMDIsWorker(char **argv) {
  if (AMUDP_SPMDStartupCalled) return 1; 
  else {
    AMUDP_assert(argv != NULL);
    return (argv[1] && !strcmp(argv[1], AMUDP_SPMDSLAVE_FLAG));
    }
  }
/* ------------------------------------------------------------------------------------ */
extern int AMUDP_SPMDStartup(int *argc, char ***argv,
                             int nproc, int networkdepth, 
                             amudp_spawnfn_t spawnfn,
                             uint64_t *networkpid,
                             eb_t *eb, ep_t *ep) {
  
  if (AMUDP_SPMDStartupCalled) AMUDP_RETURN_ERR(RESOURCE);
  if (!argc || !argv) AMUDP_RETURN_ERR(BAD_ARG);
  /* we need a separate socklibinit for master 
     and to prevent AM_Terminate from murdering all our control sockets */
  if (!socklibinit()) AMUDP_RETURN_ERRFR(RESOURCE, AMUDP_SPMDStartup, "socklibinit() failed");

  /* ------------------------------------------------------------------------------------ 
   *  I'm a master 
   * ------------------------------------------------------------------------------------ */
  if ((*argc) < 2 || strcmp((*argv)[1], AMUDP_SPMDSLAVE_FLAG)) { 
    int usingdefaultdegree = 0;
    if (nproc < 0 || nproc > AMUDP_MAX_SPMDPROCS) AMUDP_RETURN_ERR(BAD_ARG);

    /* defaulting */
    if (networkdepth < 0) AMUDP_RETURN_ERR(BAD_ARG);
    if (networkdepth == 0) {
      const char *netdepth_str = AMUDP_getenv_prefixed("NETWORKDEPTH");
      if (netdepth_str) networkdepth = atoi(netdepth_str);
      if (networkdepth <= 0) networkdepth = AMUDP_DEFAULT_NETWORKDEPTH;
    }

    if (nproc == 0) { /* default to read from args */
      if (*argc > 1) nproc = atoi((*argv)[1]);
      if (nproc < 1) {
        fprintf(stderr, 
          "AMUDP SPMD Runtime Layer v%s, Copyright 2001, Dan Bonachea\n"
          "This program requires you specify the parallel degree\n"
          "as the first argument to %s\n" 
          , AMUDP_LIBRARY_VERSION_STR, (*argv)[0]);
        exit(1);
        AMUDP_RETURN_ERR(BAD_ARG);
      }
      
      usingdefaultdegree = 1;

      /* readjust params */
      (*argv)[1] = (*argv)[0];
      (*argv)++;
      (*argc)--;
    }

    AMUDP_SPMDNUMPROCS = nproc;

    { /* check job size */
      int maxtranslations = 0;
      int temp = AM_MaxNumTranslations(&maxtranslations);
      if (temp != AM_OK) {
        ErrMessage("Failed to AM_MaxNumTranslations() in AMUDP_SPMDStartup");
        AMUDP_RETURN(temp);
      } else if (AMUDP_SPMDNUMPROCS > maxtranslations) {
        ErrMessage("Too many nodes: AM_MaxNumTranslations (%d) less than number of requested nodes (%d)",
                maxtranslations, AMUDP_SPMDNUMPROCS);
        AMUDP_RETURN_ERR(RESOURCE);
      }
    }

    if (!spawnfn && *argc > 1 && strlen((*argv)[1]) == 1) {
      for (int i=0; AMUDP_Spawnfn_Desc[i].abbrev; i++) {
        if (toupper((*argv)[1][0]) == toupper(AMUDP_Spawnfn_Desc[i].abbrev)) {
          spawnfn = AMUDP_Spawnfn_Desc[i].fnptr;
          break;
        }
      }
      /* readjust params */
      (*argv)[1] = (*argv)[0];
      (*argv)++;
      (*argc)--;
    }
    if (!spawnfn) {
      fprintf(stderr, 
        "AMUDP SPMD Runtime Layer v%s, Copyright 2001, Dan Bonachea\n"
        "Usage: %s%s <spawnfn> program args...\n"
        " <spawnfn> = one of the following mechanisms for spawning remote workers:\n"
        , AMUDP_LIBRARY_VERSION_STR, (*argv)[0], (usingdefaultdegree?" <paralleldegree>":""));
      for (int i=0; AMUDP_Spawnfn_Desc[i].abbrev; i++) {
        fprintf(stderr, "    '%c'  %s\n",  
              toupper(AMUDP_Spawnfn_Desc[i].abbrev), AMUDP_Spawnfn_Desc[i].desc);
      }
      exit(1);
      AMUDP_RETURN_ERR(BAD_ARG);
    }

    // setup bootstrap info 
    AMUDP_SPMDBootstrapInfo_t bootstrapinfo;
    bootstrapinfo.numprocs = AMUDP_SPMDNUMPROCS;
    bootstrapinfo.translationtablesz = AMUDP_SPMDNUMPROCS * sizeof(amudp_translation_t);
    bootstrapinfo.depth = networkdepth;
    {
      int masterpid = getpid();
      uint32_t masterIP;
      try {
        SockAddr dnsAddr = DNSLookup(getMyHostName());
        masterIP = dnsAddr.IP();
        }
      catch (xBase &exn) {
        AMUDP_RETURN_ERRFR(RESOURCE, AMUDP_SPMDStartup, exn.why());
        }
      bootstrapinfo.networkpid = ((uint64_t)masterIP) << 32 | 
                                 (((uint64_t)masterpid) & 0xFFFF);
      if (networkpid) *networkpid = bootstrapinfo.networkpid;
      }

    { char *faultRate = getenv("AMUDP_FAULT_RATE");
      if (faultRate && atof(faultRate) != 0.0) {      
        bootstrapinfo.faultInjectionRate = atof(faultRate);
        }
      else bootstrapinfo.faultInjectionRate = 0.0;
      }

    const char *masterHostname = getMyHostName();
    #if AMUDP_DEBUG_VERBOSE
      printf("master host name: %s\n", masterHostname);
    #endif

    // TCP socket lists
    SocketList allList(AMUDP_SPMDNUMPROCS*4+10); // a list of all active sockets
    SocketList coordList(AMUDP_SPMDNUMPROCS);    // a list of all coordination sockets
    SocketList stdinList(AMUDP_SPMDNUMPROCS);    // a list of all stdin routing sockets
    SocketList stdoutList(AMUDP_SPMDNUMPROCS);   // a list of all stdout routing sockets
    SocketList stderrList(AMUDP_SPMDNUMPROCS);   // a list of all stderr routing sockets
    AMUDP_SPMDSlaveSocket = (SOCKET*)malloc(AMUDP_SPMDNUMPROCS * sizeof(SOCKET));

    try {
      // create our TCP listen ports 
      unsigned short anyport = 0;
      AMUDP_SPMDListenSocket = listen_socket(anyport, false);
      AMUDP_SPMDStdinListenSocket = listen_socket(anyport, false);
      AMUDP_SPMDStdoutListenSocket = listen_socket(anyport, false);
      AMUDP_SPMDStderrListenSocket = listen_socket(anyport, false);
      bootstrapinfo.stdinMaster = getsockname(AMUDP_SPMDStdinListenSocket).port();
      bootstrapinfo.stdoutMaster = getsockname(AMUDP_SPMDStdoutListenSocket).port();
      bootstrapinfo.stderrMaster = getsockname(AMUDP_SPMDStderrListenSocket).port();
      }
    catch (xBase &exn) {
      AMUDP_RETURN_ERRFR(RESOURCE, AMUDP_SPMDStartup, exn.why());
      }

    allList.insert(AMUDP_SPMDListenSocket);
    allList.insert(AMUDP_SPMDStdinListenSocket);
    allList.insert(AMUDP_SPMDStdoutListenSocket);
    allList.insert(AMUDP_SPMDStderrListenSocket);

    // create and initialize the translation table that we'll fill in as slaves connect
    AMUDP_SPMDTranslationTable = (amudp_translation_t*)malloc(bootstrapinfo.translationtablesz);
    for (int i=0; i < AMUDP_SPMDNUMPROCS; i++) {
      AMUDP_SPMDSlaveSocket[i] = INVALID_SOCKET;
      AMUDP_SPMDTranslationTable[i].tag = bootstrapinfo.networkpid | ((uint64_t)i) << 16;
      }

    {
      /* flatten a snapshot of the master's environment for transmission to slaves
       * here we assume the standard representation where a pointer to the environment 
       * is stored in a global variable 'environ' and the environment is represented as an array 
       * of null-terminated strings where each has the form 'key=value' and value may be empty, 
       * and the final string pointer is a NULL pointer
       * we flatten this into a list of null-terminated 'key=value' strings, 
       * terminated with a double-null
       */
      int i;
      int totalEnvSize = 0;
      for(i = 0; environ[i]; i++) 
        totalEnvSize += strlen(environ[i]) + 1;
      totalEnvSize++;

      AMUDP_SPMDMasterEnvironment = (char *)malloc(totalEnvSize);
      char *p = AMUDP_SPMDMasterEnvironment;
      p[0] = '\0';
      for(i = 0; environ[i]; i++) {
        strcpy(p, environ[i]);
        p += strlen(p) + 1;
        }
      *p = '\0';
      AMUDP_assert((p+1) - AMUDP_SPMDMasterEnvironment == totalEnvSize);
      bootstrapinfo.environtablesz = totalEnvSize;
      }

    // setup a slave argv
    const char **slaveargv = (const char**)malloc(sizeof(const char*)*((*argc)+3));
    int slaveargc = (*argc)+2;
    slaveargv[0] = (*argv)[0];
    slaveargv[1] = AMUDP_SPMDSLAVE_FLAG;
    SockAddr masterAddr = getsockname(AMUDP_SPMDListenSocket);
    #if USE_NUMERIC_MASTER_ADDR
      slaveargv[2] = masterAddr.FTPStr();
    #else
      char masteraddrstr[1024];
      sprintf(masteraddrstr, "%s:%i", masterHostname, masterAddr.port());
      slaveargv[2] = masteraddrstr;
    #endif
    for (int k = 1; k < (*argc); k++) {
      slaveargv[k+2] = (*argv)[k];
      }
    slaveargv[slaveargc] = NULL;

    // call system-specific spawning routine
    AMUDP_SPMDSpawnRunning = TRUE;
    if (!spawnfn(AMUDP_SPMDNUMPROCS, slaveargc, (char **)slaveargv)) { 
      ErrMessage("Error spawning SPMD worker threads. Exiting...");
      exit(1);
      }
    AMUDP_SPMDSpawnRunning = FALSE;

    if (!AMUDP_SPMDRedirectStdsockets) {
      // spawn function disabled our stdsocket redirect - signal the slaves of this fact
      bootstrapinfo.stdinMaster = 0;
      bootstrapinfo.stdoutMaster = 0;
      bootstrapinfo.stderrMaster = 0;
      }

    // main communication loop for master
    try {
      int numSlavesAttached = 0;

      fd_set sockset;
      fd_set* psockset = &sockset;
      int numset; // helpers for coord socket
      SOCKET *tempSockArr = new SOCKET[AMUDP_SPMDNUMPROCS];
      while (1) {
        allList.makeFD_SET(psockset);

        if (select(allList.getMaxFd()+1, psockset, NULL, NULL, NULL) == -1) { // block for activity
          perror("select");
          exit(1);
          }
        //------------------------------------------------------------------------------------
        // stdin/stderr/stdout listeners - incoming connections
        if (AMUDP_SPMDStdinListenSocket != INVALID_SOCKET &&
            FD_ISSET(AMUDP_SPMDStdinListenSocket, psockset))  
          setupStdSocket(AMUDP_SPMDStdinListenSocket, stdinList, allList);
        if (AMUDP_SPMDStdoutListenSocket != INVALID_SOCKET &&
            FD_ISSET(AMUDP_SPMDStdoutListenSocket, psockset)) 
          setupStdSocket(AMUDP_SPMDStdoutListenSocket, stdoutList, allList);
        if (AMUDP_SPMDStderrListenSocket != INVALID_SOCKET &&
            FD_ISSET(AMUDP_SPMDStderrListenSocket, psockset)) 
          setupStdSocket(AMUDP_SPMDStderrListenSocket, stderrList, allList);
        //------------------------------------------------------------------------------------
        // stdout/err sockets - must come before possible exit to drain output
        handleStdOutput(stdout, psockset, stdoutList, allList, AMUDP_SPMDNUMPROCS);
        handleStdOutput(stderr, psockset, stderrList, allList, AMUDP_SPMDNUMPROCS);
        // stdin (illegal to receive anything here)
        if ((numset = stdinList.getIntersection(psockset, tempSockArr, AMUDP_SPMDNUMPROCS))) {
          for (int i=0; i < numset; i++) {
            SOCKET s = tempSockArr[i];
            AMUDP_assert(FD_ISSET(s, psockset));
            if (isClosed(s)) {
              DEBUG_MASTER("dropping a stdinList socket...");
              stdinList.remove(s);
              allList.remove(s);
              }
            else {
              ErrMessage("Master got illegal input on a stdin socket");
              stdinList.remove(s); // prevent subsequent warnings
              allList.remove(s);
              }
            }
          }
        //------------------------------------------------------------------------------------
        // coordination listener
        if (AMUDP_SPMDListenSocket != INVALID_SOCKET && 
            FD_ISSET(AMUDP_SPMDListenSocket, psockset)) { // incoming connection on coordination socket
          //DEBUG_MASTER("got some activity on AMUDP_SPMDListenSocket");
          if (numSlavesAttached < AMUDP_SPMDNUMPROCS) { // attach a slave
            SockAddr remoteAddr;
            SOCKET newcoord = accept_socket(AMUDP_SPMDListenSocket, remoteAddr);
            coordList.insert(newcoord);
            allList.insert(newcoord);

            #if USE_COORD_KEEPALIVE
            { // make sure we get connection termination notification in a timely manner
              int val = 1;
              if (setsockopt(newcoord, SOL_SOCKET, SO_KEEPALIVE, (char *)&val, sizeof(int)) == SOCKET_ERROR)
                DEBUG_MASTER("failed to setsockopt(SO_KEEPALIVE) on coord socket");
              }
            #endif

            // receive bootstrapping info
            AMUDP_SPMDSlaveSocket[numSlavesAttached] = newcoord;
            recvAll(newcoord, &(AMUDP_SPMDTranslationTable[numSlavesAttached].name), sizeof(en_t));

            numSlavesAttached++;
            if (numSlavesAttached == AMUDP_SPMDNUMPROCS) { // all have now reported in, so we can begin computation
              // close listener
              close_socket(AMUDP_SPMDListenSocket);
              allList.remove(AMUDP_SPMDListenSocket);
              AMUDP_SPMDListenSocket = INVALID_SOCKET;

              // transmit bootstrapping info
              for (int i=0; i < AMUDP_SPMDNUMPROCS; i++) {
                // fill out process-specific bootstrap info
                bootstrapinfo.procid = i;
                bootstrapinfo.tag = AMUDP_SPMDTranslationTable[i].tag;
                // send it
                sendAll(AMUDP_SPMDSlaveSocket[i], &bootstrapinfo, sizeof(bootstrapinfo));
                sendAll(AMUDP_SPMDSlaveSocket[i], AMUDP_SPMDTranslationTable, bootstrapinfo.translationtablesz);
                sendAll(AMUDP_SPMDSlaveSocket[i], AMUDP_SPMDMasterEnvironment, bootstrapinfo.environtablesz);
                }
              #if AMUDP_DEBUG_VERBOSE
                printf("Endpoint table (nproc=%i):\n", AMUDP_SPMDNUMPROCS);
                for (int j=0; j < AMUDP_SPMDNUMPROCS; j++) {
                  char temp[80];
                  printf(" P#%i:\t%s", j, AMUDP_enStr(AMUDP_SPMDTranslationTable[j].name, temp));
                  printf("\ttag: %s\n", AMUDP_tagStr(AMUDP_SPMDTranslationTable[j].tag, temp));
                  }
              #endif
              }
            }
          else ErrMessage("master detected some unrecognized activity on AMUDP_SPMDListenSocket");
          }
        //------------------------------------------------------------------------------------
        // coord sockets
        if ((numset = coordList.getIntersection(psockset, tempSockArr, AMUDP_SPMDNUMPROCS))) { // we have some active coord sockets
          //DEBUG_MASTER("got some activity on coord sockets");
          for (int i=0; i < numset; i++) {
            SOCKET s = tempSockArr[i];
            AMUDP_assert(FD_ISSET(s, psockset));
            if (isClosed(s)) {
              DEBUG_MASTER("dropping a coordList socket...\n");
              coordList.remove(s);
              allList.remove(s);

              #if ABORT_JOB_ON_NODE_FAILURE
                int exitCode = -1;
                for (int i=0; i < (int)coordList.getCount(); i++) {
                  sendAll(coordList[i], "E");
                  sendAll(coordList[i], &exitCode, sizeof(int));
                  close_socket(coordList[i]);
                  }
                if (!socklibend()) ErrMessage("master failed to socklibend()");
                DEBUG_MASTER("Lost a worker process - job aborting...");
                exit(exitCode);
              #endif
              continue;
              }
            char command;
            recvAll(s, &command, 1);
            switch(command) {
              case 'B': { // enter barrier
                static int AMUDP_SPMDBarrierCount = 0; /* number of processors that have entered barrier */
                AMUDP_SPMDBarrierCount++;
                if (AMUDP_SPMDBarrierCount == AMUDP_SPMDNUMPROCS) { // barrier complete
                  DEBUG_MASTER("Completed barrier");
                  // broadcast completion message
                  for (int i=0; i < (int)coordList.getCount(); i++) {
                    sendAll(coordList[i], "B");
                    }
                  AMUDP_SPMDBarrierCount = 0;
                  }
                break;
                }

              case 'G': { // enter gather
                static int AMUDP_SPMDGatherCount = 0; /* number of processors that have sent gather messages */
                static int AMUDP_SPMDGatherLen = 0;
                static char *AMUDP_SPMDGatherBuf = NULL;
                int len=0;
                int id=0;
                try {
                  recvAll(s, &id, sizeof(int));
                  recvAll(s, &len, sizeof(int));
                }
                catch (xSocket& exn) {
                  ErrMessage("got exn while reading gather len: %s", exn.why());
                }
                AMUDP_assert(id >= 0 && id < AMUDP_SPMDNUMPROCS && len > 0);
                if (AMUDP_SPMDGatherCount == 0) { // first slave to report
                  AMUDP_assert(AMUDP_SPMDGatherBuf == NULL && AMUDP_SPMDGatherLen == 0);
                  AMUDP_SPMDGatherLen = len;
                  AMUDP_SPMDGatherBuf = new char[AMUDP_SPMDGatherLen*AMUDP_SPMDNUMPROCS];
                } else AMUDP_assert(len == AMUDP_SPMDGatherLen);
                try {
                  recvAll(s, &(AMUDP_SPMDGatherBuf[AMUDP_SPMDGatherLen*id]), AMUDP_SPMDGatherLen);
                }
                catch (xSocket& exn) {
                  ErrMessage("got exn while reading gather data: %s", exn.why());
                }
                AMUDP_SPMDGatherCount++;
                if (AMUDP_SPMDGatherCount == AMUDP_SPMDNUMPROCS) { // gather complete
                  DEBUG_MASTER("Completed gather");
                  // broadcast completion data
                  for (int i=0; i < (int)coordList.getCount(); i++) {
                    sendAll(coordList[i], "G");
                    sendAll(coordList[i], &len, sizeof(int));
                    sendAll(coordList[i], AMUDP_SPMDGatherBuf, AMUDP_SPMDGatherLen*AMUDP_SPMDNUMPROCS);
                  }
                  delete AMUDP_SPMDGatherBuf;
                  AMUDP_SPMDGatherBuf = NULL;
                  AMUDP_SPMDGatherCount = 0;
                  AMUDP_SPMDGatherLen = 0;
                }
                break;
              }

            #ifdef UETH
              case 'F': { // NIC fail-over
                // get relevant en_t's
                en_t olden;
                en_t newen;
                int failedidx=1;
                try {
                  recvAll(s, &failedidx, sizeof(int));
                  recvAll(s, &olden, sizeof(en_t));
                  recvAll(s, &newen, sizeof(en_t));
                  }
                catch (xSocket& exn) {
                  ErrMessage("got exn while reading fail-over addresses: %s", exn.why());
                  }
                // update our local table 
                int j;
                for (j = 0; j < AMUDP_SPMDNUMPROCS; j++) {
                  if (enEqual(AMUDP_SPMDTranslationTable[j].name, olden)) {
                    AMUDP_SPMDTranslationTable[j].name = newen;
                    break;
                    }
                  }
                if (j == AMUDP_SPMDNUMPROCS) ErrMessage("unrecognized endpoint received in fail-over message");
                if (j != failedidx) ErrMessage("mismatched slaveid in fail-over message");
                // tell all slaves about the change
                for (int i=0; i < (int)coordList.getCount(); i++) {
                  sendAll(coordList[i], "F");
                  sendAll(coordList[i], failedidx, sizeof(int));
                  sendAll(coordList[i], &olden, sizeof(en_t));
                  sendAll(coordList[i], &newen, sizeof(en_t));
                }
                #if AMUDP_DEBUG_VERBOSE
                  char temp[80];
                  printf("master: processed NIC failover on slave %i: ", j);
                  printf("%s ->", AMUDP_enStr(olden, temp));
                  printf(" %s\n", AMUDP_enStr(newen, temp));
                #endif
                break;
                }

              case 'A': { // NIC fail-over acknowledgement - bounce to slave
                // get relevant en_t's
                int failedidx=1;
                try {
                  recvAll(s, &failedidx, sizeof(int));

                  AMUDP_assert(failedidx > 0 && failedidx < AMUDP_SPMDNUMPROCS);

                  sendAll(coordList[failedidx], "A");
                  sendAll(coordList[failedidx], &failedidx, sizeof(int));
                }
                catch (xSocket& exn) {
                  ErrMessage("got exn while handling fail-over ack: %s", exn.why());
                }
                break;
              }
            #endif

              case 'E': { // exit code
                // get slave terminate code
                int exitCode = -1;
                try {
                  recvAll(s, &exitCode, sizeof(int));
                  }
                catch (xSocket& exn) {
                  ErrMessage("got exn while reading exit code: %s", exn.why());
                  }
                // tell all other slaves to terminate
                // TODO: perhaps use an active message for this? for now, just rely on coord socket dying
                // TODO: it's possblie we can lose some final output because coord and std are asynchronous
                for (int i=0; i < (int)coordList.getCount(); i++) {
                  sendAll(coordList[i], "E");
                  sendAll(coordList[i], &exitCode, sizeof(int));
                  close_socket(coordList[i]);
                  }
                if (!socklibend()) ErrMessage("master failed to socklibend()");
                #if AMUDP_DEBUG_VERBOSE
                  printf("Exiting after AMUDP_SPMDExit(%i)...\n", exitCode);
                #endif
                exit(exitCode);
                break;
                }

              default:
                ErrMessage("master got an unknown command on coord socket: %c", command);
              }
            }
          if (coordList.getCount() == 0) {
            DEBUG_MASTER("Exiting after losing all worker slave connections (noone called AMUDP_Exit())\n");
            exit(0); // program exit, noone called terminate
            }
          }
        //------------------------------------------------------------------------------------
        } // loop
      }
    catch (xSocket& exn) {
      ErrMessage("Master got an xSocket: %s", exn.why());
      exit(1);
      }
    catch (xBase& exn) {
      ErrMessage("Master got an xBase: %s", exn.why());
      exit(1);
      }
    }
  /* ------------------------------------------------------------------------------------ 
   *  I'm a worker slave 
   * ------------------------------------------------------------------------------------ */
  else {  
    int temp;

    #if FREEZE_SLAVE
      freezeForDebugger();
    #else
      /* do *not* use prefixed getenv here - want an independent freeze point */
      if (getenv("AMUDP_FREEZE")) freezeForDebugger();
    #endif

    if (!eb || !ep) AMUDP_RETURN_ERR(BAD_ARG);
    if (AM_Init() != AM_OK) {
      ErrMessage("Failed to AM_Init() in AMUDP_SPMDStartup");
      AMUDP_RETURN_ERRFR(RESOURCE, AMUDP_SPMDStartup, "AM_Init() failed");
      }

    // parse special args 
    SockAddr masterAddr;
    if ((*argc) < 3) ErrMessage("Missing arguments to slave process");
    {
      #if USE_NUMERIC_MASTER_ADDR
        masterAddr = SockAddr((*argv)[2]);
      #else
        char *IPStr = new char[strlen((*argv)[2])+10];
        strcpy(IPStr, (*argv)[2]);
        char *portStr = strchr(IPStr, ':');
        if (!portStr) {
          ErrMessage("Malformed address argument passed to slave:'%s' (missing port)", (*argv)[2]);
          AMUDP_RETURN_ERR(BAD_ARG);
          }
        int masterPort = atoi(portStr+1);
        if (masterPort < 1 || masterPort > 65535) {
          ErrMessage("Malformed address argument passed to slave:'%s' (bad port=%i)", (*argv)[2], masterPort);
          AMUDP_RETURN_ERR(BAD_ARG);
          }
        (*portStr) = '\0';
        try {
          masterAddr = SockAddr((uint32_t)DNSLookup(IPStr).IP(), (uint16_t)masterPort);
          }
        catch (xSocket &exn) {
          AMUDP_RETURN_ERRFR(RESOURCE, "slave failed DNSLookup on master host name", exn.why());
          }
        delete IPStr;
      #endif
      (*argv)[2] = (*argv)[0]; // strip off our special args
      (*argv) += 2;
      (*argc) -= 2;
      }

    try {
      #if AMUDP_DEBUG_VERBOSE
        fprintf(stderr, "slave connecting to %s:%i\n", masterAddr.IPStr(), masterAddr.port());
        fflush(stderr);
      #endif

      AMUDP_SPMDControlSocket = connect_socket(masterAddr);

      #if USE_COORD_KEEPALIVE
      { // make sure we get connection termination notification in a timely manner
        int val = 1;
        if (setsockopt(AMUDP_SPMDControlSocket, SOL_SOCKET, SO_KEEPALIVE, (char *)&val, sizeof(int)) == SOCKET_ERROR)
          DEBUG_MASTER("failed to setsockopt(SO_KEEPALIVE) on coord socket");
        }
      #endif

      #ifndef UETH
        /* here we assume the interface used to contact the master is the same 
           one to be used for UDP endpoints */
        SockAddr myinterface = getsockname(AMUDP_SPMDControlSocket);
        AMUDP_SetUDPInterface(myinterface.IP());
      #endif
        
      /* create endpoint and get name */
      temp = AM_AllocateBundle(AM_SEQ, &AMUDP_SPMDBundle);
      if (temp != AM_OK) {
        ErrMessage("Failed to create bundle in AMUDP_SPMDStartup");
        AMUDP_RETURN(temp);
        }
      temp = AM_AllocateEndpoint(AMUDP_SPMDBundle, &AMUDP_SPMDEndpoint, &AMUDP_SPMDName);
      if (temp != AM_OK) {
        ErrMessage("Failed to create endpoint in AMUDP_SPMDStartup");
        AMUDP_RETURN(temp);
        }

      // send our endpoint name to the master
      sendAll(AMUDP_SPMDControlSocket, &AMUDP_SPMDName, sizeof(AMUDP_SPMDName));

      // get information from master 
      // get the bootstrap info and translation table
      AMUDP_SPMDBootstrapInfo_t bootstrapinfo;
      recvAll(AMUDP_SPMDControlSocket, &bootstrapinfo, sizeof(AMUDP_SPMDBootstrapInfo_t));
      
      // unpack the bootstrapping info
      AMUDP_SPMDNUMPROCS = bootstrapinfo.numprocs;
      AMUDP_SPMDMYPROC = bootstrapinfo.procid;
      if (networkpid) *networkpid = bootstrapinfo.networkpid;

      // sanity checking on bootstrap info
      AMUDP_assert(AMUDP_SPMDNUMPROCS > 0);
      AMUDP_assert(AMUDP_SPMDMYPROC >= 0 && AMUDP_SPMDMYPROC < AMUDP_SPMDNUMPROCS);
      AMUDP_assert(bootstrapinfo.translationtablesz == bootstrapinfo.numprocs * sizeof(amudp_translation_t)); 
      AMUDP_assert(AMUDP_SPMDMYPROC >= 0 && AMUDP_SPMDMYPROC < AMUDP_SPMDNUMPROCS);

      amudp_translation_t *tempTranslationTable = (amudp_translation_t *)malloc(bootstrapinfo.translationtablesz);
      AMUDP_assert(tempTranslationTable != NULL);
      recvAll(AMUDP_SPMDControlSocket, tempTranslationTable, bootstrapinfo.translationtablesz);

      AMUDP_assert(tempTranslationTable[AMUDP_SPMDMYPROC].tag == bootstrapinfo.tag);
      AMUDP_assert(enEqual(tempTranslationTable[AMUDP_SPMDMYPROC].name, AMUDP_SPMDName));

      // setup translation table
      for (int i = 0; i < bootstrapinfo.numprocs; i++) {
        temp = AM_Map(AMUDP_SPMDEndpoint, i, tempTranslationTable[i].name, tempTranslationTable[i].tag);
        if (temp != AM_OK) {
          ErrMessage("Failed to AM_Map() in AMUDP_SPMDStartup");
          AMUDP_RETURN(temp);
          }
        }

      free(tempTranslationTable);
      tempTranslationTable = NULL;

      // receive snapshot of master environment
      AMUDP_SPMDMasterEnvironment = (char *)malloc(bootstrapinfo.environtablesz);
      AMUDP_assert(AMUDP_SPMDMasterEnvironment != NULL);
      recvAll(AMUDP_SPMDControlSocket, AMUDP_SPMDMasterEnvironment, bootstrapinfo.environtablesz);
      
      /* allocate network buffers */
      temp = AM_SetExpectedResources(AMUDP_SPMDEndpoint, AMUDP_SPMDNUMPROCS, bootstrapinfo.depth);
      if (temp != AM_OK) {
        ErrMessage("Failed to AM_SetExpectedResources() in AMUDP_SPMDStartup");
        AMUDP_RETURN(temp);
        }
      
      // set tag
      temp = AM_SetTag(AMUDP_SPMDEndpoint, bootstrapinfo.tag);
      if (temp != AM_OK) {
        ErrMessage("Failed to AM_SetTag() in AMUDP_SPMDStartup");
        AMUDP_RETURN(temp);
        }

      #if !DISABLE_STDSOCKET_REDIRECT
        if (bootstrapinfo.stdinMaster) {
            // perform stdin/out/err redirection
            newstdin  = connect_socket(SockAddr(masterAddr.IP(),bootstrapinfo.stdinMaster));
            newstdout = connect_socket(SockAddr(masterAddr.IP(),bootstrapinfo.stdoutMaster));
            newstderr = connect_socket(SockAddr(masterAddr.IP(),bootstrapinfo.stderrMaster));
            #if 0
              // disable buffering
              setvbuf(stdin, NULL, _IONBF, 0);
              setvbuf(stdout, NULL, _IONBF, 0);
              setvbuf(stderr, NULL, _IONBF, 0);
            #endif
            #ifdef WIN32
              #if 0
              // not sure how to do this on Win32 yet - maybe use _fdopen() and/or _fileno()
              {FILE* newf;
               if( ( newf = fopen( "c:\\data", "w" ) ) == NULL )
               {
                  puts( "Can't open file 'data'\n" );
                  exit( 1 );
               }
               if( ( newf = _fdopen( newstdout, "w" ) ) == NULL )
               {
                  puts( "fdopen failed\n" );
                  exit( 1 );
               }
              if (dup2(_fileno(newf), FD_STDOUT) < 0) { // redirect stdout to socket
                perror("dup2(stdout)");
                _exit(1); 
                }
              printf("yomama\n");
              fflush(stdout);
              fclose(newf);
              exit(0);
                }
              #endif
            #else
              /* UNIX */
              if (dup2(newstdin, FD_STDIN) < 0) { // redirect stdout to socket
                perror("dup2(stdin)");
                _exit(1); 
                }
              if (dup2(newstdout, FD_STDOUT) < 0) { // redirect stdout to socket
                perror("dup2(stdout)");
                _exit(1); 
                }
              if (dup2(newstderr, FD_STDERR) < 0) { // redirect stdout to socket
                perror("dup2(stderr)");
                _exit(1); 
                }
            #endif
          }
     #endif

      if (bootstrapinfo.faultInjectionRate != 0.0) {
        AMUDP_FaultInjectionEnabled = 1;
        AMUDP_FaultInjectionRate = bootstrapinfo.faultInjectionRate;
        fprintf(stderr, "*** Warning: AMUDP running with fault injection enabled. Rate = %6.2f %%\n",
          100.0 * AMUDP_FaultInjectionRate);
        fflush(stderr);
        }
      }
    catch (xSocket& exn) {
      ErrMessage("Got an xSocket while spawning slave process: %s", exn.why());
      exit(1);
      }

    *eb = AMUDP_SPMDBundle;
    *ep = AMUDP_SPMDEndpoint;
    AMUDP_SPMDStartupCalled = 1;

    #if USE_ASYNC_TCP_CONTROL
      // enable async notification
      reghandler(AMUDP_SIGIO, AMUDP_SPMDControlSocketCallback);
      if (fcntl(AMUDP_SPMDControlSocket, F_SETOWN, getpid())) {
        perror("fcntl(F_SETOWN, getpid())");
        ErrMessage("Failed to fcntl(F_SETOWN, getpid()) on TCP control socket - try disabling USE_ASYNC_TCP_CONTROL");
        abort();
        }
      if (fcntl(AMUDP_SPMDControlSocket, F_SETSIG, AMUDP_SIGIO)) {
        ErrMessage("Failed to fcntl(F_SETSIG, AMUDP_SIGIO) on TCP control socket - try disabling USE_ASYNC_TCP_CONTROL");
        perror("fcntl(F_SETSIG)");
        abort();
        }
      if (fcntl(AMUDP_SPMDControlSocket, F_SETFL, O_ASYNC|O_NONBLOCK)) { 
        perror("fcntl(F_SETFL, O_ASYNC|O_NONBLOCK)");
        ErrMessage("Failed to fcntl(F_SETFL, O_ASYNC|O_NONBLOCK) on TCP control socket - try disabling USE_ASYNC_TCP_CONTROL");
        abort();
        }
    #endif

    #if AMUDP_DEBUG_VERBOSE
    { char temp[80];
      tag_t tag;
      AM_GetTag(AMUDP_SPMDEndpoint, &tag);
      fprintf(stderr, "Slave %i/%i starting (tag=%s)...\n", 
        AMUDP_SPMDMyProc(), AMUDP_SPMDNumProcs(), AMUDP_tagStr(tag, temp));
      fflush(stderr);
      }
    #endif

    return AM_OK;
    }
  /* ------------------------------------------------------------------------------------ */
  abort(); /* never reach here */
  return AM_OK;
  }

/* ------------------------------------------------------------------------------------ 
 *  worker control handler
 * ------------------------------------------------------------------------------------ */
// called by slave to handle traffic on control socket
// sets controlMessagesServiced to indicate how many message serviced
extern int AMUDP_SPMDHandleControlTraffic(int *controlMessagesServiced) {
  if (AMUDP_SPMDControlSocket == INVALID_SOCKET) return AM_OK; // not running in SPMD mode
  #if USE_ASYNC_TCP_CONTROL
    if (!AMUDP_SPMDIsActiveControlSocket) return AM_OK; // nothing to do
    ASYNC_TCP_DISABLE();
    AMUDP_SPMDIsActiveControlSocket = FALSE; 
  #endif 
  if (controlMessagesServiced) *controlMessagesServiced = 0;
  
  while (1) { // service everything waiting
    try {
      if (!inputWaiting(AMUDP_SPMDControlSocket)) {
        ASYNC_TCP_ENABLE();
        return AM_OK; // nothing more to do
        }
      } 
    catch (xBase &exn) {
      ErrMessage("Error checking AMUDP_SPMDControlSocket: %s", exn.why()); // probably conn reset
      }

    try {
      SOCKET s = AMUDP_SPMDControlSocket;

      if (isClosed(s)) {
        DEBUG_SLAVE("master control socket slammed shut. Exiting...\n");
        AMUDP_SPMDShutdown(1);
        }

      // there's something waiting on the control socket for us - grab it
      char command;
      recvAll(s, &command, 1);
      switch(command) {
        case 'B': { // barrier complete
          AMUDP_assert(!AMUDP_SPMDBarrierDone);
          AMUDP_SPMDBarrierDone = 1; // flag completion
          break;
        }

        case 'G': { // gather complete
          AMUDP_assert(!AMUDP_SPMDBarrierDone && AMUDP_SPMDGatherLen > 0 && AMUDP_SPMDGatherData != NULL);
          try {
            int len = -1;
            recvAll(s, &len, sizeof(int));
            AMUDP_assert(len == AMUDP_SPMDGatherLen);
            recvAll(s, AMUDP_SPMDGatherData, AMUDP_SPMDGatherLen*AMUDP_SPMDNUMPROCS);
          }
          catch (xSocket& exn) {
            ErrMessage("got exn while reading gather data: %s", exn.why());
            exit(1);
          }
          AMUDP_SPMDGatherDone = 1; // flag completion
          break;
        }

      #ifdef UETH
        case 'F': { // NIC fail-over
          // get relevant en_t's
          en_t olden;
          en_t newen;
          int failidx = -1;
          try {
            recvAll(s, &failidx, sizeof(int));
            recvAll(s, &olden, sizeof(en_t));
            recvAll(s, &newen, sizeof(en_t));
            }
          catch (xSocket& exn) {
            ErrMessage("got exn while reading fail-over addresses: %s", exn.why());
            exit(1);
            }

          // this update could be rather slow, but we expect it to run extremely infrequently
          DEBUG_SLAVE("Received a NIC fail-over notification. Updating tables...");

          // update all translation tables
          for (int i = 0; i < AMUDP_SPMDBundle->n_endpoints; i++) {
            ep_t ep = AMUDP_SPMDBundle->endpoints[i];
            AMUDP_assert(ep);

            for (int j = 0; j < AMUDP_MAX_NUMTRANSLATIONS; j++) {
              if (ep->translation[j].inuse) {
                if (enEqual(ep->translation[j].name, olden)) { // need to re-map
                  ep->translation[j].name = newen;
                  }
                }
              }
            for (int procid = 0; procid < ep->P; procid++) {
              if (enEqual(ep->perProcInfo[procid].remoteName, olden)) { // need to re-map
                AMUDP_assert(procid == failidx);
                ep->perProcInfo[procid].remoteName = newen;
                /* need to remap the preset request/reply destinations */
                for (int inst = 0; inst < ep->depth; inst++) {
                  amudp_bufdesc_t *reqdesc = GET_REQ_DESC(ep, procid, inst);
                  amudp_bufdesc_t *repdesc = GET_REP_DESC(ep, procid, inst);
                  amudp_buf_t *reqbuf = GET_REQ_BUF(ep, procid, inst);
                  amudp_buf_t *repbuf = GET_REP_BUF(ep, procid, inst);

                  if (reqdesc->transmitCount > 0)
                    ueth_cancel_send(reqbuf, reqbuf->bufhandle);
                  if (ueth_set_packet_destination(reqbuf, &newen) != UETH_OK)
                    ErrMessage("ueth_set_packet_destination failed on NIC fail-over");

                  if (repdesc->transmitCount > 0)
                    ueth_cancel_send(repbuf, repbuf->bufhandle);
                  if (ueth_set_packet_destination(repbuf, &newen) != UETH_OK)
                    ErrMessage("ueth_set_packet_destination failed on NIC fail-over");
                }
                }
              }

            #ifndef UETH
              // update any messages already accepted into the rx buffers from this guy
              // this currently never runs because UETH has no explicit receive buffers we can see
              for (int i = 0; i < AMUDP_SPMDBundle->n_endpoints; i++) {
                ep_t ep = AMUDP_SPMDBundle->endpoints[i];
                AMUDP_assert(ep);

                for (int j = ep->rxReadyIdx; j != ep->rxFreeIdx; j = (j+1)%ep->rxNumBufs) {
                  if (enEqual(ep->rxBuf[j].source, olden) {
                    ep->rxBuf[j].source = newen;
                    }
                  }
                }
            #endif
            }
          DEBUG_SLAVE("Update complete.");
          #if AMUDP_DEBUG_VERBOSE
            char temp[80];
            printf("slave: handled NIC failover: ");
            printf("%s ->", AMUDP_enStr(olden, temp));
            printf(" %s\n", AMUDP_enStr(newen, temp));
          #endif

          try { // send acknowledgement to master
            sendAll(s, "A");
            sendAll(s, &failidx, sizeof(int));
            }
          catch (xSocket& exn) {
            ErrMessage("Slave got an xSocket sending failure ACK: %s. Exiting...", exn.why());
            AMUDP_SPMDShutdown(1);
            }
          break;
          }
          case 'A': { // NIC fail-over acknowledgement - record an ACK
            int failedidx=1;
            try {
              recvAll(s, &failedidx, sizeof(int));

              AMUDP_assert(failedidx == AMUDP_SPMDMYPROC);
              AMUDP_assert(AMUDP_FailoverAcksOutstanding > 0);

              AMUDP_FailoverAcksOutstanding--;
              }
            catch (xSocket& exn) {
              ErrMessage("got exn while handling fail-over ack: %s", exn.why());
              }
          break;
          }
      #endif

        case 'E': { // exit code
          // get slave terminate code
          int exitCode = -1;
          try {
            recvAll(s, &exitCode, sizeof(int));
            }
          catch (xSocket& exn) {
            ErrMessage("got exn while reading exit code: %s", exn.why());
            }
          #if AMUDP_DEBUG_VERBOSE
            printf("Exiting after exit signal from master (%i)...\n", exitCode);
          #endif
          AMUDP_SPMDShutdown(exitCode);
          break;
          }

        default:
          ErrMessage("slave got an unknown command on coord socket: %c", command);
          exit(1); // this is a fatal error
        }
      }
    catch (xSocket& exn) {
      ErrMessage("Slave got an xSocket: %s. Exiting...", exn.why());
      AMUDP_SPMDShutdown(1);
      }
    catch (xBase& exn) {
      ErrMessage("Slave got an xBase: %s. Exiting...", exn.why());
      AMUDP_SPMDShutdown(1);
      }
    if (controlMessagesServiced) (*controlMessagesServiced)++;
    }
  }
/* ------------------------------------------------------------------------------------ 
 *  handler for NIC fail-over
 * ------------------------------------------------------------------------------------ */
#ifdef UETH
extern void AMUDP_SPMDAddressChangeCallback(ueth_addr_t *address) {
  DEBUG_SLAVE("AMUDP_SPMDAddressChangeCallback() called.. Fail-over starting...");
  AMUDP_assert(AMUDP_UETH_endpoint);
  AMUDP_assert(address);
  en_t olden = AMUDP_UETH_endpoint->name;
  en_t newen = *address;

  AMUDP_UETH_endpoint->name = newen;
  AMUDP_SPMDName = newen;

  // send change to master, who will propagate new address info to peers and back to us
  // we update our translation table on recieving the reflection from the master
  AMUDP_FailoverAcksOutstanding = AMUDP_SPMDNUMPROCS;
  try {
    ASYNC_TCP_DISABLE();
    sendAll(AMUDP_SPMDControlSocket, "F");
    sendAll(AMUDP_SPMDControlSocket, &AMUDP_SPMDMYPROC, sizeof(int));
    sendAll(AMUDP_SPMDControlSocket, &olden, sizeof(en_t));
    sendAll(AMUDP_SPMDControlSocket, &newen, sizeof(en_t));
    ASYNC_TCP_ENABLE();
    }
  catch (xSocket& exn) {
    ErrMessage("Slave got an xSocket: %s. Exiting...", exn.why());
    AMUDP_SPMDShutdown(1);
    }
  catch (xBase& exn) {
    ErrMessage("Slave got an xBase: %s. Exiting...", exn.why());
    AMUDP_SPMDShutdown(1);
    }

  ep_t ep = AMUDP_UETH_endpoint;
  /* need to remap all preset request/reply destinations for failed node */
  for (int inst = 0; inst < ep->depth; inst++) {
    for (int procid = 0; procid < ep->P; procid++) {
      amudp_bufdesc_t *reqdesc = GET_REQ_DESC(ep, procid, inst);
      amudp_bufdesc_t *repdesc = GET_REP_DESC(ep, procid, inst);
      amudp_buf_t *reqbuf = GET_REQ_BUF(ep, procid, inst);
      amudp_buf_t *repbuf = GET_REP_BUF(ep, procid, inst);

      if (reqdesc->transmitCount > 0)
        ueth_cancel_send(reqbuf, reqbuf->bufhandle);
      if (ueth_set_packet_destination(reqbuf, &ep->perProcInfo[procid].remoteName) != UETH_OK)
        ErrMessage("ueth_set_packet_destination failed on NIC fail-over");

      if (repdesc->transmitCount > 0)
        ueth_cancel_send(repbuf, repbuf->bufhandle);
      if (ueth_set_packet_destination(repbuf, &ep->perProcInfo[procid].remoteName) != UETH_OK)
        ErrMessage("ueth_set_packet_destination failed on NIC fail-over");
    }
  }

  // update any messages already accepted into the rx buffers
  #ifdef UETH
    int packetschanged = ueth_fixup_recv(&olden, &newen);
    if (packetschanged < 0)
      ErrMessage("ueth_fixup_recv failed on NIC fail-over");
  #else
    // this currently never runs because UETH has no explicit receive buffers we can see
    for (int i = 0; i < AMUDP_SPMDBundle->n_endpoints; i++) {
      ep_t ep = AMUDP_SPMDBundle->endpoints[i];
      AMUDP_assert(ep);

      for (int j = ep->rxReadyIdx; j != ep->rxFreeIdx; j = (j+1)%ep->rxNumBufs) {
        if (enEqual(ep->rxBuf[j].dest, olden) {
          ep->rxBuf[j].dest = newen;
          }
        }
      }
  #endif

  // wait until all the slaves recieve the failover notification and acknowledge
  // before we allow this node to continue transmitting on the new NIC
  // can't use a regular barrier here because it may cause us to poll
  while (AMUDP_FailoverAcksOutstanding > 0) {
    int junk=0;
    AMUDP_SPMDHandleControlTraffic(&junk);
    sched_yield();
  }

  DEBUG_SLAVE("Fail-over complete.");
  }
#endif
/* ------------------------------------------------------------------------------------ 
 *  process termination
 * ------------------------------------------------------------------------------------ */
static void (*AMUDP_SPMDExitCallback)(int) = NULL;
extern int AMUDP_SPMDSetExitCallback(void (*fp)(int)) {
  AMUDP_SPMDExitCallback = fp;
  return AM_OK;
}
extern "C" {
  void (*AMUDP_SPMDkillmyprocess)(int) = &_exit;
}

/* shutdown this process */
static int AMUDP_SPMDShutdown(int exitcode) {
  ASYNC_TCP_DISABLE();
  /* this function is not re-entrant - if someone tries, something is seriously wrong */
  { static int shutdownInProgress = FALSE;
    if (shutdownInProgress) abort(); 
    shutdownInProgress = TRUE;
  }

  flushStreams("AMUDP_SPMDShutdown");

  if (AMUDP_SPMDExitCallback) (*AMUDP_SPMDExitCallback)(exitcode);

  /* important to make this call to release resources */
  if (AM_Terminate() != AM_OK) 
    ErrMessage("failed to AM_Terminate() in AMUDP_SPMDExit()");

  flushStreams("AMUDP_SPMDShutdown");

  if (fclose(stdin)) {
    ErrMessage("failed to fclose stdout in AMUDP_SPMDExit()"); 
    perror("fclose");
  }
  if (fclose(stdout)) {
    ErrMessage("failed to fclose stdout in AMUDP_SPMDExit()"); 
    perror("fclose");
  }
  if (fclose(stderr)) {
    ErrMessage("failed to fclose stderr in AMUDP_SPMDExit()"); 
    perror("fclose");
  }

  /* use normal shutdown and closesocket to ignore errors */
  if (newstdin != INVALID_SOCKET) {
    shutdown(newstdin, SHUT_RDWR);
    closesocket(newstdin); 
  }
  if (newstdout != INVALID_SOCKET) {
    shutdown(newstdout, SHUT_RDWR);
    closesocket(newstdout); 
  }
  if (newstderr != INVALID_SOCKET) {
    shutdown(newstderr, SHUT_RDWR);
    closesocket(newstderr);
  }

  sched_yield();

  if (AMUDP_SPMDControlSocket != INVALID_SOCKET) {
    closesocket(AMUDP_SPMDControlSocket);
  }

  if (!socklibend()) ErrMessage("slave failed to socklibend()");

  AMUDP_SPMDStartupCalled = 0;
  DEBUG_SLAVE("exiting..");
  AMUDP_SPMDkillmyprocess(exitcode);
  abort();
  return AM_OK;
}

extern int AMUDP_SPMDExit(int exitcode) {
  DEBUG_SLAVE("AMUDP_SPMDExit");
  if (!AMUDP_SPMDStartupCalled) AMUDP_RETURN_ERR(NOT_INIT);

  ASYNC_TCP_DISABLE();
  /* this function is not re-entrant - if someone tries, something is seriously wrong */
  { static int exitInProgress = FALSE;
    if (exitInProgress) abort(); 
    exitInProgress = TRUE;
  }

  flushStreams("AMUDP_SPMDExit");

  sched_yield();

  /* notify master we're exiting */
  try {
    sendAll(AMUDP_SPMDControlSocket, "E");
    sendAll(AMUDP_SPMDControlSocket, &exitcode, sizeof(int));
    while (1) { // swallow everything and wait for master to close
      char temp;
      int retval = recv(AMUDP_SPMDControlSocket, &temp, 1, 0); 
      if (retval == 0 || retval == SOCKET_ERROR) break;
      }
    }
  catch (xBase& ) { } // ignore errors that may happen on conn reset 

  AMUDP_SPMDStartupCalled = 0;
  DEBUG_SLAVE("AMUDP_SPMDShutdown..");
  /* exit this proc */
  AMUDP_SPMDShutdown(exitcode);
  abort();
  return AM_OK;
}
/* ------------------------------------------------------------------------------------ 
 *  poll-wait for a flag to become non-zero as a result of a control message
 * ------------------------------------------------------------------------------------ */
static void AMUDP_SPMDWaitForControl(volatile int *done) {
  #if USE_BLOCKING_SPMD_BARRIER
    { int oldmask;
      AM_GetEventMask(AMUDP_SPMDBundle, &oldmask);
      // wait for completion
      AM_Poll(AMUDP_SPMDBundle);
      while (!*done) {
        AM_SetEventMask(AMUDP_SPMDBundle, AM_NOTEMPTY);
        AMUDP_SPMDwakeupOnControlActivity = 1;
        AM_WaitSema(AMUDP_SPMDBundle);
        AMUDP_SPMDwakeupOnControlActivity = 0;
        AM_Poll(AMUDP_SPMDBundle);
      }
      AM_SetEventMask(AMUDP_SPMDBundle, oldmask);
    }
  #else
  {
    uint32_t timeoutusec = 100;
    AM_Poll(AMUDP_SPMDBundle);
    while (!*done) {

      struct timeval tv;
      tv.tv_sec  = timeoutusec / 1000000;
      tv.tv_usec = timeoutusec % 1000000;
      select(1, NULL, NULL, NULL, &tv); /* sleep a little while */

      AM_Poll(AMUDP_SPMDBundle);
      if (timeoutusec < 10000) timeoutusec *= 2;
    }
  }
  #endif
}
/* ------------------------------------------------------------------------------------ 
 *  barrier
 * ------------------------------------------------------------------------------------ */
extern int AMUDP_SPMDBarrier() {
  if (!AMUDP_SPMDStartupCalled) {
    ErrMessage("called AMUDP_SPMDBarrier before AMUDP_SPMDStartup()");
    AMUDP_RETURN_ERR(NOT_INIT);
  }

  flushStreams("AMUDP_SPMDBarrier");
  AMUDP_assert(AMUDP_SPMDBarrierDone == 0);
  ASYNC_TCP_DISABLE();
  sendAll(AMUDP_SPMDControlSocket, "B");
  ASYNC_TCP_ENABLE();

  AMUDP_SPMDWaitForControl(&AMUDP_SPMDBarrierDone);

  AMUDP_SPMDBarrierDone = 0;
  DEBUG_SLAVE("Leaving barrier");
  return AM_OK;
}
/* ------------------------------------------------------------------------------------ 
 *  AMUDP_SPMDAllGather: gather len bytes from source buf on each node, concatenate them and write 
 *  them into the dest buffer (which must have length len*numnodes) in rank order
 * ------------------------------------------------------------------------------------ */
extern int AMUDP_SPMDAllGather(void *source, void *dest, size_t len) {
  int mylen = len;
  if (!AMUDP_SPMDStartupCalled) {
    ErrMessage("called AMUDP_SPMDAllGather before AMUDP_SPMDStartup()");
    AMUDP_RETURN_ERR(NOT_INIT);
  }
  if (source == NULL) AMUDP_RETURN_ERR(BAD_ARG);
  if (dest == NULL) AMUDP_RETURN_ERR(BAD_ARG);
  if (len <= 0) AMUDP_RETURN_ERR(BAD_ARG);

  AMUDP_assert(AMUDP_SPMDGatherDone == 0);
  AMUDP_SPMDGatherData = dest;
  AMUDP_SPMDGatherLen = len;

  ASYNC_TCP_DISABLE();
  sendAll(AMUDP_SPMDControlSocket, "G");
  sendAll(AMUDP_SPMDControlSocket, &AMUDP_SPMDMYPROC, sizeof(int));
  sendAll(AMUDP_SPMDControlSocket, &mylen, sizeof(int));
  sendAll(AMUDP_SPMDControlSocket, source, mylen);
  ASYNC_TCP_ENABLE();
  
  AMUDP_SPMDWaitForControl(&AMUDP_SPMDGatherDone);

  AMUDP_SPMDGatherDone = 0;
  DEBUG_SLAVE("Leaving gather");
  return AM_OK;
}

/* ------------------------------------------------------------------------------------ 
 *  global getenv()
 * ------------------------------------------------------------------------------------ */
extern const char* AMUDP_SPMDgetenvMaster(const char *keyname) {
  if (!AMUDP_SPMDStartupCalled) {
    ErrMessage("called AMUDP_SPMDgetenvMaster before AMUDP_SPMDStartup()");
    return NULL;
    }

  AMUDP_assert(AMUDP_SPMDMasterEnvironment != NULL);
  char *p = AMUDP_SPMDMasterEnvironment;
  if (!keyname) return NULL;
  int keylen = strlen(keyname);
  while (*p) {
    if (!strncmp(keyname, p, keylen) && p[keylen] == '=') {
      return p + keylen + 1;
      }
    p += strlen(p) + 1;
    }
  return NULL; // not found
  }

char *AMUDP_getenv_prefixed(const char *basekey) {
  char key[3][255];
  const char *val[3];
  int winner = -1;
  char *(*getfn)(const char *) = NULL;
  if (AMUDP_SPMDStartupCalled && AMUDP_SPMDMasterEnvironment != NULL) getfn = (char *(*)(const char *))AMUDP_SPMDgetenvMaster;
  else getfn = (char *(*)(const char *))getenv;

  if (basekey == NULL || !*basekey) return NULL;
  sprintf(key[0], "%s_%s", AMUDP_ENV_PREFIX_STR, basekey);
  val[0] = getfn(key[0]);
  sprintf(key[1], "%s_%s", "AMUDP", basekey);
  val[1] = getfn(key[1]);
  strcpy(key[2], basekey);
  val[2] = getfn(key[2]);
  for (int i=0; i < 3; i++) {
    if (val[i] != NULL) {
      if (winner == -1) winner = i;
      else if (strcmp(val[winner], val[i])) {
        fprintf(stderr,"AMUDP: Warning: both $%s and $%s are set, to different values. Using the former.\n",
          key[winner], key[i]);
      }
    }
  }
  if (winner == -1) return NULL;
  else return (char *)val[winner];
}
/* ------------------------------------------------------------------------------------ */


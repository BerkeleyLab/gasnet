/*   $Source: /Users/kamil/work/gasnet-cvs2/gasnet/other/ssh-spawner/gasnet_bootstrap_ssh.c,v $
 *     $Date: 2005/04/01 20:02:53 $
 * $Revision: 1.31 $
 * Description: GASNet conduit-independent ssh-based spawner
 * Copyright 2005, The Regents of the University of California
 * Terms of use are as specified in license.txt
 */
#include <gasnet_internal.h>
#include <gasnet_core_internal.h>

#include <stdlib.h>
#include <stdio.h>
#include <stdarg.h>
#include <string.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <sys/socket.h>
#include <sys/ioctl.h>
#include <netinet/in.h>
#if HAVE_NETINET_TCP_H
  #include <netinet/tcp.h>
#endif
#if HAVE_PR_SET_PDEATHSIG
  /* Under Linux we can ask to be sent a given signal when our parent exits.
   * We use if mainly because of a bug in some versions of OpenSSH that
   * fail to kill spawned children when the ssh exits.  However, this will
   * also make us more resistant to runaways if somebody starts sending
   * SIGKILL to our processes (which leaves us no other way to cleanup
   * gracefully).  Not safe on some Linux kernels.
   */
  #include <sys/prctl.h>
  #include <signal.h>
#endif
#include <unistd.h>
#include <fcntl.h>
#include <netdb.h>

/* NOTES

   This is a ssh-based (or rsh is you want) spawner for GASNet.  It is
   intended to be conduit-neutral.
  
   In the interest of scalability the ssh processes are started up in
   a balanced N-ary tree, where N is determined at compile-time (the
   value of "OUT_DEGREE", set below.  Typically we want this value to
   be resonably large, since deep trees would result in multiple steps
   of forwarding for standard I/O (which is performed entirely by the
   ssh processes at this point).

   The process corresponding to gasnet node zero is the root of this
   tree, but it also has a parent: the original process started by the
   user (or upcrun, etc.), the "master" process.  This allows for the
   possibility that the spawner is launched somewhere other than a
   compute node, such as a front end node.

   In addition to the tree of ssh connections, there is a TCP socket
   created between each process and its parent (including between the
   root and the master).  This socket is used for control information,
   during startup.  For instance, the the environment and arguments
   are transferred over this socket.

   The control sockets are used to send each process only a portion of
   the list of host names.  Rather than send the entire list, each processs
   receives only its own host name and that of any children it may have in
   the tree.

   The spawner is able to (in most cases) avoid orphaned processes
   by using TCP out-of-band data to generate a SIGURG.  The handler for
   this signal will send the out-of-band data both up and down the tree
   before exiting.

   If a child has the same hostname as its parent, it will be started
   directly, rather than via ssh.

   The tree structure is used to provide scalable implementations of
   the following "service" routines for use during the bootstrap, as
   required in the template-conduit:
      extern void gasneti_bootstrapBarrier(void);
      extern void gasneti_bootstrapExchange(void *src, size_t len, void *dest);
      extern void gasneti_bootstrapBroadcast(void *src, size_t len, void *dest, int rootnode);
   
   Additionally, the following is useful (at least in vapi-conduit)
   for exchanging endpoint identifiers in a scalable manner:
      extern void gasneti_bootstrapAlltoall(void *src, size_t len, void *dest);

   If demand exists, scalable Scatter and Gather are possible.

   The following are needed to handle startup and termination:
      extern void gasneti_bootstrapInit(int *argc_p, char ***argv_p,
                                        gasnet_node_t *nodes_p,
                                        gasnet_node_t *mynode_p);
      extern void gasneti_bootstrapFini(void);
      extern void gasneti_bootstrapAbort(int exitcode);
   In the case of normal termination, all nodes should call
   gasneti_bootstrapFini() before they call exit().  In the event that
   gasnet is unable to arrange for an orderly shutdown, a call to
   gasneti_bootstrapAbort() will try to force all processes to exit
   with the given exit code.

   To deal with global environment propagation, one should
      #define GASNETI_CONDUIT_GETENV gasneti_bootstrapGetenv
   A call to gasneti_setupGlobalEnvironment() is not required.
   A call to gasneti_bootstrapGetenv() can safely be made at anytime,
   but will return NULL until after the call to gasneti_bootstrapInit().

   To control the spawner, there are a few environment variables, all
   of which are processed only by the master process (which send the
   relavent information on to the others via the control sockets).
   See README for documentation on these variables.

   XXX: still to do
   + Complete the "flat tree" implementation (master spawns all procs)
     and make it a runtime switch rather than compile time.
   + Consider making OUT_DEGREE a runtime variable.
   + Implement "custom" spawner in the spirit of udp-conduit.
   + Look at udp-conduit for things missing from this list. :-)
   + We probably leak small strings in a few places.

 */

#define OUT_DEGREE 32
#define USE_LOCAL_SPAWN 1
#define FLAT_TREE 0 /* "flat" collectives not yet implemented */

#ifndef USE_LOCAL_SPAWN
  #define USE_LOCAL_SPAWN 0
#endif
#ifndef FLAT_TREE
  #define FLAT_TREE 0
#endif

#define WHITESPACE " \t\n\r"
#define SSH_SERVERS_DELIM_CHARS  ",/;:" WHITESPACE

extern char **environ;
#ifndef ENV_PREFIX
  #define ENV_PREFIX "GASNET_"
#endif

/* Master & slaves */
  static int is_master = 0;
  static int is_verbose = 0;
  static gasnet_node_t nproc = 0;
  static char cwd[1024];
  static int devnull = -1;
  static int listener = -1;
  static int listen_port = -1;
  static const char *argv0;
  static char **nodelist;
  static char **ssh_argv = NULL;
  static int ssh_argc = 0;
  static char *master_env = NULL;
  static size_t master_env_len = 0;
  static struct child {
    int			sock;
    int			is_local;
    volatile pid_t	pid;	/* pid of ssh (or locally exec()ed app) */
    gasnet_node_t	rank;
    gasnet_node_t	procs;	/* size in procs of subtree rooted at this child */
    gasnet_node_t	nodes;	/* size in nodes of subtree rooted at this child */
    char **		nodelist;
  } *child = NULL;
  static int children = 0;
  static volatile int accepted = 0;
  int finalized = 0;
/* Slaves only */
  static gasnet_node_t myproc = (gasnet_node_t)(-1L);
  static gasnet_node_t tree_procs = (gasnet_node_t)(-1L);
  static gasnet_node_t tree_nodes = (gasnet_node_t)(-1L);
  static int parent = -1; /* socket */
  static int mypid;
/* Master only */
  static volatile int exit_status = -1;
  static gasnet_node_t nnodes = 0;	/* nodes, as distinct from procs */
  static pid_t *all_pids;

static void gather_pids(void);

static void do_verbose(const char *fmt, ...) __attribute__((__format__ (__printf__, 1, 2)));
static void do_verbose(const char *fmt, ...)
{
  va_list args;
  va_start(args, fmt);
  vfprintf(stderr, fmt, args);
  fflush(stderr);
  va_end(args);
}
#define BOOTSTRAP_VERBOSE(ARGS)		if_pf (is_verbose) do_verbose ARGS

static char *sappendf(char *s, const char *fmt, ...) __attribute__((__format__ (__printf__, 2, 3)));
static char *sappendf(char *s, const char *fmt, ...)
{
  va_list args;
  int old_len, add_len;

  /* compute length of thing to append */
  va_start(args, fmt);
  add_len = vsnprintf(NULL, 0, fmt, args);
  va_end(args);

  /* grow the string, including space for '\0': */
  if (s) {
    old_len = strlen(s);
#if 0 /* No gasneti_realloc */
    s = gasneti_realloc(s, old_len + add_len + 1);
#else
    { char *tmp = gasneti_malloc(old_len + add_len + 1);
      memcpy(tmp, s, old_len + 1);
      gasneti_free(s);
      s = tmp;
    }
#endif
  } else {
    old_len = 0;
    s = gasneti_malloc(add_len + 1);
  }

  /* append */
  va_start(args, fmt);
  vsprintf((s+old_len), fmt, args);
  va_end(args);

  return s;
}

/* Add single quotes around a string, taking care of any existing quotes */
static char *quote_arg(const char *arg) {
  char *result = gasneti_strdup("'");
  char *p, *q, *tmp;

  p = tmp = gasneti_strdup(arg);
  while ((q = strchr(p, '\'')) != NULL) {
    *q = '\0';
    result = sappendf(result, "%s'\\''", p);
    p = q + 1;
  }
  result = sappendf(result, "%s'", p);
  gasneti_free(tmp);
  return result;
}

/* Like gasneti_fatalerror, but w/o dumping core
 * This is used for probable user errors
 */
static void die(int exitcode, const char *msg, ...) GASNET_NORETURN __attribute__((__format__ (__printf__, 2, 3)));
static void die(int exitcode, const char *msg, ...) {
  va_list argptr;
  char expandedmsg[255];

  strcpy(expandedmsg, "*** ERROR: ");
  strcat(expandedmsg, msg);
  strcat(expandedmsg, "\n");
  va_start(argptr, msg);
    vfprintf(stderr, expandedmsg, argptr);
    fflush(stderr);
  va_end(argptr);
  gasneti_killmyprocess(exitcode);
}

static void kill_one(const char *rem_host, pid_t rem_pid) {
  pid_t pid;
 
  gasneti_assert(is_master);
  gasneti_assert(rem_host != NULL);

  if (rem_pid == 0) return;

  pid = fork();
  if (pid < 0) {
    gasneti_fatalerror("fork() failed");
  } else if (pid == 0) {
    BOOTSTRAP_VERBOSE(("Killing %s:%d\n", rem_host, (int)rem_pid));
    (void)close(STDIN_FILENO);
#if !GASNET_DEBUG
    (void)close(STDOUT_FILENO);
    (void)close(STDERR_FILENO);
#endif
    ssh_argv[ssh_argc] = (/* noconst */ char *)rem_host;
    ssh_argv[ssh_argc+1] = sappendf(NULL, "cd %s; exec %s -kill %d",
				      quote_arg(cwd), quote_arg(argv0), rem_pid);
    execvp(ssh_argv[0], ssh_argv);
    gasneti_fatalerror("execvp(ssh kill) failed");
  }
}

static void clean_up(void)
{
  gasnet_node_t p_quot = nproc / nnodes;
  gasnet_node_t p_rem = nproc % nnodes;
  gasnet_node_t j, rank;

  gasneti_assert(is_master);
  fprintf(stderr, "Cleaning up orphaned processes...\n");

  exit_status = -1;

  gasneti_reghandler(SIGQUIT, SIG_DFL);
  gasneti_reghandler(SIGINT,  SIG_DFL);
  gasneti_reghandler(SIGTERM, SIG_DFL);
  gasneti_reghandler(SIGHUP,  SIG_DFL);
  gasneti_reghandler(SIGPIPE, SIG_DFL);

  for (j = 0, rank = 0; j < nnodes; ++j) {
    gasnet_node_t i;
    for (i = p_quot + ((j<p_rem)?1:0); i != 0; --i, ++rank) {
      kill_one(nodelist[j], all_pids[rank]);
    }
  }
}

/* Note that these are fired off asynchronously */
static void signal_one(const char *rem_host, pid_t rem_pid, int sig) {
  /* XXX: could avoid ssh for local case */
  pid_t pid;
 
  gasneti_assert(is_master);
  gasneti_assert(rem_host != NULL);

  if (rem_pid == 0) return;

  pid = fork();
  if (pid < 0) {
    gasneti_fatalerror("fork() failed");
  } else if (pid == 0) {
    BOOTSTRAP_VERBOSE(("Sending signal %d to %s:%d\n", sig, rem_host, (int)rem_pid));
    (void)close(STDIN_FILENO);
    (void)close(STDOUT_FILENO);
    (void)close(STDERR_FILENO);
    ssh_argv[ssh_argc] = (/* noconst */ char *)rem_host;
    ssh_argv[ssh_argc+1] = sappendf(NULL, "sh -c 'kill -s %d %d >/dev/null 2>&1'", sig, rem_pid);
    execvp(ssh_argv[0], ssh_argv);
    gasneti_fatalerror("execvp(ssh kill) failed");
  }
}

static void signal_all(int sig)
{
  gasnet_node_t p_quot = nproc / nnodes;
  gasnet_node_t p_rem = nproc % nnodes;
  gasnet_node_t j, rank;

  gasneti_assert(is_master);

  for (j = 0, rank = 0; j < nnodes; ++j) {
    gasnet_node_t i;
    for (i = p_quot + ((j<p_rem)?1:0); i != 0; --i, ++rank) {
      signal_one(nodelist[j], all_pids[rank], sig);
    }
  }
}

/* master forwards signals if possible */
static void sigforward(int sig)
{
  gasneti_assert(is_master);

  /* If we get it a second time don't forward */
  gasneti_reghandler(sig, SIG_DFL);

  if (child) {
    BOOTSTRAP_VERBOSE(("Master forwarding signal %d\n", sig));
#if 0
    signal_one(nodelist[0], all_pids[0], sig);
#else
    signal_all(sig);
#endif
  } else {
    BOOTSTRAP_VERBOSE(("Master resending signal %d to self\n", sig));
    raise(sig);
  }
}

static void do_oob(unsigned char exitcode) {
  const int flags = MSG_OOB 
#ifdef MSG_DONTWAIT
	  		| MSG_DONTWAIT
#endif
#ifdef MSG_NOSIGNAL
			| MSG_NOSIGNAL
#endif
			;
  int j;

  if (!is_master) {
    send(parent, &exitcode, 1, flags);
  }
  if (child) {
    for (j = 0; j < children; ++j) {
      (void)send(child[j].sock, &exitcode, 1, flags);
    }
  }
}

static void do_abort(unsigned char exitcode) GASNET_NORETURN;
static void do_abort(unsigned char exitcode) {
  gasneti_reghandler(SIGURG, SIG_IGN);
  do_oob(exitcode);
  _exit(exitcode);

  /* paranoia... */
  gasneti_reghandler(SIGABRT, SIG_DFL);
  abort();

  /* NOT REACHED */
}

static void reap_one(pid_t pid, int status)
{
  BOOTSTRAP_VERBOSE(("Process %d reaped pid %d\n", is_master ? -1 : (int)mypid, (int)pid));
  if (child) {
    int j;

    for (j = 0; j < children; ++j) {
      if (pid == child[j].pid) {
        (void)close(child[j].sock);
	if (exit_status == -1 && WIFEXITED(status)) {
	  exit_status = WEXITSTATUS(status);
	}
        if (finalized) {
	  BOOTSTRAP_VERBOSE(("Process %d exited\n", child[j].rank));
	} else {
	  BOOTSTRAP_VERBOSE(("Process %d exited before finalize\n", child[j].rank));
	  finalized = 1; /* avoid reentrance */
	  if (is_master) {
	    clean_up();
	  } else {
	    do_abort(exit_status);
	  }
	}
	break;
      }
    }
  }

  if (accepted < children) {
    gasneti_fatalerror("One or more processes died before setup was completed");
  }
}

static void reaper(int sig)
{
  pid_t pid;
  int status;

  gasneti_assert(!is_master);
  gasneti_reghandler(sig, &reaper);
  while((pid = waitpid(-1,&status,WNOHANG)) > 0) {
    reap_one(pid, status);
  }
}

static void sigurg_handler(int sig)
{
  unsigned char exitcode = 255;
  int j;

  /* We need to read our single byte of urgent data here.
   * Since we don't know which socket sent it, we just
   * try them all.  MSG_OOB is supposed to always be
   * non-blocking.  If multiple sockets have OOB data
   * pending, we don't care which one we read.
   */
  (void)recv(parent, &exitcode, 1, MSG_OOB);
  if (child) {
    for (j = 0; j < children; ++j) {
      (void)recv(child[j].sock, &exitcode, 1, MSG_OOB);
    }
  }

  do_abort(exitcode);
  /* NOT REACHED */
}

static void do_write(int fd, const void *buf, size_t len)
{
  const char *p = (const char *)buf;
  while (len) {
    ssize_t rc = write(fd, p, len);
    if_pf (rc <= 0) {
      do_oob(255);
      if (rc == 0) {
        gasneti_fatalerror("unexpected zero return from write(ctrl_socket)");
      } else {
        gasneti_fatalerror("write(ctrl_socket) returned errno=%d", errno);
      }
    }
    p += rc;
    len -= rc;
  }
}

static void do_write_string(int fd, const char *string) {
  size_t len = string ? strlen(string) : 0;
  do_write(fd, &len, sizeof(len));
  do_write(fd, string, len);
}

static void do_read(int fd, void *buf, size_t len)
{
  char *p = (char *)buf;
  while (len) {
    ssize_t rc = read(fd, p, len);
    if_pf (rc <= 0) {
      do_oob(255);
      if (rc == 0) {
        gasneti_fatalerror("unexpected EOF from read(ctrl_socket) on process %d", is_master ? -1 : (int)myproc);
      } else {
        gasneti_fatalerror("read(ctrl_socket) returned errno=%d on process %d", errno, is_master ? -1 : (int)myproc);
      }
    }
    p += rc;
    len -= rc;
  }
}

static char *do_read_string(int fd) {
  char *result = NULL;
  size_t len;

  do_read(fd, &len, sizeof(size_t));
  if (len) {
    result = gasneti_malloc(len + 1);
    do_read(fd, result, len);
    result[len] = '\0';
  }

  return result;
}

static int options_helper(char **list, const char *string, const char *where)
{
  int count = 0;
  int in_quotes = 0;
  const char *special[] = {WHITESPACE "\\\"'",	/* special chars outside dbl quotes */
			   "\\\""};		/* special chars inside dbl quotes */
  
  if (!string) {
    return 0;
  }

  /* Outer loop adds a word to the list on each pass
     with the possible exception of the last
   */
  while (*string) {
    char tmp[1024];
    char *p = tmp;
    while (*string && strchr(WHITESPACE,*string)) ++string; /* eat leading whitespace */
    if (!*string) { break; /* reached end of string */ }
    /* This loop brings together pieces of a "word", possible w/ quotes */
    in_quotes = 0;
    do {
      int i = strcspn(string, special[in_quotes]);
      memcpy(p , string, i); p += i;
      gasneti_assert((uintptr_t)(p-tmp) < (sizeof(tmp)-1));
      string += i;
      switch (*string) {
	case '\0':
	  break;
	case '\\':
	  if (!string[1]) {
	    die(1, "string ends with \\ %s", where);
	  } else if (strchr(special[in_quotes],string[1])) {
	    /* Drop the backslash if it quotes a special character */
            *(p++) = string[1];
            gasneti_assert((uintptr_t)(p-tmp) < (sizeof(tmp)-1));
	  } else {
	    /* Keep the backslash */
	    memcpy(p , string, 2); p += 2;
            gasneti_assert((uintptr_t)(p-tmp) < (sizeof(tmp)-1));
	  }
	  string += 2;
	  break;
	case '\'':
	  ++string;
	  i = strcspn(string, "\'");
	  if (string[i] != '\'') {
	    die(1, "unbalanced ' %s", where);
	  }
          memcpy(p , string, i); p += i;
          gasneti_assert((uintptr_t)(p-tmp) < (sizeof(tmp)-1));
	  string += i + 1;
	  break;
	case '"':
	  ++string;
	  in_quotes = !in_quotes;
	  break;
	default: /* WHITESPACE */
	  break;
      }
    } while (*string && (in_quotes || !strchr(WHITESPACE,*string)));
    if (in_quotes) {
      die(1, "unbalanced \" %s", where);
    }
    if (list) {
      gasneti_assert((uintptr_t)(p-tmp) < sizeof(tmp));
      *p = '\0';
      list[count] = gasneti_strdup(tmp);
    }
    ++count;
  }
  if (list) {
    list[count] = NULL;
  }
  return count;
}

/* Parse a string into an array of "words", following shell rules for '," and \ */
static char **parse_options(const char *string, int *count_p, const char *where)
{
  int count;
  char **list;

  /* First parse pass will just count the words */
  count = options_helper(NULL, string, where);
  list = gasneti_malloc(sizeof(char *) * (count+1));

  /* Second pass fills the list of words */
  (void)options_helper(list, string, where);

  if (count_p) *count_p = count;
  return list;
}

static void configure_ssh(void) {
  char *env_string;
  char *ssh_argv0;
  char **ssh_options = NULL;
  int is_openssh = 0;
  int optcount = 0;
  int i, argi;

  /* Determine the ssh command */
  if ((env_string = getenv(ENV_PREFIX "SSH_CMD")) != NULL && strlen(env_string)) {
    ssh_argv0 = env_string;
  } else {
    ssh_argv0 = gasneti_strdup("ssh");
  }

  /* Check for OpenSSH */
  {
    char *cmd = sappendf(NULL, "%s -v 2>&1 | grep OpenSSH >/dev/null 2>/dev/null", ssh_argv0);
    is_openssh = (0 == system(cmd));
    gasneti_free(cmd);
    BOOTSTRAP_VERBOSE(("Configuring for OpenSSH\n"));
  }

  /* Check for user-supplied options */
  if ((env_string = getenv(ENV_PREFIX "SSH_OPTIONS")) != NULL && strlen(env_string)) {
    ssh_options = parse_options(env_string, &optcount, "while parsing " ENV_PREFIX "SSH_OPTIONS");
  }

  /* Now build the command line */
  ssh_argc = optcount + (is_openssh ? 7 : 1);
  ssh_argv = gasneti_calloc((ssh_argc + 3 /* host + cmd + NULL = 3 */), sizeof(char *));
  ssh_argv[0] = ssh_argv0;
  argi = 1;
  if (is_openssh) {
    ssh_argv[argi++] = (char *)"-o"; ssh_argv[argi++] = (char *)"StrictHostKeyChecking no";
    ssh_argv[argi++] = (char *)"-o"; ssh_argv[argi++] = (char *)"FallBackToRsh no";
    ssh_argv[argi++] = (char *)"-o"; ssh_argv[argi++] = (char *)"BatchMode yes";
  }
  if (optcount) {
    for (i=0; i<optcount; ++i, ++argi) {
      ssh_argv[argi] = ssh_options[i];
    }
    gasneti_free(ssh_options);
  }

  BOOTSTRAP_VERBOSE(("Constructed ssh command line:\n"));
  for (i=0; i<ssh_argc; ++i) {
    BOOTSTRAP_VERBOSE(("\t%s\n", ssh_argv[i]));
  }
  BOOTSTRAP_VERBOSE(("\tHOST\n\tCMD\n"));
}

/* Build an array of hostnames from a file */
static char ** parse_nodefile(const char *filename) {
  char **result = NULL;
  gasnet_node_t i;
  FILE *fp;

  BOOTSTRAP_VERBOSE(("Parsing nodefile '%s'\n", filename));
  fp = fopen(filename, "r");
  if (!fp) {
    die(1, "failed to open nodefile '%s'", filename);
  }

  result = gasneti_malloc(nnodes * sizeof(char *));
  for (i = 0; i < nnodes;) {
    static char buf[1024];
    char *p;

    if (!fgets(buf, sizeof(buf), fp)) {
      /* ran out of lines */
      die(1, "Out of lines in nodefile '%s'", filename);
    }
 
    p = buf;
    while (*p && strchr(WHITESPACE, *p)) ++p; /* eat leading whitespace */
    if (*p != '#') {
      p[strcspn(p, WHITESPACE)] = '\0';
      result[i] = gasneti_strdup(p);
#if 0
      /* XXX: Eat consecutive duplicates ?
       * When running w/ PBS and nodes=2:ppn=2 we see NODE0,NODE0,NODE1,NODE1
       * while running the same config w/ LSF gives just NODE0,NODE1.
       * We'd get consistent results by squeezing out the PBS style dups,
       * but the upcrun handling currently doesn't do this so we don't either.
       */
      if ((i > 0) && !strcmp(result[i], result[i-1])) {
        gasneti_free(result[i]);
	continue;
      }
#endif
      ++i;
      BOOTSTRAP_VERBOSE(("\t%s\n", p));
    }
  }

  (void)fclose(fp);

  return result;
}

/* Build an array of hostnames from a delimited string */
static char ** parse_servers(const char *list) {
  static const char *delims = SSH_SERVERS_DELIM_CHARS;
  char **result = NULL;
  char *string, *alloc;
  gasnet_node_t i;

  alloc = string = gasneti_strdup(list);
  result = gasneti_malloc(nnodes * sizeof(char *));
  BOOTSTRAP_VERBOSE(("Parsing servers list '%s'\n", string));
  for (i = 0; i < nnodes; ++i) {
    char *p;
    while (*string && strchr(delims,*string)) ++string; /* eat leading delimiters */
    if (!*string) {
      die(1, "Too few hosts in " ENV_PREFIX "SSH_SERVERS");
    }
    p = string;
    string += strcspn(string, delims);
    if (*string) *(string++) = '\0';
    result[i] = gasneti_strdup(p);
    BOOTSTRAP_VERBOSE(("\t%s\n", result[i]));
  }
  gasneti_free(alloc);

  return result;
}

static void build_nodelist(void)
{
  const char *env_string;

  if (nproc < nnodes) {
    fprintf(stderr, "Warning: %d nodes is larger than %d processes, nodes reduced to %d\n", nnodes, nproc, nproc);
    nnodes = nproc;
  }

  if ((env_string = getenv(ENV_PREFIX "SSH_NODEFILE")) != NULL && strlen(env_string)) {
    nodelist = parse_nodefile(env_string);
  } else if ((env_string = getenv(ENV_PREFIX "SSH_SERVERS")) != NULL && strlen(env_string)) {
    nodelist = parse_servers(env_string);
  } else if ((env_string = getenv("PBS_NODEFILE")) != NULL && strlen(env_string)) {
    nodelist = parse_nodefile(env_string);
  } else if ((env_string = getenv("SSS_HOSTLIST")) != NULL && strlen(env_string)) {
    nodelist = parse_servers(env_string);
  } else if ((env_string = getenv("LSB_HOSTS")) != NULL && strlen(env_string)) {
    nodelist = parse_servers(env_string);
  } else {
    die(1, "No " ENV_PREFIX "SSH_NODEFILE or " ENV_PREFIX "SSH_SERVERS in environment");
  }
}

static void send_nodelist(int s, int count, char ** list) {
  gasnet_node_t i;

  /* length of list is already known to the recipient */
  for (i = 0; i < count; ++i) {
    do_write_string(s, list[i]);
  }
}

static void recv_nodelist(int s, int count) {
  if (count) {
    gasnet_node_t i;

    nodelist = gasneti_malloc(count * sizeof(char *));
    for (i = 0; i < count; ++i) {
      nodelist[i] = do_read_string(s);
    }
  }
}

/*
 * Send environment as a big char[] with \0 between each 'VAR=VAL'
 * and a double \0 to terminate. (inspired by amudp)
 */
static void send_env(int s) {
  if (!master_env) {
    int i;
    const char *p;
    char *q;
    size_t rlen = strlen(ENV_PREFIX "SSH_");

    gasneti_assert(is_master);

    /* First pass over environment to get its size */
    master_env_len = 1; /* for the doubled \0 at the end */
    for (i = 0, p = environ[0]; p != NULL; p = environ[++i]) {
      if (!strncmp(ENV_PREFIX "SSH_", p, rlen)) {
        /* We parse these ourselves, don't forward */
      } else {
        master_env_len += strlen(p) + 1;
      }
    }

    /* Append all the strings together */
    q = master_env = gasneti_malloc(master_env_len);
    for (i = 0, p = environ[0]; p != NULL; p = environ[++i]) {
      if (!strncmp(ENV_PREFIX "SSH_", p, rlen)) {
        /* We parse these ourselves, don't forward */
      } else {
        size_t tmp = strlen(p) + 1;
        memcpy(q, p, tmp);
        q += tmp;
      }
    }
    *q = '\0';
  }

  /* send it */
  do_write(s, &master_env_len, sizeof(master_env_len));
  do_write(s, master_env, master_env_len);
}

static void recv_env(int s) {
  do_read(s, &master_env_len, sizeof(master_env_len));
  master_env = gasneti_malloc(master_env_len);
  do_read(s, master_env, master_env_len);
}

static void send_ssh_argv(int s) {
  int i;

  do_write(s, &ssh_argc, sizeof(int));
  for (i = 0; i < ssh_argc; ++i) {
    do_write_string(s, ssh_argv[i]);
  }
}

static void recv_ssh_argv(int s) {
  int i;

  do_read(s, &ssh_argc, sizeof(int));
  ssh_argv = gasneti_calloc(ssh_argc+3, sizeof(char *));
  for (i = 0; i < ssh_argc; ++i) {
    ssh_argv[i] = do_read_string(s);
  }
}

static void send_argv(int s, int argc, char * const *argv) {
  int i;

  do_write(s, &argc, sizeof(int));
  for (i = 0; i < argc; ++i) {
    do_write_string(s, argv[i]);
  }
}

static void recv_argv(int s, int *argc_p, char ***argv_p) {
  int argc, i;
  char **argv;

  do_read(s, &argc, sizeof(int));
  argv = gasneti_calloc(argc+1, sizeof(char **));
  for (i = 0; i < argc; ++i) {
    argv[i] = do_read_string(s);
  }
  argv[argc] = NULL;

  *argc_p = argc;
  *argv_p = argv;
}

static void pre_spawn(count) {
  struct sockaddr_in sock_addr;
  socklen_t addr_len;

  /* Get the cwd */
  if (!getcwd(cwd, sizeof(cwd))) {
    gasneti_fatalerror("getcwd() failed");
  }

  /* Open /dev/null */
  devnull = open("/dev/null", O_RDONLY);
  if (devnull < 0) {
    gasneti_fatalerror("open(/dev/null) failed");
  }
  (void)fcntl(devnull, F_SETFD, FD_CLOEXEC);

  /* Create listening socket */
  if ((listener = socket(PF_INET, SOCK_STREAM, IPPROTO_TCP)) < 0) {
    gasneti_fatalerror("listener = socket() failed");
  }
  (void)fcntl(listener, F_SETFD, FD_CLOEXEC);
  sock_addr.sin_family = AF_INET;
  sock_addr.sin_port = 0;
  sock_addr.sin_addr.s_addr = INADDR_ANY;
  addr_len = sizeof(sock_addr);
  if (bind(listener, (struct sockaddr *)&sock_addr, addr_len) < 0) {
    gasneti_fatalerror("bind() failed");
  }
  if (listen(listener, count) < 0) {
    gasneti_fatalerror("listen() failed w/ errno=%d", errno);
  }
  if (getsockname(listener, (struct sockaddr *)&sock_addr, &addr_len) < 0) {
    gasneti_fatalerror("getsockname() failed");
  }
  listen_port = ntohs(sock_addr.sin_port);
}

static void post_spawn(int count, int argc, char * const *argv) {
  /* Accept count connections */
  while (count--) {
    struct sockaddr_in sock_addr;
    socklen_t addr_len = sizeof(sock_addr);
    static const int one = 1;
    gasnet_node_t child_id;
    struct child *ch = NULL;
    int s;

    if ((s = accept(listener, (struct sockaddr *)&sock_addr, &addr_len)) < 0) {
      gasneti_fatalerror("accept() failed w/ errno=%d", errno);
    }
    (void)fcntl(s, F_SETFD, FD_CLOEXEC);
    (void)ioctl(s, SIOCSPGRP, &mypid); /* Enable SIGURG delivery on OOB data */
    (void)setsockopt(s, IPPROTO_TCP, TCP_NODELAY, (char *) &one, sizeof(one));
    do_read(s, &child_id, sizeof(gasnet_node_t));
    gasneti_assert(child_id < children);
    ch = &(child[child_id]);
    child[child_id].sock = s;
    gasneti_assert(ch->rank < nproc);
    gasneti_assert(ch->procs > 0);
    gasneti_assert(ch->procs <= nproc);
    do_write(s, &ch->rank, sizeof(gasnet_node_t));
    do_write(s, &nproc, sizeof(gasnet_node_t));
    do_write(s, &ch->procs, sizeof(gasnet_node_t));
    do_write(s, &ch->nodes, sizeof(gasnet_node_t));
    send_env(s);
    send_nodelist(s, ch->nodes, ch->nodelist);
    send_ssh_argv(s);
    send_argv(s, argc, argv);
    ++accepted;
  }

  /* Close listener and /dev/null */
  close(listener);
  close(devnull);

  /* Free the nodelist and ssh_argv */
  if (myproc != (gasnet_node_t)(-1L)) {
    gasnet_node_t i;
    int j;

    for (i = 0; i < tree_nodes; ++i) {
      gasneti_free(nodelist[i]);
    }
    gasneti_free(nodelist);

    for (j = 0; j < ssh_argc; ++j) {
      gasneti_free(ssh_argv[j]);
    }
    gasneti_free(ssh_argv);
  }
}

static void do_connect(gasnet_node_t child_id, const char *parent_name, int parent_port, int *argc_p, char ***argv_p) {
  struct sockaddr_in sock_addr;
  socklen_t addr_len;
  static const int one = 1;
  struct hostent *h = gethostbyname(parent_name);

  gasneti_assert(!is_master);

  if (h == NULL) {
    gasneti_fatalerror("gethostbyname(%s) failed", parent_name);
  }
  if ((parent = socket(PF_INET, SOCK_STREAM, IPPROTO_TCP)) < 0) {
    gasneti_fatalerror("parent = socket() failed");
  }
  sock_addr.sin_family = AF_INET;
  sock_addr.sin_port = htons(parent_port);
  sock_addr.sin_addr = *(struct in_addr *)(h->h_addr_list[0]);
  addr_len = sizeof(sock_addr);
  if (connect(parent, (struct sockaddr *)&sock_addr, addr_len) < 0) {
    gasneti_fatalerror("connect(host=%s, port=%d) failed w/ errno=%d", parent_name, parent_port, errno);
  }
  (void)fcntl(parent, F_SETFD, FD_CLOEXEC);
  (void)ioctl(parent, SIOCSPGRP, &mypid); /* Enable SIGURG delivery on OOB data */
  (void)setsockopt(parent, IPPROTO_TCP, TCP_NODELAY, (char *) &one, sizeof(one));
  do_write(parent, &child_id, sizeof(gasnet_node_t));
  do_read(parent, &myproc, sizeof(gasnet_node_t));
  do_read(parent, &nproc, sizeof(gasnet_node_t));
  do_read(parent, &tree_procs, sizeof(gasnet_node_t));
  do_read(parent, &tree_nodes, sizeof(gasnet_node_t));
  gasneti_assert(nproc > 0);
  gasneti_assert(myproc < nproc);
  gasneti_assert(tree_procs > 0);
  gasneti_assert(tree_procs <= nproc);
  recv_env(parent);
  recv_nodelist(parent, tree_nodes);
  recv_ssh_argv(parent);
  recv_argv(parent, argc_p, argv_p);
  BOOTSTRAP_VERBOSE(("Process %d connected\n", myproc));
}

static void spawn_one(gasnet_node_t child_id, const char *myhost) {
  const char *host = child[child_id].nodelist ? child[child_id].nodelist[0] : NULL;
  pid_t pid;
  int is_local = (USE_LOCAL_SPAWN && (!host || !strcmp(host, myhost)));

  child[child_id].pid = pid = fork();

  if (pid < 0) {
    gasneti_fatalerror("fork() failed");
  } else if (pid == 0) {
    /* For all children except the root do </dev/null */
    if (child[child_id].rank != 0) {
      if (dup2(STDIN_FILENO, devnull) < 0) {
        gasneti_fatalerror("dup2(STDIN_FILENO, /dev/null) failed");
      }
    }
    if (is_local) {
      /* XXX: if we are clever enough, we might be able to "unwind" w/o the exec() */
      BOOTSTRAP_VERBOSE(("Process %d spawning process %d on %s via fork()\n",
			 (is_master ? -1 : (int)myproc),
			 (int)child[child_id].rank, myhost));
      execlp(argv0, argv0, "-slave", "localhost",
	     sappendf(NULL, "%d", listen_port),
	     sappendf(NULL, "%d", (int)child_id),
	     is_verbose ? "-v" : NULL,
	     NULL);
      gasneti_fatalerror("execlp(sh) failed");
    } else {
      #if HAVE_PR_SET_PDEATHSIG
	/* If parent exits before us (an abnormal condition) then we exit too */
	(void)prctl(PR_SET_PDEATHSIG, SIGHUP);
      #endif
      BOOTSTRAP_VERBOSE(("Process %d spawning process %d on %s via %s\n",
			 (is_master ? -1 : (int)myproc),
			 (int)child[child_id].rank, host, ssh_argv[0]));
      ssh_argv[ssh_argc] = (/* noconst */ char *)host;
      ssh_argv[ssh_argc+1] = sappendf(NULL, "cd %s; exec %s -slave %s %d %d%s",
				      quote_arg(cwd), quote_arg(argv0),
				      myhost, listen_port, (int)child_id,
				      is_verbose ? " -v" : "");
      execvp(ssh_argv[0], ssh_argv);
      gasneti_fatalerror("execvp(ssh) failed");
    }
  }
}

static void do_spawn(int argc, char **argv, char *myhost) {
  int j;

  pre_spawn(children);
  for (j = 0; j < children; ++j) {
    spawn_one(j, myhost);
  }
  post_spawn(children, argc, argv);
}

static void usage(const char *argv0) {
  die(1, "usage: %s [-master] [-v] NPROC[:NODES] [--] [ARGS...]", argv0);
}

static void do_kill(int argc, char **argv) GASNET_NORETURN;
static void do_kill(int argc, char **argv) {
  int pid;
  int rc;
  int loops = 30;

  if ((argc != 3) || ((pid = atoi(argv[2])) < 1)) {
    _exit(-1);
  }

  /* First try the polite signal */
  rc = kill(pid, SIGTERM);

  /* Loop until process is gone (kill fails) or time runs out */
  while ((rc == 0) && (loops--)) {
    (void)sleep(1);
    rc = kill(pid,  0); /* just checks if it is still alive and ours */
  }

  /* Be forcefull if still around */
  if (rc == 0) { 
    (void)kill(pid, SIGKILL);
  }

  _exit(0);
}

static void do_master(int argc, char **argv) GASNET_NORETURN;
static void do_master(int argc, char **argv) {
  char myhost[1024];
  char *p;
  int argi=1;

  is_master = 1;
  gasneti_reghandler(SIGURG, &sigurg_handler);

  if ((argi < argc) && (strcmp(argv[argi], "-master") == 0)) {
    argi++;
  }
  if ((argi < argc) && (strcmp(argv[argi], "-v") == 0)) {
    is_verbose = 1;
    argi++;
  }
  if (argi >= argc) usage(argv[0]); /* ran out of args */

  nproc = atoi(argv[argi]);
  if (nproc < 1) usage(argv[0]); /* bad argument */
  p = strchr(argv[argi], ':');
  if (p) {
    nnodes = atoi(p+1);
    if (nnodes < 1) usage(argv[0]); /* bad argument */
  } else {
    nnodes = nproc;
  }
  BOOTSTRAP_VERBOSE(("Spawning '%s': %d processes on %d nodes\n", argv[0], (int)nproc, (int)nnodes));
  argi++;

  if ((argi < argc) && (strcmp(argv[argi], "--") == 0)) {
    argi++;
  }

  argv[argi-1] = argv[0];
  argc -= argi-1;
  argv += argi-1;

  if (gethostname(myhost, sizeof(myhost)) < 0) {
    die(1, "gethostname() failed");
  }

  configure_ssh();
  build_nodelist();
  all_pids = gasneti_calloc(nproc, sizeof(pid_t));

  /* Arrange to forward termination signals */
  gasneti_reghandler(SIGQUIT, &sigforward);
  gasneti_reghandler(SIGINT,  &sigforward);
  gasneti_reghandler(SIGTERM, &sigforward);
  gasneti_reghandler(SIGHUP,  &sigforward);
  gasneti_reghandler(SIGPIPE, &sigforward);

  /* Configure child(ren) */
  #if FLAT_TREE
  {
    gasnet_node_t p_quot = nproc / nnodes;
    gasnet_node_t p_rem = nproc % nnodes;
    gasnet_node_t rank;
    int j;

    children = nproc;
    child = gasneti_calloc(children, sizeof(struct child));
    for (j = 0, rank = 0; j < nnodes; ++j) {
      gasnet_node_t i;
      for (i = p_quot + ((j<p_rem)?1:0); i != 0; --i, ++rank) {
        child[j].rank = rank;
        child[j].procs = 1;
        child[j].nodes = 1;
        child[j].nodelist = nodelist+j;
      }
    }
  }
  #else
    children = 1;
    child = gasneti_calloc(children, sizeof(struct child));
    child[0].rank = 0;
    child[0].procs = nproc;
    child[0].nodes = nnodes;
    child[0].nodelist = nodelist;
  #endif

  /* Start the process(es) */
  do_spawn(argc, argv, myhost);

  /* Locate all procs */
  gather_pids();

  /* Wait on the child(ren) */
#if FLAT_TREE
  /* XXX: Finalize unimplemented */
#else
  {
    char cmd;
    ssize_t rc;
    do {
      rc = read(child[0].sock, &cmd, sizeof(cmd));
      if (rc == sizeof(cmd)) {
        gasneti_assert(cmd == 'F');
        finalized = 1;
        cmd = 'G';
        (void)write(child[0].sock, &cmd, sizeof(cmd));
      }
    } while ((rc < 0) && (errno == EINTR));
  }
#endif

  /* Wait for all children to terminate */
  {
    int status;
    pid_t pid;
    while (((pid = waitpid(-1,&status,0)) > 0) ||
	   ((pid < 0) && (errno == EINTR))) {
      reap_one(pid, status);
    }
  }

  exit (exit_status);
}

static void do_slave(int *argc_p, char ***argv_p, gasnet_node_t *nodes_p, gasnet_node_t *mynode_p)
{
  int argc = *argc_p;
  char **argv = *argv_p;
  gasnet_node_t child_id;
  const char *parent_name;
  int parent_port;

  is_master = 0;
  gasneti_reghandler(SIGURG, &sigurg_handler);

  #if HAVE_PR_SET_PDEATHSIG
    /* If parent exits before us (an abnormal condition) then trigger an abort */
    (void)prctl(PR_SET_PDEATHSIG, SIGURG);
  #endif

  if ((argc < 5) || (argc > 6)){
    gasneti_fatalerror("Invalid command line in slave process");
  }
  parent_name = argv[2];
  parent_port = atoi(argv[3]);
  child_id = atoi(argv[4]);
  if (argc == 6) {
    gasneti_assert(!strcmp("-v",argv[5]));
    is_verbose = 1;
  }

  mypid = getpid();

  /* Connect w/ parent to find out who we are */
  do_connect(child_id, parent_name, parent_port, argc_p, argv_p);

  /* Start any children */
  if (tree_procs > 1) {
    gasnet_node_t p_quot, p_rem; /* quotient and remainder of nproc/nodes */
    gasnet_node_t n_quot, n_rem; /* quotient and remainder of nodes/OUT_DEGREE */
    gasnet_node_t local_procs; /* the local processes (proc-per-node), excluding self */
    gasnet_node_t rank, j;
    char **sublist;

    p_quot = tree_procs / tree_nodes;
    p_rem = tree_procs % tree_nodes;

    local_procs = p_quot + (p_rem?1:0) - 1;
    p_rem -= (p_rem?1:0);

    /* Children = (local_procs other than self) + (child nodes) */
    children = local_procs + MIN(OUT_DEGREE, (tree_nodes - 1));
    child = gasneti_calloc(children, sizeof(struct child));
    rank = myproc + 1;

    /* Map out the local processes */
    for (j = 0; j < local_procs; ++j) {
	child[j].rank = rank++;
	child[j].procs = 1;
	child[j].nodes = 0; /* N/A */
        child[j].nodelist = NULL;
    }

    /* Map out the child nodes */
    n_quot = (tree_nodes - 1) / OUT_DEGREE;
    n_rem = (tree_nodes - 1) % OUT_DEGREE;
    sublist = nodelist + 1;
    for (j = local_procs; rank < (myproc + tree_procs); j++) {
      gasnet_node_t nodes = n_quot + (n_rem?1:0);
      gasnet_node_t procs = (nodes * p_quot) + MIN(p_rem, nodes);
      n_rem -= (n_rem?1:0);
      p_rem -= MIN(p_rem, nodes);

      child[j].rank = rank;
      child[j].procs = procs;
      child[j].nodes = nodes;
      child[j].nodelist = sublist;
      sublist += nodes;
      rank += procs;
    }
  
    /* Prepare to reap children */
    gasneti_reghandler(SIGCHLD, &reaper);

    /* Spawn them */
    do_spawn(*argc_p, *argv_p, nodelist[0]);
  }

  gather_pids();

  *nodes_p = nproc;
  *mynode_p = myproc;
}

/* dest is >= len*tree_procs, used as temp space on all but root */
static void do_gather0(void *src, size_t len, void *dest)
{
  int j;
  void *tmp;

  memcpy(dest, src, len);
  tmp = (void *)((uintptr_t)dest + len);
  for (j = 0; j < children; ++j) {
    gasnet_node_t procs = child[j].procs;
    do_read(child[j].sock, tmp, len*procs);
    tmp = (void *)((uintptr_t)tmp + len*procs);
  }
  if (myproc) {
    do_write(parent, dest, len * tree_procs);
  }
}

/* src is >= len*tree_procs, used as temp space on all but root */
static void do_scatter0(void *src, size_t len, void *dest)
{
  int j;
  void *tmp;

  if (myproc) {
    do_read(parent, src, len * tree_procs);
  }
  memcpy(dest, src, len);
  tmp = (void *)((uintptr_t)src + len);
  for (j = 0; j < children; ++j) {
    gasnet_node_t procs = child[j].procs;
    do_write(child[j].sock, tmp, len*procs);
    tmp = (void *)((uintptr_t)tmp + len*procs);
  }
}

static void do_bcast0(size_t len, void *dest) {
  int j;

  if (myproc) {
    do_read(parent, dest, len);
  }
  for (j = 0; j < children; ++j) {
    do_write(child[j].sock, dest, len);
  }
}

static void gather_pids(void)
{
  if (is_master) {
    do_read(child[0].sock, all_pids, sizeof(pid_t) * nproc);
  } else {
    pid_t *pids = gasneti_malloc(sizeof(pid_t) * tree_procs);

    /* Collect rows from our subtree (gather) */
    do_gather0(&mypid, sizeof(pid_t), pids);

    if (!myproc) {
      do_write(parent, pids, sizeof(pid_t) * nproc);
    }

    gasneti_free(pids);
  }
}

/*----------------------------------------------------------------------------------------------*/

/* gasneti_bootstrapInit
 *
 * Upon return:
 *   + argc and argv are those the user specified
 *   + *nodes_p and *mynode_p are set
 *   + the global environment is available via gasneti_bootstrapGetenv()
 * There is no barrier at the end, so it is possible that in a multi-level
 * tree, there are still some processes not yet spawned.  This is OK, since
 * we assume that at least one gasneti_bootstrap*() collectives will follow.
 * Not waiting here allows any subsequent that first collective to overlap
 * with the spawning.
 */
void gasneti_bootstrapInit(int *argc_p, char ***argv_p, gasnet_node_t *nodes_p, gasnet_node_t *mynode_p) {
  int argc = *argc_p;
  char **argv = *argv_p;

  if (argc < 2) {
    usage(argv[0]);
  }

  argv0 = argv[0];

  if (strcmp(argv[1], "-slave") == 0) {
    do_slave(argc_p, argv_p, nodes_p, mynode_p);
  } else if (strcmp(argv[1], "-kill") == 0) {
    do_kill(argc, argv); /* Does not return */
  } else {
    do_master(argc, argv); /* Does not return */
  }
}

/* gasneti_bootstrapFini
 *
 * Waits for children to exit.
 */
void gasneti_bootstrapFini(void) {
  static const char F = 'F';
  char cmd;
  int j;

  /* Perform Finalization barrier */
  for (j = 0; j < children; ++j) {
    do_read(child[j].sock, &cmd, sizeof(cmd));
    gasneti_assert(cmd == 'F');
  }
  finalized = 1;
  do_write(parent, &F, sizeof(F));
  if (!myproc) {
    do_read(parent, &cmd, sizeof(cmd));
    gasneti_assert(cmd == 'G');
  }
  do_bcast0(sizeof(cmd), &cmd);
  gasneti_assert(cmd == 'G');

  /* Close our control sockets */
  if (child) {
    for (j = 0; j < children; ++j) {
      (void)close(child[j].sock);
    }
  }
  (void)close(parent);

#if GASNET_DEBUG
  {
    /* Wait for all children to exit */
    int status;
    pid_t pid;
    gasneti_reghandler(SIGCHLD, SIG_DFL);
    while (((pid = waitpid(-1,&status,0)) > 0) ||
	   ((pid < 0) && (errno == EINTR))) {
      reap_one(pid, status);
    }
  }
#endif
}

/* gasneti_bootstrapAbort
 *
 * Force immediate (abnormal) termination.
 */
void gasneti_bootstrapAbort(int exitcode) {
  do_abort((unsigned char)exitcode);
  /* NOT REACHED */
}

void gasneti_bootstrapBarrier(void) {
  int cmd, j;

  /* UP */
  for (j = 0; j < children; ++j) {
    do_read(child[j].sock, &cmd, sizeof(cmd));
    gasneti_assert(cmd == 0);
  }
  if (myproc) {
    static const int zero = 0;
    do_write(parent, &zero, sizeof(zero));
  }

  /* DOWN */
  cmd = 1;
  do_bcast0(sizeof(cmd), &cmd);
  gasneti_assert(cmd == 1);
}

void gasneti_bootstrapExchange(void *src, size_t len, void *dest) {
  int j;

  /* Gather data up the tree, assembling partial results in-place in dest */
  do_gather0(src, len, (void *)((uintptr_t)dest + len*myproc));

  /* Move data down, reducing traffic by sending
     only parts that a given node did not send to us */
  if (myproc) {
    gasnet_node_t next = myproc + tree_procs;
    do_read(parent, dest, len*myproc);
    do_read(parent, (void *)((uintptr_t)dest + len*next), len*(nproc - next));
  }
  for (j = 0; j < children; ++j) {
    gasnet_node_t next = child[j].rank + child[j].procs;
    do_write(child[j].sock, dest, len*child[j].rank);
    do_write(child[j].sock, (void *)((uintptr_t)dest + len*next), len*(nproc - next));
  }
}

void gasneti_bootstrapAlltoall(void *src, size_t len, void *dest) {
  size_t row_len = len * nproc;
  char *tmp;
                                                                                                              
  tmp = gasneti_malloc(row_len * tree_procs);

  /* Collect rows from our subtree (gather) */
  do_gather0(src, row_len, tmp);

  /* Transpose at root, using dest for free temporary space */
  if (!myproc) {
    gasnet_node_t i;
    for (i = 0; i < nproc; ++i) {
      gasnet_node_t k;
      for (k = 0; k < i; ++k) {
	void *p = tmp + (i*row_len + k*len);
	void *q = tmp + (k*row_len + i*len);
        memcpy(dest, p, len);
        memcpy(p, q, len);
        memcpy(q, dest, len);
      }
    }
  }

  /* Move data back down (scatter) */
  do_scatter0(tmp, row_len, dest);

  gasneti_free(tmp);
}

void gasneti_bootstrapBroadcast(void *src, size_t len, void *dest, int rootnode) {
  int j;

  /* Move up the tree to proc 0 */
  if (rootnode != 0) {
    if (rootnode == myproc) {
      do_write(parent, src, len);
    } else if ((rootnode > myproc) && (rootnode < (myproc + tree_procs))) {
      /* Forward from child to parent */
      for (j = 0; (rootnode >= child[j].rank + child[j].procs); ++j) {
	/* searching for proper child */
      }
      do_read(child[j].sock, dest, len);
      if (myproc) {
        do_write(parent, dest, len);
      }
    }
  } else if (!myproc) {
    memcpy(dest, src, len);
  }

  /* Now move it down */
  do_bcast0(len, dest);
}

/* gasneti_bootstrapGetenv
 *
 * Fetch a variable from the environment on the master node.
 * (more or less copied from amudp_spmd.cpp)
 */
char *gasneti_bootstrapGetenv(const char *var) {
  if (master_env && var && (*var != '\0')) {
    char *p = master_env;
    size_t len = strlen(var);

    while (*p) {
      if (!strncmp(var, p, len) && (p[len] == '=')) {
        return p + len + 1;
      } else {
        p += strlen(p) + 1;
      }
    }
  }
  return NULL;
}

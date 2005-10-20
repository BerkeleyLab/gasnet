/*    $Source: /Users/kamil/work/gasnet-cvs2/gasnet/other/amudp/socket.h,v $
 *      $Date: 2005/10/20 09:19:32 $
 *  $Revision: 1.9.4.2 $
 *  Description: portable header socket functions
 *  (c) Scott McPeak, 1998-1999, Modified by Dan Bonachea
 */

#ifndef SOCKET_H
#define SOCKET_H

/*  ------------- win32 -------------------- */
#if defined(WIN32) && !defined(__CYGWIN__)
#define WINSOCK 1
#include <winsock2.h>    /*  sockets */
#include <windows.h>  
    
#define SHUT_RD   SD_RECEIVE 
#define SHUT_WR   SD_SEND 
#define SHUT_RDWR SD_BOTH 

#define _FIONREAD FIONREAD

/*  ------------ unix ------------------ */
#else

#ifdef __osf__
#include <inttypes.h>      /* must proceed below or int64_t won't be defined correctly */
#endif

#include <sys/types.h>     /*  Solaris 2.5.1 fix: u_short, required by sys/socket.h */
#include <sys/socket.h>    /*  sockets */
#include <sys/time.h>      /*  timeval */
#include <sys/ioctl.h>     /*  ioctl  */
#include <string.h>        /*  bzero, for FD_SET */
#include <strings.h>       /*  bzero, for FD_ZERO (AIX) */
#include <netinet/in.h>    /*  INADDR_*, in_addr, sockaddr_in, htonl etc. */
#include <netdb.h>         /*  getservbyname */
#include <arpa/inet.h>     /*  inet_addr */
#include <errno.h>         /*  socket error codes */

#if defined(SOLARIS)
  /*  all this just to get ioctl(FIONREAD) to work (see man 7I streamio) */
  #if 1
    #include <stropts.h>
    #include <sys/conf.h>
    #define _FIONREAD I_NREAD
  #else
    #include <sys/filio.h>     /*  FIONREAD */
  #endif
#elif defined(AIX) && 0 /*  AIX has I_NREAD and the docs claim it has the right semantics */
                        /*  but it appears to be broken for sockets */
  #include <stropts.h>
  #define _FIONREAD I_NREAD
#elif defined(SUPERUX) && 0 /* similarly broken on SuperUX, despite the docs - what a disaster */
  #include <stropts.h>
  #define _FIONREAD I_NREAD
#else
  #define _FIONREAD FIONREAD
#endif

#if defined(SUPERUX)
  #include <sys/select.h>
#endif

/*  these constants are useful, but appear to be specific to */
/*  winsock; so, I define them here and use them as if they */
/*  were standard */
#define INVALID_SOCKET          ((SOCKET)(~0))
#define SOCKET_ERROR            (-1)

#define SD_RECEIVE SHUT_RD
#define SD_SEND SHUT_WR
#define SD_BOTH SHUT_RDWR

/*  some systems (like Linux!) incorrectly #define NULL as ((void*)0), where */
/*  the correct definition (for C++) is: */
#ifndef NULL
#define NULL 0
#endif

/*  SunOS apparently doesn't have this */
#ifndef INADDR_NONE
#  define INADDR_NONE           0xffffffff
#endif

/*  some linuxes are missing this */
#ifndef SHUT_RD 
  enum
  {
    SHUT_RD = 0,          /* No more receptions.  */
  #define SHUT_RD         SHUT_RD
    SHUT_WR,              /* No more transmissions.  */
  #define SHUT_WR         SHUT_WR
    SHUT_RDWR             /* No more receptions or transmissions.  */
  #define SHUT_RDWR       SHUT_RDWR
  };
#endif

/*  closesocket */
#define closesocket close
#if defined(__CYGWIN__)
#  include <sys/unistd.h>     /*  close */
#else   /*  __UNIX__ */
#  include <unistd.h>         /*  close */
#endif

/* ioctlsocket */
#ifdef MTA
#define ioctlsocket(a,b,c) ioctl((a),(b),(caddr_t)(c))
/* these are missing on MTA for some reason */
#ifdef __cplusplus
extern "C" {
#endif
  ssize_t      recv(int, void *, size_t, int); 
  ssize_t      send(int, const void *, size_t, int);
#ifdef __cplusplus
}
#endif
#else
#define ioctlsocket ioctl
#endif

typedef unsigned int SOCKET;
typedef fd_set FD_SET;
#endif

#ifdef __cplusplus
  #define BEGIN_EXTERNC extern "C" {
  #define END_EXTERNC }
  #define EXTERNC extern "C"
#else
  #define BEGIN_EXTERNC 
  #define END_EXTERNC 
  #define EXTERNC
#endif

/*  resolve disagreements about types of arguments to misc. functions */
#if defined(LINUX) || defined(FREEBSD) || defined(NETBSD) || defined(SOLARIS) || (defined(AIX) && defined(_AIX51))
#  define GETSOCKNAME_LENGTH_T socklen_t
#  define GETSOCKOPT_LENGTH_T socklen_t
#elif defined(AIX)
#  define GETSOCKNAME_LENGTH_T size_t
#  define GETSOCKOPT_LENGTH_T size_t
#else
#  define GETSOCKNAME_LENGTH_T int
#  define GETSOCKOPT_LENGTH_T int
#endif

#if defined(WIN32) || defined(CYGWIN) || defined(AIX) || \
    defined(SOLARIS) || defined(LINUX) || defined(OSF) || defined(SUPERUX) || \
   defined(__crayx1) /* X1 docs claim it's a size_t, they lie */
  #define IOCTL_FIONREAD_ARG_T unsigned int
#elif defined(IRIX)
  #define IOCTL_FIONREAD_ARG_T size_t
#elif defined(MTA)
  #define IOCTL_FIONREAD_ARG_T size_t
#else
  #define IOCTL_FIONREAD_ARG_T unsigned long
#endif

/* addr-length argument type fiasco.. */
#if defined(LINUX) || defined(FREEBSD) || defined(AIX) || \
    defined(SOLARIS) || defined(NETBSD)
#  define LENGTH_PARAM socklen_t
#elif defined(OSF1)
#  define LENGTH_PARAM unsigned long
#else
#  define LENGTH_PARAM int
#endif

#if defined(AMUDP_DEBUG) || defined(AMUDP_NDEBUG) || \
    defined(GASNET_DEBUG) || defined(GASNET_NDEBUG)
  #define SOCK_USE_C_BYPASS 1 /* Use C-mode bypass for system calls with non-portable signatures */
#endif

#if SOCK_USE_C_BYPASS
  BEGIN_EXTERNC
  extern int SOCK_getsockopt(int  s, int level, int optname, void *optval, GETSOCKOPT_LENGTH_T *optlen);
  extern int SOCK_getsockname(int s, struct sockaddr *name, GETSOCKNAME_LENGTH_T *namelen);
  extern int SOCK_getpeername(int s, struct sockaddr *name, GETSOCKNAME_LENGTH_T *namelen);
  extern int SOCK_ioctlsocket(int d, int request, IOCTL_FIONREAD_ARG_T *val);
  extern int SOCK_accept(SOCKET listener, struct sockaddr* calleraddr, LENGTH_PARAM *sz);
  extern int SOCK_recvfrom(SOCKET s, char * buf, int len, int flags, struct sockaddr *from, LENGTH_PARAM *sz);
  END_EXTERNC
#else
  #define SOCK_getsockopt   getsockopt
  #define SOCK_getsockname  getsockname
  #define SOCK_getpeername  getpeername
  #define SOCK_ioctlsocket  ioctlsocket
  #define SOCK_accept       accept
  #define SOCK_recvfrom     recvfrom
#endif

#endif /*  __SOCKET_H */

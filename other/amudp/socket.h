/*   $Archive:: /Ti/AMUDP/socket.h                                         $
 *      $Date: 2004/01/19 12:57:33 $
 *  $Revision: 1.2 $
 *  Description: portable header socket functions
 *  (c) Scott McPeak, 1998-1999, Modified by Dan Bonachea
 */

#ifndef SOCKET_H
#define SOCKET_H

/*  ------------- win32 -------------------- */
#ifdef WIN32

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
#else
  #define _FIONREAD FIONREAD
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
#define ioctlsocket ioctl

typedef unsigned int SOCKET;
typedef fd_set FD_SET;
#endif


/*  resolve disagreements about types of arguments to misc. functions */
#if defined(LINUX) || defined(FREEBSD) || defined(SOLARIS) || (defined(AIX) && defined(_AIX51))
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
    defined(SOLARIS) || defined(LINUX) || defined(OSF)
  #define IOCTL_FIONREAD_ARG_T unsigned int
#elif defined(IRIX)
  #define IOCTL_FIONREAD_ARG_T size_t
#else
  #define IOCTL_FIONREAD_ARG_T unsigned long
#endif

#endif /*  __SOCKET_H */

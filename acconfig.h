/*   $Archive:: /Ti/GASNet/acconfig.h                                      $ */
/*      $Date: 2003/11/12 08:56:03 $ */
/*  $Revision: 1.21.4.1 $ */
/*  Description: GASNet acconfig.h (or config.h)                             */
/*  Copyright 2002, Dan Bonachea <bonachea@cs.berkeley.edu>                  */
/* Terms of use are as specified in license.txt */

#ifndef _INCLUDE_GASNET_CONFIG_H_
#define _INCLUDE_GASNET_CONFIG_H_
#if !defined(_IN_GASNET_H) && !defined(_IN_GASNET_TOOLS_H)
  #error This file is not meant to be included directly- clients should include gasnet.h or gasnet_tools.h
#endif

@TOP@

/* using the MIPSPro C compiler (which doesn't seem to have other identifying markers) */
#undef MIPSPRO_COMPILER

/* Defined to be the inline function modifier supported by the C compiler (if supported) */
#undef CC_INLINE_MODIFIER

/* Functions may be declared "static inline" */
#undef STATIC_INLINE_WORKS

/* have mmap() */
#undef HAVE_MMAP

/* has usleep() */
#undef HAVE_USLEEP

/* has nanosleep() */
#undef HAVE_NANOSLEEP

/* has nsleep() */
#undef HAVE_NSLEEP

/* has sched_yield() */
#undef HAVE_SCHED_YIELD

/* has pthread_kill_other_threads_np() */
#undef HAVE_PTHREAD_KILL_OTHER_THREADS_NP

/* has __builtin_expect */
#undef HAVE_BUILTIN_EXPECT

/* has __func__ function name defined */
#undef HAVE_FUNC

/* Linux asm/atomic.h broken */
#undef BROKEN_LINUX_ASM_ATOMIC_H

/* forcing UP build, even if build platform is a multi-processor */
#undef GASNETI_UNI_BUILD

/* auto-detected mmap data page size */
#undef GASNETI_PAGESIZE
#undef GASNETI_PAGESHIFT

/* various OS and machine definitions */
#undef UNIX
#undef LINUX
#undef FREEBSD
#undef SOLARIS
#undef UNICOS
#undef CRAYT3E
#undef CRAYX1
#undef AIX
#undef OSF
#undef HPUX
#undef IRIX
#undef CYGWIN
#undef DARWIN

/* GASNet segment definition */
#undef GASNET_SEGMENT_FAST
#undef GASNET_SEGMENT_LARGE
#undef GASNET_SEGMENT_EVERYTHING

/* GASNet vapi-conduit bug work-arounds */
#undef GASNETC_VAPI_ENABLE_INLINE_PUTS
#undef GASNETC_VAPI_FORCE_POLL_LOCK

@BOTTOM@

/* special GCC features */
#if ! defined (__GNUC__) && ! defined (__attribute__)
#define __attribute__(flags)
#endif

#if defined(__GNUC__)
#define GASNET_NORETURN __attribute__((__noreturn__))
#else
#define GASNET_NORETURN 
#endif



#endif

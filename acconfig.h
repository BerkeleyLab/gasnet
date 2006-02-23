/*    $Source: /Users/kamil/work/gasnet-cvs2/gasnet/acconfig.h,v $ */
/*      $Date: 2006/02/23 12:36:38 $ */
/*  $Revision: 1.61.2.2 $ */
/*  Description: GASNet acconfig.h (or config.h)                             */
/*  Copyright 2002, Dan Bonachea <bonachea@cs.berkeley.edu>                  */
/* Terms of use are as specified in license.txt */

#ifndef _INCLUDE_GASNET_CONFIG_H_
#define _INCLUDE_GASNET_CONFIG_H_
#if !defined(_IN_GASNET_H) && !defined(_IN_GASNET_TOOLS_H)
  #error This file is not meant to be included directly- clients should include gasnet.h or gasnet_tools.h
#endif

@TOP@

#undef GASNETI_BUILD_ID
#undef GASNETI_CONFIGURE_ARGS
#undef GASNETI_SYSTEM_TUPLE
#undef GASNETI_SYSTEM_NAME

/* using the MIPSPro C compiler (which doesn't seem to have other identifying markers) */
#undef MIPSPRO_COMPILER

/* Defined to be the inline function modifier supported by the C compiler (if supported) */
#undef CC_INLINE_MODIFIER

/* C compilers 'restrict' keyword (or empty) */
#undef GASNETI_RESTRICT

/* true iff GASNETI_RESTRICT may be applied to types which are not pointer types until after typedef expansion */
#undef GASNETI_RESTRICT_MAY_QUALIFY_TYPEDEFS

/* Functions may be declared "static inline" */
#undef STATIC_INLINE_WORKS

/* have mmap() */
#undef HAVE_MMAP

/* --with-segment-mmap-max value (if given) */
#undef GASNETI_MMAP_MAX_SIZE

/* has usleep() */
#undef HAVE_USLEEP

/* has nanosleep() */
#undef HAVE_NANOSLEEP

/* has nsleep() */
#undef HAVE_NSLEEP

/* has sched_yield() */
#undef HAVE_SCHED_YIELD

/* have ptmalloc's mallopt() options */
#undef HAVE_PTMALLOC

/* have pthread_setconcurrency */
#undef HAVE_PTHREAD_SETCONCURRENCY

/* has pthread_kill_other_threads_np() */
#undef HAVE_PTHREAD_KILL_OTHER_THREADS_NP

/* have pause instruction, only relevant on Pentium4 */
#undef HAVE_X86_PAUSE_INSTRUCTION

/* has __builtin_expect */
#undef HAVE_BUILTIN_EXPECT

/* has __builtin_prefetch */
#undef HAVE_BUILTIN_PREFETCH

/* has __func__ function name defined */
#undef HAVE_FUNC

/* portable inttypes support */
#undef HAVE_INTTYPES_H
#undef HAVE_STDINT_H
#undef HAVE_SYS_TYPES_H
#undef COMPLETE_INTTYPES_H
#undef COMPLETE_STDINT_H
#undef COMPLETE_SYS_TYPES_H

/* Linux PR_SET_PDEATHSIG support */
#undef HAVE_PR_SET_PDEATHSIG

/* Linux asm/atomic.h broken */
#undef BROKEN_LINUX_ASM_ATOMIC_H

/* forcing UP build, even if build platform is a multi-processor */
#undef GASNETI_UNI_BUILD

/* force memory barriers on GASNet local (loopback) puts and gets */
#undef GASNETI_MEMSYNC_ON_LOOPBACK

/* throttle polling threads in multi-threaded configurations to reduce contention */
#undef GASNETI_THROTTLE_FEATURE_ENABLED

/* auto-detected mmap data page size */
#undef GASNETI_PAGESIZE
#undef GASNETI_PAGESHIFT

/* auto-detected shared data cache line size */
#undef GASNETI_CACHE_LINE_BYTES

/* udp-conduit default custom spawn command */
#undef GASNET_CSPAWN_CMD

/* various OS and machine definitions */
#undef UNIX
#undef LINUX
#undef FREEBSD
#undef NETBSD
#undef SOLARIS
#undef UNICOS
#undef CRAYT3E
#undef CRAYX1
#undef MTA
#undef AIX
#undef OSF
#undef HPUX
#undef SUPERUX
#undef IRIX
#undef CYGWIN
#undef DARWIN
#undef ALTIX
#undef GASNETI_ARCH_SPARCV9
#undef CATAMOUNT

/* GASNet segment definition */
#undef GASNET_SEGMENT_FAST
#undef GASNET_SEGMENT_LARGE
#undef GASNET_SEGMENT_EVERYTHING

/* GASNet ref-extended settings */
#undef GASNETE_USE_AMDISSEMINATION_REFBARRIER

/* GASNet gm-conduit broken 2.x versions */
#undef GASNETC_GM_ENABLE_BROKEN_VERSIONS
#undef GASNETC_GM_MPI_COMPAT

/* GASNet vapi-conduit features and bug work-arounds */
#undef HAVE_VAPI_FMR
#undef GASNETC_VAPI_FORCE_POLL_LOCK
#undef GASNETC_VAPI_RCV_THREAD

/* GASNet lapi-conduit specific */
#undef GASNETC_LAPI_FEDERATION
#undef GASNETC_LAPI_COLONY
#undef GASNETC_LAPI_VERSION_A
#undef GASNETC_LAPI_VERSION_B
#undef GASNETC_LAPI_VERSION_C
#undef GASNETC_LAPI_VERSION_D

/* GASNet elan-conduit specific */
#undef GASNETC_ELAN3
#undef GASNETC_ELAN4
#undef ELAN_VERSION_MAJOR
#undef ELAN_VERSION_MINOR
#undef ELAN_VERSION_SUB
#undef ELAN_DRIVER_VERSION
#undef ELAN4_KERNEL_PATCH
#undef HAVE_RMS_RMSAPI_H
#undef HAVE_RMS_KILLRESOURCE
#undef RMS_RCONTROL_PATH
#undef SLURM_SCANCEL_PATH
#undef HAVE_SLURM_SLURM_H
#undef HAVE_SLURM_KILL_JOB
#undef HAVE_ELAN_QUEUEMAXSLOTSIZE
#undef HAVE_ELAN_DONE
#undef HAVE_ELAN_QUEUETXINIT

@BOTTOM@

/* special GCC features */
#if ! defined (__GNUC__) && ! defined (__attribute__)
#define __attribute__(flags)
#endif

#if defined(__GNUC__) && !(__GNUC__ <= 2 && __GNUC_MINOR__ <= 95)
  #define GASNETI_MALLOC __attribute__((__malloc__))
#else
  /* malloc attribute missing in egcs-2.91.66, gcc 2.95.4, and non-gcc compilers */
  #define GASNETI_MALLOC
#endif

#if defined(__GNUC__)
#define GASNETI_NORETURN __attribute__((__noreturn__))
#else
#define GASNETI_NORETURN 
#endif

#endif

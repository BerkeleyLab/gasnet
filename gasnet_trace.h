/*  $Archive:: /Ti/GASNet/gasnet_trace.h                                   $
 *     $Date: 2004/04/10 12:57:42 $
 * $Revision: 1.20 $
 * Description: GASNet Tracing Helpers (Internal code, not for client use)
 * Copyright 2002, Dan Bonachea <bonachea@cs.berkeley.edu>
 * Terms of use are as specified in license.txt
 */

#ifndef _IN_GASNET_H
  #error This file is not meant to be included directly- clients should include gasnet.h
#endif

#ifndef _GASNET_TRACE_H
#define _GASNET_TRACE_H

#include <stdio.h>

BEGIN_EXTERNC

/* ------------------------------------------------------------------------------------ */
/* Statistical collection & tracing 
   See README for user-interface usage information
*/

#if GASNETI_STATS_OR_TRACE
  /* emit trace info and increment a stat ctr */
  #define GASNETI_TRACE_EVENT(type, name) do { \
       _GASNETI_STAT_EVENT (type, name);       \
       _GASNETI_TRACE_EVENT(type, name);       \
      } while (0)

  /* emit trace info and accumulate an integer stat value */
  #define GASNETI_TRACE_EVENT_VAL(type, name, val) do { \
       gasneti_statctr_t _val = (val);                  \
       _GASNETI_STAT_EVENT_VAL (type, name, _val);      \
       _GASNETI_TRACE_EVENT_VAL(type, name, _val);      \
      } while (0)

  /* emit trace info and accumulate a time stat value */
  #define GASNETI_TRACE_EVENT_TIME(type, name, time) do { \
       gasneti_stattime_t _time = (time);                 \
       _GASNETI_STAT_EVENT_TIME(type, name, _time);       \
       _GASNETI_TRACE_EVENT_TIME(type, name, _time);      \
      } while (0)
#else
  #define GASNETI_TRACE_EVENT(type, name)
  #define GASNETI_TRACE_EVENT_VAL(type, name, val)
  #define GASNETI_TRACE_EVENT_TIME(type, name, time)
#endif

#if GASNET_TRACE
  /* print a string on the trace 
     Ex: GASNETI_TRACE_MSG(C, "init complete") */
  #define GASNETI_TRACE_MSG(type, string) \
      GASNETI_TRACE_PRINTF(type, ("%s",(string)))

  /* print a formatted string on output
     Ex: GASNETI_TRACE_PRINTF(C, ("%i buffers free", numbufs))
      (note the extra parentheses around arg)
  */
  #define GASNETI_TRACE_PRINTF(type, args) do { \
    if (GASNETI_TRACE_ENABLED(type)) {          \
      char *_msg = gasneti_dynsprintf args;     \
      gasneti_trace_output(#type, _msg, 1);     \
    }                                           \
  } while(0)
#else
  #define GASNETI_TRACE_MSG(type, string) 
  #define GASNETI_TRACE_PRINTF(type, args)
#endif

#if GASNET_STATS
  /* print an arbitrary string of statistical output on the trace 
     Ex: GASNETI_STATS_MSG(C, "init complete") */
  #define GASNETI_STATS_MSG(type, string) \
      GASNETI_STATS_PRINTF(type, ("%s",(string)))

  /* print a formatted string of statistical output on the trace 
     Ex: GASNETI_STATS_PRINTF(C, ("%i buffers free", numbufs))
      (note the extra parentheses around arg)
  */
  #define GASNETI_STATS_PRINTF(type, args) do { \
    if (GASNETI_STATS_ENABLED(type)) {          \
      char *_msg = gasneti_dynsprintf args;     \
      gasneti_stats_output(#type, _msg, 0);     \
    }                                           \
  } while(0)
#else
  #define GASNETI_STATS_MSG(type, string) 
  #define GASNETI_STATS_PRINTF(type, args)
#endif

/* allow for final output of conduit-specific statistics */
#ifndef GASNETC_TRACE_FINISH
#define GASNETC_TRACE_FINISH()
#endif

#ifndef GASNETI_STATS_ECHOED_TO_TRACEFILE
#define GASNETI_STATS_ECHOED_TO_TRACEFILE 1
#endif

#if GASNET_TRACE
  /* GASNETI_TRACE_SETSOURCELINE(filename, linenum): 
     set the current "high-level" source file and line for the current thread
     to the given value. This information is used to dump out current line information
     into the tracefile along with each trace message.
     Passing a NULL filename implies no change to the current filename
   */
  #if GASNETI_CLIENT_THREADS
    extern void gasneti_trace_setsourceline(const char *filename, unsigned int linenum);
  #else
    extern const char *gasneti_srcfilename;
    extern unsigned int gasneti_srclinenum;
    GASNET_INLINE_MODIFIER(gasneti_trace_setsourceline)
    void gasneti_trace_setsourceline(const char *filename, unsigned int linenum) {
      if_pt (filename != NULL) gasneti_srcfilename = filename;
      gasneti_srclinenum = linenum;
    }
  #endif
    #define GASNETI_TRACE_SETSOURCELINE(filename, linenum) \
      (GASNETI_TRACE_ENABLED(N) ? gasneti_trace_setsourceline(filename, linenum) : ((void)0))
#else
  #define GASNETI_TRACE_SETSOURCELINE(filename, linenum) ((void)0)
#endif

/* ------------------------------------------------------------------------------------ */
/* misc helpers for specific tracing scenarios */
#ifdef GASNETI_PTR32 
  #define GASNETI_LADDRFMT "0x%08x"
  #define GASNETI_LADDRSTR(ptr) ((unsigned int)(uintptr_t)(ptr))
  #define GASNETI_RADDRFMT "(%i,0x%08x)"
  #define GASNETI_RADDRSTR(node,ptr) ((int)(node)),GASNETI_LADDRSTR(ptr)
#else
  #define GASNETI_LADDRFMT "0x%08x %08x"
  #define GASNETI_LADDRSTR(ptr) GASNETI_HIWORD(ptr), GASNETI_LOWORD(ptr)
  #define GASNETI_RADDRFMT "(%i,0x%08x %08x)"
  #define GASNETI_RADDRSTR(node,ptr) ((int)(node)),GASNETI_LADDRSTR(ptr)
#endif

#define GASNETI_TRACE_GET(name,dest,node,src,nbytes) do {                                          \
  GASNETI_TRACE_EVENT_VAL(G,name,(nbytes));                                                        \
  GASNETI_TRACE_PRINTF(D,(#name ": "GASNETI_LADDRFMT" <- "GASNETI_RADDRFMT" (%i bytes)",           \
                          GASNETI_LADDRSTR(dest), GASNETI_RADDRSTR((node),(src)), (int)(nbytes))); \
} while (0)

#define GASNETI_TRACE_PUT(name,node,dest,src,nbytes) do {                                        \
  GASNETI_TRACE_EVENT_VAL(P,name,(nbytes));                                                      \
  GASNETI_TRACE_PRINTF(D,(#name ": "GASNETI_RADDRFMT" <- "GASNETI_LADDRFMT" (%i bytes): %s",     \
                          GASNETI_RADDRSTR((node),(dest)), GASNETI_LADDRSTR(src), (int)(nbytes), \
                          gasneti_formatdata((src),(nbytes))));                                  \
} while (0)

#define GASNETI_TRACE_MEMSET(name,node,dest,val,nbytes) do {                       \
  GASNETI_TRACE_EVENT_VAL(P,name,(nbytes));                                        \
  GASNETI_TRACE_PRINTF(D,(#name": "GASNETI_RADDRFMT" val=%i nbytes=%i",            \
                          GASNETI_RADDRSTR((node),(dest)), (val), (int)(nbytes))); \
} while (0)
/*------------------------------------------------------------------------------------*/
#define GASNETI_TRACE_TRYSYNC(name,success) \
  GASNETI_TRACE_EVENT_VAL(S,name,((success) == GASNET_OK?1:0))

#if GASNETI_STATS_OR_TRACE
  #define GASNETI_TRACE_WAITSYNC_BEGIN() \
    gasneti_stattime_t _waitstart = GASNETI_STATTIME_NOW_IFENABLED(S)
#else 
  #define GASNETI_TRACE_WAITSYNC_BEGIN() \
    static char _dummy = (char)sizeof(_dummy)
#endif

#if GASNET_STATS
  typedef void (*gasnett_stats_callback_t)(void (*)(const char *, ...));
  extern gasnett_stats_callback_t gasnett_stats_callback;
#endif

#define GASNETI_TRACE_WAITSYNC_END(name) \
  GASNETI_TRACE_EVENT_TIME(S,name,GASNETI_STATTIME_NOW_IFENABLED(S) - _waitstart)
/*------------------------------------------------------------------------------------*/
/* AM Request/Reply tracing helpers */
#define _GASNETI_TRACE_GATHERARGS(numargs)                          \
  char argstr[256];                                                 \
  do {                                                              \
    int i;                                                          \
    va_list _argptr;                                                \
    *argstr='\0';                                                   \
    va_start(_argptr, numargs); /*  assumes last arg was numargs */ \
      for (i=0;i<numargs;i++) {                                     \
        char temp[20];                                              \
        /* must be int due to default argument promotion */         \
        sprintf(temp," 0x%08x",(int)(uint32_t)va_arg(_argptr,int)); \
        strcat(argstr,temp);                                        \
      }                                                             \
    va_end(_argptr);                                                \
  } while(0)

#define GASNETI_TRACE_AMSHORT(name,dest,handler,numargs) do {                        \
  _GASNETI_TRACE_GATHERARGS(numargs);                                                \
  _GASNETI_STAT_EVENT(A,name);                                                       \
  GASNETI_TRACE_PRINTF(A,(#name": dest=%i handler=%i args:%s",(int)dest,handler,argstr)); \
} while(0)

#define GASNETI_TRACE_AMMEDIUM(name,dest,handler,source_addr,nbytes,numargs) do {                       \
  _GASNETI_TRACE_GATHERARGS(numargs);                                                                   \
  _GASNETI_STAT_EVENT(A,name);                                                                          \
  GASNETI_TRACE_PRINTF(A,(#name": dest=%i handler=%i source_addr="GASNETI_LADDRFMT" nbytes=%i args:%s", \
    (int)dest,handler,GASNETI_LADDRSTR(source_addr),(int)nbytes,argstr));                                    \
  GASNETI_TRACE_PRINTF(D,(#name": payload data: %s", gasneti_formatdata(source_addr,nbytes)));          \
} while(0)

#define GASNETI_TRACE_AMLONG(name,dest,handler,source_addr,nbytes,dest_addr,numargs) do {                                            \
  _GASNETI_TRACE_GATHERARGS(numargs);                                                                                                \
  _GASNETI_STAT_EVENT(A,name);                                                                                                       \
  GASNETI_TRACE_PRINTF(A,(#name": dest=%i handler=%i source_addr="GASNETI_LADDRFMT" nbytes=%i dest_addr="GASNETI_LADDRFMT" args:%s", \
    (int)dest,handler,GASNETI_LADDRSTR(source_addr),(int)nbytes,GASNETI_LADDRSTR(dest_addr),argstr));                                     \
  GASNETI_TRACE_PRINTF(D,(#name": payload data: %s", gasneti_formatdata(source_addr,nbytes)));                                       \
} while(0)

#if GASNET_TRACE
  #define GASNETI_TRACE_AMREQUESTSHORT(dest,handler,numargs) \
          GASNETI_TRACE_AMSHORT(AMREQUEST_SHORT,dest,handler,numargs)
  #define GASNETI_TRACE_AMREPLYSHORT(token,handler,numargs) do {         \
          gasnet_node_t temp;                                            \
          if (gasnet_AMGetMsgSource(token,&temp) != GASNET_OK)           \
            gasneti_fatalerror("gasnet_AMGetMsgSource() failed");        \
          GASNETI_TRACE_AMSHORT(AMREPLY_SHORT,temp,handler,numargs);     \
          GASNETI_TRACE_PRINTF(C,("AMREPLY_SHORT: Reply token: %s",      \
                            gasneti_formatdata(&token, sizeof(token)))); \
  } while(0)

  #define GASNETI_TRACE_AMREQUESTMEDIUM(dest,handler,source_addr,nbytes,numargs) \
          GASNETI_TRACE_AMMEDIUM(AMREQUEST_MEDIUM,dest,handler,source_addr,nbytes,numargs)
  #define GASNETI_TRACE_AMREPLYMEDIUM(token,handler,source_addr,nbytes,numargs) do {      \
          gasnet_node_t temp;                                                             \
          if (gasnet_AMGetMsgSource(token,&temp) != GASNET_OK)                            \
            gasneti_fatalerror("gasnet_AMGetMsgSource() failed");                         \
          GASNETI_TRACE_AMMEDIUM(AMREPLY_MEDIUM,temp,handler,source_addr,nbytes,numargs); \
          GASNETI_TRACE_PRINTF(C,("AMREPLY_MEDIUM: Reply token: %s",                      \
                            gasneti_formatdata(&token, sizeof(token))));                  \
  } while(0)

  #define GASNETI_TRACE_AMREQUESTLONG(dest,handler,source_addr,nbytes,dest_addr,numargs) \
          GASNETI_TRACE_AMLONG(AMREQUEST_LONG,dest,handler,source_addr,nbytes,dest_addr,numargs)
  #define GASNETI_TRACE_AMREPLYLONG(token,handler,source_addr,nbytes,dest_addr,numargs) do {    \
          gasnet_node_t temp;                                                                   \
          if (gasnet_AMGetMsgSource(token,&temp) != GASNET_OK)                                  \
            gasneti_fatalerror("gasnet_AMGetMsgSource() failed");                               \
          GASNETI_TRACE_AMLONG(AMREPLY_LONG,temp,handler,source_addr,nbytes,dest_addr,numargs); \
          GASNETI_TRACE_PRINTF(C,("AMREPLY_LONG: Reply token: %s",                              \
                            gasneti_formatdata(&token, sizeof(token))));                        \
  } while(0)

  #define GASNETI_TRACE_AMREQUESTLONGASYNC(dest,handler,source_addr,nbytes,dest_addr,numargs) \
          GASNETI_TRACE_AMLONG(AMREQUEST_LONGASYNC,dest,handler,source_addr,nbytes,dest_addr,numargs)

#elif GASNET_STATS
  #define GASNETI_TRACE_AMREQUESTSHORT(dest,handler,numargs) \
     GASNETI_TRACE_EVENT(A,AMREQUEST_SHORT)
  #define GASNETI_TRACE_AMREPLYSHORT(token,handler,numargs) \
     GASNETI_TRACE_EVENT(A,AMREPLY_SHORT)
  #define GASNETI_TRACE_AMREQUESTMEDIUM(dest,handler,source_addr,nbytes,numargs) \
     GASNETI_TRACE_EVENT(A,AMREQUEST_MEDIUM)
  #define GASNETI_TRACE_AMREPLYMEDIUM(token,handler,source_addr,nbytes,numargs) \
     GASNETI_TRACE_EVENT(A,AMREPLY_MEDIUM)
  #define GASNETI_TRACE_AMREQUESTLONG(dest,handler,source_addr,nbytes,dest_addr,numargs) \
     GASNETI_TRACE_EVENT(A,AMREQUEST_LONG)
  #define GASNETI_TRACE_AMREPLYLONG(token,handler,source_addr,nbytes,dest_addr,numargs) \
     GASNETI_TRACE_EVENT(A,AMREPLY_LONG)
  #define GASNETI_TRACE_AMREQUESTLONGASYNC(dest,handler,source_addr,nbytes,dest_addr,numargs) \
     GASNETI_TRACE_EVENT(A,AMREQUEST_LONGASYNC)
#else
  #define GASNETI_TRACE_AMREQUESTSHORT(dest,handler,numargs) 
  #define GASNETI_TRACE_AMREPLYSHORT(token,handler,numargs) 
  #define GASNETI_TRACE_AMREQUESTMEDIUM(dest,handler,source_addr,nbytes,numargs) 
  #define GASNETI_TRACE_AMREPLYMEDIUM(token,handler,source_addr,nbytes,numargs) 
  #define GASNETI_TRACE_AMREQUESTLONG(dest,handler,source_addr,nbytes,dest_addr,numargs) 
  #define GASNETI_TRACE_AMREPLYLONG(token,handler,source_addr,nbytes,dest_addr,numargs) 
  #define GASNETI_TRACE_AMREQUESTLONGASYNC(dest,handler,source_addr,nbytes,dest_addr,numargs)
#endif
/* ------------------------------------------------------------------------------------ */
/* AM Handler tracing */
#if GASNETI_STATS_OR_TRACE
  #define _GASNETI_TRACE_GATHERHANDLERARGS(numargs, arghandle)              \
    char argstr[256];                                                       \
    do {                                                                    \
      int i;                                                                \
      *argstr='\0';                                                         \
      for (i=0;i<numargs;i++) {                                             \
        char temp[20];                                                      \
        /* here we assume args are stored in an array named by arghandle */ \
        sprintf(temp," 0x%08x",(int)((uint32_t*)arghandle)[i]);             \
        strcat(argstr,temp);                                                \
      }                                                                     \
    } while(0)

  #define _GASNETI_TRACE_AMSHORT_HANDLER(name, handlerid, token, numargs, arghandle) do { \
    gasnet_node_t src;                                                                    \
    _GASNETI_TRACE_GATHERHANDLERARGS(numargs, arghandle);                                 \
    _GASNETI_STAT_EVENT(A,name);                                                          \
    if (gasnet_AMGetMsgSource(token,&src) != GASNET_OK)                                   \
      gasneti_fatalerror("gasnet_AMGetMsgSource() failed");                               \
    GASNETI_TRACE_PRINTF(A,(#name": src=%i handler=%i args:%s",                           \
      (int)src,(int)(handlerid),argstr));                                                 \
    GASNETI_TRACE_PRINTF(C,(#name": token: %s",                                           \
                      gasneti_formatdata(&token, sizeof(token))));                        \
    } while(0)

  #define _GASNETI_TRACE_AMMEDLONG_HANDLER(name, handlerid, token, addr, nbytes, numargs, arghandle) do { \
    gasnet_node_t src;                                                                                    \
    _GASNETI_TRACE_GATHERHANDLERARGS(numargs, arghandle);                                                 \
    _GASNETI_STAT_EVENT(A,name);                                                                          \
    if (gasnet_AMGetMsgSource(token,&src) != GASNET_OK)                                                   \
      gasneti_fatalerror("gasnet_AMGetMsgSource() failed");                                               \
    GASNETI_TRACE_PRINTF(A,(#name": src=%i handler=%i addr="GASNETI_LADDRFMT" nbytes=%i args:%s",         \
      (int)src,(int)(handlerid),GASNETI_LADDRSTR(addr),nbytes,argstr));                                   \
    GASNETI_TRACE_PRINTF(C,(#name": token: %s",                                                           \
                      gasneti_formatdata(&token, sizeof(token))));                                        \
    GASNETI_TRACE_PRINTF(D,(#name": payload data: %s", gasneti_formatdata(addr,nbytes)));                 \
  } while(0)

  #define GASNETI_TRACE_AMSHORT_REQHANDLER(handlerid, token, numargs, arghandle) \
         _GASNETI_TRACE_AMSHORT_HANDLER(AMREQUEST_SHORT_HANDLER, handlerid, token, numargs, arghandle)
  #define GASNETI_TRACE_AMSHORT_REPHANDLER(handlerid, token, numargs, arghandle) \
         _GASNETI_TRACE_AMSHORT_HANDLER(AMREPLY_SHORT_HANDLER, handlerid, token, numargs, arghandle)

  #define GASNETI_TRACE_AMMEDIUM_REQHANDLER(handlerid, token, addr, nbytes, numargs, arghandle) \
         _GASNETI_TRACE_AMMEDLONG_HANDLER(AMREQUEST_MEDIUM_HANDLER, handlerid, token, addr, nbytes, numargs, arghandle)
  #define GASNETI_TRACE_AMMEDIUM_REPHANDLER(handlerid, token, addr, nbytes, numargs, arghandle) \
         _GASNETI_TRACE_AMMEDLONG_HANDLER(AMREPLY_MEDIUM_HANDLER, handlerid, token, addr, nbytes, numargs, arghandle)

  #define GASNETI_TRACE_AMLONG_REQHANDLER(handlerid, token, addr, nbytes, numargs, arghandle) \
         _GASNETI_TRACE_AMMEDLONG_HANDLER(AMREQUEST_LONG_HANDLER, handlerid, token, addr, nbytes, numargs, arghandle)
  #define GASNETI_TRACE_AMLONG_REPHANDLER(handlerid, token, addr, nbytes, numargs, arghandle) \
         _GASNETI_TRACE_AMMEDLONG_HANDLER(AMREPLY_LONG_HANDLER, handlerid, token, addr, nbytes, numargs, arghandle)
#else
  #define GASNETI_TRACE_AMSHORT_REQHANDLER(handlerid, token, numargs, arghandle) 
  #define GASNETI_TRACE_AMSHORT_REPHANDLER(handlerid, token, numargs, arghandle) 
  #define GASNETI_TRACE_AMMEDIUM_REQHANDLER(handlerid, token, addr, nbytes, numargs, arghandle) 
  #define GASNETI_TRACE_AMMEDIUM_REPHANDLER(handlerid, token, addr, nbytes, numargs, arghandle) 
  #define GASNETI_TRACE_AMLONG_REQHANDLER(handlerid, token, addr, nbytes, numargs, arghandle) 
  #define GASNETI_TRACE_AMLONG_REPHANDLER(handlerid, token, addr, nbytes, numargs, arghandle) 
#endif
/* ------------------------------------------------------------------------------------ */
/* Vector, Indexed & Strided tracing */
#if GASNETI_STATS_OR_TRACE
  #define _GASNETI_TRACE_PUTVGETV(name,type,node,dstcount,dstlist,srccount,srclist) do {                   \
    if (GASNETI_TRACE_ENABLED(D)) {                                                                        \
      char * dstlist_str = gasneti_extern_malloc(gasneti_format_memveclist_bufsz(dstcount));               \
      char * srclist_str = gasneti_extern_malloc(gasneti_format_memveclist_bufsz(srccount));               \
      gasneti_memveclist_stats_t dststats = gasneti_format_memveclist(dstlist_str, (dstcount), (dstlist)); \
      gasneti_memveclist_stats_t srcstats = gasneti_format_memveclist(srclist_str, (srccount), (srclist)); \
      GASNETI_TRACE_EVENT_VAL(type,name,dststats.totalsz);                                                 \
      GASNETI_TRACE_PRINTF(D,(#name ": (%i data bytes) node=%i\n"                                          \
                              "dst: %s\nsrc: %s",                                                          \
                              (int)dststats.totalsz, (int)(node),                                          \
                              dstlist_str, srclist_str));                                                  \
      gasneti_extern_free(dstlist_str);                                                                    \
      gasneti_extern_free(srclist_str);                                                                    \
    } else GASNETI_TRACE_EVENT_VAL(type,name, gasnete_memveclist_stats(dstcount,dstlist).totalsz);                                                \
  } while (0)

  #define _GASNETI_TRACE_PUTIGETI(name,type,node,dstcount,dstlist,dstlen,srccount,srclist,srclen) do {           \
    if (GASNETI_TRACE_ENABLED(D)) {                                                                              \
      char * dstlist_str = gasneti_extern_malloc(gasneti_format_addrlist_bufsz(dstcount));                       \
      char * srclist_str = gasneti_extern_malloc(gasneti_format_addrlist_bufsz(srccount));                       \
      gasneti_addrlist_stats_t dststats = gasneti_format_addrlist(dstlist_str, (dstcount), (dstlist), (dstlen)); \
      gasneti_addrlist_stats_t srcstats = gasneti_format_addrlist(srclist_str, (srccount), (srclist), (srclen)); \
      uintptr_t totalsz = ((uintptr_t)(dstcount))*(dstlen);                                                      \
      GASNETI_TRACE_EVENT_VAL(type,name,totalsz);                                                                \
      GASNETI_TRACE_PRINTF(D,(#name ": (%i data bytes) node=%i\n"                                                \
                              "dst: %s\nsrc: %s",                                                                \
                              (int)totalsz, (int)(node),                                                         \
                              dstlist_str, srclist_str));                                                        \
      gasneti_extern_free(dstlist_str);                                                                          \
      gasneti_extern_free(srclist_str);                                                                          \
    } else GASNETI_TRACE_EVENT_VAL(type,name, (dstcount)*(dstlen));                                              \
  } while (0)

  #define _GASNETI_TRACE_PUTSGETS(name,type,node,dstaddr,dststrides,srcaddr,srcstrides,count,stridelevels) do {  \
    if (GASNETI_TRACE_ENABLED(D)) {                                                                              \
      gasnete_strided_stats_t _stats;                                                                            \
      gasnete_strided_stats(&_stats, (dststrides), (srcstrides), (count), (stridelevels));                       \
      GASNETI_TRACE_EVENT_VAL(type,name,_stats.totalsz);                                                         \
      GASNETI_TRACE_PRINTF(D,(#name ": (%i data bytes) node=%i stridelevels=%i count=%s\n"                       \
                              "dualcontiguity=%i nulldims=%i\n"                                                  \
                              "dst: dstaddr="GASNETI_LADDRFMT" dststrides=%s\n"                                  \
                              "     extent=%i bounds=["GASNETI_LADDRFMT"..."GASNETI_LADDRFMT"]\n"                \
                              "     contiguity=%i contigsz=%i contigsegments=%i\n"                               \
                              "src: srcaddr="GASNETI_LADDRFMT" srcstrides=%s\n"                                  \
                              "     extent=%i bounds=["GASNETI_LADDRFMT"..."GASNETI_LADDRFMT"]\n"                \
                              "     contiguity=%i contigsz=%i contigsegments=%i",                                \
                              (int)_stats.totalsz, (int)(node), (int)(stridelevels),                             \
                              gasneti_format_strides(stridelevels+1, count),                                     \
                              (int)_stats.dualcontiguity, (int)_stats.nulldims,                                  \
                              GASNETI_LADDRSTR(dstaddr),                                                         \
                              gasneti_format_strides(stridelevels, dststrides),                                  \
                              (int)_stats.dstextent,                                                             \
                              GASNETI_LADDRSTR(dstaddr), GASNETI_LADDRSTR((((char *)dstaddr)+_stats.dstextent)), \
                              (int)_stats.dstcontiguity, (int)_stats.dstcontigsz, (int)_stats.dstsegments,       \
                              GASNETI_LADDRSTR(srcaddr),                                                         \
                              gasneti_format_strides(stridelevels, srcstrides),                                  \
                              (int)_stats.srcextent,                                                             \
                              GASNETI_LADDRSTR(srcaddr), GASNETI_LADDRSTR((((char *)srcaddr)+_stats.srcextent)), \
                              (int)_stats.srccontiguity, (int)_stats.srccontigsz, (int)_stats.srcsegments        \
                              ));                                                                                \
    } else GASNETI_TRACE_EVENT_VAL(type,name, gasnete_strided_datasize(count,stridelevels));                     \
  } while (0)

  #define GASNETI_TRACE_PUTV(name,node,dstcount,dstlist,srccount,srclist) \
     _GASNETI_TRACE_PUTVGETV(name,P,node,dstcount,dstlist,srccount,srclist)
  #define GASNETI_TRACE_GETV(name,node,dstcount,dstlist,srccount,srclist) \
     _GASNETI_TRACE_PUTVGETV(name,G,node,dstcount,dstlist,srccount,srclist)

  #define GASNETI_TRACE_PUTI(name,node,dstcount,dstlist,dstlen,srccount,srclist,srclen) \
     _GASNETI_TRACE_PUTIGETI(name,P,node,dstcount,dstlist,dstlen,srccount,srclist,srclen)
  #define GASNETI_TRACE_GETI(name,node,dstcount,dstlist,dstlen,srccount,srclist,srclen) \
     _GASNETI_TRACE_PUTIGETI(name,G,node,dstcount,dstlist,dstlen,srccount,srclist,srclen)

  #define GASNETI_TRACE_PUTS(name,node,dstaddr,dststrides,srcaddr,srcstrides,count,stridelevels) \
     _GASNETI_TRACE_PUTSGETS(name,P,node,dstaddr,dststrides,srcaddr,srcstrides,count,stridelevels)
  #define GASNETI_TRACE_GETS(name,node,dstaddr,dststrides,srcaddr,srcstrides,count,stridelevels) \
     _GASNETI_TRACE_PUTSGETS(name,G,node,dstaddr,dststrides,srcaddr,srcstrides,count,stridelevels)
#else
  #define GASNETI_TRACE_PUTV(name,node,dstcount,dstlist,srccount,srclist)
  #define GASNETI_TRACE_GETV(name,node,dstcount,dstlist,srccount,srclist)
  #define GASNETI_TRACE_PUTI(name,node,dstcount,dstlist,dstlen,srccount,srclist,srclen)
  #define GASNETI_TRACE_GETI(name,node,dstcount,dstlist,dstlen,srccount,srclist,srclen)
  #define GASNETI_TRACE_PUTS(name,node,dstaddr,dststrides,srcaddr,srcstrides,count,stridelevels)
  #define GASNETI_TRACE_GETS(name,node,dstaddr,dststrides,srcaddr,srcstrides,count,stridelevels)
#endif
/* ------------------------------------------------------------------------------------ */
/* Internal implementation of statistical/tracing output */

typedef uint64_t gasneti_statctr_t;
#define GASNETI_STATCTR_MIN ((gasneti_statctr_t)0)
#define GASNETI_STATCTR_MAX ((gasneti_statctr_t)-1)


#define GASNETI_STATTIME_NOW_IFENABLED(type)                      \
  ((GASNETI_STATS_ENABLED(type) || GASNETI_TRACE_ENABLED(type)) ? \
   GASNETI_STATTIME_NOW():(gasneti_stattime_t)0)
typedef struct {
  gasneti_statctr_t count;
  gasneti_statctr_t minval;
  gasneti_statctr_t maxval;
  gasneti_statctr_t sumval;
} gasneti_stat_intval_t;

typedef struct {
  gasneti_statctr_t count;
  gasneti_stattime_t minval;
  gasneti_stattime_t maxval;
  gasneti_stattime_t sumval;
} gasneti_stat_timeval_t;

/* startup & cleanup called by GASNet */
extern void gasneti_trace_init();
extern void gasneti_trace_finish();

/* defines all the types */
#define GASNETI_ALLTYPES "GPSBLAICDNH"


/* GASNETI_ALL_STATS lists all the statistics values we gather, 
   in the format: (type,name,value_description)
*/
#define GASNETI_ALL_STATS(CNT,VAL,TIME)                   \
        VAL(G, GET, sz)                                   \
        VAL(G, GET_BULK, sz)                              \
        VAL(G, GET_NB, sz)                                \
        VAL(G, GET_NB_BULK, sz)                           \
        VAL(G, GET_NB_VAL, sz)                            \
        VAL(G, GET_NBI, sz)                               \
        VAL(G, GET_NBI_BULK, sz)                          \
        VAL(G, GET_VAL, sz)                               \
                                                          \
        VAL(G, GET_LOCAL, sz)                             \
        VAL(G, GET_BULK_LOCAL, sz)                        \
        VAL(G, GET_NB_LOCAL, sz)                          \
        VAL(G, GET_NB_BULK_LOCAL, sz)                     \
        VAL(G, GET_NB_VAL_LOCAL, sz)                      \
        VAL(G, GET_NBI_LOCAL, sz)                         \
        VAL(G, GET_NBI_BULK_LOCAL, sz)                    \
        VAL(G, GET_VAL_LOCAL, sz)                         \
                                                          \
        VAL(P, PUT, sz)                                   \
        VAL(P, PUT_BULK, sz)                              \
        VAL(P, PUT_NB, sz)                                \
        VAL(P, PUT_NB_BULK, sz)                           \
        VAL(P, PUT_NB_VAL, sz)                            \
        VAL(P, PUT_NBI, sz)                               \
        VAL(P, PUT_NBI_BULK, sz)                          \
        VAL(P, PUT_NBI_VAL, sz)                           \
        VAL(P, PUT_VAL, sz)                               \
        VAL(P, MEMSET, sz)                                \
        VAL(P, MEMSET_NB, sz)                             \
        VAL(P, MEMSET_NBI, sz)                            \
                                                          \
        VAL(P, PUT_LOCAL, sz)                             \
        VAL(P, PUT_BULK_LOCAL, sz)                        \
        VAL(P, PUT_NB_LOCAL, sz)                          \
        VAL(P, PUT_NB_BULK_LOCAL, sz)                     \
        VAL(P, PUT_NB_VAL_LOCAL, sz)                      \
        VAL(P, PUT_NBI_LOCAL, sz)                         \
        VAL(P, PUT_NBI_BULK_LOCAL, sz)                    \
        VAL(P, PUT_NBI_VAL_LOCAL, sz)                     \
        VAL(P, PUT_VAL_LOCAL, sz)                         \
        VAL(P, MEMSET_LOCAL, sz)                          \
        VAL(P, MEMSET_NB_LOCAL, sz)                       \
        VAL(P, MEMSET_NBI_LOCAL, sz)                      \
                                                          \
        VAL(G, GETV_BULK, sz)                             \
        VAL(G, GETV_NB_BULK, sz)                          \
        VAL(G, GETV_NBI_BULK, sz)                         \
        VAL(G, GETI_BULK, sz)                             \
        VAL(G, GETI_NB_BULK, sz)                          \
        VAL(G, GETI_NBI_BULK, sz)                         \
        VAL(G, GETS_BULK, sz)                             \
        VAL(G, GETS_NB_BULK, sz)                          \
        VAL(G, GETS_NBI_BULK, sz)                         \
                                                          \
        VAL(P, PUTV_BULK, sz)                             \
        VAL(P, PUTV_NB_BULK, sz)                          \
        VAL(P, PUTV_NBI_BULK, sz)                         \
        VAL(P, PUTI_BULK, sz)                             \
        VAL(P, PUTI_NB_BULK, sz)                          \
        VAL(P, PUTI_NBI_BULK, sz)                         \
        VAL(P, PUTS_BULK, sz)                             \
        VAL(P, PUTS_NB_BULK, sz)                          \
        VAL(P, PUTS_NBI_BULK, sz)                         \
                                                          \
        VAL(S, TRY_SYNCNB, success)                       \
        VAL(S, TRY_SYNCNB_ALL, success)                   \
        VAL(S, TRY_SYNCNB_SOME, success)                  \
        TIME(S, WAIT_SYNCNB, waittime)                    \
        TIME(S, WAIT_SYNCNB_ALL, waittime)                \
        TIME(S, WAIT_SYNCNB_SOME, waittime)               \
        TIME(S, WAIT_SYNCNB_VALGET, waittime)             \
        VAL(S, TRY_SYNCNBI_ALL, success)                  \
        VAL(S, TRY_SYNCNBI_GETS, success)                 \
        VAL(S, TRY_SYNCNBI_PUTS, success)                 \
        TIME(S, WAIT_SYNCNBI_ALL, waittime)               \
        TIME(S, WAIT_SYNCNBI_GETS, waittime)              \
        TIME(S, WAIT_SYNCNBI_PUTS, waittime)              \
                                                          \
        VAL(I, END_NBI_ACCESSREGION, numops)              \
                                                          \
        CNT(B, BARRIER_NOTIFY, cnt)                       \
        TIME(B, BARRIER_NOTIFYWAIT, notify-wait interval) \
        TIME(B, BARRIER_WAIT, waittime)                   \
        VAL(B, BARRIER_TRY, success)                      \
                                                          \
        TIME(L, HSL_LOCK, waittime)                       \
        TIME(L, HSL_UNLOCK, holdtime)                     \
        VAL(L, HSL_TRYLOCK, success)                       \
                                                          \
        CNT(A, AMREQUEST_SHORT, cnt)                      \
        CNT(A, AMREQUEST_MEDIUM, cnt)                     \
        CNT(A, AMREQUEST_LONG, cnt)                       \
        CNT(A, AMREQUEST_LONGASYNC, cnt)                  \
        CNT(A, AMREPLY_SHORT, cnt)                        \
        CNT(A, AMREPLY_MEDIUM, cnt)                       \
        CNT(A, AMREPLY_LONG, cnt)                         \
                                                          \
        CNT(A, AMREQUEST_SHORT_HANDLER, cnt)              \
        CNT(A, AMREQUEST_MEDIUM_HANDLER, cnt)             \
        CNT(A, AMREQUEST_LONG_HANDLER, cnt)               \
        CNT(A, AMREPLY_SHORT_HANDLER, cnt)                \
        CNT(A, AMREPLY_MEDIUM_HANDLER, cnt)               \
        CNT(A, AMREPLY_LONG_HANDLER, cnt)                 \
                                                          \
        VAL(I, GASNET_MALLOC, sz)                         \
        VAL(I, GASNET_FREE, sz)                           \
                                                          \
        CONDUIT_CORE_STATS(CNT,VAL,TIME)                  \
        CONDUIT_EXTENDED_STATS(CNT,VAL,TIME)

/* CONDUIT_CORE_STATS and CONDUIT_EXTENDED_STATS provide a way for conduits 
   to declare their own statistics (which should be given type C or I)
 */
#ifndef CONDUIT_CORE_STATS
#define CONDUIT_CORE_STATS(CNT,VAL,TIME)
#endif
#ifndef CONDUIT_EXTENDED_STATS
#define CONDUIT_EXTENDED_STATS(CNT,VAL,TIME)
#endif

/* these are legal even without STATS/TRACE */
extern int gasneti_format_memveclist_bufsz(size_t count);
extern gasneti_memveclist_stats_t gasneti_format_memveclist(char *buf, size_t count, gasnet_memvec_t const *list);
extern int gasneti_format_addrlist_bufsz(size_t count);
extern gasneti_addrlist_stats_t gasneti_format_addrlist(char *buf, size_t count, void * const *list, size_t len);

#if GASNETI_STATS_OR_TRACE
  #define BUILD_ENUM(type,name,desc) GASNETI_STAT_##name,
  typedef enum {
    GASNETI_ALL_STATS(BUILD_ENUM, BUILD_ENUM, BUILD_ENUM)
    GASNETI_STAT_COUNT
  } gasneti_statidx_t;
  typedef struct {
    const char * const type;
    const char * const name;
    const char * const desc;
  } gasneti_statinfo_t;
  extern gasneti_statinfo_t gasneti_stats[];

  extern FILE *gasneti_tracefile;
  extern FILE *gasneti_statsfile;
  extern char *gasneti_dynsprintf(const char *format,...) __attribute__((__format__ (__printf__, 1, 2)));
  extern char *gasneti_formatdata(void *p, int nbytes);
  extern const char *gasneti_format_strides(size_t count, const size_t *list);
  extern void gasneti_trace_output(const char *type, const char *msg, int traceheader);
  extern void gasneti_stats_output(const char *type, const char *msg, int traceheader);

  extern char gasneti_tracetypes[];
  extern char gasneti_statstypes[];
#endif
#if GASNET_TRACE
  #define GASNETI_TRACE_ENABLED(type) (gasneti_tracetypes[(int)*(char*)#type])
#else
  #define GASNETI_TRACE_ENABLED(type) 0
#endif
#if GASNET_STATS
  #define GASNETI_STATS_ENABLED(type) (gasneti_statstypes[(int)*(char*)#type])
#else
  #define GASNETI_STATS_ENABLED(type) 0
#endif


#if GASNET_TRACE
  #define _GASNETI_TRACE_EVENT(type, name) \
    GASNETI_TRACE_PRINTF(type, ("%s", #name))
  #define _GASNETI_TRACE_EVENT_VAL(type, name, val) \
    GASNETI_TRACE_PRINTF(type, ("%s: %s = %6i",     \
        #name, gasneti_stats[(int)GASNETI_STAT_##name].desc, (int)val))
  #define _GASNETI_TRACE_EVENT_TIME(type, name, time)        \
    GASNETI_TRACE_PRINTF(type, ("%s: %s = %6ius",            \
        #name, gasneti_stats[(int)GASNETI_STAT_##name].desc, \
        (int)GASNETI_STATTIME_TO_US(time)))
#else
  #define _GASNETI_TRACE_EVENT(type, name) 
  #define _GASNETI_TRACE_EVENT_VAL(type, name, val) 
  #define _GASNETI_TRACE_EVENT_TIME(type, name, time) 
#endif


#if GASNET_STATS
  #define DECL_CTR(type,name,desc)                   \
    extern gasneti_statctr_t gasneti_stat_ctr_##name;
  #define DECL_INTVAL(type,name,desc)                   \
    extern gasneti_stat_intval_t gasneti_stat_intval_##name;
  #define DECL_TIMEVAL(type,name,desc)                    \
    extern gasneti_stat_timeval_t gasneti_stat_timeval_##name;
  GASNETI_ALL_STATS(DECL_CTR, DECL_INTVAL, DECL_TIMEVAL)
  #undef DECL_CTR
  #undef DECL_INTVAL
  #undef DECL_TIMEVAL

  extern void gasneti_stat_count_accumulate(gasneti_statctr_t *pctr);
  extern void gasneti_stat_intval_accumulate(gasneti_stat_intval_t *pintval, gasneti_statctr_t val);
  extern void gasneti_stat_timeval_accumulate(gasneti_stat_timeval_t *pintval, gasneti_stattime_t val);
  #define _GASNETI_STAT_EVENT(type, name) do {                 \
    if (GASNETI_STATS_ENABLED(type))                           \
      gasneti_stat_count_accumulate(&gasneti_stat_ctr_##name); \
  } while (0)
  #define _GASNETI_STAT_EVENT_VAL(type, name, val) do {                                  \
    if (GASNETI_STATS_ENABLED(type))                                                     \
      gasneti_stat_intval_accumulate(&gasneti_stat_intval_##name,(gasneti_statctr_t)val);\
  } while (0)
  #define _GASNETI_STAT_EVENT_TIME(type, name, time) do {                                  \
    if (GASNETI_STATS_ENABLED(type))                                                       \
      gasneti_stat_timeval_accumulate(&gasneti_stat_timeval_##name,(gasneti_stattime_t)time);\
  } while (0)
#else
  #define _GASNETI_STAT_EVENT(type, name)
  #define _GASNETI_STAT_EVENT_VAL(type, name, val) 
  #define _GASNETI_STAT_EVENT_TIME(type, name, time) 
#endif
#define GASNETI_STAT_EVENT      _GASNETI_STAT_EVENT
#define GASNETI_STAT_EVENT_VAL  _GASNETI_STAT_EVENT_VAL
#define GASNETI_STAT_EVENT_TIME _GASNETI_STAT_EVENT_TIME

/* ------------------------------------------------------------------------------------ */

END_EXTERNC

#endif

/*  $Archive:: /Ti/GASNet/template-conduit/gasnet_core.h                  $
 *     $Date: 2003/05/04 01:33:47 $
 * $Revision: 1.1 $
 * Description: GASNet header for smp conduit core
 * Copyright 2002, Dan Bonachea <bonachea@cs.berkeley.edu>
 * Terms of use are as specified in license.txt
 */

#ifndef _IN_GASNET_H
  #error This file is not meant to be included directly- clients should include gasnet.h
#endif

#ifndef _GASNET_CORE_H
#define _GASNET_CORE_H

#include <stdlib.h>
#ifdef GASNETI_THREADS
  #ifdef LINUX
   struct timespec; /* avoid an annoying warning on Linux */
  #endif
  #include <pthread.h>
#endif

#include <gasnet_core_help.h>

BEGIN_EXTERNC

#ifdef DEBUG
  void gasnetc_checkinit();
  void gasnetc_checkattach();
  #define GASNETC_CHECKINIT()    gasnetc_checkinit()
  #define GASNETC_CHECKATTACH()  gasnetc_checkattach()
#else
  #define GASNETC_CHECKINIT()
  #define GASNETC_CHECKATTACH()
#endif

/* ------------------------------------------------------------------------------------ */
/*
  Initialization
  ==============
*/
/* gasnet_init not inlined or renamed because we use redef-name trick on  
   it to ensure proper version linkage */
extern int gasnet_init(int *argc, char ***argv);

extern int gasnetc_attach(gasnet_handlerentry_t *table, int numentries,
                          uintptr_t segsize, uintptr_t minheapoffset);
#define gasnet_attach gasnetc_attach

extern void gasnetc_exit(int exitcode) GASNET_NORETURN;
#define gasnet_exit gasnetc_exit

extern uintptr_t gasnetc_getMaxLocalSegmentSize();
extern uintptr_t gasnetc_getMaxGlobalSegmentSize();
#define gasnet_getMaxLocalSegmentSize   gasnetc_getMaxLocalSegmentSize 
#define gasnet_getMaxGlobalSegmentSize gasnetc_getMaxGlobalSegmentSize 

/* ------------------------------------------------------------------------------------ */
/*
  Job Environment Queries
  =======================
*/
extern int gasnetc_getSegmentInfo(gasnet_seginfo_t *seginfo_table, int numentries);

GASNET_INLINE_MODIFIER(gasnet_mynode)
gasnet_node_t gasnet_mynode() {
  GASNETC_CHECKINIT();
  return gasnetc_mynode;
}
 
GASNET_INLINE_MODIFIER(gasnet_nodes)
gasnet_node_t gasnet_nodes() {
  GASNETC_CHECKINIT();
  return gasnetc_nodes;
}

#define gasnet_getSegmentInfo gasnetc_getSegmentInfo

GASNET_INLINE_MODIFIER(gasnet_getenv)
char *gasnet_getenv(const char *s) {
  GASNETC_CHECKINIT();
  return gasneti_getenv(s);
}

/* ------------------------------------------------------------------------------------ */
/*
  No-interrupt sections
  =====================
*/
/* conduit may or may not need this based on whether interrupts are used for running handlers */
#if 0
  extern void gasnetc_hold_interrupts();
  extern void gasnetc_resume_interrupts();

  #define gasnet_hold_interrupts    gasnetc_hold_interrupts
  #define gasnet_resume_interrupts  gasnetc_resume_interrupts
#else
  #define gasnet_hold_interrupts()
  #define gasnet_resume_interrupts()
#endif

/* ------------------------------------------------------------------------------------ */
/*
  Handler-safe locks
  ==================
*/
typedef struct _gasnet_hsl_t {
  #ifdef GASNETI_THREADS
    pthread_mutex_t lock;
  #else
    char _dummy; /* prevent an illegal empty structure decl */
  #endif

  #if defined(STATS) || defined(TRACE)
    gasneti_stattime_t acquiretime;
  #endif

} gasnet_hsl_t;

#ifdef GASNETI_THREADS
  #define GASNETC_LOCK_MUTEX_INIT PTHREAD_MUTEX_INITIALIZER
#else 
  #define GASNETC_LOCK_MUTEX_INIT 0
#endif

#if defined(STATS) || defined(TRACE)
  #define GASNETC_LOCK_STAT_INIT ,0 
#else
  #define GASNETC_LOCK_STAT_INIT  
#endif

#define GASNET_HSL_INITIALIZER { \
  GASNETC_LOCK_MUTEX_INIT        \
  GASNETC_LOCK_STAT_INIT         \
  }

extern void gasnetc_hsl_init   (gasnet_hsl_t *hsl);
extern void gasnetc_hsl_destroy(gasnet_hsl_t *hsl);
extern void gasnetc_hsl_lock   (gasnet_hsl_t *hsl);
extern void gasnetc_hsl_unlock (gasnet_hsl_t *hsl);

#define gasnet_hsl_init    gasnetc_hsl_init
#define gasnet_hsl_destroy gasnetc_hsl_destroy
#define gasnet_hsl_lock    gasnetc_hsl_lock
#define gasnet_hsl_unlock  gasnetc_hsl_unlock
/* ------------------------------------------------------------------------------------ */
/*
  Active Message Size Limits
  ==========================
*/

#define gasnet_AMMaxArgs()          ((size_t)16)
#define gasnet_AMMaxMedium()        ((size_t)GASNETC_MAX_MEDIUM)
#define gasnet_AMMaxLongRequest()   ((size_t)GASNETC_MAX_LONG)
#define gasnet_AMMaxLongReply()     ((size_t)GASNETC_MAX_LONG)

/* ------------------------------------------------------------------------------------ */
/*
  Misc. Active Message Functions
  ==============================
*/
extern int gasnetc_AMGetMsgSource(gasnet_token_t token, gasnet_node_t *srcindex);
extern int gasnetc_AMPoll();

#define gasnet_AMPoll          gasnetc_AMPoll
#define gasnet_AMGetMsgSource  gasnetc_AMGetMsgSource

#define GASNET_BLOCKUNTIL(cond) while (!(cond)) gasnet_AMPoll()

/* ------------------------------------------------------------------------------------ */
/*
  Active Message Request/Reply Functions
  ======================================
*/

/* The rest of this header uses macros to coalesce all the possible
   AM request/reply functions into the following short list of variable-argument
   functions. This is not the only implementation option, but seems to work 
   rather cleanly.
*/

extern int gasnetc_AMRequestShortM( 
                            gasnet_node_t dest,       /* destination node */
                            gasnet_handler_t handler, /* index into destination endpoint's handler table */ 
                            int numargs, ...);

extern int gasnetc_AMRequestMediumM( 
                            gasnet_node_t dest,      /* destination node */
                            gasnet_handler_t handler, /* index into destination endpoint's handler table */ 
                            void *source_addr, size_t nbytes,   /* data payload */
                            int numargs, ...);

extern int gasnetc_AMRequestLongM( gasnet_node_t dest,        /* destination node */
                            gasnet_handler_t handler, /* index into destination endpoint's handler table */ 
                            void *source_addr, size_t nbytes,   /* data payload */
                            void *dest_addr,                    /* data destination on destination node */
                            int numargs, ...);

extern int gasnetc_AMRequestLongAsyncM( gasnet_node_t dest,        /* destination node */
                            gasnet_handler_t handler, /* index into destination endpoint's handler table */ 
                            void *source_addr, size_t nbytes,   /* data payload */
                            void *dest_addr,                    /* data destination on destination node */
                            int numargs, ...);

extern int gasnetc_AMReplyShortM( 
                            gasnet_token_t token,       /* token provided on handler entry */
                            gasnet_handler_t handler, /* index into destination endpoint's handler table */ 
                            int numargs, ...);

extern int gasnetc_AMReplyMediumM( 
                            gasnet_token_t token,       /* token provided on handler entry */
                            gasnet_handler_t handler, /* index into destination endpoint's handler table */ 
                            void *source_addr, size_t nbytes,   /* data payload */
                            int numargs, ...);

extern int gasnetc_AMReplyLongM( 
                            gasnet_token_t token,       /* token provided on handler entry */
                            gasnet_handler_t handler, /* index into destination endpoint's handler table */ 
                            void *source_addr, size_t nbytes,   /* data payload */
                            void *dest_addr,                    /* data destination on destination node */
                            int numargs, ...);


/* ------------------------------------------------------------------------------------ */
/*
  Active Message Macros
  =====================
*/
/*  yes, this is ugly, but it works... */
/* ------------------------------------------------------------------------------------ */
#define gasnet_AMRequestShort0(dest, handler) \
       gasnetc_AMRequestShortM(dest, handler, 0)
#define gasnet_AMRequestShort1(dest, handler, a0) \
       gasnetc_AMRequestShortM(dest, handler, 1, (gasnet_handlerarg_t)a0)
#define gasnet_AMRequestShort2(dest, handler, a0, a1) \
       gasnetc_AMRequestShortM(dest, handler, 2, (gasnet_handlerarg_t)a0, (gasnet_handlerarg_t)a1)
#define gasnet_AMRequestShort3(dest, handler, a0, a1, a2) \
       gasnetc_AMRequestShortM(dest, handler, 3, (gasnet_handlerarg_t)a0, (gasnet_handlerarg_t)a1, (gasnet_handlerarg_t)a2)
#define gasnet_AMRequestShort4(dest, handler, a0, a1, a2, a3) \
       gasnetc_AMRequestShortM(dest, handler, 4, (gasnet_handlerarg_t)a0, (gasnet_handlerarg_t)a1, (gasnet_handlerarg_t)a2, (gasnet_handlerarg_t)a3)

#define gasnet_AMRequestShort5(dest, handler, a0, a1, a2, a3, a4) \
       gasnetc_AMRequestShortM(dest, handler, 5, (gasnet_handlerarg_t)a0, (gasnet_handlerarg_t)a1, (gasnet_handlerarg_t)a2, (gasnet_handlerarg_t)a3, (gasnet_handlerarg_t)a4)
#define gasnet_AMRequestShort6(dest, handler, a0, a1, a2, a3, a4, a5) \
       gasnetc_AMRequestShortM(dest, handler, 6, (gasnet_handlerarg_t)a0, (gasnet_handlerarg_t)a1, (gasnet_handlerarg_t)a2, (gasnet_handlerarg_t)a3, (gasnet_handlerarg_t)a4, (gasnet_handlerarg_t)a5)
#define gasnet_AMRequestShort7(dest, handler, a0, a1, a2, a3, a4, a5, a6) \
       gasnetc_AMRequestShortM(dest, handler, 7, (gasnet_handlerarg_t)a0, (gasnet_handlerarg_t)a1, (gasnet_handlerarg_t)a2, (gasnet_handlerarg_t)a3, (gasnet_handlerarg_t)a4, (gasnet_handlerarg_t)a5, (gasnet_handlerarg_t)a6)
#define gasnet_AMRequestShort8(dest, handler, a0, a1, a2, a3, a4, a5, a6, a7) \
       gasnetc_AMRequestShortM(dest, handler, 8, (gasnet_handlerarg_t)a0, (gasnet_handlerarg_t)a1, (gasnet_handlerarg_t)a2, (gasnet_handlerarg_t)a3, (gasnet_handlerarg_t)a4, (gasnet_handlerarg_t)a5, (gasnet_handlerarg_t)a6, (gasnet_handlerarg_t)a7)

#define gasnet_AMRequestShort9 (dest, handler, a0, a1, a2, a3, a4, a5, a6, a7, a8 ) \
        gasnetc_AMRequestShortM(dest, handler,  9, (gasnet_handlerarg_t)a0, (gasnet_handlerarg_t)a1, (gasnet_handlerarg_t)a2, (gasnet_handlerarg_t)a3, (gasnet_handlerarg_t)a4, (gasnet_handlerarg_t)a5, (gasnet_handlerarg_t)a6, (gasnet_handlerarg_t)a7, (gasnet_handlerarg_t)a8)
#define gasnet_AMRequestShort10(dest, handler, a0, a1, a2, a3, a4, a5, a6, a7, a8, a9) \
        gasnetc_AMRequestShortM(dest, handler, 10, (gasnet_handlerarg_t)a0, (gasnet_handlerarg_t)a1, (gasnet_handlerarg_t)a2, (gasnet_handlerarg_t)a3, (gasnet_handlerarg_t)a4, (gasnet_handlerarg_t)a5, (gasnet_handlerarg_t)a6, (gasnet_handlerarg_t)a7, (gasnet_handlerarg_t)a8, (gasnet_handlerarg_t)a9)
#define gasnet_AMRequestShort11(dest, handler, a0, a1, a2, a3, a4, a5, a6, a7, a8, a9, a10) \
        gasnetc_AMRequestShortM(dest, handler, 11, (gasnet_handlerarg_t)a0, (gasnet_handlerarg_t)a1, (gasnet_handlerarg_t)a2, (gasnet_handlerarg_t)a3, (gasnet_handlerarg_t)a4, (gasnet_handlerarg_t)a5, (gasnet_handlerarg_t)a6, (gasnet_handlerarg_t)a7, (gasnet_handlerarg_t)a8, (gasnet_handlerarg_t)a9, (gasnet_handlerarg_t)a10)
#define gasnet_AMRequestShort12(dest, handler, a0, a1, a2, a3, a4, a5, a6, a7, a8, a9, a10, a11) \
        gasnetc_AMRequestShortM(dest, handler, 12, (gasnet_handlerarg_t)a0, (gasnet_handlerarg_t)a1, (gasnet_handlerarg_t)a2, (gasnet_handlerarg_t)a3, (gasnet_handlerarg_t)a4, (gasnet_handlerarg_t)a5, (gasnet_handlerarg_t)a6, (gasnet_handlerarg_t)a7, (gasnet_handlerarg_t)a8, (gasnet_handlerarg_t)a9, (gasnet_handlerarg_t)a10, (gasnet_handlerarg_t)a11)

#define gasnet_AMRequestShort13(dest, handler, a0, a1, a2, a3, a4, a5, a6, a7, a8, a9, a10, a11, a12) \
        gasnetc_AMRequestShortM(dest, handler, 13, (gasnet_handlerarg_t)a0, (gasnet_handlerarg_t)a1, (gasnet_handlerarg_t)a2, (gasnet_handlerarg_t)a3, (gasnet_handlerarg_t)a4, (gasnet_handlerarg_t)a5, (gasnet_handlerarg_t)a6, (gasnet_handlerarg_t)a7, (gasnet_handlerarg_t)a8, (gasnet_handlerarg_t)a9, (gasnet_handlerarg_t)a10, (gasnet_handlerarg_t)a11, (gasnet_handlerarg_t)a12)
#define gasnet_AMRequestShort14(dest, handler, a0, a1, a2, a3, a4, a5, a6, a7, a8, a9, a10, a11, a12, a13) \
        gasnetc_AMRequestShortM(dest, handler, 14, (gasnet_handlerarg_t)a0, (gasnet_handlerarg_t)a1, (gasnet_handlerarg_t)a2, (gasnet_handlerarg_t)a3, (gasnet_handlerarg_t)a4, (gasnet_handlerarg_t)a5, (gasnet_handlerarg_t)a6, (gasnet_handlerarg_t)a7, (gasnet_handlerarg_t)a8, (gasnet_handlerarg_t)a9, (gasnet_handlerarg_t)a10, (gasnet_handlerarg_t)a11, (gasnet_handlerarg_t)a12, (gasnet_handlerarg_t)a13)
#define gasnet_AMRequestShort15(dest, handler, a0, a1, a2, a3, a4, a5, a6, a7, a8, a9, a10, a11, a12, a13, a14) \
        gasnetc_AMRequestShortM(dest, handler, 15, (gasnet_handlerarg_t)a0, (gasnet_handlerarg_t)a1, (gasnet_handlerarg_t)a2, (gasnet_handlerarg_t)a3, (gasnet_handlerarg_t)a4, (gasnet_handlerarg_t)a5, (gasnet_handlerarg_t)a6, (gasnet_handlerarg_t)a7, (gasnet_handlerarg_t)a8, (gasnet_handlerarg_t)a9, (gasnet_handlerarg_t)a10, (gasnet_handlerarg_t)a11, (gasnet_handlerarg_t)a12, (gasnet_handlerarg_t)a13, (gasnet_handlerarg_t)a14)
#define gasnet_AMRequestShort16(dest, handler, a0, a1, a2, a3, a4, a5, a6, a7, a8, a9, a10, a11, a12, a13, a14, a15) \
        gasnetc_AMRequestShortM(dest, handler, 16, (gasnet_handlerarg_t)a0, (gasnet_handlerarg_t)a1, (gasnet_handlerarg_t)a2, (gasnet_handlerarg_t)a3, (gasnet_handlerarg_t)a4, (gasnet_handlerarg_t)a5, (gasnet_handlerarg_t)a6, (gasnet_handlerarg_t)a7, (gasnet_handlerarg_t)a8, (gasnet_handlerarg_t)a9, (gasnet_handlerarg_t)a10, (gasnet_handlerarg_t)a11, (gasnet_handlerarg_t)a12, (gasnet_handlerarg_t)a13, (gasnet_handlerarg_t)a14, (gasnet_handlerarg_t)a15)
/* ------------------------------------------------------------------------------------ */
#define gasnet_AMRequestMedium0(dest, handler, source_addr, nbytes) \
       gasnetc_AMRequestMediumM(dest, handler, source_addr, nbytes, 0)
#define gasnet_AMRequestMedium1(dest, handler, source_addr, nbytes, a0) \
       gasnetc_AMRequestMediumM(dest, handler, source_addr, nbytes, 1, (gasnet_handlerarg_t)a0)
#define gasnet_AMRequestMedium2(dest, handler, source_addr, nbytes, a0, a1) \
       gasnetc_AMRequestMediumM(dest, handler, source_addr, nbytes, 2, (gasnet_handlerarg_t)a0, (gasnet_handlerarg_t)a1)
#define gasnet_AMRequestMedium3(dest, handler, source_addr, nbytes, a0, a1, a2) \
       gasnetc_AMRequestMediumM(dest, handler, source_addr, nbytes, 3, (gasnet_handlerarg_t)a0, (gasnet_handlerarg_t)a1, (gasnet_handlerarg_t)a2)
#define gasnet_AMRequestMedium4(dest, handler, source_addr, nbytes, a0, a1, a2, a3) \
       gasnetc_AMRequestMediumM(dest, handler, source_addr, nbytes, 4, (gasnet_handlerarg_t)a0, (gasnet_handlerarg_t)a1, (gasnet_handlerarg_t)a2, (gasnet_handlerarg_t)a3)

#define gasnet_AMRequestMedium5(dest, handler, source_addr, nbytes, a0, a1, a2, a3, a4) \
       gasnetc_AMRequestMediumM(dest, handler, source_addr, nbytes, 5, (gasnet_handlerarg_t)a0, (gasnet_handlerarg_t)a1, (gasnet_handlerarg_t)a2, (gasnet_handlerarg_t)a3, (gasnet_handlerarg_t)a4)
#define gasnet_AMRequestMedium6(dest, handler, source_addr, nbytes, a0, a1, a2, a3, a4, a5) \
       gasnetc_AMRequestMediumM(dest, handler, source_addr, nbytes, 6, (gasnet_handlerarg_t)a0, (gasnet_handlerarg_t)a1, (gasnet_handlerarg_t)a2, (gasnet_handlerarg_t)a3, (gasnet_handlerarg_t)a4, (gasnet_handlerarg_t)a5)
#define gasnet_AMRequestMedium7(dest, handler, source_addr, nbytes, a0, a1, a2, a3, a4, a5, a6) \
       gasnetc_AMRequestMediumM(dest, handler, source_addr, nbytes, 7, (gasnet_handlerarg_t)a0, (gasnet_handlerarg_t)a1, (gasnet_handlerarg_t)a2, (gasnet_handlerarg_t)a3, (gasnet_handlerarg_t)a4, (gasnet_handlerarg_t)a5, (gasnet_handlerarg_t)a6)
#define gasnet_AMRequestMedium8(dest, handler, source_addr, nbytes, a0, a1, a2, a3, a4, a5, a6, a7) \
       gasnetc_AMRequestMediumM(dest, handler, source_addr, nbytes, 8, (gasnet_handlerarg_t)a0, (gasnet_handlerarg_t)a1, (gasnet_handlerarg_t)a2, (gasnet_handlerarg_t)a3, (gasnet_handlerarg_t)a4, (gasnet_handlerarg_t)a5, (gasnet_handlerarg_t)a6, (gasnet_handlerarg_t)a7)

#define gasnet_AMRequestMedium9 (dest, handler, source_addr, nbytes, a0, a1, a2, a3, a4, a5, a6, a7, a8 ) \
        gasnetc_AMRequestMediumM(dest, handler, source_addr, nbytes,  9, (gasnet_handlerarg_t)a0, (gasnet_handlerarg_t)a1, (gasnet_handlerarg_t)a2, (gasnet_handlerarg_t)a3, (gasnet_handlerarg_t)a4, (gasnet_handlerarg_t)a5, (gasnet_handlerarg_t)a6, (gasnet_handlerarg_t)a7, (gasnet_handlerarg_t)a8)
#define gasnet_AMRequestMedium10(dest, handler, source_addr, nbytes, a0, a1, a2, a3, a4, a5, a6, a7, a8, a9) \
        gasnetc_AMRequestMediumM(dest, handler, source_addr, nbytes, 10, (gasnet_handlerarg_t)a0, (gasnet_handlerarg_t)a1, (gasnet_handlerarg_t)a2, (gasnet_handlerarg_t)a3, (gasnet_handlerarg_t)a4, (gasnet_handlerarg_t)a5, (gasnet_handlerarg_t)a6, (gasnet_handlerarg_t)a7, (gasnet_handlerarg_t)a8, (gasnet_handlerarg_t)a9)
#define gasnet_AMRequestMedium11(dest, handler, source_addr, nbytes, a0, a1, a2, a3, a4, a5, a6, a7, a8, a9, a10) \
        gasnetc_AMRequestMediumM(dest, handler, source_addr, nbytes, 11, (gasnet_handlerarg_t)a0, (gasnet_handlerarg_t)a1, (gasnet_handlerarg_t)a2, (gasnet_handlerarg_t)a3, (gasnet_handlerarg_t)a4, (gasnet_handlerarg_t)a5, (gasnet_handlerarg_t)a6, (gasnet_handlerarg_t)a7, (gasnet_handlerarg_t)a8, (gasnet_handlerarg_t)a9, (gasnet_handlerarg_t)a10)
#define gasnet_AMRequestMedium12(dest, handler, source_addr, nbytes, a0, a1, a2, a3, a4, a5, a6, a7, a8, a9, a10, a11) \
        gasnetc_AMRequestMediumM(dest, handler, source_addr, nbytes, 12, (gasnet_handlerarg_t)a0, (gasnet_handlerarg_t)a1, (gasnet_handlerarg_t)a2, (gasnet_handlerarg_t)a3, (gasnet_handlerarg_t)a4, (gasnet_handlerarg_t)a5, (gasnet_handlerarg_t)a6, (gasnet_handlerarg_t)a7, (gasnet_handlerarg_t)a8, (gasnet_handlerarg_t)a9, (gasnet_handlerarg_t)a10, (gasnet_handlerarg_t)a11)

#define gasnet_AMRequestMedium13(dest, handler, source_addr, nbytes, a0, a1, a2, a3, a4, a5, a6, a7, a8, a9, a10, a11, a12) \
        gasnetc_AMRequestMediumM(dest, handler, source_addr, nbytes, 13, (gasnet_handlerarg_t)a0, (gasnet_handlerarg_t)a1, (gasnet_handlerarg_t)a2, (gasnet_handlerarg_t)a3, (gasnet_handlerarg_t)a4, (gasnet_handlerarg_t)a5, (gasnet_handlerarg_t)a6, (gasnet_handlerarg_t)a7, (gasnet_handlerarg_t)a8, (gasnet_handlerarg_t)a9, (gasnet_handlerarg_t)a10, (gasnet_handlerarg_t)a11, (gasnet_handlerarg_t)a12)
#define gasnet_AMRequestMedium14(dest, handler, source_addr, nbytes, a0, a1, a2, a3, a4, a5, a6, a7, a8, a9, a10, a11, a12, a13) \
        gasnetc_AMRequestMediumM(dest, handler, source_addr, nbytes, 14, (gasnet_handlerarg_t)a0, (gasnet_handlerarg_t)a1, (gasnet_handlerarg_t)a2, (gasnet_handlerarg_t)a3, (gasnet_handlerarg_t)a4, (gasnet_handlerarg_t)a5, (gasnet_handlerarg_t)a6, (gasnet_handlerarg_t)a7, (gasnet_handlerarg_t)a8, (gasnet_handlerarg_t)a9, (gasnet_handlerarg_t)a10, (gasnet_handlerarg_t)a11, (gasnet_handlerarg_t)a12, (gasnet_handlerarg_t)a13)
#define gasnet_AMRequestMedium15(dest, handler, source_addr, nbytes, a0, a1, a2, a3, a4, a5, a6, a7, a8, a9, a10, a11, a12, a13, a14) \
        gasnetc_AMRequestMediumM(dest, handler, source_addr, nbytes, 15, (gasnet_handlerarg_t)a0, (gasnet_handlerarg_t)a1, (gasnet_handlerarg_t)a2, (gasnet_handlerarg_t)a3, (gasnet_handlerarg_t)a4, (gasnet_handlerarg_t)a5, (gasnet_handlerarg_t)a6, (gasnet_handlerarg_t)a7, (gasnet_handlerarg_t)a8, (gasnet_handlerarg_t)a9, (gasnet_handlerarg_t)a10, (gasnet_handlerarg_t)a11, (gasnet_handlerarg_t)a12, (gasnet_handlerarg_t)a13, (gasnet_handlerarg_t)a14)
#define gasnet_AMRequestMedium16(dest, handler, source_addr, nbytes, a0, a1, a2, a3, a4, a5, a6, a7, a8, a9, a10, a11, a12, a13, a14, a15) \
        gasnetc_AMRequestMediumM(dest, handler, source_addr, nbytes, 16, (gasnet_handlerarg_t)a0, (gasnet_handlerarg_t)a1, (gasnet_handlerarg_t)a2, (gasnet_handlerarg_t)a3, (gasnet_handlerarg_t)a4, (gasnet_handlerarg_t)a5, (gasnet_handlerarg_t)a6, (gasnet_handlerarg_t)a7, (gasnet_handlerarg_t)a8, (gasnet_handlerarg_t)a9, (gasnet_handlerarg_t)a10, (gasnet_handlerarg_t)a11, (gasnet_handlerarg_t)a12, (gasnet_handlerarg_t)a13, (gasnet_handlerarg_t)a14, (gasnet_handlerarg_t)a15)
/* ------------------------------------------------------------------------------------ */
#define gasnet_AMRequestLong0(dest, handler, source_addr, nbytes, dest_addr) \
       gasnetc_AMRequestLongM(dest, handler, source_addr, nbytes, dest_addr, 0)
#define gasnet_AMRequestLong1(dest, handler, source_addr, nbytes, dest_addr, a0) \
       gasnetc_AMRequestLongM(dest, handler, source_addr, nbytes, dest_addr, 1, (gasnet_handlerarg_t)a0)
#define gasnet_AMRequestLong2(dest, handler, source_addr, nbytes, dest_addr, a0, a1) \
       gasnetc_AMRequestLongM(dest, handler, source_addr, nbytes, dest_addr, 2, (gasnet_handlerarg_t)a0, (gasnet_handlerarg_t)a1)
#define gasnet_AMRequestLong3(dest, handler, source_addr, nbytes, dest_addr, a0, a1, a2) \
       gasnetc_AMRequestLongM(dest, handler, source_addr, nbytes, dest_addr, 3, (gasnet_handlerarg_t)a0, (gasnet_handlerarg_t)a1, (gasnet_handlerarg_t)a2)
#define gasnet_AMRequestLong4(dest, handler, source_addr, nbytes, dest_addr, a0, a1, a2, a3) \
       gasnetc_AMRequestLongM(dest, handler, source_addr, nbytes, dest_addr, 4, (gasnet_handlerarg_t)a0, (gasnet_handlerarg_t)a1, (gasnet_handlerarg_t)a2, (gasnet_handlerarg_t)a3)

#define gasnet_AMRequestLong5(dest, handler, source_addr, nbytes, dest_addr, a0, a1, a2, a3, a4) \
       gasnetc_AMRequestLongM(dest, handler, source_addr, nbytes, dest_addr, 5, (gasnet_handlerarg_t)a0, (gasnet_handlerarg_t)a1, (gasnet_handlerarg_t)a2, (gasnet_handlerarg_t)a3, (gasnet_handlerarg_t)a4)
#define gasnet_AMRequestLong6(dest, handler, source_addr, nbytes, dest_addr, a0, a1, a2, a3, a4, a5) \
       gasnetc_AMRequestLongM(dest, handler, source_addr, nbytes, dest_addr, 6, (gasnet_handlerarg_t)a0, (gasnet_handlerarg_t)a1, (gasnet_handlerarg_t)a2, (gasnet_handlerarg_t)a3, (gasnet_handlerarg_t)a4, (gasnet_handlerarg_t)a5)
#define gasnet_AMRequestLong7(dest, handler, source_addr, nbytes, dest_addr, a0, a1, a2, a3, a4, a5, a6) \
       gasnetc_AMRequestLongM(dest, handler, source_addr, nbytes, dest_addr, 7, (gasnet_handlerarg_t)a0, (gasnet_handlerarg_t)a1, (gasnet_handlerarg_t)a2, (gasnet_handlerarg_t)a3, (gasnet_handlerarg_t)a4, (gasnet_handlerarg_t)a5, (gasnet_handlerarg_t)a6)
#define gasnet_AMRequestLong8(dest, handler, source_addr, nbytes, dest_addr, a0, a1, a2, a3, a4, a5, a6, a7) \
       gasnetc_AMRequestLongM(dest, handler, source_addr, nbytes, dest_addr, 8, (gasnet_handlerarg_t)a0, (gasnet_handlerarg_t)a1, (gasnet_handlerarg_t)a2, (gasnet_handlerarg_t)a3, (gasnet_handlerarg_t)a4, (gasnet_handlerarg_t)a5, (gasnet_handlerarg_t)a6, (gasnet_handlerarg_t)a7)

#define gasnet_AMRequestLong9 (dest, handler, source_addr, nbytes, dest_addr, a0, a1, a2, a3, a4, a5, a6, a7, a8 ) \
        gasnetc_AMRequestLongM(dest, handler, source_addr, nbytes, dest_addr,  9, (gasnet_handlerarg_t)a0, (gasnet_handlerarg_t)a1, (gasnet_handlerarg_t)a2, (gasnet_handlerarg_t)a3, (gasnet_handlerarg_t)a4, (gasnet_handlerarg_t)a5, (gasnet_handlerarg_t)a6, (gasnet_handlerarg_t)a7, (gasnet_handlerarg_t)a8)
#define gasnet_AMRequestLong10(dest, handler, source_addr, nbytes, dest_addr, a0, a1, a2, a3, a4, a5, a6, a7, a8, a9) \
        gasnetc_AMRequestLongM(dest, handler, source_addr, nbytes, dest_addr, 10, (gasnet_handlerarg_t)a0, (gasnet_handlerarg_t)a1, (gasnet_handlerarg_t)a2, (gasnet_handlerarg_t)a3, (gasnet_handlerarg_t)a4, (gasnet_handlerarg_t)a5, (gasnet_handlerarg_t)a6, (gasnet_handlerarg_t)a7, (gasnet_handlerarg_t)a8, (gasnet_handlerarg_t)a9)
#define gasnet_AMRequestLong11(dest, handler, source_addr, nbytes, dest_addr, a0, a1, a2, a3, a4, a5, a6, a7, a8, a9, a10) \
        gasnetc_AMRequestLongM(dest, handler, source_addr, nbytes, dest_addr, 11, (gasnet_handlerarg_t)a0, (gasnet_handlerarg_t)a1, (gasnet_handlerarg_t)a2, (gasnet_handlerarg_t)a3, (gasnet_handlerarg_t)a4, (gasnet_handlerarg_t)a5, (gasnet_handlerarg_t)a6, (gasnet_handlerarg_t)a7, (gasnet_handlerarg_t)a8, (gasnet_handlerarg_t)a9, (gasnet_handlerarg_t)a10)
#define gasnet_AMRequestLong12(dest, handler, source_addr, nbytes, dest_addr, a0, a1, a2, a3, a4, a5, a6, a7, a8, a9, a10, a11) \
        gasnetc_AMRequestLongM(dest, handler, source_addr, nbytes, dest_addr, 12, (gasnet_handlerarg_t)a0, (gasnet_handlerarg_t)a1, (gasnet_handlerarg_t)a2, (gasnet_handlerarg_t)a3, (gasnet_handlerarg_t)a4, (gasnet_handlerarg_t)a5, (gasnet_handlerarg_t)a6, (gasnet_handlerarg_t)a7, (gasnet_handlerarg_t)a8, (gasnet_handlerarg_t)a9, (gasnet_handlerarg_t)a10, (gasnet_handlerarg_t)a11)

#define gasnet_AMRequestLong13(dest, handler, source_addr, nbytes, dest_addr, a0, a1, a2, a3, a4, a5, a6, a7, a8, a9, a10, a11, a12) \
        gasnetc_AMRequestLongM(dest, handler, source_addr, nbytes, dest_addr, 13, (gasnet_handlerarg_t)a0, (gasnet_handlerarg_t)a1, (gasnet_handlerarg_t)a2, (gasnet_handlerarg_t)a3, (gasnet_handlerarg_t)a4, (gasnet_handlerarg_t)a5, (gasnet_handlerarg_t)a6, (gasnet_handlerarg_t)a7, (gasnet_handlerarg_t)a8, (gasnet_handlerarg_t)a9, (gasnet_handlerarg_t)a10, (gasnet_handlerarg_t)a11, (gasnet_handlerarg_t)a12)
#define gasnet_AMRequestLong14(dest, handler, source_addr, nbytes, dest_addr, a0, a1, a2, a3, a4, a5, a6, a7, a8, a9, a10, a11, a12, a13) \
        gasnetc_AMRequestLongM(dest, handler, source_addr, nbytes, dest_addr, 14, (gasnet_handlerarg_t)a0, (gasnet_handlerarg_t)a1, (gasnet_handlerarg_t)a2, (gasnet_handlerarg_t)a3, (gasnet_handlerarg_t)a4, (gasnet_handlerarg_t)a5, (gasnet_handlerarg_t)a6, (gasnet_handlerarg_t)a7, (gasnet_handlerarg_t)a8, (gasnet_handlerarg_t)a9, (gasnet_handlerarg_t)a10, (gasnet_handlerarg_t)a11, (gasnet_handlerarg_t)a12, (gasnet_handlerarg_t)a13)
#define gasnet_AMRequestLong15(dest, handler, source_addr, nbytes, dest_addr, a0, a1, a2, a3, a4, a5, a6, a7, a8, a9, a10, a11, a12, a13, a14) \
        gasnetc_AMRequestLongM(dest, handler, source_addr, nbytes, dest_addr, 15, (gasnet_handlerarg_t)a0, (gasnet_handlerarg_t)a1, (gasnet_handlerarg_t)a2, (gasnet_handlerarg_t)a3, (gasnet_handlerarg_t)a4, (gasnet_handlerarg_t)a5, (gasnet_handlerarg_t)a6, (gasnet_handlerarg_t)a7, (gasnet_handlerarg_t)a8, (gasnet_handlerarg_t)a9, (gasnet_handlerarg_t)a10, (gasnet_handlerarg_t)a11, (gasnet_handlerarg_t)a12, (gasnet_handlerarg_t)a13, (gasnet_handlerarg_t)a14)
#define gasnet_AMRequestLong16(dest, handler, source_addr, nbytes, dest_addr, a0, a1, a2, a3, a4, a5, a6, a7, a8, a9, a10, a11, a12, a13, a14, a15) \
        gasnetc_AMRequestLongM(dest, handler, source_addr, nbytes, dest_addr, 16, (gasnet_handlerarg_t)a0, (gasnet_handlerarg_t)a1, (gasnet_handlerarg_t)a2, (gasnet_handlerarg_t)a3, (gasnet_handlerarg_t)a4, (gasnet_handlerarg_t)a5, (gasnet_handlerarg_t)a6, (gasnet_handlerarg_t)a7, (gasnet_handlerarg_t)a8, (gasnet_handlerarg_t)a9, (gasnet_handlerarg_t)a10, (gasnet_handlerarg_t)a11, (gasnet_handlerarg_t)a12, (gasnet_handlerarg_t)a13, (gasnet_handlerarg_t)a14, (gasnet_handlerarg_t)a15)
/* ------------------------------------------------------------------------------------ */
#define gasnet_AMRequestLongAsync0(dest, handler, source_addr, nbytes, dest_addr) \
       gasnetc_AMRequestLongAsyncM(dest, handler, source_addr, nbytes, dest_addr, 0)
#define gasnet_AMRequestLongAsync1(dest, handler, source_addr, nbytes, dest_addr, a0) \
       gasnetc_AMRequestLongAsyncM(dest, handler, source_addr, nbytes, dest_addr, 1, (gasnet_handlerarg_t)a0)
#define gasnet_AMRequestLongAsync2(dest, handler, source_addr, nbytes, dest_addr, a0, a1) \
       gasnetc_AMRequestLongAsyncM(dest, handler, source_addr, nbytes, dest_addr, 2, (gasnet_handlerarg_t)a0, (gasnet_handlerarg_t)a1)
#define gasnet_AMRequestLongAsync3(dest, handler, source_addr, nbytes, dest_addr, a0, a1, a2) \
       gasnetc_AMRequestLongAsyncM(dest, handler, source_addr, nbytes, dest_addr, 3, (gasnet_handlerarg_t)a0, (gasnet_handlerarg_t)a1, (gasnet_handlerarg_t)a2)
#define gasnet_AMRequestLongAsync4(dest, handler, source_addr, nbytes, dest_addr, a0, a1, a2, a3) \
       gasnetc_AMRequestLongAsyncM(dest, handler, source_addr, nbytes, dest_addr, 4, (gasnet_handlerarg_t)a0, (gasnet_handlerarg_t)a1, (gasnet_handlerarg_t)a2, (gasnet_handlerarg_t)a3)

#define gasnet_AMRequestLongAsync5(dest, handler, source_addr, nbytes, dest_addr, a0, a1, a2, a3, a4) \
       gasnetc_AMRequestLongAsyncM(dest, handler, source_addr, nbytes, dest_addr, 5, (gasnet_handlerarg_t)a0, (gasnet_handlerarg_t)a1, (gasnet_handlerarg_t)a2, (gasnet_handlerarg_t)a3, (gasnet_handlerarg_t)a4)
#define gasnet_AMRequestLongAsync6(dest, handler, source_addr, nbytes, dest_addr, a0, a1, a2, a3, a4, a5) \
       gasnetc_AMRequestLongAsyncM(dest, handler, source_addr, nbytes, dest_addr, 6, (gasnet_handlerarg_t)a0, (gasnet_handlerarg_t)a1, (gasnet_handlerarg_t)a2, (gasnet_handlerarg_t)a3, (gasnet_handlerarg_t)a4, (gasnet_handlerarg_t)a5)
#define gasnet_AMRequestLongAsync7(dest, handler, source_addr, nbytes, dest_addr, a0, a1, a2, a3, a4, a5, a6) \
       gasnetc_AMRequestLongAsyncM(dest, handler, source_addr, nbytes, dest_addr, 7, (gasnet_handlerarg_t)a0, (gasnet_handlerarg_t)a1, (gasnet_handlerarg_t)a2, (gasnet_handlerarg_t)a3, (gasnet_handlerarg_t)a4, (gasnet_handlerarg_t)a5, (gasnet_handlerarg_t)a6)
#define gasnet_AMRequestLongAsync8(dest, handler, source_addr, nbytes, dest_addr, a0, a1, a2, a3, a4, a5, a6, a7) \
       gasnetc_AMRequestLongAsyncM(dest, handler, source_addr, nbytes, dest_addr, 8, (gasnet_handlerarg_t)a0, (gasnet_handlerarg_t)a1, (gasnet_handlerarg_t)a2, (gasnet_handlerarg_t)a3, (gasnet_handlerarg_t)a4, (gasnet_handlerarg_t)a5, (gasnet_handlerarg_t)a6, (gasnet_handlerarg_t)a7)

#define gasnet_AMRequestLongAsync9 (dest, handler, source_addr, nbytes, dest_addr, a0, a1, a2, a3, a4, a5, a6, a7, a8 ) \
        gasnetc_AMRequestLongAsyncM(dest, handler, source_addr, nbytes, dest_addr,  9, (gasnet_handlerarg_t)a0, (gasnet_handlerarg_t)a1, (gasnet_handlerarg_t)a2, (gasnet_handlerarg_t)a3, (gasnet_handlerarg_t)a4, (gasnet_handlerarg_t)a5, (gasnet_handlerarg_t)a6, (gasnet_handlerarg_t)a7, (gasnet_handlerarg_t)a8)
#define gasnet_AMRequestLongAsync10(dest, handler, source_addr, nbytes, dest_addr, a0, a1, a2, a3, a4, a5, a6, a7, a8, a9) \
        gasnetc_AMRequestLongAsyncM(dest, handler, source_addr, nbytes, dest_addr, 10, (gasnet_handlerarg_t)a0, (gasnet_handlerarg_t)a1, (gasnet_handlerarg_t)a2, (gasnet_handlerarg_t)a3, (gasnet_handlerarg_t)a4, (gasnet_handlerarg_t)a5, (gasnet_handlerarg_t)a6, (gasnet_handlerarg_t)a7, (gasnet_handlerarg_t)a8, (gasnet_handlerarg_t)a9)
#define gasnet_AMRequestLongAsync11(dest, handler, source_addr, nbytes, dest_addr, a0, a1, a2, a3, a4, a5, a6, a7, a8, a9, a10) \
        gasnetc_AMRequestLongAsyncM(dest, handler, source_addr, nbytes, dest_addr, 11, (gasnet_handlerarg_t)a0, (gasnet_handlerarg_t)a1, (gasnet_handlerarg_t)a2, (gasnet_handlerarg_t)a3, (gasnet_handlerarg_t)a4, (gasnet_handlerarg_t)a5, (gasnet_handlerarg_t)a6, (gasnet_handlerarg_t)a7, (gasnet_handlerarg_t)a8, (gasnet_handlerarg_t)a9, (gasnet_handlerarg_t)a10)
#define gasnet_AMRequestLongAsync12(dest, handler, source_addr, nbytes, dest_addr, a0, a1, a2, a3, a4, a5, a6, a7, a8, a9, a10, a11) \
        gasnetc_AMRequestLongAsyncM(dest, handler, source_addr, nbytes, dest_addr, 12, (gasnet_handlerarg_t)a0, (gasnet_handlerarg_t)a1, (gasnet_handlerarg_t)a2, (gasnet_handlerarg_t)a3, (gasnet_handlerarg_t)a4, (gasnet_handlerarg_t)a5, (gasnet_handlerarg_t)a6, (gasnet_handlerarg_t)a7, (gasnet_handlerarg_t)a8, (gasnet_handlerarg_t)a9, (gasnet_handlerarg_t)a10, (gasnet_handlerarg_t)a11)

#define gasnet_AMRequestLongAsync13(dest, handler, source_addr, nbytes, dest_addr, a0, a1, a2, a3, a4, a5, a6, a7, a8, a9, a10, a11, a12) \
        gasnetc_AMRequestLongAsyncM(dest, handler, source_addr, nbytes, dest_addr, 13, (gasnet_handlerarg_t)a0, (gasnet_handlerarg_t)a1, (gasnet_handlerarg_t)a2, (gasnet_handlerarg_t)a3, (gasnet_handlerarg_t)a4, (gasnet_handlerarg_t)a5, (gasnet_handlerarg_t)a6, (gasnet_handlerarg_t)a7, (gasnet_handlerarg_t)a8, (gasnet_handlerarg_t)a9, (gasnet_handlerarg_t)a10, (gasnet_handlerarg_t)a11, (gasnet_handlerarg_t)a12)
#define gasnet_AMRequestLongAsync14(dest, handler, source_addr, nbytes, dest_addr, a0, a1, a2, a3, a4, a5, a6, a7, a8, a9, a10, a11, a12, a13) \
        gasnetc_AMRequestLongAsyncM(dest, handler, source_addr, nbytes, dest_addr, 14, (gasnet_handlerarg_t)a0, (gasnet_handlerarg_t)a1, (gasnet_handlerarg_t)a2, (gasnet_handlerarg_t)a3, (gasnet_handlerarg_t)a4, (gasnet_handlerarg_t)a5, (gasnet_handlerarg_t)a6, (gasnet_handlerarg_t)a7, (gasnet_handlerarg_t)a8, (gasnet_handlerarg_t)a9, (gasnet_handlerarg_t)a10, (gasnet_handlerarg_t)a11, (gasnet_handlerarg_t)a12, (gasnet_handlerarg_t)a13)
#define gasnet_AMRequestLongAsync15(dest, handler, source_addr, nbytes, dest_addr, a0, a1, a2, a3, a4, a5, a6, a7, a8, a9, a10, a11, a12, a13, a14) \
        gasnetc_AMRequestLongAsyncM(dest, handler, source_addr, nbytes, dest_addr, 15, (gasnet_handlerarg_t)a0, (gasnet_handlerarg_t)a1, (gasnet_handlerarg_t)a2, (gasnet_handlerarg_t)a3, (gasnet_handlerarg_t)a4, (gasnet_handlerarg_t)a5, (gasnet_handlerarg_t)a6, (gasnet_handlerarg_t)a7, (gasnet_handlerarg_t)a8, (gasnet_handlerarg_t)a9, (gasnet_handlerarg_t)a10, (gasnet_handlerarg_t)a11, (gasnet_handlerarg_t)a12, (gasnet_handlerarg_t)a13, (gasnet_handlerarg_t)a14)
#define gasnet_AMRequestLongAsync16(dest, handler, source_addr, nbytes, dest_addr, a0, a1, a2, a3, a4, a5, a6, a7, a8, a9, a10, a11, a12, a13, a14, a15) \
        gasnetc_AMRequestLongAsyncM(dest, handler, source_addr, nbytes, dest_addr, 16, (gasnet_handlerarg_t)a0, (gasnet_handlerarg_t)a1, (gasnet_handlerarg_t)a2, (gasnet_handlerarg_t)a3, (gasnet_handlerarg_t)a4, (gasnet_handlerarg_t)a5, (gasnet_handlerarg_t)a6, (gasnet_handlerarg_t)a7, (gasnet_handlerarg_t)a8, (gasnet_handlerarg_t)a9, (gasnet_handlerarg_t)a10, (gasnet_handlerarg_t)a11, (gasnet_handlerarg_t)a12, (gasnet_handlerarg_t)a13, (gasnet_handlerarg_t)a14, (gasnet_handlerarg_t)a15)

/* ------------------------------------------------------------------------------------ */

#define gasnet_AMReplyShort0(token, handler) \
       gasnetc_AMReplyShortM(token, handler, 0)
#define gasnet_AMReplyShort1(token, handler, a0) \
       gasnetc_AMReplyShortM(token, handler, 1, (gasnet_handlerarg_t)a0)
#define gasnet_AMReplyShort2(token, handler, a0, a1) \
       gasnetc_AMReplyShortM(token, handler, 2, (gasnet_handlerarg_t)a0, (gasnet_handlerarg_t)a1)
#define gasnet_AMReplyShort3(token, handler, a0, a1, a2) \
       gasnetc_AMReplyShortM(token, handler, 3, (gasnet_handlerarg_t)a0, (gasnet_handlerarg_t)a1, (gasnet_handlerarg_t)a2)
#define gasnet_AMReplyShort4(token, handler, a0, a1, a2, a3) \
       gasnetc_AMReplyShortM(token, handler, 4, (gasnet_handlerarg_t)a0, (gasnet_handlerarg_t)a1, (gasnet_handlerarg_t)a2, (gasnet_handlerarg_t)a3)

#define gasnet_AMReplyShort5(token, handler, a0, a1, a2, a3, a4) \
       gasnetc_AMReplyShortM(token, handler, 5, (gasnet_handlerarg_t)a0, (gasnet_handlerarg_t)a1, (gasnet_handlerarg_t)a2, (gasnet_handlerarg_t)a3, (gasnet_handlerarg_t)a4)
#define gasnet_AMReplyShort6(token, handler, a0, a1, a2, a3, a4, a5) \
       gasnetc_AMReplyShortM(token, handler, 6, (gasnet_handlerarg_t)a0, (gasnet_handlerarg_t)a1, (gasnet_handlerarg_t)a2, (gasnet_handlerarg_t)a3, (gasnet_handlerarg_t)a4, (gasnet_handlerarg_t)a5)
#define gasnet_AMReplyShort7(token, handler, a0, a1, a2, a3, a4, a5, a6) \
       gasnetc_AMReplyShortM(token, handler, 7, (gasnet_handlerarg_t)a0, (gasnet_handlerarg_t)a1, (gasnet_handlerarg_t)a2, (gasnet_handlerarg_t)a3, (gasnet_handlerarg_t)a4, (gasnet_handlerarg_t)a5, (gasnet_handlerarg_t)a6)
#define gasnet_AMReplyShort8(token, handler, a0, a1, a2, a3, a4, a5, a6, a7) \
       gasnetc_AMReplyShortM(token, handler, 8, (gasnet_handlerarg_t)a0, (gasnet_handlerarg_t)a1, (gasnet_handlerarg_t)a2, (gasnet_handlerarg_t)a3, (gasnet_handlerarg_t)a4, (gasnet_handlerarg_t)a5, (gasnet_handlerarg_t)a6, (gasnet_handlerarg_t)a7)

#define gasnet_AMReplyShort9 (token, handler, a0, a1, a2, a3, a4, a5, a6, a7, a8 ) \
        gasnetc_AMReplyShortM(token, handler,  9, (gasnet_handlerarg_t)a0, (gasnet_handlerarg_t)a1, (gasnet_handlerarg_t)a2, (gasnet_handlerarg_t)a3, (gasnet_handlerarg_t)a4, (gasnet_handlerarg_t)a5, (gasnet_handlerarg_t)a6, (gasnet_handlerarg_t)a7, (gasnet_handlerarg_t)a8)
#define gasnet_AMReplyShort10(token, handler, a0, a1, a2, a3, a4, a5, a6, a7, a8, a9) \
        gasnetc_AMReplyShortM(token, handler, 10, (gasnet_handlerarg_t)a0, (gasnet_handlerarg_t)a1, (gasnet_handlerarg_t)a2, (gasnet_handlerarg_t)a3, (gasnet_handlerarg_t)a4, (gasnet_handlerarg_t)a5, (gasnet_handlerarg_t)a6, (gasnet_handlerarg_t)a7, (gasnet_handlerarg_t)a8, (gasnet_handlerarg_t)a9)
#define gasnet_AMReplyShort11(token, handler, a0, a1, a2, a3, a4, a5, a6, a7, a8, a9, a10) \
        gasnetc_AMReplyShortM(token, handler, 11, (gasnet_handlerarg_t)a0, (gasnet_handlerarg_t)a1, (gasnet_handlerarg_t)a2, (gasnet_handlerarg_t)a3, (gasnet_handlerarg_t)a4, (gasnet_handlerarg_t)a5, (gasnet_handlerarg_t)a6, (gasnet_handlerarg_t)a7, (gasnet_handlerarg_t)a8, (gasnet_handlerarg_t)a9, (gasnet_handlerarg_t)a10)
#define gasnet_AMReplyShort12(token, handler, a0, a1, a2, a3, a4, a5, a6, a7, a8, a9, a10, a11) \
        gasnetc_AMReplyShortM(token, handler, 12, (gasnet_handlerarg_t)a0, (gasnet_handlerarg_t)a1, (gasnet_handlerarg_t)a2, (gasnet_handlerarg_t)a3, (gasnet_handlerarg_t)a4, (gasnet_handlerarg_t)a5, (gasnet_handlerarg_t)a6, (gasnet_handlerarg_t)a7, (gasnet_handlerarg_t)a8, (gasnet_handlerarg_t)a9, (gasnet_handlerarg_t)a10, (gasnet_handlerarg_t)a11)

#define gasnet_AMReplyShort13(token, handler, a0, a1, a2, a3, a4, a5, a6, a7, a8, a9, a10, a11, a12) \
        gasnetc_AMReplyShortM(token, handler, 13, (gasnet_handlerarg_t)a0, (gasnet_handlerarg_t)a1, (gasnet_handlerarg_t)a2, (gasnet_handlerarg_t)a3, (gasnet_handlerarg_t)a4, (gasnet_handlerarg_t)a5, (gasnet_handlerarg_t)a6, (gasnet_handlerarg_t)a7, (gasnet_handlerarg_t)a8, (gasnet_handlerarg_t)a9, (gasnet_handlerarg_t)a10, (gasnet_handlerarg_t)a11, (gasnet_handlerarg_t)a12)
#define gasnet_AMReplyShort14(token, handler, a0, a1, a2, a3, a4, a5, a6, a7, a8, a9, a10, a11, a12, a13) \
        gasnetc_AMReplyShortM(token, handler, 14, (gasnet_handlerarg_t)a0, (gasnet_handlerarg_t)a1, (gasnet_handlerarg_t)a2, (gasnet_handlerarg_t)a3, (gasnet_handlerarg_t)a4, (gasnet_handlerarg_t)a5, (gasnet_handlerarg_t)a6, (gasnet_handlerarg_t)a7, (gasnet_handlerarg_t)a8, (gasnet_handlerarg_t)a9, (gasnet_handlerarg_t)a10, (gasnet_handlerarg_t)a11, (gasnet_handlerarg_t)a12, (gasnet_handlerarg_t)a13)
#define gasnet_AMReplyShort15(token, handler, a0, a1, a2, a3, a4, a5, a6, a7, a8, a9, a10, a11, a12, a13, a14) \
        gasnetc_AMReplyShortM(token, handler, 15, (gasnet_handlerarg_t)a0, (gasnet_handlerarg_t)a1, (gasnet_handlerarg_t)a2, (gasnet_handlerarg_t)a3, (gasnet_handlerarg_t)a4, (gasnet_handlerarg_t)a5, (gasnet_handlerarg_t)a6, (gasnet_handlerarg_t)a7, (gasnet_handlerarg_t)a8, (gasnet_handlerarg_t)a9, (gasnet_handlerarg_t)a10, (gasnet_handlerarg_t)a11, (gasnet_handlerarg_t)a12, (gasnet_handlerarg_t)a13, (gasnet_handlerarg_t)a14)
#define gasnet_AMReplyShort16(token, handler, a0, a1, a2, a3, a4, a5, a6, a7, a8, a9, a10, a11, a12, a13, a14, a15) \
        gasnetc_AMReplyShortM(token, handler, 16, (gasnet_handlerarg_t)a0, (gasnet_handlerarg_t)a1, (gasnet_handlerarg_t)a2, (gasnet_handlerarg_t)a3, (gasnet_handlerarg_t)a4, (gasnet_handlerarg_t)a5, (gasnet_handlerarg_t)a6, (gasnet_handlerarg_t)a7, (gasnet_handlerarg_t)a8, (gasnet_handlerarg_t)a9, (gasnet_handlerarg_t)a10, (gasnet_handlerarg_t)a11, (gasnet_handlerarg_t)a12, (gasnet_handlerarg_t)a13, (gasnet_handlerarg_t)a14, (gasnet_handlerarg_t)a15)
/* ------------------------------------------------------------------------------------ */
#define gasnet_AMReplyMedium0(token, handler, source_addr, nbytes) \
       gasnetc_AMReplyMediumM(token, handler, source_addr, nbytes, 0)
#define gasnet_AMReplyMedium1(token, handler, source_addr, nbytes, a0) \
       gasnetc_AMReplyMediumM(token, handler, source_addr, nbytes, 1, (gasnet_handlerarg_t)a0)
#define gasnet_AMReplyMedium2(token, handler, source_addr, nbytes, a0, a1) \
       gasnetc_AMReplyMediumM(token, handler, source_addr, nbytes, 2, (gasnet_handlerarg_t)a0, (gasnet_handlerarg_t)a1)
#define gasnet_AMReplyMedium3(token, handler, source_addr, nbytes, a0, a1, a2) \
       gasnetc_AMReplyMediumM(token, handler, source_addr, nbytes, 3, (gasnet_handlerarg_t)a0, (gasnet_handlerarg_t)a1, (gasnet_handlerarg_t)a2)
#define gasnet_AMReplyMedium4(token, handler, source_addr, nbytes, a0, a1, a2, a3) \
       gasnetc_AMReplyMediumM(token, handler, source_addr, nbytes, 4, (gasnet_handlerarg_t)a0, (gasnet_handlerarg_t)a1, (gasnet_handlerarg_t)a2, (gasnet_handlerarg_t)a3)

#define gasnet_AMReplyMedium5(token, handler, source_addr, nbytes, a0, a1, a2, a3, a4) \
       gasnetc_AMReplyMediumM(token, handler, source_addr, nbytes, 5, (gasnet_handlerarg_t)a0, (gasnet_handlerarg_t)a1, (gasnet_handlerarg_t)a2, (gasnet_handlerarg_t)a3, (gasnet_handlerarg_t)a4)
#define gasnet_AMReplyMedium6(token, handler, source_addr, nbytes, a0, a1, a2, a3, a4, a5) \
       gasnetc_AMReplyMediumM(token, handler, source_addr, nbytes, 6, (gasnet_handlerarg_t)a0, (gasnet_handlerarg_t)a1, (gasnet_handlerarg_t)a2, (gasnet_handlerarg_t)a3, (gasnet_handlerarg_t)a4, (gasnet_handlerarg_t)a5)
#define gasnet_AMReplyMedium7(token, handler, source_addr, nbytes, a0, a1, a2, a3, a4, a5, a6) \
       gasnetc_AMReplyMediumM(token, handler, source_addr, nbytes, 7, (gasnet_handlerarg_t)a0, (gasnet_handlerarg_t)a1, (gasnet_handlerarg_t)a2, (gasnet_handlerarg_t)a3, (gasnet_handlerarg_t)a4, (gasnet_handlerarg_t)a5, (gasnet_handlerarg_t)a6)
#define gasnet_AMReplyMedium8(token, handler, source_addr, nbytes, a0, a1, a2, a3, a4, a5, a6, a7) \
       gasnetc_AMReplyMediumM(token, handler, source_addr, nbytes, 8, (gasnet_handlerarg_t)a0, (gasnet_handlerarg_t)a1, (gasnet_handlerarg_t)a2, (gasnet_handlerarg_t)a3, (gasnet_handlerarg_t)a4, (gasnet_handlerarg_t)a5, (gasnet_handlerarg_t)a6, (gasnet_handlerarg_t)a7)

#define gasnet_AMReplyMedium9 (token, handler, source_addr, nbytes, a0, a1, a2, a3, a4, a5, a6, a7, a8 ) \
        gasnetc_AMReplyMediumM(token, handler, source_addr, nbytes,  9, (gasnet_handlerarg_t)a0, (gasnet_handlerarg_t)a1, (gasnet_handlerarg_t)a2, (gasnet_handlerarg_t)a3, (gasnet_handlerarg_t)a4, (gasnet_handlerarg_t)a5, (gasnet_handlerarg_t)a6, (gasnet_handlerarg_t)a7, (gasnet_handlerarg_t)a8)
#define gasnet_AMReplyMedium10(token, handler, source_addr, nbytes, a0, a1, a2, a3, a4, a5, a6, a7, a8, a9) \
        gasnetc_AMReplyMediumM(token, handler, source_addr, nbytes, 10, (gasnet_handlerarg_t)a0, (gasnet_handlerarg_t)a1, (gasnet_handlerarg_t)a2, (gasnet_handlerarg_t)a3, (gasnet_handlerarg_t)a4, (gasnet_handlerarg_t)a5, (gasnet_handlerarg_t)a6, (gasnet_handlerarg_t)a7, (gasnet_handlerarg_t)a8, (gasnet_handlerarg_t)a9)
#define gasnet_AMReplyMedium11(token, handler, source_addr, nbytes, a0, a1, a2, a3, a4, a5, a6, a7, a8, a9, a10) \
        gasnetc_AMReplyMediumM(token, handler, source_addr, nbytes, 11, (gasnet_handlerarg_t)a0, (gasnet_handlerarg_t)a1, (gasnet_handlerarg_t)a2, (gasnet_handlerarg_t)a3, (gasnet_handlerarg_t)a4, (gasnet_handlerarg_t)a5, (gasnet_handlerarg_t)a6, (gasnet_handlerarg_t)a7, (gasnet_handlerarg_t)a8, (gasnet_handlerarg_t)a9, (gasnet_handlerarg_t)a10)
#define gasnet_AMReplyMedium12(token, handler, source_addr, nbytes, a0, a1, a2, a3, a4, a5, a6, a7, a8, a9, a10, a11) \
        gasnetc_AMReplyMediumM(token, handler, source_addr, nbytes, 12, (gasnet_handlerarg_t)a0, (gasnet_handlerarg_t)a1, (gasnet_handlerarg_t)a2, (gasnet_handlerarg_t)a3, (gasnet_handlerarg_t)a4, (gasnet_handlerarg_t)a5, (gasnet_handlerarg_t)a6, (gasnet_handlerarg_t)a7, (gasnet_handlerarg_t)a8, (gasnet_handlerarg_t)a9, (gasnet_handlerarg_t)a10, (gasnet_handlerarg_t)a11)

#define gasnet_AMReplyMedium13(token, handler, source_addr, nbytes, a0, a1, a2, a3, a4, a5, a6, a7, a8, a9, a10, a11, a12) \
        gasnetc_AMReplyMediumM(token, handler, source_addr, nbytes, 13, (gasnet_handlerarg_t)a0, (gasnet_handlerarg_t)a1, (gasnet_handlerarg_t)a2, (gasnet_handlerarg_t)a3, (gasnet_handlerarg_t)a4, (gasnet_handlerarg_t)a5, (gasnet_handlerarg_t)a6, (gasnet_handlerarg_t)a7, (gasnet_handlerarg_t)a8, (gasnet_handlerarg_t)a9, (gasnet_handlerarg_t)a10, (gasnet_handlerarg_t)a11, (gasnet_handlerarg_t)a12)
#define gasnet_AMReplyMedium14(token, handler, source_addr, nbytes, a0, a1, a2, a3, a4, a5, a6, a7, a8, a9, a10, a11, a12, a13) \
        gasnetc_AMReplyMediumM(token, handler, source_addr, nbytes, 14, (gasnet_handlerarg_t)a0, (gasnet_handlerarg_t)a1, (gasnet_handlerarg_t)a2, (gasnet_handlerarg_t)a3, (gasnet_handlerarg_t)a4, (gasnet_handlerarg_t)a5, (gasnet_handlerarg_t)a6, (gasnet_handlerarg_t)a7, (gasnet_handlerarg_t)a8, (gasnet_handlerarg_t)a9, (gasnet_handlerarg_t)a10, (gasnet_handlerarg_t)a11, (gasnet_handlerarg_t)a12, (gasnet_handlerarg_t)a13)
#define gasnet_AMReplyMedium15(token, handler, source_addr, nbytes, a0, a1, a2, a3, a4, a5, a6, a7, a8, a9, a10, a11, a12, a13, a14) \
        gasnetc_AMReplyMediumM(token, handler, source_addr, nbytes, 15, (gasnet_handlerarg_t)a0, (gasnet_handlerarg_t)a1, (gasnet_handlerarg_t)a2, (gasnet_handlerarg_t)a3, (gasnet_handlerarg_t)a4, (gasnet_handlerarg_t)a5, (gasnet_handlerarg_t)a6, (gasnet_handlerarg_t)a7, (gasnet_handlerarg_t)a8, (gasnet_handlerarg_t)a9, (gasnet_handlerarg_t)a10, (gasnet_handlerarg_t)a11, (gasnet_handlerarg_t)a12, (gasnet_handlerarg_t)a13, (gasnet_handlerarg_t)a14)
#define gasnet_AMReplyMedium16(token, handler, source_addr, nbytes, a0, a1, a2, a3, a4, a5, a6, a7, a8, a9, a10, a11, a12, a13, a14, a15) \
        gasnetc_AMReplyMediumM(token, handler, source_addr, nbytes, 16, (gasnet_handlerarg_t)a0, (gasnet_handlerarg_t)a1, (gasnet_handlerarg_t)a2, (gasnet_handlerarg_t)a3, (gasnet_handlerarg_t)a4, (gasnet_handlerarg_t)a5, (gasnet_handlerarg_t)a6, (gasnet_handlerarg_t)a7, (gasnet_handlerarg_t)a8, (gasnet_handlerarg_t)a9, (gasnet_handlerarg_t)a10, (gasnet_handlerarg_t)a11, (gasnet_handlerarg_t)a12, (gasnet_handlerarg_t)a13, (gasnet_handlerarg_t)a14, (gasnet_handlerarg_t)a15)
/* ------------------------------------------------------------------------------------ */
#define gasnet_AMReplyLong0(token, handler, source_addr, nbytes, token_addr) \
       gasnetc_AMReplyLongM(token, handler, source_addr, nbytes, token_addr, 0)
#define gasnet_AMReplyLong1(token, handler, source_addr, nbytes, token_addr, a0) \
       gasnetc_AMReplyLongM(token, handler, source_addr, nbytes, token_addr, 1, (gasnet_handlerarg_t)a0)
#define gasnet_AMReplyLong2(token, handler, source_addr, nbytes, token_addr, a0, a1) \
       gasnetc_AMReplyLongM(token, handler, source_addr, nbytes, token_addr, 2, (gasnet_handlerarg_t)a0, (gasnet_handlerarg_t)a1)
#define gasnet_AMReplyLong3(token, handler, source_addr, nbytes, token_addr, a0, a1, a2) \
       gasnetc_AMReplyLongM(token, handler, source_addr, nbytes, token_addr, 3, (gasnet_handlerarg_t)a0, (gasnet_handlerarg_t)a1, (gasnet_handlerarg_t)a2)
#define gasnet_AMReplyLong4(token, handler, source_addr, nbytes, token_addr, a0, a1, a2, a3) \
       gasnetc_AMReplyLongM(token, handler, source_addr, nbytes, token_addr, 4, (gasnet_handlerarg_t)a0, (gasnet_handlerarg_t)a1, (gasnet_handlerarg_t)a2, (gasnet_handlerarg_t)a3)

#define gasnet_AMReplyLong5(token, handler, source_addr, nbytes, token_addr, a0, a1, a2, a3, a4) \
       gasnetc_AMReplyLongM(token, handler, source_addr, nbytes, token_addr, 5, (gasnet_handlerarg_t)a0, (gasnet_handlerarg_t)a1, (gasnet_handlerarg_t)a2, (gasnet_handlerarg_t)a3, (gasnet_handlerarg_t)a4)
#define gasnet_AMReplyLong6(token, handler, source_addr, nbytes, token_addr, a0, a1, a2, a3, a4, a5) \
       gasnetc_AMReplyLongM(token, handler, source_addr, nbytes, token_addr, 6, (gasnet_handlerarg_t)a0, (gasnet_handlerarg_t)a1, (gasnet_handlerarg_t)a2, (gasnet_handlerarg_t)a3, (gasnet_handlerarg_t)a4, (gasnet_handlerarg_t)a5)
#define gasnet_AMReplyLong7(token, handler, source_addr, nbytes, token_addr, a0, a1, a2, a3, a4, a5, a6) \
       gasnetc_AMReplyLongM(token, handler, source_addr, nbytes, token_addr, 7, (gasnet_handlerarg_t)a0, (gasnet_handlerarg_t)a1, (gasnet_handlerarg_t)a2, (gasnet_handlerarg_t)a3, (gasnet_handlerarg_t)a4, (gasnet_handlerarg_t)a5, (gasnet_handlerarg_t)a6)
#define gasnet_AMReplyLong8(token, handler, source_addr, nbytes, token_addr, a0, a1, a2, a3, a4, a5, a6, a7) \
       gasnetc_AMReplyLongM(token, handler, source_addr, nbytes, token_addr, 8, (gasnet_handlerarg_t)a0, (gasnet_handlerarg_t)a1, (gasnet_handlerarg_t)a2, (gasnet_handlerarg_t)a3, (gasnet_handlerarg_t)a4, (gasnet_handlerarg_t)a5, (gasnet_handlerarg_t)a6, (gasnet_handlerarg_t)a7)

#define gasnet_AMReplyLong9 (token, handler, source_addr, nbytes, token_addr, a0, a1, a2, a3, a4, a5, a6, a7, a8 ) \
        gasnetc_AMReplyLongM(token, handler, source_addr, nbytes, token_addr,  9, (gasnet_handlerarg_t)a0, (gasnet_handlerarg_t)a1, (gasnet_handlerarg_t)a2, (gasnet_handlerarg_t)a3, (gasnet_handlerarg_t)a4, (gasnet_handlerarg_t)a5, (gasnet_handlerarg_t)a6, (gasnet_handlerarg_t)a7, (gasnet_handlerarg_t)a8)
#define gasnet_AMReplyLong10(token, handler, source_addr, nbytes, token_addr, a0, a1, a2, a3, a4, a5, a6, a7, a8, a9) \
        gasnetc_AMReplyLongM(token, handler, source_addr, nbytes, token_addr, 10, (gasnet_handlerarg_t)a0, (gasnet_handlerarg_t)a1, (gasnet_handlerarg_t)a2, (gasnet_handlerarg_t)a3, (gasnet_handlerarg_t)a4, (gasnet_handlerarg_t)a5, (gasnet_handlerarg_t)a6, (gasnet_handlerarg_t)a7, (gasnet_handlerarg_t)a8, (gasnet_handlerarg_t)a9)
#define gasnet_AMReplyLong11(token, handler, source_addr, nbytes, token_addr, a0, a1, a2, a3, a4, a5, a6, a7, a8, a9, a10) \
        gasnetc_AMReplyLongM(token, handler, source_addr, nbytes, token_addr, 11, (gasnet_handlerarg_t)a0, (gasnet_handlerarg_t)a1, (gasnet_handlerarg_t)a2, (gasnet_handlerarg_t)a3, (gasnet_handlerarg_t)a4, (gasnet_handlerarg_t)a5, (gasnet_handlerarg_t)a6, (gasnet_handlerarg_t)a7, (gasnet_handlerarg_t)a8, (gasnet_handlerarg_t)a9, (gasnet_handlerarg_t)a10)
#define gasnet_AMReplyLong12(token, handler, source_addr, nbytes, token_addr, a0, a1, a2, a3, a4, a5, a6, a7, a8, a9, a10, a11) \
        gasnetc_AMReplyLongM(token, handler, source_addr, nbytes, token_addr, 12, (gasnet_handlerarg_t)a0, (gasnet_handlerarg_t)a1, (gasnet_handlerarg_t)a2, (gasnet_handlerarg_t)a3, (gasnet_handlerarg_t)a4, (gasnet_handlerarg_t)a5, (gasnet_handlerarg_t)a6, (gasnet_handlerarg_t)a7, (gasnet_handlerarg_t)a8, (gasnet_handlerarg_t)a9, (gasnet_handlerarg_t)a10, (gasnet_handlerarg_t)a11)

#define gasnet_AMReplyLong13(token, handler, source_addr, nbytes, token_addr, a0, a1, a2, a3, a4, a5, a6, a7, a8, a9, a10, a11, a12) \
        gasnetc_AMReplyLongM(token, handler, source_addr, nbytes, token_addr, 13, (gasnet_handlerarg_t)a0, (gasnet_handlerarg_t)a1, (gasnet_handlerarg_t)a2, (gasnet_handlerarg_t)a3, (gasnet_handlerarg_t)a4, (gasnet_handlerarg_t)a5, (gasnet_handlerarg_t)a6, (gasnet_handlerarg_t)a7, (gasnet_handlerarg_t)a8, (gasnet_handlerarg_t)a9, (gasnet_handlerarg_t)a10, (gasnet_handlerarg_t)a11, (gasnet_handlerarg_t)a12)
#define gasnet_AMReplyLong14(token, handler, source_addr, nbytes, token_addr, a0, a1, a2, a3, a4, a5, a6, a7, a8, a9, a10, a11, a12, a13) \
        gasnetc_AMReplyLongM(token, handler, source_addr, nbytes, token_addr, 14, (gasnet_handlerarg_t)a0, (gasnet_handlerarg_t)a1, (gasnet_handlerarg_t)a2, (gasnet_handlerarg_t)a3, (gasnet_handlerarg_t)a4, (gasnet_handlerarg_t)a5, (gasnet_handlerarg_t)a6, (gasnet_handlerarg_t)a7, (gasnet_handlerarg_t)a8, (gasnet_handlerarg_t)a9, (gasnet_handlerarg_t)a10, (gasnet_handlerarg_t)a11, (gasnet_handlerarg_t)a12, (gasnet_handlerarg_t)a13)
#define gasnet_AMReplyLong15(token, handler, source_addr, nbytes, token_addr, a0, a1, a2, a3, a4, a5, a6, a7, a8, a9, a10, a11, a12, a13, a14) \
        gasnetc_AMReplyLongM(token, handler, source_addr, nbytes, token_addr, 15, (gasnet_handlerarg_t)a0, (gasnet_handlerarg_t)a1, (gasnet_handlerarg_t)a2, (gasnet_handlerarg_t)a3, (gasnet_handlerarg_t)a4, (gasnet_handlerarg_t)a5, (gasnet_handlerarg_t)a6, (gasnet_handlerarg_t)a7, (gasnet_handlerarg_t)a8, (gasnet_handlerarg_t)a9, (gasnet_handlerarg_t)a10, (gasnet_handlerarg_t)a11, (gasnet_handlerarg_t)a12, (gasnet_handlerarg_t)a13, (gasnet_handlerarg_t)a14)
#define gasnet_AMReplyLong16(token, handler, source_addr, nbytes, token_addr, a0, a1, a2, a3, a4, a5, a6, a7, a8, a9, a10, a11, a12, a13, a14, a15) \
        gasnetc_AMReplyLongM(token, handler, source_addr, nbytes, token_addr, 16, (gasnet_handlerarg_t)a0, (gasnet_handlerarg_t)a1, (gasnet_handlerarg_t)a2, (gasnet_handlerarg_t)a3, (gasnet_handlerarg_t)a4, (gasnet_handlerarg_t)a5, (gasnet_handlerarg_t)a6, (gasnet_handlerarg_t)a7, (gasnet_handlerarg_t)a8, (gasnet_handlerarg_t)a9, (gasnet_handlerarg_t)a10, (gasnet_handlerarg_t)a11, (gasnet_handlerarg_t)a12, (gasnet_handlerarg_t)a13, (gasnet_handlerarg_t)a14, (gasnet_handlerarg_t)a15)

/* ------------------------------------------------------------------------------------ */

END_EXTERNC

#endif

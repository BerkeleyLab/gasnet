/*   $Source: /Users/kamil/work/gasnet-cvs2/gasnet/smp-conduit/gasnet_core_internal.h,v $
 *     $Date: 2007/05/06 00:18:36 $
 * $Revision: 1.11.52.1 $
 * Description: GASNet smp conduit header for internal definitions in Core API
 * Copyright 2002, Dan Bonachea <bonachea@cs.berkeley.edu>
 * Terms of use are as specified in license.txt
 */

#ifndef _GASNET_CORE_INTERNAL_H
#define _GASNET_CORE_INTERNAL_H

#include <gasnet_internal.h>

typedef struct {
  gasnet_node_t srcnode;
#if GASNET_DEBUG
  int8_t   isReq; 
  int8_t   handlerRunning; 
  int8_t   replyIssued;    
#endif
} gasnetc_bufdesc_t;

typedef struct {
  uint8_t  requestBuf[GASNETC_MAX_MEDIUM];
  uint8_t  replyBuf[GASNETC_MAX_MEDIUM];
} gasnetc_threadinfo_t;

/*  whether or not to use spin-locking for HSL's */
#define GASNETC_HSL_SPINLOCK 1

/* ------------------------------------------------------------------------------------ */
#define GASNETC_HANDLER_BASE  1 /* reserve 1-63 for the core API */
#define _hidx_gasnetc_auxseg_reqh             (GASNETC_HANDLER_BASE+0)
/* add new core API handlers here and to the bottom of gasnet_core.c */

typedef enum {
  gasnetc_Short=0, 
  gasnetc_Medium=1, 
  gasnetc_Long=2,
  gasnetc_System=3
  } gasnetc_category_t;

#if GASNETI_CLIENT_THREADS
  #define gasnetc_mythread() ((void**)(gasnete_mythread()))
#else
  extern void *_gasnetc_mythread;
  #define gasnetc_mythread() &_gasnetc_mythread
#endif

#endif

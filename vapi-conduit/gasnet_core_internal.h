/*  $Archive:: /Ti/GASNet/template-conduit/gasnet_core_internal.h         $
 *     $Date: 2003/03/21 19:41:07 $
 * $Revision: 1.1.2.2 $
 * Description: GASNet vapi conduit header for internal definitions in Core API
 * Copyright 2002, Dan Bonachea <bonachea@cs.berkeley.edu>
 * Terms of use are as specified in license.txt
 */

#ifndef _GASNET_CORE_INTERNAL_H
#define _GASNET_CORE_INTERNAL_H

#include <gasnet.h>
#include <gasnet_internal.h>

#include <vapi.h>
#include <evapi.h>
#include <vapi_common.h>

extern gasnet_seginfo_t *gasnetc_seginfo;

#define gasnetc_boundscheck(node,ptr,nbytes) gasneti_boundscheck(node,ptr,nbytes,c)

/*  whether or not to use spin-locking for HSL's */
#define GASNETC_HSL_SPINLOCK 1

#if defined(DEBUG) && !defined(GASNET_QUIET)
  #define DEBUG_VERBOSE               1
#else
  #define DEBUG_VERBOSE               0
#endif

/* ------------------------------------------------------------------------------------ */
/* make a GASNet call - if it fails, print error message and return */
#define GASNETC_SAFE(fncall) do {                            \
   int retcode = (fncall);                                   \
   if_pf (gasneti_VerboseErrors && retcode != GASNET_OK) {   \
     char msg[1024];                                         \
     sprintf(msg, "\nGASNet encountered an error: %s(%i)\n", \
        gasneti_ErrorName(retcode), retcode);                \
     GASNETI_RETURN_ERRFR(RESOURCE, fncall, msg);            \
   }                                                         \
 } while (0)

/* ------------------------------------------------------------------------------------ */
#define GASNETC_HANDLER_BASE  1 /* reserve 1-63 for the core API */
#define _hidx_                              (GASNETC_HANDLER_BASE+)
/* add new core API handlers here and to the bottom of gasnet_core.c */


/* Use of IB's 32-bit immediate data:
 *   0-1: category
 *     2: request or reply
 *   3-7: numargs
 *  8-15: handerID
 * 16-31: source-generated sequence number	(might replace w/ source ID?)
 */

typedef enum {
  gasnetc_Short=0,
  gasnetc_Medium=1,
  gasnetc_Long=2,
  gasnetc_System=3
} gasnetc_category_t;

#define GASNETC_MSG_GENFLAGS(isreq, cat, nargs, hand, seq)	\
  (uint32_t)(  (((seq)    & 0xffff) << 16)	\
	     | (((hand)   & 0xff)   << 8 )	\
	     | (((nargs)  & 0x1f)   << 3 )	\
	     | ((!(isreq) & 0x1)    << 2 )	\
	     | (((cat)    & 0x3)         ))

#define GASNETC_MSG_NUMARGS(flags)	(((flags) >> 3) & 0x1f)
#define GASNETC_MSG_ISREQUEST(flags)	(!((flags) & 0x4))
#define GASNETC_MSG_CATEGORY(flags)	((gasnetc_category_t)((flags) & 0x3))
#define GASNETC_MSG_HANDLERID(flags)	((uint8_t)((flags) >> 8))
#define GASNETC_MSG_SEQUENCE(flags)	((uint16_t)((flags) >> 16))

/* Structure for a cep, connection end-point
 * Include whatever per-node data we need.
 */
typedef struct {
  /* ### Need more here */
  VAPI_qp_hndl_t	qp_handle;
} gasnetc_cep_t;

typedef struct {
  uint32_t	args[GASNETC_MAX_ARGS];	
} gasnetc_shortmsg_t;

typedef struct {
  uint16_t	nBytes;
  uint16_t	_pad0;
  uint32_t	args[GASNETC_MAX_ARGS];	
} gasnetc_medmsg_t;

typedef struct {
  uintptr_t	destLoc;
  uint32_t	nBytes;
  uint32_t	args[GASNETC_MAX_ARGS];	
} gasnetc_longmsg_t;


#endif

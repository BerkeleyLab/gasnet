/*  $Archive:: /Ti/GASNet/mpi-conduit/gasnet_core_internal.h              $
 *     $Date: 2004/01/05 05:01:17 $
 * $Revision: 1.13 $
 * Description: GASNet MPI conduit header for internal definitions in Core API
 * Copyright 2002, Dan Bonachea <bonachea@cs.berkeley.edu>
 * Terms of use are as specified in license.txt
 */

#ifndef _GASNET_CORE_INTERNAL_H
#define _GASNET_CORE_INTERNAL_H

#include <gasnet.h>
#include <gasnet_internal.h>

/*  whether or not to use spin-locking for HSL's */
#define GASNETC_HSL_SPINLOCK 1
#define GASNETC_NISTIMEOUT_WARNING_THRESHOLD 1000000 /* us for timeout warning under HSL_ERRCHECK */

extern ep_t gasnetc_endpoint;
extern gasnet_seginfo_t *gasnetc_seginfo;

#define gasnetc_boundscheck(node,ptr,nbytes) gasneti_boundscheck(node,ptr,nbytes,c)

extern gasneti_mutex_t gasnetc_AMlock; /*  protect access to AMMPI */
#define AMLOCK()             gasneti_mutex_lock(&gasnetc_AMlock)
#define AMUNLOCK()           gasneti_mutex_unlock(&gasnetc_AMlock)
#define AM_ASSERT_LOCKED()   gasneti_mutex_assertlocked(&gasnetc_AMlock)
#define AM_ASSERT_UNLOCKED() gasneti_mutex_assertunlocked(&gasnetc_AMlock)

/* ------------------------------------------------------------------------------------
 *  AM Error Handling
 * ------------------------------------------------------------------------------------ */
GASNET_INLINE_MODIFIER(gasneti_AMErrorName)
const char *gasneti_AMErrorName(int errval) {
  switch (errval) {
    case AM_OK:           return "AM_OK";      
    case AM_ERR_NOT_INIT: return "AM_ERR_NOT_INIT";      
    case AM_ERR_BAD_ARG:  return "AM_ERR_BAD_ARG";       
    case AM_ERR_RESOURCE: return "AM_ERR_RESOURCE";      
    case AM_ERR_NOT_SENT: return "AM_ERR_NOT_SENT";      
    case AM_ERR_IN_USE:   return "AM_ERR_IN_USE";       
    default: return "*unknown*";
    }
  }

/* ------------------------------------------------------------------------------------ */
/* make an AM call - if it fails, print error message and return */
#define GASNETI_AM_SAFE(fncall) do {                            \
   int retcode = (fncall);                                      \
   if (gasneti_VerboseErrors && retcode != AM_OK) {                                      \
     char msg[1024];                                            \
     sprintf(msg, "\nGASNet encountered an AM Error: %s(%i)\n", \
        gasneti_AMErrorName(retcode), retcode);                 \
     GASNETI_RETURN_ERRFR(RESOURCE, fncall, msg);               \
   }                                                            \
 } while (0)

/* ------------------------------------------------------------------------------------ */
/* make an AM call - 
 * if it fails, print error message and value of expression is FALSE, 
 * otherwise, the value of this expression will be TRUE 
 */
#define GASNETI_AM_SAFE_NORETURN(fncall) (gasneti_VerboseErrors ?        \
      gasneti_checkAMreturn(fncall, #fncall,                             \
                          GASNETI_CURRENT_FUNCTION, __FILE__, __LINE__): \
      (fncall) == AM_OK)
GASNET_INLINE_MODIFIER(gasneti_checkAMreturn)
int gasneti_checkAMreturn(int retcode, const char *fncallstr, 
                                const char *context, const char *file, int line) {
   if (retcode != AM_OK) {  
     fprintf(stderr, "\nGASNet %s encountered an AM Error: %s(%i)\n"
                     "  at %s:%i\n", 
       context, 
       gasneti_AMErrorName(retcode), 
       retcode, file, line); 
     fflush(stderr);
     return FALSE;
   }
   else return TRUE;
}
/* ------------------------------------------------------------------------------------ */
/* make a GASNet call - if it fails, print error message and return */
#define GASNETC_SAFE(fncall) do {                            \
   int retcode = (fncall);                                   \
   if_pf (gasneti_VerboseErrors && retcode != GASNET_OK) {                               \
     char msg[1024];                                         \
     sprintf(msg, "\nGASNet encountered an error: %s(%i)\n", \
        gasnet_ErrorName(retcode), retcode);                 \
     GASNETI_RETURN_ERRFR(RESOURCE, fncall, msg);            \
   }                                                         \
 } while (0)

/* ------------------------------------------------------------------------------------ */
#define GASNETC_HANDLER_BASE  1 /* reserve 1-63 for the core API */
#define _hidx_                              (GASNETC_HANDLER_BASE+)
/* add new core API handlers here and to the bottom of gasnet_core.c */


#endif

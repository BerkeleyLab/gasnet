/* vapi-conduit/firehose_fwd.h
 * $Date: 2004/02/09 23:03:32 $
 * $Revision: 1.2 $
 * Description: Configuration of firehose code to fit vapi-conduit
 * Copyright 2003, E. O. Lawrence Berekely National Laboratory
 * Terms of use are as specified in license.txt
 */

#ifndef _VAPI_FIREHOSE_FWD_H
#define _VAPI_FIREHOSE_FWD_H

#include <vapi_types.h>

/* Set this here because we need it to match */
#define FH_BUCKET_SIZE	GASNETI_PAGESIZE

/* vapi offers "Fast Memory Regions".
 * They really are faster, so we use them by default */
#define FIREHOSE_VAPI_USE_FMR 1

/* vapi-conduit uses firehose-region */
#define FIREHOSE_REGION

/* vapi-conduit allows completion callbacks to run in handlers */
#define FIREHOSE_COMPLETION_IN_HANDLER

/* vapi-conduit has a client_t */
#define FIREHOSE_CLIENT_T
typedef struct _firehose_client_t {
    #if FIREHOSE_VAPI_USE_FMR
      EVAPI_fmr_hndl_t handle;	/* used to release the region */
    #else
      VAPI_mr_hndl_t   handle;	/* used to release the region */
    #endif
    VAPI_lkey_t      lkey;	/* used for local access by HCA */
    VAPI_rkey_t      rkey;	/* used for remote access by HCA */
} firehose_client_t;

/* vapi-conduit does not presently use remote callback.
   XXX: Don't yet have a way to disable this entirely. */
typedef int firehose_remotecallback_args_t;
#define FIREHOSE_REMOTE_CALLBACK_IN_HANDLER

/* Setup conduit-specific region parameters
 * Note that these are kept to sane sizes rather than the HCA limit
 * 128kB is the peak of the bandwidth curve and thus a good size.
 * With 32k * 128k = 4G we can pin upto 4GB of physical memory with these.
 * We don't yet deal well with many small regions.
 */
#define FIREHOSE_CLIENT_MAXREGIONS	32768
#define FIREHOSE_CLIENT_MAXREGION_SIZE	131072

#endif

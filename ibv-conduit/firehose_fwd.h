/*   $Source: /Users/kamil/work/gasnet-cvs2/gasnet/ibv-conduit/firehose_fwd.h,v $
 *     $Date: 2005/12/07 00:20:44 $
 * $Revision: 1.8 $
 * Description: Configuration of firehose code to fit vapi-conduit
 * Copyright 2003, E. O. Lawrence Berekely National Laboratory
 * Terms of use are as specified in license.txt
 */

#ifndef _VAPI_FIREHOSE_FWD_H
#define _VAPI_FIREHOSE_FWD_H

#include <vapi_types.h>

/* Set this here because we need it to match */
#define FH_BUCKET_SIZE	GASNET_PAGESIZE

/* vapi offers "Fast Memory Regions".
 * They really are faster, so we use them by default when available */
#ifndef FIREHOSE_VAPI_USE_FMR
  #if HAVE_VAPI_FMR
    #define FIREHOSE_VAPI_USE_FMR 1
  #else
    #define FIREHOSE_VAPI_USE_FMR 0
  #endif
#endif

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

#ifndef GASNETC_PUTINMOVE_LIMIT_MAX
  /* Compile-time max bytes to piggyback on a put/miss.
   * Environment can always specify a lesser limit, but not larger.
   */
  #define GASNETC_PUTINMOVE_LIMIT_MAX 3072
#endif
typedef struct {
    void	*addr;
    size_t	len;
    char	data[GASNETC_PUTINMOVE_LIMIT_MAX];
} firehose_remotecallback_args_t;

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

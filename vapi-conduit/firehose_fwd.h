/* vapi-conduit/firehose_fwd.h
 * $Date: 2003/10/07 20:48:26 $
 * $Revision: 1.1.2.1 $
 * Description: Configuration of firehose code to fit vapi-conduit
 * Copyright 2003, E. O. Lawrence Berekely National Laboratory
 * Terms of use are as specified in license.txt
 */

#include <vapi_types.h>

/* vapi-conduit uses firehose-region */
#define FIREHOSE_REGION

/* vapi-conduit allows completion callbacks to run in handlers */
#define FIREHOSE_COMPLETION_IN_HANDLER

/* vapi-conduit has a client_t */
#define FIREHOSE_CLIENT_T
typedef struct _firehose_client_t {
    VAPI_mr_hndl_t   handle;	/* used to release the region */
    VAPI_lkey_t      lkey;	/* used for local access by HCA */
    VAPI_rkey_t      rkey;	/* used for remote access by HCA */
} firehose_client_t;

/* vapi-conduit does not presently use remote callback.
   XXX: Don't yet have a way to disable this entirely. */
typedef int firehose_remotecallback_args_t;

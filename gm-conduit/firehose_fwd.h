/*   $Source: /Users/kamil/work/gasnet-cvs2/gasnet/gm-conduit/Attic/firehose_fwd.h,v $
 *     $Date: 2004/08/30 06:57:50 $
 * $Revision: 1.2.8.1 $
 * Description: 
 * Copyright 2004, Christian Bell <csbell@cs.berkeley.edu>
 * Terms of use are as specified in license.txt
 */
#include <inttypes.h>
#include <stdlib.h>

/* GASNet/GM uses firehose-page without any callback requirements */
#define FIREHOSE_PAGE

typedef struct _firehose_remotecallback_args_t {
	uintptr_t	local_addr;
	uintptr_t	remote_addr;
	size_t		nbytes;
}
firehose_remotecallback_args_t;


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


#include <firehose.h>
#include <firehose_internal.h>

/* ##################################################################### */
/* PUBLIC FIREHOSE INTERFACE                                             */
/* ##################################################################### */
/* firehose_init()
 * firehose_fini()
 *    
 * firehose_local_pin() 
 *     calls fh_local_pin() helper function
 *
 * firehose_try_local_pin() 
 *     calls fh_local_pin() helper function
 *
 * firehose_remote_pin()
 *     calls fh_acquire_remote_region()
 *
 * firehose_try_remote_pin()
 *
 * firehose_release()
 *     calls fh_release_local_region() or fh_release_remote_region()
 */

extern void
firehose_init(uintptr_t max_pinnable_memory, size_t max_regions, 
	      firehose_info_t *info)
{
	int	i;

	/* XXX to be completed */

	/* initialize firehose and bucket tables */
	/* set firehose maxima according to max_pinnable_memory/max_regions and
	 * environement variables:
	 *  - find firehose 'M' parameter
	 */

	/* Allocate the per-node firehose FIFO queue */
	fh_RemoteNodeFifo = (fh_fifoq_t *) 
		gasneti_malloc(gasnet_nodes() * sizeof(fh_fifoq_t));
	for (i = 0; i < gasnet_nodes(); i++) 
		FH_TAILQ_INIT(&fh_RemoteNodeFifo[i]);

	/* Initialize the local firehose FIFO queue */
	FH_TAILQ_INIT(&fh_LocalFifo);

	/* Initialize the Bucket table to 128k lists */
	fh_BucketTable = fh_hash_create((1<<17));

	#ifdef FIREHOSE_REGION
	/* XXX ??? */
	fh_RegionTable = fh_hash_create((1<<16));
	#endif



	/* hit the request_t freelist for first allocation */
	(void) fh_request_new();

	/* Initialize -page OR -region specific data. _MUST_ be the last thing
	 * called before return */
	fh_init_plugin(max_pinnable_memory, max_regions, info);
	return;
}

/*
 * XXX should call from gasnet_exit(), fatal or not
 *
 */
static firehose_request_t	*fh_request_bufs[];
static fh_bucket_t		*fh_buckets_bufs[];
void
firehose_fini()
{
	int	i;
	/* XXX to be completed 
	 * - free the bucket and firehose tables
	 */

	/* Free the per-node firehose FIFO queues and counters */
	gasneti_free(fh_RemoteNodeFifo);

	/* Deallocate the arrays of request_t buffers used, if applicable */
	for (i = 0; i < 256; i++) {
		if (fh_request_bufs[i] == NULL)
			break;
		gasneti_free(fh_request_bufs[i]);
	}

	/* Deallocate the arrays of bucket buffers used, if applicable */
	for (i = 0; i < 4096; i++) {
		if (fh_buckets_bufs[i] == NULL)
			break;
		gasneti_free(fh_buckets_bufs[i]);
	}

	fh_fini_plugin();
	return;
}


/* firehose_poll()
 *
 * Empties the Callback Fifo Queue.
 *
 * XXX should make fh_callback_t allocated from freelists.
 */
void
firehose_poll()
{
	fh_callback_t	*fhc;

	if (!FH_STAILQ_EMPTY(&fh_CallbackFifo)) {
		FH_POLLQ_LOCK;

		if (!FH_STAILQ_EMPTY(&fh_CallbackFifo)) {
			fhc = FH_STAILQ_FIRST(&fh_CallbackFifo);
			FH_STAILQ_REMOVE_HEAD(&fh_CallbackFifo);
			FH_POLLQ_UNLOCK;

			if (fhc->flags & FH_FLAG_COMPLETION) {
				fh_completion_callback_t *fhcc =
					(fh_completion_callback_t *) fhc;
				fhcc->callback(fhcc->context, fhcc->request);
			}

			/* XXX Add support for remote completion callbacks */
			gasneti_free(fhc);
		}
		else
			FH_POLLQ_UNLOCK;
	}

	return;
}

/*
 * Inlined fh_local_pin
 *
 * for 'firehose_local_pin' and 'firehose_try_local_pin'
 */
GASNET_INLINE_MODIFIER(fh_local_pin)
firehose_request_t *
fh_local_pin(uintptr_t addr, size_t nbytes, firehose_request_t *req)
{
	firehose_region_t	region;

	if (req == NULL) {
		req = fh_request_new();
		req->flags = FH_FLAG_FHREQ;
	}
	else
		req->flags = 0;

	req->node = gasnet_mynode();
	FH_FILL_REGION(&region, addr, nbytes);

	FH_TABLE_LOCK;
	req->internal =
		fh_acquire_local_region(&region);

	FH_TABLE_UNLOCK;

	FH_COPY_REGION_TO_REQUEST(req, &region);

	return req;
}

extern const firehose_request_t *
firehose_local_pin(uintptr_t addr, size_t nbytes, firehose_request_t *req)
{
	FH_TABLE_LOCK;
	req = fh_local_pin(addr, nbytes, req);
	FH_TABLE_UNLOCK;

	return req;
}

extern const firehose_request_t *
firehose_try_local_pin(uintptr_t addr, size_t len, firehose_request_t *ureq)
{
	firehose_request_t	*req = NULL;

	FH_TABLE_LOCK;

	addr = FH_ADDR_ALIGN(addr);
	len  = FH_SIZE_LEN(addr,len);

	if (fh_region_ispinned(gasnet_mynode(), addr, len) != NULL)
		req = fh_local_pin(addr, len, ureq);

	FH_TABLE_UNLOCK;

	return req;
}

extern const firehose_request_t *
firehose_remote_pin(gasnet_node_t node, uintptr_t addr, size_t len,
		    firehose_completed_fn_t callback, void *context,
		    int return_if_pinned, firehose_request_t *ureq) 
{
	firehose_private_t	*priv;
	firehose_region_t	region;
	firehose_request_t	*req = NULL;

	FH_TABLE_LOCK;
	priv = fh_acquire_remote_region(node, &region, callback, context);
	FH_TABLE_UNLOCK;

	if (priv != FH_REGION_UNPINNED) {
		if (ureq == NULL) {
			req = fh_request_new();
			req->flags = FH_FLAG_FHREQ;
		}
		else {
			req = ureq;
			req->flags = 0;
		}
	
		req->internal = priv;
		req->node     = node;

		FH_COPY_REGION_TO_REQUEST(req, &region);

		/* If the request could be entirely pinned, process the
		 * callback or return to user.  If it could not be pinned, the
		 * callback will be subsequently called from within the
		 * firehose library */

		if (!return_if_pinned)
			callback(context, req);
	}

	return req;
}

extern const firehose_request_t *
firehose_try_remote_pin(gasnet_node_t node, uintptr_t addr, size_t len,
			firehose_request_t *ureq)
{
	firehose_request_t	*req = NULL;

	FH_TABLE_LOCK;

	if (fh_region_ispinned(node, addr, len) != NULL) {
		uintptr_t	bucket_addr, end_addr;

		if (ureq == NULL) {
			req = fh_request_new();
			req->flags = FH_FLAG_FHREQ;
		}
		else {
			req = ureq;
			req->flags = 0;
		}

		req->node = node;
		req->addr = FH_ADDR_ALIGN(addr);
		req->len  = FH_SIZE_ALIGN(addr, addr+len);
		end_addr  = req->addr + (uintptr_t) req->len - 1;

 		FH_FOREACH_BUCKET(req->addr, end_addr, bucket_addr) {
			fhi_bucket_acquire(node, bucket_addr);
		}
	}
	FH_TABLE_UNLOCK;

	return req;
}

extern const firehose_request_t *
firehose_partial_remote_pin(gasnet_node_t node, uintptr_t addr, size_t len,
			    firehose_request_t *ureq)
{
	/* Unimplemented, just use a try pin for now */
	return firehose_try_remote_pin(node, addr, len, ureq);
}

extern void
firehose_release(const firehose_request_t **reqs, int numreqs)
{
	int			i;

	FH_TABLE_LOCK;

	for (i = 0; i < numreqs; i++) {
		if (fhi_node(reqs[i]->internal) == gasnet_mynode()) 
			fh_release_local_region(
				(firehose_request_t *) reqs[i]);
		else
			fh_release_remote_region(
				(firehose_request_t *) reqs[i]);

		if (reqs[i]->flags & FH_FLAG_FHREQ)
			fh_request_free((firehose_request_t *) reqs[i]);
	}

	FH_TABLE_UNLOCK;

	return;
}

/* ##################################################################### */
/* COMMON FIREHOSE INTERFACE                                             */
/* ##################################################################### */

/* Although clients can pass a pointer to a request_t, the alternative is to
 * have the firehose library allocate a request_t and return it.  For the
 * latter case, allocation is done using a freelist allocator and the internal
 * pointer is used to link the request_t.
 */
#define FH_REQUEST_ALLOC_PERIDX	256
static firehose_request_t	*fh_request_freehead = NULL;
static int			 fh_request_bufidx = 0;
static firehose_request_t	*fh_request_bufs[256] = { 0 };

firehose_request_t *
fh_request_new()
{
	firehose_request_t	*req;

	FH_TABLE_LOCK;

	if (fh_request_freehead != NULL) {
		req = fh_request_freehead;
		fh_request_freehead = (firehose_request_t *) req->internal;
	}
	else {
		firehose_request_t	*alloc;
		int			 i;

		if (fh_request_bufidx == 256)
			gasneti_fatalerror("Firehose: Ran out "
			    "of request handles (limit=%d)",
			    FH_REQUEST_ALLOC_PERIDX*256);

		alloc = (firehose_request_t *)
			gasneti_malloc(FH_REQUEST_ALLOC_PERIDX*
				       sizeof(firehose_request_t));

		fh_request_bufs[fh_request_bufidx] = alloc;
		fh_request_bufidx++;

		memset(alloc, 0, FH_REQUEST_ALLOC_PERIDX*
		       sizeof(firehose_request_t));

		for (i = 1; i < FH_REQUEST_ALLOC_PERIDX-1; i++)
			alloc[i].internal = (firehose_private_t *) &alloc[i+1];

		alloc[i].internal = NULL;
		req = &alloc[0];
		fh_request_freehead = &alloc[1];
	}

	req->internal = NULL;
	req->flags = 0;
			    
	FH_TABLE_UNLOCK;

	return req;
}

void
fh_request_free(firehose_request_t *req)
{
	FH_TABLE_LOCK;

	req->internal = (firehose_private_t *) fh_request_freehead;
	fh_request_freehead = req;

	FH_TABLE_UNLOCK;

	return;
}

/* region/page must provide implementations of these functions */

/* Data structures (PAGE)
 *
 * Table of fh_bucket_t (local and remote)
 *   Adding: fh_bucket_t are added once a bucket is pinned locally or a
 *           firehose maps to a remote bucket.
 *   Removing: Local fh_bucket_t are removed once a bucket is unpinned locally.
 *             Remote firehoses to fh_bucket_t are removed once an AM move is
 *             completed and that bucket had been selected as a replacement
 *             bucket.
 *
 * Local Victim Fifo list of fh_bucket_t (oldest at tail, newest at head)
 *   Popping: fh_bucket_t are usually removed so as to create one contiguous
 *            region_t.
 *   Pushing: fh_bucket_t are usually pushed in reverse order from a region_t.
 *            This allows a subsequent popping operation to construct a
 *            contiguous region_t.
 *
 * Per-node firehose victim FIFO
 *   Popping: A firehose fh_bucket_t is removed when a node decides that it has
 *            used up all it's firehoses to a remote node and needs replacement
 *            buckets.
 *
 *   Pushing: Firehoses for which fh_bucket_t reaches a refcount of zero are
 *            added to the per-node firehose victim FIFO.
 */

/* Metadata that can be used while holding the FH_TABLE_LOCK.
 *
 * Temporary arrays:
 *
 * 1. fh_bucket_t **fh_bucket_temp (of size max_RemotePinSize << FH_BUCKET_SIZE)
 *    This array can be used to construct a temporary array of pointers to
 *    fh_bucket_t.
 */

/* ##################################################################### */
/* Bucket (local and remote) operations (COMMON CODE)                    */
/* ##################################################################### */

static fh_bucket_t	*fh_buckets_freehead = NULL;
static int		 fh_buckets_bufidx = 0;
static fh_bucket_t	*fh_buckets_bufs[4096] = { 0 };
static int		 fh_buckets_per_alloc = 0;

void
fh_bucket_init_freelist(int max_buckets_pinned)
{
	/* XXX this should probably be further aligned. . */
	fh_buckets_per_alloc = (int) (max_buckets_pinned + (4096-1)) / 4096;
	fh_buckets_freehead = NULL; 

	return;
}

fh_bucket_t *
fh_bucket_lookup(gasnet_node_t node, uintptr_t bucket_addr)
{
	fh_bucket_t *entry;

	FH_TABLE_ASSERT_LOCKED;

	FH_ASSERT_BUCKET_ADDR(bucket_addr);

	entry = (fh_bucket_t *)
		fh_hash_find(fh_BucketTable, fhi_key_make(bucket_addr, node));

	return entry;
}

fh_bucket_t *
fh_bucket_add(gasnet_node_t node, uintptr_t bucket_addr)
{
	fh_bucket_t	*entry;

	FH_TABLE_ASSERT_LOCKED;
	FH_ASSERT_BUCKET_ADDR(bucket_addr);

	/* allocate a new bucket for the table */
	if (fh_buckets_freehead != NULL) {
		entry = fh_buckets_freehead;
		fh_buckets_freehead = entry->fh_next;
	}
	else {
		fh_bucket_t	*alloc;
		int		 i;

		if (fh_buckets_bufidx == 4096)
			gasneti_fatalerror("Firehose: Ran out of "
				"hash entries (limit=%d)",
				4096*fh_buckets_per_alloc);

		alloc = (fh_bucket_t *) 
			gasneti_malloc(fh_buckets_per_alloc*
				       sizeof(fh_bucket_t *));

		memset(alloc, 0, fh_buckets_per_alloc*sizeof(fh_bucket_t *));

		fh_buckets_bufs[fh_buckets_bufidx] = alloc;
		fh_buckets_bufidx++;

		for (i = 1; i < fh_buckets_per_alloc-1; i++)
			alloc[i].fh_next = &alloc[i+1];

		alloc[i].fh_next = NULL;
		entry = &alloc[0];
		entry->fh_next = NULL;

		fh_buckets_freehead = &alloc[1];
	}

	entry->fh_key = fhi_key_make(bucket_addr, node);
	fh_hash_insert(fh_BucketTable, entry->fh_key, entry);

	return entry;
}

void
fh_bucket_remove(fh_bucket_t *entry)
{
	fh_bucket_t *bucket;

	FH_TABLE_ASSERT_LOCKED;
	bucket = fh_hash_insert(fh_BucketTable, entry->fh_key, NULL);
	bucket->fh_next = fh_buckets_freehead;
	fh_buckets_freehead = bucket;
}
/* 
 * fh_getenv()
 *
 * Firehose environement variables are units given 
 *
 * Recognizes modifiers [Mm][Kk][Gg] in numbers 
 */ 
unsigned long
fh_getenv(const char *var, unsigned long multiplier)
{
        char	*env;
        char	numbuf[32], c;
        int	i;
        double	num;

        env = gasnet_getenv(var);

        if (env == NULL || *env == '\0')
                return 0;

        memset(numbuf, '\0', 32);
        for (i = 0; i < strlen(env) && i < 32; i++) {
                c = env[i];
                if ((c >= '0' && c <= '9') || c == '.')
                        numbuf[i] = c;
                else {  
                        if (c == 'M' || c == 'm')
                                multiplier = 1U<<20;
                        else if (c == 'G' || c == 'g')
                                multiplier = 1U<<30;
                        else if (c == 'K' || c == 'k')
                                multiplier = 1U<<10;
                        break;
                }
        }
        num = atof(numbuf);
        num *= multiplier;

        return (unsigned long) num;
}


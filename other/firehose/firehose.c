#include <firehose.h>
#include <firehose_internal.h>

/* ##################################################################### */
/* PUBLIC FIREHOSE INTERFACE                                             */
/* ##################################################################### */
/* firehose_init()
 * firehose_fini()
 *    
 * firehose_local_pin() 
 *     calls fh_acquire_local_region()
 *
 * firehose_try_local_pin() 
 *     calls fh_commit_try_local_region()
 *
 * firehose_partial_local_pin()
 *     calls fh_commit_try_local_region()
 *
 * firehose_remote_pin()
 *     calls fh_acquire_remote_region()
 *
 * firehose_try_local_pin() 
 *     calls fh_commit_try_remote_region()
 *
 * firehose_partial_local_pin()
 *     calls fh_commit_try_remote_region()
 *
 * firehose_release()
 *     calls fh_release_local_region() or fh_release_remote_region()
 */

gasnet_node_t	fh_mynode = (gasnet_node_t)-1;

fh_hash_t	*fh_BucketTable;
#ifdef FIREHOSE_REGION
fh_hash_t	*fh_RegionTable;
#endif

extern void
firehose_init(uintptr_t max_pinnable_memory, size_t max_regions, 
	      const firehose_region_t *prepinned_regions,
              size_t num_reg, firehose_info_t *info)
{
	int	i;

	/* Make sure the refc field in buckets can also be used as a FIFO
	 * pointer */
	gasneti_assert(sizeof(fh_refc_t) == sizeof(void *));

	FH_TABLE_LOCK;

	fh_mynode = gasnet_mynode();

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
	{
		firehose_request_t *req = fh_request_new(NULL);
		fh_request_free(req);
	}

	/* Initialize -page OR -region specific data. _MUST_ be the last thing
	 * called before return */
	fh_init_plugin(max_pinnable_memory, max_regions, prepinned_regions, 
		       num_reg, info);

	FH_TABLE_UNLOCK;

	return;
}

/*
 * XXX should call from gasnet_exit(), fatal or not
 *
 */
static firehose_request_t	*fh_request_bufs[256] = { 0 };
static fh_bucket_t		*fh_buckets_bufs[FH_BUCKETS_BUFS] = { 0 };

void
firehose_fini()
{
	int	i;
	/* Free the per-node firehose FIFO queues and counters */
	gasneti_free(fh_RemoteNodeFifo);

	/* Deallocate the arrays of request_t buffers used, if applicable */
	for (i = 0; i < 256; i++) {
		if (fh_request_bufs[i] == NULL)
			break;
		gasneti_free(fh_request_bufs[i]);
	}

	/* Deallocate the arrays of bucket buffers used, if applicable */
	for (i = 0; i < FH_BUCKETS_BUFS; i++) {
		if (fh_buckets_bufs[i] == NULL)
			break;
		gasneti_free(fh_buckets_bufs[i]);
	}

	fh_hash_destroy(fh_BucketTable);

	fh_fini_plugin();
	return;
}

/* firehose_poll()
 *
 * Empties the Callback Fifo Queue.
 *
 * XXX should make fh_callback_t allocated from freelists.
 */
#ifndef FH_POLL_NOOP
fh_pollq_t	fh_CallbackFifo = FH_STAILQ_HEAD_INITIALIZER(fh_CallbackFifo);

void
firehose_poll()
{
	fh_callback_t	*fhc;

	while (!FH_STAILQ_EMPTY(&fh_CallbackFifo)) {
		FH_POLLQ_LOCK;

		if (!FH_STAILQ_EMPTY(&fh_CallbackFifo)) {
			fhc = FH_STAILQ_FIRST(&fh_CallbackFifo);
			FH_STAILQ_REMOVE_HEAD(&fh_CallbackFifo);
			FH_POLLQ_UNLOCK;

			#ifndef FIREHOSE_COMPLETION_IN_HANDLER
			if (fhc->flags & FH_CALLBACK_TYPE_COMPLETION) {
				fh_completion_callback_t *cc =
					(fh_completion_callback_t *) fhc;
				cc->callback(cc->context, cc->request, 0);
			}
			#endif

			#ifndef FIREHOSE_REMOTE_CALLBACK_IN_HANDLER
			else if (fhc->flags & FH_CALLBACK_TYPE_REMOTE) {
				fh_remote_callback_t *rc =
					(fh_remote_callback_t *) fhc;
				firehose_remote_callback(rc->node, 
					rc->pin_list, rc->pin_list_num, 
					&(rc->args));

				/* Send an AMRequest to the reply handler */
				fh_send_firehose_reply(rc);
				gasneti_free(rc->pin_list);
				gasneti_free(fhc);
			}
			#endif
		}
		else
			FH_POLLQ_UNLOCK;
	}

	return;
}
#endif

extern const firehose_request_t *
firehose_local_pin(uintptr_t addr, size_t nbytes, firehose_request_t *ureq)
{
	firehose_request_t	*req = NULL;
	firehose_region_t	region;

	region.addr = FH_ADDR_ALIGN(addr);
	region.len  = FH_SIZE_ALIGN(addr,nbytes);

	FH_TABLE_LOCK;

	fh_acquire_local_region(&region);

	req         = fh_request_new(ureq);
	req->node   = fh_mynode;
	req->flags |= FH_FLAG_PINNED;
	FH_COPY_REGION_TO_REQUEST(req, &region);

	FH_TABLE_UNLOCK;

	return req;
}

extern const firehose_request_t *
firehose_try_local_pin(uintptr_t addr, size_t len, firehose_request_t *ureq)
{
	firehose_request_t	*req = NULL;
	firehose_region_t	region;

	region.addr = FH_ADDR_ALIGN(addr);
	region.len  = FH_SIZE_ALIGN(addr,len);

	FH_TABLE_LOCK;
	if (fh_region_ispinned(fh_mynode, &region)) {
		fh_commit_try_local_region(&region);

		req         = fh_request_new(ureq);
		req->node   = fh_mynode;
		req->flags |= FH_FLAG_PINNED;
		FH_COPY_REGION_TO_REQUEST(req, &region);
	}
	FH_TABLE_UNLOCK;

	return req;
}

extern const firehose_request_t *
firehose_partial_local_pin(uintptr_t addr, size_t len,
                           firehose_request_t *ureq)
{
	firehose_request_t	*req = NULL;
	firehose_region_t	region;

	region.addr = FH_ADDR_ALIGN(addr);
	region.len  = FH_SIZE_ALIGN(addr,len);

	FH_TABLE_LOCK;
	if (fh_region_partial(fh_mynode, &region)) {
		fh_commit_try_local_region(&region);

		req         = fh_request_new(ureq);
		req->node   = fh_mynode;
		req->flags |= FH_FLAG_PINNED;
		FH_COPY_REGION_TO_REQUEST(req, &region);
	}
	FH_TABLE_UNLOCK;

	return req;
}

extern const firehose_request_t *
firehose_remote_pin(gasnet_node_t node, uintptr_t addr, size_t len,
		    uint32_t flags, firehose_request_t *ureq,
		    firehose_remotecallback_args_t *remote_args,
		    firehose_completed_fn_t callback, void *context)
{
	firehose_region_t	region;
	firehose_request_t	*req = NULL;

	if_pf (node == fh_mynode)
		gasneti_fatalerror("Cannot request a Remote pin on a local node.");

	region.addr = FH_ADDR_ALIGN(addr); 
	region.len  = FH_SIZE_ALIGN(addr,len);

	gasneti_assert(remote_args == NULL ? 1 : 
		(flags & FIREHOSE_FLAG_ENABLE_REMOTE_CALLBACK));

	/* The 'req' is allocated in fh_acquire_remote_region() since that
	 * function needs to unlock the table lock prior to returning */
	req = fh_acquire_remote_region(node, &region, callback, context,
			flags, remote_args, ureq);

	if (req->flags & FH_FLAG_PINNED) {
		/* If the request could be entirely pinned, process the
		 * callback or return to user.  If it could not be pinned, the
		 * callback will be subsequently called from within the
		 * firehose library */

		if (!(flags & FIREHOSE_FLAG_RETURN_IF_PINNED)) {
			GASNETI_TRACE_PRINTF(C, 
			    ("Firehoses pinned, callback"));
			callback(context, req, 1);
		}
		return req;
	}
	else
		return NULL;
}

extern const firehose_request_t *
firehose_try_remote_pin(gasnet_node_t node, uintptr_t addr, size_t len,
			uint32_t flags, firehose_request_t *ureq)
{
	firehose_request_t	*req = NULL;
	firehose_region_t	region;

	if_pf (node == fh_mynode)
		gasneti_fatalerror("Cannot request a Remote pin on a local node.");

	region.addr = FH_ADDR_ALIGN(addr);
	region.len  = FH_SIZE_ALIGN(addr,len);

	FH_TABLE_LOCK;

	if (fh_region_ispinned(node, &region)) {
		req = fh_request_new(ureq);
		req->node = node;

		fh_commit_try_remote_region(node, &region);
		FH_COPY_REGION_TO_REQUEST(req, &region);
	}
	FH_TABLE_UNLOCK;

	return req;
}

extern const firehose_request_t *
firehose_partial_remote_pin(gasnet_node_t node, uintptr_t addr,
                            size_t len, uint32_t flags,
                            firehose_request_t *ureq)
{
	firehose_request_t	*req = NULL;
	firehose_region_t	region;

	if_pf (node == fh_mynode)
		gasneti_fatalerror("Cannot request a Remote pin on a local node.");

	region.addr = FH_ADDR_ALIGN(addr);
	region.len  = FH_SIZE_ALIGN(addr,len);

	FH_TABLE_LOCK;

	if (fh_region_partial(node, &region)) {
		req = fh_request_new(ureq);
		req->node = node;
		fh_commit_try_remote_region(node, &region);
		FH_COPY_REGION_TO_REQUEST(req, &region);
	}
	FH_TABLE_UNLOCK;

	return req;
}

extern void
firehose_release(firehose_request_t const **reqs, int numreqs)
{
	int			i;

	FH_TABLE_LOCK;

	for (i = 0; i < numreqs; i++) {
		if (reqs[i]->node == fh_mynode)
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

/* TODO: allocate from a pool */
fh_completion_callback_t *
fh_alloc_completion_callback()
{
	fh_completion_callback_t *cc;

	FH_TABLE_ASSERT_LOCKED;

	cc = gasneti_malloc(sizeof(fh_completion_callback_t));
	if_pf (cc == NULL)
		gasneti_fatalerror("malloc in remote callback");
	cc->flags = FH_CALLBACK_TYPE_COMPLETION;

	return cc;
}

void
fh_free_completion_callback(fh_completion_callback_t *cc)
{
	FH_TABLE_ASSERT_LOCKED;

	gasneti_free(cc);
	return;
}

/* Although clients can pass a pointer to a request_t, the alternative is to
 * have the firehose library allocate a request_t and return it.  For the
 * latter case, allocation is done using a freelist allocator and the internal
 * pointer is used to link the request_t.
 */
#define FH_REQUEST_ALLOC_PERIDX	256
static firehose_request_t	*fh_request_freehead = NULL;
static int			 fh_request_bufidx = 0;

firehose_request_t *
fh_request_new(firehose_request_t *ureq)
{
	firehose_request_t	*req;

	FH_TABLE_ASSERT_LOCKED;

	if (ureq != NULL) {
		req = ureq;
		req->flags = 0;
		/*
		req->internal = 
		    (firehose_private_t *) fh_alloc_completion_callback();
		((fh_completion_callback_t *)req->internal)->request = req;
		*/
		return req;
	}

	if (fh_request_freehead != NULL) {
		req = fh_request_freehead;
		fh_request_freehead = (firehose_request_t *) req->internal;
	}
	else {
		firehose_request_t	*buf;
		int			 i;

		if (fh_request_bufidx == 256)
			gasneti_fatalerror("Firehose: Ran out "
			    "of request handles (limit=%d)",
			    FH_REQUEST_ALLOC_PERIDX*256);

		buf = (firehose_request_t *)
			gasneti_malloc(FH_REQUEST_ALLOC_PERIDX*
				       sizeof(firehose_request_t));

		fh_request_bufs[fh_request_bufidx] = buf;
		fh_request_bufidx++;

		memset(buf, 0, FH_REQUEST_ALLOC_PERIDX*
		       sizeof(firehose_request_t));

		for (i = 1; i < FH_REQUEST_ALLOC_PERIDX-1; i++)
			buf[i].internal = (firehose_private_t *) &buf[i+1];

		buf[i].internal = NULL;
		req = &buf[0];
		fh_request_freehead = &buf[1];
	}

	req->flags = FH_FLAG_FHREQ;
	req->internal = NULL;
			    
	return req;
}

void
fh_request_free(firehose_request_t *req)
{
	FH_TABLE_ASSERT_LOCKED;

	if (req->flags & FH_FLAG_PENDING) {
		gasneti_assert(req->internal != NULL);
		fh_free_completion_callback(
		    (fh_completion_callback_t *)req->internal);
	}
	/*
	else
		gasneti_assert(req->internal == NULL);
		*/

	if (req->flags & FH_FLAG_FHREQ) {
		req->flags = 0;
		req->internal = (firehose_private_t *) fh_request_freehead;
		fh_request_freehead = req;
	}
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
 * Local Victim Fifo list of fh_bucket_t (oldest at head, newest at tail)
 *   Popping: fh_bucket_t are usually removed so as to create one contiguous
 *            region_t.
 *   Pushing: fh_bucket_t are usually pushed in reverse order from a region_t.
 *            This allows a subsequent popping operation to optimistically
 *            construct contiguous region_t's.
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
static int		 fh_buckets_per_alloc = 0;

void
fh_bucket_init_freelist(int max_buckets_pinned)
{
	FH_TABLE_ASSERT_LOCKED;

	/* XXX this should probably be further aligned. . */
	fh_buckets_per_alloc = (int) MAX( 
	    ((max_buckets_pinned + (FH_BUCKETS_BUFS-1)) / FH_BUCKETS_BUFS),
	    (1024));

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
		fh_hash_find(fh_BucketTable, FH_KEYMAKE(bucket_addr, node));

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
		fh_bucket_t	*buf;
		int		 i;

		if (fh_buckets_bufidx == FH_BUCKETS_BUFS)
			gasneti_fatalerror("Firehose: Ran out of "
				"hash entries (limit=%d)",
				FH_BUCKETS_BUFS*fh_buckets_per_alloc);

		buf = (fh_bucket_t *) 
			gasneti_malloc(fh_buckets_per_alloc*
				       sizeof(fh_bucket_t));
		if (buf == NULL)
			gasneti_fatalerror("Couldn't allocate buffer "
			    "of buckets");

		memset(buf, 0, fh_buckets_per_alloc*sizeof(fh_bucket_t));

		fh_buckets_bufs[fh_buckets_bufidx] = buf;
		fh_buckets_bufidx++;

		for (i = 1; i < fh_buckets_per_alloc-1; i++)
			buf[i].fh_next = &buf[i+1];

		buf[i].fh_next = NULL;
		entry = &buf[0];
		entry->fh_next = NULL;

		fh_buckets_freehead = &buf[1];
	}

	entry->fh_key = FH_KEYMAKE(bucket_addr, node);
	FH_SET_USED(entry);

	fh_hash_insert(fh_BucketTable, entry->fh_key, entry);
	gasneti_assert(fh_bucket_lookup(node, bucket_addr) == entry);

	return entry;
}

void
fh_bucket_remove(fh_bucket_t *entry)
{
	fh_bucket_t *bucket;

	FH_TABLE_ASSERT_LOCKED;
	FH_SET_USED(entry);
	bucket = fh_hash_insert(fh_BucketTable, entry->fh_key, NULL);
	gasneti_assert(entry == bucket);
	memset(bucket, 0, sizeof(fh_bucket_t));
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
			/* XXX this is only here for testing purposes */
			else if (c == 'b')
				multiplier = 1;
                        break;
                }
        }
        num = atof(numbuf);
        num *= multiplier;

        return (unsigned long) num;
}


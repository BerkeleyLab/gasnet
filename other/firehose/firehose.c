#include <firehose.h>
#include <firehose_internal.h>

firehose_info_t	*fh_limits_info;

/* ##################################################################### */
/* PUBLIC FIREHOSE INTERFACE                                             */
/* ##################################################################### */
/*
 * firehose_init()
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

extern const firehose_info_t *
firehose_init(uintptr_t max_pinnable_memory, size_t max_regions)
{
	firehose_info_t	*info = (firehose_info_t *) 
	    gasneti_malloc(sizeof(firehose_info_t));

	/* XXX to be completed */
	/* initialize firehose and bucket tables */
	/* set firehose maxima according to max_pinnable_memory/max_regions and
	 * environement variables:
	 *  - find firehose 'M' parameter
	 *  - initialize the request_t freelist
	 *  - initialize the array of per-node victims
	 *  - initialize the array of per-node available firehoses
	 */

	fh_limits_info = info;
	return fh_limits_info;
}

void
firehose_fini()
{
	/* XXX to be completed */
	/* - deallocate arrays of per-node victims, per-node firehoses,
	 * request_t freelist.
	 * - free the bucket and firehose tables
	 */
	return;
}

/*
 * Inlined fh_local_pin
 *
 * for 'firehose_local_pin' and 'firehose_try_local_pin'
 */
GASNET_INLINE_MODIFIER(fh_local_pin)
extern firehose_request_t *
fh_local_pin(uintptr_t addr, size_t nbytes)
{
	firehose_request_t	*req;
	firehose_region_t	region;

	req = fh_request_new();

	req->node = node;
	FH_FILL_REGION(&region, addr, nbytes);

	req->internal =
		fh_acquire_local_region(&region);

	FH_COPY_REGION_TO_REQUEST(req, &region);

	return req;
}

extern firehose_request_t *
firehose_local_pin(uintptr_t addr, size_t nbytes)
{
	firehose_request_t	*req;

	FH_TABLE_LOCK;
	req = fh_local_pin(addr, nbytes);
	FH_TABLE_UNLOCK;

	return req;
}

extern firehose_request_t *
firehose_try_local_pin(uintptr_t addr, size_t nbytes)
{
	firehose_request_t	*req = NULL;

	FH_TABLE_LOCK;

	if (fh_region_ispinned(gasnet_mynode(), addr, len) != NULL)
		req = fh_local_pin(addr, nbytes);

	FH_TABLE_UNLOCK;

	return req;
}

extern firehose_request_t *
firehose_remote_pin(gasnet_node_t node, uintptr_t addr, size_t len,
		    firehose_completed_fn_t callback, void *context,
		    int return_if_pinned)
{
	firehose_request_t	*req = NULL;
	firehose_private_t	*priv;
	firehose_region_t	region;

	FH_TABLE_LOCK;
		priv = fh_acquire_remote_region(&region, callback, context);
	FH_TABLE_UNLOCK;

	if (priv != FH_REGION_UNPINNED) {
		req = fh_request_new();
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

extern firehose_request_t *
firehose_try_remote_pin(gasnet_node_t node, uintptr_t addr, size_t len);
{
	firehose_request_t	*req = NULL;

	FH_TABLE_LOCK;

	if (fh_region_ispinned(node, addr, len) != NULL) {
		uintptr_t	bucket_addr, end_addr;

		req = fh_request_new();

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
firehose_partial_remote_pin(gasnet_node_t node, uintptr_t addr, size_t len)
{
	/* Unimplemented, just use a try pin for now */
	return firehose_try_remote_pin(node, addr, len);
}

extern void
firehose_release(firehose_request_t **reqs, int numreqs)
{
	int			i;

	for (i = 0; i < numreqs; i++) {
		if (fhi_node(reqs[i]->internal) == gasnet_mynode()) 
			fh_release_local_region(reqs[i]);
		else
			fh_release_remote_region(reqs[i]);
	}

	return;
}

/* ##################################################################### */
/* COMMON FIREHOSE INTERFACE                                             */
/* ##################################################################### */

firehose_request_t *
fh_request_new()
{
	/* XXX to be completed */
	return NULL;
}

void
fh_request_free(firehose_request_t *)
{
	/* XXX to be completed */
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
/* ACTIVE MESSAGES                                                       */ 
/* ##################################################################### */

GASNET_INLINE_MODIFIER(fh_am_move_reqh_inner)
void
fh_am_move_reqh_inner(gasnet_token_t token, void *addr,
		      size_t nbytes,
		      gasnet_handlerarg_t new_num,
		      gasnet_handlerarg_t old_num,
		      void *callback,
		      void *context)
{
	firehose_region_t	*new_regions = (firehose_region_t *) addr;
	firehose_region_t	*old_regions = 
				    (firehose_region_t *) addr + new_num;

	gasneti_stattime_t      movetime = GASNETI_STATTIME_NOW_IFENABLED(C);
	gasneti_stattime_t      unpintime;
	gasnet_node_t		node;
	int			i;

	assert(new_regions > 0);

	gasnet_AMGetMsgSource(token, &node);

	FH_TABLE_LOCK;

	/* First take care of old regions, and have the client unpin only old
	 * regions.
	 */


	/*
	 * The algorithm for acquiring a new region is the following:
	 *   Loop over the array of new regions
	 *      Acquire the region (page)
	 *
	 *      If the region is _not_ currently pinned
	 *         copy the region in the "to_be_pinned" array.
	 *         XXX In page, we also see if the previous region in the
	 *             "to_be_pinned" array is contiguous in order to
	 *             coalesce the pin call.
	 *         XXX In region, we first try to see if a superset of the
	 *             requested region can be found to match the reqeusted pin
	 *             region prior to copying the requested region into the
	 *             "to_be_pinned" array.
	 *      Else
	 *         Simply increment the reference count.
	 *
	 *   3. Call firehose_move_callback if there is at least one element in
	 *      the "to_be_unpinned" and "to_be_pinned" arrays.
	 */
	for (i = 0; i < old_num; i++) {
	


	firehose_move_callback(node, &regions[new_regions], old_regions,
				     &regions[0], new_regions);

	/* Now update the reference counts on all */
}

/* ##################################################################### */
/* Bucket (local and remote) operations (COMMON)                         */
/* ##################################################################### */
fh_bucket_t *
fh_bucket_lookup(gasnet_node_t node, uintptr_t bucket_addr)
{
	fh_bucket_t *entry;

	FH_ASSERT_BUCKET_ADDR(bucket_addr);

	entry = (fh_bucket_t *)
		fh_hash_find(fhi_key_make(bucket_addr, node));

	return entry;
}

fh_bucket_t *
fh_bucket_add(gasnet_node_t node, uintptr_t bucket_addr)
{
	fh_bucket_t	*entry;

	FH_ASSERT_BUCKET_ADDR(bucket_addr);

	/* allocate a new bucket for the table */
	fh_freelist_alloc(fh_bucket_t, fh_fifo_next, entry);

	entry->fh_key = fhi_key_make(bucket_addr, node);

	fh_hash_insert(fh_BucketTable, entry->fh_key, entry);

	return entry;
}

void
fh_bucket_remove(fh_bucket_t *entry)
{
	fh_bucket_t	*bucket = fh_hash_delete(fh_BucketTable, entry);
	fh_freelist_free(bucket);
}

int
fh_bucket_refcount(fh_bucket_t *entry)
{
	if (entry->fh_fifo_next != NULL)
		return 0;
	else
		return entry->_fr_union.refcount;
}

int
fh_bucket_release(fh_bucket_t *entry)
{
	/* make sure the refcount is _not_ 0 */
	assert(fh_bucket_refcount(entry) > 0);
	return --(entry->_fr_union.refcount);
}

int
fh_bucket_acquire(fh_bucket_t *entry)
{
	/* make sure the bucket is _not_ in the fifo */
	assert(!fh_bucket_infifo(entry));
	return ++(entry->_fr_union.refcount);
}



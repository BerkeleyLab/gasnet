typedef firehose_private_t	fh_bucket_t;
#define FH_ADDR_ALIGN(addr) (GASNETI_ALIGNDOWN(addr, FH_BUCKET_SIZE)
#define FH_SIZE_ALIGN(addr,len)	(GASNETI_ALIGNUP(addr+len, FH_BUCKET_SIZE)-\
				 GASNETI_ALIGNDOWN(addr, FH_BUCKET_SIZE)
#define FH_NUM_BUCKETS(addr,end)	###

/* values for firehose_private_t * */
#define FH_REQ_UNPINNED	((firehose_private_t *) 0)

/* Macro to ease looping over buckets in a memory region.  'end' here is
 * defined as 'start + len - 1'.  All parameters should be 'uintptr_t'.
 */
#define FH_FOREACH_BUCKET(start,end,bucket_addr)			\
		for ((bucket_addr) = (start); (bucket_addr) <= (end);	\
		    (bucket_addr) += FH_BUCKET_SIZE)


#define FH_FILL_REQUEST(req, nodei, addr, length) do {			\
		(req)->node = (nodei);					\
		(req)->start = FH_ADDR_ALIGN(addr);			\
		(req)->len   = FH_SIZE_ALIGN((req)->start, addr+nbytes);\
		(req)->end   = (req)->start + (req)->len - 1;		\
	} while (0)


/* ######################################################################### */
/* Public firehose interface */

/* firehose_local_pin(addr, nbytes)
 */
extern firehose_request_t *
firehose_local_pin(uintptr_t addr, size_t nbytes)
{
	firehose_request_t	*req;
	unsigned int		num_pinned;

	req = fh_request_new();

	FH_FILL_REQUEST(req, gasnet_mynode(), addr, nbytes);
	num_pinned = fh_pin_local_request(req);

	GASNETI_TRACE_EVENT_VAL(C, BUCKET_LOCAL_PINS, num_pinned);
	GASNETI_TRACE_EVENT_VAL(C, BUCKET_LOCAL_TOUCHED, 
	    (req->len>>FH_BUCKET_SHIFT));

	return req;
}

extern firehose_request_t *
firehose_try_local_pin(uintptr_t addr, size_t nbytes)
{
	uintptr_t		end 
	unsigned int		num_pinned;
	firehose_request_t	*req = NULL;

	end = addr + (uintptr_t) nbytes - 1;

	FH_TABLE_LOCK;

	if (fhi_ispinned_region(gasnet_mynode(), addr, len, end)) {
		req = fh_request_new();

		FH_FILL_REQUEST(req, gasnet_mynode(), addr, nbytes);
		num_pinned = 
		    fh_acquire_local_region(req->start, req->len, req->end);
	}

	FH_TABLE_UNLOCK;

	return req;
}

extern firehose_request_t *
firehose_remote_pin(gasnet_node_t node, uintptr_t addr, size_t len,
		    firehose_completed_fn_t callback, void *context,
		    int return_if_pinned)
{
	firehose_request_t	*req;

	req = fh_request_new();

	req->node  = gasnet_mynode();
	req->start = FH_ADDR_ALIGN(addr);
	req->len   = FH_SIZE_ALIGN(req->start, addr+nbytes);
	req->end   = req->start + req->len - 1;

	num_pinned = fh_remote_pin_request(req);

	/* If the request could be entirely pinned, process the callback or
	 * return to user.  If it could not be pinned, the callback will be
	 * subsequently called from within the firehose library */
	if (req->internal != FH_REQ_UNPINNED) {
		if (!return_if_pinned)
			callback(context, req);
		return req;
	}

	return NULL;
}

extern firehose_request_t *
firehose_try_remote_pin(gasnet_node_t node, uintptr_t addr, size_t len);
{
	uintptr_t		end 
	unsigned int		num_pinned;
	firehose_request_t	*req = NULL;

	end = addr + (uintptr_t) nbytes - 1;

	FH_TABLE_LOCK;

	if (fhi_ispinned_region(node, addr, len, end)) {
		req = fh_request_new();
		FH_FILL_REQUEST(req, node, addr, len);
		num_pinned = fh_acquire_remote_region(node, 
		    req->start, req->len, req->end);
	}

	FH_TABLE_UNLOCK;

	return req;
}

extern int
firehose_release(firehose_request_t *local, firehose_request_t *remote)
{
	if (local != NULL)
		fh_release_local_region(local->start, local->len, local->end);

	if (remote != NULL)
		fh_release_remote_region(remote->node, remote->start, 
		    remote->len, remote->end);

	return 0;
}

extern int
firehose_release_local(firehose_request_t *local)
{
	if (local != NULL)
		fh_release_local_region(local->start, local->len, local->end);

	return 0;
}

extern int
firehose_release_remote(firehose_request_t *req)
{
	if (remote != NULL)
		fh_release_remote_region(remote->node, remote->start, 
		    remote->len, remote->end);
	return 0;
}

/* ######################################################################### */
/* GENERAL interface - region/page must provide implementations of these
 * functions */

/* ####################### */
/* LOCAL OPERATIONS */
/* fh_pin_local_request(req)
 * GENERAL
 *
 * Pins a region of memory according to contents of firehose_request_t.  Field
 * 'start' must be aligned to a page address and 'start+length' must cover a
 * multiple of GASNETI_PAGE_SIZE.
 *
 * Fields may be modified to meet the requirements of the firehose algorithm.
 * On firehose-page, the internal pointer is set to a descriptor representing
 * the first page of the region (it is essentially caches a hash lookup).
 *
 * Function calls: fhi_create_key(key) to attach a key to the internal pointer
 *                 fh_acquire_local_region(start, length, end) to acquire the
 *                         underlying region (pin/increment refcounts)
 *
 * The function returns the amount of pages already pinned.
 */
int
fh_pin_local_request(firehose_request_t *req)
{
	int pinned;

	req->internal = fhi_create_key(fhi_key_req(req));
	pinned = fh_acquire_local_region(req->start, req->len, req->end);
	return pinned;
}

/* fh_unpin_local_request(req)
 * GENERAL
 *
 * Unpins a region of memory according to contents of firehose_request_t.
 * Field 'start' must be aligned to a page address and 'start+length' must
 * cover a multiple of GASNETI_PAGE_SIZE.
 */
int	
fh_unpin_local_request(firehose_request_t *req)
{
	fh_release_local_region(addr, len, end);

	return 0;
}

/* fh_acquire_local_region(addr, len, end)
 * GENERAL
 *
 * Pins/increments pages covered in [addr,addr+len] and returns the amount of
 * pages already pinned.
 *
 * Attempts to coalesce pin calls
 *
 * Function calls: firehose_local_pin(addr,len) for unpinned regions
 *                 fhi_bucket_acquire(bucket_addr) over all buckets.
 */

int
fh_acquire_local_region(uintptr_t addr, size_t len, uintptr_t end)
{
	uintptr_t	bucket_addr;
	int		numpinned = 0;

	firehose_request_t	req;

	FH_TABLE_LOCK;

	req.start = addr;
	req.len = 0;
 	FH_FOREACH_BUCKET(addr, end, bucket_addr) {
		if (fhi_bucket_ispinned(gasnet_mynode, bucket_addr)) {
			fhi_bucket_acquire(gasnet_mynode, bucket_addr);
			numpinned++;
			if (req.len > 0) {
				req.end = req.start + req.len - 1;
				firehose_pin(&req);
				req.start = req.end + 1;
				req.len = 0;
			}
			else
				req.start += FH_BUCKET_SIZE;
		}
		else {
			req.len += FH_BUCKET_SIZE;
		}
	}

	if (req.len > 0) {
		req.end = req.start + req.len - 1;
		firehose_pin(&req);
	}

	FH_TABLE_UNLOCK;

	return numpinned;
}

#define FH_LOCAL_FIFO_FULL(buckets)

/* fh_release_local_region(addr, len, end)
 *
 * Decrements/unpins pages covered in [addr,addr+len]
 */
int
fh_release_local_region(uintptr_t addr, size_t len, uintptr_t end)
{
	uintptr_t	bucket_addr;

	firehose_request_t	req;

	FH_TABLE_LOCK;

	req.start = addr;
	req.len = 0;

	FH_FOREACH_BUCKET(addr, end, bucket_addr) {
		if (fhi_bucket_release(gasnet_mynode, bucket_addr) != 0) {
			if (req.len > 0) {
				req.end = req.start + req.len - 1;
				firehose_unpin(&req);
				req.start = req.end + 1;
				req.len = 0;
			}
			else
				req.start += FH_BUCKET_SIZE;
		}
		else {
			/* if the local fifo is not full, add the bucket */
			if (!FH_LOCAL_FIFO_FULL()) {
				assert(req.len == 0);
				fh_fifo_add(gasnet_mynode, bucket_addr)
			else
				req.len += FH_BUCKET_SIZE;
		}
	}

	if (req.len > 0) {
		req.end = req.start + req.len - 1;
		firehose_unpin(&req);
	}

	FH_TABLE_UNLOCK;

	return 0;
}

/* fh_acquire_local_region_list(region_list, list_length)
 * GENERAL
 *
 * This function allows for a list of regions (region descriptors) to be
 * acquired.  Upon returning, the function guarentees that every region could
 * be acquired.
 */
void
fh_acquire_local_region_list(firehose_region_t *region, size_t num) 
{
	int		i;
	uintptr_t	addr;
	uintptr_t	len;

	#ifdef FIREHOSE_PAGE
		addr = region->addr;
		len  = GASNETI_PAGE_SIZE;
	#elif defined(FIREHOSE_REGION)
		addr = region->addr;
		len  = region->len;
	#endif

	/* XXX Should try coalescing the regions among themselves */
	for (i = 0; i < num; i++) {
		fh_acquire_local_region(addr, len, addr+len-1);
	}
	return;
}
	
/* fh_release_region_list(node region_list, list_length)
 * GENERAL
 *
 * This function allows for a list of regions (region descriptors) to be
 * released.  Upon returning, the function guarentees that every region could
 * be released.
 */
int
fh_release_region_list(gasnet_node_t node, firehose_region_t *region, size_t num) 
{
	int		i;
	uintptr_t	addr;
	uintptr_t	len;

	#ifdef FIREHOSE_PAGE
		addr = region->addr;
		len  = GASNETI_PAGE_SIZE;
	#elif defined(FIREHOSE_REGION)
		addr = region->addr;
		len  = region->len;
	#endif

	/* XXX Should try coalescing the list */
	for (i = 0; i < num; i++) {
		fh_release_region(node, addr, len, addr+len-1);
	}
	return;
}

/* ####################### */
/* REMOTE OPERATIONS       */
/* Pins a region of memory according to contents of firehose_request_t.  Field
 * 'start' must be aligned to a page address and 'start+length' must cover a
 * multiple of GASNETI_PAGE_SIZE.
 *
 * The algorithm only requests a remote pin operation if one of the pages
 * covered in the region is not known to be pinned on the remote host.  Unless
 * the entire region hits the remote firehose hash, the value of the internal
 * pointer is set to FH_REQ_UNPINNED and a request for remote pages to be
 * pinned is enqueued.
 *
 * The function returns the amount of pages already pinned.
 *
 * Detailed behaviour:
 *  - The entire region to be pinned is scanned on a per-page basis.
 *  - If the page is pinned, its reference count is incremented.  If the page
 *    is not pinned, the 'non-pinned' reference count is incremented.
 *  - If the 'non-pinned' refcount is greater than zero, an amount of memory is
 *    alloc'd (alloca) to be large enough to fit twice the amount of
 *    'non-pinned' reference counts (alloca'd as a firehose_region_t).
 *  - The list is scanned again and the region to be pinned and an equivalent
 *    replacement region is inserted into the alloca array.
 *
 *  NOTE: This entire process has to be done while holding the firehose lock.
 */

#define FH_REQ_INFLIGHT	((firehose_private_t *) 1)

int
fh_remote_pin_request(firehose_request_t *req)
{
 	uintptr_t	bucket_addr;
	int		notpinned = 0;

	firehose_region_t	*reg_alloc;

	FH_TABLE_LOCK;

 	FH_FOREACH_BUCKET(req->start, req->end, bucket_addr) {
		if (fhi_bucket_ispinned(req->node, bucket_addr))
			fhi_bucket_acquire(req->node, bucket_addr);
		else
			notpinned++;
		}
	}

	if (notpinned > 0) {
		reg_alloc = alloca(sizeof(firehose_region_t) * 
				(size_t) (1.5 * notpinned));
	}

	FH_TABLE_UNLOCK;
}

/* ####################### */
/* FIREHOSE TABLE QUERIES  */
/* fhi_ispinned_region(node, addr, len, end)
 * INTERNAL
 * 
 * Returns non-null if the entire region is already pinned 
 */
int
fhi_ispinned_region(gasnet_node_t node, uintptr_t addr, size_t len, uintptr_t end)
{
 	uintptr_t	bucket_addr;

 	FH_FOREACH_BUCKET(addr, end, bucket_addr) {
		if (!fhi_bucket_ispinned(node, bucket_addr)) {
			return 0;
		}
	}
	return 1;
}

/* fhi_replace_regions(node, regions, num_regions)
 * INTERNAL
 *
 * This function uses space in the 'regions' array to replace 'num_regions'
 * firehose regions on the remote host.  
 *
 * Each node can own up to 'fh_buckets_per_node' and 'fh_regions_per_node' on
 * every other node.  If either of these values is set to zero, there is no
 * established maximum on the number of buckets and the number of regions that
 * can be owned.
 *
 * Each node keeps a running count of the number of buckets and regions it owns
 * on every other node (each other node is an entry in the job-wide array).  If
 * the per-node count is below the 'fh_*_per_node' count, a firehose can be
 * mapped to a new region without replacements.  In every other case, an
 * 'inactive' region of equal or greater amount of buckets must be traded for
 * the new region to be remotely pinned.
 *
 * The election process amongst buckets/regions to be replaced is done on a
 * per-node FIFO basis.  Once a region/buckets reaches a reference count of 0,
 * it is appended to the per-node victim FIFO.
 *
 * Function returns the number of _replacement_ regions.
 */

static int	 fh_buckets_per_node;
static int	*fh_buckets_count;

static int	 fh_regions_per_node;
static int	*fh_regions_count;

#ifdef FIREHOSE_PAGE
#define FH_REGION_SIZE(reg)	GASNETI_PAGE_SIZE
#define FH_REGION_BUCKETS(reg)	1
#elif defined(FIREHOSE_REGION)
#define FH_REGION_SIZE(reg)	((reg)->len)
#define 
#endif


/* Firehose-page has no limitations on the number of regions */
int
fhi_replace_regions(gasnet_node_t node, firehose_region_t *reg, size_t num)
{
	int	i,j;
	int	num_buckets;

	firehose_region_t	old_reg;

	for (i = 0, j = num; i < num; i++) {
		/* Replace based on regions limitations -- since a region also
		 * contains buckets, finding regions may also free enough */
		if (fh_regions_per_node && 
		    fh_regions_count[node] > fh_regions_per_node) {
			fhi_find_region_fifo(gasnet_node_t node, 
			    &old_reg, FH_REGION_SIZE(reg));
		}
		else
			fh_regions_count[node]++;

		/* Replace based on buckets limitations */
		if (fh_buckets_per_node && 
		    fh_buckets_count[node] > fh_buckets_per_node) {
			fhi_find_bucket_fifo(&old_reg);
		}
		else
			fh_buckets_count[node]++;

	num_buckets = 
	
}

/* ######################################################################### */
/* firehose-page internal functions */

/* fhi_bucket_ispinned(node, bucket_addr)
 * INTERNAL
 *
 * Returns non-null if the page is pinned in memory.  
 */

/* fhi_bucket_acquire(node, bucket_addr)
 * INTERNAL
 *
 * Increments the reference count of the bucket at address 'bucket_addr'.  Note
 * that the page must already be pinned
 *
 * This function returns the new reference count
 */

/* fhi_bucket_release(node, bucket_addr)
 * INTERNAL
 *
 * Decrements the reference count of the bucket at address 'bucket_addr'.  Note
 * that the page must already be pinned and the reference count non-zero.
 *
 * This function returns the new reference count
 */

#define fhi_key_req(req)	((fh_int_t) ((req)->node | (req)->start))
#define fhi_key_make(addr,node)	((fh_int_t) (addr) | (node))
#define fhi_key_addr(key)	((uintptr_t) ((key) & ~FH_BUCKET_MASK)
#define fhi_key_node(key)	((gasnet_node_t) ((key) & FH_BUCKET_MASK)

/* fhi_acquire_key(fh_int_t key)
 * INTERNAL
 *
 * This function acquires the key, which amounts to adding it to the hash table
 * if it is inexistant, and subsequently increments its reference count.
 *
 * The function returns the new reference count
 */

/* fhi_release_key(fh_int_t key)
 * INTERNAL
 *
 * In firehose-page, each page has a key entry in the hash table.  This
 * function releases the key, which essentially amounts to decrementing the
 * reference count associated to the resource. 
 * 
 * If the decrementing of the reference count is non-zero, the key is simply
 * left in the hash table.
 *
 * If the reference reaches zero and the length of the victim FIFO is under its
 * threshold, the key is left in the hash table and appended to the victim
 * FIFO.  If the FIFO is too long, the key is removed from the hash table.
 *
 * The function returns the new reference count
 */

/* fh_fifo_add(node, bucket)
 * INTERNAL
 * 
 * This function adds the bucket descriptor to the front of the FIFO list.
 * This function is stateless - it is up to the implementor to figure out if
 * the FIFO length has exceeded the threshold.
 */
void fh_fifo_add(gasnet_node_t node, uintptr_t bucket_addr);

/* fh_fifo_remove(node, bucket)
 * INTERNAL
 * This function removes the bucket descriptor from the FIFO list.
 */


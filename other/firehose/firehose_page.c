#ifdef FIREHOSE_PAGE

/* Counters. . what type of counts and when they are incremented
 *
 *
 * fhc_BucketsAvail - decrementing counter
 *     Available amount of buckets that can be pinned without
 *     removing/unpinning buckets from the bucket FIFO.
 *
 * fhc_LocalBucketsFifoAvail - decrementing counter
 *     Available amount of buckets that can be appended to the local bucket
 *     FIFO.
 *
 * fhc_RemoteBucketsFifoAvail[0..nodes-1] - Array of decrementing counters
 *     Available amount of remote buckets that can be used without sending
 *     replacement buckets.
 *
 * fhc_pages_overflow - incrementing counter
 *     Amount of local pinned pages currently being stolen to other nodes.
 *
 */


/* fh_region_ispinned(node, addr, len)
 * INTERNAL
 * 
 * Returns non-null if the entire region is already pinned 
 *
 * PAGE:
 *   Uses fh_bucket_ispinned() to query if the current page is pinned.
 */
int
fh_region_ispinned(gasnet_node_t node, uintptr_t addr, size_t len)
{
 	uintptr_t	bucket_addr;
	uintptr_t	end_addr = addr + len - 1;
	fh_bucket_t	*bd;

 	FH_FOREACH_BUCKET(addr, end_addr, bucket_addr) {
		if (fh_bucket_lookup(node, bucket_addr) == NULL)
			return 0;
	}
	return 1;
}

/* fh_acquire_local_region(region)
 *
 * In acquiring local pages covered over the region, pin calls are coalesced.
 * Acquiring a page may lead to a pin call but always results in the page
 * reference count being incremented.
 *
 * Under firehose-page, acquiring means finding bucket descriptors for each
 * bucket in the region and incrementing the bucket descriptor's reference
 * count.
 *
 * The function always returns NULL, as there is there is no need for
 * request_t's to cache private_t pointers 
 *
 * XXX An optimization would be to cache the first bucket descriptor.
 *
 * Called by:
 *    fh_move_handler (AM Handler)
 *    fh_local_pin() (firehose_local_pin, firehose_local_try_pin)
 *
 */

firehose_private_t *
fh_acquire_local_region(firehose_region_t *region)
{
	uintptr_t		bucket_addr, end_addr;
	firehose_region_t	reg;
	fh_bucket_t		*bd;
	FH_NUMPINNED_DECL;

	reg.addr = region->addr;
	reg.len = 0;

	end_addr = reg.addr + (uintptr_t) region->len - 1;

 	FH_FOREACH_BUCKET(region->addr, end_addr, bucket_addr) {
		bd = fh_bucket_lookup(gasnet_mynode(), bucket_addr);

		if (bd != NULL) {
			fhi_bucket_acquire(bd);
			FH_NUMPINNED_INC;

			if (reg.len > 0) {
				firehose_pin(&reg);
				reg.addr += reg.len;
				reg.len = 0;
			}
			else
				reg.addr += FH_BUCKET_SIZE;
		}
		else {
			reg.len += FH_BUCKET_SIZE;
		}
	}

	if (reg.len > 0)
		firehose_pin(&reg);

	FH_NUMPINNED_TRACE_LOCAL;

	return NULL;
}

/* 
 * fh_release_local_region(request)
 *
 * Decrements/unpins pages covered in 
 *     [request->addr, request->addr+request->len].
 *
 * The algorithm for releasing a region is the following:
 *
 * PAGE:
 *   1. Loop over all buckets in region
 *         decrement refcount
 *         if refcount == 0
 *            increment local_victim_count
 *            add fh_bucket_t * to fh_bucket_temp
 *
 *   2. if (local_victim_count > fhc_PagesFifoAvail)
 *         Remove (local_victim_count-fhc_PagesFifoAvail) from the tail of the
 *         fifo queue and unpin.
 *   3. if (local_victim_count > 0)
 *         Push fh_bucket_t in the local victim FIFO in the reverse order
 *
 *   NOTE: If a call to this function is made as part of many regions to
 *         be released, it is up to the caller to see if the array of regions
 *         is not an array of consequentive pages.  In that case, the region_t
 *         should be adjusted to reflect consequentive pages to be released.
 *         
 */

void
fh_release_local_region(firehose_request_t *request)
{
	uintptr_t	bucket_addr;
	uintptr_t	end_addr = request->addr + (uintptr_t) request->len - 1;
	int		vcount = 0;
	fh_bucket_t	*bd;

	FH_TABLE_ASSERT_LOCKED;

	FH_FOREACH_BUCKET(addr, end_addr, bucket_addr) {
		bd = fh_bucket_lookup(gasnet_mynode(), bucket_addr);

		if (fhi_bucket_release(bd) == 0) {
			fh_bucket_temp[vcount] = bd;
			vcount++;
		}
	}

	/* vcount holds the number of buckets which have reached a reference
	 * count of zero */
	if (vcount > 0) {
		int	i;
		if (vcount > fhc_PagesFifoAvail)
			fhi_fifo_unpin(fhc_PagesFifoAvail-vcount);

		for (i = vcount-1 ; i >= 0; i--)
			fhi_fifo_add(fh_bucket_temp[i]);

		/* XXX should keep stats based on vcount */
	}

	return;
}

/* ##################################################################### */
/* REMOTE PINNING                                                        */
/* ##################################################################### */

/*
 * fh_acquire_remote_region(node, region, callback, context)
 *
 * Helpers:
 *         fhi_coalesce_new_buckets(buckets, num, region_array)
 *         fhi_find_old_buckets(node, num_buckets, region_array)
 * 
 * fhi_coalesce_new_buckets(buckets, num, regions)
 *
 * Helper function to coalesce contiguous buckets into the regions array.
 *
 * The function loops over the bucket descriptors in the 'buckets' array in the
 * hopes of creating the smallest amount of region_t in the 'regions' array.
 * This is made possible by coalescing buckets found to be contiguous in memory
 * by looking at the previous bucekt descriptors in the 'buckets' array.
 *
 * It is probably not worth our time making the coalescing process smarter by
 * searching through the whole 'buckets' array each time.
 *
 */
int
fhi_coalesce_new_buckets(fh_bucket_t **buckets, size_t num_buckets,
		firehose_region_t *regions)
{
	int		i, b = 0; /* new buckets created */
	fh_bucket_t	*bd;
	uintptr_t	addr_next = 0;

	/* Coalesce consequentive pages into a single request_t */
	for (i = 0; i < num_buckets; i++) {
		bd = buckets[i];
		if (i > 0 && fhi_paddr(bd) == addr_next)
			regions[(b-1)].len += FH_BUCKET_SIZE;
		else {
			b++;
			regions[b].addr = fhi_paddr(bd);
			regions[b].len  = fhi_len(bd);
		}

		addr_next = fhi_paddr(bd) + FH_BUCKET_SIZE;
	}

	return b;
}
	
/*
 * Helper function to coalesce consequentive buckets
 *
 * Returns the amount of regions in 'regions'
 *
 * This function 
 * The function uses 'num_buckets' as input, meaning 'num_buckets' new
 * firehoses will be mapped to remote buckets.  This function can adjust the
 * amount of old_buckets that may be required based on the amount of buckets
 * the node currently owns to the remote node.
 */
int
fhi_find_old_buckets(gasnet_node_t node, size_t num_buckets, 
		    firehose_region_t *regions)
{
	int		i, b = 0;
	fh_bucket_t	*bd;
	uintptr_t	addr_next = 0;

	assert(num_buckets > 0);
	/* Make sure the FIFO is not empty */
	assert(!TAILQ_EMPTY(&fh_firehose_fifo[node]));

	for (i = 0; i < num_buckets; i++) {
		/* XXX need a macro to remove entries only if refcount is 0 */
		bd = TAILQ_LAST(&fh_firehose_fifo[node], entry);

		/* We may be able to coalesce two buckets */
		if (i > 0 && fhi_paddr(bd) == addr_next)
			regions[(b-1)].len += FH_BUCKET_SIZE;
		else {
			b++;
			regions[b].addr = fhi_paddr(bd);
			regions[b].len  = fhi_len(bd);
		}

		addr_next = fhi_paddr(bd) + FH_BUCKET_SIZE;
		TAILQ_REMOVE(&fh_firehose_fifo[node], bd, entry);
		fhi_bucket_remove(bd);
	}

	return b;
}

/* 
 * fh_acquire_remote_region(node, region, callback, context)
 *
 * The function only requests a remote pin operation (AM) if one of the pages
 * covered in the region is not known to be pinned on the remote host.  Unless
 * the entire region hits the remote firehose hash, the value of the internal
 * pointer is set to FH_REQ_UNPINNED and a request for remote pages to be
 * pinned is enqueued.
 *
 * PAGE:
 *   1. Loop over all buckets in region
 *         If bucket is pinned, its reference count is incremented
 *         Else 
 *            Add the bucket descriptor to the temp bucket array
 *            increment the count of required region_t if applicable (new_r)
 *   2. If a remote bucket needs to be pinned
 *         a) Find the amount of replacement buckets required (replace_r)
 *         b) Allocate region array on the stack based on replace_r and new_r
 *         c) Coalesce new regions into a the region array
 *         d) If replace_r > 0.
 *         e) Call firehose_move and return with FH_REGION_UNPINNED.
 *   3. Return with a pointer to the first bucket descriptor
 *
 *   Called by:
 *      - firehose_remote_pin() ONLY.
 *   Calls:
 *      - fhi_bucket_acquire(), fh_bucket_lookup()
 *      - fhi_coalesce_new_buckets() to coalesce a list of bucket descriptors
 *                                   into the stack-based region array.
 *      - fhi_find_old_buckets() to find replacement buckets
 *      - fh_am_move() with the stack-based region array if firehose movement
 *                     is required.
 *      
 * REGION: ???
 */

firehose_private_t *
fh_acquire_remote_region(gasnet_node_t node, firehose_region_t *reg, 
		         firehose_completed_fn_t callback, void *context)
{
 	uintptr_t	bucket_addr, end_addr, next_addr = 0;
	int		notpinned = 0, new_r = 0;

	end_addr = reg->addr + (uintptr_t) reg->len - 1;

	FH_TABLE_LOCK;

 	FH_FOREACH_BUCKET(reg->addr, end_addr, bucket_addr) {
		bd = fh_bucket_lookup(node, bucket_addr);

		if (bd != NULL) {
			fh_bucket_acquire(bd);
		else {
			fh_bucket_temp[notpinned] = bd;
			if (addr_next != bucket_addr)
				new_r++;

			addr_next = bucket_addr + FH_BUCKET_SIZE;
			notpinned++;
		}
	}

	if (notpinned > 0) {
		int			i, old_r, replace_r;
		firehose_region_t	*reg_alloc;

		if (fhc_RemoteBucketsFifoAvail[node] >= notpinned) {
			rep_r = 0;
			old_r = 0;
			fhc_RemoteBucketsFifoAvail[node] -= notpinned;
		}
		else {
			rep_r = notpinned - fhc_RemoteBucketsFifoAvail[node];
			fhc_RemoteBucketsFifoAvail[node] = 0;
		}

		/* We've calculated 'new_r' regions will be sufficient for the
		 * replacement buckets and estimate a worst-case of 'replace_r'
		 * will be required for replacement firehoses 
		 * XXX should keep stats on the size of the alloca */
		reg_alloc = 
			alloca(sizeof(firehose_region_t) * (new_r+replace_r));

		/* Coalesce new buckets into a minimal amount of regions */
		new_r = fhi_coalesce_new_buckets(fh_bucket_temp, notpinned,
				reg_alloc);

		/* Find replacement buckets if required */
		if (rep_r > 0)
			old_r = fhi_find_old_buckets(node, rep_r,
					&reg_alloc[new_r]);
		FH_TABLE_UNLOCK;

		fh_am_move(reg_alloc, new_r, old_r, callback, context);

		return FH_REGION_UNPINNED;
	}
	else {
		FH_TABLE_UNLOCK;
		return fh_bucket_lookup(node, reg->addr);
	}
}

/*
 * fh_release_remote_region(request)
 *
 * This function releases every page in the region described in the firehose
 * request type.
 *
 * PAGE:
 *   1. Loop over each bucket in reverse order
 *      If the reference count reaches zero, push the descriptor at the head of
 *      the victim FIFO
 */

void
fh_release_remote_region(firehose_request_t *request)
{
	int		i;
	uintptr_t	end_addr, bucket_addr;
	fh_bucket_t	*bd;

	end_addr = request->addr + request->len - 1;

	/* Process region in reverse order so regions can be later coalesced in
	 * the proper order (lower to higher address) from the FIFO */
	FH_FOREACH_BUCKET_REV(request->addr, end_addr, bucket_addr) {
		bd = fh_bucket_lookup(request->node, bucket_addr);

		if (fhi_bucket_release(bd) == 0)
			fhi_fifo_push(request->node, bd);
	}

	return;
}

#endif

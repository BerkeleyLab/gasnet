#include <firehose.h>
#include <firehose_internal.h>
#include <gasnet.h>

#ifdef FIREHOSE_PAGE
typedef firehose_private_t fh_bucket_t;


/* 
 * There is currently no support for bind callbacks in firehose-page 
 * as no client_t is currently envisioned in known -page clients. 
 */

#if defined(FIREHOSE_BIND_CALLBACK) || defined(FIREHOSE_UNBIND_CALLBACK)
  #error firehose-page currently has no support for bind/unbind callbacks
#endif

/* 
 * For b_num buckets to be pinned and a function that uses coalescing to create
 * regions to be pinned, a worst case number of buckets is calculated as
 * follows:
 *
 * For each region (contiguous buckets), a worst case of (buckets+1)/2 new
 * region_t's are required to hold unpinned buckets.
 *    
 * For example, new_r = { regA, regB } and new_num = 2.
 *
 * regA = 5 buckets, 3 unpinned of the form 10101
 * regB = 3 buckets, 2 unpinned of the form 101
 *
 * yields a calulation of (5+1)/2 + (3+1)/2 = 3 + 2  = 5
 *
 * Another example, new_r = { regA, regB, regC } and new_num = 3.
 *
 * regA = 1 unpinned bucket
 * regB = 1 unpinned bucket
 * regC = 1 unpinned bucket
 *
 * yields a calculation of 3 * (1+1)/2 = 3 * 1 = 3
 *
 */

#define FH_MIN_REGIONS_FOR_BUCKETS(buckets)	(((buckets)+1)>>1)
#define FH_MAX_BUCKETS_FOR_REGIONS(regions)	(((regions)<<1)-1)

/* ##################################################################### */
/* PAGE-SPECIFIC FUNCTIONS                                               */
/* ##################################################################### */

/*
 * Local region handling
 *
 * Local regions can either be acquired by a local pin request or from an AM
 * handler as part of a remote firehose pin request.  All three of the
 * functions below are called from both AM handler context and local pin
 * request:
 *
 * 1. AcquireLocalRegionsList(): Given a list of input regions to pin, an
 *    unpinned list of regions is returned by way of a RegionPool.
 *      * called by fh_acquire_local_region() 
 *      * called by fh_am_move_reqh()
 *
 * 2. ReleaseLocalRegionsList(): Given a list of input regions to unpin,
 *    buckets reaching a refcount of zero are appended to (and possibly
 *    overcommit) the local victim FIFO.
 *      * called by fh_release_local_region() 
 *      * called by fh_am_move_reqh()
 */

int	fhi_AcquireLocalRegionsList(int local_ref, 
		firehose_region_t *region, size_t reg_num, 
		fhi_RegionPool_t *rpool);

void	fhi_ReleaseLocalRegionsList(int local_ref, firehose_region_t *reg, 
				size_t reg_num);


/* 
 * Remote region handling
 *
 * TryAcquireRemoteRegion builds a list of regions that are not pinned.
 *
 * CoalesceBuckets is a utility to minimize the number of regions required to
 * describe a list of buckets (contiguous buckets can be described by a single
 * region).
 *
 */
int	fhi_TryAcquireRemoteRegion(firehose_request_t *req, 
			fh_completion_callback_t *ccb,
			int *new_regions);

int	fhi_CoalesceBuckets(uintptr_t *bucket_addr_list, size_t num_buckets,
			    firehose_region_t *regions);

/* ##################################################################### */
/* BUFFERS AND QUEUES                                                    */
/* ##################################################################### */

/* 
 * GLOBAL SCRATCH BUFFERS
 *
 * The following two scratch buffers are used as temporary arrays to store both
 * bucket addresses and firehose bucket entry pointers (they are allocated at
 * startup).
 *
 * fh_temp_buckets[] is used to store bucket addresses between the
 * TryAcquireRemoteRegion() and CoalesceBuckets() phases of a remote pin
 * request.
 *
 * fh_temp_bucket_ptrs[] is used exclusively by FlushPendingRequests() as part
 * of a two phase operation in first marking pending buckets as non-pending and
 * then processing requests that were attached to these pending buckets.
 */
static int		fh_max_regions = 0;
static uintptr_t *	fh_temp_buckets = NULL;
static fh_bucket_t **	fh_temp_bucket_ptrs = NULL;

/*
 * The bucket table
 */
fh_hash_t	*fh_BucketTable;

/* ##################################################################### */
/* UTILITY FUNCTIONS FOR REGIONS AND BUCKETS                             */
/* ##################################################################### */

static fh_bucket_t      *fh_buckets_bufs[FH_BUCKETS_BUFS] = { 0 };
static fh_bucket_t	*fh_buckets_freehead = NULL;
static int		 fh_buckets_bufidx = 0;
static int		 fh_buckets_per_alloc = 0;

static void
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

GASNET_INLINE_MODIFIER(fh_bucket_lookup)
fh_bucket_t *
fh_bucket_lookup(gasnet_node_t node, uintptr_t bucket_addr)
{
	fh_bucket_t *entry;
	fh_int_t key;

	FH_TABLE_ASSERT_LOCKED;

	FH_ASSERT_BUCKET_ADDR(bucket_addr);

	return (fh_bucket_t *)
			fh_hash_find(fh_BucketTable,
				     FH_KEYMAKE(bucket_addr, node));
}

static fh_bucket_t *
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
	gasneti_assert(entry ==
		(fh_bucket_t *)fh_hash_find(fh_BucketTable, entry->fh_key));

	return entry;
}

static void
fh_bucket_remove(fh_bucket_t *bucket)
{
	void *entry;

	FH_TABLE_ASSERT_LOCKED;

	entry = fh_hash_insert(fh_BucketTable, bucket->fh_key, NULL);
	gasneti_assert(entry == (void *)bucket);

	memset(bucket, 0, sizeof(fh_bucket_t));
	bucket->fh_next = fh_buckets_freehead;
	fh_buckets_freehead = bucket;
}

/* fh_region_ispinned(node, addr, len)
 * 
 * Returns non-null if the entire region is already pinned 
 *
 * Uses fh_bucket_lookup() to query if the current page is pinned.
 */
int
fh_region_ispinned(gasnet_node_t node, uintptr_t addr, size_t len)
{
 	uintptr_t	bucket_addr;
	uintptr_t	end_addr = addr + len - 1;
	fh_bucket_t	*bd;
	int		is_local = (node == fh_mynode);

	FH_TABLE_ASSERT_LOCKED;
 	FH_FOREACH_BUCKET(addr, end_addr, bucket_addr) {
		bd = fh_bucket_lookup(node, bucket_addr);

		/* 
		 * Upon lookup, the bucket can either not be present in the
		 * hash table in which case it is certainly unpinned, or it
		 * can be in the table but be pending.  If the bucket is
		 * pending a firehose move, the region cannot be declared as
		 * pinned.
		 */
		if (bd == NULL || (!is_local && FH_IS_REMOTE_PENDING(bd)))
			return 0;
	}
	return 1;
}

/* fh_region_partial(node, addr_p, len_p)
 *
 * Search for first range of pinned pages in the given range
 * and if succesful, overwrite the region with the pinned range.
 *
 * Returns non-zero if any pinned pages were found.
 */
int
fh_region_partial(gasnet_node_t node, uintptr_t *addr_p, size_t *len_p)
{
	uintptr_t	tmp_addr = 0;
	uintptr_t	addr, end_addr, bucket_addr;
	size_t		len;
	fh_bucket_t	*bd;
	int		is_local = (node == fh_mynode);

	addr     = *addr_p;
	len      = *len_p;
	end_addr = addr + len - 1;

	FH_TABLE_ASSERT_LOCKED;
        FH_FOREACH_BUCKET(addr, end_addr, bucket_addr) {
                bd = fh_bucket_lookup(node, bucket_addr);
		if ((bd != NULL) &&
		    (is_local || !FH_IS_REMOTE_PENDING(bd))) {
			/* found first pinned page */
			tmp_addr = bucket_addr;
			break;
		}
	}
	addr = tmp_addr;

	if_pf (tmp_addr == 0) {
		/* No pinned pages found in the requested region */
		return 0;
	}

	/* Search remainder of the interval, limiting the resulting length */
	len  = (end_addr - tmp_addr) + 1;	/* bytes remaining */
	if (is_local) {
		len = MIN(len, fhc_MaxVictimBuckets << FH_BUCKET_SHIFT);
	} else {
		len = MIN(len, fhc_MaxRemoteBuckets << FH_BUCKET_SHIFT);
	}
	end_addr = tmp_addr + (len - 1);

	tmp_addr += FH_BUCKET_SIZE;	/* first page known pinned */
	if_pt (tmp_addr != 0) { /* guards against wrap around */
       		FH_FOREACH_BUCKET(tmp_addr, end_addr, bucket_addr) {
               		bd = fh_bucket_lookup(node, bucket_addr);
			if ((bd == NULL) ||
			    (!is_local && FH_IS_REMOTE_PENDING(bd))) {
				/* found an unpinned page */
				len = bucket_addr - addr;
				break;
			}
		}
	}

	*addr_p = addr;
	*len_p  = len;
		
	return 1;
}

/* fh_init_plugin()
 *
 * This function is only called from firehose_init and allows -page OR -region
 * to run plugin specific code.
 */

void
fh_init_plugin(uintptr_t max_pinnable_memory, size_t max_regions, 
	      const firehose_region_t *regions, size_t num_reg,
	      firehose_info_t *fhinfo)
{
	int		i;
	unsigned long	M, maxvictim, firehoses, m_prepinned = 0;
	size_t		b_prepinned = 0;

        /* Initialize the Bucket table to 128k lists */
	fh_BucketTable = fh_hash_create((1<<17));

	/* 
	 * In -page, we ignore regions. . there should not be a limit on the
	 * number of regions 
	 */ 

	if (max_regions != 0)
		gasneti_fatalerror("firehose-page does not support a "
				   "limitation on the number of regions");
	/*
	 * Prepin optimization: PHASE 1.
	 *
	 * Count the number of buckets that are set as prepinned.
	 *
	 */
	if (num_reg > 0) {
		int		i;

		for (i = 0; i < num_reg; i++) {
			gasneti_assert(regions[i].addr % FH_BUCKET_SIZE == 0);
			gasneti_assert(regions[i].len % FH_BUCKET_SIZE == 0);
			b_prepinned +=
				FH_NUM_BUCKETS(regions[i].addr,regions[i].len);

		}
	}

	M           = fh_getenv("GASNET_FIREHOSE_M", (1<<20));
	m_prepinned = FH_BUCKET_SIZE * b_prepinned;
	maxvictim   = fh_getenv("GASNET_FIREHOSE_MAXVICTIM_M", (1<<20));

	GASNETI_TRACE_PRINTF(C, 
	    ("Firehose M=%ld, MAXVICTIM_M=%ld", M, maxvictim));

	/* First assign values based on either what the user passed or what is
	 * determined to be the best M and maxvictim parameters based on
	 * max_pinnable_memory and FH_MAXVICTIM_TO_PHYSMEM_RATIO */

	if (M == 0 && maxvictim == 0) {
		M         = (unsigned long) max_pinnable_memory *
				(1-FH_MAXVICTIM_TO_PHYSMEM_RATIO);
		maxvictim = (unsigned long) max_pinnable_memory *
				    FH_MAXVICTIM_TO_PHYSMEM_RATIO;
	}
	else if (M == 0)
		M = max_pinnable_memory - maxvictim;
	else if (maxvictim == 0)
		maxvictim = max_pinnable_memory - M;

	/* 
	 * Validate firehose parameters parameters 
	 */ 
	{
		unsigned long	M_min = FH_BUCKET_SIZE * gasnet_nodes() * 1024;
		unsigned long	maxvictim_min = FH_BUCKET_SIZE * 4096;

		if_pf (M < M_min)
			gasneti_fatalerror("GASNET_FIREHOSE_M is less"
			    "than the minimum %lu (%lu buckets)", M_min, 
			    M_min >> FH_BUCKET_SHIFT);

		if_pf (maxvictim < maxvictim_min)
			gasneti_fatalerror("GASNET_MAXVICTIM_M is less than the "
			    "minimum %lu (%lu buckets)", maxvictim_min,
			    maxvictim_min >> FH_BUCKET_SHIFT);

		if_pf (M - m_prepinned < M_min)
			gasneti_fatalerror("Too many buckets passed on initial"
			    " pinned bucket list (%d) for current "
			    "GASNET_FIREHOSE_M parameter (%lu)", 
			    (int) b_prepinned, M);
	}

	/* 
	 * Set local parameters
	 */
	fhc_LocalOnlyBucketsPinned = b_prepinned;
	fhc_LocalVictimFifoBuckets = 0;
	fhc_MaxVictimBuckets = (maxvictim + m_prepinned) >> FH_BUCKET_SHIFT;

	/* 
	 * Set remote parameters
	 */
	firehoses = (M - m_prepinned) >> FH_BUCKET_SHIFT;
	fhc_RemoteBucketsM = gasnet_nodes() > 1
				? firehoses / (gasnet_nodes()-1)
				: firehoses;

	/* Initialize bucket freelist with the total amount of buckets
	 * to be pinned (including the ones the client passed) */
	fh_bucket_init_freelist(firehoses + fhc_MaxVictimBuckets + b_prepinned);

	/*
	 * Prepin optimization: PHASE 2.
	 *
	 * In this phase, the firehose parameters have been validated and the
	 * buckets are added to the firehose table and set as 'used'.
	 *
	 */
	if (num_reg > 0) {
		uintptr_t	bucket_addr, end_addr;
		fh_bucket_t	*bd;

		for (i = 0; i < num_reg; i++) {
			end_addr = regions[i].addr + regions[i].len - 1;
			FH_FOREACH_BUCKET(regions[i].addr, 
	 				  end_addr, bucket_addr) {

				bd = fh_bucket_add(fh_mynode, bucket_addr);

				FH_BSTATE_SET(bd, fh_used);
				FH_SET_USED(bd);
				FH_TRACE_BUCKET(bd, ADDING PREPINNED);
			}
		}
	}


	/* 
	 * Set fields in the firehose information type, according to the limits
	 * established by the firehose parameters.
	 */
	{
		unsigned	med_regions, med_buckets;

		med_regions = (gasnet_AMMaxMedium() 
				- sizeof(firehose_remotecallback_args_t))
				/ sizeof(firehose_region_t);

		/* 
		 * For med_regions possible regions in the AMMedium, the worse
		 * case is drawn up as the following, a 1-for-1 replacement of
		 * old and new regions, which leaves a worse case of
		 * med_regions/2.
		 *
		 */
		med_buckets  = FH_MAX_BUCKETS_FOR_REGIONS(med_regions);
		fhc_MaxRemoteBuckets = MIN(med_buckets/2, fhc_RemoteBucketsM);

		fhinfo->max_RemoteRegions = 0;
		fhinfo->max_LocalRegions= 0;

		fhinfo->max_LocalPinSize  = 
		    fhc_MaxVictimBuckets * FH_BUCKET_SIZE;
		fhinfo->max_RemotePinSize = 
		    fhc_MaxRemoteBuckets * FH_BUCKET_SIZE;
		fh_max_regions = 
			FH_MIN_REGIONS_FOR_BUCKETS(fhc_MaxRemoteBuckets);

		GASNETI_TRACE_PRINTF(C, 
		    ("Firehose M=%ld (fh=%ld)\tprepinned=%ld (buckets=%d)",
		    M, firehoses, m_prepinned, (int) b_prepinned));
		GASNETI_TRACE_PRINTF(C, ("Firehose Maxvictim=%ld (fh=%d)",
		    maxvictim, fhc_MaxVictimBuckets));

		GASNETI_TRACE_PRINTF(C, 
		    ("MaxLocalPinSize=%d\tMaxRemotePinSize=%d", 
		    (int) fhinfo->max_LocalPinSize,
                    (int) fhinfo->max_RemotePinSize));
	}

	/*
	 * Allocate temporary buffers (temp_buckets and temp_bucket_ptrs) for
	 * use in managing remote pinned regions.
	 *
	 */
	fh_temp_buckets = (uintptr_t *)
		gasneti_malloc(sizeof(uintptr_t) * fh_max_regions);

	fh_temp_bucket_ptrs = (fh_bucket_t **)
		gasneti_malloc(sizeof(fh_bucket_t *) * fh_max_regions);

	return;
}

void
fh_fini_plugin()
{
	fhi_RegionPool_t	*rpool;
	int			i;

        /* Deallocate the arrays of bucket buffers used, if applicable */
        for (i = 0; i < FH_BUCKETS_BUFS; i++) {
                if (fh_buckets_bufs[i] == NULL)
                        break;
                gasneti_free(fh_buckets_bufs[i]);
        }

	fh_hash_destroy(fh_BucketTable);

	return;
}

/* ##################################################################### */
/* PAGE-SPECIFIC INTERNAL FUNCTIONS                                      */
/* ##################################################################### */

/* ################################################## */
/* Internal acquiring of regions on page granularity  */
/* ################################################## */
/* fhi_AcquireLocalRegionsList
 *
 * This function is used as a utility function for 'acquiring' new buckets, and
 * is used both for client-initiated local pinning and local pinning from AM
 * handlers.  The function differentiates these two with the 'local_ref'
 * parameter.
 *
 * It's main purpose is to filter out the buckets that are already pinned from
 * the input list of regions.  This means incrementing the refcount for buckets
 * already pinned and returning only a region of buckets that are currently
 * _not_ pinned.  This function does not call the client-supplied
 * move_callback.
 *
 * The function returns the amount of buckets (not regions) contained in the
 * buildregion type (the amount of regions can be queried from the type).
 *
 * The rpool is updated to reflect the number of buckets and regions that were
 * written.
 */

int
fhi_AcquireLocalRegionsList(int local_ref, firehose_region_t *region,
			    size_t reg_num, fhi_RegionPool_t *rpool)
{
	int			i, j, buckets_topin;
	firehose_region_t	*reg = rpool->regions;
	uintptr_t		bucket_addr, end_addr, next_addr;
	fh_bucket_t		*bd;

	buckets_topin = 0;
	rpool->regions_num = 0;
	rpool->buckets_num = 0;
	next_addr = (uintptr_t) -1;

	for (i = 0, j = -1; i < reg_num; i++) {

		end_addr = region[i].addr + region[i].len - 1;
				
 		FH_FOREACH_BUCKET(region[i].addr, end_addr, bucket_addr) 
		{
			gasneti_assert(bucket_addr > 0);
			bd = fh_bucket_lookup(fh_mynode, bucket_addr);

			if (bd != NULL) {
				/* 
				 * The bucket is already pinned, increment refc
				 */
				fh_priv_acquire_local(local_ref, bd);
				gasneti_assert(bd->fh_tqe_next == FH_USED_TAG);
			}
			else {
				/* The bucket is not pinned, see if the
				 * previous unpinned bucket is contiguous to
				 * this one. If so, simply increment the length
				 * of the region_t */

				if (j >= 0 && bucket_addr == next_addr)
					reg[j].len += FH_BUCKET_SIZE;
				else {
					gasneti_assert(rpool->len >
						sizeof(firehose_region_t) *
							rpool->regions_num);
					++j;
					reg[j].addr = bucket_addr;
					reg[j].len  = FH_BUCKET_SIZE;
					rpool->regions_num++;
				}

				rpool->buckets_num++;
				next_addr = bucket_addr + FH_BUCKET_SIZE;
			}
		}
	}

	return rpool->buckets_num;
}

/* 
 * fhi_ReleaseLocalRegionsList
 *
 * This function releases a list of regions and builds a new list of regions to
 * be unpinned by way of the client-supplied move_callback().
 *
 * By releasing buckets and adding them to the FIFO, it is possible that the
 * FIFO become overcommitted.  Overcommitting to the FIFO is permitted as we
 * are looping over the regions as long as a check is subsequently made.
 *
 * We process each region in the reverse order in order to ease coalescing when
 * popping victims from the victim FIFO.
 *
 */
void
fhi_ReleaseLocalRegionsList(int local_ref, firehose_region_t *reg, 
				size_t reg_num)
{
	int			i;
	uintptr_t		bucket_addr, end_addr;
	fh_bucket_t		*bd;

	FH_TABLE_ASSERT_LOCKED;

	for (i = 0; i < reg_num; i++) {
		end_addr = reg[i].addr + reg[i].len - 1;

		GASNETI_TRACE_PRINTF(C, 
		    ("Firehose ReleaseLocalRegions ("GASNETI_LADDRFMT", %d)",
		    GASNETI_LADDRSTR(reg[i].addr), (int)reg[i].len));
				
 		FH_FOREACH_BUCKET_REV(reg[i].addr, end_addr, bucket_addr) 
		{
			bd = fh_bucket_lookup(fh_mynode, bucket_addr);
			gasneti_assert(bd != NULL);

			fh_priv_release_local(local_ref, bd);
		}
	}
	return;
}

/* ####################################### */
/* Victim FIFO interfaces (page specific ) */
/* ####################################### */
/*
 * fh_FreeVictim(count, region_array, head)
 *
 * This function removes 'count' buckets from the victim FIFO (local or
 * remote), and fills the region_array with regions suitable for move_callback.
 * It returns the amount of regions (not buckets) created in the region_array.
 *
 * NOTE: it is up to the caller to make sure the region array can fit at most
 *       'buckets_topin' regions (ie: uncontiguous in the victim FIFO).
 *
 */

int
fh_FreeVictim(int buckets, firehose_region_t *reg, fh_fifoq_t *fifo_head)
{
	int		i, j;
	fh_bucket_t	*bd;
	uintptr_t	next_addr = 0;

	FH_TABLE_ASSERT_LOCKED;

	/* There must be enough buckets in the victim FIFO to unpin.  This
	 * criteria should always hold true per the constraints on
	 * fhc_LocalOnlyBucketsPinned. */
	for (i = 0, j = -1; i < buckets; i++) {
		bd = FH_TAILQ_FIRST(fifo_head);

		if (i > 0 && FH_BADDR(bd) == next_addr)
			reg[j].len += FH_BUCKET_SIZE;
		else {
			++j;
			reg[j].addr = FH_BADDR(bd);
			reg[j].len = FH_BUCKET_SIZE;
		}

		FH_TRACE_BUCKET(bd, REMFIFO);

		/* Remove the bucket descriptor from the FIFO and hash */
		FH_TAILQ_REMOVE(fifo_head, bd);
		fh_bucket_remove(bd);

		/* Next contiguous bucket address */
		next_addr = FH_BADDR(bd) + FH_BUCKET_SIZE;
	}
	gasneti_assert(buckets == j+1);
	return j+1;
}

/* 
 * fhi_CoalesceBuckets(buckets_ptr_vec, num_buckets, regions_array)
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
 * The function returns the amount of regions in the region array.
 *
 */
int
fhi_CoalesceBuckets(uintptr_t *bucket_list, size_t num_buckets,
		firehose_region_t *regions)
{
	int		i, j = -1; /* new buckets created */
	uintptr_t	addr_next = 0, bucket_addr;

	FH_TABLE_ASSERT_LOCKED;

	gasneti_assert(num_buckets > 0);
	/* Coalesce consequentive pages into a single region_t */
	for (i = 0; i < num_buckets; i++) {
		bucket_addr = bucket_list[i];
		if (i > 0 && bucket_addr == addr_next)
			regions[j].len += FH_BUCKET_SIZE;
		else {
			j++;
			regions[j].addr = bucket_addr;
			regions[j].len  = FH_BUCKET_SIZE;
		}

		addr_next = bucket_addr + FH_BUCKET_SIZE;
	}

	gasneti_assert(regions[j].addr > 0);
	return j+1;
}
	

/* ##################################################################### */
/* LOCAL PINNING                                                         */
/* ##################################################################### */
/* fhi_InitLocalRegionsList(local_ref, region, reg_num)
 *
 * This function adds all the buckets contained in the list of regions to the
 * hash table and initializes either the local or remote refcount to 1.
 *
 * It is used in fh_acquire_local_region() and fh_am_move_reqh_inner().
 *
 */
void
fhi_InitLocalRegionsList(int local_ref, firehose_region_t *region, 
					      int numreg)
{
	uintptr_t	end_addr, bucket_addr;
	fh_bucket_t	*bd;
	int		i;
	unsigned int	loc, rem;

	gasneti_assert((local_ref == 0) || (local_ref == 1));

	loc = local_ref;
	rem = !local_ref;

	FH_TABLE_ASSERT_LOCKED;

	/* Once pinned, We can walk over the regions to be pinned and
	 * set the reference count to 1. */
	for (i = 0; i < numreg; i++) {
		end_addr = region[i].addr + region[i].len - 1;

		gasneti_assert(region[i].addr > 0);

		FH_FOREACH_BUCKET(region[i].addr,end_addr,bucket_addr) {
			/* 
			 * Normally, the bucket will not already exist in the
			 * table.  However, in some threaded configurations
			 * it is possible for another thread to add the bucket
			 * (and pin the associated memory) while this current
			 * thread unlocked the table lock.
			 */
			#if GASNET_PAR || GASNETI_CONDUIT_THREADS
			bd = fh_bucket_lookup(fh_mynode, bucket_addr);
			if_pf (bd != NULL) {
				FH_BSTATE_SET(bd, fh_used);
				FH_BUCKET_REFC(bd)->refc_l += loc;
				FH_BUCKET_REFC(bd)->refc_r += rem;
				FH_TRACE_BUCKET(bd, INIT++);
			}
			else 
			#endif
			{
				bd = fh_bucket_add(fh_mynode, bucket_addr);
				FH_BSTATE_SET(bd, fh_used);
				FH_BUCKET_REFC(bd)->refc_l = loc;
				FH_BUCKET_REFC(bd)->refc_r = rem;
				FH_TRACE_BUCKET(bd, INIT);
			}
		}
	}

	return;
}

/* 
 * fh_acquire_local_region(region)
 *
 * In acquiring local pages covered over the region, pin calls are coalesced.
 * Acquiring a page may lead to a pin call but always results in the page
 * reference count being incremented.
 *
 * Under firehose-page, acquiring means finding bucket descriptors for each
 * bucket in the region and incrementing the bucket descriptor's reference
 * count.
 *
 * Called by fh_local_pin() (firehose_local_pin, firehose_local_try_pin)
 */

void
fh_acquire_local_region(firehose_request_t *req)
{
	int			b_num, b_total;
	firehose_region_t	region;
	fhi_RegionPool_t	*pin_p;


	FH_TABLE_ASSERT_LOCKED;

	gasneti_assert(req->node == fh_mynode);

	b_total = FH_NUM_BUCKETS(req->addr, req->len);
	/* Make sure the size of the region respects the local limits */
	gasneti_assert(b_total <= fhc_MaxVictimBuckets);

	pin_p = fhi_AllocRegionPool(FH_MIN_REGIONS_FOR_BUCKETS(b_total));
	region.addr = req->addr;
	region.len  = req->len;
	b_num = fhi_AcquireLocalRegionsList(1, &region, 1, pin_p);

	/* b_num contains the number of new Buckets to be pinned.  We may have
	 * to unpin Buckets in order to respect the threshold on locally pinned
	 * buckets. */
	if (b_num > 0) {
		fhi_RegionPool_t	*unpin_p;

		unpin_p = fhi_AllocRegionPool(FH_MIN_REGIONS_FOR_BUCKETS(b_num));
		unpin_p->regions_num = 
			fh_WaitLocalFirehoses(b_num, unpin_p->regions);

		FH_TABLE_UNLOCK;
		firehose_move_callback(fh_mynode,
				unpin_p->regions, unpin_p->regions_num,
				pin_p->regions, pin_p->regions_num);
		FH_TABLE_LOCK;

		fhi_InitLocalRegionsList(1, 
					 pin_p->regions, pin_p->regions_num);

		fhi_FreeRegionPool(unpin_p);
	}

	fhi_FreeRegionPool(pin_p);

	return;
}

void
fh_commit_try_local_region(firehose_request_t *req)
{
	uintptr_t	end_addr, bucket_addr;
	fh_bucket_t	*bd;


	FH_TABLE_ASSERT_LOCKED;
	gasneti_assert(req->node == fh_mynode);

	/* Make sure the size of the region respects the local limits */
	gasneti_assert(FH_NUM_BUCKETS(req->addr, req->len)
						<= fhc_MaxVictimBuckets);

	end_addr = req->addr + req->len - 1;
				
	FH_FOREACH_BUCKET(req->addr, end_addr, bucket_addr) 
	{
		gasneti_assert(bucket_addr > 0);
		bd = fh_bucket_lookup(fh_mynode, bucket_addr);
		gasneti_assert(bd != NULL);
		fh_priv_acquire_local(1, bd);
	}

	return;
}

/*
 * This function is called by the Firehose reply once a firehose request to pin
 * functions covered into a region completes.
 *
 * The function walks over each bucket in the region and identifies the buckets
 * that were marked as 'pending'.  These 'pending buckets' may or may not have
 * requests associated to them.  In the former case, requests pending a
 * callback are identified and may be added to a list of callbacks to be run
 * (algorithm documented below).
 *
 * The function returns the amount of callbacks that were added to the list of
 * pending requests pointing to the 'reqpend' parameter.
 *
 */
int
fh_find_pending_callbacks(gasnet_node_t node, firehose_region_t *region,
			  int nreg, void *context, fh_pollq_t *PendQ)
{
	int		numpend = 0, callspend = 0;
	uintptr_t	base_addr, end_addr, bucket_addr;
	fh_bucket_t	*bd, *bdi;
	int		i;

	fh_completion_callback_t	*ccb, *next_ccb;
	firehose_request_t		*req;

	FH_TABLE_ASSERT_LOCKED;	/* uses fh_temp_bucket_ptrs */
	gasneti_assert(node != fh_mynode);

	FH_STAILQ_INIT(PendQ);

	for (i = 0; i < nreg; i++) {
		end_addr = region[i].addr + region[i].len - 1;
		gasneti_assert(region[i].addr > 0);

		FH_FOREACH_BUCKET(region[i].addr,end_addr,bucket_addr) {
			bd = fh_bucket_lookup(node, bucket_addr);
			gasneti_assert(bd != NULL);

			/* Make sure the bucket was set as pending */
			gasneti_assert(FH_IS_REMOTE_PENDING(bd));
			FH_BSTATE_ASSERT(bd, fh_pending);
			gasneti_assert(bd->fh_tqe_next != NULL);

			/* if there is a pending request on the bucket, save it
			 * in the temp array */
			fh_temp_bucket_ptrs[numpend] = bd;
			numpend++;
			gasneti_assert(numpend < fh_max_regions);
			FH_UNSET_REMOTE_PENDING(bd);
			FH_BSTATE_SET(bd, fh_used);
		}
	}

	/* Each 'bd' is confirmed to pinned and may contain a pending request.
	 *
	 * For each pending request 
	 *
	 */
	for (i = 0; i < numpend; i++) {
		bd = fh_temp_bucket_ptrs[i];
		base_addr = FH_BADDR(bd) + FH_BUCKET_SIZE;
		ccb = (fh_completion_callback_t *) bd->fh_tqe_next;

		FH_SET_USED(bd);

		gasneti_assert(ccb != NULL);
		while (ccb != FH_COMPLETION_END) {
			next_ccb = FH_STAILQ_NEXT(ccb);
			gasneti_assert(ccb->flags &
					FH_CALLBACK_TYPE_COMPLETION);
			req = ccb->request;
			gasneti_assert(req && req->flags & FH_FLAG_PENDING);

			GASNETI_TRACE_PRINTF(C,
			    ("Firehose Pending FLUSH bd=%p (%p,%d), req=%p",
			     (void *) bd, (void *) FH_BADDR(bd), (int) FH_NODE(bd), (void *) req));

			/* Assume no other buckets are pending */
			req->flags &= ~FH_FLAG_PENDING;
			end_addr = req->addr + req->len - 1;

			/* Walk through each bucket in the region until a
			 * pending bucket is found.  If none can be found, the
			 * callback can be called.
			 */
			FH_FOREACH_BUCKET(base_addr, end_addr, bucket_addr) {
				bdi = fh_bucket_lookup(node, bucket_addr);
				gasneti_assert(bdi != NULL);

				if (FH_IS_REMOTE_PENDING(bdi)) {
					ccb->fh_tqe_next =
						(fh_completion_callback_t *)
						bdi->fh_tqe_next;
					bdi->fh_tqe_next = (fh_bucket_t *) ccb;

					req->flags |= FH_FLAG_PENDING;
					break;
				}
			}

			/* If the ccb is not pending any more, it has not been
			 * attached to any other bucket and its callback can be
			 * executed */
			if (!(req->flags & FH_FLAG_PENDING)) {
				FH_STAILQ_INSERT_TAIL(PendQ, 
					(fh_callback_t *) ccb);
				GASNETI_TRACE_PRINTF(C,
				    ("Firehose Pending Request (%p,%d) "
				     "enqueued  %p for callback", 
				     (void *) req->addr, (int) req->len, (void *) req));
				callspend++;
			}

			ccb = next_ccb;
		}
	}

	return callspend;
}

void
fh_commit_try_remote_region(firehose_request_t *req)
{
	uintptr_t	bucket_addr, end_addr  = req->addr + req->len - 1;
	fh_bucket_t	*bd;
	gasnet_node_t	node = req->node;

	FH_TABLE_ASSERT_LOCKED;

	/* Make sure the size of the region respects the remote limits */
	gasneti_assert(FH_NUM_BUCKETS(req->addr, req->len)
						<= fhc_MaxRemoteBuckets);

 	FH_FOREACH_BUCKET(req->addr, end_addr, bucket_addr) {
		bd = fh_bucket_lookup(node, bucket_addr);
		fh_priv_acquire_remote(node, bd);
	}
	return;
}


/* fh_release_local_region(request)
 *
 * DECrements/unpins pages covered in 
 *     [request->addr, request->addr+request->len].
 */
void
fh_release_local_region(firehose_request_t *request)
{
	firehose_region_t	reg;
	int			b_total;

	FH_TABLE_ASSERT_LOCKED;

	b_total = FH_NUM_BUCKETS(request->addr, request->len);
	FH_COPY_REQUEST_TO_REGION(&reg, request);


	fhi_ReleaseLocalRegionsList(1, &reg, 1);
	fh_AdjustLocalFifoAndPin(fh_mynode, NULL, 0);

	return;
}

/* ##################################################################### */
/* REMOTE PINNING                                                        */
/* ##################################################################### */
/*
 * The function attempts to acquire the region by hitting the firehose table.
 * For every bucket that is unpinned, the temp_array is used to hold 
 */

int
fhi_TryAcquireRemoteRegion(firehose_request_t *req, 
			fh_completion_callback_t *ccb,
			int *new_regions)
{
 	uintptr_t	bucket_addr, end_addr, next_addr = 0;
	int		unpinned = 0;
	int		new_r = 0, b_num;
	fh_bucket_t	*bd;
	gasnet_node_t	node;

	fh_completion_callback_t *ccba;

	end_addr = req->addr + (uintptr_t) req->len - 1;

	FH_TABLE_ASSERT_LOCKED;

	gasneti_assert(req != NULL);
	gasneti_assert(req->node != fh_mynode);
	node = req->node;

	b_num = FH_NUM_BUCKETS(req->addr, req->len);

	/* Validate the maximum number of regions required to describe these buckets */
	gasneti_assert(FH_MIN_REGIONS_FOR_BUCKETS(b_num) <= fh_max_regions); 

 	FH_FOREACH_BUCKET(req->addr, end_addr, bucket_addr) {
		bd = fh_bucket_lookup(node, bucket_addr);

		if (bd != NULL) {
			/* If the bucket is pending and the current request
			 * does not have a callback associated to it yet,
			 * allocate it */
			if (FH_IS_REMOTE_PENDING(bd)) {
				gasneti_assert(bd->fh_tqe_next != NULL);

				if (!(req->flags & FH_FLAG_PENDING)) {
					gasneti_assert(req->internal == NULL);
					ccba = fh_alloc_completion_callback();
					memcpy(ccba, ccb, 
					    sizeof(fh_completion_callback_t));
					ccba->fh_tqe_next = 
					    (fh_completion_callback_t *) 
					    bd->fh_tqe_next;
					bd->fh_tqe_next = (fh_bucket_t *) ccba;

					req->flags |= FH_FLAG_PENDING;
					req->internal = (firehose_private_t *)
						ccba;

					FH_TRACE_BUCKET(bd, PENDADD);

					GASNETI_TRACE_PRINTF(C,
			    		    ("Firehose Pending ADD bd=%p "
					     "(%p,%d), req=%p", (void *) bd, 
					     (void *) FH_BADDR(bd),
					     (int) FH_NODE(bd), 
					     (void *) req));
				}
				FH_BUCKET_REFC(bd)->refc_r++;
				gasneti_assert(FH_BUCKET_REFC(bd)->refc_r > 0);
				FH_TRACE_BUCKET(bd, PENDING);
			}
			else
				fh_priv_acquire_remote(node, bd);
		}
		else {
			fh_temp_buckets[unpinned] = bucket_addr;
			/* We add the bucket but set it PENDING */
			bd = fh_bucket_add(node, bucket_addr);
			FH_BSTATE_SET(bd, fh_pending);
			FH_SET_REMOTE_PENDING(bd);
			FH_TRACE_BUCKET(bd, INIT);

			if (next_addr != bucket_addr)
				new_r++;

			next_addr = bucket_addr + FH_BUCKET_SIZE;
			unpinned++;

			if (!(req->flags & FH_FLAG_PENDING)) {
				gasneti_assert(req->internal == NULL);
				ccba = fh_alloc_completion_callback();
				memcpy(ccba, ccb, 
					    sizeof(fh_completion_callback_t));
				ccba->fh_tqe_next = FH_COMPLETION_END;
				bd->fh_tqe_next = (fh_bucket_t *) ccba;
				req->flags |= FH_FLAG_PENDING;
				req->internal = (firehose_private_t *) ccba;
			}
		}
	}

	*new_regions = new_r;

	return unpinned;
}

/* fh_acquire_remote_region(request, callback, context, flags,
 *                          remotecallback_args)
 *
 * The function only requests a remote pin operation (AM) if one of the pages
 * covered in the region is not known to be pinned on the remote host.  Unless
 * the entire region hits the remote firehose hash, the value of the internal
 * pointer is set to FH_REQ_UNPINNED and a request for remote pages to be
 * pinned is enqueued.
 *
 * NOTE: this function begins with the table lock held an concludes with the
 * table lock released!!!
 * XXX/PHH: Can the UNBIND and AM stuff move to firehose.c to avoid this?
 *      If so, we must stop using alloca() here!  Instead of alloca(), we
 *      should pass the AM-args around, eliminating the scalar arguments
 *      entirely in favor of packing a struct provided by the caller
 *      (in firehose.c), and replacing 'args'.
 *      That means either calling alloca() for the maximum size
 *      unconditionally in the caller, or equivalently having an automatic
 *	variable or thread-local buffer of maximum size.
 */

void
fh_acquire_remote_region(firehose_request_t *req,
			 firehose_completed_fn_t callback, void *context,
			 uint32_t flags, 
			 firehose_remotecallback_args_t *args)
{
	int			 notpinned, new_r = 0;
	fh_completion_callback_t  ccb;
	gasnet_node_t node;

	/* Make sure the size of the region respects the remote limits */
	/* XXX should this check be done in non-assert */
	gasneti_assert(FH_NUM_BUCKETS(req->addr, req->len)
						<= fhc_RemoteBucketsM);

	FH_TABLE_ASSERT_LOCKED;

	node = req->node;
	req->internal = NULL;

	/* Fill in a completion callback struct temporarily as it may be used
	 * in fhi_TryAcquireRemoteRegion() */
	ccb.flags = FH_CALLBACK_TYPE_COMPLETION;
	ccb.fh_tqe_next = FH_COMPLETION_END;
	ccb.callback = callback;
	ccb.request  = req;
	ccb.context  = context;

	/* Writes the non-pinned buckets to temp_buckets array */
	notpinned = fhi_TryAcquireRemoteRegion(req, &ccb, &new_r);

	GASNETI_TRACE_PRINTF(C, 
	    ("Firehose Request Remote on %d ("GASNETI_LADDRFMT",%d) "
	     "(%d buckets unpinned, flags=0x%x)",
	     node, GASNETI_LADDRSTR(req->addr), (int) req->len,
	     notpinned, req->flags));

	/* 
	 * In moving remote regions, none of the temp arrays can be used, as
	 * the AM call has to be done without holding the TABLE lock.  For this
	 * reason, alloca() is used to acquire a temporary array of regions.
	 */

	if (notpinned > 0) {
		int			replace_b, old_r = 0, free_b;
		void			*reg_alloc;
		firehose_region_t	*reg_alloc_new, *reg_alloc_old;
		int			args_len = 0;

		gasneti_assert(req->internal != NULL);
		gasneti_assert(req->flags & FH_FLAG_PENDING);

		/* If the remote victim fifo is not full, no replacements are
		 * necessary */
		free_b = MIN(notpinned,
			    fhc_RemoteBucketsM - fhc_RemoteBucketsUsed[node]);
		replace_b = notpinned - free_b;

		/* See if we need any args */
		if (flags & FIREHOSE_FLAG_ENABLE_REMOTE_CALLBACK)
			args_len = sizeof(firehose_remotecallback_args_t);

		/* We've calculated 'new_r' regions will be sufficient for the
		 * replacement buckets and estimate a worst-case of 'replace_r'
		 * will be required for replacement firehoses 
		 * XXX should keep stats on the size of the alloca */
		reg_alloc = (void *)
			alloca(sizeof(firehose_region_t) * (new_r+replace_b) +
			       args_len);
		reg_alloc_new = (firehose_region_t *) reg_alloc;

		/* Coalesce new buckets into a minimal amount of regions */
		new_r = fhi_CoalesceBuckets(fh_temp_buckets, notpinned,
				reg_alloc_new);

		gasneti_assert(new_r > 0);
		reg_alloc_old = reg_alloc_new + new_r;

		/* Find replacement buckets if required */
		if (replace_b > 0)
			old_r = fh_WaitRemoteFirehoses(node, replace_b,
					reg_alloc_old);
		else
			old_r = 0;

		fhc_RemoteBucketsUsed[node] += (notpinned - replace_b);

		gasneti_assert(fhc_RemoteBucketsUsed[node] <= fhc_RemoteBucketsM);
		FH_TABLE_UNLOCK;

		if (args_len > 0)
			memcpy(reg_alloc_old + old_r, args, 
				sizeof(firehose_remotecallback_args_t));


		#ifdef FIREHOSE_UNBIND_CALLBACK
		if (old_r > 0)
			firehose_unbind_callback(node, reg_alloc_old, old_r);
		#endif

                MEDIUM_REQ(4,5,
			   (node,
			    fh_handleridx(fh_am_move_reqh),
			    reg_alloc, 
			    sizeof(firehose_region_t)*(new_r+old_r)+args_len, 
			    flags,
			    new_r,
			    old_r,
			    PACK(NULL)));
	}
	else {
		/* Only set the PINNED flag if the request is not set on any
		 * pending buckets */
		if (!(req->flags & FH_FLAG_PENDING))
			req->flags |= FH_FLAG_PINNED;
		FH_TABLE_UNLOCK;
	}

	FH_TABLE_ASSERT_UNLOCKED;
}

/*
 * fh_release_remote_region(request)
 *
 * This function releases every page in the region described in the firehose
 * request type.
 *
 * Loop over each bucket in reverse order
 *  If the reference count reaches zero, push the descriptor at the head of the
 *  victim FIFO
 */

void
fh_release_remote_region(firehose_request_t *request)
{
	uintptr_t	end_addr, bucket_addr;
	fh_bucket_t	*bd;

	FH_TABLE_ASSERT_LOCKED;

	end_addr = request->addr + request->len - 1;

	GASNETI_TRACE_PRINTF(C, ("Firehose release_remote_region("
				 GASNETI_LADDRFMT", %d) "GASNETI_LADDRFMT,
				 GASNETI_LADDRSTR(request->addr),
				 (int) request->len,
				 GASNETI_LADDRSTR(request)));
	/* Process region in reverse order so regions can be later coalesced in
	 * the proper order (lower to higher address) from the FIFO */
	FH_FOREACH_BUCKET_REV(request->addr, end_addr, bucket_addr) {
		bd = fh_bucket_lookup(request->node, bucket_addr);
		gasneti_assert(bd != NULL);
		gasneti_assert(!FH_IS_REMOTE_PENDING(bd));

		fh_priv_release_remote(request->node, bd);
	}

	gasneti_assert(fhc_RemoteVictimFifoBuckets[request->node] 
				<= fhc_RemoteBucketsM);

	return;
}

/* ##################################################################### */
/* ACTIVE MESSAGES                                                       */ 
/* ##################################################################### */
void
fh_move_request(gasnet_node_t node,
		firehose_region_t *new_reg, size_t r_new,
		firehose_region_t *old_reg, size_t r_old,
		void *context)
{
	fhi_RegionPool_t	*rpool;
	int			i, r_alloc;

	FH_TABLE_LOCK;

	GASNETI_TRACE_PRINTF(C, ("Firehose move request: new=%d, old=%d",
				 (int) r_new, (int) r_old));

	/* Loop over the new regions to count the worst case number of
	 * regions we will need to describe their unpinned subset. */
	for (i=0, r_alloc=0; i < r_new; ++i) {
		r_alloc += FH_MIN_REGIONS_FOR_BUCKETS(
			   FH_NUM_BUCKETS(
				new_reg[i].addr, new_reg[i].len));
	}
	rpool = fhi_AllocRegionPool(r_alloc);
	fhi_AcquireLocalRegionsList(0, new_reg, r_new, rpool);

	GASNETI_TRACE_PRINTF(C, ("Firehose move request: pin new=%d",
				 (int) rpool->buckets_num));

	/* The next function may overcommit the fifo before the call to
	 * actually pin new regions is issued. */
	fhi_ReleaseLocalRegionsList(0, old_reg, r_old);

	fh_AdjustLocalFifoAndPin(node, rpool->regions, rpool->regions_num);
	fhi_InitLocalRegionsList(0, rpool->regions, rpool->regions_num);

	fhi_FreeRegionPool(rpool);
	FH_TABLE_UNLOCK;

	return;
}

void
fh_dump_counters()
{
	int 		i;
	gasnet_node_t	node = fh_mynode;

	/* Local counters */
	printf("%d> MaxVictimB=%d, Local[Only/Fifo]=[%d/%d]\n",
		node, fhc_MaxVictimBuckets, fhc_LocalOnlyBucketsPinned, 
		fhc_LocalVictimFifoBuckets);

	/* Remote counters */
	for (i = 0; i < gasnet_nodes(); i++) {
		if (i == node)
			continue;
		printf("%d> RemoteBuckets on %2d =     [%6d/%6d]\n", 
			node, i, fhc_RemoteBucketsUsed[i], fhc_RemoteBucketsM);
	}

	for (i = 0; i < gasnet_nodes(); i++) {
		if (i == node)
			continue;
		printf("%d> RemoteFifoBuckets on %2d = [%6d/%6d]\n", node, i,
			fhc_RemoteVictimFifoBuckets[i], fhc_RemoteBucketsM);
	}
}
#endif

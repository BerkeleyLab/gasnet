#include <firehose.h>
#include <firehose_internal.h>
#include <gasnet.h>
#include <gasnet_handler.h>

#ifdef FIREHOSE_PAGE

#define FH_MAX_REGION_BUCKETS	4096
#define FH_MAX_REGION_BUCKETS_SIZE					\
		(FH_MAX_REGION_BUCKETS*sizeof(firehose_region_t))
#define FH_MAX_REGIONBYTES	(FH_MAX_REGION_BUCKETS*FH_BUCKET_SIZE)

#ifndef FH_MAXVICTIM_TO_PHYSMEM_RATIO
#define FH_MAXVICTIM_TO_PHYSMEM_RATIO	0.25
#endif

/* fhi_RegionFilter_t
 *
 * The region filter type is used throughout firehose-page as a mechanism to
 * allow separation of unpinned and pinned buckets given one (or many) regions.
 * This operation proves to be useful in both client and AM-initiated pinning
 * requests and is mainly used in the fhi_AcquireLocalRegionsList() and
 * fhi_ReleaseLocalRegionsList() functions.
 */
typedef
struct _fhi_RegionFilter_t {
	firehose_region_t	*regions_in;
	int	 		 in;
	firehose_region_t	*regions_out;
	int	 		 out;
}
fhi_RegionFilter_t;


/* Internal, firehose-page only functions */
int	fhi_AcquireLocalRegionsList(gasnet_node_t, fhi_RegionFilter_t *);
int	fhi_ReleaseLocalRegionsList(gasnet_node_t, fhi_RegionFilter_t *);

int 	fhi_FreeVictimLocal(int buckets, firehose_region_t *);
int	fhi_FreeVictimRemote(gasnet_node_t, int buckets, firehose_region_t *);

int	fhi_CoalesceBuckets(fh_bucket_t **buckets, size_t num_buckets,
			    firehose_region_t *regions);

void	fhi_RecoverLocalBucketsAndMove(firehose_region_t *reg_pin, int topin,
				       int b_recover);
void	fhi_InitRegionsList(gasnet_node_t, firehose_region_t *, int numreg);

/* ##################################################################### */
/* LOCKS AND BUFFERS                                                     */
/* ##################################################################### */

/* The following lock, referred to as the "table" lock, is the only firehose
 * lock that is required.  It must be held during all of the firehose
 * operations - adding/removing to the hash table, adding/removing from the
 * local and victim FIFOs.
 */
gasneti_mutex_t		fh_table_lock = GASNETI_MUTEX_INITIALIZER;

/* This lock protects the poll FIFO queue, used to enqueue callbacks. */
gasneti_mutex_t		fh_pollq_lock = GASNETI_MUTEX_INITIALIZER;

/* The following two buffers are two temporary regions/buckets buffers that can
 * be used while holding the table lock.  There contents remains valid while
 * the lock is held and are rendered invalid as soon as the table lock is
 * released.  It cannot be assumed that the contents remain valid if the lock
 * is successively unlocked and locked as another thread waiting for the table
 * lock may be scheduled and may reuse the buffer.
 *
 * The only two problem cases concerning use of the buffer arise in client
 * requests to pin local and/or remote regions.  In order to remain within the
 * M and MAXVICTIMS thresholds, these functions may be required to call
 * gasnet_AMPoll() to make the gasnet core make progress on the firehose
 * requests already initiated.  In acquiring a remote region, this problem is
 * dealt with by using alloca(), whereas acquiring a local region causes
 * firehose to use gasneti_malloc() in the less common case that polling is
 * required to recover some buckets.
 */
static firehose_region_t	fh_temp_regions[FH_MAX_REGIONBYTES];
static fh_bucket_t		*fh_temp_buckets[FH_MAX_REGIONBYTES];

/* ##################################################################### */
/* COUNTERS                                                              */
/* ##################################################################### */
/* LOCAL COUNTERS                                                        */

/* fhc_LocalOnlyBucketsPinned - incrementing counter
 *     Amount of buckets pinned only for the local node (localref > 0 AND
 *     remoteref == 0).                                                */
int	fhc_LocalOnlyBucketsPinned;

/* fhc_LocalVictimFifoBuckets - incrementing counter
 *     Amount of buckets currently contained in the Local Victim FIFO. */
int	fhc_LocalVictimFifoBuckets;

/* fhc_MaxVictimBuckets - static count
 *     Maximum amount of victims that may be pinned other than M.  At all
 *     fhc_LocalOnlyBucketsPinned + 
 *        fhc_LocalVictimFifoBuckets < fhc_MaxVictimBuckets             */
int	fhc_MaxVictimBuckets;

/* fhc_LocalOnlyBucketsInFlight - incrementing counter
 *     Total amount of local buckets currently touched (refcount incremented)
 *     by locally-initiated operations.  This count must be less than
 *     fhc_MaxVictimBuckets in order to avoid deadlocks.                   */
int	fhc_LocalOnlyBucketsInFlight;

/* XXX Currently unused. . will be useful in adaptive victim FIFO.
 * fhc_LocalBucketsPinned - incrementing counter
 *     Amount of buckets pinned as a result of remote requests to pin local
 *     buckets.  This parameter is bounded by 'M'.
 *
 */
#define FHC_MAXVICTIM_BUCKETS_AVAIL (fhc_MaxVictimBuckets - 		\
		(fhc_LocalOnlyBucketsPinned + fhc_LocalVictimFifoBuckets))

/* REMOTE COUNTERS                                                         */
/* fhc_RemoteBucketsM - static count
 *    Amount of per-node firehoses that can be mapped as established by the
 *    firehose 'M' parameter.                                              */
int	fhc_RemoteBucketsM;

/* fhc_RemoteBucketsUsed[0..nodes-1] - Array of incrementing counters
 *    Amount of buckets currently used by the current node.                */
int	*fhc_RemoteBucketsUsed;

/* fhc_RemoteVictimFifoBuckets[0..nodes-1] - Array of incrementing counters
 *     Available amount of remote buckets that can be used without sending
 *     replacement buckets.  */
int	*fhc_RemoteVictimFifoBuckets;

/* ACTIVE MESSAGES DECL                                                   */ 
static gasnet_handlerentry_t fh_am_handlers[];
/* Initial value of index for gasnet registration */
#define _hidx_fh_am_move_reqh			0
#define _hidx_fh_am_move_reph			0

/* Index into the fh_am_handlers table to obtain the gasnet registered index */
#define _fh_hidx_fh_am_move_reqh		0
#define _fh_hidx_fh_am_move_reph		1

#define fh_handleridx(reqh)	(fh_am_handlers[ _fh_hidx_ ## reqh ].index)

/* ##################################################################### */
/* UTILITY FUNCTIONS FOR REGIONS AND BUCKETS                             */
/* ##################################################################### */
/* fh_region_ispinned(node, addr, len)
 * 
 * Returns non-null if the entire region is already pinned 
 *
 * Uses fh_bucket_ispinned() to query if the current page is pinned.
 */
int
fh_region_ispinned(gasnet_node_t node, uintptr_t addr, size_t len)
{
 	uintptr_t	bucket_addr;
	uintptr_t	end_addr = addr + len - 1;

	FH_TABLE_ASSERT_LOCKED;
 	FH_FOREACH_BUCKET(addr, end_addr, bucket_addr) {
		if (fh_bucket_lookup(node, bucket_addr) == NULL)
			return 0;
	}
	return 1;
}

fh_refc_t
fh_bucket_acquire(gasnet_node_t node, fh_bucket_t *entry)
{
	FH_TABLE_ASSERT_LOCKED;
	/* In fifo, local=0 remote=0 */
	if (fh_in_fifo(entry)) {
		FH_TAILQ_REMOVE(&fh_LocalFifo, entry);

		if (gasnet_mynode() == node) {
			fh_lrefcRst(fh_refcount(entry));
			fh_lrefcInc(fh_refcount(entry));

			fhc_LocalOnlyBucketsPinned++;
		}
		else {
			fh_rrefcRst(fh_refcount(entry));
			fh_rrefcInc(fh_refcount(entry));
		}
	}
	else {
		if (gasnet_mynode() == node)
			fh_lrefcInc(fh_refcount(entry));
		else {
			/* If incrementing the remote refcount from 0 to 1, we
			 * conclude that the bucket was previously a local-only
			 * pin. */
			fhc_LocalOnlyBucketsPinned--;
			fh_rrefcInc(fh_refcount(entry));
		}
	}

	return fh_refcount(entry);
}

fh_refc_t
fh_bucket_release(gasnet_node_t node, fh_bucket_t *entry)
{
	FH_TABLE_ASSERT_LOCKED;
	/* Make sure the reference count to be decremented is greater than 0 if
	 * the release is local or remote */
	/* XXX perhaps expose this error to client with fatalerror() ? */
	assert(node == gasnet_mynode() 
		? fh_lrefc(fh_refcount(entry)) > 0
		: fh_rrefc(fh_refcount(entry)) > 0);

	if (gasnet_mynode() == node) {
		fh_lrefcDec(fh_refcount(entry));

		/* If the local refcount is 0, it isn't a local-only bucket (if
		 * remoteref is also zero, it will be caught later on */
		if (fh_lrefc(fh_refcount(entry)) == 0)
			fhc_LocalOnlyBucketsPinned--;
	}
	else
		fh_rrefcDec(fh_refcount(entry));

	if (fh_refc_is_victim(fh_refcount(entry)))
		FH_TAILQ_INSERT_TAIL(&fh_LocalFifo, entry);

	return fh_refcount(entry);
}


/* fh_init_plugin()
 *
 * This function is only called from firehose_init and allows -page OR -region
 * to run plugin specific code.
 */

void
fh_init_plugin(uintptr_t max_pinnable_memory, size_t max_regions, 
	      firehose_info_t *info)
{
	int		i;
	unsigned long	M, maxvictim, firehoses;

	assert(FH_MAXVICTIM_TO_PHYSMEM_RATIO >= 0 && 
	       FH_MAXVICTIM_TO_PHYSMEM_RATIO <= 1);

	/* In -page, we ignore regions. . there should not be a limit on the
	 * number of regions */

	if (max_regions != 0)
		gasneti_fatalerror("firehose-page does not support a "
				   "limitation on the number of regions");

	/* Allocate the per-node counters */
	fhc_RemoteBucketsUsed = (int *)
		gasneti_malloc(gasnet_nodes() * sizeof(int));
	memset(fhc_RemoteBucketsUsed, 0, gasnet_nodes() * sizeof(int));

	fhc_RemoteVictimFifoBuckets = (int *)
		gasneti_malloc(gasnet_nodes() * sizeof(int));
	memset(fhc_RemoteVictimFifoBuckets, 0, gasnet_nodes() * sizeof(int));

	M = fh_getenv("GASNET_FIREHOSE_M", (1>>20));
	maxvictim = fh_getenv("GASNET_FIREHOSE_MAXVICTIM_M", (1>>20));

	if (M == 0 && maxvictim == 0) {
		M = (unsigned long) max_pinnable_memory *
				    FH_MAXVICTIM_TO_PHYSMEM_RATIO;
		maxvictim = (unsigned long) max_pinnable_memory *
				(1-FH_MAXVICTIM_TO_PHYSMEM_RATIO);
	}
	else if (M == 0)
		M = max_pinnable_memory - maxvictim;
	else if (maxvictim == 0)
		maxvictim = max_pinnable_memory - M;

	/* Local */
	fhc_LocalOnlyBucketsPinned = 0;
	fhc_LocalVictimFifoBuckets = 0;
	fhc_LocalOnlyBucketsInFlight = 0;
	fhc_MaxVictimBuckets = maxvictim >> FH_BUCKET_SHIFT;

	/* Remote */
	firehoses = M >> FH_BUCKET_SHIFT;
	fhc_RemoteBucketsM = firehoses / gasnet_nodes();
	for (i = 0; i < gasnet_mynode(); i++) {
		fhc_RemoteVictimFifoBuckets[i] = 0;
		fhc_RemoteBucketsUsed[i] = 0;
	}

	/* Initialize bucket freelist with the total amount of buckets to be
	 * pinned */
	fh_bucket_init_freelist(firehoses+fhc_MaxVictimBuckets);

	return;
}

void
fh_fini_plugin()
{
	gasneti_free(fhc_RemoteBucketsUsed);
	gasneti_free(fhc_RemoteVictimFifoBuckets);
	return;
}

/* ##################################################################### */
/* PAGE-SPECIFIC INTERNAL FUNCTIONS                                      */
/* ##################################################################### */
/* fhi_AcquireLocalRegionsList(node, buildregion)
 *
 * This function is used as a utility function for 'acquiring' new buckets, and
 * is used both for client-initiated local pinning and local pinning from AM
 * handlers.  The function differentiates these two with the 'node' parameter.
 * Client-initiated local pins pass 'gasnet_mynode()' while AM pins pass the
 * node of the initiator. 
 *
 * It's main purpose is to filter out the buckets that are already pinned from
 * the input list of regions.  This means incrementing the refcount for buckets
 * already pinned and returning only a region of buckets that are currently
 * _not_ pinned.  This function does not call the client-supplied
 * move_callback.
 *
 * From the fhi_RegionFilter_t type,
 *   regions_in:  List of regions to be pinned
 *   regions_out: List of regions that constitute the 'pin_list' for
 *   		  firehose_move_callback.  
 *
 * The function returns the amount of buckets (not regions) contained in the
 * buildregion type (the amount of regions can be queried from the type).
 */
int
fhi_AcquireLocalRegionsList(gasnet_node_t node, fhi_RegionFilter_t *regfilt)
{
	int			i, j, buckets_topin;
	firehose_region_t	*reg = regfilt->regions_out;
	uintptr_t		bucket_addr, end_addr, next_addr;
	fh_bucket_t		*bd;

	buckets_topin = 0;

	for (i = 0, j = -1; i < regfilt->in; i++) {

		reg[j].addr = regfilt->regions_in[i].addr;
		reg[j].len  = 0;

		end_addr = regfilt->regions_in[i].addr +
				regfilt->regions_in[i].len - 1;
				
 		FH_FOREACH_BUCKET(regfilt->regions_in[i].addr, 
		                  end_addr, 
				  bucket_addr) 
		{
			bd = fh_bucket_lookup(gasnet_mynode(), bucket_addr);

			if (bd != NULL) {
				/* The bucket is already pinned */
				fh_bucket_acquire(node, bd);
			}
			else {
				/* The bucket is not pinned, see if the
				 * previous unpinned bucket is contiguous to
				 * this one. If so, simply increment the length
				 * of the region_t */

				if (j >= 0 && bucket_addr == next_addr)
					reg[j].len += FH_BUCKET_SIZE;
				else {
					++j;
					reg[j].addr = bucket_addr;
					reg[j].len  = FH_BUCKET_SIZE;
				}
				buckets_topin++;
			}

			next_addr = bucket_addr + FH_BUCKET_SIZE;
		}
	}

	/* Make sure we actually did loop over a region before assigning the
	 * number of existing regions in regions_out */
	regfilt->out = i > 0 ? j + 1 : 0;

	return buckets_topin;
}

/* fhi_ReleaseLocalRegionsList(node, buildregion)
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
 */
int
fhi_ReleaseLocalRegionsList(gasnet_node_t node, fhi_RegionFilter_t *regfilt)
{
	int			i, j, buckets_tounpin;
	firehose_region_t	*reg = regfilt->regions_out;
	uintptr_t		bucket_addr, end_addr;
	fh_bucket_t		*bd;

	for (i = 0; i < regfilt->in; i++) {

		reg[j].addr = regfilt->regions_in[i].addr;
		reg[j].len  = 0;

		end_addr = regfilt->regions_in[i].addr +
				regfilt->regions_in[i].len - 1;
				
 		FH_FOREACH_BUCKET(regfilt->regions_in[i].addr, 
		                  end_addr, 
				  bucket_addr) 
		{
			bd = fh_bucket_lookup(gasnet_mynode(), bucket_addr);
			assert(bd != NULL);

			fh_bucket_release(node, bd);
		}
	}

	/* Now check if we've overcommitted to the FIFO.  If so, we build a
	 * list of regions to unpin from the head of the FIFO (oldest victim).*/
	buckets_tounpin = 
		(fhc_LocalOnlyBucketsPinned + fhc_LocalVictimFifoBuckets) - 
		fhc_MaxVictimBuckets;

	if (buckets_tounpin > 0)
		regfilt->out = 
		    fhi_FreeVictimLocal(buckets_tounpin, regfilt->regions_out);

	return buckets_tounpin;
}

/*
 * _fhi_FreeVictim(buckets, region_array, head)
 *
 * This function removes 'buckets' buckets from the victim FIFO (local or
 * remote), and fills the region_array with regions suitable for move_callback.
 * It returns the amount of regions (not buckets) created in the region_array.
 *
 * NOTE: it is up to the caller to make sure the region array can fit at most
 *       'buckets_topin' regions (ie: uncontiguous in the victim FIFO).
 *
 */

GASNET_INLINE_MODIFIER(_fhi_FreeVictim)
int
_fhi_FreeVictim(int buckets, firehose_region_t *reg, fh_fifoq_t *fifo_head)
{
	int		i, j;
	fh_bucket_t	*bd;
	uintptr_t	next_addr;

	/* There must be enough buckets in the victim FIFO to unpin.  This
	 * criteria should always hold true per the constraints on
	 * fhc_LocalOnlyBucketsPinned. */
	for (i = 0, j = -1; i < buckets; i++) {
		bd = FH_TAILQ_FIRST(fifo_head);

		if (i > 0 && fh_baddr(bd) == next_addr)
			reg[j].len += FH_BUCKET_SIZE;
		else {
			++j;
			reg[j].addr = fh_baddr(bd);
			reg[j].len = FH_BUCKET_SIZE;
		}

		/* Remove the bucket descriptor from the FIFO and hash */
		FH_TAILQ_REMOVE(fifo_head, bd);
		fh_bucket_remove(bd);

		/* Next contiguous bucket address */
		next_addr = fh_baddr(bd) + FH_BUCKET_SIZE;
	}
	return i > 0 ? j : 0;
}

/* fhi_FreeVictimLocal(buckets, reg)
 *
 * FreeVictim for the local bucket fifo.
 */
int 
fhi_FreeVictimLocal(int buckets, firehose_region_t *reg)
{
	int freed;

	assert(buckets <= fhc_LocalVictimFifoBuckets);
	freed = _fhi_FreeVictim(buckets, reg, &fh_LocalFifo);
	fhc_LocalVictimFifoBuckets -= freed;
	return freed;
}

/* fhi_FreeVictimRemote(node, buckets, reg)
 *
 * FreeVictim for the local bucket fifo.
 */
int
fhi_FreeVictimRemote(gasnet_node_t node, int buckets, firehose_region_t *reg)
{
	int	freed;

	assert(buckets <= fhc_RemoteVictimFifoBuckets[node]);
	freed = _fhi_FreeVictim(buckets, reg, &fh_RemoteNodeFifo[node]);
	fhc_RemoteVictimFifoBuckets[node] -= freed;
	return freed;
}

/* fhi_CoalesceBuckets(buckets_ptr_vec, num_buckets, regions_array)
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
fhi_CoalesceBuckets(fh_bucket_t **buckets, size_t num_buckets,
		firehose_region_t *regions)
{
	int		i, j = -1; /* new buckets created */
	fh_bucket_t	*bd;
	uintptr_t	addr_next = 0;

	assert(num_buckets > 0);
	/* Coalesce consequentive pages into a single region_t */
	for (i = 0; i < num_buckets; i++) {
		bd = buckets[i];
		if (i > 0 && fh_baddr(bd) == addr_next)
			regions[j].len += FH_BUCKET_SIZE;
		else {
			j++;
			regions[j].addr = fh_baddr(bd);
			regions[j].len  = FH_BUCKET_SIZE;
		}

		addr_next = fh_baddr(bd) + FH_BUCKET_SIZE;
	}

	return j;
}
	

/* ##################################################################### */
/* LOCAL PINNING                                                         */
/* ##################################################################### */

/* fhi_RecoverLocalBucketsAndMove(regions_to_pin, to_pin, buckets_to_recover)
 *
 * This is a utility function for fh_acquire_local_region() when the number of
 * local pins nears MAXVICTIM.  This helper can recover 'buckets_to_recover'
 * buckets in order to remain within the MAXVICTIM threshold.  
 *
 * In the common case where there are enough buckets in the FIFO, enough
 * buckets can be recovered simply by popping buckets from the FIFO.
 *
 * In the less common case where there are not enough buckets in the FIFO to
 * fulfill the firehose move requirements, the function has to poll for
 * buckets.  In this case, the temp_regions array cannot be used as other
 * threads waiting for the TABLE lock in fh_acquire_local_region() could
 * possibly be rescheduled once gasnet_AMPoll() returns.  For this less common
 * case, gasneti_malloc() is used to acquire memory and copy the temp_regions
 * array.
 */
void
fhi_RecoverLocalBucketsAndMove(firehose_region_t *reg_pin, int topin, 
		               int b_recover)
{
	firehose_region_t	*reg_unpin;
	int			 b_unpin, tounpin = 0;

	FH_TABLE_ASSERT_LOCKED;

	b_unpin = MIN(b_recover, fhc_LocalVictimFifoBuckets);

	/* If we can simply recover buckets without polling, we can use the
	 * temp_regions array without trouble */

	if (b_unpin >= b_recover) {
		reg_unpin = &fh_temp_regions[topin];
		tounpin = fhi_FreeVictimLocal(b_unpin, reg_unpin);
		firehose_move_callback(gasnet_mynode(), reg_pin, topin,
							reg_unpin, tounpin);
	}
	else {
		firehose_region_t	*reg_unpin_r;
		firehose_region_t	*reg_alloc = 
			gasneti_malloc(sizeof(firehose_region_t) * 
				       (b_recover + topin));
		if (reg_alloc == NULL)
			gasneti_fatalerror(
			"Can't allocate memory for saved regions buf");
		memcpy(reg_alloc, reg_pin, sizeof(firehose_region_t) * topin);

		reg_pin = reg_alloc;
		reg_unpin_r = reg_unpin = &reg_alloc[topin];

		/* Simply Poll until we can reuse some buckets or unpin some
		 * more */
		while (b_recover > 0) {

			FH_TABLE_UNLOCK;
			gasnet_AMPoll();
			FH_TABLE_LOCK;

			b_unpin = MIN(b_recover, fhc_LocalVictimFifoBuckets);

			if (b_unpin > 0) {
				int u = fhi_FreeVictimLocal(b_unpin, 
						            reg_unpin_r);

				b_recover -= u;
				reg_unpin_r += u;
			}
		}

		firehose_move_callback(gasnet_mynode(), reg_pin, topin,
							reg_unpin, tounpin);
		free(reg_alloc);	
	}
	
	fhi_InitRegionsList(gasnet_mynode(), reg_pin, topin);

	return;
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
 * Called by fh_local_pin() (firehose_local_pin, firehose_local_try_pin)
 */

firehose_private_t *
fh_acquire_local_region(firehose_region_t *region)
{
	fhi_RegionFilter_t	regfilt;
	firehose_region_t	*reg_unpin = NULL;
	int			reg_unpin_num = 0;
	int			b_topin, b_recover;
	int			b_avail;

	FH_TABLE_ASSERT_LOCKED;

	/* Make sure this request doesn't overflow the
	 * fhc_LocalOnlyBucketsInFlight  counter */
	b_topin = FH_NUM_BUCKETS(region->addr, region->len);
	assert((b_topin <= fhc_MaxVictimBuckets) ||
			(printf("TOPIN = %d\n", b_topin) && fflush(stdout)));

	b_recover = fhc_MaxVictimBuckets - fhc_LocalOnlyBucketsInFlight;
	assert(b_recover >= 0);

	/* If we overflow the counter, we must wait for some operations to
	 * complete in order to (possibly) avoid deadlock. */
	while (b_topin < fhc_MaxVictimBuckets - fhc_LocalOnlyBucketsInFlight) {
			FH_TABLE_UNLOCK;
			gasnet_AMPoll();
			FH_TABLE_LOCK;
	}

	regfilt.regions_in  = region;
	regfilt.regions_out = fh_temp_regions;
	regfilt.in = 1;

	/* Acquire the region for the _local_ node */
	b_topin = fhi_AcquireLocalRegionsList(gasnet_mynode(), &regfilt);

	if (b_topin > 0) {
		firehose_region_t	*reg;

		reg = reg_unpin = &fh_temp_regions[regfilt.out];
		b_recover = b_topin - FHC_MAXVICTIM_BUCKETS_AVAIL;

		/* If we can't pin these buckets and remain within MAXVICTIM,
		 * we may have to recover buckets from the victim FIFO.  If
		 * such is the case, chances are we have to recover buckets by
		 * polling. */

		if (b_recover > 0) {
			fhi_RecoverLocalBucketsAndMove(regfilt.regions_out,
					regfilt.out, b_recover);
		}
		else {
			firehose_move_callback(gasnet_mynode(),
		    		regfilt.regions_out, regfilt.out, 
				reg_unpin, reg_unpin_num);

			fhi_InitRegionsList(gasnet_mynode(), 
				    regfilt.regions_out, regfilt.out);
		}


	}

	fhc_LocalOnlyBucketsInFlight += b_topin;

	return fh_bucket_lookup(gasnet_mynode(), region->addr);
}

/* fhi_InitRegionsList(node, region, reg_num)
 *
 * This function adds all the buckets contained in the list of regions to the
 * hash table and initializes either the local or remote refcount to 1.
 */
void
fhi_InitRegionsList(gasnet_node_t node, firehose_region_t *region, int numreg)
{
	uintptr_t	end_addr, bucket_addr;
	fh_bucket_t	*bd;
	int		i, loc, rem;

	FH_TABLE_ASSERT_LOCKED;

	if (node == gasnet_mynode())
		loc = 1, rem = 0;
	else
		loc = 0, rem = 1;

	/* Once pinned, We can walk over the regions to be pinned and
	 * set the reference count to 1. */
	for (i = 0; i < numreg; i++) {

		end_addr = region[i].addr + region[i].len - 1;

		FH_FOREACH_BUCKET(region[i].addr, end_addr, bucket_addr) {
			bd = fh_bucket_add(gasnet_mynode(), bucket_addr);
			fh_refcRst(fh_refcount(bd));
			fh_refcSet(fh_refcount(bd), loc, rem);
		}
	}
}

/* fh_release_local_region(request)
 *
 * Decrements/unpins pages covered in 
 *     [request->addr, request->addr+request->len].
 *
 */

void
fh_release_local_region(firehose_request_t *request)
{
	fhi_RegionFilter_t	regfilt;
	firehose_region_t	reg;
	int			to_unpin;

	FH_COPY_REQUEST_TO_REGION(request, &reg);

	regfilt.regions_in = &reg;
	regfilt.in = 1;
	regfilt.regions_out = fh_temp_regions;

	/* The function returns the number of buckets to unpin.  Buckets here
	 * are irrelevent, but this tells us if some regions where filled in
	 * 'regions_out'.  Before returning, 'fhi_ReleaseLocalRegionsList'
	 * makes sure that the MAXVICTIM threshold is respected */
	to_unpin = fhi_ReleaseLocalRegionsList(gasnet_mynode(), &regfilt);

	if (to_unpin > 0) {
		firehose_move_callback(gasnet_mynode(), 
			NULL, 0, regfilt.regions_out, regfilt.out);
	}

	/* Adjust the number of buckets pinned as part of the current number of
	 * operations in flight */
	fhc_LocalOnlyBucketsInFlight -= 
		FH_NUM_BUCKETS(request->addr, request->len);

	return;
}

/* ##################################################################### */
/* REMOTE PINNING                                                        */
/* ##################################################################### */
/* fh_acquire_remote_region(node, region, callback, context, flags,
 *                          remotecallback_args)
 *
 * The function only requests a remote pin operation (AM) if one of the pages
 * covered in the region is not known to be pinned on the remote host.  Unless
 * the entire region hits the remote firehose hash, the value of the internal
 * pointer is set to FH_REQ_UNPINNED and a request for remote pages to be
 * pinned is enqueued.
 *
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
 *      - fh_bucket_acquire(), fh_bucket_lookup()
 *      - fhi_CoalesceBuckets() to coalesce a list of bucket descriptors
 *                                   into the stack-based region array.
 *      - fhi_FindOldBuckets() to find replacement buckets
 *      - fh_am_move() with the stack-based region array if firehose movement
 *                     is required.
 */

firehose_private_t *
fh_acquire_remote_region(gasnet_node_t node, firehose_region_t *reg, 
		         firehose_completed_fn_t callback, void *context,
			 uint32_t flags, 
			 firehose_remotecallback_args_t *args)
{
 	uintptr_t	bucket_addr, end_addr, next_addr = 0;
	int		notpinned = 0, new_r = 0;
	fh_bucket_t	*bd;

	end_addr = reg->addr + (uintptr_t) reg->len - 1;

	FH_TABLE_LOCK;

 	FH_FOREACH_BUCKET(reg->addr, end_addr, bucket_addr) {
		bd = fh_bucket_lookup(node, bucket_addr);

		if (bd != NULL)
			fh_bucket_acquire(node, bd);
		else {
			fh_temp_buckets[notpinned] = bd;
			if (next_addr != bucket_addr)
				new_r++;

			next_addr = bucket_addr + FH_BUCKET_SIZE;
			notpinned++;
		}
	}

	/* In moving remote regions, none of the temp arrays can be used, as
	 * the AM call has to be done without holding the TABLE lock.  For this
	 * reason, alloca() is used to acquire a temporary array of regions.
	 */

	if (notpinned > 0) {
		int			i, replace_b, old_r = 0, free_b;
		void			*reg_alloc;
		firehose_region_t	*reg_alloc_new, *reg_alloc_old;
		int			args_len = 0;

		/* If the remote victim fifo is not full, no replacements are
		 * necessary */
		free_b = fhc_RemoteBucketsM - fhc_RemoteBucketsUsed[node];

		/* We figure out the number of replacement buckets that will
		 * have to be tagged on to the move request.  It's possible
		 * that the move is "free" if not all firehoses are used up */

		if (free_b > 0) {
			if (free_b >= notpinned) 
				replace_b = 0;
			else
				replace_b = notpinned - free_b;

		}
		else
			replace_b = notpinned;

		/* See if we need any args */
		/* XXX should make sure we are on a 4-byte boundary! */
		if (flags & FIREHOSE_FLAG_ENABLE_REMOTE_CALLBACK)
			args_len = sizeof(firehose_remotecallback_args_t);

		/* We've calculated 'new_r' regions will be sufficient for the
		 * replacement buckets and estimate a worst-case of 'replace_r'
		 * will be required for replacement firehoses 
		 * XXX should keep stats on the size of the alloca */
		reg_alloc = (void *)
			alloca(sizeof(firehose_region_t) * (new_r+replace_b) +
			       args_len);
		reg_alloc_new = (firehose_region_t *) reg_alloc + args_len;
		reg_alloc_old = reg_alloc_new + replace_b;

		if (args_len > 0)
			memcpy(reg_alloc, args, sizeof(firehose_region_t));

		/* Coalesce new buckets into a minimal amount of regions */
		new_r = fhi_CoalesceBuckets(fh_temp_buckets, notpinned,
				reg_alloc_new);

		/* Find replacement buckets if required */
		if (replace_b > 0) {
			if (fhc_RemoteVictimFifoBuckets[node] >= replace_b) {
				old_r = fhi_FreeVictimRemote
					(node, replace_b, reg_alloc_old); 
			}
			else {
				do {
					FH_TABLE_UNLOCK;
					gasnet_AMPoll();
					FH_TABLE_LOCK;
				} while 
				(fhc_RemoteVictimFifoBuckets[node] < replace_b);

				old_r = fhi_FreeVictimRemote
					(node, replace_b, reg_alloc_old); 
			}
		}

		fhc_RemoteBucketsUsed[node] += (notpinned - replace_b);

		FH_TABLE_UNLOCK;

		#ifdef FIREHOSE_UNBIND_CALLBACK
		if (old_r > 0)
			firehose_unbind_callback(node, reg_alloc_old, old_r);
		#endif

                MEDIUM_REQ(5, 8, 
                   (node, fh_handleridx(fh_am_move_reqh),
                    (void *) reg_alloc_new, 
		    (size_t) (new_r+old_r+args_len), flags,
		    new_r, old_r, PACK(callback), PACK(context)));

		return FH_REGION_UNPINNED;
	}
	else {
		bd = fh_bucket_lookup(node, reg->addr);
		FH_TABLE_UNLOCK;
		return bd;
	}
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
	int		i;
	uintptr_t	end_addr, bucket_addr;
	fh_bucket_t	*bd;

	FH_TABLE_ASSERT_LOCKED;

	end_addr = request->addr + request->len - 1;

	/* Process region in reverse order so regions can be later coalesced in
	 * the proper order (lower to higher address) from the FIFO */
	FH_FOREACH_BUCKET_REV(request->addr, end_addr, bucket_addr) {
		bd = fh_bucket_lookup(request->node, bucket_addr);

		if (fh_bucket_release(request->node, bd) == 0) {
			FH_TAILQ_INSERT_TAIL(
				&fh_RemoteNodeFifo[request->node],
				bd);
		}
	}

	return;
}

/* ##################################################################### */
/* ACTIVE MESSAGES                                                       */ 
/* ##################################################################### */
GASNET_INLINE_MODIFIER(fh_am_move_reqh_inner)
void
fh_am_move_reqh_inner(gasnet_token_t token, void *addr,
		      size_t nbytes,
		      gasnet_handlerarg_t flags,
		      gasnet_handlerarg_t new_num,
		      gasnet_handlerarg_t old_num,
		      void *callback,
		      void *context,
		      void *request_type)
{
	fhi_RegionFilter_t	rbuild_n, rbuild_o;

	gasneti_stattime_t      movetime = GASNETI_STATTIME_NOW_IFENABLED(C);
	gasneti_stattime_t      unpintime;
	gasnet_node_t		node;
	size_t			regreply_n;

	assert(new_num > 0);
	gasnet_AMGetMsgSource(token, &node);

	rbuild_n.regions_in = (firehose_region_t *) addr;
	rbuild_n.in = new_num;

	#ifdef FIREHOSE_UNEXPORT_CALLBACK
	if (old_num > 0)
		firehose_unexport_callback(
			node, (firehose_region_t *) addr + new_num, old_num);
	#endif

	FH_TABLE_LOCK;
	rbuild_n.regions_out = fh_temp_regions;

	fhi_AcquireLocalRegionsList(node, &rbuild_n);

	/* By building the list of regions to be pinned prior to seeing what
	 * regions can be unpinned, we allow the fifo lengths to reduce. */
	rbuild_o.regions_in = (firehose_region_t *) addr + new_num + 
			(flags & FIREHOSE_FLAG_ENABLE_REMOTE_CALLBACK ? 
			sizeof(firehose_remotecallback_args_t)
			: 0 );

	rbuild_o.in = old_num;
	rbuild_o.regions_out = fh_temp_regions + rbuild_n.out;

	fhi_ReleaseLocalRegionsList(node, &rbuild_o);

	/* Once the new and old regions are build, we can call the move
	 * callback. */
	firehose_move_callback(node, rbuild_o.regions_out, rbuild_o.out,
		rbuild_n.regions_out, rbuild_n.out);

	FH_TABLE_UNLOCK;

	regreply_n = rbuild_n.in;
	
	#ifdef FIREHOSE_EXPORT_CALLBACK
	if (rbuild_n.out > 0) {
		firehose_export_callback(node, 
			rbuild_n.regions_out, rbuild_n.out);
		regreply_n += rbuild_n.out;
	}
	#endif

	/* If the user requires to run a remote callback, and the
	 * callback is not to be run in place, run it */ 
	if (flags & FIREHOSE_FLAG_ENABLE_REMOTE_CALLBACK) {

		/* Client may be able to support callbacks for DMA
		 * operations within the AM handler */

		#ifdef FIREHOSE_REMOTE_CALLBACK_IN_HANDLER
			firehose_remote_callback(node, 
			    (const firehose_region_t *) rbuild_n.regions_in,
			    rbuild_n.in, 
			    (firehose_remotecallback_args_t *) addr);

			MEDIUM_REP(3,6,(token,
			    fh_handleridx(fh_am_move_reph),
			    rbuild_n.regions_in, 
			    regreply_n,
			    PACK(callback), PACK(context), PACK(request_type)));
		#else
			/* XXX ugghh.. malloc */
			fh_remote_callback_t *rc = 
			    (fh_remote_callback_t *)
			    gasneti_malloc(sizeof(fh_remote_callback_t));
			assert(rc != NULL);

			rc->pin_list = (firehose_region_t *)
				malloc(sizeof(firehose_region_t) * regreply_n);
			assert(rc->pin_list != NULL);
			memcpy(&(rc->pin_list), rbuild_n.regions_in, 
			       sizeof(firehose_region_t) * regreply_n);
			rc->reply_len = 
			       sizeof(firehose_region_t) * regreply_n;

			rc->pin_list_num = rbuild_n.in;
			rc->node = node;
			rc->flags = FH_CALLBACK_TYPE_COMPLETION;
			memcpy(&(rc->args), addr,
			    sizeof(firehose_remotecallback_args_t));
			rc->callback = callback;
			rc->context = context;
			rc->request = request_type;
	
			/* TODO */
			FH_POLLQ_LOCK;
			FH_STAILQ_INSERT_TAIL(&fh_CallbackFifo, 
				      (fh_callback_t *) rc);
			FH_POLLQ_UNLOCK;
		#endif
	}
	else {
		MEDIUM_REP(3,6,(token,
		    fh_handleridx(fh_am_move_reph),
		    rbuild_n.regions_in, 
		    regreply_n,
		    PACK(callback), PACK(context), PACK(request_type)));
	}

	return;
}
MEDIUM_HANDLER(fh_am_move_reqh,6,9,
              (token,addr,nbytes, a0, a1, a2, UNPACK(a3),      UNPACK(a4),
					      UNPACK(a5)                     ),
              (token,addr,nbytes, a0, a1, a2, UNPACK2(a3, a4), UNPACK2(a5, a6),
					      UNPACK2(a7, a8)                ));

/*
 * Firehose AM Reply
 *
 */
GASNET_INLINE_MODIFIER(fh_am_move_reph_inner)
void
fh_am_move_reph_inner(gasnet_token_t token, void *addr,
		      size_t nbytes,
		      gasnet_handlerarg_t new_num,
		      gasnet_handlerarg_t old_num,
		      void *callback,
		      void *context,
		      void *request_type)
{
	firehose_request_t *req = (firehose_request_t *) request_type;
	firehose_completed_fn_t func = (firehose_completed_fn_t) callback;
	firehose_region_t *regions = (firehose_region_t *) addr;
	gasnet_node_t	node;

	gasnet_AMGetMsgSource(token, &node);

	#ifdef FIREHOSE_BIND_CALLBACK
	if (num_regions > 0)
		firehose_bind_callback(node, (firehose_region_t *) addr,
				       num_regions);
	#endif

	/* Add the new buckets to the table */
	FH_TABLE_LOCK;
	fhi_InitRegionsList(node, regions, new_num);
	FH_TABLE_UNLOCK;

	if (callback != NULL) {
		fh_completion_callback_t *fcc = (fh_completion_callback_t *)
			gasneti_malloc(sizeof(fh_completion_callback_t));
		assert(fcc != NULL);

		fcc->flags = FH_CALLBACK_TYPE_COMPLETION;
		fcc->callback = callback;
		fcc->context = context;
		fcc->request = req;

		FH_POLLQ_LOCK;
		FH_STAILQ_INSERT_TAIL(&fh_CallbackFifo, 
				      (fh_callback_t *) fcc);
		FH_POLLQ_UNLOCK;
	}
}
MEDIUM_HANDLER(fh_am_move_reph,5,8,
              (token,addr,nbytes, a0, a1, UNPACK(a2),      UNPACK(a3),
					  UNPACK(a4)                     ),
              (token,addr,nbytes, a0, a1, UNPACK2(a2, a3), UNPACK2(a4, a5),
					  UNPACK2(a6, a7)                ));


void
fh_send_firehose_reply(fh_remote_callback_t *rc)
{
	MEDIUM_REQ(3,6,
	    (rc->node, fh_handleridx(fh_am_move_reph),
	    rc->pin_list, rc->reply_len, PACK(rc->callback),
	    PACK(rc->context), PACK(rc->request)));
}

/* indexes for firehose AM handlers */
static 
gasnet_handlerentry_t fh_am_handlers[] = {
	/* ptr-width dependent handlers */
	gasneti_handler_tableentry_with_bits(fh_am_move_reqh),
	gasneti_handler_tableentry_with_bits(fh_am_move_reph),
	{ 0, NULL }
};

extern gasnet_handlerentry_t * 
firehose_get_handlertable() {
	return fh_am_handlers;
}

#endif

#include <firehose.h>
#include <firehose_internal.h>
#include <gasnet.h>
#include <gasnet_handler.h>

#ifdef FIREHOSE_PAGE

/* 
 * The following define is used only when users do not specify 
 * the amount of memory required for the MACVICTIM_M parameter.
 */

#ifndef FH_MAXVICTIM_TO_PHYSMEM_RATIO
#define FH_MAXVICTIM_TO_PHYSMEM_RATIO	0.25
#endif

/* 
 * There is currently no support for bind callbacks in firehose-page 
 * as no client_t is currently envisioned in known -page clients. 
 */

#if defined(FIREHOSE_BIND_CALLBACK) || defined(FIREHOSE_UNBIND_CALLBACK)
  #error firehose-page currenty has no support for bind/unbind callbacks
#endif


#define FH_REGIONPOOL_DEFAULT_BUFNUM	32768

typedef
struct _fhi_RegionPool_t {
	/* Used internally */
	size_t		 		len;
	struct _fhi_RegionPool_t	*fh_tqe_next;

	/* User modifiable fields */
	firehose_region_t	*regions;
	size_t			 regions_num;
	size_t			 buckets_num;
	uint8_t			 _pad[FH_CACHE_LINE_BYTES-
				      3*sizeof(size_t)-2*sizeof(void*)];
}
fhi_RegionPool_t;

/* 
 * Freelist of FH_REGIONPOOL_DEFAULT_BUFSIZE buffers
 */
static
FH_STAILQ_HEAD(_fhi_regpool_list_t, _fhi_RegionPool_t)
fhi_regpool_list;

static int	fhi_regpool_num = 0;
static int	fhi_regpool_numbig = 0;


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
 * regA = 5 buckets, 3 unpinned to form 10101
 * regB = 3 buckets, 2 unpinned to form 101
 *
 * (5+3+1)/2 = 5
 */

#define FH_MIN_REGIONS_FOR_BUCKETS(buckets)	(((buckets)+1)>>1)
#define FH_MAX_BUCKETS_FOR_REGIONS(regions)	(((regions)<<1)-1)

/* ##################################################################### */
/* PAGE-SPECIFIC FUNCTIONS                                               */
/* ##################################################################### */
/*
 * Victim FIFO handling (remote and local)
 *
 * The local FIFO can be freed in order to replace existing pinned (but unused)
 * buckets for new ones.  The FIFO can be freed either by a local pin request
 * that frees unused buckets to pin new ones or by a local release which
 * happens to overcommit the FIFO.  In the latter case, enough LRU elements in
 * the FIFO are freed to remain within the established firehose limits.
 *
 */
int 	fhi_FreeVictimLocal(int buckets, firehose_region_t *);

int	fhi_FreeVictimRemote(gasnet_node_t, int buckets, firehose_region_t *);

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
 *
 * 3. AdjustLocalFifoAndPin(): Given a RegionPool of regions to pin, this
 *    function prepares the call to firehose_move_callback() by also including
 *    a list of regions to unpin if the victim FIFO was overcommitted.
 *      * called by fh_release_local_region() (in which case the RegionPool of
 *                  buckets to pin is NULL -- there are no new regions to pin)
 *	* called by fh_am_move_reqh()
 *
 */

int	fhi_AcquireLocalRegionsList(gasnet_node_t node, 
		firehose_region_t *region, size_t reg_num, 
		fhi_RegionPool_t *rpool);

void	fhi_ReleaseLocalRegionsList(gasnet_node_t node, firehose_region_t *reg, 
				size_t reg_num);

void	fhi_AdjustLocalFifoAndPin(gasnet_node_t node, fhi_RegionPool_t *rpool,
		int inhandler);


/* 
 * Remote region handling
 *
 * TryAcquireRemoteRegion builds a list of regions that are not pinned, while
 * FlushPendingRequests is called once a remote firehose move request is
 * completed in order to mark the remote buckets as pinned and make progress on
 * any firehose request waiting on the buckets to be pinned.
 *
 * CoalesceBuckets is a utility to minimize the number of regions required to
 * describe a list of buckets (contiguous buckets can be described by a single
 * region).
 *
 */
int	fhi_FlushPendingRequests(gasnet_node_t node, firehose_region_t *region,
			 int nreg, fh_pollq_t *PendQ);

int	fhi_TryAcquireRemoteRegion(gasnet_node_t node, firehose_request_t *req, 
			fh_completion_callback_t *ccb,
			firehose_region_t *reg, int *new_regions);

int	fhi_CoalesceBuckets(uintptr_t *bucket_addr_list, size_t num_buckets,
			    firehose_region_t *regions);


/*
 * Waiting/Polling for local and remote buckets
 *
 * Three 'Wait' type functions that may or may not call AMPoll() in order to
 * either respect firehose constraints or to acquire more resources.
 *
 * The WaitLocalBucketsInFlight() function stalls the local pin operating in
 * order to respect the upper bound on the total number of buckets in flight
 * (represented by the fhc_LocalOnlyBucketsInFlight counter).
 *
 * The WaitLocalBucketsToPin() functions talls on the total number of local
 * buckets pinned by the local node only (buckets which do not have any remote
 * firehose mappings).  The limit is established by the MAXVICTIM_M parameter
 * and respects the fhc_LocalVictimFifoBuckets counter.
 * 
 * TODO:  Firehose should implement a deadlock-free and starvation-free polling
 *        mechanism for threaded clients.
 *
 */
void	fhi_WaitLocalBucketsInFlight(int b_num);

int	fhi_WaitLocalBucketsToPin(int b_num, firehose_region_t *region);

int	fhi_WaitRemoteFirehosesToUnpin(gasnet_node_t node, int b_num, 
					firehose_region_t *region);

/* ##################################################################### */
/* LOCKS, BUFFERS AND QUEUES                                             */
/* ##################################################################### */

/* The following lock, referred to as the "table" lock, must be held for every
 * firehose operation that modifies the state of the firehose table.  It must
 * be held during most of the firehose operations - adding/removing to the hash
 * table, adding/removing from the local and victim FIFOs.
 */
gasneti_mutex_t		fh_table_lock = GASNETI_MUTEX_INITIALIZER;

/* This lock protects the poll FIFO queue, used to enqueue callbacks. */
gasneti_mutex_t		fh_pollq_lock = GASNETI_MUTEX_INITIALIZER;


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
 * Firehose FIFOs
 *
 */
fh_fifoq_t	fh_LocalFifo = FH_TAILQ_HEAD_INITIALIZER(fh_LocalFifo);
fh_fifoq_t	*fh_RemoteNodeFifo = NULL;

/* ##################################################################### */
/* COUNTERS                                                              */
/* ##################################################################### */
/* 
 * LOCAL COUNTERS
 *
 * fhc_LocalOnlyBucketsPinned - incrementing counter
 *     Amount of buckets pinned only for the local node (localref > 0 AND
 *     remoteref == 0).
 *
 * fhc_LocalOnlyBucketsInFlight - incrementing counter
 *     Total amount of local buckets currently touched (refcount incremented)
 *     by locally-initiated operations.  This count must be less than
 *     fhc_MaxVictimBuckets in order to avoid deadlocks.
 *
 * fhc_LocalVictimFifoBuckets - incrementing counter
 *     Amount of buckets currently contained in the Local Victim FIFO. 
 *
 * fhc_MaxVictimBuckets - static count
 *     Maximum amount of victims that may be pinned other than M.  At all
 *     fhc_LocalOnlyBucketsPinned + 
 *        fhc_LocalVictimFifoBuckets < fhc_MaxVictimBuckets
 *
 * XXX Currently unused. . will be useful in adaptive victim FIFO.
 * fhc_LocalBucketsPinned - incrementing counter
 *     Amount of buckets pinned as a result of remote requests to pin local
 *     buckets.  This parameter is bounded by 'M'.
 *
 */

int	fhc_LocalOnlyBucketsPinned;
int	fhc_LocalOnlyBucketsInFlight;
int	fhc_LocalVictimFifoBuckets;
int	fhc_MaxVictimBuckets;

#define FHC_MAXVICTIM_BUCKETS_AVAIL 					\
		(fhc_MaxVictimBuckets - fhc_LocalOnlyBucketsPinned)

/* 
 * REMOTE COUNTERS
 *
 * fhc_RemoteBucketsM - static count
 *    Amount of per-node firehoses that can be mapped as established by the
 *    firehose 'M' parameter.
 *
 * fhc_RemoteBucketsUsed[0..nodes-1] - Array of incrementing counters
 *    Amount of buckets currently used by the current node.
 *
 * fhc_RemoteVictimFifoBuckets[0..nodes-1] - Array of incrementing counters
 *     Available amount of remote buckets that can be used without sending
 *     replacement buckets.
 *
 */
int	 fhc_RemoteBucketsM;
int	*fhc_RemoteBucketsUsed;
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
	fh_bucket_t	*bd;

	FH_TABLE_ASSERT_LOCKED;
 	FH_FOREACH_BUCKET(addr, end_addr, bucket_addr) {
		bd = fh_bucket_lookup(node, bucket_addr);
		if (bd == NULL || 
		   (node != gasnet_mynode() && FH_IS_REMOTE_PENDING(bd)))
			return 0;
	}
	return 1;
}

fh_refc_t
fh_bucket_acquire(gasnet_node_t node, fh_bucket_t *entry)
{
	FH_TABLE_ASSERT_LOCKED;
	
	/* If the bucket is a local, if can contain both local and remote
	 * reference counts. */
	assert(entry != NULL);

	if (FH_NODE(entry) == gasnet_mynode()) {
		int	loc = (node == gasnet_mynode());

		if (FH_IS_LOCAL_FIFO(entry)) {
			FH_TAILQ_REMOVE(&fh_LocalFifo, entry);
			assert(FH_NODE(entry) == gasnet_mynode());
			assert(entry->fh_state == fh_local_fifo);
			FH_REFCSET(FH_REFCOUNT(entry), loc, !loc);
			fhc_LocalOnlyBucketsPinned -= !loc;
			fhc_LocalVictimFifoBuckets--;
			entry->fh_state = fh_used;
			FH_SET_USED(entry);
			FH_TRACE_BUCKET(entry, ACQFIFO);
			return 1;
		}
		else {
			FH_SET_USED(entry);
			assert(entry->fh_state == fh_used);
			if (loc) {
				FH_LREFCINC(FH_REFCOUNT(entry));
				FH_TRACE_BUCKET(entry, ACQUIRE);
				return FH_LREFC(FH_REFCOUNT(entry));
			}
			else {
				if (FH_RREFC(FH_REFCOUNT(entry)) == 0) {
					assert(FH_LREFC(
					    FH_REFCOUNT(entry) > 0));
					fhc_LocalOnlyBucketsPinned--;
				}
				FH_RREFCINC(FH_REFCOUNT(entry));
				FH_TRACE_BUCKET(entry, ACQUIRE);
				return FH_RREFC(FH_REFCOUNT(entry));
			}
		}
	}

	/* If the bucket is a remote bucket, the node cannot by equal to
	 * gasnet_mynode() */
	else {
		assert(node != gasnet_mynode());

		if (FH_IS_REMOTE_FIFO(entry)) {
			FH_TAILQ_REMOVE(&fh_RemoteNodeFifo[node], entry);
			assert(FH_NODE(entry) != gasnet_mynode());
			assert(entry->fh_state == fh_remote_fifo);
			fhc_RemoteVictimFifoBuckets[node]--;
			FH_REFCSET(FH_REFCOUNT(entry), 0, 1);
			FH_SET_USED(entry);
			entry->fh_state = fh_used;
			FH_TRACE_BUCKET(entry, ACQFIFO);
			return 0;
		}
		else {
			/* Pending buckets must be handled separately */
			assert(entry->fh_state == fh_used);
			assert(!FH_IS_REMOTE_PENDING(entry));
			FH_RREFCINC(FH_REFCOUNT(entry));
			FH_TRACE_BUCKET(entry, ACQUIRE);
			return FH_RREFC(FH_REFCOUNT(entry));
		}
	}
}

fh_refc_t
fh_bucket_release(gasnet_node_t node, fh_bucket_t *entry)
{
	FH_TABLE_ASSERT_LOCKED;

	/* Make sure the reference count to be decremented is greater
	 * than 0 if the release is local or remote */
	/* XXX perhaps expose this error to client with fatalerror() ? */

	assert(entry != NULL);
	assert(entry->fh_state == fh_used || 
	    (printf("state=%d, node=%d, entry_node=%d, addr=%p\n", entry->fh_state,
		    gasnet_mynode(), FH_NODE(entry), (void *) FH_BADDR(entry)),0));
	/*
	assert(node == gasnet_mynode() 
		? FH_LREFC(FH_REFCOUNT(entry)) > 0
		: FH_RREFC(FH_REFCOUNT(entry)) > 0);
		*/

	/* Deal with local buckets, which can contain local and remote
	 * refcounts */
	if (FH_NODE(entry) == gasnet_mynode()) {
		int		loc = (node == gasnet_mynode());
		fh_refc_t	ret;

		assert(!FH_IS_LOCAL_FIFO(entry));

		if (loc) {
			FH_LREFCDEC(FH_REFCOUNT(entry));
			ret = FH_LREFC(FH_REFCOUNT(entry));
		}
		else {
			FH_RREFCDEC(FH_REFCOUNT(entry));
			ret = FH_RREFC(FH_REFCOUNT(entry));
		}

		if (FH_REFC_IS_VICTIM(FH_REFCOUNT(entry))) {
			FH_TAILQ_INSERT_TAIL(&fh_LocalFifo, entry);
			assert(FH_IS_LOCAL_FIFO(entry));
			assert(FH_NODE(FH_TAILQ_FIRST(&fh_LocalFifo)) 
					== gasnet_mynode());
			fhc_LocalOnlyBucketsPinned += !loc;
			fhc_LocalVictimFifoBuckets++;
			entry->fh_state = fh_local_fifo;
			FH_TRACE_BUCKET(entry, ADDFIFO);
			return 0;
		}
		else {
			FH_SET_USED(entry);
			if (FH_RREFC(FH_REFCOUNT(entry)) == 0 && !loc)
				fhc_LocalOnlyBucketsPinned++;

			FH_TRACE_BUCKET(entry, RELEASE);
			return ret;
		}
	}
	/* The bucket is a remote bucket, and it cannot contain any local
	 * refcounts.  Also, it should not be pending as pending buckets are
	 * handled separately */
	else {
		assert(node != gasnet_mynode());
		assert(!FH_IS_REMOTE_PENDING(entry));

		FH_RREFCDEC(FH_REFCOUNT(entry));
		fh_refc_t refc = FH_RREFC(FH_REFCOUNT(entry));
		if (refc == 0) {
			FH_TAILQ_INSERT_TAIL(
			    &fh_RemoteNodeFifo[node], entry);
			assert(FH_NODE(FH_TAILQ_FIRST(
			    &fh_RemoteNodeFifo[node])) != gasnet_mynode());
			fhc_RemoteVictimFifoBuckets[node]++;
			entry->fh_state = fh_remote_fifo;
			FH_TRACE_BUCKET(entry, ADDFIFO);
			return 0;
		}
		else {
			FH_TRACE_BUCKET(entry, RELEASE);
			return refc;
		}
	}
}

/* fh_init_plugin()
 *
 * This function is only called from firehose_init and allows -page OR -region
 * to run plugin specific code.
 */

void
fh_init_plugin(uintptr_t max_pinnable_memory, size_t max_regions, 
	      firehose_region_t *regions, size_t num_reg,
	      firehose_info_t *fhinfo)
{
	int		i;
	unsigned long	M, maxvictim, firehoses, m_prepinned = 0;
	size_t		b_prepinned = 0;

	assert(FH_MAXVICTIM_TO_PHYSMEM_RATIO >= 0 && 
	       FH_MAXVICTIM_TO_PHYSMEM_RATIO <= 1);

	/* In -page, we ignore regions. . there should not be a limit on the
	 * number of regions */

	if (max_regions != 0)
		gasneti_fatalerror("firehose-page does not support a "
				   "limitation on the number of regions");

	/* Find how many buckets the client pinned need to hashed */
	{
		uintptr_t	bucket_addr, end_addr;
		fh_bucket_t	*bd;
		int		i;

		for (i = 0; i < num_reg; i++) {

			if (regions[i].addr % FH_BUCKET_SIZE != 0)
				gasneti_fatalerror("firehose_init: prepinned "
				    "region is not aligned on a bucket "
				    "boundary (addr = %p)", 
				    (void *) regions[i].addr);

			if (regions[i].len % FH_BUCKET_SIZE != 0)
				gasneti_fatalerror("firehose_init: prepinned "
				    "region is not a multiple of firehose "
				    "bucket size in length (len = %d)",
				    regions[i].len);

			b_prepinned +=
				FH_NUM_BUCKETS(regions[i].addr,regions[i].len);

		}
		/* Initialize bucket freelist with the total amount of buckets
		 * to be pinned */
		fh_bucket_init_freelist(firehoses
			+ fhc_MaxVictimBuckets + b_prepinned);

		/* Now add all the prepinned buckets and marked them as
		 * prepinned (which makes acquire/release of each bucket a
		 * no-op */
		for (i = 0; i < num_reg; i++) {
			end_addr = regions[i].addr + regions[i].len - 1;
			FH_FOREACH_BUCKET(regions[i].addr, 
	 				  end_addr, bucket_addr) {

				bd = fh_bucket_add(gasnet_mynode(), 
						   bucket_addr);

				bd->fh_state = fh_used;
				FH_SET_USED(bd);
				FH_TRACE_BUCKET(bd, ADDING PREPINNED);
			}
		}
	}

	/* Allocate the per-node counters */
	fhc_RemoteBucketsUsed = (int *)
		gasneti_malloc(gasnet_nodes() * sizeof(int));
	memset(fhc_RemoteBucketsUsed, 0, gasnet_nodes() * sizeof(int));

	fhc_RemoteVictimFifoBuckets = (int *)
		gasneti_malloc(gasnet_nodes() * sizeof(int));
	memset(fhc_RemoteVictimFifoBuckets, 0, gasnet_nodes() * sizeof(int));

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
			    "than the minimum %d (%d buckets)", M_min, 
			    M_min >> FH_BUCKET_SHIFT);

		if_pf (maxvictim < maxvictim_min)
			gasneti_fatalerror("GASNET_MAXVICTIM_M is less than the "
			    "minimum %d (%d buckets)", maxvictim_min,
			    maxvictim_min >> FH_BUCKET_SHIFT);

		if_pf (M + m_prepinned < M_min)
			gasneti_fatalerror("Too many buckets passed on initial"
			    " pinned bucket list (%d) for current "
			    "GASNET_FIREHOSE_M parameter (%d)", 
			    b_prepinned, M);
	}

	/* 
	 * Set local parameters
	 */
	fhc_LocalOnlyBucketsPinned = b_prepinned;
	fhc_LocalVictimFifoBuckets = 0;
	fhc_LocalOnlyBucketsInFlight = 0;
	fhc_MaxVictimBuckets = (maxvictim + m_prepinned) >> FH_BUCKET_SHIFT;

	/* 
	 * Set remote parameters
	 */
	firehoses = (M - m_prepinned) >> FH_BUCKET_SHIFT;
	fhc_RemoteBucketsM = gasnet_nodes() > 1
				? firehoses / (gasnet_nodes()-1)
				: firehoses;
	for (i = 0; i < gasnet_mynode(); i++) {
		fhc_RemoteVictimFifoBuckets[i] = 0;
		fhc_RemoteBucketsUsed[i] = 0;
	}

	/* 
	 * Set fields in the firehose information type, according to the limits
	 * established by the firehose parameters.
	 */
	{
		unsigned	firehose_limits = 
				    fhc_RemoteBucketsM * FH_BUCKET_SIZE;
		unsigned	med_regions, med_buckets, new_buckets;
		unsigned	med_maxbytes, fh_maxbytes;

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
		med_maxbytes = (med_buckets/2) * FH_BUCKET_SIZE;
		fh_maxbytes  = fhc_RemoteBucketsM * FH_BUCKET_SIZE;

		fhinfo->max_RegionsLocal = 0;
		fhinfo->max_RegionsRemote = 0;

		fhinfo->max_LocalPinSize  = 
		    fhc_MaxVictimBuckets * FH_BUCKET_SIZE;
		fhinfo->max_RemotePinSize = MIN(med_maxbytes, fh_maxbytes);
		fh_max_regions = 
			FH_MIN_REGIONS_FOR_BUCKETS(
			    fhinfo->max_RemotePinSize >> FH_BUCKET_SHIFT);

		GASNETI_TRACE_PRINTF(C, 
		    ("Firehose M=%ld (fh=%ld)\tprepinned=%ld (buckets=%d)",
		    M, firehoses, m_prepinned, b_prepinned));
		GASNETI_TRACE_PRINTF(C, ("Firehose Maxvictim=%ld (fh=%ld)",
		    maxvictim, fhc_MaxVictimBuckets));

		GASNETI_TRACE_PRINTF(C, 
		    ("MaxLocalPinSize=%d\tMaxRemotePinSize=%d", 
		    fhinfo->max_LocalPinSize, fhinfo->max_RemotePinSize));
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

	FH_STAILQ_INIT(&fhi_regpool_list);

	return;
}

void
fh_fini_plugin()
{
	int	i;
	fhi_RegionPool_t	*rpool;

	while (!FH_STAILQ_EMPTY(&fhi_regpool_list)) {
		rpool = FH_STAILQ_FIRST(&fhi_regpool_list);
		FH_STAILQ_REMOVE_HEAD(&fhi_regpool_list);
		gasneti_free(rpool);
	}

	gasneti_free(fhc_RemoteBucketsUsed);
	gasneti_free(fhc_RemoteVictimFifoBuckets);


	return;
}

/* ##################################################################### */
/* PAGE-SPECIFIC INTERNAL FUNCTIONS                                      */
/* ##################################################################### */
/* ####################################### */
/* Conditional wait and polling functions  */
/* ####################################### */

#ifndef FHI_DONTUSE_POOL

fhi_RegionPool_t *
fhi_AllocRegionPool(b_num)
{
	fhi_RegionPool_t *rpool;

	FH_TABLE_ASSERT_LOCKED;

	rpool = FH_STAILQ_FIRST(&fhi_regpool_list);

	if_pf (b_num > FH_REGIONPOOL_DEFAULT_BUFNUM || rpool == NULL) {

		rpool = (fhi_RegionPool_t *) 
			gasneti_malloc(sizeof(fhi_RegionPool_t));
		rpool->regions_num = 0;
		rpool->buckets_num = 0;

		if (b_num > FH_REGIONPOOL_DEFAULT_BUFNUM) {
			rpool->len     = sizeof(firehose_region_t) * b_num;
			rpool->regions = (firehose_region_t *) 
					    gasneti_malloc(rpool->len);
			if_pf (rpool->regions == NULL)
				gasneti_fatalerror("malloc in RegionPool");
			fhi_regpool_numbig++;
			return rpool;
		}
		else {
			b_num = FH_REGIONPOOL_DEFAULT_BUFNUM;
			rpool->len     = FH_REGIONPOOL_DEFAULT_BUFNUM * 
						sizeof(firehose_region_t);
			rpool->regions = (firehose_region_t *) 
					    gasneti_malloc(rpool->len);
			if_pf (rpool->regions == NULL)
				gasneti_fatalerror("malloc in RegionPool");

			FH_STAILQ_INSERT_HEAD(&fhi_regpool_list, rpool);
			assert(!FH_STAILQ_EMPTY(&fhi_regpool_list));
			assert(FH_STAILQ_FIRST(&fhi_regpool_list) == rpool);

			/*
			 * This time, the freelist should hit an allocation
			 */
			fhi_regpool_num++;
			return fhi_AllocRegionPool(b_num);
		}
	}
	else {
		FH_STAILQ_REMOVE_HEAD(&fhi_regpool_list);
		return rpool;
	}
}

void
fhi_FreeRegionPool(fhi_RegionPool_t *rpool)
{
	FH_TABLE_ASSERT_LOCKED;

	if_pf (rpool->len > 
	   FH_REGIONPOOL_DEFAULT_BUFNUM*sizeof(firehose_region_t)) {
		gasneti_free(rpool->regions);
		gasneti_free(rpool);
	}
	else {
		rpool->regions_num = 0;
		rpool->buckets_num = 0;
		FH_STAILQ_INSERT_HEAD(&fhi_regpool_list, rpool);
		assert(FH_STAILQ_FIRST(&fhi_regpool_list) == rpool);
	}

	return;
}

#else
fhi_RegionPool_t *
fhi_AllocRegionPool(b_num)
{
	fhi_RegionPool_t *rpool;

	FH_TABLE_ASSERT_LOCKED;

	rpool = (fhi_RegionPool_t *) gasneti_malloc(sizeof(fhi_RegionPool_t));
	if_pf (rpool == NULL)
		gasneti_fatalerror("malloc");
	rpool->len = sizeof(firehose_region_t) * b_num;
	rpool->regions = (firehose_region_t *) gasneti_malloc(rpool->len);
	rpool->regions_num = 0;
	rpool->buckets_num = 0;
	
	return rpool;
}

void
fhi_FreeRegionPool(fhi_RegionPool_t *rpool)
{
	FH_TABLE_ASSERT_LOCKED;

	gasneti_free(rpool->regions);
	gasneti_free(rpool);

	return;
}
#endif

void
fhi_WaitLocalBucketsInFlight(int b_num)
{
	int	b_remain, b_avail;

	FH_TABLE_ASSERT_LOCKED;

	if (fhc_LocalOnlyBucketsInFlight < fhc_MaxVictimBuckets)
		return;

	GASNETI_TRACE_PRINTF(C, ("Firehose Outstanding number of "
	    "buckets in flight reached threshold %d", fhc_MaxVictimBuckets));

	b_remain = b_num;

	while (b_remain > 0) {
		b_avail = MIN(b_remain, 
		        fhc_MaxVictimBuckets - fhc_LocalOnlyBucketsInFlight);

		if (b_avail > 0) {
			fhc_LocalOnlyBucketsInFlight += b_avail;
			b_remain -= b_avail;
		}
		else {
			FH_TABLE_UNLOCK;
			gasnet_AMPoll();
			FH_TABLE_LOCK;
		}
	}

	return;
}

int
fhi_WaitLocalBucketsToPin(int b_num, firehose_region_t *region)
{
	int			b_remain, b_avail, r_freed;
	firehose_region_t	*reg = region;

	FH_TABLE_ASSERT_LOCKED;

	assert(fhc_MaxVictimBuckets - fhc_LocalOnlyBucketsPinned >= 0);
	b_avail = MIN(b_num, fhc_MaxVictimBuckets - fhc_LocalOnlyBucketsPinned);
	fhc_LocalOnlyBucketsPinned += b_avail;

	b_remain = b_num - b_avail;

	if (b_remain == 0)
		return 0;

	GASNETI_TRACE_PRINTF(C, ("Firehose Polls Local pinned needs to recover"
	    " %d buckets from FIFO (currently %d buckets)", b_remain,
	    fhc_LocalVictimFifoBuckets));

	while (b_remain > 0) {
		b_avail = MIN(b_remain, fhc_LocalVictimFifoBuckets);

		if (b_avail > 0) {
			/* Adjusts LocalVictimFifoBuckets count */
			r_freed = fhi_FreeVictimLocal(b_avail, reg);
			fhc_LocalVictimFifoBuckets -= b_avail;
			b_remain -= b_avail;
			reg += r_freed;
		}
		else {
			FH_TABLE_UNLOCK;
			gasnet_AMPoll();
			FH_TABLE_LOCK;
		}
	}

	assert(FHC_MAXVICTIM_BUCKETS_AVAIL >= 0);
	assert(reg - region >= 0);

	/* When the function returns, rpool contains regions to be unpinned. */
	return (int) (reg - region);
}

/*
 * Fills in a list of regions that can be used as replacement regions in a
 * remote pin request.
 *
 * The number of buckets contained in the sum of all regions is always equal to
 * 'b_num', the number of replacement buckets requested.
 */
int
fhi_WaitRemoteFirehosesToUnpin(gasnet_node_t node, int b_num, 
				firehose_region_t *region)
{
	int			b_remain, b_avail, r_freed;
	firehose_region_t	*reg = region;

	FH_TABLE_ASSERT_LOCKED;

	b_remain = b_num;

	GASNETI_TRACE_PRINTF(C, 
	   ("Firehose Polls Remote firehoses requires %d buckets (FIFO=%d)",
	   b_num, fhc_RemoteVictimFifoBuckets[node]));

	while (b_remain > 0) {
		b_avail = MIN(b_remain, fhc_RemoteVictimFifoBuckets[node]);

		if (b_avail > 0) {
			r_freed = fhi_FreeVictimRemote(node, b_remain, reg);
			fhc_RemoteVictimFifoBuckets[node] -= b_remain;
			b_remain -= b_avail;
			reg += r_freed;
		}
		else {
			FH_TABLE_UNLOCK;
			gasnet_AMPoll();
			FH_TABLE_LOCK;
		}
	}

	assert(fhc_RemoteVictimFifoBuckets[node] >= 0);
	assert(reg - region > 0);

	return (int) (reg - region);
}

/* Check that the local victim fifo is not overcommitted.
 *
 * Always pin from rpool_pin if it is non-null
 */

void
fhi_AdjustLocalFifoAndPin(gasnet_node_t node, fhi_RegionPool_t *rpool_pin, 
			  int inhandler)
{
	int			b_unpin, pin_num;
	firehose_region_t	*reg_pin;

	FH_TABLE_ASSERT_LOCKED;

	if (rpool_pin != NULL) {
		reg_pin = rpool_pin->regions;
		pin_num = rpool_pin->regions_num;
	}
	else {
		reg_pin = NULL;
		pin_num = 0;
	}

	/* Check if the local FIFO is overcommitted.  If so, we build a list of
	 * regions to unpin from the head of the FIFO (oldest victim).*/
	b_unpin = fhc_LocalOnlyBucketsPinned - fhc_MaxVictimBuckets;

	if (b_unpin > 0) {
		GASNETI_TRACE_PRINTF(C, 
		    ("Firehose Overcommitted FIFO by %d buckets", b_unpin));

		fhi_RegionPool_t *rpool = fhi_AllocRegionPool(b_unpin);
		rpool->buckets_num = b_unpin;
		rpool->regions_num =
			fhi_FreeVictimLocal(b_unpin, rpool->regions);

		fhc_LocalVictimFifoBuckets -= b_unpin;
		fhc_LocalOnlyBucketsPinned -= b_unpin;
		assert(FHC_MAXVICTIM_BUCKETS_AVAIL >= 0);

		FH_TABLE_UNLOCK;
		firehose_move_callback(node, inhandler, rpool->regions, 
				rpool->regions_num, reg_pin, pin_num);
		FH_TABLE_LOCK;

		fhi_FreeRegionPool(rpool);
	}
	else if (pin_num > 0) {
		FH_TABLE_UNLOCK;
		firehose_move_callback(node, inhandler,
				       NULL, 0, reg_pin, pin_num);
		FH_TABLE_LOCK;
	}
	return;
}


/* ################################################## */
/* Internal acquiring of regions on page granularity  */
/* ################################################## */
/* fhi_AcquireLocalRegionsList
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
 * The function returns the amount of buckets (not regions) contained in the
 * buildregion type (the amount of regions can be queried from the type).
 *
 * The rpool is updated to reflect the number of buckets and regions that were
 * written.
 */

int
fhi_AcquireLocalRegionsList(gasnet_node_t node, firehose_region_t *region,
			    size_t reg_num, fhi_RegionPool_t *rpool)
{
	int			i, j, buckets_topin;
	firehose_region_t	*reg = rpool->regions;
	uintptr_t		bucket_addr, end_addr, next_addr;
	fh_bucket_t		*bd;

	buckets_topin = 0;
	rpool->regions_num = 0;
	rpool->buckets_num = 0;

	for (i = 0, j = -1; i < reg_num; i++) {

		end_addr = region[i].addr + region[i].len - 1;
				
 		FH_FOREACH_BUCKET(region[i].addr, end_addr, bucket_addr) 
		{
			assert(bucket_addr > 0);
			bd = fh_bucket_lookup(gasnet_mynode(), bucket_addr);

			if (bd != NULL) {
				/* The bucket is already pinned */
				fh_bucket_acquire(node, bd);
				assert(bd->fh_tqe_next == (fh_bucket_t *) -1);
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
					rpool->regions_num++;
				}
				rpool->buckets_num++;
			}

			next_addr = bucket_addr + FH_BUCKET_SIZE;
		}
	}

	return rpool->buckets_num;
}

/* fhi_ReleaseLocalRegionsList
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
fhi_ReleaseLocalRegionsList(gasnet_node_t node, firehose_region_t *reg, 
				size_t reg_num)
{
	int			i;
	uintptr_t		bucket_addr, end_addr;
	fh_bucket_t		*bd;

	FH_TABLE_ASSERT_LOCKED;

	for (i = 0; i < reg_num; i++) {
		end_addr = reg[i].addr + reg[i].len - 1;

		GASNETI_TRACE_PRINTF(C, 
		    ("Firehose ReleaseLocalRegions (%p, %d)",
		    reg[i].addr, reg[i].len));
				
 		FH_FOREACH_BUCKET_REV(reg[i].addr, end_addr, bucket_addr) 
		{
			bd = fh_bucket_lookup(gasnet_mynode(), bucket_addr);
			assert(bd != NULL);

			fh_bucket_release(node, bd);
		}
	}
	return;
}

/* ####################### */
/* Victim FIFO interfaces  */
/* ####################### */
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
	assert(buckets == j+1);
	return j+1;
}

/* fhi_FreeVictimLocal(buckets, reg)
 *
 * FreeVictim for the local bucket fifo.
 */
int 
fhi_FreeVictimLocal(int buckets, firehose_region_t *reg)
{
	assert(buckets <= fhc_LocalVictimFifoBuckets);
	return _fhi_FreeVictim(buckets, reg, &fh_LocalFifo);
}

/* fhi_FreeVictimRemote(node, buckets, reg)
 *
 * FreeVictim for the local bucket fifo.
 */
int
fhi_FreeVictimRemote(gasnet_node_t node, int buckets, firehose_region_t *reg)
{
	assert(buckets <= fhc_RemoteVictimFifoBuckets[node]);
	return _fhi_FreeVictim(buckets, reg, &fh_RemoteNodeFifo[node]);
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
fhi_CoalesceBuckets(uintptr_t *bucket_list, size_t num_buckets,
		firehose_region_t *regions)
{
	int		i, j = -1; /* new buckets created */
	fh_bucket_t	*bd;
	uintptr_t	addr_next = 0, bucket_addr;

	FH_TABLE_ASSERT_LOCKED;

	assert(num_buckets > 0);
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

	assert(regions[j].addr > 0);
	return j+1;
}
	

/* ##################################################################### */
/* LOCAL PINNING                                                         */
/* ##################################################################### */
/* fhi_InitLocalRegionsList(region, reg_num)
 *
 * This function adds all the buckets contained in the list of regions to the
 * hash table and initializes either the local or remote refcount to 1.
 *
 * It is only used as part of fh_acquire_local_region().
 *
 */
void
fhi_InitLocalRegionsList(gasnet_node_t node, firehose_region_t *region, 
					      int numreg)
{
	uintptr_t	end_addr, bucket_addr;
	fh_bucket_t	*bd;
	int		i;
	unsigned	loc, rem;

	if (node == gasnet_mynode()) {
		loc = 1;
		rem = 0;
	}
	else {
		loc = 0;
		rem = 1;
	}

	FH_TABLE_ASSERT_LOCKED;

	/* Once pinned, We can walk over the regions to be pinned and
	 * set the reference count to 1. */
	for (i = 0; i < numreg; i++) {
		end_addr = region[i].addr + region[i].len - 1;

		assert(region[i].addr > 0);

		FH_FOREACH_BUCKET(region[i].addr,end_addr,bucket_addr) {
			bd = fh_bucket_add(gasnet_mynode(), bucket_addr);
			bd->fh_state = fh_used;
			FH_REFCSET(FH_REFCOUNT(bd), loc, rem);

			FH_TRACE_BUCKET(bd, INIT);
		}
	}

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
 * Called by fh_local_pin() (firehose_local_pin, firehose_local_try_pin)
 */

void
fh_acquire_local_region(firehose_region_t *region)
{
	int			b_num, b_total;
	fhi_RegionPool_t	*pin_p;


	FH_TABLE_ASSERT_LOCKED;

	b_total = FH_NUM_BUCKETS(region->addr, region->len);
	/* Make sure the size of the region respects the local limits */
	assert(b_total <= fhc_MaxVictimBuckets);

	pin_p = fhi_AllocRegionPool(FH_MIN_REGIONS_FOR_BUCKETS(b_total));
	b_num = fhi_AcquireLocalRegionsList(
			gasnet_mynode(), region, 1, pin_p);

	/* b_num contains the amount of new regions to be pinned.  We may have
	 * to unpin Buckets in order to respect the threshold on locally pinned
	 * buckets. */
	if (b_num > 0) {
		fhi_RegionPool_t	*unpin_p;

		unpin_p = fhi_AllocRegionPool(b_num);
		unpin_p->regions_num = 
			fhi_WaitLocalBucketsToPin(b_num, unpin_p->regions);

		/* Make sure we don't exceed the threshold for buckets in
		 * flight. This may stall until enough buckets are recovered.
	 	 */
		fhi_WaitLocalBucketsInFlight(b_total);

		FH_TABLE_UNLOCK;
		firehose_move_callback(gasnet_mynode(), 0,
				unpin_p->regions, unpin_p->regions_num,
				pin_p->regions, pin_p->regions_num);
		FH_TABLE_LOCK;

		fhi_InitLocalRegionsList(gasnet_mynode(), 
					 pin_p->regions, pin_p->regions_num);

		fhi_FreeRegionPool(unpin_p);
	}
	else	/* Simply wait/increment for InFlight buckets */
		fhi_WaitLocalBucketsInFlight(b_total);

	fhi_FreeRegionPool(pin_p);

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
fhi_FlushPendingRequests(gasnet_node_t node, firehose_region_t *region,
			 int nreg, fh_pollq_t *PendQ)
{
	int		numpend = 0, callspend = 0;
	uintptr_t	base_addr, end_addr, bucket_addr;
	fh_bucket_t	*bd, *bdi;
	int		i;

	fh_completion_callback_t	*ccb, *ccbn;
	firehose_request_t		*req;

	FH_TABLE_ASSERT_LOCKED;	/* uses fh_temp_bucket_ptrs */
	assert(node != gasnet_mynode());

	FH_STAILQ_INIT(PendQ);

	for (i = 0; i < nreg; i++) {
		end_addr = region[i].addr + region[i].len - 1;
		assert(region[i].addr > 0);

		FH_FOREACH_BUCKET(region[i].addr,end_addr,bucket_addr) {
			bd = fh_bucket_lookup(node, bucket_addr);
			assert(bd != NULL);

			/* Make sure the bucket was set as pending */
			assert(FH_IS_REMOTE_PENDING(bd));
			assert(bd->fh_state == fh_pending);
			assert(bd->fh_tqe_next != NULL);

			/* if there is a pending request on the bucket, save it
			 * in the temp array */
			fh_temp_bucket_ptrs[numpend] = bd;
			numpend++;
			assert(numpend < fh_max_regions); 
			FH_UNSET_REMOTE_PENDING(bd);
			bd->fh_state = fh_used;
		}
	}

	/* Each 'bd' is confirmed to be pinned and contains a pending request.
	 *
	 * For each pending request 
	 *
	 */
	for (i = 0; i < numpend; i++) {
		bd = fh_temp_bucket_ptrs[i];
		base_addr = FH_BADDR(bd) + FH_BUCKET_SIZE;
		ccb = (fh_completion_callback_t *) bd->fh_tqe_next;

		FH_SET_USED(bd);

		assert(ccb != NULL);
		while (ccb != FH_COMPLETION_END)
		{
			bd->fh_tqe_next = (fh_bucket_t *) ccb->fh_tqe_next;
			assert(ccb->flags & FH_CALLBACK_TYPE_COMPLETION);
			req = ccb->request;
			assert(req && req->flags & FH_FLAG_PENDING);

			GASNETI_TRACE_PRINTF(C,
			    ("Firehose Pending FLUSH bd=%p (%p,%d), req=%p",
			     bd, (void *) FH_BADDR(bd), FH_NODE(bd), req));

			/* Assume no other buckets are pending */
			req->flags &= ~FH_FLAG_PENDING;
			end_addr = req->addr + req->len - 1;

			/* Walk through each bucket in the region until a
			 * pending bucket is found.  If none can be found, the
			 * callback can be called.
			 */
			FH_FOREACH_BUCKET(base_addr, end_addr, bucket_addr) {
				bdi = fh_bucket_lookup(node, bucket_addr);
				assert(bdi != NULL);

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
				     (void *) req->addr, req->len, req));
				callspend++;
			}

			ccb = (fh_completion_callback_t *) bd->fh_tqe_next;
		} 
	}

	return callspend;
}

void
fh_commit_try_remote_region(gasnet_node_t node, uintptr_t addr, size_t nbytes)
{
	uintptr_t	bucket_addr, end_addr  = addr + nbytes - 1;
	fh_bucket_t	*bd;

	FH_TABLE_ASSERT_LOCKED;

 	FH_FOREACH_BUCKET(addr, end_addr, bucket_addr) {
		bd = fh_bucket_lookup(node, bucket_addr);
		fh_bucket_acquire(node, bd);
	}
	return;
}


/* fh_release_local_region(request)
 *
 * DECrements/unpins pages covered in 
 *     [request->addr, request->addr+request->len].
 */
void
fh_release_local_region(firehose_request_t *request, int inhandler)
{
	firehose_region_t	reg;
	int			b_unpin, b_total, b_num;

	FH_TABLE_ASSERT_LOCKED;

	b_total = FH_NUM_BUCKETS(request->addr, request->len);
	FH_COPY_REQUEST_TO_REGION(&reg, request);


	fhi_ReleaseLocalRegionsList(gasnet_mynode(), &reg, 1);
	fhi_AdjustLocalFifoAndPin(gasnet_mynode(), NULL, inhandler);

	fhc_LocalOnlyBucketsInFlight -= b_total;

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
fhi_TryAcquireRemoteRegion(gasnet_node_t node, firehose_request_t *req, 
			fh_completion_callback_t *ccb,
			firehose_region_t *reg, int *new_regions)
{
 	uintptr_t	bucket_addr, end_addr, next_addr = 0;
	int		unpinned = 0;
	int		new_r = 0, b_num;
	fh_bucket_t	*bd;

	fh_completion_callback_t *ccba;

	end_addr = reg->addr + (uintptr_t) reg->len - 1;

	FH_TABLE_ASSERT_LOCKED;

	assert(req != NULL);
	assert(node != gasnet_mynode());

	b_num = FH_NUM_BUCKETS(reg->addr, reg->len);

	/* Make sure the number of buckets doesn't exceed the maximum number of
	 * regions required to describe these buckets */
	assert(b_num <= fh_max_regions); 

 	FH_FOREACH_BUCKET(reg->addr, end_addr, bucket_addr) {
		bd = fh_bucket_lookup(node, bucket_addr);

		if (bd != NULL) {
			/* If the bucket is pending and the current request
			 * does not have a callback associated to it yet,
			 * allocate it */
			if (FH_IS_REMOTE_PENDING(bd)) {
				assert(bd->fh_tqe_next != NULL && 
				       bd->fh_tqe_next != (fh_bucket_t *) -1);

				if (!(req->flags & FH_FLAG_PENDING)) {
					assert(req->internal == NULL);
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
			    		    ("Firehose Pending ADD bd=%d "
					     "(%p,%d), req=%p", bd, 
					     (void *) FH_BADDR(bd), FH_NODE(bd), 
					     req));
				}
				FH_RREFCINC(FH_REFCOUNT(bd));
				FH_TRACE_BUCKET(bd, PENDING);
			}
			else
				fh_bucket_acquire(node, bd);
		}
		else {
			fh_temp_buckets[unpinned] = bucket_addr;
			/* We add the bucket but set it PENDING */
			bd = fh_bucket_add(node, bucket_addr);
			bd->fh_state = fh_pending;
			FH_SET_REMOTE_PENDING(bd);
			FH_TRACE_BUCKET(bd, INIT);

			if (next_addr != bucket_addr)
				new_r++;

			next_addr = bucket_addr + FH_BUCKET_SIZE;
			unpinned++;

			if (!(req->flags & FH_FLAG_PENDING)) {
				assert(req->internal == NULL);
				ccba = fh_alloc_completion_callback();
				ccba->fh_tqe_next = FH_COMPLETION_END;
				memcpy(ccba, ccb, 
					    sizeof(fh_completion_callback_t));
				bd->fh_tqe_next = (fh_bucket_t *) ccba;
				req->flags |= FH_FLAG_PENDING;
				req->internal = (firehose_private_t *) ccba;
			}
		}
	}

	*new_regions = new_r;

	return unpinned;
}

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

firehose_request_t *
fh_acquire_remote_region(gasnet_node_t node, firehose_region_t *reg, 
		         firehose_completed_fn_t callback, void *context,
			 uint32_t flags, 
			 firehose_remotecallback_args_t *args,
			 firehose_request_t *ureq)
{
	int			 notpinned, new_r = 0;
	fh_bucket_t		 *bd;
	firehose_request_t	 *req;
	fh_completion_callback_t  ccb;

	/* Make sure the size of the region respects the remote limits */
	/* XXX should this check be done in non-assert */
	assert(FH_NUM_BUCKETS(reg->addr, reg->len) <= fhc_RemoteBucketsM);

	FH_TABLE_LOCK;

	req = fh_request_new(ureq);
	req->node = node;
	req->internal = NULL;
	FH_COPY_REGION_TO_REQUEST(req, reg);

	/* Fill in a completion callback struct temporarily as it may be used
	 * in fhi_TryAcquireRemoteRegion() */
	ccb.flags = FH_CALLBACK_TYPE_COMPLETION;
	ccb.fh_tqe_next = FH_COMPLETION_END;
	ccb.callback = callback;
	ccb.request  = req;
	ccb.context  = context;

	/* Writes the non-pinned buckets to temp_buckets array */
	notpinned = fhi_TryAcquireRemoteRegion(node, req, &ccb, reg, &new_r);

	GASNETI_TRACE_PRINTF(C, 
	    ("Firehose Request Remote on %d (%p,%d) (%d buckets unpinned, "
	     "flags=0x%x)",
	     node, req->addr, req->len, notpinned, req->flags));

	/* In moving remote regions, none of the temp arrays can be used, as
	 * the AM call has to be done without holding the TABLE lock.  For this
	 * reason, alloca() is used to acquire a temporary array of regions.
	 */

	if (notpinned > 0) {
		int			i, replace_b, old_r = 0, free_b;
		void			*reg_alloc;
		firehose_region_t	*reg_alloc_new, *reg_alloc_old;
		int			args_len = 0;

		assert(req->internal != NULL);
		assert(req->flags & FH_FLAG_PENDING);

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

		assert(new_r > 0);
		reg_alloc_old = reg_alloc_new + new_r;

		/* Find replacement buckets if required */
		if (replace_b > 0)
			old_r = fhi_WaitRemoteFirehosesToUnpin(node, replace_b,
					reg_alloc_old);
		else
			old_r = 0;

		fhc_RemoteBucketsUsed[node] += (notpinned - replace_b);

		assert(fhc_RemoteBucketsUsed[node] <= fhc_RemoteBucketsM);
		FH_TABLE_UNLOCK;

		if (args_len > 0)
			memcpy(reg_alloc_old + old_r, args, 
				sizeof(firehose_remotecallback_args_t));


		#ifdef FIREHOSE_UNBIND_CALLBACK
		if (old_r > 0)
			firehose_unbind_callback(node, reg_alloc_old, old_r);
		#endif

                MEDIUM_REQ(5, 6, 
                   (node, fh_handleridx(fh_am_move_reqh),
                    reg_alloc, 
		    sizeof(firehose_region_t) * (new_r+old_r) + args_len, 
		    flags, new_r, old_r, notpinned, PACK(req)));
	}
	else {
		/* Only set the PINNED flag if the request is not set on any
		 * pending buckets */
		if (!(req->flags & FH_FLAG_PENDING))
			req->flags |= FH_FLAG_PINNED;
		FH_TABLE_UNLOCK;
	}

	return req;
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
fh_release_remote_region(firehose_request_t *request, int inhandler)
{
	int		i;
	uintptr_t	end_addr, bucket_addr;
	fh_bucket_t	*bd;

	FH_TABLE_ASSERT_LOCKED;

	end_addr = request->addr + request->len - 1;

	GASNETI_TRACE_PRINTF(C, ("Firehose release_remote_region(%p, %d) %p",
	    request->addr, request->len, request));
	/* Process region in reverse order so regions can be later coalesced in
	 * the proper order (lower to higher address) from the FIFO */
	FH_FOREACH_BUCKET_REV(request->addr, end_addr, bucket_addr) {
		bd = fh_bucket_lookup(request->node, bucket_addr);
		assert(bd != NULL);
		assert(!FH_IS_REMOTE_PENDING(bd));

		fh_bucket_release(request->node, bd);
	}

	assert(fhc_RemoteVictimFifoBuckets[request->node] 
			<= fhc_RemoteBucketsM);

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
		      gasnet_handlerarg_t r_new,
		      gasnet_handlerarg_t r_old,
		      gasnet_handlerarg_t b_new,
		      void *request_type)
{
	firehose_region_t	*new_reg, *old_reg;
	fhi_RegionPool_t	*rpool;

	gasneti_stattime_t      movetime = GASNETI_STATTIME_NOW_IFENABLED(C);
	gasneti_stattime_t      unpintime;
	gasnet_node_t		node;

	assert(request_type != NULL);
	assert(b_new > 0);

	gasnet_AMGetMsgSource(token, &node);

	new_reg = (firehose_region_t *) addr;
	old_reg = (firehose_region_t *) addr + r_new;

	#ifdef FIREHOSE_UNEXPORT_CALLBACK
	if (old_num > 0)
		firehose_unexport_callback(node, old_reg, r_old);
	#endif

	FH_TABLE_LOCK;

	GASNETI_TRACE_PRINTF(C, ("Firehose move request: new=%d, old=%d",
			r_new, r_old));
	rpool = fhi_AllocRegionPool( FH_MIN_REGIONS_FOR_BUCKETS(b_new) );
	fhi_AcquireLocalRegionsList(node, new_reg, r_new, rpool);

	/* The next function may overcommit the fifo before the call to
	 * actually pin new regions is issued. */
	fhi_ReleaseLocalRegionsList(node, old_reg, r_old);
	GASNETI_TRACE_PRINTF(C, ("Firehose move request: pin new=%d",
			rpool->buckets_num));

	fhi_AdjustLocalFifoAndPin(node, rpool, 1);
	fhi_InitLocalRegionsList(node, rpool->regions, rpool->regions_num);

	fhi_FreeRegionPool(rpool);
	FH_TABLE_UNLOCK;

	#ifdef FIREHOSE_EXPORT_CALLBACK
	if (new_num > 0)
		firehose_export_callback(node, new_reg, r_new);
	#endif

	/* If the user requires to run a remote callback, and the
	 * callback is not to be run in place, run it */ 
	if (flags & FIREHOSE_FLAG_ENABLE_REMOTE_CALLBACK) {
		firehose_remotecallback_args_t	*args =
		    (firehose_remotecallback_args_t *)
		    ((firehose_region_t *) addr + r_new + r_old);

		/* Client may be able to support callbacks for DMA
		 * operations within the AM handler */

		#ifdef FIREHOSE_REMOTE_CALLBACK_IN_HANDLER
			firehose_remote_callback(node, 
			    (const firehose_region_t *) new_reg, r_new);

			MEDIUM_REP(2,3,(token,
			    fh_handleridx(fh_am_move_reph),
			    new_reg, sizeof(firehose_region_t) * r_new,
			    r_new, PACK(request_type)));
	
		#else
			/* TODO. . solve MALLOC ? */
			fh_remote_callback_t *rc = 
			    (fh_remote_callback_t *)
			    gasneti_malloc(sizeof(fh_remote_callback_t));
			if_pf (rc == NULL)
				gasneti_fatalerror("malloc");

			rc->flags = FH_CALLBACK_TYPE_REMOTE;
			rc->node = node;
			rc->pin_list_num = r_new;
			rc->reply_len = sizeof(firehose_region_t) * r_new;
			rc->request = request_type;

			rc->pin_list = (firehose_region_t *)
				gasneti_malloc(sizeof(firehose_region_t)*r_new);
			if_pf (rc->pin_list == NULL)
				gasneti_fatalerror("malloc");

			memcpy(rc->pin_list, new_reg, rc->reply_len);
			memcpy(&(rc->args), args,
			    sizeof(firehose_remotecallback_args_t));
	
			FH_POLLQ_LOCK;
			FH_STAILQ_INSERT_TAIL(&fh_CallbackFifo, 
				      (fh_callback_t *) rc);
			FH_POLLQ_UNLOCK;
		#endif
	}
	else {
		MEDIUM_REP(2,3,(token,
		    fh_handleridx(fh_am_move_reph),
		    new_reg, sizeof(firehose_region_t) * r_new,
		    r_new, PACK(request_type)));
	}

	return;
}
MEDIUM_HANDLER(fh_am_move_reqh,5,6,
              (token,addr,nbytes, a0, a1, a2, a3, UNPACK (a4    )),
              (token,addr,nbytes, a0, a1, a2, a3, UNPACK2(a4, a5)));

/*
 * Firehose AM Reply
 *
 */
GASNET_INLINE_MODIFIER(fh_am_move_reph_inner)
void
fh_am_move_reph_inner(gasnet_token_t token, void *addr,
		      size_t nbytes,
		      gasnet_handlerarg_t r_new,
		      void *request_type)
{
	firehose_region_t	*regions = (firehose_region_t *) addr;
	firehose_request_t	*req = (firehose_request_t *) request_type;
	fh_pollq_t		pendCallbacks;
	int			numpend;
	gasnet_node_t		node;

	fh_completion_callback_t	*ccb;

	assert(request_type != NULL);
	gasnet_AMGetMsgSource(token, &node);

	FH_TABLE_LOCK;

	/* 
	 * At least one pending request is attached a bucket, so process them
	 * and dynamically create a list in pendCallbacks
	 */

	numpend = 
	    fhi_FlushPendingRequests(node, regions, r_new, &pendCallbacks);

	if (numpend > 0) {
		#ifdef FIREHOSE_COMPLETION_IN_HANDLER
		fh_completion_callback_t	*ccb2;

		ccb = FH_STAILQ_FIRST(&pendCallbacks);
		while (ccb != NULL) {
			ccb2 = FH_STAILQ_NEXT(ccb);
			assert(!(ccb->request->flags & FH_FLAG_PENDING));
			ccb->callback(ccb->context, ccb->request, 0);
			ccb = ccb2;
		}
		#else
		fh_callback_t	*cb;
		
		FH_POLLQ_LOCK;
		FH_STAILQ_MERGE(&fh_CallbackFifo, &pendCallbacks);
		assert(!FH_STAILQ_EMPTY(&fh_CallbackFifo));
		FH_POLLQ_UNLOCK;
		#endif
	}
	FH_TABLE_UNLOCK;

	return;
}
MEDIUM_HANDLER(fh_am_move_reph,2,3,
              (token,addr,nbytes, a0, UNPACK(a1)     ),
              (token,addr,nbytes, a0, UNPACK2(a1, a2)));


void
fh_send_firehose_reply(fh_remote_callback_t *rc)
{
	MEDIUM_REQ(2,3,
	    (rc->node, fh_handleridx(fh_am_move_reph),
	    rc->pin_list, rc->reply_len, rc->pin_list_num, 
	    PACK(rc->request)));
}

void
fh_dump_counters()
{
	int 		i;
	gasnet_node_t	node = gasnet_mynode();

	/* Local counters */
	printf("%d> MaxVictimB=%d, Local[Only/Fifo/Inflight]=[%d/%d/%d]\n",
		node, fhc_MaxVictimBuckets, fhc_LocalOnlyBucketsPinned, 
		fhc_LocalVictimFifoBuckets, fhc_LocalOnlyBucketsInFlight);

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

#include <firehose.h>
#include <firehose_internal.h>

static firehose_request_t *fh_request_new(firehose_request_t *ureq);
static void fh_request_free(firehose_request_t *req);

/* ##################################################################### */
/* LOCKS, FIFOS, ETC.                                                    */
/* ##################################################################### */

/* The following lock, referred to as the "table" lock, must be held for every
 * firehose operation that modifies the state of the firehose table.  It must
 * be held during most of the firehose operations - adding/removing to the hash
 * table, adding/removing from the local and victim FIFOs.
 */
gasneti_mutex_t         fh_table_lock = GASNETI_MUTEX_INITIALIZER;

#ifndef FH_POLL_NOOP 
  /* This lock protects the poll FIFO queue, used to enqueue callbacks. */
  gasneti_mutex_t         fh_pollq_lock = GASNETI_MUTEX_INITIALIZER;
#endif

/* Firehose FIFOs */
fh_fifoq_t      fh_LocalFifo = FH_TAILQ_HEAD_INITIALIZER(fh_LocalFifo);
fh_fifoq_t      *fh_RemoteNodeFifo = NULL;

/* Local Limits & Counters */
int fhc_LocalOnlyBucketsPinned;
int fhc_LocalOnlyBucketsInFlight;
int fhc_LocalVictimFifoBuckets;
int fhc_MaxVictimBuckets;

/* Remote Limits & Counters */
int fhc_RemoteBucketsM;
int fhc_MaxRemoteBuckets;
int *fhc_RemoteBucketsUsed;
int *fhc_RemoteVictimFifoBuckets;

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

extern void
firehose_init(uintptr_t max_pinnable_memory, size_t max_regions, 
	      const firehose_region_t *prepinned_regions,
              size_t num_reg, firehose_info_t *info)
{
	int	i;

	/* Make sure the refc field in buckets can also be used as a FIFO
	 * pointer */
	assert(sizeof(fh_refc_t) == sizeof(void *));

	assert(FH_MAXVICTIM_TO_PHYSMEM_RATIO >= 0 && 
	       FH_MAXVICTIM_TO_PHYSMEM_RATIO <= 1);

	/* validate the prepinned regions list */
	for (i = 0; i < num_reg; i++) {
		const firehose_region_t *region = &prepinned_regions[i];
		if (region->addr % FH_BUCKET_SIZE != 0)
			gasneti_fatalerror("firehose_init: prepinned "
					"region is not aligned on a bucket "
					"boundary (addr = %p)",
					(void *) region->addr);
                                                                                
		if (region->len % FH_BUCKET_SIZE != 0)
			gasneti_fatalerror("firehose_init: prepinned "
					"region is not a multiple of firehose "
					"bucket size in length (len = %d)",
					region->len);
	}
                                                                                

	FH_TABLE_LOCK;

	fh_mynode = gasnet_mynode();

	/* Initialize the local firehose FIFO queue */
	FH_TAILQ_INIT(&fh_LocalFifo);

	/* hit the request_t freelist for first allocation */
	{
		firehose_request_t *req = fh_request_new(NULL);
		fh_request_free(req);
	}

        /* Allocate the per-node FIFOs and counters */
	fh_RemoteNodeFifo = (fh_fifoq_t *) 
		gasneti_malloc(gasnet_nodes() * sizeof(fh_fifoq_t));
        fhc_RemoteBucketsUsed = (int *)
                gasneti_malloc(gasnet_nodes() * sizeof(int));
        fhc_RemoteVictimFifoBuckets = (int *)
                gasneti_malloc(gasnet_nodes() * sizeof(int));
	for (i = 0; i < gasnet_nodes(); i++) {
		FH_TAILQ_INIT(&fh_RemoteNodeFifo[i]);
		fhc_RemoteBucketsUsed[i] = 0;
		fhc_RemoteVictimFifoBuckets[i] = 0;
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

void
firehose_fini()
{
	int	i;

	fh_fini_plugin();

	/* Free the per-node firehose FIFO queues and counters */
	gasneti_free(fh_RemoteNodeFifo);
        gasneti_free(fhc_RemoteBucketsUsed);
        gasneti_free(fhc_RemoteVictimFifoBuckets);

	/* Deallocate the arrays of request_t buffers used, if applicable */
	for (i = 0; i < 256; i++) {
		if (fh_request_bufs[i] == NULL)
			break;
		gasneti_free(fh_request_bufs[i]);
	}

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
				continue;
			}
			#endif

			#ifndef FIREHOSE_REMOTE_CALLBACK_IN_HANDLER
			if (fhc->flags & FH_CALLBACK_TYPE_REMOTE) {
				fh_remote_callback_t *rc =
					(fh_remote_callback_t *) fhc;
				firehose_remote_callback(rc->node, 
					rc->pin_list, rc->pin_list_num, 
					&(rc->args));

				/* Send an AMRequest to the reply handler */
				fh_send_firehose_reply(rc);
				gasneti_free(rc->pin_list);
				gasneti_free(fhc);
				continue;
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

	FH_TABLE_LOCK;

	req         = fh_request_new(ureq);
	req->node   = fh_mynode;
	req->addr   = FH_ADDR_ALIGN(addr);
	req->len    = FH_SIZE_ALIGN(addr,nbytes);
	req->flags |= FH_FLAG_PINNED;

	fh_acquire_local_region(req);

	FH_TABLE_UNLOCK;

	return req;
}

extern const firehose_request_t *
firehose_try_local_pin(uintptr_t addr, size_t len, firehose_request_t *ureq)
{
	firehose_request_t	*req = NULL;

	addr = FH_ADDR_ALIGN(addr);
	len  = FH_SIZE_ALIGN(addr,len);

	FH_TABLE_LOCK;
	if (fh_region_ispinned(fh_mynode, addr, len)) {
		req         = fh_request_new(ureq);
		req->node   = fh_mynode;
		req->addr   = addr;
		req->len    = len;
		req->flags |= FH_FLAG_PINNED;

		fh_commit_try_local_region(req);
	}
	FH_TABLE_UNLOCK;

	return req;
}

extern const firehose_request_t *
firehose_partial_local_pin(uintptr_t addr, size_t len,
                           firehose_request_t *ureq)
{
	firehose_request_t	*req = NULL;

	addr = FH_ADDR_ALIGN(addr);
	len  = FH_SIZE_ALIGN(addr,len);

	FH_TABLE_LOCK;
	if (fh_region_partial(fh_mynode, &addr, &len)) {
		req         = fh_request_new(ureq);
		req->node   = fh_mynode;
		req->addr   = addr;
		req->len    = len;
		req->flags |= FH_FLAG_PINNED;

		fh_commit_try_local_region(req);
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
	firehose_request_t	*req = NULL;

	if_pf (node == fh_mynode)
		gasneti_fatalerror("Cannot request a Remote pin on a local node.");

	assert(remote_args == NULL ? 1 : 
		(flags & FIREHOSE_FLAG_ENABLE_REMOTE_CALLBACK));

	FH_TABLE_LOCK;

	req = fh_request_new(ureq);
	req->node = node;
	req->addr = FH_ADDR_ALIGN(addr); 
	req->len  = FH_SIZE_ALIGN(addr,len);

	fh_acquire_remote_region(req, callback, context, flags, remote_args);

	/* Note that fh_acquire_remote_region unlocks before returning */
	FH_TABLE_ASSERT_UNLOCKED;

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

	if_pf (node == fh_mynode)
		gasneti_fatalerror("Cannot request a Remote pin on a local node.");

	addr = FH_ADDR_ALIGN(addr);
	len  = FH_SIZE_ALIGN(addr,len);

	FH_TABLE_LOCK;

	if (fh_region_ispinned(node, addr, len)) {
		req = fh_request_new(ureq);
		req->node = node;
		req->addr = addr;
		req->len  = len;

		fh_commit_try_remote_region(req);
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

	if_pf (node == fh_mynode)
		gasneti_fatalerror("Cannot request a Remote pin on a local node.");

	addr = FH_ADDR_ALIGN(addr);
	len  = FH_SIZE_ALIGN(addr,len);

	FH_TABLE_LOCK;

	if (fh_region_partial(node, &addr, &len)) {
		req = fh_request_new(ureq);
		req->node = node;
		req->addr = addr;
		req->len  = len;

		fh_commit_try_remote_region(req);
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

static firehose_request_t *
fh_request_new(firehose_request_t *ureq)
{
	firehose_request_t	*req;

	FH_TABLE_ASSERT_LOCKED;

	if_pt (ureq != NULL) {
		req = ureq;
		req->flags = 0;
		req->internal = NULL;
		return req;
	}

	if_pt (fh_request_freehead != NULL) {
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

static void
fh_request_free(firehose_request_t *req)
{
	FH_TABLE_ASSERT_LOCKED;

	if (req->flags & FH_FLAG_PENDING) {
		assert(req->internal != NULL);
		fh_free_completion_callback(
		    (fh_completion_callback_t *)req->internal);
	}
	/*
	else
		assert(req->internal == NULL);
		*/

	if (req->flags & FH_FLAG_FHREQ) {
		req->flags = 0;
		req->internal = (firehose_private_t *) fh_request_freehead;
		fh_request_freehead = req;
	}
	return;
}

/* region/page must provide implementations of these functions */

/* Data structures 
 *
 * Table(s) of firehose_private_t
 *   Adding: Entries are added once a private_t is pinned locally or a
 *           mapped to a remote location.
 *   Removing: Local private_t's are removed once memory is unpinned locally.
 *             Remote private_t's are removed when an AM move is required and
 *             that private_t had been selected for replacement.
 *   non-common implementation
 *
 */

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

/* 
 * Common data structures 
 *
 * Local Victim FIFO of firehose_private_t (oldest at head, newest at tail)
 *   Pushing: Adjacent entries are usually pushed in reverse-address order.
 *            This allows a subsequent popping operation to optimistically
 *            construct contiguous region_t's (for firehose-page).
 *   Popping: Entries are usually removed so as to create one contiguous
 *            region_t.
 *
 * Per-node firehose victim FIFO of firehose_private_t
 *   Pushing: Firehoses which reach a refcount of zero are added to the
 *            per-node firehose victim FIFO for possible replacement or reuse.
 *   Popping: An entry is removed when a node decides that it has used up all
 *            it's firehoses to a remote node and needs replacements.
 *
 * XXX/PHH: update to drop 'bucket' terminology here:
 *
 * Bucket state transitions
 *
 * Each bucket (whether local or remote) can be either pinned or unpinned.
 * Local buckets have a remote (R) and local (L) refcount whereas remote
 * buckets only have remote refcounts.
 *
 * Remote bucket handling is straightforward -- if R=0, the bucket is in the
 * remote fifo, and if R>0, it is in use.
 *
 * Local bucket handling is complicated by the L refcount and the necessity to
 * maintain the 'fhc_LocalOnlyBucketsPinned' counter (shown as LOnly below).
 *
 *********************************
 * LOCAL BUCKET STATE TRANSITIONS
 *********************************
 * Each state transition is triggered by acquire and release.
 *
 *           R L        
 *          .---.        
 *       A. |0 0| (UNPINNED)
 *          `---'          
 *          |  ^         
 *          |  | LOnly--
 *  LOnly++ |  |        
 *          V  |                              R L 
 *          .---. (PINNED)                   .---.
 *       B. |0 0| (IN FIFO) <-- -- -- -- --> |0 1| C. (PINNED)
 *          `---'                            `---'
 *          |  ^                             |  ^ 
 *          |  | LOnly++                     |  |  LOnly++
 *  LOnly-- |  |                     LOnly-- |  |
 *          V  |                             V  |
 *          .---.                            .---.
 *       E. |1 0| (PINNED)  <-- -- -- -- --> |1 1| D. (PINNED)
 *          `---'                            `---'
 *
 * All transitions  _TO_  state 'B' add    the bucket to the FIFO
 * All transitions _FROM_ state 'B' remove the bucket to the FIFO
 *
 *********************************
 * REMOTE BUCKET STATE TRANSITIONS
 *********************************
 * State transitions triggers are indicated in the diagram
 * 
 *            C.                               B.
 *          .---. (PINNED)   acquire(),R=1   .---.
 *          |R=0| (IN FIFO) <-- -- -- -- --> |R>0| (PINNED)
 *          `---'            release(),R=0   `---'
 *                                             ^ 
 *                                             |  Firehose reply
 *                                             | 
 *                                             |
 *                                           .---. (UNPINNED, PENDING PIN)
 *          Firehose request -- -- -- -- --> |R>0| -- --.
 *          first acquire()                  `---'      |
 *                                        A.  ^         |  acquire()
 *                                            |_ __ __ / 
 *
 * - Some transitions from 'B' are missing, the transition to 'C' only happens
 *   when the reference count reaches zero.
 * - Subsequent acquires on a bucket pending pin (state 'A') cause firehose
 *   requests to be queued up at the sender.  In other words, completions can
 *   be coalesced by a single firehose reply.
 *
 *--
 * Acquiring a bucket increments the refcount (either R or L)
 * Release a bucket decrements the refcount (ether R or L)
 *
 * Both functions return the new reference count for the incremented count.
 *
 */

fh_refc_t *
fh_priv_acquire(gasnet_node_t node, firehose_private_t *entry)
{
	fh_refc_t	*rp = FH_BUCKET_REFC(entry);

	FH_TABLE_ASSERT_LOCKED;
	
	/* 
	 * If the bucket is a local, if can contain both local and remote
	 * reference counts.
	 *
	 */
	assert(entry != NULL);

	if (FH_NODE(entry) == fh_mynode) {

		int	ref_L = (node == fh_mynode);
		/*
		 * 'ref_L' is TRUE if we are acquiring a local bucket for the
		 *         local node (ie: fh_local_pin).  
		 * 'ref_L' is FALSE if we are acquireing a local bucket from a
		 *         firehose request (fh_am_move).
		 *
		 */

		if (FH_IS_LOCAL_FIFO(entry)) {
			FH_TAILQ_REMOVE(&fh_LocalFifo, entry);
			assert(FH_NODE(entry) == fh_mynode);
			FH_BSTATE_ASSERT(entry, fh_local_fifo);

			rp->refc_l = ref_L;
			rp->refc_r = !ref_L;

			fhc_LocalOnlyBucketsPinned -= !ref_L;
			fhc_LocalVictimFifoBuckets--;
			FH_BSTATE_SET(entry, fh_used);
			FH_SET_USED(entry);

			FH_TRACE_BUCKET(entry, ACQFIFO);
		}
		else {
			FH_SET_USED(entry);
			FH_BSTATE_ASSERT(entry, fh_used);
			if (ref_L) {
				rp->refc_l++;
				FH_TRACE_BUCKET(entry, ACQUIRE);
			}
			else {
				if (rp->refc_r == 0) {
					assert(rp->refc_l > 0);
					fhc_LocalOnlyBucketsPinned--;
				}

				rp->refc_r++;
				FH_TRACE_BUCKET(entry, ACQUIRE);
			}
		}
	}

	/* If the bucket is a remote bucket, the node cannot be equal to
	 * fh_mynode */
	else {
		assert(node != fh_mynode);

		if (FH_IS_REMOTE_FIFO(entry)) {
			FH_TAILQ_REMOVE(&fh_RemoteNodeFifo[node], entry);

			assert(FH_NODE(entry) != fh_mynode);
			FH_BSTATE_ASSERT(entry, fh_remote_fifo);

			fhc_RemoteVictimFifoBuckets[node]--;
			rp->refc_l = 0;
			rp->refc_r = 1;
			
			FH_SET_USED(entry);
			FH_BSTATE_SET(entry, fh_used);
			FH_TRACE_BUCKET(entry, ACQFIFO);
		}
		else {
			/* Pending buckets must be handled separately */
			assert(!FH_IS_REMOTE_PENDING(entry));
			FH_BSTATE_ASSERT(entry, fh_used);

			rp->refc_r++;
			assert(rp->refc_r > 0);
			FH_TRACE_BUCKET(entry, ACQUIRE);
		}
	}
	return rp;
}

fh_refc_t *
fh_priv_release(gasnet_node_t node, firehose_private_t *entry)
{
	fh_refc_t	*rp = FH_BUCKET_REFC(entry);

	FH_TABLE_ASSERT_LOCKED;

	assert(entry != NULL);
	FH_BSTATE_ASSERT(entry, fh_used);

	if (FH_NODE(entry) == fh_mynode) {
		int		ref_L = (node == fh_mynode);
		/*
		 * 'ref_L' is TRUE if we are releasing a local bucket for the
		 *         local node
		 * 'ref_L' is FALSE if we are releasing a local bucket from a
		 *         firehose request
		 *
		 */

		assert(!FH_IS_LOCAL_FIFO(entry));

		if (ref_L) {
			assert(rp->refc_l > 0);
		}
		else {
			assert(rp->refc_r > 0);
		}

		rp->refc_l -= ref_L;
		rp->refc_r -= !ref_L;

		/* As a result, the bucket may be unused */
		if (rp->refc_r == 0 && rp->refc_l == 0) {
			FH_TAILQ_INSERT_TAIL(&fh_LocalFifo, entry);

			fhc_LocalOnlyBucketsPinned += !ref_L;
			fhc_LocalVictimFifoBuckets++;

			FH_BSTATE_SET(entry, fh_local_fifo);
			FH_TRACE_BUCKET(entry, ADDFIFO);
			return rp;
		}
		else {
			if (rp->refc_r == 0 && !ref_L) 
				fhc_LocalOnlyBucketsPinned++;

			FH_TRACE_BUCKET(entry, RELEASE);
			return rp;
		}
	}
	/* The bucket is a remote bucket, and it cannot contain any local
	 * refcounts.  Also, it should not be pending as pending buckets are
	 * handled separately */
	else {
                fh_refc_t refc;
		assert(node != fh_mynode);
		assert(!FH_IS_REMOTE_PENDING(entry));

		assert(rp->refc_r > 0);
		rp->refc_r--;

		if (rp->refc_r== 0) {
			FH_TAILQ_INSERT_TAIL(
			    &fh_RemoteNodeFifo[node], entry);

			fhc_RemoteVictimFifoBuckets[node]++;

			FH_BSTATE_SET(entry, fh_remote_fifo);
			FH_TRACE_BUCKET(entry, ADDFIFO);
			return rp;
		}
		else {
			FH_TRACE_BUCKET(entry, RELEASE);
			return rp;
		}
	}
}

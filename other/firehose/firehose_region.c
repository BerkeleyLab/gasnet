#include <firehose.h>
#include <firehose_internal.h>
#include <gasnet.h>
#include <gasnet_handler.h>

#ifdef FIREHOSE_REGION

typedef
struct _fh_bucket_t {
        fh_int_t         fh_key;	/* cached key for hash table */
        void            *fh_next;	/* linked list in hash table */
					/* _must_ be in this order */

        /* pointer to the containing region.  holds ref counts, etc */
        firehose_private_t      *priv;
        /* pointer to next bucket in same region */
        struct _fh_bucket_t     *next;
}
fh_bucket_t;

/* IFF firehose_fwd.h did not set these, complain now */
#ifndef FIREHOSE_CLIENT_MAXREGION_SIZE
  #error "Conduit didn't define FIREHOSE_CLIENT_MAXREGION_SIZE in firehose_fwd.h"
#endif
#ifndef FIREHOSE_CLIENT_MAXREGIONS
  #error "Conduit didn't define FIREHOSE_CLIENT_MAXREGIONS in firehose_fwd.h"
#endif

/* ##################################################################### */
/* GLOBAL TABLES, LOCKS, ETC.                                            */
/* ##################################################################### */

static firehose_private_t *fhi_lookup_cache;

/* ##################################################################### */
/* FORWARD DECLARATIONS, INTERNAL MACROS, ETC.                           */
/* ##################################################################### */

static gasnet_handlerentry_t fh_am_handlers[];
/* Initial value of index for gasnet registration */
#define _hidx_fh_am_move_reqh                   0
#define _hidx_fh_am_move_reph                   0
/* Index into the fh_am_handlers table to obtain the gasnet registered index */
#define _fh_hidx_fh_am_move_reqh                0
#define _fh_hidx_fh_am_move_reph                1
/* Magic to call a hander w/ runtime index assignment */
#define fh_handleridx(reqh)     (fh_am_handlers[ _fh_hidx_ ## reqh ].index)

/* Disqualify remote pending buckets */
#define FH_IS_READY(is_local, priv) \
	((is_local) || !FH_IS_REMOTE_PENDING(priv))

/* Assumes node field is correct */
#define CP_PRIV_TO_REQ(req, priv) 	do {			\
		(req)->addr = FH_BADDR(priv);			\
		(req)->len = (priv)->len;			\
		(req)->internal = (priv);			\
		(req)->client = (priv)->client;			\
	} while(0)

/* ##################################################################### */
/* VARIOUS HELPER FUNCTIONS                                              */
/* ##################################################################### */

/* compute ending address of "region" */
GASNET_INLINE_MODIFIER(fh_region_end)
uintptr_t fh_region_end(const firehose_region_t *region)
{
	assert(region != NULL);
	return (region->addr + (region->len - 1));
}

/* compute ending address of "req" */
GASNET_INLINE_MODIFIER(fh_req_end)
uintptr_t fh_req_end(const firehose_request_t *req)
{
	assert(req != NULL);
	return (req->addr + (req->len - 1));
}

/* compute ending address of "priv" */
GASNET_INLINE_MODIFIER(fh_priv_end)
uintptr_t fh_priv_end(const firehose_private_t *priv)
{
	assert(priv != NULL);
	return (FH_BADDR(priv) + (priv->len - 1));
}

/* compute ending address of "bucket" */
GASNET_INLINE_MODIFIER(fh_bucket_end)
uintptr_t fh_bucket_end(const fh_bucket_t *bucket)
{
	assert(bucket != NULL);
	return fh_priv_end(bucket->priv);
}

/* Compare two buckets with the same (node, address)	*
 * return true if first is best				*
 * "best" is the one with the greatest forward extent.	*
 *  XXX: we don't yet try to break ties intelligently.	*/
GASNET_INLINE_MODIFIER(fh_bucket_is_better)
int fh_bucket_is_better(fh_bucket_t *a, fh_bucket_t *b)
{
  assert(a != NULL);
  assert(b != NULL);
  assert(a->fh_key == b->fh_key);

  return (fh_bucket_end(a) > fh_bucket_end(b));
}

/* Compare two buckets with the same (node, address) to pick the "best" one */
GASNET_INLINE_MODIFIER(fh_best_bucket)
fh_bucket_t *fh_best_bucket(fh_bucket_t *a, fh_bucket_t *b)
{
  return (fh_bucket_is_better(a,b)) ? a : b;
}

/* ##################################################################### */
/* BUCKET TABLE HANDLING                                                 */
/* ##################################################################### */

fh_hash_t *fh_BucketTable1;
fh_hash_t *fh_BucketTable2;

/* "Best" matches for each bucket go in the first ("best") hash table.
 * Any others go (unsorted) in the second ("other") hash table.
 *
 * Lookup only needs to consult the "best" list.
 * Insertion may involve bumping an entry from "best" to "other".
 * Deletion may involve promoting an entry from "other" to "best".
 * Only the deletion requires comparisions in the "other" list and
 * then only if we are deleting what is otherwise our best match.
 */

static fh_bucket_t
*fh_bucket_lookup(gasnet_node_t node, uintptr_t addr)
{
        FH_TABLE_ASSERT_LOCKED;

        FH_ASSERT_BUCKET_ADDR(addr);

        /* Only ever need to lookup in the first table */
        return (fh_bucket_t *)
		fh_hash_find(fh_BucketTable1, FH_KEYMAKE(addr, node));
}

static void
fh_bucket_hash(fh_bucket_t *bucket, fh_int_t key)
{
	fh_bucket_t *other;
	fh_hash_t *hash;

        FH_TABLE_ASSERT_LOCKED;
	assert(bucket != NULL);

	bucket->fh_key = key;
	hash = fh_BucketTable1;

	/* check for existing entry, resolving conflict if any */
	other = (fh_bucket_t *)fh_hash_find(fh_BucketTable1, key);
	if_pf (other != NULL) {
		/* resolve conflict */
		if (fh_bucket_is_better(bucket, other)) {
			fh_hash_replace(fh_BucketTable1, other, bucket);
			bucket = other;
		}
		hash = fh_BucketTable2;
	}

	fh_hash_insert(hash, key, bucket);

	return;
}

static void
fh_bucket_unhash(fh_bucket_t *bucket)
{
    fh_int_t key;

    FH_TABLE_ASSERT_LOCKED;
    assert(bucket != NULL);

    key = bucket->fh_key;

    /* check for existence in "best" list */
    if_pf ((fh_bucket_t *)fh_hash_find(fh_BucketTable1, key) == bucket) {
        /* found in the "best" list, so must search for a replacement */
        fh_bucket_t *best = (fh_bucket_t *)fh_hash_find(fh_BucketTable2, key);

        if (best != NULL) {
	    fh_bucket_t *other = fh_hash_next(fh_BucketTable2, best);

            while (other) {
                best = fh_best_bucket(other, best);
                other = fh_hash_next(fh_BucketTable2, other);
	    }

	    fh_hash_replace(fh_BucketTable2, best, NULL);
	    fh_hash_replace(fh_BucketTable1, bucket, best);
	} else {
	    fh_hash_replace(fh_BucketTable1, bucket, NULL);
	}
    } else {
	fh_hash_replace(fh_BucketTable2, bucket, NULL);
    }

    return;
}

/* XXX/PHH use a freelist here */
/* Given a node and a region_t, create the necessary hash table entries.
 * The FIFO linkage and client_t are NOT initialized */
firehose_private_t *
fh_create_priv(gasnet_node_t node, uintptr_t addr, size_t len)
{
    uintptr_t end_addr, bucket_addr;
    firehose_private_t *priv;
    fh_bucket_t **prev;

    FH_TABLE_ASSERT_LOCKED;

    priv = gasneti_malloc(sizeof(firehose_private_t));
    memset(priv, 0, sizeof(firehose_private_t));

    priv->fh_key = FH_KEYMAKE(addr, node);
    priv->len = len;

    end_addr = addr + (len - 1);
    prev = &priv->bucket;
    FH_FOREACH_BUCKET(addr, end_addr, bucket_addr) {
        fh_bucket_t *bd = gasneti_malloc(sizeof(fh_bucket_t));
                                                                                
	bd->priv = priv;
	fh_bucket_hash(bd, FH_KEYMAKE(bucket_addr, node));

	*prev = bd;
	prev= &bd->next;
    }
    *prev = NULL;

    /* XXX/PHH hash the private_t somewhere ? */

    return priv;
}

/* ========= */
/* Commit a region known to be pinned, possibly in a FIFO */
void
fh_commit_region(firehose_request_t *req)
{
    firehose_private_t *priv;

    FH_TABLE_ASSERT_LOCKED;

    /* We *MUST* be commiting the most recent lookup */
    priv = fhi_lookup_cache;
    assert(priv != NULL);
    assert(req->addr >= FH_BADDR(priv));
    assert(fh_req_end(req) <= fh_priv_end(priv));

    fh_priv_acquire(fh_mynode, priv);
    CP_PRIV_TO_REQ(req, priv);

    return;
}

/* ##################################################################### */
/* PINNING QUERIES                                                       */
/* ##################################################################### */

/* If entire region is pinned then return non-zero.
   Note that we are counting on lookup giving the match with greatest
   forward extent.
 */
int
fh_region_ispinned(gasnet_node_t node, uintptr_t addr, size_t len)
{
    fh_bucket_t *bd;
    int retval = 0;

    FH_TABLE_ASSERT_LOCKED;

    bd = fh_bucket_lookup(node, addr);

    if_pt (bd &&
	   FH_IS_READY(node == fh_mynode, bd->priv) &&
	   ((addr + (len - 1)) <= fh_bucket_end(bd))) {
	fhi_lookup_cache = bd->priv;
	retval = 1;
    }

    return retval;
}

/* If any part of region is pinned then update region and return non-zero */
int
fh_region_partial(gasnet_node_t node, uintptr_t *addr_p, size_t *len_p)
{
    uintptr_t start_addr, end_addr, bucket_addr;
    int is_local = (node == fh_mynode);
    int retval = 0;

    FH_TABLE_ASSERT_LOCKED;

    start_addr = *addr_p;
    end_addr = start_addr + (*len_p - 1);

    FH_FOREACH_BUCKET(start_addr, end_addr, bucket_addr) {
        fh_bucket_t *bd = fh_bucket_lookup(node, bucket_addr);

	if (bd && FH_IS_READY(is_local, bd->priv)) {
	    *addr_p = FH_BADDR(bd->priv);
	    *len_p  = bd->priv->len;
	    fhi_lookup_cache = bd->priv;
	    retval = 1;
	    break;
	}
    }

    return retval;
}

/* ##################################################################### */
/* LOCAL PINNING                                                         */
/* ##################################################################### */
void
fh_acquire_local_region(firehose_request_t *req)
{
    assert(req != NULL);
    assert(req->node == fh_mynode);

    FH_TABLE_ASSERT_LOCKED;

    /* XXX unimplemented */
    return;
}

void
fh_commit_try_local_region(firehose_request_t *req)
{
    assert(req != NULL);
    assert(req->node == fh_mynode);

    FH_TABLE_ASSERT_LOCKED;

    /* Make sure the size of the region respects the local limits */
    assert(FH_NUM_BUCKETS(req->addr, req->len) <= fhc_MaxVictimBuckets);
                                                                                                             
    fh_commit_region(req);
}

void
fh_release_local_region(firehose_request_t *request)
{
        FH_TABLE_ASSERT_LOCKED;
	assert(request != NULL);
	assert(request->node == fh_mynode);
	assert(request->internal != NULL);
                                                                                                              
	fh_priv_release(fh_mynode, request->internal);
        //cleanup_overcommitted_local_FIFO();
                                                                                                              
        fhc_LocalOnlyBucketsInFlight -= 
		FH_NUM_BUCKETS(request->addr, request->len);

	return;
}

/* ##################################################################### */
/* REMOTE PINNING                                                        */
/* ##################################################################### */

void
fh_acquire_remote_region(firehose_request_t *req, 
		         firehose_completed_fn_t callback, void *context,
                         uint32_t flags,
                         firehose_remotecallback_args_t *remote_args)
{
    assert(req != NULL);
    assert(req->node != fh_mynode);

    FH_TABLE_ASSERT_LOCKED;

    /* XXX unimplemented */
    FH_TABLE_UNLOCK;
}

void
fh_commit_try_remote_region(firehose_request_t *req)
{
    assert(req != NULL);
    assert(req->node != fh_mynode);

    FH_TABLE_ASSERT_LOCKED;

    /* Make sure the size of the region respects the remote limits */
    assert(FH_NUM_BUCKETS(req->addr, req->len) <= fhc_MaxRemoteBuckets);

    fh_commit_region(req);
}

void
fh_release_remote_region(firehose_request_t *request)
{
        FH_TABLE_ASSERT_LOCKED;
	assert(request != NULL);
	assert(request->node != fh_mynode);
	assert(request->internal != NULL);
	assert(!FH_IS_REMOTE_PENDING(request->internal));
                                                                                                              
	fh_priv_release(request->node, request->internal);
                                                                                                              
        assert(fhc_RemoteVictimFifoBuckets[request->node]
                        <= fhc_RemoteBucketsM);
                                                                                                              
	return;
}

/* ##################################################################### */
/* INITIALIZATION & FINALIZATION                                         */
/* ##################################################################### */

/*
 * XXX:
 * We are constrained in two directions: limits on pages & regions
 * For now we are going to take the easy way out.  Since the code
 * inherited from firehose-page counts the number of private_t's
 * (which are pinned regions for us), we'll just use that single
 * limit.  We then set the limits in fhinfo such that the products
 * of terms will fit the memory limits:
 *	max_LocalRegions  * max_LocalPinSize  <= MAXVICTIM_M
 *	max_RemoteRegions * max_RemotePinSize <= M / (N-1)
 * As with firehose-page, the prepinned regions are counted against
 * the local regions.
 */
void
fh_init_plugin(uintptr_t max_pinnable_memory, size_t max_regions,
               const firehose_region_t *regions, size_t num_reg,
	       firehose_info_t *fhinfo)
{
	unsigned long param_M, param_VM;
	unsigned long param_R, param_VR;
	unsigned long param_RS;
	int i;
	unsigned long firehoses, m_prepinned;
	unsigned med_regions;
	int b_prepinned = 0;

        /* Initialize the Bucket tables */
        fh_BucketTable1 = fh_hash_create(1<<16); /* 64k */
        fh_BucketTable2 = fh_hash_create(1<<17); /* 128k */

	/* Count how many regions fit into an AM Medium payload */
	med_regions = (gasnet_AMMaxMedium() 
				- sizeof(firehose_remotecallback_args_t))
				/ sizeof(firehose_region_t);

	/*
	 * Prepin optimization: PHASE 1.
	 *
	 * Count the number of buckets that are set as prepinned.
	 *
	 */
	for (i = 0; i < num_reg; i++) {
		b_prepinned += FH_NUM_BUCKETS(regions[i].addr,regions[i].len);
	}
	m_prepinned = FH_BUCKET_SIZE * b_prepinned;

	/* Get limits from the environment */
	param_M  = fh_getenv("GASNET_FIREHOSE_M", (1<<20));
	param_VM = fh_getenv("GASNET_FIREHOSE_MAXVICTIM_M", (1<<20));
	param_R  = fh_getenv("GASNET_FIREHOSE_R", 1);
	param_VR = fh_getenv("GASNET_FIREHOSE_MAXVICTIM_R", 1);
	param_RS = fh_getenv("GASNET_FIREHOSE_MAXREGION_SIZE", (1<<20));
	GASNETI_TRACE_PRINTF(C, 
	    ("ENV: Firehose M=%ld, MAXVICTIM_M=%ld", param_M, param_VM));
	GASNETI_TRACE_PRINTF(C, 
	    ("ENV: Firehose R=%ld, MAXVICTIM_R=%ld", param_R, param_VR));
	GASNETI_TRACE_PRINTF(C, 
	    ("ENV: Firehose max region size=%ld", param_RS));

	/* Now assign decent "M" defaults based on physical memory */
	if (param_M == 0 && param_VM == 0) {
		param_M  = (unsigned long) max_pinnable_memory *
				(1-FH_MAXVICTIM_TO_PHYSMEM_RATIO);
		param_VM = (unsigned long) max_pinnable_memory *
				    FH_MAXVICTIM_TO_PHYSMEM_RATIO;
	}
	else if (param_M == 0)
		param_M = max_pinnable_memory - param_VM;
	else if (param_VM == 0)
		param_VM = max_pinnable_memory - param_M;
	GASNETI_TRACE_PRINTF(C,
			("param_M=%ld param_VM=%ld", param_M, param_VM));

	if (param_RS == 0) {
		/* We always send one AM to pin one region.  So, we need to
		 * have enough room AM to encode the requested region plus
		 * some number of regions to unpin.  In the worst case, the
		 * regions selected for replacement will be single-bucket
		 * sized (the minimum possible).
		 * So, we require param_RS <= (med_regions-1)*FH_BUCKET_SIZE
		 */
		param_RS = MIN(FIREHOSE_CLIENT_MAXREGION_SIZE,
			       (med_regions-1)*FH_BUCKET_SIZE);
	}
	/* Round down to multiple of FH_BUCKET_SIZE for sanity */
	param_RS &= ~FH_PAGE_MASK;
	GASNETI_TRACE_PRINTF(C, ("param_RS=%ld", param_RS));


	/* Try to work it all out with the given RS
 	 * The goal is (currently) to honor the given region size and
         * reduce the number of available regions as needed.
	 */
	if (param_R == 0 && param_VR == 0) {
		double ratio;

		/* try naively... */
		param_R  = (param_M - m_prepinned)  / param_RS;
		param_VR = param_VM / param_RS;
			
		/* then rescale if needed */
		ratio = (FIREHOSE_CLIENT_MAXREGIONS - num_reg) /
				(double)(param_R + param_VR);
		if (ratio < 1.) {
			param_R  *= ratio;
			param_VR *= ratio;
		}
	}
	else if (param_R == 0)
		param_R  = FIREHOSE_CLIENT_MAXREGIONS - num_reg - param_VR;
	else if (param_VR == 0)
		param_VR = FIREHOSE_CLIENT_MAXREGIONS - num_reg - param_R;
	GASNETI_TRACE_PRINTF(C,
			("param_R=%ld param_VR=%ld", param_R, param_VR));

	/* Trim and eliminate round-off so that limits are self-consistent */
	param_R  = MIN(param_R,  (param_M - m_prepinned)  / param_RS);
	param_VR = MIN(param_VR, param_VM / param_RS);
	param_M  = param_RS * param_R + m_prepinned;
	param_VM = param_RS * param_VR;

	/* 
	 * Validate firehose parameters parameters 
	 */ 
	{
		/* Want at least 1k buckets per node */
		unsigned long	M_min = FH_BUCKET_SIZE * gasnet_nodes() * 1024;

		/* Want at least 4k buckets of victim FIFO */
		unsigned long	VM_min = FH_BUCKET_SIZE * 4096;

		/* Want at least 1 region per node */
		/* XXX/PHH THIS IS REALLY A BARE MINIMUM */
		unsigned long	R_min = gasnet_nodes();

		/* Want at least 2 regions of FIFO */
		/* XXX/PHH THIS IS REALLY A BARE MINIMUM */
		unsigned long	VR_min = 2;

		if_pf (param_RS < FH_BUCKET_SIZE)
			gasneti_fatalerror("GASNET_FIREHOSE_MAXREGION_SIZE "
			    "is less than the minimum %d", FH_BUCKET_SIZE); 

		if_pf (param_RS > (med_regions-1)*FH_BUCKET_SIZE)
			gasneti_fatalerror("GASNET_FIREHOSE_MAXREGION_SIZE "
			    "is too large to encode in an AM Medium payload "
			    "(%d bytes max)", FH_BUCKET_SIZE*(med_regions-1));

		if_pf (param_M < M_min)
			gasneti_fatalerror("GASNET_FIREHOSE_M is less "
			    "than the minimum %d (%d buckets)", M_min, 
			    M_min >> FH_BUCKET_SHIFT);

		if_pf (param_VM < VM_min)
			gasneti_fatalerror("GASNET_MAXVICTIM_M is less than "
			    "the minimum %d (%d buckets)", VM_min,
			    VM_min >> FH_BUCKET_SHIFT);

		if_pf (param_M - m_prepinned < M_min)
			gasneti_fatalerror("Too many bytes in initial"
			    " pinned regions list (%d) for current "
			    "GASNET_FIREHOSE_M parameter (%d)", 
			    b_prepinned, param_M);

		if_pf (param_R < R_min)
			gasneti_fatalerror("GASNET_FIREHOSE_R is less"
			    "than the minimum %d", R_min);

		if_pf (param_VR < VR_min)
			gasneti_fatalerror("GASNET_MAXVICTIM_R is less than "
			    "the minimum %d", VR_min);

		if_pf (param_R - num_reg < R_min)
			gasneti_fatalerror("Too many regions passed on initial"
			    " pinned bucket list (%d) for current "
			    "GASNET_FIREHOSE_R parameter (%d)", 
			    num_reg, param_R);
	}

	/* 
	 * Set local parameters
	 */
	fhc_LocalOnlyBucketsPinned = num_reg;
	fhc_LocalVictimFifoBuckets = 0;
	fhc_LocalOnlyBucketsInFlight = 0;
	fhc_MaxVictimBuckets = num_reg + param_VR;

	/* 
	 * Set remote parameters
	 */
	firehoses = MIN(param_R, (param_M - m_prepinned) / param_RS);
	fhc_RemoteBucketsM = gasnet_nodes() > 1
				? firehoses / (gasnet_nodes()-1)
				: firehoses;

	GASNETI_TRACE_PRINTF(C, 
		    ("Maximum pinnable=%d\tMax allowed=%d", 
		     (firehoses + param_VR) * param_RS + m_prepinned,
		     max_pinnable_memory));
	assert((firehoses + param_VR) * param_RS + m_prepinned <= max_pinnable_memory);

#if 0
	/* Initialize bucket freelist with the total amount of buckets
	 * to be pinned (including the ones the client passed) */
	/* XXX/PHH: to check w/ christian: looks like prepinned memory is double counted */
	fh_bucket_init_freelist(firehoses + fhc_MaxVictimBuckets);
#endif

	/*
	 * Prepin optimization: PHASE 2.
	 *
	 * In this phase, the firehose parameters have been validated and the
	 * buckets are added to the firehose table and set as 'used'.
	 *
	 */
	for (i = 0; i < num_reg; i++) {
		firehose_private_t	*priv;

		priv = fh_create_priv(fh_mynode,
				      regions[i].addr, regions[i].len);
		priv->client = regions[i].client;

		FH_BSTATE_SET(priv, fh_used);
		FH_SET_USED(priv);
		FH_TRACE_BUCKET(priv, ADDING PREPINNED);
		FH_BUCKET_REFC(priv)->refc_l = 0; /* XXX/PHH:  =1 ? */
		FH_BUCKET_REFC(priv)->refc_r = 0;
	}


	/* 
	 * Set fields in the firehose information type, according to the limits
	 * established by the firehose parameters.
	 */
	{
		fhc_MaxRemoteBuckets = param_RS >> FH_BUCKET_SHIFT;

		fhinfo->max_RemoteRegions = fhc_RemoteBucketsM;
		fhinfo->max_LocalRegions  = param_VR;

		fhinfo->max_LocalPinSize  = param_RS;
		fhinfo->max_RemotePinSize = param_RS;

		GASNETI_TRACE_PRINTF(C, 
		    ("Firehose M=%ld (fh=%ld)\tprepinned=%ld (buckets=%d)",
		    param_M, firehoses, m_prepinned, b_prepinned));
		GASNETI_TRACE_PRINTF(C, ("Firehose Maxvictim=%ld (fh=%ld)",
		    param_VM, fhc_MaxVictimBuckets));

		GASNETI_TRACE_PRINTF(C, 
		    ("MaxLocalPinSize=%d\tMaxRemotePinSize=%d", 
		    fhinfo->max_LocalPinSize, fhinfo->max_RemotePinSize));
		GASNETI_TRACE_PRINTF(C, 
		    ("MaxLocalRegions=%d\tMaxRemoteRegions=%d", 
		    fhinfo->max_LocalRegions, fhinfo->max_RemoteRegions));
	}

	return;
}

void
fh_fini_plugin(void)
{
        fh_hash_destroy(fh_BucketTable2);
        fh_hash_destroy(fh_BucketTable1);
}

/* ##################################################################### */
/* ACTIVE MESSAGES                                                       */
/* ##################################################################### */

GASNET_INLINE_MODIFIER(fh_am_move_reqh_inner)
void
fh_am_move_reqh_inner(gasnet_token_t token, void *addr, size_t nbytes,
		      gasnet_handlerarg_t flags,
		      gasnet_handlerarg_t r_new,
		      gasnet_handlerarg_t r_old,
		      gasnet_handlerarg_t b_new,
		      void *request_type)
{
	/* XXX unimplemented */
	return;
}
MEDIUM_HANDLER(fh_am_move_reqh,5,6,
              (token,addr,nbytes, a0, a1, a2, a3, UNPACK (a4    )),
              (token,addr,nbytes, a0, a1, a2, a3, UNPACK2(a4, a5)));

GASNET_INLINE_MODIFIER(fh_am_move_reph_inner)
void
fh_am_move_reph_inner(gasnet_token_t token, void *addr, size_t nbytes,
		      gasnet_handlerarg_t r_new)
{
	/* XXX unimplemented */
	return;
}
MEDIUM_HANDLER(fh_am_move_reph,1,1,
              (token,addr,nbytes, a0),
              (token,addr,nbytes, a0));

static
gasnet_handlerentry_t fh_am_handlers[] = {
        /* ptr-width dependent handlers */
        gasneti_handler_tableentry_with_bits(fh_am_move_reqh),
        gasneti_handler_tableentry_with_bits(fh_am_move_reph),
        { 0, NULL }
};
                                                                                                              
gasnet_handlerentry_t *
firehose_get_handlertable() {
        return fh_am_handlers;
}

#endif

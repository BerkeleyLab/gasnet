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

/* ##################################################################### */
/* GLOBAL TABLES, LOCKS, ETC.                                            */
/* ##################################################################### */

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

/* Assumes node is already correct */
#define CP_PRIV_TO_REGION(reg, priv) 	do {			\
		(reg)->addr = FH_BADDR(priv);			\
		(reg)->len = (priv)->len;			\
		(reg)->client = (priv)->client;			\
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

#if 0
/* XXX/PHH: NOT YET FULLY IMPLEMENTED
 * Need to use a freelist, keep related buckets linked, etc.
 */
static void
fh_bucket_add(fh_bucket_t *bucket)
{
	fh_int_t key;
	fh_bucket_t *other;
	fh_hash_t *hash;

        FH_TABLE_ASSERT_LOCKED;
	assert(bucket != NULL);

	key = bucket->fh_key;
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
fh_bucket_remove(fh_bucket_t *bucket)
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
    
    /* XXX/PHH: return entry to freelist */

    return;
}
#endif


/* ========= */
/* Commit a region known to be pinned, possibly in a FIFO */
void
fh_commit_region(gasnet_node_t node, firehose_region_t *region)
{
    fh_bucket_t     *bd;

    FH_TABLE_ASSERT_LOCKED;

    /* XXX: should have a way to avoid repeating the lookup here */
    bd = fh_bucket_lookup(node, region->addr);
    assert(bd != NULL);
    assert(fh_region_end(region) <= fh_bucket_end(bd));

    fh_priv_acquire(fh_mynode, bd->priv);
    CP_PRIV_TO_REGION(region, bd->priv);

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
fh_region_ispinned(gasnet_node_t node, firehose_region_t *region)
{
    fh_bucket_t *bd;
    int retval = 0;

    FH_TABLE_ASSERT_LOCKED;

    bd = fh_bucket_lookup(node, region->addr);

    if_pf (bd &&
	   FH_IS_READY(node == fh_mynode, bd->priv) &&
	   (fh_region_end(region) <= fh_bucket_end(bd))) {
	retval = 1;
    }

    return retval;
}

/* If any part of region is pinned then update region and return non-zero */
int
fh_region_partial(gasnet_node_t node, firehose_region_t *region)
{
    uintptr_t end_addr, bucket_addr;
    int is_local = (node == fh_mynode);
    int retval = 0;

    FH_TABLE_ASSERT_LOCKED;

    end_addr = fh_region_end(region);
    FH_FOREACH_BUCKET(region->addr, end_addr, bucket_addr) {
        fh_bucket_t *bd = fh_bucket_lookup(node, bucket_addr);

	if_pf (bd && FH_IS_READY(is_local, bd->priv)) {
	    CP_PRIV_TO_REGION(region, bd->priv);
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
fh_acquire_local_region(firehose_region_t *region)
{
	/* XXX unimplemented */
	return;
}

void
fh_commit_try_local_region(firehose_region_t *region)
{
    /* Make sure the size of the region respects the local limits */
    assert(FH_NUM_BUCKETS(region->addr, region->len) <= fhc_MaxVictimBuckets);
                                                                                                             
    fh_commit_region(fh_mynode, region);
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

firehose_request_t *
fh_acquire_remote_region(gasnet_node_t node, firehose_region_t *reg, 
		         firehose_completed_fn_t callback, void *context,
                         uint32_t flags,
                         firehose_remotecallback_args_t *remote_args,
                         firehose_request_t *ureq)
{
	/* XXX unimplemented */
	return NULL;
}

void
fh_commit_try_remote_region(gasnet_node_t node, firehose_region_t *region)
{
    /* Make sure the size of the region respects the remote limits */
    assert(FH_NUM_BUCKETS(region->addr, region->len) <= fhc_MaxRemoteBuckets);

    fh_commit_region(node, region);
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

void
fh_init_plugin(uintptr_t max_pinnable_memory, size_t max_regions,
               const firehose_region_t *prepinned_regions,
               size_t num_prepinned, firehose_info_t *fhinfo)
{
        /* Initialize the Bucket tables */
        fh_BucketTable1 = fh_hash_create(1<<16); /* 64k */
        fh_BucketTable2 = fh_hash_create(1<<17); /* 128k */

	/* ### Add prepinned regions to the tables */
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

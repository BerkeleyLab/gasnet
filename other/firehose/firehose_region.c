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
static firehose_private_t *fhi_priv_freelist;

static size_t fhi_MaxRegionSize;

/* ##################################################################### */
/* FORWARD DECLARATIONS, INTERNAL MACROS, ETC.                           */
/* ##################################################################### */

/* Disqualify remote pending buckets */
#define FH_IS_READY(is_local, priv) \
	((is_local) || !FH_IS_REMOTE_PENDING(priv))

#ifdef FIREHOSE_CLIENT_T
  #define FH_CP_CLIENT(A,B) (A)->client = (B)->client
#else
  #define FH_CP_CLIENT(A,B)
#endif

/* Assumes node field is correct */
#define CP_PRIV_TO_REQ(req, priv) 	do {			\
		(req)->addr = FH_BADDR(priv);			\
		(req)->len = (priv)->len;			\
		(req)->internal = (priv);			\
		FH_CP_CLIENT((req), (priv));			\
	} while(0)

#define CP_REG_TO_PRIV(priv, node, reg) 	do {		    \
		(priv)->fh_key = FH_KEYMAKE((reg)->addr, (node));   \
		(priv)->len = (reg)->len;			    \
		FH_CP_CLIENT((priv), (reg));			    \
	} while(0)
#define CP_PRIV_TO_REG(reg, priv) 	do {			\
		(reg)->addr = FH_BADDR(priv);			\
		(reg)->len = (priv)->len;			\
		FH_CP_CLIENT((reg), (priv));			\
	} while(0)

/* ##################################################################### */
/* VARIOUS HELPER FUNCTIONS                                              */
/* ##################################################################### */

/* compute ending address of "region" */
GASNET_INLINE_MODIFIER(fh_region_end)
uintptr_t fh_region_end(const firehose_region_t *region)
{
	gasneti_assert(region != NULL);
	return (region->addr + (region->len - 1));
}

/* compute ending address of "req" */
GASNET_INLINE_MODIFIER(fh_req_end)
uintptr_t fh_req_end(const firehose_request_t *req)
{
	gasneti_assert(req != NULL);
	return (req->addr + (req->len - 1));
}

/* compute ending address of "priv" */
GASNET_INLINE_MODIFIER(fh_priv_end)
uintptr_t fh_priv_end(const firehose_private_t *priv)
{
	gasneti_assert(priv != NULL);
	return (FH_BADDR(priv) + (priv->len - 1));
}

/* compute ending address of "bucket" */
GASNET_INLINE_MODIFIER(fh_bucket_end)
uintptr_t fh_bucket_end(const fh_bucket_t *bucket)
{
	gasneti_assert(bucket != NULL);
	return fh_priv_end(bucket->priv);
}

/* Compare two buckets with the same (node, address)
 * return non-zero if first is best
 * "best" is the one with the greatest forward extent.
 * In case of a tie on forward extent, the longer region wins.
 * In case of a complete tie, we return 0.
 */
GASNET_INLINE_MODIFIER(fh_bucket_is_better)
int fh_bucket_is_better(const fh_bucket_t *a, const fh_bucket_t *b)
{
  uintptr_t end_a, end_b;

  gasneti_assert(a != NULL);
  gasneti_assert(a->priv != NULL);
  gasneti_assert(b != NULL);
  gasneti_assert(b->priv != NULL);
  gasneti_assert(a->fh_key == b->fh_key);

  end_a = fh_bucket_end(a);
  end_b = fh_bucket_end(b);

  if_pt (end_a != end_b) {
    return (end_a > end_b);
  }
  else {
    return (a->priv->len > b->priv->len);
  }
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

/* "Best" matches for each bucket go in the first ("best") hash table (1).
 * Any others go (unsorted) in the second ("other") hash table (2).
 *
 * Lookup only needs to consult the "best" list.
 * Insertion may involve bumping an entry from "best" to "other".
 * Deletion may involve promoting an entry from "other" to "best".
 * Only the deletion requires comparisions in the "other" list and
 * then only if we are deleting what is otherwise our best match.
 */
fh_hash_t *fh_BucketTable1;
fh_hash_t *fh_BucketTable2;

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
	gasneti_assert(bucket != NULL);

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
    gasneti_assert(bucket != NULL);

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
	}
	else {
	    fh_hash_replace(fh_BucketTable1, bucket, NULL);
	}
    }
    else {
	fh_hash_replace(fh_BucketTable2, bucket, NULL);
    }

    return;
}

static void
fh_bucket_rehash(fh_bucket_t *bucket)
{
    fh_bucket_t *other;
    fh_int_t key;

    FH_TABLE_ASSERT_LOCKED;
    gasneti_assert(bucket != NULL);

    key = bucket->fh_key;

    other = (fh_bucket_t *)fh_hash_find(fh_BucketTable1, key);
    if_pf ((other != bucket) && fh_bucket_is_better(bucket, other)) {
	/* We've moved up in the world */
	fh_hash_replace(fh_BucketTable2, bucket, NULL);
	fh_hash_replace(fh_BucketTable1, other, bucket);
	fh_hash_insert(fh_BucketTable2, key, other);
    }

    return;
}

/* XXX: merge freelist w/ page? */
static fh_bucket_t *fhi_bucket_freelist = NULL;

GASNET_INLINE_MODIFIER(fh_bucket_new)
fh_bucket_t *fh_bucket_new(void)
{
    fh_bucket_t *bucket = fhi_bucket_freelist;

    if_pt (bucket != NULL) {
	fhi_bucket_freelist = bucket->fh_next;
    }
    else {
        bucket = gasneti_malloc(sizeof(fh_bucket_t));
    }
    memset(bucket, 0, sizeof(fh_bucket_t));

    return bucket;
}

GASNET_INLINE_MODIFIER(fh_bucket_free)
void fh_bucket_free(fh_bucket_t *bucket)
{
    bucket->fh_next = fhi_bucket_freelist;
    fhi_bucket_freelist = bucket;
}

/* Also keep a hash table of the local private_t's we create so that we can
 * match them when received in an AM (pin reply or unpin request).
 * XXX: this use is trashing the NODE portion of priv->fh_key.  We've been
 * careful to ensure that the only thing this breaks is the debugging output.
 * However, we should really see about a better way to do the lookup from
 * the unpin request to the private_t.  I see two options:
 * 1) Carry an extra field over the wire (probably the actual private_t *)
 *    that must be passed back to unpin (stored and passed in exactly the
 *    same places that the client_t is)
 * 2) Keep a "PrivTable" which hashes locally pinned regions, but does so
 *    external to the private_t.
 * If only local regions need to be hashed in this way then #2 wins on both
 * network traffic and storage space.
 * If remote regions are also hashed in this way (perhaps for detection of
 * duplicates?), then #2 wins on network traffic but loses slightly on
 * storage.
 */
fh_hash_t *fh_PrivTable;

/* XXX: Would be nice to allow client to supply a hash on client_t.
 * However, that currently creates lifetime problems when creating
 * and destroying private_t's.
 */
#ifndef FIREHOSE_HASH_PRIV
  #define FIREHOSE_HASH_PRIV(addr, len) \
	FH_KEYMAKE((addr), ((len) >> FH_BUCKET_SHIFT))
#endif

GASNET_INLINE_MODIFIER(fh_region_to_priv)
firehose_private_t *
fh_region_to_priv(const firehose_region_t *reg)
{
	firehose_private_t *priv;
        fh_int_t key;

        FH_TABLE_ASSERT_LOCKED;

	key = FIREHOSE_HASH_PRIV(reg->addr, reg->len);
        priv = (firehose_private_t *)fh_hash_find(fh_PrivTable, key);
	gasneti_assert(priv != NULL);

	return priv;
}

/* Given a node and a region_t, create the necessary hash table entries.
 * The FIFO linkage is NOT initialized */
firehose_private_t *
fh_create_priv(gasnet_node_t node, const firehose_region_t *reg)
{
    uintptr_t end_addr, bucket_addr;
    firehose_private_t *priv;
    fh_bucket_t **prev;

    FH_TABLE_ASSERT_LOCKED;

    priv = fhi_priv_freelist;
    if_pt (priv != NULL) {
	fhi_priv_freelist = priv->fh_next;
    }
    else {
        priv = gasneti_malloc(sizeof(firehose_private_t));
    }
    memset(priv, 0, sizeof(firehose_private_t));

    CP_REG_TO_PRIV(priv, node, reg);

    end_addr = fh_region_end(reg);
    prev = &priv->bucket;
    FH_FOREACH_BUCKET(reg->addr, end_addr, bucket_addr) {
        fh_bucket_t *bd = fh_bucket_new();

	bd->priv = priv;
	fh_bucket_hash(bd, FH_KEYMAKE(bucket_addr, node));

	*prev = bd;
	prev= &bd->next;
    }
    *prev = NULL;

    /* Hash the priv IFF local*/
    if_pt (node == fh_mynode) {
	/* XXX: preserves the ADDR part but invalidates NODE */
	priv->fh_key = FIREHOSE_HASH_PRIV(reg->addr, reg->len);
	gasneti_assert(fh_hash_find(fh_PrivTable, priv->fh_key) == NULL);
	fh_hash_insert(fh_PrivTable, priv->fh_key, priv);
	gasneti_assert(fh_hash_find(fh_PrivTable, priv->fh_key) == priv);
    }

    return priv;
}

void
fh_destroy_priv(firehose_private_t *priv)
{
    fh_bucket_t *bucket;
    gasnet_node_t node;

    /* Unhash & free all the buckets */
    bucket = priv->bucket;
    gasneti_assert(bucket != NULL);
    node = FH_NODE(bucket);
    do {
	fh_bucket_t *next = bucket->next;
        fh_bucket_unhash(bucket);
	fh_bucket_free(bucket);
	bucket = next;
    } while (bucket != NULL);

    /* Unhash the priv IFF local*/
    if_pt (node == fh_mynode) {
	gasneti_assert(fh_hash_find(fh_PrivTable, priv->fh_key) == priv);
	fh_hash_insert(fh_PrivTable, priv->fh_key, NULL);
	gasneti_assert(fh_hash_find(fh_PrivTable, priv->fh_key) == NULL);
    }

    priv->fh_next = fhi_priv_freelist;
    fhi_priv_freelist = priv;
}

/* Given an existing private_t and a region_t, change the necessary hash
 * table entries.
 */
void
fh_update_priv(firehose_private_t *priv, const firehose_region_t *reg)
{
    uintptr_t bucket_addr;
    uintptr_t old_start, new_start;
    uintptr_t old_end, new_end;
    gasnet_node_t node = FH_NODE(priv);	/* XXX safe because priv is remote */
    fh_bucket_t *bucket;
    fh_bucket_t **prev;

    FH_TABLE_ASSERT_LOCKED;

    old_start = FH_BADDR(priv);
    old_end = fh_priv_end(priv);
    new_start = reg->addr;
    new_end = fh_region_end(reg);

    if_pt ((old_start == new_start) && (old_end == new_end)) {
	FH_CP_CLIENT(priv, reg);
	return;		/* nothing else needs to change */
    }

#if 0
    bucket = fh_bucket_lookup(node, new_start);
    if (bucket && (fh_bucket_end(bucket) == new_end)) {
	fprintf(stderr, "%d> exact match %p %p (%d, %p, %dk)\n",
		fh_mynode, bucket->priv, priv, node, (void *)reg->addr, (int)(reg->len)/1024);
    }
#endif

    /* updates fh_key, len and client */
    CP_REG_TO_PRIV(priv, node, reg);

    /* Create new hash entries for "prefix" */
    bucket = priv->bucket;
    if (new_start < old_start) {
        prev = &priv->bucket;
        FH_FOREACH_BUCKET(new_start, old_start-1, bucket_addr) {
            fh_bucket_t *bd = fh_bucket_new();

	    bd->priv = priv;
	    fh_bucket_hash(bd, FH_KEYMAKE(bucket_addr, node));

	    *prev = bd;
	    prev = &bd->next;
        }
	*prev = bucket;
    }

    /* Rehash existing buckets as needed */
    do {
	fh_bucket_rehash(bucket);
	prev = &bucket->next;
	bucket = bucket->next;
    } while (bucket != NULL);

    /* Create new hash entries for "suffix" */
    if (new_end > old_end) {
	FH_FOREACH_BUCKET(old_end+1, new_end, bucket_addr) {
            fh_bucket_t *bd = fh_bucket_new();

	    bd->priv = priv;
	    fh_bucket_hash(bd, FH_KEYMAKE(bucket_addr, node));

	    *prev = bd;
	    prev= &bd->next;
        }
    }

    return;
}

void
fhi_remove_from_fifo(firehose_region_t *reg, firehose_private_t *priv,
			fh_fifoq_t *fifo_head)
{
    FH_TAILQ_REMOVE(fifo_head, priv);
    CP_PRIV_TO_REG(reg, priv);
    FH_TRACE_BUCKET(priv, REMFIFO);
    fh_destroy_priv(priv);
}

/* Look for opportunities to merge adjacent pinned regions.
 */
GASNET_INLINE_MODIFIER(fhi_merge_regions)
void
fhi_merge_regions(firehose_region_t *pin_region, uintptr_t addr, size_t len)
{
    fh_bucket_t *bd;
    size_t	extend;
    size_t	space_avail = fhi_MaxRegionSize - len;

    gasneti_assert(len <= fhi_MaxRegionSize);

    /* Because we prioritize lookups by "forward extent", our best
     * chance of fully replacing a region comes from merging with one
     * which preceeds the new one, even if we can't fully cover it. */
    if_pt (addr != 0) { /* avoid wrap around */
	bd = fh_bucket_lookup(fh_mynode, addr - FH_BUCKET_SIZE);
	if (bd != NULL) {

	    gasneti_assert(bd->priv != NULL);
	    gasneti_assert(fh_priv_end(bd->priv) >= (addr - 1));
	    gasneti_assert(fh_priv_end(bd->priv) < (addr + (len - 1)));

	    extend = MIN(addr - FH_BADDR(bd), space_avail);
	    addr -= extend;
	    len += extend;
	    space_avail -= extend;
	}
	gasneti_assert(len <= fhi_MaxRegionSize);
    }

    /* Now try to extend forward as well.
     * Not as worth while if we can't fully cover the other region.
     */
    if_pt (addr + len != 0) { /* avoid wrap around */
	uintptr_t next_addr = addr + len;
	bd = fh_bucket_lookup(fh_mynode, next_addr);
	if (bd != NULL) {
	    uintptr_t end_addr = fh_priv_end(bd->priv) + 1;

	    gasneti_assert(end_addr > next_addr);
	    extend = end_addr - next_addr;

	    /* only accept complete coverage */
	    if (extend <= space_avail) {
		len += extend;
		space_avail -= extend;
	    }
	}
	gasneti_assert(len <= fhi_MaxRegionSize);
    }

    pin_region->addr = addr;
    pin_region->len  = len;
}

/* Lookup a region, returning the coresponding priv if found, else NULL.
 */
GASNET_INLINE_MODIFIER(fhi_find_priv)
firehose_private_t *
fhi_find_priv(gasnet_node_t node, uintptr_t addr, size_t len)
{
    firehose_private_t *priv = NULL;
    fh_bucket_t *bd;
		
    FH_TABLE_ASSERT_LOCKED;

    bd = fh_bucket_lookup(node, addr);

    if_pt (bd && ((addr + (len - 1)) <= fh_bucket_end(bd))) {
	/* Firehose HIT */
	priv = bd->priv;
    }

    return priv;
}

/*
 * fh_FreeVictim(count, region_array, head)
 *
 * This function removes 'count' firehoses from the victim FIFO (local or
 * remote), and fills the region_array with regions suitable for move_callback.
 * It returns the amount of regions (not buckets) created in the region_array.
 *
 * NOTE: it is up to the caller to make sure the region array is large enough.
 */
int
fh_FreeVictim(int count, firehose_region_t *reg, fh_fifoq_t *fifo_head)
{
	int			i;
	firehose_private_t	*priv;

	FH_TABLE_ASSERT_LOCKED;

	/* XXX/PHH FOR NOW... */
	gasneti_assert(count == 1);

	/* There must be enough buckets in the victim FIFO to unpin.  This
	 * criteria should always hold true per the constraints on
	 * fhc_LocalOnlyBucketsPinned. */
	for (i = 0; i < count; i++) {
		priv = FH_TAILQ_FIRST(fifo_head);

		fhi_remove_from_fifo(&reg[i], priv, fifo_head);
	}
	gasneti_assert(count == i);
	return i;
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
    firehose_private_t *priv;
    firehose_region_t pin_region, unpin_region;
    int num_unpin = 0;
    int retval = 0;

    gasneti_assert(req != NULL);
    gasneti_assert(req->node == fh_mynode);
    gasneti_assert(req->len <= fhi_MaxRegionSize);

    /* Make sure the size of the region respects the local limits */
    gasneti_assert(FH_NUM_BUCKETS(req->addr, req->len)
		    				<= fhc_MaxVictimBuckets);
    FH_TABLE_ASSERT_LOCKED;

    priv = fhi_find_priv(fh_mynode, req->addr, req->len);
    if_pf (priv == NULL) {
	/* Firehose MISS, now must pin it */

	/* Try to look for opportunities to merge adjacent pinned regions.
	 * The hash table is such that any region completely covered by
	 * the new region will no longer get any hits.  So, such regions will
	 * eventually end up being recycled from the FIFO.
	 */
	fhi_merge_regions(&pin_region, req->addr, req->len);

	/* XXX/PHH create in-TRANSIT "priv" here, before UNLOCK in Wait... */

	num_unpin = fh_WaitLocalFirehoses(1, &unpin_region);
	gasneti_assert ((num_unpin == 0) || (num_unpin == 1));

	FH_TABLE_UNLOCK;
	firehose_move_callback(fh_mynode,
				&unpin_region, num_unpin,
				&pin_region, 1);
	FH_TABLE_LOCK;

#if 0
	/* XXX/PHH commit the in-TRANSIT "priv" here */
#else
	priv = fh_create_priv(fh_mynode, &pin_region);

	FH_BSTATE_SET(priv, fh_used);
	FH_SET_USED(priv);
	FH_TRACE_BUCKET(priv, INIT);
	FH_BUCKET_REFC(priv)->refc_l = 1;
	FH_BUCKET_REFC(priv)->refc_r = 0;
#endif
    }
    else {
	/* HIT, just need to acquire */
	fh_priv_acquire_local(1, priv);
    }

    CP_PRIV_TO_REQ(req, priv);

    return;
}

void
fh_commit_try_local_region(firehose_request_t *req)
{
    firehose_private_t *priv;

    gasneti_assert(req != NULL);
    gasneti_assert(req->node == fh_mynode);

    FH_TABLE_ASSERT_LOCKED;

    /* Make sure the size of the region respects the local limits */
    gasneti_assert(FH_NUM_BUCKETS(req->addr, req->len)
		    				<= fhc_MaxVictimBuckets);

    /* We *MUST* be commiting the most recent lookup */
    priv = fhi_lookup_cache;
    gasneti_assert(priv != NULL);
    gasneti_assert(req->addr >= FH_BADDR(priv));
    gasneti_assert(fh_req_end(req) <= fh_priv_end(priv));

    fh_priv_acquire_local(1, priv);
    CP_PRIV_TO_REQ(req, priv);
}

void
fh_release_local_region(firehose_request_t *request)
{
        FH_TABLE_ASSERT_LOCKED;
	gasneti_assert(request != NULL);
	gasneti_assert(request->node == fh_mynode);
	gasneti_assert(request->internal != NULL);

	fh_priv_release_local(1, request->internal);
	fh_AdjustLocalFifoAndPin(fh_mynode, NULL, 0);

	return;
}

/* ##################################################################### */
/* REMOTE PINNING                                                        */
/* ##################################################################### */

void
fhi_hang_callback(firehose_private_t *priv, firehose_request_t *req, 
		  firehose_completed_fn_t callback, void *context)
{
    fh_completion_callback_t *ccb = fh_alloc_completion_callback();

    ccb->flags = FH_CALLBACK_TYPE_COMPLETION;
    ccb->request = req;
    ccb->callback = callback;
    ccb->context = context;

    ccb->fh_tqe_next = (fh_completion_callback_t *) priv->fh_tqe_next;
    priv->fh_tqe_next = (firehose_private_t *) ccb;
                                                                                                              
#if 0	/* XXX: need this to free the callback from req_free
	   Also need it to point to 'priv'. */
    gasneti_assert(req->internal == NULL);
    req->internal = (firehose_private_t *) ccb;
#endif
                                                                                                              
    FH_TRACE_BUCKET(priv, PENDADD);
                                                                                                              
    GASNETI_TRACE_PRINTF(C, ("Firehose Pending ADD priv=%p "
                             "(%p,%d), req=%p", priv,
                             (void *) FH_BADDR(priv),
			     FH_NODE(priv->bucket),
                             req));

    req->flags |= FH_FLAG_PENDING;
}

void
fh_acquire_remote_region(firehose_request_t *req, 
		         firehose_completed_fn_t callback, void *context,
                         uint32_t flags,
                         firehose_remotecallback_args_t *remote_args)
{
    firehose_private_t *priv;
    gasnet_node_t node;

    gasneti_assert(req != NULL);
    gasneti_assert(req->node != fh_mynode);
    gasneti_assert(req->len <= fhi_MaxRegionSize);

    node = req->node;

    /* Make sure the size of the region respects the remote limits */
    gasneti_assert(FH_NUM_BUCKETS(req->addr, req->len)
		    				<= fhc_RemoteBucketsM);
    FH_TABLE_ASSERT_LOCKED;

    priv = fhi_find_priv(node, req->addr, req->len);
    if_pf (priv == NULL) {
	/* MISS */
    	char *payload[2*sizeof(firehose_region_t) +
		      sizeof(firehose_remotecallback_args_t)];
    	firehose_region_t *pin_region = (firehose_region_t *)payload;
    	firehose_region_t *unpin_region = pin_region + 1;
	size_t payload_size = sizeof(firehose_region_t);
	int num_unpin = 0;

	pin_region->addr = req->addr;
	pin_region->len  = req->len;

	/* Acquire resources for the pinning */
	if_pt (fhc_RemoteBucketsM > fhc_RemoteBucketsUsed[node]) {
	    fhc_RemoteBucketsUsed[node]++;
	}
	else {
	    num_unpin = fh_WaitRemoteFirehoses(node, 1, unpin_region);
	    payload_size += sizeof(firehose_region_t);
	    gasneti_assert(num_unpin == 1);
	}
	gasneti_assert ((num_unpin == 0) || (num_unpin == 1));

	/* Create the "pending bucket" */
        priv = fh_create_priv(node, pin_region);
        FH_BSTATE_SET(priv, fh_pending);
        FH_SET_REMOTE_PENDING(priv);
        FH_TRACE_BUCKET(priv, INIT);
	fhi_hang_callback(priv, req, callback, context);
                                                                                                              
	/* Assemble AM payload */
	if (flags & FIREHOSE_FLAG_ENABLE_REMOTE_CALLBACK) {
	    memcpy(unpin_region + num_unpin, remote_args,
		   sizeof(firehose_remotecallback_args_t));
	    payload_size += sizeof(firehose_remotecallback_args_t);
	}

	FH_TABLE_UNLOCK;

	#ifdef FIREHOSE_UNBIND_CALLBACK
	if (num_unpin)
	    firehose_unbind_callback(node, unpin_region, 1);
	#endif

	MEDIUM_REQ(4,5,
		   (node,
		    fh_handleridx(fh_am_move_reqh),
		    payload,
		    payload_size,
		    flags,
		    1,
		    num_unpin,
		    priv));
    }
    else if_pf (FH_IS_REMOTE_PENDING(priv)) {
	/* HIT Pending */
	fhi_hang_callback(priv, req, callback, context);
	FH_BUCKET_REFC(priv)->refc_r++;
	gasneti_assert(FH_BUCKET_REFC(priv)->refc_r > 0);
	FH_TRACE_BUCKET(priv, PENDING);
	FH_TABLE_UNLOCK;
    }
    else {
	/* "pure" HIT */
	fh_priv_acquire_remote(node, priv);
	CP_PRIV_TO_REQ(req, priv);
	req->flags |= FH_FLAG_PINNED;
	FH_TABLE_UNLOCK;
    }

    FH_TABLE_ASSERT_UNLOCKED;
}

void
fh_commit_try_remote_region(firehose_request_t *req)
{
    firehose_private_t *priv;

    gasneti_assert(req != NULL);
    gasneti_assert(req->node != fh_mynode);

    FH_TABLE_ASSERT_LOCKED;

    /* Make sure the size of the region respects the remote limits */
    gasneti_assert(FH_NUM_BUCKETS(req->addr, req->len)
		    				<= fhc_MaxRemoteBuckets);

    /* We *MUST* be commiting the most recent lookup */
    priv = fhi_lookup_cache;
    gasneti_assert(priv != NULL);
    gasneti_assert(req->addr >= FH_BADDR(priv));
    gasneti_assert(fh_req_end(req) <= fh_priv_end(priv));

    fh_priv_acquire_remote(req->node, priv);
    CP_PRIV_TO_REQ(req, priv);
}

void
fh_release_remote_region(firehose_request_t *request)
{
        FH_TABLE_ASSERT_LOCKED;
	gasneti_assert(request != NULL);
	gasneti_assert(request->node != fh_mynode);
	gasneti_assert(request->internal != NULL);
	gasneti_assert(!FH_IS_REMOTE_PENDING(request->internal));

	fh_priv_release_remote(request->node, request->internal);

        gasneti_assert(fhc_RemoteVictimFifoBuckets[request->node]
                        <= fhc_RemoteBucketsM);

	return;
}

/*
 * This function is called by the Firehose reply once a firehose request to pin
 * functions covered into a region completes.
 *
 * The function identifies the private_t's that were marked as 'pending'.
 * These 'pending buckets' may or may not have requests associated to them.
 * If they do, then requests pending a callback are added to a list to be run.
 *
 * The function returns the amount of callbacks that were added to the list of
 * pending requests pointing to the 'PendQ' parameter.
 */
int
fh_find_pending_callbacks(gasnet_node_t node, firehose_region_t *region,
			  int nreg, void *context, fh_pollq_t *PendQ)
{
	firehose_private_t		*priv = context;
	fh_completion_callback_t	*ccb;
	int		callspend = 0;
	int		i;

	FH_TABLE_ASSERT_LOCKED;

	/* Sanity checks */
	gasneti_assert(priv != NULL);
	gasneti_assert(node != fh_mynode);
	gasneti_assert(node == FH_NODE(priv));
	gasneti_assert(nreg == 1);

	FH_STAILQ_INIT(PendQ);

	/* Make sure the private_t was set as pending */
	gasneti_assert(FH_IS_REMOTE_PENDING(priv));
	FH_BSTATE_ASSERT(priv, fh_pending);

	/* Update priv to reflect the reply */
	fh_update_priv(priv, region);

	/* Now make the private_t not pending */
	FH_UNSET_REMOTE_PENDING(priv);
	FH_BSTATE_SET(priv, fh_used);

	/* Queue the callbacks */
	ccb = (fh_completion_callback_t *) priv->fh_tqe_next;
	gasneti_assert(ccb != NULL);

	while (ccb != FH_COMPLETION_END) {
		firehose_request_t		*req;
		fh_completion_callback_t	*next;

		gasneti_assert(ccb->flags & FH_CALLBACK_TYPE_COMPLETION);

		next = (fh_completion_callback_t *) ccb->fh_tqe_next;
		req = ccb->request;

		gasneti_assert(req && (req->flags & FH_FLAG_PENDING));
		req->flags &= ~FH_FLAG_PENDING;
		req->internal = priv;

		FH_STAILQ_INSERT_TAIL(PendQ, (fh_callback_t *) ccb);
		GASNETI_TRACE_PRINTF(C,
			    ("Firehose Pending Request (%p,%d) "
			     "enqueued %p for callback", 
			     (void *) req->addr, req->len, req));
		callspend++;

		ccb = next;
	}

	FH_SET_USED(priv);

	return callspend;
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
	int i, j;
	unsigned long firehoses, m_prepinned;
	unsigned med_regions;
	int b_prepinned = 0;
	int num_nodes = gasnet_nodes();

        /* Initialize the Bucket tables */
        fh_BucketTable1 = fh_hash_create(1<<16); /* 64k */
        fh_BucketTable2 = fh_hash_create(1<<17); /* 128k */

	i = 1.2 * FIREHOSE_CLIENT_MAXREGIONS;	/* factor 1.2 is arbitrary */
	/* round 'i' up to a power of two: */
	for (j = 1; j < i; j *= 2) { /* nothing */ }
       	fh_PrivTable = fh_hash_create(j);

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
		unsigned long	M_min = FH_BUCKET_SIZE * num_nodes * 1024;

		/* Want at least 4k buckets of victim FIFO */
		unsigned long	VM_min = FH_BUCKET_SIZE * 4096;

		/* Want at least 1 region per node */
		/* XXX/PHH THIS IS REALLY A BARE MINIMUM */
		unsigned long	R_min = num_nodes;

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
			    "than the minimum %ld (%ld buckets)", M_min, 
			    M_min >> FH_BUCKET_SHIFT);

		if_pf (param_VM < VM_min)
			gasneti_fatalerror("GASNET_MAXVICTIM_M is less than "
			    "the minimum %ld (%ld buckets)", VM_min,
			    VM_min >> FH_BUCKET_SHIFT);

		if_pf (param_M - m_prepinned < M_min)
			gasneti_fatalerror("Too many bytes in initial"
			    " pinned regions list (%d) for current "
			    "GASNET_FIREHOSE_M parameter (%ld)", 
			    b_prepinned, param_M);

		if_pf (param_R < R_min)
			gasneti_fatalerror("GASNET_FIREHOSE_R is less"
			    "than the minimum %ld", R_min);

		if_pf (param_VR < VR_min)
			gasneti_fatalerror("GASNET_MAXVICTIM_R is less than "
			    "the minimum %ld", VR_min);

		if_pf (param_R - num_reg < R_min)
			gasneti_fatalerror("Too many regions passed on initial"
			    " pinned bucket list (%ld) for current "
			    "GASNET_FIREHOSE_R parameter (%ld)", 
			    (unsigned long)num_reg, param_R);
	}

	/* 
	 * Set local parameters
	 */
	fhc_LocalOnlyBucketsPinned = num_reg;
	fhc_LocalVictimFifoBuckets = 0;
	fhc_MaxVictimBuckets = num_reg + param_VR;
	fhi_MaxRegionSize = param_RS;


	/* 
	 * Set remote parameters
	 */
	firehoses = MIN(param_R, (param_M - m_prepinned) / param_RS);
	fhc_RemoteBucketsM = num_nodes > 1
				? firehoses / (num_nodes - 1)
				: firehoses;

	GASNETI_TRACE_PRINTF(C, 
		    ("Maximum pinnable=%ld\tMax allowed=%ld", 
		     (firehoses + param_VR) * param_RS + m_prepinned,
		     (unsigned long)max_pinnable_memory));
	gasneti_assert((firehoses + param_VR) * param_RS + m_prepinned
						<= max_pinnable_memory);


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

		priv = fh_create_priv(fh_mynode, &regions[i]);

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
		GASNETI_TRACE_PRINTF(C, ("Firehose Maxvictim=%ld (fh=%d)",
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
	firehose_private_t *priv;

        fh_hash_destroy(fh_BucketTable2);
        fh_hash_destroy(fh_BucketTable1);
        fh_hash_destroy(fh_PrivTable);

	priv = fhi_priv_freelist;
	while (priv != NULL) {
		firehose_private_t *next = priv->fh_next;
		gasneti_free(priv);
		priv = next;
	}
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
	firehose_private_t	*priv;
	int			num_pin = 0;
	int			i;

	FH_TABLE_LOCK;

	GASNETI_TRACE_PRINTF(C, ("Firehose move request: new=%d, old=%d",
			r_new, r_old));

	gasneti_assert(r_new == 1);	/* a feature of FIREHOSE_REGION */

	priv = fhi_find_priv(fh_mynode, new_reg->addr, new_reg->len);
	if_pt (priv) {
		/* HIT in table */
		fh_priv_acquire_local(0, priv);
		CP_PRIV_TO_REG(new_reg, priv);
	}
	else {
		/* MISSED in table */

		/* Try to look for opportunities to merge adjacent pinned regions.
		 * The hash table is such that any region completely covered by
		 * the new region will no longer get any hits.  So, such regions will
		 * eventually end up being recycled from the FIFO.
		 */
		fhi_merge_regions(new_reg, new_reg->addr, new_reg->len);

		/* XXX/PHH create in-TRANSIT "priv" here */

		num_pin = 1;
	}


	GASNETI_TRACE_PRINTF(C, ("Firehose move request: pin new=%d",
				 num_pin));

	/* Release the "old" regions, potentially overcommiting the FIFO */
	/* XXX: would like to do away with region_to_priv */
	for (i=0; i < r_old; ++i) {
		fh_priv_release_local(0, fh_region_to_priv(&(old_reg[i])));
	}

	/* Pin the region if needed and fix any FIFO overcommit */
	fh_AdjustLocalFifoAndPin(node, new_reg, num_pin);

	/* Finish table entry for newly pinned region */
	if (num_pin) {
#if 0
		/* XXX/PHH commit the in-TRANSIT "priv" here */
#else
		priv = fh_create_priv(fh_mynode, new_reg);

		FH_BSTATE_SET(priv, fh_used);
		FH_SET_USED(priv);
		FH_TRACE_BUCKET(priv, INIT);
		FH_BUCKET_REFC(priv)->refc_l = 0;
		FH_BUCKET_REFC(priv)->refc_r = 1;
#endif
	}

	FH_TABLE_UNLOCK;

	return;
}

#endif

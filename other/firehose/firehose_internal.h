#include <inttypes.h>

/* firehose_internal.h: Internal Header file
 */

#if SIZEOF_VOID == 32
typedef uint32_t	fh_uint_t;
typedef int32_t		fh_int_t;
#elif SIZEOF_VOID == 64
typedef uint64_t	fh_uint_t;
typedef int64_t		fh_int_t;
#endif

/* fh_bucket_t
 *
 * The firehose bucket type is a descriptor for a single page (or multiple amount
 * of pages according to the ability for the underlying memory allocator to
 * allocate in multiples of GASNETI_PAGESIZE).
 *
 * The current implementation equates one bucket to one page.
 *
 * Under both firehose-page and firehose-region, bucket descriptors for all the
 * buckets contained in the region to be pinned are added to the firehose hash
 * table (for both remote and local pins).
 */

struct _fh_bucket_t {
        fh_int_t         fh_key;                 /* cached key for hash table */
        void            *fh_next;		 /* linked list in hash table */
						 /* _must_ be in this order */

	#ifdef FIREHOSE_REGION			/* buckets have a list of regions */
	firehose_private_t	*fh_region_list;
	#endif

	struct _fh_bucket_t	*fh_fifo_next;	/* 0 when not in FIFO */
        union { 
		struct _fh_bucket_t	*fifo_prev;	/* used in FIFO */
		unsigned int		 refcount;	/* 0 in FIFO */
        } _fr_union;
} fh_bucket_t;

#ifdef FIREHOSE_PAGE
/* Under firehose-page, the private type is essentially a bucket as the
 * firehose table holds no state about regions.  Within the request_t, the
 * private pointer is essentially a cached pointer to the first page of the
 * described region.
 */
struct _firehose_private_t {
	fh_bucket_t	bucket;
};

#define fhi_node(pri)      ((pri)->bucket.h_key & FHI_PAGE_MASK)
#define fhi_paddr(pri)     ((pri)->bucket.h_key & ~FHI_PAGE_MASK)
#define fhi_fifo_prev(pri) ((pri)->bucket._fr_union.fifo_prev)
#define fhi_fifo_next(pri) ((pri)->bucket.fh_fifo_next)
#define fhi_refcount(pri)  ((pri)->bucket._fr_union.refcount)
#define fhi_in_fifo(pri)   ((pri)->bucket.fh_fifo_next != 0)
#define fhi_length(pri)	   (GASNETI_PAGESIZE)

#elif defined(FIREHOSE_REGION)
/* Under firehose-region, the private type requires a client type to be inlined
 * and the region's length to be specified (the region's base address and
 * destination node may be extracted from the pointer to the first bucket of
 * the region).
 *
 * Although all buckets covering pinned regions are hashed just as in
 * firehose-page, firehose-region additionally hashes the firehose_private_t
 * type.  
 */
struct _firehose_private_t {
	size_t		len;

	struct _fh_bucket_t	*bucket;	/* pointer to first bucket */

	firehose_client_t	client;
};

#define fhi_node(pri)      ((pri)->bucket->h_key & FHI_PAGE_MASK)
#define fhi_paddr(pri)     ((pri)->bucket->h_key & ~FHI_PAGE_MASK)
#define fhi_fifo_prev(pri) ((pri)->bucket->_fr_union.fifo_prev)
#define fhi_fifo_next(pri) ((pri)->bucket->fh_fifo_next)
#define fhi_refcount(pri)  ((pri)->bucket->_fr_union.refcount)
#define fhi_in_fifo(pri)   ((pri)->bucket->fh_fifo_next != 0)
#define fhi_regions(pri)   ((pri)->bucket->fh_region_list)
#define fhi_length(pri)	   ((pri)->len)

#endif

#define FH_PAGE_MASK	(GASNETI_PAGESIZE-1)


#include <inttypes.h>

/* firehose_internal.h: Internal Header file
 */

typedef uintptr_t	fh_uint_t;
typedef intptr_t	fh_int_t;

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


/*
 * Conduit Features	gm-conduit	vapi-conduit	sci-conduit
 * ------------------------------------------------------------------
 * flavour		page		region		?
 * client_t		no		yes		yes
 * bind callback	no		yes		yes
 * unbind callback	no		yes		yes
 *
 * Callbacks		gm-conduit	vapi-conduit	sci-conduit
 * ------------------------------------------------------------------
 * move callback	unpins/pins	repins ?	unpins,	
 * 							selects segmentId,
 * 							stores sci_local_segment_t
 *
 * bind callback	n/a		?		connects to segmentId,
 * 							stores sci_remote_segment_t
 *
 * unbind callback	n/a		?		disconnects from sci_remote_segment_t
 */

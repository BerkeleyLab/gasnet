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

#ifdef FIREHOSE_PAGE
struct _firehose_private_t {
#elif defined(FIREHOSE_REGION)
struct _fh_bucket_t {
#endif
        fh_int_t         fh_key;                 /* cached key for hash table */
        void            *fh_next;		 /* linked list in hash table */
						 /* _must_ be in this order */

	struct _fh_bucket_t	*fh_fifo_next;	/* NULL when not in FIFO */
        union { 
		struct _fh_bucket_t	*fifo_prev;	/* used in FIFO */
		unsigned int		 refcount;	/* 0 in FIFO */
        } _fr_union;
};

#ifdef FIREHOSE_PAGE
/* Under firehose-page, the private type is essentially a bucket as the
 * firehose table holds no state about regions.  */
typedef struct _firehose_private_t	fh_bucket_t;

#define fhi_node(pri)      ((pri)->h_key & FHI_PAGE_MASK)
#define fhi_paddr(pri)     ((pri)->h_key & ~FHI_PAGE_MASK)
#define fhi_fifo_prev(pri) ((pri)->_fr_union.fifo_prev)
#define fhi_fifo_next(pri) ((pri)->fh_fifo_next)
#define fhi_refcount(pri)  ((pri)->_fr_union.refcount)
#define fhi_in_fifo(pri)   ((pri)->fh_fifo_next != 0)
#define fhi_length(pri)	   (GASNETI_PAGESIZE)

#elif defined(FIREHOSE_REGION)
/* Under firehose-region, the private type requires a client type to be inlined
 * if FIREHOSE_CLIENT_T is defined and the region's length to be specified (the
 * region's base address and destination node may be extracted from the pointer
 * to the first bucket of the region).
 *
 * Although all buckets covering pinned regions are hashed just as in
 * firehose-page, firehose-region additionally hashes the firehose_private_t
 * type.  
 */
typedef struct _fh_bucket_t	fh_bucket_t;

struct _firehose_private_t {
	fh_int_t	fh_key;			/* cached key for hash table */
	void		*fh_next;		/* linked list in hash table */
						/* _must_ be in this order */

	size_t		len;
	fh_bucket_t		*bucket;	/* pointer to first bucket */

	#ifdef FIREHOSE_CLIENT_T
	firehose_client_t	client;
	#endif
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

/*
 * Both -page and -region implement these functions.
 *
 * Reusable functions are found in firehose.c and flavour-specific 
 * functions should be in firehose_page.c and firehose_region.c
 *                                                                       */
/* ##################################################################### */

/* ##################################################################### */
/* Request type freelists (COMMON)                                       */
/* ##################################################################### */
			/* Allocate a request type                       */
firehose_request_t *	fh_request_new(gasnete_threaddata_t *const thread);
			/* Return the request type to the freelist       */
void			fh_request_free(firehose_request_t *req);

/* ##################################################################### */
/* Firehose Hash Table Utility (COMMON, firehose_hash.c)                 */
/* The hash table utility functions can be used for hashing buckets (and
 * regions in firehose-region                                            */
/* ##################################################################### */

struct _fh_hash_t;
typedef struct _fh_hash_t fh_hash_t;

extern fh_hash_t	*fh_BucketTable;
#ifdef FIREHOSE_REGION
extern fh_hash_t	*fh_RegionTable;
#endif

fh_hash_t *	fh_hash_create(size_t entries, 
			   int (*compare)(fh_int_t key1, void *key2));
void	fh_hash_destroy(fh_hash_t *hash);
void *	fh_hash_find(fh_hash_t *hash, fh_int_t key);
void * 	fh_hash_insert(fh_hash_t *hash, fh_int_t key, void *newval);
void *	fh_hash_delete(fh_hash_t *hash, void *val);

/* ##################################################################### */
/* Bucket (local and remote) operations (COMMON, firehose.c)             */
/* ##################################################################### */
		/* Returns a descriptor given an existing bucket address */
fh_bucket_t *	fh_bucket_lookup(gasnet_node_t node, uintptr_t bucket_addr);
		/* Adds the bucket to the table and returns its desc.    */
fh_bucket_t *	fh_bucket_add(gasnet_node_t node, uintptr_t bucket_addr);
		/* Removes the bucket from the table                     */
void		fh_bucket_remove(fh_bucket_t *);
		/* Returns the refcount associated with the bucket	 */
int		fh_bucket_refcount(fh_bucket_t *);
#define 	fh_bucket_infifo(bucket) (fh_bucket_refcount(bucket) == 0)
		/* Releases the bucket (decrements the refcount)         */
int		fh_bucket_release(fh_bucket_t *);
		/* Acquires the bucket (increments the refcount). _ONLY_ 
		 * valid if the bucket already exists in the table       */
int		fh_bucket_acquire(fh_bucket_t *);

/* ##################################################################### */
/* Region querying (SPECIFIC)                                            */
/* ##################################################################### */
		/* Return a matching private if the region is pinned     */
firehose_private_t *	fh_region_ispinned(gasnet_node_t node, 
					   uintptr_t addr, size_t len);

/* ##################################################################### */
/* Firehose FIFO (COMMON with private_t *)                               */
/* ##################################################################### */
			/* Adds private to the front of the FIFO         */
void	fh_fifo_push(gasnet_node_t node, firehose_private_t *);
			/* Removes the last private from the FIFO        */
firehose_private_t *	fh_fifo_pop_last(gasnet_node_t node);
			/* Removes private from anywhere in the FIFO     */
void	fh_fifo_remove(gasnet_node_t node, firehose_private_t *);

/* ##################################################################### */
/* Local bucket FIFO (SPECIFIC)                                          */
/* ##################################################################### */
			/* Removes 'buckets' buckets from the local victim
			 * FIFO.  It is up to the client to make sure there 
			 * are enough buckets/regions in the victim FIFO */
void			fh_victim_unpin(int buckets);
			/* Pushes the private at the head of victim FIFO */
void			fh_victim_push(firehose_private_t *);
			/* Remotes the private anywhere in the FIFO */
void			fh_victim_remove(firehose_private_t *);
			/* Removes the last private from the tail */
firehose_private_t *	fh_victim_pop_last();

/* ##################################################################### */
/* Firehose internal pinning functions                                   */
/* ##################################################################### */
/* See documentation in firehose_page.c                                  */
firehose_private_t *	fh_acquire_local_region(firehose_region_t *region);
void			fh_release_local_region(firehose_request_t *req);

firehose_private_t *	fh_acquire_remote_region(firehose_region_t *region);
void			fh_release_remote_region(firehose_request_t *req);

/* Utility Macros */
#define FH_PAGE_MASK	(GASNETI_PAGESIZE-1)
#define FH_ADDR_ALIGN(addr) (GASNETI_ALIGNDOWN(addr, FH_BUCKET_SIZE)
#define FH_SIZE_ALIGN(addr,len)	(GASNETI_ALIGNUP(addr+len, FH_BUCKET_SIZE)-\
				 GASNETI_ALIGNDOWN(addr, FH_BUCKET_SIZE)

/* values for firehose_private_t * */
#define FH_REQ_UNPINNED	((firehose_private_t *) 0)

/* Macro to ease looping over buckets in a memory region.  'end' here is
 * defined as 'start + len - 1'.  All parameters should be 'uintptr_t'.
 */
#define FH_FOREACH_BUCKET(start,end,bucket_addr)			\
		for ((bucket_addr) = (start); (bucket_addr) <= (end);	\
		    (bucket_addr) += FH_BUCKET_SIZE)

#define FH_FOREACH_BUCKET_REV(start,end,bucket_addr)			\
		for ((bucket_addr) = FH_ADDR_ALIGN(end);		\
			(bucket_addr) >= (start);			\
			(bucket_addr) -= FH_BUCKET_SIZE)

/* Macros to implement a do/while loop over a region */
#define FH_DO_BUCKET(start,bucket_addr)					\
		(bucket_addr) = (start); do {
#define FH_WHILE_BUCKET(end,bucket_addr)				\
		} while ((bucket_addr) <= (end) && 			\
			(bucket_addr) += FH_BUCKET_SIZE)

#define FH_FILL_REGION(reg, addr, length) do {				\
		(reg)->addr = FH_ADDR_ALIGN(addr);			\
		(reg)->len  = FH_SIZE_ALIGN(addr, addr+length);  	\
	} while (0)

#ifdef FIREHOSE_CLIENT_T
#define FH_COPY_REGION_TO_REQUEST(req, reg) do {			\
		(req)->addr = (uintptr_t) (reg)->addr;			\
		(req)->len  = (size_t) (reg)->len;			\
		memcpy(&((req)->client), &((reg)->client), 		\
		    sizeof(firehose_client_t));				\
	} while (0)
#define FH_COPY_REQUEST_TO_REGION(reg, req) do {			\
		(reg)->addr = (uintptr_t) (req)->addr;			\
		(reg)->len  = (size_t) (req)->len;			\
		memcpy(&((reg)->client), &((req)->client), 		\
		    sizeof(firehose_client_t));				\
	} while (0)
#else
#define FH_COPY_REGION_TO_REQUEST(req, reg) do {			\
		(req)->addr = (uintptr_t) (reg)->addr;			\
		(req)->len  = (size_t) (reg)->len;			\
	} while (0)
#define FH_COPY_REQUEST_TO_REGION(reg, req) do {			\
		(reg)->addr = (uintptr_t) (req)->addr;			\
		(reg)->len  = (size_t) (req)->len;			\
		memcpy(&((reg)->client), &((req)->client), 		\
		    sizeof(firehose_client_t));				\
	} while (0)
#endif

#define firehose_pin(pin)	firehose_move_callback(pin,1,NULL,0)
#define firehose_unpin(pin)	firehose_move_callback(NULL,0,pin,1)

#ifdef GASNET_TRACE
#define FH_NUMPINNED_DECL	int _fh_numpinned = 0
#define FH_NUMPINNED_INC	_fh_numpinned++
#define FH_NUMPINNED_TRACE_LOCAL	GASNETI_TRACE_EVENT_VAL(C, \
					BUCKET_LOCAL_PINS, _fh_numpinned)
#define FH_NUMPINNED_TRACE_REMOTE	GASNETI_TRACE_EVENT_VAL(C, \
					BUCKET_REMOTE_PINS, _fh_numpinned)
#else
#define FH_NUMPINNED_DECL
#define FH_NUMPINNED_INC
#define FH_NUMPINNED_TRACE_LOCAL
#define FH_NUMPINNED_TRACE_REMOTE
#endif


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
 * unbind callback	n/a		?		disconnects sci_remote_segment_t
 */

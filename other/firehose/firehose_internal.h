#include <inttypes.h>
#include <gasnet_internal.h>	/* gasnet mutex */

/* firehose_internal.h: Internal Header file
 */

/* Some conduits may be able to support running both the completion and remote
 * callbacks from within an AM Handler, in which case there is no need for the
 * client to make progress through firehose_poll().
 */
#if defined(FIREHOSE_REMOTE_CALLBACK_IN_HANDLER) && \
    defined(FIREHOSE_COMPLETION_IN_HANDLER)
#define FH_POLL_NOOP
#endif

typedef uintptr_t	fh_uint_t;
typedef intptr_t	fh_int_t;

extern gasnet_node_t	fh_mynode;

/* 
 * Locks
 */

extern gasneti_mutex_t		fh_table_lock;
extern gasneti_mutex_t		fh_pollq_lock;

#define FH_TABLE_LOCK		gasneti_mutex_lock(&fh_table_lock)
#define FH_TABLE_UNLOCK		gasneti_mutex_unlock(&fh_table_lock)
#define FH_TABLE_ASSERT_LOCKED	gasneti_mutex_assertlocked(&fh_table_lock)
#define FH_TABLE_ASSERT_UNLOCKED gasneti_mutex_assertunlocked(&fh_table_lock)

#define FH_POLLQ_LOCK		gasneti_mutex_lock(&fh_pollq_lock)
#define FH_POLLQ_UNLOCK		gasneti_mutex_unlock(&fh_pollq_lock)

#ifndef FH_BUCKET_SIZE
#define FH_BUCKET_SIZE	GASNETI_PAGESIZE
#endif

#ifndef FH_BUCKET_SHIFT
  #ifdef GASNETT_PAGESHIFT
  #define FH_BUCKET_SHIFT GASNETT_PAGESHIFT
  #else
  #define FH_BUCKET_SHIFT 12
  #endif
#endif

/* Utility Macros */
#define FH_CACHE_LINE_BYTES	(128)
#define FH_PAGE_MASK		(GASNETI_PAGESIZE-1)
#define FH_ADDR_ALIGN(addr)	(GASNETI_ALIGNDOWN(addr, FH_BUCKET_SIZE))
#define FH_SIZE_ALIGN(addr,len)	(GASNETI_ALIGNUP(addr+len, FH_BUCKET_SIZE)-\
				 GASNETI_ALIGNDOWN(addr, FH_BUCKET_SIZE))
#define FH_NUM_BUCKETS(addr,len)(FH_SIZE_ALIGN(addr,len)>>FH_BUCKET_SHIFT)
#define FH_ASSERT_BUCKET_ADDR(bucket) (gasneti_assert((bucket) % FH_BUCKET_SIZE == 0))

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

#if SIZEOF_VOID_P == 4
typedef uint16_t	fh_refc_uint_t;
#else
typedef uint32_t	fh_refc_uint_t;
#endif

/* The 'refcount' type is stored as a logical union with the fh_tqe_prev field
 * and must therefore be the same size as a pointer */
typedef struct _fh_refc_t {
	fh_refc_uint_t	refc_l;
	fh_refc_uint_t	refc_r;
}
fh_refc_t;

/*
 * Bucket and private types
 */

#ifdef DEBUG_BUCKETS
  typedef enum { fh_local_fifo, fh_remote_fifo, fh_pending, fh_used, fh_unused }
  fh_bstate_t;
  #define FH_BSTATE_ASSERT(entry, state) gasneti_assert((entry)->fh_state == state)
  #define FH_BSTATE_SET(entry, state)	 (entry)->fh_state = state
  #else
  #define FH_BSTATE_ASSERT(entry, state)
  #define FH_BSTATE_SET(entry, state)
#endif

#ifdef FIREHOSE_PAGE
typedef struct _firehose_private_t	fh_bucket_t;

struct _firehose_private_t {
        fh_int_t         fh_key;                 /* cached key for hash table */
	#define FH_KEYMAKE(addr,node)	(addr | node)
	#define FH_NODE(priv)    ((priv)->fh_key & FH_PAGE_MASK)
	#define FH_BADDR(priv)   ((priv)->fh_key & ~FH_PAGE_MASK)

        void            *fh_next;		 /* linked list in hash table */
						 /* _must_ be in this order */

	/* FIFO and refcount */
	#ifdef DEBUG_BUCKETS
	fh_bstate_t	fh_state;
	#endif

	fh_bucket_t	*fh_tqe_next;		/* -1 when not in FIFO, 
						   NULL when end of list,
						   else next pointer in FIFO */
	fh_bucket_t	**fh_tqe_prev;		/* refcount when not in FIFO,
						   prev pointer otherwise    */
};

#define FH_BUCKET_REFC(priv) ((fh_refc_t *) (&(priv)->fh_tqe_prev))

/* Local and Remote buckets can be in various states.
 *
 * Local buckets can be in either of these two states:
 *   1. in FIFO (fh_tqe_next != FH_USED_TAG)
 *   2. in USE  (fh_tqe_next == FH_USED_TAG)
 *
 * Remote buckets can be in either of these three states 
 *   1. in USE  (fh_tqe_next == FH_USED_TAG)
 *      a) PENDING (LOCAL reference count == FH_REMOTE_PENDING_TAG)
 *      b) NOT PENDING (LOCAL refcount != FH_REMOTE_PENDING_TAG)
 *   2. in FIFO (fh_tqe_next != FH_USED_TAG)
 */
#define FH_USED_TAG		((fh_bucket_t *) -1)
#define FH_REMOTE_PENDING_TAG	((fh_refc_uint_t) -1)

#define FH_IS_LOCAL_FIFO(priv)	((priv)->fh_tqe_next != FH_USED_TAG)
#define FH_IS_REMOTE_FIFO(priv)	(!FH_IS_REMOTE_PENDING(priv) &&		\
				 (priv)->fh_tqe_next != FH_USED_TAG)
#define FH_SET_USED(priv)	((priv)->fh_tqe_next = FH_USED_TAG)

/* Remote buckets can be in a 'pending' state */
#define FH_IS_REMOTE_PENDING(priv)					\
		(FH_BUCKET_REFC(priv)->refc_l == FH_REMOTE_PENDING_TAG)
#define FH_SET_REMOTE_PENDING(priv)	do { 				\
		FH_BUCKET_REFC(priv)->refc_l = FH_REMOTE_PENDING_TAG;	\
		FH_BUCKET_REFC(priv)->refc_r = 1;			\
		(priv)->fh_tqe_next = FH_USED_TAG; }  while (0)
#define FH_UNSET_REMOTE_PENDING(priv)					\
		(FH_BUCKET_REFC(priv)->refc_l = 0)

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

typedef
struct _fh_bucket_t {
        fh_int_t         fh_key;                 /* cached key for hash table */
	#define FH_KEYMAKE(addr,node)	(addr | node)
	#define FH_NODE(priv)    ((priv)->fh_key & FH_PAGE_MASK)
	#define FH_BADDR(priv)   ((priv)->fh_key & ~FH_PAGE_MASK)

        void            *fh_next;		 /* linked list in hash table */
						 /* _must_ be in this order */
	fh_refc_t	refcounts;
}
fh_bucket_t;

struct _firehose_private_t {
	fh_int_t	fh_key;			/* cached key for hash table */
	void		*fh_next;		/* linked list in hash table */
						/* _must_ be in this order */

	size_t		len;
	fh_bucket_t	*bucket;		/* pointer to first bucket */

	firehose_private_t *fh_tqe_next;	/* NULL when not in FIFO */
	firehose_private_t **fh_tqe_prev;

	#ifdef FIREHOSE_CLIENT_T
	firehose_client_t	client;
	#endif
};
#endif

/*
 * Both -page and -region implement these functions.
 *
 * Reusable functions are found in firehose.c and flavour-specific 
 * functions should be in firehose_page.c and firehose_region.c
 *                                                                       */
/* ##################################################################### */

void	fh_init_plugin(uintptr_t max_pinnable_memory, size_t max_regions, 
		       const firehose_region_t *prepinned_regions,
                       size_t num_reg, firehose_info_t *info);
void	fh_fini_plugin();

/* ##################################################################### */
/* Request type freelists (COMMON)                                       */
/* ##################################################################### */
/* Flags */
#define FH_FLAG_FHREQ	0x01	/* firehose supplied the request_t */
#define FH_FLAG_PINNED	0x02
#define FH_FLAG_PENDING 0x04

			/* Allocate a request type                       */
firehose_request_t *	fh_request_new(firehose_request_t *ureq);
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

fh_hash_t *	fh_hash_create(size_t entries);
void		fh_hash_destroy(fh_hash_t *hash);
void *		fh_hash_find(fh_hash_t *hash, fh_int_t key);
void *		fh_hash_insert(fh_hash_t *hash, fh_int_t key, void *newval);

/* ##################################################################### */
/* Bucket (local and remote) operations (COMMON, firehose.c)             */
/* ##################################################################### */
		/* Initialize the bucket freelist */
void		fh_bucket_init_freelist(int max_buckets_pinned);
		/* Returns a descriptor given an existing bucket address */
fh_bucket_t *	fh_bucket_lookup(gasnet_node_t node, uintptr_t bucket_addr);
		/* Adds the bucket to the table and returns its desc.    */
fh_bucket_t *	fh_bucket_add(gasnet_node_t node, uintptr_t bucket_addr);
		/* Removes the bucket from the table                     */
void		fh_bucket_remove(fh_bucket_t *);

/* The following two functions are not common */
		/* Releases the bucket (decrements the refcount)         */
fh_refc_t *	fh_bucket_release(gasnet_node_t node, fh_bucket_t *);
		/* Acquires the bucket (increments the refcount). _ONLY_ 
		 * valid if the bucket already exists in the table       */
fh_refc_t *	fh_bucket_acquire(gasnet_node_t node, fh_bucket_t *);

/* ##################################################################### */
/* Misc functions (specific to page and region)                          */
/* ##################################################################### */
int	fh_region_ispinned(gasnet_node_t node, firehose_region_t *region);
int	fh_region_partial(gasnet_node_t node, firehose_region_t *region);
unsigned long	fh_getenv(const char *var, unsigned long multiplier);

/* Common Queue Macros for Firehose FIFO and Local Bucket FIFO */
#define FH_TAILQ_HEAD(name, type)	\
struct name {				\
	struct type	*fh_tqh_first;	\
	struct type	**fh_tqh_last;	\
}
#define FH_STAILQ_HEAD(name,type)	FH_TAILQ_HEAD(name,type)

/* QUEUE functions (based on the BSD TAILQ and STAILQ macros of
 * /usr/include/sys/queue.h) */
#define FH_TAILQ_FIRST(head)	((head)->fh_tqh_first)
#define FH_TAILQ_LAST(head)	((head)->fh_tqh_last)
#define FH_TAILQ_EMPTY(head)	((head)->fh_tqh_first == NULL)
#define FH_TAILQ_NEXT(elem)	((elem)->fh_tqe_next)
#define FH_TAILQ_PREV(elem)	((elem)->fh_tqe_prev)

#define FH_STAILQ_FIRST(head)	((head)->fh_tqh_first)
#define FH_STAILQ_LAST(head)	((head)->fh_tqh_last)
#define FH_STAILQ_EMPTY(head)	((head)->fh_tqh_first == NULL)
#define FH_STAILQ_NEXT(elem)	((elem)->fh_tqe_next)

/* Doubles/single list initialization */
#define FH_STAILQ_HEAD_INITIALIZER(head)  { NULL, &(head).fh_tqh_first }
#define FH_TAILQ_HEAD_INITIALIZER(head)   { NULL, &(head).fh_tqh_first }

#define FH_TAILQ_INIT(head)	do {				\
	FH_TAILQ_FIRST((head)) = NULL;				\
	FH_TAILQ_LAST(head) = &FH_TAILQ_FIRST((head));		\
} while (0)
#define FH_STAILQ_INIT(head)	FH_TAILQ_INIT(head)

/* Double/singe list tail addition */
#define FH_TAILQ_INSERT_TAIL(head, elem) do {				\
	FH_TAILQ_NEXT(elem) = NULL;					\
	FH_TAILQ_PREV(elem) = FH_TAILQ_LAST(head);			\
	*(FH_TAILQ_LAST(head)) = (elem);				\
	FH_TAILQ_LAST(head) = &FH_TAILQ_NEXT(elem);			\
} while (0)
#define	FH_STAILQ_INSERT_TAIL(head, elem) do {				\
	FH_STAILQ_NEXT(elem) = NULL;					\
	*(FH_STAILQ_LAST(head)) = (elem);				\
	FH_STAILQ_LAST(head) = &FH_STAILQ_NEXT(elem);			\
} while (0)

#define FH_STAILQ_INSERT_HEAD(head, elem) do {				\
	if ((FH_STAILQ_NEXT(elem) = FH_STAILQ_FIRST(head)) == NULL)	\
		FH_STAILQ_LAST(head) = &FH_STAILQ_NEXT(elem);		\
	FH_STAILQ_FIRST(head) = (elem);					\
} while (0);

#define FH_STAILQ_MERGE(head1, head2) do {				\
	*(FH_STAILQ_LAST(head1)) = FH_STAILQ_FIRST(head2);		\
	FH_STAILQ_LAST(head1) = FH_STAILQ_LAST(head2);			\
} while (0)

/* Double remove anywhere in the list.  The membar at the end prevents a
 * reordering bug that occurs on gcc when the optimizer has strict aliasing
 * enabled.  We are on the lookout to see if this happens elsewhere. */
#define FH_TAILQ_REMOVE(head, elem) do {				\
	if (FH_TAILQ_NEXT(elem) != NULL)				\
		FH_TAILQ_PREV(FH_TAILQ_NEXT(elem)) = 			\
			FH_TAILQ_PREV(elem);				\
	else								\
		FH_TAILQ_LAST(head) = FH_TAILQ_PREV(elem);		\
	*(FH_TAILQ_PREV(elem)) = FH_TAILQ_NEXT(elem);			\
	gasneti_local_membar();						\
} while (0)

/* Single remove from head only */
#define	FH_STAILQ_REMOVE_HEAD(head) do {				\
	if ((FH_STAILQ_FIRST((head)) =					\
	     FH_STAILQ_NEXT(FH_STAILQ_FIRST((head)))) == NULL)		\
		FH_STAILQ_LAST(head) = &FH_STAILQ_FIRST(head);		\
} while (0)

/* Double/single foreach over the list */
#define FH_TAILQ_FOREACH(head, var)					\
	for ((var) = FH_TAILQ_FIRST(head); (var) != NULL;		\
	     (var) = FH_TAILQ_NEXT(var))
#define FH_STAILQ_FOREACH(head, var)					\
	for ((var) = FH_STAILQ_FIRST(head); (var) != NULL;		\
	     (var) = FH_STAILQ_NEXT(var))

/* ##################################################################### */
/* Firehose/Bucket FIFOs and Callback Polling queues                     */
/* ##################################################################### */
FH_TAILQ_HEAD(_fh_fifoq_t, _firehose_private_t);
typedef struct _fh_fifoq_t	fh_fifoq_t;

FH_STAILQ_HEAD(_fh_pollq_t, _fh_callback_t);
typedef struct _fh_pollq_t	fh_pollq_t;


/* There is also a pollqueue which is drained by firehose_poll */
#ifndef FH_POLL_NOOP
extern fh_pollq_t	fh_CallbackFifo;
#endif

/* Each node has a FirehoseFifo */
extern fh_fifoq_t	*fh_RemoteNodeFifo;
extern fh_fifoq_t	fh_LocalFifo;

/* This type is used to abstract the use of different callback types in the
 * same fifo.  The 'flags' parameter is used as a tag to differentiate both
 * types.
 */
typedef
struct _fh_callback_t {
	uint32_t	 	flags;
	struct _fh_callback_t	*fh_tqe_next;
}
fh_callback_t;

#define FH_CALLBACK_TYPE_REMOTE		0x01
#define FH_CALLBACK_TYPE_COMPLETION	0x02

#define FH_CALLBACK_PENDING		0x04

/* The remote callback type is pretty page-specific right now, waiting for
 * firehose-region to catch up before making a "standard" remote_callback_t */
typedef
struct _fh_remote_callback_t {
	uint32_t		flags;
	struct _fh_remote_callback_t	*fh_tqe_next;

	gasnet_node_t			node;
	firehose_remotecallback_args_t	args;

	firehose_region_t		*pin_list;
	size_t				 pin_list_num;
	size_t				 reply_len;

	/* Initiator's request_t */
	firehose_request_t		*request;

}
fh_remote_callback_t;

typedef
struct _fh_completion_callback_t {
	uint32_t		flags;
	struct _fh_completion_callback_t	*fh_tqe_next;

	firehose_completed_fn_t	callback;
	firehose_request_t	*request;
	void			*context;
}
fh_completion_callback_t;
#define FH_COMPLETION_END	((fh_completion_callback_t *) -1)

fh_completion_callback_t *	fh_alloc_completion_callback();
void	fh_free_completion_callback(fh_completion_callback_t *rc);

/* ##################################################################### */
/* Firehose internal pinning functions                                   */
/* ##################################################################### */
/* See documentation in firehose_page.c                                  */
void	fh_acquire_local_region(firehose_region_t *);
void	fh_commit_try_local_region(firehose_region_t *);
void	fh_release_local_region(firehose_request_t *);

firehose_request_t *	fh_acquire_remote_region(gasnet_node_t node, 
				firehose_region_t *reg, 
				firehose_completed_fn_t callback, 
				void *context, uint32_t flags,
		        	firehose_remotecallback_args_t *remote_args,
				firehose_request_t *ureq);
void			fh_commit_try_remote_region(gasnet_node_t node, 
						    firehose_region_t *);
void			fh_release_remote_region(firehose_request_t *);

void			fh_send_firehose_reply(fh_remote_callback_t *);

/* How many buffers (of buffers) to allocate to use as bucket descriptors in
 * hash table */
#define FH_BUCKETS_BUFS	1024

/*
 * Macros to implement do/while and foreach over the region.  When a reference
 * to 'end' is made, it refers to 'start + len - 1'.
 */
#define FH_FOREACH_BUCKET(start,end,bucket_addr)			\
		for ((bucket_addr) = (start); (bucket_addr) <= (end);	\
		    (bucket_addr) += FH_BUCKET_SIZE)
#define FH_FOREACH_BUCKET_REV(start,end,bucket_addr)			\
		for ((bucket_addr) = FH_ADDR_ALIGN(end);		\
			(bucket_addr) >= (start);			\
			(bucket_addr) -= FH_BUCKET_SIZE)
#define FH_DO_BUCKET(start,bucket_addr)					\
		(bucket_addr) = (start); do {
#define FH_WHILE_BUCKET(end,bucket_addr)				\
		} while ((bucket_addr) <= (end) && 			\
			(bucket_addr) += FH_BUCKET_SIZE)

/*
 * Macros to copy client_t to and from region/request
 */
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
	} while (0)
#endif

#if GASNET_TRACE
#define FH_TRACE_BUCKET(bd, bmsg) 					\
	do {								\
		char	msg[64];					\
		fh_refc_t *rp = FH_BUCKET_REFC(bd);			\
		if (FH_NODE(bd) != fh_mynode) {				\
			if (FH_IS_REMOTE_PENDING(bd)) 			\
				sprintf(msg, "rrefc=%d PENDING",	\
				    rp->refc_r);			\
			else if (FH_IS_REMOTE_FIFO(bd))			\
				sprintf(msg, "IN FIFO");		\
			else						\
				sprintf(msg, "rrefc=%d", rp->refc_r);   \
		}							\
		else {							\
			if (FH_IS_LOCAL_FIFO(bd))			\
				sprintf(msg, "IN FIFO");		\
			else						\
				sprintf(msg, "rrefc=%d lrefc=%d",	\
				    rp->refc_r, rp->refc_l);		\
		}							\
		GASNETI_TRACE_PRINTF(C,					\
		    ("Firehose Bucket %s %s node=%d,addr=%p,%s",	\
		     #bmsg, FH_NODE(bd) == fh_mynode ?	 		\
		     "Local " : "Remote",				\
		     FH_NODE(bd), FH_BADDR(bd), msg));			\
	} while (0)

#define FH_NUMPINNED_DECL	int _fh_numpinned = 0
#define FH_NUMPINNED_INC	_fh_numpinned++
#define FH_NUMPINNED_TRACE_LOCAL	GASNETI_TRACE_EVENT_VAL(C, \
					BUCKET_LOCAL_PINS, _fh_numpinned)
#define FH_NUMPINNED_TRACE_REMOTE	GASNETI_TRACE_EVENT_VAL(C, \
					BUCKET_REMOTE_PINS, _fh_numpinned)
#else
#define FH_TRACE_BUCKET(bd, bmsg)
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

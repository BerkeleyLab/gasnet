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
#define FH_PAGE_MASK		(GASNETI_PAGESIZE-1)
#define FH_ADDR_ALIGN(addr)	(GASNETI_ALIGNDOWN(addr, FH_BUCKET_SIZE))
#define FH_SIZE_ALIGN(addr,len)	(GASNETI_ALIGNUP(addr+len, FH_BUCKET_SIZE)-\
				 GASNETI_ALIGNDOWN(addr, FH_BUCKET_SIZE))
#define FH_NUM_BUCKETS(addr,len)(FH_SIZE_ALIGN(addr,len)>>FH_BUCKET_SHIFT)
#define FH_ASSERT_BUCKET_ADDR(bucket) (bucket % FH_BUCKET_SIZE == 0)

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

/*
 * Reference Count for local and remote reference counts.
 *
 * Packed representation, using top 24 bits for remote refcounts and bottom 8
 * bits for local refcount */
typedef uint32_t		fh_refc_t;

#define FH_REFC_ZERO		0
#define FH_LREFC(refc_t)	((refc_t) & 0x000000ff)
#define FH_RREFC(refc_t)	(((refc_t) & 0xffffff00)>>8)

#define FH_LREFCINC(refc_t)	(assert(FH_LREFC(refc_t) < 0xff), (refc_t)++)
#define FH_RREFCINC(refc_t)	(assert(FH_RREFC(refc_t) < 0xffffff),	\
					(refc_t) += 0x00000100)
		
#define FH_REFCSET(refc_t,l,r)	((refc_t) = (((r)<<8) & 0xffffff00) | 	\
					     ((l) & 0x000000ff))
#define FH_REFCRST(refc_t)	((refc_t) = 0)
#define FH_LREFCRST(refc_t)	((refc_t) & 0xffffff00)
#define FH_RREFCRST(refc_t)	((refc_t) & 0x000000ff)
#define FH_LREFCDEC(refc_t)	(assert(FH_LREFC(refc_t) > 0), (refc_t)--)
#define FH_RREFCDEC(refc_t)	(assert(FH_RREFC(refc_t) > 0),		\
					(refc_t) -= 0x00000100)
#define FH_REFC_IS_VICTIM(refc_t)	((refc_t) == 0)

/*
 * Bucket and private types
 */

#ifdef FIREHOSE_PAGE
typedef struct _firehose_private_t	fh_bucket_t;

struct _firehose_private_t {
        fh_int_t         fh_key;                 /* cached key for hash table */
#define FH_KEYMAKE(addr,node)	(addr | node)
#define FH_NODE(priv)    ((priv)->fh_key & FH_PAGE_MASK)  /* bucket's node */
#define FH_BADDR(priv)   ((priv)->fh_key & ~FH_PAGE_MASK) /* bucket address */

        void            *fh_next;		 /* linked list in hash table */
						 /* _must_ be in this order */

	/* FIFO and refcount */
	fh_bucket_t	*fh_tqe_next;		/* -1 when not in FIFO, 
						   NULL when end of list,
						   else next pointer in FIFO */
	fh_bucket_t	**fh_tqe_prev;		/* refcount when not in FIFO,
						   prev pointer otherwise    */
};
#define FH_REFCOUNT(priv) ((fh_refc_t) ((priv)->fh_tqe_prev))

/* A bucket can be in three states:
 * 1. FIFO     : next != -1 and prev != -1
 * 2. USED     : next == -1 and prev != -1
 * 3. PREPINNED: next == -1 and prev == -1
 */
#define FH_IS_FIFO(priv)      ((priv)->fh_tqe_next != (fh_bucket_t *) -1)
#define FH_IS_USED(priv)      ((priv)->fh_tqe_next == (fh_bucket_t *) -1 && \
			       (priv)->fh_tqe_prev != (fh_bucket_t **) -1)
#define FH_IS_PREPINNED(priv) ((priv)->fh_tqe_next == (fh_bucket_t *) -1 && \
			       (priv)->fh_tqe_prev == (fh_bucket_t **) -1)

#define FH_SET_PREPINNED(priv)	do { 					\
		(priv)->fh_tqe_next = (fh_bucket_t *) -1;		\
		(priv)->fh_tqe_prev = (fh_bucket_t **) -1;		\
	} while (0)
#define FH_SET_USED(priv)      ((priv)->fh_tqe_next = (fh_bucket_t *) -1)

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
#define FH_NODE(priv)    ((priv)->fh_key & FH_PAGE_MASK)  /* bucket's node */
#define FH_BADDR(priv)   ((priv)->fh_key & ~FH_PAGE_MASK) /* bucket address */
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
#define FH_REFCOUNT(priv) ((fh_refc_t) ((priv)->fh_tqe_prev))

#define FH_IS_FIFO(priv)      
#define FH_IS_USED(priv)     
#define FH_IS_PREPINNED(priv) 
#define FH_SET_PREPINNED(priv)
#define FH_SET_USED(priv) 
#endif

/*
 * Both -page and -region implement these functions.
 *
 * Reusable functions are found in firehose.c and flavour-specific 
 * functions should be in firehose_page.c and firehose_region.c
 *                                                                       */
/* ##################################################################### */

void	fh_init_plugin(uintptr_t max_pinnable_memory, size_t max_regions, 
		       firehose_region_t *prepinned_regions, size_t num_reg,
		       firehose_info_t *info);
void	fh_fini_plugin();

/* ##################################################################### */
/* Request type freelists (COMMON)                                       */
/* ##################################################################### */
/* Flags */
#define FH_FLAG_FHREQ	0x01	/* firehose supplied the request_t */

			/* Allocate a request type                       */
firehose_request_t *	fh_request_new();
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
fh_refc_t	fh_bucket_release(gasnet_node_t node, fh_bucket_t *);
		/* Acquires the bucket (increments the refcount). _ONLY_ 
		 * valid if the bucket already exists in the table       */
fh_refc_t	fh_bucket_acquire(gasnet_node_t node, fh_bucket_t *);

/* ##################################################################### */
/* Misc functions (specific to page and region)                          */
/* ##################################################################### */
		/* Return a matching private if the region is pinned     */
int	fh_region_ispinned(gasnet_node_t node, uintptr_t addr, size_t len);
unsigned long	fh_getenv(const char *var, unsigned long multiplier);

/* Common Queue Macros for Firehose FIFO and Local Bucket FIFO */
#define FH_TAILQ_HEAD(name, type)	\
struct name {				\
	struct type	*fh_tqh_first;	\
	struct type	**fh_tqh_last;	\
}
#define FH_STAILQ_HEAD(name,type)	FH_TAILQ_HEAD

/* Create a fh_fifoq_t type for local and remote fifos */
FH_TAILQ_HEAD(_fh_fifoq_t, _firehose_private_t);
typedef struct _fh_fifoq_t	fh_fifoq_t;

/* Create a fh_pollq_t type to hold firehose callbacks queued 
 * from within an AM Reply handler */
FH_TAILQ_HEAD(_fh_pollq_t, _fh_callback_t);
typedef struct _fh_pollq_t	fh_pollq_t;

/* There is also a pollqueue which is drained by firehose_poll */
#ifndef FH_POLL_NOOP
fh_pollq_t	fh_CallbackFifo;
#endif

/* Each node has a FirehoseFifo */
fh_fifoq_t	*fh_RemoteNodeFifo;
fh_fifoq_t	fh_LocalFifo;

/* This type is used to abstract the use of different callback types in the
 * same fifo.  The 'flags' parameter is used as a tag to differentiate both
 * types.
 */
typedef
struct _fh_callback_t {
	uint8_t		 	flags;
	struct _fh_callback_t	*fh_tqe_next;
}
fh_callback_t;

#define FH_CALLBACK_TYPE_REMOTE		0x01
#define FH_CALLBACK_TYPE_COMPLETION	0x02

typedef
struct _fh_remote_callback_t {
	uint8_t		flags;

	struct _fh_remote_callback_t	*fh_tqe_next;

	gasnet_node_t			node;
	firehose_remotecallback_args_t	args;

	firehose_region_t		*pin_list;
	size_t				 pin_list_num;
	size_t				reply_len;

	/* Parameters for initator's completion callback. Sent as
	 * parameters in the firehose reply AM */
	firehose_completed_fn_t		callback;
	firehose_request_t		*request;
	void				*context;

}
fh_remote_callback_t;

typedef
struct _fh_completion_callback_t {
	uint8_t		flags;

	struct _fh_completion_callback_t	*fh_tqe_next;

	firehose_completed_fn_t	callback;
	firehose_request_t	*request;
	void			*context;
}
fh_completion_callback_t;

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

/* Double remove anywhere in the list */
#define FH_TAILQ_REMOVE(head, elem) do {				\
	if (FH_TAILQ_NEXT(elem) != NULL)				\
		FH_TAILQ_PREV(FH_TAILQ_NEXT(elem)) = 			\
			FH_TAILQ_PREV(elem);				\
	else								\
		FH_TAILQ_LAST(head) = FH_TAILQ_PREV(elem);		\
	*(FH_TAILQ_PREV(elem)) = FH_TAILQ_NEXT(elem);			\
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
	     (var) = TAILQ_NEXT(var))
#define FH_STAILQ_FOREACH(head, var)					\
	for ((var) = FH_STAILQ_FIRST(head); (var) != NULL;		\
	     (var) = STAILQ_NEXT(var))
		
/* ##################################################################### */
/* Firehose internal pinning functions                                   */
/* ##################################################################### */
/* See documentation in firehose_page.c                                  */
firehose_private_t *	fh_acquire_local_region(firehose_region_t *);
void			fh_release_local_region(firehose_request_t *);

firehose_request_t *	fh_acquire_remote_region(gasnet_node_t node, 
				firehose_region_t *reg, 
				firehose_completed_fn_t callback, 
				void *context, uint32_t flags,
		        	firehose_remotecallback_args_t *remote_args,
				firehose_request_t *ureq);
void			fh_release_remote_region(firehose_request_t *);
void			fh_commit_try_remote_region(gasnet_node_t node, 
						uintptr_t addr, size_t len);
void			fh_send_firehose_reply(fh_remote_callback_t *);

/* values for firehose_private_t * */
#define FH_REGION_UNPINNED	((firehose_private_t *) -1)

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

#ifdef TRACE
#define FH_TRACE_BUCKET(bd, bmsg) 					\
	do {								\
		char	msg[64];					\
		if (FH_IS_PREPINNED(bd))				\
			sprintf(msg, "PREPINNED");			\
		else if (FH_IS_FIFO(bd))				\
			sprintf(msg, "IN FIFO (lrefc=0, rrefc=0)");	\
		else							\
			sprintf(msg, "lrefc=%d, rrefc=%d",		\
			    FH_LREFC(FH_REFCOUNT(bd)),			\
			    FH_RREFC(FH_REFCOUNT(bd)));			\
		GASNETI_TRACE_PRINTF(C,					\
		    ("Firehose Bucket %s %s node=%d,addr=%p,%s",	\
		     #bmsg, FH_NODE(bd) == gasnet_mynode() ? 		\
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

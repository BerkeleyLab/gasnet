/* $Id: gasnet_core_firehose.c,v 1.23 2003/06/09 06:02:38 csbell Exp $
 * $Date: 2003/06/09 06:02:38 $
 * Description: GASNet GM conduit Firehose DMA Registration Algorithm
 * Copyright 2002, Christian Bell <csbell@cs.berkeley.edu>
 * Terms of use are as specified in license.txt
 */
/* ------------------------------------------------------------------------ */
/* Firehose                                                                 */
/* ------------------------------------------------------------------------ */
/*
The idea behind firehose resides in reserving a portion of the amount of
pinnable pages to every node on the cluster, allowing each node to manage its
number of pinnable pages in any way it wants.

Firehose works using the following parameters:
M: amount of local memory we try to guarentee as pinnable
   - n.b. Means to "guarentee" the memory is still unclear. . either we pin it
     all or we trust the system until we are out of physical memory (the
     second approach is probably sufficient if M is a smaller fraction of the
     amount of physical memory and/or system load).
B: bucket size, a multiple of the page size, defines a region of memory that
   can be used for DMAs if pinned
N: the amount of nodes in the job
F: a firehose is a local handle to a remote _pinned_ bucket
   Each node posesses F = M/(B*(N-1)) firehoses that it can use on N-1 nodes

For example, a 16 node job (N) that uses 4 4k pages for bucket size (B) over
512 MB of pinnable memory (M) on a 32-bit architecture posesses
    F = 512M/( 4*4k*15 ) = 2184 firehoses per node

F is significant both for local buckets and firehoses (remotely "owned" buckets)
- locally, the node will have to manage up to 2184*15 = 32760 buckets which
  could be mapped to firehoses all over the job.
- remotely, for firehoses the node owns to other buckets, the node will need
  to store metadata for up to 2184 firehoses on 15 nodes.

The amount of metadata put aside for the algorithm scales with the value of M,
which in turn should scale with the amount of physical memory on the machine.
As such, the firehose algorithm is scalable.
*/

/* ------------------------------------------------------------------------ */
/* Buckets and Bucket Descriptors
Each bucket needs a descriptor and two types of descriptors must be created for
- Local Buckets, where M/B bucket descriptors are initialized at startup to
  support all the buckets that could be pinned in the local node's memory
  space.
- Firehoses (Remote Buckets), where M/(B*(N-1)) descriptors are initialized
  for (N-1) nodes at startup to support the maximum number of firehoses (M/B)
  that may be mapped to remote firehoses.
*/

/* ------------------------------------------------------------------------ */
/* Bucket Descriptor reference count
Each bucket descriptor has a reference count, which is used differently for
local buckets and firehoses:
- Local Buckets keep a reference count to know how many remote nodes expect
  the bucket to be pinned.  In other words, how many other nodes own firehoses
  to the local bucket.  At initialization, the reference count for each bucket
  descriptor is set to REFCOUNT_INIT, meaning the bucket is not pinned.  Once
  a first node requests the bucket to be pinned, its reference count is bumped
  up to 1.  Other nodes are free to move one of their firehoses to/from this
  bucket as the operation on refcount will be respectively
  increment/decrement.  Once the refcount reaches zero, we are free to unpin
  the page or manage it separately - see below for more details.
- Firehoses keep a refcount to prevent a race condition.  When the local node
  needs RDMA access on a remote node, it sends a list of old and new bucket
  addresses.  To the node receiving the firehose move request, it is simply a
  matter of decrementing/incrementing the refcount on old/new buckets.
  However, to prevent other threads/processes on the local node from moving a
  firehose another thread expects to use, refcount is used for the concerned
  bucket descriptors to indicate that a move_firehose/dma operation is in
  progress.  In other words, the refcount for firehoses simply locks the
  bucket from being unpinned by other threads/processes running on the same
  node.
*/

/* ------------------------------------------------------------------------ */
/*
We need two separate types of lookup tables:
- Local Bucket Table, we must inquire about every bucket in the segment.
  Lookup is based solely on <bucket_address>
- Firehose Table, we must inquire about firehoses on each node.  Lookup is
  based on <node:bucket_address> where bucket_address is one of the F
  firehoses the local node posesses to each other node. 
*/

/* ------------------------------------------------------------------------ */
/*
Implementing the lookup tables
 - Local Bucket Table
   The Local Bucket Table requires a total M/B bucket descriptors to be
   created and if each of these descripors use the bucket address as the key
   for the lookup.  Since the number of possible key lookups is equal to the
   amount of descriptors actually stored in memory, the Local Bucket Table may
   simply be an array.
 - Firehose Table
   The Firehose Table is actually composed of 'N' smaller firehose tables,
   each having M/B possible key lookups.  This means we have M*N/B possible
   lookups for only M/B descriptors, which would require a constant overhead
   of 'N' in order to implement an array-based lookup.  For obvious reasons,
   some sort of hash table will have to be used in order to provide a
   fixed-time <node:bucket_address> lookup.
*/
 
/* ------------------------------------------------------------------------ */
/*
Unreferenced Local Buckets
Once the refcount for local buckets reaches zero, we have two choices:
 1. Leave the bucket pinned
 2. Unpin it

 The first approach saves some pinning cost and possibly offers better
 performance in environments where physical memory is not an issue by leaving
 parts of the segment pinned.  Conversely, it also helps "guarentee" the M
 parameter by leaving a page of physical memory pinned for use of the current
 application.  The second approach is more simpler than the first in that it
 doesn't require any additional overhead in order to manage unreferenced
 buckets.

 The projected implementation will use a combination of both approaches.  It
 will leave some buckets lying around up to a specified threshold, where it
 will start unpinning buckets in order to prevent starving the system in
 physical memory.  The ideal value of this threshold remains to be determined.
*/

/* ------------------------------------------------------------------------ */
/*
Managing Unreferenced Local Buckets
In order to make recycling of unreferenced local buckets possible, they are
managed explicitly in a Local Bucket Victim FIFO.  This FIFO is simply an
LRU-based list which keeps track of a bounded amount of unreferenced local
buckets.  A bucket from this list may be unpinned at any time in order to
satisfy a request to pin another bucket in the segment.
*/

/* ------------------------------------------------------------------------ */
/*
Local Bucket descriptor representation
- address	the actual address of the pinned page(s)
- refcount 	the reference count for the current bucket. 
- next/prev	pointers/indexes used in Bucket Victim FIFO

 * 32-bit platforms (4k pages)
   bucket_desc_max = M_max/B_min = 4GB/4K = 1MB
   So we have at most 1MB bucket descriptors to create.  If we use an
   array-based table lookup, we don't need to store the address in the
   descriptor, may use indexes for next/prev and need at most 10 bits for
   refcount (max nodes on cluster = 1024).  Thus, a 64-bit type would be
   sufficient:
   | prev (20 bits) | next (20 bits) | refcount (24 bits) |

 * 64-bit platforms (8k pages)
   bucket_desc_max = M_max/B_min = 64GB/8k = 8MB (here 64 is taken since we
   cannot fathom more than 64GB on 64-bit system running Myrinet yet).
   So we have at most 8MB bucket descriptors to create.  Again, using an
   array-based table lookup, we may use the above approach to use a 64-bit
   type to store the descriptor:
   | prev (23 bits) | next (23 bits) | refcount (18 bits) |
   A worse case metadata overhead in this case yields 64MB, which is improved
   when we start clustering the B parameter.  Hence, the larger M, the more we
   can afford to have a larger B while maintaining the same amount of pinning
   overhead.

 * In both cases, the amount of metadata scales with M which in turn is
   loosely based on the amount of physical memory - an array-based table
   lookup-up
*/

/* ------------------------------------------------------------------------ */
/*
Firehose (Remote Bucket) descriptor representation
- address	the actual address of the pinned page(s)
- refcount 	the reference count (bucket "locked")
- next		LRU FIFO for 0-refcounts

  * 32-bit platforms (4k pages)
    F_max = M_max/(B_min*(N_min-1)) = 4GB/(4K*(2-1)) = 1MB
    A node may own a maximum of 1MB firehoses for all nodes, although we
    support up to 1024 nodes, which requires the look-up table to support up
    to 1024*1MB possibly lookups.  Array-based table lookup is impossible in
    this case.  We need some form of hashing.
  * 64-bit platforms (8k pages)
    F_max = M_max/(B_min(N_min-1)) = 64GB/(8K*(2-1)) = 8MB
    idem, would need 8GB for array-based lookup.  Also need some form of
    hashing.
*/
/* ------------------------------------------------------------------------ */
/*
Moving the Firehose on remote nodes
When the local node moves a firehose on a remote node, it expects the firehose
to stay put during the entire duration of the DMA operation.  Once the DMA
completes, the firehose is free to be moved.  The only way for firehoses to be
mishandled or for non-blocking put/get operations and for threaded clients:
Both are able to read/modify the firehose before the DMA application
associated with the firehose is complete.  In order to 'lock' the firehoses, a
reference count is kept with each firehose descriptor which references the
amount of outstanding operations which use the firehose on a fixed remote
bucket.

In implementation, it may be required to bound the number of outstanding
operations to the maximum storable reference count in order to guarentee
correctness and prevent overflow.

Finding a free firehose requires keeping track of each usued/unused firehose.
This is done using a FIFO list of 0-refcount firehose descriptors which allows
using the least recently used firehose when looking for a firehose to move.

A different approach might be to ignore recently used firehoses and walk
through the hash table looking for 0-refcount firehoses.

In both cases, it may be required to poll if no firehose has a 0-refcount.
This is to allow outstanding DMA operations to complete and decrement their
refcounts in the hopes that a 0-refcount may be obtained as soon as possible.
*/
/* ------------------------------------------------------------------------ */
/*
Pinning Local buckets
Requests to pin memory must be fulfilled for
 - bucket pins/unpins inside reserved segment
   Usual puts/gets from/to segment locations, which use the prescribed
   firehose algorithm.
   This approach explicitly lists which buckets to pin/unpin by bucket
   address.
 - out-of segment bucket pins
   These accomodate putting from an out-of-segment region into a remote
   segment region or getting into an out-of-segment region from a remote
   segment region.  However, these are always used by the local node wanting
   to fulfill an RDMA operation.  The only restriction on these bucket pins is
   that they may only be used during the elapsed time for the DMA operation to
   complete.  While buckets may not be stolen while the operation completes,
   they are immediately set to a reference count of 0 and enqueued in the
   Victim FIFO queue as soon as the operation completes.
   This approach implicitly lists the buckets to pin by providing a src/length
   tuple.
   XXX Currently, the approach is to pin everything, even "small" puts which
       are out of the segment will lead to a bucket being pinned out of the
       segment

Requests for pinning memory come in one of the two following forms:
 A. By address and length
    Caller needs a certain portion in memory to be pinned.  Useful for
    out-of-segment local pins. 
 B. Ordered bucket list
    Caller provides a sorted bucket list to pin/unpin.  Hopefully, the sort
    allows us to find contiguous buckets in order to minimize registration
    overhead.

In both cases, the caller doesn't really care if the pages covered by the
requested region are pinned or not, he simply wants them pinned after the
call.  In the first case, we know that the first region contains contiguous
buckets while the second forces us to find them.
*/

/* ------------------------------------------------------------------------ */
/*
Futher Research
It might be interesting, once firehose is up and running, to profile the
algorithm in order to see how it performs with respect to the F parameter.
Since each node posesses F firehoses to each other node, this constrains the
amount of firehoses a specific node may use to another node.  It might be
interesting to look at credit or token-based approaches where it is possible
for a node to posess more firehoses to another node.  This would probably use
an approach where unused firehoses are given for other nodes to use.  However,
this would lead to having dynamic sized hash tables, which presents another
problem difficult to overcome with the current firehose approach.  It may be
possible to overcome this pitfall by using a single large hash table that
hashes using two keys <node:bucket_address> instead of 'N' smaller hash tables
only using the <bucket_address> key.  In any case, we know that a single node
need only to create a hash table the size of F_max, which is the maximum
number of firehoses the node may own over all nodes combined.
*/
/* ------------------------------------------------------------------------ */
#include <gasnet.h>
#ifdef GASNETC_FIREHOSE
#include <gasnet_core_internal.h>
#include <gasnet_internal.h>
#include <gasnet_handler.h>
/*
 * The code below uses the following namespaces
 *
 * GASNETC_BUCKET_*, GASNETC_FIREHOSE_* respectively refer to local bucket and
 * remote bucket (firehose) macros.
 *
 * gasnetc_bucket_*, gasnetc_firehose_* refer to functions and variables
 * (although gasnetc_firehose_* variables are abbreviated to gasnetc_fh_)
 */

gasnet_handlerentry_t const	*gasnetc_get_handlertable();

/* Firehose MACROs */
#ifdef GASNETI_PTR32
#define GASNETC_BUCKET_SEGMENT		(1<<(32-GASNETC_BUCKET_SHIFT))
#else
#error Cannot use 64 bit segments yet
#endif
#define GASNETC_SEGMENT_MOD_SIZE	GASNETC_BUCKET_SIZE

typedef
struct gasnetc_bucket_desc {
	/* packed are refcount and prev pointer when used in victim fifo */
	uint32_t	refc_prev;
	uint32_t	next;
} 
gasnetc_bucket_desc_t;

static gasnetc_bucket_desc_t	*gasnetc_bucket_table;
static size_t			 gasnetc_bucket_victim_count;
static size_t			 gasnetc_bucket_victim_max;
static gasnetc_bucket_desc_t	*gasnetc_bucket_victim_head_ptr;
static gasnetc_bucket_desc_t	*gasnetc_bucket_victim_tail_ptr;
static uintptr_t		 gasnetc_stackaddr_lo;
static uintptr_t		 gasnetc_stackaddr_hi;

static int			gasnetc_bucket_initialized = 0;
static int			gasnetc_firehose_initialized = 0;

uintptr_t	 		gasnetc_firehose_MaxVictim = 0;

gasneti_mutex_t	gasnetc_lock_bucket        = GASNETI_MUTEX_INITIALIZER;
gasneti_mutex_t	gasnetc_lock_bucket_victim = GASNETI_MUTEX_INITIALIZER;

/* Functions exported to gasnet core */
extern void	gasnete_firehose_move_done(void *);
extern void	gasnetc_rdma_init(uintptr_t segbase, uintptr_t segsize, 
				  uintptr_t global_physmem);
extern void	gasnetc_rdma_finalize();
extern int	gasnetc_is_pinned(gasnet_node_t, uintptr_t, size_t);
extern void	gasnetc_done_pinned(gasnet_node_t, uintptr_t, size_t);

/* Functions export to extended firehose implementation */
extern void	gasnetc_bucket_pin_by_addr(uintptr_t, size_t);
extern void	gasnetc_bucket_unpin_by_addr(uintptr_t, size_t);

/* Internal bucket functions */
static void	gasnetc_bucket_init(uintptr_t, uintptr_t);
static void	gasnetc_bucket_finalize();
static void	gasnetc_bucket_pin_stack();
static void	gasnetc_bucket_victim_free(size_t);
static void	gasnetc_bucket_pin_register_wrapper(uintptr_t, size_t);
static int	gasnetc_bucket_trypin_by_bucket(uintptr_t, size_t);
static void	gasnetc_bucket_pin_by_list(uintptr_t *, size_t);
static void	gasnetc_bucket_unpin_deregister_wrapper(uintptr_t, size_t);

/* Tryunpin can either be called from gasnetc_done_pinned (GM callback) or from
 * an AM handler.  In the first case, the GM lock is implicitly held. */
static void	gasnetc_bucket_tryunpin_by_bucket_inner(uintptr_t, size_t, int);
#define		gasnetc_bucket_tryunpin_by_bucket_gm_locked(ptr,siz)     \
			gasnetc_bucket_tryunpin_by_bucket_inner(ptr,siz,1)
#define		gasnetc_bucket_tryunpin_by_bucket(ptr,siz)	         \
			gasnetc_bucket_tryunpin_by_bucket_inner(ptr,siz,0)

static void	gasnetc_bucket_unpin_by_list(uintptr_t *, size_t);

/* function to enfore lock hierarchy */
#define GASNETC_LOCK_GM do {						    \
		gasneti_mutex_assertunlocked(&gasnetc_lock_bucket);         \
		gasneti_mutex_assertunlocked(&gasnetc_lock_bucket_victim);  \
		gasneti_mutex_lock(&gasnetc_lock_gm); } while (0)
#define GASNETC_UNLOCK_GM do {						    \
		gasneti_mutex_assertunlocked(&gasnetc_lock_bucket);         \
		gasneti_mutex_assertunlocked(&gasnetc_lock_bucket_victim);  \
		gasneti_mutex_unlock(&gasnetc_lock_gm); } while (0)

#define GASNETC_LOCK_BUCKET do {					    \
		gasneti_mutex_assertunlocked(&gasnetc_lock_bucket_victim);  \
		gasneti_mutex_lock(&gasnetc_lock_bucket); } while (0)
#define GASNETC_UNLOCK_BUCKET do {					    \
		gasneti_mutex_assertunlocked(&gasnetc_lock_bucket_victim);  \
		gasneti_mutex_unlock(&gasnetc_lock_bucket); } while (0)

#define GASNETC_LOCK_BUCKET_VICTIM do {					    \
		gasneti_mutex_assertlocked(&gasnetc_lock_bucket);	    \
		gasneti_mutex_lock(&gasnetc_lock_bucket_victim); } while (0)
#define GASNETC_UNLOCK_BUCKET_VICTIM do {				    \
		gasneti_mutex_assertlocked(&gasnetc_lock_bucket);	    \
		gasneti_mutex_unlock(&gasnetc_lock_bucket_victim); } while (0)

/* We only need to support 1023 reference counts from remote firehoses and at
 * most ~280 local reference counts from local operations using GM tokens.
 * 11 bits are sufficient */
#define GASNETC_BDESC_REFC_OFF		(11)
#define GASNETC_BDESC_REFC_MASK		(0x7ff)
#define GASNETC_BDESC_REFC_UNPINNED	GASNETC_BDESC_REFC_MASK
#define GASNETC_BDESC_PREV_MASK		(~GASNETC_BDESC_REFC_MASK)

/* Bucket descriptor macros */
#define GASNETC_BDESC_INDEX_FROM_ADDR(bdptr)                                  \
				((uintptr_t)(bdptr)>>GASNETC_BUCKET_SHIFT)
#define GASNETC_BDESC_FROM_ADDR(addr)                                         \
			     (&gasnetc_bucket_table[                          \
			       GASNETC_BDESC_INDEX_FROM_ADDR(addr)])
#define GASNETC_BDESC_INDEX(bdptr)  ((bdptr)-gasnetc_bucket_table)
#define GASNETC_BDESC_TO_ADDR(bptr) ((uintptr_t)(((bptr)-gasnetc_bucket_table)\
					<<GASNETC_BUCKET_SHIFT))

/* Reference count macros */
#define GASNETC_BDESC_REFC(bdptr) 	((bdptr)->refc_prev &                  \
						GASNETC_BDESC_REFC_MASK)
#define GASNETC_BDESC_REFC_INC(bdptr)	(assert(GASNETC_BDESC_REFC(bdptr) <    \
						GASNETC_BDESC_REFC_MASK),      \
						(bdptr)->refc_prev++)
#define GASNETC_BDESC_REFC_DEC(bdptr)	(assert(GASNETC_BDESC_REFC(bdptr)!= 0),\
						((bdptr)->refc_prev--))
#define GASNETC_BDESC_REFC_SET(bdptr,v)	(bdptr)->refc_prev =                  \
					    (GASNETC_BDESC_PREV_MASK &        \
				    	        (bdptr)->refc_prev) |	      \
					    (v & GASNETC_BDESC_REFC_MASK)
#define GASNETC_BDESC_REFC_ZERO(bdptr)	((bdptr)->refc_prev &=                 \
					    GASNETC_BDESC_PREV_MASK)
#define GASNETC_BDESC_REFC_ISZERO(bdptr)	(GASNETC_BDESC_REFC(bdptr) == 0)

/* Pointer macros */
#define GASNETC_BDESC_NEXT(bdptr)	((bdptr)->next)
#define GASNETC_BDESC_NEXT_SET(bdptr,n)	((bdptr)->next = (n))
#define GASNETC_BDESC_NEXT_ZERO(bdptr)	((bdptr)->next = 0)
#define GASNETC_BDESC_PREV(bdptr)	((((bdptr)->refc_prev &                \
						GASNETC_BDESC_PREV_MASK)) >>   \
						GASNETC_BDESC_REFC_OFF)
#define GASNETC_BDESC_PREV_SET(bdptr,p)	((bdptr)->refc_prev =                  \
						GASNETC_BDESC_REFC(bdptr) |    \
						((p) << GASNETC_BDESC_REFC_OFF))
#define GASNETC_BDESC_PREV_ZERO(bdptr)	((bdptr)->refc_prev =                  \
						GASNETC_BDESC_REFC(bdptr))

#define GASNETC_BDESC_ISPINNED(bdptr)	(GASNETC_BDESC_REFC(bdptr) !=          \
						GASNETC_BDESC_REFC_UNPINNED)

#define GASNETC_BDESC_ADDR_CONTIGUOUS(bdptr1,bdptr2)                           \
	((uintptr_t)(bdptr2)-(uintptr_t)(bdptr1)==GASNETC_BUCKET_SIZE)
#define GASNETC_BDESC_CONTIGUOUS(bdptr1,bdptr2)           ((bdptr2)-(bdptr1)==1)


/* ------------------------------------------------------------------------ */
/* Local Bucket initialization */
static void
gasnetc_bucket_init(uintptr_t segsize, uintptr_t global_physmem)
{
	size_t			 num_buckets;
	gasnetc_bucket_desc_t	*table;
	uintptr_t		maxvictim;
	unsigned int		i;

	num_buckets = GASNETC_BUCKET_SEGMENT;
	table = (gasnetc_bucket_desc_t *)
		gasneti_malloc(num_buckets*sizeof(gasnetc_bucket_desc_t));
	GASNETI_TRACE_PRINTF(C, ("Firehose local buckets=%d (table=%d bytes)",
	    num_buckets, num_buckets*sizeof(gasnetc_bucket_desc_t)));

	for (i = 0; i < GASNETC_BUCKET_SEGMENT; i++)
		table[i].refc_prev = GASNETC_BDESC_REFC_MASK;

	/* setup the initial bucket victim fifo queue, where the head's next
	 * pointer is the tail and the tail's previous pointer is the head
	 * We assume we can use the last two buckets in virtual memory
	 */
	gasnetc_bucket_victim_head_ptr = &table[GASNETC_BUCKET_SEGMENT-2];
	gasnetc_bucket_victim_tail_ptr = &table[GASNETC_BUCKET_SEGMENT-1];

	GASNETC_BDESC_NEXT_SET(gasnetc_bucket_victim_head_ptr, 
	    GASNETC_BUCKET_SEGMENT-1);
	GASNETC_BDESC_PREV_SET(gasnetc_bucket_victim_tail_ptr, 
	    GASNETC_BUCKET_SEGMENT-2);
	gasnetc_bucket_table = table;

	assert(GASNETC_FIREHOSE_MAXVICTIM_RATIO > 0 && 
	       GASNETC_FIREHOSE_MAXVICTIM_RATIO <= 1);

	/* Get the maxvictim parameters from the environment */
	maxvictim = GASNETI_ALIGNDOWN(
	    (uintptr_t) gasnetc_getenv_numeric("GASNETGM_FIREHOSE_MAXVICTIM"),
	    GASNETC_BUCKET_SIZE);

	if (maxvictim > 0) 
		gasnetc_firehose_MaxVictim = maxvictim;
	else
		gasnetc_firehose_MaxVictim = (uintptr_t)
		    GASNETI_ALIGNDOWN(global_physmem *
	    	        GASNETC_FIREHOSE_MAXVICTIM_RATIO, GASNETC_BUCKET_SIZE);

	gasnetc_bucket_victim_max = 
	    gasnetc_firehose_MaxVictim >> GASNETC_BUCKET_SHIFT;
	gasnetc_bucket_victim_count = 0;

	GASNETI_TRACE_PRINTF(C, 
	    ("Firehose GASNETGM_FIREHOSE_MAXVICTIM=%u bytes, ratio=%.2f, "
	    "maxvictim=%.2f Mb",
	    (unsigned int) maxvictim,
	    GASNETC_FIREHOSE_MAXVICTIM_RATIO,
	    ((unsigned) gasnetc_firehose_MaxVictim) / (1<<20)));

	GASNETI_TRACE_PRINTF(C, 
	    ("Firehose local victims max=%d (head=%d,tail=%d)",
	    gasnetc_bucket_victim_max,
	    GASNETC_BDESC_NEXT(gasnetc_bucket_victim_head_ptr),
	    GASNETC_BDESC_PREV(gasnetc_bucket_victim_tail_ptr)));

	gasnetc_bucket_initialized = 1;
	return;
}

static void
gasnetc_bucket_finalize()
{
	if (!gasnetc_bucket_initialized || !gasnetc_firehose_initialized)
		gasneti_fatalerror("gasnetc_bucket_finalize(): Firehose "
		 "algorithm not initialized");

	gasneti_free(gasnetc_bucket_table);
	gasnetc_bucket_initialized = 0;
}

/* Pin the stack (we assume the stack grows down)
 *
 * We pin the page &stack_addr is in and GASNETC_PINNED_STACK_PAGES below it.
 */
static void
gasnetc_bucket_pin_stack()
{
	char		stack_addr;
	char		stack_addr2;
	uintptr_t	stack_top, stack_bottom, va_top;

	/* stack grows down?
	assert(&stack_addr2 < &stack_addr);
	*/
	stack_top = 
	    GASNETI_PAGE_ALIGNUP((uintptr_t)&stack_addr);
	va_top = GASNETI_PAGE_ALIGNDOWN((uintptr_t)-1);

	GASNETI_TRACE_PRINTF(C, 
	    ("Firehose stack addresses: va_top=%p, stack_top=%p", 
	    va_top, stack_top));

	gasnetc_stackaddr_hi = MIN(va_top, stack_top);

	stack_bottom = (uintptr_t)&stack_addr - 
	    (GASNETC_PINNED_STACK_PAGES*GASNET_PAGESIZE);
	gasnetc_stackaddr_lo = 
	    GASNETI_PAGE_ALIGNDOWN(stack_bottom);

	GASNETI_TRACE_PRINTF(C, ("Firehose register stack: %d pages (%p-%p)",
	    ((unsigned) gasnetc_stackaddr_hi-gasnetc_stackaddr_lo)/
	    GASNET_PAGESIZE,
	    (void *) gasnetc_stackaddr_lo, (void *) gasnetc_stackaddr_hi));

	GASNETC_LOCK_GM;

	if (gm_register_memory(_gmc.port, (void *)gasnetc_stackaddr_lo, 
	    (unsigned) gasnetc_stackaddr_hi-gasnetc_stackaddr_lo) 
	    != GM_SUCCESS) {
		fprintf(stderr, "could not register stack memory");
		gasnet_exit(-1);
	}

	GASNETC_UNLOCK_GM;

	return;
}

/* A helper function to remove a bucket from the fifo, and set the refcount */
GASNET_INLINE_MODIFIER(gasnetc_bucket_fifo_remove)
void
gasnetc_bucket_fifo_remove(gasnetc_bucket_desc_t *bdesc, int refc)
{
	gasnetc_bucket_desc_t	*bdesc_prev, *bdesc_next;

	GASNETC_LOCK_BUCKET_VICTIM;
	assert(GASNETC_BDESC_REFC_ISZERO(bdesc));

	bdesc_prev = 
	    &gasnetc_bucket_table[GASNETC_BDESC_PREV(bdesc)];
	bdesc_next = &gasnetc_bucket_table[GASNETC_BDESC_NEXT(bdesc)];

	GASNETC_BDESC_NEXT_SET(bdesc_prev, GASNETC_BDESC_NEXT(bdesc));
	GASNETC_BDESC_PREV_SET(bdesc_next, GASNETC_BDESC_PREV(bdesc));

	GASNETI_TRACE_PRINTF(C, 
	    ("Firehose remove %p prev=%d,next=%d (head=%d,tail=%d)", 
	     GASNETC_BDESC_TO_ADDR(bdesc), GASNETC_BDESC_PREV(bdesc), 
	     GASNETC_BDESC_NEXT(bdesc), 
	     GASNETC_BDESC_NEXT(gasnetc_bucket_victim_head_ptr), 
	     GASNETC_BDESC_PREV(gasnetc_bucket_victim_tail_ptr)));

	GASNETC_BDESC_NEXT_ZERO(bdesc);
	GASNETC_BDESC_PREV_ZERO(bdesc);
	gasnetc_bucket_victim_count--;

	GASNETC_BDESC_REFC_SET(bdesc, refc);
	GASNETC_UNLOCK_BUCKET_VICTIM;
	return;
}

GASNET_INLINE_MODIFIER(gasnetc_bucket_fifo_add)
void
gasnetc_bucket_fifo_add(gasnetc_bucket_desc_t *bdesc, int refc)
{
	gasnetc_bucket_desc_t *bdesc_next;

	GASNETC_LOCK_BUCKET_VICTIM;

	if (GASNETC_BDESC_NEXT(bdesc) != 0) 
		gasneti_fatalerror("fifo_add"); 
	assert(GASNETC_BDESC_NEXT(bdesc) == 0);
	assert(GASNETC_BDESC_PREV(bdesc) == 0);

	GASNETI_TRACE_PRINTF(C, 
	    ("Firehose local bucket added victim (%p, %d) "
	     "(head=%d,tail=%d),cur(prev=%d,next=%d),count=%d",
	    GASNETC_BDESC_TO_ADDR(bdesc), 
	    GASNETC_BDESC_INDEX(bdesc),
	    GASNETC_BDESC_NEXT(gasnetc_bucket_victim_head_ptr),
	    GASNETC_BDESC_PREV(gasnetc_bucket_victim_tail_ptr),
	    GASNETC_BDESC_PREV(bdesc),
	    GASNETC_BDESC_NEXT(bdesc),
	    gasnetc_bucket_victim_count));

	bdesc_next = &gasnetc_bucket_table[
	    GASNETC_BDESC_NEXT(gasnetc_bucket_victim_head_ptr)];

	/* Set next to head's next, and previous to head */
	GASNETC_BDESC_NEXT_SET(bdesc, 
	    GASNETC_BDESC_NEXT(gasnetc_bucket_victim_head_ptr));
	GASNETC_BDESC_PREV_SET(bdesc, 
	    GASNETC_BUCKET_SEGMENT-2);

	/* Set next's prev, and head to cur */
	GASNETC_BDESC_PREV_SET(bdesc_next, 
	    GASNETC_BDESC_INDEX(bdesc));
	GASNETC_BDESC_NEXT_SET(gasnetc_bucket_victim_head_ptr,
	    GASNETC_BDESC_INDEX(bdesc));

	/* Set the reference count */
	GASNETC_BDESC_REFC_SET(bdesc, refc);

	gasnetc_bucket_victim_count++;
	assert(gasnetc_bucket_victim_count <= gasnetc_bucket_victim_max);

	GASNETI_TRACE_PRINTF(C, 
	    ("Firehose local bucket added victim (%p, %d) "
	     "(head=%d,tail=%d),cur (prev=%d,next=%d),count=%d",
	    GASNETC_BDESC_TO_ADDR(bdesc), 
	    GASNETC_BDESC_INDEX(bdesc),
	    GASNETC_BDESC_NEXT(gasnetc_bucket_victim_head_ptr),
	    GASNETC_BDESC_PREV(gasnetc_bucket_victim_tail_ptr),
	    GASNETC_BDESC_PREV(bdesc),
	    GASNETC_BDESC_NEXT(bdesc),
	    gasnetc_bucket_victim_count));

	GASNETC_UNLOCK_BUCKET_VICTIM;
	return;
}

/* ------------------------------------------------------------------------ */
/*
 * A wrapper to free 'num_buckets' from the victim fifo queue
 */
void
gasnetc_bucket_victim_free(size_t num_buckets)
{
	gasnetc_bucket_desc_t	*bdesc_cur, *bdesc_prev, *bdesc_main;
	uintptr_t		bucket_addr;
	int			i;

	/* The locking policy here is to hold the GM lock the whole time.  It
	 * is probably not worth the locking overhead to interleave GM
	 * lock/unlocks with the bucket victim lock in order to maximize
	 * concurrency - time spent in deregistering in GM lock is much larger
	 * than the memory references in bucket_victim lock anyhow */

	gasneti_mutex_assertlocked(&gasnetc_lock_gm);
	GASNETC_LOCK_BUCKET_VICTIM;

	/* Start removing buckets from the tail, always trying to find
	 * contiguous buckets in order to minimize deregistration overhead */
	assert(gasnetc_bucket_victim_count >= num_buckets);
	bdesc_main = &gasnetc_bucket_table[
	    GASNETC_BDESC_PREV(gasnetc_bucket_victim_tail_ptr)];

	GASNETI_TRACE_PRINTF(C, 
	    ("Firehose bucket free at count=%d, num_buckets request=%d",
	    gasnetc_bucket_victim_count,
	    num_buckets));

	gasnetc_bucket_victim_count -= num_buckets;

	while (num_buckets > 0) {
		i = 1;
		bdesc_prev = 
		    &gasnetc_bucket_table[GASNETC_BDESC_PREV(bdesc_main)];

		GASNETC_BDESC_PREV_ZERO(bdesc_main);
		GASNETC_BDESC_NEXT_ZERO(bdesc_main);
		GASNETC_BDESC_REFC_SET(bdesc_main, 
		    GASNETC_BDESC_REFC_UNPINNED);

		GASNETI_TRACE_PRINTF(C, ("Firehose bucket free (%p reset) "
		    "cur=%d prev=%d diff=%d",
		    GASNETC_BDESC_TO_ADDR(bdesc_main),
		    GASNETC_BDESC_INDEX(bdesc_main),
		    GASNETC_BDESC_INDEX(bdesc_prev),
		    bdesc_prev-bdesc_main));

		num_buckets--;
		bdesc_cur = bdesc_main;

		while (num_buckets > 0 && 
		    GASNETC_BDESC_CONTIGUOUS(bdesc_prev, bdesc_cur)) {

			GASNETI_TRACE_PRINTF(C, ("Firehose bucket free "
			    "cur=%d, prev=%d", GASNETC_BDESC_INDEX(bdesc_cur),
			    GASNETC_BDESC_INDEX(bdesc_prev)));

			bdesc_main = bdesc_cur = bdesc_prev;
			bdesc_prev = 
			    &gasnetc_bucket_table[GASNETC_BDESC_PREV(bdesc_prev)];

			GASNETC_BDESC_PREV_ZERO(bdesc_cur);
			GASNETC_BDESC_NEXT_ZERO(bdesc_cur);
			GASNETC_BDESC_REFC_SET(bdesc_cur, 
			    GASNETC_BDESC_REFC_UNPINNED);

			num_buckets--; 
			i++;
		}

		GASNETI_TRACE_PRINTF(C, 
		    ("Firehose bucket free (%p,%d) - num_buckets=%d",
		    (void *)GASNETC_BDESC_TO_ADDR(bdesc_main), 
		    i<<GASNETC_BUCKET_SHIFT,
		    num_buckets));

		if_pf (GASNETC_BDESC_TO_ADDR(bdesc_main) == 0)
			gasneti_fatalerror(
			    "unpinning erroneous address, main=%p, cur=%p, prev=%p", 
			    (void *)GASNETC_BDESC_TO_ADDR(bdesc_main),
			    (void *)GASNETC_BDESC_TO_ADDR(bdesc_cur),
			    (void *)GASNETC_BDESC_TO_ADDR(bdesc_prev));
		gasnetc_bucket_unpin_deregister_wrapper(
		    GASNETC_BDESC_TO_ADDR(bdesc_main), i);
		bdesc_main = bdesc_prev;
	}
	/* set the new tail, which is _cur */
	GASNETI_TRACE_PRINTF(C, ("Firehose setting tail to %d",
	    GASNETC_BDESC_INDEX(bdesc_prev)));

	GASNETC_BDESC_PREV_SET(gasnetc_bucket_victim_tail_ptr,
	    GASNETC_BDESC_INDEX(bdesc_prev));
	GASNETC_BDESC_NEXT_SET(bdesc_prev, GASNETC_BUCKET_SEGMENT-1);

	GASNETC_UNLOCK_BUCKET_VICTIM;

	return;
}

/*
 * Wrapper to gm_register_memory
 *
 * We first free buckets if there are too many (over threshold)
 * There are two stabs and registering memory, and returning 0 should be
 * considered fatal by the caller.  The first one tries to register memory
 * (notwithstanding the threshhold) while the second tries again after
 * freeing more buckets if possible.
 */
void
gasnetc_bucket_pin_register_wrapper(uintptr_t bucket_addr, size_t num_buckets)
{
	gm_status_t	status;

	assert(bucket_addr > 0);
	assert(bucket_addr % GASNETC_BUCKET_SIZE == 0);

	GASNETI_TRACE_PRINTF(C, ("Firehose register memory (%p,%d)",
	    (void *) bucket_addr, num_buckets << GASNETC_BUCKET_SHIFT));

	GASNETC_LOCK_GM;

	GASNETC_LOCK_BUCKET;

	if (gasnetc_bucket_victim_count >= gasnetc_bucket_victim_max) {
		GASNETI_TRACE_PRINTF(C, 
		    ("Firehose exceeded bucket victims (%d >= %d)",
		     gasnetc_bucket_victim_count,
		     gasnetc_bucket_victim_max));

		gasnetc_bucket_victim_free(
		    MIN(num_buckets, gasnetc_bucket_victim_count));
	}

	GASNETC_UNLOCK_BUCKET;

	if (gm_register_memory(_gmc.port, (void *)bucket_addr, 
	    num_buckets << GASNETC_BUCKET_SHIFT) != GM_SUCCESS) {

		/* failed, let us try to unregister more, if possible */
		if (gasnetc_bucket_victim_count < num_buckets) 

			gasneti_fatalerror(
			    "gm_register_memory failed (%p, %d) sbrk(0)=%p\n", 
			    (void *)bucket_addr, 
			    num_buckets << GASNETC_BUCKET_SHIFT,
			    sbrk(0)); 
		else {
			GASNETC_LOCK_BUCKET;

			gasnetc_bucket_victim_free(num_buckets);

			GASNETC_UNLOCK_BUCKET;

			if (gm_register_memory(_gmc.port, (void *)bucket_addr, 
			    num_buckets << GASNETC_BUCKET_SHIFT) != GM_SUCCESS) 

				gasneti_fatalerror(
				    "gm_register_memory failed (%p, %d) sbrk(0)=%p\n", 
				    (void *)bucket_addr, 
				    num_buckets << GASNETC_BUCKET_SHIFT,
				    sbrk(0)); 
		}
	}

	GASNETC_UNLOCK_GM;
}	

/*
 * Wrapper to gm_unregister_memory
 *
 * No fancy stuff.  Registers 'num_buckets' starting at bucket_addr
 */
void
gasnetc_bucket_unpin_deregister_wrapper(uintptr_t bucket_addr, 
					size_t num_buckets)
{
	gm_status_t	status;
	size_t		stack_buckets;

	assert(bucket_addr % GASNETC_BUCKET_SIZE == 0);
	assert(bucket_addr > 0);
	assert(num_buckets > 0);

	gasneti_mutex_assertlocked(&gasnetc_lock_gm);

	if (bucket_addr + (num_buckets<<GASNETC_BUCKET_SHIFT) > 
	    gasnetc_stackaddr_lo) {
		/* unpin falls entirely into pinned stack segment */
		if (bucket_addr >= gasnetc_stackaddr_lo)
			return;
		else { /* unpin falls partly into pinned stack segment */
			stack_buckets = 
			    bucket_addr + (num_buckets<<GASNETC_BUCKET_SHIFT) -
			    gasnetc_stackaddr_lo;
			assert(stack_buckets < num_buckets);
			num_buckets -= stack_buckets;
		}
	}

	if (gm_deregister_memory(_gmc.port, (void *)bucket_addr, 
	    num_buckets << GASNETC_BUCKET_SHIFT) == GM_SUCCESS)

		return;
	else {
		size_t nbytes = num_buckets << GASNETC_BUCKET_SHIFT;

		if (bucket_addr < (uintptr_t)gasnetc_seginfo[gasnetc_mynode].addr
		    || (bucket_addr + nbytes) > 
		    ((uintptr_t)gasnetc_seginfo[gasnetc_mynode].addr + 
		    gasnetc_seginfo[gasnetc_mynode].size)) {

			fprintf(stderr, 
			    "Deregistration out of segment [%p..%p] "
			    "(pinnned stack %p..%p)\n",
			    (void *) gasnetc_seginfo[gasnetc_mynode].addr,
			    (void *) (
			    (uintptr_t)gasnetc_seginfo[gasnetc_mynode].addr +
			    gasnetc_seginfo[gasnetc_mynode].size),
			    (void *) gasnetc_stackaddr_lo,
			    (void *) gasnetc_stackaddr_hi);
		}

		gasneti_fatalerror("Could not deregister memory at %p (%d bytes)", 
		    (void *)bucket_addr, num_buckets << GASNETC_BUCKET_SHIFT);
	}
}

/*
 * This version of pin simply attempts to wrap calls to local_pin_by_bucket by
 * providing the largest 'num_buckets_contiguous' possible.  In this case, the
 * caller has a region of memory to be pinned but doesn't know if it is pinned
 * or not.  We must try to do all we can to pin the memory.
 *
 * Returned are the number of buckets that were _already_ pinned
 */
int
gasnetc_bucket_trypin_by_bucket(uintptr_t bucket_addr, size_t num_buckets)
{
	int		i = 0, j = 0, num_pinned = 0;
	gasnetc_bucket_desc_t	*bdesc, *bdesc_cur, *bdesc_prev, *bdesc_next;

	assert(bucket_addr % GASNETC_BUCKET_SIZE == 0);

	GASNETC_LOCK_BUCKET;

	GASNETI_TRACE_PRINTF(C, ("Firehose local bucket pin (%p,%d buckets,%d)",
	    (void *) bucket_addr, num_buckets, 
	    (num_buckets<<GASNETC_BUCKET_SHIFT)));

	bdesc = GASNETC_BDESC_FROM_ADDR(bucket_addr);

	while (i < num_buckets) {
		bdesc_cur = bdesc + i;

		if (GASNETC_BDESC_ISPINNED(bdesc_cur)) {
			num_pinned++;

			/* If zero, remove from Victim FIFO queue */
			if (GASNETC_BDESC_REFC_ISZERO(bdesc_cur)) {
				/* Do The equivalent to. .
				 * bdesc.prev.next = bdesc.next;
				 * bdesc.next.prev = bdesc.prev;
				*/

				gasnetc_bucket_fifo_remove(bdesc_cur, 1);

				GASNETI_TRACE_PRINTF(C, 
				    ("Firehose local bucket refcount=1 (%p)"
				     " - removed from victim FIFO (count=%d)",
				    (void *) GASNETC_BDESC_TO_ADDR(bdesc),
				    gasnetc_bucket_victim_count));
			}
			else {
				GASNETC_BDESC_REFC_INC(bdesc_cur);

				GASNETI_TRACE_PRINTF(C, 
				    ("Firehose local bucket refcount=%d (%p)",
				    GASNETC_BDESC_REFC(bdesc_cur),
				    (void *) GASNETC_BDESC_TO_ADDR(bdesc_cur)));
			}

			i++;
		}
		else {
			j = 0;
			while ((i+j < num_buckets) && 
			    !GASNETC_BDESC_ISPINNED(bdesc_cur+j)) {

				GASNETC_BDESC_REFC_SET(bdesc_cur+j, 1);

				GASNETI_TRACE_PRINTF(C, 
				    ("Firehose local bucket NOT pinned (%p)",
				    (void *)GASNETC_BDESC_TO_ADDR(bdesc_cur+j)));

				j++; 
			}

			assert(j > 0);

			/* Make sure we register without holding the bucket
			 * lock */
			GASNETC_UNLOCK_BUCKET;

			gasnetc_bucket_pin_register_wrapper(
			    GASNETC_BDESC_TO_ADDR(bdesc_cur), j);

			GASNETC_LOCK_BUCKET;

			i += j;
		}
	}

	GASNETC_UNLOCK_BUCKET;

	return num_pinned;
}

/*
 * Same as above, except there is a special handling to use the victim fifo
 * queue for refcounts
 */
void
gasnetc_bucket_tryunpin_by_bucket_inner(uintptr_t bucket_addr, 
		size_t num_buckets, int locked)
{
	unsigned int		i = 0, refc;
	gasnetc_bucket_desc_t	*bdesc, *bdesc_cur, *bdesc_next;

	assert(gasnetc_bucket_victim_max > 0);

	GASNETC_LOCK_BUCKET;

	bdesc = GASNETC_BDESC_FROM_ADDR(bucket_addr);

	for (i = 0; i < num_buckets; i++) {
		bdesc_cur = bdesc + i;

		gasneti_mutex_assertlocked(&gasnetc_lock_bucket);

		/* Three categories. .
		 * a) refcnt is > 1, simply decrement and skip dereg/fifo add
		 * b) refcnt is 1, decrement and try dereg/fifo add
		 * c) refcnt is 0, try dereg/fifo
		 */
		refc = GASNETC_BDESC_REFC(bdesc_cur);

		if (refc > 0) {
			GASNETC_BDESC_REFC_DEC(bdesc_cur);
			if (refc > 1) /* bucket still in use */
				continue;
		}

		/* Refcount is zero, see if we add it to the victim count or
		 * deregister the pages (very expensive on Myrinet) */
		GASNETI_TRACE_EVENT_VAL(C, BUCKET_VICTIM_COUNT, 
		    gasnetc_bucket_victim_count);

		if (gasnetc_bucket_victim_count < gasnetc_bucket_victim_max) {

			/* don't add again if refc == 0 */
			if (refc == 0)
				continue;

			assert(refc == 1);
			/* if not, add it to the bucket fifo, and set its
			 * reference count to 0 */
			gasnetc_bucket_fifo_add(bdesc_cur, 0);
		}
		else {
			unsigned int	num = num_buckets-i, j;
			uintptr_t	unpin_addr = 
			    GASNETC_BDESC_TO_ADDR(bdesc_cur);

			GASNETI_TRACE_EVENT_VAL(C, BUCKET_VICTIM_UNPINS, num);

			GASNETI_TRACE_PRINTF(C, 
			    ("Firehose local bucket (%p,refc=%d) - "
			    "must deregister remaining %d buckets "
			    "(victim FIFO count=%d,max=%d)", 
			    GASNETC_BDESC_TO_ADDR(bdesc_cur), 
			    GASNETC_BDESC_REFC(bdesc_cur), num,
			    gasnetc_bucket_victim_count,
			    gasnetc_bucket_victim_max));

			if (!locked) {
				/* Lock hierarchy is GM, bucket, bucket_victim */
				GASNETC_UNLOCK_BUCKET;
				GASNETC_LOCK_GM;
				GASNETC_LOCK_BUCKET;
			}

			/* if refc is 0, the bucket to be deregistered is in
			 * the fifo, make sure we remove it */
			if (refc == 0)
				gasnetc_bucket_fifo_remove(bdesc_cur, 0);

			for (j = 0; j < num; j++) {

				GASNETI_TRACE_PRINTF(C, 
				    ("Firehose dereg local bucket (%p)",
				    GASNETC_BDESC_TO_ADDR(bdesc_cur+j)));

				GASNETC_BDESC_REFC_SET(bdesc_cur+j, 
				    GASNETC_BDESC_REFC_UNPINNED);
			}

			if (!locked) {
				GASNETC_UNLOCK_BUCKET;
			}

			if_pf ((void *)unpin_addr == NULL)
				gasneti_fatalerror("unpinning erroneous address");

			gasnetc_bucket_unpin_deregister_wrapper(
			    unpin_addr, num);

			if (!locked)
				GASNETC_UNLOCK_GM;
			else
				GASNETC_UNLOCK_BUCKET;

			/* Simply return after deregistering pages */
			return;
		}
	}

	GASNETC_UNLOCK_BUCKET;

	return;
}

/*
 * From a source address and a length parameter, a certain number of
 * buckets are pinned and initialized to a reference count of 1
 *
 * Form 'A' of pinning memory
 *
 */
extern void
gasnetc_bucket_pin_by_addr(uintptr_t src, size_t nbytes) 
{
	uintptr_t	bucket_addr;
	size_t		num_buckets;
	unsigned int	num_pinned;

	bucket_addr = GASNETI_ALIGNDOWN(src, GASNETC_BUCKET_SIZE);
	num_buckets = GASNETC_NUM_BUCKETS(bucket_addr, src+nbytes);
	num_pinned = gasnetc_bucket_trypin_by_bucket(bucket_addr, num_buckets);

	GASNETI_TRACE_EVENT_VAL(C, BUCKET_LOCAL_PINS, num_pinned);
	GASNETI_TRACE_EVENT_VAL(C, BUCKET_LOCAL_TOUCHED, num_buckets);
}

/*
 * Unpinning/decrementing by refcount will probably only be used in GM
 * callbacks, since pinning by address is only done for local to remote
 * DMA operations
 */
extern void
gasnetc_bucket_unpin_by_addr(uintptr_t src, size_t nbytes)
{
	uintptr_t	bucket_addr;
	size_t		num_buckets;

	gasneti_mutex_assertunlocked(&gasnetc_lock_gm);
	bucket_addr = GASNETI_ALIGNDOWN(src, GASNETC_BUCKET_SIZE);
	num_buckets = GASNETC_NUM_BUCKETS(bucket_addr,src+nbytes);
	gasnetc_bucket_tryunpin_by_bucket(bucket_addr, num_buckets);
}

/*
 * This version of pin takes an array of sorted buckets (ascending) and
 * guarentees that the pages will be pinned after the call.
 */
void
gasnetc_bucket_pin_by_list(uintptr_t *bucket_list, 
			   size_t num_buckets_list)
{
	unsigned int	i,j, num_pinned = 0;
	gasneti_stattime_t      starttime = GASNETI_STATTIME_NOW_IFENABLED(C);

#ifdef TRACE
	for (i = 0; i < num_buckets_list; i++)
		GASNETI_TRACE_PRINTF(C, ("firehose move %d=%p",
		    i, (void *) bucket_list[i]));
#endif
	i = 0;
	while (i < num_buckets_list) {
		j = 1;
		while (i+j < num_buckets_list &&
		    GASNETC_BDESC_ADDR_CONTIGUOUS(
		    bucket_list[i+j-1],bucket_list[i+j]))
			j++;
		num_pinned += gasnetc_bucket_trypin_by_bucket(bucket_list[i], j);
		i += j;
	}
	GASNETI_TRACE_EVENT_TIME(C, FIREHOSE_PIN_TIME,
	    GASNETI_STATTIME_NOW_IFENABLED(C)-starttime);
	return;
}

/*
 * Idem, for unpinning buckets
 */
void
gasnetc_bucket_unpin_by_list(uintptr_t *bucket_list, 
			     size_t num_buckets_list)
{
	unsigned int	i,j;
	gasneti_mutex_assertunlocked(&gasnetc_lock_gm);

	i = 0;
	while (i < num_buckets_list) {
		j = 1;
		while (i+j < num_buckets_list &&
		    GASNETC_BDESC_ADDR_CONTIGUOUS(
		    GASNETC_BDESC_FROM_ADDR(bucket_list + i + j - 1),
		    GASNETC_BDESC_FROM_ADDR(bucket_list + i + j)))
			j++;
		gasnetc_bucket_tryunpin_by_bucket(bucket_list[i], j);
		i += j;
	}
	return;
}

/* ------------------------------------------------------------------------ */
/* Firehose (Remote Bucket) operations (pin/unpin) */
/*
 * F = M/((N-1)*B)
 * Fore the gm hash table api, keys are of the format:
 * (gasnetc_bucket_addr | node_id)
 */

/* Hash value, once looked up.
 * Its size is the value of two pointers.
 */
typedef
struct gasnetc_fh_data {
	/* bucket_refcount holds bucket address _and_ refcount
	 * assert refcount is < ... */
	uintptr_t		bucket_refcount;
	struct gasnetc_fh_data	*next;
}
gasnetc_fh_data_t;

typedef uintptr_t		gasnetc_fh_key_t;

static gasnetc_fh_data_t	*gasnetc_fh_victims;
static gasneti_atomic_t		*gasnetc_fh_victim_count;
static size_t			 gasnetc_fh_num;
gasneti_atomic_t		*gasnetc_fh_avail;

uintptr_t	*gasnetc_firehose_buf = NULL;
uintptr_t	 gasnetc_firehose_M = 0;
size_t		 gasnetc_firehose_num = 0;
size_t		 gasnetc_firehose_buf_num = 0;

gasneti_mutex_t	gasnetc_lock_fh_victim = GASNETI_MUTEX_INITIALIZER;

static void	gasnetc_firehose_init(uintptr_t, uintptr_t);
static void	gasnetc_firehose_finalize();
static void	gasnetc_firehose_table_init(size_t);
static void	gasnetc_firehose_table_finalize();

/* Two functions exported to the 'extended' firehose implementation */
extern int	gasnetc_firehose_build_list(uintptr_t *, gasnet_node_t, 
			uintptr_t, unsigned int, unsigned int *, unsigned int *);
extern void	gasnetc_firehose_decrement_refcount(gasnet_node_t, uintptr_t, 
						    size_t);

/* redefine lock functions to respect lock hierarchy */
#undef GASNETC_LOCK_GM
#undef GASNETC_UNLOCK_GM
#define GASNETC_LOCK_GM do {						    \
		gasneti_mutex_assertunlocked(&gasnetc_lock_fh_hash);        \
		gasneti_mutex_assertunlocked(&gasnetc_lock_fh_victim);	    \
		gasneti_mutex_lock(&gasnetc_lock_gm); } while (0)
#define GASNETC_UNLOCK_GM do {						    \
		gasneti_mutex_assertunlocked(&gasnetc_lock_fh_hash);        \
		gasneti_mutex_assertunlocked(&gasnetc_lock_fh_victim);	    \
		gasneti_mutex_unlock(&gasnetc_lock_gm); } while (0)

#define GASNETC_LOCK_FH_VICTIM do {					    \
		gasneti_mutex_lock(&gasnetc_lock_fh_victim); } while (0)
#define GASNETC_UNLOCK_FH_VICTIM do {					    \
		gasneti_mutex_unlock(&gasnetc_lock_fh_victim); } while (0)

#define GASNETC_FH_KEY_NODE_MASK	(GASNETC_BUCKET_SIZE-1)
#define GASNETC_FH_KEY_ADDR_MASK	~GASNETC_FH_KEY_NODE_MASK
#define GASNETC_FH_KEY_NODE(key)	((key) & GASNETC_FH_KEY_NODE_MASK)
#define GASNETC_FH_KEY_ADDR(key)	((key) & GASNETC_FH_KEY_ADDR_MASK)
#define GASNETC_FH_KEY(baddr,node) (assert((baddr&(GASNETC_BUCKET_SIZE-1))==0),\
					((baddr) | (node)))
#define GASNETC_FH_NEXT_END		((void *) -1)

#define GASNETC_FH_REFC_MASK		(GASNETC_BUCKET_SIZE-1)
#define GASNETC_FH_ADDR_MASK		~GASNETC_FH_REFC_MASK
#define GASNETC_FH_ADDR(fhptr) ((fhptr)->bucket_refcount & GASNETC_FH_ADDR_MASK)
#define GASNETC_FH_REFC(fhptr) ((fhptr)->bucket_refcount & GASNETC_FH_REFC_MASK)
#define GASNETC_FH_REFC_INC(fhptr)	(assert((fhptr)->bucket_refcount !=    \
						GASNETC_FH_REFC_MASK),         \
						(fhptr)->bucket_refcount++)
#define GASNETC_FH_REFC_DEC(fhptr)	(assert(GASNETC_FH_REFC(fhptr) != 0),  \
						(fhptr)->bucket_refcount--)
#define GASNETC_FH_REFC_ADDR_SET(fhptr, addr)				       \
					((fhptr)->bucket_refcount = (addr) |   \
					 GASNETC_FH_REFC(fhptr))
#define GASNETC_FH_REFC_SET(fhptr,d)	(assert(d <= GASNETC_FH_REFC_MASK),    \
						(fhptr)->bucket_refcount =     \
						(GASNETC_FH_ADDR(fhptr) | d))
#define GASNETC_FH_REFC_INIT(fhptr,addr,d)				       \
					(assert(d <= GASNETC_FH_REFC_MASK),    \
					 	(fhptr)->bucket_refcount =     \
						(addr) | (d))
#define GASNETC_FH_REFC_ZERO(fhptr)	((fhptr)->bucket_refcount &=           \
						GASNETC_FH_ADDR_MASK)
#define GASNETC_FH_REFC_ISZERO(fhptr)	(GASNETC_FH_REFC(fhptr) == 0)

/* ------------------------------------------------------------------------ */
/* Firehose initialization and finalization functions, abstracted by
 * gasnetc_rdma_ functions */

static void
gasnetc_firehose_init(uintptr_t segsize, uintptr_t global_physmem)
{
	size_t		firehoses;
	int		i;
	uintptr_t	fh_M;
	uintptr_t	pinnable = global_physmem;

	assert(gasnetc_bucket_initialized);

	/* Get the firehose M parameter if set in the * environment */
	fh_M = GASNETI_ALIGNDOWN(
	    (uintptr_t) gasnetc_getenv_numeric("GASNETGM_FIREHOSE_M"),
	    GASNETC_BUCKET_SIZE);

	/* Make sure FIREHOSE_M + FIREHOSE_MAXVICTIM don't exceed global
	 * pinnable memory */
	pinnable = 
	    GASNETI_ALIGNDOWN(
	        GASNETC_FIREHOSE_PHYSMEM_RATIO * global_physmem,
	        GASNETC_BUCKET_SIZE);

	if (fh_M + gasnetc_firehose_MaxVictim > pinnable) {
			gasneti_fatalerror(
			    "GASNETGM_FIREHOSE_M +"
			    "GASNETGM_FIREHOSE_MAXVICTIM (%d Mb) exceed "
			    "Maximum Global pinnable memory (%d Mb)\n",
			    (unsigned int) (fh_M + gasnetc_firehose_MaxVictim)
			        / (1<<20), (unsigned int) pinnable / (1<<20));
	}

	/* Calculate the maximum value for M parameter */
	assert(GASNETC_FIREHOSE_MAXVICTIM_RATIO > 0 && 
	       GASNETC_FIREHOSE_MAXVICTIM_RATIO <= 1);

	if (fh_M > 0)
		gasnetc_firehose_M = fh_M;
	else 
		gasnetc_firehose_M = (uintptr_t)
			GASNETI_ALIGNDOWN(pinnable *
		    	    (1-GASNETC_FIREHOSE_MAXVICTIM_RATIO), 
			    GASNETC_BUCKET_SIZE);

	/* Total firehoses: initialized from the page-aligned fh_M size */
	gasnetc_firehose_num = 
	    firehoses = (size_t) gasnetc_firehose_M >> GASNETC_BUCKET_SHIFT;

	gasnetc_firehose_table_init(firehoses);

	/* Mark down the number of firehoses owned to each node */
	gasnetc_fh_num = gasnetc_nodes == 1 ? (size_t) firehoses :
	    (size_t) (firehoses/(gasnetc_nodes-1));


	/* Initialize the list of firehose victims for each node.  This fh_data
	 * becomes the head of the per-node fifo, which also keeps a per-node
	 * count, gasnetc_fh_victim_count and a global maximum,
	 * gasnetc_fh_victim_max
	 */
	gasnetc_fh_victims = (gasnetc_fh_data_t *) 
	    gasneti_malloc(sizeof(gasnetc_fh_data_t) * (gasnetc_nodes));

	for (i = 0; i < gasnetc_nodes; i++)
		gasnetc_fh_victims[i].next = GASNETC_FH_NEXT_END;

	gasnetc_fh_victim_count = (gasneti_atomic_t *)
	    gasneti_malloc(sizeof(gasneti_atomic_t) * (gasnetc_nodes));
	memset((void *) gasnetc_fh_victim_count, 0, 
	    sizeof(gasneti_atomic_t) * (gasnetc_nodes));

	/* Each node keeps a per-node statistic of the number of available
	 * firehoses, gasnetc_fh_avail which compares with the maximum number of
	 * per-node firehoses, initialized below as gasnetc_fh_num. */
	gasnetc_fh_avail = (gasneti_atomic_t *)
	    gasneti_malloc(sizeof(gasneti_atomic_t) * (gasnetc_nodes));
	for (i = 0; i < gasnetc_nodes; i++)
		gasneti_atomic_set(&gasnetc_fh_avail[i], gasnetc_fh_num);

#ifdef STATS
	fprintf(stderr, "GASNETGM_FIREHOSE_M = %.2f Mb\t"
	    "GASNETGM_MAXVICTIM = %.2f Mb\n", 
	    (float) gasnetc_firehose_M / (1024*1024),
	    (float) gasnetc_firehose_MaxVictim / (1024*1024));
#endif
	GASNETI_TRACE_PRINTF(C, 
	    ("Firehose GASNETGM_FIREHOSE_M=%u bytes, ratio=%.2f, M=%.2f Mb", 
	    (unsigned int) fh_M,
	    (1-GASNETC_FIREHOSE_MAXVICTIM_RATIO),
	    ((unsigned) gasnetc_firehose_M) / (1<<20)));
	GASNETI_TRACE_PRINTF(C, 
	    ("Firehose Pinnable Memory=%.2f Gb", ((unsigned)pinnable)/(1<<30)));
	GASNETI_TRACE_PRINTF(C, 
	    ("Firehose hash_elems=%d, %d firehoses/node", firehoses, 
	    gasnetc_fh_num));

	gasnetc_firehose_initialized = 1;
	return;
}

static void
gasnetc_firehose_finalize()
{
	if (gasnetc_bucket_initialized || !gasnetc_firehose_initialized)
		gasneti_fatalerror("gasnetc_firehose_finalize(): Firehose "
		 "algorithm not initialized");

	/* Upon a clean exit, explicitly free the storage associated with
	 * firehose metadata */
	free(gasnetc_fh_victims);
	free(gasnetc_fh_victim_count);
	free((void *)gasnetc_fh_avail);

	gasnetc_firehose_table_finalize();

	gasnetc_firehose_initialized = 0;

	return;
}

/* ------------------------------------------------------------------------ */
/* Firehose Table implementation
 * *****************************
 * The firehose table stores information about hoses a node owns to every other
 * node.  Under the current implementation over GM, it is simply a wrapper
 * around the GM hash facility.  Other conduits or firehose implementations may
 * wish to use their own hash table api.
 */

gasneti_mutex_t	gasnetc_lock_fh_hash   = GASNETI_MUTEX_INITIALIZER;

static struct gm_hash		*gasnetc_fh_hash;
unsigned long			 gasnetc_firehose_hash_elems = 0;
unsigned long	 		 gasnetc_firehose_hash_total = 0;

#define GASNETC_LOCK_FH_HASH do {					    \
		gasneti_mutex_assertunlocked(&gasnetc_lock_fh_victim);      \
		gasneti_mutex_lock(&gasnetc_lock_fh_hash); } while (0)
#define GASNETC_UNLOCK_FH_HASH do {					    \
		gasneti_mutex_assertunlocked(&gasnetc_lock_fh_victim);      \
		gasneti_mutex_unlock(&gasnetc_lock_fh_hash); } while (0)


static void
gasnetc_firehose_table_init(size_t firehoses)
{
	/* XXX 1.2 hack to play safe until details are resolved in the
	 * algorithm */
	gasnetc_firehose_hash_total = (unsigned) firehoses*1.2;
	gasnetc_fh_hash = 
	    gm_create_hash(gm_hash_compare_ptrs, gm_hash_hash_ptr,
	    0, sizeof(gasnetc_fh_data_t), gasnetc_firehose_hash_total, 0);
	if (gasnetc_fh_hash == NULL)
		gasneti_fatalerror("could not create firehose hash!\n");
}

static void
gasnetc_firehose_table_finalize()
{
	gm_destroy_hash(gasnetc_fh_hash);
}

/* Wrapper around gm_hash_find, on its way out to a private interface */
GASNET_INLINE_MODIFIER(gasnetc_firehose_find)
gasnetc_fh_data_t *
gasnetc_firehose_find(gasnetc_fh_key_t key)
{
	gasnetc_fh_data_t	*data;

	GASNETC_LOCK_FH_HASH;

	data = (gasnetc_fh_data_t *) 
	    gm_hash_find(gasnetc_fh_hash, (void *)key);

	GASNETC_UNLOCK_FH_HASH;

	return data;
}

/* Adding a firehose should only be called from the AM move firehose reply
 * handler.  When a node issues a firehose move, the state of each firehose is
 * not marked until the roundtrip confirming the firehose move is completed.
 * When issuing many non-blocking operations to the same remote firehose, it is
 * possible that many firehose requests be in flight, each requesting to move
 * the _same_ firehose.  If node A and node B each request a move to the same
 * firehose, they must reserve the resources to move the firehose.  Reserving
 * resources either translates into increasing the firehose usage count if it
 * is within the maximum, or removing a firehose from the victim FIFO queue.
 *
 * The first firehose handler to complete will add the key to the hash table
 * and set its reference count to 1.  Once any other firehose reply handler
 * runs and attempts to add the key to the hash, it should see that the key
 * already exists, thus resorts to incrementing the reference count.  This
 * collision must also free the resources that were put aside in order to
 * satisfy the firehose move in the first place.  If the firehose usage count
 * was incremented or a 0-refcount firehose was popped from the victim fifo,
 * the firehose using count can simply be decremented as the colliding firehose
 * add will _not_ be using additional firehose resources.
 */
GASNET_INLINE_MODIFIER(gasnetc_firehose_add)
void
gasnetc_firehose_add(gasnetc_fh_key_t key)
{
	gm_status_t		status;
	gasnetc_fh_data_t	data, *data_ptr;
	gasnet_node_t		node;

	node = GASNETC_FH_KEY_NODE(key);
	assert((unsigned)node < gasnetc_nodes);

	GASNETC_LOCK_FH_HASH;

	if ((data_ptr = (gasnetc_fh_data_t *)
	    gm_hash_find(gasnetc_fh_hash, (void *)key)) != NULL) {

		GASNETC_FH_REFC_INC(data_ptr);

		/* Some other thread presumably received a firehose ack and
		 * inserted the firehose into the table before we could.
		 * Therefore, one thread decremented the available number of
		 * firehoses one too many */
		gasneti_atomic_increment(&gasnetc_fh_avail[node]);

		GASNETI_TRACE_PRINTF(C,
		    ("Firehose add key: %p already in hash! (refcount=%d)",
		    (void *) key, GASNETC_FH_REFC(data_ptr)));
	}
	else {
		data.next = NULL;
		GASNETC_FH_REFC_INIT(&data, GASNETC_FH_KEY_ADDR(key), 1);
		GASNETI_TRACE_PRINTF(C, ("Firehose add key: %p", (void *)key));

		status = 
		    gm_hash_insert(gasnetc_fh_hash, (void *)key, (void *)&data);

		gasnetc_firehose_hash_elems++;
		if (gasnetc_firehose_hash_elems > gasnetc_firehose_hash_total)
			gasneti_fatalerror("Hash table overflow! "
			    "elems/tot = %lu/%lu\n", gasnetc_firehose_hash_elems,
			    gasnetc_firehose_hash_total);

		assert(GASNETC_FH_REFC((gasnetc_fh_data_t *)
	    	    gm_hash_find(gasnetc_fh_hash, (void *)key)) == 1);

		if (status != GM_SUCCESS)
			gasneti_fatalerror(
			    "could not insert key in firehose hash");
	}

	GASNETC_UNLOCK_FH_HASH;

	return;
}

GASNET_INLINE_MODIFIER(gasnetc_firehose_remove)
void
gasnetc_firehose_remove(gasnetc_fh_key_t key)
{
	GASNETC_LOCK_FH_HASH;
	if (gm_hash_remove(gasnetc_fh_hash, (void *)key) == NULL) {
		gasneti_fatalerror(
		    "key doesn't exist in firehose hash");
	}
	gasnetc_firehose_hash_elems--;
	GASNETC_UNLOCK_FH_HASH;
}


GASNET_INLINE_MODIFIER(gasnetc_firehose_is_pinned)
int
gasnetc_firehose_is_pinned(gasnetc_fh_key_t key)
{
	gasnetc_fh_data_t	*fh_data;
	int			ret = 0;

	GASNETC_LOCK_FH_HASH;

	if ((fh_data = (gasnetc_fh_data_t *) 
	    gm_hash_find(gasnetc_fh_hash, (void *)key)) != NULL) {

		/* In order to save storage space for firehose metadata, each
		 * firehose descriptor only stores a next pointer, which points
		 * to the next element in the firehose victim fifo.  However,
		 * this means that it is possible to have non-zero reference
		 * counts in the FIFO when a refcount=0 descriptor is reused.
		 * This is a limitation of the algorithm, but dealt with
		 * correctly when looking for "free" firehoses below. */

		GASNETC_FH_REFC_INC(fh_data);

		GASNETI_TRACE_PRINTF(C, 
		    ("Firehose ispinned: %p refcount=%d", (void *)key,
		    GASNETC_FH_REFC(fh_data)));

		ret = 1;
	}
	#ifdef TRACE
	else {
		GASNETI_TRACE_PRINTF(C, 
		    ("Firehose ispinned: %p NOT in hash", (void *)key));
	}
	#endif

	GASNETC_UNLOCK_FH_HASH;

	return ret;
}
/* END firehose table */
#undef GASNETC_LOCK_FH_HASH
/* ------------------------------------------------------------------------ */

/* Unpin is only called on firehose keys which have previously been pinned.
 * Therefore, it should not be possible to 'unpin' a key with refcount=0 since
 * this would mean decrementing a reference count that has never been
 * incremented. */
GASNET_INLINE_MODIFIER(gasnetc_firehose_unpin)
void
gasnetc_firehose_unpin(gasnetc_fh_key_t key)
{
	gasnet_node_t		node;
	gasnetc_fh_data_t	*fh_data;

	fh_data = gasnetc_firehose_find(key);

	if (fh_data == NULL)
		gasneti_fatalerror("Firehose bucket not found in hash (key=%p)",
		    (void *) key);

	assert(GASNETC_FH_REFC(fh_data) > 0);

	GASNETC_FH_REFC_DEC(fh_data);

	GASNETI_TRACE_PRINTF(C, ("Firehose unpin key %p (refcount=%d)",
	    (void *)key, (unsigned) GASNETC_FH_REFC(fh_data)));

	/* If refcount==0 and the current firehose is not already in
	 * the victim FIFO, add it */
	if (GASNETC_FH_REFC_ISZERO(fh_data) && fh_data->next == NULL) {


		node = GASNETC_FH_KEY_NODE(key);
		assert((unsigned) node < gasnetc_nodes);

		GASNETC_LOCK_FH_VICTIM;

		fh_data->next = gasnetc_fh_victims[node].next;
		gasnetc_fh_victims[node].next = fh_data;
		gasneti_atomic_increment(&gasnetc_fh_victim_count[node]);

		GASNETC_UNLOCK_FH_VICTIM;

		GASNETI_TRACE_PRINTF(C, 
		    ("Firehose victim added: %p, node %d=%d victims", 
		    (void *)key, (unsigned) node,
		    (unsigned) 
		    gasneti_atomic_read(&gasnetc_fh_victim_count[node])));

	}

	return;
}

/*
 * In attempting to find free firehoses from the 0-refcount victim firehose
 * FIFO, it is possible that entries in the fifo are dirty (their refcount is
 * not 0).  This arises when 0-refcount entries living in the victim FIFO queue
 * are reused (thus locked) in an outsanding operation.  This situtation could
 * be corrected by adding prev/next pointers but this leads to larger metadata.
 * In our case, we sacrifice a small amount of CPU time.
 */

GASNET_INLINE_MODIFIER(gasnetc_firehose_find_freevictim)
gasnetc_fh_data_t *
gasnetc_firehose_find_freevictim(gasnet_node_t node)
{
	gasnetc_fh_data_t	*data, *data_tmp, *data_ret = NULL;
	gasnetc_fh_key_t	key;

	assert(node < gasnetc_nodes);
	gasneti_mutex_assertunlocked(&gasnetc_lock_fh_victim);
	gasneti_mutex_assertunlocked(&gasnetc_lock_fh_hash);

	GASNETC_LOCK_FH_VICTIM;

	data = &gasnetc_fh_victims[node];
	while (data->next != GASNETC_FH_NEXT_END) {
		/* We always remove the element from the FIFO, notwithstanding
		 * its reference count.  It's important to set the next pointer
		 * to NULL */
		data_tmp = data->next;
		data->next = data->next->next;
		data_tmp->next = NULL;
		gasneti_atomic_decrement(&gasnetc_fh_victim_count[node]);
		key = GASNETC_FH_KEY(GASNETC_FH_ADDR(data_tmp), node);

		if (gasnetc_firehose_find(key) != NULL)
			gasneti_fatalerror(
			   "Firehose in FIFO queue not in hash table "
			   "key=%p count=%d", (void *)key, 
			   gasneti_atomic_read(&gasnetc_fh_victim_count[node]));

		/* If the reference count is zero, we can safely reuse this
		 * firehose, or else just remove the element from the FIFO
		 * and keep looking for a refcount==0
		 */
		if (GASNETC_FH_REFC_ISZERO(data_tmp)) {
			data_ret = data_tmp;
			break;
		}
	} 

	GASNETC_UNLOCK_FH_VICTIM;

	return data_ret;
}

/* Firehose lookup by address checks to see if the local node has a firehose to
 * each bucket covered by (dest, dest+len) on remote node 'node'.  In case one
 * of the firehoses does not map to a required bucket, we must find a
 * replacement.  Returns a successful '0' if the node owns firehoses for all
 * buckets
 */
extern int
gasnetc_firehose_build_list(uintptr_t *buf, gasnet_node_t node, uintptr_t dest,
				unsigned int num_buckets,
				unsigned int *new_buckets,
				unsigned int *old_buckets)
{
	unsigned int			i;
	unsigned int			new, old;

	volatile gasnetc_fh_data_t	*victim;
	uintptr_t			bucket_addr, bucket_cur;
	uintptr_t			*buf_new, *buf_old;
	uintptr_t			vic_addr;

	gasneti_mutex_assertunlocked(&gasnetc_lock_fh_victim);
	gasneti_mutex_assertunlocked(&gasnetc_lock_fh_hash);
	assert((unsigned)node < gasnetc_nodes);
	assert(buf != NULL);
	assert(sizeof(uintptr_t)*num_buckets*2 < gasnet_AMMaxMedium());

	bucket_addr = GASNETI_ALIGNDOWN(dest, GASNETC_BUCKET_SIZE);
	buf_new = buf;
	buf_old = (uintptr_t *) buf + num_buckets;

	new = 0;	/* new firehose to move */
	old = 0;	/* replacement firehose */

	for (i = 0; i < num_buckets; i++) {
		bucket_cur = bucket_addr + (i<<GASNETC_BUCKET_SHIFT);

		/* if already have a firehose, go to the next bucket */
		if (gasnetc_firehose_is_pinned(GASNETC_FH_KEY(bucket_cur,node)))
			continue;

		/* if fh_avail is down to zero, poll for firehoses */
		if (gasneti_atomic_read(&gasnetc_fh_avail[node]) == 0) {

			GASNETI_TRACE_PRINTF(C,
			    ("Firehose out of firehoses, polling. ."));

			victim = gasnetc_firehose_find_freevictim(node);
			if (victim == NULL) {

				/* Overcomplicated polling loop to minimize
				 * locking overhead when looking for victims */
				while (1) {
					gasnetc_AMPoll();
			   		if (gasneti_atomic_read(
					    &gasnetc_fh_victim_count[node]) == 0);
						continue;

					victim = 
					    gasnetc_firehose_find_freevictim(node);
					if (victim != NULL)
						break;
				}

				/* Once we return from a poll, it's possible
				 * that other threads decided to acquire the
				 * firehose lock and move a firehose over the
				 * current bucket desc.
				 */
				if (gasnetc_firehose_is_pinned(
				    GASNETC_FH_KEY(bucket_cur,node)))
					continue;
			}

			GASNETI_TRACE_EVENT(C, FIREHOSE_VICTIM_POLLS);

			/* By now, we really know we need to move the firehose
			 * to the new location */
			vic_addr = GASNETC_FH_ADDR(victim);
			GASNETI_TRACE_PRINTF(C, 
			    ("Firehose remove victim key=%p, addr=%p",
			    (void *)GASNETC_FH_KEY(vic_addr, node),
			    (void *)vic_addr));

			gasnetc_firehose_remove((gasnetc_fh_key_t)
			    GASNETC_FH_KEY(vic_addr, node));

			buf_old[old] = vic_addr;
			old++;
		}

		/* Lose a firehose for now */
		gasneti_atomic_decrement(&gasnetc_fh_avail[node]);

		buf_new[new] = bucket_cur;
		new++;
	}
	assert(new >= old);
	*new_buckets = new;
	*old_buckets = old;
	return new+old;
}

/* Called froma GM callback function to decrement the reference count over
 * [dest,dest+nbytes] */

extern void
gasnetc_firehose_decrement_refcount(gasnet_node_t node, uintptr_t dest, 
				    size_t nbytes)
{
	uintptr_t	bucket_cur;
	size_t		num_buckets;
	int		i;

	gasneti_mutex_assertlocked(&gasnetc_lock_gm); /* gm callback only */
	bucket_cur = GASNETI_ALIGNDOWN(dest, GASNETC_BUCKET_SIZE);
	num_buckets = GASNETC_NUM_BUCKETS(bucket_cur,dest+nbytes);

	for (i = 0; i < num_buckets; i++) {
		gasnetc_firehose_unpin(GASNETC_FH_KEY(bucket_cur,node));
		bucket_cur += GASNETC_BUCKET_SIZE;
	}
}

/* ------------------------------------------------------------------------ */
/* Firehose move reply handler, called by the core Firehose request
 * handler */
GASNET_INLINE_MODIFIER(gasnetc_firehose_move_reph_inner)
void
gasnetc_firehose_move_reph_inner(gasnet_token_t token, void *addr, 
				 size_t nbytes, 
				 gasnet_handlerarg_t new_buckets,
				 void *context)
{
	uintptr_t	*new_buckets_list;
	gasnet_node_t	node;
	unsigned int	i;

	gasnet_AMGetMsgSource(token, &node);
	assert(new_buckets > 0);
	assert(node < gasnetc_nodes);
	new_buckets_list = (uintptr_t *) addr;
	GASNETI_TRACE_PRINTF(C, 
	    ("Firehose move reply received new=%d", new_buckets));
	for (i = 0; i < new_buckets; i++) {
		assert(new_buckets_list[i] % GASNETC_BUCKET_SIZE == 0);
		gasnetc_firehose_add(GASNETC_FH_KEY(new_buckets_list[i],node));
	}
	/* Entry point for extended wanting to move firehose */
	gasnete_firehose_move_done(context);
}
MEDIUM_HANDLER(gasnetc_firehose_move_reph,2,3,
             (token,addr,nbytes, a0, UNPACK(a1)     ),
             (token,addr,nbytes, a0, UNPACK2(a1, a2)));

GASNET_INLINE_MODIFIER(gasnetc_firehose_move_firehose_reqh_inner)
void
gasnetc_firehose_move_reqh_inner(gasnet_token_t token, void *addr,
				 size_t nbytes, 
				 gasnet_handlerarg_t new_buckets, 
				 gasnet_handlerarg_t old_buckets,
				 gasnet_handlerarg_t old_bucket_off,
				 void *context)
{
	uintptr_t		*old_bucket_list;
	gasneti_stattime_t      movetime = GASNETI_STATTIME_NOW_IFENABLED(C);
	gasneti_stattime_t      unpintime;

	assert(new_buckets > 0);

	GASNETI_TRACE_PRINTF(C, 
	    ("Firehose move request received old=%d, new=%d", 
	    old_buckets, new_buckets));

	if (old_buckets > 0) {
		GASNETI_TRACE_EVENT_VAL(C, FIREHOSE_MOVE_OLD_BUCKETS, 
		    old_buckets);
		#if defined(TRACE) || defined(STATS)
		unpintime = GASNETI_STATTIME_NOW_IFENABLED(C);
		#endif
		old_bucket_list = (uintptr_t *) addr + old_bucket_off;
		gasnetc_bucket_unpin_by_list(old_bucket_list, old_buckets);
		GASNETI_TRACE_EVENT_TIME(C, FIREHOSE_UNPIN_TIME,
		    GASNETI_STATTIME_NOW_IFENABLED(C)-unpintime);
	}
	gasnetc_bucket_pin_by_list((uintptr_t *) addr, new_buckets);
	GASNETI_TRACE_EVENT_TIME(C, FIREHOSE_MOVE_TIME,
	    GASNETI_STATTIME_NOW_IFENABLED(C)-movetime);

	MEDIUM_REP(2,3,(token, 
	    gasneti_handleridx(gasnetc_firehose_move_reph), 
	    addr, new_buckets*sizeof(uintptr_t), new_buckets, PACK(context)));
}	
MEDIUM_HANDLER(gasnetc_firehose_move_reqh,4,5,
              (token,addr,nbytes, a0, a1, a2,  UNPACK(a3)     ),
              (token,addr,nbytes, a0, a1, a2, UNPACK2(a3, a4)));

/* ------------------------------------------------------------------------ */
/* Two initialization that abstract the firehose algorithm from the core. */
extern void
gasnetc_rdma_init(uintptr_t segbase, uintptr_t segsize, uintptr_t global_physmem)
{
	gasnetc_bucket_init(segsize, global_physmem);
	gasnetc_bucket_pin_stack();
	gasnetc_firehose_init(segsize, global_physmem);
}

extern void
gasnetc_rdma_finalize()
{
	gasnetc_bucket_finalize();
	gasnetc_firehose_finalize();
}
/* ------------------------------------------------------------------------ */
/* "IS_PINNED" interface exported to core by the firehose RDMA plugin */
GASNET_INLINE_MODIFIER(gasnetc_bucket_is_stack)
int
gasnetc_bucket_is_stack(uintptr_t src, size_t nbytes)
{
	if (src >= gasnetc_stackaddr_lo && (src+nbytes) < gasnetc_stackaddr_hi)
		return 1;
	else
		return 0;
}

GASNET_INLINE_MODIFIER(gasnetc_bucket_is_pinned_by_addr)
int
gasnetc_bucket_is_pinned_by_addr(uintptr_t src, size_t nbytes)
{
	uintptr_t	bucket_addr;
	size_t		num_buckets;
	unsigned int	i, ispinned, bidx;

	ispinned = 1;
	bucket_addr = GASNETI_ALIGNDOWN(src, GASNETC_BUCKET_SIZE);
	num_buckets = GASNETC_NUM_BUCKETS(bucket_addr,src+nbytes);
	bidx = GASNETC_BDESC_INDEX_FROM_ADDR(bucket_addr);

	GASNETC_LOCK_BUCKET;

	for (i = 0; i < num_buckets; i++) {
		if (!GASNETC_BDESC_ISPINNED(gasnetc_bucket_table+bidx+i)) {
			ispinned = 0;
			break;
		}
	}
	/* if all buckets are pinned, now mark them */
	if (ispinned) {
		for (i = 0; i < num_buckets; i++)
			GASNETC_BDESC_REFC_INC(gasnetc_bucket_table+bidx+i);
	}

	GASNETC_UNLOCK_BUCKET;

	return ispinned;
}
GASNET_INLINE_MODIFIER(gasnetc_firehose_is_pinned_by_addr)
int
gasnetc_firehose_is_pinned_by_addr(gasnet_node_t node, uintptr_t ptr,
				   size_t nbytes)
{
	uintptr_t	bucket_addr;
	size_t		num_buckets;
	unsigned int	i;

	gasneti_mutex_assertunlocked(&gasnetc_lock_fh_victim);
	gasneti_mutex_assertunlocked(&gasnetc_lock_fh_hash);

	bucket_addr = GASNETI_ALIGNDOWN(ptr, GASNETC_BUCKET_SIZE);
	num_buckets = GASNETC_NUM_BUCKETS(bucket_addr,ptr+nbytes);

	for (i = 0; i < num_buckets; i++) {
		if (!gasnetc_firehose_is_pinned(
		    GASNETC_FH_KEY(bucket_addr,node)))
			return 0;
		bucket_addr += GASNETC_BUCKET_SIZE;
	}
	return 1;
}

/* ------------------------------------------------------------------------- */
/* Two functions exported to the core for RDMA operations */
extern int
gasnetc_is_pinned(gasnet_node_t node, uintptr_t ptr, size_t nbytes)
{
	if (node == gasnetc_mynode) {
		if (gasnetc_bucket_is_stack(ptr,nbytes)) {
			GASNETI_TRACE_PRINTF(C, 
			    ("Firehose ispinned in stack (%p)", (void *)ptr));
			return 1;
		}
		if (gasnetc_bucket_is_pinned_by_addr(ptr,nbytes)) {
			GASNETI_TRACE_PRINTF(C, 
			    ("Firehose ispinned local TRUE (%p,%d)", 
			    (void *)ptr, nbytes));
			return 1;
		}
		GASNETI_TRACE_PRINTF(C, 
		    ("Firehose ispinned local FALSE (%p,%d)", 
		    (void *)ptr, nbytes));
		return 0;
	}
	else {
		if (gasnetc_firehose_is_pinned_by_addr(node,ptr,nbytes)) {
			GASNETI_TRACE_PRINTF(C, 
			    ("Firehose ispinned TRUE (%d <- %p,%d)", 
			    (unsigned) node, (void *)ptr, nbytes));
			return 1;
		}
		GASNETI_TRACE_PRINTF(C, 
		    ("Firehose ispinned FALSE (%d <- %p,%d)", 
		    (unsigned) node, (void *)ptr, nbytes));
		return 0;
	}
}

extern void
gasnetc_done_pinned(gasnet_node_t node, uintptr_t ptr, size_t nbytes)
{
	uintptr_t	bucket_addr;
	size_t		num_buckets;
	unsigned int	i;

	gasneti_mutex_assertlocked(&gasnetc_lock_gm); /* gm callback only */

	if (node == gasnetc_mynode) {
		if (gasnetc_bucket_is_stack(ptr,nbytes)) {
			GASNETI_TRACE_PRINTF(C, 
			    ("Firehose done_pinned in stack (%p)",(void *)ptr));
			return;
		}

		GASNETI_TRACE_PRINTF(C, 
		    ("Firehose done_pinned local (%p,%d bytes)", 
		    (void *)ptr, nbytes));

		bucket_addr = GASNETI_ALIGNDOWN(ptr, GASNETC_BUCKET_SIZE);
		num_buckets = GASNETC_NUM_BUCKETS(bucket_addr,ptr+nbytes);

		GASNETI_TRACE_PRINTF(C, ("Firehose bucket_done (%p,%d bytes)", 
		    (void *) ptr, nbytes));

		gasnetc_bucket_tryunpin_by_bucket_gm_locked(bucket_addr, 
		    num_buckets);
		return;

	}
	else {
		GASNETI_TRACE_PRINTF(C, 
		    ("Firehose done_pinned remote (%d <- %p,%d bytes)", 
		    (unsigned) node, (void *)ptr, nbytes));

		assert(node < gasnetc_nodes);
		bucket_addr = GASNETI_ALIGNDOWN(ptr, GASNETC_BUCKET_SIZE);
		num_buckets = GASNETC_NUM_BUCKETS(bucket_addr, ptr+nbytes);

		for (i = 0; i < num_buckets; i++) {
			GASNETI_TRACE_PRINTF(C,("Firehose done_pinned (%d <- %p)",
			    (unsigned) node, (void *) bucket_addr));
			gasnetc_firehose_unpin(GASNETC_FH_KEY(bucket_addr, node));
			bucket_addr += GASNETC_BUCKET_SIZE;
		}
	}
	return;
}

/* ------------------------------------------------------------------------- */
/* RDMA plugin handlers */
static gasnet_handlerentry_t const gasnetc_handlers[] = {
  /* ptr-width dependent handlers */
  gasneti_handler_tableentry_with_bits(gasnetc_firehose_move_reqh),
  gasneti_handler_tableentry_with_bits(gasnetc_firehose_move_reph),
  { 0, NULL }
};

extern gasnet_handlerentry_t const * gasnetc_get_rdma_handlertable() {
  return gasnetc_handlers;
}

#endif

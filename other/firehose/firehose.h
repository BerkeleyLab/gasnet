/* firehose.h: Public Header file */
#include "firehose_fwd.h"

typedef struct _firehose_request_t	firehose_request_t;

struct _firehose_private_t;

#if (defined(FIREHOSE_PAGE) && defined(FIREHOSE_REGION)) || \
    (!defined(FIREHOSE_PAGE) && !defined(FIREHOSE_REGION))
#error Only define one of FIREHOSE_PAGE or FIREHOSE_REGION
#endif

/* In page-flavoured firehose, the request type only serves as a
 * descriptor between initiation and release of a region destined for
 * DMA operations.  Each page covered within the region has a
 * descriptor as firehose_private_t which resides in a hash table.
 * The first bucket of each region is cached in the request type.
 *
 * Copies of this type are never kept around in hash tables.
 *
 * In region-flavoured firehose, the request type serves as a
 * descriptor between initiation and release of a region but also is
 * hashed as the descriptor for all active regions.
 *
 * Copies of this will exist both in the firehose table on the node
 * owning the firehose, and in the bucket table of the node with the
 * memory.
 */
struct _firehose_request_t {
	gasnet_node_t	node;	/* ignored in the bucket table */
	uintptr_t	addr;
	size_t		len;

	/* For internal use by firehose library, defined in firehose_internal.h
	 * hold such things as the reference count and linked list pointers */
	struct _firehose_private_t	*internal;

        #ifdef FIREHOSE_CLIENT_T
	  /* For CLIENT use, defined in firehose_fwd.h
	   * Useful for keys/handles and similar transport-specific data
	   * Note that this is included inline, not as a reference.
	   */
	firehose_client_t	client;
        #endif
};

#ifdef FIREHOSE_PAGE
/* In page-based firehose, only page addresses need to be copied over
 * the network when firehose movement is required. */
struct _firehose_region_t {
	void	*addr;

	#ifdef FIREHOSE_CLIENT_T
	firehose_client_t	client;
	#endif
};

#elif defined(FIREHOSE_REGION)
/* In region-based firehose, the address and length must be sent over
 * the network when firehose moves are requested.  Also,
 * client-specific data may be required.
 */
struct _firehose_region_t {
	void	*addr;
	size_t	len;

	#ifdef FIREHOSE_CLIENT_T
	firehose_client_t	client;
	#endif
};
#endif

/* Type for function called after firehose placement is acknowledged */
typedef int (*firehose_completed_fn_t)(void *context, firehose_request_t *req);

/* This prototype is for a callback implemented by the CLIENT
 * There is no need to carry it around as a function pointer
 *
 * firehose_pin_callback(fh_req)
 *
 * This callback is invoked when the firehose library has
 * determined the need for a "new" pinned region (one which
 * does not replace an existing one) on the local node.
 *
 * This is a synchronous (blocking) operation, and appropriate care
 * must be taken as this function may be called from an AM handler.
 *
 * Upon entry the 'addr' and 'len' fields give the requested memory
 * region, rounded to page alignment.  If FIREHOSE_CLIENT_T is
 * defined, the function should also fill-in any necessary data in the
 * 'client' field, which will be copied back to the node owning the
 * firehose (could be the local node) in an AMReplyMedium().
 *
 * Returns: 0 on success, non-zero on failure.
 */
extern int firehose_pin_callback(firehose_request_t *req);

/* This prototype is for a callback implemented by the CLIENT
 * There is no need to carry it around as a function pointer
 *
 * firehose_unpin_callback(fh_req)
 *
 * This callback is invoked when the firehose library has determined
 * the need to delete a pinned region on the local node.
 *
 * This is a synchronous (blocking) operation, and appropriate care
 * must be taken as this function may be called from an AM handler.
 *
 * Returns: 0 on success, non-zero on failure.
 */
extern int firehose_unpin_callback(firehose_request_t *req);

/* This prototype is for a callback implemented by the CLIENT
 * There is no need to carry it around as a function pointer
 *
 * firehose_move(old, old_num, reg, reg_num)
 *
 * This callback is invoked when the firehose library has determined the
 * need to move a pinned region (replace an existing one).  For some
 * networks, supplying the 'old' data can allow a firehose move to be done
 * much more efficiently (reusing resources) at the network API layer than
 * an unpin and pin.
 *
 * This is a synchronous (blocking) operation and is always called
 * from within an AM handler.
 *
 * If the client has defined FIREHOSE_CLIENT_T, the function should
 * fill-in any neccesary data in the 'client' field, which will be
 * copied back to the node owning the firehose (could be the local
 * node) in an AMReplyMedium().
 *
 * The 'old' argument describes the region to be replaced with the
 * requested one.
 *
 * Returns: 0 on success, non-zero on failure.
 */
extern int firehose_move(firehose_region_t *old, size_t old_num,
		         firehose_region_t *reg, size_t new_num);

/* firehose_init(maximum_pinnable_memory, maximum_regions)
 *
 * Called to setup the firehose tables and data structures.  Firehose
 * can account for two parameters in separating pinning resources:
 *   1. The 'maximum_pinnable_memory' is the upper bound for the
 *      firehose 'M' parameter and must be a global minimum of 
 *      the largest amount of memory that can be pinned by each node.
 *      This value should be a fraction of the amount of physical
 *      memory a single process can pin.
 *   2. The 'maximum_regions' is the upper bound for the firehose
 *      'M-region' parameter and must be a global minimum of the
 *      largest amount of regions that can be allocated by each node.
 *
 *   Setting either value to zero removes the constraints associated
 *   to the count.  In other words, the firehose algorithm can consider
 *   there to be no contraints on the amount of pinned memory or
 *   maximum regions if either value is set to 0.
 */
extern void
firehose_init(uintptr_t max_pinnable_memory, size_t max_regions);

/* Environment variables used in firehose initialization
 *
 * Although firehose is informed of job-wide memory limitations
 * through its initialization function, users are expected to control
 * firehose through environment variables.
 *
 * GASNET_FIREHOSE_M establishes the number of firehose buckets to be 
 *                   partitioned across all nodes.  This is limited by
 *                   the 'max_pinnable_memory' parameter.
 *
 * GASNET_FIREHOSE_R establishes the maximum number of regions to be
 *                   partitioned across all nodes.  This value is
 *                   ignored if 'max_regions' is 0.
 *
 * GASNET_FIREHOSE_MAXVICTIM limits the length of the FIFO queue and
 *                           hence the amount of inactive pinned
 *                           regions in MB.  This allows firehose to
 *                           ammortize the number of unpin operations.
 *
 * NOTE: firehose_init() will fail at initialization if
 * GASNET_FIREHOSE_M+GASNET_FIREHOSE_MAXVICTIM > max_pinnable_memory or
 * if GASNET_FIREHOSE_R > max_regions
 *
 * Failing to set these environment variables causes metadata for the
 * maximum amount of memory and regions to be allocated at
 * initialization. 
 */

/* firehose_fini()
 *
 * Called to cleanup */
extern void
firehose_fini(void);

/* firehose_local_pin(addr, len)
 *
 * Called to request local pinning of a specified region.
 * This is a synchronous (blocking) operation.
 * 
 * The return value will be non-null on success and may describe a
 * region that is a superset of the one requested, namely the start
 * address can be lower and the length of the region can be larger.
 *
 * The return value will be non-null on success and describe a region
 * which may be larger than requested, but never smaller.
 *
 * On failure, returns NULL.
 *
 * This call increments the ref count on the region and therefore must
 * be balanced by a call to firehose_release_*().
 */
extern firehose_request_t *
firehose_local_pin(uintptr_t addr, size_t len);

/* firehose_try_local_pin(addr, len)
 *
 * Called to attempt a local pinning of a specified region.
 * This is a non-blocking operation.
 *
 * If it is known that the region is already pinned, the region's
 * reference count is incremented and the region's request descriptor
 * is returned.  The returned region may be a superset of the one
 * requested, namely the start address can be lower and the length of
 * the region can be larger.
 *
 * If the region covered by (addr, addr+len) is not pinned, the
 * function returns NULL.
 */
extern firehose_request_t *
firehose_try_local_pin(uintptr_t addr, size_t len);

/* firehose_remote_pin(node, addr, len, callback, context)
 *
 * Called to request remote pinning of a specified region.  This call
 * always causes an asynchronous callback on the local node except
 * when 'return_if_pinned' is non-null, in which case the call is
 * allowed to return with a descriptor representing the already pinned
 * region if it is known that the region is already pinned.  If the
 * region is not pinned, the call returns 0 and a remote pin request
 * is enqueued.
 *
 * If 'return_if_pinned' is zero, the supplied callback will always be
 * invoked on the local node with the supplied context pointer once
 * the region is pinned on the remote host.  In the event the
 * requested region is already pinned the supplied callback is still
 * called by the firehose library.  However, it is undefined whether
 * that call is made before this call returns, or in what thread it
 * executes.
 *
 * The library increments the ref count on the region before invoking
 * the local callback.  Therefore the callback is responsible for a
 * call to firehose_release_*(), or ensuring one will be made
 * eventually.  The returned region may be a superset of the one
 * requested, namely the start address can be lower and the length of
 * the region can be larger.
 */
extern firehose_request_t *
firehose_remote_pin(gasnet_node_t node, uintptr_t addr, size_t len,
		    firehose_completed_fn_t callback, void *context,
		    int return_if_pinned);

/* firehose_try_remote_pin(node, addr, len)
 *
 * Called to request a conditional remote pinning operation in a
 * non-blocking fashion.  The call only returns with a valid request
 * type if the remote region is already pinned.  In any other case,
 * the call returns NULL.
 *
 * The returned region may be a superset of the one requested, namely
 * the start address can be lower and the length of the region can be
 * larger.
 */
extern firehose_request_t *
firehose_try_remote_pin(gasnet_node_t node, uintptr_t addr, size_t len);

/* firehose_release(requests, num_requests)
 *
 * Called to indicate that use of the indicated 'num_requests'
 * requests for RDMA has completed.  This is a synchronous (blocking)
 * operation.
 *
 * The reference counts are decremented and the regions moved to a
 * victim FIFO if the count reaches zero.
 *
 * The supplied regions can be local or remote.
 *
 */
extern void
firehose_release(firehose_request_t **reqs, int numreqs);

/*
 * firehose_partial_remote_pin(node, addr, len)
 *
 * Called to select a pinned region that is possibly a subset of the
 * region specified.
 *
 * Like firehose_try_remote_pin(), this routine returns a region which
 * was already pinned at the time of the call, without causing network
 * traffic, or NULL if no such region is available.  If the return is
 * not NULL, the reference count has been incremented before return.
 *
 * Unlike firehose_try_remote_pin(), this routine may return regions
 * which only partially cover the requested range.  When multiple
 * regions intersect the requested range, a deterministic algorithm is
 * used to select the "initial/maximal" intersection.  Roughly this
 * means highest importance is given to where in the requested range
 * the region starts (initial), and ties are broken by the size of the
 * intersection (maximal).
 
 * Specifically:
 * 1) First select the region(s) with the minimum starting address for
 *    the intersection with the requested range.  Note that this gives
 *    equal ranking to all regions which overlap the first page of the
 *    requested range.
 * 2) If step 1 yields a unique minimum, then return the corresponding
 *    region.  Else proceed to step 3, keeping only the regions which
 *    tied for the minimum in step 1.
 * 3) From the remaining candidates, select the region(s) with the
 *    maximum ending address.
 * 4) If step 3 yields a unique maximum, then return the corresponding
 *    region.  Else proceed to step 5, keeping only the regions which
 *    tied for the maximum in step 3.
 * 5) Return the largest region from the remaining candidates.
 */

extern firehose_request_t *
firehose_partial_remote_pin(gasnet_node_t node, uintptr_t addr, size_t len);


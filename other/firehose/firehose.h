/* firehose.h: Public Header file */
#include "firehose_fwd.h"

struct _firehose_private_t;

#if (defined(FIREHOSE_PAGE) && defined(FIREHOSE_REGION)) || \
    (!defined(FIREHOSE_PAGE) && !defined(FIREHOSE_REGION)) || \
    (defined(FIREHOSE_PAGE) && defined(FIREHOSE_CLIENT_T)
#error Only define one of FIREHOSE_PAGE or FIREHOSE_REGION.  Make sure \
       FIREHOSE_CLIENT_T is only defined if FIREHOSE_REGION is defined.
#endif

/* The firehose request type is returned as a read-only type from
 * firehose local and remote pin functions.  Based on the address and
 * length requested by the pin operation, this return type describes a
 * region that is a superset of the one requested, namely the start
 * address can be lower and the length of the region can be larger.
 *
 * Request types are allocated on a per-pin request basis, and are
 * freed once the request is released by the client.  Once returned to
 * the client, this type is read-only.  Copies of this type are never
 * kept around in hash tables.
 */
typedef
struct _firehose_request_t {
	gasnet_node_t	node;	/* ignored in the bucket table */
	uintptr_t	addr;
	size_t		len;

	/* For internal use by firehose library, defined in
	 * firehose_internal.h hold such things as the reference count
	 * and linked list pointers */
	struct _firehose_private_t	*internal;

        #ifdef FIREHOSE_CLIENT_T
	  /* For CLIENT use, defined in firehose_fwd.h
	   * Useful for keys/handles and similar transport-specific data
	   * Note that this is included inline, not as a reference.
	   */
	firehose_client_t	client;
        #endif
}
firehose_request_t;

/* The firehose region type contains the necessary minimal information
 * required to describe a pinned region.  The type is used both by the
 * client-supplied firehose_move_callback to pin and unpin regions and
 * internally by the firehose algorithm to disconnect old firehoses
 * and reconnect new ones.
 *
 * If the network requires client data to be attached to each pinning
 * operation, the client field should be filled in.
 *
 */
typedef
struct _firehose_region_t {
	uintptr_t	addr;
	size_t		len;	/* length field is extraneous on 
				   the network in firehose-page */

	#ifdef FIREHOSE_CLIENT_T
	firehose_client_t	client;
	#endif
} 
firehose_region_t;

/* The firehose information type contains information relative to the
 * the limits of system and network-related available to firehose.
 * The type is returned at initialization and contains limit
 * information the client can query at initialization.  The limit
 * values are calculated by the firehose interface according to the
 * following parameters:
 *    1. Maximum amount of globally pinnable memory
 *    2. Maximum amount of regions that may be created
 *    3. Environment variables to control firehose (see
 *       GASNET_FIREHOSE_ environment variables below).
 *    4. gasnet_AMMaxMedium() as implemented by the underlying gasnet
 *       core API.
 *
 * The values returned by firehose_info_t are established at
 * initialization.  Typically, a client will use these limits in order
 * to determine the size of the largest remote and/or local region
 * that can be requested through the firehose interface.
 */
typedef
struct _firehose_info_t {
	size_t	max_RemotePinSize;
	size_t	max_LocalPinSize;

	size_t  max_FifoPages;
}
firehose_info_t;

/* Type for function called after firehose placement is acknowledged */
typedef int (*firehose_completed_fn_t)(void *context, firehose_request_t *req);

/* This prototype is for a callback implemented by the CLIENT
 *
 * firehose_move_callback(node, unpin_list, unpin_num, pin_list, pin_num)
 *
 * This callback is invoked when the firehose library has determined
 * the need to pin and/or unpin one or many regions.  If there are
 * regions to be unpinned, the unpin call should be executed prior to
 * the pin call.  For some networks, it may be possible to use a repin
 * operation, allowing pinning resources to be used more effectively.
 *
 * This is a synchronous (blocking) operation and is always called
 * from within an AM handler.
 *
 * If the client has defined FIREHOSE_CLIENT_T, the function should
 * fill-in any neccesary data in the 'client' field, which will be
 * copied back to the node owning the firehose (could be the local
 * node) in an AMReplyMedium().
 *
 * Returns: 0 on success, non-zero on failure.
 */
extern int firehose_move_callback(gasnet_node_t node,
				  firehose_region_t *unpin_list, 
				  size_t unpin_num, 
				  firehose_region_t *pin_list, 
				  size_t pin_num);

#ifdef FIREHOSE_BIND_CALLBACK
/* This prototype is for a callback implemented by the CLIENT iff the
 * client defines FIREHOSE_BIND_CALLBACK.
 *
 * This callback is invoked by the firehose library when the node
 * initiating a move operation has received a reply to the list of
 * regions to be pinned.  It is up to the client to make sure
 * (possibly by way of a firehose_client_t) that any metadata required
 * to bind to a remote region is part of the region type.
 *
 */
extern int firehose_bind_callback(gasnet_node_t node,
				  firehose_region_t *pin_list,
				  size_t pin_num);
#endif

#ifdef FIREHOSE_UNBIND_CALLBACK 
/* This prototype is for a callback implemented by the CLIENT iff the
 * client defines FIREHOSE_UNBIND_CALLBACK.
 *
 * This callback is invoked by the firehose library selects one or
 * many regions to be unpinned.  It is up to the client to make sure
 * (possibly by way of a firehose_client_t) that any metadata required
 * to unbind a local node to a remote region is part of the region
 * type.
 */
extern int firehose_unbind_callback(gasnet_node_t node,
				    firehose_region_t *unpin_list,
				    size_t unpin_num);
#endif

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

extern const firehose_info_t *
firehose_init(uintptr_t max_pinnable_memory, size_t max_regions);

/* Environment variables used in firehose initialization
 *
 * Although firehose is informed of job-wide resource limitations
 * through its initialization function, users can control firehose
 * parameters through environment variables.
 *
 * Except where noted, the numerical values are assumed to be base-2
 * megabytes.  For these environement variables, a suffix of 'GB' can
 * be appended for (base-2) gigabytes ('MB' will simply be ignored if
 * it is specified).
 *
 * GASNET_FIREHOSE_M establishes, from megabytes, the number of
 *                   firehose buckets to be partitioned across all
 *                   nodes.  This is limited by the
 *                   'max_pinnable_memory' parameter.
 *
 * GASNET_FIREHOSE_R establishes, from units in regions, the maximum
 * 		     number of regions to be partitioned across all
 * 		     nodes.  This value is ignored if 'max_regions' is
 * 		     0.
 *
 * GASNET_FIREHOSE_MAXVICTIM_M limits, from megabytes, the length of
 *                             the FIFO queue and hence the amount of
 *                             inactive pinned regions.  This allows
 *                             firehose to ammortize the number
 *                             of unpin operations.
 *
 * GASNET_FIREHOSE_MAXVICTIM_R limits, from units in regions, the
 *                             length of the FIFO queue and hence the
 *                             amount of inactive pinned regions.
 *                             This value is ignored if 'max_regions'
 *                             is 0.
 *
 * GASNET_FIREHOSE_MAXREGION_SIZE limits, from megabytes, the length
 * 				  of the largest possible region to be
 * 				  managed by firehose.
 *
 * NOTE: firehose_init() will fail at initialization if
 * GASNET_FIREHOSE_M+GASNET_FIREHOSE_MAXVICTIM_M > max_pinnable_memory
 * or if 
 * GASNET_FIREHOSE_R+GASNET_FIREHOSE_MAXVICTIM_R > max_regions
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
extern const firehose_request_t *
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
extern const firehose_request_t *
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
extern const firehose_request_t *
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
extern const firehose_request_t *
firehose_try_remote_pin(gasnet_node_t node, uintptr_t addr, size_t len);

/* firehose_release(requests, num_requests)
 *
 * Called to indicate that use of the indicated 'num_requests'
 * requests for RDMA has completed.  This is a synchronous (blocking)
 * operation.
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

extern const firehose_request_t *
firehose_partial_remote_pin(gasnet_node_t node, uintptr_t addr, size_t len);


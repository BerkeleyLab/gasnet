/* firehose.h: Public Header file */
#ifndef FIREHOSE_H
#define FIREHOSE_H
#include <firehose_fwd.h>
#include <gasnet.h>

struct _firehose_private_t;
typedef struct _firehose_private_t	firehose_private_t;

#if ((defined(FIREHOSE_PAGE) && defined(FIREHOSE_REGION)) || \
    (!defined(FIREHOSE_PAGE) && !defined(FIREHOSE_REGION)) || \
    (defined(FIREHOSE_PAGE) && defined(FIREHOSE_CLIENT_T)))
#error Only define one of FIREHOSE_PAGE or FIREHOSE_REGION.  Make sure \
       FIREHOSE_CLIENT_T is only defined if FIREHOSE_REGION is defined.
#endif

#define FIREHOSE_API_VERSION	0x100

/* The firehose request type is returned as a read-only type from
 * firehose local and remote pin functions.  Based on the address and
 * length requested by the pin operation, this return type describes a
 * region that is a superset of the one requested, namely the start
 * address can be lower and the length of the region can be larger.
 * The returned base address ('addr' field) is always aligned on a
 * page boundary and the length ('len' field) is always a multiple of
 * page size.
 *
 * Request types are allocated on a per-pin request basis, and are
 * freed once the request is released by the client.  Once returned to
 * the client, this type is read-only.  Copies of this type are never
 * kept around in hash tables.  On all the firehose_*_pin functions,
 * clients can pass a pointer to their own allocated request_t.  If
 * this pointer is non-null and the firehose library requires storage
 * in a request_t, the client request_t is used.  If the pointer is
 * null and the firehose library requires storage in a request_t, it
 * is allocated.
 */
typedef
struct _firehose_request_t {
	uint16_t	flags;	/* internal flags -- opaque */

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
 * The address field is always aligned on a page boundary and the
 * length field is a multiple of page size.
 *
 * If the network requires client data to be attached to each pinning
 * operation, the client field should be filled in.
 *
 */
typedef
struct _firehose_region_t {
	uintptr_t	addr;
	size_t		len;

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
	/* Local and remote maximum region sizes that can be requested
	 * through one of the firehose_*_pin functions */
	size_t	max_RemotePinSize;
	size_t	max_LocalPinSize;

	/* Local and remote maximum number of active regions */
	size_t	max_RegionsLocal;
	size_t	max_RegionsRemote;

	/* Local maximum number of buckets that can be pinned */
	size_t  max_FifoBuckets;
}
firehose_info_t;

/* firehose_completed_fn_t
 *
 * Type for function called after firehose placement is acknowledged.
 * The callback is never run within an AM handler context which means
 * the client is free to initiate any communication operation.
 *
 * The callback is thread-safe and firehose makes no guarentees as to
 * what thread the callback is run on.  Clients that require ordering
 * in the completion of RDMA operations will have to implement their
 * own locking mechanism in order to ensure that RDMA operations are
 * completed sequentially.
 */
typedef void (*firehose_completed_fn_t)(void *context, firehose_request_t *req);

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
 * node) in an AMReplyMedium().  Changes to the client type in the
 * region type will be reflected in the request type once the move
 * callback completes.
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
 * This callback is invoked when the firehose library selects one or
 * many regions to be unpinned.  It is up to the client to make sure
 * (possibly by way of a firehose_client_t) that any metadata required
 * to unbind a local node to a remote region is part of the region
 * type.
 */
extern int firehose_unbind_callback(gasnet_node_t node,
				    firehose_region_t *unpin_list,
				    size_t unpin_num);
#endif

#ifdef FIREHOSE_EXPORT_CALLBACK
/* This prototype is for a callback implemented by the CLIENT iff the
 * client defines FIREHOSE_EXPORT_CALLBACK.
 *
 * This callback is invoked by the firehose library when a request to
 * pin a local region is received.  It is up to the client to make
 * sure (possibly by way of a firehose_client_t) that any metadata
 * required to export a region to a remote node is part of the region
 * type.
 */
extern int firehose_export_callback(gasnet_node_t node,
				    firehose_region_t *pin_list,
				    size_t pin_num);
#endif

#ifdef FIREHOSE_UNEXPORT_CALLBACK
/* This prototype is for a callback implemented by the CLIENT iff the
 * client defines FIREHOSE_UNEXPORT_CALLBACK.
 *
 * This callback is invoked by the firehose library when a request to
 * unpin a local region is received.  It is up to the client to make
 * sure (possibly by way of a firehose_client_t) that any metadata
 * required to export a region to a remote node is part of the region
 * type.
 */
extern int firehose_unexport_callback(gasnet_node_t node,
				      firehose_region_t *pin_list,
				      size_t pin_num);
#endif

/* ################################################################ */
/* Firehose initialization and runtime functions
 *
 * The following functions must be used at initialization and
 * termination of the firehose interface, as well as at runtime for
 * the firehose progress engine (firehose_poll()).
 */
/* ################################################################ */

/* firehose_get_handlertable()
 *
 * This function must be called by the client prior to initializing
 * the firehose interface in order to register firehose AM handlers.
 * The function returns an array of gasnet_handlerentry_t terminated
 * with a gasnet_handlerentry_t entry containing a NULL function
 * pointer.
 *
 * Upon calling firehose_get_handlertable(), clients should loop over
 * the array of gasnet_handlerentry_t and fill in a valid
 * gasnet_handler_t index for each function pointer.  At firehose
 * initialization, a check is made to make sure each function pointer
 * has been assigned a useable index number.
 */
extern gasnet_handlerentry_t * firehose_get_handlertable();

/* firehose_init(maximum_pinnable_memory, maximum_regions, info_type)
 *
 * Called to setup the firehose tables and data structures.  This call
 * must be executed once gasnet has been registered the segment and
 * all core and extended AM handlers.  Typically, the firehose init
 * call is done as part of the last step before gasnet_attach's final
 * bootstrap barrier.  Additionally, the client must have registered
 * firehose AM handlers by querying firehose_gethandlers() prior to
 * calling firehose_init().
 *
 * Firehose separates pinning resources using two parameters:
 *   1. The 'maximum_pinnable_memory' is the upper bound for the
 *      firehose 'M' parameter and must be a global minimum of 
 *      the largest amount of memory that can be pinned by each node.
 *      This value should be a fraction of the amount of physical
 *      memory a single process can pin and it is up to the client to
 *      implement a network specific exchange operation to find the
 *      global minimum.
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
firehose_init(uintptr_t max_pinnable_memory, size_t max_regions,
	      firehose_info_t *info);

/* Environment variables used in firehose initialization
 *
 * Although firehose is informed of job-wide resource limitations
 * through its initialization function, users can control firehose
 * parameters through environment variables.
 *
 * Except where noted, the numerical values are assumed to be base-2
 * megabytes.  For these environement variables, a suffix of 'GB' can
 * be appended for (base-2) gigabytes or 'KB' for (base-2) kilobytes 
 * ('MB' will simply be ignored if it is specified).
 *
 * GASNET_FIREHOSE_M establishes, in megabytes, the number of
 *                   firehose buckets each node partitions across all
 *                   nodes.  This is limited by the
 *                   'max_pinnable_memory' parameter.
 *
 * GASNET_FIREHOSE_R establishes, from units in regions, the maximum
 * 		     number of regions each node partitions across all
 * 		     nodes.  This value is ignored if 'max_regions' is
 * 		     0.
 *
 * GASNET_FIREHOSE_MAXVICTIM_M limits, in megabytes, the length of
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
 * GASNET_FIREHOSE_MAXREGION_SIZE limits, in megabytes, the length
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
 * Called to cleanup and terminate firehose.  This call is
 * non-collective and should be called as part of gasnet_exit().
 *
 */
extern void
firehose_fini(void);

/* firehose_poll()
 *
 * Called to make progress on the outstanding DMA requests.  This call
 * is thread-safe and may be called concurrently on different threads
 * (see the comment in the firehose_completed_fn_t discussion about
 * maintaining ordered DMA completions).
 *
 * Implementors should remember to call firehose_poll() after
 * servicing AMReply handlers.
 */
extern void
firehose_poll(void);

/* ################################################################ */
/* Firehose pinning functions
 *
 * All the firehose pinning functions below, denoted as
 * firehose_*_pin, cannot be called from within an AM handler context.
 */
/* ################################################################ */

/* firehose_local_pin(addr, len, request_t)
 *
 * Called to request local pinning of a specified region.
 * This is a synchronous (blocking) operation.
 * 
 * The return value will be non-null on success and may describe a
 * region that is a superset of the one requested, namely the start
 * address can be lower and the length of the region can be larger.
 * The returned request_t pointer can use storage given through a 
 * valid, non-null pointer in the function call or be allocated if 
 * the passed request_t is null.
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
firehose_local_pin(uintptr_t addr, size_t len, firehose_request_t *req);

/* firehose_try_local_pin(addr, len)
 *
 * Called to attempt a local pinning of a specified region.
 * This is a non-blocking operation.
 *
 * If it is known that the region is already pinned, the region's
 * reference count is incremented and the region's request descriptor
 * is returned.  The returned region may be a superset of the one
 * requested, namely the start address can be lower and the length of
 * the region can be larger. The returned request_t pointer can use
 * storage given through a valid, non-null pointer in the function
 * call or be allocated if the passed request_t is null.
 *
 * If the region covered by (addr, addr+len) is not pinned, the
 * function returns NULL.
 */
extern const firehose_request_t *
firehose_try_local_pin(uintptr_t addr, size_t len, firehose_request_t *req);

#if 0 /* temporary remote callback type */
typedef
struct firehose_remote_callback_t {
	void	(*firehose_remote_pin_callback_fn)(gasnet_node_t node, 
		uintptr_t local, uintptr_t remote, size_t nbytes);
	uintptr_t	local_addr;
	uintptr_t	remote_addr;
	size_t		nbytes;
}
firehose_remote_callback_t;
#endif

/* firehose_remote_pin(node, addr, len, callback, context,
 * 		       ret_ifpinned, request_t)
 *
 * Called to request remote pinning of a specified region.  This call
 * always causes an asynchronous callback on the local node except
 * when 'return_if_pinned' is non-null, in which case the call is
 * allowed to return with a descriptor representing the already pinned
 * region if it is known that the region is already pinned.  If the
 * region is not pinned, the call returns 0 and a remote pin request
 * is enqueued.  It is invalid to call this function with the local
 * node number as a destination node.
 *
 * If 'return_if_pinned' is zero, the supplied callback will always be
 * invoked on the local node with the supplied context pointer once
 * the region is pinned on the remote host.  In the event the
 * requested region is already pinned the supplied callback is still
 * called by the firehose library.  However, it is undefined whether
 * that call is made before this call returns, or in what thread it
 * executes (it may be in an AM handler context).
 *
 * The returned request_t pointer can use storage given through a 
 * valid, non-null pointer in the function call or be allocated if 
 * the passed request_t is null.
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
		    int return_if_pinned, firehose_request_t *req);

/* firehose_try_remote_pin(node, addr, len, request_t)
 *
 * Called to request a conditional remote pinning operation in a
 * non-blocking fashion.  The call only returns with a valid request
 * type if the remote region is already pinned.  In any other case,
 * the call returns NULL.  It is invalid to call this function with
 * the local node number as a destination node.
 *
 * The returned request_t pointer can use storage given through a 
 * valid, non-null pointer in the function call or be allocated if 
 * the passed request_t is null.
 *
 * The returned region may be a superset of the one requested, namely
 * the start address can be lower and the length of the region can be
 * larger.
 */
extern const firehose_request_t *
firehose_try_remote_pin(gasnet_node_t node, uintptr_t addr, size_t len,
			firehose_request_t *req);

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
firehose_release(const firehose_request_t **reqs, int numreqs);

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
 * Unlike firehose_try_remote_pin(), this routine may return a region
 * which only partially covers the requested range.  When multiple
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
firehose_partial_remote_pin(gasnet_node_t node, uintptr_t addr, 
			    size_t len, firehose_request_t *req);

#endif

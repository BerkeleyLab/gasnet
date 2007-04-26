/**
 * Common API for SysV messaging
 * ------------------------------------
 * This API allows "virtual networks" (vnets) to be setup within a gasnet
 * shared memory supernode.
 */

/* Some systems (T3E, others?) may not have #defined page size? 
 * - That's my reading of configure.in, at least... */
#if GASNET_PAGESIZE < 4096
  #define GASNETI_SYSVNET_PAGESIZE 4096
#else
  #define GASNETI_SYSVNET_PAGESIZE GASNET_PAGESIZE
#endif

/* Virtual network between processes within a shared
 * memory 'supernode'.  
 * - Implemented as a set of message queues located in a shared memory space
 *   provided by the client of this API.
 * - Conduits using GASNET_SYSV will generally want at least 2 separate
 *   'receive' & 'reply' vnets (to allow a fast implementation of Active
 *   Messages to run within the supernode), and may create others as are
 *   needed or useful.
 */
struct gasneti_sysvnet;			/* opaque type */
typedef struct gasneti_sysvnet gasneti_sysvnet_t;


/* Returns amount of memory needed (rounded up to a multiple of the system
 * page size) needed for a new gasneti_sysvnet_t.
 * - Takes the number of nodes in the gasnet supernode.
 * - Reads the GASNET_SYSVNET_QUEUE_DEPTH and GASNET_SYSVNET_QUEUE_MEMORY
 *   environment variables, if present.
 */
size_t gasneti_sysvnet_memory_needed(gasnet_node_t nodes);

/* Creates a new virtual network within a gasnet shared memory supernode.
 * This function must be called collectively, and with a shared memory region
 * already created that is accessible to all nodes in the supernode
 * - 'start': starting address of region: must be page-aligned
 * - 'len': length of shared region: must be at least as long as the value
 *   returned from gasneti_sysvnet_memory_needed().
 * - 'firstnode': the lowest gasnet_node # in the supernode. 
 * - 'nodes': count of the nodes in the supernode: gasnet node numbers within a
 *   supernode must be continuous, i.e. 'firstnode=4, nodes=3' implies that
 *   nodes 4, 5, and 6 are the members of the supernode.
 */
void gasneti_sysvnet_init(gasneti_sysvnet_t **pvnet, void *start, size_t len, 
                     gasnet_node_t firstnode, gasnet_node_t node_count);

/* returns the maximum size payload that sysvnet can offer.  This is the
 * maximum size one can ask of gasneti_sysvnet_get_send_buffer.  It is
 * typically slightly less than a page in size. */
size_t gasneti_sysvnet_max_payload();

/* Returns send buffer, into which message should be written.  Then
 * deliver_send_buffer() must be called, after which is is not safe to touch the
 * buffer any more.
 * - 'nbytes' must be <= gasneti_sysvnet_max_payload() (typically slightly less
 *   than a page).
 * - Returns NULL if no buffer is available (poll your receive queue, then try
 *   again).
 */
void * gasneti_sysvnet_get_send_buffer(gasneti_sysvnet_t *vnet, size_t nbytes, 
                                       gasnet_node_t target);

/* "Sends" message to target process.
 * Notifies target that message is ready to be received.  After calling, 'buf'
 * logically belongs to the target process, and the caller should not touch
 * the memory pointed to by 'buf' again.
 *
 * Returns nonzero if no message can be sent (message queue full).  Poll your
 * own queues and try again later.
 */
int gasneti_sysvnet_deliver_send_buffer(gasneti_sysvnet_t *vnet, void *buf, size_t nbytes,
                                        gasnet_node_t target);


/* Polls receipt queue, but only for messages from the given sender.
 *
 * Returns nonzero if no message to receive */
int gasneti_sysvnet_recv_from(gasneti_sysvnet_t *vnet, void **pbuf, size_t *psize, 
                              gasnet_node_t sender);

/* Polls receipt queue for any messages from any sender.
 * - 'pbuf': address of pointer which will point to message (if successful)
 * - 'psize': out parameter (msg length will be written into memory)
 * - 'from': out parameter (sender node ID written into memory)
 *
 * returns nonzero if no message to receive */
int gasneti_sysvnet_recv_any(gasneti_sysvnet_t *vnet, void **pbuf, size_t *psize, 
                             gasnet_node_t *from);

/* Called by msg receiver, to release memory after message processed.
 * It is not safe to refer to the memory pointed to by 'buf' after this call
 * is made.
 */
void gasneti_sysvnet_recv_release(gasneti_sysvnet_t *vnet, void *buf); 




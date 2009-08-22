/**
 * Common API for SysV messaging
 * ------------------------------------
 * This API allows "virtual networks" (vnets) to be setup within a gasnet
 * shared memory supernode.
 */
#include <stdarg.h>


#if GASNET_SYSV

/* Some systems (T3E, others?) may not have #defined page size? 
 * - That's my reading of configure.in, at least... */
#if GASNET_PAGESIZE < 4096
  #define GASNETI_SYSVNET_PAGESIZE 4096
#else
  #define GASNETI_SYSVNET_PAGESIZE GASNET_PAGESIZE
#endif

typedef void (*gasneti_handler_fn_t)();
typedef struct gasneti_sysvname_s{
    char file_name[16];
} gasnet_sysvname_t;

extern uintptr_t *gasneti_seginfo_correction;
extern uintptr_t gasneti_sysvsize;
extern gasnet_sysvname_t *gasneti_sysvname;
extern gasnet_sysvname_t gasneti_vnetname;
extern int gasnetc_sysv_init;

extern void gasneti_new_sysv_file(char *filename);
void *gasneti_mmap_vnet(uintptr_t segsize);
gasnet_token_t gasnetc_token_create(gasnet_node_t src, int isRequest);
void gasnetc_token_destroy(gasnet_token_t token);
gasneti_handler_fn_t gasneti_get_handler(int handler_id);


/* Virtual network between processes within a shared
 * memory 'supernode'.  
 * - Implemented as a set of message queues located in a shared memory space
 *   provided by the client of this API.
 */
struct gasneti_sysvnet;			/* opaque type */
typedef struct gasneti_sysvnet gasneti_sysvnet_t;

/* max # of incoming requests per node, per supernode peer */
int gasneti_sysvnet_queue_depth;  
#define GASNETI_SYSVNET_DEFAULT_QUEUE_DEPTH 24
#define GASNETI_SYSVNET_MAX_QUEUE_DEPTH 1024

/* payload memory available for outstanding requests, per node */
uintptr_t gasneti_sysvnet_queue_mem; 
#define GASNETI_SYSVNET_DEFAULT_QUEUE_MEMORY (1<<20)
#define GASNETI_SYSVNET_MAX_QUEUE_MEMORY (1<<28) 


/* data about an incoming message */
typedef struct gasneti_sysvnet_msg {
  void * addr;
  size_t len;
  gasneti_atomic_t ready4receipt;
  /* Paul informs me that padding with GASNETI_CACHE_PAD ensures the struct
   * is sizeof(cache_line), but not that it's aligned on a single cache line.
   * But we enforce cache line alignment, so we're OK */
  #if 1
    char _pad[GASNETI_CACHE_PAD(sizeof(void *)
                               +sizeof(size_t)
                               +sizeof(gasneti_atomic_t))];
  #else
   /* Alternative: pad out the struct to two cache lines, to ensure we'll have
    * no spurious cache line conflicts.  Many vapi structs use this too. */
    char _pad[GASNETI_CACHE_LINE_BYTES];
  #endif
} gasneti_sysvnet_msg_t;


/* Circular queue of info about received messages */
typedef struct gasneti_sysvnet_queue {
  gasneti_sysvnet_msg_t *queue;   
  /* Only need to lock queue ptr if client multithreaded */
  gasneti_mutex_t recv_lock;
  gasneti_sysvnet_msg_t *recv_next;  
  gasneti_mutex_t send_lock;
  gasneti_sysvnet_msg_t *send_next;  
  gasneti_sysvnet_msg_t *justpastlast;  
  #if 1
    /* See above comment about cache alignment: we ensure queue_t's are
     * cache-aligned, too */
    char _pad[GASNETI_CACHE_PAD(sizeof(void *)*4
                               +sizeof(gasneti_mutex_t)*2)];
  #else
    char _pad[GASNETI_CACHE_LINE_BYTES];
  #endif
} gasneti_sysvnet_queue_t;

struct gasneti_sysvnet_allocator;  /* forward definition */

/* message payload metadata
 */
typedef struct gasneti_sysvnet_payload_info {
  gasneti_sysvnet_msg_t *msg;
  struct gasneti_sysvnet_allocator *allocator;
} gasneti_sysvnet_payload_info_t;

/* Max payload size: make sure this is kept in sync with definition
 * of gasneti_sysvnet_allocator_block_t */
#define GASNETI_SYSVNET_MAX_PAYLOAD \
        (17*GASNETI_SYSVNET_PAGESIZE - (2*sizeof(gasneti_sysvnet_payload_info_t)))

#define round_up_to_sysvpage(size_or_addr)               \
        GASNETI_ALIGNUP(size_or_addr, GASNETI_SYSVNET_PAGESIZE)

#define sysvnet_get_struct_addr_from_field_addr(structname, fieldname, fieldaddr) \
        ((structname*)(((char *)fieldaddr) - (char *)(&((structname *)0)->fieldname)))

typedef struct gasneti_sysvnet_payload {
  gasneti_sysvnet_payload_info_t info;
  char payload[GASNETI_SYSVNET_MAX_PAYLOAD];
} gasneti_sysvnet_payload_t;

gasneti_sysvnet_t *gasneti_request_sysvnet, *gasneti_reply_sysvnet;
/*******************************************************************************
 * <SysV variables that must be initialized by the conduit using SYSV>
 */
/*  Sysvnets needed for SysV active messages.
 *
 * - Conduits using GASNET_SYSV must initialize these two vnets
 *   to allow a fast implementation of Active Messages to run within the
 *   supernode.  Other vnets may be created as are needed or useful.
 * - Initialize these vnets before use via gasneti_sysvnet_init().
 */
/* # of nodes in my supernode, lowest of contiguous gasnet node #s in
 * supernode, and my 0-based rank within it */
extern gasnet_node_t gasneti_sysvnodes;
extern gasnet_node_t gasneti_firstsysvnode;
extern gasnet_node_t gasneti_mysysvnode;

/*
 * </SysV variables that must be initialized by the conduit using SYSV>
 *******************************************************************************/

/* Returns 1 if given node is in the caller's supernode, or 0 if it's not. */
GASNETI_INLINE(gasneti_sysvnet_in_supernode)
int gasneti_sysv_in_supernode(gasnet_node_t node) {
  int index = node - gasneti_firstsysvnode;
  return (index >= 0 && index < gasneti_sysvnodes);
}

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

/* Bootstrap exchange via sysvnet.
 *
 * This function has the following restrictions:
 * 1) It must be called after gasneti_sysvnet_init() has completed.
 * 2) It must be called collectively by all nodes in the vnet.
 * 3) The conduit's bootstrap_barrier function must be called before further
 *    calls to this function (or gasneti_sysvnet_bootstrap_Exchange) are made.
 */
void gasneti_sysvnet_bootstrapExchange(gasneti_sysvnet_t *vnet, void *src, 
                                       size_t len, void *dest);

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


/* Polls receipt queue for any messages from any sender.
 * - 'pbuf': address of pointer which will point to message (if successful)
 * - 'psize': out parameter (msg length will be written into memory)
 * - 'from': out parameter (sender node ID written into memory)
 *
 * returns nonzero if no message to receive */
int gasneti_sysvnet_recv(gasneti_sysvnet_t *vnet, void **pbuf, size_t *psize, 
                         gasnet_node_t *from);

/* Called by msg receiver, to release memory after message processed.
 * It is not safe to refer to the memory pointed to by 'buf' after this call
 * is made.
 */
void gasneti_sysvnet_recv_release(gasneti_sysvnet_t *vnet, void *buf); 

/*******************************************************************************
 * AMSYSV: Active Messages over Sysvnet
 *******************************************************************************/

/* Processes pending messages:  if 'repliesOnly', only checks the 'reply'
 * SYSV network (i.e. gasneti_reply_sysvnet).  */
extern int gasneti_AMSYSVPoll(int repliesOnly);

/* Don't call this function directly: internal sysv function */
int gasnetc_AMSYSV_ReqRepGeneric(int category, int isReq, int dest,
                                 gasnet_handler_t handler, void *source_addr, int nbytes, 
                                 void *dest_ptr, int numargs, va_list argptr);

/* Generic AM handler for SysVnet.
 * Divert your conduit's regular AM requests to this function if a call to
 * gasneti_sysv_in_supernode(dest) is nonzero */ 
GASNETI_INLINE(gasneti_AMSYSV_RequestGeneric)
int gasneti_AMSYSV_RequestGeneric(int category, int dest, 
                                  gasnet_handler_t handler, void *source_addr, int nbytes,
                                  void *dest_ptr, int numargs, va_list argptr) 
{
  return gasnetc_AMSYSV_ReqRepGeneric(category, 1, dest, handler, source_addr,
                                      nbytes, dest_ptr, numargs, argptr); 
}

/* Generic AM handler for SysVnet.
 * Divert your conduit's regular AM replies to this function if a call to
 * gasneti_sysv_in_supernode(dest) is nonzero */ 
GASNETI_INLINE(gasneti_AMSYSV_ReplyGeneric)
int gasneti_AMSYSV_ReplyGeneric(int category, gasnet_token_t token, 
                                       gasnet_handler_t handler, void *source_addr, 
                                       int nbytes, void *dest_ptr, int numargs, 
                                       va_list argptr) 
{
  int retval;
  gasnet_node_t sourceid;
  gasnetc_AMGetMsgSource(token, &sourceid);
  retval = gasnetc_AMSYSV_ReqRepGeneric(category, 0, sourceid, handler, source_addr, 
                                        nbytes, dest_ptr, numargs, argptr); 
  return retval;
}
#endif /*GASNET_SYSV*/

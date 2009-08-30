/*   $Source: /Users/kamil/work/gasnet-cvs2/gasnet/Attic/gasnet_sysv.h,v $
 *     $Date: 2009/08/30 20:42:51 $
 * $Revision: 1.1.4.30 $
 * Description: GASNet infrastructure for shared memory communications
 * Copyright 2009, E. O. Lawrence Berekely National Laboratory
 * Terms of use are as specified in license.txt
 */

#ifndef _GASNET_SYSV_H
#define _GASNET_SYSV_H

#ifndef GASNET_SYSV
  #error "gasnet_sysv.h included in a non-PSHM build"
#endif

#include <gasnet_handler.h> /* Need gasneti_handler_fn_t */

/* Some systems (T3E, others?) may not have #defined page size? 
 * - That's my reading of configure.in, at least... */
#if GASNET_PAGESIZE < 4096
  #define GASNETI_SYSVNET_PAGESIZE 4096
#else
  #define GASNETI_SYSVNET_PAGESIZE GASNET_PAGESIZE
#endif

/* In gasnet_mmap.c */
#define GASNETI_SYSV_UNIQUE_LEN 6
extern const char *gasneti_sysv_makenames(const char *unique);
extern void *gasneti_mmap_vnet(uintptr_t segsize);
extern void gasneti_unlink_vnet(void);

/* Virtual network between processes within a shared
 * memory 'supernode'.  
 * - Implemented as a set of message queues located in a shared memory space
 *   provided by the client of this API.
 */
struct gasneti_sysvnet;			/* opaque type */
typedef struct gasneti_sysvnet gasneti_sysvnet_t;

/* Initialize sysv request and reply networks given a conduit-specific exchange function */
extern void gasneti_init_sysv(gasneti_bootstrapExchangefn_t exchangefn);
extern gasneti_sysvnet_t *gasneti_request_sysvnet;
extern gasneti_sysvnet_t *gasneti_reply_sysvnet;


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

/* Returns 1 if given node is in the caller's supernode, or 0 if it's not.
 * NOTE: result is false if !gasneti_sysvnodes (e.g. before vnet initialization)
 * TODO: This implementation is only correct when gasnet node numbers
 *       within a supernode are contiguous.
 */
GASNETI_INLINE(gasneti_sysvnet_in_supernode)
int gasneti_sysv_in_supernode(gasnet_node_t node) {
  /* NOTE: gasnet_node_t is an unsigned type, so in the case of
   * (node < gasneti_firstsysvnode), the subtraction will wrap to
   * a "large" value and the result of "<" is the required FALSE.
   */
  gasnet_node_t diff = (node - gasneti_firstsysvnode);
  int retval = (diff < gasneti_sysvnodes);

  gasneti_assert(!retval || (node >= gasneti_firstsysvnode));
  gasneti_assert(!retval || (node < (gasneti_firstsysvnode + gasneti_sysvnodes)));
  return retval;
}

/* Returns amount of memory needed (rounded up to a multiple of the system
 * page size) needed for a new gasneti_sysvnet_t.
 * - Takes the number of nodes in the gasnet supernode.
 * - Reads the GASNET_SYSVNET_QUEUE_DEPTH and GASNET_SYSVNET_QUEUE_MEMORY
 *   environment variables, if present.
 */
extern size_t gasneti_sysvnet_memory_needed(gasnet_node_t nodes);

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

/* Bootstrap barrier via sysvnet.
 *
 * This function has the following restrictions:
 * 1) It must be called after gasneti_sysvnet_init() has completed.
 * 2) It must be called collectively by all nodes in the vnet.
 */
void gasneti_sysvnet_bootstrapBarrier(void);

/* Bootstrap broadcast via sysvnet.
 *
 * This function has the following restrictions:
 * 1) It must be called after gasneti_sysvnet_init() has completed.
 * 2) It must be called collectively by all nodes in the vnet.
 * 3) The rootsysvnode is the supernode-local rank
 */
void gasneti_sysvnet_bootstrapBroadcast(gasneti_sysvnet_t *vnet, void *src, 
                                        size_t len, void *dest, int rootsysvnode);

/* Bootstrap exchange via sysvnet.
 *
 * This function has the following restrictions:
 * 1) It must be called after gasneti_sysvnet_init() has completed.
 * 2) It must be called collectively by all nodes in the vnet.
 */
void gasneti_sysvnet_bootstrapExchange(gasneti_sysvnet_t *vnet, void *src, 
                                       size_t len, void *dest);

/* returns the maximum size payload that sysvnet can offer.  This is the
 * maximum size one can ask of gasneti_sysvnet_get_send_buffer.
 */
size_t gasneti_sysvnet_max_payload();

/* Returns send buffer, into which message should be written.  Then
 * deliver_send_buffer() must be called, after which is is not safe to touch the
 * buffer any more.
 * - 'nbytes' must be <= gasneti_sysvnet_max_payload().
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
                                 void *dest_addr, int numargs, va_list argptr);

/* Generic AM handler for SysVnet.
 * Divert your conduit's regular AM requests to this function if a call to
 * gasneti_sysv_in_supernode(dest) is nonzero */ 
GASNETI_INLINE(gasneti_AMSYSV_RequestGeneric)
int gasneti_AMSYSV_RequestGeneric(int category, int dest, 
                                  gasnet_handler_t handler, void *source_addr, int nbytes,
                                  void *dest_addr, int numargs, va_list argptr) 
{
  return gasnetc_AMSYSV_ReqRepGeneric(category, 1, dest, handler, source_addr,
                                      nbytes, dest_addr, numargs, argptr); 
}

/* Generic AM handler for SysVnet.
 * Divert your conduit's regular AM replies to this function if a call to
 * gasneti_sysv_in_supernode(dest) is nonzero */ 
GASNETI_INLINE(gasneti_AMSYSV_ReplyGeneric)
int gasneti_AMSYSV_ReplyGeneric(int category, gasnet_token_t token, 
                                       gasnet_handler_t handler, void *source_addr, 
                                       int nbytes, void *dest_addr, int numargs, 
                                       va_list argptr) 
{
  int retval;
  gasnet_node_t sourceid;
  gasnetc_AMGetMsgSource(token, &sourceid);
  retval = gasnetc_AMSYSV_ReqRepGeneric(category, 0, sourceid, handler, source_addr, 
                                        nbytes, dest_addr, numargs, argptr); 
  return retval;
}



/* Optional conduit-specific code if defaults in gasnet_sysv.c are not usable.
 * Conduits providing these should #define the appropriate token in gasnet_core_fwd.h
 * When a given token is NOT defined, internal default implementations are used.
 */
#ifdef GASNETC_GET_HANDLER
  extern gasneti_handler_fn_t gasnetc_get_handler(gasnet_handler_t handler);
#endif
#ifdef GASNETC_TOKEN_CREATE
  extern gasnet_token_t gasnetc_token_create(gasnet_node_t src, int isRequest);
  extern void gasnetc_token_destroy(gasnet_token_t token);
#else
    /* Conduits using the default gasnetc_token_create() will
     * want/need to use this in their gasnetc_AMGetMsgSource().
     * Returns GASNET_OK if token was recognized, GASNET_ERR_BAD_ARG otherwise.
     */
    GASNETI_INLINE(gasneti_AMSYSVGetMsgSource)
    int gasneti_AMSYSVGetMsgSource(gasnet_token_t token, gasnet_node_t *src_ptr) {
      int retval = GASNET_ERR_BAD_ARG;
      if ((uintptr_t)token & 1) {
        gasnet_node_t tmp = gasneti_firstsysvnode + (gasnet_node_t)((uintptr_t)token >> 1);
        gasneti_assert(gasneti_sysv_in_supernode(tmp));
        *src_ptr = tmp;
        retval = GASNET_OK;
      }
      return retval;
    }
#endif

#endif /* _GASNET_SYSV_H */

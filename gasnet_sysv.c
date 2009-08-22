/*   $Source: /Users/kamil/work/gasnet-cvs2/gasnet/Attic/gasnet_sysv.c,v $
 *     $Date: 2009/08/22 04:34:35 $
 * $Revision: 1.1.4.11 $
 * Description: GASNet infrastructure for shared memory communications
 * Copyright 2007, Dan Bonachea <bonachea@cs.berkeley.edu>
 * Terms of use are as specified in license.txt
 */

#include <gasnet_internal.h>


#if GASNET_SYSV

static void *gasnetc_sysvnet_region;

/* maximum number of processes that share a single shared memory region */
 #define GASNETC_MAX_SYSV_NODES 256

/* Supernode data that lives in shared space */
struct gasnetc_supernode_info_t {
  gasnet_node_t node2pid[GASNETC_MAX_SYSV_NODES]; /* pid lookup table */
  gasneti_atomic_t startup_counter;		    /* one-time barrier */
};
static struct gasnetc_supernode_info_t *gasnetc_sn_info;
gasneti_mutex_t gasneti_index_lock;

#define gasneti_sysv_node2pid gasnetc_sn_info->node2pid



void gasnetc_init_sysv(){
  size_t vnetsz, sninfosz, mmapsz;
  int retval = GASNET_OK;
  int i, sysv_nodes = 0, myrank = 0;

  
#if GASNET_CONDUIT_SMP_SYSV     
  gasneti_sysvnodes = gasneti_nodes;
  gasneti_firstsysvnode = 0;
  gasneti_mysysvnode = gasneti_mynode;
#else
  gasneti_sysvnodes = gasneti_nodemap_local_count;
  gasneti_firstsysvnode = gasneti_nodemap[gasneti_mynode];
  gasneti_mysysvnode = gasneti_nodemap_local_rank;
#endif


  /* set up additional shared memory region for shared supernode data and AM
   * infrastructure.
   */
  vnetsz = gasneti_sysvnet_memory_needed(gasneti_sysvnodes); 
  sninfosz = sizeof(struct gasnetc_supernode_info_t);
  sninfosz = GASNETI_ALIGNUP(sninfosz, GASNETI_SYSVNET_PAGESIZE);
  mmapsz = sninfosz + (2*vnetsz);
  /* NOTE: do we need gasneti_sysvsize (is it ever used)? */
  gasneti_sysvsize = sninfosz + (2*vnetsz);
  /* NOTE: What happens here if there is not enough memory to alloc vnet? */
  gasnetc_sysvnet_region = gasneti_mmap_vnet(mmapsz);
  if (gasnetc_sysvnet_region == NULL) //MAP_FAILED)
    gasneti_fatalerror("mmap for shared memory Active Messages region failed!");
  
  /* Initializing supernode info. NOTE: I am not sure if we really need node2pid ... */
  gasnetc_sn_info = (struct gasnetc_supernode_info_t *)gasnetc_sysvnet_region;
  if (gasneti_mynode==0) memset(gasnetc_sn_info, 0, sizeof(struct gasnetc_supernode_info_t));
  else sleep(1);
  gasneti_sysv_node2pid[0] = getpid();
  gasneti_mutex_init(&gasneti_index_lock);

  /* Collective call to initialize Shared AM "networks" */
  gasneti_sysvnet_init(&gasneti_request_sysvnet, ((char*)(gasnetc_sysvnet_region))+sninfosz,
                       vnetsz, gasneti_firstsysvnode, gasneti_sysvnodes);
  gasneti_sysvnet_init(&gasneti_reply_sysvnet, ((char*)(gasnetc_sysvnet_region))+(sninfosz+vnetsz),
                       vnetsz, gasneti_firstsysvnode, gasneti_sysvnodes);

  /* One-time 'barrier' */
  gasneti_atomic_increment(&gasnetc_sn_info->startup_counter, GASNETI_ATOMIC_REL);
  while (gasneti_atomic_read(&gasnetc_sn_info->startup_counter, GASNETI_ATOMIC_ACQ) 
            != gasneti_sysvnodes)
    gasneti_sched_yield();
    

}

/*******************************************************************************
 * "SysV Net":  virtual network between peers in a shared memory supernode 
 ******************************************************************************/
/* # of nodes in my supernode, lowest of contiguous gasnet node #s in
 * supernode, and my 0-based rank within it */
gasnet_node_t gasneti_sysvnodes;
gasnet_node_t gasneti_firstsysvnode;
gasnet_node_t gasneti_mysysvnode;

size_t gasneti_sysvnet_max_payload() {
  return GASNETI_SYSVNET_MAX_PAYLOAD;
}

/******************************************************************************
 * Payload memory allocator interface.
 *
 * Keep the payload allocator interface clean, so we can replace the current
 * algorithm (circular queue of fixed-length buffers) with something else
 * (like a full-blown memory allocator) if needed.
 * - It should be mainly a matter of autotools goopery to replace this
 *   implementation with umalloc, should that prove to be a good idea.
 * - Naming note: 'allocator' = "payload allocator", just like a "J. Lo"
 *   allocator would would be called "jallocator".  Isn't that obvious?
 ******************************************************************************/

/* Memory layout of allocator.
 *
 * Logically, we have this layout:
 *  
 *    1) Atomic used by allocator as 'in use' bit
 *    2) A gasnet_sysvnet_payload_t struct, used by sysvnet
 *    3) The rest of the page-sized, for payload.
 *
 * Suboptimal hack: I don't know my alignment niceties well enough, so I'm
 * having the allocator treat the space as
 *  
 *    1) A gasnet_sysvnet_payload_t sized-space, which I cast to a (definitely
 *       smaller) atomic_t for use as the 'in_use' bit.
 *    2) A second gasnet_sysvnet_payload_t, used by sysvnet.  This is the
 *       'start' of the memory returned by the allocator, as sysvent sees it.
 *    3) A third gasnet_sysvnet_payload_t, which is cast to 'void *', and is
 *       the start of the payload.
 */ 

/* sizeof(gasneti_sysvnet_allocator_block) == GASNETI_SYSVNET_PAGESIZE 
 * - if not, adjust definition of GASNETI_SYSVNET_MAX_PAYLOAD */
typedef struct gasneti_sysvnet_allocator_block {
  union {
    gasneti_atomic_t in_use;
    gasneti_sysvnet_payload_info_t padding;
  } alignmentFun; 
  gasneti_sysvnet_payload_t payload_t;
} gasneti_sysvnet_allocator_block_t;

/* This implementation uses a circular queue of fixed-size payloads */
typedef struct gasneti_sysvnet_allocator {
  gasneti_sysvnet_allocator_block_t *queue;
  gasneti_mutex_t next_lock;    /* only locked by owning process */
  gasneti_sysvnet_allocator_block_t *next;
  gasneti_sysvnet_allocator_block_t *justpastlast;
  char _pad[GASNETI_CACHE_LINE_BYTES];
} gasneti_sysvnet_allocator_t;

/* WARNING: the amount requested from this allocator must be less than 
 * sizeof(gasneti_sysvnet_payload_t)
 * - returns NULL if no memory available
 */
static gasneti_sysvnet_allocator_t *gasneti_sysvnet_init_allocator(void *region, size_t len);
static void * gasneti_sysvnet_alloc(gasneti_sysvnet_allocator_t *a, size_t nbytes);
/* Frees memory.  Note that this must be callable by a different node */
static void gasneti_sysvnet_free(gasneti_sysvnet_allocator_t *a, void *p);

/******************************************************************************
 * </Payload memory allocator interface>
 ******************************************************************************/

/* Per node view of 'network' of queues in supernode 
 * - Note that this struct itself is not stored in shared memory. 
 */
struct gasneti_sysvnet {
  gasnet_node_t firstnode;          /* first gasnet node in this supernode */
  gasnet_node_t nodecount;          /* nodes in supernode */ 
  gasnet_node_t nextindex;          /* index of next node to check for msgs */
  /* my 'in' queues are other nodes' 'out' queues */
  gasneti_sysvnet_queue_t **in_queues;
  gasneti_sysvnet_queue_t **out_queues;
  /* only need to see one's own allocator */
  gasneti_sysvnet_allocator_t *my_allocator;
};

#define sysvnode(vnet, gasnet_node) \
        (gasnet_node - vnet->firstnode)

#define gasneti_assert_align(p, align) \
        gasneti_assert((((uintptr_t)p) % align) == 0)

/* Macros for determining the offset and the real address, used for
 * the addresses inside the sysnet region */
#define gasneti_sysv_offset(addr) \
                (void *)((uintptr_t)addr - (uintptr_t)gasnetc_sysvnet_region)

#define gasneti_sysv_addr(addr) \
                (void *)((uintptr_t)addr + (uintptr_t)gasnetc_sysvnet_region)


static int get_queue_depth(gasnet_node_t nodes) 
{
  int val = gasneti_getenv_int_withdefault("GASNET_SYSVNET_QUEUE_DEPTH", GASNETI_SYSVNET_DEFAULT_QUEUE_DEPTH, 0);
  if (val > GASNETI_SYSVNET_MAX_QUEUE_DEPTH) {
    fprintf(stderr, "GASNET_SYSVNET_QUEUE_DEPTH (%d) larger than max: using max (%d)\n",
            val, GASNETI_SYSVNET_MAX_QUEUE_DEPTH);
    val = GASNETI_SYSVNET_MAX_QUEUE_DEPTH;
  } else if (val < nodes) {
    /* ensure queue depth >= nodes, so that lazily-written 
     * gasneti_sysvnet_bootstrapExchange() function doesn't deadlock */
    fprintf(stderr, "GASNET_SYSVNET_QUEUE_DEPTH (%d) < than SysV nodes (%d): using %d\n",
            val, nodes, nodes);
    val = nodes;
  }
  return val;
}

static uintptr_t get_queue_mem(int nodes) 
{
  /* theoretical limit = 1 send buffer per peer? 
   * - future implementations may also need some space for allocator's metadata */
  size_t minsize = GASNETI_SYSVNET_PAGESIZE*nodes*2;
  uintptr_t pernode = gasneti_getenv_int_withdefault("GASNET_SYSVNET_QUEUE_MEMORY", 
                    GASNETI_SYSVNET_DEFAULT_QUEUE_MEMORY, 1<<20);
  if (pernode > GASNETI_SYSVNET_MAX_QUEUE_MEMORY) {
    fprintf(stderr, "GASNET_SYSVNET_QUEUE_MEMORY (%ld) larger than max: using max (%ld)\n",
            (long)pernode, (long)GASNETI_SYSVNET_MAX_QUEUE_MEMORY);
    pernode = GASNETI_SYSVNET_MAX_QUEUE_MEMORY;
  } else if (pernode < minsize) {
    fprintf(stderr, "GASNET_SYSVNET_QUEUE_MEMORY (%ld) smaller than min: using min (%ld)\n",
            (long)pernode, (long)minsize);
    pernode = minsize;
  }
  gasneti_assert(pernode > 0);

  /* round up to multiple of payload size */
  if ( (pernode % sizeof(gasneti_sysvnet_payload_t)) != 0)
    pernode = ((pernode / sizeof(gasneti_sysvnet_payload_t)) + 1) 
                * sizeof(gasneti_sysvnet_payload_t);
  return pernode;
}

static size_t gasneti_sysvnet_memory_needed_pernode(gasnet_node_t nodes)
{
  size_t size = 0;

  gasneti_sysvnet_queue_depth = get_queue_depth(nodes);
  gasneti_sysvnet_queue_mem = get_queue_mem(nodes);

  /* Message infos and queue */
  size = sizeof(gasneti_sysvnet_queue_t)*(nodes);
  size += sizeof(gasneti_sysvnet_msg_t)*gasneti_sysvnet_queue_depth*(nodes-1);
  size = round_up_to_sysvpage(size);
  size += sizeof(gasneti_sysvnet_allocator_t);
  size = round_up_to_sysvpage(size);
  size += gasneti_sysvnet_queue_mem;  
  size = round_up_to_sysvpage(size);

  return size;
}

size_t gasneti_sysvnet_memory_needed(gasnet_node_t nodes)
{
  return gasneti_sysvnet_memory_needed_pernode(nodes) * nodes;
}

#define SYSV_OFFSET
#ifdef SYSV_OFFSET

static void init_queue(gasneti_sysvnet_queue_t *q, gasneti_sysvnet_msg_t *msgs,
                       int depth)
{
  int i;

  /* Instead of using the "real" msgs address, we use only the offset with respect to the beginning of
   * the mapping */
  q->queue = q->recv_next = q->send_next = (gasneti_sysvnet_msg_t *)gasneti_sysv_offset(msgs);
  q->justpastlast = (gasneti_sysvnet_msg_t *)gasneti_sysv_offset(msgs) + depth;
  gasneti_mutex_init(&q->recv_lock);
  gasneti_mutex_init(&q->send_lock);

  for (i = 0; i < depth; i++) {
    gasneti_atomic_set(&msgs->ready4receipt, 0, 0);
    msgs++;
  }

}
#else

static void init_queue(gasneti_sysvnet_queue_t *q, gasneti_sysvnet_msg_t *msgs,
                       int depth)
{
  int i;

  q->queue = q->recv_next = q->send_next = msgs;
  q->justpastlast = msgs + depth;
  gasneti_mutex_init(&q->recv_lock);
  gasneti_mutex_init(&q->send_lock);

  //printf("%d> init_queue address q->send_next %u, q->send_next->ready4receipt %u q->send_next->ready4receipt.ctr %u\n",gasneti_mysysvnode,&q->send_next, &q->send_next->ready4receipt, &q->send_next->ready4receipt.ctr);
  for (i = 0; i < depth; i++) {
    gasneti_atomic_set(&msgs->ready4receipt, 0, 0);
    msgs++;
  }
}

#endif

static void gasneti_sysvnet_init_my_sysv(gasneti_sysvnet_t *pvnet, char * myregion, 
                                  gasnet_node_t firstnode, gasnet_node_t nodes)
{
  int i;
  gasneti_sysvnet_queue_t *myqueues;
  gasneti_sysvnet_msg_t   *mymsgs, *remote_mymsgs;
  void *alloc_region;
  gasneti_assert_align(myregion, GASNETI_SYSVNET_PAGESIZE);

  /* NOTE: other init code relies on queues being at start of region */
  myqueues = (gasneti_sysvnet_queue_t *)myregion;
  mymsgs = (gasneti_sysvnet_msg_t *)(((char*)myqueues) 
                                      + sizeof(gasneti_sysvnet_queue_t)*nodes);  
  gasneti_assert_align(mymsgs, GASNETI_CACHE_LINE_BYTES);
  alloc_region = ((char*)mymsgs) + 
        sizeof(gasneti_sysvnet_msg_t)*gasneti_sysvnet_queue_depth*(nodes-1);
  alloc_region = (void *)round_up_to_sysvpage(alloc_region);

  for (i = 0; i < nodes; i++) {
    if (i == gasneti_mysysvnode) {
      memset(&myqueues[i], 0, sizeof(gasneti_sysvnet_queue_t));
    } else {
      init_queue(&myqueues[i], mymsgs, gasneti_sysvnet_queue_depth);
      mymsgs += gasneti_sysvnet_queue_depth;
    }
  }
  pvnet->my_allocator = 
    gasneti_sysvnet_init_allocator(alloc_region, gasneti_sysvnet_queue_mem);

  pvnet->nextindex = 0;
}

/* Initializes the sysvnet region. Called from each node twice: 
 * to initialize sysvnet_request and sysvnet_reply */
void gasneti_sysvnet_init(gasneti_sysvnet_t **pvnet, void *start, size_t nbytes, 
                          gasnet_node_t firstnode, gasnet_node_t sysvnodes)
{
  gasneti_sysvnet_t *vnet;
  gasnet_node_t i, othernode;
  size_t szpernode, regionlen;
  void *region, *myregion;

  /* make sure that our max buffer size isn't smaller than whatever network
   * is being used */
  gasneti_assert(GASNETC_MAX_MEDIUM_SYSV < GASNETI_SYSVNET_MAX_PAYLOAD);

  region = start;
  region = (void *)round_up_to_sysvpage(region);

  regionlen = nbytes - ( ((uintptr_t)region)-((uintptr_t)start));
  szpernode = gasneti_sysvnet_memory_needed_pernode(sysvnodes);
  if (regionlen < szpernode * sysvnodes) 
    gasneti_fatalerror("Internal error: not enough memory for sysvnet: \n"
                       " given %lu effective bytes, but need %lu", 
                       (unsigned long)regionlen, (unsigned long)(szpernode * sysvnodes));
  vnet = gasneti_malloc(sizeof(gasneti_sysvnet_t));
  vnet->firstnode = firstnode;
  vnet->nodecount = sysvnodes;
  myregion = (void *)( ((uintptr_t)region) + (szpernode*gasneti_mysysvnode));
  /* collective call, so each process inits its own region.
   * To allow non-fixed mapping of the sysvnet memory, we initialize
   * each reqion using the offset-addresses */
  gasneti_sysvnet_init_my_sysv(vnet, myregion, firstnode, sysvnodes);

  /* initialize queue pointers */
  vnet->in_queues = gasneti_malloc(sizeof(gasneti_sysvnet_queue_t*)*sysvnodes);
  vnet->out_queues = gasneti_malloc(sizeof(gasneti_sysvnet_queue_t*)*sysvnodes);
  for (i = 0; i < sysvnodes; i++) {
    vnet->in_queues[i] = ((gasneti_sysvnet_queue_t *)myregion) + i;
    vnet->out_queues[i] = ((gasneti_sysvnet_queue_t *) (((uintptr_t)region)+(szpernode*i))) 
                          + gasneti_mysysvnode;
  }

  *pvnet = vnet;
}

void * gasneti_sysvnet_get_send_buffer(gasneti_sysvnet_t *vnet, size_t nbytes, 
                                       gasnet_node_t target)
{
  gasneti_sysvnet_payload_t *p;
  void *retval = NULL;
  
  gasneti_assert(nbytes <= GASNETI_SYSVNET_MAX_PAYLOAD);

  p = gasneti_sysvnet_alloc(vnet->my_allocator, sizeof(gasneti_sysvnet_payload_t));
  if (p != NULL) {
    p->info.msg = NULL;
    p->info.allocator = vnet->my_allocator;
    retval = p->payload;
  }
  
  return retval;
}


#ifdef SYSV_OFFSET

int gasneti_sysvnet_deliver_send_buffer(gasneti_sysvnet_t *vnet, void *buf, 
                                        size_t nbytes, gasnet_node_t target)
{
  int retval = -1;
  gasneti_sysvnet_msg_t *q_send_next;
  gasneti_sysvnet_msg_t *q_queue;
  gasneti_sysvnet_msg_t *q_justpastlast;
  gasneti_sysvnet_payload_t *p;
  gasneti_sysvnet_queue_t *q = vnet->out_queues[sysvnode(vnet, target)];
  gasneti_assert(q != NULL);

  gasneti_mutex_lock(&q->send_lock);
  
  /* Get the actuall addresses of q->send_next, queue and justpastlast (they are currently
   * only offsets) */
  q_send_next = (gasneti_sysvnet_msg_t *)gasneti_sysv_addr(q->send_next);
  q_queue = (gasneti_sysvnet_msg_t *)gasneti_sysv_addr(q->queue);
  q_justpastlast = (gasneti_sysvnet_msg_t *)gasneti_sysv_addr(q->justpastlast);

   /* This code assumes that if the current 'send_node' isn't free yet, there
   * are no free slots in the recipient's queue.  Since there is only one
   * sender, one receiver, and the receiver consumes messages in order, this
   * should be true, so no scan over the list is needed. */
  if (!gasneti_atomic_read(&q_send_next->ready4receipt, 0)) {
    retval = 0;
    /* fill in message info. Instead of buf we use offset since it
     * will be read by another node wich does not have identical sysvnet
     * memory mapping */
    q_send_next->addr = gasneti_sysv_offset(buf);
    q_send_next->len = nbytes;
    /* set pointer to msg in buffer */
    p = sysvnet_get_struct_addr_from_field_addr(gasneti_sysvnet_payload_t, payload, buf);
    gasneti_assert(buf == &p->payload);
    p->info.msg = gasneti_sysv_offset(q_send_next);
    /* Perform write flush before writing ready bit */
    gasneti_atomic_set(&q_send_next->ready4receipt, 1, GASNETI_ATOMIC_REL);
    if (++q_send_next == q_justpastlast)
      q_send_next = q_queue;
  }
 
  /* If q_send_next was changed, set the offset value in q->send_next to
   * mach the changed value */
  q->send_next = (gasneti_sysvnet_msg_t *)gasneti_sysv_offset(q_send_next);
  gasneti_mutex_unlock(&q->send_lock);

  return retval;
}


int gasneti_sysvnet_recv(gasneti_sysvnet_t *vnet, void **pbuf, size_t *psize, 
                         gasnet_node_t *from)
{
  int i, nextindex;
  gasneti_sysvnet_msg_t *q_recv_next;
  gasneti_sysvnet_msg_t *q_queue;
  gasneti_sysvnet_msg_t *q_justpastlast;
   
  gasneti_mutex_lock(&gasneti_index_lock);
  nextindex = vnet->nextindex;
  gasneti_mutex_unlock(&gasneti_index_lock);

  /* We could try using i instead of vnet->nextindex
   * but that would influence the fairness. Not sure
   * what is better ... */
  for (i = 0; i < vnet->nodecount; i++) {
    if (nextindex != gasneti_mysysvnode) {
      gasneti_sysvnet_queue_t *q = vnet->in_queues[nextindex];
      
      gasneti_assert(q != NULL);
      gasneti_mutex_lock(&q->recv_lock);
      /* Get the actuall addresses of q->recv_next, queue and justpastlast (they are currently
       * only offsets) */
      q_recv_next = (gasneti_sysvnet_msg_t *)gasneti_sysv_addr(q->recv_next);
      q_queue = (gasneti_sysvnet_msg_t *)gasneti_sysv_addr(q->queue);
      q_justpastlast = (gasneti_sysvnet_msg_t *)gasneti_sysv_addr(q->justpastlast);
      
      if (gasneti_atomic_read(&q_recv_next->ready4receipt, GASNETI_ATOMIC_ACQ)) {
        /* Transform the offset in q_recv_next->addr
         * into a real address */
        *pbuf = gasneti_sysv_addr(q_recv_next->addr);
        *psize = q_recv_next->len;
        if (++q_recv_next == q_justpastlast)
          q_recv_next = q_queue;
        *from = nextindex + vnet->firstnode;
     
        /* If q_recv_next was changed, set the offset value in q->send_next to
         * mach the changed value */
        q->recv_next = (gasneti_sysvnet_msg_t *)gasneti_sysv_offset(q_recv_next);
        gasneti_mutex_unlock(&q->recv_lock);

        /* Ensure fairness: next check starts with next node */
        if (++nextindex == vnet->nodecount) 
          nextindex = 0;

        gasneti_mutex_lock(&gasneti_index_lock);
        vnet->nextindex=nextindex;
        gasneti_mutex_unlock(&gasneti_index_lock);
 
        return 0;
      }
        
      /* If q_recv_next was changed, set the offset value in q->recv_next to
       * mach the changed value */
      q->recv_next = (gasneti_sysvnet_msg_t *)gasneti_sysv_offset(q_recv_next);
      gasneti_mutex_unlock(&q->recv_lock);
    }

    if (++nextindex == vnet->nodecount) 
      nextindex = 0;
  }
  
  gasneti_mutex_lock(&gasneti_index_lock);
  vnet->nextindex=nextindex;
  gasneti_mutex_unlock(&gasneti_index_lock);

  return -1;
}
#else

int gasneti_sysvnet_deliver_send_buffer(gasneti_sysvnet_t *vnet, void *buf, 
                                        size_t nbytes, gasnet_node_t target)
{
  int retval = -1;
  gasneti_sysvnet_payload_t *p;
  gasneti_sysvnet_queue_t *q = vnet->out_queues[sysvnode(vnet, target)];
  gasneti_assert(q != NULL);
  gasneti_mutex_lock(&q->send_lock);

  /* This code assumes that if the current 'send_node' isn't free yet, there
   * are no free slots in the recipient's queue.  Since there is only one
   * sender, one receiver, and the receiver consumes messages in order, this
   * should be true, so no scan over the list is needed. */
  if (!gasneti_atomic_read(&q->send_next->ready4receipt, 0)) {
    retval = 0;
    /* fill in message info */
    q->send_next->addr = buf;
    q->send_next->len = nbytes;
    /* set pointer to msg in buffer */
    p = sysvnet_get_struct_addr_from_field_addr(gasneti_sysvnet_payload_t, payload, buf);
    gasneti_assert(buf == &p->payload);
    p->info.msg = q->send_next;
    /* Perform write flush before writing ready bit */
    gasneti_atomic_set(&q->send_next->ready4receipt, 1, GASNETI_ATOMIC_REL);
    if (++q->send_next == q->justpastlast)
      q->send_next = q->queue;
  }
  gasneti_mutex_unlock(&q->send_lock);
  return retval;
}

int gasneti_sysvnet_recv(gasneti_sysvnet_t *vnet, void **pbuf, size_t *psize, 
                         gasnet_node_t *from)
{
  int i;
  for (i = 0; i < vnet->nodecount; i++) {
    if (vnet->nextindex != gasneti_mysysvnode) {
      gasneti_sysvnet_queue_t *q = vnet->in_queues[vnet->nextindex];
      gasneti_assert(q != NULL);
      gasneti_mutex_lock(&q->recv_lock);
      if (gasneti_atomic_read(&q->recv_next->ready4receipt, GASNETI_ATOMIC_ACQ)) {
        *pbuf = q->recv_next->addr;
        *psize = q->recv_next->len;
        if (++q->recv_next == q->justpastlast)
          q->recv_next = q->queue;
        gasneti_mutex_unlock(&q->recv_lock);
        *from = vnet->nextindex + vnet->firstnode;
        /* Ensure fairness: next check starts with next node */
        if (++vnet->nextindex == vnet->nodecount) 
          vnet->nextindex = 0;
        return 0;
      }
      gasneti_mutex_unlock(&q->recv_lock);
    }
    if (++vnet->nextindex == vnet->nodecount) 
      vnet->nextindex = 0;
  }
  return -1;
}

#endif

/* TODO: the current behavior if a user forgets to call this function is
 * NASTY--the message stays marked as 'ready4receipt', which will both cause
 * senders to think the queue is full, and the receiver to receive the same
 * message again if/when the queue pointer wraps around.  This could cause
 * deadlock and/or lots of confusion (for me it was the latter).
 * - Add another field to payload ('marked_as_released') in debug mode, and
 *   throw an error in receive/deliver functions if it's not set? 
 */
void gasneti_sysvnet_recv_release(gasneti_sysvnet_t *vnet, void *buf)
{
  /* Address we handed out was the addr of the 'payload' field */
  gasneti_sysvnet_payload_t *p = 
    sysvnet_get_struct_addr_from_field_addr(gasneti_sysvnet_payload_t,
                                            payload, buf);
  gasneti_assert(buf == &p->payload);
  gasneti_assert(p && p->info.msg && p->info.allocator);
  /* mark msg as free */
  p->info.msg = gasneti_sysv_addr(p->info.msg);
  gasneti_atomic_set(&p->info.msg->ready4receipt, 0, 0);
  gasneti_sysvnet_free(p->info.allocator, p);
}


/******************************************************************************
 * Sysvnet bootstrap exchange
 * - TODO: to make this more robust, should there be a separate vnet for
 *   this?  (We'd want it to be smaller, to consume less resources, but right
 *   now all vnets are the same size).
 * - Also, could remove requirement that queue depth >= nodes if we check
 *   for incoming msgs as we send them.
 ******************************************************************************/

void gasneti_sysvnet_bootstrapExchange(gasneti_sysvnet_t *vnet, void *src, 
                                       size_t len, void *dest)
{
  gasnet_node_t i, from;
  void *msg;
  size_t inlen;

  gasneti_assert(vnet != NULL);

  /* TODO: right now we assume queues are empty, that queue depth <=
   * nodes. */
  for (i = 0 ; i < vnet->nodecount; i++) {
    if (i == gasnet_mynode())
      continue;
    msg = gasneti_sysvnet_get_send_buffer(vnet, len, i);
    if (msg) {
      memcpy(msg, src, len);
      if (gasneti_sysvnet_deliver_send_buffer(vnet, msg, len, i)) {
        gasneti_fatalerror("T%d: Can't deliver msg to node %d during bootstrap exchange", 
                           gasnet_mynode(), i);
      }
    } else {
      gasneti_fatalerror("T%d: Couldn't get send buffer during bootstrap exchange!", 
                         gasnet_mynode());
    }
  }
  for (i = 1; i < vnet->nodecount; i++) {
    while (gasneti_sysvnet_recv(vnet, &msg, &inlen, &from))
      gasneti_sched_yield();
    if (len != inlen)
      gasneti_fatalerror("T%d: got invalid msg length (%ld) during bootstrap exchange!", 
                         gasnet_mynode(), (long int)inlen);
    memcpy( ((char*)dest)+len*sysvnode(vnet, from), msg, len);
    gasneti_sysvnet_recv_release(vnet, msg);
  }
  /* memcpy our own piece */
  memcpy( ((char*)dest)+len*sysvnode(vnet, gasnet_mynode()), src, len);
}


/******************************************************************************
 * Allocator implementation
 ******************************************************************************/

static gasneti_sysvnet_allocator_t *gasneti_sysvnet_init_allocator(void *region, size_t len)
{
  int i;
  int count = len / sizeof(gasneti_sysvnet_allocator_block_t);
  gasneti_sysvnet_allocator_block_t *tmp;

  /* This implementation doesn't need to put allocator within shared memory.
   * If a later one does, consider increasing the size returned by
   * get_queue_mem()
   */
  gasneti_sysvnet_allocator_t *a = gasneti_malloc(sizeof(gasneti_sysvnet_allocator_t));

  /* make sure we've arranged for block == page */
  gasneti_assert(sizeof(gasneti_sysvnet_allocator_block_t) == 17*GASNETI_SYSVNET_PAGESIZE);
  gasneti_assert_align(region, GASNETI_SYSVNET_PAGESIZE);

  a->queue = a->next = tmp = region;
  a->justpastlast = tmp + count;
  gasneti_mutex_init(&a->next_lock);
  for (i = 0; i < count; i++, tmp++) {
    gasneti_atomic_set(&tmp->alignmentFun.in_use, 0, 0);
  }
  return a;
}


static void * gasneti_sysvnet_alloc(gasneti_sysvnet_allocator_t *a, size_t nbytes)
{
  void *retval = NULL;

  gasneti_assert(nbytes <= sizeof(gasneti_sysvnet_payload_t));

  gasneti_mutex_lock(&a->next_lock);
  /* NOTE: I assume messages are generally consumed in serial order, so just
   * check the 'next' payload, rather than scan the whole array.  I believe
   * this is at least correct, but given that the payloads can go to different
   * receivers (who may take different times to get around to consuming them),
   * perhaps we ought to do a full scan?
   */
  if (!gasneti_atomic_read(&a->next->alignmentFun.in_use, 0)) {
    gasneti_atomic_set(&a->next->alignmentFun.in_use, 1, 0);
    retval = &a->next->payload_t;
    if (++a->next == a->justpastlast)
      a->next = a->queue;
  }
  gasneti_mutex_unlock(&a->next_lock);
  return retval;
}

static void gasneti_sysvnet_free(gasneti_sysvnet_allocator_t *a, void *p)
{
  /* We don't need the allocator ptr, but other implementations might  */

  /* Address we handed out was the addr of the 'payload_t' field */
  gasneti_sysvnet_allocator_block_t *block = 
      sysvnet_get_struct_addr_from_field_addr(gasneti_sysvnet_allocator_block_t,
                                              payload_t, p);
  gasneti_assert(p == &block->payload_t);
  /* assert block is page-aligned */
  gasneti_assert( (((uintptr_t)block) % GASNETI_SYSVNET_PAGESIZE) == 0);

  gasneti_atomic_set(&block->alignmentFun.in_use, 0, 0);
}

/******************************************************************************
 * AMSYSV:  Active Message API over Sysvnet
 ******************************************************************************/

enum {
  gasnetc_Short=0, 
  gasnetc_Medium=1, 
  gasnetc_Long=2,
  gasnetc_invalid_category
};
typedef uint32_t gasneti_AMSYSV_category_t;
typedef uint32_t gasneti_AMSYSV_handler_t;

/* TODO: tweak data sizes?  */
typedef struct {
  gasneti_AMSYSV_category_t category;      /* AM msg type: small, med, large */
  gasneti_AMSYSV_handler_t handler_id;
  uint32_t numargs;
  gasnet_handlerarg_t args[GASNETC_MAX_ARGS_SYSV];
} gasneti_AMSYSV_msg_t;
typedef gasneti_AMSYSV_msg_t gasneti_AMSYSV_smallmsg_t;

typedef struct {
  gasneti_AMSYSV_msg_t msg;
  uint32_t numbytes;
  uint8_t  mediumdata[(size_t)GASNETC_MAX_MEDIUM_SYSV];
} gasneti_AMSYSV_medmsg_t;

typedef struct {
  gasneti_AMSYSV_msg_t msg;
  uint32_t numbytes;
  void *   longdata;
} gasneti_AMSYSV_longmsg_t;

#define GASNETI_AMSYSV_MSG_CATEGORY(msg)      (((gasneti_AMSYSV_msg_t*)msg)->category)
#define GASNETI_AMSYSV_MSG_HANDLERID(msg)     (((gasneti_AMSYSV_msg_t*)msg)->handler_id)
#define GASNETI_AMSYSV_MSG_NUMARGS(msg)       (((gasneti_AMSYSV_msg_t*)msg)->numargs)
#define GASNETI_AMSYSV_MSG_ARGS(msg)          (((gasneti_AMSYSV_msg_t*)msg)->args)
#define GASNETI_AMSYSV_MSG_MED_NUMBYTES(msg)  (((gasneti_AMSYSV_medmsg_t*)msg)->numbytes)
#define GASNETI_AMSYSV_MSG_MED_DATA(msg)      (((gasneti_AMSYSV_medmsg_t*)msg)->mediumdata)
#define GASNETI_AMSYSV_MSG_LONG_NUMBYTES(msg) (((gasneti_AMSYSV_longmsg_t*)msg)->numbytes)
#define GASNETI_AMSYSV_MSG_LONG_DATA(msg)     (((gasneti_AMSYSV_longmsg_t*)msg)->longdata)

#define GASNETI_AMSYSV_MAX_RECVMSGS_PER_POLL 10

/* ------------------------------------------------------------------------------------ */
GASNETI_INLINE(gasneti_AMSYSV_service_incoming_msg)
int gasneti_AMSYSV_service_incoming_msg(gasneti_sysvnet_t *vnet, int isReq)
{
  void *msg;
  size_t msgsz;
  gasnet_node_t from;
  gasneti_AMSYSV_category_t category;
  gasneti_AMSYSV_handler_t handler_id;
  void (*handler_fn)();
  int numargs;
  gasnet_handlerarg_t *args;
  gasnet_token_t token;

  if (gasneti_sysvnet_recv(vnet, &msg, &msgsz, &from))
    return -1;

  token = gasnetc_token_create(from, isReq);
  category = GASNETI_AMSYSV_MSG_CATEGORY(msg);
  gasneti_assert(category < gasnetc_invalid_category);
  handler_id = GASNETI_AMSYSV_MSG_HANDLERID(msg);
  handler_fn = gasneti_get_handler(handler_id);
  numargs = GASNETI_AMSYSV_MSG_NUMARGS(msg);
  args = GASNETI_AMSYSV_MSG_ARGS(msg);

  switch (category) {
    case gasnetc_Short:
      { 
        GASNETI_RUN_HANDLER_SHORT(isReq,handler_id,handler_fn,token,args,numargs);
      }
      break;
    case gasnetc_Medium:
      {
        void * data = GASNETI_AMSYSV_MSG_MED_DATA(msg);
        size_t nbytes = GASNETI_AMSYSV_MSG_MED_NUMBYTES(msg);
        GASNETI_RUN_HANDLER_MEDIUM(
          isReq,handler_id,handler_fn,token,args,numargs,data,nbytes);
      }
      break;
    case gasnetc_Long:
      { 
        void * data;
        size_t nbytes;
        
        data = GASNETI_AMSYSV_MSG_LONG_DATA(msg);
        nbytes = GASNETI_AMSYSV_MSG_LONG_NUMBYTES(msg);
#if 0
        data = (void*)((uintptr_t)data + gasneti_sysv_seginfo_client[gasneti_mysysvnode].addr);
#else
        data = (void *)((uintptr_t)data + (uintptr_t)gasneti_seginfo[gasneti_mynode].addr);
#endif
        GASNETI_RUN_HANDLER_LONG(
            isReq,handler_id,handler_fn,token,args,numargs,data,nbytes);
      }
      break;
  }
  gasnetc_token_destroy(token);
  gasneti_sysvnet_recv_release(vnet, msg);
  return 0;
}

/* ------------------------------------------------------------------------------------ */
int gasneti_AMSYSVPoll(int repliesOnly)
{
  int i = 0;

  GASNETI_CHECKATTACH();

  for (; i < GASNETI_AMSYSV_MAX_RECVMSGS_PER_POLL; i++) 
    if (gasneti_AMSYSV_service_incoming_msg(gasneti_reply_sysvnet, 0))
      break;
  if (!repliesOnly)
    for (; i < GASNETI_AMSYSV_MAX_RECVMSGS_PER_POLL; i++) 
      if (gasneti_AMSYSV_service_incoming_msg(gasneti_request_sysvnet, 1))
        break;
  return GASNET_OK;
}

/* ------------------------------------------------------------------------------------ */
/*
 * Active Message Request Functions
 * ================================
 */

int gasnetc_AMSYSV_ReqRepGeneric(int category, int isReq, int dest,
                                 gasnet_handler_t handler, void *source_addr, int nbytes, 
                                 void *dest_ptr, int numargs, va_list argptr) 
{
  gasneti_sysvnet_t *vnet = (isReq ? gasneti_request_sysvnet : gasneti_reply_sysvnet);
  int msgsz, i;
  void *msg, *dest_addr;
  gasnet_handlerarg_t *pargs;
  int loopback = (dest == gasneti_mynode);

  gasneti_assert(vnet != NULL);

  //printf("%d> reqrep category=%d\n",gasneti_mysysvnode,category);
  /* calculate size of sysV buffer needed */
  switch (category) {
    case gasnetc_Short:
      msgsz = sizeof(gasneti_AMSYSV_smallmsg_t);
      break;
    case gasnetc_Medium:
      msgsz = sizeof(gasneti_AMSYSV_medmsg_t);
      break;
    case gasnetc_Long:
      msgsz = sizeof(gasneti_AMSYSV_longmsg_t);
      break;
    default:
      gasneti_fatalerror("internal error: unknown msg category");
  }
  gasneti_assert(msgsz <= GASNETI_SYSVNET_MAX_PAYLOAD); 

  /* Get buffer, poll if busy */
  if (loopback) {
    /* TODO: instead of doing a malloc each time, keep a per-thread pair of
     * medmsg-sized request/reply buffers, and use them.  See smp-conduit's
     * gasnetc_ReqRepGeneric's handling of mediummsgs */
    msg = gasneti_malloc(msgsz);
  } else {
    while (!(msg = gasneti_sysvnet_get_send_buffer(vnet, msgsz, dest))) {
      /* If reply, only poll reply network: avoids deadlock  */
      //gasneti_AMSYSVPoll(!isReq);

        gasnetc_AMPoll(!isReq);
    }
  }

#if 0
  void *dest_addr = (void*)((uintptr_t)dest_ptr + gasneti_sysv_seginfo_client[dest-gasneti_firstsysvnode].addr);
#else
  dest_addr = (void*)((uintptr_t)dest_ptr + (uintptr_t)gasneti_seginfo[dest].remote_addr);
#endif
  /* Fill in message */
  GASNETI_AMSYSV_MSG_CATEGORY(msg) = category;
  GASNETI_AMSYSV_MSG_HANDLERID(msg) = handler;
  GASNETI_AMSYSV_MSG_NUMARGS(msg) = numargs;
  for(i = 0; i < numargs; i++) 
    GASNETI_AMSYSV_MSG_ARGS(msg)[i] = (gasnet_handlerarg_t)va_arg(argptr, int);

  switch (category) {
    case gasnetc_Short:
      break;
    case gasnetc_Medium:
      GASNETI_AMSYSV_MSG_MED_NUMBYTES(msg) = nbytes;
      memcpy(GASNETI_AMSYSV_MSG_MED_DATA(msg), source_addr, nbytes);
      break;
    case gasnetc_Long:
      GASNETI_AMSYSV_MSG_LONG_DATA(msg) = dest_ptr; 
      GASNETI_AMSYSV_MSG_LONG_NUMBYTES(msg) = nbytes;
      /* deliver_msg call, below, contains write flush, so don't need here */
      /* TODO: given that msg may be long, is it worth using our (allegedly
       * faster) ALIGNED memcpy macro here, and just use vanilla memcpy() on
       * any non-aligned part? */
      memcpy(dest_addr, source_addr, nbytes);
      break;
  }

  /* Deliver message */
  if (loopback) {
    gasneti_handler_fn_t handler_fn = gasneti_get_handler(handler); 
    gasnet_token_t token = gasnetc_token_create(gasneti_mynode, isReq);
    gasnet_handlerarg_t *args = GASNETI_AMSYSV_MSG_ARGS(msg);
    switch (category) {
      case gasnetc_Short:
        GASNETI_RUN_HANDLER_SHORT(isReq,handler,handler_fn,token,args,numargs);

        break;
      case gasnetc_Medium:
        GASNETI_RUN_HANDLER_MEDIUM(isReq, handler, handler_fn, token, args, numargs,
                                   GASNETI_AMSYSV_MSG_MED_DATA(msg), nbytes);
        break;
      case gasnetc_Long:
        gasneti_local_wmb(); /* sync memcpy, above */
        GASNETI_RUN_HANDLER_LONG(isReq, handler, handler_fn, token, args, numargs,
                                 dest_addr, nbytes);
        break;
    }
    gasneti_free(msg);
    gasnetc_token_destroy(token);
  } else {
    
    while (gasneti_sysvnet_deliver_send_buffer(vnet, msg, msgsz, dest)) {
      /* If reply, only poll reply network: avoids deadlock  */
      //gasneti_AMSYSVPoll(!isReq);
        gasnetc_AMPoll(!isReq);
    }
  }
  return GASNET_OK;
}

#endif /* GASNET_SYSV */

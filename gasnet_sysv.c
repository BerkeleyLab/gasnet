/*   $Source: /Users/kamil/work/gasnet-cvs2/gasnet/Attic/gasnet_sysv.c,v $
 *     $Date: 2007/04/26 23:23:12 $
 * $Revision: 1.1.2.5 $
 * Description: GASNet infrastructure for shared memory communications
 * Copyright 2007, Dan Bonachea <bonachea@cs.berkeley.edu>
 * Terms of use are as specified in license.txt
 */

#include <gasnet_internal.h>


#if GASNET_SYSV

/*******************************************************************************
 * "SysV Net":  virtual network between peers in a shared memory supernode 
 ******************************************************************************/

/* my rank within my supernode (0-based index) */
gasnet_node_t gasneti_sysvnet_mynode;

/* max # of incoming requests per node, per supernode peer */
static int gasneti_sysvnet_queue_depth;  
#define GASNETI_SYSVNET_DEFAULT_QUEUE_DEPTH 24
#define GASNETI_SYSVNET_MAX_QUEUE_DEPTH 1024

/* payload memory available for outstanding requests, per node */
static uintptr_t gasneti_sysvnet_queue_mem; 
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
        (GASNETI_SYSVNET_PAGESIZE - (2*sizeof(gasneti_sysvnet_payload_info_t)))

#define round_up_to_sysvpage(size_or_addr)               \
        GASNETI_ALIGNUP(size_or_addr, GASNETI_SYSVNET_PAGESIZE)

size_t gasneti_sysvnet_max_payload() {
  return GASNETI_SYSVNET_MAX_PAYLOAD;
}

typedef struct gasneti_sysvnet_payload {
  gasneti_sysvnet_payload_info_t info;
  char payload[GASNETI_SYSVNET_MAX_PAYLOAD];
} gasneti_sysvnet_payload_t;

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

static int get_queue_depth() 
{
  int val = gasneti_getenv_int_withdefault("GASNET_SYSVNET_QUEUE_DEPTH", GASNETI_SYSVNET_DEFAULT_QUEUE_DEPTH, 0);
  if (val > GASNETI_SYSVNET_MAX_QUEUE_DEPTH) {
    fprintf(stderr, "GASNET_SYSVNET_QUEUE_DEPTH (%d) larger than max: using max (%d)\n",
            val, GASNETI_SYSVNET_MAX_QUEUE_DEPTH);
    val = GASNETI_SYSVNET_MAX_QUEUE_DEPTH;
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

  gasneti_sysvnet_queue_depth = get_queue_depth();
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

static void init_queue(gasneti_sysvnet_queue_t *q, gasneti_sysvnet_msg_t *msgs,
                       int depth)
{
  int i;

  q->queue = q->recv_next = q->send_next = msgs;
  q->justpastlast = msgs + depth;
  gasneti_mutex_init(&q->recv_lock);
  gasneti_mutex_init(&q->send_lock);

  for (i = 0; i < depth; i++) {
    gasneti_atomic_set(&msgs->ready4receipt, 0, 0);
    msgs++;
  }
}

static void gasneti_sysvnet_init_my_sysv(gasneti_sysvnet_t *pvnet, char * myregion, 
                                  gasnet_node_t firstnode, gasnet_node_t nodes)
{
  int i;
  gasneti_sysvnet_queue_t *myqueues;
  gasneti_sysvnet_msg_t   *mymsgs;
  void *alloc_region;

printf("gasneti_sysvnet_init_my_sysv: got myregion=%p\n", myregion);
  gasneti_assert_align(myregion, GASNETI_SYSVNET_PAGESIZE);

  /* NOTE: other init code relies on queues being at start of region */
  myqueues = (gasneti_sysvnet_queue_t *)myregion;
  mymsgs = (gasneti_sysvnet_msg_t *)(((char*)myqueues) 
                                      + sizeof(gasneti_sysvnet_queue_t)*nodes);
printf("T%d (%d): myqueues=%p, mymsgs=%p, sizeof(queue)=%lu, sizeof(msg)=%lu\n", gasnet_mynode(), gasneti_sysvnet_mynode, myqueues, mymsgs, sizeof(gasneti_sysvnet_queue_t), sizeof(gasneti_sysvnet_msg_t));
  gasneti_assert_align(mymsgs, GASNETI_CACHE_LINE_BYTES);
  alloc_region = ((char*)mymsgs) + 
        sizeof(gasneti_sysvnet_msg_t)*gasneti_sysvnet_queue_depth*(nodes-1);
  alloc_region = (void *)round_up_to_sysvpage(alloc_region);

  for (i = 0; i < nodes; i++) {
    if (i == gasneti_sysvnet_mynode) {
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

void gasneti_sysvnet_init(gasneti_sysvnet_t **pvnet, void *start, size_t nbytes, 
                          gasnet_node_t firstnode, gasnet_node_t sysvnodes)
{
  gasneti_sysvnet_t *vnet;
  gasnet_node_t i, othernode;
  size_t szpernode, regionlen;
  void *region, *myregion;

  gasneti_sysvnet_mynode = gasnet_mynode() - firstnode;
  region = start;
  region = (void *)round_up_to_sysvpage(region);
  regionlen = nbytes - ( ((uintptr_t)region)-((uintptr_t)start));
  szpernode = gasneti_sysvnet_memory_needed_pernode(sysvnodes);
  if (regionlen < szpernode * sysvnodes) 
    gasneti_fatalerror("Internal error: not enough memory for sysvnet: \n"
                       " given %ld effective bytes, but need %ld", 
                       regionlen, szpernode * sysvnodes);
  vnet = gasneti_malloc(sizeof(gasneti_sysvnet_t));
  vnet->firstnode = firstnode;
  vnet->nodecount = sysvnodes;
  myregion = (void *)( ((uintptr_t)region) + (szpernode*gasneti_sysvnet_mynode));
  /* collective call, so each process inits its own region */
  gasneti_sysvnet_init_my_sysv(vnet, myregion, firstnode, sysvnodes);

  /* initialize queue pointers */
  vnet->in_queues = gasneti_malloc(sizeof(gasneti_sysvnet_queue_t*)*sysvnodes);
  vnet->out_queues = gasneti_malloc(sizeof(gasneti_sysvnet_queue_t*)*sysvnodes);
  for (i = 0; i < sysvnodes; i++) {
    vnet->in_queues[i] = ((gasneti_sysvnet_queue_t *)myregion) + i;
    vnet->out_queues[i] = ((gasneti_sysvnet_queue_t *) (((uintptr_t)region)+(szpernode*i))) 
                          + gasneti_sysvnet_mynode;
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

int gasneti_sysvnet_deliver_send_buffer(gasneti_sysvnet_t *vnet, void *buf, 
                                        size_t nbytes, gasnet_node_t target)
{
  int retval = -1;
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
    if (vnet->nextindex != gasneti_sysvnet_mynode) {
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

#define sysvnet_get_struct_addr_from_field_addr(structname, fieldname, fieldaddr) \
        ((structname*)(((char *)fieldaddr) - (char *)((structname *)0)->fieldname))

void gasneti_sysvnet_recv_release(gasneti_sysvnet_t *vnet, void *buf)
{
  /* Address we handed out was the addr of the 'payload' field */
  gasneti_sysvnet_payload_t *p = 
    sysvnet_get_struct_addr_from_field_addr(gasneti_sysvnet_payload_t,
                                            payload, buf);
  /* mark msg as free */
  gasneti_atomic_set(&p->info.msg->ready4receipt, 0, 0);
  gasneti_sysvnet_free(p->info.allocator, p);
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
  gasneti_assert(sizeof(gasneti_sysvnet_allocator_block_t) == GASNETI_SYSVNET_PAGESIZE);
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
    gasneti_atomic_set(&a->next->alignmentFun.in_use, 0, 0);
    retval = &a->next->payload_t;
    if (++a->next == a->justpastlast)
      a->next = a->queue;
  }
  gasneti_mutex_unlock(&a->next_lock);
  return retval;
}

static void gasneti_sysvnet_free(gasneti_sysvnet_allocator_t *a, void *p)
{
  /* We don't need the allocator ptr, but other implementation might  */

  /* Address we handed out was the addr of the 'payload_t' field */
  gasneti_sysvnet_payload_t *payload_t = (gasneti_sysvnet_payload_t *)p;
  /* backup one to get to the start of our allocator block */
  gasneti_sysvnet_allocator_block_t *block = 
      (gasneti_sysvnet_allocator_block_t *) --payload_t;

  /* assert block is page-aligned */
  gasneti_assert( (((uintptr_t)block) % GASNETI_SYSVNET_PAGESIZE) == 0);

  gasneti_atomic_set(&block->alignmentFun.in_use, 0, 0);
}


#endif /* GASNET_SYSV */

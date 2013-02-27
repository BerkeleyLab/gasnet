#include <gasnet_internal.h>
#include <gasnet_core_internal.h>
#include <gasnet_handler.h>
#include <gasnet_gemini.h>
#include <fcntl.h>
#include <sys/mman.h>
#include <signal.h>

#define GASNETC_NETWORKDEPTH_DEFAULT 12

#ifdef GASNET_CONDUIT_GEMINI
  /* Use dual-events + PI_FLUSH to get "proper" ordering w/ relaxed and default PI ordering */
  #define FIX_HT_ORDERING 1
  static uint16_t gasnetc_fma_put_cq_mode = GNI_CQMODE_GLOBAL_EVENT;
#else
  #define FIX_HT_ORDERING 0
#endif

int      gasnetc_dev_id;
uint32_t gasnetc_cookie;
uint32_t gasnetc_address;
uint8_t  gasnetc_ptag;

static uint32_t gasnetc_memreg_flags;
static int gasnetc_mem_consistency;

int gasnetc_poll_burst = 10;
static int bank_credits;

static double shutdown_max;
static uint32_t sys_exit_rcvd;

#if GASNETC_SMSG_GASNET
typedef union {
  volatile uint64_t full; /* is zero until filled */
  GC_Header_t header;
  uint8_t raw[GASNETC_MSG_MAXSIZE];
} gasnetc_mailbox_t;
#endif

typedef struct peer_struct {
  struct peer_struct *next; /* pointer to next when queue, GC_NOT_QUEUED otherwise */
  gni_ep_handle_t ep_handle;
  gni_mem_handle_t mem_handle;
  gasneti_weakatomic_t am_credit;
  gasneti_weakatomic_t am_credit_bank; /* credits accumulated for return to peer */
#if GASNETC_SMSG_GASNET
  struct {
    gasnetc_mailbox_t *loc_addr;
    gasnetc_mailbox_t *rem_addr;
    gni_mem_handle_t rem_hndl;
    /* "pos" variables are in range [1..mb_slots] and moving backwards */
    unsigned int recv_pos;
    unsigned int send_pos;
  } mb;
#endif
  gasnet_node_t rank;
} peer_struct_t;

static peer_struct_t *peer_data;

static gni_mem_handle_t my_smsg_handle;
static gni_mem_handle_t my_mem_handle;

static gni_cdm_handle_t cdm_handle;
static gni_nic_handle_t nic_handle;
static gni_cq_handle_t bound_cq_handle;
static gni_cq_handle_t smsg_cq_handle;
static gni_cq_handle_t destination_cq_handle;

static void *smsg_mmap_ptr;
static size_t smsg_mmap_bytes;

static gasnet_seginfo_t gasnetc_bounce_buffers;
static gasnet_seginfo_t gasnetc_pd_buffers;

static unsigned int mb_slots;
static unsigned int am_maxcredit;

/*------ Resource accounting ------*/

/* Limit the number of active dynamic memory registrations */
static gasneti_weakatomic_t reg_credit;
#define gasnetc_alloc_reg_credit()         gasnetc_weakatomic_dec_if_positive(&reg_credit)
#if GASNET_DEBUG && 0
static gasneti_weakatomic_val_t reg_credit_max;
#define gasnetc_init_reg_credit(_val)      gasneti_weakatomic_set(&reg_credit,reg_credit_max=(_val),0)
  static void gasnetc_return_reg_credit(void) {
    gasneti_weakatomic_val_t new_val = gasneti_weakatomic_add(&reg_credit,1,0);
    gasneti_assert(new_val <= reg_credit_max);
  }
#else
  #define gasnetc_init_reg_credit(_val)    gasneti_weakatomic_set(&reg_credit,(_val),0)
  #define gasnetc_return_reg_credit()      gasneti_weakatomic_increment(&reg_credit,0)
#endif

/*------ Convience functions for printing error messages ------*/

static const char *gni_return_string(gni_return_t status)
{
  if (status == GNI_RC_SUCCESS) return ("GNI_RC_SUCCESS");
  if (status == GNI_RC_NOT_DONE) return ("GNI_RC_NOT_DONE");
  if (status == GNI_RC_INVALID_PARAM) return ("GNI_RC_INVALID_PARAM");
  if (status == GNI_RC_ERROR_RESOURCE) return ("GNI_RC_ERROR_RESOURCE");
  if (status == GNI_RC_TIMEOUT) return ("GNI_RC_TIMEOU");
  if (status == GNI_RC_PERMISSION_ERROR) return ("GNI_RC_PERMISSION_ERROR");
  if (status == GNI_RC_DESCRIPTOR_ERROR) return ("GNI_RC_DESCRIPTOR ERROR");
  if (status == GNI_RC_ALIGNMENT_ERROR) return ("GNI_RC_ALIGNMENT_ERROR");
  if (status == GNI_RC_INVALID_STATE) return ("GNI_RC_INVALID_STATE");
  if (status == GNI_RC_NO_MATCH) return ("GNI_RC_NO_MATCH");
  if (status == GNI_RC_SIZE_ERROR) return ("GNI_RC_SIZE_ERROR");
  if (status == GNI_RC_TRANSACTION_ERROR) return ("GNI_RC_TRANSACTION_ERROR");
  if (status == GNI_RC_ILLEGAL_OP) return ("GNI_RC_ILLEGAL_OP");
  if (status == GNI_RC_ERROR_NOMEM) return ("GNI_RC_ERROR_NOMEM");
  return("unknown");
}


const char *gasnetc_type_string(int type)
{
  if (type == GC_CMD_NULL) return("GC_CMD_AM_NULL");
  if (type == GC_CMD_AM_SHORT) return ("GC_CMD_AM_SHORT");
  if (type == GC_CMD_AM_LONG) return ("GC_CMD_AM_LONG");
  if (type == GC_CMD_AM_MEDIUM) return ("GC_CMD_AM_MEDIUM");
  if (type == GC_CMD_AM_SHORT_REPLY) return ("GC_CMD_AM_SHORT_REPLY");
  if (type == GC_CMD_AM_LONG_REPLY) return ("GC_CMD_AM_LONG_REPLY");
  if (type == GC_CMD_AM_MEDIUM_REPLY) return ("GC_CMD_AM_MEDIUM_REPLY");
  if (type == GC_CMD_AM_NOP_REPLY) return ("GC_CMD_AM_NOP_REPLY");
  if (type == GC_CMD_SYS_SHUTDOWN_REQUEST) return ("GC_CMD_SYS_SHUTDOWN_REQUEST");
  return("unknown");
}

const char *gasnetc_post_type_string(gni_post_type_t type)
{
  if (type == GNI_POST_RDMA_PUT) return("GNI_POST_RDMA_PUT");
  if (type == GNI_POST_RDMA_GET) return("GNI_POST_RDMA_GET");
  if (type == GNI_POST_FMA_PUT) return("GNI_POST_FMA_PUT");
  if (type == GNI_POST_FMA_GET) return("GNI_POST_FMA_GET");
  return("unknown");
}

/*------ Work Queue threading peer_struct_t with pending AM receives ------*/

static
struct {
  peer_struct_t *head;
  peer_struct_t *tail;
  gasnetc_queuelock_t lock;
} smsg_work_queue;

#define GC_NOT_QUEUED ((struct peer_struct*)(uintptr_t)1)
#define GC_IS_QUEUED(_qi) \
        (gasneti_assert((_qi) != NULL), ((_qi)->next != GC_NOT_QUEUED))

void gasnetc_work_queue_init(void)
{
  smsg_work_queue.head = NULL;
  smsg_work_queue.tail = NULL;
  GASNETC_INITLOCK_QUEUE(&smsg_work_queue);
}

/* Remove qi from HEAD of work queue - acquires and releases lock */
GASNETI_INLINE(gasnetc_work_dequeue)
peer_struct_t *gasnetc_work_dequeue(void)
{
  peer_struct_t *qi;
  GASNETC_LOCK_QUEUE(&smsg_work_queue);
  qi = smsg_work_queue.head;
  if (qi != NULL) {
    smsg_work_queue.head = qi->next;
    if (smsg_work_queue.head == NULL) smsg_work_queue.tail = NULL;
    qi->next = GC_NOT_QUEUED;
  }
  GASNETC_UNLOCK_QUEUE(&smsg_work_queue);
  return(qi);
}

/* Add qi at TAIL of work queue - caller must hold lock */
GASNETI_INLINE(gasnetc_work_enqueue_nolock)
void gasnetc_work_enqueue_nolock(peer_struct_t *qi)
{
  gasneti_assert(qi != NULL);
  gasneti_assert(qi->next == GC_NOT_QUEUED);
  /* GASNETC_LOCK_QUEUE(&smsg_work_queue); */
  if (smsg_work_queue.head == NULL) {
    smsg_work_queue.head = qi;
  } else {
    gasneti_assert(smsg_work_queue.tail != NULL);
    smsg_work_queue.tail->next = qi;
  }
  smsg_work_queue.tail = qi;
  qi->next = NULL;
  /* GASNETC_UNLOCK_QUEUE(&smsg_work_queue); */
}

/*-------------------------------------------------*/
/* We don't allocate resources for comms w/ self or PSHM-reachable peers */

#if GASNET_PSHM
  #define node_is_local(_i) gasneti_pshm_in_supernode(_i)
#else
  #define node_is_local(_i) ((_i) == gasneti_mynode)
#endif

/* From point-of-view of a remote node, what is MY index as an Smsg peer? */
GASNETI_INLINE(my_smsg_index)
int my_smsg_index(gasnet_node_t remote_node) {
#if GASNET_PSHM
  int i, result = 0;

  gasneti_assert(NULL != gasneti_nodemap);
  for (i = 0; i < gasneti_mynode; ++i) {
    /* counts nodes that are not local to remote_node */
    result += (gasneti_nodemap[i] != gasneti_nodemap[remote_node]);
  }
  return result;
#else
  /* we either fall before or after the remote node skips over itself */
  return gasneti_mynode - (gasneti_mynode > remote_node);
#endif
}

/*------ Convience functions for factoring Smsg implementation(s) ------*/

static unsigned int
gasnetc_bytes_per_mbox(int slots)
{
#if GASNETC_SMSG_GASNET
  return slots * sizeof(gasnetc_mailbox_t);
#else
  gni_return_t status;
  gni_smsg_attr_t attr;
  unsigned int result;

#if GASNETC_SMSG_ARIES
  attr.msg_type = GNI_SMSG_TYPE_MBOX_AUTO_RETRANSMIT;
#elif GASNETC_SMSG_GEMINI
  attr.msg_type = GNI_SMSG_TYPE_MBOX;
#endif
  attr.mbox_maxcredit = slots;
  attr.msg_maxsize = GASNETC_MSG_MAXSIZE;

  status = GNI_SmsgBufferSizeNeeded(&attr, &result);
  if (status != GNI_RC_SUCCESS) {
    gasnetc_GNIT_Abort("GNI_GetSmsgBufferSize returned error %s",gni_return_string(status));
  }
  result = GASNETI_ALIGNUP(result, GASNETC_CACHELINE_SIZE);
  /* TODO: no other GNI client is doing this scaling, yet GNI_SmsgBufferSizeNeeded()
     yields a value much too small to buffer (slots*GASNETC_MSG_MAXSIZE) bytes.
   */
  result += slots * GASNETC_MSG_MAXSIZE;
  return result;
#endif
}

GASNETI_INLINE(gasnetc_smsg_get_next)
gni_return_t gasnetc_smsg_get_next(peer_struct_t *peer, GC_Header_t **recv_p)
{
#if GASNETC_SMSG_GASNET
  unsigned int slot = peer->mb.recv_pos - 1;
  gasnetc_mailbox_t * const mb = &peer->mb.loc_addr[slot];
  gni_return_t status = GNI_RC_NOT_DONE;

  if (mb->full) { /* First word is zero until mailbox is filled */
    gasneti_sync_reads();
    peer->mb.recv_pos = slot ? slot : mb_slots;
    *recv_p = &mb->header;
    status = GNI_RC_SUCCESS;
  }

  return status;
#else
  return GNI_SmsgGetNext(peer->ep_handle, (void **) recv_p);
#endif
}

GASNETI_INLINE(gasnetc_smsg_release)
gni_return_t gasnetc_smsg_release(peer_struct_t *peer, GC_Header_t *header)
{
#if GASNETC_SMSG_GASNET
  ((gasnetc_mailbox_t *)header)->full = 0;
  return GNI_RC_SUCCESS;
#else
  return GNI_SmsgRelease(peer->ep_handle);
#endif
}

/*-------------------------------------------------*/

static uint32_t *gather_nic_addresses(void)
{
  uint32_t *result = gasneti_malloc(gasneti_nodes * sizeof(uint32_t));

  if (gasnetc_dev_id == -1) {
    /* no value was found in environment */
    gni_return_t status;
    uint32_t cpu_id;

    gasnetc_dev_id  = 0;
    status = GNI_CdmGetNicAddress(gasnetc_dev_id, &gasnetc_address, &cpu_id);
    if (status != GNI_RC_SUCCESS) {
      gasnetc_GNIT_Abort("GNI_CdmGetNicAddress failed: %s", gni_return_string(status));
    }
  } else {
    /* use gasnetc_address taken from the environment */
  }

  gasnetc_bootstrapExchange(&gasnetc_address, sizeof(uint32_t), result);

  return result;
}

/*-------------------------------------------------*/
/* called after segment init. See gasneti_seginfo */
/* allgather the memory handles for the segments */
/* create endpoints */
void gasnetc_init_segment(void *segment_start, size_t segment_size)
{
  gni_return_t status;
  /* Map the shared segment */

  { int envval = gasneti_getenv_int_withdefault("GASNETC_GNI_MEMREG", GASNETC_GNI_MEMREG_DEFAULT, 0);
    if (envval < 1) envval = 1;
    gasnetc_init_reg_credit(envval);
  }

  gasnetc_mem_consistency = GASNETC_DEFAULT_RDMA_MEM_CONSISTENCY;
  { char * envval = gasneti_getenv("GASNETC_GNI_MEM_CONSISTENCY");
    if (!envval || !envval[0]) {
      /* No value given - keep default */
    } else if (!strcmp(envval, "strict") || !strcmp(envval, "STRICT")) {
      gasnetc_mem_consistency = GASNETC_STRICT_MEM_CONSISTENCY;
    } else if (!strcmp(envval, "relaxed") || !strcmp(envval, "RELAXED")) {
      gasnetc_mem_consistency = GASNETC_RELAXED_MEM_CONSISTENCY;
    } else if (!strcmp(envval, "default") || !strcmp(envval, "DEFAULT")) {
      gasnetc_mem_consistency = GASNETC_DEFAULT_MEM_CONSISTENCY;
    } else if (!gasneti_mynode) {
      fflush(NULL);
      fprintf(stderr, "WARNING: ignoring unknown value '%s' for environment "
                      "variable GASNETC_GNI_MEM_CONSISTENCY\n", envval);
      fflush(NULL);
    }
  }

  switch (gasnetc_mem_consistency) {
    case GASNETC_STRICT_MEM_CONSISTENCY:
      gasnetc_memreg_flags = GNI_MEM_STRICT_PI_ORDERING;
      break;
    case GASNETC_RELAXED_MEM_CONSISTENCY:
      gasnetc_memreg_flags = GNI_MEM_RELAXED_PI_ORDERING;
      break;
    case GASNETC_DEFAULT_MEM_CONSISTENCY:
      gasnetc_memreg_flags = 0;
      break;
  }
  gasnetc_memreg_flags |= GNI_MEM_READWRITE;

#if FIX_HT_ORDERING
  if (gasnetc_mem_consistency != GASNETC_STRICT_MEM_CONSISTENCY) {
    gasnetc_memreg_flags |= GNI_MEM_PI_FLUSH; 
    gasnetc_fma_put_cq_mode |= GNI_CQMODE_REMOTE_EVENT;

    /* With 1 completion entry this queue is INTENDED to always overflow */
    status = GNI_CqCreate(nic_handle, 1, 0, GNI_CQ_NOBLOCK, NULL, NULL, &destination_cq_handle);
    gasneti_assert_always (status == GNI_RC_SUCCESS);
  }
#endif

  {
    int count = 0;
    for (;;) {
      status = GNI_MemRegister(nic_handle, (uint64_t) segment_start, 
			       (uint64_t) segment_size, destination_cq_handle,
			       gasnetc_memreg_flags, -1, 
			       &my_mem_handle);
      if (status == GNI_RC_SUCCESS) break;
      if (status == GNI_RC_ERROR_RESOURCE) {
	gasnetc_GNIT_Log("MemRegister segment fault %d at  %p %lx, code %s", 
		count, segment_start, segment_size, gni_return_string(status));
	count += 1;
	if (count >= 10) break;
      } else {
	break;
      }
    }
  }
			       
  assert (status == GNI_RC_SUCCESS);
  
  {
    gni_mem_handle_t *all_mem_handle = gasneti_malloc(gasneti_nodes * sizeof(gni_mem_handle_t));
    gasnet_node_t i;
    gasnetc_bootstrapExchange(&my_mem_handle, sizeof(gni_mem_handle_t), all_mem_handle);
    for (i = 0; i < gasneti_nodes; ++i) {
      peer_data[i].mem_handle = all_mem_handle[i];
    }
    gasneti_free(all_mem_handle);
  }
}

uintptr_t gasnetc_init_messaging(void)
{
  const gasnet_node_t remote_nodes = gasneti_nodes - (GASNET_PSHM ? gasneti_nodemap_local_count : 1);
  gni_return_t status;
#if !GASNETC_SMSG_GASNET
  gni_smsg_attr_t my_smsg_attr;
  gni_smsg_type_t smsg_type;
#endif
  uint32_t *all_addr;
  uint32_t remote_addr;
  uint32_t local_address;
  uint32_t i;
  unsigned int bytes_per_mbox;
  unsigned int bytes_needed;
  int modes = 0;

#if GASNET_DEBUG
  modes |= GNI_CDM_MODE_ERR_NO_KILL;
#endif

  GASNETC_INITLOCK_GNI();

  gasnetc_work_queue_init();

  status = GNI_CdmCreate(gasneti_mynode,
			 gasnetc_ptag, gasnetc_cookie,
			 modes,
			 &cdm_handle);
  gasneti_assert_always (status == GNI_RC_SUCCESS);

  status = GNI_CdmAttach(cdm_handle,
			 gasnetc_dev_id,
			 &local_address,
			 &nic_handle);

  gasneti_assert_always (status == GNI_RC_SUCCESS);

#if GASNETC_SMSG_ARIES
  smsg_type = GNI_SMSG_TYPE_MBOX_AUTO_RETRANSMIT;
#elif GASNETC_SMSG_GEMINI
  smsg_type = GNI_SMSG_TYPE_MBOX;
#endif
 
  { /* Determine credits for AMs: GASNET_NETWORKDEPTH */
    int depth = gasneti_getenv_int_withdefault("GASNET_NETWORKDEPTH",
                                               GASNETC_NETWORKDEPTH_DEFAULT, 0);
    am_maxcredit = MAX(1,depth); /* Min is 1 */
#if GASNETC_SMSG_GASNET
    mb_slots = 2 * am_maxcredit; /* (req + reply) = 2 */
#else
    mb_slots = 2 * am_maxcredit + 2; /* (req + reply) = 2 , +2 for "lag"?*/
#endif
    bank_credits = (depth > 1) ? 1 : 0;
  }

  { /* Determine Cq size: GASNETC_GNI_NUM_PD */
    int num_pd, cq_entries;
    num_pd = gasneti_getenv_int_withdefault("GASNETC_GNI_NUM_PD",
                                            GASNETC_GNI_NUM_PD_DEFAULT,1);

    cq_entries = num_pd+2; /* XXX: why +2 ?? */

    status = GNI_CqCreate(nic_handle, cq_entries, 0, GNI_CQ_NOBLOCK, NULL, NULL, &bound_cq_handle);
    gasneti_assert_always (status == GNI_RC_SUCCESS);
  }

  /* init peer_data */
  all_addr = gather_nic_addresses();
  peer_data = gasneti_malloc(gasneti_nodes * sizeof(peer_struct_t));
  for (i = 0; i < gasneti_nodes; i += 1) {
    if (node_is_local(i)) continue; /* no connection to self or PSHM-reachable peers */
    peer_data[i].next = GC_NOT_QUEUED;
    status = GNI_EpCreate(nic_handle, bound_cq_handle, &peer_data[i].ep_handle);
    gasneti_assert_always (status == GNI_RC_SUCCESS);
    status = GNI_EpBind(peer_data[i].ep_handle, all_addr[i], i);
    gasneti_assert_always (status == GNI_RC_SUCCESS);
    /* mem_handle will be set at end of gasnetc_init_segment */
    gasneti_weakatomic_set(&peer_data[i].am_credit, am_maxcredit, 0);
    gasneti_weakatomic_set(&peer_data[i].am_credit_bank, 0, 0);
    peer_data[i].rank = i;
  }
  gasneti_free(all_addr);

  /* Initialize the short message system */

  /*
   * allocate a CQ in which to receive message notifications
   */
  /* MAX(1,) avoids complication for remote_nodes==0 */
  status = GNI_CqCreate(nic_handle,MAX(1,remote_nodes*mb_slots),0,GNI_CQ_NOBLOCK,NULL,NULL,&smsg_cq_handle);
  if (status != GNI_RC_SUCCESS) {
    gasnetc_GNIT_Abort("GNI_CqCreate returned error %s", gni_return_string(status));
  }
  
  /*
   * Set up an mmap region to contain all of my mailboxes.
   */

  bytes_per_mbox = gasnetc_bytes_per_mbox(mb_slots);

  /* TODO: remove MAX(1,) while still avoiding "issues" on single-(super)node runs */
  bytes_needed = MAX(1,remote_nodes) * bytes_per_mbox;
  
  smsg_mmap_ptr = gasneti_huge_mmap(NULL, bytes_needed);
  smsg_mmap_bytes = bytes_needed;
  if (smsg_mmap_ptr == (char *)MAP_FAILED) {
    gasnetc_GNIT_Abort("hugepage mmap failed: ");
  }
  
  {
    int count = 0;
    for (;;) {
      status = GNI_MemRegister(nic_handle, 
			       (unsigned long)smsg_mmap_ptr, 
			       bytes_needed,
			       smsg_cq_handle,
			       GNI_MEM_STRICT_PI_ORDERING | GNI_MEM_PI_FLUSH | GNI_MEM_READWRITE,
			       -1,
			       &my_smsg_handle);
      if (status == GNI_RC_SUCCESS) break;
      if (status == GNI_RC_ERROR_RESOURCE) {
	gasnetc_GNIT_Log("MemRegister smsg fault %d at  %p %x, code %s", 
		count, smsg_mmap_ptr, bytes_needed, gni_return_string(status));
	count += 1;
	if (count >= 10) break;
      } else {
	break;
      }
    }
  }
  if (status != GNI_RC_SUCCESS) {
    gasnetc_GNIT_Abort("GNI_MemRegister returned error %s",gni_return_string(status));
  }

#if !GASNETC_SMSG_GASNET
  my_smsg_attr.mem_hndl = my_smsg_handle;
  my_smsg_attr.msg_type = smsg_type;
  my_smsg_attr.msg_buffer = smsg_mmap_ptr;
  my_smsg_attr.buff_size = bytes_per_mbox;
  my_smsg_attr.mbox_maxcredit = mb_slots;
  my_smsg_attr.msg_maxsize = GASNETC_MSG_MAXSIZE;
  my_smsg_attr.mbox_offset = 0;
#endif

  /* exchange peer data and initialize smsg */
  { struct smsg_exchange { uint8_t *addr; gni_mem_handle_t handle; };
    struct smsg_exchange my_smsg_exchg = { smsg_mmap_ptr, my_smsg_handle };
    struct smsg_exchange *all_smsg_exchg = gasneti_malloc(gasneti_nodes * sizeof(struct smsg_exchange));
#if GASNETC_SMSG_GASNET
    uint8_t *local_buffer = smsg_mmap_ptr;
#else
    gni_smsg_attr_t remote_attr;

    remote_attr.msg_type = smsg_type;
    remote_attr.buff_size = bytes_per_mbox;
    remote_attr.mbox_maxcredit = mb_slots;
    remote_attr.msg_maxsize = GASNETC_MSG_MAXSIZE;
#endif

    gasnetc_bootstrapExchange(&my_smsg_exchg, sizeof(struct smsg_exchange), all_smsg_exchg);
  
    /* At this point all_smsg_exchg has the required information for everyone */
    for (i = 0; i < gasneti_nodes; i += 1) {
      if (node_is_local(i)) continue; /* no connection to self or PSHM-reachable peers */

#if GASNETC_SMSG_GASNET
      peer_data[i].mb.loc_addr = (gasnetc_mailbox_t*) local_buffer;
      peer_data[i].mb.rem_addr = (gasnetc_mailbox_t*) all_smsg_exchg[i].addr + (mb_slots * my_smsg_index(i));
      peer_data[i].mb.rem_hndl = all_smsg_exchg[i].handle;
      peer_data[i].mb.recv_pos = mb_slots;
      peer_data[i].mb.send_pos = mb_slots;
      local_buffer += bytes_per_mbox;
#else
      remote_attr.msg_buffer  = all_smsg_exchg[i].addr;
      remote_attr.mem_hndl    = all_smsg_exchg[i].handle;
      remote_attr.mbox_offset = bytes_per_mbox * my_smsg_index(i);

      status = GNI_SmsgInit(peer_data[i].ep_handle, &my_smsg_attr, &remote_attr);
      if (status != GNI_RC_SUCCESS) {
        gasnetc_GNIT_Abort("GNI_SmsgInit returned error %s", gni_return_string(status));
      }
      my_smsg_attr.mbox_offset += bytes_per_mbox;
#endif
    }

    gasneti_free(all_smsg_exchg);
  }

  /* set the number of seconds we poll until forceful shutdown.
   * May be over-ridden by env-vars.
   */
  gasnetc_shutdown_seconds = gasneti_get_exittimeout(shutdown_max, 3., 0.125, 0.);

  /* TODO: distinct cut-offs for Put and Get */
  gasnetc_fma_rdma_cutover = 
    gasneti_getenv_int_withdefault("GASNETC_GNI_FMA_RDMA_CUTOVER",
				   GASNETC_GNI_FMA_RDMA_CUTOVER_DEFAULT,1);
  if (gasnetc_fma_rdma_cutover > GASNETC_GNI_FMA_RDMA_CUTOVER_MAX)
    gasnetc_fma_rdma_cutover = GASNETC_GNI_FMA_RDMA_CUTOVER_MAX;

  /* Now make sure everyone is ready */
  gasnetc_bootstrapBarrier();

  return bytes_needed;
}

void gasnetc_shutdown(void)
{
  int i;
  int tries;
  int left;
  gni_return_t status;
  /* seize gni lock and hold it  */
  GASNETC_LOCK_GNI();
  /* Do other threads need to be killed off here?
     release resources in the reverse order of acquisition
   */

  /* for each connected rank */
  left = gasneti_nodes - (GASNET_PSHM ? gasneti_nodemap_local_count : 1);
  for (tries=0; tries<10; ++tries) {
    for (i = 0; i < gasneti_nodes; i += 1) {
      if (node_is_local(i)) continue; /* no connection to self or PSHM-reachable peers */
      if (peer_data[i].ep_handle != NULL) {
	status = GNI_EpUnbind(peer_data[i].ep_handle);
	status = GNI_EpDestroy(peer_data[i].ep_handle);
	if (status == GNI_RC_SUCCESS) {
	  peer_data[i].ep_handle = NULL;
	  left -= 1;
	}
      }
    }
    if (!left) break;
#if 0
    GASNETI_WAITHOOK();
    gasnetc_poll_local_queue();
#endif
  }
  if (left > 0) {
    gasnetc_GNIT_Log("at shutdown: %d endpoints left after 10 tries", left);
  }

  if (gasneti_attach_done) {
    status = GNI_MemDeregister(nic_handle, &my_mem_handle);
    if_pf (status != GNI_RC_SUCCESS) {
      gasnetc_GNIT_Abort("MemDeregister(segment) failed with %s", gni_return_string(status));
    }
  }

  gasneti_huge_munmap(smsg_mmap_ptr, smsg_mmap_bytes);

  status = GNI_MemDeregister(nic_handle, &my_smsg_handle);
  if_pf (status != GNI_RC_SUCCESS) {
    gasnetc_GNIT_Abort("MemDeregister(smsg_mem) failed with %s", gni_return_string(status));
  }

  if (destination_cq_handle) {
    status = GNI_CqDestroy(destination_cq_handle);
    if_pf (status != GNI_RC_SUCCESS) {
      gasnetc_GNIT_Abort("CqDestroy(dest_cq) failed with %s", gni_return_string(status));
    }
  }

  status = GNI_CqDestroy(smsg_cq_handle);
  if_pf (status != GNI_RC_SUCCESS) {
    gasnetc_GNIT_Abort("CqDestroy(smsg_cq) failed with %s", gni_return_string(status));
  }

  status = GNI_CqDestroy(bound_cq_handle);
  if_pf (status != GNI_RC_SUCCESS) {
    gasnetc_GNIT_Abort("CqDestroy(bound_cq) failed with %s", gni_return_string(status));
  }

  status = GNI_CdmDestroy(cdm_handle);
  if_pf (status != GNI_RC_SUCCESS) {
    gasnetc_GNIT_Abort("CdmDestroy(bound_cq) failed with %s", gni_return_string(status));
  }
}


GASNETI_INLINE(gasnetc_handle_am_short_packet)
int gasnetc_handle_am_short_packet(int req, gasnet_node_t source, 
			       gasnetc_am_short_packet_t *am)
{
  int handlerindex = am->header.handler;
  gasneti_handler_fn_t handler = gasnetc_handler[handlerindex];
  gasnetc_token_t the_token = { source, req };
  gasnet_token_t token = (gasnet_token_t)&the_token; /* RUN macro needs al lvalue */
  gasnet_handlerarg_t *pargs = (gasnet_handlerarg_t *) am->args;
  int numargs = am->header.numargs;
  GASNETI_RUN_HANDLER_SHORT(req, 
			    handlerindex,
			    handler,
			    token,
			    pargs,
			    numargs);
  return the_token.need_reply;
}

GASNETI_INLINE(gasnetc_handle_am_medium_packet)
int gasnetc_handle_am_medium_packet(int req, gasnet_node_t source, 
				gasnetc_am_medium_packet_t *am, void* data)
{
  int handlerindex = am->header.handler;
  gasneti_handler_fn_t handler = gasnetc_handler[handlerindex];
  gasnetc_token_t the_token = { source, req };
  gasnet_token_t token = (gasnet_token_t)&the_token; /* RUN macro needs al lvalue */
  gasnet_handlerarg_t *pargs = (gasnet_handlerarg_t *) am->args;
  int numargs = am->header.numargs;
  GASNETI_RUN_HANDLER_MEDIUM(req, 
			     handlerindex,
			     handler,
			     token,
			     pargs,
			     numargs,
			     data,
			     am->header.misc);
  return the_token.need_reply;
}

GASNETI_INLINE(gasnetc_handle_am_long_packet)
int gasnetc_handle_am_long_packet(int req, gasnet_node_t source, 
			      gasnetc_am_long_packet_t *am)
{
  int handlerindex = am->header.handler;
  gasneti_handler_fn_t handler = gasnetc_handler[handlerindex];
  gasnetc_token_t the_token = { source, req };
  gasnet_token_t token = (gasnet_token_t)&the_token; /* RUN macro needs al lvalue */
  gasnet_handlerarg_t *pargs = (gasnet_handlerarg_t *) am->args;
  int numargs = am->header.numargs;
  GASNETI_RUN_HANDLER_LONG(req, 
			   handlerindex,
			   handler,
			   token,
			   pargs,
			   numargs,
			   am->data,
			   am->data_length);
  return the_token.need_reply;
}


extern void gasnetc_handle_sys_shutdown_packet(uint32_t source, gasnetc_sys_shutdown_packet_t *sys);
extern void  gasnetc_poll_smsg_completion_queue(void);
static void gasnetc_send_credit(uint32_t pe);

void gasnetc_process_smsg_q(gasnet_node_t pe)
{
  peer_struct_t * const peer = &peer_data[pe];
  union {
    gasnetc_packet_t packet;
    uint8_t raw[GASNETC_MSG_MAXSIZE];
    uint64_t dummy_for_alignment;
  } buffer;
  for (;;) {
#if GASNETC_SMSG_GASNET
    /* TODO: Use a different/new lock since there are no longer
     *       any GNI calls in this critical section.
     *       Per-peer is possible, but requires extra serializetion
     *       be added around or in gasnetc_handle_sys_shutdown_packet.
     */
#endif

    GC_Header_t *recv_header;
    gni_return_t status;

    GASNETC_LOCK_GNI();
    status = gasnetc_smsg_get_next(peer, &recv_header);
    GASNETC_UNLOCK_GNI_IF_SEQ();
    if (status == GNI_RC_SUCCESS) {
      const uint32_t numargs = recv_header->numargs;
      const int is_req = GASNETC_CMD_IS_REQ(recv_header->command);
      const unsigned int credits = recv_header->credit + !is_req;
      int need_reply = 0;

      gasneti_assert((((uintptr_t) recv_header) & 7) == 0);
      gasneti_assert(numargs <= gasnet_AMMaxArgs());
      GASNETI_TRACE_PRINTF(D, ("smsg r from %d type %s%s\n", pe,
                               gasnetc_type_string(recv_header->command),
                               recv_header->credit ? " (+credit)" : ""));

      switch (recv_header->command) {
      case GC_CMD_AM_NOP_REPLY: {
	status = gasnetc_smsg_release(peer, recv_header);
        GASNETC_UNLOCK_GNI_IF_PAR();
	gasneti_assert(is_req == 0); /* ensure implied credit */
	gasneti_assert(need_reply == 0); /* ensure no reply is generated */
 	/* fall through to credit handling */
	break;
      }
      case GC_CMD_AM_SHORT:
      case GC_CMD_AM_SHORT_REPLY: {
	const size_t head_length = GASNETC_HEADLEN(short, numargs);
	memcpy(&buffer, recv_header, head_length);
	status = gasnetc_smsg_release(peer, recv_header);
	GASNETC_UNLOCK_GNI_IF_PAR();
	need_reply = gasnetc_handle_am_short_packet(is_req, pe, &buffer.packet.gasp);
	break;
      }
      case GC_CMD_AM_MEDIUM:
      case GC_CMD_AM_MEDIUM_REPLY: {
	const size_t head_length = GASNETC_HEADLEN(medium, numargs);
	const size_t length = head_length + recv_header->misc;
	memcpy(&buffer, recv_header, length);
	gasneti_assert(recv_header->misc <= gasnet_AMMaxMedium());
	status = gasnetc_smsg_release(peer, recv_header);
	GASNETC_UNLOCK_GNI_IF_PAR();
	need_reply = gasnetc_handle_am_medium_packet(is_req, pe, &buffer.packet.gamp, &buffer.raw[head_length]);
	break;
      }
      case GC_CMD_AM_LONG:
      case GC_CMD_AM_LONG_REPLY: {
	const size_t head_length = GASNETC_HEADLEN(long, numargs);
	memcpy(&buffer, recv_header, head_length);
	if (buffer.packet.galp.header.misc) { /* payload follows header - copy it into place */
	  void *im_data = (void *) (((uintptr_t) recv_header) + head_length);
	  memcpy(buffer.packet.galp.data, im_data, buffer.packet.galp.data_length);
	}
	status = gasnetc_smsg_release(peer, recv_header);
	GASNETC_UNLOCK_GNI_IF_PAR();
	need_reply = gasnetc_handle_am_long_packet(is_req, pe, &buffer.packet.galp);
	break;
      }
      case GC_CMD_SYS_SHUTDOWN_REQUEST: {
	memcpy(&buffer, recv_header, sizeof(buffer.packet.gssp));
	status = gasnetc_smsg_release(peer, recv_header);
	gasnetc_handle_sys_shutdown_packet(pe, &buffer.packet.gssp); /* <- run w/ lock held */
	GASNETC_UNLOCK_GNI_IF_PAR();
	gasneti_assert(is_req == 1); /* ensure no implied credit */
	gasneti_assert(need_reply == 0); /* ensure no reply is generated */
	break;
      }
      default:
	gasnetc_GNIT_Abort("unknown packet type");
      }
      if (need_reply) {
        gasnetc_send_credit(pe);
      }
      if (credits) {
        GASNETI_UNUSED_UNLESS_DEBUG int newval = 
            gasneti_weakatomic_add(&peer->am_credit, credits, GASNETI_ATOMIC_NONE);
        gasneti_assert(newval <= am_maxcredit);
      }
#if !GASNETC_SMSG_GASNET
      /* TODO: how could this really fail? */
      /* now check the SmsgRelease status */      
      if (status == GNI_RC_SUCCESS) {
	/* LCS nothing to do */
      } else {
	/* LCS SmsgRelease Failed */
	/* GNI_RC_INVALID_PARAM here means bad endpoint */
	/* GNI_RC_NOT_DONE here means there was no smsg */
	gasnetc_GNIT_Log("SmsgRelease from pe %d fail with %s",
		   pe, gni_return_string(status));
      }
#endif
    } else if (status == GNI_RC_NOT_DONE) {
      GASNETC_UNLOCK_GNI_IF_PAR();
      break;  /* GNI_RC_NOT_DONE here means there was no smsg */
    }
#if !GASNETC_SMSG_GASNET
    else {
      gasnetc_GNIT_Abort("SmsgGetNext from pe %d fail with %s", 
		 pe, gni_return_string(status));
    }
#endif
    gasnetc_poll_smsg_completion_queue();
  }
}


#define SMSG_BURST 20
#define SMSG_PEER_BURST 4

/* algorithm, imagining that several threads call this at once
 * seize the GNI lock
 *   call CqGetEvent BURST times, into a local array
 * release the GNI lock
 * if any messages found:
 * seize the smsq_work_queue lock
 *   scan the local array, adding peers to the work queue if needed
 * release the smsg work queue lock
 * check the final status from the CqGetEvents
 *   if queue overflow
 *   seize the GNI lock and drain the cq
 *   seize the smsg_work_queue_lock and enqueue everyone not already there
 * up to PEER_BURST times
 *  try to dequeue a peer and process messages from them
 */

void gasnetc_poll_smsg_completion_queue(void)
{
  gni_return_t status;
  gasnet_node_t source;
  gni_cq_entry_t event_data[SMSG_BURST];
  int messages;
  int i;

  /* grab the gni lock, then spin through SMSG_BURST calls to
   * CqGetEvent as fast as possible, saving the interpretation
   * for later.  The GNI lock is global, for all GNI api activity.
   */
  messages = 0;
  GASNETC_LOCK_GNI();
  for (;;) {
    status = GNI_CqGetEvent(smsg_cq_handle,&event_data[messages]);
    if (status != GNI_RC_SUCCESS) break;
    messages += 1;
    if (messages >= SMSG_BURST) break;
  }
  GASNETC_UNLOCK_GNI();
  /* Now run through what you found */
  if (messages > 0) {
    GASNETC_LOCK_QUEUE(&smsg_work_queue);
    for (i = 0; i < messages; i += 1) {
      source = gni_cq_get_inst_id(event_data[i]);
      gasneti_assert(source < gasneti_nodes);
      /* atomically enqueue the peer on the smsg queue if it isn't
	 already there.  */
      if (!GC_IS_QUEUED(&peer_data[source]))
	gasnetc_work_enqueue_nolock(&peer_data[source]);
    }
    GASNETC_UNLOCK_QUEUE(&smsg_work_queue);
  }
  /* Now check the final status from the CqGetEvent */
  /* the most likely case is no more messages */
  /* The next most likely case is a full BURST, ending in success */
  /* Next is queue overflow, which shouldn't happen but is recoverable */
  if (status == GNI_RC_NOT_DONE) {
    /* nothing to do */
  } else if (status == GNI_RC_SUCCESS) {
    /* nothing to do */
  } else if (status == GNI_RC_ERROR_RESOURCE) {
    /* drain the cq completely */
    GASNETC_STAT_EVENT(SMSG_CQ_OVERRUN);
    GASNETC_LOCK_GNI();
    for (;;) {
      status = GNI_CqGetEvent(smsg_cq_handle,&event_data[0]);
      if (status == GNI_RC_SUCCESS) continue;
      if (status == GNI_RC_ERROR_RESOURCE) continue;
      if (status == GNI_RC_NOT_DONE) break;
      gasnetc_GNIT_Abort("CqGetEvent(smsg_cq) drain returns error %s", gni_return_string(status));
    }
    GASNETC_UNLOCK_GNI();
    /* and enqueue everyone on the work queue, who isn't already */
    GASNETC_LOCK_QUEUE(&smsg_work_queue);
    for (source = 0; source < gasneti_nodes; source += 1) {
      if (node_is_local(source)) continue; /* no AMs for self or PSHM-reachable peers */
      if (!GC_IS_QUEUED(&peer_data[source]))
	gasnetc_work_enqueue_nolock(&peer_data[source]);
    }
    GASNETC_UNLOCK_QUEUE(&smsg_work_queue);
  } else {
    /* anything else is a fatal error */
    gasnetc_GNIT_Abort("CqGetEvent(smsg_cq) returns error %s", gni_return_string(status));
  }
}

void gasnetc_poll_smsg_queue(void)
{
  int i;
  gasnetc_poll_smsg_completion_queue();

  /* Now see about processing some peers off the smsg_work_queue */
  for (i = 0; i < SMSG_PEER_BURST; i += 1) {
    peer_struct_t *peer = gasnetc_work_dequeue();
    if (peer == NULL) break;
    gasnetc_process_smsg_q(peer->rank);
  }
}

#if GASNETC_SMSG_ARIES || GASNETC_SMSG_GASNET

static gasneti_lifo_head_t gasnetc_smsg_buffers = GASNETI_LIFO_INITIALIZER;

/* For exit code, here for use in gasnetc_free_smsg */
static gasneti_weakatomic_t shutdown_smsg_counter = gasneti_weakatomic_init(0);

GASNETI_INLINE(gasnetc_smsg_buffer)
void * gasnetc_smsg_buffer(size_t buffer_len) {
  void *result = gasneti_lifo_pop(&gasnetc_smsg_buffers);
  return result ? result : gasneti_malloc(GASNETC_MSG_MAXSIZE); /* XXX: less? */
}

GASNETI_INLINE(gasnetc_free_smsg)
void gasnetc_free_smsg(gasnetc_post_descriptor_t * const gpd)
{
  gasnetc_smsg_t * const smsg = &gpd->u.smsg;

  if (smsg->buffer) {
    gasneti_lifo_push(&gasnetc_smsg_buffers, smsg->buffer);
  } else if_pf (smsg->smsg_header.header.command == GC_CMD_SYS_SHUTDOWN_REQUEST) {
    gasneti_weakatomic_decrement(&shutdown_smsg_counter, GASNETI_ATOMIC_NONE);
  }
  gasnetc_free_post_descriptor(gpd);
}

gasnetc_smsg_t *gasnetc_alloc_smsg(void)
{
  gasnetc_post_descriptor_t *gpd = gasnetc_alloc_post_descriptor();
  return &gpd->u.smsg;
}

#endif /* GASNETC_SMSG_AREIES || GASNETC_SMSG_GASNET */

static int
#if GASNETC_SMSG_GASNET
gasnetc_send_smsg(gasnet_node_t dest, int take_lock, gasnetc_smsg_t *smsg,
                  size_t length)
#else
gasnetc_send_smsg(gasnet_node_t dest, int take_lock, gasnetc_smsg_t *smsg,
                  int header_length, void *data, int data_length)
#endif
{
  peer_struct_t * const peer = &peer_data[dest];
  gni_return_t status;
  const int max_trials = 4;
  int trial = 0;

#if GASNETC_SMSG_GASNET
  gasnetc_packet_t * const smsg_header = smsg->buffer ? smsg->buffer : &smsg->smsg_header;
  gasnetc_mailbox_t * mb;
  gasnetc_post_descriptor_t * const gpd =
          gasnetc_get_struct_addr_from_field_addr(gasnetc_post_descriptor_t, u.smsg, smsg);
  gni_post_descriptor_t * const pd = &gpd->pd;
  const uint64_t *buffer = (uint64_t *)smsg_header;
  gasneti_assert(length >= 8);
  gasneti_assert(length <= GASNETC_MSG_MAXSIZE);
#elif GASNETC_SMSG_ARIES
  gasnetc_packet_t * const smsg_header = &smsg->smsg_header;
  gasnetc_post_descriptor_t * const gpd =
          gasnetc_get_struct_addr_from_field_addr(gasnetc_post_descriptor_t, u.smsg, smsg);
  const uint32_t msgid = gpd->pd.post_id;
#else
  gasnetc_packet_t * const smsg_header = &smsg->smsg_header;
  const uint32_t msgid = 0;
#endif

  gasneti_assert(!node_is_local(dest));

  smsg_header->header.credit = gasnetc_weakatomic_swap(&peer->am_credit_bank, 0);

  GASNETI_TRACE_PRINTF(D, ("smsg to %d type %s%s\n", dest,
                           gasnetc_type_string(smsg_header->header.command),
                           smsg_header->header.credit ? " (+credit)" : ""));

#if GASNETC_SMSG_GASNET
  /*  bzero(&pd, sizeof(gni_post_descriptor_t)); */
  pd->cq_mode = GNI_CQMODE_GLOBAL_EVENT | GNI_CQMODE_REMOTE_EVENT;
  pd->dlvr_mode = GNI_DLVMODE_PERFORMANCE;
  pd->type = GNI_POST_FMA_PUT_W_SYNCFLAG;

  /* First word will be written to the remote sync flag, while rest is the Put payload */
  pd->sync_flag_value = buffer[0];
  pd->length = length - 8;
  pd->local_addr = (uint64_t) &buffer[1];
  pd->remote_mem_hndl = peer->mb.rem_hndl;

  if (take_lock) GASNETC_LOCK_GNI();
  { unsigned int slot = peer->mb.send_pos - 1;
    mb = &peer->mb.rem_addr[slot];
    peer->mb.send_pos = slot ? slot : mb_slots;
  }

  pd->sync_flag_addr = (uint64_t) &mb->full;
  pd->remote_addr = (uint64_t) (&mb->full + 1);
  for (;;) {
    status = GNI_PostFma(peer->ep_handle, pd);
    if (take_lock) GASNETC_UNLOCK_GNI();

    if_pt (status == GNI_RC_SUCCESS) {
      if_pf (trial) GASNETC_STAT_EVENT_VAL(SMSG_SEND_RETRY, trial);
      return GNI_RC_SUCCESS;
    }

    if (status != GNI_RC_ERROR_RESOURCE) {
      gasnetc_GNIT_Abort("PostFma for AM returned error %s", gni_return_string(status));
    }

    if_pf (++trial == max_trials) {
      return GASNET_ERR_RESOURCE;
    }

    GASNETI_WAITHOOK();
    if (take_lock) {
      gasnetc_poll_local_queue();
      GASNETC_LOCK_GNI();
    }
  }
#else
  for (;;) {
    if (take_lock) GASNETC_LOCK_GNI();
    status = GNI_SmsgSend(peer->ep_handle,
                          smsg_header, header_length,
                          data, data_length, msgid);
    if (take_lock) GASNETC_UNLOCK_GNI();

    if_pt (status == GNI_RC_SUCCESS) {
      if_pf (trial) GASNETC_STAT_EVENT_VAL(SMSG_SEND_RETRY, trial);
      return GNI_RC_SUCCESS;
    }

    if (status != GNI_RC_NOT_DONE) {
      gasnetc_GNIT_Abort("GNI_SmsgSend returned error %s", gni_return_string(status));
    }

    if_pf (++trial == max_trials) {
      return GASNET_ERR_RESOURCE;
    }

    GASNETI_WAITHOOK();
    if (take_lock) gasnetc_poll_local_queue();
  }
#endif

  return GASNET_OK;
}

int
gasnetc_send_am(gasnet_node_t dest, 
                  gasnetc_smsg_t *smsg, int header_length, 
                  void *data, int data_length, int do_copy)
{
  gasnetc_packet_t * const smsg_header = &smsg->smsg_header;
  const size_t total_len = header_length + data_length;

#if GASNETC_SMSG_GASNET
  smsg->buffer = NULL;
  if (data_length) {
    const size_t imm_limit = GASNETC_GNI_IMMEDIATE_BOUNCE_SIZE - offsetof(gasnetc_smsg_t,smsg_header);
    uint8_t * buffer = (uint8_t*) smsg_header;
    if (total_len > imm_limit) {
      buffer = gasnetc_smsg_buffer(total_len);
      memcpy(buffer, smsg_header, header_length);
      smsg->buffer = buffer;
    }
    memcpy(buffer + header_length, data, data_length);
  }
  return gasnetc_send_smsg(dest, 1, smsg, total_len);
#elif GASNETC_SMSG_ARIES
  smsg->buffer = NULL;
  if (do_copy && data_length) {
    const size_t imm_limit = GASNETC_GNI_IMMEDIATE_BOUNCE_SIZE - offsetof(gasnetc_smsg_t,smsg_header);
    void * buffer;
    if (total_len > imm_limit) {
      buffer = gasnetc_smsg_buffer(data_length);
      smsg->buffer = buffer;
    } else {
      buffer = (void*) ((uintptr_t)smsg_header + header_length);
    }
    memcpy(buffer, data, data_length);
  }
  return gasnetc_send_smsg(dest, 1,
                           smsg, header_length,
                           data, data_length);
#else
  return gasnetc_send_smsg(dest, 1,
                           smsg, header_length,
                           data, data_length);
#endif
}


GASNETI_NEVER_INLINE(gasnetc_poll_local_queue,
void gasnetc_poll_local_queue(void))
{
  gni_return_t status;
  gni_cq_entry_t event_data;
  int i;
  gni_post_descriptor_t *pd;
  gasnetc_post_descriptor_t *gpd;

  GASNETC_LOCK_GNI(); /* TODO: can/should we reduce length of this critical section? */
  for (i = 0; i < gasnetc_poll_burst; i += 1) {
    /* Poll the bound_ep completion queue */
    status = GNI_CqGetEvent(bound_cq_handle,&event_data);
    if_pt (status == GNI_RC_SUCCESS) {
      gasneti_assert(!GNI_CQ_OVERRUN(event_data));

#if GASNETC_SMSG_ARIES
      /* first handle completion of message sends */
      if (GNI_CQ_GET_TYPE(event_data) == GNI_CQ_EVENT_TYPE_SMSG) {
        uint32_t msgid = GNI_CQ_GET_MSG_ID(event_data);
        gasnetc_post_descriptor_t * const gpd =
                msgid + (gasnetc_post_descriptor_t *) gasnetc_pd_buffers.addr;
        gasneti_assert(gpd->pd.post_id == msgid);
        gasnetc_free_smsg(gpd);
	continue;
      }
#endif

      status = GNI_GetCompleted(bound_cq_handle, event_data, &pd);
      if (status != GNI_RC_SUCCESS)
	gasnetc_GNIT_Abort("GetCompleted(%p) failed %s",
		   (void *) event_data, gni_return_string(status));
      gpd = gasnetc_get_struct_addr_from_field_addr(gasnetc_post_descriptor_t, pd, pd);

#if GASNETC_SMSG_GASNET
      /* first handle completion of message sends */
      if (pd->type == GNI_POST_FMA_PUT_W_SYNCFLAG) {
        /* TODO: textually inline, possibly using a gpd->flags value to distinguish? */
        gasnetc_free_smsg(gpd);
	continue;
      }
#endif

      /* handle remaining work */
      if (gpd->flags & GC_POST_SEND) {
#if GASNETC_SMSG_GASNET
        gasnetc_smsg_t *smsg = gpd->u.smsg_p;
        gasnetc_am_long_packet_t * const galp = &smsg->smsg_header.galp;
        int rc = gasnetc_send_smsg(gpd->dest, 0, smsg,
                                   GASNETC_HEADLEN(long, galp->header.numargs));
#elif GASNETC_SMSG_ARIES
        gasnetc_smsg_t *smsg = gpd->u.smsg_p;
        gasnetc_am_long_packet_t * const galp = &smsg->smsg_header.galp;
        int rc = gasnetc_send_smsg(gpd->dest, 0, smsg,
                                   GASNETC_HEADLEN(long, galp->header.numargs),
                                   NULL, 0);
#else
        gasnetc_smsg_t *smsg = &gpd->u.smsg;
        gasnetc_am_long_packet_t * const galp = &smsg->smsg_header.galp;
        int rc = gasnetc_send_smsg(gpd->dest, 0, smsg,
                                   GASNETC_HEADLEN(long, galp->header.numargs),
                                   NULL, 0);
#endif
        gasneti_assert_always (rc == GASNET_OK);
      } else if (gpd->flags & GC_POST_COPY) {
        void * const buffer = gpd->bounce_buffer;
        size_t length = gpd->pd.length - (gpd->flags & GC_POST_COPY_TRIM);
	memcpy(gpd->get_target, gpd->bounce_buffer, length);
	gpd->bounce_buffer = (void*)(~3 & (uintptr_t)buffer); /* fixup for possible UNBOUNCE */
      }

      /* indicate completion */
      if (gpd->flags & GC_POST_COMPLETION_FLAG) {
	gasneti_weakatomic_set(gpd->completion.flag, 1, 0);
      } else if(gpd->flags & GC_POST_COMPLETION_OP) {
	gasnete_op_markdone(gpd->completion.op, (gpd->flags & GC_POST_GET) != 0);
      }

      /* release resources */
      if (gpd->flags & GC_POST_UNREGISTER) {
	status = GNI_MemDeregister(nic_handle, &gpd->pd.local_mem_hndl);
	gasneti_assert_always (status == GNI_RC_SUCCESS);
	gasnetc_return_reg_credit();
      } else if (gpd->flags & GC_POST_UNBOUNCE) {
	gasnetc_free_bounce_buffer(gpd->bounce_buffer);
      }
      if (!(gpd->flags & GC_POST_KEEP_GPD)) {
        gasnetc_free_post_descriptor(gpd);
      }
    } else if (status == GNI_RC_NOT_DONE) {
      break;
    } else if (!gasnetc_shutdownInProgress) {
      gasnetc_GNIT_Log("bound CqGetEvent %s", gni_return_string(status));
    }
  }
  GASNETC_UNLOCK_GNI();
}
  
void gasnetc_poll(void)
{
  gasnetc_poll_smsg_queue();
  gasnetc_poll_local_queue();
}

GASNETI_INLINE(gasnetc_send_credit)
void gasnetc_send_credit(uint32_t pe)
{
  /* bank_credits = 0: all credits result in a smsg sent
   * bank_credits = 1: send only if 1 credit is alrady banked, otherwise bank this one
   */
  if (!bank_credits || gasnetc_weakatomic_swap(&peer_data[pe].am_credit_bank, 1)) {
    GASNETC_DECL_SMSG(smsg);
    gasnetc_am_nop_packet_t *ganp = &smsg->smsg_header.ganp;
    int rc;

    ganp->header.command = GC_CMD_AM_NOP_REPLY;
  #if GASNET_DEBUG
    ganp->header.numargs = 0;
  #endif

  #if GASNETC_SMSG_GASNET || GASNETC_SMSG_ARIES
    smsg->buffer = NULL;
  #endif

  #if GASNETC_SMSG_GASNET
    rc = gasnetc_send_smsg(pe, 1, smsg, MAX(8, sizeof(gasnetc_am_nop_packet_t)));
  #else
    rc = gasnetc_send_smsg(pe, 1, smsg, sizeof(gasnetc_am_nop_packet_t),
                           NULL, 0);
  #endif

    if_pf (rc) {
      gasnetc_GNIT_Abort("Failed to return AM implicit credit");
    }
  }
}

GASNETI_NEVER_INLINE(print_post_desc,
static void print_post_desc(const char *title, gni_post_descriptor_t *cmd)) {
  const int in_seg = gasneti_in_segment(gasneti_mynode, (void *) cmd->local_addr, cmd->length);
  printf("r %d %s-segment %s, desc addr %p\n", gasneti_mynode, (in_seg?"in":"non"), title, cmd);
  printf("r %d status: %ld\n", gasneti_mynode, cmd->status);
  printf("r %d cq_mode_complete: 0x%x\n", gasneti_mynode, cmd->cq_mode_complete);
  printf("r %d type: %d (%s)\n", gasneti_mynode, cmd->type, gasnetc_post_type_string(cmd->type));
  printf("r %d cq_mode: 0x%x\n", gasneti_mynode, cmd->cq_mode);
  printf("r %d dlvr_mode: 0x%x\n", gasneti_mynode, cmd->dlvr_mode);
  printf("r %d local_address: %p(0x%lx, 0x%lx)\n", gasneti_mynode, (void *) cmd->local_addr, 
	 cmd->local_mem_hndl.qword1, cmd->local_mem_hndl.qword2);
  printf("r %d remote_address: %p(0x%lx, 0x%lx)\n", gasneti_mynode, (void *) cmd->remote_addr, 
	 cmd->remote_mem_hndl.qword1, cmd->remote_mem_hndl.qword2);
  printf("r %d length: 0x%lx\n", gasneti_mynode, cmd->length);
  printf("r %d rdma_mode: 0x%x\n", gasneti_mynode, cmd->rdma_mode);
  printf("r %d src_cq_hndl: %p\n", gasneti_mynode, cmd->src_cq_hndl);
  printf("r %d sync: (0x%lx,0x%lx)\n", gasneti_mynode, cmd->sync_flag_value, cmd->sync_flag_addr);
  printf("r %d amo_cmd: %d\n", gasneti_mynode, cmd->amo_cmd);
  printf("r %d amo: 0x%lx, 0x%lx\n", gasneti_mynode, cmd->first_operand, cmd->second_operand);
  printf("r %d cqwrite_value: 0x%lx\n", gasneti_mynode, cmd->cqwrite_value);
}

static gni_return_t myPostRdma(gni_ep_handle_t ep, gni_post_descriptor_t *pd)
{
  gni_return_t status;
  const int max_trials = 1000;
  int trial = 0;

  do {
      GASNETC_LOCK_GNI();
      status = GNI_PostRdma(ep, pd);
      GASNETC_UNLOCK_GNI();
      if_pt (status == GNI_RC_SUCCESS) {
        if (trial) GASNETC_STAT_EVENT_VAL(POST_RDMA_RETRY, trial);
        return GNI_RC_SUCCESS;
      }
      if (status != GNI_RC_ERROR_RESOURCE) break; /* Fatal */
      GASNETI_WAITHOOK();
      gasnetc_poll_local_queue();
  } while (++trial < max_trials);
  if (status == GNI_RC_ERROR_RESOURCE) {
    gasnetc_GNIT_Log("PostRdma retry failed");
  }
  return status;
}

static gni_return_t myPostFma(gni_ep_handle_t ep, gni_post_descriptor_t *pd)
{
  gni_return_t status;
  const int max_trials = 1000;
  int trial = 0;

  do {
      GASNETC_LOCK_GNI();
      status = GNI_PostFma(ep, pd);
      GASNETC_UNLOCK_GNI();
      if_pt (status == GNI_RC_SUCCESS) {
        if (trial) GASNETC_STAT_EVENT_VAL(POST_FMA_RETRY, trial);
        return GNI_RC_SUCCESS;
      }
      if (status != GNI_RC_ERROR_RESOURCE) break; /* Fatal */
      GASNETI_WAITHOOK();
      gasnetc_poll_local_queue();
  } while (++trial < max_trials);
  if (status == GNI_RC_ERROR_RESOURCE) {
    gasnetc_GNIT_Log("PostFma retry failed");
  }
  return status;
}

/* Register local side of a pd, with bounded retry */
static gni_return_t myRegisterPd(gni_post_descriptor_t *pd)
{
  const uint64_t addr = pd->local_addr;
  const size_t nbytes = pd->length;
  const int max_trials = 10;
  int trial = 0;
  gni_return_t status;

  if_pf (!gasnetc_alloc_reg_credit()) {
    /* We may simple not have polled the Cq recently.
       So, WAITHOOK and STALL tracing only if still nothing after first poll */
    GASNETC_TRACE_WAIT_BEGIN();
    int stall = 0;
    goto first;
    do {
      GASNETI_WAITHOOK();
      stall = 1;
first:
      gasnetc_poll_local_queue();
    } while (!gasnetc_alloc_reg_credit());
    if_pf (stall) GASNETC_TRACE_WAIT_END(MEM_REG_STALL);
  }

  do {
    GASNETC_LOCK_GNI();
    status = GNI_MemRegister(nic_handle, addr, nbytes, NULL,
                             gasnetc_memreg_flags, -1, &pd->local_mem_hndl);
    GASNETC_UNLOCK_GNI();
    if_pt (status == GNI_RC_SUCCESS) {
      if (trial) GASNETC_STAT_EVENT_VAL(MEM_REG_RETRY, trial);
      return GNI_RC_SUCCESS;
    }
    if (status != GNI_RC_ERROR_RESOURCE) break; /* Fatal */
    GASNETI_WAITHOOK();
  } while (++trial < max_trials);
  if (status == GNI_RC_ERROR_RESOURCE) {
    gasnetc_GNIT_Log("MemRegister retry failed");
  }
  return status;
}

/* Perform an rdma/fma Put with no concern for local completion */
void gasnetc_rdma_put_bulk(gasnet_node_t dest,
		 void *dest_addr, void *source_addr,
		 size_t nbytes, gasnetc_post_descriptor_t *gpd)
{
  peer_struct_t * const peer = &peer_data[dest];
  gni_post_descriptor_t * const pd = &gpd->pd;
  gni_return_t status;

  gasneti_assert(!node_is_local(dest));

  /*  bzero(&pd, sizeof(gni_post_descriptor_t)); */
  pd->cq_mode = GNI_CQMODE_GLOBAL_EVENT;
  pd->dlvr_mode = GNI_DLVMODE_PERFORMANCE;
  pd->remote_addr = (uint64_t) dest_addr;
  pd->remote_mem_hndl = peer->mem_handle;
  pd->length = nbytes;

  /* confirm that the destination is in-segment on the far end */
  gasneti_boundscheck(dest, dest_addr, nbytes);

  /* Start with defaults suitable for FMA or in-segment case */
  pd->local_addr = (uint64_t) source_addr;
  pd->local_mem_hndl = my_mem_handle;

  if (nbytes <= gasnetc_fma_rdma_cutover) {
    /* Small enough for FMA - no local memory registration is required */
    pd->type = GNI_POST_FMA_PUT;
#if FIX_HT_ORDERING
    pd->cq_mode = gasnetc_fma_put_cq_mode;
#endif
    status = myPostFma(peer->ep_handle, pd);
  } else { /* Using RDMA, which requires local memory registration */
    if_pf (!gasneti_in_segment(gasneti_mynode, source_addr, nbytes)) {
      /* Use a bounce buffer or mem-reg according to size.
       * Use of gpd->u.immedate would only be reachable if
       *     (fma_rdma_cutover < IMMEDIATE_BOUNCE_SIZE),
       * which is not the default (nor recommended).
       */
      if (nbytes <= gasnetc_bounce_register_cutover) {
        gpd->flags |= GC_POST_UNBOUNCE;
        gpd->bounce_buffer = gasnetc_alloc_bounce_buffer();
        memcpy(gpd->bounce_buffer, source_addr, nbytes);
        pd->local_addr = (uint64_t) gpd->bounce_buffer;
      } else {
        gpd->flags |= GC_POST_UNREGISTER;
        status = myRegisterPd(pd);
        gasneti_assert_always (status == GNI_RC_SUCCESS);
      }
    }
    pd->type = GNI_POST_RDMA_PUT;
    status = myPostRdma(peer->ep_handle, pd);
  }

  if_pf (status != GNI_RC_SUCCESS) {
    print_post_desc("Put", pd);
    gasnetc_GNIT_Abort("Put failed with %s", gni_return_string(status));
  }
}

/* Perform an rdma/fma Put which favors rapid local completion
 * The return value is boolean, where 1 means locally complete.
 */
int gasnetc_rdma_put(gasnet_node_t dest,
		 void *dest_addr, void *source_addr,
		 size_t nbytes, gasnetc_post_descriptor_t *gpd)
{
  peer_struct_t * const peer = &peer_data[dest];
  gni_post_descriptor_t * const pd = &gpd->pd;
  gni_return_t status;
  int result = 1; /* assume local completion */

  gasneti_assert(!node_is_local(dest));

  /*  bzero(&pd, sizeof(gni_post_descriptor_t)); */
  pd->cq_mode = GNI_CQMODE_GLOBAL_EVENT;
  pd->dlvr_mode = GNI_DLVMODE_PERFORMANCE;
  pd->remote_addr = (uint64_t) dest_addr;
  pd->remote_mem_hndl = peer->mem_handle;
  pd->length = nbytes;

  /* confirm that the destination is in-segment on the far end */
  gasneti_boundscheck(dest, dest_addr, nbytes);

  /* Start with defaults suitable for FMA or in-segment case */
  pd->local_addr = (uint64_t) source_addr;
  pd->local_mem_hndl = my_mem_handle;

#if GASNET_CONDUIT_GEMINI
  /* On Gemini (only) return from PostFma follows local completion. */
  if (nbytes <= gasnetc_fma_rdma_cutover) {
    /* Small enough for FMA - no local memory registration is required */
    pd->type = GNI_POST_FMA_PUT;
  #if FIX_HT_ORDERING
    pd->cq_mode = gasnetc_fma_put_cq_mode;
  #endif
    status = myPostFma(peer->ep_handle, pd);
  } else
#endif
  { /* Favor bounce buffers if possible */
  #if !GASNET_CONDUIT_GEMINI /* On Gemini the FMA path above would be selected instead */
    if (nbytes <= GASNETC_GNI_IMMEDIATE_BOUNCE_SIZE) {
      gpd->bounce_buffer = gpd->u.immediate;
      memcpy(gpd->bounce_buffer, source_addr, nbytes);
      pd->local_addr = (uint64_t) gpd->bounce_buffer;
    } else
  #endif
    if (nbytes <= gasnetc_bounce_register_cutover) {
      gpd->flags |= GC_POST_UNBOUNCE;
      gpd->bounce_buffer = gasnetc_alloc_bounce_buffer();
      memcpy(gpd->bounce_buffer, source_addr, nbytes);
      pd->local_addr = (uint64_t) gpd->bounce_buffer;
    } else {
      result = 0;
      if_pf (!gasneti_in_segment(gasneti_mynode, source_addr, nbytes)) {
        /* source not in segment */
        gpd->flags |= GC_POST_UNREGISTER;
        status = myRegisterPd(pd);
        gasneti_assert_always (status == GNI_RC_SUCCESS);
      }
    }
 
#if !GASNET_CONDUIT_GEMINI
    /*  TODO: distinct Put and Get cut-overs */
    if (nbytes <= gasnetc_fma_rdma_cutover) {
      pd->type = GNI_POST_FMA_PUT;
    #if FIX_HT_ORDERING
      pd->cq_mode = gasnetc_fma_put_cq_mode;
    #endif
      status = myPostFma(peer->ep_handle, pd);
    } else
#endif
    {
      pd->type = GNI_POST_RDMA_PUT;
      status = myPostRdma(peer->ep_handle, pd);
    }
  }

  if_pf (status != GNI_RC_SUCCESS) {
    print_post_desc("Put", pd);
    gasnetc_GNIT_Abort("Put failed with %s", gni_return_string(status));
  }

  gasneti_assert((result == 0) || (result == 1)); /* ensures caller can use "&=" or "+=" */
  return result;
}

/* FMA Put from the bounce_buffer field of the gpd */
void gasnetc_rdma_put_buff(gasnet_node_t dest, void *dest_addr,
		size_t nbytes, gasnetc_post_descriptor_t *gpd)
{
  peer_struct_t * const peer = &peer_data[dest];
  gni_post_descriptor_t * const pd = &gpd->pd;
  gni_return_t status;

  gasneti_assert(!node_is_local(dest));

  /* confirm that the destination is in-segment on the far end */
  gasneti_boundscheck(dest, dest_addr, nbytes);

  /*  bzero(&pd, sizeof(gni_post_descriptor_t)); */
  pd->cq_mode = GNI_CQMODE_GLOBAL_EVENT;
  pd->dlvr_mode = GNI_DLVMODE_PERFORMANCE;
  pd->remote_addr = (uint64_t) dest_addr;
  pd->remote_mem_hndl = peer->mem_handle;
  pd->length = nbytes;
  pd->local_addr = (uint64_t) gpd->bounce_buffer;

  /* now initiate always using FMA */
  pd->type = GNI_POST_FMA_PUT;
#if FIX_HT_ORDERING
  pd->cq_mode = gasnetc_fma_put_cq_mode;
#endif
  status = myPostFma(peer->ep_handle, pd);

  if_pf (status != GNI_RC_SUCCESS) {
    print_post_desc("Put", pd);
    gasnetc_GNIT_Abort("Put failed with %s", gni_return_string(status));
  }
}

/* initiate a Get according to fma/rdma cutover */
GASNETI_INLINE(gasnetc_post_get)
void gasnetc_post_get(gni_ep_handle_t ep, gni_post_descriptor_t *pd)
{
  gni_return_t status;
  const size_t nbytes = pd->length;

  /*  TODO: distinct Put and Get cut-overs */
  if (nbytes <= gasnetc_fma_rdma_cutover) {
      pd->type = GNI_POST_FMA_GET;
      status = myPostFma(ep, pd);
  } else {
      pd->type = GNI_POST_RDMA_GET;
      status = myPostRdma(ep, pd);
  }

  if_pf (status != GNI_RC_SUCCESS) {
    print_post_desc("Get", pd);
    gasnetc_GNIT_Abort("Get failed with %s", gni_return_string(status));
  }
}

/* for get, source_addr is remote */
void gasnetc_rdma_get(gasnet_node_t dest,
		 void *dest_addr, void *source_addr,
		 size_t nbytes, gasnetc_post_descriptor_t *gpd)
{
  peer_struct_t * const peer = &peer_data[dest];
  gni_post_descriptor_t * const pd = &gpd->pd;
  gni_return_t status;

  gasneti_assert(!node_is_local(dest));

  gpd->flags |= GC_POST_GET;

  /*  bzero(&pd, sizeof(gni_post_descriptor_t)); */
  pd->cq_mode = GNI_CQMODE_GLOBAL_EVENT;
  pd->dlvr_mode = GNI_DLVMODE_PERFORMANCE;
  pd->remote_addr = (uint64_t) source_addr;
  pd->remote_mem_hndl = peer->mem_handle;
  pd->length = nbytes;

  /* confirm that the source is in-segment on the far end */
  gasneti_boundscheck(dest, source_addr, nbytes);

  /* Start with defaults suitable for in-segment case */
  pd->local_addr = (uint64_t) dest_addr;
  pd->local_mem_hndl = my_mem_handle;

  /* check where the local addr is */
  if_pf (!gasneti_in_segment(gasneti_mynode, dest_addr, nbytes)) {
    /* dest not (entirely) in segment */
    /* if (nbytes <= gasnetc_bounce_register_cutover)  then use bounce buffer
     * else mem-register
     */
    if (nbytes < GASNETC_GNI_IMMEDIATE_BOUNCE_SIZE) {
      gpd->flags |= GC_POST_COPY;
      gpd->bounce_buffer = gpd->u.immediate;
      gpd->get_target = dest_addr;
      pd->local_addr = (uint64_t) gpd->bounce_buffer;
    } else if (nbytes <= gasnetc_bounce_register_cutover) {
      gpd->flags |= GC_POST_UNBOUNCE | GC_POST_COPY;
      gpd->bounce_buffer = gasnetc_alloc_bounce_buffer();
      gpd->get_target = dest_addr;
      pd->local_addr = (uint64_t) gpd->bounce_buffer;
    } else {
      gpd->flags |= GC_POST_UNREGISTER;
      status = myRegisterPd(pd);
      gasneti_assert_always (status == GNI_RC_SUCCESS);
    }
  }

  gasnetc_post_get(peer->ep_handle, pd);
}

/* for get in which one or more of dest_addr, source_addr or nbytes is NOT divisible by 4 */
void gasnetc_rdma_get_unaligned(gasnet_node_t dest,
		 void *dest_addr, void *source_addr,
		 size_t nbytes, gasnetc_post_descriptor_t *gpd)
{
  peer_struct_t * const peer = &peer_data[dest];
  gni_post_descriptor_t * const pd = &gpd->pd;
  char * buffer;

  /* Compute length of "overfetch" required, if any */
  unsigned int pre = (uintptr_t) source_addr & 3;
  size_t       length = GASNETI_ALIGNUP(nbytes + pre, 4);
  unsigned int overfetch = length - nbytes;

  gasneti_assert(!node_is_local(dest));

  gasneti_assert(0 == (overfetch & ~GC_POST_COPY_TRIM));
  gpd->flags |= GC_POST_GET | GC_POST_COPY | overfetch;
  gpd->get_target = dest_addr;

  /*  bzero(&pd, sizeof(gni_post_descriptor_t)); */
  pd->cq_mode = GNI_CQMODE_GLOBAL_EVENT;
  pd->dlvr_mode = GNI_DLVMODE_PERFORMANCE;
  pd->remote_addr = (uint64_t) source_addr - pre;
  pd->remote_mem_hndl = peer->mem_handle;
  pd->length = length;
  pd->local_mem_hndl = my_mem_handle;

  /* confirm that the source is in-segment on the far end */
  gasneti_boundscheck(dest, (void*)pd->remote_addr, pd->length);

  /* must always use immediate or bounce buffer */
  if (length < GASNETC_GNI_IMMEDIATE_BOUNCE_SIZE) {
    buffer = gpd->u.immediate;
  } else if (length <= gasnetc_bounce_register_cutover) {
    gpd->flags |= GC_POST_UNBOUNCE;
    buffer = gasnetc_alloc_bounce_buffer();
  } else {
    gasneti_fatalerror("get_unaligned called with nbytes too large for bounce buffers");
  }

  pd->local_addr = (uint64_t) buffer;
  gpd->bounce_buffer = buffer + pre;

  gasnetc_post_get(peer->ep_handle, pd);
}

/* Get into bounce_buffer indicated in the gpd
   Will overfetch if source_addr or nbytes are not 4-byte aligned
   but expects (does not check) that the buffer is aligned.
   Caller must be allow for space (upto 6 bytes) for the overfetch.
   Returns offset to start of data after adjustment for overfetch
 */
int gasnetc_rdma_get_buff(gasnet_node_t dest, void *source_addr,
		size_t nbytes, gasnetc_post_descriptor_t *gpd)
{
  peer_struct_t * const peer = &peer_data[dest];
  gni_post_descriptor_t * const pd = &gpd->pd;
  gni_return_t status;

  /* Compute length of "overfetch" required, if any */
  unsigned int pre = (uintptr_t) source_addr & 3;
  size_t       length = GASNETI_ALIGNUP(nbytes + pre, 4);
  unsigned int overfetch = length - nbytes;

  gasneti_assert(!node_is_local(dest));
  gasneti_assert(nbytes  <= GASNETC_GNI_IMMEDIATE_BOUNCE_SIZE);

  /* confirm that the source is in-segment on the far end */
  gasneti_boundscheck(dest, source_addr, nbytes);

  /*  bzero(&pd, sizeof(gni_post_descriptor_t)); */
  pd->cq_mode = GNI_CQMODE_GLOBAL_EVENT;
  pd->dlvr_mode = GNI_DLVMODE_PERFORMANCE;
  pd->remote_addr = (uint64_t) source_addr - pre;
  pd->remote_mem_hndl = peer->mem_handle;
  pd->length = length;
  pd->local_addr = (uint64_t) gpd->bounce_buffer;
  pd->local_mem_hndl = my_mem_handle;

  /* now initiate - *always* FMA for now */
  pd->type = GNI_POST_FMA_GET;
  status = myPostFma(peer->ep_handle, pd);

  if_pf (status != GNI_RC_SUCCESS) {
    print_post_desc("Get", pd);
    gasnetc_GNIT_Abort("Get failed with %s", gni_return_string(status));
  }

  return pre;
}


void gasnetc_get_am_credit(uint32_t pe)
{
  gasneti_weakatomic_t *p = &peer_data[pe].am_credit;
  if_pf (!gasnetc_weakatomic_dec_if_positive(p)) {
    GASNETC_TRACE_WAIT_BEGIN();
    do {
      GASNETI_WAITHOOK();
      gasneti_AMPoll();
    } while (!gasnetc_weakatomic_dec_if_positive(p));
    GASNETC_TRACE_WAIT_END(GET_AM_CREDIT_STALL);
  }
}

static gasneti_lifo_head_t post_descriptor_pool = GASNETI_LIFO_INITIALIZER;

/* Needs no lock because it is called only from the init code */
void gasnetc_init_post_descriptor_pool(void)
{
  int i;
  const int count = gasnetc_pd_buffers.size / sizeof(gasnetc_post_descriptor_t);
  gasnetc_post_descriptor_t *data = gasnetc_pd_buffers.addr;
  gasneti_assert_always(data);
  memset(data, 0, gasnetc_pd_buffers.size); /* Just in case */
  for (i = 0; i < count; i += 1) {
  #if GASNETC_SMSG_ARIES
    data[i].pd.post_id = i;
  #endif
    gasneti_lifo_push(&post_descriptor_pool, &data[i]);
  }
}

/* This needs no lock because there is an internal lock in the queue */
gasnetc_post_descriptor_t *gasnetc_alloc_post_descriptor(void)
{
  gasnetc_post_descriptor_t *gpd =
            (gasnetc_post_descriptor_t *) gasneti_lifo_pop(&post_descriptor_pool);
  if_pf (!gpd) {
    /* We may simple not have polled the Cq recently.
       So, WAITHOOK and STALL tracing only if still nothing after first poll */
    GASNETC_TRACE_WAIT_BEGIN();
    int stall = 0;
    goto first;
    do {
      GASNETI_WAITHOOK();
      stall = 1;
first:
      gasnetc_poll_local_queue();
      gpd = (gasnetc_post_descriptor_t *) gasneti_lifo_pop(&post_descriptor_pool);
    } while (!gpd);
    if_pf (stall) GASNETC_TRACE_WAIT_END(ALLOC_PD_STALL);
  }
  return(gpd);
}


/* This needs no lock because there is an internal lock in the queue */
/* LCS inline this */
void gasnetc_free_post_descriptor(gasnetc_post_descriptor_t *gpd)
{
  gasneti_lifo_push(&post_descriptor_pool, gpd);
}

/* exit related */
volatile int gasnetc_shutdownInProgress = 0;
double gasnetc_shutdown_seconds = 0.0;
static double shutdown_max = 120.;  /* 2 minutes */
static uint32_t sys_exit_rcvd = 0;

#if GASNET_PSHM
gasnetc_exitcode_t *gasnetc_exitcodes = NULL;
#endif

extern void gasnetc_sys_SendShutdownMsg(gasnet_node_t peeridx, int shift, int exitcode)
{
#if GASNETC_SMSG_GASNET || GASNETC_SMSG_ARIES
  gasnetc_smsg_t *smsg;
#else
  static gasnetc_smsg_t shutdown_smsg;
  gasnetc_smsg_t *smsg = &shutdown_smsg;
#endif
#if GASNET_PSHM
  const gasnet_node_t dest = gasneti_pshm_firsts[peeridx];
#else
  const gasnet_node_t dest = peeridx;
#endif
  gasnetc_sys_shutdown_packet_t *gssp;
  int result;

#if GASNETC_SMSG_GASNET || GASNETC_SMSG_ARIES
  if (0 == gasnetc_pd_buffers.addr) {
    /* If in exit before attach, must populate gdp freelist */
    const int count = 32; /* XXX: any reason to economize? */
    gasnetc_pd_buffers.addr = gasneti_calloc(count, sizeof(gasnetc_post_descriptor_t));
    gasnetc_pd_buffers.size = count * sizeof(gasnetc_post_descriptor_t);
    gasnetc_init_post_descriptor_pool();
  }
  smsg = gasnetc_alloc_smsg();
  gasneti_weakatomic_increment(&shutdown_smsg_counter, GASNETI_ATOMIC_NONE);
#endif

  GASNETI_TRACE_PRINTF(C,("Send SHUTDOWN Request to node %d w/ shift %d, exitcode %d",dest,shift,exitcode));
  gssp = &smsg->smsg_header.gssp;
  gssp->header.command = GC_CMD_SYS_SHUTDOWN_REQUEST;
  gssp->header.misc    = exitcode; /* only 14 bits, but exit() only preserves low 8-bits anyway */
#if GASNET_DEBUG
  gssp->header.numargs = 0;
#endif
  gssp->header.handler = shift; /* log(distance) */
#if GASNETC_SMSG_GASNET || GASNETC_SMSG_ARIES
  smsg->buffer = NULL;
#endif

  gasnetc_get_am_credit(dest);

#if GASNETC_SMSG_GASNET
  result = gasnetc_send_smsg(dest, 1, smsg, MAX(8, sizeof(gasnetc_sys_shutdown_packet_t)));
#else
  result = gasnetc_send_smsg(dest, 1,
                             smsg, sizeof(gasnetc_sys_shutdown_packet_t),
                             NULL, 0);
#endif

#if GASNET_DEBUG
  if_pf (result) {
    gasnetc_GNIT_Log("WARNING: gasnetc_send_smsg() call at Shutdown failed");
  }
#endif
}


/* this is called from poll when a shutdown packet arrives */
/* NOTE: serialized by GNI_LOCK in caller */
void gasnetc_handle_sys_shutdown_packet(uint32_t source, gasnetc_sys_shutdown_packet_t *sys)
{
  uint32_t distance = 1 << sys->header.handler;
  uint8_t exitcode = sys->header.misc;
  uint8_t oldcode;
#if GASNET_DEBUG || GASNETI_STATS_OR_TRACE
  int sender = source;
  GASNETI_TRACE_PRINTF(C,("Got SHUTDOWN Request from node %d w/ exitcode %d",sender,exitcode));
#endif
  oldcode = gasneti_atomic_read((gasneti_atomic_t *) &gasnetc_exitcode, 0);
  if (exitcode > oldcode) {
    gasneti_atomic_set((gasneti_atomic_t *) &gasnetc_exitcode, exitcode, 0);
  } else {
    exitcode = oldcode;
  }
  sys_exit_rcvd |= distance;
}


/* Reduction (op=MAX) over exitcodes using dissemination pattern.
   Returns 0 on sucess, or non-zero on error (timeout).
 */
extern int gasnetc_sys_exit(int *exitcode_p)
{
#if GASNET_PSHM                  
  const gasnet_node_t size = gasneti_nodemap_global_count;
  const gasnet_node_t rank = gasneti_nodemap_global_rank;
#else
  const gasnet_node_t size = gasneti_nodes;
  const gasnet_node_t rank = gasneti_mynode;
#endif
  uint32_t goal = 0;
  uint32_t distance;
  int result = 0; /* success */
  int exitcode = *exitcode_p;
  int oldcode;
  int shift;
  gasneti_tick_t timeout_us = 1e6 * gasnetc_shutdown_seconds;
  gasneti_tick_t starttime = gasneti_ticks_now();
  const int pre_attach = !gasneti_attach_done;

  GASNETI_TRACE_PRINTF(C,("Entering SYS EXIT"));

#if GASNET_PSHM
  if (gasneti_nodemap_local_rank) {
    gasnetc_exitcode_t * const self = &gasnetc_exitcodes[gasneti_nodemap_local_rank];
    gasnetc_exitcode_t * const lead = &gasnetc_exitcodes[0];

    /* publish our exit code for leader to read */
    self->exitcode = exitcode;
    gasneti_local_wmb(); /* Release */
    self->present = 1;

    /* wait for leader to publish final result */
    while (! lead->present) {
      gasnetc_poll();
      if (gasneti_ticks_to_us(gasneti_ticks_now() - starttime) > timeout_us) {
        result = 1; /* failure */
        goto out;
      }
    }
    gasneti_local_rmb(); /* Acquire */
    exitcode = lead->exitcode;

    goto out;
  } else {
    gasnetc_exitcode_t * const self = &gasnetc_exitcodes[0];
    int i;

    /* collect exit codes */
    for (i = 1; i < gasneti_nodemap_local_count; ++i) {
      gasnetc_exitcode_t * const peer = &gasnetc_exitcodes[i];

      while (! peer->present) {
        gasnetc_poll();
        if (gasneti_ticks_to_us(gasneti_ticks_now() - starttime) > timeout_us) {
          result = 1; /* failure */
          goto out;
        }
      }
      gasneti_local_rmb(); /* Acquire */
      exitcode = MAX(exitcode, peer->exitcode);
    }
  }
#endif

  if (pre_attach) gasneti_attach_done = 1; /* so we can poll for credits */

  for (distance = 1, shift = 0; distance < size; distance *= 2, ++shift) {
    gasnet_node_t peeridx = (distance >= size - rank) ? rank - (size - distance)
                                                      : rank + distance;

    oldcode = gasneti_atomic_read((gasneti_atomic_t *) &gasnetc_exitcode, 0);
    exitcode = MAX(exitcode, oldcode);

    gasnetc_sys_SendShutdownMsg(peeridx, shift, exitcode);

    /* wait for completion of the proper receive, which might arrive out of order */
    goal |= distance;
    while ((sys_exit_rcvd & goal) != goal) {
      gasnetc_poll();
      if (gasneti_ticks_to_us(gasneti_ticks_now() - starttime) > timeout_us) {
        result = 1; /* failure */
        goto out;
      }
    }
  }

  if (pre_attach) gasneti_attach_done = 0; /* so we clean up the right resources */

#if GASNET_PSHM
  { /* publish final exit code */
    gasnetc_exitcode_t * const self = &gasnetc_exitcodes[0];
    self->exitcode = exitcode;
    gasneti_local_wmb(); /* Release */
    self->present = 1;
  }
#endif

  #if GASNETC_SMSG_ARIES || GASNETC_SMSG_GASNET
  /* drain send completion events to avoid NOT_DONE at unbind: */
    while (gasneti_weakatomic_read(&shutdown_smsg_counter, GASNETI_ATOMIC_NONE)) {
      gasnetc_poll_local_queue();
      if (gasneti_ticks_to_us(gasneti_ticks_now() - starttime) > timeout_us) {
        result = 1; /* failure */
        goto out;
      }
    }
  #endif
    
out:
  oldcode = gasneti_atomic_read((gasneti_atomic_t *) &gasnetc_exitcode, 0);
  *exitcode_p = MAX(exitcode, oldcode);

  return result;
}






/* AuxSeg setup for registered bounce buffer space*/
GASNETI_IDENT(gasneti_bounce_auxseg_IdentString,
              "$GASNetAuxSeg_bounce: GASNETC_GNI_BOUNCE_SIZE:" _STRINGIFY(GASNETC_GNI_BOUNCE_SIZE_DEFAULT)" $");
gasneti_auxseg_request_t gasnetc_bounce_auxseg_alloc(gasnet_seginfo_t *auxseg_info) {
  gasneti_auxseg_request_t retval;
  
  retval.minsz = gasneti_getenv_int_withdefault("GASNETC_GNI_MIN_BOUNCE_SIZE",
                                                GASNETC_GNI_MIN_BOUNCE_SIZE_DEFAULT,1);
  retval.optimalsz = gasneti_getenv_int_withdefault("GASNETC_GNI_BOUNCE_SIZE",
                                                    GASNETC_GNI_BOUNCE_SIZE_DEFAULT,1);
  if (retval.optimalsz < retval.minsz) retval.optimalsz = retval.minsz;
  if (auxseg_info != NULL) { /* auxseg granted */
    /* The only one we care about is our own node */
    gasnetc_bounce_buffers = auxseg_info[gasneti_mynode];
  }

  return retval;
}

/* AuxSeg setup for registered post descriptors*/
/* This ident string is used by upcrun (and potentially by other tools) to estimate
 * the auxseg requirements, and gets rounded up.
 * So, this doesn't need to be an exact value.
 * As of 2013.02.14 I have systems with
 *     Gemini = 328 bytes
 *     Aries  = 344 bytes
 */
#if GASNET_CONDUIT_GEMINI
  #define GASNETC_SIZEOF_GDP 328
#else
  #define GASNETC_SIZEOF_GDP 344
#endif
GASNETI_IDENT(gasneti_pd_auxseg_IdentString, /* XXX: update if gasnetc_post_descriptor_t changes */
              "$GASNetAuxSeg_pd: " _STRINGIFY(GASNETC_SIZEOF_GDP) "*"
              "(GASNETC_GNI_NUM_PD:" _STRINGIFY(GASNETC_GNI_NUM_PD_DEFAULT) ") $");
gasneti_auxseg_request_t gasnetc_pd_auxseg_alloc(gasnet_seginfo_t *auxseg_info) {
  gasneti_auxseg_request_t retval;
  
  retval.minsz = gasneti_getenv_int_withdefault("GASNETC_GNI_MIN_NUM_PD",
                                                GASNETC_GNI_MIN_NUM_PD_DEFAULT,1)
    * sizeof(gasnetc_post_descriptor_t);
  retval.optimalsz = gasneti_getenv_int_withdefault("GASNETC_GNI_NUM_PD",
                                                    GASNETC_GNI_NUM_PD_DEFAULT,1) 
    * sizeof(gasnetc_post_descriptor_t);
  if (retval.optimalsz < retval.minsz) retval.optimalsz = retval.minsz;
  if (auxseg_info != NULL) { /* auxseg granted */
    /* The only one we care about is our own node */
    gasnetc_pd_buffers = auxseg_info[gasneti_mynode];
  }

  return retval;
}

gasneti_lifo_head_t gasnetc_bounce_buffer_pool = GASNETI_LIFO_INITIALIZER;

void gasnetc_init_bounce_buffer_pool(void)
{
  int i;
  int num_bounce;
  gasneti_assert_always(gasnetc_bounce_buffers.addr != NULL);
  gasneti_assert_always(gasnetc_bounce_buffers.size >= GASNETC_GNI_MIN_BOUNCE_SIZE_DEFAULT);
  gasnetc_bounce_register_cutover = 
    gasneti_getenv_int_withdefault("GASNETC_GNI_BOUNCE_REGISTER_CUTOVER",
				   GASNETC_GNI_BOUNCE_REGISTER_CUTOVER_DEFAULT,1);
  if (gasnetc_bounce_register_cutover > GASNETC_GNI_BOUNCE_REGISTER_CUTOVER_MAX)
    gasnetc_bounce_register_cutover = GASNETC_GNI_BOUNCE_REGISTER_CUTOVER_MAX;
  if (gasnetc_bounce_register_cutover > gasnetc_bounce_buffers.size)
    gasnetc_bounce_register_cutover = gasnetc_bounce_buffers.size;
  num_bounce = gasnetc_bounce_buffers.size / gasnetc_bounce_register_cutover;
  for(i = 0; i < num_bounce; i += 1) {
    gasneti_lifo_push(&gasnetc_bounce_buffer_pool, (char *) gasnetc_bounce_buffers.addr + 
		      (gasnetc_bounce_register_cutover * i));
  }
}

void *gasnetc_alloc_bounce_buffer(void)
{
  void *buf = gasneti_lifo_pop(&gasnetc_bounce_buffer_pool);
  if_pf (!buf) {
    /* We may simple not have polled the Cq recently.
       So, WAITHOOK and STALL tracing only if still nothing after first poll */
    GASNETC_TRACE_WAIT_BEGIN();
    int stall = 0;
    goto first;
    do {
      GASNETI_WAITHOOK();
      stall = 1;
first:
      gasnetc_poll_local_queue();
      buf = gasneti_lifo_pop(&gasnetc_bounce_buffer_pool);
    } while (!buf);
    if_pf (stall) GASNETC_TRACE_WAIT_END(ALLOC_BB_STALL);
  }
  return(buf);
}

void gasnetc_free_bounce_buffer(void *gcb)
{
  gasneti_lifo_push(&gasnetc_bounce_buffer_pool, gcb);
}

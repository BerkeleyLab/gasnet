/* MLW: Which of these includes do we really need? */
#include <gasnet_internal.h>
#include <gasnet_core_internal.h>
#include <gasnet_extended_internal.h>
#include <gasnet_handler.h>
#include <gasnet_portals.h>


/* macros used for simple hash table lookup.  Only accessed in this file. */
#define HASHTABLE_SIZE 512
#define HASHVAL HASHTABLE_SIZE
#define HASHFUNC(procid) (((procid)->nid) % HASHVAL)

/* We maintain a single Request send buffer */
size_t gasnetc_ReqSB_numchunk = 1024;
gasnetc_PtlBuffer_t gasnetc_ReqSB;

/* We maintain an array of Request Receive Buffers */
int    gasnetc_ReqRB_pool_size = 3;
size_t gasnetc_ReqRB_numchunk = 1024;
gasnetc_PtlBuffer_t *gasnetc_ReqRB;          

/* We maintain a single Reply send buffer */
size_t gasnetc_RplSB_numchunk = 64;       /* may only need one, for single-threaded implementation */
gasnetc_PtlBuffer_t gasnetc_RplSB;

/* The catch-basin buffer */
gasnetc_PtlBuffer_t gasnetc_CB;          

/* And the Remote Access Region, covered by two buffers */
gasnetc_PtlBuffer_t gasnetc_RAR;
gasnetc_PtlBuffer_t gasnetc_RARAM;

ptl_handle_ni_t gasnetc_ni_h;              /* the network interface handle */
ptl_handle_eq_t gasnetc_EQ_h;              /* Handle to the combined Event Queue */

/* We limit the number of temporary memory descriptors in use at any time.
 * If over the limit, allocator will poll until the number of outstanding tmp mds
 * drops below the limit.
 */
int gasnetc_max_tmpmd = GASNETC_MAX_TMP_MDS;
gasneti_weakatomic_t gasnetc_tmpmd_count;
#if GASNETI_STATS_OR_TRACE
int gasnetc_tmpmd_hwm = 0;
#endif

ptl_uid_t gasnetc_uid;
ptl_process_id_t gasnetc_myid;
gasnetc_procid_t *gasnetc_procid_map = NULL;

gasnetc_conn_t *gasnetc_conn_state = NULL;

/* ------------------------------------------------------------------------------------ */
/* The number of GASNet Put or Get operations that can occur before a poll to the
 * network is made.
 */
int gasnete_putget_poll = 6;
gasneti_weakatomic_t gasnete_putget_poll_cnt;

/* The number of outstanding Put/Get operations allowed and the Put/Get limit.
 * Note that we must limit so that we can insure no event queue overflow
 * for operations we initiate.  In general, each Put generates two local events
 * and each Get also generates two (although only should generate one).
 */
gasneti_weakatomic_t gasnete_putget_inflight;
int gasnete_putget_limit = 255;

/* used in AMLong Request operations when issuing a non-packed data put operation.
 * AMLongRequest must poll until this counter drops to zero, indicating that
 * message is off-node and client can overwrite source data region.
 * NOTE: Not used in AMLongRequestAsync.
 * NOTE: Also not used in AMLongReply.
 */
gasneti_weakatomic_t gasnetc_amlongReq_datacnt;

int gasnetc_max_poll_events = GASNETC_MAX_POLL_EVENTS;


const char* gasnetc_md_name[] = {"RAR_MD","RARAM_MD","REQSB_MD","REQRB_MD","RPLSB_MD","CB_MD","TMP_MD"};

/* =================================================================================
 * This top portion of the file is where file-scope worker routines are located.
 * ================================================================================= */

/* Forward reference for ReqRB event handler */
static void ReqRB_event(ptl_event_t *ev);

/* ------------------------------------------------------------------------------------
 * Search the lid cache for this object.
 * If not found, create one, add to list and return it, setting found = false.
 * If found, remove from list and return, setting found = true;
 * This is a worker function that should ONLY be called from 
 *        get_lid_object_from_data 
 *  -OR-  get_lid_object_from_header
 * Lid cache must be locked before this is called, caller must unlock.
 * --------------------------------------------------------------------------------- */
static int get_or_insert_lid(gasnet_node_t src, uint32_t lid, int numarg, gasnetc_amlongcache_t **obj)
{
  gasnetc_amlongcache_t *p, *prev;
  int found = 0;
  prev = NULL;
  p = gasnetc_conn_state[src].lids;
  while (! found) {
    if (p == NULL) break;
    if (p->dest_lid == lid) {
      found = true;
    } else { /* advance */
      prev = p;
      p = p->next;
    }
  }

  if (found) {
    /* remove from the list and return it */
    if (prev == NULL) {
      /* first in list */
      gasnetc_conn_state[src].lids = p->next;
    } else {
      prev->next = p->next;
    }
    p->next = NULL;
    *obj = p;
    return found;
  }

  /* not found, create new entry and add to list */
  p = (gasnetc_amlongcache_t*)gasneti_malloc(sizeof(gasnetc_amlongcache_t) + numargs*sizeof(gasnet_handlerarg_t));
  p->dest_lid = lid;
  p->narg = numargs;
  p->flags = 0;
  /* prev is either NULL, or points to the end of the list */
  if (prev == NULL) {
    /* empty list */
    p->next = gasnetc_conn_state[src].lids;
    gasnetc_conn_state[src].lids = p;
  } else {
    /* add at end */
    p->next = prev->next;
    prev->next = p;
  }

  *obj = p;
  return 0;
}

/* ------------------------------------------------------------------------------------
 * Search the lid cache from this src node for a matching object.
 * This call is made when the data portion of an AM Long arrives in the RARAM.
 * If found
 *     - remove from cache and return it
 *     - Data fields not updated
 *     - calling routine must deallocate
 * If not found
 *     - alloc new object and insert into cache.
 *     - set data fields as per arguments.
 *     - return NULL.
 *
 * --------------------------------------------------------------------------------- */
static gasnetc_amlongcache_t* get_lid_obj_from_data(gasnet_node_t src, uint32_t lid, void* dataptr, size_t datalen)
{
  gasnetc_amlongcache_t *obj;
  int found;

  /* lock the list */
  found = get_or_insert_lid(src, lid, 0, &obj);
  obj->flags &= GASNETC_LID_DATA_HERE;
  obj->data = dataptr;
  obj->datalen = datalen;
  /* unlock the list */
  if (found) {
    /* second to arrive, return obj to caller */
    return obj;
  }
  /* we are the first to arrive, obj remains on the list */
  return NULL;
}

/* ------------------------------------------------------------------------------------
 * Search the lid cache from this src node for a matching object.
 * This call is made when the Header portion of an AM Long arrives in either ReqRB or ReqSB.
 * If found
 *     - remove from cache and return it
 *     - Data fields not updated
 *     - calling routine must deallocate
 * If not found
 *     - alloc new object and insert into cache.
 *     - set data fields as per arguments.
 *     - return NULL.
 * --------------------------------------------------------------------------------- */
static gasnetc_amlongcache_t* get_lid_obj_from_header(gasnet_node_t src, uint32_t lid, gasnet_handler_t ghandler, uint32_t src_offset, int nargs, gasnet_handlerarg_t *args)
{
  gasnetc_amlongcache_t *obj;
  int found;
  int i;

  /* lock the list here */
  found = get_or_insert_lid(src, lid, nargs, &obj);

  obj->flags &= GASNETC_LID_HEADER_HERE;
  obj->ghandler = ghandler;
  obj->initiator_offset = src_offset;
  obj->narg = nargs;
  for (int i = 0; i < nargs; i++) {
    obj->args[i] = args[i];
  }

  /* unlock the list here */
  if (found) {
    /* second to arrive, return obj to caller */
    return obj;
  }

  /* we are the first to arrive, obj remains on the list */
  return NULL;
}

/* ------------------------------------------------------------------------------------
 * Unpack the data from the event structure and execute the Request or Reply AM Short
 * handler function.
 * isReq is true if this is an AM Short Request, false for a Reply.
 *
 * NOTE: the lower 32 bits of the match_bits have already been unpacked.
 * For a Request: upper 32 bits of match_bits = offset in sender ReqSB.
 * For a   Reply: upper 32 bits of match_bits = arg2
 * For both a request and reply:
 *   hdr_data:      [arg0 << 32 | arg1]
 *   Data Payload:  [remaining args]
 * NOTES:
 *   - message should be sizeof(double) aligned.
 * This function is called from:
 * - ReqRB_event in response to the arrival of an AM Short Request
 * - ReqSB_event in response to the arrival of an AM Short Reply
 * --------------------------------------------------------------------------------- */
static void exec_amshort_handler(int isReq, ptl_event_t *ev, int numarg, int ghandler)
{
  ptl_match_bits_t   mbits = ev->match_bits;
  gasnetc_ptl_token_t tok;
  gasnet_token_t token = (gasnet_token_t)&tok;
  gasnet_handlerarg_t args[numarg];
  uint8_t *data;
  int    argcnt = 0;

  tok.flags = 0;
  tok.initiator = ev->initiator;
  tok.srcnode = gasnetc_get_nodeid(&ev->initiator);

  if (isReq) {
    ptl_size_t rpl_offset;
    if (!gasnetc_chunk_alloc(&gasnetc_RplSB, GASNETC_CHUNKSIZE, &rpl_offset)) {
      gasneti_fatalerror("No RplSB chunks avail in ReqRB event handler");
    }
    /* MLW: NOTE that rpl send buffer is small, offset never > 4GB */
    tok.rplsb_offset = (uint32_t)rpl_offset;
    tok.initiator_offset = (uint32_t)(mbits >> 32);
  }

  /* crack args out of hdr_data */
  if (numarg > 0) args[argcnt++] = (gasnet_handlerarg_t)GASNETC_UNPACK_UPPER(ev->hdr_data);
  if (numarg > 1) args[argcnt++] = (gasnet_handlerarg_t)GASNETC_UNPACK_LOWER(ev->hdr_data);

  if (!isReq && (numarg > 2)) {
    /* Reply third arg in upper bits of match_bits */
    args[argcnt++] = (gasnet_handlerarg_t)GASNETC_UNPACK_UPPER(mbits);
  }

  /* set data pointer */
  data = (uint8_t*)ev->md.start + ev->offset;

  /* insure our data pointer is aligned for a double */
  gasneti_assert( ((intptr_t)data % sizeof(double)) == 0 );

  /* unpack remaining args from data payload */
  for(; argcnt < numarg; argcnt++) {
    memcpy(&args[argcnt], data, sizeof(gasnet_handlerarg_t));
    data += sizeof(gasnet_handlerarg_t);
  }

#ifdef GASNET_DEBUG
  {
    if (numarg == 0) {
      GASNETI_TRACE_PRINTF(C,("Running Short %s handler %d with No Args",(isReq ? "Request" : "Reply"),(int)ghandler));
    } else {
      int i;
      for (i = 0; i < numarg; i++) {
	GASNETI_TRACE_PRINTF(C,("Running Short %s handler %d Arg[%d]=%d",(isReq ? "Request" : "Reply"),(int)ghandler,i,(int)args[i]));
      }
    }
  }
#endif

  GASNETI_RUN_HANDLER_SHORT(isReq, ghandler, gasnetc_handler[handler], token, args, numarg);

  if (isReq && !(tok.flags & GASNETC_PTL_REPLY_SENT)) {
    GASNETI_SAFE(
		 SHORT_REP(0,0, (token , gasneti_handleridx(gasnetc_noop_reph)) )
		 );
  }
}

/* ------------------------------------------------------------------------------------
 * Unpack the data from the event structure and execute the Request or Reply AM Medium
 * handler function.
 * isReq is true if this is an AM Medium Request, false for a Reply.
 *
 * NOTE: the lower 32 bits of the match_bits have already been unpacked.
 * For a Request: upper 32 bits of match_bits = offset in sender ReqSB.
 * For a   Reply: upper 32 bits of match_bits = arg2
 * For both a request and reply:
 *   hdr_data:      [arg0 << 32 | arg1]
 *   Data Payload:  [remaining args][data payload length][pad][data payload]
 * NOTES:
 *   - message should be sizeof(double) aligned.
 *   - pad insures data payload is sizeof(double) aligned.
 * This function is called from:
 * - ReqRB_event in response to the arrival of an AM Medium Request
 * - ReqSB_event in response to the arrival of an AM Medium Reply
 * --------------------------------------------------------------------------------- */
static void exec_ammedium_handler(int isReq, ptl_event_t *ev, int numarg, int ghandler)
{
  ptl_match_bits_t   mbits = ev->match_bits;
  gasnetc_ptl_token_t tok;
  gasnet_token_t token = (gasnet_token_t)&tok;
  gasnet_handlerarg_t args[numarg];
  uint8_t *data;
  int      bytes_so_far, pad;
  int32_t  payload_bytes;
  size_t   nbytes;
  int      argcnt = 0;

  tok.flags = 0;
  tok.initiator = ev->initiator;
  tok.srcnode = gasnetc_get_nodeid(&ev->initiator);

  if (isReq) {
    ptl_size_t rpl_offset;
    if (!gasnetc_chunk_alloc(&gasnetc_RplSB, GASNETC_CHUNKSIZE, &rpl_offset)) {
      gasneti_fatalerror("No RplSB chunks avail in ReqRB event handler");
    }
    /* MLW: NOTE that rpl send buffer is small, offset never > 4GB */
    tok.rplsb_offset = (uint32_t)rpl_offset;
    tok.initiator_offset = (uint32_t)(mbits >> 32);
  }

  /* crack args out of hdr_data */
  if (numarg > 0) args[argcnt++] = GASNETC_UNPACK_UPPER(ev->hdr_data);
  if (numarg > 1) args[argcnt++] = GASNETC_UNPACK_LOWER(ev->hdr_data);

  if (!isReq && (numarg>2)) {
    args[argcnt++] = GASNETC_UNPACK_UPPER(mbits);
  }

  /* set data pointer */
  data = (uint8_t*)ev->md.start + ev->offset;

  /* insure our data pointer is aligned for a double */
  gasneti_assert( ((intptr_t)data % sizeof(double)) == 0 );

  bytes_so_far = 0;
  /* unpack remaining args from data payload */
  for(; argcnt < numarg; argcnt++) {
    memcpy(&args[argcnt], data, sizeof(gasnet_handlerarg_t));
    data += sizeof(gasnet_handlerarg_t);
    bytes_so_far += sizeof(gasnet_handlerarg_t);
  }

  /* unpack handler payload length */
  memcpy(&payload_bytes,data,sizeof(int32_t));
  data += sizeof(int32_t);
  bytes_so_far += sizeof(int32_t);
  nbytes = payload_bytes;  /* type conversion */

  /* Skip over any pad field so that handler payload is double-aligned */
  GASNETC_COMPUTE_DOUBLE_PAD(bytes_so_far,pad);
  data += pad;
  bytes_so_far += pad;

#ifdef GASNET_DEBUG
  {
    GASNETI_TRACE_PRINTF(C,("Running Medium %s handler %d with %d byte payload",(isReq ? "Request" : "Reply"),(int)ghandler,payload_bytes));
    int i;
    for (i = 0; i < numarg; i++) {
      GASNETI_TRACE_PRINTF(C,("Running Medium %s handler %d Arg[%d]=%d",(isReq ? "Request" : "Reply"),(int)ghandler,i,(int)args[i]));
    }
  }
#endif

  GASNETI_RUN_HANDLER_MEDIUM(isReq, ghandler, gasnetc_handler[handler], token, args, numarg, data, nbytes);

  if (isReq && !(tok.flags & GASNETC_PTL_REPLY_SENT)) {
    GASNETI_SAFE(
		 SHORT_REP(0,0, (token , gasneti_handleridx(gasnetc_noop_reph)) )
		 );
  }
}

/* ------------------------------------------------------------------------------------
 * Unpack the data from the event structure and attempt to execute the AM Long handler.
 * This routine will be called from both Request and Reply AMs, isReq=true of Request.
 * In general, AM Longs require two messages, a data payload sent directly to the RAR
 * and a header send to ReqRB (for a Request) or ReqSB (for a Reply).  isHeader=true
 * if this function is called in response to a header message, and false if in response
 * to the arrival of a data message.  The last to arrive will execute the requested handler.
 * The first to arrive will cache its metadata in a LID cache that the second can retrieve.
 * AMLong messages with small data payloads may have the payload packed with the header
 * message (and no data message).  isPacked=true if a packed message.
 * Note that if isHeader=false, the numarg and ghandler arguments are not used.
 * In summary, this function is called from:
 * - RARAM_event: in response to a Request or Reply AMLong data packet arrival.
 * - ReqRB_event: in response to an AM Long Header Request message.
 * - ReqSB_event: in response to an AM Long Header Reply message.
 * --------------------------------------------------------------------------------- */
static void exec_amlong_handler(int isReq, int isHeader, int isPacked,
				ptl_event_t *ev, int numarg, int ghandler)
{
  ptl_match_bits_t   mbits = ev->match_bits;
  gasnetc_ptl_token_t tok;
  gasnet_token_t token = (gasnet_token_t)&tok;
  gasnet_handlerarg_t args[numarg];
  uint32_t lid;
  uint8_t *data;
  int      pad;
  int32_t  payload_bytes;
  size_t   nbytes;
  int      argcnt = 0;
  int      bytes_so_far = 0;
  void    *dest;

  tok.flags = 0;
  tok.initiator = ev->initiator;
  tok.srcnode = gasnetc_get_nodeid(&ev->initiator);

  if (isReq && isHeader) {
    ptl_size_t rpl_offset;
    if (!gasnetc_chunk_alloc(&gasnetc_RplSB, GASNETC_CHUNKSIZE, &rpl_offset)) {
      gasneti_fatalerror("No RplSB chunks avail in ReqRB event handler");
    }
    /* MLW: NOTE that rpl send buffer is small, offset never > 4GB */
    tok.rplsb_offset = (uint32_t)rpl_offset;
    tok.initiator_offset = (uint32_t)(mbits >> 32);
  }

  /* extract LID and check if this is a packed AM Long */
  /* if this is a packed AM, the resulting LID is actually the data payload length */
  lid = GASNETC_UNPACK_LOWER(ev->hdr_data);
  if (isPacked) gasneti_assert(isHeader);

  if (isHeader) {
    /* crack args out of hdr_data */
    if (numarg > 0) args[argcnt++] = GASNETC_UNPACK_UPPER(ev->hdr_data);

    if (!isReq && (numarg > 1)) args[argcnt++] = GASNETC_UNPACK_UPPER(mbits);

    /* set data pointer */
    data = (uint8_t*)ev->md.start + ev->offset;

    /* insure our data pointer is aligned for a double */
    gasneti_assert( ((intptr_t)data % sizeof(double)) == 0 );

    /* unpack remaining args from data payload */
    for(; argcnt < numarg; argcnt++) {
      memcpy(&args[argcnt], data, sizeof(gasnet_handlerarg_t));
      data += sizeof(gasnet_handlerarg_t);
      bytes_so_far += sizeof(gasnet_handlerarg_t);
    }

    if (isPacked) {
      nbytes = (size_t)lid;
      /* extract the data payload destination, shoud be in local RAR */
      memcpy(&dest, data, sizeof(void*));
      data += sizeof(void*);
      bytes_so_far += sizeof(void*);
      
      /* copy the data payload to the specified destination */
      memcpy(dest,data,nbytes);

      GASNETI_TRACE_PRINTF(C,("Running AMLong Packed handler"));

      GASNETI_RUN_HANDLER_LONG(isReq, ghandler, gasnetc_handler[handler], token, args, numarg, dest, nbytes);

    } else {

      /* called from Header packet, but not a packed message, check if data message has arrived */
      gasnetc_amlongcache_t *p = get_lid_obj_from_header(tok.srcnode, lid, ghandler, tok.initiator_offset, numarg, args);

      if (p) {
	/* data has arrived, run handler */

	/* we are the second to arrive, we run the handler */
	GASNETI_RUN_HANDLER_LONG(isReq, ghandler ,gasnetc_handler[ghandler], token, args, numarg, p->data, p->datalen);

	/* free the lid object, it has already been removed from the list */
	gasneti_free(p);
      } else {
	/* nothing to do until header arrives in ReqRB */
      }      
    }

  } else {
    
    /* called from RARAM event handler when data payload arrived */
    gasnetc_amlongcache_t *p = get_lid_obj_from_data(tok.srcnode, lid, ghandler, tok.initiator_offset, numarg, args);
    if (p) {
      /* data has arrived, run handler */
      GASNETI_RUN_HANDLER_LONG(isReq, ghandler ,gasnetc_handler[ghandler], token, p->args, numarg, p->data, p->datalen);

      /* free the lid object, it has already been removed from the list */
      gasneti_free(p);
    } else {
      /* nothing to do until header arrives in ReqRB */
    }
  }

  if (check_reply && !(tok.flags & GASNETC_PTL_REPLY_SENT)) {
    /* must always issue a reply to dealloc ReqSB chunk.  If GASNet handler did
     * not reply, we reply here with a short no-op
     */
    GASNETI_SAFE(
		 SHORT_REP(0,0, (token , gasneti_handleridx(gasnetc_noop_reph)) )
		 );
  }

#error  HELP!!! Double check this, need RplSB if Request, but when to alloc?

}

/* ------------------------------------------------------------------------------------
 * Allocate memory with a given byte alignment.
 *  -- The aligned memory is the function return value
 *  -- The actual start of the memory (for freeing it) is returned in allocated_start
 *  -- The alignment MUST be a power of 2
 * --------------------------------------------------------------------------------- */
static void* gasnetc_aligned_alloc(size_t nbytes, uint32_t alignment, void **allocated_start)
{
  size_t bytes;
  void *loc;
  uintptr_t ptr, mask;

  bytes = nbytes + (alignment > 0 ? alignment - 1 : 0);
  loc = gasneti_malloc(bytes);
  *allocated_start = loc;

  if (alignment == 0) {
    /* no alignment constraint */
    return loc;
  }

  /* insure alignment is power of 2 (contains exactly one non-zero bit) */
  {
    uintptr_t bits = alignment;
    int cnt = 0;
    while (bits > 0) {
      if (bits & 0x1) cnt++;
      bits = bits >> 1;
    }
    if (cnt != 1) {
      gasneti_fatalerror("gasnetc_aligned_alloc with non-power-of-2 alignment %d",(int)alignment);
    }
  }
    
  /* finally, do the alignment by zeroing the low order bits */
  mask = alignment-1;
  ptr = ((uintptr_t)( (uint8_t*)loc + alignment - 1)) & ~mask;
  return (void*)ptr;
}

/* ------------------------------------------------------------------------------------
 * Allocate a buffer with the given alignment.
 * This buffer will NOT be managed by a chunk allocator
 * --------------------------------------------------------------------------------- */
static void gasnetc_buf_init(gasnetc_PtlBuffer_t *buf, const char *name, size_t nbytes, uint32_t alignment)
{
  {
    int len = strlen(name) + 1;
    buf->name = (char*)gasneti_malloc(len);
    strncpy(buf->name,name,len);
  }
  buf->alignment = alignment;
  buf->nbytes = nbytes;
  if (nbytes > 0) {
    buf->start = gasnetc_aligned_alloc(nbytes,alignment,&buf->actual_start);
    GASNETI_TRACE_PRINTF(C,("gasnetc_buf_init for %s alignment %u at %p, start=%p",name,alignment,buf->start,buf->actual_start));
  } else {
    buf->start = buf->actual_start = NULL;
    buf->alignment = 0;
  }
  buf->use_chunks = 0;
}

/* ------------------------------------------------------------------------------------
 * Trivial chunk allocator for bounce buffer and Msg Send buffers.
 * (1) WARNING WARNING WARNING!
 *     Not a thread-safe freelist implementation!!!
 *     Can easily have one thread pulling something off the list while a portals
 *     event handler, executing in another thread is putting a chunk back on the list.
 *     Must change for multi-threaded implementation.
 * (2) What we really should have is an efficient buddy-buffer implementation so that
 *     small messages dont have to allocate a full KB.  Concern that this will be expensive
 *     and even more expensive in multi-threaded environment.
 * --------------------------------------------------------------------------------- */
static void gasnetc_chunk_init(gasnetc_PtlBuffer_t *buf, const char *name, size_t nchunks)
{
  int i;
  size_t nbytes = nchunks * GASNETC_CHUNKSIZE;
  gasnetc_chunk_t *p;

  GASNETI_TRACE_PRINTF(C,("gasnetc_chunk_init for %s with %lu chunks",name,(ulong)nchunks));
  {
    int len = strlen(name) + 1;
    buf->name = (char*)gasneti_malloc(len);
    strncpy(buf->name,name,len);
  }
  buf->alignment = GASNETC_CHUNKSIZE;
  buf->nbytes = nbytes;
  buf->start = gasnetc_aligned_alloc(nbytes,buf->alignment,&buf->actual_start);
  buf->use_chunks = 1;
  buf->numchunks = nchunks;
  buf->inuse = 0;
  buf->hwm = 0;
  buf->freelist = NULL;
  GASNETI_TRACE_PRINTF(C,("CHUNK_INIT: %s nchunks=%i, nbytes=%i, start=0x%p",name,(int)nchunks,(int)nbytes,buf->start));
  p = (gasnetc_chunk_t*) buf->start;
  if (p == NULL) {
    gasneti_fatalerror("failed to alloc %i bytes for chunk allocator %s at %s",(int)nbytes,name,gasneti_current_loc);
  }
  for (i = 0; i < nchunks; i++) {
    p->next = buf->freelist;
    buf->freelist = p;
    p++;
  }
}

/* ---------------------------------------------------------------------------------
 * delete the allocator memory.
 * --------------------------------------------------------------------------------- */
static void gasnetc_buf_free(gasnetc_PtlBuffer_t *buf)
{
  if (buf->use_chunks) {
    int nchunk = 0;
    gasnetc_chunk_t *p = buf->freelist;

#if GASNET_DEBUG
    /* check for allocated chunks */
    while (p != NULL) {
      nchunk++;
      p = p->next;
    }
    GASNETI_TRACE_PRINTF(C,("gasnetc_buf_free: %d free chunks, expected %d for %s",nchunk,buf->numchunks,buf->name));
    gasneti_assert(nchunk == buf->numchunks);
#endif
  }

  if (buf->actual_start != NULL) {
    gasneti_free(buf->actual_start);
  }
  buf->start = buf->actual_start = NULL;
  buf->nbytes = 0;

  /* keep name and stats for trace_finish call */
}

/* ---------------------------------------------------------------------------------
 * This function is called when a Request Receive Buffer needs to be refreshed
 * and placed on the match list just before the catch-basin buffer.
 * The start_addr is the starting address of the memory buffer.  We use this
 * to determine which ReqRB from the pool needs to be re-cycled
 * --------------------------------------------------------------------------------- */
static void ReqRB_refresh(uintptr_t start_addr)
{
  int i;
  gasnetc_PtlBuffer_t *p = NULL;
  ptl_md_t md;
  ptl_process_id_t match_id;

  for (i = 0; i < gasnetc_ReqRB_pool_size; i++) {
    uintptr_t buf_start = (uintptr_t)gasnetc_ReqRB[i].start;
    if (buf_start == start_addr) {
      p = &gasnetc_ReqRB[i];
      break;
    }
  }
  if (p == NULL) {
    gasneti_fatalerror("ReqRB_refresh:Unable to find ReqRB with starting address 0x%llx",(unsigned long long)start_addr);
  }
  md.start = p->start;
  md.length = p->nbytes;
  md.threshold = PTL_MD_THRESH_INF;
  md.max_size = GASNETC_CHUNKSIZE;
  md.options = PTL_MD_OP_PUT | PTL_MD_EVENT_START_DISABLE | PTL_MD_MAX_SIZE;
#if GASNETC_USE_EQ_HANDLER
  md.user_ptr = (void*)(uint64_t)GASNETC_REQRB_MD;
#else
  md.user_ptr = (void*)ReqRB_event;
#endif
  md.eq_handle = gasnetc_EQ_h;

  match_id.nid = PTL_NID_ANY;
  match_id.pid = PTL_PID_ANY;

  GASNETC_PTLSAFE(PtlMEInsert(gasnetc_CB.me_h, match_id, GASNETC_PTL_REQRB_BITS, GASNETC_PTL_IGNORE_BITS,PTL_UNLINK, PTL_INS_BEFORE, &p->me_h));

  GASNETC_PTLSAFE(PtlMDAttach(p->me_h, md, PTL_UNLINK, &p->md_h));
}

/* ---------------------------------------------------------------------------------
 * Handle events on the local RARAM Memory Descriptor
 * Used as :
 *   - source of GASNet Puts or dest of Gets when data region happens to lie in RAR.
 *   - source of AM_Long when data region happens to lie in RAR.
 *   - dest of AM_Long data segment.
 * Events:
 *  SEND_END => Put (or Get) local completion
 *       ACK => GASNet Put completed remotely.  Mark operation as complete.
 *              NOTE: AMs dont request ACKs
 * REPLY_END => GASNet Get completed.  Mark operation as complete.
 *   PUT_END => Data portion of AM_Long arrived.  Extract LID and see if header has
 *              also arrived.  If so, call GASNet handler, else insert LID in LID table.
 * --------------------------------------------------------------------------------- */
static void RARAM_event(ptl_event_t *ev)
{
  ptl_size_t offset = ev->offset;
  ptl_match_bits_t   mbits = ev->match_bits;
  gasnete_threadidx_t threadid;
  gasnete_opaddr_t addr;
  uint8_t msg_type, amflag, numarg, ghandler;
  gasnete_op_t *op;

  msg_type = GASNETC_GET_MSG_TYPE(mbits);
  GASNETI_TRACE_PRINTF(C,("RARAM event %s offset = %i, mbits = 0x%lx, msg_type = 0x%x",ptl_event_str[ev->type],(int)offset,(uint64_t)mbits,msg_type));

  /* extract the lower bits based on message type */
  if (msg_type & GASNETC_PTL_MSG_AM) {
    gasnetc_get_am_lowbits(mbits, &amflag, &numarg, &ghandler);
  } else {
    gasnete_get_op_lowbits(mbits, &threadid, &addr);
  }

  /* we never truncate on this MD */
  gasneti_assert(ev->rlength == ev->mlength);

  switch (ev->type) {
  case PTL_EVENT_SEND_END:
    /* InSegment Put (from local RAR) */
    if ((msg_type & GASNETC_PTL_MSG_PUT) && (msg_type & GASNETC_PTL_MSG_DOLC)) {
      gasnete_threaddata_t *th = gasnete_threadtable[GASNETE_THREADID(threadid)];
      gasneti_weakatomic_decrement(&(th->local_completion_count), 0);
    } else if (msg_type & GASNETC_PTL_AMDATA) {
      uint32_t tag = (uint32_t)((mbits >> 8) & GASNETC_MASK_BYTE0);
      switch (tag) {
      case 0: /* do nothing */
	break;
      case 1:
	gasneti_weakatomic_decrement(&gasnetc_amlongReq_datacnt, 0);
	break;
      default:
	gasneti_fatalerror("RARAM_event on SEND_END for AMLong Data, tag = %u",tag);
      }
    }
    break;
  case PTL_EVENT_ACK:
    /* InSegment Put (from local RAR) */
    gasneti_assert(msg_type & GASNETC_PTL_MSG_PUT);
    gasneti_weakatomic_decrement(&gasnete_putget_inflight, 0);
    op = gasnete_opaddr_to_ptr(threadid, addr);
    /* mark the put (isget=0) operation complete */
    gasnete_op_markdone(op, 0 /* !isget */);
    break;

  case PTL_EVENT_REPLY_END:
    /* InSegment Get (to local RAR) */
    gasneti_assert(msg_type & GASNETC_PTL_MSG_GET);
    gasneti_weakatomic_decrement(&gasnete_putget_inflight, 0);
    op = gasnete_opaddr_to_ptr(threadid, addr);
    /* mark the get (isget=1) operation complete */
    gasnete_op_markdone(op, 1);
    break;

  case PTL_EVENT_PUT_END:
    /* Must be data packet of AM Long */
    /* isReq=true, isHeader=false, isPacked=false, numargs and ghandler in lid cache */
    exec_amlong_handler(1,0,0,ev,1,-1);
    break;

  default:
    gasneti_fatalerror("Invalid event %s on RARAM",ptl_event_str[ev->type]);
  }
}

/* ---------------------------------------------------------------------------------
 * Handle events on Temporary Memory Descriptors.
 * Used only as local source of GASNet Put or dest of GASNet Get when not
 * in local RAR and too large to use bounce buffer (on no chunks available).
 *  SEND_END => Put (or Get) completed locally.
 *       ACK => Put completed, mark operation as done and free MD.
 * REPLY_END => Get completed, mark operation done and free MD.
 * --------------------------------------------------------------------------------- */
static void TMPMD_event(ptl_event_t *ev)
{
  ptl_size_t offset = ev->offset;
  ptl_match_bits_t   mbits = ev->match_bits;
  gasnete_threadidx_t threadid;
  gasnete_opaddr_t addr;
  uint8_t msg_type;
  gasnete_op_t *op;

  msg_type = GASNETC_GET_MSG_TYPE(mbits);
  GASNETI_TRACE_PRINTF(C,("TMPMD event %s offset = %i, mbits = 0x%lx, msg_type = 0x%x",ptl_event_str[ev->type],(int)offset,(uint64_t)mbits,msg_type));

  /* extract the lower bits based on message type */
  if (msg_type & GASNETC_PTL_MSG_AM) {
    gasneti_fatalerror("Unexpected AM msg type on TMPMD, mbits = 0x%lx",(uint64_t)mbits);
  } else {
    gasnete_get_op_lowbits(mbits, &threadid, &addr);
  }

  /* we never truncate on this MD */
  gasneti_assert(ev->rlength == ev->mlength);

  switch (ev->type) {
  case PTL_EVENT_SEND_END:
    /* Put from TmpMD */
    if ((msg_type & GASNETC_PTL_MSG_PUT) && (msg_type & GASNETC_PTL_MSG_DOLC)) {
      gasnete_threaddata_t *th = gasnete_threadtable[GASNETE_THREADID(threadid)];
      gasneti_weakatomic_decrement(&(th->local_completion_count), 0);
    } else if (msg_type & GASNETC_PTL_AMDATA) {
      uint32_t tag = (uint32_t)((mbits >> 8) & GASNETC_MASK_BYTE0);
      switch (tag) {
      case 0: /* do nothing */
	break;
      case 1:
	gasneti_weakatomic_decrement(&gasnetc_amlongReq_datacnt, 0);
	break;
      default:
	gasneti_fatalerror("TMPMD_event on SEND_END for AMLong Data, tag = %u",tag);
      }
      /* unlink the tmp MD used in the AM Long data put */
      gasnetc_free_tmpmd(ev->md_handle);
      
    }
    break;
  case PTL_EVENT_ACK:
    /* Put from TmpMD */
    gasneti_assert(msg_type & GASNETC_PTL_MSG_PUT);
    gasneti_weakatomic_decrement(&gasnete_putget_inflight, 0);
    gasnetc_free_tmpmd(ev->md_handle);
    op = gasnete_opaddr_to_ptr(threadid, addr);
    /* mark the put (isget=0) operation complete */
    gasnete_op_markdone(op, 0 /* !isget */);
    break;

  case PTL_EVENT_REPLY_END:
    /* Get into TmpMD */
    gasneti_assert(msg_type & GASNETC_PTL_MSG_GET);
    gasneti_weakatomic_decrement(&gasnete_putget_inflight, 0);
    gasnetc_free_tmpmd(ev->md_handle);
    op = gasnete_opaddr_to_ptr(threadid, addr);
    /* mark the get (isget=1) operation complete */
    gasnete_op_markdone(op, 1);
    break;

  default:
    gasneti_fatalerror("Invalid event %s on TMPMD",ptl_event_str[ev->type]);
  }
}

/* ------------------------------------------------------------------------------------
 * Handle events on one of the Request Send Buffer
 *  SEND_END => Put (and Get) local completion.
 *                - Ignore for AM Request sends
 *                - Ignore for Gets (Cray Portals gens these in violation of spec)
 *                - If non-bulk Put, increment local completion counter and free chunk.
 *       ACK => Put through bounce buffer completed.  
 *                - NOTE: AMs will not generate ACKs.
 * REPLY_END => Get operation completed, data in bounce buffer must be copied
 *              to actual destination, mark op free, free chunk.
 *   PUT_END => Reply AM arrived in same chunk as Request was sent.
 *              Call GASNet handler then free chunk.
 *   GET_END => Catch-basin recovery underway.  Mark source node as in-recovery.
 * --------------------------------------------------------------------------------- */
static void ReqSB_event(ptl_event_t *ev)
{
  ptl_size_t offset = ev->offset;
  ptl_match_bits_t   mbits = ev->match_bits;
  gasnete_threadidx_t threadid;
  gasnete_opaddr_t addr;
  uint8_t msg_type, amflag, numarg, ghandler;
  gasnete_op_t *op;
  uint8_t *pdata, *q;
  void *dest;
  gasnetc_conn_t      *state;
  int pending;


  msg_type = GASNETC_GET_MSG_TYPE(mbits);
  GASNETI_TRACE_PRINTF(C,("ReqSB event %s offset = %i, mbits = 0x%lx, msg_type = 0x%x",ptl_event_str[ev->type],(int)offset,(uint64_t)mbits,msg_type));

  /* extract the lower bits based on message type */
  if (msg_type & GASNETC_PTL_MSG_AM) {
    gasnetc_get_am_lowbits(mbits, &amflag, &numarg, &ghandler);
  } else {
    gasnete_get_op_lowbits(mbits, &threadid, &addr);
  }

  /* we never truncate on this MD */
  gasneti_assert(ev->rlength == ev->mlength);

  switch (ev->type) {
  case PTL_EVENT_SEND_END:
    if (msg_type & GASNETC_PTL_MSG_PUT) {
      /* Put bounced through ReqSB, can free chunk now */
      if (msg_type & GASNETC_PTL_MSG_DOLC) {
	gasnete_threaddata_t *th = gasnete_threadtable[GASNETE_THREADID(threadid)];
	gasneti_weakatomic_decrement(&(th->local_completion_count), 0);
      }
      gasnetc_chunk_free(&gasnetc_ReqSB,offset);
    }

    break;
  case PTL_EVENT_ACK:
    /* Put bounced through ReqSB, mark op complete */
    gasneti_assert(msg_type & GASNETC_PTL_MSG_PUT);
    gasneti_weakatomic_decrement(&gasnete_putget_inflight, 0);
    op = gasnete_opaddr_to_ptr(threadid, addr);
    /* mark the put (isget=0) operation complete */
    gasnete_op_markdone(op, 0 /* !isget */);
    break;

  case PTL_EVENT_REPLY_END:
    /* Get bouncing through ReqSB, copy to dest and complete */
    gasneti_assert(msg_type & GASNETC_PTL_MSG_GET);
    gasneti_weakatomic_decrement(&gasnete_putget_inflight, 0);
    pdata = ((uint8_t*)ev->md.start + offset);
    q = pdata - sizeof(void*);
    /* q points to location where real destination address is stored */
    dest = (void*)*(uintptr_t*)q;
    GASNETI_TRACE_PRINTF(C,("EV_handler copying %lli bytes from bb 0x%lx to 0x%lx",(long long)ev->mlength,(uintptr_t)pdata,(uintptr_t)dest));
    memcpy(dest,pdata,ev->mlength);
    /* free the bounce buffer */
    offset -= sizeof(void*);
    gasnetc_chunk_free(&gasnetc_ReqSB,offset);
    op = gasnete_opaddr_to_ptr(threadid, addr);
    /* mark the get (isget=1) operation complete */
    gasnete_op_markdone(op, 1);
    break;

  case PTL_EVENT_PUT_END:
    /* This is an AM reply from a previous request */
    if (amflag & GASNETC_PTL_AM_SHORT) {
      exec_amshort_handler(0,ev,numarg,ghandler);
    } else if (amflag & GASNETC_PTL_AM_MDEIUM) {
      exec_ammedium_handler(0,ev,numarg,ghandler);
    } else if (amflag & GASNETC_PTL_AM_LONG) {
      /* isReq = 0, isHeader=1, isPacked=?, */
      exec_amlong_handler(0,1,amflag&GASNETC_PTL_AM_PACKED,ev,numarg,ghandler);
    } else {
      gasneti_fatalerror("ReqSB: Invalid amflag from mbits = %lx",(uint64_t)mbits);
    }

    /* dealloc the chunk */
    gasnetc_chunk_free(&gasnetc_ReqSB,offset);

    /* update connection state */
    state = gasnetc_conn_state + ptok.srcnode;
    pending = (int)gasneti_weakatomic_read(&state->AM_pending, 0);
    if (pending == 1) gasneti_weakatomic_set(&state->in_recovery, 0, 0);
    gasneti_weakatomic_decrement(&state->AM_pending,0);
    break;

  default:
    gasneti_fatalerror("Invalid event %s on ReqSB",ptl_event_str[ev->type]);
  }
}

/* ------------------------------------------------------------------------------------
 * Handle events on one of the Reply Send Buffer
 * SEND_END => reply message sent, free the chunk
 * --------------------------------------------------------------------------------- */
static void RplSB_event(ptl_event_t *ev)
{
  ptl_size_t offset = ev->offset;
  ptl_match_bits_t   mbits = ev->match_bits;
  uint8_t msg_type, amflag, numarg, ghandler;

  msg_type = GASNETC_GET_MSG_TYPE(mbits);
  GASNETI_TRACE_PRINTF(C,("RplSB event %s offset = %i, mbits = 0x%lx, msg_type = 0x%x",ptl_event_str[ev->type],(int)offset,(uint64_t)mbits,msg_type));

#if 0
  /* extract the lower bits based on message type */
  if (msg_type & GASNETC_PTL_MSG_AM) {
    gasnetc_get_am_lowbits(mbits, &amflag, &numarg, &ghandler);
  } else {
    gasneti_fatalerror("Invalid event msg type on RplSB, mbits = 0x%lx",(uint64_t)mbits);
  }
#endif

  /* we never truncate on this MD */
  gasneti_assert(ev->rlength == ev->mlength);

  switch (ev->type) {
  case PTL_EVENT_SEND_END:
    /* just reclaim the chunk */
    gasnetc_chunk_free(&gasnetc_RplSB, offset);
    break;

  default:
    gasneti_fatalerror("Invalid event %s on RplSB",ptl_event_str[ev->type]);
  }

}

/* ------------------------------------------------------------------------------------
 * Handle events on one of the ReqRB buffers
 * PUT_END => Arrival of new AM Request
 * UNLINK  => spent buffer, recycle and link at end of list
 * --------------------------------------------------------------------------------- */
static void ReqRB_event(ptl_event_t *ev)
{
  ptl_match_bits_t   mbits = ev->match_bits;
  uint8_t msg_type, amflag, numarg, ghandler;

  msg_type = GASNETC_GET_MSG_TYPE(mbits);
  GASNETI_TRACE_PRINTF(C,("ReqRB event %s offset = %i, mbits = 0x%lx, msg_type = 0x%x",ptl_event_str[ev->type],(int)ev->offset,(uint64_t)mbits,msg_type));

  /* extract the lower bits based on message type */
  if (msg_type & GASNETC_PTL_MSG_AM) {
    gasnetc_get_am_lowbits(mbits, &amflag, &numarg, &ghandler);
  } else {
    gasneti_fatalerror("Invalid event msg type on ReqRB, mbits = 0x%lx",(uint64_t)mbits);
  }

  /* we never truncate on this MD */
  gasneti_assert(ev->rlength == ev->mlength);

  switch (ev->type) {
  case PTL_EVENT_PUT_END:

    if (amflag & GASNETC_PTL_AM_SHORT) {
      exec_amshort_handler(1,ev,numarg,ghandler);
    } else if (amflag & GASNETC_PTL_AM_MDEIUM) {
      exec_ammedium_handler(1,ev,numarg,ghandler);
    } else if (amflag & GASNETC_PTL_AM_LONG) {
      /* isReq=true, isHeader=true, isPacked=? */
      exec_amlong_handler(1,1,amflag&GASNETC_PTL_AM_PACKED,ev,numarg,ghandler);
    } else {
      gasneti_fatalerror("ReqRB: Invalid amflag from mbits = %lx",(uint64_t)mbits);
    }


    /* Should we check if this buffer can be recycled here as well as below? */
#if 1
    {
      ptl_size_t space_left = ev->md.length - (ev->offset + ev->mlength);
      if (space_left < GASNETC_CHUNKSIZE) {
	/* attempt an unlink.  If successful, refresh the buffer */
	int rc = PtlMDUnlink(ev->md_handle);
	switch (rc) {
	case PTL_OK:
	  /* put it back on the end of the list */
	  ReqRB_refresh((intptr_t)ev->md.start);
	  break;
	case PTL_MD_IN_USE:
	  /* do nothing, will unlink later */
	  break;
	default:
	  gasneti_fatalerror("ReqRB_event: unlink attempt returned error %d",rc);
	}
      }
    }
#endif
    break;

  case PTL_EVENT_UNLINK:
    /* buffer was auto-unlinked, refresh and relink at end of buffer list */
    ReqRB_refresh((intptr_t)ev->md.start);
    break;

  default:
    gasneti_fatalerror("Invalid event %s on ReqRB",ptl_event_str[ev->type]);
  }
}

/* ------------------------------------------------------------------------------------
 * Handle events on the Catch-Basin memory descriptor.
 * - PUT_END => dropped AM Request
 * --------------------------------------------------------------------------------- */
static void CB_event(ptl_event_t *ev)
{
  ptl_size_t offset = ev->offset;
  ptl_match_bits_t   mbits = ev->match_bits;
  uint8_t msg_type, amflag, numarg, ghandler;

  msg_type = GASNETC_GET_MSG_TYPE(mbits);
  GASNETI_TRACE_PRINTF(C,("CB event %s offset = %i, mbits = 0x%lx, msg_type = 0x%x",ptl_event_str[ev->type],(int)offset,(uint64_t)mbits,msg_type));

  /* extract the lower bits based on message type */
  if (msg_type & GASNETC_PTL_MSG_AM) {
    gasnetc_get_am_lowbits(mbits, &amflag, &numarg, &ghandler);
  } else {
    gasneti_fatalerror("Invalid event msg type on CB, mbits = 0x%lx",(uint64_t)mbits);
  }
  /* we always truncate on this MD */
  gasneti_assert(ev->mlength == 0);

  switch (ev->type) {
  case PTL_EVENT_PUT_END:

  default:
    gasneti_fatalerror("Invalid event %s on CB",ptl_event_str[ev->type]);
  }
}

/* ---------------------------------------------------------------------------------
 * Construct the memory descriptors that cover the Remote Access Region.
 * There are two Memory Descriptors:
 *   - RAR_MD covers the region but has no event queue.  It is the target of
 *     remote Put and Get operations.
 *   - RARAM_MD also covers the RAR, but this MD is associated with an event
 *     queue.  This MD is used for the data payload Puts of AM_Long operations
 *     as well as the src/dest of Put/Get operations when the src/dest happen
 *     to lie in the local RAR (and we need the events to complete the ops).
 * Both are linked on the GASNETC_RAR_PTE portals table entry list.
 * --------------------------------------------------------------------------------- */
static void RAR_init()
{
  ptl_md_t md;
  ptl_handle_me_t me1_h, me2_h;
  ptl_process_id_t  match_id;
  void* rar_start   = gasneti_seginfo[gasneti_mynode].addr;
  size_t rar_len    = gasneti_seginfo[gasneti_mynode].size;

  GASNETI_TRACE_PRINTF(C,("RAR_init with len = %lu at %p",(unsigned long)rar_len,rar_start));

  match_id.nid = PTL_NID_ANY;
  match_id.pid = PTL_PID_ANY;

  gasnetc_RAR.start = rar_start;
  gasnetc_RAR.nbytes = rar_len;
  gasnetc_RAR.alignment = GASNET_PAGESIZE;
  gasnetc_RAR.actual_start = NULL;      /* this gets lost in gasneti_segmentattach, so cant free */
  gasnetc_RAR.name = "RAR";
  gasnetc_RAR.use_chunks = 0;

  /* Insert a MLE at the head of the list */
  GASNETC_PTLSAFE(PtlMEAttach(gasnetc_ni_h, GASNETC_PTL_RAR_PTE, match_id, GASNETC_PTL_RAR_BITS,
			      GASNETC_PTL_IGNORE_BITS, PTL_UNLINK, PTL_INS_BEFORE, &gasnetc_RAR.me_h));

  /* The RAR does not generate events, but will produce ACKs */
  md.start = rar_start;
  md.length = rar_len;
  md.threshold = PTL_MD_THRESH_INF;
  md.max_size = 0;
  md.options = PTL_MD_OP_PUT | PTL_MD_OP_GET | PTL_MD_MANAGE_REMOTE |
    PTL_MD_EVENT_START_DISABLE | PTL_MD_EVENT_END_DISABLE;
  md.user_ptr = 0;
  md.eq_handle = PTL_EQ_NONE;

  /* Attach this as first Match-list entry in portals table at index RAR_PTE */
  GASNETC_PTLSAFE(PtlMDAttach(gasnetc_RAR.me_h, md, PTL_RETAIN, &gasnetc_RAR.md_h));

  /* We create another md to cover the same RAR region but this one will have
   * an EQ.  We will use it as the source MD of a Put or dest MD of a get
   * in the case when the src/dest happens to lie within the RAR.
   * We could make this free-floating but will need it attached to an MLE
   * in the future, when used as the data target of an AM Long
   */
  gasnetc_RARAM.start = rar_start;
  gasnetc_RARAM.nbytes = rar_len;
  gasnetc_RAR.alignment = GASNET_PAGESIZE;
  gasnetc_RAR.actual_start = NULL;      /* this gets lost in gasneti_segmentattach, so cant free */
  gasnetc_RARAM.name = "RARAM";
  gasnetc_RARAM.use_chunks = 0;

  md.start = rar_start;
  md.length = rar_len;
  md.threshold = PTL_MD_THRESH_INF;
  md.max_size = 0;
  md.options = PTL_MD_OP_PUT | PTL_MD_OP_GET | PTL_MD_MANAGE_REMOTE |
    PTL_MD_EVENT_START_DISABLE;
#if GASNETC_USE_EQ_HANDLER
  md.user_ptr = (void*)(uint64_t)GASNETC_RARAM_MD;
#else
  md.user_ptr = (void*)RARAM_event;
#endif
  md.eq_handle = gasnetc_EQ_h;

  GASNETC_PTLSAFE(PtlMEInsert(gasnetc_RAR.me_h, match_id, GASNETC_PTL_RARAM_BITS,
			      GASNETC_PTL_IGNORE_BITS, PTL_UNLINK, PTL_INS_AFTER,
			      &gasnetc_RARAM.me_h));
  GASNETC_PTLSAFE(PtlMDAttach(gasnetc_RARAM.me_h, md, PTL_RETAIN, &gasnetc_RARAM.md_h));

}

/* ------------------------------------------------------------------------------------
 * Remove the memory descriptors associated with the Remote Access Region, but dont
 * deallocate the memory.
 * --------------------------------------------------------------------------------- */
static void RAR_exit()
{
  /* these will automatically unlink the match-list entries as well */
  GASNETC_PTLSAFE(PtlMDUnlink(gasnetc_RAR.md_h));
  GASNETC_PTLSAFE(PtlMDUnlink(gasnetc_RARAM.md_h));
}

/* ---------------------------------------------------------------------------------
 * Construct the Reply Send Buffer.
 * Replies to AM Requests are issued from this buffer.  It is never the target of
 * a remote operation so it is allocated a free-floating memory descriptor.
 * --------------------------------------------------------------------------------- */
static void RplSB_init()
{
  ptl_md_t md;

  GASNETI_TRACE_PRINTF(C,("RplSB_init"));

  gasnetc_chunk_init(&gasnetc_RplSB, "RplSB", gasnetc_RplSB_numchunk);

  md.start = gasnetc_RplSB.start;
  md.length = gasnetc_RplSB.nbytes;
  md.threshold = PTL_MD_THRESH_INF;
  md.max_size = 0;
  md.options = PTL_MD_EVENT_START_DISABLE;
#if GASNETC_USE_EQ_HANDLER
  md.user_ptr = (void*)(uint64_t)GASNETC_REQSB_MD;
#else
  md.user_ptr = (void*)RplSB_event;
#endif
  md.eq_handle = gasnetc_EQ_h;

  GASNETC_PTLSAFE(PtlMDBind(gasnetc_ni_h, md, PTL_RETAIN, &gasnetc_RplSB.md_h));
  GASNETI_TRACE_PRINTF(C,("RplSB_init: %s %lu chunks md=%lu",gasnetc_RplSB.name,(ulong)gasnetc_RplSB_numchunk,(ulong)gasnetc_RplSB.md_h));

}

/* ------------------------------------------------------------------------------------
 * Clean up the Reply Send Buffer.  Unlink and delete the memory.
 * --------------------------------------------------------------------------------- */
static void RplSB_exit()
{
  GASNETC_PTLSAFE(PtlMDUnlink(gasnetc_RplSB.md_h));
  gasnetc_buf_free(&gasnetc_RplSB);
}

/* ------------------------------------------------------------------------------------
 * Construct a pool of Request Receive Buffers along with Memory Descriptors for
 * each.  Link the buffers at the head of the GASNERC_AM_PTE portals table entry.
 * This is where AM Request messages will be placed (these are the only unexpected
 * messages in this implementation).  We do not implement any strict flow-control
 * so a flood of AM Requests into a node can overflow these buffers.  They are implemented
 * as a double or triple buffer scheme so that when one buffer it exhausted, it can
 * be reset and added back onto the end of the list.
 * We also allocate the Catch-Basin Memory descriptor here.  It lives at the end of
 * the list and uses the same match-bits as the Receive buffers.  
 * --------------------------------------------------------------------------------- */
static void ReqRB_init()
{
  int i;
  ptl_md_t md;
  size_t nbytes = gasnetc_ReqRB_numchunk * GASNETC_CHUNKSIZE;
  gasnetc_PtlBuffer_t *p;
  ptl_handle_me_t me_h;
  ptl_process_id_t  match_id;
  char name[32];

  GASNETI_TRACE_PRINTF(C,("ReqRB_init"));

  match_id.nid = PTL_NID_ANY;
  match_id.pid = PTL_PID_ANY;

  p = gasnetc_ReqRB = (gasnetc_PtlBuffer_t*)gasneti_malloc(gasnetc_ReqRB_pool_size*sizeof(gasnetc_PtlBuffer_t));
  gasneti_assert(gasnetc_ReqRB != NULL);
  for (i = 0; i < gasnetc_ReqRB_pool_size; i++) {
    sprintf(&name[0],"ReqRB_%02d",i);

    gasnetc_buf_init(p,name,nbytes,sizeof(double));

    md.start = p->start;
    md.length = p->nbytes;
    md.threshold = PTL_MD_THRESH_INF;
    md.max_size = GASNETC_CHUNKSIZE;
    md.options = PTL_MD_OP_PUT | PTL_MD_EVENT_START_DISABLE | PTL_MD_MAX_SIZE;
#if GASNETC_USE_EQ_HANDLER
    md.user_ptr = (void*)(uint64_t)GASNETC_REQRB_MD;
#else
    md.user_ptr = (void*)ReqRB_event;
#endif
    md.eq_handle = gasnetc_EQ_h;

    if (i == 0) {
      /* make first in list */
      GASNETC_PTLSAFE(PtlMEAttach(gasnetc_ni_h, GASNETC_PTL_AM_PTE, match_id, GASNETC_PTL_REQRB_BITS, GASNETC_PTL_IGNORE_BITS, PTL_UNLINK, PTL_INS_BEFORE, &p->me_h));
    } else {
      /* insert after i-1 */
      GASNETC_PTLSAFE(PtlMEInsert(gasnetc_ReqRB[i-1].me_h, match_id, GASNETC_PTL_REQRB_BITS, GASNETC_PTL_IGNORE_BITS, PTL_UNLINK, PTL_INS_AFTER, &p->me_h));
    }
    GASNETC_PTLSAFE(PtlMDAttach(p->me_h, md, PTL_UNLINK, &(p->md_h)));

    GASNETI_TRACE_PRINTF(C,("ReqRB_init[%d]: %s %lu bytes me=%lu md=%lu",i,p->name,(ulong)nbytes,(ulong)p->me_h,(ulong)p->md_h));

    p++;
  }

  /* Now add the Catch-Basin MD */
  gasnetc_buf_init(&gasnetc_CB,"Catch_Basin",0,0);
  md.start = NULL;
  md.length = 0;
  md.threshold = PTL_MD_THRESH_INF;
  md.max_size = 0;
  md.options = PTL_MD_OP_PUT | PTL_MD_EVENT_START_DISABLE | PTL_MD_ACK_DISABLE | PTL_MD_TRUNCATE;
#if GASNETC_USE_EQ_HANDLER
  md.user_ptr = (void*)(uint64_t)GASNETC_CB_MD;
#else
  md.user_ptr = (void*)CB_event;
#endif
  md.eq_handle = gasnetc_EQ_h;
  GASNETC_PTLSAFE(PtlMEInsert(gasnetc_ReqRB[gasnetc_ReqRB_pool_size-1].me_h, match_id, GASNETC_PTL_REQRB_BITS, GASNETC_PTL_IGNORE_BITS, PTL_UNLINK, PTL_INS_AFTER, &gasnetc_CB.me_h));
  GASNETC_PTLSAFE(PtlMDAttach(gasnetc_CB.me_h, md, PTL_RETAIN, &gasnetc_CB.md_h));

  GASNETI_TRACE_PRINTF(C,("CB_init: %s me=%lu md=%lu",gasnetc_CB.name,(ulong)gasnetc_CB.me_h,(ulong)gasnetc_CB.md_h));

}

/* ---------------------------------------------------------------------------------
 * Cleanup Request Receive Buffer resources.
 * --------------------------------------------------------------------------------- */
static void ReqRB_exit()
{
  int i;

  for (i = 0; i < gasnetc_ReqRB_pool_size; i++) {
    /* This should also unlink the associated MEs */
    GASNETC_PTLSAFE(PtlMDUnlink(gasnetc_ReqRB[i].md_h));
    gasnetc_buf_free(&gasnetc_ReqRB[i]);
  }
  gasneti_free(gasnetc_ReqRB);

  /* no data buffer for the CB */
  GASNETC_PTLSAFE(PtlMDUnlink(gasnetc_CB.md_h));
}

/* ---------------------------------------------------------------------------------
 * Allocate the Request Send Buffer (also used as a Bounce Buffer) and
 * Construct a Memory Descriptor for it.
 * Note:  Current Implementation is for free-floating MD.  Must put this on
 *        Match-list for AM Replys and for Catch-Basin algorithm to work.  
 *        Put it on GASNETC_AM_PTE table entry.
 * --------------------------------------------------------------------------------- */
static void ReqSB_init()
{
  ptl_md_t md;
  gasnetc_PtlBuffer_t *p = &gasnetc_ReqSB;
  ptl_process_id_t  match_id;

  match_id.nid = PTL_NID_ANY;
  match_id.pid = PTL_PID_ANY;

  GASNETI_TRACE_PRINTF(C,("ReqSB_init"));
  gasnetc_chunk_init(p, "ReqSB", gasnetc_ReqSB_numchunk);

  /* construct a memory descriptor for the Request Send Buffer and attach to AM PTE */
  md.start = gasnetc_ReqSB.start;
  md.length = gasnetc_ReqSB.nbytes;
  md.threshold = PTL_MD_THRESH_INF;
  md.max_size = 0;
  md.options = PTL_MD_EVENT_START_DISABLE | PTL_MD_OP_PUT | PTL_MD_MANAGE_REMOTE;
#if GASNETC_USE_EQ_HANDLER
  md.user_ptr = (void*)(uint64_t)GASNETC_REQSB_MD;
#else
  md.user_ptr = (void*)ReqSB_event;
#endif
  md.eq_handle = gasnetc_EQ_h;

  /* Insert this after the Catch-Basin ME entry (at end of list) */
  GASNETC_PTLSAFE(PtlMEInsert(gasnetc_CB.me_h, match_id, GASNETC_PTL_REQSB_BITS, GASNETC_PTL_IGNORE_BITS, PTL_UNLINK, PTL_INS_AFTER, &p->me_h));
  GASNETC_PTLSAFE(PtlMDAttach(p->me_h, md, PTL_UNLINK, &p->md_h ));

  GASNETI_TRACE_PRINTF(C,("ReqSB_init: %s %lu chunks me=%lu md=%lu",p->name,(ulong)gasnetc_ReqSB_numchunk,(ulong)p->me_h,(ulong)p->md_h));

}

/* ---------------------------------------------------------------------------------
 * Cleanup Request Send Buffer Resources
 * --------------------------------------------------------------------------------- */
static void ReqSB_exit()
{
  /* unlink the MD */
  GASNETC_PTLSAFE(PtlMDUnlink(gasnetc_ReqSB.md_h));

  /* free the memory */
  gasnetc_buf_free(&gasnetc_ReqSB);
}

/* =================================================================================
 * This lower portion of the file is where exported functions are located.
 * These are exported to both the Core and Extended API implementations.
 * ================================================================================= */


/* ---------------------------------------------------------------------------------
 * Allocate a new LID = "Long ID" for a new AMLong Request or Reply operation
 * --------------------------------------------------------------------------------- */
extern uint32_t gasnetc_new_lid(gasnet_node_t dest)
{
  /* NEED TO LOCK ALLOCATION OF NEW LID */
  uint32_t newlid = gasnetc_conn_state[dest].src_lid++;
  return newlid;
}

/* ---------------------------------------------------------------------------------
 * Setup the portals network so that we can begin communicating
 *   - Get the Portals network-interface handle.  
 *   - Get the Portals proc_id map so we know how to talk to all other nodes.
 * --------------------------------------------------------------------------------- */
static char* gasnetc_flush_buf = NULL;
static int gasnetc_io_buffer_size = 0;  /* was 1024 */
extern void gasnetc_init_portals_network(void)
{
  ptl_interface_t   ptl_iface;
#ifdef CNLinux
  int               use_bridge = PTL_BRIDGE_UK;
#else
  int               use_bridge = PTL_BRIDGE_QK;
#endif
  int               use_nal = PTL_IFACE_SS;
  ptl_ni_limits_t   ni_limits;
  cnos_nidpid_map_t *cnos_map;
  int               rc, i;
  int               num_interfaces;
 
  /* Set up buffered IO for STDOUT */
  if (gasnetc_io_buffer_size > 0) {
    gasnetc_flush_buf = (char*)gasneti_malloc(gasnetc_io_buffer_size);
    setvbuf(stdout, gasnetc_flush_buf, _IOFBF, gasnetc_io_buffer_size);
  }

  /* First, init portals */
  GASNETC_PTLSAFE(PtlInit(&num_interfaces));

  /* construct the interface */
  ptl_iface = IFACE_FROM_BRIDGE_AND_NALID(use_bridge,use_nal);

  printf("[%d] Before PtlNIInit\n",gasneti_mynode);
  /* Get the network handle */
  rc = PtlNIInit(ptl_iface, PTL_PID_ANY, NULL, &ni_limits, &gasnetc_ni_h);
  switch (rc) {
  case PTL_OK:
  case PTL_IFACE_DUP:
    break;
  default:
    gasneti_fatalerror("GASNet Portals failed on call to PtlNiInit:\n"
		       "  iface_type = %x error=%s (%i)\n"
		       "  at: %s\n",ptl_iface,ptl_err_str[rc],rc,gasneti_current_loc);
  }

  /* Get my process info */
  GASNETC_PTLSAFE(PtlGetUid(gasnetc_ni_h,&gasnetc_uid));
  GASNETC_PTLSAFE(PtlGetId(gasnetc_ni_h,&gasnetc_myid));
  
#ifdef CNLinux
  /* Assume APRUN launcher */
  if ((rc=cnos_register_ptlid(gasnetc_myid)) != 0) {
    gasneti_fatalerror("cnos_register_ptlid returned %d\n",rc);
  }
#else
  /* Under Catamount, dont have to do anything */
#endif

  /* get process to portals address mapping */
  printf("[%d] Before nidpid_map\n",gasneti_mynode);
  if(gasneti_nodes != cnos_get_nidpid_map(&cnos_map)) {
    gasneti_fatalerror("cnos_get_nidpid_map size != %d",gasneti_nodes);
  }

  gasneti_assert_always(cnos_map[gasneti_mynode].nid == gasnetc_myid.nid);
  gasneti_assert_always(cnos_map[gasneti_mynode].pid == gasnetc_myid.pid);
  gasnetc_procid_map = (gasnet_procid_t*)gasneti_malloc(gasneti_nodes * sizeof(gasnet_procid_t));
  for (node = 0; node < gasneti_nodes; node++) {
    gasnetc_procid_map[node].node_id = node;
    gasnetc_procid_map[node].ptl_id.nid = cnos_map[node].nid;
    gasnetc_procid_map[node].ptl_id.pid = cnos_map[node].pid;
    gasnetc_procid_map[node].next = NULL;
  }

  /* construct the hash table for reverse lookups */
  static gasnetc_hashrec_t *addrtable[HASHTABLE_SIZE];

  /* init the table to a list of null pointers */
  for (i = 0; i < HASHTABLE_SIZE; i++) {
    addrtable[i] = NULL;
  }

  /* now populate the table */
  for (node = 0; node < gasneti_nodes; node++) {
    gasnet_procid_t = *proc = &gasnetc_procid_map[node];
    int indx = HASHFUNC(&proc->ptl_id);
    proc->next = addrtable[indx];
    addrtable[indx] = proc;
  }

#ifdef GASNET_DEBUG
  {
    int i;
    int numzero = 0;
    int sum = 0;
    int mincnt = gasneti_nodes+1;
    int maxcnt = 0;
    double avg;
    for (i = 0; i < HASHTABLE_SIZE; i++) {
      int cnt = 0;
      gasnetc_procid_t *p = addrtable[i];
      if (p == NULL) numzero++;
      while (p != NULL) {
	GASNETI_TRACE_PRINTF(C,("Table[%d][%d] :: Node=%d, Nid=%d, Pid=%d",i,cnt,p->node_id,p->ptl_id.nid,p->ptl_id.pid));
	cnt++;
	p = p->next;
      }
      mincnt = MIN(cnt,mincnt);
      maxcnt = MAX(cnt,maxcnt);
      sum += cnt;
    }
    gasneti_assert(sum == gasneti_nodes);
    avg = ((double)sum)/((double)HASHTABLE_SIZE);
    GASNETI_TRACE_PRINTF(C,("Table stats: NumZero=%d, AvgLen=%6.2f, MinLen=%d, MaxLen=%d",numzero,avg,mincnt,maxcnt));
  }
#endif

  /* init weakatomic vars */
  gasneti_weakatomic_set(&gasnetc_amlongReq_datacnt, 0, 0);

}

/* Function to convert a ptl_process_id_t to a GASNet Node id */
extern gasnet_node_t gasnetc_get_nodeid(ptl_process_id_t *proc)
{
  int indx = HASHFUNC(proc);
  gasnetc_procid_t *p = addrtable[indx];
  while (p != NULL) {
    if ((p->ptl_id.nid == proc->nid) && (p->ptl_id.pid == proc->pid)) {
      return p->node_id;
    }
    p = p->next;
  }
  gasneti_fatalerror("gasnetc_get_nodeid failed with nid=%d,pid=%d, table index=%d",proc->nid,proc->pid,indx);
  return -1;
}


/* ---------------------------------------------------------------------------------
 * Function to issue data Put of AM Request Long payload to remote RAR.
 * If local data source is in RAR, use the RARAM MD.
 * If not, alloc a TMP MD, which will be unlinked by the event handlers.
 * If !sync, no need for caller to wait for put is off-node before returning.
 * If sync, bump amlongdata_cnt.  Event handler will decrement counter.
 * and caller will poll until zero.
 * mbits = [unused 32 bits ][24 bits for sync_flag][4 bits for msg type][4 bits for matching]
 * hdr_data = [unused 32 bits][32 bit lid]
 * msg_type = MSG_AMDATA
 * sync_flag = 1 if sync
 * sync_flag = 0, if !sync
 * --------------------------------------------------------------------------------- */
extern void gasnetc_amlong_datasend(int sync, uint32_t lid, gasnet_node_t dest, void *src_addr,
				    size_t nbytes, void* dest_addr)
{
  ptl_handle_md_t  md_h;
  ptl_process_id_t target_id = gasnetc_procid_map[node].ptl_id;
  ptl_ac_index_t ac_index = GASNETC_PTL_AC_ID;
  ptl_match_bits_t match_bits = GASNETC_PTL_MSG_AMDATA | GASNETC_PTL_RARAM_BITS;
  ptl_size_t local_offset = 0;
  ptl_size_t remote_offset = GASNETC_PTL_OFFSET(node,dest_addr);
  ptl_hdr_data_t hdr_data = (ptl_hdr_data_t)lid;

  if (sync) {
    gasneti_assert(gasneti_weakatomic_read(&gasnetc_amlongReq_datacnt, 0) == 0);
  }

  if (gasnetc_in_local_rar(src_addr,nbytes)) {
    md_h = gasnetc_RARAM.md_h;
    local_offset = GASNETC_PTL_OFFSET(gasneti_mynode,src);
  } else {
    /* alloc a temp md for the source region */
    md_h = gasnetc_alloc_tmpmd(src_addr, nbytes, gasnetc_EQ_h);
    local_offset = 0;
  }
  if (sync) {
    match_bits |= (0x1<<8);    /* signal to event handler to decr Req counter */
    gasneti_weakatomic_increment(&gasnetc_amlongReq_datacnt, 0);
  }
    
  /* Issue Ptl Put operation */
  GASNETC_PTLSAFE(PtlPutRegion(md_h, local_offset, nbytes, PTL_NOACK_REQ, target_id, GASNETC_PTL_RAR_PTE, ac_index, match_bits, remote_offset, hdr_data));
}

/* ---------------------------------------------------------------------------------
 * Bootstrap barrier function.
 * Just use cnos_barrier on XT3, but might have to init it first.
 * --------------------------------------------------------------------------------- */
extern void gasnetc_bootstrapBarrier() {
  static int gasnetc_bootstrapBarrierCnt = 0;

#ifdef CNLinux
  if (gasnetc_bootstrapBarrierCnt == 0) {
    /* First time, must init cnos barrier */
    cnos_barrier_init();
  }
#endif
  gasnetc_bootstrapBarrierCnt++;
  GASNETI_TRACE_PRINTF(C,("bootstrapBarrier count = %d",gasnetc_bootstrapBarrierCnt));

  cnos_barrier();
}

/* ---------------------------------------------------------------------------------
 * Bootstrap exchange function over portals
 * After the network has been initialized, but before all the conduit resources
 * have been allocated, can use this function to perform the equivelent of
 * an MPI_Broadcast.  The root node will broadcast its info to all other nodes.
 * --------------------------------------------------------------------------------- */
extern void gasnetc_bootstrapBroadcast(void *src, size_t len, void *dest, int rootnode)
{
  ptl_md_t src_md, dest_md;
  ptl_handle_me_t dest_me_h;
  ptl_handle_md_t src_h, dest_h;
  ptl_handle_eq_t eq_h;
  ptl_process_id_t  match_id;
  ptl_event_t ev;
  ptl_match_bits_t match_bits  = 0x0F0F0F0F0F0F0F0F;
  ptl_match_bits_t ignore_bits = 0x0000000000000000;
  int eq_len;
  int i, rc;

  match_id.nid = PTL_NID_ANY;
  match_id.pid = PTL_PID_ANY;

  GASNETI_TRACE_PRINTF(C,("bootBroadcast from %d len = %d, src=%p dest=%p",rootnode,(int)len,src,dest));

  if (gasneti_mynode != rootnode) {
    /* alloc an event queue for the action */
    GASNETC_PTLSAFE(PtlEQAlloc(gasnetc_ni_h, eq_len, NULL, &eq_h));

    /* register the dest md with an EQ on a match list */
    dest_md.start = dest;
    dest_md.length = len;
    dest_md.threshold = PTL_MD_THRESH_INF;
    dest_md.max_size = 0;
    dest_md.options = PTL_MD_EVENT_START_DISABLE | PTL_MD_OP_PUT | PTL_MD_MANAGE_REMOTE;
    dest_md.user_ptr = 0;
    dest_md.eq_handle = eq_h;

    /* construct the match entry */
    GASNETC_PTLSAFE(PtlMEAttach(gasnetc_ni_h, GASNETC_PTL_AM_PTE, match_id, match_bits, ignore_bits, PTL_UNLINK, PTL_INS_AFTER, &dest_me_h));

    /* attach the dest memory descriptor */
    GASNETC_PTLSAFE(PtlMDAttach(dest_me_h, dest_md, PTL_RETAIN, &dest_h));
  }

  /* need barrier here to insure everyone is ready to go */
  gasnetc_bootstrapBarrier();

  if (gasneti_mynode == rootnode) {
    /* alloc an event queue for the action */
    eq_len = 2*gasneti_nodes;
    int found = 0;

    GASNETC_PTLSAFE(PtlEQAlloc(gasnetc_ni_h, eq_len, NULL, &eq_h));

    /* register the src md with no event queue */
    src_md.start = src;
    src_md.length = len;
    src_md.threshold = PTL_MD_THRESH_INF;
    src_md.max_size = 0;
    src_md.options = PTL_MD_EVENT_START_DISABLE;
    src_md.user_ptr = 0;
    src_md.eq_handle = eq_h;
    GASNETC_PTLSAFE(PtlMDBind(gasnetc_ni_h, src_md, PTL_RETAIN, &src_h));

    /* spray out the message */
    for (i = 0; i < gasneti_nodes; i++) {
      if (i != gasneti_mynode) {
	GASNETC_PTLSAFE(PtlPut(src_h,PTL_NOACK_REQ, gasnetc_procid_map[i].ptl_id,GASNETC_PTL_AM_PTE,GASNETC_PTL_AC_ID,match_bits,0,0));
      }
    }

    while (found < (gasneti_nodes - 1)) {
      rc = PtlEQWait(eq_h, &ev);
      switch (rc) {
      case PTL_OK:
	gasneti_assert_always( ev.type == PTL_EVENT_SEND_END );
	break;
      default:
	gasneti_fatalerror("GASNet Portals Error in bootExchange waiting for event: %s (%i)\n at %s\n",
			   ptl_err_str[rc],rc,gasneti_current_loc);
      };
    }

    /* now remove the src MD */
    GASNETC_PTLSAFE(PtlMDUnlink(src_h));
    /* reclaim the event queue */
    GASNETC_PTLSAFE(PtlEQFree(eq_h));

  } else {
    eq_len = 10;
    /* wait the the message that data has arrived */
    rc = PtlEQWait(eq_h, &ev);
    switch (rc) {
    case PTL_OK:
      gasneti_assert_always( ev.type == PTL_EVENT_PUT_END );
      break;
    default:
      gasneti_fatalerror("GASNet Portals Error in bootExchange waiting for event: %s (%i)\n at %s\n",
			 ptl_err_str[rc],rc,gasneti_current_loc);
    };
  
    /* now remove the dest MD, which will also unlink the match-list entry */
    GASNETC_PTLSAFE(PtlMDUnlink(dest_h));

    /* reclaim the event queue */
    GASNETC_PTLSAFE(PtlEQFree(eq_h));
  }

  /* should not need a barrier here */
  GASNETI_TRACE_PRINTF(C,("bootBroadcast exit"));
}


/* ---------------------------------------------------------------------------------
 * Bootstrap exchange function over portals
 * After the network has been initialized, but before all the conduit resources
 * have been allocated, can use this function to perform the equivelent of
 * an MPI_Allgather.  Each node will broadcast its info to all other nodes.
 * --------------------------------------------------------------------------------- */
extern void gasnetc_bootstrapExchange(void *src, size_t len, void *dest)
{
  ptl_md_t src_md, dest_md;
  ptl_handle_me_t dest_me_h;
  ptl_handle_md_t src_h, dest_h;
  ptl_handle_eq_t eq_h;
  int eq_len = gasneti_nodes * 4;
  ptl_process_id_t  match_id;
  int found = 0;
  ptl_event_t ev;
  ptl_match_bits_t match_bits  = 0xF0F0F0F0F0F0F0F0;
  ptl_match_bits_t ignore_bits = 0x0000000000000000;
  int dest_offset = gasneti_mynode*len;
  int i;

  match_id.nid = PTL_NID_ANY;
  match_id.pid = PTL_PID_ANY;

  GASNETI_TRACE_PRINTF(C,("bootExch with len = %d, src = %p dest = %p",(int)len,src,dest));

  /* alloc an event queue for the action */
  GASNETC_PTLSAFE(PtlEQAlloc(gasnetc_ni_h, eq_len, NULL, &eq_h));

  /* register the src md with EQ */
  src_md.start = src;
  src_md.length = len;
  src_md.threshold = PTL_MD_THRESH_INF;
  src_md.max_size = 0;
  src_md.options = PTL_MD_EVENT_START_DISABLE;
  src_md.user_ptr = 0;
  src_md.eq_handle = PTL_EQ_NONE;
  src_md.eq_handle = eq_h;
  GASNETC_PTLSAFE(PtlMDBind(gasnetc_ni_h, src_md, PTL_RETAIN, &src_h));

  /* register the dest md with an EQ on a match list */
  dest_md.start = dest;
  dest_md.length = len*gasneti_nodes;
  dest_md.threshold = PTL_MD_THRESH_INF;
  dest_md.max_size = 0;
  dest_md.options = PTL_MD_EVENT_START_DISABLE | PTL_MD_OP_PUT | PTL_MD_MANAGE_REMOTE;
  dest_md.user_ptr = 0;
  dest_md.eq_handle = eq_h;

  /* construct the match entry */
  GASNETC_PTLSAFE(PtlMEAttach(gasnetc_ni_h, GASNETC_PTL_AM_PTE, match_id, match_bits, ignore_bits, PTL_UNLINK, PTL_INS_AFTER, &dest_me_h));

  /* attach the dest memory descriptor */
  GASNETC_PTLSAFE(PtlMDAttach(dest_me_h, dest_md, PTL_RETAIN, &dest_h));

  /* need barrier here to insure everyone is ready to go */
  gasnetc_bootstrapBarrier();

  /* spray out the message */
  for (i = 0; i < gasneti_nodes; i++) {
    if (i == gasneti_mynode) {
      memcpy(((uint8_t*)dest+dest_offset),src,len); 
    } else {
      GASNETC_PTLSAFE(PtlPut(src_h,PTL_NOACK_REQ, gasnetc_procid_map[i].ptl_id,GASNETC_PTL_AM_PTE,GASNETC_PTL_AC_ID,match_bits,dest_offset,0));
    }
  }

  /* now poll EQ until we see 2*(N-1) PUT_END/SEND_END events */
  while (found < 2*(gasneti_nodes-1)) {
    int rc = PtlEQWait(eq_h, &ev);
    switch (rc) {
    case PTL_OK:
      gasneti_assert_always( (ev.type == PTL_EVENT_PUT_END) || (ev.type == PTL_EVENT_SEND_END) );
      found++;
      break;
    default:
      gasneti_fatalerror("GASNet Portals Error in bootExchange waiting for event: %s (%i)\n at %s\n",
			 ptl_err_str[rc],rc,gasneti_current_loc);
    };
  }

  gasnetc_bootstrapBarrier(); /* MLW not needed */
  
  /* now remove the src MD */
  GASNETC_PTLSAFE(PtlMDUnlink(src_h));

  /* now remove the dest MD, which will also unlink the match-list entry */
  GASNETC_PTLSAFE(PtlMDUnlink(dest_h));

  /* reclaim the event queue */
  GASNETC_PTLSAFE(PtlEQFree(eq_h));

  /* should not need a barrier here */
  GASNETI_TRACE_PRINTF(C,("bootExch exit"));
}


extern void gasnetc_testBootExch(void)
{
  char *src = (char*)gasneti_malloc(gasneti_nodes*4*sizeof(char));
  char *dest = (char*)gasneti_malloc(gasneti_nodes*4*sizeof(char));
  int i;
  int failed = 0;
  char *probe;

  for (i = 0; i < 4*gasneti_nodes; i++) dest[i] = 'z';
  src[0] = 'a'; 
  src[1] = 'b'; 
  src[2] = 'c'; 
  src[3] = 'd'; 
  src[4] = 'e'; 
  src[5] = 'f'; 
  src[6] = 'g'; 
  src[7] = 'h';
  printf("[%d] testboot src = %p  dest = %p\n",gasneti_mynode,src,dest);

  probe = src;
  printf("[%d] testboot src data is [%d]-[%d]-[%d]-[%d]  [%d]-[%d]-[%d]-[%d]\n",
	 gasneti_mynode,
	 (int)probe[0],(int)probe[1],(int)probe[2],(int)probe[3],
	 (int)probe[4],(int)probe[5],(int)probe[6],(int)probe[7]);
  probe = dest;
  printf("[%d] testboot dest data is [%d]-[%d]-[%d]-[%d]  [%d]-[%d]-[%d]-[%d]\n",
	 gasneti_mynode,
	 (int)probe[0],(int)probe[1],(int)probe[2],(int)probe[3],
	 (int)probe[4],(int)probe[5],(int)probe[6],(int)probe[7]);

  gasnetc_bootstrapExchange((gasneti_mynode ? (src+4) : src),4,dest);

  probe = src;
  printf("[%d] testboot src data is [%d]-[%d]-[%d]-[%d]  [%d]-[%d]-[%d]-[%d]\n",
	 gasneti_mynode,
	 (int)probe[0],(int)probe[1],(int)probe[2],(int)probe[3],
	 (int)probe[4],(int)probe[5],(int)probe[6],(int)probe[7]);
  probe = dest;
  printf("[%d] testboot dest data is [%d]-[%d]-[%d]-[%d]  [%d]-[%d]-[%d]-[%d]\n",
	 gasneti_mynode,
	 (int)probe[0],(int)probe[1],(int)probe[2],(int)probe[3],
	 (int)probe[4],(int)probe[5],(int)probe[6],(int)probe[7]);

  gasnetc_bootstrapBarrier();

  gasneti_free(src);
  gasneti_free(dest);

}

/* ---------------------------------------------------------------------------------
 * Function to determine if a chunk of memory of the given size can be allocated
 * and registered as a portals memory descriptor (pinned).
 * Returns true if can be, false if the memory either cannot be allocated or
 * if pinning fails.
 * Cleans up after itself.
 * --------------------------------------------------------------------------------- */
static int try_pin(uintptr_t size)
{
  ptl_md_t md;
  ptl_handle_md_t md_h;
  int rc, ok;
  void *mem = gasneti_malloc_allowfail(size);

  if (mem == NULL) return 0;

  /* Now try to pin by creating a free floating MD for this memory */
  md.start = mem;
  md.length = size;
  md.threshold = PTL_MD_THRESH_INF;
  md.max_size = 0;
  md.options = PTL_MD_EVENT_START_DISABLE | PTL_MD_EVENT_END_DISABLE;
  md.user_ptr = 0;
  md.eq_handle = PTL_EQ_NONE;
  rc = PtlMDBind(gasnetc_ni_h, md, PTL_RETAIN, &md_h);
  switch(rc) {
  case PTL_OK:
    ok = 1;
    break;
  case PTL_NO_SPACE:
    ok = 0;
    break;
  default:
    gasneti_fatalerror("try_pin::PltMDBind returned error %d = %s",rc,ptl_err_str[rc]);
  }
  /* release the memory descriptor */
  if (ok) {
    GASNETC_PTLSAFE(PtlMDUnlink(md_h));
  }
  gasneti_free(mem);
  GASNETI_TRACE_PRINTF(C,("try_pin of %lu bytes %s",(unsigned long)size,(ok?"successful":"failed")));
  return ok;
}

/* ---------------------------------------------------------------------------------
 * Determine the largest amount of memory that can be pinned on the node.
 * --------------------------------------------------------------------------------- */
extern uintptr_t gasnetc_portalsMaxPinMem(void)
{
#define MBYTE 1048576ULL
  uint64_t granularity = 16ULL * MBYTE;
  uint64_t low = granularity;
  uint64_t high = 16ULL * 1024ULL * MBYTE;
  uint64_t prev;
#undef MBYTE
  void *mem = NULL;

  /* make sure we can pin at least the initial low watermark of memory */
  if (! try_pin(low)) {
    gasneti_fatalerror("Unable to alloc and pin minimal memory of size %d bytes",(int)low);
  }
  high = low;

  /* move high boundary up (exponentially) intil it will no longer pin */
  prev = low;
  high = prev*2;
  while (try_pin(high)) {
    prev = high;
    high *= 2;
  }
  low = prev;

  /* Now bisect until difference is within the granularity */
  do {
    uint64_t mid = (low + high)/2;
#if 0
    GASNETI_TRACE_PRINTF(C,("MaxPinMem: low = %lu  mid = %lu  high = %lu",(unsigned long)low,(unsigned long)mid,(unsigned long)high));
#endif
    if (try_pin(mid)) {
      low = mid;
    } else {
      high = mid;
    }
  } while ((high - low) > granularity);
  if (try_pin(high)) {
    low = high;
  }
  GASNETI_TRACE_PRINTF(C,("MaxPinMem = %lu",(unsigned long)low));
  return (uintptr_t)low;
}

/* ---------------------------------------------------------------------------------
 * get a chunk from the allocator.
 * returns 1=TRUE on success, 0=FAIL if not able to satisfly request.
 * Will poll network a bounded number of times if necessary to alloc chunk.
 * MLW: may want input flag to control this.
 * --------------------------------------------------------------------------------- */
extern int gasnetc_chunk_alloc(gasnetc_PtlBuffer_t *buf, size_t nbytes, ptl_size_t *offset)
{
    gasnetc_chunk_t *p;
    int poll_max = 2;
    int cnt = 0;
    
    gasneti_assert(buf->use_chunks);

    if (nbytes > GASNETC_CHUNKSIZE) {
      gasneti_fatalerror("gasnetc_chunk_alloc requested %lu bytes, limit is %lu",(ulong)nbytes,(ulong)GASNETC_CHUNKSIZE);
    }
    while ((buf->freelist == NULL) && cnt < poll_max) {
      /* poll npoll times, to see if slot frees up */
      GASNETI_SAFE(gasneti_AMPoll());
      cnt++;
    }
    if (buf->freelist == NULL) return 0;
    p = buf->freelist;
    buf->freelist = p->next;
    *offset = ((uint8_t*)p - (uint8_t*)(buf->start));
#if GASNETI_STATS_OR_TRACE
    buf->inuse++;
    if (buf->inuse > buf->hwm) buf->hwm = buf->inuse;
    GASNETI_TRACE_PRINTF(C,("CHUNK_ALLOC: name %s, inuse = %d, hwm = %d, offset=%lu",buf->name,buf->inuse,buf->hwm,(unsigned long)*offset));
    GASNETI_TRACE_EVENT(C, CHUNK_ALLOC);
#endif

    return 1;
}

/* ---------------------------------------------------------------------------------
 * release a chunk back to the allocator.
 * MLW: Programmer error to return chunk to wrong allocator since just giving
 *      the offset.  We could hand in full address and compute offset here for
 *      better error checking, but would have to compute full address from offset
 *      at point of call, and unwind that here.  
 * --------------------------------------------------------------------------------- */
extern void gasnetc_chunk_free(gasnetc_PtlBuffer_t *buf, ptl_size_t offset)
{
    gasnetc_chunk_t *p = (gasnetc_chunk_t*)((uint8_t*)buf->start + offset);
    p->next = buf->freelist;
    buf->freelist = p;
#if GASNETI_STATS_OR_TRACE
    buf->inuse--;
    GASNETI_TRACE_PRINTF(C,("CHUNK_FREE: name %s, inuse = %d, hwm = %d, offset=%lu",buf->name,buf->inuse,buf->hwm,(unsigned long)offset));
    GASNETI_TRACE_EVENT(C, CHUNK_FREE);
#endif
}

/* ---------------------------------------------------------------------------------
 * Allocate a temp md to be used as the source of a Put or destination
 * of a Get operation.  MD to be free floating, not target of remote op.
 * MLW: Can make more general by specifying OPTIONS as arg.  May need this
 *      for CB recovery.
 * --------------------------------------------------------------------------------- */
extern ptl_handle_md_t gasnetc_alloc_tmpmd(void* dest, size_t nbytes, ptl_handle_eq_t eq_h)
{
  ptl_md_t md;
  ptl_handle_md_t md_h;

  /* Want to limit the number of tmp MDs in operation at once.
   * Poll until number of outstanding TMP MDs is less than limit.
   */
  GASNETI_TRACE_PRINTF(C,("Alloc_Tmpmd: num TmpMD outstanding = %d",gasneti_weakatomic_read(&gasnetc_tmpmd_count,0)));
  gasneti_pollwhile( (gasneti_weakatomic_read(&gasnetc_tmpmd_count,0) >= gasnetc_max_tmpmd) );
  gasneti_weakatomic_increment(&gasnetc_tmpmd_count,0);

  md.start = dest;
  md.length = nbytes;
  md.threshold = PTL_MD_THRESH_INF;
  md.max_size = 0;
  md.options = PTL_MD_EVENT_START_DISABLE;
#if GASNETC_USE_EQ_HANDLER
  md.user_ptr = (void*)(uint64_t)GASNETC_TMP_MD;
#else
  md.user_ptr = (void*)TMPMD_event;
#endif
  md.eq_handle = eq_h;

  GASNETC_PTLSAFE(PtlMDBind(gasnetc_ni_h, md, PTL_RETAIN, &md_h));

#if GASNETI_STATS_OR_TRACE
  {
	int inuse = (int)gasneti_weakatomic_read(&gasnetc_tmpmd_count,0);
	if (gasnetc_tmpmd_hwm < inuse) gasnetc_tmpmd_hwm = inuse;
	GASNETI_TRACE_PRINTF(C,("ALLOC TMPMD at 0x%p, len=%d, inuse=%d, hwm=%d",dest,(int)nbytes,inuse,gasnetc_tmpmd_hwm));
	GASNETI_TRACE_EVENT(C, TMPMD_ALLOC);
  }
#endif

  return md_h;
}

/* ---------------------------------------------------------------------------------
 * Release the TMP memory descriptor (unpin the region), but dont dealloc the
 * memory space (since we did not alloc it).
 * --------------------------------------------------------------------------------- */
extern void gasnetc_free_tmpmd(ptl_handle_md_t md_h)
{
  gasneti_weakatomic_decrement(&gasnetc_tmpmd_count,0);
  GASNETC_PTLSAFE(PtlMDUnlink(md_h));
#if GASNETI_STATS_OR_TRACE
      {
	int inuse = (int)gasneti_weakatomic_read(&gasnetc_tmpmd_count,0);
	GASNETI_TRACE_PRINTF(C,("FREE TMPMD"));
	GASNETI_TRACE_EVENT(C, TMPMD_FREE);
      }
#endif
}

/* ---------------------------------------------------------------------------------
 * Initialize all the Portals resources for GASNet:
 *   - Read portals-related env vars to adjust buffer sizes and polling counts
 *   - Allocate the event queues:  Only using one for now
 *   - Allocate the buffers for bounce, send/recv regions
 *   - Construct match-list entries and memory descriptors for bounce, send/recv space
 *     and RAR (Segment already allocated and initialized).
 * --------------------------------------------------------------------------------- */
extern void gasnetc_init_portals_resources(void)
{
  /* Set up my RAR MDs */
  ptl_size_t        eq_len;
  int               rc;
  int               i, num_chunk;

  /* read Portals specific env vars */
  gasnetc_ReqRB_pool_size = (int)gasneti_getenv_int_withdefault("GASNET_PORTAL_POOLSZ",
   			         (int64_t)gasnetc_ReqRB_pool_size,0);
  gasnetc_ReqRB_numchunk = (int)gasneti_getenv_int_withdefault("GASNET_PORTAL_RB_CHUNKS",
				(int64_t)gasnetc_ReqRB_numchunk,0);
  gasnetc_ReqSB_numchunk = (int)gasneti_getenv_int_withdefault("GASNET_PORTAL_SB_CHUNKS",
				(int64_t)gasnetc_ReqSB_numchunk,0);
  gasnetc_RplSB_numchunk = (int)gasneti_getenv_int_withdefault("GASNET_PORTAL_RPL_CHUNKS",
				(int64_t)gasnetc_RplSB_numchunk,0);
  gasnetc_max_tmpmd = (int)gasneti_getenv_int_withdefault("GASNET_PORTAL_NUM_TMPMD",
			   (int64_t)GASNETC_MAX_TMP_MDS,0);
  gasnetc_max_poll_events = (int)gasneti_getenv_int_withdefault("GASNET_PORTAL_MAX_POLL_EVENTS",
				 (int64_t)GASNETC_MAX_POLL_EVENTS,0);
  gasnete_putget_poll = (int)gasneti_getenv_int_withdefault("GASNET_PORTAL_PUTGET_POLL",
				(int64_t)gasnete_putget_poll,0);
  gasnete_putget_limit = (int)gasneti_getenv_int_withdefault("GASNET_PORTAL_PUTGET_LIMIT",
				(int64_t)gasnete_putget_limit,0);
				
  GASNETI_TRACE_PRINTF(C,("Portals_Init: ReqRB_Pool_size = %d",gasnetc_ReqRB_pool_size));
  GASNETI_TRACE_PRINTF(C,("Portals_Init: ReqRB_numchunk  = %d",(int)gasnetc_ReqRB_numchunk));
  GASNETI_TRACE_PRINTF(C,("Portals_Init: ReqSB_numchunk  = %d",(int)gasnetc_ReqSB_numchunk));
  GASNETI_TRACE_PRINTF(C,("Portals_Init: RplSB_numchunk  = %d",(int)gasnetc_RplSB_numchunk));
  GASNETI_TRACE_PRINTF(C,("Portals_Init: max_tmpmd       = %d",gasnetc_max_tmpmd));
  GASNETI_TRACE_PRINTF(C,("Portals_Init: max_poll_events = %d",gasnetc_max_poll_events));
  GASNETI_TRACE_PRINTF(C,("Portals_Init: putget_per_poll = %d",gasnete_putget_poll));
  GASNETI_TRACE_PRINTF(C,("Portals_Init: putget_limit    = %d",gasnete_putget_limit));

  /* Init the temp md counter to zero */
  gasneti_weakatomic_set(&gasnetc_tmpmd_count, 0, 0);

  /* keep a counter of number of puts/gets since last poll */
  gasneti_weakatomic_set(&gasnete_putget_poll_cnt, 0, 0);
  gasneti_weakatomic_set(&gasnete_putget_inflight, 0, 0);

  /* Allocate and init the connection state array */
  gasnetc_conn_state = (gasnetc_conn_t*)gasneti_malloc(gasneti_nodes*sizeof(gasnetc_conn_t));
  for (i = 0; i < gasneti_nodes; i++) {
    gasneti_weakatomic_set(&(gasnetc_conn_state[i].AM_pending), 0, 0);
    gasneti_weakatomic_set(&(gasnetc_conn_state[i].in_recovery), 0, 0);
    gasnetc_conn_state[i].src_lid = 0;
    gasnetc_conn_state[i].lids = NULL;
  }

  /* On first cut, lets just create a single EQ for all of the MDs that will
   * generate events.  This includes:
   * BounceBuffer:  number = 2*num_chunks;
   * RARAM:         unknown ... scale with num procs? what scaling factor?
   * TMPMD:         2*max number of tmpmds
   */
  num_chunk = gasnetc_ReqSB_numchunk + gasnetc_RplSB_numchunk 
    + gasnetc_ReqRB_pool_size*gasnetc_ReqRB_numchunk;
  eq_len = 2*(num_chunk + gasnetc_max_tmpmd);
  /* two events per outstanding put or get */
  if (gasnete_putget_limit) {
    eq_len += 2*gasnete_putget_limit;
  } else {
    /* Allow unlimited number of Put/Get ops in flight.
     * Living dangerously, may not poll in time to prevent EQ overflow */
    eq_len += 50*gasneti_nodes;
  }
  /* for good measure */
  eq_len += 100;

  GASNETI_TRACE_PRINTF(C,("Constructing EQ with %lld entries",(long long)eq_len));
#if 0
  if (gasneti_mynode == 0) {
    printf("MAX_POLL_EVENTS = %i\n",gasnetc_max_poll_events);
    printf("PUTGET_MAXPOLL  = %i\n",gasnete_putget_poll);
    printf("PUTGET_LIMIT    = %i\n",gasnete_putget_limit);
    printf("TOTAL EVENTS    = %i\n",eq_len);
  }
#endif

  GASNETC_PTLSAFE(PtlEQAlloc(gasnetc_ni_h, eq_len, GASNETC_EQ_HANDLER, &gasnetc_EQ_h));

  RAR_init();
  ReqRB_init();
  ReqSB_init();  /* required to init ReqSB after ReqRB, since it must go on end of list */
  RplSB_init();

#ifndef GASNETC_USE_EQ_HANDLER
  /* Enable the progress function */
  GASNETI_TRACE_PRINTF(C,("Enabling Portals polling function (No EQ Handler)"));
  GASNETI_PROGRESSFNS_ENABLE(gasnete_pf_portals_poll,BOOLEAN);
#endif
}

/* ---------------------------------------------------------------------------------
 * Release Portals resources
 *   - The proc_id map
 *   - Remove MDs and match-list entries
 *   - Free the buffers used for bounce, send/recv
 * --------------------------------------------------------------------------------- */
extern void gasnetc_portals_exit()
{

  RplSB_exit();
  ReqRB_exit();
  ReqSB_exit();
  RAR_exit();

  /* remove the event queues */
  GASNETC_PTLSAFE(PtlEQFree(gasnetc_EQ_h));

  /* free the proc id map */
  gasneti_free(gasnetc_procid_map);

  /* free the AM connection state array */
  gasneti_free(gasnetc_conn_state);

}

/* ------------------------------------------------------------------------------------
 * --------------------------------------------------------------------------------- */
/* Portals polling function.  Process the event queue.
 * This gets called by gasneti_AMPoll() after checking for incoming MPI messages 
 * In the current implementation, we only have one event queue,
 * so just check it and run appropriate handler
 */

extern void gasnetc_portals_poll(void)
{
  ptl_event_t ev;
  int rc;
  int processed = 0;
  int finished = 0;

  /* Reset the putget counter, since we are about to poll... */
  gasneti_weakatomic_set(&gasnete_putget_poll_cnt, 0, 0);

  /* Note that PtlEQGet is just a user-space call and will see if anything is
   * on the list without diving into the kernel to see if any unprocessed events
   * are waiting.  Try it first.  If multiple exist, run them back to back since they
   * release resources.
   */
  while (! finished) {
    rc = PtlEQGet( gasnetc_EQ_h, &ev);
    switch (rc) {
    case PTL_OK:
      GASNETI_TRACE_PRINTF(C,("Q Handler: Got event %s from PtlEQGet, md=%lu",ptl_event_str[ev.type],(ulong)ev.md_handle));
      GASNETC_CALL_EQ_HANDLER(ev); 
      processed++;
      if (gasnetc_max_poll_events && (processed > gasnetc_max_poll_events)) finished = 1;
      break;
    case PTL_EQ_EMPTY:
      finished = 1;
      break;
    default:
      gasneti_fatalerror("GASNet Portals Error in PtlEQGet: %s (%i)\n at %s\n",
			 ptl_err_str[rc],rc,gasneti_current_loc);
      break;
    }
  }

  if (processed == 0) {
    /* No easy pickings... try polling, which may enter the kernel */
    int which = 0;
    int timeout = 0;       /* number of usec to wait */
    rc = PtlEQPoll(&gasnetc_EQ_h,1,timeout,&ev,&which);
    switch (rc) {
    case PTL_OK:
      GASNETI_TRACE_PRINTF(C,("Q Handler: Got event %s from PtlEQPoll, md=%lu",ptl_event_str[ev.type],(ulong)ev.md_handle));
      GASNETC_CALL_EQ_HANDLER(ev);
      processed++;
      break;
    case PTL_EQ_EMPTY:
      break;
    default:
      gasneti_fatalerror("GASNet Portals Error in PtlEQPoll: %s (%i)\n at %s\n",
			 ptl_err_str[rc],rc,gasneti_current_loc);
      break;
    }
  }
  GASNETI_TRACE_EVENT_VAL(C, EVENT_CNT, processed);
}

/* ------------------------------------------------------------------------------------
 * Attempt to get an event from the specified event queue.
 * Return 1=true if successful, 0 otherwise.
 * ev points to the event structure to be filled in.
 * --------------------------------------------------------------------------------- */
extern int gasnetc_get_event(ptl_handle_eq_t eq_h, ptl_event_t *ev)
{
  ptl_event_t ev;
  int rc;
  int which = 0;
  int timeout = 0;       /* number of usec to wait */

  /* Note that PtlEQGet is just a user-space call and will see if anything is
   * on the list without diving into the kernel to see if any unprocessed events
   * are waiting.  Try it first.  If multiple exist, run them back to back since they
   * release resources.
   */
  rc = PtlEQGet( eq_h, ev);
  switch (rc) {
  case PTL_OK:
    return 1;
    break;
  case PTL_EQ_EMPTY:
    break;
  default:
    gasneti_fatalerror("gasnetc_get_event Portals Error in PtlEQGet: %s (%i)\n at %s\n",
		       ptl_err_str[rc],rc,gasneti_current_loc);
    break;
  }

  /* No easy pickings... try polling, which may enter the kernel */
  rc = PtlEQPoll(&eq_h,1,timeout,ev,&which);
  switch (rc) {
  case PTL_OK:
    return 1;
    break;
  case PTL_EQ_EMPTY:
    break;
  default:
    gasneti_fatalerror("gasnetc_get_event Portals Error in PtlEQPoll: %s (%i)\n at %s\n",
		       ptl_err_str[rc],rc,gasneti_current_loc);
    break;
  }
  return 0;
}


/* ------------------------------------------------------------------------------------
 * Jump table to determine which event handler to run.
 * Only used in case when we register a Portals event handler with an EQ, otherwise
 * functions are called through a function pointer stored in ev->md.user_prt
 * --------------------------------------------------------------------------------- */
extern void gasnetc_event_handler(ptl_event_t *ev)
{
#if GASNETC_USE_EQ_HANDLER
  unsigned int which_md = (unsigned int)(uintptr_t)ev->md.user_ptr;

  gasneti_assert(which_md < GASNETC_NUM_MD);

  switch (which_md) {
  case GASNETC_RARAM_MD:
    RARAM_event(ev);
    break;

  case GASNETC_REQSB_MD:
    ReqSB_event(ev);
    break;

  case GASNETC_TMP_MD:
    TMPMD_event(ev);
    break;

  case GASNETC_REQRB_MD:
    ReqRB_event(ev);
    break;

  case GASNETC_RPLSB_MD:
    RplSB_event(ev);
    break;
    
  case GASNETC_CB_MD:
    CB_event(ev);
    break;

  default:
    gasneti_fatalerror("Invalid MD [%i] for event %s",(int)which_md,ptl_event_str[ev->type]);
  }
#endif
}

/* ------------------------------------------------------------------------------------
 * Called when the gasnet_core exits.
 * Dump locally collected statistics.
 * --------------------------------------------------------------------------------- */
extern void gasnetc_ptl_trace_finish(void)
{
  GASNETI_STATS_PRINTF(C,("TMPMD HWM:                         %i",gasnetc_tmpmd_hwm));
  GASNETI_STATS_PRINTF(C,("ReqSB CHUNK HWM:                   %i/%i",gasnetc_ReqSB.hwm,gasnetc_ReqSB.numchunks));
  GASNETI_STATS_PRINTF(C,("RplSB CHUNK HWM:                   %i/%i",gasnetc_RplSB.hwm,gasnetc_RplSB.numchunks));
}


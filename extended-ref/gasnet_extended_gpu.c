/* $Source: /Users/kamil/work/gasnet-cvs2/gasnet/extended-ref/Attic/gasnet_extended_gpu.c,v $
 * $Date: 2010/07/15 20:25:19 $
 * $Revision: 1.1.2.1 $
 *
 * Description: GASNet Extended API Reference Implementation for GPU extensions
 * 
 * Yili Zheng
 * LBNL 2010
 */

/* This file is to be included in gasnet_extended.c */

//#define DEBUG_GPU

#include <gasnet_internal.h> /*  for GASNET-Internal OP Interface */
#include <gasnet_extended_internal.h>

#include <gasnet_handler.h>

#include <gasnet_gpu_amhandlers.h> /* AM handlers for GPU extensions */
#include <gasnet_gpu_internal.h> /* internal GPU extension interface */
#include <gasnet_extended_gpu.h> /* public GPU extension interface */ 
#include <gasnet_gpu_cuda.h> /* device specific interface */
#include <gasnet_gpu_queue.h> /* generic queue data structure used in GPU extensions */

/* mark an op done - isget ignored for explicit ops, which is usually
   defined in gasnet_extended.c */
void gasnete_op_markdone(gasnete_op_t *op, int isget); 

#if defined(GASNET_CONDUIT_VAPI) || defined(GASNET_CONDUIT_IBV) 
#ifndef gasnete_op_markdone
void gasnete_op_markdone(gasnete_op_t *op, int isget) 
{
  if (op->type == gasnete_opExplicit) /* vapi and ibv conduit only */
    gasneti_eop_markdone((gasneti_eop_t *)op);
  else 
    gasneti_iop_markdone((gasneti_iop_t *)op, 1, isget);
}
#endif
#endif

/* If the data transfer size is less than or equal to the eager
   threshold, then the eager protocol implemented by AM Medium will be
   used.  Otherwise, the rendezvous protocol implemented by AM Long
   will be used. */
size_t gasnete_gpu_put_eager_threshold = 64*1024; /* 16 KBytes for now, to be set by env */
size_t gasnete_gpu_get_eager_threshold = 4l*1024*1024*1024; /* Set a large threshold for now so that AM Medium gets used in most cases. This is due to the size limit of AM Long Reply. */

size_t gasnete_gpu_msg_chunk_size = 1024*1024; /* 1M for now */

static gasneti_lifo_head_t gasnete_gpu_buf_free_list = GASNETI_LIFO_INITIALIZER;

/* ---------------------------------------------------------------------------- 
 * Initialization and utility routines which are just wrappers of GPU
 * device-specific code
 * ---------------------------------------------------------------------------- 
 */
int gasnete_gpu_get_device_count(void)
{
  return _gasnete_gpu_get_device_count();
}

int gasnete_gpu_get_device_id(void)
{
  gasnete_gpu_threaddata_t *td;
  gasnete_threaddata_t *_threadinfo = gasnete_mythread(); /* for GASNETE_MYTHREAD */

  td = GASNETE_GPU_MYTHREAD;
  return td->dev_id;
}

int gasnete_gpu_attach(int dev_id)
{
  static int firstcall = 1;
  int rv;

  GASNETI_TRACE_PRINTF(C,("gasnete_gpu_attach(%d)", dev_id));

  gasnete_gpu_msg_chunk_size = 
    gasneti_getenv_int_withdefault("GASNET_GPU_MSG_CHUNK_SIZE", 4*1024*1024, 0);

  gasnete_gpu_put_eager_threshold = 
        gasneti_getenv_int_withdefault("GASNET_GPU_PUT_EAGER_THRESHOLD", 4*1024, 0);

  gasnete_gpu_get_eager_threshold = 
    gasneti_getenv_int_withdefault("GASNET_GPU_GET_EAGER_THRESHOLD", 
                                   4l*1024*1024*1024, 0);
  
  rv = _gasnete_gpu_attach(dev_id);
  firstcall = 0;

  return rv;
}

void * gasnete_gpu_host_alloc(size_t nbytes)
{
  return _gasnete_gpu_host_alloc(nbytes);
}
 
void * gasnete_gpu_device_alloc(size_t nbytes)
{
  return _gasnete_gpu_device_alloc(nbytes);
}

void gasnete_gpu_device_free(void *devPtr)
{
  _gasnete_gpu_device_free(devPtr);
}

void gasnete_gpu_store(void *devDst, void *hostSrc, size_t nbytes)
{
  _gasnete_gpu_store(devDst, hostSrc, nbytes);
}

void gasnete_gpu_store_async(void *devDst, void *hostSrc, size_t nbytes, int stream_id)
{
  _gasnete_gpu_store_async(devDst, hostSrc, nbytes, stream_id);
}

void gasnete_gpu_load(void *hostDst, void *devSrc, size_t nbytes)
{
  _gasnete_gpu_load(hostDst, devSrc, nbytes);
}

void gasnete_gpu_load_async(void *hostDst, void *devSrc, size_t nbytes, int stream_id)
{
  _gasnete_gpu_load_async(hostDst, devSrc, nbytes, stream_id);
}

void gasnete_gpu_memcpy(void *dst, void *src, size_t nbytes, gpuMemcpyKind_t kind)
{
  _gasnete_gpu_memcpy(dst, src, nbytes, kind);
}

void gasnete_gpu_memset(void *dest, int val, size_t nbytes)
{
  _gasnete_gpu_memset(dest, val, nbytes); 
}

void gasnete_gpu_stream_sync(int stream_id)
{
  _gasnete_gpu_stream_sync(stream_id);
}

/* ---------------------------------------------------------------------------- 
 * 
 * Buffer management Comments: use malloc and free for simplicity for
 * now and this can be optimized by a buffer pool.  The buffers can be
 * accessed by the network if they are allocated from the segment.  We
 * assume segment-everything is configured for now.
 * ----------------------------------------------------------------------------
 */
GASNETI_INLINE(gasnete_gpu_buf_alloc)
void * gasnete_gpu_buf_alloc(void)
{
  void *buf;

  buf = gasneti_lifo_pop(&gasnete_gpu_buf_free_list);
  if (!buf) {
    //buf = gasneti_malloc(gasnete_gpu_msg_chunk_size);
    buf = gasnete_gpu_host_alloc(gasnete_gpu_msg_chunk_size);
  }
  return buf;
}

GASNETI_INLINE(gasnete_gpu_buf_free)
void gasnete_gpu_buf_free(void *buf)
{
  gasneti_assert(buf != NULL);
  gasneti_lifo_push(&gasnete_gpu_buf_free_list, buf);
}

/* ---------------------------------------------------------------------------- 
 * AM handlers for non-blocking memory-to-memory transfers
 * ---------------------------------------------------------------------------- 
 */
GASNETI_INLINE(gasnete_get_hosttogpu_reqh_inner)
void gasnete_get_hosttogpu_reqh_inner(gasnet_token_t token, 
                                      void *gpuDst, 
                                      void *hostSrc,
                                      void *data_sz,
                                      void *op) 
{
  size_t nbytes = (size_t)data_sz;

#ifdef DEBUG_GPU
  fprintf(stderr, "gasnete_get_hosttogpu_reqh_inner: token %p, gpuDst %p, hostSrc %p, data_sz %lu, op %p\n",
          token, gpuDst, hostSrc, (size_t)data_sz, op);
#endif

  gasneti_assert(nbytes <= gasnet_AMMaxMedium());
  GASNETI_SAFE(MEDIUM_REP(2,4,(token, gasneti_handleridx(gasnete_get_togpu_reph),
                               hostSrc, nbytes, PACK(gpuDst), PACK(op))));
}
SHORT_HANDLER(gasnete_get_hosttogpu_reqh, 4, 8, 
              (token, UNPACK(a0),      UNPACK(a1),      UNPACK(a2),      UNPACK(a3)     ),
              (token, UNPACK2(a0, a1), UNPACK2(a2, a3), UNPACK2(a4, a5), UNPACK2(a6, a7)));
/*---------------------------------------------------------------------------*/
GASNETI_INLINE(gasnete_get_hosttogpu_reqh2_inner)
void gasnete_get_hosttogpu_reqh2_inner(gasnet_token_t token, 
                                       void *gpuDst, 
                                       void *hostSrc, 
                                       void *data_sz,
                                       void *hostBuf, 
                                       void *op) 
{
  size_t nbytes = (size_t)data_sz;

#ifdef DEBUG_GPU
  fprintf(stderr, "gasnete_get_hosttogpu_reqh2_inner: token %p, mynode %u, gpuDst %p, hostSrc %p, data_sz %lu, hostBuf %p, op %p\n",
          token, gasnet_mynode(), gpuDst, hostSrc, (size_t)data_sz, hostBuf, op);
#endif

  gasneti_assert(nbytes <= gasnet_AMMaxLongReply());

  GASNETI_SAFE(LONG_REP(2,4,(token, gasneti_handleridx(gasnete_get_togpu_reph2),
                             hostSrc, nbytes, hostBuf, PACK(gpuDst), PACK(op))));
}
SHORT_HANDLER(gasnete_get_hosttogpu_reqh2, 5, 10, 
              (token, UNPACK(a0),      UNPACK(a1),      UNPACK(a2),      UNPACK(a3),      UNPACK(a4)      ),
              (token, UNPACK2(a0, a1), UNPACK2(a2, a3), UNPACK2(a4, a5), UNPACK2(a6, a7), UNPACK2(a8, a9)));
/*---------------------------------------------------------------------------*/
GASNETI_INLINE(gasnete_get_gputogpu_reqh_inner)
void gasnete_get_gputogpu_reqh_inner(gasnet_token_t token, 
                                     void *gpuDst, 
                                     void *gpuSrc, 
                                     void *data_sz, 
                                     void *op) 
{
  void *buf;
  size_t nbytes = (size_t)data_sz;

#ifdef DEBUG_GPU
  fprintf(stderr, "gasnete_get_gputogpu_reqh_inner: token %p, mynode %u, gpuDst %p, gpuSrc %p, data_sz %lu, op %p\n",
          token, gasnet_mynode(), gpuDst, gpuSrc, (size_t)data_sz, op);
#endif

  gasneti_assert(nbytes <= gasnet_AMMaxMedium());
  gasneti_assert(nbytes <= gasnete_gpu_msg_chunk_size);

  buf = gasnete_gpu_buf_alloc(); 
  gasnete_gpu_load(buf, gpuSrc, nbytes);
  
  GASNETI_SAFE(MEDIUM_REP(2,4,(token, 
                               gasneti_handleridx(gasnete_get_togpu_reph),
                               buf, nbytes, PACK(gpuDst), PACK(op))));

  gasnete_gpu_buf_free(buf);
}
SHORT_HANDLER(gasnete_get_gputogpu_reqh, 4, 8, 
              (token, UNPACK(a0),      UNPACK(a1),      UNPACK(a2),      UNPACK(a3)     ),
              (token, UNPACK2(a0, a1), UNPACK2(a2, a3), UNPACK2(a4, a5), UNPACK2(a6, a7)));
/*---------------------------------------------------------------------------*/
GASNETI_INLINE(gasnete_get_gputogpu_reqh2_inner)
void gasnete_get_gputogpu_reqh2_inner(gasnet_token_t token, 
                                      void *gpuDst, 
                                      void *gpuSrc, 
                                      void *data_sz, 
                                      void *hostBuf, 
                                      void *op) 
{
  void *buf;
  size_t nbytes = (size_t)data_sz;

#ifdef DEBUG_GPU
  fprintf(stderr, "gasnete_get_gputogpu_reqh2_inner: token %p, mynode %u, gpuDst %p, gpuSrc %p, data_sz %lu, hostBuf %p, op %p\n",
          token, gasnet_mynode(), gpuDst, gpuSrc, (size_t)data_sz, hostBuf, op);
#endif

  gasneti_assert(nbytes <= gasnet_AMMaxLongReply());
  gasneti_assert(nbytes <= gasnete_gpu_msg_chunk_size);

  buf = gasnete_gpu_buf_alloc(); 
  gasnete_gpu_load(buf, gpuSrc, nbytes);
  
  GASNETI_SAFE(LONG_REP(2,4,(token, 
                             gasneti_handleridx(gasnete_get_togpu_reph2),
                             buf, nbytes, hostBuf, PACK(gpuDst), PACK(op))));

  gasnete_gpu_buf_free(buf);
}
SHORT_HANDLER(gasnete_get_gputogpu_reqh2, 5, 10, 
              (token, UNPACK(a0),      UNPACK(a1),      UNPACK(a2),      UNPACK(a3),      UNPACK(a4)      ),
              (token, UNPACK2(a0, a1), UNPACK2(a2, a3), UNPACK2(a4, a5), UNPACK2(a6, a7), UNPACK2(a8, a9)));
/*---------------------------------------------------------------------------*/

GASNETI_INLINE(gasnete_get_togpu_reph_inner)
void gasnete_get_togpu_reph_inner(gasnet_token_t token, 
                                  void *addr, 
                                  size_t nbytes,
                                  void *gpuDst, 
                                  void *op) 
{
#ifdef DEBUG_GPU
  fprintf(stderr, "gasnete_get_togpu_reph_inner: token %p, mynode %u, addr %p, nbytes %lu, gpuDst %p, op %p\n",
          token, gasnet_mynode(), addr, (size_t)nbytes, gpuDst, op);
#endif

  gasnete_gpu_store(gpuDst, addr, nbytes);
  /* need to handle two types of ops: eop and iop */
  gasnete_op_markdone((gasnete_op_t *)op, 1);
}
MEDIUM_HANDLER(gasnete_get_togpu_reph, 2, 4,
               (token,addr,nbytes, UNPACK(a0),      UNPACK(a1)    ),
               (token,addr,nbytes, UNPACK2(a0, a1), UNPACK2(a2, a3)));
/*---------------------------------------------------------------------------*/
GASNETI_INLINE(gasnete_get_togpu_reph2_inner)
void gasnete_get_togpu_reph2_inner(gasnet_token_t token, 
                                   void *addr, 
                                   size_t nbytes,
                                   void *gpuDst, 
                                   void *op) 
{
#ifdef DEBUG_GPU
  fprintf(stderr, "gasnete_get_togpu_reph2_inner: token %p, mynode %u, addr %p, nbytes %lu, gpuDst %p, op %p\n",
          token, gasnet_mynode(), addr, (size_t)nbytes, gpuDst, op);
#endif

  gasnete_gpu_store(gpuDst, addr, nbytes);
  gasnete_gpu_buf_free(addr); /* hostBuf is previously allocated. */
  /* need to handle two types of ops: eop and iop */
  gasnete_op_markdone((gasnete_op_t *)op, 1);
}
LONG_HANDLER(gasnete_get_togpu_reph2, 2, 4,
             (token,addr,nbytes, UNPACK(a0),      UNPACK(a1)    ),
             (token,addr,nbytes, UNPACK2(a0, a1), UNPACK2(a2, a3)));
/*---------------------------------------------------------------------------*/
GASNETI_INLINE(gasnete_get_gputohost_reqh_inner)
void gasnete_get_gputohost_reqh_inner(gasnet_token_t token, 
                                      void *hostDst, 
                                      void *gpuSrc, 
                                      void *data_sz, 
                                      void *op) 
{
  void *buf;
  size_t nbytes = (size_t)data_sz;

#ifdef DEBUG_GPU
  fprintf(stderr, "gasnete_get_gputohost_reqh_inner: token %p, mynode %u, hostDst %p, gpuSrc %p, nbytes %lu, op %p\n",
          token, gasnet_mynode(), hostDst, gpuSrc, (size_t)nbytes, op);
#endif

  gasneti_assert(nbytes <= gasnete_gpu_msg_chunk_size);
  gasneti_assert(nbytes <= gasnet_AMMaxLongReply());

  buf = gasnete_gpu_buf_alloc();
  gasnete_gpu_load(buf, gpuSrc, nbytes);
  GASNETI_SAFE(LONG_REP(1,2,(token, gasneti_handleridx(gasnete_get_markdone_reph),
                             buf, nbytes, hostDst, PACK(op))));
  gasnete_gpu_buf_free(buf);
}
SHORT_HANDLER(gasnete_get_gputohost_reqh, 4, 8, 
              (token, UNPACK(a0),      UNPACK(a1),      UNPACK(a2),      UNPACK(a3)     ),
              (token, UNPACK2(a0, a1), UNPACK2(a2, a3), UNPACK2(a4, a5), UNPACK2(a6, a7)));
/*---------------------------------------------------------------------------*/
GASNETI_INLINE(gasnete_gpu_getremotebuf_reqh_inner)
void gasnete_gpu_getremotebuf_reqh_inner(gasnet_token_t token, 
                                         void *nbytes, 
                                         void *buf_ptr,
                                         void *ready_ptr) 
{
  void *gpu_buffer;

#ifdef DEBUG_GPU
  fprintf(stderr, "gasnete_gpu_getremotebuf_reqh_inner: token %p, mynode %u, nbytes %lu, buf_ptr %p, ready_ptr %p\n",
          token, gasnet_mynode(), (size_t)nbytes, buf_ptr, ready_ptr);
#endif

  gasneti_assert((size_t)nbytes <= gasnete_gpu_msg_chunk_size);

  /* assume gasnet uses segment-everything so the gpu_buffer is
     network-accesable. */
  gpu_buffer = gasnete_gpu_buf_alloc();
  GASNETI_SAFE(SHORT_REP(3,6,(token, gasneti_handleridx(gasnete_gpu_getremotebuf_reph),
                              PACK(gpu_buffer), PACK(buf_ptr), PACK(ready_ptr))));
}
SHORT_HANDLER(gasnete_gpu_getremotebuf_reqh, 3, 6, 
              (token, UNPACK(a0),      UNPACK(a1),      UNPACK(a2)     ),
              (token, UNPACK2(a0, a1), UNPACK2(a2, a3), UNPACK2(a4, a5)));
/*---------------------------------------------------------------------------*/
GASNETI_INLINE(gasnete_gpu_getremotebuf_reph_inner)
void gasnete_gpu_getremotebuf_reph_inner(gasnet_token_t token, 
                                         void *hostBuf, 
                                         void *buf_ptr, 
                                         void *ready_ptr)

{
  /* update the destination buffer */
  *((void **)buf_ptr) = hostBuf;
  *((int32_t *)ready_ptr) = 1;
#ifdef DEBUG_GPU
  fprintf(stderr, "gasnete_gpu_getremotebuf_reph_inner: mynode %u, hostBuf %p, buf_ptr %p, ready %d\n",
          gasnet_mynode(), hostBuf, buf_ptr, *(int *)ready_ptr);
#endif
}
SHORT_HANDLER(gasnete_gpu_getremotebuf_reph, 3, 6, 
              (token, UNPACK(a0),      UNPACK(a1),      UNPACK(a2)     ),
              (token, UNPACK2(a0, a1), UNPACK2(a2, a3), UNPACK2(a4, a5)));
/*---------------------------------------------------------------------------*/
GASNETI_INLINE(gasnete_put_togpu_reqh_inner)
void gasnete_put_togpu_reqh_inner(gasnet_token_t token, 
                                  void *addr, 
                                  size_t nbytes,
                                  void *gpuDst, 
                                  void *op) 
{
#ifdef DEBUG_GPU
  fprintf(stderr, "gasnete_put_togpu_reqh_inner: token %p, mynode %u, addr %p, nbytes %lu , gpuDst %p, op %p\n",
          token, gasnet_mynode(), addr, nbytes, gpuDst, op);
#endif

  gasnete_gpu_store(gpuDst, addr, nbytes);
  GASNETI_SAFE(SHORT_REP(1,2,(token, gasneti_handleridx(gasnete_put_markdone_reph),
                              PACK(op))));
}
MEDIUM_HANDLER(gasnete_put_togpu_reqh,2,4, 
               (token,addr,nbytes, UNPACK(a0),      UNPACK(a1)     ),
               (token,addr,nbytes, UNPACK2(a0, a1), UNPACK2(a2, a3)));
/*---------------------------------------------------------------------------*/
GASNETI_INLINE(gasnete_put_togpu_reqh2_inner)
void gasnete_put_togpu_reqh2_inner(gasnet_token_t token, 
                                   void *addr, 
                                   size_t nbytes,
                                   void *gpuDst, 
                                   void *op) 
{
#ifdef DEBUG_GPU
  fprintf(stderr, "gasnete_put_togpu_reqh2_inner: mynode %u, addr %p, nbytes %lu, gpuDst %p, op %p\n",
          gasnet_mynode(), addr, nbytes, gpuDst, op);
  fprintf(stderr, "addr[0]=%d\n", *((int *)addr));
#endif

  gasnete_gpu_store(gpuDst, addr, nbytes);
  gasnete_gpu_buf_free(addr); /* free the gpu buffer allocated in gasnete_gpu_getremotebuf_reqh_inner */
  GASNETI_SAFE(SHORT_REP(1,2,(token, gasneti_handleridx(gasnete_put_markdone_reph),
                              PACK(op))));
}
LONG_HANDLER(gasnete_put_togpu_reqh2, 2, 4, 
             (token,addr,nbytes, UNPACK(a0),      UNPACK(a1)     ),
             (token,addr,nbytes, UNPACK2(a0, a1), UNPACK2(a2, a3)));
/*---------------------------------------------------------------------------*/
GASNETI_INLINE(gasnete_put_tohost_reqh_inner)
void gasnete_put_tohost_reqh_inner(gasnet_token_t token, 
                                   void *addr, 
                                   size_t nbytes,
                                   void *op) 
{
#ifdef DEBUG_GPU
  fprintf(stderr, "gasnete_put_tohost_reqh_inner: addr %p, nbytes %lu, op %p\n",
          addr, nbytes, op);
#endif

  GASNETI_SAFE(SHORT_REP(1,2,(token, gasneti_handleridx(gasnete_put_markdone_reph),
                              PACK(op))));
}
LONG_HANDLER(gasnete_put_tohost_reqh, 1, 2, 
             (token,addr,nbytes, UNPACK(a0),   ),
             (token,addr,nbytes, UNPACK2(a0, a1)));
/*---------------------------------------------------------------------------*/
GASNETI_INLINE(gasnete_memset_togpu_reqh_inner)
void gasnete_memset_togpu_reqh_inner(gasnet_token_t token, 
                                     void *gpuDst, 
                                     gasnet_handlerarg_t val, 
                                     void *data_sz, 
                                     void *op) 
{
  size_t nbytes = (size_t)data_sz;

#ifdef DEBUG_GPU
  fprintf(stderr, "gasnete_memset_togpu_reqh_inner: mynode %u, gpuDst %p, val %u, data_sz %lu, op %p\n",
          gasnet_mynode(), gpuDst, (uint32_t)val, nbytes, op);
#endif

  gasnete_gpu_memset(gpuDst, (int)val, nbytes);
  GASNETI_SAFE(SHORT_REP(1,2,(token, gasneti_handleridx(gasnete_put_markdone_reph),
                              PACK(op))));
}
SHORT_HANDLER(gasnete_memset_togpu_reqh, 4, 7,
              (token, UNPACK(a0),      a1, UNPACK(a2),      UNPACK(a3)     ),
              (token, UNPACK2(a0, a1), a2, UNPACK2(a3, a4), UNPACK2(a5, a6)));
/*---------------------------------------------------------------------------*/
GASNETI_INLINE(gasnete_put_markdone_reph_inner)
void gasnete_put_markdone_reph_inner(gasnet_token_t token, 
                                     void *op) 
{
#ifdef DEBUG_GPU
  fprintf(stderr, "gasnete_put_markdone_reph_inner: mynode %u, op %p\n", gasnet_mynode(), op);
#endif

  gasnete_op_markdone((gasnete_op_t *)op, 0); /* explicit or put implicit */
}
SHORT_HANDLER(gasnete_put_markdone_reph, 1, 2,
              (token, UNPACK(a0)    ),
              (token, UNPACK2(a0, a1)));
/*---------------------------------------------------------------------------*/
GASNETI_INLINE(gasnete_get_markdone_reph_inner)
void gasnete_get_markdone_reph_inner(gasnet_token_t token, 
                                     void *addr, 
                                     size_t nbytes, 
                                     void *op) 
{
#ifdef DEBUG_GPU
  fprintf(stderr, "gasnete_get_markdone_reph_inner: mynode %u, addr %p, nbytes %lu, op %p\n",
          gasnet_mynode(), addr, nbytes, op);
#endif

  gasnete_op_markdone((gasnete_op_t *)op, 1); /* get implicit */
}
LONG_HANDLER(gasnete_get_markdone_reph, 1, 2,
             (token, addr, nbytes, UNPACK(a0)    ),
             (token, addr, nbytes, UNPACK2(a0, a1)));

/* ---------------------------------------------------------------------------- 
 * Non-blocking memory-to-memory transfers (implicit handle)
 * ---------------------------------------------------------------------------- 
 */
GASNETI_INLINE(gasnete_get_togpu_nbi_inner)
void gasnete_get_togpu_nbi_inner(void *dest, gasnet_node_t node, void *src, 
                                 size_t nbytes, gasnet_handler_t reqhandler,
                                 gasnet_handler_t reqhandler2
                                 GASNETE_THREAD_FARG) 
{
  gasneti_iop_t *iop;
  unsigned int nops;
  size_t chunk_sz, remain_sz;
  uint8_t *psrc = src;
  uint8_t *pdest = dest;

#ifdef DEBUG_GPU
  fprintf(stderr, "gasnete_get_togpu_nbi_inner: mynode %u, dest %p, src node %u, src %p, nbytes %lu.\n",
          gasnet_mynode(), dest, node, src, nbytes);
#endif

  if (nbytes == 0)
    return;

  remain_sz = nbytes;

  if (nbytes <= gasnete_gpu_get_eager_threshold) {
    /* use AM Medium */
    if (gasnete_gpu_msg_chunk_size > gasnet_AMMaxMedium())
      chunk_sz = gasnet_AMMaxMedium();
    else
      chunk_sz = gasnete_gpu_msg_chunk_size;

    nops = (nbytes + chunk_sz - 1) / chunk_sz;
    iop = gasneti_iop_register(nops, 1 GASNETE_THREAD_PASS);
    
    if (remain_sz < chunk_sz)
      chunk_sz = remain_sz; /* one packet */

    /* Send messages in chunk_sz */
    while (remain_sz >= chunk_sz) {
      GASNETI_SAFE(SHORT_REQ(4,8,(node, reqhandler, PACK(pdest), PACK(psrc), PACK(chunk_sz), PACK(iop))));
      remain_sz -= chunk_sz;
      psrc += chunk_sz;
      pdest += chunk_sz;
      if (remain_sz < chunk_sz && remain_sz > 0)
        chunk_sz = remain_sz;
    }
  } else {
    /* use AM Long */
    void *hostBuf;
    
    if (gasnete_gpu_msg_chunk_size > nbytes) 
      chunk_sz = nbytes;
    else
      chunk_sz = gasnete_gpu_msg_chunk_size;
 
    nops = (nbytes + chunk_sz - 1) / chunk_sz;
    iop = gasneti_iop_register(nops, 1 GASNETE_THREAD_PASS);
    
    while (remain_sz >= chunk_sz) {
      /* hostBuf will be freed in the reply handler. */
      gasneti_assert(chunk_sz <= gasnete_gpu_msg_chunk_size);
      hostBuf = gasnete_gpu_buf_alloc();
      GASNETI_SAFE(SHORT_REQ(5,10,(node, reqhandler2, PACK(pdest), PACK(psrc), PACK(chunk_sz), PACK(hostBuf), PACK(iop))));
      remain_sz -= chunk_sz;
      psrc += chunk_sz;
      pdest += chunk_sz;
      if (remain_sz < chunk_sz && remain_sz > 0)
        chunk_sz = remain_sz;
    }
  }
}

void gasnete_get_hosttogpu_nbi(void *dest, gasnet_node_t node, void *src, 
                               size_t nbytes GASNETE_THREAD_FARG) 
{
  if (gasnet_mynode() == node) {
    gasnete_gpu_store(dest, src, nbytes);
    return;
  }

  gasnete_get_togpu_nbi_inner(dest, node, src, nbytes, 
                              gasneti_handleridx(gasnete_get_hosttogpu_reqh),
                              gasneti_handleridx(gasnete_get_hosttogpu_reqh2)
                              GASNETE_THREAD_PASS); 
}

void gasnete_get_gputogpu_nbi(void *dest, gasnet_node_t node, void *src, 
                              size_t nbytes GASNETE_THREAD_FARG) 
{
  gasnete_get_togpu_nbi_inner(dest, node, src, nbytes, 
                              gasneti_handleridx(gasnete_get_gputogpu_reqh), 
                              gasneti_handleridx(gasnete_get_gputogpu_reqh2)
                              GASNETE_THREAD_PASS); 
}

void gasnete_get_gputohost_nbi(void *dest, gasnet_node_t node, void *src, 
                               size_t nbytes GASNETE_THREAD_FARG) 
{

  gasneti_iop_t *iop;
  unsigned int nops;
  size_t chunk_sz, remain_sz;
  uint8_t *psrc = src;
  uint8_t *pdest = dest;

#ifdef DEBUG_GPU
  fprintf(stderr, "gasnete_get_gputohost_nbi: mynode %u, dest %p, src node %u, src %p, nbytes %lu.\n",
          gasnet_mynode(), dest, node, src, nbytes);
#endif

  if (nbytes == 0)
    return;

  /* If the src node is local, load the data directly from the gpu to
     the host. */
  if (gasnet_mynode() == node) {
    gasnete_gpu_load(dest, src, nbytes);
    return;
  }

  remain_sz = nbytes;

  if (gasnete_gpu_msg_chunk_size > nbytes) 
    chunk_sz = nbytes;
  else
    chunk_sz = gasnete_gpu_msg_chunk_size;


  if (chunk_sz > gasnet_AMMaxLongReply())
    chunk_sz = gasnet_AMMaxLongReply();

  nops = (nbytes + chunk_sz - 1) / chunk_sz;
  iop = gasneti_iop_register(nops, 1 GASNETE_THREAD_PASS);

#ifdef DEBUG_GPU
  fprintf(stderr, "gasnete_get_gputohst_inner: mynode %u, chunk_sz %lu, nops %d.\n", 
          gasnet_mynode(), chunk_sz, nops);

#endif

  while (remain_sz >= chunk_sz) {
    GASNETI_SAFE(SHORT_REQ(4,8,(node, gasneti_handleridx(gasnete_get_gputohost_reqh),
                                PACK(pdest), PACK(psrc), PACK(chunk_sz), PACK(iop))));
    remain_sz -= chunk_sz;
    psrc += chunk_sz;
    pdest += chunk_sz;
    if (remain_sz < chunk_sz && remain_sz > 0)
      chunk_sz = remain_sz;
  }    
}

void gasnete_put_hosttogpu_nbi(gasnet_node_t node, void *dest, void *src, 
                               size_t nbytes GASNETE_THREAD_FARG) 
{
  gasneti_iop_t *iop;
  unsigned int nops;
  size_t chunk_sz, remain_sz;
  uint8_t *psrc = src;
  uint8_t *pdest = dest;

#ifdef DEBUG_GPU
  fprintf(stderr, "gasnete_put_hosttogpu_nbi: node %d, dest %p, src %p, nbytes %lu\n",
          node, dest, src, nbytes);
#endif

  if (nbytes == 0)
    return;
  
  /* If the src node is local, store the data directly from the host
     to the gpu. */
  if (gasnet_mynode() == node) {
    gasnete_gpu_store(dest, src, nbytes);
    return;
  }

  remain_sz = nbytes;

  if (nbytes <= gasnete_gpu_put_eager_threshold) {
    /* eager protocol implmented with AM Medium. */
    if (gasnete_gpu_msg_chunk_size > gasnet_AMMaxMedium())
      chunk_sz = gasnet_AMMaxMedium();
    else
      chunk_sz = gasnete_gpu_msg_chunk_size;

    nops = (nbytes + chunk_sz - 1) / chunk_sz;
    iop = gasneti_iop_register(nops, 0 GASNETE_THREAD_PASS);

    /* Send messages in chunk_sz */
    if (remain_sz < chunk_sz)
      chunk_sz = remain_sz; /* one packet */

#ifdef DEBUG_GPU
    fprintf(stderr, "gasnete_put_hosttogpu_nbi: eager protocol, gasnete_gpu_put_eager_threshold %lu.\n", 
            gasnete_gpu_put_eager_threshold);
    fprintf(stderr, "gasnete_put_hosttogpu_nbi: AMMaxMedium %lu, chunk_sz %lu, nops %u.\n", 
            gasnet_AMMaxMedium(), chunk_sz, nops);
#endif

    while (remain_sz >= chunk_sz) {
      GASNETI_SAFE(MEDIUM_REQ(2,4,(node, gasneti_handleridx(gasnete_put_togpu_reqh),
                                   psrc, chunk_sz, PACK(pdest), PACK(iop))));
      remain_sz -= chunk_sz;
      psrc += chunk_sz;
      pdest += chunk_sz;

      if (remain_sz < chunk_sz && remain_sz > 0)
        chunk_sz = remain_sz; /* the last chunk */
    }
  } else {
    /* rendezvous protocol implemented with AM long: first request a
       remote host buffer and then use AM Long to transder. */
    gasnete_queue_t *gpu_op_queue; 
    gasnete_gpu_op_t *gpu_op;
    
    gpu_op_queue = gasnete_queue_new();
    
    if (gasnete_gpu_msg_chunk_size > nbytes)
      chunk_sz = nbytes;
    else
      chunk_sz = gasnete_gpu_msg_chunk_size;

    nops = (nbytes + chunk_sz - 1) / chunk_sz;
    iop = gasneti_iop_register(nops, 0 GASNETE_THREAD_PASS);

#ifdef DEBUG_GPU
    fprintf(stderr, "gasnete_put_hosttogpu_nbi: rendezvous protocol, gasnete_gpu_put_eager_threshold %lu.\n", 
            gasnete_gpu_put_eager_threshold);
    fprintf(stderr, "gasnete_put_hosttogpu_nbi: gasnete_gpu_msg_chunk_size %lu, chunk_sz %lu, nops %u.\n", 
            gasnete_gpu_msg_chunk_size, chunk_sz, nops);
#endif

    /* Send requests for remote host buffers and enqueue the gpu operations */
    while (remain_sz >= chunk_sz) {
      gpu_op = gasnete_gpu_op_new(GASNETE_PUT_HOST2GPU, gasnet_mynode(), psrc, NULL,
                                  node, pdest, NULL, chunk_sz, -1, NULL, NULL);
#ifdef DEBUG_GPU
      gasnete_gpu_op_print(stderr, gpu_op);
#endif
      gasnete_queue_enqueue(gpu_op_queue, gpu_op);
      GASNETI_SAFE(SHORT_REQ(3,6,(node, gasneti_handleridx(gasnete_gpu_getremotebuf_reqh), 
                                  PACK(chunk_sz), PACK((void *)&(gpu_op->dstBuf)), 
                                  PACK((void *)&(gpu_op->ready)))));
      remain_sz -= chunk_sz;
      psrc += chunk_sz;
      pdest += chunk_sz;
      if (remain_sz < chunk_sz && remain_sz > 0)
        chunk_sz = remain_sz; /* the last chunk */
    }
    
    /* Poll the gpu_op_queue to check if any of the remote buffers are
       ready; send data when a remote buffer is available. */
    while (!gasnete_queue_is_empty(gpu_op_queue)) {
      gasnete_qnode_t *qnode = gpu_op_queue->head;
      gasnete_qnode_t *tmp_qnode;
      while (qnode != NULL) {
        gpu_op = (gasnete_gpu_op_t *)qnode->data;
        if (gpu_op->ready) {
#ifdef DEBUG_GPU
          gasnete_gpu_op_print(stderr, gpu_op);
          fprintf(stderr, "gasnete_put_host2gpu: mynode %u src[0]=%d\n", 
                  gasnet_mynode(), *((int *)gpu_op->srcPtr));
#endif
          gasneti_assert(gpu_op->dstBuf != NULL);
          GASNETI_SAFE(LONG_REQ(2,4,(node, gasneti_handleridx(gasnete_put_togpu_reqh2),
                                     gpu_op->srcPtr, gpu_op->nbytes, gpu_op->dstBuf,
                                     PACK(gpu_op->dstPtr), PACK(iop))));
          gpu_op->done = 1;
          gasnete_gpu_op_free(gpu_op);
          tmp_qnode = qnode;
          qnode = qnode->next;
          gasnete_queue_remove_node(gpu_op_queue, tmp_qnode);
        } else {
          qnode = qnode->next;
        }
        gasnetc_AMPoll();
      }
    }
    
    /* all done */
    gasnete_queue_free(gpu_op_queue);
  }
}

void gasnete_put_gputogpu_nbi(gasnet_node_t node, void *dest, void *src, 
                              size_t nbytes GASNETE_THREAD_FARG) 
{
  void *buf;
  gasneti_iop_t *iop;
  unsigned int nops;
  size_t chunk_sz, remain_sz;
  uint8_t *psrc = src;
  uint8_t *pdest = dest;
  int stream_id;

#ifdef DEBUG_GPU
  fprintf(stderr, "gasnete_put_gputogpu_nbi: node %d, dest %p, src %p, nbytes %lu\n",
          node, dest, src, nbytes);
#endif
  
  if (nbytes == 0)
    return;

  /* If the dest node is local, load the data from the src gpu to the
     host and then store the data from the host to the dest gpu.  If
     the src gpu is same as the dest gpu, it is just an internal data
     transfer on the gpu. Currently, we cannot support multiple GPUs
     per GASNet node so the src GPU must be the same as the dest GPU
     if the src node is the same as the dest node. */
  if (gasnet_mynode() == node) {
    gasnete_gpu_memcpy(dest, src, nbytes, gpuMemcpyDeviceToDevice);
    return;
  }

  remain_sz = nbytes;
  buf = gasnete_gpu_buf_alloc();
  
  if (nbytes <= gasnete_gpu_put_eager_threshold) {
    /* eager protocol implmented with AM Medium. */
    if (gasnete_gpu_msg_chunk_size > gasnet_AMMaxMedium())
      chunk_sz = gasnet_AMMaxMedium();
    else
      chunk_sz = gasnete_gpu_msg_chunk_size;

    nops = (nbytes + chunk_sz - 1) / chunk_sz;
    iop = gasneti_iop_register(nops, 0 GASNETE_THREAD_PASS);

    /* Send messages in chunk_sz */
    if (remain_sz < chunk_sz)
      chunk_sz = remain_sz; /* one packet */

#ifdef DEBUG_GPU
    fprintf(stderr, "gasnete_put_gputogpu_nbi: eager protocol, gasnete_gpu_put_eager_threshold %lu.\n", 
            gasnete_gpu_put_eager_threshold);
    fprintf(stderr, "gasnete_put_gputogpu_nbi: AMMaxMedium %lu, chunk_sz %lu, nops %u.\n", 
            gasnet_AMMaxMedium(), chunk_sz, nops);
#endif

    while (remain_sz >= chunk_sz) {
      gasnete_gpu_load(buf, psrc, chunk_sz);
      GASNETI_SAFE(MEDIUM_REQ(2,4,(node, gasneti_handleridx(gasnete_put_togpu_reqh),
                                   buf, chunk_sz, PACK(pdest), PACK(iop))));
      remain_sz -= chunk_sz;
      psrc += chunk_sz;
      pdest += chunk_sz;

      if (remain_sz < chunk_sz && remain_sz > 0)
        chunk_sz = remain_sz; /* the last chunk */
    }
  } else {
    /* rendezvous protocol implemented with AM long: first request a
       remote host buffer and then use AM Long to transder. */
    gasnete_queue_t *gpu_op_queue; 
    gasnete_gpu_op_t *gpu_op;
    
    gpu_op_queue = gasnete_queue_new();
    
    if (gasnete_gpu_msg_chunk_size > nbytes)
      chunk_sz = nbytes;
    else
      chunk_sz = gasnete_gpu_msg_chunk_size;

    nops = (nbytes + chunk_sz - 1) / chunk_sz;
    iop = gasneti_iop_register(nops, 0 GASNETE_THREAD_PASS);

#ifdef DEBUG_GPU
    fprintf(stderr, "gasnete_put_gputogpu_nbi: rendezvous protocol, gasnete_gpu_put_eager_threshold %lu.\n", 
            gasnete_gpu_put_eager_threshold);
    fprintf(stderr, "gasnete_put_gputogpu_nbi: gasnete_gpu_msg_chunk_size %lu, chunk_sz %lu, nops %u.\n", 
            gasnete_gpu_msg_chunk_size, chunk_sz, nops);
#endif

    /* Send requests for remote host buffers and enqueue the gpu operations */
    while (remain_sz >= chunk_sz) {
      gpu_op = gasnete_gpu_op_new(GASNETE_PUT_HOST2GPU, gasnet_mynode(), psrc, NULL,
                                  node, pdest, NULL, chunk_sz, -1, NULL, NULL);
#ifdef DEBUG_GPU
      gasnete_gpu_op_print(stderr, gpu_op);
#endif
      gasnete_queue_enqueue(gpu_op_queue, gpu_op);
      GASNETI_SAFE(SHORT_REQ(3,6,(node, gasneti_handleridx(gasnete_gpu_getremotebuf_reqh), 
                                  PACK(chunk_sz), PACK((void *)&(gpu_op->dstBuf)), 
                                  PACK((void *)&(gpu_op->ready)))));
      remain_sz -= chunk_sz;
      psrc += chunk_sz;
      pdest += chunk_sz;
      if (remain_sz < chunk_sz && remain_sz > 0)
        chunk_sz = remain_sz; /* the last chunk */
    }
    
    /* Poll the gpu_op_queue to check if any of the remote buffers are
       ready; send data when a remote buffer is available. */
    while (!gasnete_queue_is_empty(gpu_op_queue)) {
      gasnete_qnode_t *qnode = gpu_op_queue->head;
      gasnete_qnode_t *tmp_qnode;
      while (qnode != NULL) {
        gpu_op = (gasnete_gpu_op_t *)qnode->data;
        if (gpu_op->ready) {
          gasnete_gpu_load(buf, gpu_op->srcPtr, gpu_op->nbytes);
#ifdef DEBUG_GPU
          gasnete_gpu_op_print(stderr, gpu_op);
          fprintf(stderr, "gasnete_put_host2gpu: mynode %u src[0]=%d\n", 
                  gasnet_mynode(), *((int *)buf));
#endif
          gasneti_assert(gpu_op->dstBuf != NULL);
          GASNETI_SAFE(LONG_REQ(2,4,(node, gasneti_handleridx(gasnete_put_togpu_reqh2),
                                     buf, gpu_op->nbytes, gpu_op->dstBuf,
                                     PACK(gpu_op->dstPtr), PACK(iop))));
          gpu_op->done = 1;
          gasnete_gpu_op_free(gpu_op);
          tmp_qnode = qnode;
          qnode = qnode->next;
          gasnete_queue_remove_node(gpu_op_queue, tmp_qnode);
        } else {
          qnode = qnode->next;
        }
        gasnetc_AMPoll();
      }
    }    
    /* all done */
    gasnete_queue_free(gpu_op_queue);
  }

  gasnete_gpu_buf_free(buf);
}

void gasnete_put_gputohost_nbi(gasnet_node_t node, void *dest, void *src, 
                               size_t nbytes GASNETE_THREAD_FARG)
{
  gasneti_iop_t *iop;
  unsigned int nops;
  void *bufs[MAX_GPU_STREAM_NUM];
  //void *buf;
  uint8_t *psrc = src;
  uint8_t *pdest = dest;  
  size_t chunk_sz, remain_sz, next_chunk_sz;
  gasnet_node_t mynode = gasnet_mynode();
  int stream_id, next_stream_id;

#ifdef DEBUG_GPU
  fprintf(stderr, "gasnete_put_gputohost_nbi: mynode %u, dst node %u, dest %p, src %p, nbytes %lu.\n",
          mynode, node, dest, src, nbytes);
#endif

  if (nbytes == 0)
    return;

  if (mynode == node) {
    gasnete_gpu_load(dest, src, nbytes);
    return;
  }

  if (gasnete_gpu_msg_chunk_size > nbytes)
    chunk_sz = nbytes;
  else
    chunk_sz = gasnete_gpu_msg_chunk_size;

  for (stream_id=0; stream_id<MAX_GPU_STREAM_NUM; stream_id++)
    bufs[stream_id] = gasnete_gpu_buf_alloc();
  //buf = gasnete_gpu_buf_alloc();

  remain_sz = nbytes;

#ifdef DEBUG_GPU
  fprintf(stderr, "gasnete_put_gputohost_nbi: mynode %u, chunk_sz %lu.\n",
          mynode, chunk_sz);
#endif

  //nops = (nbytes + chunk_sz - 1) / chunk_sz;
  //iop = gasneti_iop_register(nops, 0 GASNETE_THREAD_PASS);
  
  /* send many messages in chunk_sz size */
  stream_id = 0;
  next_stream_id = 1;
  gasnete_gpu_load_async(bufs[stream_id], psrc, chunk_sz, stream_id);
  while (remain_sz >= chunk_sz) {
    remain_sz -= chunk_sz;
    //gasnete_gpu_load(buf, psrc, chunk_sz);
    // preload the next chunk if it exists 
    if (remain_sz > 0) {
      next_stream_id = (stream_id + 1)%MAX_GPU_STREAM_NUM;
      next_chunk_sz = (remain_sz > chunk_sz) ? chunk_sz : remain_sz;
      gasnete_gpu_load_async(bufs[next_stream_id], psrc+chunk_sz, next_chunk_sz,
                             next_stream_id);
    }
    gasnete_gpu_stream_sync(stream_id);
    // Don't know why put_nbi doesn't work, try AM Long Req
    // gasnet_put_nbi(node, pdest, buf, chunk_sz);
    gasnet_put_nbi(node, pdest, bufs[stream_id], chunk_sz);
    
    /* GASNETI_SAFE(LONG_REQ(1,2,(node, gasneti_handleridx(gasnete_put_tohost_reqh), */
    /*                            buf, chunk_sz, pdest, PACK(iop)))); */

    psrc += chunk_sz;
    pdest += chunk_sz;
    
    stream_id = next_stream_id;

    if (remain_sz < chunk_sz && remain_sz > 0)
      chunk_sz = remain_sz; /* last chunk */
  }
  
  /* all done */
  gasneti_assert(remain_sz == 0);
  for (stream_id=0; stream_id<MAX_GPU_STREAM_NUM; stream_id++)
    gasnete_gpu_buf_free(bufs[stream_id]);
  //gasnete_gpu_buf_free(buf);
}

void gasnete_memset_togpu_nbi(gasnet_node_t node, void *dest, int val, 
                              size_t nbytes GASNETE_THREAD_FARG) 
{
  gasneti_iop_t *iop;

  if (nbytes == 0)
    return;

  iop = gasneti_iop_register(1, 0 GASNETE_THREAD_PASS);
  
  GASNETI_SAFE(SHORT_REQ(4,7,(node, gasneti_handleridx(gasnete_memset_togpu_reqh),
                              PACK(dest), (gasnet_handlerarg_t)val, 
                              PACK(nbytes), PACK(iop))));
}

/* ---------------------------------------------------------------------------- 
 * Non-blocking memory-to-memory transfers (explicit handle)
 * ---------------------------------------------------------------------------- 
 */
gasnet_handle_t gasnete_get_hosttogpu_nb(void *dest, gasnet_node_t node, 
                                         void *src, size_t nbytes 
                                         GASNETE_THREAD_FARG) 
{
  if (nbytes == 0)
    return GASNET_INVALID_HANDLE;

  gasnete_begin_nbi_accessregion(1 /* enable recursion */ GASNETE_THREAD_PASS);
  gasnete_get_hosttogpu_nbi(dest, node, src, nbytes GASNETE_THREAD_PASS);
  return gasnete_end_nbi_accessregion(GASNETE_THREAD_PASS_ALONE);
}

gasnet_handle_t gasnete_get_gputogpu_nb(void *dest, gasnet_node_t node, 
                                        void *src, size_t nbytes 
                                        GASNETE_THREAD_FARG) 
{
  if (nbytes == 0)
    return GASNET_INVALID_HANDLE;

  gasnete_begin_nbi_accessregion(1 /* enable recursion */ GASNETE_THREAD_PASS);
  gasnete_get_gputogpu_nbi(dest, node, src, nbytes GASNETE_THREAD_PASS);
  return gasnete_end_nbi_accessregion(GASNETE_THREAD_PASS_ALONE);
}

gasnet_handle_t gasnete_get_gputohost_nb(void *dest, gasnet_node_t node, 
                                         void *src, size_t nbytes 
                                         GASNETE_THREAD_FARG) 
{
  if (nbytes == 0)
    return GASNET_INVALID_HANDLE;

  gasnete_begin_nbi_accessregion(1 /* enable recursion */ GASNETE_THREAD_PASS);
  gasnete_get_gputohost_nbi(dest, node, src, nbytes GASNETE_THREAD_PASS);
  return gasnete_end_nbi_accessregion(GASNETE_THREAD_PASS_ALONE);
}

gasnet_handle_t gasnete_put_hosttogpu_nb(gasnet_node_t node, void *dest, 
                                         void *src, size_t nbytes
                                         GASNETE_THREAD_FARG) 
{
  if (nbytes == 0)
    return GASNET_INVALID_HANDLE;

  gasnete_begin_nbi_accessregion(1 GASNETE_THREAD_PASS);
  gasnete_put_hosttogpu_nbi(node, dest, src, nbytes GASNETE_THREAD_PASS);
  return gasnete_end_nbi_accessregion(GASNETE_THREAD_PASS_ALONE);
}

gasnet_handle_t gasnete_put_gputogpu_nb(gasnet_node_t node, void *dest, 
                                        void *src, size_t nbytes
                                        GASNETE_THREAD_FARG) 
{
  if (nbytes == 0)
    return GASNET_INVALID_HANDLE;

  gasnete_begin_nbi_accessregion(1 GASNETE_THREAD_PASS);
  gasnete_put_gputogpu_nbi(node, dest, src, nbytes GASNETE_THREAD_PASS);
  return gasnete_end_nbi_accessregion(GASNETE_THREAD_PASS_ALONE);
}

gasnet_handle_t gasnete_put_gputohost_nb(gasnet_node_t node, void *dest, 
                                         void *src, size_t nbytes
                                         GASNETE_THREAD_FARG) 
{
  if (nbytes == 0)
    return GASNET_INVALID_HANDLE;

  gasnete_begin_nbi_accessregion(1 GASNETE_THREAD_PASS);
  gasnete_put_gputohost_nbi(node, dest, src, nbytes GASNETE_THREAD_PASS);
  return gasnete_end_nbi_accessregion(GASNETE_THREAD_PASS_ALONE);
}

gasnet_handle_t gasnete_memset_togpu_nb(gasnet_node_t node, void *dest, 
                                        int val, size_t nbytes 
                                        GASNETE_THREAD_FARG) 
{
  gasneti_eop_t *eop;

  if (nbytes == 0)
    return GASNET_INVALID_HANDLE;

  eop = gasneti_eop_create(GASNETE_THREAD_PASS_ALONE);
  
  GASNETI_SAFE(SHORT_REQ(4,6,(node, gasneti_handleridx(gasnete_memset_togpu_reqh),
                              (gasnet_handlerarg_t)val, (gasnet_handlerarg_t)nbytes,
                              PACK(dest), PACK(eop))));
  
  return gasneti_eop_to_handle(eop);
}


/* ---------------------------------------------------------------------------- 
 * Per-thread data management for GPU extensions
 * ----------------------------------------------------------------------------
 */
void gasnete_gpu_cleanup_threaddata(void *td) 
{
  gasnete_gpu_threaddata_t *_td = (gasnete_gpu_threaddata_t *)td;
}

gasnete_gpu_threaddata_t *gasnete_gpu_new_threaddata(void) 
{
  gasnete_gpu_threaddata_t *td = gasneti_malloc(sizeof(gasnete_gpu_threaddata_t));

  gasneti_assert(td != NULL);

  /* Initialize the active op queue and the free op queue */
  /*
    td->active_op_queue = gasnete_queue_new();
    td->free_op_queue = gasnete_queue_new();
  */
  td->dev_id = -1;  
  gasnete_register_threadcleanup(gasnete_gpu_cleanup_threaddata, td);

  return td;
}

/**
 * Read the gpu device id for each node from a file.
 * each line of the file should have two integers representing "node_id  device_id".
 * valid node id should be >= 0 and < # of gasnet nodes.
 * valid device id should be >= 0 and < # of GPU on the node.
 * the default device id is "-1" which means the gasnet process is not attached to any gpu.
 *
 * \param fn the name of the gpu device id map file
 * \param dev_id_map is an pre-allocated array of size gasnet_nodes() for storing the gpu device id for each node
 * \return the return value is the number lines that has been read from the file.
 *
 */
int gasnete_gpu_read_deviceid_map(const char *fn, int *dev_id_map)
{
  FILE *dev_id_file;
  int dev_id;
  uint32_t node_id;
  gasnet_node_t i, num_gpus;

  num_gpus = 0;
  
  /* initialize the device id to -1 (no device) for each entry */
  for (i=0; i<gasnet_nodes(); i++)
    dev_id_map[i] = -1; 

  dev_id_file = fopen(fn, "r");
  if (dev_id_file == NULL) {
    gasneti_fatalerror("Failed to open the gpu device id map file %s\n", fn);
  }
  while (!feof(dev_id_file)) {
    fscanf(dev_id_file, "%u %d\n", &node_id, &dev_id);
    gasneti_assert(node_id < gasnet_nodes());
    dev_id_map[node_id] = dev_id;
    num_gpus++;
  }
  fclose(dev_id_file);

#ifdef DEBUG_GPU
  /* print out the device id map*/
  fprintf(stderr, "gasnet nodes %d, gpu devices %d\n", gasnet_nodes(), num_gpus);
  for (i=0;i<gasnet_nodes(); i++) {
    fprintf(stderr, "node %d: gpu dev id %d\n", i, dev_id_map[i]);
  }
#endif

  return num_gpus;
}


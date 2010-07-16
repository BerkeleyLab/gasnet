/* $Source: /Users/kamil/work/gasnet-cvs2/gasnet/extended-ref/Attic/gasnet_gpu_internal.h,v $
 * $Date: 2010/07/16 18:35:42 $
 * $Revision: 1.1.4.2 $
 *
 * Description: Internal API for GASNet GPU extensions
 * 
 * Yili Zheng
 * LBNL 2010
 */

#ifndef GASNET_GPU_INTERNAL_H_
#define GASNET_GPU_INTERNAL_H_

/* ------------------------------------------------------------------------------------ 
 * Data structures
 * ------------------------------------------------------------------------------------ 
 */
/* per-thread state for GPU */
typedef struct {
  int dev_id; /**< device id of the GPU associated with the current thread*/
  // int progressfn_active; /**< indicate if it's currently in the proofress fn */
  // gasnete_queue_t *active_op_queue; /**< queue of active ops */
  // gasnete_queue_t *free_op_queue; /**<  queue of free ops */
} gasnete_gpu_threaddata_t;

extern gasnete_gpu_threaddata_t *gasnete_gpu_new_threaddata(void);

extern void gasnete_gpu_cleanup_threaddata(void *td);

/* gasnete_threaddata_t might not be defined yet, but GPU ptr must be
 * 4th which is defined in extended-ref/gasnet_extended_help.h.
 */
#define GASNETE_GPU_MYTHREAD (((void **)GASNETE_MYTHREAD)[3]  \
                              ? ((void **)GASNETE_MYTHREAD)[3] \
                              : (((void **)GASNETE_MYTHREAD)[3] = gasnete_gpu_new_threaddata()))


typedef enum _gasnete_gpu_op_kind_t {
  GASNETE_PUT_HOST2HOST,
  GASNETE_PUT_HOST2GPU,
  GASNETE_PUT_GPU2HOST,
  GASNETE_PUT_GPU2GPU,
  GASNETE_GET_HOST2HOST,
  GASNETE_GET_HOST2GPU,
  GASNETE_GET_GPU2HOST,
  GASNETE_GET_GPU2GPU
} gasnete_gpu_op_kind_t;

typedef struct _gasnete_gpu_op_t {

  gasnete_gpu_op_kind_t type;

  gasnet_node_t srcNode;
  /* int src_dev_id; */ /* will need this field if there are multiple GPUs per GASNet node */
  void *srcPtr;  
  void *srcBuf; /**< host buffer for the source if the source is on gpu */
  
  gasnet_node_t dstNode;
  /* int dst_dev_id; */ /* will need this field if there are multiple GPUs per GASNet node */
  void *dstPtr; 
  void *dstBuf; /**< host buffer for the destination if the destination is on gpu */
  
  size_t nbytes; /**< size of data in bytes */
  int stream_id; /**<GPU execution stream id */

  int ready; /**< flag to mark the operation ready */
  int done; /**< flag to mark the operation done */

  void (*cb_func)(void *cb_data); /* callback function */
  void *cb_data; /* client data for the callback function */

} gasnete_gpu_op_t;

static inline gasnete_gpu_op_t *gasnete_gpu_op_new(gasnete_gpu_op_kind_t type,
                                                   gasnet_node_t srcNode,
                                                   void *srcPtr,
                                                   void *srcBuf,
                                                   gasnet_node_t dstNode,
                                                   void *dstPtr,
                                                   void *dstBuf,
                                                   size_t nbytes,
                                                   int stream_id,
                                                   void (*cb_func)(void *cb_data),
                                                   void *cb_data)
{
  gasnete_gpu_op_t *new_op;

  /* We can also use a freelist. */
  new_op = (gasnete_gpu_op_t *)gasneti_malloc(sizeof(gasnete_gpu_op_t));
  gasneti_assert(new_op != NULL);
 
  new_op->type = type;
  new_op->srcNode = srcNode;
  new_op->srcPtr = srcPtr;
  new_op->srcBuf = srcBuf;
  new_op->dstNode = dstNode;
  new_op->dstPtr = dstPtr;
  new_op->dstBuf = dstBuf;
  new_op->nbytes = nbytes;
  new_op->stream_id = stream_id;
  new_op->cb_func = cb_func;
  new_op->cb_data = cb_data;

  new_op->ready = 0;
  new_op->done = 0;

  return new_op;
}

static inline void gasnete_gpu_op_print(FILE *fp, gasnete_gpu_op_t *gpu_op)
{
  gasneti_assert(fp != NULL);
  gasneti_assert(gpu_op != NULL);

  fprintf(fp, "gpu_op %p, type %u, srcNode %u, srcPtr %p, srcBuf %p, dstNode %u, dstPtr %p, dstBuf %p, nbytes %lu, stream_id %d, ready %d, done %d, cb_func %p, cb_data %p\n",  
          gpu_op, gpu_op->type, gpu_op->srcNode, gpu_op->srcPtr, gpu_op->srcBuf,
          gpu_op->dstNode, gpu_op->dstPtr, gpu_op->dstBuf, gpu_op->nbytes, 
          gpu_op->stream_id, gpu_op->ready, gpu_op->done, gpu_op->cb_func, 
          gpu_op->cb_data);
}

static inline void gasnete_gpu_op_free(gasnete_gpu_op_t *gpu_op)
{
  gasneti_assert(gpu_op != NULL);
  gasneti_assert(gpu_op->done); /* It would be odd to free an op before it's done. */
  gasneti_free(gpu_op);
}                                                

#endif /* GASNET_GPU_INTERNAL_H_ */

/* $Source: /Users/kamil/work/gasnet-cvs2/gasnet/extended-ref/Attic/gasnet_gpu_cuda.cu,v $
 * $Date: 2010/07/15 20:25:19 $
 * $Revision: 1.1.2.1 $
 *
 * Description: Implements the interface between GASNet and NVIDIA CUDA
 *
 * Yili Zheng
 * LBNL 2010
 */

#include <cuda.h>
#include <stdio.h>
#include <assert.h>

// #include <cutil_inline.h> /* in gasnet/other/cuda_sdk */

#include <gasnet_gpu_cuda.h>

static enum cudaMemcpyKind cudaMemcpyKindMap[gpuMemcpyKind_num] = 
  {cudaMemcpyHostToHost, 
   cudaMemcpyHostToDevice, 
   cudaMemcpyDeviceToHost, 
   cudaMemcpyDeviceToDevice};

cudaStream_t gasnet_gpu_streams[MAX_GPU_STREAM_NUM];

void _gasnete_gpu_stream_init()
{
  int i;
  for (i=0; i<MAX_GPU_STREAM_NUM; i++)
    cudaStreamCreate(&gasnet_gpu_streams[i]);
}

void _gasnete_gpu_stream_fini()
{
  int i;
  for (i=0; i<MAX_GPU_STREAM_NUM; i++)
    cudaStreamDestroy(gasnet_gpu_streams[i]);
}

void _gasnete_gpu_stream_sync(int stream_id)
{
  cudaError_t rv;
  assert(stream_id >=0 && stream_id <MAX_GPU_STREAM_NUM);
  rv = cudaStreamSynchronize(gasnet_gpu_streams[stream_id]);
  assert(rv == cudaSuccess);
}

int _gasnete_gpu_get_device_count(void)
{
  enum cudaError rv;

  int num_gpus;
  rv = cudaGetDeviceCount(&num_gpus);
  assert(rv == cudaSuccess);
  return num_gpus;
}


int _gasnete_gpu_attach(int gpuid)
{
  enum cudaError rv;

  cudaDeviceProp deviceProp;
  rv = cudaSetDevice(gpuid);
  assert(rv == cudaSuccess);
  cudaGetDeviceProperties(&deviceProp, gpuid);
  printf("GPU %d (%s) is attached.\n", gpuid, deviceProp.name);

  _gasnete_gpu_stream_init();

  return 0;
}


void * _gasnete_gpu_device_alloc(size_t nbytes)
{
  enum cudaError rv;
  void *devPtr;
  
  rv = cudaMalloc(&devPtr, nbytes);
  assert(rv == cudaSuccess);

  return devPtr;
}


void _gasnete_gpu_device_free(void *devPtr)
{
  cudaFree(devPtr);
}


void * _gasnete_gpu_host_alloc(size_t nbytes)
{
  void *hostPtr;
  
  cudaHostAlloc(&hostPtr, nbytes, cudaHostAllocDefault);

  return hostPtr;
}


void _gasnete_gpu_host_free(void *hostPtr)
{
  cudaFreeHost(hostPtr);
}


void _gasnete_gpu_store(void *devDst, void *hostSrc, size_t nbytes)
{
  enum cudaError rv;
  /* need to add error checking to make sure that the current
     process/thread has already attached to a GPU. */
  rv = cudaMemcpy(devDst, hostSrc, nbytes, cudaMemcpyHostToDevice);
  assert(rv == cudaSuccess);
}


void _gasnete_gpu_store_async(void *devDst, void *hostSrc, size_t nbytes, int stream_id)
{
  enum cudaError rv;
  assert(stream_id >= 0 && stream_id < MAX_GPU_STREAM_NUM);
  rv = cudaMemcpyAsync(devDst, hostSrc, nbytes, cudaMemcpyHostToDevice, 
                       gasnet_gpu_streams[stream_id]);
  assert(rv == cudaSuccess);
}


void _gasnete_gpu_load(void *hostDst, void *devSrc, size_t nbytes)
{
  enum cudaError rv;
    
  /* need to add error checking to make sure that the current
     process/thread has already attached to a GPU. */
  rv = cudaMemcpy(hostDst, devSrc, nbytes, cudaMemcpyDeviceToHost);
  assert(rv == cudaSuccess);
}


void _gasnete_gpu_load_async(void *hostDst, void *devSrc, size_t nbytes, int stream_id)
{
  enum cudaError rv;
    
  assert(stream_id >= 0 && stream_id < MAX_GPU_STREAM_NUM);

  rv = cudaMemcpyAsync(hostDst, devSrc, nbytes, cudaMemcpyDeviceToHost, 
                  gasnet_gpu_streams[stream_id]);
  assert(rv == cudaSuccess);
}


void _gasnete_gpu_memcpy(void *dst, void *src, size_t nbytes, gpuMemcpyKind_t kind)
{ 
  enum cudaError rv;
    
  /* need to add error checking to make sure that the current
     process/thread has already attached to a GPU. */
  assert(kind >= 0 && kind < gpuMemcpyKind_num);
  rv = cudaMemcpy(dst, src, nbytes, cudaMemcpyKindMap[kind]);
  assert(rv == cudaSuccess);  
}


void _gasnete_gpu_memset(void *devPtr, int val, size_t nbytes)
{
  enum cudaError rv;
  rv = cudaMemset(devPtr, val, nbytes);
  assert(rv == cudaSuccess);
}

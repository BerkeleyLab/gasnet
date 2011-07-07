/* $Source: /Users/kamil/work/gasnet-cvs2/gasnet/extended-ref/Attic/gasnet_gpu_cuda.h,v $
 * $Date: 2011/07/07 23:20:31 $
 * $Revision: 1.1.4.3 $
 *
 * Description: Interface between GASNet runtime and NVIDIA CUDA
 *
 * Yili Zheng
 * LBNL 2010
 */

#ifndef GASNET_GPU_CUDA_H_
#define GASNET_GPU_CUDA_H_

#include <gasnet_gpu_memcpy.h> /* for gpuMemcpyKind_t */

/* Note: Because the CUDA compiler nvcc compiles .cu files as C++
   files, so it's important to declare the function prototypes in C.
   Otherwise, the linker would complain it cannot find some missing C
   functions because those functions are compiled as C++ functions
   whose names have been mingled!
*/
#ifdef __cplusplus
extern "C" {
#endif

  int _gasnete_gpu_get_device_count(void);

  int _gasnete_gpu_attach(int gpuid);

  void * _gasnete_gpu_host_alloc(size_t nbytes);
 
  void * _gasnete_gpu_device_alloc(size_t nbytes);

  void _gasnete_gpu_device_free(void *devPtr);

  //void cudaHostRegister();
  void _gasnete_cudaHostRegister();

  /* put data from local host memory to attached GPU memory */
  void _gasnete_gpu_store(void *devDst, void *hostSrc, size_t nbytes);

  void _gasnete_gpu_store_async(void *devDst, void *hostSrc, size_t nbytes, int stream_id);

  /* get data from attached GPU memory to local host memory */
  void _gasnete_gpu_load(void *hostDst, void *devSrc, size_t nbytes);

  void _gasnete_gpu_load_async(void *hostDst, void *devSrc, size_t nbytes, int stream_id);
  
  void _gasnete_gpu_memcpy(void *dst, void *src, size_t nbytes, gpuMemcpyKind_t kind);

  void _gasnete_gpu_memset(void *dest, int val, size_t nbytes);

  void _gasnete_gpu_stream_sync(int stream_id);

#ifdef __cplusplus
}
#endif

#define MAX_GPU_STREAM_NUM 4

#endif /* GASNET_GPU_CUDA_H_ */

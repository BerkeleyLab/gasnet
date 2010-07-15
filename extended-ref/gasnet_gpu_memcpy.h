/* $Source: /Users/kamil/work/gasnet-cvs2/gasnet/extended-ref/Attic/gasnet_gpu_memcpy.h,v $
 * $Date: 2010/07/15 20:25:19 $
 * $Revision: 1.1.2.1 $
 *
 * Yili Zheng
 * LBNL 2010
 */

#ifndef GASNET_GPU_MEMCPY_H_
#define GASNET_GPU_MEMCPY_H_

typedef enum _gpuMemcpyKint_t {
  gpuMemcpyHostToHost,
  gpuMemcpyHostToDevice,
  gpuMemcpyDeviceToHost,
  gpuMemcpyDeviceToDevice,
  gpuMemcpyKind_num
} gpuMemcpyKind_t;

void gasnete_gpu_memcpy(void *dst, void *src, size_t nbytes, gpuMemcpyKind_t kind);

#endif /* GASNET_GPU_MEMCPY_H_ */

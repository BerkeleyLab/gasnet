/* $Source: /Users/kamil/work/gasnet-cvs2/gasnet/extended-ref/Attic/gasnet_extended_gpu.h,v $
 * $Date: 2011/04/21 19:07:27 $
 * $Revision: 1.1.4.3 $
 *
 * Description: External API for GASNet GPU extensions
 * 
 * Yili Zheng
 * LBNL 2010
 */

#ifndef GASNET_EXTENDED_GPU_H_
#define GASNET_EXTENDED_GPU_H_

#include <gasnet_gpu_memcpy.h>

/* ------------------------------------------------------------------------------------ 
 * Public interface of GPU device-specific routines in gasnet_gpu_cuda.h, which should
 * not be included outside of GASNet because it may contain non-portable code.
 * ------------------------------------------------------------------------------------ 
 */
/* ------------------------------------------------------------------------------------ 
 * Initialization
 * ------------------------------------------------------------------------------------ 
 */
#define gasnet_gpu_attach gasnete_gpu_attach
extern int gasnet_gpu_attach(int dev_id);

#define gasnet_gpu_get_device_count gasnete_gpu_get_device_count
extern int gasnet_gpu_get_device_count(void);

#define gasnet_gpu_get_device_id gasnete_gpu_get_device_id
extern int gasnet_gpu_get_device_id(void);

#define gasnet_gpu_read_deviceid_map gasnete_gpu_read_deviceid_map
extern int gasnet_gpu_read_deviceid_map(const char *fn, int *dev_id_map);

#define gasnet_gpu_device_alloc gasnete_gpu_device_alloc
extern void * gasnet_gpu_device_alloc(size_t nbytes);

#define gasnet_gpu_device_free gasnete_gpu_device_free
extern void gasnet_gpu_device_free(void *devPtr);

/* put data from local host memory to attached GPU memory */
#define gasnet_gpu_store gasnete_gpu_store
extern void gasnet_gpu_store(void *devDst, void *hostSrc, size_t nbytes);

/* get data from attached GPU memory to local host memory */ 
#define gasnet_gpu_load gasnete_gpu_load
extern void gasnet_gpu_load(void *hostDst, void *devSrc, size_t nbytes);  

#define gasnet_gpu_memset gasnete_gpu_memset
extern void gasnet_gpu_memset(void *dest, int val, size_t nbytes);

/* ------------------------------------------------------------------------------------ 
 * Non-blocking memory-to-memory transfers (implicit handle)
 * ------------------------------------------------------------------------------------ 
 */
/* get from host to gpu */
extern void gasnete_get_hosttogpu_nbi(void *dest, gasnet_node_t node, void *src, size_t nbytes GASNETE_THREAD_FARG);

GASNETT_INLINE(gasnet_get_hosttogpu_nbi)
void gasnet_get_hosttogpu_nbi(void *dest, gasnet_node_t node, void *src, size_t nbytes)
{
  gasnete_get_hosttogpu_nbi(dest, node, src, nbytes GASNETE_THREAD_GET);
}

/* get from gpu to gpu */
extern void gasnete_get_gputogpu_nbi(void *dest, gasnet_node_t node, void *src, size_t nbytes GASNETE_THREAD_FARG);

GASNETT_INLINE(gasnet_get_gputogpu_nbi)
void gasnet_get_gputogpu_nbi(void *dest, gasnet_node_t node, void *src, size_t nbytes)
{
  gasnete_get_gputogpu_nbi(dest, node, src, nbytes GASNETE_THREAD_GET);
}

/* get from gpu to host */
extern void gasnete_get_gputohost_nbi(void *dest, gasnet_node_t node, void *src, size_t nbytes GASNETE_THREAD_FARG);

GASNETT_INLINE(gasnet_get_gputohost_nbi)
void gasnet_get_gputohost_nbi(void *dest, gasnet_node_t node, void *src, size_t nbytes)
{
  gasnete_get_gputohost_nbi(dest, node, src, nbytes GASNETE_THREAD_GET);
}

/* put from host to gpu */
extern void gasnete_put_hosttogpu_nbi(gasnet_node_t node, void *dest, void *src, size_t nbytes GASNETE_THREAD_FARG);

GASNETT_INLINE(gasnet_put_hosttogpu_nbi)
void gasnet_put_hosttogpu_nbi(gasnet_node_t node, void *dest, void *src, size_t nbytes)
{
  gasnete_put_hosttogpu_nbi(node, dest, src, nbytes GASNETE_THREAD_GET);
}

/* put from gpu to gpu */
extern void gasnete_put_gputogpu_nbi(gasnet_node_t node, void *dest, void *src, size_t nbytes GASNETE_THREAD_FARG);

GASNETT_INLINE(gasnet_put_gputogpu_nbi)
void gasnet_put_gputogpu_nbi(gasnet_node_t node, void *dest, void *src, size_t nbytes)
{
  gasnete_put_gputogpu_nbi(node, dest, src, nbytes GASNETE_THREAD_GET);
}

/* put from gpu to host */
extern void gasnete_put_gputohost_nbi(gasnet_node_t node, void *dest, void *src, size_t nbytes GASNETE_THREAD_FARG);

GASNETT_INLINE(gasnet_put_gputohost_nbi)
void gasnet_put_gputohost_nbi(gasnet_node_t node, void *dest, void *src, size_t nbytes)
{
  gasnete_put_gputohost_nbi(node, dest, src, nbytes GASNETE_THREAD_GET);
}

/* set gpu memory to val */
extern void gasnete_memset_togpu_nbi(gasnet_node_t node, void *dest, int val, size_t nbytes GASNETE_THREAD_FARG); 

GASNETT_INLINE(gasnet_memset_togpu_nbi)
void gasnet_memset_togpu_nbi(gasnet_node_t node, void *dest, int val, size_t nbytes)
{
  gasnete_memset_togpu_nbi(node, dest, val, nbytes GASNETE_THREAD_GET);
}

/* ------------------------------------------------------------------------------------ 
 * Non-blocking memory-to-memory transfers (explicit handle)
 * ------------------------------------------------------------------------------------ 
 */
/* get from host to gpu */
GASNETT_INLINE(gasnete_get_hosttogpu_nb)
gasnet_handle_t gasnete_get_hosttogpu_nb(void *dest, gasnet_node_t node, void *src, size_t nbytes GASNETE_THREAD_FARG)
{
  if (nbytes == 0)
    return GASNET_INVALID_HANDLE;
  
  gasnete_begin_nbi_accessregion(1 /* enable recursion */ GASNETE_THREAD_PASS);
  gasnete_get_hosttogpu_nbi(dest, node, src, nbytes GASNETE_THREAD_PASS);
  return gasnete_end_nbi_accessregion(GASNETE_THREAD_PASS_ALONE);
}

GASNETT_INLINE(gasnet_get_hosttogpu_nb)
gasnet_handle_t gasnet_get_hosttogpu_nb(void *dest, gasnet_node_t node, void *src, size_t nbytes)
{
  return gasnete_get_hosttogpu_nb(dest, node, src, nbytes GASNETE_THREAD_GET);
}


/* get from gpu to gpu */
GASNETT_INLINE(gasnete_get_gputogpu_nb)
gasnet_handle_t gasnete_get_gputogpu_nb(void *dest, gasnet_node_t node, void *src, size_t nbytes GASNETE_THREAD_FARG)
{
  if (nbytes == 0)
    return GASNET_INVALID_HANDLE;
  
  gasnete_begin_nbi_accessregion(1 /* enable recursion */ GASNETE_THREAD_PASS);
  gasnete_get_gputogpu_nbi(dest, node, src, nbytes GASNETE_THREAD_PASS);
  return gasnete_end_nbi_accessregion(GASNETE_THREAD_PASS_ALONE);
}

GASNETT_INLINE(gasnet_get_gputogpu_nb)
gasnet_handle_t gasnet_get_gputogpu_nb(void *dest, gasnet_node_t node, void *src, size_t nbytes)
{
  return gasnete_get_gputogpu_nb(dest, node, src, nbytes GASNETE_THREAD_GET);
}


/* get from gpu to host */
GASNETT_INLINE(gasnete_get_gputohost_nb)
gasnet_handle_t gasnete_get_gputohost_nb(void *dest, gasnet_node_t node, void *src, size_t nbytes GASNETE_THREAD_FARG)
{
  if (nbytes == 0)
    return GASNET_INVALID_HANDLE;
  
  gasnete_begin_nbi_accessregion(1 /* enable recursion */ GASNETE_THREAD_PASS);
  gasnete_get_gputohost_nbi(dest, node, src, nbytes GASNETE_THREAD_PASS);
  return gasnete_end_nbi_accessregion(GASNETE_THREAD_PASS_ALONE);
}

GASNETT_INLINE(gasnet_get_gputohost_nb)
gasnet_handle_t gasnet_get_gputohost_nb(void *dest, gasnet_node_t node, void *src, size_t nbytes)
{
  return gasnete_get_gputohost_nb(dest, node, src, nbytes GASNETE_THREAD_GET);
}

/* put from host to gpu */
GASNETT_INLINE(gasnete_put_hosttogpu_nb)
gasnet_handle_t gasnete_put_hosttogpu_nb(gasnet_node_t node, void *dest, void *src, size_t nbytes GASNETE_THREAD_FARG)
{
  if (nbytes == 0)
    return GASNET_INVALID_HANDLE;

  gasnete_begin_nbi_accessregion(1 GASNETE_THREAD_PASS);
  gasnete_put_hosttogpu_nbi(node, dest, src, nbytes GASNETE_THREAD_PASS);
  return gasnete_end_nbi_accessregion(GASNETE_THREAD_PASS_ALONE);
}

GASNETT_INLINE(gasnet_put_hosttogpu_nb)
gasnet_handle_t gasnet_put_hosttogpu_nb(gasnet_node_t node, void *dest, void *src, size_t nbytes)
{
  return gasnete_put_hosttogpu_nb(node, dest, src, nbytes GASNETE_THREAD_GET);
}

/* put from gpu to gpu */
GASNETT_INLINE(gasnete_put_gputogpu_nb)
gasnet_handle_t gasnete_put_gputogpu_nb(gasnet_node_t node, void *dest, void *src, size_t nbytes GASNETE_THREAD_FARG)
{
  if (nbytes == 0)
    return GASNET_INVALID_HANDLE;

  gasnete_begin_nbi_accessregion(1 GASNETE_THREAD_PASS);
  gasnete_put_gputogpu_nbi(node, dest, src, nbytes GASNETE_THREAD_PASS);
  return gasnete_end_nbi_accessregion(GASNETE_THREAD_PASS_ALONE);
}

GASNETT_INLINE(gasnet_put_gputogpu_nb)
gasnet_handle_t gasnet_put_gputogpu_nb(gasnet_node_t node, void *dest, void *src, size_t nbytes)
{
  return gasnete_put_gputogpu_nb(node, dest, src, nbytes GASNETE_THREAD_GET);
}


/* put from gpu to host */
GASNETT_INLINE(gasnete_put_gputohost_nb)
gasnet_handle_t gasnete_put_gputohost_nb(gasnet_node_t node, void *dest, void *src, size_t nbytes GASNETE_THREAD_FARG)
{
  if (nbytes == 0)
    return GASNET_INVALID_HANDLE;

  gasnete_begin_nbi_accessregion(1 GASNETE_THREAD_PASS);
  gasnete_put_gputohost_nbi(node, dest, src, nbytes GASNETE_THREAD_PASS);
  return gasnete_end_nbi_accessregion(GASNETE_THREAD_PASS_ALONE);
}

GASNETT_INLINE(gasnet_put_gputohost_nb)
gasnet_handle_t gasnet_put_gputohost_nb(gasnet_node_t node, void *dest, void *src, size_t nbytes)
{
  return gasnete_put_gputohost_nb(node, dest, src, nbytes GASNETE_THREAD_GET);
}


/* set gpu memory to val */
extern gasnet_handle_t gasnete_memset_togpu_nb(gasnet_node_t node, void *dest, int val, size_t nbytes GASNETE_THREAD_FARG);

GASNETT_INLINE(gasnet_memset_togpu_nb)
gasnet_handle_t gasnet_memset_togpu_nb(gasnet_node_t node, void *dest, int val, size_t nbytes)
{
  return gasnete_memset_togpu_nb(node, dest, val, nbytes GASNETE_THREAD_GET);
}

/* ------------------------------------------------------------------------------------ 
 * Blocking memory-to-memory transfers (to be added as wrappers of the non-blocking 
 * version
 * ------------------------------------------------------------------------------------ 
 */
/* get from host to gpu */
GASNETT_INLINE(gasnet_get_hosttogpu)
void gasnet_get_hosttogpu(void *dest, gasnet_node_t node, void *src, size_t nbytes)
{
  gasnet_handle_t h;
  h = gasnete_get_hosttogpu_nb(dest, node, src, nbytes GASNETE_THREAD_GET);
  gasnet_wait_syncnb(h);
}

/* get from gpu to gpu */
GASNETT_INLINE(gasnet_get_gputogpu)
void gasnet_get_gputogpu(void *dest, gasnet_node_t node, void *src, size_t nbytes)
{
  gasnet_handle_t h;
  h = gasnete_get_gputogpu_nb(dest, node, src, nbytes GASNETE_THREAD_GET);
  gasnet_wait_syncnb(h);
}

/* get from gpu to host */
GASNETT_INLINE(gasnet_get_gputohost)
void gasnet_get_gputohost(void *dest, gasnet_node_t node, void *src, size_t nbytes)
{
  gasnet_handle_t h;
  h = gasnete_get_gputohost_nb(dest, node, src, nbytes GASNETE_THREAD_GET);
  gasnet_wait_syncnb(h);
}

/* put from host to gpu */
GASNETT_INLINE(gasnet_put_hosttogpu)
void gasnet_put_hosttogpu(gasnet_node_t node, void *dest, void *src, size_t nbytes)
{
  gasnet_handle_t h;
  h = gasnete_put_hosttogpu_nb(node, dest, src, nbytes GASNETE_THREAD_GET);
  gasnet_wait_syncnb(h);
}

/* put from gpu to gpu */
GASNETT_INLINE(gasnet_put_gputogpu)
void gasnet_put_gputogpu(gasnet_node_t node, void *dest, void *src, size_t nbytes)
{
  gasnet_handle_t h;
  h = gasnete_put_gputogpu_nb(node, dest, src, nbytes GASNETE_THREAD_GET);
  gasnet_wait_syncnb(h);
}

/* put from gpu to host */
GASNETT_INLINE(gasnet_put_gputohost)
void gasnet_put_gputohost(gasnet_node_t node, void *dest, void *src, size_t nbytes)
{
  gasnet_handle_t h;
  h = gasnete_put_gputohost_nb(node, dest, src, nbytes GASNETE_THREAD_GET);
  gasnet_wait_syncnb(h);
}

/* set gpu memory to val */
GASNETT_INLINE(gasnet_memset_togpu)
void gasnet_memset_togpu(gasnet_node_t node, void *dest, int val, size_t nbytes)
{
  gasnet_handle_t h;
  h = gasnete_memset_togpu_nb(node, dest, val, nbytes GASNETE_THREAD_GET);
  gasnet_wait_syncnb(h);
}

#endif /* GASNET_EXTENDED_GPU_H_ */


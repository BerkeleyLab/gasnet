/* $Source: /Users/kamil/work/gasnet-cvs2/gasnet/extended-ref/Attic/gasnet_extended_gpu.h,v $
 * $Date: 2010/07/15 20:25:19 $
 * $Revision: 1.1.2.1 $
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
extern int gasnete_gpu_attach(int dev_id);

extern int gasnete_gpu_get_device_count(void);

extern int gasnete_gpu_get_device_id(void);

extern int gasnete_gpu_read_deviceid_map(const char *fn, int *dev_id_map);
 
extern void * gasnete_gpu_device_alloc(size_t nbytes);

extern void gasnete_gpu_device_free(void *devPtr);

/* put data from local host memory to attached GPU memory */
extern void gasnete_gpu_store(void *devDst, void *hostSrc, size_t nbytes);

/* get data from attached GPU memory to local host memory */
extern void gasnete_gpu_load(void *hostDst, void *devSrc, size_t nbytes);

extern void gasnete_gpu_memset(void *dest, int val, size_t nbytes);

/* ------------------------------------------------------------------------------------ 
 * Non-blocking memory-to-memory transfers (implicit handle)
 * ------------------------------------------------------------------------------------ 
 */
/* get from host to gpu */
extern void gasnete_get_hosttogpu_nbi(void *dest, gasnet_node_t node, void *src, 
                                      size_t nbytes GASNETE_THREAD_FARG);
/* get from gpu to gpu */
extern void gasnete_get_gputogpu_nbi(void *dest, gasnet_node_t node, void *src, 
                                     size_t nbytes GASNETE_THREAD_FARG);

/* get from gpu to host */
extern void gasnete_get_gputohost_nbi(void *dest, gasnet_node_t node, void *src,
                                      size_t nbytes GASNETE_THREAD_FARG);

/* put from host to gpu */
extern void gasnete_put_hosttogpu_nbi(gasnet_node_t node, void *dest, void *src, 
                                      size_t nbytes GASNETE_THREAD_FARG);

/* put from gpu to gpu */
extern void gasnete_put_gputogpu_nbi(gasnet_node_t node, void *dest, void *src, 
                                     size_t nbytes GASNETE_THREAD_FARG);

/* put from gpu to host */
void gasnete_put_gputohost_nbi(gasnet_node_t node, void *dest, void *src, 
                               size_t nbytes GASNETE_THREAD_FARG);

/* set gpu memory to val */
extern void gasnete_memset_togpu_nbi(gasnet_node_t node, void *dest, int val, 
                                     size_t nbytes GASNETE_THREAD_FARG); 


/* ------------------------------------------------------------------------------------ 
 * Non-blocking memory-to-memory transfers (explicit handle)
 * ------------------------------------------------------------------------------------ 
 */
/* get from host to gpu */
extern gasnet_handle_t gasnete_get_hosttogpu_nb(void *dest, gasnet_node_t node, 
                                                void *src, size_t nbytes 
                                                GASNETE_THREAD_FARG);

/* get from gpu to gpu */
extern gasnet_handle_t gasnete_get_gputogpu_nb(void *dest, gasnet_node_t node, 
                                               void *src, size_t nbytes 
                                               GASNETE_THREAD_FARG);

/* get from gpu to host */
extern gasnet_handle_t gasnete_get_gputohost_nb(void *dest, gasnet_node_t node, 
                                                void *src, size_t nbytes 
                                                GASNETE_THREAD_FARG);

/* put from host to gpu */
extern gasnet_handle_t gasnete_put_hosttogpu_nb(gasnet_node_t node, void *dest, 
                                                void *src, size_t nbytes
                                                GASNETE_THREAD_FARG);

/* put from gpu to gpu */
extern gasnet_handle_t gasnete_put_gputogpu_nb(gasnet_node_t node, void *dest, 
                                               void *src, size_t nbytes 
                                               GASNETE_THREAD_FARG);

/* put from gpu to host */
extern gasnet_handle_t gasnete_put_gputohost_nb(gasnet_node_t node, void *dest, 
                                                void *src, size_t nbytes
                                                GASNETE_THREAD_FARG) ;

/* set gpu memory to val */
extern gasnet_handle_t gasnete_memset_togpu_nb(gasnet_node_t node, void *dest, 
                                               int val, size_t nbytes 
                                               GASNETE_THREAD_FARG);

/* ------------------------------------------------------------------------------------ 
 * Blocking memory-to-memory transfers (to be added as wrappers of the non-blocking 
 * version
 * ------------------------------------------------------------------------------------ 
 */


#endif /* GASNET_EXTENDED_GPU_H_ */


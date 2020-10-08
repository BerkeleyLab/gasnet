/*   $Source: bitbucket.org:berkeleylab/gasnet.git/other/kinds/gasnet_cuda_uva.c $
 * Description: GASNet Memory Kinds Implementation for CUDA UVA devices
 * Copyright (c) 2020, The Regents of the University of California
 * Terms of use are as specified in license.txt
 */

#define GASNETI_NEED_GASNET_MK_H 1
#include <gasnet_internal.h>
#include <gasnet_kinds_internal.h>

#if GASNET_HAVE_MK_CLASS_CUDA_UVA // Else empty

#include <cuda.h>
#include <cuda_runtime_api.h>

//
// Class-specific MK type and functions
//

typedef struct my_MK_s {
  GASNETI_MK_COMMON // Class-indep prefix

  CUcontext       ctx;
  CUdevice        dev;
} *my_MK_t;

//
// Error checking/reporting wrapper
//
#define gasneti_check_cudacall_always(op) do {              \
    CUresult _retval = (op);                                \
    if_pf (_retval) {                                       \
      const char *_errorname;                               \
      if (cuGetErrorName(_retval, &_errorname)) _errorname = "UNKNOWN"; \
      gasneti_fatalerror("%s returned %s(%i)",#op,_errorname,_retval);\
    }                                                       \
  } while (0)
#if GASNET_DEBUG
  #define gasneti_check_cudacall(op)  gasneti_check_cudacall_always(op)
#else
  #define gasneti_check_cudacall(op)  do { op; } while(0)
#endif

static void gasneti_MK_Destroy_cuda_uva(
            gasneti_MK_t                     i_mk,
            gex_Flags_t                      flags)
{
  my_MK_t mk = (my_MK_t) i_mk;
  gasneti_check_cudacall_always(cuCtxSetCurrent(NULL));
  gasneti_check_cudacall_always(cuDevicePrimaryCtxRelease(mk->dev));
  gasneti_free_mk(i_mk);
}

//
// Class-specific "impl(ementation)": constants and function pointers.
//
// Due to lack of designated initializers in GASNet's required C99 subset, we
// address the fragility as the structure grows or changes by lazy explicit
// initialization.
static gasneti_mk_impl_t *get_impl(void) {
  // Static storage duration ensures these are zero-initialized
  static gasneti_mk_impl_t the_impl;
  static gasneti_mk_impl_t *result;

  if (!result) {
    static gasneti_mutex_t lock = GASNETI_MUTEX_INITIALIZER;
    gasneti_mutex_lock(&lock);
    if (!result) {
      the_impl.mk_class     = GEX_MK_CLASS_CUDA_UVA;
      the_impl.mk_name      = "CUDA_UVA";
      the_impl.mk_sizeof    = sizeof(struct my_MK_s);

      the_impl.mk_destroy   = &gasneti_MK_Destroy_cuda_uva;

      gasneti_sync_writes();
      result = &the_impl;
    }
    gasneti_mutex_unlock(&lock);
  }

  gasneti_assert(result);
  return result;
}

// Class-specific create
int gasneti_MK_Create_cuda_uva(
            gasneti_MK_t                     *i_memkind_p,
            gasneti_Client_t                 client,
            const gex_MK_Create_args_t       *args,
            gex_Flags_t                      flags)
{
  CUdevice dev = args->gex_args.gex_class_cuda_uva.gex_CUdevice;
  GASNETI_TRACE_PRINTF(O,("gex_MK_Create: class=CUDA_UVA gex_CUdevice=%d", dev));

  if (dev < 0) {
    // This is always treated as programmer error
    gasneti_fatalerror("gex_MK_Create called with negative CUdevice=%i", dev);
  }

  // Obtain the primary context for the given device, initializing if needed
  CUcontext ctx;
  CUresult res = cuDevicePrimaryCtxRetain(&ctx, dev);
  if (res == CUDA_ERROR_NOT_INITIALIZED) {
    int initRes = cuInit(0);
    if (initRes == CUDA_SUCCESS) {
      res = cuDevicePrimaryCtxRetain(&ctx, dev);
    } else if (initRes == CUDA_ERROR_NO_DEVICE) {
      GASNETI_RETURN_ERRR(BAD_ARG,"GEX_MK_CLASS_CUDA_UVA: no CUDA devices found");
    } else {
      const char *errorname;
      if (cuGetErrorName(initRes, &errorname)) errorname = "UNKNOWN";
      const char *msg = gasneti_dynsprintf("GEX_MK_CLASS_CUDA_UVA: cuInit() returned %s(%i)", errorname, initRes);
      GASNETI_RETURN_ERRR(BAD_ARG,msg);
    }
  }

  // Failed to obtain the primary context, try to reason out why
  // TODO: explicit diagnosis of more failure cases
  if_pf (res != CUDA_SUCCESS) {
    const char *why = "unknown failure";
    if (res == CUDA_ERROR_INVALID_DEVICE) {
      int dev_count;
      if (cuDeviceGetCount(&dev_count)) {
        why = "cuDeviceGetCount() failed";
      } else if (! dev_count) {
        why = "no CUDA devices found";
      } else {
        why = gasneti_dynsprintf("invalid CUdevice=%i (%d devices found)", dev, dev_count);
      }
    } else {
      const char *errorname;
      if (cuGetErrorName(res, &errorname)) errorname = "UNKNOWN";
      why = gasneti_dynsprintf("cuDevicePrimaryCtxRetain() returned %s(%i)", errorname ,res);
    }
    const char *msg = gasneti_dynsprintf("GEX_MK_CLASS_CUDA_UVA: %s", why);
    GASNETI_RETURN_ERRR(BAD_ARG,msg);
  }

  int isUVA;
  if (cuDeviceGetAttribute(&isUVA, CU_DEVICE_ATTRIBUTE_UNIFIED_ADDRESSING, dev)) {
    GASNETI_RETURN_ERRR(BAD_ARG,"GEX_MK_CLASS_CUDA_UVA: failed to query CUDA device for UVA support");
  }
  if (!isUVA) {
    GASNETI_RETURN_ERRR(BAD_ARG,"GEX_MK_CLASS_CUDA_UVA: passed context for a non-UVA device");
  }

  my_MK_t result = (my_MK_t) gasneti_alloc_mk(client, get_impl(), flags);
  result->dev = dev;
  result->ctx = ctx;

  *i_memkind_p = (gasneti_MK_t) result;
  return GASNET_OK;
}

#endif

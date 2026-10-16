/*   $Source: bitbucket.org:berkeleylab/gasnet.git/ucx-conduit/gasnet_kinds.c $
 * Description: GASNet Memory Kinds implementation
 * Copyright 2022, The Regents of the University of California
 * Terms of use are as specified in license.txt
 */

#define GASNETI_NEED_GASNET_MK_H 1
#include <gasnet_internal.h>
#include <gasnet_kinds_internal.h>

#include <uct/api/uct.h>

// 1 if found
// 0 if not found
// -1 if error prevented search
static
int check_transport(const char *tr) {
  int found = 0;
  ucs_status_t st;

#if UCT_API < UCT_VERSION(1,7)
  uct_md_resource_desc_t *mds = NULL;
  unsigned int num_md;
  st = uct_query_md_resources(&mds, &num_md);
  if (st) return -1;
  for (unsigned int i = 0; (i < num_md) && !found; ++i) {
    found = !strcmp(tr, mds[i].md_name);
  }
  uct_release_md_resource_list(mds);
#else
  uct_component_h *comps;
  unsigned int num_comp;
  st = uct_query_components(&comps, &num_comp);
  if (st) return -1;
  for (unsigned int i = 0; (i < num_comp) && !found; ++i) {
    uct_component_attr_t attr;
    attr.field_mask = UCT_COMPONENT_ATTR_FIELD_NAME |
                      UCT_COMPONENT_ATTR_FIELD_MD_RESOURCE_COUNT;
    st = uct_component_query(comps[i], &attr);
    if (st) { found = -1; break; }
    found = !strcmp(tr, attr.name) && attr.md_resource_count;
  }
  uct_release_component_list(comps);
#endif

  return found;
}

int gasnetc_mk_create_hook(
                    gasneti_MK_t                     kind,
                    gasneti_Client_t                 client,
                    const gex_MK_Create_args_t       *args,
                    gex_Flags_t                      flags)
{
  // Verify that the UCX library has support for the requested device
  //
  // We probe "[foo]_ipc" because these names have remained stable across
  // versions, while both "cuda_copy" and "cuda_cpy" have been used at times.
  // However, "[foo]_copy" and "[foo]_ipc" are inseparable in the UCX build.

  int found = 0;
  switch (args->gex_class) {
  #if GASNETI_MK_CLASS_CUDA_UVA_ENABLED
    case GEX_MK_CLASS_CUDA_UVA:
      found = check_transport("cuda_ipc");
      break;
  #endif

  #if GASNETI_MK_CLASS_HIP_ENABLED
    case GEX_MK_CLASS_HIP:
    #if GASNETI_HIP_PLATFORM_AMD
      found = check_transport("rocm_ipc");
    #elif GASNETI_HIP_PLATFORM_NVIDIA
      found = check_transport("cuda_ipc");
    #else
      #error Unknown HIP platform
    #endif
      break;
  #endif

  #if GASNETI_MK_CLASS_ZE_ENABLED
    case GEX_MK_CLASS_ZE:
      found = check_transport("ze_ipc");
      break;
  #endif

    default:
      gasneti_unreachable_error(("Unknown memory kind '%s'", kind->_mk_impl->mk_name));
      break;
  }

  if (found != 1) {
    GASNETI_RETURN_ERRR(BAD_ARG,"Requested device memory type is not supported in the UCX library");
  }

  return GASNET_OK;
}

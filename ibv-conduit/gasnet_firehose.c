/* vapi-conduit/gasnet_firehose.c
 * $Date: 2004/02/09 23:03:32 $
 * $Revision: 1.2 $
 * Description: Client-specific firehose code
 * Copyright 2003, E. O. Lawrence Berekely National Laboratory
 * Terms of use are as specified in license.txt
 */

/* Implement client-specific callbacks for use by firehose-region */

#include <gasnet.h>
#include <gasnet_internal.h>
#include <gasnet_core_internal.h>
#include <gasnet_extended_internal.h>

#if GASNETC_USE_FIREHOSE /* otherwise file is empty */

extern int
firehose_move_callback(gasnet_node_t node,
                       const firehose_region_t *unpin_list,
                       size_t unpin_num,
                       firehose_region_t *pin_list,
                       size_t pin_num)
#if FIREHOSE_VAPI_USE_FMR
{
    VAPI_ret_t    vstat;
    EVAPI_fmr_map_t map;
    EVAPI_fmr_hndl_t *handles;
    int repin_num;
    int i;

    map.page_array_len = 0;

    /* Perform all the unpins with a single unmap call: */
    if (unpin_num) {
      handles = alloca(unpin_num * sizeof(EVAPI_fmr_hndl_t));
      for (i = 0; i < unpin_num; ++i) {
	GASNETC_STAT_EVENT_VAL(DYNAMIC_UNPIN, (int)unpin_list[i].len/GASNET_PAGESIZE);
	handles[i] = unpin_list[i].client.handle;
      }
      vstat = EVAPI_unmap_fmr(gasnetc_hca, unpin_num, handles);
      GASNETC_VAPI_CHECK(vstat, "from EVAPI_unmap_fmr");
    }

    /* Reuse the unmapped FMRs where possible */
    repin_num = MIN(unpin_num, pin_num);
    for (i = 0; i < repin_num; i++) {
	pin_list[i].client.handle = unpin_list[i].client.handle;
    }
    
    /* Destroy excess FMRs (if any) */
    for (i = repin_num; i < unpin_num; i++) {
      vstat = EVAPI_free_fmr(gasnetc_hca, unpin_list[i].client.handle);
      GASNETC_VAPI_CHECK(vstat, "from EVAPI_free_fmr");
    }

    /* Allocate more FMRs (if needed) */
    for (i = repin_num; i < pin_num; i++) {
      vstat = EVAPI_alloc_fmr(gasnetc_hca, &gasnetc_fmr_props,
			      &(pin_list[i].client.handle));
      GASNETC_VAPI_CHECK(vstat, "from EVAPI_alloc_fmr");
    }

    /* Now perform all the mappings */
    for (i = 0; i < pin_num; i++) {
	firehose_region_t *region = pin_list + i;

	gasneti_assert(region->addr % GASNETI_PAGESIZE == 0);
	gasneti_assert(region->len % GASNETI_PAGESIZE == 0);

	map.start = (uintptr_t)region->addr;
	map.size  = region->len;
        vstat = EVAPI_map_fmr(gasnetc_hca, region->client.handle, &map,
			      &(region->client.lkey), &(region->client.rkey));
        GASNETC_VAPI_CHECK(vstat, "from EVAPI_map_fmr");
	GASNETC_STAT_EVENT_VAL(DYNAMIC_PIN, (int)pin_list[i].len/GASNET_PAGESIZE);
    }

    return 0;
}
#else
{
    VAPI_ret_t    vstat;
    VAPI_mr_t     mr_in;
    int repin_num;
    int i;

    mr_in.type    = VAPI_MR;
    mr_in.pd_hndl = gasnetc_pd;
    mr_in.acl     = VAPI_EN_LOCAL_WRITE |
		    VAPI_EN_REMOTE_WRITE |
		    VAPI_EN_REMOTE_READ;

    /* Perform replacements where possible */
    repin_num = MIN(unpin_num, pin_num);
    for (i = 0; i < repin_num; i++) {
	firehose_region_t *region = pin_list + i;
	firehose_client_t *client = &region->client;
	VAPI_mr_hndl_t old_handle = unpin_list[i].client.handle;
	VAPI_mr_t mr_out;

	gasneti_assert(region->addr % GASNETI_PAGESIZE == 0);
	gasneti_assert(region->len % GASNETI_PAGESIZE == 0);

	mr_in.start = (uintptr_t)region->addr;
	mr_in.size  = region->len;

	vstat = VAPI_reregister_mr(gasnetc_hca, old_handle,
				   VAPI_MR_CHANGE_TRANS,
				   &mr_in, &client->handle, &mr_out);
        GASNETC_VAPI_CHECK(vstat, "from VAPI_reregister_mr");

	client->lkey     = mr_out.l_key;
	client->rkey     = mr_out.r_key;
    }
    unpin_list += repin_num;
    unpin_num -= repin_num;
    pin_list += repin_num;
    pin_num -= repin_num;

    /* Take care of any "left over".
     * Note that we can't have entries left in *both* lists */
    if (unpin_num) {
        for (i = 0; i < unpin_num; i++) {
	    VAPI_mr_hndl_t old_handle = unpin_list[i].client.handle;

	    vstat = VAPI_deregister_mr(gasnetc_hca, old_handle);
            GASNETC_VAPI_CHECK(vstat, "from VAPI_deregister_mr");
        }
    }
    else if (pin_num) {
        for (i = 0; i < pin_num; i++) {
	    firehose_region_t *region = pin_list + i;
	    firehose_client_t *client = &region->client;
	    VAPI_mr_t mr_out;
    
	    gasneti_assert(region->addr % GASNETI_PAGESIZE == 0);
	    gasneti_assert(region->len % GASNETI_PAGESIZE == 0);
    
	    mr_in.start = (uintptr_t)region->addr;
	    mr_in.size  = region->len;
    
	    vstat = VAPI_register_mr(gasnetc_hca, &mr_in, &client->handle, &mr_out);
            GASNETC_VAPI_CHECK(vstat, "from VAPI_register_mr");
    
	    client->lkey     = mr_out.l_key;
	    client->rkey     = mr_out.r_key;
	}
    }
    return 0;
}
#endif

extern int
firehose_remote_callback(gasnet_node_t node,
                         const firehose_region_t *pin_list, size_t num_pinned,
                         firehose_remotecallback_args_t *args)
{
    /* DO NOTHING.  IF WE GET CALLED WE COMPLAIN. */
    gasneti_fatalerror("attempted to call firehose_remote_callback()");
    return -1;
}

#endif /* GASNETC_USE_FIREHOSE */

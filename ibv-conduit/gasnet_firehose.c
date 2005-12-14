/*   $Source: /Users/kamil/work/gasnet-cvs2/gasnet/ibv-conduit/gasnet_firehose.c,v $
 *     $Date: 2005/12/14 01:46:14 $
 * $Revision: 1.11 $
 * Description: Client-specific firehose code
 * Copyright 2003, E. O. Lawrence Berekely National Laboratory
 * Terms of use are as specified in license.txt
 */

/* Implement client-specific callbacks for use by firehose-region */

#include <gasnet_internal.h>
#include <gasnet_core_internal.h>
#include <gasnet_extended_internal.h>

#if GASNETI_STATS_OR_TRACE
  #define GASNETC_TRACE_MR(_event, _verb, _region) do {                  \
	const firehose_region_t *_reg = (_region);                       \
	int _pages = (int)(_reg->len/GASNET_PAGESIZE);                   \
	GASNETI_TRACE_PRINTF(D, ("FIREHOSE_MOVE: " _STRINGIFY(_verb)     \
				 " %d page(s) at " GASNETI_LADDRFMT,     \
				 _pages, GASNETI_LADDRSTR(_reg->addr))); \
	GASNETC_STAT_EVENT_VAL(_event, _pages);                          \
  } while(0)
  #define GASNETC_TRACE_PIN(_region)	GASNETC_TRACE_MR(FIREHOSE_PIN, pin, (_region))
  #define GASNETC_TRACE_UNPIN(_region)	GASNETC_TRACE_MR(FIREHOSE_UNPIN, unpin, (_region))
#else
  #define GASNETC_TRACE_PIN(_region) 	((void)0)
  #define GASNETC_TRACE_UNPIN(_region) 	((void)0)
#endif

extern int
firehose_move_callback(gasnet_node_t node,
                       const firehose_region_t *unpin_list,
                       size_t unpin_num,
                       firehose_region_t *pin_list,
                       size_t pin_num)
#if FIREHOSE_VAPI_USE_FMR
{
    GASNETC_TRACE_WAIT_BEGIN();
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
	GASNETC_TRACE_UNPIN(&unpin_list[i]);
	handles[i] = unpin_list[i].client.handle;
      }
      vstat = EVAPI_unmap_fmr(GASNETC_HCA_ZERO.handle, unpin_num, handles);
      GASNETC_VAPI_CHECK(vstat, "from EVAPI_unmap_fmr");
    }

    /* Reuse the unmapped FMRs where possible */
    repin_num = MIN(unpin_num, pin_num);
    for (i = 0; i < repin_num; i++) {
	pin_list[i].client.handle = unpin_list[i].client.handle;
    }
    
    /* Destroy excess FMRs (if any) */
    for (i = repin_num; i < unpin_num; i++) {
      vstat = EVAPI_free_fmr(GASNETC_HCA_ZERO.handle, unpin_list[i].client.handle);
      GASNETC_VAPI_CHECK(vstat, "from EVAPI_free_fmr");
    }

    /* Allocate more FMRs (if needed) */
    for (i = repin_num; i < pin_num; i++) {
      vstat = EVAPI_alloc_fmr(GASNETC_HCA_ZERO.handle, &GASNETC_HCA_ZERO.fmr_props,
			      &(pin_list[i].client.handle));
      GASNETC_VAPI_CHECK(vstat, "from EVAPI_alloc_fmr");
    }

    /* Now perform all the mappings */
    for (i = 0; i < pin_num; i++) {
	firehose_region_t *region = pin_list + i;

	gasneti_assert(region->addr % GASNET_PAGESIZE == 0);
	gasneti_assert(region->len % GASNET_PAGESIZE == 0);

	map.start = (uintptr_t)region->addr;
	map.size  = region->len;
        vstat = EVAPI_map_fmr(GASNETC_HCA_ZERO.handle, region->client.handle, &map,
			      &(region->client.lkey), &(region->client.rkey));
        GASNETC_VAPI_CHECK(vstat, "from EVAPI_map_fmr");
	GASNETC_TRACE_PIN(&pin_list[i]);
    }

    GASNETC_TRACE_WAIT_END(FIREHOSE_MOVE);
    return 0;
}
#else
{
    GASNETC_TRACE_WAIT_BEGIN();
    VAPI_ret_t    vstat;
    VAPI_mr_t     mr_in;
    int repin_num;
    int i;

    mr_in.type    = VAPI_MR;
    mr_in.pd_hndl = GASNETC_HCA_ZERO.pd;
    mr_in.acl     = VAPI_EN_LOCAL_WRITE |
		    VAPI_EN_REMOTE_WRITE |
		    VAPI_EN_REMOTE_READ;

    repin_num = MIN(unpin_num, pin_num);

    /* Take care of any unpairable unpins first */
    for (i = repin_num; i < unpin_num; i++) {
	VAPI_mr_hndl_t old_handle = unpin_list[i].client.handle;

	vstat = VAPI_deregister_mr(GASNETC_HCA_ZERO.handle, old_handle);
        GASNETC_VAPI_CHECK(vstat, "from VAPI_deregister_mr");
	GASNETC_TRACE_UNPIN(&unpin_list[i]);
    }

    /* Perform replacements where possible */
    for (i = 0; i < repin_num; i++) {
	firehose_region_t *region = pin_list + i;
	firehose_client_t *client = &region->client;
	VAPI_mr_hndl_t old_handle = unpin_list[i].client.handle;
	VAPI_mr_t mr_out;

	gasneti_assert(region->addr % GASNET_PAGESIZE == 0);
	gasneti_assert(region->len % GASNET_PAGESIZE == 0);

	mr_in.start = (uintptr_t)region->addr;
	mr_in.size  = region->len;

	vstat = VAPI_reregister_mr(GASNETC_HCA_ZERO.handle, old_handle,
				   VAPI_MR_CHANGE_TRANS,
				   &mr_in, &client->handle, &mr_out);
        GASNETC_VAPI_CHECK(vstat, "from VAPI_reregister_mr");
	GASNETC_TRACE_UNPIN(&unpin_list[i]);
	GASNETC_TRACE_PIN(&pin_list[i]);

	client->lkey     = mr_out.l_key;
	client->rkey     = mr_out.r_key;
    }

    /* Take care of any unpairable pins */
    for (i = repin_num; i < pin_num; i++) {
	firehose_region_t *region = pin_list + i;
	firehose_client_t *client = &region->client;
	VAPI_mr_t mr_out;
    
	gasneti_assert(region->addr % GASNET_PAGESIZE == 0);
	gasneti_assert(region->len % GASNET_PAGESIZE == 0);
    
	mr_in.start = (uintptr_t)region->addr;
	mr_in.size  = region->len;
    
	vstat = VAPI_register_mr(GASNETC_HCA_ZERO.handle, &mr_in, &client->handle, &mr_out);
        GASNETC_VAPI_CHECK(vstat, "from VAPI_register_mr");
	GASNETC_TRACE_PIN(&pin_list[i]);
    
	client->lkey     = mr_out.l_key;
	client->rkey     = mr_out.r_key;
    }

    GASNETC_TRACE_WAIT_END(FIREHOSE_MOVE);
    return 0;
}
#endif

extern int
firehose_remote_callback(gasnet_node_t node,
                         const firehose_region_t *pin_list, size_t num_pinned,
                         firehose_remotecallback_args_t *args)
{
    #if GASNETC_PIN_SEGMENT
	/* DO NOTHING.  IF WE GET CALLED WE COMPLAIN. */
	gasneti_fatalerror("invalid attempted to call firehose_remote_callback()");
	return -1;
    #else
	/* Memcpy payload into place */
	gasneti_assert(args != NULL);
	gasneti_assert(args->addr != NULL);
	gasneti_assert(args->len > 0);
	gasneti_assert(args->len <= gasnetc_putinmove_limit);
	memcpy(args->addr, args->data, args->len);
	gasneti_sync_writes();
	return 0;
    #endif
}

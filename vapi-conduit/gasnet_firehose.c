/* vapi-conduit/gasnet_firehose.c
 * $Date: 2003/10/08 16:11:29 $
 * $Revision: 1.1.2.1 $
 * Description: Client-specific firehose code
 * Copyright 2003, E. O. Lawrence Berekely National Laboratory
 * Terms of use are as specified in license.txt
 */

/* (NEARLY) EMPTY FILE FOR NOW */

#include <gasnet.h>
#include <gasnet_internal.h>
#include <gasnet_core_internal.h>
#include <gasnet_extended_internal.h>
#include <firehose.h>

extern int
firehose_move_callback(gasnet_node_t node,
                       const firehose_region_t *unpin_list,
                       size_t unpin_num,
                       firehose_region_t *pin_list,
                       size_t pin_num)
{
    VAPI_ret_t    vstat;
    VAPI_mr_t     mr_in;
    int i;

    for (i = 0; i < unpin_num; i++) {
	const firehose_region_t *region = unpin_list + i;

	assert(region->addr % GASNETI_PAGESIZE == 0);
	assert(region->len % GASNETI_PAGESIZE == 0);

	vstat = VAPI_deregister_mr(gasnetc_hca, region->client.handle);
	assert(vstat == VAPI_OK);
    }
                                                                                                              
    mr_in.type    = VAPI_MR;
    mr_in.pd_hndl = gasnetc_pd;
    mr_in.acl     = VAPI_EN_LOCAL_WRITE | VAPI_EN_REMOTE_WRITE | VAPI_EN_REMOTE_READ;

    for (i = 0; i < pin_num; i++) {
	firehose_region_t *region = pin_list + i;
	firehose_client_t *client = &region->client;
	VAPI_mr_t mr_out;

	assert(region->addr % GASNETI_PAGESIZE == 0);
	assert(region->len % GASNETI_PAGESIZE == 0);
                                                                                                              
	mr_in.start = (uintptr_t)region->addr;
	mr_in.size  = region->len;
                                                                                                              
	vstat = VAPI_register_mr(gasnetc_hca, &mr_in, &client->handle, &mr_out);
	assert(vstat == VAPI_OK);
                                                                                                              
	client->lkey     = mr_out.l_key;
	client->rkey     = mr_out.r_key;
    }

    return -1;	/* not yet implemented */
}

extern int
firehose_remote_callback(gasnet_node_t node,
                         const firehose_region_t *pin_list, size_t num_pinned,
                         firehose_remotecallback_args_t *args)
{
    /* DO NOTHING.  IF WE GET CALLED WE COMPLAIN. */
    assert(0);
    return -1;
}

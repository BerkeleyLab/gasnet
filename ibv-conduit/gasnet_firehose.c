/* vapi-conduit/gasnet_firehose.c
 * $Date: 2003/10/24 21:27:42 $
 * $Revision: 1.1.2.3 $
 * Description: Client-specific firehose code
 * Copyright 2003, E. O. Lawrence Berekely National Laboratory
 * Terms of use are as specified in license.txt
 */

/* Implement client-specific callbacks for use by firehose-region */

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
	gasneti_assert(vstat == VAPI_OK);

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
	    gasneti_assert(vstat == VAPI_OK);
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
	    gasneti_assert(vstat == VAPI_OK);
    
	    client->lkey     = mr_out.l_key;
	    client->rkey     = mr_out.r_key;
	}
    }
    return 0;
}

extern int
firehose_remote_callback(gasnet_node_t node,
                         const firehose_region_t *pin_list, size_t num_pinned,
                         firehose_remotecallback_args_t *args)
{
    /* DO NOTHING.  IF WE GET CALLED WE COMPLAIN. */
    gasneti_fatalerror("attempted to call firehose_remote_callback()");
    return -1;
}

/*   $Source: /Users/kamil/work/gasnet-cvs2/gasnet/lapi-conduit/Attic/gasnet_firehose.c,v $
 *     $Date: 2007/04/18 19:16:01 $
 * $Revision: 1.1.2.1 $
 * Description: Client-specific firehose code
 * Copyright 2007, E. O. Lawrence Berekely National Laboratory
 * Terms of use are as specified in license.txt
 */

/* Implement client-specific callbacks for use by firehose-region */

#include <gasnet_internal.h>
#include <gasnet_core_internal.h>
#include <gasnet_extended_internal.h>

/* Implement the move callback */

/* From the VAPI conduit */
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

int gasnete_lapi_pin_level=0;
size_t total_pinned = 0;
extern int
firehose_move_callback(gasnet_node_t node,
                       const firehose_region_t *unpin_list,
                       size_t unpin_num,
                       firehose_region_t *pin_list,
                       size_t pin_num)
{
  size_t i;
  lapi_get_pvo_t req;

#if 0
  printf("%d Entering move callback, level = %d total = %ld\n",gasneti_mynode,gasnete_lapi_pin_level,total_pinned);
#endif
  /* Unpin first */

  for(i=0;i < unpin_num;i++) {
    const firehose_region_t *region = unpin_list + i;
#if 1
    total_pinned -= region->len; 
#endif
    req.Util_type = LAPI_XLATE_ADDRESS;
    req.length = 0;
    req.usr_pvo = region->client.pvo;
    req.address = 0;
    req.operation = LAPI_RDMA_RELEASE;

    GASNETC_LCHECK(LAPI_Util(gasnetc_lapi_context, (lapi_util_t *) &req)); 

    /*GASNETC_TRACE_UNPIN(&unpin_list[i]);*/
  }

  /* Now pin */
  for(i=0;i < pin_num;i++) {
    firehose_region_t *region = pin_list + i;

    req.Util_type = LAPI_XLATE_ADDRESS;
    req.length = (size_t) region->len;
    req.usr_pvo = 0;
    req.address = (void *) region->addr;
    req.operation = LAPI_RDMA_ACQUIRE;

#if 0
    printf("%d Asking to pin: %ld addr=%p total already pinned= %ld\n",gasneti_mynode,req.length,region->addr,total_pinned);
#endif
    GASNETC_LCHECK(LAPI_Util(gasnetc_lapi_context, (lapi_util_t *) &req)); 
#if 1
    total_pinned += region->len;
#endif
#if 0   
    printf("%d Pin succeeded: %ld\n",gasneti_mynode,req.length);
#endif
    region->client.pvo = req.usr_pvo;

    /*GASNETC_TRACE_PIN(&pin_list[i]);*/
  }

  gasnete_lapi_pin_level += (pin_num - unpin_num);
#if 0
  if(gasnete_lapi_pin_level > 5) {
    printf("> 5 regions pinned on %d (%d)\n",gasneti_mynode,gasnete_lapi_pin_level);
  }
#endif
#if 0
  printf("%d Exiting move callback, level = %d total = %ld\n",gasneti_mynode,gasnete_lapi_pin_level,total_pinned);
#endif
  return 0;
}

extern int
firehose_remote_callback(gasnet_node_t node,
                         const firehose_region_t *pin_list, size_t num_pinned,
                         firehose_remotecallback_args_t *args)
{
  return(0);
}

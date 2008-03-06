#ifndef __GASNET_SMP_COLL_H
#define __GASNET_SMP_COLL_H 1

#include <gasnet_internal.h>
#include <gasnet_tools.h>
#include <gasnet_coll.h>

void gasnete_smp_coll_init(gasnete_coll_team_t team, const gasnet_image_t images[], gasnet_image_t my_image,
			   const gasnet_coll_fn_entry_t fn_tbl[], size_t fn_count,
			   int init_flags GASNETE_THREAD_FARG);



void gasnete_smp_coll_barrier(gasnete_coll_team_t team, int flags GASNETE_THREAD_FARG);


void gasnete_smp_coll_broadcast(gasnete_coll_team_t team,  void *dst,
        						gasnet_image_t srcimage, const void *src,
        						size_t nbytes, int flags GASNETE_THREAD_FARG);

gasnet_coll_handle_t 
gasnete_smp_coll_broadcast_nb(gasnete_coll_team_t team,  void *dst,
        					  gasnet_image_t srcimage, const void *src,
        			     	  size_t nbytes, int flags, uint32_t sequence GASNETE_THREAD_FARG);

void 
gasnete_smp_coll_broadcastM(gasnete_coll_team_t team,  void * const dstlist[],
        						 gasnet_image_t srcimage, const void *src,
        						 size_t nbytes, int flags GASNETE_THREAD_FARG);

gasnet_coll_handle_t 
gasnete_smp_coll_broadcastM_nb(gasnete_coll_team_t team,  void * const dstlist[],
													gasnet_image_t srcimage, const void *src,
													size_t nbytes, int flags, uint32_t sequence GASNETE_THREAD_FARG);

#endif

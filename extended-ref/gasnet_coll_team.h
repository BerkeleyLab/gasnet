/* $Source: /Users/kamil/work/gasnet-cvs2/gasnet/extended-ref/gasnet_coll_team.h,v $ 
 * $Date: 2011/11/17 17:24:34 $ 
 * $Revision: 1.3.34.1 $ 
 *
 * Description: GASNet team implementation for collectives
 * Copyright 2010, E. O. Lawrence Berekely National Laboratory
 * Terms of use are as specified in license.txt 
 */

#ifndef GASNET_COLL_TEAM_H_
#define GASNET_COLL_TEAM_H_

#include <gasnet.h>
#include <gasnet_coll_hashtable.h>

/************************************************************/
/* Team functions                                           */
/************************************************************/

#define GASNETE_COLL_TEAMS_OVERRIDE 
gasnet_team_handle_t gasnete_coll_team_lookup(uint32_t team_id);


void gasnete_coll_team_print(gasnet_team_handle_t team, FILE *fp);


gasnet_image_t gasnete_coll_team_my_local_image(gasnet_team_handle_t team
                                                GASNETE_THREAD_FARG);

extern gasnete_hashtable_t *gasnete_global_team_dir;

/**
 * return the my image id in the team 
 */
gasnet_image_t gasnete_coll_team_my_image(gasnet_team_handle_t team);

/* Return the relative process rank of an image in the team */
gasnet_node_t gasnete_coll_image2rank(gasnet_team_handle_t team,
                                      gasnet_image_t image);


/* Return the gasnet node id (absolute rank in GASNET_TEAM_ALL) of an
   image in the team */
gasnet_node_t gasnete_coll_image2node(gasnet_team_handle_t team,
                                      gasnet_image_t image);


gasnet_node_t gasnete_coll_team_rank2node(gasnet_team_handle_t team, 
                                          gasnet_node_t rank);


gasnet_node_t gasnete_coll_team_node2rank(gasnet_team_handle_t team, 
                                          gasnet_node_t node);


#define gasnete_coll_team_id(team)  ((team)->team_id)

#define gasnete_coll_team_my_mailbox(team) \
  ((team)->mailbox[gasnete_coll_team_my_local_image((team) GASNETE_THREAD_PASS)])

void gasnete_coll_team_mailbox_copy(gasnet_team_handle_t team, void *data, 
                                    gasnet_image_t *images, 
                                    gasnet_image_t image_count);


void gasnete_coll_local_bcast(void *src, void **dst, int dst_count, size_t nbytes);


void gasnete_coll_team_local_bcast(gasnet_team_handle_t team, void *buffer, 
                                   size_t nbytes, gasnet_image_t root);


void gasnete_coll_team_shuffle_pointers(void * const in_ptrs[], 
                                        void * out_ptrs[],
                                        size_t count,
                                        size_t in2out_map[]);


void gasnete_coll_team_shuffle_array(void *src, 
                                     void *dst,
                                     size_t count,
                                     size_t nbytes,
                                     size_t in2out_map[]);


void gasnete_coll_team_unshuffle_array(void *src, 
                                       void *dst,
                                       size_t count,
                                       size_t nbytes,
                                       size_t in2out_map[]);


void gasnete_coll_team_local_barrier(gasnet_team_handle_t team);

/* If the conduit hasn't defined team barrier define it here*/
#ifndef gasnete_coll_teambarrier
#define gasnete_coll_teambarrier(TEAM) do {\
    gasnete_coll_barrier_notify(TEAM, 0, (GASNET_BARRIERFLAG_ANONYMOUS | GASNET_BARRIERFLAG_IMAGES) GASNETE_THREAD_GET); \
		gasnete_coll_barrier_wait(TEAM, 0, (GASNET_BARRIERFLAG_ANONYMOUS | GASNET_BARRIERFLAG_IMAGES) GASNETE_THREAD_GET); \
	} while(0)
#endif

#ifndef gasnete_coll_teambarrier_notify
#define gasnete_coll_teambarrier_notify(TEAM) do {\
    gasnete_coll_barrier_notify(TEAM, 0, (GASNET_BARRIERFLAG_ANONYMOUS | GASNET_BARRIERFLAG_IMAGES) GASNETE_THREAD_GET); \
	} while(0)
#endif

#ifndef gasnete_coll_teambarrier_wait
#define gasnete_coll_teambarrier_wait(TEAM) do {\
    gasnete_coll_barrier_wait(TEAM, 0, (GASNET_BARRIERFLAG_ANONYMOUS | GASNET_BARRIERFLAG_IMAGES) GASNETE_THREAD_GET); \
	} while(0)
#endif

gasnet_node_t gasnete_coll_team_size(gasnet_team_handle_t team);

/* The initialization function for GASNET_TEAM_ALL which requires
 * special handling than other derived teams and thus has a special
 * interface. 
 */
void gasnete_coll_teamall_init(gasnet_node_t total_ranks,
                               gasnet_node_t myrank,
                               gasnet_image_t my_image,
                               const gasnet_image_t images[],
                               gasnet_seginfo_t *scratch_segs
                               GASNETE_THREAD_FARG);

/* Team finalization function for teams except GASNET_TEAM_ALL */
void gasnete_coll_team_fini(gasnet_team_handle_t team  GASNETE_THREAD_FARG);

/**
 * gasnete_coll_team_create - create a team 
 *
 * @param parent_team
 * @param total_images the image count of the new team (size of the images array)
 * @param images the subset of images in the parent_team that will consist the new team
 * @param my_image my relative image id in the new team
 * @param scratch_segs the array of sratch info for all threads in the new team
 * @retrun the handle of the newly created team
 */
gasnet_team_handle_t gasnete_coll_team_create(gasnet_team_handle_t parent_team,
                                              gasnet_image_t total_images,
                                              gasnet_image_t *images,
                                              uint32_t new_team_id,
                                              gasnet_image_t my_new_imageid,
                                              gasnet_seginfo_t *scratch_segs 
                                              GASNETE_THREAD_FARG);

/* Clean up a team when it is no longer needed. */
void gasnete_coll_team_free(gasnet_team_handle_t team GASNETE_THREAD_FARG);

/**
 * gasnete_coll_team_split - create new teams by splitting the parent
 * team and grouping threads with the same color together
 * 
 * @param parent_team the team handle for the parent team
 * @param mycolor my color for the new team
 * @param my_image my image id in the new team
 * @param myscratch_seginfo it's used to pass in the scratch space from the gasnet client
 * @return the handle of the new team which the running thread is in
 */
gasnet_team_handle_t gasnete_coll_team_split(gasnet_team_handle_t parent_team,
                                             gasnet_image_t mycolor,
                                             gasnet_image_t my_image,
                                             gasnet_seginfo_t *myscratch_seginfo
                                             GASNETE_THREAD_FARG);

#endif /* GASNET_COLL_TEAM_H_ */

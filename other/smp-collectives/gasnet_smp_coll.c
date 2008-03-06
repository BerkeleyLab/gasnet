#include <smp-collectives/gasnet_smp_coll_internal.h>
	
#define Z 	do {gasnete_coll_threaddata_t *td = GASNETE_COLL_MYTHREAD_NOALLOC;\
	gasnete_smp_coll_team_t smp_coll_team = (gasnete_smp_coll_team_t) td->smp_coll_team;\
	fprintf(stderr, "%d> %s (%s:%d)\n", smp_coll_team->MYTHREAD, __FUNCTION__, __FILE__, __LINE__);\
} while(0)

/*declare a global variable that contains the GASNETE_SMP_COLL_TEAM_ALL
 * All other refs in include files have been declared w/ extern */



void gasnete_smp_coll_init(gasnete_coll_team_t team, const gasnet_image_t images[], gasnet_image_t my_image,
		   const gasnet_coll_fn_entry_t fn_tbl[], size_t fn_count,
		   int init_flags GASNETE_THREAD_FARG){
	
  static volatile uintptr_t *gasnete_smp_coll_all_flags;
  static gasnett_atomic_t *gasnete_smp_coll_atomic_vars;
  static void **gasnete_smp_coll_addrlist; 
  gasnete_coll_threaddata_t *td = GASNETE_COLL_MYTHREAD;
  gasnete_smp_coll_team_t ret;
  int i;

  ret = (struct gasnete_smp_coll_team_handle_t_*) gasneti_malloc(sizeof(struct gasnete_smp_coll_team_handle_t_));
  /* since tehre is only one GASNet Process the images array has one element,
   * the number of images on the local node*/
  ret->MYTHREAD = my_image;
  if(images)
	  ret->THREADS = images[0];
  else
	  ret->THREADS = 1;
  ret->flag_set = 0;


  BOOTSTRAP_BARRIER(ret,0);
  GASNETE_SMP_COLL_CONSTRUCT_BARR_ROUTINES(ret);
  GASNETE_SMP_COLL_CONSTRUCT_BCAST_ROUTINES(ret);
//  SMP_COLL_CONSTRUCT_SCATTER_ROUTINES(ret);
//  SMP_COLL_CONSTRUCT_GATHER_ROUTINES(ret);
//  SMP_COLL_CONSTRUCT_ALLRED_INT_ROUTINES(ret);
  
  BOOTSTRAP_BARRIER(ret,0);
  if(ret->MYTHREAD==0) {
    int t;
    gasnete_smp_coll_all_flags = (volatile uintptr_t*) gasneti_malloc(GASNETE_SMP_COLL_CACHE_LINE*ret->THREADS*sizeof(uintptr_t)+GASNETE_SMP_COLL_CACHE_LINE);
    gasnete_smp_coll_atomic_vars = (gasnett_atomic_t*) gasneti_malloc(2*GASNETE_SMP_COLL_CACHE_LINE*ret->THREADS*sizeof(gasnett_atomic_t)+GASNETE_SMP_COLL_CACHE_LINE);
    gasnete_smp_coll_addrlist = (void**) gasneti_malloc(ret->THREADS*sizeof(void*)+GASNETE_SMP_COLL_CACHE_LINE);
  }
  BOOTSTRAP_BARRIER(ret,0);
  
  ret->flags = (volatile uintptr_t*) GASNETI_ALIGNUP(gasnete_smp_coll_all_flags,GASNETE_SMP_COLL_CACHE_LINE);
  ret->atomic_vars = (gasnett_atomic_t*) GASNETI_ALIGNUP(gasnete_smp_coll_atomic_vars,GASNETE_SMP_COLL_CACHE_LINE);  
  ret->addrlist = (void**) GASNETI_ALIGNUP(gasnete_smp_coll_addrlist, GASNETE_SMP_COLL_CACHE_LINE);
  ret->curr_atomic_set = 0;
  
  for(i=0; i<GASNETE_SMP_COLL_CACHE_LINE; i++) {
    GASNETE_SMP_COLL_SET_FLAG(ret, ret->MYTHREAD, i, 0);    
    gasnett_atomic_set(&ret->atomic_vars[ret->MYTHREAD*GASNETE_SMP_COLL_CACHE_LINE+i], 0, GASNETT_ATOMIC_MB_POST);
    gasnett_atomic_set(&ret->atomic_vars[ret->THREADS*GASNETE_SMP_COLL_CACHE_LINE+ret->MYTHREAD*GASNETE_SMP_COLL_CACHE_LINE+i], 0, GASNETT_ATOMIC_MB_POST);
  }

  /*ret->dissem_info=NULL;
  smp_coll_set_barrier_routine(ret, SMP_COLL_BARRIER_COND_VAR, 2);
  smp_coll_set_broadcast_routine(ret, SMP_COLL_BROADCAST_TREE_FLAG, 2);
  smp_coll_set_all_reduce_int_routine(ret, SMP_COLL_ALL_REDUCE_INT_DISSEM2_FLAG, 2);*/
  /*gasnett_mmap*/
  //ret->aux_space = (uint8_t*) gasneti_malloc(sizeof(uint8_t)*GASNETE_SMP_COLL_AUX_SPACE_SIZE);
  BOOTSTRAP_BARRIER(ret,0);
  td->smp_coll_team = (void*) ret;
}

/* basic condition variable barrier*/
void gasnete_smp_coll_barrier_cond_var(gasnete_smp_coll_team_t team, int flags GASNETE_THREAD_FARG){
  static gasnett_cond_t barrier_cond[2] = /* must be phased on some OS's (HPUX) */
  { GASNETT_COND_INITIALIZER, GASNETT_COND_INITIALIZER };
  static gasnett_mutex_t barrier_mutex[2] = 
  { GASNETT_MUTEX_INITIALIZER, GASNETT_MUTEX_INITIALIZER  };
  static volatile unsigned int barrier_count = 0;
  static volatile int phase = 0;
  const int myphase = phase;
  gasnett_local_wmb(); /*Make sure that all writes before the barrier are done*/
  gasnett_mutex_lock(&barrier_mutex[myphase]);
  barrier_count++;
  /* if i am not hte last thread to enter the barrier, 
   * release teh lock and go to sleep*/
  if (barrier_count < team->THREADS) {
    /* CAUTION: changing the "do-while" to a "while" triggers a bug in the SunStudio 2006-08
     * compiler for x86_64.  See http://upc-bugs.lbl.gov/bugzilla/show_bug.cgi?id=1858
     * which includes a link to Sun's own database entry for this issue.
     */
    do {
      gasnett_cond_wait(&barrier_cond[myphase], &barrier_mutex[myphase]);
    } while (myphase == phase);
  } else {  
	  /* i am the last thread to enter the barrier. 
	   * wake all others up indicating the barrier is done*/
    barrier_count = 0;
    phase = !phase;
    gasnett_cond_broadcast(&barrier_cond[myphase]);
  }       
  gasnett_mutex_unlock(&barrier_mutex[myphase]);
  gasnett_local_rmb(); /*make sure that the barrier is done before any reads are issued*/
}

void gasnete_smp_coll_barrier(gasnete_coll_team_t team, int flags GASNETE_THREAD_FARG) {
	gasnete_coll_threaddata_t *td = GASNETE_COLL_MYTHREAD_NOALLOC;
	gasnete_smp_coll_team_t smp_coll_team = (gasnete_smp_coll_team_t) td->smp_coll_team;
		
	(*(smp_coll_team)->barr_fns[(smp_coll_team)->curr_barrier_routine])(smp_coll_team, flags GASNETE_THREAD_PASS);
}


void gasnete_smp_coll_broadcast_flat(gasnete_smp_coll_team_t team, 
									void * const dstlist[], 
									gasnet_image_t srcimage, const void *src, 
									size_t nbytes, int flags GASNETE_THREAD_FARG){
  int idx = 0;
  
  if(!(flags & GASNET_COLL_IN_NOSYNC)) FAST_BARRIER(team, flags GASNETE_THREAD_PASS); 
  if(team->MYTHREAD==srcimage) {
	  for(idx=0; idx<team->THREADS; idx++) {
		  GASNETE_FAST_UNALIGNED_MEMCPY_CHECK(dstlist[idx], src, nbytes); 
	  }
  }
  if(!(flags & GASNET_COLL_OUT_NOSYNC)) FAST_BARRIER(team, flags GASNETE_THREAD_PASS); 

}

/********************
 * Public Functions *
 ********************/

void gasnete_smp_coll_broadcast(gasnete_coll_team_t team, 
								void * dst, 
								gasnet_image_t srcimage, const void *src, 
								size_t nbytes, int flags GASNETE_THREAD_FARG) {
	GASNETE_FAST_UNALIGNED_MEMCPY_CHECK(dst, src, nbytes);
}

gasnet_coll_handle_t gasnete_smp_coll_broadcast_nb(gasnete_coll_team_t team, 
								void * dst, 
								gasnet_image_t srcimage, const void *src, 
								size_t nbytes, int flags, uint32_t sequence GASNETE_THREAD_FARG) {
	gasnete_smp_coll_broadcast(team, dst, srcimage, src, nbytes, flags GASNETE_THREAD_PASS);
	return GASNET_COLL_INVALID_HANDLE;
}


GASNETI_INLINE(gasnete_smp_coll_broadcastM_inner)
void gasnete_smp_coll_broadcastM_inner(gasnete_coll_team_t team, 
								void * const dstlist[], 
								gasnet_image_t srcimage, const void *src, 
								size_t nbytes, int flags GASNETE_THREAD_FARG) {
	gasnete_coll_threaddata_t *td = GASNETE_COLL_MYTHREAD_NOALLOC;
	gasnete_smp_coll_team_t smp_coll_team = (gasnete_smp_coll_team_t) td->smp_coll_team;
	
	if(flags & GASNETE_COLL_THREAD_LOCAL) {
		
		smp_coll_team->addrlist[smp_coll_team->MYTHREAD] = dstlist[0];

		FAST_BARRIER(smp_coll_team, flags GASNETE_THRED_PASS); /* ensure that all threads post their address*/
		
		/*since we are imposing a barrier before and after the collective, we can safely pass IN/OUT NOSYNC to the collective
		 * regardless of the input synchmode*/
		(*(smp_coll_team)->bcast_fns[(smp_coll_team)->curr_bcast_routine])(smp_coll_team, smp_coll_team->addrlist, 
				srcimage, src, nbytes, GASNET_COLL_IN_NOSYNC|GASNET_COLL_OUT_NOSYNC GASNETE_THREAD_PASS);		
		
		/*ensure that the all threads see the collective finished before we allow addrlist to be modified*/
		FAST_BARRIER(smp_coll_team, flags GASNETE_THREAD_PASS); 
		
		
			
	} else {
		(*(smp_coll_team)->bcast_fns[(smp_coll_team)->curr_bcast_routine])(smp_coll_team, dstlist, srcimage, 
																		   src, nbytes, flags GASNETE_THREAD_PASS);
	}

}

void gasnete_smp_coll_broadcastM(gasnete_coll_team_t team, 
								void * const dstlist[], 
								gasnet_image_t srcimage, const void *src, 
								size_t nbytes, int flags GASNETE_THREAD_FARG) {
	
	gasnete_smp_coll_broadcastM_inner(team, dstlist, srcimage, src, nbytes, flags GASNETE_THREAD_PASS);
}

gasnet_coll_handle_t gasnete_smp_coll_broadcastM_nb(gasnete_coll_team_t team, 
								void * const dstlist[], 
								gasnet_image_t srcimage, const void *src, 
								size_t nbytes, int flags, uint32_t sequence GASNETE_THREAD_FARG) {
	
	gasnete_smp_coll_broadcastM_inner(team, dstlist, srcimage, src, nbytes, flags GASNETE_THREAD_PASS);
	return GASNET_COLL_INVALID_HANDLE;

}




#undef check_zeroret

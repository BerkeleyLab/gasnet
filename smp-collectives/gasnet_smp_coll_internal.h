#include <gasnet_internal.h>
#include <extended-ref/gasnet_coll_internal.h>
#include <smp-collectives/gasnet_smp_coll.h>

struct gasnete_smp_coll_team_handle_t_;
typedef struct gasnete_smp_coll_team_handle_t_ *gasnete_smp_coll_team_t;


#define GASNETE_SMP_COLL_CACHE_LINE MAX(GASNETT_CACHE_LINE_BYTES,64)
#define GASNETE_SMP_COLL_AUX_SPACE_SIZE 64*1024

static inline int gasnete_smp_coll_mylogn(int num, int base) {
  int ret=1;
  int mult = base;
  while (num > mult) {
    ret++;
    mult*=base;
  }
  return ret;
  
}

static inline int gasnete_smp_coll_mypown(int base, int power) {  
  int ret=1;
  
  while(power>0) {
    ret*=base;
    power--;
  }
  return ret;
}


#define GASNETE_SMP_COLL_GET_FLAG(HANDLE,THREAD_ID,IDX)\
((volatile uint8_t*)(HANDLE)->flags)[(THREAD_ID)*GASNETE_SMP_COLL_CACHE_LINE+(IDX)]

#define GASNETE_SMP_COLL_SET_FLAG(HANDLE,THREAD_ID,IDX,NEW_VAL)\
((volatile uint8_t*)(HANDLE)->flags)[(THREAD_ID)*GASNETE_SMP_COLL_CACHE_LINE+(IDX)] = (uint8_t)(NEW_VAL)

#define GASNETE_SMP_COLL_READ_ATOMIC(HANDLE,THREAD,IDX,FLAG_SET)\
(int)gasnett_atomic_read(&((HANDLE)->atomic_vars)[(FLAG_SET)*(HANDLE)->THREADS*GASNETE_SMP_COLL_CACHE_LINE+(THREAD)*GASNETE_SMP_COLL_CACHE_LINE+IDX], GASNETT_ATOMIC_NONE)

#define GASNETE_SMP_COLL_INC_ATOMIC(HANDLE,THREAD,IDX,FLAG_SET)\
gasnett_atomic_increment(&((HANDLE)->atomic_vars)[(FLAG_SET)*(HANDLE)->THREADS*GASNETE_SMP_COLL_CACHE_LINE+(THREAD)*GASNETE_SMP_COLL_CACHE_LINE+IDX], GASNETT_ATOMIC_NONE)

#define GASNETE_SMP_COLL_DEC_ATOMIC(HANDLE,THREAD,IDX,FLAG_SET)\
gasnett_atomic_decrement(&((HANDLE)->atomic_vars)[(FLAG_SET)*(HANDLE)->THREADS*GASNETE_SMP_COLL_CACHE_LINE+(THREAD)*GASNETE_SMP_COLL_CACHE_LINE+IDX], GASNETT_ATOMIC_NONE)


#define GASNETE_SMP_COLL_RESET_ATOMIC(HANDLE,THREAD,IDX,FLAG_SET)\
gasnett_atomic_set(&((HANDLE)->atomic_vars)[(FLAG_SET)*(HANDLE)->THREADS*GASNETE_SMP_COLL_CACHE_LINE+(THREAD)*GASNETE_SMP_COLL_CACHE_LINE+IDX], 0, GASNETT_ATOMIC_NONE);


/*
 Utility operations for constructing power 2 trees
*/
#define GASNETE_SMP_COLL_GET_ITH_DIGIT_POWER2RADIX(NUMBER,DIGIT_ID,RADIX,LOG_2_RADIX) \
(((NUMBER) & ((RADIX)-1)<<((DIGIT_ID)*(LOG_2_RADIX))) >> ((DIGIT_ID)*(LOG_2_RADIX)))

/*take the number template and replace digits 0-DIGIT_ID-1 with 0 and DIGIT with NEW_DIGIT*/
#define GASNETE_SMP_COLL_MAKE_NUM_POWER2RADIX(TEMPLATE_NUMBER,DIGIT_ID,NEW_DIGIT,RADIX,LOG_2_RADIX) \
(((TEMPLATE_NUMBER) & (-1)<<(((DIGIT_ID+1))*(LOG_2_RADIX))) + ((NEW_DIGIT) << ((DIGIT_ID))*(LOG_2_RADIX)))

/*take the number template and replace i^th digit with NEW_DIGIT*/
#define GASNETE_SMP_COLL_REPLACE_DIGIT_POWER2RADIX(TEMPLATE_NUMBER,DIGIT_ID,NEW_DIGIT,RADIX,LOG_2_RADIX) \
(((TEMPLATE_NUMBER) & ~(((RADIX)-1)<<(((DIGIT_ID)*(LOG_2_RADIX))))) + ((NEW_DIGIT) << ((DIGIT_ID))*(LOG_2_RADIX)))

/*take the number and get digits DIGIT_ID through last digit*/
#define GASNETE_SMP_COLL_GET_UPPER_K_DIGITS_POWER2RADIX(NUMBER,DIGIT_ID,RADIX,LOG_2_RADIX) \
((NUMBER) >> ((DIGIT_ID)*(LOG_2_RADIX)))

#define GASNETE_SMP_COLL_GET_LOWER_K_DIGITS_POWER2RADIX(NUMBER,DIGIT_ID,RADIX,LOG_2_RADIX) \
((NUMBER) & ~((-1)<<((DIGIT_ID)*(LOG_2_RADIX))))


typedef void (*GASNETE_SMP_COLL_BARR_FN)(gasnete_smp_coll_team_t team, int flags GASNETE_THREAD_FARG);

void gasnete_smp_coll_barrier_cond_var(gasnete_smp_coll_team_t team, int flags GASNETE_THREAD_FARG);
void gasnete_smp_coll_barrier_dissem_atomic(gasnete_smp_coll_team_t team, int flags GASNETE_THREAD_FARG);
void gasnete_smp_coll_barrier_tree_atomic(gasnete_smp_coll_team_t team, int flags GASNETE_THREAD_FARG);
void gasnete_smp_coll_barrier_tree_flag(gasnete_smp_coll_team_t team, int flags GASNETE_THREAD_FARG);

#define GASNETE_SMP_COLL_NUM_BARR_ROUTINES 1
#define GASNETE_SMP_COLL_CONSTRUCT_BARR_ROUTINES(HANDLE) do{\
(HANDLE)->barr_fns[0] = gasnete_smp_coll_barrier_cond_var; \
/*(HANDLE)->barr_fns[1] = gasnete_smp_coll_barrier_dissem_atomic;*/ \
/*(HANDLE)->barr_fns[2] = gasnete_smp_coll_barrier_tree_atomic;*/ \
/*(HANDLE)->barr_fns[3] = gasnete_smp_coll_barrier_tree_flag;*/ \
(HANDLE)->curr_barrier_routine=0;\
} while(0)

typedef void (*GASNETE_SMP_COLL_BCAST_FN)(gasnete_smp_coll_team_t team, void * const dstlist[], gasnet_image_t srcimage, 
					  const void *src, size_t nbytes, int flags GASNETE_THREAD_FARG);

void gasnete_smp_coll_broadcast_flat(gasnete_smp_coll_team_t team, void * const dstlist[], gasnet_image_t srcimage, 
				     const void *src, size_t nbytes, int flags GASNETE_THREAD_FARG);
void gasnete_smp_coll_broadcast_tree_atomic(gasnete_smp_coll_team_t team, void * const dstlist[], gasnet_image_t srcimage, 
					    const void *src, size_t nbytes, int flags GASNETE_THREAD_FARG);
void gasnete_smp_coll_broadcast_tree_flag(gasnete_smp_coll_team_t team, void * const dstlist[], gasnet_image_t srcimage, 
					  const void *src, size_t nbytes, int flags GASNETE_THREAD_FARG);

#define GASNETE_SMP_COLL_NUM_BROADCAST_ROUTINES 1
#define GASNETE_SMP_COLL_CONSTRUCT_BCAST_ROUTINES(HANDLE) do{	\
(HANDLE)->bcast_fns[0] = gasnete_smp_coll_broadcast_flat; \
/*(HANDLE)->bcast_fns[1] = gasnete_smp_coll_broadcast_tree_atomic;*/ \
/*(HANDLE)->bcast_fns[2] = gasnete_smp_coll_broadcast_tree_flag;*/ \
(HANDLE)->curr_bcast_routine=0;\
} while(0)


struct gasnete_smp_coll_team_handle_t_{
  int THREADS;
  int MYTHREAD;
  pthread_t thread_id;
  volatile uintptr_t *flags;
  int flag_set;
  gasnett_atomic_t *atomic_vars;
  int curr_atomic_set;
  void **addrlist; /*set up for GASNETE_COLL_THREAD_LOCAL*/
  
  GASNETE_SMP_COLL_BARR_FN barr_fns[GASNETE_SMP_COLL_NUM_BARR_ROUTINES];
//  dissem_info_t *dissem_info;
  int barrier_radix, barrier_log_2_radix, barrier_log_radix_THREADS;
  int curr_barrier_routine;


  GASNETE_SMP_COLL_BCAST_FN bcast_fns[GASNETE_SMP_COLL_NUM_BROADCAST_ROUTINES];
  int broadcast_radix, broadcast_log_2_radix, broadcast_log_radix_THREADS;
  int curr_bcast_routine;

  
  uint8_t *aux_space;
};

#define BOOTSTRAP_BARRIER(SMP_COLL_TEAM, FLAGS) gasnete_smp_coll_barrier_cond_var((SMP_COLL_TEAM), (FLAGS) GASNETE_THREAD_PASS)
#define FAST_BARRIER(SMP_COLL_TEAM, FLAGS) (*(SMP_COLL_TEAM)->barr_fns[(SMP_COLL_TEAM)->curr_barrier_routine])(SMP_COLL_TEAM, flags GASNETE_THREAD_PASS)




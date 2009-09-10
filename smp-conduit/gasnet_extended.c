/*   $Source: /Users/kamil/work/gasnet-cvs2/gasnet/smp-conduit/Attic/gasnet_extended.c,v $
 *     $Date: 2009/09/10 02:19:00 $
 * $Revision: 1.1.2.2 $
 * Description: GASNet Extended API for smp-conduit
 * Copyright 2009, E. O. Lawrence Berekely National Laboratory
 * Terms of use are as specified in license.txt
 */

#include <gasnet_core_internal.h>

#ifndef _IN_GASNET_EXTENDED_C
#define _IN_GASNET_EXTENDED_C
#else
#error "#include loop detected"
#endif

/* ------------------------------------------------------------------------------------ */
#if GASNET_PSHM
/*
  Conduit-specifc Barrier "interface":
  ===================================
*/

static void gasnete_pshmbarrier_init(void);
static void gasnete_pshmbarrier_notify(int id, int flags);
static int gasnete_pshmbarrier_wait(int id, int flags);
static int gasnete_pshmbarrier_try(int id, int flags);

#define GASNETE_BARRIER_DEFAULT "PSHM_BARRIER"
#define GASNETE_BARRIER_INIT() do {                         \
    if (GASNETE_ISBARRIER("PSHM_BARRIER")) {                \
      gasnete_barrier_notify = &gasnete_pshmbarrier_notify; \
      gasnete_barrier_wait =   &gasnete_pshmbarrier_wait;   \
      gasnete_barrier_try =    &gasnete_pshmbarrier_try;    \
      gasnete_pshmbarrier_init();                           \
    }                                                       \
  } while (0)

#endif /* GASNET_PSHM */
/* ------------------------------------------------------------------------------------ */

/* pull in the reference extended w/o any changes */
#include "extended-ref/gasnet_extended.c"

/* ------------------------------------------------------------------------------------ */
#if GASNET_PSHM
/*
  Conduit-specifc Barrier "implementation":
  ========================================
*/

/* The "flip" variable - alternates between 0 and 1
 * and prevents deadlock if 2 barriers are consecutievly
 * called */
static int flip;

static void gasnete_pshmbarrier_init(void) {
    int i;
    
    barrier_splitstate = OUTSIDE_BARRIER;

    /* Counter used to detect that all nodes have reached the barrier */
    gasneti_atomic_set(&gasneti_pshm_barrier->counter[0], 0, GASNETI_ATOMIC_REL);
    gasneti_atomic_set(&gasneti_pshm_barrier->counter[1], 0, GASNETI_ATOMIC_REL);

    /* Counter used to detect the last node to leave the barrier. This
     * node will reset the barrier values. */
    gasneti_atomic_set(&gasneti_pshm_barrier->done[0], 0, GASNETI_ATOMIC_REL);
    gasneti_atomic_set(&gasneti_pshm_barrier->done[1], 0, GASNETI_ATOMIC_REL);

    /* Variables used to detect if all the values of the named barriers
     * are equal. Using these variables we avoid scanning through
     * the array that holds the barrier values */ 
    gasneti_atomic_set(&gasneti_pshm_barrier->named_sum[0], 0, GASNETI_ATOMIC_REL);
    gasneti_atomic_set(&gasneti_pshm_barrier->named_sum[1], 0, GASNETI_ATOMIC_REL);
    gasneti_atomic_set(&gasneti_pshm_barrier->named_num[0], 0, GASNETI_ATOMIC_REL);
    gasneti_atomic_set(&gasneti_pshm_barrier->named_num[1], 0, GASNETI_ATOMIC_REL);
    gasneti_pshm_barrier->named_value[0]=0;
    gasneti_pshm_barrier->named_value[1]=0;

    /* Variables that detects if some node has passed mismatch
     * for the barrier value */
    gasneti_pshm_barrier->mismatch[0]=0;
    gasneti_pshm_barrier->mismatch[1]=0;

    /* Arrays that hold the passed flags and values */
    for(i=0; i<gasneti_nodes; i++){

        gasneti_pshm_barrier->flags[0][i]=0;
        gasneti_pshm_barrier->flags[1][i]=0;

        gasneti_pshm_barrier->value[0][i]=0;
        gasneti_pshm_barrier->value[1][i]=0;

    }
}

#define MISMATCH_FLAG GASNET_BARRIERFLAG_MISMATCH
#define ANON_FLAG GASNET_BARRIERFLAG_ANONYMOUS
#define NAMED_FLAG 0x00

static void gasnete_pshmbarrier_notify(int id, int flags) {

  if(barrier_splitstate == INSIDE_BARRIER) {
    gasneti_fatalerror("gasnet_barrier_notify() called twice in a row");
  } 

  /* Record the passed flag and value */
  gasneti_pshm_barrier->flags[flip][gasneti_mynode]=flags;
  gasneti_pshm_barrier->value[flip][gasneti_mynode]=id;
  
  /* Detect if someone used the mismatch flag */
  if (flags == MISMATCH_FLAG) gasneti_pshm_barrier->mismatch[flip] = 1;

  /* In the named barrier is called, add the passed barrier value to
   * the named_sum variable and increment the counter for named barriers
   * This information is later used to detect if all named barriers 
   * have passed the same value. See barrier_mismatch() function.*/
  if (flags == NAMED_FLAG){
      gasneti_atomic_add(&gasneti_pshm_barrier->named_sum[flip],id,GASNETI_ATOMIC_REL);
      gasneti_atomic_increment(&gasneti_pshm_barrier->named_num[flip], GASNETI_ATOMIC_REL);
      gasneti_pshm_barrier->named_value[flip] = id;
  }

  /* Notify others that I have reached the barrier */
  gasneti_atomic_increment(&gasneti_pshm_barrier->counter[flip], GASNETI_ATOMIC_REL);
  
  barrier_splitstate = INSIDE_BARRIER; 
}

/* At least one node has passed MISMATCH_FLAG to pshmbarrier_notify()
 */
static int barrier_mismatch(void){
    int ret = 0;

    if (gasneti_pshm_barrier->mismatch[flip] == 1){
      ret=1;
    }else if (gasneti_pshm_barrier->named_value[flip]
              *gasneti_atomic_read(&gasneti_pshm_barrier->named_num[flip], GASNETI_ATOMIC_REL) 
              != gasneti_atomic_read(&gasneti_pshm_barrier->named_sum[flip], GASNETI_ATOMIC_REL)){
      ret=1;
    }
    
    return ret; 
}

static inline int finish_barrier(int id, int flags) {
  int ret;
  
  /*at this point the barrier is complete so check the flags 
    that we get and make sure they are the same as the ones
    we pass in*/
  if_pf(flags != gasneti_pshm_barrier->flags[flip][gasneti_mynode]){
    ret = GASNET_ERR_BARRIER_MISMATCH; 
  }else if(id != gasneti_pshm_barrier->value[flip][gasneti_mynode]){
    ret = GASNET_ERR_BARRIER_MISMATCH;
  }else if(barrier_mismatch()) {
    /*someone has signalled a mismatch so return mismatch on everyone*/
    ret = GASNET_ERR_BARRIER_MISMATCH;
  }else{ 
    /*everyone passed same id and flags so the barrier result is good... should be the 
      normal path (could be the anonymous tag but everyone was consistent)*/
    ret = GASNET_OK;
  }

  barrier_splitstate = OUTSIDE_BARRIER;
  return ret;
}

static void reset_barrier(void){
  int i;

  gasneti_atomic_set(&gasneti_pshm_barrier->counter[flip], 0, GASNETI_ATOMIC_REL);
  gasneti_atomic_set(&gasneti_pshm_barrier->done[flip], 0, GASNETI_ATOMIC_REL);
  gasneti_atomic_set(&gasneti_pshm_barrier->named_sum[flip], 0, GASNETI_ATOMIC_REL);
  gasneti_atomic_set(&gasneti_pshm_barrier->named_num[flip], 0, GASNETI_ATOMIC_REL);
  gasneti_pshm_barrier->mismatch[flip]=0;
  gasneti_pshm_barrier->named_value[flip]=0;

  /* We do not need to reset the values of the arrays that hold
   * the flags and values, because they will get new values the
   * next time barrier is called */
  /*
  for(i=0; i<gasneti_nodes; i++){
    gasneti_pshm_barrier->flags[flip][i]=0;
    gasneti_pshm_barrier->value[flip][i]=0;
  }
  */

}

static int gasnete_pshmbarrier_wait(int id, int flags) {
  int ret;
  
  if(barrier_splitstate == OUTSIDE_BARRIER) {
    gasneti_fatalerror("gasnet_barrier_wait() called without a matching notify");
  }

  /* Wait until all nodes have reached the barrier */
  gasneti_waitwhile((gasneti_atomic_read(&gasneti_pshm_barrier->counter[flip], GASNETI_ATOMIC_REL) < gasneti_nodes));
  ret = finish_barrier(id, flags);
  
  /* Detect if I am the last node to leave the barrier, and if so, reset the 
   * barrier values. */
  gasneti_atomic_increment(&gasneti_pshm_barrier->done[flip], GASNETI_ATOMIC_REL);
  if (gasneti_atomic_read(&gasneti_pshm_barrier->done[flip], 0) == gasneti_nodes){
      reset_barrier();
  }
  
  /* Switch the barrier variables used in the next barrier call.*/
  flip ^= 1;
  return ret;
}

static int gasnete_pshmbarrier_try(int id, int flags) { 

  int ret;
  if(barrier_splitstate == OUTSIDE_BARRIER) {
    gasneti_fatalerror("gasnet_barrier_try() called without a matching notify");
  }
  if (gasneti_atomic_read(&gasneti_pshm_barrier->counter[flip], 0) == gasneti_nodes){
    ret = finish_barrier(id,flags);
    /* Detect if I am the last node to leave the barrier, and if so, reset the 
     * barrier values. */
    gasneti_atomic_increment(&gasneti_pshm_barrier->done[flip], GASNETI_ATOMIC_REL);
    if (gasneti_atomic_read(&gasneti_pshm_barrier->done[flip], 0) == gasneti_nodes){
      reset_barrier();
    }

    /* Switch the barrier variables used in the next barrier call.*/
    flip ^= 1;
  }else ret = GASNET_ERR_NOT_READY;
  
  return ret;
}
#endif /* GASNET_PSHM */
/* ------------------------------------------------------------------------------------ */

/* $Source: /Users/kamil/work/gasnet-cvs2/gasnet/other/smp-collectives/smp_coll.c,v $
 * $Date: 2011/11/02 23:20:06 $
 * $Revision: 1.3.6.4 $
 * Description: Shared Memory Collectives
 * Copyright 2009, Rajesh Nishtala <rajeshn@eecs.berkeley.edu>
 * Terms of use are as specified in license.txt
 */

#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#include <inttypes.h>
#include <smp-collectives/smp_coll_internal.h>
#include <smp-collectives/smp_coll_dissem.c>

#if defined(ENABLE_AFFINITY_VIA_SOLARIS)
#include <sys/types.h>
#include <sys/processor.h>
#include <sys/procset.h>
#endif

//#define DEBUG_SMP

static volatile void *shared_buffer[SMP_COLL_MAX_NUM_THREADS]; /**< global buffer for data sharing among threads */
static gasnett_atomic_t shared_atomics[SMP_COLL_MAX_NUM_THREADS]; /**< global atomic vars for synchronization */
static volatile int smp_barrier_phase[SMP_COLL_MAX_NUM_THREADS];

#define BOOTSTRAP_BARRIER(THREADS, FLAGS) smp_coll_barrier_cond_var(THREADS, FLAGS)

/*
 static volatile uint32_t *smp_coll_all_flags;
 static volatile uint32_t *smp_coll_all_barrier_flags;
 static volatile uint32_t *smp_coll_all_bcast_flags;
 static gasnett_atomic_t *atomic_vars;
 */

/* This counting barrier only works when there no concurrent overlapped teams */
static void smp_team_barrier(int team_lead, int THREADS, int MYTHREAD)
{
	gasnett_atomic_t *barrier_counter = &shared_atomics[team_lead];

  gasneti_assert(THREADS > 0);

#ifdef DEBUG_SMP
  printf("enter smp_team_barrier: team_lead %d, THREADS %d, MYTHREAD %d\n",
         team_lead, THREADS, MYTHREAD);
#endif

	if (THREADS == 1)
		return;

  gasneti_waituntil(smp_barrier_phase[team_lead] == 0);

	gasnett_atomic_increment(barrier_counter, GASNETT_ATOMIC_MB_POST);

  /* printf("smp_team_barrier 1: team_lead %d, THREADS %d, MYTHREAD %d, *barrier_counter %d smp_barrier_phase %d\n",  */
  /*        team_lead, THREADS, MYTHREAD, *(int *)barrier_counter, smp_barrier_phase[team_lead]); */
  /* fflush(stdout); */

  if ((int)gasnett_atomic_read(barrier_counter, GASNETT_ATOMIC_NONE) == THREADS) {
    smp_barrier_phase[team_lead] = 1;
  } else {
    gasneti_waituntil(smp_barrier_phase[team_lead] == 1);
  }

  /* printf("smp_team_barrier 2: team_lead %d, THREADS %d, MYTHREAD %d, *barrier_counter %d smp_barrier_phase %d\n",  */
  /*        team_lead, THREADS, MYTHREAD, *(int *)barrier_counter, smp_barrier_phase[team_lead]); */
  /* fflush(stdout); */

	/* exit the barrier after all threads have arrived. */
	gasnett_atomic_decrement(barrier_counter, GASNETT_ATOMIC_MB_POST);
  if ((int)gasnett_atomic_read(barrier_counter, GASNETT_ATOMIC_NONE) == 0) {
    smp_barrier_phase[team_lead] = 0;
  }

  /* printf("smp_team_barrier 3: team_lead %d, THREADS %d, MYTHREAD %d, *barrier_counter %d smp_barrier_phase %d\n",  */
  /*        team_lead, THREADS, MYTHREAD, *(int *)barrier_counter, smp_barrier_phase[team_lead]); */
  /* fflush(stdout); */

#ifdef DEBUG_SMP
  printf("exit smp_team_barrier: team_lead %d, THREADS %d, MYTHREAD %d\n",
         team_lead, THREADS, MYTHREAD);
#endif
}

void smp_coll_set_affinity(int location)
{
#if defined(ENABLE_AFFINITY_VIA_SOLARIS)
	if(processor_bind(P_LWPID, P_MYID, location, NULL)<0) {
		fprintf(stderr, "WARNING: Couldn't bind thread %d\n",location);
	}
#else
	gasnett_set_affinity(location);
#endif
}

void smp_coll_reset_all_flags(smp_coll_t handle)
{
	int i;
	//BOOTSTRAP_BARRIER(handle, 0);
	smp_team_barrier(handle->team_lead, handle->THREADS, handle->MYTHREAD);
	for (i = 0; i < SMP_COLL_CACHE_LINE; i++) {
		SMP_COLL_SET_FLAG(handle, handle->MYTHREAD, i, 0);
		SMP_COLL_SET_BARRIER_FLAG(handle, handle->MYTHREAD, i, 0);
		SMP_COLL_SET_BCAST_FLAG(handle, handle->MYTHREAD, i, 0);
		gasnett_atomic_set(&handle->atomic_vars[handle->MYTHREAD*SMP_COLL_CACHE_LINE+i],
				0, GASNETT_ATOMIC_MB_POST);
		gasnett_atomic_set(&handle->atomic_vars[handle->THREADS*SMP_COLL_CACHE_LINE+handle->MYTHREAD*SMP_COLL_CACHE_LINE+i],
				0, GASNETT_ATOMIC_MB_POST);
	}
	//BOOTSTRAP_BARRIER(handle, 0);
	smp_team_barrier(handle->team_lead, handle->THREADS, handle->MYTHREAD);
}

void smp_coll_fini(smp_coll_t handle)
{
	smp_coll_team_fini(handle);
}

/* initialize the smp collective component and the default team */
smp_coll_t smp_coll_init(size_t aux_space_per_thread, int flags, int THREADS,
		int MYTHREAD)
{
	gasneti_assert(THREADS <= SMP_COLL_MAX_NUM_THREADS);
	gasneti_assert(MYTHREAD < THREADS && MYTHREAD >= 0);
	/* Initialize my part of the global shared data */
  gasnett_atomic_set(&shared_atomics[MYTHREAD], 0, GASNETT_ATOMIC_MB_POST);
  smp_barrier_phase[MYTHREAD] = 0;
	shared_buffer[MYTHREAD] = NULL;

	BOOTSTRAP_BARRIER(THREADS,0);

  return smp_coll_team_init(aux_space_per_thread, flags, THREADS, MYTHREAD, 0);
}

/*
 * \param team_lead the global thread id (as in TEAM_ALL) of the team lead
 */
smp_coll_t smp_coll_team_init(size_t aux_space_per_thread, int flags,
		int THREADS, int MYTHREAD, int team_lead)
{
	uint8_t **allscratch;
	smp_coll_t ret, team_lead_handle;

#ifdef DEBUG_SMP
  fprintf(stderr, "smp_coll_team_init: THREADS %d, MYTHREAD %d, team_lead %d\n",
          THREADS, MYTHREAD, team_lead);
#endif

  if (MYTHREAD >= THREADS || team_lead >= SMP_COLL_MAX_NUM_THREADS) {
    gasneti_fatalerror("smp_coll_team_init error: MYTHREAD %d, THREADS %d, team_lead %d, SMP_COLL_MAX_NUM_THREADS %d\n",
                       MYTHREAD, THREADS, team_lead, SMP_COLL_MAX_NUM_THREADS);
  }

	ret = (struct smp_coll_t_*) gasneti_malloc(sizeof(struct smp_coll_t_));
	ret->MYTHREAD = MYTHREAD;
	ret->THREADS = THREADS;
	ret->team_lead = team_lead;
	ret->flag_set = 0;
	ret->tempaddrs = (void**) gasneti_malloc(sizeof(void*) * THREADS);

	if (flags & SMP_COLL_SET_AFFINITY) {
		smp_coll_set_affinity(MYTHREAD);
	}
	smp_team_barrier(team_lead, THREADS, MYTHREAD);

	SMP_COLL_CONSTRUCT_BARR_ROUTINES(ret);
#if 0
	SMP_COLL_CONSTRUCT_BCAST_ROUTINES(ret);
	SMP_COLL_CONSTRUCT_SCATTER_ROUTINES(ret);
	SMP_COLL_CONSTRUCT_GATHER_ROUTINES(ret);
	SMP_COLL_CONSTRUCT_ALLRED_INT_ROUTINES(ret);
	SMP_COLL_CONSTRUCT_ALLRED_DOUBLE_ROUTINES(ret);
	SMP_COLL_CONSTRUCT_RED_INT_ROUTINES(ret);
	SMP_COLL_CONSTRUCT_RED_DOUBLE_ROUTINES(ret);
	SMP_COLL_CONSTRUCT_EXCHANGE_ROUTINES(ret);
#endif

  smp_team_barrier(team_lead, THREADS, MYTHREAD);

	if (MYTHREAD == 0) {
		/* Each thread has its own cache line. The extra +1 cache line is
		 for cache line alignment for the start address. Need to make
		 sure a cache line has enough space. */
		ret->all_flags = (volatile uint32_t*) gasneti_malloc(
				SMP_COLL_CACHE_LINE * (THREADS * sizeof(uint32_t) + 1));
		ret->all_barrier_flags = (volatile uint32_t*) gasneti_malloc(
				SMP_COLL_CACHE_LINE * (THREADS * sizeof(uint32_t) + 1));
		ret->all_bcast_flags = (volatile uint32_t*) gasneti_malloc(
				SMP_COLL_CACHE_LINE * (THREADS * sizeof(uint32_t) + 1));
		ret->atomic_vars = (gasnett_atomic_t*) gasneti_malloc(
				2 * SMP_COLL_CACHE_LINE * THREADS * sizeof(gasnett_atomic_t)
						+SMP_COLL_CACHE_LINE);
		ret->aux_space_all = gasneti_malloc(sizeof(uint8_t*) * THREADS);

#if HAVE_PTHREAD_BARRIER
		if (MYTHREAD==0) {
			pthread_barrier_t *ptr;
			ptr = (pthread_barrier_t *) gasneti_malloc(sizeof(pthread_barrier_t));
			pthread_barrier_init(ptr, NULL, THREADS);
			ret->pthread_barrier = ptr;
		}
#endif

		shared_buffer[team_lead] = (void *) ret;
	}

	// BOOTSTRAP_BARRIER(ret,0);
	smp_team_barrier(team_lead, THREADS, MYTHREAD);

	team_lead_handle = (smp_coll_t) shared_buffer[team_lead];
	if (MYTHREAD != 0) {
		ret->aux_space_all = team_lead_handle->aux_space_all;
	}
	ret->aux_space = (uint8_t*) gasneti_malloc(
			sizeof(uint8_t) * SMP_COLL_AUX_SPACE_SIZE);
	ret->aux_space_all[MYTHREAD] = ret->aux_space;

	smp_team_barrier(team_lead, THREADS, MYTHREAD);

	ret->flag_set = 0;
	ret->barrier_flag_set = 0;
	ret->curr_atomic_set = 0;
	ret->dissem_info = NULL;
	ret->flags
			= (volatile uint32_t*) ALIGNUP(team_lead_handle->all_flags, SMP_COLL_CACHE_LINE);
	ret->barrier_flags
			= (volatile uint32_t*) ALIGNUP(team_lead_handle->all_barrier_flags, SMP_COLL_CACHE_LINE);
	ret->bcast_flags
			= (volatile uint32_t*) ALIGNUP(team_lead_handle->all_bcast_flags, SMP_COLL_CACHE_LINE);
	ret->atomic_vars
			= (gasnett_atomic_t*) ALIGNUP(team_lead_handle->atomic_vars, SMP_COLL_CACHE_LINE);

#if HAVE_PTHREAD_BARRIER
	if (MYTHREAD != 0) {
		ret->pthread_barrier = team_lead_handle->pthread_barrier;
	}
	{
		int temp = pthread_barrier_wait(ret->pthread_barrier);
		if (temp!=0 && temp!=PTHREAD_BARRIER_SERIAL_THREAD) {
			fprintf(stderr, "barrier error (%d) %d %d\n", temp, EINVAL, PTHREAD_BARRIER_SERIAL_THREAD);
			exit(1);
		}
	}
#endif

  // smp_coll_reset_all_flags(ret);

	if (!(flags & SMP_COLL_SKIP_TUNE_BARRIERS)) {
		smp_coll_tune_barrier(ret);
	} else {
		smp_coll_set_barrier_routine(ret, SMP_COLL_BARRIER_TREE_PUSH_PULL, 4);
	}

#if 0
	smp_coll_set_broadcast_routine(ret, SMP_COLL_BROADCAST_TREE_FLAG, 2);
	smp_coll_set_all_reduce_int_routine(ret, SMP_COLL_ALL_REDUCE_FLAT, THREADS);
	smp_coll_set_all_reduce_double_routine(ret, SMP_COLL_ALL_REDUCE_FLAT, THREADS);
	smp_coll_set_exchange_routine(ret, SMP_COLL_EXCHANGE_FLAT, THREADS);
#endif

	smp_team_barrier(team_lead, THREADS, MYTHREAD);
	smp_coll_reset_all_flags(ret);
	return ret;
}

void smp_coll_team_fini(smp_coll_t handle)
{
	gasneti_assert(handle != NULL);
	gasneti_free(handle->aux_space);
	gasneti_free(handle->tempaddrs);

	if (handle->MYTHREAD == 0) {
		gasneti_free((void *) handle->all_flags);
		gasneti_free((void *) handle->all_barrier_flags);
		gasneti_free((void *) handle->all_bcast_flags);
		gasneti_free((void *) handle->atomic_vars);
		gasneti_free(handle->aux_space_all);
	}

#if HAVE_PTHREAD_BARRIER
	gasneti_free(handle->pthread_barrier);
#endif

	gasneti_free(handle);
}

void smp_coll_safe_barrier(smp_coll_t handle, int flags)
{
	int i, j;
	// BOOTSTRAP_BARRIER(handle, flags);
	smp_team_barrier(handle->team_lead, handle->THREADS, handle->MYTHREAD);
	if (handle->MYTHREAD == 0) {
		for (i = 0; i < handle->THREADS; i++) {
			for (j = 0; j < SMP_COLL_CACHE_LINE; j++) {
				SMP_COLL_SET_FLAG(handle, i, j, 0);
			}
		}
	}
	// BOOTSTRAP_BARRIER(handle, flags);
	smp_team_barrier(handle->team_lead, handle->THREADS, handle->MYTHREAD);
}


/* $Id: testcas.c,v 1.5 2004/10/19 04:41:57 bonachea Exp $
 *
 * Description: GASNet atomic CAS.
 *   The test verifies the atomic compare-and-swap on platforms which support it.
 *
 * Terms of use are as specified in license.txt
 */

#include "gasnet.h"
#include "gasnet_tools.h"
#include <stdio.h>
#include <pthread.h>

#include "gasnet_internal.h"	/* EVIL, but only way to test internal stuff */

/* more crap required to make the evil hack above function reliably */
#undef malloc
#undef free
#undef assert
#include <assert.h>
#ifndef assert
  static void _my_assert_fail(const char *expr, const char *file, int line) {
    fprintf(stderr,"ASSERTION FAILURE: %s at %s:%i\n", expr, file, line); fflush(stderr);
    abort();
  }
  #define assert(x) ((x)?((void)0):_my_assert_fail(#x,__FILE__,__LINE__))
#endif
#include "test.h" 

#ifndef GASNET_PAR
#error This test can only be built for GASNet PAR configuration
#endif

/* configurable parameters */
#define DEFAULT_ITERS 1000
#define DEFAULT_THREADS 2

#if defined(GASNETI_HAVE_ATOMIC_CAS)
static pthread_t *tt_tids;
static uint32_t counter1 = 0;
static gasneti_atomic_t counter2 = gasneti_atomic_init(0);
static int iters = DEFAULT_ITERS;
static int threads = DEFAULT_THREADS;

static gasneti_atomic_t my_lock = gasneti_atomic_init(0);

static void *
threadmain(void *args)
{
    int i;
    uint32_t oldval;

    /* First test the spinlock which may or may not be CAS based */
    #ifdef GASNETI_HAVE_SPINLOCK
      PTHREAD_LOCALBARRIER(threads);	/* increase likelyhood of contention */
      for (i = 0; i < iters; ++i) {
	  gasneti_spinlock_lock(&my_lock);
	  ++counter1;
	  gasneti_spinlock_unlock(&my_lock);
      }
    #else
      counter1 = threads * iters;
    #endif

    /* Now test a CAS based atomic increment implementation */
    PTHREAD_LOCALBARRIER(threads);	/* increase likelyhood of contention */
    while ((oldval = gasneti_atomic_read(&counter2)) != counter1) {
	gasneti_atomic_compare_and_swap(&counter2, oldval, (oldval + 1));
    }
    
    return NULL;
}

int
main(int argc, char **argv)
{
	uint32_t want;
	int i;
	gasnet_node_t mynode;
	void * myseg;

	GASNET_Safe(gasnet_init(&argc, &argv));
    	GASNET_Safe(gasnet_attach(NULL, 0, TEST_SEGSZ, TEST_MINHEAPOFFSET));

	MSG("running...");

	mynode = gasnet_mynode();
	myseg = TEST_MYSEG();

	if (argc > 1) iters = atoi(argv[1]);
	if (!iters) iters = DEFAULT_ITERS;

	if (argc > 2) threads = atoi(argv[2]);
	if (!iters) threads = DEFAULT_THREADS;

	MSG("Running parallel compare-and-swap test with %d iterations", iters);
	MSG("Forking %d gasnet threads", threads);
	tt_tids = (pthread_t *) test_malloc(sizeof(pthread_t) * threads);
	for (i = 0; i < threads; i++) {
		int err;
	
		pthread_attr_t attr;
		pthread_attr_init(&attr);
		pthread_attr_setscope(&attr, PTHREAD_SCOPE_SYSTEM);

		if ((err = pthread_create(&tt_tids[i], &attr, threadmain, NULL)) != 0) {
			printf("Error %d forking threads\n", err);
			exit(EXIT_FAILURE);
		}
	}

	for (i = 0; i < threads; i++) {
		void *ret;
		int err;

		if ((err = pthread_join(tt_tids[i], &ret)) != 0) {
			printf("Error %d joining threads\n", err);
			exit(EXIT_FAILURE);
		}
	}

	want = (threads * iters);
	if (counter1 != want) {
		MSG("*** ERROR: incorrect counter1 value (got %d when expecting %d)\n", (unsigned int)counter1, (unsigned int)want);
		gasnet_exit(1);
	}
	if (gasneti_atomic_read(&counter2) != want) {
		MSG("*** ERROR: incorrect counter2 value (got %u when expecting %u)\n", (unsigned int)gasneti_atomic_read(&counter2), (unsigned int)want);
		gasnet_exit(1);
	}

        BARRIER();

	test_free(tt_tids);

	MSG("Tests complete");

        BARRIER();

	gasnet_exit(0);

	return 0;
}
#else
int
main(int argc, char **argv)
{
	printf("The platform does not support atomic CAS.  Test passes trivially.\n");
	return 0;
}
#endif

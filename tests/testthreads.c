/* $Id: testthreads.c,v 1.3 2003/08/25 19:02:16 phargrov Exp $
 *
 * Description: GASNet threaded tester.
 *   The test initializes GASNet and forks off up to 256 threads.  Each of
 *   these threads randomly chooses from a set of communication operations for
 *   a given amount of iterations.  The idea is to detect race errors by having
 *   many threads concurrently use different GASNet communication operations.
 *
 * Copyright 2003, Christian Bell <csbell@cs.berkeley.edu>
 * Terms of use are as specified in license.txt
 */

#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <sys/time.h>
#include <unistd.h>
#include <fcntl.h>
#include <pthread.h>
#include <getopt.h>

#include "gasnet.h"
#include "gasnet_tools.h"
#include "test.h"

#define CACHE_LINE_BYTES	(128)
#define AM_TRACE		1	/* set to 1 to print in AM handlers */

typedef 
struct _threaddata_t {
	int	tid;		/* global thread id */
	int	tid_local;	/* local thread it */

	int	tid_peer;	/* id of remote peer thread */
	int	tid_local_peer; /* id of local peer thread */

	volatile int	flag;
	char	_pad[CACHE_LINE_BYTES-5*sizeof(int)];
} 
threaddata_t;

typedef void (*testfunc_t)(threaddata_t *);
typedef gasnet_handlerarg_t harg_t;

/* configurable parameters */
int	iters = 50;
int	sleep_min_us = 1;
int	sleep_max_us = 250000;
int	amiters_max = 50;
int     verbose = 0;

#define ACTION_PRINTF  if (verbose) printf

int	sizes[] = { 1, 9, 128, 256, 1024, 2048, 16384, 30326 };
#define	SIZES_NUM	(sizeof(sizes)/sizeof(int))
#define RANDOM_SIZE()	(sizes[ (rand() % SIZES_NUM)])

int		AM_loopback = 0;
int		threads_num;
pthread_t	*tt_tids;
gasnet_node_t	*tt_thread_map;
void		**tt_addr_map;
threaddata_t	*tt_thread_data;

void	alloc_thread_data(int threads);
void	free_thread_data();
void	thread_barrier();
void *	threadmain(void *args);

/* GASNet Test functions */
void	test_sleep(threaddata_t *tdata);
void	test_put(threaddata_t *tdata);
void	test_get(threaddata_t *tdata);
void	test_amshort(threaddata_t *tdata);
void	test_ammedium(threaddata_t *tdata);
void	test_amlong(threaddata_t *tdata);

testfunc_t	test_functions_all[] = {
	test_sleep, test_put, test_get, test_amshort, test_ammedium, test_amlong
};

#define NUM_FUNCTIONS	(sizeof(test_functions_all)/sizeof(testfunc_t))
/* This array remains uninitialized */
testfunc_t	test_functions[NUM_FUNCTIONS] = { 0 };
static int	functions_num = 0;

/* AM Handlers */
void	ping_shorthandler(gasnet_token_t token, harg_t tid);
void 	pong_shorthandler(gasnet_token_t token, harg_t tid);

void	ping_medhandler(gasnet_token_t token, void *buf, size_t nbytes, 
		harg_t tid);
void	pong_medhandler(gasnet_token_t token, void *buf, size_t nbytes, 
		harg_t tid);

void	ping_longhandler(gasnet_token_t token, void *buf, size_t nbytes,
		harg_t tid);
void	pong_longhandler(gasnet_token_t token, void *buf, size_t nbytes, 
		harg_t tid);

#define hidx_ping_shorthandler   201
#define hidx_pong_shorthandler   202
#define hidx_ping_medhandler     203
#define hidx_pong_medhandler     204
#define hidx_ping_longhandler    205
#define hidx_pong_longhandler    206

gasnet_handlerentry_t htable[] = { 
	{ hidx_ping_shorthandler,  ping_shorthandler  },
	{ hidx_pong_shorthandler,  pong_shorthandler  },
	{ hidx_ping_medhandler,    ping_medhandler    },
	{ hidx_pong_medhandler,    pong_medhandler    },
	{ hidx_ping_longhandler,   ping_longhandler   },
	{ hidx_pong_longhandler,   pong_longhandler   },
};
#define HANDLER_TABLE_SIZE (sizeof(htable)/sizeof(gasnet_handlerentry_t))

void
usage(char *progname)
{
	printf("usage: %s [ -pgmlv ] [ -i <iters> ] <num_threads>\n\n", progname);
	printf("<num_threads> must be between 1 and 256       \n");
	printf("no options means -pgml                        \n");
	printf("options:                                      \n");
	printf("  -p  use puts                                   \n");
	printf("  -g  use puts                                   \n");
	printf("  -m  use Active Messages                        \n");
	printf("  -l  use local Active Messages                  \n");
	printf("  -v  output information about actions taken     \n");
	printf("  -i <iters> use <iters> iterations per thread   \n\n");

	exit(EXIT_FAILURE);
}

int
main(int argc, char **argv)
{
	int 		threads = 1;
	int		i;
	pthread_t	*tids;

	while ((i = getopt (argc, argv, "pgmlvi:")) != EOF) {
		switch (i) {

		case 'p':
			test_functions[functions_num++] = test_put;
			break;
		case 'g':
			test_functions[functions_num++] = test_get;
			break;
		case 'm':
			test_functions[functions_num++] = test_amshort;
			test_functions[functions_num++] = test_ammedium;
			test_functions[functions_num++] = test_amlong;
			break;
		case 'l':
			AM_loopback = 1;
			break;

		case 'i':
			iters = atoi(optarg);
			break;

                case 'v':
                        verbose = 1;
                        break;

		default:
			usage(argv[0]);
		}
	}

	/* Assume all test functions if no option is passed */
	if (functions_num  == 0) {
		printf(" running all functions!\n");
		
		memcpy(test_functions, test_functions_all, 
				sizeof(test_functions_all));
		functions_num = NUM_FUNCTIONS;
	}

	argc -= optind;

	if (argc != 1)
		usage(argv[0]);
	else {
		argv += optind;
		threads_num = threads = atoi(argv[0]);
	}

	if (threads > 256 || threads < 1) {
		printf("Threads must be between 1 and 256\n");
		exit(EXIT_FAILURE);
	}

	GASNET_Safe(gasnet_init(&argc, &argv));
    	GASNET_Safe(gasnet_attach(htable, HANDLER_TABLE_SIZE,
		    threads*TEST_SEGSZ, TEST_MINHEAPOFFSET));

	alloc_thread_data(threads);

	{
		int 	i;
		void	*ret;

		printf("%d> Forking %d gasnet threads\n", gasnet_mynode(), 
				threads);
		for (i = 0; i < threads; i++) {

			if (pthread_create(&tt_tids[i], NULL, threadmain, 
					(void *) &tt_thread_data[i]) != 0) {
				printf("Error forking threads\n");
				exit(EXIT_FAILURE);
			}
		}

		for (i = 0; i < threads; i++) {
			if (pthread_join(tt_tids[i], &ret) != 0) {
				printf("Error joining threads\n");
				exit(EXIT_FAILURE);
			}
		}
	}

	free_thread_data();

	printf("%d> Tests complete\n", gasnet_mynode());

	return 0;
}

void *
threadmain(void *args)
{
	int	i, idx;

	testfunc_t	func;
	threaddata_t	*td = (threaddata_t *) args;

	srand((unsigned int) time(0) * td->tid);

	thread_barrier();

	printf("tid=%3d> starting.\n", td->tid);

	for (i = 0; i < iters; i++) {
		idx = rand() % functions_num;
		func = test_functions[idx];
		assert(func != NULL);

		func(td);
	}

	thread_barrier();
	printf("tid=%3d> done.\n", td->tid);

	return NULL;
}

void
alloc_thread_data(int threads)
{
	int	nodes, tot_threads;

	nodes = gasnet_nodes();
	tot_threads = nodes * threads;

	tt_tids = (pthread_t *) malloc(sizeof(pthread_t) * threads);
	if (tt_tids == NULL) {
		printf("couldn't allocate thread id array\n");
		exit(EXIT_FAILURE);
	}

	tt_thread_map = (gasnet_node_t *) 
		malloc(sizeof(gasnet_node_t) * tot_threads);
	if (tt_thread_map == NULL) {
		printf("could't allocate thread mapping array\n");
		exit(EXIT_FAILURE);
	}

	tt_thread_data = (threaddata_t *) 
		malloc(sizeof(threaddata_t) * threads);
	if (tt_thread_data == NULL) {
		printf("could't allocate thread data array\n");
		exit(EXIT_FAILURE);
	}

	tt_addr_map = (void **)
		malloc(sizeof(void *) * tot_threads);

	if (tt_addr_map == NULL) {
		printf("could't allocate thread address array\n");
		exit(EXIT_FAILURE);
	}

	/* Initialize the thread to node map array and local thread data */
	{
		int 	i, j, tid, base;
		void	*segbase;

		threaddata_t	*td;
		for (i = 0; i < nodes; i++) {
			segbase = TEST_SEG(i);

			base = i * threads;
			for (j = 0; j < threads; j++) {
				tid = base + j;
				tt_thread_map[tid] = i;
				tt_addr_map[tid] = (void *) 
				    ((uintptr_t) segbase + j * TEST_SEGSZ);

				if (i == gasnet_mynode()) {
					td = &tt_thread_data[j];

					td->tid = tid;
					td->tid_local = j;
					td->tid_local_peer = (tid+1) % threads;
					td->tid_peer = (tid+threads) % 
							tot_threads;
				}
			}
		}
	}
}


void
free_thread_data()
{
	free(tt_tids);
	free(tt_thread_map);
	free(tt_addr_map);
	free(tt_thread_data);
}

/* Cheap (but functional!) pthread + gasnet barrier */
static pthread_mutex_t	barrier_mutex = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t	barrier_cond = PTHREAD_COND_INITIALIZER;
static volatile int	barrier_count = 0;

void
thread_barrier() {
        pthread_mutex_lock(&barrier_mutex);
        barrier_count++;
        if (barrier_count < threads_num)
                pthread_cond_wait(&barrier_cond, &barrier_mutex);
        else {  
		/* Now do the gasnet barrier */
		BARRIER();
                barrier_count = 0;
                pthread_cond_broadcast(&barrier_cond);
        }       
        pthread_mutex_unlock(&barrier_mutex);
}

/****************************************************************/
/* AM Handlers */
#if AM_TRACE
#define PRINT_AM(x)	ACTION_PRINTF x
#else
#define PRINT_AM(x)
#endif

void 
ping_shorthandler(gasnet_token_t token, harg_t idx) 
{
	gasnet_node_t	node;
	gasnet_AMGetMsgSource(token, &node);

	PRINT_AM(("node=%2d> AMShort Request for (%d,%d)\n", 
			gasnet_mynode(), node, idx));
	GASNET_Safe(gasnet_AMReplyShort1(token, hidx_pong_shorthandler, idx));
}

void 
pong_shorthandler(gasnet_token_t token, harg_t idx) 
{
	int	tid = tt_thread_data[idx].tid;
	PRINT_AM(("node=%2d> AMShort Reply for tid=%d, (%d,%d)\n", 
			gasnet_mynode(), tid, gasnet_mynode(), idx));
	tt_thread_data[idx].flag++;
}

void 
ping_medhandler(gasnet_token_t token, void *buf, size_t nbytes, harg_t idx) 
{
	gasnet_node_t	node;
	gasnet_AMGetMsgSource(token, &node);

	PRINT_AM(("node=%2d> AMMedium Request for (%d,%d)\n", 
			gasnet_mynode(), node, idx));
	GASNET_Safe(
		gasnet_AMReplyMedium1(token, hidx_pong_medhandler, 
			buf, nbytes, idx));
}
void 
pong_medhandler(gasnet_token_t token, void *buf, size_t nbytes, 
		gasnet_handlerarg_t idx) 
{
	int	tid = tt_thread_data[idx].tid;

	PRINT_AM(("node=%2d> AMMedium Reply for tid=%d, (%d,%d)\n", 
			gasnet_mynode(), tid, gasnet_mynode(), idx));
	tt_thread_data[idx].flag++;
}

void 
ping_longhandler(gasnet_token_t token, void *buf, size_t nbytes, harg_t idx) 
{
	int		tid;
	void		*paddr;
	gasnet_node_t	node;

	gasnet_AMGetMsgSource(token, &node);
	tid = node * threads_num + idx;
	paddr = tt_addr_map[tid];

	PRINT_AM(("node=%2d> AMLong Request for (%d,%d)\n", 
			gasnet_mynode(), node, idx));
	GASNET_Safe(
		gasnet_AMReplyLong1(token, hidx_pong_longhandler, 
			buf, nbytes, paddr, idx));
}

void 
pong_longhandler(gasnet_token_t token, void *buf, size_t nbytes, harg_t idx) {
	int	tid = tt_thread_data[idx].tid;

	PRINT_AM(("node=%2d> AMLong Reply for tid=%d, (%d,%d)\n", 
			gasnet_mynode(), tid, gasnet_mynode(), idx));
	tt_thread_data[idx].flag++;
}

/****************************************************************/
/* GASNet testers */

void
test_sleep(threaddata_t *tdata)
{
	unsigned	usecs = (unsigned) sleep_min_us + 
				(rand() % (sleep_max_us - sleep_min_us));
	ACTION_PRINTF("tid=%3d> sleeping %.3f millisecs\n", tdata->tid, usecs/1000.0);
        { uint64_t goal = gasnett_ticks_to_us(gasnett_ticks_now()) + usecs;
          while (gasnett_ticks_to_us(gasnett_ticks_now()) < goal) 
            gasnett_sched_yield();
        }
	ACTION_PRINTF("tid=%3d> awaking\n", tdata->tid);
}

void
test_put(threaddata_t *tdata)
{
	int	peer = tdata->tid_peer;
	int	node = tt_thread_map[peer];
	void	*laddr = tt_addr_map[tdata->tid];
	void	*raddr = tt_addr_map[peer];
	int	 len = RANDOM_SIZE();

	ACTION_PRINTF("tid=%3d> put (%p,%8d) -> tid=%3d,node=%d,addr=%p\n",
			tdata->tid, laddr, len, peer, node, raddr);

	gasnet_put(node, raddr, laddr, len);
}

void
test_get(threaddata_t *tdata)
{
	int	peer = tdata->tid_peer;
	int	node = tt_thread_map[peer];
	void	*laddr = tt_addr_map[tdata->tid];
	void	*raddr = tt_addr_map[peer];
	int	 len = RANDOM_SIZE();

	ACTION_PRINTF("tid=%3d> get (%p,%8d) <- tid=%3d,node=%d,addr=%p\n",
			tdata->tid, laddr, len, peer, node, raddr);

	gasnet_get(raddr, node, laddr, len);
}

#define RANDOM_PEER(tdata)					\
	(AM_loopback ? 						\
		(rand() % 2 == 0 ? tdata->tid_peer		\
				 : tdata->tid_local_peer)	\
	: tdata->tid_peer);

void
test_amshort(threaddata_t *tdata)
{
	int 	 	peer = RANDOM_PEER(tdata);
	int		node = tt_thread_map[peer];

	ACTION_PRINTF("tid=%3d> AMShortRequest to tid=%3d\n", tdata->tid, peer);
	tdata->flag = -1;
        gasnett_local_membar();
	GASNET_Safe(gasnet_AMRequestShort1(node, 
		    hidx_ping_shorthandler, tdata->tid_local));
	GASNET_BLOCKUNTIL(tdata->flag == 0);
	tdata->flag = -1;

	ACTION_PRINTF("tid=%3d> AMShortRequest to tid=%3d complete.\n", tdata->tid, peer);
}

void
test_ammedium(threaddata_t *tdata)
{
	int 	 	peer = RANDOM_PEER(tdata);
	int		node = tt_thread_map[peer];
	void		*laddr = tt_addr_map[tdata->tid];
	size_t	 	len;

	do {
		len = RANDOM_SIZE();
	} while (len > gasnet_AMMaxMedium());
		
	ACTION_PRINTF("tid=%3d> AMMediumRequest to tid=%3d\n", tdata->tid, peer);
	tdata->flag = -1;
        gasnett_local_membar();
	GASNET_Safe(gasnet_AMRequestMedium1(node, 
		    hidx_ping_medhandler, laddr, len, 
		    tdata->tid_local));
	GASNET_BLOCKUNTIL(tdata->flag == 0);
	tdata->flag = -1;

	ACTION_PRINTF("tid=%3d> AMMediumRequest to tid=%3d complete.\n", tdata->tid, peer);
}


void
test_amlong(threaddata_t *tdata)
{
	int 	 	peer = RANDOM_PEER(tdata);
	int		node = tt_thread_map[peer];
	void		*laddr = tt_addr_map[tdata->tid];
	void		*raddr = tt_addr_map[peer];
	size_t	 	len;

	do {
		len = RANDOM_SIZE();
	} while ((len > gasnet_AMMaxLongRequest()) || (len > gasnet_AMMaxLongReply()));
		
	tdata->flag = -1;
        gasnett_local_membar();
	ACTION_PRINTF("tid=%3d> AMLongRequest to tid=%3d\n", tdata->tid, peer);

	ACTION_PRINTF("%d> parameters: %d, %d, %p, %d, %p, %d\n",
		gasnet_mynode(), node, hidx_ping_longhandler, laddr, len, raddr, tdata->tid_local);

	GASNET_Safe(gasnet_AMRequestLong1(node, 
		    hidx_ping_longhandler, laddr, len, raddr, 
		    tdata->tid_local));
	GASNET_BLOCKUNTIL(tdata->flag == 0);
	tdata->flag = -1;

	ACTION_PRINTF("tid=%3d> AMLongRequest to tid=%3d complete.\n", tdata->tid, peer);
}


/*  $Archive:: /Ti/GASNet/tests/testcoll.c                                 $
 *     $Date: 2004/06/25 20:04:24 $
 * $Revision: 1.2 $
 * Description: GASNet collectives test
 * Copyright 2002-2004, Jaein Jeong and Dan Bonachea <bonachea@cs.berkeley.edu>
 * Terms of use are as specified in license.txt
 */

#include "gasnet.h"
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <sys/time.h>
#include <unistd.h>
#include <fcntl.h>
#include "test.h"

#define DEFAULT_SZ	(32*1024)

#define PRINT_LATENCY 0
#define PRINT_THROUGHPUT 1

#ifndef GASNET_ALIGNED_SEGMENTS
 #error "This test requires aligned segments"
#endif

int myproc;
int numprocs;
int peerproc;

int *segment;

typedef struct {
	int datasize;
	int iters;
	uint64_t time;
} stat_struct_t;


#define init_stat \
  GASNETT_TRACE_SETSOURCELINE(__FILE__,__LINE__), _init_stat
#define update_stat \
  GASNETT_TRACE_SETSOURCELINE(__FILE__,__LINE__), _update_stat
#define print_stat \
  GASNETT_TRACE_SETSOURCELINE(__FILE__,__LINE__), _print_stat

void _init_stat(stat_struct_t *st, int sz)
{
	st->iters = 0;
	st->datasize = sz;
	st->time = 0;
}

void _update_stat(stat_struct_t *st, uint64_t temptime, int iters)
{
	st->iters += iters;
	st->time += temptime;
} 

void _print_stat(int myproc, stat_struct_t *st, const char *name, int operation)
{
	switch (operation) {
	case PRINT_LATENCY:
		printf("Proc %2i - %4i byte : %7i iters,"
			   " latency %10i us total, %9.3f us ave. (%s)\n",
			myproc, st->datasize, st->iters, (int) st->time,
			((float)st->time) / st->iters,
			name);
		fflush(stdout);
		break;
	case PRINT_THROUGHPUT:
		printf("Proc %2i - %4i byte : %7i iters,"
#if 0
			" throughput %9.3f KB/sec (%s)\n"
#else
			" inv. throughput %9.3f us (%s)\n"
#endif
                        ,
			myproc, st->datasize, st->iters,
#if 0
			((int)st->time == 0 ? 0.0 :
                        (1000000.0 * st->datasize * st->iters / 1024.0) / ((int)st->time)),
#else
                        (((float)((int)st->time)) / st->iters),
#endif
			name);
		fflush(stdout);
		break;
	default:
		break;
	}
}


void ALL_ALL_test(int iters, int nbytes)
{GASNET_BEGIN_FUNCTION();
    int i;
    int64_t begin, end;
    stat_struct_t st;
    gasnet_coll_handle_t *handles;

	int iamsender = (myproc == 0);
	int iamreceiver = !iamsender;

        handles = (gasnet_coll_handle_t*) test_malloc(sizeof(gasnet_coll_handle_t) * iters);

	/* initialize statistics */
	init_stat(&st, nbytes);
	
	BARRIER();
	
	begin = TIME();
	for (i = 0; i < iters; i++) {
		/* XXX: fix src/dst overlap */
		gasnet_coll_broadcast(GASNET_TEAM_ALL, segment, 0, segment, nbytes,
					GASNET_COLL_SINGLE |
					GASNET_COLL_IN_ALLSYNC |
					GASNET_COLL_OUT_ALLSYNC |
					GASNET_COLL_SRC_IN_SEGMENT |
					GASNET_COLL_DST_IN_SEGMENT);
	}
	end = TIME();
 	update_stat(&st, (end - begin), iters);
	
	BARRIER();
	
	if (iamsender) {
		print_stat(myproc, &st, "broadcast(ALL,ALL) latency", PRINT_LATENCY);
	}	

	/* initialize statistics */
	init_stat(&st, nbytes);


	BARRIER();
	
	begin = TIME();
	for (i = 0; i < iters; i++) {
		/* XXX: fix src/dst overlap */
		handles[i] = 
			gasnet_coll_broadcast_nb(GASNET_TEAM_ALL, segment, 0, segment, nbytes,
						GASNET_COLL_SINGLE |
						GASNET_COLL_IN_ALLSYNC |
						GASNET_COLL_OUT_ALLSYNC |
						GASNET_COLL_SRC_IN_SEGMENT |
						GASNET_COLL_DST_IN_SEGMENT);
	}
	gasnet_coll_wait_sync_all(handles, iters);
	BARRIER();
	end = TIME();
 	update_stat(&st, (end - begin), iters);
	
	
	if (iamsender) {
		print_stat(myproc, &st, "broadcast_nb(ALL,ALL) throughput", PRINT_THROUGHPUT);
	}	
	test_free(handles);
}

void NO_NO_test(int iters, int nbytes)
{GASNET_BEGIN_FUNCTION();
    int i;
    int64_t begin, end;
    stat_struct_t st;
    gasnet_coll_handle_t h, *handles;

	int iamsender = (myproc == 0);
	int iamreceiver = !iamsender;

        handles = (gasnet_coll_handle_t*) test_malloc(sizeof(gasnet_coll_handle_t) * iters);

	/* initialize statistics */
	init_stat(&st, nbytes);
	
	BARRIER();
	
	begin = TIME();
	for (i = 0; i < iters; i++) {
		/* XXX: fix src/dst overlap */
		gasnet_coll_broadcast(GASNET_TEAM_ALL, segment, 0, segment, nbytes,
					GASNET_COLL_SINGLE |
					GASNET_COLL_IN_NOSYNC |
					GASNET_COLL_OUT_NOSYNC |
					GASNET_COLL_SRC_IN_SEGMENT |
					GASNET_COLL_DST_IN_SEGMENT);
	}
	BARRIER();
	end = TIME();
 	update_stat(&st, (end - begin), iters);
	
	
	if (iamsender) {
		print_stat(myproc, &st, "broadcast(NO,NO) latency", PRINT_LATENCY);
	}	

	/* initialize statistics */
	init_stat(&st, nbytes);

	BARRIER();
	
	begin = TIME();
	for (i = 0; i < iters; i++) {
		/* XXX: fix src/dst overlap */
		handles[i] = 
			gasnet_coll_broadcast_nb(GASNET_TEAM_ALL, segment, 0, segment, nbytes,
						GASNET_COLL_SINGLE |
						GASNET_COLL_IN_NOSYNC |
						GASNET_COLL_OUT_NOSYNC |
						GASNET_COLL_SRC_IN_SEGMENT |
						GASNET_COLL_DST_IN_SEGMENT);
	}
	gasnet_coll_wait_sync_all(handles, iters);
	BARRIER();
	end = TIME();
 	update_stat(&st, (end - begin), iters);
	
	
	if (iamsender) {
		print_stat(myproc, &st, "broadcast_nb(NO,NO) throughput", PRINT_THROUGHPUT);
	}	
	test_free(handles);

	/* initialize statistics */
	init_stat(&st, nbytes);

	BARRIER();
	
	if (iamsender) {
		gasnet_handle_t *h = (gasnet_handle_t *)test_malloc(iters*sizeof(gasnet_handle_t));
		begin = TIME();
		for (i = 0; i < iters; i++) {
			/* XXX: fix src/dst overlap */
			int j;

			gasnet_begin_nbi_accessregion();
			for (j=0; j<numprocs; ++j) {
				gasnet_put_nbi_bulk(j, segment, segment, nbytes);
			}
			h[i] = gasnet_end_nbi_accessregion();
		}
		gasnet_wait_syncnb_all(h, iters);
		end = TIME();
 		update_stat(&st, (end - begin), iters);
		test_free(h);
	}
	
	BARRIER();
	
	if (iamsender) {
		print_stat(myproc, &st, "put_nbi-bcast throughput", PRINT_THROUGHPUT);
	}	

	/* initialize statistics */
	init_stat(&st, nbytes);

	BARRIER();
	
	begin = TIME();
	for (i = 0; i < iters; i++) {
		gasnet_get_nbi_bulk(segment, 0, segment, nbytes);
	}
	gasnet_wait_syncnbi_gets();
	BARRIER();
	end = TIME();
 	update_stat(&st, (end - begin), iters);
	
	if (iamsender) {
		print_stat(myproc, &st, "get_nbi-bcast throughput", PRINT_THROUGHPUT);
	}	
}

int main(int argc, char **argv)
{
    int arg;
    int iters = 0;
    int i, j;
    int *src;
   
    /* call startup */
    GASNET_Safe(gasnet_init(&argc, &argv));
    GASNET_Safe(gasnet_attach(NULL, 0, TEST_SEGSZ, TEST_MINHEAPOFFSET));

    if (argc > 1) {
      iters = atoi(argv[1]);
    }
    if (iters < 1) {
      iters = 1000;
    }
    /* get SPMD info */
    myproc = gasnet_mynode();
    numprocs = gasnet_nodes();
    
    if (myproc == 0) {
	printf("Running coll test(s) with %d iterations.\n", iters);
    }
    gasnet_coll_init(NULL, NULL, 0, 0);

    segment = (int *) TEST_MYSEG();
    src = segment + 16;

    MSG("running.");
    BARRIER();

    for (j = 0; j < iters; ++j) {
      
      *segment = -1;
      for (i = 0; i < numprocs; ++i) {
	int want = j ^ i;
	int tmp;
	gasnet_coll_handle_t h;

	*src = j ^ myproc;

        h = gasnet_coll_broadcast_nb(GASNET_TEAM_ALL, segment+2, i, src, sizeof(int),
				     GASNET_COLL_SINGLE |
				     GASNET_COLL_IN_ALLSYNC |
				     GASNET_COLL_OUT_ALLSYNC |
				     GASNET_COLL_SRC_IN_SEGMENT |
				     GASNET_COLL_DST_IN_SEGMENT);

        (void)gasnet_coll_broadcast_nb(GASNET_TEAM_ALL, segment, i, src, sizeof(int),
				      GASNET_COLL_SINGLE |
				      GASNET_COLL_IN_MYSYNC |
				      GASNET_COLL_OUT_NOSYNC |
				      GASNET_COLL_SRC_IN_SEGMENT |
				      GASNET_COLL_DST_IN_SEGMENT |
				      GASNET_COLL_AGGREGATE);
        gasnet_coll_broadcast(GASNET_TEAM_ALL, segment+1, i, src, sizeof(int),
				      GASNET_COLL_SINGLE |
				      GASNET_COLL_IN_NOSYNC |
				      GASNET_COLL_OUT_MYSYNC |
				      GASNET_COLL_SRC_IN_SEGMENT |
				      GASNET_COLL_DST_IN_SEGMENT);
	tmp = segment[0];
	if (tmp != want) {
          MSG("Expected segment[0]=%d got %d", want, tmp);
	}
	gasnet_coll_wait_sync(h);
	tmp = segment[2];
	if (tmp != want) {
          MSG("Expected segment[2]=%d got %d", want, tmp);
	}
      }
    }

#if 0
    for (i = 1; i <= 4096; i *= 2) {
      ALL_ALL_test(iters, i);
      NO_NO_test(iters, i);
    }
#endif

    BARRIER();

    MSG("done.");

    gasnet_exit(0);

    return 0;

}
/* ------------------------------------------------------------------------------------ */

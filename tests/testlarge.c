/*  $Archive:: /Ti/GASNet/tests/testlarge.c                                 $
 *     $Date: 2004/01/23 10:35:05 $
 * $Revision: 1.12 $
 * Description: GASNet bulk get/put performance test
 *   measures the ping-pong average round-trip time and
 *   average flood throughput of GASNet bulk gets and puts
 *   over varying payload size and synchronization mechanisms
 * Copyright 2002, Jaein Jeong and Dan Bonachea <bonachea@cs.berkeley.edu>
 * Terms of use are as specified in license.txt
 */

/************************************************************
	testlarge.c:
		measures the bandwidth of get and put
		for large messages with payload size (512 .. limit bytes)
		also measures the barrier time.
		
*************************************************************/

#include "gasnet.h"
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <sys/time.h>
#include <unistd.h>
#include <fcntl.h>

#include "test.h"

#define GASNET_HEADNODE 0
#define PRINT_LATENCY 0
#define PRINT_THROUGHPUT 1

typedef struct {
	int datasize;
	int iters;
	uint64_t time;
	double max_throughput;
} stat_struct_t;

gasnet_handlerentry_t handler_table[2];

int insegment = 0;

int myproc;
int numprocs;
int peerproc;

int min_payload;
int max_payload;

char *tgtmem;
void *msgbuf;

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
		printf("Proc %3i - %10i byte : %7i iters,"
			   " latency %12i us total, %9.3f us ave. (%s)\n",
			myproc, st->datasize, st->iters, (int) st->time,
			((float)st->time) / st->iters,
			name);
		fflush(stdout);
		break;
	case PRINT_THROUGHPUT:
		printf("Proc %3i - %10i byte : %7i iters,"
			" throughput %9.3f KB/sec (%s)\n",
			myproc, st->datasize, st->iters,
                        ((int)st->time == 0 ? 0.0 :
                        (1000000.0 * st->datasize * st->iters / 1024.0) / ((int)st->time)),
			name);
		fflush(stdout);
		break;
	default:
		break;
	}
}

void bulk_test(int iters) {GASNET_BEGIN_FUNCTION();
    int i;
    int64_t begin, end;
    int64_t temptime;
    stat_struct_t stget, stput;
    int payload;
	int iamsender = (myproc % 2 == 0);
	int iamreceiver = !iamsender;
    
	for (payload = min_payload; payload <= max_payload; payload *= 2) {
		init_stat(&stput, payload);

		BARRIER();
	
		if (iamsender) {
			/* measure the throughput of sending a message */
			begin = TIME();
			for (i = 0; i < iters; i++) {
				gasnet_put_bulk(peerproc, tgtmem, msgbuf, payload);
			}
			end = TIME();
		 	update_stat(&stput, (end - begin), iters);
		}
	
		BARRIER();

		if (iamsender) {
			print_stat(myproc, &stput, "put_bulk throughput", PRINT_THROUGHPUT);
		}	
	
		init_stat(&stget, payload);

		if (iamsender) {
			/* measure the throughput of receiving a message */
			begin = TIME();
			for (i = 0; i < iters; i++) {
			    gasnet_get_bulk(msgbuf, peerproc, tgtmem, payload);
			}
			end = TIME();
		 	update_stat(&stget, (end - begin), iters);
		}
	
		BARRIER();

		if (iamsender) {
			print_stat(myproc, &stget, "get_bulk throughput", PRINT_THROUGHPUT);
		}	

	}

}

void bulk_test_nbi(int iters) {GASNET_BEGIN_FUNCTION();
    int i, j;
    int increment = 1024;
    int64_t begin, end;
    int64_t temptime;
    stat_struct_t stget, stput;
    int payload;
	int iamsender = (myproc % 2 == 0);
	int iamreceiver = !iamsender;
    
	for (payload = min_payload; payload <= max_payload; payload *= 2) {
		init_stat(&stput, payload);

		BARRIER();
	
		if (iamsender) {
			/* measure the throughput of sending a message */
			begin = TIME();
			for (i = 0; i < iters; i++) {
				gasnet_put_nbi_bulk(peerproc, tgtmem, msgbuf, payload);
			}
			gasnet_wait_syncnbi_puts();
			end = TIME();
		 	update_stat(&stput, (end - begin), iters);
		}
	
		BARRIER();

		if (iamsender) {
			print_stat(myproc, &stput, "put_nbi_bulk throughput", PRINT_THROUGHPUT);
		}	
	
		init_stat(&stget, payload);

		if (iamsender) {
			/* measure the throughput of receiving a message */
			begin = TIME();
			for (i = 0; i < iters; i++) {
			    gasnet_get_nbi_bulk(msgbuf, peerproc, tgtmem, payload);
			}
			gasnet_wait_syncnbi_gets();
			end = TIME();
		 	update_stat(&stget, (end - begin), iters);
		}
	
		BARRIER();

		if (iamsender) {
			print_stat(myproc, &stget, "get_nbi_bulk throughput", PRINT_THROUGHPUT);
		}	

	}

}

void bulk_test_nb(int iters) {GASNET_BEGIN_FUNCTION();
    int i, j;
    int64_t begin, end;
    int64_t temptime;
    int increment = 1024;
    stat_struct_t stget, stput;
    gasnet_handle_t hdlget, hdlput;
    gasnet_handle_t *handles;
    int payload;
	int iamsender = (myproc % 2 == 0);
	int iamreceiver = !iamsender;
    
	handles = (gasnet_handle_t *) test_malloc(sizeof(gasnet_handle_t) * iters);

	for (payload = min_payload; payload <= max_payload; payload *= 2) {
		init_stat(&stput, payload);

		BARRIER();
	
		if (iamsender) {
			/* measure the throughput of sending a message */
			begin = TIME();
			for (i = 0; i < iters; i++) {
				handles[i] = gasnet_put_nb_bulk(peerproc, tgtmem, msgbuf, payload);
			}
			gasnet_wait_syncnb_all(handles, iters);
			end = TIME();
		 	update_stat(&stput, (end - begin), iters);
		}
	
		BARRIER();
       
		if (iamsender) {
			print_stat(myproc, &stput, "put_nb_bulk throughput", PRINT_THROUGHPUT);
		}	
	
		init_stat(&stget, payload);

		if (iamsender) {
			/* measure the throughput of receiving a message */
			begin = TIME();
			for (i = 0; i < iters; i++) {
			    handles[i] = gasnet_get_nb_bulk(msgbuf, peerproc, tgtmem, payload);
			}
			gasnet_wait_syncnb_all(handles, iters);
			end = TIME();
		 	update_stat(&stget, (end - begin), iters);
		}
	
		BARRIER();

		if (iamsender) {
			print_stat(myproc, &stget, "get_nb_bulk throughput", PRINT_THROUGHPUT);
		}	

	}

	test_free(handles);
}


int main(int argc, char **argv)
{
    int iters = 0;
    int arg;
   
    /* call startup */
    GASNET_Safe(gasnet_init(&argc, &argv));
    GASNET_Safe(gasnet_attach(NULL, 0, TEST_SEGSZ, TEST_MINHEAPOFFSET));
    TEST_SEG(gasnet_mynode()); /* ensure we got the segment requested */

    /* parse arguments (we could do better) */
    arg = 1;
    if (argc > arg && !strcmp(argv[arg], "-in")) {
        insegment = 1;
        ++arg;
    }
    if (argc > arg && !strcmp(argv[arg], "-out")) {
        insegment = 0;
        ++arg;
    }
    if (argc > arg+1) {
        printf("Usage: %s [-in|-out] (iters) \n"
               "  The 'in' or 'out' option selects whether the initiator-side\n"
               "  memory is in the GASNet segment or not (default it not).\n",
               argv[0]);
        gasnet_exit(1);
    }

    if (argc > arg) iters = atoi(argv[arg]);
    if (!iters) iters = 1;

    /* get SPMD info */
    myproc = gasnet_mynode();
    numprocs = gasnet_nodes();
    
    /* Only allow 1 or even number for numprocs */
    if (numprocs % 2 == 1) {
    	printf("Number of threads should be even number.\n");
    	gasnet_exit(1);
    }

    
    /* Setting peer thread rank */
    peerproc = (myproc % 2) ? (myproc - 1) : (myproc + 1);
    
    tgtmem = (void *) TEST_SEG(peerproc);

	min_payload = 16;
	max_payload = TEST_SEGSZ;

        if (insegment) {
	    msgbuf = tgtmem;
        } else {
	    msgbuf = (void *) test_malloc(max_payload);
        }

	bulk_test(iters);
	bulk_test_nbi(iters);
	bulk_test_nb(iters);

        if (!insegment) {
	    test_free(msgbuf);
	}

    gasnet_exit(0);

    return 0;

}


/* ------------------------------------------------------------------------------------ */

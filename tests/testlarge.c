/*   $Source: /Users/kamil/work/gasnet-cvs2/gasnet/tests/testlarge.c,v $
 *     $Date: 2004/09/27 09:53:03 $
 * $Revision: 1.22 $
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
#if defined(GASNET_SEGMENT_EVERYTHING) && !defined(TEST_SEGSZ)
#define TEST_SEGSZ alignup((16*1048576),PAGESZ)
#endif
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
int peerproc = -1;
int iamsender = 0;

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
    int maxsz = 0;
    int arg;
    void *myseg;
    void *alloc;
    int firstlastmode = 0;
    int help = 0;   

    /* call startup */
    GASNET_Safe(gasnet_init(&argc, &argv));

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
    if (argc > arg && !strcmp(argv[arg], "-f")) {
        firstlastmode = 1;
        ++arg;
    }
    if (argc > arg && argv[arg][0] == '-') {
        help = 1;
        ++arg;
    }
    if (help || argc > arg+2) {
        printf("Usage: %s [-in|-out] (iters) (maxsz)\n"
               "  The 'in' or 'out' option selects whether the initiator-side\n"
               "  memory is in the GASNet segment or not (default it not).\n"
               "  The -f option enables 'first/last' mode, where the first/last\n"
               "  nodes communicate with each other, while all other nodes sit idle.\n",
               argv[0]);
        gasnet_exit(1);
    }

    if (argc > arg) { iters = atoi(argv[arg]); arg++; }
    if (!iters) iters = 1000;
    if (argc > arg) { maxsz = atoi(argv[arg]); arg++; }
    if (!maxsz) maxsz = 2*1024*1024; /* 2 MB default */

    min_payload = 16;
    max_payload = maxsz;

    if (max_payload < min_payload) {
      printf("ERROR: maxsz must be >= %i\n",min_payload);
      gasnet_exit(1);
    }

    /* get SPMD info */
    myproc = gasnet_mynode();
    numprocs = gasnet_nodes();
    
    if (!firstlastmode) {
      /* Only allow 1 or even number for numprocs */
      if (numprocs > 1 && numprocs % 2 != 0) {
    	  printf("Number of threads should be even number.\n");
    	  gasnet_exit(1);
      }
    }
    
    /* Setting peer thread rank */
    if (firstlastmode) {
      peerproc = numprocs-1;
      iamsender = (myproc == 0);
    }  else if (numprocs == 1) {
      peerproc = 0;
      iamsender = 1;
    } else { 
      peerproc = (myproc % 2) ? (myproc - 1) : (myproc + 1);
      iamsender = (myproc % 2 == 0);
    }

    #ifdef GASNET_SEGMENT_EVERYTHING
      if (maxsz > TEST_SEGSZ) { MSG("maxsz must be <= %i on GASNET_SEGMENT_EVERYTHING",TEST_SEGSZ); gasnet_exit(1); }
    #endif
    GASNET_Safe(gasnet_attach(NULL, 0, alignup(((uintptr_t)maxsz), PAGESZ), TEST_MINHEAPOFFSET));
    TEST_DEBUGPERFORMANCE_WARNING();
    #ifdef GASNET_SEGMENT_EVERYTHING
      myseg = TEST_SEG(myproc);
      tgtmem = TEST_SEG(peerproc);
    #else
    { /* ensure we got the segment requested */
      int i;
      gasnet_seginfo_t *s = test_malloc(gasnet_nodes()*sizeof(gasnet_seginfo_t));
      GASNET_Safe(gasnet_getSegmentInfo(s, gasnet_nodes()));
      for (i=0; i < gasnet_nodes(); i++) {
        assert(s[i].size >= maxsz);
        #if GASNET_ALIGNED_SEGMENTS == 1
          assert(s[i].addr == s[0].addr);
        #endif
      }
      tgtmem = s[peerproc].addr; /* get peer segment */
      myseg = s[myproc].addr; 
      test_free(s);
    }
    #endif
    assert(((uintptr_t)myseg) % PAGESZ == 0);
    assert(((uintptr_t)tgtmem) % PAGESZ == 0);

        if (insegment) {
	    msgbuf = (void *) myseg;
        } else {
	    alloc = (void *) test_malloc(maxsz+PAGESZ);
            msgbuf = (void *) alignup(((uintptr_t)alloc), PAGESZ); /* ensure page alignment of base */
        }
        assert(((uintptr_t)msgbuf) % PAGESZ == 0);

        if (myproc == 0) 
          MSG("Running %i iterations of %sbulk put/get with local addresses %sside the segment for sizes: %i...%i\nGASNET_CONFIG:%s\n", 
          iters, 
          firstlastmode ? "first/last " : "",
          insegment ? "in" : "out", 
          min_payload, max_payload, GASNET_CONFIG_STRING);
        BARRIER();
	bulk_test(iters);
	bulk_test_nbi(iters);
	bulk_test_nb(iters);

        if (!insegment) {
	    test_free(alloc);
	}

    gasnet_exit(0);

    return 0;

}


/* ------------------------------------------------------------------------------------ */

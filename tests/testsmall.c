/*   $Source: /Users/kamil/work/gasnet-cvs2/gasnet/tests/testsmall.c,v $
 *     $Date: 2004/09/22 09:53:08 $
 * $Revision: 1.19 $
 * Description: GASNet non-bulk get/put performance test
 *   measures the ping-pong average round-trip time and
 *   average flood throughput of GASNet gets and puts
 *   over varying payload size and synchronization mechanisms
 * Copyright 2002, Jaein Jeong and Dan Bonachea <bonachea@cs.berkeley.edu>
 * Terms of use are as specified in license.txt
 */

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
} stat_struct_t;

gasnet_handlerentry_t handler_table[2];

int insegment = 0;

int myproc;
int numprocs;
int peerproc = -1;
int iamsender = 0;

void *tgtmem;
char *msgbuf;
char *ackbuf;

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
#if 1
			" throughput %9.3f KB/sec (%s)\n"
#else
			" inv. throughput %9.3f us (%s)\n"
#endif
                        ,
			myproc, st->datasize, st->iters,
#if 1
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


void roundtrip_test(int iters, int nbytes)
{GASNET_BEGIN_FUNCTION();
    int i;
    int64_t begin, end;
    stat_struct_t st;

	/* initialize statistics */
	init_stat(&st, nbytes);
	
	memset(msgbuf, 1, nbytes);
	memset(ackbuf, 0, nbytes);

	BARRIER();
	
	if (iamsender) {
		/* measure the round-trip time of put */
		begin = TIME();
		for (i = 0; i < iters; i++) {
			gasnet_put(peerproc, tgtmem, msgbuf, nbytes);
		}
		end = TIME();
	 	update_stat(&st, (end - begin), iters);
	}
	
	BARRIER();
	
	if (iamsender) {
		print_stat(myproc, &st, "put latency", PRINT_LATENCY);
	}	

	/* initialize statistics */
	init_stat(&st, nbytes);

	if (iamsender) {
		/* measure the round-trip time of get */
		begin = TIME();
		for (i = 0; i < iters; i++) {
	 		gasnet_get(ackbuf, peerproc, tgtmem, nbytes);
		}
		end = TIME();
	 	update_stat(&st, (end - begin), iters);
	}
	
	BARRIER();
	
	if (iamsender) {
		print_stat(myproc, &st, "get latency", PRINT_LATENCY);
	}	
}

void oneway_test(int iters, int nbytes)
{GASNET_BEGIN_FUNCTION();
    int i;
    int64_t begin, end;
    stat_struct_t st;

	/* initialize statistics */
	init_stat(&st, nbytes);
	
	memset(msgbuf, 1, nbytes);
	memset(ackbuf, 0, nbytes);

	BARRIER();
	
	if (iamsender) {
		/* measure the throughput of put */
		begin = TIME();
		for (i = 0; i < iters; i++) {
			gasnet_put(peerproc, tgtmem, msgbuf, nbytes);
		}
		end = TIME();
	 	update_stat(&st, (end - begin), iters);
	}
	
	BARRIER();
	
	if (iamsender) {
		print_stat(myproc, &st, "put throughput", PRINT_THROUGHPUT);
	}	

	/* initialize statistics */
	init_stat(&st, nbytes);

	if (iamsender) {
		/* measure the throughput of get */
		begin = TIME();
		for (i = 0; i < iters; i++) {
	 		gasnet_get(ackbuf, peerproc, tgtmem, nbytes);
		}
		end = TIME();
	 	update_stat(&st, (end - begin), iters);
	}
	
	BARRIER();
	
	if (iamsender) {
		print_stat(myproc, &st, "get throughput", PRINT_THROUGHPUT);
	}	
}


void roundtrip_nbi_test(int iters, int nbytes)
{GASNET_BEGIN_FUNCTION();
    int i;
    int64_t begin, end;
    stat_struct_t st;

	/* initialize statistics */
	init_stat(&st, nbytes);
	
	memset(msgbuf, 1, nbytes);
	memset(ackbuf, 0, nbytes);

	BARRIER();
	
	if (iamsender) {
		/* measure the round-trip time of nonblocking implicit put */
		begin = TIME();
		for (i = 0; i < iters; i++) {
			gasnet_put_nbi(peerproc, tgtmem, msgbuf, nbytes);
			gasnet_wait_syncnbi_puts();
		}
		end = TIME();
	 	update_stat(&st, (end - begin), iters);
	}
	
	BARRIER();
	
	if (iamsender) {
		print_stat(myproc, &st, "put_nbi latency", PRINT_LATENCY);
	}	


	/* initialize statistics */
	init_stat(&st, nbytes);

	if (iamsender) {
		/* measure the round-trip time of nonblocking implicit get */
		begin = TIME();
		for (i = 0; i < iters; i++) {
	 		gasnet_get_nbi(ackbuf, peerproc, tgtmem, nbytes);
			gasnet_wait_syncnbi_gets();
		}
		end = TIME();
	 	update_stat(&st, (end - begin), iters);
	}
	
	BARRIER();
	
	if (iamsender) {
		print_stat(myproc, &st, "get_nbi latency", PRINT_LATENCY);
	}	

}

void oneway_nbi_test(int iters, int nbytes)
{GASNET_BEGIN_FUNCTION();
    int i;
    int64_t begin, end;
    stat_struct_t st;

	/* initialize statistics */
	init_stat(&st, nbytes);
	
	memset(msgbuf, 1, nbytes);
	memset(ackbuf, 0, nbytes);

	BARRIER();
	
	if (iamsender) {
		/* measure the throughput of nonblocking implicit put */
		begin = TIME();
		for (i = 0; i < iters; i++) {
			gasnet_put_nbi(peerproc, tgtmem, msgbuf, nbytes);
		}
		gasnet_wait_syncnbi_puts();
		end = TIME();
	 	update_stat(&st, (end - begin), iters);
	}
	
	BARRIER();
	
	if (iamsender) {
		print_stat(myproc, &st, "put_nbi throughput", PRINT_THROUGHPUT);
	}	

	/* initialize statistics */
	init_stat(&st, nbytes);

	if (iamsender) {
		/* measure the throughput of nonblocking implicit get */
		begin = TIME();
		for (i = 0; i < iters; i++) {
	 		gasnet_get_nbi(ackbuf, peerproc, tgtmem, nbytes);
		}
		gasnet_wait_syncnbi_gets();
		end = TIME();
	 	update_stat(&st, (end - begin), iters);
	}
	
	BARRIER();
	
	if (iamsender) {
		print_stat(myproc, &st, "get_nbi throughput", PRINT_THROUGHPUT);
	}	
}


void roundtrip_nb_test(int iters, int nbytes)
{GASNET_BEGIN_FUNCTION();
    int i;
    int64_t begin, end;
    stat_struct_t st;
    gasnet_handle_t hdlget, hdlput;

	/* initialize statistics */
	init_stat(&st, nbytes);
	
	memset(msgbuf, 1, nbytes);
	memset(ackbuf, 0, nbytes);

	BARRIER();
	
	if (iamsender) {
		/* measure the round-trip time of nonblocking put */
		begin = TIME();
		for (i = 0; i < iters; i++) {
			hdlput = gasnet_put_nb(peerproc, tgtmem, msgbuf, nbytes);
			gasnet_wait_syncnb(hdlput);
		}
		end = TIME();
	 	update_stat(&st, (end - begin), iters);
	}
	
	BARRIER();
	
	if (iamsender) {
		print_stat(myproc, &st, "put_nb latency", PRINT_LATENCY);
	}	

	/* initialize statistics */
	init_stat(&st, nbytes);

	if (iamsender) {
		/* measure the round-trip time of nonblocking get */
		begin = TIME();
		for (i = 0; i < iters; i++) {
	 		hdlget = gasnet_get_nb(ackbuf, peerproc, tgtmem, nbytes);
			gasnet_wait_syncnb(hdlget);
		}
		end = TIME();
	 	update_stat(&st, (end - begin), iters);
	}
	
	BARRIER();
	
	if (iamsender) {
		print_stat(myproc, &st, "get_nb latency", PRINT_LATENCY);
	}	

}

void oneway_nb_test(int iters, int nbytes)
{GASNET_BEGIN_FUNCTION();
    int i;
    int64_t begin, end;
    stat_struct_t st;
    gasnet_handle_t hdlget, hdlput;
    gasnet_handle_t *handles;

	/* initialize statistics */
	init_stat(&st, nbytes);
	
	handles = (gasnet_handle_t*) test_malloc(sizeof(gasnet_handle_t) * iters);
	
	memset(msgbuf, 1, nbytes);
	memset(ackbuf, 0, nbytes);

	BARRIER();
	
	if (iamsender) {
		/* measure the throughput of sending a message */
		begin = TIME();
		/*for (i = 0; i < iters; i++) {
			hdlput = gasnet_put_nb(peerproc, tgtmem, msgbuf, nbytes);
		        gasnet_wait_syncnb(hdlput);
		}*/
                for (i = 0; i < iters; i++) {
                        handles[i] = gasnet_put_nb(peerproc, tgtmem, msgbuf, nbytes);
                }
		gasnet_wait_syncnb_all(handles, iters); 
		end = TIME();
	 	update_stat(&st, (end - begin), iters);
	}
	
	BARRIER();
	
	if (iamsender) {
		print_stat(myproc, &st, "put_nb throughput", PRINT_THROUGHPUT);
	}	
	
	/* initialize statistics */
	init_stat(&st, nbytes);

	if (iamsender) {
		/* measure the throughput of receiving a message */
		begin = TIME();
		/*for (i = 0; i < iters; i++) {
		    hdlget = gasnet_get_nb(msgbuf, peerproc, tgtmem, nbytes);
		    gasnet_wait_syncnb(hdlget);
		}*/
                for (i = 0; i < iters; i++) {
                    handles[i] = gasnet_get_nb(msgbuf, peerproc, tgtmem, nbytes);
                } 
		gasnet_wait_syncnb_all(handles, iters); 
		end = TIME();
	 	update_stat(&st, (end - begin), iters);
	}
	
	BARRIER();
	
	if (iamsender) {
		print_stat(myproc, &st, "get_nb throughput", PRINT_THROUGHPUT);
	}	
	
	test_free(handles);
}

int main(int argc, char **argv)
{
    int min_payload, max_payload;
    int maxsz = 0;
    void *myseg;
    void *alloc;
    int arg;
    int iters = 0;
    int i, j;
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
        printf("Usage: %s [-in|-out|-f] (iters) (maxsz)\n"
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
    if (!maxsz) maxsz = 2048; /* 2 KB default */

    min_payload = 1;
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
      if (maxsz > TEST_SEGSZ/2) { MSG("maxsz must be <= %i on GASNET_SEGMENT_EVERYTHING",TEST_SEGSZ/2); gasnet_exit(1); }
    #endif
    GASNET_Safe(gasnet_attach(NULL, 0, alignup(((uintptr_t)maxsz), PAGESZ)*2, TEST_MINHEAPOFFSET));
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
	    alloc = (void *) test_malloc((maxsz+PAGESZ)*2);
            msgbuf = (void *) alignup(((uintptr_t)alloc), PAGESZ); /* ensure page alignment of base */
        }
        ackbuf = msgbuf + PAGESZ;
        assert(((uintptr_t)msgbuf) % PAGESZ == 0);
        assert(((uintptr_t)ackbuf) % PAGESZ == 0);
        if (myproc == 0) 
          MSG("Running %i iterations of %snon-bulk put/get with local addresses %sside the segment for sizes: %i...%i\nGASNET_CONFIG:%s\n", 
          iters, 
          firstlastmode ? "first/last " : "",
          insegment ? "in" : "out", 
          min_payload, max_payload, GASNET_CONFIG_STRING);
        BARRIER();

	for (j = min_payload; j <= max_payload; j *= 2)  roundtrip_test(iters, j); 

  	for (j = min_payload; j <= max_payload; j *= 2)  oneway_test(iters, j);

  	for (j = min_payload; j <= max_payload; j *= 2)  roundtrip_nbi_test(iters, j);

  	for (j = min_payload; j <= max_payload; j *= 2)  oneway_nbi_test(iters, j);

  	for (j = min_payload; j <= max_payload; j *= 2)  roundtrip_nb_test(iters, j);

  	for (j = min_payload; j <= max_payload; j *= 2)  oneway_nb_test(iters, j);

        if (!insegment) {
	  test_free(alloc);
	}

    gasnet_exit(0);

    return 0;

}
/* ------------------------------------------------------------------------------------ */

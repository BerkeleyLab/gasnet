/*   $Source: /Users/kamil/work/gasnet-cvs2/gasnet/tests/testqueue.c,v $
 *     $Date: 2005/04/11 02:51:22 $
 * $Revision: 1.1 $
 * Description: GASNet put/get injection performance test
 *   measures the average non-blocking put/get injection time 
 *   for increasing number of back-to-back operations
 *   over varying payload size and synchronization mechanisms
 *   reveals software-imposed queue depth backpressure limitations
 * Copyright 2002, Jaein Jeong and Dan Bonachea <bonachea@cs.berkeley.edu>
 * Terms of use are as specified in license.txt
 */

#include "gasnet.h"
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
int maxsz = 0;
#ifndef TEST_SEGSZ
  #define TEST_SEGSZ_EXPR ((uintptr_t)maxsz)
#endif
#include "test.h"

int insegment = 0;

int myproc;
int numprocs;
int peerproc = -1;
int iamsender = 0;
int multisender = 0;

int min_payload;
int max_payload;
int maxdepth = 0;

char *tgtmem;
void *msgbuf;
gasnet_handle_t *handles;

int main(int argc, char **argv) {
    int iters = 0;
    int arg;
    void *myseg;
    void *alloc;
    int firstlastmode = 0;
    int fullduplexmode = 0;
    int help = 0;   

    /* call startup */
    GASNET_Safe(gasnet_init(&argc, &argv));

    /* parse arguments */
    arg = 1;
    while (argc > arg) {
      if (!strcmp(argv[arg], "-in")) {
        insegment = 1;
        ++arg;
      } else if (!strcmp(argv[arg], "-out")) {
        insegment = 0;
        ++arg;
      } else if (!strcmp(argv[arg], "-f")) {
        firstlastmode = 1;
        ++arg;
      } else if (!strcmp(argv[arg], "-a")) {
        fullduplexmode = 1;
        ++arg;
      } else if (argv[arg][0] == '-') {
        help = 1;
        ++arg;
      } else break;
    }
    if (fullduplexmode && firstlastmode) help = 1;
    if (help || argc > arg+3) {
        printf("Usage: %s [-in|-out|-a|-f] (iters) (maxdepth) (maxsz)\n"
               "  The 'in' or 'out' option selects whether the initiator-side\n"
               "  memory is in the GASNet segment or not (default it not).\n"
               "  The -a option enables full-duplex mode, where all nodes send.\n"
               "  The -f option enables 'first/last' mode, where the first node\n"
               "  sends to the last, while all other nodes sit idle.\n",
               argv[0]);
        gasnet_exit(1);
    }

    if (argc > arg) { iters = atoi(argv[arg]); arg++; }
    if (!iters) iters = 10;
    if (argc > arg) { maxdepth = atoi(argv[arg]); arg++; }
    if (!maxdepth) maxdepth = 1024; /* 1024 default */
    if (argc > arg) { maxsz = atoi(argv[arg]); arg++; }
    if (!maxsz) maxsz = 2*1024*1024; /* 2 MB default */

    min_payload = 1;
    max_payload = maxsz;

    if (max_payload < min_payload) {
      printf("ERROR: maxsz must be >= %i\n",min_payload);
      gasnet_exit(1);
    }

    /* get SPMD info */
    myproc = gasnet_mynode();
    numprocs = gasnet_nodes();

    if (!myproc)
	print_testname("testqueue", numprocs);
    
    if (!firstlastmode) {
      /* Only allow 1 or even number for numprocs */
      if (numprocs > 1 && numprocs % 2 != 0) {
        MSG("WARNING: This test requires a unary or even number of threads. Test skipped.\n");
        gasnet_exit(0); /* exit 0 to prevent false negatives in test harnesses for smp-conduit */
      }
    }
    
    /* Setting peer thread rank */
    if (firstlastmode) {
      peerproc = numprocs-1;
      iamsender = (myproc == 0);
    } else if (numprocs == 1) {
      peerproc = 0;
      iamsender = 1;
    } else { 
      peerproc = (myproc % 2) ? (myproc - 1) : (myproc + 1);
      iamsender = (fullduplexmode || myproc % 2 == 0);
      multisender = (fullduplexmode || numprocs >= 4);
    }
    multisender = 1; /* messes up output on some systems */

    #ifdef GASNET_SEGMENT_EVERYTHING
      if (maxsz > TEST_SEGSZ) { MSG("maxsz must be <= %lu on GASNET_SEGMENT_EVERYTHING",(unsigned long)TEST_SEGSZ); gasnet_exit(1); }
    #endif
    GASNET_Safe(gasnet_attach(NULL, 0, TEST_SEGSZ_REQUEST, TEST_MINHEAPOFFSET));
    TEST_DEBUGPERFORMANCE_WARNING();
    myseg = TEST_SEG(myproc);
    tgtmem = TEST_SEG(peerproc);

    if (insegment) {
	msgbuf = (void *) myseg;
    } else {
	alloc = (void *) test_malloc(maxsz+PAGESZ);
        msgbuf = (void *) alignup(((uintptr_t)alloc), PAGESZ); /* ensure page alignment of base */
    }
    assert(((uintptr_t)msgbuf) % PAGESZ == 0);

    MSG0("Running %squeue test with local addr %sside segment, iters=%i, maxdepth=%i, sz: %i...%i", 
      firstlastmode ? "first/last " : (fullduplexmode ? "full-duplex ": ""),
      insegment ? "in" : "out", 
      iters, 
      maxdepth,
      min_payload, max_payload);
    MSG0("x-axis: queue depth, y-axis: message size, injection time in microseconds\n");
    BARRIER();

    handles = (gasnet_handle_t *) test_malloc(sizeof(gasnet_handle_t) * maxdepth);

    #define QUEUE_TEST(OPDESC, OP, SYNC) do {                               \
      int depth, payload;                                                   \
      BARRIER();                                                            \
      MSG0("\n%s\n--------------------\n", OPDESC);                         \
      { char header[1024];                                                  \
        char *pheader = header;                                             \
        sprintf(pheader, "        "); pheader += strlen(pheader);          \
        for (depth = 1; depth <= maxdepth; depth *= 2) {                    \
          sprintf(pheader, " %7i", depth); pheader += strlen(pheader);      \
        }                                                                   \
        MSG0(header);                                                       \
      }                                                                     \
      for (payload = min_payload; payload <= max_payload; payload *= 2) {   \
        char row[1024];                                                     \
        char *prow = row;                                                   \
        sprintf(prow, "%-8i", payload); prow += strlen(prow);               \
        if (!multisender) { printf("%s",row); fflush(stdout); prow = row; } \
        for (depth = 1; depth <= maxdepth; depth *= 2) {                    \
          BARRIER();                                                        \
                                                                            \
          if (iamsender) {                                                  \
            int iter,i;                                                     \
            gasnett_tick_t total = 0,                                       \
                           min = GASNETT_TICK_MAX,                          \
                           max = GASNETT_TICK_MIN;                          \
            for (iter = 0; iter < iters; iter++) {                          \
              gasnett_tick_t begin, end, thistime;                          \
              BARRIER();                                                    \
              /* measure time to inject depth operations of payload sz */   \
              begin = gasnett_ticks_now();                                  \
                for (i = 0; i < depth; i++) {                               \
                  OP;                                                       \
                }                                                           \
              end = gasnett_ticks_now();                                    \
              { SYNC; }                                                     \
              BARRIER();                                                    \
              thistime = (end - begin);                                     \
              total += thistime;                                            \
              min = MIN(min,thistime);                                      \
              max = MAX(max,thistime);                                      \
            }                                                               \
            { double avgus = gasnett_ticks_to_us(total) /                   \
                             (double)iters / (double)depth;                 \
              double minus = gasnett_ticks_to_us(min) / (double)depth;      \
              double maxus = gasnett_ticks_to_us(max) / (double)depth;      \
              int prec;                                                     \
              if (avgus < 1000.0) prec = 3;                                 \
              else if (avgus < 10000.0) prec = 2;                           \
              else if (avgus < 100000.0) prec = 1;                          \
              else prec = 0;                                                \
              sprintf(prow, " %7.*f", prec, avgus); prow += strlen(prow);   \
              if (!multisender) {                                           \
                  printf("%s",row); fflush(stdout); prow = row;             \
              }                                                             \
            }                                                               \
          } else {                                                          \
            int i;                                                          \
            for (i = 0; i < 2*iters; i++) {                                 \
              BARRIER();                                                    \
            }                                                               \
          }                                                                 \
        }                                                                   \
        if (iamsender) {                                                    \
          printf("%s\n", row); fflush(stdout);                              \
        }                                                                   \
      }                                                                     \
    } while (0)

    QUEUE_TEST("gasnet_put_nb_bulk", 
               handles[i] = gasnet_put_nb_bulk(peerproc, tgtmem, msgbuf, payload), 
               gasnet_wait_syncnb_all(handles, depth));

    QUEUE_TEST("gasnet_get_nb_bulk", 
               handles[i] = gasnet_get_nb_bulk(msgbuf, peerproc, tgtmem, payload), 
               gasnet_wait_syncnb_all(handles, depth));

    QUEUE_TEST("gasnet_put_nbi_bulk", 
               gasnet_put_nbi_bulk(peerproc, tgtmem, msgbuf, payload), 
               gasnet_wait_syncnbi_all());

    QUEUE_TEST("gasnet_get_nbi_bulk", 
               gasnet_get_nbi_bulk(msgbuf, peerproc, tgtmem, payload), 
               gasnet_wait_syncnbi_all());

    QUEUE_TEST("gasnet_put_nb", 
               handles[i] = gasnet_put_nb(peerproc, tgtmem, msgbuf, payload), 
               gasnet_wait_syncnb_all(handles, depth));

    QUEUE_TEST("gasnet_get_nb", 
               handles[i] = gasnet_get_nb(msgbuf, peerproc, tgtmem, payload), 
               gasnet_wait_syncnb_all(handles, depth));

    QUEUE_TEST("gasnet_put_nbi", 
               gasnet_put_nbi(peerproc, tgtmem, msgbuf, payload), 
               gasnet_wait_syncnbi_all());

    QUEUE_TEST("gasnet_get_nbi", 
               gasnet_get_nbi(msgbuf, peerproc, tgtmem, payload), 
               gasnet_wait_syncnbi_all());

    BARRIER();
    test_free(handles);
    if (!insegment) {
	test_free(alloc);
    }

    gasnet_exit(0);

    return 0;

}


/* ------------------------------------------------------------------------------------ */

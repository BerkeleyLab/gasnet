/* $Id: testcontend.c,v 1.1 2004/07/17 17:00:47 bonachea Exp $
 *
 * Description: GASNet threaded contention tester.
 *   The test initializes GASNet and forks off up to 256 threads.  
 *  The test measures the level of inter-thread contention for local 
 *  network resources with various different usage patterns.
 *
 * Copyright 2004, Dan Bonachea <bonachea@cs.berkeley.edu>
 * Terms of use are as specified in license.txt
 */

#include "gasnet.h"
#include "gasnet_tools.h"
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <pthread.h>

#include "test.h"

#ifndef GASNET_PAR
#error This test can only be built for GASNet PAR configuration
#endif

typedef struct {
  int activecnt;
  int passivecnt;
} threadcnt_t;

typedef gasnet_handlerarg_t harg_t;

/* configurable parameters */
#define DEFAULT_ITERS 50
int	iters = DEFAULT_ITERS;
pthread_t	*tt_tids;
int amactive;
int peer = -1;
char *peerseg = NULL;
int threads;
gasnett_atomic_t pong;
int signal_done = 0;

void	thread_barrier();
typedef void * (*threadmain_t)(void *args);

/* AM Handlers */
void	ping_shorthandler(gasnet_token_t token);
void 	pong_shorthandler(gasnet_token_t token);

void	ping_medhandler(gasnet_token_t token, void *buf, size_t nbytes);
void	pong_medhandler(gasnet_token_t token, void *buf, size_t nbytes);

void	ping_longhandler(gasnet_token_t token, void *buf, size_t nbytes);
void	pong_longhandler(gasnet_token_t token, void *buf, size_t nbytes);

void	markdone_shorthandler(gasnet_token_t token);
void	noop_shorthandler(gasnet_token_t token);


#define hidx_ping_shorthandler        201
#define hidx_pong_shorthandler        202
#define hidx_ping_medhandler          203
#define hidx_pong_medhandler          204
#define hidx_ping_longhandler         205
#define hidx_pong_longhandler         206
#define hidx_markdone_shorthandler    207
#define hidx_noop_shorthandler        208

gasnet_handlerentry_t htable[] = { 
	{ hidx_ping_shorthandler,  ping_shorthandler  },
	{ hidx_pong_shorthandler,  pong_shorthandler  },
	{ hidx_ping_medhandler,    ping_medhandler    },
	{ hidx_pong_medhandler,    pong_medhandler    },
	{ hidx_ping_longhandler,   ping_longhandler   },
	{ hidx_pong_longhandler,   pong_longhandler   },
	{ hidx_markdone_shorthandler,   markdone_shorthandler   },
	{ hidx_noop_shorthandler,   noop_shorthandler   },
};
#define HANDLER_TABLE_SIZE (sizeof(htable)/sizeof(gasnet_handlerentry_t))

#define SPINPOLL_UNTIL(cond) do { while (!(cond)) gasnet_AMPoll(); } while (0)

int _havereport = 0;
char _reportstr[255];
const char *getreport() {
  if (_havereport) {
    _havereport = 0;
    return _reportstr;
  } else return NULL;
}
void report(gasnett_tick_t ticks) {
  double timeus = (double)gasnett_ticks_to_us(ticks);
  sprintf(_reportstr, 
     "%7.3f us\t%5.3f sec", 
     timeus/iters, timeus/1000000);
  _havereport = 1;
}

/* testing functions */

#define AMPINGPONG(fnname, POLLUNTIL)                                                   \
  void * fnname(void *args) {                                                           \
    int mythread = (int)(intptr_t)args;                                                 \
    gasnett_tick_t start, end;                                                          \
    signal_done = 0;                                                                    \
    thread_barrier();                                                                   \
    if (mythread == 0) {                                                                \
      int i;                                                                            \
      gasnett_atomic_set(&pong,0);                                                      \
      start = gasnett_ticks_now();                                                      \
      for (i = 0; i < iters; i++) {                                                     \
        GASNET_Safe(gasnet_AMRequestShort0(peer, hidx_ping_shorthandler));              \
        POLLUNTIL(gasnett_atomic_read(&pong) > i);                                      \
      }                                                                                 \
      end = gasnett_ticks_now();                                                        \
      GASNET_Safe(gasnet_AMRequestShort0(peer, hidx_markdone_shorthandler));            \
      GASNET_Safe(gasnet_AMRequestShort0(gasnet_mynode(), hidx_markdone_shorthandler)); \
    } else {                                                                            \
      POLLUNTIL(signal_done);                                                           \
    }                                                                                   \
    thread_barrier();                                                                   \
    if (mythread == 0 && amactive) report(end-start);                                   \
    return NULL;                                                                        \
  }

AMPINGPONG(ampingpong_poll_active, SPINPOLL_UNTIL)
AMPINGPONG(ampingpong_block_active, GASNET_BLOCKUNTIL)

#define PUTGETPINGPONG(fnname, POLLUNTIL, putgetstmt)                                   \
  void * fnname(void *args) {                                                           \
    int64_t tmp = 0;                                                                    \
    int mythread = (int)(intptr_t)args;                                                 \
    gasnett_tick_t start, end;                                                          \
    signal_done = 0;                                                                    \
    thread_barrier();                                                                   \
    if (mythread == 0) {                                                                \
      int i;                                                                            \
      gasnett_atomic_set(&pong,0);                                                      \
      start = gasnett_ticks_now();                                                      \
      for (i = 0; i < iters; i++) {                                                     \
        putgetstmt;                                                                     \
      }                                                                                 \
      end = gasnett_ticks_now();                                                        \
      GASNET_Safe(gasnet_AMRequestShort0(peer, hidx_markdone_shorthandler));            \
      GASNET_Safe(gasnet_AMRequestShort0(gasnet_mynode(), hidx_markdone_shorthandler)); \
    } else {                                                                            \
      POLLUNTIL(signal_done);                                                           \
    }                                                                                   \
    thread_barrier();                                                                   \
    if (mythread == 0 && amactive) report(end-start);                                   \
    return NULL;                                                                        \
  }                                                                                     \

PUTGETPINGPONG(put_poll_active, SPINPOLL_UNTIL, gasnet_put(peer, peerseg, &tmp, 8));
PUTGETPINGPONG(get_poll_active, SPINPOLL_UNTIL, gasnet_get(&tmp, peer, peerseg, 8));
PUTGETPINGPONG(put_block_active, GASNET_BLOCKUNTIL, gasnet_put(peer, peerseg, &tmp, 8));
PUTGETPINGPONG(get_block_active, GASNET_BLOCKUNTIL, gasnet_get(&tmp, peer, peerseg, 8));

void * poll_passive(void *args) {
  int mythread = (int)(intptr_t)args;
  signal_done = 0;
  thread_barrier();
  while (!signal_done) gasnet_AMPoll();
  thread_barrier();
  return NULL;
}
void * block_passive(void *args) {
  int mythread = (int)(intptr_t)args;
  signal_done = 0;
  thread_barrier();
  GASNET_BLOCKUNTIL(signal_done);
  thread_barrier();
  return NULL;
}

typedef struct {
  const char *desc;
  threadmain_t activefunc;
  threadmain_t passivefunc;
} fntable_t;

fntable_t fntable[] = {
  { "AM Ping-pong vs. spin-AMPoll()", ampingpong_poll_active, poll_passive },
  { "AM Ping-pong vs. BLOCKUNTIL",    ampingpong_block_active, block_passive },
  { "gasnet_put vs. spin-AMPoll()", put_poll_active, poll_passive },
  { "gasnet_put vs. BLOCKUNTIL",    put_block_active, block_passive },
  { "gasnet_get vs. spin-AMPoll()", get_poll_active, poll_passive },
  { "gasnet_get vs. BLOCKUNTIL",    get_block_active, block_passive }
};
#define NUM_FUNC (sizeof(fntable)/sizeof(fntable_t))

int
main(int argc, char **argv)
{
	int 		maxthreads = 4;
	int		i;
        int             fnidx;
	pthread_t	*tids;
        int             tcountentries;
        threadcnt_t     *tcount, *ptcount;

	GASNET_Safe(gasnet_init(&argc, &argv));
    	GASNET_Safe(gasnet_attach(htable, HANDLER_TABLE_SIZE,
		    TEST_SEGSZ, TEST_MINHEAPOFFSET));
        TEST_SEG(gasnet_mynode()); /* ensure we got the segment requested */
        TEST_DEBUGPERFORMANCE_WARNING();

	if (argc >= 2) maxthreads = atoi(argv[1]);
	if (argc >= 3) iters = atoi(argv[2]);

	if (maxthreads > TEST_MAXTHREADS || maxthreads < 1) {
	  printf("Threads must be between 1 and %i\n", TEST_MAXTHREADS);
	  gasnet_exit(-1);
	}
        if (gasnet_nodes() % 2 != 0) {
          MSG("Need an even number of nodes for this test.");
	  gasnet_exit(-1);
        }
        if (gasnet_mynode() == 0) {
          MSG("Running testcontend with 1..%i threads and %i iterations", maxthreads, iters);
        }
        tcountentries = 3 * maxthreads;
        tcount = test_malloc(tcountentries * sizeof(threadcnt_t));
        ptcount = tcount;
        for (i = 1; i <= maxthreads; i++) { ptcount->activecnt = i; ptcount->passivecnt = 1; ptcount++; }
        for (i = 1; i <= maxthreads; i++) { ptcount->activecnt = 1; ptcount->passivecnt = i; ptcount++; }
        for (i = 1; i <= maxthreads; i++) { ptcount->activecnt = i; ptcount->passivecnt = i; ptcount++; }
        tt_tids = test_malloc(maxthreads * sizeof(pthread_t));
        peer = gasnet_mynode() ^ 1;
        amactive = (gasnet_mynode() % 2 == 0);

        peerseg = TEST_SEG(peer);

        for (fnidx = 0; fnidx < NUM_FUNC; fnidx++) {
	  int 	tcountpos;
	  void	*ret;

          if (gasnet_mynode() == 0) {
            MSG("--------------------------------------------------------------------------");
            MSG("Running test %s", fntable[fnidx].desc);
            MSG("--------------------------------------------------------------------------");
            MSG(" Active-end threads\tPassive-end threads\tIterTime\tTotalTime");
            MSG("--------------------------------------------------------------------------");
          }
          BARRIER();
          for (tcountpos = 0; tcountpos < tcountentries; tcountpos++) {
            threadmain_t mainfn = amactive ? fntable[fnidx].activefunc : fntable[fnidx].passivefunc;
            threads = amactive ? tcount[tcountpos].activecnt : tcount[tcountpos].passivecnt;
            BARRIER();
	    for (i = 0; i < threads; i++) {
              pthread_attr_t attr;
              pthread_attr_init(&attr);
              pthread_attr_setscope(&attr, PTHREAD_SCOPE_SYSTEM);
              if (pthread_create(&tt_tids[i], &attr, mainfn, (void *)(intptr_t)i) != 0) { MSG("Error forking threads\n"); gasnet_exit(-1); }
	    }

	    for (i = 0; i < threads; i++) {
              if (pthread_join(tt_tids[i], &ret) != 0) { MSG("Error joining threads\n"); gasnet_exit(-1); }
	    }
            BARRIER();
            { const char *rpt = getreport();
              if (rpt) MSG("\t   %d\t\t\t  %d\t\t%s", 
                tcount[tcountpos].activecnt, tcount[tcountpos].passivecnt, rpt);
            }
          }
	}

        BARRIER();

	MSG("Tests complete");

        BARRIER();

	gasnet_exit(0);

	return 0;
}

/* Cheap (but functional!) pthread + gasnet barrier */
void
thread_barrier() {
        static pthread_mutex_t	barrier_mutex = PTHREAD_MUTEX_INITIALIZER;
        static pthread_cond_t	barrier_cond = PTHREAD_COND_INITIALIZER;
        static volatile int	barrier_count = 0;
        static int volatile phase = 0;
        pthread_mutex_lock(&barrier_mutex);
        barrier_count++;
        if (barrier_count < threads) {
          int myphase = phase;
          while (myphase == phase) {
                pthread_cond_wait(&barrier_cond, &barrier_mutex);
          }
        } else {  
		/* Now do the gasnet barrier */
		BARRIER();
                barrier_count = 0;
                phase = !phase;
                pthread_cond_broadcast(&barrier_cond);
        }       
        pthread_mutex_unlock(&barrier_mutex);
}

/****************************************************************/
/* AM Handlers */
void ping_shorthandler(gasnet_token_t token) {
  GASNET_Safe(gasnet_AMReplyShort0(token, hidx_pong_shorthandler));
}

void pong_shorthandler(gasnet_token_t token) {
  gasnett_atomic_increment(&pong);
}

void ping_medhandler(gasnet_token_t token, void *buf, size_t nbytes) {
  GASNET_Safe(gasnet_AMReplyMedium0(token, hidx_pong_medhandler, buf, nbytes));
}

void pong_medhandler(gasnet_token_t token, void *buf, size_t nbytes) {
  gasnett_atomic_increment(&pong);
}

void ping_longhandler(gasnet_token_t token, void *buf, size_t nbytes) {
  GASNET_Safe(gasnet_AMReplyLong0(token, hidx_pong_longhandler, buf, nbytes, peerseg));
}

void pong_longhandler(gasnet_token_t token, void *buf, size_t nbytes) {
  gasnett_atomic_increment(&pong);
}

void noop_shorthandler(gasnet_token_t token) {
}

void markdone_shorthandler(gasnet_token_t token) {
  signal_done = 1;
}


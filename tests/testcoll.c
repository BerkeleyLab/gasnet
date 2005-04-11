/*   $Source: /Users/kamil/work/gasnet-cvs2/gasnet/tests/testcoll.c,v $
 *     $Date: 2005/04/11 04:22:59 $
 * $Revision: 1.6.2.2 $
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

int myproc;
int numprocs;
int peerproc;

int *segment;

static int *A, *B, *C, *D, *E, *F, *G;
static int **Av, **Bv, **Cv, **Dv, **Ev, **Fv, **Gv;

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


#define CALL(FUNC,DST,SRC,FLAGS) \
  gasnet_coll_##FUNC(GASNET_TEAM_ALL,DST,SRC,sizeof(int),\
			FLAGS|GASNET_COLL_SRC_IN_SEGMENT|GASNET_COLL_DST_IN_SEGMENT);
#define DEFN(PREFIX, DESC, FLAGS, SUFFIX)                                    \
/* NO/NO - in/out data is not generated/consumed in same barrier phase */    \
void PREFIX##_NONO(int iters, gasnet_node_t root) {                          \
    const char name[] = DESC " NO/NO";                                       \
    int j;                                                                   \
                                                                             \
    MSG0("Starting %s test", name);                                          \
                                                                             \
    for (j = 0; j < iters; ++j) {                                            \
	gasnet_node_t i;                                                     \
	int r = random();                                                    \
                                                                             \
	*LOCAL(A) = (myproc == root) ? r : -1;                               \
	*LOCAL(B) = myproc;                                                  \
	for (i = 0; i < numprocs; ++i) {                                     \
	    LOCAL(D)[i] = i * r + myproc;                                    \
	}                                                                    \
                                                                             \
	BARRIER();                                                           \
                                                                             \
	CALL(broadcast##SUFFIX, ALL(A), ROOT(A),                             \
	     FLAGS | GASNET_COLL_IN_NOSYNC | GASNET_COLL_OUT_NOSYNC);        \
	CALL(gather##SUFFIX, ROOT(C), ALL(B),                                \
	     FLAGS | GASNET_COLL_IN_NOSYNC | GASNET_COLL_OUT_NOSYNC);        \
	CALL(scatter##SUFFIX, ALL(E), ROOT(D),                               \
	     FLAGS | GASNET_COLL_IN_NOSYNC | GASNET_COLL_OUT_NOSYNC);        \
	CALL(gather_all##SUFFIX, ALL(F), ALL(B),                             \
	     FLAGS | GASNET_COLL_IN_NOSYNC | GASNET_COLL_OUT_NOSYNC);        \
	CALL(exchange##SUFFIX, ALL(G), ALL(D),                               \
	     FLAGS | GASNET_COLL_IN_NOSYNC | GASNET_COLL_OUT_NOSYNC);        \
                                                                             \
	BARRIER();                                                           \
                                                                             \
	if (r != *LOCAL(A)) {                                                \
	    MSG("ERROR: %s broadcast validation failed", name);              \
	    gasnet_exit(1);                                                  \
	}                                                                    \
	if (myproc == root) {                                                \
	    for (i = 0; i < numprocs; ++i) {                                 \
		if (LOCAL(C)[i] != i) {                                      \
		    MSG("ERROR: %s gather validation failed", name);         \
		    gasnet_exit(1);                                          \
		}                                                            \
	    }                                                                \
	}                                                                    \
	if (*LOCAL(E) != myproc*r + root) {                                  \
	    MSG("ERROR: %s scatter validation failed", name);                \
	    gasnet_exit(1);                                                  \
	}                                                                    \
	for (i = 0; i < numprocs; ++i) {                                     \
	    if (LOCAL(F)[i] != i) {                                          \
		MSG("ERROR: %s gather_all validation failed", name);         \
	    }                                                                \
	}                                                                    \
	for (i = 0; i < numprocs; ++i) {                                     \
	    if (LOCAL(G)[i] != i + myproc*r) {                               \
		MSG("ERROR: %s exchange validation failed", name);           \
		gasnet_exit(1);                                              \
	    }                                                                \
	}                                                                    \
    }                                                                        \
                                                                             \
    BARRIER(); /* ensure validation completes before next test */            \
}                                                                            \
/* MY/MY - in/out data is generated/consumed locally in same barrier phase */\
void PREFIX##_MYMY(int iters, gasnet_node_t root) {                          \
    const char name[] = DESC " MY/MY";                                       \
    int j;                                                                   \
                                                                             \
    MSG0("Starting %s test", name);                                          \
                                                                             \
    for (j = 0; j < iters; ++j) {                                            \
	gasnet_node_t i;                                                     \
	int r = random();                                                    \
                                                                             \
	*LOCAL(A) = (myproc == root) ? r : -1;                               \
	*LOCAL(B) = myproc;                                                  \
                                                                             \
	CALL(broadcast##SUFFIX, ALL(A), ROOT(A),                             \
	     FLAGS | GASNET_COLL_IN_MYSYNC | GASNET_COLL_OUT_MYSYNC);        \
	if (r != *LOCAL(A)) {                                                \
	    MSG("ERROR: %s broadcast validation failed", name);              \
	    gasnet_exit(1);                                                  \
	}                                                                    \
	CALL(gather##SUFFIX, ROOT(C), ALL(B),                                \
	     FLAGS | GASNET_COLL_IN_MYSYNC | GASNET_COLL_OUT_MYSYNC);        \
	if (myproc == root) {                                                \
	    for (i = 0; i < numprocs; ++i) {                                 \
		if (LOCAL(C)[i] != i) {                                      \
		    MSG("ERROR: %s gather validation failed", name);         \
		    gasnet_exit(1);                                          \
		}                                                            \
		LOCAL(C)[i] *= r;                                            \
	    }                                                                \
	}                                                                    \
	CALL(scatter##SUFFIX, ALL(B), ROOT(C),                               \
	     FLAGS | GASNET_COLL_IN_MYSYNC | GASNET_COLL_OUT_MYSYNC);        \
	if (*LOCAL(B) != myproc*r) {                                         \
	    MSG("ERROR: %s scatter validation failed", name);                \
	    gasnet_exit(1);                                                  \
	}                                                                    \
	CALL(gather_all##SUFFIX, ALL(C), ALL(B),                             \
	     FLAGS | GASNET_COLL_IN_MYSYNC | GASNET_COLL_OUT_MYSYNC);        \
	for (i = 0; i < numprocs; ++i) {                                     \
	    if (LOCAL(C)[i] != i*r) {                                        \
		MSG("ERROR: %s gather_all validation failed", name);         \
		gasnet_exit(1);                                              \
	    }                                                                \
	    LOCAL(C)[i] += myproc;                                           \
	}                                                                    \
	CALL(exchange##SUFFIX, ALL(D), ALL(C),                               \
	     FLAGS | GASNET_COLL_IN_MYSYNC | GASNET_COLL_OUT_MYSYNC);        \
	for (i = 0; i < numprocs; ++i) {                                     \
	    if (LOCAL(D)[i] != i + myproc*r) {                               \
		MSG("ERROR: %s exchange validation failed", name);           \
		gasnet_exit(1);                                              \
	    }                                                                \
	}                                                                    \
    }                                                                        \
                                                                             \
    BARRIER(); /* ensure validation completes before next test */            \
}                                                                            \
/* ALL/ALL - data is generated/consumed *remotely* in same barrier phase */  \
void PREFIX##_ALLALL(int iters, gasnet_node_t root) {                        \
    const char name[] = DESC " ALL/ALL";                                     \
    int j;                                                                   \
    int tmp;                                                                 \
    gasnet_node_t peer;                                                      \
                                                                             \
    MSG0("Starting %s test", name);                                          \
                                                                             \
    peer = ((myproc ^ 1) == numprocs) ? myproc : (myproc ^ 1);               \
                                                                             \
    for (j = 0; j < iters; ++j) {                                            \
	gasnet_node_t i;                                                     \
	int r = random();                                                    \
                                                                             \
	tmp = (peer == root) ? r : -1;                                       \
	gasnet_put(peer, REMOTE(A,peer), &tmp, sizeof(int));                 \
                                                                             \
	CALL(broadcast##SUFFIX, ALL(A), ROOT(A),                             \
	     FLAGS | GASNET_COLL_IN_ALLSYNC | GASNET_COLL_OUT_ALLSYNC);      \
	gasnet_get(&tmp, peer, REMOTE(A,peer), sizeof(int));                 \
	if (tmp != r) {                                                      \
	    MSG("ERROR: %s broadcast validation failed", name);              \
	    gasnet_exit(1);                                                  \
	}                                                                    \
	tmp = peer;                                                          \
	gasnet_put(peer, REMOTE(B,peer), &tmp, sizeof(int));                 \
	CALL(gather##SUFFIX, ROOT(C), ALL(B),                                \
	     FLAGS | GASNET_COLL_IN_ALLSYNC | GASNET_COLL_OUT_ALLSYNC);      \
	gasnet_get_bulk(LOCAL(D), root, REMOTE(C,root), numprocs*sizeof(int));\
	for (i = 0; i < numprocs; ++i) {                                     \
	    if (LOCAL(D)[i] != i) {                                          \
		MSG("ERROR: %s gather validation failed", name);             \
		gasnet_exit(1);                                              \
	    }                                                                \
	}                                                                    \
	BARRIER(); /* to avoid conflict on D */                              \
	tmp = myproc * r;                                                    \
	gasnet_put(root, REMOTE(D,root)+myproc, &tmp, sizeof(int));          \
	CALL(scatter##SUFFIX, ALL(B), ROOT(D),                               \
	     FLAGS | GASNET_COLL_IN_ALLSYNC | GASNET_COLL_OUT_ALLSYNC);      \
	gasnet_get(&tmp, peer, REMOTE(B,peer), sizeof(int));                 \
	if (tmp != peer*r) {                                                 \
	    MSG("ERROR: %s scatter validation failed", name);                \
	    gasnet_exit(1);                                                  \
	}                                                                    \
	BARRIER(); /* to avoid conflict on B */                              \
	tmp = peer*r - 1;                                                    \
	gasnet_put(peer, REMOTE(B,peer), &tmp, sizeof(int));                 \
	CALL(gather_all##SUFFIX, ALL(C), ALL(B),                             \
	     FLAGS | GASNET_COLL_IN_ALLSYNC | GASNET_COLL_OUT_ALLSYNC);      \
	gasnet_get_bulk(LOCAL(D), peer, REMOTE(C,peer), numprocs*sizeof(int));\
	for (i = 0; i < numprocs; ++i) {                                     \
	    if (LOCAL(D)[i] != i*r - 1) {                                    \
		MSG("ERROR: %s gather_all validation failed", name);         \
		gasnet_exit(1);                                              \
	    }                                                                \
	}                                                                    \
	BARRIER(); /* to avoid conflict on C & D */                          \
	for (i = 0; i < numprocs; ++i) {                                     \
	    LOCAL(C)[i] += peer;                                             \
	}                                                                    \
	gasnet_put_bulk(peer, REMOTE(D,peer), LOCAL(C), numprocs*sizeof(int));\
	CALL(exchange##SUFFIX, ALL(C), ALL(D),                               \
	     FLAGS | GASNET_COLL_IN_ALLSYNC | GASNET_COLL_OUT_ALLSYNC);      \
	gasnet_get_bulk(LOCAL(D), peer, REMOTE(C,peer), numprocs*sizeof(int));\
	for (i = 0; i < numprocs; ++i) {                                     \
	    if (LOCAL(D)[i] != i + peer*r - 1) {                             \
		MSG("ERROR: %s exchange validation failed", name);           \
		gasnet_exit(1);                                              \
	    }                                                                \
	}                                                                    \
    }                                                                        \
                                                                             \
    BARRIER(); /* ensure validation completes before next test */            \
}

#define ALL(X)		X
#define ROOT(X)		root, X
#define LOCAL(X)	X
#define REMOTE(X,N)	X
DEFN(testSS, "SINGLE/single-addr", GASNET_COLL_SINGLE,)
#undef ALL
#undef ROOT
#undef LOCAL
#undef REMOTE

#define ALL(X)		(void*const*)X##v
#define ROOT(X)		root, X##v[root]
#define LOCAL(X)	(X##v[myproc])
#define REMOTE(X,N)	X##v[N]
DEFN(testSM, "SINGLE/multi-addr", GASNET_COLL_SINGLE, M)
#undef ALL
#undef ROOT
#undef LOCAL
#undef REMOTE

#define ALL(X)		X##v[myproc]
#define ROOT(X)		root, (myproc==root)?X##v[root]:NULL
#define LOCAL(X)	(X##v[myproc])
#define REMOTE(X,N)	X##v[N]
DEFN(testLS, "LOCAL/single-addr", GASNET_COLL_LOCAL,)
#undef ALL
#undef ROOT
#undef LOCAL
#undef REMOTE

#define ALL(X)		(void*const*)(X##v+myproc)
#define ROOT(X)		root, (myproc==root)?X##v[root]:NULL
#define LOCAL(X)	(X##v[myproc])
#define REMOTE(X,N)	X##v[N]
DEFN(testLM, "LOCAL/multi-addr", GASNET_COLL_LOCAL, M)
#undef ALL
#undef ROOT
#undef LOCAL
#undef REMOTE

/* XXX: Not yet templating the NB tests (simple approach to SM requires VLA) */

void testSS_NB(int iters, gasnet_node_t root) {
    const char name[] = "SINGLE/single-addr NB";
    int j;
    int *X = test_malloc(iters*sizeof(int));
    gasnet_coll_handle_t *h = test_malloc(iters*sizeof(gasnet_coll_handle_t));

    MSG0("Starting %s test", name);

    for (j = 0; j < iters; ++j) {
	X[j] = random();
	A[j] = (myproc == root) ? X[j] : 0;
	h[j] = gasnet_coll_broadcast_nb(GASNET_TEAM_ALL, A+j, root, A+j, sizeof(int),
				      		GASNET_COLL_SINGLE |
				      		GASNET_COLL_IN_MYSYNC |
				      		GASNET_COLL_OUT_ALLSYNC |
				      		GASNET_COLL_SRC_IN_SEGMENT |
				      		GASNET_COLL_DST_IN_SEGMENT);
    }
    gasnet_coll_wait_sync_all(h, iters);
    for (j = 0; j < iters; ++j) {
	if (A[j] != X[j]) {
	    MSG("ERROR: %s broadcast validation failed", name);
	    gasnet_exit(1);
	}
    }

    test_free(X);
    test_free(h);

    BARRIER(); /* final barrier to ensure validation completes before next test */
}

void testSM_NB(int iters, gasnet_node_t root) {
    const char name[] = "SINGLE/multi-addr NB";
    int i, j;
    int **Z = test_malloc(iters * numprocs * sizeof(int *));
    int *X = test_malloc(iters*sizeof(int));
    int *Y = (int *)TEST_MYSEG() + myproc;
    gasnet_coll_handle_t *h = test_malloc(iters*sizeof(gasnet_coll_handle_t));

    for (i = 0; i < iters; ++i) {
      int **p = Z + i*numprocs;
      for (j = 0; j < numprocs; ++j) {
	p[j] = (int *)TEST_SEG(j) + j + i;
      }
    }

    MSG0("Starting %s test", name);

    for (j = 0; j < iters; ++j) {
        int **p = Z + j*numprocs;
	X[j] = random();
	Y[j] = (myproc == root) ? X[j] : 0;
	assert(&(Y[j]) == p[myproc]);
	h[j] = gasnet_coll_broadcastM_nb(GASNET_TEAM_ALL, (void*const*)p, root, p[root], sizeof(int),
				      		GASNET_COLL_SINGLE |
				      		GASNET_COLL_IN_MYSYNC |
				      		GASNET_COLL_OUT_ALLSYNC |
				      		GASNET_COLL_SRC_IN_SEGMENT |
				      		GASNET_COLL_DST_IN_SEGMENT);
    }
    gasnet_coll_wait_sync_all(h, iters);
    for (j = 0; j < iters; ++j) {
	if (Y[j] != X[j]) {
	    MSG("ERROR: %s broadcast validation failed", name);
	    gasnet_exit(1);
	}
    }

    test_free(Z);
    test_free(X);
    test_free(h);

    BARRIER(); /* final barrier to ensure validation completes before next test */
}

void testLS_NB(int iters, gasnet_node_t root) {
    const char name[] = "LOCAL/single-addr NB";
    int j;
    int *X = test_malloc(iters*sizeof(int));
    int *Y = (int *)TEST_MYSEG() + myproc;
    gasnet_coll_handle_t *h = test_malloc(iters*sizeof(gasnet_coll_handle_t));

    MSG0("Starting %s test", name);

    for (j = 0; j < iters; ++j) {
	X[j] = random();
	Y[j] = (myproc == root) ? X[j] : 0;
	h[j] = gasnet_coll_broadcast_nb(GASNET_TEAM_ALL, Y+j, root, (myproc == root) ? Y+j : NULL, sizeof(int),
				      		GASNET_COLL_LOCAL |
				      		GASNET_COLL_IN_MYSYNC |
				      		GASNET_COLL_OUT_ALLSYNC |
				      		GASNET_COLL_SRC_IN_SEGMENT |
				      		GASNET_COLL_DST_IN_SEGMENT);
    }
    gasnet_coll_wait_sync_all(h, iters);
    for (j = 0; j < iters; ++j) {
	if (Y[j] != X[j]) {
	    MSG("ERROR: %s broadcast validation failed", name);
	    gasnet_exit(1);
	}
    }

    test_free(X);
    test_free(h);

    BARRIER(); /* final barrier to ensure validation completes before next test */
}

void testLM_NB(int iters, gasnet_node_t root) {
    const char name[] = "LOCAL/multi-addr NB";
    int i, j;
    int **Z = test_malloc(iters*sizeof(int *));
    int *X = test_malloc(iters*sizeof(int));
    int *Y = (int *)TEST_MYSEG() + myproc;
    gasnet_coll_handle_t *h = test_malloc(iters*sizeof(gasnet_coll_handle_t));

    for (i = 0; i < iters; ++i) {
      Z[i] = &Y[i];
    }

    MSG0("Starting %s test", name);

    for (j = 0; j < iters; ++j) {
	X[j] = random();
	Y[j] = (myproc == root) ? X[j] : 0;
	h[j] = gasnet_coll_broadcastM_nb(GASNET_TEAM_ALL, (void*const*)(Z+j), root, (myproc == root) ? Y+j : NULL, sizeof(int),
				      		GASNET_COLL_LOCAL |
				      		GASNET_COLL_IN_MYSYNC |
				      		GASNET_COLL_OUT_ALLSYNC |
				      		GASNET_COLL_SRC_IN_SEGMENT |
				      		GASNET_COLL_DST_IN_SEGMENT);
    }
    gasnet_coll_wait_sync_all(h, iters);
    for (j = 0; j < iters; ++j) {
	if (Y[j] != X[j]) {
	    MSG("ERROR: %s broadcast validation failed", name);
	    gasnet_exit(1);
	}
    }

    test_free(Z);
    test_free(X);
    test_free(h);

    BARRIER(); /* final barrier to ensure validation completes before next test */
}

int main(int argc, char **argv)
{
    int iters = 0;
    gasnet_node_t i;
   
    /* call startup */
    GASNET_Safe(gasnet_init(&argc, &argv));
    GASNET_Safe(gasnet_attach(NULL, 0, TEST_SEGSZ_REQUEST, TEST_MINHEAPOFFSET));

    /* get SPMD info */
    myproc = gasnet_mynode();
    numprocs = gasnet_nodes();

    if (!myproc)
	print_testname("testcoll", numprocs);

    if (argc > 1) {
      iters = atoi(argv[1]);
    }
    if (iters < 1) {
      iters = 1000;
    }
    
    if (myproc == 0) {
	printf("Running coll test(s) with %d iterations.\n", iters);
    }
    gasnet_coll_init(NULL, 0, NULL, 0, 0);

    segment = (int *) TEST_MYSEG();

    MSG("running.");
    BARRIER();

    srandom(1);

    Av = test_malloc(numprocs * sizeof(int *));
    Bv = test_malloc(numprocs * sizeof(int *));
    Cv = test_malloc(numprocs * sizeof(int *));
    Dv = test_malloc(numprocs * sizeof(int *));
    Ev = test_malloc(numprocs * sizeof(int *));
    Fv = test_malloc(numprocs * sizeof(int *));
    Gv = test_malloc(numprocs * sizeof(int *));

    /* Carve some variables out of the segment: */
    A = segment;	/* int [1] */
    B = A + 1;		/* int [1] */
    C = B + 1;		/* int [N] */
    D = C + numprocs;	/* int [N] */
    E = D + numprocs;	/* int [1] */
    F = E + 1;		/* int [N] */
    G = F + numprocs;	/* int [N] */

    /* The unaligned eqivalents as arrays of pointers: */
    /* Using (TEST_SEG(i) + i) yields unaligned even when the segments are aligned.
       This is to help catch any case where addresses might have been misused that
       might go undetected if the addresses were aligned */
    for (i = 0; i < numprocs; ++i) {
	Av[i] = (int *)TEST_SEG(i) + i;
	Bv[i] = Av[i] + 1;
	Cv[i] = Bv[i] + 1;
	Dv[i] = Cv[i] + numprocs;
	Ev[i] = Dv[i] + numprocs;
	Fv[i] = Ev[i] + 1;
	Gv[i] = Fv[i] + numprocs;
    }

    for (i = 0; i < numprocs; ++i) {
      MSG0("Running tests with root = %d", (int)i);

#if GASNET_ALIGNED_SEGMENTS == 1
      testSS_NONO(iters, i);
      testSS_MYMY(iters, i);
      testSS_ALLALL(iters, i);
      testSS_NB(iters, i);
#endif	/* Aligned segments */
      testSM_NONO(iters, i);
      testSM_MYMY(iters, i);
      testSM_ALLALL(iters, i);
      testSM_NB(iters, i);
      testLS_NONO(iters, i);
      testLS_MYMY(iters, i);
      testLS_ALLALL(iters, i);
      testLS_NB(iters, i);
      testLM_NONO(iters, i);
      testLM_MYMY(iters, i);
      testLM_ALLALL(iters, i);
      testLM_NB(iters, i);
    }

    BARRIER();

    test_free(Av);
    test_free(Bv);
    test_free(Cv);
    test_free(Dv);
    test_free(Ev);
    test_free(Fv);
    test_free(Gv);

    MSG("done.");

    gasnet_exit(0);

    return 0;

}
/* ------------------------------------------------------------------------------------ */

#include "shmem_tests.h"

#define ITERS	10000000

#ifndef DATA
#define DATA int
#endif

static int  flag;

DATA * input;
static DATA* dest; 
unsigned int size;

uint64_t
test_store_shmem(int pe, int iters)
{
    uint64_t	start, end;
    int		i;

    start = TimeStamp();
    for (i = 0; i < iters; i++) {
	shmem_put(dest, input, size * sizeof(DATA), pe);
	shmem_quiet();
    }
    end = TimeStamp();

    return (end-start);
}

uint64_t
test_load_shmem(int pe, int iters)
{
    uint64_t	start, end;
    int		i, val;

    start = TimeStamp();
    for (i = 0; i < iters; i++)
      shmem_get(dest, input, size * sizeof(DATA), pe);
    end = TimeStamp();

    return (end-start);
}

uint64_t
test_store(int pe, int iters)
{
    uint64_t	start, end;
    int		i;
    DATA* p;

    /* First get remote pointer */


    start = TimeStamp();
    for (i = 0; i < iters; i++) {
      p = (DATA *) shmem_ptr(dest, pe);
      bcopy(input, p, size * sizeof(DATA));
    }
    end = TimeStamp();

    shmem_quiet();

    return (end-start);
}

uint64_t
test_load(int pe, int iters)
{
    uint64_t	start, end;
    int	         i;
    DATA* p;

    start = TimeStamp();
    for (i = 0; i < iters; i++) {
      p = (DATA *) shmem_ptr(dest, pe);
      bcopy(p, input, size * sizeof(DATA));
    }
    end = TimeStamp();

    return (end-start);
}

int
main(int argc, char **argv)
{
    uint64_t	elapsed;
    unsigned int mype, numpes, peer, iters;

    if (argc < 3) {
	    printf("%s <iters> <data size>\n", argv[0]);
	    return 1;
    }
    else {
      iters = atoi(argv[1]);
      size = atoi(argv[2]);
    }
    
    input = (DATA *) malloc(size * sizeof(DATA));
    dest = (DATA *) shmalloc(size * sizeof(DATA));

    printf("Running shmem overhead test for %d iterations\n", iters);
    fflush(stdout);

    mype = shmem_my_pe();
    peer = mype ^ 1;
    numpes = shmem_n_pes();

    shmem_barrier_all();

    if ((mype & 0x1) && numpes > 1) {
	/* Remote Store */
	elapsed = test_store_shmem(peer, iters);
	printf("%3d> Remote shmem  store tot=%f s, us/iter=%8.3f us\n",
		    mype, elapsed / 1000000.0, (float) elapsed / iters);

	elapsed = test_store(peer, iters);
	printf("%3d> Remote direct store tot=%f s, us/iter=%8.3f us\n",
		    mype, elapsed / 1000000.0, (float) elapsed / iters);

	shmem_barrier_all();

	/* Remote Load */
	elapsed = test_load_shmem(peer, iters);
	printf("%3d> Remote shmem  load  tot=%f s, us/iter=%8.3f us\n",
		    mype, elapsed / 1000000.0, (float) elapsed / iters);

	elapsed = test_load(peer, iters);
	printf("%3d> Remote direct load  tot=%f s, us/iter=%8.3f us\n",
		    mype, elapsed / 1000000.0, (float) elapsed / iters);

    }
    else {
	shmem_barrier_all();
    }

    shmem_barrier_all();

    if (mype & 0x1) {
	/* Local Store */
	elapsed = test_store_shmem(mype, iters);
	printf("%3d> Local  shmem  store tot=%f s, us/iter=%8.3f us\n",
		    mype, elapsed / 1000000.0, (float) elapsed / iters);

	elapsed = test_store(mype, iters);
	printf("%3d> Local  direct store tot=%f s, us/iter=%8.3f us\n",
		    mype, elapsed / 1000000.0, (float) elapsed / iters);

	shmem_barrier_all();

	/* Local Load */
	elapsed = test_load_shmem(mype, iters);
	printf("%3d> Local  shmem  load  tot=%f s, us/iter=%8.3f us\n",
		    mype, elapsed / 1000000.0, (float) elapsed / iters);

	elapsed = test_load(mype, iters);
	printf("%3d> Local  direct load  tot=%f s, us/iter=%8.3f us\n",
		    mype, elapsed / 1000000.0, (float) elapsed / iters);

    }
    else {
	shmem_barrier_all();
    }

    shmem_barrier_all();

}




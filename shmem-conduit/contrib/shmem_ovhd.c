#include "shmem_tests.h"

#define ITERS	10000000

static int  flag;

uint64_t
test_store_shmem(int pe, int iters)
{
    uint64_t	start, end;
    int		i;

    start = TimeStamp();
    for (i = 0; i < iters; i++) {
	shmem_int_p(&flag, i, pe);
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
	val = shmem_int_g(&flag, pe);
    end = TimeStamp();

    return (end-start);
}

uint64_t
test_store(int pe, int iters)
{
    uint64_t	start, end;
    int		*p, i;

    /* First get remote pointer */
    p = (int *) shmem_ptr(&flag, pe);

    start = TimeStamp();
    for (i = 0; i < iters; i++) {
	*p = i;
    }
    end = TimeStamp();

    shmem_quiet();

    return (end-start);
}

uint64_t
test_load(int pe, int iters)
{
    uint64_t	start, end;
    int		*p, val, i;

    /* First get remote pointer */
    p = (int *) shmem_ptr(&flag, pe);

    start = TimeStamp();
    for (i = 0; i < iters; i++)
	val = *p;
    end = TimeStamp();

    return (end-start);
}

int
main(int argc, char **argv)
{
    uint64_t	elapsed;
    int		mype, numpes, peer, iters;

    if (argc < 2) {
	    printf("%s <iters>\n", argv[0]);
	    return 1;
    }
    else
	    iters = atoi(argv[1]);

    printf("Running shmem overhead test for %d iterations\n", iters);
    fflush(stdout);

    mype = shmem_my_pe();
    peer = mype ^ 1;
    numpes = shmem_n_pes();

    shmem_barrier_all();

    if ((mype & 0x1) && numpes > 1) {
	/* Remote Store */
	elapsed = test_store_shmem(peer, iters);
	printf("%3d> Remote shmem  store tot=%12d, us/iter=%8.3f us\n",
		    mype, elapsed, (float) elapsed / 1000000.0);

	elapsed = test_store(peer, iters);
	printf("%3d> Remote direct store tot=%12d, us/iter=%8.3f us\n",
		    mype, elapsed, (float) elapsed / 1000000.0);

	shmem_barrier_all();

	/* Remote Load */
	elapsed = test_load_shmem(peer, iters);
	printf("%3d> Remote shmem  load  tot=%12d, us/iter=%8.3f us\n",
		    mype, elapsed, (float) elapsed / 1000000.0);

	elapsed = test_load(peer, iters);
	printf("%3d> Remote direct load  tot=%12d, us/iter=%8.3f us\n",
		    mype, elapsed, (float) elapsed / 1000000.0);

    }
    else {
	shmem_barrier_all();
    }

    shmem_barrier_all();

    if (mype & 0x1) {
	/* Local Store */
	elapsed = test_store_shmem(mype, iters);
	printf("%3d> Local  shmem  store tot=%12d, us/iter=%8.3f us\n",
		    mype, elapsed, (float) elapsed / 1000000.0);

	elapsed = test_store(mype, iters);
	printf("%3d> Local  direct store tot=%12d, us/iter=%8.3f us\n",
		    mype, elapsed, (float) elapsed / 1000000.0);

	shmem_barrier_all();

	/* Local Load */
	elapsed = test_load_shmem(mype, iters);
	printf("%3d> Local  shmem  load  tot=%12d, us/iter=%8.3f us\n",
		    mype, elapsed, (float) elapsed / 1000000.0);

	elapsed = test_load(mype, iters);
	printf("%3d> Local  direct load  tot=%12d, us/iter=%8.3f us\n",
		    mype, elapsed, (float) elapsed / 1000000.0);

    }
    else {
	shmem_barrier_all();
    }

    shmem_barrier_all();

}




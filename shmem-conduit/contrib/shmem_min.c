#include <stdio.h>
#include <stdlib.h>
#ifdef _CRAY
#include <mpp/shmem.h>
#else
#include <shmem.h>
#endif

int
main()
{
	static long pSync[_SHMEM_COLLECT_SYNC_SIZE];
	static long pWork[_SHMEM_REDUCE_MIN_WRKDATA_SIZE];

	long	min_all;
	long	min_my;
	int	i, mype;

	start_pes(0);

	mype = shmem_my_pe();

	srand((long) (mype + 702) / (mype+1));
	min_my = (long) rand() * 702;

	printf("%d> %d\n", mype, min_my);

	for (i = 0; i < _SHMEM_REDUCE_SYNC_SIZE; i++)
		pSync[i] = _SHMEM_REDUCE_SYNC_SIZE*_SHMEM_SYNC_VALUE;

	shmem_barrier_all();

	shmem_long_min_to_all(&min_all, &min_my, 
			      1, 0, 0, _num_pes(), pWork, pSync);

	if (mype == 0) {
		printf("min across all is %d\n", min_all);
		fflush(stdout);
	}
}


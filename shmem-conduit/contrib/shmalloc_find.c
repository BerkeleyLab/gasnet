#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <time.h>
#include <sys/time.h>
#ifdef _CRAY
#include <mpp/shmem.h>
#else
#include <inttypes.h>
#include <shmem.h>
#endif

#define GASNETC_SHMALLOC_SIZE_INIT	(1<<30)
#define GASNETC_SHMALLOC_GRANULARITY	(100<<20)

/*
_SC_PAGESIZE
*/

static size_t	gasnetc_pagesize;

#define GASNETI_ALIGNDOWN(p,P)    ((uintptr_t)(p)&~((uintptr_t)(P)-1))

#define GASNETC_PAGESIZE	   gasnetc_pagesize
#define GASNETC_PAGE_ALIGNDOWN(p)  (GASNETI_ALIGNDOWN(p,GASNETC_PAGESIZE))

typedef
struct _seginfo {
	void	*addr;
	size_t	 size;
}
seginfo_t;

static
int64_t 
TimeStamp()
{
        int64_t         retval;
        struct timeval  tv;

        if (gettimeofday(&tv, NULL)) {
                perror("gettimeofday");
                abort();
        }
        retval = ((int64_t)tv.tv_sec) * 1000000 + tv.tv_usec;
        return retval;
}

static
seginfo_t
gasnetc_SHMallocBinarySearch(size_t low, size_t high)
{
	seginfo_t	si;

	if (high - low <= GASNETC_SHMALLOC_GRANULARITY) {
		si.addr = NULL;
		si.size = 0;
		return si;
	}

	si.size = GASNETC_PAGE_ALIGNDOWN(low + (high-low)/2);

	/* possibly use shmemalign() */
	si.addr = (void *) shmalloc(si.size);

	if (si.addr == NULL)
		return gasnetc_SHMallocBinarySearch(low, si.size);
	else {
		seginfo_t	si_temp;

		shfree(si.addr);
		si_temp = gasnetc_SHMallocBinarySearch(si.size, high);
		if (si_temp.size)
			return si_temp;
		else
			return si;
	}
}

static
seginfo_t
gasnetc_SHMallocSegmentSearch(size_t maxsz)
{
	seginfo_t	si;
	int64_t		start, end;

	if (_my_pe() == 0)
		printf("sizeof(size_t)=%d, maxsiz = %lu, pagesize=%d\n\n", 
			sizeof(size_t), maxsz, gasnetc_pagesize);

	start = TimeStamp();
	si = gasnetc_SHMallocBinarySearch(0UL, maxsz);
	end = TimeStamp();

	if (_my_pe() == 0)
		printf("shmalloc search for %d bytes (max=%lu) took %d us\n", 
		    si.size, maxsz, (end-start));

	return si;
}

int
main()
{
	seginfo_t	si;
	size_t		maxsz = (size_t)(64UL<<30);

	gasnetc_pagesize = (size_t) sysconf(_SC_PAGESIZE);
	start_pes(0);

	if (_my_pe() == 0)
		printf("sizeof(size_t)=%d, maxsiz = %lu, pagesize=%d\n\n", 
			sizeof(size_t), maxsz, gasnetc_pagesize);

	/* 64 gigs?! */
	si = gasnetc_SHMallocSegmentSearch(maxsz);

	if (_my_pe() == 0)
		printf("%d> segment is at 0x%p of size %d\n", 
		    _my_pe(), si.addr, si.size);
}


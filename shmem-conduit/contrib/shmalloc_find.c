#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <time.h>
#include <sys/time.h>
#ifdef _CRAY
#include <sys/sv2/apteamctl.h>
#include <mpp/shmem.h>
#include <sys/resource.h>
#else
#include <inttypes.h>
#include <shmem.h>
#endif

#define GASNETC_SHMALLOC_SIZE_INIT	(1<<30)
#define GASNETC_SHMALLOC_GRANULARITY	(100<<20)

extern int errno;

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

#ifdef _CRAY
seginfo_t
gasnetc_SegmentInit()
{
    struct rlimit   rss;

    memset(&rss, 0, sizeof(rss)); 

    if (getrlimit(RLIMIT_RSS, &rss) < 0) {
	    perror("Failed to query for RSS");
	    exit(-1);
    }
    printf("RLIMIT_RSS: soft=%l, hard=%l\n", (long) rss.rlim_cur, (long) rss.rlim_max);
}
#endif

int
main()
{
    seginfo_t	si;
    size_t	maxsz = (size_t)(64UL<<30);

#ifdef _CRAY
    /* 
     * Cray recommended way of obtaining text and other (data?) pagesize, as
     * this can be set at runtime.
     */
    {	
	ApTeam_t apt;
	memset(&apt, 0, sizeof(apt)); 
	s = apteamctl(ApTeam_Status, 0, 0, &apt); 

	/* other_pagesize holds non-text, text_pagesize is text pages,
	 * pedepth holds # of SSPs per PE, and pecount holds # of PEs in app
	 */
	if (s < 0) {
		perror("Failed to query ApTeam");
		return -1;
	}

	gasnetc_pagesize = (size_t) (1<<apt.other_pagesize);

	printf("Pagesize is %d, with %d SSPs per PE over %d PEs\n",
			gasnetc_pagesize, apt.pedepth, apt.pecount);
    }
#endif

	start_pes(0);

	if (_my_pe() == 0)
		printf("sizeof(size_t)=%d, maxsiz = %lu, pagesize=%d\n\n", 
			sizeof(size_t), maxsz, gasnetc_pagesize);

#if 0
	/* 64 gigs?! */
	si = gasnetc_SHMallocSegmentSearch(maxsz);

	if (_my_pe() == 0)
		printf("%d> segment is at 0x%p of size %d\n", 
		    _my_pe(), si.addr, si.size);
#endif
}


#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <time.h>
#include <assert.h>
#include <sys/time.h>
#include <inttypes.h>

#if defined(_CRAY)
  #include <mpp/shmem.h>
  #include <intrinsics.h>
  #include <stdint.h>
  #define SHMEM_INIT()	start_pes(0)
  #define SHMEM_MEMBAR() 
#elif defined(__ECC)	/* NO SGI or ALTIX symbols ! */
  #include <mpp/shmem.h>
  #include <ia64intrin.h>
  #warning SGI has no support for mswap!
  #define SHMEM_NOMSWAP
  #define shmem_int_mswap  shmem_long_cswap
  #define SHMEM_INIT()	start_pes(0)
  #define SHMEM_MEMBAR()    do { __memory_barrier(); __mf(); } while (0)
#elif defined(__digital__)
  #include <c_asm.h>
  #define SHMEM_INIT()	shmem_init()
  #define SHMEM_MEMBAR()    asm("mb")
  #include <shmem.h>
#else
  #error Unknown shmem platform
#endif

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



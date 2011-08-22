#include <stdint.h>
#include <assert.h>
#include <gasnet_gemini.h>
#include <pmi_cray.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <gni_pub.h>

uint32_t gc_num_ranks;
uint32_t gc_rank;

#ifndef MPI_SUCCESS
#define MPI_SUCCESS 0
#endif

long int mygetenv(const char *name)
{
  char *valuestring = getenv(name);
  long int value;
  char recast[20];
  if (valuestring == NULL) return (-1);
  value = atol(valuestring);
  //fprintf(stderr, "rank %d, get(%s) -> %s, %ld\n", rank, name, valuestring, value);
  return(value);
}

unsigned int *MPID_UGNI_AllAddr;

void *gather_nic_addresses(void)
{
  gni_return_t status;
  uint32_t myaddress, pmiaddress;
  uint32_t cpu_id;
  uint32_t device_id = GNIT_Device_Id();
  int i;
  MPID_UGNI_AllAddr = gasneti_malloc(gc_num_ranks * sizeof(uint32_t));
  gasneti_assert(MPID_UGNI_AllAddr);
  status = GNI_CdmGetNicAddress(device_id, &myaddress, &cpu_id);
  if (status != GNI_RC_SUCCESS) {
    GNIT_Abort();
  } else {
    //fprintf(stderr, "rank %d Cdm_GetNicAddress %x\n", rank, myaddress);
  }
  pmiaddress = mygetenv("PMI_GNI_LOC_ADDR");
  if (pmiaddress != myaddress) {
#if GASNETC_DEBUG
    fprintf(stderr, "rank %d PMI_GNI_LOC_ADDR is %d, using it\n", gc_rank, pmiaddress);
#endif
    myaddress = pmiaddress;
  } else {
    //fprintf(stderr, "rank %d PMI_GNI_LOC_ADDR is %d, same\n", rank, pmiaddress);
  }

  GNIT_Allgather(&myaddress, sizeof(uint32_t), MPID_UGNI_AllAddr);
  if (MPID_UGNI_AllAddr[gc_rank] != myaddress) {
    fprintf(stderr, "rank %d gathernic got %x should be %x\n", gc_rank,
	    MPID_UGNI_AllAddr[gc_rank], myaddress);
    GNIT_Abort();
  }
  /*
  fprintf(stderr, "rank %d addresses ", rank);
  for (i = 0; i < gc_num_ranks; i += 1) {
    fprintf(stderr, " %x", MPID_UGNI_AllAddr[i]);
  }
  fprintf(stderr, "\n");
  */
  return(MPID_UGNI_AllAddr);
}


int gc_init(gasnet_node_t *sizep, gasnet_node_t *rankp, char **errorstringp)
{

   int spawned;
   int appnum;
   int ret;

   /* rank and gc_num_ranks are unsigned, but PMI wants signed, go figure */
   if ((ret = PMI2_Init (&spawned, (int *) &gc_num_ranks, (int *) &gc_rank, &appnum)) 
       != MPI_SUCCESS) {
     *errorstringp = (char *) "Failure in PMI2_Init\n";
     return(GASNET_ERR_NOT_INIT);
   }
   *sizep = gc_num_ranks;
   *rankp = gc_rank;
   return(GASNET_OK);
}


/* Algorithm
 * Get the number of ranks on my supernode
 * Allocate an array of that size node numbers
 * Get the ranks of all local nodes
 * Find the smallest
 * AllGather that number so everyone has an array indexed by rank of the
 *  smallest rank on their supernode
 */



void GNIT_Job_size(int *nranks)
{
  *nranks = gc_num_ranks;
}

void GNIT_Rank(int *inst_id)
{
  *inst_id = gc_rank;
}

char GNIT_Ptag(void)
{
  return(mygetenv("PMI_GNI_PTAG"));
}

int GNIT_Cookie(void)
{
  return(mygetenv("PMI_GNI_COOKIE"));
}

int GNIT_Device_Id(void)
{
  return(mygetenv("PMI_GNI_DEV_ID"));
}

void GNIT_Allgather(void *local, long length, void *global)
{
  long itemsize = 1 + 1 + (length / sizeof(long)); // length in longs
  long *unsorted = gasneti_malloc(itemsize * sizeof(long) * gc_num_ranks);
  long *sorted = gasneti_malloc(itemsize * sizeof(long) * gc_num_ranks);
  uint32_t peer;
  uint32_t i;
  int status;
  gasneti_assert(unsorted);
  gasneti_assert(sorted);
  /* use beginning of sorted for the local data */
  sorted[0] = gc_rank;
  memcpy(&sorted[1], local, length);  
  /* initialize the unsorted array */
  for (i = 0; i < itemsize * gc_num_ranks; i += 1) {
    unsorted[i] = -1;
  }
  status = PMI_Allgather(sorted, unsorted, itemsize * sizeof(long));
  if (status != PMI_SUCCESS) {
    fprintf(stderr, "rank %d: PMI_Allgather failed\n", gc_rank);
    GNIT_Abort();
  }
  for (i = 0; i < gc_num_ranks; i += 1) {
    peer = unsorted[i * itemsize];
    if ((peer < 0) || (peer >= gc_num_ranks)) {
      fprintf(stderr, "rank %d PMI_Allgather failed, item %d is %d\n", 
	      gc_rank, i, peer);
      GNIT_Abort();
    }
    memcpy(&sorted[peer * itemsize], 
	   &unsorted[i * itemsize],
	   itemsize * sizeof(long));
  }
  for (i = 0; i < gc_num_ranks; i += 1) {
    if (sorted[i * itemsize] != i) {
      fprintf(stderr, "rank %d Allgather rank %d missing\n", gc_rank, i);
      GNIT_Abort();
    }
    memcpy((void *) ((uintptr_t) global + (i * length)), &sorted[(i * itemsize) + 1], length);
  }
  /* check own data */
  if (memcmp(local, (void *) ((uintptr_t ) global + (gc_rank * length)), length) != 0) {
    fprintf(stderr, "rank %d, allgather error\n", gc_rank);
    GNIT_Abort();
  }
  gasneti_free(unsorted);
  gasneti_free(sorted);
}


void GNIT_TEST_SUCCESS()
{
  fprintf(stderr, "GNIT_TEST_SUCCESS called\n");
}

void GNIT_Finalize()
{
  PMI2_Finalize();
}


void GNIT_Barrier()
{
  PMI_Barrier();
}

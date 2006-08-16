#include "gasnet.h"
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <sys/time.h>
#include <unistd.h>
#include <fcntl.h>
#include "test.h"

void assert_eq(char *x, char *y, int len, int start, int i, int j, char *msg)
{
  int k;
  int error = 0;
  for(k=0;k < len;k++) {
    if(x[k] != y[k]) {
      error=1;
      break;
    }
  }
  if(error) {
    ERR("FAILURE %s outer iteration %d inner iteration %d starting point = %d length = %d FAILURE\n",msg,i,j,start,len);
  } else {
    MSG("SUCCESS %s outer iteration %d inner iteration %d starting point = %d length = %d SUCCESS\n",msg,i,j,start,len);
  }
}

int main(int argc, char **argv)
{
    int segsize;
    int outer_iterations;
    int inner_iterations;
    int numprocs, myproc;
    int sender_p;
    char *shadow_region_1, *shadow_region_2;
    int i,j;
    char *local_base, *target_base;

    /* call startup */
    GASNET_Safe(gasnet_init(&argc, &argv));

    /* get SPMD info */
    myproc = gasnet_mynode();
    numprocs = gasnet_nodes();

    if(numprocs != 2) {
        printf("This test only runs with 2 nodes\n");
        gasnet_exit(1);
    }
    sender_p = (myproc == 0);

    /* parse arguments */
    if (argc != 4) {
        printf("Usage: %s (segsize) (iterations) (# of sizes per iteration)\n", argv[0]);
        gasnet_exit(1);
    }

    segsize = atoi(argv[1]);
    outer_iterations = atoi(argv[2]);
    inner_iterations = atoi(argv[3]);

    printf("Running with segment size = %d outer iterations=%d inner iterations=%d\n",segsize,outer_iterations, inner_iterations);

    GASNET_Safe(gasnet_attach(NULL, 0, segsize, TEST_MINHEAPOFFSET));
    
    BARRIER();

    /* Allocate two shadow regions the same size as the segment */
    shadow_region_1 = (char *) test_malloc(segsize);
    shadow_region_2 = (char *) test_malloc(segsize);
   
    /* Fill up the shadow region with random data */
    for(i=0;i < segsize;i++) {
      shadow_region_1[i] = (char) TEST_RAND(0,255);
    }
    bzero(shadow_region_2,segsize);

    /* Big loop performing the following */
    if(sender_p) {
      local_base = TEST_SEG(0);
      target_base = TEST_SEG(1);
      for(i=0;i < outer_iterations;i++) {
        /* Pick a starting point anywhere in the segment */
        int starting_point = TEST_RAND(0,(segsize-1));
 
        for(j=0;j < inner_iterations;j++) {
          /* Pick a length */
          int len = TEST_RAND(1,segsize-starting_point);
          int remote_starting_point = TEST_RAND(0,segsize-len);
          int local_starting_point_1 = TEST_RAND(0,segsize-len);
          int local_starting_point_2 = TEST_RAND(0,segsize-len);

          /* Perform operations */
          /* Out of segment put from shadow_region 1 to remote */
          gasnet_put(1,target_base+remote_starting_point,shadow_region_1 + starting_point,len); 
  
          /* In segment get from remote to local segment */
          gasnet_get(local_base+local_starting_point_1,1,target_base+remote_starting_point,len); 
  
          /* Verify */
          assert_eq(shadow_region_1 + starting_point, local_base + local_starting_point_1, len,starting_point,i,j,"Out of segment put + in segment get");
  
          /* Out of segment get from remote to shadow_region_2 (starting from 0) */
          gasnet_get(shadow_region_2+local_starting_point_2,1,target_base+remote_starting_point,len); 
  
          /* Verify */
          assert_eq(shadow_region_2+local_starting_point_2, shadow_region_1 + starting_point, len,starting_point,i,j,"Out of segment get");
        }
      }
    }
    BARRIER();
    gasnet_exit(0);

    return 0;

}
/* ------------------------------------------------------------------------------------ */

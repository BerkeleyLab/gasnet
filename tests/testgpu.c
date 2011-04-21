/* $Source: /Users/kamil/work/gasnet-cvs2/gasnet/tests/Attic/testgpu.c,v $
 * $Date: 2011/04/21 19:07:29 $
 * $Revision: 1.1.4.3 $
 *
 * Description: Point-to-poing communication tests for GASNet GPU
 * extensions. It requires to run with two gasnet nodes/processes.
 *
 * Note: The curret gpu extension implementation and this test doesn't
 * work with PAR build.
 *  
 * Yili Zheng
 * LBNL 2010
 */

#include <gasnet.h>

#define gpu_deviceid_map_fn "/global/homes/y/yzheng/gasnet_gpu_deviceid_map"

#define TEST_SEGSZ_EXPR 256*1024*1024 /* 256 MB segment size */

#define MAGIC_NUM 163

#define MAX_DATA_SIZE 64*1024*1024 /* 1 MB for now */

#define TEST_MAXTHREADS 1
#include <test.h>

#include <gasnet_extended_gpu.h>

#define DEBUG_GPU

int *devA, *devB, **devA_ptrs, **devB_ptrs;
int *hostA, *hostB, **hostA_ptrs, **hostB_ptrs;

void test_host2localgpu(size_t nelems)
{
  int i;
  size_t nbytes;
  gasnet_node_t mynode = gasnet_mynode();

  nbytes = sizeof(int)*nelems;

  /* initialize data */
  for (i=0; i<nelems; i++) {
    hostA[i] = mynode + MAGIC_NUM + i;
    hostB[i] = 0;
  }

  /* put from host to local gpu */
  gasnete_gpu_store(devA, hostA, nbytes);

  /* copy data within the local gpu */
  gasnete_gpu_memcpy(devB, devA, nbytes, gpuMemcpyDeviceToDevice);

  /* get from local gpu to host */
  gasnete_gpu_load(hostB, devB, nbytes);

  /* verify */
  for (i=0; i<nelems; i++) {
    if (hostA[i] != hostB[i]) {
      gasneti_fatalerror("mynode %d test_host2localgpu data transfer error: hostA[%d] %d != hostB[%d] %d!\n",
                         gasnet_mynode(), i, hostA[i], i, hostB[i]);
    }
  }
}

/* Assume only 2 gasnet processes are running */
void test_put_host2gpu(size_t nelems)
{
  gasnet_handle_t h;
  int i, expected_val;
  size_t nbytes;
  gasnet_node_t dstNode;
  gasnet_node_t mynode = gasnet_mynode();

  nbytes = sizeof(int)*nelems;
  dstNode = !mynode; /* 0 <-> 1*/

  /* initialize data */
  for (i=0; i<nelems; i++) {
    hostA[i] = (mynode + MAGIC_NUM) * 3 + i;
    hostB[i] = 0;
  }
 
  BARRIER();

  if (mynode == 0) 
  {
    /* put from host to remote gpu */
    h = gasnete_put_hosttogpu_nb(dstNode, devA_ptrs[dstNode], hostA, nbytes);
    gasnet_wait_syncnb(h);
  }

  BARRIER();

  if (mynode == 1) 
  {
    gasnete_gpu_load(hostB, devA, nbytes);
    
    /* verify */
    for (i=0; i<nelems; i++) {
      expected_val = (dstNode + MAGIC_NUM) * 3 + i;
      if (hostB[i] != expected_val) {
        gasneti_fatalerror("mynode %d test_put_host2gpu data transfer error: hostB[%d] %d != expected_val %d!\n",
                           gasnet_mynode(), i, hostB[i], expected_val);
      }
    }
  }
  BARRIER();
}

/* Assume only 2 gasnet processes are running */
void test_get_host2gpu(size_t nelems)
{
  gasnet_handle_t h;
  int i, expected_val;
  size_t nbytes;
  gasnet_node_t dstNode;
  gasnet_node_t mynode = gasnet_mynode();

  nbytes = sizeof(int)*nelems;
  dstNode = !mynode; /* 0 <-> 1*/

  /* initialize data */
  for (i=0; i<nelems; i++) {
    hostA[i] = (mynode + MAGIC_NUM) * 5 + i;
    hostB[i] = 0;
  }
 
  BARRIER();
 
  /* put from host to remote gpu */
  h = gasnete_get_hosttogpu_nb(devA, dstNode, hostA_ptrs[dstNode], nbytes);
  gasnet_wait_syncnb(h);

  BARRIER();
  
  gasnete_gpu_load(hostB, devA, nbytes);
  
  /* verify */
  for (i=0; i<nelems; i++) {
    expected_val = (dstNode + MAGIC_NUM) * 5 + i;
    if (hostB[i] != expected_val) {
        gasneti_fatalerror("mynode %d test_get_host2gpu data transfer error: hostB[%d] %d != expected_val %d!\n",
                           gasnet_mynode(), i, hostB[i], expected_val);
    }
  }

  BARRIER();
}

/* Assume only 2 gasnet processes are running */
void test_put_gpu2host(size_t nelems)
{

  gasnet_handle_t h;
  int i, expected_val;
  size_t nbytes;
  gasnet_node_t dstNode;
  gasnet_node_t mynode = gasnet_mynode();

  nbytes = sizeof(int)*nelems;

  /* initialize data */
  for (i=0; i<nelems; i++) {
    hostA[i] = (mynode + MAGIC_NUM ) * 7 + i;
    hostB[i] = 0;
  }
   
  gasnete_gpu_store(devA, hostA, nbytes);

  /* put from gpu to remote host */
  dstNode = !mynode; /* 0 <-> 1*/
  h = gasnete_put_gputohost_nb(dstNode, hostB_ptrs[dstNode], devA, nbytes);
  gasnet_wait_syncnb(h);
    
  BARRIER();

  /* verify */
  for (i=0; i<nelems; i++) {
    expected_val = (dstNode + MAGIC_NUM) * 7 + i;
    if (hostB[i] != expected_val) {
        gasneti_fatalerror("mynode %d test_put_gpu2host data transfer error: hostB[%d] %d != expected_val %d!\n",
                           gasnet_mynode(), i, hostB[i], expected_val);
    }
  }

  BARRIER();
}

/* Assume only 2 gasnet processes are running */
void test_get_gpu2host(size_t nelems)
{

  gasnet_handle_t h;
  int i, expected_val;
  size_t nbytes;
  gasnet_node_t dstNode;
  gasnet_node_t mynode = gasnet_mynode();

  nbytes = sizeof(int)*nelems;

  /* initialize data */
  for (i=0; i<nelems; i++) {
    hostA[i] = (mynode + MAGIC_NUM) * 11 + i;
    hostB[i] = 0;
  }
   
  gasnete_gpu_store(devA, hostA, nbytes);

  BARRIER();

  /* put from gpu to remote host */
  dstNode = !mynode; /* 0 <-> 1*/
  h = gasnete_get_gputohost_nb(hostB, dstNode, devA_ptrs[dstNode], nbytes);
  gasnet_wait_syncnb(h);
    
  BARRIER();

  /* verify */
  for (i=0; i<nelems; i++) {
    expected_val = (dstNode + MAGIC_NUM) * 11 + i;
    if (hostB[i] != expected_val) {
        gasneti_fatalerror("mynode %d test_get_gpu2host data transfer error: hostB[%d] %d != expected_val %d!\n",
                           gasnet_mynode(), i, hostB[i], expected_val);
    }
  }

  BARRIER();
}


void test_put_gpu2gpu(size_t nelems)
{
  gasnet_handle_t h;
  int i, expected_val;
  size_t nbytes;
  gasnet_node_t dstNode;
  gasnet_node_t mynode = gasnet_mynode();

  nbytes = sizeof(int)*nelems;

  /* initialize data */
  for (i=0; i<nelems; i++) {
    hostA[i] = (mynode + MAGIC_NUM) * 13 + i;
    hostB[i] = 0;
  }
   
  dstNode = !mynode; /* 0 <-> 1*/

  gasnete_gpu_store(devA, hostA, nbytes);

  /* put from gpu to remote host */
  h = gasnete_put_gputogpu_nb(dstNode, devB_ptrs[dstNode], devA, nbytes);
  gasnet_wait_syncnb(h);
  
  BARRIER();
  
  gasnete_gpu_load(hostB, devB, nbytes);
  
  /* verify */
  for (i=0; i<nelems; i++) {
    expected_val = (dstNode + MAGIC_NUM) * 13 + i;
    if (hostB[i] != expected_val) {
      gasneti_fatalerror("mynode %d test_put_gpu2gpu data transfer error: hostB[%d] %d != expected_val %d!\n",
                         gasnet_mynode(), i, hostB[i], expected_val);
    }
  }

  BARRIER();
}

void test_get_gpu2gpu(size_t nelems)
{
  gasnet_handle_t h;
  int i, expected_val;
  size_t nbytes;
  gasnet_node_t dstNode;
  gasnet_node_t mynode = gasnet_mynode();

  nbytes = sizeof(int)*nelems;

  /* initialize data */
  for (i=0; i<nelems; i++) {
    hostA[i] = (mynode + MAGIC_NUM) * 17 + i;
    hostB[i] = 0;
  }
   
  dstNode = !mynode; /* 0 <-> 1*/

  gasnete_gpu_store(devA, hostA, nbytes);

  BARRIER();

  /* get from gpu to remote host */
  h = gasnete_get_gputogpu_nb(devB, dstNode, devA_ptrs[dstNode], nbytes);
  gasnet_wait_syncnb(h);
  
  gasnete_gpu_load(hostB, devB, nbytes);
  
  /* verify */
  for (i=0; i<nelems; i++) {
    expected_val = (dstNode + MAGIC_NUM) * 17 + i;
    if (hostB[i] != expected_val) {
      gasneti_fatalerror("mynode %d test_get_gpu2gpu data transfer error: hostB[%d] %d != expected_val %d!\n",
                         gasnet_mynode(), i, hostB[i], expected_val);
    }
  }

  BARRIER();
}


void test_gpu_memset(size_t nelems)
{
}

int main(int argc, char **argv)
{
  int ngpus, mygpu;
  gasnet_node_t mynode, num_nodes;
  int *dev_id_map;

  int i, j, k;
  size_t nelems;

  GASNET_Safe(gasnet_init(&argc, &argv));

  mynode = gasnet_mynode();
  num_nodes = gasnet_nodes();

  GASNET_Safe(gasnet_attach(NULL, 0, TEST_SEGSZ_REQUEST, TEST_MINHEAPOFFSET));

  /* The following collectives initialization only works with SEG build */
  gasnet_coll_init(NULL, 0, NULL, 0, 0);

  test_init("testgpu2", 0 , "");


  assert(num_nodes == 2);

  dev_id_map = test_malloc(sizeof(gasnet_node_t) * num_nodes);
  assert(dev_id_map != NULL);

  gasnete_gpu_read_deviceid_map(gpu_deviceid_map_fn, dev_id_map);

  ngpus = gasnete_gpu_get_device_count();

  mygpu = dev_id_map[mynode]; 

  assert(mygpu < ngpus);
  printf("mynode %d: ngpus %d, mygpu %d\n", mynode, ngpus, mygpu);

  if (mygpu != -1) {
    gasnete_gpu_attach(mygpu); 
    devA = gasnete_gpu_device_alloc(MAX_DATA_SIZE);
    devB = gasnete_gpu_device_alloc(MAX_DATA_SIZE);
  } else {
    devA = devB = NULL; /* no gpu attached */
  }

  /* Allocate memory space on the GASNet client segment for
     communication, half for src and half for dst. */
  devA_ptrs = (int **)TEST_MYSEG(); /* ((sizeof(devA) * gasnet_nodes()) */
  hostA_ptrs = (int **)((char *)devA_ptrs + (sizeof(devA) * gasnet_nodes()));
  hostA = (int *)((char *)hostA_ptrs + (sizeof(hostA) * gasnet_nodes()));

  devB_ptrs = (int **)((char *)TEST_MYSEG() + TEST_SEGSZ / 2); 
  hostB_ptrs = (int **)((char *)devB_ptrs + (sizeof(devB) * gasnet_nodes()));
  hostB =  (int *)((char *)hostB_ptrs + (sizeof(hostB) * gasnet_nodes()));

  /* gather all gpu device pointers */
  {
    gasnete_threaddata_t *_threadinfo = gasnete_mythread(); /* for GASNETE_MYTHREAD */
    gasnet_coll_gather_all(GASNET_TEAM_ALL, devA_ptrs, &devA, sizeof(devA), 
                           GASNET_COLL_LOCAL | GASNET_COLL_SRC_IN_SEGMENT | GASNET_COLL_DST_IN_SEGMENT | GASNET_COLL_IN_MYSYNC |  GASNET_COLL_OUT_MYSYNC
                           GASNETE_THREAD_PASS);
    gasnet_coll_gather_all(GASNET_TEAM_ALL, devB_ptrs, &devB, sizeof(devB), 
                           GASNET_COLL_LOCAL | GASNET_COLL_SRC_IN_SEGMENT | GASNET_COLL_DST_IN_SEGMENT | GASNET_COLL_IN_MYSYNC |  GASNET_COLL_OUT_MYSYNC
                           GASNETE_THREAD_PASS);
    gasnet_coll_gather_all(GASNET_TEAM_ALL, hostA_ptrs, &hostA, sizeof(hostA), 
                           GASNET_COLL_LOCAL | GASNET_COLL_SRC_IN_SEGMENT | GASNET_COLL_DST_IN_SEGMENT | GASNET_COLL_IN_MYSYNC |  GASNET_COLL_OUT_MYSYNC
                           GASNETE_THREAD_PASS);
    gasnet_coll_gather_all(GASNET_TEAM_ALL, hostB_ptrs, &hostB, sizeof(hostB), 
                           GASNET_COLL_LOCAL | GASNET_COLL_SRC_IN_SEGMENT | GASNET_COLL_DST_IN_SEGMENT | GASNET_COLL_IN_MYSYNC |  GASNET_COLL_OUT_MYSYNC
                           GASNETE_THREAD_PASS);
  }

#ifdef DEBUG_GPU
  for (i=0; i<gasnet_nodes(); i++)
    fprintf(stderr, "my node %d, node %d, devA %p, hostA %p, devB %p hostB %p\n",
            mynode, i, devA_ptrs[i], hostA_ptrs[i], devB_ptrs[i], hostB_ptrs[i]);
#endif

  for (nelems=1; nelems<MAX_DATA_SIZE/sizeof(int); nelems *= 2) {
    //test_host2localgpu(nelems);
    //MSG0("test_host2localgpu(%lu) passed.", nelems);

    test_put_host2gpu(nelems);
    MSG0("test_put_host2gpu(%lu) passed.", nelems);

    test_get_host2gpu(nelems);
    MSG0("test_get_host2gpu(%lu) passed.", nelems);

    test_put_gpu2host(nelems);
    MSG0("test_put_gpu2host(%lu) passed.", nelems);

    test_get_gpu2host(nelems);
    MSG0("test_get_gpu2host(%lu) passed.", nelems);

    test_put_gpu2gpu(nelems);
    MSG0("test_put_gpu2gpu(%lu) passed.", nelems);

    test_get_gpu2gpu(nelems);
    MSG0("test_get_gpu2gpu(%lu) passed.", nelems);
  }

  /* all done and clean up */

  if (mygpu != -1) {
    gasnete_gpu_device_free(devA);
    gasnete_gpu_device_free(devB);
  }
  
  MSG0("testgpu2 passed.");

  return 0;
}

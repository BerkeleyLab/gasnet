/*GASNet Collective Tuner*/
/*This small program is intended to probe the target machine and choose among the trees
  that GASNet has and pick the best one and write the result back to a file that is 
  later loaded by every invocation of the GASNet program
*/

#include <stdio.h>
#include <stdlib.h>


#include <gasnet.h>
#include <gasnet_tools.h>
#include <gasnet_coll.h>
#include <gasnet_coll_autotune.h>

/*file for writing out XML information*/
#include <../other/myxml/myxml.h>

#define DEFAULT_PERFORMANCE_ITERS 50

typedef struct {
  int my_local_thread;
  int mythread;
  gasnet_coll_handle_t *hndl;
  uint8_t *mysrc, *mydest;
  uint8_t *node_src, *node_dst;
  char _pad[GASNETT_CACHE_LINE_BYTES];

} thread_data_t;

/* max data size for the test in bytes*/
#define DEFAULT_MAX_DATA_SIZE 32768 

#define PRINT_TIMERS 1
#define VERBOSE_VERIFICATION_OUTPUT 0

/*max_dsize is a variable set in main*/
#define TOTAL_THREADS threads_per_node*gasnet_nodes()

#if 1
#define ERROR_EXIT() gasnet_exit(1)
#else
#define ERROR_EXIT() do {} while(0)
#endif

gasnet_node_t mynode;
gasnet_node_t nodes;
gasnet_image_t threads_per_node;
gasnet_image_t THREADS;

int performance_iters;
size_t max_data_size;

#define TEST_SEGSZ_EXPR (sizeof(int)*(max_data_size*TOTAL_THREADS*threads_per_node*2))
#define SEG_PER_THREAD (sizeof(int)*max_data_size*TOTAL_THREADS)
uint8_t **my_srcs;
uint8_t **my_dsts;
uint8_t **all_srcs;
uint8_t **all_dsts;

char *outputfile = (char*) "./gasnet_coll_tuning_defaults.bin";


#include <test.h>

#define COLL_BARRIER() PTHREAD_BARRIER(threads_per_node)

char* fill_flag_str(int flags, char *outstr) {
  
  if(flags & GASNET_COLL_IN_NOSYNC && flags & GASNET_COLL_OUT_NOSYNC) {
    sprintf(outstr, "no/no");
  } else if(flags & GASNET_COLL_IN_NOSYNC && flags & GASNET_COLL_OUT_MYSYNC) {
    sprintf(outstr, "no/my");
  } else if(flags & GASNET_COLL_IN_NOSYNC && flags & GASNET_COLL_OUT_ALLSYNC) {
    sprintf(outstr, "no/all");
  } else if(flags & GASNET_COLL_IN_MYSYNC && flags & GASNET_COLL_OUT_NOSYNC) {
    sprintf(outstr, "my/no");
  } else if(flags & GASNET_COLL_IN_MYSYNC && flags & GASNET_COLL_OUT_MYSYNC) {
    sprintf(outstr, "my/my");
  } else if(flags & GASNET_COLL_IN_MYSYNC && flags & GASNET_COLL_OUT_ALLSYNC) {
    sprintf(outstr, "my/all");
  } else if(flags & GASNET_COLL_IN_ALLSYNC && flags & GASNET_COLL_OUT_NOSYNC) {
    sprintf(outstr, "all/no");
  } else if(flags & GASNET_COLL_IN_ALLSYNC && flags & GASNET_COLL_OUT_MYSYNC) {
    sprintf(outstr, "all/my");
  } else if(flags & GASNET_COLL_IN_ALLSYNC && flags & GASNET_COLL_OUT_ALLSYNC) {
    sprintf(outstr, "all/all");
  }
  return outstr;
}


void run_MULTI_tree_tests(thread_data_t *td, uint8_t **dst_arr, uint8_t **src_arr,  int root_thread, int in_flags, myxml_node_t *root_xml_node) {
  int s, c, i, f;
  myxml_node_t *addr_mode_node, *current_parent_node = root_xml_node, *temp_node= NULL;
  int *src, *dst;
  int num_tree_classes;
  int num_fanouts; 
  int best_tree; 
  int best_fanout;
  gasnett_tick_t begin,end;
  gasnett_tick_t best_time;
  char buffer[50];

  int flags = in_flags | GASNET_COLL_SRC_IN_SEGMENT|GASNET_COLL_DST_IN_SEGMENT;
  
  if(in_flags & GASNET_COLL_SINGLE) { /*whether or not each node presents one address that is valid for all nodes*/
    addr_mode_node = myxml_createNode(root_xml_node, (char *)"address_mode", (char*) "val", (char*) "single", NULL);
    src = (int*) src_arr[0];
    dst = (int*) dst_arr[0];
  } else { /*each node only gives address that are only valid on the local node*/
    addr_mode_node = myxml_createNode(root_xml_node, (char*) "address_mode", (char*) "val", (char*) "local", NULL);
    src = (int*) td->mysrc;
    dst= (int*) td->mydest;
  }
  


  /*************** BROADCASTM *****************/
  current_parent_node = myxml_createNode(addr_mode_node, (char*) "collective", (char *) "val", (char*) "broadcastM", NULL);
  
  temp_node = current_parent_node;
  
   for(s=1; s<=max_data_size; s*=2) {
    uint32_t best_alg;
    uint32_t num_params;
    uint32_t *param_list;
     MSG0("starting test: %s %d bytes", fill_flag_str(flags, buffer), (int)sizeof(int)*s);
    
    current_parent_node = myxml_createNodeInt(temp_node, (char*) "size", (char *) "start", s, NULL);
    myxml_addAttributeInt(current_parent_node, (char*) "end", (s == max_data_size ? 1<<31 : (s*2)-1));
    
    /*run the gasnet tuner and report back the results!*/
    ganset_coll_tune_generic_op(GASNET_TEAM_ALL, GASNET_COLL_BROADCASTM_OP, dst_arr, src_arr, root_thread, flags, sizeof(int)*s,
                                NULL, NULL, &best_alg, &num_params, &param_list);

    sprintf(buffer, "%d", best_alg);
    myxml_createNode(current_parent_node, (char*) "Best_Alg", NULL, NULL, buffer);
    sprintf(buffer, "%d", num_params);
    myxml_createNode(current_parent_node, (char*) "Num_Params", NULL, NULL, buffer);
    for(c=0; c<num_params; c++) {
      char buff_idx[20];
      sprintf(buff_idx, "param_%d", c);
      sprintf(buffer, "%d", param_list[c]);
      myxml_createNode(current_parent_node, buff_idx, NULL, NULL, buffer);
    }
  }
  /*******************END BROADCASTM***************/

}

void run_SINGLE_tree_tests(thread_data_t *td, uint8_t **dst_arr, uint8_t **src_arr,  int root_thread, int in_flags, myxml_node_t *root_xml_node) {
  int s, c, i, f;
  myxml_node_t *addr_mode_node, *current_parent_node = root_xml_node, *temp_node= NULL;
  int *src, *dst;
  int num_tree_classes;
  int num_fanouts; 
  int best_tree; 
  int best_fanout;
  gasnett_tick_t begin,end;
  gasnett_tick_t best_time;
  char buffer[50];
  
  int flags = in_flags | GASNET_COLL_SRC_IN_SEGMENT|GASNET_COLL_DST_IN_SEGMENT;
  
  if(in_flags & GASNET_COLL_SINGLE) { /*whether or not each node presents one address that is valid for all nodes*/
    addr_mode_node = myxml_createNode(root_xml_node, (char *)"address_mode", (char*) "val", (char*) "single", NULL);
    src = (int*) src_arr[0];
    dst = (int*) dst_arr[0];
  } else { /*each node only gives address that are only valid on the local node*/
    addr_mode_node = myxml_createNode(root_xml_node, (char*) "address_mode", (char*) "val", (char*) "local", NULL);
    src = (int*) td->mysrc;
    dst= (int*) td->mydest;
  }
  
  
  
  /*************** BROADCAST *****************/
  current_parent_node = myxml_createNode(addr_mode_node, (char*) "collective", (char *) "val", (char*) "broadcast", NULL);
  
  num_tree_classes = gasnet_coll_get_num_tree_classes(GASNET_TEAM_ALL, GASNET_COLL_BROADCAST_OP);
  temp_node = current_parent_node;
  
  for(s=1; s<=max_data_size; s*=2) {
    uint32_t best_alg;
    uint32_t num_params;
    uint32_t *param_list;
    MSG0("starting test: %s %d bytes", fill_flag_str(flags, buffer), (int)sizeof(int)*s);
    
    current_parent_node = myxml_createNodeInt(temp_node, (char*) "size", (char *) "start", s, NULL);
    myxml_addAttributeInt(current_parent_node, (char*) "end", (s == max_data_size ? 1<<31 : (s*2)-1));
    
    /*run the gasnet tuner and report back the results!*/
    ganset_coll_tune_generic_op(GASNET_TEAM_ALL, GASNET_COLL_BROADCAST_OP, (uint8_t**) &dst, (uint8_t**) &src, root_thread, flags, sizeof(int)*s,
                                NULL, NULL, &best_alg, &num_params, &param_list);
    
    sprintf(buffer, "%d", best_alg);
    myxml_createNode(current_parent_node, (char*) "Best_Alg", NULL, NULL, buffer);
    sprintf(buffer, "%d", num_params);
    myxml_createNode(current_parent_node, (char*) "Num_Params", NULL, NULL, buffer);
    for(c=0; c<num_params; c++) {
      char buff_idx[20];
      sprintf(buff_idx, "param_%d", c);
      sprintf(buffer, "%d", param_list[c]);
      myxml_createNode(current_parent_node, buff_idx, NULL, NULL, buffer);
    }
  }
  /*******************END BROADCAST***************/
  

}



void *thread_main(void *arg) {
  int flag_iter;
  thread_data_t *td = (thread_data_t*) arg;
  myxml_node_t *tuning_root, *temp, *temp2;
  char buffer[100];
  int skip_msg_printed = 0;
#if GASNET_PAR
  int i;
  gasnet_image_t *imagearray = test_malloc(nodes * sizeof(gasnet_image_t));
  for (i=0; i<nodes; ++i) { imagearray[i] = threads_per_node; }
  gasnet_coll_init(imagearray, td->mythread, NULL, 0, 0);
  test_free(imagearray);
#else
  gasnet_coll_init(NULL, td->mythread, NULL, 0, 0);
#endif


  COLL_BARRIER();

  tuning_root = myxml_createNode(NULL, (char*) "machine", (char*)"CONFIG", (char*) GASNET_CONFIG_STRING, NULL);
  
  temp = myxml_createNodeInt(tuning_root, (char*)"threads_per_node", (char*)"val", threads_per_node, NULL);
  

  for(flag_iter=0; flag_iter<9; flag_iter++) {
    int flags;
    myxml_node_t *sync_node, *test_root;
    char buffer[8];
    COLL_BARRIER();
    


    
    switch(flag_iter) {
    case 0: flags = GASNET_COLL_IN_NOSYNC  | GASNET_COLL_OUT_NOSYNC; break;
    case 1: flags = GASNET_COLL_IN_NOSYNC  | GASNET_COLL_OUT_MYSYNC; break;
    case 2: flags = GASNET_COLL_IN_NOSYNC  | GASNET_COLL_OUT_ALLSYNC; break;
    case 3: flags = GASNET_COLL_IN_MYSYNC  | GASNET_COLL_OUT_NOSYNC; break;
    case 4: flags = GASNET_COLL_IN_MYSYNC  | GASNET_COLL_OUT_MYSYNC; break;
    case 5: flags = GASNET_COLL_IN_MYSYNC  | GASNET_COLL_OUT_ALLSYNC; break;
    case 6: flags = GASNET_COLL_IN_ALLSYNC | GASNET_COLL_OUT_NOSYNC; break;
    case 7: flags = GASNET_COLL_IN_ALLSYNC | GASNET_COLL_OUT_MYSYNC; break;
    case 8: flags = GASNET_COLL_IN_ALLSYNC | GASNET_COLL_OUT_ALLSYNC; break;
    default: continue;
    }
    

    sync_node = myxml_createNode(temp, (char*)"sync_mode", (char*)"val", fill_flag_str(flags, buffer), NULL);
    /*do single addr tests*/
 
    
#if GASNET_ALIGNED_SEGMENTS
    if(threads_per_node == 1) {
      test_root = myxml_createNode(sync_node, (char*)"num_addrs", (char*) "val", (char*)"single", NULL);
      /*call the single address (coll single) test routines with testroot*/
      run_SINGLE_tree_tests(td, all_dsts, all_srcs, 0, flags | GASNET_COLL_SINGLE, test_root);
    } else {
      if(td->mythread == 0 && !skip_msg_printed) MSG0("skipping SINGLE/SINGLE (multiple threads per node)");
    }
#else
    if(td->mythread == 0 && !skip_msg_printed) MSG0("skipping SINGLE/SINGLE (unaligned segments)");
#endif

    if(threads_per_node == 1) {
      test_root = myxml_createNode(sync_node,(char*)"num_addrs", (char*)"val", (char*)"single", NULL);
      /*call the single address (coll local) test routines with testroot*/
      run_SINGLE_tree_tests(td, all_dsts, all_srcs, 0, flags | GASNET_COLL_LOCAL, test_root);
    } else {
      if(td->mythread == 0 && !skip_msg_printed) MSG0("skipping SINGLE/LOCAL (multiple threads per node) (test unimplemetned for now)");
    }

    skip_msg_printed = 1;
    /*do multi addr tests*/
    if(threads_per_node > 1) {
      /*call the multi address test (coll single) routines with testroot*/
      test_root = myxml_createNode(sync_node,(char*)"num_addrs", (char*)"val", (char*)"multi", NULL);
      run_MULTI_tree_tests(td, all_dsts, all_srcs, 0, flags | GASNET_COLL_SINGLE, test_root);
      
      /*call the multi address test (coll local) routines with testroot*/
      test_root = myxml_createNode(sync_node,(char*)"num_addrs", (char*)"val", (char*)"multi", NULL);
      run_MULTI_tree_tests(td, my_dsts, my_srcs, 0, flags | GASNET_COLL_LOCAL, test_root);
    }
  }


  

  MSG0("starting dump of tuning data");
  if(td->mythread == 0){ 
    FILE *outstream=fopen(outputfile, "w");
    myxml_printTreeBIN(outstream, tuning_root);
    fclose(outstream);
 //   myxml_printTreeXML(stdout, tuning_root, " ");
    fflush(stdout);
    fflush(stdout);
  }
  MSG0("tunign data dumped");
      

  COLL_BARRIER();
  return 0;
}

int main(int argc, char **argv) {
  int i,j;
  static uint8_t *A, *B;
  thread_data_t *td_arr;
  GASNET_Safe(gasnet_init(&argc, &argv));
  
  max_data_size = DEFAULT_MAX_DATA_SIZE/sizeof(int);
  performance_iters = DEFAULT_PERFORMANCE_ITERS;


#if GASNET_PAR
  threads_per_node = gasnett_cpu_count();
#else
  threads_per_node = 1;
#endif
  
  for(i=1; i<argc; i++) {
    if(strcmp("-i", argv[i])==0 || strcmp("-iters", argv[i])==0) {
      performance_iters = atoi(argv[i+1]);
      i++;
    } 
#if GASNET_PAR
    else if(strcmp("-t", argv[i])==0 || strcmp("-threads", argv[i])==0) {
      threads_per_node = atoi(argv[i+1]);
      i++;
    } 
#endif
    else if(strcmp("-sz", argv[i])==0 || strcmp("-max-data-size", argv[i])==0) {
      max_data_size = atoi(argv[i+1])/sizeof(int);
      i++;
    } else if(strcmp("-f", argv[i])==0 || strcmp("-tune-file", argv[i])==0) {
      outputfile = test_malloc(strlen(argv[i+1])+1);
      strcpy(outputfile, argv[i+1]);
      i++;
    } else if(strcmp("-h", argv[i])==0 || strcmp("-help", argv[i])==0) {
#if GASNET_PAR
      if(gasneti_mynode == 0) printf("usage: %s (-i iters) (-t num threads) (-sz max size) (-f output file)\n", argv[0]);
#else
      if(gasneti_mynode == 0) printf("usage: %s (-i iters) (-sz max size) (-f output file)\n", argv[0]);
#endif
      gasnet_exit(0);
    }
    
  }                      
  
  if(performance_iters <=0) {
    gasnet_exit(0);
  }
#if 1

  
  mynode = gasnet_mynode();
  nodes = gasnet_nodes();
  THREADS = nodes * threads_per_node;

  if (threads_per_node > gasnett_cpu_count()) {
    MSG0("WARNING: thread count (%i) exceeds physical cpu count (%i) - enabling  \"polite\", low-performance synchronization algorithms",
         (int) threads_per_node, gasnett_cpu_count());
    gasnet_set_waitmode(GASNET_WAIT_BLOCK);
  }
  
  GASNET_Safe(gasnet_attach(NULL, 0, TEST_SEGSZ_REQUEST, TEST_MINHEAPOFFSET));
  A = TEST_MYSEG();
  B = A+(SEG_PER_THREAD*threads_per_node);
  my_srcs =  (uint8_t**) test_malloc(sizeof(uint8_t*)*threads_per_node);
  my_dsts =  (uint8_t**) test_malloc(sizeof(uint8_t*)*threads_per_node);
  all_srcs = (uint8_t**) test_malloc(sizeof(uint8_t*)*THREADS);
  all_dsts = (uint8_t**) test_malloc(sizeof(uint8_t*)*THREADS);
  td_arr = (thread_data_t*) test_malloc(sizeof(thread_data_t)*threads_per_node);
  
  for(i=0; i<threads_per_node; i++) {
    my_srcs[i] = A + i*SEG_PER_THREAD;
    my_dsts[i] = B + i*SEG_PER_THREAD;
    td_arr[i].my_local_thread = i;
    td_arr[i].mythread = mynode*threads_per_node+i;
    td_arr[i].mysrc = my_srcs[i];
    td_arr[i].mydest = my_dsts[i];
  }
  
  for(i=0; i<nodes; i++) {
    /*    assert_always(TEST_SEG(i).size >= SEG_PER_THREAD*threads_per_node); */
    for(j=0; j<threads_per_node; j++) {
      all_srcs[i*threads_per_node+j] = (uint8_t*) TEST_SEG(i) + j*SEG_PER_THREAD;
      all_dsts[i*threads_per_node+j] = (uint8_t*) TEST_SEG(i) + SEG_PER_THREAD*threads_per_node + j*SEG_PER_THREAD;
    }
  }
#if GASNET_PAR
  test_createandjoin_pthreads(threads_per_node, &thread_main, td_arr, sizeof(thread_data_t));
#else
  thread_main(&td_arr[0]);
#endif
  
  test_free(td_arr);
  BARRIER();

#endif
  gasnet_exit(0);
  return 0;
}

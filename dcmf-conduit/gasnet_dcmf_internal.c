#include <gasnet_core_internal.h>

#define PRINT_NODE_INFO 1

gasnetc_dcmf_req_t *gasnetc_dcmf_req_free_list;

void gasnetc_recv_done_cb(void *arg) {
    gasnetc_recv_done_cb_args_t *in = (gasnetc_recv_done_cb_args_t*) arg;
    if(in->counter) (*(in->counter))++;
    gasnetc_free_dcmf_req(in->req);
    gasneti_free(in);
}


gasnetc_dcmf_req_t * gasnetc_get_dcmf_req() {
    gasnetc_dcmf_req_t *req;
    if(gasnetc_dcmf_req_free_list) {
	req = gasnetc_dcmf_req_free_list;
	gasnetc_dcmf_req_free_list = req->next;
    } else {
	req = (gasnetc_dcmf_req_t*) gasneti_malloc(sizeof(gasnetc_dcmf_req_t));
    }
    return req;
}

void gasnetc_free_dcmf_req(gasnetc_dcmf_req_t *req){
    req->next = gasnetc_dcmf_req_free_list;
    gasnetc_dcmf_req_free_list = req;
}

void gasnetc_free_dcmf_req_cb(void *arg){
    gasnetc_free_dcmf_req((gasnetc_dcmf_req_t*) arg);
}

void gasnetc_inc_unit64_arg_cb(void* arg) {
    uint64_t *in = (uint64_t*) arg;
    (*in)++;
}

void gasnetc_inc_unit32_arg_cb(void* arg) {
    uint32_t *in = (uint32_t*) arg;
    (*in)++;
}

void gasnetc_inc_unit8_arg_cb(void* arg) {
    uint8_t *in = (uint8_t*) arg;
    (*in)++;
}

void gasnetc_dcmf_init(gasnet_node_t* mynode, gasnet_node_t *nodes) {
    
    DCMF_CriticalSection_enter(0);
    if(DCMF_Messager_initialize()!=1) {
	fprintf(stderr, "MESSAGER INITIALIZATION ERROR\n");
	exit(1);
    }
    
    gasnetc_dcmf_req_free_list = NULL;

    *mynode = DCMF_Messager_rank();
    *nodes = DCMF_Messager_size();
    DCMF_CriticalSection_exit(0);
    
    gasnetc_dcmf_bootstrap_coll_init();
    
    
    
#if PRINT_NODE_INFO
    do {
	DCMF_Hardware_t hw;
	DCMF_SAFE(DCMF_Hardware(&hw));
	fprintf(stdout, "%d> Sizes x: %d y: %d z: %d t: %d\n", gasneti_mynode,
		hw.xSize, hw.ySize, hw.zSize, hw.tSize);
	fprintf(stdout, "%d> Coords x: %d y: %d z: %d t: %d\n", gasneti_mynode,
		hw.xCoord, hw.yCoord, hw.zCoord, hw.tCoord);
	fprintf(stdout, "%d> isTrous x: %d y: %d z: %d t: %d\n", gasneti_mynode,
		hw.xTorus, hw.yTorus, hw.zTorus, hw.tTorus);
    } while(0);
#endif
}

void gasnetc_dcmf_finalize() {
    DCMF_Messager_finalize();
}

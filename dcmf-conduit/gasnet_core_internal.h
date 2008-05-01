/*   $Source: /Users/kamil/work/gasnet-cvs2/gasnet/dcmf-conduit/gasnet_core_internal.h,v $
 *     $Date: 2008/05/01 21:14:51 $
 * $Revision: 1.1.2.2 $
 * Description: GASNet dcmf conduit header for internal definitions in Core API
 * Copyright 2002, Dan Bonachea <bonachea@cs.berkeley.edu>
 * Terms of use are as specified in license.txt
 */

#ifndef _GASNET_CORE_INTERNAL_H
#define _GASNET_CORE_INTERNAL_H

#include <gasnet_internal.h>
#include <dcmf.h>
#include <dcmf_collectives.h>

#define Z fprintf(stderr,"%d> %s(%d)\n", gasneti_mynode, __FILE__,__LINE__)
//#define Z do{} while(0)
#define DCMF_SAFE(FUNCALL) if(FUNCALL!=DCMF_SUCCESS) fprintf(stderr, "error on line %d\n", __LINE__)

typedef struct gasnetc_dcmf_req_t_{
    DCMF_Request_t req;
    struct gasnetc_dcmf_req_t_ *next;
} gasnetc_dcmf_req_t __attribute__((__aligned__(1024)));

typedef struct gasnetc_recv_done_cb_args_{
    gasnetc_dcmf_req_t *req;
    uint64_t *counter;
} gasnetc_recv_done_cb_args_t __attribute__((__aligned__(8)));


void gasnetc_dcmf_init(gasnet_node_t *mynode, gasnet_node_t *nodes);
void gasnetc_dcmf_finalize();

void gasnetc_dcmf_bootstrap_coll_init();
void gasnetc_dcmf_p2p_init();

void gasnetc_recv_done_cb(void *arg);
gasnetc_dcmf_req_t *gasnetc_get_dcmf_req();
void gasnetc_free_dcmf_req(gasnetc_dcmf_req_t *req);
void gasnetc_free_dcmf_req_cb(void *req);

void gasnetc_inc_unit64_arg_cb(void* arg);
void gasnetc_inc_unit32_arg_cb(void* arg);
void gasnetc_inc_unit8_arg_cb(void* arg);


void gasnetc_dcmf_bootstrapBarrier();
void gasnetc_dcmf_bootstrapBroadcast(void *src, size_t len, void *dest, int rootnode);
void gasnetc_dcmf_bootstrapExchange(void *src, size_t nbytes, void *dst);

/*  whether or not to use spin-locking for HSL's */
#define GASNETC_HSL_SPINLOCK 1

/* ------------------------------------------------------------------------------------ */
#define GASNETC_HANDLER_BASE  1 /* reserve 1-63 for the core API */
#define _hidx_gasnetc_auxseg_reqh             (GASNETC_HANDLER_BASE+0)
/* add new core API handlers here and to the bottom of gasnet_core.c */


#endif

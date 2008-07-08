/*   $Source: /Users/kamil/work/gasnet-cvs2/gasnet/dcmf-conduit/gasnet_core_internal.h,v $
 *     $Date: 2008/07/08 19:52:52 $
 * $Revision: 1.1.2.8 $
 * Description: GASNet dcmf conduit header for internal definitions in Core API
 * Copyright 2002, Dan Bonachea <bonachea@cs.berkeley.edu>
 * Terms of use are as specified in license.txt
 */

#ifndef _GASNET_CORE_INTERNAL_H
#define _GASNET_CORE_INTERNAL_H

#include <gasnet_internal.h>
#include <dcmf.h>
#include <dcmf_collectives.h>
#include <dcmf_globalcollectives.h>

#define GASNETC_MAXQUADS_PER_AM 7
#define GASNETC_MAX_AM_ARGS 24

#define Z fprintf(stderr,"%d> %s(%d)\n", gasneti_mynode, __FILE__,__LINE__)
//#define Z do{} while(0
#define DCMF_SAFE(FUNCALL) if(FUNCALL!=DCMF_SUCCESS) gasneti_fatalerror("DCMF error on line (%s)%d\n", __FILE__, __LINE__)

#define ALIGN_STRUCT(BYTES) __attribute__((__aligned__(BYTES)))
typedef struct gasnetc_dcmf_req_t_{
	DCMF_Request_t req;
	struct gasnetc_dcmf_req_t_ *next;
} gasnetc_dcmf_req_t ALIGN_STRUCT(1024);



typedef enum{
	GASNETC_AMREQ=0, 
	GASNETC_AMREP,
	GASNETC_NUM_AMTYPES
} gasnetc_dcmf_amtype_t;


typedef enum{
	GASNETC_AMSHORT=0, 
	GASNETC_AMMED,
	GASNETC_AMLONG,
	GASNETC_AMLONGASYNC,
	GASNETC_NUM_AMCATS
} gasnetc_dcmf_amcategory_t;


typedef enum{
	GASNETC_DCMF_SEND_DEFAULT=0,
	GASNETC_DCMF_SEND_EAGER, 
	GASNETC_DCMF_SEND_RVOUS,
	GASNETC_DCMF_NUM_SENDCATS
} gasnetc_dcmf_send_category_t;


typedef struct gasnetc_ambuf_t_ {
	struct gasnetc_ambuf_t_ *next;
	uint32_t _pad; /*4 byte padding to ensure that the buffer lies on an 8 byte boundry*/
	uint8_t data[GASNETC_AMMAXMED];
	
} gasnetc_ambuf_t;

typedef struct gasnetc_token_t_ {
	struct gasnetc_token_t_ *next;
	gasnet_node_t srcnode;
	uint8_t sent_reply;
	gasnetc_dcmf_amtype_t amtype;
	gasnetc_dcmf_amcategory_t amcat;
	gasnetc_dcmf_req_t *dcmf_req;
} gasnetc_token_t ALIGN_STRUCT(32);

typedef struct gasnetc_amhandler_t_{ 
	struct gasnetc_amhandler_t_ *next;
	gasnetc_token_t *token;
	gasnet_handler_t handleridx;
	/*when the buffer is known this is  a pointer to data
		otherwise its just a pointer to a ambuf_t*/
	void *buffer;
	uint8_t buffer_needs_free;
	size_t nbytes;
	int numargs;
	unsigned seq_number;
	gasnet_handlerarg_t amargs[GASNETC_MAX_AM_ARGS];
} gasnetc_amhandler_t  ALIGN_STRUCT(1024);


typedef void (*GASNETC_DCMF_RECV_SHORT_CB)(void *client_data, const DCQuad *msginfo, unsigned numquads,
					   unsigned peer, const char *src, unsigned nbytes);

typedef void (*GASNETC_DCMF_RECV_HEADER_CB)(void *client_data, const DCQuad *msginfo, unsigned numquads,
					    unsigned peer, unsigned sendlen, unsigned *rcvlen, char **rcvbuff, DCMF_Callback_t *cb_done);


typedef struct gasnetc_dcmf_amregistration_t_ {
	DCMF_Protocol_t registration;
	DCMF_Send_Protocol send_category;
} gasnetc_dcmf_amregistration_t  ALIGN_STRUCT(512);

extern gasnetc_dcmf_amregistration_t *gasnetc_dcmf_amregistration[GASNETC_NUM_AMTYPES][GASNETC_NUM_AMCATS][GASNETC_DCMF_NUM_SENDCATS];

#define GASNETC_DCMF_AM_REGISTARTION(AMTYPE, AMCATEGORY, AMSENDCAT) \
	gasnetc_dcmf_amregistration[AMTYPE][AMCATEGORY][AMSENDCAT]->registration

#define GASNETC_DCMF_AM_SEND_CAT(AMTYPE, AMCATECORY) \
	gasnetc_dcmf_amregistration[AMTYPE][AMCATEGORY][AMSENDCAT]->send_category



void gasnetc_dcmf_init(gasnet_node_t *mynode, gasnet_node_t *nodes);
void gasnetc_dcmf_finalize();

void gasnetc_dcmf_bootstrap_coll_init();
void gasnetc_dcmf_p2p_init();


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

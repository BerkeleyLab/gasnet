/*  $Archive:: /Ti/GASNet/sci-conduit/gasnet_core_internal.h         $
 *     $Date: 2004/03/29 17:46:38 $
 * $Revision: 1.1.2.4 $
 * Description: GASNet sci conduit header for internal definitions in Core API
 * Copyright 2002, Dan Bonachea <bonachea@cs.berkeley.edu>
 *				   Hung-Hsun Su <su@hcs.ufl.edu>
 *				   Burt Gordon <gordon@hcs.ufl.edu>
 * Terms of use are as specified in license.txt
 */

#ifndef _GASNET_CORE_INTERNAL_H
#define _GASNET_CORE_INTERNAL_H

#include <gasnet.h>
#include <gasnet_internal.h>

/* SCI conduit specific headers*/
#ifndef _SISCI_HEADERS
#define _SISCI_HEADERS
#include "sisci_types.h"
#include "sisci_api.h"
#include "sisci_error.h"
#endif

/*Necessary use of some C definitions*/
#include <sys/types.h>
#include <sys/time.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>

/*******************************************************
		Big Phys Area Patch installed?
*******************************************************/
#define GASNETC_BIGPHY_ENABLE 0 /*set to 0 if not installed*/

/********************************************************
					Data Structure
********************************************************/

/* SCI conduit specific data structures*/
typedef struct
{
	gasnet_node_t source_id;
	uint8_t msg_number;
} gasnetc_sci_token_t;

typedef struct
{
	uint16_t header;	/* handler (8 bits) + msg type (1 bit, request/reply) + AM type (2 bits) + num_arg (4 bits)
						/* msg type: 0 = request; 1 = reply;
						/* AM type: 0 = short; 1 = medium; 2 = long; 3 = control (basic return msg to free mls);*/
} gasnetc_command_t;

typedef struct
{
	uint16_t header;	/* handler (8 bits) + msg type (1 bit, request/reply) + AM type (2 bits) + num_arg (4 bits)
						/* msg type: 0 = request; 1 = reply;
						/* AM type: 0 = short; 1 = medium; 2 = long; 3 = control (basic return msg to free mls);*/
	gasnetc_sci_token_t token;
	void * next;
} gasnetc_command_receiver_t;

typedef struct
{
	gasnetc_command_t header;
	uint16_t payload_size;
	gasnet_handlerarg_t args[16];
} gasnetc_ShortMedium_command_t;

typedef struct
{
	gasnetc_command_t header;
	uint16_t payload_size;
	gasnet_handlerarg_t args[16];
	gasnetc_sci_token_t token;	
	void * next;
} gasnetc_ShortMedium_command_receiver_t;

typedef struct
{
	gasnetc_command_t header;
	uint16_t payload_size;
	void * payload;
	gasnet_handlerarg_t args[16];	
} gasnetc_Long_command_t;

typedef struct
{
	gasnetc_command_t header;
	uint16_t payload_size;
	void * payload;
	gasnet_handlerarg_t args[16];
	gasnetc_sci_token_t token;
	void * next;
} gasnetc_Long_command_receiver_t;

typedef uint8_t bool;

typedef void (*gasnetc_handler_short) (gasnet_token_t token, ...);
typedef void (*gasnetc_handler_mediumlong)(gasnet_token_t token, void *buf, size_t nbytes, ...);

/********************************************************
					Constants
********************************************************/

#define gasnetc_boundscheck(node,ptr,nbytes) gasneti_boundscheck(node,ptr,nbytes,c)
#define GASNETC_HSL_SPINLOCK 1					/*  whether or not to use spin-locking for HSL's */
/* ------------------------------------------------------------------------------------ */
/* make a GASNet call - if it fails, print error message and return */
#define GASNETC_SAFE(fncall) do {                            \
   int retcode = (fncall);                                   \
   if_pf (gasneti_VerboseErrors && retcode != GASNET_OK) {   \
     char msg[1024];                                         \
     sprintf(msg, "\nGASNet encountered an error: %s(%i)\n", \
        gasnet_ErrorName(retcode), retcode);                 \
     GASNETI_RETURN_ERRFR(RESOURCE, fncall, msg);            \
   }                                                         \
 } while (0)

/* ------------------------------------------------------------------------------------ */
#define GASNETC_HANDLER_BASE  1 /* reserve 1-63 for the core API */
#define _hidx_                              (GASNETC_HANDLER_BASE+)
/* add new core API handlers here and to the bottom of gasnet_core.c */

/* SCI conduit specific constants*/
#define GASNETC_SCI_NO_CALLBACK         		NULL
#define GASNETC_SCI_NO_FLAGS            		0
#define GASNETC_SCI_DATA_TRANSFER_READY 		8
#define GASNETC_SCI_FILE 						"gasnet_nodes.sci"
#define	GASNETC_SCI_FAST_SEG					1048576	/*1MB seg size for fast implementation*/
#define	GASNETC_SCI_PERCENT						80		/*use 80% of available blocks (leave some for command regions)*/
#define GASNETC_SCI_TRUE						1
#define GASNETC_SCI_FALSE						0
#define GASNETC_SCI_MAX_REQUEST_MSG				2
#define GASNETC_SCI_MAX_HANDLER_NUMBER			256
#define GASNETC_SCI_COMMAND_MESSAGE_SIZE		1104 /*1024 +84 */
#define GASNETC_SCI_NUM_DMA_QUEUE				1
#define GASNETC_SCI_MAX_LONG_PAYLOAD_SIZE		(512*1024)
#define GASNETC_SCI_MAX_DMA_QUEUE_USAGE			1
#define GASNETC_SCI_MAX_BARRIER					10
#define GASNETC_SCI_REQUEST						0
#define GASNETC_SCI_REPLY						1
#define GASNETC_SCI_SHORT						0
#define GASNETC_SCI_MEDIUM						1
#define GASNETC_SCI_LONG						2
#define GASNETC_SCI_CONTROL						3
#define GASNETC_ONE_MB							1048576 /*define 1 MB*/

/********************************************************
					Global Variables
********************************************************/

/* SCI conduit specific Global Variables*/
extern gasnet_seginfo_t		*gasnetc_seginfo;
extern sci_desc_t			*gasnetc_sci_sd, *gasnetc_sci_gb_sd;				/*SCI virtual device descriptors*/
extern sci_desc_t			*gasnetc_sci_sd_remote, *gasnetc_sci_sd_long;	
extern sci_desc_t			gasnetc_sci_gas_seg;
extern sci_local_segment_t	*gasnetc_sci_localSegment;							/*Handlers to local memory segment*/
extern sci_remote_segment_t	*gasnetc_sci_remoteSegment;							/*Handlers to remote memory segment*/
extern sci_remote_segment_t	*gasnetc_sci_remoteSegment_gb;
extern sci_remote_segment_t	*gasnetc_sci_remoteSegment_long;
extern sci_map_t			*gasnetc_sci_localMap;								/*Handlers to locally mapped segment*/
extern sci_map_t			*gasnetc_sci_remoteMap;								/*Handlers to remotely mapped segment*/
extern sci_map_t			*gasnetc_sci_remoteMap_gb;							/*Handlers to remotely mapped segment for global bytes*/
extern unsigned int			gasnetc_sci_localAdapterNo;
extern unsigned int			gasnetc_sci_localSCIId;
extern unsigned int			*gasnetc_sci_remoteNodeId;
extern unsigned int			*gasnetc_sci_localSegmentId;						/*host-wide unique segment identifier*/
extern unsigned int			*gasnetc_sci_remoteSegmentId;
extern unsigned int			*gasnetc_sci_remoteSegmentId_gb;
extern unsigned int			gasnetc_sci_offset;									/*default offset within the segment*/
extern unsigned int			gasnetc_sci_max_local_seg;							/*the values of the maximum segment sizes in system*/
extern unsigned int			gasnetc_sci_max_global_seg;							/*across all systems*/
extern unsigned int			*gasnetc_sci_SCI_Ids;
extern void 				**gasnetc_sci_local_mem;							/*an array of void pointers to each local segment*/
extern void 				**gasnetc_sci_remote_mem;							/*an array of void pointers to each remote segment*/
extern void 				**gasnetc_sci_global_ready;
extern bool 				*gasnetc_sci_msg_flag;
extern sci_map_t			*gasnetc_sci_local_dma_map;
extern sci_local_segment_t	*gasnetc_sci_local_dma_segment;
extern sci_dma_queue_t		*gasnetc_sci_local_dma_queue;
extern sci_desc_t			*gasnetc_sci_local_dma_sd;
extern void					**gasnetc_sci_local_dma_addr;

extern void					*gasnetc_sci_handler_table[256];
extern uint8_t				*gasnetc_sci_msg_loc_status;
pthread_mutex_t				gasnetc_sci_poll_lock;
pthread_mutex_t				gasnetc_sci_wq_lock;
pthread_mutex_t				gasnetc_sci_msg_loc_mutex;

/*pthread mutex variables*/
extern pthread_mutex_t      gasnetc_mutex_sci_gmrf;
extern pthread_mutex_t      gasnetc_mutex_sci_memcpy ;
extern pthread_mutex_t      gasnetc_mutex_sci_transfer ;
extern pthread_mutex_t      gasnetc_mutex_sci_create_sequence ;
extern pthread_mutex_t      gasnetc_mutex_sci_start_sequence  ;
extern pthread_mutex_t      gasnetc_mutex_sci_check_sequence  ;
extern pthread_mutex_t      gasnetc_mutex_sci_remove_sequence  ;
extern pthread_mutex_t      gasnetc_mutex_sci_open  ;
extern pthread_mutex_t      gasnetc_mutex_sci_create_segment  ;
extern pthread_mutex_t      gasnetc_mutex_sci_prepare_segment  ;
extern pthread_mutex_t      gasnetc_mutex_sci_map_segment  ;
extern pthread_mutex_t      gasnetc_mutex_sci_unmap_segment  ;
extern pthread_mutex_t      gasnetc_mutex_sci_remove_segment  ;
extern pthread_mutex_t      gasnetc_mutex_sci_close  ;
extern pthread_mutex_t      gasnetc_mutex_sci_create_dma  ;
extern pthread_mutex_t      gasnetc_mutex_sci_remove_dma  ;
extern pthread_mutex_t      gasnetc_mutex_sci_enqueue_dma  ;
extern pthread_mutex_t      gasnetc_mutex_sci_lock1  ;
extern pthread_mutex_t      gasnetc_mutex_sci_lock2  ;
extern pthread_mutex_t      gasnetc_mutex_sci_lock3  ;
extern pthread_mutex_t		gasnetc_mutex_sci_msgloc;
extern pthread_mutex_t		gasnetc_mutex_sci_zero;
extern pthread_mutex_t		gasnetc_sci_exit_lock;
extern pthread_mutex_t		gasnetc_sci_cb_exit;

/********************************************************
					SCI SETUP FUNCTIONS					
********************************************************/

/*the first step to exiting*/
void gasnetc_sci_call_exit(unsigned int sig);

/* BARRIER FUNCTION
// Creates a temporary segment and connects to all other nodes' 
// temp segment, writes a 1 in their segment at index:gasnetc_mynode,
// then waits until everybody has written a 1 into all of our index spots
// destroys the segment and continues.*/
void gasnetc_sci_internal_Barrier();

/* Parses all the SCI Ids and places them in gasnetc_sci_SCI_Ids.
// The corresponding GASNet node ID is the index of the location of the SCI id in the
// array.*/
int gasnetc_parseSCIIds(FILE *node_info, const int number);

/* Returns the Max_local_Segment size in bytes based on available 
// free mem on the system and writes the size to a global file*/
int gasnetc_get_free_mem();

/* Returns the maximum global segment size. This is the minimum of the segment
// sizes available over the whole cluster. Return -1 if not all node information
// is in the file, throws an error otherwise.*/
int gasnetc_getSCIglobal_seg(int number);

/* This uses the number given to it to create the segments needed by the command
// region. It will leave an open spot for the gasnet segment space to be
// the next to last segment. It will be created in GASNet Attach.*/
gasnet_node_t gasnetc_SCI_Create_Connections(int number);

/* Connects all the command regions and then all the global ready bytes
// across all nodes.*/
int gasnetc_SCI_connect_cmd(int number);

/* This function finds the total number of nodes that will run GASNet as well as 
// assign GASNet Node IDS to each SCI ID. Additionally, the total amount of free
// memory on the system is determined and a percentage is used as the MAX_local_Seg
// size.*/ 
unsigned int gasnetc_SCIInit(gasnet_node_t *my_node);

/* Create the payload (GASNET segment) segment and set it available.*/
void* gasnetc_create_gasnetc_sci_seg(uintptr_t *segsize, int index);

/* This waits for all nodes to write their segment sizes and addresses to the
// mailboxes. Then gets all the segment info and places int the segment array
// info.*/
void gasnetc_get_SegInfo(gasnet_seginfo_t* gasnetc_sci_seginfo, uintptr_t segsize,  void * segbase);

/********************************************************
				   Segment Info Table
********************************************************/

/* Return the starting address of a segment with a given Input node ID*/
extern void * gasnetc_sit_get_base_addr (gasnet_node_t InputID);

/* Return the size of a segment with a given Input node ID*/
extern size_t gasnetc_sit_get_size (gasnet_node_t InputID);

/********************************************************
				Local/Remote segment Info
********************************************************/

/* Return the local memory address dedicated for the remote node*/
extern void * gasnetc_ls_get_addr (gasnet_node_t RemoteID);

/* Return the memory address (ptr, virtual) to the dedicated segment on the remote node*/
extern void * gasnetc_rs_get_addr (gasnet_node_t RemoteID);

/* Return the remote map handler (SCI) created for the dedicated segment on the remote node*/
extern sci_map_t gasnetc_rs_get_rmap (gasnet_node_t RemoteID);

/* Return the memory address (ptr, virtual) to the control segment on the remote node*/
extern void * gasnetc_gr_get_addr (gasnet_node_t RemoteID);

/* Return the remote segment handler (SCI) created for the dedicated segment on the remote node*/
extern sci_remote_segment_t gasnetc_rs_get_seg (gasnet_node_t RemoteID);

/* Calculates the appropriate offset required based on the input address and the starting address of a remote payload segment*/
extern int gasnetc_rs_get_offset (gasnet_node_t RemoteID, void * dest_addr);

/********************************************************
				Message Location Status
		-- Use by Local node to keep track of --
		--	command msgs to all other nodes   --
********************************************************/

/* Allocate space and initialize the MLS*/
extern void gasnetc_mls_init ();

/* Check if the given location is free or not*/
extern int gasnetc_mls_chk_free (gasnet_node_t RemoteID, uint8_t msg_number);

/* Set a given message space to un-occupied (false)*/
extern void gasnetc_mls_release (gasnet_node_t RemoteID, uint8_t msg_num);

/* Obtain a free request message location*/
extern int gasnetc_mls_get_loc (gasnet_node_t RemoteID);

/********************************************************
					  Handler Table
********************************************************/

/* Allocate and initialize the handler table*/
extern void gasnetc_ht_init();

/* Add a new handler to the table using a given index*/
extern void gasnetc_ht_add_handler (void * func_ptr, int index);

/* Return the function pointer base of a given handler*/
extern void * gasnetc_ht_get_handler (gasnet_handler_t input);

/********************************************************
					 Handler Running
********************************************************/

/* Runs short message handler*/
extern void gasnetc_run_handler_short (gasnet_token_t token, void* func_ptr, int numargs, gasnet_handlerarg_t *args);

/* Runs medium/long message handler*/
extern void gasnetc_run_handler_mediumlong (gasnet_token_t token, void* func_ptr, int numargs, gasnet_handlerarg_t *args, 
											void *payload, size_t payload_size);

/********************************************************
					  Work Queue
********************************************************/

/* Adds the given message to the work queue*/
extern int gasnetc_enqueue_msg (gasnet_node_t node_id, uint8_t msg_number);

/* Removes the first element of the work queue*/
extern void * gasnetc_dequeue_msg (gasnet_node_t *sender_id, uint8_t *msg_number);

/* Scans the MRFs to enqueue new messages*/
extern void * gasnetc_MRF_scan (gasnet_node_t *sender_id, uint8_t *msg_number);

/********************************************************
			Command Segment Related Functions
********************************************************/

/* Return the Handler # for the given message*/
extern gasnet_handler_t gasnetc_get_msg_handler (uint16_t header);

/* Return the type (Request/Reply) for the given message*/
extern uint8_t gasnetc_get_msg_type (uint16_t header);

/* Return the AM type (Short/Medium/Long) for the given message*/
extern uint8_t gasnetc_get_AM_type (uint16_t header);

/* Return the # of argument for the given message*/
extern uint8_t gasnetc_get_msg_num_arg (uint16_t header);

/* Generate Control Message Header*/
extern void gasnetc_construct_Control_command (gasnetc_command_t *temp);

/* Generate Short / Medium Message Header*/
extern void gasnetc_construct_ShortMedium_command (void *input_addr, gasnet_handler_t handler, 
											uint8_t msg_type, uint8_t AM_type, size_t size, uint8_t num_args, gasnet_handlerarg_t args[]);

/* Generate Long Message Header*/
extern void gasnetc_construct_Long_command (void *input_addr, gasnet_handler_t handler, 
								uint8_t msg_type, void *payload, size_t size, uint8_t num_args, gasnet_handlerarg_t args[]);

/********************************************************
				All AM Transfer Functions
********************************************************/

/* Calculate the correct size of the header*/
extern int gasnetc_get_header_size (int AM_type, int num_arg);

/********************************************************
			Short/Medium AM Transfer Functions
********************************************************/

/* 0 copy 2 transfer SM transfer*/
extern int gasnetc_SM_transfer (gasnet_node_t dest, uint8_t msg_number, uint8_t msg_type, uint8_t AM_type, gasnet_handler_t handler, 
						int numargs, gasnet_handlerarg_t args[], void *payload, size_t segment_size, 
						bool *remote_msg_flag_addr, void *long_payload);

/* SM Request*/
extern int gasnetc_SM_request (gasnet_node_t dest, uint8_t AM_type, gasnet_handler_t handler, 
						int numargs, gasnet_handlerarg_t args[], void *payload, size_t segment_size, 
						bool *remote_msg_flag_addr, void *long_payload);

/********************************************************
		Long AM Initialization/Transfer Functions
********************************************************/

/* Allocates space for local dma queues*/
extern int gasnetc_create_dma_queues ();

/* Remove the previously allocated local dma queues*/
extern int gasnetc_remove_dma_queues ();

/* Transfer the Long message using the DMA transfer method*/
extern int gasnetc_DMA_write (gasnet_node_t dest, void *source_addr, size_t nbytes, void *dest_addr);

/********************************************************
		Environment Setup/Remove Functions
********************************************************/

/* Initialize necessary system variables*/
extern void gasnetc_setup_env ();

/* Remove system variables*/
extern void gasnetc_free_env ();

#endif

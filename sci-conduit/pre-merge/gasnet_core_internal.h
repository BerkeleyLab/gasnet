/*  $Archive:: /Ti/GASNet/sci-conduit/gasnet_core_internal.h         $
 *     $Date: 2003/10/11 14:22:41 $
 * $Revision: 1.1.2.1 $
 * Description: GASNet sci conduit header for internal definitions in Core API
 * Copyright 2002, Dan Bonachea <bonachea@cs.berkeley.edu>
 *				   Hung-Hsun Su <su@hcs.ufl.edu>
 *				   Burt Gordon <gordon@hcs.ufl.edu>
 * Terms of use are as specified in license.txt
 */

#ifndef _GASNET_CORE_INTERNAL_H
#define _GASNET_CORE_INTERNAL_H

/********************************************************
					GASNet SCI Conduit 
				   Core API Header File
********************************************************/
/*
#include <gasnet.h>
#include <gasnet_internal.h>
*/

// SCI conduit specific headers
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

/****************************************************************************/
// EXTRA STUFF ADDED TO TEST FUNCTIONS BEFORE INTEGRATION. REMOVE AFTER INTEGRATION
#define NOT_INIT 0
#define GASNET_OK 1
#define GASNET_PAGESIZE 4

typedef unsigned char uint8_t;
typedef unsigned short uint16_t;
typedef unsigned int uint32_t;
typedef unsigned long uint64_t;
typedef unsigned int uintptr_t;
typedef uint16_t gasnet_node_t;
typedef uint8_t gasnet_handler_t;
typedef int32_t gasnet_handlerarg_t;
typedef struct 
{
	gasnet_handler_t index; /*  == 0 for don't care  */
    void (*fnptr)();    
} gasnet_handlerentry_t;

typedef struct 
{
	void *addr;
	uintptr_t size;
} gasnet_seginfo_t;

#define GASNET_INLINE_MODIFIER(fnname) static CC_INLINE_MODIFIER;
void gasneti_fatalerror(char * input, ...);
void GASNETI_RETURN_ERRR(int i, char * input);
void gasneti_freezeForDebugger();
void GASNETC_CHECKATTACH();
gasnet_handlerentry_t const *gasnetc_get_handlertable();
extern uintptr_t gasnetc_MaxLocalSegmentSize;
extern uintptr_t gasnetc_MaxGlobalSegmentSize;
extern gasnet_seginfo_t *gasnetc_seginfo;
extern int gasnetc_init_done; /*  true after init */
extern int gasnetc_attach_done; /*  true after attach */
extern gasnet_node_t gasnetc_mynode;
extern gasnet_node_t gasnetc_nodes;
int gasneti_init_done;
int gasneti_attach_done;

#ifdef DEBUG
  #ifndef GASNETI_FORCE_TRUE_MUTEXES
    /* GASNETI_FORCE_TRUE_MUTEXES will force gasneti_mutex_t to always
       use true locking (even under GASNET_SEQ config), 
       for inherently multi-threaded conduits such as lapi-conduit
     */
    #define GASNETI_FORCE_TRUE_MUTEXES 0
  #endif
  #define GASNETI_MUTEX_NOOWNER       -1
  #ifndef GASNETI_THREADIDQUERY
    /* allow conduit override of thread-id query */
    #if defined(GASNET_PAR) || GASNETI_FORCE_TRUE_MUTEXES
      #define GASNETI_THREADIDQUERY()   ((uintptr_t)pthread_self())
    #else
      #define GASNETI_THREADIDQUERY()   (0)
    #endif
  #endif
  #if defined(GASNET_PAR) || GASNETI_FORCE_TRUE_MUTEXES
    #include <pthread.h>
    typedef struct {
      pthread_mutex_t lock;
      uintptr_t owner;
    } gasneti_mutex_t;
    #if defined(PTHREAD_ADAPTIVE_MUTEX_INITIALIZER_NP)
      /* These are faster, though less "featureful" than the default
       * mutexes on linuxthreads implementations which offer them.
       */
      #define GASNETI_MUTEX_INITIALIZER { PTHREAD_ADAPTIVE_MUTEX_INITIALIZER_NP, (uintptr_t)GASNETI_MUTEX_NOOWNER }
    #else
      #define GASNETI_MUTEX_INITIALIZER { PTHREAD_MUTEX_INITIALIZER, (uintptr_t)GASNETI_MUTEX_NOOWNER }
    #endif
    #define gasneti_mutex_lock(pl) do {                                \
              int retval;                                              \
              assert((pl)->owner != GASNETI_THREADIDQUERY());          \
              retval = pthread_mutex_lock(&((pl)->lock));              \
              assert(!retval);                                         \
              assert((pl)->owner == (uintptr_t)GASNETI_MUTEX_NOOWNER); \
              (pl)->owner = GASNETI_THREADIDQUERY();                   \
            } while (0)
    #define gasneti_mutex_unlock(pl) do {                     \
              int retval;                                     \
              assert((pl)->owner == GASNETI_THREADIDQUERY()); \
              (pl)->owner = (uintptr_t)GASNETI_MUTEX_NOOWNER; \
              retval = pthread_mutex_unlock(&((pl)->lock));   \
              assert(!retval);                                \
            } while (0)
    #define gasneti_mutex_init(pl) do {                       \
              pthread_mutex_init(&((pl)->lock),NULL);         \
             (pl)->owner = (uintptr_t)GASNETI_MUTEX_NOOWNER; \
            } while (0)
    #define gasneti_mutex_destroy(pl)  pthread_mutex_destroy(&((pl)->lock))
  #else
    typedef struct {
      volatile int owner;
    } gasneti_mutex_t;
    #define GASNETI_MUTEX_INITIALIZER   { GASNETI_MUTEX_NOOWNER }
    #define gasneti_mutex_lock(pl) do {                     \
              assert((pl)->owner == GASNETI_MUTEX_NOOWNER); \
              (pl)->owner = GASNETI_THREADIDQUERY();        \
            } while (0)
    #define gasneti_mutex_unlock(pl) do {                     \
              assert((pl)->owner == GASNETI_THREADIDQUERY()); \
              (pl)->owner = GASNETI_MUTEX_NOOWNER;            \
            } while (0)
    #define gasneti_mutex_init(pl) do {                       \
              (pl)->owner = GASNETI_MUTEX_NOOWNER;            \
            } while (0)
    #define gasneti_mutex_destroy(pl)
  #endif
  #define gasneti_mutex_assertlocked(pl)    assert((pl)->owner == GASNETI_THREADIDQUERY())
  #define gasneti_mutex_assertunlocked(pl)  assert((pl)->owner != GASNETI_THREADIDQUERY())
#else
  #if defined(GASNET_PAR) || GASNETI_FORCE_TRUE_MUTEXES
    #include <pthread.h>
    typedef pthread_mutex_t           gasneti_mutex_t;
    #if defined(PTHREAD_ADAPTIVE_MUTEX_INITIALIZER_NP)
      /* These are faster, though less "featureful" than the default
       * mutexes on linuxthreads implementations which offer them.
       */
      #define GASNETI_MUTEX_INITIALIZER PTHREAD_ADAPTIVE_MUTEX_INITIALIZER_NP
    #else
      #define GASNETI_MUTEX_INITIALIZER PTHREAD_MUTEX_INITIALIZER
    #endif
    #define gasneti_mutex_lock(pl)  pthread_mutex_lock(pl)
    #define gasneti_mutex_unlock(pl)  pthread_mutex_unlock(pl)
    #define gasneti_mutex_init(pl)  pthread_mutex_init((pl),NULL)
    #define gasneti_mutex_destroy(pl)  pthread_mutex_destroy(pl)
  #else
    typedef char           gasneti_mutex_t;
    #define GASNETI_MUTEX_INITIALIZER '\0'
    #define gasneti_mutex_lock(pl)    
    #define gasneti_mutex_unlock(pl)  
    #define gasneti_mutex_init(pl)
    #define gasneti_mutex_destroy(pl)
  #endif
  #define gasneti_mutex_assertlocked(pl)
  #define gasneti_mutex_assertunlocked(pl)
#endif

/****************************************************************************/

/********************************************************
					Data Structure
********************************************************/

// SCI conduit specific data structures
typedef struct
{
	gasnet_node_t source_id;
	uint8_t msg_number;
} gasnet_token_t;

typedef struct
{
	uint16_t header;	// handler (8 bits) + msg type (1 bit, request/reply) + AM type (2 bits) + num_arg (4 bits)
						// msg type: 0 = request; 1 = reply;
						// AM type: 0 = short; 1 = medium; 2 = long; 3 = control (basic return msg to free mls);
} gasnetc_command_t;

typedef struct
{
	uint16_t header;	// handler (8 bits) + msg type (1 bit, request/reply) + AM type (2 bits) + num_arg (4 bits)
						// msg type: 0 = request; 1 = reply;
						// AM type: 0 = short; 1 = medium; 2 = long; 3 = control (basic return msg to free mls);
	gasnet_token_t token;
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
	gasnet_token_t token;	
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
	gasnet_token_t token;
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

// SCI conduit specific constants
#define GASNETC_SCI_NO_CALLBACK         		NULL
#define GASNETC_SCI_NO_FLAGS            		0
#define GASNETC_SCI_DATA_TRANSFER_READY 		8
#define GASNETC_SCI_FILE 						"gasnet_nodes.sci"
#define	GASNETC_SCI_FAST_SEG					262144	//256KB seg size for fast implementation
#define	GASNETC_SCI_PERCENT						10		//divide the memory into tenths
#define GASNETC_SCI_TRUE						1
#define GASNETC_SCI_FALSE						0
#define GASNETC_SCI_MAX_REQUEST_MSG				2
#define GASNETC_SCI_MAX_HANDLER_NUMBER			256
#define GASNETC_SCI_COMMAND_MESSAGE_SIZE		1024
#define GASNETC_SCI_NUM_DMA_QUEUE				1
#define GASNETC_SCI_MAX_LONG_PAYLOAD_SIZE		500000
#define GASNETC_SCI_MAX_DMA_QUEUE_USAGE			1
#define GASNETC_SCI_REQUEST						0
#define GASNETC_SCI_REPLY						1
#define GASNETC_SCI_SHORT						0
#define GASNETC_SCI_MEDIUM						1
#define GASNETC_SCI_LONG						2
#define GASNETC_SCI_CONTROL						3
#define GASNETC_ONE_MB							1048576 //define 1 MB

/********************************************************
					Global Variables
********************************************************/

// SCI conduit specific Global Variables
extern sci_desc_t						*gasnetc_sci_sd, *gasnetc_sci_gb_sd;				//SCI virtual device descriptors
extern sci_desc_t						*gasnetc_sci_sd_remote, *gasnetc_sci_sd_long;	
extern sci_desc_t						gasnetc_sci_gas_seg;
extern sci_local_segment_t				*gasnetc_sci_localSegment;							//Handlers to local memory segment
extern sci_remote_segment_t				*gasnetc_sci_remoteSegment;							//Handlers to remote memory segment
extern sci_remote_segment_t				*gasnetc_sci_remoteSegment_gb;
extern sci_remote_segment_t				*gasnetc_sci_remoteSegment_long;
extern sci_map_t						*gasnetc_sci_localMap;								//Handlers to locally mapped segment
extern sci_map_t						*gasnetc_sci_remoteMap;								//Handlers to remotely mapped segment
extern sci_map_t						*gasnetc_sci_remoteMap_gb;							//Handlers to remotely mapped segment for global bytes
extern unsigned int						gasnetc_sci_localAdapterNo;
extern unsigned int						gasnetc_sci_localSCIId;
extern unsigned int						*gasnetc_sci_remoteNodeId;
extern unsigned int						*gasnetc_sci_localSegmentId;						//host-wide unique segment identifier
extern unsigned int						*gasnetc_sci_remoteSegmentId;
extern unsigned int						*gasnetc_sci_remoteSegmentId_gb;
extern unsigned int						gasnetc_sci_offset;									//default offset within the segment
extern unsigned int						gasnetc_sci_max_local_seg;							//the values of the maximum segment sizes in system
extern unsigned int						gasnetc_sci_max_global_seg;							//across all systems
extern unsigned int						*gasnetc_sci_SCI_Ids;
extern void 							**gasnetc_sci_local_mem;							//an array of void pointers to each local segment
extern void 							**gasnetc_sci_remote_mem;							//an array of void pointers to each remote segment
extern void 							**gasnetc_sci_global_ready;
extern bool 							*gasnetc_sci_msg_flag;
extern sci_map_t						*gasnetc_sci_local_dma_map;
extern sci_local_segment_t				*gasnetc_sci_local_dma_segment;
extern sci_dma_queue_t					*gasnetc_sci_local_dma_queue;
extern sci_desc_t						*gasnetc_sci_local_dma_sd;
extern void								**gasnetc_sci_local_dma_addr;

extern void								*gasnetc_sci_handler_table[256];
extern uint8_t							*gasnetc_sci_msg_loc_status;

/********************************************************
					SCI SETUP FUNCTIONS					
********************************************************/

// BARRIER FUNCTION
// Uses the global ready region and has everybody write a 1 into their message ready flag
// then waits to see that everybody has completed setting the flags, then returns.
// IMPORTANT -- This should not be used after gasnet_attach(), it will interfere with 
// 			AM messages being sent and received.
void gasnetc_sci_internal_Barrier();

// Parses all the SCI Ids and places them in gasnetc_sci_SCI_Ids.
// The corresponding GASNet node ID is the index of the location of the SCI id in the
// array.
int gasnetc_parseSCIIds(FILE *node_info, const int number);

// Returns the Max_local_Segment size in bytes based on available 
// free mem on the system and writes the size to a global file
int gasnetc_get_free_mem();

// Returns the maximum global segment size. This is the minimum of the segment
// sizes available over the whole cluster. Return -1 if not all node information
// is in the file, throws an error otherwise.
int gasnetc_getSCIglobal_seg(int number);

// This uses the number given to it to create the segments needed by the command
// region. It will leave an open spot for the gasnet segment space to be
// the next to last segment. It will be created in GASNet Attach.
gasnet_node_t gasnetc_SCI_Create_Connections(int number);

// Connects all the command regions and then all the global ready bytes
// across all nodes.
int gasnetc_SCI_connect_cmd(int number);

// This function finds the total number of nodes that will run GASNet as well as 
// assign GASNet Node IDS to each SCI ID. Additionally, the total amount of free
// memory on the system is determined and a percentage is used as the MAX_local_Seg
// size. 
unsigned int gasnetc_SCIInit(gasnet_node_t *my_node);

// Create the payload (GASNET segment) segment and set it available.
void* gasnetc_create_gasnetc_sci_seg(uintptr_t *segsize, int index);

// This waits for all nodes to write their segment sizes and addresses to the
// mailboxes. Then gets all the segment info and places int the segment array
// info.
void gasnetc_get_SegInfo(gasnet_seginfo_t* gasnetc_sci_seginfo, uintptr_t segsize,  void * segbase);

/********************************************************
				   Segment Info Table
********************************************************/

// Return the starting address of a segment with a given Input node ID
extern void * gasnetc_sit_get_base_addr (gasnet_node_t InputID);

// Return the size of a segment with a given Input node ID
extern size_t gasnetc_sit_get_size (gasnet_node_t InputID);

/********************************************************
				 Segment ID Calculation
********************************************************/

// Return the next available temporary segment id for the local node
extern unsigned int gasnetc_get_temp_seg_id ();

/********************************************************
				Local/Remote segment Info
********************************************************/

// Return the local memory address dedicated for the remote node
extern void * gasnetc_ls_get_addr (gasnet_node_t RemoteID);

// Return the memory address (ptr, virtual) to the dedicated segment on the remote node
extern void * gasnetc_rs_get_addr (gasnet_node_t RemoteID);

// Return the remote map handler (SCI) created for the dedicated segment on the remote node
extern sci_map_t gasnetc_rs_get_rmap (gasnet_node_t RemoteID);

// Return the memory address (ptr, virtual) to the control segment on the remote node
extern void * gasnetc_gr_get_addr (gasnet_node_t RemoteID);

// Return the remote segment handler (SCI) created for the dedicated segment on the remote node
extern sci_remote_segment_t gasnetc_rs_get_seg (gasnet_node_t RemoteID);

// Calculates the appropriate offset required based on the input address and the starting address of a remote payload segment
extern int gasnetc_rs_get_offset (gasnet_node_t RemoteID, void * dest_addr);

/********************************************************
				Message Location Status
		-- Use by Local node to keep track of --
		--	command msgs to all other nodes   --
********************************************************/

// Allocate space and initialize the MLS
extern void gasnetc_mls_init ();

// Check if the given location is free or not
extern int gasnetc_mls_chk_free (gasnet_node_t RemoteID, uint8_t msg_number);

// Set a given message space to un-occupied (false)
extern void gasnetc_mls_release (gasnet_node_t RemoteID, uint8_t msg_num);

// Obtain a free request message location
extern int gasnetc_mls_get_loc (gasnet_node_t RemoteID);

/********************************************************
					  Handler Table
********************************************************/

// Allocate and initialize the handler table
extern void gasnetc_ht_init();

// Add a new handler to the table using a given index
extern void gasnetc_ht_add_handler (void * func_ptr, int index);

// Return the function pointer base of a given handler
extern void * gasnetc_ht_get_handler (gasnet_handler_t input);

/********************************************************
					 Handler Running
********************************************************/

// Runs short message handler
extern void gasnetc_run_handler_short (gasnet_token_t token, void* func_ptr, int numargs, gasnet_handlerarg_t *args);

// Runs medium/long message handler
extern void gasnetc_run_handler_mediumlong (gasnet_token_t token, void* func_ptr, int numargs, gasnet_handlerarg_t *args, void *payload, size_t payload_size);

/********************************************************
					  Work Queue
********************************************************/

// Adds the given message to the work queue
extern int gasnetc_enqueue_msg (gasnet_node_t node_id, uint8_t msg_number);

// Removes the first element of the work queue
extern void * gasnetc_dequeue_msg (gasnet_node_t *sender_id, uint8_t *msg_number);

// Scans the MRFs to enqueue new messages
extern void * gasnetc_MRF_scan (gasnet_node_t *sender_id, uint8_t *msg_number);

/********************************************************
			Command Segment Related Functions
********************************************************/

// Return the Handler # for the given message
extern gasnet_handler_t gasnetc_get_msg_handler (uint16_t header);

// Return the type (Request/Reply) for the given message
extern uint8_t gasnetc_get_msg_type (uint16_t header);

// Return the AM type (Short/Medium/Long) for the given message
extern uint8_t gasnetc_get_AM_type (uint16_t header);

// Return the # of argument for the given message
extern uint8_t gasnetc_get_msg_num_arg (uint16_t header);

// Generate Control Message Header
extern void gasnetc_construct_Control_command (gasnetc_command_t *temp);

// Generate Short / Medium Message Header
extern void gasnetc_construct_ShortMedium_command (gasnetc_ShortMedium_command_t *temp, gasnet_handler_t handler, 
											uint8_t msg_type, uint8_t AM_type, size_t size, uint8_t num_args, gasnet_handlerarg_t args[]);

// Generate Long Message Header
extern void gasnetc_construct_Long_command (gasnetc_Long_command_t *temp, gasnet_handler_t handler, 
								uint8_t msg_type, void *payload, size_t size, uint8_t num_args, gasnet_handlerarg_t args[]);

/********************************************************
				All AM Transfer Functions
********************************************************/

// Calculate the correct size of the header
extern int gasnetc_get_header_size (int AM_type, int num_arg);

/********************************************************
			Short/Medium AM Transfer Functions
********************************************************/

// 0 copy 2 transfer SM transfer
extern int gasnetc_SM_transfer (gasnet_node_t dest, uint8_t msg_number, uint8_t msg_type, uint8_t AM_type, gasnet_handler_t handler, 
						int numargs, gasnet_handlerarg_t args[], void *payload, size_t segment_size, 
						bool *remote_msg_flag_addr, void *long_payload);

// SM Request
extern int gasnetc_SM_request (gasnet_node_t dest, uint8_t AM_type, gasnet_handler_t handler, 
						int numargs, gasnet_handlerarg_t args[], void *payload, size_t segment_size, 
						bool *remote_msg_flag_addr, void *long_payload);

/********************************************************
		Long AM Initialization/Transfer Functions
********************************************************/

// Allocates space for local dma queues
extern int gasnetc_create_dma_queues ();

// Remove the previously allocated local dma queues
extern int gasnetc_remove_dma_queues ();

// Transfer the Long message using the DMA transfer method
extern int gasnetc_DMA_write (gasnet_node_t dest, void *source_addr, size_t nbytes, void *dest_addr);

/********************************************************
		Environment Setup/Remove Functions
********************************************************/

// Initialize necessary system variables
extern void gasnetc_setup_env ();

// Remove system variables
extern void gasnetc_free_env ();

#endif

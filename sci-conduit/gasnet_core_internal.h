/*  $Archive:: /Ti/GASNet/sci-conduit/gasnet_core_internal.h         $
 *     $Date: 2004/07/05 22:42:23 $
 * $Revision: 1.2 $
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

/*  SCI conduit specific headers */
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

/********************************************************
					Data Structure
********************************************************/

/*  SCI conduit specific data structures */
typedef struct
{
	gasnet_node_t source_id;
	uint8_t msg_number;
} gasnetc_sci_token_t;

typedef struct
{
	gasnet_node_t source_id;
	uint8_t msg_number;
	uint8_t msg_type;
} gasnetc_sci_msg_list_t;

typedef struct
{
	uint16_t header;	/*  handler (8 bits) + msg type (1 bit, request/reply) + AM type (2 bits) + num_arg (5 bits) */
                                /*  msg type: 0 = request; 1 = reply; */
                                /*  AM type: 0 = short; 1 = medium; 2 = long; 3 = control (basic return msg to free mls); */
} gasnetc_Command_header_t;

typedef struct
{
	uint16_t header;
	uint16_t payload_size;
	gasnet_handlerarg_t args[16];
} gasnetc_ShortMedium_header_t;

typedef struct
{
	uint16_t header;
	uint16_t payload_size;
	void * payload;
	gasnet_handlerarg_t args[16];
} gasnetc_Long_header_t;

typedef uint8_t bool;

typedef void (*gasnetc_handler_short) (gasnet_token_t token, ...);
typedef void (*gasnetc_handler_mediumlong)(gasnet_token_t token, void *buf, size_t nbytes, ...);

/********************************************************
					Constants
********************************************************/

#define gasnetc_boundscheck(node,ptr,nbytes) gasneti_boundscheck(node,ptr,nbytes,c)
/*  whether or not to use spin-locking for HSL's */
#define GASNETC_HSL_SPINLOCK 1
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

/*  SCI conduit specific constants */
#define GASNETC_SCI_NO_CALLBACK         		NULL
#define GASNETC_SCI_NO_FLAGS            		0
#define GASNETC_SCI_FILE 				"gasnet_nodes.sci"
#define	GASNETC_SCI_FAST_SEG				1048576
#define GASNETC_SCI_TRUE			        1
#define GASNETC_SCI_FALSE				0
#define GASNETC_SCI_MAX_REQUEST_MSG			2
#define GASNETC_SCI_MAX_HANDLER_NUMBER			256
#define GASNETC_SCI_COMMAND_MESSAGE_SIZE		1096 /*  1024 + 76 = size of the longest header */
#define GASNETC_SCI_MODE_SWITCH_SIZE                    1024 /*  exact size for max medium payload */
#define GASNETC_SCI_NUM_DMA_QUEUE			2
#define GASNETC_SCI_MAX_DMA_QUEUE_USAGE			1
#define GASNETC_SCI_REQUEST				0
#define GASNETC_SCI_REPLY				1
#define GASNETC_SCI_CONTROL				0
#define GASNETC_SCI_SHORT				1
#define GASNETC_SCI_MEDIUM				2
#define GASNETC_SCI_LONG				3
#define GASNETC_SCI_CONTROL_FLAG			10
#define GASNETC_SCI_SHORT_FLAG				11
#define GASNETC_SCI_MEDIUM_FLAG				12
#define GASNETC_SCI_LONG_FLAG				13
#define GASNETC_SCI_ONE_MB				1048576 /* define 1 MB */
#define GASNETC_SCI_TIMEOUT_SEC                         20

/********************************************************
					Global Variables
********************************************************/
extern int GASNETC_BIGPHY_ENABLE;
extern gasnet_seginfo_t		        *gasnetc_seginfo;
extern sci_desc_t			*gasnetc_sci_sd;
extern sci_desc_t			*gasnetc_sci_sd_gb;
extern sci_desc_t                       *gasnetc_sci_sd_remote;
extern sci_desc_t                       *gasnetc_sci_sd_long;
extern sci_local_segment_t		*gasnetc_sci_localSegment;
extern sci_remote_segment_t	        *gasnetc_sci_remoteSegment_long;
extern unsigned int			gasnetc_sci_localAdapterNo;
extern sci_map_t			*gasnetc_sci_localMap;
extern sci_map_t			*gasnetc_sci_remoteMap;
extern sci_map_t			*gasnetc_sci_remoteMap_gb;
extern void 				**gasnetc_sci_global_ready;
extern unsigned int			gasnetc_sci_max_local_seg;
extern unsigned int			gasnetc_sci_max_global_seg;
extern void				*gasnetc_sci_handler_table[256];
extern bool				*gasnetc_sci_msg_loc_status;
extern bool 			        *gasnetc_sci_msg_flag;
extern int                              gasnetc_sci_internal_barrier_flag;
extern int                              *gasnetc_sci_outstanding_msg_count;             /*  order enforcing related */

/********************************************************
                                        BARRIER FUNCTIONS
********************************************************/

void gasnetc_sci_barrier_notify (int barrier_value);

int gasnetc_sci_barrier_try (int barrier_value);

int gasnetc_sci_barrier_wait (int barrier_value);

/********************************************************
					SCI SETUP FUNCTIONS
********************************************************/

/* the first step to exiting */
void gasnetc_sci_call_exit(unsigned int sig);

/*  BARRIER FUNCTION */
/*  Creates a temporary segment and connects to all other nodes' */
/*  temp segment, writes a 1 in their segment at index:gasnetc_mynode, */
/*  then waits until everybody has written a 1 into all of our index spots */
/*  destroys the segment and continues. */
void gasnetc_sci_internal_Barrier();

/*  Parses all the SCI Ids and places them in gasnetc_sci_SCI_Ids. */
/*  The corresponding GASNet node ID is the index of the location of the SCI id in the */
/*  array. */
int gasnetc_parseSCIIds(FILE *node_info, const int number);

/*  Returns the Max_local_Segment size in bytes based on available */
/*  free mem on the system and writes the size to a global file */
int gasnetc_get_free_mem();

/*  Returns the maximum global segment size. This is the minimum of the segment */
/*  sizes available over the whole cluster. Return -1 if not all node information */
/*  is in the file, throws an error otherwise. */
int gasnetc_getSCIglobal_seg(int number);

/*  This uses the number given to it to create the segments needed by the command */
/*  region. It will leave an open spot for the gasnet segment space to be */
/*  the next to last segment. It will be created in GASNet Attach. */
gasnet_node_t gasnetc_SCI_Create_Connections(int number);

/*  Connects all the command regions and then all the global ready bytes */
/*  across all nodes. */
int gasnetc_SCI_connect_cmd(int number);

/*  This function finds the total number of nodes that will run GASNet as well as */
/*  assign GASNet Node IDS to each SCI ID. Additionally, the total amount of free */
/*  memory on the system is determined and a percentage is used as the MAX_local_Seg */
/*  size. */
unsigned int gasnetc_SCIInit(gasnet_node_t *my_node);

/*  Create the payload (GASNET segment) segment and set it available. */
void* gasnetc_create_gasnetc_sci_seg(uintptr_t *segsize, int index);

/*  This waits for all nodes to write their segment sizes and addresses to the */
/*  mailboxes. Then gets all the segment info and places int the segment array */
/*  info. */
void gasnetc_get_SegInfo(gasnet_seginfo_t* gasnetc_sci_seginfo, uintptr_t segsize,  void * segbase);

/********************************************************
                                Local/Remote segment Info
********************************************************/

/*  Return the memory address (ptr, virtual) to the control segment on the remote node */
GASNET_INLINE_MODIFIER(gasnetc_gr_get_addr)
void * gasnetc_gr_get_addr (gasnet_node_t RemoteID)
{
        return gasnetc_sci_global_ready[RemoteID];
}

/*  Return the remote map handler (SCI) created for the dedicated segment on the remote node */
GASNET_INLINE_MODIFIER(gasnetc_rs_get_rmap)
sci_map_t gasnetc_rs_get_rmap (gasnet_node_t RemoteID)
{
        return gasnetc_sci_remoteMap[RemoteID];
}

/*  Calculates the appropriate offset required based on the input address and the starting address of a remote payload segment */
GASNET_INLINE_MODIFIER(gasnetc_rs_get_offset)
int gasnetc_rs_get_offset (gasnet_node_t RemoteID, void * dest_addr)
{
        void * dest_base_addr = gasnetc_seginfo[RemoteID].addr;
        if (dest_addr >= dest_base_addr)
        {
            int offset = (int) (((uint8_t *) dest_addr) - ((uint8_t *) dest_base_addr));
            if ((offset >= 0) && (offset <= gasnetc_seginfo[RemoteID].size))
            {
                    return offset;
            }
            else
            {
                   gasneti_fatalerror ("(%d) request addr of %p is out of range from the base addr %p at node %d (size = %d)", gasnetc_mynode, dest_addr, dest_base_addr, RemoteID, gasnetc_seginfo[RemoteID].size);
            }
        }
        else
        {
            gasneti_fatalerror ("(%d) request addr of %p is smaller than the base addr %p at node %d", gasnetc_mynode, dest_addr, dest_base_addr, RemoteID);
        }
}

/********************************************************
                Order enforcing functions
********************************************************/

GASNET_INLINE_MODIFIER(gasnetc_outstanding_msg_count_status)
int gasnetc_outstanding_msg_count_status (gasnet_node_t node)
{
      return gasnetc_sci_outstanding_msg_count[node];
}

/********************************************************
				Message Location Status
		-- Use by Local node to keep track of --
		--	command msgs to all other nodes   --
********************************************************/

/*  Check if the given location is free or not */
GASNET_INLINE_MODIFIER(gasnetc_mls_status)
bool gasnetc_mls_status (gasnet_node_t RemoteID, uint8_t msg_number)
{
        return gasnetc_sci_msg_loc_status[RemoteID * GASNETC_SCI_MAX_REQUEST_MSG * 2 + msg_number];
}

/*  Set a given message space to un-occupied (false) */
GASNET_INLINE_MODIFIER(gasnetc_mls_set)
void gasnetc_mls_set (gasnet_node_t RemoteID, uint8_t msg_number)
{
        if (RemoteID >= gasnetc_nodes)
        {
            gasneti_fatalerror ("Error: node %d trying to send to node %d outside of system\n", gasnetc_mynode, RemoteID);
        }
        if (msg_number >= GASNETC_SCI_MAX_REQUEST_MSG * 2)
        {
            gasneti_fatalerror ("Error: node %d trying to use mailbox %d, which is not allow\n", gasnetc_mynode, msg_number);
        }
        if (gasnetc_sci_msg_loc_status[RemoteID * GASNETC_SCI_MAX_REQUEST_MSG * 2 + msg_number] == GASNETC_SCI_TRUE)
        {
            gasneti_fatalerror ("Error: node %d Attempt to set a msg location that is already set\n", gasnetc_mynode);
        }
        else
        {
            gasnetc_sci_msg_loc_status[RemoteID * GASNETC_SCI_MAX_REQUEST_MSG * 2 + msg_number] = GASNETC_SCI_TRUE;
        }
}

/********************************************************
					  Handler Table
********************************************************/

/*  Add a new handler to the table using a given index */
void gasnetc_ht_add_handler (void * func_ptr, int index);

/* / Return the function pointer base of a given handler */
GASNET_INLINE_MODIFIER(gasnetc_ht_get_handler)
void * gasnetc_ht_get_handler (gasnet_handler_t input)
{
        return gasnetc_sci_handler_table[input];
}

/********************************************************
				  MSG Flag Management
********************************************************/
/*  return the status of message existense flag */
GASNET_INLINE_MODIFIER(gasnetc_msg_exist_flag_status)
bool gasnetc_msg_exist_flag_status ()
{
        return gasnetc_sci_msg_flag[gasnetc_nodes * GASNETC_SCI_MAX_REQUEST_MSG * 2];
}

/*  set the message existense flag to FALSE */
GASNET_INLINE_MODIFIER(gasnetc_msg_exist_flag_release)
void gasnetc_msg_exist_flag_release ()
{
        gasnetc_sci_msg_flag[gasnetc_nodes * GASNETC_SCI_MAX_REQUEST_MSG * 2] = GASNETC_SCI_FALSE;
}

/*  return the status of msg_flag */
GASNET_INLINE_MODIFIER(gasnetc_msg_flag_status)
bool gasnetc_msg_flag_status (gasnet_node_t node_id, uint8_t msg_number)
{
        return gasnetc_sci_msg_flag[node_id * GASNETC_SCI_MAX_REQUEST_MSG * 2 + msg_number];
}

/*  set the corresponding msg_flag to FALSE */
GASNET_INLINE_MODIFIER(gasnetc_msg_flag_release)
void gasnetc_msg_flag_release (gasnet_node_t node_id, uint8_t msg_number)
{
        gasnetc_sci_msg_flag[node_id * GASNETC_SCI_MAX_REQUEST_MSG * 2 + msg_number] = GASNETC_SCI_FALSE;
}

/*  Scans the MRFs to enqueue new messages */
void gasnetc_MRF_scan ();

/********************************************************
			Command Segment Related Functions
********************************************************/

/*  Return the Handler # for the given message */
GASNET_INLINE_MODIFIER(gasnetc_get_msg_handler)
gasnet_handler_t gasnetc_get_msg_handler (uint16_t header)
{
        return ((gasnet_handler_t) (header>>8));
}

/*  Return the type (Request/Reply) for the given message */
GASNET_INLINE_MODIFIER(gasnetc_get_msg_type)
uint8_t gasnetc_get_msg_type (uint16_t header)
{
        return ((uint8_t) ((header>>7) & 1));
}

/*  Return the AM type (Short/Medium/Long) for the given message */
GASNET_INLINE_MODIFIER(gasnetc_get_AM_type)
uint8_t gasnetc_get_AM_type (uint16_t header)
{
        return ((uint8_t) ((header>>5) & 3));
}

/*  Return the # of argument for the given message */
GASNET_INLINE_MODIFIER(gasnetc_get_msg_num_arg)
uint8_t gasnetc_get_msg_num_arg (uint16_t header)
{
        return ((uint8_t) (header & 31));
}

/********************************************************
			Short/Medium AM Transfer Functions
********************************************************/

/*  0 copy 2 transfer SM transfer */
int gasnetc_SM_transfer (gasnet_node_t dest, uint8_t msg_number, uint8_t msg_type, uint8_t AM_type, gasnet_handler_t handler,
						int numargs, gasnet_handlerarg_t args[], void *payload, size_t segment_size,
						bool *remote_msg_flag_addr, void *long_payload);

/*  SM Request */
int gasnetc_SM_request (gasnet_node_t dest, uint8_t AM_type, gasnet_handler_t handler,
						int numargs, gasnet_handlerarg_t args[], void *payload, size_t segment_size,
						bool *remote_msg_flag_addr, void *long_payload);

/********************************************************
		Long AM Initialization/Transfer Functions
********************************************************/

/*  Allocates space for local dma queues */
int gasnetc_create_dma_queues ();

/*  Remove the previously allocated local dma queues */
int gasnetc_remove_dma_queues ();

/*  Transfer the Long message using the DMA transfer method */
int gasnetc_DMA_write (gasnet_node_t dest, void *source_addr, size_t nbytes, void *dest_addr);

/********************************************************
		Environment Setup/Remove Functions
********************************************************/

/*  Initialize necessary system variables */
void gasnetc_setup_env ();

/*  Remove system variables */
void gasnetc_free_env ();

#endif

/*  $Archive:: /Ti/GASNet/sci-conduit/gasnet_core_internal.c         $
 *     $Date: 2004/03/29 17:46:38 $
 * $Revision: 1.1.2.4 $
 * Description: GASNet sci conduit c-file for internal definitions in Core API
 * Copyright 2002, Dan Bonachea <bonachea@cs.berkeley.edu>
 *				   Hung-Hsun Su <su@hcs.ufl.edu>
 *				   Burt Gordon <gordon@hcs.ufl.edu>
 * Terms of use are as specified in license.txt
 */

/********************************************************
					GASNet SCI Conduit 
				   Core API Header File
********************************************************/

/* Try not to include them more than once */
#ifndef _SISCI_HEADERS
#define _SISCI_HEADERS
#include "sisci_types.h"
#include "sisci_api.h"
#include "sisci_error.h"
#endif

/*Necessary C system calls and definitions info*/
#include <sys/types.h>
#include <sys/time.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <gasnet_core_internal.h>

/********************************************************
					Global Variables
********************************************************/
sci_desc_t				*gasnetc_sci_sd, *gasnetc_sci_gb_sd;				/*SCI virtual device descriptors*/
sci_desc_t				*gasnetc_sci_sd_remote, *gasnetc_sci_sd_long;	
sci_desc_t				gasnetc_sci_gas_seg;
sci_local_segment_t		*gasnetc_sci_localSegment;							/*Handlers to local memory segment*/
sci_remote_segment_t	*gasnetc_sci_remoteSegment;							/*Handlers to remote memory segment*/
sci_remote_segment_t	*gasnetc_sci_remoteSegment_gb;
sci_remote_segment_t	*gasnetc_sci_remoteSegment_long;
sci_map_t				*gasnetc_sci_localMap;								/*Handlers to locally mapped segment*/
sci_map_t				*gasnetc_sci_remoteMap;								/*Handlers to remotely mapped segment*/
sci_map_t				*gasnetc_sci_remoteMap_gb;							/*Handlers to remotely mapped segment for global bytes*/
sci_query_adapter_t		gasnetc_sci_SCIAdapter;								/*Handler to find out current node's SCI ID*/
unsigned int			gasnetc_sci_localAdapterNo = 0;
unsigned int			gasnetc_sci_localSCIId;
unsigned int			*gasnetc_sci_remoteNodeId;
unsigned int			*gasnetc_sci_localSegmentId;						/*host-wide unique segment identifier*/
unsigned int			*gasnetc_sci_remoteSegmentId;
unsigned int			*gasnetc_sci_remoteSegmentId_gb;
unsigned int			gasnetc_sci_offset = 0;								/*default offset within the segment*/
unsigned int			gasnetc_sci_max_local_seg =0;						/*the values of the maximum segment sizes in system*/
unsigned int			gasnetc_sci_max_global_seg =0;						/*across all systems*/
unsigned int			*gasnetc_sci_SCI_Ids;
void 					**gasnetc_sci_local_mem;							/*an array of void pointers to each local segment*/
void 					**gasnetc_sci_remote_mem;							/*an array of void pointers to each remote segment*/
void 					**gasnetc_sci_global_ready;
bool 					*gasnetc_sci_msg_flag;
sci_map_t				*gasnetc_sci_local_dma_map;
sci_local_segment_t		*gasnetc_sci_local_dma_segment;
sci_dma_queue_t			*gasnetc_sci_local_dma_queue;
sci_desc_t				*gasnetc_sci_local_dma_sd;
void					**gasnetc_sci_local_dma_addr;
void					*gasnetc_sci_handler_table[256];				/* array of handler information with the index = handler ID */
																		/* and handler_table[index] = function ptr for the handler*/
int						gasnetc_sci_current_index = 0;	
uint16_t				gasnetc_sci_temp_seg_count = 0;						/* # of temporary segment created, used for dynamic linking to remote DMA segments*/
uint16_t				gasnetc_sci_barrier_count = 0;
uint8_t					*gasnetc_sci_msg_loc_status;	
pthread_mutex_t			gasnetc_sci_msg_loc_mutex = PTHREAD_MUTEX_INITIALIZER;	/* MUTEX variable for the MLS*/
void					*gasnetc_sci_first = NULL;
void					*gasnetc_sci_last = NULL;
pthread_mutex_t			gasnetc_sci_wq_lock = PTHREAD_MUTEX_INITIALIZER;	/* MUTEX variable for the work queue*/
uint8_t					gasnetc_sci_dma_count = 0;							/* Determine the next dma queue to use*/
pthread_mutex_t			gasnetc_sci_poll_lock = PTHREAD_MUTEX_INITIALIZER;

/********************************************************
				MUTEX VARIABLES TO USE					
********************************************************/
/*each SCI function used in the main gasnet functions
/*should have a mutex associated with it*/
pthread_mutex_t gasnetc_mutex_sci_memcpy = PTHREAD_MUTEX_INITIALIZER;
pthread_mutex_t gasnetc_mutex_sci_transfer = PTHREAD_MUTEX_INITIALIZER;
pthread_mutex_t gasnetc_mutex_sci_create_sequence = PTHREAD_MUTEX_INITIALIZER;
pthread_mutex_t gasnetc_mutex_sci_start_sequence  = PTHREAD_MUTEX_INITIALIZER;
pthread_mutex_t gasnetc_mutex_sci_check_sequence  = PTHREAD_MUTEX_INITIALIZER;
pthread_mutex_t gasnetc_mutex_sci_remove_sequence  = PTHREAD_MUTEX_INITIALIZER;
pthread_mutex_t gasnetc_mutex_sci_open  = PTHREAD_MUTEX_INITIALIZER;
pthread_mutex_t gasnetc_mutex_sci_create_segment  = PTHREAD_MUTEX_INITIALIZER;
pthread_mutex_t gasnetc_mutex_sci_prepare_segment  = PTHREAD_MUTEX_INITIALIZER;
pthread_mutex_t gasnetc_mutex_sci_map_segment  = PTHREAD_MUTEX_INITIALIZER;
pthread_mutex_t gasnetc_mutex_sci_unmap_segment  = PTHREAD_MUTEX_INITIALIZER;
pthread_mutex_t gasnetc_mutex_sci_remove_segment  = PTHREAD_MUTEX_INITIALIZER;
pthread_mutex_t gasnetc_mutex_sci_close  = PTHREAD_MUTEX_INITIALIZER;
pthread_mutex_t gasnetc_mutex_sci_create_dma  = PTHREAD_MUTEX_INITIALIZER;
pthread_mutex_t gasnetc_mutex_sci_remove_dma  = PTHREAD_MUTEX_INITIALIZER;
pthread_mutex_t gasnetc_mutex_sci_enqueue_dma  = PTHREAD_MUTEX_INITIALIZER;
pthread_mutex_t gasnetc_mutex_sci_lock1  = PTHREAD_MUTEX_INITIALIZER;
pthread_mutex_t gasnetc_mutex_sci_lock2  = PTHREAD_MUTEX_INITIALIZER;
pthread_mutex_t gasnetc_mutex_sci_lock3  = PTHREAD_MUTEX_INITIALIZER;
pthread_mutex_t gasnetc_mutex_sci_gmrf = PTHREAD_MUTEX_INITIALIZER;
pthread_mutex_t gasnetc_mutex_sci_msgloc = PTHREAD_MUTEX_INITIALIZER;
pthread_mutex_t gasnetc_mutex_sci_zero = PTHREAD_MUTEX_INITIALIZER;

/********************************************************
				 Segment ID Calculation
********************************************************/

/* Return the next available temporary segment id for the local node*/
unsigned int gasnetc_get_temp_seg_id ()
{
	pthread_mutex_lock( &gasnetc_mutex_sci_lock3);

	if (gasnetc_sci_temp_seg_count > gasnetc_nodes + GASNETC_SCI_NUM_DMA_QUEUE)
	{
		gasnetc_sci_temp_seg_count = gasnetc_nodes + 1;		/* reset it so the seg_id = seg_id of global ready segment*/
	}
	if (gasnetc_sci_temp_seg_count == 0)	/* exhaust all temp id, recycle*/
	{
		gasnetc_sci_temp_seg_count = gasnetc_nodes + 1;		/* reset it so the seg_id = seg_id of global ready segment*/
	}
	gasnetc_sci_temp_seg_count++;

	pthread_mutex_unlock( &gasnetc_mutex_sci_lock3);

	return ((((uint32_t) gasnetc_mynode) << 16) | ((uint32_t) gasnetc_sci_temp_seg_count));
}

/*similar to gasnetc_get_temp_id, but only returns the 
/* number to be appended to the end, not the actual segment
/* id.*/
unsigned int gasnetc_get_barrier_id()
{
	pthread_mutex_lock( &gasnetc_mutex_sci_lock3);

	if (gasnetc_sci_barrier_count > (gasnetc_nodes + GASNETC_SCI_NUM_DMA_QUEUE + GASNETC_SCI_MAX_BARRIER))
	{
		gasnetc_sci_barrier_count = gasnetc_nodes + GASNETC_SCI_NUM_DMA_QUEUE + 1;
	}
	if (gasnetc_sci_barrier_count == 0)	/* exhaust all temp id, recycle*/
	{
		gasnetc_sci_barrier_count = gasnetc_nodes + GASNETC_SCI_NUM_DMA_QUEUE + 1;
	}
	gasnetc_sci_barrier_count++;

	pthread_mutex_unlock( &gasnetc_mutex_sci_lock3);

	return gasnetc_sci_barrier_count;
}

/********************************************************
					SCI CALLBACK FUNCTIONS				
********************************************************/

sci_callback_action_t gasnetc_sci_remote_callback(void * arg,  sci_remote_segment_t seg,
									sci_segment_cb_reason_t reason,
									sci_error_t status)
{
	switch(reason)
	{
		case SCI_CB_CONNECT: /*just a notice that somebody connected*/
			return SCI_CALLBACK_CONTINUE;
			break;
		case SCI_CB_DISCONNECT: /*we lost somebody, kill the show*/
			gasnetc_sci_call_exit(1);
			return SCI_CALLBACK_CANCEL;
			break;
		case SCI_CB_LOST:
			gasnetc_sci_call_exit(1);/*we lost somebody, kill the show*/
			return SCI_CALLBACK_CANCEL;
			break;
		default:
			return SCI_CALLBACK_CONTINUE; /*it's not a terminal call, so continue*/
			break;
	}

	return SCI_CALLBACK_CONTINUE;
}


/********************************************************
					SCI SETUP FUNCTIONS				
********************************************************/

/* BARRIER FUNCTION
/* Creates a temporary segment and connects to all other nodes' 
/* temp segment, writes a 1 in their segment at index:gasnetc_mynode,
/* then waits until everybody has written a 1 into all of our index spots
/* destroys the segment and continues.*/
void gasnetc_sci_internal_Barrier()
{
	int count, counter, index;
	unsigned int ID, seg_ID, remote_seg_ID;
	bool* gb_local, *gr_add,**gb_remote;
	sci_desc_t				barrier_sd, *remote_sd;
	sci_local_segment_t		barrier_local;
	sci_remote_segment_t	*barrier_remote;
	sci_map_t				barrier_map, *barrier_map_remote;
	sci_error_t				gasnetc_sci_error;

	SCIOpen(&barrier_sd, GASNETC_SCI_NO_FLAGS, &gasnetc_sci_error);
		if (gasnetc_sci_error != SCI_ERR_OK) 
		{
			gasneti_fatalerror("Barrier SCIOpen failed - Error code: 0x%x\n",gasnetc_sci_error);
		}
	
	remote_sd = (sci_desc_t*)gasneti_malloc(sizeof(sci_desc_t) * gasnetc_nodes);

	for (index = 0; index < gasnetc_nodes ; index++ )
	{
		SCIOpen(&remote_sd[index], GASNETC_SCI_NO_FLAGS, &gasnetc_sci_error);
		if (gasnetc_sci_error != SCI_ERR_OK) 
		{
			gasneti_fatalerror("Barrier SCIOpen failed - Error code: 0x%x\n",gasnetc_sci_error);
		}
	}

	ID = gasnetc_get_barrier_id();
	seg_ID = (gasnetc_mynode << 16) | ID;

	SCICreateSegment(barrier_sd, &barrier_local, seg_ID, gasnetc_nodes, /*create as many bytes as there are nodes*/ 
					GASNETC_SCI_NO_CALLBACK, NULL, GASNETC_SCI_NO_FLAGS, &gasnetc_sci_error);
		if (gasnetc_sci_error != SCI_ERR_OK) 
		{
			gasneti_fatalerror("Barrier, SCICreateSegment failed - Error code: 0x%x\n",gasnetc_sci_error);
		}

	SCIPrepareSegment(barrier_local,gasnetc_sci_localAdapterNo,GASNETC_SCI_NO_FLAGS,&gasnetc_sci_error);
		if (gasnetc_sci_error != SCI_ERR_OK) 
		{
			gasneti_fatalerror("Barrier, Local segment failed, SCIPrepareSegment failed 1 - Error code: 0x%x\n",gasnetc_sci_error);
		}
	
	gb_local = SCIMapLocalSegment(barrier_local,&barrier_map, gasnetc_sci_offset,gasnetc_nodes,
								NULL,GASNETC_SCI_NO_FLAGS,&gasnetc_sci_error);
			if (gasnetc_sci_error != SCI_ERR_OK) 
			{
				gasneti_fatalerror("Problem mapping barrier regions, SCIMapLocalSegment failed 1 - Error code: 0x%x\n",gasnetc_sci_error);
			}
	
	SCISetSegmentAvailable(barrier_local, gasnetc_sci_localAdapterNo, GASNETC_SCI_NO_FLAGS, &gasnetc_sci_error);
		if (gasnetc_sci_error != SCI_ERR_OK) 
		{
		    gasneti_fatalerror("Command regions not available, SCISETAvailableSegment failed 1 - Error code: 0x%x\n",gasnetc_sci_error);
		}
	
	/*Now we need to connect to everybody else's barrier region*/
	barrier_remote = (sci_remote_segment_t*)gasneti_malloc(gasnetc_nodes * sizeof(sci_remote_segment_t));
	barrier_map_remote = (sci_map_t*)gasneti_malloc(gasnetc_nodes * sizeof(sci_map_t));
	for (index = 0; index < gasnetc_nodes ; index++ )
	{
		remote_seg_ID = (index << 16) | ID;
		
		counter = 0;
		do { 
			SCIConnectSegment(remote_sd[index], &barrier_remote[index],gasnetc_sci_SCI_Ids[index],
				remote_seg_ID,gasnetc_sci_localAdapterNo,gasnetc_sci_remote_callback,NULL,SCI_INFINITE_TIMEOUT,
				SCI_FLAG_USE_CALLBACK,&gasnetc_sci_error);
			if(gasnetc_sci_error != SCI_ERR_OK)
				sleep(1);/*connections may not be ready yet, wait a second then try again*/
			counter++;
		} while (gasnetc_sci_error != SCI_ERR_OK && (counter < 20));/*Stop after 20 timeouts of trying*/
	
		if(gasnetc_sci_error != SCI_ERR_OK)/*if this is true, counter is 240 so don't test*/
		{
			gasneti_fatalerror("Barrier Could not make all node connections\n");/*leave gasnet, something is wrong*/
		}
	}

	gb_remote = (bool**)gasneti_malloc( gasnetc_nodes * sizeof(bool*) );

	for (index = 0; index < gasnetc_nodes ; index ++ )
	{
		/*Map the segments to user space*/
		gb_remote[index] = (bool *)SCIMapRemoteSegment(barrier_remote[index],&barrier_map_remote[index],
													gasnetc_sci_offset,gasnetc_nodes,NULL,
													GASNETC_SCI_NO_FLAGS,&gasnetc_sci_error);
		if (gasnetc_sci_error != SCI_ERR_OK) 
		{
			gasneti_fatalerror("Problem mapping Barrier remote segments, SCIMapRemoteSegment failed - Error code 0x%x\n",gasnetc_sci_error);
		} 
	}

	/*Need to write TRUE in my slot in everybody's global ready region*/
	for (index = 0; index < gasnetc_nodes ; index++ )
	{
		gr_add = (bool*)gb_remote[index];
		gr_add[gasnetc_mynode] = 1;
	}

	/*now I should be done sending to everybody, check my own*/
	count = 0;
	index = 0;
	while(count < gasnetc_nodes)
	{
		index++;
		if(gb_local[count] == 1)
		{
			count++;
		}
	}

	/*set unavailable and unmap everything, then destroy segments and descriptors*/
	SCISetSegmentUnavailable(barrier_local, gasnetc_sci_localAdapterNo,
							SCI_FLAG_FORCE_DISCONNECT ,&gasnetc_sci_error);

	for (index = 0; index <  gasnetc_nodes; index++)
	{
	  SCIDisconnectSegment(barrier_remote[index],GASNETC_SCI_NO_FLAGS,&gasnetc_sci_error);
	}

	SCIUnmapSegment(barrier_map, GASNETC_SCI_NO_FLAGS, &gasnetc_sci_error);

	for (index = 0; index < gasnetc_nodes; index++ )
    {
		SCIUnmapSegment(barrier_map_remote[index], GASNETC_SCI_NO_FLAGS, &gasnetc_sci_error);
	}

	SCIClose(barrier_sd, GASNETC_SCI_NO_FLAGS, &gasnetc_sci_error);
	for (index = 0;index < gasnetc_nodes ; index++ )
	{
		SCIClose(remote_sd[index], GASNETC_SCI_NO_FLAGS, &gasnetc_sci_error);
	}

	gasneti_free(remote_sd);
	gasneti_free(barrier_remote);
	gasneti_free(barrier_map_remote);
	gasneti_free(gb_remote);
} /*end barrier*/

/* Parses all the SCI Ids and places them in gasnetc_sci_SCI_Ids.
/* The corresponding GASNet node ID is the index of the location of the SCI id in the
/* array.*/
int gasnetc_parseSCIIds(FILE *node_info, const int number)
{
	int index, scanned;
	char ch;

	for(index = 0; index < number; index++)
	{
		if( fscanf(node_info,"%d %c", &scanned,&ch) != EOF)
		{
			gasnetc_sci_SCI_Ids[index] = scanned;
		}
		else
		{
			gasneti_fatalerror("ERROR: couldn't get SCI node IDs");
		}
	}
	return 1;
}

/* Returns the Max_local_Segment size in bytes based on available 
/* free mem on the system and writes the size to a global file*/
int gasnetc_get_free_mem()
{
	FILE* meminfo;
	int size, found, mod, orig_size;
	char title[100];

	if (GASNETC_BIGPHY_ENABLE)	
	{
		meminfo = fopen("/proc/bigphysarea", "r");
		if(meminfo == NULL)
		{
			gasneti_fatalerror("Problem openning bigphysarea\n");
		}

		found = 0;
		while((found == 0) && (feof(meminfo) == 0))
		{
			fscanf(meminfo, "%s", title);
			if( strcmp(title, "block:") == 0 )
			{
				found = 1;
			}
		}

		if(found == 0)
		{
			gasneti_fatalerror("ERROR: could not find amount of free memory.");
		}
		
		fscanf(meminfo, "%d", &size);
		fclose(meminfo);
		size = size * 1024; /*convert kB to B*/
		orig_size = size;		
		size =  orig_size - (10*GASNETC_ONE_MB); /*save 10MB for command segments = 4 mailboxes on 1280 nodes or 20 mailboxes on 237 nodes*/
		mod = size%GASNET_PAGESIZE; /* make it an even multiple of GASNET_PAGESIZE (should be multiple of 4 for SCI)*/
		size = size - mod;
	}
	else
	{
			/*for now, we can only really use 1MB segment sizes */
			size = GASNETC_ONE_MB;
	}

	gasnetc_sci_max_local_seg = size;

	return size;
}

/* Returns the maximum global segment size. This is the minimum of the segment
/* sizes available over the whole cluster. Return -1 if not all node information
/* is in the file, throws an error otherwise.*/
int gasnetc_getSCIglobal_seg(int number)
{
	int count, index, min = 0, offset = 0;
	unsigned int *Table_Sizes, *segment_size; /*table of all the sizes, and pointer to uint seg info*/
	sci_sequence_t sequence;
	sci_map_t	curr_remote_map;
	sci_error_t	error;
	bool *ready;

	/*place my information in everybody's mailbox*/
	for(index=0; index < gasnetc_nodes; index++)
	{
		unsigned int offset = 0;

		curr_remote_map = gasnetc_sci_remoteMap[index];
		
		do{
			SCICreateMapSequence(curr_remote_map, &sequence, GASNETC_SCI_NO_FLAGS, &error);
		}while(error != SCI_ERR_OK);

		do{
			SCIStartSequence(sequence, GASNETC_SCI_NO_FLAGS, &error);
		}while(error != SCI_ERR_OK);

		do
		{
			SCIMemCpy(sequence, &gasnetc_sci_max_local_seg, curr_remote_map, offset, sizeof(unsigned int), GASNETC_SCI_NO_FLAGS, &error);
			SCICheckSequence(sequence, GASNETC_SCI_NO_FLAGS, &error);
		}while(error != SCI_ERR_OK);
		
	}
	
	gasnetc_sci_internal_Barrier();/*wait for everybody*/

	/*allocate enough space to place them all in the table*/
	Table_Sizes = (unsigned int *) gasneti_malloc( sizeof(unsigned int)* gasnetc_nodes);

	/*get info, place into the table*/
	for(index = 0; index < gasnetc_nodes; index++)
	{
		segment_size = (unsigned int*) gasnetc_sci_local_mem[index];
		Table_Sizes[index] = segment_size[offset];
	}
	
	/*find the minimum value*/
	min = Table_Sizes[0];
	for(index=0; index < gasnetc_nodes ; index++ )
	{
		if(Table_Sizes[index] <  min )
			min = Table_Sizes[index];
	}
		
	/*set the minimum*/
	gasnetc_sci_max_global_seg = min;
	return min;
}

/* This uses the number given to it to create the segments needed by the command
/* region. It will leave an open spot for the gasnet segment space to be
/* the next to last segment. It will be created in GASNet Attach.*/
gasnet_node_t gasnetc_SCI_Create_Connections(int number)
{
	int index;
	gasnet_node_t gasnetc_mynode;/*unsigned short*/
	unsigned int LocalID;
	sci_error_t gasnetc_sci_error;
	sci_query_adapter_t		gasnetc_sci_SCIAdapter;		/*Handler to find out current node's SCI ID*/

	gasnetc_nodes = number;/*set global variable*/
	
	gasnetc_sci_SCIAdapter.subcommand = SCI_Q_ADAPTER_NODEID;
    gasnetc_sci_SCIAdapter.localAdapterNo = 0;
    gasnetc_sci_SCIAdapter.data = &LocalID;

	 #if GASNET_DEBUG_VERBOSE
		/* note - can't call trace macros during gasnet_init because trace system not yet initialized */
		fprintf(stderr,"gasnet creating SCI command segments..."); fflush(stderr);
	#endif

	SCIQuery(SCI_Q_ADAPTER, &gasnetc_sci_SCIAdapter, GASNETC_SCI_NO_FLAGS, &gasnetc_sci_error);
	if (gasnetc_sci_error != SCI_ERR_OK) /*eventually will not exit until it has tried all 3 adapter numbers*/
	{
        gasneti_fatalerror("Cannot find Adapter: %d. SCIQuery failed - Error code: 0x%x\n",0,gasnetc_sci_error);
    }
	
	gasnetc_sci_localAdapterNo = 0;/*since 0 returned successful, set it for now.*/
	gasnetc_sci_offset = 0; /* for creations, never use offsets*/

	for (index = 0; index < number ; index++)
	{
		if (gasnetc_sci_SCI_Ids[index] == LocalID)
		{
			gasnetc_mynode = index;
			break;
		}
	}
	
	/*first open all sci virtual descriptors*/
	for (index=0; index < (number +2) ; index++)
	{
		SCIOpen(&gasnetc_sci_sd[index], GASNETC_SCI_NO_FLAGS, &gasnetc_sci_error);
		if (gasnetc_sci_error != SCI_ERR_OK) 
		{
			gasneti_fatalerror("SCIOpen failed - Error code: 0x%x\n",gasnetc_sci_error);
		}
	}
	
	/*now create the segments, size of GASNETC_SCI_COMMAND_MESSAGE_SIZE * GASNETC_SCI_MAX_REQUEST_MSG * 2*/
	for (index = 0;index < number ; index++ )
	{
		gasnetc_sci_localSegmentId[index] = (gasnetc_mynode << 16) | index; /*set proper ID, mynodeID is the leading 16 bits*/
		SCICreateSegment(gasnetc_sci_sd[index], &gasnetc_sci_localSegment[index], gasnetc_sci_localSegmentId[index], 
						 GASNETC_SCI_COMMAND_MESSAGE_SIZE * GASNETC_SCI_MAX_REQUEST_MSG * 2, NULL/*callback*/, NULL, 
						 GASNETC_SCI_NO_FLAGS, &gasnetc_sci_error);
		if (gasnetc_sci_error != SCI_ERR_OK) 
		{
			gasneti_fatalerror("%d:Command segments failed, SCICreateSegment failed 1 - Error code: 0x%x\n",gasnetc_mynode,gasnetc_sci_error);
		}
	}

	/*Now create the global ready byte region, leaving the num + 1 region still uncreated*/
	gasnetc_sci_localSegmentId[number+1] = (gasnetc_mynode << 16) | (number+1);
	SCICreateSegment(gasnetc_sci_sd[index], &gasnetc_sci_localSegment[number+1], gasnetc_sci_localSegmentId[number+1], 
		             (number * (GASNETC_SCI_MAX_REQUEST_MSG *2))+1, NULL/*callback*/, NULL, GASNETC_SCI_NO_FLAGS, &gasnetc_sci_error);
		if (gasnetc_sci_error != SCI_ERR_OK) 
		{
			gasneti_fatalerror("Global Ready bytes failed, SCICreateSegment failed 1 - Error code: 0x%x\n",gasnetc_sci_error);
	    }

	/*Prepare the segments*/
	for (index = 0;index < (number+2) ; index++ )
	{
		
		if( index == (number))
		{
			continue; /*skip preparation of payload region as it has not been created yet*/
		}
		else
		{
			SCIPrepareSegment(gasnetc_sci_localSegment[index],gasnetc_sci_localAdapterNo,GASNETC_SCI_NO_FLAGS,&gasnetc_sci_error);
			fflush(stdout);
			if (gasnetc_sci_error != SCI_ERR_OK) 
			{
				gasneti_fatalerror("Local segment failed, SCIPrepareSegment failed 1 - Error code: 0x%x\n",gasnetc_sci_error);
			}
		}
	}
	
	gasnetc_sci_local_mem = (void **) gasneti_malloc( (number +2) * sizeof(void*) );

	/*now map all the segments to usable space*/
	for (index = 0; index < (number+2); index++ )
	{
		if( index == (number))
		{
			continue; /*skip mapping of payload region as it has not been created yet*/
		}
		if( index == (number+1)) /*global ready byte region*/
		{
			gasnetc_sci_local_mem[index] = SCIMapLocalSegment(gasnetc_sci_localSegment[index],&gasnetc_sci_localMap[index], 
											gasnetc_sci_offset,(number * (GASNETC_SCI_MAX_REQUEST_MSG *2))+1, NULL,GASNETC_SCI_NO_FLAGS,&gasnetc_sci_error);
			if (gasnetc_sci_error != SCI_ERR_OK) 
			{
				gasneti_fatalerror("Problem mapping Global ready regions, SCIMapLocalSegment failed 1 - Error code: 0x%x\n",gasnetc_sci_error);
			}
			continue;
		}
		else
		{
			/*Now map the command segments so they may be used */
			gasnetc_sci_local_mem[index] = SCIMapLocalSegment(gasnetc_sci_localSegment[index],&gasnetc_sci_localMap[index], 
											gasnetc_sci_offset,GASNETC_SCI_COMMAND_MESSAGE_SIZE * GASNETC_SCI_MAX_REQUEST_MSG * 2, 
											NULL,GASNETC_SCI_NO_FLAGS,&gasnetc_sci_error);
			if (gasnetc_sci_error != SCI_ERR_OK) 
			{
				gasneti_fatalerror("Command regions didn't map, SCIMapLocalSegment failed 1 - Error code: 0x%x\n",gasnetc_sci_error);
			}
		}
	}
	/* ----------------discarded code, need to clean up later------------------------------------------
	map local global ready region to another place for use in other parts of conduit
	gasnetc_sci_msg_flag = (uint8_t*)gasnetc_sci_local_mem[number+1]; 
	
	//zero all the flags
	for (index=0; index < (number * (GASNETC_SCI_MAX_REQUEST_MSG *2))+1 ; index++ )
	{
		gasnetc_sci_msg_flag[index] = GASNETC_SCI_FALSE;
	}
	-------------------------------------------------------------------------------------------------*/

	/*make the segments available to the world*/
	for (index = 0; index < (number+2); index++)
	{
		if( index == (number))
		{
			continue; /*skip exporting of payload region as it has not been created yet*/
		}
		/*make them available to the outside world*/
		SCISetSegmentAvailable(gasnetc_sci_localSegment[index], gasnetc_sci_localAdapterNo, GASNETC_SCI_NO_FLAGS, &gasnetc_sci_error);
		if (gasnetc_sci_error != SCI_ERR_OK) 
		{
		    gasneti_fatalerror("Command regions not available, SCISETAvailableSegment failed 1 - Error code: 0x%x\n",gasnetc_sci_error);
		}
	}

	 #if GASNET_DEBUG_VERBOSE
		/* note - can't call trace macros during gasnet_init because trace system not yet initialized */
		fprintf(stderr,"done\n"); fflush(stderr);
	#endif

	return gasnetc_mynode;
}

/* Connects all the command regions and then all the global ready bytes
/* across all nodes.*/
int gasnetc_SCI_connect_cmd(int number)
{
	int index, gb_ID, counter = 0;
	int My_ID = gasnetc_mynode;
	sci_error_t gasnetc_sci_error;

	 #if GASNET_DEBUG_VERBOSE
		/* note - can't call trace macros during gasnet_init because trace system not yet initialized */
		fprintf(stderr,"gasnet connecting SCI command segments..."); fflush(stderr);
	#endif

	gasnetc_sci_sd_remote = (sci_desc_t*) gasneti_malloc(number* sizeof(sci_desc_t));

	for (index = 0; index < number ; index++)
	{
		SCIOpen(&gasnetc_sci_sd_remote[index], GASNETC_SCI_NO_FLAGS, &gasnetc_sci_error);
		if (gasnetc_sci_error != SCI_ERR_OK) 
		{
			gasneti_fatalerror("Remote discriptors failed, SCIOpen failed - Error code: 0x%x\n",gasnetc_sci_error);
		}
	}
	
	/*connect all the command regions,even ourselves, global ready bytes come later*/
	for (index = 0; index < number ; index++)
	{
		gasnetc_sci_remoteSegmentId[index] = (index << 16) | My_ID;
		counter = 0;
		do { 
			SCIConnectSegment(gasnetc_sci_sd_remote[index],&gasnetc_sci_remoteSegment[index],gasnetc_sci_SCI_Ids[index],
				gasnetc_sci_remoteSegmentId[index],gasnetc_sci_localAdapterNo,gasnetc_sci_remote_callback,NULL,SCI_INFINITE_TIMEOUT,SCI_FLAG_USE_CALLBACK,&gasnetc_sci_error);
			if(gasnetc_sci_error != SCI_ERR_OK)
				sleep(1);/*connections may not be ready yet, wait a second then try again*/
			counter++;
		} while (gasnetc_sci_error != SCI_ERR_OK && (counter < 20));/**Stop after 20 timeouts of trying**/
	
		if(gasnetc_sci_error != SCI_ERR_OK)/*if this is true, counter is 90 so don't test*/
		{
			gasneti_fatalerror("Could not make all sci node connections\n");/*leave gasnet, something is wrong*/
		}
	}

	gasnetc_sci_remote_mem = (void**) gasneti_malloc( (number +2) * sizeof(void*) );

	for (index = 0; index < number ; index ++ )
	{
		/*Map the segments to user space*/
		gasnetc_sci_remote_mem[index] = (void *)SCIMapRemoteSegment(gasnetc_sci_remoteSegment[index],&gasnetc_sci_remoteMap[index],
													gasnetc_sci_offset,GASNETC_SCI_COMMAND_MESSAGE_SIZE * GASNETC_SCI_MAX_REQUEST_MSG * 2,
													NULL,GASNETC_SCI_NO_FLAGS,&gasnetc_sci_error);
		if (gasnetc_sci_error != SCI_ERR_OK) 
		{
			gasneti_fatalerror("Problem mapping remote segments, SCIMapRemoteSegment failed - Error code 0x%x\n",gasnetc_sci_error);
		} 
	}

	/*Now get all the global ready byte regions*/
	/*Need to open descriptors for each of the byte segments*/
	gasnetc_sci_gb_sd = (sci_desc_t*) gasneti_malloc(sizeof(sci_desc_t) * (number));
	for (index = 0; index < number ; index++)
	{
		SCIOpen(&gasnetc_sci_gb_sd[index], GASNETC_SCI_NO_FLAGS, &gasnetc_sci_error);
		if (gasnetc_sci_error != SCI_ERR_OK) 
		{
			gasneti_fatalerror("Global ready descriptors failed, SCIOpen failed - Error code: 0x%x\n",gasnetc_sci_error);
		}
	}
	gasnetc_sci_remoteSegment_gb = (sci_remote_segment_t*) gasneti_malloc(sizeof(sci_remote_segment_t)*number);

	for (index = 0; index < number ; index++)
	{
		gb_ID = (index << 16) | (number+1);
		counter = 0;
		do { 
			SCIConnectSegment(gasnetc_sci_gb_sd[index],&gasnetc_sci_remoteSegment_gb[index],gasnetc_sci_SCI_Ids[index],
				gb_ID,gasnetc_sci_localAdapterNo,gasnetc_sci_remote_callback,NULL,SCI_INFINITE_TIMEOUT,SCI_FLAG_USE_CALLBACK,&gasnetc_sci_error);
			if (gasnetc_sci_error != SCI_ERR_OK)
				sleep(1);/*may not be ready yet, wait 1 second then try again*/
			counter++;
		} while (gasnetc_sci_error != SCI_ERR_OK &&(counter < 20));/*wait 2 timeouts then end trying to connect*/
		if(gasnetc_sci_error != SCI_ERR_OK)
		{
			gasneti_fatalerror("Could not make proper node connections to global ready byte region.\n");
		}
	}
	gasnetc_sci_global_ready = (void*) gasneti_malloc( number * sizeof(void*) );
	gasnetc_sci_remoteMap_gb = (sci_map_t*) gasneti_malloc( number * sizeof(sci_map_t) );

	/*Map global ready bytes to usable space*/
	for (index = 0; index < number ; index++)
	{
		/*Map the segment to user space*/
		 gasnetc_sci_global_ready[index] = (void *)SCIMapRemoteSegment(gasnetc_sci_remoteSegment_gb[index],
														&gasnetc_sci_remoteMap_gb[index],gasnetc_sci_offset, 
														(number * (GASNETC_SCI_MAX_REQUEST_MSG *2))+1,NULL,GASNETC_SCI_NO_FLAGS,
														&gasnetc_sci_error);
		if (gasnetc_sci_error != SCI_ERR_OK) 
		{
			gasneti_fatalerror("Mapping global ready bytes failed, SCIMapRemoteSegment failed - Error code 0x%x\n",gasnetc_sci_error);
		}
	}

	gasnetc_sci_msg_flag = (bool*) gasnetc_sci_global_ready[gasnetc_mynode];

	/*EVERYTHING, gb too, is connected and mapped*/
	 #if GASNET_DEBUG_VERBOSE
		/* note - can't call trace macros during gasnet_init because trace system not yet initialized */
		fprintf(stderr,"done\n"); fflush(stderr);
	#endif

	return 1;
}

/* This function finds the total number of nodes that will run GASNet as well as 
/* assign GASNet Node IDS to each SCI ID. Additionally, the total amount of free
/* memory on the system is determined and a percentage is used as the MAX_local_Seg
/* size. */
unsigned int gasnetc_SCIInit(gasnet_node_t * my_node)
{
	FILE* node_info;
	int index, number, mem_available;       
	char num_of_nodes[10], *node_ids;

	node_info = fopen(GASNETC_SCI_FILE, "r+"); /*open file that holds all the info*/
	if(node_info == NULL)
	{
		gasneti_fatalerror("Failed to acquire access to SCI initialization information");
	}

	fgets(num_of_nodes,6,node_info); /*upto 5 digits worth of nodes, SCI only supports upto 65,000 nodes*/

	number = atol(num_of_nodes);
	if(number == 0 )
	{
		gasneti_fatalerror("Problem reading number of nodes, or you chose to run on zero nodes.\n");
	}

	/* Now we need to parse through the file and extract the ids, also amount of available mem*/
	gasnetc_sci_SCI_Ids = (unsigned int *) gasneti_malloc( sizeof(unsigned int) * number );
	index = gasnetc_parseSCIIds(node_info, number);
	
	gasnetc_sci_max_local_seg = gasnetc_get_free_mem();

	/*now allocate enough space to create $number+2 number of segments*/
	gasnetc_sci_sd = gasneti_malloc( sizeof(sci_desc_t) * (number + 2) );
	
	/*we know how many there should be, so allocate the space and hop to it*/
	gasnetc_sci_localSegmentId = (unsigned int *) gasneti_malloc( sizeof(unsigned int) * (number +2) );
	gasnetc_sci_localSegment = (sci_local_segment_t *) gasneti_malloc( sizeof(sci_local_segment_t) * (number +2)); 
    gasnetc_sci_remoteSegment = (sci_remote_segment_t *) gasneti_malloc( (sizeof(sci_remote_segment_t)) * (number + 2));
    gasnetc_sci_remoteSegmentId = (unsigned int *) gasneti_malloc( sizeof(unsigned int) * (number + 2));
	gasnetc_sci_localMap = (sci_map_t *) gasneti_malloc(sizeof(sci_map_t) * (number +2) );
	gasnetc_sci_remoteMap = (sci_map_t *) gasneti_malloc(sizeof(sci_map_t) * (number +2) );

	/*create,prepare, map, and export all command regions and global ready byte 
	  returnd the gasnet ID to current system*/
	*my_node = gasnetc_SCI_Create_Connections(number);

	/*Connect all the command regions and global ready bytes to each other*/
	gasnetc_SCI_connect_cmd(number);

	/*Get the minimum max size across all segments*/
	gasnetc_sci_max_global_seg = gasnetc_getSCIglobal_seg(number);

	return number;
}

/* Create the payload (GASNET segment) segment and set it available.*/
void* gasnetc_create_gasnetc_sci_seg(uintptr_t *segsize, int index)
{
	int number = gasnetc_nodes;
	unsigned int Size;
	index = gasnetc_nodes;
	sci_error_t gasnetc_sci_error;

	if (!GASNETC_BIGPHY_ENABLE)
	{
		if(*segsize >= GASNETC_ONE_MB)	/*can't currently use more than 1MB*/
		*segsize = GASNETC_ONE_MB;
	}
	

	Size = *segsize;
	
	SCIOpen(&gasnetc_sci_gas_seg, GASNETC_SCI_NO_FLAGS, &gasnetc_sci_error);
		if (gasnetc_sci_error != SCI_ERR_OK) 
		{
			gasneti_fatalerror("Could not open the necessary descriptors for GASNET segment");
		}
	
	/*index -- the number of nodes, the next to last index in the array, where payload region is located*/
	gasnetc_sci_localSegmentId[index] = (gasnetc_mynode << 16) | index;
	SCICreateSegment(gasnetc_sci_gas_seg, &gasnetc_sci_localSegment[index], gasnetc_sci_localSegmentId[index], 
					 Size, NULL, NULL, GASNETC_SCI_NO_FLAGS, &gasnetc_sci_error);
	if (gasnetc_sci_error != SCI_ERR_OK) 
	{
		gasneti_fatalerror("Node %d failed in creating GASNET segment, SCICreateSegment failed 1 - Error code: 0x%x\n",gasnetc_mynode,gasnetc_sci_error);
    }

	SCIPrepareSegment(gasnetc_sci_localSegment[index],gasnetc_sci_localAdapterNo,GASNETC_SCI_NO_FLAGS,&gasnetc_sci_error);
	if (gasnetc_sci_error != SCI_ERR_OK) 
	{
		gasneti_fatalerror("Problem preparing GASNET segment, SCIPrepareSegment failed 1 - Error code: 0x%x\n",gasnetc_sci_error);
	}

	gasnetc_sci_local_mem[index] = SCIMapLocalSegment(gasnetc_sci_localSegment[index],&gasnetc_sci_localMap[index], 
										gasnetc_sci_offset,Size, NULL,GASNETC_SCI_NO_FLAGS,&gasnetc_sci_error);
	if (gasnetc_sci_error != SCI_ERR_OK) 
	{
		gasneti_fatalerror("Problem mapping GASNET segment internally, SCIMapLocalSegment failed 1 - Error code: 0x%x\n",gasnetc_sci_error);
	}

	/*make them available to the outside world*/
	SCISetSegmentAvailable(gasnetc_sci_localSegment[index], gasnetc_sci_localAdapterNo, GASNETC_SCI_NO_FLAGS, &gasnetc_sci_error);
	if (gasnetc_sci_error != SCI_ERR_OK) 
	{
	    gasneti_fatalerror("Problem making GASNET segment available, SCISETAvailableSegment failed 1 - Error code: 0x%x\n",gasnetc_sci_error);
	}

	/*Now we need to connect the DMA segments from everybody else*/

	gasnetc_sci_remoteSegment_long = (sci_remote_segment_t *) gasneti_malloc(sizeof(sci_remote_segment_t) * number);
	gasnetc_sci_sd_long = (sci_desc_t *) gasneti_malloc(sizeof(sci_desc_t) * number);

	/*open a descriptor for each*/
	for (index = 0; index < number ; index++)
	{
		SCIOpen(&gasnetc_sci_sd_long[index], GASNETC_SCI_NO_FLAGS, &gasnetc_sci_error);
		if (gasnetc_sci_error != SCI_ERR_OK) 
		{
			gasneti_fatalerror("Openning of remote descriptors failed for GASNET segment, SCIOpen failed - Error code: 0x%x\n",gasnetc_sci_error);
		}
	}

	/*connect all the DMA regions, don't map yet*/
	for (index = 0; index < number ; index++)
	{
		int counter = 0;
		unsigned int gasnetc_sci_remoteDMAID = (index << 16) | number;
		do { 
			SCIConnectSegment(gasnetc_sci_sd_long[index],&gasnetc_sci_remoteSegment_long[index],gasnetc_sci_SCI_Ids[index],
				gasnetc_sci_remoteDMAID,gasnetc_sci_localAdapterNo,gasnetc_sci_remote_callback,NULL,SCI_INFINITE_TIMEOUT,SCI_FLAG_USE_CALLBACK,&gasnetc_sci_error);
			if(gasnetc_sci_error != SCI_ERR_OK)
				sleep(1);/*maybe not ready yet, wait 1 second then try again*/
			counter++;
		} while (gasnetc_sci_error != SCI_ERR_OK && (counter< 20)); /*wait for 2 tries after timing out*/
		if(gasnetc_sci_error != SCI_ERR_OK)
			gasneti_fatalerror("Could not connect all GASNET segments.\n");
	}

	return gasnetc_sci_local_mem[index]; /*return the address of the pointer to our GASNET 
										 //segment in our local memory space*/
}

/* This waits for all nodes to write their segment sizes and addresses to the
/* mailboxes. Then gets all the segment info and places int the segment array
/* info.*/
void gasnetc_get_SegInfo( gasnet_seginfo_t * SEG_INFO, uintptr_t segsize,  void * segbase)
{
	int ID, segSize,count, index;
	long address;
	gasnet_seginfo_t *TEMP;
	sci_sequence_t sequence;
	sci_map_t	curr_remote_map;
	sci_error_t	error;
	bool *ready;
	
	ID = gasnetc_mynode;
	TEMP = (gasnet_seginfo_t *) gasneti_malloc(sizeof(gasnet_seginfo_t));/*allocate enough space for one*/

	/*very similar to getting the segment size info, place my segsize and segbase 
	/* into mailbox gasnetc_mynode on everybody else's nodes, then check my mailbox
	/* and gather everybody's segment information and place into the table*/
	
	/*prepare my information for transfer*/
	TEMP->addr = (void*)segbase;
	TEMP->size = segsize;

	/*place my information in everybody's mailbox*/
	for(index=0; index < gasnetc_nodes; index++)
	{
		curr_remote_map = gasnetc_sci_remoteMap[index];
		
		do{
			SCICreateMapSequence(curr_remote_map, &sequence, GASNETC_SCI_NO_FLAGS, &error);
		}while(error != SCI_ERR_OK);

		do{
			SCIStartSequence(sequence, GASNETC_SCI_NO_FLAGS, &error);
		}while(error != SCI_ERR_OK);

		do
		{
			SCIMemCpy(sequence, TEMP, curr_remote_map, 0 /*no offset*/, sizeof(gasnet_seginfo_t), GASNETC_SCI_NO_FLAGS, &error);
			SCICheckSequence(sequence, GASNETC_SCI_NO_FLAGS, &error);
		}while(error != SCI_ERR_OK);	
	}
	
	gasnetc_sci_internal_Barrier();/*wait for everybody*/

	/*get info, place into the table*/
	for(index = 0; index < gasnetc_nodes; index++)
	{
		TEMP = (gasnet_seginfo_t*) gasnetc_sci_local_mem[index];
		SEG_INFO[index].addr = TEMP->addr;
		SEG_INFO[index].size = TEMP->size;
	}
}

/********************************************************
				   Segment Info Table
********************************************************/

/* Return the starting address of a segment with a given Input node ID*/
GASNET_INLINE_MODIFIER(gasnetc_sit_get_base_addr);
void * gasnetc_sit_get_base_addr (gasnet_node_t InputID)
{
	return gasnetc_seginfo[InputID].addr;
}

/* Return the size of a segment with a given Input node ID*/
GASNET_INLINE_MODIFIER(gasnetc_sit_get_size);
size_t gasnetc_sit_get_size (gasnet_node_t InputID)
{
	return gasnetc_seginfo [InputID].size;
}

/********************************************************
				Local/Remote segment Info
********************************************************/

/* Return the local memory address dedicated for the remote node*/
GASNET_INLINE_MODIFIER(gasnetc_ls_get_addr);
void * gasnetc_ls_get_addr (gasnet_node_t RemoteID)
{
	return gasnetc_sci_local_mem [RemoteID];
}

/* Return the memory address (ptr, virtual) to the dedicated segment on the remote node*/
GASNET_INLINE_MODIFIER(gasnetc_rs_get_addr);
void * gasnetc_rs_get_addr (gasnet_node_t RemoteID)
{
	return gasnetc_sci_remote_mem [RemoteID];
}

/* Return the remote map handler (SCI) created for the dedicated segment on the remote node*/
GASNET_INLINE_MODIFIER(gasnetc_rs_get_rmap);
sci_map_t gasnetc_rs_get_rmap (gasnet_node_t RemoteID)
{
	return gasnetc_sci_remoteMap[RemoteID];
}

/* Return the memory address (ptr, virtual) to the control segment on the remote node*/
GASNET_INLINE_MODIFIER(gasnetc_gr_get_addr);
void * gasnetc_gr_get_addr (gasnet_node_t RemoteID)
{
	return gasnetc_sci_global_ready [RemoteID];
}

/* Return the remote segment handler (SCI) created for the dedicated segment on the remote node*/
GASNET_INLINE_MODIFIER(gasnetc_rs_get_seg);
sci_remote_segment_t gasnetc_rs_get_seg (gasnet_node_t RemoteID)
{
	return gasnetc_sci_remoteSegment [RemoteID];
}

/* Calculates the appropriate offset required based on the input address and the starting address of a remote payload segment*/
int gasnetc_rs_get_offset (gasnet_node_t RemoteID, void * dest_addr)
{
	void * dest_base_addr = gasnetc_sit_get_base_addr (RemoteID);
	int offset = (int) ((uint8_t *) dest_addr - (uint8_t *) dest_base_addr);
	if ((offset >= 0) && (offset <= gasnetc_sit_get_size (RemoteID)))
	{
		return offset;
	}
	else
	{
		return -1;	
	}
}

/********************************************************
				Message Location Status
		-- Use by Local node to keep track of --
		--	command msgs to all other nodes   --
********************************************************/

/* Allocate space and initialize the MLS*/
void gasnetc_mls_init ()
{
	int i, end;

	pthread_mutex_lock( &gasnetc_mutex_sci_msgloc);

	gasnetc_sci_msg_loc_status = (uint8_t *) gasneti_malloc ((sizeof (uint8_t)) * (GASNETC_SCI_MAX_REQUEST_MSG * gasnetc_nodes * 2));
	end = (GASNETC_SCI_MAX_REQUEST_MSG * gasnetc_nodes * 2);
	/* Initialize mls array*/
	for (i = 0; i < end; i++)
	{
		gasnetc_sci_msg_loc_status[i] = GASNETC_SCI_FALSE;
	}

	pthread_mutex_unlock( &gasnetc_mutex_sci_msgloc);
}

/* Check if the given location is free or not*/
GASNET_INLINE_MODIFIER(gasnetc_mls_chk_free);
int gasnetc_mls_chk_free (gasnet_node_t RemoteID, uint8_t msg_number)
{
	int i;
	pthread_mutex_lock( &gasnetc_mutex_sci_msgloc);
	i = gasnetc_sci_msg_loc_status[RemoteID * GASNETC_SCI_MAX_REQUEST_MSG * 2 + msg_number];
	pthread_mutex_unlock( &gasnetc_mutex_sci_msgloc);

	return i;
}

/* Set a given message space to un-occupied (false)*/
void gasnetc_mls_release (gasnet_node_t RemoteID, uint8_t msg_num)
{
	pthread_mutex_lock(&gasnetc_sci_msg_loc_mutex);

	gasnetc_sci_msg_loc_status[RemoteID * GASNETC_SCI_MAX_REQUEST_MSG * 2 + msg_num] = GASNETC_SCI_FALSE;

	pthread_mutex_unlock(&gasnetc_sci_msg_loc_mutex);
}

/* Obtain a free request message location*/
int gasnetc_mls_get_loc (gasnet_node_t RemoteID)
{
	uint8_t counter = 0;
	bool found = GASNETC_SCI_FALSE;
	int status = -1;
	pthread_mutex_lock(&gasnetc_sci_msg_loc_mutex);
	while ((counter < (GASNETC_SCI_MAX_REQUEST_MSG)) && (!found))
	{
		if (gasnetc_mls_chk_free (RemoteID, counter) == GASNETC_SCI_FALSE)
		{
			pthread_mutex_lock( &gasnetc_mutex_sci_msgloc);
			gasnetc_sci_msg_loc_status[RemoteID * GASNETC_SCI_MAX_REQUEST_MSG * 2+ counter] = GASNETC_SCI_TRUE;
			pthread_mutex_unlock( &gasnetc_mutex_sci_msgloc);

			found = GASNETC_SCI_TRUE;
		}
		else
		{
			counter++;
		}
	}
	pthread_mutex_unlock(&gasnetc_sci_msg_loc_mutex);

	if (found)
	{
		status = counter;
	}
	return status;
}

/********************************************************
					  Handler Table
********************************************************/

/* Allocate and initialize the handler table*/
void gasnetc_ht_init()
{
	int i;	
	pthread_mutex_lock( &gasnetc_mutex_sci_lock3);
	for (i = 0; i < GASNETC_SCI_MAX_HANDLER_NUMBER; i++) 
	{
		gasnetc_sci_handler_table[i] = NULL;
	}
	pthread_mutex_unlock( &gasnetc_mutex_sci_lock3);
}

/* Add a new handler to the table using a given index*/
void gasnetc_ht_add_handler (void * func_ptr, int index)
{
	pthread_mutex_lock( &gasnetc_mutex_sci_lock3);
	gasnetc_sci_handler_table[index] = func_ptr;
	if (index > gasnetc_sci_current_index)
	{
		gasnetc_sci_current_index = index;
	}
	pthread_mutex_unlock( &gasnetc_mutex_sci_lock3);
}

/* Return the function pointer base of a given handler*/
void * gasnetc_ht_get_handler (gasnet_handler_t input)
{
	void * here;

	pthread_mutex_lock( &gasnetc_mutex_sci_lock3);
	here = gasnetc_sci_handler_table[input];
	pthread_mutex_unlock( &gasnetc_mutex_sci_lock3);

	return here;
}

/********************************************************
					 Handler Running
********************************************************/

/* Runs the short message handler*/
void gasnetc_run_handler_short (gasnet_token_t token, void* func_ptr, int numargs, gasnet_handlerarg_t *args)
{ 
	if (func_ptr != NULL)
	{
		switch (numargs)
		{
			case 0: (*(gasnetc_handler_short)func_ptr)(token); break; 
			case 1: (*(gasnetc_handler_short)func_ptr)(token, args[0]); break;
			case 2: (*(gasnetc_handler_short)func_ptr)(token, args[0], args[1]); break;
			case 3: (*(gasnetc_handler_short)func_ptr)(token, args[0], args[1], args[2]); break;
			case 4: (*(gasnetc_handler_short)func_ptr)(token, args[0], args[1], args[2], args[3]); break;
			case 5: (*(gasnetc_handler_short)func_ptr)(token, args[0], args[1], args[2], args[3], args[4]); break;
			case 6: (*(gasnetc_handler_short)func_ptr)(token, args[0], args[1], args[2], args[3], args[4], args[5]); break;
			case 7: (*(gasnetc_handler_short)func_ptr)(token, args[0], args[1], args[2], args[3], args[4], args[5], args[6]); break;
			case 8: (*(gasnetc_handler_short)func_ptr)(token, args[0], args[1], args[2], args[3], args[4], args[5], args[6], args[7]); break;
			case 9: (*(gasnetc_handler_short)func_ptr)(token, args[0], args[1], args[2], args[3], args[4], args[5], args[6], args[7], args[8]); break;
			case 10: (*(gasnetc_handler_short)func_ptr)(token, args[0], args[1], args[2], args[3], args[4], args[5], args[6], args[7], args[8], args[9]); break;
			case 11: (*(gasnetc_handler_short)func_ptr)(token, args[0], args[1], args[2], args[3], args[4], args[5], args[6], args[7], args[8], args[9], args[10]); break;
			case 12: (*(gasnetc_handler_short)func_ptr)(token, args[0], args[1], args[2], args[3], args[4], args[5], args[6], args[7], args[8], args[9], args[10], args[11]); break;
			case 13: (*(gasnetc_handler_short)func_ptr)(token, args[0], args[1], args[2], args[3], args[4], args[5], args[6], args[7], args[8], args[9], args[10], args[11], args[12]); break;
			case 14: (*(gasnetc_handler_short)func_ptr)(token, args[0], args[1], args[2], args[3], args[4], args[5], args[6], args[7], args[8], args[9], args[10], args[11], args[12], args[13]); break;
			case 15: (*(gasnetc_handler_short)func_ptr)(token, args[0], args[1], args[2], args[3], args[4], args[5], args[6], args[7], args[8], args[9], args[10], args[11], args[12], args[13], args[14]); break;
			case 16: (*(gasnetc_handler_short)func_ptr)(token, args[0], args[1], args[2], args[3], args[4], args[5], args[6], args[7], args[8], args[9], args[10], args[11], args[12], args[13], args[14], args[15]); break;
		}
	}
}

/* Runs medium/long message handler*/
void gasnetc_run_handler_mediumlong (gasnet_token_t token, void* func_ptr, int numargs, gasnet_handlerarg_t *args, void *payload, size_t payload_size)
{
	if (func_ptr != NULL)
	{
		switch (numargs)
		{
			case 0: (*(gasnetc_handler_mediumlong)func_ptr)(token, payload, payload_size); break; 
			case 1: (*(gasnetc_handler_mediumlong)func_ptr)(token, payload, payload_size, args[0]); break;
			case 2: (*(gasnetc_handler_mediumlong)func_ptr)(token, payload, payload_size, args[0], args[1]); break;
			case 3: (*(gasnetc_handler_mediumlong)func_ptr)(token, payload, payload_size, args[0], args[1], args[2]); break;
			case 4: (*(gasnetc_handler_mediumlong)func_ptr)(token, payload, payload_size, args[0], args[1], args[2], args[3]); break;
			case 5: (*(gasnetc_handler_mediumlong)func_ptr)(token, payload, payload_size, args[0], args[1], args[2], args[3], args[4]); break;
			case 6: (*(gasnetc_handler_mediumlong)func_ptr)(token, payload, payload_size, args[0], args[1], args[2], args[3], args[4], args[5]); break;
			case 7: (*(gasnetc_handler_mediumlong)func_ptr)(token, payload, payload_size, args[0], args[1], args[2], args[3], args[4], args[5], args[6]); break;
			case 8: (*(gasnetc_handler_mediumlong)func_ptr)(token, payload, payload_size, args[0], args[1], args[2], args[3], args[4], args[5], args[6], args[7]); break;
			case 9: (*(gasnetc_handler_mediumlong)func_ptr)(token, payload, payload_size, args[0], args[1], args[2], args[3], args[4], args[5], args[6], args[7], args[8]); break;
			case 10: (*(gasnetc_handler_mediumlong)func_ptr)(token, payload, payload_size, args[0], args[1], args[2], args[3], args[4], args[5], args[6], args[7], args[8], args[9]); break;
			case 11: (*(gasnetc_handler_mediumlong)func_ptr)(token, payload, payload_size, args[0], args[1], args[2], args[3], args[4], args[5], args[6], args[7], args[8], args[9], args[10]); break;
			case 12: (*(gasnetc_handler_mediumlong)func_ptr)(token, payload, payload_size, args[0], args[1], args[2], args[3], args[4], args[5], args[6], args[7], args[8], args[9], args[10], args[11]); break;
			case 13: (*(gasnetc_handler_mediumlong)func_ptr)(token, payload, payload_size, args[0], args[1], args[2], args[3], args[4], args[5], args[6], args[7], args[8], args[9], args[10], args[11], args[12]); break;
			case 14: (*(gasnetc_handler_mediumlong)func_ptr)(token, payload, payload_size, args[0], args[1], args[2], args[3], args[4], args[5], args[6], args[7], args[8], args[9], args[10], args[11], args[12], args[13]); break;
			case 15: (*(gasnetc_handler_mediumlong)func_ptr)(token, payload, payload_size, args[0], args[1], args[2], args[3], args[4], args[5], args[6], args[7], args[8], args[9], args[10], args[11], args[12], args[13], args[14]); break;
			case 16: (*(gasnetc_handler_mediumlong)func_ptr)(token, payload, payload_size, args[0], args[1], args[2], args[3], args[4], args[5], args[6], args[7], args[8], args[9], args[10], args[11], args[12], args[13], args[14], args[15]); break;
		}
	}
}

/********************************************************
					  Work Queue
********************************************************/

/* Adds the given message to the work queue*/
int gasnetc_enqueue_msg (gasnet_node_t node_id, uint8_t msg_number)
{
	gasnetc_command_receiver_t * msg_ptr = (gasnetc_command_receiver_t *) (((uint8_t *) gasnetc_sci_local_mem[node_id]) + GASNETC_SCI_COMMAND_MESSAGE_SIZE * msg_number);
	int msg_AM_type = gasnetc_get_AM_type (msg_ptr->header);
	if ((msg_AM_type == GASNETC_SCI_SHORT) || (msg_AM_type == GASNETC_SCI_MEDIUM))
	{
		gasnetc_ShortMedium_command_receiver_t *SM_msg = (gasnetc_ShortMedium_command_receiver_t *) msg_ptr;
		SM_msg->token.source_id = node_id;
		SM_msg->token.msg_number = msg_number;
		SM_msg->next = NULL;
	}
	else if (msg_AM_type == GASNETC_SCI_LONG)
	{
		gasnetc_Long_command_receiver_t *L_msg = (gasnetc_Long_command_receiver_t *) msg_ptr;
		L_msg->token.source_id = node_id;
		L_msg->token.msg_number = msg_number;
		L_msg->next = NULL;
	} 
	else if (msg_AM_type == GASNETC_SCI_CONTROL)
	{
		msg_ptr->token.source_id = node_id;
		msg_ptr->token.msg_number = msg_number;
		msg_ptr->next = NULL;
	}
	
	if (gasnetc_sci_first == NULL)
	{
		gasnetc_sci_first = msg_ptr;
		gasnetc_sci_last = msg_ptr;
	}
	else
	{
		gasnetc_command_receiver_t * last_msg_ptr = (gasnetc_command_receiver_t *) gasnetc_sci_last;

		int last_msg_AM_type = gasnetc_get_AM_type (last_msg_ptr->header);

		if ((last_msg_AM_type == GASNETC_SCI_SHORT) || (last_msg_AM_type == GASNETC_SCI_MEDIUM))
		{
			gasnetc_ShortMedium_command_receiver_t *last_SM_msg = (gasnetc_ShortMedium_command_receiver_t *) last_msg_ptr;
			last_SM_msg->next = msg_ptr;
		}
		else if (last_msg_AM_type == GASNETC_SCI_LONG)
		{
			gasnetc_Long_command_receiver_t *last_L_msg = (gasnetc_Long_command_receiver_t *) last_msg_ptr;
			last_L_msg->next = msg_ptr;
		} 
		else if (last_msg_AM_type == GASNETC_SCI_CONTROL)
		{
			last_msg_ptr->next = msg_ptr;
		}	
		gasnetc_sci_last = msg_ptr;
	}	
	return GASNET_OK;
}

/* Removes the first element of the work queue*/
void * gasnetc_dequeue_msg (gasnet_node_t *sender_id, uint8_t *msg_number)
{	
	pthread_mutex_lock(&gasnetc_sci_wq_lock);
	if (gasnetc_sci_first == NULL)
	{
		*sender_id = -1;
		*msg_number = -1;
		pthread_mutex_unlock(&gasnetc_sci_wq_lock);
		return NULL;
	}
	else
	{
		gasnetc_command_receiver_t *temp_ptr = (gasnetc_command_receiver_t *) gasnetc_sci_first;
		int temp_AM_type = gasnetc_get_AM_type (temp_ptr->header);

		
		if ((temp_AM_type == GASNETC_SCI_SHORT) || (temp_AM_type == GASNETC_SCI_MEDIUM))
		{
			gasnetc_ShortMedium_command_receiver_t *temp_SM_msg = (gasnetc_ShortMedium_command_receiver_t *) temp_ptr;
			*sender_id = temp_SM_msg->token.source_id;
			*msg_number = temp_SM_msg->token.msg_number;
			gasnetc_sci_first = temp_SM_msg->next;
		}
		else if (temp_AM_type == GASNETC_SCI_LONG)
		{
			gasnetc_Long_command_receiver_t *temp_L_msg = (gasnetc_Long_command_receiver_t *) temp_ptr;
			*sender_id = temp_L_msg->token.source_id;
			*msg_number = temp_L_msg->token.msg_number;
			gasnetc_sci_first = temp_L_msg->next;
		} 
		else if (temp_AM_type == GASNETC_SCI_CONTROL)
		{
			*sender_id = temp_ptr->token.source_id;
			*msg_number = temp_ptr->token.msg_number;
			gasnetc_sci_first = temp_ptr->next;
		}		
		pthread_mutex_unlock(&gasnetc_sci_wq_lock);
		return temp_ptr;
	}

}

/* Scans the MRFs to enqueue new messages*/
void * gasnetc_MRF_scan (gasnet_node_t *sender_id, uint8_t *msg_number)
{
	pthread_mutex_lock(&gasnetc_sci_wq_lock);
	int i, j;
	for (i = 0; i < gasnetc_nodes; i++)
	{
		bool test;
		int offset = i * GASNETC_SCI_MAX_REQUEST_MSG * 2;
		for (j = 0; j < (GASNETC_SCI_MAX_REQUEST_MSG * 2); j++)
		{
			pthread_mutex_lock( &gasnetc_mutex_sci_gmrf);
			test = gasnetc_sci_msg_flag[offset + j] ;
			pthread_mutex_unlock( &gasnetc_mutex_sci_gmrf);

			if (test == GASNETC_SCI_TRUE)
			{
				pthread_mutex_lock( &gasnetc_mutex_sci_gmrf);
				gasnetc_sci_msg_flag[offset + j] = GASNETC_SCI_FALSE;	/* reset message ready bit*/
				pthread_mutex_unlock( &gasnetc_mutex_sci_gmrf);
				gasnetc_enqueue_msg ((gasnet_node_t) i, (uint8_t) j);						
			}
		}
	}
	pthread_mutex_unlock(&gasnetc_sci_wq_lock);
	/* check work queue for new job*/
	return gasnetc_dequeue_msg(sender_id, msg_number);
}

/********************************************************
			Command Segment Related Functions
********************************************************/

/* Return the Handler # for the given message*/
GASNET_INLINE_MODIFIER(gasnetc_get_msg_handler);
gasnet_handler_t gasnetc_get_msg_handler (uint16_t header)
{
	return ((gasnet_handler_t) (header>>8));
}

/* Return the type (Request/Reply) for the given message*/
GASNET_INLINE_MODIFIER(gasnetc_get_msg_type);
uint8_t gasnetc_get_msg_type (uint16_t header)
{
	return ((uint8_t) ((header>>7) & 1));
}

/* Return the AM type (Short/Medium/Long) for the given message*/
GASNET_INLINE_MODIFIER(gasnetc_get_AM_type);
uint8_t gasnetc_get_AM_type (uint16_t header)
{
	return ((uint8_t) ((header>>5) & 3));
}

/* Return the # of argument for the given message*/
GASNET_INLINE_MODIFIER(gasnetc_get_msg_num_arg);
uint8_t gasnetc_get_msg_num_arg (uint16_t header)
{
	return ((uint8_t) (header & 15));
}

/* Generate Control Message Header*/
void gasnetc_construct_Control_command (gasnetc_command_t *temp)
{
	temp->header = (((1<<2) | 3)<<5);
}

/* Generate Short / Medium Message Header*/
void gasnetc_construct_ShortMedium_command (void *input_addr, gasnet_handler_t handler, 
											uint8_t msg_type, uint8_t AM_type, size_t size, uint8_t num_args, gasnet_handlerarg_t args[])
{
	gasnetc_ShortMedium_command_t *temp = (gasnetc_ShortMedium_command_t *) input_addr;
	int i;
	temp->header.header = (((((handler<<1) | msg_type)<<2) | AM_type)<<5) | num_args;
	temp->payload_size = (uint16_t) size;
	for (i = 0; i < num_args; i++)
	{
		temp->args[i] = args[i];
	}
}

/* Generate Long Message Header*/
void gasnetc_construct_Long_command (void *input_addr, gasnet_handler_t handler, 
								uint8_t msg_type, void *payload, size_t size, uint8_t num_args, gasnet_handlerarg_t args[])
{
	gasnetc_Long_command_t *temp = (gasnetc_Long_command_t *) input_addr;
	uint8_t AM_type = 2;	/* Long AM*/
	int i;
	temp->header.header = (((((handler<<1) | msg_type)<<2) | AM_type)<<5) | num_args;
	temp->payload_size = (uint16_t) size;
	temp->payload = payload;
	for (i = 0; i < num_args; i++)
	{
		temp->args[i] = args[i];
	}
}

/********************************************************
				All AM Transfer Functions
********************************************************/

/* Calculate the actual header size*/
int gasnetc_get_header_size (int AM_type, int num_arg)
{
	int Size = 0;
	if ((AM_type == GASNETC_SCI_SHORT) || (AM_type == GASNETC_SCI_MEDIUM))
	{
		Size = (sizeof(uint16_t) * 2) + num_arg * (sizeof(gasnet_handlerarg_t)); /* Header + payload_size + args*/
	}
	else if (AM_type == GASNETC_SCI_LONG)
	{
		Size = (sizeof(uint16_t) * 2) + sizeof(void *) + num_arg * (sizeof(gasnet_handlerarg_t));
	}			   
	else if (AM_type == GASNETC_SCI_CONTROL)
	{
		Size = sizeof(gasnetc_command_t);
	}
	/* re-adjust size to align SCI transfer size*/
	Size = Size + (4 - (Size % 4));
	return Size;
}

/********************************************************
			Short/Medium AM Transfer Functions
********************************************************/

/* 0 copy 2 transfer SM transfer*/
int gasnetc_SM_transfer (gasnet_node_t dest, uint8_t msg_number, uint8_t msg_type, uint8_t AM_type, gasnet_handler_t handler, 
						int numargs, gasnet_handlerarg_t args[], void *medium_payload, size_t segment_size, 
						bool *remote_msg_flag_addr, void *long_payload)
{
	void *command;
	sci_error_t error;
	sci_sequence_t sequence;
	unsigned int offset = GASNETC_SCI_COMMAND_MESSAGE_SIZE * msg_number;
	int header_size = gasnetc_get_header_size (AM_type, numargs);
	sci_map_t current_remote_map = gasnetc_rs_get_rmap(dest);

	if (msg_type == GASNETC_SCI_REPLY)
	{
		pthread_mutex_lock( &gasnetc_mutex_sci_msgloc);
		gasnetc_sci_msg_loc_status[dest * GASNETC_SCI_MAX_REQUEST_MSG * 2 + msg_number] = GASNETC_SCI_TRUE;
		pthread_mutex_unlock( &gasnetc_mutex_sci_msgloc);
	}

	if ((AM_type == GASNETC_SCI_SHORT) || (AM_type == GASNETC_SCI_MEDIUM))
	{
		command = gasneti_malloc (sizeof(gasnetc_ShortMedium_command_t));
		gasnetc_construct_ShortMedium_command (command, handler, msg_type, AM_type, segment_size, numargs, args);
	}
	else if (AM_type == GASNETC_SCI_LONG)
	{
		command = gasneti_malloc (sizeof(gasnetc_Long_command_t));
		gasnetc_construct_Long_command (command, handler, msg_type, long_payload, segment_size, numargs, args);
	}
	else
	{
		/* Generates control package to tell sender the message has been handled*/
		command = gasneti_malloc (sizeof(gasnetc_command_t));
		gasnetc_construct_Control_command (command);
	}

	/* Create a new sequence*/
	do
	{
		 pthread_mutex_lock( &gasnetc_mutex_sci_create_sequence );
		SCICreateMapSequence(current_remote_map, &sequence, GASNETC_SCI_NO_FLAGS, &error);
		 pthread_mutex_unlock( &gasnetc_mutex_sci_create_sequence );
	} while (error != SCI_ERR_OK);

	/* Sequence checking provide barrier for header/payload transfer*/
	do
	{
		pthread_mutex_lock( &gasnetc_mutex_sci_start_sequence );
		SCIStartSequence(sequence, GASNETC_SCI_NO_FLAGS, &error);
		pthread_mutex_unlock( &gasnetc_mutex_sci_start_sequence );
		
		pthread_mutex_lock( &gasnetc_mutex_sci_memcpy );
		SCIMemCpy(sequence, command, current_remote_map , offset, header_size, GASNETC_SCI_NO_FLAGS, &error);	/* transfer command package*/
		pthread_mutex_unlock( &gasnetc_mutex_sci_memcpy );

		if ((AM_type == GASNETC_SCI_MEDIUM) && (segment_size > 0))
		{
			int align_offset = segment_size % 4;
			int transfer_size = segment_size - align_offset;
			uint8_t * dest_ptr = (uint8_t *) gasnetc_rs_get_addr (dest);
			uint8_t * source_ptr = (uint8_t *) medium_payload;
			/* transfer data in multiple of 4 bytes*/
			pthread_mutex_lock( &gasnetc_mutex_sci_memcpy );
			SCIMemCpy(sequence, medium_payload, current_remote_map, (offset + sizeof(gasnetc_Long_command_receiver_t)), transfer_size, GASNETC_SCI_NO_FLAGS, &error);
			pthread_mutex_unlock( &gasnetc_mutex_sci_memcpy );

			int k;
			for (k = 0; k < align_offset; k++)
			{
				/* transfer any left over data*/
				pthread_mutex_lock( &gasnetc_mutex_sci_transfer );
				dest_ptr[offset + sizeof(gasnetc_Long_command_receiver_t) + transfer_size + k] = source_ptr [transfer_size + k];
				pthread_mutex_unlock( &gasnetc_mutex_sci_transfer );
			}
		}
		
		pthread_mutex_lock( &gasnetc_mutex_sci_check_sequence );
		SCICheckSequence(sequence, GASNETC_SCI_NO_FLAGS, &error);
		pthread_mutex_unlock( &gasnetc_mutex_sci_check_sequence );
	} while (error != SCI_ERR_OK);

	do
	{
		pthread_mutex_lock( &gasnetc_mutex_sci_remove_sequence );
		SCIRemoveSequence(sequence, GASNETC_SCI_NO_FLAGS, &error);
		pthread_mutex_unlock( &gasnetc_mutex_sci_remove_sequence );
	} while (error != SCI_ERR_OK);

	 pthread_mutex_lock( &gasnetc_mutex_sci_transfer );
	remote_msg_flag_addr[gasnetc_mynode * GASNETC_SCI_MAX_REQUEST_MSG * 2 + msg_number] = 1;/* Write msg ready bit*/
	remote_msg_flag_addr[gasnetc_nodes * GASNETC_SCI_MAX_REQUEST_MSG * 2] = 1;				/* Write global ready bit*/
	 pthread_mutex_unlock( &gasnetc_mutex_sci_transfer );
	
	return GASNET_OK;
}

/* SM Request*/
int gasnetc_SM_request (gasnet_node_t dest, uint8_t AM_type, gasnet_handler_t handler, 
						int numargs, gasnet_handlerarg_t args[], void *medium_payload, size_t segment_size, 
						bool *remote_msg_flag_addr, void *long_payload)
{
	int msg_location;
	do
	{
		msg_location = gasnetc_mls_get_loc (dest);
		if (msg_location == -1)
		{
			gasnetc_AMPoll();
		}
	} while (msg_location == -1);
	return gasnetc_SM_transfer (dest, msg_location, GASNETC_SCI_REQUEST, AM_type, handler, numargs, args, medium_payload, segment_size, remote_msg_flag_addr, long_payload);
}

/********************************************************
		Long AM Initialization/Transfer Functions
********************************************************/

/* Allocates space for local dma queues*/
int gasnetc_create_dma_queues ()
{	 
	int i;
	sci_error_t error;
	unsigned int temp_segment_ID;

	gasnetc_sci_local_dma_map = (sci_map_t *) gasneti_malloc ((sizeof(sci_map_t)) * GASNETC_SCI_NUM_DMA_QUEUE);
	gasnetc_sci_local_dma_segment = (sci_local_segment_t *) gasneti_malloc ((sizeof(sci_local_segment_t)) * GASNETC_SCI_NUM_DMA_QUEUE);
	gasnetc_sci_local_dma_queue = (sci_dma_queue_t *) gasneti_malloc ((sizeof(sci_dma_queue_t)) * GASNETC_SCI_NUM_DMA_QUEUE);
	gasnetc_sci_local_dma_sd = (sci_desc_t *) gasneti_malloc ((sizeof(sci_desc_t)) * GASNETC_SCI_NUM_DMA_QUEUE);
	gasnetc_sci_local_dma_addr = gasneti_malloc ((sizeof(void *)) * GASNETC_SCI_NUM_DMA_QUEUE);

	for (i = 0; i < GASNETC_SCI_NUM_DMA_QUEUE; i++)
	{
		temp_segment_ID = gasnetc_get_temp_seg_id ();
		pthread_mutex_lock( &gasnetc_mutex_sci_open );
		SCIOpen(&(gasnetc_sci_local_dma_sd[i]), GASNETC_SCI_NO_FLAGS, &error);
		pthread_mutex_unlock( &gasnetc_mutex_sci_open );
		if (error != SCI_ERR_OK) 
		{
			gasneti_fatalerror ("SCIOpen() failed \n");
		}

		pthread_mutex_lock( &gasnetc_mutex_sci_create_segment );
		SCICreateSegment(gasnetc_sci_local_dma_sd[i], &(gasnetc_sci_local_dma_segment[i]), temp_segment_ID, GASNETC_SCI_MAX_LONG_PAYLOAD_SIZE, GASNETC_SCI_NO_CALLBACK, NULL, GASNETC_SCI_NO_FLAGS, &error);
		pthread_mutex_unlock( &gasnetc_mutex_sci_create_segment );
		if (error != SCI_ERR_OK) 
		{
			gasneti_fatalerror ("SCICreateSegment() failed \n");
		}

		pthread_mutex_lock( &gasnetc_mutex_sci_prepare_segment );
		SCIPrepareSegment(gasnetc_sci_local_dma_segment[i], gasnetc_sci_localAdapterNo, GASNETC_SCI_NO_FLAGS, &error);
		pthread_mutex_unlock( &gasnetc_mutex_sci_prepare_segment );
		if (error != SCI_ERR_OK) 
		{
			gasneti_fatalerror ("SCIPrepareSegment() failed \n");
		}

		pthread_mutex_lock( &gasnetc_mutex_sci_map_segment );
		gasnetc_sci_local_dma_addr[i] = SCIMapLocalSegment(gasnetc_sci_local_dma_segment[i], &gasnetc_sci_local_dma_map[i], 0, GASNETC_SCI_MAX_LONG_PAYLOAD_SIZE, NULL, GASNETC_SCI_NO_FLAGS, &error);
		pthread_mutex_unlock( &gasnetc_mutex_sci_map_segment );
		if (error != SCI_ERR_OK) 
		{
			gasneti_fatalerror ("SCIMapLocalSegment() failed \n");
		}

		pthread_mutex_lock( &gasnetc_mutex_sci_create_dma );
		SCICreateDMAQueue(gasnetc_sci_local_dma_sd[i], &gasnetc_sci_local_dma_queue[i], gasnetc_sci_localAdapterNo, GASNETC_SCI_MAX_DMA_QUEUE_USAGE, GASNETC_SCI_NO_FLAGS, &error);
		pthread_mutex_unlock( &gasnetc_mutex_sci_create_dma );
		if (error != SCI_ERR_OK) 
		{
			gasneti_fatalerror ("SCICreateDMAQueue() failed \n");
		}
	}
	return GASNET_OK;
}

/* Remove the previously allocated local dma queues*/
int gasnetc_remove_dma_queues ()
{
	int i;
	sci_error_t error;

	for (i = 0; i < GASNETC_SCI_NUM_DMA_QUEUE; i++)
	{
		pthread_mutex_lock( &gasnetc_mutex_sci_remove_dma );
		SCIRemoveDMAQueue(gasnetc_sci_local_dma_queue[i], GASNETC_SCI_NO_FLAGS, &error);
		pthread_mutex_unlock( &gasnetc_mutex_sci_remove_dma );
		if (error != SCI_ERR_OK) 
		{
			gasneti_fatalerror ("SCIRemoveDMAQueue() failed \n");
		}

		pthread_mutex_lock( &gasnetc_mutex_sci_unmap_segment );
		SCIUnmapSegment(gasnetc_sci_local_dma_map[i], GASNETC_SCI_NO_FLAGS, &error);
		pthread_mutex_unlock( &gasnetc_mutex_sci_unmap_segment );
		if (error != SCI_ERR_OK) 
		{
			gasneti_fatalerror ("SCIUnmapSegment() failed \n");
		}

		pthread_mutex_lock( &gasnetc_mutex_sci_remove_segment );
		SCIRemoveSegment(gasnetc_sci_local_dma_segment[i], GASNETC_SCI_NO_FLAGS, &error);
		pthread_mutex_unlock( &gasnetc_mutex_sci_remove_segment );
		if (error != SCI_ERR_OK) 
		{
			gasneti_fatalerror ("SCIRemoveSegment() failed \n");
		}

		pthread_mutex_lock( &gasnetc_mutex_sci_close );
		SCIClose(gasnetc_sci_local_dma_sd[i] , GASNETC_SCI_NO_FLAGS, &error);
		pthread_mutex_unlock( &gasnetc_mutex_sci_close );
		if (error != SCI_ERR_OK) 
		{
			gasneti_fatalerror ("SCIClose() failed \n");
		}
	}
	return GASNET_OK;
}

/* Improved version of DMA write*/
int gasnetc_DMA_write (gasnet_node_t dest, void *source_addr, size_t nbytes, void *dest_addr)
{
	int i;
	sci_error_t error;
	sci_dma_queue_state_t dma_queue_state;
	sci_remote_segment_t remote_segment = gasnetc_sci_remoteSegment_long[dest];
	uint8_t *target_addr = (uint8_t *) gasnetc_sci_local_dma_addr[gasnetc_sci_dma_count];
	uint8_t *buf_addr = (uint8_t *) source_addr;
	int remote_offset = gasnetc_rs_get_offset (dest, dest_addr);   /* Obtain the offset that is needed from the designated remote segment base on input destination*/
	int alignment_size = 8;

	int ro_align = remote_offset % alignment_size;

	if ((ro_align) != 0)
	{
		/* segments are not aligned properly, adjust by copying the data from dma queue offset*/
		remote_offset = remote_offset - ro_align; /* move offset to be before the destination address*/
		target_addr = target_addr + ro_align;  /* set so data is copy to an offset of the queue*/
		nbytes = nbytes + ro_align;     /* re-adjust the size that needs to be send over*/
	}

	memmove (target_addr, buf_addr, nbytes);

	/* make sure the size of transfer is of appropriate size, need to be multiple of 8 bytes*/
	if ((nbytes % alignment_size) != 0)
	{
		nbytes = nbytes + (alignment_size - (nbytes % alignment_size));
	}

	do
	{
		 pthread_mutex_lock( &gasnetc_mutex_sci_enqueue_dma );
		SCIEnqueueDMATransfer(gasnetc_sci_local_dma_queue[gasnetc_sci_dma_count],
		gasnetc_sci_local_dma_segment[gasnetc_sci_dma_count], remote_segment, 0,
		remote_offset, nbytes, 
		SCI_FLAG_DMA_POST | 
		SCI_FLAG_DMA_WAIT | 
		SCI_FLAG_DMA_RESET,
		&error);
		 pthread_mutex_unlock( &gasnetc_mutex_sci_enqueue_dma );
	} while (error != SCI_ERR_OK);

	gasnetc_sci_dma_count++;
	if (gasnetc_sci_dma_count == GASNETC_SCI_NUM_DMA_QUEUE)
	{
		gasnetc_sci_dma_count = 0;
	}

	return GASNET_OK;
}

/********************************************************
		Environment Setup/Remove Functions
********************************************************/

/* Initialize necessary system variables*/
void gasnetc_setup_env ()
{ 
	gasnetc_mls_init ();
	gasnetc_ht_init ();
    /*pthread_mutex_init(&gasnetc_sci_msg_loc_mutex);
	pthread_mutex_init(&gasnetc_sci_wq_lock);*/
}

/* Remove system variables*/
void gasnetc_free_env ()
{ 
	if(gasneti_attach_done == 1)
	{
		gasnetc_remove_dma_queues();
		pthread_mutex_destroy(&gasnetc_sci_msg_loc_mutex);
		pthread_mutex_destroy(&gasnetc_sci_wq_lock);
		gasneti_free(gasnetc_sci_remoteSegment_long);
		gasneti_free(gasnetc_sci_sd_long);
	}

	gasneti_free(gasnetc_sci_sd);
	gasneti_free(gasnetc_sci_gb_sd);
	gasneti_free(gasnetc_sci_sd_remote);
	gasneti_free(gasnetc_sci_localSegment);
	gasneti_free(gasnetc_sci_remoteSegment);
	gasneti_free(gasnetc_sci_remoteSegment_gb);
	gasneti_free(gasnetc_sci_localMap);
	gasneti_free(gasnetc_sci_remoteMap);
	gasneti_free(gasnetc_sci_remoteMap_gb);
	gasneti_free(gasnetc_sci_remoteNodeId);
	gasneti_free(gasnetc_sci_localSegmentId);
	gasneti_free(gasnetc_sci_remoteSegmentId);
	gasneti_free(gasnetc_sci_remoteSegmentId_gb);
	gasneti_free(gasnetc_sci_SCI_Ids);
	gasneti_free(gasnetc_sci_local_mem);
	gasneti_free(gasnetc_sci_remote_mem);
	gasneti_free(gasnetc_sci_global_ready);	
	remove(GASNETC_SCI_FILE);
}




/*   $Source: /Users/kamil/work/gasnet-cvs2/gasnet/sci-conduit/Attic/gasnet_core_internal.c,v $
 *     $Date: 2004/08/26 04:54:01 $
 * $Revision: 1.7 $
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

/*******************************************************
		Big Phys Area Patch installed?
*******************************************************/
int GASNETC_BIGPHY_ENABLE = 1; /* set to 0 to disable (otherwise, auto-probe for it) */

/********************************************************
					Global Variables
********************************************************/
sci_desc_t				*gasnetc_sci_sd;
sci_desc_t				*gasnetc_sci_sd_gb;
sci_desc_t                              *gasnetc_sci_sd_remote;
sci_desc_t				*gasnetc_sci_sd_long;
sci_desc_t				gasnetc_sci_gas_seg;
sci_local_segment_t		        *gasnetc_sci_localSegment;				/* Handlers to local memory segment */
sci_remote_segment_t	                *gasnetc_sci_remoteSegment;				/* Handlers to remote memory segment */
sci_remote_segment_t	                *gasnetc_sci_remoteSegment_gb;
sci_remote_segment_t	                *gasnetc_sci_remoteSegment_long;
sci_map_t				*gasnetc_sci_localMap;					/* Handlers to locally mapped segment */
sci_map_t				*gasnetc_sci_remoteMap;					/* Handlers to remotely mapped segment */
sci_map_t				*gasnetc_sci_remoteMap_gb;				/* Handlers to remotely mapped segment for global bytes */
unsigned int			        gasnetc_sci_localAdapterNo = 0;
unsigned int			        gasnetc_sci_offset = 0;					/* default offset within the segment */
unsigned int			        gasnetc_sci_max_local_seg =0;				/* the values of the maximum segment sizes in system */
unsigned int			        gasnetc_sci_max_global_seg =0;				/* across all systems */
unsigned int			        *gasnetc_sci_SCI_Ids;
void 					**gasnetc_sci_local_mem;				/* an array of void pointers to each local segment */
void 					**gasnetc_sci_remote_mem;				/* an array of void pointers to each remote segment */
void 					**gasnetc_sci_global_ready;
bool 			                *gasnetc_sci_msg_flag;
sci_map_t				*gasnetc_sci_local_dma_map;
sci_local_segment_t		        *gasnetc_sci_local_dma_segment;
sci_dma_queue_t			        *gasnetc_sci_local_dma_queue;
sci_desc_t				*gasnetc_sci_local_dma_sd;
void					**gasnetc_sci_local_dma_addr;
void					*gasnetc_sci_handler_table[256];			/*  array of handler information with the index = handler ID */
                                                                                                /*  and handler_table[index] = function ptr for the handler */
int					gasnetc_sci_current_index = 0;
uint16_t				gasnetc_sci_dmaqueue_count = 0;				/*  # of temporary segment created, used for dynamic linking to remote DMA segments */
bool				        *gasnetc_sci_msg_loc_status;
sci_desc_t                              gasnetc_sci_local_barrier_sd;
sci_local_segment_t                     gasnetc_sci_local_barrier_segment;
sci_map_t                               gasnetc_sci_local_barrier_map;
sci_desc_t                              *gasnetc_sci_remote_barrier_sd;
sci_remote_segment_t                    *gasnetc_sci_remote_barrier_segment;
sci_map_t                               *gasnetc_sci_remote_barrier_map;
int                                     **gasnetc_sci_barrier_addr;
int                                     *gasnetc_sci_dma_queue_status;
sci_sequence_t                          *gasnetc_sci_sequence;
int                                     gasnetc_sci_internal_barrier_flag;
int                                     gasnetc_sci_current_barrier_value = 1;
gasneti_mutex_t		                gasnetc_sci_request_mutex = GASNETI_MUTEX_INITIALIZER;

/********************************************************
                                 Sequence Generation
********************************************************/
void gasnetc_sci_create_sequence ()
{
      int i;
      sci_error_t error;
      sci_map_t current_remote_map;
      sci_sequence_status_t sequence_status;
      gasnetc_sci_sequence = (sci_sequence_t *) gasneti_malloc (sizeof(sci_sequence_t) * gasnetc_nodes);

      for (i = 0; i < gasnetc_nodes; i++)
      {
            current_remote_map = gasnetc_rs_get_rmap(i);
            /*  Create a new sequence */
            do
            {
                  SCICreateMapSequence(current_remote_map, &(gasnetc_sci_sequence[i]), GASNETC_SCI_NO_FLAGS, &error);
            } while (error != SCI_ERR_OK);
      }
}

void gasnetc_sci_remove_sequence ()
{
      int i;
      sci_error_t error;
      for (i = 0; i < gasnetc_nodes; i++)
      {
            do
            {
                  SCIRemoveSequence(gasnetc_sci_sequence[i], GASNETC_SCI_NO_FLAGS, &error);
            } while (error != SCI_ERR_OK);
      }

}

/********************************************************
				 Segment ID Calculation
********************************************************/

unsigned int gasnetc_get_local_command_id (gasnet_node_t my_id, gasnet_node_t remote_id)
{
        return (my_id << 16) | remote_id;
}

unsigned int gasnetc_get_remote_command_id (gasnet_node_t my_id, gasnet_node_t remote_id)
{
        return (remote_id << 16) | my_id;
}

unsigned int gasnetc_get_local_globalready_id (gasnet_node_t my_id, gasnet_node_t num_nodes)
{
        return (my_id << 16) | (num_nodes);
}

unsigned int gasnetc_get_remote_globalready_id (gasnet_node_t remote_id, gasnet_node_t num_nodes)
{
        return (remote_id << 16) | (num_nodes);
}

unsigned int gasnetc_get_local_barrier_id (gasnet_node_t my_id, gasnet_node_t num_nodes)
{
        return (my_id << 16) | (num_nodes + 1);
}

unsigned int gasnetc_get_remote_barrier_id (gasnet_node_t remote_id, gasnet_node_t num_nodes)
{
        return (remote_id << 16) | (num_nodes + 1);
}

unsigned int gasnetc_get_local_payload_id (gasnet_node_t my_id, gasnet_node_t num_nodes)
{
        return (my_id << 16) | (num_nodes + 2);
}

unsigned int gasnetc_get_remote_payload_id (gasnet_node_t remote_id, gasnet_node_t num_nodes)
{
        return (remote_id << 16) | (num_nodes + 2);
}

/*  Return the next available temporary segment id for the local node */
unsigned int gasnetc_get_dmaqueue_id ()
{
	if (gasnetc_sci_dmaqueue_count == 0)	/*  exhaust all temp id, recycle */
	{
		gasnetc_sci_dmaqueue_count = gasnetc_nodes + 3;		/*  reset it so the seg_id = seg_id of global ready segment */
	}
        else
        {
	        gasnetc_sci_dmaqueue_count++;
        }
	return (gasnetc_mynode << 16) | (gasnetc_sci_dmaqueue_count);
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
                case SCI_CB_DISCONNECT: /*we lost somebody, kill the show*/
                        gasnetc_sci_call_exit(1);
                        return SCI_CALLBACK_CANCEL;
                case SCI_CB_LOST:
                        gasnetc_sci_call_exit(1);/*we lost somebody, kill the show*/
                        return SCI_CALLBACK_CANCEL;
                default:
                        return SCI_CALLBACK_CONTINUE; /*it's not a terminal call, so continue*/
        }
}


/*******************************************************
                SCI BARRIER FUNCTIONS
********************************************************/

void gasnetc_sci_create_barrier_segment()
{
        sci_error_t error;
        unsigned int gasnetc_sci_local_barrier_id = gasnetc_get_local_barrier_id(gasnetc_mynode, gasnetc_nodes);
        gasnetc_sci_barrier_addr = (void *) gasneti_malloc (sizeof(void *) * gasnetc_nodes);

        GASNETC_SCISAFE(SCIOpen(&gasnetc_sci_local_barrier_sd, GASNETC_SCI_NO_FLAGS, &error));

        if (gasnetc_mynode == 0) {
                GASNETC_SCISAFE(SCICreateSegment(gasnetc_sci_local_barrier_sd, &gasnetc_sci_local_barrier_segment,
                                 gasnetc_sci_local_barrier_id, sizeof(int) * gasnetc_nodes, GASNETC_SCI_NO_CALLBACK,
                                 NULL, GASNETC_SCI_NO_FLAGS, &error));
        } else {
                GASNETC_SCISAFE(SCICreateSegment(gasnetc_sci_local_barrier_sd, &gasnetc_sci_local_barrier_segment,
                                 gasnetc_sci_local_barrier_id, sizeof(int), GASNETC_SCI_NO_CALLBACK,
                                 NULL, GASNETC_SCI_NO_FLAGS, &error));
        }

        GASNETC_SCISAFE(SCIPrepareSegment(gasnetc_sci_local_barrier_segment, gasnetc_sci_localAdapterNo,
                                          GASNETC_SCI_NO_FLAGS, &error));

        if (gasnetc_mynode == 0) {
                GASNETC_SCISAFE(gasnetc_sci_barrier_addr[gasnetc_mynode] =
                             (int *) SCIMapLocalSegment(gasnetc_sci_local_barrier_segment, &gasnetc_sci_local_barrier_map,
                                                        0, sizeof(int) * gasnetc_nodes, NULL, GASNETC_SCI_NO_FLAGS, &error));
                int j;
                for (j = 0; j < gasnetc_nodes; j++)
                {
                      (gasnetc_sci_barrier_addr[gasnetc_mynode])[j] = -2;
                }
        } else {
                GASNETC_SCISAFE(gasnetc_sci_barrier_addr[gasnetc_mynode] =
                             (int *) SCIMapLocalSegment(gasnetc_sci_local_barrier_segment, &gasnetc_sci_local_barrier_map,
                                                        0, sizeof(int) , NULL, GASNETC_SCI_NO_FLAGS, &error));
                (gasnetc_sci_barrier_addr[gasnetc_mynode])[0] = -2;
        }

        GASNETC_SCISAFE(SCISetSegmentAvailable(gasnetc_sci_local_barrier_segment, gasnetc_sci_localAdapterNo,
                                               GASNETC_SCI_NO_FLAGS, &error));

        /*  Connecting to all remote barrier segments */
        if (gasnetc_mynode == 0) {
                int i;
                gasnetc_sci_remote_barrier_segment = (sci_remote_segment_t *)gasneti_malloc(gasnetc_nodes * sizeof(sci_remote_segment_t));
                gasnetc_sci_remote_barrier_map = (sci_map_t *) gasneti_malloc(gasnetc_nodes * sizeof(sci_map_t));
                gasnetc_sci_remote_barrier_sd = (sci_desc_t *) gasneti_malloc(gasnetc_nodes * sizeof(sci_desc_t));

                for (i = 1; i < gasnetc_nodes ; i++ ) {
                        int counter = 0;
                        unsigned int gasnetc_sci_remote_barrier_id = gasnetc_get_remote_barrier_id (i, gasnetc_nodes);

                        GASNETC_SCISAFE(SCIOpen(&gasnetc_sci_remote_barrier_sd[i], GASNETC_SCI_NO_FLAGS, &error));

                        do {
                                SCIConnectSegment(gasnetc_sci_remote_barrier_sd[i], &gasnetc_sci_remote_barrier_segment[i], gasnetc_sci_SCI_Ids[i],
                                                  gasnetc_sci_remote_barrier_id, gasnetc_sci_localAdapterNo, gasnetc_sci_remote_callback, NULL, SCI_INFINITE_TIMEOUT,
                                                  SCI_FLAG_USE_CALLBACK, &error);
                                if(error != SCI_ERR_OK)
                                        sleep(1);
                                counter++;
                        } while (error != SCI_ERR_OK && (counter < GASNETC_SCI_TIMEOUT_SEC));

                        if(error != SCI_ERR_OK)
                        {
                                gasneti_fatalerror("(%d) Barrier Could not make all node connections\n", gasnetc_mynode);
                        }

                        GASNETC_SCISAFE(gasnetc_sci_barrier_addr[i] =
                                     (int *) SCIMapRemoteSegment(gasnetc_sci_remote_barrier_segment[i],
                                                      &(gasnetc_sci_remote_barrier_map[i]), 0, sizeof(int), NULL,
                                                      GASNETC_SCI_NO_FLAGS, &error));
                }
        } else {
                 int counter = 0;
                 gasnetc_sci_remote_barrier_segment = (sci_remote_segment_t *)gasneti_malloc(sizeof(sci_remote_segment_t));
                 gasnetc_sci_remote_barrier_map = (sci_map_t *) gasneti_malloc(sizeof(sci_map_t));
                 gasnetc_sci_remote_barrier_sd = (sci_desc_t *) gasneti_malloc(sizeof(sci_desc_t));

                 GASNETC_SCISAFE(SCIOpen(gasnetc_sci_remote_barrier_sd, GASNETC_SCI_NO_FLAGS, &error));

                 do {
                         SCIConnectSegment(*gasnetc_sci_remote_barrier_sd, gasnetc_sci_remote_barrier_segment, gasnetc_sci_SCI_Ids[0],
                                           gasnetc_get_remote_barrier_id (0, gasnetc_nodes), gasnetc_sci_localAdapterNo,
                                           gasnetc_sci_remote_callback, NULL, SCI_INFINITE_TIMEOUT,
                                           SCI_FLAG_USE_CALLBACK, &error);
                         if(error != SCI_ERR_OK)
                                 sleep(1);
                         counter++;
                 } while (error != SCI_ERR_OK && (counter < GASNETC_SCI_TIMEOUT_SEC));

                 if(error != SCI_ERR_OK)
                 {
                         gasneti_fatalerror("(%d) Barrier Could not make all node connections\n", gasnetc_mynode);
                 }

                 GASNETC_SCISAFE(gasnetc_sci_barrier_addr[0] =
                       (int *) SCIMapRemoteSegment(*gasnetc_sci_remote_barrier_segment, gasnetc_sci_remote_barrier_map,
                                                   0, sizeof(int) * gasnetc_nodes, NULL, GASNETC_SCI_NO_FLAGS, &error));
        }
}

void gasnetc_sci_remove_barrier_segment()
{
        int i;
        sci_error_t error;

        GASNETC_SCISAFE(SCIUnmapSegment(gasnetc_sci_local_barrier_map, GASNETC_SCI_NO_FLAGS, &error));
        GASNETC_SCISAFE(SCIRemoveSegment(gasnetc_sci_local_barrier_segment, GASNETC_SCI_NO_FLAGS, &error));
        GASNETC_SCISAFE(SCIClose(gasnetc_sci_local_barrier_sd, GASNETC_SCI_NO_FLAGS, &error));

        if (gasnetc_mynode == 0) {
                for (i = 1; i < gasnetc_nodes; i++) {
                        GASNETC_SCISAFE(SCIUnmapSegment(gasnetc_sci_remote_barrier_map[i], GASNETC_SCI_NO_FLAGS, &error));
                        GASNETC_SCISAFE(SCIClose(gasnetc_sci_remote_barrier_sd[i], GASNETC_SCI_NO_FLAGS, &error));
                }
        } else {
                 GASNETC_SCISAFE(SCIUnmapSegment(gasnetc_sci_remote_barrier_map[0], GASNETC_SCI_NO_FLAGS, &error));
                 GASNETC_SCISAFE(SCIClose(gasnetc_sci_remote_barrier_sd[0], GASNETC_SCI_NO_FLAGS, &error));
        }
}

void gasnetc_sci_barrier_notify (int barrier_value)
{
        if (gasnetc_mynode != 0)
        {
              (gasnetc_sci_barrier_addr[gasnetc_mynode])[0] = -2;
              (gasnetc_sci_barrier_addr[0])[gasnetc_mynode] = barrier_value;
        }
        else if (barrier_value == -1)
        {
              /*  node 0 cause a mismatch */
              int i;
              for (i = 1; i < gasnetc_nodes; i++)
              {
                    (gasnetc_sci_barrier_addr[i])[0] = -1;
              }
        }
}

int gasnetc_sci_barrier_try (int barrier_value)
{
        int barrier_complete = GASNET_OK;
        if (gasnetc_mynode == 0)
        {
              int counter = 1;
              int curr_value;
              while ((barrier_complete == GASNET_OK) && (counter < gasnetc_nodes))
              {
                  curr_value = (gasnetc_sci_barrier_addr[0])[counter];
                  if (curr_value == -1)
                  {
                        /*  Sent error signal to all other nodes */
                        int i;
                        for (i = 1; i < gasnetc_nodes; i++)
                        {
                            (gasnetc_sci_barrier_addr[i])[0] = -1;
                        }
                        return GASNET_ERR_BARRIER_MISMATCH;
                  }
                  else if (curr_value == -2)
                  {
                        barrier_complete = GASNET_ERR_NOT_READY;
                 }
                  else if (curr_value == barrier_value)
                  {
                        counter++;
                  }
                  else
                  {
                        gasneti_fatalerror ("(%d) Error - undefined barrier value detected, (gasnetc_sci_barrier_addr[0])[%d] = %d\n", gasnetc_mynode, counter, curr_value);
                  }
              }
              if (barrier_complete == GASNET_OK)
              {
                  int i;
                  /*  Sent complete signal to all other nodes */
                  for (i = 1; i < gasnetc_nodes; i++)
                  {
                      (gasnetc_sci_barrier_addr[0])[i] = -2;  /*  reset value for next barrier */
                      (gasnetc_sci_barrier_addr[i])[0] = barrier_value;
                  }
              }
        }
        else
        {
              int curr_value = (gasnetc_sci_barrier_addr[gasnetc_mynode])[0];
              if (curr_value == -1)
              {
                    barrier_complete = GASNET_ERR_BARRIER_MISMATCH;
              }
              else if (curr_value == -2)
              {
                    barrier_complete = GASNET_ERR_NOT_READY;
              }
              else if (curr_value == barrier_value)
              {
                   (gasnetc_sci_barrier_addr[gasnetc_mynode])[0] = -2;  /*  reset value for next barrier */
                   barrier_complete = GASNET_OK;
              }
              else
              {
                    gasneti_fatalerror ("(%d) Error: barrier has a value that is not allowed %d\n", gasnetc_mynode, (gasnetc_sci_barrier_addr[gasnetc_mynode])[0]);
              }
        }
        return barrier_complete;
}

int gasnetc_sci_barrier_wait (int barrier_value)
{
        int result;
        do
        {
              result = gasnetc_sci_barrier_try (barrier_value);

              if (result == GASNET_ERR_BARRIER_MISMATCH)
              {
                    return GASNET_ERR_BARRIER_MISMATCH;
              }
              else if (result == GASNET_ERR_NOT_READY)
              {
                    if (gasnetc_sci_internal_barrier_flag == GASNETC_SCI_FALSE)
                    {
                        gasneti_AMPoll();
                    }
              }

        } while (result == GASNET_ERR_NOT_READY);
        return GASNET_OK;
}

void gasnetc_sci_internal_Barrier()
{
      gasnetc_sci_internal_barrier_flag = GASNETC_SCI_TRUE;

      gasnetc_sci_barrier_notify (gasnetc_sci_current_barrier_value);
      gasnetc_sci_barrier_wait (gasnetc_sci_current_barrier_value);

      gasnetc_sci_current_barrier_value++;
}

/*  Parses all the SCI Ids and places them in gasnetc_sci_SCI_Ids. */
/*  The corresponding GASNet node ID is the index of the location of the SCI id in the */
/*  array. */
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
			gasneti_fatalerror("(%d) ERROR: couldn't get SCI node IDs", gasnetc_mynode);
		}
	}
	return 1;
}

/*  Returns the Max_local_Segment size in bytes based on available */
/*  free mem on the system and writes the size to a global file */
int gasnetc_get_free_mem()
{
        int size, found, mod, orig_size;
        char title[100];

        if (GASNETC_BIGPHY_ENABLE) {
            FILE* meminfo = fopen("/proc/bigphysarea", "r");
            if(meminfo == NULL) {
              #if GASNET_DEBUG_VERBOSE
                     /* note - can't call trace macros during gasnet_init because trace system not yet initialized */
                     fprintf(stderr,"(%d) Failed to open /proc/bigphysarea, assuming no patch available...", gasnetc_mynode); fflush(stderr);
              #endif
              GASNETC_BIGPHY_ENABLE = 0;
            } else {
                found = 0;
                while((found == 0) && (feof(meminfo) == 0))
                {
                        fscanf(meminfo, "%s", title);
                        if( strcmp(title, "block:") == 0 )
                        {
                                found = 1;
                        }
                }

                if(found == 0) {
                        gasneti_fatalerror("(%d) ERROR: could not find amount of free memory.", gasnetc_mynode);
                }

                fscanf(meminfo, "%d", &size);
                fclose(meminfo);
                size = size * 1024; /*convert kB to B*/
                orig_size = size;
                size =  orig_size - (10*GASNETC_SCI_ONE_MB); /*save 10MB for command segments = 4 mailboxes on 1280 nodes or 20 mailboxes on 237 nodes*/
                mod = size%GASNET_PAGESIZE; /* make it an even multiple of GASNET_PAGESIZE (should be multiple of 4 for SCI)*/
                size = size - mod;
            }
        }
        if (!GASNETC_BIGPHY_ENABLE) {
            /*for now, we can only really use 1MB segment sizes */
            size = GASNETC_SCI_ONE_MB;
        }

        gasnetc_sci_max_local_seg = size;

        return size;
}

/*  Returns the maximum global segment size. This is the minimum of the segment */
/*  sizes available over the whole cluster. Return -1 if not all node information */
/*  is in the file, throws an error otherwise. */
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

/*  This uses the number given to it to create the segments needed by the command */
/*  region. It will leave an open spot for the gasnet segment space to be */
/*  the next to last segment. It will be created in GASNet Attach. */
gasnet_node_t gasnetc_SCI_Create_Connections(int number)
{
	int index;
	gasnet_node_t gasnetc_mynode;/* unsigned short */
	unsigned int LocalID;
	sci_error_t error;
	sci_query_adapter_t		gasnetc_sci_SCIAdapter;		/* Handler to find out current node's SCI ID */

	gasnetc_nodes = number;/* set global variable */

	gasnetc_sci_SCIAdapter.subcommand = SCI_Q_ADAPTER_NODEID;
        gasnetc_sci_SCIAdapter.localAdapterNo = 0;
        gasnetc_sci_SCIAdapter.data = &LocalID;

        #if GASNET_DEBUG_VERBOSE
               /* note - can't call trace macros during gasnet_init because trace system not yet initialized */
               fprintf(stderr,"gasnet creating SCI command segments..."); fflush(stderr);
        #endif

	SCIQuery(SCI_Q_ADAPTER, &gasnetc_sci_SCIAdapter, GASNETC_SCI_NO_FLAGS, &error);
	if (error != SCI_ERR_OK) /* eventually will not exit until it has tried all 3 adapter numbers */
	{
        gasneti_fatalerror("(%d) Cannot find Adapter: %d. SCIQuery failed - Error code: 0x%x\n",0, gasnetc_mynode, error);
    }

	gasnetc_sci_localAdapterNo = 0;/* since 0 returned successful, set it for now. */
	gasnetc_sci_offset = 0; /*  for creations, never use offsets */

	for (index = 0; index < number ; index++)
	{
		if (gasnetc_sci_SCI_Ids[index] == LocalID)
		{
			gasnetc_mynode = index;
			break;
		}
	}

	/* first open all sci virtual descriptors */
	for (index=0; index < (number +2) ; index++)
	{
		GASNETC_SCISAFE(SCIOpen(&gasnetc_sci_sd[index], GASNETC_SCI_NO_FLAGS, &error));
	}

	/* now create the segments, size of GASNETC_SCI_COMMAND_MESSAGE_SIZE * GASNETC_SCI_MAX_REQUEST_MSG * 2 */
	for (index = 0;index < number ; index++ )
	{
		GASNETC_SCISAFE(SCICreateSegment(gasnetc_sci_sd[index], &gasnetc_sci_localSegment[index], gasnetc_get_local_command_id (gasnetc_mynode, index),
						 GASNETC_SCI_COMMAND_MESSAGE_SIZE * GASNETC_SCI_MAX_REQUEST_MSG * 2, NULL/*callback*/, NULL,
						 GASNETC_SCI_NO_FLAGS, &error));
	}
	/*Now create the global ready byte region, leaving the num + 1 region still uncreated*/
	/* this one uses the callback function as a way to send a notice to everybody and recieve notices of
	node failures, helps with gasnet_exit()*/
	GASNETC_SCISAFE(SCICreateSegment(gasnetc_sci_sd[index], &gasnetc_sci_localSegment[number+1],
                                         gasnetc_get_local_globalready_id (gasnetc_mynode, number),
		                         (number * (GASNETC_SCI_MAX_REQUEST_MSG *2))+1, NULL/*callback*/, NULL,
                                         GASNETC_SCI_NO_FLAGS, &error));
	/* Prepare the segments */
	for (index = 0;index < (number+2) ; index++ )
	{

		if( index == (number)) {
			continue; /* skip preparation of payload region as it has not been created yet */
		} else {
			GASNETC_SCISAFE(SCIPrepareSegment(gasnetc_sci_localSegment[index],gasnetc_sci_localAdapterNo,GASNETC_SCI_NO_FLAGS,&error));
		}
	}

	gasnetc_sci_local_mem = (void **) gasneti_malloc( (number +2) * sizeof(void*) );

	/* now map all the segments to usable space */
	for (index = 0; index < (number+2); index++ )
	{
		if( index == (number))
		{
			continue; /* skip mapping of payload region as it has not been created yet */
		}
		if( index == (number+1)) { /* global ready byte region */
			GASNETC_SCISAFE(gasnetc_sci_local_mem[index] =
                            SCIMapLocalSegment(gasnetc_sci_localSegment[index],&gasnetc_sci_localMap[index],
						gasnetc_sci_offset,(number * (GASNETC_SCI_MAX_REQUEST_MSG *2))+1,
                                                NULL,GASNETC_SCI_NO_FLAGS,&error));
			continue;
		} else {
			/* Now map the command segments so they may be used */
			GASNETC_SCISAFE(gasnetc_sci_local_mem[index] =
                            SCIMapLocalSegment(gasnetc_sci_localSegment[index],&gasnetc_sci_localMap[index],
				gasnetc_sci_offset,GASNETC_SCI_COMMAND_MESSAGE_SIZE * GASNETC_SCI_MAX_REQUEST_MSG * 2,
				NULL,GASNETC_SCI_NO_FLAGS,&error));
		}
	}

	/* make the segments available to the world */
	for (index = 0; index < (number+2); index++)
	{
		if( index == (number))
		{
			continue; /* skip exporting of payload region as it has not been created yet */
		}
		/* make them available to the outside world */
		GASNETC_SCISAFE(SCISetSegmentAvailable(gasnetc_sci_localSegment[index], gasnetc_sci_localAdapterNo, GASNETC_SCI_NO_FLAGS, &error));
	}

        #if GASNET_DEBUG_VERBOSE
               /* note - can't call trace macros during gasnet_init because trace system not yet initialized */
               fprintf(stderr,"done\n"); fflush(stderr);
        #endif

	return gasnetc_mynode;
}

/*  Connects all the command regions and then all the global ready bytes */
/*  across all nodes. */
int gasnetc_SCI_connect_cmd(int number)
{
	int index, gb_ID, counter = 0;
	int My_ID = gasnetc_mynode;
	sci_error_t error;

        #if GASNET_DEBUG_VERBOSE
               /* note - can't call trace macros during gasnet_init because trace system not yet initialized */
               fprintf(stderr,"gasnet connecting SCI command segments..."); fflush(stderr);
        #endif

	gasnetc_sci_sd_remote = (sci_desc_t*) gasneti_malloc(number* sizeof(sci_desc_t));

	for (index = 0; index < number ; index++) {
		GASNETC_SCISAFE(SCIOpen(&gasnetc_sci_sd_remote[index], GASNETC_SCI_NO_FLAGS, &error));
	}

	/*connect all the command regions,even ourselves, global ready bytes come later*/
	for (index = 0; index < number ; index++)
	{
		counter = 0;
		do {
			SCIConnectSegment(gasnetc_sci_sd_remote[index],&gasnetc_sci_remoteSegment[index],gasnetc_sci_SCI_Ids[index],
				gasnetc_get_remote_command_id (My_ID, index),gasnetc_sci_localAdapterNo,
				gasnetc_sci_remote_callback,NULL,SCI_INFINITE_TIMEOUT,SCI_FLAG_USE_CALLBACK,&error);
			if(error != SCI_ERR_OK)
                        {
                            sleep(1);/* connections may not be ready yet, wait a second then try again */
			    counter++;
                        }
		} while (error != SCI_ERR_OK && (counter < GASNETC_SCI_TIMEOUT_SEC));

		if(error != SCI_ERR_OK)/* if this is true, counter is 90 so don't test */
		{
			gasneti_fatalerror("(%d) Could not make all sci node connections\n", gasnetc_mynode);/* leave gasnet, something is wrong */
		}
	}

	gasnetc_sci_remote_mem = (void**) gasneti_malloc( (number +2) * sizeof(void*) );

	for (index = 0; index < number ; index ++ ) {
		/* Map the segments to user space */
		GASNETC_SCISAFE(gasnetc_sci_remote_mem[index] =
                    (void *)SCIMapRemoteSegment(gasnetc_sci_remoteSegment[index],&gasnetc_sci_remoteMap[index],
				gasnetc_sci_offset,GASNETC_SCI_COMMAND_MESSAGE_SIZE * GASNETC_SCI_MAX_REQUEST_MSG * 2,
				NULL,GASNETC_SCI_NO_FLAGS,&error));
	}

	/*Now get all the global ready byte regions*/
	/*Need to open descriptors for each of the byte segments*/
	gasnetc_sci_sd_gb = (sci_desc_t*) gasneti_malloc(sizeof(sci_desc_t) * (number));
	for (index = 0; index < number ; index++)
	{
		GASNETC_SCISAFE(SCIOpen(&gasnetc_sci_sd_gb[index], GASNETC_SCI_NO_FLAGS, &error));
	}
	gasnetc_sci_remoteSegment_gb = (sci_remote_segment_t*) gasneti_malloc(sizeof(sci_remote_segment_t)*number);

	for (index = 0; index < number ; index++)
	{
		void * arg;
		counter = 0;
		do {
			SCIConnectSegment(gasnetc_sci_sd_gb[index], &gasnetc_sci_remoteSegment_gb[index],
				gasnetc_sci_SCI_Ids[index],	gasnetc_get_remote_globalready_id(index, number),
				gasnetc_sci_localAdapterNo, gasnetc_sci_remote_callback, arg,
				SCI_INFINITE_TIMEOUT, SCI_FLAG_USE_CALLBACK,&error);
			if (error != SCI_ERR_OK)
				sleep(1);/* may not be ready yet, wait 1 second then try again */
			counter++;
		} while (error != SCI_ERR_OK &&(counter < GASNETC_SCI_TIMEOUT_SEC));
		if(error != SCI_ERR_OK)
		{
			gasneti_fatalerror("(%d) Could not make proper node connections to global ready byte region.\n", gasnetc_mynode);
		}
	}

	gasnetc_sci_global_ready = (void*) gasneti_malloc( number * sizeof(void*) );
	gasnetc_sci_remoteMap_gb = (sci_map_t*) gasneti_malloc( number * sizeof(sci_map_t) );

	/* Map global ready bytes to usable space */
	for (index = 0; index < number ; index++) {
		/* Map the segment to user space */
		GASNETC_SCISAFE(gasnetc_sci_global_ready[index] =
                    (void *)SCIMapRemoteSegment(gasnetc_sci_remoteSegment_gb[index],
						&gasnetc_sci_remoteMap_gb[index],gasnetc_sci_offset,
						(number * (GASNETC_SCI_MAX_REQUEST_MSG *2))+1,NULL,GASNETC_SCI_NO_FLAGS,
						&error));
	}

	/* map local global ready region to another place for use in other parts of conduit */
	gasnetc_sci_msg_flag = (bool *) gasnetc_sci_global_ready[gasnetc_mynode];

	/* zero all the flags */
	int i, j;
	for (i = 0; i < gasnetc_nodes; i++)
	{
		for (j = 0; j < GASNETC_SCI_MAX_REQUEST_MSG * 2; j++ )
		{
			gasnetc_msg_flag_release (i, j);
		}
	}
	gasnetc_msg_exist_flag_release ();

        /*EVERYTHING, gb too, is connected and mapped*/
         #if GASNET_DEBUG_VERBOSE
                /* note - can't call trace macros during gasnet_init because trace system not yet initialized */
                fprintf(stderr,"done\n"); fflush(stderr);
        #endif

	return 1;
}

/*  This function finds the total number of nodes that will run GASNet as well as */
/*  assign GASNet Node IDS to each SCI ID. Additionally, the total amount of free */
/*  memory on the system is determined and a percentage is used as the MAX_local_Seg */
/*  size. */
unsigned int gasnetc_SCIInit(gasnet_node_t * my_node)
{
	FILE* node_info;
	int index, number, mem_available;
	char num_of_nodes[10], *node_ids;

	node_info = fopen(GASNETC_SCI_FILE, "r+"); /* open file that holds all the info */
	if(node_info == NULL)
	{
		gasneti_fatalerror("(%d) Failed to acquire access to SCI initialization information", gasnetc_mynode);
	}

	fgets(num_of_nodes,6,node_info); /* upto 5 digits worth of nodes, SCI only supports upto 65,000 nodes */

	number = atol(num_of_nodes);
	if(number == 0 )
	{
		gasneti_fatalerror("(%d) Problem reading number of nodes, or you chose to run on zero nodes.\n", gasnetc_mynode);
	}

	/* Now we need to parse through the file and extract the ids, also amount of available mem*/
	gasnetc_sci_SCI_Ids = (unsigned int *) gasneti_malloc( sizeof(unsigned int) * number );
	index = gasnetc_parseSCIIds(node_info, number);

	gasnetc_sci_max_local_seg = gasnetc_get_free_mem();

	/* now allocate enough space to create $number+2 number of segments */
	gasnetc_sci_sd = (void *) gasneti_malloc( sizeof(sci_desc_t) * (number + 2) );

	/*we know how many there should be, so allocate the space and hop to it*/
	gasnetc_sci_localSegment = (sci_local_segment_t *) gasneti_malloc( sizeof(sci_local_segment_t) * (number +2));
        gasnetc_sci_remoteSegment = (sci_remote_segment_t *) gasneti_malloc( (sizeof(sci_remote_segment_t)) * (number + 2));
	gasnetc_sci_localMap = (sci_map_t *) gasneti_malloc(sizeof(sci_map_t) * (number +2) );
	gasnetc_sci_remoteMap = (sci_map_t *) gasneti_malloc(sizeof(sci_map_t) * (number +2) );

	/*create,prepare, map, and export all command regions and global ready byte
	  returnd the gasnet ID to current system*/

	*my_node = gasnetc_SCI_Create_Connections(number);

	/*Connect all the command regions and global ready bytes to each other*/

	gasnetc_SCI_connect_cmd(number);

        gasnetc_setup_env ();

	/*Get the minimum max size across all segments*/
	gasnetc_sci_max_global_seg = gasnetc_getSCIglobal_seg(number);

	return number;
}

/*  Create the payload (GASNET segment) segment and set it available. */
void* gasnetc_create_gasnetc_sci_seg(uintptr_t *segsize, int index)
{
	int number = gasnetc_nodes;
	unsigned int Size;
	index = gasnetc_nodes;
	sci_error_t error;
	void* arg;

	if (!GASNETC_BIGPHY_ENABLE)
	{
		if(*segsize >= GASNETC_SCI_ONE_MB)	/* can't currently use more than 1MB */
		*segsize = GASNETC_SCI_ONE_MB;
	}


	Size = *segsize;

	GASNETC_SCISAFE(SCIOpen(&gasnetc_sci_gas_seg, GASNETC_SCI_NO_FLAGS, &error));

	/* index -- the number of nodes, the next to last index in the array, where payload region is located */
	GASNETC_SCISAFE(SCICreateSegment(gasnetc_sci_gas_seg, &gasnetc_sci_localSegment[index],
		gasnetc_get_local_payload_id (gasnetc_mynode, gasnetc_nodes), Size,	NULL, NULL,	GASNETC_SCI_NO_FLAGS,
		&error));

	GASNETC_SCISAFE(SCIPrepareSegment(gasnetc_sci_localSegment[index],gasnetc_sci_localAdapterNo,GASNETC_SCI_NO_FLAGS,&error));

	GASNETC_SCISAFE(gasnetc_sci_local_mem[index] =
            SCIMapLocalSegment(gasnetc_sci_localSegment[index],&gasnetc_sci_localMap[index],
				gasnetc_sci_offset,Size, NULL,GASNETC_SCI_NO_FLAGS,&error));

	/* make them available to the outside world */
	GASNETC_SCISAFE(SCISetSegmentAvailable(gasnetc_sci_localSegment[index], gasnetc_sci_localAdapterNo, GASNETC_SCI_NO_FLAGS, &error));

	/*Now we need to connect the DMA segments from everybody else*/

	gasnetc_sci_remoteSegment_long = (sci_remote_segment_t *) gasneti_malloc(sizeof(sci_remote_segment_t) * number);
	gasnetc_sci_sd_long = (sci_desc_t *) gasneti_malloc(sizeof(sci_desc_t) * number);

	/* open a descriptor for each */
	for (index = 0; index < number ; index++) {
		GASNETC_SCISAFE(SCIOpen(&gasnetc_sci_sd_long[index], GASNETC_SCI_NO_FLAGS, &error));
	}

	/*connect all the DMA regions, don't map yet*/
	for (index = 0; index < number ; index++)
	{
		int counter = 0;
		do {
			SCIConnectSegment(gasnetc_sci_sd_long[index],&gasnetc_sci_remoteSegment_long[index], gasnetc_sci_SCI_Ids[index],
				gasnetc_get_remote_payload_id (index, number), gasnetc_sci_localAdapterNo,gasnetc_sci_remote_callback,NULL,SCI_INFINITE_TIMEOUT,SCI_FLAG_USE_CALLBACK,&error);
			if(error != SCI_ERR_OK)
				sleep(1);/* maybe not ready yet, wait 1 second then try again */
			counter++;
		} while (error != SCI_ERR_OK && (counter< 20)); /* wait for 2 tries after timing out */
		if(error != SCI_ERR_OK)
			gasneti_fatalerror("(%d) Could not connect all GASNET segments.\n", gasnetc_mynode);
	}

	return gasnetc_sci_local_mem[index]; /* return the address of the pointer to our GASNET */
										 /* segment in our local memory space */
}

/*  This waits for all nodes to write their segment sizes and addresses to the */
/*  mailboxes. Then gets all the segment info and places int the segment array */
/*  info. */
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
	TEMP = (gasnet_seginfo_t *) gasneti_malloc(sizeof(gasnet_seginfo_t));/* allocate enough space for one */

	/* very similar to getting the segment size info, place my segsize and segbase */
	/*  into mailbox gasnetc_mynode on everybody else's nodes, then check my mailbox */
	/*  and gather everybody's segment information and place into the table */

	/* prepare my information for transfer */
	TEMP->addr = (void*)segbase;
	TEMP->size = segsize;

	/* place my information in everybody's mailbox */
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

        gasnetc_sci_internal_Barrier();/* wait for everybody */

	/* get info, place into the table */
	for(index = 0; index < gasnetc_nodes; index++)
	{
		TEMP = (gasnet_seginfo_t*) gasnetc_sci_local_mem[index];
		SEG_INFO[index].addr = TEMP->addr;
		SEG_INFO[index].size = TEMP->size;
	}
}

/********************************************************
				Message Location Status
		-- Use by Local node to keep track of --
		--	command msgs to all other nodes   --
********************************************************/

/*  Allocate space and initialize the MLS */
void gasnetc_mls_init ()
{
	int i;
	gasnetc_sci_msg_loc_status = (uint8_t *) gasneti_malloc ((sizeof (uint8_t)) * (gasnetc_nodes * GASNETC_SCI_MAX_REQUEST_MSG * 2));
	for (i = 0; i < (gasnetc_nodes * GASNETC_SCI_MAX_REQUEST_MSG * 2); i++)
	{
		gasnetc_sci_msg_loc_status[i] = GASNETC_SCI_FALSE;
	}
}

void gasnetc_mls_release (gasnet_node_t RemoteID, uint8_t msg_number)
{
        gasnetc_sci_msg_loc_status[RemoteID * GASNETC_SCI_MAX_REQUEST_MSG * 2 + msg_number] = GASNETC_SCI_FALSE;
}

/*  Obtain a free request message location */
int gasnetc_mls_get_loc (gasnet_node_t dest_node_id)
{
        int error_count = 0;
        int found = 0;
        int counter = 0;
        do
        {
              counter = 0;
              while ((found == 0) && (counter < GASNETC_SCI_MAX_REQUEST_MSG))
              {
                    if (gasnetc_sci_msg_loc_status[dest_node_id * GASNETC_SCI_MAX_REQUEST_MSG * 2 + counter]  == GASNETC_SCI_TRUE)
                    {
                        counter++;
                    }
                    else
                    {
                        found = 1;
                        gasnetc_sci_msg_loc_status[dest_node_id * GASNETC_SCI_MAX_REQUEST_MSG * 2 + counter]  = GASNETC_SCI_TRUE;
                    }
              }
              if (found == 0)
              {
                  gasneti_AMPoll();
              }
        } while (found == 0);

        return counter;
}

/********************************************************
					  Handler Table
********************************************************/

/*  Allocate and initialize the handler table */
void gasnetc_ht_init()
{
	int i;
	for (i = 0; i < GASNETC_SCI_MAX_HANDLER_NUMBER; i++)
	{
		gasnetc_sci_handler_table[i] = NULL;
	}
}

/*  Add a new handler to the table using a given index */
void gasnetc_ht_add_handler (void * func_ptr, int index)
{
        gasnetc_sci_handler_table[index] = func_ptr;
        if (index > gasnetc_sci_current_index)
        {
                gasnetc_sci_current_index = index;
        }
}

/********************************************************
					 Handler Running
********************************************************/
/*  Runs the short message handler */
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
                  default: abort();
            }
      }
}

/*  Runs medium/long message handler */
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
                  default: abort();
            }
      }
}

void gasnetc_sci_handle_msg (gasnet_node_t sender_id, uint8_t msg_number, uint8_t msg_AM_type)
{
      gasnetc_sci_token_t reply_token;
      reply_token.source_id = sender_id;
      reply_token.msg_number = msg_number;
      gasnet_token_t handler_token = &reply_token;

      switch (msg_AM_type) {
        case GASNETC_SCI_SHORT: {
          gasnetc_ShortMedium_header_t *Short_msg = (gasnetc_ShortMedium_header_t *)(((uint8_t *) gasnetc_sci_local_mem[sender_id]) + GASNETC_SCI_COMMAND_MESSAGE_SIZE * msg_number);
          int msg_numargs = gasnetc_get_msg_num_arg (Short_msg->header);
          void *func_ptr = gasnetc_ht_get_handler (gasnetc_get_msg_handler (Short_msg->header));

          gasnetc_run_handler_short (handler_token, func_ptr, msg_numargs, Short_msg->args);
          break;
        }
        case GASNETC_SCI_MEDIUM: {
          gasnetc_ShortMedium_header_t *Medium_msg = (gasnetc_ShortMedium_header_t *) (((uint8_t *) gasnetc_sci_local_mem[sender_id]) + GASNETC_SCI_COMMAND_MESSAGE_SIZE * msg_number);
          int msg_numargs = gasnetc_get_msg_num_arg (Medium_msg->header);
          void *func_ptr = gasnetc_ht_get_handler (gasnetc_get_msg_handler (Medium_msg->header));
          /*  payload starts right after the largest sized header */
          // void *msg_payload = ((uint8_t *) Medium_msg) + sizeof (gasnetc_Long_header_t);
          void *msg_payload = ((uint8_t *) Medium_msg) + Medium_msg->header_size;

          gasnetc_run_handler_mediumlong (handler_token, func_ptr, msg_numargs, Medium_msg->args, msg_payload, Medium_msg->payload_size);
          break;
        }
        case GASNETC_SCI_LONG: {
          gasnetc_Long_header_t *Long_msg = (gasnetc_Long_header_t *) (((uint8_t *) gasnetc_sci_local_mem[sender_id]) + GASNETC_SCI_COMMAND_MESSAGE_SIZE * msg_number);
          int msg_numargs = gasnetc_get_msg_num_arg (Long_msg->header);
          void *func_ptr = gasnetc_ht_get_handler (gasnetc_get_msg_handler (Long_msg->header));

          /*  handle the unaligned data */
          if (Long_msg->payload_size <= gasnet_AMMaxMedium())
          {
                // uint8_t *payload_source  = ((uint8_t *) Long_msg) + sizeof (gasnetc_Long_header_t);
                uint8_t *payload_source  = ((uint8_t *) Long_msg) + Long_msg->header_size;
                memcpy (Long_msg->payload, payload_source, Long_msg->payload_size);
          }
          else
          {
                /* copy unaligned data to correct location */
                uint8_t start_unaligned_bytes, right_unaligned_bytes;
                gasnetc_send_unaligned_offset_calculation  (gasnetc_seginfo[gasnetc_mynode].addr, Long_msg->payload, Long_msg->payload_size, &start_unaligned_bytes, &right_unaligned_bytes);
                // uint8_t *payload_source  = ((uint8_t *) Long_msg) + sizeof (gasnetc_Long_header_t);
                uint8_t *payload_source  = ((uint8_t *) Long_msg) + Long_msg->header_size;

                /* handle start addr offset */
                if (start_unaligned_bytes > 0)
                {
                      memcpy (Long_msg->payload, payload_source, start_unaligned_bytes);
                }
                /* handle transfer size offset */
                if (right_unaligned_bytes > 0)
                {
                      uint8_t *payload_dest = (uint8_t *) Long_msg->payload + (Long_msg->payload_size - right_unaligned_bytes);
                      memcpy (payload_dest, payload_source + start_unaligned_bytes, right_unaligned_bytes);
                }
          }

          gasnetc_run_handler_mediumlong (handler_token, func_ptr, msg_numargs, Long_msg->args, Long_msg->payload, Long_msg->payload_size);
          break;
      }
      default:
          gasneti_fatalerror ("(%d) ERROR - Polling Control msg\n", gasnetc_mynode);
    }

      if (msg_number >= GASNETC_SCI_MAX_REQUEST_MSG)
      {
          /*  Free up the request slot of current node since the msg polled was a reply */
          gasnetc_mls_release (sender_id, msg_number - GASNETC_SCI_MAX_REQUEST_MSG);
      }
      else
      {
          /*  Check if reply was generated, if not, generates a control reply */
          if (gasnetc_mls_status (sender_id, msg_number + GASNETC_SCI_MAX_REQUEST_MSG) == GASNETC_SCI_TRUE)
          {
              /*  corresponding reply sent back */
              gasnetc_mls_release (sender_id, msg_number + GASNETC_SCI_MAX_REQUEST_MSG);
          }
          else
          {
              if (sender_id == gasnetc_mynode)
              {
                  /*  no reply, need to clear slot on own node after handling the msg */
                  gasnetc_mls_release (gasnetc_mynode, msg_number);
              }
              else
              {
                  /*  no reply, need to send generic reply back to sender */
                  int Status = gasnetc_SM_transfer (sender_id, GASNETC_SCI_MAX_REQUEST_MSG + msg_number, GASNETC_SCI_REPLY, GASNETC_SCI_CONTROL, 0, 0, NULL, NULL, 0, NULL);
              }
          }
      }
}

/********************************************************
				  MSG Flag Management
********************************************************/
/*  Scans the MRFs to enqueue new messages */
void gasnetc_MRF_scan ()
{
        int i, j;
        int msg_flag_status;

        for (i = 0; i < gasnetc_nodes; i++)
        {
                for (j = 0; j < (GASNETC_SCI_MAX_REQUEST_MSG * 2); j++)
                {
                      msg_flag_status = gasnetc_msg_flag_status (i, j);
                      if (msg_flag_status != GASNETC_SCI_FALSE)
                      {
                            if (msg_flag_status == GASNETC_SCI_CONTROL_FLAG)
                            {
                                  if (j >= GASNETC_SCI_MAX_REQUEST_MSG)
                                  {
                                        /* reply control */
                                        gasnetc_msg_flag_release (i, j);
                                        gasnetc_mls_release (i, j - GASNETC_SCI_MAX_REQUEST_MSG);
                                  }
                                  else
                                  {
                                        gasneti_fatalerror ("(%d) Error - receiving a control request\n", gasnetc_mynode);
                                  }
                            }
                            else
                            {
                                  gasnetc_msg_flag_release (i, j);
                                  if ((msg_flag_status >= GASNETC_SCI_SHORT_FLAG) && (msg_flag_status <= GASNETC_SCI_LONG_FLAG))
                                  {
                                        gasnetc_sci_handle_msg (i, j, msg_flag_status - 10);
                                  }
                                  else
                                  {
                                        gasneti_fatalerror ("(%d) Got invalid msg type = %d\n", gasnetc_mynode, msg_flag_status);
                                  }
                            }
                      }
                }
        }
}

/********************************************************
			Short/Medium AM Transfer Functions
********************************************************/
/*  Using preallocated sequence */
int gasnetc_SM_transfer (gasnet_node_t dest, uint8_t msg_number, uint8_t msg_type, uint8_t AM_type, gasnet_handler_t handler,
                                                int numargs, gasnet_handlerarg_t args[], void *payload_source_addr, size_t segment_size,
                                                void *payload_dest_addr)
{
        bool *remote_msg_flag_addr = gasnetc_gr_get_addr (dest);

        gasneti_assert(msg_number < GASNETC_SCI_MAX_REQUEST_MSG * 2);

        if (AM_type != GASNETC_SCI_CONTROL)
        {
              int j;
              void *command;
              sci_error_t error;
              sci_sequence_status_t sequence_status;
              unsigned int offset = GASNETC_SCI_COMMAND_MESSAGE_SIZE * msg_number;
              int header_size = gasnetc_get_header_size (AM_type, numargs);
              sci_map_t current_remote_map = gasnetc_rs_get_rmap(dest);
              uint8_t * remote_header_addr = (uint8_t *)(gasnetc_sci_remote_mem [dest]) + offset;
              uint8_t * remote_payload_addr = remote_header_addr + header_size;
              uint8_t * source_header_ptr;
              uint8_t * source_payload_ptr = (uint8_t *) payload_source_addr;
              uint8_t start_unaligned_bytes, right_unaligned_bytes;
              size_t DMA_nbytes;

              if ((AM_type == GASNETC_SCI_SHORT) || (AM_type == GASNETC_SCI_MEDIUM))
              {
                      gasnetc_ShortMedium_header_t new_SM_msg;
                      command = &new_SM_msg;
                      gasnetc_construct_ShortMedium_command (&new_SM_msg, handler, msg_type, AM_type, segment_size, numargs, args, header_size);
                      source_header_ptr = (uint8_t *) command;
             }
              else if (AM_type == GASNETC_SCI_LONG)
              {
                      gasnetc_Long_header_t new_L_msg;
                      command = &new_L_msg;
                      gasnetc_construct_Long_command (&new_L_msg, handler, msg_type, payload_dest_addr, segment_size, numargs, args, header_size);
                      source_header_ptr = (uint8_t *) command;
                      if (segment_size > gasnet_AMMaxMedium())
                      {
                            gasnetc_send_unaligned_offset_calculation (gasnetc_seginfo[dest].addr, payload_dest_addr, segment_size, &start_unaligned_bytes, &right_unaligned_bytes);
                            DMA_nbytes = segment_size - start_unaligned_bytes - right_unaligned_bytes;

                            /*  aligned long payload transfer */
                            if (DMA_nbytes > 0)
                            {
                                  gasnetc_DMA_write (dest, source_payload_ptr + start_unaligned_bytes, DMA_nbytes, (uint8_t *) payload_dest_addr + start_unaligned_bytes);
                            }
                      }
              }

              do
              {
                    while (SCIStartSequence(gasnetc_sci_sequence[dest], GASNETC_SCI_NO_FLAGS, &error) != SCI_SEQ_OK)
                    {}

                    switch (AM_type)
                    {
                           case GASNETC_SCI_SHORT:
                                for (j = 0; j < header_size; j++)
                                {
                                      remote_header_addr[j] = source_header_ptr[j];
                                }
                                break;
                           case GASNETC_SCI_MEDIUM:
                                for (j = 0; j < header_size; j++)
                                {
                                      remote_header_addr[j] = source_header_ptr[j];
                                }
                                for (j = 0; j < segment_size; j++)
                                {
                                      remote_payload_addr[j] = source_payload_ptr[j];
                                }
                                break;
                           case GASNETC_SCI_LONG:
                                for (j = 0; j < header_size; j++)
                                {
                                      remote_header_addr[j] = source_header_ptr[j];
                                }
                                if (segment_size <= gasnet_AMMaxMedium())
                                {
                                      for (j = 0; j < segment_size; j++)
                                       {
                                           remote_payload_addr[j] = source_payload_ptr[j];
                                       }
                                }
                                else
                                {
                                      /* handle start addr offset */
                                      for (j = 0; j < start_unaligned_bytes; j++)
                                      {
                                          remote_payload_addr[j] = source_payload_ptr[j];
                                      }

                                      /* handle transfer size offset */
                                      for (j = 0; j < right_unaligned_bytes; j++)
                                      {
                                            remote_payload_addr[start_unaligned_bytes + j] = source_payload_ptr[segment_size - right_unaligned_bytes + j];
                                      }
                                }
                                break;
                    }

              } while (SCICheckSequence(gasnetc_sci_sequence[dest], GASNETC_SCI_NO_FLAGS, &error) != SCI_SEQ_OK);
        }

        switch (AM_type)
         {
               case GASNETC_SCI_CONTROL:
                     remote_msg_flag_addr[gasnetc_mynode * GASNETC_SCI_MAX_REQUEST_MSG * 2 + msg_number] = GASNETC_SCI_CONTROL_FLAG;
                    break;
               case GASNETC_SCI_SHORT:
                     remote_msg_flag_addr[gasnetc_mynode * GASNETC_SCI_MAX_REQUEST_MSG * 2 + msg_number] = GASNETC_SCI_SHORT_FLAG;
                     break;
               case GASNETC_SCI_MEDIUM:
                     remote_msg_flag_addr[gasnetc_mynode * GASNETC_SCI_MAX_REQUEST_MSG * 2 + msg_number] = GASNETC_SCI_MEDIUM_FLAG;
                     break;
               case GASNETC_SCI_LONG:
                     remote_msg_flag_addr[gasnetc_mynode * GASNETC_SCI_MAX_REQUEST_MSG * 2 + msg_number] = GASNETC_SCI_LONG_FLAG;
                     break;
               default:                  gasneti_fatalerror ("(%d) invalid AM type, type = %d\n", gasnetc_mynode, AM_type);
         }

        remote_msg_flag_addr[gasnetc_nodes * GASNETC_SCI_MAX_REQUEST_MSG * 2] = GASNETC_SCI_TRUE;  /*  Write global ready bit */

        /* force memory mapped I/O to happen now */
        gasneti_local_wmb();

        return GASNET_OK;
}

/********************************************************
                DMA Queue Management
********************************************************/
int gasnetc_sci_get_dma_queue ()
{
      int counter = 0;
      sci_error_t error;
      while (counter < GASNETC_SCI_NUM_DMA_QUEUE)
      {
            if (gasnetc_sci_dma_queue_status[counter] == 0)
            {
                gasnetc_sci_dma_queue_status[counter] = 1;

                /* Reset the DMA queue */
                SCIResetDMAQueue(gasnetc_sci_local_dma_queue[counter], GASNETC_SCI_NO_FLAGS, &error);
                return counter;
            }
            else
            {
                counter++;
            }
      }
      return -1;
}


/********************************************************
		Long AM Initialization/Transfer Functions
********************************************************/

/*  Allocates space for local dma queues */
int gasnetc_create_dma_queues ()
{
	int i;
	sci_error_t error;
	unsigned int temp_segment_ID;
	gasnetc_sci_local_dma_map = (sci_map_t *) gasneti_malloc ((sizeof(sci_map_t)) * GASNETC_SCI_NUM_DMA_QUEUE);
	gasnetc_sci_local_dma_segment = (sci_local_segment_t *) gasneti_malloc ((sizeof(sci_local_segment_t)) * GASNETC_SCI_NUM_DMA_QUEUE);
	gasnetc_sci_local_dma_queue = (sci_dma_queue_t *) gasneti_malloc ((sizeof(sci_dma_queue_t)) * GASNETC_SCI_NUM_DMA_QUEUE);
	gasnetc_sci_local_dma_sd = (sci_desc_t *) gasneti_malloc ((sizeof(sci_desc_t)) * GASNETC_SCI_NUM_DMA_QUEUE);
	gasnetc_sci_local_dma_addr = (void *) gasneti_malloc ((sizeof(void *)) * GASNETC_SCI_NUM_DMA_QUEUE);
        int dma_queue_size = gasnet_AMMaxLongRequest();

	for (i = 0; i < GASNETC_SCI_NUM_DMA_QUEUE; i++)
	{
		temp_segment_ID = gasnetc_get_dmaqueue_id ();

		GASNETC_SCISAFE(SCIOpen(&(gasnetc_sci_local_dma_sd[i]), GASNETC_SCI_NO_FLAGS, &error));

		GASNETC_SCISAFE(SCICreateSegment(gasnetc_sci_local_dma_sd[i], &(gasnetc_sci_local_dma_segment[i]),
                                                 temp_segment_ID, dma_queue_size, GASNETC_SCI_NO_CALLBACK, NULL,
                                                 GASNETC_SCI_NO_FLAGS, &error));

		GASNETC_SCISAFE(SCIPrepareSegment(gasnetc_sci_local_dma_segment[i], gasnetc_sci_localAdapterNo,
                                                  GASNETC_SCI_NO_FLAGS, &error));

		GASNETC_SCISAFE(gasnetc_sci_local_dma_addr[i] =
                    SCIMapLocalSegment(gasnetc_sci_local_dma_segment[i], &gasnetc_sci_local_dma_map[i],
                      0, dma_queue_size, NULL, GASNETC_SCI_NO_FLAGS, &error));

		GASNETC_SCISAFE(SCICreateDMAQueue(gasnetc_sci_local_dma_sd[i], &gasnetc_sci_local_dma_queue[i],
                                                  gasnetc_sci_localAdapterNo, GASNETC_SCI_MAX_DMA_QUEUE_USAGE,
                                                  GASNETC_SCI_NO_FLAGS, &error));
	}

        gasnetc_sci_dma_queue_status = (int *) gasneti_malloc(sizeof(int) * GASNETC_SCI_NUM_DMA_QUEUE);
        for (i = 0; i < GASNETC_SCI_NUM_DMA_QUEUE; i++)
        {
              gasnetc_sci_dma_queue_status[i] = 0;
        }

	return GASNET_OK;
}

/*  Remove the previously allocated local dma queues */
int gasnetc_remove_dma_queues ()
{
	int i;
	sci_error_t error;

	for (i = 0; i < GASNETC_SCI_NUM_DMA_QUEUE; i++) {
		GASNETC_SCISAFE(SCIRemoveDMAQueue(gasnetc_sci_local_dma_queue[i], GASNETC_SCI_NO_FLAGS, &error));

		GASNETC_SCISAFE(SCIUnmapSegment(gasnetc_sci_local_dma_map[i], GASNETC_SCI_NO_FLAGS, &error));

		GASNETC_SCISAFE(SCIRemoveSegment(gasnetc_sci_local_dma_segment[i], GASNETC_SCI_NO_FLAGS, &error));

		GASNETC_SCISAFE(SCIClose(gasnetc_sci_local_dma_sd[i] , GASNETC_SCI_NO_FLAGS, &error));
	}

        gasneti_free(gasnetc_sci_local_dma_map);
        gasneti_free(gasnetc_sci_local_dma_segment);
        gasneti_free(gasnetc_sci_local_dma_queue);
        gasneti_free(gasnetc_sci_local_dma_sd);
        gasneti_free(gasnetc_sci_local_dma_addr);

	return GASNET_OK;
}

/*  New version of DMA write for aligned data only */
int gasnetc_DMA_write (gasnet_node_t dest, void *source_addr, size_t DMA_nbytes, void *dest_addr)
{
        int i = 0;
        int error_count = 0;
        sci_error_t error;
        sci_dma_queue_state_t dma_queue_state;
        sci_remote_segment_t remote_segment = gasnetc_sci_remoteSegment_long[dest];
        int remote_offset = gasnetc_rs_get_offset (dest, dest_addr);   /*  Obtain the offset that is needed from the designated remote segment base on input destination */
        int dma_queue_offset = 0;
        int dma_queue;
        void *dma_queue_address;

        do
        {
            dma_queue = gasnetc_sci_get_dma_queue ();
            i++;
            if (i > 100000000)
            {
                  gasneti_fatalerror ("(%d) DMA queue not ready\n", gasnetc_mynode);
            }
        } while (dma_queue == -1);

        /*  move source data to dma_queue, can be replace by sciregister segment */
        memcpy (gasnetc_sci_local_dma_addr[dma_queue], source_addr, DMA_nbytes);

        do
        {
              SCIEnqueueDMATransfer(gasnetc_sci_local_dma_queue[dma_queue],
              gasnetc_sci_local_dma_segment[dma_queue], remote_segment, dma_queue_offset,
              remote_offset, DMA_nbytes,
              SCI_FLAG_DMA_POST |
              SCI_FLAG_DMA_WAIT |
              SCI_FLAG_DMA_RESET,
              &error);

              if (error != SCI_ERR_OK)
              {
                  error_count++;
                  if (error_count > 100000000)
                  {
                      gasneti_fatalerror ("(%d) EnqueueDMA Error = 0x%x\n", gasnetc_mynode, error);
                  }
              }
        } while (error != SCI_ERR_OK);

        /*  transfer is finish at this point */
        gasnetc_sci_dma_queue_status[dma_queue] = 0;
        return GASNET_OK;
}

/********************************************************
		Environment Setup/Remove Functions
********************************************************/

/*  Initialize necessary system variables */
void gasnetc_setup_env ()
{
	gasnetc_mls_init ();
	gasnetc_ht_init ();
        gasnetc_sci_create_barrier_segment();
        gasnetc_sci_create_sequence();
}

/*  Remove system variables */
void gasnetc_free_env ()
{
	if(gasneti_attach_done == 1)
	{
		/*these can only be safely removed if gasnet_attach()
		  completed successfully */
		gasnetc_remove_dma_queues();
		gasnetc_sci_remove_barrier_segment();
		gasnetc_sci_remove_sequence ();
		gasneti_free(gasnetc_sci_sd_long);
		gasneti_free(gasnetc_sci_remoteSegment_long);
		gasneti_free(gasnetc_sci_localMap);
		gasneti_free(gasnetc_sci_remoteMap);
		gasneti_free(gasnetc_sci_remoteMap_gb);
		gasneti_free(gasnetc_sci_remote_barrier_sd);
		gasneti_free(gasnetc_sci_remote_barrier_segment);
		gasneti_free(gasnetc_sci_remote_barrier_map);
		gasneti_free(gasnetc_sci_barrier_addr);
		gasneti_free(gasnetc_sci_dma_queue_status);
		gasneti_free(gasnetc_sci_sequence);
	}

	/* free all variables that don't depend on the
	completion of gasnet_attach()*/
	gasneti_free(gasnetc_sci_sd);
	gasneti_free(gasnetc_sci_sd_gb);
	gasneti_free(gasnetc_sci_sd_remote);
	gasneti_free(gasnetc_sci_SCI_Ids);
	gasneti_free(gasnetc_sci_local_mem);
	gasneti_free(gasnetc_sci_remote_mem);
	gasneti_free(gasnetc_sci_global_ready);
	gasneti_free(gasnetc_sci_localSegment);
	gasneti_free(gasnetc_sci_remoteSegment);
	gasneti_free(gasnetc_sci_remoteSegment_gb);
	/* finally, remove the files created by the start script for bootstrapping */
	remove(GASNETC_SCI_FILE);
}


#include "gasnet_core.c"

int NumElement = 100;
int payload[100];
int count = 100;

void Test_Handler_s (gasnet_token_t token, int input)
{
	/*
	printf ("___________________________________________\n");
	printf ("Sender ID = %d \n", token.source_id);
	printf ("Msg number = %d \n", token.msg_number);
	printf ("Input = %d \n", input);
	printf ("___________________________________________\n");
	*/
	int Status;

	do
	{
		Status = gasnetc_AMReplyShortM(token, 22, 0);
	} while (Status != GASNET_OK);
}

void Test_Handler_m (gasnet_token_t token, int *payload, size_t payload_size, int input)
{
	/*
	int j;
	printf ("___________________________________________\n");
	printf ("Sender ID = %d \n", token.source_id);
	printf ("Msg number = %d \n", token.msg_number);
	printf ("Input = %d \n", input);
	printf ("Payload Addr = %p \n", payload);
	printf ("Payload Size = %d \n", payload_size);
	for (j = 0; j < (payload_size / 4); j++)
	{
		printf ("Payload[%d] = %d \n", j, payload[j]);
	}
	printf ("___________________________________________\n");
	
	for (j = 0; j < NumElement; j++)
	{
		payload[j] = 100 + j + input;
	}
	*/
	int Status;
	do
	{
		Status = gasnetc_AMReplyMediumM(token, 33, payload, sizeof(payload), 1, 333);
	} while (Status != GASNET_OK);
}

void Test_Handler_l (gasnet_token_t token, int *input_payload, size_t input_payload_size, int input)
{	
	/*
	input_payload = gasnetc_sci_local_mem[gasnetc_nodes];
	printf ("___________________________________________\n");
	printf ("Sender ID = %d \n", token.source_id);
	printf ("Msg number = %d \n", token.msg_number);
	printf ("Input = %d \n", input);
	printf ("Payload Addr = %p \n", input_payload);
	printf ("Payload Size = %d \n", input_payload_size);
	int j;
	for (j = 0; j < (input_payload_size / 4); j++)
	{
		printf ("Payload[%d] = %d \n", j, input_payload[j]);
	}
	printf ("___________________________________________\n");
	for (j = 0; j < NumElement; j++)
	{
		payload[j] = 2200 + input;
	}
	*/
	int Status;
	do
	{
		Status = gasnetc_AMReplyLongM(token, 44, payload, sizeof(payload), NULL, 1, 444);
	} while (Status != GASNET_OK);
}

void send (int destination)
{
		int i;
		int TrueCount = 0;
		long average = 0;
		printf ("SENDER \n");
		printf ("payload size %d \n", sizeof(payload));
		long Inter[count];
		for (i = 0; i < count; i++)
		{
			int j;
			
			struct timeval starttime, endtime;
			gettimeofday(&starttime, NULL);
			int Status, Poll_Result;
			/*
			if ((i % 3) == 0)
			{
				Status = gasnetc_AMRequestShortM(destination, 1, 2, i*20, i*20 + 1);
			}
			else if ((i % 3) ==1)
			{
				for (j = 0; j < NumElement; j++)
				{
					payload[j] = i + 100 + j;
				}
				// Status = gasnetc_AMRequestMediumM(destination, 2, payload, sizeof(payload), 0);
				Status = gasnetc_AMRequestMediumM(destination, 2, payload, sizeof(payload), 16, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16);
			}
			else if ((i % 3) == 2)
			{
			*/

				for (j = 0; j < NumElement; j++)
				{
					payload[j] = i + 100;
				}
				Status = gasnetc_AMRequestLongM(destination, 0, payload, sizeof(payload), gasnetc_seginfo[destination].addr, 1, i*20);
			// }
			gettimeofday(&endtime, NULL);

			Inter[i] = (endtime.tv_usec - starttime.tv_usec) + ((endtime.tv_sec - starttime.tv_sec) * 1000000);
			printf ("Iteration #%d, Interval = %ld \n", i, Inter[i]);
			if (Inter[i] < 100000)
			{
				TrueCount++;
				average = average + Inter[i];
			}
		}
		float avg = (float) average / count;
		printf ("One way latency %f \n", avg / 2);
}

void poll ()
{
	int i = 0;
	printf ("RECEIVER \n");
	for (i = 0; i < count * (gasnetc_nodes -1); i++)
	{
		int Status = 0;
		do
		{
			Status = gasnetc_AMPoll();
		} while (Status != GASNET_OK);
	}
}

/******************************************************************************
 MAIN PROGRAM --  Calls each function to see how they react.
 Need to remove, or not include in any integration attempt.
******************************************************************************/
int main(int *argc, char*** argv)
{
	gasnet_handlerentry_t *table;
	int return_val, segsize;

	printf("\nStarting the test of functions.\n");
	fflush(NULL);
	return_val = gasnetc_init(argc, argv);
	if(return_val == GASNET_OK)
	{
		printf("\nGasnet_Init completed successfully.\n");
		fflush(NULL);
	}
	else
	{
		printf("Bad init\n");
		fflush(NULL);
		gasnetc_exit(-1);
		exit(1);
	}

	table = (gasnet_handlerentry_t *) malloc(sizeof(gasnet_handlerentry_t));
	segsize = gasnetc_getMaxLocalSegmentSize();
	printf("Max local Segment is : %d\n", segsize);
	segsize = gasnetc_getMaxGlobalSegmentSize();
	printf("Max Global Segment is : %d\n", segsize);
	printf("Going to attempt to attach.\n");
	fflush(stdout);
	return_val = gasnetc_attach(table, 0, segsize, 0);
	if(return_val == GASNET_OK)
	{
		printf("\nGood attach.Segment size = %d.\n", segsize);
		fflush(NULL);
	}

	printf ("My node = %d \n", gasnetc_mynode);
	printf ("Num node = %d \n", gasnetc_nodes);

	gasnetc_ht_add_handler (&Test_Handler_s, 1);
	gasnetc_ht_add_handler (&Test_Handler_m, 2);
	gasnetc_ht_add_handler (&Test_Handler_l, 3);

	fflush(NULL);
	
	int i;

/*
	printf ("-------------SEGMENT INFO TABLE---------------\n");
	for (i = 0; i < gasnetc_nodes; i++)
	{
		printf ("node = %d, Base addr = %p, Size = %d \n", i, gasnetc_seginfo[i].addr, gasnetc_seginfo[i].size);
	}
	printf ("\n");

	printf ("-------------MEF/MRF FLAGS---------------\n");
	for (i = 0; i < gasnetc_nodes * 4; i++)
	{
		printf ("%d / ", gasnetc_sci_msg_flag[i]);
	}
	printf ("\n");

	printf ("-------------MLS FLAGS---------------\n");
	for (i = 0; i < (gasnetc_nodes * 4); i++)
	{
		printf ("%d / ", msg_loc_status[i]);
	}
	printf ("\n");

	printf ("%p \n", Test_Handler_s);
	printf ("%p \n", Test_Handler_m);
	printf ("%p \n", Test_Handler_l);

	printf ("-------------HANDLER TABLE---------------\n");
	for (i = 0; i < 256; i++)
	{
		printf ("%d - %p \n", i, handler_table[i]);
	}
	printf ("\n");

	printf ("-------------Local Segment Address---------------\n");
	for (i = 0; i < gasnetc_nodes; i++)
	{
		printf ("%d - %p \n", i, gasnetc_sci_local_mem[i]);
	}
	printf ("\n");

	printf ("-------------Remote Segment Address---------------\n");
	for (i = 0; i < gasnetc_nodes; i++)
	{
		printf ("%d - %p \n", i, gasnetc_sci_remote_mem[i]);
	}
	printf ("\n");

	printf ("-------------Remote Segment Map---------------\n");
	for (i = 0; i < gasnetc_nodes; i++)
	{
		printf ("%d - %d \n", i, gasnetc_sci_remoteMap[i]);
	}
	printf ("\n");

	printf ("-------------Remote Segment Segment---------------\n");
	for (i = 0; i < gasnetc_nodes; i++)
	{
		printf ("%d - %d \n", i, gasnetc_sci_remoteSegment[i]);
	}
	printf ("\n");

	printf ("-------------GR Segment Address---------------\n");
	for (i = 0; i < gasnetc_nodes; i++)
	{
		printf ("%d - %p \n", i, gasnetc_sci_global_ready[i]);
	}
	printf ("\n");

	printf ("-------------Remote Segment long Segment---------------\n");
	for (i = 0; i < gasnetc_nodes; i++)
	{
		printf ("%d - %d \n", i, gasnetc_sci_remoteSegment_long[i]);
	}
	printf ("\n");

	printf ("DMA Addr %p \n", gasnetc_sci_local_dma_addr[0]);
	printf ("DMA Queue %d \n", gasnetc_sci_local_dma_queue[0]);
	printf ("DMA Segment %d \n", gasnetc_sci_local_dma_segment[0]);
*/
	printf ("BEFORE TRANSFER \n");
	fflush(NULL);

	if (gasnetc_mynode == 0)
	{
		poll ();
	}
	else
	{
		send (0);
	}

	fflush(NULL);

	gasnetc_exit(1);

	printf("Complete!\n");
	fflush(NULL);
	return 0;
}

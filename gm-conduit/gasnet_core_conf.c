/* $Id: gasnet_core_conf.c,v 1.5 2003/05/22 15:11:38 csbell Exp $
 * $Date: 2003/05/22 15:11:38 $
 * Description: GASNet GM conduit Implementation
 * Copyright 2002, Christian Bell <csbell@cs.berkeley.edu>
 * Copyright 2002, Dan Bonachea <bonachea@cs.berkeley.edu>
 * Terms of use are as specified in license.txt
 */
/* Much of the code below is taken from Myricom's MPICH bootstrap code */

#include <gasnet_core_internal.h>
#include <netdb.h>
#ifdef GASNETC_HAVE_BNR
#include <bnr.h>
#endif

#define RETURN_ERR(err)	do {	\
		printf err;			\
		printf ("\n");			\
		return GASNET_ERR_RESOURCE;	\
	} while (0)


#define GASNETC_INIT_TIMEOUT	(3*60*1000)	/* 3 minutes */
#define GASNETC_SOCKET_BUFSIZ	(128*1024)
#define GASNETC_GETENV(env, ptr, err) do {			\
		char *ptrenv = getenv(env);			\
		if (ptrenv == NULL || *ptrenv == '\0') {	\
			fprintf(stderr, err);			\
			return GASNET_ERR_RESOURCE;		\
		}						\
		else {						\
			ptr = ptrenv;				\
		}						\
	} while (0)

#ifdef GASNETC_HAVE_BNR
int
gasnetc_getconf_BNR(void)
{
	char	attr_buffer[BNR_MAXATTRLEN];
	char	val_buffer[BNR_MAXVALLEN];

	/* MPD to spawn processes */
	char	my_hostname[256];
	char	*hostnames;

	BNR_Group 	bnr_group;
		
	/* MPD to spawn processes */
	gmpi.mpd = 1;
	i = BNR_Init ();
	i = BNR_Get_group (&bnr_group);
	i = BNR_Get_rank (bnr_group, &gasnetc_mynode);
	i = BNR_Get_size (bnr_group, &gasnetc_nodes);
	
	/* data allocation */
	if (!gasnetc_alloc_nodemap(gasnetc_nodes))
		RETURN_ERR(("%d: Can't allocate node mapping", gasnetc_mynode));

	hostnames = (char *) gasneti_malloc (gasnetc_nodes * 256 * sizeof (char *));

	/* open a port */
	board_id = -1;
	port_id = 0;
	if (!gasnetc_gmport_allocate(&board_id, &port_id))
		RETURN_ERR(("%d: Can't obtain GM port", gasnetc_mynode));

	_gmc.my_port = port_id;

	/* get the GM node id */
	if (gm_get_node_id (_gmc.port, &_gmc.my_id) != GM_SUCCESS)
		RETURN_ERR(("%d: Can't get local GM node id", gasnetc_mynode));
		
	/* build the data to send to master */
	memset(val_buffer, 0, BNR_MAXVALLEN * sizeof (char));
	gm_get_host_name (_gmc.port, my_hostname);
	sprintf (val_buffer, "<%d:%d:%d:%s>\n", port_id, board_id, 
	    _gmc.my_id, my_hostname);
		
	/* put our information */
	memset (attr_buffer, 0, BNR_MAXATTRLEN * sizeof (char));
	sprintf (attr_buffer, "MPICH-GM data [%d]\n", gasnetc_mynode);
	i = BNR_Put (bnr_group, attr_buffer, val_buffer, -1);
		
	/* get other processes data */
	i = BNR_Fence (bnr_group);
	for (j = 0; j < gasnetc_nodes; j++) {
		memset (attr_buffer, 0, BNR_MAXATTRLEN * sizeof (char));
		sprintf (attr_buffer, "MPICH-GM data [%d]\n", j);
		i = BNR_Get (bnr_group, attr_buffer, val_buffer);
		
		/* decrypt data */
		if (sscanf (val_buffer, "<%hd:%*hd:%hd:%*s>",
		    _gmc.gm_nodes[j].port, _gmc.gm_nodes[j].id) != 2)
			RETURN_ERR(("%d: can't decode node mapping"));
	}

	return GASNET_OK;
}
#else
int
gasnetc_getconf_BNR(void)
{
	fprintf(stderr, "BNR via MPD is unsupported\n");
	return GASNET_ERR_RESOURCE;
}
#endif

int
gasnetc_getconf_sockets(void)
{
	char	*magic, *master, *port1, *port2;
	char	*id, *np, *board;
	char	buffer[GASNETC_SOCKET_BUFSIZ];
	char	*temp;
	//char	temp[32];
	int	sockfd;

	unsigned int 	count, magic_number, master_port1, master_port2;
	unsigned int	board_id, port_id;
	unsigned int	i, j;

	struct hostent		*master_he;
	struct sockaddr_in	sa;
		
	/* gasnetrun with sockets */
	GASNETC_GETENV ("GASNETGM_MAGIC", magic, "the job magic number");
	GASNETC_GETENV ("GASNETGM_MASTER", master, "the master's hostname");
	GASNETC_GETENV ("GASNETGM_PORT1", port1, "the master's port 1 number");
	GASNETC_GETENV ("GASNETGM_PORT2", port2, "the master's port 2 number");
	GASNETC_GETENV ("GASNETGM_ID", id, "the GASNet node id of the process");
	GASNETC_GETENV ("GASNETGM_NP", np, "the number of GASNet/GM processes");
	GASNETC_GETENV ("GASNETGM_BOARD", board, "the specified board");
	
	if (sscanf (magic, "%d", &magic_number) != 1)
		RETURN_ERR(("Bad magic number: %s", magic));

	_gmc.job_magic = magic_number;
		
	if ((sscanf (np, "%hu", (unsigned short *) &gasnetc_nodes) != 1) &&
	    gasnetc_nodes != (gasnet_node_t) -1)
		RETURN_ERR(("Bad number of processes: %s", np));
	else {
		char *nprocs;
		nprocs = getenv("GEXEC_NPROCS");
		if (nprocs != NULL && *nprocs != '\0')
			gasnetc_nodes = (gasnet_node_t) atoi(nprocs);
		else {
			nprocs = getenv("LAMWORLD");
			if (nprocs != NULL && *nprocs != '\0')
				gasnetc_nodes = (gasnet_node_t) atoi(nprocs);
		}
	}
	if (gasnetc_nodes < 1)
		RETURN_ERR(("Bad number of processes: %s", np));

	if ((sscanf (id, "%hu", (unsigned short *) &gasnetc_mynode) != 1) && 
	    gasnetc_mynode != (gasnet_node_t) -1) 
		RETURN_ERR(("Bad id %d out of %d processes", gasnetc_mynode, 
		    gasnetc_nodes));
	else {
		char *rank;
		rank = getenv("GEXEC_MY_VNN");
		if (rank != NULL && *rank != '\0')
			gasnetc_mynode = (gasnet_node_t) atoi(rank);
		else {
			rank = getenv("LAMRANK");
			if (rank != NULL && *rank != '\0')
				gasnetc_mynode = (gasnet_node_t) atoi(rank);
		}
	}
	if (gasnetc_mynode >= gasnetc_nodes)
		RETURN_ERR(("Bad id %d out of %d processes", gasnetc_mynode, 
		    gasnetc_nodes));


	if (sscanf (port1, "%d", &master_port1) != 1) 
		RETURN_ERR(("Bad master port 1 (%s out of %d processes", 
		     id, gasnetc_nodes));
	_gmc.master_port1 = master_port1;

	if (sscanf (port2, "%d", &master_port2) != 1) 
		RETURN_ERR(("Bad master port 2 (%s out of %d processes", 
		    id, gasnetc_nodes));
	_gmc.master_port2 = master_port2;

	if (sscanf (board, "%d", &board_id) != 1)
		RETURN_ERR(("Bad magic number: %s", magic));
	_gmc.my_board = board_id;

	port_id = 0;
	if (!gasnetc_gmport_allocate(&board_id, &port_id))
		RETURN_ERR(("%d: Can't obtain GM port", gasnetc_mynode));

	_gmc.my_port = port_id;

	/* get the GM node id */
	if (gm_get_node_id (_gmc.port, &_gmc.my_id) != GM_SUCCESS)
		RETURN_ERR(("%d: Can't get local GM node id", gasnetc_mynode));

	/* allocate space for node mapping */
	if (!gasnetc_alloc_nodemap(gasnetc_nodes))
		RETURN_ERR(("%d: Can't allocate node mapping", gasnetc_mynode));

	/* get a socket */
	sockfd = socket (AF_INET, SOCK_STREAM, 0);
	if (sockfd < 0)
		RETURN_ERR(("%d: Can't open socket", gasnetc_mynode));

	/* get the master's IP address */
	master_he = gethostbyname (master);
	if (master_he == NULL) 
		RETURN_ERR(("%d: Can't get IP from master host %s", 
		     gasnetc_mynode, master));

	/* connect to the master */
	memset(&_gmc.master_addr, 0, sizeof(struct sockaddr));
	_gmc.master_addr.sin_family = AF_INET;
	_gmc.master_addr.sin_port = htons(_gmc.master_port1);

	memcpy(&_gmc.master_addr.sin_addr, 
	    master_he->h_addr, master_he->h_length);

	i = GASNETC_INIT_TIMEOUT;
	while (connect (sockfd, (struct sockaddr *) &_gmc.master_addr, 
	    sizeof (_gmc.master_addr)) < 0) {
		usleep (1000);
		if (i == 0)
			RETURN_ERR(("%d: Can't connect to master", 
			    gasnetc_mynode));
		i--;
	}

	/* send the magic:ID:port:board:node used to the master */
	count = 0;
	sprintf (buffer, "<<<%d:%d:%d:%d:%d:%d>>>\n", magic_number, 
	    gasnetc_mynode, port_id, board_id, _gmc.my_id, (int) getpid ());

	while (count < strlen (buffer)) {
		i = write (sockfd, &buffer[count], strlen (buffer) - count);
		if (i < 0)
			RETURN_ERR(("%d: Write to socket failed", 
			    gasnetc_mynode));
		count += i;
	}
	close (sockfd);
		
	/* get another socket */
	sockfd = socket (AF_INET, SOCK_STREAM, 0);
	if (sockfd < 0)
		RETURN_ERR(("%d: Can't open second socket", gasnetc_mynode));

	/* re-connect to the master */
	_gmc.master_addr.sin_port = htons(_gmc.master_port2);
	i = GASNETC_INIT_TIMEOUT;
	while (connect (sockfd, (struct sockaddr *) &_gmc.master_addr, 
	    sizeof (_gmc.master_addr)) < 0) {
		usleep (1000);
		if (i == 0)
			RETURN_ERR(("%d: Can't connect to master", 
			    gasnetc_mynode));
		i--;
	}

	/* send the a small authentification magic:ID to the master */
	count = 0;
	sprintf (buffer, "<->%d:%d<->\n", magic_number, gasnetc_mynode);

	while (count < strlen (buffer)) {
		i = write (sockfd, &buffer[count], strlen (buffer) - count); 
		if (i < 0) 
			RETURN_ERR(("%d: Write to socket failed", gasnetc_mynode));
		count += i;
	}

	/* Get the whole GM mapping from the master */
	count = 0;
	memset(buffer, '0', GASNETC_SOCKET_BUFSIZ * sizeof(char));

	while (strstr (buffer, "]]]") == NULL) {
		i = read (sockfd, &buffer[count], GASNETC_SOCKET_BUFSIZ-count);
		if (i < 0)
			RETURN_ERR(("%d: read from socket failed", 
			    gasnetc_mynode));
		count += i;
	}
	close (sockfd);
  
	/* check the initial marker */
	j = 0;
	if (strncmp (&buffer[j], "[[[", 3) != 0)
		RETURN_ERR(("%d: bad format in node data from master",
		    gasnetc_mynode));

	/* Decrypt the global mapping */
	j += 3;
	for (i = 0; i < gasnetc_nodes; i++) {
		if (sscanf (&buffer[j], "<%hu:%*d:%hu>",
		    (unsigned short *) &_gmc.gm_nodes[i].port, 
		    (unsigned short *) &_gmc.gm_nodes[i].id) != 2)
			RETURN_ERR(("%d: can't decode node mapping", 
			    gasnetc_mynode));

		_gmc.gm_nodes_rev[i].port = _gmc.gm_nodes[i].port;
		_gmc.gm_nodes_rev[i].id = _gmc.gm_nodes[i].id;
		_gmc.gm_nodes_rev[i].node = i;

		temp = strchr(&buffer[j], '>');
		if (temp == NULL)
			RETURN_ERR(("%d: can't decode node mapping (>)", 
			    gasnetc_mynode));
		j += (size_t) (temp - &buffer[j] + 1);
	}

	/* decrypt the local mapping */
	if (strncmp (&buffer[j], "]]]", 3) != 0)
		RETURN_ERR(("%d: can't decode node mapping (end marker)", 
		    gasnetc_mynode));

	/* check consistency */
	if ((_gmc.gm_nodes[gasnetc_mynode].port != _gmc.my_port) 
	    || (_gmc.gm_nodes[gasnetc_mynode].id != _gmc.my_id))
		RETURN_ERR(("%d: inconsistency in data collected from master",
		    gasnetc_mynode));

	qsort(_gmc.gm_nodes_rev, gasnetc_nodes, sizeof(gasnetc_gm_nodes_rev_t),
	    gasnetc_gm_nodes_compare);

	return GASNET_OK;
}

extern int
gasnetc_getconf (void)
{
	setbuf (stdout, NULL);
	setbuf (stderr, NULL);

	if (getenv ("MAN_MPD_FD") != NULL)
		return gasnetc_getconf_BNR();
	else {
		if (getenv("GASNETGM_MASTER") == NULL)
			return gasnetc_getconf_conffile();
		else 
			return gasnetc_getconf_sockets();
	}
}

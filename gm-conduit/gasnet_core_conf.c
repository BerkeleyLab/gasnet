/* $Id: gasnet_core_conf.c,v 1.9 2003/11/08 23:50:57 csbell Exp $
 * $Date: 2003/11/08 23:50:57 $
 * Description: GASNet GM conduit Implementation
 * Copyright 2002, Christian Bell <csbell@cs.berkeley.edu>
 * Copyright 2002, Dan Bonachea <bonachea@cs.berkeley.edu>
 * Terms of use are as specified in license.txt
 */
/* Much of the code below is taken from Myricom's MPICH bootstrap code */

#include <gasnet_core_internal.h>
#include <string.h>
#include <netdb.h>

#define RETURN_ERR(err)	do {	\
		printf err;			\
		printf ("\n");			\
		return GASNET_ERR_RESOURCE;	\
	} while (0)


#define GASNETC_INIT_TIMEOUT	(3*60*1000)	/* 3 minutes */
#define GASNETC_SOCKET_BUFSIZ	(128*1024)

#define GASNETC_GETENV(env, ptr, err) do {				\
		char *ptrenv = getenv(env);				\
		if (ptrenv == NULL || *ptrenv == '\0') {		\
			gasneti_fatalerror("%d> Bootstrap error: %s\n",	\
				gasnetc_mynode, err);			\
		}							\
		else {							\
			ptr = ptrenv;					\
		}							\
	} while (0)

static
char *
gasnetc_gexec_hostname(char *buf, size_t buflen, gasnet_node_t nodeid)
{
	char	*svrs, *rank, *c, *e, *hostname;
	int	id, procs, i, len;
	size_t	s;

	svrs = getenv("GEXEC_SVRS");
	if (svrs == NULL || *svrs == '\0')
		return NULL;

	c = svrs;
	if (*c == ' ')
		c += strspn(svrs, " ");
	if (*c == '\0')
		return NULL;

	for (i = 0; i < nodeid; i++) {
		c += strcspn(c, " \t");
		if (*c == '\0') 
			return NULL;
		c += strspn(c, " ");
		if (*c == '\0') 
			return NULL;
	}

	len = strcspn(c, " \t");

	memcpy(buf, c, len);
	buf[len] = '\0';

	return buf;
}

static
void
gasnetc_get_nodeids(gasnet_node_t *myid, gasnet_node_t *numnodes)
{
	char    	*nprocs, *rank;
	gasnet_node_t	id, nodes;

	nprocs = getenv("GEXEC_NPROCS");

	if (nprocs != NULL && *nprocs != '\0') {
		nodes = (gasnet_node_t) atoi(nprocs);

		rank = getenv("GEXEC_MY_VNN");
		if (rank == NULL || *rank == '\0')
			gasneti_fatalerror(
			    "Can't obtain the number of GASNet/GM processes");
		id = (gasnet_node_t) atoi(rank);
	}
	else {
		nprocs = getenv("GMPI_NP");
		if (nprocs == NULL || *nprocs == '\0')
			gasneti_fatalerror(
			    "Can't obtain the number of GASNet/GM processes");
		nodes = (gasnet_node_t) atoi(nprocs);

		rank = getenv("GMPI_ID");
		if (rank == NULL || *rank == '\0')
			gasneti_fatalerror(
			    "Can't obtain the number of GASNet/GM processes");
		id = (gasnet_node_t) atoi(rank);
	}

	if (nodes < 1)
		gasneti_fatalerror("Bad number of processes: %d", nodes);
	
	if (id >= nodes)
		gasneti_fatalerror("Bad id %d out of %d processes", id, nodes);

	*numnodes = nodes;
	*myid = id;

	return;
}

void
gasnetc_getconf_mpiexec()
{
	char	*magic, *master, *port;
	char	*id, *np, *board;
	char	buffer[GASNETC_SOCKET_BUFSIZ];
	char	*temp;
	int	sockfd, sockfd2;

	unsigned int	slave_port, master_port;

	unsigned int 	count, magic_number;
	unsigned int	board_id, port_id, temp_id, temp_local_id;
	unsigned int	i, j;

	struct sockaddr_in	sa;
		
	/* gasnetrun with sockets */
	GASNETC_GETENV ("GMPI_MAGIC", magic, "the job magic number");
	GASNETC_GETENV ("GMPI_MASTER", master, "the master's hostname");
	GASNETC_GETENV ("GMPI_PORT", port, "the master's port number");
	GASNETC_GETENV ("GMPI_BOARD", board, "the specified board");

	/* 
	 * Check for mpich 1.2.4..8, which we do _not_ support.
	 *
	 * XXX This might break if Myricom decides to revive PORT1 in a new
	 *     mpirun revision.  
	 */
	{
		char *t1, *t2;
		t1 = getenv("GMPI_PORT1");
		t2 = getenv("MPIEXEC_STDOUT_PORT");

		if (t1 != NULL && *t1 != '\0' &&
		    (t2 == NULL || *t2 == '\0'))
			gasneti_fatalerror(
		    "Bootstrap doesn't support spawning from MPICH 1.2.4..8");
	}

	if (sscanf (magic, "%d", &magic_number) != 1)
		gasneti_fatalerror("Bootstrap: Bad magic number %s", magic);
	_gmc.job_magic = magic_number;
		
	if (sscanf (port, "%d", &master_port) != 1) 
		gasneti_fatalerror(
		    "Bootstrap: Bad master port 1 (%s out of %d processes", 
		    id, gasnetc_nodes);
	_gmc.master_port = master_port;

	if (sscanf (board, "%d", &board_id) != 1)
		gasneti_fatalerror("Bootstrap: Bad magic number: %s", magic);
	_gmc.my_board = board_id;

	gasnetc_get_nodeids(&gasnetc_mynode, &gasnetc_nodes);

	port_id = 0;
	if (!gasnetc_gmport_allocate((int*)&board_id, (int*)&port_id))
		gasneti_fatalerror("%d: Can't obtain GM port", gasnetc_mynode);

	_gmc.my_port = port_id;

	/* get the GM node id */
	if (gm_get_node_id (_gmc.port, &_gmc.my_id) != GM_SUCCESS)
		gasneti_fatalerror(
		    "%d: Can't get local GM node id", gasnetc_mynode);

	#ifdef GASNETC_GM_2
	temp_id = _gmc.my_id;

	/* GM2 only stores local node ids, so a global has to be obtained */
	if (gm_node_id_to_global_id(_gmc.port, temp_id, &(_gmc.my_id)) 
	    != GM_SUCCESS)
		gasneti_fatalerror("Couldn't get GM global node id");
	#endif

	/* allocate space for node mapping */
	if (!gasnetc_alloc_nodemap(gasnetc_nodes))
		gasneti_fatalerror(
		    "%d: Can't allocate node mapping", gasnetc_mynode);

	/*
	 * Set up a socket for the master to connect o.
	 * 
	 */
	sockfd = socket (AF_INET, SOCK_STREAM, 0);
	if (sockfd < 0)
		gasneti_fatalerror(
		    "%d> Can't open first socket", gasnetc_mynode);
	else {
		struct hostent	*slave_he;
		char		*slave;
		char		buf[256];

		slave = getenv("GMPI_SLAVE");
		if (slave == NULL || *slave == '\0') {
			slave = gasnetc_gexec_hostname(buf, 256, 
					gasnetc_mynode);

			if (slave == NULL)
				gasneti_fatalerror(
				    "Can't identify local hostname");
		}

		slave_he = gethostbyname(slave);
		if (slave_he == NULL)
			gasneti_fatalerror("%d> can't get hostname for %s",
					gasnetc_mynode, slave);
		/*
		 * Bind the slave to a port
		 */

		memset(&_gmc.slave_addr, 0, sizeof(struct sockaddr));
		_gmc.slave_addr.sin_family = AF_INET;
		memcpy(&_gmc.slave_addr.sin_addr, 
			slave_he->h_addr, slave_he->h_length);

		for (slave_port = 8000; slave_port < 20000; slave_port++) {
			_gmc.slave_addr.sin_port = htons(slave_port);
			if (bind(sockfd, (struct sockaddr *) &(_gmc.slave_addr),
			    sizeof(_gmc.slave_addr)) == 0)
				break;
		}
		if (slave_port >= 20000)
			gasneti_fatalerror(
			    "%d> Couldn't find a port to bind slave socket",
			    gasnetc_mynode);
	
	}

	/*
	 * Listen for the master
	 */
	if (listen(sockfd, 1) != 0)
		gasneti_fatalerror("%d> can't listen() on socket",
				gasnetc_mynode);

	/*
	 * Get a second socket to connect to the master
	 *
	 */
	sockfd2 = socket (AF_INET, SOCK_STREAM, 0);
	if (sockfd2 < 0)
		gasneti_fatalerror(
		    "%d> Can't open second socket", gasnetc_mynode);
	else {
		struct hostent		*master_he;
		gm_u64_t 		start_time, stop_time;
		ssize_t			b;
		int			junk;

		master_he = gethostbyname (master);

		if (master_he == NULL)
			gasneti_fatalerror("%d> can't get hostname for %s",
					gasnetc_mynode, master);

		memset(&_gmc.master_addr, 0, sizeof(struct sockaddr));
		_gmc.master_addr.sin_family = AF_INET;
		_gmc.master_addr.sin_port = htons(_gmc.master_port);
		memcpy(&_gmc.master_addr.sin_addr, master_he->h_addr, 
		       master_he->h_length);

		start_time = gm_ticks(_gmc.port);

		while (connect(sockfd2, (struct sockaddr *) 
		    &_gmc.master_addr, sizeof(_gmc.master_addr)) < 0) {

			stop_time = gm_ticks(_gmc.port);
			
			if ((stop_time - start_time) > 
			    (2000 * GASNETC_INIT_TIMEOUT))
				gasneti_fatalerror(
				    "%d> Unable to connect to master",
				    gasnetc_mynode);
		}

		/* 
		 * Send the magic:ID:port:board:node:numanode:pid::remoteport
		 * used to the master 
		 */

		count = 0;
		sprintf (buffer, 
			"<<<%d:%d:%d:%d:%u:%d:%d::%d>>>\n", 
			magic_number, gasnetc_mynode, port_id, board_id, 
			(unsigned) _gmc.my_id, 0, (int) getpid(), slave_port);

		while (count < strlen (buffer)) {
			b = write(sockfd2, &buffer[count], 
				  strlen (buffer) - count);
			if (b < 0)
				gasneti_fatalerror("%d> can't write to socket",
						gasnetc_mynode);
			count += b;
		}
		close (sockfd2);
		
		/*
		 * Wait for the master to send the mapping
		 */
		sockfd2 = accept(sockfd, 0, 0);
		if (sockfd2 < 0)
			gasneti_fatalerror("%d> can't accept on socket",
					gasnetc_mynode);

		count = 0;
		memset(buffer, '0', GASNETC_SOCKET_BUFSIZ * sizeof(char));

		/*
		 * Read the entire mapping
		 *
		 */
		while (strstr (buffer, "]]]") == NULL) {
			b = read(sockfd2, &buffer[count], 
				 GASNETC_SOCKET_BUFSIZ-count);
			if (b < 0)
				gasneti_fatalerror("%d> can't read from socket",
						gasnetc_mynode);
			count += b;
		}

		close(sockfd2);


		/*
		 * Find where the mapping actually starts
		 */
		j = 0;
		if (strncmp (buffer, "[[[", 3) != 0)
			gasneti_fatalerror(
			    "%d> bad format in node data from master",
			    gasnetc_mynode);

		/*
		 * Decrypt the mapping
		 *
		 * We ignore the board id (field 2) and numa_nodes (field 4).
		 */
		j += 3;
		for (i = 0; i < gasnetc_nodes; i++) {
			if (sscanf (&buffer[j], "<%hu:%*d:%u:%*d>",
		    	    (unsigned short *) &_gmc.gm_nodes[i].port, &temp_id) != 2)
				gasneti_fatalerror("%d> can't decode node mapping",
						gasnetc_mynode);
			
			_gmc.gm_nodes_rev[i].port = _gmc.gm_nodes[i].port;
			_gmc.gm_nodes_rev[i].node = i;

			#ifdef GASNETC_GM_2
			temp_local_id = 0;
			if (gm_global_id_to_node_id(_gmc.port, temp_id,
			    &temp_local_id) != GM_SUCCESS)
				gasneti_fatalerror("%d> couldn't translate "
				    "GM global node id (%u) for gasnet "
				    "node id %d", gasnetc_mynode, 
				    (unsigned) temp_id, i);

			_gmc.gm_nodes_rev[i].id = (uint16_t) temp_local_id;
			_gmc.gm_nodes[i].id = (uint16_t) temp_local_id;
			#else
			_gmc.gm_nodes[i].id = temp_id;
			_gmc.gm_nodes_rev[i].id = temp_id;
			#endif

			temp = strchr(&buffer[j], '>');
			if (temp == NULL)
				gasneti_fatalerror("%d> malformed node map",
						gasnetc_mynode);
			j += (size_t) (temp - &buffer[j] + 1);
		}

		/* 
		 * Skip the local mapping .
		 *
		 * The global->local ampping is separated by |||
		 */

		if (strncmp (&buffer[j], "]]]", 3) != 0 &&
		    strncmp (&buffer[j], "|||", 3) != 0) {
			char toto[96];
			snprintf(toto, 96, &buffer[j]);
			gasneti_fatalerror(
			    "%d: can't decode node mapping (end marker): %s",
			    gasnetc_mynode, toto);
		}

		/* check consistency */
		if (_gmc.gm_nodes[gasnetc_mynode].port != _gmc.my_port)
			gasneti_fatalerror(
			    "%d: inconsistency in data collected from master",
			    gasnetc_mynode);

		qsort(_gmc.gm_nodes_rev, gasnetc_nodes,
				sizeof(gasnetc_gm_nodes_rev_t),
				gasnetc_gm_nodes_compare);
	}

	return;
}

extern void
gasnetc_getconf ()
{
	setbuf (stdout, NULL);
	setbuf (stderr, NULL);

	if (getenv("GMPI_MAGIC"))
		gasnetc_getconf_mpiexec();
	else
		gasnetc_getconf_conffile();

	return;
}

#include <sys/types.h>
#include <sys/socket.h>
#include <sys/fcntl.h>
#include <string.h>
#include <netinet/in.h>
#include <stdio.h>
#include <pthread.h>
#include <unistd.h>
#include <poll.h>

#include <gasnet.h>
#include <gasnet_internal.h>
#include <firehose.h>

#define SENDPORTSTART 21000
#define RECVPORTSTART 20100

#define BACKLOG	10

/* No interrupts */
extern int gasnetc_hold_interrupts() { return; }
extern int gasnetc_resume_interrupts() { return; }

gasnet_node_t gasnetc_mynode;
gasnet_node_t gasnetc_nodes;

typedef struct _gasnetc_sockmap {
    gasnet_node_t   node;
    int		    fd;
} 
gasnetc_sockmap_t;

/*
 * One mapping for nodeid -> fd and one for pollfd index -> node
 */
gasnetc_sockmap_t   *gasnetc_IdMapFd;
gasnetc_sockmap_t   *gasnetc_PollMapNode;

gasnet_node_t	 gasnetc_nodes;
gasnet_node_t	 gasnetc_mynode;
int		 gasnetc_threadspernode = 1;

int		*gasnetc_sockfds;
struct pollfd	*gasnetc_pollfds;

static gasneti_mutex_t gasnetc_socklock = GASNETI_MUTEX_INITIALIZER;

gasnetc_handler_fn_t	gasnetc_handlers[256];

int
gasnetc_AMGetMsgSource(gasnet_token_t token, gasnet_node_t *node)
{
    gasnetc_sockmap_t	*smap = (gasnetc_sockmap_t *) token;

    gasneti_assert(smap != NULL);
    gasneti_assert(smap->node < gasnetc_nodes);
    *node = smap->node;
}

// a wrapper around send so that partial sends are abstracted away
void 
gasnetc_writesocket(int destfd, char *msg, int len) {
  int bytessent=0;
  
  while(bytessent<len) {
    int temp;
    temp = write(destfd, msg+bytessent, len-bytessent);
    if(temp==-1) {
      perror("write");
      exit(1);
    } else {
      bytessent+=temp;
    }
  }
}

// a wrapper around recv so that partial receves are abstracted away
void 
gasnetc_readsocket(int srcfd, char *msg, int len) {
  int bytesrecv=0;
  
  while(bytesrecv<len) {
    int temp;
    temp = read(srcfd, msg+bytesrecv, len-bytesrecv);
    if(temp==-1) {
      perror("read");
      exit(1);
    } else {
      bytesrecv+=temp;
    }
  }
}




extern int 
gasnetc_AMRequestMediumM( 
	    gasnet_node_t dest,      /* destination node */
            gasnet_handler_t handler, /* index into destination endpoint's handler table */ 
            void *source_addr, size_t nbytes,   /* data payload */
            int numargs, ...) 
{
    int	    retval = 1, fd, wbytes;
    va_list argptr;
    void    *buf;
    uint8_t *hdrptr, *payptr;

    gasneti_assert(numargs >= 0 && numargs <= gasnet_AMMaxArgs());

    va_start(argptr, numargs); /*  pass in last argument */
    gasneti_assert(nbytes <= AM_MAXLEN);

    /* XXX local loopback ? */

    buf = gasneti_malloc(AM_BUFSZ);
    if (buf == NULL)
	    abort();

    hdrptr = (uint8_t *) buf;
    payptr = hdrptr + AM_PAYOFF;

    /* Pack args and header in buffer */
    *((uint8_t *) (hdrptr + 0)) =  (uint8_t) AM_REQUEST;
    *((uint8_t *) (hdrptr + 1)) =  (uint8_t) handler;
    *((uint16_t *)(hdrptr + 2)) = (uint16_t) numargs;
    *((uint32_t *)(hdrptr + 4)) = (uint32_t) nbytes;
    {
	int i;
	int32_t *pArg = (int32_t *) (hdrptr + 8);
	for (i = 0; i < numargs; i++)
	    pArg[i] = (int32_t) va_arg(argptr, int);
    }

    /* Copy payload */
    memcpy(payptr, source_addr, nbytes);
    fd = gasnetc_IdMapFd[dest].fd;
    gasnetc_writesocket(fd, buf, nbytes+AM_PAYOFF);
    /* On write completion, free the buffer */
    gasneti_free(buf);

    va_end(argptr);
    return GASNET_OK;
}

extern int 
gasnetc_AMReplyMediumM( 
	    gasnet_token_t token,       /* token provided on handler entry */
            gasnet_handler_t handler, /* index into destination endpoint's handler table */ 
            void *source_addr, size_t nbytes,   /* data payload */
            int numargs, ...) 
{
    int	    retval = 1;
    va_list argptr;
    void    *buf;
    uint8_t *hdrptr, *payptr;
    int	    fd;

    gasnet_node_t   node;

    gasneti_assert(numargs >= 0 && numargs <= gasnet_AMMaxArgs());

    va_start(argptr, numargs); /*  pass in last argument */
    gasneti_assert(nbytes <= AM_MAXLEN);

    buf = gasneti_malloc(AM_BUFSZ);
    if (buf == NULL)
	    abort();

    hdrptr = (uint8_t *) buf;
    payptr = hdrptr + AM_PAYOFF;

    /* Pack args and header in buffer */
    *((uint8_t *) (hdrptr + 0)) =  (uint8_t) AM_REPLY;
    *((uint8_t *) (hdrptr + 1)) =  (uint8_t) handler;
    *((uint16_t *)(hdrptr + 2)) = (uint16_t) numargs;
    *((uint32_t *)(hdrptr + 4)) = (uint32_t) nbytes;
    {
	int i;
	int32_t *pArg = (int32_t *) (hdrptr + 8);
	for (i = 0; i < numargs; i++)
	    pArg[i] = (int32_t) va_arg(argptr, int);
    }

    gasnetc_AMGetMsgSource(token, &node);
    fd = gasnetc_IdMapFd[node].fd;

    /* Copy payload */
    memcpy(payptr, source_addr, nbytes);
    gasnetc_writesocket(fd, buf, nbytes+AM_PAYOFF);
    /* On write completion, free the buffer */
    gasneti_free(buf);

    va_end(argptr);
    return GASNET_OK;
}

#define MAX_BUFS    256
int	 gasnetc_AMBufsIdx  = 0;
int	 gasnetc_AMBufsFree = 0;
uint8_t	*gasnetc_AMBufs[MAX_BUFS];

#if 0
void *
gasnetc_getRecvBuf()
{
#endif

extern int 
gasnetc_AMPoll()
{
    uint8_t  *pptr, hidx;
    int32_t  *argptr;
    uint16_t  hdr, numargs;
    uint32_t  paylen;
    size_t    nbytes;
    int	      ret, i;
    void     *buf, *payptr;

    gasnetc_sockmap_t *smap;

    if (gasnetc_nodes == 1)
	    return;

    gasneti_mutex_lock(&gasnetc_socklock);
    ret = poll(gasnetc_pollfds, gasnetc_nodes-1, 0);

    if (ret == -1) {
	perror("poll failed");
	exit(1);
    }
    else if (ret == 0)
	return GASNET_OK;

    for (i=0; i < gasnetc_nodes-1; i++) {

	if (gasnetc_pollfds[i].revents & (POLLERR|POLLHUP|POLLNVAL)) 
	    fprintf(stderr, "error polling fd %d\n", i);
	else if (gasnetc_pollfds[i].revents & POLLIN) {
	    gasnetc_pollfds[i].fd = gasnetc_sockfds[i];
	}
    }


    /*
    select(...)
    smap = 
    */

    pptr = (uint8_t *) buf;

    hdr     = *((uint8_t  *) (pptr + 0));
    hidx    = *((uint8_t  *) (pptr + 1));
    numargs = *((uint16_t *) (pptr + 2));
    paylen  = *((uint32_t *) (pptr + 4));
    argptr  =   (uint32_t *) (pptr + 8);
    payptr  =       (void *) (pptr + AM_PAYOFF);
    
    gasneti_assert(nbytes >= AM_PAYOFF);
    gasneti_assert(paylen == nbytes - AM_PAYOFF);
    gasneti_assert(gasnetc_handlers[hidx] != NULL);

    gasneti_mutex_unlock(&gasnetc_socklock);

    RUN_HANDLER(gasnetc_handlers[hidx], (void *) smap, argptr, numargs, payptr, paylen);

    return GASNET_OK;
}
    
extern int
firehose_move_callback(gasnet_node_t node, 
		       const firehose_region_t *unpin_list, 
		       size_t unpin_num, 
		       firehose_region_t *pin_list, 
		       size_t pin_num)
{
    /* How about writing 0xbb to each page ? */

}

extern int 
firehose_remote_callback(gasnet_node_t node, 
		const firehose_region_t *pin_list, size_t num_pinned,
		firehose_remotecallback_args_t *args)
{
    /* No support for remote callback yet */
    abort();
}

extern void
gasnetc_exit(int exitcode)
{
    exit(exitcode);
}

void setnonblock(int fd) {
  int fl;
  if ((fl=fcntl(fd, F_GETFL, 0))<0) {
    fprintf(stderr, "fcntl F_GETFL failed");
  }
  if(fcntl(fd, F_SETFL, fl|O_NONBLOCK)<0) {
    fprintf(stderr, "fcntl F_SETFL failed");
  }
}
void setblock(int fd) {
  int fl;
  if ((fl=fcntl(fd, F_GETFL, 0))<0) {
    fprintf(stderr,"fcntl F_GETFL failed");
  }
  if(fcntl(fd, F_SETFL, fl&~O_NONBLOCK)<0) {
    fprintf(stderr, "fcntl F_SETFL failed");
  }
}

/*
 * Initialize connects between all nodes (including self)
 */
void
gasnetc_init()
{
  int	send_port = SENDPORTSTART+gasnetc_mynode;
  int	i,j,val=1;
  int	tempfd, sin_size;

  struct sockaddr_in *ina, *ina_local;
  struct sockaddr_in my_addr, their_addr;
  
  ina	    = (struct sockaddr_in *) 
		    gasneti_malloc(sizeof(struct sockaddr_in)*gasnetc_nodes);
  ina_local = (struct sockaddr_in *) 
		    gasneti_malloc(sizeof(struct sockaddr_in)*gasnetc_nodes);

  for(i=0; i<gasnetc_nodes; i++) {
    gasnetc_sockfds[i] = -1;
  }

  my_addr.sin_family = AF_INET;         // host byte order
  my_addr.sin_port = htons(send_port);     // short, network byte order
  my_addr.sin_addr.s_addr = INADDR_ANY; // automatically fill with my IP
  memset(&(my_addr.sin_zero), '\0', 8); // zero the rest of the struct

  //create socket addresses for both sides of the communication
  // i play games with the ports to figure out the sender and receiver of the data
  for(i=0; i<gasnetc_nodes; i++) {
    ina[i].sin_addr.s_addr = inet_addr("127.0.0.1");
    ina[i].sin_family = AF_INET;
    ina[i].sin_port = htons((unsigned short) SENDPORTSTART+i);
    memset(&(ina[i].sin_zero), '\0', 8);
    ina_local[i].sin_addr.s_addr = inet_addr("127.0.0.1");
    ina_local[i].sin_family = AF_INET;
    ina_local[i].sin_port = htons((unsigned short) RECVPORTSTART+i+1000*gasnetc_mynode);
    memset(&(ina_local[i].sin_zero), '\0', 8);
  }
  //create the listening socket
  if ((tempfd = socket(AF_INET, SOCK_STREAM, 0)) == -1) {
    perror("socket");
    exit(1);
  }
  if (setsockopt(tempfd, SOL_SOCKET, SO_REUSEADDR, &val, sizeof(val)) == -1) {
    perror("reuseaddr socket option");
    exit(1);
  }
  //bind it to the listen port
  if(bind(tempfd, (struct sockaddr *)&my_addr, sizeof(struct sockaddr))==-1) {
    perror("bind");
    exit(1);
  }
  //set it to listen
  if(listen(tempfd, BACKLOG) == -1) {
    perror("listen");
   
    exit(1);
  }
  
  //accept 0, gasnetc_mynode-1 connections
  for(i=0; i<gasnetc_mynode; i++) {
    int new_fd;
    int their_id;
    sin_size = sizeof(struct sockaddr_in);
    struct sockaddr_in temp;

    if ((new_fd = accept(tempfd, (struct sockaddr *)&their_addr, &sin_size)) == -1) {
      perror("accept");
      continue;
    }
    temp = (struct sockaddr_in) their_addr;
    their_id = (ntohs((unsigned short) temp.sin_port)-gasnetc_mynode-RECVPORTSTART)/1000; 
    fprintf(stderr, "%d accepting from %d (port=%d), id=%d\n", 
		    gasnetc_mynode, their_id, (int)ntohs(temp.sin_port), new_fd);
    ina[their_id] = (struct sockaddr_in) their_addr;
    gasnetc_sockfds[their_id] = new_fd;
  }

  //fill the array at gasnetc_mynode with junk.
  gasnetc_sockfds[gasnetc_mynode] = -42; 
  ina[gasnetc_mynode] =  ina_local[gasnetc_mynode];
  close(tempfd); /*tempfd is no longer needed*/

  //fill gasnetc_mynode+1 to gasnetc_nodes with the other connections
  for (i=gasnetc_mynode+1; i<gasnetc_nodes; i++) {
    fprintf(stderr, "%d connecting to %d using %d\n", gasnetc_mynode, i, 
		    RECVPORTSTART+i+1000*gasnetc_mynode);

    if ((gasnetc_sockfds[i] = socket(AF_INET, SOCK_STREAM, 0)) == -1) {
      perror("socket");
      exit(1);
    }
    if (setsockopt(gasnetc_sockfds[i], SOL_SOCKET, SO_REUSEADDR, 
		   &val, sizeof(val)) == -1) {
	perror("reuseaddr socket option");
	exit(1);
    }
    if (bind(gasnetc_sockfds[i], (struct sockaddr *) &ina_local[i], 
			    sizeof(struct sockaddr))==-1) {
	perror("bind");
	exit(1);
    }
    
    if (connect(gasnetc_sockfds[i], (struct sockaddr *)&ina[i],
		sizeof(struct sockaddr)) == -1) {
      perror("connect");
      exit(1);
    }
    
    fprintf(stderr, "connection succeeded on port %d, id=%d\n", 
		    (int) ntohs(ina_local[i].sin_port),
		    gasnetc_sockfds[i]);
    
  }
  //setup the poll list
  //since we will not poll local host we only have total-nodes -1 on the poll list
  //and thus we use seperate counters for the poll list index (kinda of a hack but works
  for(j=0, i=0; i<gasnetc_nodes; i++) {
    if(i!=gasnetc_mynode) {
      // setnonblock(sockfds[i]);
      gasnetc_pollfds[j].fd = gasnetc_sockfds[i];
      gasnetc_pollfds[j].events = POLLIN | POLLERR | POLLHUP | POLLNVAL;
      gasnetc_PollMapNode[j].node = i;
      gasnetc_PollMapNode[j].fd = gasnetc_sockfds[i];
      j++;
    }

    gasnetc_IdMapFd[i].node = i;
    gasnetc_IdMapFd[i].fd   = gasnetc_sockfds[i];
  }

  gasneti_free(ina);
  gasneti_free(ina_local);
}

void
gasnetc_finalize()
{
    int	i;

    for (i = 0; i < gasnetc_nodes; i++) {
	if (gasnetc_sockfds[i] > 0) {
	    printf("closing socket id %d\n", gasnetc_sockfds[i]);
	    if (close(gasnetc_sockfds[i]) == -1) {
		fprintf(stderr, "close failed!\n");
		exit(EXIT_FAILURE);
	    }
	}

    }
}

int
main(int argc, char **argv)
{
    int i;

    pthread_t *client_thread_ids;
    //[THREADS_PER_PROC];
    pthread_t send_thread; /*will handle all the sends*/
    pthread_t recv_thread; /*will handle all the recvs*/

    /*
     * testconduit <mynode> <numnodes> [numthreads]
     */
    if (argc < 3) {
	fprintf(stderr, "%s <mynode> <numnodes> [numthreads]\n", argv[0]);
	exit(EXIT_FAILURE);
    }

    gasnetc_mynode = (gasnet_node_t) atoi(argv[1]);
    gasnetc_nodes  = (gasnet_node_t) atoi(argv[2]);

    if (argc > 3 && argv[3] != NULL)
	gasnetc_threadspernode = atoi(argv[3]);
	
    gasnetc_sockfds = (int *) gasneti_malloc(sizeof(int) * gasnetc_nodes);
    gasnetc_pollfds = (struct pollfd *) 
			gasneti_malloc(sizeof(struct pollfd) * (gasnetc_nodes));

    /* Initilize firehose handlers */
    {
	gasnet_handlerentry_t *fh_hnds = firehose_get_handlertable();
	int gidx = 1; /* first free gasnet handler is 1 */
	int fidx = 0; /* where to start looking for handlers */

	while (fh_hnds[fidx].index != 0) {
	    gasnetc_handlers[gidx] = fh_hnds[fidx].fnptr;
	    fh_hnds[fidx].index = gidx;
	    fprintf(stderr, "registered fh idx %d at idx %d\n", fidx, gidx);
	    gidx++;
	    fidx++;
	}
    }

    gasnetc_init();

    printf("i am %d of %d\n", gasnetc_mynode, gasnetc_nodes);
    
    gasnetc_finalize();

}


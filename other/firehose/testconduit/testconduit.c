/*   $Source: /Users/kamil/work/gasnet-cvs2/gasnet/other/firehose/testconduit/Attic/testconduit.c,v $
 *     $Date: 2004/08/26 04:53:59 $
 * $Revision: 1.3 $
 * Description: 
 * Copyright 2004, Christian Bell <csbell@cs.berkeley.edu>
 * Terms of use are as specified in license.txt
 */
#include <sys/types.h>
#include <sys/socket.h> 
#include <sys/fcntl.h>
#include <string.h>
#include <netinet/in.h>
#include <stdio.h>
#include <pthread.h>
#include <unistd.h>
#include <errno.h>
#include <poll.h>

#include <gasnet.h>
#include <gasnet_internal.h>
#include <gasnet_handler.h>
#include <firehose.h>

#define SENDPORTSTART 24000
#define RECVPORTSTART 20400

#define BACKLOG	10

extern int errno;

/* No interrupts */
extern int gasnetc_hold_interrupts() { return; }
extern int gasnetc_resume_interrupts() { return; }

gasnet_node_t gasnetc_mynode;
gasnet_node_t gasnetc_nodes;
gasnet_node_t gasnete_mynode;
gasnet_node_t gasnete_nodes;

typedef struct _gasnetc_sockmap {
    gasnet_node_t   node;
    int		    fd;
} 
gasnetc_sockmap_t;

typedef struct _gasnetc_sockdata {
    uint8_t  hdr;
    uint8_t  handler_idx;
    uint16_t numargs;
    uint32_t paylen;

    int32_t  *argptr;
    void     *payptr;

    /* User provides buf an buflen */
    void     *buf;
    uint32_t buflen;

}
gasnetc_sockdata_t;

gasnet_seginfo_t    *gasnetc_seginfo;

firehose_info_t	  gasnetc_firehose_info;

/*
 * One mapping for nodeid -> fd and one for pollfd index -> node
 */
gasnetc_sockmap_t   *gasnetc_IdMapFd;
gasnetc_sockmap_t   *gasnetc_PollMapNode;
int		    *gasnetc_ThreadMapNode;

gasnet_node_t	 gasnetc_nodes;
gasnet_node_t	 gasnetc_mynode;
int              gasnetc_threads;
int		 gasnetc_threadspernode = 1;
int		 gasnetc_send_portstart = SENDPORTSTART;
int		 gasnetc_recv_portstart = RECVPORTSTART;

int		*gasnetc_sockfds;
struct pollfd	*gasnetc_pollfds;

uintptr_t       gasnetc_MaxLocalSegmentSize = 0;
uintptr_t       gasnetc_MaxGlobalSegmentSize = 0;


#if 0
static gasneti_mutex_t gasnetc_socklock = GASNETI_MUTEX_INITIALIZER;
#define SOCK_LOCK   gasneti_mutex_lock(&gasnetc_socklock)
#define SOCK_UNLOCK gasneti_mutex_unlock(&gasnetc_socklock)
#define SOCK_ASSERT_LOCKED gasneti_mutex_assertlocked(&gasnetc_socklock)
#else
static pthread_mutex_t gasnetc_socklock = PTHREAD_MUTEX_INITIALIZER;
#define SOCK_LOCK   pthread_mutex_lock(&gasnetc_socklock)
#define SOCK_UNLOCK pthread_mutex_unlock(&gasnetc_socklock)
#define SOCK_ASSERT_LOCKED pthread_mutex_assertlocked(&gasnetc_socklock)
#endif

void	gasnetc_barrier();
void	gasnetc_bootstrapExchange(void *src, size_t len, void *dest);
void	gasnetc_init();
void	gasnetc_exit();
int	gasnetc_AMPoll();
size_t	gasnetc_readsocket(int fd, void *buf, size_t len, gasnetc_sockdata_t *sd);
void	gasnetc_writesocket(int destfd, char *msg, int len);
int	gasnetc_AMGetMsgSource(gasnet_token_t token, gasnet_node_t *node);

#define client_put(rem_thread,dest,src,nbytes,mythread)	\
	    gasnete_put_node(gasnetc_ThreadMapNode[rem_thread],dest,src,nbytes,mythread)

void	gasnete_put_node(int rem_tid, void *dest, void *src, size_t nbytes, int tid);

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
size_t
gasnetc_readsocket(int fd, void *buf, size_t len, gasnetc_sockdata_t *sd)
{
    uint8_t *hptr = (uint8_t *) buf;
    uint8_t *pptr = (uint8_t *) buf + AM_HDRLEN;
    size_t  bread = 0, brecv;

    gasneti_assert(len >= AM_HDRLEN);

    sd->buf    = buf;
    sd->buflen = len;

    brecv = read(fd, hptr, AM_HDRLEN);

    if (brecv == 0)
	return 0;

    if (brecv != AM_HDRLEN)
	gasneti_fatalerror("Couldn't read message header: ", gasneti_formatdata(hptr, brecv));

    bread += brecv;

    sd->hdr         = *((uint8_t  *) (hptr + 0));
    sd->handler_idx = *((uint8_t  *) (hptr + 1));
    sd->numargs     = *((uint16_t *) (hptr + 2));
    sd->paylen      = *((uint32_t *) (hptr + 4));

    if (sd->hdr & AM_SHORT) {
	sd->payptr  = NULL;
	if (sd->numargs > 0) {
	    sd->argptr = (uint32_t *) pptr;
	    brecv = read(fd, pptr, sd->numargs * 4);
	    gasneti_assert(brecv == sd->numargs * 4);
	    bread += brecv;
	}
	else {
	    sd->argptr = NULL;
	}
    }
    else if (sd->hdr & AM_MEDIUM) {
	size_t argpaylen = sd->paylen + sd->numargs*4;
	if (argpaylen > 0) {
	    sd->argptr = (uint32_t *) pptr;
	    sd->payptr = (void *) (pptr + sd->numargs*4);
	    brecv = read(fd, pptr, argpaylen);
	    gasneti_assert(brecv == argpaylen);
	    bread += brecv;
	    //printf("................ => %s\n", gasneti_formatdata(pptr,argpaylen));
	}
	else {
	    sd->argptr = NULL;
	    sd->payptr = NULL;
	}
    }
    else {
	printf("UNKNOWN %s\n", gasneti_formatdata(hptr, AM_HDRLEN));
	gasneti_fatalerror("unknown message received");
    }

    return bread;
}

extern int 
gasnetc_AMRequestShortM( 
	    gasnet_node_t dest,      /* destination node */
            gasnet_handler_t handler, /* index into destination endpoint's handler table */ 
            int numargs, ...) 
{
    int	    retval = 1, *pArg;
    size_t  nbytes;
    va_list argptr;
    void    *buf;
    uint8_t *hdrptr;

    gasneti_assert(numargs >= 0 && numargs <= 16);

    va_start(argptr, numargs); /*  pass in last argument */

    nbytes = numargs*4 + AM_HDRLEN;
    buf = alloca(nbytes);

    hdrptr = (uint8_t *) buf;

    /* Pack args and header in buffer */
    *((uint8_t *) (hdrptr + 0)) =  (uint8_t) (AM_REQUEST | AM_SHORT);
    *((uint8_t *) (hdrptr + 1)) =  (uint8_t) handler;
    *((uint16_t *)(hdrptr + 2)) = (uint16_t) numargs;
    *((uint32_t *)(hdrptr + 4)) = (uint32_t) 0;
    {
	int i;
	pArg = (int32_t *) (hdrptr + AM_HDRLEN);
	for (i = 0; i < numargs; i++)
	    pArg[i] = (int32_t) va_arg(argptr, int);
    }

    if (dest == gasnetc_mynode) {
	gasnetc_sockmap_t smap;
	smap.node = dest;
	smap.fd = -42;
	SOCK_LOCK;
	RUN_HANDLER_SHORT(gasnetc_handlers[handler], (void *) &smap, pArg, numargs);
	SOCK_UNLOCK;
    }
    else {
	SOCK_LOCK;
	gasnetc_writesocket(gasnetc_IdMapFd[dest].fd, buf, nbytes);
	SOCK_UNLOCK;
    }

    va_end(argptr);
    return GASNET_OK;
}

extern int 
gasnetc_AMReplyShortM( 
	    gasnet_token_t token,
            gasnet_handler_t handler, /* index into destination endpoint's handler table */ 
            int numargs, ...) 
{
    size_t  nbytes;
    va_list argptr;
    void    *buf;
    int	    fd, *pArg;
    uint8_t *hdrptr;

    gasnet_node_t   node;

    gasneti_assert(numargs >= 0 && numargs <= 16);

    va_start(argptr, numargs); /*  pass in last argument */

    nbytes = numargs*4 + AM_HDRLEN;
    buf = alloca(nbytes);

    hdrptr = (uint8_t *) buf;

    /* Pack args and header in buffer */
    *((uint8_t *) (hdrptr + 0)) =  (uint8_t) (AM_REPLY | AM_SHORT);
    *((uint8_t *) (hdrptr + 1)) =  (uint8_t) handler;
    *((uint16_t *)(hdrptr + 2)) = (uint16_t) numargs;
    *((uint32_t *)(hdrptr + 4)) = (uint32_t) 0;
    {
	int i;
	pArg = (int32_t *) (hdrptr + AM_HDRLEN);
	for (i = 0; i < numargs; i++)
	    pArg[i] = (int32_t) va_arg(argptr, int);
    }
    
    gasnetc_AMGetMsgSource(token, &node);
    fd = gasnetc_IdMapFd[node].fd;

    if (node == gasnetc_mynode) {
	gasnetc_sockmap_t smap;
	smap.node = node;
	smap.fd = -42;
	RUN_HANDLER_SHORT(gasnetc_handlers[handler], (void *) &smap, pArg, numargs);
    }
    else {
	SOCK_LOCK;
	gasnetc_writesocket(fd, buf, nbytes);
	SOCK_UNLOCK;
    }

    va_end(argptr);
    return GASNET_OK;
}



extern int 
gasnetc_AMRequestMediumM( 
	    gasnet_node_t dest,      /* destination node */
            gasnet_handler_t handler, /* index into destination endpoint's handler table */ 
            void *source_addr, size_t nbytes,   /* data payload */
            int numargs, ...) 
{
    int	    retval = 1, fd, wbytes, totsz;
    va_list argptr;
    void    *buf;
    uint8_t *hdrptr, *payptr;
    int	     payoff;
    int32_t *pArg;

    gasneti_assert(numargs >= 0 && numargs <= 16);

    va_start(argptr, numargs); /*  pass in last argument */
    gasneti_assert(nbytes <= AM_MAXPAYLEN);

    payoff = AM_HDRLEN + 4*numargs;

    buf = gasneti_malloc(AM_BUFSZ);
    if (buf == NULL)
	    abort();

    hdrptr = (uint8_t *) buf;
    payptr = hdrptr + payoff;

    /* Pack args and header in buffer */
    *((uint8_t *) (hdrptr + 0)) =  (uint8_t) (AM_REQUEST | AM_MEDIUM);
    *((uint8_t *) (hdrptr + 1)) =  (uint8_t) handler;
    *((uint16_t *)(hdrptr + 2)) = (uint16_t) numargs;
    *((uint32_t *)(hdrptr + 4)) = (uint32_t) nbytes;
    {
	int i;
	pArg = (int32_t *) (hdrptr + AM_HDRLEN);
	for (i = 0; i < numargs; i++)
	    pArg[i] = (int32_t) va_arg(argptr, int);
    }

    if (dest == gasnetc_mynode) {
	gasnetc_sockmap_t smap;
	smap.node = dest;
	smap.fd = -42;
	SOCK_LOCK;
	RUN_HANDLER_MEDLONG(gasnetc_handlers[handler], (void *) &smap, 
			    pArg, numargs, source_addr, nbytes);
	SOCK_UNLOCK;
    }
    else {
	/* Copy payload */
	memcpy(payptr, source_addr, nbytes);
	fd = gasnetc_IdMapFd[dest].fd;

	//printf("%d> ************ %d => %s\n", gasnetc_mynode, dest, gasneti_formatdata(source_addr,nbytes));
	SOCK_LOCK;
	gasnetc_writesocket(fd, buf, nbytes + payoff);
	SOCK_UNLOCK;
    }

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
    int	    payoff;
    int32_t *pArg;

    gasnet_node_t   node;

    gasneti_assert(numargs >= 0 && numargs <= 16);

    va_start(argptr, numargs); /*  pass in last argument */
    gasneti_assert(nbytes <= AM_MAXPAYLEN);

    payoff = AM_HDRLEN + 4*numargs;

    buf = gasneti_malloc(AM_BUFSZ);
    if (buf == NULL)
	    abort();

    hdrptr = (uint8_t *) buf;
    payptr = hdrptr + payoff;

    /* Pack args and header in buffer */
    *((uint8_t *) (hdrptr + 0)) =  (uint8_t) (AM_REPLY | AM_MEDIUM);
    *((uint8_t *) (hdrptr + 1)) =  (uint8_t) handler;
    *((uint16_t *)(hdrptr + 2)) = (uint16_t) numargs;
    *((uint32_t *)(hdrptr + 4)) = (uint32_t) nbytes;
    {
	int i;
	pArg = (int32_t *) (hdrptr + 8);
	for (i = 0; i < numargs; i++)
	    pArg[i] = (int32_t) va_arg(argptr, int);
    }

    gasnetc_AMGetMsgSource(token, &node);

    if (node == gasnetc_mynode) {
	gasnetc_sockmap_t smap;
	smap.node = node;
	smap.fd = -42;
	RUN_HANDLER_MEDLONG(gasnetc_handlers[handler], (void *) &smap, 
			    pArg, numargs, source_addr, nbytes);
    }
    else {
	fd = gasnetc_IdMapFd[node].fd;

	/* Copy payload */
	memcpy(payptr, source_addr, nbytes);

	gasnetc_writesocket(fd, buf, nbytes + payoff);
    }

    /* On write completion, free the buffer */
    gasneti_free(buf);

    va_end(argptr);
    return GASNET_OK;
}

/* reference implementation of barrier */
#define GASNETE_HANDLER_BASE  64 /* reserve 64-127 for the extended API */
#define _hidx_gasnete_am_medping			(GASNETE_HANDLER_BASE+0)
#define _hidx_gasnete_am_medpong			(GASNETE_HANDLER_BASE+1)
#define _hidx_gasnete_am_exchange			(GASNETE_HANDLER_BASE+2)

#define _hidx_gasnete_ambarrier_notify_reqh	        (GASNETE_HANDLER_BASE+3) 
#define _hidx_gasnete_ambarrier_done_reqh		(GASNETE_HANDLER_BASE+4)

#define GASNETI_GASNET_EXTENDED_REFBARRIER_C 1
#define gasnete_refbarrier_notify  gasnete_extref_barrier_notify
#define gasnete_refbarrier_wait    gasnete_extref_barrier_wait
#define gasnete_refbarrier_try     gasnete_extref_barrier_try
  
#include "gasnet_extended_refbarrier.c"
#undef GASNETI_GASNET_EXTENDED_REFBARRIER_C


#if 0
#define MAX_BUFS    256
int	 gasnetc_AMBufsIdx  = 0;
int	 gasnetc_AMBufsFree = 0;
uint8_t	*gasnetc_AMBufs[MAX_BUFS];

static char gasnetc_am_recvbuf[AM_BUFSZ];
#endif

extern int 
gasnetc_AMPoll()
{
    int	      ret, i;
    size_t    rbytes;
    void     *buf;

    gasnetc_sockmap_t  smap;

    if (gasnetc_nodes == 1)
	    return;

    SOCK_LOCK;

    ret = poll(gasnetc_pollfds, gasnetc_nodes-1, 0);

    if (ret == -1) {
	gasneti_fatalerror("poll() failed");
    }
    else if (ret == 0) {
	goto unlock_ret;
    }

    for (i=0; i < gasnetc_nodes-1; i++) {

	if (gasnetc_pollfds[i].revents & (POLLERR|POLLHUP|POLLNVAL)) 
	    gasneti_fatalerror("error polling fd %d", i);
	else if (gasnetc_pollfds[i].revents & POLLIN) {
	    gasnetc_sockdata_t sd;
	    char *rbuf = alloca(AM_BUFSZ);

	    smap.node = gasnetc_PollMapNode[i].node;
	    smap.fd   = gasnetc_pollfds[i].fd;

	    memset(&sd, 0, sizeof(sd));

	    gasneti_assert(smap.fd != -42);
	    rbytes = gasnetc_readsocket(smap.fd, rbuf, AM_BUFSZ, &sd);

	    if (rbytes == 0)
		continue;

	    if (sd.hdr & AM_SHORT) {
		RUN_HANDLER_SHORT(gasnetc_handlers[sd.handler_idx], 
				  (void *) &smap, sd.argptr, sd.numargs);
	    }
	    else {
                RUN_HANDLER_MEDLONG(gasnetc_handlers[sd.handler_idx], 
			(void *) &smap, sd.argptr, sd.numargs, sd.payptr, sd.paylen);
	    }
	}
    }

unlock_ret:
    SOCK_UNLOCK;

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

  uintptr_t	segsize;

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
    struct sockaddr_in temp;
    sin_size = sizeof(struct sockaddr_in);

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
	perror("connect to other");
	sleep(1);
	if (connect(gasnetc_sockfds[i], (struct sockaddr *)&ina[i],
		sizeof(struct sockaddr)) == -1) {
	    perror("connect to other try 2");
	    exit(1);
        }
    }

    
    fprintf(stderr, "%d> connection succeeded on port %d, id=%d\n", 
		    gasnetc_mynode,
		    (int) ntohs(ina_local[i].sin_port),
		    gasnetc_sockfds[i]);
    
  }
  //setup the poll list
  //since we will not poll local host we only have total-nodes -1 on the poll list
  //and thus we use seperate counters for the poll list index (kinda of a hack but works

  for(j=0, i=0; i<gasnetc_nodes; i++) {
    if(i!=gasnetc_mynode) {
      //setnonblock(gasnetc_sockfds[i]);
      gasnetc_pollfds[j].fd = gasnetc_sockfds[i];
      gasnetc_pollfds[j].events = POLLIN | POLLERR | POLLHUP | POLLNVAL;
      gasnetc_PollMapNode[j].node = i;
      gasnetc_PollMapNode[j].fd = gasnetc_sockfds[i];
      j++;
    }

    gasnetc_IdMapFd[i].node = i;
    gasnetc_IdMapFd[i].fd   = gasnetc_sockfds[i];
  }

  gasneti_trace_init();

  GASNETC_NODE_BARRIER;

  gasneti_segmentInit(
	&gasnetc_MaxLocalSegmentSize, &gasnetc_MaxGlobalSegmentSize,
	(uintptr_t) -1, gasnetc_nodes, &gasnetc_bootstrapExchange);

  GASNETC_NODE_BARRIER;

  printf("local = %ld and global = %ld\n",
		    gasnetc_MaxLocalSegmentSize, gasnetc_MaxGlobalSegmentSize);

  {
	char *env = getenv("GASNET_SEGMENT_SIZE");

	if (env == NULL || *env == '\0')
	    gasneti_fatalerror("GASNET_SEGMENT_SIZE must be set");

	segsize = atol(env);
  }

  if (gasnetc_MaxGlobalSegmentSize < segsize)
	gasneti_fatalerror(
	    "Largest segment (%ld) is less than %d",
	    gasnetc_MaxGlobalSegmentSize, segsize);

  /* assume 100 MB of physical memory per thread */
  firehose_init(100*1024*1024, 0, NULL, 0, &gasnetc_firehose_info);
  

  gasneti_segmentAttach(
	segsize, 250*1024*1024, 
	gasnetc_seginfo, &gasnetc_bootstrapExchange);

  GASNETC_NODE_BARRIER;

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
    firehose_fini();
}

extern void
gasnetc_hsl_lock(gasnet_hsl_t *hsl)
{
    return;
}

extern void
gasnetc_hsl_unlock(gasnet_hsl_t *hsl)
{
    return;
}

static	volatile int gasnete_medping_count[GASNETE_MAXTHREADS] = { 0 };

void
gasnete_am_medping(gasnet_token_t token, void *p, size_t len, 
		   gasnet_handlerarg_t tid)
{
    gasnet_node_t   node;

    gasnetc_AMGetMsgSource(token, &node);

    //fprintf(stderr, "%d> Received Ping from thread %d on node %d\n", gasnetc_mynode, tid, node);

    gasnet_AMReplyMedium1(token, gasneti_handleridx(gasnete_am_medpong),
	    p, len, tid);
}

void
gasnete_am_medpong(gasnet_token_t token, void *p, size_t len, gasnet_handlerarg_t tid)
{
    gasnet_node_t   node;

    gasnetc_AMGetMsgSource(token, &node);

    fprintf(stderr, "%d> Received Pong from thread %d on node %d\n", gasnetc_mynode, tid, node);

    gasnete_medping_count[tid]++;
}

#define GASNETE_EXCHANGE_MASTER	(gasnetc_nodes-1)

static int  gasnete_exch_count = 0;
static int  gasnete_exch_phase = 1;
static char gasnete_exch_buf[4096][2];

void
gasnete_am_exchange(gasnet_token_t token, void *p, size_t len)
{
    gasnet_node_t   node;
    uint8_t	    *dst = (uint8_t *) gasnete_exch_buf;

    gasnetc_AMGetMsgSource(token, &node);

    if (gasnete_mynode == GASNETE_EXCHANGE_MASTER) {
	memcpy(dst + node *len, p, len);
    }
    else {
	memcpy(dst, p, len);
        //printf("%d> exch from %d: %s\n", gasnetc_mynode, node, gasneti_formatdata(p, len));
    }

    gasnete_exch_count++;
}

void 
gasnetc_bootstrapExchange(void *src, size_t len, void *dest)
{
    int	i;

    if (len*gasnete_nodes > 4096)
	gasneti_fatalerror("exch buf too small");

    if (gasnete_exch_count != 0)
        gasneti_fatalerror("only one outstanding exch allowed");

    gasnet_AMRequestMedium0(GASNETE_EXCHANGE_MASTER,
	gasneti_handleridx(gasnete_am_exchange), src, len);

    if (gasnetc_mynode == GASNETE_EXCHANGE_MASTER) {

	while (gasnete_exch_count != gasnete_nodes)
		gasnetc_AMPoll();

	for (i = 0; i < gasnetc_nodes; i++) {
	    if (i != GASNETE_EXCHANGE_MASTER) {
		gasnet_AMRequestMedium0(i,
		    gasneti_handleridx(gasnete_am_exchange), 
		    gasnete_exch_buf, gasnetc_nodes*len);
	    }
	}
    }
    else {
	while (gasnete_exch_count != 1)
		gasnetc_AMPoll();
    }

    memcpy(dest, gasnete_exch_buf, len*gasnete_nodes);

    gasnete_exch_count = 0;

    return;
}


static gasnet_handlerentry_t const gasnete_ref_handlers[] = {

  /* ptr-width independent handlers */
  gasneti_handler_tableentry_no_bits(gasnete_am_medping),
  gasneti_handler_tableentry_no_bits(gasnete_am_medpong),
  gasneti_handler_tableentry_no_bits(gasnete_am_exchange),

  /* ptr-width dependent handlers */

  #ifdef GASNETE_REFBARRIER_HANDLERS
    GASNETE_REFBARRIER_HANDLERS(),
  #endif
  { 0, NULL }
};

void *
user_main(void *arg)
{
    int	    iter = 1;
    char    buf[128];

    gasnet_node_t   dest = gasnetc_mynode + 1 < gasnetc_nodes 
			    ? gasnetc_mynode + 1 : 0;

    gasnet_AMRequestMedium1(dest,
	gasneti_handleridx(gasnete_am_medping), buf, 128, 0);

    while (gasnete_medping_count[0] != 1)
	gasnetc_AMPoll();
}

struct threadarg {
    int	tid;
    int	tid_local;
    int	node;
};

void *
user_threadmain_pingpong(void *arg)
{
    char    buf[128];
    int	    local_tid = (int) arg;
    int	    global_tid = local_tid + gasnete_mynode*gasnetc_threadspernode;
    int	    peer_node = gasnete_mynode+1 < gasnete_nodes ? gasnete_mynode+1 : 0;
    int	    peer_tid = peer_node + local_tid;

    gasnet_AMRequestMedium1(peer_node,
	gasneti_handleridx(gasnete_am_medping), buf, 128, local_tid);

    while (gasnete_medping_count[local_tid] != 1)
	gasnetc_AMPoll();

    gasnete_medping_count[local_tid] = 0;

    gasnetc_barrier();

    printf("I am local tid %d out of %d\n", local_tid, global_tid);

    gasnet_AMRequestMedium1(peer_node,
	gasneti_handleridx(gasnete_am_medping), buf, 128, local_tid);

    while (gasnete_medping_count[local_tid] != 1)
	gasnetc_AMPoll();

    gasnete_medping_count[local_tid] = 0;
}

static char    gasnetc_temp_putbuf[256];

extern void (*work_threads[])(int);

void *
user_threadmain(void *arg)
{
    int	    local_tid = (int) arg;
    int	    global_tid = local_tid + gasnete_mynode*gasnetc_threadspernode;
    int	    peer_node = gasnete_mynode+1 < gasnete_nodes ? gasnete_mynode+1 : 0;
    int	    peer_tid = peer_node*gasnetc_threadspernode + local_tid;
    void    (*wt)(int) = work_threads[global_tid];

#if 0
    void *remote_addr = gasnetc_seginfo[peer_node].addr;

    printf("%2d> put (%p,%d) -> to thread %d on node %d (%p)\n",
	    global_tid, gasnetc_temp_putbuf, 2048, peer_tid, peer_node, remote_addr);

    gasneti_assert(peer_node != gasnete_mynode);

    gasnete_put_node(peer_tid, remote_addr, gasnetc_temp_putbuf, 2048, global_tid);

    gasnetc_barrier();
#endif

    printf("%d> starting worker thread id is %d\n", global_tid, GASNETI_THREADIDQUERY() );
    wt(global_tid);
}

static pthread_mutex_t	gasnetc_barrier_mutex = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t	gasnetc_barrier_cond = PTHREAD_COND_INITIALIZER;
static int	gasnetc_barrier_count;
static int	gasnetc_barrier_threads;

void
gasnetc_barrier() 
{
	pthread_mutex_lock(&gasnetc_barrier_mutex);
	gasnetc_barrier_count++;
	if (gasnetc_barrier_count < gasnetc_barrier_threads)
		pthread_cond_wait(&gasnetc_barrier_cond, &gasnetc_barrier_mutex);
	else { /* single thread does the barrier */
		GASNETC_NODE_BARRIER;
		gasnetc_barrier_count = 0;
		pthread_cond_broadcast(&gasnetc_barrier_cond);
	}
	pthread_mutex_unlock(&gasnetc_barrier_mutex);
}

/*
 * Fake alpha-beta model, where a single message takes
 *
 * alpha + beta*bytes
 *
 * Myrinet: alpha= 7.7us, beta= 0.005us
 * Quadrics: 
 */

typedef
struct gasnete_op {
    const firehose_request_t	*req_local;
    const firehose_request_t	req_remote;

    int		    tid;
    gasnet_node_t   node;

    void    *dest;
    void    *src;
    size_t  nbytes;
}
gasnete_op_t;

void
gasnete_fh_request_put(void *_op, const firehose_request_t *req, int allLocalHit)
{
    gasnete_op_t    *op = (gasnete_op_t *) _op;
    const firehose_request_t	*fhreqs[2];

    GASNETI_TRACE_PRINTF(C, 
	("Firehose put (%p): (%d,%p) <- %p (%d bytes)", 
	op, (unsigned) op->req_remote.node, op->dest, op->src, op->nbytes));

    if (allLocalHit)
	GASNETI_TRACE_EVENT(C, FIREHOSE_REMOTE_HITS);
    else
	GASNETI_TRACE_EVENT(C, FIREHOSE_REMOTE_MISSES);

    fhreqs[0] = &(op->req_remote);
    fhreqs[1] = op->req_local;

    firehose_release(fhreqs, 2);

    gasneti_free(op);
}

void
gasnete_fh_request_get(void *_op, const firehose_request_t *req, int allLocalHit)
{
    gasnete_op_t    *op = (gasnete_op_t *) _op;
    const firehose_request_t	*fhreqs[2];

    GASNETI_TRACE_PRINTF(C, 
	("Firehose get (%p): (%d,%p) <- %p (%d bytes)", 
	op, (unsigned) op->req_remote.node, op->dest, op->src, op->nbytes));

    fhreqs[0] = &(op->req_remote);
    fhreqs[1] = op->req_local;

    firehose_release(fhreqs, 2);

    gasneti_free(op);
}

void
gasnete_get_node(void *dest, int rem_tid, void *src, size_t nbytes, int tid)
{
    gasnet_node_t   node = gasnetc_ThreadMapNode[rem_tid];

    gasnete_op_t    *op;

    if (node != gasnete_mynode) {
	op = gasneti_malloc(sizeof(gasnete_op_t));

	op->src  = src;
	op->dest = dest;
	op->nbytes = nbytes;
	op->tid  = rem_tid;
	op->node = node;

	op->req_local = firehose_local_pin((uintptr_t) src, nbytes, NULL);

	printf("%d> remote_pin at tid=%d,node=%d\n", tid, rem_tid, node);

	firehose_remote_pin(node, (uintptr_t) dest, nbytes,
	    0, (firehose_request_t *) &(op->req_remote), NULL,
	    gasnete_fh_request_get, op);
    }
    return;
}

void
gasnete_put_node(int rem_tid, void *dest, void *src, size_t nbytes, int tid)
{
    gasnet_node_t   node = gasnetc_ThreadMapNode[rem_tid];

    gasnete_op_t    *op;

    if (node != gasnete_mynode) {
	op = gasneti_malloc(sizeof(gasnete_op_t));

	op->src  = src;
	op->dest = dest;
	op->nbytes = nbytes;
	op->tid  = rem_tid;
	op->node = node;

	op->req_local = firehose_local_pin((uintptr_t) src, nbytes, NULL);

	printf("%d> remote_pin at tid=%d,node=%d\n", tid, rem_tid, node);

	firehose_remote_pin(node, (uintptr_t) dest, nbytes,
	    0, (firehose_request_t *) &(op->req_remote), NULL,
	    gasnete_fh_request_put, op);
    }
    return;
}

int
main(int argc, char **argv)
{
    int i, j, tid;

    pthread_t	*pt_tids;

    /*
     * testconduit <mynode> <numnodes> [numthreads]
     */
    if (argc < 3) {
	fprintf(stderr, "%s <mynode> <numnodes> [numthreads]\n", argv[0]);
	exit(EXIT_FAILURE);
    }

    gasnetc_mynode = (gasnet_node_t) atoi(argv[1]);
    gasnetc_nodes  = (gasnet_node_t) atoi(argv[2]);
    gasnete_nodes  = gasnetc_nodes;
    gasnete_mynode = gasnetc_mynode;

    if (argc > 3 && argv[3] != NULL)
	gasnetc_threadspernode = atoi(argv[3]);

    if (gasnetc_nodes < 1) 
	gasneti_fatalerror("Must have at least 1 GASNet node");
    
    if (gasnetc_threadspernode > GASNETE_MAXTHREADS)
	gasneti_fatalerror("Too many local threads: %d", GASNETE_MAXTHREADS);

    /* Feel free lying to gasnet that we've initialized */
    gasneti_init_done = 1;
    gasneti_attach_done = 1;

    pt_tids = (pthread_t *) 
	    gasneti_malloc(sizeof(pthread_t) * gasnetc_threadspernode);
    gasnetc_threads = gasnetc_nodes * gasnetc_threadspernode;

    gasnetc_sockfds = (int *) gasneti_malloc(sizeof(int) * gasnetc_nodes);
    gasnetc_pollfds = (struct pollfd *) 
			gasneti_malloc(sizeof(struct pollfd) * (gasnetc_nodes));
    gasnetc_PollMapNode = (gasnetc_sockmap_t *) 
			gasneti_malloc(
			    sizeof(gasnetc_sockmap_t)*(gasnetc_nodes));
    gasnetc_IdMapFd = (gasnetc_sockmap_t *) 
			gasneti_malloc(
			    sizeof(gasnetc_sockmap_t)*(gasnetc_nodes));
    gasnetc_ThreadMapNode = (int *) gasneti_malloc(sizeof(int) * gasnetc_threads);
    gasnetc_seginfo = (gasnet_seginfo_t *) 
			gasneti_malloc(sizeof(gasnet_seginfo_t) * gasnetc_nodes);
    
    for (i = 0; i < gasnetc_nodes; i++) {
	for (j = 0; j < gasnetc_threadspernode; j++) {
	    tid = i*gasnetc_threadspernode + j;
	    gasnetc_ThreadMapNode[tid] = i;
	}
    }

    /* Initilize firehose handlers */
    {
	gasnet_handlerentry_t *fh_hnds = firehose_get_handlertable();

	int gidx = 1; /* first free gasnet handler is 1 */
	int fidx = 0; /* where to start looking for handlers */

	while (fh_hnds[fidx].fnptr != NULL) {
	    gasnetc_handlers[gidx] = fh_hnds[fidx].fnptr;
	    fh_hnds[fidx].index = gidx;
	    //fprintf(stderr, "registered firehose handler (%p) at idx %d\n",
	    //		    fh_hnds[fidx].fnptr, gidx);
	    gidx++;
	    fidx++;
	}

	fidx = 0;
	gidx = GASNETE_HANDLER_BASE;

	while (gasnete_ref_handlers[fidx].fnptr != NULL) {
	    gasnetc_handlers[gidx] = gasnete_ref_handlers[fidx].fnptr;

	    //fprintf(stderr, "registered ref handler (%p) at idx %d\n",
	    //		gasnete_ref_handlers[fidx].fnptr, gidx);
	    fidx++;
	    gidx++;
	}
    }

    gasnetc_init();

    /* Spawn threads */

    for (i = 0; i < gasnetc_threadspernode; i++) {
	tid = gasnetc_threadspernode*gasnetc_mynode + i;
	pthread_create(&pt_tids[i], NULL, user_threadmain, (void *) i);
    }

    for (i = 0; i < gasnetc_threadspernode; i++)
	pthread_join(pt_tids[i], (void **) NULL);

    GASNETC_NODE_BARRIER;

    gasnetc_finalize();

}


#include <gasnet.h>
#include <firehose.h>

/* No interrupts */
extern void gasnetc_hold_interrupts() { return; }
extern void gasnetc_resume_interrupts() { return; }

gasnet_node_t gasnetc_mynode;
gasnet_node_t gasnetc_nodes;

typedef struct _gasnetc_sockmap {
    gasnet_node_t   node;

    int	     fd;
    char    *hostname;
    int	     port;
} 
gasnetc_sockmap_t;

gasnetc_sockmap_t   *gasnetc_SockMap;

int
gasnetc_AMGetMsgSource(gasnet_token_t token, gasnet_node_t *node)
{
    gasnetc_sockmap_t	*smap = (gasnetc_sockmap_t *) token;

    gasneti_assert(smap != NULL);
    gasneti_assert(smap->node < gasnetc_nodes);
    *node = smap->node;
}

extern int 
gasnetc_AMRequestMediumM( 
	    gasnet_node_t dest,      /* destination node */
            gasnet_handler_t handler, /* index into destination endpoint's handler table */ 
            void *source_addr, size_t nbytes,   /* data payload */
            int numargs, ...) 
{
    int	    retval = 1;
    va_list argptr;
    void    *buf;
    uint8_t *hdrptr, *payptr;

    gasneti_assert(numargs >= 0 && numargs <= gasnet_AMMaxArgs());

    va_start(argptr, numargs); /*  pass in last argument */
    gasneti_assert(nbytes <= AM_MAXLEN);

    buf = malloc(AM_BUFSZ);
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

    /* XXX get remote IP and port from node 'dest' 
    write(socket,...);
    */

    /* On write completion, free the buffer */

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
    uint8_t  *hdrptr, *payptr;

    gasneti_assert(numargs >= 0 && numargs <= gasnet_AMMaxArgs());

    va_start(argptr, numargs); /*  pass in last argument */
    gasneti_assert(nbytes <= AM_MAXLEN);

    buf = malloc(AM_BUFSZ);
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

    /* Copy payload */
    memcpy(payptr, source_addr, nbytes);

    /* write to socket
    write(socket,...);
    */
  
    va_end(argptr);
    return GASNET_OK;
}

extern int 
gasnetc_AMPoll()
{
    uint8_t  *pptr;
    int32_t  *argptr;
    uint16_t  hdr, numargs;
    uint32_t  paylen;
    size_t    nbytes;
    void     *buf, *payptr;

    gasnetc_sockmap_t *smap;

    /*
    select(...)
    smap = 
    */

    pptr = (uint8_t *) buf;

    hdr     = *((uint16_t *) (pptr));
    numargs = *((uint16_t *) (pptr + 2));
    paylen  = *((uint32_t *) (pptr + 4));
    argptr  =   (uint32_t *) (pptr + 8);
    payptr  =       (void *) (pptr + AM_PAYOFF);
    
    assert(nbytes >= AM_PAYOFF);
    assert(paylen == nbytes - AM_PAYOFF);


    RUN_HANDLER(handler_idx, (void *) smap, argptr, numargs, patptr, paylen);

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


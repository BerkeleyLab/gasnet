/* $Id: gasnet_core_misc.c,v 1.26.2.1 2003/01/18 22:07:47 csbell Exp $
 * $Date: 2003/01/18 22:07:47 $
 * $Revision: 1.26.2.1 $
 * Description: GASNet GM conduit Implementation
 * Copyright 2002, Christian Bell <csbell@cs.berkeley.edu>
 * Copyright 2002, Dan Bonachea <bonachea@cs.berkeley.edu>
 */

#include <gasnet_core_internal.h>
/* Required for mmaps */
#include <errno.h>
extern int errno;

#define	_BUFSZ	127
#define BOARD	0

/* mmap_segment_search starts binary search for valid mmaps at len
 * and returns the biggest valid mmap in a seginfo_t
 */
int
gasnetc_mmap_segment_search(gasnet_seginfo_t *segment, size_t len, 
		size_t offset)
{
	size_t	newlen;
	void	*mmap_addr;
	#if GASNETC_MMAP_DEBUG_VERBOSE
	gasneti_stattime_t	t1, t2;
	#endif

	assert(segment != NULL);
	assert(len > 0);
	assert(offset > 0);

	#if GASNETC_MMAP_DEBUG_VERBOSE
	t1 = GASNETI_STATTIME_NOW();
	#endif
	mmap_addr = 
	    mmap(NULL, len, PROT_READ|PROT_WRITE, MAP_ANON|MAP_PRIVATE, -1, 0);
	#if GASNETC_MMAP_DEBUG_VERBOSE
	t2 = GASNETI_STATTIME_NOW();
	#endif
	if (mmap_addr == MAP_FAILED) {
		if (errno != ENOMEM) {
			char str[_BUFSZ+1];
			snprintf(str, _BUFSZ, "unrecognized mmap error: %s", 
			    strerror(errno));
			GASNETI_RETURN_ERRR(RESOURCE, str);
		}
		#if GASNETC_MMAP_DEBUG_VERBOSE
		GASNETC_DPRINTF(("mmap(%d MBytes,off=%d) %dus FAILED: %s\n", 
		   len>>20, offset,
		   (unsigned int) GASNETI_STATTIME_TO_US(t2-t1), 
		   strerror(errno)) );
		#endif
		if (segment->addr > 0)
			offset /= 2;
		else if (offset <= GASNETC_MMAP_GRANULARITY)
			return GASNET_ERR_RESOURCE;
		/* Unless we've already found a working segment, don't narrow
		 * in a smaller segment yet */
		newlen = 
		    GASNETI_PAGE_ROUNDUP(len-offset, GASNETC_SEGMENT_ALIGN);
	}
	else {
		#if GASNETC_MMAP_DEBUG_VERBOSE
		GASNETC_DPRINTF(("mmap(%d MBytes,off=%d) %dus = 0x%x\n", 
		    len>>20, offset,
		    (unsigned int) GASNETI_STATTIME_TO_US(t2-t1), 
		    (uintptr_t) mmap_addr) ); 
		#endif
		segment->addr = mmap_addr;
		segment->size = (uintptr_t) len;
		if (offset <= GASNETC_MMAP_GRANULARITY)
			return GASNET_OK;
		munmap (mmap_addr, len);
		offset /= 2;
		newlen = 
		    GASNETI_PAGE_ROUNDUP(len+offset, GASNETC_SEGMENT_ALIGN);
	}
	return gasnetc_mmap_segment_search(segment, newlen, offset);
}

int
gasnetc_mmap_segment(gasnet_seginfo_t *segment)
{
	void *mmap_addr;

	mmap_addr = 
	    mmap(
	        segment->addr, (uintptr_t) segment->size, 
	        PROT_READ|PROT_WRITE, 
		MAP_ANON|MAP_PRIVATE|MAP_FIXED, 
		-1, 0);

	if (mmap_addr == MAP_FAILED || mmap_addr != segment->addr) {
		char str[_BUFSZ+1];
		snprintf(str, _BUFSZ, "mmap failed: %s", 
		    strerror(errno));
		GASNETI_RETURN_ERRR(RESOURCE, str);
	}
	else
		return GASNET_OK;
}


int
gasnetc_munmap_segment(gasnet_seginfo_t *segment)
{
	assert((size_t) segment->size > 0);

	if (munmap(segment->addr, (size_t) segment->size) == 0)
		return GASNET_OK;
	else {
		char str[_BUFSZ+1];
		snprintf(str, _BUFSZ, "unrecognized munmap error: %s", 
		    strerror(errno));
		GASNETI_RETURN_ERRR(RESOURCE, str);
	}
}

void
gasnetc_sendbuf_init()
{
	int	i, j;
	int	stoks, rtoks;
	int	dma_size;

	assert(_gmc.port != NULL);

	stoks = gm_num_send_tokens(_gmc.port);
	_gmc.stoks.max = stoks;
	_gmc.stoks.hi = _gmc.stoks.lo = _gmc.stoks.total = 0;

	rtoks = gm_num_receive_tokens(_gmc.port);
	_gmc.rtoks.max = rtoks;

	/* We need to allocate the following types of DMA'd buffers:
	 * 1. 1 AMReplyBuf (handling replies after an AMMediumRequest) 
	 * 2. (stoks-1) AMRequest bufs
	 * 3. rtoks AMRequest and AMReply receive bufs
	 * 4. 1 Scratch DMA buf (System Messages)
	 *
	 * Note that each of these have a bufdesc_t attached to them
	 */
	_gmc.bd_list_num = 1 + rtoks + stoks-1 + 1;
	dma_size = _gmc.bd_list_num << GASNETC_AM_SIZE;

	/* Allocate and register DMA buffers */ 
	_gmc.dma_bufs = gm_alloc_pages(dma_size);
	if_pf (_gmc.dma_bufs == NULL)
		gasneti_fatalerror("gm_alloc_pages(%d) %s", dma_size,
		   gasneti_current_loc);
	gm_register_memory(_gmc.port, _gmc.dma_bufs, dma_size);

	/* Allocate the AMRequest send buffer pool stack */
	_gmc.reqs_pool = (int *) 
	    gasneti_malloc(sizeof(int) * (stoks-1));

	/* Allocate a buffer descriptor (bufdesc_t) for each DMA'd buffer
	 * and fill in id/sendbuf for cheap reverse lookups */
	_gmc.bd_ptr = (gasnetc_bufdesc_t *)
	    gasneti_malloc(_gmc.bd_list_num * sizeof(gasnetc_bufdesc_t));
	for (i = 0; i < _gmc.bd_list_num; i++) {
		_gmc.bd_ptr[i].id = i;
		_gmc.bd_ptr[i].sendbuf = (void *)
		    ((uint8_t *) _gmc.dma_bufs + (i<<GASNETC_AM_SIZE));
	}
	/* fifo_max is the last possible fifo index */
	_gmc.reqs_pool_cur = _gmc.reqs_pool_max = stoks-2;

	/* stoks-1 AMRequest send in FIFO */
	for (i = rtoks, j = 0; i < rtoks+stoks-1; i++, j++)
		_gmc.reqs_pool[j] = i;
	assert(j-1 == _gmc.reqs_pool_max);

	/* use the omitted send token for the AMReply buf */
	_gmc.AMReplyBuf = &_gmc.bd_ptr[i]; 
	_gmc.scratchBuf = (void *) ((uint8_t *) _gmc.dma_bufs + 
			    ((++i)<<GASNETC_AM_SIZE));
	_gmc.ReplyCount = 0;
	#if GASNETC_RROBIN_BUFFERS > 1
	_gmc.RRobinCount = 0;
	#endif
}

void
gasnetc_sendbuf_finalize()
{
	if (_gmc.dma_bufs != NULL)
		gm_free_pages(_gmc.dma_bufs, 
		    _gmc.bd_list_num << GASNETC_AM_SIZE);
	if (_gmc.bd_ptr != NULL)
		gasneti_free(_gmc.bd_ptr);
	if (_gmc.reqs_pool != NULL)
		gasneti_free(_gmc.reqs_pool);
}

void
gasnetc_provide_receive_buffers()
{
	int 	i;
	int	rtoks_hi, rtoks_lo;

	/* Extra check to make sure we recovered all our receive
	 * and send * tokens once the system is bootstrapped */
	assert(_gmc.stoks.max == gm_num_send_tokens(_gmc.port));
	assert(_gmc.rtoks.max == gm_num_receive_tokens(_gmc.port));

	_gmc.rtoks.lo = _gmc.rtoks.hi = _gmc.rtoks.total = 0;
	rtoks_lo = _gmc.rtoks.max/2;
	rtoks_hi = _gmc.rtoks.max - rtoks_lo;

	/* Provide GM with rtoks_lo AMRequest receive LOW */
	for (i = 0; i < rtoks_lo; i++) 
		gasnetc_provide_AMRequest_buffer(
		    (void *)((uint8_t *)_gmc.dma_bufs + (i<<GASNETC_AM_SIZE)));
	/* Provide GM with rtoks_hi AMReply receive HIGH */
	for (i = rtoks_lo; i < _gmc.rtoks.max; i++)
		gasnetc_provide_AMReply_buffer(
		    (void *)((uint8_t *)_gmc.dma_bufs + (i<<GASNETC_AM_SIZE)));

	if (gm_set_acceptable_sizes(_gmc.port, GM_HIGH_PRIORITY, 
			1<<GASNETC_AM_SIZE) != GM_SUCCESS)
		gasneti_fatalerror("can't set acceptable sizes for HIGH "
			"priority");
	if (gm_set_acceptable_sizes(_gmc.port, GM_LOW_PRIORITY, 
			1<<GASNETC_AM_SIZE) != GM_SUCCESS)
		gasneti_fatalerror("can't set acceptable sizes for LOW "
			"priority");
	gm_allow_remote_memory_access(_gmc.port);
}


int	
gasnetc_gm_nodes_compare(const void *k1, const void *k2)
{
	gasnetc_gm_nodes_rev_t	*a = (gasnetc_gm_nodes_rev_t *) k1;
	gasnetc_gm_nodes_rev_t	*b = (gasnetc_gm_nodes_rev_t *) k2;

	if (a->id > b->id)
		return 1;
	else if (a->id < b->id)
		return -1;
	else {
		if (a->port > b->port) return 1;
		if (a->port < b->port) return -1;
		else
			return 0;
	}
}

void
gasnetc_tokensend_AMRequest(void *buf, uint32_t len, 
		uint32_t id, uint32_t port,
		gm_send_completion_callback_t callback, 
		void *callback_ptr, uintptr_t dest_addr)
{
	int sent = 0;

	while (!sent) {
		/* don't force locking when polling */
		while (!GASNETC_TOKEN_LO_AVAILABLE())
			gasnetc_AMPoll();

		gasneti_mutex_lock(&gasnetc_lock_gm);
		/* assure last poll was successful */
		if (GASNETC_TOKEN_LO_AVAILABLE()) {
			gasnetc_gm_send_AMRequest(buf, len, id, port, callback,
			    callback_ptr, dest_addr);
			_gmc.stoks.lo += 1;
			_gmc.stoks.total += 1;
			sent = 1;
		}
		gasneti_mutex_unlock(&gasnetc_lock_gm);
	}
}

gasnetc_bufdesc_t *
gasnetc_AMRequestPool_block() 
{
	int			 bufd_idx = -1;
	gasnetc_bufdesc_t	*bufd;

	/* Since every AMRequest send must go through the Pool, use this
	 * as an entry point to make progress in the Receive queue */
	gasnetc_AMPoll();

	while (bufd_idx < 0) {
		while (_gmc.reqs_pool_cur < 0)
			gasnetc_AMPoll();

		gasneti_mutex_lock(&gasnetc_lock_reqpool);
		if_pt (_gmc.reqs_pool_cur >= 0) {
			bufd_idx = _gmc.reqs_pool[_gmc.reqs_pool_cur];
			GASNETI_TRACE_PRINTF(C,
			    ("AMRequestPool (%d/%d) gave bufdesc id %d\n",
	    		    _gmc.reqs_pool_cur, _gmc.reqs_pool_max,
	    		    _gmc.reqs_pool[_gmc.reqs_pool_cur]));
			_gmc.reqs_pool_cur--;
			gasneti_mutex_unlock(&gasnetc_lock_reqpool);
		}
		else 
			gasneti_mutex_unlock(&gasnetc_lock_reqpool);
	}
	assert(bufd_idx < _gmc.bd_list_num);
	assert(_gmc.bd_ptr[bufd_idx].sendbuf != NULL);
	assert(_gmc.bd_ptr[bufd_idx].id == bufd_idx);
	return &_gmc.bd_ptr[bufd_idx];
}

gasnetc_bufdesc_t *
gasnetc_AMRequestPool_nb() 
{
	int			 bufd_idx = -1;
	gasnetc_bufdesc_t	*bufd;

	gasneti_mutex_lock(&gasnetc_lock_reqpool);
	if (_gmc.reqs_pool_cur >= 0) {
		bufd_idx = _gmc.reqs_pool[_gmc.reqs_pool_cur];
		assert(bufd_idx < _gmc.bd_list_num);
		assert(_gmc.bd_ptr[bufd_idx].sendbuf != NULL);
		assert(_gmc.bd_ptr[bufd_idx].id == bufd_idx);
		_gmc.reqs_pool_cur--;
		gasneti_mutex_unlock(&gasnetc_lock_reqpool);
		return &_gmc.bd_ptr[bufd_idx];
	}
	else {
		gasneti_mutex_unlock(&gasnetc_lock_reqpool);
		return NULL;
	}
}

/* This function is not thread safe as it is guarenteed to be called
 * from only one thread, during initialization */
void
gasnetc_SysBarrier()
{
	int		count = 1;
	uintptr_t	*scratchPtr;

	gasneti_mutex_lock(&gasnetc_lock_gm);
	scratchPtr = (uintptr_t *) _gmc.scratchBuf;
	assert(scratchPtr != NULL);
	if (gasnetc_mynode == 0) {
		while (count < gasnetc_nodes) {
			gm_provide_receive_buffer(_gmc.port, 
			    (void *) scratchPtr, GASNETC_SYS_SIZE, 
			    GM_HIGH_PRIORITY);
			if (gasnetc_SysPoll((void *)&count) != BARRIER_GATHER)
				gasneti_fatalerror("System Barrier did not "
				    "receive a BARRIER_GATHER! fatal");
		}
		GASNETC_SYSHEADER_WRITE((uint8_t *)scratchPtr, BARRIER_NOTIFY);
		gasnetc_gm_send_AMSystem_broadcast((void *) scratchPtr, 1,
		    gasnetc_callback_hi, NULL, 0);
	}
	else {
		GASNETC_SYSHEADER_WRITE((uint8_t *)scratchPtr, BARRIER_GATHER);
		while (!gasnetc_token_hi_acquire()) {
			if (gasnetc_SysPoll((void *)-1) != _NO_MSG)
				gasneti_fatalerror("AMSystem_broadcast: "
					"unexpected message while "
					"recuperating tokens");
		}
		gasnetc_gm_send_AMSystem((void *) scratchPtr, 1,
			_gmc.gm_nodes[0].id, _gmc.gm_nodes[0].port, 
			gasnetc_callback_hi, NULL);
		GASNETI_TRACE_PRINTF(C, 
		    ("gasnetc_Sysbarrier: Sent GATHER, waiting for NOTIFY") );
		gm_provide_receive_buffer(_gmc.port, (void *) scratchPtr, 
		    GASNETC_SYS_SIZE, GM_HIGH_PRIORITY);
		if (gasnetc_SysPoll(NULL) != BARRIER_NOTIFY)
			gasneti_fatalerror("expected BARRIER_NOTIFY, fatal");
	}
	gasneti_mutex_unlock(&gasnetc_lock_gm);
	return;
}
			
/* Again, not thread-safe for the same reasons as above. */
void
gasnetc_gm_send_AMSystem_broadcast(void *buf, size_t len,
		gm_send_completion_callback_t callback,
		void *callback_ptr, int recover)
{
	int			i;
	int			token_hi;
	gasnetc_sysmsg_t	sysmsg;

	gasneti_mutex_assertlocked(&gasnetc_lock_gm);
	assert(buf != NULL);
	assert(len >= 1);
	assert(callback != NULL);
	assert(gasnetc_mynode == 0);

	if (recover) 
		token_hi = _gmc.stoks.hi;

	GASNETI_TRACE_PRINTF(C, ("gm_send_AMSystemBroadcast len=%d data=0x%x",
	     len, *((uint8_t *)buf)) );

	for (i = 1; i < gasnetc_nodes; i++) {
		while (!gasnetc_token_hi_acquire()) {
			if (gasnetc_SysPoll((void *)-1) != _NO_MSG)
				gasneti_fatalerror("AMSystem_broadcast: "
				    "unexpected message while "
				    "recuperating tokens");
		}
		gasnetc_gm_send_AMSystem(buf, len, _gmc.gm_nodes[i].id,
		    _gmc.gm_nodes[i].port, callback, callback_ptr);
	}

	if (recover) {
		while (_gmc.stoks.hi != token_hi) {
			if (gasnetc_SysPoll((void *)-1) != _NO_MSG)
				gasneti_fatalerror("AMSystem_broadcast: "
				    "unexpected message while "
				    "recuperating tokens");
		}
	}
	return;
}

/* -------------------------------------------------------------------------- */
/* 'Gather'-type functions
 * The following functions are used mostly during bootstrap and are all
 * two-pass all-to-all operations.  All nodes report a specific value to
 * 0 and wait for the reply.
 */

uintptr_t
gasnetc_gather_MaxSegment(void *segbase, uintptr_t segsize)
{
	uintptr_t		*scratchPtr;
	gasnet_seginfo_t	seginfo; 

	gasneti_mutex_lock(&gasnetc_lock_gm);
	scratchPtr = (uintptr_t *) _gmc.scratchBuf;

	if (gasnetc_mynode == 0) {
		int count = 1;
		uintptr_t segceil = (uintptr_t) segbase + segsize;

		while (count < gasnetc_nodes) {
			gm_provide_receive_buffer(_gmc.port,
			    (void *) scratchPtr, GASNETC_SYS_SIZE, 
			    GM_HIGH_PRIORITY);
			seginfo.addr = NULL;
			if (gasnetc_SysPoll(
			    (void *) &seginfo) != SEGMENT_LOCAL)
				gasneti_fatalerror(
				    "expected SEGMENT_LOCAL, fatal");
			assert(seginfo.addr != NULL);
			if (seginfo.addr > segbase)
				segbase = seginfo.addr;
			if ((uintptr_t) seginfo.addr + seginfo.size < segceil)
				segceil = (uintptr_t)seginfo.addr+seginfo.size;
			count++;
		}

		segceil = 
		    (uintptr_t) GASNETI_PAGE_ALIGN(segceil, GASNETC_PAGE_SIZE);
		segsize = segceil - (uintptr_t)segbase;
		if (segceil < (uintptr_t) segbase)
			segsize = 0;
		GASNETI_TRACE_PRINTF(C, ("MaxGlobalSegmentSize = %d at 0x%x\n", 
		    segsize, (uintptr_t)segbase) );
		GASNETC_SYSHEADER_WRITE((uint8_t *)scratchPtr, 
		    (uint8_t) SEGMENT_GLOBAL);
		scratchPtr[1] = (uintptr_t) segbase;
		scratchPtr[2] = segsize;
		gasnetc_gm_send_AMSystem_broadcast((void *) scratchPtr, 
		    3*sizeof(uintptr_t), gasnetc_callback_hi, 
		    NULL, 0);
	}
	else {
		GASNETC_SYSHEADER_WRITE((uint8_t *)scratchPtr, 
		    (uint8_t) SEGMENT_LOCAL);
		scratchPtr[1] = (uintptr_t) segbase;
		scratchPtr[2] = segsize;

		while (!gasnetc_token_hi_acquire()) {
			if (gasnetc_SysPoll((void *)-1) != _NO_MSG)
				gasneti_fatalerror("AMSystem_broadcast: "
				    "unexpected message while "
				    "recuperating tokens");
		}
		gasnetc_gm_send_AMSystem((void *) scratchPtr, 
		    3*sizeof(uintptr_t), _gmc.gm_nodes[0].id, 
		    _gmc.gm_nodes[0].port, gasnetc_callback_hi, NULL);

		gm_provide_receive_buffer(_gmc.port, (void *) scratchPtr, 
		    GASNETC_SYS_SIZE, GM_HIGH_PRIORITY);
		seginfo.addr = NULL;
		if (gasnetc_SysPoll((void *) &seginfo) != SEGMENT_GLOBAL)
			gasneti_fatalerror("expected SEGMENT_GLOBAL, fatal");
		assert(seginfo.addr != NULL);
		segbase = seginfo.addr;
		segsize = seginfo.size;
		GASNETI_TRACE_PRINTF(C, ("MaxGlobalSegmentSize = %d at 0x%x\n", 
		    segsize, (uintptr_t)segbase) );
	}
	gasneti_mutex_unlock(&gasnetc_lock_gm);
	return segsize;
}

int
gasnetc_gather_seginfo(gasnet_seginfo_t *seginfo)
{

	uintptr_t	*scratchPtr;

	assert(seginfo != NULL);

	gasneti_mutex_lock(&gasnetc_lock_gm);
	scratchPtr = (uintptr_t *) _gmc.scratchBuf;
	if (gasnetc_mynode == 0) {
		int count = 1, i;

		while (count < gasnetc_nodes) {
			gm_provide_receive_buffer(_gmc.port,
			    (void *) scratchPtr, GASNETC_SYS_SIZE, 
			    GM_HIGH_PRIORITY);
			if (gasnetc_SysPoll(
			    (void *) seginfo) != SEGINFO_GATHER)
				gasneti_fatalerror(
				    "expected SEGINFO_GATHER, fatal");
			count++;
		}
		GASNETC_SYSHEADER_WRITE((uint8_t *)scratchPtr, 
		    (uint8_t) SEGINFO_BROADCAST);
		for (i = 0; i < gasnetc_nodes; i++)
			scratchPtr[i+1] = (uintptr_t) seginfo[i].size;
		gasnetc_gm_send_AMSystem_broadcast((void *) scratchPtr, 
		    (gasnetc_nodes+1)*sizeof(uintptr_t), 
		    gasnetc_callback_hi, NULL, 0);
	}
	else {
		GASNETC_SYSHEADER_WRITE((uint8_t *)scratchPtr, 
		    (uint8_t) SEGINFO_GATHER);
		scratchPtr[1] = (uintptr_t) seginfo[gasnetc_mynode].size;

		while (!gasnetc_token_hi_acquire()) {
			if (gasnetc_SysPoll((void *)-1) != _NO_MSG)
				gasneti_fatalerror("AMSystem_broadcast: "
				    "unexpected message while "
				    "recuperating tokens");
		}
		gasnetc_gm_send_AMSystem((void *) scratchPtr, 
		    2*sizeof(uintptr_t), _gmc.gm_nodes[0].id, 
		    _gmc.gm_nodes[0].port, gasnetc_callback_hi, NULL);

		gm_provide_receive_buffer(_gmc.port, (void *) scratchPtr, 
		    GASNETC_SYS_SIZE, GM_HIGH_PRIORITY);
		if (gasnetc_SysPoll((void *) seginfo) != SEGINFO_BROADCAST)
			gasneti_fatalerror("expected SEGINFO_BROADCAST, fatal");
	}
	gasneti_mutex_unlock(&gasnetc_lock_gm);
	return GASNET_OK;
}

uintptr_t
gasnetc_segment_sbrk(uintptr_t sbrk_local_aligned)
{
	uintptr_t	*scratchPtr;
	uintptr_t	sbrk_global;
	size_t		pagesize;

	gasneti_mutex_lock(&gasnetc_lock_gm);
	scratchPtr = (uintptr_t *) _gmc.scratchBuf;
	pagesize = GASNET_PAGESIZE;

	assert(sbrk_local_aligned % pagesize == 0);
	assert(_gmc.gm_nodes[0].id > 0);

	if (gasnetc_mynode == 0) {
		int count = 1;
		uintptr_t sbrk_high = sbrk_local_aligned;
		while (count < gasnetc_nodes) {
			gm_provide_receive_buffer(_gmc.port, 
			    (void *) scratchPtr, GASNETC_SYS_SIZE, 
			    GM_HIGH_PRIORITY);
			if (gasnetc_SysPoll((void *) &sbrk_global) != SBRK_TOP)
				gasneti_fatalerror("expected SBRK_TOP, fatal");
			if (sbrk_global > sbrk_high)
				sbrk_high = sbrk_global;
		}
		GASNETI_TRACE_PRINTF(C, ("SBRK HIGH = 0x%x\n", sbrk_high) );
		GASNETC_SYSHEADER_WRITE((uint8_t *)scratchPtr, 
		    (uint8_t) SBRK_BASE);
		scratchPtr[1] = sbrk_high;
		gasnetc_gm_send_AMSystem_broadcast((void *) scratchPtr, 
		    2*sizeof(uintptr_t), gasnetc_callback_hi, 
		    NULL, 0);
		gasneti_mutex_unlock(&gasnetc_lock_gm);
		return sbrk_high;
	}
	else {
		GASNETC_SYSHEADER_WRITE((uint8_t *)scratchPtr, 
		    (uint8_t) SBRK_TOP);
		scratchPtr[1] = sbrk_local_aligned;

		while (!gasnetc_token_hi_acquire()) {
			if (gasnetc_SysPoll((void *)-1) != _NO_MSG)
				gasneti_fatalerror("AMSystem_broadcast: "
				    "unexpected message while "
				    "recuperating tokens");
		}
		gasnetc_gm_send_AMSystem((void *) scratchPtr, 
		    2*sizeof(uintptr_t), _gmc.gm_nodes[0].id, 
		    _gmc.gm_nodes[0].port, gasnetc_callback_hi, NULL);

		gm_provide_receive_buffer(_gmc.port, (void *) scratchPtr, 
		    GASNETC_SYS_SIZE, GM_HIGH_PRIORITY);
		if (gasnetc_SysPoll((void *) &sbrk_global) != SBRK_BASE)
			gasneti_fatalerror("expected SBRK_BASE, fatal");
		GASNETI_TRACE_PRINTF(C, ("SBRK HIGH = 0x%x\n", sbrk_global) );
		gasneti_mutex_unlock(&gasnetc_lock_gm);
		return sbrk_global;
	}
}

int
gasnetc_gmpiconf_init()
{

	FILE		*fp;
	char		line[_BUFSZ+1];
	char		gmconf[_BUFSZ+1], *gmconfenv;
	char		gmhost[_BUFSZ+1], hostname[MAXHOSTNAMELEN+1];
	char		**hostnames;
	char		*homedir;
	int		lnum = 0, gmportnum, i;
	int		thisport = 0, thisid = 0, numnodes = 0, thisnode = -1;
	gm_status_t	status;
	struct gm_port	*p;

	if ((homedir = getenv("HOME")) == NULL)
		GASNETI_RETURN_ERRR(RESOURCE, "Couldn't find $HOME directory");

	if ((gmconfenv = getenv("GMPI_CONF")) != NULL)
		snprintf(gmconf, _BUFSZ, "%s", gmconfenv);
	else
		snprintf(gmconf, _BUFSZ, "%s/.gmpi/conf", homedir);

	if (gethostname(hostname,_BUFSZ) < 0)
		GASNETI_RETURN_ERRR(RESOURCE, "Couldn't get local hostname");

	if ((fp = fopen(gmconf, "r")) == NULL) {
		fprintf(stderr, "Couldn't open GMPI configuration file\n: %s", 
		    gmconf);
		return GASNET_ERR_RESOURCE;
	}

	/* must do gm_init() from this point on since gm_host_name_to_node_id
	 * must use the port
	 */

	while (fgets(line, _BUFSZ, fp)) {
	
		if (lnum == 0) {
	      		if ((sscanf(line, "%d\n", &numnodes)) < 1) 
				GASNETI_RETURN_ERRR(RESOURCE, 
				    "job size not found in GMPI config file");
	      		else if (numnodes < 1) 
				GASNETI_RETURN_ERRR(RESOURCE, 
				    "invalid numnodes in GMPI config file");

			_gmc.gm_nodes = (gasnetc_gm_nodes_t *) 
			    gasneti_malloc(numnodes*sizeof(gasnetc_gm_nodes_t));

			_gmc.gm_nodes_rev = (gasnetc_gm_nodes_rev_t *) 
			    gasneti_malloc(numnodes *
			    sizeof(gasnetc_gm_nodes_rev_t));

			hostnames = (char **)
			    gasneti_malloc((numnodes+1)*sizeof(char *));
			hostnames[numnodes] = NULL;
			for (i = 0; i < numnodes; i++) {
				hostnames[i] =
				gasneti_malloc(MAXHOSTNAMELEN);
			}
			lnum++;
	      	}

		else if (lnum <= numnodes) {
			if ((sscanf(line,"%s %d\n",gmhost,&gmportnum)) == 2) {
				if (gmportnum < 1 || gmportnum > 7)
					GASNETI_RETURN_ERRR(RESOURCE, 
					    "Invalid GM port");

				assert(gmhost != NULL);

				_gmc.gm_nodes[lnum-1].port = gmportnum;
				memcpy(&hostnames[lnum-1][0], 
				    (void *)gmhost, MAXHOSTNAMELEN);

				if (strcasecmp(gmhost, hostname) == 0) {
					GASNETI_TRACE_PRINTF(C,
					    ("%s will bind to port %d\n", 
					    hostname, gmportnum) );
					thisnode = lnum-1;
					thisport = gmportnum;
				}
			}
                        else {
				printf("couldn't parse: %s\n", line);
			}
			lnum++;
		}
	}
	
	fclose(fp);

	if (numnodes == 0 || thisnode == -1)
		GASNETI_RETURN_ERRR(RESOURCE, 
		    "could not find myself in GMPI config file");
	gm_init();
	status = 
		gm_open(&p,BOARD,thisport,"GASNet/GM", 
		    GM_API_VERSION_1_4);
	if (status != GM_SUCCESS) 
		GASNETI_RETURN_ERRR(RESOURCE, "could not open GM port");
	status = gm_get_node_id(p, (unsigned int *) &thisid);
	if (status != GM_SUCCESS)
		GASNETI_RETURN_ERRR(RESOURCE, "could not get GM node id");

	for (i = 0; i < numnodes; i++) {
		_gmc.gm_nodes[i].id = 
		    gm_host_name_to_node_id(p, hostnames[i]);

		if (_gmc.gm_nodes[i].id == GM_NO_SUCH_NODE_ID) {
			fprintf(stderr, "%d has no id! Check mapper\n",
			    _gmc.gm_nodes[i].id);
			GASNETI_RETURN_ERRR(RESOURCE, 
			    "Unknown GMid or GM mapper down");
		}
		_gmc.gm_nodes_rev[i].id = _gmc.gm_nodes[i].id;
		_gmc.gm_nodes_rev[i].port = _gmc.gm_nodes[i].port;
		_gmc.gm_nodes_rev[i].node = (gasnet_node_t) i;

		GASNETI_TRACE_PRINTF(C, ("%d> %s (gm %d, port %d)\n", 
		    i, hostnames[i], _gmc.gm_nodes[i].id, 
		    _gmc.gm_nodes[i].port));

	}

	gasnetc_mynode = thisnode;
	gasnetc_nodes = numnodes;
	for (i = 0; i < numnodes; i++)
		gasneti_free_inhandler(hostnames[i]);
	gasneti_free_inhandler(hostnames);

	/* sort out the gm_nodes_rev for bsearch, glibc qsort uses recursion,
	 * so stack memory in order to complete the sort.  We want to minimize
	 * the number of mallocs
	 */
	qsort(_gmc.gm_nodes_rev, numnodes, sizeof(gasnetc_gm_nodes_rev_t),
	    gasnetc_gm_nodes_compare);
	_gmc.port = p;
	return GASNET_OK;
}

#ifdef LINUX
uintptr_t
gasnetc_get_physmem()
{
	FILE		*fp;
	char		line[_BUFSZ+1];
	unsigned long	mem = 0;

	if ((fp = fopen("/proc/meminfo", "r")) == NULL)
		gasneti_fatalerror("Can't open /proc/meminfo");

	while (fgets(line, _BUFSZ, fp)) {
		if (sscanf(line, "Mem: %ld", &mem) > 0)
			break;
	}
	fclose(fp);
	return (uintptr_t) mem;
}
#elif defined(FREEBSD)
#include <sys/types.h>
#include <sys/sysctl.h>
uintptr_t
gasnetc_get_physmem()
{
	uintptr_t	mem = 0;
	size_t		len = sizeof(uintptr_t);

	if (sysctlbyname("hw.physmem", &mem, &len, NULL, NULL))
		gasneti_fatalerror("couldn't query systcl(hw.physmem");
	return mem;
}
#else
uintptr_t
gasnetc_get_physmem()
{
	return (uintptr_t) 0;
}
#endif

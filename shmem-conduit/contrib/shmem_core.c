#include <shmem_tests.h>

//#define DEBUG

//#define RANDOM_TICKET

#define MAX_SLOTS   512
#define NUM_SLOTS   (512/sizeof(int))

#define USED_SLOTS  1	    /* this value will change at runtime */

#ifdef DEBUG
#define DPRINTF(x)  printf x
#else
#define DPRINTF(x)
#endif

#define Q_DEPTH	32
#define Q_MASK	(Q_DEPTH-1)

/* Reqrep queue */
#define Q_FREE    0
#define Q_USED    1
#define Q_DONE    2

/*
 * Bogus prepare message MACRO
 */
#define PREPARE_MSG(msg)	    \
	do {			    \
	    (msg).type = 0;	    \
	    (msg).reqrep = 1;	    \
	    (msg).handler_idx = 13; \
	    (msg).args[0] = 0;	    \
	    (msg).args[1] = 0;	    \
	    (msg).args[2] = 0;	    \
	    (msg).args[3] = 0;	    \
	} while (0)

typedef
struct _msg {
	int	type;
	int	numargs;
	int	reqrep;
	int	handler_idx;

	int	args[16];
}
msg_t;

typedef void (*core_fn)(int,int,int);
typedef void (*poll_fn)(int);

static int	mype;
static int	numpes;

static volatile int	handlers_recvd = 0;

static msg_t	msg_q[Q_DEPTH];
static msg_t	small_msg;

static void pollq_cas(int donebit);
static void pollq_cas_bit(int donebit);
static void pollq_ticket(int donebit);
static void pollq_ticket_bit(int donebit);

static void amcore_ticket(int peer_pe, int iters, int donebit);
static void amcore_ticket_bit(int peer_pe, int iters, int donebit);
static void amcore_cas(int peer_pe, int iters, int donebit);
static void amcore_cas_bit(int peer_pe, int iters, int donebit);


struct core_test_data {
	int	donebit;
	int	random_slot;

	poll_fn	poll;
	core_fn	core;
	char	desc[32];
}
core_tests[] = 
    { { 0, 0, pollq_cas, amcore_cas, "CAS/no global done" },
//      { 1, 0, pollq_cas, amcore_cas, "CAS/with global done" },
      { 0, 0, pollq_ticket, amcore_ticket, "Ticket/no global done" },
 //     { 1, 0, pollq_ticket, amcore_ticket, "Ticket/with global done" },
#ifndef SHMEM_NOMSWAP
      { 0, 0, pollq_cas_bit, amcore_cas_bit, "CAS/with bitfield" },
      { 0, 0, pollq_ticket_bit, amcore_ticket_bit, "Ticket/with bitfield" },
#endif
};

#define CORE_TESTS_NUM (sizeof(core_tests)/sizeof(struct core_test_data))

/*
 * Reqrep_q holds index into cas_reqrep_q or ticket_q
 */
static int	reqrep_q_idx = 0;
static int	pollq_skipped = 0;
static int	notemptyq_bit = 0;
static int	ticketq_done[NUM_SLOTS] = { 0 };

/* CAS-based slots */
static int      cas_reqrep_q[Q_DEPTH];

/* 
 * ticket-based slots 
 */
#define CACHE_LINE_SIZE	(128)
typedef
struct _ticket_t {
	int	serve;
	char	_pad0[CACHE_LINE_SIZE-sizeof(int)];

	int	avail;
	int	done;
	char	_pad1[CACHE_LINE_SIZE-2*sizeof(int)];
}
ticket_t;

static	ticket_t    ticketq[Q_DEPTH];
    
static
int64_t 
TimeStamp()
{
        int64_t         retval;
        struct timeval  tv;

        if (gettimeofday(&tv, NULL)) {
                perror("gettimeofday");
                abort();
        }
        retval = ((int64_t)tv.tv_sec) * 1000000 + tv.tv_usec;
        return retval;
}

/*
 * This poll version reads a global bit before it scans.  Its effect should be
 * compared in the full duplex test.
 */

static
void
pollq_cas(int donebit)
{
	int	i, idx = 0;
	int	iters = Q_DEPTH;

	if (notemptyq_bit == 0 || ++pollq_skipped < 1000)
		return;

	pollq_skipped = 0;

	while (1) {

		/*
		 * Consume message
		 */
	
		if (cas_reqrep_q[idx] == Q_DONE) {
			handlers_recvd++;

			DPRINTF(
			    ("%d> id=%d, numargs=%d, reqrep=%d, handler_idx=%d,"
			     " recvd=%d\n", mype, idx, msg_q[idx].numargs, 
			     msg_q[idx].reqrep, msg_q[idx].handler_idx, 
			     handlers_recvd));

			cas_reqrep_q[idx] = Q_FREE;
		}
		
		iters--;

		if (iters == 0) {
			if (donebit)
			    notemptyq_bit = 0;
			break;
		}

		idx = (idx + 1) & Q_MASK;
	}
}

static
void
pollq_cas_bit(int nothing)
{
	int	i = 0, idx = 0;
	int	field;

	for (i = 0; i < USED_SLOTS; i++) {

	    field = shmem_int_mswap(&ticketq_done[i], 0, 0, mype);
	    if (field == 0)
		    continue;
	    idx = ffs(field) - 1;

	    /* 
	     * Process the bitfield using ffs()
	     */

	    while (idx >= 0) {
		    field &= ~(1<<idx);

		    handlers_recvd++;

		    shmem_int_mswap(&ticketq_done[i], (1<<idx), 0, mype);
		    cas_reqrep_q[idx] = Q_FREE;

		    idx = ffs(field) - 1;
	    }
	}
}

/*
 * This poll version is based on the ticket-based approach, and uses the shmem
 * mswap call to query bitfields of used slots
 */

static
void
pollq_ticket_bit(int notused)
{
	int	idx = 0, i = 0;
	int	tickets, done;

	/* Read current bitfield */
	/*
	for (i = 0; i < USED_SLOTS; i++) {
	*/

	    tickets = shmem_int_mswap(&ticketq_done[i], 0, 0, mype);

	    if (tickets == 0)
		    /*
		    continue;
		    */
		    return;

	    idx = ffs(tickets) - 1;

	    /*
	     * Process the bitfield using ffs()
	     */
	    while (idx >= 0) {
		    tickets &= ~(1<<idx);

		    handlers_recvd++;

		    shmem_int_mswap(&ticketq_done[i], (1<<idx), 0, mype);
		    ticketq[idx].serve++;

		    idx = ffs(tickets) - 1;
	    }
	    /*
	}
	*/
}

/*
 * This poll version is based on the ticket-based mechanism
 *
 * Currently doesn't support global bit set.
 */
static
void
pollq_ticket(int donebit)
{
	int	idx = 0;
	int	which;
	int	iters = Q_DEPTH;

	if (notemptyq_bit == 0 || ++pollq_skipped < 1000)
		return;

	pollq_skipped = 0;

	/*
	#ifndef RANDOM_TICKET
	idx = reqrep_q_idx & Q_MASK;
	#endif
	*/

	while (1) {

		/*
		 * Consume message
		 */
	
		if (ticketq[idx].done == Q_DONE) {
			handlers_recvd++;

			ticketq[idx].done = Q_FREE;
			SHMEM_MEMBAR();
			ticketq[idx].serve++;
		}
		iters--;

		if (iters == 0)
			return;

		#if 1 && defined(RANDOM_TICKET)
		idx = (idx + 1) & Q_MASK;
		#else
		idx = (idx - 1) & Q_MASK;
		#endif
	}
}

static
void
amcore_cas(int pe, int iters, int donebit)
{
	int i, curidx, myidx;

	for (i = 0; i < iters; i++) {

		PREPARE_MSG(small_msg);

		#ifdef RANDOM_TICKET
		myidx = ((int) random()) & Q_MASK;
		#elif 1
		curidx = shmem_int_finc(&reqrep_q_idx, pe);
		myidx = curidx & Q_MASK;
		#else
		do {
		    curidx = shmem_int_g(&reqrep_q_idx, pe);
		    myidx = (curidx + 1) & Q_MASK;
		} while (shmem_int_cswap(&reqrep_q_idx, curidx, myidx, pe) != curidx);
		#endif

		while (1) {
		    if (shmem_int_cswap(
			&cas_reqrep_q[myidx], Q_FREE, Q_USED, pe) == Q_FREE)
			    break;

		    pollq_cas(donebit);
		}

		shmem_putmem(&msg_q[myidx], &small_msg, sizeof(small_msg), pe);

		shmem_fence();

		shmem_int_p(&cas_reqrep_q[myidx], Q_DONE, pe);

		if (donebit)
		    shmem_int_p(&notemptyq_bit, 1, pe);
	}
} 

static
void
amcore_cas_bit(int pe, int iters, int donebit)
{
	int i, curidx, myidx;

	for (i = 0; i < iters; i++) {

		PREPARE_MSG(small_msg);

		#ifdef RANDOM_TICKET
		myidx = ((int) random()) & Q_MASK;
		#else
		curidx = shmem_int_finc(&reqrep_q_idx, pe);
		myidx = curidx & Q_MASK;
		#endif

		while (1) {
		    if (shmem_int_cswap(
			&cas_reqrep_q[myidx], Q_FREE, Q_USED, pe) == Q_FREE)
			    break;

		    pollq_cas(donebit);
		}

		shmem_putmem(&msg_q[myidx], &small_msg, sizeof(small_msg), pe);
		shmem_fence();
		shmem_int_mswap(&ticketq_done[0], (1<<myidx), (1<<myidx), pe);
	}
} 

static
void
amcore_ticket(int pe, int iters, int donebit)
{
	int i, curidx, myidx, myticket;

	for (i = 0; i < iters; i++) {

		PREPARE_MSG(small_msg);

		#ifdef RANDOM_TICKET
		myidx = ((int) random()) & Q_MASK;
		#else

		/* Consider using RAND */
		curidx = shmem_int_finc(&reqrep_q_idx, pe);
		myidx = curidx & Q_MASK;
		#endif

		myticket = shmem_int_finc(&ticketq[myidx].avail, pe);

		while (myticket != shmem_int_g(&ticketq[myidx].serve, pe))
			pollq_ticket(donebit);

		shmem_putmem(&msg_q[myidx], &small_msg, sizeof(small_msg), pe);
		shmem_fence();
		shmem_int_p(&ticketq[myidx].done, Q_DONE, pe);

		if (donebit)
		    shmem_int_p(&notemptyq_bit, 1, pe);
	}
} 

static
void
amcore_ticket_bit(int pe, int iters, int notavail)
{
	int i, curidx, myidx, myticket;

	for (i = 0; i < iters; i++) {

		PREPARE_MSG(small_msg);

		#ifdef RANDOM_TICKET
		myidx = ((int) random()) & Q_MASK;
		#else
		curidx = shmem_int_finc(&reqrep_q_idx, pe);
		myidx = curidx & Q_MASK;
		#endif

		myticket = shmem_int_finc(&ticketq[myidx].avail, pe);

		while (myticket != shmem_int_g(&ticketq[myidx].serve, pe))
			pollq_ticket(0);

		shmem_putmem(&msg_q[myidx], &small_msg, sizeof(small_msg), pe);
		shmem_fence();
		shmem_int_mswap(&ticketq_done[0], (1<<myidx), (1<<myidx), pe);
	}
} 

void
init_msg_q()
{
	int	i;

	memset(&msg_q, 0, sizeof(msg_t) * Q_DEPTH);

	for (i = 0; i < Q_DEPTH; i++) {
		cas_reqrep_q[i] = Q_FREE;
		ticketq[i].avail = 0;
		ticketq[i].serve = 0;
		ticketq[i].done = Q_FREE;
	}

	for (i = 0; i < USED_SLOTS; i++) {
		ticketq_done[i] = 0;
	}

	handlers_recvd = 0;
	notemptyq_bit = 1;
	pollq_skipped = 0;
}

/*
 * Even and odd PEs are paired together
 *
 */
void
pairwise_fullduplex(int iters, int testno)
{
	uint64_t    start, end, tot;
	int	    donebit;
	poll_fn	    qpoll;
	core_fn	    qcore;

	init_msg_q();

	donebit = core_tests[testno].donebit;
	qpoll   = core_tests[testno].poll;
	qcore   = core_tests[testno].core;

	/* Skip if last PE is odd */
	if ((numpes & 1) && (mype == numpes-1)) {
		shmem_barrier_all();
	}
	else {
	    if (mype == 0)
		DPRINTF(("%d> Starting Full duplex pairwise test\n", mype));

	    shmem_barrier_all();

	    start = TimeStamp();
	    qcore(mype ^ 1, iters, donebit);

	    DPRINTF(("%d> received %d handlers before the end\n", mype, handlers_recvd));
	    while (handlers_recvd != iters)
		qpoll(donebit);

	    end = TimeStamp();
	    tot = end-start;

	    printf("%3d> Full duplex pairwise %24s (%s): tot=%5.3f s, "
		    "periter=%8.3f us\n", mype, 
		    core_tests[testno].desc, 
		    #ifdef RANDOM_TICKET
		    "rand slot",
		    #else
		    "finc slot",
		    #endif
		    (float) tot / 1000000.0, 
		    (float) tot / (float) iters);
	}

	shmem_barrier_all();

	return;
}

/*
 * All (but 0) send to 0.
 *
 */
void
many_to_one(int pes, int iters, int testno)
{
	uint64_t    start, end, tot;
	int	    donebit, expected;
	poll_fn	    qpoll;
	core_fn	    qcore;

	init_msg_q();

	donebit = core_tests[testno].donebit;
	qpoll   = core_tests[testno].poll;
	qcore   = core_tests[testno].core;

	/* Skip other PEs if pes != numpes */
	if (mype > pes) {
		shmem_barrier_all();
	}
	else {
	    if (mype == 0)
		DPRINTF(("%d> Starting %d to 0 test\n", mype, pes));

	    shmem_barrier_all();

	    start = TimeStamp();
	    if (mype == 0) {
		expected = iters*pes;
		while (handlers_recvd != expected)
		    qpoll(donebit);
	    }
	    else {
		qcore(0, iters, donebit);
	    }
	    end = TimeStamp();
	    tot = end-start;
	    if (mype == 0)
		printf("%3d> %3d to one %24s (%s): tot=%5.3f s, "
		    "periter=%8.3f us\n", mype, pes, 
		    core_tests[testno].desc, 
		    #ifdef RANDOM_TICKET
		    "rand slot",
		    #else
		    "finc slot",
		    #endif
		    (float) tot / 1000000.0, 
		    (float) tot / (float) iters);
	}

	shmem_barrier_all();

	return;
}

int
main()
{
	int	    i, t;

	SHMEM_INIT();

	mype = shmem_my_pe();
	numpes = shmem_n_pes();

	if (numpes < 2) {
		printf("test needs at least 2 procs\n");
		return 1;
	}

	srandom(33 + mype);

	for (t = 0; t < CORE_TESTS_NUM; t++) {
	//for (t = CORE_TESTS_NUM-1; t >= 0; t--) {
	    many_to_one(1, 300000, t);
	    for (i = 2; i < numpes; i++)
		many_to_one(i, 80000, t);
	}

	return 0;
}


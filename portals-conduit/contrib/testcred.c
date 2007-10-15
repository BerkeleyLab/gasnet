/*   $Source: /Users/kamil/work/gasnet-cvs2/gasnet/portals-conduit/contrib/Attic/testcred.c,v $
 *     $Date: 2007/08/26 06:01:26 $
 * $Revision: 1.2 $
 * Description: GASNet Active Messages performance test
 * Copyright 2002, Dan Bonachea <bonachea@cs.berkeley.edu>
 * Terms of use are as specified in license.txt
 */

#include <stdarg.h>
#include <gasnet.h>

uint64_t  buffered_length = 0;
int       nodes = 0;
int num_partners = 0;

#ifndef TEST_SEGSZ
#define TEST_SEGSZ_EXPR ((uintptr_t)(buffered_length*(2) + nodes*(num_partners)*sizeof(int)))
#endif
#include <test.h>

/* ------------------------------------------------------------------------------------ */
#define hidx_MedReqHandler1      201
#define hidx_ShortRplyHandler3   202

typedef enum{CHAIN,ODDPOLL,PAIRS} pairing_t;
typedef struct _partner_state {
  int             partner_id;     /* id of peer node I am sending to */
  gasnet_hsl_t    lock;           /* lock controlling msg_inflight */
  int             msg_count;      /* number of messages I sent in current send group */
  volatile int    msg_inflight;   /* current number of messages in-flight */
  int64_t         time;           /* time it took to issue the sends and get replies */
  int             start_credits;  /* number of SEND credits I have before sends */
  int             end_credits;    /* number of SEND credits I have after sends */
  int             stalls;         /* number of times I stalled sending to this peer */
  int             S_banked;       /* number of credits in sender bank at end of sends */
  int             R_banked;       /* number of credits in receiver bank at end of sends */
  int             R_revoked;      /* number of credits receiver (as lender) has revoked with us */
  void*           src;
} partner_state_t;


static pairing_t method = PAIRS;
static partner_state_t  *partners = NULL;
static int partner_gap = 0;
static uint64_t msg_length = 1024*1024;
static uint64_t alignment = 1024;
static int num_rotations = 1;
static int num_steps = 10;
static void *data_source = NULL;
static void *data_dest = NULL;
static int  *xref_table = NULL;
static uint64_t xref_table_size;
static int mynode = 0;
static void* myseg = NULL;
static int debug = 0;
static int I_am_sender = 0;
static int output_partners = 1;
static int index_scale = 100;
static int rootnode = 0;
static int sender_id = 0;
static int do_copy = 0;
static int num_subcycles = 1;

extern int gasnet_send_credits(gasnet_node_t peer);

uint64_t round_up(uint64_t value, uint64_t alignment)
{
  uint64_t mask = alignment-1;
  return (value + alignment) & ~mask;
}

/* round address up to nearest multiple of alignment */
void* align(void* addr, uintptr_t alignment)
{
  uintptr_t mask = alignment-1;
  return (void*)(((uintptr_t)addr + alignment) & (~mask));
}

/* nodes form partners by cycling through possible partners, trying to keep
   a uniform spacing between them, making sure not to have any repeated
   partners.  Also try to number of times a node is a receiver constant
   across all nodes.
*/
static void define_chain_partners(void)
{
  int start = mynode;
  int msg_count, i, j;

  /* make sure important parameters have been defined */
  assert_always(nodes > 1);
  assert_always(num_partners > 0);
  assert_always(num_partners < nodes);
  assert_always(msg_length > 0);
  assert_always(data_source != NULL);
  assert_always(data_dest != NULL);


  /* generate a collection of partners, try to make them evenly spaced */
  for (i = 0; i < num_partners; i++) {
    int candidate = (start + partner_gap) % nodes;
    int cnt = 0;
  verify_candidate:
    assert(cnt < nodes);
    cnt++;
    if (candidate == mynode) {
      candidate = (candidate+1)%nodes;
      goto verify_candidate;
    }
    for (j = 0; j < i; j++) {
      if (partners[j].partner_id == candidate) {
	candidate = (candidate+1)%nodes;
	goto verify_candidate;
      }
    }
    /* if we got here, our candidate is ok */
    partners[i].partner_id = candidate;
    /*    partners[i].src = ((uint8_t*)data_source + i*buffered_length); */
    partners[i].src = (uint8_t*)data_source;
    memset(partners[i].src,(candidate%64),buffered_length);
    gasnet_hsl_init(&partners[i].lock);
    if (debug && mynode == 0) printf("[%d]: partner is %d\n",mynode,candidate);
    start = candidate;
  }
}

/* nodes form pairs, and only communicate with each other */
static void define_pairs_partners()
{
  int start = mynode;
  int msg_count, i, j;
  int partner_id;
  int even = (mynode % 2 == 0);

  /* make sure important parameters have been defined */
  assert_always(nodes > 1);
  assert_always(nodes%2 == 0);
  assert_always(num_partners > 0);
  assert_always(num_partners < nodes);
  assert_always(partner_gap % 2 == 0);
  assert_always(msg_length > 0);
  assert_always(data_source != NULL);
  assert_always(data_dest != NULL);

  /* even nodes partner with odd nodes */
  /* nodes = 4:  (0->1), (1->0), (2->3), (3->2) */
  partner_id = (mynode + (even?1:nodes-1)) % nodes;
  for (i = 0; i < num_partners; i++) {
    partners[i].partner_id = partner_id;
    /*    partners[i].src = ((uint8_t*)data_source + i*buffered_length); */
    partners[i].src = (uint8_t*)data_source;
    memset(partners[i].src,(partner_id%64),buffered_length);
    gasnet_hsl_init(&partners[i].lock);
    partner_id = (partner_id + (even? partner_gap : nodes-partner_gap)) % nodes;
  }
}

/* even nodes pair with odd nodes, only even nodes send */
static void define_oddpoll_partners()
{
  int start = mynode;
  int msg_count, i, j;
  int partner_id;

  /* make sure important parameters have been defined */
  assert_always(nodes > 1);
  assert_always(nodes%2 == 0);
  assert_always(num_partners > 0);
  assert_always(num_partners < nodes);
  assert_always(partner_gap % 2 == 0);
  assert_always(msg_length > 0);
  assert_always(data_source != NULL);
  assert_always(data_dest != NULL);


  /* even nodes partner with odd nodes */
  partner_id = (mynode + partner_gap + 1) % nodes;
  for (i = 0; i < num_partners; i++) {
    partners[i].partner_id = partner_id;
    /*    partners[i].src = ((uint8_t*)data_source + i*buffered_length); */
    partners[i].src = (uint8_t*)data_source;
    memset(partners[i].src,(partner_id%64),buffered_length);
    gasnet_hsl_init(&partners[i].lock);
    if (debug && mynode == 0) printf("[%d]: partner is %d\n",mynode,partner_id);
    partner_id = (partner_id + partner_gap) % nodes;
  }

  /* for non-senders, set invalid partner ids */
  if (!I_am_sender) {
    for (i = 0; i < num_partners; i++) partners[i].partner_id = -1;
  }
}

void partner_exchange(void)
{
  /* every one exchange partner info */
  int *start = xref_table + mynode*num_partners;
  int *dest;
  int i;
  for (i = 0; i < num_partners; i++) {
    start[i] = partners[i].partner_id;
  }
  /* put this to root node xref table */

  dest = TEST_SEG(rootnode);
  dest += num_partners*mynode;
  gasnet_put(rootnode,dest,start,num_partners*sizeof(int));

  /* barrier */
  BARRIER();
}

void print_partners(void)
{
  if (mynode == rootnode) {
    /* count the number of times a node is a receiver */
    int *receivers = calloc(nodes,sizeof(int));
    int i, node;
    printf("\nTable of Partners:\n");
    for (node = 0; node < nodes; node++) {
      int *start = xref_table + node*num_partners;
      printf("%4d:",node);
      for (i = 0; i < num_partners; i++) {
	printf("  %4d",start[i]);
	if (start[i] >= 0) receivers[start[i]]++;
      }
      printf("\n");
    }
    /* print out receiver count */
    printf("\nReceivers:");
    for (i = 0; i < nodes; i++) printf(" %2d",receivers[i]);
    printf("\n\n");
    fflush(stdout);
  }
  BARRIER();
}

void rotate_pairs(void)
{
  int i;
  int even = mynode%2==0;
  int partner_id;
  /* even nodes partner with odd nodes */
  /* nodes = 4:  (0->1), (1->0), (2->3), (3->2) */
  /* start from where we left off last time */
  partner_id = (partners[num_partners-1].partner_id + (even?partner_gap:nodes-partner_gap)) % nodes;
  for (i = 0; i < num_partners; i++) {
    partners[i].partner_id = partner_id;
    /*    partners[i].src = ((uint8_t*)data_source + i*buffered_length); */
    partners[i].src = (uint8_t*)data_source;
    memset(partners[i].src,(partner_id%64),buffered_length);
    gasnet_hsl_init(&partners[i].lock);
    partner_id = (partner_id + (even? partner_gap : nodes-partner_gap)) % nodes;
  }
  
}
/* shift our set of communicating partners by one, make sure we dont send to ourself */
void rotate_partners(void)
{
  int i, j;
  int nodes = gasnet_nodes();
  int mynode = gasnet_mynode();
  int candidate;

  if (method == PAIRS) {
    rotate_pairs();
  } else if (I_am_sender) {
    /* start off where we left behind last time */
    candidate = (partners[num_partners-1].partner_id + partner_gap) % nodes;
    for (i = 0; i < num_partners; i++) {
      int cnt = 0;
    verify_candidate:
      assert(cnt < nodes);
      cnt++;
      if (candidate == mynode) {
	assert_always( method == CHAIN );  /* should not happen in other methods */
	candidate = (candidate+1)%nodes;
	goto verify_candidate;
      }
      for (j = 0; j < i; j++) {
	if (partners[j].partner_id == candidate) {
	  assert_always( method == CHAIN );  /* should not happen in other methods */
	  candidate = (candidate + partner_gap)%nodes;
	  goto verify_candidate;
	}
      }
      /* if we got here, our candidate is ok */
      partners[i].partner_id = candidate;
      candidate = (candidate + partner_gap) % nodes;
      /* keep source data region the same */
    }
  }

  if (output_partners) {
    partner_exchange();
    print_partners();
  }
}

void print_header()
{
  printf("\nc%8s  %4s %4s  %6s  %6s  %12s  %8s  %8s  %8s  %8s  %8s  %8s  %8s  %8s  %8s  %8s\n",
	 "Index","SNod","RNod","Order","Step","Msg_Length","Time(s)","MB/sec","Msg_Cnt","S_Credit",
	 "E_Credit","d_Credit","Stalls","S_Bank","R_bank","R_revoke");
  fflush(stdout);
}

#define MB 1024*1024
void report(int peer_indx, int step, int nbytes)
{
  if (I_am_sender) {
    partner_state_t *p = &partners[peer_indx];
    int peer = p->partner_id;
    double time = ((double)(p->time))/1000000.0;
    double rate = ((double)(nbytes)/(time*MB));
    int d_credit = p->end_credits - p->start_credits;
    int index = mynode*index_scale + peer;
    int order = sender_id*num_partners + peer_indx;
    
    printf("x%8d  %4d %4d  %6d  %6d  %12ld  %8.4f  %8.3f  %8d  %8d  %8d  %8d  %8d  %8d  %8d  %8d\n",
	   index,mynode,peer,order,step,nbytes,time,rate,p->msg_count,p->start_credits,
	   p->end_credits,d_credit,p->stalls,
	   p->S_banked,p->R_banked,p->R_revoked);
    fflush(stdout);
  }
}

#define SET(peer,count) do {			  \
    gasnet_hsl_lock(&peer->lock);		  \
    peer->msg_inflight = count;			  \
    gasnet_hsl_unlock(&peer->lock);		  \
  } while (0)


void Send_To_Peer(int peer_index, int step, int nbytes)
{
  partner_state_t *p = &partners[peer_index];
  gasnet_node_t peer = p->partner_id;
  uint8_t *src = p->src;
  int64_t bytes_left = nbytes;
  int msg_count = nbytes / gasnet_AMMaxMedium();
  size_t bytes_to_send;
  int i;

  if (! I_am_sender) return;
  if (msg_count*gasnet_AMMaxMedium() < nbytes) msg_count++;
  
  SET(p,msg_count);
  p->msg_count = msg_count;
  p->start_credits = gasnet_send_credits(peer);
  p->stalls = gasnet_credit_stalls(peer);
  p->time = -TIME();
  for (i = 0; i < msg_count; i++) {
    bytes_to_send = MIN(bytes_left,gasnet_AMMaxMedium());
    GASNET_Safe(gasnet_AMRequestMedium1(peer, hidx_MedReqHandler1, src, bytes_to_send, peer_index));
    bytes_left -= bytes_to_send;
    src += bytes_to_send;
  }
  GASNET_BLOCKUNTIL(p->msg_inflight == 0);
  p->time += TIME();
  p->end_credits = gasnet_send_credits(peer);
  p->stalls = gasnet_credit_stalls(peer) - p->stalls;
  p->S_banked = gasnet_banked_credits();
  report(peer_index,step,nbytes);
}


/* Handler executed on target node when AMMedium data arrives */
void MedReqHandler1(gasnet_token_t token, void *buf, size_t nbytes, gasnet_handlerarg_t peer_indx)
{
  gasnet_node_t sender;
  GASNET_Safe(gasnet_AMGetMsgSource(token,&sender));
  /* copy the data just so we do some work */
  if (do_copy) memcpy(data_dest,buf,nbytes);
  /* issue the reply */
  GASNET_Safe(gasnet_AMReplyShort3(token,hidx_ShortRplyHandler3,peer_indx,
				   gasnet_banked_credits(),gasnet_revoked_credits(sender)));
}

/* the reply simply decrements the peers message counter on the origin node */
void ShortRplyHandler3(gasnet_token_t token, gasnet_handlerarg_t peer_indx,
		       gasnet_handlerarg_t peer_banked, gasnet_handlerarg_t peer_revoked)
{
  partner_state_t *peer;
  assert_always(peer_indx >= 0);
  assert_always(peer_indx < nodes);
  peer = &partners[peer_indx];
  gasnet_hsl_lock(&peer->lock);
  peer->msg_inflight--;
  peer->R_banked = peer_banked;
  peer->R_revoked = peer_revoked;
  gasnet_hsl_unlock(&peer->lock);
}


/* ------------------------------------------------------------------------------------ */
int main(int argc, char **argv) {
  int irot, i;
  uintptr_t max_seg_sz;
  const char *meth_str = "chain";
  const char *usage;
  gasnet_handlerentry_t htable[] = { 
    { hidx_MedReqHandler1,     MedReqHandler1 },
    { hidx_ShortRplyHandler3,  ShortRplyHandler3  }
  };

  GASNET_Safe(gasnet_init(&argc, &argv));

  mynode = gasnet_mynode();
  nodes = gasnet_nodes();
  max_seg_sz = gasnet_getMaxLocalSegmentSize();

  /* construct appropriate index scale */
  {
    int rem = nodes;
    index_scale = 1;

    while (rem > 0) {
      index_scale *= 10;
      rem /= 10;
    }
  }
  

  /* parse the command line args */
  usage = 
    "\t-d           : Debug mode\n"
    "\t-n num       : Number of partners\n"
    "\t-g num       : Partner Gap\n"
    "\t-l num       : msg_length in KB\n"
    "\t-s num       : Number of Steps between Rotations\n"
    "\t-b num       : Number of suBcycles per step\n"
    "\t-r num       : Number of Rotations\n"
    "\t-m method    : Pairing Method = chain, oddpoll, pairs\n"
    "\t-c           : Copy data from payload to target loc on receiver\n"
    "";
      
  while ( (i=getopt(argc,argv,"dcg:n:l:s:r:m:b:")) != -1) {
    switch(i) {
    case 'd':
      debug = 1;
      break;
    case 'c':
      do_copy = 1;
      break;
    case 'g':
      partner_gap = atoi(optarg);
      break;
    case 'n':
      num_partners = atoi(optarg);
      break;
    case 'l':
      msg_length = atoi(optarg);
      msg_length *= 1024;
      break;
    case 's':
      num_steps = atoi(optarg);
      break;
    case 'b':
      num_subcycles = atoi(optarg);
      break;
    case 'r':
      num_rotations = atoi(optarg);
      break;
    case 'm':
      meth_str = optarg;
      break;
    default:
      printf("invalid switch: %d\n",i);
      perror(usage);
    }
  }

  buffered_length = round_up(msg_length,1024);
  if (mynode == 0) {
    printf("msg_length              = %ld\n",(long)msg_length);
    printf("buffered_length         = %ld\n",(long)buffered_length);
    printf("nodes                   = %d\n", nodes);
    printf("num_partners            = %d\n", num_partners);
    printf("Max Local Segment Size  = %ld\n",(long)max_seg_sz);
    printf("Requesting Segment Size = %ld\n",(long)(TEST_SEGSZ_REQUEST));
    printf("Min Heap Offset         = %ld\n",(long)(TEST_MINHEAPOFFSET));
    fflush(stdout);
  }

  GASNET_Safe(gasnet_attach(htable, sizeof(htable)/sizeof(gasnet_handlerentry_t),
                            TEST_SEGSZ_REQUEST, TEST_MINHEAPOFFSET));

  if (num_partners == 0) num_partners = 2;
  if (strcmp(meth_str,"chain") == 0) {
    method = CHAIN;
    I_am_sender = 1;
    sender_id = mynode;
    num_partners = MIN(num_partners,nodes-1);
    if (!partner_gap) partner_gap = 1;
  } else if (strcmp(meth_str,"oddpoll") == 0) {
    assert(nodes % 2 == 0);
    method = ODDPOLL;
    I_am_sender = (mynode%2 == 0 ? 1 : 0);
    sender_id = (mynode/2); /* only even nodes send */
    num_partners = MIN(num_partners,nodes/2);
    if (!partner_gap) partner_gap = 2;
    if (partner_gap%2 == 1) partner_gap++;
  } else if (strcmp(meth_str,"pairs") == 0) { 
    assert(nodes % 2 == 0);
    method = PAIRS;
    I_am_sender = 1;
    sender_id = mynode;
    assert(nodes % 2 == 0);
    num_partners = MIN(num_partners,nodes/2);
    if (!partner_gap) partner_gap = 2;
    if (partner_gap%2 == 1) partner_gap++;
  } else {
    if (mynode == 0) {
      printf("Error: unknown method = %s\n",meth_str);
      fflush(stdout);
    }
    exit(1);
  }

  test_init("testcred", 1, usage);

  TEST_PRINT_CONDUITINFO();
  
  myseg = TEST_MYSEG();
  if (debug && mynode == 0) printf("myseg = %p\n",myseg);
  /* reserve space for cross-reference table */
  xref_table_size = num_partners*nodes*sizeof(int);
  xref_table = myseg;
  data_dest = align((uint8_t*)xref_table + xref_table_size, 1024);
  data_dest = align(myseg,1024);
  if (debug && mynode == 0) printf("data_dest = %p\n",data_dest);
  data_source = align( ((uint8_t*)data_dest + buffered_length), 1024);
  if (debug && mynode == 0) printf("data_source = %p\n",data_source);

  partners = (partner_state_t *)calloc(num_partners,sizeof(partner_state_t));
  assert(partners != NULL);

  switch (method) {
  case CHAIN:
    define_chain_partners();
    break;
  case PAIRS:
    define_pairs_partners();
    break;
  case ODDPOLL:
    define_oddpoll_partners();
    break;
  }

  partner_exchange();
  if (mynode == 0) {
    printf("Nodes         = %d\n",nodes);
    printf("Num Partners  = %d\n",num_partners);
    printf("Partner Gap   = %d\n",partner_gap);
    printf("Msg Length    = %ld\n",msg_length);
    printf("Num Steps     = %d\n",num_steps);
    printf("Num Rotations = %d\n",num_rotations);
    printf("Method        = %s\n",meth_str);
    printf("Num Subcycles = %d\n",num_subcycles);
  }
  if (output_partners) print_partners();

  for (irot = 0; irot < num_rotations; irot++) {
    int step;
    if (irot > 0) rotate_partners();
    BARRIER();

    for (step = 0; step < num_steps; step++) {
      int peer_indx;
      BARRIER();
      if (mynode == 0) print_header();
      BARRIER();
      for (peer_indx = 0; peer_indx < num_partners; peer_indx++) {
	int substep = step*num_subcycles;
	int bytes_left = msg_length;
	int nbytes = msg_length/num_subcycles;
	int iter;
	for (iter = 0; iter < num_subcycles; iter++) {
	  if (iter == (num_subcycles-1)) nbytes = bytes_left;
	  Send_To_Peer(peer_indx,substep+iter,nbytes);
	  bytes_left -= nbytes;
	}
	BARRIER();
	if (mynode == 0) {
	  printf("\n");
	  fflush(stdout);
	}
	BARRIER();
      }
    }
  }

  BARRIER();

  MSG("done.");

  gasnet_exit(0);
  return 0;
}


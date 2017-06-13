/*   $Source: bitbucket.org:berkeleylab/gasnet.git/tests/testgasnet.c $
 * Description: General GASNet correctness tests
 * Copyright 2002, Dan Bonachea <bonachea@cs.berkeley.edu>
 * Terms of use are as specified in license.txt
 */

#include <gasnetex.h>
#include <gasnet_tools.h>

/* limit segsz to prevent stack overflows for seg_everything tests */
#define TEST_MAXTHREADS 1
#include <test.h>

#define TEST_GASNETEX 1
#define SHORT_REQ_BASE 128
#include <other/amxtests/testam.h>

/* Define to get one big function that pushes the gcc inliner heursitics */
#undef TESTGASNET_NO_SPLIT

TEST_BACKTRACE_DECLS();

void doit(int partner, int *partnerseg);
void doit2(int partner, int *partnerseg);
void doit3(int partner, int *partnerseg);
/*void doit4(int partner, int *partnerseg); -- removed along with the memset*() calls */
void doit5(int partner, int *partnerseg);

static gex_Client_t      myclient;
static gex_EP_t    myep;
static gex_TM_t myteam;
static gex_Segment_t     mysegment;

static gex_Rank_t myrank;
static gex_Rank_t numranks;

/* ------------------------------------------------------------------------------------ */
#if GASNET_SEGMENT_EVERYTHING
  typedef struct {
    void *static_seg;
    void *common_seg;
    void *malloc_seg;
    void *sbrk_seg;
    void *mmap_seg;
    void *stack_seg;
  } test_everything_seginfo_t;
  test_everything_seginfo_t myinfo;
  test_everything_seginfo_t partnerinfo;
  int done = 0;
  GASNETT_EXTERNC void seg_everything_reqh(gex_AM_Token_t token) {
    gex_AM_ReplyMedium0(token, 251, &myinfo, sizeof(test_everything_seginfo_t), GEX_EVENT_NOW, 0);
  }
  GASNETT_EXTERNC void seg_everything_reph(gex_AM_Token_t token, void *buf, size_t nbytes) {
    assert(nbytes == sizeof(test_everything_seginfo_t));
    memcpy(&partnerinfo, buf, nbytes);
    gasnett_local_wmb();
    done = 1;
  }
  #define EVERYTHING_SEG_HANDLERS() \
    { 250, (handler_fn_t)seg_everything_reqh, GEX_FLAG_AM_REQUEST|GEX_FLAG_AM_SHORT, 0, NULL, NULL }, \
    { 251, (handler_fn_t)seg_everything_reph, GEX_FLAG_AM_REPLY|GEX_FLAG_AM_MEDIUM, 0, NULL, NULL },

  char _static_seg[TEST_SEGSZ+PAGESZ] = {1};
  char _common_seg[TEST_SEGSZ+PAGESZ];
  void everything_tests(int partner) {
    char _stack_seg[TEST_SEGSZ+PAGESZ];

    if (myrank == 0) MSG("*** gathering data segment info for SEGMENT_EVERYTHING tests...");
    BARRIER();
    myinfo.static_seg = alignup_ptr(&_static_seg, PAGESZ);
    myinfo.common_seg = alignup_ptr(&_common_seg, PAGESZ);
    myinfo.malloc_seg = alignup_ptr(test_malloc(TEST_SEGSZ+PAGESZ), PAGESZ);
    myinfo.sbrk_seg = alignup_ptr(sbrk(TEST_SEGSZ+PAGESZ), PAGESZ);
    #ifdef HAVE_MMAP
      myinfo.mmap_seg = alignup_ptr(gasnett_mmap(TEST_SEGSZ+PAGESZ), PAGESZ);
    #endif
    myinfo.stack_seg = alignup_ptr(&_stack_seg, PAGESZ);
    BARRIER();
    /* fetch partner's addresses into partnerinfo */
    gex_AM_RequestShort0(myteam, (gex_Rank_t)partner, 250, 0);
    GASNET_BLOCKUNTIL(done);
    BARRIER();

    /* test that remote access works will all the various data areas */
    if (myrank == 0) MSG(" --- testgasnet w/ static data area ---");
    doit(partner, (int*)partnerinfo.static_seg);
    if (myrank == 0) MSG(" --- testgasnet w/ common block data area ---");
    doit(partner, (int*)partnerinfo.common_seg);
    if (myrank == 0) MSG(" --- testgasnet w/ malloc data area ---");
    doit(partner, (int*)partnerinfo.malloc_seg);
    if (myrank == 0) MSG(" --- testgasnet w/ sbrk data area ---");
    doit(partner, (int*)partnerinfo.sbrk_seg);
    #ifdef HAVE_MMAP
      if (myrank == 0) MSG(" --- testgasnet w/ mmap'd data area ---");
      doit(partner, (int*)partnerinfo.mmap_seg);
    #endif
    if (myrank == 0) MSG(" --- testgasnet w/ stack data area ---");
    doit(partner, (int*)partnerinfo.stack_seg);
    BARRIER();
  }
#else
  #define EVERYTHING_SEG_HANDLERS()
#endif

#if GASNET_PAR
  #define MAX_THREADS 10
#else
  #define MAX_THREADS 1
#endif
int num_threads = MAX_THREADS;

void test_threadinfo(int threadid, int numthreads) {
  int i;
  gasnet_threadinfo_t my_ti;
  static gasnet_threadinfo_t all_ti[MAX_THREADS];

  { GASNET_BEGIN_FUNCTION();
    my_ti = GASNET_GET_THREADINFO();
  }
  { gasnet_threadinfo_t ti = GASNET_GET_THREADINFO();
    assert_always(ti == my_ti);
  }
  { GASNET_POST_THREADINFO(my_ti);
    gasnet_threadinfo_t ti = GASNET_GET_THREADINFO();
    assert_always(ti == my_ti);
  }
  assert(threadid < numthreads && numthreads <= MAX_THREADS);
  all_ti[threadid] = my_ti;
  PTHREAD_LOCALBARRIER(numthreads);
  for (i = 0; i < numthreads; i++) {
    if (i != threadid) assert_always(my_ti != all_ti[i]);
  }
  PTHREAD_LOCALBARRIER(numthreads);
}
/* ------------------------------------------------------------------------------------ */
/* test libgasnet-specific gasnet_tools interfaces */
#if GASNET_PAR
  /* thread-parallel gasnet_tools tests */
  #ifdef __cplusplus
    extern "C"
  #endif
  void *test_libgasnetpar_tools(void *p) {
    int idx = (int)(uintptr_t)p;
    PTHREAD_LOCALBARRIER(num_threads);
    test_threadinfo(idx, num_threads);
    PTHREAD_LOCALBARRIER(num_threads);
  #if GASNETI_ARCH_ALTIX
    /* Don't pin threads because system is either shared or using cgroups */
  #elif GASNETI_ARCH_IBMPE
    /* Don't pin threads because system s/w will have already done so */
  #else
    gasnett_set_affinity(idx);
  #endif
    PTHREAD_LOCALBARRIER(num_threads);
    return NULL;
  }
#endif
void test_libgasnet_tools(void) {
  void *p;
  TEST_TRACING_MACROS();
  #ifdef HAVE_MMAP
    p = gasnett_mmap(GASNETT_PAGESIZE);
    assert_always(p);
    assert_always(((uintptr_t)p)%GASNETT_PAGESIZE == 0);
  #endif
  test_threadinfo(0, 1);
  #if GASNET_DEBUGMALLOC
  { char *ptr = (char *)gasnett_debug_malloc(10); 
    char *ptr2;
    gasnett_heapstats_t hs;
    assert_always(ptr);
    gasnett_debug_memcheck(ptr);
    ptr = (char *)gasnett_debug_realloc(ptr,20);
    assert_always(ptr);
    gasnett_debug_free(ptr);
    ptr = (char *)gasnett_debug_calloc(10,20);
    strcpy(ptr,"testing 1 2 3");
    ptr2 = gasnett_debug_strdup(ptr);
    assert_always(ptr2 && ptr != ptr2 && !strcmp(ptr,ptr2));
    gasnett_debug_free(ptr2);
    ptr2 = gasnett_debug_strndup(ptr,4);
    assert_always(ptr2 && ptr != ptr2 && !strncmp(ptr,ptr2,4) && strlen(ptr2) == 4);
    gasnett_debug_memcheck_one();
    gasnett_debug_memcheck_all(); 
    gasnett_debug_free(ptr2);
    gasnett_debug_free(ptr);
    gasnett_getheapstats(&hs);
  }
  #endif
  #if GASNET_PAR
    num_threads = test_thread_limit(num_threads);
    test_createandjoin_pthreads(num_threads, &test_libgasnetpar_tools, NULL, 0);
  #endif
  MSG("*** passed libgasnet_tools test!!");
}
/* ------------------------------------------------------------------------------------ */
int main(int argc, char **argv) {
  uintptr_t local_segsz, global_segsz;
  int partner;
  
  gex_AM_Entry_t handlers[] = { EVERYTHING_SEG_HANDLERS() ALLAM_HANDLERS() };

  const char *myname = "testgasnet";
  const gex_Flags_t myflags = 0;
  GASNET_Safe(gex_Client_Init(&myclient, &myep, &myteam, myname, &argc, &argv, myflags));
  if (strcmp(myname, gex_Client_QueryName(myclient))) {
    MSG("*** ERROR - FAILED CLIENT NAME TEST!!!!!");
  }
  if (myflags != gex_Client_QueryFlags(myclient)) {
    MSG("*** ERROR - FAILED CLIENT FLAGS TEST!!!!!");
  }
  if (myclient != gex_EP_QueryClient(myep)) {
    MSG("*** ERROR - FAILED EP CLIENT TEST!!!!!");
  }
  if (myclient != gex_TM_QueryClient(myteam)) {
    MSG("*** ERROR - FAILED TM CLIENT TEST!!!!!");
  }
  if (myep != gex_TM_QueryEP(myteam)) {
    MSG("*** ERROR - FAILED TM EP TEST!!!!!");
  }
  if (GEX_SEGMENT_INVALID != gex_EP_QuerySegment(myep)) {
    MSG("*** ERROR - FAILED EP NO-SEGMENT TEST!!!!!");
  }

  void *mydata = (void*)&main;
  if (NULL != gex_Client_QueryCData(myclient) ||
      mydata != (gex_Client_SetCData(myclient, mydata),
                 gex_Client_QueryCData(myclient))) {
    MSG("*** ERROR - FAILED CLIENT CDATA TEST!!!!!");
  }
  if (NULL != gex_EP_QueryCData(myep) ||
      mydata != (gex_EP_SetCData(myep, mydata),
                 gex_EP_QueryCData(myep))) {
    MSG("*** ERROR - FAILED EP CDATA TEST!!!!!");
  }
  if (NULL != gex_TM_QueryCData(myteam) ||
      mydata != (gex_TM_SetCData(myteam, mydata),
                 gex_TM_QueryCData(myteam))) {
    MSG("*** ERROR - FAILED TM CDATA TEST!!!!!");
  }

  myrank = gex_TM_QueryRank(myteam);
  numranks = gex_TM_QuerySize(myteam);

  local_segsz = gasnet_getMaxLocalSegmentSize();
  global_segsz = gasnet_getMaxGlobalSegmentSize();
  #if GASNET_SEGMENT_EVERYTHING
    assert_always(local_segsz == (uintptr_t)-1);
    assert_always(global_segsz == (uintptr_t)-1);
  #else
    assert_always(local_segsz >= global_segsz);
    assert_always(local_segsz % GASNET_PAGESIZE == 0);
    assert_always(global_segsz % GASNET_PAGESIZE == 0);
    assert_always(global_segsz > 0);
  #endif

  GASNET_Safe(gex_Segment_Attach(&mysegment, myteam, TEST_SEGSZ_REQUEST));
#if GASNET_SEGMENT_EVERYTHING
  // test.h intercepted gex_Segment_Attach() but does not fake a gex_Segment_t
#else
  if (myclient != gex_Segment_QueryClient(mysegment)) {
    MSG("*** ERROR - FAILED SEGMENT CLIENT TEST!!!!!");
  }
  if (mysegment != gex_EP_QuerySegment(myep)) {
    MSG("*** ERROR - FAILED EP SEGMENT TEST!!!!!");
  }
  if (NULL != gex_Segment_QueryCData(mysegment) ||
      mydata != (gex_Segment_SetCData(mysegment, mydata),
                 gex_Segment_QueryCData(mysegment))) {
    MSG("*** ERROR - FAILED SEGMENT CDATA TEST!!!!!");
  }

  // To be removed:
  assert(gex_Segment_QueryAddr(mysegment) == TEST_MYSEG());
  assert(gex_Segment_QuerySize(mysegment) >= TEST_SEGSZ_REQUEST);
#endif

  GASNET_Safe(gex_EP_RegisterHandlers(myep, handlers, sizeof(handlers)/sizeof(gex_AM_Entry_t)));

  test_init("testgasnet",0,"");
  assert(TEST_SEGSZ >= 2*sizeof(int)*NUMHANDLERS_PER_TYPE);

  TEST_PRINT_CONDUITINFO();
  { char lstr[50], gstr[50];
    gasnett_format_number(local_segsz, lstr, sizeof(lstr), 1);
    gasnett_format_number(global_segsz, gstr, sizeof(gstr), 1);
    MSG0(" MaxLocalSegmentSize on node0:  %s\n"
         " MaxGlobalSegmentSize:          %s",
         lstr, gstr);
  }
  BARRIER();

  { int smaj = GEX_SPEC_VERSION_MAJOR;
    int smin = GEX_SPEC_VERSION_MINOR;
    int rmaj = GASNET_RELEASE_VERSION_MAJOR;
    int rmin = GASNET_RELEASE_VERSION_MINOR;
    int rpat = GASNET_RELEASE_VERSION_PATCH;
    // TODO-EX: (smaj > 0) when we reach 1.0
    assert_always(smaj >= 0 && smin >= 0 && rmaj > 0 && rmin >= 0 && rpat >= 0);
  }

  { int i;
    printf("my args: argc=%i argv=[", argc);
    for (i=0; i < argc; i++) {
      printf("%s'%s'",(i>0?" ":""),argv[i]);
    }
    printf("]\n"); fflush(stdout);
  }
  BARRIER();

  TEST_BACKTRACE_INIT(argv[0]);
  TEST_BACKTRACE();

  test_libgasnet_tools();
  partner = (myrank + 1) % numranks;
  #if GASNET_SEGMENT_EVERYTHING
    everything_tests(partner);
  #else
    doit(partner, (int *)TEST_SEG(partner));
  #endif

  MSG("done.");

  gasnet_exit(0);
  return 0;
}

void doit(int partner, int *partnerseg) {
  BARRIER();
  /*  blocking test */
  { int val1=0, val2=0;
    val1 = myrank + 100;

    gex_RMA_PutBlocking(myteam, partner, partnerseg, &val1, sizeof(int), 0);
    gex_RMA_GetBlocking(myteam, &val2, partner, partnerseg, sizeof(int), 0);

    if (val2 == (myrank + 100)) MSG("*** passed blocking test!!");
    else MSG("*** ERROR - FAILED BLOCKING TEST!!!!!");
  }

  BARRIER();
  /*  blocking list test */
  #define iters 100
  { GASNET_BEGIN_FUNCTION();
    gex_Event_t events[iters];
    int val1;
    int vals[iters];
    int success = 1;
    int i;
    for (i = 0; i < iters; i++) {
      val1 = 100 + i + myrank;
      events[i] = gex_RMA_PutNB(myteam, partner, partnerseg+i, &val1, sizeof(int), GEX_EVENT_NOW, 0);
    }
    gex_Event_WaitAll(events, iters, 0);
    for (i = 0; i < iters; i++) {
      events[i] = gex_RMA_GetNB(myteam, &vals[i], partner, partnerseg+i, sizeof(int), 0);
    }
    gex_Event_WaitAll(events, iters, 0);
    for (i=0; i < iters; i++) {
      if (vals[i] != 100 + myrank + i) {
        MSG("*** ERROR - FAILED NB LIST TEST!!! vals[%i] = %i, expected %i",
            i, vals[i], 100 + myrank + i);
        success = 0;
      }
    }
    if (success) MSG("*** passed blocking list test!!");
  }

#ifndef TESTGASNET_NO_SPLIT
  doit2(partner, partnerseg);
}
void doit2(int partner, int *partnerseg) {
#endif

  BARRIER();
  { /*  implicit test */
    GASNET_BEGIN_FUNCTION();
    int vals[100];
    int i, success=1;
    for (i=0; i < 100; i++) {
      int tmp = myrank + i;
      gex_RMA_PutNBI(myteam, partner, partnerseg+i, &tmp, sizeof(int), GEX_EVENT_NOW, 0);
    }
    gex_NBI_Wait(GEX_EC_PUT,0);
    for (i=0; i < 100; i++) {
      gex_RMA_GetNBI(myteam, &vals[i], partner, partnerseg+i, sizeof(int), 0);
    }
    gex_NBI_Wait(GEX_EC_GET,0);
    for (i=0; i < 100; i++) {
      if (vals[i] != myrank + i) {
        MSG("*** ERROR - FAILED NBI TEST!!! vals[%i] = %i, expected %i",
            i, vals[i], myrank + i);
        success = 0;
      }
    }
    if (success) MSG("*** passed nbi test!!");
  }

#ifndef TESTGASNET_NO_SPLIT
  doit3(partner, partnerseg);
}
void doit3(int partner, int *partnerseg) {
#endif

  BARRIER();

  { /*  value test */
    GASNET_BEGIN_FUNCTION();
    int i, success=1;
    unsigned char *partnerbase2 = (unsigned char *)(partnerseg+300);
    for (i=0; i < 100; i++) {
      gex_RMA_PutBlockingVal(myteam, partner, partnerseg+i, 1000 + myrank + i, sizeof(int), 0);
    }
    for (i=0; i < 100; i++) {
      gex_Event_Wait(gex_RMA_PutNBVal(myteam, partner, partnerseg+i+100, 1000 + myrank + i, sizeof(int), 0));
    }
    for (i=0; i < 100; i++) {
      gex_RMA_PutNBIVal(myteam, partner, partnerseg+i+200, 1000 + myrank + i, sizeof(int), 0);
    }
    gex_NBI_Wait(GEX_EC_PUT,0);

    for (i=0; i < 100; i++) {
      int tmp1 = gex_RMA_GetBlockingVal(myteam, partner, partnerseg+i, sizeof(int), 0);
      int tmp2 = gex_RMA_GetBlockingVal(myteam, partner, partnerseg+i+200, sizeof(int), 0);
      if (tmp1 != 1000 + myrank + i || tmp2 != 1000 + myrank + i) {
        MSG("*** ERROR - FAILED INT VALUE TEST 1!!!");
        printf("node %i/%i  i=%i tmp1=%i tmp2=%i (1000 + myrank + i)=%i\n", 
          (int)myrank, (int)numranks, 
          i, tmp1, tmp2, 1000 + myrank + i); fflush(stdout); 
        success = 0;
      }
    }

    for (i=0; i < 100; i++) {
      gex_RMA_PutBlockingVal(myteam, partner, partnerbase2+i, 100 + myrank + i, sizeof(unsigned char), 0);
    }
    for (i=0; i < 100; i++) {
      gex_Event_Wait(gex_RMA_PutNBVal(myteam, partner, partnerbase2+i+100, 100 + myrank + i, sizeof(unsigned char), 0));
    }
    for (i=0; i < 100; i++) {
      gex_RMA_PutNBIVal(myteam, partner, partnerbase2+i+200, 100 + myrank + i, sizeof(unsigned char), 0);
    }
    gex_NBI_Wait(GEX_EC_PUT,0);

    for (i=0; i < 100; i++) {
      unsigned int tmp1 = (unsigned int)gex_RMA_GetBlockingVal(myteam, partner, partnerbase2+i, sizeof(unsigned char), 0);
      unsigned int tmp2 = (unsigned int)gex_RMA_GetBlockingVal(myteam, partner, partnerbase2+i+200, sizeof(unsigned char), 0);
      if (tmp1 != (unsigned char)(100 + myrank + i) || 
          tmp2 != (unsigned char)(100 + myrank + i)) {
        MSG("*** ERROR - FAILED CHAR VALUE TEST 1!!!");
        printf("node %i/%i  i=%i tmp1=%i tmp2=%i (100 + myrank + i)=%i\n", 
          (int)myrank, (int)numranks, 
          i, tmp1, tmp2, 100 + myrank + i); fflush(stdout); 
        success = 0;
      }
    }

    if (success) MSG("*** passed value test!!");
  }

#ifndef TESTGASNET_NO_SPLIT
  doit5(partner, partnerseg);
}
void doit5(int partner, int *partnerseg) {
#endif

  BARRIER();

  /* NB and NBI put/overwrite/get tests */
  #define MAXVALS (1024)
  #define MAXSZ (MAXVALS*8)
  #define SEGSZ (MAXSZ*4)
  #define VAL(sz, iter) \
    (((uint64_t)(sz) << 32) | ((uint64_t)(100 + myrank) << 16) | ((iter) & 0xFF))
  assert(TEST_SEGSZ >= 2*SEGSZ);
  { GASNET_BEGIN_FUNCTION();
    uint64_t *localvals=(uint64_t *)test_malloc(SEGSZ);
    int success = 1;
    int i, sz;
    for (i = 0; i < MAX(1,iters/10); i++) {
      uint64_t *localpos=localvals;
      uint64_t *segpos=(uint64_t *)TEST_MYSEG();
      uint64_t *rsegpos=(uint64_t *)((char*)partnerseg+SEGSZ);
      for (sz = 1; sz <= MAXSZ; sz*=2) {
        gex_Event_t event;
        int elems = sz/8;
        int j;
        uint64_t val = VAL(sz, i); /* setup known src value */
        if (sz < 8) {
          elems = 1;
          memset(localpos, (val & 0xFF), sz);
          memset(segpos, (val & 0xFF), sz);
          memset(&val, (val & 0xFF), sz);
        } else {
          for (j=0; j < elems; j++) {
            localpos[j] = val;
            segpos[j] = val;
          }
        }
        event = gex_RMA_PutNB(myteam, partner, rsegpos, localpos, sz, GEX_EVENT_DEFER, 0);
        gex_Event_Wait(event);

        event = gex_RMA_PutNB(myteam, partner, rsegpos+elems, localpos, sz, GEX_EVENT_NOW, 0);
        memset(localpos, 0xCC, sz); /* clear */
        gex_Event_Wait(event);

        event = gex_RMA_PutNB(myteam, partner, rsegpos+2*elems, segpos, sz, GEX_EVENT_DEFER, 0);
        gex_Event_Wait(event);

        event = gex_RMA_PutNB(myteam, partner, rsegpos+3*elems, segpos, sz, GEX_EVENT_NOW, 0);
        memset(segpos, 0xCC, sz); /* clear */
        gex_Event_Wait(event);

        gex_Event_Wait(gex_RMA_GetNB(myteam, localpos, partner, rsegpos, sz, 0));
        gex_Event_Wait(gex_RMA_GetNB(myteam, localpos+elems, partner, rsegpos+elems, sz, 0));
        gex_Event_Wait(gex_RMA_GetNB(myteam, segpos, partner, rsegpos+2*elems, sz, 0));
        gex_Event_Wait(gex_RMA_GetNB(myteam, segpos+elems, partner, rsegpos+3*elems, sz, 0));

        for (j=0; j < elems*2; j++) {
          int ok;
          ok = localpos[j] == val;
          if (sz < 8) ok = !memcmp(&(localpos[j]), &val, sz);
          if (!ok) {
              MSG("*** ERROR - FAILED OUT-OF-SEG PUT_NB/OVERWRITE TEST!!! sz=%i j=%i (got=%016" PRIx64 " expected=%016" PRIx64 ")",
                  sz, j, localpos[j], val);
              success = 0;
          }
          ok = segpos[j] == val;
          if (sz < 8) ok = !memcmp(&(segpos[j]), &val, sz);
          if (!ok) {
              MSG("*** ERROR - FAILED IN-SEG PUT_NB/OVERWRITE TEST!!! sz=%i j=%i (got=%016" PRIx64 " expected=%016" PRIx64 ")",
                  sz, j, segpos[j], val);
              success = 0;
          }
        }
      }
    }
    test_free(localvals);
    if (success) MSG("*** passed nb put/overwrite test!!");
  }
  { GASNET_BEGIN_FUNCTION();
    uint64_t *localvals=(uint64_t *)test_malloc(SEGSZ);
    int success = 1;
    int i, sz;
    for (i = 0; i < MAX(1,iters/10); i++) {
      uint64_t *localpos=localvals;
      uint64_t *segpos=(uint64_t *)TEST_MYSEG();
      uint64_t *rsegpos=(uint64_t *)((char*)partnerseg+SEGSZ);
      for (sz = 1; sz <= MAXSZ; sz*=2) {
        int elems = sz/8;
        int j;
        uint64_t val = VAL(sz, i+91); /* setup known src value, different from NB test */
        if (sz < 8) {
          elems = 1;
          memset(localpos, (val & 0xFF), sz);
          memset(segpos, (val & 0xFF), sz);
          memset(&val, (val & 0xFF), sz);
        } else {
          for (j=0; j < elems; j++) {
            localpos[j] = val;
            segpos[j] = val;
          }
        }
        gex_RMA_PutNBI(myteam, partner, rsegpos, localpos, sz, GEX_EVENT_DEFER, 0);
        gex_NBI_Wait(GEX_EC_PUT,0);

        gex_RMA_PutNBI(myteam, partner, rsegpos+elems, localpos, sz, GEX_EVENT_NOW, 0);
        memset(localpos, 0xCC, sz); /* clear */
        gex_NBI_Wait(GEX_EC_PUT,0);

        gex_RMA_PutNBI(myteam, partner, rsegpos+2*elems, segpos, sz, GEX_EVENT_DEFER, 0);
        gex_NBI_Wait(GEX_EC_PUT,0);

        gex_RMA_PutNBI(myteam, partner, rsegpos+3*elems, segpos, sz, GEX_EVENT_NOW, 0);
        memset(segpos, 0xCC, sz); /* clear */
        gex_NBI_Wait(GEX_EC_PUT,0);

        gex_RMA_GetNBI(myteam, localpos, partner, rsegpos, sz, 0);
        gex_NBI_Wait(GEX_EC_GET,0);
        gex_RMA_GetNBI(myteam, localpos+elems, partner, rsegpos+elems, sz, 0);
        gex_NBI_Wait(GEX_EC_GET,0);
        gex_RMA_GetNBI(myteam, segpos, partner, rsegpos+2*elems, sz, 0);
        gex_NBI_Wait(GEX_EC_GET,0);
        gex_RMA_GetNBI(myteam, segpos+elems, partner, rsegpos+3*elems, sz, 0);
        gex_NBI_Wait(GEX_EC_GET,0);

        for (j=0; j < elems*2; j++) {
          int ok;
          ok = localpos[j] == val;
          if (sz < 8) ok = !memcmp(&(localpos[j]), &val, sz);
          if (!ok) {
              MSG("*** ERROR - FAILED OUT-OF-SEG PUT_NBI/OVERWRITE TEST!!! sz=%i j=%i (got=%016" PRIx64 " expected=%016" PRIx64 ")",
                  sz, j, localpos[j], val);
              success = 0;
          }
          ok = segpos[j] == val;
          if (sz < 8) ok = !memcmp(&(segpos[j]), &val, sz);
          if (!ok) {
              MSG("*** ERROR - FAILED IN-SEG PUT_NBI/OVERWRITE TEST!!! sz=%i j=%i (got=%016" PRIx64 " expected=%016" PRIx64 ")",
                  sz, j, segpos[j], val);
              success = 0;
          }
        }
      }
    }
    test_free(localvals);
    if (success) MSG("*** passed nbi put/overwrite test!!");
  }

  BARRIER();

  { /* all ams test */
    int i;
    static int base = 0;
    for (i=0; i < 10; i++) {
      ALLAM_REQ(partner);

      GASNET_BLOCKUNTIL(ALLAM_DONE(base+i+1));
    }
    base += i;

    MSG("*** passed AM test!!");
  }

  BARRIER();

  /* Invoke all the atomics, once each.
   * This is a compile/link check, used to ensure that clients can link all the
   * the atomics (especially from c++ when testgasnet is built as textcxx).
   * This is distinct from testtools, which checks that these "do the right thing".
   */
  #ifndef GASNETT_HAVE_ATOMIC_CAS
    #define gasnett_atomic_compare_and_swap(a,b,c,d) ((void)0)
    #define gasnett_atomic_swap(a,b,c)               ((void)0)
  #endif
  #ifndef GASNETT_HAVE_ATOMIC_ADD_SUB
    #define gasnett_atomic_add(a,b,c)                ((void)0)
    #define gasnett_atomic_subtract(a,b,c)           ((void)0)
  #endif
  #ifndef GASNETT_HAVE_STRONGATOMIC_CAS
    #define gasnett_strongatomic_compare_and_swap(a,b,c,d) ((void)0)
    #define gasnett_strongatomic_swap(a,b,c)               ((void)0)
  #endif
  #ifndef GASNETT_HAVE_STRONGATOMIC_ADD_SUB
    #define gasnett_strongatomic_add(a,b,c)                ((void)0)
    #define gasnett_strongatomic_subtract(a,b,c)           ((void)0)
  #endif
  #define TEST_ATOMICS(scalar,class) do { \
    gasnett_##class##_t val = gasnett_##class##_init(1);             \
    scalar tmp = gasnett_##class##_read(&val, 0);                    \
    gasnett_##class##_set(&val, tmp, 0);                             \
    gasnett_##class##_increment(&val, 0);                            \
    gasnett_##class##_decrement(&val, 0);                            \
    (void) gasnett_##class##_decrement_and_test(&val, 0);            \
    (void) gasnett_##class##_compare_and_swap(&val, 0, 1 ,0);        \
    (void) gasnett_##class##_swap(&val, 1 ,0);                       \
    (void) gasnett_##class##_add(&val, tmp ,0);                      \
    (void) gasnett_##class##_subtract(&val, tmp ,0);                 \
  } while(0)
  {
    gasnett_atomic_sval_t stmp = gasnett_atomic_signed((gasnett_atomic_val_t)0);
    gasnett_atomic_increment((gasnett_atomic_t*)&stmp,0);
    TEST_ATOMICS(gasnett_atomic_val_t, atomic);
    TEST_ATOMICS(gasnett_atomic_val_t, strongatomic);
    TEST_ATOMICS(uint32_t, atomic32);
    TEST_ATOMICS(uint32_t, strongatomic32);
    TEST_ATOMICS(uint64_t, atomic64);
    TEST_ATOMICS(uint64_t, strongatomic64);
  }
  { /* attempt to generate alignment problems: */
    gasnett_atomic32_t *ptr32;
    uint32_t tmp32;
    gasnett_atomic64_t *ptr64;
    uint64_t tmp64;
    { struct { char c; gasnett_atomic32_t val32; } s = {0, gasnett_atomic32_init(1)};
      ptr32 = &s.val32;
      tmp32 = gasnett_atomic32_read(ptr32, 0);
      gasnett_atomic32_set(ptr32, tmp32, 0);
      (void)gasnett_atomic32_compare_and_swap(ptr32, 0, 1, 0);
    }
    { struct { char c; gasnett_atomic64_t val64; } s = {0, gasnett_atomic64_init(1)};
      ptr64 = &s.val64;
      tmp64 = gasnett_atomic64_read(ptr64, 0);
      gasnett_atomic64_set(ptr64, tmp64, 0);
      (void)gasnett_atomic64_compare_and_swap(ptr64, 0, 1, 0);
    }
    { double dbl = 1.0;
      uintptr_t tmp = (uintptr_t)&dbl;
      ptr64 = (gasnett_atomic64_t *)tmp; /* conversion suppresses gcc-4 warning (bug 2158) */
      tmp64 = gasnett_atomic64_read(ptr64, 0);
      gasnett_atomic64_set(ptr64, tmp64, 0);
      (void)gasnett_atomic64_compare_and_swap(ptr64, 0, 1, 0);
    }
    { struct { char c; double dbl; } s = {0, 1.0};
      uintptr_t tmp = (uintptr_t)&s.dbl;
      ptr64 = (gasnett_atomic64_t *)tmp; /* conversion suppresses gcc-4 warning (bug 2158) */
      tmp64 = gasnett_atomic64_read(ptr64, 0);
      gasnett_atomic64_set(ptr64, tmp64, 0);
      (void)gasnett_atomic64_compare_and_swap(ptr64, 0, 1, 0);
    }
  }

  /* Serial tests of optional internal 128-bit atomics have
   * moved to gasnet_diagnostic.c (run from testinternal).
   */
  
  BARRIER();
}

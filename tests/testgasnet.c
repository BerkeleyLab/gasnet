/*   $Source: /Users/kamil/work/gasnet-cvs2/gasnet/tests/testgasnet.c,v $
 *     $Date: 2004/10/22 19:47:23 $
 * $Revision: 1.22 $
 * Description: General GASNet correctness tests
 * Copyright 2002, Dan Bonachea <bonachea@cs.berkeley.edu>
 * Terms of use are as specified in license.txt
 */

#include <gasnet.h>

/* limit segsz to prevent stack overflows for seg_everything tests */
#define TEST_MAXTHREADS 1
#include <test.h>

#define TEST_GASNET 1
#define SHORT_REQ_BASE 128
#include <other/amxtests/testam.h>

void doit(int partner, int *partnerseg);

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
  void seg_everything_reqh(gasnet_token_t token) {
    GASNET_Safe(gasnet_AMReplyMedium0(token, 251, &myinfo, sizeof(test_everything_seginfo_t)));
  }
  void seg_everything_reph(gasnet_token_t token, void *buf, size_t nbytes) {
    assert(nbytes == sizeof(test_everything_seginfo_t));
    memcpy(&partnerinfo, buf, nbytes);
    gasnett_local_wmb();
    done = 1;
  }
  #define EVERYTHING_SEG_HANDLERS() \
    { 250, (void (*)())seg_everything_reqh }, \
    { 251, (void (*)())seg_everything_reph },

  char _static_seg[TEST_SEGSZ+PAGESZ] = {1};
  char _common_seg[TEST_SEGSZ+PAGESZ];
  void everything_tests(int partner) {
    char _stack_seg[TEST_SEGSZ+PAGESZ];

    if (gasnet_mynode() == 0) MSG("*** gathering data segment info for SEGMENT_EVERYTHING tests...");
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
    GASNET_Safe(gasnet_AMRequestShort0((gasnet_node_t)partner, 250));
    GASNET_BLOCKUNTIL(done);
    BARRIER();

    /* test that remote access works will all the various data areas */
    if (gasnet_mynode() == 0) MSG(" --- testgasnet w/ static data area ---");
    doit(partner, (int*)partnerinfo.static_seg);
    if (gasnet_mynode() == 0) MSG(" --- testgasnet w/ common block data area ---");
    doit(partner, (int*)partnerinfo.common_seg);
    if (gasnet_mynode() == 0) MSG(" --- testgasnet w/ malloc data area ---");
    doit(partner, (int*)partnerinfo.malloc_seg);
    if (gasnet_mynode() == 0) MSG(" --- testgasnet w/ sbrk data area ---");
    doit(partner, (int*)partnerinfo.sbrk_seg);
    #ifdef HAVE_MMAP
      if (gasnet_mynode() == 0) MSG(" --- testgasnet w/ mmap'd data area ---");
      doit(partner, (int*)partnerinfo.mmap_seg);
    #endif
    if (gasnet_mynode() == 0) MSG(" --- testgasnet w/ stack data area ---");
    doit(partner, (int*)partnerinfo.stack_seg);
    BARRIER();
  }
#else
  #define EVERYTHING_SEG_HANDLERS()
#endif

int main(int argc, char **argv) {
  int partner;
  
  gasnet_handlerentry_t handlers[] = { EVERYTHING_SEG_HANDLERS() ALLAM_HANDLERS() };

  GASNET_Safe(gasnet_init(&argc, &argv));
  GASNET_Safe(gasnet_attach(handlers, sizeof(handlers)/sizeof(gasnet_handlerentry_t), 
    TEST_SEGSZ, TEST_MINHEAPOFFSET));
  TEST_SEG(gasnet_mynode()); /* ensure we got the segment requested */
  assert(TEST_SEGSZ >= 2*sizeof(int)*NUMHANDLERS_PER_TYPE);

  MSG("running...");

  { int i;
    printf("my args: argc=%i argv=[", argc);
    for (i=0; i < argc; i++) {
      printf("%s'%s'",(i>0?" ":""),argv[i]);
    }
    printf("]\n"); fflush(stdout);
  }
  partner = (gasnet_mynode() + 1) % gasnet_nodes();
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
  int mynode = gasnet_mynode();

  BARRIER();
  /*  blocking test */
  { int val1=0, val2=0;
    val1 = mynode + 100;

    gasnet_put(partner, partnerseg, &val1, sizeof(int));
    gasnet_get(&val2, partner, partnerseg, sizeof(int));

    if (val2 == (mynode + 100)) MSG("*** passed blocking test!!");
    else MSG("*** ERROR - FAILED BLOCKING TEST!!!!!");
  }

  BARRIER();
  /*  blocking list test */
  #define iters 100
  { GASNET_BEGIN_FUNCTION();
    gasnet_handle_t handles[iters];
    int val1;
    int vals[iters];
    int success = 1;
    int i;
    for (i = 0; i < iters; i++) {
      val1 = 100 + i + mynode;
      handles[i] = gasnet_put_nb(partner, partnerseg+i, &val1, sizeof(int));
    }
    gasnet_wait_syncnb_all(handles, iters); 
    for (i = 0; i < iters; i++) {
      handles[i] = gasnet_get_nb(&vals[i], partner, partnerseg+i, sizeof(int));
    }
    gasnet_wait_syncnb_all(handles, iters); 
    for (i=0; i < iters; i++) {
      if (vals[i] != 100 + mynode + i) {
        MSG("*** ERROR - FAILED NB LIST TEST!!!");
        success = 0;
      }
    }
    if (success) MSG("*** passed blocking list test!!");
  }

  BARRIER();

  { /*  implicit test */
    GASNET_BEGIN_FUNCTION();
    int vals[100];
    int i, success=1;
    for (i=0; i < 100; i++) {
      int tmp = mynode + i;
      gasnet_put_nbi(partner, partnerseg+i, &tmp, sizeof(int));
    }
    gasnet_wait_syncnbi_puts();
    for (i=0; i < 100; i++) {
      gasnet_get_nbi(&vals[i], partner, partnerseg+i, sizeof(int));
    }
    gasnet_wait_syncnbi_gets();
    for (i=0; i < 100; i++) {
      if (vals[i] != mynode + i) {
        MSG("*** ERROR - FAILED NBI TEST!!!");
        success = 0;
      }
    }
    if (success) MSG("*** passed nbi test!!");
  }

  BARRIER();

  { /*  value test */
    GASNET_BEGIN_FUNCTION();
    int vals[300];
    int i, success=1;
    unsigned char *partnerbase2 = (unsigned char *)(partnerseg+300);
    for (i=0; i < 100; i++) {
      gasnet_put_val(partner, partnerseg+i, 1000 + mynode + i, sizeof(int));
    }
    for (i=0; i < 100; i++) {
      gasnet_wait_syncnb(gasnet_put_nb_val(partner, partnerseg+i+100, 1000 + mynode + i, sizeof(int)));
    }
    for (i=0; i < 100; i++) {
      gasnet_put_nbi_val(partner, partnerseg+i+200, 1000 + mynode + i, sizeof(int));
    }
    gasnet_wait_syncnbi_puts();

    for (i=0; i < 100; i++) {
      int tmp1 = gasnet_get_val(partner, partnerseg+i, sizeof(int));
      int tmp2 = gasnet_get_val(partner, partnerseg+i+200, sizeof(int));
      if (tmp1 != 1000 + mynode + i || tmp2 != 1000 + mynode + i) {
        MSG("*** ERROR - FAILED INT VALUE TEST 1!!!");
        printf("node %i/%i  i=%i tmp1=%i tmp2=%i (1000 + mynode + i)=%i\n", 
          (int)gasnet_mynode(), (int)gasnet_nodes(), 
          i, tmp1, tmp2, 1000 + mynode + i); fflush(stdout); 
        success = 0;
      }
    }
    { gasnet_valget_handle_t handles[100];
      for (i=0; i < 100; i++) {
        handles[i] = gasnet_get_nb_val(partner, partnerseg+i+100, sizeof(int));
      }
      for (i=0; i < 100; i++) {
        int tmp = (int)gasnet_wait_syncnb_valget(handles[i]);
        if (tmp != 1000 + mynode + i) {
          MSG("*** ERROR - FAILED INT VALUE TEST 2!!!");
          printf("node %i/%i  i=%i tmp1=%i (1000 + mynode + i)=%i\n", 
            (int)gasnet_mynode(), (int)gasnet_nodes(), 
            i, tmp, 1000 + mynode + i); fflush(stdout); 
          success = 0;
        }
      }
    }

    for (i=0; i < 100; i++) {
      gasnet_put_val(partner, partnerbase2+i, 100 + mynode + i, sizeof(unsigned char));
    }
    for (i=0; i < 100; i++) {
      gasnet_wait_syncnb(gasnet_put_nb_val(partner, partnerbase2+i+100, 100 + mynode + i, sizeof(unsigned char)));
    }
    for (i=0; i < 100; i++) {
      gasnet_put_nbi_val(partner, partnerbase2+i+200, 100 + mynode + i, sizeof(unsigned char));
    }
    gasnet_wait_syncnbi_puts();

    for (i=0; i < 100; i++) {
      unsigned int tmp1 = (unsigned int)gasnet_get_val(partner, partnerbase2+i, sizeof(unsigned char));
      unsigned int tmp2 = (unsigned int)gasnet_get_val(partner, partnerbase2+i+200, sizeof(unsigned char));
      if (tmp1 != (unsigned int)(100 + mynode + i) || 
          tmp2 != (unsigned int)(100 + mynode + i)) {
        MSG("*** ERROR - FAILED CHAR VALUE TEST 1!!!");
        printf("node %i/%i  i=%i tmp1=%i tmp2=%i (100 + mynode + i)=%i\n", 
          (int)gasnet_mynode(), (int)gasnet_nodes(), 
          i, tmp1, tmp2, 100 + mynode + i); fflush(stdout); 
        success = 0;
      }
    }
    { gasnet_valget_handle_t handles[100];
      for (i=0; i < 100; i++) {
        handles[i] = gasnet_get_nb_val(partner, partnerbase2+i+100, sizeof(unsigned char));
      }
      for (i=0; i < 100; i++) {
        unsigned int tmp = (unsigned int)gasnet_wait_syncnb_valget(handles[i]);
        if (tmp != (unsigned int)(100 + mynode + i)) {
          MSG("*** ERROR - FAILED CHAR VALUE TEST 2!!!");
          printf("node %i/%i  i=%i tmp1=%i (100 + mynode + i)=%i\n", 
            (int)gasnet_mynode(), (int)gasnet_nodes(), 
            i, tmp, 100 + mynode + i); fflush(stdout); 
          success = 0;
        }
      }
    }

    if (success) MSG("*** passed value test!!");
  }

  BARRIER();

  { /*  memset test */
    GASNET_BEGIN_FUNCTION();
    int i, success=1;
    int vals[300];

    gasnet_memset(partner, partnerseg, 0x55, 100*sizeof(int));
    gasnet_wait_syncnb(gasnet_memset_nb(partner, partnerseg+100, 0x66, 100*sizeof(int)));
    gasnet_memset_nbi(partner, partnerseg+200, 0x77, 100*sizeof(int));
    gasnet_wait_syncnbi_puts();

    gasnet_get(&vals, partner, partnerseg, 300*sizeof(int));

    for (i=0; i < 100; i++) {
      if (vals[i] != ((int)(unsigned long long)0x5555555555555555ull)) {
        MSG("*** ERROR - FAILED MEMSET TEST!!!");
        success = 0;
      }
      if (vals[i+100] != ((int)(unsigned long long)0x6666666666666666ull)) {
        MSG("*** ERROR - FAILED MEMSET TEST!!!");
        success = 0;
      }
      if (vals[i+200] != ((int)(unsigned long long)0x7777777777777777ull)) {
        MSG("*** ERROR - FAILED MEMSET TEST!!!");
        success = 0;
      }
    }
    if (success) MSG("*** passed memset test!!");
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

}

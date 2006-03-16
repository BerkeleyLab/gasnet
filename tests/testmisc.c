/*   $Source: /Users/kamil/work/gasnet-cvs2/gasnet/tests/testmisc.c,v $
 *     $Date: 2006/03/16 01:57:38 $
 * $Revision: 1.26.2.1 $
 * Description: GASNet misc performance test
 *   Measures the overhead associated with a number of purely local 
 *   operations that involve no communication. 
 * Copyright 2002, Dan Bonachea <bonachea@cs.berkeley.edu>
 * Terms of use are as specified in license.txt
 */

#include <gasnet.h>
#include <gasnet_tools.h>

#include <test.h>

#if defined(GASNET_PAR) || defined (GASNET_PARSYNC)
  #include <pthread.h>
#endif

int mynode = 0;
int iters=0;
void *myseg = NULL;
int accuracy = 0;

void report(const char *desc, int64_t totaltime, int iters) {
  if (mynode == 0) {
      char format[80];
      sprintf(format, "%%-50s: %%%i.%if sec  %%%i.%if us/iter\n", 
              (4+accuracy), accuracy, (4+accuracy), accuracy);
      printf(format, desc, totaltime/1.0E9, (totaltime/1000.0)/iters);
      fflush(stdout);
  }
}

/* placed in a function to avoid excessive inlining */
gasnett_tick_t ticktime() { return gasnett_ticks_now(); }
uint64_t tickcvt(gasnett_tick_t ticks) { return gasnett_ticks_to_ns(ticks); }

void doit1();
void doit2();
void doit3();
void doit4();
void doit5();
void doit6();
void doit7();
/* ------------------------------------------------------------------------------------ */
#define hidx_null_shorthandler        201
#define hidx_justreply_shorthandler   202

#define hidx_null_medhandler          203
#define hidx_justreply_medhandler     204

#define hidx_null_longhandler         205
#define hidx_justreply_longhandler    206

void null_shorthandler(gasnet_token_t token) {
}

void justreply_shorthandler(gasnet_token_t token) {
  gasnet_AMReplyShort0(token, hidx_null_shorthandler);
}

void null_medhandler(gasnet_token_t token, void *buf, size_t nbytes) {
}

void justreply_medhandler(gasnet_token_t token, void *buf, size_t nbytes) {
  gasnet_AMReplyMedium0(token, hidx_null_medhandler, buf, nbytes);
}

void null_longhandler(gasnet_token_t token, void *buf, size_t nbytes) {
}

void justreply_longhandler(gasnet_token_t token, void *buf, size_t nbytes) {
  gasnet_AMReplyLong0(token, hidx_null_longhandler, buf, nbytes, buf);
}
/* ------------------------------------------------------------------------------------ */
/* This tester measures the performance of a number of miscellaneous GASNet functions 
   that don't involve actual communication, to assist in evaluating the overhead of 
   the GASNet layer itself
 */
int main(int argc, char **argv) {
  gasnet_handlerentry_t htable[] = { 
    { hidx_null_shorthandler,       null_shorthandler },
    { hidx_justreply_shorthandler,  justreply_shorthandler },
    { hidx_null_medhandler,         null_medhandler },
    { hidx_justreply_medhandler,    justreply_medhandler },
    { hidx_null_longhandler,        null_longhandler },
    { hidx_justreply_longhandler,   justreply_longhandler }
  };

  GASNET_Safe(gasnet_init(&argc, &argv));
  GASNET_Safe(gasnet_attach(htable, sizeof(htable)/sizeof(gasnet_handlerentry_t),
                            TEST_SEGSZ_REQUEST, TEST_MINHEAPOFFSET));
  test_init("testmisc",1,"(iters) (accuracy_digits)");

  mynode = gasnet_mynode();
  myseg = TEST_MYSEG();

  if (argc > 1) iters = atoi(argv[1]);
  if (!iters) iters = 100000;

  if (argc > 2) accuracy = atoi(argv[2]);
  if (!accuracy) accuracy = 3;

  if (argc > 2) test_usage();

  if (mynode == 0) {
      printf("Running misc performance test with %i iterations...\n",iters);
      printf("%-50s    Total time    Avg. time\n"
             "%-50s    ----------    ---------\n", "", "");
      fflush(stdout);
  }

  doit1();
  MSG("done.");

  gasnet_exit(0);
  return 0;
}

#define TIME_OPERATION_FULL(desc, preop, op, postop)       \
  { int i, _iters = iters, _warmupiters = MAX(1,iters/10); \
    gasnett_tick_t start,end;  /* use ticks interface */   \
    BARRIER();                 /* for best accuracy */     \
    preop;       /* warm-up */                             \
    for (i=0; i < _warmupiters; i++) { op; }               \
    postop;                                                \
    BARRIER();                                             \
    start = ticktime();                                    \
    preop;                                                 \
    for (i=0; i < _iters; i++) { op; }                     \
    postop;                                                \
    end = ticktime();                                      \
    BARRIER();                                             \
    if (((const char *)(desc)) && ((char*)(desc))[0])      \
      report((desc), tickcvt(end - start), iters);         \
    else report(#op, tickcvt(end - start), iters);         \
  }
#define TIME_OPERATION(desc, op) TIME_OPERATION_FULL(desc, {}, op, {})

char p[1];
gasnet_hsl_t hsl = GASNET_HSL_INITIALIZER;
gasnett_atomic_t a = gasnett_atomic_init(0);
int32_t temp = 0;
gasnett_tick_t timertemp = 0;
int8_t bigtemp[1024];
gasnet_handle_t handles[8];

/* ------------------------------------------------------------------------------------ */
void doit1() { GASNET_BEGIN_FUNCTION();

    { int i; for (i=0;i<8;i++) handles[i] = GASNET_INVALID_HANDLE; }

    TIME_OPERATION("Tester overhead", {});
    
    TIME_OPERATION("gasnett_ticks_now()",
      { timertemp = gasnett_ticks_now(); });
    
    TIME_OPERATION("gasnett_ticks_to_us()",
      { timertemp = (gasnett_tick_t)gasnett_ticks_to_us(timertemp); });
    
    TIME_OPERATION("gasnett_ticks_to_ns()",
      { timertemp = (gasnett_tick_t)gasnett_ticks_to_ns(timertemp); });
    
    TIME_OPERATION("Do-nothing gasnet_AMPoll()",
      { gasnet_AMPoll(); });
    
    TIME_OPERATION("Loopback do-nothing gasnet_AMRequestShort0()",
      { gasnet_AMRequestShort0(mynode, hidx_null_shorthandler); });

    TIME_OPERATION("Loopback do nothing AM short request-reply",
      { gasnet_AMRequestShort0(mynode, hidx_justreply_shorthandler); });

    TIME_OPERATION("Loopback do-nothing gasnet_AMRequestMedium0()",
      { gasnet_AMRequestMedium0(mynode, hidx_null_medhandler, p, 0); });

    TIME_OPERATION("Loopback do nothing AM medium request-reply",
      { gasnet_AMRequestMedium0(mynode, hidx_justreply_medhandler, p, 0); });

    TIME_OPERATION("Loopback do-nothing gasnet_AMRequestLong0()",
      { gasnet_AMRequestLong0(mynode, hidx_null_medhandler, p, 0, myseg); });

    TIME_OPERATION("Loopback do nothing AM long request-reply",
      { gasnet_AMRequestLong0(mynode, hidx_justreply_medhandler, p, 0, myseg); });

    doit2();
}
/* ------------------------------------------------------------------------------------ */
void doit2() { GASNET_BEGIN_FUNCTION();

    TIME_OPERATION("hold/resume interrupts",
      { gasnet_hold_interrupts(); gasnet_resume_interrupts(); });

    #if defined(GASNET_PAR) || defined (GASNET_PARSYNC)
      { static pthread_mutex_t mutex = PTHREAD_MUTEX_INITIALIZER;
        TIME_OPERATION("lock/unlock uncontended pthread mutex",
          { pthread_mutex_lock(&mutex); pthread_mutex_unlock(&mutex); });
      }
    #endif

    TIME_OPERATION("lock/unlock uncontended HSL (" _STRINGIFY(TEST_PARSEQ) " mode)",
      { gasnet_hsl_lock(&hsl); gasnet_hsl_unlock(&hsl); });

    TIME_OPERATION("gasnett_local_wmb", gasnett_local_wmb());
    TIME_OPERATION("gasnett_local_rmb", gasnett_local_rmb());
    TIME_OPERATION("gasnett_local_mb", gasnett_local_mb());

    TIME_OPERATION("gasnett_atomic_read", gasnett_atomic_read(&a,0));
    TIME_OPERATION("gasnett_atomic_set", gasnett_atomic_set(&a,1,0));
    TIME_OPERATION("gasnett_atomic_increment", gasnett_atomic_increment(&a,0));
    TIME_OPERATION("gasnett_atomic_decrement", gasnett_atomic_decrement(&a,0));
    TIME_OPERATION("gasnett_atomic_decrement_and_test", gasnett_atomic_decrement_and_test(&a,0));

#if defined(GASNETT_HAVE_ATOMIC_CAS)
    TIME_OPERATION_FULL("gasnett_atomic_compare_and_swap (result=1)", { gasnett_atomic_set(&a,0,0); },
			{ gasnett_atomic_compare_and_swap(&a,0,0,0); }, {});
    TIME_OPERATION_FULL("gasnett_atomic_compare_and_swap (result=0)", { gasnett_atomic_set(&a,1,0); },
			{ gasnett_atomic_compare_and_swap(&a,0,0,0); }, {});
#endif

    doit3();
}
/* ------------------------------------------------------------------------------------ */
void doit3() { 
  void * volatile x = 0;
  volatile gasnet_threadinfo_t ti;
  volatile uintptr_t y = 0;

  { GASNET_BEGIN_FUNCTION();
    gasnett_threadkey_t key = GASNETT_THREADKEY_INITIALIZER;

    gasnett_threadkey_init(&key);
    TIME_OPERATION("gasnett_threadkey_get (" _STRINGIFY(TEST_PARSEQ) " mode)",
      { x = gasnett_threadkey_get(key); });
    TIME_OPERATION("gasnett_threadkey_set (" _STRINGIFY(TEST_PARSEQ) " mode)",
      { gasnett_threadkey_set(key, x); });
    TIME_OPERATION("gasnett_threadkey_get_noinit (" _STRINGIFY(TEST_PARSEQ) " mode)",
      { x = gasnett_threadkey_get_noinit(key); });
    TIME_OPERATION("gasnett_threadkey_set_noinit (" _STRINGIFY(TEST_PARSEQ) " mode)",
      { gasnett_threadkey_set_noinit(key, x); });
  }

  TIME_OPERATION("GASNET_BEGIN_FUNCTION (" _STRINGIFY(TEST_PARSEQ) " mode)", 
      { GASNET_BEGIN_FUNCTION(); });
  TIME_OPERATION("GASNET_POST_THREADINFO (" _STRINGIFY(TEST_PARSEQ) " mode)", 
      { GASNET_POST_THREADINFO(ti); });
  { GASNET_BEGIN_FUNCTION();
    TIME_OPERATION("GASNET_GET_THREADINFO (w/ BEGIN) (" _STRINGIFY(TEST_PARSEQ) " mode)", 
      { y ^= (uintptr_t)GASNET_GET_THREADINFO(); });
  }
  TIME_OPERATION("GASNET_GET_THREADINFO (no BEGIN) (" _STRINGIFY(TEST_PARSEQ) " mode)", 
      { y ^= (uintptr_t)GASNET_GET_THREADINFO(); });

  doit4();
}
/* ------------------------------------------------------------------------------------ */
void doit4() { GASNET_BEGIN_FUNCTION();

    TIME_OPERATION("local 4-byte gasnet_put",
      { gasnet_put(mynode, myseg, &temp, 4); });

    TIME_OPERATION("local 4-byte gasnet_put_nb",
      { gasnet_wait_syncnb(gasnet_put_nb(mynode, myseg, &temp, 4)); });

    TIME_OPERATION_FULL("local 4-byte gasnet_put_nbi", {},
      { gasnet_put_nbi(mynode, myseg, &temp, 4); },
      { gasnet_wait_syncnbi_puts(); });

    TIME_OPERATION("local 4-byte gasnet_put_bulk",
      { gasnet_put_bulk(mynode, myseg, &temp, 4); });

    TIME_OPERATION("local 4-byte gasnet_put_nb_bulk",
      { gasnet_wait_syncnb(gasnet_put_nb_bulk(mynode, myseg, &temp, 4)); });

    TIME_OPERATION_FULL("local 4-byte gasnet_put_nbi_bulk", {},
      { gasnet_put_nbi(mynode, myseg, &temp, 4); },
      { gasnet_wait_syncnbi_puts(); });

    TIME_OPERATION("local 4-byte gasnet_put_val",
      { gasnet_put_val(mynode, myseg, temp, 4); });

    TIME_OPERATION("local 4-byte gasnet_put_nb_val",
      { gasnet_wait_syncnb(gasnet_put_nb_val(mynode, myseg, temp, 4)); });

    TIME_OPERATION_FULL("local 4-byte gasnet_put_nbi_val", {},
      { gasnet_put_nbi_val(mynode, myseg, temp, 4); },
      { gasnet_wait_syncnbi_puts(); });

    TIME_OPERATION("local 1024-byte gasnet_put_bulk",
      { gasnet_put_bulk(mynode, myseg, &bigtemp, 1024); });

    doit5();
}
/* ------------------------------------------------------------------------------------ */
void doit5() { GASNET_BEGIN_FUNCTION();

    TIME_OPERATION("local 4-byte gasnet_get",
      { gasnet_get(&temp, mynode, myseg, 4); });

    TIME_OPERATION("local 4-byte gasnet_get_nb",
      { gasnet_wait_syncnb(gasnet_get_nb(&temp, mynode, myseg, 4)); });

    TIME_OPERATION_FULL("local 4-byte gasnet_get_nbi", {},
      { gasnet_get_nbi(&temp, mynode, myseg, 4); },
      { gasnet_wait_syncnbi_gets(); });

    TIME_OPERATION("local 4-byte gasnet_get_bulk",
      { gasnet_get_bulk(&temp, mynode, myseg, 4); });

    TIME_OPERATION("local 4-byte gasnet_get_nb_bulk",
      { gasnet_wait_syncnb(gasnet_get_nb_bulk(&temp, mynode, myseg, 4)); });

    TIME_OPERATION_FULL("local 4-byte gasnet_get_nbi_bulk", {},
      { gasnet_get_nbi_bulk(&temp, mynode, myseg, 4); },
      { gasnet_wait_syncnbi_gets(); });

    TIME_OPERATION("local 4-byte gasnet_get_val",
      { temp = (int32_t)gasnet_get_val(mynode, myseg, 4); });

    TIME_OPERATION("local 4-byte gasnet_get_nb_val",
      { gasnet_valget_handle_t handle = gasnet_get_nb_val(mynode, myseg, 4);
        temp = (int32_t)gasnet_wait_syncnb_valget(handle);
      });

    TIME_OPERATION("local 1024-byte gasnet_get_bulk",
      { gasnet_get_bulk(&bigtemp, mynode, myseg, 1024); });

    doit6();
}
/* ------------------------------------------------------------------------------------ */
void doit6() { GASNET_BEGIN_FUNCTION();

    { int32_t temp1 = 0;
      int32_t temp2 = 0;
      int32_t volatile *ptemp1 = &temp1;
      int32_t volatile *ptemp2 = &temp2;
      TIME_OPERATION("local 4-byte assignment",
        { *(ptemp1) = *(ptemp2); });
    }

    { int8_t temp1[1024];
      int8_t temp2[1024];
      int64_t start = TIME();
      TIME_OPERATION("local 1024-byte memcpy",
        { memcpy(temp1, temp2, 1024); });
    }

    doit7();
}
/* ------------------------------------------------------------------------------------ */
void doit7() { GASNET_BEGIN_FUNCTION();

    TIME_OPERATION("do-nothing gasnet_wait_syncnb()",
      { gasnet_wait_syncnb(GASNET_INVALID_HANDLE);  });

    TIME_OPERATION("do-nothing gasnet_try_syncnb()",
      { gasnet_try_syncnb(GASNET_INVALID_HANDLE); });

    TIME_OPERATION("do-nothing gasnet_wait_syncnb_all() (8 handles)",
      { gasnet_wait_syncnb_all(handles, 8); });

    TIME_OPERATION("do-nothing gasnet_wait_syncnb_some() (8 handles)",
      { gasnet_wait_syncnb_some(handles, 8); });

    TIME_OPERATION("do-nothing gasnet_try_syncnb_all() (8 handles)",
      { gasnet_try_syncnb_all(handles, 8);  });

    TIME_OPERATION("do-nothing gasnet_try_syncnb_some() (8 handles)",
      { gasnet_try_syncnb_some(handles, 8); });

    TIME_OPERATION("do-nothing gasnet_wait_syncnbi_all()",
      { gasnet_wait_syncnbi_all(); });

    TIME_OPERATION("do-nothing gasnet_wait_syncnbi_puts()",
      { gasnet_wait_syncnbi_puts(); });

    TIME_OPERATION("do-nothing gasnet_wait_syncnbi_gets()",
      { gasnet_wait_syncnbi_gets(); });

    TIME_OPERATION("do-nothing gasnet_try_syncnbi_all()",
      { gasnet_try_syncnbi_all(); });

    TIME_OPERATION("do-nothing gasnet_try_syncnbi_puts()",
      { gasnet_try_syncnbi_puts(); });

    TIME_OPERATION("do-nothing gasnet_try_syncnbi_gets()",
      { gasnet_try_syncnbi_gets(); });

    TIME_OPERATION("do-nothing begin/end nbi accessregion",
      { gasnet_begin_nbi_accessregion();
        gasnet_wait_syncnb(gasnet_end_nbi_accessregion());
      });

    TIME_OPERATION("single-node barrier",
      { gasnet_barrier_notify(0,GASNET_BARRIERFLAG_ANONYMOUS);            
        gasnet_barrier_wait(0,GASNET_BARRIERFLAG_ANONYMOUS); 
      });
    if (gasnet_nodes() > 1)
      MSG0("Note: this is actually the barrier time for %i nodes, "
           "since you're running with more than one node.\n", (int)gasnet_nodes());
}
/* ------------------------------------------------------------------------------------ */


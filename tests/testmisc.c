/*  $Archive:: /Ti/GASNet/tests/testmisc.c                             $
 *     $Date: 2004/07/08 16:59:20 $
 * $Revision: 1.7.10.1 $
 * Description: GASNet misc performance test
 *   Measures the overhead associated with a number of purely local 
 *   operations that involve no communication. 
 * Copyright 2002, Dan Bonachea <bonachea@cs.berkeley.edu>
 * Terms of use are as specified in license.txt
 */

#include <gasnet.h>

#include <test.h>

#if defined(GASNET_PAR) || defined (GASNET_PARSYNC)
  #include <pthread.h>
#endif

int mynode = 0;

void report(const char *desc, int64_t totaltime, int iters) {
  if (mynode == 0) {
      printf("%-50s: %8.3f sec  %8.3f us/iter\n",
        desc, ((float)totaltime)/1000000, ((float)totaltime)/iters);
      fflush(stdout);
  }
}
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
  int iters=0;
  int i = 0;
  void *myseg = NULL;
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
                            TEST_SEGSZ, TEST_MINHEAPOFFSET));
  TEST_DEBUGPERFORMANCE_WARNING();

  MSG("running...");

  mynode = gasnet_mynode();
  myseg = TEST_MYSEG();

  if (argc > 1) iters = atoi(argv[1]);
  if (!iters) iters = 100000;

  if (mynode == 0) {
      printf("Running misc performance test with %i iterations...\n",iters);
      printf("GASNET_CONFIG:%s\n",GASNET_CONFIG_STRING);
      printf("%-50s    Total time    Avg. time\n"
             "%-50s    ----------    ---------\n", "", "");
      fflush(stdout);
  }

  /* ------------------------------------------------------------------------------------ */
  { GASNET_BEGIN_FUNCTION();

    BARRIER();

    {
      int64_t start = TIME();
      for (i=0; i < iters; i++) {
      }
      report("Tester overhead",TIME() - start, iters);
    }

    BARRIER();

    {
      int64_t start = TIME();
      for (i=0; i < iters; i++) {
        gasnet_AMPoll();            
      }
      report("Do-nothing gasnet_AMPoll()",TIME() - start, iters);
    }

    BARRIER();

    {
      int64_t start = TIME();
      for (i=0; i < iters; i++) {
        gasnet_AMRequestShort0(mynode, hidx_null_shorthandler);
      }
      report("Loopback do-nothing gasnet_AMRequestShort0()",TIME() - start, iters);
    }

    BARRIER();

    {
      int64_t start = TIME();
      for (i=0; i < iters; i++) {
        gasnet_AMRequestShort0(mynode, hidx_justreply_shorthandler);
      }
      report("Loopback do nothing AM short request-reply",TIME() - start, iters);
    }

    BARRIER();

    {
      int64_t start = TIME();
      char p[1];
      for (i=0; i < iters; i++) {
        gasnet_AMRequestMedium0(mynode, hidx_null_medhandler, p, 0);
      }
      report("Loopback do-nothing gasnet_AMRequestMedium0()",TIME() - start, iters);
    }

    BARRIER();

    {
      int64_t start = TIME();
      char p[1];
      for (i=0; i < iters; i++) {
        gasnet_AMRequestMedium0(mynode, hidx_justreply_medhandler, p, 0);
      }
      report("Loopback do nothing AM medium request-reply",TIME() - start, iters);
    }

    BARRIER();

    {
      int64_t start = TIME();
      char p[1];
      for (i=0; i < iters; i++) {
        gasnet_AMRequestLong0(mynode, hidx_null_medhandler, p, 0, myseg);
      }
      report("Loopback do-nothing gasnet_AMRequestLong0()",TIME() - start, iters);
    }

    BARRIER();

    {
      int64_t start = TIME();
      char p[1];
      for (i=0; i < iters; i++) {
        gasnet_AMRequestLong0(mynode, hidx_justreply_medhandler, p, 0, myseg);
      }
      report("Loopback do nothing AM long request-reply",TIME() - start, iters);
    }

    /* ------------------------------------------------------------------------------------ */

    BARRIER();

    {
      int64_t start = TIME();
      for (i=0; i < iters; i++) {
        gasnet_hold_interrupts();          
        gasnet_resume_interrupts();
      }
      report("hold/resume interrupts",TIME() - start, iters);
    }

    BARRIER();

  #if defined(GASNET_PAR) || defined (GASNET_PARSYNC)
    {
      static pthread_mutex_t mutex = PTHREAD_MUTEX_INITIALIZER;
      int64_t start = TIME();
      for (i=0; i < iters; i++) {
        pthread_mutex_lock(&mutex);
        pthread_mutex_unlock(&mutex);
      }
      report("lock/unlock uncontended pthread mutex",TIME() - start, iters);
    }
  #endif

    BARRIER();

    {
      static gasnet_hsl_t hsl = GASNET_HSL_INITIALIZER;
      int64_t start = TIME();
      for (i=0; i < iters; i++) {
        gasnet_hsl_lock(&hsl);
        gasnet_hsl_unlock(&hsl);
      }
      report("lock/unlock uncontended HSL",TIME() - start, iters);
    }

    /* ------------------------------------------------------------------------------------ */

    BARRIER();

    {
      int64_t start = TIME();
      int32_t temp = 0;
      for (i=0; i < iters; i++) {
        gasnet_put(mynode, myseg, &temp, 4);
      }
      report("local 4-byte gasnet_put",TIME() - start, iters);
    }

    BARRIER();

    {
      int64_t start = TIME();
      int32_t temp = 0;
      for (i=0; i < iters; i++) {
        gasnet_wait_syncnb(gasnet_put_nb(mynode, myseg, &temp, 4));
      }
      report("local 4-byte gasnet_put_nb",TIME() - start, iters);
    }

    BARRIER();

    {
      int64_t start = TIME();
      int32_t temp = 0;
      for (i=0; i < iters; i++) {
        gasnet_put_nbi(mynode, myseg, &temp, 4);
      }
      gasnet_wait_syncnbi_puts();
      report("local 4-byte gasnet_put_nbi",TIME() - start, iters);
    }

    BARRIER();

    {
      int64_t start = TIME();
      int32_t temp = 0;
      for (i=0; i < iters; i++) {
        gasnet_put_bulk(mynode, myseg, &temp, 4);
      }
      report("local 4-byte gasnet_put_bulk",TIME() - start, iters);
    }

    BARRIER();
    {
      int64_t start = TIME();
      int32_t temp = 0;
      for (i=0; i < iters; i++) {
        gasnet_wait_syncnb(gasnet_put_nb_bulk(mynode, myseg, &temp, 4));
      }
      report("local 4-byte gasnet_put_nb_bulk",TIME() - start, iters);
    }

    BARRIER();

    {
      int64_t start = TIME();
      int32_t temp = 0;
      for (i=0; i < iters; i++) {
        gasnet_put_nbi_bulk(mynode, myseg, &temp, 4);
      }
      gasnet_wait_syncnbi_puts();
      report("local 4-byte gasnet_put_nbi_bulk",TIME() - start, iters);
    }

    BARRIER();

    {
      int64_t start = TIME();
      int32_t temp = 0;
      for (i=0; i < iters; i++) {
        gasnet_put_val(mynode, myseg, temp, 4);
      }
      report("local 4-byte gasnet_put_val",TIME() - start, iters);
    }

    BARRIER();

    {
      int64_t start = TIME();
      int32_t temp = 0;
      for (i=0; i < iters; i++) {
        gasnet_wait_syncnb(gasnet_put_nb_val(mynode, myseg, temp, 4));
      }
      report("local 4-byte gasnet_put_nb_val",TIME() - start, iters);
    }

    BARRIER();

    {
      int64_t start = TIME();
      int32_t temp = 0;
      for (i=0; i < iters; i++) {
        gasnet_put_nbi_val(mynode, myseg, temp, 4);
      }
      gasnet_wait_syncnbi_puts();
      report("local 4-byte gasnet_put_nbi_val",TIME() - start, iters);
    }

    BARRIER();

    {
      int8_t temp[1024];
      int64_t start = TIME();
      for (i=0; i < iters; i++) {
        gasnet_put_bulk(mynode, myseg, &temp, 1024);
      }
      report("local 1024-byte gasnet_put_bulk",TIME() - start, iters);
    }

    /* ------------------------------------------------------------------------------------ */

    BARRIER();

    {
      int64_t start = TIME();
      int32_t temp = 0;
      for (i=0; i < iters; i++) {
        gasnet_get(&temp, mynode, myseg, 4);
      }
      report("local 4-byte gasnet_get",TIME() - start, iters);
    }

    BARRIER();

    {
      int64_t start = TIME();
      int32_t temp = 0;
      for (i=0; i < iters; i++) {
        gasnet_wait_syncnb(gasnet_get_nb(&temp, mynode, myseg, 4));
      }
      report("local 4-byte gasnet_get_nb",TIME() - start, iters);
    }

    BARRIER();

    {
      int64_t start = TIME();
      int32_t temp = 0;
      for (i=0; i < iters; i++) {
        gasnet_get_nbi(&temp, mynode, myseg, 4);
      }
      gasnet_wait_syncnbi_gets();
      report("local 4-byte gasnet_get_nbi",TIME() - start, iters);
    }

    BARRIER();

    {
      int64_t start = TIME();
      int32_t temp = 0;
      for (i=0; i < iters; i++) {
        gasnet_get_bulk(&temp, mynode, myseg, 4);
      }
      report("local 4-byte gasnet_get_bulk",TIME() - start, iters);
    }

    BARRIER();
    {
      int64_t start = TIME();
      int32_t temp = 0;
      for (i=0; i < iters; i++) {
        gasnet_wait_syncnb(gasnet_get_nb_bulk(&temp, mynode, myseg, 4));
      }
      report("local 4-byte gasnet_get_nb_bulk",TIME() - start, iters);
    }

    BARRIER();

    {
      int64_t start = TIME();
      int32_t temp = 0;
      for (i=0; i < iters; i++) {
        gasnet_get_nbi_bulk(&temp, mynode, myseg, 4);
      }
      gasnet_wait_syncnbi_gets();
      report("local 4-byte gasnet_get_nbi_bulk",TIME() - start, iters);
    }

    BARRIER();


    {
      int64_t start = TIME();
      int32_t temp = 0;
      for (i=0; i < iters; i++) {
        temp = (int32_t)gasnet_get_val(mynode, myseg, 4);
      }
      report("local 4-byte gasnet_get_val",TIME() - start, iters);
    }

    BARRIER();

    {
      int64_t start = TIME();
      int32_t temp = 0;
      for (i=0; i < iters; i++) {
        gasnet_valget_handle_t handle = gasnet_get_nb_val(mynode, myseg, 4);
        temp = (int32_t)gasnet_wait_syncnb_valget(handle);
      }
      report("local 4-byte gasnet_get_nb_val",TIME() - start, iters);
    }

    BARRIER();

    {
      int8_t temp[1024];
      int64_t start = TIME();
      for (i=0; i < iters; i++) {
        gasnet_get_bulk(&temp, mynode, myseg, 1024);
      }
      report("local 1024-byte gasnet_get_bulk",TIME() - start, iters);
    }

    /* ------------------------------------------------------------------------------------ */

    BARRIER();

    {
      int32_t temp1 = 0;
      int32_t temp2 = 0;
      int32_t volatile *ptemp1 = &temp1;
      int32_t volatile *ptemp2 = &temp2;
      int64_t start = TIME();
      for (i=0; i < iters; i++) {
        *(ptemp1) = *(ptemp2);
      }
      report("local 4-byte assignment",TIME() - start, iters);
    }

    BARRIER();

    {
      int8_t temp1[1024];
      int8_t temp2[1024];
      int64_t start = TIME();
      for (i=0; i < iters; i++) {
        memcpy(temp1, temp2, 1024);
      }
      report("local 1024-byte memcpy",TIME() - start, iters);
    }

    /* ------------------------------------------------------------------------------------ */

    BARRIER();

    {
      int64_t start = TIME();
      for (i=0; i < iters; i++) {
        gasnet_wait_syncnb(GASNET_INVALID_HANDLE);          
      }
      report("do-nothing gasnet_wait_syncnb()",TIME() - start, iters);
    }

    BARRIER();

    {
      int64_t start = TIME();
      for (i=0; i < iters; i++) {
        gasnet_try_syncnb(GASNET_INVALID_HANDLE);          
      }
      report("do-nothing gasnet_try_syncnb()",TIME() - start, iters);
    }

    BARRIER();

    {
      int64_t start;
      gasnet_handle_t handles[8];
      for (i=0;i<8;i++) handles[i] = GASNET_INVALID_HANDLE;
      start = TIME();
      for (i=0; i < iters; i++) {
        gasnet_wait_syncnb_all(handles, 8);          
      }
      report("do-nothing gasnet_wait_syncnb_all() (8 handles)",TIME() - start, iters);
    }

    BARRIER();

    {
      int64_t start;
      gasnet_handle_t handles[8];
      for (i=0;i<8;i++) handles[i] = GASNET_INVALID_HANDLE;
      start = TIME();
      for (i=0; i < iters; i++) {
        gasnet_wait_syncnb_some(handles, 8);          
      }
      report("do-nothing gasnet_wait_syncnb_some() (8 handles)",TIME() - start, iters);
    }

    BARRIER();

    {
      int64_t start;
      gasnet_handle_t handles[8];
      for (i=0;i<8;i++) handles[i] = GASNET_INVALID_HANDLE;
      start = TIME();
      for (i=0; i < iters; i++) {
        gasnet_try_syncnb_all(handles, 8);          
      }
      report("do-nothing gasnet_try_syncnb_all() (8 handles)",TIME() - start, iters);
    }

    BARRIER();

    {
      int64_t start;
      gasnet_handle_t handles[8];
      for (i=0;i<8;i++) handles[i] = GASNET_INVALID_HANDLE;
      start = TIME();
      for (i=0; i < iters; i++) {
        gasnet_try_syncnb_some(handles, 8);          
      }
      report("do-nothing gasnet_try_syncnb_some() (8 handles)",TIME() - start, iters);
    }

    BARRIER();

    { 
      int64_t start = TIME();
      for (i=0; i < iters; i++) {
        gasnet_wait_syncnbi_all();          
      }
      report("do-nothing gasnet_wait_syncnbi_all()",TIME() - start, iters);
    }

    BARRIER();

    {
      int64_t start = TIME();
      for (i=0; i < iters; i++) {
        gasnet_wait_syncnbi_puts();          
      }
      report("do-nothing gasnet_wait_syncnbi_puts()",TIME() - start, iters);
    }

    BARRIER();

    {
      int64_t start = TIME();
      for (i=0; i < iters; i++) {
        gasnet_wait_syncnbi_gets();          
      }
      report("do-nothing gasnet_wait_syncnbi_gets()",TIME() - start, iters);
    }

    BARRIER();

    {
      int64_t start = TIME();
      for (i=0; i < iters; i++) {
        gasnet_try_syncnbi_all();          
      }
      report("do-nothing gasnet_try_syncnbi_all()",TIME() - start, iters);
    }

    BARRIER();

    {
      int64_t start = TIME();
      for (i=0; i < iters; i++) {
        gasnet_try_syncnbi_puts();          
      }
      report("do-nothing gasnet_try_syncnbi_puts()",TIME() - start, iters);
    }

    BARRIER();

    {
      int64_t start = TIME();
      for (i=0; i < iters; i++) {
        gasnet_try_syncnbi_gets();          
      }
      report("do-nothing gasnet_try_syncnbi_gets()",TIME() - start, iters);
    }

    BARRIER();

    {
      int64_t start = TIME();
      for (i=0; i < iters; i++) {
        gasnet_begin_nbi_accessregion();          
        gasnet_wait_syncnb(gasnet_end_nbi_accessregion());          
      }
      report("do-nothing begin/end nbi accessregion",TIME() - start, iters);
    }

    BARRIER();

    {
      int64_t start = TIME();
      for (i=0; i < iters; i++) {
        gasnete_barrier_notify(0,GASNET_BARRIERFLAG_ANONYMOUS);            
        gasnete_barrier_wait(0,GASNET_BARRIERFLAG_ANONYMOUS); 
      }
      report("single-node barrier",TIME() - start, iters);
      if (mynode == 0 && gasnet_nodes() > 1) {
        printf("Note: this is actually the barrier time for %i nodes, "
               "since you're running with more than one node.\n", (int)gasnet_nodes());
        fflush(stdout);
      }
    }

    BARRIER();

  }
  /* ------------------------------------------------------------------------------------ */


  MSG("done.");

  gasnet_exit(0);
  return 0;
}

/*   $Source: /Users/kamil/work/gasnet-cvs2/gasnet/ibv-conduit/gasnet_core_thread.c,v $
 *     $Date: 2012/03/06 02:20:33 $
 * $Revision: 1.1 $
 * Description: GASNet vapi/ibv conduit implementation, progress thread logic
 * Copyright 2012, LBNL
 * Terms of use are as specified in license.txt
 */

#include <gasnet_internal.h>
#include <gasnet_core_internal.h>

#include <errno.h>

#if !GASNETI_CONDUIT_THREADS

/* Protect against compiler warnings about "empty compilation unit" */
extern int gasnetc_thread_dummy = 1;

#else

GASNETI_INLINE(gasnetc_testcancel)
void gasnetc_testcancel(void) {
  const int save_errno = errno;
  pthread_testcancel();
  if_pf (GASNETC_IS_EXITING()) pthread_exit(NULL);
  errno = save_errno;
}

typedef struct {
  gasnetc_hca_hndl_t hca_hndl;
  gasnetc_cq_hndl_t cq_hndl;
  gasnetc_comp_handler_t compl_hndl;
  void (*fn)(gasnetc_wc_t *, void *);
  void *fn_arg;
} gasnetc_thread_closure_t;

static void * gasnetc_progress_thread(void *arg)
{
  gasnetc_thread_closure_t *args = (gasnetc_thread_closure_t *)arg;
  const gasnetc_hca_hndl_t hca_hndl         = args->hca_hndl;
  const gasnetc_cq_hndl_t cq_hndl           = args->cq_hndl;
  const gasnetc_comp_handler_t compl_hndl   = args->compl_hndl;
  void (* const fn)(gasnetc_wc_t *, void *) = args->fn;
  void * const fn_arg                       = args->fn_arg;

  while (1) {
    gasnetc_wc_t comp;
    int rc;

  #if GASNET_CONDUIT_VAPI
    rc = EVAPI_poll_cq_block(hca_hndl, cq_hndl, 0, &comp);
  #else
    rc = ibv_poll_cq(cq_hndl, 1, &comp);
  #endif
    gasnetc_testcancel();
    if (rc == GASNETC_POLL_CQ_OK) {
      gasneti_assert((comp.opcode == GASNETC_WC_RECV) ||
		     (comp.status != GASNETC_WC_SUCCESS));
      (fn)(&comp, fn_arg);
    } else if (rc == GASNETC_POLL_CQ_EMPTY) {
    #if GASNET_CONDUIT_VAPI
      continue; /* false wake up - loop again */
    #else
      gasnetc_cq_hndl_t the_cq;
      void *the_ctx;

      /* block for event on the empty CQ */
      rc = ibv_get_cq_event(compl_hndl, &the_cq, &the_ctx);
      gasnetc_testcancel();
      GASNETC_VAPI_CHECK(rc, "while blocked for CQ event");
      gasneti_assert(the_cq == cq_hndl);

      /* ack the event and rearm */
      ibv_ack_cq_events(cq_hndl, 1);
      rc = ibv_req_notify_cq(cq_hndl, 0);
      GASNETC_VAPI_CHECK(rc, "while requesting CQ events");

      /* loop to poll for the new completion */
    #endif
    } else {
      gasneti_fatalerror("failed CQ poll an async thread");
    }
  }
}

extern void
gasnetc_create_progress_thread(pthread_t *id_p,
                               gasnetc_hca_hndl_t hca_hndl,
                               gasnetc_cq_hndl_t cq_hndl,
                               gasnetc_comp_handler_t compl_hndl,
		               void (*fn)(gasnetc_wc_t *, void *),
                               void *fn_arg)
{
  gasnetc_thread_closure_t *args = gasneti_malloc(sizeof(gasnetc_thread_closure_t));
  pthread_attr_t attr;

  args->hca_hndl   = hca_hndl;
  args->cq_hndl    = cq_hndl;
  args->compl_hndl = compl_hndl;
  args->fn         = fn;
  args->fn_arg     = fn_arg;

  pthread_attr_init(&attr);
  (void)pthread_attr_setscope(&attr, PTHREAD_SCOPE_SYSTEM); /* ignore failures */
  gasneti_assert_zeroret(pthread_create(id_p, &attr, gasnetc_progress_thread, args));
  gasneti_assert_zeroret(pthread_attr_destroy(&attr));
  gasneti_leak(args);
}
#endif

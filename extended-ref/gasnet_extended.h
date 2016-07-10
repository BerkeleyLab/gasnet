/*   $Source: bitbucket.org:berkeleylab/gasnet.git/extended-ref/gasnet_extended.h $
 * Description: GASNet Extended API Header
 * Copyright 2002, Dan Bonachea <bonachea@cs.berkeley.edu>
 * Terms of use are as specified in license.txt
 */

#ifndef _IN_GASNETEX_H
  #error This file is not meant to be included directly- clients should include gasnetex.h
#endif

#ifndef _GASNET_EXTENDED_H
#define _GASNET_EXTENDED_H

#include <string.h>

#include <gasnet_extended_help.h>
#include <gasnet_coll.h>

/*  TODO: add debug code to enforce restrictions on SEQ and PARSYNC config */
/*        (only one thread calls, HSL's only locked by that thread - how to check without pthread_getspecific()?) */
/* ------------------------------------------------------------------------------------ */
/*
  Initialization
  ==============
*/
/* passes back a pointer to a handler table containing the handlers of
    the extended API, which the core should register on its behalf
    (the table is terminated with an entry where fnptr == NULL)
   all handlers will have an index in range 100-199 
   may be called before gasnete_init()
*/
extern gasnet_handlerentry_t const *gasnete_get_handlertable(void);

/* Initialize the Extended API:
   must be called by the core API at the end of gasnet_attach() before calls to extended API
     (this function may make calls to the core functions)
*/
extern void gasnete_init(void);

/* ------------------------------------------------------------------------------------ */
/*
  Non-blocking memory-to-memory transfers (explicit handle)
  ==========================================================
*/

#ifndef gasnete_put_nb
  extern gasnetex_handle_t gasnete_put_nb(
                        gasnetex_team_member_t team,
                        gasnetex_rank_t rank, void *dest,
                        /*const*/ void *src,  // TODO-EX: un-comment const
                        size_t nbytes, gasnetex_lc_handle_t *lc_opt,
                        gasnetex_flags_t flags GASNETI_THREAD_FARG) GASNETI_WARN_UNUSED_RESULT;
#endif

#ifndef gasnete_get_nb
  extern gasnetex_handle_t gasnete_get_nb(
                        gasnetex_team_member_t team, void *dest,
                        gasnetex_rank_t rank, void *src,
                        size_t nbytes, gasnetex_flags_t flags
                        GASNETI_THREAD_FARG) GASNETI_WARN_UNUSED_RESULT;
#endif

GASNETI_INLINE(_gasnetex_get_nb) GASNETI_WARN_UNUSED_RESULT
gasnetex_handle_t _gasnetex_get_nb(
                        gasnetex_team_member_t team, void *dest,
                        gasnetex_rank_t rank, void *src,
                        size_t nbytes, gasnetex_flags_t flags
                        GASNETI_THREAD_FARG) {
  GASNETI_CHECKZEROSZ_GET(NB,H);
  if (gasnete_islocal(rank)) {
    GASNETI_TRACE_GET_LOCAL(NB,dest,rank,src,nbytes);
    GASNETE_FAST_ALIGNED_MEMCPY(dest, src, nbytes);
    gasnete_loopbackget_memsync();
    return GASNETEX_INVALID_HANDLE;
  } else {
    GASNETI_TRACE_GET(NB,dest,rank,src,nbytes);
    return gasnete_get_nb(team, dest, rank, src, nbytes, flags GASNETI_THREAD_PASS);
  }
}
#define gasnetex_get_nb(team,dest,rank,src,nbytes,flags) \
       _gasnetex_get_nb(team,dest,rank,src,nbytes,flags GASNETI_THREAD_GET)

GASNETI_INLINE(_gasnetex_put_nb) GASNETI_WARN_UNUSED_RESULT
gasnetex_handle_t _gasnetex_put_nb(
                        gasnetex_team_member_t team,
                        gasnetex_rank_t rank, void *dest,
                        /*const*/ void *src,  // TODO-EX: un-comment const
                        size_t nbytes, gasnetex_lc_handle_t *lc_opt,
                        gasnetex_flags_t flags GASNETI_THREAD_FARG) {
  GASNETI_CHECKZEROSZ_PUT(NB,H);
  if (gasnete_islocal(rank)) {
    GASNETI_TRACE_PUT_LOCAL(NB,rank,dest,src,nbytes);
    GASNETE_FAST_ALIGNED_MEMCPY(dest, src, nbytes);
    gasnete_loopbackput_memsync();
    return GASNETEX_INVALID_HANDLE;
  } else {
    GASNETI_TRACE_PUT(NB,rank,dest,src,nbytes);
    return gasnete_put_nb(team, rank, dest, src, nbytes, lc_opt, flags GASNETI_THREAD_PASS);
  }
}
#define gasnetex_put_nb(team,rank,dest,src,nbytes,lc_opt,flags) \
       _gasnetex_put_nb(team,rank,dest,src,nbytes,lc_opt,flags GASNETI_THREAD_GET)

/* ------------------------------------------------------------------------------------ */
/*
  Synchronization for explicit-handle non-blocking operations:
  ===========================================================
*/

#ifndef gasnete_test_syncnb
extern int gasnete_test_syncnb(gasnetex_handle_t handle);
#endif
#ifndef gasnete_test_syncnb_some
extern int gasnete_test_syncnb_some(gasnetex_handle_t *phandle, size_t numhandles);
#endif
#ifndef gasnete_test_syncnb_all
extern int gasnete_test_syncnb_all (gasnetex_handle_t *phandle, size_t numhandles);
#endif


GASNETI_INLINE(gasnetex_test_syncnb) GASNETI_WARN_UNUSED_RESULT
int  gasnetex_test_syncnb(gasnetex_handle_t handle) {
  int result = GASNET_OK;
  if_pt (handle != GASNETEX_INVALID_HANDLE)
    result = gasnete_test_syncnb(handle);
  GASNETI_TRACE_TRYSYNC(TEST_SYNCNB,result);
  return result;
}

GASNETI_INLINE(gasnetex_test_syncnb_some)
int gasnetex_test_syncnb_some(gasnetex_handle_t *phandle, size_t numhandles) {
  int result = gasnete_test_syncnb_some(phandle,numhandles);
  GASNETI_TRACE_TRYSYNC(TEST_SYNCNB_SOME,result);
  return result;
}

GASNETI_INLINE(gasnetex_test_syncnb_all)
int gasnetex_test_syncnb_all(gasnetex_handle_t *phandle, size_t numhandles) {
  int result = gasnete_test_syncnb_all(phandle,numhandles);
  GASNETI_TRACE_TRYSYNC(TEST_SYNCNB_ALL,result);
  return result;
}


#ifndef gasnete_wait_syncnb
  #define gasnete_wait_syncnb(handle) do {                                      \
      gasnetex_handle_t _handle = (handle);                                     \
      if_pt (_handle != GASNETEX_INVALID_HANDLE) {                              \
        gasneti_AMPoll(); /* Ensure at least one poll - TODO: remove? */        \
        gasneti_pollwhile(gasnete_test_syncnb(_handle) == GASNET_ERR_NOT_READY);\
      }                                                                         \
    } while(0)
#endif

GASNETI_INLINE(gasnetex_wait_syncnb)
void gasnetex_wait_syncnb(gasnetex_handle_t handle) {
  GASNETI_TRACE_WAITSYNC_BEGIN();
  gasnete_wait_syncnb(handle);
  GASNETI_TRACE_WAITSYNC_END(WAIT_SYNCNB);
}

#ifndef gasnete_wait_syncnb_some
  #define gasnete_wait_syncnb_some(phandle, numhandles) do {                                   \
      gasneti_AMPoll(); /* Ensure at least one poll - TODO: remove? */                         \
      gasneti_pollwhile(gasnete_test_syncnb_some(phandle, numhandles) == GASNET_ERR_NOT_READY);\
    } while(0)
#endif

GASNETI_INLINE(gasnetex_wait_syncnb_some)
void gasnetex_wait_syncnb_some(gasnetex_handle_t *phandle, size_t numhandles) {
  GASNETI_TRACE_WAITSYNC_BEGIN();
  gasnete_wait_syncnb_some(phandle, numhandles);
  GASNETI_TRACE_WAITSYNC_END(WAIT_SYNCNB_SOME);
}

#ifndef gasnete_wait_syncnb_all
  #define gasnete_wait_syncnb_all(phandle, numhandles) do {                                   \
      gasneti_AMPoll(); /* Ensure at least one poll - TODO: remove? */                        \
      gasneti_pollwhile(gasnete_test_syncnb_all(phandle, numhandles) == GASNET_ERR_NOT_READY);\
    } while(0)
#endif

GASNETI_INLINE(gasnetex_wait_syncnb_all)
void gasnetex_wait_syncnb_all(gasnetex_handle_t *phandle, size_t numhandles) {
  GASNETI_TRACE_WAITSYNC_BEGIN();
  gasnete_wait_syncnb_all(phandle, numhandles);
  GASNETI_TRACE_WAITSYNC_END(WAIT_SYNCNB_ALL);
}

/* ------------------------------------------------------------------------------------ */
/*
  Operations on local-completion handles
  ======================================
*/

#ifndef gasnete_test_lc
extern int gasnete_test_lc(gasnetex_lc_handle_t lchandle);
#endif
#ifndef gasnete_test_lc_some
extern int gasnete_test_lc_some(gasnetex_lc_handle_t *plchandle, size_t numlchandles);
#endif
#ifndef gasnete_test_lc_all
extern int gasnete_test_lc_all (gasnetex_lc_handle_t *plchandle, size_t numlchandles);
#endif


GASNETI_INLINE(gasnetex_test_lc) GASNETI_WARN_UNUSED_RESULT
int gasnetex_test_lc(gasnetex_lc_handle_t lchandle) {
  int result = GASNET_OK;
  if_pt (lchandle != GASNETEX_INVALID_LC_HANDLE)
    result = gasnete_test_lc(lchandle);
  GASNETI_TRACE_TRYSYNC(TEST_LC,result);
  return result;
}

GASNETI_INLINE(gasnetex_test_lc_some)
int gasnetex_test_lc_some(gasnetex_lc_handle_t *plchandle, size_t numlchandles) {
  int result = gasnete_test_lc_some(plchandle,numlchandles);
  GASNETI_TRACE_TRYSYNC(TEST_LC_SOME,result);
  return result;
}

GASNETI_INLINE(gasnetex_test_lc_all)
int gasnetex_test_lc_all(gasnetex_lc_handle_t *plchandle, size_t numlchandles) {
  int result = gasnete_test_lc_all(plchandle,numlchandles);
  GASNETI_TRACE_TRYSYNC(TEST_LC_ALL,result);
  return result;
}


#ifndef gasnete_wait_lc
  #define gasnete_wait_lc(lchandle) do {                                      \
      gasnetex_lc_handle_t _lchandle = (lchandle);                            \
      if_pt (_lchandle != GASNETEX_INVALID_LC_HANDLE) {                       \
        gasneti_AMPoll(); /* Ensure at least one poll - TODO: remove? */      \
        gasneti_pollwhile(gasnete_test_lc(_lchandle) == GASNET_ERR_NOT_READY);\
      }                                                                       \
    } while(0)
#endif

GASNETI_INLINE(gasnetex_wait_lc)
void gasnetex_wait_lc(gasnetex_lc_handle_t lchandle) {
  GASNETI_TRACE_WAITSYNC_BEGIN();
  gasnete_wait_lc(lchandle);
  GASNETI_TRACE_WAITSYNC_END(WAIT_LC);
}

#ifndef gasnete_wait_lc_some
  #define gasnete_wait_lc_some(plchandle, numlchandles) do {                                   \
      gasneti_AMPoll(); /* Ensure at least one poll - TODO: remove? */                         \
      gasneti_pollwhile(gasnete_test_lc_some(plchandle, numlchandles) == GASNET_ERR_NOT_READY);\
    } while(0)
#endif

GASNETI_INLINE(gasnetex_wait_lc_some)
void gasnetex_wait_lc_some(gasnetex_lc_handle_t *plchandle, size_t numlchandles) {
  GASNETI_TRACE_WAITSYNC_BEGIN();
  gasnete_wait_lc_some(plchandle, numlchandles);
  GASNETI_TRACE_WAITSYNC_END(WAIT_LC_SOME);
}

#ifndef gasnete_wait_lc_all
  #define gasnete_wait_lc_all(plchandle, numlchandles) do {                                   \
      gasneti_AMPoll(); /* Ensure at least one poll - TODO: remove? */                        \
      gasneti_pollwhile(gasnete_test_lc_all(plchandle, numlchandles) == GASNET_ERR_NOT_READY);\
    } while(0)
#endif

GASNETI_INLINE(gasnetex_wait_lc_all)
void gasnetex_wait_lc_all(gasnetex_lc_handle_t *plchandle, size_t numlchandles) {
  GASNETI_TRACE_WAITSYNC_BEGIN();
  gasnete_wait_lc_all(plchandle, numlchandles);
  GASNETI_TRACE_WAITSYNC_END(WAIT_LC_ALL);
}


#ifndef gasnete_test_lc_group
extern int gasnete_test_lc_group (GASNETI_THREAD_FARG_ALONE);
#endif

GASNETI_INLINE(_gasnetex_test_lc_group) GASNETI_WARN_UNUSED_RESULT
int _gasnetex_test_lc_group(GASNETI_THREAD_FARG_ALONE) {
  int retval = gasnete_test_lc_group(GASNETI_THREAD_PASS_ALONE);
  GASNETI_TRACE_TRYSYNC(TEST_LC_GROUP,retval);
  return retval;
}
#define gasnetex_test_lc_group()   \
       _gasnetex_test_lc_group(GASNETI_THREAD_GET_ALONE)

#ifndef gasnete_wait_lc_group
  #define gasnete_wait_lc_group \
    gasneti_pollwhile(gasnete_test_lc_group(GASNETI_THREAD_GET_ALONE) == GASNET_ERR_NOT_READY) \
    GASNETI_THREAD_SWALLOW
#endif

#define gasnetex_wait_lc_group() do {                                                        \
  GASNETI_TRACE_WAITSYNC_BEGIN();                                                            \
  gasneti_AMPoll(); /* ensure at least one poll */                                           \
  gasnete_wait_lc_group(GASNETI_THREAD_GET_ALONE);                                           \
  GASNETI_TRACE_WAITSYNC_END(WAIT_LC_GROUP);                                                 \
  } while (0)


/* ------------------------------------------------------------------------------------ */
/*
  Non-blocking memory-to-memory transfers (implicit handle)
  ==========================================================
*/

#ifndef gasnete_get_nbi
extern int gasnete_get_nbi  (gasnetex_team_member_t team, void *dest,
                             gasnetex_rank_t rank, void *src,
                             size_t nbytes, gasnetex_flags_t flags
                             GASNETI_THREAD_FARG);
#endif

#ifndef gasnete_put_nbi
extern int gasnete_put_nbi  (gasnetex_team_member_t team,
                             gasnetex_rank_t rank, void *dest,
                             /*const*/ void *src,  // TODO-EX: un-comment const
                             size_t nbytes, gasnetex_lc_handle_t *lc_opt,
                             gasnetex_flags_t flags GASNETI_THREAD_FARG);
#endif

GASNETI_INLINE(_gasnetex_get_nbi)
int _gasnetex_get_nbi  (gasnetex_team_member_t team, void *dest,
                        gasnetex_rank_t rank, void *src,
                        size_t nbytes, gasnetex_flags_t flags
                        GASNETI_THREAD_FARG) {
  GASNETI_CHECKZEROSZ_GET(NBI,I);
  if (gasnete_islocal(rank)) {
    GASNETI_TRACE_GET_LOCAL(NBI,dest,rank,src,nbytes);
    GASNETE_FAST_ALIGNED_MEMCPY(dest, src, nbytes);
    gasnete_loopbackget_memsync();
    return 0;
  } else {
    GASNETI_TRACE_GET(NBI,dest,rank,src,nbytes);
    return gasnete_get_nbi(team, dest, rank, src, nbytes, flags GASNETI_THREAD_PASS);
  }
}
#define gasnetex_get_nbi(team,dest,rank,src,nbytes,flags) \
       _gasnetex_get_nbi(team,dest,rank,src,nbytes,flags GASNETI_THREAD_GET)

GASNETI_INLINE(_gasnetex_put_nbi)
int _gasnetex_put_nbi  (gasnetex_team_member_t team,
                        gasnetex_rank_t rank, void *dest,
                        /*const*/ void *src,  // TODO-EX: un-comment const
                        size_t nbytes, gasnetex_lc_handle_t *lc_opt,
                        gasnetex_flags_t flags GASNETI_THREAD_FARG) {
  GASNETI_CHECKZEROSZ_PUT(NBI,I);
  if (gasnete_islocal(rank)) {
    GASNETI_TRACE_PUT_LOCAL(NBI,rank,dest,src,nbytes);
    GASNETE_FAST_ALIGNED_MEMCPY(dest, src, nbytes);
    gasnete_loopbackput_memsync();
    return 0;
  } else {
    GASNETI_TRACE_PUT(NBI,rank,dest,src,nbytes);
    return gasnete_put_nbi(team, rank, dest, src, nbytes, lc_opt, flags GASNETI_THREAD_PASS);
  }
}
#define gasnetex_put_nbi(team,rank,dest,src,nbytes,lc_opt,flags) \
       _gasnetex_put_nbi(team,rank,dest,src,nbytes,lc_opt,flags GASNETI_THREAD_GET)

/* ------------------------------------------------------------------------------------ */
/*
  Synchronization for implicit-handle non-blocking operations:
  ===========================================================
*/

#ifndef gasnete_test_syncnbi_gets
  extern int  gasnete_test_syncnbi_gets(GASNETI_THREAD_FARG_ALONE);
#endif
#ifndef gasnete_test_syncnbi_puts
  extern int  gasnete_test_syncnbi_puts(GASNETI_THREAD_FARG_ALONE);
#endif


GASNETI_INLINE(_gasnetex_test_syncnbi_gets) GASNETI_WARN_UNUSED_RESULT
int _gasnetex_test_syncnbi_gets(GASNETI_THREAD_FARG_ALONE) {
  int retval = gasnete_test_syncnbi_gets(GASNETI_THREAD_PASS_ALONE);
  GASNETI_TRACE_TRYSYNC(TEST_SYNCNBI_GETS,retval);
  return retval;
}
#define gasnetex_test_syncnbi_gets()   \
       _gasnetex_test_syncnbi_gets(GASNETI_THREAD_GET_ALONE)

GASNETI_INLINE(_gasnetex_test_syncnbi_puts) GASNETI_WARN_UNUSED_RESULT
int _gasnetex_test_syncnbi_puts(GASNETI_THREAD_FARG_ALONE) {
  int retval = gasnete_test_syncnbi_puts(GASNETI_THREAD_PASS_ALONE);
  GASNETI_TRACE_TRYSYNC(TEST_SYNCNBI_PUTS,retval);
  return retval;
}
#define gasnetex_test_syncnbi_puts()   \
       _gasnetex_test_syncnbi_puts(GASNETI_THREAD_GET_ALONE)

#ifndef gasnete_test_syncnbi_all
  // NOTE: This implementation assumes that syncnbi_puts() includes lc_group().
  // Conduits with true LC that don't have that property should define a
  // replacement gasnete_test_syncnbi_all() macro which also includes
  //   "gasnete_test_lc_group(GASNETI_THREAD_PASS_ALONE)"
  // TODO-EX: should we provide a knob "GASNETE_SYNCNBI_PUTS_INCLUDES_LC" ?
  #define gasnete_test_syncnbi_all                                               \
   ((gasnete_test_syncnbi_gets(GASNETI_THREAD_PASS_ALONE) == GASNET_OK &&        \
     gasnete_test_syncnbi_puts(GASNETI_THREAD_PASS_ALONE) == GASNET_OK)          \
                                       ? GASNET_OK : GASNET_ERR_NOT_READY)       \
    GASNETI_THREAD_SWALLOW
#endif

GASNETI_INLINE(_gasnetex_test_syncnbi_all) GASNETI_WARN_UNUSED_RESULT
int _gasnetex_test_syncnbi_all(GASNETI_THREAD_FARG_ALONE) {
  int retval = gasnete_test_syncnbi_all(GASNETI_THREAD_PASS_ALONE);
  GASNETI_TRACE_TRYSYNC(TEST_SYNCNBI_ALL,retval);
  return retval;
}
#define gasnetex_test_syncnbi_all()   \
       _gasnetex_test_syncnbi_all(GASNETI_THREAD_GET_ALONE)


#ifndef gasnete_wait_syncnbi_gets
  #define gasnete_wait_syncnbi_gets \
    gasneti_pollwhile(gasnete_test_syncnbi_gets(GASNETI_THREAD_GET_ALONE) == GASNET_ERR_NOT_READY) \
    GASNETI_THREAD_SWALLOW
#endif

#define gasnetex_wait_syncnbi_gets() do {                                                        \
  GASNETI_TRACE_WAITSYNC_BEGIN();                                                                \
  gasneti_AMPoll(); /* ensure at least one poll */                                               \
  gasnete_wait_syncnbi_gets(GASNETI_THREAD_GET_ALONE);                                           \
  GASNETI_TRACE_WAITSYNC_END(WAIT_SYNCNBI_GETS);                                                 \
  } while (0)

#ifndef gasnete_wait_syncnbi_puts
  #define gasnete_wait_syncnbi_puts \
    gasneti_pollwhile(gasnete_test_syncnbi_puts(GASNETI_THREAD_GET_ALONE) == GASNET_ERR_NOT_READY) \
    GASNETI_THREAD_SWALLOW
#endif

#define gasnetex_wait_syncnbi_puts() do {                                                        \
  GASNETI_TRACE_WAITSYNC_BEGIN();                                                                \
  gasneti_AMPoll(); /* ensure at least one poll */                                               \
  gasnete_wait_syncnbi_puts(GASNETI_THREAD_GET_ALONE);                                           \
  GASNETI_TRACE_WAITSYNC_END(WAIT_SYNCNBI_PUTS);                                                 \
  } while (0)

#ifndef gasnete_wait_syncnbi_all
  // NOTE: This implementation assumes that syncnbi_puts() includes lc_group().
  // See the note with gasnete_test_syncnbi_all() for more info.
  #define gasnete_wait_syncnbi_all do {                                                     \
    gasneti_pollwhile(gasnete_test_syncnbi_gets(GASNETI_THREAD_GET_ALONE) == GASNET_ERR_NOT_READY); \
    gasneti_pollwhile(gasnete_test_syncnbi_puts(GASNETI_THREAD_GET_ALONE) == GASNET_ERR_NOT_READY); \
  } while (0) GASNETI_THREAD_SWALLOW
#endif

#define gasnetex_wait_syncnbi_all() do {                                                         \
  GASNETI_TRACE_WAITSYNC_BEGIN();                                                                \
  gasneti_AMPoll(); /* ensure at least one poll */                                               \
  gasnete_wait_syncnbi_all(GASNETI_THREAD_GET_ALONE);                                            \
  GASNETI_TRACE_WAITSYNC_END(WAIT_SYNCNBI_ALL);                                                  \
  } while (0)
        
/* ------------------------------------------------------------------------------------ */
/*
  Implicit access region synchronization
  ======================================
*/
#ifndef gasnete_begin_nbi_accessregion
extern void gasnete_begin_nbi_accessregion(gasnetex_flags_t flags, int allowrecursion GASNETI_THREAD_FARG);
#endif
#ifndef gasnete_end_nbi_accessregion
extern gasnetex_handle_t gasnete_end_nbi_accessregion(gasnetex_lc_handle_t *lc_opt, gasnetex_flags_t flags GASNETI_THREAD_FARG) GASNETI_WARN_UNUSED_RESULT;
#endif

#define gasnetex_begin_nbi_accessregion(flags) gasnete_begin_nbi_accessregion(flags,0 GASNETI_THREAD_GET)
#define gasnetex_end_nbi_accessregion(lc_opt,flags)   gasnete_end_nbi_accessregion(lc_opt,flags GASNETI_THREAD_GET)

/* ------------------------------------------------------------------------------------ */
/*
  Blocking memory-to-memory transfers
  ===================================
*/


#if GASNETI_DIRECT_BLOCKING_GET
  extern int gasnete_get  (gasnetex_team_member_t team,
                           void *dest,
                           gasnetex_rank_t rank, void *src,
                           size_t nbytes, gasnetex_flags_t flags
                           GASNETI_THREAD_FARG);
#elif !defined(gasnete_get)
  GASNETI_INLINE(gasnete_get)
  int gasnete_get (gasnetex_team_member_t team,
                    void *dest,
                    gasnetex_rank_t rank, void *src,
                    size_t nbytes, gasnetex_flags_t flags
                    GASNETI_THREAD_FARG)
  {
    gasnetex_handle_t h = gasnete_get_nb(team, dest, rank, src, nbytes, flags GASNETI_THREAD_PASS);
    if (h == GASNETEX_NO_OP_HANDLE) return 1;
    else gasnete_wait_syncnb(h);
    return 0;
  }
#endif

#if GASNETI_DIRECT_BLOCKING_PUT
  extern int gasnete_put  (gasnetex_team_member_t team,
                           gasnetex_rank_t rank, void* dest,
                           /*const*/ void *src, // TODO-EX: uncomment const
                           size_t nbytes, gasnetex_flags_t flags
                           GASNETI_THREAD_FARG);
#elif !defined(gasnete_put)
  GASNETI_INLINE(gasnete_put)
  int gasnete_put  (gasnetex_team_member_t team,
                    gasnetex_rank_t rank, void* dest,
                    /*const*/ void *src, // TODO-EX: uncomment const
                    size_t nbytes, gasnetex_flags_t flags
                    GASNETI_THREAD_FARG)
  {
    gasnetex_handle_t h = gasnete_put_nb(team, rank, dest, src, nbytes, GASNETEX_LC_SYNC, flags GASNETI_THREAD_PASS);
    if (h == GASNETEX_NO_OP_HANDLE) return 1;
    else gasnete_wait_syncnb(h);
    return 0;
  }
#endif

GASNETI_INLINE(_gasnetex_get)
int _gasnetex_get  (gasnetex_team_member_t team, void *dest,
                    gasnetex_rank_t rank, void *src,
                    size_t nbytes, gasnetex_flags_t flags
                    GASNETI_THREAD_FARG) {
  GASNETI_CHECKZEROSZ_NAMED(GASNETI_TRACE_GET_NAMED(GET_LOCAL,LOCAL,dest,rank,src,nbytes),I);
  if (gasnete_islocal(rank)) {
    GASNETI_TRACE_GET_NAMED(GET_LOCAL,LOCAL,dest,rank,src,nbytes);
    GASNETE_FAST_ALIGNED_MEMCPY(dest, src, nbytes);
    gasnete_loopbackget_memsync();
    return 0;
  } else {
    GASNETI_TRACE_GET_NAMED(GET,NONLOCAL,dest,rank,src,nbytes);
    return gasnete_get(team, dest, rank, src, nbytes, flags GASNETI_THREAD_PASS);
  }
}
#define gasnetex_get(team,dest,rank,src,nbytes,flags) \
       _gasnetex_get(team,dest,rank,src,nbytes,flags GASNETI_THREAD_GET)

GASNETI_INLINE(_gasnetex_put)
int _gasnetex_put  (gasnetex_team_member_t team,
                    gasnetex_rank_t rank, void *dest,
                    /*const*/ void *src,  // TODO-EX: un-comment const
                    size_t nbytes, gasnetex_flags_t flags
                    GASNETI_THREAD_FARG) {
  GASNETI_CHECKZEROSZ_NAMED(GASNETI_TRACE_PUT_NAMED(PUT_LOCAL,LOCAL,rank,dest,src,nbytes),I);
  if (gasnete_islocal(rank)) {
    GASNETI_TRACE_PUT_NAMED(PUT_LOCAL,LOCAL,rank,dest,src,nbytes);
    GASNETE_FAST_MEMCPY(dest, src, nbytes);
    gasnete_loopbackput_memsync();
    return 0;
  } else {
    GASNETI_TRACE_PUT_NAMED(PUT,NONLOCAL,rank,dest,src,nbytes);
    return gasnete_put(team, rank, dest, src, nbytes, flags GASNETI_THREAD_PASS);
  }
}
#define gasnetex_put(team,rank,dest,src,nbytes,flags) \
       _gasnetex_put(team,rank,dest,src,nbytes,flags GASNETI_THREAD_GET)

/* ------------------------------------------------------------------------------------ */
/*
  Value Put
  =========
*/

#if GASNETI_DIRECT_PUT_VAL
  extern int gasnete_put_val(
                        gasnetex_team_member_t team,
                        gasnetex_rank_t rank, void *dest,
                        gasnetex_register_value_t value,
                        size_t nbytes, gasnetex_flags_t flags
                        GASNETI_THREAD_FARG);
#elif !defined(gasnete_put_val)
  GASNETI_INLINE(gasnete_put_val)
  int gasnete_put_val( gasnetex_team_member_t team,
                        gasnetex_rank_t rank, void *dest,
                        gasnetex_register_value_t value,
                        size_t nbytes, gasnetex_flags_t flags
                        GASNETI_THREAD_FARG)
  {
    gasnetex_register_value_t src = value;
    return gasnete_put(team, rank, dest, GASNETE_STARTOFBITS(&src,nbytes),
                       nbytes, flags GASNETI_THREAD_PASS);
  }
#endif

GASNETI_INLINE(_gasnetex_put_val)
int _gasnetex_put_val(  gasnetex_team_member_t team,
                        gasnetex_rank_t rank, void *dest,
                        gasnetex_register_value_t value,
                        size_t nbytes, gasnetex_flags_t flags
                        GASNETI_THREAD_FARG)
{
  gasneti_assert(nbytes > 0 && nbytes <= sizeof(gasnetex_register_value_t));
  if (gasnete_islocal(rank)) {
    GASNETI_TRACE_PUT_LOCAL(VAL,rank,dest,&value,nbytes);
    GASNETE_VALUE_ASSIGN(dest, value, nbytes);
    gasnete_loopbackput_memsync();
    return 0;
  } else {
    GASNETI_TRACE_PUT(VAL,rank,dest,GASNETE_STARTOFBITS(&value,nbytes),nbytes);
    return gasnete_put_val(team, rank, dest, value, nbytes, flags GASNETI_THREAD_PASS);
  }
}
#define gasnetex_put_val(team,rank,dest,value,nbytes,flags) \
       _gasnetex_put_val(team,rank,dest,value,nbytes,flags GASNETI_THREAD_GET)

#if GASNETI_DIRECT_PUT_NB_VAL && !defined(gasnete_put_nb_val)
  extern gasnetex_handle_t gasnete_put_nb_val(
                        gasnetex_team_member_t team,
                        gasnetex_rank_t rank, void *dest,
                        gasnetex_register_value_t value,
                        size_t nbytes, gasnetex_flags_t flags
                        GASNETI_THREAD_FARG); GASNETI_WARN_UNUSED_RESULT;
#endif

GASNETI_INLINE(_gasnetex_put_nb_val) GASNETI_WARN_UNUSED_RESULT
gasnetex_handle_t _gasnetex_put_nb_val (
                        gasnetex_team_member_t team,
                        gasnetex_rank_t rank, void *dest,
                        gasnetex_register_value_t value,
                        size_t nbytes, gasnetex_flags_t flags
                        GASNETI_THREAD_FARG)
{
  gasneti_assert(nbytes > 0 && nbytes <= sizeof(gasnetex_register_value_t));
  if (gasnete_islocal(rank)) {
    GASNETI_TRACE_PUT_LOCAL(NB_VAL,rank,dest,&value,nbytes);
    GASNETE_VALUE_ASSIGN(dest, value, nbytes);
    gasnete_loopbackput_memsync();
    return GASNETEX_INVALID_HANDLE;
  } else {
    GASNETI_TRACE_PUT(NB_VAL,rank,dest,GASNETE_STARTOFBITS(&value,nbytes),nbytes);
    #if GASNETI_DIRECT_PUT_NB_VAL || defined(gasnete_put_nb_val)
      return gasnete_put_nb_val(team, rank, dest, value, nbytes, flags GASNETI_THREAD_PASS);
    #else
      { gasnetex_register_value_t src = value;
        return gasnete_put_nb(team, rank, dest, GASNETE_STARTOFBITS(&src,nbytes),
                              nbytes, GASNETEX_LC_INIT, flags GASNETI_THREAD_PASS);
      }
    #endif
  }
}
#define gasnetex_put_nb_val(team,rank,dest,value,nbytes,flags) \
       _gasnetex_put_nb_val(team,rank,dest,value,nbytes,flags GASNETI_THREAD_GET)

#if GASNETI_DIRECT_PUT_NBI_VAL
  extern int gasnete_put_nbi_val(
                        gasnetex_team_member_t team,
                        gasnetex_rank_t rank, void *dest,
                        gasnetex_register_value_t value,
                        size_t nbytes, gasnetex_flags_t flags
                        GASNETI_THREAD_FARG);
#elif !defined(gasnete_put_nbi_val)
  GASNETI_INLINE(gasnete_put_nbi_val)
  int gasnete_put_nbi_val(
                        gasnetex_team_member_t team,
                        gasnetex_rank_t rank, void *dest,
                        gasnetex_register_value_t value,
                        size_t nbytes, gasnetex_flags_t flags
                        GASNETI_THREAD_FARG)
  {
    gasnetex_register_value_t src = value;
    return gasnete_put_nbi(team, rank, dest, GASNETE_STARTOFBITS(&src,nbytes),
                           nbytes, GASNETEX_LC_INIT, flags GASNETI_THREAD_PASS);
  }
#endif

GASNETI_INLINE(_gasnetex_put_nbi_val)
int _gasnetex_put_nbi_val(
                        gasnetex_team_member_t team,
                        gasnetex_rank_t rank, void *dest,
                        gasnetex_register_value_t value,
                        size_t nbytes, gasnetex_flags_t flags
                        GASNETI_THREAD_FARG)
{
  gasneti_assert(nbytes > 0 && nbytes <= sizeof(gasnetex_register_value_t));
  if (gasnete_islocal(rank)) {
    GASNETI_TRACE_PUT_LOCAL(NBI_VAL,rank,dest,&value,nbytes);
    GASNETE_VALUE_ASSIGN(dest, value, nbytes);
    gasnete_loopbackput_memsync();
    return 0;
  } else {
    GASNETI_TRACE_PUT(NBI_VAL,rank,dest,GASNETE_STARTOFBITS(&value,nbytes),nbytes);
    return gasnete_put_nbi_val(team, rank, dest, value, nbytes, flags GASNETI_THREAD_PASS);
  }
}
#define gasnetex_put_nbi_val(team,rank,dest,value,nbytes,flags) \
       _gasnetex_put_nbi_val(team,rank,dest,value,nbytes,flags GASNETI_THREAD_GET)

/* ------------------------------------------------------------------------------------ */
/*
  Blocking Value Get
  ==================
*/

#if PLATFORM_COMPILER_SUN_C
  #pragma error_messages(off, E_END_OF_LOOP_CODE_NOT_REACHED)
#endif

#if !defined(gasnete_get_val) && GASNETI_DIRECT_GET_VAL
  extern gasnetex_register_value_t gasnete_get_val (
                  gasnetex_team_member_t team,
                  gasnetex_rank_t rank, void *src,
                  size_t nbytes, gasnetex_flags_t flags
                  GASNETI_THREAD_FARG);
#endif

GASNETI_INLINE(_gasnetex_get_val) GASNETI_WARN_UNUSED_RESULT
gasnetex_register_value_t _gasnetex_get_val (
                gasnetex_team_member_t team,
                gasnetex_rank_t rank, void *src,
                size_t nbytes, gasnetex_flags_t flags
                GASNETI_THREAD_FARG)
{
  if (gasnete_islocal(rank)) {
    GASNETI_TRACE_GET_LOCAL(VAL,NULL,rank,src,nbytes);
    GASNETE_VALUE_RETURN(src, nbytes);
  } else {
    GASNETI_TRACE_GET(VAL,NULL,rank,src,nbytes);
    #if GASNETI_DIRECT_GET_VAL || defined(gasnete_get_val)
      return gasnete_get_val(team, rank, src, nbytes, flags GASNETI_THREAD_PASS);
    #else
      { gasnetex_register_value_t val = 0;
        gasnete_get(team, GASNETE_STARTOFBITS(&val,nbytes), rank, src, nbytes, flags GASNETI_THREAD_PASS);
        return val;
      }
    #endif
  }
}
#define gasnetex_get_val(team,rank,src,nbytes,flags) \
       _gasnetex_get_val(team,rank,src,nbytes,flags GASNETI_THREAD_GET)

#if PLATFORM_COMPILER_SUN_C
  #pragma error_messages(default, E_END_OF_LOOP_CODE_NOT_REACHED)
#endif
/* ------------------------------------------------------------------------------------ */
/*
  Barriers:
  =========
*/


#ifndef GASNET_TEAM_ALL
extern gasnet_team_handle_t gasnete_coll_team_all;
#define GASNET_TEAM_ALL gasnete_coll_team_all
#endif

/*intialize the barriers for a given team*/
extern void gasnete_coll_barrier_init(gasnete_coll_team_t team, int barrier_type,
                                      gasnetex_rank_t *nodes, gasnetex_rank_t *supernodes);

/*initialize the barriers for GASNET_TEAM_ALL*/
extern void gasnete_barrier_init(void);


#if GASNETI_STATS_OR_TRACE
extern gasneti_tick_t gasnete_barrier_notifytime;
#endif

extern void gasnet_barrier_notify(int id, int flags);
extern int gasnet_barrier_wait(int id, int flags);
extern int gasnet_barrier_try(int id, int flags);
extern int gasnet_barrier(int id, int flags);
extern int gasnet_barrier_result(int *id);
/* ------------------------------------------------------------------------------------ */

// TODO-EX: remove these checks for conduits using legacy internal APIs
#if GASNETI_DIRECT_GET
  #error "out-of-date #define of GASNETI_DIRECT_GET"
#endif
#if GASNETI_DIRECT_GET_BULK
  #error "out-of-date #define of GASNETI_DIRECT_GET_BULK"
#elif defined(gasnete_get_bulk)
  #error "out-of-date #define of gasnete_get_bulk"
#endif
#if GASNETI_DIRECT_PUT
  #error "out-of-date #define of GASNETI_DIRECT_PUT"
#endif
#if GASNETI_DIRECT_PUT_BULK
  #error "out-of-date #define of GASNETI_DIRECT_PUT_BULK"
#elif defined(gasnete_put_bulk)
  #error "out-of-date #define of gasnete_put_bulk"
#endif
#if GASNETI_DIRECT_GET_NBI
  #error "out-of-date #define of GASNETI_DIRECT_GET_NBI"
#endif
#if GASNETI_DIRECT_GET_NB
  #error "out-of-date #define of GASNETI_DIRECT_GET_NB"
#endif
#if GASNETI_DIRECT_MEMSET
  #error "out-of-date #define of GASNETI_DIRECT_MEMSET"
#endif

#endif

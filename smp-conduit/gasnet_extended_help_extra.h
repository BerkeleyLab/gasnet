/*   $Source: bitbucket.org:berkeleylab/gasnet.git/smp-conduit/gasnet_extended_help_extra.h $
 * Description: GASNet Extended smp-specific Header
 * Terms of use are as specified in license.txt
 */

#ifndef _IN_GASNETEX_H
  #error This file is not meant to be included directly- clients should include gasnetex.h
#endif

#ifndef _GASNET_EXTENDED_HELP_EXTRA_H
#define _GASNET_EXTENDED_HELP_EXTRA_H

/* ------------------------------------------------------------------------------------ */
/*
  Non-blocking memory-to-memory transfers (explicit handle)
  =========================================================
 */

GASNETI_INLINE(gasnete_get_nb) GASNETI_WARN_UNUSED_RESULT
gasnetex_handle_t gasnete_get_nb(
                     gasnetex_team_member_t team,
                     void *dest,
                     gasnetex_rank_t rank, void *src,
                     size_t nbytes,
                     gasnetex_flags_t flags GASNETI_THREAD_FARG)
{
  GASNETI_CHECKPSHM_GET(H);
  gasneti_assert(0 && "Unreachable");
  return GASNETEX_INVALID_HANDLE;
}
#define gasnete_get_nb gasnete_get_nb

GASNETI_INLINE(gasnete_put_nb) GASNETI_WARN_UNUSED_RESULT
gasnetex_handle_t gasnete_put_nb(
                     gasnetex_team_member_t team,
                     gasnetex_rank_t rank, void *dest,
                     void *src,
                     size_t nbytes, gasnetex_handle_t *lc_opt,
                     gasnetex_flags_t flags GASNETI_THREAD_FARG)
{
  GASNETI_CHECKPSHM_PUT(H);
  gasneti_assert(0 && "Unreachable");
  return GASNETEX_INVALID_HANDLE;
}
#define gasnete_put_nb gasnete_put_nb

/* ------------------------------------------------------------------------------------ */
/*
  Synchronization for explicit-handle non-blocking operations:
  ===========================================================
*/

GASNETI_INLINE(gasnete_syncnb_one)
int gasnete_syncnb_one(gasnetex_handle_t handle)
{
  gasneti_assert(handle == GASNETEX_INVALID_HANDLE);
  gasneti_sync_reads();
  return GASNET_OK;
}
#define gasnete_test gasnete_syncnb_one
#define gasnete_wait gasnete_syncnb_one

GASNETI_INLINE(gasnete_syncnb_array)
int gasnete_syncnb_array(gasnetex_handle_t *phandle, size_t numhandles)
{
#if GASNET_DEBUG
  for (size_t i=0; i<numhandles; ++i)
    gasneti_assert(phandle[i] == GASNETEX_INVALID_HANDLE);
#endif
  gasneti_sync_reads();
  return GASNET_OK;
}
#define gasnete_test_some gasnete_syncnb_array
#define gasnete_test_all  gasnete_syncnb_array
#define gasnete_wait_some gasnete_syncnb_array
#define gasnete_wait_all  gasnete_syncnb_array

/* ------------------------------------------------------------------------------------ */
/*
  Non-blocking memory-to-memory transfers (implicit handle)
  ==========================================================
 */
   
GASNETI_INLINE(gasnete_get_nbi)
int gasnete_get_nbi (gasnetex_team_member_t team,
                     void *dest,
                     gasnetex_rank_t rank, void *src,
                     size_t nbytes,
                     gasnetex_flags_t flags GASNETI_THREAD_FARG)
{
  GASNETI_CHECKPSHM_GET(I);
  gasneti_assert(0 && "Unreachable");
  return 0;
}
#define gasnete_get_nbi gasnete_get_nbi

GASNETI_INLINE(gasnete_put_nbi)
int gasnete_put_nbi (gasnetex_team_member_t team,
                     gasnetex_rank_t rank, void *dest,
                     void *src,
                     size_t nbytes, gasnetex_handle_t *lc_opt,
                     gasnetex_flags_t flags GASNETI_THREAD_FARG)
{
  GASNETI_CHECKPSHM_PUT(I);
  gasneti_assert(0 && "Unreachable");
  return 0;
}
#define gasnete_put_nbi gasnete_put_nbi

/* ------------------------------------------------------------------------------------ */
/*
  Synchronization for implicit-handle non-blocking operations:
  ===========================================================
*/
GASNETI_INLINE(gasnete_syncnbi)
int gasnete_syncnbi(GASNETE_THREAD_FARG_ALONE)
{
  gasneti_sync_reads();
  return GASNET_OK;
}
#define gasnete_test_syncnbi_all  gasnete_syncnbi
#define gasnete_test_syncnbi_gets gasnete_syncnbi
#define gasnete_test_syncnbi_puts gasnete_syncnbi
#define gasnete_wait_syncnbi_all  gasnete_syncnbi
#define gasnete_wait_syncnbi_gets gasnete_syncnbi
#define gasnete_wait_syncnbi_puts gasnete_syncnbi

// TODO-EX: remove these and replace with ..._syncnbi(type)
#define gasnete_test_syncnbi_lc   gasnete_syncnbi
#define gasnete_wait_syncnbi_lc   gasnete_syncnbi

GASNETI_INLINE(gasnete_begin_nbi_accessregion)
void gasnete_begin_nbi_accessregion(gasnetex_flags_t flags, int allowrecursion GASNETI_THREAD_FARG)
{ /* empty */ }
#define gasnete_begin_nbi_accessregion gasnete_begin_nbi_accessregion

GASNETI_INLINE(gasnete_end_nbi_accessregion) GASNETI_WARN_UNUSED_RESULT
gasnetex_handle_t gasnete_end_nbi_accessregion(gasnetex_handle_t *lc_opt, gasnetex_flags_t flags GASNETI_THREAD_FARG)
{
  gasneti_assert(lc_opt != GASNETEX_EVENT_GROUP); // TODO-EX: allow this if we nest access region?
  if (lc_opt != NULL) gasneti_leaf_finish(lc_opt);
  return GASNETEX_INVALID_HANDLE;
}
#define gasnete_end_nbi_accessregion gasnete_end_nbi_accessregion

/* ------------------------------------------------------------------------------------ */
/*
  Value Put
  =========
*/

GASNETI_INLINE(gasnete_put_val)
int gasnete_put_val(
                gasnetex_team_member_t team,
                gasnetex_rank_t rank, void *dest,
                gasnetex_register_value_t value,
                size_t nbytes, gasnetex_flags_t flags
                GASNETI_THREAD_FARG)
{
  GASNETI_CHECKPSHM_PUTVAL(I);
  gasneti_assert(0 && "Unreachable");
}
#define gasnete_put_val gasnete_put_val

GASNETI_INLINE(gasnete_put_nb_val) GASNETI_WARN_UNUSED_RESULT
gasnetex_handle_t gasnete_put_nb_val(
                gasnetex_team_member_t team,
                gasnetex_rank_t rank, void *dest,
                gasnetex_register_value_t value,
                size_t nbytes, gasnetex_flags_t flags
                GASNETI_THREAD_FARG)
{
  GASNETI_CHECKPSHM_PUTVAL(H);
  gasneti_assert(0 && "Unreachable");
  return GASNETEX_INVALID_HANDLE;
}
#define gasnete_put_nb_val gasnete_put_nb_val

/* nbi is trivially identical to blocking */
#define gasnete_put_nbi_val gasnete_put_val

/* ------------------------------------------------------------------------------------ */
/*
  Blocking Value Get
  ==================
*/

GASNETI_INLINE(gasnete_get_val)
gasnetex_register_value_t gasnete_get_val(
                gasnetex_team_member_t team,
                gasnetex_rank_t rank, void *src,
                size_t nbytes, gasnetex_flags_t flags
                GASNETI_THREAD_FARG)
{
  GASNETI_CHECKPSHM_GETVAL();
  gasneti_assert(0 && "Unreachable");
  return 0;
}
#define gasnete_get_val gasnete_get_val

#endif

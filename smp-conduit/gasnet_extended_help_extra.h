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
  Non-blocking memory-to-memory transfers (explicit event)
  =========================================================
 */

GASNETI_INLINE(gasnete_get_nb) GASNETI_WARN_UNUSED_RESULT
gex_Event_t gasnete_get_nb(
                     gex_TM_t tm,
                     void *dest,
                     gex_Rank_t rank, void *src,
                     size_t nbytes,
                     gex_Flags_t flags GASNETI_THREAD_FARG)
{
  GASNETI_CHECKPSHM_GET(H);
  gasneti_assert(0 && "Unreachable");
  return GEX_EVENT_INVALID;
}
#define gasnete_get_nb gasnete_get_nb

GASNETI_INLINE(gasnete_put_nb) GASNETI_WARN_UNUSED_RESULT
gex_Event_t gasnete_put_nb(
                     gex_TM_t tm,
                     gex_Rank_t rank, void *dest,
                     void *src,
                     size_t nbytes, gex_Event_t *lc_opt,
                     gex_Flags_t flags GASNETI_THREAD_FARG)
{
  GASNETI_CHECKPSHM_PUT(H);
  gasneti_assert(0 && "Unreachable");
  return GEX_EVENT_INVALID;
}
#define gasnete_put_nb gasnete_put_nb

/* ------------------------------------------------------------------------------------ */
/*
  Synchronization for explicit-event non-blocking operations:
  ===========================================================
*/

GASNETI_INLINE(gasnete_syncnb_one)
int gasnete_syncnb_one(gex_Event_t event GASNETI_THREAD_FARG)
{
  gasneti_assert(event == GEX_EVENT_INVALID);
  gasneti_sync_reads();
  return GASNET_OK;
}
#define gasnete_test gasnete_syncnb_one
#define gasnete_wait gasnete_syncnb_one

GASNETI_INLINE(gasnete_syncnb_array)
int gasnete_syncnb_array(gex_Event_t *pevent, size_t numevents GASNETI_THREAD_FARG)
{
#if GASNET_DEBUG
  for (size_t i=0; i<numevents; ++i)
    gasneti_assert(pevent[i] == GEX_EVENT_INVALID);
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
  Non-blocking memory-to-memory transfers (implicit event)
  ==========================================================
 */
   
GASNETI_INLINE(gasnete_get_nbi)
int gasnete_get_nbi (gex_TM_t tm,
                     void *dest,
                     gex_Rank_t rank, void *src,
                     size_t nbytes,
                     gex_Flags_t flags GASNETI_THREAD_FARG)
{
  GASNETI_CHECKPSHM_GET(I);
  gasneti_assert(0 && "Unreachable");
  return 0;
}
#define gasnete_get_nbi gasnete_get_nbi

GASNETI_INLINE(gasnete_put_nbi)
int gasnete_put_nbi (gex_TM_t tm,
                     gex_Rank_t rank, void *dest,
                     void *src,
                     size_t nbytes, gex_Event_t *lc_opt,
                     gex_Flags_t flags GASNETI_THREAD_FARG)
{
  GASNETI_CHECKPSHM_PUT(I);
  gasneti_assert(0 && "Unreachable");
  return 0;
}
#define gasnete_put_nbi gasnete_put_nbi

/* ------------------------------------------------------------------------------------ */
/*
  Synchronization for implicit-event non-blocking operations:
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

// Note we must allow for the possibility that the arg has side-effects
GASNETI_INLINE(gasnete_syncnbi_mask)
int gasnete_syncnbi_mask(unsigned int event_mask, gex_Flags_t flags GASNETE_THREAD_FARG)
{
  gasneti_sync_reads();
  return GASNET_OK;
}
#define gasnete_test_syncnbi_mask gasnete_syncnbi_mask
#define gasnete_wait_syncnbi_mask gasnete_syncnbi_mask

GASNETI_INLINE(gasnete_begin_nbi_accessregion)
void gasnete_begin_nbi_accessregion(gex_Flags_t flags, int allowrecursion GASNETI_THREAD_FARG)
{ /* empty */ }
#define gasnete_begin_nbi_accessregion gasnete_begin_nbi_accessregion

GASNETI_INLINE(gasnete_end_nbi_accessregion) GASNETI_WARN_UNUSED_RESULT
gex_Event_t gasnete_end_nbi_accessregion(gex_Flags_t flags GASNETI_THREAD_FARG)
{ return GEX_EVENT_INVALID; }
#define gasnete_end_nbi_accessregion gasnete_end_nbi_accessregion

/* ------------------------------------------------------------------------------------ */
/*
  Value Put
  =========
*/

GASNETI_INLINE(gasnete_put_val)
int gasnete_put_val(
                gex_TM_t tm,
                gex_Rank_t rank, void *dest,
                gex_RMA_Value_t value,
                size_t nbytes, gex_Flags_t flags
                GASNETI_THREAD_FARG)
{
  GASNETI_CHECKPSHM_PUTVAL(I);
  gasneti_assert(0 && "Unreachable");
  return 0;
}
#define gasnete_put_val gasnete_put_val

GASNETI_INLINE(gasnete_put_nb_val) GASNETI_WARN_UNUSED_RESULT
gex_Event_t gasnete_put_nb_val(
                gex_TM_t tm,
                gex_Rank_t rank, void *dest,
                gex_RMA_Value_t value,
                size_t nbytes, gex_Flags_t flags
                GASNETI_THREAD_FARG)
{
  GASNETI_CHECKPSHM_PUTVAL(H);
  gasneti_assert(0 && "Unreachable");
  return GEX_EVENT_INVALID;
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
gex_RMA_Value_t gasnete_get_val(
                gex_TM_t tm,
                gex_Rank_t rank, void *src,
                size_t nbytes, gex_Flags_t flags
                GASNETI_THREAD_FARG)
{
  GASNETI_CHECKPSHM_GETVAL();
  gasneti_assert(0 && "Unreachable");
  return 0;
}
#define gasnete_get_val gasnete_get_val

#endif

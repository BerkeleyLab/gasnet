/*   $Source: bitbucket.org:berkeleylab/gasnet.git/gasnet.h $
 * Description: GASNet -> GASNet-EX transitional header
 * Copyright 2016, The Regents of the University of California
 * Terms of use are as specified in license.txt
 */

#ifndef _GASNET_H
#define _GASNET_H
#define _INCLUDED_GASNET_H

#include <gasnetex.h>
#include <gasnet_tools.h>

GASNETT_BEGIN_EXTERNC
GASNETI_BEGIN_NOWARN

/* ------------------------------------------------------------------------------------ */
/*
  Globals
  =====================
*/
extern gex_Client_t      gasneti_thunk_client;
extern gex_EP_t          gasneti_thunk_endpoint;
extern gex_TM_t          gasneti_thunk_tm;
extern gex_Segment_t     gasneti_thunk_segment;

/* ------------------------------------------------------------------------------------ */
/*
  Compile-time constants
  =====================
*/
#define SIZEOF_GASNET_REGISTER_VALUE_T SIZEOF_GEX_RMA_VALUE_T

/* public spec version numbers */
#define GASNET_SPEC_VERSION_MAJOR GASNETI_SPEC_VERSION_MAJOR
#define GASNET_SPEC_VERSION_MINOR GASNETI_SPEC_VERSION_MINOR

/*  legacy name for major spec version number */
#define GASNET_VERSION GASNET_SPEC_VERSION_MAJOR

/* ------------------------------------------------------------------------------------ */
/*
  Base types
  ==========
*/

typedef gex_Rank_t gasnet_node_t;
typedef gex_AM_Token_t gasnet_token_t;
typedef gex_AM_Index_t gasnet_handler_t;
typedef gex_AM_Arg_t gasnet_handlerarg_t;
typedef gex_RMA_Value_t gasnet_register_value_t;

typedef gex_Event_t gasnet_handle_t;
#define GASNET_INVALID_HANDLE GEX_EVENT_INVALID

/* ------------------------------------------------------------------------------------ */
/*
  Initialization
  ==============
*/

GASNETT_INLINE(gasnet_init)
int gasnet_init(int *argc, char ***argv) {
  return gex_Client_Init (     &gasneti_thunk_client,
                               &gasneti_thunk_endpoint,
                               &gasneti_thunk_tm,
                               "LEGACY", argc, argv,
                               GASNETI_FLAG_INIT_LEGACY);
}

extern int gasnetc_attach( gex_Client_t      *client_p,
                           gex_EP_t               *endpoint_p,
                           gex_TM_t               *tm_p,
                           gex_Segment_t     *segment_p,
                           gasnet_handlerentry_t  *table,
                           int                    numentries,
                           uintptr_t              segsize);
GASNETT_INLINE(gasnet_attach)
int gasnet_attach( gasnet_handlerentry_t *table, int numentries,
                   uintptr_t segsize, uintptr_t minheapoffset ) {
  return gasnetc_attach( &gasneti_thunk_client, &gasneti_thunk_endpoint,
                         &gasneti_thunk_tm, &gasneti_thunk_segment,
                         table, numentries, segsize);
}

GASNETT_INLINE(gasnet_QueryGexObjects)
void gasnet_QueryGexObjects( gex_Client_t      *client_p,
                             gex_EP_t          *endpoint_p,
                             gex_TM_t          *tm_p,
                             gex_Segment_t     *segment_p) {
  GASNETI_CHECKATTACH();
  if (client_p)   *client_p   = gasneti_thunk_client;
  if (endpoint_p) *endpoint_p = gasneti_thunk_endpoint;
  if (tm_p)     *tm_p     = gasneti_thunk_tm;
  if (segment_p)  *segment_p  = gasneti_thunk_segment;
}

/* ------------------------------------------------------------------------------------ */
/*
  Active Message Query Functions
  ==============================
*/

#define gasnet_AMMaxMedium()       MIN(gex_AM_LUBRequestMedium(),\
                                       gex_AM_LUBReplyMedium())
#define gasnet_AMMaxLongRequest()  gex_AM_LUBRequestLong()
#define gasnet_AMMaxLongReply()    gex_AM_LUBReplyLong()

/* ------------------------------------------------------------------------------------ */
/*
  Active Message Request/Reply Functions
  ======================================
*/

#define gasnet_AMRequestShort0(dest, handler) \
       gex_AM_RequestShort0(gasneti_thunk_tm, dest, handler, 0)
#define gasnet_AMRequestShort1(dest, handler, a0) \
       gex_AM_RequestShort1(gasneti_thunk_tm, dest, handler, 0, (gex_AM_Arg_t)a0)
#define gasnet_AMRequestShort2(dest, handler, a0, a1) \
       gex_AM_RequestShort2(gasneti_thunk_tm, dest, handler, 0, (gex_AM_Arg_t)a0, (gex_AM_Arg_t)a1)
#define gasnet_AMRequestShort3(dest, handler, a0, a1, a2) \
       gex_AM_RequestShort3(gasneti_thunk_tm, dest, handler, 0, (gex_AM_Arg_t)a0, (gex_AM_Arg_t)a1, (gex_AM_Arg_t)a2)
#define gasnet_AMRequestShort4(dest, handler, a0, a1, a2, a3) \
       gex_AM_RequestShort4(gasneti_thunk_tm, dest, handler, 0, (gex_AM_Arg_t)a0, (gex_AM_Arg_t)a1, (gex_AM_Arg_t)a2, (gex_AM_Arg_t)a3)

#define gasnet_AMRequestShort5(dest, handler, a0, a1, a2, a3, a4) \
       gex_AM_RequestShort5(gasneti_thunk_tm, dest, handler, 0, (gex_AM_Arg_t)a0, (gex_AM_Arg_t)a1, (gex_AM_Arg_t)a2, (gex_AM_Arg_t)a3, (gex_AM_Arg_t)a4)
#define gasnet_AMRequestShort6(dest, handler, a0, a1, a2, a3, a4, a5) \
       gex_AM_RequestShort6(gasneti_thunk_tm, dest, handler, 0, (gex_AM_Arg_t)a0, (gex_AM_Arg_t)a1, (gex_AM_Arg_t)a2, (gex_AM_Arg_t)a3, (gex_AM_Arg_t)a4, (gex_AM_Arg_t)a5)
#define gasnet_AMRequestShort7(dest, handler, a0, a1, a2, a3, a4, a5, a6) \
       gex_AM_RequestShort7(gasneti_thunk_tm, dest, handler, 0, (gex_AM_Arg_t)a0, (gex_AM_Arg_t)a1, (gex_AM_Arg_t)a2, (gex_AM_Arg_t)a3, (gex_AM_Arg_t)a4, (gex_AM_Arg_t)a5, (gex_AM_Arg_t)a6)
#define gasnet_AMRequestShort8(dest, handler, a0, a1, a2, a3, a4, a5, a6, a7) \
       gex_AM_RequestShort8(gasneti_thunk_tm, dest, handler, 0, (gex_AM_Arg_t)a0, (gex_AM_Arg_t)a1, (gex_AM_Arg_t)a2, (gex_AM_Arg_t)a3, (gex_AM_Arg_t)a4, (gex_AM_Arg_t)a5, (gex_AM_Arg_t)a6, (gex_AM_Arg_t)a7)

#define gasnet_AMRequestShort9( dest, handler, a0, a1, a2, a3, a4, a5, a6, a7, a8 ) \
        gex_AM_RequestShort9(gasneti_thunk_tm, dest, handler, 0, (gex_AM_Arg_t)a0, (gex_AM_Arg_t)a1, (gex_AM_Arg_t)a2, (gex_AM_Arg_t)a3, (gex_AM_Arg_t)a4, (gex_AM_Arg_t)a5, (gex_AM_Arg_t)a6, (gex_AM_Arg_t)a7, (gex_AM_Arg_t)a8)
#define gasnet_AMRequestShort10(dest, handler, a0, a1, a2, a3, a4, a5, a6, a7, a8, a9) \
        gex_AM_RequestShort10(gasneti_thunk_tm, dest, handler, 0, (gex_AM_Arg_t)a0, (gex_AM_Arg_t)a1, (gex_AM_Arg_t)a2, (gex_AM_Arg_t)a3, (gex_AM_Arg_t)a4, (gex_AM_Arg_t)a5, (gex_AM_Arg_t)a6, (gex_AM_Arg_t)a7, (gex_AM_Arg_t)a8, (gex_AM_Arg_t)a9)
#define gasnet_AMRequestShort11(dest, handler, a0, a1, a2, a3, a4, a5, a6, a7, a8, a9, a10) \
        gex_AM_RequestShort11(gasneti_thunk_tm, dest, handler, 0, (gex_AM_Arg_t)a0, (gex_AM_Arg_t)a1, (gex_AM_Arg_t)a2, (gex_AM_Arg_t)a3, (gex_AM_Arg_t)a4, (gex_AM_Arg_t)a5, (gex_AM_Arg_t)a6, (gex_AM_Arg_t)a7, (gex_AM_Arg_t)a8, (gex_AM_Arg_t)a9, (gex_AM_Arg_t)a10)
#define gasnet_AMRequestShort12(dest, handler, a0, a1, a2, a3, a4, a5, a6, a7, a8, a9, a10, a11) \
        gex_AM_RequestShort12(gasneti_thunk_tm, dest, handler, 0, (gex_AM_Arg_t)a0, (gex_AM_Arg_t)a1, (gex_AM_Arg_t)a2, (gex_AM_Arg_t)a3, (gex_AM_Arg_t)a4, (gex_AM_Arg_t)a5, (gex_AM_Arg_t)a6, (gex_AM_Arg_t)a7, (gex_AM_Arg_t)a8, (gex_AM_Arg_t)a9, (gex_AM_Arg_t)a10, (gex_AM_Arg_t)a11)

#define gasnet_AMRequestShort13(dest, handler, a0, a1, a2, a3, a4, a5, a6, a7, a8, a9, a10, a11, a12) \
        gex_AM_RequestShort13(gasneti_thunk_tm, dest, handler, 0, (gex_AM_Arg_t)a0, (gex_AM_Arg_t)a1, (gex_AM_Arg_t)a2, (gex_AM_Arg_t)a3, (gex_AM_Arg_t)a4, (gex_AM_Arg_t)a5, (gex_AM_Arg_t)a6, (gex_AM_Arg_t)a7, (gex_AM_Arg_t)a8, (gex_AM_Arg_t)a9, (gex_AM_Arg_t)a10, (gex_AM_Arg_t)a11, (gex_AM_Arg_t)a12)
#define gasnet_AMRequestShort14(dest, handler, a0, a1, a2, a3, a4, a5, a6, a7, a8, a9, a10, a11, a12, a13) \
        gex_AM_RequestShort14(gasneti_thunk_tm, dest, handler, 0, (gex_AM_Arg_t)a0, (gex_AM_Arg_t)a1, (gex_AM_Arg_t)a2, (gex_AM_Arg_t)a3, (gex_AM_Arg_t)a4, (gex_AM_Arg_t)a5, (gex_AM_Arg_t)a6, (gex_AM_Arg_t)a7, (gex_AM_Arg_t)a8, (gex_AM_Arg_t)a9, (gex_AM_Arg_t)a10, (gex_AM_Arg_t)a11, (gex_AM_Arg_t)a12, (gex_AM_Arg_t)a13)
#define gasnet_AMRequestShort15(dest, handler, a0, a1, a2, a3, a4, a5, a6, a7, a8, a9, a10, a11, a12, a13, a14) \
        gex_AM_RequestShort15(gasneti_thunk_tm, dest, handler, 0, (gex_AM_Arg_t)a0, (gex_AM_Arg_t)a1, (gex_AM_Arg_t)a2, (gex_AM_Arg_t)a3, (gex_AM_Arg_t)a4, (gex_AM_Arg_t)a5, (gex_AM_Arg_t)a6, (gex_AM_Arg_t)a7, (gex_AM_Arg_t)a8, (gex_AM_Arg_t)a9, (gex_AM_Arg_t)a10, (gex_AM_Arg_t)a11, (gex_AM_Arg_t)a12, (gex_AM_Arg_t)a13, (gex_AM_Arg_t)a14)
#define gasnet_AMRequestShort16(dest, handler, a0, a1, a2, a3, a4, a5, a6, a7, a8, a9, a10, a11, a12, a13, a14, a15) \
        gex_AM_RequestShort16(gasneti_thunk_tm, dest, handler, 0, (gex_AM_Arg_t)a0, (gex_AM_Arg_t)a1, (gex_AM_Arg_t)a2, (gex_AM_Arg_t)a3, (gex_AM_Arg_t)a4, (gex_AM_Arg_t)a5, (gex_AM_Arg_t)a6, (gex_AM_Arg_t)a7, (gex_AM_Arg_t)a8, (gex_AM_Arg_t)a9, (gex_AM_Arg_t)a10, (gex_AM_Arg_t)a11, (gex_AM_Arg_t)a12, (gex_AM_Arg_t)a13, (gex_AM_Arg_t)a14, (gex_AM_Arg_t)a15)
/* ------------------------------------------------------------------------------------ */
#define gasnet_AMRequestMedium0(dest, handler, source_addr, nbytes) \
       gex_AM_RequestMedium0(gasneti_thunk_tm, dest, handler, source_addr, nbytes, GEX_EVENT_NOW, 0)
#define gasnet_AMRequestMedium1(dest, handler, source_addr, nbytes, a0) \
       gex_AM_RequestMedium1(gasneti_thunk_tm, dest, handler, source_addr, nbytes, GEX_EVENT_NOW, 0, (gex_AM_Arg_t)a0)
#define gasnet_AMRequestMedium2(dest, handler, source_addr, nbytes, a0, a1) \
       gex_AM_RequestMedium2(gasneti_thunk_tm, dest, handler, source_addr, nbytes, GEX_EVENT_NOW, 0, (gex_AM_Arg_t)a0, (gex_AM_Arg_t)a1)
#define gasnet_AMRequestMedium3(dest, handler, source_addr, nbytes, a0, a1, a2) \
       gex_AM_RequestMedium3(gasneti_thunk_tm, dest, handler, source_addr, nbytes, GEX_EVENT_NOW, 0, (gex_AM_Arg_t)a0, (gex_AM_Arg_t)a1, (gex_AM_Arg_t)a2)
#define gasnet_AMRequestMedium4(dest, handler, source_addr, nbytes, a0, a1, a2, a3) \
       gex_AM_RequestMedium4(gasneti_thunk_tm, dest, handler, source_addr, nbytes, GEX_EVENT_NOW, 0, (gex_AM_Arg_t)a0, (gex_AM_Arg_t)a1, (gex_AM_Arg_t)a2, (gex_AM_Arg_t)a3)

#define gasnet_AMRequestMedium5(dest, handler, source_addr, nbytes, a0, a1, a2, a3, a4) \
       gex_AM_RequestMedium5(gasneti_thunk_tm, dest, handler, source_addr, nbytes, GEX_EVENT_NOW, 0, (gex_AM_Arg_t)a0, (gex_AM_Arg_t)a1, (gex_AM_Arg_t)a2, (gex_AM_Arg_t)a3, (gex_AM_Arg_t)a4)
#define gasnet_AMRequestMedium6(dest, handler, source_addr, nbytes, a0, a1, a2, a3, a4, a5) \
       gex_AM_RequestMedium6(gasneti_thunk_tm, dest, handler, source_addr, nbytes, GEX_EVENT_NOW, 0, (gex_AM_Arg_t)a0, (gex_AM_Arg_t)a1, (gex_AM_Arg_t)a2, (gex_AM_Arg_t)a3, (gex_AM_Arg_t)a4, (gex_AM_Arg_t)a5)
#define gasnet_AMRequestMedium7(dest, handler, source_addr, nbytes, a0, a1, a2, a3, a4, a5, a6) \
       gex_AM_RequestMedium7(gasneti_thunk_tm, dest, handler, source_addr, nbytes, GEX_EVENT_NOW, 0, (gex_AM_Arg_t)a0, (gex_AM_Arg_t)a1, (gex_AM_Arg_t)a2, (gex_AM_Arg_t)a3, (gex_AM_Arg_t)a4, (gex_AM_Arg_t)a5, (gex_AM_Arg_t)a6)
#define gasnet_AMRequestMedium8(dest, handler, source_addr, nbytes, a0, a1, a2, a3, a4, a5, a6, a7) \
       gex_AM_RequestMedium8(gasneti_thunk_tm, dest, handler, source_addr, nbytes, GEX_EVENT_NOW, 0, (gex_AM_Arg_t)a0, (gex_AM_Arg_t)a1, (gex_AM_Arg_t)a2, (gex_AM_Arg_t)a3, (gex_AM_Arg_t)a4, (gex_AM_Arg_t)a5, (gex_AM_Arg_t)a6, (gex_AM_Arg_t)a7)

#define gasnet_AMRequestMedium9( dest, handler, source_addr, nbytes, a0, a1, a2, a3, a4, a5, a6, a7, a8 ) \
        gex_AM_RequestMedium9(gasneti_thunk_tm, dest, handler, source_addr, nbytes, GEX_EVENT_NOW, 0, (gex_AM_Arg_t)a0, (gex_AM_Arg_t)a1, (gex_AM_Arg_t)a2, (gex_AM_Arg_t)a3, (gex_AM_Arg_t)a4, (gex_AM_Arg_t)a5, (gex_AM_Arg_t)a6, (gex_AM_Arg_t)a7, (gex_AM_Arg_t)a8)
#define gasnet_AMRequestMedium10(dest, handler, source_addr, nbytes, a0, a1, a2, a3, a4, a5, a6, a7, a8, a9) \
        gex_AM_RequestMedium10(gasneti_thunk_tm, dest, handler, source_addr, nbytes, GEX_EVENT_NOW, 0, (gex_AM_Arg_t)a0, (gex_AM_Arg_t)a1, (gex_AM_Arg_t)a2, (gex_AM_Arg_t)a3, (gex_AM_Arg_t)a4, (gex_AM_Arg_t)a5, (gex_AM_Arg_t)a6, (gex_AM_Arg_t)a7, (gex_AM_Arg_t)a8, (gex_AM_Arg_t)a9)
#define gasnet_AMRequestMedium11(dest, handler, source_addr, nbytes, a0, a1, a2, a3, a4, a5, a6, a7, a8, a9, a10) \
        gex_AM_RequestMedium11(gasneti_thunk_tm, dest, handler, source_addr, nbytes, GEX_EVENT_NOW, 0, (gex_AM_Arg_t)a0, (gex_AM_Arg_t)a1, (gex_AM_Arg_t)a2, (gex_AM_Arg_t)a3, (gex_AM_Arg_t)a4, (gex_AM_Arg_t)a5, (gex_AM_Arg_t)a6, (gex_AM_Arg_t)a7, (gex_AM_Arg_t)a8, (gex_AM_Arg_t)a9, (gex_AM_Arg_t)a10)
#define gasnet_AMRequestMedium12(dest, handler, source_addr, nbytes, a0, a1, a2, a3, a4, a5, a6, a7, a8, a9, a10, a11) \
        gex_AM_RequestMedium12(gasneti_thunk_tm, dest, handler, source_addr, nbytes, GEX_EVENT_NOW, 0, (gex_AM_Arg_t)a0, (gex_AM_Arg_t)a1, (gex_AM_Arg_t)a2, (gex_AM_Arg_t)a3, (gex_AM_Arg_t)a4, (gex_AM_Arg_t)a5, (gex_AM_Arg_t)a6, (gex_AM_Arg_t)a7, (gex_AM_Arg_t)a8, (gex_AM_Arg_t)a9, (gex_AM_Arg_t)a10, (gex_AM_Arg_t)a11)

#define gasnet_AMRequestMedium13(dest, handler, source_addr, nbytes, a0, a1, a2, a3, a4, a5, a6, a7, a8, a9, a10, a11, a12) \
        gex_AM_RequestMedium13(gasneti_thunk_tm, dest, handler, source_addr, nbytes, GEX_EVENT_NOW, 0, (gex_AM_Arg_t)a0, (gex_AM_Arg_t)a1, (gex_AM_Arg_t)a2, (gex_AM_Arg_t)a3, (gex_AM_Arg_t)a4, (gex_AM_Arg_t)a5, (gex_AM_Arg_t)a6, (gex_AM_Arg_t)a7, (gex_AM_Arg_t)a8, (gex_AM_Arg_t)a9, (gex_AM_Arg_t)a10, (gex_AM_Arg_t)a11, (gex_AM_Arg_t)a12)
#define gasnet_AMRequestMedium14(dest, handler, source_addr, nbytes, a0, a1, a2, a3, a4, a5, a6, a7, a8, a9, a10, a11, a12, a13) \
        gex_AM_RequestMedium14(gasneti_thunk_tm, dest, handler, source_addr, nbytes, GEX_EVENT_NOW, 0, (gex_AM_Arg_t)a0, (gex_AM_Arg_t)a1, (gex_AM_Arg_t)a2, (gex_AM_Arg_t)a3, (gex_AM_Arg_t)a4, (gex_AM_Arg_t)a5, (gex_AM_Arg_t)a6, (gex_AM_Arg_t)a7, (gex_AM_Arg_t)a8, (gex_AM_Arg_t)a9, (gex_AM_Arg_t)a10, (gex_AM_Arg_t)a11, (gex_AM_Arg_t)a12, (gex_AM_Arg_t)a13)
#define gasnet_AMRequestMedium15(dest, handler, source_addr, nbytes, a0, a1, a2, a3, a4, a5, a6, a7, a8, a9, a10, a11, a12, a13, a14) \
        gex_AM_RequestMedium15(gasneti_thunk_tm, dest, handler, source_addr, nbytes, GEX_EVENT_NOW, 0, (gex_AM_Arg_t)a0, (gex_AM_Arg_t)a1, (gex_AM_Arg_t)a2, (gex_AM_Arg_t)a3, (gex_AM_Arg_t)a4, (gex_AM_Arg_t)a5, (gex_AM_Arg_t)a6, (gex_AM_Arg_t)a7, (gex_AM_Arg_t)a8, (gex_AM_Arg_t)a9, (gex_AM_Arg_t)a10, (gex_AM_Arg_t)a11, (gex_AM_Arg_t)a12, (gex_AM_Arg_t)a13, (gex_AM_Arg_t)a14)
#define gasnet_AMRequestMedium16(dest, handler, source_addr, nbytes, a0, a1, a2, a3, a4, a5, a6, a7, a8, a9, a10, a11, a12, a13, a14, a15) \
        gex_AM_RequestMedium16(gasneti_thunk_tm, dest, handler, source_addr, nbytes, GEX_EVENT_NOW, 0, (gex_AM_Arg_t)a0, (gex_AM_Arg_t)a1, (gex_AM_Arg_t)a2, (gex_AM_Arg_t)a3, (gex_AM_Arg_t)a4, (gex_AM_Arg_t)a5, (gex_AM_Arg_t)a6, (gex_AM_Arg_t)a7, (gex_AM_Arg_t)a8, (gex_AM_Arg_t)a9, (gex_AM_Arg_t)a10, (gex_AM_Arg_t)a11, (gex_AM_Arg_t)a12, (gex_AM_Arg_t)a13, (gex_AM_Arg_t)a14, (gex_AM_Arg_t)a15)
/* ------------------------------------------------------------------------------------ */
#define gasnet_AMRequestLong0(dest, handler, source_addr, nbytes, dest_addr) \
       gex_AM_RequestLong0(gasneti_thunk_tm, dest, handler, source_addr, nbytes, dest_addr, GEX_EVENT_NOW, 0)
#define gasnet_AMRequestLong1(dest, handler, source_addr, nbytes, dest_addr, a0) \
       gex_AM_RequestLong1(gasneti_thunk_tm, dest, handler, source_addr, nbytes, dest_addr, GEX_EVENT_NOW, 0, (gex_AM_Arg_t)a0)
#define gasnet_AMRequestLong2(dest, handler, source_addr, nbytes, dest_addr, a0, a1) \
       gex_AM_RequestLong2(gasneti_thunk_tm, dest, handler, source_addr, nbytes, dest_addr, GEX_EVENT_NOW, 0, (gex_AM_Arg_t)a0, (gex_AM_Arg_t)a1)
#define gasnet_AMRequestLong3(dest, handler, source_addr, nbytes, dest_addr, a0, a1, a2) \
       gex_AM_RequestLong3(gasneti_thunk_tm, dest, handler, source_addr, nbytes, dest_addr, GEX_EVENT_NOW, 0, (gex_AM_Arg_t)a0, (gex_AM_Arg_t)a1, (gex_AM_Arg_t)a2)
#define gasnet_AMRequestLong4(dest, handler, source_addr, nbytes, dest_addr, a0, a1, a2, a3) \
       gex_AM_RequestLong4(gasneti_thunk_tm, dest, handler, source_addr, nbytes, dest_addr, GEX_EVENT_NOW, 0, (gex_AM_Arg_t)a0, (gex_AM_Arg_t)a1, (gex_AM_Arg_t)a2, (gex_AM_Arg_t)a3)

#define gasnet_AMRequestLong5(dest, handler, source_addr, nbytes, dest_addr, a0, a1, a2, a3, a4) \
       gex_AM_RequestLong5(gasneti_thunk_tm, dest, handler, source_addr, nbytes, dest_addr, GEX_EVENT_NOW, 0, (gex_AM_Arg_t)a0, (gex_AM_Arg_t)a1, (gex_AM_Arg_t)a2, (gex_AM_Arg_t)a3, (gex_AM_Arg_t)a4)
#define gasnet_AMRequestLong6(dest, handler, source_addr, nbytes, dest_addr, a0, a1, a2, a3, a4, a5) \
       gex_AM_RequestLong6(gasneti_thunk_tm, dest, handler, source_addr, nbytes, dest_addr, GEX_EVENT_NOW, 0, (gex_AM_Arg_t)a0, (gex_AM_Arg_t)a1, (gex_AM_Arg_t)a2, (gex_AM_Arg_t)a3, (gex_AM_Arg_t)a4, (gex_AM_Arg_t)a5)
#define gasnet_AMRequestLong7(dest, handler, source_addr, nbytes, dest_addr, a0, a1, a2, a3, a4, a5, a6) \
       gex_AM_RequestLong7(gasneti_thunk_tm, dest, handler, source_addr, nbytes, dest_addr, GEX_EVENT_NOW, 0, (gex_AM_Arg_t)a0, (gex_AM_Arg_t)a1, (gex_AM_Arg_t)a2, (gex_AM_Arg_t)a3, (gex_AM_Arg_t)a4, (gex_AM_Arg_t)a5, (gex_AM_Arg_t)a6)
#define gasnet_AMRequestLong8(dest, handler, source_addr, nbytes, dest_addr, a0, a1, a2, a3, a4, a5, a6, a7) \
       gex_AM_RequestLong8(gasneti_thunk_tm, dest, handler, source_addr, nbytes, dest_addr, GEX_EVENT_NOW, 0, (gex_AM_Arg_t)a0, (gex_AM_Arg_t)a1, (gex_AM_Arg_t)a2, (gex_AM_Arg_t)a3, (gex_AM_Arg_t)a4, (gex_AM_Arg_t)a5, (gex_AM_Arg_t)a6, (gex_AM_Arg_t)a7)

#define gasnet_AMRequestLong9( dest, handler, source_addr, nbytes, dest_addr, a0, a1, a2, a3, a4, a5, a6, a7, a8 ) \
        gex_AM_RequestLong9(gasneti_thunk_tm, dest, handler, source_addr, nbytes, dest_addr, GEX_EVENT_NOW, 0, (gex_AM_Arg_t)a0, (gex_AM_Arg_t)a1, (gex_AM_Arg_t)a2, (gex_AM_Arg_t)a3, (gex_AM_Arg_t)a4, (gex_AM_Arg_t)a5, (gex_AM_Arg_t)a6, (gex_AM_Arg_t)a7, (gex_AM_Arg_t)a8)
#define gasnet_AMRequestLong10(dest, handler, source_addr, nbytes, dest_addr, a0, a1, a2, a3, a4, a5, a6, a7, a8, a9) \
        gex_AM_RequestLong10(gasneti_thunk_tm, dest, handler, source_addr, nbytes, dest_addr, GEX_EVENT_NOW, 0, (gex_AM_Arg_t)a0, (gex_AM_Arg_t)a1, (gex_AM_Arg_t)a2, (gex_AM_Arg_t)a3, (gex_AM_Arg_t)a4, (gex_AM_Arg_t)a5, (gex_AM_Arg_t)a6, (gex_AM_Arg_t)a7, (gex_AM_Arg_t)a8, (gex_AM_Arg_t)a9)
#define gasnet_AMRequestLong11(dest, handler, source_addr, nbytes, dest_addr, a0, a1, a2, a3, a4, a5, a6, a7, a8, a9, a10) \
        gex_AM_RequestLong11(gasneti_thunk_tm, dest, handler, source_addr, nbytes, dest_addr, GEX_EVENT_NOW, 0, (gex_AM_Arg_t)a0, (gex_AM_Arg_t)a1, (gex_AM_Arg_t)a2, (gex_AM_Arg_t)a3, (gex_AM_Arg_t)a4, (gex_AM_Arg_t)a5, (gex_AM_Arg_t)a6, (gex_AM_Arg_t)a7, (gex_AM_Arg_t)a8, (gex_AM_Arg_t)a9, (gex_AM_Arg_t)a10)
#define gasnet_AMRequestLong12(dest, handler, source_addr, nbytes, dest_addr, a0, a1, a2, a3, a4, a5, a6, a7, a8, a9, a10, a11) \
        gex_AM_RequestLong12(gasneti_thunk_tm, dest, handler, source_addr, nbytes, dest_addr, GEX_EVENT_NOW, 0, (gex_AM_Arg_t)a0, (gex_AM_Arg_t)a1, (gex_AM_Arg_t)a2, (gex_AM_Arg_t)a3, (gex_AM_Arg_t)a4, (gex_AM_Arg_t)a5, (gex_AM_Arg_t)a6, (gex_AM_Arg_t)a7, (gex_AM_Arg_t)a8, (gex_AM_Arg_t)a9, (gex_AM_Arg_t)a10, (gex_AM_Arg_t)a11)

#define gasnet_AMRequestLong13(dest, handler, source_addr, nbytes, dest_addr, a0, a1, a2, a3, a4, a5, a6, a7, a8, a9, a10, a11, a12) \
        gex_AM_RequestLong13(gasneti_thunk_tm, dest, handler, source_addr, nbytes, dest_addr, GEX_EVENT_NOW, 0, (gex_AM_Arg_t)a0, (gex_AM_Arg_t)a1, (gex_AM_Arg_t)a2, (gex_AM_Arg_t)a3, (gex_AM_Arg_t)a4, (gex_AM_Arg_t)a5, (gex_AM_Arg_t)a6, (gex_AM_Arg_t)a7, (gex_AM_Arg_t)a8, (gex_AM_Arg_t)a9, (gex_AM_Arg_t)a10, (gex_AM_Arg_t)a11, (gex_AM_Arg_t)a12)
#define gasnet_AMRequestLong14(dest, handler, source_addr, nbytes, dest_addr, a0, a1, a2, a3, a4, a5, a6, a7, a8, a9, a10, a11, a12, a13) \
        gex_AM_RequestLong14(gasneti_thunk_tm, dest, handler, source_addr, nbytes, dest_addr, GEX_EVENT_NOW, 0, (gex_AM_Arg_t)a0, (gex_AM_Arg_t)a1, (gex_AM_Arg_t)a2, (gex_AM_Arg_t)a3, (gex_AM_Arg_t)a4, (gex_AM_Arg_t)a5, (gex_AM_Arg_t)a6, (gex_AM_Arg_t)a7, (gex_AM_Arg_t)a8, (gex_AM_Arg_t)a9, (gex_AM_Arg_t)a10, (gex_AM_Arg_t)a11, (gex_AM_Arg_t)a12, (gex_AM_Arg_t)a13)
#define gasnet_AMRequestLong15(dest, handler, source_addr, nbytes, dest_addr, a0, a1, a2, a3, a4, a5, a6, a7, a8, a9, a10, a11, a12, a13, a14) \
        gex_AM_RequestLong15(gasneti_thunk_tm, dest, handler, source_addr, nbytes, dest_addr, GEX_EVENT_NOW, 0, (gex_AM_Arg_t)a0, (gex_AM_Arg_t)a1, (gex_AM_Arg_t)a2, (gex_AM_Arg_t)a3, (gex_AM_Arg_t)a4, (gex_AM_Arg_t)a5, (gex_AM_Arg_t)a6, (gex_AM_Arg_t)a7, (gex_AM_Arg_t)a8, (gex_AM_Arg_t)a9, (gex_AM_Arg_t)a10, (gex_AM_Arg_t)a11, (gex_AM_Arg_t)a12, (gex_AM_Arg_t)a13, (gex_AM_Arg_t)a14)
#define gasnet_AMRequestLong16(dest, handler, source_addr, nbytes, dest_addr, a0, a1, a2, a3, a4, a5, a6, a7, a8, a9, a10, a11, a12, a13, a14, a15) \
        gex_AM_RequestLong16(gasneti_thunk_tm, dest, handler, source_addr, nbytes, dest_addr, GEX_EVENT_NOW, 0, (gex_AM_Arg_t)a0, (gex_AM_Arg_t)a1, (gex_AM_Arg_t)a2, (gex_AM_Arg_t)a3, (gex_AM_Arg_t)a4, (gex_AM_Arg_t)a5, (gex_AM_Arg_t)a6, (gex_AM_Arg_t)a7, (gex_AM_Arg_t)a8, (gex_AM_Arg_t)a9, (gex_AM_Arg_t)a10, (gex_AM_Arg_t)a11, (gex_AM_Arg_t)a12, (gex_AM_Arg_t)a13, (gex_AM_Arg_t)a14, (gex_AM_Arg_t)a15)
/* ------------------------------------------------------------------------------------ */
#define gasnet_AMRequestLongAsync0 gasnet_AMRequestLong0
#define gasnet_AMRequestLongAsync1 gasnet_AMRequestLong1
#define gasnet_AMRequestLongAsync2 gasnet_AMRequestLong2
#define gasnet_AMRequestLongAsync3 gasnet_AMRequestLong3
#define gasnet_AMRequestLongAsync4 gasnet_AMRequestLong4
#define gasnet_AMRequestLongAsync5 gasnet_AMRequestLong5
#define gasnet_AMRequestLongAsync6 gasnet_AMRequestLong6
#define gasnet_AMRequestLongAsync7 gasnet_AMRequestLong7
#define gasnet_AMRequestLongAsync8 gasnet_AMRequestLong8
#define gasnet_AMRequestLongAsync9 gasnet_AMRequestLong9
#define gasnet_AMRequestLongAsync10 gasnet_AMRequestLong10
#define gasnet_AMRequestLongAsync11 gasnet_AMRequestLong11
#define gasnet_AMRequestLongAsync12 gasnet_AMRequestLong12
#define gasnet_AMRequestLongAsync13 gasnet_AMRequestLong13
#define gasnet_AMRequestLongAsync14 gasnet_AMRequestLong14
#define gasnet_AMRequestLongAsync15 gasnet_AMRequestLong15
#define gasnet_AMRequestLongAsync16 gasnet_AMRequestLong16

/* ------------------------------------------------------------------------------------ */

#define gasnet_AMReplyShort0(token, handler) \
       gex_AM_ReplyShort0(token, handler, 0)
#define gasnet_AMReplyShort1(token, handler, a0) \
       gex_AM_ReplyShort1(token, handler, 0, (gex_AM_Arg_t)a0)
#define gasnet_AMReplyShort2(token, handler, a0, a1) \
       gex_AM_ReplyShort2(token, handler, 0, (gex_AM_Arg_t)a0, (gex_AM_Arg_t)a1)
#define gasnet_AMReplyShort3(token, handler, a0, a1, a2) \
       gex_AM_ReplyShort3(token, handler, 0, (gex_AM_Arg_t)a0, (gex_AM_Arg_t)a1, (gex_AM_Arg_t)a2)
#define gasnet_AMReplyShort4(token, handler, a0, a1, a2, a3) \
       gex_AM_ReplyShort4(token, handler, 0, (gex_AM_Arg_t)a0, (gex_AM_Arg_t)a1, (gex_AM_Arg_t)a2, (gex_AM_Arg_t)a3)

#define gasnet_AMReplyShort5(token, handler, a0, a1, a2, a3, a4) \
       gex_AM_ReplyShort5(token, handler, 0, (gex_AM_Arg_t)a0, (gex_AM_Arg_t)a1, (gex_AM_Arg_t)a2, (gex_AM_Arg_t)a3, (gex_AM_Arg_t)a4)
#define gasnet_AMReplyShort6(token, handler, a0, a1, a2, a3, a4, a5) \
       gex_AM_ReplyShort6(token, handler, 0, (gex_AM_Arg_t)a0, (gex_AM_Arg_t)a1, (gex_AM_Arg_t)a2, (gex_AM_Arg_t)a3, (gex_AM_Arg_t)a4, (gex_AM_Arg_t)a5)
#define gasnet_AMReplyShort7(token, handler, a0, a1, a2, a3, a4, a5, a6) \
       gex_AM_ReplyShort7(token, handler, 0, (gex_AM_Arg_t)a0, (gex_AM_Arg_t)a1, (gex_AM_Arg_t)a2, (gex_AM_Arg_t)a3, (gex_AM_Arg_t)a4, (gex_AM_Arg_t)a5, (gex_AM_Arg_t)a6)
#define gasnet_AMReplyShort8(token, handler, a0, a1, a2, a3, a4, a5, a6, a7) \
       gex_AM_ReplyShort8(token, handler, 0, (gex_AM_Arg_t)a0, (gex_AM_Arg_t)a1, (gex_AM_Arg_t)a2, (gex_AM_Arg_t)a3, (gex_AM_Arg_t)a4, (gex_AM_Arg_t)a5, (gex_AM_Arg_t)a6, (gex_AM_Arg_t)a7)

#define gasnet_AMReplyShort9( token, handler, a0, a1, a2, a3, a4, a5, a6, a7, a8 ) \
        gex_AM_ReplyShort9(token, handler, 0, (gex_AM_Arg_t)a0, (gex_AM_Arg_t)a1, (gex_AM_Arg_t)a2, (gex_AM_Arg_t)a3, (gex_AM_Arg_t)a4, (gex_AM_Arg_t)a5, (gex_AM_Arg_t)a6, (gex_AM_Arg_t)a7, (gex_AM_Arg_t)a8)
#define gasnet_AMReplyShort10(token, handler, a0, a1, a2, a3, a4, a5, a6, a7, a8, a9) \
        gex_AM_ReplyShort10(token, handler, 0, (gex_AM_Arg_t)a0, (gex_AM_Arg_t)a1, (gex_AM_Arg_t)a2, (gex_AM_Arg_t)a3, (gex_AM_Arg_t)a4, (gex_AM_Arg_t)a5, (gex_AM_Arg_t)a6, (gex_AM_Arg_t)a7, (gex_AM_Arg_t)a8, (gex_AM_Arg_t)a9)
#define gasnet_AMReplyShort11(token, handler, a0, a1, a2, a3, a4, a5, a6, a7, a8, a9, a10) \
        gex_AM_ReplyShort11(token, handler, 0, (gex_AM_Arg_t)a0, (gex_AM_Arg_t)a1, (gex_AM_Arg_t)a2, (gex_AM_Arg_t)a3, (gex_AM_Arg_t)a4, (gex_AM_Arg_t)a5, (gex_AM_Arg_t)a6, (gex_AM_Arg_t)a7, (gex_AM_Arg_t)a8, (gex_AM_Arg_t)a9, (gex_AM_Arg_t)a10)
#define gasnet_AMReplyShort12(token, handler, a0, a1, a2, a3, a4, a5, a6, a7, a8, a9, a10, a11) \
        gex_AM_ReplyShort12(token, handler, 0, (gex_AM_Arg_t)a0, (gex_AM_Arg_t)a1, (gex_AM_Arg_t)a2, (gex_AM_Arg_t)a3, (gex_AM_Arg_t)a4, (gex_AM_Arg_t)a5, (gex_AM_Arg_t)a6, (gex_AM_Arg_t)a7, (gex_AM_Arg_t)a8, (gex_AM_Arg_t)a9, (gex_AM_Arg_t)a10, (gex_AM_Arg_t)a11)

#define gasnet_AMReplyShort13(token, handler, a0, a1, a2, a3, a4, a5, a6, a7, a8, a9, a10, a11, a12) \
        gex_AM_ReplyShort13(token, handler, 0, (gex_AM_Arg_t)a0, (gex_AM_Arg_t)a1, (gex_AM_Arg_t)a2, (gex_AM_Arg_t)a3, (gex_AM_Arg_t)a4, (gex_AM_Arg_t)a5, (gex_AM_Arg_t)a6, (gex_AM_Arg_t)a7, (gex_AM_Arg_t)a8, (gex_AM_Arg_t)a9, (gex_AM_Arg_t)a10, (gex_AM_Arg_t)a11, (gex_AM_Arg_t)a12)
#define gasnet_AMReplyShort14(token, handler, a0, a1, a2, a3, a4, a5, a6, a7, a8, a9, a10, a11, a12, a13) \
        gex_AM_ReplyShort14(token, handler, 0, (gex_AM_Arg_t)a0, (gex_AM_Arg_t)a1, (gex_AM_Arg_t)a2, (gex_AM_Arg_t)a3, (gex_AM_Arg_t)a4, (gex_AM_Arg_t)a5, (gex_AM_Arg_t)a6, (gex_AM_Arg_t)a7, (gex_AM_Arg_t)a8, (gex_AM_Arg_t)a9, (gex_AM_Arg_t)a10, (gex_AM_Arg_t)a11, (gex_AM_Arg_t)a12, (gex_AM_Arg_t)a13)
#define gasnet_AMReplyShort15(token, handler, a0, a1, a2, a3, a4, a5, a6, a7, a8, a9, a10, a11, a12, a13, a14) \
        gex_AM_ReplyShort15(token, handler, 0, (gex_AM_Arg_t)a0, (gex_AM_Arg_t)a1, (gex_AM_Arg_t)a2, (gex_AM_Arg_t)a3, (gex_AM_Arg_t)a4, (gex_AM_Arg_t)a5, (gex_AM_Arg_t)a6, (gex_AM_Arg_t)a7, (gex_AM_Arg_t)a8, (gex_AM_Arg_t)a9, (gex_AM_Arg_t)a10, (gex_AM_Arg_t)a11, (gex_AM_Arg_t)a12, (gex_AM_Arg_t)a13, (gex_AM_Arg_t)a14)
#define gasnet_AMReplyShort16(token, handler, a0, a1, a2, a3, a4, a5, a6, a7, a8, a9, a10, a11, a12, a13, a14, a15) \
        gex_AM_ReplyShort16(token, handler, 0, (gex_AM_Arg_t)a0, (gex_AM_Arg_t)a1, (gex_AM_Arg_t)a2, (gex_AM_Arg_t)a3, (gex_AM_Arg_t)a4, (gex_AM_Arg_t)a5, (gex_AM_Arg_t)a6, (gex_AM_Arg_t)a7, (gex_AM_Arg_t)a8, (gex_AM_Arg_t)a9, (gex_AM_Arg_t)a10, (gex_AM_Arg_t)a11, (gex_AM_Arg_t)a12, (gex_AM_Arg_t)a13, (gex_AM_Arg_t)a14, (gex_AM_Arg_t)a15)
/* ------------------------------------------------------------------------------------ */
#define gasnet_AMReplyMedium0(token, handler, source_addr, nbytes) \
       gex_AM_ReplyMedium0(token, handler, source_addr, nbytes, GEX_EVENT_NOW, 0)
#define gasnet_AMReplyMedium1(token, handler, source_addr, nbytes, a0) \
       gex_AM_ReplyMedium1(token, handler, source_addr, nbytes, GEX_EVENT_NOW, 0, (gex_AM_Arg_t)a0)
#define gasnet_AMReplyMedium2(token, handler, source_addr, nbytes, a0, a1) \
       gex_AM_ReplyMedium2(token, handler, source_addr, nbytes, GEX_EVENT_NOW, 0, (gex_AM_Arg_t)a0, (gex_AM_Arg_t)a1)
#define gasnet_AMReplyMedium3(token, handler, source_addr, nbytes, a0, a1, a2) \
       gex_AM_ReplyMedium3(token, handler, source_addr, nbytes, GEX_EVENT_NOW, 0, (gex_AM_Arg_t)a0, (gex_AM_Arg_t)a1, (gex_AM_Arg_t)a2)
#define gasnet_AMReplyMedium4(token, handler, source_addr, nbytes, a0, a1, a2, a3) \
       gex_AM_ReplyMedium4(token, handler, source_addr, nbytes, GEX_EVENT_NOW, 0, (gex_AM_Arg_t)a0, (gex_AM_Arg_t)a1, (gex_AM_Arg_t)a2, (gex_AM_Arg_t)a3)

#define gasnet_AMReplyMedium5(token, handler, source_addr, nbytes, a0, a1, a2, a3, a4) \
       gex_AM_ReplyMedium5(token, handler, source_addr, nbytes, GEX_EVENT_NOW, 0, (gex_AM_Arg_t)a0, (gex_AM_Arg_t)a1, (gex_AM_Arg_t)a2, (gex_AM_Arg_t)a3, (gex_AM_Arg_t)a4)
#define gasnet_AMReplyMedium6(token, handler, source_addr, nbytes, a0, a1, a2, a3, a4, a5) \
       gex_AM_ReplyMedium6(token, handler, source_addr, nbytes, GEX_EVENT_NOW, 0, (gex_AM_Arg_t)a0, (gex_AM_Arg_t)a1, (gex_AM_Arg_t)a2, (gex_AM_Arg_t)a3, (gex_AM_Arg_t)a4, (gex_AM_Arg_t)a5)
#define gasnet_AMReplyMedium7(token, handler, source_addr, nbytes, a0, a1, a2, a3, a4, a5, a6) \
       gex_AM_ReplyMedium7(token, handler, source_addr, nbytes, GEX_EVENT_NOW, 0, (gex_AM_Arg_t)a0, (gex_AM_Arg_t)a1, (gex_AM_Arg_t)a2, (gex_AM_Arg_t)a3, (gex_AM_Arg_t)a4, (gex_AM_Arg_t)a5, (gex_AM_Arg_t)a6)
#define gasnet_AMReplyMedium8(token, handler, source_addr, nbytes, a0, a1, a2, a3, a4, a5, a6, a7) \
       gex_AM_ReplyMedium8(token, handler, source_addr, nbytes, GEX_EVENT_NOW, 0, (gex_AM_Arg_t)a0, (gex_AM_Arg_t)a1, (gex_AM_Arg_t)a2, (gex_AM_Arg_t)a3, (gex_AM_Arg_t)a4, (gex_AM_Arg_t)a5, (gex_AM_Arg_t)a6, (gex_AM_Arg_t)a7)

#define gasnet_AMReplyMedium9( token, handler, source_addr, nbytes, a0, a1, a2, a3, a4, a5, a6, a7, a8 ) \
        gex_AM_ReplyMedium9(token, handler, source_addr, nbytes, GEX_EVENT_NOW, 0, (gex_AM_Arg_t)a0, (gex_AM_Arg_t)a1, (gex_AM_Arg_t)a2, (gex_AM_Arg_t)a3, (gex_AM_Arg_t)a4, (gex_AM_Arg_t)a5, (gex_AM_Arg_t)a6, (gex_AM_Arg_t)a7, (gex_AM_Arg_t)a8)
#define gasnet_AMReplyMedium10(token, handler, source_addr, nbytes, a0, a1, a2, a3, a4, a5, a6, a7, a8, a9) \
        gex_AM_ReplyMedium10(token, handler, source_addr, nbytes, GEX_EVENT_NOW, 0, (gex_AM_Arg_t)a0, (gex_AM_Arg_t)a1, (gex_AM_Arg_t)a2, (gex_AM_Arg_t)a3, (gex_AM_Arg_t)a4, (gex_AM_Arg_t)a5, (gex_AM_Arg_t)a6, (gex_AM_Arg_t)a7, (gex_AM_Arg_t)a8, (gex_AM_Arg_t)a9)
#define gasnet_AMReplyMedium11(token, handler, source_addr, nbytes, a0, a1, a2, a3, a4, a5, a6, a7, a8, a9, a10) \
        gex_AM_ReplyMedium11(token, handler, source_addr, nbytes, GEX_EVENT_NOW, 0, (gex_AM_Arg_t)a0, (gex_AM_Arg_t)a1, (gex_AM_Arg_t)a2, (gex_AM_Arg_t)a3, (gex_AM_Arg_t)a4, (gex_AM_Arg_t)a5, (gex_AM_Arg_t)a6, (gex_AM_Arg_t)a7, (gex_AM_Arg_t)a8, (gex_AM_Arg_t)a9, (gex_AM_Arg_t)a10)
#define gasnet_AMReplyMedium12(token, handler, source_addr, nbytes, a0, a1, a2, a3, a4, a5, a6, a7, a8, a9, a10, a11) \
        gex_AM_ReplyMedium12(token, handler, source_addr, nbytes, GEX_EVENT_NOW, 0, (gex_AM_Arg_t)a0, (gex_AM_Arg_t)a1, (gex_AM_Arg_t)a2, (gex_AM_Arg_t)a3, (gex_AM_Arg_t)a4, (gex_AM_Arg_t)a5, (gex_AM_Arg_t)a6, (gex_AM_Arg_t)a7, (gex_AM_Arg_t)a8, (gex_AM_Arg_t)a9, (gex_AM_Arg_t)a10, (gex_AM_Arg_t)a11)

#define gasnet_AMReplyMedium13(token, handler, source_addr, nbytes, a0, a1, a2, a3, a4, a5, a6, a7, a8, a9, a10, a11, a12) \
        gex_AM_ReplyMedium13(token, handler, source_addr, nbytes, GEX_EVENT_NOW, 0, (gex_AM_Arg_t)a0, (gex_AM_Arg_t)a1, (gex_AM_Arg_t)a2, (gex_AM_Arg_t)a3, (gex_AM_Arg_t)a4, (gex_AM_Arg_t)a5, (gex_AM_Arg_t)a6, (gex_AM_Arg_t)a7, (gex_AM_Arg_t)a8, (gex_AM_Arg_t)a9, (gex_AM_Arg_t)a10, (gex_AM_Arg_t)a11, (gex_AM_Arg_t)a12)
#define gasnet_AMReplyMedium14(token, handler, source_addr, nbytes, a0, a1, a2, a3, a4, a5, a6, a7, a8, a9, a10, a11, a12, a13) \
        gex_AM_ReplyMedium14(token, handler, source_addr, nbytes, GEX_EVENT_NOW, 0, (gex_AM_Arg_t)a0, (gex_AM_Arg_t)a1, (gex_AM_Arg_t)a2, (gex_AM_Arg_t)a3, (gex_AM_Arg_t)a4, (gex_AM_Arg_t)a5, (gex_AM_Arg_t)a6, (gex_AM_Arg_t)a7, (gex_AM_Arg_t)a8, (gex_AM_Arg_t)a9, (gex_AM_Arg_t)a10, (gex_AM_Arg_t)a11, (gex_AM_Arg_t)a12, (gex_AM_Arg_t)a13)
#define gasnet_AMReplyMedium15(token, handler, source_addr, nbytes, a0, a1, a2, a3, a4, a5, a6, a7, a8, a9, a10, a11, a12, a13, a14) \
        gex_AM_ReplyMedium15(token, handler, source_addr, nbytes, GEX_EVENT_NOW, 0, (gex_AM_Arg_t)a0, (gex_AM_Arg_t)a1, (gex_AM_Arg_t)a2, (gex_AM_Arg_t)a3, (gex_AM_Arg_t)a4, (gex_AM_Arg_t)a5, (gex_AM_Arg_t)a6, (gex_AM_Arg_t)a7, (gex_AM_Arg_t)a8, (gex_AM_Arg_t)a9, (gex_AM_Arg_t)a10, (gex_AM_Arg_t)a11, (gex_AM_Arg_t)a12, (gex_AM_Arg_t)a13, (gex_AM_Arg_t)a14)
#define gasnet_AMReplyMedium16(token, handler, source_addr, nbytes, a0, a1, a2, a3, a4, a5, a6, a7, a8, a9, a10, a11, a12, a13, a14, a15) \
        gex_AM_ReplyMedium16(token, handler, source_addr, nbytes, GEX_EVENT_NOW, 0, (gex_AM_Arg_t)a0, (gex_AM_Arg_t)a1, (gex_AM_Arg_t)a2, (gex_AM_Arg_t)a3, (gex_AM_Arg_t)a4, (gex_AM_Arg_t)a5, (gex_AM_Arg_t)a6, (gex_AM_Arg_t)a7, (gex_AM_Arg_t)a8, (gex_AM_Arg_t)a9, (gex_AM_Arg_t)a10, (gex_AM_Arg_t)a11, (gex_AM_Arg_t)a12, (gex_AM_Arg_t)a13, (gex_AM_Arg_t)a14, (gex_AM_Arg_t)a15)
/* ------------------------------------------------------------------------------------ */
#define gasnet_AMReplyLong0(token, handler, source_addr, nbytes, dest_addr) \
       gex_AM_ReplyLong0(token, handler, source_addr, nbytes, dest_addr, GEX_EVENT_NOW, 0)
#define gasnet_AMReplyLong1(token, handler, source_addr, nbytes, dest_addr, a0) \
       gex_AM_ReplyLong1(token, handler, source_addr, nbytes, dest_addr, GEX_EVENT_NOW, 0, (gex_AM_Arg_t)a0)
#define gasnet_AMReplyLong2(token, handler, source_addr, nbytes, dest_addr, a0, a1) \
       gex_AM_ReplyLong2(token, handler, source_addr, nbytes, dest_addr, GEX_EVENT_NOW, 0, (gex_AM_Arg_t)a0, (gex_AM_Arg_t)a1)
#define gasnet_AMReplyLong3(token, handler, source_addr, nbytes, dest_addr, a0, a1, a2) \
       gex_AM_ReplyLong3(token, handler, source_addr, nbytes, dest_addr, GEX_EVENT_NOW, 0, (gex_AM_Arg_t)a0, (gex_AM_Arg_t)a1, (gex_AM_Arg_t)a2)
#define gasnet_AMReplyLong4(token, handler, source_addr, nbytes, dest_addr, a0, a1, a2, a3) \
       gex_AM_ReplyLong4(token, handler, source_addr, nbytes, dest_addr, GEX_EVENT_NOW, 0, (gex_AM_Arg_t)a0, (gex_AM_Arg_t)a1, (gex_AM_Arg_t)a2, (gex_AM_Arg_t)a3)

#define gasnet_AMReplyLong5(token, handler, source_addr, nbytes, dest_addr, a0, a1, a2, a3, a4) \
       gex_AM_ReplyLong5(token, handler, source_addr, nbytes, dest_addr, GEX_EVENT_NOW, 0, (gex_AM_Arg_t)a0, (gex_AM_Arg_t)a1, (gex_AM_Arg_t)a2, (gex_AM_Arg_t)a3, (gex_AM_Arg_t)a4)
#define gasnet_AMReplyLong6(token, handler, source_addr, nbytes, dest_addr, a0, a1, a2, a3, a4, a5) \
       gex_AM_ReplyLong6(token, handler, source_addr, nbytes, dest_addr, GEX_EVENT_NOW, 0, (gex_AM_Arg_t)a0, (gex_AM_Arg_t)a1, (gex_AM_Arg_t)a2, (gex_AM_Arg_t)a3, (gex_AM_Arg_t)a4, (gex_AM_Arg_t)a5)
#define gasnet_AMReplyLong7(token, handler, source_addr, nbytes, dest_addr, a0, a1, a2, a3, a4, a5, a6) \
       gex_AM_ReplyLong7(token, handler, source_addr, nbytes, dest_addr, GEX_EVENT_NOW, 0, (gex_AM_Arg_t)a0, (gex_AM_Arg_t)a1, (gex_AM_Arg_t)a2, (gex_AM_Arg_t)a3, (gex_AM_Arg_t)a4, (gex_AM_Arg_t)a5, (gex_AM_Arg_t)a6)
#define gasnet_AMReplyLong8(token, handler, source_addr, nbytes, dest_addr, a0, a1, a2, a3, a4, a5, a6, a7) \
       gex_AM_ReplyLong8(token, handler, source_addr, nbytes, dest_addr, GEX_EVENT_NOW, 0, (gex_AM_Arg_t)a0, (gex_AM_Arg_t)a1, (gex_AM_Arg_t)a2, (gex_AM_Arg_t)a3, (gex_AM_Arg_t)a4, (gex_AM_Arg_t)a5, (gex_AM_Arg_t)a6, (gex_AM_Arg_t)a7)

#define gasnet_AMReplyLong9( token, handler, source_addr, nbytes, dest_addr, a0, a1, a2, a3, a4, a5, a6, a7, a8 ) \
        gex_AM_ReplyLong9(token, handler, source_addr, nbytes, dest_addr, GEX_EVENT_NOW, 0, (gex_AM_Arg_t)a0, (gex_AM_Arg_t)a1, (gex_AM_Arg_t)a2, (gex_AM_Arg_t)a3, (gex_AM_Arg_t)a4, (gex_AM_Arg_t)a5, (gex_AM_Arg_t)a6, (gex_AM_Arg_t)a7, (gex_AM_Arg_t)a8)
#define gasnet_AMReplyLong10(token, handler, source_addr, nbytes, dest_addr, a0, a1, a2, a3, a4, a5, a6, a7, a8, a9) \
        gex_AM_ReplyLong10(token, handler, source_addr, nbytes, dest_addr, GEX_EVENT_NOW, 0, (gex_AM_Arg_t)a0, (gex_AM_Arg_t)a1, (gex_AM_Arg_t)a2, (gex_AM_Arg_t)a3, (gex_AM_Arg_t)a4, (gex_AM_Arg_t)a5, (gex_AM_Arg_t)a6, (gex_AM_Arg_t)a7, (gex_AM_Arg_t)a8, (gex_AM_Arg_t)a9)
#define gasnet_AMReplyLong11(token, handler, source_addr, nbytes, dest_addr, a0, a1, a2, a3, a4, a5, a6, a7, a8, a9, a10) \
        gex_AM_ReplyLong11(token, handler, source_addr, nbytes, dest_addr, GEX_EVENT_NOW, 0, (gex_AM_Arg_t)a0, (gex_AM_Arg_t)a1, (gex_AM_Arg_t)a2, (gex_AM_Arg_t)a3, (gex_AM_Arg_t)a4, (gex_AM_Arg_t)a5, (gex_AM_Arg_t)a6, (gex_AM_Arg_t)a7, (gex_AM_Arg_t)a8, (gex_AM_Arg_t)a9, (gex_AM_Arg_t)a10)
#define gasnet_AMReplyLong12(token, handler, source_addr, nbytes, dest_addr, a0, a1, a2, a3, a4, a5, a6, a7, a8, a9, a10, a11) \
        gex_AM_ReplyLong12(token, handler, source_addr, nbytes, dest_addr, GEX_EVENT_NOW, 0, (gex_AM_Arg_t)a0, (gex_AM_Arg_t)a1, (gex_AM_Arg_t)a2, (gex_AM_Arg_t)a3, (gex_AM_Arg_t)a4, (gex_AM_Arg_t)a5, (gex_AM_Arg_t)a6, (gex_AM_Arg_t)a7, (gex_AM_Arg_t)a8, (gex_AM_Arg_t)a9, (gex_AM_Arg_t)a10, (gex_AM_Arg_t)a11)

#define gasnet_AMReplyLong13(token, handler, source_addr, nbytes, dest_addr, a0, a1, a2, a3, a4, a5, a6, a7, a8, a9, a10, a11, a12) \
        gex_AM_ReplyLong13(token, handler, source_addr, nbytes, dest_addr, GEX_EVENT_NOW, 0, (gex_AM_Arg_t)a0, (gex_AM_Arg_t)a1, (gex_AM_Arg_t)a2, (gex_AM_Arg_t)a3, (gex_AM_Arg_t)a4, (gex_AM_Arg_t)a5, (gex_AM_Arg_t)a6, (gex_AM_Arg_t)a7, (gex_AM_Arg_t)a8, (gex_AM_Arg_t)a9, (gex_AM_Arg_t)a10, (gex_AM_Arg_t)a11, (gex_AM_Arg_t)a12)
#define gasnet_AMReplyLong14(token, handler, source_addr, nbytes, dest_addr, a0, a1, a2, a3, a4, a5, a6, a7, a8, a9, a10, a11, a12, a13) \
        gex_AM_ReplyLong14(token, handler, source_addr, nbytes, dest_addr, GEX_EVENT_NOW, 0, (gex_AM_Arg_t)a0, (gex_AM_Arg_t)a1, (gex_AM_Arg_t)a2, (gex_AM_Arg_t)a3, (gex_AM_Arg_t)a4, (gex_AM_Arg_t)a5, (gex_AM_Arg_t)a6, (gex_AM_Arg_t)a7, (gex_AM_Arg_t)a8, (gex_AM_Arg_t)a9, (gex_AM_Arg_t)a10, (gex_AM_Arg_t)a11, (gex_AM_Arg_t)a12, (gex_AM_Arg_t)a13)
#define gasnet_AMReplyLong15(token, handler, source_addr, nbytes, dest_addr, a0, a1, a2, a3, a4, a5, a6, a7, a8, a9, a10, a11, a12, a13, a14) \
        gex_AM_ReplyLong15(token, handler, source_addr, nbytes, dest_addr, GEX_EVENT_NOW, 0, (gex_AM_Arg_t)a0, (gex_AM_Arg_t)a1, (gex_AM_Arg_t)a2, (gex_AM_Arg_t)a3, (gex_AM_Arg_t)a4, (gex_AM_Arg_t)a5, (gex_AM_Arg_t)a6, (gex_AM_Arg_t)a7, (gex_AM_Arg_t)a8, (gex_AM_Arg_t)a9, (gex_AM_Arg_t)a10, (gex_AM_Arg_t)a11, (gex_AM_Arg_t)a12, (gex_AM_Arg_t)a13, (gex_AM_Arg_t)a14)
#define gasnet_AMReplyLong16(token, handler, source_addr, nbytes, dest_addr, a0, a1, a2, a3, a4, a5, a6, a7, a8, a9, a10, a11, a12, a13, a14, a15) \
        gex_AM_ReplyLong16(token, handler, source_addr, nbytes, dest_addr, GEX_EVENT_NOW, 0, (gex_AM_Arg_t)a0, (gex_AM_Arg_t)a1, (gex_AM_Arg_t)a2, (gex_AM_Arg_t)a3, (gex_AM_Arg_t)a4, (gex_AM_Arg_t)a5, (gex_AM_Arg_t)a6, (gex_AM_Arg_t)a7, (gex_AM_Arg_t)a8, (gex_AM_Arg_t)a9, (gex_AM_Arg_t)a10, (gex_AM_Arg_t)a11, (gex_AM_Arg_t)a12, (gex_AM_Arg_t)a13, (gex_AM_Arg_t)a14, (gex_AM_Arg_t)a15)

/* ------------------------------------------------------------------------------------ */
/* Handler-safe locks */

#define gasnet_hsl_t    	gex_HSL_t
#define gasnet_hsl_init	        gex_HSL_Init
#define gasnet_hsl_destroy	gex_HSL_Destroy
#define gasnet_hsl_lock         gex_HSL_Lock
#define gasnet_hsl_unlock	gex_HSL_Unlock
#define gasnet_hsl_trylock	gex_HSL_Trylock
#define GASNET_HSL_INITIALIZER	GEX_HSL_INITIALIZER

/* ------------------------------------------------------------------------------------ */
/* Blocking Put and Get */
// TODO-EX: pass GEX_FLAG_DST_IN_SEGMENT and possibly other flags

#define gasnet_put(node,dest,src,nbytes) \
                gex_RMA_PutBlocking(gasneti_thunk_tm,node,dest,src,nbytes,0)
#define gasnet_put_bulk(node,dest,src,nbytes) \
                gex_RMA_PutBlocking(gasneti_thunk_tm,node,dest,src,nbytes,0)
#define gasnet_get(dest,node,src,nbytes) \
                gex_RMA_GetBlocking(gasneti_thunk_tm,dest,node,src,nbytes,0)
#define gasnet_get_bulk(dest,node,src,nbytes) \
                gex_RMA_GetBlocking(gasneti_thunk_tm,dest,node,src,nbytes,0)

/* ------------------------------------------------------------------------------------ */
/* Implicit-handle non-blocking Put and Get */
// TODO-EX: pass GEX_FLAG_DST_IN_SEGMENT and possibly other flags

#define gasnet_put_nbi(node,dest,src,nbytes) \
                gex_RMA_PutNBI(gasneti_thunk_tm,node,dest,src,nbytes,GEX_EVENT_NOW,0)
#define gasnet_put_nbi_bulk(node,dest,src,nbytes) \
                gex_RMA_PutNBI(gasneti_thunk_tm,node,dest,src,nbytes,GEX_EVENT_DEFER,0)
#define gasnet_get_nbi(dest,node,src,nbytes) \
                gex_RMA_GetNBI(gasneti_thunk_tm,dest,node,src,nbytes,0)
#define gasnet_get_nbi_bulk(dest,node,src,nbytes) \
                gex_RMA_GetNBI(gasneti_thunk_tm,dest,node,src,nbytes,0)

/* ------------------------------------------------------------------------------------ */
/* Explicit-handle non-blocking Put and Get */
// TODO-EX: pass GEX_FLAG_DST_IN_SEGMENT and possibly other flags

#define gasnet_put_nb(node,dest,src,nbytes) \
                gex_RMA_PutNB(gasneti_thunk_tm,node,dest,src,nbytes,GEX_EVENT_NOW,0)
#define gasnet_put_nb_bulk(node,dest,src,nbytes) \
                gex_RMA_PutNB(gasneti_thunk_tm,node,dest,src,nbytes,GEX_EVENT_DEFER,0)
#define gasnet_get_nb(dest,node,src,nbytes) \
                gex_RMA_GetNB(gasneti_thunk_tm,dest,node,src,nbytes,0)
#define gasnet_get_nb_bulk(dest,node,src,nbytes) \
                gex_RMA_GetNB(gasneti_thunk_tm,dest,node,src,nbytes,0)

/* ------------------------------------------------------------------------------------ */
/* Value Gets - blocking and explicit-handle non-blocking */
// TODO-EX: pass GEX_FLAG_SRC_IN_SEGMENT and possibly other flags

#define gasnet_get_val(node,src,nbytes) \
                gex_RMA_GetBlockingVal(gasneti_thunk_tm,node,src,nbytes,0)

typedef struct {
  gex_RMA_Value_t v;
  gex_Event_t         h;
} *gasnet_valget_handle_t;

GASNETT_INLINE(gasnet_get_nb_val)
gasnet_valget_handle_t gasnet_get_nb_val(gex_Rank_t node, void *src, size_t nbytes)
{
  gasnet_valget_handle_t result = (gasnet_valget_handle_t)malloc(sizeof(*result));
#ifdef PLATFORM_ARCH_BIG_ENDIAN
  void *dest = (void*)((uintptr_t)&(result->v) + sizeof(gex_RMA_Value_t) - nbytes);
#else /* little-endian */
  void *dest = &result->v;
#endif
  result->v = 0;
  //assert(nbytes > 0 && nbytes <= sizeof(gex_RMA_Value_t));
  result->h = gex_RMA_GetNB(gasneti_thunk_tm, dest, node, src, nbytes, 0);
  return result;
}

GASNETT_INLINE(gasnet_wait_syncnb_valget)
gex_RMA_Value_t gasnet_wait_syncnb_valget(gasnet_valget_handle_t handle)
{
  gex_RMA_Value_t result;
  gex_Event_Wait(handle->h);
  result = handle->v;
  free(handle);
  return result;
}


/* ------------------------------------------------------------------------------------ */
/* Value Puts - blocking, and explicit- and implicit-handle non-blocking */
// TODO-EX: pass GEX_FLAG_DST_IN_SEGMENT and possibly other flags

#define gasnet_put_val(node,dest,value,nbytes) \
                gex_RMA_PutBlockingVal(gasneti_thunk_tm,node,dest,value,nbytes,0)
#define gasnet_put_nb_val(node,dest,value,nbytes) \
                gex_RMA_PutNBVal(gasneti_thunk_tm,node,dest,value,nbytes,0)
#define gasnet_put_nbi_val(node,dest,value,nbytes) \
                gex_RMA_PutNBIVal(gasneti_thunk_tm,node,dest,value,nbytes,0)

/* ------------------------------------------------------------------------------------ */
/* Explicit-handle sync operations */

#define gasnet_try_syncnb_nopoll(h)          gex_Event_Test(h)
#define gasnet_try_syncnb_some_nopoll(ph,sz) gex_Event_TestSome(ph,sz)
#define gasnet_try_syncnb_all_nopoll(ph,sz)  gex_Event_TestAll(ph,sz)

#define gasnet_try_syncnb(h)          (gasnet_AMPoll(),gex_Event_Test(h))
#define gasnet_try_syncnb_some(ph,sz) (gasnet_AMPoll(),gex_Event_TestSome(ph,sz))
#define gasnet_try_syncnb_all(ph,sz)  (gasnet_AMPoll(),gex_Event_TestAll(ph,sz))

#define gasnet_wait_syncnb(h)          gex_Event_Wait(h)
#define gasnet_wait_syncnb_some(ph,sz) gex_Event_WaitSome(ph,sz)
#define gasnet_wait_syncnb_all(ph,sz)  gex_Event_WaitAll(ph,sz)

/* ------------------------------------------------------------------------------------ */
/* Implicit-handle sync operations */

#define gasnet_try_syncnbi_gets() (gasnet_AMPoll(),gex_NBI_TestGets())
#define gasnet_try_syncnbi_puts() (gasnet_AMPoll(),gex_NBI_TestPuts())
#define gasnet_try_syncnbi_all()  (gasnet_AMPoll(),gex_NBI_TestAll ())

#define gasnet_wait_syncnbi_gets() gex_NBI_WaitGets()
#define gasnet_wait_syncnbi_puts() gex_NBI_WaitPuts()
#define gasnet_wait_syncnbi_all()  gex_NBI_WaitAll ()

/* ------------------------------------------------------------------------------------ */
/* Implicit-handle access regions */

#define gasnet_begin_nbi_accessregion() gex_NBI_BeginAccessRegion(0)
#define gasnet_end_nbi_accessregion()   gex_NBI_EndAccessRegion(0)

/* ------------------------------------------------------------------------------------ */
GASNETI_END_NOWARN
GASNETT_END_EXTERNC

#endif

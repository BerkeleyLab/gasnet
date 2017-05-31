/*   $Source: bitbucket.org:berkeleylab/gasnet.git/gasnet_ammacros.h $
 * Description: GASNet ammacros header
 * Copyright 2002, Dan Bonachea <bonachea@cs.berkeley.edu>
 * Terms of use are as specified in license.txt
 */

#ifndef _IN_GASNETEX_H
  #error This file is not meant to be included directly- clients should include gasnetex.h
#endif

#ifndef _GASNET_AMMACROS_H
#define _GASNET_AMMACROS_H

/* ------------------------------------------------------------------------------------ */
/*
  Active Message Request/Reply Functions
  ======================================
*/

/* This header uses macros to coalesce all the possible
   AM request/reply functions into the following short list of variable-argument
   functions. This is not the only implementation option, but seems to work 
   rather cleanly.
*/

// TODO-EX: introduce 'const' - has cascading effects!!!

// Long
extern int gasnetc_AMRequestLongM(
                gasnetex_team_member_t team,   // Names a local context ("return address")
                gasnetex_rank_t rank,          // Together with 'team', names a remote context
                gasnetex_handler_t handler,    // Index into handler table of remote context
                /*const*/ void *source_addr,   // Payload address (or OFFSET)
                size_t nbytes,                 // Payload length
                void *dest_addr,               // Payload destination address (or OFFSET)
                gasnetex_handle_t *lc_opt,  // Local completion control (see above)
                gasnetex_flags_t flags         // Flags to control this operation
                GASNETI_THREAD_FARG,           // Hidden thread-specific info argument
                int numargs, ...);             // Argument list (0..AMMaxArgs) as varargs
extern int gasnetc_AMReplyLongM(
                gasnetex_token_t token,        // Names local and remote contexts
                gasnetex_handler_t handler,
                /*const*/ void *source_addr,
                size_t nbytes,
                void *dest_addr,
                gasnetex_handle_t *lc_opt,
                gasnetex_flags_t flags,
                int numargs, ...);
// Medium
extern int gasnetc_AMRequestMediumM(
                gasnetex_team_member_t team,
                gasnetex_rank_t rank,
                gasnetex_handler_t handler,
                /*const*/ void *source_addr,
                size_t nbytes,
                gasnetex_handle_t *lc_opt,
                gasnetex_flags_t flags
                GASNETI_THREAD_FARG,
                int numargs, ...);
extern int gasnetc_AMReplyMediumM(
                gasnetex_token_t token,
                gasnetex_handler_t handler,
                /*const*/ void *source_addr,
                size_t nbytes,
                gasnetex_handle_t *lc_opt,
                gasnetex_flags_t flags,
                int numargs, ...);
// Short
extern int gasnetc_AMRequestShortM(
                gasnetex_team_member_t team,
                gasnetex_rank_t rank,
                gasnetex_handler_t handler,
                gasnetex_flags_t flags
                GASNETI_THREAD_FARG,
                int numargs, ...);
extern int gasnetc_AMReplyShortM(
                gasnetex_token_t token,
                gasnetex_handler_t handler,
                gasnetex_flags_t flags,
                int numargs, ...);

/* ------------------------------------------------------------------------------------ */
/*
  Active Message Macros
  =====================
*/
/*  yes, this is ugly, but it works... */
/* ------------------------------------------------------------------------------------ */
#define gex_AM_RequestShort0(team, rank, handler, flags) \
       gasnetc_AMRequestShortM(team, rank, handler, flags GASNETI_THREAD_GET, 0)
#define gex_AM_RequestShort1(team, rank, handler, flags, a0) \
       gasnetc_AMRequestShortM(team, rank, handler, flags GASNETI_THREAD_GET, 1, (gasnetex_handlerarg_t)(a0))
#define gex_AM_RequestShort2(team, rank, handler, flags, a0, a1) \
       gasnetc_AMRequestShortM(team, rank, handler, flags GASNETI_THREAD_GET, 2, (gasnetex_handlerarg_t)(a0), (gasnetex_handlerarg_t)(a1))
#define gex_AM_RequestShort3(team, rank, handler, flags, a0, a1, a2) \
       gasnetc_AMRequestShortM(team, rank, handler, flags GASNETI_THREAD_GET, 3, (gasnetex_handlerarg_t)(a0), (gasnetex_handlerarg_t)(a1), (gasnetex_handlerarg_t)(a2))
#define gex_AM_RequestShort4(team, rank, handler, flags, a0, a1, a2, a3) \
       gasnetc_AMRequestShortM(team, rank, handler, flags GASNETI_THREAD_GET, 4, (gasnetex_handlerarg_t)(a0), (gasnetex_handlerarg_t)(a1), (gasnetex_handlerarg_t)(a2), (gasnetex_handlerarg_t)(a3))

#define gex_AM_RequestShort5(team, rank, handler, flags, a0, a1, a2, a3, a4) \
       gasnetc_AMRequestShortM(team, rank, handler, flags GASNETI_THREAD_GET, 5, (gasnetex_handlerarg_t)(a0), (gasnetex_handlerarg_t)(a1), (gasnetex_handlerarg_t)(a2), (gasnetex_handlerarg_t)(a3), (gasnetex_handlerarg_t)(a4))
#define gex_AM_RequestShort6(team, rank, handler, flags, a0, a1, a2, a3, a4, a5) \
       gasnetc_AMRequestShortM(team, rank, handler, flags GASNETI_THREAD_GET, 6, (gasnetex_handlerarg_t)(a0), (gasnetex_handlerarg_t)(a1), (gasnetex_handlerarg_t)(a2), (gasnetex_handlerarg_t)(a3), (gasnetex_handlerarg_t)(a4), (gasnetex_handlerarg_t)(a5))
#define gex_AM_RequestShort7(team, rank, handler, flags, a0, a1, a2, a3, a4, a5, a6) \
       gasnetc_AMRequestShortM(team, rank, handler, flags GASNETI_THREAD_GET, 7, (gasnetex_handlerarg_t)(a0), (gasnetex_handlerarg_t)(a1), (gasnetex_handlerarg_t)(a2), (gasnetex_handlerarg_t)(a3), (gasnetex_handlerarg_t)(a4), (gasnetex_handlerarg_t)(a5), (gasnetex_handlerarg_t)(a6))
#define gex_AM_RequestShort8(team, rank, handler, flags, a0, a1, a2, a3, a4, a5, a6, a7) \
       gasnetc_AMRequestShortM(team, rank, handler, flags GASNETI_THREAD_GET, 8, (gasnetex_handlerarg_t)(a0), (gasnetex_handlerarg_t)(a1), (gasnetex_handlerarg_t)(a2), (gasnetex_handlerarg_t)(a3), (gasnetex_handlerarg_t)(a4), (gasnetex_handlerarg_t)(a5), (gasnetex_handlerarg_t)(a6), (gasnetex_handlerarg_t)(a7))

#define gex_AM_RequestShort9(team, rank, handler, flags, a0, a1, a2, a3, a4, a5, a6, a7, a8 ) \
        gasnetc_AMRequestShortM(team, rank, handler, flags GASNETI_THREAD_GET, 9, (gasnetex_handlerarg_t)(a0), (gasnetex_handlerarg_t)(a1), (gasnetex_handlerarg_t)(a2), (gasnetex_handlerarg_t)(a3), (gasnetex_handlerarg_t)(a4), (gasnetex_handlerarg_t)(a5), (gasnetex_handlerarg_t)(a6), (gasnetex_handlerarg_t)(a7), (gasnetex_handlerarg_t)(a8))
#define gex_AM_RequestShort10(team, rank, handler, flags, a0, a1, a2, a3, a4, a5, a6, a7, a8, a9) \
        gasnetc_AMRequestShortM(team, rank, handler, flags GASNETI_THREAD_GET, 10, (gasnetex_handlerarg_t)(a0), (gasnetex_handlerarg_t)(a1), (gasnetex_handlerarg_t)(a2), (gasnetex_handlerarg_t)(a3), (gasnetex_handlerarg_t)(a4), (gasnetex_handlerarg_t)(a5), (gasnetex_handlerarg_t)(a6), (gasnetex_handlerarg_t)(a7), (gasnetex_handlerarg_t)(a8), (gasnetex_handlerarg_t)(a9))
#define gex_AM_RequestShort11(team, rank, handler, flags, a0, a1, a2, a3, a4, a5, a6, a7, a8, a9, a10) \
        gasnetc_AMRequestShortM(team, rank, handler, flags GASNETI_THREAD_GET, 11, (gasnetex_handlerarg_t)(a0), (gasnetex_handlerarg_t)(a1), (gasnetex_handlerarg_t)(a2), (gasnetex_handlerarg_t)(a3), (gasnetex_handlerarg_t)(a4), (gasnetex_handlerarg_t)(a5), (gasnetex_handlerarg_t)(a6), (gasnetex_handlerarg_t)(a7), (gasnetex_handlerarg_t)(a8), (gasnetex_handlerarg_t)(a9), (gasnetex_handlerarg_t)(a10))
#define gex_AM_RequestShort12(team, rank, handler, flags, a0, a1, a2, a3, a4, a5, a6, a7, a8, a9, a10, a11) \
        gasnetc_AMRequestShortM(team, rank, handler, flags GASNETI_THREAD_GET, 12, (gasnetex_handlerarg_t)(a0), (gasnetex_handlerarg_t)(a1), (gasnetex_handlerarg_t)(a2), (gasnetex_handlerarg_t)(a3), (gasnetex_handlerarg_t)(a4), (gasnetex_handlerarg_t)(a5), (gasnetex_handlerarg_t)(a6), (gasnetex_handlerarg_t)(a7), (gasnetex_handlerarg_t)(a8), (gasnetex_handlerarg_t)(a9), (gasnetex_handlerarg_t)(a10), (gasnetex_handlerarg_t)(a11))

#define gex_AM_RequestShort13(team, rank, handler, flags, a0, a1, a2, a3, a4, a5, a6, a7, a8, a9, a10, a11, a12) \
        gasnetc_AMRequestShortM(team, rank, handler, flags GASNETI_THREAD_GET, 13, (gasnetex_handlerarg_t)(a0), (gasnetex_handlerarg_t)(a1), (gasnetex_handlerarg_t)(a2), (gasnetex_handlerarg_t)(a3), (gasnetex_handlerarg_t)(a4), (gasnetex_handlerarg_t)(a5), (gasnetex_handlerarg_t)(a6), (gasnetex_handlerarg_t)(a7), (gasnetex_handlerarg_t)(a8), (gasnetex_handlerarg_t)(a9), (gasnetex_handlerarg_t)(a10), (gasnetex_handlerarg_t)(a11), (gasnetex_handlerarg_t)(a12))
#define gex_AM_RequestShort14(team, rank, handler, flags, a0, a1, a2, a3, a4, a5, a6, a7, a8, a9, a10, a11, a12, a13) \
        gasnetc_AMRequestShortM(team, rank, handler, flags GASNETI_THREAD_GET, 14, (gasnetex_handlerarg_t)(a0), (gasnetex_handlerarg_t)(a1), (gasnetex_handlerarg_t)(a2), (gasnetex_handlerarg_t)(a3), (gasnetex_handlerarg_t)(a4), (gasnetex_handlerarg_t)(a5), (gasnetex_handlerarg_t)(a6), (gasnetex_handlerarg_t)(a7), (gasnetex_handlerarg_t)(a8), (gasnetex_handlerarg_t)(a9), (gasnetex_handlerarg_t)(a10), (gasnetex_handlerarg_t)(a11), (gasnetex_handlerarg_t)(a12), (gasnetex_handlerarg_t)(a13))
#define gex_AM_RequestShort15(team, rank, handler, flags, a0, a1, a2, a3, a4, a5, a6, a7, a8, a9, a10, a11, a12, a13, a14) \
        gasnetc_AMRequestShortM(team, rank, handler, flags GASNETI_THREAD_GET, 15, (gasnetex_handlerarg_t)(a0), (gasnetex_handlerarg_t)(a1), (gasnetex_handlerarg_t)(a2), (gasnetex_handlerarg_t)(a3), (gasnetex_handlerarg_t)(a4), (gasnetex_handlerarg_t)(a5), (gasnetex_handlerarg_t)(a6), (gasnetex_handlerarg_t)(a7), (gasnetex_handlerarg_t)(a8), (gasnetex_handlerarg_t)(a9), (gasnetex_handlerarg_t)(a10), (gasnetex_handlerarg_t)(a11), (gasnetex_handlerarg_t)(a12), (gasnetex_handlerarg_t)(a13), (gasnetex_handlerarg_t)(a14))
#define gex_AM_RequestShort16(team, rank, handler, flags, a0, a1, a2, a3, a4, a5, a6, a7, a8, a9, a10, a11, a12, a13, a14, a15) \
        gasnetc_AMRequestShortM(team, rank, handler, flags GASNETI_THREAD_GET, 16, (gasnetex_handlerarg_t)(a0), (gasnetex_handlerarg_t)(a1), (gasnetex_handlerarg_t)(a2), (gasnetex_handlerarg_t)(a3), (gasnetex_handlerarg_t)(a4), (gasnetex_handlerarg_t)(a5), (gasnetex_handlerarg_t)(a6), (gasnetex_handlerarg_t)(a7), (gasnetex_handlerarg_t)(a8), (gasnetex_handlerarg_t)(a9), (gasnetex_handlerarg_t)(a10), (gasnetex_handlerarg_t)(a11), (gasnetex_handlerarg_t)(a12), (gasnetex_handlerarg_t)(a13), (gasnetex_handlerarg_t)(a14), (gasnetex_handlerarg_t)(a15))
/* ------------------------------------------------------------------------------------ */
#define gex_AM_RequestMedium0(team, rank, handler, source_addr, nbytes, lc_opt, flags) \
       gasnetc_AMRequestMediumM(team, rank, handler, source_addr, nbytes, lc_opt, flags GASNETI_THREAD_GET, 0)
#define gex_AM_RequestMedium1(team, rank, handler, source_addr, nbytes, lc_opt, flags, a0) \
       gasnetc_AMRequestMediumM(team, rank, handler, source_addr, nbytes, lc_opt, flags GASNETI_THREAD_GET, 1, (gasnetex_handlerarg_t)(a0))
#define gex_AM_RequestMedium2(team, rank, handler, source_addr, nbytes, lc_opt, flags, a0, a1) \
       gasnetc_AMRequestMediumM(team, rank, handler, source_addr, nbytes, lc_opt, flags GASNETI_THREAD_GET, 2, (gasnetex_handlerarg_t)(a0), (gasnetex_handlerarg_t)(a1))
#define gex_AM_RequestMedium3(team, rank, handler, source_addr, nbytes, lc_opt, flags, a0, a1, a2) \
       gasnetc_AMRequestMediumM(team, rank, handler, source_addr, nbytes, lc_opt, flags GASNETI_THREAD_GET, 3, (gasnetex_handlerarg_t)(a0), (gasnetex_handlerarg_t)(a1), (gasnetex_handlerarg_t)(a2))
#define gex_AM_RequestMedium4(team, rank, handler, source_addr, nbytes, lc_opt, flags, a0, a1, a2, a3) \
       gasnetc_AMRequestMediumM(team, rank, handler, source_addr, nbytes, lc_opt, flags GASNETI_THREAD_GET, 4, (gasnetex_handlerarg_t)(a0), (gasnetex_handlerarg_t)(a1), (gasnetex_handlerarg_t)(a2), (gasnetex_handlerarg_t)(a3))

#define gex_AM_RequestMedium5(team, rank, handler, source_addr, nbytes, lc_opt, flags, a0, a1, a2, a3, a4) \
       gasnetc_AMRequestMediumM(team, rank, handler, source_addr, nbytes, lc_opt, flags GASNETI_THREAD_GET, 5, (gasnetex_handlerarg_t)(a0), (gasnetex_handlerarg_t)(a1), (gasnetex_handlerarg_t)(a2), (gasnetex_handlerarg_t)(a3), (gasnetex_handlerarg_t)(a4))
#define gex_AM_RequestMedium6(team, rank, handler, source_addr, nbytes, lc_opt, flags, a0, a1, a2, a3, a4, a5) \
       gasnetc_AMRequestMediumM(team, rank, handler, source_addr, nbytes, lc_opt, flags GASNETI_THREAD_GET, 6, (gasnetex_handlerarg_t)(a0), (gasnetex_handlerarg_t)(a1), (gasnetex_handlerarg_t)(a2), (gasnetex_handlerarg_t)(a3), (gasnetex_handlerarg_t)(a4), (gasnetex_handlerarg_t)(a5))
#define gex_AM_RequestMedium7(team, rank, handler, source_addr, nbytes, lc_opt, flags, a0, a1, a2, a3, a4, a5, a6) \
       gasnetc_AMRequestMediumM(team, rank, handler, source_addr, nbytes, lc_opt, flags GASNETI_THREAD_GET, 7, (gasnetex_handlerarg_t)(a0), (gasnetex_handlerarg_t)(a1), (gasnetex_handlerarg_t)(a2), (gasnetex_handlerarg_t)(a3), (gasnetex_handlerarg_t)(a4), (gasnetex_handlerarg_t)(a5), (gasnetex_handlerarg_t)(a6))
#define gex_AM_RequestMedium8(team, rank, handler, source_addr, nbytes, lc_opt, flags, a0, a1, a2, a3, a4, a5, a6, a7) \
       gasnetc_AMRequestMediumM(team, rank, handler, source_addr, nbytes, lc_opt, flags GASNETI_THREAD_GET, 8, (gasnetex_handlerarg_t)(a0), (gasnetex_handlerarg_t)(a1), (gasnetex_handlerarg_t)(a2), (gasnetex_handlerarg_t)(a3), (gasnetex_handlerarg_t)(a4), (gasnetex_handlerarg_t)(a5), (gasnetex_handlerarg_t)(a6), (gasnetex_handlerarg_t)(a7))

#define gex_AM_RequestMedium9(team, rank, handler, source_addr, nbytes, lc_opt, flags, a0, a1, a2, a3, a4, a5, a6, a7, a8 ) \
        gasnetc_AMRequestMediumM(team, rank, handler, source_addr, nbytes, lc_opt, flags GASNETI_THREAD_GET, 9, (gasnetex_handlerarg_t)(a0), (gasnetex_handlerarg_t)(a1), (gasnetex_handlerarg_t)(a2), (gasnetex_handlerarg_t)(a3), (gasnetex_handlerarg_t)(a4), (gasnetex_handlerarg_t)(a5), (gasnetex_handlerarg_t)(a6), (gasnetex_handlerarg_t)(a7), (gasnetex_handlerarg_t)(a8))
#define gex_AM_RequestMedium10(team, rank, handler, source_addr, nbytes, lc_opt, flags, a0, a1, a2, a3, a4, a5, a6, a7, a8, a9) \
        gasnetc_AMRequestMediumM(team, rank, handler, source_addr, nbytes, lc_opt, flags GASNETI_THREAD_GET, 10, (gasnetex_handlerarg_t)(a0), (gasnetex_handlerarg_t)(a1), (gasnetex_handlerarg_t)(a2), (gasnetex_handlerarg_t)(a3), (gasnetex_handlerarg_t)(a4), (gasnetex_handlerarg_t)(a5), (gasnetex_handlerarg_t)(a6), (gasnetex_handlerarg_t)(a7), (gasnetex_handlerarg_t)(a8), (gasnetex_handlerarg_t)(a9))
#define gex_AM_RequestMedium11(team, rank, handler, source_addr, nbytes, lc_opt, flags, a0, a1, a2, a3, a4, a5, a6, a7, a8, a9, a10) \
        gasnetc_AMRequestMediumM(team, rank, handler, source_addr, nbytes, lc_opt, flags GASNETI_THREAD_GET, 11, (gasnetex_handlerarg_t)(a0), (gasnetex_handlerarg_t)(a1), (gasnetex_handlerarg_t)(a2), (gasnetex_handlerarg_t)(a3), (gasnetex_handlerarg_t)(a4), (gasnetex_handlerarg_t)(a5), (gasnetex_handlerarg_t)(a6), (gasnetex_handlerarg_t)(a7), (gasnetex_handlerarg_t)(a8), (gasnetex_handlerarg_t)(a9), (gasnetex_handlerarg_t)(a10))
#define gex_AM_RequestMedium12(team, rank, handler, source_addr, nbytes, lc_opt, flags, a0, a1, a2, a3, a4, a5, a6, a7, a8, a9, a10, a11) \
        gasnetc_AMRequestMediumM(team, rank, handler, source_addr, nbytes, lc_opt, flags GASNETI_THREAD_GET, 12, (gasnetex_handlerarg_t)(a0), (gasnetex_handlerarg_t)(a1), (gasnetex_handlerarg_t)(a2), (gasnetex_handlerarg_t)(a3), (gasnetex_handlerarg_t)(a4), (gasnetex_handlerarg_t)(a5), (gasnetex_handlerarg_t)(a6), (gasnetex_handlerarg_t)(a7), (gasnetex_handlerarg_t)(a8), (gasnetex_handlerarg_t)(a9), (gasnetex_handlerarg_t)(a10), (gasnetex_handlerarg_t)(a11))

#define gex_AM_RequestMedium13(team, rank, handler, source_addr, nbytes, lc_opt, flags, a0, a1, a2, a3, a4, a5, a6, a7, a8, a9, a10, a11, a12) \
        gasnetc_AMRequestMediumM(team, rank, handler, source_addr, nbytes, lc_opt, flags GASNETI_THREAD_GET, 13, (gasnetex_handlerarg_t)(a0), (gasnetex_handlerarg_t)(a1), (gasnetex_handlerarg_t)(a2), (gasnetex_handlerarg_t)(a3), (gasnetex_handlerarg_t)(a4), (gasnetex_handlerarg_t)(a5), (gasnetex_handlerarg_t)(a6), (gasnetex_handlerarg_t)(a7), (gasnetex_handlerarg_t)(a8), (gasnetex_handlerarg_t)(a9), (gasnetex_handlerarg_t)(a10), (gasnetex_handlerarg_t)(a11), (gasnetex_handlerarg_t)(a12))
#define gex_AM_RequestMedium14(team, rank, handler, source_addr, nbytes, lc_opt, flags, a0, a1, a2, a3, a4, a5, a6, a7, a8, a9, a10, a11, a12, a13) \
        gasnetc_AMRequestMediumM(team, rank, handler, source_addr, nbytes, lc_opt, flags GASNETI_THREAD_GET, 14, (gasnetex_handlerarg_t)(a0), (gasnetex_handlerarg_t)(a1), (gasnetex_handlerarg_t)(a2), (gasnetex_handlerarg_t)(a3), (gasnetex_handlerarg_t)(a4), (gasnetex_handlerarg_t)(a5), (gasnetex_handlerarg_t)(a6), (gasnetex_handlerarg_t)(a7), (gasnetex_handlerarg_t)(a8), (gasnetex_handlerarg_t)(a9), (gasnetex_handlerarg_t)(a10), (gasnetex_handlerarg_t)(a11), (gasnetex_handlerarg_t)(a12), (gasnetex_handlerarg_t)(a13))
#define gex_AM_RequestMedium15(team, rank, handler, source_addr, nbytes, lc_opt, flags, a0, a1, a2, a3, a4, a5, a6, a7, a8, a9, a10, a11, a12, a13, a14) \
        gasnetc_AMRequestMediumM(team, rank, handler, source_addr, nbytes, lc_opt, flags GASNETI_THREAD_GET, 15, (gasnetex_handlerarg_t)(a0), (gasnetex_handlerarg_t)(a1), (gasnetex_handlerarg_t)(a2), (gasnetex_handlerarg_t)(a3), (gasnetex_handlerarg_t)(a4), (gasnetex_handlerarg_t)(a5), (gasnetex_handlerarg_t)(a6), (gasnetex_handlerarg_t)(a7), (gasnetex_handlerarg_t)(a8), (gasnetex_handlerarg_t)(a9), (gasnetex_handlerarg_t)(a10), (gasnetex_handlerarg_t)(a11), (gasnetex_handlerarg_t)(a12), (gasnetex_handlerarg_t)(a13), (gasnetex_handlerarg_t)(a14))
#define gex_AM_RequestMedium16(team, rank, handler, source_addr, nbytes, lc_opt, flags, a0, a1, a2, a3, a4, a5, a6, a7, a8, a9, a10, a11, a12, a13, a14, a15) \
        gasnetc_AMRequestMediumM(team, rank, handler, source_addr, nbytes, lc_opt, flags GASNETI_THREAD_GET, 16, (gasnetex_handlerarg_t)(a0), (gasnetex_handlerarg_t)(a1), (gasnetex_handlerarg_t)(a2), (gasnetex_handlerarg_t)(a3), (gasnetex_handlerarg_t)(a4), (gasnetex_handlerarg_t)(a5), (gasnetex_handlerarg_t)(a6), (gasnetex_handlerarg_t)(a7), (gasnetex_handlerarg_t)(a8), (gasnetex_handlerarg_t)(a9), (gasnetex_handlerarg_t)(a10), (gasnetex_handlerarg_t)(a11), (gasnetex_handlerarg_t)(a12), (gasnetex_handlerarg_t)(a13), (gasnetex_handlerarg_t)(a14), (gasnetex_handlerarg_t)(a15))
/* ------------------------------------------------------------------------------------ */
#define gex_AM_RequestLong0(team, rank, handler, source_addr, nbytes, dest_addr, lc_opt, flags) \
       gasnetc_AMRequestLongM(team, rank, handler, source_addr, nbytes, dest_addr, lc_opt, flags GASNETI_THREAD_GET, 0)
#define gex_AM_RequestLong1(team, rank, handler, source_addr, nbytes, dest_addr, lc_opt, flags, a0) \
       gasnetc_AMRequestLongM(team, rank, handler, source_addr, nbytes, dest_addr, lc_opt, flags GASNETI_THREAD_GET, 1, (gasnetex_handlerarg_t)(a0))
#define gex_AM_RequestLong2(team, rank, handler, source_addr, nbytes, dest_addr, lc_opt, flags, a0, a1) \
       gasnetc_AMRequestLongM(team, rank, handler, source_addr, nbytes, dest_addr, lc_opt, flags GASNETI_THREAD_GET, 2, (gasnetex_handlerarg_t)(a0), (gasnetex_handlerarg_t)(a1))
#define gex_AM_RequestLong3(team, rank, handler, source_addr, nbytes, dest_addr, lc_opt, flags, a0, a1, a2) \
       gasnetc_AMRequestLongM(team, rank, handler, source_addr, nbytes, dest_addr, lc_opt, flags GASNETI_THREAD_GET, 3, (gasnetex_handlerarg_t)(a0), (gasnetex_handlerarg_t)(a1), (gasnetex_handlerarg_t)(a2))
#define gex_AM_RequestLong4(team, rank, handler, source_addr, nbytes, dest_addr, lc_opt, flags, a0, a1, a2, a3) \
       gasnetc_AMRequestLongM(team, rank, handler, source_addr, nbytes, dest_addr, lc_opt, flags GASNETI_THREAD_GET, 4, (gasnetex_handlerarg_t)(a0), (gasnetex_handlerarg_t)(a1), (gasnetex_handlerarg_t)(a2), (gasnetex_handlerarg_t)(a3))

#define gex_AM_RequestLong5(team, rank, handler, source_addr, nbytes, dest_addr, lc_opt, flags, a0, a1, a2, a3, a4) \
       gasnetc_AMRequestLongM(team, rank, handler, source_addr, nbytes, dest_addr, lc_opt, flags GASNETI_THREAD_GET, 5, (gasnetex_handlerarg_t)(a0), (gasnetex_handlerarg_t)(a1), (gasnetex_handlerarg_t)(a2), (gasnetex_handlerarg_t)(a3), (gasnetex_handlerarg_t)(a4))
#define gex_AM_RequestLong6(team, rank, handler, source_addr, nbytes, dest_addr, lc_opt, flags, a0, a1, a2, a3, a4, a5) \
       gasnetc_AMRequestLongM(team, rank, handler, source_addr, nbytes, dest_addr, lc_opt, flags GASNETI_THREAD_GET, 6, (gasnetex_handlerarg_t)(a0), (gasnetex_handlerarg_t)(a1), (gasnetex_handlerarg_t)(a2), (gasnetex_handlerarg_t)(a3), (gasnetex_handlerarg_t)(a4), (gasnetex_handlerarg_t)(a5))
#define gex_AM_RequestLong7(team, rank, handler, source_addr, nbytes, dest_addr, lc_opt, flags, a0, a1, a2, a3, a4, a5, a6) \
       gasnetc_AMRequestLongM(team, rank, handler, source_addr, nbytes, dest_addr, lc_opt, flags GASNETI_THREAD_GET, 7, (gasnetex_handlerarg_t)(a0), (gasnetex_handlerarg_t)(a1), (gasnetex_handlerarg_t)(a2), (gasnetex_handlerarg_t)(a3), (gasnetex_handlerarg_t)(a4), (gasnetex_handlerarg_t)(a5), (gasnetex_handlerarg_t)(a6))
#define gex_AM_RequestLong8(team, rank, handler, source_addr, nbytes, dest_addr, lc_opt, flags, a0, a1, a2, a3, a4, a5, a6, a7) \
       gasnetc_AMRequestLongM(team, rank, handler, source_addr, nbytes, dest_addr, lc_opt, flags GASNETI_THREAD_GET, 8, (gasnetex_handlerarg_t)(a0), (gasnetex_handlerarg_t)(a1), (gasnetex_handlerarg_t)(a2), (gasnetex_handlerarg_t)(a3), (gasnetex_handlerarg_t)(a4), (gasnetex_handlerarg_t)(a5), (gasnetex_handlerarg_t)(a6), (gasnetex_handlerarg_t)(a7))

#define gex_AM_RequestLong9(team, rank, handler, source_addr, nbytes, dest_addr, lc_opt, flags, a0, a1, a2, a3, a4, a5, a6, a7, a8 ) \
        gasnetc_AMRequestLongM(team, rank, handler, source_addr, nbytes, dest_addr, lc_opt, flags GASNETI_THREAD_GET, 9, (gasnetex_handlerarg_t)(a0), (gasnetex_handlerarg_t)(a1), (gasnetex_handlerarg_t)(a2), (gasnetex_handlerarg_t)(a3), (gasnetex_handlerarg_t)(a4), (gasnetex_handlerarg_t)(a5), (gasnetex_handlerarg_t)(a6), (gasnetex_handlerarg_t)(a7), (gasnetex_handlerarg_t)(a8))
#define gex_AM_RequestLong10(team, rank, handler, source_addr, nbytes, dest_addr, lc_opt, flags, a0, a1, a2, a3, a4, a5, a6, a7, a8, a9) \
        gasnetc_AMRequestLongM(team, rank, handler, source_addr, nbytes, dest_addr, lc_opt, flags GASNETI_THREAD_GET, 10, (gasnetex_handlerarg_t)(a0), (gasnetex_handlerarg_t)(a1), (gasnetex_handlerarg_t)(a2), (gasnetex_handlerarg_t)(a3), (gasnetex_handlerarg_t)(a4), (gasnetex_handlerarg_t)(a5), (gasnetex_handlerarg_t)(a6), (gasnetex_handlerarg_t)(a7), (gasnetex_handlerarg_t)(a8), (gasnetex_handlerarg_t)(a9))
#define gex_AM_RequestLong11(team, rank, handler, source_addr, nbytes, dest_addr, lc_opt, flags, a0, a1, a2, a3, a4, a5, a6, a7, a8, a9, a10) \
        gasnetc_AMRequestLongM(team, rank, handler, source_addr, nbytes, dest_addr, lc_opt, flags GASNETI_THREAD_GET, 11, (gasnetex_handlerarg_t)(a0), (gasnetex_handlerarg_t)(a1), (gasnetex_handlerarg_t)(a2), (gasnetex_handlerarg_t)(a3), (gasnetex_handlerarg_t)(a4), (gasnetex_handlerarg_t)(a5), (gasnetex_handlerarg_t)(a6), (gasnetex_handlerarg_t)(a7), (gasnetex_handlerarg_t)(a8), (gasnetex_handlerarg_t)(a9), (gasnetex_handlerarg_t)(a10))
#define gex_AM_RequestLong12(team, rank, handler, source_addr, nbytes, dest_addr, lc_opt, flags, a0, a1, a2, a3, a4, a5, a6, a7, a8, a9, a10, a11) \
        gasnetc_AMRequestLongM(team, rank, handler, source_addr, nbytes, dest_addr, lc_opt, flags GASNETI_THREAD_GET, 12, (gasnetex_handlerarg_t)(a0), (gasnetex_handlerarg_t)(a1), (gasnetex_handlerarg_t)(a2), (gasnetex_handlerarg_t)(a3), (gasnetex_handlerarg_t)(a4), (gasnetex_handlerarg_t)(a5), (gasnetex_handlerarg_t)(a6), (gasnetex_handlerarg_t)(a7), (gasnetex_handlerarg_t)(a8), (gasnetex_handlerarg_t)(a9), (gasnetex_handlerarg_t)(a10), (gasnetex_handlerarg_t)(a11))

#define gex_AM_RequestLong13(team, rank, handler, source_addr, nbytes, dest_addr, lc_opt, flags, a0, a1, a2, a3, a4, a5, a6, a7, a8, a9, a10, a11, a12) \
        gasnetc_AMRequestLongM(team, rank, handler, source_addr, nbytes, dest_addr, lc_opt, flags GASNETI_THREAD_GET, 13, (gasnetex_handlerarg_t)(a0), (gasnetex_handlerarg_t)(a1), (gasnetex_handlerarg_t)(a2), (gasnetex_handlerarg_t)(a3), (gasnetex_handlerarg_t)(a4), (gasnetex_handlerarg_t)(a5), (gasnetex_handlerarg_t)(a6), (gasnetex_handlerarg_t)(a7), (gasnetex_handlerarg_t)(a8), (gasnetex_handlerarg_t)(a9), (gasnetex_handlerarg_t)(a10), (gasnetex_handlerarg_t)(a11), (gasnetex_handlerarg_t)(a12))
#define gex_AM_RequestLong14(team, rank, handler, source_addr, nbytes, dest_addr, lc_opt, flags, a0, a1, a2, a3, a4, a5, a6, a7, a8, a9, a10, a11, a12, a13) \
        gasnetc_AMRequestLongM(team, rank, handler, source_addr, nbytes, dest_addr, lc_opt, flags GASNETI_THREAD_GET, 14, (gasnetex_handlerarg_t)(a0), (gasnetex_handlerarg_t)(a1), (gasnetex_handlerarg_t)(a2), (gasnetex_handlerarg_t)(a3), (gasnetex_handlerarg_t)(a4), (gasnetex_handlerarg_t)(a5), (gasnetex_handlerarg_t)(a6), (gasnetex_handlerarg_t)(a7), (gasnetex_handlerarg_t)(a8), (gasnetex_handlerarg_t)(a9), (gasnetex_handlerarg_t)(a10), (gasnetex_handlerarg_t)(a11), (gasnetex_handlerarg_t)(a12), (gasnetex_handlerarg_t)(a13))
#define gex_AM_RequestLong15(team, rank, handler, source_addr, nbytes, dest_addr, lc_opt, flags, a0, a1, a2, a3, a4, a5, a6, a7, a8, a9, a10, a11, a12, a13, a14) \
        gasnetc_AMRequestLongM(team, rank, handler, source_addr, nbytes, dest_addr, lc_opt, flags GASNETI_THREAD_GET, 15, (gasnetex_handlerarg_t)(a0), (gasnetex_handlerarg_t)(a1), (gasnetex_handlerarg_t)(a2), (gasnetex_handlerarg_t)(a3), (gasnetex_handlerarg_t)(a4), (gasnetex_handlerarg_t)(a5), (gasnetex_handlerarg_t)(a6), (gasnetex_handlerarg_t)(a7), (gasnetex_handlerarg_t)(a8), (gasnetex_handlerarg_t)(a9), (gasnetex_handlerarg_t)(a10), (gasnetex_handlerarg_t)(a11), (gasnetex_handlerarg_t)(a12), (gasnetex_handlerarg_t)(a13), (gasnetex_handlerarg_t)(a14))
#define gex_AM_RequestLong16(team, rank, handler, source_addr, nbytes, dest_addr, lc_opt, flags, a0, a1, a2, a3, a4, a5, a6, a7, a8, a9, a10, a11, a12, a13, a14, a15) \
        gasnetc_AMRequestLongM(team, rank, handler, source_addr, nbytes, dest_addr, lc_opt, flags GASNETI_THREAD_GET, 16, (gasnetex_handlerarg_t)(a0), (gasnetex_handlerarg_t)(a1), (gasnetex_handlerarg_t)(a2), (gasnetex_handlerarg_t)(a3), (gasnetex_handlerarg_t)(a4), (gasnetex_handlerarg_t)(a5), (gasnetex_handlerarg_t)(a6), (gasnetex_handlerarg_t)(a7), (gasnetex_handlerarg_t)(a8), (gasnetex_handlerarg_t)(a9), (gasnetex_handlerarg_t)(a10), (gasnetex_handlerarg_t)(a11), (gasnetex_handlerarg_t)(a12), (gasnetex_handlerarg_t)(a13), (gasnetex_handlerarg_t)(a14), (gasnetex_handlerarg_t)(a15))
/* ------------------------------------------------------------------------------------ */
#define gex_AM_ReplyShort0(token, handler, flags) \
       gasnetc_AMReplyShortM(token, handler, flags, 0)
#define gex_AM_ReplyShort1(token, handler, flags, a0) \
       gasnetc_AMReplyShortM(token, handler, flags, 1, (gasnetex_handlerarg_t)(a0))
#define gex_AM_ReplyShort2(token, handler, flags, a0, a1) \
       gasnetc_AMReplyShortM(token, handler, flags, 2, (gasnetex_handlerarg_t)(a0), (gasnetex_handlerarg_t)(a1))
#define gex_AM_ReplyShort3(token, handler, flags, a0, a1, a2) \
       gasnetc_AMReplyShortM(token, handler, flags, 3, (gasnetex_handlerarg_t)(a0), (gasnetex_handlerarg_t)(a1), (gasnetex_handlerarg_t)(a2))
#define gex_AM_ReplyShort4(token, handler, flags, a0, a1, a2, a3) \
       gasnetc_AMReplyShortM(token, handler, flags, 4, (gasnetex_handlerarg_t)(a0), (gasnetex_handlerarg_t)(a1), (gasnetex_handlerarg_t)(a2), (gasnetex_handlerarg_t)(a3))

#define gex_AM_ReplyShort5(token, handler, flags, a0, a1, a2, a3, a4) \
       gasnetc_AMReplyShortM(token, handler, flags, 5, (gasnetex_handlerarg_t)(a0), (gasnetex_handlerarg_t)(a1), (gasnetex_handlerarg_t)(a2), (gasnetex_handlerarg_t)(a3), (gasnetex_handlerarg_t)(a4))
#define gex_AM_ReplyShort6(token, handler, flags, a0, a1, a2, a3, a4, a5) \
       gasnetc_AMReplyShortM(token, handler, flags, 6, (gasnetex_handlerarg_t)(a0), (gasnetex_handlerarg_t)(a1), (gasnetex_handlerarg_t)(a2), (gasnetex_handlerarg_t)(a3), (gasnetex_handlerarg_t)(a4), (gasnetex_handlerarg_t)(a5))
#define gex_AM_ReplyShort7(token, handler, flags, a0, a1, a2, a3, a4, a5, a6) \
       gasnetc_AMReplyShortM(token, handler, flags, 7, (gasnetex_handlerarg_t)(a0), (gasnetex_handlerarg_t)(a1), (gasnetex_handlerarg_t)(a2), (gasnetex_handlerarg_t)(a3), (gasnetex_handlerarg_t)(a4), (gasnetex_handlerarg_t)(a5), (gasnetex_handlerarg_t)(a6))
#define gex_AM_ReplyShort8(token, handler, flags, a0, a1, a2, a3, a4, a5, a6, a7) \
       gasnetc_AMReplyShortM(token, handler, flags, 8, (gasnetex_handlerarg_t)(a0), (gasnetex_handlerarg_t)(a1), (gasnetex_handlerarg_t)(a2), (gasnetex_handlerarg_t)(a3), (gasnetex_handlerarg_t)(a4), (gasnetex_handlerarg_t)(a5), (gasnetex_handlerarg_t)(a6), (gasnetex_handlerarg_t)(a7))

#define gex_AM_ReplyShort9(token, handler, flags, a0, a1, a2, a3, a4, a5, a6, a7, a8 ) \
        gasnetc_AMReplyShortM(token, handler, flags, 9, (gasnetex_handlerarg_t)(a0), (gasnetex_handlerarg_t)(a1), (gasnetex_handlerarg_t)(a2), (gasnetex_handlerarg_t)(a3), (gasnetex_handlerarg_t)(a4), (gasnetex_handlerarg_t)(a5), (gasnetex_handlerarg_t)(a6), (gasnetex_handlerarg_t)(a7), (gasnetex_handlerarg_t)(a8))
#define gex_AM_ReplyShort10(token, handler, flags, a0, a1, a2, a3, a4, a5, a6, a7, a8, a9) \
        gasnetc_AMReplyShortM(token, handler, flags, 10, (gasnetex_handlerarg_t)(a0), (gasnetex_handlerarg_t)(a1), (gasnetex_handlerarg_t)(a2), (gasnetex_handlerarg_t)(a3), (gasnetex_handlerarg_t)(a4), (gasnetex_handlerarg_t)(a5), (gasnetex_handlerarg_t)(a6), (gasnetex_handlerarg_t)(a7), (gasnetex_handlerarg_t)(a8), (gasnetex_handlerarg_t)(a9))
#define gex_AM_ReplyShort11(token, handler, flags, a0, a1, a2, a3, a4, a5, a6, a7, a8, a9, a10) \
        gasnetc_AMReplyShortM(token, handler, flags, 11, (gasnetex_handlerarg_t)(a0), (gasnetex_handlerarg_t)(a1), (gasnetex_handlerarg_t)(a2), (gasnetex_handlerarg_t)(a3), (gasnetex_handlerarg_t)(a4), (gasnetex_handlerarg_t)(a5), (gasnetex_handlerarg_t)(a6), (gasnetex_handlerarg_t)(a7), (gasnetex_handlerarg_t)(a8), (gasnetex_handlerarg_t)(a9), (gasnetex_handlerarg_t)(a10))
#define gex_AM_ReplyShort12(token, handler, flags, a0, a1, a2, a3, a4, a5, a6, a7, a8, a9, a10, a11) \
        gasnetc_AMReplyShortM(token, handler, flags, 12, (gasnetex_handlerarg_t)(a0), (gasnetex_handlerarg_t)(a1), (gasnetex_handlerarg_t)(a2), (gasnetex_handlerarg_t)(a3), (gasnetex_handlerarg_t)(a4), (gasnetex_handlerarg_t)(a5), (gasnetex_handlerarg_t)(a6), (gasnetex_handlerarg_t)(a7), (gasnetex_handlerarg_t)(a8), (gasnetex_handlerarg_t)(a9), (gasnetex_handlerarg_t)(a10), (gasnetex_handlerarg_t)(a11))

#define gex_AM_ReplyShort13(token, handler, flags, a0, a1, a2, a3, a4, a5, a6, a7, a8, a9, a10, a11, a12) \
        gasnetc_AMReplyShortM(token, handler, flags, 13, (gasnetex_handlerarg_t)(a0), (gasnetex_handlerarg_t)(a1), (gasnetex_handlerarg_t)(a2), (gasnetex_handlerarg_t)(a3), (gasnetex_handlerarg_t)(a4), (gasnetex_handlerarg_t)(a5), (gasnetex_handlerarg_t)(a6), (gasnetex_handlerarg_t)(a7), (gasnetex_handlerarg_t)(a8), (gasnetex_handlerarg_t)(a9), (gasnetex_handlerarg_t)(a10), (gasnetex_handlerarg_t)(a11), (gasnetex_handlerarg_t)(a12))
#define gex_AM_ReplyShort14(token, handler, flags, a0, a1, a2, a3, a4, a5, a6, a7, a8, a9, a10, a11, a12, a13) \
        gasnetc_AMReplyShortM(token, handler, flags, 14, (gasnetex_handlerarg_t)(a0), (gasnetex_handlerarg_t)(a1), (gasnetex_handlerarg_t)(a2), (gasnetex_handlerarg_t)(a3), (gasnetex_handlerarg_t)(a4), (gasnetex_handlerarg_t)(a5), (gasnetex_handlerarg_t)(a6), (gasnetex_handlerarg_t)(a7), (gasnetex_handlerarg_t)(a8), (gasnetex_handlerarg_t)(a9), (gasnetex_handlerarg_t)(a10), (gasnetex_handlerarg_t)(a11), (gasnetex_handlerarg_t)(a12), (gasnetex_handlerarg_t)(a13))
#define gex_AM_ReplyShort15(token, handler, flags, a0, a1, a2, a3, a4, a5, a6, a7, a8, a9, a10, a11, a12, a13, a14) \
        gasnetc_AMReplyShortM(token, handler, flags, 15, (gasnetex_handlerarg_t)(a0), (gasnetex_handlerarg_t)(a1), (gasnetex_handlerarg_t)(a2), (gasnetex_handlerarg_t)(a3), (gasnetex_handlerarg_t)(a4), (gasnetex_handlerarg_t)(a5), (gasnetex_handlerarg_t)(a6), (gasnetex_handlerarg_t)(a7), (gasnetex_handlerarg_t)(a8), (gasnetex_handlerarg_t)(a9), (gasnetex_handlerarg_t)(a10), (gasnetex_handlerarg_t)(a11), (gasnetex_handlerarg_t)(a12), (gasnetex_handlerarg_t)(a13), (gasnetex_handlerarg_t)(a14))
#define gex_AM_ReplyShort16(token, handler, flags, a0, a1, a2, a3, a4, a5, a6, a7, a8, a9, a10, a11, a12, a13, a14, a15) \
        gasnetc_AMReplyShortM(token, handler, flags, 16, (gasnetex_handlerarg_t)(a0), (gasnetex_handlerarg_t)(a1), (gasnetex_handlerarg_t)(a2), (gasnetex_handlerarg_t)(a3), (gasnetex_handlerarg_t)(a4), (gasnetex_handlerarg_t)(a5), (gasnetex_handlerarg_t)(a6), (gasnetex_handlerarg_t)(a7), (gasnetex_handlerarg_t)(a8), (gasnetex_handlerarg_t)(a9), (gasnetex_handlerarg_t)(a10), (gasnetex_handlerarg_t)(a11), (gasnetex_handlerarg_t)(a12), (gasnetex_handlerarg_t)(a13), (gasnetex_handlerarg_t)(a14), (gasnetex_handlerarg_t)(a15))
/* ------------------------------------------------------------------------------------ */
#define gex_AM_ReplyMedium0(token, handler, source_addr, nbytes, lc_opt, flags) \
       gasnetc_AMReplyMediumM(token, handler, source_addr, nbytes, lc_opt, flags, 0)
#define gex_AM_ReplyMedium1(token, handler, source_addr, nbytes, lc_opt, flags, a0) \
       gasnetc_AMReplyMediumM(token, handler, source_addr, nbytes, lc_opt, flags, 1, (gasnetex_handlerarg_t)(a0))
#define gex_AM_ReplyMedium2(token, handler, source_addr, nbytes, lc_opt, flags, a0, a1) \
       gasnetc_AMReplyMediumM(token, handler, source_addr, nbytes, lc_opt, flags, 2, (gasnetex_handlerarg_t)(a0), (gasnetex_handlerarg_t)(a1))
#define gex_AM_ReplyMedium3(token, handler, source_addr, nbytes, lc_opt, flags, a0, a1, a2) \
       gasnetc_AMReplyMediumM(token, handler, source_addr, nbytes, lc_opt, flags, 3, (gasnetex_handlerarg_t)(a0), (gasnetex_handlerarg_t)(a1), (gasnetex_handlerarg_t)(a2))
#define gex_AM_ReplyMedium4(token, handler, source_addr, nbytes, lc_opt, flags, a0, a1, a2, a3) \
       gasnetc_AMReplyMediumM(token, handler, source_addr, nbytes, lc_opt, flags, 4, (gasnetex_handlerarg_t)(a0), (gasnetex_handlerarg_t)(a1), (gasnetex_handlerarg_t)(a2), (gasnetex_handlerarg_t)(a3))

#define gex_AM_ReplyMedium5(token, handler, source_addr, nbytes, lc_opt, flags, a0, a1, a2, a3, a4) \
       gasnetc_AMReplyMediumM(token, handler, source_addr, nbytes, lc_opt, flags, 5, (gasnetex_handlerarg_t)(a0), (gasnetex_handlerarg_t)(a1), (gasnetex_handlerarg_t)(a2), (gasnetex_handlerarg_t)(a3), (gasnetex_handlerarg_t)(a4))
#define gex_AM_ReplyMedium6(token, handler, source_addr, nbytes, lc_opt, flags, a0, a1, a2, a3, a4, a5) \
       gasnetc_AMReplyMediumM(token, handler, source_addr, nbytes, lc_opt, flags, 6, (gasnetex_handlerarg_t)(a0), (gasnetex_handlerarg_t)(a1), (gasnetex_handlerarg_t)(a2), (gasnetex_handlerarg_t)(a3), (gasnetex_handlerarg_t)(a4), (gasnetex_handlerarg_t)(a5))
#define gex_AM_ReplyMedium7(token, handler, source_addr, nbytes, lc_opt, flags, a0, a1, a2, a3, a4, a5, a6) \
       gasnetc_AMReplyMediumM(token, handler, source_addr, nbytes, lc_opt, flags, 7, (gasnetex_handlerarg_t)(a0), (gasnetex_handlerarg_t)(a1), (gasnetex_handlerarg_t)(a2), (gasnetex_handlerarg_t)(a3), (gasnetex_handlerarg_t)(a4), (gasnetex_handlerarg_t)(a5), (gasnetex_handlerarg_t)(a6))
#define gex_AM_ReplyMedium8(token, handler, source_addr, nbytes, lc_opt, flags, a0, a1, a2, a3, a4, a5, a6, a7) \
       gasnetc_AMReplyMediumM(token, handler, source_addr, nbytes, lc_opt, flags, 8, (gasnetex_handlerarg_t)(a0), (gasnetex_handlerarg_t)(a1), (gasnetex_handlerarg_t)(a2), (gasnetex_handlerarg_t)(a3), (gasnetex_handlerarg_t)(a4), (gasnetex_handlerarg_t)(a5), (gasnetex_handlerarg_t)(a6), (gasnetex_handlerarg_t)(a7))

#define gex_AM_ReplyMedium9(token, handler, source_addr, nbytes, lc_opt, flags, a0, a1, a2, a3, a4, a5, a6, a7, a8 ) \
        gasnetc_AMReplyMediumM(token, handler, source_addr, nbytes, lc_opt, flags,  9, (gasnetex_handlerarg_t)(a0), (gasnetex_handlerarg_t)(a1), (gasnetex_handlerarg_t)(a2), (gasnetex_handlerarg_t)(a3), (gasnetex_handlerarg_t)(a4), (gasnetex_handlerarg_t)(a5), (gasnetex_handlerarg_t)(a6), (gasnetex_handlerarg_t)(a7), (gasnetex_handlerarg_t)(a8))
#define gex_AM_ReplyMedium10(token, handler, source_addr, nbytes, lc_opt, flags, a0, a1, a2, a3, a4, a5, a6, a7, a8, a9) \
        gasnetc_AMReplyMediumM(token, handler, source_addr, nbytes, lc_opt, flags, 10, (gasnetex_handlerarg_t)(a0), (gasnetex_handlerarg_t)(a1), (gasnetex_handlerarg_t)(a2), (gasnetex_handlerarg_t)(a3), (gasnetex_handlerarg_t)(a4), (gasnetex_handlerarg_t)(a5), (gasnetex_handlerarg_t)(a6), (gasnetex_handlerarg_t)(a7), (gasnetex_handlerarg_t)(a8), (gasnetex_handlerarg_t)(a9))
#define gex_AM_ReplyMedium11(token, handler, source_addr, nbytes, lc_opt, flags, a0, a1, a2, a3, a4, a5, a6, a7, a8, a9, a10) \
        gasnetc_AMReplyMediumM(token, handler, source_addr, nbytes, lc_opt, flags, 11, (gasnetex_handlerarg_t)(a0), (gasnetex_handlerarg_t)(a1), (gasnetex_handlerarg_t)(a2), (gasnetex_handlerarg_t)(a3), (gasnetex_handlerarg_t)(a4), (gasnetex_handlerarg_t)(a5), (gasnetex_handlerarg_t)(a6), (gasnetex_handlerarg_t)(a7), (gasnetex_handlerarg_t)(a8), (gasnetex_handlerarg_t)(a9), (gasnetex_handlerarg_t)(a10))
#define gex_AM_ReplyMedium12(token, handler, source_addr, nbytes, lc_opt, flags, a0, a1, a2, a3, a4, a5, a6, a7, a8, a9, a10, a11) \
        gasnetc_AMReplyMediumM(token, handler, source_addr, nbytes, lc_opt, flags, 12, (gasnetex_handlerarg_t)(a0), (gasnetex_handlerarg_t)(a1), (gasnetex_handlerarg_t)(a2), (gasnetex_handlerarg_t)(a3), (gasnetex_handlerarg_t)(a4), (gasnetex_handlerarg_t)(a5), (gasnetex_handlerarg_t)(a6), (gasnetex_handlerarg_t)(a7), (gasnetex_handlerarg_t)(a8), (gasnetex_handlerarg_t)(a9), (gasnetex_handlerarg_t)(a10), (gasnetex_handlerarg_t)(a11))

#define gex_AM_ReplyMedium13(token, handler, source_addr, nbytes, lc_opt, flags, a0, a1, a2, a3, a4, a5, a6, a7, a8, a9, a10, a11, a12) \
        gasnetc_AMReplyMediumM(token, handler, source_addr, nbytes, lc_opt, flags, 13, (gasnetex_handlerarg_t)(a0), (gasnetex_handlerarg_t)(a1), (gasnetex_handlerarg_t)(a2), (gasnetex_handlerarg_t)(a3), (gasnetex_handlerarg_t)(a4), (gasnetex_handlerarg_t)(a5), (gasnetex_handlerarg_t)(a6), (gasnetex_handlerarg_t)(a7), (gasnetex_handlerarg_t)(a8), (gasnetex_handlerarg_t)(a9), (gasnetex_handlerarg_t)(a10), (gasnetex_handlerarg_t)(a11), (gasnetex_handlerarg_t)(a12))
#define gex_AM_ReplyMedium14(token, handler, source_addr, nbytes, lc_opt, flags, a0, a1, a2, a3, a4, a5, a6, a7, a8, a9, a10, a11, a12, a13) \
        gasnetc_AMReplyMediumM(token, handler, source_addr, nbytes, lc_opt, flags, 14, (gasnetex_handlerarg_t)(a0), (gasnetex_handlerarg_t)(a1), (gasnetex_handlerarg_t)(a2), (gasnetex_handlerarg_t)(a3), (gasnetex_handlerarg_t)(a4), (gasnetex_handlerarg_t)(a5), (gasnetex_handlerarg_t)(a6), (gasnetex_handlerarg_t)(a7), (gasnetex_handlerarg_t)(a8), (gasnetex_handlerarg_t)(a9), (gasnetex_handlerarg_t)(a10), (gasnetex_handlerarg_t)(a11), (gasnetex_handlerarg_t)(a12), (gasnetex_handlerarg_t)(a13))
#define gex_AM_ReplyMedium15(token, handler, source_addr, nbytes, lc_opt, flags, a0, a1, a2, a3, a4, a5, a6, a7, a8, a9, a10, a11, a12, a13, a14) \
        gasnetc_AMReplyMediumM(token, handler, source_addr, nbytes, lc_opt, flags, 15, (gasnetex_handlerarg_t)(a0), (gasnetex_handlerarg_t)(a1), (gasnetex_handlerarg_t)(a2), (gasnetex_handlerarg_t)(a3), (gasnetex_handlerarg_t)(a4), (gasnetex_handlerarg_t)(a5), (gasnetex_handlerarg_t)(a6), (gasnetex_handlerarg_t)(a7), (gasnetex_handlerarg_t)(a8), (gasnetex_handlerarg_t)(a9), (gasnetex_handlerarg_t)(a10), (gasnetex_handlerarg_t)(a11), (gasnetex_handlerarg_t)(a12), (gasnetex_handlerarg_t)(a13), (gasnetex_handlerarg_t)(a14))
#define gex_AM_ReplyMedium16(token, handler, source_addr, nbytes, lc_opt, flags, a0, a1, a2, a3, a4, a5, a6, a7, a8, a9, a10, a11, a12, a13, a14, a15) \
        gasnetc_AMReplyMediumM(token, handler, source_addr, nbytes, lc_opt, flags, 16, (gasnetex_handlerarg_t)(a0), (gasnetex_handlerarg_t)(a1), (gasnetex_handlerarg_t)(a2), (gasnetex_handlerarg_t)(a3), (gasnetex_handlerarg_t)(a4), (gasnetex_handlerarg_t)(a5), (gasnetex_handlerarg_t)(a6), (gasnetex_handlerarg_t)(a7), (gasnetex_handlerarg_t)(a8), (gasnetex_handlerarg_t)(a9), (gasnetex_handlerarg_t)(a10), (gasnetex_handlerarg_t)(a11), (gasnetex_handlerarg_t)(a12), (gasnetex_handlerarg_t)(a13), (gasnetex_handlerarg_t)(a14), (gasnetex_handlerarg_t)(a15))
/* ------------------------------------------------------------------------------------ */
#define gex_AM_ReplyLong0(token, handler, source_addr, nbytes, dest_addr, lc_opt, flags) \
       gasnetc_AMReplyLongM(token, handler, source_addr, nbytes, dest_addr, lc_opt, flags, 0)
#define gex_AM_ReplyLong1(token, handler, source_addr, nbytes, dest_addr, lc_opt, flags, a0) \
       gasnetc_AMReplyLongM(token, handler, source_addr, nbytes, dest_addr, lc_opt, flags, 1, (gasnetex_handlerarg_t)(a0))
#define gex_AM_ReplyLong2(token, handler, source_addr, nbytes, dest_addr, lc_opt, flags, a0, a1) \
       gasnetc_AMReplyLongM(token, handler, source_addr, nbytes, dest_addr, lc_opt, flags, 2, (gasnetex_handlerarg_t)(a0), (gasnetex_handlerarg_t)(a1))
#define gex_AM_ReplyLong3(token, handler, source_addr, nbytes, dest_addr, lc_opt, flags, a0, a1, a2) \
       gasnetc_AMReplyLongM(token, handler, source_addr, nbytes, dest_addr, lc_opt, flags, 3, (gasnetex_handlerarg_t)(a0), (gasnetex_handlerarg_t)(a1), (gasnetex_handlerarg_t)(a2))
#define gex_AM_ReplyLong4(token, handler, source_addr, nbytes, dest_addr, lc_opt, flags, a0, a1, a2, a3) \
       gasnetc_AMReplyLongM(token, handler, source_addr, nbytes, dest_addr, lc_opt, flags, 4, (gasnetex_handlerarg_t)(a0), (gasnetex_handlerarg_t)(a1), (gasnetex_handlerarg_t)(a2), (gasnetex_handlerarg_t)(a3))

#define gex_AM_ReplyLong5(token, handler, source_addr, nbytes, dest_addr, lc_opt, flags, a0, a1, a2, a3, a4) \
       gasnetc_AMReplyLongM(token, handler, source_addr, nbytes, dest_addr, lc_opt, flags, 5, (gasnetex_handlerarg_t)(a0), (gasnetex_handlerarg_t)(a1), (gasnetex_handlerarg_t)(a2), (gasnetex_handlerarg_t)(a3), (gasnetex_handlerarg_t)(a4))
#define gex_AM_ReplyLong6(token, handler, source_addr, nbytes, dest_addr, lc_opt, flags, a0, a1, a2, a3, a4, a5) \
       gasnetc_AMReplyLongM(token, handler, source_addr, nbytes, dest_addr, lc_opt, flags, 6, (gasnetex_handlerarg_t)(a0), (gasnetex_handlerarg_t)(a1), (gasnetex_handlerarg_t)(a2), (gasnetex_handlerarg_t)(a3), (gasnetex_handlerarg_t)(a4), (gasnetex_handlerarg_t)(a5))
#define gex_AM_ReplyLong7(token, handler, source_addr, nbytes, dest_addr, lc_opt, flags, a0, a1, a2, a3, a4, a5, a6) \
       gasnetc_AMReplyLongM(token, handler, source_addr, nbytes, dest_addr, lc_opt, flags, 7, (gasnetex_handlerarg_t)(a0), (gasnetex_handlerarg_t)(a1), (gasnetex_handlerarg_t)(a2), (gasnetex_handlerarg_t)(a3), (gasnetex_handlerarg_t)(a4), (gasnetex_handlerarg_t)(a5), (gasnetex_handlerarg_t)(a6))
#define gex_AM_ReplyLong8(token, handler, source_addr, nbytes, dest_addr, lc_opt, flags, a0, a1, a2, a3, a4, a5, a6, a7) \
       gasnetc_AMReplyLongM(token, handler, source_addr, nbytes, dest_addr, lc_opt, flags, 8, (gasnetex_handlerarg_t)(a0), (gasnetex_handlerarg_t)(a1), (gasnetex_handlerarg_t)(a2), (gasnetex_handlerarg_t)(a3), (gasnetex_handlerarg_t)(a4), (gasnetex_handlerarg_t)(a5), (gasnetex_handlerarg_t)(a6), (gasnetex_handlerarg_t)(a7))

#define gex_AM_ReplyLong9(token, handler, source_addr, nbytes, dest_addr, lc_opt, flags, a0, a1, a2, a3, a4, a5, a6, a7, a8 ) \
        gasnetc_AMReplyLongM(token, handler, source_addr, nbytes, dest_addr, lc_opt, flags,  9, (gasnetex_handlerarg_t)(a0), (gasnetex_handlerarg_t)(a1), (gasnetex_handlerarg_t)(a2), (gasnetex_handlerarg_t)(a3), (gasnetex_handlerarg_t)(a4), (gasnetex_handlerarg_t)(a5), (gasnetex_handlerarg_t)(a6), (gasnetex_handlerarg_t)(a7), (gasnetex_handlerarg_t)(a8))
#define gex_AM_ReplyLong10(token, handler, source_addr, nbytes, dest_addr, lc_opt, flags, a0, a1, a2, a3, a4, a5, a6, a7, a8, a9) \
        gasnetc_AMReplyLongM(token, handler, source_addr, nbytes, dest_addr, lc_opt, flags, 10, (gasnetex_handlerarg_t)(a0), (gasnetex_handlerarg_t)(a1), (gasnetex_handlerarg_t)(a2), (gasnetex_handlerarg_t)(a3), (gasnetex_handlerarg_t)(a4), (gasnetex_handlerarg_t)(a5), (gasnetex_handlerarg_t)(a6), (gasnetex_handlerarg_t)(a7), (gasnetex_handlerarg_t)(a8), (gasnetex_handlerarg_t)(a9))
#define gex_AM_ReplyLong11(token, handler, source_addr, nbytes, dest_addr, lc_opt, flags, a0, a1, a2, a3, a4, a5, a6, a7, a8, a9, a10) \
        gasnetc_AMReplyLongM(token, handler, source_addr, nbytes, dest_addr, lc_opt, flags, 11, (gasnetex_handlerarg_t)(a0), (gasnetex_handlerarg_t)(a1), (gasnetex_handlerarg_t)(a2), (gasnetex_handlerarg_t)(a3), (gasnetex_handlerarg_t)(a4), (gasnetex_handlerarg_t)(a5), (gasnetex_handlerarg_t)(a6), (gasnetex_handlerarg_t)(a7), (gasnetex_handlerarg_t)(a8), (gasnetex_handlerarg_t)(a9), (gasnetex_handlerarg_t)(a10))
#define gex_AM_ReplyLong12(token, handler, source_addr, nbytes, dest_addr, lc_opt, flags, a0, a1, a2, a3, a4, a5, a6, a7, a8, a9, a10, a11) \
        gasnetc_AMReplyLongM(token, handler, source_addr, nbytes, dest_addr, lc_opt, flags, 12, (gasnetex_handlerarg_t)(a0), (gasnetex_handlerarg_t)(a1), (gasnetex_handlerarg_t)(a2), (gasnetex_handlerarg_t)(a3), (gasnetex_handlerarg_t)(a4), (gasnetex_handlerarg_t)(a5), (gasnetex_handlerarg_t)(a6), (gasnetex_handlerarg_t)(a7), (gasnetex_handlerarg_t)(a8), (gasnetex_handlerarg_t)(a9), (gasnetex_handlerarg_t)(a10), (gasnetex_handlerarg_t)(a11))

#define gex_AM_ReplyLong13(token, handler, source_addr, nbytes, dest_addr, lc_opt, flags, a0, a1, a2, a3, a4, a5, a6, a7, a8, a9, a10, a11, a12) \
        gasnetc_AMReplyLongM(token, handler, source_addr, nbytes, dest_addr, lc_opt, flags, 13, (gasnetex_handlerarg_t)(a0), (gasnetex_handlerarg_t)(a1), (gasnetex_handlerarg_t)(a2), (gasnetex_handlerarg_t)(a3), (gasnetex_handlerarg_t)(a4), (gasnetex_handlerarg_t)(a5), (gasnetex_handlerarg_t)(a6), (gasnetex_handlerarg_t)(a7), (gasnetex_handlerarg_t)(a8), (gasnetex_handlerarg_t)(a9), (gasnetex_handlerarg_t)(a10), (gasnetex_handlerarg_t)(a11), (gasnetex_handlerarg_t)(a12))
#define gex_AM_ReplyLong14(token, handler, source_addr, nbytes, dest_addr, lc_opt, flags, a0, a1, a2, a3, a4, a5, a6, a7, a8, a9, a10, a11, a12, a13) \
        gasnetc_AMReplyLongM(token, handler, source_addr, nbytes, dest_addr, lc_opt, flags, 14, (gasnetex_handlerarg_t)(a0), (gasnetex_handlerarg_t)(a1), (gasnetex_handlerarg_t)(a2), (gasnetex_handlerarg_t)(a3), (gasnetex_handlerarg_t)(a4), (gasnetex_handlerarg_t)(a5), (gasnetex_handlerarg_t)(a6), (gasnetex_handlerarg_t)(a7), (gasnetex_handlerarg_t)(a8), (gasnetex_handlerarg_t)(a9), (gasnetex_handlerarg_t)(a10), (gasnetex_handlerarg_t)(a11), (gasnetex_handlerarg_t)(a12), (gasnetex_handlerarg_t)(a13))
#define gex_AM_ReplyLong15(token, handler, source_addr, nbytes, dest_addr, lc_opt, flags, a0, a1, a2, a3, a4, a5, a6, a7, a8, a9, a10, a11, a12, a13, a14) \
        gasnetc_AMReplyLongM(token, handler, source_addr, nbytes, dest_addr, lc_opt, flags, 15, (gasnetex_handlerarg_t)(a0), (gasnetex_handlerarg_t)(a1), (gasnetex_handlerarg_t)(a2), (gasnetex_handlerarg_t)(a3), (gasnetex_handlerarg_t)(a4), (gasnetex_handlerarg_t)(a5), (gasnetex_handlerarg_t)(a6), (gasnetex_handlerarg_t)(a7), (gasnetex_handlerarg_t)(a8), (gasnetex_handlerarg_t)(a9), (gasnetex_handlerarg_t)(a10), (gasnetex_handlerarg_t)(a11), (gasnetex_handlerarg_t)(a12), (gasnetex_handlerarg_t)(a13), (gasnetex_handlerarg_t)(a14))
#define gex_AM_ReplyLong16(token, handler, source_addr, nbytes, dest_addr, lc_opt, flags, a0, a1, a2, a3, a4, a5, a6, a7, a8, a9, a10, a11, a12, a13, a14, a15) \
        gasnetc_AMReplyLongM(token, handler, source_addr, nbytes, dest_addr, lc_opt, flags, 16, (gasnetex_handlerarg_t)(a0), (gasnetex_handlerarg_t)(a1), (gasnetex_handlerarg_t)(a2), (gasnetex_handlerarg_t)(a3), (gasnetex_handlerarg_t)(a4), (gasnetex_handlerarg_t)(a5), (gasnetex_handlerarg_t)(a6), (gasnetex_handlerarg_t)(a7), (gasnetex_handlerarg_t)(a8), (gasnetex_handlerarg_t)(a9), (gasnetex_handlerarg_t)(a10), (gasnetex_handlerarg_t)(a11), (gasnetex_handlerarg_t)(a12), (gasnetex_handlerarg_t)(a13), (gasnetex_handlerarg_t)(a14), (gasnetex_handlerarg_t)(a15))
/* ------------------------------------------------------------------------------------ */

#endif

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
#define gasnetex_AMRequestShort0(team, rank, handler, flags) \
       gasnetc_AMRequestShortM(team, rank, handler, flags GASNETI_THREAD_GET, 0)
#define gasnetex_AMRequestShort1(team, rank, handler, flags, a0) \
       gasnetc_AMRequestShortM(team, rank, handler, flags GASNETI_THREAD_GET, 1, (gasnetex_handlerarg_t)(a0))
#define gasnetex_AMRequestShort2(team, rank, handler, flags, a0, a1) \
       gasnetc_AMRequestShortM(team, rank, handler, flags GASNETI_THREAD_GET, 2, (gasnetex_handlerarg_t)(a0), (gasnetex_handlerarg_t)(a1))
#define gasnetex_AMRequestShort3(team, rank, handler, flags, a0, a1, a2) \
       gasnetc_AMRequestShortM(team, rank, handler, flags GASNETI_THREAD_GET, 3, (gasnetex_handlerarg_t)(a0), (gasnetex_handlerarg_t)(a1), (gasnetex_handlerarg_t)(a2))
#define gasnetex_AMRequestShort4(team, rank, handler, flags, a0, a1, a2, a3) \
       gasnetc_AMRequestShortM(team, rank, handler, flags GASNETI_THREAD_GET, 4, (gasnetex_handlerarg_t)(a0), (gasnetex_handlerarg_t)(a1), (gasnetex_handlerarg_t)(a2), (gasnetex_handlerarg_t)(a3))

#define gasnetex_AMRequestShort5(team, rank, handler, flags, a0, a1, a2, a3, a4) \
       gasnetc_AMRequestShortM(team, rank, handler, flags GASNETI_THREAD_GET, 5, (gasnetex_handlerarg_t)(a0), (gasnetex_handlerarg_t)(a1), (gasnetex_handlerarg_t)(a2), (gasnetex_handlerarg_t)(a3), (gasnetex_handlerarg_t)(a4))
#define gasnetex_AMRequestShort6(team, rank, handler, flags, a0, a1, a2, a3, a4, a5) \
       gasnetc_AMRequestShortM(team, rank, handler, flags GASNETI_THREAD_GET, 6, (gasnetex_handlerarg_t)(a0), (gasnetex_handlerarg_t)(a1), (gasnetex_handlerarg_t)(a2), (gasnetex_handlerarg_t)(a3), (gasnetex_handlerarg_t)(a4), (gasnetex_handlerarg_t)(a5))
#define gasnetex_AMRequestShort7(team, rank, handler, flags, a0, a1, a2, a3, a4, a5, a6) \
       gasnetc_AMRequestShortM(team, rank, handler, flags GASNETI_THREAD_GET, 7, (gasnetex_handlerarg_t)(a0), (gasnetex_handlerarg_t)(a1), (gasnetex_handlerarg_t)(a2), (gasnetex_handlerarg_t)(a3), (gasnetex_handlerarg_t)(a4), (gasnetex_handlerarg_t)(a5), (gasnetex_handlerarg_t)(a6))
#define gasnetex_AMRequestShort8(team, rank, handler, flags, a0, a1, a2, a3, a4, a5, a6, a7) \
       gasnetc_AMRequestShortM(team, rank, handler, flags GASNETI_THREAD_GET, 8, (gasnetex_handlerarg_t)(a0), (gasnetex_handlerarg_t)(a1), (gasnetex_handlerarg_t)(a2), (gasnetex_handlerarg_t)(a3), (gasnetex_handlerarg_t)(a4), (gasnetex_handlerarg_t)(a5), (gasnetex_handlerarg_t)(a6), (gasnetex_handlerarg_t)(a7))

#define gasnetex_AMRequestShort9(team, rank, handler, flags, a0, a1, a2, a3, a4, a5, a6, a7, a8 ) \
        gasnetc_AMRequestShortM(team, rank, handler, flags GASNETI_THREAD_GET, 9, (gasnetex_handlerarg_t)(a0), (gasnetex_handlerarg_t)(a1), (gasnetex_handlerarg_t)(a2), (gasnetex_handlerarg_t)(a3), (gasnetex_handlerarg_t)(a4), (gasnetex_handlerarg_t)(a5), (gasnetex_handlerarg_t)(a6), (gasnetex_handlerarg_t)(a7), (gasnetex_handlerarg_t)(a8))
#define gasnetex_AMRequestShort10(team, rank, handler, flags, a0, a1, a2, a3, a4, a5, a6, a7, a8, a9) \
        gasnetc_AMRequestShortM(team, rank, handler, flags GASNETI_THREAD_GET, 10, (gasnetex_handlerarg_t)(a0), (gasnetex_handlerarg_t)(a1), (gasnetex_handlerarg_t)(a2), (gasnetex_handlerarg_t)(a3), (gasnetex_handlerarg_t)(a4), (gasnetex_handlerarg_t)(a5), (gasnetex_handlerarg_t)(a6), (gasnetex_handlerarg_t)(a7), (gasnetex_handlerarg_t)(a8), (gasnetex_handlerarg_t)(a9))
#define gasnetex_AMRequestShort11(team, rank, handler, flags, a0, a1, a2, a3, a4, a5, a6, a7, a8, a9, a10) \
        gasnetc_AMRequestShortM(team, rank, handler, flags GASNETI_THREAD_GET, 11, (gasnetex_handlerarg_t)(a0), (gasnetex_handlerarg_t)(a1), (gasnetex_handlerarg_t)(a2), (gasnetex_handlerarg_t)(a3), (gasnetex_handlerarg_t)(a4), (gasnetex_handlerarg_t)(a5), (gasnetex_handlerarg_t)(a6), (gasnetex_handlerarg_t)(a7), (gasnetex_handlerarg_t)(a8), (gasnetex_handlerarg_t)(a9), (gasnetex_handlerarg_t)(a10))
#define gasnetex_AMRequestShort12(team, rank, handler, flags, a0, a1, a2, a3, a4, a5, a6, a7, a8, a9, a10, a11) \
        gasnetc_AMRequestShortM(team, rank, handler, flags GASNETI_THREAD_GET, 12, (gasnetex_handlerarg_t)(a0), (gasnetex_handlerarg_t)(a1), (gasnetex_handlerarg_t)(a2), (gasnetex_handlerarg_t)(a3), (gasnetex_handlerarg_t)(a4), (gasnetex_handlerarg_t)(a5), (gasnetex_handlerarg_t)(a6), (gasnetex_handlerarg_t)(a7), (gasnetex_handlerarg_t)(a8), (gasnetex_handlerarg_t)(a9), (gasnetex_handlerarg_t)(a10), (gasnetex_handlerarg_t)(a11))

#define gasnetex_AMRequestShort13(team, rank, handler, flags, a0, a1, a2, a3, a4, a5, a6, a7, a8, a9, a10, a11, a12) \
        gasnetc_AMRequestShortM(team, rank, handler, flags GASNETI_THREAD_GET, 13, (gasnetex_handlerarg_t)(a0), (gasnetex_handlerarg_t)(a1), (gasnetex_handlerarg_t)(a2), (gasnetex_handlerarg_t)(a3), (gasnetex_handlerarg_t)(a4), (gasnetex_handlerarg_t)(a5), (gasnetex_handlerarg_t)(a6), (gasnetex_handlerarg_t)(a7), (gasnetex_handlerarg_t)(a8), (gasnetex_handlerarg_t)(a9), (gasnetex_handlerarg_t)(a10), (gasnetex_handlerarg_t)(a11), (gasnetex_handlerarg_t)(a12))
#define gasnetex_AMRequestShort14(team, rank, handler, flags, a0, a1, a2, a3, a4, a5, a6, a7, a8, a9, a10, a11, a12, a13) \
        gasnetc_AMRequestShortM(team, rank, handler, flags GASNETI_THREAD_GET, 14, (gasnetex_handlerarg_t)(a0), (gasnetex_handlerarg_t)(a1), (gasnetex_handlerarg_t)(a2), (gasnetex_handlerarg_t)(a3), (gasnetex_handlerarg_t)(a4), (gasnetex_handlerarg_t)(a5), (gasnetex_handlerarg_t)(a6), (gasnetex_handlerarg_t)(a7), (gasnetex_handlerarg_t)(a8), (gasnetex_handlerarg_t)(a9), (gasnetex_handlerarg_t)(a10), (gasnetex_handlerarg_t)(a11), (gasnetex_handlerarg_t)(a12), (gasnetex_handlerarg_t)(a13))
#define gasnetex_AMRequestShort15(team, rank, handler, flags, a0, a1, a2, a3, a4, a5, a6, a7, a8, a9, a10, a11, a12, a13, a14) \
        gasnetc_AMRequestShortM(team, rank, handler, flags GASNETI_THREAD_GET, 15, (gasnetex_handlerarg_t)(a0), (gasnetex_handlerarg_t)(a1), (gasnetex_handlerarg_t)(a2), (gasnetex_handlerarg_t)(a3), (gasnetex_handlerarg_t)(a4), (gasnetex_handlerarg_t)(a5), (gasnetex_handlerarg_t)(a6), (gasnetex_handlerarg_t)(a7), (gasnetex_handlerarg_t)(a8), (gasnetex_handlerarg_t)(a9), (gasnetex_handlerarg_t)(a10), (gasnetex_handlerarg_t)(a11), (gasnetex_handlerarg_t)(a12), (gasnetex_handlerarg_t)(a13), (gasnetex_handlerarg_t)(a14))
#define gasnetex_AMRequestShort16(team, rank, handler, flags, a0, a1, a2, a3, a4, a5, a6, a7, a8, a9, a10, a11, a12, a13, a14, a15) \
        gasnetc_AMRequestShortM(team, rank, handler, flags GASNETI_THREAD_GET, 16, (gasnetex_handlerarg_t)(a0), (gasnetex_handlerarg_t)(a1), (gasnetex_handlerarg_t)(a2), (gasnetex_handlerarg_t)(a3), (gasnetex_handlerarg_t)(a4), (gasnetex_handlerarg_t)(a5), (gasnetex_handlerarg_t)(a6), (gasnetex_handlerarg_t)(a7), (gasnetex_handlerarg_t)(a8), (gasnetex_handlerarg_t)(a9), (gasnetex_handlerarg_t)(a10), (gasnetex_handlerarg_t)(a11), (gasnetex_handlerarg_t)(a12), (gasnetex_handlerarg_t)(a13), (gasnetex_handlerarg_t)(a14), (gasnetex_handlerarg_t)(a15))
/* ------------------------------------------------------------------------------------ */
#define gasnetex_AMRequestMedium0(team, rank, handler, source_addr, nbytes, lc_opt, flags) \
       gasnetc_AMRequestMediumM(team, rank, handler, source_addr, nbytes, lc_opt, flags GASNETI_THREAD_GET, 0)
#define gasnetex_AMRequestMedium1(team, rank, handler, source_addr, nbytes, lc_opt, flags, a0) \
       gasnetc_AMRequestMediumM(team, rank, handler, source_addr, nbytes, lc_opt, flags GASNETI_THREAD_GET, 1, (gasnetex_handlerarg_t)(a0))
#define gasnetex_AMRequestMedium2(team, rank, handler, source_addr, nbytes, lc_opt, flags, a0, a1) \
       gasnetc_AMRequestMediumM(team, rank, handler, source_addr, nbytes, lc_opt, flags GASNETI_THREAD_GET, 2, (gasnetex_handlerarg_t)(a0), (gasnetex_handlerarg_t)(a1))
#define gasnetex_AMRequestMedium3(team, rank, handler, source_addr, nbytes, lc_opt, flags, a0, a1, a2) \
       gasnetc_AMRequestMediumM(team, rank, handler, source_addr, nbytes, lc_opt, flags GASNETI_THREAD_GET, 3, (gasnetex_handlerarg_t)(a0), (gasnetex_handlerarg_t)(a1), (gasnetex_handlerarg_t)(a2))
#define gasnetex_AMRequestMedium4(team, rank, handler, source_addr, nbytes, lc_opt, flags, a0, a1, a2, a3) \
       gasnetc_AMRequestMediumM(team, rank, handler, source_addr, nbytes, lc_opt, flags GASNETI_THREAD_GET, 4, (gasnetex_handlerarg_t)(a0), (gasnetex_handlerarg_t)(a1), (gasnetex_handlerarg_t)(a2), (gasnetex_handlerarg_t)(a3))

#define gasnetex_AMRequestMedium5(team, rank, handler, source_addr, nbytes, lc_opt, flags, a0, a1, a2, a3, a4) \
       gasnetc_AMRequestMediumM(team, rank, handler, source_addr, nbytes, lc_opt, flags GASNETI_THREAD_GET, 5, (gasnetex_handlerarg_t)(a0), (gasnetex_handlerarg_t)(a1), (gasnetex_handlerarg_t)(a2), (gasnetex_handlerarg_t)(a3), (gasnetex_handlerarg_t)(a4))
#define gasnetex_AMRequestMedium6(team, rank, handler, source_addr, nbytes, lc_opt, flags, a0, a1, a2, a3, a4, a5) \
       gasnetc_AMRequestMediumM(team, rank, handler, source_addr, nbytes, lc_opt, flags GASNETI_THREAD_GET, 6, (gasnetex_handlerarg_t)(a0), (gasnetex_handlerarg_t)(a1), (gasnetex_handlerarg_t)(a2), (gasnetex_handlerarg_t)(a3), (gasnetex_handlerarg_t)(a4), (gasnetex_handlerarg_t)(a5))
#define gasnetex_AMRequestMedium7(team, rank, handler, source_addr, nbytes, lc_opt, flags, a0, a1, a2, a3, a4, a5, a6) \
       gasnetc_AMRequestMediumM(team, rank, handler, source_addr, nbytes, lc_opt, flags GASNETI_THREAD_GET, 7, (gasnetex_handlerarg_t)(a0), (gasnetex_handlerarg_t)(a1), (gasnetex_handlerarg_t)(a2), (gasnetex_handlerarg_t)(a3), (gasnetex_handlerarg_t)(a4), (gasnetex_handlerarg_t)(a5), (gasnetex_handlerarg_t)(a6))
#define gasnetex_AMRequestMedium8(team, rank, handler, source_addr, nbytes, lc_opt, flags, a0, a1, a2, a3, a4, a5, a6, a7) \
       gasnetc_AMRequestMediumM(team, rank, handler, source_addr, nbytes, lc_opt, flags GASNETI_THREAD_GET, 8, (gasnetex_handlerarg_t)(a0), (gasnetex_handlerarg_t)(a1), (gasnetex_handlerarg_t)(a2), (gasnetex_handlerarg_t)(a3), (gasnetex_handlerarg_t)(a4), (gasnetex_handlerarg_t)(a5), (gasnetex_handlerarg_t)(a6), (gasnetex_handlerarg_t)(a7))

#define gasnetex_AMRequestMedium9(team, rank, handler, source_addr, nbytes, lc_opt, flags, a0, a1, a2, a3, a4, a5, a6, a7, a8 ) \
        gasnetc_AMRequestMediumM(team, rank, handler, source_addr, nbytes, lc_opt, flags GASNETI_THREAD_GET, 9, (gasnetex_handlerarg_t)(a0), (gasnetex_handlerarg_t)(a1), (gasnetex_handlerarg_t)(a2), (gasnetex_handlerarg_t)(a3), (gasnetex_handlerarg_t)(a4), (gasnetex_handlerarg_t)(a5), (gasnetex_handlerarg_t)(a6), (gasnetex_handlerarg_t)(a7), (gasnetex_handlerarg_t)(a8))
#define gasnetex_AMRequestMedium10(team, rank, handler, source_addr, nbytes, lc_opt, flags, a0, a1, a2, a3, a4, a5, a6, a7, a8, a9) \
        gasnetc_AMRequestMediumM(team, rank, handler, source_addr, nbytes, lc_opt, flags GASNETI_THREAD_GET, 10, (gasnetex_handlerarg_t)(a0), (gasnetex_handlerarg_t)(a1), (gasnetex_handlerarg_t)(a2), (gasnetex_handlerarg_t)(a3), (gasnetex_handlerarg_t)(a4), (gasnetex_handlerarg_t)(a5), (gasnetex_handlerarg_t)(a6), (gasnetex_handlerarg_t)(a7), (gasnetex_handlerarg_t)(a8), (gasnetex_handlerarg_t)(a9))
#define gasnetex_AMRequestMedium11(team, rank, handler, source_addr, nbytes, lc_opt, flags, a0, a1, a2, a3, a4, a5, a6, a7, a8, a9, a10) \
        gasnetc_AMRequestMediumM(team, rank, handler, source_addr, nbytes, lc_opt, flags GASNETI_THREAD_GET, 11, (gasnetex_handlerarg_t)(a0), (gasnetex_handlerarg_t)(a1), (gasnetex_handlerarg_t)(a2), (gasnetex_handlerarg_t)(a3), (gasnetex_handlerarg_t)(a4), (gasnetex_handlerarg_t)(a5), (gasnetex_handlerarg_t)(a6), (gasnetex_handlerarg_t)(a7), (gasnetex_handlerarg_t)(a8), (gasnetex_handlerarg_t)(a9), (gasnetex_handlerarg_t)(a10))
#define gasnetex_AMRequestMedium12(team, rank, handler, source_addr, nbytes, lc_opt, flags, a0, a1, a2, a3, a4, a5, a6, a7, a8, a9, a10, a11) \
        gasnetc_AMRequestMediumM(team, rank, handler, source_addr, nbytes, lc_opt, flags GASNETI_THREAD_GET, 12, (gasnetex_handlerarg_t)(a0), (gasnetex_handlerarg_t)(a1), (gasnetex_handlerarg_t)(a2), (gasnetex_handlerarg_t)(a3), (gasnetex_handlerarg_t)(a4), (gasnetex_handlerarg_t)(a5), (gasnetex_handlerarg_t)(a6), (gasnetex_handlerarg_t)(a7), (gasnetex_handlerarg_t)(a8), (gasnetex_handlerarg_t)(a9), (gasnetex_handlerarg_t)(a10), (gasnetex_handlerarg_t)(a11))

#define gasnetex_AMRequestMedium13(team, rank, handler, source_addr, nbytes, lc_opt, flags, a0, a1, a2, a3, a4, a5, a6, a7, a8, a9, a10, a11, a12) \
        gasnetc_AMRequestMediumM(team, rank, handler, source_addr, nbytes, lc_opt, flags GASNETI_THREAD_GET, 13, (gasnetex_handlerarg_t)(a0), (gasnetex_handlerarg_t)(a1), (gasnetex_handlerarg_t)(a2), (gasnetex_handlerarg_t)(a3), (gasnetex_handlerarg_t)(a4), (gasnetex_handlerarg_t)(a5), (gasnetex_handlerarg_t)(a6), (gasnetex_handlerarg_t)(a7), (gasnetex_handlerarg_t)(a8), (gasnetex_handlerarg_t)(a9), (gasnetex_handlerarg_t)(a10), (gasnetex_handlerarg_t)(a11), (gasnetex_handlerarg_t)(a12))
#define gasnetex_AMRequestMedium14(team, rank, handler, source_addr, nbytes, lc_opt, flags, a0, a1, a2, a3, a4, a5, a6, a7, a8, a9, a10, a11, a12, a13) \
        gasnetc_AMRequestMediumM(team, rank, handler, source_addr, nbytes, lc_opt, flags GASNETI_THREAD_GET, 14, (gasnetex_handlerarg_t)(a0), (gasnetex_handlerarg_t)(a1), (gasnetex_handlerarg_t)(a2), (gasnetex_handlerarg_t)(a3), (gasnetex_handlerarg_t)(a4), (gasnetex_handlerarg_t)(a5), (gasnetex_handlerarg_t)(a6), (gasnetex_handlerarg_t)(a7), (gasnetex_handlerarg_t)(a8), (gasnetex_handlerarg_t)(a9), (gasnetex_handlerarg_t)(a10), (gasnetex_handlerarg_t)(a11), (gasnetex_handlerarg_t)(a12), (gasnetex_handlerarg_t)(a13))
#define gasnetex_AMRequestMedium15(team, rank, handler, source_addr, nbytes, lc_opt, flags, a0, a1, a2, a3, a4, a5, a6, a7, a8, a9, a10, a11, a12, a13, a14) \
        gasnetc_AMRequestMediumM(team, rank, handler, source_addr, nbytes, lc_opt, flags GASNETI_THREAD_GET, 15, (gasnetex_handlerarg_t)(a0), (gasnetex_handlerarg_t)(a1), (gasnetex_handlerarg_t)(a2), (gasnetex_handlerarg_t)(a3), (gasnetex_handlerarg_t)(a4), (gasnetex_handlerarg_t)(a5), (gasnetex_handlerarg_t)(a6), (gasnetex_handlerarg_t)(a7), (gasnetex_handlerarg_t)(a8), (gasnetex_handlerarg_t)(a9), (gasnetex_handlerarg_t)(a10), (gasnetex_handlerarg_t)(a11), (gasnetex_handlerarg_t)(a12), (gasnetex_handlerarg_t)(a13), (gasnetex_handlerarg_t)(a14))
#define gasnetex_AMRequestMedium16(team, rank, handler, source_addr, nbytes, lc_opt, flags, a0, a1, a2, a3, a4, a5, a6, a7, a8, a9, a10, a11, a12, a13, a14, a15) \
        gasnetc_AMRequestMediumM(team, rank, handler, source_addr, nbytes, lc_opt, flags GASNETI_THREAD_GET, 16, (gasnetex_handlerarg_t)(a0), (gasnetex_handlerarg_t)(a1), (gasnetex_handlerarg_t)(a2), (gasnetex_handlerarg_t)(a3), (gasnetex_handlerarg_t)(a4), (gasnetex_handlerarg_t)(a5), (gasnetex_handlerarg_t)(a6), (gasnetex_handlerarg_t)(a7), (gasnetex_handlerarg_t)(a8), (gasnetex_handlerarg_t)(a9), (gasnetex_handlerarg_t)(a10), (gasnetex_handlerarg_t)(a11), (gasnetex_handlerarg_t)(a12), (gasnetex_handlerarg_t)(a13), (gasnetex_handlerarg_t)(a14), (gasnetex_handlerarg_t)(a15))
/* ------------------------------------------------------------------------------------ */
#define gasnetex_AMRequestLong0(team, rank, handler, source_addr, nbytes, dest_addr, lc_opt, flags) \
       gasnetc_AMRequestLongM(team, rank, handler, source_addr, nbytes, dest_addr, lc_opt, flags GASNETI_THREAD_GET, 0)
#define gasnetex_AMRequestLong1(team, rank, handler, source_addr, nbytes, dest_addr, lc_opt, flags, a0) \
       gasnetc_AMRequestLongM(team, rank, handler, source_addr, nbytes, dest_addr, lc_opt, flags GASNETI_THREAD_GET, 1, (gasnetex_handlerarg_t)(a0))
#define gasnetex_AMRequestLong2(team, rank, handler, source_addr, nbytes, dest_addr, lc_opt, flags, a0, a1) \
       gasnetc_AMRequestLongM(team, rank, handler, source_addr, nbytes, dest_addr, lc_opt, flags GASNETI_THREAD_GET, 2, (gasnetex_handlerarg_t)(a0), (gasnetex_handlerarg_t)(a1))
#define gasnetex_AMRequestLong3(team, rank, handler, source_addr, nbytes, dest_addr, lc_opt, flags, a0, a1, a2) \
       gasnetc_AMRequestLongM(team, rank, handler, source_addr, nbytes, dest_addr, lc_opt, flags GASNETI_THREAD_GET, 3, (gasnetex_handlerarg_t)(a0), (gasnetex_handlerarg_t)(a1), (gasnetex_handlerarg_t)(a2))
#define gasnetex_AMRequestLong4(team, rank, handler, source_addr, nbytes, dest_addr, lc_opt, flags, a0, a1, a2, a3) \
       gasnetc_AMRequestLongM(team, rank, handler, source_addr, nbytes, dest_addr, lc_opt, flags GASNETI_THREAD_GET, 4, (gasnetex_handlerarg_t)(a0), (gasnetex_handlerarg_t)(a1), (gasnetex_handlerarg_t)(a2), (gasnetex_handlerarg_t)(a3))

#define gasnetex_AMRequestLong5(team, rank, handler, source_addr, nbytes, dest_addr, lc_opt, flags, a0, a1, a2, a3, a4) \
       gasnetc_AMRequestLongM(team, rank, handler, source_addr, nbytes, dest_addr, lc_opt, flags GASNETI_THREAD_GET, 5, (gasnetex_handlerarg_t)(a0), (gasnetex_handlerarg_t)(a1), (gasnetex_handlerarg_t)(a2), (gasnetex_handlerarg_t)(a3), (gasnetex_handlerarg_t)(a4))
#define gasnetex_AMRequestLong6(team, rank, handler, source_addr, nbytes, dest_addr, lc_opt, flags, a0, a1, a2, a3, a4, a5) \
       gasnetc_AMRequestLongM(team, rank, handler, source_addr, nbytes, dest_addr, lc_opt, flags GASNETI_THREAD_GET, 6, (gasnetex_handlerarg_t)(a0), (gasnetex_handlerarg_t)(a1), (gasnetex_handlerarg_t)(a2), (gasnetex_handlerarg_t)(a3), (gasnetex_handlerarg_t)(a4), (gasnetex_handlerarg_t)(a5))
#define gasnetex_AMRequestLong7(team, rank, handler, source_addr, nbytes, dest_addr, lc_opt, flags, a0, a1, a2, a3, a4, a5, a6) \
       gasnetc_AMRequestLongM(team, rank, handler, source_addr, nbytes, dest_addr, lc_opt, flags GASNETI_THREAD_GET, 7, (gasnetex_handlerarg_t)(a0), (gasnetex_handlerarg_t)(a1), (gasnetex_handlerarg_t)(a2), (gasnetex_handlerarg_t)(a3), (gasnetex_handlerarg_t)(a4), (gasnetex_handlerarg_t)(a5), (gasnetex_handlerarg_t)(a6))
#define gasnetex_AMRequestLong8(team, rank, handler, source_addr, nbytes, dest_addr, lc_opt, flags, a0, a1, a2, a3, a4, a5, a6, a7) \
       gasnetc_AMRequestLongM(team, rank, handler, source_addr, nbytes, dest_addr, lc_opt, flags GASNETI_THREAD_GET, 8, (gasnetex_handlerarg_t)(a0), (gasnetex_handlerarg_t)(a1), (gasnetex_handlerarg_t)(a2), (gasnetex_handlerarg_t)(a3), (gasnetex_handlerarg_t)(a4), (gasnetex_handlerarg_t)(a5), (gasnetex_handlerarg_t)(a6), (gasnetex_handlerarg_t)(a7))

#define gasnetex_AMRequestLong9(team, rank, handler, source_addr, nbytes, dest_addr, lc_opt, flags, a0, a1, a2, a3, a4, a5, a6, a7, a8 ) \
        gasnetc_AMRequestLongM(team, rank, handler, source_addr, nbytes, dest_addr, lc_opt, flags GASNETI_THREAD_GET, 9, (gasnetex_handlerarg_t)(a0), (gasnetex_handlerarg_t)(a1), (gasnetex_handlerarg_t)(a2), (gasnetex_handlerarg_t)(a3), (gasnetex_handlerarg_t)(a4), (gasnetex_handlerarg_t)(a5), (gasnetex_handlerarg_t)(a6), (gasnetex_handlerarg_t)(a7), (gasnetex_handlerarg_t)(a8))
#define gasnetex_AMRequestLong10(team, rank, handler, source_addr, nbytes, dest_addr, lc_opt, flags, a0, a1, a2, a3, a4, a5, a6, a7, a8, a9) \
        gasnetc_AMRequestLongM(team, rank, handler, source_addr, nbytes, dest_addr, lc_opt, flags GASNETI_THREAD_GET, 10, (gasnetex_handlerarg_t)(a0), (gasnetex_handlerarg_t)(a1), (gasnetex_handlerarg_t)(a2), (gasnetex_handlerarg_t)(a3), (gasnetex_handlerarg_t)(a4), (gasnetex_handlerarg_t)(a5), (gasnetex_handlerarg_t)(a6), (gasnetex_handlerarg_t)(a7), (gasnetex_handlerarg_t)(a8), (gasnetex_handlerarg_t)(a9))
#define gasnetex_AMRequestLong11(team, rank, handler, source_addr, nbytes, dest_addr, lc_opt, flags, a0, a1, a2, a3, a4, a5, a6, a7, a8, a9, a10) \
        gasnetc_AMRequestLongM(team, rank, handler, source_addr, nbytes, dest_addr, lc_opt, flags GASNETI_THREAD_GET, 11, (gasnetex_handlerarg_t)(a0), (gasnetex_handlerarg_t)(a1), (gasnetex_handlerarg_t)(a2), (gasnetex_handlerarg_t)(a3), (gasnetex_handlerarg_t)(a4), (gasnetex_handlerarg_t)(a5), (gasnetex_handlerarg_t)(a6), (gasnetex_handlerarg_t)(a7), (gasnetex_handlerarg_t)(a8), (gasnetex_handlerarg_t)(a9), (gasnetex_handlerarg_t)(a10))
#define gasnetex_AMRequestLong12(team, rank, handler, source_addr, nbytes, dest_addr, lc_opt, flags, a0, a1, a2, a3, a4, a5, a6, a7, a8, a9, a10, a11) \
        gasnetc_AMRequestLongM(team, rank, handler, source_addr, nbytes, dest_addr, lc_opt, flags GASNETI_THREAD_GET, 12, (gasnetex_handlerarg_t)(a0), (gasnetex_handlerarg_t)(a1), (gasnetex_handlerarg_t)(a2), (gasnetex_handlerarg_t)(a3), (gasnetex_handlerarg_t)(a4), (gasnetex_handlerarg_t)(a5), (gasnetex_handlerarg_t)(a6), (gasnetex_handlerarg_t)(a7), (gasnetex_handlerarg_t)(a8), (gasnetex_handlerarg_t)(a9), (gasnetex_handlerarg_t)(a10), (gasnetex_handlerarg_t)(a11))

#define gasnetex_AMRequestLong13(team, rank, handler, source_addr, nbytes, dest_addr, lc_opt, flags, a0, a1, a2, a3, a4, a5, a6, a7, a8, a9, a10, a11, a12) \
        gasnetc_AMRequestLongM(team, rank, handler, source_addr, nbytes, dest_addr, lc_opt, flags GASNETI_THREAD_GET, 13, (gasnetex_handlerarg_t)(a0), (gasnetex_handlerarg_t)(a1), (gasnetex_handlerarg_t)(a2), (gasnetex_handlerarg_t)(a3), (gasnetex_handlerarg_t)(a4), (gasnetex_handlerarg_t)(a5), (gasnetex_handlerarg_t)(a6), (gasnetex_handlerarg_t)(a7), (gasnetex_handlerarg_t)(a8), (gasnetex_handlerarg_t)(a9), (gasnetex_handlerarg_t)(a10), (gasnetex_handlerarg_t)(a11), (gasnetex_handlerarg_t)(a12))
#define gasnetex_AMRequestLong14(team, rank, handler, source_addr, nbytes, dest_addr, lc_opt, flags, a0, a1, a2, a3, a4, a5, a6, a7, a8, a9, a10, a11, a12, a13) \
        gasnetc_AMRequestLongM(team, rank, handler, source_addr, nbytes, dest_addr, lc_opt, flags GASNETI_THREAD_GET, 14, (gasnetex_handlerarg_t)(a0), (gasnetex_handlerarg_t)(a1), (gasnetex_handlerarg_t)(a2), (gasnetex_handlerarg_t)(a3), (gasnetex_handlerarg_t)(a4), (gasnetex_handlerarg_t)(a5), (gasnetex_handlerarg_t)(a6), (gasnetex_handlerarg_t)(a7), (gasnetex_handlerarg_t)(a8), (gasnetex_handlerarg_t)(a9), (gasnetex_handlerarg_t)(a10), (gasnetex_handlerarg_t)(a11), (gasnetex_handlerarg_t)(a12), (gasnetex_handlerarg_t)(a13))
#define gasnetex_AMRequestLong15(team, rank, handler, source_addr, nbytes, dest_addr, lc_opt, flags, a0, a1, a2, a3, a4, a5, a6, a7, a8, a9, a10, a11, a12, a13, a14) \
        gasnetc_AMRequestLongM(team, rank, handler, source_addr, nbytes, dest_addr, lc_opt, flags GASNETI_THREAD_GET, 15, (gasnetex_handlerarg_t)(a0), (gasnetex_handlerarg_t)(a1), (gasnetex_handlerarg_t)(a2), (gasnetex_handlerarg_t)(a3), (gasnetex_handlerarg_t)(a4), (gasnetex_handlerarg_t)(a5), (gasnetex_handlerarg_t)(a6), (gasnetex_handlerarg_t)(a7), (gasnetex_handlerarg_t)(a8), (gasnetex_handlerarg_t)(a9), (gasnetex_handlerarg_t)(a10), (gasnetex_handlerarg_t)(a11), (gasnetex_handlerarg_t)(a12), (gasnetex_handlerarg_t)(a13), (gasnetex_handlerarg_t)(a14))
#define gasnetex_AMRequestLong16(team, rank, handler, source_addr, nbytes, dest_addr, lc_opt, flags, a0, a1, a2, a3, a4, a5, a6, a7, a8, a9, a10, a11, a12, a13, a14, a15) \
        gasnetc_AMRequestLongM(team, rank, handler, source_addr, nbytes, dest_addr, lc_opt, flags GASNETI_THREAD_GET, 16, (gasnetex_handlerarg_t)(a0), (gasnetex_handlerarg_t)(a1), (gasnetex_handlerarg_t)(a2), (gasnetex_handlerarg_t)(a3), (gasnetex_handlerarg_t)(a4), (gasnetex_handlerarg_t)(a5), (gasnetex_handlerarg_t)(a6), (gasnetex_handlerarg_t)(a7), (gasnetex_handlerarg_t)(a8), (gasnetex_handlerarg_t)(a9), (gasnetex_handlerarg_t)(a10), (gasnetex_handlerarg_t)(a11), (gasnetex_handlerarg_t)(a12), (gasnetex_handlerarg_t)(a13), (gasnetex_handlerarg_t)(a14), (gasnetex_handlerarg_t)(a15))
/* ------------------------------------------------------------------------------------ */
#define gasnetex_AMReplyShort0(token, handler, flags) \
       gasnetc_AMReplyShortM(token, handler, flags, 0)
#define gasnetex_AMReplyShort1(token, handler, flags, a0) \
       gasnetc_AMReplyShortM(token, handler, flags, 1, (gasnetex_handlerarg_t)(a0))
#define gasnetex_AMReplyShort2(token, handler, flags, a0, a1) \
       gasnetc_AMReplyShortM(token, handler, flags, 2, (gasnetex_handlerarg_t)(a0), (gasnetex_handlerarg_t)(a1))
#define gasnetex_AMReplyShort3(token, handler, flags, a0, a1, a2) \
       gasnetc_AMReplyShortM(token, handler, flags, 3, (gasnetex_handlerarg_t)(a0), (gasnetex_handlerarg_t)(a1), (gasnetex_handlerarg_t)(a2))
#define gasnetex_AMReplyShort4(token, handler, flags, a0, a1, a2, a3) \
       gasnetc_AMReplyShortM(token, handler, flags, 4, (gasnetex_handlerarg_t)(a0), (gasnetex_handlerarg_t)(a1), (gasnetex_handlerarg_t)(a2), (gasnetex_handlerarg_t)(a3))

#define gasnetex_AMReplyShort5(token, handler, flags, a0, a1, a2, a3, a4) \
       gasnetc_AMReplyShortM(token, handler, flags, 5, (gasnetex_handlerarg_t)(a0), (gasnetex_handlerarg_t)(a1), (gasnetex_handlerarg_t)(a2), (gasnetex_handlerarg_t)(a3), (gasnetex_handlerarg_t)(a4))
#define gasnetex_AMReplyShort6(token, handler, flags, a0, a1, a2, a3, a4, a5) \
       gasnetc_AMReplyShortM(token, handler, flags, 6, (gasnetex_handlerarg_t)(a0), (gasnetex_handlerarg_t)(a1), (gasnetex_handlerarg_t)(a2), (gasnetex_handlerarg_t)(a3), (gasnetex_handlerarg_t)(a4), (gasnetex_handlerarg_t)(a5))
#define gasnetex_AMReplyShort7(token, handler, flags, a0, a1, a2, a3, a4, a5, a6) \
       gasnetc_AMReplyShortM(token, handler, flags, 7, (gasnetex_handlerarg_t)(a0), (gasnetex_handlerarg_t)(a1), (gasnetex_handlerarg_t)(a2), (gasnetex_handlerarg_t)(a3), (gasnetex_handlerarg_t)(a4), (gasnetex_handlerarg_t)(a5), (gasnetex_handlerarg_t)(a6))
#define gasnetex_AMReplyShort8(token, handler, flags, a0, a1, a2, a3, a4, a5, a6, a7) \
       gasnetc_AMReplyShortM(token, handler, flags, 8, (gasnetex_handlerarg_t)(a0), (gasnetex_handlerarg_t)(a1), (gasnetex_handlerarg_t)(a2), (gasnetex_handlerarg_t)(a3), (gasnetex_handlerarg_t)(a4), (gasnetex_handlerarg_t)(a5), (gasnetex_handlerarg_t)(a6), (gasnetex_handlerarg_t)(a7))

#define gasnetex_AMReplyShort9(token, handler, flags, a0, a1, a2, a3, a4, a5, a6, a7, a8 ) \
        gasnetc_AMReplyShortM(token, handler, flags, 9, (gasnetex_handlerarg_t)(a0), (gasnetex_handlerarg_t)(a1), (gasnetex_handlerarg_t)(a2), (gasnetex_handlerarg_t)(a3), (gasnetex_handlerarg_t)(a4), (gasnetex_handlerarg_t)(a5), (gasnetex_handlerarg_t)(a6), (gasnetex_handlerarg_t)(a7), (gasnetex_handlerarg_t)(a8))
#define gasnetex_AMReplyShort10(token, handler, flags, a0, a1, a2, a3, a4, a5, a6, a7, a8, a9) \
        gasnetc_AMReplyShortM(token, handler, flags, 10, (gasnetex_handlerarg_t)(a0), (gasnetex_handlerarg_t)(a1), (gasnetex_handlerarg_t)(a2), (gasnetex_handlerarg_t)(a3), (gasnetex_handlerarg_t)(a4), (gasnetex_handlerarg_t)(a5), (gasnetex_handlerarg_t)(a6), (gasnetex_handlerarg_t)(a7), (gasnetex_handlerarg_t)(a8), (gasnetex_handlerarg_t)(a9))
#define gasnetex_AMReplyShort11(token, handler, flags, a0, a1, a2, a3, a4, a5, a6, a7, a8, a9, a10) \
        gasnetc_AMReplyShortM(token, handler, flags, 11, (gasnetex_handlerarg_t)(a0), (gasnetex_handlerarg_t)(a1), (gasnetex_handlerarg_t)(a2), (gasnetex_handlerarg_t)(a3), (gasnetex_handlerarg_t)(a4), (gasnetex_handlerarg_t)(a5), (gasnetex_handlerarg_t)(a6), (gasnetex_handlerarg_t)(a7), (gasnetex_handlerarg_t)(a8), (gasnetex_handlerarg_t)(a9), (gasnetex_handlerarg_t)(a10))
#define gasnetex_AMReplyShort12(token, handler, flags, a0, a1, a2, a3, a4, a5, a6, a7, a8, a9, a10, a11) \
        gasnetc_AMReplyShortM(token, handler, flags, 12, (gasnetex_handlerarg_t)(a0), (gasnetex_handlerarg_t)(a1), (gasnetex_handlerarg_t)(a2), (gasnetex_handlerarg_t)(a3), (gasnetex_handlerarg_t)(a4), (gasnetex_handlerarg_t)(a5), (gasnetex_handlerarg_t)(a6), (gasnetex_handlerarg_t)(a7), (gasnetex_handlerarg_t)(a8), (gasnetex_handlerarg_t)(a9), (gasnetex_handlerarg_t)(a10), (gasnetex_handlerarg_t)(a11))

#define gasnetex_AMReplyShort13(token, handler, flags, a0, a1, a2, a3, a4, a5, a6, a7, a8, a9, a10, a11, a12) \
        gasnetc_AMReplyShortM(token, handler, flags, 13, (gasnetex_handlerarg_t)(a0), (gasnetex_handlerarg_t)(a1), (gasnetex_handlerarg_t)(a2), (gasnetex_handlerarg_t)(a3), (gasnetex_handlerarg_t)(a4), (gasnetex_handlerarg_t)(a5), (gasnetex_handlerarg_t)(a6), (gasnetex_handlerarg_t)(a7), (gasnetex_handlerarg_t)(a8), (gasnetex_handlerarg_t)(a9), (gasnetex_handlerarg_t)(a10), (gasnetex_handlerarg_t)(a11), (gasnetex_handlerarg_t)(a12))
#define gasnetex_AMReplyShort14(token, handler, flags, a0, a1, a2, a3, a4, a5, a6, a7, a8, a9, a10, a11, a12, a13) \
        gasnetc_AMReplyShortM(token, handler, flags, 14, (gasnetex_handlerarg_t)(a0), (gasnetex_handlerarg_t)(a1), (gasnetex_handlerarg_t)(a2), (gasnetex_handlerarg_t)(a3), (gasnetex_handlerarg_t)(a4), (gasnetex_handlerarg_t)(a5), (gasnetex_handlerarg_t)(a6), (gasnetex_handlerarg_t)(a7), (gasnetex_handlerarg_t)(a8), (gasnetex_handlerarg_t)(a9), (gasnetex_handlerarg_t)(a10), (gasnetex_handlerarg_t)(a11), (gasnetex_handlerarg_t)(a12), (gasnetex_handlerarg_t)(a13))
#define gasnetex_AMReplyShort15(token, handler, flags, a0, a1, a2, a3, a4, a5, a6, a7, a8, a9, a10, a11, a12, a13, a14) \
        gasnetc_AMReplyShortM(token, handler, flags, 15, (gasnetex_handlerarg_t)(a0), (gasnetex_handlerarg_t)(a1), (gasnetex_handlerarg_t)(a2), (gasnetex_handlerarg_t)(a3), (gasnetex_handlerarg_t)(a4), (gasnetex_handlerarg_t)(a5), (gasnetex_handlerarg_t)(a6), (gasnetex_handlerarg_t)(a7), (gasnetex_handlerarg_t)(a8), (gasnetex_handlerarg_t)(a9), (gasnetex_handlerarg_t)(a10), (gasnetex_handlerarg_t)(a11), (gasnetex_handlerarg_t)(a12), (gasnetex_handlerarg_t)(a13), (gasnetex_handlerarg_t)(a14))
#define gasnetex_AMReplyShort16(token, handler, flags, a0, a1, a2, a3, a4, a5, a6, a7, a8, a9, a10, a11, a12, a13, a14, a15) \
        gasnetc_AMReplyShortM(token, handler, flags, 16, (gasnetex_handlerarg_t)(a0), (gasnetex_handlerarg_t)(a1), (gasnetex_handlerarg_t)(a2), (gasnetex_handlerarg_t)(a3), (gasnetex_handlerarg_t)(a4), (gasnetex_handlerarg_t)(a5), (gasnetex_handlerarg_t)(a6), (gasnetex_handlerarg_t)(a7), (gasnetex_handlerarg_t)(a8), (gasnetex_handlerarg_t)(a9), (gasnetex_handlerarg_t)(a10), (gasnetex_handlerarg_t)(a11), (gasnetex_handlerarg_t)(a12), (gasnetex_handlerarg_t)(a13), (gasnetex_handlerarg_t)(a14), (gasnetex_handlerarg_t)(a15))
/* ------------------------------------------------------------------------------------ */
#define gasnetex_AMReplyMedium0(token, handler, source_addr, nbytes, lc_opt, flags) \
       gasnetc_AMReplyMediumM(token, handler, source_addr, nbytes, lc_opt, flags, 0)
#define gasnetex_AMReplyMedium1(token, handler, source_addr, nbytes, lc_opt, flags, a0) \
       gasnetc_AMReplyMediumM(token, handler, source_addr, nbytes, lc_opt, flags, 1, (gasnetex_handlerarg_t)(a0))
#define gasnetex_AMReplyMedium2(token, handler, source_addr, nbytes, lc_opt, flags, a0, a1) \
       gasnetc_AMReplyMediumM(token, handler, source_addr, nbytes, lc_opt, flags, 2, (gasnetex_handlerarg_t)(a0), (gasnetex_handlerarg_t)(a1))
#define gasnetex_AMReplyMedium3(token, handler, source_addr, nbytes, lc_opt, flags, a0, a1, a2) \
       gasnetc_AMReplyMediumM(token, handler, source_addr, nbytes, lc_opt, flags, 3, (gasnetex_handlerarg_t)(a0), (gasnetex_handlerarg_t)(a1), (gasnetex_handlerarg_t)(a2))
#define gasnetex_AMReplyMedium4(token, handler, source_addr, nbytes, lc_opt, flags, a0, a1, a2, a3) \
       gasnetc_AMReplyMediumM(token, handler, source_addr, nbytes, lc_opt, flags, 4, (gasnetex_handlerarg_t)(a0), (gasnetex_handlerarg_t)(a1), (gasnetex_handlerarg_t)(a2), (gasnetex_handlerarg_t)(a3))

#define gasnetex_AMReplyMedium5(token, handler, source_addr, nbytes, lc_opt, flags, a0, a1, a2, a3, a4) \
       gasnetc_AMReplyMediumM(token, handler, source_addr, nbytes, lc_opt, flags, 5, (gasnetex_handlerarg_t)(a0), (gasnetex_handlerarg_t)(a1), (gasnetex_handlerarg_t)(a2), (gasnetex_handlerarg_t)(a3), (gasnetex_handlerarg_t)(a4))
#define gasnetex_AMReplyMedium6(token, handler, source_addr, nbytes, lc_opt, flags, a0, a1, a2, a3, a4, a5) \
       gasnetc_AMReplyMediumM(token, handler, source_addr, nbytes, lc_opt, flags, 6, (gasnetex_handlerarg_t)(a0), (gasnetex_handlerarg_t)(a1), (gasnetex_handlerarg_t)(a2), (gasnetex_handlerarg_t)(a3), (gasnetex_handlerarg_t)(a4), (gasnetex_handlerarg_t)(a5))
#define gasnetex_AMReplyMedium7(token, handler, source_addr, nbytes, lc_opt, flags, a0, a1, a2, a3, a4, a5, a6) \
       gasnetc_AMReplyMediumM(token, handler, source_addr, nbytes, lc_opt, flags, 7, (gasnetex_handlerarg_t)(a0), (gasnetex_handlerarg_t)(a1), (gasnetex_handlerarg_t)(a2), (gasnetex_handlerarg_t)(a3), (gasnetex_handlerarg_t)(a4), (gasnetex_handlerarg_t)(a5), (gasnetex_handlerarg_t)(a6))
#define gasnetex_AMReplyMedium8(token, handler, source_addr, nbytes, lc_opt, flags, a0, a1, a2, a3, a4, a5, a6, a7) \
       gasnetc_AMReplyMediumM(token, handler, source_addr, nbytes, lc_opt, flags, 8, (gasnetex_handlerarg_t)(a0), (gasnetex_handlerarg_t)(a1), (gasnetex_handlerarg_t)(a2), (gasnetex_handlerarg_t)(a3), (gasnetex_handlerarg_t)(a4), (gasnetex_handlerarg_t)(a5), (gasnetex_handlerarg_t)(a6), (gasnetex_handlerarg_t)(a7))

#define gasnetex_AMReplyMedium9(token, handler, source_addr, nbytes, lc_opt, flags, a0, a1, a2, a3, a4, a5, a6, a7, a8 ) \
        gasnetc_AMReplyMediumM(token, handler, source_addr, nbytes, lc_opt, flags,  9, (gasnetex_handlerarg_t)(a0), (gasnetex_handlerarg_t)(a1), (gasnetex_handlerarg_t)(a2), (gasnetex_handlerarg_t)(a3), (gasnetex_handlerarg_t)(a4), (gasnetex_handlerarg_t)(a5), (gasnetex_handlerarg_t)(a6), (gasnetex_handlerarg_t)(a7), (gasnetex_handlerarg_t)(a8))
#define gasnetex_AMReplyMedium10(token, handler, source_addr, nbytes, lc_opt, flags, a0, a1, a2, a3, a4, a5, a6, a7, a8, a9) \
        gasnetc_AMReplyMediumM(token, handler, source_addr, nbytes, lc_opt, flags, 10, (gasnetex_handlerarg_t)(a0), (gasnetex_handlerarg_t)(a1), (gasnetex_handlerarg_t)(a2), (gasnetex_handlerarg_t)(a3), (gasnetex_handlerarg_t)(a4), (gasnetex_handlerarg_t)(a5), (gasnetex_handlerarg_t)(a6), (gasnetex_handlerarg_t)(a7), (gasnetex_handlerarg_t)(a8), (gasnetex_handlerarg_t)(a9))
#define gasnetex_AMReplyMedium11(token, handler, source_addr, nbytes, lc_opt, flags, a0, a1, a2, a3, a4, a5, a6, a7, a8, a9, a10) \
        gasnetc_AMReplyMediumM(token, handler, source_addr, nbytes, lc_opt, flags, 11, (gasnetex_handlerarg_t)(a0), (gasnetex_handlerarg_t)(a1), (gasnetex_handlerarg_t)(a2), (gasnetex_handlerarg_t)(a3), (gasnetex_handlerarg_t)(a4), (gasnetex_handlerarg_t)(a5), (gasnetex_handlerarg_t)(a6), (gasnetex_handlerarg_t)(a7), (gasnetex_handlerarg_t)(a8), (gasnetex_handlerarg_t)(a9), (gasnetex_handlerarg_t)(a10))
#define gasnetex_AMReplyMedium12(token, handler, source_addr, nbytes, lc_opt, flags, a0, a1, a2, a3, a4, a5, a6, a7, a8, a9, a10, a11) \
        gasnetc_AMReplyMediumM(token, handler, source_addr, nbytes, lc_opt, flags, 12, (gasnetex_handlerarg_t)(a0), (gasnetex_handlerarg_t)(a1), (gasnetex_handlerarg_t)(a2), (gasnetex_handlerarg_t)(a3), (gasnetex_handlerarg_t)(a4), (gasnetex_handlerarg_t)(a5), (gasnetex_handlerarg_t)(a6), (gasnetex_handlerarg_t)(a7), (gasnetex_handlerarg_t)(a8), (gasnetex_handlerarg_t)(a9), (gasnetex_handlerarg_t)(a10), (gasnetex_handlerarg_t)(a11))

#define gasnetex_AMReplyMedium13(token, handler, source_addr, nbytes, lc_opt, flags, a0, a1, a2, a3, a4, a5, a6, a7, a8, a9, a10, a11, a12) \
        gasnetc_AMReplyMediumM(token, handler, source_addr, nbytes, lc_opt, flags, 13, (gasnetex_handlerarg_t)(a0), (gasnetex_handlerarg_t)(a1), (gasnetex_handlerarg_t)(a2), (gasnetex_handlerarg_t)(a3), (gasnetex_handlerarg_t)(a4), (gasnetex_handlerarg_t)(a5), (gasnetex_handlerarg_t)(a6), (gasnetex_handlerarg_t)(a7), (gasnetex_handlerarg_t)(a8), (gasnetex_handlerarg_t)(a9), (gasnetex_handlerarg_t)(a10), (gasnetex_handlerarg_t)(a11), (gasnetex_handlerarg_t)(a12))
#define gasnetex_AMReplyMedium14(token, handler, source_addr, nbytes, lc_opt, flags, a0, a1, a2, a3, a4, a5, a6, a7, a8, a9, a10, a11, a12, a13) \
        gasnetc_AMReplyMediumM(token, handler, source_addr, nbytes, lc_opt, flags, 14, (gasnetex_handlerarg_t)(a0), (gasnetex_handlerarg_t)(a1), (gasnetex_handlerarg_t)(a2), (gasnetex_handlerarg_t)(a3), (gasnetex_handlerarg_t)(a4), (gasnetex_handlerarg_t)(a5), (gasnetex_handlerarg_t)(a6), (gasnetex_handlerarg_t)(a7), (gasnetex_handlerarg_t)(a8), (gasnetex_handlerarg_t)(a9), (gasnetex_handlerarg_t)(a10), (gasnetex_handlerarg_t)(a11), (gasnetex_handlerarg_t)(a12), (gasnetex_handlerarg_t)(a13))
#define gasnetex_AMReplyMedium15(token, handler, source_addr, nbytes, lc_opt, flags, a0, a1, a2, a3, a4, a5, a6, a7, a8, a9, a10, a11, a12, a13, a14) \
        gasnetc_AMReplyMediumM(token, handler, source_addr, nbytes, lc_opt, flags, 15, (gasnetex_handlerarg_t)(a0), (gasnetex_handlerarg_t)(a1), (gasnetex_handlerarg_t)(a2), (gasnetex_handlerarg_t)(a3), (gasnetex_handlerarg_t)(a4), (gasnetex_handlerarg_t)(a5), (gasnetex_handlerarg_t)(a6), (gasnetex_handlerarg_t)(a7), (gasnetex_handlerarg_t)(a8), (gasnetex_handlerarg_t)(a9), (gasnetex_handlerarg_t)(a10), (gasnetex_handlerarg_t)(a11), (gasnetex_handlerarg_t)(a12), (gasnetex_handlerarg_t)(a13), (gasnetex_handlerarg_t)(a14))
#define gasnetex_AMReplyMedium16(token, handler, source_addr, nbytes, lc_opt, flags, a0, a1, a2, a3, a4, a5, a6, a7, a8, a9, a10, a11, a12, a13, a14, a15) \
        gasnetc_AMReplyMediumM(token, handler, source_addr, nbytes, lc_opt, flags, 16, (gasnetex_handlerarg_t)(a0), (gasnetex_handlerarg_t)(a1), (gasnetex_handlerarg_t)(a2), (gasnetex_handlerarg_t)(a3), (gasnetex_handlerarg_t)(a4), (gasnetex_handlerarg_t)(a5), (gasnetex_handlerarg_t)(a6), (gasnetex_handlerarg_t)(a7), (gasnetex_handlerarg_t)(a8), (gasnetex_handlerarg_t)(a9), (gasnetex_handlerarg_t)(a10), (gasnetex_handlerarg_t)(a11), (gasnetex_handlerarg_t)(a12), (gasnetex_handlerarg_t)(a13), (gasnetex_handlerarg_t)(a14), (gasnetex_handlerarg_t)(a15))
/* ------------------------------------------------------------------------------------ */
#define gasnetex_AMReplyLong0(token, handler, source_addr, nbytes, dest_addr, lc_opt, flags) \
       gasnetc_AMReplyLongM(token, handler, source_addr, nbytes, dest_addr, lc_opt, flags, 0)
#define gasnetex_AMReplyLong1(token, handler, source_addr, nbytes, dest_addr, lc_opt, flags, a0) \
       gasnetc_AMReplyLongM(token, handler, source_addr, nbytes, dest_addr, lc_opt, flags, 1, (gasnetex_handlerarg_t)(a0))
#define gasnetex_AMReplyLong2(token, handler, source_addr, nbytes, dest_addr, lc_opt, flags, a0, a1) \
       gasnetc_AMReplyLongM(token, handler, source_addr, nbytes, dest_addr, lc_opt, flags, 2, (gasnetex_handlerarg_t)(a0), (gasnetex_handlerarg_t)(a1))
#define gasnetex_AMReplyLong3(token, handler, source_addr, nbytes, dest_addr, lc_opt, flags, a0, a1, a2) \
       gasnetc_AMReplyLongM(token, handler, source_addr, nbytes, dest_addr, lc_opt, flags, 3, (gasnetex_handlerarg_t)(a0), (gasnetex_handlerarg_t)(a1), (gasnetex_handlerarg_t)(a2))
#define gasnetex_AMReplyLong4(token, handler, source_addr, nbytes, dest_addr, lc_opt, flags, a0, a1, a2, a3) \
       gasnetc_AMReplyLongM(token, handler, source_addr, nbytes, dest_addr, lc_opt, flags, 4, (gasnetex_handlerarg_t)(a0), (gasnetex_handlerarg_t)(a1), (gasnetex_handlerarg_t)(a2), (gasnetex_handlerarg_t)(a3))

#define gasnetex_AMReplyLong5(token, handler, source_addr, nbytes, dest_addr, lc_opt, flags, a0, a1, a2, a3, a4) \
       gasnetc_AMReplyLongM(token, handler, source_addr, nbytes, dest_addr, lc_opt, flags, 5, (gasnetex_handlerarg_t)(a0), (gasnetex_handlerarg_t)(a1), (gasnetex_handlerarg_t)(a2), (gasnetex_handlerarg_t)(a3), (gasnetex_handlerarg_t)(a4))
#define gasnetex_AMReplyLong6(token, handler, source_addr, nbytes, dest_addr, lc_opt, flags, a0, a1, a2, a3, a4, a5) \
       gasnetc_AMReplyLongM(token, handler, source_addr, nbytes, dest_addr, lc_opt, flags, 6, (gasnetex_handlerarg_t)(a0), (gasnetex_handlerarg_t)(a1), (gasnetex_handlerarg_t)(a2), (gasnetex_handlerarg_t)(a3), (gasnetex_handlerarg_t)(a4), (gasnetex_handlerarg_t)(a5))
#define gasnetex_AMReplyLong7(token, handler, source_addr, nbytes, dest_addr, lc_opt, flags, a0, a1, a2, a3, a4, a5, a6) \
       gasnetc_AMReplyLongM(token, handler, source_addr, nbytes, dest_addr, lc_opt, flags, 7, (gasnetex_handlerarg_t)(a0), (gasnetex_handlerarg_t)(a1), (gasnetex_handlerarg_t)(a2), (gasnetex_handlerarg_t)(a3), (gasnetex_handlerarg_t)(a4), (gasnetex_handlerarg_t)(a5), (gasnetex_handlerarg_t)(a6))
#define gasnetex_AMReplyLong8(token, handler, source_addr, nbytes, dest_addr, lc_opt, flags, a0, a1, a2, a3, a4, a5, a6, a7) \
       gasnetc_AMReplyLongM(token, handler, source_addr, nbytes, dest_addr, lc_opt, flags, 8, (gasnetex_handlerarg_t)(a0), (gasnetex_handlerarg_t)(a1), (gasnetex_handlerarg_t)(a2), (gasnetex_handlerarg_t)(a3), (gasnetex_handlerarg_t)(a4), (gasnetex_handlerarg_t)(a5), (gasnetex_handlerarg_t)(a6), (gasnetex_handlerarg_t)(a7))

#define gasnetex_AMReplyLong9(token, handler, source_addr, nbytes, dest_addr, lc_opt, flags, a0, a1, a2, a3, a4, a5, a6, a7, a8 ) \
        gasnetc_AMReplyLongM(token, handler, source_addr, nbytes, dest_addr, lc_opt, flags,  9, (gasnetex_handlerarg_t)(a0), (gasnetex_handlerarg_t)(a1), (gasnetex_handlerarg_t)(a2), (gasnetex_handlerarg_t)(a3), (gasnetex_handlerarg_t)(a4), (gasnetex_handlerarg_t)(a5), (gasnetex_handlerarg_t)(a6), (gasnetex_handlerarg_t)(a7), (gasnetex_handlerarg_t)(a8))
#define gasnetex_AMReplyLong10(token, handler, source_addr, nbytes, dest_addr, lc_opt, flags, a0, a1, a2, a3, a4, a5, a6, a7, a8, a9) \
        gasnetc_AMReplyLongM(token, handler, source_addr, nbytes, dest_addr, lc_opt, flags, 10, (gasnetex_handlerarg_t)(a0), (gasnetex_handlerarg_t)(a1), (gasnetex_handlerarg_t)(a2), (gasnetex_handlerarg_t)(a3), (gasnetex_handlerarg_t)(a4), (gasnetex_handlerarg_t)(a5), (gasnetex_handlerarg_t)(a6), (gasnetex_handlerarg_t)(a7), (gasnetex_handlerarg_t)(a8), (gasnetex_handlerarg_t)(a9))
#define gasnetex_AMReplyLong11(token, handler, source_addr, nbytes, dest_addr, lc_opt, flags, a0, a1, a2, a3, a4, a5, a6, a7, a8, a9, a10) \
        gasnetc_AMReplyLongM(token, handler, source_addr, nbytes, dest_addr, lc_opt, flags, 11, (gasnetex_handlerarg_t)(a0), (gasnetex_handlerarg_t)(a1), (gasnetex_handlerarg_t)(a2), (gasnetex_handlerarg_t)(a3), (gasnetex_handlerarg_t)(a4), (gasnetex_handlerarg_t)(a5), (gasnetex_handlerarg_t)(a6), (gasnetex_handlerarg_t)(a7), (gasnetex_handlerarg_t)(a8), (gasnetex_handlerarg_t)(a9), (gasnetex_handlerarg_t)(a10))
#define gasnetex_AMReplyLong12(token, handler, source_addr, nbytes, dest_addr, lc_opt, flags, a0, a1, a2, a3, a4, a5, a6, a7, a8, a9, a10, a11) \
        gasnetc_AMReplyLongM(token, handler, source_addr, nbytes, dest_addr, lc_opt, flags, 12, (gasnetex_handlerarg_t)(a0), (gasnetex_handlerarg_t)(a1), (gasnetex_handlerarg_t)(a2), (gasnetex_handlerarg_t)(a3), (gasnetex_handlerarg_t)(a4), (gasnetex_handlerarg_t)(a5), (gasnetex_handlerarg_t)(a6), (gasnetex_handlerarg_t)(a7), (gasnetex_handlerarg_t)(a8), (gasnetex_handlerarg_t)(a9), (gasnetex_handlerarg_t)(a10), (gasnetex_handlerarg_t)(a11))

#define gasnetex_AMReplyLong13(token, handler, source_addr, nbytes, dest_addr, lc_opt, flags, a0, a1, a2, a3, a4, a5, a6, a7, a8, a9, a10, a11, a12) \
        gasnetc_AMReplyLongM(token, handler, source_addr, nbytes, dest_addr, lc_opt, flags, 13, (gasnetex_handlerarg_t)(a0), (gasnetex_handlerarg_t)(a1), (gasnetex_handlerarg_t)(a2), (gasnetex_handlerarg_t)(a3), (gasnetex_handlerarg_t)(a4), (gasnetex_handlerarg_t)(a5), (gasnetex_handlerarg_t)(a6), (gasnetex_handlerarg_t)(a7), (gasnetex_handlerarg_t)(a8), (gasnetex_handlerarg_t)(a9), (gasnetex_handlerarg_t)(a10), (gasnetex_handlerarg_t)(a11), (gasnetex_handlerarg_t)(a12))
#define gasnetex_AMReplyLong14(token, handler, source_addr, nbytes, dest_addr, lc_opt, flags, a0, a1, a2, a3, a4, a5, a6, a7, a8, a9, a10, a11, a12, a13) \
        gasnetc_AMReplyLongM(token, handler, source_addr, nbytes, dest_addr, lc_opt, flags, 14, (gasnetex_handlerarg_t)(a0), (gasnetex_handlerarg_t)(a1), (gasnetex_handlerarg_t)(a2), (gasnetex_handlerarg_t)(a3), (gasnetex_handlerarg_t)(a4), (gasnetex_handlerarg_t)(a5), (gasnetex_handlerarg_t)(a6), (gasnetex_handlerarg_t)(a7), (gasnetex_handlerarg_t)(a8), (gasnetex_handlerarg_t)(a9), (gasnetex_handlerarg_t)(a10), (gasnetex_handlerarg_t)(a11), (gasnetex_handlerarg_t)(a12), (gasnetex_handlerarg_t)(a13))
#define gasnetex_AMReplyLong15(token, handler, source_addr, nbytes, dest_addr, lc_opt, flags, a0, a1, a2, a3, a4, a5, a6, a7, a8, a9, a10, a11, a12, a13, a14) \
        gasnetc_AMReplyLongM(token, handler, source_addr, nbytes, dest_addr, lc_opt, flags, 15, (gasnetex_handlerarg_t)(a0), (gasnetex_handlerarg_t)(a1), (gasnetex_handlerarg_t)(a2), (gasnetex_handlerarg_t)(a3), (gasnetex_handlerarg_t)(a4), (gasnetex_handlerarg_t)(a5), (gasnetex_handlerarg_t)(a6), (gasnetex_handlerarg_t)(a7), (gasnetex_handlerarg_t)(a8), (gasnetex_handlerarg_t)(a9), (gasnetex_handlerarg_t)(a10), (gasnetex_handlerarg_t)(a11), (gasnetex_handlerarg_t)(a12), (gasnetex_handlerarg_t)(a13), (gasnetex_handlerarg_t)(a14))
#define gasnetex_AMReplyLong16(token, handler, source_addr, nbytes, dest_addr, lc_opt, flags, a0, a1, a2, a3, a4, a5, a6, a7, a8, a9, a10, a11, a12, a13, a14, a15) \
        gasnetc_AMReplyLongM(token, handler, source_addr, nbytes, dest_addr, lc_opt, flags, 16, (gasnetex_handlerarg_t)(a0), (gasnetex_handlerarg_t)(a1), (gasnetex_handlerarg_t)(a2), (gasnetex_handlerarg_t)(a3), (gasnetex_handlerarg_t)(a4), (gasnetex_handlerarg_t)(a5), (gasnetex_handlerarg_t)(a6), (gasnetex_handlerarg_t)(a7), (gasnetex_handlerarg_t)(a8), (gasnetex_handlerarg_t)(a9), (gasnetex_handlerarg_t)(a10), (gasnetex_handlerarg_t)(a11), (gasnetex_handlerarg_t)(a12), (gasnetex_handlerarg_t)(a13), (gasnetex_handlerarg_t)(a14), (gasnetex_handlerarg_t)(a15))
/* ------------------------------------------------------------------------------------ */

#endif

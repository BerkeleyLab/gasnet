/*   $Source: bitbucket.org:berkeleylab/gasnet.git/gasnet_ammacros.h $
 * Description: GASNet ammacros header
 * Copyright 2002, Dan Bonachea <bonachea@cs.berkeley.edu>
 * Terms of use are as specified in license.txt
 */

#ifndef _IN_GASNET_H
  #error This file is not meant to be included directly- clients should include gasnet.h
#endif

#ifndef _GASNET_AMMACROS_H
#define _GASNET_AMMACROS_H

GASNETI_BEGIN_EXTERNC

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
                gasnetex_lc_handle_t *lc_opt,  // Local completion control (see above)
                gasnetex_flags_t flags,        // Flags to control this operation
                int numargs, ...);             // Argument list (0..AMMaxArgs) as varargs
extern int gasnetc_AMReplyLongM(
                gasnetex_token_t token,        // Names local and remote contexts
                gasnetex_handler_t handler,
                /*const*/ void *source_addr,
                size_t nbytes,
                void *dest_addr,
                gasnetex_lc_handle_t *lc_opt,
                gasnetex_flags_t flags,
                int numargs, ...);
// Medium
extern int gasnetc_AMRequestMediumM(
                gasnetex_team_member_t team,
                gasnetex_rank_t rank,
                gasnetex_handler_t handler,
                /*const*/ void *source_addr,
                size_t nbytes,
                gasnetex_lc_handle_t *lc_opt,
                gasnetex_flags_t flags,
                int numargs, ...);
extern int gasnetc_AMReplyMediumM(
                gasnetex_token_t token,
                gasnetex_handler_t handler,
                /*const*/ void *source_addr,
                size_t nbytes,
                gasnetex_lc_handle_t *lc_opt,
                gasnetex_flags_t flags,
                int numargs, ...);
// Short
extern int gasnetc_AMRequestShortM(
                gasnetex_team_member_t team,
                gasnetex_rank_t rank,
                gasnetex_handler_t handler,
                gasnetex_flags_t flags,
                int numargs, ...);
extern int gasnetc_AMReplyShortM(
                gasnetex_token_t token,
                gasnetex_handler_t handler,
                gasnetex_flags_t flags,
                int numargs, ...);

GASNETI_END_EXTERNC

/* ------------------------------------------------------------------------------------ */
/*
  Active Message Macros
  =====================
*/
/*  yes, this is ugly, but it works... */
/* ------------------------------------------------------------------------------------ */
#define gasnetex_AMRequestShort0(team, rank, handler, flags) \
       gasnetc_AMRequestShortM(team, rank, handler, flags, 0)
#define gasnetex_AMRequestShort1(team, rank, handler, flags, a0) \
       gasnetc_AMRequestShortM(team, rank, handler, flags, 1, (gasnet_handlerarg_t)a0)
#define gasnetex_AMRequestShort2(team, rank, handler, flags, a0, a1) \
       gasnetc_AMRequestShortM(team, rank, handler, flags, 2, (gasnet_handlerarg_t)a0, (gasnet_handlerarg_t)a1)
#define gasnetex_AMRequestShort3(team, rank, handler, flags, a0, a1, a2) \
       gasnetc_AMRequestShortM(team, rank, handler, flags, 3, (gasnet_handlerarg_t)a0, (gasnet_handlerarg_t)a1, (gasnet_handlerarg_t)a2)
#define gasnetex_AMRequestShort4(team, rank, handler, flags, a0, a1, a2, a3) \
       gasnetc_AMRequestShortM(team, rank, handler, flags, 4, (gasnet_handlerarg_t)a0, (gasnet_handlerarg_t)a1, (gasnet_handlerarg_t)a2, (gasnet_handlerarg_t)a3)

#define gasnetex_AMRequestShort5(team, rank, handler, flags, a0, a1, a2, a3, a4) \
       gasnetc_AMRequestShortM(team, rank, handler, flags, 5, (gasnet_handlerarg_t)a0, (gasnet_handlerarg_t)a1, (gasnet_handlerarg_t)a2, (gasnet_handlerarg_t)a3, (gasnet_handlerarg_t)a4)
#define gasnetex_AMRequestShort6(team, rank, handler, flags, a0, a1, a2, a3, a4, a5) \
       gasnetc_AMRequestShortM(team, rank, handler, flags, 6, (gasnet_handlerarg_t)a0, (gasnet_handlerarg_t)a1, (gasnet_handlerarg_t)a2, (gasnet_handlerarg_t)a3, (gasnet_handlerarg_t)a4, (gasnet_handlerarg_t)a5)
#define gasnetex_AMRequestShort7(team, rank, handler, flags, a0, a1, a2, a3, a4, a5, a6) \
       gasnetc_AMRequestShortM(team, rank, handler, flags, 7, (gasnet_handlerarg_t)a0, (gasnet_handlerarg_t)a1, (gasnet_handlerarg_t)a2, (gasnet_handlerarg_t)a3, (gasnet_handlerarg_t)a4, (gasnet_handlerarg_t)a5, (gasnet_handlerarg_t)a6)
#define gasnetex_AMRequestShort8(team, rank, handler, flags, a0, a1, a2, a3, a4, a5, a6, a7) \
       gasnetc_AMRequestShortM(team, rank, handler, flags, 8, (gasnet_handlerarg_t)a0, (gasnet_handlerarg_t)a1, (gasnet_handlerarg_t)a2, (gasnet_handlerarg_t)a3, (gasnet_handlerarg_t)a4, (gasnet_handlerarg_t)a5, (gasnet_handlerarg_t)a6, (gasnet_handlerarg_t)a7)

#define gasnetex_AMRequestShort9(team, rank, handler, flags, a0, a1, a2, a3, a4, a5, a6, a7, a8 ) \
        gasnetc_AMRequestShortM(team, rank, handler, flags, 9, (gasnet_handlerarg_t)a0, (gasnet_handlerarg_t)a1, (gasnet_handlerarg_t)a2, (gasnet_handlerarg_t)a3, (gasnet_handlerarg_t)a4, (gasnet_handlerarg_t)a5, (gasnet_handlerarg_t)a6, (gasnet_handlerarg_t)a7, (gasnet_handlerarg_t)a8)
#define gasnetex_AMRequestShort10(team, rank, handler, flags, a0, a1, a2, a3, a4, a5, a6, a7, a8, a9) \
        gasnetc_AMRequestShortM(team, rank, handler, flags, 10, (gasnet_handlerarg_t)a0, (gasnet_handlerarg_t)a1, (gasnet_handlerarg_t)a2, (gasnet_handlerarg_t)a3, (gasnet_handlerarg_t)a4, (gasnet_handlerarg_t)a5, (gasnet_handlerarg_t)a6, (gasnet_handlerarg_t)a7, (gasnet_handlerarg_t)a8, (gasnet_handlerarg_t)a9)
#define gasnetex_AMRequestShort11(team, rank, handler, flags, a0, a1, a2, a3, a4, a5, a6, a7, a8, a9, a10) \
        gasnetc_AMRequestShortM(team, rank, handler, flags, 11, (gasnet_handlerarg_t)a0, (gasnet_handlerarg_t)a1, (gasnet_handlerarg_t)a2, (gasnet_handlerarg_t)a3, (gasnet_handlerarg_t)a4, (gasnet_handlerarg_t)a5, (gasnet_handlerarg_t)a6, (gasnet_handlerarg_t)a7, (gasnet_handlerarg_t)a8, (gasnet_handlerarg_t)a9, (gasnet_handlerarg_t)a10)
#define gasnetex_AMRequestShort12(team, rank, handler, flags, a0, a1, a2, a3, a4, a5, a6, a7, a8, a9, a10, a11) \
        gasnetc_AMRequestShortM(team, rank, handler, flags, 12, (gasnet_handlerarg_t)a0, (gasnet_handlerarg_t)a1, (gasnet_handlerarg_t)a2, (gasnet_handlerarg_t)a3, (gasnet_handlerarg_t)a4, (gasnet_handlerarg_t)a5, (gasnet_handlerarg_t)a6, (gasnet_handlerarg_t)a7, (gasnet_handlerarg_t)a8, (gasnet_handlerarg_t)a9, (gasnet_handlerarg_t)a10, (gasnet_handlerarg_t)a11)

#define gasnetex_AMRequestShort13(team, rank, handler, flags, a0, a1, a2, a3, a4, a5, a6, a7, a8, a9, a10, a11, a12) \
        gasnetc_AMRequestShortM(team, rank, handler, flags, 13, (gasnet_handlerarg_t)a0, (gasnet_handlerarg_t)a1, (gasnet_handlerarg_t)a2, (gasnet_handlerarg_t)a3, (gasnet_handlerarg_t)a4, (gasnet_handlerarg_t)a5, (gasnet_handlerarg_t)a6, (gasnet_handlerarg_t)a7, (gasnet_handlerarg_t)a8, (gasnet_handlerarg_t)a9, (gasnet_handlerarg_t)a10, (gasnet_handlerarg_t)a11, (gasnet_handlerarg_t)a12)
#define gasnetex_AMRequestShort14(team, rank, handler, flags, a0, a1, a2, a3, a4, a5, a6, a7, a8, a9, a10, a11, a12, a13) \
        gasnetc_AMRequestShortM(team, rank, handler, flags, 14, (gasnet_handlerarg_t)a0, (gasnet_handlerarg_t)a1, (gasnet_handlerarg_t)a2, (gasnet_handlerarg_t)a3, (gasnet_handlerarg_t)a4, (gasnet_handlerarg_t)a5, (gasnet_handlerarg_t)a6, (gasnet_handlerarg_t)a7, (gasnet_handlerarg_t)a8, (gasnet_handlerarg_t)a9, (gasnet_handlerarg_t)a10, (gasnet_handlerarg_t)a11, (gasnet_handlerarg_t)a12, (gasnet_handlerarg_t)a13)
#define gasnetex_AMRequestShort15(team, rank, handler, flags, a0, a1, a2, a3, a4, a5, a6, a7, a8, a9, a10, a11, a12, a13, a14) \
        gasnetc_AMRequestShortM(team, rank, handler, flags, 15, (gasnet_handlerarg_t)a0, (gasnet_handlerarg_t)a1, (gasnet_handlerarg_t)a2, (gasnet_handlerarg_t)a3, (gasnet_handlerarg_t)a4, (gasnet_handlerarg_t)a5, (gasnet_handlerarg_t)a6, (gasnet_handlerarg_t)a7, (gasnet_handlerarg_t)a8, (gasnet_handlerarg_t)a9, (gasnet_handlerarg_t)a10, (gasnet_handlerarg_t)a11, (gasnet_handlerarg_t)a12, (gasnet_handlerarg_t)a13, (gasnet_handlerarg_t)a14)
#define gasnetex_AMRequestShort16(team, rank, handler, flags, a0, a1, a2, a3, a4, a5, a6, a7, a8, a9, a10, a11, a12, a13, a14, a15) \
        gasnetc_AMRequestShortM(team, rank, handler, flags, 16, (gasnet_handlerarg_t)a0, (gasnet_handlerarg_t)a1, (gasnet_handlerarg_t)a2, (gasnet_handlerarg_t)a3, (gasnet_handlerarg_t)a4, (gasnet_handlerarg_t)a5, (gasnet_handlerarg_t)a6, (gasnet_handlerarg_t)a7, (gasnet_handlerarg_t)a8, (gasnet_handlerarg_t)a9, (gasnet_handlerarg_t)a10, (gasnet_handlerarg_t)a11, (gasnet_handlerarg_t)a12, (gasnet_handlerarg_t)a13, (gasnet_handlerarg_t)a14, (gasnet_handlerarg_t)a15)
/* ------------------------------------------------------------------------------------ */
#define gasnetex_AMRequestMedium0(team, rank, handler, source_addr, nbytes, lc_opt, flags) \
       gasnetc_AMRequestMediumM(team, rank, handler, source_addr, nbytes, lc_opt, flags, 0)
#define gasnetex_AMRequestMedium1(team, rank, handler, source_addr, nbytes, lc_opt, flags, a0) \
       gasnetc_AMRequestMediumM(team, rank, handler, source_addr, nbytes, lc_opt, flags, 1, (gasnet_handlerarg_t)a0)
#define gasnetex_AMRequestMedium2(team, rank, handler, source_addr, nbytes, lc_opt, flags, a0, a1) \
       gasnetc_AMRequestMediumM(team, rank, handler, source_addr, nbytes, lc_opt, flags, 2, (gasnet_handlerarg_t)a0, (gasnet_handlerarg_t)a1)
#define gasnetex_AMRequestMedium3(team, rank, handler, source_addr, nbytes, lc_opt, flags, a0, a1, a2) \
       gasnetc_AMRequestMediumM(team, rank, handler, source_addr, nbytes, lc_opt, flags, 3, (gasnet_handlerarg_t)a0, (gasnet_handlerarg_t)a1, (gasnet_handlerarg_t)a2)
#define gasnetex_AMRequestMedium4(team, rank, handler, source_addr, nbytes, lc_opt, flags, a0, a1, a2, a3) \
       gasnetc_AMRequestMediumM(team, rank, handler, source_addr, nbytes, lc_opt, flags, 4, (gasnet_handlerarg_t)a0, (gasnet_handlerarg_t)a1, (gasnet_handlerarg_t)a2, (gasnet_handlerarg_t)a3)

#define gasnetex_AMRequestMedium5(team, rank, handler, source_addr, nbytes, lc_opt, flags, a0, a1, a2, a3, a4) \
       gasnetc_AMRequestMediumM(team, rank, handler, source_addr, nbytes, lc_opt, flags, 5, (gasnet_handlerarg_t)a0, (gasnet_handlerarg_t)a1, (gasnet_handlerarg_t)a2, (gasnet_handlerarg_t)a3, (gasnet_handlerarg_t)a4)
#define gasnetex_AMRequestMedium6(team, rank, handler, source_addr, nbytes, lc_opt, flags, a0, a1, a2, a3, a4, a5) \
       gasnetc_AMRequestMediumM(team, rank, handler, source_addr, nbytes, lc_opt, flags, 6, (gasnet_handlerarg_t)a0, (gasnet_handlerarg_t)a1, (gasnet_handlerarg_t)a2, (gasnet_handlerarg_t)a3, (gasnet_handlerarg_t)a4, (gasnet_handlerarg_t)a5)
#define gasnetex_AMRequestMedium7(team, rank, handler, source_addr, nbytes, lc_opt, flags, a0, a1, a2, a3, a4, a5, a6) \
       gasnetc_AMRequestMediumM(team, rank, handler, source_addr, nbytes, lc_opt, flags, 7, (gasnet_handlerarg_t)a0, (gasnet_handlerarg_t)a1, (gasnet_handlerarg_t)a2, (gasnet_handlerarg_t)a3, (gasnet_handlerarg_t)a4, (gasnet_handlerarg_t)a5, (gasnet_handlerarg_t)a6)
#define gasnetex_AMRequestMedium8(team, rank, handler, source_addr, nbytes, lc_opt, flags, a0, a1, a2, a3, a4, a5, a6, a7) \
       gasnetc_AMRequestMediumM(team, rank, handler, source_addr, nbytes, lc_opt, flags, 8, (gasnet_handlerarg_t)a0, (gasnet_handlerarg_t)a1, (gasnet_handlerarg_t)a2, (gasnet_handlerarg_t)a3, (gasnet_handlerarg_t)a4, (gasnet_handlerarg_t)a5, (gasnet_handlerarg_t)a6, (gasnet_handlerarg_t)a7)

#define gasnetex_AMRequestMedium9(team, rank, handler, source_addr, nbytes, lc_opt, flags, a0, a1, a2, a3, a4, a5, a6, a7, a8 ) \
        gasnetc_AMRequestMediumM(team, rank, handler, source_addr, nbytes, lc_opt, flags,  9, (gasnet_handlerarg_t)a0, (gasnet_handlerarg_t)a1, (gasnet_handlerarg_t)a2, (gasnet_handlerarg_t)a3, (gasnet_handlerarg_t)a4, (gasnet_handlerarg_t)a5, (gasnet_handlerarg_t)a6, (gasnet_handlerarg_t)a7, (gasnet_handlerarg_t)a8)
#define gasnetex_AMRequestMedium10(team, rank, handler, source_addr, nbytes, lc_opt, flags, a0, a1, a2, a3, a4, a5, a6, a7, a8, a9) \
        gasnetc_AMRequestMediumM(team, rank, handler, source_addr, nbytes, lc_opt, flags, 10, (gasnet_handlerarg_t)a0, (gasnet_handlerarg_t)a1, (gasnet_handlerarg_t)a2, (gasnet_handlerarg_t)a3, (gasnet_handlerarg_t)a4, (gasnet_handlerarg_t)a5, (gasnet_handlerarg_t)a6, (gasnet_handlerarg_t)a7, (gasnet_handlerarg_t)a8, (gasnet_handlerarg_t)a9)
#define gasnetex_AMRequestMedium11(team, rank, handler, source_addr, nbytes, lc_opt, flags, a0, a1, a2, a3, a4, a5, a6, a7, a8, a9, a10) \
        gasnetc_AMRequestMediumM(team, rank, handler, source_addr, nbytes, lc_opt, flags, 11, (gasnet_handlerarg_t)a0, (gasnet_handlerarg_t)a1, (gasnet_handlerarg_t)a2, (gasnet_handlerarg_t)a3, (gasnet_handlerarg_t)a4, (gasnet_handlerarg_t)a5, (gasnet_handlerarg_t)a6, (gasnet_handlerarg_t)a7, (gasnet_handlerarg_t)a8, (gasnet_handlerarg_t)a9, (gasnet_handlerarg_t)a10)
#define gasnetex_AMRequestMedium12(team, rank, handler, source_addr, nbytes, lc_opt, flags, a0, a1, a2, a3, a4, a5, a6, a7, a8, a9, a10, a11) \
        gasnetc_AMRequestMediumM(team, rank, handler, source_addr, nbytes, lc_opt, flags, 12, (gasnet_handlerarg_t)a0, (gasnet_handlerarg_t)a1, (gasnet_handlerarg_t)a2, (gasnet_handlerarg_t)a3, (gasnet_handlerarg_t)a4, (gasnet_handlerarg_t)a5, (gasnet_handlerarg_t)a6, (gasnet_handlerarg_t)a7, (gasnet_handlerarg_t)a8, (gasnet_handlerarg_t)a9, (gasnet_handlerarg_t)a10, (gasnet_handlerarg_t)a11)

#define gasnetex_AMRequestMedium13(team, rank, handler, source_addr, nbytes, lc_opt, flags, a0, a1, a2, a3, a4, a5, a6, a7, a8, a9, a10, a11, a12) \
        gasnetc_AMRequestMediumM(team, rank, handler, source_addr, nbytes, lc_opt, flags, 13, (gasnet_handlerarg_t)a0, (gasnet_handlerarg_t)a1, (gasnet_handlerarg_t)a2, (gasnet_handlerarg_t)a3, (gasnet_handlerarg_t)a4, (gasnet_handlerarg_t)a5, (gasnet_handlerarg_t)a6, (gasnet_handlerarg_t)a7, (gasnet_handlerarg_t)a8, (gasnet_handlerarg_t)a9, (gasnet_handlerarg_t)a10, (gasnet_handlerarg_t)a11, (gasnet_handlerarg_t)a12)
#define gasnetex_AMRequestMedium14(team, rank, handler, source_addr, nbytes, lc_opt, flags, a0, a1, a2, a3, a4, a5, a6, a7, a8, a9, a10, a11, a12, a13) \
        gasnetc_AMRequestMediumM(team, rank, handler, source_addr, nbytes, lc_opt, flags, 14, (gasnet_handlerarg_t)a0, (gasnet_handlerarg_t)a1, (gasnet_handlerarg_t)a2, (gasnet_handlerarg_t)a3, (gasnet_handlerarg_t)a4, (gasnet_handlerarg_t)a5, (gasnet_handlerarg_t)a6, (gasnet_handlerarg_t)a7, (gasnet_handlerarg_t)a8, (gasnet_handlerarg_t)a9, (gasnet_handlerarg_t)a10, (gasnet_handlerarg_t)a11, (gasnet_handlerarg_t)a12, (gasnet_handlerarg_t)a13)
#define gasnetex_AMRequestMedium15(team, rank, handler, source_addr, nbytes, lc_opt, flags, a0, a1, a2, a3, a4, a5, a6, a7, a8, a9, a10, a11, a12, a13, a14) \
        gasnetc_AMRequestMediumM(team, rank, handler, source_addr, nbytes, lc_opt, flags, 15, (gasnet_handlerarg_t)a0, (gasnet_handlerarg_t)a1, (gasnet_handlerarg_t)a2, (gasnet_handlerarg_t)a3, (gasnet_handlerarg_t)a4, (gasnet_handlerarg_t)a5, (gasnet_handlerarg_t)a6, (gasnet_handlerarg_t)a7, (gasnet_handlerarg_t)a8, (gasnet_handlerarg_t)a9, (gasnet_handlerarg_t)a10, (gasnet_handlerarg_t)a11, (gasnet_handlerarg_t)a12, (gasnet_handlerarg_t)a13, (gasnet_handlerarg_t)a14)
#define gasnetex_AMRequestMedium16(team, rank, handler, source_addr, nbytes, lc_opt, flags, a0, a1, a2, a3, a4, a5, a6, a7, a8, a9, a10, a11, a12, a13, a14, a15) \
        gasnetc_AMRequestMediumM(team, rank, handler, source_addr, nbytes, lc_opt, flags, 16, (gasnet_handlerarg_t)a0, (gasnet_handlerarg_t)a1, (gasnet_handlerarg_t)a2, (gasnet_handlerarg_t)a3, (gasnet_handlerarg_t)a4, (gasnet_handlerarg_t)a5, (gasnet_handlerarg_t)a6, (gasnet_handlerarg_t)a7, (gasnet_handlerarg_t)a8, (gasnet_handlerarg_t)a9, (gasnet_handlerarg_t)a10, (gasnet_handlerarg_t)a11, (gasnet_handlerarg_t)a12, (gasnet_handlerarg_t)a13, (gasnet_handlerarg_t)a14, (gasnet_handlerarg_t)a15)
/* ------------------------------------------------------------------------------------ */
#define gasnetex_AMRequestLong0(team, rank, handler, source_addr, nbytes, dest_addr, lc_opt, flags) \
       gasnetc_AMRequestLongM(team, rank, handler, source_addr, nbytes, dest_addr, lc_opt, flags, 0)
#define gasnetex_AMRequestLong1(team, rank, handler, source_addr, nbytes, dest_addr, lc_opt, flags, a0) \
       gasnetc_AMRequestLongM(team, rank, handler, source_addr, nbytes, dest_addr, lc_opt, flags, 1, (gasnet_handlerarg_t)a0)
#define gasnetex_AMRequestLong2(team, rank, handler, source_addr, nbytes, dest_addr, lc_opt, flags, a0, a1) \
       gasnetc_AMRequestLongM(team, rank, handler, source_addr, nbytes, dest_addr, lc_opt, flags, 2, (gasnet_handlerarg_t)a0, (gasnet_handlerarg_t)a1)
#define gasnetex_AMRequestLong3(team, rank, handler, source_addr, nbytes, dest_addr, lc_opt, flags, a0, a1, a2) \
       gasnetc_AMRequestLongM(team, rank, handler, source_addr, nbytes, dest_addr, lc_opt, flags, 3, (gasnet_handlerarg_t)a0, (gasnet_handlerarg_t)a1, (gasnet_handlerarg_t)a2)
#define gasnetex_AMRequestLong4(team, rank, handler, source_addr, nbytes, dest_addr, lc_opt, flags, a0, a1, a2, a3) \
       gasnetc_AMRequestLongM(team, rank, handler, source_addr, nbytes, dest_addr, lc_opt, flags, 4, (gasnet_handlerarg_t)a0, (gasnet_handlerarg_t)a1, (gasnet_handlerarg_t)a2, (gasnet_handlerarg_t)a3)

#define gasnetex_AMRequestLong5(team, rank, handler, source_addr, nbytes, dest_addr, lc_opt, flags, a0, a1, a2, a3, a4) \
       gasnetc_AMRequestLongM(team, rank, handler, source_addr, nbytes, dest_addr, lc_opt, flags, 5, (gasnet_handlerarg_t)a0, (gasnet_handlerarg_t)a1, (gasnet_handlerarg_t)a2, (gasnet_handlerarg_t)a3, (gasnet_handlerarg_t)a4)
#define gasnetex_AMRequestLong6(team, rank, handler, source_addr, nbytes, dest_addr, lc_opt, flags, a0, a1, a2, a3, a4, a5) \
       gasnetc_AMRequestLongM(team, rank, handler, source_addr, nbytes, dest_addr, lc_opt, flags, 6, (gasnet_handlerarg_t)a0, (gasnet_handlerarg_t)a1, (gasnet_handlerarg_t)a2, (gasnet_handlerarg_t)a3, (gasnet_handlerarg_t)a4, (gasnet_handlerarg_t)a5)
#define gasnetex_AMRequestLong7(team, rank, handler, source_addr, nbytes, dest_addr, lc_opt, flags, a0, a1, a2, a3, a4, a5, a6) \
       gasnetc_AMRequestLongM(team, rank, handler, source_addr, nbytes, dest_addr, lc_opt, flags, 7, (gasnet_handlerarg_t)a0, (gasnet_handlerarg_t)a1, (gasnet_handlerarg_t)a2, (gasnet_handlerarg_t)a3, (gasnet_handlerarg_t)a4, (gasnet_handlerarg_t)a5, (gasnet_handlerarg_t)a6)
#define gasnetex_AMRequestLong8(team, rank, handler, source_addr, nbytes, dest_addr, lc_opt, flags, a0, a1, a2, a3, a4, a5, a6, a7) \
       gasnetc_AMRequestLongM(team, rank, handler, source_addr, nbytes, dest_addr, lc_opt, flags, 8, (gasnet_handlerarg_t)a0, (gasnet_handlerarg_t)a1, (gasnet_handlerarg_t)a2, (gasnet_handlerarg_t)a3, (gasnet_handlerarg_t)a4, (gasnet_handlerarg_t)a5, (gasnet_handlerarg_t)a6, (gasnet_handlerarg_t)a7)

#define gasnetex_AMRequestLong9(team, rank, handler, source_addr, nbytes, dest_addr, lc_opt, flags, a0, a1, a2, a3, a4, a5, a6, a7, a8 ) \
        gasnetc_AMRequestLongM(team, rank, handler, source_addr, nbytes, dest_addr, lc_opt, flags,  9, (gasnet_handlerarg_t)a0, (gasnet_handlerarg_t)a1, (gasnet_handlerarg_t)a2, (gasnet_handlerarg_t)a3, (gasnet_handlerarg_t)a4, (gasnet_handlerarg_t)a5, (gasnet_handlerarg_t)a6, (gasnet_handlerarg_t)a7, (gasnet_handlerarg_t)a8)
#define gasnetex_AMRequestLong10(team, rank, handler, source_addr, nbytes, dest_addr, lc_opt, flags, a0, a1, a2, a3, a4, a5, a6, a7, a8, a9) \
        gasnetc_AMRequestLongM(team, rank, handler, source_addr, nbytes, dest_addr, lc_opt, flags, 10, (gasnet_handlerarg_t)a0, (gasnet_handlerarg_t)a1, (gasnet_handlerarg_t)a2, (gasnet_handlerarg_t)a3, (gasnet_handlerarg_t)a4, (gasnet_handlerarg_t)a5, (gasnet_handlerarg_t)a6, (gasnet_handlerarg_t)a7, (gasnet_handlerarg_t)a8, (gasnet_handlerarg_t)a9)
#define gasnetex_AMRequestLong11(team, rank, handler, source_addr, nbytes, dest_addr, lc_opt, flags, a0, a1, a2, a3, a4, a5, a6, a7, a8, a9, a10) \
        gasnetc_AMRequestLongM(team, rank, handler, source_addr, nbytes, dest_addr, lc_opt, flags, 11, (gasnet_handlerarg_t)a0, (gasnet_handlerarg_t)a1, (gasnet_handlerarg_t)a2, (gasnet_handlerarg_t)a3, (gasnet_handlerarg_t)a4, (gasnet_handlerarg_t)a5, (gasnet_handlerarg_t)a6, (gasnet_handlerarg_t)a7, (gasnet_handlerarg_t)a8, (gasnet_handlerarg_t)a9, (gasnet_handlerarg_t)a10)
#define gasnetex_AMRequestLong12(team, rank, handler, source_addr, nbytes, dest_addr, lc_opt, flags, a0, a1, a2, a3, a4, a5, a6, a7, a8, a9, a10, a11) \
        gasnetc_AMRequestLongM(team, rank, handler, source_addr, nbytes, dest_addr, lc_opt, flags, 12, (gasnet_handlerarg_t)a0, (gasnet_handlerarg_t)a1, (gasnet_handlerarg_t)a2, (gasnet_handlerarg_t)a3, (gasnet_handlerarg_t)a4, (gasnet_handlerarg_t)a5, (gasnet_handlerarg_t)a6, (gasnet_handlerarg_t)a7, (gasnet_handlerarg_t)a8, (gasnet_handlerarg_t)a9, (gasnet_handlerarg_t)a10, (gasnet_handlerarg_t)a11)

#define gasnetex_AMRequestLong13(team, rank, handler, source_addr, nbytes, dest_addr, lc_opt, flags, a0, a1, a2, a3, a4, a5, a6, a7, a8, a9, a10, a11, a12) \
        gasnetc_AMRequestLongM(team, rank, handler, source_addr, nbytes, dest_addr, lc_opt, flags, 13, (gasnet_handlerarg_t)a0, (gasnet_handlerarg_t)a1, (gasnet_handlerarg_t)a2, (gasnet_handlerarg_t)a3, (gasnet_handlerarg_t)a4, (gasnet_handlerarg_t)a5, (gasnet_handlerarg_t)a6, (gasnet_handlerarg_t)a7, (gasnet_handlerarg_t)a8, (gasnet_handlerarg_t)a9, (gasnet_handlerarg_t)a10, (gasnet_handlerarg_t)a11, (gasnet_handlerarg_t)a12)
#define gasnetex_AMRequestLong14(team, rank, handler, source_addr, nbytes, dest_addr, lc_opt, flags, a0, a1, a2, a3, a4, a5, a6, a7, a8, a9, a10, a11, a12, a13) \
        gasnetc_AMRequestLongM(team, rank, handler, source_addr, nbytes, dest_addr, lc_opt, flags, 14, (gasnet_handlerarg_t)a0, (gasnet_handlerarg_t)a1, (gasnet_handlerarg_t)a2, (gasnet_handlerarg_t)a3, (gasnet_handlerarg_t)a4, (gasnet_handlerarg_t)a5, (gasnet_handlerarg_t)a6, (gasnet_handlerarg_t)a7, (gasnet_handlerarg_t)a8, (gasnet_handlerarg_t)a9, (gasnet_handlerarg_t)a10, (gasnet_handlerarg_t)a11, (gasnet_handlerarg_t)a12, (gasnet_handlerarg_t)a13)
#define gasnetex_AMRequestLong15(team, rank, handler, source_addr, nbytes, dest_addr, lc_opt, flags, a0, a1, a2, a3, a4, a5, a6, a7, a8, a9, a10, a11, a12, a13, a14) \
        gasnetc_AMRequestLongM(team, rank, handler, source_addr, nbytes, dest_addr, lc_opt, flags, 15, (gasnet_handlerarg_t)a0, (gasnet_handlerarg_t)a1, (gasnet_handlerarg_t)a2, (gasnet_handlerarg_t)a3, (gasnet_handlerarg_t)a4, (gasnet_handlerarg_t)a5, (gasnet_handlerarg_t)a6, (gasnet_handlerarg_t)a7, (gasnet_handlerarg_t)a8, (gasnet_handlerarg_t)a9, (gasnet_handlerarg_t)a10, (gasnet_handlerarg_t)a11, (gasnet_handlerarg_t)a12, (gasnet_handlerarg_t)a13, (gasnet_handlerarg_t)a14)
#define gasnetex_AMRequestLong16(team, rank, handler, source_addr, nbytes, dest_addr, lc_opt, flags, a0, a1, a2, a3, a4, a5, a6, a7, a8, a9, a10, a11, a12, a13, a14, a15) \
        gasnetc_AMRequestLongM(team, rank, handler, source_addr, nbytes, dest_addr, lc_opt, flags, 16, (gasnet_handlerarg_t)a0, (gasnet_handlerarg_t)a1, (gasnet_handlerarg_t)a2, (gasnet_handlerarg_t)a3, (gasnet_handlerarg_t)a4, (gasnet_handlerarg_t)a5, (gasnet_handlerarg_t)a6, (gasnet_handlerarg_t)a7, (gasnet_handlerarg_t)a8, (gasnet_handlerarg_t)a9, (gasnet_handlerarg_t)a10, (gasnet_handlerarg_t)a11, (gasnet_handlerarg_t)a12, (gasnet_handlerarg_t)a13, (gasnet_handlerarg_t)a14, (gasnet_handlerarg_t)a15)
/* ------------------------------------------------------------------------------------ */
#define gasnetex_AMReplyShort0(token, handler, flags) \
       gasnetc_AMReplyShortM(token, handler, flags, 0)
#define gasnetex_AMReplyShort1(token, handler, flags, a0) \
       gasnetc_AMReplyShortM(token, handler, flags, 1, (gasnet_handlerarg_t)a0)
#define gasnetex_AMReplyShort2(token, handler, flags, a0, a1) \
       gasnetc_AMReplyShortM(token, handler, flags, 2, (gasnet_handlerarg_t)a0, (gasnet_handlerarg_t)a1)
#define gasnetex_AMReplyShort3(token, handler, flags, a0, a1, a2) \
       gasnetc_AMReplyShortM(token, handler, flags, 3, (gasnet_handlerarg_t)a0, (gasnet_handlerarg_t)a1, (gasnet_handlerarg_t)a2)
#define gasnetex_AMReplyShort4(token, handler, flags, a0, a1, a2, a3) \
       gasnetc_AMReplyShortM(token, handler, flags, 4, (gasnet_handlerarg_t)a0, (gasnet_handlerarg_t)a1, (gasnet_handlerarg_t)a2, (gasnet_handlerarg_t)a3)

#define gasnetex_AMReplyShort5(token, handler, flags, a0, a1, a2, a3, a4) \
       gasnetc_AMReplyShortM(token, handler, flags, 5, (gasnet_handlerarg_t)a0, (gasnet_handlerarg_t)a1, (gasnet_handlerarg_t)a2, (gasnet_handlerarg_t)a3, (gasnet_handlerarg_t)a4)
#define gasnetex_AMReplyShort6(token, handler, flags, a0, a1, a2, a3, a4, a5) \
       gasnetc_AMReplyShortM(token, handler, flags, 6, (gasnet_handlerarg_t)a0, (gasnet_handlerarg_t)a1, (gasnet_handlerarg_t)a2, (gasnet_handlerarg_t)a3, (gasnet_handlerarg_t)a4, (gasnet_handlerarg_t)a5)
#define gasnetex_AMReplyShort7(token, handler, flags, a0, a1, a2, a3, a4, a5, a6) \
       gasnetc_AMReplyShortM(token, handler, flags, 7, (gasnet_handlerarg_t)a0, (gasnet_handlerarg_t)a1, (gasnet_handlerarg_t)a2, (gasnet_handlerarg_t)a3, (gasnet_handlerarg_t)a4, (gasnet_handlerarg_t)a5, (gasnet_handlerarg_t)a6)
#define gasnetex_AMReplyShort8(token, handler, flags, a0, a1, a2, a3, a4, a5, a6, a7) \
       gasnetc_AMReplyShortM(token, handler, flags, 8, (gasnet_handlerarg_t)a0, (gasnet_handlerarg_t)a1, (gasnet_handlerarg_t)a2, (gasnet_handlerarg_t)a3, (gasnet_handlerarg_t)a4, (gasnet_handlerarg_t)a5, (gasnet_handlerarg_t)a6, (gasnet_handlerarg_t)a7)

#define gasnetex_AMReplyShort9(token, handler, flags, a0, a1, a2, a3, a4, a5, a6, a7, a8 ) \
        gasnetc_AMReplyShortM(token, handler, flags, 9, (gasnet_handlerarg_t)a0, (gasnet_handlerarg_t)a1, (gasnet_handlerarg_t)a2, (gasnet_handlerarg_t)a3, (gasnet_handlerarg_t)a4, (gasnet_handlerarg_t)a5, (gasnet_handlerarg_t)a6, (gasnet_handlerarg_t)a7, (gasnet_handlerarg_t)a8)
#define gasnetex_AMReplyShort10(token, handler, flags, a0, a1, a2, a3, a4, a5, a6, a7, a8, a9) \
        gasnetc_AMReplyShortM(token, handler, flags, 10, (gasnet_handlerarg_t)a0, (gasnet_handlerarg_t)a1, (gasnet_handlerarg_t)a2, (gasnet_handlerarg_t)a3, (gasnet_handlerarg_t)a4, (gasnet_handlerarg_t)a5, (gasnet_handlerarg_t)a6, (gasnet_handlerarg_t)a7, (gasnet_handlerarg_t)a8, (gasnet_handlerarg_t)a9)
#define gasnetex_AMReplyShort11(token, handler, flags, a0, a1, a2, a3, a4, a5, a6, a7, a8, a9, a10) \
        gasnetc_AMReplyShortM(token, handler, flags, 11, (gasnet_handlerarg_t)a0, (gasnet_handlerarg_t)a1, (gasnet_handlerarg_t)a2, (gasnet_handlerarg_t)a3, (gasnet_handlerarg_t)a4, (gasnet_handlerarg_t)a5, (gasnet_handlerarg_t)a6, (gasnet_handlerarg_t)a7, (gasnet_handlerarg_t)a8, (gasnet_handlerarg_t)a9, (gasnet_handlerarg_t)a10)
#define gasnetex_AMReplyShort12(token, handler, flags, a0, a1, a2, a3, a4, a5, a6, a7, a8, a9, a10, a11) \
        gasnetc_AMReplyShortM(token, handler, flags, 12, (gasnet_handlerarg_t)a0, (gasnet_handlerarg_t)a1, (gasnet_handlerarg_t)a2, (gasnet_handlerarg_t)a3, (gasnet_handlerarg_t)a4, (gasnet_handlerarg_t)a5, (gasnet_handlerarg_t)a6, (gasnet_handlerarg_t)a7, (gasnet_handlerarg_t)a8, (gasnet_handlerarg_t)a9, (gasnet_handlerarg_t)a10, (gasnet_handlerarg_t)a11)

#define gasnetex_AMReplyShort13(token, handler, flags, a0, a1, a2, a3, a4, a5, a6, a7, a8, a9, a10, a11, a12) \
        gasnetc_AMReplyShortM(token, handler, flags, 13, (gasnet_handlerarg_t)a0, (gasnet_handlerarg_t)a1, (gasnet_handlerarg_t)a2, (gasnet_handlerarg_t)a3, (gasnet_handlerarg_t)a4, (gasnet_handlerarg_t)a5, (gasnet_handlerarg_t)a6, (gasnet_handlerarg_t)a7, (gasnet_handlerarg_t)a8, (gasnet_handlerarg_t)a9, (gasnet_handlerarg_t)a10, (gasnet_handlerarg_t)a11, (gasnet_handlerarg_t)a12)
#define gasnetex_AMReplyShort14(token, handler, flags, a0, a1, a2, a3, a4, a5, a6, a7, a8, a9, a10, a11, a12, a13) \
        gasnetc_AMReplyShortM(token, handler, flags, 14, (gasnet_handlerarg_t)a0, (gasnet_handlerarg_t)a1, (gasnet_handlerarg_t)a2, (gasnet_handlerarg_t)a3, (gasnet_handlerarg_t)a4, (gasnet_handlerarg_t)a5, (gasnet_handlerarg_t)a6, (gasnet_handlerarg_t)a7, (gasnet_handlerarg_t)a8, (gasnet_handlerarg_t)a9, (gasnet_handlerarg_t)a10, (gasnet_handlerarg_t)a11, (gasnet_handlerarg_t)a12, (gasnet_handlerarg_t)a13)
#define gasnetex_AMReplyShort15(token, handler, flags, a0, a1, a2, a3, a4, a5, a6, a7, a8, a9, a10, a11, a12, a13, a14) \
        gasnetc_AMReplyShortM(token, handler, flags, 15, (gasnet_handlerarg_t)a0, (gasnet_handlerarg_t)a1, (gasnet_handlerarg_t)a2, (gasnet_handlerarg_t)a3, (gasnet_handlerarg_t)a4, (gasnet_handlerarg_t)a5, (gasnet_handlerarg_t)a6, (gasnet_handlerarg_t)a7, (gasnet_handlerarg_t)a8, (gasnet_handlerarg_t)a9, (gasnet_handlerarg_t)a10, (gasnet_handlerarg_t)a11, (gasnet_handlerarg_t)a12, (gasnet_handlerarg_t)a13, (gasnet_handlerarg_t)a14)
#define gasnetex_AMReplyShort16(token, handler, flags, a0, a1, a2, a3, a4, a5, a6, a7, a8, a9, a10, a11, a12, a13, a14, a15) \
        gasnetc_AMReplyShortM(token, handler, flags, 16, (gasnet_handlerarg_t)a0, (gasnet_handlerarg_t)a1, (gasnet_handlerarg_t)a2, (gasnet_handlerarg_t)a3, (gasnet_handlerarg_t)a4, (gasnet_handlerarg_t)a5, (gasnet_handlerarg_t)a6, (gasnet_handlerarg_t)a7, (gasnet_handlerarg_t)a8, (gasnet_handlerarg_t)a9, (gasnet_handlerarg_t)a10, (gasnet_handlerarg_t)a11, (gasnet_handlerarg_t)a12, (gasnet_handlerarg_t)a13, (gasnet_handlerarg_t)a14, (gasnet_handlerarg_t)a15)
/* ------------------------------------------------------------------------------------ */
#define gasnetex_AMReplyMedium0(token, handler, source_addr, nbytes, lc_opt, flags) \
       gasnetc_AMReplyMediumM(token, handler, source_addr, nbytes, lc_opt, flags, 0)
#define gasnetex_AMReplyMedium1(token, handler, source_addr, nbytes, lc_opt, flags, a0) \
       gasnetc_AMReplyMediumM(token, handler, source_addr, nbytes, lc_opt, flags, 1, (gasnet_handlerarg_t)a0)
#define gasnetex_AMReplyMedium2(token, handler, source_addr, nbytes, lc_opt, flags, a0, a1) \
       gasnetc_AMReplyMediumM(token, handler, source_addr, nbytes, lc_opt, flags, 2, (gasnet_handlerarg_t)a0, (gasnet_handlerarg_t)a1)
#define gasnetex_AMReplyMedium3(token, handler, source_addr, nbytes, lc_opt, flags, a0, a1, a2) \
       gasnetc_AMReplyMediumM(token, handler, source_addr, nbytes, lc_opt, flags, 3, (gasnet_handlerarg_t)a0, (gasnet_handlerarg_t)a1, (gasnet_handlerarg_t)a2)
#define gasnetex_AMReplyMedium4(token, handler, source_addr, nbytes, lc_opt, flags, a0, a1, a2, a3) \
       gasnetc_AMReplyMediumM(token, handler, source_addr, nbytes, lc_opt, flags, 4, (gasnet_handlerarg_t)a0, (gasnet_handlerarg_t)a1, (gasnet_handlerarg_t)a2, (gasnet_handlerarg_t)a3)

#define gasnetex_AMReplyMedium5(token, handler, source_addr, nbytes, lc_opt, flags, a0, a1, a2, a3, a4) \
       gasnetc_AMReplyMediumM(token, handler, source_addr, nbytes, lc_opt, flags, 5, (gasnet_handlerarg_t)a0, (gasnet_handlerarg_t)a1, (gasnet_handlerarg_t)a2, (gasnet_handlerarg_t)a3, (gasnet_handlerarg_t)a4)
#define gasnetex_AMReplyMedium6(token, handler, source_addr, nbytes, lc_opt, flags, a0, a1, a2, a3, a4, a5) \
       gasnetc_AMReplyMediumM(token, handler, source_addr, nbytes, lc_opt, flags, 6, (gasnet_handlerarg_t)a0, (gasnet_handlerarg_t)a1, (gasnet_handlerarg_t)a2, (gasnet_handlerarg_t)a3, (gasnet_handlerarg_t)a4, (gasnet_handlerarg_t)a5)
#define gasnetex_AMReplyMedium7(token, handler, source_addr, nbytes, lc_opt, flags, a0, a1, a2, a3, a4, a5, a6) \
       gasnetc_AMReplyMediumM(token, handler, source_addr, nbytes, lc_opt, flags, 7, (gasnet_handlerarg_t)a0, (gasnet_handlerarg_t)a1, (gasnet_handlerarg_t)a2, (gasnet_handlerarg_t)a3, (gasnet_handlerarg_t)a4, (gasnet_handlerarg_t)a5, (gasnet_handlerarg_t)a6)
#define gasnetex_AMReplyMedium8(token, handler, source_addr, nbytes, lc_opt, flags, a0, a1, a2, a3, a4, a5, a6, a7) \
       gasnetc_AMReplyMediumM(token, handler, source_addr, nbytes, lc_opt, flags, 8, (gasnet_handlerarg_t)a0, (gasnet_handlerarg_t)a1, (gasnet_handlerarg_t)a2, (gasnet_handlerarg_t)a3, (gasnet_handlerarg_t)a4, (gasnet_handlerarg_t)a5, (gasnet_handlerarg_t)a6, (gasnet_handlerarg_t)a7)

#define gasnetex_AMReplyMedium9(token, handler, source_addr, nbytes, lc_opt, flags, a0, a1, a2, a3, a4, a5, a6, a7, a8 ) \
        gasnetc_AMReplyMediumM(token, handler, source_addr, nbytes, lc_opt, flags,  9, (gasnet_handlerarg_t)a0, (gasnet_handlerarg_t)a1, (gasnet_handlerarg_t)a2, (gasnet_handlerarg_t)a3, (gasnet_handlerarg_t)a4, (gasnet_handlerarg_t)a5, (gasnet_handlerarg_t)a6, (gasnet_handlerarg_t)a7, (gasnet_handlerarg_t)a8)
#define gasnetex_AMReplyMedium10(token, handler, source_addr, nbytes, lc_opt, flags, a0, a1, a2, a3, a4, a5, a6, a7, a8, a9) \
        gasnetc_AMReplyMediumM(token, handler, source_addr, nbytes, lc_opt, flags, 10, (gasnet_handlerarg_t)a0, (gasnet_handlerarg_t)a1, (gasnet_handlerarg_t)a2, (gasnet_handlerarg_t)a3, (gasnet_handlerarg_t)a4, (gasnet_handlerarg_t)a5, (gasnet_handlerarg_t)a6, (gasnet_handlerarg_t)a7, (gasnet_handlerarg_t)a8, (gasnet_handlerarg_t)a9)
#define gasnetex_AMReplyMedium11(token, handler, source_addr, nbytes, lc_opt, flags, a0, a1, a2, a3, a4, a5, a6, a7, a8, a9, a10) \
        gasnetc_AMReplyMediumM(token, handler, source_addr, nbytes, lc_opt, flags, 11, (gasnet_handlerarg_t)a0, (gasnet_handlerarg_t)a1, (gasnet_handlerarg_t)a2, (gasnet_handlerarg_t)a3, (gasnet_handlerarg_t)a4, (gasnet_handlerarg_t)a5, (gasnet_handlerarg_t)a6, (gasnet_handlerarg_t)a7, (gasnet_handlerarg_t)a8, (gasnet_handlerarg_t)a9, (gasnet_handlerarg_t)a10)
#define gasnetex_AMReplyMedium12(token, handler, source_addr, nbytes, lc_opt, flags, a0, a1, a2, a3, a4, a5, a6, a7, a8, a9, a10, a11) \
        gasnetc_AMReplyMediumM(token, handler, source_addr, nbytes, lc_opt, flags, 12, (gasnet_handlerarg_t)a0, (gasnet_handlerarg_t)a1, (gasnet_handlerarg_t)a2, (gasnet_handlerarg_t)a3, (gasnet_handlerarg_t)a4, (gasnet_handlerarg_t)a5, (gasnet_handlerarg_t)a6, (gasnet_handlerarg_t)a7, (gasnet_handlerarg_t)a8, (gasnet_handlerarg_t)a9, (gasnet_handlerarg_t)a10, (gasnet_handlerarg_t)a11)

#define gasnetex_AMReplyMedium13(token, handler, source_addr, nbytes, lc_opt, flags, a0, a1, a2, a3, a4, a5, a6, a7, a8, a9, a10, a11, a12) \
        gasnetc_AMReplyMediumM(token, handler, source_addr, nbytes, lc_opt, flags, 13, (gasnet_handlerarg_t)a0, (gasnet_handlerarg_t)a1, (gasnet_handlerarg_t)a2, (gasnet_handlerarg_t)a3, (gasnet_handlerarg_t)a4, (gasnet_handlerarg_t)a5, (gasnet_handlerarg_t)a6, (gasnet_handlerarg_t)a7, (gasnet_handlerarg_t)a8, (gasnet_handlerarg_t)a9, (gasnet_handlerarg_t)a10, (gasnet_handlerarg_t)a11, (gasnet_handlerarg_t)a12)
#define gasnetex_AMReplyMedium14(token, handler, source_addr, nbytes, lc_opt, flags, a0, a1, a2, a3, a4, a5, a6, a7, a8, a9, a10, a11, a12, a13) \
        gasnetc_AMReplyMediumM(token, handler, source_addr, nbytes, lc_opt, flags, 14, (gasnet_handlerarg_t)a0, (gasnet_handlerarg_t)a1, (gasnet_handlerarg_t)a2, (gasnet_handlerarg_t)a3, (gasnet_handlerarg_t)a4, (gasnet_handlerarg_t)a5, (gasnet_handlerarg_t)a6, (gasnet_handlerarg_t)a7, (gasnet_handlerarg_t)a8, (gasnet_handlerarg_t)a9, (gasnet_handlerarg_t)a10, (gasnet_handlerarg_t)a11, (gasnet_handlerarg_t)a12, (gasnet_handlerarg_t)a13)
#define gasnetex_AMReplyMedium15(token, handler, source_addr, nbytes, lc_opt, flags, a0, a1, a2, a3, a4, a5, a6, a7, a8, a9, a10, a11, a12, a13, a14) \
        gasnetc_AMReplyMediumM(token, handler, source_addr, nbytes, lc_opt, flags, 15, (gasnet_handlerarg_t)a0, (gasnet_handlerarg_t)a1, (gasnet_handlerarg_t)a2, (gasnet_handlerarg_t)a3, (gasnet_handlerarg_t)a4, (gasnet_handlerarg_t)a5, (gasnet_handlerarg_t)a6, (gasnet_handlerarg_t)a7, (gasnet_handlerarg_t)a8, (gasnet_handlerarg_t)a9, (gasnet_handlerarg_t)a10, (gasnet_handlerarg_t)a11, (gasnet_handlerarg_t)a12, (gasnet_handlerarg_t)a13, (gasnet_handlerarg_t)a14)
#define gasnetex_AMReplyMedium16(token, handler, source_addr, nbytes, lc_opt, flags, a0, a1, a2, a3, a4, a5, a6, a7, a8, a9, a10, a11, a12, a13, a14, a15) \
        gasnetc_AMReplyMediumM(token, handler, source_addr, nbytes, lc_opt, flags, 16, (gasnet_handlerarg_t)a0, (gasnet_handlerarg_t)a1, (gasnet_handlerarg_t)a2, (gasnet_handlerarg_t)a3, (gasnet_handlerarg_t)a4, (gasnet_handlerarg_t)a5, (gasnet_handlerarg_t)a6, (gasnet_handlerarg_t)a7, (gasnet_handlerarg_t)a8, (gasnet_handlerarg_t)a9, (gasnet_handlerarg_t)a10, (gasnet_handlerarg_t)a11, (gasnet_handlerarg_t)a12, (gasnet_handlerarg_t)a13, (gasnet_handlerarg_t)a14, (gasnet_handlerarg_t)a15)
/* ------------------------------------------------------------------------------------ */
#define gasnetex_AMReplyLong0(token, handler, source_addr, nbytes, dest_addr, lc_opt, flags) \
       gasnetc_AMReplyLongM(token, handler, source_addr, nbytes, dest_addr, lc_opt, flags, 0)
#define gasnetex_AMReplyLong1(token, handler, source_addr, nbytes, dest_addr, lc_opt, flags, a0) \
       gasnetc_AMReplyLongM(token, handler, source_addr, nbytes, dest_addr, lc_opt, flags, 1, (gasnet_handlerarg_t)a0)
#define gasnetex_AMReplyLong2(token, handler, source_addr, nbytes, dest_addr, lc_opt, flags, a0, a1) \
       gasnetc_AMReplyLongM(token, handler, source_addr, nbytes, dest_addr, lc_opt, flags, 2, (gasnet_handlerarg_t)a0, (gasnet_handlerarg_t)a1)
#define gasnetex_AMReplyLong3(token, handler, source_addr, nbytes, dest_addr, lc_opt, flags, a0, a1, a2) \
       gasnetc_AMReplyLongM(token, handler, source_addr, nbytes, dest_addr, lc_opt, flags, 3, (gasnet_handlerarg_t)a0, (gasnet_handlerarg_t)a1, (gasnet_handlerarg_t)a2)
#define gasnetex_AMReplyLong4(token, handler, source_addr, nbytes, dest_addr, lc_opt, flags, a0, a1, a2, a3) \
       gasnetc_AMReplyLongM(token, handler, source_addr, nbytes, dest_addr, lc_opt, flags, 4, (gasnet_handlerarg_t)a0, (gasnet_handlerarg_t)a1, (gasnet_handlerarg_t)a2, (gasnet_handlerarg_t)a3)

#define gasnetex_AMReplyLong5(token, handler, source_addr, nbytes, dest_addr, lc_opt, flags, a0, a1, a2, a3, a4) \
       gasnetc_AMReplyLongM(token, handler, source_addr, nbytes, dest_addr, lc_opt, flags, 5, (gasnet_handlerarg_t)a0, (gasnet_handlerarg_t)a1, (gasnet_handlerarg_t)a2, (gasnet_handlerarg_t)a3, (gasnet_handlerarg_t)a4)
#define gasnetex_AMReplyLong6(token, handler, source_addr, nbytes, dest_addr, lc_opt, flags, a0, a1, a2, a3, a4, a5) \
       gasnetc_AMReplyLongM(token, handler, source_addr, nbytes, dest_addr, lc_opt, flags, 6, (gasnet_handlerarg_t)a0, (gasnet_handlerarg_t)a1, (gasnet_handlerarg_t)a2, (gasnet_handlerarg_t)a3, (gasnet_handlerarg_t)a4, (gasnet_handlerarg_t)a5)
#define gasnetex_AMReplyLong7(token, handler, source_addr, nbytes, dest_addr, lc_opt, flags, a0, a1, a2, a3, a4, a5, a6) \
       gasnetc_AMReplyLongM(token, handler, source_addr, nbytes, dest_addr, lc_opt, flags, 7, (gasnet_handlerarg_t)a0, (gasnet_handlerarg_t)a1, (gasnet_handlerarg_t)a2, (gasnet_handlerarg_t)a3, (gasnet_handlerarg_t)a4, (gasnet_handlerarg_t)a5, (gasnet_handlerarg_t)a6)
#define gasnetex_AMReplyLong8(token, handler, source_addr, nbytes, dest_addr, lc_opt, flags, a0, a1, a2, a3, a4, a5, a6, a7) \
       gasnetc_AMReplyLongM(token, handler, source_addr, nbytes, dest_addr, lc_opt, flags, 8, (gasnet_handlerarg_t)a0, (gasnet_handlerarg_t)a1, (gasnet_handlerarg_t)a2, (gasnet_handlerarg_t)a3, (gasnet_handlerarg_t)a4, (gasnet_handlerarg_t)a5, (gasnet_handlerarg_t)a6, (gasnet_handlerarg_t)a7)

#define gasnetex_AMReplyLong9(token, handler, source_addr, nbytes, dest_addr, lc_opt, flags, a0, a1, a2, a3, a4, a5, a6, a7, a8 ) \
        gasnetc_AMReplyLongM(token, handler, source_addr, nbytes, dest_addr, lc_opt, flags,  9, (gasnet_handlerarg_t)a0, (gasnet_handlerarg_t)a1, (gasnet_handlerarg_t)a2, (gasnet_handlerarg_t)a3, (gasnet_handlerarg_t)a4, (gasnet_handlerarg_t)a5, (gasnet_handlerarg_t)a6, (gasnet_handlerarg_t)a7, (gasnet_handlerarg_t)a8)
#define gasnetex_AMReplyLong10(token, handler, source_addr, nbytes, dest_addr, lc_opt, flags, a0, a1, a2, a3, a4, a5, a6, a7, a8, a9) \
        gasnetc_AMReplyLongM(token, handler, source_addr, nbytes, dest_addr, lc_opt, flags, 10, (gasnet_handlerarg_t)a0, (gasnet_handlerarg_t)a1, (gasnet_handlerarg_t)a2, (gasnet_handlerarg_t)a3, (gasnet_handlerarg_t)a4, (gasnet_handlerarg_t)a5, (gasnet_handlerarg_t)a6, (gasnet_handlerarg_t)a7, (gasnet_handlerarg_t)a8, (gasnet_handlerarg_t)a9)
#define gasnetex_AMReplyLong11(token, handler, source_addr, nbytes, dest_addr, lc_opt, flags, a0, a1, a2, a3, a4, a5, a6, a7, a8, a9, a10) \
        gasnetc_AMReplyLongM(token, handler, source_addr, nbytes, dest_addr, lc_opt, flags, 11, (gasnet_handlerarg_t)a0, (gasnet_handlerarg_t)a1, (gasnet_handlerarg_t)a2, (gasnet_handlerarg_t)a3, (gasnet_handlerarg_t)a4, (gasnet_handlerarg_t)a5, (gasnet_handlerarg_t)a6, (gasnet_handlerarg_t)a7, (gasnet_handlerarg_t)a8, (gasnet_handlerarg_t)a9, (gasnet_handlerarg_t)a10)
#define gasnetex_AMReplyLong12(token, handler, source_addr, nbytes, dest_addr, lc_opt, flags, a0, a1, a2, a3, a4, a5, a6, a7, a8, a9, a10, a11) \
        gasnetc_AMReplyLongM(token, handler, source_addr, nbytes, dest_addr, lc_opt, flags, 12, (gasnet_handlerarg_t)a0, (gasnet_handlerarg_t)a1, (gasnet_handlerarg_t)a2, (gasnet_handlerarg_t)a3, (gasnet_handlerarg_t)a4, (gasnet_handlerarg_t)a5, (gasnet_handlerarg_t)a6, (gasnet_handlerarg_t)a7, (gasnet_handlerarg_t)a8, (gasnet_handlerarg_t)a9, (gasnet_handlerarg_t)a10, (gasnet_handlerarg_t)a11)

#define gasnetex_AMReplyLong13(token, handler, source_addr, nbytes, dest_addr, lc_opt, flags, a0, a1, a2, a3, a4, a5, a6, a7, a8, a9, a10, a11, a12) \
        gasnetc_AMReplyLongM(token, handler, source_addr, nbytes, dest_addr, lc_opt, flags, 13, (gasnet_handlerarg_t)a0, (gasnet_handlerarg_t)a1, (gasnet_handlerarg_t)a2, (gasnet_handlerarg_t)a3, (gasnet_handlerarg_t)a4, (gasnet_handlerarg_t)a5, (gasnet_handlerarg_t)a6, (gasnet_handlerarg_t)a7, (gasnet_handlerarg_t)a8, (gasnet_handlerarg_t)a9, (gasnet_handlerarg_t)a10, (gasnet_handlerarg_t)a11, (gasnet_handlerarg_t)a12)
#define gasnetex_AMReplyLong14(token, handler, source_addr, nbytes, dest_addr, lc_opt, flags, a0, a1, a2, a3, a4, a5, a6, a7, a8, a9, a10, a11, a12, a13) \
        gasnetc_AMReplyLongM(token, handler, source_addr, nbytes, dest_addr, lc_opt, flags, 14, (gasnet_handlerarg_t)a0, (gasnet_handlerarg_t)a1, (gasnet_handlerarg_t)a2, (gasnet_handlerarg_t)a3, (gasnet_handlerarg_t)a4, (gasnet_handlerarg_t)a5, (gasnet_handlerarg_t)a6, (gasnet_handlerarg_t)a7, (gasnet_handlerarg_t)a8, (gasnet_handlerarg_t)a9, (gasnet_handlerarg_t)a10, (gasnet_handlerarg_t)a11, (gasnet_handlerarg_t)a12, (gasnet_handlerarg_t)a13)
#define gasnetex_AMReplyLong15(token, handler, source_addr, nbytes, dest_addr, lc_opt, flags, a0, a1, a2, a3, a4, a5, a6, a7, a8, a9, a10, a11, a12, a13, a14) \
        gasnetc_AMReplyLongM(token, handler, source_addr, nbytes, dest_addr, lc_opt, flags, 15, (gasnet_handlerarg_t)a0, (gasnet_handlerarg_t)a1, (gasnet_handlerarg_t)a2, (gasnet_handlerarg_t)a3, (gasnet_handlerarg_t)a4, (gasnet_handlerarg_t)a5, (gasnet_handlerarg_t)a6, (gasnet_handlerarg_t)a7, (gasnet_handlerarg_t)a8, (gasnet_handlerarg_t)a9, (gasnet_handlerarg_t)a10, (gasnet_handlerarg_t)a11, (gasnet_handlerarg_t)a12, (gasnet_handlerarg_t)a13, (gasnet_handlerarg_t)a14)
#define gasnetex_AMReplyLong16(token, handler, source_addr, nbytes, dest_addr, lc_opt, flags, a0, a1, a2, a3, a4, a5, a6, a7, a8, a9, a10, a11, a12, a13, a14, a15) \
        gasnetc_AMReplyLongM(token, handler, source_addr, nbytes, dest_addr, lc_opt, flags, 16, (gasnet_handlerarg_t)a0, (gasnet_handlerarg_t)a1, (gasnet_handlerarg_t)a2, (gasnet_handlerarg_t)a3, (gasnet_handlerarg_t)a4, (gasnet_handlerarg_t)a5, (gasnet_handlerarg_t)a6, (gasnet_handlerarg_t)a7, (gasnet_handlerarg_t)a8, (gasnet_handlerarg_t)a9, (gasnet_handlerarg_t)a10, (gasnet_handlerarg_t)a11, (gasnet_handlerarg_t)a12, (gasnet_handlerarg_t)a13, (gasnet_handlerarg_t)a14, (gasnet_handlerarg_t)a15)
/* ------------------------------------------------------------------------------------ */

/* Variable-argument AM calls using C99 variable-argument macros to count args
 *
 * Note that there is no portable way to omit a comma appearing before the
 * expansion of an empty __VA_ARGS__.  For this reason, we are including the
 * 'flags' argument within the "...".
 */

#define GASNETEX_AMNUMARGS(...) GASNETEX_AMNUMARGS_(__VA_ARGS__,16,15,14,13,12,11,10,9,8,7,6,5,4,3,2,1,0)
#define GASNETEX_AMNUMARGS_(_0,_1,_2,_3,_4,_5,_6,_7,_8,_9,_10,_11,_12,_13,_14,_15,_16,N,...) N
#define GASNETEX_AMVA(_stem,...) _CONCAT(gasnetex_AM##_stem,GASNETEX_AMNUMARGS(__VA_ARGS__))

#define                 gasnetex_AMRequestShort(team,rank,hidx,...) \
        GASNETEX_AMVA(RequestShort,__VA_ARGS__)(team,rank,hidx,__VA_ARGS__)

#define                 gasnetex_AMRequestMedium(team,rank,hidx,src_addr,nbytes,lc_opt,...) \
        GASNETEX_AMVA(RequestMedium,__VA_ARGS__)(team,rank,hidx,src_addr,nbytes,lc_opt,__VA_ARGS__)

#define                 gasnetex_AMRequestLong(team,rank,hidx,src_addr,nbytes,dst_addr,lc_opt,...) \
        GASNETEX_AMVA(RequestLong,__VA_ARGS__)(team,rank,hidx,src_addr,nbytes,dst_addr,lc_opt,__VA_ARGS__)

#define                 gasnetex_AMReplyShort(token,hidx,...) \
        GASNETEX_AMVA(ReplyShort,__VA_ARGS__)(token,hidx,__VA_ARGS__)

#define                 gasnetex_AMReplyMedium(token,hidx,src_addr,nbytes,lc_opt,...) \
        GASNETEX_AMVA(ReplyMedium,__VA_ARGS__)(token,hidx,src_addr,nbytes,lc_opt,__VA_ARGS__)

#define                 gasnetex_AMReplyLong(token,hidx,src_addr,nbytes,dst_addr,lc_opt,...) \
        GASNETEX_AMVA(ReplyLong,__VA_ARGS__)(token,hidx,src_addr,nbytes,dst_addr,lc_opt,__VA_ARGS__)

/* ------------------------------------------------------------------------------------ */

// TODO-EX: START OF LEGACY APIs -- move these to gasnet2ex.h at some point
#define gasnet_AMRequestShort0(dest, handler) \
       gasnetex_AMRequestShort0(NULL, dest, handler, 0)
#define gasnet_AMRequestShort1(dest, handler, a0) \
       gasnetex_AMRequestShort1(NULL, dest, handler, 0, (gasnet_handlerarg_t)a0)
#define gasnet_AMRequestShort2(dest, handler, a0, a1) \
       gasnetex_AMRequestShort2(NULL, dest, handler, 0, (gasnet_handlerarg_t)a0, (gasnet_handlerarg_t)a1)
#define gasnet_AMRequestShort3(dest, handler, a0, a1, a2) \
       gasnetex_AMRequestShort3(NULL, dest, handler, 0, (gasnet_handlerarg_t)a0, (gasnet_handlerarg_t)a1, (gasnet_handlerarg_t)a2)
#define gasnet_AMRequestShort4(dest, handler, a0, a1, a2, a3) \
       gasnetex_AMRequestShort4(NULL, dest, handler, 0, (gasnet_handlerarg_t)a0, (gasnet_handlerarg_t)a1, (gasnet_handlerarg_t)a2, (gasnet_handlerarg_t)a3)

#define gasnet_AMRequestShort5(dest, handler, a0, a1, a2, a3, a4) \
       gasnetex_AMRequestShort5(NULL, dest, handler, 0, (gasnet_handlerarg_t)a0, (gasnet_handlerarg_t)a1, (gasnet_handlerarg_t)a2, (gasnet_handlerarg_t)a3, (gasnet_handlerarg_t)a4)
#define gasnet_AMRequestShort6(dest, handler, a0, a1, a2, a3, a4, a5) \
       gasnetex_AMRequestShort6(NULL, dest, handler, 0, (gasnet_handlerarg_t)a0, (gasnet_handlerarg_t)a1, (gasnet_handlerarg_t)a2, (gasnet_handlerarg_t)a3, (gasnet_handlerarg_t)a4, (gasnet_handlerarg_t)a5)
#define gasnet_AMRequestShort7(dest, handler, a0, a1, a2, a3, a4, a5, a6) \
       gasnetex_AMRequestShort7(NULL, dest, handler, 0, (gasnet_handlerarg_t)a0, (gasnet_handlerarg_t)a1, (gasnet_handlerarg_t)a2, (gasnet_handlerarg_t)a3, (gasnet_handlerarg_t)a4, (gasnet_handlerarg_t)a5, (gasnet_handlerarg_t)a6)
#define gasnet_AMRequestShort8(dest, handler, a0, a1, a2, a3, a4, a5, a6, a7) \
       gasnetex_AMRequestShort8(NULL, dest, handler, 0, (gasnet_handlerarg_t)a0, (gasnet_handlerarg_t)a1, (gasnet_handlerarg_t)a2, (gasnet_handlerarg_t)a3, (gasnet_handlerarg_t)a4, (gasnet_handlerarg_t)a5, (gasnet_handlerarg_t)a6, (gasnet_handlerarg_t)a7)

#define gasnet_AMRequestShort9( dest, handler, a0, a1, a2, a3, a4, a5, a6, a7, a8 ) \
        gasnetex_AMRequestShort9(NULL, dest, handler, 0, (gasnet_handlerarg_t)a0, (gasnet_handlerarg_t)a1, (gasnet_handlerarg_t)a2, (gasnet_handlerarg_t)a3, (gasnet_handlerarg_t)a4, (gasnet_handlerarg_t)a5, (gasnet_handlerarg_t)a6, (gasnet_handlerarg_t)a7, (gasnet_handlerarg_t)a8)
#define gasnet_AMRequestShort10(dest, handler, a0, a1, a2, a3, a4, a5, a6, a7, a8, a9) \
        gasnetex_AMRequestShort10(NULL, dest, handler, 0, (gasnet_handlerarg_t)a0, (gasnet_handlerarg_t)a1, (gasnet_handlerarg_t)a2, (gasnet_handlerarg_t)a3, (gasnet_handlerarg_t)a4, (gasnet_handlerarg_t)a5, (gasnet_handlerarg_t)a6, (gasnet_handlerarg_t)a7, (gasnet_handlerarg_t)a8, (gasnet_handlerarg_t)a9)
#define gasnet_AMRequestShort11(dest, handler, a0, a1, a2, a3, a4, a5, a6, a7, a8, a9, a10) \
        gasnetex_AMRequestShort11(NULL, dest, handler, 0, (gasnet_handlerarg_t)a0, (gasnet_handlerarg_t)a1, (gasnet_handlerarg_t)a2, (gasnet_handlerarg_t)a3, (gasnet_handlerarg_t)a4, (gasnet_handlerarg_t)a5, (gasnet_handlerarg_t)a6, (gasnet_handlerarg_t)a7, (gasnet_handlerarg_t)a8, (gasnet_handlerarg_t)a9, (gasnet_handlerarg_t)a10)
#define gasnet_AMRequestShort12(dest, handler, a0, a1, a2, a3, a4, a5, a6, a7, a8, a9, a10, a11) \
        gasnetex_AMRequestShort12(NULL, dest, handler, 0, (gasnet_handlerarg_t)a0, (gasnet_handlerarg_t)a1, (gasnet_handlerarg_t)a2, (gasnet_handlerarg_t)a3, (gasnet_handlerarg_t)a4, (gasnet_handlerarg_t)a5, (gasnet_handlerarg_t)a6, (gasnet_handlerarg_t)a7, (gasnet_handlerarg_t)a8, (gasnet_handlerarg_t)a9, (gasnet_handlerarg_t)a10, (gasnet_handlerarg_t)a11)

#define gasnet_AMRequestShort13(dest, handler, a0, a1, a2, a3, a4, a5, a6, a7, a8, a9, a10, a11, a12) \
        gasnetex_AMRequestShort13(NULL, dest, handler, 0, (gasnet_handlerarg_t)a0, (gasnet_handlerarg_t)a1, (gasnet_handlerarg_t)a2, (gasnet_handlerarg_t)a3, (gasnet_handlerarg_t)a4, (gasnet_handlerarg_t)a5, (gasnet_handlerarg_t)a6, (gasnet_handlerarg_t)a7, (gasnet_handlerarg_t)a8, (gasnet_handlerarg_t)a9, (gasnet_handlerarg_t)a10, (gasnet_handlerarg_t)a11, (gasnet_handlerarg_t)a12)
#define gasnet_AMRequestShort14(dest, handler, a0, a1, a2, a3, a4, a5, a6, a7, a8, a9, a10, a11, a12, a13) \
        gasnetex_AMRequestShort14(NULL, dest, handler, 0, (gasnet_handlerarg_t)a0, (gasnet_handlerarg_t)a1, (gasnet_handlerarg_t)a2, (gasnet_handlerarg_t)a3, (gasnet_handlerarg_t)a4, (gasnet_handlerarg_t)a5, (gasnet_handlerarg_t)a6, (gasnet_handlerarg_t)a7, (gasnet_handlerarg_t)a8, (gasnet_handlerarg_t)a9, (gasnet_handlerarg_t)a10, (gasnet_handlerarg_t)a11, (gasnet_handlerarg_t)a12, (gasnet_handlerarg_t)a13)
#define gasnet_AMRequestShort15(dest, handler, a0, a1, a2, a3, a4, a5, a6, a7, a8, a9, a10, a11, a12, a13, a14) \
        gasnetex_AMRequestShort15(NULL, dest, handler, 0, (gasnet_handlerarg_t)a0, (gasnet_handlerarg_t)a1, (gasnet_handlerarg_t)a2, (gasnet_handlerarg_t)a3, (gasnet_handlerarg_t)a4, (gasnet_handlerarg_t)a5, (gasnet_handlerarg_t)a6, (gasnet_handlerarg_t)a7, (gasnet_handlerarg_t)a8, (gasnet_handlerarg_t)a9, (gasnet_handlerarg_t)a10, (gasnet_handlerarg_t)a11, (gasnet_handlerarg_t)a12, (gasnet_handlerarg_t)a13, (gasnet_handlerarg_t)a14)
#define gasnet_AMRequestShort16(dest, handler, a0, a1, a2, a3, a4, a5, a6, a7, a8, a9, a10, a11, a12, a13, a14, a15) \
        gasnetex_AMRequestShort16(NULL, dest, handler, 0, (gasnet_handlerarg_t)a0, (gasnet_handlerarg_t)a1, (gasnet_handlerarg_t)a2, (gasnet_handlerarg_t)a3, (gasnet_handlerarg_t)a4, (gasnet_handlerarg_t)a5, (gasnet_handlerarg_t)a6, (gasnet_handlerarg_t)a7, (gasnet_handlerarg_t)a8, (gasnet_handlerarg_t)a9, (gasnet_handlerarg_t)a10, (gasnet_handlerarg_t)a11, (gasnet_handlerarg_t)a12, (gasnet_handlerarg_t)a13, (gasnet_handlerarg_t)a14, (gasnet_handlerarg_t)a15)
/* ------------------------------------------------------------------------------------ */
#define gasnet_AMRequestMedium0(dest, handler, source_addr, nbytes) \
       gasnetex_AMRequestMedium0(NULL, dest, handler, source_addr, nbytes, GASNETEX_LC_INIT, 0)
#define gasnet_AMRequestMedium1(dest, handler, source_addr, nbytes, a0) \
       gasnetex_AMRequestMedium1(NULL, dest, handler, source_addr, nbytes, GASNETEX_LC_INIT, 0, (gasnet_handlerarg_t)a0)
#define gasnet_AMRequestMedium2(dest, handler, source_addr, nbytes, a0, a1) \
       gasnetex_AMRequestMedium2(NULL, dest, handler, source_addr, nbytes, GASNETEX_LC_INIT, 0, (gasnet_handlerarg_t)a0, (gasnet_handlerarg_t)a1)
#define gasnet_AMRequestMedium3(dest, handler, source_addr, nbytes, a0, a1, a2) \
       gasnetex_AMRequestMedium3(NULL, dest, handler, source_addr, nbytes, GASNETEX_LC_INIT, 0, (gasnet_handlerarg_t)a0, (gasnet_handlerarg_t)a1, (gasnet_handlerarg_t)a2)
#define gasnet_AMRequestMedium4(dest, handler, source_addr, nbytes, a0, a1, a2, a3) \
       gasnetex_AMRequestMedium4(NULL, dest, handler, source_addr, nbytes, GASNETEX_LC_INIT, 0, (gasnet_handlerarg_t)a0, (gasnet_handlerarg_t)a1, (gasnet_handlerarg_t)a2, (gasnet_handlerarg_t)a3)

#define gasnet_AMRequestMedium5(dest, handler, source_addr, nbytes, a0, a1, a2, a3, a4) \
       gasnetex_AMRequestMedium5(NULL, dest, handler, source_addr, nbytes, GASNETEX_LC_INIT, 0, (gasnet_handlerarg_t)a0, (gasnet_handlerarg_t)a1, (gasnet_handlerarg_t)a2, (gasnet_handlerarg_t)a3, (gasnet_handlerarg_t)a4)
#define gasnet_AMRequestMedium6(dest, handler, source_addr, nbytes, a0, a1, a2, a3, a4, a5) \
       gasnetex_AMRequestMedium6(NULL, dest, handler, source_addr, nbytes, GASNETEX_LC_INIT, 0, (gasnet_handlerarg_t)a0, (gasnet_handlerarg_t)a1, (gasnet_handlerarg_t)a2, (gasnet_handlerarg_t)a3, (gasnet_handlerarg_t)a4, (gasnet_handlerarg_t)a5)
#define gasnet_AMRequestMedium7(dest, handler, source_addr, nbytes, a0, a1, a2, a3, a4, a5, a6) \
       gasnetex_AMRequestMedium7(NULL, dest, handler, source_addr, nbytes, GASNETEX_LC_INIT, 0, (gasnet_handlerarg_t)a0, (gasnet_handlerarg_t)a1, (gasnet_handlerarg_t)a2, (gasnet_handlerarg_t)a3, (gasnet_handlerarg_t)a4, (gasnet_handlerarg_t)a5, (gasnet_handlerarg_t)a6)
#define gasnet_AMRequestMedium8(dest, handler, source_addr, nbytes, a0, a1, a2, a3, a4, a5, a6, a7) \
       gasnetex_AMRequestMedium8(NULL, dest, handler, source_addr, nbytes, GASNETEX_LC_INIT, 0, (gasnet_handlerarg_t)a0, (gasnet_handlerarg_t)a1, (gasnet_handlerarg_t)a2, (gasnet_handlerarg_t)a3, (gasnet_handlerarg_t)a4, (gasnet_handlerarg_t)a5, (gasnet_handlerarg_t)a6, (gasnet_handlerarg_t)a7)

#define gasnet_AMRequestMedium9( dest, handler, source_addr, nbytes, a0, a1, a2, a3, a4, a5, a6, a7, a8 ) \
        gasnetex_AMRequestMedium9(NULL, dest, handler, source_addr, nbytes, GASNETEX_LC_INIT, 0, (gasnet_handlerarg_t)a0, (gasnet_handlerarg_t)a1, (gasnet_handlerarg_t)a2, (gasnet_handlerarg_t)a3, (gasnet_handlerarg_t)a4, (gasnet_handlerarg_t)a5, (gasnet_handlerarg_t)a6, (gasnet_handlerarg_t)a7, (gasnet_handlerarg_t)a8)
#define gasnet_AMRequestMedium10(dest, handler, source_addr, nbytes, a0, a1, a2, a3, a4, a5, a6, a7, a8, a9) \
        gasnetex_AMRequestMedium10(NULL, dest, handler, source_addr, nbytes, GASNETEX_LC_INIT, 0, (gasnet_handlerarg_t)a0, (gasnet_handlerarg_t)a1, (gasnet_handlerarg_t)a2, (gasnet_handlerarg_t)a3, (gasnet_handlerarg_t)a4, (gasnet_handlerarg_t)a5, (gasnet_handlerarg_t)a6, (gasnet_handlerarg_t)a7, (gasnet_handlerarg_t)a8, (gasnet_handlerarg_t)a9)
#define gasnet_AMRequestMedium11(dest, handler, source_addr, nbytes, a0, a1, a2, a3, a4, a5, a6, a7, a8, a9, a10) \
        gasnetex_AMRequestMedium11(NULL, dest, handler, source_addr, nbytes, GASNETEX_LC_INIT, 0, (gasnet_handlerarg_t)a0, (gasnet_handlerarg_t)a1, (gasnet_handlerarg_t)a2, (gasnet_handlerarg_t)a3, (gasnet_handlerarg_t)a4, (gasnet_handlerarg_t)a5, (gasnet_handlerarg_t)a6, (gasnet_handlerarg_t)a7, (gasnet_handlerarg_t)a8, (gasnet_handlerarg_t)a9, (gasnet_handlerarg_t)a10)
#define gasnet_AMRequestMedium12(dest, handler, source_addr, nbytes, a0, a1, a2, a3, a4, a5, a6, a7, a8, a9, a10, a11) \
        gasnetex_AMRequestMedium12(NULL, dest, handler, source_addr, nbytes, GASNETEX_LC_INIT, 0, (gasnet_handlerarg_t)a0, (gasnet_handlerarg_t)a1, (gasnet_handlerarg_t)a2, (gasnet_handlerarg_t)a3, (gasnet_handlerarg_t)a4, (gasnet_handlerarg_t)a5, (gasnet_handlerarg_t)a6, (gasnet_handlerarg_t)a7, (gasnet_handlerarg_t)a8, (gasnet_handlerarg_t)a9, (gasnet_handlerarg_t)a10, (gasnet_handlerarg_t)a11)

#define gasnet_AMRequestMedium13(dest, handler, source_addr, nbytes, a0, a1, a2, a3, a4, a5, a6, a7, a8, a9, a10, a11, a12) \
        gasnetex_AMRequestMedium13(NULL, dest, handler, source_addr, nbytes, GASNETEX_LC_INIT, 0, (gasnet_handlerarg_t)a0, (gasnet_handlerarg_t)a1, (gasnet_handlerarg_t)a2, (gasnet_handlerarg_t)a3, (gasnet_handlerarg_t)a4, (gasnet_handlerarg_t)a5, (gasnet_handlerarg_t)a6, (gasnet_handlerarg_t)a7, (gasnet_handlerarg_t)a8, (gasnet_handlerarg_t)a9, (gasnet_handlerarg_t)a10, (gasnet_handlerarg_t)a11, (gasnet_handlerarg_t)a12)
#define gasnet_AMRequestMedium14(dest, handler, source_addr, nbytes, a0, a1, a2, a3, a4, a5, a6, a7, a8, a9, a10, a11, a12, a13) \
        gasnetex_AMRequestMedium14(NULL, dest, handler, source_addr, nbytes, GASNETEX_LC_INIT, 0, (gasnet_handlerarg_t)a0, (gasnet_handlerarg_t)a1, (gasnet_handlerarg_t)a2, (gasnet_handlerarg_t)a3, (gasnet_handlerarg_t)a4, (gasnet_handlerarg_t)a5, (gasnet_handlerarg_t)a6, (gasnet_handlerarg_t)a7, (gasnet_handlerarg_t)a8, (gasnet_handlerarg_t)a9, (gasnet_handlerarg_t)a10, (gasnet_handlerarg_t)a11, (gasnet_handlerarg_t)a12, (gasnet_handlerarg_t)a13)
#define gasnet_AMRequestMedium15(dest, handler, source_addr, nbytes, a0, a1, a2, a3, a4, a5, a6, a7, a8, a9, a10, a11, a12, a13, a14) \
        gasnetex_AMRequestMedium15(NULL, dest, handler, source_addr, nbytes, GASNETEX_LC_INIT, 0, (gasnet_handlerarg_t)a0, (gasnet_handlerarg_t)a1, (gasnet_handlerarg_t)a2, (gasnet_handlerarg_t)a3, (gasnet_handlerarg_t)a4, (gasnet_handlerarg_t)a5, (gasnet_handlerarg_t)a6, (gasnet_handlerarg_t)a7, (gasnet_handlerarg_t)a8, (gasnet_handlerarg_t)a9, (gasnet_handlerarg_t)a10, (gasnet_handlerarg_t)a11, (gasnet_handlerarg_t)a12, (gasnet_handlerarg_t)a13, (gasnet_handlerarg_t)a14)
#define gasnet_AMRequestMedium16(dest, handler, source_addr, nbytes, a0, a1, a2, a3, a4, a5, a6, a7, a8, a9, a10, a11, a12, a13, a14, a15) \
        gasnetex_AMRequestMedium16(NULL, dest, handler, source_addr, nbytes, GASNETEX_LC_INIT, 0, (gasnet_handlerarg_t)a0, (gasnet_handlerarg_t)a1, (gasnet_handlerarg_t)a2, (gasnet_handlerarg_t)a3, (gasnet_handlerarg_t)a4, (gasnet_handlerarg_t)a5, (gasnet_handlerarg_t)a6, (gasnet_handlerarg_t)a7, (gasnet_handlerarg_t)a8, (gasnet_handlerarg_t)a9, (gasnet_handlerarg_t)a10, (gasnet_handlerarg_t)a11, (gasnet_handlerarg_t)a12, (gasnet_handlerarg_t)a13, (gasnet_handlerarg_t)a14, (gasnet_handlerarg_t)a15)
/* ------------------------------------------------------------------------------------ */
#define gasnet_AMRequestLong0(dest, handler, source_addr, nbytes, dest_addr) \
       gasnetex_AMRequestLong0(NULL, dest, handler, source_addr, nbytes, dest_addr, GASNETEX_LC_INIT, 0)
#define gasnet_AMRequestLong1(dest, handler, source_addr, nbytes, dest_addr, a0) \
       gasnetex_AMRequestLong1(NULL, dest, handler, source_addr, nbytes, dest_addr, GASNETEX_LC_INIT, 0, (gasnet_handlerarg_t)a0)
#define gasnet_AMRequestLong2(dest, handler, source_addr, nbytes, dest_addr, a0, a1) \
       gasnetex_AMRequestLong2(NULL, dest, handler, source_addr, nbytes, dest_addr, GASNETEX_LC_INIT, 0, (gasnet_handlerarg_t)a0, (gasnet_handlerarg_t)a1)
#define gasnet_AMRequestLong3(dest, handler, source_addr, nbytes, dest_addr, a0, a1, a2) \
       gasnetex_AMRequestLong3(NULL, dest, handler, source_addr, nbytes, dest_addr, GASNETEX_LC_INIT, 0, (gasnet_handlerarg_t)a0, (gasnet_handlerarg_t)a1, (gasnet_handlerarg_t)a2)
#define gasnet_AMRequestLong4(dest, handler, source_addr, nbytes, dest_addr, a0, a1, a2, a3) \
       gasnetex_AMRequestLong4(NULL, dest, handler, source_addr, nbytes, dest_addr, GASNETEX_LC_INIT, 0, (gasnet_handlerarg_t)a0, (gasnet_handlerarg_t)a1, (gasnet_handlerarg_t)a2, (gasnet_handlerarg_t)a3)

#define gasnet_AMRequestLong5(dest, handler, source_addr, nbytes, dest_addr, a0, a1, a2, a3, a4) \
       gasnetex_AMRequestLong5(NULL, dest, handler, source_addr, nbytes, dest_addr, GASNETEX_LC_INIT, 0, (gasnet_handlerarg_t)a0, (gasnet_handlerarg_t)a1, (gasnet_handlerarg_t)a2, (gasnet_handlerarg_t)a3, (gasnet_handlerarg_t)a4)
#define gasnet_AMRequestLong6(dest, handler, source_addr, nbytes, dest_addr, a0, a1, a2, a3, a4, a5) \
       gasnetex_AMRequestLong6(NULL, dest, handler, source_addr, nbytes, dest_addr, GASNETEX_LC_INIT, 0, (gasnet_handlerarg_t)a0, (gasnet_handlerarg_t)a1, (gasnet_handlerarg_t)a2, (gasnet_handlerarg_t)a3, (gasnet_handlerarg_t)a4, (gasnet_handlerarg_t)a5)
#define gasnet_AMRequestLong7(dest, handler, source_addr, nbytes, dest_addr, a0, a1, a2, a3, a4, a5, a6) \
       gasnetex_AMRequestLong7(NULL, dest, handler, source_addr, nbytes, dest_addr, GASNETEX_LC_INIT, 0, (gasnet_handlerarg_t)a0, (gasnet_handlerarg_t)a1, (gasnet_handlerarg_t)a2, (gasnet_handlerarg_t)a3, (gasnet_handlerarg_t)a4, (gasnet_handlerarg_t)a5, (gasnet_handlerarg_t)a6)
#define gasnet_AMRequestLong8(dest, handler, source_addr, nbytes, dest_addr, a0, a1, a2, a3, a4, a5, a6, a7) \
       gasnetex_AMRequestLong8(NULL, dest, handler, source_addr, nbytes, dest_addr, GASNETEX_LC_INIT, 0, (gasnet_handlerarg_t)a0, (gasnet_handlerarg_t)a1, (gasnet_handlerarg_t)a2, (gasnet_handlerarg_t)a3, (gasnet_handlerarg_t)a4, (gasnet_handlerarg_t)a5, (gasnet_handlerarg_t)a6, (gasnet_handlerarg_t)a7)

#define gasnet_AMRequestLong9( dest, handler, source_addr, nbytes, dest_addr, a0, a1, a2, a3, a4, a5, a6, a7, a8 ) \
        gasnetex_AMRequestLong9(NULL, dest, handler, source_addr, nbytes, dest_addr, GASNETEX_LC_INIT, 0, (gasnet_handlerarg_t)a0, (gasnet_handlerarg_t)a1, (gasnet_handlerarg_t)a2, (gasnet_handlerarg_t)a3, (gasnet_handlerarg_t)a4, (gasnet_handlerarg_t)a5, (gasnet_handlerarg_t)a6, (gasnet_handlerarg_t)a7, (gasnet_handlerarg_t)a8)
#define gasnet_AMRequestLong10(dest, handler, source_addr, nbytes, dest_addr, a0, a1, a2, a3, a4, a5, a6, a7, a8, a9) \
        gasnetex_AMRequestLong10(NULL, dest, handler, source_addr, nbytes, dest_addr, GASNETEX_LC_INIT, 0, (gasnet_handlerarg_t)a0, (gasnet_handlerarg_t)a1, (gasnet_handlerarg_t)a2, (gasnet_handlerarg_t)a3, (gasnet_handlerarg_t)a4, (gasnet_handlerarg_t)a5, (gasnet_handlerarg_t)a6, (gasnet_handlerarg_t)a7, (gasnet_handlerarg_t)a8, (gasnet_handlerarg_t)a9)
#define gasnet_AMRequestLong11(dest, handler, source_addr, nbytes, dest_addr, a0, a1, a2, a3, a4, a5, a6, a7, a8, a9, a10) \
        gasnetex_AMRequestLong11(NULL, dest, handler, source_addr, nbytes, dest_addr, GASNETEX_LC_INIT, 0, (gasnet_handlerarg_t)a0, (gasnet_handlerarg_t)a1, (gasnet_handlerarg_t)a2, (gasnet_handlerarg_t)a3, (gasnet_handlerarg_t)a4, (gasnet_handlerarg_t)a5, (gasnet_handlerarg_t)a6, (gasnet_handlerarg_t)a7, (gasnet_handlerarg_t)a8, (gasnet_handlerarg_t)a9, (gasnet_handlerarg_t)a10)
#define gasnet_AMRequestLong12(dest, handler, source_addr, nbytes, dest_addr, a0, a1, a2, a3, a4, a5, a6, a7, a8, a9, a10, a11) \
        gasnetex_AMRequestLong12(NULL, dest, handler, source_addr, nbytes, dest_addr, GASNETEX_LC_INIT, 0, (gasnet_handlerarg_t)a0, (gasnet_handlerarg_t)a1, (gasnet_handlerarg_t)a2, (gasnet_handlerarg_t)a3, (gasnet_handlerarg_t)a4, (gasnet_handlerarg_t)a5, (gasnet_handlerarg_t)a6, (gasnet_handlerarg_t)a7, (gasnet_handlerarg_t)a8, (gasnet_handlerarg_t)a9, (gasnet_handlerarg_t)a10, (gasnet_handlerarg_t)a11)

#define gasnet_AMRequestLong13(dest, handler, source_addr, nbytes, dest_addr, a0, a1, a2, a3, a4, a5, a6, a7, a8, a9, a10, a11, a12) \
        gasnetex_AMRequestLong13(NULL, dest, handler, source_addr, nbytes, dest_addr, GASNETEX_LC_INIT, 0, (gasnet_handlerarg_t)a0, (gasnet_handlerarg_t)a1, (gasnet_handlerarg_t)a2, (gasnet_handlerarg_t)a3, (gasnet_handlerarg_t)a4, (gasnet_handlerarg_t)a5, (gasnet_handlerarg_t)a6, (gasnet_handlerarg_t)a7, (gasnet_handlerarg_t)a8, (gasnet_handlerarg_t)a9, (gasnet_handlerarg_t)a10, (gasnet_handlerarg_t)a11, (gasnet_handlerarg_t)a12)
#define gasnet_AMRequestLong14(dest, handler, source_addr, nbytes, dest_addr, a0, a1, a2, a3, a4, a5, a6, a7, a8, a9, a10, a11, a12, a13) \
        gasnetex_AMRequestLong14(NULL, dest, handler, source_addr, nbytes, dest_addr, GASNETEX_LC_INIT, 0, (gasnet_handlerarg_t)a0, (gasnet_handlerarg_t)a1, (gasnet_handlerarg_t)a2, (gasnet_handlerarg_t)a3, (gasnet_handlerarg_t)a4, (gasnet_handlerarg_t)a5, (gasnet_handlerarg_t)a6, (gasnet_handlerarg_t)a7, (gasnet_handlerarg_t)a8, (gasnet_handlerarg_t)a9, (gasnet_handlerarg_t)a10, (gasnet_handlerarg_t)a11, (gasnet_handlerarg_t)a12, (gasnet_handlerarg_t)a13)
#define gasnet_AMRequestLong15(dest, handler, source_addr, nbytes, dest_addr, a0, a1, a2, a3, a4, a5, a6, a7, a8, a9, a10, a11, a12, a13, a14) \
        gasnetex_AMRequestLong15(NULL, dest, handler, source_addr, nbytes, dest_addr, GASNETEX_LC_INIT, 0, (gasnet_handlerarg_t)a0, (gasnet_handlerarg_t)a1, (gasnet_handlerarg_t)a2, (gasnet_handlerarg_t)a3, (gasnet_handlerarg_t)a4, (gasnet_handlerarg_t)a5, (gasnet_handlerarg_t)a6, (gasnet_handlerarg_t)a7, (gasnet_handlerarg_t)a8, (gasnet_handlerarg_t)a9, (gasnet_handlerarg_t)a10, (gasnet_handlerarg_t)a11, (gasnet_handlerarg_t)a12, (gasnet_handlerarg_t)a13, (gasnet_handlerarg_t)a14)
#define gasnet_AMRequestLong16(dest, handler, source_addr, nbytes, dest_addr, a0, a1, a2, a3, a4, a5, a6, a7, a8, a9, a10, a11, a12, a13, a14, a15) \
        gasnetex_AMRequestLong16(NULL, dest, handler, source_addr, nbytes, dest_addr, GASNETEX_LC_INIT, 0, (gasnet_handlerarg_t)a0, (gasnet_handlerarg_t)a1, (gasnet_handlerarg_t)a2, (gasnet_handlerarg_t)a3, (gasnet_handlerarg_t)a4, (gasnet_handlerarg_t)a5, (gasnet_handlerarg_t)a6, (gasnet_handlerarg_t)a7, (gasnet_handlerarg_t)a8, (gasnet_handlerarg_t)a9, (gasnet_handlerarg_t)a10, (gasnet_handlerarg_t)a11, (gasnet_handlerarg_t)a12, (gasnet_handlerarg_t)a13, (gasnet_handlerarg_t)a14, (gasnet_handlerarg_t)a15)
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
       gasnetex_AMReplyShort0(token, handler, 0)
#define gasnet_AMReplyShort1(token, handler, a0) \
       gasnetex_AMReplyShort1(token, handler, 0, (gasnet_handlerarg_t)a0)
#define gasnet_AMReplyShort2(token, handler, a0, a1) \
       gasnetex_AMReplyShort2(token, handler, 0, (gasnet_handlerarg_t)a0, (gasnet_handlerarg_t)a1)
#define gasnet_AMReplyShort3(token, handler, a0, a1, a2) \
       gasnetex_AMReplyShort3(token, handler, 0, (gasnet_handlerarg_t)a0, (gasnet_handlerarg_t)a1, (gasnet_handlerarg_t)a2)
#define gasnet_AMReplyShort4(token, handler, a0, a1, a2, a3) \
       gasnetex_AMReplyShort4(token, handler, 0, (gasnet_handlerarg_t)a0, (gasnet_handlerarg_t)a1, (gasnet_handlerarg_t)a2, (gasnet_handlerarg_t)a3)

#define gasnet_AMReplyShort5(token, handler, a0, a1, a2, a3, a4) \
       gasnetex_AMReplyShort5(token, handler, 0, (gasnet_handlerarg_t)a0, (gasnet_handlerarg_t)a1, (gasnet_handlerarg_t)a2, (gasnet_handlerarg_t)a3, (gasnet_handlerarg_t)a4)
#define gasnet_AMReplyShort6(token, handler, a0, a1, a2, a3, a4, a5) \
       gasnetex_AMReplyShort6(token, handler, 0, (gasnet_handlerarg_t)a0, (gasnet_handlerarg_t)a1, (gasnet_handlerarg_t)a2, (gasnet_handlerarg_t)a3, (gasnet_handlerarg_t)a4, (gasnet_handlerarg_t)a5)
#define gasnet_AMReplyShort7(token, handler, a0, a1, a2, a3, a4, a5, a6) \
       gasnetex_AMReplyShort7(token, handler, 0, (gasnet_handlerarg_t)a0, (gasnet_handlerarg_t)a1, (gasnet_handlerarg_t)a2, (gasnet_handlerarg_t)a3, (gasnet_handlerarg_t)a4, (gasnet_handlerarg_t)a5, (gasnet_handlerarg_t)a6)
#define gasnet_AMReplyShort8(token, handler, a0, a1, a2, a3, a4, a5, a6, a7) \
       gasnetex_AMReplyShort8(token, handler, 0, (gasnet_handlerarg_t)a0, (gasnet_handlerarg_t)a1, (gasnet_handlerarg_t)a2, (gasnet_handlerarg_t)a3, (gasnet_handlerarg_t)a4, (gasnet_handlerarg_t)a5, (gasnet_handlerarg_t)a6, (gasnet_handlerarg_t)a7)

#define gasnet_AMReplyShort9( token, handler, a0, a1, a2, a3, a4, a5, a6, a7, a8 ) \
        gasnetex_AMReplyShort9(token, handler, 0, (gasnet_handlerarg_t)a0, (gasnet_handlerarg_t)a1, (gasnet_handlerarg_t)a2, (gasnet_handlerarg_t)a3, (gasnet_handlerarg_t)a4, (gasnet_handlerarg_t)a5, (gasnet_handlerarg_t)a6, (gasnet_handlerarg_t)a7, (gasnet_handlerarg_t)a8)
#define gasnet_AMReplyShort10(token, handler, a0, a1, a2, a3, a4, a5, a6, a7, a8, a9) \
        gasnetex_AMReplyShort10(token, handler, 0, (gasnet_handlerarg_t)a0, (gasnet_handlerarg_t)a1, (gasnet_handlerarg_t)a2, (gasnet_handlerarg_t)a3, (gasnet_handlerarg_t)a4, (gasnet_handlerarg_t)a5, (gasnet_handlerarg_t)a6, (gasnet_handlerarg_t)a7, (gasnet_handlerarg_t)a8, (gasnet_handlerarg_t)a9)
#define gasnet_AMReplyShort11(token, handler, a0, a1, a2, a3, a4, a5, a6, a7, a8, a9, a10) \
        gasnetex_AMReplyShort11(token, handler, 0, (gasnet_handlerarg_t)a0, (gasnet_handlerarg_t)a1, (gasnet_handlerarg_t)a2, (gasnet_handlerarg_t)a3, (gasnet_handlerarg_t)a4, (gasnet_handlerarg_t)a5, (gasnet_handlerarg_t)a6, (gasnet_handlerarg_t)a7, (gasnet_handlerarg_t)a8, (gasnet_handlerarg_t)a9, (gasnet_handlerarg_t)a10)
#define gasnet_AMReplyShort12(token, handler, a0, a1, a2, a3, a4, a5, a6, a7, a8, a9, a10, a11) \
        gasnetex_AMReplyShort12(token, handler, 0, (gasnet_handlerarg_t)a0, (gasnet_handlerarg_t)a1, (gasnet_handlerarg_t)a2, (gasnet_handlerarg_t)a3, (gasnet_handlerarg_t)a4, (gasnet_handlerarg_t)a5, (gasnet_handlerarg_t)a6, (gasnet_handlerarg_t)a7, (gasnet_handlerarg_t)a8, (gasnet_handlerarg_t)a9, (gasnet_handlerarg_t)a10, (gasnet_handlerarg_t)a11)

#define gasnet_AMReplyShort13(token, handler, a0, a1, a2, a3, a4, a5, a6, a7, a8, a9, a10, a11, a12) \
        gasnetex_AMReplyShort13(token, handler, 0, (gasnet_handlerarg_t)a0, (gasnet_handlerarg_t)a1, (gasnet_handlerarg_t)a2, (gasnet_handlerarg_t)a3, (gasnet_handlerarg_t)a4, (gasnet_handlerarg_t)a5, (gasnet_handlerarg_t)a6, (gasnet_handlerarg_t)a7, (gasnet_handlerarg_t)a8, (gasnet_handlerarg_t)a9, (gasnet_handlerarg_t)a10, (gasnet_handlerarg_t)a11, (gasnet_handlerarg_t)a12)
#define gasnet_AMReplyShort14(token, handler, a0, a1, a2, a3, a4, a5, a6, a7, a8, a9, a10, a11, a12, a13) \
        gasnetex_AMReplyShort14(token, handler, 0, (gasnet_handlerarg_t)a0, (gasnet_handlerarg_t)a1, (gasnet_handlerarg_t)a2, (gasnet_handlerarg_t)a3, (gasnet_handlerarg_t)a4, (gasnet_handlerarg_t)a5, (gasnet_handlerarg_t)a6, (gasnet_handlerarg_t)a7, (gasnet_handlerarg_t)a8, (gasnet_handlerarg_t)a9, (gasnet_handlerarg_t)a10, (gasnet_handlerarg_t)a11, (gasnet_handlerarg_t)a12, (gasnet_handlerarg_t)a13)
#define gasnet_AMReplyShort15(token, handler, a0, a1, a2, a3, a4, a5, a6, a7, a8, a9, a10, a11, a12, a13, a14) \
        gasnetex_AMReplyShort15(token, handler, 0, (gasnet_handlerarg_t)a0, (gasnet_handlerarg_t)a1, (gasnet_handlerarg_t)a2, (gasnet_handlerarg_t)a3, (gasnet_handlerarg_t)a4, (gasnet_handlerarg_t)a5, (gasnet_handlerarg_t)a6, (gasnet_handlerarg_t)a7, (gasnet_handlerarg_t)a8, (gasnet_handlerarg_t)a9, (gasnet_handlerarg_t)a10, (gasnet_handlerarg_t)a11, (gasnet_handlerarg_t)a12, (gasnet_handlerarg_t)a13, (gasnet_handlerarg_t)a14)
#define gasnet_AMReplyShort16(token, handler, a0, a1, a2, a3, a4, a5, a6, a7, a8, a9, a10, a11, a12, a13, a14, a15) \
        gasnetex_AMReplyShort16(token, handler, 0, (gasnet_handlerarg_t)a0, (gasnet_handlerarg_t)a1, (gasnet_handlerarg_t)a2, (gasnet_handlerarg_t)a3, (gasnet_handlerarg_t)a4, (gasnet_handlerarg_t)a5, (gasnet_handlerarg_t)a6, (gasnet_handlerarg_t)a7, (gasnet_handlerarg_t)a8, (gasnet_handlerarg_t)a9, (gasnet_handlerarg_t)a10, (gasnet_handlerarg_t)a11, (gasnet_handlerarg_t)a12, (gasnet_handlerarg_t)a13, (gasnet_handlerarg_t)a14, (gasnet_handlerarg_t)a15)
/* ------------------------------------------------------------------------------------ */
#define gasnet_AMReplyMedium0(token, handler, source_addr, nbytes) \
       gasnetex_AMReplyMedium0(token, handler, source_addr, nbytes, GASNETEX_LC_INIT, 0)
#define gasnet_AMReplyMedium1(token, handler, source_addr, nbytes, a0) \
       gasnetex_AMReplyMedium1(token, handler, source_addr, nbytes, GASNETEX_LC_INIT, 0, (gasnet_handlerarg_t)a0)
#define gasnet_AMReplyMedium2(token, handler, source_addr, nbytes, a0, a1) \
       gasnetex_AMReplyMedium2(token, handler, source_addr, nbytes, GASNETEX_LC_INIT, 0, (gasnet_handlerarg_t)a0, (gasnet_handlerarg_t)a1)
#define gasnet_AMReplyMedium3(token, handler, source_addr, nbytes, a0, a1, a2) \
       gasnetex_AMReplyMedium3(token, handler, source_addr, nbytes, GASNETEX_LC_INIT, 0, (gasnet_handlerarg_t)a0, (gasnet_handlerarg_t)a1, (gasnet_handlerarg_t)a2)
#define gasnet_AMReplyMedium4(token, handler, source_addr, nbytes, a0, a1, a2, a3) \
       gasnetex_AMReplyMedium4(token, handler, source_addr, nbytes, GASNETEX_LC_INIT, 0, (gasnet_handlerarg_t)a0, (gasnet_handlerarg_t)a1, (gasnet_handlerarg_t)a2, (gasnet_handlerarg_t)a3)

#define gasnet_AMReplyMedium5(token, handler, source_addr, nbytes, a0, a1, a2, a3, a4) \
       gasnetex_AMReplyMedium5(token, handler, source_addr, nbytes, GASNETEX_LC_INIT, 0, (gasnet_handlerarg_t)a0, (gasnet_handlerarg_t)a1, (gasnet_handlerarg_t)a2, (gasnet_handlerarg_t)a3, (gasnet_handlerarg_t)a4)
#define gasnet_AMReplyMedium6(token, handler, source_addr, nbytes, a0, a1, a2, a3, a4, a5) \
       gasnetex_AMReplyMedium6(token, handler, source_addr, nbytes, GASNETEX_LC_INIT, 0, (gasnet_handlerarg_t)a0, (gasnet_handlerarg_t)a1, (gasnet_handlerarg_t)a2, (gasnet_handlerarg_t)a3, (gasnet_handlerarg_t)a4, (gasnet_handlerarg_t)a5)
#define gasnet_AMReplyMedium7(token, handler, source_addr, nbytes, a0, a1, a2, a3, a4, a5, a6) \
       gasnetex_AMReplyMedium7(token, handler, source_addr, nbytes, GASNETEX_LC_INIT, 0, (gasnet_handlerarg_t)a0, (gasnet_handlerarg_t)a1, (gasnet_handlerarg_t)a2, (gasnet_handlerarg_t)a3, (gasnet_handlerarg_t)a4, (gasnet_handlerarg_t)a5, (gasnet_handlerarg_t)a6)
#define gasnet_AMReplyMedium8(token, handler, source_addr, nbytes, a0, a1, a2, a3, a4, a5, a6, a7) \
       gasnetex_AMReplyMedium8(token, handler, source_addr, nbytes, GASNETEX_LC_INIT, 0, (gasnet_handlerarg_t)a0, (gasnet_handlerarg_t)a1, (gasnet_handlerarg_t)a2, (gasnet_handlerarg_t)a3, (gasnet_handlerarg_t)a4, (gasnet_handlerarg_t)a5, (gasnet_handlerarg_t)a6, (gasnet_handlerarg_t)a7)

#define gasnet_AMReplyMedium9( token, handler, source_addr, nbytes, a0, a1, a2, a3, a4, a5, a6, a7, a8 ) \
        gasnetex_AMReplyMedium9(token, handler, source_addr, nbytes, GASNETEX_LC_INIT, 0, (gasnet_handlerarg_t)a0, (gasnet_handlerarg_t)a1, (gasnet_handlerarg_t)a2, (gasnet_handlerarg_t)a3, (gasnet_handlerarg_t)a4, (gasnet_handlerarg_t)a5, (gasnet_handlerarg_t)a6, (gasnet_handlerarg_t)a7, (gasnet_handlerarg_t)a8)
#define gasnet_AMReplyMedium10(token, handler, source_addr, nbytes, a0, a1, a2, a3, a4, a5, a6, a7, a8, a9) \
        gasnetex_AMReplyMedium10(token, handler, source_addr, nbytes, GASNETEX_LC_INIT, 0, (gasnet_handlerarg_t)a0, (gasnet_handlerarg_t)a1, (gasnet_handlerarg_t)a2, (gasnet_handlerarg_t)a3, (gasnet_handlerarg_t)a4, (gasnet_handlerarg_t)a5, (gasnet_handlerarg_t)a6, (gasnet_handlerarg_t)a7, (gasnet_handlerarg_t)a8, (gasnet_handlerarg_t)a9)
#define gasnet_AMReplyMedium11(token, handler, source_addr, nbytes, a0, a1, a2, a3, a4, a5, a6, a7, a8, a9, a10) \
        gasnetex_AMReplyMedium11(token, handler, source_addr, nbytes, GASNETEX_LC_INIT, 0, (gasnet_handlerarg_t)a0, (gasnet_handlerarg_t)a1, (gasnet_handlerarg_t)a2, (gasnet_handlerarg_t)a3, (gasnet_handlerarg_t)a4, (gasnet_handlerarg_t)a5, (gasnet_handlerarg_t)a6, (gasnet_handlerarg_t)a7, (gasnet_handlerarg_t)a8, (gasnet_handlerarg_t)a9, (gasnet_handlerarg_t)a10)
#define gasnet_AMReplyMedium12(token, handler, source_addr, nbytes, a0, a1, a2, a3, a4, a5, a6, a7, a8, a9, a10, a11) \
        gasnetex_AMReplyMedium12(token, handler, source_addr, nbytes, GASNETEX_LC_INIT, 0, (gasnet_handlerarg_t)a0, (gasnet_handlerarg_t)a1, (gasnet_handlerarg_t)a2, (gasnet_handlerarg_t)a3, (gasnet_handlerarg_t)a4, (gasnet_handlerarg_t)a5, (gasnet_handlerarg_t)a6, (gasnet_handlerarg_t)a7, (gasnet_handlerarg_t)a8, (gasnet_handlerarg_t)a9, (gasnet_handlerarg_t)a10, (gasnet_handlerarg_t)a11)

#define gasnet_AMReplyMedium13(token, handler, source_addr, nbytes, a0, a1, a2, a3, a4, a5, a6, a7, a8, a9, a10, a11, a12) \
        gasnetex_AMReplyMedium13(token, handler, source_addr, nbytes, GASNETEX_LC_INIT, 0, (gasnet_handlerarg_t)a0, (gasnet_handlerarg_t)a1, (gasnet_handlerarg_t)a2, (gasnet_handlerarg_t)a3, (gasnet_handlerarg_t)a4, (gasnet_handlerarg_t)a5, (gasnet_handlerarg_t)a6, (gasnet_handlerarg_t)a7, (gasnet_handlerarg_t)a8, (gasnet_handlerarg_t)a9, (gasnet_handlerarg_t)a10, (gasnet_handlerarg_t)a11, (gasnet_handlerarg_t)a12)
#define gasnet_AMReplyMedium14(token, handler, source_addr, nbytes, a0, a1, a2, a3, a4, a5, a6, a7, a8, a9, a10, a11, a12, a13) \
        gasnetex_AMReplyMedium14(token, handler, source_addr, nbytes, GASNETEX_LC_INIT, 0, (gasnet_handlerarg_t)a0, (gasnet_handlerarg_t)a1, (gasnet_handlerarg_t)a2, (gasnet_handlerarg_t)a3, (gasnet_handlerarg_t)a4, (gasnet_handlerarg_t)a5, (gasnet_handlerarg_t)a6, (gasnet_handlerarg_t)a7, (gasnet_handlerarg_t)a8, (gasnet_handlerarg_t)a9, (gasnet_handlerarg_t)a10, (gasnet_handlerarg_t)a11, (gasnet_handlerarg_t)a12, (gasnet_handlerarg_t)a13)
#define gasnet_AMReplyMedium15(token, handler, source_addr, nbytes, a0, a1, a2, a3, a4, a5, a6, a7, a8, a9, a10, a11, a12, a13, a14) \
        gasnetex_AMReplyMedium15(token, handler, source_addr, nbytes, GASNETEX_LC_INIT, 0, (gasnet_handlerarg_t)a0, (gasnet_handlerarg_t)a1, (gasnet_handlerarg_t)a2, (gasnet_handlerarg_t)a3, (gasnet_handlerarg_t)a4, (gasnet_handlerarg_t)a5, (gasnet_handlerarg_t)a6, (gasnet_handlerarg_t)a7, (gasnet_handlerarg_t)a8, (gasnet_handlerarg_t)a9, (gasnet_handlerarg_t)a10, (gasnet_handlerarg_t)a11, (gasnet_handlerarg_t)a12, (gasnet_handlerarg_t)a13, (gasnet_handlerarg_t)a14)
#define gasnet_AMReplyMedium16(token, handler, source_addr, nbytes, a0, a1, a2, a3, a4, a5, a6, a7, a8, a9, a10, a11, a12, a13, a14, a15) \
        gasnetex_AMReplyMedium16(token, handler, source_addr, nbytes, GASNETEX_LC_INIT, 0, (gasnet_handlerarg_t)a0, (gasnet_handlerarg_t)a1, (gasnet_handlerarg_t)a2, (gasnet_handlerarg_t)a3, (gasnet_handlerarg_t)a4, (gasnet_handlerarg_t)a5, (gasnet_handlerarg_t)a6, (gasnet_handlerarg_t)a7, (gasnet_handlerarg_t)a8, (gasnet_handlerarg_t)a9, (gasnet_handlerarg_t)a10, (gasnet_handlerarg_t)a11, (gasnet_handlerarg_t)a12, (gasnet_handlerarg_t)a13, (gasnet_handlerarg_t)a14, (gasnet_handlerarg_t)a15)
/* ------------------------------------------------------------------------------------ */
#define gasnet_AMReplyLong0(token, handler, source_addr, nbytes, dest_addr) \
       gasnetex_AMReplyLong0(token, handler, source_addr, nbytes, dest_addr, GASNETEX_LC_INIT, 0)
#define gasnet_AMReplyLong1(token, handler, source_addr, nbytes, dest_addr, a0) \
       gasnetex_AMReplyLong1(token, handler, source_addr, nbytes, dest_addr, GASNETEX_LC_INIT, 0, (gasnet_handlerarg_t)a0)
#define gasnet_AMReplyLong2(token, handler, source_addr, nbytes, dest_addr, a0, a1) \
       gasnetex_AMReplyLong2(token, handler, source_addr, nbytes, dest_addr, GASNETEX_LC_INIT, 0, (gasnet_handlerarg_t)a0, (gasnet_handlerarg_t)a1)
#define gasnet_AMReplyLong3(token, handler, source_addr, nbytes, dest_addr, a0, a1, a2) \
       gasnetex_AMReplyLong3(token, handler, source_addr, nbytes, dest_addr, GASNETEX_LC_INIT, 0, (gasnet_handlerarg_t)a0, (gasnet_handlerarg_t)a1, (gasnet_handlerarg_t)a2)
#define gasnet_AMReplyLong4(token, handler, source_addr, nbytes, dest_addr, a0, a1, a2, a3) \
       gasnetex_AMReplyLong4(token, handler, source_addr, nbytes, dest_addr, GASNETEX_LC_INIT, 0, (gasnet_handlerarg_t)a0, (gasnet_handlerarg_t)a1, (gasnet_handlerarg_t)a2, (gasnet_handlerarg_t)a3)

#define gasnet_AMReplyLong5(token, handler, source_addr, nbytes, dest_addr, a0, a1, a2, a3, a4) \
       gasnetex_AMReplyLong5(token, handler, source_addr, nbytes, dest_addr, GASNETEX_LC_INIT, 0, (gasnet_handlerarg_t)a0, (gasnet_handlerarg_t)a1, (gasnet_handlerarg_t)a2, (gasnet_handlerarg_t)a3, (gasnet_handlerarg_t)a4)
#define gasnet_AMReplyLong6(token, handler, source_addr, nbytes, dest_addr, a0, a1, a2, a3, a4, a5) \
       gasnetex_AMReplyLong6(token, handler, source_addr, nbytes, dest_addr, GASNETEX_LC_INIT, 0, (gasnet_handlerarg_t)a0, (gasnet_handlerarg_t)a1, (gasnet_handlerarg_t)a2, (gasnet_handlerarg_t)a3, (gasnet_handlerarg_t)a4, (gasnet_handlerarg_t)a5)
#define gasnet_AMReplyLong7(token, handler, source_addr, nbytes, dest_addr, a0, a1, a2, a3, a4, a5, a6) \
       gasnetex_AMReplyLong7(token, handler, source_addr, nbytes, dest_addr, GASNETEX_LC_INIT, 0, (gasnet_handlerarg_t)a0, (gasnet_handlerarg_t)a1, (gasnet_handlerarg_t)a2, (gasnet_handlerarg_t)a3, (gasnet_handlerarg_t)a4, (gasnet_handlerarg_t)a5, (gasnet_handlerarg_t)a6)
#define gasnet_AMReplyLong8(token, handler, source_addr, nbytes, dest_addr, a0, a1, a2, a3, a4, a5, a6, a7) \
       gasnetex_AMReplyLong8(token, handler, source_addr, nbytes, dest_addr, GASNETEX_LC_INIT, 0, (gasnet_handlerarg_t)a0, (gasnet_handlerarg_t)a1, (gasnet_handlerarg_t)a2, (gasnet_handlerarg_t)a3, (gasnet_handlerarg_t)a4, (gasnet_handlerarg_t)a5, (gasnet_handlerarg_t)a6, (gasnet_handlerarg_t)a7)

#define gasnet_AMReplyLong9( token, handler, source_addr, nbytes, dest_addr, a0, a1, a2, a3, a4, a5, a6, a7, a8 ) \
        gasnetex_AMReplyLong9(token, handler, source_addr, nbytes, dest_addr, GASNETEX_LC_INIT, 0, (gasnet_handlerarg_t)a0, (gasnet_handlerarg_t)a1, (gasnet_handlerarg_t)a2, (gasnet_handlerarg_t)a3, (gasnet_handlerarg_t)a4, (gasnet_handlerarg_t)a5, (gasnet_handlerarg_t)a6, (gasnet_handlerarg_t)a7, (gasnet_handlerarg_t)a8)
#define gasnet_AMReplyLong10(token, handler, source_addr, nbytes, dest_addr, a0, a1, a2, a3, a4, a5, a6, a7, a8, a9) \
        gasnetex_AMReplyLong10(token, handler, source_addr, nbytes, dest_addr, GASNETEX_LC_INIT, 0, (gasnet_handlerarg_t)a0, (gasnet_handlerarg_t)a1, (gasnet_handlerarg_t)a2, (gasnet_handlerarg_t)a3, (gasnet_handlerarg_t)a4, (gasnet_handlerarg_t)a5, (gasnet_handlerarg_t)a6, (gasnet_handlerarg_t)a7, (gasnet_handlerarg_t)a8, (gasnet_handlerarg_t)a9)
#define gasnet_AMReplyLong11(token, handler, source_addr, nbytes, dest_addr, a0, a1, a2, a3, a4, a5, a6, a7, a8, a9, a10) \
        gasnetex_AMReplyLong11(token, handler, source_addr, nbytes, dest_addr, GASNETEX_LC_INIT, 0, (gasnet_handlerarg_t)a0, (gasnet_handlerarg_t)a1, (gasnet_handlerarg_t)a2, (gasnet_handlerarg_t)a3, (gasnet_handlerarg_t)a4, (gasnet_handlerarg_t)a5, (gasnet_handlerarg_t)a6, (gasnet_handlerarg_t)a7, (gasnet_handlerarg_t)a8, (gasnet_handlerarg_t)a9, (gasnet_handlerarg_t)a10)
#define gasnet_AMReplyLong12(token, handler, source_addr, nbytes, dest_addr, a0, a1, a2, a3, a4, a5, a6, a7, a8, a9, a10, a11) \
        gasnetex_AMReplyLong12(token, handler, source_addr, nbytes, dest_addr, GASNETEX_LC_INIT, 0, (gasnet_handlerarg_t)a0, (gasnet_handlerarg_t)a1, (gasnet_handlerarg_t)a2, (gasnet_handlerarg_t)a3, (gasnet_handlerarg_t)a4, (gasnet_handlerarg_t)a5, (gasnet_handlerarg_t)a6, (gasnet_handlerarg_t)a7, (gasnet_handlerarg_t)a8, (gasnet_handlerarg_t)a9, (gasnet_handlerarg_t)a10, (gasnet_handlerarg_t)a11)

#define gasnet_AMReplyLong13(token, handler, source_addr, nbytes, dest_addr, a0, a1, a2, a3, a4, a5, a6, a7, a8, a9, a10, a11, a12) \
        gasnetex_AMReplyLong13(token, handler, source_addr, nbytes, dest_addr, GASNETEX_LC_INIT, 0, (gasnet_handlerarg_t)a0, (gasnet_handlerarg_t)a1, (gasnet_handlerarg_t)a2, (gasnet_handlerarg_t)a3, (gasnet_handlerarg_t)a4, (gasnet_handlerarg_t)a5, (gasnet_handlerarg_t)a6, (gasnet_handlerarg_t)a7, (gasnet_handlerarg_t)a8, (gasnet_handlerarg_t)a9, (gasnet_handlerarg_t)a10, (gasnet_handlerarg_t)a11, (gasnet_handlerarg_t)a12)
#define gasnet_AMReplyLong14(token, handler, source_addr, nbytes, dest_addr, a0, a1, a2, a3, a4, a5, a6, a7, a8, a9, a10, a11, a12, a13) \
        gasnetex_AMReplyLong14(token, handler, source_addr, nbytes, dest_addr, GASNETEX_LC_INIT, 0, (gasnet_handlerarg_t)a0, (gasnet_handlerarg_t)a1, (gasnet_handlerarg_t)a2, (gasnet_handlerarg_t)a3, (gasnet_handlerarg_t)a4, (gasnet_handlerarg_t)a5, (gasnet_handlerarg_t)a6, (gasnet_handlerarg_t)a7, (gasnet_handlerarg_t)a8, (gasnet_handlerarg_t)a9, (gasnet_handlerarg_t)a10, (gasnet_handlerarg_t)a11, (gasnet_handlerarg_t)a12, (gasnet_handlerarg_t)a13)
#define gasnet_AMReplyLong15(token, handler, source_addr, nbytes, dest_addr, a0, a1, a2, a3, a4, a5, a6, a7, a8, a9, a10, a11, a12, a13, a14) \
        gasnetex_AMReplyLong15(token, handler, source_addr, nbytes, dest_addr, GASNETEX_LC_INIT, 0, (gasnet_handlerarg_t)a0, (gasnet_handlerarg_t)a1, (gasnet_handlerarg_t)a2, (gasnet_handlerarg_t)a3, (gasnet_handlerarg_t)a4, (gasnet_handlerarg_t)a5, (gasnet_handlerarg_t)a6, (gasnet_handlerarg_t)a7, (gasnet_handlerarg_t)a8, (gasnet_handlerarg_t)a9, (gasnet_handlerarg_t)a10, (gasnet_handlerarg_t)a11, (gasnet_handlerarg_t)a12, (gasnet_handlerarg_t)a13, (gasnet_handlerarg_t)a14)
#define gasnet_AMReplyLong16(token, handler, source_addr, nbytes, dest_addr, a0, a1, a2, a3, a4, a5, a6, a7, a8, a9, a10, a11, a12, a13, a14, a15) \
        gasnetex_AMReplyLong16(token, handler, source_addr, nbytes, dest_addr, GASNETEX_LC_INIT, 0, (gasnet_handlerarg_t)a0, (gasnet_handlerarg_t)a1, (gasnet_handlerarg_t)a2, (gasnet_handlerarg_t)a3, (gasnet_handlerarg_t)a4, (gasnet_handlerarg_t)a5, (gasnet_handlerarg_t)a6, (gasnet_handlerarg_t)a7, (gasnet_handlerarg_t)a8, (gasnet_handlerarg_t)a9, (gasnet_handlerarg_t)a10, (gasnet_handlerarg_t)a11, (gasnet_handlerarg_t)a12, (gasnet_handlerarg_t)a13, (gasnet_handlerarg_t)a14, (gasnet_handlerarg_t)a15)
/* ------------------------------------------------------------------------------------ */
// END OF LEGACY

#endif

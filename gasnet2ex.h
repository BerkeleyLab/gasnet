/*   $Source: bitbucket.org:berkeleylab/gasnet.git/gasnet2ex.h $
 * Description: GASNet -> GASNet-EX transitional header
 * Copyright 2016, The Regents of the University of California
 * Terms of use are as specified in license.txt
 */

#ifndef _GASNET2EX_H
#define _GASNET2EX_H

GASNETI_BEGIN_EXTERNC

/* Client-provided: */
extern gasnetex_team_member_t the_team;

/* ------------------------------------------------------------------------------------ */
/*
  Active Message Request/Reply Functions
  ======================================
*/

#define gasnet_AMRequestShort0(dest, handler) \
       gasnetex_AMRequestShort0(the_team, dest, handler, 0)
#define gasnet_AMRequestShort1(dest, handler, a0) \
       gasnetex_AMRequestShort1(the_team, dest, handler, 0, (gasnet_handlerarg_t)a0)
#define gasnet_AMRequestShort2(dest, handler, a0, a1) \
       gasnetex_AMRequestShort2(the_team, dest, handler, 0, (gasnet_handlerarg_t)a0, (gasnet_handlerarg_t)a1)
#define gasnet_AMRequestShort3(dest, handler, a0, a1, a2) \
       gasnetex_AMRequestShort3(the_team, dest, handler, 0, (gasnet_handlerarg_t)a0, (gasnet_handlerarg_t)a1, (gasnet_handlerarg_t)a2)
#define gasnet_AMRequestShort4(dest, handler, a0, a1, a2, a3) \
       gasnetex_AMRequestShort4(the_team, dest, handler, 0, (gasnet_handlerarg_t)a0, (gasnet_handlerarg_t)a1, (gasnet_handlerarg_t)a2, (gasnet_handlerarg_t)a3)

#define gasnet_AMRequestShort5(dest, handler, a0, a1, a2, a3, a4) \
       gasnetex_AMRequestShort5(the_team, dest, handler, 0, (gasnet_handlerarg_t)a0, (gasnet_handlerarg_t)a1, (gasnet_handlerarg_t)a2, (gasnet_handlerarg_t)a3, (gasnet_handlerarg_t)a4)
#define gasnet_AMRequestShort6(dest, handler, a0, a1, a2, a3, a4, a5) \
       gasnetex_AMRequestShort6(the_team, dest, handler, 0, (gasnet_handlerarg_t)a0, (gasnet_handlerarg_t)a1, (gasnet_handlerarg_t)a2, (gasnet_handlerarg_t)a3, (gasnet_handlerarg_t)a4, (gasnet_handlerarg_t)a5)
#define gasnet_AMRequestShort7(dest, handler, a0, a1, a2, a3, a4, a5, a6) \
       gasnetex_AMRequestShort7(the_team, dest, handler, 0, (gasnet_handlerarg_t)a0, (gasnet_handlerarg_t)a1, (gasnet_handlerarg_t)a2, (gasnet_handlerarg_t)a3, (gasnet_handlerarg_t)a4, (gasnet_handlerarg_t)a5, (gasnet_handlerarg_t)a6)
#define gasnet_AMRequestShort8(dest, handler, a0, a1, a2, a3, a4, a5, a6, a7) \
       gasnetex_AMRequestShort8(the_team, dest, handler, 0, (gasnet_handlerarg_t)a0, (gasnet_handlerarg_t)a1, (gasnet_handlerarg_t)a2, (gasnet_handlerarg_t)a3, (gasnet_handlerarg_t)a4, (gasnet_handlerarg_t)a5, (gasnet_handlerarg_t)a6, (gasnet_handlerarg_t)a7)

#define gasnet_AMRequestShort9( dest, handler, a0, a1, a2, a3, a4, a5, a6, a7, a8 ) \
        gasnetex_AMRequestShort9(the_team, dest, handler, 0, (gasnet_handlerarg_t)a0, (gasnet_handlerarg_t)a1, (gasnet_handlerarg_t)a2, (gasnet_handlerarg_t)a3, (gasnet_handlerarg_t)a4, (gasnet_handlerarg_t)a5, (gasnet_handlerarg_t)a6, (gasnet_handlerarg_t)a7, (gasnet_handlerarg_t)a8)
#define gasnet_AMRequestShort10(dest, handler, a0, a1, a2, a3, a4, a5, a6, a7, a8, a9) \
        gasnetex_AMRequestShort10(the_team, dest, handler, 0, (gasnet_handlerarg_t)a0, (gasnet_handlerarg_t)a1, (gasnet_handlerarg_t)a2, (gasnet_handlerarg_t)a3, (gasnet_handlerarg_t)a4, (gasnet_handlerarg_t)a5, (gasnet_handlerarg_t)a6, (gasnet_handlerarg_t)a7, (gasnet_handlerarg_t)a8, (gasnet_handlerarg_t)a9)
#define gasnet_AMRequestShort11(dest, handler, a0, a1, a2, a3, a4, a5, a6, a7, a8, a9, a10) \
        gasnetex_AMRequestShort11(the_team, dest, handler, 0, (gasnet_handlerarg_t)a0, (gasnet_handlerarg_t)a1, (gasnet_handlerarg_t)a2, (gasnet_handlerarg_t)a3, (gasnet_handlerarg_t)a4, (gasnet_handlerarg_t)a5, (gasnet_handlerarg_t)a6, (gasnet_handlerarg_t)a7, (gasnet_handlerarg_t)a8, (gasnet_handlerarg_t)a9, (gasnet_handlerarg_t)a10)
#define gasnet_AMRequestShort12(dest, handler, a0, a1, a2, a3, a4, a5, a6, a7, a8, a9, a10, a11) \
        gasnetex_AMRequestShort12(the_team, dest, handler, 0, (gasnet_handlerarg_t)a0, (gasnet_handlerarg_t)a1, (gasnet_handlerarg_t)a2, (gasnet_handlerarg_t)a3, (gasnet_handlerarg_t)a4, (gasnet_handlerarg_t)a5, (gasnet_handlerarg_t)a6, (gasnet_handlerarg_t)a7, (gasnet_handlerarg_t)a8, (gasnet_handlerarg_t)a9, (gasnet_handlerarg_t)a10, (gasnet_handlerarg_t)a11)

#define gasnet_AMRequestShort13(dest, handler, a0, a1, a2, a3, a4, a5, a6, a7, a8, a9, a10, a11, a12) \
        gasnetex_AMRequestShort13(the_team, dest, handler, 0, (gasnet_handlerarg_t)a0, (gasnet_handlerarg_t)a1, (gasnet_handlerarg_t)a2, (gasnet_handlerarg_t)a3, (gasnet_handlerarg_t)a4, (gasnet_handlerarg_t)a5, (gasnet_handlerarg_t)a6, (gasnet_handlerarg_t)a7, (gasnet_handlerarg_t)a8, (gasnet_handlerarg_t)a9, (gasnet_handlerarg_t)a10, (gasnet_handlerarg_t)a11, (gasnet_handlerarg_t)a12)
#define gasnet_AMRequestShort14(dest, handler, a0, a1, a2, a3, a4, a5, a6, a7, a8, a9, a10, a11, a12, a13) \
        gasnetex_AMRequestShort14(the_team, dest, handler, 0, (gasnet_handlerarg_t)a0, (gasnet_handlerarg_t)a1, (gasnet_handlerarg_t)a2, (gasnet_handlerarg_t)a3, (gasnet_handlerarg_t)a4, (gasnet_handlerarg_t)a5, (gasnet_handlerarg_t)a6, (gasnet_handlerarg_t)a7, (gasnet_handlerarg_t)a8, (gasnet_handlerarg_t)a9, (gasnet_handlerarg_t)a10, (gasnet_handlerarg_t)a11, (gasnet_handlerarg_t)a12, (gasnet_handlerarg_t)a13)
#define gasnet_AMRequestShort15(dest, handler, a0, a1, a2, a3, a4, a5, a6, a7, a8, a9, a10, a11, a12, a13, a14) \
        gasnetex_AMRequestShort15(the_team, dest, handler, 0, (gasnet_handlerarg_t)a0, (gasnet_handlerarg_t)a1, (gasnet_handlerarg_t)a2, (gasnet_handlerarg_t)a3, (gasnet_handlerarg_t)a4, (gasnet_handlerarg_t)a5, (gasnet_handlerarg_t)a6, (gasnet_handlerarg_t)a7, (gasnet_handlerarg_t)a8, (gasnet_handlerarg_t)a9, (gasnet_handlerarg_t)a10, (gasnet_handlerarg_t)a11, (gasnet_handlerarg_t)a12, (gasnet_handlerarg_t)a13, (gasnet_handlerarg_t)a14)
#define gasnet_AMRequestShort16(dest, handler, a0, a1, a2, a3, a4, a5, a6, a7, a8, a9, a10, a11, a12, a13, a14, a15) \
        gasnetex_AMRequestShort16(the_team, dest, handler, 0, (gasnet_handlerarg_t)a0, (gasnet_handlerarg_t)a1, (gasnet_handlerarg_t)a2, (gasnet_handlerarg_t)a3, (gasnet_handlerarg_t)a4, (gasnet_handlerarg_t)a5, (gasnet_handlerarg_t)a6, (gasnet_handlerarg_t)a7, (gasnet_handlerarg_t)a8, (gasnet_handlerarg_t)a9, (gasnet_handlerarg_t)a10, (gasnet_handlerarg_t)a11, (gasnet_handlerarg_t)a12, (gasnet_handlerarg_t)a13, (gasnet_handlerarg_t)a14, (gasnet_handlerarg_t)a15)
/* ------------------------------------------------------------------------------------ */
#define gasnet_AMRequestMedium0(dest, handler, source_addr, nbytes) \
       gasnetex_AMRequestMedium0(the_team, dest, handler, source_addr, nbytes, GASNETEX_LC_INIT, 0)
#define gasnet_AMRequestMedium1(dest, handler, source_addr, nbytes, a0) \
       gasnetex_AMRequestMedium1(the_team, dest, handler, source_addr, nbytes, GASNETEX_LC_INIT, 0, (gasnet_handlerarg_t)a0)
#define gasnet_AMRequestMedium2(dest, handler, source_addr, nbytes, a0, a1) \
       gasnetex_AMRequestMedium2(the_team, dest, handler, source_addr, nbytes, GASNETEX_LC_INIT, 0, (gasnet_handlerarg_t)a0, (gasnet_handlerarg_t)a1)
#define gasnet_AMRequestMedium3(dest, handler, source_addr, nbytes, a0, a1, a2) \
       gasnetex_AMRequestMedium3(the_team, dest, handler, source_addr, nbytes, GASNETEX_LC_INIT, 0, (gasnet_handlerarg_t)a0, (gasnet_handlerarg_t)a1, (gasnet_handlerarg_t)a2)
#define gasnet_AMRequestMedium4(dest, handler, source_addr, nbytes, a0, a1, a2, a3) \
       gasnetex_AMRequestMedium4(the_team, dest, handler, source_addr, nbytes, GASNETEX_LC_INIT, 0, (gasnet_handlerarg_t)a0, (gasnet_handlerarg_t)a1, (gasnet_handlerarg_t)a2, (gasnet_handlerarg_t)a3)

#define gasnet_AMRequestMedium5(dest, handler, source_addr, nbytes, a0, a1, a2, a3, a4) \
       gasnetex_AMRequestMedium5(the_team, dest, handler, source_addr, nbytes, GASNETEX_LC_INIT, 0, (gasnet_handlerarg_t)a0, (gasnet_handlerarg_t)a1, (gasnet_handlerarg_t)a2, (gasnet_handlerarg_t)a3, (gasnet_handlerarg_t)a4)
#define gasnet_AMRequestMedium6(dest, handler, source_addr, nbytes, a0, a1, a2, a3, a4, a5) \
       gasnetex_AMRequestMedium6(the_team, dest, handler, source_addr, nbytes, GASNETEX_LC_INIT, 0, (gasnet_handlerarg_t)a0, (gasnet_handlerarg_t)a1, (gasnet_handlerarg_t)a2, (gasnet_handlerarg_t)a3, (gasnet_handlerarg_t)a4, (gasnet_handlerarg_t)a5)
#define gasnet_AMRequestMedium7(dest, handler, source_addr, nbytes, a0, a1, a2, a3, a4, a5, a6) \
       gasnetex_AMRequestMedium7(the_team, dest, handler, source_addr, nbytes, GASNETEX_LC_INIT, 0, (gasnet_handlerarg_t)a0, (gasnet_handlerarg_t)a1, (gasnet_handlerarg_t)a2, (gasnet_handlerarg_t)a3, (gasnet_handlerarg_t)a4, (gasnet_handlerarg_t)a5, (gasnet_handlerarg_t)a6)
#define gasnet_AMRequestMedium8(dest, handler, source_addr, nbytes, a0, a1, a2, a3, a4, a5, a6, a7) \
       gasnetex_AMRequestMedium8(the_team, dest, handler, source_addr, nbytes, GASNETEX_LC_INIT, 0, (gasnet_handlerarg_t)a0, (gasnet_handlerarg_t)a1, (gasnet_handlerarg_t)a2, (gasnet_handlerarg_t)a3, (gasnet_handlerarg_t)a4, (gasnet_handlerarg_t)a5, (gasnet_handlerarg_t)a6, (gasnet_handlerarg_t)a7)

#define gasnet_AMRequestMedium9( dest, handler, source_addr, nbytes, a0, a1, a2, a3, a4, a5, a6, a7, a8 ) \
        gasnetex_AMRequestMedium9(the_team, dest, handler, source_addr, nbytes, GASNETEX_LC_INIT, 0, (gasnet_handlerarg_t)a0, (gasnet_handlerarg_t)a1, (gasnet_handlerarg_t)a2, (gasnet_handlerarg_t)a3, (gasnet_handlerarg_t)a4, (gasnet_handlerarg_t)a5, (gasnet_handlerarg_t)a6, (gasnet_handlerarg_t)a7, (gasnet_handlerarg_t)a8)
#define gasnet_AMRequestMedium10(dest, handler, source_addr, nbytes, a0, a1, a2, a3, a4, a5, a6, a7, a8, a9) \
        gasnetex_AMRequestMedium10(the_team, dest, handler, source_addr, nbytes, GASNETEX_LC_INIT, 0, (gasnet_handlerarg_t)a0, (gasnet_handlerarg_t)a1, (gasnet_handlerarg_t)a2, (gasnet_handlerarg_t)a3, (gasnet_handlerarg_t)a4, (gasnet_handlerarg_t)a5, (gasnet_handlerarg_t)a6, (gasnet_handlerarg_t)a7, (gasnet_handlerarg_t)a8, (gasnet_handlerarg_t)a9)
#define gasnet_AMRequestMedium11(dest, handler, source_addr, nbytes, a0, a1, a2, a3, a4, a5, a6, a7, a8, a9, a10) \
        gasnetex_AMRequestMedium11(the_team, dest, handler, source_addr, nbytes, GASNETEX_LC_INIT, 0, (gasnet_handlerarg_t)a0, (gasnet_handlerarg_t)a1, (gasnet_handlerarg_t)a2, (gasnet_handlerarg_t)a3, (gasnet_handlerarg_t)a4, (gasnet_handlerarg_t)a5, (gasnet_handlerarg_t)a6, (gasnet_handlerarg_t)a7, (gasnet_handlerarg_t)a8, (gasnet_handlerarg_t)a9, (gasnet_handlerarg_t)a10)
#define gasnet_AMRequestMedium12(dest, handler, source_addr, nbytes, a0, a1, a2, a3, a4, a5, a6, a7, a8, a9, a10, a11) \
        gasnetex_AMRequestMedium12(the_team, dest, handler, source_addr, nbytes, GASNETEX_LC_INIT, 0, (gasnet_handlerarg_t)a0, (gasnet_handlerarg_t)a1, (gasnet_handlerarg_t)a2, (gasnet_handlerarg_t)a3, (gasnet_handlerarg_t)a4, (gasnet_handlerarg_t)a5, (gasnet_handlerarg_t)a6, (gasnet_handlerarg_t)a7, (gasnet_handlerarg_t)a8, (gasnet_handlerarg_t)a9, (gasnet_handlerarg_t)a10, (gasnet_handlerarg_t)a11)

#define gasnet_AMRequestMedium13(dest, handler, source_addr, nbytes, a0, a1, a2, a3, a4, a5, a6, a7, a8, a9, a10, a11, a12) \
        gasnetex_AMRequestMedium13(the_team, dest, handler, source_addr, nbytes, GASNETEX_LC_INIT, 0, (gasnet_handlerarg_t)a0, (gasnet_handlerarg_t)a1, (gasnet_handlerarg_t)a2, (gasnet_handlerarg_t)a3, (gasnet_handlerarg_t)a4, (gasnet_handlerarg_t)a5, (gasnet_handlerarg_t)a6, (gasnet_handlerarg_t)a7, (gasnet_handlerarg_t)a8, (gasnet_handlerarg_t)a9, (gasnet_handlerarg_t)a10, (gasnet_handlerarg_t)a11, (gasnet_handlerarg_t)a12)
#define gasnet_AMRequestMedium14(dest, handler, source_addr, nbytes, a0, a1, a2, a3, a4, a5, a6, a7, a8, a9, a10, a11, a12, a13) \
        gasnetex_AMRequestMedium14(the_team, dest, handler, source_addr, nbytes, GASNETEX_LC_INIT, 0, (gasnet_handlerarg_t)a0, (gasnet_handlerarg_t)a1, (gasnet_handlerarg_t)a2, (gasnet_handlerarg_t)a3, (gasnet_handlerarg_t)a4, (gasnet_handlerarg_t)a5, (gasnet_handlerarg_t)a6, (gasnet_handlerarg_t)a7, (gasnet_handlerarg_t)a8, (gasnet_handlerarg_t)a9, (gasnet_handlerarg_t)a10, (gasnet_handlerarg_t)a11, (gasnet_handlerarg_t)a12, (gasnet_handlerarg_t)a13)
#define gasnet_AMRequestMedium15(dest, handler, source_addr, nbytes, a0, a1, a2, a3, a4, a5, a6, a7, a8, a9, a10, a11, a12, a13, a14) \
        gasnetex_AMRequestMedium15(the_team, dest, handler, source_addr, nbytes, GASNETEX_LC_INIT, 0, (gasnet_handlerarg_t)a0, (gasnet_handlerarg_t)a1, (gasnet_handlerarg_t)a2, (gasnet_handlerarg_t)a3, (gasnet_handlerarg_t)a4, (gasnet_handlerarg_t)a5, (gasnet_handlerarg_t)a6, (gasnet_handlerarg_t)a7, (gasnet_handlerarg_t)a8, (gasnet_handlerarg_t)a9, (gasnet_handlerarg_t)a10, (gasnet_handlerarg_t)a11, (gasnet_handlerarg_t)a12, (gasnet_handlerarg_t)a13, (gasnet_handlerarg_t)a14)
#define gasnet_AMRequestMedium16(dest, handler, source_addr, nbytes, a0, a1, a2, a3, a4, a5, a6, a7, a8, a9, a10, a11, a12, a13, a14, a15) \
        gasnetex_AMRequestMedium16(the_team, dest, handler, source_addr, nbytes, GASNETEX_LC_INIT, 0, (gasnet_handlerarg_t)a0, (gasnet_handlerarg_t)a1, (gasnet_handlerarg_t)a2, (gasnet_handlerarg_t)a3, (gasnet_handlerarg_t)a4, (gasnet_handlerarg_t)a5, (gasnet_handlerarg_t)a6, (gasnet_handlerarg_t)a7, (gasnet_handlerarg_t)a8, (gasnet_handlerarg_t)a9, (gasnet_handlerarg_t)a10, (gasnet_handlerarg_t)a11, (gasnet_handlerarg_t)a12, (gasnet_handlerarg_t)a13, (gasnet_handlerarg_t)a14, (gasnet_handlerarg_t)a15)
/* ------------------------------------------------------------------------------------ */
#define gasnet_AMRequestLong0(dest, handler, source_addr, nbytes, dest_addr) \
       gasnetex_AMRequestLong0(the_team, dest, handler, source_addr, nbytes, dest_addr, GASNETEX_LC_INIT, 0)
#define gasnet_AMRequestLong1(dest, handler, source_addr, nbytes, dest_addr, a0) \
       gasnetex_AMRequestLong1(the_team, dest, handler, source_addr, nbytes, dest_addr, GASNETEX_LC_INIT, 0, (gasnet_handlerarg_t)a0)
#define gasnet_AMRequestLong2(dest, handler, source_addr, nbytes, dest_addr, a0, a1) \
       gasnetex_AMRequestLong2(the_team, dest, handler, source_addr, nbytes, dest_addr, GASNETEX_LC_INIT, 0, (gasnet_handlerarg_t)a0, (gasnet_handlerarg_t)a1)
#define gasnet_AMRequestLong3(dest, handler, source_addr, nbytes, dest_addr, a0, a1, a2) \
       gasnetex_AMRequestLong3(the_team, dest, handler, source_addr, nbytes, dest_addr, GASNETEX_LC_INIT, 0, (gasnet_handlerarg_t)a0, (gasnet_handlerarg_t)a1, (gasnet_handlerarg_t)a2)
#define gasnet_AMRequestLong4(dest, handler, source_addr, nbytes, dest_addr, a0, a1, a2, a3) \
       gasnetex_AMRequestLong4(the_team, dest, handler, source_addr, nbytes, dest_addr, GASNETEX_LC_INIT, 0, (gasnet_handlerarg_t)a0, (gasnet_handlerarg_t)a1, (gasnet_handlerarg_t)a2, (gasnet_handlerarg_t)a3)

#define gasnet_AMRequestLong5(dest, handler, source_addr, nbytes, dest_addr, a0, a1, a2, a3, a4) \
       gasnetex_AMRequestLong5(the_team, dest, handler, source_addr, nbytes, dest_addr, GASNETEX_LC_INIT, 0, (gasnet_handlerarg_t)a0, (gasnet_handlerarg_t)a1, (gasnet_handlerarg_t)a2, (gasnet_handlerarg_t)a3, (gasnet_handlerarg_t)a4)
#define gasnet_AMRequestLong6(dest, handler, source_addr, nbytes, dest_addr, a0, a1, a2, a3, a4, a5) \
       gasnetex_AMRequestLong6(the_team, dest, handler, source_addr, nbytes, dest_addr, GASNETEX_LC_INIT, 0, (gasnet_handlerarg_t)a0, (gasnet_handlerarg_t)a1, (gasnet_handlerarg_t)a2, (gasnet_handlerarg_t)a3, (gasnet_handlerarg_t)a4, (gasnet_handlerarg_t)a5)
#define gasnet_AMRequestLong7(dest, handler, source_addr, nbytes, dest_addr, a0, a1, a2, a3, a4, a5, a6) \
       gasnetex_AMRequestLong7(the_team, dest, handler, source_addr, nbytes, dest_addr, GASNETEX_LC_INIT, 0, (gasnet_handlerarg_t)a0, (gasnet_handlerarg_t)a1, (gasnet_handlerarg_t)a2, (gasnet_handlerarg_t)a3, (gasnet_handlerarg_t)a4, (gasnet_handlerarg_t)a5, (gasnet_handlerarg_t)a6)
#define gasnet_AMRequestLong8(dest, handler, source_addr, nbytes, dest_addr, a0, a1, a2, a3, a4, a5, a6, a7) \
       gasnetex_AMRequestLong8(the_team, dest, handler, source_addr, nbytes, dest_addr, GASNETEX_LC_INIT, 0, (gasnet_handlerarg_t)a0, (gasnet_handlerarg_t)a1, (gasnet_handlerarg_t)a2, (gasnet_handlerarg_t)a3, (gasnet_handlerarg_t)a4, (gasnet_handlerarg_t)a5, (gasnet_handlerarg_t)a6, (gasnet_handlerarg_t)a7)

#define gasnet_AMRequestLong9( dest, handler, source_addr, nbytes, dest_addr, a0, a1, a2, a3, a4, a5, a6, a7, a8 ) \
        gasnetex_AMRequestLong9(the_team, dest, handler, source_addr, nbytes, dest_addr, GASNETEX_LC_INIT, 0, (gasnet_handlerarg_t)a0, (gasnet_handlerarg_t)a1, (gasnet_handlerarg_t)a2, (gasnet_handlerarg_t)a3, (gasnet_handlerarg_t)a4, (gasnet_handlerarg_t)a5, (gasnet_handlerarg_t)a6, (gasnet_handlerarg_t)a7, (gasnet_handlerarg_t)a8)
#define gasnet_AMRequestLong10(dest, handler, source_addr, nbytes, dest_addr, a0, a1, a2, a3, a4, a5, a6, a7, a8, a9) \
        gasnetex_AMRequestLong10(the_team, dest, handler, source_addr, nbytes, dest_addr, GASNETEX_LC_INIT, 0, (gasnet_handlerarg_t)a0, (gasnet_handlerarg_t)a1, (gasnet_handlerarg_t)a2, (gasnet_handlerarg_t)a3, (gasnet_handlerarg_t)a4, (gasnet_handlerarg_t)a5, (gasnet_handlerarg_t)a6, (gasnet_handlerarg_t)a7, (gasnet_handlerarg_t)a8, (gasnet_handlerarg_t)a9)
#define gasnet_AMRequestLong11(dest, handler, source_addr, nbytes, dest_addr, a0, a1, a2, a3, a4, a5, a6, a7, a8, a9, a10) \
        gasnetex_AMRequestLong11(the_team, dest, handler, source_addr, nbytes, dest_addr, GASNETEX_LC_INIT, 0, (gasnet_handlerarg_t)a0, (gasnet_handlerarg_t)a1, (gasnet_handlerarg_t)a2, (gasnet_handlerarg_t)a3, (gasnet_handlerarg_t)a4, (gasnet_handlerarg_t)a5, (gasnet_handlerarg_t)a6, (gasnet_handlerarg_t)a7, (gasnet_handlerarg_t)a8, (gasnet_handlerarg_t)a9, (gasnet_handlerarg_t)a10)
#define gasnet_AMRequestLong12(dest, handler, source_addr, nbytes, dest_addr, a0, a1, a2, a3, a4, a5, a6, a7, a8, a9, a10, a11) \
        gasnetex_AMRequestLong12(the_team, dest, handler, source_addr, nbytes, dest_addr, GASNETEX_LC_INIT, 0, (gasnet_handlerarg_t)a0, (gasnet_handlerarg_t)a1, (gasnet_handlerarg_t)a2, (gasnet_handlerarg_t)a3, (gasnet_handlerarg_t)a4, (gasnet_handlerarg_t)a5, (gasnet_handlerarg_t)a6, (gasnet_handlerarg_t)a7, (gasnet_handlerarg_t)a8, (gasnet_handlerarg_t)a9, (gasnet_handlerarg_t)a10, (gasnet_handlerarg_t)a11)

#define gasnet_AMRequestLong13(dest, handler, source_addr, nbytes, dest_addr, a0, a1, a2, a3, a4, a5, a6, a7, a8, a9, a10, a11, a12) \
        gasnetex_AMRequestLong13(the_team, dest, handler, source_addr, nbytes, dest_addr, GASNETEX_LC_INIT, 0, (gasnet_handlerarg_t)a0, (gasnet_handlerarg_t)a1, (gasnet_handlerarg_t)a2, (gasnet_handlerarg_t)a3, (gasnet_handlerarg_t)a4, (gasnet_handlerarg_t)a5, (gasnet_handlerarg_t)a6, (gasnet_handlerarg_t)a7, (gasnet_handlerarg_t)a8, (gasnet_handlerarg_t)a9, (gasnet_handlerarg_t)a10, (gasnet_handlerarg_t)a11, (gasnet_handlerarg_t)a12)
#define gasnet_AMRequestLong14(dest, handler, source_addr, nbytes, dest_addr, a0, a1, a2, a3, a4, a5, a6, a7, a8, a9, a10, a11, a12, a13) \
        gasnetex_AMRequestLong14(the_team, dest, handler, source_addr, nbytes, dest_addr, GASNETEX_LC_INIT, 0, (gasnet_handlerarg_t)a0, (gasnet_handlerarg_t)a1, (gasnet_handlerarg_t)a2, (gasnet_handlerarg_t)a3, (gasnet_handlerarg_t)a4, (gasnet_handlerarg_t)a5, (gasnet_handlerarg_t)a6, (gasnet_handlerarg_t)a7, (gasnet_handlerarg_t)a8, (gasnet_handlerarg_t)a9, (gasnet_handlerarg_t)a10, (gasnet_handlerarg_t)a11, (gasnet_handlerarg_t)a12, (gasnet_handlerarg_t)a13)
#define gasnet_AMRequestLong15(dest, handler, source_addr, nbytes, dest_addr, a0, a1, a2, a3, a4, a5, a6, a7, a8, a9, a10, a11, a12, a13, a14) \
        gasnetex_AMRequestLong15(the_team, dest, handler, source_addr, nbytes, dest_addr, GASNETEX_LC_INIT, 0, (gasnet_handlerarg_t)a0, (gasnet_handlerarg_t)a1, (gasnet_handlerarg_t)a2, (gasnet_handlerarg_t)a3, (gasnet_handlerarg_t)a4, (gasnet_handlerarg_t)a5, (gasnet_handlerarg_t)a6, (gasnet_handlerarg_t)a7, (gasnet_handlerarg_t)a8, (gasnet_handlerarg_t)a9, (gasnet_handlerarg_t)a10, (gasnet_handlerarg_t)a11, (gasnet_handlerarg_t)a12, (gasnet_handlerarg_t)a13, (gasnet_handlerarg_t)a14)
#define gasnet_AMRequestLong16(dest, handler, source_addr, nbytes, dest_addr, a0, a1, a2, a3, a4, a5, a6, a7, a8, a9, a10, a11, a12, a13, a14, a15) \
        gasnetex_AMRequestLong16(the_team, dest, handler, source_addr, nbytes, dest_addr, GASNETEX_LC_INIT, 0, (gasnet_handlerarg_t)a0, (gasnet_handlerarg_t)a1, (gasnet_handlerarg_t)a2, (gasnet_handlerarg_t)a3, (gasnet_handlerarg_t)a4, (gasnet_handlerarg_t)a5, (gasnet_handlerarg_t)a6, (gasnet_handlerarg_t)a7, (gasnet_handlerarg_t)a8, (gasnet_handlerarg_t)a9, (gasnet_handlerarg_t)a10, (gasnet_handlerarg_t)a11, (gasnet_handlerarg_t)a12, (gasnet_handlerarg_t)a13, (gasnet_handlerarg_t)a14, (gasnet_handlerarg_t)a15)
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
/* Blocking Put and Get */

#define gasnet_put(node,dest,src,nbytes) \
                gasnetex_Put(the_team,node,dest,src,nbytes,0)
#define gasnet_put_bulk(node,dest,src,nbytes) \
                gasnetex_Put(the_team,node,dest,src,nbytes,0)
#define gasnet_get(dest,node,src,nbytes) \
                gasnetex_Get(the_team,dest,node,src,nbytes,0)
#define gasnet_get_bulk(dest,node,src,nbytes) \
                gasnetex_Get(the_team,dest,node,src,nbytes,0)

/* ------------------------------------------------------------------------------------ */
/* Implicit-handle non-blocking Put and Get */
#define gasnet_put_nbi(node,dest,src,nbytes) \
                gasnetex_Put_nbi(the_team,node,dest,src,nbytes,GASNETEX_LC_INIT,0)
#define gasnet_put_nbi_bulk(node,dest,src,nbytes) \
                gasnetex_Put_nbi(the_team,node,dest,src,nbytes,GASNETEX_LC_SYNC,0)
#define gasnet_get_nbi(dest,node,src,nbytes) \
                gasnetex_Get_nbi(the_team,dest,node,src,nbytes,0)
#define gasnet_get_nbi_bulk(dest,node,src,nbytes) \
                gasnetex_Get_nbi(the_team,dest,node,src,nbytes,0)

/* ------------------------------------------------------------------------------------ */
/* Explicit-handle non-blocking Put and Get */
#define gasnet_put_nb(node,dest,src,nbytes) \
                gasnetex_Put_nb(the_team,node,dest,src,nbytes,GASNETEX_LC_INIT,0)
#define gasnet_put_nb_bulk(node,dest,src,nbytes) \
                gasnetex_Put_nb(the_team,node,dest,src,nbytes,GASNETEX_LC_SYNC,0)
#define gasnet_get_nb(dest,node,src,nbytes) \
                gasnetex_Get_nb(the_team,dest,node,src,nbytes,0)
#define gasnet_get_nb_bulk(dest,node,src,nbytes) \
                gasnetex_Get_nb(the_team,dest,node,src,nbytes,0)


GASNETI_END_EXTERNC

#endif

/*   $Source: bitbucket.org:berkeleylab/gasnet.git/tests/testratomic.c $
 * Description: GASNet remote atomics correctness tests
 * Copyright (c) 2017, The Regents of the University of California
 * Terms of use are as specified in license.txt
 */

#include <gasnetex.h>
#include <test.h>

static gex_Client_t  myclient;
static gex_EP_t      myep;
static gex_TM_t      myteam;
static gex_Segment_t mysegment;

#if GASNET_CONDUIT_SMP
int main(int argc, char **argv) {
  GASNET_Safe(gex_Client_Init(&myclient, &myep, &myteam, "testratomic", &argc, &argv, 0));
  MSG0("WARNING: smp-conduit does not support remote atomics");
  gasnet_exit(0);
  return 0;
}
#else

#include <gasnet_ratomic.h>

#include <stdint.h>
#include <float.h>

static gex_Rank_t myrank;
static gex_Rank_t numranks;

static int peer;
static void *peerseg;
static int iters = 0;

/* Hidden state for error reporting and recovery */
static const char* subtest = "N/A";
#define SUBTEST(_name) subtest = _name
static int prev_fail = 0;
static int failures = 0;

/* Blocking atomic via either NB or NBI (chosen at random) */
/* With or without IMMEDIATE (also at random) */
/* Note that some arguments and variables are hard-coded */
#define _TEST_ROP(_tcode, _result_p, _opcode, _op1, _op2) do { \
    gex_Flags_t flags = TEST_RAND_ONEIN(2) ? GEX_FLAG_IMMEDIATE : 0;                       \
    if (TEST_RAND_ONEIN(2)) {                                                              \
      gex_Event_t ev;                                                                      \
      while (GEX_EVENT_NO_OP ==                                                            \
             (ev = gex_AD_OpNB_##_tcode(ad,_result_p,peer,peerseg,_opcode,_op1,_op2,flags))) {\
        assert_always(flags & GEX_FLAG_IMMEDIATE);                                         \
        flags &= ~GEX_FLAG_IMMEDIATE;                                                      \
      }                                                                                    \
      gex_Event_Wait(ev);                                                                  \
    } else {                                                                               \
      while (gex_AD_OpNBI_##_tcode(ad,_result_p,peer,peerseg,_opcode,_op1,_op2,flags)) {   \
        assert_always(flags & GEX_FLAG_IMMEDIATE);                                         \
        flags &= ~GEX_FLAG_IMMEDIATE;                                                      \
      }                                                                                    \
      gex_NBI_Wait(GEX_EC_ALL,0);                                                          \
    }                                                                                      \
  } while (0)

// Update mirror value
// Also store remotely if (and only if) previous check failed
#define _TEST_ROP_MIRROR(_tcode, _newval) do { \
    mirror = _newval;                          \
    if_pf (prev_fail) {                        \
      gex_Event_Wait(gex_AD_OpNB_##_tcode(ad,NULL,peer,peerseg,GEX_OP_SET,mirror,0,0)); \
    }                                          \
    prev_fail = 0;                             \
  } while (0)

/* Test an atomic op (fetching and non-fetching variants */
/* Update the expected value and stores it remotely if the mot recent fetching op failed validation */
#define TEST_ROP(_tcode, _opcode, _op1, _op2, _newval)  do { \
        if (! (_opcode & ops)) break;                 \
        _TEST_ROP(_tcode, NULL, _opcode, _op1, _op2); \
	_TEST_ROP_MIRROR(_tcode, _newval);            \
  } while (0)
#define TEST_ROP_FETCH(_tcode, _opcode, _op1, _op2, _newval) do { \
    if (! (_opcode & ops)) break;                        \
    _TEST_ROP(_tcode, &fetch, _opcode, _op1, _op2);      \
    prev_fail = (fetch != mirror);                       \
    if_pf (prev_fail) {                                  \
      ++failures;                                        \
      static int once = 0;                               \
      if (!once) {                                       \
        ERR("Valued fetched by \"%s\" did not match expected value "  \
            "(got %d, want %d)\n", subtest, (int)fetch, (int)mirror); \
        once = 1;                                        \
      }                                                  \
    }                                                    \
    _TEST_ROP_MIRROR(_tcode, _newval);                   \
  } while (0)



/* Randomized testing of atomic ops */
#define TEST_RAND_DECL(_tcode, _type, _isint) \
void test_rand_##_tcode(gex_AD_t ad, int lo, int hi, const char *msg) {\
  _type mirror = 0;                                           \
  gex_OP_t ops = gex_AD_QueryOps(ad);                         \
  MSG0("Randomized remote atomic ops test for type " #_type   \
       " and operation set 0x%x%s", (unsigned int)ops, msg);  \
  for (int i = 0; i < iters; ++i) {                           \
    _type unused = (_type)TEST_RAND(lo,hi); /* garbage */     \
    _type fetch, x, y;                                        \
    /* first few iterations are NON-random (0, 1, 2) */       \
    x = (i <= 2) ? (_type)i : (_type)TEST_RAND(lo,hi);        \
    SUBTEST("SET(x)");                                        \
      TEST_ROP(_tcode, GEX_OP_SET, x, unused, x);             \
    SUBTEST("GET(x)");                                        \
      TEST_ROP_FETCH(_tcode, GEX_OP_GET, unused, unused, mirror); \
    SUBTEST("SET(0)");                                        \
      TEST_ROP(_tcode, GEX_OP_SET, 0, unused, 0);             \
    SUBTEST("GET(0)");                                        \
      TEST_ROP_FETCH(_tcode, GEX_OP_GET, unused, unused, mirror); \
    SUBTEST("FINC()");                                        \
      TEST_ROP_FETCH(_tcode, GEX_OP_FINC, unused, unused, mirror + 1); \
    SUBTEST("INC()");                                         \
      TEST_ROP(_tcode, GEX_OP_INC, unused, unused, mirror + 1); \
    SUBTEST("FADD(x+1)");                                     \
      TEST_ROP_FETCH(_tcode, GEX_OP_FADD, (x+1), unused, mirror + x + 1); \
    SUBTEST("ADD(x+1)");                                      \
      TEST_ROP(_tcode, GEX_OP_ADD, (x+1), unused, mirror + x + 1); \
    SUBTEST("FDEC()");                                        \
      TEST_ROP_FETCH(_tcode, GEX_OP_FDEC, unused, unused, mirror - 1); \
    SUBTEST("DEC()");                                         \
      TEST_ROP(_tcode, GEX_OP_DEC, unused, unused, mirror - 1); \
    SUBTEST("FSUB(x)");                                       \
      TEST_ROP_FETCH(_tcode, GEX_OP_FSUB, x, unused, mirror - x); \
    SUBTEST("SUB(x)");                                        \
      TEST_ROP(_tcode, GEX_OP_SUB, x, unused, mirror - x);    \
    SUBTEST("FMULT(x)");                                      \
      TEST_ROP_FETCH(_tcode, GEX_OP_FMULT, x, unused, mirror * x); \
    SUBTEST("MULT(x)");                                       \
      TEST_ROP(_tcode, GEX_OP_MULT, x, unused, mirror * x);   \
    SUBTEST("SWAP(x)");                                       \
      TEST_ROP_FETCH(_tcode, GEX_OP_SWAP, x, unused, x);      \
    SUBTEST("CSWAP(mirror,mirror) - PASS");                   \
      TEST_ROP_FETCH(_tcode, GEX_OP_CSWAP, mirror, mirror, mirror); \
    SUBTEST("CSWAP(mirror+1,0) - FAIL");                      \
      TEST_ROP_FETCH(_tcode, GEX_OP_CSWAP, mirror+1, 0, mirror); \
    SUBTEST("CSWAP(mirror,random) - PASS");                   \
      y = (_type)TEST_RAND(lo,hi);                            \
      TEST_ROP_FETCH(_tcode, GEX_OP_CSWAP, mirror, y, y);     \
    SUBTEST("CSWAP(random,random) - FAIL");                   \
      do { y = (_type)TEST_RAND(lo,hi); } while (y == mirror);\
      TEST_ROP_FETCH(_tcode, GEX_OP_CSWAP, y, y, mirror);     \
    SUBTEST("GET(cswap)");                                    \
      TEST_ROP_FETCH(_tcode, GEX_OP_GET, unused, unused, mirror); \
    SUBTEST("MIN(random)");                                   \
      y = (_type)TEST_RAND(lo,hi);                            \
      y = TEST_RAND_ONEIN(2) ? y : ((_type)-1) * y;           \
      TEST_ROP(_tcode, GEX_OP_MIN, y, unused, MIN(mirror,y)); \
    SUBTEST("FMIN(random)");                                  \
      y = (_type)TEST_RAND(lo,hi);                            \
      y = TEST_RAND_ONEIN(2) ? y : ((_type)-1) * y;           \
      TEST_ROP_FETCH(_tcode, GEX_OP_FMIN, y, unused, MIN(mirror,y)); \
    SUBTEST("MAX(random)");                                   \
      y = (_type)TEST_RAND(lo,hi);                            \
      y = TEST_RAND_ONEIN(2) ? y : ((_type)-1) * y;           \
      TEST_ROP(_tcode, GEX_OP_MAX, y, unused, MAX(mirror,y)); \
    SUBTEST("FMAX(random)");                                  \
      y = (_type)TEST_RAND(lo,hi);                            \
      y = TEST_RAND_ONEIN(2) ? y : ((_type)-1) * y;           \
      TEST_ROP_FETCH(_tcode, GEX_OP_FMAX, y, unused, MAX(mirror,y)); \
    TEST_RAND_BITS##_isint(_tcode,_type)                      \
    SUBTEST("GET(final)");                                    \
      TEST_ROP_FETCH(_tcode, GEX_OP_GET, unused, unused, mirror); \
  }                                                           \
  if (failures) {                                             \
    MSG("  Total: failures %d for type " #_type, failures);   \
    failures = 0;                                             \
  }                                                           \
}
#define TEST_RAND_BITS0(_tcode,_type) /*empty*/
#define TEST_RAND_BITS1(_tcode,_type) \
    SUBTEST("AND(random)");                                   \
      y = (_type)TEST_RAND(lo,hi);                            \
      TEST_ROP(_tcode, GEX_OP_AND, y, unused, mirror & y);    \
    SUBTEST("FAND(random)");                                  \
      y = (_type)TEST_RAND(lo,hi);                            \
      TEST_ROP_FETCH(_tcode, GEX_OP_FAND, y, unused, mirror & y); \
    SUBTEST("OR(random)");                                    \
      y = (_type)TEST_RAND(lo,hi);                            \
      TEST_ROP(_tcode, GEX_OP_OR, y, unused, mirror | y);     \
    SUBTEST("FOR(random)");                                   \
      y = (_type)TEST_RAND(lo,hi);                            \
      TEST_ROP_FETCH(_tcode, GEX_OP_FOR, y, unused, mirror | y);  \
    SUBTEST("XOR(random)");                                   \
      y = (_type)TEST_RAND(lo,hi);                            \
      TEST_ROP(_tcode, GEX_OP_XOR, y, unused, mirror ^ y);    \
    SUBTEST("FXOR(random)");                                  \
      y = (_type)TEST_RAND(lo,hi);                            \
      TEST_ROP_FETCH(_tcode, GEX_OP_FXOR, y, unused, mirror ^ y); \
//
TEST_RAND_DECL(U32, uint32_t, 1)
TEST_RAND_DECL(I32, int32_t,  1)
TEST_RAND_DECL(U64, uint64_t, 1)
TEST_RAND_DECL(I64, int64_t,  1)
TEST_RAND_DECL(FLT, float,    0)
TEST_RAND_DECL(DBL, double,   0)


/* Deterministic testing of AD_MY_* flags */
#define TEST_FLAGS_DECL(_tcode, _type, _isint) \
void test_flags_##_tcode(gex_AD_t ad) {                                      \
  gex_Event_t ev;                                                            \
  _type result;                                                              \
  MSG0("Remote atomic ops flags test for type " #_type);                     \
                                                                             \
  /* MY_RANK and MY_NEIGHBORHOOD applied to self */                          \
  ev = gex_AD_OpNB_##_tcode(ad,NULL,myrank,TEST_MYSEG(),GEX_OP_SET,          \
                            myrank,0,GEX_FLAG_AD_MY_RANK);                   \
  gex_Event_Wait(ev);                                                        \
  gex_AD_OpNBI_##_tcode(ad,&result,myrank,TEST_MYSEG(),GEX_OP_FADD,          \
                       myrank,0,GEX_FLAG_AD_MY_RANK);                        \
  gex_NBI_Wait(GEX_EC_RMW,0);                                                \
  assert_always(result == myrank);                                           \
  ev = gex_AD_OpNB_##_tcode(ad,&result,myrank,TEST_MYSEG(),GEX_OP_FADD,      \
                            myrank,0,GEX_FLAG_AD_MY_NEIGHBORHOOD);           \
  gex_Event_Wait(ev);                                                        \
  assert_always(result == 2*myrank);                                         \
  BARRIER();                                                                 \
                                                                             \
  /* MY_NEIGHBORHOOD applied to not-self-unless-no-other-valid-choice */     \
  gex_Rank_t nbr_rank;                                                       \
  void * nbr_addr;                                                           \
  { /* Find neighbor - possibly self */                                      \
    gex_NeighborhoodInfo_t *info;                                            \
    gex_Rank_t info_count, my_info_index;                                    \
    gex_System_QueryNeighborhoodInfo(&info, &info_count, &my_info_index);    \
    nbr_rank = info[(my_info_index + 1) % info_count].gex_jobrank;           \
    nbr_addr = TEST_SEG(nbr_rank);                                           \
    assert(nbr_addr);                                                        \
  }                                                                          \
  gex_AD_OpNBI_##_tcode(ad,&result,nbr_rank,nbr_addr,GEX_OP_FADD,            \
                        nbr_rank,0,GEX_FLAG_AD_MY_NEIGHBORHOOD);             \
  gex_NBI_Wait(GEX_EC_RMW,0);                                                \
  assert_always(result == 3*nbr_rank);                                       \
}
TEST_FLAGS_DECL(U32, uint32_t, 1)
TEST_FLAGS_DECL(I32, int32_t,  1)
TEST_FLAGS_DECL(U64, uint64_t, 1)
TEST_FLAGS_DECL(I64, int64_t,  1)
TEST_FLAGS_DECL(FLT, float,    0)
TEST_FLAGS_DECL(DBL, double,   0)


void doit(gex_DT_t dt) {
  gex_OP_t all_ops =
        GEX_OP_ADD  | GEX_OP_SUB  | GEX_OP_MULT  |
        GEX_OP_MIN  | GEX_OP_MAX  |
        GEX_OP_INC  | GEX_OP_DEC  |
        GEX_OP_FADD | GEX_OP_FSUB | GEX_OP_FMULT |
        GEX_OP_FMIN | GEX_OP_FMAX |
        GEX_OP_FINC | GEX_OP_FDEC |
        GEX_OP_SET  | GEX_OP_GET  |
        GEX_OP_SWAP | GEX_OP_CSWAP;
  if ((dt != GEX_DT_FLT) && (dt != GEX_DT_DBL)) {
    all_ops |=
        GEX_OP_AND  | GEX_OP_OR  | GEX_OP_XOR |
        GEX_OP_FAND | GEX_OP_FOR | GEX_OP_FXOR;
  }

  // Test of AD-specific flags
  {
    gex_AD_t ad;
    gex_AD_Create(&ad, myteam, dt, GEX_OP_SET | GEX_OP_FADD, 0);

    BARRIER();
    switch (dt) {
      case GEX_DT_U32:
        test_flags_U32(ad);
        break;
      case GEX_DT_I32:
        test_flags_I32(ad);
        break;
      case GEX_DT_U64:
        test_flags_U64(ad);
        break;
      case GEX_DT_I64:
        test_flags_I64(ad);
        break;
      case GEX_DT_FLT:
        test_flags_FLT(ad);
        break;
      case GEX_DT_DBL:
        test_flags_DBL(ad);
        break;
    }

    gex_AD_Destroy(ad);
  }

  // Range of random numbers
  // Chosen to exercise as many bits of each type as possible within constraints:
  // + lo and hi must be integers valid for use with TEST_RAND
  // + For x in [lo,hi] 2*x*x must be representable exactly in the target type
  //   (because 2*x*x is the max value reached by the series of arithmetic ops)
  int lo, hi;
  test_static_assert(FLT_RADIX == 2); // May fail on s390!!
  switch (dt) {
    case GEX_DT_U32:
      hi = (1<<15);
      lo = 0;
      break;
    case GEX_DT_I32:
      hi = (1<<15) - 1;
      lo = -hi;
      break;
    case GEX_DT_U64:
      hi = (1U<<31) - 2; // could be larger by +2 if not using TEST_RAND
      lo = 0;
      break;
    case GEX_DT_I64:
      hi = (1U<<30) - 1; // could be larger by *2 if not using TEST_RAND
      lo = -hi;
      break;
    case GEX_DT_FLT:
      hi = 1 << (FLT_MANT_DIG / 2);
      lo = -hi;
      break;
    case GEX_DT_DBL:
      hi = 1 << (DBL_MANT_DIG / 2);
      lo = -hi;
      break;
  }

  // TODO: deterministic tests of bitwise ops (see testtools for examples)
  // TODO: deterministic tests of known corner cases (if/when specified)
  //         Integer overflow behaviors
  //         Floating-point +/- zero behavior
  // TODO: randomized tests of important subsets
  //         SET/GET/CSWAP (universal)
  //         SET/GET/CSWAP/SWAP (MCS locks and Nemesis queues)
  //         SET/GET/FADD
  // TODO: concurrent tests of correctness

  // Randomized testing
  for (int subtest = 0; subtest < 2; ++subtest) {
    gex_OP_t ops = 0;
    const char *msg = "";
    switch (subtest) {
      case 0:  // Test all ops legal for the data type
        ops = all_ops;
        break;

      case 1: // Whitebox testing of conduits with native implementations
        msg = " (conduit-specialized ops)";
        ops = all_ops;
        // This logic reduces "ops" to the conduit's specialized subset
      #if GASNET_CONDUIT_ARIES
        // 1) No MULT for any type
        ops &= ~(GEX_OP_MULT | GEX_OP_FMULT);
        // 2) No MIN or MAX for the unsigned integer types
        if ((dt == GEX_DT_U32) || (dt == GEX_DT_U64)) {
          ops &= ~(GEX_OP_MIN | GEX_OP_FMIN | GEX_OP_MAX | GEX_OP_FMAX);
        }
        // 3) No ADD (or its derivatives) for double
        if (dt == GEX_DT_DBL) {
          ops &= ~(GEX_OP_ADD | GEX_OP_FADD | GEX_OP_SUB | GEX_OP_FSUB |
                   GEX_OP_INC | GEX_OP_FINC | GEX_OP_DEC | GEX_OP_FDEC);
        }
      #else
        continue;  // No whitebox testing
      #endif
    }

    gex_AD_t ad;
    gex_AD_Create(&ad, myteam, dt, ops, 0);

    BARRIER();
    switch (dt) {
      case GEX_DT_U32:
        test_rand_U32(ad,lo,hi,msg);
        break;
      case GEX_DT_I32:
        test_rand_I32(ad,lo,hi,msg);
        break;
      case GEX_DT_U64:
        test_rand_U64(ad,lo,hi,msg);
        break;
      case GEX_DT_I64:
        test_rand_I64(ad,lo,hi,msg);
        break;
      case GEX_DT_FLT:
        test_rand_FLT(ad,lo,hi,msg);
        break;
      case GEX_DT_DBL:
        test_rand_DBL(ad,lo,hi,msg);
        break;
    }
    BARRIER();

    gex_AD_Destroy(ad);
  }
}

/* ------------------------------------------------------------------------------------ */

int main(int argc, char **argv) {
  GASNET_Safe(gex_Client_Init(&myclient, &myep, &myteam, "testratomic", &argc, &argv, 0));

  int arg = 1;
  if (argc > arg) { iters = atoi(argv[arg]); ++arg; }
  if (!iters) iters = 1000;

  TEST_SRAND((int)TIME());

  GASNET_Safe(gex_Segment_Attach(&mysegment, myteam, TEST_SEGSZ_REQUEST));

  test_init("testratomic",0,"(iters)");

  MSG0("Running %i iterations of remote atomics tests.\n", iters);

  myrank   = gex_TM_QueryRank(myteam);
  numranks = gex_TM_QuerySize(myteam);
  peer = (myrank + 1) % numranks;
  peerseg = TEST_SEG(peer);

  doit(GEX_DT_U32);
  doit(GEX_DT_I32);
  doit(GEX_DT_U64);
  doit(GEX_DT_I64);
  doit(GEX_DT_FLT);
  doit(GEX_DT_DBL);

  MSG("done.");

  gasnet_exit(0);
  return 0;
}

#endif // ! SMP conduit

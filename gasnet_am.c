/*   $Source: bitbucket.org:berkeleylab/gasnet.git/gasnet_am.c $
 * Description: GASNet conduit-independent code for Active Messages
 * Copyright 2002, Dan Bonachea <bonachea@cs.berkeley.edu>
 * Copyright 2018, The Regents of the University of California
 * Terms of use are as specified in license.txt
 */

#include <gasnet_internal.h>
#include <gasnet_am.h>

/* ------------------------------------------------------------------------------------ */
extern void gasneti_defaultAMHandler(gex_Token_t token) {
  gex_Token_Info_t info;
  info.gex_srcrank = GEX_RANK_INVALID; // to print -1 if query were to fail
  gex_Token_Info(token, &info, GEX_TI_SRCRANK);
  gex_Rank_t srcnode = info.gex_srcrank;
  gasneti_fatalerror("GASNet node %i/%i received an AM message from node %i for a handler index "
                     "with no associated AM handler function registered", 
                     (int)gasneti_mynode, (int)gasneti_nodes, (int)srcnode);
}
/* ------------------------------------------------------------------------------------ */

// Validate a handler table prior to registration
static void gasneti_am_validate(
                        const gex_AM_Entry_t *table,
                        int numentries)
{
  if (!numentries) return;

  // Internally-constructed legacy table should be all-or-nothing.
  if (table[0].gex_nargs == GASNETI_HANDLER_NARGS_UNK ||
      table[0].gex_flags & GASNETI_FLAG_INIT_LEGACY) {
    for (int i = 0; i < numentries; ++i) {
       gasneti_assert_always(table[i].gex_nargs == GASNETI_HANDLER_NARGS_UNK);
       gasneti_assert_always(table[i].gex_flags == (GASNETI_FLAG_AM_ANY | GASNETI_FLAG_INIT_LEGACY));
    }
    return;
  }

  // Normal tables have several rules to check:
  for (int i = 0; i < numentries; ++i) {
    int idx = table[i].gex_index;

    if_pf (table[i].gex_nargs > gex_AM_MaxArgs()) {
      gasneti_fatalerror("AM Handler table entry %d: invalid gex_nargs: %d (Max %d)",
                         i, (int)table[i].gex_nargs, (int)gex_AM_MaxArgs());
    }

    if_pf (0 == (table[i].gex_flags & (GEX_FLAG_AM_REQUEST|GEX_FLAG_AM_REPLY))) {
      gasneti_fatalerror("AM Handler table entry %d(idx=%d): invalid gex_flags: contains neither GEX_FLAG_AM_REQUEST nor GEX_FLAG_AM_REPLY", i, idx);
    }

    gex_Flags_t category = table[i].gex_flags & (GEX_FLAG_AM_SHORT|GEX_FLAG_AM_MEDIUM|GEX_FLAG_AM_LONG);
    const char *cat_msg = NULL;
    switch (category) {
    case 0:
      cat_msg = "none of GEX_FLAG_AM_SHORT, GEX_FLAG_AM_MEDIUM, or GEX_FLAG_AM_LONG";
      break;
    case GEX_FLAG_AM_SHORT|GEX_FLAG_AM_MEDIUM|GEX_FLAG_AM_LONG:
      cat_msg = "invalid combination (GEX_FLAG_AM_SHORT | GEX_FLAG_AM_MEDIUM | GEX_FLAG_AM_LONG)";
      break;
    case GEX_FLAG_AM_SHORT|GEX_FLAG_AM_MEDIUM:
      cat_msg = "invalid combination (GEX_FLAG_AM_SHORT | GEX_FLAG_AM_MEDIUM )";
      break;
    case GEX_FLAG_AM_SHORT|GEX_FLAG_AM_LONG:
      cat_msg = "invalid combination (GEX_FLAG_AM_SHORT | GEX_FLAG_AM_LONG)";
      break;
    }
    if_pf (cat_msg) {
      gasneti_fatalerror("AM Handler table entry %d(idx=%d): invalid gex_flags: contains %s", i, idx, cat_msg);
    }
  }
}

#if GASNETC_AMREGISTER
  /* Use a conduit-specific hook at registration */
  extern int gasnetc_amregister(gex_AM_Index_t, gex_AM_Entry_t *);
#endif

// Register handlers in the range [lowlimit,highlimit)
extern int gasneti_amregister( gex_AM_Entry_t *output,
                               gex_AM_Entry_t *input, int numentries,
                               int lowlimit, int highlimit,
                               int dontcare, int *numregistered) {
  int i;
  *numregistered = 0;

  gasneti_am_validate(input, numentries);

  for (i = 0; i < numentries; i++) {
    int newindex;

    if ((input[i].gex_index == 0 && !dontcare) ||
        (input[i].gex_index && dontcare)) continue;
    else if (input[i].gex_index) newindex = input[i].gex_index;
    else { /* deterministic assignment of dontcare indexes from top down */
      for (newindex = highlimit-1; newindex >= lowlimit; newindex--) {
        if (!output[newindex].gex_index) break; // 0 index marks free entry
      }
      if (newindex < lowlimit) {
        char s[255];
        snprintf(s, sizeof(s), "Too many handlers. (limit=%i)", highlimit - lowlimit);
        GASNETI_RETURN_ERRR(BAD_ARG, s);
      }
    }

    /*  ensure handlers fall into the proper range of pre-assigned values */
    if (newindex < lowlimit || newindex >= highlimit) {
      char s[255];
      snprintf(s, sizeof(s), "handler index (%i) out of range [%i..%i)", newindex, lowlimit, highlimit);
      GASNETI_RETURN_ERRR(BAD_ARG, s);
    }

    /* discover duplicates */
    if (output[newindex].gex_index) // entry is taken
      GASNETI_RETURN_ERRR(BAD_ARG, "handler index not unique");

    /* register a single handler with conduit-specifc hook, if any */
  #if GASNETC_AMREGISTER
    int rc = gasnetc_amregister((gex_AM_Index_t)newindex, &input[i]);
    if (GASNET_OK != rc) return rc;
  #endif

    /* The check below for !input[i].index is redundant and present
     * only to defeat the over-aggressive optimizer in pathcc 2.1
     */
    if (dontcare && !input[i].gex_index) input[i].gex_index = newindex;

    /* Install the entire table entry */
    gasneti_assert(! output[newindex].gex_index);
    output[newindex] = input[i];

    (*numregistered)++;
  }
  return GASNET_OK;
}

// Register client handlers
extern int gasneti_amregister_client(
                        gex_AM_Entry_t *output,
                        gex_AM_Entry_t *input,
                        size_t numentries)
{
  if_pf (numentries == 0) return GASNET_OK;
  if_pf (numentries > GASNETC_MAX_NUMHANDLERS - GEX_AM_INDEX_BASE) 
      GASNETI_RETURN_ERRR(BAD_ARG,"Tried to register too many handlers");
  if_pf (input == NULL)
      GASNETI_RETURN_ERRR(BAD_ARG,"Invalid AM handler table");

  /*  first pass - assign all fixed-index handlers */
  int numreg1 = 0;
  if (gasneti_amregister(output, input, numentries,
                         GASNETI_CLIENT_HANDLER_BASE, GASNETC_MAX_NUMHANDLERS,
                         0, &numreg1) != GASNET_OK) {
      GASNETI_RETURN_ERRR(RESOURCE,"Error registering fixed-index client handlers");
  }

  /*  second pass - fill in dontcare-index handlers */
  int numreg2 = 0;
  if (gasneti_amregister(output, input, numentries,
                         GASNETI_CLIENT_HANDLER_BASE, GASNETC_MAX_NUMHANDLERS,
                         1, &numreg2) != GASNET_OK) {
      GASNETI_RETURN_ERRR(RESOURCE,"Error registering variable-index client handlers");
  }

  gasneti_assert(numreg1 + numreg2 == numentries);

  return GASNET_OK;
}

// Wrapper to provide continued support for GASNet-1 legacy handler tables,
// such as through gasnet_attach().  Only supports the clients's index range.
// TODO-EX: should be absorbed into an eventual conduit-indep gasnet_attach()
extern int gasneti_amregister_legacy( gex_AM_Entry_t *output,
                                      gasnet_handlerentry_t *table, int numentries) {

  if_pf (numentries == 0) return GASNET_OK;
  if_pf (numentries > GASNETC_MAX_NUMHANDLERS - GEX_AM_INDEX_BASE) 
      GASNETI_RETURN_ERRR(BAD_ARG,"Tried to register too many handlers");
  if_pf (numentries < 0) 
      GASNETI_RETURN_ERRR(BAD_ARG,"Invalid AM handler table size");
  if_pf (table == NULL)
      GASNETI_RETURN_ERRR(BAD_ARG,"Invalid AM handler table");

  /* create temporary ex-compatible table */
  gex_AM_Entry_t *extable = gasneti_calloc(numentries, sizeof(gex_AM_Entry_t));
  for (int i = 0; i < numentries; ++i) {
    extable[i].gex_index = table[i].index;
    extable[i].gex_fnptr = table[i].fnptr;
    extable[i].gex_nargs = GASNETI_HANDLER_NARGS_UNK;
    extable[i].gex_flags = GASNETI_FLAG_AM_ANY | GASNETI_FLAG_INIT_LEGACY;
  }

  /* register */
  if (gasneti_amregister_client(output, extable, numentries) != GASNET_OK) {
      gasneti_free(extable);
      GASNETI_RETURN_ERRR(RESOURCE,"Error registering client handlers");
  }

  /* copy back from temporary ex-compatible table */
  for (int i = 0; i < numentries; ++i) {
    table[i].index = extable[i].gex_index;
  }
  gasneti_free(extable);

  return GASNET_OK;
}

// Initialize a caller-allocated handler table
extern int gasneti_amtbl_init(gex_AM_Entry_t *output) {
  static const char *fnname = "gasneti_defaultAMHandler";
  for (int i = 0; i < GASNETC_MAX_NUMHANDLERS; i++) {
    output[i].gex_index = 0; // marks an unused entry
    output[i].gex_nargs = GASNETI_HANDLER_NARGS_UNK;
    output[i].gex_flags = GASNETI_FLAG_AM_ANY;
    output[i].gex_fnptr = gasneti_defaultAMHandler;
    output[i].gex_cdata = NULL;
    output[i].gex_name  = fnname;
  }
  return GASNET_OK;
}

#if GASNET_DEBUG
// Validate call to a handler
extern void gasneti_amtbl_check(const gex_AM_Entry_t *entry, int nargs,
                                gasneti_category_t category, int isReq) {
  char buf[128] = {'\0'};
  const char *msg = NULL;
  if ((entry->gex_nargs != nargs) && (entry->gex_nargs != GASNETI_HANDLER_NARGS_UNK)) {
    snprintf(buf, sizeof(buf), "registered with nargs=%d but called with %d", entry->gex_nargs, nargs);
    msg = buf;
  } else if (isReq && !(entry->gex_flags & GEX_FLAG_AM_REQUEST)) {
    msg = "invoked as a Request handler, but not registered with GEX_FLAG_AM_REQUEST";
  } else if (!isReq && !(entry->gex_flags & GEX_FLAG_AM_REPLY)) {
    msg = "invoked as a Reply handler, but not registered with GEX_FLAG_AM_REPLY";
  } else if (category == gasneti_Short && !(entry->gex_flags & GEX_FLAG_AM_SHORT)) {
    msg = "invoked as a Short handler, but not registered with GEX_FLAG_AM_SHORT";
  } else if (category == gasneti_Medium && !(entry->gex_flags & GEX_FLAG_AM_MEDIUM)) {
    msg = "invoked as a Medium handler, but not registered with GEX_FLAG_AM_MEDIUM";
  } else if (category == gasneti_Long && !(entry->gex_flags & GEX_FLAG_AM_LONG)) {
    msg = "invoked as a Long handler, but not registered with GEX_FLAG_AM_LONG";
  }
  if (msg) {
    char fnaddr[32];
    const char *fnname = entry->gex_name;
    if (!fnname) {
      (void) snprintf(fnaddr, sizeof(fnaddr), "%p", 
                     *(void**)&entry->gex_fnptr); // level of indirection avoids -pedantic warning
      fnname = fnaddr;
    }
    gasneti_fatalerror("AM handler %d (%s) %s", entry->gex_index, fnname, msg);
  }
}
#endif

/* ------------------------------------------------------------------------------------ */
#if GASNET_DEBUG
// Post processing of gex_Token_Info() results
extern gex_TI_t gasneti_token_info_return(gex_TI_t result, gex_Token_Info_t *info, gex_TI_t mask) {
  // Validate client's requested mask
  if (mask & ~GEX_TI_ALL) {
    gasneti_fatalerror("Mask argument to gex_Token_Info() includes unknown bits");
  }

  // Validate conduit's returned mask (any requested+required fields missing?);
  gasneti_assert(! (~result & (mask & GASNETI_TI_REQUIRED)));

  // For each field set: validate
  if (result & GEX_TI_SRCRANK) {
    gasneti_assert(info->gex_srcrank < gasneti_nodes);
  }
  if (result & GEX_TI_EP) {
    // TODO-EX: will need some means to validate in conduit-independent manner
    // NULL THUNK_TM may occur in bootstrap collectives before ep0 exists
    gasneti_assert(!gasneti_THUNK_TM || info->gex_ep == gasneti_THUNK_EP);
  }
  if (result & GEX_TI_ENTRY) {
    gasneti_assert(info->gex_entry);
    gasneti_am_validate(info->gex_entry, 1);
  }
  if (result & GEX_TI_IS_REQ) {
    gasneti_assert(info->gex_is_req == !!info->gex_is_req); // Is 0 or 1
    if (result & GEX_TI_ENTRY) {
      gasneti_assert(info->gex_entry->gex_flags &
                     (info->gex_is_req ? GEX_FLAG_AM_REQUEST : GEX_FLAG_AM_REPLY));
    }
  }
  if (result & GEX_TI_IS_LONG) {
    gasneti_assert(info->gex_is_long == !!info->gex_is_long); // Is 0 or 1
    if (result & GEX_TI_ENTRY) {
      gasneti_assert(info->gex_entry->gex_flags &
                     (info->gex_is_long ? GEX_FLAG_AM_LONG : GEX_FLAG_AM_SHORT|GEX_FLAG_AM_MEDIUM));
    }
  }

  // From here forward, consider only the requested subset of the conduit-provided result
  result &= mask;

  // For each field not requested or requested but not set: INvalidate
  if (!(result & GEX_TI_SRCRANK)) {
    info->gex_srcrank = GEX_RANK_INVALID;
  }
  if (!(result & GEX_TI_EP)) {
    info->gex_ep = NULL;
  }
  if (!(result & GEX_TI_ENTRY)) {
    info->gex_entry = NULL;
  }
  if (!(result & GEX_TI_IS_REQ)) {
    info->gex_is_req = 2; // true invalidation not possible for a boolean
  }
  if (!(result & GEX_TI_IS_LONG)) {
    info->gex_is_long = 2; // true invalidation not possible for a boolean
  }

  return result;
}
#endif

/* ------------------------------------------------------------------------------------ */
// Default implementation of va_list (V-suffixed) AM interfaces in terms of M-suffixed.
// NOTE: this is NOT a path one should use except to support the default split-phase AMs.

#ifndef gasneti_AMRequestMediumV
extern int _gasneti_AMRequestMediumV(
                gex_TM_t tm, gex_Rank_t rank, gex_AM_Index_t hidx,
                /*const*/ void *src_addr, size_t nbytes,
                gex_Event_t *lc_opt, gex_Flags_t flags GASNETI_THREAD_FARG,
                int nargs, va_list args)
{
  gasneti_assert(gex_AM_MaxArgs() <= 16);
  gex_AM_Arg_t pargs[16];
  for (int i = 0; i < nargs; ++i) {
    pargs[i] = va_arg(args, gex_AM_Arg_t);
  }

  int rc;
  switch (nargs) {
    case  0: rc = gex_AM_RequestMedium0 (tm, rank, hidx, src_addr, nbytes, lc_opt, flags); break;
    case  1: rc = gex_AM_RequestMedium1 (tm, rank, hidx, src_addr, nbytes, lc_opt, flags, pargs[0]); break;
    case  2: rc = gex_AM_RequestMedium2 (tm, rank, hidx, src_addr, nbytes, lc_opt, flags, pargs[0], pargs[1]); break;
    case  3: rc = gex_AM_RequestMedium3 (tm, rank, hidx, src_addr, nbytes, lc_opt, flags, pargs[0], pargs[1], pargs[2]); break;
    case  4: rc = gex_AM_RequestMedium4 (tm, rank, hidx, src_addr, nbytes, lc_opt, flags, pargs[0], pargs[1], pargs[2], pargs[3]); break;
    case  5: rc = gex_AM_RequestMedium5 (tm, rank, hidx, src_addr, nbytes, lc_opt, flags, pargs[0], pargs[1], pargs[2], pargs[3], pargs[4]); break;
    case  6: rc = gex_AM_RequestMedium6 (tm, rank, hidx, src_addr, nbytes, lc_opt, flags, pargs[0], pargs[1], pargs[2], pargs[3], pargs[4], pargs[5]); break;
    case  7: rc = gex_AM_RequestMedium7 (tm, rank, hidx, src_addr, nbytes, lc_opt, flags, pargs[0], pargs[1], pargs[2], pargs[3], pargs[4], pargs[5], pargs[6]); break;
    case  8: rc = gex_AM_RequestMedium8 (tm, rank, hidx, src_addr, nbytes, lc_opt, flags, pargs[0], pargs[1], pargs[2], pargs[3], pargs[4], pargs[5], pargs[6], pargs[7]); break;
    case  9: rc = gex_AM_RequestMedium9 (tm, rank, hidx, src_addr, nbytes, lc_opt, flags, pargs[0], pargs[1], pargs[2], pargs[3], pargs[4], pargs[5], pargs[6], pargs[7], pargs[8]); break;
    case 10: rc = gex_AM_RequestMedium10(tm, rank, hidx, src_addr, nbytes, lc_opt, flags, pargs[0], pargs[1], pargs[2], pargs[3], pargs[4], pargs[5], pargs[6], pargs[7], pargs[8], pargs[9]); break;
    case 11: rc = gex_AM_RequestMedium11(tm, rank, hidx, src_addr, nbytes, lc_opt, flags, pargs[0], pargs[1], pargs[2], pargs[3], pargs[4], pargs[5], pargs[6], pargs[7], pargs[8], pargs[9], pargs[10]); break;
    case 12: rc = gex_AM_RequestMedium12(tm, rank, hidx, src_addr, nbytes, lc_opt, flags, pargs[0], pargs[1], pargs[2], pargs[3], pargs[4], pargs[5], pargs[6], pargs[7], pargs[8], pargs[9], pargs[10], pargs[11]); break;
    case 13: rc = gex_AM_RequestMedium13(tm, rank, hidx, src_addr, nbytes, lc_opt, flags, pargs[0], pargs[1], pargs[2], pargs[3], pargs[4], pargs[5], pargs[6], pargs[7], pargs[8], pargs[9], pargs[10], pargs[11], pargs[12]); break;
    case 14: rc = gex_AM_RequestMedium14(tm, rank, hidx, src_addr, nbytes, lc_opt, flags, pargs[0], pargs[1], pargs[2], pargs[3], pargs[4], pargs[5], pargs[6], pargs[7], pargs[8], pargs[9], pargs[10], pargs[11], pargs[12], pargs[13]); break;
    case 15: rc = gex_AM_RequestMedium15(tm, rank, hidx, src_addr, nbytes, lc_opt, flags, pargs[0], pargs[1], pargs[2], pargs[3], pargs[4], pargs[5], pargs[6], pargs[7], pargs[8], pargs[9], pargs[10], pargs[11], pargs[12], pargs[13], pargs[14]); break;
    case 16: rc = gex_AM_RequestMedium16(tm, rank, hidx, src_addr, nbytes, lc_opt, flags, pargs[0], pargs[1], pargs[2], pargs[3], pargs[4], pargs[5], pargs[6], pargs[7], pargs[8], pargs[9], pargs[10], pargs[11], pargs[12], pargs[13], pargs[14], pargs[15]); break;
  }

  return rc;
}
#define gasneti_AMRequestMediumV(tm,rank,hidx,src_addr,nbytes,lc_opt,flags,nargs,args) \
       _gasneti_AMRequestMediumV(tm,rank,hidx,src_addr,nbytes,lc_opt,flags GASNETI_THREAD_GET,nargs,args)
#endif

#ifndef gasneti_AMRequestLongV
extern int _gasneti_AMRequestLongV(
                gex_TM_t tm, gex_Rank_t rank, gex_AM_Index_t hidx,
                /*const*/ void *src_addr, size_t nbytes, void *dst_addr,
                gex_Event_t *lc_opt, gex_Flags_t flags GASNETI_THREAD_FARG,
                int nargs, va_list args)
{
  gasneti_assert(gex_AM_MaxArgs() <= 16);
  gex_AM_Arg_t pargs[16];
  for (int i = 0; i < nargs; ++i) {
    pargs[i] = va_arg(args, gex_AM_Arg_t);
  }

  int rc;
  switch (nargs) {
    case  0: rc = gex_AM_RequestLong0 (tm, rank, hidx, src_addr, nbytes, dst_addr, lc_opt, flags); break;
    case  1: rc = gex_AM_RequestLong1 (tm, rank, hidx, src_addr, nbytes, dst_addr, lc_opt, flags, pargs[0]); break;
    case  2: rc = gex_AM_RequestLong2 (tm, rank, hidx, src_addr, nbytes, dst_addr, lc_opt, flags, pargs[0], pargs[1]); break;
    case  3: rc = gex_AM_RequestLong3 (tm, rank, hidx, src_addr, nbytes, dst_addr, lc_opt, flags, pargs[0], pargs[1], pargs[2]); break;
    case  4: rc = gex_AM_RequestLong4 (tm, rank, hidx, src_addr, nbytes, dst_addr, lc_opt, flags, pargs[0], pargs[1], pargs[2], pargs[3]); break;
    case  5: rc = gex_AM_RequestLong5 (tm, rank, hidx, src_addr, nbytes, dst_addr, lc_opt, flags, pargs[0], pargs[1], pargs[2], pargs[3], pargs[4]); break;
    case  6: rc = gex_AM_RequestLong6 (tm, rank, hidx, src_addr, nbytes, dst_addr, lc_opt, flags, pargs[0], pargs[1], pargs[2], pargs[3], pargs[4], pargs[5]); break;
    case  7: rc = gex_AM_RequestLong7 (tm, rank, hidx, src_addr, nbytes, dst_addr, lc_opt, flags, pargs[0], pargs[1], pargs[2], pargs[3], pargs[4], pargs[5], pargs[6]); break;
    case  8: rc = gex_AM_RequestLong8 (tm, rank, hidx, src_addr, nbytes, dst_addr, lc_opt, flags, pargs[0], pargs[1], pargs[2], pargs[3], pargs[4], pargs[5], pargs[6], pargs[7]); break;
    case  9: rc = gex_AM_RequestLong9 (tm, rank, hidx, src_addr, nbytes, dst_addr, lc_opt, flags, pargs[0], pargs[1], pargs[2], pargs[3], pargs[4], pargs[5], pargs[6], pargs[7], pargs[8]); break;
    case 10: rc = gex_AM_RequestLong10(tm, rank, hidx, src_addr, nbytes, dst_addr, lc_opt, flags, pargs[0], pargs[1], pargs[2], pargs[3], pargs[4], pargs[5], pargs[6], pargs[7], pargs[8], pargs[9]); break;
    case 11: rc = gex_AM_RequestLong11(tm, rank, hidx, src_addr, nbytes, dst_addr, lc_opt, flags, pargs[0], pargs[1], pargs[2], pargs[3], pargs[4], pargs[5], pargs[6], pargs[7], pargs[8], pargs[9], pargs[10]); break;
    case 12: rc = gex_AM_RequestLong12(tm, rank, hidx, src_addr, nbytes, dst_addr, lc_opt, flags, pargs[0], pargs[1], pargs[2], pargs[3], pargs[4], pargs[5], pargs[6], pargs[7], pargs[8], pargs[9], pargs[10], pargs[11]); break;
    case 13: rc = gex_AM_RequestLong13(tm, rank, hidx, src_addr, nbytes, dst_addr, lc_opt, flags, pargs[0], pargs[1], pargs[2], pargs[3], pargs[4], pargs[5], pargs[6], pargs[7], pargs[8], pargs[9], pargs[10], pargs[11], pargs[12]); break;
    case 14: rc = gex_AM_RequestLong14(tm, rank, hidx, src_addr, nbytes, dst_addr, lc_opt, flags, pargs[0], pargs[1], pargs[2], pargs[3], pargs[4], pargs[5], pargs[6], pargs[7], pargs[8], pargs[9], pargs[10], pargs[11], pargs[12], pargs[13]); break;
    case 15: rc = gex_AM_RequestLong15(tm, rank, hidx, src_addr, nbytes, dst_addr, lc_opt, flags, pargs[0], pargs[1], pargs[2], pargs[3], pargs[4], pargs[5], pargs[6], pargs[7], pargs[8], pargs[9], pargs[10], pargs[11], pargs[12], pargs[13], pargs[14]); break;
    case 16: rc = gex_AM_RequestLong16(tm, rank, hidx, src_addr, nbytes, dst_addr, lc_opt, flags, pargs[0], pargs[1], pargs[2], pargs[3], pargs[4], pargs[5], pargs[6], pargs[7], pargs[8], pargs[9], pargs[10], pargs[11], pargs[12], pargs[13], pargs[14], pargs[15]); break;
  }

  return rc;
}
#define gasneti_AMRequestLongV(tm,rank,hidx,src_addr,nbytes,dst_addr,lc_opt,flags,nargs,args) \
       _gasneti_AMRequestLongV(tm,rank,hidx,src_addr,nbytes,dst_addr,lc_opt,flags GASNETI_THREAD_GET,nargs,args)
#endif

#ifndef gasneti_AMReplyMediumV
extern int _gasneti_AMReplyMediumV(
                gex_Token_t token, gex_AM_Index_t hidx,
                /*const*/ void *src_addr, size_t nbytes,
                gex_Event_t *lc_opt, gex_Flags_t flags GASNETI_THREAD_FARG,
                int nargs, va_list args)
{
  gasneti_assert(gex_AM_MaxArgs() <= 16);
  gex_AM_Arg_t pargs[16];
  for (int i = 0; i < nargs; ++i) {
    pargs[i] = va_arg(args, gex_AM_Arg_t);
  }

  int rc;
  switch (nargs) {
    case  0: rc = gex_AM_ReplyMedium0 (token, hidx, src_addr, nbytes, lc_opt, flags); break;
    case  1: rc = gex_AM_ReplyMedium1 (token, hidx, src_addr, nbytes, lc_opt, flags, pargs[0]); break;
    case  2: rc = gex_AM_ReplyMedium2 (token, hidx, src_addr, nbytes, lc_opt, flags, pargs[0], pargs[1]); break;
    case  3: rc = gex_AM_ReplyMedium3 (token, hidx, src_addr, nbytes, lc_opt, flags, pargs[0], pargs[1], pargs[2]); break;
    case  4: rc = gex_AM_ReplyMedium4 (token, hidx, src_addr, nbytes, lc_opt, flags, pargs[0], pargs[1], pargs[2], pargs[3]); break;
    case  5: rc = gex_AM_ReplyMedium5 (token, hidx, src_addr, nbytes, lc_opt, flags, pargs[0], pargs[1], pargs[2], pargs[3], pargs[4]); break;
    case  6: rc = gex_AM_ReplyMedium6 (token, hidx, src_addr, nbytes, lc_opt, flags, pargs[0], pargs[1], pargs[2], pargs[3], pargs[4], pargs[5]); break;
    case  7: rc = gex_AM_ReplyMedium7 (token, hidx, src_addr, nbytes, lc_opt, flags, pargs[0], pargs[1], pargs[2], pargs[3], pargs[4], pargs[5], pargs[6]); break;
    case  8: rc = gex_AM_ReplyMedium8 (token, hidx, src_addr, nbytes, lc_opt, flags, pargs[0], pargs[1], pargs[2], pargs[3], pargs[4], pargs[5], pargs[6], pargs[7]); break;
    case  9: rc = gex_AM_ReplyMedium9 (token, hidx, src_addr, nbytes, lc_opt, flags, pargs[0], pargs[1], pargs[2], pargs[3], pargs[4], pargs[5], pargs[6], pargs[7], pargs[8]); break;
    case 10: rc = gex_AM_ReplyMedium10(token, hidx, src_addr, nbytes, lc_opt, flags, pargs[0], pargs[1], pargs[2], pargs[3], pargs[4], pargs[5], pargs[6], pargs[7], pargs[8], pargs[9]); break;
    case 11: rc = gex_AM_ReplyMedium11(token, hidx, src_addr, nbytes, lc_opt, flags, pargs[0], pargs[1], pargs[2], pargs[3], pargs[4], pargs[5], pargs[6], pargs[7], pargs[8], pargs[9], pargs[10]); break;
    case 12: rc = gex_AM_ReplyMedium12(token, hidx, src_addr, nbytes, lc_opt, flags, pargs[0], pargs[1], pargs[2], pargs[3], pargs[4], pargs[5], pargs[6], pargs[7], pargs[8], pargs[9], pargs[10], pargs[11]); break;
    case 13: rc = gex_AM_ReplyMedium13(token, hidx, src_addr, nbytes, lc_opt, flags, pargs[0], pargs[1], pargs[2], pargs[3], pargs[4], pargs[5], pargs[6], pargs[7], pargs[8], pargs[9], pargs[10], pargs[11], pargs[12]); break;
    case 14: rc = gex_AM_ReplyMedium14(token, hidx, src_addr, nbytes, lc_opt, flags, pargs[0], pargs[1], pargs[2], pargs[3], pargs[4], pargs[5], pargs[6], pargs[7], pargs[8], pargs[9], pargs[10], pargs[11], pargs[12], pargs[13]); break;
    case 15: rc = gex_AM_ReplyMedium15(token, hidx, src_addr, nbytes, lc_opt, flags, pargs[0], pargs[1], pargs[2], pargs[3], pargs[4], pargs[5], pargs[6], pargs[7], pargs[8], pargs[9], pargs[10], pargs[11], pargs[12], pargs[13], pargs[14]); break;
    case 16: rc = gex_AM_ReplyMedium16(token, hidx, src_addr, nbytes, lc_opt, flags, pargs[0], pargs[1], pargs[2], pargs[3], pargs[4], pargs[5], pargs[6], pargs[7], pargs[8], pargs[9], pargs[10], pargs[11], pargs[12], pargs[13], pargs[14], pargs[15]); break;
  }

  return rc;
}
#define gasneti_AMReplyMediumV(token,hidx,src_addr,nbytes,lc_opt,flags,nargs,args) \
       _gasneti_AMReplyMediumV(token,hidx,src_addr,nbytes,lc_opt,flags GASNETI_THREAD_GET,nargs,args)
#endif

#ifndef gasneti_AMReplyLongV
extern int _gasneti_AMReplyLongV(
                gex_Token_t token, gex_AM_Index_t hidx,
                /*const*/ void *src_addr, size_t nbytes, void *dst_addr,
                gex_Event_t *lc_opt, gex_Flags_t flags GASNETI_THREAD_FARG,
                int nargs, va_list args)
{
  gasneti_assert(gex_AM_MaxArgs() <= 16);
  gex_AM_Arg_t pargs[16];
  for (int i = 0; i < nargs; ++i) {
    pargs[i] = va_arg(args, gex_AM_Arg_t);
  }

  int rc;
  switch (nargs) {
    case  0: rc = gex_AM_ReplyLong0 (token, hidx, src_addr, nbytes, dst_addr, lc_opt, flags); break;
    case  1: rc = gex_AM_ReplyLong1 (token, hidx, src_addr, nbytes, dst_addr, lc_opt, flags, pargs[0]); break;
    case  2: rc = gex_AM_ReplyLong2 (token, hidx, src_addr, nbytes, dst_addr, lc_opt, flags, pargs[0], pargs[1]); break;
    case  3: rc = gex_AM_ReplyLong3 (token, hidx, src_addr, nbytes, dst_addr, lc_opt, flags, pargs[0], pargs[1], pargs[2]); break;
    case  4: rc = gex_AM_ReplyLong4 (token, hidx, src_addr, nbytes, dst_addr, lc_opt, flags, pargs[0], pargs[1], pargs[2], pargs[3]); break;
    case  5: rc = gex_AM_ReplyLong5 (token, hidx, src_addr, nbytes, dst_addr, lc_opt, flags, pargs[0], pargs[1], pargs[2], pargs[3], pargs[4]); break;
    case  6: rc = gex_AM_ReplyLong6 (token, hidx, src_addr, nbytes, dst_addr, lc_opt, flags, pargs[0], pargs[1], pargs[2], pargs[3], pargs[4], pargs[5]); break;
    case  7: rc = gex_AM_ReplyLong7 (token, hidx, src_addr, nbytes, dst_addr, lc_opt, flags, pargs[0], pargs[1], pargs[2], pargs[3], pargs[4], pargs[5], pargs[6]); break;
    case  8: rc = gex_AM_ReplyLong8 (token, hidx, src_addr, nbytes, dst_addr, lc_opt, flags, pargs[0], pargs[1], pargs[2], pargs[3], pargs[4], pargs[5], pargs[6], pargs[7]); break;
    case  9: rc = gex_AM_ReplyLong9 (token, hidx, src_addr, nbytes, dst_addr, lc_opt, flags, pargs[0], pargs[1], pargs[2], pargs[3], pargs[4], pargs[5], pargs[6], pargs[7], pargs[8]); break;
    case 10: rc = gex_AM_ReplyLong10(token, hidx, src_addr, nbytes, dst_addr, lc_opt, flags, pargs[0], pargs[1], pargs[2], pargs[3], pargs[4], pargs[5], pargs[6], pargs[7], pargs[8], pargs[9]); break;
    case 11: rc = gex_AM_ReplyLong11(token, hidx, src_addr, nbytes, dst_addr, lc_opt, flags, pargs[0], pargs[1], pargs[2], pargs[3], pargs[4], pargs[5], pargs[6], pargs[7], pargs[8], pargs[9], pargs[10]); break;
    case 12: rc = gex_AM_ReplyLong12(token, hidx, src_addr, nbytes, dst_addr, lc_opt, flags, pargs[0], pargs[1], pargs[2], pargs[3], pargs[4], pargs[5], pargs[6], pargs[7], pargs[8], pargs[9], pargs[10], pargs[11]); break;
    case 13: rc = gex_AM_ReplyLong13(token, hidx, src_addr, nbytes, dst_addr, lc_opt, flags, pargs[0], pargs[1], pargs[2], pargs[3], pargs[4], pargs[5], pargs[6], pargs[7], pargs[8], pargs[9], pargs[10], pargs[11], pargs[12]); break;
    case 14: rc = gex_AM_ReplyLong14(token, hidx, src_addr, nbytes, dst_addr, lc_opt, flags, pargs[0], pargs[1], pargs[2], pargs[3], pargs[4], pargs[5], pargs[6], pargs[7], pargs[8], pargs[9], pargs[10], pargs[11], pargs[12], pargs[13]); break;
    case 15: rc = gex_AM_ReplyLong15(token, hidx, src_addr, nbytes, dst_addr, lc_opt, flags, pargs[0], pargs[1], pargs[2], pargs[3], pargs[4], pargs[5], pargs[6], pargs[7], pargs[8], pargs[9], pargs[10], pargs[11], pargs[12], pargs[13], pargs[14]); break;
    case 16: rc = gex_AM_ReplyLong16(token, hidx, src_addr, nbytes, dst_addr, lc_opt, flags, pargs[0], pargs[1], pargs[2], pargs[3], pargs[4], pargs[5], pargs[6], pargs[7], pargs[8], pargs[9], pargs[10], pargs[11], pargs[12], pargs[13], pargs[14], pargs[15]); break;
  }

  return rc;
}
#define gasneti_AMReplyLongV(token,hidx,src_addr,nbytes,dst_addr,lc_opt,flags,nargs,args) \
       _gasneti_AMReplyLongV(token,hidx,src_addr,nbytes,dst_addr,lc_opt,flags GASNETI_THREAD_GET,nargs,args)
#endif

/* ------------------------------------------------------------------------------------ */
// Default implementation of split-phase AMs in terms of single-phase
// TODO-EX: this is not a "good" implementation for any conduit
// TODO-EX: should really have single-phase in terms of the split-phase instead

#ifndef _GEX_AM_SRCDESC_T
#ifndef gasneti_import_srcdesc
gasneti_AM_SrcDesc_t gasneti_import_srcdesc(gex_AM_SrcDesc_t _srcdesc) {
  const gasneti_AM_SrcDesc_t _real_srcdesc = GASNETI_IMPORT_POINTER(gasneti_AM_SrcDesc_t,_srcdesc);
  GASNETI_CHECK_MAGIC(_real_srcdesc, GASNETI_AM_SRCDESC_MAGIC);
  return _real_srcdesc;
}
#endif

#ifndef gasneti_export_srcdesc
gex_AM_SrcDesc_t gasneti_export_srcdesc(gasneti_AM_SrcDesc_t _real_srcdesc) {
  GASNETI_CHECK_MAGIC(_real_srcdesc, GASNETI_AM_SRCDESC_MAGIC);
  return GASNETI_EXPORT_POINTER(gex_AM_SrcDesc_t, _real_srcdesc);
}
#endif

GASNETI_INLINE(gasneti_alloc_srcdesc)
gasneti_AM_SrcDesc_t gasneti_alloc_srcdesc(
                       int            nargs
                       GASNETI_THREAD_FARG)
{
  gasneti_AM_SrcDesc_t sd = gasneti_malloc(sizeof(*sd));
  GASNETI_INIT_MAGIC(sd, GASNETI_AM_SRCDESC_MAGIC);
#if GASNET_DEBUG
  sd->_thread = GASNETI_MYTHREAD;
#endif
  sd->_nargs     = nargs;
  return sd;
}

static gasneti_AM_SrcDesc_t gasneti_alloc_request_srcdesc(
                       gex_TM_t       tm,
                       gex_Rank_t     rank,
                       int            nargs
                       GASNETI_THREAD_FARG)
{
  gasneti_AM_SrcDesc_t sd = gasneti_alloc_srcdesc(nargs GASNETI_THREAD_PASS);
  sd->_dest._request._tm   = tm;
  sd->_dest._request._rank = rank;
#if GASNET_DEBUG
  sd->_isreq  = 1;
#endif
#ifdef GASNETI_SD_ALLOC_REQ_EXTRA
  GASNETI_SD_ALLOC_REQ_EXTRA(sd);
#endif
  return sd;
}

static gasneti_AM_SrcDesc_t gasneti_alloc_reply_srcdesc(
                       gex_Token_t    token,
                       int            nargs
                       GASNETI_THREAD_FARG)
{
  gasneti_AM_SrcDesc_t sd = gasneti_alloc_srcdesc(nargs GASNETI_THREAD_PASS);
  sd->_dest._reply._token = token;
#if GASNET_DEBUG
  sd->_isreq  = 0;
#endif
#ifdef GASNETI_SD_ALLOC_REP_EXTRA
  GASNETI_SD_ALLOC_REP_EXTRA(sd);
#endif
  return sd;
}

static void gasneti_free_srcdesc(gasneti_AM_SrcDesc_t sd)
{
#ifdef GASNETI_SD_FREE_EXTRA
  GASNETI_SD_FREE_EXTRA(sd);
#endif
  gasneti_free(sd->_tofree);
  GASNETI_INIT_MAGIC(sd, GASNETI_AM_SRCDESC_BAD_MAGIC);
  gasneti_free(sd);
}
#endif // _GEX_AM_SRCDESC_T

// Common argument processing
#if GASNET_DEBUG
  // TODO-EX: tracing should probably occur here as well
  #define _GASNETI_CHECK_PREPARE(cbuf, min_length, max_length, limit, lc_opt, nargs, is_req, cat) \
    do {                                                                                                 \
      const char *_reqrep = is_req ? "Request" : "Reply";                                                \
      if (cbuf == NULL) {                                                                                \
        if (lc_opt != NULL)                                                                              \
          gasneti_fatalerror("gex_AM_Prepare%s" _STRINGIFY(cat) ": "                                     \
                             "only NULL is a valid lc_opt value when client_buf is NULL", _reqrep);      \
      } else if (lc_opt == NULL) {                                                                       \
        gasneti_fatalerror("gex_AM_Prepare%s" _STRINGIFY(cat) ": lc_opt must be non-NULL "               \
                           "when client_buf is non-NULL", _reqrep);                                      \
      } else if (is_req) {                                                                               \
        if (!gasneti_leaf_is_pointer(lc_opt) && (lc_opt != GEX_EVENT_NOW) && (lc_opt != GEX_EVENT_GROUP))\
          gasneti_fatalerror("gex_AM_Prepare%s" _STRINGIFY(cat) ": only pointer-to-event, "              \
                             "GEX_EVENT_NOW and GEX_EVENT_GROUP are valid lc_opt values "                \
                             "when client_buf is non-NULL", _reqrep);                                    \
      } else {                                                                                           \
        if (!gasneti_leaf_is_pointer(lc_opt) && (lc_opt != GEX_EVENT_NOW))                               \
          gasneti_fatalerror("gex_AM_Prepare%s" _STRINGIFY(cat) ": only pointer-to-event, "              \
                             "and GEX_EVENT_NOW are valid lc_opt values "                                \
                             "when client_buf is non-NULL", _reqrep);                                    \
      }                                                                                                  \
      if (lc_opt && gasneti_leaf_is_pointer(lc_opt)) {                                                   \
        *lc_opt = GEX_EVENT_NO_OP;                                                                       \
      }                                                                                                  \
      if (nargs > gex_AM_MaxArgs())                                                                      \
        gasneti_fatalerror("gex_AM_Prepare%s" _STRINGIFY(cat) ": "                                       \
                           "numargs larger than gex_AM_MaxArgs() (%u > %u)",                             \
                           _reqrep, (unsigned int)nargs, (unsigned int)gex_AM_MaxArgs());                \
      if (min_length > max_length)                                                                       \
        gasneti_fatalerror("gex_AM_Prepare%s" _STRINGIFY(cat) ": "                                       \
                           "min_length larger than max_length (%"PRIuPTR" > %"PRIuPTR")",                \
                           _reqrep, (uintptr_t)min_length, (uintptr_t)max_length);                       \
      if (min_length > limit)                                                                            \
        gasneti_fatalerror("gex_AM_Prepare%s" _STRINGIFY(cat) ": min_length larger than gex_AM_Max%s"    \
                           _STRINGIFY(cat) "() (%"PRIuPTR" > %"PRIuPTR")",                               \
                           _reqrep, _reqrep, (uintptr_t)min_length, (uintptr_t)limit);                   \
    } while(0)
  #define GASNETI_AMPREPREQUESTCOMMON(tm,dest,cbuf,min_len,max_len,limit,lc_opt,nargs,cat) \
    do {                                                                                   \
      if (dest >= gex_TM_QuerySize(tm))                                                    \
        gasneti_fatalerror("gex_AM_PrepareRequest" _STRINGIFY(cat) ": "                    \
                           "destination rank out-of-range (%lu >= %lu)",                   \
                           (unsigned long)dest, (unsigned long)gex_TM_QuerySize(tm));      \
        _GASNETI_CHECK_PREPARE(cbuf,min_len,max_len,limit,lc_opt,nargs,1,cat);             \
    } while(0)
  #define GASNETI_AMPREPREPLYCOMMON(cbuf,min_len,max_len,limit,lc_opt,nargs,cat) \
             _GASNETI_CHECK_PREPARE(cbuf,min_len,max_len,limit,lc_opt,nargs,0,cat)

  #define _GASNETI_CHECK_COMMIT(sd,handler,nbytes,dest_addr,nargs,is_req,cat) \
    do {                                                                                                 \
      const char *_reqrep = is_req ? "Request" : "Reply";                                                \
      if (!sd)                                                                                           \
        gasneti_fatalerror("gex_AM_Commit%s" _STRINGIFY(cat) "%d: "                                      \
                           "passed invalid gex_AM_SrcDesc (GEX_AM_SRCDESC_NO_OP == 0)", _reqrep, nargs); \
      if (sd->_thread != gasnete_mythread())                                                             \
        gasneti_fatalerror("gex_AM_Commit%s" _STRINGIFY(cat) "%d: "                                      \
                           "return from Prepare passed to Commit in a different thread", _reqrep, nargs);\
      if (sd->_isreq != is_req)                                                                          \
        gasneti_fatalerror("gex_AM_Commit%s" _STRINGIFY(cat) "%d: "                                      \
                           "paired with incompatible Prepare (%s)",                                      \
                           _reqrep, nargs, (sd->_isreq?"Request":"Reply"));                              \
      if (sd->_category != (int)gasneti_##cat)                                                           \
        gasneti_fatalerror("gex_AM_Commit%s" _STRINGIFY(cat) "%d: "                                      \
                           "paired with incompatible Prepare (%s)",                                      \
                           _reqrep, nargs, (sd->_category==(int)gasneti_Long?"Long":"Medium"));          \
      if (sd->_nargs != nargs)                                                                           \
        gasneti_fatalerror("gex_AM_Commit%s" _STRINGIFY(cat) "%d: "                                      \
                           "paired with incompatible Prepare (nargs = %d)",                              \
                           _reqrep, nargs, sd->_nargs);                                                  \
      if (sd->_size < nbytes)                                                                            \
        gasneti_fatalerror("gex_AM_Commit%s" _STRINGIFY(cat) "%d: "                                      \
                           "nbytes larger than returned from Prepare (%"PRIuPTR" > %"PRIuPTR")",         \
                           _reqrep, nargs, (uintptr_t)nbytes, (uintptr_t)sd->_size);                     \
      if ((sd->_dest_addr != NULL) &&                                                                    \
          !((dest_addr == sd->_dest_addr) || ((dest_addr == NULL) && (nbytes == 0))))                    \
        gasneti_fatalerror("gex_AM_Commit%s" _STRINGIFY(cat) "%d: "                                      \
                           "dest_addr does not match the value passed to Prepare", _reqrep, nargs);      \
      if (sd->_tofree) {                                                                                 \
        if (gasneti_test_sd_poison(sd->_tofree, sd->_size))                                              \
          gasneti_fatalerror("gex_AM_Commit%s" _STRINGIFY(cat) "%d: "                                    \
                             "client did not write to the GASNet-provided buffer",                       \
                             _reqrep, nargs);                                                            \
      }                                                                                                  \
    } while(0)
  #define GASNETI_AMCOMMITREQUESTCOMMON(sd,handler,nbytes,dest_addr,nargs,cat) \
                  _GASNETI_CHECK_COMMIT(sd,handler,nbytes,dest_addr,nargs,1,cat)
  #define GASNETI_AMCOMMITREPLYCOMMON(sd,handler,nbytes,dest_addr,nargs,cat) \
                  _GASNETI_CHECK_COMMIT(sd,handler,nbytes,dest_addr,nargs,0,cat)
#else
  #define gasneti_init_sd_poison(a,l) ((void)0)
  #define GASNETI_AMPREPREQUESTCOMMON(tm,dest,cbuf,min,max,lim,lc_opt,nargs,cat) ((void)0)
  #define GASNETI_AMPREPREPLYCOMMON(cbuf,minlen,maxlen,lim,lc_opt,nargs,cat) ((void)0)
  #define GASNETI_AMCOMMITREQUESTCOMMON(sd,handler,nbytes,dest_addr,nargs,cat) ((void)0)
  #define GASNETI_AMCOMMITREPLYCOMMON(sd,handler,nbytes,dest_addr,nargs,cat) ((void)0)
#endif

GASNETI_INLINE(gasneti_prepare_common)
gex_AM_SrcDesc_t gasneti_prepare_common(
                       gasneti_AM_SrcDesc_t sd,
                       const void           *client_buf,
                       size_t               length,
                       gex_Event_t          *lc_opt,
                       gex_Flags_t          flags)
{
    if (client_buf) {
        sd->_tofree = NULL;
        sd->_addr   = (/*non-const*/void *)client_buf;
    } else {
#if GASNET_DEBUG
        // Allocate at least one byte because zero-byte allocation
        // returns NULL which then leads to ambiguity in argument checking.
        sd->_addr   = gasneti_malloc(MAX(1,length));
#else
        sd->_addr   = gasneti_malloc(length);
#endif
        sd->_tofree = sd->_addr;
        gasneti_init_sd_poison(sd->_tofree, length);
    }
    sd->_size     = length;
    sd->_lc_opt   = lc_opt;
    sd->_flags    = flags;
    return gasneti_export_srcdesc(sd);
}

GASNETI_INLINE(gasneti_prepare_medium_common)
gex_AM_SrcDesc_t gasneti_prepare_medium_common(
                       gasneti_AM_SrcDesc_t sd,
                       const void           *client_buf,
                       size_t               length,
                       gex_Event_t          *lc_opt,
                       gex_Flags_t          flags)
{
#if GASNET_DEBUG
    sd->_category  = (int)gasneti_Medium;
    sd->_dest_addr = NULL;
#endif
    return gasneti_prepare_common(sd, client_buf, length, lc_opt, flags);
}

GASNETI_INLINE(gasneti_prepare_long_common)
gex_AM_SrcDesc_t gasneti_prepare_long_common(
                       gasneti_AM_SrcDesc_t sd,
                       const void           *client_buf,
                       size_t               length,
                       void                 *dest_addr,
                       gex_Event_t          *lc_opt,
                       gex_Flags_t          flags)
{
#if GASNET_DEBUG
    sd->_category  = (int)gasneti_Long;
#endif
    sd->_dest_addr = dest_addr;
    return gasneti_prepare_common(sd, client_buf, length, lc_opt, flags);
}


#ifndef gasnetc_AM_PrepareRequestMedium
extern gex_AM_SrcDesc_t gasnetc_AM_PrepareRequestMedium(
                       gex_TM_t           tm,
                       gex_Rank_t         dest,
                       const void        *client_buf,
                       size_t             min_length,
                       size_t             max_length,
                       gex_Event_t       *lc_opt,
                       gex_Flags_t        flags
                       GASNETI_THREAD_FARG,
                       unsigned int       nargs)
{
    flags &= ~GEX_FLAG_IMMEDIATE;
    size_t limit = gex_AM_MaxRequestMedium(tm,dest,lc_opt,flags,nargs);
    GASNETI_AMPREPREQUESTCOMMON(tm,dest,client_buf, min_length, max_length, limit, lc_opt, nargs, Medium);

    return gasneti_prepare_medium_common(
               gasneti_alloc_request_srcdesc(tm, dest, nargs GASNETI_THREAD_PASS),
               client_buf, MIN(limit, max_length), lc_opt, flags);
}
#endif // gasnetc_AM_PrepareRequestMedium

#ifndef gasnetc_AM_PrepareReplyMedium
extern gex_AM_SrcDesc_t gasnetc_AM_PrepareReplyMedium(
                       gex_Token_t        token,
                       const void        *client_buf,
                       size_t             min_length,
                       size_t             max_length,
                       gex_Event_t       *lc_opt,
                       gex_Flags_t        flags
                       GASNETI_THREAD_FARG,
                       unsigned int       nargs)
{
    flags &= ~GEX_FLAG_IMMEDIATE;
    // TODO-EX: using limit=LUB here unless conduit has provided an alternative
    // TODO-EX: expect to eventually use gex_Token_MaxReplyMedium()
#if defined(gasnetc_Token_MaxReplyMedium)
    size_t limit = gasnetc_Token_MaxReplyMedium(token,lc_opt,flags,nargs);
#else
    size_t limit = gex_AM_LUBReplyMedium();
#endif
    GASNETI_AMPREPREPLYCOMMON(client_buf, min_length, max_length, limit, lc_opt, nargs, Medium);

    return gasneti_prepare_medium_common(
               gasneti_alloc_reply_srcdesc(token, nargs GASNETI_THREAD_PASS),
               client_buf, MIN(limit, max_length), lc_opt, flags);
}
#endif // gasnetc_AM_PrepareReplyMedium

#ifndef gasnetc_AM_PrepareRequestLong
extern gex_AM_SrcDesc_t gasnetc_AM_PrepareRequestLong(
                       gex_TM_t           tm,
                       gex_Rank_t         dest,
                       const void        *client_buf,
                       size_t             min_length,
                       size_t             max_length,
                       void              *dest_addr,
                       gex_Event_t       *lc_opt,
                       gex_Flags_t        flags
                       GASNETI_THREAD_FARG,
                       unsigned int       nargs)
{
    flags &= ~GEX_FLAG_IMMEDIATE;
    size_t limit = gex_AM_MaxRequestLong(tm,dest,lc_opt,flags,nargs);
    GASNETI_AMPREPREQUESTCOMMON(tm,dest,client_buf, min_length, max_length, limit, lc_opt, nargs, Long);

    return gasneti_prepare_long_common(
               gasneti_alloc_request_srcdesc(tm, dest, nargs GASNETI_THREAD_PASS),
               client_buf, MIN(limit, max_length), dest_addr, lc_opt, flags);
}
#endif // gasnetc_AM_PrepareRequestLong

#ifndef gasnetc_AM_PrepareReplyLong
extern gex_AM_SrcDesc_t gasnetc_AM_PrepareReplyLong(
                       gex_Token_t        token,
                       const void        *client_buf,
                       size_t             min_length,
                       size_t             max_length,
                       void              *dest_addr,
                       gex_Event_t       *lc_opt,
                       gex_Flags_t        flags
                       GASNETI_THREAD_FARG,
                       unsigned int       nargs)
{
    flags &= ~GEX_FLAG_IMMEDIATE;
    // TODO-EX: using limit=LUB here unless conduit has provided an alternative
    // TODO-EX: expect to eventually use gex_Token_MaxReplyLong()
#if defined(gasnetc_Token_MaxReplyLong)
    size_t limit = gasnetc_Token_MaxReplyLong(token,lc_opt,flags,nargs);
#else
    size_t limit = gex_AM_LUBReplyLong();
#endif
    GASNETI_AMPREPREPLYCOMMON(client_buf, min_length, max_length, limit, lc_opt, nargs, Long);

    return gasneti_prepare_long_common(
               gasneti_alloc_reply_srcdesc(token, nargs GASNETI_THREAD_PASS),
               client_buf, MIN(limit, max_length), dest_addr, lc_opt, flags);
}
#endif // gasnetc_AM_PrepareReplyLong

#ifndef gasnetc_AM_CommitRequestMediumM
void gasnetc_AM_CommitRequestMediumM(
                       gex_AM_Index_t          handler,
                       size_t                  nbytes
                       GASNETI_THREAD_FARG,
                     #if GASNET_DEBUG
                       unsigned int            nargs_arg,
                     #endif
                       gex_AM_SrcDesc_t        sd_arg, ...)
{
    gasneti_AM_SrcDesc_t sd = gasneti_import_srcdesc(sd_arg);
    const unsigned int nargs = sd->_nargs;

    GASNETI_AMCOMMITREQUESTCOMMON(sd,handler,nbytes,NULL,nargs_arg,Medium);

    gex_TM_t   tm          = sd->_dest._request._tm;
    gex_Rank_t dest        = sd->_dest._request._rank;
    void *src_addr         = sd->_addr;
    gex_Event_t *lc_opt    = sd->_lc_opt ? sd->_lc_opt : /* GASNet-owned buffer: */ GEX_EVENT_NOW;
    gex_Flags_t flags      = sd->_flags;

    GASNET_POST_THREADINFO(GASNETI_THREAD_PASS_ALONE);

    va_list argptr;
    va_start(argptr, sd_arg);
    int rc = gasneti_AMRequestMediumV(tm, dest, handler, src_addr, nbytes, lc_opt, flags, nargs, argptr);
    va_end(argptr);

    gasneti_assert(!rc); // IMMEDIATE is only permissible reason to return non-zero
    gasneti_free_srcdesc(sd);
}
#endif // gasnetc_AM_CommitRequestMediumM

#ifndef gasnetc_AM_CommitReplyMediumM
void gasnetc_AM_CommitReplyMediumM(
                       gex_AM_Index_t          handler,
                       size_t                  nbytes,
                     #if GASNET_DEBUG
                       unsigned int            nargs_arg,
                     #endif
                       gex_AM_SrcDesc_t        sd_arg, ...)
{
    gasneti_AM_SrcDesc_t sd = gasneti_import_srcdesc(sd_arg);
    const unsigned int nargs = sd->_nargs;

    GASNETI_AMCOMMITREPLYCOMMON(sd,handler,nbytes,NULL,nargs_arg,Medium);
    
    gex_Token_t token      = sd->_dest._reply._token;
    void *src_addr         = sd->_addr;
    gex_Event_t *lc_opt    = sd->_lc_opt ? sd->_lc_opt : /* GASNet-owned buffer: */ GEX_EVENT_NOW;
    gex_Flags_t flags      = sd->_flags;

    va_list argptr;
    va_start(argptr, sd_arg);
    int rc = gasneti_AMReplyMediumV(token, handler, src_addr, nbytes, lc_opt, flags, nargs, argptr);
    va_end(argptr);

    gasneti_assert(!rc); // IMMEDIATE is only permissible reason to return non-zero
    gasneti_free_srcdesc(sd);
}
#endif // gasnetc_AM_CommitReplyMediumM

#ifndef gasnetc_AM_CommitRequestLongM
void gasnetc_AM_CommitRequestLongM(
                       gex_AM_Index_t          handler,
                       size_t                  nbytes,
                       void                    *dest_addr
                       GASNETI_THREAD_FARG,
                     #if GASNET_DEBUG
                       unsigned int            nargs_arg,
                     #endif
                       gex_AM_SrcDesc_t        sd_arg, ...)
{
    gasneti_AM_SrcDesc_t sd = gasneti_import_srcdesc(sd_arg);
    const unsigned int nargs = sd->_nargs;

    GASNETI_AMCOMMITREQUESTCOMMON(sd,handler,nbytes,dest_addr,nargs_arg,Long);

    gex_TM_t   tm          = sd->_dest._request._tm;
    gex_Rank_t dest        = sd->_dest._request._rank;
    void *src_addr         = sd->_addr;
    gex_Event_t *lc_opt    = sd->_lc_opt ? sd->_lc_opt : /* GASNet-owned buffer: */ GEX_EVENT_NOW;
    gex_Flags_t flags      = sd->_flags;

    GASNET_POST_THREADINFO(GASNETI_THREAD_PASS_ALONE);

    va_list argptr;
    va_start(argptr, sd_arg);
    int rc = gasneti_AMRequestLongV(tm, dest, handler, src_addr, nbytes, dest_addr, lc_opt, flags, nargs, argptr);
    va_end(argptr);

    gasneti_assert(!rc); // IMMEDIATE is only permissible reason to return non-zero
    gasneti_free_srcdesc(sd);
}
#endif // gasnetc_AM_CommitRequestLongM

#ifndef gasnetc_AM_CommitReplyLongM
void gasnetc_AM_CommitReplyLongM(
                       gex_AM_Index_t          handler,
                       size_t                  nbytes,
                       void                    *dest_addr,
                     #if GASNET_DEBUG
                       unsigned int            nargs_arg,
                     #endif
                       gex_AM_SrcDesc_t        sd_arg, ...)
{
    gasneti_AM_SrcDesc_t sd = gasneti_import_srcdesc(sd_arg);
    const unsigned int nargs = sd->_nargs;

    GASNETI_AMCOMMITREPLYCOMMON(sd,handler,nbytes,dest_addr,nargs_arg,Long);
    
    gex_Token_t token      = sd->_dest._reply._token;
    void *src_addr         = sd->_addr;
    gex_Event_t *lc_opt    = sd->_lc_opt ? sd->_lc_opt : /* GASNet-owned buffer: */ GEX_EVENT_NOW;
    gex_Flags_t flags      = sd->_flags;

    va_list argptr;
    va_start(argptr, sd_arg);
    int rc = gasneti_AMReplyLongV(token, handler, src_addr, nbytes, dest_addr, lc_opt, flags, nargs, argptr);
    va_end(argptr);

    gasneti_assert(!rc); // IMMEDIATE is only permissible reason to return non-zero
    gasneti_free_srcdesc(sd);
}
#endif // gasnetc_AM_CommitReplyLongM

/* ------------------------------------------------------------------------------------ */

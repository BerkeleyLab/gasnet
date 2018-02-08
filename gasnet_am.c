/*   $Source: bitbucket.org:berkeleylab/gasnet.git/gasnet_am.c $
 * Description: GASNet conduit-independent code for Active Messages
 * Copyright 2002, Dan Bonachea <bonachea@cs.berkeley.edu>
 * Copyright 2018, The Regents of the University of California
 * Terms of use are as specified in license.txt
 */

#include <gasnet_internal.h>
#include <gasnet_extended_internal.h>
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

#if GASNET_DEBUG && !defined(gasneti_amtbl_check)
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
// Implementation of Negotiated-Payload AMs
//
// For conduit's without specialization of NP-AM, this provides the entire
// default implementation, using malloc() to obtain gasnet-owned buffers (if
// any) at Prepare and using gasneti_AM{Request,Reply}{Medium,Long}V() to
// perform the AM injection at Commit.
//
// TODO-EX: This default is not a "good" implementation for any conduit.
// TODO-EX: Native conduits should provide their own negotiated-payload and
//          ideally implement it and fixed-payload in terms of a common base.
// TODO-EX: This default's use of GEX_EVENT_NOW for gasnet-owned buffers could
//          be replaced with &event, *if* a progress function (or dependent
//          operation) were available to reap them and free buffers.  However,
//          that is only fruitful with a conduit which can provide asynchronous
//          local completion other than by copying the payload.

#ifndef _GEX_AM_SRCDESC_T
#ifndef gasneti_import_srcdesc
gasneti_AM_SrcDesc_t gasneti_import_srcdesc(gex_AM_SrcDesc_t _srcdesc) {
  const gasneti_AM_SrcDesc_t _real_srcdesc = GASNETI_IMPORT_POINTER(gasneti_AM_SrcDesc_t,_srcdesc);
  GASNETI_CHECK_MAGIC(_real_srcdesc, GASNETI_AM_SRCDESC_MAGIC);
  gasneti_assert(!_real_srcdesc || (_real_srcdesc->_thread == gasnete_mythread()));
  return _real_srcdesc;
}
#endif

#ifndef gasneti_export_srcdesc
gex_AM_SrcDesc_t gasneti_export_srcdesc(gasneti_AM_SrcDesc_t _real_srcdesc) {
  GASNETI_CHECK_MAGIC(_real_srcdesc, GASNETI_AM_SRCDESC_MAGIC);
  return GASNETI_EXPORT_POINTER(gex_AM_SrcDesc_t, _real_srcdesc);
}
#endif

gasneti_AM_SrcDesc_t gasneti_init_srcdesc(int isreq GASNETI_THREAD_FARG)
{
  gasneti_assert(isreq == !!isreq); // 0 or 1
  gasneti_AM_SrcDesc_t sd = &(GASNETI_MYTHREAD->gasneti_sds[isreq]);
  GASNETI_INIT_MAGIC(sd, GASNETI_AM_SRCDESC_BAD_MAGIC); // Yes, we start "BAD"
  sd->_thread = GASNETI_MYTHREAD;
#if GASNET_DEBUG
  sd->_isreq  = isreq;
#endif
  if (isreq) {
     GASNETI_MYTHREAD->gasneti_req_sd = sd;
  } else {
     GASNETI_MYTHREAD->gasneti_rep_sd = sd;
  }
  return sd;
}
#endif // _GEX_AM_SRCDESC_T

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
    gasneti_AM_SrcDesc_t sd = gasneti_init_request_srcdesc(GASNETI_THREAD_PASS_ALONE);
    GASNETI_AMPREPREQUESTCOMMON(sd,tm,dest,client_buf,min_length,max_length,NULL,lc_opt,flags,nargs,Medium);

#if GASNET_PSHM
    if (GASNETI_IS_AMPSHM_PREPARE_REQ(sd, tm, dest)) {
        gasneti_AMPoll(); // Ensure at least one poll upon Request injection
        int imm = gasnetc_AMPSHM_PrepareRequestMedium(sd, tm, dest, client_buf, min_length, max_length,
                                                      lc_opt, flags, nargs GASNETI_THREAD_PASS);
        if (imm) goto out_immediate;
    } else
#endif
    {
        // Ensure at least one poll upon Request injection (exactly one if possible)
        #if GASNETC_REQUESTV_POLLS
            // Conduit's Request{Medium,Long}V will AMPoll in Commit
        #else
            gasneti_AMPoll();
        #endif
        size_t limit = gex_AM_MaxRequestMedium(tm, dest, lc_opt, flags, nargs);
        size_t size = MIN(max_length, limit);
        gasneti_prepare_request_common(sd, tm, dest, client_buf, size, lc_opt, flags, nargs);
    }

    gasneti_init_sd_poison(sd);
    return gasneti_export_srcdesc(sd);

out_immediate:
    gasneti_reset_srcdesc(sd);
    return GEX_AM_SRCDESC_NO_OP;
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
    gasneti_AM_SrcDesc_t sd = gasneti_init_reply_srcdesc(GASNETI_THREAD_PASS_ALONE);
    GASNETI_AMPREPREPLYCOMMON(sd,token,client_buf,min_length,max_length,NULL,lc_opt,flags,nargs,Medium);

#if GASNET_PSHM
    if (GASNETI_IS_AMPSHM_PREPARE_REP(sd, token)) {
        int imm = gasnetc_AMPSHM_PrepareReplyMedium(sd, token, client_buf, min_length, max_length,
                                                      lc_opt, flags, nargs GASNETI_THREAD_PASS);
        if (imm) goto out_immediate;
    } else
#endif
    {
        size_t limit = gasnetc_Token_MaxReplyMedium(token, lc_opt, flags, nargs);
        size_t size = MIN(max_length, limit);
        gasneti_prepare_reply_common(sd, token, client_buf, size, lc_opt, flags, nargs);
    }

    gasneti_init_sd_poison(sd);
    return gasneti_export_srcdesc(sd);

out_immediate:
    gasneti_reset_srcdesc(sd);
    return GEX_AM_SRCDESC_NO_OP;
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
    gasneti_AM_SrcDesc_t sd = gasneti_init_request_srcdesc(GASNETI_THREAD_PASS_ALONE);
    GASNETI_AMPREPREQUESTCOMMON(sd,tm,dest,client_buf,min_length,max_length,dest_addr,lc_opt,flags,nargs,Long);

#if GASNET_PSHM
    if (GASNETI_IS_AMPSHM_PREPARE_REQ(sd, tm, dest)) {
        gasneti_AMPoll(); // Ensure at least one poll upon Request injection
        int imm = gasnetc_AMPSHM_PrepareRequestLong(sd, tm, dest, client_buf, min_length, max_length,
                                                    dest_addr, lc_opt, flags, nargs GASNETI_THREAD_PASS);
        if (imm) goto out_immediate;
    } else
#endif
    {
        // Ensure at least one poll upon Request injection (exactly one if possible)
        #if GASNETC_REQUESTV_POLLS
            // Conduit's Request{Medium,Long}V will AMPoll in Commit
        #else
            gasneti_AMPoll();
        #endif
        size_t limit = gex_AM_MaxRequestLong(tm, dest, lc_opt, flags, nargs);
        size_t size = MIN(max_length, limit);
        gasneti_prepare_request_common(sd, tm, dest, client_buf, size, lc_opt, flags, nargs);
        sd->_dest_addr = dest_addr;
    }

    gasneti_init_sd_poison(sd);
    return gasneti_export_srcdesc(sd);

out_immediate:
    gasneti_reset_srcdesc(sd);
    return GEX_AM_SRCDESC_NO_OP;
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
    gasneti_AM_SrcDesc_t sd = gasneti_init_reply_srcdesc(GASNETI_THREAD_PASS_ALONE);
    GASNETI_AMPREPREPLYCOMMON(sd,token,client_buf,min_length,max_length,dest_addr,lc_opt,flags,nargs,Long);

#if GASNET_PSHM
    if (GASNETI_IS_AMPSHM_PREPARE_REP(sd, token)) {
        int imm = gasnetc_AMPSHM_PrepareReplyLong(sd, token, client_buf, min_length, max_length,
                                                  dest_addr, lc_opt, flags, nargs GASNETI_THREAD_PASS);
        if (imm) goto out_immediate;
    } else
#endif
    {
        size_t limit = gasnetc_Token_MaxReplyLong(token, lc_opt, flags, nargs);
        size_t size = MIN(max_length, limit);
        gasneti_prepare_reply_common(sd, token, client_buf, size, lc_opt, flags, nargs);
        sd->_dest_addr = dest_addr;
    }

    gasneti_init_sd_poison(sd);
    return gasneti_export_srcdesc(sd);

out_immediate:
    gasneti_reset_srcdesc(sd);
    return GEX_AM_SRCDESC_NO_OP;
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

    GASNETI_AMCOMMITREQUESTCOMMON(sd,handler,nbytes,NULL,nargs_arg,Medium);

    va_list argptr;
    va_start(argptr, sd_arg);
#if GASNET_PSHM
    if (GASNETI_IS_AMPSHM_COMMIT(sd)) {
        gasnetc_AMPSHM_CommitRequestMedium(sd, handler, nbytes, argptr);
    } else
#endif
    {   GASNET_POST_THREADINFO(GASNETI_THREAD_PASS_ALONE);
        gex_TM_t   tm          = sd->_dest._request._tm;
        gex_Rank_t dest        = sd->_dest._request._rank;
        void *src_addr         = sd->_addr;
        gex_Event_t *lc_opt    = sd->_lc_opt ? sd->_lc_opt : /* GASNet-owned buffer: */ GEX_EVENT_NOW;
        gex_Flags_t flags      = sd->_flags & ~GEX_FLAG_IMMEDIATE;
        unsigned int nargs     = sd->_nargs;

        int rc = gasneti_AMRequestMediumV(tm, dest, handler, src_addr, nbytes, lc_opt, flags, nargs, argptr);
        gasneti_assert(!rc); // IMMEDIATE is only permissible reason to return non-zero
    }
    va_end(argptr);

    gasneti_reset_srcdesc(sd);
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

    GASNETI_AMCOMMITREPLYCOMMON(sd,handler,nbytes,NULL,nargs_arg,Medium);

    va_list argptr;
    va_start(argptr, sd_arg);
#if GASNET_PSHM
    if (GASNETI_IS_AMPSHM_COMMIT(sd)) {
        gasnetc_AMPSHM_CommitReplyMedium(sd, handler, nbytes, argptr);
    } else
#endif
    {   GASNET_POST_THREADINFO(sd->_thread);
        gex_Token_t token      = sd->_dest._reply._token;
        void *src_addr         = sd->_addr;
        gex_Event_t *lc_opt    = sd->_lc_opt ? sd->_lc_opt : /* GASNet-owned buffer: */ GEX_EVENT_NOW;
        gex_Flags_t flags      = sd->_flags & ~GEX_FLAG_IMMEDIATE;
        unsigned int nargs     = sd->_nargs;

        int rc = gasneti_AMReplyMediumV(token, handler, src_addr, nbytes, lc_opt, flags, nargs, argptr);
        gasneti_assert(!rc); // IMMEDIATE is only permissible reason to return non-zero
    }
    va_end(argptr);

    gasneti_reset_srcdesc(sd);
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

    GASNETI_AMCOMMITREQUESTCOMMON(sd,handler,nbytes,dest_addr,nargs_arg,Long);

    va_list argptr;
    va_start(argptr, sd_arg);
#if GASNET_PSHM
    if (GASNETI_IS_AMPSHM_COMMIT(sd)) {
        gasnetc_AMPSHM_CommitRequestLong(sd, handler, nbytes, dest_addr, argptr);
    } else
#endif
    {   GASNET_POST_THREADINFO(GASNETI_THREAD_PASS_ALONE);
        gex_TM_t   tm          = sd->_dest._request._tm;
        gex_Rank_t dest        = sd->_dest._request._rank;
        void *src_addr         = sd->_addr;
        gex_Event_t *lc_opt    = sd->_lc_opt ? sd->_lc_opt : /* GASNet-owned buffer: */ GEX_EVENT_NOW;
        gex_Flags_t flags      = sd->_flags & ~GEX_FLAG_IMMEDIATE;
        unsigned int nargs     = sd->_nargs;

        int rc = gasneti_AMRequestLongV(tm, dest, handler, src_addr, nbytes, dest_addr, lc_opt, flags, nargs, argptr);
        gasneti_assert(!rc); // IMMEDIATE is only permissible reason to return non-zero
    }
    va_end(argptr);

    gasneti_reset_srcdesc(sd);
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

    GASNETI_AMCOMMITREPLYCOMMON(sd,handler,nbytes,dest_addr,nargs_arg,Long);

    va_list argptr;
    va_start(argptr, sd_arg);
#if GASNET_PSHM
    if (GASNETI_IS_AMPSHM_COMMIT(sd)) {
        gasnetc_AMPSHM_CommitReplyLong(sd, handler, nbytes, dest_addr, argptr);
    } else
#endif
    {   GASNET_POST_THREADINFO(sd->_thread);
        gex_Token_t token      = sd->_dest._reply._token;
        void *src_addr         = sd->_addr;
        gex_Event_t *lc_opt    = sd->_lc_opt ? sd->_lc_opt : /* GASNet-owned buffer: */ GEX_EVENT_NOW;
        gex_Flags_t flags      = sd->_flags & ~GEX_FLAG_IMMEDIATE;
        unsigned int nargs     = sd->_nargs;

        int rc = gasneti_AMReplyLongV(token, handler, src_addr, nbytes, dest_addr, lc_opt, flags, nargs, argptr);
        gasneti_assert(!rc); // IMMEDIATE is only permissible reason to return non-zero
    }
    va_end(argptr);

    gasneti_reset_srcdesc(sd);
}
#endif // gasnetc_AM_CommitReplyLongM

/* ------------------------------------------------------------------------------------ */

gasneti_lifo_head_t gasnetc_loopback_medium_pool = GASNETI_LIFO_INITIALIZER;

extern gex_TI_t gasnetc_nbrhd_Token_Info(
                gex_Token_t         token,
                gex_Token_Info_t    *info,
                gex_TI_t            mask)
{
  gasneti_assert(token);
  gasneti_assert(info);

  *info = ((gasnetc_nbrhd_token_t *)(1^(uintptr_t)token))->ti;
  gex_TI_t result = GEX_TI_SRCRANK | GEX_TI_EP | GEX_TI_ENTRY | GEX_TI_IS_REQ | GEX_TI_IS_LONG;
  return GASNETI_TOKEN_INFO_RETURN(result, info, mask);
}

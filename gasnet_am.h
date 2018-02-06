/*   $Source: bitbucket.org:berkeleylab/gasnet.git/gasnet_am.h $
 * Description: GASNet header for conduit-independent code for Active Messages
 * Copyright 2002, Dan Bonachea <bonachea@cs.berkeley.edu>
 * Copyright 2018, The Regents of the University of California
 * Terms of use are as specified in license.txt
 */

#ifndef _GASNET_AM_H
#define _GASNET_AM_H

/* ------------------------------------------------------------------------------------ */
/* common error-checking code for AM request/reply entry points */

// TODO-EX: GASNETI_CHECK_ERRR should *not* be returning the error code - need error handling callback instead

#define GASNETI_COMMON_AMREQUESTSHORT(tm,rank,handler,flags,numargs) do {      \
    GASNETI_CHECKATTACH();                                                     \
    gasneti_assert(numargs >= 0 && numargs <= gex_AM_MaxArgs());             \
    GASNETI_TRACE_AMREQUESTSHORT(tm,rank,handler,numargs);                     \
    GASNETI_CHECK_ERRR((rank >= gasneti_nodes),BAD_ARG,"node index too high"); \
  } while (0)
#define GASNETI_COMMON_AMREQUESTMEDIUM(tm,rank,handler,source_addr,nbytes,lc_opt,flags,numargs) do { \
    GASNETI_CHECKATTACH();                                                           \
    gasneti_assert(numargs >= 0 && numargs <= gex_AM_MaxArgs());                   \
    GASNETI_TRACE_AMREQUESTMEDIUM(tm,rank,handler,source_addr,nbytes,numargs);       \
    GASNETI_CHECK_ERRR((rank >= gasneti_nodes),BAD_ARG,"node index too high");       \
    GASNETI_CHECK_ERRR((nbytes > gex_AM_MaxRequestMedium(tm,rank,lc_opt,flags,numargs)),\
                       BAD_ARG,"nbytes too large");                                  \
    GASNETI_CHECK_ERRR((lc_opt == NULL),BAD_ARG,"lc_opt=NULL is invalid");           \
    GASNETI_CHECK_ERRR((lc_opt == GEX_EVENT_DEFER),BAD_ARG,"EVENT_DEFER is invalid for Requests"); \
  } while (0)
#define GASNETI_COMMON_AMREQUESTLONG(tm,rank,handler,source_addr,nbytes,dest_addr,lc_opt,flags,numargs) do { \
    GASNETI_CHECKATTACH();                                                                   \
    gasneti_assert(numargs >= 0 && numargs <= gex_AM_MaxArgs());                           \
    GASNETI_TRACE_AMREQUESTLONG(tm,rank,handler,source_addr,nbytes,dest_addr,numargs);       \
    GASNETI_CHECK_ERRR((rank >= gasneti_nodes),BAD_ARG,"node index too high");               \
    GASNETI_CHECK_ERRR((nbytes > gex_AM_MaxRequestLong(tm,rank,lc_opt,flags,numargs)),  \
                       BAD_ARG,"nbytes too large");                                  \
    GASNETI_CHECK_ERRR((lc_opt == NULL),BAD_ARG,"lc_opt=NULL is invalid");                   \
    GASNETI_CHECK_ERRR((lc_opt == GEX_EVENT_DEFER),BAD_ARG,"EVENT_DEFER is invalid for Requests"); \
  } while (0)
#define GASNETI_COMMON_AMREPLYSHORT(token,handler,flags,numargs) do {    \
    gasneti_assert(numargs >= 0 && numargs <= gex_AM_MaxArgs()); \
    GASNETI_TRACE_AMREPLYSHORT(token,handler,numargs);             \
  } while (0)
// TODO-EX: need to restore bounds-check on nbytes in GASNETI_COMMON_AMREPLYMEDIUM
#define GASNETI_COMMON_AMREPLYMEDIUM(token,handler,source_addr,nbytes,lc_opt,flags,numargs) do { \
    gasneti_assert(numargs >= 0 && numargs <= gex_AM_MaxArgs());                  \
    GASNETI_CHECK_ERRR((lc_opt == NULL),BAD_ARG,"lc_opt=NULL is invalid");          \
    GASNETI_CHECK_ERRR((lc_opt == GEX_EVENT_DEFER),BAD_ARG,"EVENT_DEFER is invalid for Replies"); \
    GASNETI_CHECK_ERRR((lc_opt == GEX_EVENT_GROUP),BAD_ARG,"EVENT_GROUP is invalid for Replies"); \
    GASNETI_TRACE_AMREPLYMEDIUM(token,handler,source_addr,nbytes,numargs);          \
  } while (0)
#if GASNET_DEBUG || GASNETI_ENABLE_ERRCHECKS
  // TODO-EX: need to restore bounds-check on nbytes _GASNETI_COMMON_AMREPLYLONG_CHECKS
  #define _GASNETI_COMMON_AMREPLYLONG_CHECKS(token,handler,source_addr,nbytes,dest_addr,lc_opt,flags,numargs) do { \
      GASNETI_CHECK_ERRR((lc_opt == NULL),BAD_ARG,"lc_opt=NULL is invalid");                          \
      GASNETI_CHECK_ERRR((lc_opt == GEX_EVENT_DEFER),BAD_ARG,"EVENT_DEFER is invalid for Replies");      \
      GASNETI_CHECK_ERRR((lc_opt == GEX_EVENT_GROUP),BAD_ARG,"EVENT_GROUP is invalid for Replies");    \
    } while (0)
#else
  #define _GASNETI_COMMON_AMREPLYLONG_CHECKS(token,handler,source_addr,nbytes,dest_addr,lc_opt,flags,numargs) ((void)0)
#endif
#define GASNETI_COMMON_AMREPLYLONG(token,handler,source_addr,nbytes,dest_addr,lc_opt,flags,numargs) do { \
    gasneti_assert(numargs >= 0 && numargs <= gex_AM_MaxArgs());                          \
    GASNETI_TRACE_AMREPLYLONG(token,handler,source_addr,nbytes,dest_addr,numargs);          \
    _GASNETI_COMMON_AMREPLYLONG_CHECKS(token,handler,source_addr,nbytes,dest_addr,lc_opt,flags,numargs); \
  } while (0)

/* ------------------------------------------------------------------------------------ */
/* utility macros for dispatching AM handlers */

typedef void (*gasneti_HandlerShort) (gex_Token_t token, ...);
typedef void (*gasneti_HandlerMedium)(gex_Token_t token, void *buf, size_t nbytes, ...);
typedef void (*gasneti_HandlerLong)  (gex_Token_t token, void *buf, size_t nbytes, ...);

/* ------------------------------------------------------------------------------------ */
#define GASNETI_RUN_HANDLER_SHORT(isReq, hid, phandlerfn, token, pArgs, numargs) do { \
  gasneti_assert(phandlerfn);                                                         \
  if (isReq) GASNETI_TRACE_AMSHORT_REQHANDLER(hid, token, numargs, pArgs);            \
  else       GASNETI_TRACE_AMSHORT_REPHANDLER(hid, token, numargs, pArgs);            \
  if (numargs == 0) (*(gasneti_HandlerShort)phandlerfn)((gex_Token_t)token);          \
  else {                                                                              \
    gex_AM_Arg_t *_args = (gex_AM_Arg_t *)(pArgs); /* eval only once */ \
    switch (numargs) {                                                                \
      case 1:  (*(gasneti_HandlerShort)phandlerfn)((gex_Token_t)token, _args[0]); break; \
      case 2:  (*(gasneti_HandlerShort)phandlerfn)((gex_Token_t)token, _args[0], _args[1]); break;\
      case 3:  (*(gasneti_HandlerShort)phandlerfn)((gex_Token_t)token, _args[0], _args[1], _args[2]); break; \
      case 4:  (*(gasneti_HandlerShort)phandlerfn)((gex_Token_t)token, _args[0], _args[1], _args[2], _args[3]); break; \
      case 5:  (*(gasneti_HandlerShort)phandlerfn)((gex_Token_t)token, _args[0], _args[1], _args[2], _args[3], _args[4]); break; \
      case 6:  (*(gasneti_HandlerShort)phandlerfn)((gex_Token_t)token, _args[0], _args[1], _args[2], _args[3], _args[4], _args[5]); break; \
      case 7:  (*(gasneti_HandlerShort)phandlerfn)((gex_Token_t)token, _args[0], _args[1], _args[2], _args[3], _args[4], _args[5], _args[6]); break; \
      case 8:  (*(gasneti_HandlerShort)phandlerfn)((gex_Token_t)token, _args[0], _args[1], _args[2], _args[3], _args[4], _args[5], _args[6], _args[7]); break; \
      case 9:  (*(gasneti_HandlerShort)phandlerfn)((gex_Token_t)token, _args[0], _args[1], _args[2], _args[3], _args[4], _args[5], _args[6], _args[7], _args[8]); break; \
      case 10: (*(gasneti_HandlerShort)phandlerfn)((gex_Token_t)token, _args[0], _args[1], _args[2], _args[3], _args[4], _args[5], _args[6], _args[7], _args[8], _args[9]); break; \
      case 11: (*(gasneti_HandlerShort)phandlerfn)((gex_Token_t)token, _args[0], _args[1], _args[2], _args[3], _args[4], _args[5], _args[6], _args[7], _args[8], _args[9], _args[10]); break; \
      case 12: (*(gasneti_HandlerShort)phandlerfn)((gex_Token_t)token, _args[0], _args[1], _args[2], _args[3], _args[4], _args[5], _args[6], _args[7], _args[8], _args[9], _args[10], _args[11]); break; \
      case 13: (*(gasneti_HandlerShort)phandlerfn)((gex_Token_t)token, _args[0], _args[1], _args[2], _args[3], _args[4], _args[5], _args[6], _args[7], _args[8], _args[9], _args[10], _args[11], _args[12]); break; \
      case 14: (*(gasneti_HandlerShort)phandlerfn)((gex_Token_t)token, _args[0], _args[1], _args[2], _args[3], _args[4], _args[5], _args[6], _args[7], _args[8], _args[9], _args[10], _args[11], _args[12], _args[13]); break; \
      case 15: (*(gasneti_HandlerShort)phandlerfn)((gex_Token_t)token, _args[0], _args[1], _args[2], _args[3], _args[4], _args[5], _args[6], _args[7], _args[8], _args[9], _args[10], _args[11], _args[12], _args[13], _args[14]); break; \
      case 16: (*(gasneti_HandlerShort)phandlerfn)((gex_Token_t)token, _args[0], _args[1], _args[2], _args[3], _args[4], _args[5], _args[6], _args[7], _args[8], _args[9], _args[10], _args[11], _args[12], _args[13], _args[14], _args[15]); break; \
      default: gasneti_fatalerror("Illegal numargs=%i in GASNETI_RUN_HANDLER_SHORT", (int)numargs);        \
      }                                                                                                    \
    }                                                                                                      \
    GASNETI_TRACE_PRINTF(A,("AM%s_SHORT_HANDLER: handler execution complete", (isReq?"REQUEST":"REPLY"))); \
  } while (0)
/* ------------------------------------------------------------------------------------ */
#define _GASNETI_RUN_HANDLER_MEDLONG(phandlerfn, token, pArgs, numargs, pData, datalen) do { \
  gasneti_assert(phandlerfn);                                                                \
  if (numargs == 0) (*phandlerfn)(token, pData, datalen);                                    \
  else {                                                                                     \
    gex_AM_Arg_t *_args = (gex_AM_Arg_t *)(pArgs); /* eval only once */        \
    switch (numargs) {                                                                       \
      case 1:  (*phandlerfn)(token, pData, datalen, _args[0]); break;                        \
      case 2:  (*phandlerfn)(token, pData, datalen, _args[0], _args[1]); break;              \
      case 3:  (*phandlerfn)(token, pData, datalen, _args[0], _args[1], _args[2]); break;    \
      case 4:  (*phandlerfn)(token, pData, datalen, _args[0], _args[1], _args[2], _args[3]); break; \
      case 5:  (*phandlerfn)(token, pData, datalen, _args[0], _args[1], _args[2], _args[3], _args[4]); break; \
      case 6:  (*phandlerfn)(token, pData, datalen, _args[0], _args[1], _args[2], _args[3], _args[4], _args[5]); break; \
      case 7:  (*phandlerfn)(token, pData, datalen, _args[0], _args[1], _args[2], _args[3], _args[4], _args[5], _args[6]); break; \
      case 8:  (*phandlerfn)(token, pData, datalen, _args[0], _args[1], _args[2], _args[3], _args[4], _args[5], _args[6], _args[7]); break; \
      case 9:  (*phandlerfn)(token, pData, datalen, _args[0], _args[1], _args[2], _args[3], _args[4], _args[5], _args[6], _args[7], _args[8]); break; \
      case 10: (*phandlerfn)(token, pData, datalen, _args[0], _args[1], _args[2], _args[3], _args[4], _args[5], _args[6], _args[7], _args[8], _args[9]); break; \
      case 11: (*phandlerfn)(token, pData, datalen, _args[0], _args[1], _args[2], _args[3], _args[4], _args[5], _args[6], _args[7], _args[8], _args[9], _args[10]); break; \
      case 12: (*phandlerfn)(token, pData, datalen, _args[0], _args[1], _args[2], _args[3], _args[4], _args[5], _args[6], _args[7], _args[8], _args[9], _args[10], _args[11]); break; \
      case 13: (*phandlerfn)(token, pData, datalen, _args[0], _args[1], _args[2], _args[3], _args[4], _args[5], _args[6], _args[7], _args[8], _args[9], _args[10], _args[11], _args[12]); break; \
      case 14: (*phandlerfn)(token, pData, datalen, _args[0], _args[1], _args[2], _args[3], _args[4], _args[5], _args[6], _args[7], _args[8], _args[9], _args[10], _args[11], _args[12], _args[13]); break; \
      case 15: (*phandlerfn)(token, pData, datalen, _args[0], _args[1], _args[2], _args[3], _args[4], _args[5], _args[6], _args[7], _args[8], _args[9], _args[10], _args[11], _args[12], _args[13], _args[14]); break; \
      case 16: (*phandlerfn)(token, pData, datalen, _args[0], _args[1], _args[2], _args[3], _args[4], _args[5], _args[6], _args[7], _args[8], _args[9], _args[10], _args[11], _args[12], _args[13], _args[14], _args[15]); break; \
      default: gasneti_fatalerror("Illegal numargs=%i in _GASNETI_RUN_HANDLER_MEDLONG", (int)numargs); \
      }                                                                                 \
    }                                                                                   \
  } while (0)

/* be default, we guarantee double-word alignment for data payload of medium xfers 
 */
#ifndef GASNETI_MEDBUF_ALIGNMENT
#define GASNETI_MEDBUF_ALIGNMENT 8
#endif

#define GASNETI_RUN_HANDLER_MEDIUM(isReq, hid, phandlerfn, token, pArgs, numargs, pData, datalen) do {      \
    gasneti_assert(((uintptr_t)pData) % GASNETI_MEDBUF_ALIGNMENT == 0 || datalen == 0);                     \
    if (isReq) GASNETI_TRACE_AMMEDIUM_REQHANDLER(hid, token, pData, datalen, numargs, pArgs);               \
    else       GASNETI_TRACE_AMMEDIUM_REPHANDLER(hid, token, pData, datalen, numargs, pArgs);               \
    _GASNETI_RUN_HANDLER_MEDLONG((gasneti_HandlerMedium)phandlerfn, (gex_Token_t)token,                     \
                                 pArgs, numargs, (void *)pData, (int)datalen);                              \
    GASNETI_TRACE_PRINTF(A,("AM%s_MEDIUM_HANDLER: handler execution complete", (isReq?"REQUEST":"REPLY"))); \
  } while (0)
#define GASNETI_RUN_HANDLER_LONG(isReq, hid, phandlerfn, token, pArgs, numargs, pData, datalen) do {      \
    if (isReq) GASNETI_TRACE_AMLONG_REQHANDLER(hid, token, pData, datalen, numargs, pArgs);               \
    else       GASNETI_TRACE_AMLONG_REPHANDLER(hid, token, pData, datalen, numargs, pArgs);               \
    _GASNETI_RUN_HANDLER_MEDLONG((gasneti_HandlerLong)phandlerfn, (gex_Token_t)token,                     \
                                 pArgs, numargs, (void *)pData, (int)datalen);                            \
    GASNETI_TRACE_PRINTF(A,("AM%s_LONG_HANDLER: handler execution complete", (isReq?"REQUEST":"REPLY"))); \
  } while (0)
/* ------------------------------------------------------------------------------------ */
/* AM handler registration and management */

typedef enum {
  gasneti_Short=0,
  gasneti_Medium=1,
  gasneti_Long=2
} gasneti_category_t;

/* default AM handler for unregistered entries - prints a fatal error */
extern void gasneti_defaultAMHandler(gex_Token_t token);

extern int gasneti_amtbl_init(gex_AM_Entry_t *output);
extern int gasneti_amregister( gex_AM_Entry_t *output,
                               gex_AM_Entry_t *input, int numentries,
                               int lowlimit, int highlimit,
                               int dontcare, int *numregistered);
extern int gasneti_amregister_client(gex_AM_Entry_t *output,
                                     gex_AM_Entry_t *input, size_t numentries);
extern int gasneti_amregister_legacy(gex_AM_Entry_t *output,
                                     gasnet_handlerentry_t *input, int numentries);

#if GASNET_DEBUG
  extern void gasneti_amtbl_check(const gex_AM_Entry_t *entry, int nargs,
                                  gasneti_category_t category, int isReq);
#else
  #define gasneti_amtbl_check(entry, nargs, category, isReq) ((void)0)
#endif

// AM "catch all" for defaultAMHandler and similar
#define GASNETI_FLAG_AM_ANY \
     ( GEX_FLAG_AM_SHORT|GEX_FLAG_AM_MEDIUM|GEX_FLAG_AM_LONG | \
       GEX_FLAG_AM_REQUEST|GEX_FLAG_AM_REPLY )

/* ------------------------------------------------------------------------------------ */
/* common logic for gex_Token_Info() */

// OR of all the required bits
#define GASNETI_TI_REQUIRED (GEX_TI_SRCRANK | GEX_TI_EP)

#if GASNET_DEBUG
  extern gex_TI_t gasneti_token_info_return(gex_TI_t result, gex_Token_Info_t * info, gex_TI_t mask);
  #define GASNETI_TOKEN_INFO_RETURN gasneti_token_info_return
#else
  #define GASNETI_TOKEN_INFO_RETURN(result, info, mask) (result)
#endif

/* ------------------------------------------------------------------------------------ */
/* common logic for Negotiated Payload AMs */

// Common argument processing
// TODO-EX: tracing should probably occur here as well
#if GASNET_DEBUG
  extern void gasneti_init_sd_poison(void *addr, size_t len);
  extern int gasneti_test_sd_poison(void *addr, size_t len);

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
  #define GASNETI_AMPREPREQUESTCOMMON(sd,tm,dest,cbuf,min_len,max_len,dest_addr,lc_opt,flags,nargs,cat) \
    do {                                                                                   \
      sd->_category  = (int)gasneti_##cat;                                                 \
      sd->_dest_addr = dest_addr;                                                          \
      sd->_nargs     = nargs;                                                              \
      size_t limit = gex_AM_MaxRequest##cat(tm,dest,lc_opt,flags,nargs);                   \
      if (dest >= gex_TM_QuerySize(tm))                                                    \
        gasneti_fatalerror("gex_AM_PrepareRequest" _STRINGIFY(cat) ": "                    \
                           "destination rank out-of-range (%lu >= %lu)",                   \
                           (unsigned long)dest, (unsigned long)gex_TM_QuerySize(tm));      \
      _GASNETI_CHECK_PREPARE(cbuf,min_len,max_len,limit,lc_opt,nargs,1,cat);             \
    } while(0)
  #define GASNETI_AMPREPREPLYCOMMON(sd,token,cbuf,min_len,max_len,dest_addr,lc_opt,flags,nargs,cat) \
    do {                                                                               \
      sd->_category  = (int)gasneti_##cat;                                             \
      sd->_dest_addr = dest_addr;                                                      \
      sd->_nargs     = nargs;                                                          \
      size_t limit = gasnetc_Token_MaxReply##cat(token,lc_opt,flags,nargs);            \
      _GASNETI_CHECK_PREPARE(cbuf,min_len,max_len,limit,lc_opt,nargs,0,cat);           \
    } while(0)

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
        if (gasneti_test_sd_poison(sd->_tofree, nbytes))                                                 \
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
  #define GASNETI_AMPREPREQUESTCOMMON(sd,tm,dest,cbuf,min,max,dest_addr,lc_opt,flags,nargs,cat) ((void)0)
  #define GASNETI_AMPREPREPLYCOMMON(sd,token,cbuf,minlen,maxlen,dest_addr,lc_opt,flags,nargs,cat) ((void)0)
  #define GASNETI_AMCOMMITREQUESTCOMMON(sd,handler,nbytes,dest_addr,nargs,cat) ((void)0)
  #define GASNETI_AMCOMMITREPLYCOMMON(sd,handler,nbytes,dest_addr,nargs,cat) ((void)0)
#endif

#ifndef _GEX_AM_SRCDESC_T
// Allocate a buffer (use IFF client_buf is NULL)
GASNETI_INLINE(gasneti_prepare_alloc_buffer)
void gasneti_prepare_alloc_buffer(gasneti_AM_SrcDesc_t sd)
{
    size_t size = sd->_size;
#if GASNET_DEBUG
    // Allocate at least one byte because zero-byte allocation
    // returns NULL which then leads to ambiguity in argument checking.
    if (!size) size = 1;
#endif
    sd->_addr   = gasneti_malloc(size);
    sd->_tofree = sd->_addr;
}

extern gasneti_AM_SrcDesc_t gasneti_init_srcdesc(int isreq GASNETI_THREAD_FARG);

// Get the thread-specfic SD for Requests, initializing on first call
GASNETI_INLINE(gasneti_init_request_srcdesc)
gasneti_AM_SrcDesc_t gasneti_init_request_srcdesc(GASNETI_THREAD_FARG_ALONE)
{
  void ** const mythread_ptrs = (void **)GASNETI_MYTHREAD;
  gasneti_AM_SrcDesc_t sd = mythread_ptrs[4]; // 5th pointer
  if_pf (!sd) { sd = gasneti_init_srcdesc(1 GASNETI_THREAD_PASS); }
  GASNETI_CHECK_MAGIC(sd, GASNETI_AM_SRCDESC_BAD_MAGIC); // Would catch nested prepare
  GASNETI_INIT_MAGIC(sd, GASNETI_AM_SRCDESC_MAGIC);
  sd->_tofree = NULL;
  return sd;
}

// Get the thread-specfic SD for Replies, initializing on first call
GASNETI_INLINE(gasneti_init_reply_srcdesc)
gasneti_AM_SrcDesc_t gasneti_init_reply_srcdesc(GASNETI_THREAD_FARG_ALONE)
{
  void ** const mythread_ptrs = (void **)GASNETI_MYTHREAD;
  gasneti_AM_SrcDesc_t sd = mythread_ptrs[3]; // 4th pointer
  if_pf (!sd) { sd = gasneti_init_srcdesc(0 GASNETI_THREAD_PASS); }
  GASNETI_CHECK_MAGIC(sd, GASNETI_AM_SRCDESC_BAD_MAGIC); // Would catch nested prepare
  GASNETI_INIT_MAGIC(sd, GASNETI_AM_SRCDESC_MAGIC);
  sd->_tofree = NULL;
  return sd;
}

// Return a thread-specfic SD to its "inactive" state
GASNETI_INLINE(gasneti_reset_srcdesc)
void gasneti_reset_srcdesc(gasneti_AM_SrcDesc_t sd)
{
  gasneti_free(sd->_tofree);
  GASNETI_INIT_MAGIC(sd, GASNETI_AM_SRCDESC_BAD_MAGIC);
}

GASNETI_INLINE(gasneti_prepare_common)
void gasneti_prepare_common(
                       gasneti_AM_SrcDesc_t sd,
                       const void          *client_buf,
                       size_t               size,
                       gex_Event_t         *lc_opt,
                       gex_Flags_t          flags,
                       unsigned int         nargs)
{
    sd->_lc_opt = lc_opt;
    sd->_flags  = flags;
    sd->_nargs  = nargs;
    sd->_size   = size;
    if (client_buf) {
        sd->_addr = (/*non-const*/void *)client_buf;
    } else {
        gasneti_prepare_alloc_buffer(sd);
        gasneti_init_sd_poison(sd->_addr, size);
    }
}

GASNETI_INLINE(gasneti_prepare_request_common)
void gasneti_prepare_request_common(
                       gasneti_AM_SrcDesc_t sd,
                       gex_TM_t             tm,
                       gex_Rank_t           dest,
                       const void          *client_buf,
                       size_t               size,
                       gex_Event_t         *lc_opt,
                       gex_Flags_t          flags,
                       unsigned int         nargs)
{
    sd->_dest._request._tm   = tm;
    sd->_dest._request._rank = dest;
    gasneti_prepare_common(sd, client_buf, size, lc_opt, flags, nargs);
}

GASNETI_INLINE(gasneti_prepare_reply_common)
void gasneti_prepare_reply_common(
                       gasneti_AM_SrcDesc_t sd,
                       gex_Token_t          token,
                       const void          *client_buf,
                       size_t               size,
                       gex_Event_t         *lc_opt,
                       gex_Flags_t          flags,
                       unsigned int         nargs)
{
    sd->_dest._reply._token = token;
    gasneti_prepare_common(sd, client_buf, size, lc_opt, flags, nargs);
}
#endif // _GEX_AM_SRCDESC_T

/*
  INTERNAL Active Message Request/Reply w/ va_list
  ================================================
  NOTE:
    These functions do not TRACE or perform complete argument validation.
    These Request functions do not AMPoll.
    Those are the caller's responsibility.
*/

extern int gasnetc_AMRequestMediumV(
                gex_TM_t tm, gex_Rank_t rank, gex_AM_Index_t handler,
                void *source_addr, size_t nbytes,
                gex_Event_t *lc_opt, gex_Flags_t flags,
                int numargs, va_list argptr GASNETI_THREAD_FARG);
#define gasneti_AMRequestMediumV(tm,rank,hidx,src_addr,nbytes,lc_opt,flags,nargs,args) \
        gasnetc_AMRequestMediumV(tm,rank,hidx,src_addr,nbytes,lc_opt,flags,nargs,args GASNETI_THREAD_GET)
extern int gasnetc_AMRequestLongV(
                gex_TM_t tm, gex_Rank_t rank, gex_AM_Index_t handler,
                void *source_addr, size_t nbytes, void *dest_addr,
                gex_Event_t *lc_opt, gex_Flags_t flags,
                int numargs, va_list argptr GASNETI_THREAD_FARG);
#define gasneti_AMRequestLongV(tm,rank,hidx,src_addr,nbytes,dst_addr,lc_opt,flags,nargs,args) \
        gasnetc_AMRequestLongV(tm,rank,hidx,src_addr,nbytes,dst_addr,lc_opt,flags,nargs,args GASNETI_THREAD_GET)

extern int gasnetc_AMReplyMediumV(
                gex_Token_t token, gex_AM_Index_t handler,
                void *source_addr, size_t nbytes,
                gex_Event_t *lc_opt, gex_Flags_t flags,
                int numargs, va_list argptr GASNETI_THREAD_FARG);
#define gasneti_AMReplyMediumV(token,hidx,src_addr,nbytes,lc_opt,flags,nargs,args) \
        gasnetc_AMReplyMediumV(token,hidx,src_addr,nbytes,lc_opt,flags,nargs,args GASNETI_THREAD_GET)
extern int gasnetc_AMReplyLongV(
                gex_Token_t token, gex_AM_Index_t handler,
                void *source_addr, size_t nbytes, void *dest_addr,
                gex_Event_t *lc_opt, gex_Flags_t flags,
                int numargs, va_list argptr GASNETI_THREAD_FARG);
#define gasneti_AMReplyLongV(token,hidx,src_addr,nbytes,dst_addr,lc_opt,flags,nargs,args) \
        gasnetc_AMReplyLongV(token,hidx,src_addr,nbytes,dst_addr,lc_opt,flags,nargs,args GASNETI_THREAD_GET)

/* ------------------------------------------------------------------------------------ */
#endif

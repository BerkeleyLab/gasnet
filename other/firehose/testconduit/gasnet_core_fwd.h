#ifndef _IN_GASNET_H
  #error This file is not meant to be included directly- clients should include gasnet.h
#endif

#ifndef _GASNET_CORE_FWD_H
#define _GASNET_CORE_FWD_H
#define GASNET_CONDUIT_SMP  1 /* HACK HACK HACK */

#define GASNET_CORE_VERSION      1.5
#define GASNET_CORE_VERSION_STR  _STRINGIFY(GASNET_CORE_VERSION)
#define GASNET_CORE_NAME         FIREHOSETEST
#define GASNET_CORE_NAME_STR     _STRINGIFY(GASNET_CORE_NAME)

#define GASNET_CONDUIT_FIREHOSETEST 1

#define GASNET_ALIGNED_SEGMENTS 1
 
#define GASNETI_USE_TRUE_MUTEXES 1

#define CONDUIT_CORE_STATS(CNT,VAL,TIME) 

#define _GASNET_TOKEN_T
struct _gasnetc_sockmap;
typedef struct _gasnetc_sockmap *gasnet_token_t;

typedef void (*gasnetc_handler_fn_t)();

#endif /* _GASNET_CORE_FWD_H */

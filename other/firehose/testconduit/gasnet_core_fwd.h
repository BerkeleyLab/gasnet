/*   $Source: /Users/kamil/work/gasnet-cvs2/gasnet/other/firehose/testconduit/Attic/gasnet_core_fwd.h,v $
 *     $Date: 2004/08/26 04:53:59 $
 * $Revision: 1.3 $
 * Description: 
 * Copyright 2004, Christian Bell <csbell@cs.berkeley.edu>
 * Terms of use are as specified in license.txt
 */
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

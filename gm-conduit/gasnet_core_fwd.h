/* $Id: gasnet_core_fwd.h,v 1.12.2.1 2003/01/18 22:07:47 csbell Exp $
 * $Date: 2003/01/18 22:07:47 $
 * $Revision: 1.12.2.1 $
 * Description: GASNet header for GM conduit core (forward definitions)
 * Copyright 2002, Christian Bell <csbell@cs.berkeley.edu>
 * Copyright 2002, Dan Bonachea <bonachea@cs.berkeley.edu>
 */

#ifndef _IN_GASNET_H
  #error This file is not meant to be included directly- clients should include gasnet.h
#endif

#ifndef _GASNET_CORE_FWD_H
#define _GASNET_CORE_FWD_H

#define GASNET_CORE_VERSION      0.1
#define GASNET_CORE_VERSION_STR  _STRINGIFY(GASNET_CORE_VERSION)
#define GASNET_CORE_NAME         GM
#define GASNET_CORE_NAME_STR     _STRINGIFY(GASNET_CORE_NAME)

  /*  defined to be 1 if gasnet_init guarantees that the remote-access memory segment will be aligned  */
  /*  at the same virtual address on all nodes. defined to 0 otherwise */
#define GASNET_ALIGNED_SEGMENTS	1
#define GASNET_MAXNODES		1024

#if !defined(FREEBSD) && !defined(PAGE_SIZE)
#define PAGE_SIZE	4096
#endif

#if PAGE_SIZE == 4096
#define PAGE_SHIFT	12
#elif PAGE_SIZE == 8192
#define PAGE_SHIFT	13
#endif

/* only have firehose for now */
#define GASNETC_FIREHOSE
#if !defined(GASNETC_RV_NO_UNPIN) && !defined(GASNETC_RV_WITH_UNPIN)
#error please defined either GASNETC_RV_NO_UNPIN or GASNETC_RV_WITH_UNPIN
#elif defined(GASNETC_RV_NO_UNPIN) && defined(GASNETC_RV_WITH_UNPIN)
#error please defined one of GASNETC_RV_NO_UNPIN or GASNETC_RV_WITH_UNPIN
#endif

  /* this can be used to add conduit-specific 
     statistical collection values (see gasnet_help.h) */
#define CONDUIT_CORE_STATS(CNT,VAL,TIME) 

#define _GASNET_NODE_T
typedef uint16_t	gasnet_node_t;
#define _GASNET_HANDLER_T
typedef uint8_t		gasnet_handler_t;

#define _GASNET_TOKEN_T
struct gasnetc_bufdesc;
typedef struct gasnetc_bufdesc *gasnet_token_t;
typedef struct gasnetc_bufdesc gasnetc_op_t;

#endif /* _GASNET_CORE_FWD_H */

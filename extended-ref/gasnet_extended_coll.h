/*  $Archive:: /Ti/GASNet/extended/gasnet_extended_coll.h                 $
 *     $Date: 2004/03/31 23:28:27 $
 * $Revision: 1.1.2.1 $
 * Description: GASNet Extended API Collective declarations
 * Copyright 2004, Dan Bonachea <bonachea@cs.berkeley.edu>
 * Terms of use are as specified in license.txt
 */

#ifndef _IN_GASNET_H
  #error This file is not meant to be included directly- clients should include gasnet.h
#endif

#ifndef _GASNET_EXTENDED_COLL_H
#define _GASNET_EXTENDED_COLL_H

/*---------------------------------------------------------------------------------*/

/* Handle type for collectives: */
struct gasnete_coll_op_t_;	/* Forward type declaration in C */
typedef struct gasnete_coll_op_t_ *gasnet_coll_handle_t;

/*---------------------------------------------------------------------------------*/
#endif

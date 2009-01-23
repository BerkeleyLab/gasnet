/*   $Source: /Users/kamil/work/gasnet-cvs2/gasnet/dcmf-conduit/Attic/gasnet_dcmf_internal.c,v $
 *     $Date: 2009/01/23 20:37:58 $
 * $Revision: 1.2.6.2 $
 * Description: GASNet dcmf conduit Implementation
 * Copyright 2008, Rajesh Nishtala <rajeshn@cs.berkeley.edu>, 
                   Dan Bonachea <bonachea@cs.berkeley.edu>
 * Terms of use are as specified in license.txt
 */

#include <gasnet_core_internal.h>

#define PRINT_NODE_INFO 1
/*preallocate request objects and tokens and put them on the free list*/


/*   $Source: /Users/kamil/work/gasnet-cvs2/gasnet/other/mpi-spawner/gasnet_bootstrap_internal.h,v $
 *     $Date: 2005/01/15 00:23:20 $
 * $Revision: 1.1 $
 * Description: GASNet conduit-independent mpi-based spawner (prototypes)
 * Copyright 2005, The Regents of the University of California
 * Terms of use are as specified in license.txt
 */

#ifndef _GASNET_BOOTSTAP_INTERNAL_H
#define _GASNET_BOOTSTAP_INTERNAL_H

extern void gasneti_bootstrapInit(int *argc_p, char ***argv_p,
                                  gasnet_node_t *nodes_p, gasnet_node_t *mynode_p);
extern void gasneti_bootstrapFini(void);
extern void gasneti_bootstrapAbort(int exitcode) GASNET_NORETURN;
extern void gasneti_bootstrapBarrier(void);
extern void gasneti_bootstrapExchange(void *src, size_t len, void *dest);
extern void gasneti_bootstrapAlltoall(void *src, size_t len, void *dest);
extern void gasneti_bootstrapBroadcast(void *src, size_t len, void *dest, int rootnode);

#endif

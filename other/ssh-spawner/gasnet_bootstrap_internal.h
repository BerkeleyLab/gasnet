/*   $Source: /Users/kamil/work/gasnet-cvs2/gasnet/other/ssh-spawner/gasnet_bootstrap_internal.h,v $
 *     $Date: 2005/04/17 15:44:13 $
 * $Revision: 1.1.2.2 $
 * Description: GASNet conduit-independent ssh-based spawner (prototypes)
 * Copyright 2005, The Regents of the University of California
 * Terms of use are as specified in license.txt
 */

#ifndef _GASNET_BOOTSTRAP_INTERNAL_SSH_H
#define _GASNET_BOOTSTRAP_INTERNAL_SSH_H

extern void gasneti_bootstrapInit_ssh(int *argc_p, char ***argv_p,
                                      gasnet_node_t *nodes_p, gasnet_node_t *mynode_p);
extern void gasneti_bootstrapFini_ssh(void);
extern void gasneti_bootstrapAbort_ssh(int exitcode) GASNET_NORETURN;
extern void gasneti_bootstrapBarrier_ssh(void);
extern void gasneti_bootstrapExchange_ssh(void *src, size_t len, void *dest);
extern void gasneti_bootstrapAlltoall_ssh(void *src, size_t len, void *dest);
extern void gasneti_bootstrapBroadcast_ssh(void *src, size_t len, void *dest, int rootnode);

#endif

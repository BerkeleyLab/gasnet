/*   $Source: /Users/kamil/work/gasnet-cvs2/gasnet/other/ssh-spawner/Attic/gasnet_bootstrap_fwd.h,v $
 *     $Date: 2005/01/15 00:23:22 $
 * $Revision: 1.1 $
 * Description: GASNet conduit-independent ssh-based spawner (fwd decls)
 * Copyright 2005, The Regents of the University of California
 * Terms of use are as specified in license.txt
 */

#ifndef _IN_GASNET_H
  #error This file is not meant to be included directly- clients should include gasnet.h
#endif

#ifndef _GASNET_BOOTSTRAP_FWD_H
#define _GASNET_BOOTSTRAP_FWD_H

#define GASNETI_CONDUIT_GETENV(X) gasneti_bootstrapGetenv(X)
extern char *gasneti_bootstrapGetenv(const char *var);

#endif

/*   $Source: bitbucket.org:berkeleylab/gasnet.git/extended-ref/gasnet_ratomic_fwd.h $
 * Description: GASNet Remote Atomics API Header (forward decls)
 * Copyright 2017, The Regents of the University of California
 * Terms of use are as specified in license.txt
 */

#ifndef _IN_GASNETEX_H
  #error This file is not meant to be included directly- clients should include gasnetex.h
#endif

#ifndef _GASNET_RATOMIC_FWD_H
#define _GASNET_RATOMIC_FWD_H

/* stats needed by the RAtomic reference implementation */
#ifndef GASNETI_RATOMIC_STATS
  #define GASNETI_RATOMIC_STATS(CNT,VAL,TIME)    \
        /* Currently empty */
#endif

#endif // _GASNET_RATOMIC_FWD_H

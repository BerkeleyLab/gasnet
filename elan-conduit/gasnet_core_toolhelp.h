/*   $Source: /Users/kamil/work/gasnet-cvs2/gasnet/elan-conduit/Attic/gasnet_core_toolhelp.h,v $
 *     $Date: 2006/04/18 04:37:10 $
 * $Revision: 1.2 $
 * Description: GASNet header for elan conduit core (gasnet_tools helper)
 * Copyright 2002, Dan Bonachea <bonachea@cs.berkeley.edu>
 * Terms of use are as specified in license.txt
 */

#ifndef _GASNET_CORE_TOOLHELP_H
#define _GASNET_CORE_TOOLHELP_H

/* use ELAN-specific high-performance nanosecond timer -
   currently only a win on Alpha, where the native timer support is poor
 */
#if defined(__alpha__) || defined(__alpha) || \
    defined(GASNETC_FORCE_ELAN_TIMERS)
  #define GASNETC_CONDUIT_SPECIFIC_TIMERS
  typedef uint64_t gasneti_tick_t;
  extern uint64_t gasnetc_clock();
  #define gasneti_ticks_now()      (gasnetc_clock())
  #define gasneti_ticks_to_ns(st)  (st)
#endif


#endif /* _GASNET_CORE_TOOLHELP_H */

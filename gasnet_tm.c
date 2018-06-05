/*   $Source: bitbucket.org:berkeleylab/gasnet.git/gasnet_team.c $
 * Description: GASNet implementation of teams
 * Copyright 2018, The Regents of the University of California
 * Terms of use are as specified in license.txt
 */

#include <gasnet_internal.h>
#include <gasnet_coll_internal.h>

// TODO-EX:
//
// Currently we implement a "team" (the abstract distributed collection, of
// which a "tm" is a representative) in terms of the legacy GASNet-1 collective
// team.  We should eventually using something smarter, including scalable
// storage.

// Given (tm,rank) return the jobrank
gex_Rank_t gasneti_tm_fwd_lookup(gasneti_TM_t tm, gex_Rank_t rank) {
  gasnete_coll_team_t team = tm->_coll_team;
  gasneti_assert(team != GASNET_TEAM_ALL); // TM0 should not reach here
  return team->rel2act_map[rank];
}

// Given (tm,jobrank) return the rank or GEX_RANK_INVALID
// TODO-EX: Should at least avoid letting TM0 reach here
// TODO-EX: THIS IS A HORRIBLE O(size(tm)) SCAN!
gex_Rank_t gasneti_tm_rev_lookup(gasneti_TM_t tm, gex_Rank_t jobrank) {
  gasnete_coll_team_t team = tm->_coll_team;
  gex_Rank_t size = tm->_size;
  gasneti_assert(size == team->total_ranks);
  for (gex_Rank_t rank = 0; rank < size; ++rank) {
    if (team->rel2act_map[rank] == jobrank) return rank;
  }
  return GEX_RANK_INVALID;
}

/* ------------------------------------------------------------------------------------ */
/* TM trace formatting - legal even without STATS/TRACE */

// Format a gex_TM_t as a GUID
extern const char *gasneti_formattm(gex_TM_t e_tm) {
  if ((uintptr_t)e_tm == 1)     return "N/A";  // GASNet-1 collectives team
  if (e_tm == NULL)             return "JOB";  // JobRank, as with token
  gasnete_coll_team_t team = gasneti_import_tm(e_tm)->_coll_team;
  if (team == NULL)             return "TM0";  // Team0 before end of Client_Init
  return gasneti_dynsprintf("TM%x", (unsigned int)team->team_id);
}

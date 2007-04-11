/* 
* Description: Files for Autotuner
 * Copyright 2007, Rajesh Nishtala <rajeshn@eecs.berkeley.edu> Dan Bonachea <bonachea@cs.berkeley.edu>
 * Terms of use are as specified in license.txt
 */

/* This is intended as a stub for the autotuner routines*/

#include "gasnet_coll_autotune.h"

gasnete_coll_tree_class_t gasnete_coll_current_tree_kind;
int gasnete_coll_current_fanout;


/* These "set" routines are only intended for testing purposes. Eventually 
   The "get tree" routines will be the primary method of picking trees*/
void gasnet_coll_set_tree_class(char *str) {
  if(strcmp(str, "GASNET_BINOMIAL_TREE")==0) {
    gasnete_coll_current_tree_kind = GASNETE_COLL_BINOMIAL_TREE;
  } else if(strcmp(str, "GASNET_NARY_TREE")==0) {
    gasnete_coll_current_tree_kind = GASNETE_COLL_NARY_TREE;
  } else if(strcmp(str, "GASNET_DFS_RECURSIVE_TREE")==0) {
    gasnete_coll_current_tree_kind = GASNETE_COLL_DFS_RECURSIVE_TREE;
  } else if(strcmp(str, "GASNET_REV_RECURSIVE_TREE")==0) {
    gasnete_coll_current_tree_kind = GASNETE_COLL_REV_RECURSIVE_TREE;
  } else {
    gasneti_fatalerror("Unknown Tree Type: %s\n", str);
  }
}
void gasnet_coll_set_fanout(int fanout) {
  gasnete_coll_current_fanout = fanout;
}
gasnete_coll_tree_type_t gasnete_coll_get_current_tree_kind() {
  /* A LOT OF AUTOTUNING STUFF HERE*/
  gasnete_coll_tree_type_t ret;
  ret.tree_class = gasnete_coll_current_tree_kind;
  ret.fanout = gasnete_coll_current_fanout;
  return ret;
}


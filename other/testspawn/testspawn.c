/*   $Source: /Users/kamil/work/gasnet-cvs2/gasnet/other/testspawn/testspawn.c,v $
 *     $Date: 2005/02/17 13:19:11 $
 * $Revision: 1.6 $
 * Description: 
 * Copyright 2005, Regents of the University of California
 * Terms of use are as specified in license.txt
 */

#include <stdio.h>

#include <gasnet_internal.h>
#include <gasnet_core_internal.h>

int main(int argc, char **argv) {
  gasnet_node_t *array1, *array2;
  gasnet_node_t i, j;

  gasneti_bootstrapInit(&argc, &argv, &gasneti_nodes, &gasneti_mynode);
  gasneti_bootstrapBarrier();
  array1 = gasneti_calloc(gasneti_nodes, sizeof(gasnet_node_t));
  array2 = gasneti_calloc(gasneti_nodes, sizeof(gasnet_node_t));

  printf("Hello from node %d of %d\n", (int)gasneti_mynode, (int)gasneti_nodes);
  fflush(stdout);

  /* Test bootstrapBroadcast */
  gasneti_bootstrapBarrier();
  if (!gasneti_mynode) {
    printf("Testing bootstrapBroadcast()...\n");
    fflush(stdout);
  }
  for (i=0; i<gasneti_nodes; ++i) {
    gasneti_bootstrapBroadcast(&gasneti_mynode, sizeof(gasnet_node_t), &j, i);
    if (j != i) {
      gasneti_fatalerror("gasneti_bootstrapBroadcast(root=%d) failed", (int)i);
    }
  }
  gasneti_bootstrapBarrier();
  if (!gasneti_mynode) {
    printf("bootstrapBroadcast() OK\n");
    fflush(stdout);
  }

  /* Test bootstrapExchange */
  gasneti_bootstrapBarrier();
  if (!gasneti_mynode) {
    printf("Testing bootstrapExchange()...\n");
    fflush(stdout);
  }
  gasneti_bootstrapExchange(&gasneti_mynode, sizeof(gasnet_node_t), array1);
  for (i=0; i<gasneti_nodes; ++i) {
    if (array1[i] != i) {
      gasneti_fatalerror("gasneti_bootstrapExchange failed");
    }
  }
  gasneti_bootstrapBarrier();
  if (!gasneti_mynode) {
    printf("bootstrapExchange() OK\n");
    fflush(stdout);
  }
 
  /* Test bootstrapAlltoall */
  gasneti_bootstrapBarrier();
  if (!gasneti_mynode) {
    printf("Testing bootstrapAlltoall()...\n");
    fflush(stdout);
  }
  for (i=0; i<gasneti_nodes; ++i) {
    array1[i] = i + 100*gasneti_mynode;
  }
  gasneti_bootstrapAlltoall(array1, sizeof(gasnet_node_t), array2);
  for (i=0; i<gasneti_nodes; ++i) {
    if (array2[i] != gasneti_mynode + 100*i) {
      gasneti_fatalerror("gasneti_bootstrapAlltoall() failed");
    }
  }
  gasneti_bootstrapBarrier();
  if (!gasneti_mynode) {
    printf("bootstrapAlltoall() OK\n");
    fflush(stdout);
  }

  gasneti_bootstrapBarrier();
  gasneti_bootstrapFini();

  return 0;
}

/*   $Source: /Users/kamil/work/gasnet-cvs2/gasnet/tests/delay.c,v $
 *     $Date: 2005/04/04 03:33:27 $
 * $Revision: 1.5.4.1 $
 * Description: 
 * Copyright 2004, Paul Hargrove <PHHargrove@lbl.gov>
 * Terms of use are as specified in license.txt
 */
#include <float.h>
#include <gasnet_tools.h>

static volatile float x, y;
static volatile float z = (1.00001);

float test_bogus() { /* ensure the values escape (otherwise x is dead) */
 return x+y+z;
}

                                                                                                              
void test_delay (int64_t n)
{
  int64_t i;


  y = z;
  x = 1.0;
  for (i=0; i<n; i++) { x *= y; }
}

#include <float.h>
#include <gasnet_tools.h>

static float x, y;
static volatile float z = (1.0 + FLT_EPSILON);

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

#include <float.h>

static float x, y;
static volatile float z = (1.0 + FLT_EPSILON);

void delay (int n)
{
  int i;


  y = z;
  x = 1.0;
  for (i=0; i<n; i++) { x *= y; }
}
